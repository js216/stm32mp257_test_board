// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file sd_glue.c
 * @brief SD card bring-up for the STM32MP257F-DK (SDMMC1, 4-bit).
 * @copyright 2026 Jakob Kastelic
 *
 * Thin wrapper around the carried TF-A SDMMC2/MMC drivers: switch the SD
 * pads' VDDIO1 I/O domain on, clock and pin-mux SDMMC1, then run the
 * standard mmc_init/mmc_read_blocks flow. Pin set and modes follow
 * stm32mp25-pinctrl.dtsi sdmmc1_b4_pins_b (DK device tree).
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/mmc.h>
#include <drivers/st/stm32_sdmmc2.h>

#include "gpio.h"
#include "stm32mp25.h"
#include "lib/mmio.h"
#include "rcc.h"
#include "sd.h"
#include "timer.h"

#define SDMMC1_BASE 0x48220000UL

/* PWR_CR8: VDDIO1 (SD pads) I/O-domain validation. */
#define PWR_CR8_ADDR  0x4421001CUL
#define VDDIO1_VMEN   BIT(0)
#define VDDIO1_SV     BIT(8)
#define VDDIO1_RDY    BIT(16)

/* SDMMC1 pins (GPIOE, all AF10): D0-D3, CMD, CK. */
#define SD_GPIO GPIO_BANK(4) /* GPIOE */
#define SD_AF   10U

static const unsigned int sd_pins[] = {4U, 5U, 0U, 1U, 2U, 3U};

static int vddio1_enable(void)
{
   if ((mmio_read_32(PWR_CR8_ADDR) & VDDIO1_SV) != 0U) {
      return 0;
   }

   mmio_setbits_32(PWR_CR8_ADDR, VDDIO1_VMEN);

   uint64_t expire = timeout_init_us(10000U);
   while ((mmio_read_32(PWR_CR8_ADDR) & VDDIO1_RDY) == 0U) {
      if (timeout_elapsed(expire)) {
         mmio_clrbits_32(PWR_CR8_ADDR, VDDIO1_VMEN);
         return -1;
      }
   }

   mmio_setbits_32(PWR_CR8_ADDR, VDDIO1_SV);
   mmio_clrbits_32(PWR_CR8_ADDR, VDDIO1_VMEN);
   return 0;
}

static struct mmc_device_info sd_dev_info = {
   .mmc_dev_type = MMC_IS_SD_HC,
   .ocr_voltage  = 0U, /* filled by stm32_sdmmc2_mmc_init */
};

static struct stm32_sdmmc2_params sd_params = {
   .reg_base    = SDMMC1_BASE,
   .bus_width   = MMC_BUS_WIDTH_4,
   .device_info = &sd_dev_info,
};

int sd_init(void)
{
   if (vddio1_enable() != 0) {
      return -1;
   }

   rcc_sdmmc1_clk_init();

   for (unsigned int i = 0; i < (sizeof(sd_pins) / sizeof(sd_pins[0])); i++) {
      gpio_set_speed(SD_GPIO, sd_pins[i], (sd_pins[i] == 3U) ? 2U : 1U);
      gpio_set_af(SD_GPIO, sd_pins[i], SD_AF);
   }

   return stm32_sdmmc2_mmc_init(&sd_params);
}

int sd_read(unsigned int lba, uintptr_t buf, unsigned int size)
{
   return (mmc_read_blocks((int)lba, buf, size) == size) ? 0 : -1;
}

int sd_write(unsigned int lba, uintptr_t buf, unsigned int size)
{
   stm32_sdmmc2_set_write_pending(true);
   size_t done = mmc_write_blocks((int)lba, buf, size);
   stm32_sdmmc2_set_write_pending(false);
   return (done == size) ? 0 : -1;
}

unsigned long long sd_size_bytes(void)
{
   return sd_dev_info.device_size;
}
