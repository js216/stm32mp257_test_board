// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file cmd.h
 * @brief Command line interface.
 * @copyright 2025-2026 Jakob Kastelic
 */

#ifndef CMD_H
#define CMD_H

#include <stdint.h>

/** Print the banner and the first prompt. */
void cmd_init(void);

/** Drain the console ring buffer: line editing, history, dispatch. */
void cmd_poll(void);

/** "help" command handler. */
void cmd_help(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/** "pmic" command handler: read STPMIC2 product id + version. */
void cmd_pmic(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/** Initialise the SD card and read the MBR (step 5). */
void cmd_sd(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/** Save/write/verify/restore the SD last block (step 5). */
void cmd_sdw(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/** Export the SD card over USB mass storage (step 6). */
void cmd_usb(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/** Initialise and test the LPDDR4 (step 4). */
void cmd_ddr(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif // CMD_H
