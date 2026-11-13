/*
 * armpi/syscall.h -- un appel systeme de Korin depuis le C ou le C++ (numeros : SYS_* de
 * src/inc/syscalls.inc, docs/ABI.md), le meme source pour le Pi et le PC :
 *
 *   long ms = armpi_syscall (0x19, 0, 0, 0, 0, 0, 0);	// SYS_TIME_MS
 *
 * Pi (AArch64) : svc 0, numero dans x8, arguments x0..x5 ; PC (x86-64) : syscall, numero dans rax,
 * arguments rdi rsi rdx r10 r8 r9. Resultat negatif : une erreur (numeros de Linux). Les appels de
 * fichiers (0x80..0x8E) sont ceux d'AppKit, pas du noyau : passer par la libc.
 * armpi_relax () : une attente active courte (yield / pause).
 */
#ifndef _ARMPI_SYSCALL_H
#define _ARMPI_SYSCALL_H

#if defined(__aarch64__)
static inline long armpi_syscall (long n, long a, long b, long c, long d, long e, long f)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x3 __asm__ ("x3") = d;
	register long x4 __asm__ ("x4") = e;
	register long x5 __asm__ ("x5") = f;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5), "r" (x8)
			  : "memory");
	return x0;
}
static inline void armpi_relax (void) { __asm__ volatile ("yield"); }
#elif defined(__x86_64__)
static inline long armpi_syscall (long n, long a, long b, long c, long d, long e, long f)
{
	register long r10 __asm__ ("r10") = d;
	register long r8 __asm__ ("r8") = e;
	register long r9 __asm__ ("r9") = f;
	long r;
	__asm__ volatile ("syscall" : "=a" (r) : "a" (n), "D" (a), "S" (b), "d" (c), "r" (r10), "r" (r8), "r" (r9)
			  : "rcx", "r11", "memory");
	return r;
}
static inline void armpi_relax (void) { __asm__ volatile ("pause"); }
#else
#error "armpi/syscall.h : AArch64 (Pi) ou x86-64 (PC)"
#endif

#endif
