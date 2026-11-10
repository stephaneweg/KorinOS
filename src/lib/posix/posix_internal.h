/*
 * posix_internal.h -- declarations privees de libposix (src/lib/posix, docs/POSIX.md). Pas
 * installe. Reprise de libonyxposix (Onyx, user/Runtime/libc/posix) pour ARMPI :
 *
 *  - le bloc de controle de chaque thread (struct __posix_thread) juste sous son TCB, trouve par
 *    TPIDR_EL0 (tls.c) ; TLS natif (chaine aarch64-onyx-elf : errno et _reent de newlib par
 *    thread) ;
 *  - la table des descripteurs (fd.c) : fd -> descriptions ouvertes (comptees) sur les objets
 *    d'ARMPI : descripteurs d'AppKit (fichiers, repertoires, entree / sortie standard), bouts de
 *    tubes de vfs, sockets de NetKit, pseudo-fichiers de /dev ;
 *  - le systeme : armpi.h (appels, erreurs, kits, vfs).
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
#ifndef _POSIX_INTERNAL_H
#define _POSIX_INTERNAL_H

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <newlib.h>
#include "armpi.h"

#if !defined(_WANT_REENT_THREAD_LOCAL)
#error "libposix demande la chaine aarch64-onyx-elf (newlib --enable-newlib-reent-thread-local, TLS natif)"
#endif

#define POSIX_PAGE		4096UL		/* la page du noyau d'ARMPI */
#define POSIX_PATH_MAX		1024
#define POSIX_FD_MAX		1024		/* FD_SETSIZE, RLIMIT_NOFILE */
#define POSIX_KEYS_MAX		PTHREAD_KEYS_MAX
#define POSIX_STACK_DEFAULT	(256UL << 10)	/* pile d'un pthread (peuplee a la demande) */
#define POSIX_STACK_MAX		(64UL << 20)
#define POSIX_WAIT_FOREVER	0xFFFFFFFFu	/* __posix_futex_wait : sans delai */

#define POSIX_ERR(e)		(errno = (e), -1)
/* un resultat d'ARMPI (negatif : erreur) -> le resultat, ou -1 et errno */
#define POSIX_SYS(r)		__armpi_ret ((long) (r))

/* ---- threads ----------------------------------------------------------------------------- */
#define POSIX_THREAD_MAGIC	0x4F4E5854u	/* "ONXT" */

struct __posix_thread
{
	struct __posix_thread *self;
	unsigned magic;
	int tid;				/* tid du noyau */
	int err;
	volatile int state;			/* POSIX_T_* (le mot de futex des join) */
	int detached;
	int is_main;
	void *(*start) (void *);
	void *arg;
	void *result;
	void *specific[POSIX_KEYS_MAX];		/* pthread_setspecific */
	unsigned long long stack_lo, stack_hi, guard;
	unsigned long stack_size;
	void *stack_block;			/* la region de la pile (mmap), garde comprise, rendue apres le join du noyau */
	void *block;				/* le bloc (malloc : ce bloc + TCB + TLS) ; 0 : statique */
	struct __posix_thread *next;		/* tous les threads du process (liste de tls.c) */
	struct __posix_cleanup *cleanup;	/* pthread_cleanup_push */
	int prio;
	int cancel_state;
	char name[32];
} __attribute__ ((aligned (16)));

#define POSIX_T_RUNNING		0
#define POSIX_T_FINISHED	1		/* fini : resultat range, pas encore rejoint */

extern struct __posix_thread *__posix_main_thread;

/* le thread appelant (TPIDR_EL0 = son TCB, juste au-dessus de son bloc) */
static inline struct __posix_thread *__posix_self (void)
{
	unsigned long tp;
	__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tp));
	if (tp == 0)
		return __posix_main_thread;
	return (struct __posix_thread *) (tp - sizeof (struct __posix_thread));
}

/* l'identifiant qu'un mutex garde comme proprietaire */
static inline int __posix_owner_id (void)
{
	struct __posix_thread *t = __posix_self ();
	return t ? t->tid : 1;
}

