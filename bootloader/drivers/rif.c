// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file rif.c
 * @brief RIF firewall hand-off: every resource non-secure, DDR open.
 * @copyright 2026 Jakob Kastelic
 *
 * Carried from the direct-boot TF-A patch's stm32mp2_rif_handoff() (itself
 * mirroring the state audited on a running board) plus the BL2 RISAF4
 * non-secure DDR base region. Values are kept verbatim: every resource
 * non-secure and CID-filter-free, except CRYP2 and IWDG3 which belong to
 * the Cortex-M33 (CID2).
 */

#include "io.h"
#include "rif.h"
#include "stm32mp25.h"
#include <stdint.h>

/* RIFSC */
#define RIFSC_BASE                0x42080000UL
#define RIFSC_RISC_SECCFGR(i)     (0x10UL + 4UL * (i))
#define RIFSC_RISC_PRIVCFGR(i)    (0x30UL + 4UL * (i))
#define RIFSC_RISC_PER_CIDCFGR(i) (0x100UL + 8UL * (i))
#define RIFSC_RIMC_ATTR(i)        (0xC10UL + 4UL * (i))
#define RIFSC_PER_MAX             128U
#define RIFSC_ID_CRYP2            97U
#define RIFSC_ID_IWDG3            100U
#define RIFSC_CID2_LOCKED         0x21U /* CFEN | SCID=CID2 */

/* RCC RIF */
#define RCC_SECCFGR(i)  (0x00UL + 4UL * (i))
#define RCC_PRIVCFGR(i) (0x10UL + 4UL * (i))
#define RCC_R_CIDCFGR(i)(0x30UL + 8UL * (i))
#define RCC_RES_MAX     114U

/* PWR RIF */
#define PWR_RSECCFGR      0x100UL
#define PWR_RPRIVCFGR     0x104UL
#define PWR_R_CIDCFGR(i)  (0x108UL + 4UL * (i))
#define PWR_WIOSECCFGR    0x180UL
#define PWR_WIOPRIVCFGR   0x184UL
#define PWR_WIO_CIDCFGR(i)(0x188UL + 8UL * ((i) - 1UL))
#define PWR_R_MAX         7U
#define PWR_WIO_MAX       6U

/* EXTI RIF */
#define EXTI1_BASE        0x44220000UL
#define EXTI2_BASE        0x46230000UL
#define EXTI_SECCFGR(i)   (0x14UL + 0x20UL * (i))
#define EXTI_PRIVCFGR(i)  (0x18UL + 0x20UL * (i))
#define EXTI_EN_CIDCFGR(i)(0x180UL + 4UL * (i))
#define EXTI_WORDS        3U
#define EXTI_LINES_MAX    96U

/* GPIO RIF (per bank) */
#define GPIOZ_BASE     0x46200000UL
#define GPIO_SECCFGR   0x30UL
#define GPIO_PRIVCFGR  0x34UL
#define GPIO_CIDCFGR(p)(0x50UL + 8UL * (p))
#define GPIO_PINS      16U

/* TAMP RIF + backup register zones */
#define TAMP_BASE        0x46010000UL
#define TAMP_SECCFGR     0x20UL
#define TAMP_PRIVCFGR    0x24UL
#define TAMP_BKPRIFR(i)  (0x70UL + 4UL * ((i) - 1UL))
#define TAMP_CIDCFGR(i)  (0x80UL + 4UL * (i))
#define TAMP_SECCFGR_VAL 0x00600030U
#define TAMP_BKPRIFR1_VAL 0x18U
#define TAMP_BKPRIFR2_VAL 0x48U
#define TAMP_BKPRIFR3_VAL 0x0078006CU

/* RISAF4 (DDR) */
#define RISAF4_BASE 0x420D0000UL

static const uint32_t rimc_attr_val[16] = {
   0x214U, 0x200U, 0x200U, 0x000U,
   0x200U, 0x200U, 0x200U, 0x200U,
   0x200U, 0x200U, 0x200U, 0x204U,
   0x204U, 0x204U, 0x200U, 0x200U,
};

struct gpio_bank_cfg {
   uintptr_t base;
   uintptr_t rcc_cfgr; /* RCC_GPIOxCFGR: bit1 = clock enable */
};

static const struct gpio_bank_cfg gpio_banks[] = {
   {GPIO_BANK(0), RCC_BASE + 0x52CUL},  /* A */
   {GPIO_BANK(1), RCC_BASE + 0x530UL},  /* B */
   {GPIO_BANK(2), RCC_BASE + 0x534UL},  /* C */
   {GPIO_BANK(3), RCC_BASE + 0x538UL},  /* D */
   {GPIO_BANK(4), RCC_BASE + 0x53CUL},  /* E */
   {GPIO_BANK(5), RCC_BASE + 0x540UL},  /* F */
   {GPIO_BANK(6), RCC_BASE + 0x544UL},  /* G */
   {GPIO_BANK(7), RCC_BASE + 0x548UL},  /* H */
   {GPIO_BANK(8), RCC_BASE + 0x54CUL},  /* I */
   {GPIO_BANK(9), RCC_BASE + 0x550UL},  /* J */
   {GPIO_BANK(10), RCC_BASE + 0x554UL}, /* K */
   {GPIOZ_BASE, RCC_BASE + 0x558UL},    /* Z */
};

