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
	/* Both A35 cores can end up here (the boot ROM parks the secondary
	 * core in SYSRAM and loading this image tramples that pen): two cores
	 * then run the FSBL concurrently on one stack, doubling/interleaving
	 * UART output and corrupting I2C transfers. Park all but core 0. */
	mrs	x0, mpidr_el1
	and	x0, x0, #0xffffff	/* Aff2:Aff1:Aff0 */
	cbz	x0, 3f
2:	wfe
	b	2b

3:	/* EL3 setup: untrap FP/SIMD (CPTR_EL3 resets UNKNOWN and GCC freely
	 * emits SIMD; a trap with no vectors is a silent hang) and point
	 * VBAR_EL3 at vectors that at least print '!' before parking. */
	msr	cptr_el3, xzr
	ldr	x0, =el3_vectors
	msr	vbar_el3, x0
	isb

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

	/*
	 * EL3 exception vectors: every entry prints '!' on USART2 (the TDR
	 * write needs no flag polling for a single byte) and parks, so a
	 * crash is visible on the console instead of silently wedging.
	 */
	.section .text.vectors, "ax"
	.align	11
	.global el3_vectors
el3_vectors:
	.rept	16
	.align	7
	b	exc_hang
	.endr

exc_hang:
	ldr	x0, =0x400E0000		/* USART2 */
	mov	w1, #'!'
	str	w1, [x0, #0x28]		/* TDR */
9:	wfe
	b	9b
	.size el3_vectors, . - el3_vectors
