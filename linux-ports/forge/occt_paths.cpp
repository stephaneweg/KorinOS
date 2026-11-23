//
// occt_paths.cpp -- les chemins de Korin pour ce qu'OpenCASCADE ouvre lui-meme. Forge lui donne « HOME:/forge/x.step »
// (l'export, outil export du MCP) ; OCCT est une bibliotheque partagee du systeme, ses fopen et open ne passent pas par
// les -Wl,--wrap de korin_paths.cpp (qui n'enveloppent que les appels des .o du programme). Ces quatre fonctions,
// definies dans le programme et exportees, passent devant celles de la libc pour les bibliotheques aussi : le chemin
// est traduit (kport_path), puis la vraie fonction (dlsym RTLD_NEXT) est appelee. Un chemin de Linux reste tel quel.
//
#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include "korin_port.h"

typedef FILE *(*fopen_t) (const char *, const char *);
typedef int (*open_t) (const char *, int, ...);
#define REAL(T, n) static T real = 0; if (!real) real = (T) dlsym (RTLD_NEXT, n)

extern "C" {

FILE *fopen (const char *p, const char *m)
{
	REAL (fopen_t, "fopen");
	char b[PATH_MAX];
	return real (kport_path (p, b, sizeof b), m);
}
FILE *fopen64 (const char *p, const char *m)
{
	REAL (fopen_t, "fopen64");
	char b[PATH_MAX];
	return real (kport_path (p, b, sizeof b), m);
}
int open (const char *p, int flags, ...)
{
	REAL (open_t, "open");
	mode_t md = 0;
	if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start (a, flags); md = (mode_t) va_arg (a, int); va_end (a); }
	char b[PATH_MAX];
	return real (kport_path (p, b, sizeof b), flags, md);
}
int open64 (const char *p, int flags, ...)
{
	REAL (open_t, "open64");
	mode_t md = 0;
	if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start (a, flags); md = (mode_t) va_arg (a, int); va_end (a); }
	char b[PATH_MAX];
	return real (kport_path (p, b, sizeof b), flags, md);
}

}
