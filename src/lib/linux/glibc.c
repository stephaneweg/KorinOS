/*
 * glibc.c -- liblinux : la glibc de compatibilité (linux.h, docs/etude/WIDEVINE.md § 4.2).
 *
 * Un module de Linux importe des fonctions et des données de glibc par leur nom ; le chargeur
 * (elf.c) les cherche ici. Deux sortes :
 *   - les mêmes que newlib à l'ABI près (memcpy, malloc, snprintf…) : l'adresse de celle de libposix ;
 *   - celles dont l'ABI de Linux diffère : une enveloppe « lx_ » (errno de Linux par
 *     __errno_location, horloges et constantes de Linux, tables de ctype de glibc, mutex de glibc de
 *     40 octets, variantes _chk de _FORTIFY_SOURCE, stdout et stderr comme données).
 * Ce qui n'est pas dans la table est « absent » : linuxhost --check en donne la liste pour un module.
 *
 * Limites (étapes suivantes, § 4.2 de l'étude) : FILE * de newlib passés tels quels (le module ne
 * doit pas lire leurs champs, ce que font les macros getc_unlocked de glibc) ; pthread_mutex en
 * attente active (sched_yield), sans les mutex récursifs ; pas de pthread_cond, de pthread_create,
 * de dlopen, de TLS du module.
 */
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "linux.h"

/* ---- errno de Linux ---- */
static __thread int lx_errno_value;

static int *lx_errno_location (void)
{
	return &lx_errno_value;
}

/* le résultat brut d'un appel (-errno de Linux) vers la convention de la libc */
static long lx_ret (long r)
{
	if (r < 0 && r > -4096)
	{
		lx_errno_value = (int) -r;
		return -1;
	}
	return r;
}

/* ---- appels système ---- */
static long lx_syscall (long nr, ...)
{
	va_list ap;
	va_start (ap, nr);
	long a = va_arg (ap, long), b = va_arg (ap, long), c = va_arg (ap, long);
	long d = va_arg (ap, long), e = va_arg (ap, long), f = va_arg (ap, long);
	va_end (ap);
	return lx_ret (linux_syscall (nr, a, b, c, d, e, f));
}

static long lx_read (int fd, void *p, size_t n) { return lx_ret (linux_syscall (0, fd, (long) p, (long) n, 0, 0, 0)); }
static long lx_write (int fd, const void *p, size_t n) { return lx_ret (linux_syscall (1, fd, (long) p, (long) n, 0, 0, 0)); }
static int lx_close (int fd) { return (int) lx_ret (linux_syscall (3, fd, 0, 0, 0, 0, 0)); }
static int lx_getpid (void) { return (int) linux_syscall (39, 0, 0, 0, 0, 0, 0); }
static int lx_gettid (void) { return (int) linux_syscall (186, 0, 0, 0, 0, 0, 0); }
static int lx_clock_gettime (int id, struct timespec *ts) { return (int) lx_ret (linux_syscall (228, id, (long) ts, 0, 0, 0, 0)); }
static int lx_clock_getres (int id, struct timespec *ts) { return (int) lx_ret (linux_syscall (229, id, (long) ts, 0, 0, 0, 0)); }
static int lx_gettimeofday (void *tv, void *tz) { return (int) lx_ret (linux_syscall (96, (long) tv, (long) tz, 0, 0, 0, 0)); }
static long lx_time (long *t) { return linux_syscall (201, (long) t, 0, 0, 0, 0, 0); }
static int lx_nanosleep (const struct timespec *a, struct timespec *b) { return (int) lx_ret (linux_syscall (35, (long) a, (long) b, 0, 0, 0, 0)); }
static int lx_sched_yield (void) { return (int) linux_syscall (24, 0, 0, 0, 0, 0, 0); }
static long lx_getrandom (void *p, size_t n, unsigned fl) { return lx_ret (linux_syscall (318, (long) p, (long) n, fl, 0, 0, 0)); }
static int lx_getentropy (void *p, size_t n)
{
	if (n > 256)
	{
		lx_errno_value = 5;	/* EIO, comme glibc */
		return -1;
	}
	return lx_getrandom (p, n, 0) < 0 ? -1 : 0;
}

