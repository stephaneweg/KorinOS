/*
 * socket.c -- sockets BSD sur NetKit (libposix, docs/POSIX.md ; reecrit pour ARMPI d'apres
 * socket.c d'Onyx, sur le modele de ses anciens sockets tcp_*).
 *
 * IPv4, TCP et UDP, par les fonctions de NetKit (service netd) : net_socket, net_connect,
 * net_bind, net_listen, net_accept, net_send, net_recv (avec delai : 0 = sans attente),
 * net_sendto, net_recvfrom, net_close. NetKit n'a pas de mode non bloquant : O_NONBLOCK lit et
 * accepte avec un delai nul (EAGAIN), connect reste bloquant (rend 0 : permis) ; les octets lus
 * d'avance (par poll, par MSG_PEEK) attendent dans un tampon de retenue ; une connexion acceptee
 * d'avance par poll attend dans la description (pending). getpeername d'un socket accepte et
 * getsockname : l'adresse de l'interface active (net_info) ; l'autre bout d'un accept n'est pas
 * connu (0.0.0.0:0). shutdown ferme a moitie (TCP : FIN, on recoit toujours). Pas d'AF_INET6 ni de socket
 * AF_UNIX nomme (EAFNOSUPPORT), pas de SIGPIPE (EPIPE ; MSG_NOSIGNAL accepte). Les bouts de
 * socketpair (AF_UNIX, canaux du noyau : ipc.c) passent par __posix_lsock_* (local_of).
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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "posix_internal.h"

#undef htonl
#undef htons
#undef ntohl
#undef ntohs
uint32_t htonl (uint32_t x) { return __builtin_bswap32 (x); }
uint16_t htons (uint16_t x) { return __builtin_bswap16 (x); }
uint32_t ntohl (uint32_t x) { return __builtin_bswap32 (x); }
uint16_t ntohs (uint16_t x) { return __builtin_bswap16 (x); }

#define CARRY_CAP	65536

/* ---- NetKit ---- */
typedef long (*nk_fn) (long, long, long, long, long);
#define NK_EADDRINUSE	(-98)			/* (erreurs de NetKit : numeros de Linux) */

static long nk (unsigned rank, long a, long b, long c, long d, long e)
{
	nk_fn f = (nk_fn) __armpi_netkit (rank);
	if (f == 0)
		return -100;			/* ENETDOWN : pas de NetKit (pas de reseau) */
	return f (a, b, c, d, e);
}

static struct __posix_ofd *sock_get (int fd)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d && d->type != POSIX_FD_SOCKET)
	{
		__posix_fd_put (d);
		errno = ENOTSOCK;
		return 0;
	}
	return d;
}

/* un bout de canal local du noyau (socketpair AF_UNIX : ipc.c) ? -> sa description (une
 * reference), 0 sinon (errno inchange) */
static struct __posix_ofd *local_of (int fd)
{
	int e = errno;
	struct __posix_ofd *d = __posix_fd_get (fd);
	errno = e;
	if (d && d->type != POSIX_FD_LSOCK)
	{
		__posix_fd_put (d);
		d = 0;
	}
	return d;
}

/* l'adresse d'un bout local : AF_UNIX, sans nom */
static void unix_addr (struct sockaddr *a, socklen_t *len)
{
	if (a == 0 || len == 0)
		return;
	sa_family_t f = AF_UNIX;
	if (*len >= sizeof f)
		memcpy (a, &f, sizeof f);
	*len = sizeof f;
}

/* sockaddr_in -> ip (ordre de l'hote), port ; -1 si ce n'est pas IPv4 */
static int from_addr (const struct sockaddr *a, socklen_t len, unsigned *ip, unsigned *port)
{
	if (a == 0 || len < sizeof (struct sockaddr_in))
		return POSIX_ERR (EINVAL);
	if (a->sa_family != AF_INET)
		return POSIX_ERR (EAFNOSUPPORT);
	const struct sockaddr_in *s = (const struct sockaddr_in *) a;
	*ip = ntohl (s->sin_addr.s_addr);
	*port = ntohs (s->sin_port);
	return 0;
}

