/* winsock.c -- win16 : WINSOCK.DLL (Windows Sockets 1.1) sur les sockets de Korin (libposix ; sur l'hôte, ceux
 * de Linux). D'après winsock/socket.c de winevdm (Wine, LGPL-2.1) pour les structures 16 bits.
 *
 * Un SOCKET de Winsock 16 bits est un mot : ici l'indice + 1 d'une table de descripteurs POSIX, tous en mode non
 * bloquant ; un appel « bloquant » du programme attend par poll (le programme est figé pendant ce temps, comme
 * un Windows 3.1 sans crochet de blocage). WSAAsyncSelect : ws_poll, appelé par la boucle des messages de USER
 * (user_take), poste les messages FD_READ, FD_WRITE, FD_ACCEPT, FD_CONNECT, FD_CLOSE, réarmés comme sous
 * Windows par l'appel correspondant (recv, send, accept). Les fonctions WSAAsyncGet* répondent tout de suite (la
 * recherche est faite avant de rendre la main) par le message demandé.
 *
 * Les constantes de Winsock ne sont pas celles de POSIX (SOL_SOCKET 0xFFFF, SO_*, FIONBIO, erreurs 10000 +) :
 * elles sont traduites ici. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include "w16.h"
#include "user.h"

#define MAX_SOCK	64
#define INVALID_SOCKET16	0xffff
#define SOCKET_ERROR16		0xffff

/* les erreurs de Winsock */
#define WSAEINTR		10004
#define WSAEBADF		10009
#define WSAEACCES		10013
#define WSAEFAULT		10014
#define WSAEINVAL		10022
#define WSAEMFILE		10024
#define WSAEWOULDBLOCK		10035
#define WSAEINPROGRESS		10036
#define WSAEALREADY		10037
#define WSAENOTSOCK		10038
#define WSAEDESTADDRREQ		10039
#define WSAEMSGSIZE		10040
#define WSAEPROTOTYPE		10041
#define WSAENOPROTOOPT		10042
#define WSAEPROTONOSUPPORT	10043
#define WSAESOCKTNOSUPPORT	10044
#define WSAEOPNOTSUPP		10045
#define WSAEAFNOSUPPORT		10047
#define WSAEADDRINUSE		10048
#define WSAEADDRNOTAVAIL	10049
#define WSAENETDOWN		10050
#define WSAENETUNREACH		10051
#define WSAECONNABORTED		10053
#define WSAECONNRESET		10054
#define WSAENOBUFS		10055
#define WSAEISCONN		10056
#define WSAENOTCONN		10057
#define WSAESHUTDOWN		10058
#define WSAETIMEDOUT		10060
#define WSAECONNREFUSED		10061
#define WSAEHOSTUNREACH		10065
#define WSANOTINITIALISED	10093
#define WSAHOST_NOT_FOUND	11001
#define WSANO_DATA		11004

/* les événements de WSAAsyncSelect */
#define FD_READ		0x01
#define FD_WRITE	0x02
#define FD_OOB		0x04
#define FD_ACCEPT	0x08
#define FD_CONNECT	0x10
#define FD_CLOSE	0x20

struct wsock
{
	int used, fd, type;
	int nonblock;			/* FIONBIO, ou WSAAsyncSelect */
	int connecting;			/* connect en cours (non bloquant) */
	int listening;
	uint16_t hwnd, msg;		/* WSAAsyncSelect */
	uint16_t events;		/* les événements demandés */
	uint16_t armed;			/* ceux qui peuvent encore être postés (réarmés par recv, send, accept) */
	int closed_sent;
};

static struct wsock socks[MAX_SOCK];
static int started, last_error, async_count;
static uint16_t next_task = 1;

static struct wsock *sock_of (uint16_t s)
{
	if (s < 1 || s > MAX_SOCK || !socks[s - 1].used)
		return 0;
	return &socks[s - 1];
}

static int wsa_from_errno (int e)
{
	switch (e)
	{
	case EINTR: return WSAEINTR;
	case EBADF: return WSAEBADF;
	case EACCES: case EPERM: return WSAEACCES;
	case EFAULT: return WSAEFAULT;
	case EINVAL: return WSAEINVAL;
	case EMFILE: case ENFILE: return WSAEMFILE;
#if EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
	case EAGAIN: return WSAEWOULDBLOCK;
	case EINPROGRESS: return WSAEWOULDBLOCK;	/* (connect non bloquant : Winsock dit WOULDBLOCK) */
	case EALREADY: return WSAEALREADY;
	case ENOTSOCK: return WSAENOTSOCK;
	case EDESTADDRREQ: return WSAEDESTADDRREQ;
	case EMSGSIZE: return WSAEMSGSIZE;
	case EPROTOTYPE: return WSAEPROTOTYPE;
	case ENOPROTOOPT: return WSAENOPROTOOPT;
	case EPROTONOSUPPORT: return WSAEPROTONOSUPPORT;
	case EOPNOTSUPP: return WSAEOPNOTSUPP;
	case EAFNOSUPPORT: return WSAEAFNOSUPPORT;
	case EADDRINUSE: return WSAEADDRINUSE;
	case EADDRNOTAVAIL: return WSAEADDRNOTAVAIL;
	case ENETDOWN: return WSAENETDOWN;
	case ENETUNREACH: return WSAENETUNREACH;
	case ECONNABORTED: return WSAECONNABORTED;
	case ECONNRESET: case EPIPE: return WSAECONNRESET;
	case ENOBUFS: case ENOMEM: return WSAENOBUFS;
	case EISCONN: return WSAEISCONN;
	case ENOTCONN: return WSAENOTCONN;
	case ESHUTDOWN: return WSAESHUTDOWN;
	case ETIMEDOUT: return WSAETIMEDOUT;
	case ECONNREFUSED: return WSAECONNREFUSED;
	case EHOSTUNREACH: return WSAEHOSTUNREACH;
	default: return WSAEINVAL;
	}
}

