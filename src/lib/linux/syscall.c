/*
 * syscall.c -- liblinux : les appels système de Linux x86-64 traduits en appels de libposix
 * (linux.h, docs/etude/WIDEVINE.md § 4.3).
 *
 * linux_syscall rend ce que rendrait le noyau Linux : la valeur, ou -errno de Linux. Les numéros,
 * les constantes (horloges, drapeaux d'open, de mmap) et les numéros d'errno sont ceux de Linux ;
 * libposix (newlib) a les siens, d'où les tables plus bas. Un appel non servi rend -ENOSYS et le dit
 * une fois sur la sortie d'erreur (c'est la liste de ce qui reste à faire).
 *
 * Servis : read, write, open, openat, close, stat, fstat, lstat, newfstatat, lseek, pread64, mmap,
 * mprotect, munmap, madvise, rt_sigaction (gardé, pas installé), rt_sigprocmask, sched_yield,
 * nanosleep, getpid, gettid, get*id, uname, getrlimit, prlimit64, prctl, sched_getaffinity,
 * clock_gettime, clock_getres, gettimeofday, time, getrandom, exit, exit_group. Les chemins de /proc
 * sont fabriqués (/proc/self/maps : linux_maps d'elf.c ; /proc/net/unix : l'en-tête seul ;
 * LINUX_TRACE_PROC=1 les note). Futex, clone, et le reste : plus tard.
 */
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "linux.h"

/* numéros d'appels de Linux x86-64 (arch/x86/entry/syscalls/syscall_64.tbl) */
#define LNR_read		0
#define LNR_write		1
#define LNR_open		2
#define LNR_close		3
#define LNR_stat		4
#define LNR_fstat		5
#define LNR_lstat		6
#define LNR_lseek		8
#define LNR_mmap		9
#define LNR_mprotect		10
#define LNR_munmap		11
#define LNR_rt_sigaction	13
#define LNR_rt_sigprocmask	14
#define LNR_pread64		17
#define LNR_sched_yield		24
#define LNR_madvise		28
#define LNR_nanosleep		35
#define LNR_getpid		39
#define LNR_exit		60
#define LNR_uname		63
#define LNR_gettimeofday	96
#define LNR_getrlimit		97
#define LNR_getuid		102
#define LNR_getgid		104
#define LNR_geteuid		107
#define LNR_getegid		108
#define LNR_prctl		157
#define LNR_gettid		186
#define LNR_time		201
#define LNR_futex		202
#define LNR_sched_getaffinity	204
#define LNR_clock_gettime	228
#define LNR_clock_getres	229
#define LNR_exit_group		231
#define LNR_openat		257
#define LNR_newfstatat		262
#define LNR_prlimit64		302
#define LNR_getrandom		318

/* errno de Linux (include/uapi/asm-generic/errno-base.h et errno.h) ; 1 à 34 sont les mêmes que
   ceux de newlib, la suite diffère */
static const struct { int korin, lnx; } s_errno[] =
{
	{ EDEADLK, 35 }, { ENAMETOOLONG, 36 }, { ENOLCK, 37 }, { ENOSYS, 38 }, { ENOTEMPTY, 39 },
	{ ELOOP, 40 }, { ENOMSG, 42 }, { EIDRM, 43 }, { ENOSTR, 60 }, { ENODATA, 61 }, { ETIME, 62 },
	{ ENOSR, 63 }, { ENOLINK, 67 }, { EPROTO, 71 }, { EMULTIHOP, 72 }, { EBADMSG, 74 },
	{ EOVERFLOW, 75 }, { EILSEQ, 84 }, { ENOTSOCK, 88 }, { EDESTADDRREQ, 89 }, { EMSGSIZE, 90 },
	{ EPROTOTYPE, 91 }, { ENOPROTOOPT, 92 }, { EPROTONOSUPPORT, 93 }, { EOPNOTSUPP, 95 },
	{ EAFNOSUPPORT, 97 }, { EADDRINUSE, 98 }, { EADDRNOTAVAIL, 99 }, { ENETDOWN, 100 },
	{ ENETUNREACH, 101 }, { ENETRESET, 102 }, { ECONNABORTED, 103 }, { ECONNRESET, 104 },
	{ ENOBUFS, 105 }, { EISCONN, 106 }, { ENOTCONN, 107 }, { ETIMEDOUT, 110 },
	{ ECONNREFUSED, 111 }, { EHOSTUNREACH, 113 }, { EALREADY, 114 }, { EINPROGRESS, 115 },
	{ ESTALE, 116 }, { EDQUOT, 122 }, { ECANCELED, 125 }, { EOWNERDEAD, 130 },
	{ ENOTRECOVERABLE, 131 },
};

