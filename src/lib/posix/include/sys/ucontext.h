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

#if defined(__x86_64__)
/* Le PC : la trame de src/kernel/arch/x86_64/sigframe.c, disposition de Linux x86-64 (gregs a +40,
 * pointeur de l'etat FPU a +224, masque a +296, FXSAVE de 512 octets a +320 ; 832 octets) */
#define NGREG		23
#define REG_R8		0
#define REG_R9		1
#define REG_R10		2
#define REG_R11		3
#define REG_R12		4
#define REG_R13		5
#define REG_R14		6
#define REG_R15		7
#define REG_RDI		8
#define REG_RSI		9
#define REG_RBP		10
#define REG_RBX		11
#define REG_RDX		12
#define REG_RAX		13
#define REG_RCX		14
#define REG_RSP		15
#define REG_RIP		16
#define REG_EFL		17
#define REG_CSGSFS	18
#define REG_ERR		19
#define REG_TRAPNO	20
#define REG_OLDMASK	21
#define REG_CR2		22

typedef long long greg_t;
typedef greg_t gregset_t[NGREG];

struct _libc_fpxreg { unsigned short significand[4], exponent, __glibc_reserved1[3]; };
struct _libc_xmmreg { unsigned element[4]; };
struct _libc_fpstate
{
	unsigned short cwd, swd, ftw, fop;
	unsigned long long rip, rdp;
	unsigned mxcsr, mxcr_mask;
	struct _libc_fpxreg _st[8];
	struct _libc_xmmreg _xmm[16];
	unsigned __glibc_reserved1[24];
};
typedef struct _libc_fpstate *fpregset_t;

typedef struct
{
	gregset_t gregs;
	fpregset_t fpregs;
	unsigned long long __reserved1[8];
} mcontext_t;

typedef struct ucontext
{
	unsigned long uc_flags;
	struct ucontext *uc_link;
	stack_t uc_stack;
	mcontext_t uc_mcontext;
	sigset_t uc_sigmask;
	unsigned char __uc_pad[24 - sizeof (sigset_t)];
	struct _libc_fpstate __fpregs_mem;
} ucontext_t;

#else

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

#endif /* __x86_64__ */

#endif /* _SYS_UCONTEXT_H */
