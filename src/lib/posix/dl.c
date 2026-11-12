/*
 * dl.c -- le chemin de l'executable (/proc/self/exe, libposix, docs/POSIX.md). Le chargeur de
 * modules, dl_iterate_phdr et dladdr sont dans dlopen.c (attire par qui s'en sert).
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

extern char *__posix_progname;

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
