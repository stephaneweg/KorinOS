/*
 * dir.c -- opendir / readdir / scandir (libposix, docs/POSIX.md ; repris d'Onyx).
 *
 * Un DIR tient un descripteur de repertoire d'AppKit (SYS_OPEN) et le lit par SYS_READDIR (nom
 * UTF-8 de 255 octets, taille, attributs FAT). Pas d'entrees « . » ni « .. » (comme Onyx) ; d_ino
 * est un hachage du chemin. dirfd donne un descripteur de repertoire (fd.c) pour fstat / fchdir /
 * openat ; fdopendir en prend un.
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
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "posix_internal.h"

struct __posix_dir
{
	long h;					/* descripteur d'AppKit du repertoire, -1 : aucun */
	int fd;					/* son descripteur (dirfd / fdopendir), -1 : aucun */
	long pos;
	char *path;
	struct dirent ent;
	unsigned char k[ARMPI_DIRENT_BYTES];	/* entree d'AppKit : nom[256], taille, attributs */
};

static long dir_open (const char *p)
{
	struct stat st;
	if (__posix_path_stat (p, &st) != 0)
		return -1;
	if (!S_ISDIR (st.st_mode))
		return POSIX_ERR (ENOTDIR);
	return POSIX_SYS (__armpi_fsys (SYS_OPEN, (long) p, ARMPI_O_READ, 0));
}

DIR *opendir (const char *path)
{
	char p[POSIX_PATH_MAX], abs[POSIX_PATH_MAX];
	if (__posix_path (path && *path ? path : ".", p, sizeof p) == 0)
		return 0;
	if (__posix_abspath (p, abs, sizeof abs) != 0)
		return 0;
	long h = dir_open (abs);
	if (h < 0)
		return 0;
	DIR *d = (DIR *) calloc (1, sizeof *d);
	if (d == 0 || (d->path = strdup (abs)) == 0)
	{
		free (d);
		__armpi_fsys (SYS_CLOSE, h, 0, 0);
		errno = ENOMEM;
		return 0;
	}
	d->h = h;
	d->fd = -1;
	return d;
}

DIR *fdopendir (int fd)
{
	struct __posix_ofd *o = __posix_fd_get (fd);
	if (o == 0)
		return 0;
	DIR *d = 0;
	if (o->type != POSIX_FD_DIR || o->path == 0)
		errno = ENOTDIR;
	else if ((d = opendir (o->path)) != 0)
		d->fd = fd;				/* (closedir closes it) */
	__posix_fd_put (o);
	return d;
}

struct dirent *readdir (DIR *d)
{
	if (d == 0 || d->h < 0)
	{
		errno = EBADF;
		return 0;
	}
	struct dirent *e = &d->ent;
	memset (e, 0, sizeof *e);
	for (;;)
	{
		long r = __armpi_fsys (SYS_READDIR, d->h, (long) d->k, 0);
		if (r == 0)
			return 0;			/* la fin (errno inchange) */
		if (r < 0)
		{
			errno = __armpi_errno (r);
			return 0;
		}
		d->k[ARMPI_DIRENT_SIZE - 1] = '\0';
		const char *n = (const char *) d->k;
		if (!strcmp (n, ".") || !strcmp (n, ".."))
			continue;			/* (comme Onyx : pas de . ni de ..) */
		char full[POSIX_PATH_MAX];
		size_t pl = strlen (d->path);
		snprintf (full, sizeof full, pl && d->path[pl - 1] == '/' ? "%s%s" : "%s/%s", d->path, n);
		if (__posix_unlink_pending (full))
			continue;			/* (efface, encore ouvert ici) */
		strcpy (e->d_name, n);
		e->d_type = (*(long long *) (d->k + ARMPI_DIRENT_ATTR) & ARMPI_ATTR_DIR) ? DT_DIR : DT_REG;
		unsigned h = 2166136261u;
		for (const char *q = d->path; *q; q++)
			h = (h ^ (unsigned char) *q) * 16777619u;
		for (const char *q = n; *q; q++)
			h = (h ^ (unsigned char) *q) * 16777619u;
		e->d_ino = (ino_t) (h ^ (h >> 16));
		break;
	}
	d->pos++;
	e->d_off = d->pos;
	e->d_reclen = sizeof *e;
	return e;
}

int readdir_r (DIR *d, struct dirent *entry, struct dirent **result)
{
	int e = errno;
	errno = 0;
	struct dirent *r = readdir (d);
	if (r == 0)
	{
		*result = 0;
		int err = errno;
		errno = e;
		return err;
	}
	memcpy (entry, r, sizeof *entry);
	*result = entry;
	errno = e;
	return 0;
}

int closedir (DIR *d)
{
	if (d == 0)
	{
		errno = EBADF;
		return -1;
	}
	if (d->h >= 0)
		__armpi_fsys (SYS_CLOSE, d->h, 0, 0);
	if (d->fd >= 0)
		__posix_fd_close (d->fd);
	free (d->path);
	free (d);
	return 0;
}

void rewinddir (DIR *d)
{
	if (d == 0)
		return;
	if (d->h >= 0)
		__armpi_fsys (SYS_CLOSE, d->h, 0, 0);
	d->h = dir_open (d->path);
	d->pos = 0;
}

long telldir (DIR *d) { return d ? d->pos : -1; }

void seekdir (DIR *d, long pos)
{
	if (d == 0)
		return;
	rewinddir (d);
	while (d->pos < pos && readdir (d))
		;
}

int dirfd (DIR *d)
{
	if (d == 0)
	{
		errno = EINVAL;
		return -1;
	}
	if (d->fd < 0)
	{
		struct __posix_ofd *o = __posix_ofd_new (POSIX_FD_DIR, O_RDONLY);
		if (o == 0)
			return -1;
		o->path = strdup (d->path);
		o->h = -1;				/* (le repertoire est lu par le DIR) */
		d->fd = __posix_fd_install (o, 0, 1);
	}
	return d->fd;
}

int alphasort (const struct dirent **a, const struct dirent **b) { return strcoll ((*a)->d_name, (*b)->d_name); }
int versionsort (const struct dirent **a, const struct dirent **b) { return strcmp ((*a)->d_name, (*b)->d_name); }

int scandir (const char *path, struct dirent ***list, int (*sel) (const struct dirent *),
	     int (*cmp) (const struct dirent **, const struct dirent **))
{
	DIR *d = opendir (path);
	if (d == 0)
		return -1;
	struct dirent **v = 0;
	size_t n = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (sel && !sel (e))
			continue;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 32;
			struct dirent **nv = (struct dirent **) realloc (v, cap * sizeof *v);
			if (nv == 0)
				goto oom;
			v = nv;
		}
		v[n] = (struct dirent *) malloc (sizeof *e);
		if (v[n] == 0)
			goto oom;
		memcpy (v[n++], e, sizeof *e);
	}
	closedir (d);
	if (cmp && n > 1)
		qsort (v, n, sizeof *v, (int (*) (const void *, const void *)) cmp);
	*list = v;
	return (int) n;
oom:
	while (n)
		free (v[--n]);
	free (v);
	closedir (d);
	errno = ENOMEM;
	return -1;
}
