// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file timer.c
 * @brief Microsecond delays/timeouts from the ARM generic timer.
 * @copyright 2026 Jakob Kastelic
 *
 * The system counter (STGEN) runs from the boot ROM at the HSI rate with
 * CNTFRQ_EL0 already programmed; we only read CNTPCT_EL0. The API mirrors
 * TF-A's drivers/delay_timer.h so the carried DDR code can use it directly.
 */

#include "timer.h"
#include "io.h"
#include "stm32mp25.h"

#define STGEN_CNTCR   (STGEN_BASE + 0x00UL)
#define STGEN_CNTCVL  (STGEN_BASE + 0x08UL)
#define STGEN_CNTCVU  (STGEN_BASE + 0x0CUL)
#define STGEN_CNTFID0 (STGEN_BASE + 0x20UL)
#define CNTCR_EN      BIT(0)

#define RCC_STGENCFGR (RCC_BASE + 0x824UL)
#define STGENCFGR_EN  BIT(1)

#define STGEN_HZ 64000000UL /* ck_ker_stgen = HSI (flexgen ch33 reset state) */

void timer_init(void)
{
   /* The boot ROM leaves the system counter unconfigured (CNTFRQ_EL0 = 0
    * and STGEN not counting): clock it, start it at the HSI rate and tell
    * the CPU. Same sequence as TF-A's stm32mp_stgen_config(). */
   mmio_setbits_32(RCC_STGENCFGR, STGENCFGR_EN);
   mmio_clrbits_32(STGEN_CNTCR, CNTCR_EN);
   mmio_write_32(STGEN_CNTCVL, 0U);
   mmio_write_32(STGEN_CNTCVU, 0U);
   mmio_write_32(STGEN_CNTFID0, STGEN_HZ);
   mmio_setbits_32(STGEN_CNTCR, CNTCR_EN);
   __asm__ volatile("msr cntfrq_el0, %0" ::"r"((uint64_t)STGEN_HZ));
   __asm__ volatile("isb");
}

static inline uint64_t read_cntpct(void)
{
   uint64_t v;
   __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
   return v;
}

uint32_t timer_freq_hz(void)
{
   uint64_t f;
   __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
   return (uint32_t)f;
}

uint64_t timeout_init_us(uint32_t us)
{
   return read_cntpct() + ((uint64_t)us * timer_freq_hz()) / 1000000U;
}

int timeout_elapsed(uint64_t expire)
{
   return read_cntpct() > expire;
}

void udelay(uint32_t us)
{
   uint64_t expire = timeout_init_us(us);

   while (!timeout_elapsed(expire)) {
   }
}
