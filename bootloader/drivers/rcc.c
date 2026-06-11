// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file rcc.c
 * @brief Reset and clock control (minimal).
 * @copyright 2026 Jakob Kastelic
 *
 * Register offsets and the flexgen-channel configuration sequence are taken
 * from TF-A (drivers/st/clk/clk-stm32mp2.c, include/drivers/st/stm32mp25_rcc.h,
 * BSD-3-Clause), reduced to the channels and gates this bootloader needs.
 * The pre/final-divider and cross-bar status registers are polled (as TF-A
 * does) so the divider writes are not dropped while a channel is busy.
 */

#include "rcc.h"
#include "io.h"
#include "printf.h"
#include "stm32mp25.h"

/* Per-peripheral configuration registers: bit 1 = clock enable. */
#define RCC_USART2CFGR (RCC_BASE + 0x780UL)
#define RCC_I2C7CFGR   (RCC_BASE + 0x7BCUL)
#define RCC_GPIOACFGR  (RCC_BASE + 0x52CUL)
#define RCC_GPIODCFGR  (RCC_BASE + 0x538UL)
#define RCC_GPIOECFGR  (RCC_BASE + 0x53CUL)
#define RCC_GPIOHCFGR  (RCC_BASE + 0x548UL)
#define RCC_CFGR_RST   BIT(0)
#define RCC_CFGR_EN    BIT(1)

/* Flexgen (cross-bar) per-channel registers: channel N at base + 4*N. */
#define RCC_XBAR0CFGR   (RCC_BASE + 0x1018UL)
#define RCC_PREDIV0CFGR (RCC_BASE + 0x1118UL)
#define RCC_FINDIV0CFGR (RCC_BASE + 0x1224UL)
#define RCC_PREDIVSR1   (RCC_BASE + 0x1218UL)
#define RCC_PREDIVSR2   (RCC_BASE + 0x121CUL)
#define RCC_FINDIVSR1   (RCC_BASE + 0x1324UL)
#define RCC_FINDIVSR2   (RCC_BASE + 0x1328UL)

#define XBAR_SEL_MASK 0x0000000FUL
#define XBAR_EN       BIT(6)
#define XBAR_STS      BIT(7)
#define PREDIV_MASK   0x000003FFUL
#define FINDIV_MASK   0x0000003FUL
#define FINDIV_EN     BIT(6)

#define XBAR_SRC_HSI_KER 0x8U /* HSI kernel clock, per stm32mp25-clksrc.h */
#define XBAR_SRC_PLL4    0x0U /* per stm32mp25-clksrc.h */

/*
 * PLL4 (the bus/interconnect PLL) and the NoC flexgen channels. The ROM
 * leaves the whole interconnect on slow boot clocks; without this, DDR
 * bandwidth from the CPU is capped around 50 MB/s no matter how fast the
 * A35 PLL runs. Same integer-mode sequence and 40 MHz HSE * 30 = 1200 MHz
 * as PLL2; targets from the DK clock tree (st,flexgen): ICN_HS_MCU 400,
 * ICN_SDMMC 200, ICN_DDR 600, ICN_HSL 300, ICN_NIC 400 MHz, LSMCU = /2.
 */
#define RCC_PLL4CFGR1 (RCC_BASE + 0x1360UL)
#define RCC_PLL4CFGR2 (RCC_BASE + 0x1364UL)
#define RCC_PLL4CFGR3 (RCC_BASE + 0x1368UL)
#define RCC_PLL4CFGR4 (RCC_BASE + 0x136CUL)
#define RCC_PLL4CFGR6 (RCC_BASE + 0x1378UL)
#define RCC_PLL4CFGR7 (RCC_BASE + 0x137CUL)
#define RCC_LSMCUDIVR (RCC_BASE + 0x4D0UL)
#define MUXSEL0_SHIFT 0U
#define MUXSEL0_MASK  (0x3UL << MUXSEL0_SHIFT)
#define USART2_FLEX_CH   8U   /* CK_KER_USART2 = flexgen 8 */
#define I2C7_FLEX_CH     15U  /* CK_KER_I2C7   = flexgen 15 */
#define SDMMC1_FLEX_CH   51U  /* CK_KER_SDMMC1 = flexgen 51 */

