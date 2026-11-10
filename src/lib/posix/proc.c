/*
 * proc.c -- identites, posix_spawn, waitpid, exec*, system, popen (libposix, docs/POSIX.md ;
 * reecrit pour ARMPI d'apres proc.c d'Onyx).
 *
 *  - posix_spawn : V_EXEC a vfs (chemin, ou nom seul : SD0:/bin/<nom>, puis
 *    SD0:/apps/<nom>.app/main), puis f_spawn_file d'AppKit (lecture de l'image, SYS_SPAWN_IMAGE).
 *    argv[1..] devient la ligne d'arguments (un argument avec espaces ou guillemets entre
 *    guillemets, \" et \\ dedans : env.c les relit). Entree / sortie / erreur du fils : celles du
 *    pere (descripteurs 0, 1, 2 : un tube de vfs est passe tel quel), ou un tube donne par
 *    posix_spawn_file_actions_adddup2 ; addchdir_np : le pere change de dossier le temps du
 *    lancement. argv et l'environnement vont aussi dans un objet partage donne au fils
 *    (spawn_blob, HANDLE_INHERIT ; lu par env.c), sans limite de longueur ; la ligne ne reste
 *    que pour les programmes en assembleur (vide si elle depasse 1 024 octets).
 *    Un fichier comme entree / sortie du fils (adddup2 d'un fichier, addopen ; descripteurs 0 a
 *    2) : un bout-fichier de vfs (V_FILE_END), que le fils ouvre lui-meme, en ajout pour
 *    l'ecriture (la position du pere n'est pas reprise). addopen au-dela de 2 : ENOTSUP.
 *  - waitpid : SYS_WAIT (le noyau reveille le pere a la fin du fils) ; les fils sont ceux lances
 *    par posix_spawn ou nes d'un fork (ECHILD sinon) ; WNOHANG regarde SYS_PROCLIST. Code de
 *    sortie d'ARMPI : >= 0 -> WIFEXITED ; -signal (kill, faute, action par defaut d'un signal)
 *    -> WIFSIGNALED.
 *  - fork : SYS_FORK par f_fork d'AppKit (positions des fichiers partagees, AppKit remis en etat
 *    dans le fils) ; avant, les fonctions prepare de pthread_atfork puis les verrous internes de
 *    libposix et de newlib, pris dans un ordre fixe ; apres, rendus dans le pere, remis a zero
 *    dans le fils (le seul thread qui reste), puis les fonctions parent / child. vfork est fork
 *    (le pere n'est pas suspendu ; ce que POSIX permet au fils d'un vfork marche pareil).
 *    getppid : SYS_GETPPID.
 *  - exec* : lance le programme, l'attend, puis _exit avec son code (le pid change). system,
 *    popen : le premier mot est le programme (pas de shell).
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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "posix_internal.h"

extern char **environ;

/* ---- identites ---- */
int _getpid (void) { return (int) __armpi_sys0 (SYS_GETPID); }
pid_t getpid (void) { return (pid_t) _getpid (); }
pid_t getppid (void) { return (pid_t) __armpi_sys0 (SYS_GETPPID); }
pid_t getpgrp (void) { return getpid (); }
pid_t getpgid (pid_t p) { return p ? p : getpid (); }
pid_t getsid (pid_t p) { return p ? p : getpid (); }
pid_t setsid (void) { return getpid (); }
int setpgid (pid_t a, pid_t b) { (void) a; (void) b; return 0; }

/* ---- les fils ---- */
#define MAX_CHILDREN	64
static int s_children[MAX_CHILDREN];
static volatile unsigned s_childLock;

static void child_add (int pid)
{
	__posix_lock (&s_childLock);
	for (int i = 0; i < MAX_CHILDREN; i++)
		if (s_children[i] == 0)
		{
			s_children[i] = pid;
			break;
		}
	__posix_unlock (&s_childLock);
}

static int child_remove (int pid)
{
	int found = 0;
	__posix_lock (&s_childLock);
	for (int i = 0; i < MAX_CHILDREN; i++)
		if (s_children[i] == pid)
		{
			s_children[i] = 0;
			found = 1;
		}
	__posix_unlock (&s_childLock);
	return found;
}

