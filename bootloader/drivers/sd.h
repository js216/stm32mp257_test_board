// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file sd.h
 * @brief SD card access (SDMMC1) for the STM32MP257F-DK.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef SD_H
#define SD_H

#include <stdint.h>

/** Power, clock, pin-mux and enumerate the SD card. 0 on success. */
int sd_init(void);

/** Read @size bytes (multiple of 512) starting at block @lba. 0 on success. */
int sd_read(unsigned int lba, uintptr_t buf, unsigned int size);

/** Write @size bytes (multiple of 512) starting at block @lba. 0 on success. */
int sd_write(unsigned int lba, uintptr_t buf, unsigned int size);

/** Card capacity in bytes (valid after sd_init()). */
unsigned long long sd_size_bytes(void);

#endif // SD_H
