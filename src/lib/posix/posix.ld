/*
 * posix.ld -- edition de liens d'un programme C d'ARMPI sur libposix (docs/POSIX.md ; repris
 * d'onyx-posix.ld d'Onyx).
 *
 * Un ELF64 ET_EXEC comme les programmes en assembleur (src/inc/armpi.inc) : lie a
 * 0x40_0000_0000 (USER_BASE du noyau), un segment PT_LOAD lecture + execution, un lecture +
 * ecriture (aligne sur 64 Ko), et :
 *  - les tables de deroulement gardees (.eh_frame termine par un mot nul, son debut nomme
 *    __posix_eh_frame_start pour start.c qui l'inscrit) et .gcc_except_table : exceptions C++ ;
 *  - un segment PT_TLS (.tdata / .tbss) au debut du segment RW, image et tailles nommees pour
 *    tls.c (__posix_tdata_start, __posix_tdata_size, __posix_tls_size, __posix_tls_align) ;
 *  - le bloc TLS du thread principal reserve a la fin de .bss (__posix_main_tls_area).
 *  - les en-tetes ELF dans le premier segment (__ehdr_start) et .eh_frame_hdr (PT_GNU_EH_FRAME,
 *    --eh-frame-hdr) : ce que lisent dl_iterate_phdr et les derouleurs (dl.c) ;
 *  - un hote de dlopen (gcc -rdynamic, posix.specs) : la table de ses symboles (.dynsym, .dynstr,
 *    .hash, .dynamic, que dl.c trouve par _DYNAMIC) et, dans le bloc TLS de chaque thread, la
 *    reserve des variables __thread des modules (__posix_dl_tls_reserve_abs, pose par la spec).
 *    Relie a des .so : son PLT (.plt, .rela.plt) et les variables recopiees (.dynbss, COPY), que
 *    __posix_dl_startup (dlopen.c) resout au demarrage.
 *    Les symboles du script sont caches (HIDDEN) : exportes, ils recevraient des relocations
 *    dynamiques que personne n'applique.
 * Le chargeur du noyau (proc_spawn_elf) ne lit que les PT_LOAD ; le tas (sbrk) commence apres
 * la fin du dernier.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
ENTRY(_start)

PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS (5);	/* PF_R | PF_X */
	data PT_LOAD FLAGS (6);		/* PF_R | PF_W */
	tls PT_TLS;
	eh PT_GNU_EH_FRAME;
}

SECTIONS
{
	. = 0x4000000000 + SIZEOF_HEADERS;	/* USER_BASE du noyau d'ARMPI */

	.text : {
		*(.text._start)
		*(.text.unlikely .text.unlikely.*)
		*(.text .text.*)
	} :text
	.plt : { *(.plt) } :text
	.rodata : {
		*(.rodata .rodata.*)
		. = ALIGN(8);
		HIDDEN(__init_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
		KEEP(*(.init_array .ctors))
		HIDDEN(__init_array_end = .);
		HIDDEN(__fini_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
		KEEP(*(.fini_array .dtors))
		HIDDEN(__fini_array_end = .);
	} :text
	.gcc_except_table : {
		*(.gcc_except_table .gcc_except_table.*)
	} :text
	.eh_frame_hdr : { *(.eh_frame_hdr) } :text :eh
	.hash : { *(.hash) } :text
	.gnu.hash : { *(.gnu.hash) } :text
	.dynsym : { *(.dynsym) } :text
	.dynstr : { *(.dynstr) } :text
	.rela.dyn : { *(.rela.dyn .rela.data .rela.data.* .rela.got .rela.bss .rela.bss.*) } :text
	.rela.plt : { *(.rela.plt) } :text
	.eh_frame : {
		HIDDEN(__posix_eh_frame_start = .);
		KEEP(*(.eh_frame))
		LONG(0)			/* the terminator crtend.o would add */
	} :text

	. = ALIGN(0x10000);

	.tdata : {
		HIDDEN(__posix_tdata_start = .);
		*(.tdata .tdata.* .gnu.linkonce.td.*)
		HIDDEN(__posix_tdata_end = .);
	} :data :tls
	.tbss : {
		HIDDEN(__posix_tbss_start = .);
		*(.tbss .tbss.* .gnu.linkonce.tb.*)
		*(.tcommon)
		HIDDEN(__posix_tbss_end = .);
	} :data :tls
	HIDDEN(__posix_tdata_size = __posix_tdata_end - __posix_tdata_start);
	HIDDEN(__posix_tls_size = __posix_tbss_end - __posix_tdata_start);
	HIDDEN(__posix_tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)));
	HIDDEN(__posix_dl_tls_reserve = DEFINED(__posix_dl_tls_reserve_abs) ? __posix_dl_tls_reserve_abs : 0);
	HIDDEN(__posix_main_tls_reserve = __posix_tls_size + __posix_dl_tls_reserve > 0
		? 8192 + __posix_tls_size + __posix_dl_tls_reserve + MAX(__posix_tls_align, 64) : 0);

	.data : {
		*(.data .data.*)
		*(.data.rel.ro .data.rel.ro.*)
		*(.got) *(.got.plt)
	} :data
	.dynamic : { *(.dynamic) } :data
	.bss : {
		*(.dynbss)
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(64);
		HIDDEN(__posix_main_tls_area = .);
		. += __posix_main_tls_reserve;
	} :data

	/DISCARD/ : { *(.comment) *(.note*) }
}
