/*
 * ipc.c -- IPC POSIX entre process (libposix, docs/POSIX.md ; noms d'ipc.c d'Onyx) sur les appels
 * d'IPC du noyau d'ARMPI (docs/ABI.md 0x70-0x7D, src/kernel/ipc.asm) :
 *
 *  - socketpair (AF_UNIX, SOCK_STREAM / SOCK_SEQPACKET / SOCK_DGRAM [| SOCK_NONBLOCK | SOCK_CLOEXEC]) :
 *    un canal du noyau (CHAN_CREATE), deux poignees -> deux descripteurs POSIX_FD_LSOCK ;
 *    send / recv / sendmsg / recvmsg / read / write (CHAN_SEND, CHAN_RECV), shutdown, getsockopt
 *    (SO_TYPE, SO_DOMAIN, SO_PEERCRED : le pid du createur de la paire), poll / select (HANDLE_WAIT :
 *    une vraie attente dans le noyau) ; pas de socket AF_UNIX nomme (socket : EAFNOSUPPORT) ;
 *  - SCM_RIGHTS : les descripteurs LSOCK et SHM passent dans un message (leurs poignees ; le
 *    receveur les recoit dans sa table) ; un autre descripteur (fichier, tube de vfs, socket de
 *    NetKit) : EOPNOTSUPP ;
 *  - memfd_create, shm_open / shm_unlink (noms globaux du noyau), ftruncate, fstat, F_ADD_SEALS /
 *    F_GET_SEALS, mmap MAP_SHARED (SHM_MAP : les memes pages dans chaque process) ou MAP_PRIVATE
 *    (une copie) ; read / write sur un objet partage : EINVAL (le projeter) ;
 *  - posix_spawn (proc.c) : les descripteurs LSOCK / SHM sans FD_CLOEXEC passent au fils a leur
 *    numero (HANDLE_INHERIT, puis HANDLE_LIST au demarrage du fils : __posix_inherit_init).
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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "posix_internal.h"

/* ---- le noyau (src/inc/syscalls.inc : IPC_*, IPCM_*, IPCI_*) ---- */
#define K_KIND_CHAN	1
#define K_KIND_SHM	2
#define K_NOWAIT	1
#define K_PEEK		2
#define K_TRUNC		1
#define K_CTRUNC	2
#define K_SHUT_RD	1
#define K_SHUT_WR	2
#define K_POLLIN	1
#define K_POLLOUT	4
#define K_POLLERR	8
#define K_POLLHUP	16
#define K_POLLNVAL	32
#define K_MAX_IOV	64
#define K_MAX_HANDLES	256
#define K_WAIT_MAX	64
#define K_SHM_SEALABLE	1
#define K_SHM_CREAT	1
#define K_SHM_EXCL	2
#define K_SHM_TRUNC	4
#define K_SHM_WEAK	8
#define K_SHM_SET_SIZE	1
#define K_SHM_ADD_SEALS	2
#define K_SHM_PIN	3
#define K_SHM_UNPIN	4
#define K_SHM_PINNED	2
#define K_E_NOSYS	(-38)

struct kmsg				/* IPCM_* */
{
	const struct iovec *iov;
	unsigned iovcnt, nh;
	unsigned *handles;		/* {poignee, sorte | type << 8} */
	unsigned flags, pad;
};

struct kinfo				/* IPCI_* (64 octets) */
{
	unsigned kind, type;
	long long size;
	unsigned state, flags;
	long long pid;
	long long cap;
	long long pad[3];
};

static int handle_info (long h, struct kinfo *i)
{
	long r = __armpi_sys2 (SYS_HANDLE_INFO, h, i);
	return r < 0 ? (int) POSIX_SYS (r) : 0;
}

int __posix_handle_close (long h)
{
	return (int) POSIX_SYS (__armpi_sys1 (SYS_HANDLE_CLOSE, h));
}

