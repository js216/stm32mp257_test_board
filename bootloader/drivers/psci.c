// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file psci.c
 * @brief Minimal EL3-resident PSCI 1.0 implementation.
 * @copyright 2026 Jakob Kastelic
 *
 * Just enough PSCI for the direct-boot kernel: CPU_ON releases core 1 from
 * the startup.S holding pen into the non-secure world, CPU_OFF puts it
 * back, SET_SUSPEND_MODE accepts platform-coordinated mode (which keeps
 * the kernel from logging a firmware bug). CPU_SUSPEND and SYSTEM_RESET
 * report NOT_SUPPORTED (see features() for why suspend must stay off);
 * the bench resets the board externally.
 *
 * No printing here: once the kernel runs, the UART belongs to it.
 */

#include "psci.h"
#include <stdint.h>

#define PSCI_VERSION_FID    0x84000000U
#define PSCI_CPU_SUSPEND32  0x84000001U
#define PSCI_CPU_SUSPEND64  0xC4000001U
#define PSCI_CPU_OFF        0x84000002U
#define PSCI_CPU_ON32       0x84000003U
#define PSCI_CPU_ON64       0xC4000003U
#define PSCI_AFFINITY32     0x84000004U
#define PSCI_AFFINITY64     0xC4000004U
#define PSCI_MIG_INFO_TYPE  0x84000006U
#define PSCI_FEATURES       0x8400000AU
#define PSCI_SET_SUSPEND_MODE 0x8400000FU

#define PSCI_RET_NOT_SUPPORTED  (-1)
#define PSCI_RET_INVALID_PARAMS (-2)
#define PSCI_RET_ALREADY_ON     (-4)

/* Holding-pen mailbox in .data (startup.S): entry then context for the
 * core being powered on. Written here on core 0, consumed by core 1. */
extern volatile uint64_t psci_pen_entry;
extern volatile uint64_t psci_pen_ctx;

/* Park the calling core back in the startup.S pen (never returns). */
extern void psci_pen_return(void) __attribute__((noreturn));

static volatile uint32_t core1_on;

static int64_t cpu_on(uint64_t mpidr, uint64_t entry, uint64_t ctx)
{
   if ((mpidr & 0xFFFFFFU) != 1U) {
      return PSCI_RET_INVALID_PARAMS;
   }
   if (core1_on != 0U) {
      return PSCI_RET_ALREADY_ON;
   }

   psci_pen_ctx   = ctx;
   psci_pen_entry = entry; /* non-zero releases the pen */
   core1_on       = 1U;
   __asm volatile("dsb sy\n sev\n" ::: "memory");

   return 0;
}

static int64_t affinity_info(uint64_t mpidr)
{
   uint64_t aff = mpidr & 0xFFFFFFU;

   if (aff == 0U) {
      return 0; /* ON */
   }
   if (aff == 1U) {
      return (core1_on != 0U) ? 0 : 1; /* ON : OFF */
   }
   return PSCI_RET_INVALID_PARAMS;
}

static int64_t features(uint32_t fid)
{
   switch (fid) {
   case PSCI_VERSION_FID:
   case PSCI_CPU_OFF:
   case PSCI_CPU_ON32:
   case PSCI_CPU_ON64:
   case PSCI_AFFINITY32:
   case PSCI_AFFINITY64:
   case PSCI_MIG_INFO_TYPE:
   case PSCI_FEATURES:
   case PSCI_SET_SUSPEND_MODE:
      return 0;
   /*
    * CPU_SUSPEND stays NOT_SUPPORTED: hardware-verified that an EL3 WFI
    * here never wakes once the kernel's cpuidle stops the local timer
    * (first suspend hangs the boot). SET_SUSPEND_MODE alone is enough
    * to silence the kernel's "[Firmware Bug] set PC mode" complaint;
    * a real implementation needs GIC wake plumbing at EL3.
    */
   default:
      return PSCI_RET_NOT_SUPPORTED;
   }
}

uint64_t psci_smc(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3)
{
   switch ((uint32_t)fid) {
   case PSCI_VERSION_FID:
      return 0x00010000U; /* PSCI 1.0 */

   case PSCI_CPU_ON32:
   case PSCI_CPU_ON64:
      return (uint64_t)cpu_on(a1, a2, a3);

   case PSCI_CPU_OFF:
      core1_on = 0U;
      psci_pen_return();

   case PSCI_AFFINITY32:
   case PSCI_AFFINITY64:
      return (uint64_t)affinity_info(a1);

   case PSCI_MIG_INFO_TYPE:
      return 2U; /* no Trusted OS */

   case PSCI_SET_SUSPEND_MODE:
      /* Platform-coordinated (0) is the only mode; OSI unsupported. */
      return (a1 == 0U) ? 0U : (uint64_t)(int64_t)PSCI_RET_NOT_SUPPORTED;

   case PSCI_FEATURES:
      return (uint64_t)features((uint32_t)a1);

   default:
      (void)a2;
      (void)a3;
      return (uint64_t)(int64_t)PSCI_RET_NOT_SUPPORTED;
   }
}
