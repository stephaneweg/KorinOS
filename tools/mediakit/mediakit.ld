/* mediakit : un kit d'ARMPI (docs/KITS.md) : lie a 0, deux segments PT_LOAD
 * (code + donnees en lecture seule + relocations, puis variables), point
 * d'entree = la table d'exportation. */
ENTRY(kit_table)
PHDRS
{
	code PT_LOAD FLAGS(5);
	vars PT_LOAD FLAGS(6);
}
SECTIONS
{
	. = 0;
	__kit_base = .;
	.text : {
		KEEP(*(.text.kit_table))
		*(.text .text.*)
		*(.rodata .rodata.*)
	} :code
	.rela.dyn : {
		__rela_start = .;
		*(.rela.dyn) *(.rela.*)
		__rela_end = .;
	} :code
	.dynsym : { *(.dynsym) } :code
	.dynstr : { *(.dynstr) } :code
	.hash : { *(.hash) } :code
	.gnu.hash : { *(.gnu.hash) } :code
	. = ALIGN(4096);
	.data : {
		*(.data.rel.ro .data.rel.ro.*)
		*(.got) *(.got.plt)
		*(.data .data.*)
		*(.sdata .sdata.*)
	} :vars
	.dynamic : { *(.dynamic) } :vars
	.bss : {
		*(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
	} :vars
	/DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) *(.interp) }
}
