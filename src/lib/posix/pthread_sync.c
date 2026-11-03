/*
 * pthread_sync.c -- mutex, variables de condition, verrous lecteurs-ecrivains, once, barrieres et
 * verrous actifs (libposix, docs/POSIX.md ; repris d'Onyx).
 *
 * Chaque objet est un mot de la memoire de l'appelant : un chemin rapide par compare-and-swap,
 * et le futex du noyau (SYS_FUTEX_WAIT avec delai, SYS_FUTEX_WAKE) pour dormir. Le reveil
 * reveille tous les dormeurs d'un mot : un signal est une diffusion (permis : les attentes
 * peuvent se reveiller pour rien, et chacune boucle sur sa condition).
 *
 * Le mutex est le « mutex 2 » de Drepper (0 libre, 1 pris, 2 pris avec des attentes) : un rendu
 * n'appelle le noyau que si quelqu'un peut dormir. Une variable de condition est un numero de
 * sequence : une attente le lit, rend le mutex et dort tant qu'il n'a pas change. Les attentes
 * avec delai convertissent l'echeance absolue (CLOCK_REALTIME, ou l'horloge de
 * pthread_condattr_setclock / *_clockwait) en millisecondes a chaque tour.
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
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "posix_internal.h"

#define LOAD(p)		__atomic_load_n ((p), __ATOMIC_ACQUIRE)
#define STORE(p, v)	__atomic_store_n ((p), (v), __ATOMIC_RELEASE)
#define XCHG(p, v)	__atomic_exchange_n ((p), (v), __ATOMIC_ACQ_REL)
#define INC(p)		__atomic_add_fetch ((p), 1, __ATOMIC_ACQ_REL)
#define DEC(p)		__atomic_sub_fetch ((p), 1, __ATOMIC_ACQ_REL)

static inline int cas (volatile unsigned *p, unsigned expect, unsigned v)
{
	return __atomic_compare_exchange_n (p, &expect, v, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
static inline int cas_int (volatile int *p, int expect, int v)
{
	return __atomic_compare_exchange_n (p, &expect, v, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* ---- the futex ------------------------------------------------------------------------------ */
int __posix_futex_wait (volatile unsigned *addr, unsigned expected, unsigned ms)
{
	if (ms == 0)				/* (un delai nul : regarder seulement) */
		return LOAD (addr) == expected;
	/* SYS_FUTEX_WAIT : delai en ms, 0 = sans fin -> 0, E_AGAIN (deja change), E_TIMEDOUT */
	long r = __armpi_sys3 (SYS_FUTEX_WAIT, addr, expected, ms == POSIX_WAIT_FOREVER ? 0 : ms);
	return r == -110;
}

void __posix_futex_wake (volatile unsigned *addr)
{
	__armpi_sys2 (SYS_FUTEX_WAKE, addr, 0x7FFFFFFF);	/* (tous : un signal est une diffusion) */
}

long long __posix_ms_until (clockid_t clock, const struct timespec *abs)
{
	long long now = clock == CLOCK_MONOTONIC || clock == CLOCK_MONOTONIC_RAW ||
			clock == CLOCK_MONOTONIC_COARSE || clock == CLOCK_BOOTTIME
		? (long long) __posix_mono_ns () : __posix_real_ns ();
	long long target = (long long) abs->tv_sec * 1000000000LL + abs->tv_nsec;
	long long d = target - now;
	if (d <= 0)
		return -1;
	long long ms = (d + 999999) / 1000000;
	if (ms > 24LL * 3600 * 1000)
		ms = 24LL * 3600 * 1000;		/* (a day at a time: the loop goes on) */
	return ms;
}

static int valid_ts (const struct timespec *ts)
{
	return ts != 0 && ts->tv_nsec >= 0 && ts->tv_nsec < 1000000000L;
}

/* the internal lock (a bare "mutex 2" word) */
static int lock_word (volatile unsigned *l, clockid_t clock, const struct timespec *abs)
{
	unsigned c = 0;
	if (cas (l, 0, 1))
		return 0;
	c = LOAD (l);
	if (c != 2)
		c = XCHG (l, 2);
	while (c != 0)
	{
		unsigned ms = POSIX_WAIT_FOREVER;
		if (abs)
		{
			long long left = __posix_ms_until (clock, abs);
			if (left < 0)
				return ETIMEDOUT;
			ms = (unsigned) left;
		}
		__posix_futex_wait (l, 2, ms);
		c = XCHG (l, 2);
	}
	return 0;
}

