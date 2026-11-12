/*
 * env.c -- l'environnement et les arguments de depart d'un programme (libposix, docs/POSIX.md ;
 * repris d'Onyx).
 *
 * Un fils de posix_spawn recoit argv et environ dans un objet partage (proc.c, ipc.c), sans
 * limite de longueur. Sinon, ARMPI donne une ligne d'arguments (x0 a l'entree : crt0posix,
 * start.c ; 1 024 octets), sans le nom du programme ni d'environnement : argv[0] = le nom du
 * process (SYS_PROCLIST), puis la ligne coupee aux espaces (un argument entre guillemets peut en
 * contenir) ; environ = des valeurs par defaut. getenv / setenv / unsetenv / putenv sont ceux de
 * newlib, sur cet environ. TZ vaut UTC0 si rien ne le donne. Entrees / sorties sur un pty
 * (tty.c) : TERM=xterm-256color et COLORTERM=truecolor si TERM manque ou vaut dumb.
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
#include <stdio.h>
#include <time.h>
#include "posix_internal.h"

static char *s_noenv[1] = { 0 };
char **environ = s_noenv;
char *__posix_progname = "a.out";
char *program_invocation_name = "a.out";
char *program_invocation_short_name = "a.out";

static const char *const s_defaults[] =
{
	"HOME=SD0:/", "PATH=/bin:/gcc/bin", "TMPDIR=/tmp", "LANG=C.UTF-8", "TERM=dumb", 0
};

long __posix_spawn_h = -1;
unsigned __posix_spawn_pages;
static const struct __posix_spawn_blob *s_blob;

/* l'objet du pere, projete ; 0 sans objet (ou objet illisible) */
static const struct __posix_spawn_blob *blob_map (void)
{
	if (__posix_spawn_h < 0)
		return 0;
	long r = __armpi_sys6 (SYS_SHM_MAP, __posix_spawn_h, 0, (long) __posix_spawn_pages << 12, 1 /* PROT_READ */, 0, 0);
	if (r < 0 && r > -4096)
		return 0;
	const struct __posix_spawn_blob *b = (const struct __posix_spawn_blob *) r;
	return b->magic == POSIX_SPAWN_MAGIC ? b : 0;
}

static void blob_unmap (void)
{
	if (s_blob)
		__armpi_sys2 (SYS_VM_UNMAP, s_blob, (long) __posix_spawn_pages << 12);
	if (__posix_spawn_h >= 0)
		__armpi_sys1 (SYS_HANDLE_CLOSE, __posix_spawn_h);
	s_blob = 0;
	__posix_spawn_h = -1;
}

/* n chaines a la suite -> un tableau de copies, termine par 0 (*next : apres la derniere) */
static char **strings_copy (const char *s, unsigned n, const char **next)
{
	char **v = (char **) malloc ((n + 1) * sizeof *v);
	if (v == 0)
		return 0;
	for (unsigned i = 0; i < n; i++)
	{
		v[i] = strdup (s);
		s += strlen (s) + 1;
	}
	v[n] = 0;
	if (next)
		*next = s;
	return v;
}

void __posix_env_init (void)
{
	s_blob = blob_map ();
	if (s_blob)
		__posix_exec_restore (s_blob, (unsigned long) __posix_spawn_pages << 12);	/* (apres exec) */
	char **v = 0;
	if (s_blob)
	{
		const char *s = s_blob->strings;
		for (unsigned i = 0; i < s_blob->argc; i++)
			s += strlen (s) + 1;
		v = strings_copy (s, s_blob->envc, 0);
	}
	else
		v = strings_copy (0, 0, 0);
	if (v && s_blob == 0)
	{
		int n = 0;
		while (s_defaults[n])
			n++;
		char **d = (char **) realloc (v, (n + 1) * sizeof *d);
		if (d)
		{
			for (int i = 0; i < n; i++)
				d[i] = strdup (s_defaults[i]);
			d[n] = 0;
			v = d;
		}
	}
	if (v)
		environ = v;
	/* entrees / sorties sur un pty (le Terminal, telnetd) : un xterm, sauf TERM donne */
	const char *term = getenv ("TERM");
	if (__posix_tty_stdio () && (term == 0 || strcmp (term, "dumb") == 0))
	{
		setenv ("TERM", "xterm-256color", 1);
		if (getenv ("COLORTERM") == 0)
			setenv ("COLORTERM", "truecolor", 1);
	}
	if (getenv ("TZ") == 0)
	{
		int tz = __posix_tz_minutes ();
		char z[32];
		if (tz == 0)
			strcpy (z, "UTC0");
		else
		{
			int a = tz < 0 ? -tz : tz;
			snprintf (z, sizeof z, "LCL%c%02d:%02d", tz > 0 ? '-' : '+', a / 60, a % 60);
		}
		setenv ("TZ", z, 1);
	}
	tzset ();
}

