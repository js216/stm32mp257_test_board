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

#endif // CMD_H
