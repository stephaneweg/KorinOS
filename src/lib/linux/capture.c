/*
 * capture.c -- liblinux : les syscall directs des modules de Linux, par la capture de syscall du noyau de
 * Korin (SYS_SYSCALL_CAPTURE, docs/etude/WIDEVINE.md § 3.2).
 *
 * linux_capture_set déclare au noyau les plages d'adresses des modules chargés (elf.c l'appelle à chaque
 * chargement et déchargement). Une instruction syscall de ces plages n'est plus servie par Korin : le
 * thread arrive dans capture_entry avec la trame d'un signal (ucontext au format de Linux x86-64), qui
 * sert l'appel par linux_syscall (numéros et ABI de Linux), écrit le résultat dans rax de la trame, et
 * rend la main à capture_restorer, qui fait SIG_RETURN : le module reprend après son syscall. Tout se
 * passe dans le thread du module ; les appels faits ici, hors des plages, sont des appels de Korin.
 *
 * Construit pour Linux (essai natif dans WSL, __linux__) : pas de capture, le noyau de Linux sert les
 * syscall directs lui-même.
 */
#include <stdint.h>

#include "linux.h"

#if defined(__linux__)

unsigned long linux_capture_count;

int linux_capture_set (const uint64_t *ranges, int n)
{
	(void) ranges;
	(void) n;
	return 0;
}

#else

#include <armpi/syscall.h>

#include "korin_sys_nr.h"	/* (tiré de src/inc/syscalls.inc par tools/linux/build.sh) */

#define STR2(x) #x
#define STR(x) STR2 (x)

/* les registres de l'ucontext (ordre de Linux, src/kernel/arch/x86_64/sigframe.c) */
#define UC_GREGS 40
enum { G_R8, G_R9, G_R10, G_R11, G_R12, G_R13, G_R14, G_R15, G_RDI, G_RSI, G_RBP, G_RBX, G_RDX, G_RAX,
       G_RCX, G_RSP, G_RIP };

void linux_capture_restorer (void);
__asm__ (".text\n\t.p2align 4\n"
	 "linux_capture_restorer:\n"
	 "\tmovl $" STR (SYS_SIG_RETURN) ", %eax\n\tsyscall\n\tud2\n");

unsigned long linux_capture_count;

static void capture_entry (int sig, void *si, void *uc)
{
	(void) sig;
	(void) si;
	int64_t *g = (int64_t *) ((char *) uc + UC_GREGS);
	linux_capture_count++;
	g[G_RAX] = linux_syscall (g[G_RAX], g[G_RDI], g[G_RSI], g[G_RDX], g[G_R10], g[G_R8], g[G_R9]);
	/* (comme le noyau de Linux : rcx et r11 sont écrasés par syscall ; on les laisse tels quels) */
}

int linux_capture_set (const uint64_t *ranges, int n)
{
	if (n > CAPTURE_MAX)
		n = CAPTURE_MAX;
	long r = armpi_syscall (SYS_SYSCALL_CAPTURE, (long) ranges, n, (long) capture_entry,
				(long) linux_capture_restorer, 0, 0);
	return r < 0 ? (int) r : 0;
}

#endif
