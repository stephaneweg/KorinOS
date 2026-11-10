/*
 * pthread.c -- threads POSIX sur les threads du noyau d'ARMPI (libposix, docs/POSIX.md ; repris
 * d'Onyx) : create / join / detach / exit / self, attributs, cles, noms, ordonnancement.
 *
 * pthread_create prend le bloc du thread (tls.c : struct __posix_thread + TCB + image TLS) et sa
 * pile (une region de mmap, peuplee a la demande, 256 Ko par defaut, sous une page de garde : un
 * debordement est une faute, plus une ecriture dans le tas), puis lance un
 * thread du noyau dessus : SYS_THREAD_CREATE_EX (sommet de pile, argument, TPIDR_EL0 = le TCB,
 * priorite, nom). Le thread du noyau est toujours joignable : un join dort sur le mot d'etat du
 * bloc (futex), puis SYS_THREAD_JOIN (il finit) et rend bloc et pile. Le bloc d'un thread detache
 * est rendu par un pthread_create (ou pthread_detach) suivant, une fois le thread fini.
 *
 * Fin d'un thread : ses fonctions de nettoyage, les destructeurs de ses cles (4 tours), puis le
 * resultat et le mot d'etat (join reveilles), puis SYS_THREAD_EXIT. pthread_exit du thread
 * principal attend les autres, puis exit (0). Annulation non geree (pthread_cancel -> ENOSYS).
 * Priorite > 0 (SCHED_FIFO / SCHED_RR) : la priorite haute d'ARMPI.
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
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include "posix_internal.h"

#define LOAD(p)		__atomic_load_n ((p), __ATOMIC_ACQUIRE)
#define STORE(p, v)	__atomic_store_n ((p), (v), __ATOMIC_RELEASE)

static inline struct __posix_thread *T (pthread_t t) { return (struct __posix_thread *) t; }

static int valid (pthread_t t)
{
	if (t == 0)
		return 0;
	int ok = 0;
	__posix_thread_list_lock ();
	for (struct __posix_thread *p = __posix_thread_list_first (); p != 0; p = p->next)
		if (p == T (t))
		{
			ok = p->magic == POSIX_THREAD_MAGIC;
			break;
		}
	__posix_thread_list_unlock ();
	return ok;
}

/* ---- stack bounds ---- */
static void stack_bounds (struct __posix_thread *t, int tid)
{
	(void) tid;
	if (t->is_main)
	{
		/* la pile du thread principal : USER_STACK_SIZE sous USER_STACK_TOP (proc.asm) */
		t->stack_hi = ARMPI_USER_STACK_TOP;
		t->stack_lo = ARMPI_USER_STACK_TOP - ARMPI_USER_STACK_SIZE;
		t->guard = 0;
		return;
	}
	/* un pthread : sa pile est la region prise par pthread_create, au-dessus de la garde */
	t->stack_lo = (unsigned long) t->stack_block + t->guard;
	t->stack_hi = t->stack_lo + t->stack_size;
}

