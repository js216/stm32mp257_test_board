ENTRY(_start)
SECTIONS
{
	. = 0x84000000;
	.text : { *(.text._start) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	. += 0x4000;            /* 16 KiB stack */
	_stack_top = .;
}