/* le nom de ce process (SYS_PROCLIST), ou "a.out" */
static char *own_name (void)
{
	static long list[64 * (ARMPI_PROCLIST_ENT / 8)];
	long me = __armpi_sys0 (SYS_GETPID);
	long n = __armpi_sys2 (SYS_PROCLIST, list, 64);
	for (long i = 0; i < n; i++)
	{
		long *e = list + i * (ARMPI_PROCLIST_ENT / 8);
		if (e[0] == me)
		{
			char name[33];
			memcpy (name, (char *) e + 24, 32);
			name[32] = '\0';
			if (name[0])
				return strdup (name);
		}
	}
	return "a.out";
}

/* les noms courts du programme, d'apres argv[0] */
static void names_set (char **argv)
{
	__posix_progname = argv[0];
	program_invocation_name = argv[0];
	const char *s = argv[0];
	for (const char *q = argv[0]; *q; q++)
		if (*q == '/' || *q == ':')
			s = q + 1;
	program_invocation_short_name = (char *) s;
}

int __posix_args_init (const char *line0, char ***pargv)
{
	/* (lance pour une DLL .NET, f_spawn_file d'AppKit) 1, son chemin, 1 : argv[1] */
	char *app = 0;
	if (line0 && line0[0] == 1)
	{
		const char *e = strchr (line0 + 1, 1);
		if (e)
		{
			app = strndup (line0 + 1, (size_t) (e - line0 - 1));
			line0 = e + 1;
			__posix_exe_set ("SD0:/dotnet/dotnet");
		}
	}
	if (s_blob && s_blob->argc > 0)
	{
		char **v = strings_copy (s_blob->strings, s_blob->argc, 0);
		if (v)
		{
			int argc = (int) s_blob->argc;
			blob_unmap ();
			if (app)
			{
				char **nv = (char **) realloc (v, (argc + 2) * sizeof *v);
				if (nv)
				{
					v = nv;
					memmove (v + 2, v + 1, argc * sizeof *v);
					v[1] = app;
					argc++;
				}
			}
			names_set (v);
			*pargv = v;
			return argc;
		}
	}
	blob_unmap ();
	int cap = 8;
	char **argv = (char **) malloc (cap * sizeof *argv);
	if (argv == 0)
	{
		static char *none[2] = { "a.out", 0 };
		*pargv = none;
		return 1;
	}
	int argc = 0;
	argv[argc++] = own_name ();
	if (app)
		argv[argc++] = app;
	char *line = line0 ? strdup (line0) : 0;
	char *p = line;
	while (p && *p)
	{
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0')
			break;
		char *start, *end;
		if (*p == '"')
		{
			/* entre guillemets : \" et \\ (posix_spawn, proc.c) */
			start = ++p;
			char *w = p;
			while (*p && *p != '"')
			{
				if (*p == '\\' && (p[1] == '"' || p[1] == '\\'))
					p++;
				*w++ = *p++;
			}
			end = w;
		}
		else
		{
			start = p;
			while (*p && *p != ' ' && *p != '\t')
				p++;
			end = p;
		}
		if (*p)
			p++;
		*end = '\0';
		if (argc + 2 > cap)
		{
			cap *= 2;
			char **nv = (char **) realloc (argv, cap * sizeof *argv);
			if (nv == 0)
				break;
			argv = nv;
		}
		argv[argc++] = start;
	}
	argv[argc] = 0;
	names_set (argv);
	*pargv = argv;
	return argc;
}

const char *getprogname (void) { return program_invocation_short_name; }
void setprogname (const char *name) { program_invocation_short_name = (char *) name; }

int clearenv (void)
{
	environ = s_noenv;
	return 0;
}

/* setenv, unsetenv : ceux de newlib (_setenv_r, _unsetenv_r, sans reent : NULL comme son
   setenv.o), avec la verification de POSIX que newlib ne fait pas pour setenv : un nom vide ou
   qui contient '=' rend EINVAL (sinon la variable creee ne peut plus etre retiree : unsetenv la
   refuse ; os.environ.clear () de CPython echouait). Definies ici toutes les deux : setenv.o de
   newlib n'est plus tire. */
struct _reent;
int _setenv_r (struct _reent *, const char *, const char *, int);
int _unsetenv_r (struct _reent *, const char *);

static int env_name_ok (const char *name)
{
	return name && *name && !strchr (name, '=');
}

int setenv (const char *name, const char *value, int overwrite)
{
	if (!env_name_ok (name))
		return POSIX_ERR (EINVAL);
	return _setenv_r (0, name, value, overwrite);
}

int unsetenv (const char *name)
{
	if (!env_name_ok (name))
		return POSIX_ERR (EINVAL);
	return _unsetenv_r (0, name);
}