static void unlock_word (volatile unsigned *l)
{
	if (XCHG (l, 0) == 2)
		__posix_futex_wake (l);
}

void __posix_lock (volatile unsigned *l) { lock_word (l, CLOCK_REALTIME, 0); }
void __posix_unlock (volatile unsigned *l) { unlock_word (l); }

/* ---- mutexes -------------------------------------------------------------------------------- */
int pthread_mutexattr_init (pthread_mutexattr_t *a) { a->__type = PTHREAD_MUTEX_DEFAULT; a->__pshared = 0; return 0; }
int pthread_mutexattr_destroy (pthread_mutexattr_t *a) { (void) a; return 0; }
int pthread_mutexattr_settype (pthread_mutexattr_t *a, int t)
{
	if (t < PTHREAD_MUTEX_NORMAL || t > PTHREAD_MUTEX_ERRORCHECK)
		return EINVAL;
	a->__type = t;
	return 0;
}
int pthread_mutexattr_gettype (const pthread_mutexattr_t *a, int *t) { *t = a->__type; return 0; }
int pthread_mutexattr_setpshared (pthread_mutexattr_t *a, int p)
{
	if (p != PTHREAD_PROCESS_PRIVATE && p != PTHREAD_PROCESS_SHARED)
		return EINVAL;
	a->__pshared = p;			/* (the futex works on shared surfaces too) */
	return 0;
}
int pthread_mutexattr_getpshared (const pthread_mutexattr_t *a, int *p) { *p = a->__pshared; return 0; }
int pthread_mutexattr_setprotocol (pthread_mutexattr_t *a, int p) { (void) a; return p == PTHREAD_PRIO_NONE ? 0 : ENOTSUP; }
int pthread_mutexattr_getprotocol (const pthread_mutexattr_t *a, int *p) { (void) a; *p = PTHREAD_PRIO_NONE; return 0; }
int pthread_mutexattr_setrobust (pthread_mutexattr_t *a, int r) { (void) a; return r == PTHREAD_MUTEX_STALLED ? 0 : ENOTSUP; }
int pthread_mutexattr_getrobust (const pthread_mutexattr_t *a, int *r) { (void) a; *r = PTHREAD_MUTEX_STALLED; return 0; }

int pthread_mutex_init (pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
	m->__lock = 0;
	m->__type = a ? a->__type : PTHREAD_MUTEX_DEFAULT;
	m->__owner = 0;
	m->__count = 0;
	return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *m)
{
	return LOAD (&m->__lock) != 0 ? EBUSY : 0;
}

static int mutex_lock (pthread_mutex_t *m, clockid_t clock, const struct timespec *abs)
{
	int type = m->__type;
	int me = 0;
	if (type != PTHREAD_MUTEX_NORMAL)
	{
		me = __posix_owner_id ();
		if (m->__owner == me && LOAD (&m->__lock) != 0)
		{
			if (type == PTHREAD_MUTEX_RECURSIVE)
			{
				if (m->__count == 0x7FFFFFFF)
					return EAGAIN;
				m->__count++;
				return 0;
			}
			return EDEADLK;
		}
	}
	if (!cas (&m->__lock, 0, 1))
	{
		if (abs && !valid_ts (abs))
			return EINVAL;
		int r = lock_word (&m->__lock, clock, abs);
		if (r)
			return r;
	}
	if (type != PTHREAD_MUTEX_NORMAL)
	{
		m->__owner = me;
		m->__count = 1;
	}
	return 0;
}

int pthread_mutex_lock (pthread_mutex_t *m) { return mutex_lock (m, CLOCK_REALTIME, 0); }
int pthread_mutex_timedlock (pthread_mutex_t *m, const struct timespec *abs) { return mutex_lock (m, CLOCK_REALTIME, abs); }
int pthread_mutex_clocklock (pthread_mutex_t *m, clockid_t c, const struct timespec *abs) { return mutex_lock (m, c, abs); }

