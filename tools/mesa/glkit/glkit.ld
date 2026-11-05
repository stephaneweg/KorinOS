/* glkit.ld -- GlKit : un objet partage lie a 0, mis en kit d'ARMPI (docs/KITS.md) : deux PT_LOAD
 * (code, donnees en lecture seule, relocations et symboles dynamiques ; puis variables), point
 * d'entree = la table d'exportation. Les relocations, les symboles et la table de hachage restent
 * dans le code : glkit.c les lit pour se lier lui-meme dans chaque process. */
ENTRY(kit_table)
PHDRS
{
	code PT_LOAD FLAGS(5);
	vars PT_LOAD FLAGS(6);
	tls PT_TLS;
	dyn PT_DYNAMIC;
}
SECTIONS
{
	. = 0;
	__kit_base = .;
	.text : {
		KEEP(*(.text.kit_table))
		*(.text .text.*)
	} :code
	.plt : { *(.plt) *(.iplt) } :code
	.rodata : { *(.rodata .rodata.*) } :code
	.gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } :code
	.rela.dyn : {
		__rela_dyn_start = .;
		*(.rela.dyn) *(.rela.init) *(.rela.text .rela.text.*) *(.rela.rodata .rela.rodata.*)
		*(.rela.data .rela.data.*) *(.rela.got) *(.rela.bss .rela.bss.*) *(.rela.ifunc)
		__rela_dyn_end = .;
	} :code
	.rela.plt : {
		__rela_plt_start = .;
		*(.rela.plt) *(.rela.iplt)
		__rela_plt_end = .;
	} :code
	.dynsym : { __dynsym_start = .; *(.dynsym) } :code
	.dynstr : { __dynstr_start = .; *(.dynstr) } :code
	.hash : { __hash_start = .; *(.hash) } :code
	. = ALIGN(4096);
	.tdata : { *(.tdata .tdata.*) } :vars :tls
	.tbss : { *(.tbss .tbss.*) } :vars :tls
	.init_array : {
		__init_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
		KEEP(*(.init_array))
		__init_end = .;
	} :vars
	.fini_array : { KEEP(*(.fini_array .fini_array.*)) } :vars
	.data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) } :vars
	.dynamic : { *(.dynamic) } :vars :dyn
	.got : { *(.got) } :vars
	.got.plt : { *(.got.plt) } :vars
	.data : { *(.data .data.*) *(.sdata .sdata.*) } :vars
	.bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } :vars
	/DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) *(.interp) *(.gnu.hash) }
}
