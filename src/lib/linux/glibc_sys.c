/*
 * glibc_sys.c -- liblinux : les fonctions de glibc qui ne sont que des appels système (docs/etude/LINUX-HOST.md,
 * jalon 3), et celles qui ont une structure de glibc à traduire : sigaction et sigset_t de 128 octets, DIR et
 * readdir, stat (__xstat…), statvfs, posix_spawn, passwd et group, waitpid, getcwd.
 *
 * Une enveloppe SC (nom, numéro) appelle linux_syscall (syscall.c, syscall2.c : la traduction y est faite une
 * fois, pour les syscall directs aussi) et rend -1 et errno de Linux en cas d'échec. Sur x86-64 les
 * arguments entiers et pointeurs sont dans les mêmes registres quelle que soit la signature : une seule
 * forme sert pour toutes (et pour open, fcntl, ioctl, qui sont variadiques).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <grp.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <iconv.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <termios.h>
#include <unistd.h>

#include "linux.h"
#include "glibc_internal.h"

#define SC(name, nr) \
	static long lx_##name (long a, long b, long c, long d, long e, long f) \
	{ return lx_ret (linux_syscall (nr, a, b, c, d, e, f)); }

/* fichiers */
SC (access, 21) SC (chdir, 80) SC (chmod, 90) SC (chown, 92) SC (chroot, 161) SC (fchdir, 81) SC (fchmod, 91)
SC (fchown, 93) SC (lchown, 94) SC (link, 86) SC (mkdir, 83) SC (readlink, 89) SC (rename, 82) SC (rmdir, 84)
SC (symlink, 88) SC (truncate, 76) SC (unlink, 87) SC (umask, 95) SC (fsync, 74) SC (fdatasync, 75) SC (flock, 73)
SC (dup2, 33) SC (pipe, 22) SC (pipe2, 293) SC (ioctl, 16) SC (lseek, 8) SC (pread, 17) SC (pwrite, 18)
SC (writev, 20) SC (readv, 19) SC (ftruncate, 77) SC (openat, 257) SC (faccessat, 269)
SC (fchmodat, 268) SC (linkat, 265) SC (mkdirat, 258) SC (readlinkat, 267) SC (renameat, 264)
SC (symlinkat, 266) SC (unlinkat, 263) SC (utimensat, 280) SC (utimes, 235) SC (statfs, 137) SC (fstatfs, 138)
SC (fallocate, 285) SC (sendfile, 40) SC (splice, 275) SC (mincore, 27) SC (msync, 26) SC (mremap, 25)
SC (getxattr, 191) SC (lgetxattr, 192) SC (fgetxattr, 193) SC (listxattr, 194) SC (llistxattr, 195)
SC (flistxattr, 196) SC (mount, 165) SC (unshare, 272) SC (personality, 135) SC (poll, 7) SC (ppoll, 271)
SC (select, 23) SC (dup, 32) SC (dup3, 292) SC (fstat_, 5) SC (stat_, 4) SC (lstat_, 6)
/* process */
SC (fork, 57) SC (vfork, 58) SC (execve, 59) SC (wait4, 61) SC (waitid, 247) SC (kill, 62) SC (getppid, 110)
SC (getpgrp, 111) SC (setpgid, 109) SC (setsid, 112) SC (getuid, 102) SC (getgid, 104) SC (getegid, 108)
SC (setuid, 105) SC (setgid, 106) SC (seteuid, 105) SC (setegid, 106) SC (getgroups, 115) SC (setgroups, 116)
SC (setrlimit, 160) SC (getrusage, 98) SC (alarm, 37) SC (pause, 34) SC (sigaltstack, 131)
SC (sched_setaffinity, 203) SC (process_vm_readv, 310) SC (timer_create, 222) SC (timer_delete, 226)
SC (timer_settime, 223)
/* sockets */
SC (accept, 43) SC (accept4, 288) SC (bind, 49) SC (connect, 42) SC (listen, 50) SC (getpeername, 52)
SC (getsockname, 51) SC (getsockopt, 55) SC (setsockopt, 54) SC (recvfrom, 45) SC (recvmsg, 47)
SC (recvmmsg, 299) SC (sendto, 44) SC (sendmsg, 46) SC (sendmmsg, 307) SC (shutdown, 48) SC (socket, 41)
SC (socketpair, 53)
/* epoll, eventfd, inotify, signalfd */
SC (epoll_create1, 291) SC (epoll_ctl, 233) SC (epoll_wait, 232) SC (epoll_pwait, 281) SC (eventfd, 290)
SC (signalfd, 289) SC (inotify_init1, 294) SC (inotify_add_watch, 254) SC (inotify_rm_watch, 255)

