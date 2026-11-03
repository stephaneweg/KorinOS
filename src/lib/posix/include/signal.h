/*
 * signal.h -- newlib's, plus the sa_flags values code passes to sigaction (libposix
 * signal.c). ARMPI delivers signals asynchronously (src/kernel/signal.asm): newlib's struct
 * sigaction and siginfo_t give way to these, with sa_sigaction, si_addr and Linux's layout;
 * ucontext_t is in sys/ucontext.h. SIGPIPE is never raised (a write to a closed socket or pipe
 * fails with EPIPE).
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
#ifndef _POSIX_SIGNAL_H
#define _POSIX_SIGNAL_H

#define sigaction __newlib_sigaction
#define siginfo_t __newlib_siginfo_t
#include_next <signal.h>
#undef sigaction
#undef siginfo_t

/* le noyau livre les signaux 1 a 63 ; newlib s'arrete a 31, et ses macros (1 << sig) a 30 */
#undef NSIG
#define NSIG		64
#define SIGRTMIN	34
#define SIGRTMAX	63
#undef sigaddset
#undef sigdelset
#undef sigismember

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	int si_signo;
	int si_errno;
	int si_code;
	int __pad0;
	union
	{
		void *si_addr;			/* faute : l'adresse (SIGILL : pc) */
		struct { int si_pid, si_uid, si_status; };
#if __POSIX_VISIBLE >= 199309
		union sigval si_value;
#endif
		char __pad[112];
	};
} siginfo_t;

struct sigaction
{
	union
	{
		_sig_func_ptr sa_handler;
		void (*sa_sigaction) (int, siginfo_t *, void *);
	};
	sigset_t sa_mask;
	int sa_flags;
};

int sigaction (int, const struct sigaction *, struct sigaction *);

#ifdef __cplusplus
}
#endif

#undef SI_USER
#define SI_USER		0
#define SEGV_MAPERR	1
#define SEGV_ACCERR	2
#define BUS_ADRALN	1
#define BUS_ADRERR	2
#define BUS_OBJERR	3
#define ILL_ILLOPC	1
#define ILL_ILLOPN	2
#define ILL_ILLADR	3
#define ILL_ILLTRP	4
#define ILL_PRVOPC	5
#define ILL_PRVREG	6
#define ILL_COPROC	7
#define ILL_BADSTK	8
#define FPE_INTDIV	1
#define FPE_INTOVF	2
#define FPE_FLTDIV	3
#define FPE_FLTOVF	4
#define FPE_FLTUND	5
#define FPE_FLTRES	6
#define FPE_FLTINV	7
#define FPE_FLTSUB	8
#define TRAP_BRKPT	1
#define TRAP_TRACE	2

#ifndef SA_SIGINFO
#define SA_SIGINFO	0x2
#endif
#ifndef SA_ONSTACK
#define SA_ONSTACK	0x4
#endif
#ifndef SA_RESTART
#define SA_RESTART	0x10000000
#endif
#ifndef SA_NODEFER
#define SA_NODEFER	0x40000000
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND	0x80000000
#endif
#ifndef SIG_HOLD
#define SIG_HOLD	((_sig_func_ptr) 2)
#endif

#endif /* _POSIX_SIGNAL_H */
