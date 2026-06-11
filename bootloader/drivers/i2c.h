// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file i2c.h
 * @brief Minimal polled I2C7 master (for the PMIC).
 * @copyright 2026 Jakob Kastelic
 */

#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/** Configure I2C7 (PD15 SCL / PD14 SDA, AF10) for ~100 kHz. */
void i2c_init(void);

/** Read one byte from register @p reg of 7-bit device @p dev. 0 on success. */
int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *val);

/** Write one byte @p val to register @p reg of 7-bit device @p dev. */
int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val);

/** ISR captured at the last I2C failure (diagnostic). */
extern uint32_t i2c_last_isr;

#endif // I2C_H
