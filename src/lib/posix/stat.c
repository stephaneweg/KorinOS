/*
 * stat.c -- stat, acces, liens, renommage, droits, dates, statvfs (libposix, docs/POSIX.md ;
 * reecrit pour ARMPI d'apres stat.c d'Onyx).
 *
 * vfs donne d'un chemin sa taille, ses attributs FAT, sa date et le bit d'execution (V_STATX) :
 * st_mode (S_IFDIR 0755, S_IFREG 0644, 0755 si vfs le dit executable : regles de chemin ou index,
 * docs/FICHIERS.md), st_size, st_mtime (st_atime et st_ctime : la meme) ; st_ino est un hachage
 * du chemin absolu, st_dev celui du volume ; pas de proprietaire (0). Pas de liens ni de liens
 * symboliques (ENOSYS ; readlink : EINVAL). chmod, fchmod : V_CHMOD (seuls les bits x comptent :
 * l'index des executables de vfs) ; access(X_OK) : le bit d'execution ; chown : 0 sans effet.
 * utime... : V_UTIME. rename : V_RENAME ; d'un volume a l'autre, un fichier est recopie puis
 * l'ancien efface, un repertoire : EXDEV.
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
#include <errno.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <sys/statfs.h>
#include "posix_internal.h"

static unsigned long long hash (const char *s, size_t n)
{
	unsigned long long h = 1469598103934665603ULL;	/* FNV-1a, sans casse (FAT) */
	for (size_t i = 0; i < n && s[i]; i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c >= 'A' && c <= 'Z')
			c += 32;
		h = (h ^ c) * 1099511628211ULL;
	}
	return h ? h : 1;
}

static void fill (struct stat *st, const char *abs, long long size, int dir, int x)
{
	memset (st, 0, sizeof *st);
	st->st_mode = dir ? S_IFDIR | 0755 : x ? S_IFREG | 0755 : S_IFREG | 0644;
	st->st_size = dir ? 0 : size;
	st->st_nlink = dir ? 2 : 1;
	st->st_blksize = 512;
	st->st_blocks = (st->st_size + 511) / 512;
	if (abs)
	{
		/* (une seule identite par fichier : « /sd0/a », « /a » et « SD0:/a » hachent « SD0:/a ») */
		char sys[POSIX_PATH_MAX];
		abs = __posix_path_to_sys (abs, sys, sizeof sys);
		const char *c = strchr (abs, ':');
		st->st_ino = (ino_t) hash (abs, strlen (abs));
		st->st_dev = (dev_t) (c ? hash (abs, (size_t) (c - abs)) & 0xFFFF : 1);
	}
}

/* V_STATX a vfs -> 0, info = {taille, attributs (+ ARMPI_ATTR_EXEC), date en secondes UTC}, ou
 * une erreur d'ARMPI (un vfs d'avant V_STATX : V_STAT) */
static long path_info (const char *abs, long long info[3])
{
	struct __armpi_msg m;
	size_t n = strlen (abs);
	if (n >= sizeof m.data)
		return -36;				/* ENAMETOOLONG */
	memset (&m, 0, sizeof m);
	m.op = V_STATX;
	strcpy ((char *) m.data, abs);
	m.len = (unsigned) n + 1;
	long r = __armpi_vfs_call (&m);
	if (r == -22)
	{
		memset (&m, 0, sizeof m);
		m.op = V_STAT;
		strcpy ((char *) m.data, abs);
		m.len = (unsigned) n + 1;
		r = __armpi_vfs_call (&m);
	}
	if (r >= 0)
	{
		info[0] = m.b;
		info[1] = m.c;
		info[2] = m.d;
	}
	return r;
}

static void set_times (struct stat *st, long long t)
{
	st->st_mtime = (time_t) t;
	st->st_atime = (time_t) t;
	st->st_ctime = (time_t) t;
}