static void to_addr (struct sockaddr *a, socklen_t *len, unsigned ip, unsigned port)
{
	if (a == 0 || len == 0)
		return;
	struct sockaddr_in s;
	memset (&s, 0, sizeof s);
	s.sin_family = AF_INET;
	s.sin_addr.s_addr = htonl (ip);
	s.sin_port = htons ((uint16_t) port);
	socklen_t n = *len < sizeof s ? *len : (socklen_t) sizeof s;
	memcpy (a, &s, n);
	*len = sizeof s;
}

/* ---- creation, connexion ---- */
int socket (int domain, int type, int proto)
{
	(void) proto;
	int flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (domain != AF_INET)
		return POSIX_ERR (EAFNOSUPPORT);
	if (type != SOCK_STREAM && type != SOCK_DGRAM)
		return POSIX_ERR (EPROTONOSUPPORT);
	long sd = nk (NK_NET_SOCKET, type == SOCK_STREAM ? 1 : 2, 0, 0, 0, 0);
	if (sd < 0)
		return POSIX_SYS (sd);
	struct __posix_ofd *d = __posix_ofd_new (POSIX_FD_SOCKET, O_RDWR | ((flags & SOCK_NONBLOCK) ? O_NONBLOCK : 0));
	if (d == 0)
	{
		nk (NK_NET_CLOSE, sd, 0, 0, 0, 0);
		return -1;
	}
	d->h = sd;
	d->sotype = type;
	return __posix_fd_install (d, 0, (flags & SOCK_CLOEXEC) != 0);
}

int connect (int fd, const struct sockaddr *addr, socklen_t len)
{
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	unsigned ip, port;
	int r = from_addr (addr, len, &ip, &port);
	if (r == 0)
	{
		if (d->sotype == SOCK_DGRAM)
			d->connected = 1;		/* UDP : l'adresse par defaut de send */
		else if (d->connected)
			r = POSIX_ERR (EISCONN);
		else
		{
			long k = nk (NK_NET_CONNECT, d->h, ip, port, 0, 0);
			if (k < 0)
			{
				d->so_error = __armpi_errno (k);
				r = POSIX_SYS (k);
			}
			else
				d->connected = 1;
		}
		if (r == 0)
			memcpy (d->peer, addr, sizeof (struct sockaddr_in));
	}
	__posix_fd_put (d);
	return r;
}

int bind (int fd, const struct sockaddr *addr, socklen_t len)
{
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	unsigned ip, port;
	int r = from_addr (addr, len, &ip, &port);
	if (r == 0)
	{
		long k;
		if (port == 0)
		{
			/* port 0 : netd n'en choisit pas (EINVAL) ; un port ephemere libre (49152 a 65535,
			   comme l'IANA), a partir d'un point au hasard */
			static unsigned s_next;
			if (s_next == 0)
			{
				unsigned rnd = 0;
				__armpi_sys2 (SYS_GETRANDOM, &rnd, sizeof rnd);
				s_next = rnd % 16384;
			}
			k = NK_EADDRINUSE;
			for (int i = 0; i < 16384 && k == NK_EADDRINUSE; i++)
			{
				port = 49152 + (__atomic_fetch_add (&s_next, 1, __ATOMIC_RELAXED) % 16384);
				k = nk (NK_NET_BIND, d->h, port, 0, 0, 0);
			}
		}
		else
			k = nk (NK_NET_BIND, d->h, port, 0, 0, 0);
		if (k < 0)
			r = POSIX_SYS (k);
		else
		{
			d->bound = 1;
			d->lport = (unsigned short) port;
		}
	}
	__posix_fd_put (d);
	return r;
}

int listen (int fd, int backlog)
{
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r;
	if (d->sotype != SOCK_STREAM)
		r = POSIX_ERR (EOPNOTSUPP);
	else
	{
		long k = nk (NK_NET_LISTEN, d->h, backlog > 0 ? backlog : 4, 0, 0, 0);
		r = (int) POSIX_SYS (k);
		if (r == 0)
			d->listening = 1;
	}
	__posix_fd_put (d);
	return r;
}

