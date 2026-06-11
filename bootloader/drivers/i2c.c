// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file i2c.c
 * @brief Minimal polled I2C7 master (for the PMIC).
 * @copyright 2026 Jakob Kastelic
 *
 * Register layout and the master read/write sequence follow TF-A
 * (drivers/st/i2c/stm32_i2c.c, include/drivers/st/stm32_i2c.h, BSD-3-Clause),
 * reduced to blocking single-register transfers. TIMINGR is precomputed for
 * the I2C7 kernel clock = HSI 64 MHz at Standard-mode 100 kHz using TF-A's
 * timing constraints (l_min 4700 ns, h_min 4000 ns, su 250 ns).
 */

#include "i2c.h"
#include "io.h"
#include "gpio.h"
#include "stm32mp25.h"

#define I2C7_BASE 0x40180000UL

#define I2C_CR1     0x00U
#define I2C_CR2     0x04U
#define I2C_TIMINGR 0x10U
#define I2C_ISR     0x18U
#define I2C_ICR     0x1CU
#define I2C_RXDR    0x24U
#define I2C_TXDR    0x28U

#define CR1_PE      BIT(0)
#define CR2_RD_WRN  BIT(10)
#define CR2_START   BIT(13)
#define CR2_AUTOEND BIT(25)
#define ISR_TXIS    BIT(1)
#define ISR_RXNE    BIT(2)
#define ISR_NACKF   BIT(4)
#define ISR_STOPF   BIT(5)
#define ISR_TC      BIT(6)
#define ICR_NACKCF  BIT(4)
#define ICR_STOPCF  BIT(5)

/*
 * Statically computed TIMINGR for I2C7 kernel clock = HSI 64 MHz (tI2CCLK =
 * 15.625 ns), Standard-mode 100 kHz:
 *   PRESC  = 0x7 -> tPRESC = (7+1)*15.625 ns        = 125 ns
 *   SCLL   = 0x25 -> tLOW  = (0x25+1)*tPRESC = 38*125 = 4750 ns (>= 4700 min)
 *   SCLH   = 0x20 -> tHIGH = (0x20+1)*tPRESC = 33*125 = 4125 ns (>= 4000 min)
 *   SCLDEL = 0x4 -> data setup = (4+1)*tPRESC        = 625 ns (>= 250 min)
 *   SDADEL = 0x2 -> data hold  = 2*tPRESC            = 250 ns
 *   period ~= tLOW + tHIGH ~= 8.9 us -> ~100 kHz
 * TIMINGR = PRESC<<28 | SCLDEL<<20 | SDADEL<<16 | SCLH<<8 | SCLL.
 */
#define I2C_TIMINGR_VALUE 0x70422025U

#define I2C_TIMEOUT 2000000U

/* I2C7: SCL = PD15/AF10, SDA = PD14/AF10, open-drain. */
#define I2C_PORT GPIO_BANK(3) /* GPIOD */
#define I2C_SCL  15U
#define I2C_SDA  14U
#define I2C_AF   10U

static void i2c_delay(void)
{
   for (volatile unsigned int d = 0; d < 3000U; d++) {
   }
}

void i2c_init(void)
{
   /* Bus recovery: the boot ROM can leave a PMIC slave mid-transfer holding
    * the bus (ISR BUSY stuck). Drive SCL as an open-drain GPIO, clock 9 pulses
    * with SDA released, then issue a STOP, to free it. */
   gpio_set_output_od(I2C_PORT, I2C_SDA);
   gpio_set_output_od(I2C_PORT, I2C_SCL);
   gpio_write(I2C_PORT, I2C_SDA, 1);
   gpio_write(I2C_PORT, I2C_SCL, 1);
   i2c_delay();
   for (int i = 0; i < 9; i++) {
      gpio_write(I2C_PORT, I2C_SCL, 0);
      i2c_delay();
      gpio_write(I2C_PORT, I2C_SCL, 1);
      i2c_delay();
   }
   gpio_write(I2C_PORT, I2C_SDA, 0); /* STOP: SDA low ... */
   i2c_delay();
   gpio_write(I2C_PORT, I2C_SDA, 1); /* ... -> high while SCL high */
   i2c_delay();

   gpio_set_af_od(I2C_PORT, I2C_SCL, I2C_AF);
   gpio_set_af_od(I2C_PORT, I2C_SDA, I2C_AF);

   mmio_clrbits_32(I2C7_BASE + I2C_CR1, CR1_PE);
   mmio_write_32(I2C7_BASE + I2C_TIMINGR, I2C_TIMINGR_VALUE);
   mmio_setbits_32(I2C7_BASE + I2C_CR1, CR1_PE);
}

uint32_t i2c_last_isr; /* ISR at last failure (diagnostic) */

static int wait_flag(uint32_t flag)
{
   unsigned int n = I2C_TIMEOUT;
   for (;;) {
      uint32_t isr = mmio_read_32(I2C7_BASE + I2C_ISR);
      if ((isr & ISR_NACKF) != 0U) {
         i2c_last_isr = isr;
         mmio_write_32(I2C7_BASE + I2C_ICR, ICR_NACKCF);
         return -1;
      }
      if ((isr & flag) != 0U) {
         return 0;
      }
      if (--n == 0U) {
         i2c_last_isr = isr;
         return -1;
      }
   }
}

static void wait_stop(void)
{
   unsigned int n = I2C_TIMEOUT;
   while (((mmio_read_32(I2C7_BASE + I2C_ISR) & ISR_STOPF) == 0U) &&
          (--n != 0U)) {
   }
   mmio_write_32(I2C7_BASE + I2C_ICR, ICR_STOPCF);
}

int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *val)
{
   /* Phase 1: write the register address, hold the bus (no AUTOEND). */
   mmio_write_32(I2C7_BASE + I2C_CR2,
                 ((uint32_t)dev << 1) | (1U << 16) | CR2_START);
   if (wait_flag(ISR_TXIS) != 0) {
      return -1;
   }
   mmio_write_32(I2C7_BASE + I2C_TXDR, reg);
   if (wait_flag(ISR_TC) != 0) {
      return -1;
   }

   /* Phase 2: repeated start, read one byte, AUTOEND issues STOP. */
   mmio_write_32(I2C7_BASE + I2C_CR2, ((uint32_t)dev << 1) | (1U << 16) |
                                          CR2_START | CR2_RD_WRN | CR2_AUTOEND);
   if (wait_flag(ISR_RXNE) != 0) {
      return -1;
   }
   *val = (uint8_t)(mmio_read_32(I2C7_BASE + I2C_RXDR) & 0xFFU);
   wait_stop();
   return 0;
}

int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{
   mmio_write_32(I2C7_BASE + I2C_CR2, ((uint32_t)dev << 1) | (2U << 16) |
                                          CR2_START | CR2_AUTOEND);
   if (wait_flag(ISR_TXIS) != 0) {
      return -1;
   }
   mmio_write_32(I2C7_BASE + I2C_TXDR, reg);
   if (wait_flag(ISR_TXIS) != 0) {
      return -1;
   }
   mmio_write_32(I2C7_BASE + I2C_TXDR, val);
   wait_stop();
   return 0;
}