static long lx_open_at_cwd (long p, long fl, long mode) { return lx_ret (linux_syscall (257, -100, p, fl, mode, 0, 0)); }
static long lx_epoll_create (long size) { (void) size; return lx_ret (linux_syscall (291, 0, 0, 0, 0, 0, 0)); }
static long lx_fcntl (long fd, long cmd, long arg) { return lx_ret (linux_syscall (72, fd, cmd, arg, 0, 0, 0)); }
static uint16_t lx_ntohs (uint16_t v) { return (uint16_t) ((v >> 8) | (v << 8)); }
static uint32_t lx_ntohl (uint32_t v) { return __builtin_bswap32 (v); }
static long lx_recv (long fd, long b, long n, long fl) { return lx_ret (linux_syscall (45, fd, b, n, fl, 0, 0)); }
static long lx_send (long fd, long b, long n, long fl) { return lx_ret (linux_syscall (44, fd, b, n, fl, 0, 0)); }
static long lx_waitpid (long pid, long st, long opt) { return lx_ret (linux_syscall (61, pid, st, opt, 0, 0, 0)); }
static long lx_futimens (long fd, long t) { return lx_ret (linux_syscall (280, fd, 0, t, 0, 0, 0)); }
static long lx_raise (long s)
{
	return lx_ret (linux_syscall (234, getpid (), syscall (SYS_gettid), s, 0, 0, 0));
}
/* ce qui rend le numéro d'erreur au lieu de -1 */
static int lx_clock_nanosleep (long c, long fl, long req, long rem)
{
	long r = linux_syscall (230, c, fl, req, rem, 0, 0);
	return r < 0 ? (int) -r : 0;
}
static int lx_posix_fadvise (int fd, long off, long len, int adv) { (void) fd; (void) off; (void) len; (void) adv; return 0; }
static int lx_posix_madvise (void *p, size_t n, int adv) { (void) p; (void) n; (void) adv; return 0; }
static int lx_pthread_kill (pthread_t t, int s)
{
	int ks = s ? lx_sig_korin (s) : 0;
	if (s && !ks)
		return 22;
	return lx_perr (pthread_kill (t, ks));
}

/* stat : les formes de glibc (__xstat (version, …)) */
static long lx_xstat (int v, long p, long b) { (void) v; return lx_stat_ (p, b, 0, 0, 0, 0); }
static long lx_lxstat (int v, long p, long b) { (void) v; return lx_lstat_ (p, b, 0, 0, 0, 0); }
static long lx_fxstat (int v, long fd, long b) { (void) v; return lx_fstat_ (fd, b, 0, 0, 0, 0); }
static long lx_fxstatat (int v, long d, long p, long b, long fl) { (void) v; return lx_ret (linux_syscall (262, d, p, b, fl, 0, 0)); }

/* struct statvfs de Linux (112 octets) */
static int lx_statvfs (const char *path, uint64_t *out)
{
	struct statvfs v;
	if (statvfs (path, &v) < 0)
		return (int) lx_set_errno_from_korin ();
	memset (out, 0, 112);
	out[0] = v.f_bsize;
	out[1] = v.f_frsize ? v.f_frsize : v.f_bsize;
	out[2] = v.f_blocks;
	out[3] = v.f_bfree;
	out[4] = v.f_bavail;
	out[5] = v.f_files;
	out[6] = v.f_ffree;
	out[7] = v.f_favail;
	out[10] = 255;	/* f_namemax */
	return 0;
}

static char *lx_getcwd (char *buf, size_t n)
{
	char tmp[4096];
	long r = linux_syscall (79, (long) (buf ? buf : tmp), (long) (buf ? n : sizeof tmp), 0, 0, 0, 0);
	if (r < 0)
	{
		lx_ret (r);
		return 0;
	}
	return buf ? buf : lx_ustrdup (tmp);
}

/* ---- signaux : sigset_t de glibc, 128 octets (bit n-1 : le signal n) ---- */
static int lx_sigemptyset (uint64_t *s) { memset (s, 0, 128); return 0; }
static int lx_sigaddset (uint64_t *s, int n)
{
	if (n < 1 || n > 64) { *lx_errno_ptr () = 22; return -1; }
	s[0] |= 1ULL << (n - 1);
	return 0;
}
static int lx_sigdelset (uint64_t *s, int n)
{
	if (n < 1 || n > 64) { *lx_errno_ptr () = 22; return -1; }
	s[0] &= ~(1ULL << (n - 1));
	return 0;
}
static int lx_sigismember (const uint64_t *s, int n)
{
	if (n < 1 || n > 64) { *lx_errno_ptr () = 22; return -1; }
	return (int) ((s[0] >> (n - 1)) & 1);
}
static int lx_sigprocmask (int how, const uint64_t *set, uint64_t *old)
{
	return (int) lx_ret (linux_syscall (14, how, (long) set, (long) old, 8, 0, 0));
}
static int lx_pthread_sigmask (int how, const uint64_t *set, uint64_t *old)
{
	long r = linux_syscall (14, how, (long) set, (long) old, 8, 0, 0);
	return r < 0 ? (int) -r : 0;
}
static int lx_sigsuspend (const uint64_t *set) { return (int) lx_ret (linux_syscall (130, (long) set, 8, 0, 0, 0, 0)); }

