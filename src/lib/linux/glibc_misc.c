/*
 * glibc_misc.c -- liblinux : le reste de la glibc de compatibilité (glibc_internal.h,
 * docs/etude/WIDEVINE.md § 2.5 et § 3.1) : ce que le module Widevine importe en plus des bases de
 * glibc.c et des threads de glibc_thread.c.
 *
 *   fichiers      open64, creat64, pread64, __xstat64 (struct stat de Linux), fopen64, fclose, fgets,
 *                 getc, ungetc, perror (les FILE * de newlib, passés tels quels)
 *   temps         localtime_r, gmtime_r, mktime, timegm (struct tm de glibc : 56 octets, avec
 *                 tm_gmtoff et tm_zone ; fuseau UTC)
 *   locales       newlocale, uselocale, freelocale, *_l, catopen… : la seule locale « C »
 *   caractères    mbrtowc, wcrtomb, mbsrtowcs… de newlib (wchar_t et mbstate_t de mêmes tailles)
 *   maths         libm de newlib
 *   système       getauxval, prctl, uname, getrlimit64, madvise, geteuid, __libc_stack_end
 *   divers        dlsym, dlclose, _setjmp, longjmp, strerror_r (forme GNU), __isoc99_*scanf,
 *                 vasprintf, bcmp, __strncat_chk
 *   C++           _Unwind_* : le dérouleur de libgcc (elf.c lui déclare le .eh_frame du module)
 */
#define _GNU_SOURCE	/* (vasprintf, timegm) */
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <unwind.h>
#include <wchar.h>

#include "linux.h"
#include "glibc_internal.h"

static long sc (long nr, long a, long b, long c, long d) { return lx_ret (linux_syscall (nr, a, b, c, d, 0, 0)); }

/* ---- fichiers ---- */
static int lx_open64 (const char *p, int fl, ...)
{
	va_list ap;
	va_start (ap, fl);
	int mode = (fl & 0x40) ? va_arg (ap, int) : 0;	/* (O_CREAT de Linux) */
	va_end (ap);
	return (int) sc (2, (long) p, fl, mode, 0);
}

static int lx_creat64 (const char *p, int mode) { return (int) sc (2, (long) p, 0x241, mode, 0); }	/* WRONLY|CREAT|TRUNC */
static long lx_pread64 (int fd, void *b, size_t n, long off) { return sc (17, fd, (long) b, (long) n, off); }
static int lx_xstat64 (int ver, const char *p, void *st) { (void) ver; return (int) sc (4, (long) p, (long) st, 0, 0); }
static FILE *lx_fopen64 (const char *p, const char *m) { FILE *f = fopen (p, m); if (!f) lx_set_errno_from_korin (); return f; }

/* ---- temps : struct tm de glibc ---- */
struct gtm
{
	int sec, min, hour, mday, mon, year, wday, yday, isdst;
	long gmtoff;
	const char *zone;
};

static void to_glibc (struct gtm *g, const struct tm *t)
{
	g->sec = t->tm_sec, g->min = t->tm_min, g->hour = t->tm_hour, g->mday = t->tm_mday, g->mon = t->tm_mon;
	g->year = t->tm_year, g->wday = t->tm_wday, g->yday = t->tm_yday, g->isdst = t->tm_isdst;
	g->gmtoff = 0;
	g->zone = "UTC";
}

static void from_glibc (struct tm *t, const struct gtm *g)
{
	memset (t, 0, sizeof *t);
	t->tm_sec = g->sec, t->tm_min = g->min, t->tm_hour = g->hour, t->tm_mday = g->mday, t->tm_mon = g->mon;
	t->tm_year = g->year, t->tm_wday = g->wday, t->tm_yday = g->yday, t->tm_isdst = g->isdst;
}

static struct gtm *lx_gmtime_r (const time_t *tt, struct gtm *g)
{
	struct tm t;
	if (!gmtime_r (tt, &t))
		return 0;
	to_glibc (g, &t);
	return g;
}

static struct gtm *lx_localtime_r (const time_t *tt, struct gtm *g) { return lx_gmtime_r (tt, g); }	/* (fuseau UTC) */

static time_t lx_timegm (struct gtm *g)
{
	struct tm t;
	from_glibc (&t, g);
	time_t r = timegm (&t);
	to_glibc (g, &t);
	return r;
}

static time_t lx_mktime (struct gtm *g) { return lx_timegm (g); }

/* ---- locales : la seule « C » ---- */
static int s_c_locale;
#define LX_LOCALE ((void *) &s_c_locale)
#define LX_GLOBAL ((void *) -1L)	/* LC_GLOBAL_LOCALE */
static __thread void *s_uselocale;

