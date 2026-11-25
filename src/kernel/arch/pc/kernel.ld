/* Noyau x86-64 de Korin : lié en haut de l'espace d'adresses (-2 Go),
 * chargé n'importe où en mémoire physique par BOOTX64.EFI. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
	. = 0xFFFFFFFF80000000;
	__kernel_start = .;

	.text : ALIGN(4096) {
		*(.text.entry)
		*(.text .text.*)
	}
	. = ALIGN(4096);
	__text_end = .;		/* paging.c : texte en lecture et exécution */
	.rodata : ALIGN(4096) {
		*(.rodata .rodata.*)
	}
	. = ALIGN(4096);
	__rodata_end = .;	/* lecture seule, NX ; au-delà : données en écriture, NX */
	.data : ALIGN(4096) {
		*(.data .data.*)
	}
	.bss : ALIGN(4096) {
		*(.bss .bss.*)
		*(COMMON)
	}
	__kernel_end = .;

	/DISCARD/ : {
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame_hdr)
	}
}
