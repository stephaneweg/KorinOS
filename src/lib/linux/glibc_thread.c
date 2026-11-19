/*
 * glibc_thread.c -- liblinux : les threads de la glibc de compatibilité (glibc_internal.h,
 * docs/etude/WIDEVINE.md § 3.1).
 *
 * Les objets de glibc (pthread_mutex_t de 40 octets, pthread_cond_t de 48, pthread_rwlock_t de 56,
 * sem_t de 32, pthread_attr_t de 56) sont plus grands que ceux de libposix ou d'une autre forme ; on n'en
 * lit jamais les champs : leur premier mot porte une « boîte », un objet de libposix alloué à la première
 * utilisation (les initialiseurs statiques de glibc valent zéro : boîte vide). Les erreurs rendues
 * (pas dans errno) sont traduites en numéros de Linux (ETIMEDOUT : 110).
 *
 * Pas encore : mutex récursifs ou à vérification d'erreur (le module n'importe pas
 * pthread_mutexattr_settype), annulation, priorités (acceptées sans effet).
 */
#define _GNU_SOURCE	/* (dl_iterate_phdr, pthread_setname_np) */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "linux.h"
#include "glibc_internal.h"

/* ---- boîtes ---- */
typedef void *(*box_make) (void);
typedef void (*box_free) (void *);