/* struct sigaction de glibc : gestionnaire, masque (128), drapeaux (int), restorer */
struct lx_gsa { uint64_t handler; uint64_t mask[16]; int flags; int pad; uint64_t restorer; };
struct lx_kact { uint64_t handler, flags, restorer, mask; };

static int lx_sigaction (int s, const struct lx_gsa *a, struct lx_gsa *o)
{
	struct lx_kact k, ko;
	if (a)
	{
		k.handler = a->handler;
		k.flags = (uint64_t) (uint32_t) a->flags;
		k.restorer = a->restorer;
		k.mask = a->mask[0];
	}
	long r = linux_syscall (13, s, a ? (long) &k : 0, o ? (long) &ko : 0, 8, 0, 0);
	if (r < 0)
		return (int) lx_ret (r);
	if (o)
	{
		memset (o, 0, sizeof *o);
		o->handler = ko.handler;
		o->flags = (int) ko.flags;
		o->restorer = ko.restorer;
		o->mask[0] = ko.mask;
	}
	return 0;
}

/* signal () et les formes BSD et System V : sigaction de Linux (BSD : SA_RESTART ; System V : SA_RESETHAND et
   SA_NODEFER) ; Bun les enveloppe et appelle celles-ci par dlsym (RTLD_NEXT) */
static uint64_t set_signal (int s, uint64_t h, uint32_t flags)
{
	struct lx_kact k = { h, flags, 0, 0 }, o;
	long r = linux_syscall (13, s, (long) &k, (long) &o, 8, 0, 0);
	if (r < 0)
	{
		lx_ret (r);
		return ~0ULL;	/* SIG_ERR */
	}
	return o.handler;
}
static uint64_t lx_signal (int s, uint64_t h) { return set_signal (s, h, 0x10000000); }
static uint64_t lx_sysv_signal (int s, uint64_t h) { return set_signal (s, h, 0x80000000u | 0x40000000); }
static int lx_siginterrupt (int s, int f) { (void) s; (void) f; return 0; }
static int mask_one (int how, int s)
{
	uint64_t m[16] = { 0 };
	if (s < 1 || s > 64)
	{
		*lx_errno_ptr () = 22;
		return -1;
	}
	m[0] = 1ULL << (s - 1);
	return (int) lx_ret (linux_syscall (14, how, (long) m, 0, 8, 0, 0));
}
static int lx_sighold (int s) { return mask_one (0, s); }
static int lx_sigrelse (int s) { return mask_one (1, s); }
static int lx_sigignore (int s) { return set_signal (s, 1, 0) == ~0ULL ? -1 : 0; }
static uint64_t lx_sigset (int s, uint64_t h) { return set_signal (s, h, 0); }
static int lx_sigblock (int m) { uint64_t n = (uint32_t) m, o = 0; linux_syscall (14, 0, (long) &n, (long) &o, 8, 0, 0); return (int) o; }
static int lx_sigsetmask (int m) { uint64_t n = (uint32_t) m, o = 0; linux_syscall (14, 2, (long) &n, (long) &o, 8, 0, 0); return (int) o; }

/* ---- répertoires : un DIR de glibc fait ici, rempli par getdents64 (dirent64 = dirent sur x86-64) ---- */
struct lx_dir
{
	int fd;
	long pos, len;
	unsigned char buf[8192];
};

static void *lx_opendir (const char *path)
{
	long fd = linux_syscall (257, -100, (long) path, 0x10000 | 0x80000, 0, 0, 0);	/* O_DIRECTORY|O_CLOEXEC */
	if (fd < 0)
	{
		lx_ret (fd);
		return 0;
	}
	struct lx_dir *d = calloc (1, sizeof *d);
	if (!d)
	{
		linux_syscall (3, fd, 0, 0, 0, 0, 0);
		*lx_errno_ptr () = 12;
		return 0;
	}
	d->fd = (int) fd;
	return d;
}

static void *lx_fdopendir (int fd)
{
	struct lx_dir *d = calloc (1, sizeof *d);
	if (!d)
		return 0;
	d->fd = fd;
	return d;
}

static void *lx_readdir (struct lx_dir *d)
{
	if (d->pos >= d->len)
	{
		long n = linux_syscall (217, d->fd, (long) d->buf, sizeof d->buf, 0, 0, 0);
		if (n <= 0)
		{
			if (n < 0)
				lx_ret (n);
			return 0;
		}
		d->len = n;
		d->pos = 0;
	}
	unsigned char *e = d->buf + d->pos;
	d->pos += *(uint16_t *) (e + 16);
	return e;
}

static int lx_closedir (struct lx_dir *d)
{
	long r = linux_syscall (3, d->fd, 0, 0, 0, 0, 0);
	free (d);
	return (int) lx_ret (r);
}

static int lx_dirfd (struct lx_dir *d) { return d->fd; }
static void lx_rewinddir (struct lx_dir *d)
{
	linux_syscall (8, d->fd, 0, 0, 0, 0, 0);
	d->pos = d->len = 0;
}

