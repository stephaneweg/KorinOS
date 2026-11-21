/*
 * syscall2.c -- liblinux : les appels système de Linux x86-64 d'un exécutable (docs/etude/LINUX-HOST.md,
 * jalon 3 : Claude Code, c'est-à-dire Bun), en plus de ceux de syscall.c (les modules, Widevine).
 *
 * linux_syscall (syscall.c) demande d'abord ici (lx_syscall2) : rend 1 quand l'appel est servi. Tout ce qui a
 * une structure ou des constantes de Linux est traduit ici, une seule fois : les enveloppes de glibc
 * (glibc_sys.c) et les syscall directs de Bun (la capture du noyau) arrivent au même endroit.
 *
 * Ce qui est pareil des deux côtés (vérifié le 21/11 sur les en-têtes de libposix) : les sockets (AF_*,
 * SOCK_*, SOL_SOCKET, SO_*, MSG_*, sockaddr, msghdr), epoll (EPOLL*, mais pas la taille de epoll_event :
 * 12 octets sous Linux, 16 ici), poll, wait (WNOHANG…), les ioctl du terminal (TIOC*, FIONREAD, termios :
 * mêmes drapeaux et indices de c_cc), DT_*, S_IF*, l'ucontext du noyau du PC (gregs de Linux à +40) et le
 * début de siginfo (signo +0, code +8, adresse +16). Ce qui diffère : les numéros des signaux (newlib : à la
 * BSD), sigset_t (8 octets ici, 128 sous Linux), SIG_BLOCK/SIG_SETMASK, AT_*, UTIME_*, F_DUPFD_CLOEXEC et
 * les verrous, struct stat/statx/statfs/dirent64.
 *
 * Fabriqués : eventfd (une paire de sockets locales et un compteur), getdents64 (fdopendir), /proc/self/exe.
 * Pas encore : inotify, timerfd, signalfd, pidfd, clone3 (ENOSYS : Bun a des replis).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "linux.h"
#include "glibc_internal.h"
#include <armpi/syscall.h>
#include "korin_sys_nr.h"

const char *linux_exe_path;
const char *linux_host_path = "/bin/linuxhost";

static long fail (void)
{
	return -linux_errno (errno);
}

/* un signal à un thread du process (son tid de Korin, celui que rend gettid) */
static long armpi_syscall_sig_send (long tid, int ks)
{
	long r = armpi_syscall (SYS_SIG_SEND, 0, tid, ks, 0, 0, 0);
	return r < 0 ? -3 : 0;	/* ESRCH */
}

#define RET(x) do { long _r = (long) (x); *ret = _r < 0 ? fail () : _r; return 1; } while (0)
#define VAL(x) do { *ret = (long) (x); return 1; } while (0)

/* ---- signaux : numéros ---- */
static const signed char s_l2k[65] = {
	0, SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2,
	SIGPIPE, SIGALRM, SIGTERM, 0 /* STKFLT */, SIGCHLD, SIGCONT, SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU, SIGURG,
	SIGXCPU, SIGXFSZ, SIGVTALRM, SIGPROF, SIGWINCH, SIGIO, 0 /* PWR */, SIGSYS,
	/* 32, 33 : réservés à NPTL ; 34.. 63 : temps réel, les mêmes numéros ; 64 : aucun */
	0, 0, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, 62, 63, 0,
};

int lx_sig_korin (int l)
{
	return l > 0 && l <= 64 ? s_l2k[l] : 0;
}

int lx_sig_linux (int k)
{
	for (int l = 1; l <= 64; l++)
		if (s_l2k[l] == k)
			return l;
	return 0;
}

/* sigset_t de Linux (bit n-1 : signal n, 64 bits suffisent) <-> celui de newlib (bit n) */
static sigset_t set_korin (const uint64_t *l)
{
	sigset_t k = 0;
	for (int s = 1; s <= 64; s++)
		if ((*l >> (s - 1)) & 1)
		{
			int ks = lx_sig_korin (s);
			if (ks > 0 && ks < 64)
				k |= (sigset_t) 1 << ks;
		}
	return k;
}

static uint64_t set_linux (sigset_t k)
{
	uint64_t l = 0;
	for (int ks = 1; ks < 64; ks++)
		if ((k >> ks) & 1)
		{
			int s = lx_sig_linux (ks);
			if (s)
				l |= 1ULL << (s - 1);
		}
	return l;
}

/* ---- les actions des signaux : le gestionnaire de Linux appelé par un tremplin de Korin ---- */
#define LSA_SIGINFO	4
#define LSA_ONSTACK	0x08000000
#define LSA_RESTART	0x10000000
#define LSA_NODEFER	0x40000000
#define LSA_RESETHAND	0x80000000u
#define LSA_NOCLDSTOP	1

struct lx_kact { uint64_t handler, flags, restorer, mask; };	/* struct sigaction du noyau de Linux */
static struct lx_kact s_act[65];

/* LINUX_TRACE_SIG=1 : chaque signal livré au code de Linux, avec l'adresse de la faute, rip, rsp et la chaîne
   des rbp (le code de Bun garde les pointeurs de cadre) ; LINUX_TRACE_SIG=2 : seulement les fautes */
static void trace_signal (int s, const siginfo_t *ksi, const void *uc)
{
	static int mode = -1;
	if (mode < 0)
	{
		const char *e = getenv ("LINUX_TRACE_SIG");
		mode = e ? atoi (e) : 0;
	}
	if (!mode || (mode == 2 && s != 11 && s != 7 && s != 4 && s != 8))
		return;
	const long long *g = (const long long *) ((const char *) uc + 40);
	char line[512];
	int n = snprintf (line, sizeof line, "liblinux : signal %d, adresse %p, rip %#llx, rsp %#llx, rbp %#llx, rax %#llx,"
			  " rdi %#llx, rsi %#llx, cadres :", s, ksi ? *(void *const *) ((const char *) ksi + 16) : 0,
			  g[16], g[15], g[10], g[13], g[8], g[9]);
	const unsigned long long *fp = (const unsigned long long *) (uintptr_t) g[10];
	for (int i = 0; i < 12 && fp && ((uintptr_t) fp & 7) == 0 && (uintptr_t) fp > 0x10000 && n < (int) sizeof line - 24; i++)
	{
		n += snprintf (line + n, sizeof line - (size_t) n, " %#llx", fp[1]);
		fp = (const unsigned long long *) (uintptr_t) fp[0];
	}
	snprintf (line + n, sizeof line - (size_t) n, "\n");
	write (2, line, strlen (line));
}

static void trampoline (int ks, siginfo_t *ksi, void *uc)
{
	int s = lx_sig_linux (ks);
	trace_signal (s, ksi, uc);
	struct lx_kact *a = &s_act[s];
	uint64_t h = a->handler;
	if (h <= 1)
		return;
	if (a->flags & LSA_SIGINFO)
	{
		/* siginfo de Linux : celui du noyau du PC (même disposition), le numéro traduit */
		unsigned char si[128];
		memset (si, 0, sizeof si);
		if (ksi)
			memcpy (si, ksi, sizeof si);
		*(int *) si = s;
		*(int *) (si + 4) = 0;
		((void (*) (int, void *, void *)) h) (s, si, uc);
	}
	else
		((void (*) (int)) h) (s);
}

