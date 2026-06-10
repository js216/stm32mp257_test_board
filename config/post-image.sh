#!/usr/bin/env bash
set -euo pipefail

st_common="${BR2_EXTERNAL_ST_PATH}/board/stmicroelectronics/common"

"${st_common}/generate-sdcard.sh" "$@"
"${st_common}/generate-flashlayout.sh" "$@"

tfa_dir="${BUILD_DIR}/arm-trusted-firmware-custom"
usb_build_dir="${BUILD_DIR}/arm-trusted-firmware-usb"
cross_compile="${HOST_DIR}/bin/aarch64-none-linux-gnu-"

make -C "${tfa_dir}" -j"${PARALLEL_JOBS:-1}" \
	CROSS_COMPILE="${cross_compile}" \
	BUILD_STRING=custom \
	BUILD_BASE="${usb_build_dir}" \
	STM32MP_LPDDR4_TYPE=1 \
	DTB_FILE_NAME=stm32mp257f-dk.dtb \
	STM32MP_USB_PROGRAMMER=1 \
	BL32_EXTRA2= \
	BL33_CFG="${BINARIES_DIR}/u-boot.dtb" \
	PLAT=stm32mp2 \
	TARGET_BOARD= \
	HOSTCC="${HOSTCC:-gcc}" \
	ARM_ARCH_MAJOR=8 \
	ARCH=aarch64 \
	BL32="${BINARIES_DIR}/tee-header_v2.bin" \
	BL32_EXTRA1="${BINARIES_DIR}/tee-pager_v2.bin" \
	BL32_EXTRA2="${BINARIES_DIR}/tee-pageable_v2.bin" \
	SPD=opteed \
	BL33="${BINARIES_DIR}/u-boot-nodtb.bin" \
	all fip

cp -f "${usb_build_dir}/stm32mp2/release/tf-a-stm32mp257f-dk.stm32" \
	"${BINARIES_DIR}/tf-a-stm32mp257_dk_usb.stm32"
cp -f "${usb_build_dir}/stm32mp2/release/fip.bin" \
	"${BINARIES_DIR}/fip-stm32mp257_dk_usb.bin"
cp -f "${usb_build_dir}/stm32mp2/release/fip-ddr.bin" \
	"${BINARIES_DIR}/fip-ddr-stm32mp257_dk_usb.bin"

# ---------------------------------------------------------------------------
# Direct TF-A -> Linux boot FIP (no OP-TEE, no U-Boot).
#
# BL2 -> BL31 -> Linux as BL33 directly. No BL32/SPD, no U-Boot; the kernel
# Image is the BL33 payload and the kernel DTB is its HW_CONFIG. Loaded the
# same way as the USB programmer FIP (BL2 pulls the FIP over USB-DFU), but the
# resulting board runs with neither OP-TEE nor U-Boot in the boot chain.
# Flash with config/mp257-direct.tsv.
# ---------------------------------------------------------------------------
direct_build_dir="${BUILD_DIR}/arm-trusted-firmware-direct"
linux_image="${BUILD_DIR}/linux-custom/arch/arm64/boot/Image"

make -C "${tfa_dir}" -j"${PARALLEL_JOBS:-1}" \
	CROSS_COMPILE="${cross_compile}" \
	BUILD_STRING=direct \
	BUILD_BASE="${direct_build_dir}" \
	LOG_LEVEL=40 \
	STM32MP_LPDDR4_TYPE=1 \
	DTB_FILE_NAME=stm32mp257f-dk.dtb \
	STM32MP_USB_PROGRAMMER=1 \
	PLAT=stm32mp2 \
	TARGET_BOARD= \
	HOSTCC="${HOSTCC:-gcc}" \
	ARM_ARCH_MAJOR=8 \
	ARCH=aarch64 \
	BL33="${linux_image}" \
	BL33_CFG="${BINARIES_DIR}/stm32mp257f-dk.dtb" \
	all fip

cp -f "${direct_build_dir}/stm32mp2/release/tf-a-stm32mp257f-dk.stm32" \
	"${BINARIES_DIR}/tf-a-stm32mp257_dk_direct.stm32"
cp -f "${direct_build_dir}/stm32mp2/release/fip.bin" \
	"${BINARIES_DIR}/fip-stm32mp257_dk_direct.bin"
cp -f "${direct_build_dir}/stm32mp2/release/fip-ddr.bin" \
	"${BINARIES_DIR}/fip-ddr-stm32mp257_dk_direct.bin"
