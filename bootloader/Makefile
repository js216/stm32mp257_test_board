# SPDX-License-Identifier: BSD-3-Clause
# Makefile --- STM32MP257 bare-metal bootloader
# Copyright (c) 2026 Jakob Kastelic

CROSS_COMPILE ?= aarch64-none-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy

DIRS  := drivers src
SRC_C := $(wildcard $(addsuffix /*.c,$(DIRS)))
SRC_S := $(wildcard drivers/*.S)
OBJ   := $(patsubst %.c,build/%.o,$(SRC_C)) $(patsubst %.S,build/%.o,$(SRC_S))

CFLAGS := \
	-Idrivers -Isrc \
	-std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wundef \
	-Wmissing-prototypes -Wpointer-arith \
	-Os -g3 -MMD -MP \
	-mcpu=cortex-a35 -mlittle-endian \
	-ffreestanding -fno-common -fno-pic -fno-pie \
	-ffunction-sections -fdata-sections

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