long __posix_handle_size (long h)
{
	struct kinfo i;
	if (handle_info (h, &i) != 0)
		return -1;
	return (long) i.size;
}

/* une poignee recue (sorte | type << 8) -> une description */
static struct __posix_ofd *ofd_of_handle (long h, unsigned info)
{
	struct __posix_ofd *d;
	if ((info & 0xFF) == K_KIND_CHAN)
	{
		d = __posix_ofd_new (POSIX_FD_LSOCK, O_RDWR);
		if (d)
		{
			d->sotype = (int) (info >> 8);
			d->connected = 1;
		}
	}
	else
		d = __posix_ofd_new (POSIX_FD_SHM, O_RDWR);
	if (d == 0)
	{
		__armpi_sys1 (SYS_HANDLE_CLOSE, h);
		return 0;
	}
	d->h = h;
	return d;
}

/* ---- socketpair ---- */
int socketpair (int domain, int type, int proto, int sv[2])
{
	(void) proto;
	int flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (domain != AF_UNIX)
		return POSIX_ERR (EAFNOSUPPORT);
	if (type != SOCK_STREAM && type != SOCK_SEQPACKET && type != SOCK_DGRAM)
		return POSIX_ERR (EPROTONOSUPPORT);
	unsigned h[2];
	long r = __armpi_sys2 (SYS_CHAN_CREATE, type, h);
	if (r == K_E_NOSYS)
		return POSIX_ERR (EOPNOTSUPP);		/* (un noyau sans IPC locale) */
	if (r < 0)
		return (int) POSIX_SYS (r);
	int fd[2] = { -1, -1 };
	for (int i = 0; i < 2; i++)
	{
		struct __posix_ofd *d = ofd_of_handle (h[i], K_KIND_CHAN | ((unsigned) type << 8));
		if (d == 0)
		{
			if (i == 0)
				__armpi_sys1 (SYS_HANDLE_CLOSE, h[1]);
			else
				close (fd[0]);
			return POSIX_ERR (ENOMEM);
		}
		if (flags & SOCK_NONBLOCK)
			d->flags |= O_NONBLOCK;
		fd[i] = __posix_fd_install (d, 0, (flags & SOCK_CLOEXEC) != 0);
		if (fd[i] < 0)
		{
			if (i == 1)
				close (fd[0]);
			else
				__armpi_sys1 (SYS_HANDLE_CLOSE, h[1]);
			return -1;
		}
	}
	sv[0] = fd[0];
	sv[1] = fd[1];
	return 0;
}

/* ---- envoi, reception ---- */
static unsigned wait_flags (struct __posix_ofd *d, int flags)
{
	return ((d->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT)) ? K_NOWAIT : 0;
}

/* errno d'un envoi : EPIPE (pas de SIGPIPE), EAGAIN... */
ssize_t __posix_lsock_send (struct __posix_ofd *d, const struct iovec *iov, int n, const int *fds, int nfds, int flags)
{
	if (n < 0 || n > K_MAX_IOV || nfds < 0 || nfds > K_MAX_HANDLES)
		return POSIX_ERR (n > K_MAX_IOV ? EMSGSIZE : EINVAL);
	unsigned hv[K_MAX_HANDLES * 2];
	for (int i = 0; i < nfds; i++)
	{
		struct __posix_ofd *x = __posix_fd_get (fds[i]);
		if (x == 0)
			return -1;			/* EBADF */
		int ok = x->type == POSIX_FD_LSOCK || x->type == POSIX_FD_SHM;
		hv[i * 2] = (unsigned) x->h;		/* (la description reste ouverte pendant
							   l'envoi : le descripteur est a nous) */
		hv[i * 2 + 1] = 0;
		__posix_fd_put (x);
		if (!ok)
			return POSIX_ERR (EOPNOTSUPP);	/* (seuls les objets du noyau passent) */
	}
	struct kmsg m = { iov, (unsigned) n, (unsigned) nfds, hv, 0, 0 };
	long r = __armpi_sys6 (SYS_CHAN_SEND, d->h, &m, wait_flags (d, flags), d->sndtimeo, 0, 0);
	return POSIX_SYS (r);
}

