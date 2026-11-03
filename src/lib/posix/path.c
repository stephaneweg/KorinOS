/*
 * path.c -- les chemins POSIX sur ARMPI (libposix, docs/POSIX.md ; repris d'Onyx) : /tmp,
 * /dev, realpath, le dossier courant.
 *
 * Un chemin d'ARMPI nomme un volume : « SD0:/a/b », « SD1:/x », « USB0:/y », « RAM0:/z ». vfs
 * resout « x », « ./x », « ../x » d'apres le dossier courant, un chemin virtuel (« /gcc » : voir
 * SD0:/etc/paths.txt, docs/FICHIERS.md) par sa table et les autres « /x » d'apres le volume du
 * dossier courant : les chemins relatifs et absolus a la POSIX marchent tels quels. Deux noms sont
 * traduits ici :
 *  - « /tmp » et « /tmp/... » -> « RAM0:/tmp/... » (le volume en memoire, cree au premier usage) :
 *    mkstemp / tmpfile ecrivent en memoire, pas sur la carte ;
 *  - « /dev/null », « /dev/zero », « /dev/urandom », « /dev/random », « /dev/stdin|stdout|stderr »,
 *    « /dev/tty » : pseudo-fichiers (fd.c).
 * realpath donne la forme absolue et normalisee (« SD0:/a/b » ; SD: et USB: deviennent SD0: et
 * USB0:) ; il n'y a pas de liens.
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

static volatile int s_tmpMade;

static int starts_with_dir (const char *p, const char *dir)
{
	size_t n = strlen (dir);
	return strncmp (p, dir, n) == 0 && (p[n] == '\0' || p[n] == '/');
}

char *__posix_path (const char *in, char *buf, size_t cap)
{
	if (in == 0)
	{
		errno = EFAULT;
		return 0;
	}
	/* a volume met as a later component ("/SD:/RAM:/t.db", "SD:/x/USB1:/y": a Unix program that
	   took "RAM:/t.db" for a relative path and put its cwd before it) restarts the path there;
	   ':' is not allowed in a FAT name, so no real file name is mistaken for one */
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
	if (starts_with_dir (in, "/tmp"))
	{
		if (!s_tmpMade)
		{
			__armpi_fsys (SYS_MKDIR, (long) "RAM0:/tmp", 0, 0);	/* (E_EXIST s'il existe) */
			s_tmpMade = 1;
		}
		if (strlen (in) + 5 >= cap)
		{
			errno = ENAMETOOLONG;
			return 0;
		}
		strcpy (buf, "RAM0:");
		strcat (buf, in);
		return buf;
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

/* the length of a volume prefix ("SD:", "RAM:", "USB1:"), 0 if none */
static unsigned volume_prefix (const char *p)
{
	unsigned i = 0;
	while (p[i] != '\0' && p[i] != '/' && p[i] != ':' && i < 16)
		i++;
	return p[i] == ':' && i > 0 ? i + 1 : 0;
}

/* « /gcc/bin » passe-t-il par un chemin virtuel de vfs (SD0:/etc/paths.txt) ? */
static int __posix_path_virtual (const char *p)
{
	struct __armpi_msg m;
	if (strlen (p) >= sizeof m.data)
		return 0;
	memset (&m, 0, sizeof m);
	m.op = VFS_PATHQ;
	strcpy ((char *) m.data, p);
	m.len = (unsigned) strlen (p) + 1;
	return __armpi_vfs_call (&m) == 1;
}

int __posix_abspath (const char *in, char *out, size_t cap)
{
	char raw[POSIX_PATH_MAX * 2];
	char tmp[POSIX_PATH_MAX];
	if (__posix_path (in, tmp, sizeof tmp) == 0)
		return -1;
	in = tmp;
	unsigned nv = volume_prefix (in);
	if (nv || (in[0] == '/' && __posix_path_virtual (in)))
		snprintf (raw, sizeof raw, "%s", in);		/* (un chemin virtuel reste tel quel) */
	else
	{
		char cwd[POSIX_PATH_MAX];
		if (__armpi_fsys (SYS_GETCWD, (long) cwd, 256, 0) < 0 || cwd[0] == '\0')
			strcpy (cwd, "SD0:/");
		if (in[0] == '/')
		{
			unsigned n = volume_prefix (cwd);
			if (n == 0)
				snprintf (raw, sizeof raw, "SD0:%s", in);
			else
				snprintf (raw, sizeof raw, "%.*s%s", (int) n, cwd, in);
		}
		else
			snprintf (raw, sizeof raw, "%s/%s", cwd, in);
	}

	/* normalise : le volume en majuscules (SD: et USB: sont SD0: et USB0:), puis ".", ".." et "//" */
	nv = volume_prefix (raw);
	size_t o = 0;
	if (nv == 3 && (raw[0] == 'S' || raw[0] == 's') && (raw[1] == 'D' || raw[1] == 'd'))
	{
		memcpy (out, "SD0:", 4);
		o = 4;
	}
	else if (nv == 4 && strncasecmp (raw, "USB:", 4) == 0)
	{
		memcpy (out, "USB0:", 5);
		o = 5;
	}
	else
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
	if (resolved == 0)
		return strdup (buf);
	strncpy (resolved, buf, PATH_MAX - 1);
	resolved[PATH_MAX - 1] = '\0';
	return resolved;
}

char *canonicalize_file_name (const char *path) { return realpath (path, 0); }

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
	return (int) POSIX_SYS (__armpi_fsys (SYS_CHDIR, (long) p, 0, 0));
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
		r = (int) POSIX_SYS (__armpi_fsys (SYS_CHDIR, (long) d->path, 0, 0));
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
