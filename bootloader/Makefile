# SPDX-License-Identifier: BSD-3-Clause
# Makefile --- STM32MP257 bare-metal bootloader
# Copyright (c) 2026 Jakob Kastelic

CROSS_COMPILE ?= aarch64-none-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy

DIRS  := drivers src utils drivers/ddr/src drivers/ddr/phyinit drivers/sdmmc drivers/usb
SRC_C := $(wildcard $(addsuffix /*.c,$(DIRS)))
SRC_S := $(wildcard drivers/*.S) drivers/ddr/fw/ddr_fw_blob.S
OBJ   := $(patsubst %.c,build/%.o,$(SRC_C)) $(patsubst %.S,build/%.o,$(SRC_S))

CFLAGS := \
	-Idrivers -Isrc -Iutils \
	-DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL \
	-std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wundef \
	-Wmissing-prototypes -Wpointer-arith \
	-Os -g3 -MMD -MP \
	-mcpu=cortex-a35 -mlittle-endian -mstrict-align -mgeneral-regs-only \
	-ffreestanding -fno-common -fno-pic -fno-pie -fno-stack-protector \
	-ffunction-sections -fdata-sections

# The TF-A DDR driver and PHY-init code is carried verbatim (BSD-3-Clause):
# give it the TF-A include layout via the compat shims and the build-system
# config macros TF-A's platform.mk would set, and relax the warnings our own
# code is held to.
DDR_INC := \
	-Idrivers/ddr/compat -Idrivers/ddr/include \
	-Idrivers/ddr/include/phy -Idrivers \
	-Idrivers/ddr/include/drivers/st -Iutils
DDR_DEFS := \
	-DSTM32MP25=1 -DSTM32MP2X -DSTM32MP21=0 -DSTM32MP_M33_TDCID=0 \
	-DSTM32MP_LPDDR4_TYPE=1 -DSTM32MP_DDR3_TYPE=0 -DSTM32MP_DDR4_TYPE=0 \
	-DSTM32MP_DDR_DUAL_AXI_PORT=1 -DSTM32MP_DDR_FIP_IO_STORAGE=0 \
	-include cdefs.h -include arch_helpers.h
DDR_CFLAGS := \
	$(DDR_INC) $(DDR_DEFS) \
	-DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL \
	-std=gnu99 -Wall -Os -g3 -MMD -MP \
	-mcpu=cortex-a35 -mlittle-endian -mstrict-align -mgeneral-regs-only \
	-ffreestanding -fno-common -fno-pic -fno-pie -fno-stack-protector \
	-ffunction-sections -fdata-sections

build/drivers/ddr/%.o: CFLAGS := $(DDR_CFLAGS)
build/drivers/sdmmc/%.o: CFLAGS := $(DDR_CFLAGS) -Idrivers/sdmmc
build/drivers/usb/%.o: CFLAGS := $(DDR_CFLAGS) -Idrivers/usb

LFLAGS := \
	-nostdlib -no-pie -static \
	-Wl,--gc-sections -Wl,-Map,build/bootloader.map \
	-T bootloader.ld

all: build/bootloader.stm32

build/bootloader.elf: $(OBJ) bootloader.ld
	$(CC) $(LFLAGS) -o $@ $(OBJ)

build/bootloader.bin: build/bootloader.elf
	$(OBJCOPY) -O binary $< $@

build/bootloader.stm32: build/bootloader.bin build/bootloader.elf
	python3 scripts/stm32_header.py \
		-e build/bootloader.elf -b build/bootloader.bin -o $@ -t .RESET

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

build/%.o: %.S
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf build

.PHONY: all clean

-include $(OBJ:.o=.d)
