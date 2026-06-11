// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file uart.c
 * @brief USART2 console output (minimal, transmit only).
 * @copyright 2026 Jakob Kastelic
 *
 * Register offsets and the configure/putc sequence follow TF-A
 * (drivers/st/uart/stm32_uart.c and stm32_uart_regs.h, BSD-3-Clause). The
 * kernel clock is HSI (64 MHz), set up by rcc_clock_init(); BRR is therefore
 * a compile-time constant with oversampling by 16.
 */

#include "uart.h"
#include "io.h"
#include "gpio.h"
#include "stm32mp25.h"

#define USART_CR1   0x00U
#define USART_CR2   0x04U
#define USART_CR3   0x08U
#define USART_BRR   0x0CU
#define USART_ISR   0x1CU
#define USART_TDR   0x28U
#define USART_PRESC 0x2CU

#define USART_CR1_UE  BIT(0)
#define USART_CR1_TE  BIT(3)
#define USART_ISR_TXE BIT(7) /* transmit data register empty */

#define HSI_FREQ_HZ 64000000UL
#define UART_BAUD   115200UL

/* USART2_TX = PA4, alternate function 6 (stm32mp25-pinctrl.dtsi). */
#define UART_TX_PIN 4U
#define UART_TX_AF  6U

void uart_init(void)
{
   gpio_set_af(GPIOA_BASE, UART_TX_PIN, UART_TX_AF);

   /* Disable while configuring, then set baud and re-enable for TX. */
   mmio_write_32(USART2_BASE + USART_CR1, 0U);
   mmio_write_32(USART2_BASE + USART_CR2, 0U);
   mmio_write_32(USART2_BASE + USART_CR3, 0U);
   mmio_write_32(USART2_BASE + USART_PRESC, 0U);
   mmio_write_32(USART2_BASE + USART_BRR,
                 (HSI_FREQ_HZ + (UART_BAUD / 2UL)) / UART_BAUD);
   mmio_write_32(USART2_BASE + USART_CR1, USART_CR1_UE | USART_CR1_TE);
}

void uart_putc(char c)
{
   while ((mmio_read_32(USART2_BASE + USART_ISR) & USART_ISR_TXE) == 0U) {
      /* wait for room in the transmit register */
   }
   mmio_write_32(USART2_BASE + USART_TDR, (uint32_t)(unsigned char)c);
}

void uart_puts(const char *s)
{
   while (*s != '\0') {
      if (*s == '\n') {
         uart_putc('\r');
      }
      uart_putc(*s);
      s++;
   }
}