/* ---- terminal : struct termios de glibc = celle de libposix (60 octets, mêmes drapeaux) ---- */
static int lx_tcgetattr (int fd, struct termios *t) { return tcgetattr (fd, t) < 0 ? (int) lx_set_errno_from_korin () : 0; }
static int lx_tcsetattr (int fd, int how, const struct termios *t)
{
	return tcsetattr (fd, how, t) < 0 ? (int) lx_set_errno_from_korin () : 0;
}
static int lx_isatty (int fd)
{
	int r = isatty (fd);
	if (!r)
		*lx_errno_ptr () = 25;	/* ENOTTY */
	return r;
}
static int lx_tcgetpgrp (int fd) { int r = tcgetpgrp (fd); return r < 0 ? (int) lx_set_errno_from_korin () : r; }
static int lx_tcsetpgrp (int fd, int g) { return tcsetpgrp (fd, g) < 0 ? (int) lx_set_errno_from_korin () : 0; }
static int lx_ttyname_r (int fd, char *b, size_t n) { int r = ttyname_r (fd, b, n); return r ? linux_errno (r) : 0; }

/* ---- utilisateurs : struct passwd et group de glibc (uid sur 32 bits, pas de pw_comment) ---- */
struct lx_passwd { char *name, *passwd; uint32_t uid, gid; char *gecos, *dir, *shell; };
struct lx_group { char *name, *passwd; uint32_t gid; char **mem; };

static int copy_str (char **dst, const char *s, char **p, char *end)
{
	size_t n = strlen (s ? s : "") + 1;
	if (*p + n > end)
		return -1;
	memcpy (*p, s ? s : "", n);
	*dst = *p;
	*p += n;
	return 0;
}

static int pw_out (struct passwd *k, struct lx_passwd *o, char *buf, size_t n, struct lx_passwd **res)
{
	char *p = buf, *end = buf + n;
	o->uid = k->pw_uid;
	o->gid = k->pw_gid;
	if (copy_str (&o->name, k->pw_name, &p, end) || copy_str (&o->passwd, "x", &p, end) ||
	    copy_str (&o->gecos, k->pw_gecos, &p, end) || copy_str (&o->dir, k->pw_dir, &p, end) ||
	    copy_str (&o->shell, k->pw_shell && *k->pw_shell ? k->pw_shell : "/bin/bash", &p, end))
		return 34;	/* ERANGE */
	*res = o;
	return 0;
}

static int lx_getpwuid_r (uint32_t uid, struct lx_passwd *o, char *buf, size_t n, struct lx_passwd **res)
{
	*res = 0;
	struct passwd *k = getpwuid ((uid_t) uid);
	return k ? pw_out (k, o, buf, n, res) : 0;
}

static int lx_getpwnam_r (const char *name, struct lx_passwd *o, char *buf, size_t n, struct lx_passwd **res)
{
	*res = 0;
	struct passwd *k = getpwnam (name);
	return k ? pw_out (k, o, buf, n, res) : 0;
}

static int gr_out (struct group *k, struct lx_group *o, char *buf, size_t n, struct lx_group **res)
{
	char *p = buf, *end = buf + n;
	o->gid = k->gr_gid;
	if (copy_str (&o->name, k->gr_name, &p, end) || copy_str (&o->passwd, "x", &p, end))
		return 34;
	p = (char *) (((uintptr_t) p + 7) & ~(uintptr_t) 7);
	if (p + sizeof (char *) > end)
		return 34;
	o->mem = (char **) p;
	o->mem[0] = 0;
	*res = o;
	return 0;
}

static int lx_getgrgid_r (uint32_t gid, struct lx_group *o, char *buf, size_t n, struct lx_group **res)
{
	*res = 0;
	struct group *k = getgrgid ((gid_t) gid);
	return k ? gr_out (k, o, buf, n, res) : 0;
}

static int lx_getgrnam_r (const char *name, struct lx_group *o, char *buf, size_t n, struct lx_group **res)
{
	*res = 0;
	struct group *k = getgrnam (name);
	return k ? gr_out (k, o, buf, n, res) : 0;
}

static int lx_initgroups (const char *u, uint32_t g) { (void) u; (void) g; return 0; }

/* ---- posix_spawn : les actions et attributs de glibc gardés à notre façon dans leurs octets (le pointeur
   d'une liste au début), puis posix_spawn de libposix ; un programme de Linux passe par l'hôte ---- */
struct lx_fa { int kind, fd, fd2, flags, mode; char *path; struct lx_fa *next; };
struct lx_fas { struct lx_fa *head, *tail; };
struct lx_sa { short flags; int pgroup; uint64_t sigdef, sigmask; };
#define LX_SA_MAGIC 0x4c585341

static struct lx_fas *fas_of (void *p) { return (struct lx_fas *) p; }

static int lx_fa_init (void *p) { memset (p, 0, 80); return 0; }
static int lx_fa_destroy (void *p)
{
	struct lx_fa *a = fas_of (p)->head;
	while (a)
	{
		struct lx_fa *n = a->next;
		free (a->path);
		free (a);
		a = n;
	}
	memset (p, 0, 80);
	return 0;
}