int accept4 (int fd, struct sockaddr *addr, socklen_t *len, int flags)
{
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	if (!d->listening)
	{
		__posix_fd_put (d);
		return POSIX_ERR (EINVAL);
	}
	long sd = -1;
	int have = 0;
	__posix_ofd_lock (d);
	if (d->pending)
	{
		sd = d->pending - 1;		/* (deja acceptee par poll) */
		d->pending = 0;
		have = 1;
	}
	__posix_ofd_unlock (d);
	if (!have)
	{
		unsigned long ms = (d->flags & O_NONBLOCK) ? 0 : d->rcvtimeo ? d->rcvtimeo : ARMPI_NET_FOREVER;
		sd = nk (NK_NET_ACCEPT, d->h, (long) ms, 0, 0, 0);
		if (sd < 0)
		{
			__posix_fd_put (d);
			return sd == -11 ? POSIX_ERR (EAGAIN) : POSIX_SYS (sd);
		}
	}
	__posix_fd_put (d);
	struct __posix_ofd *n = __posix_ofd_new (POSIX_FD_SOCKET, O_RDWR | ((flags & SOCK_NONBLOCK) ? O_NONBLOCK : 0));
	if (n == 0)
	{
		nk (NK_NET_CLOSE, sd, 0, 0, 0, 0);
		return -1;
	}
	n->h = sd;
	n->sotype = SOCK_STREAM;
	n->connected = 1;
	to_addr (addr, len, 0, 0);		/* (NetKit ne donne pas l'autre bout) */
	return __posix_fd_install (n, 0, (flags & SOCK_CLOEXEC) != 0);
}

int accept (int fd, struct sockaddr *addr, socklen_t *len) { return accept4 (fd, addr, len, 0); }

/* ---- tampon de retenue (poll, MSG_PEEK) ---- */
size_t __posix_carry_take (struct __posix_ofd *d, void *buf, size_t n, int peek)
{
	size_t avail = d->carry_n - d->carry_off;
	if (avail == 0 || n == 0)
		return 0;
	if (n > avail)
		n = avail;
	memcpy (buf, d->carry + d->carry_off, n);
	if (!peek)
	{
		d->carry_off += (unsigned) n;
		if (d->carry_off == d->carry_n)
			d->carry_off = d->carry_n = 0;
	}
	return n;
}

static int carry_add (struct __posix_ofd *d, const void *b, size_t n)
{
	if (d->carry == 0 && (d->carry = (unsigned char *) malloc (CARRY_CAP)) == 0)
		return -1;
	if (d->carry_off)
	{
		memmove (d->carry, d->carry + d->carry_off, d->carry_n - d->carry_off);
		d->carry_n -= d->carry_off;
		d->carry_off = 0;
	}
	if (n > CARRY_CAP - d->carry_n)
		n = CARRY_CAP - d->carry_n;
	memcpy (d->carry + d->carry_n, b, n);
	d->carry_n += (unsigned) n;
	return 0;
}

/* sans attente : des donnees a lire ? (poll) -> 1 oui, 0 non, -1 fin ou erreur */
int __posix_carry_fill (struct __posix_ofd *d)
{
	__posix_ofd_lock (d);
	int r;
	if (d->carry_n > d->carry_off)
		r = 1;
	else if (d->eof)
		r = -1;
	else if (d->type == POSIX_FD_PIPE_R)
	{
		unsigned char tmp[ARMPI_PIPE_CHUNK];
		long k = __posix_pipe_read (d->h, tmp, sizeof tmp, 1);
		if (k > 0)
			r = carry_add (d, tmp, (size_t) k) == 0 ? 1 : -1;
		else if (k == 0)
		{
			d->eof = 1;
			r = -1;
		}
		else
			r = k == -11 ? 0 : -1;
	}
	else if (d->type != POSIX_FD_SOCKET)
		r = 1;
	else if (d->listening)
	{
		if (d->pending)
			r = 1;
		else
		{
			long sd = nk (NK_NET_ACCEPT, d->h, 0, 0, 0, 0);
			if (sd >= 0)
			{
				d->pending = sd + 1;
				r = 1;
			}
			else
				r = sd == -11 ? 0 : -1;
		}
	}
	else if (d->sotype == SOCK_STREAM && !d->connected)
		r = 0;
	else
	{
		unsigned char tmp[2048];
		long k = nk (NK_NET_RECV, d->h, (long) tmp, sizeof tmp, 0, 0);
		if (k > 0)
			r = carry_add (d, tmp, (size_t) k) == 0 ? 1 : -1;
		else if (k == 0)
		{
			d->eof = 1;
			r = -1;
		}
		else
			r = k == -11 ? 0 : -1;
	}
	__posix_ofd_unlock (d);
	return r;
}

