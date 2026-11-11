/*
 * tty.c -- les terminaux de libposix (docs/POSIX.md, docs/VTERM.md) : les bouts de
 * pseudo-terminal du service vterm.
 *
 *  - Un descripteur POSIX_FD_TTY tient une poignee de vterm (VT_TAG, src/inc/vterm.inc). Au
 *    demarrage, les entrees / sorties standard qui sont des bouts de vterm (V_STDIO de vfs,
 *    rendus tels quels) deviennent des descripteurs TTY ; les autres restent ceux d'AppKit
 *    (console ou tube). isatty n'est vrai que pour eux.
 *  - read : vterm rend E_AGAIN au bout de son delai (1 s) ou E_INTR (signal de la discipline) ;
 *    entre deux essais, un signal livre a ce process (compteur des gestionnaires, signal.c)
 *    rend EINTR. write : jusqu'au bout (pty plein : 1 ms d'attente).
 *  - Controle des taches (docs/VTERM.md) : en arriere-plan, vterm envoie SIGTTIN / SIGTTOU au
 *    groupe et rend E_AGAIN (lecture) ou E_INTR ; le process s'arrete au retour de l'appel, puis,
 *    repris, la requete est refaite (sauf si un gestionnaire a tourne : EINTR). E_IO : SIGTTIN ignore ou masque,
 *    groupe orphelin. TIOCSCTTY, TIOCNOTTY, tcgetsid : le terminal de controle (vterm).
 *  - termios, taille, groupe de premier plan : ops de vterm (tcgetattr, tcsetattr, tcflush,
 *    tcgetpgrp, tcsetpgrp, ioctl TIOCGWINSZ / TIOCSWINSZ / TIOCGPGRP / TIOCSPGRP / FIONREAD /
 *    TCGETS / TCSETS* / TIOCGPTN).
 *  - posix_openpt (et open de /dev/ptmx) : un pty neuf, le bout maitre ; le bout esclave reste
 *    tenu par ce process tant que le maitre est ouvert. ptsname : /dev/pts/N ; open de
 *    /dev/pts/N : l'esclave (VT_SLAVE). /dev/tty : le terminal des entrees / sorties standard.
 *  - Une poignee est rendue a vterm (VT_CLOSE) quand plus aucune description de ce process ne
 *    la tient (table s_refs).
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence: Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions: The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "posix_internal.h"

#define VT_TAG		0x40000000L		/* src/inc/vterm.inc */
#define VT_OPEN		1
#define VT_READ		2
#define VT_WRITE	3
#define VT_CLOSE	4
#define VT_TCGETS	5
#define VT_TCSETS	6
#define VT_GWINSZ	7
#define VT_SWINSZ	8
#define VT_GPGRP	9
#define VT_SPGRP	10
#define VT_INFO		11
#define VT_FLUSH	12
#define VT_SLAVE	13
#define VT_SCTTY	14
#define VT_NOTTY	15
#define VT_GSID		16
#define VT_NOWAIT	1
#define VTT_TAILLE	48
#define E_AGAIN_	(-11)
#define E_INTR_		(-4)

extern volatile unsigned __posix_sig_count;	/* signal.c : gestionnaires lances */

static long vt_call (struct __armpi_msg *m)
{
	long c = __armpi_sys2 (SYS_SVC_OPEN, "vterm", 0);
	if (c < 0)
		return -9;				/* (EBADF : pas de vterm) */
	return __armpi_sys2 (SYS_SVC_CALL, c, m);
}

static long vt_op (long op, long h, long b, long c, long d, struct __armpi_msg *m)
{
	memset (m, 0, 48);
	m->op = op;
	m->a = h;
	m->b = b;
	m->c = c;
	m->d = d;
	return vt_call (m);
}

/* une op que vterm peut refuser a un process en arriere-plan (E_INTR : SIGTTOU envoye a son
 * groupe, il s'est arrete au retour) : refaite, sauf si un gestionnaire a tourne */
static long vt_call_job (struct __armpi_msg *m)
{
	unsigned c0 = __posix_sig_count;
	struct __armpi_msg keep;
	memcpy (&keep, m, 48 + (m->len < sizeof keep.data ? m->len : sizeof keep.data));
	for (;;)
	{
		long r = vt_call (m);
		if (r != E_INTR_ || __posix_sig_count != c0)
			return r;
		memcpy (m, &keep, 48 + (keep.len < sizeof keep.data ? keep.len : sizeof keep.data));
	}
}

