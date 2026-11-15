/*
 * path.c -- les chemins POSIX sur Korin (libposix, docs/POSIX.md ; repris d'Onyx) : /dev,
 * realpath, le dossier courant.
 *
 * Depuis le 15/11/2026, l'arbre de Korin est celui de Unix : « / » est le volume systeme, les autres
 * volumes sont montes dans un dossier (« /tmp » en memoire, « /media/usb0 », « /net/... »), et vfs
 * resout lui-meme « /a/b », « x », « ../x ». Un seul alias : « HOME:/a », le dossier personnel de
 * l'utilisateur en cours. Rien a traduire ici, sauf :
 *  - « /dev/null », « /dev/zero », « /dev/urandom », « /dev/random », « /dev/stdin|stdout|stderr »,
 *    « /dev/tty » : pseudo-fichiers (fd.c) ;
 *  - realpath : la forme absolue et normalisee (sans « . », « .. », « // ») ; il n'y a pas de liens.
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
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include "posix_internal.h"

/* L'arbre de Korin est deja celui de POSIX (15/11/2026, docs/POSIX.md « L'arbre des chemins ») : vfs
   resout « /a/b » par ses points de montage, « HOME:/a » est le dossier personnel. Plus rien a traduire
   entre les deux formes : ces deux fonctions gardent leur nom pour les appelants. */
const char *__posix_path_to_sys (const char *in, char *buf, size_t cap)
{
	(void) buf;
	(void) cap;
	return in;
}

int __posix_path_to_posix (const char *in, char *out, size_t cap)
{
	if (strlen (in) >= cap)
		return POSIX_ERR (ENAMETOOLONG);
	strcpy (out, in);
	return 0;
}

char *__posix_path (const char *in, char *buf, size_t cap)
{
	if (in == 0)
	{
		errno = EFAULT;
		return 0;
	}
	/* « HOME: » rencontre plus loin dans le chemin (« /x/HOME:/y » : un programme Unix qui a pris
	   « HOME:/y » pour un chemin relatif et mis son dossier courant devant) : le chemin repart de la ;
	   ':' n'est pas permis dans un nom FAT, aucun vrai nom n'est pris pour un volume */
	for (const char *p = in, *c = in; *p != '\0'; p++)
	{
		if (*p == '/')
			c = p + 1;
		else if (*p == ':' && p > c && c > in && (p[1] == '/' || p[1] == '\0'))
		{
			in = c;
			p = c;
			c = in;
		}
	}
	if (strlen (in) >= cap)
	{
		errno = ENAMETOOLONG;
		return 0;
	}
	strcpy (buf, in);
	return buf;
}

int __posix_dev_type (const char *p)
{
	if (strncmp (p, "/dev/", 5) != 0)
		return 0;
	p += 5;
	if (!strcmp (p, "null")) return POSIX_FD_NULL;
	if (!strcmp (p, "zero")) return POSIX_FD_ZERO;
	if (!strcmp (p, "urandom") || !strcmp (p, "random")) return POSIX_FD_RANDOM;
	if (!strcmp (p, "stdin") || !strcmp (p, "stdout") || !strcmp (p, "stderr") ||
	    !strcmp (p, "tty") || !strcmp (p, "console") || !strncmp (p, "fd/", 3))
		return POSIX_FD_CONSOLE;
	return 0;
}

/* the length of a volume prefix ("HOME:"), 0 if none */
static unsigned volume_prefix (const char *p)
{
	unsigned i = 0;
	while (p[i] != '\0' && p[i] != '/' && p[i] != ':' && i < 16)
		i++;
	return p[i] == ':' && i > 0 ? i + 1 : 0;
}

int __posix_abspath (const char *in, char *out, size_t cap)
{
	char raw[POSIX_PATH_MAX * 2];
	char tmp[POSIX_PATH_MAX];
	if (__posix_path (in, tmp, sizeof tmp) == 0)
		return -1;
	in = tmp;
	unsigned nv = volume_prefix (in);
	if (nv || in[0] == '/')
		snprintf (raw, sizeof raw, "%s", in);
	else
	{
		char cwd[POSIX_PATH_MAX];
		if (__armpi_fsys (SYS_GETCWD, (long) cwd, 256, 0) < 0 || cwd[0] == '\0')
			strcpy (cwd, "/");
		snprintf (raw, sizeof raw, "%s/%s", cwd, in);
	}

	/* normalise : « HOME: » en majuscules, puis ".", ".." et "//" */
	nv = volume_prefix (raw);
	size_t o = 0;
	for (unsigned k = 0; k < nv && o < cap - 1; k++)
			out[o++] = raw[k] >= 'a' && raw[k] <= 'z' ? (char) (raw[k] - 32) : raw[k];
	size_t base = o;
	size_t starts[128];
	int depth = 0;
	size_t i = nv;
	while (raw[i] != '\0')
	{
		while (raw[i] == '/')
			i++;
		if (raw[i] == '\0')
			break;
		size_t j = i;
		while (raw[j] != '\0' && raw[j] != '/')
			j++;
		size_t len = j - i;
		if (len == 1 && raw[i] == '.')
			;
		else if (len == 2 && raw[i] == '.' && raw[i + 1] == '.')
		{
			if (depth > 0)
				o = starts[--depth];
		}
		else
		{
			if (depth < 128)
				starts[depth++] = o;
			if (o + len + 2 >= cap)
			{
				errno = ENAMETOOLONG;
				return -1;
			}
			out[o++] = '/';
			memcpy (out + o, raw + i, len);
			o += len;
		}
		i = j;
	}
	if (o == base)
		out[o++] = '/';
	out[o] = '\0';
	return 0;
}

