/*
 * dllinktest.c -- un programme relie a une bibliotheque partagee (libdltestl.so, dltest-l.c) :
 * le chargement de ses DT_NEEDED au demarrage (libposix dlopen.c, __posix_dl_startup). Lance par
 * le groupe dl de posixtest ; code de sortie 0 : tout passe.
 *     gcc -specs=posix.specs -rdynamic -tls-reserve=0x10000 dllinktest.c -L. -ldltestl \
 *         -Wl,-rpath,'$ORIGIN' -o dllinktest
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
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

extern int lib_l_counter, lib_l_table[4], lib_l_ready;
extern __thread int lib_l_tls;
int lib_l_fn (int);
void *lib_l_self (void);
int lib_l_bump (void);
int lib_l_tls_get (void);
int *lib_l_tls_addr (void);
unsigned lib_l_len (const char *);
extern unsigned long __posix_dl_shared_bytes;	/* (libposix) */

static int s_fail, s_seenReady;

static void check (const char *name, int ok)
{
	printf ("%s  link: %s\n", ok ? "PASS" : "FAIL", name);
	s_fail += !ok;
}

__attribute__ ((constructor)) static void prog_init (void)
{
	s_seenReady = lib_l_ready;		/* la bibliotheque d'abord */
}

static void *worker (void *p)
{
	int k = (int) (long) p;
	for (int i = 0; i < 1000; i++)
		lib_l_tls += k;
	return (void *) (long) (lib_l_tls == 9 + 1000 * k && lib_l_tls_get () == lib_l_tls
				&& lib_l_tls_addr () == &lib_l_tls);
}

int main (void)
{
	setvbuf (stdout, 0, _IOLBF, 0);
	check ("the library's constructor ran before the program's", s_seenReady == 1);
	check ("a call through the PLT (JUMP_SLOT)", lib_l_fn (5) == 15);
	check ("a variable of the library, copied (COPY), with its constructor's change", lib_l_counter == 111
	       && lib_l_table[3] == 4);
	check ("one copy: the library writes where the program reads", lib_l_bump () == 112 && lib_l_counter == 112);
	check ("a function's address is the same in the program and the library", lib_l_self () == (void *) lib_l_fn);
	check ("the library uses the program's libc", lib_l_len ("korin") == 5);
	check ("__thread of the library read by the program (initial exec)", lib_l_tls == 9 && lib_l_tls_addr () == &lib_l_tls);
	pthread_t th[4];
	int ok = 1;
	for (long i = 0; i < 4; i++)
		ok &= pthread_create (&th[i], 0, worker, (void *) (i + 1)) == 0;
	for (int i = 0; i < 4; i++)
	{
		void *r = 0;
		pthread_join (th[i], &r);
		ok &= r != 0;
	}
	check ("__thread of the library on 4 threads", ok && lib_l_tls == 9);
	check ("dlsym (RTLD_DEFAULT) finds the library (global scope)", dlsym (RTLD_DEFAULT, "lib_l_bump") != 0);
	Dl_info di;
	/* (l'adresse que le programme prend est celle de son PLT : dladdr y dirait le programme) */
	void *real = dlsym (RTLD_DEFAULT, "lib_l_tls_get");
	check ("dladdr: the library's file and symbol", real && dladdr (real, &di) && di.dli_fname
	       && strstr (di.dli_fname, "libdltestl.so") != 0 && di.dli_sname && !strcmp (di.dli_sname, "lib_l_tls_get"));
	check ("the library's code is projected from its file (shared pages)", __posix_dl_shared_bytes > 0);
	printf ("dllinktest: %d failed\n", s_fail);
	return s_fail;
}