static int is_child (int pid)
{
	int r = 0;
	__posix_lock (&s_childLock);
	for (int i = 0; i < MAX_CHILDREN; i++)
		if (s_children[i] == pid)
			r = 1;
	__posix_unlock (&s_childLock);
	return r;
}

/* le process pid tourne-t-il encore ? (SYS_PROCLIST) */
static volatile unsigned s_aliveLock;
static int alive (int pid)
{
	static long list[64 * (ARMPI_PROCLIST_ENT / 8)];
	volatile unsigned *lock = &s_aliveLock;
	__posix_lock (lock);
	long n = __armpi_sys2 (SYS_PROCLIST, list, 64);
	int r = 0;
	for (long i = 0; i < n; i++)
		if (list[i * (ARMPI_PROCLIST_ENT / 8)] == pid && list[i * (ARMPI_PROCLIST_ENT / 8) + 7] == 0)
			r = 1;				/* (+56 : 1 en fin de vie) */
	__posix_unlock (lock);
	return r;
}

int __posix_wait_status (long code)
{
	if (code < 0 && code > -NSIG)
		return (int) -code;			/* WIFSIGNALED, WTERMSIG */
	return (int) ((code & 0xFF) << 8);		/* WIFEXITED */
}

/* ---- actions sur les fichiers, attributs ---- */
#define SPAWN_ACT_MAX	64
struct posix_spawn_actions			/* posix_spawn_file_actions_t pointe sur l'une d'elles */
{
	long stdio[3];				/* poignees de tubes de vfs (0 : celles du pere) */
	int set[3];
	char *cwd;
	char *path[3];				/* fichier du descripteur 0, 1, 2 du fils */
	int mode[3];				/* 1 lecture, 2 ecriture */
	int oflags[3];				/* addopen : O_CREAT, O_TRUNC... ; 0 : deja ouvert */
	int omode[3];
	int unsupported;			/* addopen au-dela de 2 : ENOTSUP au lancement */
	int nact;				/* adddup2 / addclose, dans l'ordre (canaux et memoire
						   partagee du noyau : spawn_handles) */
	struct { int op, fd, newfd; } act[SPAWN_ACT_MAX];
};
#define SPAWN_DUP2	1
#define SPAWN_CLOSE	2

static int act_add (struct posix_spawn_actions *a, int op, int fd, int newfd)
{
	if (a->nact >= SPAWN_ACT_MAX)
		return ENOMEM;
	a->act[a->nact].op = op;
	a->act[a->nact].fd = fd;
	a->act[a->nact].newfd = newfd;
	a->nact++;
	return 0;
}

static int is_kobj (struct __posix_ofd *d)
{
	return d->type == POSIX_FD_LSOCK || d->type == POSIX_FD_SHM;
}

int posix_spawn_file_actions_init (posix_spawn_file_actions_t *fa)
{
	struct posix_spawn_actions *a = (struct posix_spawn_actions *) calloc (1, sizeof *a);
	if (a == 0)
		return ENOMEM;
	*fa = (posix_spawn_file_actions_t) a;
	return 0;
}

int posix_spawn_file_actions_destroy (posix_spawn_file_actions_t *fa)
{
	struct posix_spawn_actions *a = (struct posix_spawn_actions *) *fa;
	if (a)
	{
		for (int i = 0; i < 3; i++)
			free (a->path[i]);
		free (a->cwd);
		free (a);
	}
	*fa = 0;
	return 0;
}

/* la poignee a donner au fils pour un descripteur du pere : un tube de vfs, 0 (heriter :
 * l'entree / la sortie d'AppKit), ou -1 (pas transmissible) */
static long fd_handle (int fd, int *child_end)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return -1;
	long h = -1;
	if (d->type == POSIX_FD_PIPE_R || d->type == POSIX_FD_PIPE_W)
	{
		h = d->h;
		if (child_end)
			d->child_end = 1;
	}
	else if (d->type == POSIX_FD_CONSOLE)
		h = 0;
	__posix_fd_put (d);
	return h;
}

