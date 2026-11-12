/* bin.ld : les commandes de /bin en C (tools/bin/build_c.sh, docs/BIN-C.md), comme un programme de
 * FASMARM : en-têtes dans le premier segment (lecture, exécution) qui commence à KRT_BASE
 * (0x4000000000 sur le Pi, 0x400000 sur PC), variables dans le second (lecture, écriture), à la même
 * position dans sa page que dans le fichier ; pas de relocations. */
ENTRY(_start)
PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS(5);
	data PT_LOAD FLAGS(6);
}
SECTIONS
{
	. = KRT_BASE + SIZEOF_HEADERS;
	.text : { *(.text._start) *(.text .text.*) } :text
	.rodata : { *(.rodata .rodata.*) } :text
	. = ALIGN(0x1000) + (. & 0xFFF);
	.data : { *(.data .data.*) *(.sdata .sdata.*) } :data
	.bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } :data
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu.attributes) }
}
