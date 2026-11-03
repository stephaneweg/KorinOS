/*
 * dl.c -- ce qu'un programme statique peut dire de lui-meme (libposix, docs/POSIX.md) :
 * dl_iterate_phdr et dladdr, sur les en-tetes ELF que posix.ld garde dans le premier segment, et
 * le chemin de l'executable (/proc/self/exe). Pas de dlopen (misc.c).
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
#include <link.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "posix_internal.h"

extern const Elf64_Ehdr __ehdr_start __attribute__ ((weak));	/* (0 : un autre script de liens) */
extern char *__posix_progname;

int dl_iterate_phdr (int (*fn) (struct dl_phdr_info *, size_t, void *), void *data)
{
	if (&__ehdr_start == 0)
		return 0;
	struct dl_phdr_info i;
	memset (&i, 0, sizeof i);
	i.dlpi_name = "";
	i.dlpi_phdr = (const Elf64_Phdr *) ((const char *) &__ehdr_start + __ehdr_start.e_phoff);
	i.dlpi_phnum = __ehdr_start.e_phnum;
	i.dlpi_adds = 1;
	return fn (&i, sizeof i, data);
}

/* Le chemin du programme : le noyau ne le garde pas, il est deduit d'argv[0] au premier appel.
   Un nom seul est cherche dans PATH ; un argv[0] relatif est lu depuis le dossier courant
   d'alors ; rien de trouve : argv[0] tel quel. */
static char s_path[PATH_MAX];

/* (env.c) deja connu : l'hote .NET lance pour une DLL garde le nom du programme en argv[0] */
void __posix_exe_set (const char *path)
{
	strncpy (s_path, path, sizeof s_path - 1);
}

const char *__posix_exe_path (void)
{
	if (s_path[0] != 0)
		return s_path;
	const char *a = __posix_progname;
	if (strchr (a, '/') != 0 || strchr (a, ':') != 0)
	{
		if (realpath (a, s_path) == 0)
			strncpy (s_path, a, sizeof s_path - 1);
		return s_path;
	}
	const char *p = getenv ("PATH");
	while (p != 0 && *p != 0)
	{
		const char *e = strchr (p, ':');
		size_t n = e != 0 ? (size_t) (e - p) : strlen (p);
		char c[PATH_MAX];
		struct stat st;
		if (n + strlen (a) + 2 <= sizeof c)
		{
			memcpy (c, p, n);
			c[n] = '/';
			strcpy (c + n + 1, a);
			if (stat (c, &st) == 0 && S_ISREG (st.st_mode))
			{
				strcpy (s_path, c);
				return s_path;
			}
		}
		p = e != 0 ? e + 1 : 0;
	}
	strncpy (s_path, a, sizeof s_path - 1);
	return s_path;
}

int dladdr (const void *a, Dl_info *i)
{
	memset (i, 0, sizeof *i);
	if (&__ehdr_start == 0)
		return 0;
	const Elf64_Phdr *ph = (const Elf64_Phdr *) ((const char *) &__ehdr_start + __ehdr_start.e_phoff);
	for (unsigned n = 0; n < __ehdr_start.e_phnum; n++)
		if (ph[n].p_type == PT_LOAD && (unsigned long) a >= ph[n].p_vaddr
		    && (unsigned long) a < ph[n].p_vaddr + ph[n].p_memsz)
		{
			i->dli_fname = __posix_exe_path ();
			i->dli_fbase = (void *) &__ehdr_start;
			return 1;
		}
	return 0;
}
