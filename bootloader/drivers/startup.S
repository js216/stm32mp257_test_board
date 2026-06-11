/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * @file startup.S
 * @brief AArch64 reset entry for the STM32MP25 FSBL.
 * @copyright 2026 Jakob Kastelic
 *
 * The boot ROM loads this image into SYSRAM and jumps here in EL3 with the
 * MMU and caches disabled. We set up a stack, zero the BSS, and call main().
 * Reset-flow style follows TF-A's el3 entrypoints (BSD-3-Clause), reduced to
 * the bare minimum needed before any C runs.
 */

	.section .RESET, "ax"
	.global _start
	.type _start, %function
_start:
	/* Set up the C stack (16-byte aligned, from the linker script). */
	ldr	x0, =__stack_top
	mov	sp, x0

	/* Zero the BSS: for (p = __bss_start; p < __bss_end; p += 8) *p = 0. */
	ldr	x0, =__bss_start
	ldr	x1, =__bss_end
0:	cmp	x0, x1
	b.hs	1f
	str	xzr, [x0], #8
	b	0b

1:	bl	main

	/* main() should never return; park the core if it does. */
2:	wfe
	b	2b
	.size _start, . - _start
