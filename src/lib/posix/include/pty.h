/*
 * pty.h -- openpty, forkpty, login_tty (comme la glibc et les BSD), sur les pseudo-terminaux de
 * vterm (posix_openpt, tty.c). Code : pty.c.
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence (see tty.c).
 */
#ifndef _PTY_H
#define _PTY_H

#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int openpty (int *__amaster, int *__aslave, char *__name, const struct termios *__termp,
	     const struct winsize *__winp);
pid_t forkpty (int *__amaster, char *__name, const struct termios *__termp,
	       const struct winsize *__winp);
int login_tty (int __fd);

#ifdef __cplusplus
}
#endif

#endif /* _PTY_H */