char *realpath (const char *path, char *resolved)
{
	if (path == 0)
	{
		errno = EINVAL;
		return 0;
	}
	if (*path == '\0')
	{
		errno = ENOENT;
		return 0;
	}
	if (strcmp (path, "/proc/self/exe") == 0)
		path = __posix_exe_path ();
	char buf[POSIX_PATH_MAX];
	if (__posix_abspath (path, buf, sizeof buf) != 0)
		return 0;
	struct stat st;
	if (__posix_path_stat (buf, &st) != 0)
		return 0;
	char px[POSIX_PATH_MAX];
	if (__posix_path_to_posix (buf, px, sizeof px) == 0)
		strcpy (buf, px);
	if (resolved == 0)
		return strdup (buf);
	size_t n = strlen (buf);		/* (sans le remplissage de strncpy jusqu'a PATH_MAX) */
	if (n > PATH_MAX - 1)
		n = PATH_MAX - 1;
	memcpy (resolved, buf, n);
	resolved[n] = '\0';
	return resolved;
}

char *canonicalize_file_name (const char *path) { return realpath (path, 0); }

/* path change de dossier courant : SYS_CHDIR */
static int cwd_change (const char *path)
{
	return (int) POSIX_SYS (__armpi_fsys (SYS_CHDIR, (long) path, 0, 0));
}

char *getcwd (char *buf, size_t size)
{
	char tmp[POSIX_PATH_MAX];
	long n = __armpi_fsys (SYS_GETCWD, (long) tmp, 256, 0);
	if (n < 0 || tmp[0] == '\0')
	{
		errno = ENOENT;
		return 0;
	}
	size_t len = strlen (tmp);
	if (buf == 0)
	{
		if (size == 0)
			size = len + 1;
		if (size <= len)
		{
			errno = ERANGE;
			return 0;
		}
		buf = (char *) malloc (size);
		if (buf == 0)
		{
			errno = ENOMEM;
			return 0;
		}
	}
	else if (size == 0)
	{
		errno = EINVAL;
		return 0;
	}
	else if (size <= len)
	{
		errno = ERANGE;
		return 0;
	}
	memcpy (buf, tmp, len + 1);
	return buf;
}

char *getwd (char *buf) { return getcwd (buf, PATH_MAX); }
char *get_current_dir_name (void) { return getcwd (0, 0); }

int chdir (const char *path)
{
	char p[POSIX_PATH_MAX];
	if (__posix_path (path, p, sizeof p) == 0)
		return -1;
	struct stat st;
	if (__posix_path_stat (p, &st) != 0)
		return -1;
	if (!S_ISDIR (st.st_mode))
		return POSIX_ERR (ENOTDIR);
	return cwd_change (p);
}

int fchdir (int fd)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	int r;
	if (d->type != POSIX_FD_DIR || d->path == 0)
		r = POSIX_ERR (ENOTDIR);
	else
		r = cwd_change (d->path);
	__posix_fd_put (d);
	return r;
}

char *__posix_at_path (int dirfd, const char *name, char *buf, size_t cap)
{
	if (name == 0)
	{
		errno = EFAULT;
		return 0;
	}
	if (dirfd == AT_FDCWD || name[0] == '/' || volume_prefix (name))
		return __posix_path (name, buf, cap);
	struct __posix_ofd *d = __posix_fd_get (dirfd);
	if (d == 0)
		return 0;
	char *r = 0;
	if (d->type != POSIX_FD_DIR || d->path == 0)
		errno = ENOTDIR;
	else if ((size_t) snprintf (buf, cap, "%s/%s", d->path, name) >= cap)
		errno = ENAMETOOLONG;
	else
		r = buf;
	__posix_fd_put (d);
	return r;
}