static void fail (struct w16call *c, int wsa)
{
	last_error = wsa;
	c->ret = SOCKET_ERROR16;
}

static void fail_errno (struct w16call *c) { fail (c, wsa_from_errno (errno)); }

/* sockaddr de Winsock (16 octets : famille en petit-boutiste, port et adresse dans l'ordre du réseau) <-> POSIX */
static int addr_in (uint32_t p, int len, struct sockaddr_in *a)
{
	if (!p || len < 8)
		return 0;
	uint8_t *b = far_ptr (p);
	memset (a, 0, sizeof *a);
	a->sin_family = AF_INET;
	memcpy (&a->sin_port, b + 2, 2);
	memcpy (&a->sin_addr, b + 4, 4);
	return (b[0] | b[1] << 8) == 2;
}

static void addr_out (uint32_t p, uint32_t plen, const struct sockaddr_in *a)
{
	if (!p)
		return;
	uint8_t *b = far_ptr (p);
	int cap = plen ? (int16_t) rd16 (far_lin (plen)) : 16;
	uint8_t t[16] = { 2, 0 };
	memcpy (t + 2, &a->sin_port, 2);
	memcpy (t + 4, &a->sin_addr, 4);
	memcpy (b, t, (size_t) (cap < 16 ? (cap > 0 ? cap : 0) : 16));
	if (plen)
		wr16 (far_lin (plen), 16);
}

/* attendre qu'un descripteur soit prêt (un appel bloquant du programme) -> 1 prêt, 0 délai */
static int wait_fd (int fd, short ev, int ms)
{
	struct pollfd p = { fd, ev, 0 };
	for (;;)
	{
		int r = poll (&p, 1, ms);
		if (r < 0 && errno == EINTR)
			continue;
		return r > 0;
	}
}

#define NEED_STARTED(c) do { if (!started) { fail (c, WSANOTINITIALISED); return; } } while (0)

/* ======================================================================== démarrage */
/* WSAStartup (version, WSADATA) : wVersion, wHighVersion, szDescription[257], szSystemStatus[129], iMaxSockets,
 * iMaxUdpDg, lpVendorInfo */
void api_WSAStartup16 (struct w16call *c)
{
	uint16_t want = (uint16_t) c->a[0];
	if ((want & 0xff) < 1 || ((want & 0xff) == 1 && (want >> 8) < 1))
	{
		c->ret = 10092;	/* WSAVERNOTSUPPORTED */
		return;
	}
	if (c->a[1])
	{
		uint8_t *d = far_ptr (c->a[1]);
		memset (d, 0, 2 + 2 + 257 + 129 + 2 + 2 + 4);
		wr16 (far_lin (c->a[1]), 0x0101);
		wr16 (far_lin (c->a[1]) + 2, 0x0101);
		snprintf ((char *) d + 4, 257, "Korin OS Windows Sockets (win16)");
		snprintf ((char *) d + 4 + 257, 129, "Prêt");
		wr16 (far_lin (c->a[1]) + 4 + 257 + 129, MAX_SOCK);
		wr16 (far_lin (c->a[1]) + 4 + 257 + 129 + 2, 8192);
	}
	started++;
	c->ret = 0;
}

void api_WSACleanup16 (struct w16call *c)
{
	if (!started)
	{
		fail (c, WSANOTINITIALISED);
		return;
	}
	if (--started == 0)
		for (int i = 0; i < MAX_SOCK; i++)
			if (socks[i].used)
			{
				close (socks[i].fd);
				memset (&socks[i], 0, sizeof socks[i]);
			}
	c->ret = 0;
}

void api_WSAGetLastError16 (struct w16call *c) { c->ret = (uint32_t) last_error; }
void api_WSASetLastError16 (struct w16call *c) { last_error = (int16_t) c->a[0]; }
void api_WSAIsBlocking16 (struct w16call *c) { c->ret = 0; }
void api_WSACancelBlockingCall16 (struct w16call *c) { c->ret = 0; }
void api_WSASetBlockingHook16 (struct w16call *c) { c->ret = 0; }	/* (l'ancien crochet : aucun) */
void api_WSAUnhookBlockingHook16 (struct w16call *c) { c->ret = 0; }
void api_default_hook (struct w16call *c) { c->ret = 0; }

