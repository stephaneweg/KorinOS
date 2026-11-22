/*
 * file.c -- open et les entrees / sorties sur une description (libposix, docs/POSIX.md ;
 * reecrit pour ARMPI d'apres file.c d'Onyx).
 *
 *  - Fichiers et repertoires : descripteurs du client de fichiers d'AppKit (file_sys :
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
#include <limits.h>
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
	/* cree avec un bit x (l'editeur de liens, install) : executable pour vfs */
	if (!exists && (flags & O_CREAT) && (mode & ~__posix_umask () & 0111))
		__posix_chmod_path (p, (mode_t) mode & ~__posix_umask ());
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
/* tubes en bloc (V_PIPE_*_BULK : vfs lit ou ecrit jusqu'a 64 Ko directement dans notre memoire) ; un vfs
 * ancien les refuse (E_INVAL) : 464 octets par message, comme avant (21/11/2026 : git, ses deux tubes de
 * 464 octets par aller et retour) */
static int s_pipe_bulk_off;

/* ---- anneaux partages (PC, 22/11/2026, docs/etude/ANNEAUX.md § 4) : apres RING_MIN octets passes par messages
 * sur une poignee, on demande a vfs l'anneau du tube (V_PIPE_RING : un tampon partage du noyau) ; ensuite lecture et
 * ecriture s'y font sans message, l'attente sur les futex de son en-tete (partages entre process : cle = adresse
 * physique). Un verrou par sens (plusieurs process tiennent souvent un meme bout : bash et ses fils) ; vfs rend
 * ceux d'un process mort. vfs refuse (Pi, plus d'anneau libre) : les messages, comme avant. */
#define RING_MIN	65536
#define RING_SLOTS	32
#define RING_WAIT_MS	100		/* (une attente se relit au moins ainsi : un reveil perdu ne bloque pas) */
#define RING_TRIES	4

struct pring
{
	long h;				/* poignee (0 : libre) */
	struct __armpi_pipe_ring *r;	/* l'en-tete, 0 : messages */
	long buf;
	unsigned long long moved;	/* octets passes par messages */
	int tries;
};
static struct pring s_pr[RING_SLOTS];
static volatile unsigned s_prLock;
static int s_ring_off;			/* vfs sans anneaux : 1 */
static unsigned s_ring_pid;		/* notre pid + 1 (les verrous des anneaux) */

void __posix_pipe_ring_fork_child (void)
{
	s_ring_pid = 0;			/* (le fils garde les anneaux mappes du pere : meme adresses) */
}

static inline void ring_relax (void)
{
#if defined(__aarch64__)
	__asm__ volatile ("yield" ::: "memory");
#else
	__asm__ volatile ("pause" ::: "memory");
#endif
}

static struct pring *pr_find (long h, int make)
{
	struct pring *f = 0;
	for (int i = 0; i < RING_SLOTS; i++)
	{
		if (s_pr[i].h == h)
			return &s_pr[i];
		if (!f && !s_pr[i].h)
			f = &s_pr[i];
	}
	if (make && f)
	{
		memset (f, 0, sizeof *f);
		f->h = h;
	}
	return make ? f : 0;
}

/* l'anneau de la poignee h, s'il y en a un (en demandant a vfs quand le flux le vaut) */
static struct __armpi_pipe_ring *ring_of (long h, size_t more)
{
	if (s_ring_off)
		return 0;
	struct __armpi_pipe_ring *r = 0;
	__posix_lock (&s_prLock);
	struct pring *p = pr_find (h, 1);
	if (p)
	{
		p->moved += more;
		r = p->r;
		if (!r && p->moved >= RING_MIN && p->tries < RING_TRIES)
		{
			p->tries++;
			struct __armpi_msg m;
			memset (&m, 0, 48);
			m.op = V_PIPE_RING;
			m.a = h;
			long b = __armpi_vfs_call (&m);
			if (b == -22)
				s_ring_off = 1;		/* (Pi, vfs ancien) */
			else if (b >= 0)
			{
				/* (SYS_BUF_GRANT ne vaut que pour le dernier process autorise : refus, on redemandera) */
				long va = __armpi_sys1 (SYS_BUF_MAP, b);
				struct __armpi_pipe_ring *x = (struct __armpi_pipe_ring *) va;
				if (va > 0 && x->magic == ARMPI_RING_MAGIC)
				{
					p->r = r = x;
					p->buf = b;
				}
				else
					p->moved = 0;
			}
			else
				p->moved = 0;		/* (plus d'anneau libre : plus tard) */
		}
	}
	__posix_unlock (&s_prLock);
	return r;
}

