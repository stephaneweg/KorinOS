/*
 * fd.c -- la table des descripteurs (libposix, docs/POSIX.md ; repris d'Onyx).
 *
 * fds[0..1023] -> descriptions ouvertes comptees (struct __posix_ofd), chacune sur un objet
 * d'ARMPI : un descripteur d'AppKit (fichier, repertoire), un bout de tube de vfs, un socket de
 * NetKit, un pseudo-fichier de /dev, l'entree / la sortie / l'erreur standard. dup / dup2 / dup3 /
 * F_DUPFD partagent une description (donc sa position : AppKit garde la position par descripteur,
 * un descripteur d'AppKit par description) ; FD_CLOEXEC est par descripteur (garde ; pas d'exec).
 * Une description est rendue -- son objet ferme -- quand son dernier descripteur se ferme ET
 * qu'aucun appel ne s'en sert (__posix_fd_get prend une reference). pipe2 : un tube de vfs
 * (V_PIPE). unlink d'un fichier ouvert ici : differe a la derniere fermeture (chemin cache).
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
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "posix_internal.h"

static struct
{
	struct __posix_ofd *d;
	int cloexec;
} s_fd[POSIX_FD_MAX];
static volatile unsigned s_fdLock;
static int s_inited;

void __posix_ofd_lock (struct __posix_ofd *d) { __posix_lock (&d->lock); }
void __posix_fd_fork (int phase) { __posix_fork_lock (&s_fdLock, phase); }
void __posix_ofd_unlock (struct __posix_ofd *d) { __posix_unlock (&d->lock); }

struct __posix_ofd *__posix_ofd_new (int type, int flags)
{
	struct __posix_ofd *d = (struct __posix_ofd *) calloc (1, sizeof *d);
	if (d == 0)
	{
		errno = ENOMEM;
		return 0;
	}
	d->type = type;
	d->refs = 1;
	d->flags = flags;
	return d;
}

static struct __posix_ofd *console (int which)
{
	struct __posix_ofd *d = __posix_ofd_new (POSIX_FD_CONSOLE, which == 0 ? O_RDONLY : O_WRONLY);
	if (d)
		d->console = which;
	return d;
}

void __posix_fd_init (void)
{
	if (s_inited)
		return;
	s_inited = 1;
	for (int i = 0; i < 3; i++)
		if (s_fd[i].d == 0)
		{
			s_fd[i].d = __posix_std_ofd (i);	/* un bout de pty (tty.c), sinon AppKit */
			if (s_fd[i].d == 0)
				s_fd[i].d = console (i);
		}
	__posix_inherit_init ();		/* canaux et memoire partagee donnes par le pere (ipc.c) */
}

/* posix_spawn (proc.c) : la description du descripteur fd (une reference de plus) et son
 * FD_CLOEXEC ; 0 si fd est libre */
struct __posix_ofd *__posix_fd_peek (int fd, int *cloexec)
{
	if (fd < 0 || fd >= POSIX_FD_MAX)
		return 0;
	__posix_lock (&s_fdLock);
	struct __posix_ofd *d = s_fd[fd].d;
	if (d)
	{
		__atomic_add_fetch (&d->refs, 1, __ATOMIC_ACQ_REL);
		*cloexec = s_fd[fd].cloexec;
	}
	__posix_unlock (&s_fdLock);
	return d;
}

void (*__posix_fd_closing) (int fd);

/* d (its reference moves in) at descriptor fd, replacing what was there -> fd, or -1 (EBADF). */
int __posix_fd_place (struct __posix_ofd *d, int fd, int cloexec)
{
	if (fd < 0 || fd >= POSIX_FD_MAX)
	{
		__posix_fd_put (d);
		return POSIX_ERR (EBADF);
	}
	if (__posix_fd_closing)
		__posix_fd_closing (fd);
	__posix_lock (&s_fdLock);
	struct __posix_ofd *old = s_fd[fd].d;
	s_fd[fd].d = d;
	s_fd[fd].cloexec = cloexec;
	__posix_unlock (&s_fdLock);
	if (old)
		__posix_fd_put (old);
	return fd;
}


struct __posix_ofd *__posix_fd_get (int fd)
{
	if (!s_inited)
		__posix_fd_init ();
	if (fd < 0 || fd >= POSIX_FD_MAX)
	{
		errno = EBADF;
		return 0;
	}
	__posix_lock (&s_fdLock);
	struct __posix_ofd *d = s_fd[fd].d;
	if (d)
		__atomic_add_fetch (&d->refs, 1, __ATOMIC_ACQ_REL);
	__posix_unlock (&s_fdLock);
	if (d == 0)
		errno = EBADF;
	return d;
}

void __posix_fd_put (struct __posix_ofd *d)
{
	if (d && __atomic_sub_fetch (&d->refs, 1, __ATOMIC_ACQ_REL) == 0)
		__posix_ofd_release (d);
}