static long vt_op_job (long op, long h, long b, struct __armpi_msg *m)
{
	memset (m, 0, 48);
	m->op = op;
	m->a = h;
	m->b = b;
	return vt_call_job (m);
}

/* ---- references des poignees tenues par ce process ---- */
#define REFS_MAX	32
static struct { long h; int n; } s_refs[REFS_MAX];
static volatile unsigned s_refLock;

static void ref_add (long h)
{
	__posix_lock (&s_refLock);
	int k = -1;
	for (int i = 0; i < REFS_MAX; i++)
		if (s_refs[i].n && s_refs[i].h == h)
		{
			s_refs[i].n++;
			k = -2;
			break;
		}
		else if (s_refs[i].n == 0 && k == -1)
			k = i;
	if (k >= 0)
	{
		s_refs[k].h = h;
		s_refs[k].n = 1;
	}
	__posix_unlock (&s_refLock);
}

static void ref_drop (long h)
{
	int last = 0;
	__posix_lock (&s_refLock);
	for (int i = 0; i < REFS_MAX; i++)
		if (s_refs[i].n && s_refs[i].h == h)
		{
			last = --s_refs[i].n == 0;
			break;
		}
	__posix_unlock (&s_refLock);
	if (last)
	{
		struct __armpi_msg m;
		vt_op (VT_CLOSE, h, 0, 0, 0, &m);
	}
}

static struct __posix_ofd *tty_new (long h, int flags, long slave)
{
	struct __posix_ofd *d = __posix_ofd_new (POSIX_FD_TTY, flags);
	if (d == 0)
		return 0;
	d->h = h;
	d->pending = slave;			/* (maitre de posix_openpt : son esclave, tenu) */
	ref_add (h);
	if (slave)
		ref_add (slave);
	return d;
}

struct __posix_ofd *__posix_tty_adopt (long h, int flags, long slave)
{
	return tty_new (h, flags, slave);
}

void __posix_tty_release (struct __posix_ofd *d)
{
	ref_drop ((long) d->h);
	if (d->pending)
		ref_drop ((long) d->pending);
}

/* ---- demarrage : les entrees / sorties standard ---- */
static long s_std[3];

/* fd.c : la description de l'entree / sortie standard i (TTY si c'est un bout de vterm) */
struct __posix_ofd *__posix_std_ofd (int i)
{
	static int done;
	if (!done)
	{
		done = 1;
		long f = __armpi_sys0 (SYS_PROC_FLAGS);
		if (f >= 0 && (f & 7))
		{
			struct __armpi_msg m;
			memset (&m, 0, 48);
			m.op = V_STDIO;
			if (__armpi_vfs_call (&m) == 0)
			{
				long v[3] = { m.a, m.b, m.c };
				for (int k = 0; k < 3; k++)
					if (v[k] > 0 && (v[k] & VT_TAG))
						s_std[k] = v[k];
			}
		}
	}
	if (i >= 0 && i < 3 && s_std[i])
		return tty_new (s_std[i], O_RDWR, 0);
	return 0;
}

int __posix_tty_stdio (void)
{
	return s_std[0] || s_std[1] || s_std[2];
}

/* ---- lecture, ecriture ---- */
ssize_t __posix_tty_read (struct __posix_ofd *d, void *buf, size_t n)
{
	if (n == 0)
		return 0;
	if (n > ARMPI_PIPE_CHUNK)
		n = ARMPI_PIPE_CHUNK;
	unsigned c0 = __posix_sig_count;
	struct __armpi_msg m;
	for (;;)
	{
		int nb = (d->flags & O_NONBLOCK) != 0;
		long r = vt_op (VT_READ, d->h, (long) n, nb ? VT_NOWAIT : 0, 0, &m);
		if (r == E_AGAIN_ || r == E_INTR_)
		{
			if (__posix_sig_count != c0)
				return POSIX_ERR (EINTR);
			if (nb)
				return POSIX_ERR (EAGAIN);
			continue;
		}
		if (r > 0)
			memcpy (buf, m.data, (size_t) r);
		return r < 0 ? POSIX_SYS (r) : r;
	}
}

