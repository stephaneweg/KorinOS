/*
 * tempname.c -- mktemp, mkstemp, mkstemps, mkostemp, mkostemps, mkdtemp (et leurs _r) : ceux de
 * newlib ne savent pas creer de repertoire (mkdtemp ne fait pas de mkdir : _gettemp sans
 * HAVE_MKDIR). Tout le module est remplace, pour que mktemp.o de newlib ne soit jamais tire (ses
 * symboles en double). Noms tires de getrandom. Ajoute pour git (receive-pack : quarantaine).
 */
#include <errno.h>
#include <fcntl.h>
#include <reent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

static const char s_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/* 0 : fichier (fd dans *fdp), 1 : repertoire, 2 : nom seul */
static int gettemp (char *path, int suffix, int kind, int flags, int *fdp)
{
	size_t len = strlen (path);
	if (suffix < 0 || (size_t) suffix > len)
	{
		errno = EINVAL;
		return 0;
	}
	char *end = path + len - suffix;
	char *start = end;
	while (start > path && start[-1] == 'X')
		start--;
	if (end - start < 1)
	{
		errno = EINVAL;
		return 0;
	}
	for (int essai = 0; essai < 1000; essai++)
	{
		unsigned char r[64];
		size_t n = (size_t) (end - start);
		if (n > sizeof r)
			n = sizeof r;
		if (getrandom (r, n, 0) != (ssize_t) n)
			for (size_t i = 0; i < n; i++)
				r[i] = (unsigned char) (rand () >> 7);
		for (size_t i = 0; i < (size_t) (end - start); i++)
			start[i] = s_chars[r[i % n] % (sizeof s_chars - 1)];
		if (kind == 0)
		{
			int fd = open (path, O_CREAT | O_EXCL | O_RDWR | flags, 0600);
			if (fd >= 0)
			{
				*fdp = fd;
				return 1;
			}
		}
		else if (kind == 1)
		{
			if (mkdir (path, 0700) == 0)
				return 1;
		}
		else
		{
			struct stat st;
			if (lstat (path, &st) != 0)
				return errno == ENOENT;
			errno = EEXIST;
		}
		if (errno != EEXIST)
			return 0;
	}
	errno = EEXIST;
	return 0;
}

int mkostemps (char *path, int suffix, int flags)
{
	int fd = -1;
	return gettemp (path, suffix, 0, flags & ~O_ACCMODE, &fd) ? fd : -1;
}

int mkstemps (char *path, int suffix) { return mkostemps (path, suffix, 0); }
int mkostemp (char *path, int flags) { return mkostemps (path, 0, flags); }
int mkstemp (char *path) { return mkostemps (path, 0, 0); }

char *mkdtemp (char *path) { return gettemp (path, 0, 1, 0, 0) ? path : 0; }

char *mktemp (char *path)
{
	if (!gettemp (path, 0, 2, 0, 0))
		*path = 0;
	return path;
}

int _mkostemps_r (struct _reent *r, char *path, int suffix, int flags) { (void) r; return mkostemps (path, suffix, flags); }
int _mkstemps_r (struct _reent *r, char *path, int suffix) { (void) r; return mkstemps (path, suffix); }
int _mkostemp_r (struct _reent *r, char *path, int flags) { (void) r; return mkostemp (path, flags); }
int _mkstemp_r (struct _reent *r, char *path) { (void) r; return mkstemp (path); }
char *_mkdtemp_r (struct _reent *r, char *path) { (void) r; return mkdtemp (path); }
char *_mktemp_r (struct _reent *r, char *path)
{
	(void) r;
	if (!gettemp (path, 0, 2, 0, 0))
		*path = 0;
	return path;
}
