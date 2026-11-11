/* Kits écrits en C, communs au Pi (AArch64) et au PC (x86-64) : appels
 * système, petites fonctions de mémoire et de chaînes (sans bibliothèque).
 *
 * Un kit est un ELF64 lié à 0 (src/kits/common/kit.ld) ; sa table
 * d'exportation est générée depuis le .abi (tools/kits/abi2table.py) ; le
 * code n'a pas d'adresse absolue (accès relatifs au compteur ordinal) et ses
 * variables sont recopiées dans chaque process par le noyau.
 * Numéros des appels : korin_sys_nr.h, tiré de src/inc/syscalls.inc. */
#ifndef KORIN_KIT_H
#define KORIN_KIT_H

#include <stddef.h>
#include <stdint.h>

#include "korin_sys_nr.h"

#define KIT_HIDDEN __attribute__((visibility("hidden")))

/* ---- appels système : jusqu'à 6 arguments, résultat signé (erreur < 0) */

#if defined(__aarch64__)
static inline long korin_sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
	return x0;
}
#elif defined(__x86_64__)
static inline long korin_sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	long ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return ret;
}
#else
#error "architecture non prise en charge"
#endif

#define korin_sys0(n) korin_sys6((n), 0, 0, 0, 0, 0, 0)
#define korin_sys1(n, a) korin_sys6((n), (long)(a), 0, 0, 0, 0, 0)
#define korin_sys2(n, a, b) korin_sys6((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define korin_sys3(n, a, b, c) korin_sys6((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define korin_sys4(n, a, b, c, d) korin_sys6((n), (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)

/* ---- message des services (512 octets : docs/ABI.md, 0x30) */

#define SVC_MSG 512
#define MSG_OP 0
#define MSG_A 8
#define MSG_B 16
#define MSG_C 24
#define MSG_D 32
#define MSG_LEN 40
#define MSG_PID 44
#define MSG_DATA 48

/* ---- mémoire et chaînes (src/kits/common/kitrt.c) */

KIT_HIDDEN void *memcpy(void *d, const void *s, size_t n);
KIT_HIDDEN void *memset(void *d, int c, size_t n);
KIT_HIDDEN void *memmove(void *d, const void *s, size_t n);
KIT_HIDDEN int memcmp(const void *a, const void *b, size_t n);
KIT_HIDDEN size_t strlen(const char *s);

#endif