/* tls.c */
void __posix_tls_init_main (void);
struct __posix_thread *__posix_thread_alloc (void);
void __posix_thread_free (struct __posix_thread *);
unsigned long __posix_thread_tp (struct __posix_thread *);
void __posix_thread_list_add (struct __posix_thread *);
void __posix_thread_list_remove (struct __posix_thread *);
struct __posix_thread *__posix_thread_list_first (void);
void __posix_thread_list_lock (void);
void __posix_thread_list_unlock (void);
void __posix_keys_run_destructors (struct __posix_thread *);	/* pthread.c */

/* ---- synchronisation (pthread_sync.c) ------------------------------------------------------ */
/* Dort tant que *addr == expected (futex du noyau) ; ms : POSIX_WAIT_FOREVER ou un delai.
 * -> 0 reveille / change, 1 delai passe. */
int __posix_futex_wait (volatile unsigned *addr, unsigned expected, unsigned ms);
void __posix_futex_wake (volatile unsigned *addr);
long long __posix_ms_until (clockid_t clock, const struct timespec *abs);
void __posix_lock (volatile unsigned *l);
void __posix_unlock (volatile unsigned *l);

/* ---- temps (time.c) ------------------------------------------------------------------------ */
unsigned long long __posix_mono_ns (void);
long long __posix_real_ns (void);
int __posix_tz_minutes (void);
void __posix_sleep_ns (unsigned long long ns);

/* ---- chemins (path.c) ---------------------------------------------------------------------- */
/* Un chemin d'application rendu chemin d'ARMPI dans buf : "/tmp/x" -> "RAM0:/tmp/x" (cree au
 * premier usage) ; les autres tels quels (vfs resout "x", "./x", "/x" d'apres le dossier
 * courant). -> buf, ou 0 (ENAMETOOLONG). */
char *__posix_path (const char *in, char *buf, size_t cap);
int __posix_dev_type (const char *path);	/* "/dev/null"... -> POSIX_FD_*, sinon 0 */
int __posix_abspath (const char *in, char *out, size_t cap);	/* "SD0:/a/b" ; 0 / -1 */
char *__posix_at_path (int dirfd, const char *name, char *buf, size_t cap);
const char *__posix_exe_path (void);		/* le chemin du programme (dl.c) */

/* ---- descripteurs (fd.c) ------------------------------------------------------------------- */
enum
{
	POSIX_FD_NONE = 0,
	POSIX_FD_FILE,			/* un descripteur d'AppKit (64..127) */
	POSIX_FD_DIR,			/* un repertoire ouvert en descripteur (descripteur d'AppKit, chemin) */
	POSIX_FD_PIPE_R,		/* bouts d'un tube de vfs (poignee) */
	POSIX_FD_PIPE_W,
	POSIX_FD_SOCKET,		/* socket de NetKit */
	POSIX_FD_NULL,			/* /dev/null */
	POSIX_FD_ZERO,			/* /dev/zero */
	POSIX_FD_RANDOM,		/* /dev/urandom, /dev/random */
	POSIX_FD_CONSOLE,		/* 0, 1, 2 : entree, sortie, erreur standard (AppKit) */
	POSIX_FD_LSOCK,			/* bout de canal local du noyau (socketpair AF_UNIX) : poignee d'IPC */
	POSIX_FD_SHM,			/* objet de memoire partagee du noyau (memfd_create, shm_open) : poignee */
	POSIX_FD_TTY			/* bout de pseudo-terminal du service vterm (tty.c) : poignee */
};

