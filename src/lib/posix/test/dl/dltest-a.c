/*
 * dltest-a.c -- module d'essai du groupe dl de posixtest (libposix dlopen.c, docs/POSIX.md) :
 * des globales (dont un pointeur : relocation RELATIVE), des variables __thread (.tdata et
 * .tbss), un constructeur, des appels a l'hote (une fonction de posixtest, strtol et errno,
 * strdup : la libc de l'hote).
 *     gcc -specs=posix.specs -fPIC -shared -Wl,-soname,dltest-a.so dltest-a.c -o dltest-a.so
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
#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern int dlt_host_twice (int);		/* (posixtest) */

int dlt_a_counter = 5;
const char *dlt_a_name = "module a";
__thread int dlt_a_tls = 7;
__thread char dlt_a_tbss[100];
static int s_ctor;

__attribute__ ((constructor)) static void a_init (void)
{
	s_ctor = 42;
	dlt_a_counter++;
}

int dlt_a_ctor (void) { return s_ctor; }
int dlt_a_host (int x) { return dlt_host_twice (x) + 1; }
int *dlt_a_tls_addr (void) { return &dlt_a_tls; }
int dlt_a_tls_get (void) { return dlt_a_tls + dlt_a_tbss[99]; }

/* k ajoute n fois : la valeur finale ne depend que du thread */
int dlt_a_tls_bump (int k, int n)
{
	for (int i = 0; i < n; i++)
	{
		dlt_a_tls += k;
		dlt_a_tbss[i % 100]++;
	}
	return dlt_a_tls;
}

int dlt_a_errno (void)
{
	errno = 0;
	strtol ("999999999999999999999999", 0, 10);
	return errno;
}

char *dlt_a_strdup (const char *s) { return strdup (s); }
