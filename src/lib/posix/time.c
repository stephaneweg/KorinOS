/*
 * time.c -- horloges et sommeils (libposix, docs/POSIX.md ; repris d'Onyx).
 *
 * Pas d'appel systeme par lecture : le compteur generique d'ARM (CNTPCT_EL0 / CNTFRQ_EL0, lisibles
 * en EL0 sur ARMPI) est mis a l'echelle d'apres un echantillon : SYS_TIME_MS (le compteur au
 * demarrage) et l'heure UTC de netd (NetKit net_time, a la seconde ; NTP). CLOCK_MONOTONIC compte
 * depuis le demarrage ; CLOCK_REALTIME est l'UTC de l'echantillon plus le compteur depuis,
 * l'echantillon repris toutes les 60 s, chaque seconde tant que l'heure n'est pas connue (recale
 * seulement si l'ecart depasse 2 s). Sans reseau ni
 * NTP, l'heure part du 1er janvier 1970 au demarrage.
 *
 * Sommeils : SYS_SLEEP (ms), le reste en cedant le processeur. Les horloges de temps processeur
 * sont la monotone. Fuseau : UTC (TZ=UTC0 par defaut). alarm, setitimer : SYS_SETITIMER (SIGALRM
 * envoye par le noyau).
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
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <errno.h>
#include "posix_internal.h"

/* The physical counter (the kernel's clock_info samples it; Onyx lets EL0 read it). The PC test
 * bench (tools/tests/posixsim, under qemu-user: Linux traps CNTPCT at EL0) builds the library with
 * -DONYX_POSIXSIM and reads the virtual one (equal on the Pi: CNTVOFF is 0). */
#ifdef POSIX_POSIXSIM
#define POSIX_COUNTER	"cntvct_el0"
#else
#define POSIX_COUNTER	"cntpct_el0"
#endif

static inline unsigned long long cntpct (void)
{
#if defined(__aarch64__)
	unsigned long long c;
	__asm__ volatile ("isb\n\tmrs %0, " POSIX_COUNTER : "=r" (c));
	return c;
#else
	return 0;
#endif
}

static inline unsigned long long cntfrq (void)
{
#if defined(__aarch64__)
	unsigned long long f;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f;
#else
	return 1;
#endif
}

static struct
{
	volatile int valid;
	unsigned long long cnt, freq, boot;
	long long utc_us;
	int tz;
	int from_kernel;
	unsigned long long sampled;		/* compteur au dernier echantillon de l'heure UTC */
	int real_done;				/* heure UTC deja demandee a netd */
} s_clk;
static volatile unsigned s_clkLock;

/* ns in t counter ticks (no overflow: the quotient and the rest apart) */
static inline unsigned long long ticks_ns (unsigned long long t, unsigned long long f)
{
	return (t / f) * 1000000000ULL + (t % f) * 1000000000ULL / f;
}

