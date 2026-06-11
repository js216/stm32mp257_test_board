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
#include "i2c.h"
#include "pmic.h"
#include "printf.h"
#include "rcc.h"
#include "timer.h"
#include "uart.h"
#include <stdint.h>

#define HEARTBEAT_TICKS 4000000U

int main(void)
{
   rcc_clock_init();
   timer_init();
   uart_init();
   i2c_init();
   gpio_set_output(LED_GPIO_BASE, LED_PIN);
   printf_set_output(uart_putc);

   /* Let the clock and the TX line settle before the first transmission. */
   for (volatile unsigned int i = 0; i < 2000000U; i++) {
   }

   /*
    * Probe the STPMIC2 over I2C7, then bring up the rails as TF-A BL2
    * would (always-on rails + the LPDDR4 vdd1/vdd2 sequence for step 4).
    */
   uint8_t pid = 0;
   uint8_t pver = 0;
   int pmic_ok = (pmic_read(0x00U, &pid) == 0) && (pmic_read(0x01U, &pver) == 0);
   if (pmic_ok) {
      my_printf("PMIC product_id=0x%02X version=0x%02X\r\n",
                (unsigned int)pid, (unsigned int)pver);
   } else {
      my_printf("PMIC I2C error isr=0x%08X\r\n", (unsigned int)i2c_last_isr);
   }
   if (pmic_ok && (pmic_init_rails() == 0)) {
      pmic_print_rails();
      my_printf("RAILS OK\r\n");
   } else {
      my_printf("RAILS ERR isr=0x%08X\r\n", (unsigned int)i2c_last_isr);
   }

   /*
    * Cross-check the generic timer against the UART byte clock: 1000
    * characters at 115200 8N1 take 86.8 ms on the wire. A measurement far
    * off means CNTFRQ does not match the real counter rate and the DDR
    * timeouts/delays cannot be trusted.
    */
   uint64_t t0 = timeout_init_us(0);
   for (int k = 0; k < 10; k++) {
      for (int c = 0; c < 100; c++) {
         uart_putc('.');
      }
   }
   uart_putc('\r');
   uart_putc('\n');
   uint64_t t1 = timeout_init_us(0);
   unsigned int ms =
      (unsigned int)(((t1 - t0) * 1000U) / (uint64_t)timer_freq_hz());
   my_printf("TIMER cntfrq=%u uart1k=%u ms %s\r\n",
             (unsigned int)timer_freq_hz(), ms,
             ((ms >= 80U) && (ms <= 95U)) ? "OK" : "BAD");

   /* The ROM leaves the cluster on its slow bypass clock and nothing on
    * the lean image can raise it: do it here. */
   int a35 = rcc_a35_pll1_init();
   if (a35 == 0) {
      my_printf("A35 @ 1200 MHz\r\n");
   } else {
      my_printf("A35 PLL1 FAILED (%d), staying on bypass clock\r\n", a35);
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