struct __posix_ofd
{
	int type;
	volatile int refs;
	int flags;			/* O_ACCMODE | O_APPEND | O_NONBLOCK de newlib */
	long long h;			/* FILE / DIR : descripteur d'AppKit ; PIPE : poignee de vfs ;
					   SOCKET : sd de NetKit ; TTY : poignee de vterm */
	char *path;			/* FILE / DIR : chemin d'ARMPI (fstat, fchdir, dirfd) */
	volatile unsigned lock;		/* serialise position et tampon de retenue */
	int console;			/* CONSOLE : 0 entree, 1 sortie, 2 erreur */
	int child_end;			/* PIPE : bout donne a un fils (posix_spawn) */
	/* tampon de retenue : octets lus d'avance par poll (tubes, sockets) ou gardes par MSG_PEEK */
	unsigned char *carry;
	unsigned carry_n, carry_off;
	int eof;			/* l'autre bout a fini (tubes, sockets vus par poll) */
	/* sockets */
	int sotype;			/* SOCK_STREAM / SOCK_DGRAM */
	int connected, listening, so_error, bound, shut_wr;
	unsigned short lport;		/* port local (bind) */
	unsigned char peer[16];		/* struct sockaddr_in de l'autre bout */
	unsigned rcvtimeo, sndtimeo;	/* ms, 0 : aucun */
	long long pending;		/* listen : connexion deja acceptee par poll (sd + 1, 0 : aucune) */
	int unlinked;			/* FILE : efface pendant qu'il etait ouvert (unlink differe) */
	/* LSOCK, SHM : h = poignee d'IPC du noyau (ipc.c, docs/ABI.md 0x70-0x7D) */
};

struct __posix_ofd *__posix_fd_get (int fd);
void __posix_fd_put (struct __posix_ofd *d);
struct __posix_ofd *__posix_ofd_new (int type, int flags);
int __posix_fd_install (struct __posix_ofd *d, int min, int cloexec);
int __posix_fd_close (int fd);
extern void (*__posix_fd_closing) (int fd);	/* un numero va etre rendu (epoll.c, s'il sert) */
void __posix_fd_init (void);
int __posix_fd_cloexec (int fd, int set);
int __posix_fd_place (struct __posix_ofd *d, int fd, int cloexec);
struct __posix_ofd *__posix_fd_peek (int fd, int *cloexec);
void __posix_ofd_lock (struct __posix_ofd *d);
void __posix_ofd_unlock (struct __posix_ofd *d);

/* entrees / sorties sur une description (file.c) */
ssize_t __posix_read (struct __posix_ofd *d, void *buf, size_t n);
ssize_t __posix_write (struct __posix_ofd *d, const void *buf, size_t n);
ssize_t __posix_pread (struct __posix_ofd *d, void *buf, size_t n, off_t off);
struct stat;
void *__posix_file_share (struct __posix_ofd *d, const struct stat *st, void *addr, unsigned long len,
	int prot, int flags, long off);		/* ipc.c : un fichier, ses pages partagees */
ssize_t __posix_pwrite (struct __posix_ofd *d, const void *buf, size_t n, off_t off);
off_t __posix_lseek (struct __posix_ofd *d, off_t off, int whence);
int __posix_fstat (struct __posix_ofd *d, struct stat *st);
int __posix_ofd_release (struct __posix_ofd *d);
int __posix_open (const char *path, int flags, int mode);
/* tubes (file.c) : lecture d'un bout (nowait : E_AGAIN plutot qu'attendre) -> octets, 0 fin, < 0 */
long __posix_pipe_read (long h, void *buf, size_t n, int nowait);
int __posix_pipe_close (long h);

/* unlink d'un fichier ouvert dans ce process (fd.c) : vfs efface tout de suite, POSIX garde le
 * contenu jusqu'a la derniere fermeture ; l'effacement est donc differe, et le chemin cache
 * (stat, open, readdir : absent) en attendant */
int __posix_path_shared (struct __posix_ofd *d);	/* -> 1 : un autre descripteur sur ce fichier */
int __posix_unlink_defer (const char *abspath);		/* -> 1 differe (ouvert ici), 0 sinon */
int __posix_unlink_pending (const char *abspath);	/* -> 1 si efface en attente */
void __posix_unlink_last_close (const char *abspath);	/* une description fermee : efface si c'etait la derniere */

/* stat (stat.c) */
int __posix_path_stat (const char *armpipath, struct stat *st);
int __posix_path_is_dir (const char *armpipath);