static unsigned ring_me (void)
{
	if (!s_ring_pid)
		s_ring_pid = (unsigned) __armpi_sys0 (SYS_GETPID) + 1;
	return s_ring_pid;
}

static void ring_lock (volatile unsigned *l)
{
	unsigned me = ring_me ();
	for (int k = 0;; k++)
	{
		unsigned z = 0;
		if (__atomic_compare_exchange_n (l, &z, me, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return;
		if (k < 256)
			ring_relax ();
		else
			__armpi_sys0 (SYS_YIELD);
	}
}

static void ring_unlock (volatile unsigned *l)
{
	__atomic_store_n (l, 0, __ATOMIC_RELEASE);
}

static void ring_wake (volatile unsigned *seq, volatile unsigned *waiters)
{
	__atomic_add_fetch (seq, 1, __ATOMIC_RELEASE);
	if (__atomic_load_n (waiters, __ATOMIC_ACQUIRE))
		__armpi_sys2 (SYS_FUTEX_WAKE, seq, 0x7FFFFFFF);
}

/* dort jusqu'a un changement de seq (RING_WAIT_MS au plus), si cond est encore vraie apres s'etre compte */
#define RING_SLEEP(seq, waiters, cond) do { \
	unsigned s_ = __atomic_load_n (&(seq), __ATOMIC_ACQUIRE); \
	__atomic_add_fetch (&(waiters), 1, __ATOMIC_ACQ_REL); \
	if (cond) \
		__armpi_sys3 (SYS_FUTEX_WAIT, &(seq), s_, RING_WAIT_MS); \
	__atomic_sub_fetch (&(waiters), 1, __ATOMIC_ACQ_REL); \
} while (0)

static inline unsigned ring_avail (struct __armpi_pipe_ring *r)
{
	return (__atomic_load_n (&r->head, __ATOMIC_ACQUIRE) - __atomic_load_n (&r->tail, __ATOMIC_ACQUIRE)) &
	       (ARMPI_RING_CAP - 1);
}

/* lecture sur l'anneau -> octets, 0 fin, -11 (nowait), -4 (signal) */
static long ring_read (struct __armpi_pipe_ring *r, void *buf, size_t n, int nowait)
{
	unsigned char *d = (unsigned char *) r + ARMPI_RING_HDR;
	for (;;)
	{
		if (ring_avail (r))
		{
			ring_lock (&r->rlock);
			unsigned t = __atomic_load_n (&r->tail, __ATOMIC_ACQUIRE), k = ring_avail (r);
			if (k > n)
				k = (unsigned) n;
			unsigned a = ARMPI_RING_CAP - t < k ? ARMPI_RING_CAP - t : k;
			memcpy (buf, d + t, a);
			memcpy ((unsigned char *) buf + a, d, k - a);
			__atomic_store_n (&r->tail, (t + k) & (ARMPI_RING_CAP - 1), __ATOMIC_RELEASE);
			ring_unlock (&r->rlock);
			if (k)
			{
				ring_wake (&r->rseq, &r->wwait);
				return k;
			}
			continue;
		}
		if (!__atomic_load_n (&r->writers, __ATOMIC_ACQUIRE))
		{
			if (ring_avail (r))	/* (ecrit juste avant la fin) */
				continue;
			return 0;
		}
		if (nowait)
			return -11;
		unsigned sigs = __posix_sig_intr;
		RING_SLEEP (r->wseq, r->rwait, !ring_avail (r) && __atomic_load_n (&r->writers, __ATOMIC_ACQUIRE));
		if (__posix_sig_intr != sigs)
			return -4;
	}
}

/* ecriture sur l'anneau -> octets (moins que n : nowait et plein), -11, -32 (plus de lecteur), -4 */
static long ring_write (long h, struct __armpi_pipe_ring *r, const void *buf, size_t n, int nowait)
{
	unsigned char *d = (unsigned char *) r + ARMPI_RING_HDR;
	size_t done = 0;
	/* (PIPE_BUF octets au plus : d'un seul morceau, POSIX) */
	unsigned need = n <= PIPE_BUF ? (unsigned) n : 1;
	while (done < n)
	{
		if (!__atomic_load_n (&r->readers, __ATOMIC_ACQUIRE))
			return done ? (long) done : -32;
		unsigned room = (ARMPI_RING_CAP - 1) - ring_avail (r);
		if (room >= need)
		{
			ring_lock (&r->wlock);
			unsigned hd = __atomic_load_n (&r->head, __ATOMIC_ACQUIRE);
			room = (ARMPI_RING_CAP - 1) - ring_avail (r);
			unsigned k = room;
			if (k > n - done)
				k = (unsigned) (n - done);
			if (k < need)
				k = 0;
			unsigned a = ARMPI_RING_CAP - hd < k ? ARMPI_RING_CAP - hd : k;
			memcpy (d + hd, (const unsigned char *) buf + done, a);
			memcpy (d, (const unsigned char *) buf + done + a, k - a);
			__atomic_store_n (&r->head, (hd + k) & (ARMPI_RING_CAP - 1), __ATOMIC_RELEASE);
			ring_unlock (&r->wlock);
			if (k)
			{
				done += k;
				ring_wake (&r->wseq, &r->rwait);
				if (__atomic_load_n (&r->vfs_wait, __ATOMIC_ACQUIRE))
				{
					struct __armpi_msg m;	/* (un lecteur par messages attend dans vfs) */
					memset (&m, 0, 48);
					m.op = V_PIPE_KICK;
					m.a = h;
					__armpi_vfs_call (&m);
				}
				continue;
			}
		}
		if (nowait)
			return done ? (long) done : -11;
		unsigned sigs = __posix_sig_intr;
		RING_SLEEP (r->rseq, r->wwait, (ARMPI_RING_CAP - 1) - ring_avail (r) < need &&
			    __atomic_load_n (&r->readers, __ATOMIC_ACQUIRE));
		if (__posix_sig_intr != sigs)
			return done ? (long) done : -4;
	}
	return (long) done;
}

/* poll d'un bout d'ecriture sur l'anneau -> place libre, -32 plus de lecteur ; -1 : pas d'anneau (demander a vfs) */
long __posix_pipe_room (long h)
{
	struct __armpi_pipe_ring *r = ring_of (h, 0);
	if (!r)
		return -1;
	if (!__atomic_load_n (&r->readers, __ATOMIC_ACQUIRE))
		return -32;
	return (long) ((ARMPI_RING_CAP - 1) - ring_avail (r));
}

/* la poignee est rendue : son anneau aussi */
static void ring_forget (long h)
{
	__posix_lock (&s_prLock);
	struct pring *p = pr_find (h, 0);
	if (p)
	{
		if (p->r)
			__armpi_sys1 (SYS_BUF_FREE, p->buf);
		memset (p, 0, sizeof *p);
	}
	__posix_unlock (&s_prLock);
}

long __posix_pipe_read (long h, void *buf, size_t n, int nowait)
{
	struct __armpi_pipe_ring *rg = ring_of (h, 0);
	if (rg)
		return ring_read (rg, buf, n, nowait);
	struct __armpi_msg m;
	int bulk = n > ARMPI_PIPE_CHUNK && !s_pipe_bulk_off;
	if (n > (bulk ? ARMPI_PIPE_BULK : ARMPI_PIPE_CHUNK))
		n = bulk ? ARMPI_PIPE_BULK : ARMPI_PIPE_CHUNK;
	for (;;)
	{
		memset (&m, 0, 48);
		m.op = bulk ? V_PIPE_READ_BULK : V_PIPE_READ;
		m.a = h;
		m.b = (long) n;
		m.c = nowait ? PIPE_NOWAIT : PIPE_SHORT;
		m.d = bulk ? (long) buf : 0;
		unsigned sigs = __posix_sig_intr;
		long r = __armpi_vfs_call (&m);
		if (bulk && r == -22)
		{
			s_pipe_bulk_off = 1;		/* (vfs ancien) */
			bulk = 0;
			if (n > ARMPI_PIPE_CHUNK)
				n = ARMPI_PIPE_CHUNK;
			continue;
		}
		if (r == -11 && !nowait)
		{
			/* (une attente de vfs dure 1 s au plus) : un signal livre entre-temps (sans
			 * SA_RESTART), EINTR */
			if (__posix_sig_intr != sigs)
				return -4;
			continue;
		}
		if (r > 0 && !bulk)
			memcpy (buf, m.data, (size_t) r);
		if (r > 0)
			ring_of (h, (size_t) r);	/* (compte ; l'anneau au prochain appel) */
		return r;
	}
}

int __posix_pipe_close (long h)
{
	ring_forget (h);
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
		struct __armpi_pipe_ring *rg = ring_of (d->h, 0);
		if (rg)
		{
			long r = ring_write (d->h, rg, (const char *) buf + done, n - done, (d->flags & O_NONBLOCK) != 0);
			if (r < 0)
				return done ? (ssize_t) done : POSIX_SYS (r);
			done += (size_t) r;
			if ((size_t) r == 0 || (d->flags & O_NONBLOCK))
				break;
			continue;
		}
		size_t c = n - done;
		int bulk = c > ARMPI_PIPE_CHUNK && !s_pipe_bulk_off;
		if (c > (bulk ? ARMPI_PIPE_BULK : ARMPI_PIPE_CHUNK))
			c = bulk ? ARMPI_PIPE_BULK : ARMPI_PIPE_CHUNK;
		memset (&m, 0, 48);
		m.op = bulk ? V_PIPE_WRITE_BULK : V_PIPE_WRITE;
		m.a = d->h;
		if (bulk)
		{
			m.b = (long) c;
			m.d = (long) ((const char *) buf + done);
		}
		else
		{
			m.len = (unsigned) c;
			memcpy (m.data, (const char *) buf + done, c);
		}
		long r = __armpi_vfs_call (&m);
		if (bulk && r == -22)
		{
			s_pipe_bulk_off = 1;		/* (vfs ancien : 464 octets par message) */
			continue;
		}
		if (r < 0)
		{
			if (done)
				break;
			return POSIX_SYS (r);	/* E_PIPE : plus de lecteur */
		}
		if (r > 0)
			ring_of (d->h, (size_t) r);	/* (compte : l'anneau au tour suivant si le flux le vaut) */
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

/* pread / pwrite d'AppKit (f_pread, f_pwrite) : la position explicite va au service, celle du
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
			if (!(r = __armpi_fsys (SYS_WRITE, d->h, (long) z, left > 512 ? 512 : left)))
				r = -28;		/* (ENOSPC ; 0 bouclait ici) */
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
		/* (une autre description de ce process tient la meme poignee : vfs la garde. Au demarrage apres
		 * exec, celles de 0-2 faites par tty.c sont remplacees par celles passees par l'exec, meme poignee) */
		if (!__posix_ofd_handle_shared (d))
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
	__posix_ofd_unlink (d);
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