int linux_errno (int e)
{
	if (e > 0 && e <= 34)
		return e;
	for (size_t i = 0; i < sizeof s_errno / sizeof s_errno[0]; i++)
		if (s_errno[i].korin == e)
			return s_errno[i].lnx;
	return e == ENOTSUP ? 95 : 5;	/* (EIO pour le reste) */
}

int linux_errno_korin (int e)
{
	if (e > 0 && e <= 34)
		return e;
	for (size_t i = 0; i < sizeof s_errno / sizeof s_errno[0]; i++)
		if (s_errno[i].lnx == e)
			return s_errno[i].korin;
	return e == 95 ? ENOTSUP : EIO;
}

unsigned long linux_syscall_count[512];

/* ---- fichiers fabriqués (Korin n'a pas de /proc) : des descripteurs à part, servis ici ---- */
#define SYNTH_BASE	0x7000
#define SYNTH_MAX	16
static struct
{
	char *data;
	size_t len, pos;
} s_synth[SYNTH_MAX];
static int s_synth_lock;

static void synth_lock (void)
{
	while (__atomic_exchange_n (&s_synth_lock, 1, __ATOMIC_ACQUIRE))
		sched_yield ();
}

static void synth_unlock (void)
{
	__atomic_store_n (&s_synth_lock, 0, __ATOMIC_RELEASE);
}

static int synth_slot (long fd)
{
	if (fd < SYNTH_BASE || fd >= SYNTH_BASE + SYNTH_MAX || !s_synth[fd - SYNTH_BASE].data)
		return -1;
	return (int) (fd - SYNTH_BASE);
}

/* le contenu d'un chemin de /proc (malloc), ou NULL */
static char *synth_content (const char *path, size_t *len)
{
	char *b = 0;
	if (!strcmp (path, "/proc/self/maps") || !strcmp (path, "/proc/thread-self/maps"))
	{
		size_t cap = 64 * 1024;
		b = malloc (cap);
		if (b)
			*len = linux_maps (b, cap);
	}
	else if (!strcmp (path, "/proc/net/unix"))
	{
		static const char h[] = "Num       RefCount Protocol Flags    Type St Inode Path\n";
		b = malloc (sizeof h);
		if (b)
		{
			memcpy (b, h, sizeof h);
			*len = sizeof h - 1;
		}
	}
	if (getenv ("LINUX_TRACE_PROC"))
		fprintf (stderr, "liblinux : %s %s\n", path, b ? "fabriqué" : "absent (ENOENT)");
	return b;
}

static long synth_open (const char *path)
{
	size_t len = 0;
	char *data = synth_content (path, &len);
	if (!data)
		return -2;	/* ENOENT */
	synth_lock ();
	for (int i = 0; i < SYNTH_MAX; i++)
		if (!s_synth[i].data)
		{
			s_synth[i].data = data;
			s_synth[i].len = len;
			s_synth[i].pos = 0;
			synth_unlock ();
			return SYNTH_BASE + i;
		}
	synth_unlock ();
	free (data);
	return -24;	/* EMFILE */
}