static void *box (void *obj, box_make make, box_free unmake)
{
	void **slot = (void **) obj;
	void *p = __atomic_load_n (slot, __ATOMIC_ACQUIRE);
	if (p)
		return p;
	void *n = make ();
	if (!n)
		return 0;
	void *expected = 0;
	if (!__atomic_compare_exchange_n (slot, &expected, n, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
	{
		unmake (n);	/* (un autre thread a fait la boîte) */
		return expected;
	}
	return n;
}

static void *unbox (void *obj)
{
	void **slot = (void **) obj;
	return __atomic_exchange_n (slot, (void *) 0, __ATOMIC_ACQ_REL);
}

/* horloges de Linux (condattr) -> libposix */
static clockid_t clock_of (int linux_id)
{
	return linux_id == 1 || linux_id == 4 || linux_id == 6 || linux_id == 7 ? CLOCK_MONOTONIC : CLOCK_REALTIME;
}

/* ---- mutex ---- */
static void *mutex_make (void)
{
	pthread_mutex_t *m = malloc (sizeof *m);
	if (m)
		pthread_mutex_init (m, 0);
	return m;
}

static void mutex_unmake (void *p)
{
	pthread_mutex_destroy (p);
	free (p);
}

static pthread_mutex_t *M (void *obj) { return box (obj, mutex_make, mutex_unmake); }

static int lx_mutex_init (void *m, const void *attr)
{
	(void) attr;
	memset (m, 0, 40);
	return 0;
}

static int lx_mutex_lock (void *m) { pthread_mutex_t *x = M (m); return x ? lx_perr (pthread_mutex_lock (x)) : 12; }
static int lx_mutex_trylock (void *m) { pthread_mutex_t *x = M (m); return x ? lx_perr (pthread_mutex_trylock (x)) : 12; }
static int lx_mutex_unlock (void *m) { pthread_mutex_t *x = M (m); return x ? lx_perr (pthread_mutex_unlock (x)) : 12; }

static int lx_mutex_destroy (void *m)
{
	void *p = unbox (m);
	if (p)
		mutex_unmake (p);
	return 0;
}

static int lx_mutexattr_init (void *a) { memset (a, 0, 4); return 0; }
static int lx_mutexattr_destroy (void *a) { (void) a; return 0; }

/* ---- conditions : condattr de glibc = un int ; on y garde l'horloge de Linux + 1 ---- */
static int lx_condattr_init (int *a) { *a = 0; return 0; }
static int lx_condattr_destroy (int *a) { (void) a; return 0; }
static int lx_condattr_setclock (int *a, int clk) { *a = clk + 1; return 0; }

static __thread int s_cond_clock;	/* (l'horloge de la boîte en cours de fabrication) */

static void *cond_make (void)
{
	pthread_cond_t *c = malloc (sizeof *c);
	if (!c)
		return 0;
	pthread_condattr_t a;
	pthread_condattr_init (&a);
	pthread_condattr_setclock (&a, clock_of (s_cond_clock));
	pthread_cond_init (c, &a);
	pthread_condattr_destroy (&a);
	return c;
}

static void cond_unmake (void *p)
{
	pthread_cond_destroy (p);
	free (p);
}

static pthread_cond_t *C (void *obj) { return box (obj, cond_make, cond_unmake); }

static int lx_cond_init (void *c, const int *attr)
{
	memset (c, 0, 48);
	s_cond_clock = attr && *attr ? *attr - 1 : 0;
	pthread_cond_t *x = C (c);
	s_cond_clock = 0;
	return x ? 0 : 12;
}

static int lx_cond_destroy (void *c)
{
	void *p = unbox (c);
	if (p)
		cond_unmake (p);
	return 0;
}

static int lx_cond_wait (void *c, void *m)
{
	pthread_cond_t *x = C (c);
	pthread_mutex_t *y = M (m);
	return x && y ? lx_perr (pthread_cond_wait (x, y)) : 12;
}

static int lx_cond_timedwait (void *c, void *m, const struct timespec *t)
{
	pthread_cond_t *x = C (c);
	pthread_mutex_t *y = M (m);
	return x && y ? lx_perr (pthread_cond_timedwait (x, y, t)) : 12;
}

static int lx_cond_signal (void *c) { pthread_cond_t *x = C (c); return x ? lx_perr (pthread_cond_signal (x)) : 12; }
static int lx_cond_broadcast (void *c) { pthread_cond_t *x = C (c); return x ? lx_perr (pthread_cond_broadcast (x)) : 12; }

/* ---- verrous lecteurs/écrivains ---- */
static void *rw_make (void)
{
	pthread_rwlock_t *r = malloc (sizeof *r);
	if (r)
		pthread_rwlock_init (r, 0);
	return r;
}

static void rw_unmake (void *p)
{
	pthread_rwlock_destroy (p);
	free (p);
}

static pthread_rwlock_t *R (void *obj) { return box (obj, rw_make, rw_unmake); }

static int lx_rw_init (void *r, const void *a) { (void) a; memset (r, 0, 56); return 0; }
static int lx_rw_destroy (void *r) { void *p = unbox (r); if (p) rw_unmake (p); return 0; }
static int lx_rw_rdlock (void *r) { pthread_rwlock_t *x = R (r); return x ? lx_perr (pthread_rwlock_rdlock (x)) : 12; }
static int lx_rw_wrlock (void *r) { pthread_rwlock_t *x = R (r); return x ? lx_perr (pthread_rwlock_wrlock (x)) : 12; }
static int lx_rw_unlock (void *r) { pthread_rwlock_t *x = R (r); return x ? lx_perr (pthread_rwlock_unlock (x)) : 12; }

/* ---- sémaphores (sem_t de glibc : 32 octets ; errno pour les erreurs) ---- */
static void *sem_make (void) { return malloc (sizeof (sem_t)); }
static void sem_unmake (void *p) { sem_destroy (p); free (p); }

static int lx_sem_init (void *s, int shared, unsigned v)
{
	memset (s, 0, 32);
	sem_t *x = sem_make ();
	if (!x || sem_init (x, shared, v))
	{
		free (x);
		return (int) lx_set_errno_from_korin ();
	}
	*(void **) s = x;
	return 0;
}

static sem_t *S (void *s) { return *(sem_t **) s; }
static int lx_sem_destroy (void *s) { void *p = unbox (s); if (p) sem_unmake (p); return 0; }
static int lx_sem_wait (void *s) { return sem_wait (S (s)) ? (int) lx_set_errno_from_korin () : 0; }
static int lx_sem_trywait (void *s) { return sem_trywait (S (s)) ? (int) lx_set_errno_from_korin () : 0; }
static int lx_sem_post (void *s) { return sem_post (S (s)) ? (int) lx_set_errno_from_korin () : 0; }
static int lx_sem_timedwait (void *s, const struct timespec *t)
{
	return sem_timedwait (S (s), t) ? (int) lx_set_errno_from_korin () : 0;
}

/* ---- once ---- */
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

/* ---- threads : pthread_attr_t de glibc (56 octets) : {marque, taille de pile, détaché} à nous ---- */
struct lx_attr
{
	uint32_t magic;
	uint32_t detached;
	uint64_t stacksize;
};
#define LX_ATTR_MAGIC 0x4c584154	/* 'LXAT' */

static int lx_attr_init (struct lx_attr *a)
{
	memset (a, 0, 56);
	a->magic = LX_ATTR_MAGIC;
	return 0;
}

static int lx_attr_destroy (struct lx_attr *a) { (void) a; return 0; }
static int lx_attr_setdetachstate (struct lx_attr *a, int d) { a->detached = d == 1; return 0; }	/* (1 : DETACHED) */
static int lx_attr_setstacksize (struct lx_attr *a, size_t n) { a->stacksize = n; return 0; }

static int lx_create (unsigned long *t, const struct lx_attr *a, void *(*fn) (void *), void *arg)
{
	pthread_attr_t pa;
	pthread_attr_init (&pa);
	if (a && a->magic == LX_ATTR_MAGIC)
	{
		if (a->stacksize)
			pthread_attr_setstacksize (&pa, a->stacksize);
		if (a->detached)
			pthread_attr_setdetachstate (&pa, PTHREAD_CREATE_DETACHED);
	}
	pthread_t pt;
	int r = pthread_create (&pt, &pa, fn, arg);
	pthread_attr_destroy (&pa);
	if (!r)
		*t = (unsigned long) pt;
	return lx_perr (r);
}

static int lx_join (unsigned long t, void **ret) { return lx_perr (pthread_join ((pthread_t) t, ret)); }
static int lx_detach (unsigned long t) { return lx_perr (pthread_detach ((pthread_t) t)); }
static unsigned long lx_pthread_self (void) { return (unsigned long) pthread_self (); }
static int lx_setname (unsigned long t, const char *n) { return lx_perr (pthread_setname_np ((pthread_t) t, n)); }
static int lx_key_create (unsigned *k, void (*d) (void *)) { return lx_perr (pthread_key_create ((pthread_key_t *) k, d)); }
static int lx_key_delete (unsigned k) { return lx_perr (pthread_key_delete ((pthread_key_t) k)); }
static void *lx_getspecific (unsigned k) { return pthread_getspecific ((pthread_key_t) k); }
static int lx_setspecific (unsigned k, const void *v) { return lx_perr (pthread_setspecific ((pthread_key_t) k, v)); }

/* affinité : tous les cœurs (cpu_set_t de Linux : un masque de bits) */
static int fill_cpus (size_t size, void *set)
{
	long n = sysconf (_SC_NPROCESSORS_ONLN);
	if (n < 1)
		n = 1;
	memset (set, 0, size);
	for (long i = 0; i < n && (size_t) i < size * 8; i++)
		((unsigned char *) set)[i / 8] |= (unsigned char) (1 << (i % 8));
	return 0;
}

static int lx_getaffinity_np (unsigned long t, size_t size, void *set) { (void) t; return fill_cpus (size, set); }
static int lx_sched_getaffinity (int pid, size_t size, void *set) { (void) pid; return fill_cpus (size, set); }

static int lx_sched_cpucount (size_t size, const void *set)
{
	int n = 0;
	for (size_t i = 0; i < size; i++)
		n += __builtin_popcount (((const unsigned char *) set)[i]);
	return n;
}

/* priorités : acceptées, sans effet */
static int lx_setpriority (int which, unsigned who, int prio) { (void) which; (void) who; (void) prio; return 0; }
static int lx_sched_setscheduler (int pid, int pol, const void *p) { (void) pid; (void) pol; (void) p; return 0; }
static int lx_register_atfork (void (*a) (void), void (*b) (void), void (*c) (void), void *d)
{
	(void) a; (void) b; (void) c; (void) d;
	return 0;	/* (pas de fork dans l'hôte) */
}

const struct lx_entry lx_table_thread[] =
{
	LX_W ("pthread_mutex_init", lx_mutex_init), LX_W ("pthread_mutex_lock", lx_mutex_lock),
	LX_W ("pthread_mutex_trylock", lx_mutex_trylock), LX_W ("pthread_mutex_unlock", lx_mutex_unlock),
	LX_W ("pthread_mutex_destroy", lx_mutex_destroy), LX_W ("pthread_mutexattr_init", lx_mutexattr_init),
	LX_W ("pthread_mutexattr_destroy", lx_mutexattr_destroy),
	LX_W ("pthread_condattr_init", lx_condattr_init), LX_W ("pthread_condattr_destroy", lx_condattr_destroy),
	LX_W ("pthread_condattr_setclock", lx_condattr_setclock), LX_W ("pthread_cond_init", lx_cond_init),
	LX_W ("pthread_cond_destroy", lx_cond_destroy), LX_W ("pthread_cond_wait", lx_cond_wait),
	LX_W ("pthread_cond_timedwait", lx_cond_timedwait), LX_W ("pthread_cond_signal", lx_cond_signal),
	LX_W ("pthread_cond_broadcast", lx_cond_broadcast),
	LX_W ("pthread_rwlock_init", lx_rw_init), LX_W ("pthread_rwlock_destroy", lx_rw_destroy),
	LX_W ("pthread_rwlock_rdlock", lx_rw_rdlock), LX_W ("pthread_rwlock_wrlock", lx_rw_wrlock),
	LX_W ("pthread_rwlock_unlock", lx_rw_unlock),
	LX_W ("sem_init", lx_sem_init), LX_W ("sem_destroy", lx_sem_destroy), LX_W ("sem_wait", lx_sem_wait),
	LX_W ("sem_trywait", lx_sem_trywait), LX_W ("sem_post", lx_sem_post), LX_W ("sem_timedwait", lx_sem_timedwait),
	LX_W ("pthread_once", lx_once),
	LX_W ("pthread_attr_init", lx_attr_init), LX_W ("pthread_attr_destroy", lx_attr_destroy),
	LX_W ("pthread_attr_setdetachstate", lx_attr_setdetachstate),
	LX_W ("pthread_attr_setstacksize", lx_attr_setstacksize),
	LX_W ("pthread_create", lx_create), LX_W ("pthread_join", lx_join), LX_W ("pthread_detach", lx_detach),
	LX_W ("pthread_self", lx_pthread_self), LX_W ("pthread_setname_np", lx_setname),
	LX_W ("pthread_key_create", lx_key_create), LX_W ("pthread_key_delete", lx_key_delete),
	LX_W ("pthread_getspecific", lx_getspecific), LX_W ("pthread_setspecific", lx_setspecific),
	LX_W ("pthread_getaffinity_np", lx_getaffinity_np), LX_W ("sched_getaffinity", lx_sched_getaffinity),
	LX_W ("__sched_cpucount", lx_sched_cpucount), LX_W ("setpriority", lx_setpriority),
	LX_W ("sched_setscheduler", lx_sched_setscheduler), LX_W ("__register_atfork", lx_register_atfork),
};
const size_t lx_table_thread_n = sizeof lx_table_thread / sizeof lx_table_thread[0];
