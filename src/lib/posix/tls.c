/*
 * tls.c -- blocs des threads, TPIDR_EL0 et errno (libposix, docs/POSIX.md ; repris d'Onyx).
 *
 * Disposition TLS d'AArch64, « variante 1 » : TPIDR_EL0 (le pointeur de thread, TP) montre un TCB
 * de 16 octets suivi du bloc TLS statique du thread, a TP + 16 arrondi a l'alignement du segment
 * TLS -- la ou les decalages « local-exec » de l'editeur de liens l'attendent. libposix garde son
 * propre bloc de controle (struct __posix_thread) juste SOUS le TCB : pthread_self () est
 * TP - sizeof (struct __posix_thread), sans appel systeme. Le bloc d'un thread est une seule
 * allocation :
 *
 *     [ ... | struct __posix_thread | TCB (16) | pad | image .tdata | zeros .tbss ]
 *                                  ^ TP
 *
 * Le bloc du thread principal est statique (ou reserve dans .bss par posix.ld quand le programme a
 * un segment PT_TLS) ; crt0posix pose TP avant tout autre code C. Un nouveau thread recoit son TP du
 * noyau (SYS_THREAD_CREATE_EX) ; le noyau d'ARMPI sauve et recharge TPIDR_EL0 a chaque commutation
 * et ne l'ecrit jamais lui-meme.
 *
 * errno : la newlib de la chaine aarch64-onyx-elf est construite --enable-newlib-reent-thread-local,
 * son errno est une variable __thread (_tls_errno) dans ce bloc : par thread, rien a remplacer.
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
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <reent.h>
#include "posix_internal.h"

/* The TLS segment, from posix.ld (weak: 0 when the program has none, or is linked with
 * another script). */
extern char __posix_tdata_start[] __attribute__ ((weak));
extern char __posix_tdata_size[] __attribute__ ((weak));	/* absolute symbols: their address */
extern char __posix_tls_size[] __attribute__ ((weak));	/* is the value */
extern char __posix_tls_align[] __attribute__ ((weak));
extern char __posix_main_tls_area[] __attribute__ ((weak));
extern char __posix_main_tls_reserve[] __attribute__ ((weak));

#define TCB_SIZE	16

static inline unsigned long tls_size (void) { return (unsigned long) __posix_tls_size; }
static inline unsigned long tdata_size (void) { return (unsigned long) __posix_tdata_size; }
static inline unsigned long tls_align (void)
{
	unsigned long a = (unsigned long) __posix_tls_align;
	return a < 16 ? 16 : a;
}
static inline unsigned long align_up (unsigned long v, unsigned long a) { return (v + a - 1) & ~(a - 1); }

/* The main thread's block when the program has no TLS segment. */
static struct
{
	struct __posix_thread t;
	unsigned char tcb[TCB_SIZE];
} __attribute__ ((aligned (64))) s_main =
{
	.t = { .self = &s_main.t, .magic = POSIX_THREAD_MAGIC, .tid = 1, .is_main = 1,
	       .name = "main" },
};

struct __posix_thread *__posix_main_thread = &s_main.t;

static volatile unsigned s_listLock;
static struct __posix_thread *s_list = &s_main.t;

unsigned long __posix_thread_tp (struct __posix_thread *t)
{
	return (unsigned long) t + sizeof (struct __posix_thread);
}

static void copy_tls_image (unsigned long tp)
{
	unsigned long n = tls_size ();
	if (n == 0)
		return;
	unsigned char *block = (unsigned char *) tp + align_up (TCB_SIZE, tls_align ());
	unsigned long d = tdata_size ();
	if (d)
		memcpy (block, __posix_tdata_start, d);
	memset (block + d, 0, n - d);
}

/* crt0posix, first thing: the main thread's TP. No errno, no malloc, no TLS before it. */
void __posix_tls_init_main (void)
{
	struct __posix_thread *t = &s_main.t;
	unsigned long n = tls_size ();
	if (n != 0 && __posix_main_tls_area != 0)
	{
		/* the area posix.ld reserved: [struct | TCB | pad | TLS block] */
		unsigned long a = tls_align ();
		unsigned long need = align_up (sizeof (struct __posix_thread), a) + align_up (TCB_SIZE, a) + n + a;
		if (need <= (unsigned long) __posix_main_tls_reserve)
		{
			unsigned long base = align_up ((unsigned long) __posix_main_tls_area, a);
			unsigned long tp = base + align_up (sizeof (struct __posix_thread), a);
			t = (struct __posix_thread *) (tp - sizeof (struct __posix_thread));
			memcpy (t, &s_main.t, sizeof *t);
			t->self = t;
			s_list = t;
			__posix_main_thread = t;
		}
	}
	unsigned long tp = __posix_thread_tp (t);
	((void **) tp)[0] = t;				/* the TCB: (dtv) -> the block, for a debugger */
	((void **) tp)[1] = 0;
	copy_tls_image (tp);
#if defined(__aarch64__)
	__asm__ volatile ("msr tpidr_el0, %0" :: "r" (tp) : "memory");
#endif
}

/* A new thread's block (zeroed, its TLS image copied), or 0. */
struct __posix_thread *__posix_thread_alloc (void)
{
	unsigned long a = tls_align ();
	unsigned long p = align_up (sizeof (struct __posix_thread), a);
	unsigned long size = p + align_up (TCB_SIZE, a) + tls_size ();
	unsigned char *block = (unsigned char *) memalign (a, size);
	if (block == 0)
		return 0;
	memset (block, 0, size);
	struct __posix_thread *t = (struct __posix_thread *) (block + p - sizeof (struct __posix_thread));
	t->self = t;
	t->magic = POSIX_THREAD_MAGIC;
	t->block = block;
	unsigned long tp = __posix_thread_tp (t);
	((void **) tp)[0] = t;
	copy_tls_image (tp);
	return t;
}

void __posix_thread_free (struct __posix_thread *t)
{
	if (t == 0 || t->block == 0)
		return;
	t->magic = 0;
	free (t->block);
}

void __posix_thread_list_lock (void) { __posix_lock (&s_listLock); }
void __posix_thread_list_unlock (void) { __posix_unlock (&s_listLock); }
struct __posix_thread *__posix_thread_list_first (void) { return s_list; }

void __posix_thread_list_add (struct __posix_thread *t)
{
	__posix_lock (&s_listLock);
	t->next = s_list;
	s_list = t;
	__posix_unlock (&s_listLock);
}

void __posix_thread_list_remove (struct __posix_thread *t)
{
	__posix_lock (&s_listLock);
	for (struct __posix_thread **pp = &s_list; *pp != 0; pp = &(*pp)->next)
		if (*pp == t)
		{
			*pp = t->next;
			break;
		}
	__posix_unlock (&s_listLock);
}