/* la region de la pile de t, page de garde comprise -> 0 si echec */
static int stack_alloc (struct __posix_thread *t, unsigned long guard)
{
	void *p = mmap (0, guard + t->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return 0;
	if (guard != 0 && mprotect (p, guard, PROT_NONE) != 0)
	{
		munmap (p, guard + t->stack_size);
		return 0;
	}
	t->stack_block = p;
	t->guard = guard;
	return 1;
}

static void stack_free (struct __posix_thread *t)
{
	if (t->stack_block != 0)
		munmap (t->stack_block, t->guard + t->stack_size);
}

/* ---- the end of a thread ---- */
static void run_cleanups (struct __posix_thread *t)
{
	while (t->cleanup)
	{
		struct __posix_cleanup *c = t->cleanup;
		t->cleanup = c->__prev;
		c->__routine (c->__arg);
	}
}

__attribute__ ((noreturn)) static void thread_finish (struct __posix_thread *t, void *result)
{
	run_cleanups (t);
	__posix_keys_run_destructors (t);

	t->result = result;
	STORE (&t->state, POSIX_T_FINISHED);
	__posix_futex_wake ((volatile unsigned *) &t->state);
	/* plus rien du bloc ni de la pile n'est touche : un join peut les rendre des que le
	   thread du noyau a fini (thread_join) */
	__armpi_sys1 (SYS_THREAD_EXIT, 0);
	for (;;)
		;
}

static int thread_entry (void *p)
{
	struct __posix_thread *t = (struct __posix_thread *) p;
	unsigned long tp = __posix_thread_tp (t);
	__asm__ volatile ("msr tpidr_el0, %0" :: "r" (tp) : "memory");	/* (deja pose par le noyau) */
	t->tid = (int) __armpi_sys0 (SYS_THREAD_SELF);
	stack_bounds (t, 0);
	thread_finish (t, t->start (t->arg));
}

/* the blocks of detached threads that ended */
static void reap_detached (void)
{
	for (;;)
	{
		struct __posix_thread *dead = 0;
		__posix_thread_list_lock ();
		for (struct __posix_thread *p = __posix_thread_list_first (); p != 0; p = p->next)
			if (!p->is_main && p->detached && LOAD (&p->state) == POSIX_T_FINISHED)
			{
				p->detached = 0;	/* pris sous le verrou : un seul appelant le rend */
				dead = p;
				break;
			}
		__posix_thread_list_unlock ();
		if (dead == 0)
			return;
		__armpi_sys1 (SYS_THREAD_JOIN, dead->tid);	/* (il finit ; E_SRCH : deja parti) */
		__posix_thread_list_remove (dead);
		stack_free (dead);
		__posix_thread_free (dead);
	}
}

int pthread_create (pthread_t *out, const pthread_attr_t *attr, void *(*fn) (void *), void *arg)
{
	if (fn == 0)
		return EINVAL;
	reap_detached ();

	struct __posix_thread *t = __posix_thread_alloc ();
	if (t == 0)
		return EAGAIN;
	t->start = fn;
	t->arg = arg;
	int detached = attr != 0 && attr->__detachstate == PTHREAD_CREATE_DETACHED;
	t->detached = detached;
	t->prio =attr != 0 && attr->__inheritsched == PTHREAD_EXPLICIT_SCHED && attr->__priority > 0;
	unsigned long ss = attr != 0 ? attr->__stacksize : 0;
	strcpy (t->name, "pthread");
	__posix_thread_list_add (t);

	t->stack_size = ss ? ss : POSIX_STACK_DEFAULT;
	if (t->stack_size < PTHREAD_STACK_MIN) t->stack_size = PTHREAD_STACK_MIN;
	if (t->stack_size > POSIX_STACK_MAX) t->stack_size = POSIX_STACK_MAX;
	t->stack_size = (t->stack_size + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1);
	unsigned long guard = attr != 0 ? attr->__guardsize : POSIX_PAGE;
	guard = (guard + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1);
	long tid = -12;
	if (stack_alloc (t, guard))
		/* SYS_THREAD_CREATE_EX : entree, sommet de pile, argument, TPIDR_EL0, priorite, nom */
		tid = __armpi_sys6 (SYS_THREAD_CREATE_EX, thread_entry,
				    (char *) t->stack_block + guard + t->stack_size, t, __posix_thread_tp (t),
				    t->prio ? THREAD_PRIO_HIGH : THREAD_PRIO_NORMAL, t->name);
	if (tid < 0)
	{
		__posix_thread_list_remove (t);
		stack_free (t);
		__posix_thread_free (t);
		return tid == -22 ? EINVAL : EAGAIN;
	}
	if (!detached)			/* detache : fini, il a pu etre rendu ; thread_entry a pose le tid */
		t->tid = tid;
	*out = (pthread_t) t;
	return 0;
}

static int join (pthread_t th, void **ret, int mode, const struct timespec *abs)
{
	struct __posix_thread *t = T (th);
	if (!valid (th))
		return ESRCH;
	if (t == __posix_self ())
		return EDEADLK;
	if (t->detached || t->is_main)
		return EINVAL;
	while (LOAD (&t->state) != POSIX_T_FINISHED)
	{
		unsigned ms = POSIX_WAIT_FOREVER;
		if (mode == 1)
			return EBUSY;
		if (mode == 2)
		{
			long long left = __posix_ms_until (CLOCK_REALTIME, abs);
			if (left < 0)
				return ETIMEDOUT;
			ms = (unsigned) left;
		}
		__posix_futex_wait ((volatile unsigned *) &t->state, POSIX_T_RUNNING, ms);
	}
	__armpi_sys1 (SYS_THREAD_JOIN, t->tid);	/* (il finit ; E_SRCH : deja parti) */
	if (ret)
		*ret = t->result;
	__posix_thread_list_remove (t);
	stack_free (t);
	__posix_thread_free (t);
	return 0;
}

int pthread_join (pthread_t t, void **ret) { return join (t, ret, 0, 0); }
int pthread_tryjoin_np (pthread_t t, void **ret) { return join (t, ret, 1, 0); }
int pthread_timedjoin_np (pthread_t t, void **ret, const struct timespec *abs) { return join (t, ret, 2, abs); }

int pthread_detach (pthread_t th)
{
	struct __posix_thread *t = T (th);
	if (!valid (th))
		return ESRCH;
	if (t->detached || t->is_main)
		return EINVAL;
	t->detached = 1;
	if (t != __posix_self ())
		reap_detached ();
	return 0;
}

void pthread_exit (void *result)
{
	struct __posix_thread *t = __posix_self ();
	if (!t->is_main)
		thread_finish (t, result);
	/* the main thread: the process lives on while other threads run; it ends with the last */
	run_cleanups (t);
	__posix_keys_run_destructors (t);
	for (;;)
	{
		struct __posix_thread *running = 0;
		__posix_thread_list_lock ();
		for (struct __posix_thread *p = __posix_thread_list_first (); p != 0; p = p->next)
			if (!p->is_main && LOAD (&p->state) != POSIX_T_FINISHED)
			{
				running = p;
				break;
			}
		__posix_thread_list_unlock ();
		if (running == 0)
			break;
		__posix_futex_wait ((volatile unsigned *) &running->state, POSIX_T_RUNNING, 1000);
	}
	exit (0);
}

pthread_t pthread_self (void) { return (pthread_t) __posix_self (); }
int pthread_equal (pthread_t a, pthread_t b) { return a == b; }
int pthread_yield (void) { __armpi_sys0 (SYS_YIELD); return 0; }
int sched_yield (void) { __armpi_sys0 (SYS_YIELD); return 0; }

int pthread_setname_np (pthread_t th, const char *name)
{
	if (name == 0)
		return EINVAL;
	if (strlen (name) >= sizeof T (th)->name)
		return ERANGE;
	strcpy (T (th)->name, name);		/* (the kernel's name is the one given at creation) */
	return 0;
}

int pthread_getname_np (pthread_t th, char *buf, size_t len)
{
	size_t n = strlen (T (th)->name);
	if (buf == 0 || len <= n)
		return ERANGE;
	memcpy (buf, T (th)->name, n + 1);
	return 0;
}

int pthread_getattr_np (pthread_t th, pthread_attr_t *a)
{
	struct __posix_thread *t = T (th);
	pthread_attr_init (a);
	if (t->stack_hi == 0)
		stack_bounds (t, t->is_main ? 1 : t->tid);
	a->__detachstate = t->detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
	a->__stackaddr = (void *) (unsigned long) t->stack_lo;
	a->__stacksize = (unsigned long) (t->stack_hi - t->stack_lo);
	a->__guardsize = (unsigned long) t->guard;
	a->__priority = t->prio;
	return 0;
}

int pthread_getcpuclockid (pthread_t t, clockid_t *c)
{
	(void) t;
	*c = CLOCK_THREAD_CPUTIME_ID;
	return 0;
}

/* ---- scheduling: one policy (the kernel's round robin); priority > 0 = "real time" ---- */
int pthread_setschedparam (pthread_t th, int policy, const struct sched_param *p)
{
	(void) policy;
	struct __posix_thread *t = T (th);
	t->prio = p != 0 && p->sched_priority > 0;
	/* une priorite > 0 : la priorite haute d'ARMPI (le temps reel est reserve a SD0:/sys) */
	__armpi_sys2 (SYS_THREAD_SET_PRIORITY, t->is_main && t->tid <= 1 ? 0 : t->tid,
		      t->prio ? THREAD_PRIO_HIGH : THREAD_PRIO_NORMAL);
	return 0;
}

int pthread_getschedparam (pthread_t th, int *policy, struct sched_param *p)
{
	*policy = T (th)->prio ? SCHED_FIFO : SCHED_OTHER;
	memset (p, 0, sizeof *p);
	p->sched_priority = T (th)->prio;
	return 0;
}

int pthread_setschedprio (pthread_t th, int prio)
{
	struct sched_param p;
	memset (&p, 0, sizeof p);
	p.sched_priority = prio;
	return pthread_setschedparam (th, SCHED_OTHER, &p);
}

static int s_concurrency;
int pthread_setconcurrency (int n) { if (n < 0) return EINVAL; s_concurrency = n; return 0; }
int pthread_getconcurrency (void) { return s_concurrency; }

int sched_get_priority_max (int policy) { return policy == SCHED_OTHER ? 0 : 99; }
int sched_get_priority_min (int policy) { (void) policy; return 0; }
int sched_getscheduler (pid_t pid) { (void) pid; return SCHED_OTHER; }
int sched_setscheduler (pid_t pid, int policy, const struct sched_param *p)
{
	(void) pid; (void) policy; (void) p;
	return SCHED_OTHER;
}
int sched_getparam (pid_t pid, struct sched_param *p) { (void) pid; memset (p, 0, sizeof *p); return 0; }
int sched_setparam (pid_t pid, const struct sched_param *p) { (void) pid; (void) p; return 0; }
int sched_rr_get_interval (pid_t pid, struct timespec *ts)
{
	(void) pid;
	ts->tv_sec = 0;
	ts->tv_nsec = 20000000;			/* the kernel's slice: 20 ms */
	return 0;
}
int sched_getcpu (void)
{
	/* le coeur du thread (thread_list : il reste sur son coeur) */
	long s_list[64 * 8];
	int me = (int) __armpi_sys0 (SYS_THREAD_SELF);
	long n = __armpi_sys3 (SYS_THREAD_LIST, 0, s_list, 64);
	for (long i = 0; i < n; i++)
		if (s_list[i * 8 + THL_TID / 8] == me)
			return (int) s_list[i * 8 + THL_CPU / 8];
	return 0;
}

/* ---- attributes ---- */
int pthread_attr_init (pthread_attr_t *a)
{
	memset (a, 0, sizeof *a);
	a->__flags = 1;
	a->__guardsize = POSIX_PAGE;
	return 0;
}
int pthread_attr_destroy (pthread_attr_t *a) { a->__flags = 0; return 0; }
int pthread_attr_setdetachstate (pthread_attr_t *a, int s)
{
	if (s != PTHREAD_CREATE_JOINABLE && s != PTHREAD_CREATE_DETACHED)
		return EINVAL;
	a->__detachstate = s;
	return 0;
}
int pthread_attr_getdetachstate (const pthread_attr_t *a, int *s) { *s = a->__detachstate; return 0; }
int pthread_attr_setstacksize (pthread_attr_t *a, size_t n)
{
	if (n < PTHREAD_STACK_MIN)
		return EINVAL;
	a->__stacksize = n > POSIX_STACK_MAX ? POSIX_STACK_MAX : n;
	return 0;
}
int pthread_attr_getstacksize (const pthread_attr_t *a, size_t *n)
{
	*n = a->__stacksize ? a->__stacksize : POSIX_STACK_DEFAULT;
	return 0;
}
int pthread_attr_setstack (pthread_attr_t *a, void *addr, size_t n) { (void) a; (void) addr; (void) n; return ENOTSUP; }
int pthread_attr_getstack (const pthread_attr_t *a, void **addr, size_t *n)
{
	*addr = a->__stackaddr;
	*n = a->__stacksize;
	return 0;
}
int pthread_attr_setstackaddr (pthread_attr_t *a, void *addr) { (void) a; (void) addr; return ENOTSUP; }
int pthread_attr_getstackaddr (const pthread_attr_t *a, void **addr) { *addr = a->__stackaddr; return 0; }
int pthread_attr_setguardsize (pthread_attr_t *a, size_t n) { a->__guardsize = n; return 0; }
int pthread_attr_getguardsize (const pthread_attr_t *a, size_t *n) { *n = a->__guardsize; return 0; }
int pthread_attr_setschedparam (pthread_attr_t *a, const struct sched_param *p) { a->__priority = p->sched_priority; return 0; }
int pthread_attr_getschedparam (const pthread_attr_t *a, struct sched_param *p)
{
	memset (p, 0, sizeof *p);
	p->sched_priority = a->__priority;
	return 0;
}
int pthread_attr_setschedpolicy (pthread_attr_t *a, int p) { a->__schedpolicy = p; return 0; }
int pthread_attr_getschedpolicy (const pthread_attr_t *a, int *p) { *p = a->__schedpolicy; return 0; }
int pthread_attr_setinheritsched (pthread_attr_t *a, int i)
{
	if (i != PTHREAD_INHERIT_SCHED && i != PTHREAD_EXPLICIT_SCHED)
		return EINVAL;
	a->__inheritsched = i;
	return 0;
}
int pthread_attr_getinheritsched (const pthread_attr_t *a, int *i) { *i = a->__inheritsched; return 0; }
int pthread_attr_setscope (pthread_attr_t *a, int s)
{
	if (s == PTHREAD_SCOPE_PROCESS)
		return ENOTSUP;
	if (s != PTHREAD_SCOPE_SYSTEM)
		return EINVAL;
	a->__scope = s;
	return 0;
}
int pthread_attr_getscope (const pthread_attr_t *a, int *s) { *s = a->__scope; return 0; }

/* ---- keys ---- */
static struct
{
	volatile int used;
	void (*dtor) (void *);
} s_keys[POSIX_KEYS_MAX];
static volatile unsigned s_keyLock;
void __posix_keys_fork (int phase) { __posix_fork_lock (&s_keyLock, phase); }

int pthread_key_create (pthread_key_t *k, void (*dtor) (void *))
{
	__posix_lock (&s_keyLock);
	for (unsigned i = 0; i < POSIX_KEYS_MAX; i++)
		if (!s_keys[i].used)
		{
			s_keys[i].used = 1;
			s_keys[i].dtor = dtor;
			/* a reused key starts empty in every thread */
			__posix_thread_list_lock ();
			for (struct __posix_thread *p = __posix_thread_list_first (); p != 0; p = p->next)
				p->specific[i] = 0;
			__posix_thread_list_unlock ();
			__posix_unlock (&s_keyLock);
			*k = i;
			return 0;
		}
	__posix_unlock (&s_keyLock);
	return EAGAIN;
}

int pthread_key_delete (pthread_key_t k)
{
	if (k >= POSIX_KEYS_MAX || !s_keys[k].used)
		return EINVAL;
	s_keys[k].used = 0;
	s_keys[k].dtor = 0;
	return 0;
}

void *pthread_getspecific (pthread_key_t k)
{
	return k < POSIX_KEYS_MAX ? __posix_self ()->specific[k] : 0;
}

int pthread_setspecific (pthread_key_t k, const void *v)
{
	if (k >= POSIX_KEYS_MAX || !s_keys[k].used)
		return EINVAL;
	__posix_self ()->specific[k] = (void *) v;
	return 0;
}

void __posix_keys_run_destructors (struct __posix_thread *t)
{
	for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++)
	{
		int any = 0;
		for (unsigned i = 0; i < POSIX_KEYS_MAX; i++)
		{
			void (*dtor) (void *) = s_keys[i].dtor;
			void *v = t->specific[i];
			if (v != 0 && s_keys[i].used && dtor != 0)
			{
				t->specific[i] = 0;
				dtor (v);
				any = 1;
			}
		}
		if (!any)
			break;
	}
}