ssize_t __posix_lsock_recv (struct __posix_ofd *d, const struct iovec *iov, int n, int *fds, int *nfds, int *mflags, int flags)
{
	if (n < 0 || n > K_MAX_IOV)
		return POSIX_ERR (EMSGSIZE);
	int room = nfds ? *nfds : 0;
	if (room > K_MAX_HANDLES)
		room = K_MAX_HANDLES;
	unsigned hv[K_MAX_HANDLES * 2];
	struct kmsg m = { iov, (unsigned) n, (unsigned) room, hv, 0, 0 };
	unsigned kf = wait_flags (d, flags) | ((flags & MSG_PEEK) ? K_PEEK : 0);
	long r = __armpi_sys6 (SYS_CHAN_RECV, d->h, &m, kf, d->rcvtimeo, 0, 0);
	int mf = 0, got = 0;
	if (r >= 0)
	{
		if (m.flags & K_TRUNC)
			mf |= MSG_TRUNC;
		if (m.flags & K_CTRUNC)
			mf |= MSG_CTRUNC;
		for (unsigned i = 0; i < m.nh; i++)
		{
			struct __posix_ofd *x = ofd_of_handle (hv[i * 2], hv[i * 2 + 1]);
			int fd = x ? __posix_fd_install (x, 0, (flags & MSG_CMSG_CLOEXEC) != 0) : -1;
			if (fd < 0)
			{
				mf |= MSG_CTRUNC;	/* (la poignee est fermee) */
				continue;
			}
			fds[got++] = fd;
		}
	}
	if (nfds)
		*nfds = got;
	if (mflags)
		*mflags = mf;
	return POSIX_SYS (r);
}

int __posix_lsock_shutdown (struct __posix_ofd *d, int how)
{
	int k = how == SHUT_RD ? K_SHUT_RD : how == SHUT_WR ? K_SHUT_WR : how == SHUT_RDWR ? K_SHUT_RD | K_SHUT_WR : 0;
	if (k == 0)
		return POSIX_ERR (EINVAL);
	return (int) POSIX_SYS (__armpi_sys2 (SYS_CHAN_SHUTDOWN, d->h, k));
}

static int put_int (void *v, unsigned *len, int x)
{
	if (v == 0 || len == 0 || *len < sizeof (int))
		return POSIX_ERR (EINVAL);
	memcpy (v, &x, sizeof x);
	*len = sizeof x;
	return 0;
}

int __posix_lsock_getsockopt (struct __posix_ofd *d, int level, int opt, void *v, unsigned *len)
{
	if (level != SOL_SOCKET)
		return put_int (v, len, 0);
	switch (opt)
	{
	case SO_TYPE: return put_int (v, len, d->sotype);
	case SO_DOMAIN: return put_int (v, len, AF_UNIX);
	case SO_PROTOCOL: return put_int (v, len, 0);
	case SO_ERROR: return put_int (v, len, 0);
	case SO_ACCEPTCONN: return put_int (v, len, 0);
	case SO_RCVBUF:
	case SO_SNDBUF:
	{
		struct kinfo i;
		int c = handle_info (d->h, &i) == 0 ? (int) i.cap : 65536;
		return put_int (v, len, c);
	}
	case SO_PEERCRED:
	{
		struct kinfo i;
		struct ucred c;
		if (v == 0 || len == 0 || *len < sizeof c)
			return POSIX_ERR (EINVAL);
		if (handle_info (d->h, &i) != 0)
			return -1;
		memset (&c, 0, sizeof c);
		c.pid = (pid_t) i.pid;
		memcpy (v, &c, sizeof c);
		*len = sizeof c;
		return 0;
	}
	case SO_RCVTIMEO:
	case SO_SNDTIMEO:
	{
		unsigned ms = opt == SO_RCVTIMEO ? d->rcvtimeo : d->sndtimeo;
		struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
		if (v == 0 || len == 0 || *len < sizeof tv)
			return POSIX_ERR (EINVAL);
		memcpy (v, &tv, sizeof tv);
		*len = sizeof tv;
		return 0;
	}
	}
	return put_int (v, len, 0);
}

