# Cross-compile the EVB GPU demos (cube, fft) for aarch64 / OpenSTLinux.
#
# Prereqs on the build host (Debian):
#   sudo apt-get install gcc-aarch64-linux-gnu \
#        libegl-dev libgles-dev libdrm-dev libgbm-dev libfftw3-dev
#
# build/sysroot holds the board's own aarch64 .so files (libEGL libGLESv2
# libgbm libdrm libfftw3f), pulled from the target so DT_NEEDED SONAMEs match.
# Headers come from the host -dev packages via -idirafter, so the cross libc
# headers still win.

CROSS    ?= aarch64-linux-gnu-
CC       := $(CROSS)gcc

BUILD    := build
SYSROOT  := $(BUILD)/sysroot

CFLAGS   := -O2 -Wall -Wextra
CPPFLAGS := -idirafter /usr/include -idirafter /usr/include/libdrm
LDFLAGS  := -L$(SYSROOT) \
            -Wl,--allow-shlib-undefined \
            -Wl,--unresolved-symbols=ignore-in-shared-libs
LDLIBS   := -lEGL -lGLESv2 -lgbm -ldrm -lm

BINS     := $(BUILD)/cube $(BUILD)/fft

.PHONY: all clean
all: $(BINS)

$(BUILD)/fft: LDLIBS += -lfftw3f

$(BUILD)/%: %.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD):
	mkdir -p $@

clean:
	$(RM) $(BINS)