#define RCC_OCENSETR  (RCC_BASE + 0x49CUL)
#define RCC_OCRDYR    (RCC_BASE + 0x4A4UL)
#define OCEN_HSION    BIT(0)
#define OCEN_HSIKERON BIT(1)
#define OCEN_HSEON    BIT(8)
#define OCRDY_HSIRDY  BIT(0)
#define OCRDY_HSERDY  BIT(8)

/*
 * PLL2 (the DDR PLL): registers and the integer-mode programming sequence
 * from TF-A clk-stm32mp2.c / stm32mp25_rcc.h. The DK clock tree
 * (stm32mp257f-dk-ca35tdcid-rcc.dtsi) runs it at 600 MHz from HSE 40 MHz:
 * cfg = <30 1 1 2> = FBDIV 30, FREFDIV 1, POSTDIV1 1, POSTDIV2 2.
 */
#define RCC_MUXSELCFGR (RCC_BASE + 0x1000UL)
#define RCC_PLL2CFGR1  (RCC_BASE + 0x590UL)
#define RCC_PLL2CFGR2  (RCC_BASE + 0x594UL)
#define RCC_PLL2CFGR3  (RCC_BASE + 0x598UL)
#define RCC_PLL2CFGR4  (RCC_BASE + 0x59CUL)
#define RCC_PLL2CFGR6  (RCC_BASE + 0x5A8UL)
#define RCC_PLL2CFGR7  (RCC_BASE + 0x5ACUL)

#define PLL_CFGR1_SSMODRST      BIT(0)
#define PLL_CFGR1_PLLEN         BIT(8)
#define PLL_CFGR1_PLLRDY        BIT(24)
#define PLL_CFGR1_CKREFST       BIT(28)
#define PLL_CFGR2_FREFDIV_MASK  0x0000003FUL
#define PLL_CFGR2_FBDIV_MASK    0x0FFF0000UL
#define PLL_CFGR2_FBDIV_SHIFT   16U
#define PLL_CFGR3_FRACIN_MASK   0x00FFFFFFUL
#define PLL_CFGR3_DACEN         BIT(25)
#define PLL_CFGR3_SSCGDIS       BIT(26)
#define PLL_CFGR4_DSMEN         BIT(8)
#define PLL_CFGR4_FOUTPOSTDIVEN BIT(9)
#define PLL_CFGR4_BYPASS        BIT(10)
#define PLL_CFGR6_POSTDIV1_MASK 0x00000007UL
#define PLL_CFGR7_POSTDIV2_MASK 0x00000007UL

#define MUXSEL5_SHIFT 20U /* PLL1 (A35) source select, 2 bits */
#define MUXSEL5_MASK  (0x3UL << MUXSEL5_SHIFT)
#define MUXSEL6_SHIFT 24U /* PLL2 source select, 2 bits */
#define MUXSEL6_MASK  (0x3UL << MUXSEL6_SHIFT)
#define MUXSEL_HSE    1UL

/*
 * A35 sub-system PLL (PLL1): the CPU cluster manages its own PLL through
 * the A35SSC registers, not the RCC PLLxCFGR block. Registers, bits and
 * the bypass->config->lock->switch sequence from TF-A clk-stm32mp2.c.
 * The DK clock tree runs it at 1200 MHz from HSE: cfg = <30 1 1 1>.
 * Without this the cores stay on the ROM's bypass clock and Linux has no
 * way to raise it (the kernel's ca35ss driver only proxies via SIP SMC).
 */
#define A35SSC_BASE       0x48800000UL
#define A35_SS_CHGCLKREQ  (A35SSC_BASE + 0x00UL)
#define A35_SS_PLL_FREQ1  (A35SSC_BASE + 0x80UL)
#define A35_SS_PLL_FREQ2  (A35SSC_BASE + 0x90UL)
#define A35_SS_PLL_ENABLE (A35SSC_BASE + 0xA0UL)

#define A35_CHGCLKREQ_REQ     BIT(0)
#define A35_CHGCLKREQ_ACK     BIT(1)
#define A35_CHGCLKREQ_DIVSEL  BIT(16)
#define A35_CHGCLKREQ_DIVSELACK BIT(17)

