/*
 * syscall.c -- liblinux : les appels système de Linux x86-64 traduits en appels de libposix
 * (linux.h, docs/etude/WIDEVINE.md § 4.3).
 *
 * linux_syscall rend ce que rendrait le noyau Linux : la valeur, ou -errno de Linux. Les numéros,
 * les constantes (horloges, drapeaux d'open, de mmap) et les numéros d'errno sont ceux de Linux ;
 * libposix (newlib) a les siens, d'où les tables plus bas. Un appel non servi rend -ENOSYS et le dit
 * une fois sur la sortie d'erreur (c'est la liste de ce qui reste à faire).
 *
 * Servis : read, write, close, lseek, mmap, mprotect, munmap, sched_yield, nanosleep, getpid,
 * gettid, getuid, geteuid, getgid, getegid, clock_gettime, clock_getres, gettimeofday, time,
 * getrandom, exit, exit_group. Futex, clone, et le reste : étapes suivantes.
 */
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "linux.h"

/* numéros d'appels de Linux x86-64 (arch/x86/entry/syscalls/syscall_64.tbl) */
#define LNR_read		0
#define LNR_write		1
#define LNR_close		3
#define LNR_lseek		8
#define LNR_mmap		9
#define LNR_mprotect		10
#define LNR_munmap		11
#define LNR_sched_yield		24
#define LNR_nanosleep		35
#define LNR_getpid		39
#define LNR_exit		60
#define LNR_gettimeofday	96
#define LNR_getuid		102
#define LNR_getgid		104
#define LNR_geteuid		107
#define LNR_getegid		108
#define LNR_gettid		186
#define LNR_time		201
#define LNR_clock_gettime	228
#define LNR_clock_getres	229
#define LNR_exit_group		231
#define LNR_getrandom		318

/* errno de Linux (include/uapi/asm-generic/errno-base.h et errno.h) ; 1 à 34 sont les mêmes que
   ceux de newlib, la suite diffère */
static const struct { int korin, lnx; } s_errno[] =
{
	{ EDEADLK, 35 }, { ENAMETOOLONG, 36 }, { ENOLCK, 37 }, { ENOSYS, 38 }, { ENOTEMPTY, 39 },
	{ ELOOP, 40 }, { ENOMSG, 42 }, { EIDRM, 43 }, { ENOSTR, 60 }, { ENODATA, 61 }, { ETIME, 62 },
	{ ENOSR, 63 }, { ENOLINK, 67 }, { EPROTO, 71 }, { EMULTIHOP, 72 }, { EBADMSG, 74 },
	{ EOVERFLOW, 75 }, { EILSEQ, 84 }, { ENOTSOCK, 88 }, { EDESTADDRREQ, 89 }, { EMSGSIZE, 90 },
	{ EPROTOTYPE, 91 }, { ENOPROTOOPT, 92 }, { EPROTONOSUPPORT, 93 }, { EOPNOTSUPP, 95 },
	{ EAFNOSUPPORT, 97 }, { EADDRINUSE, 98 }, { EADDRNOTAVAIL, 99 }, { ENETDOWN, 100 },
	{ ENETUNREACH, 101 }, { ENETRESET, 102 }, { ECONNABORTED, 103 }, { ECONNRESET, 104 },
	{ ENOBUFS, 105 }, { EISCONN, 106 }, { ENOTCONN, 107 }, { ETIMEDOUT, 110 },
	{ ECONNREFUSED, 111 }, { EHOSTUNREACH, 113 }, { EALREADY, 114 }, { EINPROGRESS, 115 },
	{ ESTALE, 116 }, { EDQUOT, 122 }, { ECANCELED, 125 }, { EOWNERDEAD, 130 },
	{ ENOTRECOVERABLE, 131 },
};

int linux_errno (int e)
{
	if (e > 0 && e <= 34)
		return e;
	for (size_t i = 0; i < sizeof s_errno / sizeof s_errno[0]; i++)
		if (s_errno[i].korin == e)
			return s_errno[i].lnx;
	return e == ENOTSUP ? 95 : 5;	/* (EIO pour le reste) */
}

unsigned long linux_syscall_count[512];

/* horloges de Linux -> celles de libposix */
static int clock_of (long id, clockid_t *out)
{
	switch (id)
	{
	case 0: case 5: *out = CLOCK_REALTIME; return 0;		/* REALTIME, REALTIME_COARSE */
	case 1: case 4: case 6: case 7: *out = CLOCK_MONOTONIC; return 0;	/* MONOTONIC, _RAW, _COARSE, BOOTTIME */
#ifdef CLOCK_PROCESS_CPUTIME_ID
	case 2: *out = CLOCK_PROCESS_CPUTIME_ID; return 0;
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
	case 3: *out = CLOCK_THREAD_CPUTIME_ID; return 0;
#endif
	}
	return -1;
}

