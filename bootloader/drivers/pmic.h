// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file pmic.h
 * @brief STPMIC2 power-management IC (on I2C7, address 0x33).
 * @copyright 2026 Jakob Kastelic
 */

#ifndef PMIC_H
#define PMIC_H

#include <stdint.h>

/** Read one STPMIC2 register. 0 on success. */
int pmic_read(uint8_t reg, uint8_t *val);

/** Write one STPMIC2 register. 0 on success. */
int pmic_write(uint8_t reg, uint8_t val);

/**
 * Bring up the rails the way TF-A BL2 does on the DK board: enable every
 * regulator-always-on rail (voltage left at the PMIC NVM default), then run
 * the LPDDR4 power-on sequence (vdd1_ddr = 1.8 V, vdd2_ddr = 1.1 V, enable
 * vdd1 before vdd2). 0 on success.
 */
int pmic_init_rails(void);

/** Print every rail's enable state and voltage to the console. */
void pmic_print_rails(void);

#endif // PMIC_H
