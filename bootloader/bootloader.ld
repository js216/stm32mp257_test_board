/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * @file bootloader.ld
 * @brief Linker script: run from SYSRAM as the STM32MP25 FSBL.
 * @copyright 2026 Jakob Kastelic
 *
 * The boot ROM loads the FSBL at SYSRAM + 0x2600 (the same load address
 * TF-A's BL2 image uses; the lower 0x2600 holds the ROM boot-param area).
 * SYSRAM ends at 0x0E040000.
 */

ENTRY(_start)

MEMORY {
	SYSRAM (rwx) : ORIGIN = 0x0E002600, LENGTH = 0x0003D000
}

SECTIONS {
	. = ORIGIN(SYSRAM);

	.RESET : {
		KEEP(*(.RESET))
	} > SYSRAM

	.text : {
		*(.text*)
		*(.rodata*)
	} > SYSRAM

	.data : {
		*(.data*)
	} > SYSRAM

	.bss : {
		. = ALIGN(8);
		__bss_start = .;
		*(.bss*)
		*(COMMON)
		. = ALIGN(8);
		__bss_end = .;
	} > SYSRAM

	. = ALIGN(16);
	. = . + 0x4000;		/* 16 KB stack */
	__stack_top = .;

	/DISCARD/ : {
		*(.note*)
		*(.comment)
		*(.ARM.attributes)
	}
}
