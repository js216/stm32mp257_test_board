/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: TF-A <common/debug.h> -> console printf. */

#ifndef DEBUG_H
#define DEBUG_H

#include "printf.h"

void __dead2_panic(void);
#define panic() __dead2_panic()

#define ERROR(...)  my_printf("DDR ERR: " __VA_ARGS__)
#define NOTICE(...) my_printf(__VA_ARGS__)
#define WARN(...)   my_printf("DDR WRN: " __VA_ARGS__)
#ifdef DEBUG_INFO_PRINT
#define INFO(...)   my_printf(__VA_ARGS__)
#else
#define INFO(...)   ((void)0)
#endif
#define VERBOSE(...) ((void)0)

#define EARLY_ERROR(...)   ERROR(__VA_ARGS__)
#define EARLY_NOTICE(...)  NOTICE(__VA_ARGS__)
#define EARLY_WARN(...)    WARN(__VA_ARGS__)
#define EARLY_INFO(...)    INFO(__VA_ARGS__)
#define EARLY_VERBOSE(...) VERBOSE(__VA_ARGS__)

#endif /* DEBUG_H */
