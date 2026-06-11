// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file gpio.h
 * @brief Minimal GPIO control.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

/** Configure @p pin of the bank at @p base as a push-pull output. */
void gpio_set_output(uintptr_t base, unsigned int pin);

/** Configure @p pin of the bank at @p base for alternate function @p af. */
void gpio_set_af(uintptr_t base, unsigned int pin, unsigned int af);

/** As gpio_set_af() but open-drain (for I2C). */
void gpio_set_af_od(uintptr_t base, unsigned int pin, unsigned int af);

/** Configure @p pin as an open-drain output (for bit-banged I2C recovery). */
void gpio_set_output_od(uintptr_t base, unsigned int pin);

/** Drive @p pin of the bank at @p base high (val != 0) or low (val == 0). */
/** Set the pin output speed (OSPEEDR, 0 = low ... 3 = very high). */
void gpio_set_speed(uintptr_t base, unsigned int pin, unsigned int speed);

void gpio_write(uintptr_t base, unsigned int pin, int val);

#endif // GPIO_H
