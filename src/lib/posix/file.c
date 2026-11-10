/*
 * file.c -- open et les entrees / sorties sur une description (libposix, docs/POSIX.md ;
 * reecrit pour ARMPI d'apres file.c d'Onyx).
 *
 *  - Fichiers et repertoires : descripteurs du client de fichiers d'AppKit (fichier_sys :
 *    SYS_OPEN, SYS_READ, SYS_WRITE, SYS_SEEK, SYS_CLOSE ; 64 fichiers ouverts au plus par
 *    process). O_EXCL : stat d'abord. ftruncate : relu, rouvert avec O_TRUNC, reecrit (pas
 *    d'operation de troncature dans vfs).
 *  - Entree, sortie, erreur standard (0, 1, 2) : celles d'AppKit (tube donne par le pere, sinon
 *    la console du noyau), ou un bout de pseudo-terminal de vterm (tty.c).
 *  - Tubes : les poignees de vfs directement (V_PIPE_READ / V_PIPE_WRITE par morceaux de 464
 *    octets, sans attente si O_NONBLOCK : EAGAIN).
 *  - Sockets : socket.c (NetKit). /dev/null, /dev/zero, /dev/urandom (SYS_GETRANDOM).
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
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include "posix_internal.h"

/* ---- fichiers d'AppKit ---- */
/* la taille d'un fichier ouvert : celle que voit son descripteur d'AppKit (ses propres
 * ecritures) ou celle de vfs (les ecritures d'autres descripteurs), la plus grande */
static long file_size (struct __posix_ofd *d)
{
	long pos = __armpi_fsys (SYS_SEEK, d->h, 0, SEEK_CUR);
	long size = __armpi_fsys (SYS_SEEK, d->h, 0, SEEK_END);
	__armpi_fsys (SYS_SEEK, d->h, pos, SEEK_SET);
	long long info[2];
	if (d->path && !d->unlinked && __posix_path_shared (d) &&
	    __armpi_fsys (SYS_STAT, (long) d->path, (long) info, 0) == 0 && info[0] > size)
		size = (long) info[0];
	return size;
}

static long ak_open (const char *path, int flags)
{
	int acc = flags & O_ACCMODE;
	long kf = acc == O_WRONLY ? ARMPI_O_WRITE : acc == O_RDWR ? ARMPI_O_READ | ARMPI_O_WRITE : ARMPI_O_READ;
	if (flags & O_CREAT) kf |= ARMPI_O_CREATE;
	if (flags & O_TRUNC) kf |= ARMPI_O_TRUNC;
	if (flags & O_APPEND) kf |= ARMPI_O_APPEND;
	return __armpi_fsys (SYS_OPEN, (long) path, kf, 0);
}

int __posix_open (const char *path, int flags, int mode)
{
	(void) mode;
	if (path == 0)
		return POSIX_ERR (EFAULT);
	int t = __posix_tty_open (path, flags);	/* /dev/tty, /dev/ptmx, /dev/pts/N (tty.c) */
	if (t != -2)
		return t;
	int dev = __posix_dev_type (path);
	if (dev != 0)
	{
		struct __posix_ofd *d = __posix_ofd_new (dev, flags & (O_ACCMODE | O_APPEND | O_NONBLOCK));
		if (d == 0)
			return -1;
		if (dev == POSIX_FD_CONSOLE)
			d->console = (flags & O_ACCMODE) == O_RDONLY ? 0 : 1;
		return __posix_fd_install (d, 0, (flags & O_CLOEXEC) != 0);
	}
	char p[POSIX_PATH_MAX];
	if (__posix_path (path, p, sizeof p) == 0)
		return -1;
	if (strlen (p) > ARMPI_PATH_MAX)
		return POSIX_ERR (ENAMETOOLONG);
	struct stat st;
	int exists = __posix_path_stat (p, &st) == 0;
	if (exists && (flags & O_CREAT) && (flags & O_EXCL))
		return POSIX_ERR (EEXIST);
	if (!exists && !(flags & O_CREAT))
		return POSIX_ERR (ENOENT);
	int isdir = exists && S_ISDIR (st.st_mode);
	if ((flags & O_DIRECTORY) && !isdir)
		return POSIX_ERR (ENOTDIR);
	if (isdir && (flags & O_ACCMODE) != O_RDONLY)
		return POSIX_ERR (EISDIR);
	long h = ak_open (p, isdir ? O_RDONLY : flags);
	if (h < 0)
		return POSIX_SYS (h);
	struct __posix_ofd *d = __posix_ofd_new (isdir ? POSIX_FD_DIR : POSIX_FD_FILE,
						 flags & (O_ACCMODE | O_APPEND | O_NONBLOCK));
	if (d == 0)
	{
		__armpi_fsys (SYS_CLOSE, h, 0, 0);
		return -1;
	}
	d->h = h;
	char abs[POSIX_PATH_MAX];
	d->path = strdup (__posix_abspath (p, abs, sizeof abs) == 0 ? abs : p);
	return __posix_fd_install (d, 0, (flags & O_CLOEXEC) != 0);
}

