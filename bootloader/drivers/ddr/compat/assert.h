/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: assert() -> panic, no glibc __assert_fail dependency. */

#ifndef ASSERT_H
#define ASSERT_H

#include "common/debug.h"

#define assert(e) ((e) ? (void)0 : __dead2_panic())

#endif /* ASSERT_H */
