/*
 * ftw.c -- ftw, nftw : parcours d'une arborescence (libposix, docs/POSIX.md). Pas de liens sur les
 * volumes d'ARMPI : FTW_PHYS est sans effet, FTW_SL et FTW_SLN n'arrivent jamais.
 */
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "posix_internal.h"

static int walk (char *path, size_t cap, int base, int level, int flags,
		 int (*fn) (const char *, const struct stat *, int, struct FTW *))
{
	struct stat st;
	struct FTW f = { base, level };
	if (stat (path, &st) != 0)
		return fn (path, &st, FTW_NS, &f);
	if (!S_ISDIR (st.st_mode))
		return fn (path, &st, FTW_F, &f);
	int r;
	if (!(flags & FTW_DEPTH) && (r = fn (path, &st, FTW_D, &f)) != 0)
		return r;
	DIR *d = opendir (path);
	if (d == 0)
		return fn (path, &st, FTW_DNR, &f);
	size_t n = strlen (path);
	size_t b = n && path[n - 1] != '/' && path[n - 1] != ':' ? n + 1 : n;
	r = 0;
	struct dirent *e;
	while (r == 0 && (e = readdir (d)) != 0)
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, ".."))
			continue;
		if (b + strlen (e->d_name) + 1 >= cap)
		{
			errno = ENAMETOOLONG;
			r = -1;
			break;
		}
		path[n] = '/';
		strcpy (path + b, e->d_name);
		r = walk (path, cap, (int) b, level + 1, flags, fn);
		path[n] = '\0';
	}
	closedir (d);
	if (r == 0 && (flags & FTW_DEPTH))
		r = fn (path, &st, FTW_DP, &f);
	return r;
}

int nftw (const char *path, int (*fn) (const char *, const struct stat *, int, struct FTW *), int fds, int flags)
{
	(void) fds;
	char *p = (char *) malloc (POSIX_PATH_MAX);
	if (p == 0)
		return -1;
	size_t n = strlen (path);
	if (n >= POSIX_PATH_MAX)
	{
		free (p);
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy (p, path, n + 1);
	const char *s = strrchr (p, '/');
	int r = walk (p, POSIX_PATH_MAX, s ? (int) (s - p) + 1 : 0, 0, flags, fn);
	free (p);
	return r;
}

/* ftw : le meme parcours, sans le quatrieme argument de la fonction */
static __thread int (*s_ftw) (const char *, const struct stat *, int);

static int ftw_one (const char *p, const struct stat *st, int t, struct FTW *f)
{
	(void) f;
	return s_ftw (p, st, t);
}

int ftw (const char *path, int (*fn) (const char *, const struct stat *, int), int fds)
{
	int (*old) (const char *, const struct stat *, int) = s_ftw;
	s_ftw = fn;
	int r = nftw (path, ftw_one, fds, 0);
	s_ftw = old;
	return r;
}