/* ======================================================================== sockets */
void api_socket16 (struct w16call *c)
{
	NEED_STARTED (c);
	int af = (int16_t) c->a[0], type = (int16_t) c->a[1], proto = (int16_t) c->a[2];
	if (af != 2)
	{
		last_error = WSAEAFNOSUPPORT;
		c->ret = INVALID_SOCKET16;
		return;
	}
	int ht = type == 1 ? SOCK_STREAM : type == 2 ? SOCK_DGRAM : -1;
	if (ht < 0)
	{
		last_error = WSAESOCKTNOSUPPORT;
		c->ret = INVALID_SOCKET16;
		return;
	}
	int i = 0;
	while (i < MAX_SOCK && socks[i].used)
		i++;
	if (i == MAX_SOCK)
	{
		last_error = WSAEMFILE;
		c->ret = INVALID_SOCKET16;
		return;
	}
	int fd = socket (AF_INET, ht, proto);
	if (fd < 0)
	{
		last_error = wsa_from_errno (errno);
		c->ret = INVALID_SOCKET16;
		return;
	}
	fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
	memset (&socks[i], 0, sizeof socks[i]);
	socks[i].used = 1;
	socks[i].fd = fd;
	socks[i].type = type;
	c->ret = (uint32_t) (i + 1);
}

void api_closesocket16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
	{
		fail (c, WSAENOTSOCK);
		return;
	}
	if (s->hwnd)
		async_count--;
	close (s->fd);
	memset (s, 0, sizeof *s);
	c->ret = 0;
}

void api_bind16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	struct sockaddr_in a;
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	if (!addr_in (c->a[1], (int16_t) c->a[2], &a))
		{ fail (c, WSAEFAULT); return; }
	int one = 1;
	setsockopt (s->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	if (bind (s->fd, (struct sockaddr *) &a, sizeof a) < 0)
		{ fail_errno (c); return; }
	c->ret = 0;
}

void api_listen16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	if (listen (s->fd, (int16_t) c->a[1] > 0 ? (int16_t) c->a[1] : 5) < 0)
		{ fail_errno (c); return; }
	s->listening = 1;
	c->ret = 0;
}

void api_connect16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	struct sockaddr_in a;
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	if (!addr_in (c->a[1], (int16_t) c->a[2], &a))
		{ fail (c, WSAEFAULT); return; }
	int r = connect (s->fd, (struct sockaddr *) &a, sizeof a);
	if (r == 0 && !s->nonblock)
	{
		c->ret = 0;
		return;
	}
	/* (non bloquant : comme Winsock, toujours WSAEWOULDBLOCK, puis FD_CONNECT, même quand la connexion a abouti
	   tout de suite, ce qui arrive avec la pile de Korin) */
	if (r != 0 && errno != EINPROGRESS)
		{ fail_errno (c); return; }
	if (s->nonblock)
	{
		s->connecting = 1;
		if (s->hwnd)
			s->armed |= FD_CONNECT;
		{ fail (c, WSAEWOULDBLOCK); return; }
	}
	/* bloquant : attendre la fin de la connexion */
	wait_fd (s->fd, POLLOUT, -1);
	int err = 0;
	socklen_t l = sizeof err;
	getsockopt (s->fd, SOL_SOCKET, SO_ERROR, &err, &l);
	if (err)
		{ fail (c, wsa_from_errno (err)); return; }
	c->ret = 0;
}

void api_accept16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
	{
		last_error = WSAENOTSOCK;
		c->ret = INVALID_SOCKET16;
		return;
	}
	struct sockaddr_in a;
	socklen_t l = sizeof a;
	int fd;
	for (;;)
	{
		fd = accept (s->fd, (struct sockaddr *) &a, &l);
		if (fd >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) || s->nonblock)
			break;
		wait_fd (s->fd, POLLIN, -1);
	}
	if (s->hwnd)
		s->armed |= FD_ACCEPT;
	if (fd < 0)
	{
		last_error = wsa_from_errno (errno);
		c->ret = INVALID_SOCKET16;
		return;
	}
	int i = 0;
	while (i < MAX_SOCK && socks[i].used)
		i++;
	if (i == MAX_SOCK)
	{
		close (fd);
		last_error = WSAEMFILE;
		c->ret = INVALID_SOCKET16;
		return;
	}
	fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
	memset (&socks[i], 0, sizeof socks[i]);
	socks[i].used = 1;
	socks[i].fd = fd;
	socks[i].type = s->type;
	/* (le nouveau socket hérite de WSAAsyncSelect, comme sous Windows) */
	socks[i].nonblock = s->nonblock;
	if (s->hwnd)
	{
		socks[i].hwnd = s->hwnd;
		socks[i].msg = s->msg;
		socks[i].events = s->events;
		socks[i].armed = s->events & (FD_READ | FD_WRITE | FD_OOB | FD_CLOSE);
		async_count++;
	}
	addr_out (c->a[1], c->a[2], &a);
	c->ret = (uint32_t) (i + 1);
}

static void io_done (struct wsock *s, uint16_t ev)
{
	if (s->hwnd)
		s->armed |= ev & s->events;
}

void api_recv16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	uint16_t len = (uint16_t) c->a[2];
	int flags = (c->a[3] & 1 ? MSG_OOB : 0) | (c->a[3] & 2 ? MSG_PEEK : 0);
	ssize_t r;
	for (;;)
	{
		r = recv (s->fd, far_ptr (c->a[1]), len, flags);
		if (r >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) || s->nonblock)
			break;
		wait_fd (s->fd, POLLIN, -1);
	}
	io_done (s, FD_READ | FD_OOB);
	if (r < 0)
		{ fail_errno (c); return; }
	c->ret = (uint32_t) r;
}

