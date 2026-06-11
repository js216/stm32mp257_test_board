/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * @file ddr_fw_blob.S
 * @brief Embeds the DDR PHY training firmware (lpddr4_pmu_train.bin from
 *        STM32DDRFW-UTIL, vendored by TF-A) into the image.
 */

	.section .rodata.ddr_fw, "a"
	.global ddr_fw_blob
	.align 4
ddr_fw_blob:
	.incbin "drivers/ddr/fw/lpddr4_pmu_train.bin"
	.size ddr_fw_blob, . - ddr_fw_blob
