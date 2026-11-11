/*
 * poll.c -- poll, ppoll, select, pselect (libposix, docs/POSIX.md ; reecrit pour ARMPI d'apres
 * poll.c d'Onyx). Les canaux locaux et la memoire partagee (ipc.c) s'attendent dans le noyau
 * (HANDLE_WAIT). Le reste n'a pas d'appel poll : une boucle en espace utilisateur regarde chaque
 * descripteur sans attendre (tubes et sockets de NetKit : lecture sans attente gardee dans le
 * tampon de retenue de la description, accept d'avance d'un socket qui ecoute), puis attend 1 ms.
 * Un bout d'ecriture de tube est toujours pret ; l'entree standard aussi (une lecture attend).
 * Un bout de pty (tty.c) : pret a lire selon vterm (VT_INFO), toujours pret a ecrire.
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
#include <errno.h>
#include <poll.h>
#include <limits.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include "posix_internal.h"

/* ce pour quoi un descripteur est pret maintenant (sans attendre) */
/* bout d'ecriture d'un tube : POLLOUT seulement s'il reste PIPE_BUF octets libres (POSIX ; sinon
   un write bloquant de PIPE_BUF octets attend apres un poll « pret » : communicate () de
   subprocess s'y bloquait), POLLERR sans lecteur. vfs sans V_PIPE_ROOM : toujours pret. */
static short pipe_w_ready (struct __posix_ofd *d)
{
	struct __armpi_msg m;
	memset (&m, 0, 48);
	m.op = V_PIPE_ROOM;
	m.a = d->h;
	long r = __armpi_vfs_call (&m);
	if (r >= PIPE_BUF)
		return POLLOUT | POLLWRNORM;
	if (r >= 0)
		return 0;
	if (r == -32)				/* E_PIPE : plus de lecteur */
		return POLLERR | POLLOUT | POLLWRNORM;
	return POLLOUT | POLLWRNORM;
}

static short ready_now (struct __posix_ofd *d, short ev)
{
	short r = 0;
	switch (d->type)
	{
	case POSIX_FD_PIPE_R:
	case POSIX_FD_SOCKET:
	{
		int f = (ev & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)) || d->type == POSIX_FD_PIPE_R ?
			__posix_carry_fill (d) : 0;
		if (f > 0)
			r |= POLLIN | POLLRDNORM;
		else if (f < 0)
			r |= POLLIN | POLLRDNORM | POLLHUP;
		if (d->type == POSIX_FD_SOCKET)
		{
			if (d->so_error)
				r |= POLLERR;
			if (d->connected || d->sotype == SOCK_DGRAM)
				r |= POLLOUT | POLLWRNORM;
		}
		break;
	}
	case POSIX_FD_PIPE_W:
		r = (ev & (POLLOUT | POLLWRNORM)) ? pipe_w_ready (d) : 0;
		break;
	case POSIX_FD_CONSOLE:
		r = d->console == 0 ? POLLIN : POLLOUT;	/* (une lecture de l'entree standard attend) */
		break;
	case POSIX_FD_TTY:				/* (vterm : octets a lire, ou fin) */
	{
		int f = (ev & (POLLIN | POLLRDNORM)) ? __posix_tty_ready (d) : 0;
		if (f > 0)
			r |= POLLIN | POLLRDNORM;
		else if (f < 0)
			r |= POLLIN | POLLRDNORM | POLLHUP;
		r |= POLLOUT | POLLWRNORM;
		break;
	}
	default:
		r = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
	}
	return (short) (r & (ev | POLLERR | POLLHUP | POLLNVAL));
}

/* les descripteurs du noyau (canaux locaux, memoire partagee : ipc.c) se regardent par
 * HANDLE_WAIT, qui dort dans le noyau jusqu'a un changement */
static int is_handle (struct __posix_ofd *d)
{
	return d && (d->type == POSIX_FD_LSOCK || d->type == POSIX_FD_SHM);
}

#define HPOLL_MAX	64			/* (HANDLE_WAIT : 64 poignees par appel) */

/* poll : seulement des canaux locaux (64 au plus) : une attente du noyau (HANDLE_WAIT) ; sinon
 * une boucle en espace utilisateur : chaque tour regarde sans attendre chaque descripteur (tubes
 * et sockets de NetKit : une lecture sans attente gardee dans le tampon de retenue, un accept
 * d'avance), puis attend 1 ms (sur les canaux locaux s'il y en a : reveil tout de suite) */