int posix_spawn_file_actions_adddup2 (posix_spawn_file_actions_t *fa, int fd, int newfd)
{
	struct posix_spawn_actions *a = (struct posix_spawn_actions *) *fa;
	if (a == 0 || fd < 0 || newfd < 0 || newfd >= POSIX_FD_MAX)
		return EINVAL;
	int ce;
	struct __posix_ofd *d = __posix_fd_peek (fd, &ce);
	if (d == 0)
		return EBADF;
	int k = is_kobj (d);
	char *file = d->type == POSIX_FD_NULL && newfd <= 2 ? strdup ("/dev/null") :
		d->type == POSIX_FD_FILE && d->path && newfd <= 2 ? strdup (d->path) : 0;
	int fmode = (d->flags & O_ACCMODE) == O_RDONLY ? 1 : 2;
	__posix_fd_put (d);
	int e = act_add (a, SPAWN_DUP2, fd, newfd);
	if (e)
	{
		free (file);
		return e;
	}
	if (k || newfd > 2)
		return 0;				/* (un canal, un objet partage : a son numero ;
						   autre chose au-dela de 2 : le fils ne l'a pas) */
	free (a->path[newfd]);
	a->path[newfd] = file;
	if (file)
	{
		a->mode[newfd] = fmode;
		a->oflags[newfd] = 0;
		return 0;
	}
	long h = fd_handle (fd, 0);
	if (h < 0)
		return errno == EBADF ? EBADF : ENOTSUP;
	a->stdio[newfd] = h;
	a->set[newfd] = 1;
	return 0;
}

int posix_spawn_file_actions_addopen (posix_spawn_file_actions_t *fa, int fd, const char *path, int flags, mode_t mode)
{
	struct posix_spawn_actions *a = (struct posix_spawn_actions *) *fa;
	if (a == 0 || path == 0 || fd < 0)
		return EINVAL;
	if (fd > 2)
	{
		a->unsupported = 1;
		return 0;
	}
	char abs[POSIX_PATH_MAX];
	if (strcmp (path, "/dev/null") == 0)
		strcpy (abs, path);
	else if (__posix_abspath (path, abs, sizeof abs) != 0)
		return errno;
	char *p = strdup (abs);
	if (p == 0)
		return ENOMEM;
	free (a->path[fd]);
	a->path[fd] = p;
	a->mode[fd] = (flags & O_ACCMODE) == O_RDONLY ? 1 : 2;
	a->oflags[fd] = flags;
	a->omode[fd] = (int) mode;
	a->set[fd] = 0;
	return 0;
}

int posix_spawn_file_actions_addclose (posix_spawn_file_actions_t *fa, int fd)
{
	struct posix_spawn_actions *a = (struct posix_spawn_actions *) *fa;
	if (a == 0 || fd < 0 || fd >= POSIX_FD_MAX)
		return EINVAL;
	return act_add (a, SPAWN_CLOSE, fd, 0);
}

int posix_spawn_file_actions_addchdir_np (posix_spawn_file_actions_t *fa, const char *dir)
{
	struct posix_spawn_actions *a = (struct posix_spawn_actions *) *fa;
	if (a == 0 || dir == 0)
		return EINVAL;
	free (a->cwd);
	a->cwd = strdup (dir);
	return a->cwd ? 0 : ENOMEM;
}

int posix_spawn_file_actions_addfchdir_np (posix_spawn_file_actions_t *fa, int fd)
{
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return EBADF;
	int r = d->type == POSIX_FD_DIR && d->path ? posix_spawn_file_actions_addchdir_np (fa, d->path) : ENOTDIR;
	__posix_fd_put (d);
	return r;
}

struct posix_spawn_attr { short flags; sigset_t mask, def; pid_t pgroup; int policy; struct sched_param param; };