void api_recvfrom16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	struct sockaddr_in a;
	socklen_t l = sizeof a;
	int flags = (c->a[3] & 1 ? MSG_OOB : 0) | (c->a[3] & 2 ? MSG_PEEK : 0);
	ssize_t r;
	for (;;)
	{
		r = recvfrom (s->fd, far_ptr (c->a[1]), (uint16_t) c->a[2], flags, (struct sockaddr *) &a, &l);
		if (r >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) || s->nonblock)
			break;
		wait_fd (s->fd, POLLIN, -1);
	}
	io_done (s, FD_READ);
	if (r < 0)
		{ fail_errno (c); return; }
	addr_out (c->a[4], c->a[5], &a);
	c->ret = (uint32_t) r;
}

void api_WSARecvEx16 (struct w16call *c)
{
	uint32_t fl = c->a[3];
	struct w16call d = *c;
	d.a[3] = fl ? rd16 (far_lin (fl)) : 0;
	api_recv16 (&d);
	*c = d;
	if (fl)
		wr16 (far_lin (fl), 0);
}

void api_send16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	int flags = (c->a[3] & 1 ? MSG_OOB : 0);
#ifdef MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	ssize_t r;
	for (;;)
	{
		r = send (s->fd, far_ptr (c->a[1]), (uint16_t) c->a[2], flags);
		if (r >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) || s->nonblock)
			break;
		wait_fd (s->fd, POLLOUT, -1);
	}
	io_done (s, FD_WRITE);
	if (r < 0)
		{ fail_errno (c); return; }
	c->ret = (uint32_t) r;
}

void api_sendto16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	struct sockaddr_in a;
	int has = addr_in (c->a[4], (int16_t) c->a[5], &a);
	ssize_t r;
	for (;;)
	{
		r = has ? sendto (s->fd, far_ptr (c->a[1]), (uint16_t) c->a[2], 0, (struct sockaddr *) &a, sizeof a)
			: send (s->fd, far_ptr (c->a[1]), (uint16_t) c->a[2], 0);
		if (r >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) || s->nonblock)
			break;
		wait_fd (s->fd, POLLOUT, -1);
	}
	io_done (s, FD_WRITE);
	if (r < 0)
		{ fail_errno (c); return; }
	c->ret = (uint32_t) r;
}

void api_shutdown16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	if (shutdown (s->fd, (int16_t) c->a[1]) < 0)
		{ fail_errno (c); return; }
	c->ret = 0;
}

void api_getpeername16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	struct sockaddr_in a;
	socklen_t l = sizeof a;
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	if (getpeername (s->fd, (struct sockaddr *) &a, &l) < 0)
		{ fail_errno (c); return; }
	addr_out (c->a[1], c->a[2], &a);
	c->ret = 0;
}

void api_getsockname16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	struct sockaddr_in a;
	socklen_t l = sizeof a;
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	if (getsockname (s->fd, (struct sockaddr *) &a, &l) < 0)
		{ fail_errno (c); return; }
	addr_out (c->a[1], c->a[2], &a);
	c->ret = 0;
}

/* les options : (niveau, nom) de Winsock -> POSIX ; 0 : inconnue */
static int opt_map (int level, int name, int *hl, int *hn)
{
	if (level == 0xffff)	/* SOL_SOCKET */
	{
		*hl = SOL_SOCKET;
		switch (name)
		{
		case 0x0001: *hn = SO_DEBUG; return 1;
		case 0x0002: *hn = SO_ACCEPTCONN; return 1;
		case 0x0004: *hn = SO_REUSEADDR; return 1;
		case 0x0008: *hn = SO_KEEPALIVE; return 1;
		case 0x0010: *hn = SO_DONTROUTE; return 1;
		case 0x0020: *hn = SO_BROADCAST; return 1;
		case 0x0080: *hn = SO_LINGER; return 1;
		case 0x0100: *hn = SO_OOBINLINE; return 1;
		case 0x1001: *hn = SO_SNDBUF; return 1;
		case 0x1002: *hn = SO_RCVBUF; return 1;
		case 0x1007: *hn = SO_ERROR; return 1;
		case 0x1008: *hn = SO_TYPE; return 1;
		}
		return 0;
	}
	if (level == 6 && name == 1)	/* IPPROTO_TCP, TCP_NODELAY */
	{
		*hl = IPPROTO_TCP;
		*hn = TCP_NODELAY;
		return 1;
	}
	return 0;
}

void api_setsockopt16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	int hl, hn;
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	int level = (uint16_t) c->a[1], name = (uint16_t) c->a[2], len = (int16_t) c->a[4];
	if (!opt_map (level, name, &hl, &hn))
	{
		c->ret = 0;	/* (une option inconnue : sans effet, comme beaucoup de piles de l'époque) */
		return;
	}
	if (hn == SO_LINGER)
	{
		/* struct linger de Winsock : deux u_short */
		struct linger lg = { rd16 (far_lin (c->a[3])), rd16 (far_lin (c->a[3]) + 2) };
		setsockopt (s->fd, hl, hn, &lg, sizeof lg);
		c->ret = 0;
		return;
	}
	int v = len >= 4 ? (int) rd32 (far_lin (c->a[3])) : len >= 2 ? (int16_t) rd16 (far_lin (c->a[3])) : 0;
	if (setsockopt (s->fd, hl, hn, &v, sizeof v) < 0)
		{ fail_errno (c); return; }
	c->ret = 0;
}

