// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file rif.h
 * @brief RIF firewall hand-off: every resource non-secure, DDR open.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef RIF_H
#define RIF_H

/**
 * @brief Program the RIF hand-off state for the non-secure kernel: RIFSC,
 *        RCC, PWR, EXTI1/2, GPIO and TAMP resources non-secure and
 *        CID-filter-free (except CRYP2/IWDG3 kept for the M33), and the
 *        whole DDR opened to the non-secure world via the RISAF4 base
 *        region. Call AFTER all images are loaded into DDR -- once the
 *        base region is non-secure this (secure) bootloader can no longer
 *        write DDR.
 */
void rif_handoff(void);

#endif // RIF_H