static long synth_io (int si, long nr, long b, long c, long d)
{
	long r = 0;
	synth_lock ();
	if (nr == LNR_read || nr == LNR_pread64)
	{
		size_t pos = nr == LNR_read ? s_synth[si].pos : (size_t) d;
		size_t n = pos < s_synth[si].len ? s_synth[si].len - pos : 0;
		if (n > (size_t) c)
			n = (size_t) c;
		memcpy ((void *) b, s_synth[si].data + pos, n);
		if (nr == LNR_read)
			s_synth[si].pos += n;
		r = (long) n;
	}
	else if (nr == LNR_lseek)
	{
		long base = c == 0 ? 0 : c == 1 ? (long) s_synth[si].pos : (long) s_synth[si].len;
		if (base + b < 0)
			r = -22;
		else
			r = (long) (s_synth[si].pos = (size_t) (base + b));
	}
	else if (nr == LNR_fstat)
	{
		memset ((void *) b, 0, 144);
		((uint32_t *) b)[6] = 0100444;		/* st_mode : fichier ordinaire en lecture, taille 0 (comme /proc) */
		((uint64_t *) b)[7] = 4096;		/* st_blksize */
	}
	else	/* close */
	{
		free (s_synth[si].data);
		s_synth[si].data = 0;
	}
	synth_unlock ();
	return r;
}

/* drapeaux d'open de Linux -> libposix */
static int open_flags (long f)
{
	int r = (int) (f & 3);	/* O_RDONLY, O_WRONLY, O_RDWR : les mêmes */
	if (f & 0x40) r |= O_CREAT;
	if (f & 0x80) r |= O_EXCL;
	if (f & 0x200) r |= O_TRUNC;
	if (f & 0x400) r |= O_APPEND;
	if (f & 0x800) r |= O_NONBLOCK;
#ifdef O_DIRECTORY
	if (f & 0x10000) r |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
	if (f & 0x20000) r |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
	if (f & 0x80000) r |= O_CLOEXEC;
#endif
	return r;
}

static long do_open (const char *path, long flags, long mode)
{
	if (!path)
		return -14;	/* EFAULT */
	if (!strncmp (path, "/proc/", 6))
		return synth_open (path);
	int fd = open (path, open_flags (flags), (int) mode);
	return fd < 0 ? -linux_errno (errno) : fd;
}

/* struct stat de Linux x86-64 (144 octets) depuis celle de libposix */
static void stat_out (void *out, const struct stat *st)
{
	uint64_t *q = out;
	memset (out, 0, 144);
	q[0] = (uint64_t) st->st_dev;
	q[1] = (uint64_t) st->st_ino;
	q[2] = (uint64_t) st->st_nlink;
	uint32_t *w = (uint32_t *) (q + 3);
	w[0] = (uint32_t) st->st_mode;
	w[1] = (uint32_t) st->st_uid;
	w[2] = (uint32_t) st->st_gid;
	q[5] = (uint64_t) st->st_rdev;
	q[6] = (uint64_t) st->st_size;
	q[7] = (uint64_t) st->st_blksize;
	q[8] = (uint64_t) st->st_blocks;
	q[9] = (uint64_t) st->st_atime;
	q[11] = (uint64_t) st->st_mtime;
	q[13] = (uint64_t) st->st_ctime;
}

/* rt_sigaction : ce que le module pose est gardé, pas installé ; il relit ce qu'il a posé, sinon
   SIG_DFL (struct de Linux : gestionnaire, drapeaux, retour, masque : 32 octets) */
static uint64_t s_sigact[65][4];

static long do_sigaction (long sig, const uint64_t *act, uint64_t *old)
{
	if (sig < 1 || sig > 64 || sig == 9 || sig == 19)
		return -22;
	if (old)
		memcpy (old, s_sigact[sig], 32);
	if (act)
	{
		memcpy (s_sigact[sig], act, 32);
		fprintf (stderr, "liblinux : rt_sigaction (%ld) posé par le module : gardé, pas installé\n", sig);
	}
	return 0;
}