/* ---- cleanup handlers ---- */
void __posix_cleanup_push (struct __posix_cleanup *c, void (*fn) (void *), void *arg)
{
	struct __posix_thread *t = __posix_self ();
	c->__routine = fn;
	c->__arg = arg;
	c->__prev = t->cleanup;
	t->cleanup = c;
}

void __posix_cleanup_pop (struct __posix_cleanup *c, int execute)
{
	struct __posix_thread *t = __posix_self ();
	t->cleanup = c->__prev;
	if (execute)
		c->__routine (c->__arg);
}

/* ---- cancellation (not supported) ---- */
int pthread_cancel (pthread_t t) { (void) t; return ENOSYS; }
int pthread_setcancelstate (int s, int *old)
{
	struct __posix_thread *t = __posix_self ();
	if (old)
		*old = t->cancel_state;
	t->cancel_state = s;
	return 0;
}
int pthread_setcanceltype (int type, int *old) { (void) type; if (old) *old = PTHREAD_CANCEL_DEFERRED; return 0; }
void pthread_testcancel (void) { }
/* ---- pthread_atfork : fork (proc.c) appelle les prepare des dernieres inscrites aux
 * premieres, puis parent ou child dans l'ordre d'inscription ---- */
#define ATFORK_MAX	64
static struct { void (*prepare) (void), (*parent) (void), (*child) (void); } s_atfork[ATFORK_MAX];
static volatile int s_atforkN;
static volatile unsigned s_atforkLock;
static int s_atforkRun;				/* inscrites au moment du fork */

