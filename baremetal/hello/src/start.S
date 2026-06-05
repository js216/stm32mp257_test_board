/* start.S -- A35 entry for a ROM-DFU-loaded SYSRAM image.
 * The STM32MP257 has two Cortex-A35 cores and the BootROM releases both, so
 * park every secondary core (MPIDR affinity != 0) and let only the primary
 * run main(). */
	.section .text._start, "ax"
	.global _start
_start:
	mrs	x0, mpidr_el1
	and	x0, x0, #0xffff		/* Aff1:Aff0 (0 only on the primary core) */
	cbnz	x0, park

	ldr	x0, =_stack_top
	mov	sp, x0
	bl	main
hang:
	b	hang

park:
	wfe
	b	park
