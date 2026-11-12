/*
 * dltest-l.c -- bibliotheque d'essai liee a un programme a l'edition de liens (dllinktest.c,
 * groupe dl de posixtest ; libposix dlopen.c, __posix_dl_startup) : une variable que le programme
 * recopie (R_AARCH64_COPY), une variable __thread qu'il lit en « initial exec », un constructeur
 * qui doit tourner avant les siens, une fonction dont l'adresse doit etre la meme partout.
 *     gcc -specs=posix.specs -fPIC -shared -Wl,-soname,libdltestl.so dltest-l.c -o libdltestl.so
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
#include <string.h>

int lib_l_counter = 11;
int lib_l_table[4] = { 1, 2, 3, 4 };
__thread int lib_l_tls = 9;
int lib_l_ready;

__attribute__ ((constructor)) static void l_init (void)
{
	lib_l_ready = 1;
	lib_l_counter += 100;			/* (dans la copie du programme : la seule) */
}

int lib_l_fn (int x) { return x * 3; }
void *lib_l_self (void) { return (void *) lib_l_fn; }
int lib_l_bump (void) { return ++lib_l_counter; }
int lib_l_tls_get (void) { return lib_l_tls; }
int *lib_l_tls_addr (void) { return &lib_l_tls; }
unsigned lib_l_len (const char *s) { return (unsigned) strlen (s); }