int posix_spawnattr_init (posix_spawnattr_t *at)
{
	struct posix_spawn_attr *a = (struct posix_spawn_attr *) calloc (1, sizeof *a);
	if (a == 0)
		return ENOMEM;
	*at = (posix_spawnattr_t) a;
	return 0;
}
int posix_spawnattr_destroy (posix_spawnattr_t *at) { free (*at); *at = 0; return 0; }
#define ATTR(at) ((struct posix_spawn_attr *) *(at))
int posix_spawnattr_setflags (posix_spawnattr_t *at, short f) { ATTR (at)->flags = f; return 0; }
int posix_spawnattr_getflags (const posix_spawnattr_t *at, short *f) { *f = ATTR (at)->flags; return 0; }
int posix_spawnattr_setsigmask (posix_spawnattr_t *at, const sigset_t *m) { ATTR (at)->mask = *m; return 0; }
int posix_spawnattr_getsigmask (const posix_spawnattr_t *at, sigset_t *m) { *m = ATTR (at)->mask; return 0; }
int posix_spawnattr_setsigdefault (posix_spawnattr_t *at, const sigset_t *m) { ATTR (at)->def = *m; return 0; }
int posix_spawnattr_getsigdefault (const posix_spawnattr_t *at, sigset_t *m) { *m = ATTR (at)->def; return 0; }
int posix_spawnattr_setpgroup (posix_spawnattr_t *at, pid_t p) { ATTR (at)->pgroup = p; return 0; }
int posix_spawnattr_getpgroup (const posix_spawnattr_t *at, pid_t *p) { *p = ATTR (at)->pgroup; return 0; }
int posix_spawnattr_setschedpolicy (posix_spawnattr_t *at, int p) { ATTR (at)->policy = p; return 0; }
int posix_spawnattr_getschedpolicy (const posix_spawnattr_t *at, int *p) { *p = ATTR (at)->policy; return 0; }
int posix_spawnattr_setschedparam (posix_spawnattr_t *at, const struct sched_param *p) { ATTR (at)->param = *p; return 0; }
int posix_spawnattr_getschedparam (const posix_spawnattr_t *at, struct sched_param *p) { *p = ATTR (at)->param; return 0; }

/* ---- lancement ---- */
/* argv[1..] -> la ligne d'arguments (1024 octets au plus, comme le noyau) ; 0 si trop longue */
static char *make_line (char *const argv[])
{
	size_t cap = 1024, o = 0;
	char *b = (char *) malloc (cap);
	if (b == 0)
		return 0;
	for (int i = 1; argv && argv[i]; i++)
	{
		const char *s = argv[i];
		int quote = *s == '\0' || strpbrk (s, " \t\"\\") != 0;
		size_t need = strlen (s) * 2 + 4;
		if (o + need >= cap)
		{
			free (b);
			return 0;
		}
		if (o)
			b[o++] = ' ';
		if (quote)
			b[o++] = '"';
		for (; *s; s++)
		{
			if (quote && (*s == '"' || *s == '\\'))
				b[o++] = '\\';
			b[o++] = *s;
		}
		if (quote)
			b[o++] = '"';
	}
	b[o] = '\0';
	return b;
}

/* les canaux et objets partages du noyau (ipc.c) que le fils recoit a leur numero : ceux du pere
 * sans FD_CLOEXEC, apres les actions (dup2 : la copie n'a pas FD_CLOEXEC ; close) ->
 * HANDLE_INHERIT (le noyau les met dans la table du fils au lancement ; le fils les place a
 * leur numero au demarrage : __posix_inherit_init). -> 0, ou errno */
#define INHERIT_MAX	255
static int spawn_handles (struct posix_spawn_actions *a, long blob, unsigned blob_pages)
{
	long *map = (long *) calloc (POSIX_FD_MAX, sizeof *map);	/* poignee + 1 << 32 si CLOEXEC */
	if (map == 0)
		return ENOMEM;
	for (int fd = 0; fd < POSIX_FD_MAX; fd++)
	{
		int ce = 0;
		struct __posix_ofd *d = __posix_fd_peek (fd, &ce);
		if (d == 0)
			continue;
		if (is_kobj (d))
			map[fd] = (d->h & 0xFFFFFFFFL) | (ce ? 1L << 32 : 0);
		__posix_fd_put (d);
	}
	for (int i = 0; a && i < a->nact; i++)
		if (a->act[i].op == SPAWN_CLOSE)
			map[a->act[i].fd] = 0;
		else
			map[a->act[i].newfd] = map[a->act[i].fd] & 0xFFFFFFFFL;	/* (sans FD_CLOEXEC) */
	struct { unsigned h, tag; } *v = calloc (INHERIT_MAX, sizeof *v);
	int n = 0;
	for (int fd = 0; v && fd < POSIX_FD_MAX && n < INHERIT_MAX; fd++)
		if (map[fd] != 0 && (map[fd] >> 32) == 0)
		{
			v[n].h = (unsigned) map[fd];
			v[n].tag = (unsigned) fd + 1;
			n++;
		}
	free (map);
	if (v == 0)
		return ENOMEM;
	if (blob >= 0 && n < INHERIT_MAX)
	{
		v[n].h = (unsigned) blob;
		v[n].tag = POSIX_SPAWN_TAG | blob_pages;
		n++;
	}
	long r = n ? __armpi_sys2 (SYS_HANDLE_INHERIT, v, n) : 0;
	free (v);
	return r < 0 && r != -38 ? __armpi_errno (r) : 0;	/* (noyau sans IPC : rien a donner) */
}