/* ---- recevoir, envoyer ---- */
ssize_t __posix_sock_recv (struct __posix_ofd *d, void *buf, size_t n, int flags, void *from)
{
	if (n == 0)
		return 0;
	if (d->listening || (d->sotype == SOCK_STREAM && !d->connected))
		return POSIX_ERR (ENOTCONN);
	int nowait = (flags & MSG_DONTWAIT) || (d->flags & O_NONBLOCK);
	size_t got = 0;
	for (;;)
	{
		__posix_ofd_lock (d);
		size_t c = __posix_carry_take (d, (char *) buf + got, n - got, (flags & MSG_PEEK) != 0);
		got += c;
		if (got == n || (got && !(flags & MSG_WAITALL)) || (got && (flags & MSG_PEEK)) || d->eof)
		{
			__posix_ofd_unlock (d);
			break;
		}
		/* sans attente, le verrou reste pris pendant la lecture : le poll d'un autre thread lit aussi
		 * ce socket (__posix_carry_fill), et ce qu'il aurait retenu passerait apres ce qu'on lit ici */
		if (!nowait)
			__posix_ofd_unlock (d);
		unsigned long ms = nowait ? 0 : d->rcvtimeo ? d->rcvtimeo : ARMPI_NET_FOREVER;
		long k;
		unsigned ipport[2] = { 0, 0 };
		if (flags & MSG_PEEK)
		{
			unsigned char tmp[2048];
			k = d->sotype == SOCK_DGRAM ? nk (NK_NET_RECVFROM, d->h, (long) tmp, sizeof tmp, (long) ms, (long) ipport)
						    : nk (NK_NET_RECV, d->h, (long) tmp, sizeof tmp, (long) ms, 0);
			if (k > 0)
			{
				if (!nowait)
					__posix_ofd_lock (d);
				carry_add (d, tmp, (size_t) k);
				__posix_ofd_unlock (d);
				continue;
			}
		}
		else if (d->sotype == SOCK_DGRAM)
			k = nk (NK_NET_RECVFROM, d->h, (long) ((char *) buf + got), (long) (n - got), (long) ms, (long) ipport);
		else
			k = nk (NK_NET_RECV, d->h, (long) ((char *) buf + got), (long) (n - got), (long) ms, 0);
		if (nowait)
			__posix_ofd_unlock (d);
		if (k == 0)
		{
			d->eof = 1;
			break;
		}
		if (k < 0)
		{
			if (got)
				break;
			return k == -11 ? POSIX_ERR (EAGAIN) : POSIX_SYS (k);
		}
		if (from && d->sotype == SOCK_DGRAM)
		{
			struct sockaddr_in *s = (struct sockaddr_in *) from;
			memset (s, 0, sizeof *s);
			s->sin_family = AF_INET;
			s->sin_addr.s_addr = htonl (ipport[0]);
			s->sin_port = htons ((uint16_t) ipport[1]);
		}
		got += (size_t) k;
		if (got == n || !(flags & MSG_WAITALL) || d->sotype == SOCK_DGRAM)
			break;
	}
	return (ssize_t) got;
}