/* IPC du noyau (ipc.c) : canaux locaux (LSOCK), memoire partagee (SHM) */
struct iovec;
ssize_t __posix_lsock_send (struct __posix_ofd *d, const struct iovec *iov, int n, const int *fds, int nfds, int flags);
ssize_t __posix_lsock_recv (struct __posix_ofd *d, const struct iovec *iov, int n, int *fds, int *nfds, int *mflags, int flags);
int __posix_lsock_getsockopt (struct __posix_ofd *d, int level, int opt, void *v, unsigned *len);
int __posix_lsock_setsockopt (struct __posix_ofd *d, int level, int opt, const void *v, unsigned len);
int __posix_lsock_shutdown (struct __posix_ofd *d, int how);
int __posix_handle_close (long h);
long __posix_handle_size (long h);		/* LSOCK : octets en attente ; SHM : taille ; < 0 erreur */
int __posix_shm_truncate (struct __posix_ofd *d, long len);
int __posix_shm_seals (struct __posix_ofd *d, int add, int seals);	/* add 0 : F_GET_SEALS */
void *__posix_shm_mmap (struct __posix_ofd *d, void *addr, unsigned long len, int prot, int flags, long off);
void __posix_inherit_init (void);		/* fd.c : les poignees donnees par le pere a leurs descripteurs */
/* arguments et environnement d'un fils : un objet partage (proc.c, lu par env.c), donne par
 * HANDLE_INHERIT ; la marque dit le nombre de pages, le bit haut la distingue d'un descripteur */
#define POSIX_SPAWN_TAG		0x80000000u
#define POSIX_SPAWN_MAGIC	0x4E505341u		/* "ASPN" */
struct __posix_spawn_blob
{
	unsigned magic, argc, envc, size;
	char strings[];				/* argv[0..argc[, puis environ, chacun termine par 0 */
};
extern long __posix_spawn_h;			/* -1 : pas d'objet (lance par un programme en assembleur) */
extern unsigned __posix_spawn_pages;
/* poll : nfds entrees (POLLIN..) des descripteurs LSOCK / SHM, attente ms (< 0 : sans fin) -> pretes */
int __posix_handle_poll (const long *h, const short *ev, short *rev, int n, int ms);

/* sockets (socket.c) */
ssize_t __posix_sock_recv (struct __posix_ofd *d, void *buf, size_t n, int flags, void *from);
ssize_t __posix_sock_send (struct __posix_ofd *d, const void *buf, size_t n, int flags, const void *to);
int __posix_sock_close (struct __posix_ofd *d);
/* poll : -> 1 donnees (ou connexion en attente), 0 rien, -1 fin / erreur */
int __posix_carry_fill (struct __posix_ofd *d);
size_t __posix_carry_take (struct __posix_ofd *d, void *buf, size_t n, int peek);

/* ---- environnement, arguments, demarrage (env.c, start.c) ---------------------------------- */
void __posix_env_init (void);
int __posix_args_init (const char *line, char ***argv);	/* -> argc */
extern char *__posix_progname;
void __posix_exe_set (const char *path);
extern char *program_invocation_name;
extern char *program_invocation_short_name;

/* terminaux (tty.c) : bouts de pseudo-terminal de vterm (docs/VTERM.md) */
struct __posix_ofd *__posix_std_ofd (int i);	/* l'entree / sortie standard i si c'est un pty, sinon 0 */
int __posix_tty_stdio (void);			/* -> 1 : une entree / sortie standard est un pty */
ssize_t __posix_tty_read (struct __posix_ofd *d, void *buf, size_t n);
ssize_t __posix_tty_write (struct __posix_ofd *d, const void *buf, size_t n);
int __posix_tty_ready (struct __posix_ofd *d);	/* poll : 1 donnees, -1 fin, 0 rien */
int __posix_tty_open (const char *path, int flags);	/* /dev/tty, /dev/ptmx, /dev/pts/N ; -2 : autre */
int __posix_tty_ioctl (struct __posix_ofd *d, unsigned long req, void *arg);
void __posix_tty_release (struct __posix_ofd *d);
extern volatile unsigned __posix_sig_count;	/* signal.c : gestionnaires de signaux lances */

/* ---- divers ------------------------------------------------------------------------------- */
int __posix_signal_default (int sig);		/* signal.c : l'action d'un signal */
/* process (proc.c) : un fils fini (code de sortie d'ARMPI) -> statut de wait */
int __posix_wait_status (long code);

#endif /* _POSIX_INTERNAL_H */