static int lx_usleep (unsigned us)
{
	struct timespec t = { us / 1000000, (long) (us % 1000000) * 1000 };
	return lx_nanosleep (&t, 0);
}

static void *lx_mmap (void *a, size_t n, int prot, int fl, int fd, long off)
{
	long r = linux_syscall (9, (long) a, (long) n, prot, fl, fd, off);
	if (r < 0 && r > -4096)
	{
		lx_errno_value = (int) -r;
		return (void *) -1;
	}
	return (void *) r;
}
static int lx_munmap (void *a, size_t n) { return (int) lx_ret (linux_syscall (11, (long) a, (long) n, 0, 0, 0, 0)); }
static int lx_mprotect (void *a, size_t n, int p) { return (int) lx_ret (linux_syscall (10, (long) a, (long) n, p, 0, 0, 0)); }

/* sysconf : les numéros de Linux (bits/confname.h) */
static long lx_sysconf (int name)
{
	switch (name)
	{
	case 2: return 100;					/* _SC_CLK_TCK */
	case 30: return 4096;					/* _SC_PAGESIZE */
	case 83:						/* _SC_NPROCESSORS_CONF */
	case 84:						/* _SC_NPROCESSORS_ONLN */
	{
		long n = sysconf (_SC_NPROCESSORS_ONLN);
		return n > 0 ? n : 1;
	}
	}
	lx_errno_value = 22;	/* EINVAL */
	return -1;
}

/* ---- stdio : les FILE * de newlib, passés tels quels ---- */
static FILE *lx_stdin, *lx_stdout, *lx_stderr;

static void lx_stdio_init (void)
{
	lx_stdin = stdin;
	lx_stdout = stdout;
	lx_stderr = stderr;
}

/* _FORTIFY_SOURCE : les variantes _chk (le drapeau et les tailles ne changent rien ici, sauf les
   débordements que glibc arrêterait) */
static void lx_chk_fail (void) __attribute__ ((noreturn));
static void lx_chk_fail (void)
{
	fprintf (stderr, "liblinux : *** débordement de tampon détecté (_chk) ***\n");
	abort ();
}

static int lx_printf_chk (int flag, const char *fmt, ...)
{
	(void) flag;
	va_list ap;
	va_start (ap, fmt);
	int r = vprintf (fmt, ap);
	va_end (ap);
	return r;
}

static int lx_fprintf_chk (FILE *f, int flag, const char *fmt, ...)
{
	(void) flag;
	va_list ap;
	va_start (ap, fmt);
	int r = vfprintf (f, fmt, ap);
	va_end (ap);
	return r;
}

static int lx_vprintf_chk (int flag, const char *fmt, va_list ap)
{
	(void) flag;
	return vprintf (fmt, ap);
}

static int lx_vfprintf_chk (FILE *f, int flag, const char *fmt, va_list ap)
{
	(void) flag;
	return vfprintf (f, fmt, ap);
}

static int lx_vsnprintf_chk (char *s, size_t n, int flag, size_t slen, const char *fmt, va_list ap)
{
	(void) flag;
	if (n > slen)
		lx_chk_fail ();
	return vsnprintf (s, n, fmt, ap);
}

static int lx_snprintf_chk (char *s, size_t n, int flag, size_t slen, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	int r = lx_vsnprintf_chk (s, n, flag, slen, fmt, ap);
	va_end (ap);
	return r;
}

