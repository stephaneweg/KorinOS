/*
 * epoll.c -- epoll sur poll (libposix, docs/POSIX.md) : ce qu'il faut aux sockets de .NET. Le
 * descripteur d'un epoll est un /dev/null ; ses entrees sont dans une table d'ici, rendue
 * seulement a la fin du process. Fermer un descripteur le retire des tables, comme sous Linux : sinon
 * le socket suivant, qui reprend son numero, recoit EEXIST. Un epoll_wait relit la table par tranches
 * de 5 ms : une entree ajoutee par un autre thread pendant l'attente est vue.
 *
 * EPOLLET : un descripteur est signale quand un evenement apparait, puis redit toutes les 50 ms
 * tant qu'il reste pret. poll ne voit que des etats : sans ce rappel, des donnees lues puis
 * remplacees entre deux tranches ne seraient jamais signalees. Le code ecrit pour EPOLLET relit
 * jusqu'a EAGAIN : un rappel de trop ne lui coute qu'un essai.
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
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "posix_internal.h"

#define EP_MAX		8			/* epoll par process */
#define EP_ENTRIES	256			/* descripteurs par epoll */
#define EP_SLICE_MS	5
#define EP_AGAIN_MS	50

struct ep_entry
{
	int fd;
	struct epoll_event ev;
	unsigned seen;				/* EPOLLET : deja signale */
	long long at;				/* ms du dernier signalement */
};

struct ep
{
	int fd;					/* -1 : libre */
	int n;
	volatile unsigned lock;
	struct ep_entry e[EP_ENTRIES];
};

static struct ep *s_ep[EP_MAX];
static volatile unsigned s_lock;

static struct ep *ep_of (int fd)
{
	for (int i = 0; i < EP_MAX; i++)
		if (s_ep[i] != 0 && s_ep[i]->fd == fd)
			return s_ep[i];
	return 0;
}

static void ep_forget (int fd)
{
	for (int i = 0; i < EP_MAX; i++)
	{
		struct ep *p = s_ep[i];
		if (p == 0)
			continue;
		__posix_lock (&p->lock);
		for (int j = 0; j < p->n; j++)
			if (p->e[j].fd == fd)
			{
				p->e[j] = p->e[--p->n];
				break;
			}
		__posix_unlock (&p->lock);
	}
}

int epoll_create1 (int flags)
{
	struct ep *p = (struct ep *) calloc (1, sizeof *p);
	if (p == 0)
		return POSIX_ERR (ENOMEM);
	__posix_fd_closing = ep_forget;
	int fd = open ("/dev/null", O_RDONLY | ((flags & EPOLL_CLOEXEC) ? O_CLOEXEC : 0));
	if (fd < 0)
	{
		free (p);
		return -1;
	}
	p->fd = fd;
	__posix_lock (&s_lock);
	for (int i = 0; i < EP_MAX; i++)
		if (s_ep[i] == 0)
		{
			s_ep[i] = p;
			__posix_unlock (&s_lock);
			return fd;
		}
	__posix_unlock (&s_lock);
	close (fd);
	free (p);
	return POSIX_ERR (EMFILE);
}

int epoll_create (int size) { return size > 0 ? epoll_create1 (0) : POSIX_ERR (EINVAL); }

int epoll_ctl (int epfd, int op, int fd, struct epoll_event *ev)
{
	struct ep *p = ep_of (epfd);
	if (p == 0)
		return POSIX_ERR (EBADF);
	if (op != EPOLL_CTL_DEL && ev == 0)
		return POSIX_ERR (EFAULT);
	int r = 0, i;
	__posix_lock (&p->lock);
	for (i = 0; i < p->n; i++)
		if (p->e[i].fd == fd)
			break;
	if (op == EPOLL_CTL_ADD)
	{
		if (i < p->n)
			r = EEXIST;
		else if (p->n == EP_ENTRIES)
			r = ENOSPC;
		else
		{
			memset (&p->e[i], 0, sizeof p->e[i]);
			p->e[i].fd = fd;
			p->e[i].ev = *ev;
			p->n++;
		}
	}
	else if (i == p->n)
		r = ENOENT;
	else if (op == EPOLL_CTL_MOD)
	{
		p->e[i].ev = *ev;
		p->e[i].seen = 0;
	}
	else if (op == EPOLL_CTL_DEL)
		p->e[i] = p->e[--p->n];
	else
		r = EINVAL;
	__posix_unlock (&p->lock);
	return r ? POSIX_ERR (r) : 0;
}

int epoll_wait (int epfd, struct epoll_event *out, int max, int timeout)
{
	struct ep *p = ep_of (epfd);
	if (p == 0)
		return POSIX_ERR (EBADF);
	if (max <= 0)
		return POSIX_ERR (EINVAL);
	static __thread struct pollfd *t_pfd;
	if (t_pfd == 0 && (t_pfd = (struct pollfd *) malloc (EP_ENTRIES * sizeof *t_pfd)) == 0)
		return POSIX_ERR (ENOMEM);
	long long end = timeout < 0 ? -1 : (long long) (__posix_mono_ns () / 1000000) + timeout;
	for (;;)
	{
		__posix_lock (&p->lock);
		int n = p->n;
		for (int i = 0; i < n; i++)
		{
			t_pfd[i].fd = p->e[i].fd;
			t_pfd[i].events = 0;
			if (p->e[i].ev.events & EPOLLIN)
				t_pfd[i].events |= POLLIN;
			if (p->e[i].ev.events & EPOLLOUT)
				t_pfd[i].events |= POLLOUT;
			t_pfd[i].revents = 0;
		}
		__posix_unlock (&p->lock);

		int r = poll (t_pfd, (nfds_t) n, EP_SLICE_MS);
		if (r < 0)
			return -1;
		long long now = (long long) (__posix_mono_ns () / 1000000);
		int got = 0;
		__posix_lock (&p->lock);
		for (int i = 0; i < n && got < max; i++)
		{
			if (t_pfd[i].revents == 0)
				continue;
			int j;
			for (j = 0; j < p->n; j++)
				if (p->e[j].fd == t_pfd[i].fd)
					break;
			if (j == p->n)
				continue;
			struct ep_entry *e = &p->e[j];
			unsigned ev = 0;
			if (t_pfd[i].revents & POLLIN)
				ev |= EPOLLIN;
			if (t_pfd[i].revents & POLLOUT)
				ev |= EPOLLOUT;
			if (t_pfd[i].revents & POLLERR)
				ev |= EPOLLERR;
			if (t_pfd[i].revents & POLLHUP)
				ev |= EPOLLHUP;
			if (e->ev.events & EPOLLET)
			{
				if ((ev & ~e->seen) == 0 && now - e->at < EP_AGAIN_MS)
				{
					e->seen = ev;
					continue;
				}
				e->seen = ev;
				e->at = now;
			}
			out[got].events = ev;
			out[got].data = e->ev.data;
			got++;
		}
		__posix_unlock (&p->lock);
		if (got > 0)
			return got;
		if (timeout == 0 || (end >= 0 && now >= end))
			return 0;
	}
}