static volatile unsigned s_spawnLock;		/* HANDLE_INHERIT puis le lancement, d'un seul tenant */

/* argv et envp dans un objet partage -> sa poignee (*pages : sa taille), -1 sans objet (noyau
 * sans IPC, plus de memoire) : le fils se contente alors de la ligne et des valeurs par defaut */
static long spawn_blob (char *const argv[], char *const envp[], unsigned *pages)
{
	size_t size = sizeof (struct __posix_spawn_blob);
	unsigned argc = 0, envc = 0;
	for (; argv && argv[argc]; argc++)
		size += strlen (argv[argc]) + 1;
	for (; envp && envp[envc]; envc++)
		size += strlen (envp[envc]) + 1;
	size_t bytes = (size + 4095) & ~4095UL;
	long h = __armpi_sys2 (SYS_SHM_CREATE, bytes, 0);
	if (h < 0)
		return -1;
	long p = __armpi_sys6 (SYS_SHM_MAP, h, 0, bytes, PROT_READ | PROT_WRITE, 0, 0);
	if (p < 0 && p > -4096)
	{
		__armpi_sys1 (SYS_HANDLE_CLOSE, h);
		return -1;
	}
	struct __posix_spawn_blob *b = (struct __posix_spawn_blob *) p;
	b->magic = POSIX_SPAWN_MAGIC;
	b->argc = argc;
	b->envc = envc;
	b->size = (unsigned) size;
	char *s = b->strings;
	for (unsigned i = 0; i < argc; i++)
		s = stpcpy (s, argv[i]) + 1;
	for (unsigned i = 0; i < envc; i++)
		s = stpcpy (s, envp[i]) + 1;
	__armpi_sys2 (SYS_VM_UNMAP, p, bytes);
	*pages = (unsigned) (bytes >> 12);
	return h;
}

static int do_spawn_locked (pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, char *const argv[], int blob);

static int do_spawn (pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, char *const argv[], char *const envp[])
{
	struct posix_spawn_actions *a = fa ? (struct posix_spawn_actions *) *fa : 0;
	unsigned pages = 0;
	long blob = spawn_blob (argv, envp ? envp : environ, &pages);
	__posix_lock (&s_spawnLock);
	int r = spawn_handles (a, blob, pages);
	if (r == 0)
	{
		r = do_spawn_locked (pid, path, fa, argv, blob >= 0);
		if (r != 0)
			__armpi_sys2 (SYS_HANDLE_INHERIT, 0, 0);	/* (pas de fils : rendues) */
	}
	__posix_unlock (&s_spawnLock);
	if (blob >= 0)
		__armpi_sys1 (SYS_HANDLE_CLOSE, blob);		/* (le fils a sa reference) */
	return r;
}

/* un bout de vfs vers ce fichier -> poignee, ou une erreur d'ARMPI */
static long file_end (const char *path, int mode)
{
	struct __armpi_msg m;
	size_t n = strlen (path);
	if (n >= sizeof m.data)
		return -36;				/* ENAMETOOLONG */
	memset (&m, 0, sizeof m);
	m.op = V_FILE_END;
	m.a = mode;
	strcpy ((char *) m.data, path);
	m.len = (unsigned) n + 1;
	long r = __armpi_vfs_call (&m);
	return r < 0 ? r : m.a;
}

static void end_close (long h)
{
	struct __armpi_msg m;
	memset (&m, 0, sizeof m);
	m.op = V_PIPE_CLOSE;
	m.a = h;
	__armpi_vfs_call (&m);
}