static int lx_sprintf_chk (char *s, int flag, size_t slen, const char *fmt, ...)
{
	(void) flag;
	va_list ap;
	va_start (ap, fmt);
	int r = vsnprintf (s, slen, fmt, ap);
	va_end (ap);
	if (r >= 0 && (size_t) r >= slen)
		lx_chk_fail ();
	return r;
}

static void *lx_memcpy_chk (void *d, const void *s, size_t n, size_t dn)
{
	if (n > dn)
		lx_chk_fail ();
	return memcpy (d, s, n);
}

static void *lx_memmove_chk (void *d, const void *s, size_t n, size_t dn)
{
	if (n > dn)
		lx_chk_fail ();
	return memmove (d, s, n);
}

static void *lx_memset_chk (void *d, int c, size_t n, size_t dn)
{
	if (n > dn)
		lx_chk_fail ();
	return memset (d, c, n);
}

static char *lx_strcpy_chk (char *d, const char *s, size_t dn)
{
	if (strlen (s) >= dn)
		lx_chk_fail ();
	return strcpy (d, s);
}

static char *lx_strncpy_chk (char *d, const char *s, size_t n, size_t dn)
{
	if (n > dn)
		lx_chk_fail ();
	return strncpy (d, s, n);
}

static char *lx_strcat_chk (char *d, const char *s, size_t dn)
{
	if (strlen (d) + strlen (s) >= dn)
		lx_chk_fail ();
	return strcat (d, s);
}

/* ---- arrêts ---- */
static void lx_stack_chk_fail (void) __attribute__ ((noreturn));
static void lx_stack_chk_fail (void)
{
	fprintf (stderr, "liblinux : *** pile écrasée (canari de %%fs:0x28) ***\n");
	abort ();
}

static void lx_assert_fail (const char *expr, const char *file, unsigned line, const char *fn)
{
	fprintf (stderr, "liblinux : %s:%u: %s: l'assertion « %s » a échoué\n", file, line, fn ? fn : "?", expr);
	abort ();
}

void linux_missing (const char *name)
{
	fprintf (stderr, "liblinux : fonction de glibc absente appelée : %s\n", name);
	fflush (stderr);
	abort ();
}

/* ---- __cxa_atexit, __cxa_finalize ---- */
int linux_module_atexit (void (*fn) (void *), void *arg, void *dso);
void linux_module_finalize (void *dso);
extern int __cxa_atexit (void (*) (void *), void *, void *);

static int lx_cxa_atexit (void (*fn) (void *), void *arg, void *dso)
{
	if (!linux_module_atexit (fn, arg, dso))
		return 0;
	return __cxa_atexit (fn, arg, dso);
}

static void lx_cxa_finalize (void *dso)
{
	if (dso)
		linux_module_finalize (dso);
}

/* ---- ctype de glibc : tables de 384 entrées (-128..255), le pointeur rendu montre l'entrée 0 ---- */
/* (bits de ctype.h de glibc, petit-boutiste : _ISbit (b) = b < 8 ? (1 << b) << 8 : (1 << b) >> 8) */
#define GB(b) ((unsigned short) ((b) < 8 ? ((1 << (b)) << 8) : ((1 << (b)) >> 8)))
static unsigned short s_ctype_b[384];
static int32_t s_ctype_lower[384], s_ctype_upper[384];
static const unsigned short *s_ctype_b_ptr = s_ctype_b + 128;
static const int32_t *s_ctype_lower_ptr = s_ctype_lower + 128, *s_ctype_upper_ptr = s_ctype_upper + 128;

