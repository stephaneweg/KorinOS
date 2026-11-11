/*
 * start.c -- la moitie C de crt0posix (libposix, docs/POSIX.md ; reprise d'Onyx). Dans l'ordre :
 *  1. le pointeur de thread du thread principal (TPIDR_EL0 -> son TCB et son bloc TLS statique,
 *     tls.c) : avant tout ce qui touche au TLS ou a errno ;
 *  1 bis. un hote de dlopen (gcc -rdynamic, dlopen.c) : ses propres relocations dynamiques. ld en
 *     laisse quelques-unes vers les symboles exportes qu'il lit par le GOT (pthread_cancel, que
 *     libstdc++ teste en reference faible...) : personne d'autre ne les ferait ;
 *  2. les tables de deroulement (.eh_frame, gardees par posix.ld) inscrites aupres de libgcc,
 *     si le programme en a (exceptions C++) : reference faible, un programme C n'attire rien ;
 *  3. les descripteurs 0, 1, 2 (entree, sortie, erreur standard d'AppKit) ;
 *  4. l'environnement (valeurs par defaut, TZ) et argc / argv (la ligne d'arguments donnee par
 *     le noyau, argv[0] le nom du process) ;
 *  5. les constructeurs (.init_array, dans l'ordre des priorites : posix.ld les trie) ;
 *  6. exit (main (argc, argv, environ)) : atexit, stdio vide, _exit -> SYS_EXIT.
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
#include <stdlib.h>
#include <elf.h>
#include "posix_internal.h"

extern int main (int argc, char **argv, char **envp);
extern char **environ;

extern void (*__init_array_start[]) (void) __attribute__ ((weak));
extern void (*__init_array_end[]) (void) __attribute__ ((weak));
extern char __posix_eh_frame_start[] __attribute__ ((weak));
extern void __register_frame_info (const void *begin, void *object) __attribute__ ((weak));

extern Elf64_Dyn _DYNAMIC[] __attribute__ ((weak));

static void self_relocate (void)
{
	const Elf64_Rela *rela = 0;
	const Elf64_Sym *sym = 0;
	unsigned long n = 0;
	for (const Elf64_Dyn *d = _DYNAMIC; d->d_tag != DT_NULL; d++)
		if (d->d_tag == DT_RELA)
			rela = (const Elf64_Rela *) d->d_un.d_ptr;
		else if (d->d_tag == DT_RELASZ)
			n = d->d_un.d_val / sizeof (Elf64_Rela);
		else if (d->d_tag == DT_SYMTAB)
			sym = (const Elf64_Sym *) d->d_un.d_ptr;
	if (rela == 0 || sym == 0)
		return;
	for (unsigned long i = 0; i < n; i++)
	{
		unsigned t = ELF64_R_TYPE (rela[i].r_info);
		const Elf64_Sym *s = &sym[ELF64_R_SYM (rela[i].r_info)];
		if ((t == R_AARCH64_GLOB_DAT || t == R_AARCH64_ABS64 || t == R_AARCH64_JUMP_SLOT)
		    && s->st_shndx != SHN_UNDEF)
			*(unsigned long *) rela[i].r_offset = s->st_value + rela[i].r_addend;
	}
}

/* `struct object' de libgcc (unwind-dw2-fde.h) : 6 mots ; un peu plus ne gene pas */
static void *s_ehObject[8];

void __posix_start (const char *line) __attribute__ ((noreturn));
void __posix_start (const char *line)
{
	__posix_tls_init_main ();
	if (_DYNAMIC != 0)
		self_relocate ();

	if (__register_frame_info != 0 && __posix_eh_frame_start != 0)
		__register_frame_info (__posix_eh_frame_start, s_ehObject);

	__posix_fd_init ();
	__posix_env_init ();
	char **argv;
	int argc = __posix_args_init (line, &argv);

	if (__init_array_start != 0)
		for (void (**f) (void) = __init_array_start; f < __init_array_end; f++)
			(*f) ();

	exit (main (argc, argv, environ));
}
