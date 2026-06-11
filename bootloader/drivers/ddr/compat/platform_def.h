/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Compat shim: the TF-A <platform_def.h>/<stm32mp2_def.h> definitions the
 * carried DDR code needs (values from TF-A plat/st/stm32mp2/stm32mp2_def.h).
 */

#ifndef PLATFORM_DEF_H
#define PLATFORM_DEF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/utils_def.h"
#include "stm32mp25_rcc.h"
#include "stm32mp2_pwr.h"

enum ddr_type {
   STM32MP_DDR3,
   STM32MP_DDR4,
   STM32MP_LPDDR4
};

#define CACHE_WRITEBACK_GRANULE 64U

/* One reserved (cache-coherent; caches are off) SYSRAM page for the DWC3
 * event buffer/TRBs/setup area -- the top page, well above image + stack. */
#define STM32MP_USB_DWC3_BASE U(0x0E03F000)

#define USB_DWC3_BASE_ADDR U(0x48300000) /* the controller itself */

#define DDRCTRL_BASE U(0x48040000)
#define DDRDBG_BASE  U(0x48050000)
#define DDRPHYC_BASE U(0x48C00000)

#define STM32MP_DDR_BASE     U(0x80000000)
#define STM32MP_DDR_MAX_SIZE UL(0x100000000)

/* Retention RAM (phyinit tracks trained registers in a save area there). */
#define RETRAM_BASE U(0x0E080000)
#define RETRAM_SIZE U(0x00020000)

/*
 * The PHY training firmware (lpddr4_pmu_train.bin) is linked into this
 * image; the symbol marks its start. Section offsets match the blob layout
 * TF-A uses (DMEM at +0x400, IMEM at +0x800).
 */
extern const uint8_t ddr_fw_blob[];
#define STM32MP_DDR_FW_BASE        ((uintptr_t)ddr_fw_blob)
#define STM32MP_DDR_FW_DMEM_OFFSET U(0x400)
#define STM32MP_DDR_FW_IMEM_OFFSET U(0x800)

/* Base-address helpers (TF-A stm32mp_common.h); defined in drivers/ddr.c. */
uintptr_t stm32mp_rcc_base(void);
uintptr_t stm32mp_pwr_base(void);
uintptr_t stm32mp_ddrctrl_base(void);
uintptr_t stm32mp_ddrphyc_base(void);
uintptr_t stm32_ddrdbg_get_base(void);

#endif /* PLATFORM_DEF_H */