static void lx_ctype_init (void)
{
	for (int i = -128; i < 256; i++)
	{
		int c = i < 0 ? i + 256 : i, v = 0;
		if (c < 128)		/* (la locale « C » : rien au-dessus de 127) */
		{
			if (isupper (c)) v |= GB (0);
			if (islower (c)) v |= GB (1);
			if (isalpha (c)) v |= GB (2);
			if (isdigit (c)) v |= GB (3);
			if (isxdigit (c)) v |= GB (4);
			if (isspace (c)) v |= GB (5);
			if (isprint (c)) v |= GB (6);
			if (isgraph (c)) v |= GB (7);
			if (c == ' ' || c == '\t') v |= GB (8);
			if (iscntrl (c)) v |= GB (9);
			if (ispunct (c)) v |= GB (10);
			if (isalnum (c)) v |= GB (11);
		}
		s_ctype_b[i + 128] = (unsigned short) v;
		s_ctype_lower[i + 128] = c < 128 ? tolower (c) : i;
		s_ctype_upper[i + 128] = c < 128 ? toupper (c) : i;
	}
}

static const unsigned short **lx_ctype_b_loc (void) { return &s_ctype_b_ptr; }
static const int32_t **lx_ctype_tolower_loc (void) { return &s_ctype_lower_ptr; }
static const int32_t **lx_ctype_toupper_loc (void) { return &s_ctype_upper_ptr; }

/* ---- pthread : les types de glibc (x86-64) ---- */
/* pthread_mutex_t de glibc : 40 octets, zéro = PTHREAD_MUTEX_INITIALIZER ; on prend le premier mot
   (__lock) comme verrou. pthread_once_t : un int (0 = pas fait). */
static int lx_mutex_init (int *m, const void *attr)
{
	(void) attr;
	memset (m, 0, 40);
	return 0;
}

static int lx_mutex_lock (int *m)
{
	while (__atomic_exchange_n (m, 1, __ATOMIC_ACQUIRE))
		sched_yield ();
	return 0;
}

static int lx_mutex_trylock (int *m)
{
	return __atomic_exchange_n (m, 1, __ATOMIC_ACQUIRE) ? 16 /* EBUSY */ : 0;
}

static int lx_mutex_unlock (int *m)
{
	__atomic_store_n (m, 0, __ATOMIC_RELEASE);
	return 0;
}

static int lx_mutex_destroy (int *m)
{
	(void) m;
	return 0;
}