static long days_from_civil (int y, int m, int d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void sample (int real)
{
	/* compteur au demarrage : maintenant moins SYS_TIME_MS ; heure UTC : l'horloge du noyau
	   (SYS_CLOCK_GET : NTP de netd, ou SD0:/etc/lastclock), celle des dates de fichiers */
	s_clk.freq = cntfrq ();
	if (s_clk.freq == 0)
		s_clk.freq = 54000000;
	unsigned long long c = cntpct ();
	if (!s_clk.valid)
	{
		unsigned long long ms = (unsigned long long) __armpi_sys0 (SYS_TIME_MS);
		/* (le compteur relu apres ms : au-dessus de c, boot resterait devant lui, et le temps
		   monotone a 0 pour toujours, un sommeil sans fin) */
		c = cntpct ();
		unsigned long long up = ms * (s_clk.freq / 1000);
		s_clk.boot = c > up ? c - up : 0;
		s_clk.cnt = c;
		s_clk.tz = 0;
		s_clk.from_kernel = 0;
		s_clk.utc_us = (long long) (ticks_ns (c - s_clk.boot, s_clk.freq) / 1000);
		s_clk.sampled = c;
	}
	if (real)
	{
		long utc = __armpi_sys1 (SYS_CLOCK_GET, 0);
		c = cntpct ();
		if (utc > 1000000000)
		{
			/* le noyau donne la seconde : on ne recale que si l'ecart depasse 2 s (sinon chaque
			   nouvel echantillon ferait sauter l'heure d'une fraction de seconde) */
			long long now_us = s_clk.utc_us + (long long) (ticks_ns (c - s_clk.cnt, s_clk.freq) / 1000);
			long long d = now_us - utc * 1000000LL;
			if (!s_clk.from_kernel || d > 2000000LL || d < -2000000LL)
			{
				s_clk.cnt = c;
				s_clk.utc_us = utc * 1000000LL;
			}
			s_clk.from_kernel = 1;
		}
	}
	if (real)
	{
		s_clk.sampled = c;
		s_clk.real_done = 1;
	}
	__atomic_store_n (&s_clk.valid, 1, __ATOMIC_RELEASE);
}


static void ensure (int real)
{
	if (__atomic_load_n (&s_clk.valid, __ATOMIC_ACQUIRE) && !real)
		return;
	__posix_lock (&s_clkLock);
	if (!s_clk.valid || real)
		sample (real);
	__posix_unlock (&s_clkLock);
}

unsigned long long __posix_mono_ns (void)
{
	ensure (0);
	unsigned long long c = cntpct ();
	return ticks_ns (c > s_clk.boot ? c - s_clk.boot : 0, s_clk.freq);
}

long long __posix_real_ns (void)
{
	ensure (0);
	unsigned long long c = cntpct ();
	/* (tant que netd n'a pas donne l'heure : chaque seconde. Un process lance avant le NTP ne doit
	   pas rester une minute en 1970 : les certificats https y seraient tous refuses) */
	if (!s_clk.real_done || c - s_clk.sampled > (s_clk.from_kernel ? 60ULL : 1ULL) * s_clk.freq)
	{
		ensure (1);
		c = cntpct ();
	}
	long long base = s_clk.utc_us * 1000LL;
	return c >= s_clk.cnt ? base + (long long) ticks_ns (c - s_clk.cnt, s_clk.freq)
			      : base - (long long) ticks_ns (s_clk.cnt - c, s_clk.freq);
}

int __posix_tz_minutes (void)
{
	return (int) (__armpi_sys1 (SYS_CLOCK_GET, 1) / 60);	/* (decalage donne a l'horloge du noyau) */
}

void __posix_sleep_ns (unsigned long long ns)
{
	/* SYS_SLEEP dort en millisecondes ; le reste (moins d'1 ms) : en cedant le processeur */
	unsigned long long end = __posix_mono_ns () + ns;
	if (ns >= 1000000ULL)
		__armpi_sys1 (SYS_SLEEP, ns / 1000000ULL);
	while (__posix_mono_ns () < end)
		__armpi_sys0 (SYS_YIELD);
}

/* ---- POSIX ---- */
static int clock_ok (clockid_t c)
{
	return c == CLOCK_REALTIME || c == CLOCK_MONOTONIC || c == CLOCK_MONOTONIC_RAW ||
	       c == CLOCK_MONOTONIC_COARSE || c == CLOCK_REALTIME_COARSE || c == CLOCK_BOOTTIME ||
	       c == CLOCK_PROCESS_CPUTIME_ID || c == CLOCK_THREAD_CPUTIME_ID;
}

int clock_gettime (clockid_t c, struct timespec *ts)
{
	if (!clock_ok (c) || ts == 0)
		return POSIX_ERR (EINVAL);
	if (c == CLOCK_REALTIME || c == CLOCK_REALTIME_COARSE)
	{
		long long ns = __posix_real_ns ();
		ts->tv_sec = (time_t) (ns / 1000000000LL);
		ts->tv_nsec = (long) (ns % 1000000000LL);
		if (ts->tv_nsec < 0)
		{
			ts->tv_nsec += 1000000000L;
			ts->tv_sec--;
		}
		return 0;
	}
	unsigned long long ns = __posix_mono_ns ();
	ts->tv_sec = (time_t) (ns / 1000000000ULL);
	ts->tv_nsec = (long) (ns % 1000000000ULL);
	return 0;
}

int clock_getres (clockid_t c, struct timespec *ts)
{
	if (!clock_ok (c))
		return POSIX_ERR (EINVAL);
	if (ts)
	{
		ensure (0);
		ts->tv_sec = 0;
		ts->tv_nsec = (long) (1000000000ULL / s_clk.freq);
		if (ts->tv_nsec == 0)
			ts->tv_nsec = 1;
	}
	return 0;
}

int clock_settime (clockid_t c, const struct timespec *ts) { (void) c; (void) ts; return POSIX_ERR (EPERM); }
int clock_getcpuclockid (pid_t pid, clockid_t *c) { (void) pid; *c = CLOCK_PROCESS_CPUTIME_ID; return 0; }

int clock_nanosleep (clockid_t c, int flags, const struct timespec *req, struct timespec *rem)
{
	if (!clock_ok (c) || req == 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L)
		return EINVAL;
	if (flags & TIMER_ABSTIME)
	{
		for (;;)
		{
			struct timespec now;
			clock_gettime (c, &now);
			long long d = ((long long) req->tv_sec - now.tv_sec) * 1000000000LL + (req->tv_nsec - now.tv_nsec);
			if (d <= 0)
				return 0;
			__posix_sleep_ns ((unsigned long long) d);
		}
	}
	/* (saturé : un tv_sec immense, std::thread::sleep de Rust par exemple, ne déborde pas) */
	unsigned long long ns = (unsigned long long) req->tv_sec >= 4000000000ULL ? (1ULL << 62)
		: (unsigned long long) req->tv_sec * 1000000000ULL + (unsigned long long) req->tv_nsec;
	__posix_sleep_ns (ns);
	if (rem)
	{
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	return 0;
}

int nanosleep (const struct timespec *req, struct timespec *rem)
{
	int r = clock_nanosleep (CLOCK_MONOTONIC, 0, req, rem);
	return r ? POSIX_ERR (r) : 0;
}

int usleep (useconds_t us)
{
	__posix_sleep_ns ((unsigned long long) us * 1000ULL);
	return 0;
}

unsigned sleep (unsigned s)
{
	__posix_sleep_ns ((unsigned long long) s * 1000000000ULL);
	return 0;
}

static long rpc_gettimeofday (long tv, long tz, long c)
{
	(void) tz; (void) c;
	struct timeval *t = (struct timeval *) tv;
	long long ns = __posix_real_ns ();
	t->tv_sec = (time_t) (ns / 1000000000LL);
	t->tv_usec = (suseconds_t) ((ns % 1000000000LL) / 1000);
	return 0;
}

int _gettimeofday (struct timeval *tv, void *tz)
{
	if (tv == 0)
		return 0;
	return (int) rpc_gettimeofday ((long) tv, (long) tz, 0);
}

clock_t _times (struct tms *t)
{
	clock_t ticks = (clock_t) (__posix_mono_ns () / (1000000000ULL / CLOCKS_PER_SEC));
	if (t)
	{
		t->tms_utime = ticks;
		t->tms_stime = 0;
		t->tms_cutime = 0;
		t->tms_cstime = 0;
	}
	return ticks;
}

/* timegm: UTC broken-down time -> seconds (mktime without the time zone) */
time_t timegm (struct tm *tm)
{
	int y = tm->tm_year + 1900, m = tm->tm_mon;
	y += m / 12;
	m %= 12;
	if (m < 0)
	{
		m += 12;
		y--;
	}
	long long days = days_from_civil (y, m + 1, 1) + tm->tm_mday - 1;
	long long t = ((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60LL + tm->tm_sec;
	struct tm out;
	time_t r = (time_t) t;
	if (gmtime_r (&r, &out))
		*tm = out;
	return r;
}

/* ---- minuterie du process (SYS_SETITIMER, docs/ABI.md 0x6F) : ITIMER_REAL, le noyau envoie
 * SIGALRM a echeance (puis toutes les it_interval) ; alarm, ualarm par elle. ITIMER_VIRTUAL et
 * ITIMER_PROF : EINVAL (pas de temps processeur par process). timer_create : ENOSYS ---- */
static int itimer (long value, long interval, long old[2])
{
	old[0] = old[1] = 0;
	long r = __armpi_sys6 (SYS_SETITIMER, 0, value, interval, (long) old, 0, 0);
	return r < 0 ? (int) POSIX_SYS (r) : 0;
}

static long tv_us (const struct timeval *t) { return (long) t->tv_sec * 1000000L + t->tv_usec; }
static void us_tv (long us, struct timeval *t) { t->tv_sec = us / 1000000L; t->tv_usec = us % 1000000L; }

int setitimer (int which, const struct itimerval *v, struct itimerval *o)
{
	if (which != ITIMER_REAL)
		return POSIX_ERR (EINVAL);
	long val = -1, iv = 0;				/* (-1 : lire seulement) */
	if (v)
	{
		if (v->it_value.tv_sec < 0 || v->it_value.tv_usec < 0 || v->it_value.tv_usec >= 1000000 ||
		    v->it_interval.tv_sec < 0 || v->it_interval.tv_usec < 0 || v->it_interval.tv_usec >= 1000000)
			return POSIX_ERR (EINVAL);
		val = tv_us (&v->it_value);
		iv = tv_us (&v->it_interval);
	}
	long old[2];
	if (itimer (val, iv, old) != 0)
		return -1;
	if (o)
	{
		us_tv (old[0], &o->it_value);
		us_tv (old[1], &o->it_interval);
	}
	return 0;
}

int getitimer (int which, struct itimerval *v)
{
	if (v == 0)
		return POSIX_ERR (EFAULT);
	return setitimer (which, 0, v);
}

unsigned alarm (unsigned s)
{
	long old[2];
	if (itimer ((long) s * 1000000L, 0, old) != 0)
		return 0;
	return (unsigned) ((old[0] + 999999) / 1000000);	/* (une fraction : une seconde) */
}

useconds_t ualarm (useconds_t us, useconds_t interval)
{
	long old[2];
	if (itimer ((long) us, (long) interval, old) != 0)
		return 0;
	return (useconds_t) old[0];
}

int timer_create (clockid_t c, struct sigevent *e, timer_t *t) { (void) c; (void) e; (void) t; return POSIX_ERR (ENOSYS); }
int timer_delete (timer_t t) { (void) t; return POSIX_ERR (ENOSYS); }
int timer_settime (timer_t t, int f, const struct itimerspec *v, struct itimerspec *o) { (void) t; (void) f; (void) v; (void) o; return POSIX_ERR (ENOSYS); }
int timer_gettime (timer_t t, struct itimerspec *v) { (void) t; (void) v; return POSIX_ERR (ENOSYS); }
int timer_getoverrun (timer_t t) { (void) t; return POSIX_ERR (ENOSYS); }
