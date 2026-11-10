/*
 * signal.c -- les signaux tels qu'ARMPI peut les avoir (libposix, docs/POSIX.md ; repris d'Onyx).
 *
 * Le noyau livre les signaux (src/kernel/signal.asm, docs/ABI.md) : actions par process, masque,
 * attente et pile de signaux par thread, fautes rattrapables avec si_addr, ucontext modifiable.
 * Une copie des actions est gardee ici. Actions par defaut : SIGCHLD, SIGWINCH, SIGURG et SIGCONT
 * ignores ; tout autre signal finit le process (raise, abort : statut 128 + sig, abort 134 ; venu
 * d'un autre process : WIFSIGNALED). SIGPIPE n'est jamais leve : une ecriture dans un tube ou un
 * socket ferme echoue (EPIPE). Signal 0 -> existe-t-il (SYS_PROCLIST). SIGCHLD vient du
 * noyau, a la fin d'un fils, si un gestionnaire est installe.
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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "posix_internal.h"
#include <sys/membarrier.h>

#define STR_(x)	#x
#define STR(x)	STR_ (x)

/* la fin d'un gestionnaire (x30 quand le noyau le lance) */
__asm__ (".text\n.type __posix_sigreturn, %function\n__posix_sigreturn:\n"
	 "\tmov x8, #" STR (SYS_SIG_RETURN) "\n\tsvc 0\n");
extern void __posix_sigreturn (void);

/* <signal.h> defines these as macros: the functions too, for code that takes their address */
#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember

static struct sigaction s_act[NSIG];

static int ignored_by_default (int sig)
{
	/* (SIGTSTP, SIGTTIN, SIGTTOU : pas d'etat arrete, ignores comme dans le noyau) */
	return sig == SIGCHLD || sig == SIGWINCH || sig == SIGURG || sig == SIGCONT ||
	       sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU;
}

/* Un gestionnaire passe par __posix_sig_tramp : le compteur dit qu'un signal a ete livre (une
 * lecture de terminal qui redemande a vterm rend alors EINTR, tty.c) */
volatile unsigned __posix_sig_count;

static void __posix_sig_tramp (int sig, siginfo_t *info, void *uc)
{
	__atomic_add_fetch (&__posix_sig_count, 1, __ATOMIC_RELAXED);
	struct sigaction *a = &s_act[sig];
	if (a->sa_flags & SA_SIGINFO)
		a->sa_sigaction (sig, info, uc);
	else if (a->sa_handler != SIG_DFL && a->sa_handler != SIG_IGN)
		a->sa_handler (sig);
}

unsigned long long __posix_sig_ignored (void)
{
	unsigned long long m = 0;
	for (int s = 1; s < NSIG && s < 64; s++)
		if (s_act[s].sa_handler == SIG_IGN)
			m |= 1ULL << s;
	return m;
}

void __posix_sig_ignored_set (unsigned long long m)
{
	for (int s = 1; s < NSIG && s < 64; s++)
		if ((m >> s) & 1)
			s_act[s].sa_handler = SIG_IGN;
}

int __posix_signal_default (int sig)
{
	if (ignored_by_default (sig))
		return 0;
	_exit (128 + sig);
}

int sigaction (int sig, const struct sigaction *act, struct sigaction *old)
{
	if (sig <= 0 || sig >= NSIG || ((sig == SIGKILL || sig == SIGSTOP) && act))
		return POSIX_ERR (EINVAL);
	if (old)
		*old = s_act[sig];
	if (act)
	{
		void *h = (void *) act->sa_handler;
		if (act->sa_handler != SIG_DFL && act->sa_handler != SIG_IGN)
			h = (void *) __posix_sig_tramp;
		struct sigaction prev = s_act[sig];
		s_act[sig] = *act;			/* (avant : le gestionnaire peut venir tout de suite) */
		long r = __armpi_sys6 (SYS_SIG_ACTION, sig, h, act->sa_flags, act->sa_mask,
				       __posix_sigreturn, 0);
		if (r < 0)
		{
			s_act[sig] = prev;
			return (int) POSIX_SYS (r);
		}
	}
	return 0;
}

_sig_func_ptr signal (int sig, _sig_func_ptr fn)
{
	struct sigaction a, old;
	memset (&a, 0, sizeof a);
	a.sa_handler = fn;
	if (sigaction (sig, &a, &old) != 0)
		return SIG_ERR;
	return old.sa_handler;
}

_sig_func_ptr _signal_r (struct _reent *r, int sig, _sig_func_ptr fn) { (void) r; return signal (sig, fn); }

int raise (int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return POSIX_ERR (EINVAL);
	_sig_func_ptr h = s_act[sig].sa_handler;
	if (h == SIG_IGN)
		return 0;
	sigset_t cur = 0;
	sigprocmask (SIG_BLOCK, 0, &cur);
	if (h == SIG_DFL && !((cur >> sig) & 1))
	{
		__posix_signal_default (sig);
		return 0;
	}
	if (s_act[sig].sa_flags & SA_RESETHAND)
		s_act[sig].sa_handler = SIG_DFL;
	/* livre par le noyau au retour de cet appel, a ce thread */
	return (int) POSIX_SYS (__armpi_sys3 (SYS_SIG_SEND, 0, __armpi_sys0 (SYS_THREAD_SELF), sig));
}

int _raise_r (struct _reent *r, int sig) { (void) r; return raise (sig); }

void abort (void)
{
	raise (SIGABRT);
	_exit (128 + SIGABRT);			/* a handler returned: the end anyway (status 134) */
}

