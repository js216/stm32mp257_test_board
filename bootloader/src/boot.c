// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file boot.c
 * @brief Load images from SD into DDR and jump to them (step 7).
 * @copyright 2026 Jakob Kastelic
 */

#include "boot.h"
#include "ddr.h"
#include "gic.h"
#include "io.h"
#include "printf.h"
#include "psci.h"
#include "rif.h"
#include "sd.h"
#include "stm32mp25.h"
#include <stdint.h>

/* One sd_read() per chunk keeps the DTC transfer length modest and gives a
 * progress dot per MiB on the console. */
#define LOAD_CHUNK_BLOCKS 2048U /* 1 MiB */
#define SD_BLOCK_SIZE     512U

/* PWR_CR7: VDDIO2 I/O-domain validation, same layout as VDDIO1 in CR8. */
#define PWR_CR7_ADDR (PWR_BASE + 0x18UL)
#define VDDIO_VMEN   BIT(0)
#define VDDIO_SV     BIT(8)
#define VDDIO_RDY    BIT(16)

/* Both load_sd and jump need DDR + SD brought up; do it lazily so the plain
 * "ddr"/"sd" commands stay independently testable. */
static int boot_ensure_hw(void)
{
   if (ddr_get_size() == 0U) {
      my_printf("(ddr init...)\r\n");
      if (ddr_init() != 0) {
         my_printf("DDR init FAILED\r\n");
         return -1;
      }
   }
   if (sd_size_bytes() == 0ULL) {
      if (sd_init() != 0) {
         my_printf("SD init FAILED\r\n");
         return -1;
      }
   }
   return 0;
}

static int load_region(uint32_t n, uint32_t lba, uint32_t dest)
{
   uint32_t done = 0;
   while (done < n) {
      uint32_t cnt = n - done;
      if (cnt > LOAD_CHUNK_BLOCKS)
         cnt = LOAD_CHUNK_BLOCKS;
      if (sd_read(lba + done, (uintptr_t)(dest + done * SD_BLOCK_SIZE),
                  cnt * SD_BLOCK_SIZE) != 0) {
         my_printf("\r\nload: read FAILED at block %u\r\n",
                   (unsigned int)(lba + done));
         return -1;
      }
      done += cnt;
      my_printf(".");
   }
   return 0;
}

void cmd_load_sd(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
   uint32_t n    = BOOT_KERNEL_BLOCKS;
   uint32_t lba  = BOOT_KERNEL_LBA;
   uint32_t dest = BOOT_KERNEL_ADDR;

   if (argc >= 1)
      n = arg1;
   if (argc >= 2)
      lba = arg2;
   if (argc >= 3)
      dest = arg3;

   if (boot_ensure_hw() != 0)
      return;

   if (load_region(n, lba, dest) != 0)
      return;

   /* Word checksum so the host can verify the copy end-to-end. */
   uint32_t sum           = 0;
   const uint32_t *p      = (const uint32_t *)(uintptr_t)dest;
   const uint32_t n_words = n * (SD_BLOCK_SIZE / 4U);
   for (uint32_t i = 0; i < n_words; i++)
      sum += p[i];

   my_printf("\r\nloaded %u blocks from %u to 0x%08X sum 0x%08X\r\n",
             (unsigned int)n, (unsigned int)lba, (unsigned int)dest,
             (unsigned int)sum);
}

void cmd_jump(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
   (void)arg2;
   (void)arg3;

   uint32_t addr = BOOT_KERNEL_ADDR;
   if (argc >= 1)
      addr = arg1;

   my_printf("jump: calling 0x%08X\r\n", (unsigned int)addr);

   /* Caches are off, but the I-cache may hold stale lines from before the
    * load: invalidate it before executing freshly written memory. */
   __asm volatile("ic iallu\n dsb sy\n isb\n" ::: "memory");

   uint64_t (*entry)(void) = (uint64_t (*)(void))(uintptr_t)addr;
   uint64_t ret            = entry();

   my_printf("jump: returned 0x%08X%08X\r\n", (unsigned int)(ret >> 32),
             (unsigned int)ret);
}

/* Validate a VDDIO I/O domain like TF-A's pwr_enable_io_supply(): monitor
 * on, wait ready, then declare the supply valid. */
static void vddio2_enable(void)
{
   if ((mmio_read_32(PWR_CR7_ADDR) & VDDIO_SV) != 0U)
      return;

   mmio_setbits_32(PWR_CR7_ADDR, VDDIO_VMEN);
   for (volatile unsigned int i = 0; i < 1000000U; i++) {
      if ((mmio_read_32(PWR_CR7_ADDR) & VDDIO_RDY) != 0U) {
         mmio_setbits_32(PWR_CR7_ADDR, VDDIO_SV);
         mmio_clrbits_32(PWR_CR7_ADDR, VDDIO_VMEN);
         return;
      }
   }
   my_printf("boot: VDDIO2 not ready\r\n");
}

void cmd_boot(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
   (void)arg2;
   (void)arg3;

   uint32_t kernel_blocks = BOOT_KERNEL_BLOCKS;
   if (argc >= 1)
      kernel_blocks = arg1;

   if (boot_ensure_hw() != 0)
      return;

   my_printf("boot: loading kernel (%u blocks)", (unsigned int)kernel_blocks);
   if (load_region(kernel_blocks, BOOT_KERNEL_LBA, BOOT_KERNEL_ADDR) != 0)
      return;
   my_printf("\r\nboot: loading dtb");
   if (load_region(BOOT_DTB_BLOCKS, BOOT_DTB_LBA, BOOT_DTB_ADDR) != 0)
      return;

   vddio2_enable();

   /* Last console output from the FSBL: the UART belongs to Linux now. */
   my_printf("\r\nboot: Linux Image@0x%08X dtb@0x%08X (EL2, PSCI at EL3)\r\n",
             (unsigned int)BOOT_KERNEL_ADDR, (unsigned int)BOOT_DTB_ADDR);

   /* Open the firewalls AFTER the loads (a non-secure DDR base region cuts
    * off our own secure writes), then hand the GIC to the kernel. */
   rif_handoff();
   gic_dist_init();
   gic_cpuif_init();

   __asm volatile("ic iallu\n dsb sy\n isb\n" ::: "memory");

   drop_to_el2(BOOT_DTB_ADDR, BOOT_KERNEL_ADDR);
}