ssize_t __posix_sock_send (struct __posix_ofd *d, const void *buf, size_t n, int flags, const void *to)
{
	(void) flags;
	long k;
	if (d->shut_wr)
		return POSIX_ERR (EPIPE);
	if (d->sotype == SOCK_DGRAM)
	{
		const struct sockaddr_in *s = (const struct sockaddr_in *) (to ? to : (d->connected ? (const void *) d->peer : 0));
		if (s == 0)
			return POSIX_ERR (EDESTADDRREQ);
		k = nk (NK_NET_SENDTO, d->h, (long) buf, (long) n, (long) ntohl (s->sin_addr.s_addr), ntohs (s->sin_port));
	}
	else
	{
		if (!d->connected)
			return POSIX_ERR (ENOTCONN);
		k = nk (NK_NET_SEND, d->h, (long) buf, (long) n, 0, 0);
	}
	if (k < 0)
		return k == -104 || k == -32 ? POSIX_ERR (EPIPE) : POSIX_SYS (k);
	return (ssize_t) k;
}

ssize_t recvfrom (int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *len)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		struct iovec v = { buf, n };
		ssize_t r = __posix_lsock_recv (l, &v, 1, 0, 0, 0, flags);
		if (r >= 0)
			unix_addr (from, len);
		__posix_fd_put (l);
		return r;
	}
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	struct sockaddr_in s;
	memset (&s, 0, sizeof s);
	ssize_t r = __posix_sock_recv (d, buf, n, flags, &s);
	if (r >= 0 && from && len)
	{
		if (d->sotype != SOCK_DGRAM)
			memcpy (&s, d->peer, sizeof s);
		socklen_t m = *len < sizeof s ? *len : (socklen_t) sizeof s;
		memcpy (from, &s, m);
		*len = sizeof s;
	}
	__posix_fd_put (d);
	return r;
}

ssize_t recv (int fd, void *buf, size_t n, int flags) { return recvfrom (fd, buf, n, flags, 0, 0); }

ssize_t sendto (int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t len)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		struct iovec v = { (void *) buf, n };
		ssize_t r = to ? POSIX_ERR (EISCONN) : __posix_lsock_send (l, &v, 1, 0, 0, flags);
		__posix_fd_put (l);
		return r;
	}
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	ssize_t r;
	if (to && (len < sizeof (struct sockaddr_in) || to->sa_family != AF_INET))
		r = POSIX_ERR (EINVAL);
	else
		r = __posix_sock_send (d, buf, n, flags, to);
	__posix_fd_put (d);
	return r;
}

ssize_t send (int fd, const void *buf, size_t n, int flags) { return sendto (fd, buf, n, flags, 0, 0); }

/* SCM_RIGHTS d'un message -> les descripteurs (max) ; -1 : EINVAL */
static int cmsg_fds (const struct msghdr *m, int *fds, int max)
{
	int n = 0;
	if (m->msg_control == 0 || m->msg_controllen == 0)
		return 0;
	for (struct cmsghdr *c = CMSG_FIRSTHDR (m); c; c = CMSG_NXTHDR ((struct msghdr *) m, c))
	{
		if (c->cmsg_len < CMSG_LEN (0))
			return POSIX_ERR (EINVAL);
		if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
			continue;
		int k = (int) ((c->cmsg_len - CMSG_LEN (0)) / sizeof (int));
		if (n + k > max)
			return POSIX_ERR (EINVAL);
		memcpy (fds + n, CMSG_DATA (c), (size_t) k * sizeof (int));
		n += k;
	}
	return n;
}

ssize_t sendmsg (int fd, const struct msghdr *m, int flags)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		int fds[256];
		int n = cmsg_fds (m, fds, 256);
		ssize_t r = n < 0 ? -1 : __posix_lsock_send (l, m->msg_iov, (int) m->msg_iovlen, fds, n, flags);
		__posix_fd_put (l);
		return r;
	}
	if (m->msg_controllen)
		return POSIX_ERR (EOPNOTSUPP);	/* (pas de SCM_RIGHTS sur ARMPI) */
	size_t total = 0;
	for (size_t i = 0; i < (size_t) m->msg_iovlen; i++)
		total += m->msg_iov[i].iov_len;
	char *b = (char *) malloc (total ? total : 1);
	if (b == 0)
		return POSIX_ERR (ENOMEM);
	size_t o = 0;
	for (size_t i = 0; i < (size_t) m->msg_iovlen; i++)
	{
		memcpy (b + o, m->msg_iov[i].iov_base, m->msg_iov[i].iov_len);
		o += m->msg_iov[i].iov_len;
	}
	ssize_t r = sendto (fd, b, total, flags, (const struct sockaddr *) m->msg_name, m->msg_namelen);
	free (b);
	return r;
}

