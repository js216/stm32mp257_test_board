// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file rcc.c
 * @brief Reset and clock control (minimal).
 * @copyright 2026 Jakob Kastelic
 *
 * Register offsets and the flexgen-channel configuration sequence are taken
 * from TF-A (drivers/st/clk/clk-stm32mp2.c, include/drivers/st/stm32mp21_rcc.h,
 * BSD-3-Clause), reduced to the single channel and gates this bootloader needs.
 */

#include "rcc.h"
#include "io.h"
#include "stm32mp25.h"

/* Per-peripheral configuration registers: bit 1 = clock enable. */
#define RCC_USART2CFGR (RCC_BASE + 0x780UL)
#define RCC_GPIOACFGR  (RCC_BASE + 0x52CUL)
#define RCC_GPIOHCFGR  (RCC_BASE + 0x548UL)
#define RCC_CFGR_EN    BIT(1)

/* Flexgen (cross-bar) per-channel registers: channel N at base + 4*N. */
#define RCC_XBAR0CFGR   (RCC_BASE + 0x1018UL)
#define RCC_PREDIV0CFGR (RCC_BASE + 0x1118UL)
#define RCC_FINDIV0CFGR (RCC_BASE + 0x1224UL)

#define XBAR_SEL_MASK 0x0000000FUL
#define XBAR_EN       BIT(6)
#define XBAR_STS      BIT(7)
#define PREDIV_MASK   0x000003FFUL
#define FINDIV_MASK   0x0000003FUL
#define FINDIV_EN     BIT(6)

#define XBAR_SRC_HSI_KER 0x8U /* HSI kernel clock, per stm32mp25-clksrc.h */
#define USART2_FLEX_CH   8U   /* FLEXGEN_CFG(8, HSI_KER, 0, 0) in TF-A */

/* Route a flexgen channel to HSI with no division (prediv=1, findiv=1). */
static void flexgen_to_hsi(unsigned int channel)
{
   uintptr_t prediv = RCC_PREDIV0CFGR + (4UL * channel);
   uintptr_t findiv = RCC_FINDIV0CFGR + (4UL * channel);
   uintptr_t xbar   = RCC_XBAR0CFGR + (4UL * channel);

   mmio_clrsetbits_32(prediv, PREDIV_MASK, 0U);
   mmio_clrsetbits_32(findiv, FINDIV_MASK, 0U);
   mmio_setbits_32(findiv, FINDIV_EN);

   mmio_clrsetbits_32(xbar, XBAR_SEL_MASK, XBAR_SRC_HSI_KER);
   mmio_setbits_32(xbar, XBAR_EN);
   while ((mmio_read_32(xbar) & XBAR_STS) != 0U) {
      /* wait for the cross-bar to accept the new source */
   }
}

void rcc_clock_init(void)
{
   flexgen_to_hsi(USART2_FLEX_CH);

   mmio_setbits_32(RCC_USART2CFGR, RCC_CFGR_EN);
   mmio_setbits_32(RCC_GPIOACFGR, RCC_CFGR_EN);
   mmio_setbits_32(RCC_GPIOHCFGR, RCC_CFGR_EN);
}