#define A35_PLL_ENABLE_PD               BIT(0)
#define A35_PLL_ENABLE_LOCKP            BIT(1)
#define A35_PLL_ENABLE_NRESET_SWPLL_FF  BIT(2)

#define A35_PLL_FREQ1_FBDIV_MASK  0x00000FFFUL
#define A35_PLL_FREQ1_REFDIV_MASK 0x003F0000UL
#define A35_PLL_FREQ2_POSTDIV1_MASK 0x00000007UL
#define A35_PLL_FREQ2_POSTDIV2_MASK 0x00000038UL

#define A35_PLL_FBDIV    30U /* 40 MHz HSE * 30 / (1*1*1) = 1200 MHz */
#define A35_PLL_REFDIV   1U
#define A35_PLL_POSTDIV1 1U
#define A35_PLL_POSTDIV2 1U

#define PLL2_FBDIV    30U
#define PLL2_FREFDIV  1U
#define PLL2_POSTDIV1 1U
#define PLL2_POSTDIV2 2U

#define DIV_TIMEOUT 1000000U /* bounded spin so a stuck status never hangs */

static void wait_status_clear(uintptr_t sr, uint32_t bit)
{
   unsigned int n = DIV_TIMEOUT;
   while ((mmio_read_32(sr) & bit) != 0U) {
      if (--n == 0U) {
         break;
      }
   }
}

/*
 * Route a flexgen channel to HSI with no division (prediv=1, findiv=1).
 *
 * The divider writes occasionally do not stick (the channel hands off from the
 * boot ROM in a /2 state and the write is dropped while the divider is busy),
 * which intermittently halves the USART2/I2C7 clock. Read the dividers back
 * and re-apply until they actually read /1.
 */
static void flexgen_to_hsi(unsigned int channel)
{
   uintptr_t prediv = RCC_PREDIV0CFGR + (4UL * channel);
   uintptr_t findiv = RCC_FINDIV0CFGR + (4UL * channel);
   uintptr_t xbar   = RCC_XBAR0CFGR + (4UL * channel);
   uintptr_t presr  = (channel < 32U) ? RCC_PREDIVSR1 : RCC_PREDIVSR2;
   uintptr_t finsr  = (channel < 32U) ? RCC_FINDIVSR1 : RCC_FINDIVSR2;
   uint32_t bit     = BIT(channel & 31U);
   unsigned int retry = 100U;

   do {
      wait_status_clear(presr, bit);
      mmio_clrsetbits_32(prediv, PREDIV_MASK, 0U);
      wait_status_clear(presr, bit);

      wait_status_clear(finsr, bit);
      mmio_clrsetbits_32(findiv, FINDIV_MASK, 0U);
      wait_status_clear(finsr, bit);

      wait_status_clear(xbar, XBAR_STS);
      mmio_clrsetbits_32(xbar, XBAR_SEL_MASK, XBAR_SRC_HSI_KER);
      mmio_setbits_32(xbar, XBAR_EN);
      wait_status_clear(xbar, XBAR_STS);

      mmio_setbits_32(findiv, FINDIV_EN);
   } while ((--retry != 0U) &&
            (((mmio_read_32(prediv) & PREDIV_MASK) != 0U) ||
             ((mmio_read_32(findiv) & FINDIV_MASK) != 0U) ||
             ((mmio_read_32(xbar) & XBAR_SEL_MASK) != XBAR_SRC_HSI_KER)));
}

