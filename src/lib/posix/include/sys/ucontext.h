/*
 * sys/ucontext.h -- le contexte donne au gestionnaire d'un signal (troisieme argument), trame du
 * noyau d'ARMPI : memes champs et memes decalages que Linux arm64 (src/inc/syscalls.inc, UCX_*).
 * uc_mcontext : registres lus a l'arrivee du signal ; le programme peut les changer, SIG_RETURN
 * les reprend tels quels (x0..x30, sp, pc, NZCV, masque, FP/SIMD).
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
#ifndef _SYS_UCONTEXT_H
#define _SYS_UCONTEXT_H

#include <signal.h>

#define FPSIMD_MAGIC	0x46508001

struct _aarch64_ctx
{
	unsigned magic, size;			/* FPSIMD_MAGIC, 528 ; 0 : le thread n'a pas de FP */
};

struct fpsimd_context
{
	struct _aarch64_ctx head;
	unsigned fpsr, fpcr;
	__uint128_t vregs[32];
};

struct sigcontext
{
	unsigned long fault_address;
	unsigned long regs[31];
	unsigned long sp, pc, pstate;
	unsigned char __reserved[1024] __attribute__ ((aligned (16)));	/* struct fpsimd_context */
};

typedef struct sigcontext mcontext_t;

typedef struct ucontext
{
	unsigned long uc_flags;
	struct ucontext *uc_link;
	stack_t uc_stack;
	sigset_t uc_sigmask;
	unsigned char __uc_pad[120];
	mcontext_t uc_mcontext;
} ucontext_t;

#endif /* _SYS_UCONTEXT_H */
