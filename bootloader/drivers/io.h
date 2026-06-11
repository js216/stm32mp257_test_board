// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file io.h
 * @brief Minimal memory-mapped I/O helpers.
 * @copyright 2026 Jakob Kastelic
 *
 * Modelled on the mmio_* accessors of TF-A (include/lib/mmio.h,
 * BSD-3-Clause), trimmed to what this bootloader needs.
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

static inline void mmio_write_32(uintptr_t addr, uint32_t val)
{
   *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read_32(uintptr_t addr)
{
   return *(volatile uint32_t *)addr;
}

static inline void mmio_setbits_32(uintptr_t addr, uint32_t set)
{
   mmio_write_32(addr, mmio_read_32(addr) | set);
}

static inline void mmio_clrbits_32(uintptr_t addr, uint32_t clr)
{
   mmio_write_32(addr, mmio_read_32(addr) & ~clr);
}

static inline void mmio_clrsetbits_32(uintptr_t addr, uint32_t clr, uint32_t set)
{
   mmio_write_32(addr, (mmio_read_32(addr) & ~clr) | set);
}

#endif // IO_H