/* drapeaux de mmap de Linux -> libposix (les mêmes valeurs aujourd'hui : sys/mman.h de libposix les
   a pris de Linux ; la table garde le code juste si l'un des deux change) */
static int mmap_flags (long f, int *out)
{
	int r = 0;
	if (f & 0x01) r |= MAP_SHARED;
	if (f & 0x02) r |= MAP_PRIVATE;
	if (f & 0x10) r |= MAP_FIXED;
	if (f & 0x20) r |= MAP_ANONYMOUS;
	/* (0x4000 MAP_NORESERVE, 0x8000 MAP_POPULATE, 0x20000 MAP_STACK : sans effet ici) */
	if (f & ~(0x01 | 0x02 | 0x10 | 0x20 | 0x4000 | 0x8000 | 0x20000 | 0x100000))
		return -1;
	if (f & 0x100000)
#ifdef MAP_FIXED_NOREPLACE
		r |= MAP_FIXED_NOREPLACE;
#else
		return -1;
#endif
	*out = r;
	return 0;
}

static long fail (void)
{
	return -linux_errno (errno);
}

static void not_served (long nr)
{
	static unsigned char said[512];
	if (nr >= 0 && nr < 512)
	{
		if (said[nr])
			return;
		said[nr] = 1;
	}
	fprintf (stderr, "liblinux : appel système de Linux %ld non servi (ENOSYS)\n", nr);
}

long linux_syscall (long nr, long a, long b, long c, long d, long e, long f)
{
	(void) d;
	(void) e;
	if (nr >= 0 && nr < 512)
		linux_syscall_count[nr]++;
	switch (nr)
	{
	case LNR_read:
	{
		ssize_t r = read ((int) a, (void *) b, (size_t) c);
		return r < 0 ? fail () : r;
	}
	case LNR_write:
	{
		ssize_t r = write ((int) a, (const void *) b, (size_t) c);
		return r < 0 ? fail () : r;
	}
	case LNR_close:
		return close ((int) a) < 0 ? fail () : 0;
	case LNR_lseek:
	{
		/* SEEK_SET, SEEK_CUR, SEEK_END : 0, 1, 2 des deux côtés */
		off_t r = lseek ((int) a, (off_t) b, (int) c);
		return r < 0 ? fail () : (long) r;
	}
	case LNR_mmap:
	{
		int fl;
		if (mmap_flags (d, &fl))
			return -22;	/* EINVAL */
		void *p = mmap ((void *) a, (size_t) b, (int) c, fl, (int) e, (off_t) f);
		return p == MAP_FAILED ? fail () : (long) p;
	}
	case LNR_mprotect:
		return mprotect ((void *) a, (size_t) b, (int) c) < 0 ? fail () : 0;
	case LNR_munmap:
		return munmap ((void *) a, (size_t) b) < 0 ? fail () : 0;
	case LNR_sched_yield:
		sched_yield ();
		return 0;
	case LNR_nanosleep:
		/* struct timespec : deux longs des deux côtés */
		return nanosleep ((const struct timespec *) a, (struct timespec *) b) < 0 ? fail () : 0;
	case LNR_getpid:
		return getpid ();
	case LNR_gettid:
		return syscall (SYS_gettid);
	case LNR_getuid:
		return getuid ();
	case LNR_geteuid:
		return geteuid ();
	case LNR_getgid:
		return getgid ();
	case LNR_getegid:
		return getegid ();
	case LNR_clock_gettime:
	case LNR_clock_getres:
	{
		clockid_t id;
		if (clock_of (a, &id))
			return -22;
		int r = nr == LNR_clock_gettime ? clock_gettime (id, (struct timespec *) b)
						 : clock_getres (id, (struct timespec *) b);
		return r < 0 ? fail () : 0;
	}
	case LNR_gettimeofday:
	{
		/* struct timeval : deux longs des deux côtés ; le fuseau (b) : UTC */
		if (a && gettimeofday ((struct timeval *) a, 0) < 0)
			return fail ();
		if (b)
			memset ((void *) b, 0, 8);
		return 0;
	}
	case LNR_time:
	{
		time_t t = time (0);
		if (a)
			*(long *) a = (long) t;
		return (long) t;
	}
	case LNR_getrandom:
	{
		/* GRND_NONBLOCK (1), GRND_RANDOM (2) : toujours prêt */
		ssize_t r = getrandom ((void *) a, (size_t) b, 0);
		return r < 0 ? fail () : (long) r;
	}
	case LNR_exit:
	case LNR_exit_group:
		_exit ((int) a);
	}
	not_served (nr);
	return -38;	/* ENOSYS de Linux */
}