void api_getsockopt16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	int hl, hn;
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	int level = (uint16_t) c->a[1], name = (uint16_t) c->a[2];
	uint32_t lenp = c->a[4];
	int cap = lenp ? (int16_t) rd16 (far_lin (lenp)) : 0;
	if (!opt_map (level, name, &hl, &hn))
		{ fail (c, WSAENOPROTOOPT); return; }
	int v = 0;
	socklen_t l = sizeof v;
	if (hn == SO_TYPE)
		v = s->type;
	else if (getsockopt (s->fd, hl, hn, &v, &l) < 0)
		{ fail_errno (c); return; }
	else if (hn == SO_ERROR)
		v = v ? wsa_from_errno (v) : 0;
	if (cap >= 4)
	{
		wr32 (far_lin (c->a[3]), (uint32_t) v);
		wr16 (far_lin (lenp), 4);
	}
	else if (cap >= 2)
	{
		wr16 (far_lin (c->a[3]), (uint16_t) v);
		wr16 (far_lin (lenp), 2);
	}
	c->ret = 0;
}

void api_ioctlsocket16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	uint32_t cmd = c->a[1], argp = c->a[2];
	if (cmd == 0x8004667eu)	/* FIONBIO */
	{
		uint32_t v = argp ? rd32 (far_lin (argp)) : 0;
		if (!v && s->hwnd)
			{ fail (c, WSAEINVAL); return; }	/* (WSAAsyncSelect en cours : non bloquant obligé) */
		s->nonblock = v != 0;
		c->ret = 0;
		return;
	}
	if (cmd == 0x4004667fu)	/* FIONREAD */
	{
		int n = 0;
		if (ioctl (s->fd, FIONREAD, &n) < 0)
			n = 0;
		if (argp)
			wr32 (far_lin (argp), (uint32_t) n);
		c->ret = 0;
		return;
	}
	if (cmd == 0x40047307u)	/* SIOCATMARK : pas de données urgentes */
	{
		if (argp)
			wr32 (far_lin (argp), 1);
		c->ret = 0;
		return;
	}
	fail (c, WSAEINVAL);
}

/* fd_set de Winsock 16 bits : u_short fd_count ; SOCKET (u_short) fd_array[64] */
static int set_to_poll (uint32_t set, short ev, struct pollfd *p, uint16_t *who, int n)
{
	if (!set)
		return n;
	uint32_t b = far_lin (set);
	int cnt = rd16 (b);
	for (int i = 0; i < cnt && i < 64 && n < 3 * 64; i++)
	{
		uint16_t h = rd16 (b + 2 + 2 * (uint32_t) i);
		struct wsock *s = sock_of (h);
		if (!s)
			continue;
		p[n].fd = s->fd;
		p[n].events = ev;
		p[n].revents = 0;
		who[n] = h;
		n++;
	}
	return n;
}

static int set_from_poll (uint32_t set, short ev, struct pollfd *p, uint16_t *who, int from, int to, int listening_read)
{
	if (!set)
		return 0;
	uint32_t b = far_lin (set);
	int k = 0;
	for (int i = from; i < to; i++)
	{
		short r = p[i].revents;
		int ok = (r & ev) || (ev == POLLIN && (r & (POLLHUP | POLLERR)) && !listening_read);
		if (ev == POLLPRI)
			ok = (r & POLLPRI) != 0;
		if (ok)
			wr16 (b + 2 + 2 * (uint32_t) k++, who[i]);
	}
	wr16 (b, (uint16_t) k);
	return k;
}

void api_select16 (struct w16call *c)
{
	NEED_STARTED (c);
	struct pollfd p[3 * 64];
	uint16_t who[3 * 64];
	int n0 = 0, n1 = set_to_poll (c->a[1], POLLIN, p, who, 0);
	int n2 = set_to_poll (c->a[2], POLLOUT, p, who, n1);
	int n3 = set_to_poll (c->a[3], POLLPRI, p, who, n2);
	int ms = -1;
	if (c->a[4])
	{
		uint32_t t = far_lin (c->a[4]);
		ms = (int) rd32 (t) * 1000 + (int) rd32 (t + 4) / 1000;
	}
	int r;
	do
		r = poll (p, (nfds_t) n3, ms);
	while (r < 0 && errno == EINTR);
	if (r < 0)
		{ fail_errno (c); return; }
	int total = set_from_poll (c->a[1], POLLIN, p, who, n0, n1, 0);
	total += set_from_poll (c->a[2], POLLOUT, p, who, n1, n2, 0);
	total += set_from_poll (c->a[3], POLLPRI, p, who, n2, n3, 0);
	c->ret = (uint32_t) total;
}