static int fa_add (void *p, int kind, int fd, int fd2, const char *path, int flags, int mode)
{
	struct lx_fa *a = calloc (1, sizeof *a);
	if (!a)
		return 12;
	a->kind = kind;
	a->fd = fd;
	a->fd2 = fd2;
	a->flags = flags;
	a->mode = mode;
	a->path = path ? strdup (path) : 0;
	struct lx_fas *l = fas_of (p);
	if (l->tail)
		l->tail->next = a;
	else
		l->head = a;
	l->tail = a;
	return 0;
}

static int lx_fa_addclose (void *p, int fd) { return fa_add (p, 0, fd, 0, 0, 0, 0); }
static int lx_fa_adddup2 (void *p, int fd, int fd2) { return fa_add (p, 1, fd, fd2, 0, 0, 0); }
static int lx_fa_addopen (void *p, int fd, const char *path, int fl, int mode) { return fa_add (p, 2, fd, 0, path, fl, mode); }
static int lx_fa_addchdir (void *p, const char *path) { return fa_add (p, 3, 0, 0, path, 0, 0); }

static int lx_sa_init (void *p)
{
	memset (p, 0, 336);
	((int *) p)[83] = LX_SA_MAGIC;
	return 0;
}
static int lx_sa_destroy (void *p) { (void) p; return 0; }
static int lx_sa_setflags (struct lx_sa *a, short f) { a->flags = f; return 0; }
static int lx_sa_setpgroup (struct lx_sa *a, int g) { a->pgroup = g; return 0; }
static int lx_sa_setsigdefault (struct lx_sa *a, const uint64_t *s) { a->sigdef = s[0]; return 0; }
static int lx_sa_setsigmask (struct lx_sa *a, const uint64_t *s) { a->sigmask = s[0]; return 0; }

extern int lx_is_linux_elf (const char *path);

/* le chemin d'un programme par PATH (posix_spawnp, execvp) ; rend name s'il a un « / » */
static const char *search_path (const char *name, char *buf, size_t n)
{
	if (strchr (name, '/'))
		return name;
	const char *path = getenv ("PATH");
	if (!path || !*path)
		path = "/bin:/devtools/bin";
	while (*path)
	{
		const char *c = strchr (path, ':');
		size_t l = c ? (size_t) (c - path) : strlen (path);
		snprintf (buf, n, "%.*s/%s", (int) l, path, name);
		if (access (buf, X_OK) == 0)
			return buf;
		path += l + (c ? 1 : 0);
	}
	return 0;
}

static int do_spawn (int *pid, const char *path, const void *fap, const struct lx_sa *sa, char *const argv[],
		     char *const envp[])
{
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	posix_spawn_file_actions_init (&fa);
	posix_spawnattr_init (&at);
	if (fap)
		for (struct lx_fa *a = ((const struct lx_fas *) fap)->head; a; a = a->next)
			switch (a->kind)
			{
			case 0: posix_spawn_file_actions_addclose (&fa, a->fd); break;
			case 1: posix_spawn_file_actions_adddup2 (&fa, a->fd, a->fd2); break;
			case 2: posix_spawn_file_actions_addopen (&fa, a->fd, a->path, lx_open_flags (a->flags), (mode_t) a->mode); break;
			case 3: posix_spawn_file_actions_addchdir_np (&fa, a->path); break;
			}
	if (sa)
	{
		short fl = 0;
		if (sa->flags & 2) { fl |= POSIX_SPAWN_SETPGROUP; posix_spawnattr_setpgroup (&at, sa->pgroup); }
		if (sa->flags & 4)	/* POSIX_SPAWN_SETSIGDEF de glibc */
		{
			fl |= POSIX_SPAWN_SETSIGDEF;
			sigset_t k = 0;
			for (int s = 1; s <= 64; s++)
				if ((sa->sigdef >> (s - 1)) & 1)
				{
					int ks = lx_sig_korin (s);
					if (ks > 0 && ks < 64)
						k |= (sigset_t) 1 << ks;
				}
			posix_spawnattr_setsigdefault (&at, &k);
		}
		if (sa->flags & 0x80)	/* POSIX_SPAWN_SETSID de glibc */
		{
#ifdef POSIX_SPAWN_SETSID
			fl |= POSIX_SPAWN_SETSID;
#endif
		}
		posix_spawnattr_setflags (&at, fl);
	}
	char **nv = 0;
	const char *prog = path;
	if (lx_is_linux_elf (path))
	{
		int n = 0;
		while (argv && argv[n])
			n++;
		nv = malloc (sizeof (char *) * (size_t) (n + 4));
		if (!nv)
			return 12;
		nv[0] = (char *) "linuxhost";
		nv[1] = (char *) "run";
		nv[2] = (char *) path;
		for (int i = 1; i < n; i++)
			nv[i + 2] = argv[i];
		nv[n > 0 ? n + 2 : 3] = 0;
		prog = linux_host_path;
		argv = nv;
	}
	pid_t p = 0;
	int r = posix_spawn (&p, prog, &fa, &at, argv, envp);
	free (nv);
	posix_spawn_file_actions_destroy (&fa);
	posix_spawnattr_destroy (&at);
	if (r)
		return linux_errno (r);
	if (pid)
		*pid = (int) p;
	return 0;
}

