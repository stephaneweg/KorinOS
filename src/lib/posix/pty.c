/*
 * pty.c -- openpty, forkpty, login_tty (pty.h) : la paire maitre / esclave d'un pseudo-terminal
 * de vterm (posix_openpt, ptsname, open de /dev/pts/N : tty.c), puis, dans le fils de forkpty,
 * une session neuve dont l'esclave est le terminal de controle et les entrees / sorties
 * standard. Ajoute pour CPython (os.openpty, pty.fork), docs/PYTHON-PORT.md.
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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pty.h>
#include "posix_internal.h"

int openpty (int *amaster, int *aslave, char *name, const struct termios *termp,
	     const struct winsize *winp)
{
	char path[64];
	int m = posix_openpt (O_RDWR | O_NOCTTY);
	if (m < 0)
		return -1;
	int e = 0;
	if (grantpt (m) != 0 || unlockpt (m) != 0)
		e = errno ? errno : EIO;
	else if ((e = ptsname_r (m, path, sizeof path)) == 0)
	{
		int s = open (path, O_RDWR | O_NOCTTY);
		if (s < 0)
			e = errno;
		else
		{
			__posix_tty_unhold (m);
			if (termp)
				tcsetattr (s, TCSANOW, termp);
			if (winp)
				ioctl (s, TIOCSWINSZ, winp);
			if (name)
				strcpy (name, path);	/* (comme la glibc : le tampon de l'appelant suffit) */
			*amaster = m;
			*aslave = s;
			return 0;
		}
	}
	close (m);
	errno = e;
	return -1;
}

int login_tty (int fd)
{
	setsid ();
	if (ioctl (fd, TIOCSCTTY, 0) != 0)
		return -1;
	for (int i = 0; i < 3; i++)
		if (dup2 (fd, i) < 0)
			return -1;
	if (fd > 2)
		close (fd);
	return 0;
}

pid_t forkpty (int *amaster, char *name, const struct termios *termp, const struct winsize *winp)
{
	int m, s;
	if (openpty (&m, &s, name, termp, winp) != 0)
		return -1;
	pid_t pid = fork ();
	if (pid < 0)
	{
		int e = errno;
		close (m);
		close (s);
		errno = e;
		return -1;
	}
	if (pid == 0)
	{
		close (m);
		if (login_tty (s) != 0)
			_exit (1);
		return 0;
	}
	close (s);
	*amaster = m;
	return pid;
}
