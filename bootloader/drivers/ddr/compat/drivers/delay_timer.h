/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: TF-A <drivers/delay_timer.h> -> drivers/timer.h. */

#ifndef DELAY_TIMER_H
#define DELAY_TIMER_H

#include "timer.h"

static inline void mdelay(uint32_t ms)
{
   udelay(ms * 1000U);
}

#endif /* DELAY_TIMER_H */