static int lx_posix_spawn (int *pid, const char *path, const void *fa, const struct lx_sa *sa, char *const argv[],
			   char *const envp[])
{
	return do_spawn (pid, path, fa, sa, argv, envp);
}

static int lx_posix_spawnp (int *pid, const char *name, const void *fa, const struct lx_sa *sa, char *const argv[],
			    char *const envp[])
{
	char buf[1024];
	const char *path = search_path (name, buf, sizeof buf);
	if (!path)
		return 2;	/* ENOENT */
	return do_spawn (pid, path, fa, sa, argv, envp);
}

static int lx_pidfd_spawnp (void) { return 38; }	/* ENOSYS : Bun prend posix_spawn */

static int lx_execvp (const char *name, char *const argv[])
{
	extern char **environ;
	char buf[1024];
	const char *path = search_path (name, buf, sizeof buf);
	if (!path)
	{
		*lx_errno_ptr () = 2;
		return -1;
	}
	return (int) lx_ret (linux_syscall (59, (long) path, (long) argv, (long) environ, 0, 0, 0));
}

static int lx_execv (const char *path, char *const argv[])
{
	extern char **environ;
	return (int) lx_ret (linux_syscall (59, (long) path, (long) argv, (long) environ, 0, 0, 0));
}

/* execl, execlp, execle : les arguments variables mis dans un tableau */
#include <stdarg.h>
static int exec_list (int mode, const char *file, const char *a0, va_list ap)
{
	extern char **environ;
	char *args[256];
	int n = 0;
	args[n++] = (char *) a0;
	while (n < 255 && (args[n] = va_arg (ap, char *)) != 0)
		n++;
	args[n] = 0;
	char **env = mode == 2 ? va_arg (ap, char **) : environ;
	if (mode == 1)
		return lx_execvp (file, args);
	return (int) lx_ret (linux_syscall (59, (long) file, (long) args, (long) env, 0, 0, 0));
}
static int lx_execl (const char *p, const char *a0, ...) { va_list ap; va_start (ap, a0); int r = exec_list (0, p, a0, ap); va_end (ap); return r; }
static int lx_execlp (const char *p, const char *a0, ...) { va_list ap; va_start (ap, a0); int r = exec_list (1, p, a0, ap); va_end (ap); return r; }
static int lx_execle (const char *p, const char *a0, ...) { va_list ap; va_start (ap, a0); int r = exec_list (2, p, a0, ap); va_end (ap); return r; }

/* ---- réseau : les mêmes structures (addrinfo, ifaddrs) et codes (EAI_*) ---- */
static int lx_getservbyname_r (const char *n, const char *p, void *r, char *b, size_t l, void **res)
{
	(void) n; (void) p; (void) r; (void) b; (void) l;
	*res = 0;
	return 0;
}
static int lx_getservbyport_r (int port, const char *p, void *r, char *b, size_t l, void **res)
{
	(void) port; (void) p; (void) r; (void) b; (void) l;
	*res = 0;
	return 0;
}

/* /proc/mounts : pas de table */
static void *lx_setmntent (const char *f, const char *m) { (void) f; (void) m; *lx_errno_ptr () = 2; return 0; }
static void *lx_getmntent (void *f) { (void) f; return 0; }
static int lx_endmntent (void *f) { (void) f; return 1; }

static iconv_t lx_iconv_open (const char *to, const char *from)
{
	iconv_t c = iconv_open (to, from);
	if (c == (iconv_t) -1)
		lx_set_errno_from_korin ();
	return c;
}
static size_t lx_iconv (iconv_t c, char **in, size_t *inl, char **out, size_t *outl)
{
	size_t r = iconv (c, in, inl, out, outl);
	if (r == (size_t) -1)
		lx_set_errno_from_korin ();
	return r;
}