static int do_spawn_locked (pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, char *const argv[], int blob)
{
	struct posix_spawn_actions *a = fa ? (struct posix_spawn_actions *) *fa : 0;
	if (path == 0 || *path == '\0')
		return ENOENT;
	if (a && a->unsupported)
		return ENOTSUP;
	char p[POSIX_PATH_MAX];
	if (__posix_path (path, p, sizeof p) == 0)
		return errno;
	if (strlen (p) >= sizeof ((struct __armpi_msg *) 0)->data)
		return ENAMETOOLONG;
	long stdio[3];
	for (int i = 0; i < 3; i++)
	{
		if (a && a->set[i])
			stdio[i] = a->stdio[i];
		else
		{
			int e = errno;
			stdio[i] = fd_handle (i, 0);
			errno = e;
			if (stdio[i] < 0)
				stdio[i] = 0;		/* (fermee ou pas un tube : celle d'AppKit) */
		}
	}
	long ends[3] = { 0, 0, 0 };
	int err = 0;
	for (int i = 0; a && i < 3 && err == 0; i++)
	{
		if (a->path[i] == 0)
			continue;
		if ((a->oflags[i] & (O_CREAT | O_TRUNC)) && strcmp (a->path[i], "/dev/null") != 0)
		{
			int t = open (a->path[i], a->oflags[i], a->omode[i]);
			if (t < 0)
			{
				err = errno;
				break;
			}
			close (t);
		}
		long h = file_end (a->path[i], a->mode[i]);
		if (h < 0)
			err = __armpi_errno (h);
		else
		{
			ends[i] = h;
			stdio[i] = h;
		}
	}
	char *line = err ? 0 : make_line (argv);
	if (err == 0 && line == 0 && !blob)
		err = E2BIG;
	if (err)
	{
		for (int i = 0; i < 3; i++)
			if (ends[i])
				end_close (ends[i]);
		return err;
	}
	char old[POSIX_PATH_MAX];
	int moved = 0;
	if (a && a->cwd)
	{
		if (getcwd (old, sizeof old) == 0 || chdir (a->cwd) != 0)
		{
			int e = errno;
			free (line);
			return e;
		}
		moved = 1;
	}
	struct __armpi_msg m;
	memset (&m, 0, sizeof m);
	m.op = V_EXEC;
	strcpy ((char *) m.data, p);
	m.len = (unsigned) strlen (p) + 1;
	long r = __armpi_vfs_call (&m);
	if (r >= 0)
	{
		long (*spawn_file) (void *, const char *, long *) =
			(long (*) (void *, const char *, long *)) __armpi_appkit (AK_F_SPAWN_FILE);
		r = spawn_file ? spawn_file (&m, line && *line ? line : 0, stdio) : -38;
	}
	if (moved)
		chdir (old);
	free (line);
	for (int i = 0; i < 3; i++)
		if (ends[i])
			end_close (ends[i]);		/* (le fils a sa copie) */
	if (r < 0)
		return __armpi_errno (r);
	child_add ((int) r);
	if (pid)
		*pid = (pid_t) r;
	return 0;
}

int posix_spawn (pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa,
		 const posix_spawnattr_t *at, char *const argv[], char *const envp[])
{
	(void) at;
	return do_spawn (pid, path, fa, argv, envp);
}

int posix_spawnp (pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa,
		  const posix_spawnattr_t *at, char *const argv[], char *const envp[])
{
	/* un nom seul : vfs le cherche dans SD0:/bin puis SD0:/apps (V_EXEC) */
	return posix_spawn (pid, file, fa, at, argv, envp);
}

/* ---- attente ---- */
static pid_t reap (int pid, int *status)
{
	long code = __armpi_sys1 (SYS_WAIT, pid);
	child_remove (pid);
	if (status)
		*status = __posix_wait_status (code);
	return pid;
}