int pthread_atfork (void (*prepare) (void), void (*parent) (void), void (*child) (void))
{
	__posix_lock (&s_atforkLock);
	int n = s_atforkN;
	if (n >= ATFORK_MAX)
	{
		__posix_unlock (&s_atforkLock);
		return ENOMEM;
	}
	s_atfork[n].prepare = prepare;
	s_atfork[n].parent = parent;
	s_atfork[n].child = child;
	STORE (&s_atforkN, n + 1);
	__posix_unlock (&s_atforkLock);
	return 0;
}

void __posix_atfork_run (int phase)
{
	if (phase == POSIX_FORK_PREPARE)
	{
		s_atforkRun = LOAD (&s_atforkN);
		for (int i = s_atforkRun - 1; i >= 0; i--)
			if (s_atfork[i].prepare)
				s_atfork[i].prepare ();
		return;
	}
	if (phase == POSIX_FORK_CHILD)
		s_atforkLock = 0;
	for (int i = 0; i < s_atforkRun; i++)
	{
		void (*f) (void) = phase == POSIX_FORK_PARENT ? s_atfork[i].parent : s_atfork[i].child;
		if (f)
			f ();
	}
}

/* Linked with -u __posix_pthread_anchor (posix.specs): the whole thread layer is in the program,
 * so libstdc++'s weak references (gthr-posix.h, under WP-TC's toolchain) find it. */
void *const __posix_pthread_anchor[] =
{
	(void *) pthread_create, (void *) pthread_join, (void *) pthread_detach, (void *) pthread_once,
	(void *) pthread_mutex_lock, (void *) pthread_mutex_trylock, (void *) pthread_mutex_unlock,
	(void *) pthread_mutex_timedlock, (void *) pthread_mutex_clocklock,
	(void *) pthread_cond_wait, (void *) pthread_cond_timedwait, (void *) pthread_cond_clockwait,
	(void *) pthread_cond_signal, (void *) pthread_cond_broadcast,
	(void *) pthread_rwlock_rdlock, (void *) pthread_rwlock_wrlock, (void *) pthread_rwlock_unlock,
	(void *) pthread_key_create, (void *) pthread_key_delete, (void *) pthread_getspecific,
	(void *) pthread_setspecific, (void *) pthread_cancel, (void *) sched_yield,
};