/* un chemin d'ARMPI -> 0, ou -1 et errno */
int __posix_path_stat (const char *path, struct stat *st)
{
	char abs[POSIX_PATH_MAX];
	if (__posix_abspath (path, abs, sizeof abs) != 0)
		return -1;
	size_t n = strlen (abs);
	if (n > ARMPI_PATH_MAX)
		return POSIX_ERR (ENAMETOOLONG);
	if (n >= 2 && abs[n - 1] == '/' && abs[n - 2] == ':')
	{
		/* la racine d'un volume : un repertoire s'il se liste */
		long h = __armpi_fsys (SYS_OPEN, (long) abs, ARMPI_O_READ, 0);
		if (h < 0)
			return POSIX_SYS (h);
		__armpi_fsys (SYS_CLOSE, h, 0, 0);
		fill (st, abs, 0, 1, 0);
		return 0;
	}
	long long info[3];
	long r = path_info (abs, info);
	if (r < 0)
		return POSIX_SYS (r);
	if (__posix_unlink_pending (abs))
		return POSIX_ERR (ENOENT);		/* (efface, encore ouvert ici) */
	fill (st, abs, info[0], (info[1] & ARMPI_ATTR_DIR) != 0, (info[1] & ARMPI_ATTR_EXEC) != 0);
	set_times (st, info[2]);
	return 0;
}

int __posix_path_is_dir (const char *path)
{
	struct stat st;
	return __posix_path_stat (path, &st) == 0 && S_ISDIR (st.st_mode);
}

int __posix_fstat (struct __posix_ofd *d, struct stat *st)
{
	memset (st, 0, sizeof *st);
	st->st_blksize = 512;
	st->st_nlink = 1;
	switch (d->type)
	{
	case POSIX_FD_FILE:
	{
		__posix_ofd_lock (d);
		long pos = __armpi_fsys (SYS_SEEK, d->h, 0, SEEK_CUR);
		long size = __armpi_fsys (SYS_SEEK, d->h, 0, SEEK_END);
		__armpi_fsys (SYS_SEEK, d->h, pos, SEEK_SET);
		long long info[3];
		int known = d->path && !d->unlinked && path_info (d->path, info) == 0;
		if (known && __posix_path_shared (d) && info[0] > size)
			size = (long) info[0];
		__posix_ofd_unlock (d);
		fill (st, d->path, size < 0 ? 0 : size, 0, known && (info[1] & ARMPI_ATTR_EXEC) != 0);
		if (known)
			set_times (st, info[2]);
		return 0;
	}
	case POSIX_FD_DIR:
		fill (st, d->path, 0, 1, 0);
		return 0;
	case POSIX_FD_PIPE_R:
	case POSIX_FD_PIPE_W:
		st->st_mode = S_IFIFO | 0600;
		st->st_ino = (ino_t) d->h;
		return 0;
	case POSIX_FD_SOCKET:
		st->st_mode = S_IFSOCK | 0600;
		st->st_ino = (ino_t) d->h + 0x10000;
		return 0;
	case POSIX_FD_LSOCK:
		st->st_mode = S_IFSOCK | 0600;
		st->st_ino = (ino_t) d->h + 0x20000;
		return 0;
	case POSIX_FD_SHM:
	{
		long size = __posix_handle_size (d->h);
		if (size < 0)
			return -1;
		st->st_mode = S_IFREG | 0600;
		st->st_size = size;
		st->st_blocks = (size + 511) / 512;
		st->st_ino = (ino_t) d->h + 0x30000;
		return 0;
	}
	default:
		st->st_mode = S_IFCHR | 0666;
		st->st_rdev = (dev_t) d->type;
		return 0;
	}
}

int stat (const char *path, struct stat *st)
{
	if (path == 0 || st == 0)
		return POSIX_ERR (EFAULT);
	if (*path == '\0')
		return POSIX_ERR (ENOENT);
	int dev = __posix_dev_type (path);
	if (dev)
	{
		memset (st, 0, sizeof *st);
		st->st_mode = S_IFCHR | 0666;
		st->st_rdev = (dev_t) dev;
		st->st_nlink = 1;
		return 0;
	}
	char p[POSIX_PATH_MAX];
	if (__posix_path (path, p, sizeof p) == 0)
		return -1;
	return __posix_path_stat (p, st);
}