static void *lx_newlocale (int mask, const char *name, void *base) { (void) mask; (void) name; (void) base; return LX_LOCALE; }
static void lx_freelocale (void *l) { (void) l; }
static void *lx_uselocale (void *l)
{
	void *old = s_uselocale ? s_uselocale : LX_GLOBAL;
	if (l)
		s_uselocale = l;
	return old;
}

static double lx_strtod_l (const char *s, char **e, void *l) { (void) l; return strtod (s, e); }
static float lx_strtof_l (const char *s, char **e, void *l) { (void) l; return strtof (s, e); }
static long double lx_strtold_l (const char *s, char **e, void *l) { (void) l; return strtold (s, e); }
static long long lx_strtoll_l (const char *s, char **e, int b, void *l) { (void) l; return strtoll (s, e, b); }
static unsigned long long lx_strtoull_l (const char *s, char **e, int b, void *l) { (void) l; return strtoull (s, e, b); }

static size_t lx_strftime_l (char *s, size_t n, const char *f, const struct gtm *g, void *l)
{
	(void) l;
	struct tm t;
	from_glibc (&t, g);
	return strftime (s, n, f, &t);
}

static size_t lx_ctype_get_mb_cur_max (void) { return 1; }
static size_t lx_mbrlen (const char *s, size_t n, mbstate_t *ps) { return mbrlen (s, n, ps); }

/* catalogues de messages : aucun (les messages par défaut de libstdc++) */
static void *lx_catopen (const char *n, int f) { (void) n; (void) f; *lx_errno_ptr () = 2; return (void *) -1L; }
static char *lx_catgets (void *c, int set, int num, const char *def) { (void) c; (void) set; (void) num; return (char *) def; }
static int lx_catclose (void *c) { (void) c; return 0; }

/* ---- divers ---- */
static char *lx_strerror_r (int e, char *buf, size_t n)
{
	(void) buf;
	(void) n;
	return strerror (linux_errno_korin (e));	/* (forme GNU : rend la chaîne) */
}

static void lx_perror (const char *s)
{
	const char *m = strerror (linux_errno_korin (*lx_errno_ptr ()));
	if (s && *s)
		fprintf (stderr, "%s: %s\n", s, m);
	else
		fprintf (stderr, "%s\n", m);
}

static int lx_isoc99_sscanf (const char *s, const char *f, ...)
{
	va_list ap;
	va_start (ap, f);
	int r = vsscanf (s, f, ap);
	va_end (ap);
	return r;
}

static int lx_isoc99_vsscanf (const char *s, const char *f, va_list ap) { return vsscanf (s, f, ap); }
static int lx_bcmp (const void *a, const void *b, size_t n) { return memcmp (a, b, n); }

static char *lx_strncat_chk (char *d, const char *s, size_t n, size_t dn)
{
	size_t l = strlen (d), k = strnlen (s, n);
	if (l + k >= dn)
	{
		fprintf (stderr, "liblinux : *** débordement de tampon détecté (__strncat_chk) ***\n");
		abort ();
	}
	return strncat (d, s, n);
}

/* ---- système ---- */
static unsigned char s_at_random[16];

static unsigned long lx_getauxval (unsigned long type)
{
	switch (type)
	{
	case 6: return 4096;					/* AT_PAGESZ */
	case 11: return (unsigned long) getuid ();		/* AT_UID */
	case 12: return (unsigned long) geteuid ();		/* AT_EUID */
	case 13: return (unsigned long) getgid ();		/* AT_GID */
	case 14: return (unsigned long) getegid ();		/* AT_EGID */
	case 15: return (unsigned long) "x86_64";		/* AT_PLATFORM */
	case 16: return 0;					/* AT_HWCAP */
	case 17: return 100;					/* AT_CLKTCK */
	case 23: return 0;					/* AT_SECURE */
	case 25:						/* AT_RANDOM */
		if (!s_at_random[0] && !s_at_random[1])
			linux_syscall (318, (long) s_at_random, 16, 0, 0, 0, 0);
		return (unsigned long) s_at_random;
	case 26: return 0;					/* AT_HWCAP2 */
	}
	*lx_errno_ptr () = 2;	/* ENOENT */
	return 0;
}

static int lx_prctl (int op, unsigned long a, unsigned long b, unsigned long c, unsigned long d)
{
	return (int) lx_ret (linux_syscall (157, op, (long) a, (long) b, (long) c, (long) d, 0));
}

static int lx_uname (void *u) { return (int) sc (63, (long) u, 0, 0, 0); }
static int lx_getrlimit64 (int r, void *out) { return (int) sc (97, r, (long) out, 0, 0); }
static int lx_madvise (void *a, size_t n, int adv) { return (int) sc (28, (long) a, (long) n, adv, 0); }
static unsigned lx_geteuid (void) { return (unsigned) linux_syscall (107, 0, 0, 0, 0, 0, 0); }

