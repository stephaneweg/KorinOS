/* Kit en C : ELF64 lié à 0 (docs/KITS.md) ; l'entrée est la table
 * d'exportation, au début du code ; les variables sur leurs propres pages
 * (le noyau les recopie dans chaque process). Commun à AArch64 et x86-64. */
ENTRY(kit_table)

PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS(5); /* en-têtes compris, à 0, comme FASMARM */
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = SIZEOF_HEADERS;
	.text : {
		*(.text.kit_table)
		*(.text .text.*)
	} :text
	.rodata : { *(.rodata .rodata.*) } :text
	. = ALIGN(4096);
	.data : { *(.data .data.*) } :data
	.bss : { *(.bss .bss.*) *(COMMON) } :data
	/* un kit n'a pas de relocations : un pointeur rangé dans les données
	 * casserait la construction (tools/kits/build_c.sh le vérifie) */
	/DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) *(.interp) *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) }
}