int pthread_mutex_trylock (pthread_mutex_t *m)
{
	int type = m->__type;
	int me = 0;
	if (type != PTHREAD_MUTEX_NORMAL)
	{
		me = __posix_owner_id ();
		if (m->__owner == me && LOAD (&m->__lock) != 0)
		{
			if (type == PTHREAD_MUTEX_RECURSIVE)
			{
				m->__count++;
				return 0;
			}
			return EBUSY;
		}
	}
	if (!cas (&m->__lock, 0, 1))
		return EBUSY;
	if (type != PTHREAD_MUTEX_NORMAL)
	{
		m->__owner = me;
		m->__count = 1;
	}
	return 0;
}

int pthread_mutex_unlock (pthread_mutex_t *m)
{
	if (m->__type != PTHREAD_MUTEX_NORMAL)
	{
		if (m->__owner != __posix_owner_id () || LOAD (&m->__lock) == 0)
			return EPERM;
		if (--m->__count > 0)
			return 0;
		m->__owner = 0;
	}
	unlock_word (&m->__lock);
	return 0;
}

int pthread_mutex_consistent (pthread_mutex_t *m) { (void) m; return EINVAL; }

/* ---- condition variables -------------------------------------------------------------------- */
int pthread_condattr_init (pthread_condattr_t *a) { a->__clock = CLOCK_REALTIME; a->__pshared = 0; return 0; }
int pthread_condattr_destroy (pthread_condattr_t *a) { (void) a; return 0; }
int pthread_condattr_setclock (pthread_condattr_t *a, clockid_t c)
{
	if (c != CLOCK_REALTIME && c != CLOCK_MONOTONIC)
		return EINVAL;
	a->__clock = (int) c;
	return 0;
}
int pthread_condattr_getclock (const pthread_condattr_t *a, clockid_t *c) { *c = a->__clock ? (clockid_t) a->__clock : CLOCK_REALTIME; return 0; }
int pthread_condattr_setpshared (pthread_condattr_t *a, int p) { a->__pshared = p; return 0; }
int pthread_condattr_getpshared (const pthread_condattr_t *a, int *p) { *p = a->__pshared; return 0; }

int pthread_cond_init (pthread_cond_t *c, const pthread_condattr_t *a)
{
	c->__seq = 0;
	c->__clock = a ? a->__clock : (int) CLOCK_REALTIME;
	c->__waiters = 0;
	c->__pad = 0;
	return 0;
}

int pthread_cond_destroy (pthread_cond_t *c)
{
	return LOAD (&c->__waiters) != 0 ? EBUSY : 0;
}

static int cond_wait (pthread_cond_t *c, pthread_mutex_t *m, clockid_t clock, const struct timespec *abs)
{
	if (abs && !valid_ts (abs))
		return EINVAL;
	/* the mutex fully released (a recursive one too), its state kept */
	int type = m->__type, owner = m->__owner, count = m->__count;
	if (type != PTHREAD_MUTEX_NORMAL && (owner != __posix_owner_id () || LOAD (&m->__lock) == 0))
		return EPERM;

	unsigned seq = LOAD (&c->__seq);
	INC (&c->__waiters);
	if (type != PTHREAD_MUTEX_NORMAL)
	{
		m->__owner = 0;
		m->__count = 0;
	}
	unlock_word (&m->__lock);

	int r = 0;
	while (LOAD (&c->__seq) == seq)
	{
		unsigned ms = POSIX_WAIT_FOREVER;
		if (abs)
		{
			long long left = __posix_ms_until (clock, abs);
			if (left < 0)
			{
				r = ETIMEDOUT;
				break;
			}
			ms = (unsigned) left;
		}
		__posix_futex_wait (&c->__seq, seq, ms);
	}
	DEC (&c->__waiters);

	/* back with the mutex, as "contended" (a waiter may sleep on it) */
	unsigned v = XCHG (&m->__lock, 2);
	while (v != 0)
	{
		__posix_futex_wait (&m->__lock, 2, POSIX_WAIT_FOREVER);
		v = XCHG (&m->__lock, 2);
	}
	if (type != PTHREAD_MUTEX_NORMAL)
	{
		m->__owner = owner;
		m->__count = count;
	}
	return r;
}

static clockid_t cond_clock (const pthread_cond_t *c)
{
	return c->__clock == CLOCK_MONOTONIC ? CLOCK_MONOTONIC : CLOCK_REALTIME;
}