pid_t waitpid (pid_t pid, int *status, int options)
{
	if (pid > 0)
	{
		if (!is_child (pid))
			return POSIX_ERR (ECHILD);
		if ((options & WNOHANG) && alive (pid))
			return 0;
		return reap (pid, status);
	}
	for (;;)
	{
		int any = 0, last = 0;
		for (int i = 0; i < MAX_CHILDREN; i++)
		{
			int c = s_children[i];
			if (c == 0)
				continue;
			any++;
			last = c;
			if (!alive (c))
				return reap (c, status);
		}
		if (!any)
			return POSIX_ERR (ECHILD);
		if (options & WNOHANG)
			return 0;
		if (any == 1)
			return reap (last, status);	/* un seul : SYS_WAIT attend sa fin */
		__armpi_sys1 (SYS_SLEEP, 5);
	}
}

pid_t wait (int *status) { return waitpid (-1, status, 0); }

/* un fils fini, pas encore releve par waitpid (0 : aucun) : le waitid (WNOWAIT) de .NET */
int __posix_exited_child (void)
{
	for (int i = 0; i < MAX_CHILDREN; i++)
	{
		int c = s_children[i];
		if (c != 0 && !alive (c))
			return c;
	}
	return 0;
}
int _wait (int *status) { return (int) wait (status); }

/* ---- fork, exec ---- */
pid_t fork (void)
{
	long (*kfork) (void) = (long (*) (void)) __armpi_appkit (AK_F_FORK);
	if (kfork == 0)
		return POSIX_ERR (ENOSYS);
	__posix_atfork_run (POSIX_FORK_PREPARE);
	/* les verrous internes, toujours dans cet ordre (chacun ne prend que ceux d'apres) */
	__posix_fork_lock (&s_spawnLock, POSIX_FORK_PREPARE);
	__posix_stdio_fork (POSIX_FORK_PREPARE);
	__posix_fork_lock (&s_childLock, POSIX_FORK_PREPARE);
	__posix_fork_lock (&s_aliveLock, POSIX_FORK_PREPARE);
	__posix_fd_fork (POSIX_FORK_PREPARE);
	__posix_keys_fork (POSIX_FORK_PREPARE);
	__posix_thread_fork (POSIX_FORK_PREPARE);
	__posix_mman_fork (POSIX_FORK_PREPARE);
	__posix_malloc_fork (POSIX_FORK_PREPARE);
	long r = kfork ();
	int phase = r == 0 ? POSIX_FORK_CHILD : POSIX_FORK_PARENT;
	__posix_malloc_fork (phase);
	__posix_mman_fork (phase);
	__posix_thread_fork (phase);		/* (fils : lui seul, son tid) */
	__posix_keys_fork (phase);
	__posix_fd_fork (phase);
	__posix_fork_lock (&s_aliveLock, phase);
	__posix_fork_lock (&s_childLock, phase);
	__posix_stdio_fork (phase);
	__posix_fork_lock (&s_spawnLock, phase);
	if (r == 0)
	{
		memset (s_children, 0, sizeof s_children);	/* les fils du pere ne sont pas les siens */
		__armpi_netkit_fork_child ();
	}
	else if (r > 0)
		child_add ((int) r);
	__posix_atfork_run (phase);
	return r < 0 ? (pid_t) POSIX_SYS (r) : (pid_t) r;
}
int _fork (void) { return (int) fork (); }
pid_t vfork (void) { return fork (); }

int execve (const char *path, char *const argv[], char *const envp[])
{
	pid_t pid;
	int r = posix_spawn (&pid, path, 0, 0, argv, envp);
	if (r != 0)
		return POSIX_ERR (r);
	int st = 0;
	waitpid (pid, &st, 0);
	fflush (0);
	_exit (WIFEXITED (st) ? WEXITSTATUS (st) : 128 + WTERMSIG (st));
}

int _execve (const char *path, char *const argv[], char *const envp[]) { return execve (path, argv, envp); }
int execv (const char *path, char *const argv[]) { return execve (path, argv, environ); }
int execvp (const char *file, char *const argv[]) { return execve (file, argv, environ); }
int execvpe (const char *file, char *const argv[], char *const envp[]) { return execve (file, argv, envp); }

