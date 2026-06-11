// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file ddr.h
 * @brief LPDDR4 bring-up for the STM32MP257F-DK.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef DDR_H
#define DDR_H

#include <stddef.h>

/**
 * @brief Initialise the DDR sub-system: bus clock, PLL2 (600 MHz), DDRCTRL
 *        + PHY (with training firmware), then run the TF-A data-bus,
 *        address-bus and size checks.
 *
 * @return 0 on success (4 GB LPDDR4 usable at 0x80000000).
 */
int ddr_init(void);

/** Detected memory size in bytes (0 before ddr_init()). */
size_t ddr_get_size(void);

#endif // DDR_H