int __posix_fd_install (struct __posix_ofd *d, int min, int cloexec)
{
	if (!s_inited)
		__posix_fd_init ();
	if (min < 0)
		min = 0;
	__posix_lock (&s_fdLock);
	for (int i = min; i < POSIX_FD_MAX; i++)
		if (s_fd[i].d == 0)
		{
			s_fd[i].d = d;
			s_fd[i].cloexec = cloexec;
			__posix_unlock (&s_fdLock);
			return i;
		}
	__posix_unlock (&s_fdLock);
	__posix_fd_put (d);
	errno = EMFILE;
	return -1;
}

int __posix_fd_close (int fd)
{
	if (!s_inited)
		__posix_fd_init ();
	if (fd < 0 || fd >= POSIX_FD_MAX)
		return POSIX_ERR (EBADF);
	if (__posix_fd_closing)		/* (avant de rendre le numero : un autre thread peut le reprendre) */
		__posix_fd_closing (fd);
	__posix_lock (&s_fdLock);
	struct __posix_ofd *d = s_fd[fd].d;
	s_fd[fd].d = 0;
	s_fd[fd].cloexec = 0;
	__posix_unlock (&s_fdLock);
	if (d == 0)
		return POSIX_ERR (EBADF);
	int r = 0;
	if (__atomic_sub_fetch (&d->refs, 1, __ATOMIC_ACQ_REL) == 0)
		r = __posix_ofd_release (d);
	return r;
}

int __posix_fd_cloexec (int fd, int set)
{
	if (fd < 0 || fd >= POSIX_FD_MAX)
		return POSIX_ERR (EBADF);
	__posix_lock (&s_fdLock);
	int r = -1;
	if (s_fd[fd].d)
	{
		r = s_fd[fd].cloexec;
		if (set >= 0)
			s_fd[fd].cloexec = set;
	}
	__posix_unlock (&s_fdLock);
	if (r < 0)
		errno = EBADF;
	return r;
}


/* un autre descripteur de ce process a-t-il le meme fichier ouvert ? (file.c : sa taille vient
 * alors aussi de vfs, les ecritures de l'autre n'etant pas vues par le descripteur d'AppKit) */
int __posix_path_shared (struct __posix_ofd *me)
{
	int r = 0;
	__posix_lock (&s_fdLock);
	for (int i = 0; i < POSIX_FD_MAX && !r; i++)
	{
		struct __posix_ofd *d = s_fd[i].d;
		if (d && d != me && d->type == POSIX_FD_FILE && d->path && me->path && strcasecmp (d->path, me->path) == 0)
			r = 1;
	}
	__posix_unlock (&s_fdLock);
	return r;
}

/* ---- unlink d'un fichier ouvert ---- */
#define UNLINK_MAX	16
static char *s_unlinked[UNLINK_MAX];

int __posix_unlink_defer (const char *abs)
{
	int n = 0;
	__posix_lock (&s_fdLock);
	for (int i = 0; i < POSIX_FD_MAX; i++)
	{
		struct __posix_ofd *d = s_fd[i].d;
		if (d && d->type == POSIX_FD_FILE && d->path && strcasecmp (d->path, abs) == 0)
		{
			d->unlinked = 1;
			n++;
		}
	}
	if (n)
	{
		int k;
		for (k = 0; k < UNLINK_MAX; k++)
			if (s_unlinked[k] && strcasecmp (s_unlinked[k], abs) == 0)
				break;
		if (k == UNLINK_MAX)
			for (k = 0; k < UNLINK_MAX; k++)
				if (s_unlinked[k] == 0)
				{
					s_unlinked[k] = strdup (abs);
					break;
				}
		if (k == UNLINK_MAX)
			n = 0;				/* (liste pleine : efface tout de suite) */
	}
	__posix_unlock (&s_fdLock);
	return n != 0;
}

int __posix_unlink_pending (const char *abs)
{
	int r = 0;
	__posix_lock (&s_fdLock);
	for (int k = 0; k < UNLINK_MAX; k++)
		if (s_unlinked[k] && strcasecmp (s_unlinked[k], abs) == 0)
			r = 1;
	__posix_unlock (&s_fdLock);
	return r;
}

void __posix_unlink_last_close (const char *abs)
{
	int still = 0;
	__posix_lock (&s_fdLock);
	for (int i = 0; i < POSIX_FD_MAX; i++)
	{
		struct __posix_ofd *d = s_fd[i].d;
		if (d && d->type == POSIX_FD_FILE && d->unlinked && d->path && strcasecmp (d->path, abs) == 0)
			still = 1;
	}
	int k = -1;
	if (!still)
		for (k = 0; k < UNLINK_MAX; k++)
			if (s_unlinked[k] && strcasecmp (s_unlinked[k], abs) == 0)
				break;
	char *path = 0;
	if (k >= 0 && k < UNLINK_MAX)
	{
		path = s_unlinked[k];
		s_unlinked[k] = 0;
	}
	__posix_unlock (&s_fdLock);
	if (path)
	{
		__armpi_fsys (SYS_UNLINK, (long) path, 0, 0);
		free (path);
	}
}

