/*
 * exitproc.c -- la table des fonctions de sortie (atexit, on_exit, __cxa_atexit), sans limite.
 *
 * newlib range ces fonctions dans une table fixe de 32 places (__atexit.c) : sa croissance par malloc
 * n'est pas compilee pour notre cible, la 33e inscription rend -1. Or les destructeurs des objets
 * statiques de C++ y vont aussi (__cxa_atexit), au fil du code qui tourne : un gros programme (le
 * process web de Jet sur une page comme Facebook) remplit la table, puis OpenSSL, dont la premiere
 * initialisation fait atexit (OPENSSL_cleanup), refuse toute recherche d'algorithme jusqu'a la fin
 * du process (docs/POSIX.md, « Fonctions de sortie »).
 *
 * Ici les deux fonctions par lesquelles passe newlib : __register_exitproc (atexit, on_exit,
 * __cxa_atexit l'appellent) et __call_exitprocs (exit, et __cxa_finalize pour un module que dlclose
 * decharge). La spec les tire de libposix avant newlib (-u, tools/posix/build.sh) : les objets
 * __atexit.o et __call_atexit.o de newlib ne sont plus lies. 32 places d'abord, sans allocation
 * (un programme peut s'inscrire avant que malloc serve), puis un tableau qui double.
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
#include <string.h>
#include "posix_internal.h"

/* les sortes de newlib (sys/reent.h : __et_atexit, __et_onexit, __et_cxa) */
#define EXIT_ATEXIT	0	/* fn () */
#define EXIT_ONEXIT	1	/* fn (code, arg) */
#define EXIT_CXA	2	/* fn (arg), sous le __dso_handle d'un module */

struct exitproc
{
	void (*fn) (void);
	void *arg;
	void *dso;
	int type;
};

static struct exitproc s_first[32];
static struct exitproc *s_list = s_first;
static unsigned s_cap = 32, s_n;
static volatile unsigned s_lock;		/* une feuille : rien d'autre n'est pris sous lui (ni malloc) */

void __posix_exitproc_fork (int phase) { __posix_fork_lock (&s_lock, phase); }

int __register_exitproc (int type, void (*fn) (void), void *arg, void *dso)
{
	for (;;)
	{
		__posix_lock (&s_lock);
		if (s_n < s_cap)
		{
			s_list[s_n].fn = fn;
			s_list[s_n].arg = arg;
			s_list[s_n].dso = dso;
			s_list[s_n].type = type;
			s_n++;
			__posix_unlock (&s_lock);
			return 0;
		}
		/* plein : un tableau deux fois plus grand, alloue hors du verrou ; si un autre thread a
		   grandi la table entre-temps, on rend le sien et on recommence */
		unsigned cap = s_cap;
		__posix_unlock (&s_lock);
		struct exitproc *n = malloc (2 * cap * sizeof *n);
		if (n == 0)
			return -1;
		struct exitproc *old = 0;
		__posix_lock (&s_lock);
		if (s_cap == cap)
		{
			memcpy (n, s_list, s_n * sizeof *n);
			if (s_list != s_first)
				old = s_list;
			s_list = n;
			s_cap = 2 * cap;
			n = 0;
		}
		__posix_unlock (&s_lock);
		free (n);
		free (old);
	}
}

/* Les fonctions inscrites, de la derniere a la premiere ; dso non nul : seulement celles de
   __cxa_atexit sous ce module (__cxa_finalize). Chacune est effacee avant d'etre appelee (une
   seule fois) et appelee hors du verrou ; si elle en inscrit d'autres, on repart du haut. */
void __call_exitprocs (int code, void *dso)
{
	__posix_lock (&s_lock);
	for (unsigned i = s_n; i-- > 0;)
	{
		struct exitproc e = s_list[i];
		if (e.fn == 0 || (dso != 0 && (e.type != EXIT_CXA || e.dso != dso)))
			continue;
		s_list[i].fn = 0;
		unsigned n = s_n;
		__posix_unlock (&s_lock);
		if (e.type == EXIT_ONEXIT)
			((void (*) (int, void *)) e.fn) (code, e.arg);
		else if (e.type == EXIT_CXA)
			((void (*) (void *)) e.fn) (e.arg);
		else
			e.fn ();
		__posix_lock (&s_lock);
		if (s_n != n)
			i = s_n;		/* de nouvelles inscriptions : on repart du haut */
	}
	/* les places videes (un module decharge laisse des trous) : la table se resserre, dans l'ordre */
	unsigned k = 0;
	for (unsigned i = 0; i < s_n; i++)
		if (s_list[i].fn != 0)
			s_list[k++] = s_list[i];
	s_n = k;
	__posix_unlock (&s_lock);
}

/* Comme __call_atexit.c de newlib (--enable-newlib-register-fini) : les destructeurs de .fini_array
   (__libc_fini_array) passent par la table, inscrits avant tout le reste, donc appeles en dernier. */
extern void __libc_fini_array (void);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"
__attribute__ ((constructor (0))) static void register_fini (void)
{
	__register_exitproc (EXIT_ATEXIT, __libc_fini_array, 0, 0);
}
#pragma GCC diagnostic pop