int __posix_lsock_setsockopt (struct __posix_ofd *d, int level, int opt, const void *v, unsigned len)
{
	if (level == SOL_SOCKET && (opt == SO_RCVTIMEO || opt == SO_SNDTIMEO))
	{
		struct timeval tv;
		if (v == 0 || len < sizeof tv)
			return POSIX_ERR (EINVAL);
		memcpy (&tv, v, sizeof tv);
		unsigned ms = (unsigned) (tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000);
		if (opt == SO_RCVTIMEO)
			d->rcvtimeo = ms;
		else
			d->sndtimeo = ms;
	}
	return 0;				/* (les autres : acceptees sans effet) */
}

/* ---- poll sur des poignees : HANDLE_WAIT ---- */
int __posix_handle_poll (const long *h, const short *ev, short *rev, int n, int ms)
{
	struct { unsigned h; unsigned short ev, rev; } w[K_WAIT_MAX];
	if (n > K_WAIT_MAX)
		return POSIX_ERR (EINVAL);
	for (int i = 0; i < n; i++)
	{
		unsigned short k = 0;
		if (ev[i] & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI))
			k |= K_POLLIN;
		if (ev[i] & (POLLOUT | POLLWRNORM | POLLWRBAND))
			k |= K_POLLOUT;
		w[i].h = (unsigned) h[i];
		w[i].ev = k;
		w[i].rev = 0;
	}
	long r = __armpi_sys3 (SYS_HANDLE_WAIT, w, n, ms);
	if (r < 0)
		return (int) POSIX_SYS (r);
	for (int i = 0; i < n; i++)
	{
		short x = 0;
		unsigned short k = w[i].rev;
		if (k & K_POLLIN)
			x |= ev[i] & (POLLIN | POLLRDNORM);
		if (k & K_POLLOUT)
			x |= ev[i] & (POLLOUT | POLLWRNORM);
		if (k & K_POLLHUP)
			x |= POLLHUP;
		if (k & K_POLLERR)
			x |= POLLERR;
		if (k & K_POLLNVAL)
			x |= POLLNVAL;
		rev[i] = x;
	}
	return (int) r;
}

/* ---- memoire partagee ---- */
int memfd_create (const char *name, unsigned flags)
{
	(void) name;
	if (flags & ~(MFD_CLOEXEC | MFD_ALLOW_SEALING))
		return POSIX_ERR (EINVAL);
	long h = __armpi_sys2 (SYS_SHM_CREATE, 0, (flags & MFD_ALLOW_SEALING) ? K_SHM_SEALABLE : 0);
	if (h < 0)
		return (int) POSIX_SYS (h);
	struct __posix_ofd *d = ofd_of_handle (h, K_KIND_SHM);
	if (d == 0)
		return -1;
	return __posix_fd_install (d, 0, (flags & MFD_CLOEXEC) != 0);
}

int shm_open (const char *name, int flags, mode_t mode)
{
	(void) mode;
	if (name == 0)
		return POSIX_ERR (EINVAL);
	int k = ((flags & O_CREAT) ? K_SHM_CREAT : 0) | ((flags & O_EXCL) ? K_SHM_EXCL : 0) | ((flags & O_TRUNC) ? K_SHM_TRUNC : 0);
	long h = __armpi_sys2 (SYS_SHM_OPEN, name, k);
	if (h < 0)
		return (int) POSIX_SYS (h);
	struct __posix_ofd *d = ofd_of_handle (h, K_KIND_SHM);
	if (d == 0)
		return -1;
	d->flags = flags & O_ACCMODE;
	return __posix_fd_install (d, 0, 1);		/* (shm_open : FD_CLOEXEC) */
}