static int lx_once (int *once, void (*fn) (void))
{
	int s = __atomic_load_n (once, __ATOMIC_ACQUIRE);
	if (s == 2)
		return 0;
	int zero = 0;
	if (__atomic_compare_exchange_n (once, &zero, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
	{
		fn ();
		__atomic_store_n (once, 2, __ATOMIC_RELEASE);
		return 0;
	}
	while (__atomic_load_n (once, __ATOMIC_ACQUIRE) != 2)
		sched_yield ();
	return 0;
}

static unsigned long lx_pthread_self (void) { return (unsigned long) pthread_self (); }
static int lx_key_create (unsigned *k, void (*d) (void *)) { return pthread_key_create ((pthread_key_t *) k, d); }
static int lx_key_delete (unsigned k) { return pthread_key_delete ((pthread_key_t) k); }
static void *lx_getspecific (unsigned k) { return pthread_getspecific ((pthread_key_t) k); }
static int lx_setspecific (unsigned k, const void *v) { return pthread_setspecific ((pthread_key_t) k, v); }

/* ---- la table ---- */
struct lx_entry
{
	const char *name;
	void *addr;
};

#define F(n)		{ #n, (void *) n }
#define W(n, f)		{ n, (void *) f }

static const struct lx_entry s_table[] =
{
	/* mémoire et chaînes : les mêmes */
	F (memcpy), F (memmove), F (memset), F (memcmp), F (memchr), F (strlen), F (strnlen), F (strcmp),
	F (strncmp), F (strchr), F (strrchr), F (strstr), F (strcpy), F (strncpy), F (strcat), F (strncat),
	F (strdup), F (strndup), F (strspn), F (strcspn), F (strpbrk), F (strcasecmp), F (strncasecmp),
	F (malloc), F (free), F (calloc), F (realloc), F (posix_memalign), F (aligned_alloc),
	F (qsort), F (bsearch), F (strtol), F (strtoul), F (strtoll), F (strtoull), F (strtod), F (atoi),
	F (atol), F (abs), F (labs), F (getenv), F (abort), F (rand), F (srand),
	F (snprintf), F (vsnprintf), F (sprintf), F (vsprintf), F (sscanf),
	F (printf), F (vprintf), F (fprintf), F (vfprintf), F (puts), F (putchar), F (fputs), F (fputc),
	F (fwrite), F (fflush),
	F (tolower), F (toupper),
	/* données de stdio */
	W ("stdin", &lx_stdin), W ("stdout", &lx_stdout), W ("stderr", &lx_stderr),
	/* _FORTIFY_SOURCE */
	W ("__printf_chk", lx_printf_chk), W ("__fprintf_chk", lx_fprintf_chk),
	W ("__vprintf_chk", lx_vprintf_chk), W ("__vfprintf_chk", lx_vfprintf_chk),
	W ("__snprintf_chk", lx_snprintf_chk), W ("__vsnprintf_chk", lx_vsnprintf_chk),
	W ("__sprintf_chk", lx_sprintf_chk), W ("__memcpy_chk", lx_memcpy_chk),
	W ("__memmove_chk", lx_memmove_chk), W ("__memset_chk", lx_memset_chk),
	W ("__strcpy_chk", lx_strcpy_chk), W ("__strncpy_chk", lx_strncpy_chk),
	W ("__strcat_chk", lx_strcat_chk), W ("__chk_fail", lx_chk_fail),
	/* errno, arrêts, C++ */
	W ("__errno_location", lx_errno_location), W ("__stack_chk_fail", lx_stack_chk_fail),
	W ("__assert_fail", lx_assert_fail), W ("__cxa_atexit", lx_cxa_atexit),
	W ("__cxa_finalize", lx_cxa_finalize),
	/* ctype */
	W ("__ctype_b_loc", lx_ctype_b_loc), W ("__ctype_tolower_loc", lx_ctype_tolower_loc),
	W ("__ctype_toupper_loc", lx_ctype_toupper_loc),
	/* appels système (syscall.c) */
	W ("syscall", lx_syscall), W ("read", lx_read), W ("write", lx_write), W ("close", lx_close),
	W ("getpid", lx_getpid), W ("gettid", lx_gettid), W ("clock_gettime", lx_clock_gettime),
	W ("clock_getres", lx_clock_getres), W ("gettimeofday", lx_gettimeofday), W ("time", lx_time),
	W ("nanosleep", lx_nanosleep), W ("usleep", lx_usleep), W ("sched_yield", lx_sched_yield),
	W ("getrandom", lx_getrandom), W ("getentropy", lx_getentropy), W ("mmap", lx_mmap),
	W ("mmap64", lx_mmap), W ("munmap", lx_munmap), W ("mprotect", lx_mprotect),
	W ("sysconf", lx_sysconf),
	/* pthread */
	W ("pthread_mutex_init", lx_mutex_init), W ("pthread_mutex_lock", lx_mutex_lock),
	W ("pthread_mutex_trylock", lx_mutex_trylock), W ("pthread_mutex_unlock", lx_mutex_unlock),
	W ("pthread_mutex_destroy", lx_mutex_destroy), W ("pthread_once", lx_once),
	W ("pthread_self", lx_pthread_self), W ("pthread_key_create", lx_key_create),
	W ("pthread_key_delete", lx_key_delete), W ("pthread_getspecific", lx_getspecific),
	W ("pthread_setspecific", lx_setspecific),
};

void *linux_glibc_lookup (const char *name)
{
	static int ready;
	if (!ready)
	{
		lx_stdio_init ();
		lx_ctype_init ();
		ready = 1;
	}
	for (size_t i = 0; i < sizeof s_table / sizeof s_table[0]; i++)
		if (!strcmp (s_table[i].name, name))
			return s_table[i].addr;
	return 0;
}
