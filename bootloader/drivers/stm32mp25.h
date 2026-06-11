// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file stm32mp25.h
 * @brief STM32MP25 peripheral base addresses.
 * @copyright 2026 Jakob Kastelic
 *
 * Addresses taken from TF-A plat/st/stm32mp2/stm32mp2_def.h (BSD-3-Clause).
 */

#ifndef STM32MP25_H
#define STM32MP25_H

/* SYSRAM: where the boot ROM loads and runs this FSBL. */
#define SYSRAM_BASE 0x0E000000UL
#define SYSRAM_SIZE 0x00040000UL /* 256 KB */

#define RCC_BASE  0x44200000UL
#define PWR_BASE  0x44210000UL

/* GPIO banks A..I are spaced 0x10000 apart; bank 0 = GPIOA. */
#define GPIO_BANK(n) (0x44240000UL + ((unsigned long)(n) * 0x10000UL))
#define GPIOA_BASE GPIO_BANK(0)
#define GPIOH_BASE GPIO_BANK(7)

#define USART2_BASE 0x400E0000UL
#define STGEN_BASE  0x48080000UL

#endif // STM32MP25_H