/* Un fichier projete en lecture : ses pages sont gardees dans un objet nomme d'apres lui (nom,
   taille, date), une seule fois pour tous les process ; chacun les voit en copie sur ecriture
   (VM_COW). L'objet a une page de plus : son mot dit que le contenu est complet. Le nom est faible :
   l'objet part avec le dernier process qui le projette, sauf s'il est epingle (file_pin). Un fichier
   change prend un autre nom. */
#define FILE_MIN	65536
#define FILE_MAX	(128L << 20)
#define FILE_READY	0x41524D5046494C45L
#define FILE_NAME	56
#define K_VM_FIXED	0x10
#define K_VM_NOREPLACE	0x100000
#define K_VM_COW	0x200000

/* (le dernier element du chemin, pas st_ino qui vient du chemin : /dotnet/x et SD0:/dotnet/x sont un
   seul fichier) */
static void file_name (char *name, const char *path, const struct stat *st)
{
	const char *b = strrchr (path, '/');
	unsigned long h = 0xCBF29CE484222325UL;		/* FNV-1a */
	for (b = b ? b + 1 : path; *b; b++)
		h = (h ^ (unsigned char) *b) * 0x100000001B3UL;
	snprintf (name, FILE_NAME, "/file-%lx-%lx-%llx", h, (long) st->st_size, (unsigned long long) st->st_mtime);
}

/* L'objet du fichier, complet -> poignee, -1 : pas d'objet. create : le faire s'il manque */
static long file_object (struct __posix_ofd *d, const struct stat *st, int create)
{
	char name[FILE_NAME];
	long size = st->st_size;
	unsigned long obj = ((unsigned long) size + 4095) & ~4095UL;
	file_name (name, d->path, st);
	long h = create ? __armpi_sys2 (SYS_SHM_OPEN, name, K_SHM_CREAT | K_SHM_EXCL | K_SHM_WEAK) : -1;
	if (h >= 0)
	{
		long p = -1;
		if (__armpi_sys3 (SYS_SHM_CTL, h, K_SHM_SET_SIZE, obj + 4096) == 0)
			p = __armpi_sys6 (SYS_SHM_MAP, h, 0, obj + 4096, PROT_READ | PROT_WRITE, 0, 0);
		long got = 0;
		if (p > 0 || p < -4095)
			while (got < size)
			{
				ssize_t r = __posix_pread (d, (char *) p + got, (size_t) (size - got), got);
				if (r <= 0)
					break;
				got += r;
			}
		if (got == size)
			__atomic_store_n ((long *) (p + (long) obj), FILE_READY, __ATOMIC_RELEASE);
		else
		{
			__armpi_sys1 (SYS_SHM_UNLINK, name);
			__armpi_sys1 (SYS_HANDLE_CLOSE, h);
			if (p > 0 || p < -4095)
				__armpi_sys2 (SYS_VM_UNMAP, p, obj + 4096);
			return -1;
		}
		__armpi_sys2 (SYS_VM_UNMAP, p, obj + 4096);
	}
	else
	{
		h = __armpi_sys2 (SYS_SHM_OPEN, name, 0);	/* un autre l'a fait, ou le fait */
		if (h < 0)
			return -1;
		long p = 0;
		int mapped = 0, ready = 0;
		for (int i = 0; i < 400 && !ready; i++)
		{
			struct kinfo k;
			/* pas avant que le createur lui donne sa taille : lire au-dela tuerait le process */
			if (!mapped && __armpi_sys2 (SYS_HANDLE_INFO, h, &k) == 0 && (unsigned long) k.size > obj)
			{
				p = __armpi_sys6 (SYS_SHM_MAP, h, 0, 4096, PROT_READ, 0, (long) obj);
				mapped = p > 0 || p < -4095;
			}
			ready = mapped && __atomic_load_n ((long *) p, __ATOMIC_ACQUIRE) == FILE_READY;
			if (!ready)
				usleep (5000);
		}
		if (mapped)
			__armpi_sys2 (SYS_VM_UNMAP, p, 4096);
		if (!ready)
		{
			__armpi_sys1 (SYS_HANDLE_CLOSE, h);
			return -1;
		}
	}
	return h;
}

