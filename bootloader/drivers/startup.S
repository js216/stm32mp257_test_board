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
 *
 * Step 7 additions: the secondary core parks in a PSCI holding pen (released
 * by CPU_ON), the lower-EL sync vector dispatches SMCs to psci_smc(), and
 * drop_to_el2() enters the non-secure world for the Linux hand-off.
 */

#define CNTFRQ_HZ       64000000
#define SCTLR_EL2_RES1  0x30C50830
#define SCR_NS_EL2      0x501		/* NS | HCE | RW */
#define SPSR_EL2H_DAIF  0x3C9		/* EL2h, DAIF masked */
#define GICD_IGROUPR0   0x4AC10080
#define GICC_BASE       0x4AC20000
#define GICC_CTLR_SEC   0x1E9
#define SMC_STACK_SHIFT 10		/* 1 KB per-core SMC stack */

	.section .RESET, "ax"
	.global _start
	.type _start, %function
_start:
	/* Both A35 cores can end up here (the boot ROM parks the secondary
	 * core in SYSRAM and loading this image tramples that pen): two cores
	 * then run the FSBL concurrently on one stack, doubling/interleaving
	 * UART output and corrupting I2C transfers. Park all but core 0 in
	 * the PSCI holding pen. */
	/* Coherency on FIRST (TF-A does this in the reset handler): if
	 * SMPEN is set any later, the core can treat shareable-cacheable
	 * memory as non-cacheable once caches are enabled. */
	mrs	x0, S3_1_C15_C2_1	/* CPUECTLR_EL1 */
	orr	x0, x0, #(1 << 6)	/* SMPEN */
	msr	S3_1_C15_C2_1, x0
	isb

	mrs	x0, mpidr_el1
	and	x0, x0, #0xffffff	/* Aff2:Aff1:Aff0 */
	cbz	x0, 3f

	/* Secondary core: minimal EL3 state, then wait for PSCI CPU_ON.
	 * The mailbox lives in .data (part of the loaded image, reset to 0)
	 * because the pen runs before core 0 has zeroed the BSS. */
	msr	cptr_el3, xzr
	ldr	x0, =el3_vectors
	msr	vbar_el3, x0
	isb
	.global psci_pen_wait
psci_pen_wait:
	wfe
	ldr	x0, =psci_pen_entry
	ldr	x1, [x0]
	cbz	x1, psci_pen_wait

	/* Released: x1 = NS entry. Bring the banked GIC CPU-interface state
	 * to the same hand-off configuration gic_cpuif_init() set on core 0,
	 * then enter the kernel with x0 = context. */
	ldr	x9, =GICD_IGROUPR0
	mov	w10, #-1
	str	w10, [x9]		/* SGIs/PPIs all Group1 (NS) */
	ldr	x9, =GICC_BASE
	mov	w10, #0xFF
	str	w10, [x9, #4]		/* PMR: permissive */
	mov	w10, #GICC_CTLR_SEC
	str	w10, [x9]
	ldr	x0, =psci_pen_ctx
	ldr	x0, [x0]
	b	drop_to_el2

3:	/* EL3 setup: untrap FP/SIMD (CPTR_EL3 resets UNKNOWN and GCC freely
	 * emits SIMD; a trap with no vectors is a silent hang) and point
	 * VBAR_EL3 at the vectors (SMC dispatch + crash visibility). */
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
	 * Enter the non-secure world at EL2: x0 = payload x0 (dtb for Linux),
	 * x1 = entry. Per-core: SMPEN (coherency, required before the kernel
	 * enables caches) and CNTFRQ (per-core register; the ROM leaves it 0
	 * and timer_init() only fixed core 0).
	 */
	.global drop_to_el2
	.type drop_to_el2, %function
drop_to_el2:
	mrs	x2, S3_1_C15_C2_1	/* CPUECTLR_EL1 */
	orr	x2, x2, #(1 << 6)	/* SMPEN */
	msr	S3_1_C15_C2_1, x2
	isb
	ldr	x2, =CNTFRQ_HZ
	msr	cntfrq_el0, x2
	ldr	x2, =SCTLR_EL2_RES1	/* MMU/caches off at EL2 */
	msr	sctlr_el2, x2
	mov	x2, #SCR_NS_EL2
	msr	scr_el3, x2
	mov	x2, #SPSR_EL2H_DAIF
	msr	spsr_el3, x2
	msr	elr_el3, x1
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	isb
	eret
	.size drop_to_el2, . - drop_to_el2

	/* PSCI CPU_OFF: clear the mailbox and park back in the pen. */
	.global psci_pen_return
	.type psci_pen_return, %function
psci_pen_return:
	ldr	x0, =psci_pen_entry
	str	xzr, [x0]
	dsb	sy
	b	psci_pen_wait
	.size psci_pen_return, . - psci_pen_return

	/*
	 * EL3 exception vectors. The lower-EL AArch64 synchronous entry
	 * dispatches SMCs to psci_smc() on a small per-core stack; every
	 * other entry prints '!' on USART2 (the TDR write needs no flag
	 * polling for a single byte) and parks, so a crash is visible on
	 * the console instead of silently wedging.
	 */
	.section .text.vectors, "ax"
	.align	11
	.global el3_vectors
el3_vectors:
	.rept	8			/* current EL entries */
	.align	7
	b	exc_hang
	.endr
	.align	7
	b	smc_entry		/* lower EL, AArch64, synchronous */
	.rept	7
	.align	7
	b	exc_hang
	.endr

smc_entry:
	/* Anything that is not an SMC64 is fatal. */
	mrs	x9, esr_el3
	lsr	x9, x9, #26
	cmp	x9, #0x17
	b.ne	exc_hang

	/* Fresh per-core SMC stack each entry (no nesting at EL3: DAIF
	 * stays masked). SMCCC: x4-x17 may be clobbered, x18+ must survive
	 * the C call -- the AAPCS only guarantees x19+, so save x18. */
	mrs	x9, mpidr_el1
	and	x9, x9, #0xff
	add	x9, x9, #1
	ldr	x10, =smc_stacks
	add	x10, x10, x9, lsl #SMC_STACK_SHIFT
	mov	sp, x10
	stp	x18, x30, [sp, #-16]!
	bl	psci_smc
	ldp	x18, x30, [sp], #16
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	eret

exc_hang:
	ldr	x0, =0x400E0000		/* USART2 */
	mov	w1, #'!'
	str	w1, [x0, #0x28]		/* TDR */
9:	wfe
	b	9b
	.size el3_vectors, . - el3_vectors

	/* PSCI holding-pen mailbox: must be in .data (loaded as zero), the
	 * secondary core polls it before the BSS is cleared. */
	.section .data
	.align	3
	.global psci_pen_entry
psci_pen_entry:
	.quad	0
	.global psci_pen_ctx
psci_pen_ctx:
	.quad	0

	.section .bss
	.align	4
smc_stacks:
	.skip	(2 << SMC_STACK_SHIFT)
