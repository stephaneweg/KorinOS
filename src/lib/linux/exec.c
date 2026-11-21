/*
 * exec.c -- liblinux : démarrer un exécutable de Linux x86-64 dans ce process (docs/etude/LINUX-HOST.md,
 * jalon 1 ; linuxhost run).
 *
 * Ce que font execve et ld-linux pour lui, sans les deux : l'exécutable chargé par elf.c
 * (linux_load_file, LINUX_LOAD_EXE : un ET_EXEC à ses adresses, ou un PIE), son bloc TLS posé sous %fs
 * dans la réserve de l'hôte (exectls.S) pour ce thread et, par linux_thread_hook, pour chaque thread que
 * son code crée ; une pile à lui (PT_GNU_STACK, 8 Mo au moins) avec ce que le noyau de Linux y met :
 * argc, argv, envp, le vecteur auxiliaire (AT_PHDR, AT_ENTRY, AT_RANDOM…) et ses chaînes ; puis un saut
 * à _start, qui appelle __libc_start_main (elf.c) : constructeurs, main, exit. L'hôte ne reprend pas la
 * main : le process finit avec le programme.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "linux.h"

#define EXE_TLS_ROOM	65536
extern __thread unsigned char linux_exe_tls_pad[EXE_TLS_ROOM];

static struct linux_exe_info s_x;

static unsigned long tp_now (void)
{
	unsigned long tp;
	__asm__ ("mov %%fs:0, %0" : "=r" (tp));
	return tp;
}

/* le bloc de l'exécutable dans ce thread : [%fs - span, %fs), .tdata puis des zéros */
static void exe_tls_init (void)
{
	unsigned char *b = (unsigned char *) tp_now () - s_x.tls_span;
	memcpy (b, s_x.tls_init, s_x.tls_filesz);
	memset (b + s_x.tls_filesz, 0, s_x.tls_span - s_x.tls_filesz);
}

static void set_err (char *err, size_t n, const char *msg)
{
	if (err && n)
		snprintf (err, n, "%s", msg);
}

static char *push_str (char **p, const char *s)
{
	size_t n = strlen (s) + 1;
	*p -= n;
	memcpy (*p, s, n);
	return *p;
}

static __attribute__ ((noreturn)) void jump (uint64_t entry, unsigned long *sp)
{
	/* comme l'entrée d'un process de Linux : rsp sur argc (aligné sur 16), rdx = 0 (pas de rtld_fini) */
	__asm__ volatile ("mov %0, %%rsp\n\t"
			  "xor %%edx, %%edx\n\t"
			  "xor %%ebp, %%ebp\n\t"
			  "jmp *%1"
			  :: "D" (sp), "a" (entry) : "memory");
	__builtin_unreachable ();
}

int linux_exec (const char *path, int argc, char **argv, char **envp, int flags, char *err, size_t errlen)
{
	struct linux_module *m = linux_load_file (path, flags | LINUX_LOAD_EXE, err, errlen);
	if (!m)
		return -1;
	linux_exe_info (m, &s_x);
	if (!s_x.entry)
		return set_err (err, errlen, "pas de point d'entrée"), -1;
	if (s_x.tls_memsz)
	{
		unsigned long tp = tp_now (), pad = (unsigned long) (uintptr_t) &linux_exe_tls_pad[0];
		if (pad + EXE_TLS_ROOM != tp)
			return set_err (err, errlen, "la réserve de TLS des exécutables ne finit pas à %fs (hôte lié sans"
					" posix-lx.ld : tools/linux/build.sh)"), -1;
		if (s_x.tls_span > EXE_TLS_ROOM || s_x.tls_align > 64)
			return set_err (err, errlen, "le TLS de l'exécutable dépasse la réserve (64 Ko, alignement 64)"), -1;
		exe_tls_init ();
		linux_thread_hook = exe_tls_init;
	}

	/* la pile */
	size_t ss = s_x.stack_size > (8u << 20) ? (size_t) ((s_x.stack_size + 0xFFFF) & ~0xFFFFull) : (8u << 20);
	unsigned char *stk = mmap (0, ss, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stk == MAP_FAILED)
		return set_err (err, errlen, "mmap de la pile"), -1;
	int envc = 0;
	while (envp && envp[envc])
		envc++;
	char **ptrs = malloc (sizeof (char *) * (size_t) (argc + envc + 1));
	if (!ptrs)
		return set_err (err, errlen, "mémoire"), -1;
	char *p = (char *) stk + ss - 16;
	char *execfn = push_str (&p, path);
	char *platform = push_str (&p, "x86_64");
	for (int i = envc - 1; i >= 0; i--)
		ptrs[argc + i] = push_str (&p, envp[i]);
	for (int i = argc - 1; i >= 0; i--)
		ptrs[i] = push_str (&p, argv[i]);
	p = (char *) ((uintptr_t) p & ~(uintptr_t) 15) - 16;
	unsigned char *rnd = (unsigned char *) p;
	if (getentropy (rnd, 16) != 0)
		for (int i = 0; i < 16; i++)
			rnd[i] = (unsigned char) (rand () >> 3);
	unsigned int eax = 1, ebx, ecx, edx;
	__asm__ ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx));
	unsigned long aux[] = {
		3, s_x.phdr, 4, 56, 5, (unsigned long) s_x.phnum, 6, 4096, 7, 0, 8, 0, 9, s_x.entry,
		11, (unsigned long) getuid (), 12, (unsigned long) geteuid (), 13, (unsigned long) getgid (),
		14, (unsigned long) getegid (), 15, (unsigned long) (uintptr_t) platform, 16, edx, 17, 100,
		23, 0, 25, (unsigned long) (uintptr_t) rnd, 26, 0, 31, (unsigned long) (uintptr_t) execfn, 0, 0,
	};
	size_t naux = sizeof aux / sizeof aux[0];
	size_t words = 1 + (size_t) argc + 1 + (size_t) envc + 1 + naux;
	unsigned long *sp = (unsigned long *) ((uintptr_t) ((unsigned long *) p - words) & ~(uintptr_t) 15);
	unsigned long *w = sp;
	*w++ = (unsigned long) argc;
	for (int i = 0; i < argc; i++)
		*w++ = (unsigned long) (uintptr_t) ptrs[i];
	*w++ = 0;
	for (int i = 0; i < envc; i++)
		*w++ = (unsigned long) (uintptr_t) ptrs[argc + i];
	*w++ = 0;
	linux_auxv = w;
	memcpy (w, aux, sizeof aux);
	free (ptrs);
	linux_exe_start (m);
	fflush (0);
	jump (s_x.entry, sp);
}