void api___WSAFDIsSet16 (struct w16call *c)
{
	uint16_t h = (uint16_t) c->a[0];
	c->ret = 0;
	if (!c->a[1])
		return;
	uint32_t b = far_lin (c->a[1]);
	int cnt = rd16 (b);
	for (int i = 0; i < cnt && i < 64; i++)
		if (rd16 (b + 2 + 2 * (uint32_t) i) == h)
		{
			c->ret = 1;
			return;
		}
}

/* ======================================================================== ordre des octets, adresses */
void api_htonl16 (struct w16call *c) { c->ret = htonl (c->a[0]); }
void api_ntohl16 (struct w16call *c) { c->ret = ntohl (c->a[0]); }
void api_htons16 (struct w16call *c) { c->ret = htons ((uint16_t) c->a[0]); }
void api_ntohs16 (struct w16call *c) { c->ret = ntohs ((uint16_t) c->a[0]); }

void api_inet_addr16 (struct w16call *c)
{
	struct in_addr a;
	const char *s = c->a[0] ? w16_raw (c->a[0]) : "";
	c->ret = inet_aton (s, &a) ? a.s_addr : 0xffffffffu;	/* INADDR_NONE */
}

/* les résultats statiques (un par « fil », comme Winsock) : un bloc de mémoire du programme */
static uint16_t static_sel;
static uint32_t static_area (uint32_t off)
{
	if (!static_sel)
		static_sel = global_alloc (2048, 0);
	return MAKESEGPTR (static_sel, off);
}

void api_inet_ntoa16 (struct w16call *c)
{
	struct in_addr a;
	a.s_addr = c->a[0];
	uint32_t p = static_area (0);
	snprintf (far_ptr (p), 16, "%s", inet_ntoa (a));
	c->ret = p;
}

/* ======================================================================== la base des noms */
/* une suite de chaînes et de pointeurs écrite dans [buf, buf + cap) (mémoire 16 bits) ; -> octets employés, ou
 * -1 si trop petit */
struct wbuf { uint32_t seg; int off, cap; };

static int wb_put (struct wbuf *w, const void *d, int n)
{
	if (w->off + n > w->cap)
		return -1;
	memcpy (lin_ptr (far_lin (w->seg) + (uint32_t) w->off), d, (size_t) n);
	int at = w->off;
	w->off += n;
	return at;
}

static uint32_t wb_ptr (struct wbuf *w, int at) { return w->seg + (uint32_t) at; }	/* (seg:off + décalage) */

static void wb_align (struct wbuf *w) { w->off = (w->off + 3) & ~3; }

/* hostent 16 bits : h_name (far), h_aliases (far vers far[]), h_addrtype (short), h_length (short),
 * h_addr_list (far vers far[]) */
