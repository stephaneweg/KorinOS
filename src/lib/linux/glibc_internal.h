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
extern const struct lx_entry lx_table_dl[];
extern const size_t lx_table_dl_n;
extern const struct lx_entry lx_table_sys[];
extern const size_t lx_table_sys_n;
extern const struct lx_entry lx_table_libc[];
extern const size_t lx_table_libc_n;

/* glibc_libc.c : program_invocation_name (exec.c) */
void lx_libc_set_progname (const char *argv0);
/* glibc_libc.c : la mémoire rendue à l'appelant (le malloc de l'exécutable s'il en a un) */
extern void *(*lx_umalloc) (size_t);
extern void *(*lx_urealloc) (void *, size_t);
extern void (*lx_ufree) (void *);
char *lx_ustrdup (const char *s);

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

/* syscall.c : ce que syscall2.c reprend */
struct stat;
long lx_synth_open (const char *path);
int lx_open_flags (long linux_flags);
void lx_stat_out (void *linux_stat, const struct stat *st);
/* syscall2.c : les appels de Linux d'un exécutable (fichiers, process, signaux, sockets, epoll…) ; rend 1 et
   le résultat dans *ret si servi ici, 0 sinon (syscall.c continue) */
int lx_syscall2 (long nr, long a, long b, long c, long d, long e, long f, long *ret);
/* syscall2.c : signal de Linux <-> de Korin (0 : aucun) */
int lx_sig_korin (int linux_sig);
int lx_sig_linux (int korin_sig);
/* exec.c : le chemin de l'exécutable lancé (/proc/self/exe) et de l'hôte (execve d'un programme de Linux) */
extern const char *linux_exe_path;
extern const char *linux_host_path;

#endif
