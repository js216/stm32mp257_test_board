// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file psci.h
 * @brief Minimal EL3-resident PSCI 1.0 implementation.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef PSCI_H
#define PSCI_H

#include <stdint.h>

/**
 * @brief SMC dispatcher, called from the EL3 vectors (startup.S) with the
 *        SMCCC arguments. Implements PSCI_VERSION, CPU_ON, CPU_OFF,
 *        AFFINITY_INFO, MIGRATE_INFO_TYPE and PSCI_FEATURES; everything
 *        else returns NOT_SUPPORTED.
 *
 * @return The SMCCC x0 result.
 */
uint64_t psci_smc(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3);

/**
 * @brief Enter the non-secure world at EL2 (startup.S). Sets SMPEN and
 *        CNTFRQ on the calling core, programs SCR_EL3/SPSR/ELR for an
 *        AArch64 EL2h entry with interrupts masked, and erets.
 *
 * @param arg   value for the payload's x0 (the dtb address for Linux)
 * @param entry physical entry address
 */
void drop_to_el2(uint64_t arg, uint64_t entry) __attribute__((noreturn));

#endif // PSCI_H
