/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: TF-A <lib/mmio.h> -> this bootloader's io.h. */

#ifndef MMIO_H
#define MMIO_H

#include "io.h"

static inline void mmio_write_64(uintptr_t addr, uint64_t val)
{
   *(volatile uint64_t *)addr = val;
}

static inline uint64_t mmio_read_64(uintptr_t addr)
{
   return *(volatile uint64_t *)addr;
}

static inline void mmio_write_16(uintptr_t addr, uint16_t val)
{
   *(volatile uint16_t *)addr = val;
}

static inline uint16_t mmio_read_16(uintptr_t addr)
{
   return *(volatile uint16_t *)addr;
}

static inline void mmio_write_8(uintptr_t addr, uint8_t val)
{
   *(volatile uint8_t *)addr = val;
}

static inline uint8_t mmio_read_8(uintptr_t addr)
{
   return *(volatile uint8_t *)addr;
}

#endif /* MMIO_H */