static void risaf4_open_ddr_ns(void)
{
   uintptr_t rb    = RISAF4_BASE;
   uint32_t hwcfgr = mmio_read_32(rb + 0xFF0UL);
   uint32_t mlsb   = (hwcfgr >> 16) & 0xFFU;
   uint32_t mmsb   = mlsb + ((hwcfgr >> 24) & 0xFFU) - 1U;
   uint32_t amask  = (uint32_t)((0xFFFFFFFFU >> (31U - mmsb)) &
                                ~((1U << mlsb) - 1U));

   /* RISAF4 region 1 = non-secure base region covering all of DDR. */
   mmio_clrbits_32(rb + 0x40UL, BIT(0));                       /* BREN */
   mmio_clrsetbits_32(rb + 0x44UL, amask, 0U);                 /* STARTR */
   mmio_clrsetbits_32(rb + 0x48UL, amask, 0xFFFFFFFFU & amask);/* ENDR */
   mmio_clrsetbits_32(rb + 0x4CUL, 0x00FF00FFU, 0x00030003U);  /* CID RW */
   mmio_clrsetbits_32(rb + 0x40UL, 0x00FF8101U, BIT(0));       /* BREN=1 SEC=0 */
}

void rif_handoff(void)
{
   uint32_t i;
   uint32_t p;

   /* RIFSC peripherals */
   for (i = 0; i < RIFSC_PER_MAX; i++) {
      uint32_t cid = 0U;

      if ((i == RIFSC_ID_CRYP2) || (i == RIFSC_ID_IWDG3)) {
         cid = RIFSC_CID2_LOCKED;
      }

      mmio_write_32(RIFSC_BASE + RIFSC_RISC_PER_CIDCFGR(i), cid);
   }

   for (i = 0; i < 6U; i++) {
      uint32_t sec = (i == 3U) ? 0x12U : 0U;

      mmio_write_32(RIFSC_BASE + RIFSC_RISC_SECCFGR(i), sec);
      mmio_write_32(RIFSC_BASE + RIFSC_RISC_PRIVCFGR(i), sec);
   }

   for (i = 0; i < 16U; i++) {
      mmio_write_32(RIFSC_BASE + RIFSC_RIMC_ATTR(i), rimc_attr_val[i]);
   }

   /* RCC resources */
   for (i = 0; i < RCC_RES_MAX; i++) {
      mmio_write_32(RCC_BASE + RCC_R_CIDCFGR(i), 0U);
   }

   for (i = 0; i < 4U; i++) {
      mmio_write_32(RCC_BASE + RCC_SECCFGR(i), 0U);
      mmio_write_32(RCC_BASE + RCC_PRIVCFGR(i), 0U);
   }

   /* PWR resources and wake-up IOs */
   for (i = 0; i < PWR_R_MAX; i++) {
      mmio_write_32(PWR_BASE + PWR_R_CIDCFGR(i), 0U);
   }

   for (i = 1; i <= PWR_WIO_MAX; i++) {
      mmio_write_32(PWR_BASE + PWR_WIO_CIDCFGR(i), 0U);
   }

   mmio_write_32(PWR_BASE + PWR_RSECCFGR, 0U);
   mmio_write_32(PWR_BASE + PWR_RPRIVCFGR, 0U);
   mmio_write_32(PWR_BASE + PWR_WIOSECCFGR, 0U);
   mmio_write_32(PWR_BASE + PWR_WIOPRIVCFGR, 0U);

   /* EXTI1/EXTI2 lines */
   for (i = 0; i < EXTI_LINES_MAX; i++) {
      mmio_write_32(EXTI1_BASE + EXTI_EN_CIDCFGR(i), 0U);
      mmio_write_32(EXTI2_BASE + EXTI_EN_CIDCFGR(i), 0U);
   }

   for (i = 0; i < EXTI_WORDS; i++) {
      mmio_write_32(EXTI1_BASE + EXTI_SECCFGR(i), 0U);
      mmio_write_32(EXTI1_BASE + EXTI_PRIVCFGR(i), 0U);
      mmio_write_32(EXTI2_BASE + EXTI_SECCFGR(i), 0U);
      mmio_write_32(EXTI2_BASE + EXTI_PRIVCFGR(i), 0U);
   }

   /* GPIO banks A..K and Z */
   for (i = 0; i < sizeof(gpio_banks) / sizeof(gpio_banks[0]); i++) {
      mmio_setbits_32(gpio_banks[i].rcc_cfgr, BIT(1));

      for (p = 0; p < GPIO_PINS; p++) {
         mmio_write_32(gpio_banks[i].base + GPIO_CIDCFGR(p), 0U);
      }

      mmio_write_32(gpio_banks[i].base + GPIO_SECCFGR, 0U);
      mmio_write_32(gpio_banks[i].base + GPIO_PRIVCFGR, 0U);
   }

   /* TAMP: keep the audited backup-register zoning, no CID filtering */
   for (i = 0; i < 4U; i++) {
      mmio_write_32(TAMP_BASE + TAMP_CIDCFGR(i), 0U);
   }

   mmio_write_32(TAMP_BASE + TAMP_SECCFGR, TAMP_SECCFGR_VAL);
   mmio_write_32(TAMP_BASE + TAMP_PRIVCFGR, 0U);
   mmio_write_32(TAMP_BASE + TAMP_BKPRIFR(1), TAMP_BKPRIFR1_VAL);
   mmio_write_32(TAMP_BASE + TAMP_BKPRIFR(2), TAMP_BKPRIFR2_VAL);
   mmio_write_32(TAMP_BASE + TAMP_BKPRIFR(3), TAMP_BKPRIFR3_VAL);

   risaf4_open_ddr_ns();
}