int _kill (int pid, int sig)
{
	if (pid == 0)
		pid = -getpgrp ();			/* son groupe */
	if (pid == getpid () || pid == -1)
		return sig == 0 ? 0 : raise (sig);
	if (pid < 0)					/* un groupe (noyau : SIG_SEND a -groupe) */
		return (int) POSIX_SYS (__armpi_sys3 (SYS_SIG_SEND, pid, 0, sig));
	/* le process existe-t-il ? (SYS_PROCLIST) */
	static long list[64 * (ARMPI_PROCLIST_ENT / 8)];
	static volatile unsigned lock;
	__posix_lock (&lock);
	long n = __armpi_sys2 (SYS_PROCLIST, list, 64);
	int found = 0;
	for (long i = 0; i < n; i++)
		if (list[i * (ARMPI_PROCLIST_ENT / 8)] == pid)
			found = 1;
	__posix_unlock (&lock);
	if (!found)
		return POSIX_ERR (ESRCH);
	if (sig == 0)
		return 0;
	return (int) POSIX_SYS (__armpi_sys3 (SYS_SIG_SEND, pid, 0, sig));
}

int kill (pid_t pid, int sig) { return _kill ((int) pid, sig); }
int killpg (pid_t g, int sig) { return _kill (-(int) g, sig); }

int pthread_kill (pthread_t t, int sig)
{
	if (sig < 0 || sig >= NSIG)
		return EINVAL;
	if (t == pthread_self ())
		return sig == 0 ? 0 : (raise (sig) == 0 ? 0 : errno);
	struct __posix_thread *th = (struct __posix_thread *) t;
	if (sig == 0)
		return th->magic == POSIX_THREAD_MAGIC && th->state == POSIX_T_RUNNING ? 0 : ESRCH;
	long r = __armpi_sys3 (SYS_SIG_SEND, 0, th->tid, sig);
	return r < 0 ? __armpi_errno (r) : 0;
}

int sigprocmask (int how, const sigset_t *set, sigset_t *old)
{
	long r;
	if (how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK)
		return POSIX_ERR (EINVAL);
	if (set)
		r = __armpi_sys2 (SYS_SIG_MASK, how, *set);	/* (memes valeurs que SIGM_*) */
	else
		r = __armpi_sys2 (SYS_SIG_MASK, SIG_BLOCK, 0);
	if (old)
		*old = (sigset_t) r;
	return 0;
}

int pthread_sigmask (int how, const sigset_t *set, sigset_t *old)
{
	return sigprocmask (how, set, old) == 0 ? 0 : errno;
}

int sigemptyset (sigset_t *s) { *s = 0; return 0; }
int sigfillset (sigset_t *s) { *s = ~(sigset_t) 0; return 0; }
int sigaddset (sigset_t *s, int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return POSIX_ERR (EINVAL);
	*s |= (sigset_t) 1 << sig;
	return 0;
}
int sigdelset (sigset_t *s, int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return POSIX_ERR (EINVAL);
	*s &= ~((sigset_t) 1 << sig);
	return 0;
}
int sigismember (const sigset_t *s, int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return POSIX_ERR (EINVAL);
	return (*s >> sig) & 1;
}

#define SIGM_PENDING	3			/* (src/inc/syscalls.inc) */
#define SIGM_SUSPEND	4
#define SIGM_WAIT	5

int sigpending (sigset_t *s)
{
	*s = (sigset_t) __armpi_sys2 (SYS_SIG_MASK, SIGM_PENDING, 0);
	return 0;
}
int sigsuspend (const sigset_t *s) { return (int) POSIX_SYS (__armpi_sys2 (SYS_SIG_MASK, SIGM_SUSPEND, *s)); }
int sigwait (const sigset_t *s, int *sig)
{
	long r = __armpi_sys2 (SYS_SIG_MASK, SIGM_WAIT, *s);
	if (r < 0)
		return __armpi_errno (r);
	*sig = (int) r;
	return 0;
}
int sigaltstack (const stack_t *ss, stack_t *old)
{
	long out[2] = { 0, 0 };
	long sp = -1, size = 0;
	if (ss)
	{
		if (!(ss->ss_flags & SS_DISABLE))
		{
			if (ss->ss_size < MINSIGSTKSZ)
				return POSIX_ERR (ENOMEM);
			sp = (long) ss->ss_sp;
			size = (long) ss->ss_size;
		}
		else
			sp = 0;
	}
	long r = __armpi_sys3 (SYS_SIG_ALTSTACK, sp, size, out);
	if (r < 0)
		return (int) POSIX_SYS (r);
	if (old)
	{
		unsigned long here = (unsigned long) __builtin_frame_address (0) - (unsigned long) out[0];
		old->ss_sp = (void *) out[0];
		old->ss_size = (size_t) out[1];
		old->ss_flags = !out[1] ? SS_DISABLE : here < (unsigned long) out[1] ? SS_ONSTACK : 0;
	}
	return 0;
}
int siginterrupt (int sig, int flag) { (void) sig; (void) flag; return 0; }
int pause (void)
{
	sigset_t cur = 0;
	sigprocmask (SIG_BLOCK, 0, &cur);
	return sigsuspend (&cur);
}

int membarrier (int cmd, unsigned flags, int cpu_id)
{
	(void) cpu_id;
	return (int) POSIX_SYS (__armpi_sys2 (SYS_MEMBARRIER, cmd, flags));
}
