/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: TF-A <lib/utils.h> -> tiny zeromem. */
#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void zeromem(void *p, size_t n)
{
   memset(p, 0, n);
}

/* Caches are off (MMU disabled): maintenance is a no-op. */
static inline void flush_dcache_range(uintptr_t p, size_t n)
{
   (void)p;
   (void)n;
}

static inline void inv_dcache_range(uintptr_t p, size_t n)
{
   (void)p;
   (void)n;
}

static inline void clean_dcache_range(uintptr_t p, size_t n)
{
   (void)p;
   (void)n;
}

#endif /* UTILS_H */
