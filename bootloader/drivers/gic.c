// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file gic.c
 * @brief GIC-400 (GICv2) hand-off configuration for the non-secure world.
 * @copyright 2026 Jakob Kastelic
 *
 * The kernel owns the GIC from the non-secure side; the secure side only has
 * to mark every interrupt Group1 (non-secure), enable both groups in the
 * distributor, and open the CPU interface (permissive PMR, bypass disable)
 * the way TF-A's GICv2 driver leaves it.
 */

#include "gic.h"
#include "io.h"
#include <stdint.h>

#define GICD_BASE 0x4AC10000UL
#define GICC_BASE 0x4AC20000UL

#define GICD_CTLR      0x000UL
#define GICD_TYPER     0x004UL
#define GICD_IGROUPR   0x080UL
#define GICD_ICENABLER 0x180UL
#define GICD_ICPENDR   0x280UL

#define GICC_CTLR 0x000UL
#define GICC_PMR  0x004UL

/* Secure GICC_CTLR: EnableGrp0 | FIQEn | all bypass-disable bits; Group1
 * signalling is enabled by the kernel through the non-secure alias. */
#define GICC_CTLR_SECURE_VAL 0x1E9UL

void gic_dist_init(void)
{
   uint32_t words = ((mmio_read_32(GICD_BASE + GICD_TYPER) & 0x1FU) + 1U);

   mmio_write_32(GICD_BASE + GICD_CTLR, 0U);

   for (uint32_t i = 0; i < words; i++) {
      mmio_write_32(GICD_BASE + GICD_ICENABLER + 4U * i, 0xFFFFFFFFU);
      mmio_write_32(GICD_BASE + GICD_ICPENDR + 4U * i, 0xFFFFFFFFU);
      mmio_write_32(GICD_BASE + GICD_IGROUPR + 4U * i, 0xFFFFFFFFU);
   }

   mmio_write_32(GICD_BASE + GICD_CTLR, 3U); /* EnableGrp0 | EnableGrp1 */
}

void gic_cpuif_init(void)
{
   /* IGROUPR0 (SGIs/PPIs) is banked per CPU. */
   mmio_write_32(GICD_BASE + GICD_IGROUPR, 0xFFFFFFFFU);
   mmio_write_32(GICC_BASE + GICC_PMR, 0xFFU);
   mmio_write_32(GICC_BASE + GICC_CTLR, GICC_CTLR_SECURE_VAL);
}
