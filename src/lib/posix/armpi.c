/*
 * armpi.c -- le cote ARMPI de libposix : erreurs, kits (AppKit, NetKit), client de fichiers
 * d'AppKit (fichier_sys), service vfs (armpi.h, docs/POSIX.md).
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
#include <string.h>
#include "posix_internal.h"

/* ---- erreurs : ARMPI rend les numeros de Linux, newlib en a d'autres au-dela de 34 ---- */
int __armpi_errno (long r)
{
	long e = -r;
	switch (e)
	{
	case 35: return EDEADLK;
	case 36: return ENAMETOOLONG;
	case 38: return ENOSYS;
	case 39: return ENOTEMPTY;
	case 88: return ENOTSOCK;
	case 90: return EMSGSIZE;
	case 93: return EPROTONOSUPPORT;
	case 95: return EOPNOTSUPP;
	case 105: return ENOBUFS;
	case 97: return EAFNOSUPPORT;
	case 98: return EADDRINUSE;
	case 99: return EADDRNOTAVAIL;
	case 100: return ENETDOWN;
	case 101: return ENETUNREACH;
	case 103: return ECONNABORTED;
	case 104: return ECONNRESET;
	case 106: return EISCONN;
	case 107: return ENOTCONN;
	case 110: return ETIMEDOUT;
	case 111: return ECONNREFUSED;
	case 113: return EHOSTUNREACH;
	case 115: return EINPROGRESS;
	}
	if (e > 0 && e <= 34)
		return (int) e;			/* EPERM .. ERANGE : les memes */
	return EIO;
}

long __armpi_ret (long r)
{
	if (r < 0)
	{
		errno = __armpi_errno (r);
		return -1;
	}
	return r;
}

/* ---- kits ---- */
static const struct __armpi_kit *s_appkit, *s_netkit;
static int s_netkitTried;
static void *s_fsys;				/* fichier_sys d'AppKit (rang 13) */

const struct __armpi_kit *__armpi_kit_open (const char *name, unsigned min)
{
	long r = __armpi_sys2 (SYS_LIB_OPEN, name, min);
	if (r == -2 && strcmp (name, "appkit") != 0)
	{
		/* pas encore en memoire : AppKit le lit (SD0:/lib/<nom>.so, par init2 sans droits) */
		long (*load) (const char *) = (long (*) (const char *)) __armpi_appkit (AK_F_KIT_LOAD);
		if (load != 0 && load (name) == 0)
			r = __armpi_sys2 (SYS_LIB_OPEN, name, min);
	}
	return r < 0 ? 0 : (const struct __armpi_kit *) r;
}

/* <armpi/kit.h> : les kits depuis le C (les en-tetes des kits, la glue de WebKit) */
void *armpi_kit_open (const char *name, unsigned min_version)
{
	return (void *) __armpi_kit_open (name, min_version);
}

void *armpi_kit_fn (const void *kit, unsigned rank)
{
	const struct __armpi_kit *k = (const struct __armpi_kit *) kit;
	return k != 0 && rank < k->count ? (char *) k + k->off[rank] : 0;
}

unsigned armpi_kit_version (const void *kit)
{
	return kit != 0 ? ((const struct __armpi_kit *) kit)->count : 0;
}

static const struct __armpi_kit *appkit (void)
{
	if (s_appkit == 0)
	{
		s_appkit = __armpi_kit_open ("appkit", AK_MIN_VERSION);
		if (s_appkit == 0)
		{
			static const char m[] = "libposix : AppKit absent (SD0:/lib/appkit.so)\n";
			__armpi_sys2 (SYS_PRINT, m, sizeof m - 1);
			__armpi_sys1 (SYS_EXIT, 127);
		}
		s_fsys = (char *) s_appkit + s_appkit->off[13];
	}
	return s_appkit;
}

void *__armpi_appkit (unsigned rank)
{
	const struct __armpi_kit *k = appkit ();
	return rank < k->count ? (char *) k + k->off[rank] : 0;
}

void *__armpi_netkit (unsigned rank)
{
	if (s_netkit == 0 && !s_netkitTried)
	{
		s_netkit = __armpi_kit_open ("netkit", NK_MIN_VERSION);
		s_netkitTried = 1;
	}
	if (s_netkit == 0 || rank >= s_netkit->count)
		return 0;
	return (char *) s_netkit + s_netkit->off[rank];
}

/* fichier_sys : x8 = numero, x0..x2 ; garde tout sauf x0, x16 (et x30) */
long __armpi_fsys (long n, long a, long b, long c)
{
	if (s_fsys == 0)
		appkit ();
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = n;
	register void *x9 __asm__ ("x9") = s_fsys;
	__asm__ volatile ("blr x9" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8), "r" (x9)
			  : "x16", "x17", "x30", "memory", "cc");
	return x0;
}

/* ---- vfs ---- */
static long s_vfs = -1;

long __armpi_vfs_call (struct __armpi_msg *m)
{
	if (s_vfs < 0)
	{
		long c = __armpi_sys2 (SYS_SVC_OPEN, "vfs", 5000);
		if (c < 0)
			return c;
		s_vfs = c;
	}
	return __armpi_sys2 (SYS_SVC_CALL, s_vfs, m);
}
