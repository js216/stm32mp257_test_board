// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file rcc.h
 * @brief Reset and clock control (minimal).
 * @copyright 2026 Jakob Kastelic
 */

#ifndef RCC_H
#define RCC_H

/**
 * @brief Bring up the clocks needed for the console and the heartbeat LED.
 *
 * Routes flexgen channel 8 (USART2 kernel clock) to HSI and enables the
 * USART2, GPIOA and GPIOH peripheral gates. Assumes the boot ROM left HSI
 * running and the interconnect clocked.
 */
void rcc_clock_init(void);

#endif // RCC_H