/* struct utsname de Linux : 6 champs de 65 octets */
static long do_uname (char *u)
{
	static const char *f[6] = { "Linux", "korin", "6.8.0-korin", "#1 SMP PREEMPT_DYNAMIC", "x86_64", "(none)" };
	memset (u, 0, 6 * 65);
	for (int i = 0; i < 6; i++)
		strncpy (u + i * 65, f[i], 64);
	return 0;
}

/* limites (RLIMIT_* de Linux) : struct rlimit = deux u64 */
static long do_getrlimit (long res, uint64_t *out)
{
	if (!out)
		return -14;
	uint64_t inf = ~0ULL;
	switch (res)
	{
	case 3: out[0] = 8 << 20; out[1] = inf; break;	/* RLIMIT_STACK */
	case 7: out[0] = 64; out[1] = 64; break;	/* RLIMIT_NOFILE (64 fichiers par process) */
	default: out[0] = inf; out[1] = inf; break;
	}
	return 0;
}

/* futex de Linux sur ceux de Korin (FUTEX_WAIT/WAKE et leurs formes _BITSET, bitset ignoré : tous) */
#if defined(__linux__)
static long do_futex (long addr, long op, long val, long tmo, long a5, long a6)
{
	return syscall (SYS_futex, addr, op, val, tmo, a5, a6) < 0 ? -errno : 0;
}
#else
#include <armpi/syscall.h>
#include "korin_sys_nr.h"

