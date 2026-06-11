// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file timer.h
 * @brief Microsecond delays/timeouts from the ARM generic timer.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/** Start the STGEN system counter at 64 MHz and program CNTFRQ_EL0. */
void timer_init(void);

/** Counter frequency (CNTFRQ_EL0), Hz. */
uint32_t timer_freq_hz(void);

/** Deadline @us microseconds from now, for timeout_elapsed(). */
uint64_t timeout_init_us(uint32_t us);

/** Nonzero once @expire (from timeout_init_us) has passed. */
int timeout_elapsed(uint64_t expire);

/** Busy-wait for @us microseconds. */
void udelay(uint32_t us);

#endif // TIMER_H