ssize_t __posix_tty_write (struct __posix_ofd *d, const void *buf, size_t n)
{
	struct __armpi_msg m;
	size_t done = 0;
	unsigned c0 = __posix_sig_count;
	while (done < n)
	{
		size_t c = n - done;
		if (c > ARMPI_PIPE_CHUNK)
			c = ARMPI_PIPE_CHUNK;
		memset (&m, 0, 48);
		m.op = VT_WRITE;
		m.a = d->h;
		m.len = (unsigned) c;
		memcpy (m.data, (const char *) buf + done, c);
		long r = vt_call (&m);
		if (r == E_INTR_)			/* TOSTOP en arriere-plan : arrete, repris */
		{
			if (__posix_sig_count == c0)
				continue;
			if (done)
				break;
			return POSIX_ERR (EINTR);
		}
		if (r < 0)
		{
			if (done)
				break;
			return POSIX_SYS (r);		/* EIO : l'autre cote est parti */
		}
		if (r == 0)
		{
			if (d->flags & O_NONBLOCK)
				return done ? (ssize_t) done : POSIX_ERR (EAGAIN);
			__armpi_sys1 (SYS_SLEEP, 1);
			continue;
		}
		done += (size_t) r;
	}
	return (ssize_t) done;
}

/* poll : -> 1 donnees a lire, -1 fin (l'autre cote est parti), 0 rien */
int __posix_tty_ready (struct __posix_ofd *d)
{
	struct __armpi_msg m;
	if (vt_op (VT_INFO, d->h, 0, 0, 0, &m) < 0)
		return -1;
	if (m.c > 0)
		return 1;
	return m.d ? -1 : 0;
}

/* ---- ouvertures : /dev/tty, /dev/ptmx, /dev/pts/N ---- */
int posix_openpt (int flags)
{
	struct __armpi_msg m;
	long r = vt_op (VT_OPEN, 0, 0, 0, 0, &m);
	if (r < 0)
		return (int) POSIX_SYS (r);
	struct __posix_ofd *d = tty_new (m.a, O_RDWR | (flags & O_NONBLOCK), m.b);
	if (d == 0)
	{
		vt_op (VT_CLOSE, m.a, 0, 0, 0, &m);
		return -1;
	}
	return __posix_fd_install (d, 0, (flags & O_CLOEXEC) != 0);
}

/* file.c : -> descripteur, -1 (errno), ou -2 : pas un chemin de terminal */
int __posix_tty_open (const char *path, int flags)
{
	if (strncmp (path, "/dev/", 5) != 0)
		return -2;
	const char *p = path + 5;
	if (!strcmp (p, "ptmx"))
		return posix_openpt (flags);
	long h = 0;
	if (!strncmp (p, "pts/", 4))
	{
		char *end;
		long n = strtol (p + 4, &end, 10);
		if (end == p + 4 || *end)
			return POSIX_ERR (ENOENT);
		struct __armpi_msg m;
		long r = vt_op (VT_SLAVE, 0, n, 0, 0, &m);
		if (r < 0)
			return (int) POSIX_SYS (r);
		h = m.a;
		struct __posix_ofd *d = tty_new (h, (flags & (O_ACCMODE | O_NONBLOCK)), 0);
		if (d == 0)
			return -1;
		return __posix_fd_install (d, 0, (flags & O_CLOEXEC) != 0);
	}
	if (!strcmp (p, "tty"))
	{
		for (int i = 0; i < 3 && !h; i++)
		{
			struct __posix_ofd *s = __posix_fd_get (i);
			if (s)
			{
				if (s->type == POSIX_FD_TTY)
					h = (long) s->h;
				__posix_fd_put (s);
			}
		}
		if (!h)
			return -2;			/* (pas de terminal : la console, comme avant) */
		struct __posix_ofd *d = tty_new (h, (flags & (O_ACCMODE | O_NONBLOCK)), 0);
		if (d == 0)
			return -1;
		return __posix_fd_install (d, 0, (flags & O_CLOEXEC) != 0);
	}
	return -2;
}

/* ---- isatty, termios ---- */
static struct __posix_ofd *tty_get (int fd)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return 0;
	if (d->type != POSIX_FD_TTY)
	{
		__posix_fd_put (d);
		errno = ENOTTY;
		return 0;
	}
	return d;
}

int _isatty (int fd)
{
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return 0;
	__posix_fd_put (d);
	return 1;
}
int isatty (int fd) { return _isatty (fd); }

