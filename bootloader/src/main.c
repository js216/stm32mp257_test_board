// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file main.c
 * @brief Bootloader entry point.
 * @copyright 2026 Jakob Kastelic
 *
 * Step 1: bring up the minimum clocks, the USART2 console and the blue
 * heartbeat LED, then loop forever blinking the LED and greeting the world.
 */

#include "board.h"
#include "gpio.h"
#include "rcc.h"
#include "uart.h"

static void delay(volatile unsigned int loops)
{
   while (loops != 0U) {
      loops--;
   }
}

int main(void)
{
   rcc_clock_init();
   uart_init();
   gpio_set_output(LED_GPIO_BASE, LED_PIN);

   int led = 0;
   while (1) {
      uart_puts("Hello, world!\n");

      led ^= 1;
      gpio_write(LED_GPIO_BASE, LED_PIN, led);

      delay(50000000U);
   }

   return 0;
}
