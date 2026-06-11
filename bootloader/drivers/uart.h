// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file uart.h
 * @brief USART2 console output (minimal, transmit only).
 * @copyright 2026 Jakob Kastelic
 */

#ifndef UART_H
#define UART_H

/** Configure USART2 (PA4/AF6) for 115200 8N1, transmit only. */
void uart_init(void);

/** Send one byte, blocking until the transmit register is free. */
void uart_putc(char c);

/** Send a NUL-terminated string; '\n' is expanded to "\r\n". */
void uart_puts(const char *s);

#endif // UART_H