static int put_hostent (struct wbuf *w, const struct hostent *h)
{
	int head = wb_put (w, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
	if (head < 0)
		return -1;
	int name = wb_put (w, h->h_name, (int) strlen (h->h_name) + 1);
	wb_align (w);
	int naddr = 0;
	while (h->h_addr_list[naddr] && naddr < 16)
		naddr++;
	int addrs[16];
	for (int i = 0; i < naddr; i++)
		addrs[i] = wb_put (w, h->h_addr_list[i], 4);
	wb_align (w);
	int alist = wb_put (w, "\0\0\0\0", 0);
	for (int i = 0; i < naddr; i++)
	{
		uint32_t p = wb_ptr (w, addrs[i]);
		uint8_t b[4] = { (uint8_t) p, (uint8_t) (p >> 8), (uint8_t) (p >> 16), (uint8_t) (p >> 24) };
		if (wb_put (w, b, 4) < 0)
			return -1;
	}
	if (wb_put (w, "\0\0\0\0", 4) < 0)	/* (fin de h_addr_list) */
		return -1;
	int aliases = wb_put (w, "\0\0\0\0", 4);	/* (aucun alias) */
	if (name < 0 || alist < 0 || aliases < 0)
		return -1;
	uint32_t base = far_lin (w->seg) + (uint32_t) head;
	wr32 (base, wb_ptr (w, name));
	wr32 (base + 4, wb_ptr (w, aliases));
	wr16 (base + 8, 2);	/* AF_INET */
	wr16 (base + 10, 4);
	wr32 (base + 12, wb_ptr (w, alist));
	return w->off;
}

/* servent 16 bits : s_name, s_aliases (far), s_port (short), s_proto (far) */
static int put_servent (struct wbuf *w, const struct servent *s)
{
	int head = wb_put (w, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 14);
	int name = wb_put (w, s->s_name, (int) strlen (s->s_name) + 1);
	int proto = wb_put (w, s->s_proto, (int) strlen (s->s_proto) + 1);
	wb_align (w);
	int aliases = wb_put (w, "\0\0\0\0", 4);
	if (head < 0 || name < 0 || proto < 0 || aliases < 0)
		return -1;
	uint32_t base = far_lin (w->seg) + (uint32_t) head;
	wr32 (base, wb_ptr (w, name));
	wr32 (base + 4, wb_ptr (w, aliases));
	wr16 (base + 8, (uint16_t) s->s_port);	/* (déjà dans l'ordre du réseau) */
	wr32 (base + 10, wb_ptr (w, proto));
	return w->off;
}

/* protoent 16 bits : p_name, p_aliases (far), p_proto (short) */
static int put_protoent (struct wbuf *w, const struct protoent *p)
{
	int head = wb_put (w, "\0\0\0\0\0\0\0\0\0\0", 10);
	int name = wb_put (w, p->p_name, (int) strlen (p->p_name) + 1);
	wb_align (w);
	int aliases = wb_put (w, "\0\0\0\0", 4);
	if (head < 0 || name < 0 || aliases < 0)
		return -1;
	uint32_t base = far_lin (w->seg) + (uint32_t) head;
	wr32 (base, wb_ptr (w, name));
	wr32 (base + 4, wb_ptr (w, aliases));
	wr16 (base + 8, (uint16_t) p->p_proto);
	return w->off;
}

static struct hostent *lookup_host (const char *name)
{
	struct hostent *h = gethostbyname (name);
	if (!h || h->h_addrtype != AF_INET)
		return 0;
	return h;
}

static struct hostent *lookup_addr (uint32_t addr, int len, int type)
{
	if (type != 2 || len < 4)
		return 0;
	return gethostbyaddr (far_ptr (addr), 4, AF_INET);
}

/* le résultat dans la zone statique (après inet_ntoa) -> son adresse 16 bits, 0 si rien */
static uint32_t static_result (int kind, const void *r)
{
	if (!r)
		return 0;
	struct wbuf w = { static_area (32), 0, 2048 - 32 };
	int n = kind == 0 ? put_hostent (&w, r) : kind == 1 ? put_servent (&w, r) : put_protoent (&w, r);
	return n < 0 ? 0 : w.seg;
}

void api_gethostbyname16 (struct w16call *c)
{
	NEED_STARTED (c);
	struct hostent *h = lookup_host (c->a[0] ? w16_raw (c->a[0]) : "");
	c->ret = static_result (0, h);
	if (!c->ret)
		last_error = WSAHOST_NOT_FOUND;
}

void api_gethostbyaddr16 (struct w16call *c)
{
	NEED_STARTED (c);
	c->ret = static_result (0, lookup_addr (c->a[0], (int16_t) c->a[1], (int16_t) c->a[2]));
	if (!c->ret)
		last_error = WSAHOST_NOT_FOUND;
}

void api_gethostname16 (struct w16call *c)
{
	char n[256];
	if (gethostname (n, sizeof n) < 0)
		snprintf (n, sizeof n, "korin");
	int cap = (int16_t) c->a[1];
	if ((int) strlen (n) + 1 > cap)
		{ fail (c, WSAEFAULT); return; }
	memcpy (far_ptr (c->a[0]), n, strlen (n) + 1);
	c->ret = 0;
}

void api_getservbyname16 (struct w16call *c)
{
	NEED_STARTED (c);
	c->ret = static_result (1, getservbyname (w16_raw (c->a[0]), c->a[1] ? w16_raw (c->a[1]) : 0));
	if (!c->ret)
		last_error = WSANO_DATA;
}

void api_getservbyport16 (struct w16call *c)
{
	NEED_STARTED (c);
	c->ret = static_result (1, getservbyport ((int) (uint16_t) c->a[0], c->a[1] ? w16_raw (c->a[1]) : 0));
	if (!c->ret)
		last_error = WSANO_DATA;
}

void api_getprotobyname16 (struct w16call *c)
{
	NEED_STARTED (c);
	c->ret = static_result (2, getprotobyname (w16_raw (c->a[0])));
	if (!c->ret)
		last_error = WSANO_DATA;
}

void api_getprotobynumber16 (struct w16call *c)
{
	NEED_STARTED (c);
	c->ret = static_result (2, getprotobynumber ((int16_t) c->a[0]));
	if (!c->ret)
		last_error = WSANO_DATA;
}

/* ======================================================================== l'asynchrone */
/* WSAAsyncGet* (hwnd, msg, ..., buf (segptr), buflen) : la réponse dans buf, puis le message (wParam : la poignée
 * de la demande ; lParam : la taille employée, l'erreur) */
static void async_reply (struct w16call *c, uint16_t hwnd, uint16_t msg, int kind, const void *r, uint32_t buf,
	int buflen)
{
	uint16_t task = next_task++;
	if (!next_task)
		next_task = 1;
	int err = 0, used = 0;
	if (!r)
		err = kind == 0 ? WSAHOST_NOT_FOUND : WSANO_DATA;
	else
	{
		struct wbuf w = { buf, 0, buflen };
		used = kind == 0 ? put_hostent (&w, r) : kind == 1 ? put_servent (&w, r) : put_protoent (&w, r);
		if (used < 0)
		{
			err = WSAENOBUFS;
			used = 1024;	/* (la taille qu'il faudrait, à peu près) */
		}
	}
	user_post (hwnd, msg, task, (uint32_t) (uint16_t) used | (uint32_t) (uint16_t) err << 16);
	c->ret = task;
}

void api_WSAAsyncGetHostByName16 (struct w16call *c)
{
	NEED_STARTED (c);
	async_reply (c, (uint16_t) c->a[0], (uint16_t) c->a[1], 0, lookup_host (w16_raw (c->a[2])), c->a[3],
		(int16_t) c->a[4]);
}

void api_WSAAsyncGetHostByAddr16 (struct w16call *c)
{
	NEED_STARTED (c);
	async_reply (c, (uint16_t) c->a[0], (uint16_t) c->a[1], 0,
		lookup_addr (c->a[2], (int16_t) c->a[3], (int16_t) c->a[4]), c->a[5], (int16_t) c->a[6]);
}

void api_WSAAsyncGetServByName16 (struct w16call *c)
{
	NEED_STARTED (c);
	async_reply (c, (uint16_t) c->a[0], (uint16_t) c->a[1], 1,
		getservbyname (w16_raw (c->a[2]), c->a[3] ? w16_raw (c->a[3]) : 0), c->a[4], (int16_t) c->a[5]);
}

void api_WSAAsyncGetServByPort16 (struct w16call *c)
{
	NEED_STARTED (c);
	async_reply (c, (uint16_t) c->a[0], (uint16_t) c->a[1], 1,
		getservbyport ((int) (uint16_t) c->a[2], c->a[3] ? w16_raw (c->a[3]) : 0), c->a[4], (int16_t) c->a[5]);
}

void api_WSAAsyncGetProtoByName16 (struct w16call *c)
{
	NEED_STARTED (c);
	async_reply (c, (uint16_t) c->a[0], (uint16_t) c->a[1], 2, getprotobyname (w16_raw (c->a[2])), c->a[3],
		(int16_t) c->a[4]);
}

void api_WSAAsyncGetProtoByNumber16 (struct w16call *c)
{
	NEED_STARTED (c);
	async_reply (c, (uint16_t) c->a[0], (uint16_t) c->a[1], 2, getprotobynumber ((int16_t) c->a[2]), c->a[3],
		(int16_t) c->a[4]);
}

void api_WSACancelAsyncRequest16 (struct w16call *c) { fail (c, WSAEINVAL); }	/* (déjà répondu) */

/* WSAAsyncSelect (s, hwnd, msg, événements) : 0 annule */
void api_WSAAsyncSelect16 (struct w16call *c)
{
	struct wsock *s = sock_of ((uint16_t) c->a[0]);
	if (!s)
		{ fail (c, WSAENOTSOCK); return; }
	uint16_t ev = (uint16_t) c->a[3];
	if (s->hwnd)
		async_count--;
	s->hwnd = ev ? (uint16_t) c->a[1] : 0;
	s->msg = (uint16_t) c->a[2];
	s->events = ev;
	s->armed = ev;
	s->closed_sent = 0;
	s->nonblock = 1;
	if (s->hwnd)
		async_count++;
	c->ret = 0;
}

/* appelé par la boucle des messages : les événements des sockets en WSAAsyncSelect */
void ws_poll (void)
{
	if (!async_count)
		return;
	for (int i = 0; i < MAX_SOCK; i++)
	{
		struct wsock *s = &socks[i];
		if (!s->used || !s->hwnd || !(s->armed & s->events))
			continue;
		struct pollfd p = { s->fd, 0, 0 };
		if (s->armed & (FD_READ | FD_ACCEPT | FD_CLOSE | FD_OOB))
			p.events |= POLLIN | POLLPRI;
		if (s->armed & (FD_WRITE | FD_CONNECT))
			p.events |= POLLOUT;
		if (poll (&p, 1, 0) <= 0)
			continue;
		uint16_t h = (uint16_t) (i + 1);
		if (s->connecting && (p.revents & (POLLOUT | POLLERR | POLLHUP)))
		{
			int err = 0;
			socklen_t l = sizeof err;
			getsockopt (s->fd, SOL_SOCKET, SO_ERROR, &err, &l);
			s->connecting = 0;
			if (s->events & FD_CONNECT)
				user_post (s->hwnd, s->msg, h, FD_CONNECT | (uint32_t) (err ? wsa_from_errno (err) : 0) << 16);
			s->armed &= (uint16_t) ~FD_CONNECT;
			if (err)
				continue;
		}
		if (s->connecting)
			continue;
		if ((p.revents & POLLIN) && s->listening && (s->armed & FD_ACCEPT))
		{
			s->armed &= (uint16_t) ~FD_ACCEPT;
			user_post (s->hwnd, s->msg, h, FD_ACCEPT);
		}
		else if ((p.revents & (POLLIN | POLLHUP)) && !s->listening)
		{
			/* des données, ou la fin : recv (MSG_PEEK) le dit */
			char b;
			ssize_t r = recv (s->fd, &b, 1, MSG_PEEK);
			if (r > 0 && (s->armed & FD_READ))
			{
				s->armed &= (uint16_t) ~FD_READ;
				user_post (s->hwnd, s->msg, h, FD_READ);
			}
			else if (r == 0 && (s->events & FD_CLOSE) && !s->closed_sent)
			{
				s->closed_sent = 1;
				s->armed &= (uint16_t) ~FD_CLOSE;
				user_post (s->hwnd, s->msg, h, FD_CLOSE);
			}
		}
		if ((p.revents & POLLOUT) && (s->armed & FD_WRITE))
		{
			s->armed &= (uint16_t) ~FD_WRITE;
			user_post (s->hwnd, s->msg, h, FD_WRITE);
		}
	}
}

/* la boucle des messages attend au plus ce temps tant que des sockets sont en WSAAsyncSelect */
int ws_wait_ms (int max) { return async_count && max > 20 ? 20 : max; }