int openat (int dirfd, const char *path, int flags, ...)
{
	va_list ap;
	va_start (ap, flags);
	int mode = va_arg (ap, int);
	va_end (ap);
	char p[POSIX_PATH_MAX];
	if (__posix_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return __posix_open (p, flags, mode);
}

int creat (const char *path, mode_t mode) { return __posix_open (path, O_WRONLY | O_CREAT | O_TRUNC, (int) mode); }

/* ---- tubes de vfs ---- */
long __posix_pipe_read (long h, void *buf, size_t n, int nowait)
{
	struct __armpi_msg m;
	if (n > ARMPI_PIPE_CHUNK)
		n = ARMPI_PIPE_CHUNK;
	for (;;)
	{
		memset (&m, 0, 48);
		m.op = V_PIPE_READ;
		m.a = h;
		m.b = (long) n;
		m.c = nowait ? PIPE_NOWAIT : 0;
		long r = __armpi_vfs_call (&m);
		if (r == -11 && !nowait)
			continue;		/* (une attente de vfs dure 10 s au plus) */
		if (r > 0)
			memcpy (buf, m.data, (size_t) r);
		return r;
	}
}

int __posix_pipe_close (long h)
{
	struct __armpi_msg m;
	memset (&m, 0, 48);
	m.op = V_PIPE_CLOSE;
	m.a = h;
	return (int) __armpi_vfs_call (&m);
}

static ssize_t pipe_read (struct __posix_ofd *d, void *buf, size_t n)
{
	if (n == 0)
		return 0;
	__posix_ofd_lock (d);
	size_t got = __posix_carry_take (d, buf, n, 0);
	__posix_ofd_unlock (d);
	if (got)
		return (ssize_t) got;
	if (d->eof)
		return 0;
	long r = __posix_pipe_read (d->h, buf, n, (d->flags & O_NONBLOCK) != 0);
	if (r == 0)
		d->eof = 1;
	return r < 0 ? POSIX_SYS (r) : r;
}

static ssize_t pipe_write (struct __posix_ofd *d, const void *buf, size_t n)
{
	struct __armpi_msg m;
	size_t done = 0;
	while (done < n)
	{
		size_t c = n - done;
		if (c > ARMPI_PIPE_CHUNK)
			c = ARMPI_PIPE_CHUNK;
		memset (&m, 0, 48);
		m.op = V_PIPE_WRITE;
		m.a = d->h;
		m.len = (unsigned) c;
		memcpy (m.data, (const char *) buf + done, c);
		long r = __armpi_vfs_call (&m);
		if (r < 0)
		{
			if (done)
				break;
			return POSIX_SYS (r);	/* E_PIPE : plus de lecteur */
		}
		if (r == 0)
		{
			/* plein : sans attente, ce qui est passe (ou EAGAIN) ; sinon on reessaie */
			if (d->flags & O_NONBLOCK)
				return done ? (ssize_t) done : POSIX_ERR (EAGAIN);
			__armpi_sys1 (SYS_SLEEP, 1);
			continue;
		}
		done += (size_t) r;
	}
	return (ssize_t) done;
}

/* ---- pseudo-fichiers ---- */
static ssize_t random_fill (void *buf, size_t n)
{
	size_t done = 0;
	while (done < n)
	{
		size_t c = n - done > 65536 ? 65536 : n - done;
		long r = __armpi_sys2 (SYS_GETRANDOM, (char *) buf + done, c);
		if (r <= 0)
			return done ? (ssize_t) done : POSIX_SYS (r < 0 ? r : -5);
		done += (size_t) r;
	}
	return (ssize_t) n;
}

/* ---- lecture et ecriture ---- */
ssize_t __posix_read (struct __posix_ofd *d, void *buf, size_t n)
{
	if ((d->flags & O_ACCMODE) == O_WRONLY && d->type != POSIX_FD_CONSOLE && d->type != POSIX_FD_SOCKET)
		return POSIX_ERR (EBADF);
	switch (d->type)
	{
	case POSIX_FD_LSOCK:
	{
		struct iovec v = { buf, n };
		return __posix_lsock_recv (d, &v, 1, 0, 0, 0, 0);
	}
	case POSIX_FD_SHM:
		return POSIX_ERR (EINVAL);		/* (memoire partagee : la projeter) */
	case POSIX_FD_FILE:
		return POSIX_SYS (__armpi_fsys (SYS_READ, d->h, (long) buf, (long) n));
	case POSIX_FD_DIR:
		return POSIX_ERR (EISDIR);
	case POSIX_FD_PIPE_R:
		return pipe_read (d, buf, n);
	case POSIX_FD_PIPE_W:
		return POSIX_ERR (EBADF);
	case POSIX_FD_SOCKET:
		return __posix_sock_recv (d, buf, n, 0, 0);
	case POSIX_FD_NULL:
		return 0;
	case POSIX_FD_ZERO:
		memset (buf, 0, n);
		return (ssize_t) n;
	case POSIX_FD_RANDOM:
		return random_fill (buf, n);
	case POSIX_FD_CONSOLE:
		return POSIX_SYS (__armpi_fsys (SYS_READ, 0, (long) buf, (long) n));
	case POSIX_FD_TTY:
		return __posix_tty_read (d, buf, n);
	}
	return POSIX_ERR (EBADF);
}

ssize_t __posix_write (struct __posix_ofd *d, const void *buf, size_t n)
{
	if ((d->flags & O_ACCMODE) == O_RDONLY && d->type != POSIX_FD_CONSOLE && d->type != POSIX_FD_SOCKET &&
	    d->type != POSIX_FD_NULL && d->type != POSIX_FD_ZERO)
		return POSIX_ERR (EBADF);
	switch (d->type)
	{
	case POSIX_FD_LSOCK:
	{
		struct iovec v = { (void *) buf, n };
		return __posix_lsock_send (d, &v, 1, 0, 0, 0);
	}
	case POSIX_FD_SHM:
		return POSIX_ERR (EINVAL);
	case POSIX_FD_FILE:
	{
		__posix_ofd_lock (d);
		if (d->flags & O_APPEND)
			__armpi_fsys (SYS_SEEK, d->h, file_size (d), SEEK_SET);
		long r = __armpi_fsys (SYS_WRITE, d->h, (long) buf, (long) n);
		__posix_ofd_unlock (d);
		return POSIX_SYS (r);
	}
	case POSIX_FD_DIR:
		return POSIX_ERR (EISDIR);
	case POSIX_FD_PIPE_W:
		return pipe_write (d, buf, n);
	case POSIX_FD_PIPE_R:
		return POSIX_ERR (EBADF);
	case POSIX_FD_SOCKET:
		return __posix_sock_send (d, buf, n, 0, 0);
	case POSIX_FD_NULL:
	case POSIX_FD_ZERO:
	case POSIX_FD_RANDOM:
		return (ssize_t) n;
	case POSIX_FD_CONSOLE:
	{
		int which = d->console == 2 ? 2 : 1;
		return POSIX_SYS (__armpi_fsys (SYS_WRITE, which, (long) buf, (long) n));
	}
	case POSIX_FD_TTY:
		return __posix_tty_write (d, buf, n);
	}
	return POSIX_ERR (EBADF);
}

off_t __posix_lseek (struct __posix_ofd *d, off_t off, int whence)
{
	if (d->type == POSIX_FD_DIR)
		return 0;
	if (d->type != POSIX_FD_FILE)
		return d->type == POSIX_FD_NULL || d->type == POSIX_FD_ZERO ? 0 : POSIX_ERR (ESPIPE);
	if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
		return POSIX_ERR (EINVAL);
	__posix_ofd_lock (d);
	long r;
	if (whence == SEEK_END)
		r = __armpi_fsys (SYS_SEEK, d->h, file_size (d) + (long) off, SEEK_SET);
	else
		r = __armpi_fsys (SYS_SEEK, d->h, (long) off, whence);
	__posix_ofd_unlock (d);
	return (off_t) POSIX_SYS (r);
}

/* pread / pwrite d'AppKit (f_lire_a, f_ecrire_a) : la position explicite va au service, celle du
 * descripteur (partagee avec un fils apres fork) ne bouge pas ; sans eux : seek, transfert, seek */
typedef long (*rw_at_fn) (long fd, const void *buf, long n, long off);

ssize_t __posix_pread (struct __posix_ofd *d, void *buf, size_t n, off_t off)
{
	if (d->type != POSIX_FD_FILE)
		return d->type == POSIX_FD_DIR ? POSIX_ERR (EISDIR) : POSIX_ERR (ESPIPE);
	if (off < 0)
		return POSIX_ERR (EINVAL);
	rw_at_fn ra = (rw_at_fn) __armpi_appkit (AK_F_LIRE_A);
	if (ra)
		return POSIX_SYS (ra (d->h, buf, (long) n, (long) off));
	__posix_ofd_lock (d);
	long pos = __armpi_fsys (SYS_SEEK, d->h, 0, SEEK_CUR);
	__armpi_fsys (SYS_SEEK, d->h, (long) off, SEEK_SET);
	long r = __armpi_fsys (SYS_READ, d->h, (long) buf, (long) n);
	__armpi_fsys (SYS_SEEK, d->h, pos, SEEK_SET);
	__posix_ofd_unlock (d);
	return POSIX_SYS (r);
}

ssize_t __posix_pwrite (struct __posix_ofd *d, const void *buf, size_t n, off_t off)
{
	if (d->type != POSIX_FD_FILE)
		return d->type == POSIX_FD_DIR ? POSIX_ERR (EISDIR) : POSIX_ERR (ESPIPE);
	if (off < 0)
		return POSIX_ERR (EINVAL);
	__posix_ofd_lock (d);
	long size = file_size (d);
	rw_at_fn wa = (rw_at_fn) __armpi_appkit (AK_F_ECRIRE_A);
	if (wa)
	{
		static const char z[512];
		long r = 0;
		/* un trou : des zeros de la fin jusqu'a off (pas de fichiers creux) */
		for (long p = size; p < (long) off; p += r)
			if ((r = wa (d->h, z, (long) off - p > 512 ? 512 : (long) off - p, p)) <= 0)
			{
				r = r ? r : -28;		/* (ENOSPC) */
				break;
			}
		if (r >= 0)
			r = wa (d->h, buf, (long) n, (long) off);
		__posix_ofd_unlock (d);
		return POSIX_SYS (r);
	}
	long pos = __armpi_fsys (SYS_SEEK, d->h, 0, SEEK_CUR);
	long r = 0;
	if (off > size)
	{
		/* un trou : des zeros de la fin jusqu'a off (pas de fichiers creux) */
		static const char z[512];
		__armpi_fsys (SYS_SEEK, d->h, size, SEEK_SET);
		for (long left = off - size; left > 0 && r >= 0; left -= r)
			r = __armpi_fsys (SYS_WRITE, d->h, (long) z, left > 512 ? 512 : left);
	}
	if (r >= 0)
	{
		__armpi_fsys (SYS_SEEK, d->h, (long) off, SEEK_SET);
		r = __armpi_fsys (SYS_WRITE, d->h, (long) buf, (long) n);
	}
	__armpi_fsys (SYS_SEEK, d->h, pos, SEEK_SET);
	__posix_ofd_unlock (d);
	return POSIX_SYS (r);
}

int __posix_ofd_release (struct __posix_ofd *d)
{
	int r = 0;
	switch (d->type)
	{
	case POSIX_FD_FILE:
	case POSIX_FD_DIR:
		if (d->h >= 0)
			r = (int) POSIX_SYS (__armpi_fsys (SYS_CLOSE, d->h, 0, 0));
		if (d->unlinked && d->path)
			__posix_unlink_last_close (d->path);	/* (unlink d'un fichier ouvert : stat.c) */
		break;
	case POSIX_FD_PIPE_R:
	case POSIX_FD_PIPE_W:
		__posix_pipe_close (d->h);
		break;
	case POSIX_FD_SOCKET:
		r = __posix_sock_close (d);
		break;
	case POSIX_FD_LSOCK:
	case POSIX_FD_SHM:
		r = __posix_handle_close (d->h);
		break;
	case POSIX_FD_TTY:
		__posix_tty_release (d);
		break;
	}
	free (d->path);
	free (d->carry);
	free (d);
	return r;
}

/* ---- descripteurs : les appels POSIX ---- */
ssize_t pread (int fd, void *buf, size_t n, off_t off)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	ssize_t r = __posix_pread (d, buf, n, off);
	__posix_fd_put (d);
	return r;
}