static int pty_number (struct __posix_ofd *d)
{
	struct __armpi_msg m;
	return vt_op (VT_INFO, d->h, 0, 0, 0, &m) < 0 ? -1 : (int) m.b;
}

int ptsname_r (int fd, char *b, size_t n)
{
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return errno;
	int k = pty_number (d);
	__posix_fd_put (d);
	if (k < 0)
		return ENOTTY;
	if ((size_t) snprintf (b, n, "/dev/pts/%d", k) >= n)
		return ERANGE;
	return 0;
}

char *ptsname (int fd)
{
	static char b[32];
	int e = ptsname_r (fd, b, sizeof b);
	if (e)
	{
		errno = e;
		return 0;
	}
	return b;
}

int ttyname_r (int fd, char *b, size_t n) { return ptsname_r (fd, b, n); }
char *ttyname (int fd)
{
	static char b[32];
	int e = ptsname_r (fd, b, sizeof b);
	if (e)
	{
		errno = e;
		return 0;
	}
	return b;
}

int grantpt (int fd) { return _isatty (fd) ? 0 : -1; }
int unlockpt (int fd) { return _isatty (fd) ? 0 : -1; }

static int get_attr (struct __posix_ofd *d, struct termios *t)
{
	struct __armpi_msg m;
	long r = vt_op (VT_TCGETS, d->h, 0, 0, 0, &m);
	if (r < 0)
		return (int) POSIX_SYS (r);
	memset (t, 0, sizeof *t);
	memcpy (&t->c_iflag, m.data, 4);
	memcpy (&t->c_oflag, m.data + 4, 4);
	memcpy (&t->c_cflag, m.data + 8, 4);
	memcpy (&t->c_lflag, m.data + 12, 4);
	memcpy (t->c_cc, m.data + 16, NCCS);
	t->c_ispeed = t->c_ospeed = B38400;
	return 0;
}

static int set_attr (struct __posix_ofd *d, int act, const struct termios *t)
{
	struct __armpi_msg m;
	memset (&m, 0, 48);
	m.op = VT_TCSETS;
	m.a = d->h;
	m.b = act;
	m.len = VTT_TAILLE;
	memcpy (m.data, &t->c_iflag, 4);
	memcpy (m.data + 4, &t->c_oflag, 4);
	memcpy (m.data + 8, &t->c_cflag, 4);
	memcpy (m.data + 12, &t->c_lflag, 4);
	memcpy (m.data + 16, t->c_cc, NCCS);
	long r = vt_call_job (&m);
	return r < 0 ? (int) POSIX_SYS (r) : 0;
}

int tcgetattr (int fd, struct termios *t)
{
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return -1;
	int r = get_attr (d, t);
	__posix_fd_put (d);
	return r;
}

int tcsetattr (int fd, int act, const struct termios *t)
{
	if (act != TCSANOW && act != TCSADRAIN && act != TCSAFLUSH)
		return POSIX_ERR (EINVAL);
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return -1;
	int r = set_attr (d, act, t);
	__posix_fd_put (d);
	return r;
}

pid_t tcgetpgrp (int fd)
{
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return -1;
	struct __armpi_msg m;
	long r = vt_op (VT_GPGRP, d->h, 0, 0, 0, &m);
	__posix_fd_put (d);
	return r < 0 ? (pid_t) POSIX_SYS (r) : (pid_t) m.a;
}

int tcsetpgrp (int fd, pid_t pg)
{
	if (pg <= 0)
		return POSIX_ERR (EINVAL);
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return -1;
	struct __armpi_msg m;
	long r = vt_op_job (VT_SPGRP, d->h, pg, &m);
	__posix_fd_put (d);
	return r < 0 ? (int) POSIX_SYS (r) : 0;
}

/* la session du terminal ; un pty sans session (personne n'a fait TIOCSCTTY) est celui de tout
 * le monde : la session de l'appelant */
pid_t tcgetsid (int fd)
{
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return -1;
	struct __armpi_msg m;
	long r = vt_op (VT_GSID, d->h, 0, 0, 0, &m);
	__posix_fd_put (d);
	if (r < 0)
		return (pid_t) POSIX_SYS (r);
	return m.a ? (pid_t) m.a : getsid (0);
}

