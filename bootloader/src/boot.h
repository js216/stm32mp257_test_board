// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file boot.h
 * @brief Load images from SD into DDR and jump to them (step 7).
 * @copyright 2026 Jakob Kastelic
 */

#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

/*
 * Staging area on the SD card for the raw kernel Image and dtb, written by
 * the host over USB-MSC (msc.mp257:write offset_lba=...). It sits at 128 MiB
 * / 160 MiB, far beyond the GPT partitions (sdcard.img is ~42 MiB) and far
 * below the backup GPT at the end of the card.
 *
 * Load addresses replicate the proven direct-boot BL31->BL33 hand-off:
 * kernel at DDR+64 MiB, dtb at kernel+32 MiB, x0 = dtb.
 */
#define BOOT_KERNEL_LBA    262144U     /* 128 MiB / 512 */
#define BOOT_KERNEL_BLOCKS 49152U      /* 24 MiB */
#define BOOT_KERNEL_ADDR   0x84000000U
#define BOOT_DTB_LBA       327680U     /* 160 MiB / 512 */
#define BOOT_DTB_BLOCKS    512U        /* 256 KiB */
#define BOOT_DTB_ADDR      0x86000000U

/** "load_sd [len_blocks [sd_block [dest_addr]]]": SD blocks -> memory. */
void cmd_load_sd(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/** "jump [target_addr]": call a bare function in loaded memory (EL3). */
void cmd_jump(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/** "boot [kernel_blocks]": load kernel + dtb from the SD staging area and
 *  enter Linux at EL2 non-secure (x0 = dtb), PSCI resident at EL3. */
void cmd_boot(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif // BOOT_H
