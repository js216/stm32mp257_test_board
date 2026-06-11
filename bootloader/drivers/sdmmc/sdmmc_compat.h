/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Compat shim for the carried TF-A SDMMC/MMC code: clock, reset and
 * regulator hooks mapped onto this bootloader's RCC (SDMMC1 only).
 */

#ifndef SDMMC_COMPAT_H
#define SDMMC_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#include "lib/mmio.h"
#include "timer.h"

#define RCC_SDMMC1CFGR_ADDR 0x44200830UL
#define SDMMC1CFGR_RST      BIT(0)
#define SDMMC1CFGR_EN       BIT(1)

#define SDMMC1_KER_CLK_HZ 64000000U /* flexgen ch51 <- HSI */

/* drivers/clk.h stand-ins (the ids are ignored; SDMMC1 is the only user). */
static inline void clk_enable(unsigned int id)
{
   (void)id;
   mmio_setbits_32(RCC_SDMMC1CFGR_ADDR, SDMMC1CFGR_EN);
}

static inline unsigned long clk_get_rate(unsigned int id)
{
   (void)id;
   return SDMMC1_KER_CLK_HZ;
}

/* stm32mp_reset.h stand-ins. */
static inline int stm32mp_reset_assert(uint32_t id, unsigned int to_us)
{
   (void)id;
   (void)to_us;
   mmio_setbits_32(RCC_SDMMC1CFGR_ADDR, SDMMC1CFGR_RST);
   return 0;
}

static inline int stm32mp_reset_deassert(uint32_t id, unsigned int to_us)
{
   (void)id;
   (void)to_us;
   mmio_clrbits_32(RCC_SDMMC1CFGR_ADDR, SDMMC1CFGR_RST);
   return 0;
}

/* regulator.h stand-ins: vmmc (ldo7) is already on from pmic_init_rails. */
struct rdev;

static inline int regulator_enable(struct rdev *r)
{
   (void)r;
   return 0;
}

static inline int regulator_disable(struct rdev *r)
{
   (void)r;
   return 0;
}

#endif /* SDMMC_COMPAT_H */