/* ---- dup ---- */
int dup (int fd)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	return __posix_fd_install (d, 0, 0);		/* (the reference moves to the new fd) */
}

int dup3 (int fd, int fd2, int flags)
{
	if (fd2 < 0 || fd2 >= POSIX_FD_MAX)
		return POSIX_ERR (EBADF);
	if (fd == fd2)
		return POSIX_ERR (EINVAL);
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	__posix_lock (&s_fdLock);
	struct __posix_ofd *old = s_fd[fd2].d;
	s_fd[fd2].d = d;
	s_fd[fd2].cloexec = (flags & O_CLOEXEC) != 0;
	__posix_unlock (&s_fdLock);
	if (old)
		__posix_fd_put (old);
	return fd2;
}

int dup2 (int fd, int fd2)
{
	if (fd == fd2)
	{
		struct __posix_ofd *d = __posix_fd_get (fd);
		if (d == 0)
			return -1;
		__posix_fd_put (d);
		return fd2;
	}
	return dup3 (fd, fd2, 0);
}

/* ---- fcntl ---- */
int fcntl (int fd, int cmd, ...)
{
	va_list ap;
	va_start (ap, cmd);
	long arg = va_arg (ap, long);
	va_end (ap);

	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	switch (cmd)
	{
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
		__atomic_add_fetch (&d->refs, 1, __ATOMIC_ACQ_REL);
		r = __posix_fd_install (d, (int) arg, cmd == F_DUPFD_CLOEXEC);
		break;
	case F_GETFD:
		r = __posix_fd_cloexec (fd, -1) > 0 ? FD_CLOEXEC : 0;
		break;
	case F_SETFD:
		__posix_fd_cloexec (fd, (arg & FD_CLOEXEC) != 0);
		break;
	case F_GETFL:
		r = d->flags;
		break;
	case F_SETFL:
	{
		/* O_NONBLOCK : garde dans la description (tubes et sockets le lisent a chaque appel) */
		int keep = d->flags & ~(O_APPEND | O_NONBLOCK);
		d->flags = keep | (int) (arg & (O_APPEND | O_NONBLOCK));
		break;
	}
	case F_ADD_SEALS:				/* memfd_create (ipc.c) */
	case F_GET_SEALS:
		r = d->type == POSIX_FD_SHM ? __posix_shm_seals (d, cmd == F_ADD_SEALS, (int) arg) : POSIX_ERR (EINVAL);
		break;
	case F_GETLK:
	{
		struct flock *fl = (struct flock *) arg;
		if (fl)
			fl->l_type = F_UNLCK;		/* no lock manager: nothing ever conflicts */
		break;
	}
	case F_SETLK:
	case F_SETLKW:
		break;
	case F_GETOWN:
	case F_SETOWN:
		break;
	default:
		r = POSIX_ERR (EINVAL);
	}
	__posix_fd_put (d);
	return r;
}

int _fcntl (int fd, int cmd, int arg) { return fcntl (fd, cmd, (long) arg); }

int flock (int fd, int op)
{
	(void) op;
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	__posix_fd_put (d);
	return 0;
}

int lockf (int fd, int cmd, off_t len)
{
	(void) cmd; (void) len;
	return flock (fd, 0);
}

int getdtablesize (void) { return POSIX_FD_MAX; }

/* ---- pipes ---- */
int pipe2 (int fds[2], int flags)
{
	/* un tube de vfs (V_PIPE) : deux poignees, un bout de lecture, un d'ecriture */
	struct __armpi_msg m;
	memset (&m, 0, 48);
	m.op = V_PIPE;
	long e = __armpi_vfs_call (&m);
	if (e < 0)
		return POSIX_SYS (e);
	struct __posix_ofd *r = __posix_ofd_new (POSIX_FD_PIPE_R, O_RDONLY | (flags & O_NONBLOCK));
	struct __posix_ofd *w = __posix_ofd_new (POSIX_FD_PIPE_W, O_WRONLY | (flags & O_NONBLOCK));
	if (r == 0 || w == 0)
	{
		free (r);
		free (w);
		__posix_pipe_close (m.a);
		__posix_pipe_close (m.b);
		return POSIX_ERR (ENOMEM);
	}
	r->h = m.a;
	w->h = m.b;
	int a = __posix_fd_install (r, 0, (flags & O_CLOEXEC) != 0);
	if (a < 0)
	{
		__posix_fd_put (w);
		return -1;
	}
	int b = __posix_fd_install (w, 0, (flags & O_CLOEXEC) != 0);
	if (b < 0)
	{
		__posix_fd_close (a);
		return -1;
	}
	fds[0] = a;
	fds[1] = b;
	return 0;
}

int pipe (int fds[2]) { return pipe2 (fds, 0); }

int mkfifo (const char *path, mode_t mode) { (void) path; (void) mode; return POSIX_ERR (ENOSYS); }
int mknod (const char *path, mode_t mode, dev_t dev) { (void) path; (void) mode; (void) dev; return POSIX_ERR (EPERM); }