int poll (struct pollfd *fds, nfds_t n, int timeout)
{
	if (n > POSIX_FD_MAX)
		return POSIX_ERR (EINVAL);
	if (n == 0)
	{
		if (timeout > 0)
			__posix_sleep_ns ((unsigned long long) timeout * 1000000ULL);
		else if (timeout < 0)
			for (;;)
				__posix_sleep_ns (1000000000ULL);
		return 0;
	}
	struct __posix_ofd **d = (struct __posix_ofd **) calloc (n, sizeof *d);
	if (d == 0)
		return POSIX_ERR (ENOMEM);
	long hh[HPOLL_MAX];
	short hev[HPOLL_MAX], hrev[HPOLL_MAX];
	int hix[HPOLL_MAX];
	int nh = 0, others = 0, bad = 0;
	for (nfds_t i = 0; i < n; i++)
	{
		fds[i].revents = 0;
		if (fds[i].fd < 0)
			continue;
		int e = errno;
		d[i] = __posix_fd_get (fds[i].fd);
		errno = e;
		if (d[i] == 0)
		{
			fds[i].revents = POLLNVAL;
			bad++;
		}
		else if (is_handle (d[i]) && nh < HPOLL_MAX)
		{
			hh[nh] = d[i]->h;
			hev[nh] = fds[i].events;
			hix[nh++] = (int) i;
		}
		else
			others++;
	}
	unsigned long long end = timeout < 0 ? ~0ULL : __posix_mono_ns () + (unsigned long long) timeout * 1000000ULL;
	int r;
	for (;;)
	{
		r = 0;
		for (nfds_t i = 0; i < n; i++)
		{
			if (d[i] != 0 && !is_handle (d[i]))
				fds[i].revents = ready_now (d[i], fds[i].events);
			if (fds[i].revents && !is_handle (d[i]))
				r++;
		}
		/* les canaux : tout de suite, ou toute l'attente s'ils sont seuls */
		int ms = 0;
		if (r == 0 && others == 0 && bad == 0)
		{
			if (timeout < 0)
				ms = -1;
			else
			{
				unsigned long long now = __posix_mono_ns ();
				ms = now >= end ? 0 : (int) ((end - now + 999999ULL) / 1000000ULL);
			}
		}
		if (nh > 0)
		{
			int k = __posix_handle_poll (hh, hev, hrev, nh, ms);
			if (k < 0 && errno != EINTR)
			{
				r = -1;
				break;
			}
			for (int j = 0; j < nh; j++)
			{
				fds[hix[j]].revents = k > 0 ? hrev[j] : 0;
				if (k > 0 && hrev[j])
					r++;
			}
		}
		if (r || timeout == 0 || __posix_mono_ns () >= end)
			break;
		if (others == 0 && bad == 0 && nh > 0)
			continue;		/* (l'attente du noyau a pris le temps) */
		if (nh > 0)
		{
			/* 1 ms d'attente, reveillee par un canal pret */
			int k = __posix_handle_poll (hh, hev, hrev, nh, 1);
			if (k > 0)
				continue;
		}
		else
			__posix_sleep_ns (1000000ULL);
	}
	for (nfds_t i = 0; i < n; i++)
		if (d[i])
			__posix_fd_put (d[i]);
	free (d);
	return r;
}

int ppoll (struct pollfd *fds, nfds_t n, const struct timespec *ts, const sigset_t *mask)
{
	int ms = -1;
	if (ts)
	{
		long long t = (long long) ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000;
		ms = t > 0x7FFFFFFF ? 0x7FFFFFFF : (int) t;
	}
	sigset_t old;
	if (mask)
		sigprocmask (SIG_SETMASK, mask, &old);
	int r = poll (fds, n, ms);
	if (mask)
	{
		int e = errno;
		sigprocmask (SIG_SETMASK, &old, 0);
		errno = e;
	}
	return r;
}

static int do_select (int nfds, fd_set *rd, fd_set *wr, fd_set *ex, int ms)
{
	if (nfds < 0 || nfds > POSIX_FD_MAX)
		return POSIX_ERR (EINVAL);
	struct pollfd *p = (struct pollfd *) calloc (nfds ? (size_t) nfds : 1, sizeof *p);
	if (p == 0)
		return POSIX_ERR (ENOMEM);
	int n = 0;
	for (int fd = 0; fd < nfds; fd++)
	{
		short ev = 0;
		if (rd && FD_ISSET (fd, rd)) ev |= POLLIN;
		if (wr && FD_ISSET (fd, wr)) ev |= POLLOUT;
		if (ex && FD_ISSET (fd, ex)) ev |= POLLPRI;
		if (ev == 0)
			continue;
		p[n].fd = fd;
		p[n].events = ev;
		n++;
	}
	int r = poll (p, (nfds_t) n, ms);
	if (r < 0)
	{
		free (p);
		return -1;
	}
	for (int i = 0; i < n; i++)
		if (p[i].revents & POLLNVAL)
		{
			free (p);
			return POSIX_ERR (EBADF);
		}
	if (rd) FD_ZERO (rd);
	if (wr) FD_ZERO (wr);
	if (ex) FD_ZERO (ex);
	int count = 0;
	for (int i = 0; i < n; i++)
	{
		short re = p[i].revents;
		if (rd && (p[i].events & POLLIN) && (re & (POLLIN | POLLHUP | POLLERR)))
		{
			FD_SET (p[i].fd, rd);
			count++;
		}
		if (wr && (p[i].events & POLLOUT) && (re & (POLLOUT | POLLERR)))
		{
			FD_SET (p[i].fd, wr);
			count++;
		}
		if (ex && (p[i].events & POLLPRI) && (re & POLLPRI))
		{
			FD_SET (p[i].fd, ex);
			count++;
		}
	}
	free (p);
	return count;
}

int select (int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv)
{
	int ms = -1;
	if (tv)
	{
		long long t = (long long) tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;
		ms = t > 0x7FFFFFFF ? 0x7FFFFFFF : (int) t;
	}
	return do_select (nfds, rd, wr, ex, ms);
}

int pselect (int nfds, fd_set *rd, fd_set *wr, fd_set *ex, const struct timespec *ts, const sigset_t *mask)
{
	int ms = -1;
	if (ts)
	{
		long long t = (long long) ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000;
		ms = t > 0x7FFFFFFF ? 0x7FFFFFFF : (int) t;
	}
	sigset_t old;
	if (mask)
		sigprocmask (SIG_SETMASK, mask, &old);
	int r = do_select (nfds, rd, wr, ex, ms);
	if (mask)
	{
		int e = errno;
		sigprocmask (SIG_SETMASK, &old, 0);
		errno = e;
	}
	return r;
}