int tcflush (int fd, int q)
{
	struct __posix_ofd *d = tty_get (fd);
	if (d == 0)
		return -1;
	struct __armpi_msg m;
	long r = vt_op_job (VT_FLUSH, d->h, q == TCIFLUSH ? 0 : q == TCOFLUSH ? 1 : 2, &m);
	__posix_fd_put (d);
	return r < 0 ? (int) POSIX_SYS (r) : 0;
}

int tcdrain (int fd) { return _isatty (fd) ? 0 : -1; }
int tcsendbreak (int fd, int len) { (void) len; return _isatty (fd) ? 0 : -1; }
int tcflow (int fd, int act) { (void) act; return _isatty (fd) ? 0 : -1; }

/* misc.c : ioctl d'un descripteur TTY (d tenu par l'appelant) */
int __posix_tty_ioctl (struct __posix_ofd *d, unsigned long req, void *arg)
{
	struct __armpi_msg m;
	long r;
	switch (req)
	{
	case TIOCGWINSZ:
	{
		r = vt_op (VT_GWINSZ, d->h, 0, 0, 0, &m);
		if (r < 0)
			return (int) POSIX_SYS (r);
		struct winsize *w = (struct winsize *) arg;
		if (w)
		{
			w->ws_row = (unsigned short) m.a;
			w->ws_col = (unsigned short) m.b;
			w->ws_xpixel = (unsigned short) m.c;
			w->ws_ypixel = (unsigned short) m.d;
		}
		return 0;
	}
	case TIOCSWINSZ:
	{
		const struct winsize *w = (const struct winsize *) arg;
		if (w == 0)
			return POSIX_ERR (EFAULT);
		r = vt_op (VT_SWINSZ, d->h, w->ws_row, w->ws_col, w->ws_xpixel | ((long) w->ws_ypixel << 16), &m);
		return r < 0 ? (int) POSIX_SYS (r) : 0;
	}
	case TIOCGPGRP:
		r = vt_op (VT_GPGRP, d->h, 0, 0, 0, &m);
		if (r < 0)
			return (int) POSIX_SYS (r);
		if (arg)
			*(pid_t *) arg = (pid_t) m.a;
		return 0;
	case TIOCSPGRP:
		if (arg == 0)
			return POSIX_ERR (EFAULT);
		r = vt_op_job (VT_SPGRP, d->h, *(const pid_t *) arg, &m);
		return r < 0 ? (int) POSIX_SYS (r) : 0;
	case FIONREAD:
		r = vt_op (VT_INFO, d->h, 0, 0, 0, &m);
		if (r < 0)
			return (int) POSIX_SYS (r);
		if (arg)
			*(int *) arg = (int) m.c;
		return 0;
	case TIOCGPTN:
		r = vt_op (VT_INFO, d->h, 0, 0, 0, &m);
		if (r < 0)
			return (int) POSIX_SYS (r);
		if (arg)
			*(unsigned *) arg = (unsigned) m.b;
		return 0;
	case TCGETS:
		return arg ? get_attr (d, (struct termios *) arg) : POSIX_ERR (EFAULT);
	case TCSETS:
	case TCSETSW:
	case TCSETSF:
		return arg ? set_attr (d, (int) (req - TCSETS), (const struct termios *) arg) : POSIX_ERR (EFAULT);
	case TCFLSH:
		r = vt_op_job (VT_FLUSH, d->h, (long) arg, &m);
		return r < 0 ? (int) POSIX_SYS (r) : 0;
	case TIOCSCTTY:
		r = vt_op (VT_SCTTY, d->h, (long) arg, 0, 0, &m);
		return r < 0 ? (int) POSIX_SYS (r) : 0;
	case TIOCNOTTY:
		r = vt_op (VT_NOTTY, d->h, 0, 0, 0, &m);
		return r < 0 ? (int) POSIX_SYS (r) : 0;
	case TIOCSPTLCK:
		return 0;
	}
	return POSIX_ERR (EINVAL);
}

/* openpty (pty.c) : l'esclave vient d'etre ouvert par son chemin ; le maitre lache celui qu'il
   tenait depuis posix_openpt, pour que la fin du dernier esclave se voie (read du maitre : EIO,
   poll : POLLHUP) quand le fils de forkpty s'arrete */
void __posix_tty_unhold (int master)
{
	struct __posix_ofd *d = tty_get (master);
	if (d == 0)
		return;
	long s = (long) d->pending;
	d->pending = 0;
	__posix_fd_put (d);
	if (s)
		ref_drop (s);
}