/* Route a flexgen channel to PLL4 with the given final divider (val+1). */
static void flexgen_to_pll4(unsigned int channel, uint32_t findiv_val)
{
   uintptr_t prediv = RCC_PREDIV0CFGR + (4UL * channel);
   uintptr_t findiv = RCC_FINDIV0CFGR + (4UL * channel);
   uintptr_t xbar   = RCC_XBAR0CFGR + (4UL * channel);
   uintptr_t presr  = (channel < 32U) ? RCC_PREDIVSR1 : RCC_PREDIVSR2;
   uintptr_t finsr  = (channel < 32U) ? RCC_FINDIVSR1 : RCC_FINDIVSR2;
   uint32_t bit     = BIT(channel & 31U);
   unsigned int retry = 3U; /* fail fast: a stuck channel must not stall boot */

   do {
      wait_status_clear(presr, bit);
      mmio_clrsetbits_32(prediv, PREDIV_MASK, 0U);
      wait_status_clear(presr, bit);

      wait_status_clear(finsr, bit);
      mmio_clrsetbits_32(findiv, FINDIV_MASK, findiv_val);
      wait_status_clear(finsr, bit);

      wait_status_clear(xbar, XBAR_STS);
      mmio_clrsetbits_32(xbar, XBAR_SEL_MASK, XBAR_SRC_PLL4);
      mmio_setbits_32(xbar, XBAR_EN);
      wait_status_clear(xbar, XBAR_STS);

      mmio_setbits_32(findiv, FINDIV_EN);
   } while ((--retry != 0U) &&
            (((mmio_read_32(prediv) & PREDIV_MASK) != 0U) ||
             ((mmio_read_32(findiv) & FINDIV_MASK) != findiv_val) ||
             ((mmio_read_32(xbar) & XBAR_SEL_MASK) != XBAR_SRC_PLL4)));
}

int rcc_bus_clk_init(void)
{
   /*
    * The ROM already runs PLL4 at 1200 MHz and clocks several of its own
    * consumers (including the USB path) from it: stopping or
    * reprogramming PLL4 here crashes the board mid-USB-traffic
    * (hardware-verified). All this init does is route the one
    * bandwidth-critical channel, ICN_DDR, onto the already-running PLL4
    * at /2 = 600 MHz. ch0 (the CPU's own bus) must never be switched
    * live, and the peripheral buses stay on their proven boot clocks.
    */
   if ((mmio_read_32(RCC_PLL4CFGR1) & PLL_CFGR1_PLLRDY) == 0U) {
      return -1; /* PLL4 not running: leave everything at boot clocks */
   }

   flexgen_to_pll4(2U, 1U); /* ICN_DDR 600 MHz */
   return 0;
}

/*
 * Bring up PLL2 at 600 MHz from HSE (the DDR clock source). Returns 0 once
 * the PLL reports lock, -1 on a ready/lock timeout. Follows TF-A's
 * _clk_stm32_pll_init() integer-mode sequence.
 */
int rcc_pll2_init(void)
{
   /* HSE on (40 MHz crystal, no bypass on the DK), wait ready. */
   mmio_setbits_32(RCC_OCENSETR, OCEN_HSEON);
   unsigned int n = DIV_TIMEOUT;
   while ((mmio_read_32(RCC_OCRDYR) & OCRDY_HSERDY) == 0U) {
      if (--n == 0U) {
         return -1; /* HSE never became ready */
      }
   }

   /* Stop the PLL while reconfiguring. */
   mmio_clrbits_32(RCC_PLL2CFGR1, PLL_CFGR1_PLLEN);
   n = DIV_TIMEOUT;
   while ((mmio_read_32(RCC_PLL2CFGR1) & PLL_CFGR1_PLLRDY) != 0U) {
      if (--n == 0U) {
         return -2; /* PLL would not stop */
      }
   }

   /* Reference = HSE; wait for the source handoff. */
   mmio_clrsetbits_32(RCC_MUXSELCFGR, MUXSEL6_MASK,
                      MUXSEL_HSE << MUXSEL6_SHIFT);
   n = DIV_TIMEOUT;
   while ((mmio_read_32(RCC_PLL2CFGR1) & PLL_CFGR1_CKREFST) == 0U) {
      if (--n == 0U) {
         return -3; /* reference mux not ready */
      }
   }

   /* Integer mode, dividers for 40 MHz * 30 / (1 * 1 * 2) = 600 MHz. */
   mmio_clrbits_32(RCC_PLL2CFGR3, PLL_CFGR3_FRACIN_MASK);
   mmio_clrbits_32(RCC_PLL2CFGR4, PLL_CFGR4_DSMEN);
   mmio_clrbits_32(RCC_PLL2CFGR3, PLL_CFGR3_DACEN);
   mmio_setbits_32(RCC_PLL2CFGR3, PLL_CFGR3_SSCGDIS);
   mmio_setbits_32(RCC_PLL2CFGR1, PLL_CFGR1_SSMODRST);
   mmio_clrsetbits_32(RCC_PLL2CFGR2, PLL_CFGR2_FBDIV_MASK,
                      ((uint32_t)PLL2_FBDIV << PLL_CFGR2_FBDIV_SHIFT));
   mmio_clrsetbits_32(RCC_PLL2CFGR2, PLL_CFGR2_FREFDIV_MASK, PLL2_FREFDIV);
   mmio_clrsetbits_32(RCC_PLL2CFGR6, PLL_CFGR6_POSTDIV1_MASK, PLL2_POSTDIV1);
   mmio_clrsetbits_32(RCC_PLL2CFGR7, PLL_CFGR7_POSTDIV2_MASK, PLL2_POSTDIV2);
   mmio_clrbits_32(RCC_PLL2CFGR4, PLL_CFGR4_BYPASS);
   mmio_setbits_32(RCC_PLL2CFGR4, PLL_CFGR4_FOUTPOSTDIVEN);

   /* Enable and wait for lock. */
   mmio_setbits_32(RCC_PLL2CFGR1, PLL_CFGR1_PLLEN);
   n = DIV_TIMEOUT;
   while ((mmio_read_32(RCC_PLL2CFGR1) & PLL_CFGR1_PLLRDY) == 0U) {
      if (--n == 0U) {
         return -4; /* no lock */
      }
   }

   return 0;
}

