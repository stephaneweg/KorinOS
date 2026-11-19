/*
 * glibc_internal.h -- liblinux : ce que partagent les morceaux de la glibc de compatibilité (glibc.c,
 * glibc_thread.c, glibc_misc.c).
 */
#ifndef KORIN_LINUX_GLIBC_INTERNAL_H
#define KORIN_LINUX_GLIBC_INTERNAL_H

#include <stddef.h>

struct lx_entry
{
	const char *name;
	void *addr;
};

#define LX_F(n)		{ #n, (void *) n }
#define LX_W(n, f)	{ n, (void *) f }

/* les tables de chaque morceau */
extern const struct lx_entry lx_table_thread[];
extern const size_t lx_table_thread_n;
extern const struct lx_entry lx_table_misc[];
extern const size_t lx_table_misc_n;

/* errno de Linux du thread (glibc.c) */
int *lx_errno_ptr (void);
/* errno de Korin (libposix) -> errno de Linux du thread ; rend -1 */
long lx_set_errno_from_korin (void);
/* le résultat brut d'un appel (-errno de Linux) vers la convention de la libc (-1 et errno) */
long lx_ret (long r);

/* glibc_misc.c : préparations (__libc_stack_end) */
void lx_misc_init (void);

/* une erreur de pthread (rendue, pas dans errno) : Korin -> Linux */
int lx_perr (int korin_err);

#endif
