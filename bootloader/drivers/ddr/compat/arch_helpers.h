/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: the TF-A <arch_helpers.h> barriers the DDR code uses. */

#ifndef ARCH_HELPERS_H
#define ARCH_HELPERS_H

#ifndef __ASSEMBLER__

#define dsb()  __asm__ volatile("dsb sy" ::: "memory")
#define dmb()  __asm__ volatile("dmb sy" ::: "memory")
#define isb()  __asm__ volatile("isb" ::: "memory")
#define dsbsy() dsb()

typedef unsigned long u_register_t;

#endif /* __ASSEMBLER__ */

#endif /* ARCH_HELPERS_H */