/*
 * Raise the A35 cluster to 1200 MHz (TF-A _clk_stm32_pll1_init sequence):
 * park the cluster on the bypass clock, point MUXSEL5 at HSE, program the
 * dividers, power the PLL up, wait for lock and switch the cluster over.
 * Returns 0 on success; the cluster stays on the bypass clock on error.
 */
int rcc_a35_pll1_init(void)
{
   unsigned int n;

   /* HSE on (already up if PLL2 ran first; cheap to repeat). */
   mmio_setbits_32(RCC_OCENSETR, OCEN_HSEON);
   n = DIV_TIMEOUT;
   while ((mmio_read_32(RCC_OCRDYR) & OCRDY_HSERDY) == 0U) {
      if (--n == 0U) {
         return -1;
      }
   }

   /* Switch the cluster to the bypass clock while we touch the PLL. */
   if ((mmio_read_32(A35_SS_CHGCLKREQ) & A35_CHGCLKREQ_ACK) == 0U) {
      if ((mmio_read_32(A35_SS_CHGCLKREQ) & A35_CHGCLKREQ_DIVSEL) != 0U) {
         mmio_clrbits_32(A35_SS_CHGCLKREQ, A35_CHGCLKREQ_DIVSEL);
         n = DIV_TIMEOUT;
         while ((mmio_read_32(A35_SS_CHGCLKREQ) &
                 A35_CHGCLKREQ_DIVSELACK) != 0U) {
            if (--n == 0U) {
               return -2;
            }
         }
      }

      mmio_setbits_32(A35_SS_CHGCLKREQ, A35_CHGCLKREQ_REQ);
      n = DIV_TIMEOUT;
      while ((mmio_read_32(A35_SS_CHGCLKREQ) & A35_CHGCLKREQ_ACK) == 0U) {
         if (--n == 0U) {
            return -3;
         }
      }
   }
   mmio_clrbits_32(A35_SS_PLL_ENABLE, A35_PLL_ENABLE_NRESET_SWPLL_FF);

   /*
    * Power the PLL down before reprogramming: unlike on TF-A boots, the
    * ROM leaves PLL1 running here, and divider writes into a running PLL
    * do not latch -- the later "wait for lock" then passes instantly on
    * the OLD frequency and the cluster comes back at the boot clock.
    */
   mmio_clrbits_32(A35_SS_PLL_ENABLE, A35_PLL_ENABLE_PD);
   n = DIV_TIMEOUT;
   while ((mmio_read_32(A35_SS_PLL_ENABLE) & A35_PLL_ENABLE_LOCKP) != 0U) {
      if (--n == 0U) {
         return -6; /* PLL would not unlock/power down */
      }
   }

   /* PLL1 reference = HSE. */
   mmio_clrsetbits_32(RCC_MUXSELCFGR, MUXSEL5_MASK,
                      MUXSEL_HSE << MUXSEL5_SHIFT);

   /* Dividers: 40 MHz * 30 / (1 * 1 * 1) = 1200 MHz. */
   mmio_clrsetbits_32(A35_SS_PLL_FREQ1, A35_PLL_FREQ1_REFDIV_MASK,
                      (A35_PLL_REFDIV << 16) & A35_PLL_FREQ1_REFDIV_MASK);
   mmio_clrsetbits_32(A35_SS_PLL_FREQ1, A35_PLL_FREQ1_FBDIV_MASK,
                      A35_PLL_FBDIV & A35_PLL_FREQ1_FBDIV_MASK);
   mmio_clrsetbits_32(A35_SS_PLL_FREQ2, A35_PLL_FREQ2_POSTDIV1_MASK,
                      A35_PLL_POSTDIV1);
   mmio_clrsetbits_32(A35_SS_PLL_FREQ2, A35_PLL_FREQ2_POSTDIV2_MASK,
                      (A35_PLL_POSTDIV2 << 3) & A35_PLL_FREQ2_POSTDIV2_MASK);

   /* Power up, wait for lock, release the output, switch the cluster. */
   mmio_setbits_32(A35_SS_PLL_ENABLE, A35_PLL_ENABLE_PD);
   n = DIV_TIMEOUT;
   while ((mmio_read_32(A35_SS_PLL_ENABLE) & A35_PLL_ENABLE_LOCKP) == 0U) {
      if (--n == 0U) {
         return -4;
      }
   }
   mmio_setbits_32(A35_SS_PLL_ENABLE, A35_PLL_ENABLE_NRESET_SWPLL_FF);

   mmio_clrbits_32(A35_SS_CHGCLKREQ, A35_CHGCLKREQ_REQ);
   n = DIV_TIMEOUT;
   while ((mmio_read_32(A35_SS_CHGCLKREQ) & A35_CHGCLKREQ_ACK) != 0U) {
      if (--n == 0U) {
         return -5;
      }
   }

   return 0;
}

