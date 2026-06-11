// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file board.h
 * @brief Board-specific pin assignments for the STM32MP257F-DK.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef BOARD_H
#define BOARD_H

#include "stm32mp25.h"

/* Blue heartbeat LED: GPIOH pin 7 (led-blue in the kernel device tree). */
#define LED_GPIO_BASE GPIOH_BASE
#define LED_PIN       7U

#endif // BOARD_H
