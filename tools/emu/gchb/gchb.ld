/* gchb.ld -- un DOL : le code a 0x80003100 (ou les jeux commencent), les donnees juste apres */
ENTRY(_start)
SECTIONS
{
	. = 0x80003100;
	.text : { *(.text.start) *(.text .text.*) }
	. = ALIGN(32);
	.data : { *(.rodata .rodata.* .data .data.* .sdata .sdata.* .sdata2 .sdata2.* .got .got2) }
	. = ALIGN(32);
	__bss_start = .;
	.bss : { *(.sbss .sbss.* .bss .bss.* COMMON) }
	. = ALIGN(32);
	__bss_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu.attributes) }
}
