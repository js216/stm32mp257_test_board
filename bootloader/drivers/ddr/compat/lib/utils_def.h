/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: the TF-A <lib/utils_def.h> macros the DDR code uses. */

#ifndef UTILS_DEF_H
#define UTILS_DEF_H

#define U(v)  (v##U)
#define UL(v) (v##UL)
#define ULL(v) (v##ULL)

#define BIT_32(n) (U(1) << (n))
#define BIT_64(n) (ULL(1) << (n))
#ifndef BIT
#define BIT(n) (UL(1) << (n))
#endif

#define GENMASK_32(h, l) \
   ((0xFFFFFFFFU >> (31U - (uint32_t)(h))) & (0xFFFFFFFFU << (uint32_t)(l)))
#define GENMASK_64(h, l) \
   ((~ULL(0) >> (63ULL - (uint64_t)(h))) & (~ULL(0) << (uint64_t)(l)))
#ifndef GENMASK
#define GENMASK GENMASK_32
#endif

#define SZ_128K 0x20000U
#define SZ_1M 0x100000U

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#define DIV_ROUND_UP_2EVAL(n, d) (((n) + (d)-1) / (d))
#define div_round_up(val, div)   DIV_ROUND_UP_2EVAL(val, div)
#define DIV_ROUND_UP(n, d)       DIV_ROUND_UP_2EVAL(n, d)

#define round_up(value, boundary)   ((((value) + (boundary)-1)) & ~((boundary)-1))
#define round_down(value, boundary) ((value) & ~((boundary)-1))

#endif /* UTILS_DEF_H */