static long do_sigaction (long s, const struct lx_kact *act, struct lx_kact *old)
{
	if (s < 1 || s > 64)
		return -22;
	int ks = lx_sig_korin ((int) s);
	if (old)
		*old = s_act[s];
	if (!act)
		return 0;
	if (s == 9 || s == 19)
		return -22;
	s_act[s] = *act;
	if (!ks)
		return 0;	/* (STKFLT, PWR, 32, 33, 64 : gardés, jamais levés) */
	struct sigaction k;
	memset (&k, 0, sizeof k);
	if (act->handler <= 1)
		k.sa_handler = act->handler ? SIG_IGN : SIG_DFL;
	else
	{
		k.sa_sigaction = trampoline;
		k.sa_flags = SA_SIGINFO;
	}
	if (act->flags & LSA_ONSTACK) k.sa_flags |= SA_ONSTACK;
	if (act->flags & LSA_RESTART) k.sa_flags |= SA_RESTART;
	if (act->flags & LSA_NODEFER) k.sa_flags |= SA_NODEFER;
	if (act->flags & LSA_RESETHAND) k.sa_flags |= SA_RESETHAND;
	if (act->flags & LSA_NOCLDSTOP) k.sa_flags |= SA_NOCLDSTOP;
	k.sa_mask = set_korin (&act->mask);
	return sigaction (ks, &k, 0) < 0 ? fail () : 0;
}

static int how_korin (long how)
{
	return how == 0 ? SIG_BLOCK : how == 1 ? SIG_UNBLOCK : how == 2 ? SIG_SETMASK : -1;
}

static long do_sigprocmask (long how, const uint64_t *set, uint64_t *old, long size)
{
	sigset_t k = 0, ko = 0;
	if (set)
	{
		int h = how_korin (how);
		if (h < 0)
			return -22;
		k = set_korin (set);
		if (pthread_sigmask (h, &k, &ko) != 0)
			return -22;
	}
	else
		pthread_sigmask (SIG_BLOCK, 0, &ko);
	if (old)
	{
		memset (old, 0, (size_t) (size > 0 && size <= 128 ? size : 8));
		*old = set_linux (ko);
	}
	return 0;
}

/* ---- eventfd : une paire de sockets locales (le noyau sait y attendre : poll, epoll) et un compteur ;
   lisible tant que le compteur n'est pas nul (un octet en attente sur la paire) ---- */
#define MAX_FD 128
static struct efd
{
	int used, peer, sema, nonblock;
	uint64_t count;
	pthread_mutex_t lock;
} s_efd[MAX_FD];