#define W(n, f) LX_W (n, f)
const struct lx_entry lx_table_sys[] =
{
	W ("access", lx_access), W ("chdir", lx_chdir), W ("chmod", lx_chmod), W ("chown", lx_chown),
	W ("chroot", lx_chroot), W ("fchdir", lx_fchdir), W ("fchmod", lx_fchmod), W ("fchown", lx_fchown),
	W ("lchown", lx_lchown), W ("link", lx_link), W ("mkdir", lx_mkdir), W ("readlink", lx_readlink),
	W ("rename", lx_rename), W ("rmdir", lx_rmdir), W ("symlink", lx_symlink), W ("truncate", lx_truncate),
	W ("unlink", lx_unlink), W ("umask", lx_umask), W ("fsync", lx_fsync), W ("fdatasync", lx_fdatasync),
	W ("flock", lx_flock), W ("dup", lx_dup), W ("dup2", lx_dup2), W ("dup3", lx_dup3), W ("pipe", lx_pipe),
	W ("pipe2", lx_pipe2), W ("ioctl", lx_ioctl), W ("lseek", lx_lseek), W ("lseek64", lx_lseek),
	W ("pread", lx_pread), W ("pwrite", lx_pwrite), W ("pwrite64", lx_pwrite), W ("writev", lx_writev),
	W ("readv", lx_readv), W ("ftruncate", lx_ftruncate), W ("ftruncate64", lx_ftruncate),
	W ("open", lx_open_at_cwd), W ("open64", lx_open_at_cwd), W ("openat", lx_openat), W ("openat64", lx_openat),
	W ("faccessat", lx_faccessat), W ("fchmodat", lx_fchmodat), W ("linkat", lx_linkat), W ("mkdirat", lx_mkdirat),
	W ("readlinkat", lx_readlinkat), W ("renameat", lx_renameat), W ("symlinkat", lx_symlinkat),
	W ("unlinkat", lx_unlinkat), W ("utimensat", lx_utimensat), W ("futimens", lx_futimens), W ("utimes", lx_utimes),
	W ("statfs", lx_statfs), W ("statfs64", lx_statfs), W ("fstatfs", lx_fstatfs), W ("statvfs", lx_statvfs),
	W ("statvfs64", lx_statvfs), W ("fallocate", lx_fallocate), W ("fallocate64", lx_fallocate),
	W ("sendfile", lx_sendfile), W ("sendfile64", lx_sendfile), W ("splice", lx_splice), W ("mincore", lx_mincore),
	W ("msync", lx_msync), W ("mremap", lx_mremap), W ("getxattr", lx_getxattr), W ("lgetxattr", lx_lgetxattr),
	W ("fgetxattr", lx_fgetxattr), W ("listxattr", lx_listxattr), W ("llistxattr", lx_llistxattr),
	W ("flistxattr", lx_flistxattr), W ("mount", lx_mount), W ("unshare", lx_unshare),
	W ("personality", lx_personality), W ("poll", lx_poll), W ("ppoll", lx_ppoll), W ("select", lx_select),
	W ("__xstat", lx_xstat), W ("__xstat64", lx_xstat), W ("__lxstat", lx_lxstat), W ("__lxstat64", lx_lxstat),
	W ("__fxstat", lx_fxstat), W ("__fxstatat64", lx_fxstatat), W ("__fxstatat", lx_fxstatat),
	W ("stat", lx_stat_), W ("stat64", lx_stat_), W ("lstat", lx_lstat_), W ("lstat64", lx_lstat_),
	W ("fstat", lx_fstat_), W ("fstat64", lx_fstat_), W ("getcwd", lx_getcwd),
	W ("posix_fadvise", lx_posix_fadvise), W ("posix_fadvise64", lx_posix_fadvise),
	W ("posix_madvise", lx_posix_madvise),
	/* process */
	W ("fork", lx_fork), W ("vfork", lx_vfork), W ("execve", lx_execve), W ("execvp", lx_execvp), W ("execv", lx_execv), W ("execl", lx_execl),
	W ("execlp", lx_execlp), W ("execle", lx_execle),
	W ("wait4", lx_wait4), W ("waitid", lx_waitid), W ("waitpid", lx_waitpid), W ("kill", lx_kill),
	W ("raise", lx_raise), W ("getppid", lx_getppid), W ("getpgrp", lx_getpgrp), W ("setpgid", lx_setpgid),
	W ("setsid", lx_setsid), W ("getuid", lx_getuid), W ("getgid", lx_getgid), W ("getegid", lx_getegid),
	W ("setuid", lx_setuid), W ("setgid", lx_setgid), W ("seteuid", lx_seteuid), W ("setegid", lx_setegid),
	W ("getgroups", lx_getgroups), W ("setgroups", lx_setgroups), W ("initgroups", lx_initgroups),
	W ("setrlimit", lx_setrlimit), W ("setrlimit64", lx_setrlimit), W ("getrusage", lx_getrusage),
	W ("alarm", lx_alarm), W ("pause", lx_pause), W ("sigaltstack", lx_sigaltstack),
	W ("sched_setaffinity", lx_sched_setaffinity), W ("process_vm_readv", lx_process_vm_readv),
	W ("timer_create", lx_timer_create), W ("timer_delete", lx_timer_delete), W ("timer_settime", lx_timer_settime),
	W ("clock_nanosleep", lx_clock_nanosleep), W ("pthread_kill", lx_pthread_kill),
	W ("posix_spawn", lx_posix_spawn), W ("posix_spawnp", lx_posix_spawnp), W ("pidfd_spawnp", lx_pidfd_spawnp),
	W ("posix_spawn_file_actions_init", lx_fa_init), W ("posix_spawn_file_actions_destroy", lx_fa_destroy),
	W ("posix_spawn_file_actions_addclose", lx_fa_addclose), W ("posix_spawn_file_actions_adddup2", lx_fa_adddup2),
	W ("posix_spawn_file_actions_addopen", lx_fa_addopen),
	W ("posix_spawn_file_actions_addchdir_np", lx_fa_addchdir),
	W ("posix_spawnattr_init", lx_sa_init), W ("posix_spawnattr_destroy", lx_sa_destroy),
	W ("posix_spawnattr_setflags", lx_sa_setflags), W ("posix_spawnattr_setpgroup", lx_sa_setpgroup),
	W ("posix_spawnattr_setsigdefault", lx_sa_setsigdefault), W ("posix_spawnattr_setsigmask", lx_sa_setsigmask),
	/* signaux */
	W ("sigemptyset", lx_sigemptyset), W ("sigaddset", lx_sigaddset), W ("sigdelset", lx_sigdelset),
	W ("sigismember", lx_sigismember), W ("sigprocmask", lx_sigprocmask), W ("sigsuspend", lx_sigsuspend), W ("pthread_sigmask", lx_pthread_sigmask),
	W ("sigaction", lx_sigaction), W ("__sigaction", lx_sigaction),
	W ("signal", lx_signal), W ("bsd_signal", lx_signal), W ("sysv_signal", lx_sysv_signal),
	W ("__sysv_signal", lx_sysv_signal), W ("siginterrupt", lx_siginterrupt), W ("sighold", lx_sighold),
	W ("sigrelse", lx_sigrelse), W ("sigignore", lx_sigignore), W ("sigset", lx_sigset), W ("sigblock", lx_sigblock),
	W ("sigsetmask", lx_sigsetmask),
	/* répertoires, terminal, utilisateurs */
	W ("opendir", lx_opendir), W ("fdopendir", lx_fdopendir), W ("readdir", lx_readdir), W ("readdir64", lx_readdir),
	W ("closedir", lx_closedir), W ("dirfd", lx_dirfd), W ("rewinddir", lx_rewinddir),
	W ("tcgetattr", lx_tcgetattr), W ("tcsetattr", lx_tcsetattr), LX_F (cfmakeraw), LX_F (cfsetispeed),
	LX_F (cfsetospeed), LX_F (cfgetispeed), LX_F (cfgetospeed), W ("isatty", lx_isatty),
	W ("tcgetpgrp", lx_tcgetpgrp), W ("tcsetpgrp", lx_tcsetpgrp), W ("ttyname_r", lx_ttyname_r),
	W ("getpwuid_r", lx_getpwuid_r), W ("getpwnam_r", lx_getpwnam_r), W ("getgrgid_r", lx_getgrgid_r),
	W ("getgrnam_r", lx_getgrnam_r),
	/* réseau */
	W ("accept", lx_accept), W ("accept4", lx_accept4), W ("bind", lx_bind), W ("connect", lx_connect),
	W ("listen", lx_listen), W ("getpeername", lx_getpeername), W ("getsockname", lx_getsockname),
	W ("getsockopt", lx_getsockopt), W ("setsockopt", lx_setsockopt), W ("recv", lx_recv),
	W ("recvfrom", lx_recvfrom), W ("recvmsg", lx_recvmsg), W ("recvmmsg", lx_recvmmsg), W ("send", lx_send),
	W ("sendto", lx_sendto), W ("sendmsg", lx_sendmsg), W ("sendmmsg", lx_sendmmsg), W ("shutdown", lx_shutdown),
	W ("socket", lx_socket), W ("socketpair", lx_socketpair), LX_F (getaddrinfo), LX_F (freeaddrinfo),
	LX_F (gai_strerror), W ("getservbyname_r", lx_getservbyname_r), W ("getservbyport_r", lx_getservbyport_r),
	LX_F (gethostname), LX_F (if_indextoname), LX_F (if_nametoindex), LX_F (inet_aton), LX_F (inet_ntop),
	LX_F (inet_pton), LX_F (inet_addr), LX_F (inet_ntoa), W ("ntohs", lx_ntohs), W ("htons", lx_ntohs),
	W ("ntohl", lx_ntohl), W ("htonl", lx_ntohl), W ("fcntl", lx_fcntl), W ("fcntl64", lx_fcntl),
	LX_F (getifaddrs), LX_F (freeifaddrs),
	/* epoll, eventfd… */
	W ("epoll_create1", lx_epoll_create1), W ("epoll_create", lx_epoll_create), W ("epoll_ctl", lx_epoll_ctl),
	W ("epoll_wait", lx_epoll_wait), W ("epoll_pwait", lx_epoll_pwait), W ("eventfd", lx_eventfd),
	W ("signalfd", lx_signalfd), W ("inotify_init1", lx_inotify_init1), W ("inotify_add_watch", lx_inotify_add_watch),
	W ("inotify_rm_watch", lx_inotify_rm_watch),
	/* divers */
	W ("setmntent", lx_setmntent), W ("getmntent", lx_getmntent), W ("endmntent", lx_endmntent),
	W ("iconv_open", lx_iconv_open), W ("iconv", lx_iconv), LX_F (iconv_close),
};
const size_t lx_table_sys_n = sizeof lx_table_sys / sizeof lx_table_sys[0];