ssize_t recvmsg (int fd, struct msghdr *m, int flags)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		int fds[256];
		int n = 0;
		if (m->msg_control && m->msg_controllen >= CMSG_LEN (sizeof (int)))
			n = (int) ((m->msg_controllen - CMSG_LEN (0)) / sizeof (int));
		if (n > 256)
			n = 256;
		int mf = 0;
		ssize_t r = __posix_lsock_recv (l, m->msg_iov, (int) m->msg_iovlen, fds, &n, &mf, flags);
		__posix_fd_put (l);
		if (r < 0)
			return r;
		if (n > 0)
		{
			struct cmsghdr *c = (struct cmsghdr *) m->msg_control;
			c->cmsg_level = SOL_SOCKET;
			c->cmsg_type = SCM_RIGHTS;
			c->cmsg_len = CMSG_LEN ((size_t) n * sizeof (int));
			memcpy (CMSG_DATA (c), fds, (size_t) n * sizeof (int));
			size_t sp = CMSG_SPACE ((size_t) n * sizeof (int));
			if (sp < m->msg_controllen)
				m->msg_controllen = sp;
		}
		else
			m->msg_controllen = 0;
		if (m->msg_name)
			unix_addr ((struct sockaddr *) m->msg_name, &m->msg_namelen);
		m->msg_flags = mf;
		return r;
	}
	size_t total = 0;
	for (size_t i = 0; i < (size_t) m->msg_iovlen; i++)
		total += m->msg_iov[i].iov_len;
	char *b = (char *) malloc (total ? total : 1);
	if (b == 0)
		return POSIX_ERR (ENOMEM);
	socklen_t nl = m->msg_namelen;
	ssize_t r = recvfrom (fd, b, total, flags, (struct sockaddr *) m->msg_name, m->msg_name ? &nl : 0);
	if (r > 0)
	{
		size_t o = 0;
		for (size_t i = 0; i < (size_t) m->msg_iovlen && o < (size_t) r; i++)
		{
			size_t c = m->msg_iov[i].iov_len;
			if (c > (size_t) r - o)
				c = (size_t) r - o;
			memcpy (m->msg_iov[i].iov_base, b + o, c);
			o += c;
		}
	}
	if (m->msg_name)
		m->msg_namelen = nl;
	m->msg_controllen = 0;
	m->msg_flags = 0;
	free (b);
	return r;
}

/* ---- options ---- */
static int put_int (void *v, socklen_t *len, int x)
{
	if (v == 0 || len == 0 || *len < sizeof (int))
		return POSIX_ERR (EINVAL);
	memcpy (v, &x, sizeof x);
	*len = sizeof x;
	return 0;
}

int getsockopt (int fd, int level, int opt, void *v, socklen_t *len)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		int r = __posix_lsock_getsockopt (l, level, opt, v, (unsigned *) len);
		__posix_fd_put (l);
		return r;
	}
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	if (level == SOL_SOCKET)
		switch (opt)
		{
		case SO_ERROR: r = put_int (v, len, d->so_error); d->so_error = 0; break;
		case SO_TYPE: r = put_int (v, len, d->sotype); break;
		case SO_DOMAIN: r = put_int (v, len, AF_INET); break;
		case SO_PROTOCOL: r = put_int (v, len, d->sotype == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP); break;
		case SO_ACCEPTCONN: r = put_int (v, len, d->listening); break;
		case SO_RCVBUF: case SO_SNDBUF: r = put_int (v, len, 65536); break;
		case SO_RCVTIMEO:
		case SO_SNDTIMEO:
		{
			unsigned ms = opt == SO_RCVTIMEO ? d->rcvtimeo : d->sndtimeo;
			struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
			if (v == 0 || len == 0 || *len < sizeof tv)
				r = POSIX_ERR (EINVAL);
			else
			{
				memcpy (v, &tv, sizeof tv);
				*len = sizeof tv;
			}
			break;
		}
		default: r = put_int (v, len, 0); break;
		}
	else
		r = put_int (v, len, 0);
	__posix_fd_put (d);
	return r;
}

