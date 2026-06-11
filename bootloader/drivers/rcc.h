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

/**
 * @brief Bring up PLL2 (DDR clock) at 600 MHz from the 40 MHz HSE.
 *
 * Follows TF-A's integer-mode PLL sequence with the STM32MP257F-DK divider
 * set (FBDIV 30, FREFDIV 1, POSTDIV 1*2).
 *
 * @return 0 once the PLL reports lock, -1 on timeout.
 */
int rcc_pll2_init(void);

/**
 * @brief Raise the A35 cluster to 1200 MHz via its own PLL1 (A35SSC),
 *        following TF-A's bypass -> configure -> lock -> switch sequence.
 *        Neither the boot ROM nor the lean Linux raises the CPU clock
 *        (the kernel ca35ss driver only proxies SIP SMCs).
 *
 * @return 0 on success; negative with the cluster left on the bypass
 *         clock on timeout.
 */
int rcc_a35_pll1_init(void);

/** Route the SDMMC1 kernel clock (flexgen channel 51) to HSI 64 MHz. */
void rcc_sdmmc1_clk_init(void);

/** Print the PLL2-related RCC registers (diagnostic). */
void rcc_pll2_dump(void);

#endif // RCC_H
