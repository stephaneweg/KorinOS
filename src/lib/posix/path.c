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
 * L'arbre POSIX (12/11/2026, docs/POSIX.md « L'arbre POSIX des chemins ») : la table de vfs
 * (SD0:/etc/paths.txt, lue une fois par process par VFS_PATHLIST) a « /sd0 », « /sd1 », « /ram0 »
 * a cote de « /bin », « /tmp »... Alors :
 *  - un « /x » sans volume qui n'est pas dans la table vise toujours SD0:/x, quel que soit le
 *    volume du dossier courant (vfs, lui, garde sa regle du volume courant pour l'assembleur) ;
 *  - getcwd, realpath et /proc/self/exe rendent la forme POSIX : la ligne de la table dont le
 *    chemin physique est le plus long prefixe (« SD0:/home/x » -> « /sd0/home/x », « SD0:/bin/sh »
 *    -> « /bin/sh », « RAM0:/tmp/a » -> « /tmp/a ») ; un volume sans ligne garde « VOL:/... ».
 * Les deux formes restent acceptees partout en entree. Sans VFS_PATHLIST (un vfs ancien), tout
 * reste comme avant.
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

/* ---- la table des chemins de vfs (l'arbre POSIX) ---- */

#define MAP_N 16
static struct map_entry
{
	char v[64];			/* « /sd0 », « /gcc » */
	char p[POSIX_PATH_MAX];		/* « SD0: » (la racine d'un volume), « SD0:/gcc » */
	unsigned lv, lp;
} s_map[MAP_N];
static int s_map_n;
static int s_map_state;			/* 0 : pas lue ; 1 : lue ; -1 : vfs sans VFS_PATHLIST */

/* 1 si la table est la (lue au premier besoin, puis gardee : un fils de fork l'a deja) */
static int map_ready (void)
{
	int st = __atomic_load_n (&s_map_state, __ATOMIC_ACQUIRE);
	if (st != 0)
		return st > 0;
	struct map_entry *t = (struct map_entry *) calloc (MAP_N, sizeof *t);
	if (t == 0)
		return 0;
	int n = 0;
	long next = 0;
	while (next < MAP_N && n < MAP_N)
	{
		struct __armpi_msg m;
		memset (&m, 0, sizeof m);
		m.op = VFS_PATHLIST;
		m.a = next;
		long r = __armpi_vfs_call (&m);
		if (r < 0 && n == 0)
		{
			free (t);
			__atomic_store_n (&s_map_state, -1, __ATOMIC_RELEASE);
			return 0;
		}
		if (r <= 0)
			break;
		m.data[sizeof m.data - 1] = 0;
		const char *d = (const char *) m.data;
		for (long k = 0; k < r && n < MAP_N; k++)
		{
			const char *v = d, *p = v + strlen (v) + 1;
			d = p + strlen (p) + 1;
			/* le physique interne « /sd0/gcc » -> « SD0:/gcc » ; « /sd0 » -> « SD0: » */
			const char *s = p[0] == '/' ? p + 1 : p;
			size_t lvol = strcspn (s, "/");
			if (strlen (v) >= sizeof t[n].v || lvol + strlen (s + lvol) + 2 >= sizeof t[n].p)
				continue;
			strcpy (t[n].v, v);
			for (size_t i = 0; i < lvol; i++)
				t[n].p[i] = s[i] >= 'a' && s[i] <= 'z' ? (char) (s[i] - 32) : s[i];
			t[n].p[lvol] = ':';
			strcpy (t[n].p + lvol + 1, s + lvol);
			t[n].lv = (unsigned) strlen (t[n].v);
			t[n].lp = (unsigned) strlen (t[n].p);
			n++;
		}
		if (m.b <= next)
			break;
		next = m.b;
	}
	if (__atomic_load_n (&s_map_state, __ATOMIC_ACQUIRE) == 0)
	{
		memcpy (s_map, t, sizeof s_map);
		s_map_n = n;
		__atomic_store_n (&s_map_state, 1, __ATOMIC_RELEASE);
	}
	free (t);
	return 1;
}

/* « /gcc/bin » : l'entree de la table qui le couvre (composants entiers, l'ordre de vfs), -1 sinon */
static int map_virtual (const char *p)
{
	for (int i = 0; i < s_map_n; i++)
		if (strncmp (p, s_map[i].v, s_map[i].lv) == 0 && (p[s_map[i].lv] == '\0' || p[s_map[i].lv] == '/'))
			return i;
	return -1;
}

/* Un chemin absolu normalise (« /gcc/x », « /sd0/a », « SD0:/a ») -> la forme du systeme (« SD0:/gcc/x »,
   « SD0:/a ») : l'entree de la table developpee. Rend in tel quel sans la table, ou si trop long. */
const char *__posix_path_to_sys (const char *in, char *buf, size_t cap)
{
	if (in[0] != '/' || !map_ready ())
		return in;
	int i = map_virtual (in);
	const char *p = i >= 0 ? s_map[i].p : "SD0:", *rest = i >= 0 ? in + s_map[i].lv : in;
	if (rest[0] == '\0' && p[strlen (p) - 1] == ':')
		rest = "/";				/* (« /sd0 » -> « SD0:/ ») */
	if ((size_t) snprintf (buf, cap, "%s%s", p, rest) >= cap)
		return in;
	return buf;
}

/* Un chemin absolu du systeme (« SD0:/a/b », normalise, ou virtuel « /gcc/x ») -> sa forme POSIX
   (« /sd0/a/b ») : la ligne de la table au plus long prefixe physique. 0 ; -1 (ENAMETOOLONG). */
int __posix_path_to_posix (const char *in, char *out, size_t cap)
{
	char sys[POSIX_PATH_MAX];
	if (!map_ready ())
		goto copy;
	const char *s = __posix_path_to_sys (in, sys, sizeof sys);
	int best = -1;
	for (int i = 0; i < s_map_n; i++)
		if (strncasecmp (s, s_map[i].p, s_map[i].lp) == 0 && (s[s_map[i].lp] == '\0' || s[s_map[i].lp] == '/')
		    && (best < 0 || s_map[i].lp > s_map[best].lp))
			best = i;
	if (best >= 0)
	{
		const char *rest = s + s_map[best].lp;
		if (rest[0] == '/' && rest[1] == '\0')
			rest++;
		if ((size_t) snprintf (out, cap, "%s%s", s_map[best].v, rest) >= cap)
			return POSIX_ERR (ENAMETOOLONG);
		return 0;
	}
	in = s;
copy:
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
	/* « /x » hors de la table : SD0:/x, pas la racine du volume courant (l'arbre POSIX) ; /dev et
	   /proc restent tels quels (pseudo-fichiers) */
	if (in[0] == '/' && strncmp (in, "/dev/", 5) != 0 && strncmp (in, "/proc/", 6) != 0
	    && map_ready () && map_virtual (in) < 0)
	{
		if (strlen (in) + 4 >= cap)
		{
			errno = ENAMETOOLONG;
			return 0;
		}
		strcpy (buf, "SD0:");
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

/* Le dossier courant pris par un chemin virtuel (« /apps/x », SD0:/etc/paths.txt) : getcwd le rend sous
   cette forme, celle de realpath, et non sous celle du volume (« SD0:/apps/x ») que le systeme garde ; un
   chemin relatif en part aussi. Sinon un programme qui compare les deux se trompe : Irrlicht
   (SuperTuxKart) monte un dossier par realpath puis le liste depuis getcwd, et coupe chaque nom a la
   longueur du premier. s_cwd_sys : la forme du systeme a ce moment, pour reconnaitre un dossier change
   autrement (un fils lance la). */
static char s_cwd_virtual[POSIX_PATH_MAX], s_cwd_sys[POSIX_PATH_MAX];

int __posix_abspath (const char *in, char *out, size_t cap)
{
	char raw[POSIX_PATH_MAX * 2];
	char tmp[POSIX_PATH_MAX];
	if (__posix_path (in, tmp, sizeof tmp) == 0)
		return -1;
	in = tmp;
	unsigned nv = volume_prefix (in);
	if (nv || (in[0] == '/' && (map_ready () ? map_virtual (in) >= 0 : __posix_path_virtual (in))))
		snprintf (raw, sizeof raw, "%s", in);		/* (un chemin virtuel reste tel quel) */
	else
	{
		char cwd[POSIX_PATH_MAX];
		if (__armpi_fsys (SYS_GETCWD, (long) cwd, 256, 0) < 0 || cwd[0] == '\0')
			strcpy (cwd, "SD0:/");
		else if (s_cwd_virtual[0] != '\0' && strcmp (cwd, s_cwd_sys) == 0 && in[0] != '/')
			strcpy (cwd, s_cwd_virtual);		/* (le dossier courant virtuel : la forme de getcwd) */
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

/* path change de dossier courant : SYS_CHDIR, et sa forme virtuelle gardee si c'en est une (calculee avant :
   un chemin relatif part du dossier d'avant) */
static int cwd_change (const char *path)
{
	char v[POSIX_PATH_MAX];
	if (__posix_abspath (path, v, sizeof v) != 0)
		v[0] = '\0';
	int r = (int) POSIX_SYS (__armpi_fsys (SYS_CHDIR, (long) path, 0, 0));
	if (r != 0)
		return r;
	s_cwd_virtual[0] = '\0';
	/* (avec la table, getcwd rend toujours la forme POSIX : rien a garder) */
	if (v[0] == '/' && !map_ready () && __armpi_fsys (SYS_GETCWD, (long) s_cwd_sys, 256, 0) >= 0 && s_cwd_sys[0] != '\0')
	{
		strncpy (s_cwd_virtual, v, sizeof s_cwd_virtual - 1);
		s_cwd_virtual[sizeof s_cwd_virtual - 1] = '\0';
	}
	return 0;
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
	if (map_ready ())
	{
		char px[POSIX_PATH_MAX];
		if (__posix_path_to_posix (tmp, px, sizeof px) == 0)
			strcpy (tmp, px);
	}
	else if (s_cwd_virtual[0] != '\0' && strcmp (tmp, s_cwd_sys) == 0)
		strcpy (tmp, s_cwd_virtual);
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
