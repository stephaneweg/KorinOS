/*
 * newlib_syscalls.c -- la couche d'appels sur laquelle repose newlib, pour les programmes
 * construits sur libposix (docs/POSIX.md ; repris d'Onyx).
 *
 * Les souches passent par la table des descripteurs (fd.c) : _read / _write / _lseek / _fstat /
 * _close sur tout descripteur (fichiers d'AppKit, tubes de vfs, sockets de NetKit, pseudo-
 * fichiers de /dev, entree / sortie standard). _open est open (file.c) ; _stat / _unlink / _link
 * (stat.c), _gettimeofday / _times (time.c), _getpid / _wait / _fork / _execve (proc.c), _kill
 * (signal.c) et _isatty (misc.c) sont avec leur famille. Ici : _sbrk (SYS_SBRK), _exit
 * (SYS_EXIT) et les verrous de newlib (verrouillage reciblable, sur le mutex par futex).
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/lock.h>
#include "posix_internal.h"

/* crtbegin.o (pas lie : crt0posix est le depart) le definit ; atexit / __cxa_atexit s'en servent */
void *__dso_handle = 0;

/* ---- memoire ---- */
void *_sbrk (ptrdiff_t incr)
{
	long prev = __armpi_sys1 (SYS_SBRK, incr);
	if (prev < 0)
	{
		errno = ENOMEM;
		return (void *) -1;
	}
	return (void *) prev;
}

/* ---- descripteurs ---- */
int _read (int fd, void *buf, size_t n)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	ssize_t r = __posix_read (d, buf, n);
	__posix_fd_put (d);
	return (int) r;
}

int _write (int fd, const void *buf, size_t n)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	ssize_t r = __posix_write (d, buf, n);
	__posix_fd_put (d);
	return (int) r;
}

off_t _lseek (int fd, off_t off, int whence)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	off_t r = __posix_lseek (d, off, whence);
	__posix_fd_put (d);
	return r;
}

int _fstat (int fd, struct stat *st)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	int r = __posix_fstat (d, st);
	__posix_fd_put (d);
	return r;
}

int _close (int fd) { return __posix_fd_close (fd); }
int _open (const char *name, int flags, int mode) { return __posix_open (name, flags, mode); }

/* ---- la fin ---- */
void _exit (int code)
{
	__armpi_sys1 (SYS_EXIT, code);
	for (;;)
		;
}

void _Exit (int code) { _exit (code); }

/* ---- newlib's locks (retargetable locking): the futex mutex ---- */
struct __lock { volatile unsigned lk; volatile int owner; int count; };

struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___dd_hash_mutex;
struct __lock __lock___arc4random_mutex;

void __retarget_lock_init (_LOCK_T *lock) { *lock = (_LOCK_T) calloc (1, sizeof (struct __lock)); }
void __retarget_lock_init_recursive (_LOCK_T *lock) { __retarget_lock_init (lock); }
void __retarget_lock_close (_LOCK_T lock) { free (lock); }
void __retarget_lock_close_recursive (_LOCK_T lock) { free (lock); }

void __retarget_lock_acquire (_LOCK_T lock) { if (lock) __posix_lock (&lock->lk); }
void __retarget_lock_release (_LOCK_T lock) { if (lock) __posix_unlock (&lock->lk); }
int __retarget_lock_try_acquire (_LOCK_T lock)
{
	unsigned z = 0;
	return lock == 0 || __atomic_compare_exchange_n (&lock->lk, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void __retarget_lock_acquire_recursive (_LOCK_T lock)
{
	if (lock == 0)
		return;
	int me = __posix_owner_id ();
	if (lock->count > 0 && lock->owner == me)
	{
		lock->count++;
		return;
	}
	__posix_lock (&lock->lk);
	lock->owner = me;
	lock->count = 1;
}

int __retarget_lock_try_acquire_recursive (_LOCK_T lock)
{
	if (lock == 0)
		return 1;
	int me = __posix_owner_id ();
	if (lock->count > 0 && lock->owner == me)
	{
		lock->count++;
		return 1;
	}
	if (!__retarget_lock_try_acquire (lock))
		return 0;
	lock->owner = me;
	lock->count = 1;
	return 1;
}

void __retarget_lock_release_recursive (_LOCK_T lock)
{
	if (lock == 0 || lock->count == 0)
		return;
	if (--lock->count == 0)
	{
		lock->owner = 0;
		__posix_unlock (&lock->lk);
	}
}

/* ---- fork (proc.c) : les verrous de newlib ---- */
static void lock_fork (struct __lock *l, int phase)
{
	if (phase == POSIX_FORK_PREPARE)
		__retarget_lock_acquire_recursive (l);
	else if (phase == POSIX_FORK_PARENT)
		__retarget_lock_release_recursive (l);
	else
	{
		l->owner = 0;
		l->count = 0;
		l->lk = 0;
	}
}

/* la liste des FILE et l'environnement ; dans le fils, les verrous de stdin, stdout, stderr
 * aussi (un autre thread du pere pouvait ecrire : pas pris avant, il pourrait attendre
 * longtemps, sur un tube plein) */
void __posix_stdio_fork (int phase)
{
	if (phase == POSIX_FORK_PARENT)
	{
		lock_fork (&__lock___env_recursive_mutex, phase);
		lock_fork (&__lock___sfp_recursive_mutex, phase);
		return;
	}
	lock_fork (&__lock___sfp_recursive_mutex, phase);
	lock_fork (&__lock___env_recursive_mutex, phase);
	if (phase == POSIX_FORK_CHILD)
	{
		FILE *f[3] = { stdin, stdout, stderr };
		for (int i = 0; i < 3; i++)
			if (f[i] && f[i]->_lock)
				lock_fork ((struct __lock *) f[i]->_lock, phase);
	}
}

void __posix_malloc_fork (int phase) { lock_fork (&__lock___malloc_recursive_mutex, phase); }