int pthread_cond_wait (pthread_cond_t *c, pthread_mutex_t *m) { return cond_wait (c, m, CLOCK_REALTIME, 0); }
int pthread_cond_timedwait (pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abs)
{
	return cond_wait (c, m, cond_clock (c), abs);
}
int pthread_cond_clockwait (pthread_cond_t *c, pthread_mutex_t *m, clockid_t clock, const struct timespec *abs)
{
	if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
		return EINVAL;
	return cond_wait (c, m, clock, abs);
}

int pthread_cond_signal (pthread_cond_t *c)
{
	INC (&c->__seq);
	if (LOAD (&c->__waiters) != 0)
		__posix_futex_wake (&c->__seq);
	return 0;
}

int pthread_cond_broadcast (pthread_cond_t *c) { return pthread_cond_signal (c); }

/* ---- once ----------------------------------------------------------------------------------- */
int pthread_once (pthread_once_t *o, void (*fn) (void))
{
	for (;;)
	{
		int s = LOAD (&o->__state);
		if (s == 2)
			return 0;
		if (s == 0)
		{
			if (!cas_int (&o->__state, 0, 1))
				continue;
			fn ();
			if (XCHG (&o->__state, 2) == 3)
				__posix_futex_wake ((volatile unsigned *) &o->__state);
			return 0;
		}
		if (s == 1 && !cas_int (&o->__state, 1, 3))
			continue;
		__posix_futex_wait ((volatile unsigned *) &o->__state, 3, POSIX_WAIT_FOREVER);
	}
}

/* ---- reader-writer locks (readers first: a reader may take it again while a writer waits) ---- */
int pthread_rwlockattr_init (pthread_rwlockattr_t *a) { a->__pshared = 0; a->__pad = 0; return 0; }
int pthread_rwlockattr_destroy (pthread_rwlockattr_t *a) { (void) a; return 0; }
int pthread_rwlockattr_setpshared (pthread_rwlockattr_t *a, int p) { a->__pshared = p; return 0; }
int pthread_rwlockattr_getpshared (const pthread_rwlockattr_t *a, int *p) { *p = a->__pshared; return 0; }

int pthread_rwlock_init (pthread_rwlock_t *rw, const pthread_rwlockattr_t *a)
{
	(void) a;
	rw->__state = 0;
	rw->__seq = 0;
	rw->__waiters = 0;
	rw->__wwait = 0;
	return 0;
}

int pthread_rwlock_destroy (pthread_rwlock_t *rw) { return LOAD (&rw->__state) != 0 ? EBUSY : 0; }

static int rw_wait (pthread_rwlock_t *rw, unsigned seq, clockid_t clock, const struct timespec *abs)
{
	unsigned ms = POSIX_WAIT_FOREVER;
	if (abs)
	{
		long long left = __posix_ms_until (clock, abs);
		if (left < 0)
			return ETIMEDOUT;
		ms = (unsigned) left;
	}
	INC (&rw->__waiters);
	__posix_futex_wait (&rw->__seq, seq, ms);
	DEC (&rw->__waiters);
	return 0;
}

static int rw_rdlock (pthread_rwlock_t *rw, clockid_t clock, const struct timespec *abs)
{
	if (abs && !valid_ts (abs))
		return EINVAL;
	for (;;)
	{
		int s = LOAD (&rw->__state);
		if (s >= 0)
		{
			if (s == 0x7FFFFFFF)
				return EAGAIN;
			if (cas_int (&rw->__state, s, s + 1))
				return 0;
			continue;
		}
		unsigned seq = LOAD (&rw->__seq);
		if (LOAD (&rw->__state) >= 0)
			continue;
		int r = rw_wait (rw, seq, clock, abs);
		if (r)
			return r;
	}
}

static int rw_wrlock (pthread_rwlock_t *rw, clockid_t clock, const struct timespec *abs)
{
	if (abs && !valid_ts (abs))
		return EINVAL;
	for (;;)
	{
		if (cas_int (&rw->__state, 0, -1))
			return 0;
		unsigned seq = LOAD (&rw->__seq);
		if (LOAD (&rw->__state) == 0)
			continue;
		INC (&rw->__wwait);
		int r = rw_wait (rw, seq, clock, abs);
		DEC (&rw->__wwait);
		if (r)
			return r;
	}
}