int setsockopt (int fd, int level, int opt, const void *v, socklen_t len)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		int r = __posix_lsock_setsockopt (l, level, opt, v, len);
		__posix_fd_put (l);
		return r;
	}
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	if (level == SOL_SOCKET && (opt == SO_RCVTIMEO || opt == SO_SNDTIMEO))
	{
		struct timeval tv;
		if (v == 0 || len < sizeof tv)
			r = POSIX_ERR (EINVAL);
		else
		{
			memcpy (&tv, v, sizeof tv);
			unsigned ms = (unsigned) (tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000);
			if (opt == SO_RCVTIMEO)
				d->rcvtimeo = ms;
			else
				d->sndtimeo = ms;
		}
	}
	__posix_fd_put (d);
	return r;				/* (les autres options : acceptees sans effet) */
}

/* ---- adresses ---- */
int getsockname (int fd, struct sockaddr *a, socklen_t *len)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		unix_addr (a, len);
		__posix_fd_put (l);
		return 0;
	}
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	unsigned info[8];
	unsigned ip = 0;
	long (*ni) (void *) = (long (*) (void *)) __armpi_netkit (NK_NET_INFO);
	if (ni != 0 && ni (info) >= 0)
		ip = info[0];
	if (!d->connected && !d->listening)
		ip = 0;				/* (INADDR_ANY) */
	to_addr (a, len, ip, d->lport);
	__posix_fd_put (d);
	return 0;
}

int getpeername (int fd, struct sockaddr *a, socklen_t *len)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		unix_addr (a, len);
		__posix_fd_put (l);
		return 0;
	}
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	if (!d->connected)
		r = POSIX_ERR (ENOTCONN);
	else if (a && len)
	{
		socklen_t m = *len < sizeof (struct sockaddr_in) ? *len : (socklen_t) sizeof (struct sockaddr_in);
		memcpy (a, d->peer, m);
		if (((struct sockaddr *) d->peer)->sa_family == 0)
			to_addr (a, len, 0, 0);
		*len = sizeof (struct sockaddr_in);
	}
	__posix_fd_put (d);
	return r;
}

int shutdown (int fd, int how)
{
	struct __posix_ofd *l = local_of (fd);
	if (l)
	{
		int r = __posix_lsock_shutdown (l, how);
		__posix_fd_put (l);
		return r;
	}
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = d->connected || d->listening ? 0 : POSIX_ERR (ENOTCONN);
	if (r == 0 && how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
		r = POSIX_ERR (EINVAL);
	if (r == 0 && d->connected)
	{
		if (how != SHUT_WR)
			d->eof = 1;			/* (ce qui est deja arrive reste lisible) */
		if (how != SHUT_RD)
		{
			d->shut_wr = 1;
			long k = d->sotype == SOCK_STREAM ? nk (NK_NET_SHUTDOWN, d->h, 0, 0, 0, 0) : 0;
			if (k < 0 && k != -100)
				r = (int) POSIX_SYS (k);
		}
	}
	__posix_fd_put (d);
	return r;
}

int __posix_sock_close (struct __posix_ofd *d)
{
	if (d->pending)
		nk (NK_NET_CLOSE, d->pending - 1, 0, 0, 0, 0);
	long k = nk (NK_NET_CLOSE, d->h, 0, 0, 0, 0);
	return k < 0 && k != -100 ? POSIX_SYS (k) : 0;
}

int sockatmark (int fd)
{
	struct __posix_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	__posix_fd_put (d);
	return 0;
}
