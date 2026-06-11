// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file uart.h
 * @brief USART2 console (transmit + polled receive).
 * @copyright 2026 Jakob Kastelic
 */

#ifndef UART_H
#define UART_H

/** Configure USART2 (PA4/AF6 TX, PA8/AF8 RX) for 115200 8N1. */
void uart_init(void);

/** Send one byte, blocking until the transmit register is free. */
void uart_putc(char c);

/** Send a NUL-terminated string; '\n' is expanded to "\r\n". */
void uart_puts(const char *s);

/** Return non-zero if a received byte is waiting. */
int uart_rx_ready(void);

/** Read one received byte (call only when uart_rx_ready() is true). */
char uart_getc_raw(void);

#endif // UART_H