ssize_t pwrite (int fd, const void *buf, size_t n, off_t off)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	ssize_t r = __posix_pwrite (d, buf, n, off);
	__posix_fd_put (d);
	return r;
}

static ssize_t vec_io (int fd, const struct iovec *iov, int cnt, off_t off, int wr)
{
	if (cnt < 0 || cnt > 1024)
		return POSIX_ERR (EINVAL);
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++)
	{
		if (iov[i].iov_len == 0)
			continue;
		ssize_t r;
		if (off < 0)
			r = wr ? __posix_write (d, iov[i].iov_base, iov[i].iov_len)
			       : __posix_read (d, iov[i].iov_base, iov[i].iov_len);
		else
			r = wr ? __posix_pwrite (d, iov[i].iov_base, iov[i].iov_len, off + total)
			       : __posix_pread (d, iov[i].iov_base, iov[i].iov_len, off + total);
		if (r < 0)
		{
			if (total == 0)
				total = -1;
			break;
		}
		total += r;
		if ((size_t) r < iov[i].iov_len)
			break;
	}
	__posix_fd_put (d);
	return total;
}

ssize_t readv (int fd, const struct iovec *iov, int cnt) { return vec_io (fd, iov, cnt, -1, 0); }
ssize_t writev (int fd, const struct iovec *iov, int cnt) { return vec_io (fd, iov, cnt, -1, 1); }
ssize_t preadv (int fd, const struct iovec *iov, int cnt, off_t off) { return off < 0 ? POSIX_ERR (EINVAL) : vec_io (fd, iov, cnt, off, 0); }
ssize_t pwritev (int fd, const struct iovec *iov, int cnt, off_t off) { return off < 0 ? POSIX_ERR (EINVAL) : vec_io (fd, iov, cnt, off, 1); }

