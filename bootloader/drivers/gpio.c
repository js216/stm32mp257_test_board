// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file gpio.c
 * @brief Minimal GPIO control.
 * @copyright 2026 Jakob Kastelic
 *
 * Standard STM32 GPIO register layout (see TF-A drivers/st/gpio/stm32_gpio.c,
 * BSD-3-Clause).
 */

#include "gpio.h"
#include "io.h"

#define GPIO_MODER  0x00U /* 2 bits/pin: 00 in, 01 out, 10 alt, 11 analog */
#define GPIO_OTYPER 0x04U /* 1 bit/pin: 0 push-pull, 1 open-drain */
#define GPIO_OSPEEDR 0x08U /* 2 bits/pin: 00 low ... 11 very high */
#define GPIO_BSRR   0x18U /* lower 16: set; upper 16: reset */
#define GPIO_AFRL   0x20U /* pins 0..7,  4 bits/pin */
#define GPIO_AFRH   0x24U /* pins 8..15, 4 bits/pin */

#define MODE_OUTPUT 0x1U
#define MODE_ALT    0x2U

void gpio_set_output(uintptr_t base, unsigned int pin)
{
   mmio_clrsetbits_32(base + GPIO_MODER, 0x3U << (pin * 2U),
                      MODE_OUTPUT << (pin * 2U));
}

void gpio_set_af(uintptr_t base, unsigned int pin, unsigned int af)
{
   uintptr_t afr = base + ((pin < 8U) ? GPIO_AFRL : GPIO_AFRH);
   unsigned int shift = (pin & 0x7U) * 4U;

   mmio_clrsetbits_32(afr, 0xFU << shift, (af & 0xFU) << shift);
   mmio_clrsetbits_32(base + GPIO_MODER, 0x3U << (pin * 2U),
                      MODE_ALT << (pin * 2U));
}

void gpio_set_af_od(uintptr_t base, unsigned int pin, unsigned int af)
{
   mmio_setbits_32(base + GPIO_OTYPER, 1U << pin);
   gpio_set_af(base, pin, af);
}

void gpio_set_output_od(uintptr_t base, unsigned int pin)
{
   mmio_setbits_32(base + GPIO_OTYPER, 1U << pin);
   mmio_clrsetbits_32(base + GPIO_MODER, 0x3U << (pin * 2U),
                      MODE_OUTPUT << (pin * 2U));
}

void gpio_set_speed(uintptr_t base, unsigned int pin, unsigned int speed)
{
   mmio_clrsetbits_32(base + GPIO_OSPEEDR, 0x3U << (pin * 2U),
                      (speed & 0x3U) << (pin * 2U));
}

void gpio_write(uintptr_t base, unsigned int pin, int val)
{
   uint32_t bit = (val != 0) ? (1U << pin) : (1U << (pin + 16U));

   mmio_write_32(base + GPIO_BSRR, bit);
}