/* -> MAP_FAILED sans errno : le chemin ordinaire, une copie privee. Hors des tailles, seul un fichier
   epingle est pris */
void *__posix_file_share (struct __posix_ofd *d, const struct stat *st, void *addr, unsigned long len,
	int prot, int flags, long off)
{
	long size = st->st_size;
	if (size == 0 || off + len > (((unsigned long) size + 4095) & ~4095UL))
		return MAP_FAILED;
	long h = file_object (d, st, size >= FILE_MIN && size <= FILE_MAX);
	if (h < 0)
		return MAP_FAILED;
	long r = __armpi_sys6 (SYS_SHM_MAP, h, addr, len, prot,
		K_VM_COW | ((flags & MAP_FIXED) ? K_VM_FIXED : 0) | ((flags & MAP_FIXED_NOREPLACE) ? K_VM_NOREPLACE : 0), off);
	__armpi_sys1 (SYS_HANDLE_CLOSE, h);
	return (r < 0 && r > -4096) ? MAP_FAILED : (void *) r;
}

/* Garder un fichier en memoire sans process (la liste de dllpreload) -> 0, -1 et errno. Deux epingles
   du meme fichier n'en font qu'une */
int file_pin (const char *path)
{
	int fd = open (path, O_RDONLY), r = -1;
	if (fd < 0)
		return -1;
	struct stat st;
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d && fstat (fd, &st) == 0)
	{
		long h = st.st_size ? file_object (d, &st, 1) : -1;
		if (h < 0)
			errno = st.st_size ? EIO : EINVAL;
		else
		{
			r = (int) POSIX_SYS (__armpi_sys3 (SYS_SHM_CTL, h, K_SHM_PIN, 0));
			__armpi_sys1 (SYS_HANDLE_CLOSE, h);
		}
	}
	if (d)
		__posix_fd_put (d);
	close (fd);
	return r;
}

/* (file_unpin, file_pinned) L'objet du fichier tel qu'il est -> poignee, < 0 : aucun */
static long file_find (const char *path)
{
	struct stat st;
	char name[FILE_NAME];
	int e = errno, fd = open (path, O_RDONLY);
	if (fd < 0 || fstat (fd, &st) != 0)
		st.st_size = -1;
	if (fd >= 0)
		close (fd);
	errno = e;
	if (st.st_size < 0)
		return -1;
	file_name (name, path, &st);
	return __armpi_sys2 (SYS_SHM_OPEN, name, 0);
}

/* -> 0. L'objet reste tant qu'un process le projette, puis part */
int file_unpin (const char *path)
{
	long h = file_find (path);
	if (h >= 0)
	{
		__armpi_sys3 (SYS_SHM_CTL, h, K_SHM_UNPIN, 0);
		__armpi_sys1 (SYS_HANDLE_CLOSE, h);
	}
	return 0;
}

/* -> 1 si le fichier est epingle */
int file_pinned (const char *path)
{
	struct kinfo i;
	long h = file_find (path);
	if (h < 0)
		return 0;
	int r = __armpi_sys2 (SYS_HANDLE_INFO, h, &i) == 0 && (i.flags & K_SHM_PINNED);
	__armpi_sys1 (SYS_HANDLE_CLOSE, h);
	return r;
}

int shm_unlink (const char *name)
{
	if (name == 0)
		return POSIX_ERR (EINVAL);
	return (int) POSIX_SYS (__armpi_sys1 (SYS_SHM_UNLINK, name));
}