static char **va_argv (const char *a0, va_list ap, char ***envp)
{
	int n = 1, cap = 16;
	char **v = (char **) malloc (cap * sizeof *v);
	if (v == 0)
		return 0;
	v[0] = (char *) a0;
	for (;;)
	{
		char *s = va_arg (ap, char *);
		if (n + 1 >= cap)
		{
			cap *= 2;
			char **nv = (char **) realloc (v, cap * sizeof *v);
			if (nv == 0)
			{
				free (v);
				return 0;
			}
			v = nv;
		}
		v[n++] = s;
		if (s == 0)
			break;
	}
	if (envp)
		*envp = va_arg (ap, char **);
	return v;
}

int execl (const char *path, const char *a0, ...)
{
	va_list ap;
	va_start (ap, a0);
	char **v = va_argv (a0, ap, 0);
	va_end (ap);
	return v ? execv (path, v) : POSIX_ERR (ENOMEM);
}

int execlp (const char *file, const char *a0, ...)
{
	va_list ap;
	va_start (ap, a0);
	char **v = va_argv (a0, ap, 0);
	va_end (ap);
	return v ? execvp (file, v) : POSIX_ERR (ENOMEM);
}

int execle (const char *path, const char *a0, ...)
{
	va_list ap;
	va_start (ap, a0);
	char **envp = 0;
	char **v = va_argv (a0, ap, &envp);
	va_end (ap);
	return v ? execve (path, v, envp) : POSIX_ERR (ENOMEM);
}

/* ---- system, popen : le premier mot est le programme (pas de shell sur ARMPI) ---- */
static int split_cmd (const char *cmd, char **prog, char **argv[])
{
	char *s = strdup (cmd);
	if (s == 0)
		return -1;
	int n = 0, cap = 8;
	char **v = (char **) malloc (cap * sizeof *v);
	if (v == 0)
	{
		free (s);
		return -1;
	}
	for (char *t = strtok (s, " \t"); t; t = strtok (0, " \t"))
	{
		if (n + 2 >= cap)
		{
			cap *= 2;
			v = (char **) realloc (v, cap * sizeof *v);
			if (v == 0)
				return -1;
		}
		v[n++] = t;
	}
	v[n] = 0;
	*prog = n ? v[0] : s;
	*argv = v;
	return n;
}

int system (const char *cmd)
{
	if (cmd == 0)
		return 1;
	char *prog;
	char **v;
	if (split_cmd (cmd, &prog, &v) <= 0)
		return -1;
	pid_t pid;
	int r = posix_spawnp (&pid, prog, 0, 0, v, environ);
	int st = 0;
	if (r == 0)
		waitpid (pid, &st, 0);
	free (v[0]);
	free (v);
	if (r != 0)
		return 127 << 8;
	return st;
}

static struct { FILE *f; pid_t pid; } s_popen[16];

FILE *popen (const char *cmd, const char *mode)
{
	if (cmd == 0 || mode == 0 || (mode[0] != 'r' && mode[0] != 'w'))
		return (FILE *) (long) POSIX_ERR (EINVAL);
	int fds[2];
	if (pipe (fds) != 0)
		return 0;
	int mine = mode[0] == 'r' ? fds[0] : fds[1];
	int theirs = mode[0] == 'r' ? fds[1] : fds[0];
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, theirs, mode[0] == 'r' ? 1 : 0);
	char *prog;
	char **v;
	pid_t pid = -1;
	int r = split_cmd (cmd, &prog, &v) > 0 ? posix_spawnp (&pid, prog, &fa, 0, v, environ) : EINVAL;
	posix_spawn_file_actions_destroy (&fa);
	close (theirs);
	if (r != 0)
	{
		close (mine);
		errno = r;
		return 0;
	}
	free (v[0]);
	free (v);
	FILE *f = fdopen (mine, mode[0] == 'r' ? "r" : "w");
	for (int i = 0; i < 16; i++)
		if (s_popen[i].f == 0)
		{
			s_popen[i].f = f;
			s_popen[i].pid = pid;
			break;
		}
	return f;
}

int pclose (FILE *f)
{
	pid_t pid = -1;
	for (int i = 0; i < 16; i++)
		if (s_popen[i].f == f)
		{
			pid = s_popen[i].pid;
			s_popen[i].f = 0;
		}
	fclose (f);
	if (pid < 0)
		return POSIX_ERR (ECHILD);
	int st = 0;
	return waitpid (pid, &st, 0) == pid ? st : -1;
}
