// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file gic.h
 * @brief GIC-400 (GICv2) hand-off configuration for the non-secure world.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef GIC_H
#define GIC_H

/** Distributor: all interrupts disabled, non-pending and Group1 (NS). */
void gic_dist_init(void);

/** Per-CPU interface: banked SGI/PPI group, PMR, secure CTLR. Core 1 does
 *  the same writes in its assembly hand-off path (startup.S pen). */
void gic_cpuif_init(void);

#endif // GIC_H
