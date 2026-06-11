// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file pmic.c
 * @brief STPMIC2 power-management IC (on I2C7, address 0x33).
 * @copyright 2026 Jakob Kastelic
 *
 * Register map and voltage encodings from TF-A drivers/st/pmic/stpmic2.c and
 * include/drivers/st/stpmic2.h (BSD-3-Clause). The rail set and targets come
 * from the STM32MP257F-DK device tree (fdts/stm32mp257f-dk.dts, &pmic2).
 *
 * Rail init mirrors what TF-A BL2 does on this board: switch on every
 * regulator-always-on rail, leaving its voltage at the PMIC NVM default
 * (TF-A's regulator_enable_always_on() sets no voltages either), then run
 * the LPDDR4 power-on sequence (plat_ddr.c ddr_power_init): program
 * vdd1_ddr/vdd2_ddr and enable vdd1 (ldo3) before vdd2/vddq (buck6).
 */

#include "pmic.h"
#include "i2c.h"
#include "printf.h"

#define PMIC_ADDR 0x33U

/* BUCKx: MAIN_CR1 = voltage index (7 bits), MAIN_CR2 bit0 = enable. */
#define BUCK1_MAIN_CR1 0x20U
#define BUCK2_MAIN_CR1 0x25U
#define BUCK3_MAIN_CR1 0x2AU
#define BUCK4_MAIN_CR1 0x2FU
#define BUCK5_MAIN_CR1 0x34U
#define BUCK6_MAIN_CR1 0x39U
#define BUCK7_MAIN_CR1 0x3EU

/* LDOx: one MAIN_CR = enable bit0, voltage index bits 5:1, bypass bit6. */
#define LDO1_MAIN_CR 0x4CU
#define LDO2_MAIN_CR 0x4FU
#define LDO3_MAIN_CR 0x52U
#define LDO4_MAIN_CR 0x55U
#define LDO5_MAIN_CR 0x58U
#define LDO7_MAIN_CR 0x5EU
#define LDO8_MAIN_CR 0x61U

#define BUCK_VOLT_MASK 0x7FU
#define LDO_VOLT_MASK  0x3EU /* index bits 5:1 */
#define LDO_BYPASS     0x40U
#define RAIL_EN        0x01U

/* Buck index <-> mV: 0..100 = 500+10*idx, 100..127 = 1500+100*(idx-100). */
#define BUCK_LOW_IDX(mv) ((uint8_t)(((mv) - 500U) / 10U))
/* LDO (ldo2/3/5/7/8) index <-> mV: 900+100*idx, 5 bits. */
#define LDO_IDX(mv) ((uint8_t)(((mv) - 900U) / 100U))

#define VDD1_DDR_MV 1800U /* ldo3, DK dts vdd1_ddr */
#define VDD2_DDR_MV 1100U /* buck6, DK dts vdd2_ddr */

enum rail_kind {
   RAIL_BUCK,      /* MAIN_CR1 index, MAIN_CR2 enable */
   RAIL_LDO,       /* MAIN_CR, 900..4000 mV table */
   RAIL_LDO_FIXED, /* MAIN_CR, fixed output (ldo1 = 1800, ldo4 = 3300) */
};

struct rail {
   const char *name;   /* PMIC regulator */
   const char *signal; /* board net (DK device tree regulator-name) */
   uint8_t cr;         /* BUCKx_MAIN_CR1 or LDOx_MAIN_CR */
   uint8_t kind;
   uint16_t fixed_mv;  /* RAIL_LDO_FIXED output */
   uint8_t always_on;  /* regulator-always-on in the DK device tree */
};

static const struct rail rails[] = {
   {"buck1", "vddcpu",       BUCK1_MAIN_CR1, RAIL_BUCK,      0U,    1U},
   {"buck2", "vddcore",      BUCK2_MAIN_CR1, RAIL_BUCK,      0U,    1U},
   {"buck3", "vddgpu",       BUCK3_MAIN_CR1, RAIL_BUCK,      0U,    1U},
   {"buck4", "vddio_pmic",   BUCK4_MAIN_CR1, RAIL_BUCK,      0U,    1U},
   {"buck5", "v1v8",         BUCK5_MAIN_CR1, RAIL_BUCK,      0U,    1U},
   {"buck6", "vdd2_ddr",     BUCK6_MAIN_CR1, RAIL_BUCK,      0U,    0U},
   {"buck7", "v3v3",         BUCK7_MAIN_CR1, RAIL_BUCK,      0U,    1U},
   {"ldo1",  "vdda1v8_aon",  LDO1_MAIN_CR,   RAIL_LDO_FIXED, 1800U, 1U},
   {"ldo2",  "vdd_emmc",     LDO2_MAIN_CR,   RAIL_LDO,       0U,    1U},
   {"ldo3",  "vdd1_ddr",     LDO3_MAIN_CR,   RAIL_LDO,       0U,    0U},
   {"ldo4",  "vdd3v3_usb",   LDO4_MAIN_CR,   RAIL_LDO_FIXED, 3300U, 1U},
   {"ldo5",  "v5v_hdmi",     LDO5_MAIN_CR,   RAIL_LDO,       0U,    0U},
   {"ldo7",  "vdd_sdcard",   LDO7_MAIN_CR,   RAIL_LDO,       0U,    1U},
   {"ldo8",  "vddio_sdcard", LDO8_MAIN_CR,   RAIL_LDO,       0U,    1U},
};

#define RAIL_COUNT (sizeof(rails) / sizeof(rails[0]))

int pmic_read(uint8_t reg, uint8_t *val)
{
   return i2c_read_reg(PMIC_ADDR, reg, val);
}

int pmic_write(uint8_t reg, uint8_t val)
{
   return i2c_write_reg(PMIC_ADDR, reg, val);
}

/* Read-modify-write the bits of @mask (TF-A stpmic2_register_update). */
static int pmic_update(uint8_t reg, uint8_t val, uint8_t mask)
{
   uint8_t v = 0;

   if (pmic_read(reg, &v) != 0) {
      return -1;
   }
   v = (uint8_t)((v & (uint8_t)~mask) | (val & mask));
   return pmic_write(reg, v);
}

/* Enable register: bucks use MAIN_CR2 = MAIN_CR1 + 1, LDOs the MAIN_CR. */
static uint8_t rail_en_cr(const struct rail *r)
{
   return (r->kind == RAIL_BUCK) ? (uint8_t)(r->cr + 1U) : r->cr;
}

int pmic_init_rails(void)
{
   int err = 0;

   /* Always-on rails: enable only, voltages stay at the NVM defaults. */
   for (size_t i = 0; i < RAIL_COUNT; i++) {
      if (rails[i].always_on) {
         err |= pmic_update(rail_en_cr(&rails[i]), RAIL_EN, RAIL_EN);
      }
   }

   /* LPDDR4 power-on: set vdd1/vdd2, then enable vdd1 before vdd2/vddq. */
   err |= pmic_update(LDO3_MAIN_CR, (uint8_t)(LDO_IDX(VDD1_DDR_MV) << 1),
                      LDO_VOLT_MASK);
   err |= pmic_update(BUCK6_MAIN_CR1, BUCK_LOW_IDX(VDD2_DDR_MV),
                      BUCK_VOLT_MASK);
   err |= pmic_update(LDO3_MAIN_CR, RAIL_EN, RAIL_EN);
   err |= pmic_update(BUCK6_MAIN_CR1 + 1U, RAIL_EN, RAIL_EN);

   return err;
}

void pmic_print_rails(void)
{
   for (size_t i = 0; i < RAIL_COUNT; i++) {
      const struct rail *r = &rails[i];
      uint8_t cr = 0;
      uint8_t en = 0;
      unsigned int mv;

      if (pmic_read(r->cr, &cr) != 0 ||
          pmic_read(rail_en_cr(r), &en) != 0) {
         my_printf("%-6s %-13s I2C error\r\n", r->name, r->signal);
         continue;
      }

      switch (r->kind) {
      case RAIL_BUCK: {
         uint8_t idx = cr & BUCK_VOLT_MASK;
         mv = (idx <= 100U) ? (500U + 10U * idx)
                            : (1500U + 100U * (idx - 100U));
         break;
      }
      case RAIL_LDO:
         mv = 900U + 100U * ((cr & LDO_VOLT_MASK) >> 1);
         break;
      default: /* RAIL_LDO_FIXED */
         mv = r->fixed_mv;
         break;
      }

      my_printf("%-6s %-13s %-3s %4u mV%s\r\n", r->name, r->signal,
                (en & RAIL_EN) ? "on" : "off", mv,
                ((r->kind != RAIL_BUCK) && ((cr & LDO_BYPASS) != 0U))
                   ? " (bypass)"
                   : "");
   }
}