static long do_futex (long addr, long op, long val, long tmo, long a5, long a6)
{
	(void) a5;
	(void) a6;
	int cmd = (int) (op & 0x7f);	/* (sans FUTEX_PRIVATE_FLAG 128, FUTEX_CLOCK_REALTIME 256) */
	if (cmd == 1 || cmd == 10)	/* FUTEX_WAKE, FUTEX_WAKE_BITSET */
		return armpi_syscall (SYS_FUTEX_WAKE, addr, val, 0, 0, 0, 0);
	if (cmd != 0 && cmd != 9)	/* FUTEX_WAIT, FUTEX_WAIT_BITSET */
		return -38;
	uint64_t ms = 0;	/* (0 : sans fin, pour Korin) */
	if (tmo)
	{
		const struct timespec *t = (const struct timespec *) tmo;
		int64_t ns = (int64_t) t->tv_sec * 1000000000 + t->tv_nsec;
		if (cmd == 9)	/* délai absolu : sur CLOCK_MONOTONIC, ou CLOCK_REALTIME avec le drapeau 256 */
		{
			struct timespec now;
			clock_gettime ((op & 256) ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
			ns -= (int64_t) now.tv_sec * 1000000000 + now.tv_nsec;
		}
		if (ns <= 0)
			return *(volatile uint32_t *) addr == (uint32_t) val ? -110 : -11;	/* ETIMEDOUT, EAGAIN */
		ms = (uint64_t) ((ns + 999999) / 1000000);
	}
	/* (E_AGAIN, E_TIMEDOUT, E_INTR de Korin ont les valeurs de Linux : -11, -110, -4) */
	return armpi_syscall (SYS_FUTEX_WAIT, addr, (uint32_t) val, (long) ms, 0, 0, 0);
}
#endif

static char s_thread_name[16] = "cdmhost";

static long do_prctl (long op, long a)
{
	switch (op)
	{
	case 15: strncpy (s_thread_name, (const char *) a, 15); return 0;	/* PR_SET_NAME */
	case 16: memcpy ((char *) a, s_thread_name, 16); return 0;		/* PR_GET_NAME */
	case 3: return 1;							/* PR_GET_DUMPABLE */
	case 4: return 0;							/* PR_SET_DUMPABLE */
	}
	return -22;
}

/* horloges de Linux -> celles de libposix */
static int clock_of (long id, clockid_t *out)
{
	switch (id)
	{
	case 0: case 5: *out = CLOCK_REALTIME; return 0;		/* REALTIME, REALTIME_COARSE */
	case 1: case 4: case 6: case 7: *out = CLOCK_MONOTONIC; return 0;	/* MONOTONIC, _RAW, _COARSE, BOOTTIME */
#ifdef CLOCK_PROCESS_CPUTIME_ID
	case 2: *out = CLOCK_PROCESS_CPUTIME_ID; return 0;
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
	case 3: *out = CLOCK_THREAD_CPUTIME_ID; return 0;
#endif
	}
	return -1;
}

/* drapeaux de mmap de Linux -> libposix (les mêmes valeurs aujourd'hui : sys/mman.h de libposix les
   a pris de Linux ; la table garde le code juste si l'un des deux change) */
static int mmap_flags (long f, int *out)
{
	int r = 0;
	if (f & 0x01) r |= MAP_SHARED;
	if (f & 0x02) r |= MAP_PRIVATE;
	if (f & 0x10) r |= MAP_FIXED;
	if (f & 0x20) r |= MAP_ANONYMOUS;
	/* (0x4000 MAP_NORESERVE, 0x8000 MAP_POPULATE, 0x20000 MAP_STACK : sans effet ici) */
	if (f & ~(0x01 | 0x02 | 0x10 | 0x20 | 0x4000 | 0x8000 | 0x20000 | 0x100000))
		return -1;
	if (f & 0x100000)
#ifdef MAP_FIXED_NOREPLACE
		r |= MAP_FIXED_NOREPLACE;
#else
		return -1;
#endif
	*out = r;
	return 0;
}

static long fail (void)
{
	return -linux_errno (errno);
}

static void not_served (long nr)
{
	static unsigned char said[512];
	if (nr >= 0 && nr < 512)
	{
		if (said[nr])
			return;
		said[nr] = 1;
	}
	fprintf (stderr, "liblinux : appel système de Linux %ld non servi (ENOSYS)\n", nr);
}

long linux_syscall (long nr, long a, long b, long c, long d, long e, long f)
{
	(void) d;
	(void) e;
	if (nr >= 0 && nr < 512)
		linux_syscall_count[nr]++;
	int si = synth_slot (a);
	if (si >= 0 && (nr == LNR_read || nr == LNR_close || nr == LNR_lseek || nr == LNR_fstat || nr == LNR_pread64))
		return synth_io (si, nr, b, c, d);
	switch (nr)
	{
	case LNR_open:
		return do_open ((const char *) a, b, c);
	case LNR_openat:
		/* (dirfd ignoré pour un chemin absolu ; AT_FDCWD = -100) */
		if (b && ((const char *) b)[0] != '/' && a != -100)
			return -38;
		return do_open ((const char *) b, c, d);
	case LNR_stat:
	case LNR_lstat:
	case LNR_newfstatat:
	{
		const char *path = (const char *) (nr == LNR_newfstatat ? b : a);
		void *out = (void *) (nr == LNR_newfstatat ? c : b);
		if (!path)
			return -14;
		if (!strncmp (path, "/proc/", 6))
		{
			long fd = synth_open (path);
			if (fd < 0)
				return fd;
			synth_io (synth_slot (fd), LNR_fstat, (long) out, 0, 0);
			synth_io (synth_slot (fd), LNR_close, 0, 0, 0);
			return 0;
		}
		struct stat st;
		if (stat (path, &st) < 0)
			return fail ();
		stat_out (out, &st);
		return 0;
	}
	case LNR_fstat:
	{
		struct stat st;
		if (fstat ((int) a, &st) < 0)
			return fail ();
		stat_out ((void *) b, &st);
		return 0;
	}
	case LNR_pread64:
	{
		ssize_t r = pread ((int) a, (void *) b, (size_t) c, (off_t) d);
		return r < 0 ? fail () : r;
	}
	case LNR_rt_sigaction:
		return do_sigaction (a, (const uint64_t *) b, (uint64_t *) c);
	case LNR_rt_sigprocmask:
		if (c)
			memset ((void *) c, 0, (size_t) (d > 0 && d <= 128 ? d : 8));
		return 0;
	case LNR_madvise:
		return 0;
	case LNR_futex:
		return do_futex (a, b, c, d, e, f);
	case LNR_uname:
		return do_uname ((char *) a);
	case LNR_getrlimit:
		return do_getrlimit (a, (uint64_t *) b);
	case LNR_prlimit64:
		return c ? do_getrlimit (b, (uint64_t *) c) : 0;
	case LNR_prctl:
		return do_prctl (a, b);
	case LNR_sched_getaffinity:
	{
		long n = sysconf (_SC_NPROCESSORS_ONLN);
		if (n < 1)
			n = 1;
		memset ((void *) c, 0, (size_t) b);
		for (long i = 0; i < n && i < b * 8; i++)
			((unsigned char *) c)[i / 8] |= (unsigned char) (1 << (i % 8));
		return 8;
	}
	case LNR_read:
	{
		ssize_t r = read ((int) a, (void *) b, (size_t) c);
		return r < 0 ? fail () : r;
	}
	case LNR_write:
	{
		ssize_t r = write ((int) a, (const void *) b, (size_t) c);
		return r < 0 ? fail () : r;
	}
	case LNR_close:
		return close ((int) a) < 0 ? fail () : 0;
	case LNR_lseek:
	{
		/* SEEK_SET, SEEK_CUR, SEEK_END : 0, 1, 2 des deux côtés */
		off_t r = lseek ((int) a, (off_t) b, (int) c);
		return r < 0 ? fail () : (long) r;
	}
	case LNR_mmap:
	{
		int fl;
		if (mmap_flags (d, &fl))
			return -22;	/* EINVAL */
		void *p = mmap ((void *) a, (size_t) b, (int) c, fl, (int) e, (off_t) f);
		return p == MAP_FAILED ? fail () : (long) p;
	}
	case LNR_mprotect:
		return mprotect ((void *) a, (size_t) b, (int) c) < 0 ? fail () : 0;
	case LNR_munmap:
		return munmap ((void *) a, (size_t) b) < 0 ? fail () : 0;
	case LNR_sched_yield:
		sched_yield ();
		return 0;
	case LNR_nanosleep:
		/* struct timespec : deux longs des deux côtés */
		return nanosleep ((const struct timespec *) a, (struct timespec *) b) < 0 ? fail () : 0;
	case LNR_getpid:
		return getpid ();
	case LNR_gettid:
		return syscall (SYS_gettid);
	case LNR_getuid:
		return getuid ();
	case LNR_geteuid:
		return geteuid ();
	case LNR_getgid:
		return getgid ();
	case LNR_getegid:
		return getegid ();
	case LNR_clock_gettime:
	case LNR_clock_getres:
	{
		clockid_t id;
		if (clock_of (a, &id))
			return -22;
		int r = nr == LNR_clock_gettime ? clock_gettime (id, (struct timespec *) b)
						 : clock_getres (id, (struct timespec *) b);
		return r < 0 ? fail () : 0;
	}
	case LNR_gettimeofday:
	{
		/* struct timeval : deux longs des deux côtés ; le fuseau (b) : UTC */
		if (a && gettimeofday ((struct timeval *) a, 0) < 0)
			return fail ();
		if (b)
			memset ((void *) b, 0, 8);
		return 0;
	}
	case LNR_time:
	{
		time_t t = time (0);
		if (a)
			*(long *) a = (long) t;
		return (long) t;
	}
	case LNR_getrandom:
	{
		/* GRND_NONBLOCK (1), GRND_RANDOM (2) : toujours prêt */
		ssize_t r = getrandom ((void *) a, (size_t) b, 0);
		return r < 0 ? fail () : (long) r;
	}
	case LNR_exit:
	case LNR_exit_group:
		_exit ((int) a);
	}
	not_served (nr);
	return -38;	/* ENOSYS de Linux */
}
