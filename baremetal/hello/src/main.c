/* main.c -- STM32MP257F-EV1 bare-metal "Hello, world!" on USART2 (ST-LINK VCP).
 *
 * Runs from SYSRAM, loaded over USB DFU by the BootROM. No TF-A, no DDR, no SD.
 * USART2 TX = PA4 (AF6); kernel clock = HSI 64 MHz via RCC flexgen channel 8.
 * Register values lifted from TF-A's plat_crash_console_init for STM32MP25.
 */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* USART2 */
#define USART2 0x400E0000u
#define U_CR1 (USART2 + 0x00)
#define U_CR2 (USART2 + 0x04)
#define U_BRR (USART2 + 0x0C)
#define U_ISR (USART2 + 0x1C)
#define U_TDR (USART2 + 0x28)
/* RCC */
#define RCC 0x44200000u
#define RCC_USART2CFGR  (RCC + 0x780)
#define RCC_GPIOACFGR   (RCC + 0x52C)
#define RCC_XBAR8CFGR   (RCC + 0x1038)
#define RCC_PREDIV8CFGR (RCC + 0x1138)
#define RCC_FINDIV8CFGR (RCC + 0x1244)
/* GPIOA */
#define GPIOA 0x44240000u
#define GPIOA_MODER   (GPIOA + 0x00)
#define GPIOA_OSPEEDR (GPIOA + 0x08)
#define GPIOA_PUPDR   (GPIOA + 0x0C)
#define GPIOA_AFRL    (GPIOA + 0x20)

#define PIN 4u  /* PA4 */

static void uart_init(void)
{
	/* reset USART2 */
	REG(RCC_USART2CFGR) |= 1u;
	while ((REG(RCC_USART2CFGR) & 1u) == 0u) {}
	REG(RCC_USART2CFGR) &= ~1u;
	while ((REG(RCC_USART2CFGR) & 1u) != 0u) {}

	/* GPIOA clock + PA4 to AF6 */
	REG(RCC_GPIOACFGR) |= (1u << 1);
	REG(GPIOA_MODER)   = (REG(GPIOA_MODER) & ~(3u << (PIN * 2))) | (2u << (PIN * 2));
	REG(GPIOA_OSPEEDR) = REG(GPIOA_OSPEEDR) & ~(3u << (PIN * 2));
	REG(GPIOA_PUPDR)   = REG(GPIOA_PUPDR) & ~(3u << (PIN * 2));
	REG(GPIOA_AFRL)    = (REG(GPIOA_AFRL) & ~(0xFu << (PIN * 4))) | (6u << (PIN * 4));

	/* USART2 kernel clock: flexgen8 = HSI, /1 */
	REG(RCC_PREDIV8CFGR) = 0u;
	REG(RCC_FINDIV8CFGR) = 0x40u;        /* enable, div 1 */
	REG(RCC_XBAR8CFGR)   = 0x5u | 0x40u; /* src HSI | enable */
	REG(RCC_USART2CFGR) |= (1u << 1);    /* peripheral clock enable */

	/* configure USART2: 115200 8N1, TX only */
	REG(U_CR1) &= ~1u;                      /* UE=0 */
	REG(U_CR1) |= (1u << 3) | (1u << 29);   /* TE | FIFOEN */
	REG(U_CR2) &= ~(3u << 12);              /* STOP=0 */
	REG(U_BRR)  = 556u;                      /* 64 MHz / 115200 */
	REG(U_CR1) |= 1u;                        /* UE */
	while ((REG(U_ISR) & (1u << 21)) == 0u) {} /* TEACK */
}

static void uart_putc(char c)
{
	while ((REG(U_ISR) & (1u << 7)) == 0u) {} /* TXFNF */
	REG(U_TDR) = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *s)
{
	while (*s != '\0') {
		uart_putc(*s++);
	}
}

void main(void)
{
	uart_init();
	for (;;) {
		uart_puts("Hello, world!\r\n");
		for (volatile uint32_t d = 0u; d < 0x800000u; d++) {}
	}
}