int pthread_rwlock_rdlock (pthread_rwlock_t *rw) { return rw_rdlock (rw, CLOCK_REALTIME, 0); }
int pthread_rwlock_timedrdlock (pthread_rwlock_t *rw, const struct timespec *abs) { return rw_rdlock (rw, CLOCK_REALTIME, abs); }
int pthread_rwlock_clockrdlock (pthread_rwlock_t *rw, clockid_t c, const struct timespec *abs) { return rw_rdlock (rw, c, abs); }
int pthread_rwlock_wrlock (pthread_rwlock_t *rw) { return rw_wrlock (rw, CLOCK_REALTIME, 0); }
int pthread_rwlock_timedwrlock (pthread_rwlock_t *rw, const struct timespec *abs) { return rw_wrlock (rw, CLOCK_REALTIME, abs); }
int pthread_rwlock_clockwrlock (pthread_rwlock_t *rw, clockid_t c, const struct timespec *abs) { return rw_wrlock (rw, c, abs); }

int pthread_rwlock_tryrdlock (pthread_rwlock_t *rw)
{
	for (;;)
	{
		int s = LOAD (&rw->__state);
		if (s < 0)
			return EBUSY;
		if (cas_int (&rw->__state, s, s + 1))
			return 0;
	}
}

int pthread_rwlock_trywrlock (pthread_rwlock_t *rw)
{
	return cas_int (&rw->__state, 0, -1) ? 0 : EBUSY;
}

int pthread_rwlock_unlock (pthread_rwlock_t *rw)
{
	for (;;)
	{
		int s = LOAD (&rw->__state);
		if (s == 0)
			return EPERM;
		int n = s < 0 ? 0 : s - 1;
		if (cas_int (&rw->__state, s, n))
		{
			if (n == 0)
			{
				INC (&rw->__seq);
				if (LOAD (&rw->__waiters) != 0)
					__posix_futex_wake (&rw->__seq);
			}
			return 0;
		}
	}
}

/* ---- barriers ------------------------------------------------------------------------------- */
int pthread_barrierattr_init (pthread_barrierattr_t *a) { a->__pshared = 0; return 0; }
int pthread_barrierattr_destroy (pthread_barrierattr_t *a) { (void) a; return 0; }
int pthread_barrierattr_setpshared (pthread_barrierattr_t *a, int p) { a->__pshared = p; return 0; }
int pthread_barrierattr_getpshared (const pthread_barrierattr_t *a, int *p) { *p = a->__pshared; return 0; }

int pthread_barrier_init (pthread_barrier_t *b, const pthread_barrierattr_t *a, unsigned count)
{
	(void) a;
	if (count == 0)
		return EINVAL;
	b->__seq = 0;
	b->__in = 0;
	b->__count = count;
	b->__pad = 0;
	return 0;
}

int pthread_barrier_destroy (pthread_barrier_t *b) { return LOAD (&b->__in) != 0 ? EBUSY : 0; }

int pthread_barrier_wait (pthread_barrier_t *b)
{
	unsigned seq = LOAD (&b->__seq);
	if (INC (&b->__in) == b->__count)
	{
		/* the last one in: nobody of this round can come back before the bump */
		STORE (&b->__in, 0);
		INC (&b->__seq);
		__posix_futex_wake (&b->__seq);
		return PTHREAD_BARRIER_SERIAL_THREAD;
	}
	while (LOAD (&b->__seq) == seq)
		__posix_futex_wait (&b->__seq, seq, POSIX_WAIT_FOREVER);
	return 0;
}

/* ---- spin locks ----------------------------------------------------------------------------- */
int pthread_spin_init (pthread_spinlock_t *l, int pshared) { (void) pshared; *l = 0; return 0; }
int pthread_spin_destroy (pthread_spinlock_t *l) { (void) l; return 0; }
int pthread_spin_trylock (pthread_spinlock_t *l) { return XCHG (l, 1) ? EBUSY : 0; }
int pthread_spin_unlock (pthread_spinlock_t *l) { STORE (l, 0); return 0; }
int pthread_spin_lock (pthread_spinlock_t *l)
{
	unsigned spins = 0;
	while (XCHG (l, 1))
		while (LOAD (l))
		{
			/* le detenteur a pu etre preempte sur le meme coeur : lui laisser la place */
			if (++spins > 64)
				__armpi_sys0 (SYS_YIELD);
			else
				__asm__ volatile ("yield");
		}
	return 0;
}
