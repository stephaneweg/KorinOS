/*
 * start.c -- la moitie C de crt0posix (libposix, docs/POSIX.md ; reprise d'Onyx). Dans l'ordre :
 *  1. le pointeur de thread du thread principal (TPIDR_EL0 -> son TCB et son bloc TLS statique,
 *     tls.c) : avant tout ce qui touche au TLS ou a errno ;
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
#include "posix_internal.h"

extern int main (int argc, char **argv, char **envp);
extern char **environ;

extern void (*__init_array_start[]) (void) __attribute__ ((weak));
extern void (*__init_array_end[]) (void) __attribute__ ((weak));
extern char __posix_eh_frame_start[] __attribute__ ((weak));
extern void __register_frame_info (const void *begin, void *object) __attribute__ ((weak));

/* `struct object' de libgcc (unwind-dw2-fde.h) : 6 mots ; un peu plus ne gene pas */
static void *s_ehObject[8];

void __posix_start (const char *line) __attribute__ ((noreturn));
void __posix_start (const char *line)
{
	__posix_tls_init_main ();

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
