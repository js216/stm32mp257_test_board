// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file main.c
 * @brief Bootloader entry point.
 * @copyright 2026 Jakob Kastelic
 *
 * Step 2: bring up clocks, the USART2 console and the heartbeat LED, then
 * run the interactive command interpreter (try "help"). The blue LED blinks
 * as a liveness heartbeat while the console polls for input.
 */

#include "board.h"
#include "cmd.h"
#include "console.h"
#include "gpio.h"
#include "printf.h"
#include "rcc.h"
#include "uart.h"

#define HEARTBEAT_TICKS 4000000U

int main(void)
{
   rcc_clock_init();
   uart_init();
   gpio_set_output(LED_GPIO_BASE, LED_PIN);
   printf_set_output(uart_putc);

   /* Let the clock and the TX line settle before the first transmission. */
   for (volatile unsigned int i = 0; i < 2000000U; i++) {
   }

   cmd_init();

   unsigned int tick = 0;
   int led           = 0;
   while (1) {
      /* Feed any received bytes into the console ring buffer. */
      while (uart_rx_ready()) {
         console_push(uart_getc_raw());
      }
      cmd_poll();

      if (++tick >= HEARTBEAT_TICKS) {
         tick = 0;
         led ^= 1;
         gpio_write(LED_GPIO_BASE, LED_PIN, led);
      }
   }

   return 0;
}