int __posix_shm_truncate (struct __posix_ofd *d, long len)
{
	if (len < 0)
		return POSIX_ERR (EINVAL);
	return (int) POSIX_SYS (__armpi_sys3 (SYS_SHM_CTL, d->h, K_SHM_SET_SIZE, len));
}

int __posix_shm_seals (struct __posix_ofd *d, int add, int seals)
{
	if (add)
		return (int) POSIX_SYS (__armpi_sys3 (SYS_SHM_CTL, d->h, K_SHM_ADD_SEALS, seals));
	struct kinfo i;
	if (handle_info (d->h, &i) != 0)
		return -1;
	return (int) i.state | ((i.flags & K_SHM_SEALABLE) ? 0 : F_SEAL_SEAL);
}

void *__posix_shm_mmap (struct __posix_ofd *d, void *addr, unsigned long len, int prot, int flags, long off)
{
	long kf = flags & (MAP_FIXED | MAP_FIXED_NOREPLACE);
	if ((flags & MAP_TYPE) == MAP_SHARED)
	{
		long r = __armpi_sys6 (SYS_SHM_MAP, d->h, addr, len, prot, kf, off);
		if (r < 0 && r > -4096)
		{
			errno = __armpi_errno (r);
			return MAP_FAILED;
		}
		return (void *) r;
	}
	/* MAP_PRIVATE : les pages de l'objet en copie sur ecriture ; au-dela de sa fin, une copie
	   (region anonyme), remplie depuis une projection partagee : le reste est a zero */
	long size = __posix_handle_size (d->h);
	if (size < 0)
		return MAP_FAILED;
	if ((unsigned long) off + len <= (((unsigned long) size + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1)))
	{
		long r = __armpi_sys6 (SYS_SHM_MAP, d->h, addr, len, prot, kf | K_VM_COW, off);
		if (r < 0 && r > -4096)
		{
			errno = __armpi_errno (r);
			return MAP_FAILED;
		}
		return (void *) r;
	}
	long p = __armpi_sys6 (SYS_VM_MAP, addr, len, PROT_READ | PROT_WRITE, kf, 0, 0);
	if (p < 0 && p > -4096)
	{
		errno = __armpi_errno (p);
		return MAP_FAILED;
	}
	long n = size - off;
	if (n > (long) len)
		n = (long) len;
	if (n > 0)
	{
		unsigned long m = ((unsigned long) n + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1);
		long q = __armpi_sys6 (SYS_SHM_MAP, d->h, 0, m, PROT_READ, 0, off);
		if (q < 0 && q > -4096)
		{
			__armpi_sys2 (SYS_VM_UNMAP, p, len);
			errno = __armpi_errno (q);
			return MAP_FAILED;
		}
		memcpy ((void *) p, (const void *) q, (size_t) n);
		__armpi_sys2 (SYS_VM_UNMAP, q, m);
	}
	if (prot != (PROT_READ | PROT_WRITE))
		__armpi_sys3 (SYS_VM_PROTECT, p, len, prot);
	return (void *) p;
}

/* ---- au demarrage : les poignees donnees par le pere (posix_spawn) a leur numero ---- */
void __posix_inherit_init (void)
{
	static struct { unsigned h, tag, info, pad; } e[256];
	long n = __armpi_sys3 (SYS_HANDLE_LIST, e, 256, 1);
	for (long i = 0; i < n; i++)
	{
		if (e[i].tag == 0)
			continue;
		if (e[i].tag & POSIX_SPAWN_TAG)			/* arguments et environnement (env.c) */
		{
			__posix_spawn_h = e[i].h;
			__posix_spawn_pages = e[i].tag & ~POSIX_SPAWN_TAG;
			continue;
		}
		int fd = (int) e[i].tag - 1;
		struct __posix_ofd *d = ofd_of_handle (e[i].h, e[i].info);
		if (d)
			__posix_fd_place (d, fd, 0);
	}
}