void rcc_sdmmc1_clk_init(void)
{
   flexgen_to_hsi(SDMMC1_FLEX_CH);
}

void rcc_pll2_dump(void)
{
   my_printf("RCC muxsel=%08X ocen=%08X ocrdy=%08X\r\n",
      (unsigned int)mmio_read_32(RCC_MUXSELCFGR),
      (unsigned int)mmio_read_32(RCC_OCENSETR),
      (unsigned int)mmio_read_32(RCC_OCRDYR));
   my_printf("PLL2 cfgr1=%08X cfgr2=%08X cfgr3=%08X cfgr4=%08X cfgr6=%08X cfgr7=%08X\r\n",
      (unsigned int)mmio_read_32(RCC_PLL2CFGR1),
      (unsigned int)mmio_read_32(RCC_PLL2CFGR2),
      (unsigned int)mmio_read_32(RCC_PLL2CFGR3),
      (unsigned int)mmio_read_32(RCC_PLL2CFGR4),
      (unsigned int)mmio_read_32(RCC_PLL2CFGR6),
      (unsigned int)mmio_read_32(RCC_PLL2CFGR7));
}

void rcc_clock_init(void)
{
   flexgen_to_hsi(USART2_FLEX_CH);
   flexgen_to_hsi(I2C7_FLEX_CH);

   mmio_setbits_32(RCC_USART2CFGR, RCC_CFGR_EN);
   mmio_setbits_32(RCC_GPIOACFGR, RCC_CFGR_EN);
   mmio_setbits_32(RCC_GPIOECFGR, RCC_CFGR_EN);
   mmio_setbits_32(RCC_GPIODCFGR, RCC_CFGR_EN);
   mmio_setbits_32(RCC_GPIOHCFGR, RCC_CFGR_EN);

   /* Enable I2C7 and pulse its reset to clear any state (BUSY/TC) the boot
    * ROM left after its own PMIC transactions. */
   mmio_setbits_32(RCC_I2C7CFGR, RCC_CFGR_EN);
   mmio_setbits_32(RCC_I2C7CFGR, RCC_CFGR_RST);
   for (volatile unsigned int d = 0; d < 1000U; d++) {
   }
   mmio_clrbits_32(RCC_I2C7CFGR, RCC_CFGR_RST);
}
