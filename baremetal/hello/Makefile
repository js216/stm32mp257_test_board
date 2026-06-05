# Makefile -- bare-metal "Hello, world!" for the STM32MP257F-EV1 (Cortex-A35).
#
# Builds a self-contained SYSRAM image (build/main.stm32) that the BootROM loads
# over USB DFU and runs directly -- no TF-A, no DDR, no SD card. It brings up
# USART2 (PA4/AF6, HSI 64 MHz) itself and prints "Hello, world!" forever on the
# ST-LINK VCP at 115200 8N1.
#
# Build host prereq: gcc-aarch64-linux-gnu (override the prefix with CROSS=).
#
# Run it (board in DFU; on this bench `bench_mcu:reset_dut` resets to DFU):
#   dfu.mp257:flash_layout layout=@flash.tsv no_reconnect=true
# Console: mp257.evb-uart1 (primary ST-LINK VCP), 115200 8N1.

CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
HOSTCC  ?= cc

LOAD := 0x0e002600     # SYSRAM load + entry address (matches TF-A BL2 slot)

CFLAGS := -mcpu=cortex-a35 -ffreestanding -nostdlib -fno-pie -fno-common \
          -Os -g -std=c11 -Wall -Wextra
LFLAGS := -nostdlib -no-pie -Wl,--gc-sections -T src/sysram.ld

.PHONY: all clean
all: build/main.stm32

build/main.elf: src/start.S src/main.c src/sysram.ld
	mkdir -p build
	$(CC) $(CFLAGS) $(LFLAGS) src/start.S src/main.c -o $@

build/main.bin: build/main.elf
	$(OBJCOPY) -O binary $< $@

build/stm32image: scripts/stm32image.c
	mkdir -p build
	$(HOSTCC) -O2 -o $@ $<

# Wrap the raw binary in the STM32 V2.2 image header the BootROM expects
# (-b 0x10 = FSBL binary type).
build/main.stm32: build/main.bin build/stm32image
	build/stm32image -s $< -d $@ -l $(LOAD) -e $(LOAD) -v 0 -m 2 -n 2 -b 0x10

clean:
	rm -rf build