/* __libc_stack_end : une donnée, la fin de la pile du programme (environ : une adresse de la pile du
   premier appel de la table) */
static void *lx_libc_stack_end;

/* ---- dl ---- */
static void *lx_dlsym (void *h, const char *name)
{
	(void) h;
	void *p = linux_glibc_lookup (name);
	if (!p)
		fprintf (stderr, "liblinux : dlsym (%s) : absent\n", name);
	return p;
}

static int lx_dlclose (void *h) { (void) h; return 0; }

const struct lx_entry lx_table_misc[] =
{
	/* fichiers */
	LX_W ("open64", lx_open64), LX_W ("open", lx_open64), LX_W ("creat64", lx_creat64),
	LX_W ("pread64", lx_pread64), LX_W ("__xstat64", lx_xstat64), LX_W ("fopen64", lx_fopen64),
	LX_W ("fopen", lx_fopen64), LX_F (fclose), LX_F (fgets), LX_F (getc), LX_F (ungetc),
	LX_W ("perror", lx_perror),
	/* temps */
	LX_W ("localtime_r", lx_localtime_r), LX_W ("gmtime_r", lx_gmtime_r), LX_W ("mktime", lx_mktime),
	LX_W ("timegm", lx_timegm),
	/* locales */
	LX_W ("newlocale", lx_newlocale), LX_W ("freelocale", lx_freelocale), LX_W ("uselocale", lx_uselocale),
	LX_W ("strtod_l", lx_strtod_l), LX_W ("strtof_l", lx_strtof_l), LX_W ("strtold_l", lx_strtold_l),
	LX_W ("strtoll_l", lx_strtoll_l), LX_W ("strtoull_l", lx_strtoull_l), LX_W ("strftime_l", lx_strftime_l),
	LX_W ("__ctype_get_mb_cur_max", lx_ctype_get_mb_cur_max), LX_W ("catopen", lx_catopen),
	LX_W ("catgets", lx_catgets), LX_W ("catclose", lx_catclose),
	/* caractères larges */
	LX_F (mbrtowc), LX_F (wcrtomb), LX_F (mbsrtowcs), LX_F (wcsnrtombs), LX_F (mbsnrtowcs), LX_F (mbtowc),
	LX_W ("__mbrlen", lx_mbrlen), LX_F (getwc), LX_F (ungetwc), LX_F (fputwc), LX_F (wcslen), LX_F (wmemchr),
	/* maths */
	LX_F (frexp), LX_F (ldexp), LX_F (exp), LX_F (round), LX_F (ceilf), LX_F (floor), LX_F (log), LX_F (ceil),
	LX_F (log2),
	/* divers */
	LX_W ("strerror_r", lx_strerror_r), LX_W ("__isoc99_sscanf", lx_isoc99_sscanf),
	LX_W ("__isoc99_vsscanf", lx_isoc99_vsscanf), LX_F (vasprintf), LX_W ("bcmp", lx_bcmp),
	LX_W ("__strncat_chk", lx_strncat_chk),
	/* système */
	LX_W ("getauxval", lx_getauxval), LX_W ("prctl", lx_prctl), LX_W ("uname", lx_uname),
	LX_W ("getrlimit64", lx_getrlimit64), LX_W ("getrlimit", lx_getrlimit64), LX_W ("madvise", lx_madvise),
	LX_W ("geteuid", lx_geteuid), LX_W ("__libc_stack_end", &lx_libc_stack_end),
	/* dl, sauts, TLS */
	LX_W ("dlsym", lx_dlsym), LX_W ("dlclose", lx_dlclose), LX_W ("_setjmp", setjmp), LX_F (longjmp),
	LX_W ("__tls_get_addr", linux_tls_get_addr),
	/* C++ : le dérouleur de libgcc */
	LX_F (_Unwind_RaiseException), LX_F (_Unwind_DeleteException), LX_F (_Unwind_SetGR), LX_F (_Unwind_SetIP),
	LX_F (_Unwind_GetLanguageSpecificData), LX_F (_Unwind_GetIP), LX_F (_Unwind_GetRegionStart),
	LX_F (_Unwind_Resume),
};
const size_t lx_table_misc_n = sizeof lx_table_misc / sizeof lx_table_misc[0];

/* (appelé une fois par glibc.c, avant la première recherche) */
void lx_misc_init (void)
{
	int here;
	lx_libc_stack_end = (void *) (((uintptr_t) &here + 0xFFFF) & ~(uintptr_t) 0xFFFF);
}
