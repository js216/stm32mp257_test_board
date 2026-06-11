// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file uart.c
 * @brief USART2 console (transmit + polled receive).
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
#define USART_ICR   0x20U
#define USART_RDR   0x24U
#define USART_TDR   0x28U
#define USART_PRESC 0x2CU

#define USART_CR1_UE   BIT(0)
#define USART_CR1_RE   BIT(2)
#define USART_CR1_TE   BIT(3)
#define USART_CR1_FIFOEN BIT(29)
#define USART_ISR_RXNE BIT(5) /* read data register not empty */
#define USART_ISR_TXE  BIT(7) /* transmit data register empty */

/* ISR error flags + their ICR clear bits (same positions). */
#define USART_ERR_FLAGS (BIT(0) | BIT(1) | BIT(2) | BIT(3)) /* PE FE NE ORE */

#define HSI_FREQ_HZ 64000000UL
#define UART_BAUD   115200UL

/* USART2_TX = PA4/AF6, USART2_RX = PA8/AF8 (stm32mp25-pinctrl.dtsi). */
#define UART_TX_PIN 4U
#define UART_TX_AF  6U
#define UART_RX_PIN 8U
#define UART_RX_AF  8U

void uart_init(void)
{
   gpio_set_af(GPIOA_BASE, UART_TX_PIN, UART_TX_AF);
   gpio_set_af(GPIOA_BASE, UART_RX_PIN, UART_RX_AF);

   /* Disable while configuring, then set baud and enable TX + RX. */
   mmio_write_32(USART2_BASE + USART_CR1, 0U);
   mmio_write_32(USART2_BASE + USART_CR2, 0U);
   mmio_write_32(USART2_BASE + USART_CR3, 0U);
   mmio_write_32(USART2_BASE + USART_PRESC, 0U);
   mmio_write_32(USART2_BASE + USART_BRR,
                 (HSI_FREQ_HZ + (UART_BAUD / 2UL)) / UART_BAUD);
   mmio_write_32(USART2_BASE + USART_CR1,
                 USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                 USART_CR1_FIFOEN);

   /* Clear any stale flags so the receiver starts clean. */
   mmio_write_32(USART2_BASE + USART_ICR, 0xFFFFFFFFU);
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

int uart_rx_ready(void)
{
   uint32_t isr = mmio_read_32(USART2_BASE + USART_ISR);

   /* An overrun/framing error stalls RXNE updates -- clear it. */
   if ((isr & USART_ERR_FLAGS) != 0U) {
      mmio_write_32(USART2_BASE + USART_ICR, USART_ERR_FLAGS);
   }

   return (isr & USART_ISR_RXNE) != 0U;
}

char uart_getc_raw(void)
{
   return (char)(mmio_read_32(USART2_BASE + USART_RDR) & 0xFFU);
}