int _stat (const char *path, struct stat *st) { return stat (path, st); }
int lstat (const char *path, struct stat *st) { return stat (path, st); }

int fstatat (int dirfd, const char *path, struct stat *st, int flags)
{
	if ((flags & AT_EMPTY_PATH) && path && *path == '\0')
		return fstat (dirfd, st);
	char p[POSIX_PATH_MAX];
	if (__posix_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return stat (p, st);
}

int access (const char *path, int mode)
{
	struct stat st;
	if (strcmp (path, "/proc") == 0)	/* PhysicsFS et SDL le regardent avant readlink ("/proc/self/exe") */
		return 0;
	if (stat (path, &st) != 0)
		return -1;
	/* (un seul utilisateur : lire et ecrire sont permis ; executer : le bit de vfs) */
	if ((mode & X_OK) && !S_ISDIR (st.st_mode) && !(st.st_mode & S_IXUSR))
		return POSIX_ERR (EACCES);
	return 0;
}

int faccessat (int dirfd, const char *path, int mode, int flags)
{
	(void) flags;
	char p[POSIX_PATH_MAX];
	if (__posix_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return access (p, mode);
}

int eaccess (const char *path, int mode) { return access (path, mode); }
int euidaccess (const char *path, int mode) { return access (path, mode); }

/* ---- effacer, creer ---- */
static int do_unlink (const char *path, int dir)
{
	char p[POSIX_PATH_MAX];
	if (__posix_path (path, p, sizeof p) == 0)
		return -1;
	struct stat st;
	if (__posix_path_stat (p, &st) != 0)
		return -1;
	if (dir == 0 && S_ISDIR (st.st_mode))
		return POSIX_ERR (EISDIR);
	if (dir == 1 && !S_ISDIR (st.st_mode))
		return POSIX_ERR (ENOTDIR);
	char abs[POSIX_PATH_MAX];
	if (!S_ISDIR (st.st_mode) && __posix_abspath (p, abs, sizeof abs) == 0 && __posix_unlink_defer (abs))
		return 0;			/* ouvert ici : efface a la derniere fermeture */
	return (int) POSIX_SYS (__armpi_fsys (SYS_UNLINK, (long) p, 0, 0));
}

int unlink (const char *path) { return do_unlink (path, 0); }
int _unlink (const char *path) { return unlink (path); }
int rmdir (const char *path) { return do_unlink (path, 1); }
int remove (const char *path) { return do_unlink (path, -1); }

struct _reent;
int _remove_r (struct _reent *r, const char *path) { (void) r; return remove (path); }

int unlinkat (int dirfd, const char *path, int flags)
{
	char p[POSIX_PATH_MAX];
	if (__posix_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return do_unlink (p, (flags & AT_REMOVEDIR) ? 1 : 0);
}

int mkdir (const char *path, mode_t mode)
{
	(void) mode;
	char p[POSIX_PATH_MAX];
	if (__posix_path (path, p, sizeof p) == 0)
		return -1;
	if (*p == '\0')
		return POSIX_ERR (ENOENT);
	struct stat st;
	if (__posix_path_stat (p, &st) == 0)
		return POSIX_ERR (EEXIST);
	return (int) POSIX_SYS (__armpi_fsys (SYS_MKDIR, (long) p, 0, 0));
}

int mkdirat (int dirfd, const char *path, mode_t mode)
{
	char p[POSIX_PATH_MAX];
	if (__posix_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return mkdir (p, mode);
}

/* ---- renommer : par le service de fichiers ; d'un volume a l'autre, un fichier est recopie ---- */
static int copy_file (const char *from, const char *to)
{
	long a = __armpi_fsys (SYS_OPEN, (long) from, ARMPI_O_READ, 0);
	if (a < 0)
		return POSIX_SYS (a);
	long b = __armpi_fsys (SYS_OPEN, (long) to, ARMPI_O_WRITE | ARMPI_O_CREATE | ARMPI_O_TRUNC, 0);
	if (b < 0)
	{
		__armpi_fsys (SYS_CLOSE, a, 0, 0);
		return POSIX_SYS (b);
	}
	char *buf = (char *) malloc (16384);
	long r = buf ? 0 : -12;
	while (r == 0)
	{
		long n = __armpi_fsys (SYS_READ, a, (long) buf, 16384);
		if (n <= 0)
		{
			r = n;
			break;
		}
		long w = __armpi_fsys (SYS_WRITE, b, (long) buf, n);
		if (w != n)
			r = w < 0 ? w : -28;
	}
	free (buf);
	__armpi_fsys (SYS_CLOSE, a, 0, 0);
	__armpi_fsys (SYS_CLOSE, b, 0, 0);
	return r < 0 ? POSIX_SYS (r) : 0;
}

static int do_rename (const char *from, const char *to)
{
	char a[POSIX_PATH_MAX], b[POSIX_PATH_MAX], pa[POSIX_PATH_MAX], pb[POSIX_PATH_MAX];
	if (__posix_path (from, pa, sizeof pa) == 0 || __posix_path (to, pb, sizeof pb) == 0)
		return -1;
	if (__posix_abspath (pa, a, sizeof a) != 0 || __posix_abspath (pb, b, sizeof b) != 0)
		return -1;
	struct stat sa, sb;
	if (__posix_path_stat (a, &sa) != 0)
		return -1;
	if (strcmp (a, b) == 0)
		return 0;
	struct __armpi_msg m;
	size_t la = strlen (a), lb = strlen (b);
	if (la + lb + 2 > sizeof m.data)
		return POSIX_ERR (ENAMETOOLONG);
	memset (&m, 0, sizeof m);
	m.op = V_RENAME;
	memcpy (m.data, a, la + 1);
	memcpy (m.data + la + 1, b, lb + 1);
	m.len = (unsigned) (la + lb + 2);
	long r = __armpi_vfs_call (&m);
	if (r != -18)				/* E_XDEV : volumes differents */
		return (int) POSIX_SYS (r);
	if (S_ISDIR (sa.st_mode))
		return POSIX_ERR (EXDEV);
	if (__posix_path_stat (b, &sb) == 0)
	{
		if (S_ISDIR (sb.st_mode))
			return POSIX_ERR (EISDIR);
		long e = __armpi_fsys (SYS_UNLINK, (long) b, 0, 0);
		if (e < 0)
			return POSIX_SYS (e);
	}
	if (copy_file (a, b) != 0)
		return -1;
	return (int) POSIX_SYS (__armpi_fsys (SYS_UNLINK, (long) a, 0, 0));
}

int rename (const char *from, const char *to) { return do_rename (from, to); }
int _rename_r (struct _reent *r, const char *from, const char *to) { (void) r; return do_rename (from, to); }

int renameat (int fd1, const char *from, int fd2, const char *to)
{
	char a[POSIX_PATH_MAX], b[POSIX_PATH_MAX];
	if (__posix_at_path (fd1, from, a, sizeof a) == 0 || __posix_at_path (fd2, to, b, sizeof b) == 0)
		return -1;
	return do_rename (a, b);
}

/* ---- liens : aucun sur les volumes d'ARMPI (FAT32, exFAT, RamFS) ---- */
int _link (const char *a, const char *b) { (void) a; (void) b; return POSIX_ERR (ENOSYS); }
int link (const char *a, const char *b) { return _link (a, b); }
int linkat (int f1, const char *a, int f2, const char *b, int fl) { (void) f1; (void) f2; (void) fl; return _link (a, b); }
int symlink (const char *a, const char *b) { (void) a; (void) b; return POSIX_ERR (ENOSYS); }
int symlinkat (const char *a, int f, const char *b) { (void) f; return symlink (a, b); }

ssize_t readlink (const char *path, char *buf, size_t n)
{
	if (strcmp (path, "/proc/self/exe") == 0)	/* (la seule entree de /proc : dl.c) */
	{
		const char *e = __posix_exe_path ();
		char px[POSIX_PATH_MAX];		/* (la forme POSIX : « /bin/python3 ») */
		if (__posix_path_to_posix (e, px, sizeof px) == 0)
			e = px;
		size_t l = strlen (e);
		if (l > n)
			l = n;
		memcpy (buf, e, l);
		return (ssize_t) l;
	}
	struct stat st;
	if (stat (path, &st) != 0)
		return -1;
	return POSIX_ERR (EINVAL);			/* (jamais un lien) */
}

ssize_t readlinkat (int dirfd, const char *path, char *buf, size_t n)
{
	char p[POSIX_PATH_MAX];
	if (__posix_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return readlink (p, buf, n);
}

/* ---- droits : le bit d'execution (V_CHMOD) ; proprietaires : aucun (0 sans effet) ---- */
static mode_t s_umask = 022;
mode_t umask (mode_t m) { mode_t o = s_umask; s_umask = m & 0777; return o; }
mode_t __posix_umask (void) { return s_umask; }

/* un chemin d'ARMPI -> 0, ou -1 et errno : un bit x l'ajoute a l'index des executables de vfs,
 * aucun l'en retire (un repertoire, un chemin des regles : rien) */
int __posix_chmod_path (const char *p, mode_t m)
{
	char abs[POSIX_PATH_MAX];
	if (__posix_abspath (p, abs, sizeof abs) != 0)
		return -1;
	struct __armpi_msg msg;
	size_t n = strlen (abs);
	if (n >= sizeof msg.data)
		return POSIX_ERR (ENAMETOOLONG);
	memset (&msg, 0, sizeof msg);
	msg.op = V_CHMOD;
	msg.a = m;
	strcpy ((char *) msg.data, abs);
	msg.len = (unsigned) n + 1;
	long r = __armpi_vfs_call (&msg);
	if (r == -22)					/* (vfs d'avant V_CHMOD : sans effet) */
	{
		struct stat st;
		return __posix_path_stat (p, &st);
	}
	return (int) POSIX_SYS (r);
}

int chmod (const char *path, mode_t m)
{
	if (path && __posix_dev_type (path))
		return 0;
	char p[POSIX_PATH_MAX];
	if (__posix_path (path, p, sizeof p) == 0)
		return -1;
	return __posix_chmod_path (p, m);
}

int fchmod (int fd, mode_t m)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	int r = d->path && (d->type == POSIX_FD_FILE || d->type == POSIX_FD_DIR) ? __posix_chmod_path (d->path, m) : 0;
	__posix_fd_put (d);
	return r;
}
int fchmodat (int dirfd, const char *path, mode_t m, int fl) { (void) fl; char p[POSIX_PATH_MAX]; return __posix_at_path (dirfd, path, p, sizeof p) ? chmod (p, m) : -1; }
int chown (const char *path, uid_t u, gid_t g) { (void) u; (void) g; struct stat st; return stat (path, &st); }
int lchown (const char *path, uid_t u, gid_t g) { return chown (path, u, g); }
int fchown (int fd, uid_t u, gid_t g) { (void) u; (void) g; struct stat st; return fstat (fd, &st); }
int fchownat (int dirfd, const char *path, uid_t u, gid_t g, int fl) { (void) fl; char p[POSIX_PATH_MAX]; return __posix_at_path (dirfd, path, p, sizeof p) ? chown (p, u, g) : -1; }

/* ---- dates : une seule par fichier, a la seconde (FAT : 2 s), celle de la derniere ecriture ---- */
static int set_mtime (const char *path, long long t)
{
	char abs[POSIX_PATH_MAX];
	if (__posix_abspath (path, abs, sizeof abs) != 0)
		return -1;
	struct __armpi_msg m;
	size_t n = strlen (abs);
	if (n >= sizeof m.data)
		return POSIX_ERR (ENAMETOOLONG);
	memset (&m, 0, sizeof m);
	m.op = V_UTIME;
	m.b = t;
	strcpy ((char *) m.data, abs);
	m.len = (unsigned) n + 1;
	return (int) POSIX_SYS (__armpi_vfs_call (&m));
}

static long long clock_now (void) { return __armpi_sys1 (SYS_CLOCK_GET, 0); }

/* la date d'un timespec[2] de utimensat ; -1 : UTIME_OMIT */
static long long ts_mtime (const struct timespec t[2])
{
	if (t == 0 || t[1].tv_nsec == UTIME_NOW)
		return clock_now ();
	return t[1].tv_nsec == UTIME_OMIT ? -1 : (long long) t[1].tv_sec;
}

int utime (const char *path, const struct utimbuf *t) { return set_mtime (path, t ? (long long) t->modtime : clock_now ()); }
int utimes (const char *path, const struct timeval t[2]) { return set_mtime (path, t ? (long long) t[1].tv_sec : clock_now ()); }
int utimensat (int dirfd, const char *path, const struct timespec t[2], int fl)
{
	(void) fl;
	char p[POSIX_PATH_MAX];
	if (!__posix_at_path (dirfd, path, p, sizeof p))
		return -1;
	long long m = ts_mtime (t);
	if (m < 0)
	{
		struct stat st;
		return stat (p, &st);
	}
	return set_mtime (p, m);
}

static int fd_mtime (int fd, long long m)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	int r = d->path ? set_mtime (d->path, m) : POSIX_ERR (EBADF);
	__posix_fd_put (d);
	return r;
}
int futimens (int fd, const struct timespec t[2])
{
	long long m = ts_mtime (t);
	if (m < 0)
	{
		struct stat st;
		return fstat (fd, &st);
	}
	return fd_mtime (fd, m);
}
int futimes (int fd, const struct timeval t[2]) { return fd_mtime (fd, t ? (long long) t[1].tv_sec : clock_now ()); }

/* ---- statvfs : des valeurs raisonnables (vfs ne donne pas la place libre) ---- */
static void vfs_fill (struct statvfs *s)
{
	memset (s, 0, sizeof *s);
	s->f_bsize = 4096;
	s->f_frsize = 4096;
	s->f_blocks = 1UL << 20;
	s->f_bfree = 1UL << 19;
	s->f_bavail = 1UL << 19;
	s->f_files = 65536;
	s->f_ffree = 32768;
	s->f_favail = 32768;
	s->f_namemax = 255;
}

int statvfs (const char *path, struct statvfs *s)
{
	struct stat st;
	if (stat (path, &st) != 0)
		return -1;
	vfs_fill (s);
	return 0;
}

int fstatvfs (int fd, struct statvfs *s)
{
	struct stat st;
	if (fstat (fd, &st) != 0)
		return -1;
	vfs_fill (s);
	return 0;
}

static void statfs_fill (struct statfs *s, const struct statvfs *v)
{
	memset (s, 0, sizeof *s);
	s->f_type = MSDOS_SUPER_MAGIC;
	s->f_bsize = (long) v->f_bsize;
	s->f_frsize = (long) v->f_frsize;
	s->f_blocks = v->f_blocks;
	s->f_bfree = v->f_bfree;
	s->f_bavail = v->f_bavail;
	s->f_files = v->f_files;
	s->f_ffree = v->f_ffree;
	s->f_namelen = (long) v->f_namemax;
}

int statfs (const char *path, struct statfs *s)
{
	struct statvfs v;
	if (statvfs (path, &v) != 0)
		return -1;
	statfs_fill (s, &v);
	return 0;
}

int fstatfs (int fd, struct statfs *s)
{
	struct statvfs v;
	if (fstatvfs (fd, &v) != 0)
		return -1;
	statfs_fill (s, &v);
	return 0;
}
