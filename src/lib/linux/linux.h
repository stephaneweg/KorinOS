/*
 * linux.h -- liblinux : exécuter dans un process de Korin du code compilé pour Linux x86-64
 * (docs/etude/WIDEVINE.md, § 4 ; premier client : l'hôte du module Widevine).
 *
 * Trois morceaux :
 *   elf.c      le chargeur : un .so ELF64 x86-64 de Linux (ET_DYN) mis en mémoire, relogé, ses
 *              constructeurs appelés ; ses imports liés à la glibc de compatibilité ;
 *   glibc.c    la glibc de compatibilité : les fonctions et données de glibc que le module importe,
 *              à l'ABI de Linux (numéros d'errno, constantes, tailles des structures), servies par
 *              libposix ;
 *   syscall.c  les appels système de Linux (numéros de x86-64) traduits en appels de libposix :
 *              ceux que le module fait par syscall () de glibc. (Ceux qu'il ferait par l'instruction
 *              syscall elle-même demandent la personnalité Linux du noyau : étape suivante.)
 *
 * Code de Korin, en C, pour le PC (x86-64) ; le module Widevine n'existe pour Linux qu'en x86-64.
 */
#ifndef KORIN_LINUX_H
#define KORIN_LINUX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct linux_module;

/* options de linux_load */
#define LINUX_LOAD_STUBS	1	/* un import absent devient une fonction qui le nomme et arrête le
					   process quand elle est appelée (sinon : le chargement échoue) */
#define LINUX_LOAD_NOINIT	2	/* ne pas appeler les constructeurs (DT_INIT, DT_INIT_ARRAY) */
#define LINUX_LOAD_EXE		4	/* un exécutable (ET_EXEC ou PIE) : constructeurs par __libc_start_main,
					   TLS statique (exec.c) */

/* charge l'image (le fichier .so en mémoire, que l'appelant garde ou libère ensuite) ;
   NULL si refusée, avec la raison dans err */
struct linux_module *linux_load (const void *image, size_t size, const char *name, int flags,
				 char *err, size_t errlen);
/* la même chose depuis un fichier, lu par morceaux (en-têtes, puis chaque segment à sa place) */
struct linux_module *linux_load_file (const char *path, int flags, char *err, size_t errlen);
/* l'adresse d'un symbole défini et exporté par le module, NULL s'il n'y est pas */
void *linux_sym (struct linux_module *m, const char *name);
/* appelle les destructeurs (__cxa_atexit du module, DT_FINI_ARRAY, DT_FINI) et libère la mémoire */
void linux_unload (struct linux_module *m);
/* l'adresse de base où le module est chargé */
void *linux_base (struct linux_module *m);

/* rapport sur une image sans l'exécuter : bibliothèques demandées, imports servis ou absents,
   relocations, TLS, instructions syscall directes ; rend le nombre d'imports absents (< 0 :
   image refusée) */
int linux_check (const void *image, size_t size, FILE *out, int verbose);

/* glibc.c : l'adresse de la fonction ou de la donnée de glibc de ce nom, NULL si non servie */
void *linux_glibc_lookup (const char *name);
/* glibc.c : appelée par un import absent (LINUX_LOAD_STUBS) ; ne revient pas */
void linux_missing (const char *name) __attribute__ ((noreturn));

/* syscall.c : un appel système de Linux x86-64 ; rend la valeur du noyau Linux (< 0 : -errno de
   Linux), comme l'instruction syscall */
long linux_syscall (long nr, long a, long b, long c, long d, long e, long f);
/* syscall.c : errno de Korin (newlib) vers celui de Linux, et l'inverse */
int linux_errno (int korin_errno);
int linux_errno_korin (int linux_errno);

/* elf.c : __tls_get_addr des modules ({numéro de module, décalage}) */
void *linux_tls_get_addr (const uint64_t *ti);
/* elf.c : /proc/self/maps au format de Linux (programme hôte, modules, pile) ; rend la longueur */
size_t linux_maps (char *buf, size_t cap);

/* ---- exécutables (docs/etude/LINUX-HOST.md, jalon 1) ---- */
struct linux_exe_info
{
	uint64_t entry, phdr;		/* _start ; les en-têtes de programme en mémoire (AT_PHDR) */
	int phnum;
	uint64_t stack_size;		/* PT_GNU_STACK (0 : rien demandé) */
	const void *tls_init;		/* PT_TLS : image de .tdata, tailles, alignement ; tls_span : le bloc */
	uint64_t tls_filesz, tls_memsz, tls_align, tls_span;	/* sous %fs (variante II) */
};
/* elf.c : ce qu'un exécutable chargé (LINUX_LOAD_EXE) demande pour démarrer */
void linux_exe_info (struct linux_module *m, struct linux_exe_info *x);
/* elf.c : l'exécutable que va démarrer _start (son __libc_start_main : constructeurs, environ, fin) */
void linux_exe_start (struct linux_module *m);
/* elf.c : __libc_start_main de glibc */
int linux_libc_start_main (int (*main_fn) (int, char **, char **), int argc, char **argv,
			   void (*init) (int, char **, char **), void (*fini) (void), void (*rtld_fini) (void),
			   void *stack_end);
/* exec.c : charge l'exécutable path et le démarre dans ce process, sur sa propre pile (argv[0] : son
   nom) ; ne revient qu'en cas d'échec (raison dans err). L'hôte doit être lié avec la réserve de TLS des
   exécutables (tools/linux/build.sh : posix-lx.ld, à 0x4000_0000) */
int linux_exec (const char *path, int argc, char **argv, char **envp, int flags, char *err, size_t errlen);
/* glibc_misc.c : le vecteur auxiliaire de l'exécutable lancé (getauxval), NULL sinon */
extern const unsigned long *linux_auxv;
/* glibc_thread.c : appelée au début de chaque thread créé par le code Linux (exec.c : le TLS de
   l'exécutable), NULL sinon */
extern void (*linux_thread_hook) (void);

/* nombre d'appels traduits, par numéro de Linux (diagnostic : linuxhost --stats) */
extern unsigned long linux_syscall_count[512];

/* capture.c : déclare au noyau les plages des modules (n paires {début, fin}, au plus 4) dont les
   syscall directs reviennent à linux_syscall (capture de syscall de Korin) ; < 0 : erreur du noyau */
int linux_capture_set (const uint64_t *ranges, int n);
/* capture.c : syscall directs servis (Korin seulement) */
extern unsigned long linux_capture_count;

#ifdef __cplusplus
}
#endif

#endif