/* ftruncate : vfs n'a pas d'operation de troncature : on relit ce qui reste, on rouvre avec
 * O_TRUNC, on le reecrit ; pour agrandir, des zeros a la fin. La position est gardee. */
static int truncate_ofd (struct __posix_ofd *d, off_t len)
{
	if (d->type == POSIX_FD_SHM)
		return __posix_shm_truncate (d, (long) len);	/* (memfd, shm_open : ipc.c) */
	if (d->type != POSIX_FD_FILE)
		return POSIX_ERR (d->type == POSIX_FD_DIR ? EISDIR : EINVAL);
	if (len < 0)
		return POSIX_ERR (EINVAL);
	if ((d->flags & O_ACCMODE) == O_RDONLY)
		return POSIX_ERR (EBADF);
	__posix_ofd_lock (d);
	long size = file_size (d);
	long pos = __armpi_fsys (SYS_SEEK, d->h, 0, SEEK_CUR);
	int r = 0;
	if (len > size)
	{
		__armpi_fsys (SYS_SEEK, d->h, size, SEEK_SET);
	{
		static const char z[512];
		long left = len - size;
		while (left > 0 && r == 0)
		{
			long w = __armpi_fsys (SYS_WRITE, d->h, (long) z, left > 512 ? 512 : left);
			if (w <= 0)
				r = POSIX_SYS (w < 0 ? w : -28);
			else
				left -= w;
		}
	}
	}
	else if (len < size)
	{
		char *keep = len ? (char *) malloc ((size_t) len) : 0;
		if (len && keep == 0)
			r = POSIX_ERR (ENOMEM);
		else
		{
			if (len)
			{
				__armpi_fsys (SYS_SEEK, d->h, 0, SEEK_SET);
				__armpi_fsys (SYS_READ, d->h, (long) keep, (long) len);
			}
			/* l'ancien descripteur d'abord ferme (sinon sa fermeture reecrirait l'ancienne
			   taille sur FAT32), puis rouvert vide */
			__armpi_fsys (SYS_CLOSE, d->h, 0, 0);
			long nh = d->path ? ak_open (d->path, O_RDWR | O_TRUNC) : -9;
			if (nh < 0)
			{
				r = POSIX_SYS (nh);
				nh = d->path ? ak_open (d->path, O_RDWR) : -9;
			}
			d->h = nh;
			if (r == 0 && len)
				__armpi_fsys (SYS_WRITE, d->h, (long) keep, (long) len);
			free (keep);
		}
	}
	__armpi_fsys (SYS_SEEK, d->h, pos, SEEK_SET);
	__posix_ofd_unlock (d);
	return r;
}