static long efd_create (long init, long flags)
{
	int sv[2];
	if (socketpair (AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return fail ();
	if (sv[0] >= MAX_FD)
	{
		close (sv[0]);
		close (sv[1]);
		return -24;
	}
	struct efd *e = &s_efd[sv[0]];
	pthread_mutex_init (&e->lock, 0);
	e->peer = sv[1];
	e->sema = (flags & 1) != 0;		/* EFD_SEMAPHORE */
	e->nonblock = (flags & 0x800) != 0;	/* EFD_NONBLOCK */
	e->count = (uint64_t) init;
	fcntl (sv[0], F_SETFL, O_NONBLOCK);	/* (la lecture de l'octet : jamais bloquante ; l'attente : poll) */
	if (flags & 0x80000)
		fcntl (sv[0], F_SETFD, FD_CLOEXEC);
	fcntl (sv[1], F_SETFD, FD_CLOEXEC);
	if (init)
		write (sv[1], "", 1);
	e->used = 1;
	return sv[0];
}

static int is_efd (long fd)
{
	return fd >= 0 && fd < MAX_FD && s_efd[fd].used;
}

static long efd_read (int fd, void *buf, size_t n)
{
	struct efd *e = &s_efd[fd];
	if (n < 8)
		return -22;
	for (;;)
	{
		pthread_mutex_lock (&e->lock);
		if (e->count)
		{
			uint64_t v = e->sema ? 1 : e->count;
			e->count -= v;
			if (!e->count)
			{
				char drain[16];
				while (read (fd, drain, sizeof drain) > 0)
					;
			}
			pthread_mutex_unlock (&e->lock);
			memcpy (buf, &v, 8);
			return 8;
		}
		pthread_mutex_unlock (&e->lock);
		if (e->nonblock)
			return -11;	/* EAGAIN */
		struct pollfd p = { fd, POLLIN, 0 };
		poll (&p, 1, -1);
	}
}

static long efd_write (int fd, const void *buf, size_t n)
{
	struct efd *e = &s_efd[fd];
	uint64_t v;
	if (n < 8)
		return -22;
	memcpy (&v, buf, 8);
	if (v == ~0ULL)
		return -22;
	pthread_mutex_lock (&e->lock);
	int was = e->count == 0;
	e->count += v;
	if (was && v)
		write (e->peer, "", 1);
	pthread_mutex_unlock (&e->lock);
	return 8;
}

static void efd_close (int fd)
{
	struct efd *e = &s_efd[fd];
	e->used = 0;
	close (e->peer);
	pthread_mutex_destroy (&e->lock);
}

/* ---- répertoires : getdents64 par un DIR de libposix tenu par descripteur ---- */
static struct ldir
{
	DIR *d;
	int have;		/* une entrée lue qui n'a pas tenu dans le tampon précédent */
	struct dirent ent;
} s_dir[MAX_FD];

static long do_getdents64 (int fd, unsigned char *buf, size_t n)
{
	if (fd < 0 || fd >= MAX_FD)
		return -9;
	struct ldir *ld = &s_dir[fd];
	if (!ld->d)
	{
		int dup_fd = dup (fd);	/* (closedir fermera la copie ; fd reste à l'appelant) */
		if (dup_fd < 0)
			return fail ();
		ld->d = fdopendir (dup_fd);
		if (!ld->d)
		{
			close (dup_fd);
			return -20;	/* ENOTDIR */
		}
	}
	size_t used = 0;
	for (;;)
	{
		if (!ld->have)
		{
			errno = 0;
			struct dirent *de = readdir (ld->d);
			if (!de)
				break;
			ld->ent = *de;
			ld->have = 1;
		}
		size_t nl = strlen (ld->ent.d_name);
		size_t rec = (19 + nl + 1 + 7) & ~(size_t) 7;
		if (used + rec > n)
		{
			if (!used)
				return -22;
			break;
		}
		unsigned char *p = buf + used;
		uint64_t ino = (uint64_t) ld->ent.d_ino ? (uint64_t) ld->ent.d_ino : (uint64_t) (used + 1);
		int64_t off = (int64_t) ld->ent.d_off;
		memcpy (p, &ino, 8);
		memcpy (p + 8, &off, 8);
		uint16_t r16 = (uint16_t) rec;
		memcpy (p + 16, &r16, 2);
		p[18] = ld->ent.d_type;	/* (DT_* : les mêmes) */
		memcpy (p + 19, ld->ent.d_name, nl + 1);
		memset (p + 19 + nl + 1, 0, rec - 19 - nl - 1);
		used += rec;
		ld->have = 0;
	}
	return (long) used;
}

static void dir_forget (int fd)
{
	if (fd >= 0 && fd < MAX_FD && s_dir[fd].d)
	{
		closedir (s_dir[fd].d);
		s_dir[fd].d = 0;
		s_dir[fd].have = 0;
	}
}

/* ---- /proc : ce que lisent Bun et Node (process.memoryUsage : /proc/self/statm ; os.cpus : /proc/cpuinfo et
   /proc/stat ; os.totalmem, os.freemem : /proc/meminfo ; os.uptime, os.loadavg) ---- */
#include <malloc.h>

static long pages_rss (void)
{
	struct mallinfo mi = mallinfo ();
	long heap = (long) mi.arena + (long) mi.hblkhd;
	return 16384 + heap / 4096;	/* (l'image de l'exécutable, à peu près, et le tas de libposix) */
}

char *lx_proc_more (const char *path, size_t *len)
{
	const char *p = path;
	if (!strncmp (p, "/proc/self/", 11))
		p += 11;
	else if (!strncmp (p, "/proc/thread-self/", 18))
		p += 18;
	else if (!strncmp (p, "/proc/", 6))
	{
		char *end;
		long pid = strtol (p + 6, &end, 10);
		if (end != p + 6 && *end == '/' && pid == getpid ())
			p = end + 1;
	}
	char *b = malloc (8192);
	if (!b)
		return 0;
	int n = -1;
	long rss = pages_rss (), ncpu = sysconf (_SC_NPROCESSORS_ONLN);
	long tot = sysconf (_SC_PHYS_PAGES), av = sysconf (_SC_AVPHYS_PAGES);
	if (ncpu < 1) ncpu = 1;
	if (tot <= 0) tot = 1 << 20;
	if (av <= 0) av = tot / 2;
	struct timespec up;
	clock_gettime (CLOCK_MONOTONIC, &up);
	if (!strcmp (p, "statm"))
		n = snprintf (b, 8192, "%ld %ld %ld %ld 0 %ld 0\n", rss * 4, rss, rss / 8, rss / 2, rss / 2);
	else if (!strcmp (p, "stat") && p != path)
		n = snprintf (b, 8192, "%d (claude) R %d %d %d 0 -1 4194560 0 0 0 0 %ld 0 0 0 20 0 1 0 1 %ld %ld "
			      "18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
			      getpid (), getppid (), getpgrp (), getpgrp (), (long) up.tv_sec * 100, rss * 4 * 4096, rss);
	else if (!strcmp (p, "status"))
		n = snprintf (b, 8192, "Name:\tclaude\nState:\tR (running)\nTgid:\t%d\nPid:\t%d\nPPid:\t%d\n"
			      "Uid:\t%d\t%d\t%d\t%d\nGid:\t%d\t%d\t%d\t%d\nVmSize:\t%ld kB\nVmRSS:\t%ld kB\nThreads:\t1\n",
			      getpid (), getpid (), getppid (), getuid (), getuid (), getuid (), getuid (), getgid (), getgid (),
			      getgid (), getgid (), rss * 16, rss * 4);
	else if (!strcmp (path, "/proc/meminfo"))
		n = snprintf (b, 8192, "MemTotal:       %ld kB\nMemFree:        %ld kB\nMemAvailable:   %ld kB\n"
			      "Buffers:               0 kB\nCached:                0 kB\nSwapTotal:             0 kB\n"
			      "SwapFree:              0 kB\n", tot * 4, av * 4, av * 4);
	else if (!strcmp (path, "/proc/cpuinfo"))
	{
		n = 0;
		for (long i = 0; i < ncpu && n < 7000; i++)
			n += snprintf (b + n, 8192 - (size_t) n, "processor\t: %ld\nvendor_id\t: GenuineIntel\n"
				       "model name\t: x86-64 (Korin)\ncpu MHz\t\t: 2000.000\nflags\t\t: fpu sse sse2 ssse3 sse4_1"
				       " sse4_2 popcnt avx avx2\n\n", i);
	}
	else if (!strcmp (path, "/proc/stat"))
	{
		long t = (long) up.tv_sec * 100;
		n = snprintf (b, 8192, "cpu  %ld 0 %ld %ld 0 0 0 0 0 0\n", t / 10, t / 20, t * ncpu - t / 10 - t / 20);
		for (long i = 0; i < ncpu && n < 7000; i++)
			n += snprintf (b + n, 8192 - (size_t) n, "cpu%ld %ld 0 %ld %ld 0 0 0 0 0 0\n", i, t / 10 / ncpu,
				       t / 20 / ncpu, t - t / 10 / ncpu - t / 20 / ncpu);
		n += snprintf (b + n, 8192 - (size_t) n, "btime %ld\nprocesses 1\nprocs_running 1\n",
			       (long) time (0) - (long) up.tv_sec);
	}
	else if (!strcmp (path, "/proc/uptime"))
		n = snprintf (b, 8192, "%ld.%02ld %ld.00\n", (long) up.tv_sec, up.tv_nsec / 10000000, (long) up.tv_sec);
	else if (!strcmp (path, "/proc/loadavg"))
		n = snprintf (b, 8192, "0.10 0.10 0.10 1/1 %d\n", getpid ());
	else if (!strcmp (path, "/proc/version"))
		n = snprintf (b, 8192, "Linux version 6.8.0-korin (korin@korin) (gcc 14.2) #1 SMP PREEMPT_DYNAMIC\n");
	else if (!strcmp (path, "/proc/sys/kernel/osrelease"))
		n = snprintf (b, 8192, "6.8.0-korin\n");
	if (n < 0)
	{
		free (b);
		return 0;
	}
	*len = (size_t) n;
	return b;
}

/* ---- chemins ---- */
static int at_fd (long d)
{
	return d == -100 ? AT_FDCWD : (int) d;
}

static int at_flags (long f)
{
	int r = 0;
	if (f & 0x100) r |= AT_SYMLINK_NOFOLLOW;
	if (f & 0x400) r |= AT_SYMLINK_FOLLOW;
	if (f & 0x1000) r |= AT_EMPTY_PATH;
	return r;
}

/* /proc/self/exe et /proc/self/fd/N (readlink) */
static long proc_link (const char *path, char *buf, size_t n)
{
	const char *v = 0;
	char tmp[64];
	if (!strcmp (path, "/proc/self/exe") || !strcmp (path, "/proc/thread-self/exe"))
		v = linux_exe_path ? linux_exe_path : "/bin/linuxhost";
	else if (!strncmp (path, "/proc/self/fd/", 14) || !strncmp (path, "/proc/thread-self/fd/", 21))
	{
		/* le chemin d'un fichier ou d'un dossier ouvert (Bun : open, puis ce readlink, pour le vrai chemin ; un
		   « /dev/fd/N » ici faisait poser a Claude Code ses verrous en /dev/fd/N.lock, refuses, puis attendre) */
		extern char *__posix_at_path (int dirfd, const char *name, char *buf, size_t cap);
		extern char *__posix_fd_path (int fd, char *buf, size_t cap);
		int fd = atoi (strrchr (path, '/') + 1);
		static __thread char dir[1024];
		if (__posix_fd_path (fd, dir, sizeof dir) || __posix_at_path (fd, "", dir, sizeof dir))
		{
			size_t l = strlen (dir);
			while (l > 1 && dir[l - 1] == '/')
				dir[--l] = 0;
			v = dir;
		}
		else
		{
			snprintf (tmp, sizeof tmp, "/dev/fd/%d", fd);
			v = tmp;
		}
	}
	if (!v)
		return -2;
	size_t l = strlen (v);
	if (l > n)
		l = n;
	memcpy (buf, v, l);
	return (long) l;
}

static long do_openat (long dfd, const char *path, long flags, long mode)
{
	if (!path)
		return -14;
	if (!strncmp (path, "/proc/", 6))
		return lx_synth_open (path);
	int kf = lx_open_flags (flags & ~0x200000L);	/* (O_PATH : une ouverture en lecture) */
	int fd = (path[0] == '/' || dfd == -100) ? open (path, kf, (int) mode) : openat (at_fd (dfd), path, kf, (int) mode);
	return fd < 0 ? fail () : fd;
}

static long do_fstatat (long dfd, const char *path, void *out, long flags)
{
	struct stat st;
	int r;
	if (path && !path[0] && (flags & 0x1000))
		r = fstat ((int) dfd, &st);
	else if (!path)
		return -14;
	else if (!strncmp (path, "/proc/", 6))
	{
		long fd = lx_synth_open (path);
		if (fd < 0)
			return fd;
		linux_syscall (5, fd, (long) out, 0, 0, 0, 0);
		linux_syscall (3, fd, 0, 0, 0, 0, 0);
		return 0;
	}
	else
		r = fstatat (at_fd (dfd), path, &st, at_flags (flags) & ~AT_EMPTY_PATH);
	if (r < 0)
		return fail ();
	lx_stat_out (out, &st);
	return 0;
}

/* struct statx de Linux (256 octets) */
static long do_statx (long dfd, const char *path, long flags, long mask, unsigned char *out)
{
	(void) mask;
	unsigned char ls[144];
	long r = do_fstatat (dfd, path, ls, flags);
	if (r < 0)
		return r;
	const uint64_t *q = (const uint64_t *) ls;
	const uint32_t *w = (const uint32_t *) (q + 3);
	memset (out, 0, 256);
	uint32_t u32;
	uint16_t u16;
	u32 = 0x7ff; memcpy (out + 0, &u32, 4);			/* STATX_BASIC_STATS */
	u32 = (uint32_t) q[7]; memcpy (out + 4, &u32, 4);		/* blksize */
	u32 = (uint32_t) q[2]; memcpy (out + 16, &u32, 4);		/* nlink */
	memcpy (out + 20, &w[1], 4);					/* uid */
	memcpy (out + 24, &w[2], 4);					/* gid */
	u16 = (uint16_t) w[0]; memcpy (out + 28, &u16, 2);		/* mode */
	memcpy (out + 32, &q[1], 8);					/* ino */
	memcpy (out + 40, &q[6], 8);					/* size */
	memcpy (out + 48, &q[8], 8);					/* blocks */
	memcpy (out + 64, &q[9], 8);					/* atime */
	memcpy (out + 80, &q[13], 8);					/* btime : ctime */
	memcpy (out + 96, &q[13], 8);					/* ctime */
	memcpy (out + 112, &q[11], 8);					/* mtime */
	return 0;
}

/* struct statfs de Linux (120 octets) */
static long do_statfs (const char *path, int fd, uint64_t *out)
{
	struct statvfs v;
	int r = path ? statvfs (path, &v) : fstatvfs (fd, &v);
	if (r < 0)
		return fail ();
	memset (out, 0, 120);
	out[0] = 0x4d44;	/* MSDOS_SUPER_MAGIC (FAT) */
	out[1] = v.f_bsize;
	out[2] = v.f_blocks;
	out[3] = v.f_bfree;
	out[4] = v.f_bavail;
	out[5] = v.f_files;
	out[6] = v.f_ffree;
	out[8] = 255;		/* namelen */
	out[9] = v.f_frsize ? v.f_frsize : v.f_bsize;
	return 0;
}

static void ts_korin (struct timespec *k, const int64_t *l)
{
	k->tv_sec = (time_t) l[0];
	k->tv_nsec = l[1] == 0x3fffffff ? UTIME_NOW : l[1] == 0x3ffffffe ? UTIME_OMIT : (long) l[1];
}

static long do_utimensat (long dfd, const char *path, const int64_t *t, long flags)
{
	struct timespec k[2], *kp = 0;
	if (t)
	{
		ts_korin (&k[0], t);
		ts_korin (&k[1], t + 2);
		kp = k;
	}
	int r = path ? utimensat (at_fd (dfd), path, kp, at_flags (flags)) : futimens ((int) dfd, kp);
	return r < 0 ? fail () : 0;
}

/* ---- terminal ---- */
static long do_ioctl (int fd, unsigned long req, long arg)
{
	switch (req)
	{
	case 0x5401:	/* TCGETS : la struct termios du noyau de Linux (36 octets : jusqu'à c_cc[19]) */
	{
		struct termios t;
		if (tcgetattr (fd, &t) < 0)
			return fail ();
		memcpy ((void *) arg, &t, 36);
		return 0;
	}
	case 0x5402: case 0x5403: case 0x5404:	/* TCSETS, TCSETSW, TCSETSF */
	{
		struct termios t;
		if (tcgetattr (fd, &t) < 0)
			return fail ();
		memcpy (&t, (const void *) arg, 36);
		int how = req == 0x5402 ? TCSANOW : req == 0x5403 ? TCSADRAIN : TCSAFLUSH;
		return tcsetattr (fd, how, &t) < 0 ? fail () : 0;
	}
	case 0x5413: case 0x5414: case 0x540F: case 0x5410: case 0x541B: case 0x540E: case 0x5422:
		/* TIOCGWINSZ, TIOCSWINSZ, TIOCGPGRP, TIOCSPGRP, FIONREAD, TIOCSCTTY, TIOCNOTTY : les mêmes */
		return ioctl (fd, req, (void *) arg) < 0 ? fail () : 0;
	case 0x5421:	/* FIONBIO */
	{
		int fl = fcntl (fd, F_GETFL);
		if (fl < 0)
			return fail ();
		return fcntl (fd, F_SETFL, *(int *) arg ? fl | O_NONBLOCK : fl & ~O_NONBLOCK) < 0 ? fail () : 0;
	}
	case 0x5451: return fcntl (fd, F_SETFD, FD_CLOEXEC) < 0 ? fail () : 0;	/* FIOCLEX */
	case 0x5450: return fcntl (fd, F_SETFD, 0) < 0 ? fail () : 0;		/* FIONCLEX */
	}
	return -25;	/* ENOTTY */
}

static long do_fcntl (int fd, long cmd, long arg)
{
	int r;
	switch (cmd)
	{
	case 0: r = fcntl (fd, F_DUPFD, (int) arg); break;
	case 1030: r = fcntl (fd, F_DUPFD_CLOEXEC, (int) arg); break;
	case 1: r = fcntl (fd, F_GETFD); break;
	case 2: r = fcntl (fd, F_SETFD, (arg & 1) ? FD_CLOEXEC : 0); break;
	case 3:
		r = fcntl (fd, F_GETFL);
		if (r >= 0)
			r = (r & O_ACCMODE) | ((r & O_NONBLOCK) ? 0x800 : 0) | ((r & O_APPEND) ? 0x400 : 0);
		break;
	case 4:
	{
		int cur = fcntl (fd, F_GETFL);
		if (cur < 0)
			return fail ();
		cur &= ~(O_NONBLOCK | O_APPEND);
		r = fcntl (fd, F_SETFL, cur | ((arg & 0x800) ? O_NONBLOCK : 0) | ((arg & 0x400) ? O_APPEND : 0));
		break;
	}
	case 5: case 6: case 7: case 36: case 37: case 38:	/* F_GETLK, F_SETLK(W), F_OFD_* : accordés */
		if (cmd == 5 || cmd == 36)
			*(short *) arg = 2;	/* F_UNLCK */
		return 0;
	case 1031: return arg;		/* F_SETPIPE_SZ */
	case 1032: return 65536;	/* F_GETPIPE_SZ */
	case 1033: case 1034:		/* F_ADD_SEALS, F_GET_SEALS : les mêmes */
		r = fcntl (fd, (int) cmd, (int) arg);
		break;
	default:
		return -22;
	}
	return r < 0 ? fail () : r;
}

/* ---- select par poll (fd_set de Linux : 1024 bits) ---- */
static long do_select (int n, uint64_t *rd, uint64_t *wr, uint64_t *ex, int ms)
{
	if (n < 0 || n > 1024)
		return -22;
	struct pollfd p[128];
	int k = 0;
	for (int fd = 0; fd < n && k < 128; fd++)
	{
		short ev = 0;
		if (rd && (rd[fd / 64] >> (fd % 64)) & 1) ev |= POLLIN;
		if (wr && (wr[fd / 64] >> (fd % 64)) & 1) ev |= POLLOUT;
		if (ex && (ex[fd / 64] >> (fd % 64)) & 1) ev |= POLLPRI;
		if (ev)
			p[k++] = (struct pollfd) { fd, ev, 0 };
	}
	int r = poll (p, (nfds_t) k, ms);
	if (r < 0)
		return fail ();
	size_t words = (size_t) (n + 63) / 64;
	if (rd) memset (rd, 0, words * 8);
	if (wr) memset (wr, 0, words * 8);
	if (ex) memset (ex, 0, words * 8);
	int cnt = 0;
	for (int i = 0; i < k; i++)
	{
		int fd = p[i].fd;
		short re = p[i].revents;
		if (rd && (re & (POLLIN | POLLHUP | POLLERR))) { rd[fd / 64] |= 1ULL << (fd % 64); cnt++; }
		if (wr && (re & (POLLOUT | POLLERR))) { wr[fd / 64] |= 1ULL << (fd % 64); cnt++; }
		if (ex && (re & POLLPRI)) { ex[fd / 64] |= 1ULL << (fd % 64); cnt++; }
	}
	return cnt;
}

static int ms_of (const struct timespec *t)
{
	if (!t)
		return -1;
	long long ms = (long long) t->tv_sec * 1000 + (t->tv_nsec + 999999) / 1000000;
	return ms > 0x7fffffff ? 0x7fffffff : (int) ms;
}

/* ---- epoll : epoll_event de Linux, 12 octets (packed) ---- */
struct __attribute__ ((packed)) lx_epev { uint32_t events; uint64_t data; };

static long do_epoll_ctl (int ep, int op, int fd, const struct lx_epev *ev)
{
	struct epoll_event k;
	memset (&k, 0, sizeof k);
	if (ev)
	{
		k.events = ev->events;
		k.data.u64 = ev->data;
	}
	return epoll_ctl (ep, op, fd, ev ? &k : 0) < 0 ? fail () : 0;
}

static long do_epoll_wait (int ep, struct lx_epev *out, int max, int ms)
{
	if (max <= 0)
		return -22;
	if (max > 256)
		max = 256;
	struct epoll_event k[256];
	int r = epoll_wait (ep, k, max, ms);
	if (r < 0)
		return fail ();
	for (int i = 0; i < r; i++)
	{
		out[i].events = k[i].events;
		out[i].data = k[i].data.u64;
	}
	return r;
}

/* ---- process ---- */
static int wstatus_linux (int st)
{
	/* (le codage de wait est celui de Linux ; seul le numéro du signal change) */
	if ((st & 0x7f) != 0 && (st & 0x7f) != 0x7f)
		return (st & ~0x7f) | (lx_sig_linux (st & 0x7f) & 0x7f);
	if ((st & 0xff) == 0x7f)
		return (st & 0xff) | (lx_sig_linux ((st >> 8) & 0xff) << 8);
	return st;
}

static long do_wait4 (long pid, int *status, long options, void *ru)
{
	int st = 0;
	pid_t r = waitpid ((pid_t) pid, &st, (int) (options & (WNOHANG | WUNTRACED | WCONTINUED)));
	if (r < 0)
		return fail ();
	if (status)
		*status = wstatus_linux (st);
	if (ru)
		memset (ru, 0, 144);
	return r;
}

static long do_waitid (long idtype, long id, unsigned char *si, long options)
{
	pid_t want = idtype == 0 ? -1 : idtype == 1 ? (pid_t) id : idtype == 2 ? -(pid_t) id : 0;
	if (!want && idtype != 0)
		return -22;	/* (P_PIDFD : pas de pidfd) */
	int st = 0;
	pid_t r = waitpid (want, &st, (options & 1) ? WNOHANG : 0);
	if (r < 0)
		return fail ();
	if (si)
	{
		memset (si, 0, 128);
		if (r > 0)
		{
			int *w = (int *) si;
			w[0] = 17;	/* SIGCHLD */
			if (WIFEXITED (st)) { w[2] = 1; w[6] = WEXITSTATUS (st); }		/* CLD_EXITED */
			else if (WIFSIGNALED (st)) { w[2] = 2; w[6] = lx_sig_linux (WTERMSIG (st)); }	/* CLD_KILLED */
			else { w[2] = 5; w[6] = lx_sig_linux (WSTOPSIG (st)); }		/* CLD_STOPPED */
			w[4] = r;	/* si_pid */
			w[5] = (int) getuid ();
		}
	}
	return 0;
}

/* un exécutable de Linux (PT_INTERP, ou une note GNU d'ABI) ? Les programmes de Korin n'ont ni l'un ni l'autre */
int lx_is_linux_elf (const char *path)
{
	unsigned char h[4096];
	int fd = open (path, O_RDONLY);
	if (fd < 0)
		return 0;
	ssize_t n = read (fd, h, sizeof h);
	close (fd);
	if (n < 64 || memcmp (h, "\177ELF", 4) || h[4] != 2 || *(uint16_t *) (h + 18) != 62)
		return 0;
	uint64_t phoff = *(uint64_t *) (h + 32);
	uint16_t phnum = *(uint16_t *) (h + 56);
	for (int i = 0; i < phnum && phoff + (uint64_t) (i + 1) * 56 <= (uint64_t) n; i++)
	{
		const unsigned char *p = h + phoff + (uint64_t) i * 56;
		uint32_t type = *(const uint32_t *) p;
		if (type == 3)	/* PT_INTERP */
			return 1;
		if (type == 4)	/* PT_NOTE : « GNU », NT_GNU_ABI_TAG */
		{
			uint64_t off = *(const uint64_t *) (p + 8);
			if (off + 16 <= (uint64_t) n && *(const uint32_t *) (h + off + 8) == 1 && !memcmp (h + off + 12, "GNU", 4))
				return 1;
		}
	}
	return 0;
}

/* execve : un programme de Linux passe par l'hôte (linuxhost run CHEMIN ARG…), un programme de Korin
   (bash, git, rg…) est lancé tel quel */
static long do_execve (const char *path, char *const *argv, char *const *envp)
{
	if (!path)
		return -14;
	if (lx_is_linux_elf (path))
	{
		int n = 0;
		while (argv && argv[n])
			n++;
		char **nv = malloc (sizeof (char *) * (size_t) (n + 4));
		if (!nv)
			return -12;
		nv[0] = (char *) "linuxhost";
		nv[1] = (char *) "run";
		nv[2] = (char *) path;
		for (int i = 1; i < n; i++)
			nv[i + 2] = argv[i];
		nv[n > 0 ? n + 2 : 3] = 0;
		execve (linux_host_path, nv, envp);
		free (nv);
		return fail ();
	}
	execve (path, argv, envp);
	return fail ();
}

/* ---- mémoire ---- */
static long do_mremap (void *old, size_t olen, size_t nlen, long flags)
{
	olen = (olen + 4095) & ~(size_t) 4095;
	nlen = (nlen + 4095) & ~(size_t) 4095;
	if (nlen <= olen)
	{
		if (nlen < olen)
			munmap ((char *) old + nlen, olen - nlen);
		return (long) old;
	}
	if (!(flags & 1))	/* MREMAP_MAYMOVE */
		return -12;
	void *n = mmap (0, nlen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (n == MAP_FAILED)
		return fail ();
	memcpy (n, old, olen);
	munmap (old, olen);
	return (long) n;
}

static long do_sendfile (int out, int in, int64_t *off, size_t count)
{
	char buf[65536];
	size_t done = 0;
	while (done < count)
	{
		size_t want = count - done < sizeof buf ? count - done : sizeof buf;
		ssize_t k = off ? pread (in, buf, want, (off_t) (*off + (int64_t) done)) : read (in, buf, want);
		if (k < 0)
			return done ? (long) done : fail ();
		if (k == 0)
			break;
		ssize_t w = write (out, buf, (size_t) k);
		if (w < 0)
			return done ? (long) done : fail ();
		done += (size_t) w;
		if (w < k)
			break;
	}
	if (off)
		*off += (int64_t) done;
	return (long) done;
}

static long do_clock_nanosleep (long clk, long flags, const struct timespec *req, struct timespec *rem)
{
	struct timespec t = *req;
	if (flags & 1)	/* TIMER_ABSTIME */
	{
		struct timespec now;
		clock_gettime (clk == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
		long long ns = (long long) (t.tv_sec - now.tv_sec) * 1000000000LL + (t.tv_nsec - now.tv_nsec);
		if (ns <= 0)
			return 0;
		t.tv_sec = (time_t) (ns / 1000000000LL);
		t.tv_nsec = (long) (ns % 1000000000LL);
		rem = 0;
	}
	return nanosleep (&t, rem) < 0 ? fail () : 0;
}

static long do_getrusage (long who, long *ru)
{
	(void) who;
	memset (ru, 0, 144);
	struct timespec t;
#ifdef CLOCK_PROCESS_CPUTIME_ID
	if (clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &t) == 0)
	{
		ru[0] = t.tv_sec;		/* ru_utime */
		ru[1] = t.tv_nsec / 1000;
	}
#endif
	ru[4] = 64 * 1024;			/* ru_maxrss (Ko) */
	return 0;
}

/* « /proc/self/fd/N/reste » (et /proc/thread-self/fd, /dev/fd) : sous Linux, le dossier ouvert N sert de chemin. Claude
 * Code crée et ouvre ainsi ses dossiers de travail sans suivre de lien (mkdir("/proc/self/fd/N/tasks")) : ENOENT ici
 * lui faisait croire le dossier supprimé (« held directory vanished », plus aucune commande). Le chemin est récrit
 * avec celui du descripteur (libposix __posix_fd_path) ; « /proc/self/fd/N » seul aussi, sauf pour readlink (proc_link). */
static const char *fd_prefix (const char *p)
{
	if (!p)
		return 0;
	if (!strncmp (p, "/proc/self/fd/", 14))
		return p + 14;
	if (!strncmp (p, "/proc/thread-self/fd/", 21))
		return p + 21;
	if (!strncmp (p, "/dev/fd/", 8))
		return p + 8;
	return 0;
}

static const char *fd_path_rewrite (const char *p, int whole, char *buf, size_t cap)
{
	extern char *__posix_fd_path (int fd, char *buf, size_t cap);
	const char *n = fd_prefix (p);
	if (!n || *n < '0' || *n > '9')
		return p;
	char *end;
	long fd = strtol (n, &end, 10);
	if (*end != '/' && (*end || !whole))
		return p;
	char dir[1024];
	if (!__posix_fd_path ((int) fd, dir, sizeof dir))
		return p;
	size_t l = strlen (dir);
	while (l > 1 && dir[l - 1] == '/')
		dir[--l] = 0;
	snprintf (buf, cap, "%s%s", dir, end);
	return buf;
}

/* les arguments chemins des appels de Linux : récrits en place (syscall.c, avant tout le reste) */
void lx_rewrite_fd_paths (long nr, long *a, long *b, long *c, long *d)
{
	static __thread char b1[1100], b2[1100];
	long *x = 0, *y = 0;
	int whole = 1;
	switch (nr)
	{
	case 2: case 4: case 6: case 21: case 76: case 80: case 83: case 84: case 85: case 87: case 90: case 92: case 94:
	case 137:
		x = a; break;
	case 89: x = a; whole = 0; break;
	case 82: case 86: x = a; y = b; break;
	case 88: x = b; break;
	case 257: case 258: case 260: case 262: case 263: case 268: case 269: case 439: case 280: case 332: case 437:
		x = b; break;
	case 267: x = b; whole = 0; break;
	case 264: case 316: case 265: x = b; y = d; break;
	case 266: x = c; break;
	default: return;
	}
	if (x && *x)
		*x = (long) fd_path_rewrite ((const char *) *x, whole, b1, sizeof b1);
	if (y && *y)
		*y = (long) fd_path_rewrite ((const char *) *y, whole, b2, sizeof b2);
}

int lx_syscall2 (long nr, long a, long b, long c, long d, long e, long f, long *ret)
{
	(void) f;
	switch (nr)
	{
	/* lectures et écritures : eventfd */
	case 0:
		if (is_efd (a)) VAL (efd_read ((int) a, (void *) b, (size_t) c));
		return 0;
	case 1:
		if (is_efd (a)) VAL (efd_write ((int) a, (const void *) b, (size_t) c));
		return 0;
	case 3:	/* close */
		if (is_efd (a))
			efd_close ((int) a);
		dir_forget ((int) a);
		return 0;	/* (syscall.c ferme) */
	case 8:	/* lseek d'un répertoire : rewinddir */
		if (a >= 0 && a < MAX_FD && s_dir[a].d)
		{
			if (b == 0 && c == 0)
			{
				rewinddir (s_dir[a].d);
				s_dir[a].have = 0;
			}
			VAL (0);
		}
		return 0;
	case 19:	/* readv */
	case 20:	/* writev */
	{
		const struct iovec *v = (const struct iovec *) b;
		long tot = 0;
		for (long i = 0; i < c; i++)
		{
			long k = linux_syscall (nr == 19 ? 0 : 1, a, (long) v[i].iov_base, (long) v[i].iov_len, 0, 0, 0);
			if (k < 0)
				VAL (tot ? tot : k);
			tot += k;
			if ((size_t) k < v[i].iov_len)
				break;
		}
		VAL (tot);
	}
	case 18: RET (pwrite ((int) a, (const void *) b, (size_t) c, (off_t) d));
	case 327: case 328:	/* preadv2, pwritev2 (drapeaux ignorés ; décalage -1 : la position courante) */
		if (e == -1 || d == -1)
			VAL (linux_syscall (nr == 327 ? 19 : 20, a, b, c, 0, 0, 0));
		nr = nr == 327 ? 295 : 296;
		/* fallthrough */
	case 295: case 296:	/* preadv, pwritev */
	{
		const struct iovec *v = (const struct iovec *) b;
		long tot = 0;
		for (long i = 0; i < c; i++)
		{
			ssize_t k = nr == 295 ? pread ((int) a, v[i].iov_base, v[i].iov_len, (off_t) (d + tot))
					      : pwrite ((int) a, v[i].iov_base, v[i].iov_len, (off_t) (d + tot));
			if (k < 0)
				VAL (tot ? tot : fail ());
			tot += k;
			if ((size_t) k < v[i].iov_len)
				break;
		}
		VAL (tot);
	}
	/* fichiers et chemins */
	case 257: VAL (do_openat (a, (const char *) b, c, d));
	case 85: VAL (do_openat (-100, (const char *) a, 0x241, b));	/* creat */
	case 262:
		if (b && !strncmp ((const char *) b, "/proc/", 6))
			return 0;
		VAL (do_fstatat (a, (const char *) b, (void *) c, d));
	case 332: VAL (do_statx (a, (const char *) b, c, d, (unsigned char *) e));
	case 217: VAL (do_getdents64 ((int) a, (unsigned char *) b, (size_t) c));
	case 78: VAL (-38);	/* getdents (l'ancien) */
	case 16: VAL (do_ioctl ((int) a, (unsigned long) b, c));
	case 72: if (a >= 0x7000) VAL (b == 3 ? 0 : 0); VAL (do_fcntl ((int) a, b, c));	/* (fichiers fabriqués) */
	case 32: RET (dup ((int) a));
	case 33: RET (dup2 ((int) a, (int) b));
	case 292: RET (dup3 ((int) a, (int) b, (c & 0x80000) ? O_CLOEXEC : 0));
	case 22: case 293:	/* pipe, pipe2 */
	{
		int fl = 0;
		if (nr == 293 && (b & 0x80000)) fl |= O_CLOEXEC;
		if (nr == 293 && (b & 0x800)) fl |= O_NONBLOCK;
		RET (pipe2 ((int *) a, fl));
	}
	case 7: RET (poll ((struct pollfd *) a, (nfds_t) b, (int) c));
	case 271: RET (poll ((struct pollfd *) a, (nfds_t) b, ms_of ((const struct timespec *) c)));
	case 23:
	{
		const struct timeval *tv = (const struct timeval *) e;
		int ms = tv ? (int) (tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000) : -1;
		VAL (do_select ((int) a, (uint64_t *) b, (uint64_t *) c, (uint64_t *) d, ms));
	}
	case 270:
		VAL (do_select ((int) a, (uint64_t *) b, (uint64_t *) c, (uint64_t *) d, ms_of ((const struct timespec *) e)));
	case 21: RET (access ((const char *) a, (int) b));
	case 269: case 439:
		RET (faccessat (at_fd (a), (const char *) b, (int) c, (d & 0x200) ? AT_EACCESS : 0));
	case 80: RET (chdir ((const char *) a));
	case 81: RET (fchdir ((int) a));
	case 79:
	{
		if (!getcwd ((char *) a, (size_t) b))
			VAL (fail ());
		VAL ((long) strlen ((char *) a) + 1);
	}
	case 82: RET (rename ((const char *) a, (const char *) b));
	case 264: RET (renameat (at_fd (a), (const char *) b, at_fd (c), (const char *) d));
	case 316:
		if (e & 1)	/* RENAME_NOREPLACE */
		{
			struct stat st;
			if (fstatat (at_fd (c), (const char *) d, &st, 0) == 0)
				VAL (-17);
		}
		if (e & ~1L)
			VAL (-22);
		RET (renameat (at_fd (a), (const char *) b, at_fd (c), (const char *) d));
	case 83: RET (mkdir ((const char *) a, (mode_t) b));
	case 258: RET (mkdirat (at_fd (a), (const char *) b, (mode_t) c));
	case 84: RET (rmdir ((const char *) a));
	case 87: RET (unlink ((const char *) a));
	case 263: RET (unlinkat (at_fd (a), (const char *) b, (c & 0x200) ? AT_REMOVEDIR : 0));
	case 86: RET (link ((const char *) a, (const char *) b));
	case 265: RET (linkat (at_fd (a), (const char *) b, at_fd (c), (const char *) d, at_flags (e)));
	case 88: RET (symlink ((const char *) a, (const char *) b));
	case 266: RET (symlinkat ((const char *) a, at_fd (b), (const char *) c));
	case 89: case 267:
	{
		const char *p = (const char *) (nr == 89 ? a : b);
		char *buf = (char *) (nr == 89 ? b : c);
		size_t n = (size_t) (nr == 89 ? c : d);
		if (p && !strncmp (p, "/proc/", 6))
			VAL (proc_link (p, buf, n));
		RET (nr == 89 ? readlink (p, buf, n) : readlinkat (at_fd (a), p, buf, n));
	}
	case 90: RET (chmod ((const char *) a, (mode_t) b));
	case 91: RET (fchmod ((int) a, (mode_t) b));
	case 268: RET (fchmodat (at_fd (a), (const char *) b, (mode_t) c, 0));
	case 92: case 93: case 94: case 260: VAL (0);
	case 73: VAL (0);	/* flock : accordé (un seul utilisateur) */	/* chown : un seul utilisateur, sans effet */
	case 95: VAL (umask ((mode_t) a));
	case 76: RET (truncate ((const char *) a, (off_t) b));
	case 77: RET (ftruncate ((int) a, (off_t) b));
	case 74: case 75: RET (fsync ((int) a));
	case 162: case 277: case 187: case 221: VAL (0);	/* sync, sync_file_range, readahead, fadvise64 */
	case 285:	/* fallocate : le mode 0 (agrandir) */
	{
		if (b)
			VAL (-95);
		struct stat st;
		if (fstat ((int) a, &st) < 0)
			VAL (fail ());
		if (c + d > st.st_size)
			RET (ftruncate ((int) a, (off_t) (c + d)));
		VAL (0);
	}
	case 40: VAL (do_sendfile ((int) a, (int) b, (int64_t *) c, (size_t) d));
	case 275: case 276: case 278: case 326: VAL (-38);	/* splice, tee, vmsplice, copy_file_range */
	case 137: VAL (do_statfs ((const char *) a, -1, (uint64_t *) b));
	case 138: VAL (do_statfs (0, (int) a, (uint64_t *) b));
	case 280: VAL (do_utimensat (a, (const char *) b, (const int64_t *) c, d));
	case 235:	/* utimes : struct timeval */
	{
		int64_t t[4];
		const int64_t *tv = (const int64_t *) b;
		if (tv)
		{
			t[0] = tv[0]; t[1] = tv[1] * 1000; t[2] = tv[2]; t[3] = tv[3] * 1000;
		}
		VAL (do_utimensat (-100, (const char *) a, tv ? t : 0, 0));
	}
	case 188: case 189: case 190: case 197: case 198: case 199: VAL (-95);	/* setxattr…, removexattr… */
	case 191: case 192: case 193: VAL (-61);	/* getxattr : ENODATA */
	case 194: case 195: case 196: VAL (0);		/* listxattr : aucun */
	case 165: case 166: case 161: case 272: VAL (-1);	/* mount, umount2, chroot, unshare : EPERM */
	case 135: VAL (0);	/* personality : PER_LINUX */
	/* mémoire */
	case 25: VAL (do_mremap ((void *) a, (size_t) b, (size_t) c, d));
	case 28:	/* madvise : MADV_DONTNEED (4), MADV_FREE (8), MADV_REMOVE (9) rendent les pages : nulles au prochain
			   accès, comme sous Linux (libpas, mimalloc y comptent) ; les autres conseils : sans effet */
		if (b > 0 && (c == 4 || c == 8 || c == 9))
		{
			extern int linux_refill (void *p, size_t n);
			/* (l'image d'un exécutable ou d'un module lu d'un fichier : relue depuis le fichier, comme une
			   projection privée de fichier sous Linux ; le reste : des pages anonymes, nulles ensuite) */
			madvise ((void *) a, (size_t) b, MADV_DONTNEED);
			linux_refill ((void *) a, (size_t) b);
		}
		VAL (0);
	case 26: VAL (0);	/* msync */
	case 27:		/* mincore : tout est là */
		memset ((void *) c, 1, (size_t) ((b + 4095) / 4096));
		VAL (0);
	case 12: VAL (0);	/* brk : pas de tas de brk (malloc de Bun : mmap) */
	case 319: RET (memfd_create ((const char *) a, (unsigned) ((b & 1) ? 1 : 0)));
	/* process */
	case 110: VAL (getppid ());
	case 111: VAL (getpgrp ());
	case 109: RET (setpgid ((pid_t) a, (pid_t) b));
	case 112: RET (setsid ());
	case 121: VAL (a == 0 || a == getpid () ? getpgrp () : (long) a);
	case 124: VAL (getpid ());
	case 105: case 106: case 113: case 114: case 117: case 119: case 122: case 123:
		VAL (0);	/* setuid… : le même utilisateur */
	case 115:	/* getgroups */
		if (a > 0 && b)
			*(uint32_t *) b = (uint32_t) getgid ();
		VAL (1);
	case 116: VAL (0);
	case 118: case 120:	/* getresuid, getresgid */
	{
		uint32_t v = (uint32_t) (nr == 118 ? getuid () : getgid ());
		*(uint32_t *) a = v;
		*(uint32_t *) b = v;
		*(uint32_t *) c = v;
		VAL (0);
	}
	case 160: VAL (0);	/* setrlimit */
	case 98: VAL (do_getrusage (a, (long *) b));
	case 57: case 58: RET (fork ());	/* fork, vfork (une copie : le fils ne fait qu'exec) */
	case 56:		/* clone : seulement la forme de fork (SIGCHLD, sans CLONE_VM) */
		if ((a & 0x100) || (a & 0x10000))
			VAL (-38);
		RET (fork ());
	case 435: VAL (-38);	/* clone3 */
	case 59: VAL (do_execve ((const char *) a, (char *const *) b, (char *const *) c));
	case 61: VAL (do_wait4 (a, (int *) b, c, (void *) d));
	case 247: VAL (do_waitid (a, b, (unsigned char *) c, d));
	case 62:
	{
		int ks = b ? lx_sig_korin ((int) b) : 0;
		if (b && !ks)
			VAL (-22);
		RET (kill ((pid_t) a, ks));
	}
	case 200: case 234:	/* tkill (tid, sig), tgkill (tgid, tid, sig) */
	{
		long tid = nr == 200 ? a : b, s = nr == 200 ? b : c;
		int ks = s ? lx_sig_korin ((int) s) : 0;
		if (s && !ks)
			VAL (-22);
		if (tid == syscall (SYS_gettid))
			RET (raise (ks));
		VAL (armpi_syscall_sig_send (tid, ks));
	}
	case 434: case 424: case 438: VAL (-38);	/* pidfd_open, pidfd_send_signal, pidfd_getfd */
	/* signaux */
	/* (les modules, comme Widevine, gardent ceux de syscall.c : gardés, pas installés) */
	case 13: if (!linux_exe_path) return 0; VAL (do_sigaction (a, (const struct lx_kact *) b, (struct lx_kact *) c));
	case 14: if (!linux_exe_path) return 0; VAL (do_sigprocmask (a, (const uint64_t *) b, (uint64_t *) c, d));
	case 127:
	{
		sigset_t k = 0;
		sigpending (&k);
		memset ((void *) a, 0, (size_t) (b > 0 && b <= 128 ? b : 8));
		*(uint64_t *) a = set_linux (k);
		VAL (0);
	}
	case 130:
	{
		sigset_t k = set_korin ((const uint64_t *) a);
		RET (sigsuspend (&k));
	}
	case 34:
	{
		sigset_t k = 0;
		pthread_sigmask (SIG_BLOCK, 0, &k);
		RET (sigsuspend (&k));
	}
	case 131: RET (sigaltstack ((const stack_t *) a, (stack_t *) b));
	case 37: VAL (alarm ((unsigned) a));
	case 38: RET (setitimer ((int) a, (const struct itimerval *) b, (struct itimerval *) c));
	case 36: RET (getitimer ((int) a, (struct itimerval *) b));
	case 222: case 223: case 224: case 225: case 226: VAL (-38);	/* timer_* (pas dans libposix) */
	case 282: case 289: VAL (-38);	/* signalfd */
	case 283: case 286: case 287: VAL (-38);	/* timerfd */
	case 253: case 294: case 254: case 255: VAL (-38);	/* inotify */
	case 284: VAL (efd_create (a, 0));
	case 290: VAL (efd_create (a, b));
	/* epoll */
	case 213: case 291: RET (epoll_create1 ((b & 0x80000) || (nr == 291 && (a & 0x80000)) ? EPOLL_CLOEXEC : 0));
	case 233: VAL (do_epoll_ctl ((int) a, (int) b, (int) c, (const struct lx_epev *) d));
	case 232: case 281: case 441:
	{
		int ms = nr == 441 ? ms_of ((const struct timespec *) d) : (int) d;
		VAL (do_epoll_wait ((int) a, (struct lx_epev *) b, (int) c, ms));
	}
	/* sockets : les mêmes constantes et structures */
	case 41: RET (socket ((int) a, (int) b, (int) c));
	case 42: RET (connect ((int) a, (const struct sockaddr *) b, (socklen_t) c));
	case 43: RET (accept ((int) a, (struct sockaddr *) b, (socklen_t *) c));
	case 288: RET (accept4 ((int) a, (struct sockaddr *) b, (socklen_t *) c, (int) d));
	case 44: RET (sendto ((int) a, (const void *) b, (size_t) c, (int) d, (const struct sockaddr *) e, (socklen_t) f));
	case 45: RET (recvfrom ((int) a, (void *) b, (size_t) c, (int) d, (struct sockaddr *) e, (socklen_t *) f));
	case 46: RET (sendmsg ((int) a, (const struct msghdr *) b, (int) c));
	case 47: RET (recvmsg ((int) a, (struct msghdr *) b, (int) c));
	case 48: RET (shutdown ((int) a, (int) b));
	case 49: RET (bind ((int) a, (const struct sockaddr *) b, (socklen_t) c));
	case 50: RET (listen ((int) a, (int) b));
	case 51: RET (getsockname ((int) a, (struct sockaddr *) b, (socklen_t *) c));
	case 52: RET (getpeername ((int) a, (struct sockaddr *) b, (socklen_t *) c));
	case 53: RET (socketpair ((int) a, (int) b, (int) c, (int *) d));
	case 54: RET (setsockopt ((int) a, (int) b, (int) c, (const void *) d, (socklen_t) e));
	case 55: RET (getsockopt ((int) a, (int) b, (int) c, (void *) d, (socklen_t *) e));
	case 299: case 307:	/* recvmmsg, sendmmsg : un message à la fois */
	{
		struct lx_mmsg { struct msghdr h; unsigned len; } *m = (struct lx_mmsg *) b;
		long n = 0;
		for (; n < c; n++)
		{
			ssize_t k = nr == 307 ? sendmsg ((int) a, &m[n].h, (int) d) : recvmsg ((int) a, &m[n].h, (int) d);
			if (k < 0)
				VAL (n ? n : fail ());
			m[n].len = (unsigned) k;
			if (nr == 299)
				d |= MSG_DONTWAIT;	/* (après le premier : ce qui est déjà là) */
		}
		VAL (n);
	}
	/* divers */
	case 203: VAL (0);	/* sched_setaffinity */
	case 309:		/* getcpu */
		if (a) *(unsigned *) a = 0;
		if (b) *(unsigned *) b = 0;
		VAL (0);
	case 436:	/* close_range (premier, dernier, drapeaux : CLOSE_RANGE_CLOEXEC 4) */
		for (long fd = a; fd <= b && fd < 64; fd++)
		{
			if (c & 4)
				fcntl ((int) fd, F_SETFD, FD_CLOEXEC);
			else
			{
				long dummy;
				lx_syscall2 (3, fd, 0, 0, 0, 0, 0, &dummy);
				close ((int) fd);
			}
		}
		VAL (0);
	case 273: case 274: VAL (0);	/* set_robust_list, get_robust_list */
	case 218: VAL (syscall (SYS_gettid));	/* set_tid_address */
	case 334: VAL (-38);	/* rseq */
	case 158:		/* arch_prctl : ARCH_GET_FS */
		if (a == 0x1003)
		{
			unsigned long fs;
			__asm__ ("mov %%fs:0, %0" : "=r" (fs));
			*(unsigned long *) b = fs;
			VAL (0);
		}
		VAL (-22);
	case 99:
	{
		extern int lx_sysinfo_fill (void *);
		VAL (lx_sysinfo_fill ((void *) a));
	}
	case 230: VAL (do_clock_nanosleep (a, b, (const struct timespec *) c, (struct timespec *) d));
	case 310: case 311: VAL (-38);	/* process_vm_readv/writev */
	case 140: VAL (20);	/* getpriority : nice 0 (le noyau de Linux rend 20 - nice) */
	case 141: VAL (0);
	case 100:		/* times */
	{
		long ru[18];
		do_getrusage (0, ru);
		if (a)
			memset ((void *) a, 0, 32), *(long *) a = ru[0] * 100 + ru[1] / 10000;
		struct timespec t;
		clock_gettime (CLOCK_MONOTONIC, &t);
		VAL (t.tv_sec * 100 + t.tv_nsec / 10000000);
	}
	}
	return 0;
}