int ftruncate (int fd, off_t len)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	int r = truncate_ofd (d, len);
	__posix_fd_put (d);
	return r;
}

int truncate (const char *path, off_t len)
{
	int fd = __posix_open (path, O_WRONLY, 0);
	if (fd < 0)
		return -1;
	int r = ftruncate (fd, len);
	int e = errno;
	__posix_fd_close (fd);
	errno = e;
	return r;
}

int fsync (int fd)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	__posix_fd_put (d);
	return 0;				/* (les services de fichiers ecrivent tout de suite) */
}

int fdatasync (int fd) { return fsync (fd); }
void sync (void) { }

int posix_fallocate (int fd, off_t off, off_t len)
{
	if (off < 0 || len <= 0)
		return EINVAL;
	struct stat st;
	if (fstat (fd, &st) != 0)
		return errno;
	if (st.st_size >= off + len)
		return 0;
	return ftruncate (fd, off + len) == 0 ? 0 : errno;
}

int posix_fadvise (int fd, off_t off, off_t len, int advice)
{
	(void) off; (void) len; (void) advice;
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return EBADF;
	int r = d->type == POSIX_FD_PIPE_R || d->type == POSIX_FD_PIPE_W ? ESPIPE : 0;
	__posix_fd_put (d);
	return r;
}
