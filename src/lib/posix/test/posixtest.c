/*
 * posixtest.c -- SD0:/bin/posixtest : l'essai de conformite de libposix, la couche POSIX
 * d'ARMPI (docs/POSIX.md). Repris de posixtest d'Onyx (user/BinUtils/posixtest.c), adapte a
 * ARMPI : pages de 4 Ko, pas de projection a la demande ni de memoire executable, pas
 * d'environnement transmis aux fils, pas d'IPC locale, sockets de NetKit (connect bloquant).
 *
 *   posixtest [groupe...]     groupes : mem thread file io time proc net misc cxx (defaut : tous),
 *                             et loop (TCP / UDP sur 127.0.0.1 : pas dans le defaut)
 *   posixtest file SD0:/tmp   le groupe file dans un autre dossier (defaut RAM0:/posixtest et /tmp)
 *
 * Chaque verification ecrit PASS, FAIL (avec ce qui a ete vu) ou SKIP (ce qu'ARMPI n'a pas) ; puis
 * un bilan ; le code de sortie est le nombre d'echecs. Le groupe net demande le reseau.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <ftw.h>
#include <signal.h>
#include <sched.h>
#include <ucontext.h>
#include <sys/membarrier.h>
#include <spawn.h>
#include <pthread.h>
#include <semaphore.h>
#include <dirent.h>
#include <poll.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <dlfcn.h>
#include <limits.h>
#include <fnmatch.h>
#include <grp.h>
#include <link.h>
#include <setjmp.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern char **environ;

/* ---- reporting ---- */
static int s_pass, s_fail, s_skip;
static const char *s_group = "";

static void pass (const char *name)
{
	s_pass++;
	printf ("PASS  %s: %s\n", s_group, name);
}

static void fail (const char *name, const char *fmt, ...)
{
	char why[200];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (why, sizeof why, fmt, ap);
	va_end (ap);
	s_fail++;
	printf ("FAIL  %s: %s (%s)\n", s_group, name, why);
}

static void skip (const char *name, const char *why)
{
	s_skip++;
	printf ("SKIP  %s: %s (%s)\n", s_group, name, why);
}

#define CHECK(name, cond, ...) do { if (cond) pass (name); else fail (name, __VA_ARGS__); } while (0)
#define NOSYS		"ARMPI : absent"

/* ---- the kernel's v75 pieces (each answers -KAPI_ENOSYS until its work package lands) ---- */
static int k_vm, k_threads, k_files, k_proc, k_clock, k_sleep, k_env, k_net, k_poll, k_pipe_nb, k_ipc;

static int k_exec;
static int s_nftw_f, s_nftw_d;

static int nftw_compte (const char *p, const struct stat *st, int t, struct FTW *f)
{
	(void) p; (void) st; (void) f;
	if (t == FTW_F)
		s_nftw_f++;
	else if (t == FTW_DP && s_nftw_f == 2)
		s_nftw_d++;
	return 0;
}

static void probe_kernel (void)
{
	/* ARMPI : threads, fichiers, process, horloge, sommeil, tubes non bloquants, environnement
	   transmis ; pas de projection a la demande (vm), de sockets non bloquants a la
	   maniere d'Onyx v75 (connect bloque : le chemin des anciens sockets), d'IPC locale, de
	   memoire executable */
	k_vm = 0; k_threads = 1; k_files = 1; k_proc = 1; k_clock = 1; k_sleep = 1; k_env = 1;
	k_net = 0; k_poll = 1; k_pipe_nb = 1; k_ipc = 0; k_exec = 0;
	/* IPC locale (noyau du 30/10/2026 : canaux, SCM_RIGHTS, memoire partagee, appels 0x70-0x7D) */
	int pr[2];
	if (socketpair (AF_UNIX, SOCK_SEQPACKET, 0, pr) == 0)
	{
		k_ipc = 1;
		close (pr[0]);
		close (pr[1]);
	}
	/* projection a la demande (VM_MAP, noyau du 29/10/2026) : une reservation PROT_NONE dans
	   l'arene du noyau ; sur un ancien noyau, mman.c rend un bloc du tas (ou echoue) */
	void *probe = mmap (0, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (probe != MAP_FAILED)
	{
		k_vm = k_exec = (unsigned long) probe >= 0x5800000000UL;
		munmap (probe, 4096);
	}
	struct utsname u;
	uname (&u);
	printf ("posixtest: %s %s (%s) ; ARMPI : vm %s, threads oui, fichiers oui, process oui, "
		"horloge oui, environnement des fils oui, sockets NetKit, IPC locale %s\n", u.sysname, u.release, u.version, k_vm ? "oui" : "non",
		k_ipc ? "oui" : "non");
	(void) k_sleep; (void) k_poll;
}

static long long ms_now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void abs_in_ms (clockid_t c, long ms, struct timespec *ts)
{
	clock_gettime (c, ts);
	ts->tv_nsec += (ms % 1000) * 1000000L;
	ts->tv_sec += ms / 1000 + ts->tv_nsec / 1000000000L;
	ts->tv_nsec %= 1000000000L;
}

/* (ARMPI) l'objet de memoire partagee qui garde les pages de posixtest.big -> descripteur, -1 : aucun */
static int file_obj_open (int fd, int flags)
{
	struct stat st;
	char name[56];
	unsigned long h = 0xCBF29CE484222325UL;		/* comme file_name, ipc.c */
	for (const char *b = "posixtest.big"; *b; b++)
		h = (h ^ (unsigned char) *b) * 0x100000001B3UL;
	fstat (fd, &st);
	snprintf (name, sizeof name, "/file-%lx-%lx-%llx", h, (long) st.st_size, (unsigned long long) st.st_mtime);
	return shm_open (name, flags, 0);
}

static int file_obj_there (int fd)
{
	int s = file_obj_open (fd, O_RDONLY);
	if (s >= 0)
		close (s);
	return s >= 0;
}

static const char *s_self;

static const unsigned char *s_share_data;
static size_t s_share_n;
static const char *s_share_path;
static int s_share_bad;

/* projeter puis rendre sans cesse : l'objet meurt et renait pendant que les autres l'ouvrent */
static void *t_file_share (void *arg)
{
	(void) arg;
	int fd = open (s_share_path, O_RDONLY);
	for (int i = 0; i < 100; i++)
	{
		unsigned char *m = (unsigned char *) mmap (0, s_share_n, PROT_READ, MAP_PRIVATE, fd, 0);
		if (m == MAP_FAILED || memcmp (m, s_share_data, s_share_n) != 0)
			__atomic_add_fetch (&s_share_bad, 1, __ATOMIC_RELAXED);
		if (m != MAP_FAILED)
			munmap (m, s_share_n);
	}
	close (fd);
	return 0;
}

/* ================================================================================== mem == */
static void group_mem (void)
{
	s_group = "mem";
	CHECK ("sysconf (_SC_PAGESIZE) = 4096", sysconf (_SC_PAGESIZE) == 4096, "%ld", sysconf (_SC_PAGESIZE));
	CHECK ("getpagesize () = 4096", getpagesize () == 4096, "%d", getpagesize ());

	size_t n = 1 << 20;
	unsigned char *p = (unsigned char *) mmap (0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		fail ("mmap anonymous 1 MB", "errno %d", errno);
	else
	{
		int zero = 1;
		for (size_t i = 0; i < n; i += 4096)
			if (p[i])
				zero = 0;
		CHECK ("mmap anonymous: zero-filled, page-aligned", zero && ((unsigned long) p & 0xFFF) == 0, "%p", p);
		for (size_t i = 0; i < n; i++)
			p[i] = (unsigned char) i;
		int ok = 1;
		for (size_t i = 0; i < n; i++)
			if (p[i] != (unsigned char) i)
				ok = 0;
		CHECK ("mmap anonymous: write and read back 1 MB", ok, "mismatch");
		CHECK ("munmap", munmap (p, n) == 0, "errno %d", errno);
	}

	errno = 0;					/* (v78) executable memory: a JIT's */
	if (!k_exec)
		skip ("mmap PROT_EXEC (a JIT's memory)", NOSYS);
	else
	{
	unsigned *x = (unsigned *) mmap (0, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK ("mmap PROT_EXEC (a JIT's memory)", x != MAP_FAILED, "errno %d", errno);
	if (x != MAP_FAILED)
	{
		x[0] = 0x52800540;			/* mov w0, #42 */
		x[1] = 0xD65F03C0;			/* ret */
		__builtin___clear_cache ((char *) x, (char *) (x + 2));
		CHECK ("generated code runs", ((int (*) (void)) x) () == 42, "wrong result");
		CHECK ("mprotect PROT_READ | PROT_EXEC", mprotect (x, 65536, PROT_READ | PROT_EXEC) == 0, "errno %d", errno);
		munmap (x, 65536);
	}
	}

	void *al = 0;
	CHECK ("posix_memalign 64 KB", posix_memalign (&al, 65536, 100000) == 0 && ((unsigned long) al & 0xFFFF) == 0, "failed");
	free (al);

	if (!k_vm)
	{
		skip ("PROT_NONE reservation of 1 GB + mprotect commit", NOSYS);
		skip ("partial munmap (split), MAP_FIXED, MAP_FIXED_NOREPLACE", NOSYS);
		skip ("madvise (MADV_DONTNEED) zero-fills", NOSYS);
		skip ("4 GB-aligned reservation (JSC's pattern)", NOSYS);
	}
	else
	{
		unsigned long g = 1UL << 30;
		unsigned char *r = (unsigned char *) mmap (0, g, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (r == MAP_FAILED)
			fail ("PROT_NONE reservation of 1 GB + mprotect commit", "mmap errno %d", errno);
		else
		{
			int a = mprotect (r + (g / 2), 65536, PROT_READ | PROT_WRITE);
			if (a == 0)
				r[g / 2 + 100] = 42;
			CHECK ("PROT_NONE reservation of 1 GB + mprotect commit", a == 0 && r[g / 2 + 100] == 42, "mprotect errno %d", errno);

			void *f = mmap (r + 65536 * 4, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
			void *nr = mmap (r + 65536 * 4, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
			int split = munmap (r + 65536 * 8, 65536 * 2);
			struct { unsigned long start, end; } q1 = { 0, 0 }, q2 = { 1, 1 };	/* (pas de vm_query) */
			int s1 = 0, s2 = 0;
			CHECK ("partial munmap (split), MAP_FIXED, MAP_FIXED_NOREPLACE",
			       f == (void *) (r + 65536 * 4) && nr == MAP_FAILED && split == 0 && s1 == 0 && s2 == 0 && q1.end <= q2.start,
			       "fixed %p noreplace %p unmap %d query %d %d", f, nr, split, s1, s2);

			unsigned char *c = r + g / 2;
			c[0] = 7;
			int m = madvise (c, 65536, MADV_DONTNEED);
			CHECK ("madvise (MADV_DONTNEED) zero-fills", m == 0 && c[0] == 0 && c[100] == 0, "madvise %d, %d", m, c[0]);
			munmap (r, g);
		}

		/* WTF's OSAllocator: reserve twice the size, keep the aligned part, trim the edges */
		unsigned long four = 4UL << 30;
		unsigned char *big = (unsigned char *) mmap (0, 2 * four, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (big == MAP_FAILED)
			fail ("4 GB-aligned reservation (JSC's pattern)", "mmap 8 GB errno %d", errno);
		else
		{
			unsigned long b = (unsigned long) big, al4 = (b + four - 1) & ~(four - 1);
			int r1 = al4 > b ? munmap (big, al4 - b) : 0;
			int r2 = munmap ((void *) (al4 + four), b + 2 * four - (al4 + four));
			int r3 = mprotect ((void *) al4, 65536, PROT_READ | PROT_WRITE);
			if (r3 == 0)
				*(volatile int *) al4 = 1;
			CHECK ("4 GB-aligned reservation (JSC's pattern)", r1 == 0 && r2 == 0 && r3 == 0, "%d %d %d errno %d", r1, r2, r3, errno);
			munmap ((void *) al4, four);
		}
	}

	/* a file, MAP_PRIVATE */
	const char *path = "/tmp/posixtest.map";
	int fd = open (path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		fail ("mmap of a file (MAP_PRIVATE)", "open errno %d", errno);
	else
	{
		/* (ARMPI) le noyau recolle les regions : 4 096 au plus, un tas qui grandit par mprotect en
		   ajouterait une a chaque fois */
		unsigned long pages = 8192;
		unsigned char *c = (unsigned char *) mmap (0, pages * 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		int ok = c != MAP_FAILED;
		for (unsigned long i = 0; ok && i < pages; i++)
			ok = mprotect (c + i * 4096, 4096, PROT_READ | PROT_WRITE) == 0;
		if (ok)
			c[pages * 4096 - 1] = 42;
		CHECK ("8192 mprotect in a row: neighbouring regions merge", ok && c[pages * 4096 - 1] == 42, "errno %d", errno);
		if (c != MAP_FAILED)
			munmap (c, pages * 4096);

		char buf[3000];
		for (int i = 0; i < (int) sizeof buf; i++)
			buf[i] = (char) ('a' + i % 26);
		write (fd, buf, sizeof buf);
		char *m = (char *) mmap (0, sizeof buf, PROT_READ, MAP_PRIVATE, fd, 0);
		CHECK ("mmap of a file (MAP_PRIVATE)", m != MAP_FAILED && memcmp (m, buf, sizeof buf) == 0, "errno %d", errno);
		if (m != MAP_FAILED)
			munmap (m, sizeof buf);
		errno = 0;
		void *s = mmap (0, sizeof buf, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		CHECK ("mmap MAP_SHARED + PROT_WRITE of a file -> ENOTSUP", s == MAP_FAILED && errno == ENOTSUP, "%p errno %d", s, errno);
		close (fd);
		unlink (path);
	}

	/* (ARMPI) un gros fichier : pages partagees, chacun sa copie a l'ecriture */
	const char *big = "/tmp/posixtest.big";
	fd = open (big, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		fail ("mmap of a big file", "open errno %d", errno);
	else
	{
		static unsigned char data[200000];
		for (unsigned i = 0; i < sizeof data; i++)
			data[i] = (unsigned char) (i * 7 + (i >> 8));
		static const unsigned ret42[] = { 0x52800540, 0xD65F03C0 };	/* mov w0, 42 ; ret */
		memcpy (data + 40960, ret42, sizeof ret42);
		write (fd, data, sizeof data);
		unsigned char *a = (unsigned char *) mmap (0, sizeof data, PROT_READ, MAP_PRIVATE, fd, 0);
		unsigned char *b = (unsigned char *) mmap (0, sizeof data, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		unsigned char *c = (unsigned char *) mmap (0, sizeof data, PROT_READ, MAP_PRIVATE, fd, 0);
		int mapped = a != MAP_FAILED && b != MAP_FAILED && c != MAP_FAILED;
		CHECK ("mmap of a big file: right bytes", mapped && memcmp (a, data, sizeof data) == 0
			&& memcmp (b, data, sizeof data) == 0, "errno %d", errno);
		if (mapped)
		{
			b[5000] ^= 0xFF;
			CHECK ("mmap of a big file: a write stays private", a[5000] == data[5000] && c[5000] == data[5000]
				&& b[5000] == (unsigned char) (data[5000] ^ 0xFF), "%d", b[5000]);
			CHECK ("mprotect RW on a shared read-only page", mprotect (a, 8192, PROT_READ | PROT_WRITE) == 0, "errno %d", errno);
			a[100] ^= 0xFF;
			CHECK ("mprotect then write: still private", c[100] == data[100] && b[100] == data[100]
				&& a[100] == (unsigned char) (data[100] ^ 0xFF), "%d", a[100]);
			/* page deja lue, puis executable : comme une section de DLL sous CoreCLR */
			CHECK ("a page read, then mprotect RX", c[40960] == data[40960]
				&& mprotect (c + 40960, 4096, PROT_READ | PROT_EXEC) == 0, "errno %d", errno);
			CHECK ("code run from the shared pages of a file", ((int (*) (void)) (c + 40960)) () == 42, "");
			unsigned char *e = (unsigned char *) mmap (0, sizeof data, PROT_READ, MAP_PRIVATE, fd, 0);
			CHECK ("mmap again after the writes: the file's bytes", e != MAP_FAILED && memcmp (e, data, sizeof data) == 0, "errno %d", errno);
			if (e != MAP_FAILED)
				munmap (e, sizeof data);
		}
		if (a != MAP_FAILED)
			munmap (a, sizeof data);
		if (b != MAP_FAILED)
			munmap (b, sizeof data);
		if (c != MAP_FAILED)
			munmap (c, sizeof data);

		/* l'objet part avec sa derniere projection, sauf s'il est epingle */
		CHECK ("a file's object goes with its last mapping", !file_obj_there (fd), "");
		CHECK ("file_pin", file_pin (big) == 0 && file_pin (big) == 0 && file_pinned (big), "errno %d", errno);
		CHECK ("a pinned file stays without a mapping", file_obj_there (fd), "");
		unsigned char *f = (unsigned char *) mmap (0, sizeof data, PROT_READ, MAP_PRIVATE, fd, 0);
		CHECK ("mmap of a pinned file: right bytes", f != MAP_FAILED && memcmp (f, data, sizeof data) == 0, "errno %d", errno);
		file_unpin (big);
		CHECK ("unpinned but mapped: the object stays", !file_pinned (big) && file_obj_there (fd), "");
		if (f != MAP_FAILED)
			munmap (f, sizeof data);
		CHECK ("unpinned, last mapping gone: the object goes", !file_obj_there (fd), "");

		s_share_data = data;
		s_share_n = sizeof data;
		s_share_path = big;
		s_share_bad = 0;
		pthread_t t[3];
		for (int i = 0; i < 3; i++)
			pthread_create (&t[i], 0, t_file_share, 0);
		for (int i = 0; i < 3; i++)
			pthread_join (t[i], 0);
		CHECK ("three threads map and unmap one file, 100 times each", s_share_bad == 0 && !file_obj_there (fd), "%d wrong", s_share_bad);

		char *fa[] = { (char *) s_self, "--child", "filemap", (char *) big, 0 };
		pid_t pid = 0;
		int status = 0, there = 1;
		int r = posix_spawn (&pid, s_self, 0, 0, fa, environ);
		if (r == 0)
			waitpid (pid, &status, 0);
		for (int i = 0; i < 20 && there; i++)		/* (the kernel's reaper: every 20 ms) */
		{
			usleep (50000);
			there = file_obj_there (fd);
		}
		CHECK ("a process ends without munmap: the file's object goes", r == 0 && WIFEXITED (status) && WEXITSTATUS (status) == 17
			&& !there, "spawn %d status %x there %d", r, status, there);
		close (fd);
		unlink (big);

		/* sa memoire revient */
		fd = open (big, O_RDWR | O_CREAT | O_TRUNC, 0644);
		for (int i = 0; i < 20; i++)
			write (fd, data, sizeof data);
		long free0 = sysconf (_SC_AVPHYS_PAGES);
		unsigned char *g = (unsigned char *) mmap (0, 20 * sizeof data, PROT_READ, MAP_PRIVATE, fd, 0);
		long free1 = sysconf (_SC_AVPHYS_PAGES);
		if (g != MAP_FAILED)
			munmap (g, 20 * sizeof data);
		long free2 = sysconf (_SC_AVPHYS_PAGES);
		CHECK ("the 4 MB of a mapped file come back with munmap", g != MAP_FAILED && free1 < free0 - 800
			&& free2 >= free0 - 256, "pages %ld -> %ld -> %ld", free0, free1, free2);
		close (fd);
		unlink (big);

		/* un petit fichier n'est gardé que si on le demande */
		fd = open (big, O_RDWR | O_CREAT | O_TRUNC, 0644);
		write (fd, data, 8192);
		unsigned char *h = (unsigned char *) mmap (0, 8192, PROT_READ, MAP_PRIVATE, fd, 0);
		CHECK ("a small file: no object", h != MAP_FAILED && !file_obj_there (fd), "errno %d", errno);
		if (h != MAP_FAILED)
			munmap (h, 8192);
		CHECK ("file_pin of a small file", file_pin (big) == 0 && file_pinned (big), "errno %d", errno);
		int s = file_obj_open (fd, O_RDWR);
		unsigned char *w = s >= 0 ? (unsigned char *) mmap (0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, s, 0) : (unsigned char *) MAP_FAILED;
		h = (unsigned char *) mmap (0, 8192, PROT_READ, MAP_PRIVATE, fd, 0);
		if (w != MAP_FAILED)
			w[10] = (unsigned char) (data[10] ^ 0xFF);
		CHECK ("mmap of a pinned small file: the object's pages", w != MAP_FAILED && h != MAP_FAILED && h[10] == w[10], "errno %d", errno);
		if (w != MAP_FAILED)
			munmap (w, 4096);
		if (h != MAP_FAILED)
			munmap (h, 8192);
		if (s >= 0)
			close (s);
		file_unpin (big);
		CHECK ("file_unpin of a small file: the object goes", !file_pinned (big) && !file_obj_there (fd), "");
		close (fd);
		unlink (big);
	}
}

/* (ARMPI) la boucle locale : 127.0.0.1, sans reseau */
static void *loop_writer (void *arg)
{
	int s = *(int *) arg;
	static char b[8192];
	long sent = 0;
	for (unsigned i = 0; i < sizeof b; i++)
		b[i] = (char) (i * 13);
	while (sent < 2000000)
	{
		ssize_t r = send (s, b, sizeof b, 0);
		if (r <= 0)
			break;
		sent += r;
	}
	close (s);
	return (void *) sent;
}

static void test_loopback (void)
{
	int l = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons (47123);
	a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	int on = 1;
	setsockopt (l, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
	CHECK ("127.0.0.1: bind and listen", l >= 0 && bind (l, (struct sockaddr *) &a, sizeof a) == 0 && listen (l, 4) == 0, "errno %d", errno);
	int c = socket (AF_INET, SOCK_STREAM, 0);
	int ok = connect (c, (struct sockaddr *) &a, sizeof a) == 0;
	CHECK ("127.0.0.1: connect", ok, "errno %d", errno);
	if (!ok)
	{
		close (c);
		close (l);
		return;
	}
	int s = accept (l, 0, 0);
	CHECK ("127.0.0.1: accept", s >= 0, "errno %d", errno);
	char m[8];
	CHECK ("127.0.0.1: a message each way", send (c, "ping", 4, 0) == 4 && recv (s, m, 4, MSG_WAITALL) == 4 && memcmp (m, "ping", 4) == 0
		&& send (s, "pong", 4, 0) == 4 && recv (c, m, 4, MSG_WAITALL) == 4 && memcmp (m, "pong", 4) == 0, "errno %d", errno);
	pthread_t t;
	pthread_create (&t, 0, loop_writer, &s);
	static char r[8192];
	long got = 0;
	int same = 1;
	for (;;)
	{
		ssize_t n = recv (c, r, sizeof r, 0);
		if (n <= 0)
			break;
		for (ssize_t i = 0; i < n; i++)
			same &= r[i] == (char) ((got + i) % 8192 * 13);
		got += n;
	}
	void *sent = 0;
	pthread_join (t, &sent);
	CHECK ("127.0.0.1: 2 MB, every byte", got == (long) sent && got >= 2000000 && same, "%ld of %ld", got, (long) sent);
	close (c);

	/* demi-fermeture : le pair lit la fin, puis repond encore */
	c = socket (AF_INET, SOCK_STREAM, 0);
	connect (c, (struct sockaddr *) &a, sizeof a);
	s = accept (l, 0, 0);
	CHECK ("shutdown (SHUT_WR): the peer reads what was sent, then the end", send (c, "last", 4, 0) == 4 && shutdown (c, SHUT_WR) == 0
		&& recv (s, m, 8, MSG_WAITALL) == 4 && memcmp (m, "last", 4) == 0 && recv (s, m, 8, 0) == 0, "errno %d", errno);
	errno = 0;
	CHECK ("send after SHUT_WR -> EPIPE", send (c, "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE, "errno %d", errno);
	CHECK ("half closed: the peer still answers", send (s, "back", 4, 0) == 4 && recv (c, m, 4, MSG_WAITALL) == 4
		&& memcmp (m, "back", 4) == 0, "errno %d", errno);
	CHECK ("shutdown twice: no error", shutdown (c, SHUT_WR) == 0, "errno %d", errno);
	close (s);
	CHECK ("then the end, from the peer", recv (c, m, 4, 0) == 0, "errno %d", errno);
	close (c);
	c = socket (AF_INET, SOCK_STREAM, 0);
	connect (c, (struct sockaddr *) &a, sizeof a);
	s = accept (l, 0, 0);
	CHECK ("shutdown (SHUT_RD): recv returns 0", shutdown (c, SHUT_RD) == 0 && recv (c, m, 4, 0) == 0, "errno %d", errno);
	CHECK ("shutdown with a wrong how -> EINVAL", shutdown (c, 7) == -1 && errno == EINVAL, "errno %d", errno);
	close (s);
	close (c);
	c = socket (AF_INET, SOCK_STREAM, 0);
	errno = 0;
	CHECK ("shutdown of an unconnected socket -> ENOTCONN", shutdown (c, SHUT_WR) == -1 && errno == ENOTCONN, "errno %d", errno);
	close (c);
	close (l);
	struct addrinfo *ai = 0, hints;
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	int e = getaddrinfo ("localhost", "80", &hints, &ai);
	CHECK ("localhost resolves to 127.0.0.1", e == 0 && ai && ((struct sockaddr_in *) ai->ai_addr)->sin_addr.s_addr == htonl (INADDR_LOOPBACK), "%d", e);
	if (ai)
		freeaddrinfo (ai);
}

/* =============================================================================== thread == */
static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static long s_counter;
static __thread int t_local = 1;

static void *t_echo (void *a) { return a; }

static void *t_count (void *a)
{
	(void) a;
	for (int i = 0; i < 2000; i++)
	{
		pthread_mutex_lock (&s_mx);
		long v = s_counter;
		if ((i & 63) == 0)
			sched_yield ();
		s_counter = v + 1;
		pthread_mutex_unlock (&s_mx);
	}
	return 0;
}

static pthread_mutex_t s_pm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_pc = PTHREAD_COND_INITIALIZER;
static int s_turn, s_rounds;
static void *t_pong (void *a)
{
	(void) a;
	pthread_mutex_lock (&s_pm);
	while (s_rounds < 10000)
	{
		while (s_turn != 1)
			pthread_cond_wait (&s_pc, &s_pm);
		s_turn = 0;
		s_rounds++;
		pthread_cond_broadcast (&s_pc);
	}
	pthread_mutex_unlock (&s_pm);
	return 0;
}

static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static int s_onceRuns;
static void once_fn (void) { s_onceRuns++; sched_yield (); }
static void *t_once (void *a) { (void) a; pthread_once (&s_once, once_fn); return 0; }

static pthread_key_t s_key;
static volatile int s_dtorSeen;
static void key_dtor (void *v) { if (v == (void *) 0x1234) s_dtorSeen = 1; }
static void *t_key (void *a) { (void) a; pthread_setspecific (s_key, (void *) 0x1234); return pthread_getspecific (s_key); }

static void *t_tls (void *a)
{
	int me = (int) (long) a;
	t_local = me;
	errno = 1000 + me;
	for (int i = 0; i < 200; i++)
	{
		sched_yield ();
		if (t_local != me || errno != 1000 + me)
			return (void *) 1;
	}
	return 0;
}

static pthread_rwlock_t s_rw = PTHREAD_RWLOCK_INITIALIZER;
static volatile int s_readers, s_maxReaders, s_writerBad;
static void *t_reader (void *a)
{
	(void) a;
	for (int i = 0; i < 50; i++)
	{
		pthread_rwlock_rdlock (&s_rw);
		int n = __atomic_add_fetch (&s_readers, 1, __ATOMIC_SEQ_CST);
		if (n > s_maxReaders)
			s_maxReaders = n;
		/* (the first rounds hold the lock a little: another reader can come in meanwhile) */
		for (int k = 0; i < 3 && k < 50 && s_readers < 2; k++)
			usleep (1000);
		if (s_readers > s_maxReaders)
			s_maxReaders = s_readers;
		sched_yield ();
		__atomic_sub_fetch (&s_readers, 1, __ATOMIC_SEQ_CST);
		pthread_rwlock_unlock (&s_rw);
	}
	return 0;
}
static void *t_writer (void *a)
{
	(void) a;
	for (int i = 0; i < 50; i++)
	{
		pthread_rwlock_wrlock (&s_rw);
		if (s_readers != 0)
			s_writerBad = 1;
		sched_yield ();
		pthread_rwlock_unlock (&s_rw);
	}
	return 0;
}

static sem_t s_items;
static volatile int s_consumed;
static void *t_consume (void *a)
{
	(void) a;
	for (int i = 0; i < 1000; i++)
	{
		sem_wait (&s_items);
		s_consumed++;
	}
	return 0;
}

static pthread_barrier_t s_bar;
static volatile int s_serial;
static void *t_barrier (void *a)
{
	(void) a;
	for (int r = 0; r < 10; r++)
		if (pthread_barrier_wait (&s_bar) == PTHREAD_BARRIER_SERIAL_THREAD)
			__atomic_add_fetch (&s_serial, 1, __ATOMIC_SEQ_CST);
	return 0;
}

static void *t_bounds (void *a)
{
	(void) a;
	int local;
	pthread_attr_t at;
	void *lo;
	size_t sz;
	if (pthread_getattr_np (pthread_self (), &at) != 0 || pthread_attr_getstack (&at, &lo, &sz) != 0)
		return (void *) 1;
	return (char *) &local >= (char *) lo && (char *) &local < (char *) lo + sz ? 0 : (void *) 2;
}

static volatile int s_detachedRan;
static void *t_detached (void *a) { (void) a; s_detachedRan = 1; return 0; }

static pthread_spinlock_t s_spin;
static long s_spinCount;
static void *t_spin (void *a)
{
	(void) a;
	for (int i = 0; i < 1000; i++)
	{
		pthread_spin_lock (&s_spin);
		s_spinCount++;
		pthread_spin_unlock (&s_spin);
	}
	return 0;
}

static void group_thread (void)
{
	s_group = "thread";
	pthread_t t[32];

	/* 100 threads, in waves of 25 */
	int ok = 1, made = 0;
	for (int w = 0; w < 4; w++)
	{
		for (int i = 0; i < 25; i++)
			if (pthread_create (&t[i], 0, t_echo, (void *) (long) (w * 25 + i + 1)) != 0)
				ok = 0;
			else
				made++;
		for (int i = 0; i < 25; i++)
		{
			void *r = 0;
			if (pthread_join (t[i], &r) != 0 || r != (void *) (long) (w * 25 + i + 1))
				ok = 0;
		}
	}
	CHECK ("100 threads in waves of 25: create, join, results", ok && made == 100, "%d made", made);

	s_counter = 0;
	for (int i = 0; i < 8; i++)
		pthread_create (&t[i], 0, t_count, 0);
	for (int i = 0; i < 8; i++)
		pthread_join (t[i], 0);
	CHECK ("mutex: a counter under contention (8 x 2000)", s_counter == 16000, "%ld", s_counter);

	pthread_mutexattr_t ma;
	pthread_mutex_t rm, em, tm = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutexattr_init (&ma);
	pthread_mutexattr_settype (&ma, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init (&rm, &ma);
	int r1 = pthread_mutex_lock (&rm), r2 = pthread_mutex_lock (&rm), r3 = pthread_mutex_trylock (&rm);
	int u1 = pthread_mutex_unlock (&rm), u2 = pthread_mutex_unlock (&rm), u3 = pthread_mutex_unlock (&rm);
	int u4 = pthread_mutex_unlock (&rm);
	CHECK ("mutex: recursive", !r1 && !r2 && !r3 && !u1 && !u2 && !u3 && u4 == EPERM, "%d %d %d %d %d %d %d", r1, r2, r3, u1, u2, u3, u4);
	pthread_mutexattr_settype (&ma, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init (&em, &ma);
	pthread_mutex_lock (&em);
	int e1 = pthread_mutex_lock (&em);
	pthread_mutex_unlock (&em);
	int e2 = pthread_mutex_unlock (&em);
	CHECK ("mutex: errorcheck (EDEADLK, EPERM)", e1 == EDEADLK && e2 == EPERM, "%d %d", e1, e2);

	pthread_mutex_lock (&tm);
	struct timespec ts;
	abs_in_ms (CLOCK_REALTIME, 50, &ts);
	long long t0 = ms_now ();
	int tl = pthread_mutex_timedlock (&tm, &ts);
	long long dt = ms_now () - t0;
	pthread_mutex_unlock (&tm);
	CHECK ("mutex: timedlock times out (50 ms)", tl == ETIMEDOUT && dt >= 40 && dt < 500, "%d after %lld ms", tl, dt);

	s_turn = 0;
	s_rounds = 0;
	pthread_create (&t[0], 0, t_pong, 0);
	pthread_mutex_lock (&s_pm);
	while (s_rounds < 10000)
	{
		while (s_turn != 0 && s_rounds < 10000)
			pthread_cond_wait (&s_pc, &s_pm);
		if (s_rounds >= 10000)
			break;
		s_turn = 1;
		pthread_cond_broadcast (&s_pc);
	}
	pthread_mutex_unlock (&s_pm);
	pthread_join (t[0], 0);
	CHECK ("condition variable: ping-pong 10000 times", s_rounds == 10000, "%d", s_rounds);

	pthread_cond_t c1 = PTHREAD_COND_INITIALIZER, c2;
	pthread_condattr_t ca;
	pthread_condattr_init (&ca);
	pthread_condattr_setclock (&ca, CLOCK_MONOTONIC);
	pthread_cond_init (&c2, &ca);
	pthread_mutex_lock (&tm);
	abs_in_ms (CLOCK_REALTIME, 100, &ts);
	t0 = ms_now ();
	int w1 = pthread_cond_timedwait (&c1, &tm, &ts);
	long long d1 = ms_now () - t0;
	abs_in_ms (CLOCK_MONOTONIC, 100, &ts);
	t0 = ms_now ();
	int w2 = pthread_cond_timedwait (&c2, &tm, &ts);
	long long d2 = ms_now () - t0;
	pthread_mutex_unlock (&tm);
	CHECK ("cond timedwait 100 ms, CLOCK_REALTIME", w1 == ETIMEDOUT && d1 >= 90 && d1 < 250, "%d after %lld ms", w1, d1);
	CHECK ("cond timedwait 100 ms, CLOCK_MONOTONIC (condattr_setclock)", w2 == ETIMEDOUT && d2 >= 90 && d2 < 250, "%d after %lld ms", w2, d2);

	s_readers = s_maxReaders = s_writerBad = 0;
	for (int i = 0; i < 4; i++)
		pthread_create (&t[i], 0, t_reader, 0);
	pthread_create (&t[4], 0, t_writer, 0);
	for (int i = 0; i < 5; i++)
		pthread_join (t[i], 0);
	CHECK ("rwlock: readers together, the writer alone", s_maxReaders >= 2 && !s_writerBad, "max readers %d, writer saw readers %d", s_maxReaders, s_writerBad);

	for (int i = 0; i < 10; i++)
		pthread_create (&t[i], 0, t_once, 0);
	for (int i = 0; i < 10; i++)
		pthread_join (t[i], 0);
	CHECK ("pthread_once from 10 threads: once", s_onceRuns == 1, "%d runs", s_onceRuns);

	pthread_key_create (&s_key, key_dtor);
	void *kv = 0;
	pthread_create (&t[0], 0, t_key, 0);
	pthread_join (t[0], &kv);
	CHECK ("keys: a value per thread, its destructor at the end", kv == (void *) 0x1234 && s_dtorSeen && pthread_getspecific (s_key) == 0,
	       "value %p, destructor %d", kv, s_dtorSeen);
	pthread_key_delete (s_key);

	errno = 0;
	t_local = 99;
	ok = 1;
	for (int i = 0; i < 4; i++)
		pthread_create (&t[i], 0, t_tls, (void *) (long) (i + 1));
	for (int i = 0; i < 4; i++)
	{
		void *r;
		pthread_join (t[i], &r);
		if (r != 0)
			ok = 0;
	}
	CHECK ("__thread: one copy per thread", ok && t_local == 99, "main sees %d", t_local);
	CHECK ("errno: one per thread", ok && errno == 0, "main's errno %d", errno);

	sem_init (&s_items, 0, 0);
	s_consumed = 0;
	pthread_create (&t[0], 0, t_consume, 0);
	for (int i = 0; i < 1000; i++)
	{
		sem_post (&s_items);
		if ((i & 31) == 0)
			sched_yield ();
	}
	pthread_join (t[0], 0);
	abs_in_ms (CLOCK_REALTIME, 30, &ts);
	int sw = sem_timedwait (&s_items, &ts);
	CHECK ("semaphores: 1000 items, then a timed wait times out", s_consumed == 1000 && sw == -1 && errno == ETIMEDOUT, "%d consumed, %d", s_consumed, sw);

	pthread_barrier_init (&s_bar, 0, 5);
	s_serial = 0;
	for (int i = 0; i < 5; i++)
		pthread_create (&t[i], 0, t_barrier, 0);
	for (int i = 0; i < 5; i++)
		pthread_join (t[i], 0);
	CHECK ("barrier: 5 threads x 10 rounds, one serial a round", s_serial == 10, "%d", s_serial);

	void *br = (void *) 9;
	pthread_attr_t at;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, 256 * 1024);
	pthread_create (&t[0], &at, t_bounds, 0);
	pthread_join (t[0], &br);
	int local;
	void *lo;
	size_t sz;
	pthread_attr_t mat;
	pthread_getattr_np (pthread_self (), &mat);
	pthread_attr_getstack (&mat, &lo, &sz);
	int mainOk = (char *) &local >= (char *) lo && (char *) &local < (char *) lo + sz;
	if (!k_threads)
		CHECK ("pthread_getattr_np: the stack holds a local (v67 estimate)", br == 0 && mainOk, "thread %p, main %d", br, mainOk);
	else
		CHECK ("pthread_getattr_np: the stack holds a local (thread_info)", br == 0 && mainOk, "thread %p, main %d", br, mainOk);

	pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
	pthread_create (&t[0], &at, t_detached, 0);
	for (int i = 0; i < 200 && !s_detachedRan; i++)
		usleep (1000);
	CHECK ("a detached thread runs", s_detachedRan, "not run");

	pthread_spin_init (&s_spin, 0);
	for (int i = 0; i < 4; i++)
		pthread_create (&t[i], 0, t_spin, 0);
	for (int i = 0; i < 4; i++)
		pthread_join (t[i], 0);
	CHECK ("spin lock counter (4 x 1000)", s_spinCount == 4000, "%ld", s_spinCount);

	char name[32];
	pthread_setname_np (pthread_self (), "tester");
	pthread_getname_np (pthread_self (), name, sizeof name);
	CHECK ("self, equal, setname / getname, sched_yield",
	       pthread_equal (pthread_self (), pthread_self ()) && !strcmp (name, "tester") && sched_yield () == 0, "name '%s'", name);
}

/* ================================================================================= file == */
/* (ARMPI) l'ecriture de fatfs : clusters neufs sans zeros s'ils sont ecrits en entier, FAT une fois
   par requete, clusters contigus en une commande. Chaque octet est relu */
static unsigned char file_pat (long i, int seed)
{
	return (unsigned char) (i * 7 + (i >> 12) * 13 + seed);
}

static void file_put (int fd, long from, long n, int seed)
{
	static unsigned char b[140000];
	for (long i = 0; i < n; i++)
		b[i] = file_pat (from + i, seed);
	lseek (fd, from, SEEK_SET);
	write (fd, b, (size_t) n);
}

/* seed < 0 : des zeros -> 1 si [from, to[ est bon */
static int file_same (const char *path, long from, long to, int seed)
{
	static unsigned char b[8000];
	int fd = open (path, O_RDONLY), good = fd >= 0;
	if (good)
		lseek (fd, from, SEEK_SET);
	for (long at = from; good && at < to; )
	{
		long want = to - at < (long) sizeof b ? to - at : (long) sizeof b;
		good = read (fd, b, (size_t) want) == want;
		for (long i = 0; good && i < want; i++)
			good = b[i] == (seed < 0 ? 0 : file_pat (at + i, seed));
		at += want;
	}
	if (fd >= 0)
		close (fd);
	return good;
}

static void file_alloc_check (const char *dir)
{
	static const long sizes[] = { 4096, 70000, 1, 8192, 12345, 65536, 511, 131072, 4095, 8193 };
	char a[400], b[400];
	snprintf (a, sizeof a, "%s/c.dat", dir);
	snprintf (b, sizeof b, "%s/d.dat", dir);
	unlink (a);
	unlink (b);
	int fa = open (a, O_RDWR | O_CREAT, 0644), fb = open (b, O_RDWR | O_CREAT, 0644);
	long at = 0;
	for (int round = 0; round < 3; round++)		/* (entrelaces : ils se fragmentent) */
		for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
		{
			file_put (fa, at, sizes[i], 1);
			file_put (fb, at, sizes[i], 2);
			at += sizes[i];
		}
	close (fa);
	close (fb);
	CHECK ("two files written in turn: every byte", file_same (a, 0, at, 1) && file_same (b, 0, at, 2), "");
	struct stat st;
	CHECK ("two files written in turn: their size", stat (a, &st) == 0 && st.st_size == at, "%ld %ld", (long) st.st_size, at);

	fa = open (a, O_RDWR);				/* sur place, a cheval sur des clusters */
	file_put (fa, 5000, 100000, 3);
	close (fa);
	CHECK ("rewritten in the middle: every byte", file_same (a, 0, 5000, 1) && file_same (a, 5000, 105000, 3)
		&& file_same (a, 105000, at, 1), "");
	unlink (a);
	unlink (b);

	fa = open (a, O_RDWR | O_CREAT, 0644);		/* un trou : des zeros, jamais d'anciens octets */
	file_put (fa, 0, 100, 4);
	file_put (fa, 40960, 8192, 5);
	file_put (fa, 65536 + 300, 9000, 6);
	close (fa);
	CHECK ("written past the end: the hole reads as zeros", file_same (a, 0, 100, 4) && file_same (a, 100, 40960, -1)
		&& file_same (a, 40960, 49152, 5) && file_same (a, 49152, 65836, -1) && file_same (a, 65836, 74836, 6), "");
	unlink (a);

	fa = open (a, O_RDWR | O_CREAT, 0644);		/* rouvert, il grandit a cote de son voisin */
	file_put (fa, 0, 4000, 7);
	close (fa);
	fb = open (b, O_RDWR | O_CREAT, 0644);
	file_put (fb, 0, 6000, 8);
	close (fb);
	fa = open (a, O_RDWR);
	file_put (fa, 4000, 20000, 7);
	close (fa);
	CHECK ("reopened and grown: its neighbour intact", file_same (a, 0, 24000, 7) && file_same (b, 0, 6000, 8), "");
	fa = open (a, O_WRONLY | O_TRUNC);
	file_put (fa, 0, 100, 9);
	close (fa);
	CHECK ("truncated: its neighbour intact", file_same (a, 0, 100, 9) && file_same (b, 0, 6000, 8), "");
	unlink (b);
	fb = open (b, O_RDWR | O_CREAT, 0644);
	file_put (fb, 0, 9000, 10);
	close (fb);
	CHECK ("a new file takes no cluster of the truncated one", file_same (a, 0, 100, 9) && file_same (b, 0, 9000, 10), "");
	unlink (a);
	unlink (b);
}

static void file_group_in (const char *dir)
{
	char p[400], q[400], d2[400];
	char tag[64];
	snprintf (tag, sizeof tag, "file %s", dir);
	s_group = tag;
	mkdir (dir, 0755);
	snprintf (p, sizeof p, "%s/a.dat", dir);
	snprintf (q, sizeof q, "%s/b.dat", dir);
	unlink (p);
	unlink (q);
	file_alloc_check (dir);

	int fd = open (p, O_RDWR | O_CREAT | O_EXCL, 0644);
	int fd2 = open (p, O_RDWR | O_CREAT | O_EXCL, 0644);
	CHECK ("open O_CREAT|O_EXCL: new, then EEXIST", fd >= 0 && fd2 < 0 && errno == EEXIST, "%d %d errno %d", fd, fd2, errno);
	if (fd < 0)
		return;

	static unsigned char buf[100000], back[100000];
	for (int i = 0; i < (int) sizeof buf; i++)
		buf[i] = (unsigned char) (i * 7 + 3);
	ssize_t w = write (fd, buf, sizeof buf);
	off_t o = lseek (fd, 0, SEEK_SET);
	ssize_t r = read (fd, back, sizeof back);
	CHECK ("write / lseek / read 100 KB", w == (ssize_t) sizeof buf && o == 0 && r == w && !memcmp (buf, back, sizeof buf), "%zd %ld %zd", w, (long) o, r);

	/* pread / pwrite at random offsets, two descriptors, against a model */
	/* (the old calls hold a writable file per descriptor: one descriptor there) */
	int fdb = k_files ? open (p, O_RDWR) : dup (fd);
	unsigned seed = 12345;
	int ok = fdb >= 0;
	for (int k = 0; k < 1000 && ok; k++)
	{
		seed = seed * 1103515245 + 12345;
		off_t at = (off_t) (seed % (sizeof buf - 200));
		int len = (int) (seed >> 20) % 200 + 1;
		int f = (k & 1) ? fd : fdb;
		if (k % 3 == 0)
		{
			for (int j = 0; j < len; j++)
				buf[at + j] = (unsigned char) (k + j);
			if (pwrite (f, buf + at, (size_t) len, at) != len)
				ok = 0;
		}
		else if (pread (f, back, (size_t) len, at) != len || memcmp (back, buf + at, (size_t) len))
			ok = 0;
	}
	CHECK (k_files ? "pread / pwrite: 1000 random operations on two descriptors" : "pread / pwrite: 1000 random operations (one description)", ok, "mismatch");
	if (fdb >= 0)
		close (fdb);

	struct stat st;
	{
		/* pwrite au-dela de la fin : le trou va de la fin a off, la position ne bouge pas */
		ftruncate (fd, 0);
		lseek (fd, 0, SEEK_SET);
		char head[4] = "MZ!", gap[4], tail[4] = "end", back2[4];
		pwrite (fd, head, 4, 0);
		pwrite (fd, tail, 4, 20);
		pread (fd, back2, 4, 0);
		pread (fd, gap, 4, 8);
		CHECK ("pwrite past the end keeps the start", !memcmp (back2, head, 4) && !memcmp (gap, "\0\0\0\0", 4) && lseek (fd, 0, SEEK_CUR) == 0, "%.4s", back2);
	}
	CHECK ("ftruncate grow (zeros) and fstat", ftruncate (fd, 200000) == 0 && fstat (fd, &st) == 0 && st.st_size == 200000 &&
	       pread (fd, back, 100, 150000) == 100 && back[0] == 0 && back[99] == 0, "size %ld", (long) st.st_size);
	CHECK ("ftruncate shrink", ftruncate (fd, 1000) == 0 && fstat (fd, &st) == 0 && st.st_size == 1000, "size %ld", (long) st.st_size);
	CHECK ("fsync", fsync (fd) == 0, "errno %d", errno);

	int dfd = dup (fd);
	lseek (fd, 10, SEEK_SET);
	CHECK ("dup shares the offset", dfd >= 0 && lseek (dfd, 0, SEEK_CUR) == 10, "%ld", (long) lseek (dfd, 0, SEEK_CUR));
	int d2fd = dup2 (fd, 50);
	CHECK ("dup2 onto 50", d2fd == 50 && lseek (50, 0, SEEK_CUR) == 10, "%d", d2fd);
	close (50);
	close (dfd);
	int fl = fcntl (fd, F_GETFL);
	fcntl (fd, F_SETFD, FD_CLOEXEC);
	CHECK ("fcntl F_GETFL / F_SETFD", (fl & O_ACCMODE) == O_RDWR && fcntl (fd, F_GETFD) == FD_CLOEXEC, "fl %x", fl);
	close (fd);

	/* O_APPEND from two descriptors */
	int a1 = open (q, O_WRONLY | O_CREAT | O_APPEND, 0644), a2 = open (q, O_WRONLY | O_APPEND);
	write (a1, "one ", 4);
	write (a2, "two ", 4);
	write (a1, "three", 5);
	close (a1);
	close (a2);
	char tb[32] = "";
	int qfd = open (q, O_RDONLY);
	ssize_t qn = qfd >= 0 ? read (qfd, tb, sizeof tb - 1) : -1;
	if (qfd >= 0)
		close (qfd);
	if (!k_files)
		skip ("O_APPEND from two descriptors", NOSYS " (the old calls hold a file per descriptor)");
	else
		CHECK ("O_APPEND from two descriptors", qn == 13 && !memcmp (tb, "one two three", 13), "'%s'", tb);

	/* rename onto an existing name */
	CHECK ("rename replaces its target", rename (p, q) == 0 && stat (p, &st) != 0 && stat (q, &st) == 0 && st.st_size == 1000,
	       "errno %d size %ld", errno, (long) st.st_size);

	/* (ARMPI) nftw : un dossier et ses deux fichiers, le dossier en dernier */
	{
		char dt[300], ft[320];
		snprintf (dt, sizeof dt, "%s.w", q);
		mkdir (dt, 0755);
		for (int i = 0; i < 2; i++)
		{
			snprintf (ft, sizeof ft, "%s/f%d", dt, i);
			int w = open (ft, O_CREAT | O_WRONLY, 0644);
			if (w >= 0)
				close (w);
		}
		s_nftw_f = s_nftw_d = 0;
		int r = nftw (dt, nftw_compte, 4, FTW_DEPTH);
		CHECK ("nftw: two files, then the directory", r == 0 && s_nftw_f == 2 && s_nftw_d == 1, "%d %d %d", r, s_nftw_f, s_nftw_d);
		for (int i = 0; i < 2; i++)
		{
			snprintf (ft, sizeof ft, "%s/f%d", dt, i);
			unlink (ft);
		}
		rmdir (dt);
	}

	/* (ARMPI) rename sans copie : la date gardee, un fichier ouvert qui suit, un repertoire deplace */
	{
		char r1[300], r2[300], d1[300], d2[300], x1[320], x2[320];
		snprintf (r1, sizeof r1, "%s.r1", q);
		snprintf (r2, sizeof r2, "%s.R2", q);
		snprintf (d1, sizeof d1, "%s.d1", q);
		snprintf (d2, sizeof d2, "%s.d2", q);
		snprintf (x1, sizeof x1, "%s/x", d1);
		snprintf (x2, sizeof x2, "%s/sub/x", d2);
		long long old = (long long) time (0) - 3600;
		int rf = open (r1, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if (rf >= 0)
		{
			write (rf, "12345", 5);
			close (rf);
		}
		struct utimbuf ub = { (time_t) old, (time_t) old };
		utime (r1, &ub);
		int rr = rename (r1, r2);
		int ro = stat (r2, &st);
		long long dd = (long long) st.st_mtime - old;
		CHECK ("rename keeps size and date", rr == 0 && ro == 0 && st.st_size == 5 && dd >= -2 && dd <= 2 && stat (r1, &st) != 0,
		       "%d %d size %ld delta %lld", rr, ro, (long) st.st_size, dd);
		rf = open (r2, O_WRONLY | O_APPEND);
		rr = rename (r2, r1);
		if (rf >= 0)
		{
			write (rf, "678", 3);
			close (rf);
		}
		ro = stat (r1, &st);
		CHECK ("rename of an open file: the data follows", rf >= 0 && rr == 0 && ro == 0 && st.st_size == 8, "%d %d size %ld", rr, ro, (long) st.st_size);
		unlink (r1);
		char sub[310];
		snprintf (sub, sizeof sub, "%s/sub", d2);
		int m1 = mkdir (d1, 0755), m2 = mkdir (d2, 0755);
		rf = open (x1, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if (rf >= 0)
		{
			write (rf, "ab", 2);
			close (rf);
		}
		rr = rename (d1, sub);
		ro = stat (x2, &st);
		CHECK ("rename of a directory, into another one", m1 == 0 && m2 == 0 && rr == 0 && ro == 0 && st.st_size == 2 && stat (d1, &st) != 0,
		       "%d %d %d %d errno %d", m1, m2, rr, ro, errno);
		char in[320];
		snprintf (in, sizeof in, "%s/sub/deep", d2);
		rr = rename (sub, in);
		CHECK ("a directory does not go into itself", rr == -1 && errno == EINVAL, "%d errno %d", rr, errno);
		unlink (x2);
		rmdir (sub);
		rmdir (d2);
	}

	/* unlink while open */
	int u = open (q, O_RDONLY);
	int ur = unlink (q);
	char ub[16];
	if (!k_files)
		skip ("unlink of an open file: still readable, gone from its directory", NOSYS);
	else
		CHECK ("unlink of an open file: still readable, gone from its directory",
		       u >= 0 && ur == 0 && pread (u, ub, 16, 0) == 16 && stat (q, &st) != 0, "%d %d", u, ur);
	if (u >= 0)
		close (u);
	unlink (q);

	/* directories */
	snprintf (d2, sizeof d2, "%s/sub", dir);
	rmdir (d2);
	int m1 = mkdir (d2, 0755), m2 = mkdir (d2, 0755);
	int e2 = errno;
	CHECK ("mkdir, then EEXIST", m1 == 0 && m2 == -1 && e2 == EEXIST, "%d %d errno %d", m1, m2, e2);
	char f1[700], f2[700];
	snprintf (f1, sizeof f1, "%s/x.txt", d2);
	char longname[201];
	memset (longname, 'n', 200);
	longname[200] = '\0';
	snprintf (f2, sizeof f2, "%s/%.200s", d2, longname);
	close (open (f1, O_WRONLY | O_CREAT, 0644));
	int lfd = open (f2, O_WRONLY | O_CREAT, 0644);
	if (lfd >= 0)
		close (lfd);
	int rn = rmdir (d2);
	CHECK ("rmdir of a full directory -> ENOTEMPTY", rn == -1 && errno == ENOTEMPTY, "%d errno %d", rn, errno);
	DIR *dd = opendir (d2);
	int seenX = 0, seenLong = 0, n = 0;
	struct dirent *e;
	while (dd && (e = readdir (dd)) != 0)
	{
		n++;
		if (!strcasecmp (e->d_name, "x.txt") && e->d_type == DT_REG)
			seenX = 1;
		if (strlen (e->d_name) == 200)
			seenLong = 1;
	}
	if (dd)
		closedir (dd);
	CHECK ("opendir / readdir", seenX, "%d entries", n);
	if (!k_files)
		skip ("readdir: a 200-character name", NOSYS);
	else if (!strncasecmp (dir, "RAM0:", 5) || !strncmp (dir, "/tmp", 4))
		skip ("readdir: a 200-character name", "ARMPI : RamFS garde 63 octets par nom ; essayer posixtest file SD0:/tmp");
	else
		CHECK ("readdir: a 200-character name", seenLong && lfd >= 0, "entries %d", n);
	unlink (f1);
	unlink (f2);
	CHECK ("rmdir of an empty directory", rmdir (d2) == 0, "errno %d", errno);

	/* access, realpath, getcwd / chdir */
	snprintf (p, sizeof p, "%s/c.txt", dir);
	close (open (p, O_WRONLY | O_CREAT, 0644));
	CHECK ("access F_OK / W_OK, a missing file ENOENT", access (p, F_OK) == 0 && access (p, W_OK) == 0 &&
	       access ("/tmp/posixtest-missing", F_OK) == -1 && errno == ENOENT, "errno %d", errno);
	char cwd[300], rp[300], back2[300];
	getcwd (cwd, sizeof cwd);
	int cd = chdir (dir);
	char *rr = realpath ("c.txt", rp);
	getcwd (back2, sizeof back2);
	chdir (cwd);
	CHECK ("chdir, getcwd, realpath of a relative name", cd == 0 && rr != 0 && strstr (rp, "/c.txt") && strchr (rp, ':'),
	       "chdir %d, realpath '%s', cwd '%s'", cd, rr ? rp : "(null)", back2);
	unlink (p);

	/* stdio */
	snprintf (p, sizeof p, "%s/s.txt", dir);
	FILE *f = fopen (p, "w+");
	char line[64] = "";
	long pos = -1;
	if (f)
	{
		fprintf (f, "hello %d\nworld\n", 42);
		fseek (f, 0, SEEK_SET);
		fgets (line, sizeof line, f);
		fseek (f, 0, SEEK_END);
		pos = ftell (f);
		fclose (f);
	}
	CHECK ("stdio: fopen, fprintf, fseek, fgets, ftell", f && !strcmp (line, "hello 42\n") && pos == 15, "'%s' %ld", line, pos);
	if (k_files && stat (p, &st) == 0)
	{
		long long now = time (0);
		CHECK ("stat: mtime close to now", st.st_mtime > now - 5 && st.st_mtime < now + 5, "mtime %lld now %lld", (long long) st.st_mtime, now);
		/* (ARMPI) ce que make attend : utime garde sa date, une ecriture la remet a maintenant ;
		   FAT : 2 s de precision */
		struct utimbuf ut = { (time_t) (now - 86400), (time_t) (now - 86400) };
		int r = utime (p, &ut);
		int o = stat (p, &st);
		long long d = (long long) st.st_mtime - (now - 86400);
		CHECK ("utime: a date in the past", r == 0 && o == 0 && d >= -2 && d <= 2, "%d %d delta %lld", r, o, d);
		FILE *w = fopen (p, "a");
		if (w)
		{
			fputs ("x", w);
			fclose (w);
		}
		o = stat (p, &st);
		CHECK ("writing brings mtime back to now", w && o == 0 && st.st_mtime > now - 5, "mtime %lld now %lld", (long long) st.st_mtime, now);
		char q[300];
		snprintf (q, sizeof q, "%s.older", p);
		w = fopen (q, "w");
		if (w)
			fclose (w);
		struct stat s2;
		ut.modtime = (time_t) (now - 3600);
		ut.actime = ut.modtime;
		utime (q, &ut);
		o = stat (q, &s2);
		CHECK ("make's order: the older file has the older date", w && o == 0 && s2.st_mtime < st.st_mtime, "%lld %lld", (long long) s2.st_mtime, (long long) st.st_mtime);
		unlink (q);
	}
	else
		skip ("stat: mtime close to now", NOSYS);
	unlink (p);
}

static void group_file (const char *where)
{
	if (where)
	{
		file_group_in (where);
		return;
	}
	file_group_in ("RAM0:/posixtest");
	file_group_in ("/tmp/posixtest");	/* (/tmp est RAM0:/tmp) */

	s_group = "file";
	char tmpl[] = "/tmp/ptXXXXXX";
	int fd = mkstemp (tmpl);
	struct stat st;
	CHECK ("mkstemp in /tmp (RAM0:/tmp)", fd >= 0 && fstat (fd, &st) == 0 && S_ISREG (st.st_mode), "fd %d errno %d", fd, errno);
	if (fd >= 0)
	{
		close (fd);
		unlink (tmpl);
	}
	FILE *t = tmpfile ();
	CHECK ("tmpfile", t != 0, "errno %d", errno);
	if (t)
		fclose (t);
}

/* =================================================================================== io == */
static int s_pipeW;
static void *t_late_write (void *a)
{
	(void) a;
	usleep (50000);
	write (s_pipeW, "x", 1);
	return 0;
}

static void group_io (void)
{
	s_group = "io";
	int pf[2];
	if (pipe (pf) != 0)
	{
		fail ("pipe", "errno %d", errno);
		return;
	}
	char b[16] = "";
	ssize_t w = write (pf[1], "hello", 5), r = read (pf[0], b, sizeof b);
	CHECK ("pipe: write then read", w == 5 && r == 5 && !memcmp (b, "hello", 5), "%zd %zd", w, r);

	fcntl (pf[0], F_SETFL, O_NONBLOCK);
	r = read (pf[0], b, sizeof b);
	CHECK ("pipe O_NONBLOCK: an empty read -> EAGAIN", r == -1 && errno == EAGAIN, "%zd errno %d", r, errno);

	struct pollfd p = { pf[0], POLLIN, 0 };
	long long t0 = ms_now ();
	int pr = poll (&p, 1, 100);
	long long dt = ms_now () - t0;
	CHECK ("poll timeout 100 ms on an empty pipe", pr == 0 && dt >= 90 && dt < 300, "%d after %lld ms", pr, dt);

	pthread_t th;
	s_pipeW = pf[1];
	pthread_create (&th, 0, t_late_write, 0);
	t0 = ms_now ();
	pr = poll (&p, 1, 2000);
	dt = ms_now () - t0;
	pthread_join (th, 0);
	CHECK ("poll wakes when another thread writes (50 ms)", pr == 1 && (p.revents & POLLIN) && dt >= 40 && dt < 200, "%d revents %x after %lld ms", pr, p.revents, dt);
	read (pf[0], b, sizeof b);

	fd_set rs;
	FD_ZERO (&rs);
	FD_SET (pf[0], &rs);
	struct timeval tv = { 0, 50000 };
	t0 = ms_now ();
	int sr = select (pf[0] + 1, &rs, 0, 0, &tv);
	dt = ms_now () - t0;
	CHECK ("select timeout 50 ms", sr == 0 && dt >= 40 && dt < 250, "%d after %lld ms", sr, dt);

	if (!k_pipe_nb)
		skip ("pipe O_NONBLOCK: a full pipe -> EAGAIN", NOSYS);
	else
	{
		fcntl (pf[1], F_SETFL, O_NONBLOCK);
		static char big[4096];
		int full = 0;
		for (int i = 0; i < 1000 && !full; i++)
			if (write (pf[1], big, sizeof big) < 0 && errno == EAGAIN)
				full = 1;
		CHECK ("pipe O_NONBLOCK: a full pipe -> EAGAIN", full, "never full");
		while (read (pf[0], big, sizeof big) > 0)
			;
	}
	close (pf[1]);
	fcntl (pf[0], F_SETFL, 0);
	r = read (pf[0], b, sizeof b);
	CHECK ("pipe: the reader sees the end once the writer closes", r == 0, "%zd", r);
	close (pf[0]);

	unsigned char rnd[64];
	int ufd = open ("/dev/urandom", O_RDONLY);
	int nz = 0;
	if (ufd >= 0 && read (ufd, rnd, sizeof rnd) == (ssize_t) sizeof rnd)
		for (int i = 0; i < 64; i++)
			nz += rnd[i] != 0;
	CHECK ("/dev/urandom", ufd >= 0 && nz > 32, "%d non-zero bytes", nz);
	if (ufd >= 0)
		close (ufd);
	int zf = open ("/dev/zero", O_RDONLY), nf = open ("/dev/null", O_WRONLY);
	memset (rnd, 1, sizeof rnd);
	CHECK ("/dev/zero and /dev/null", zf >= 0 && nf >= 0 && read (zf, rnd, 64) == 64 && rnd[63] == 0 && write (nf, rnd, 64) == 64, "%d %d", zf, nf);
	close (zf);
	close (nf);
}

/* ================================================================================= time == */
static void group_time (void)
{
	s_group = "time";
	struct timespec a, b;
	int mono = 1;
	clock_gettime (CLOCK_MONOTONIC, &a);
	for (int i = 0; i < 10000; i++)
	{
		clock_gettime (CLOCK_MONOTONIC, &b);
		if (b.tv_sec < a.tv_sec || (b.tv_sec == a.tv_sec && b.tv_nsec < a.tv_nsec))
			mono = 0;
		a = b;
	}
	usleep (2000);
	clock_gettime (CLOCK_MONOTONIC, &b);
	CHECK ("CLOCK_MONOTONIC never goes back, and moves", mono && (b.tv_sec > a.tv_sec || b.tv_nsec > a.tv_nsec), "");

	struct timeval tv;
	clock_gettime (CLOCK_REALTIME, &a);
	gettimeofday (&tv, 0);
	long long d = ((long long) tv.tv_sec - a.tv_sec) * 1000000 + (tv.tv_usec - a.tv_nsec / 1000);
	CHECK ("CLOCK_REALTIME agrees with gettimeofday and time", d >= 0 && d < 20000 && labs ((long) (time (0) - tv.tv_sec)) <= 1, "%lld us", d);
	if (!k_clock)
		skip ("clock_info: the date is UTC with the kernel's zone", NOSYS);
	else
		CHECK ("the date is after 2025 (RTC / NTP)", a.tv_sec > 1735689600, "%lld", (long long) a.tv_sec);

	long long worst = 0, sum = 0;
	for (int i = 0; i < 10; i++)
	{
		struct timespec rq = { 0, 20000000 };
		long long t0 = ms_now ();
		nanosleep (&rq, 0);
		long long e = ms_now () - t0 - 20;
		sum += e;
		if (e > worst)
			worst = e;
		if (e < -1)
			worst = 1000;
	}
	CHECK ("nanosleep 20 ms x 10: never early, mean late < 15 ms", worst < 100 && sum / 10 < 15, "worst %lld, mean %lld ms", worst, sum / 10);

	struct timespec res;
	CHECK ("clock_getres", clock_getres (CLOCK_MONOTONIC, &res) == 0 && res.tv_sec == 0 && res.tv_nsec > 0 && res.tv_nsec < 1000000, "%ld ns", res.tv_nsec);

	time_t t = 1700000000;
	struct tm g, l;
	char *oldtz = getenv ("TZ") ? strdup (getenv ("TZ")) : 0;
	setenv ("TZ", "CET-1", 1);
	tzset ();
	gmtime_r (&t, &g);
	localtime_r (&t, &l);
	CHECK ("localtime_r with TZ=CET-1 is an hour ahead of gmtime_r", (l.tm_hour - g.tm_hour + 24) % 24 == 1, "%d vs %d", l.tm_hour, g.tm_hour);
	struct tm rt = g;
	CHECK ("timegm (gmtime_r (t)) = t", timegm (&rt) == t, "%lld", (long long) timegm (&rt));
	if (oldtz)
	{
		setenv ("TZ", oldtz, 1);
		free (oldtz);
	}
	else
		unsetenv ("TZ");
	tzset ();
}

/* ================================================================================= proc == */
static const char *s_self = "SD0:/bin/posixtest";

/* (ipc group) the child: a socket at fd (as WebKit's auxiliary processes: its number in argv), the
 * parent's CLOEXEC end must not be here, the dup2 target is the same kind of socket. It receives a
 * memfd by SCM_RIGHTS, maps it MAP_SHARED, checks the parent's bytes, writes its own, says "ok". */
static int ipc_child (int fd, int cloexec_fd, int dup_fd)
{
	if (fcntl (cloexec_fd, F_GETFD) != -1 || errno != EBADF)
		return 20;				/* (the CLOEXEC end leaked into the child) */
	int t = 0;
	socklen_t tl = sizeof t;
	if (getsockopt (dup_fd, SOL_SOCKET, SO_TYPE, &t, &tl) != 0 || t != SOCK_SEQPACKET)
		return 21;
	char b[16];
	char cbuf[CMSG_SPACE (sizeof (int))];
	struct iovec v = { b, sizeof b };
	struct msghdr m;
	memset (&m, 0, sizeof m);
	m.msg_iov = &v;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof cbuf;
	ssize_t n = recvmsg (fd, &m, MSG_CMSG_CLOEXEC);
	struct cmsghdr *c = CMSG_FIRSTHDR (&m);
	if (n != 3 || c == 0 || c->cmsg_type != SCM_RIGHTS)
		return 22;
	int mfd;
	memcpy (&mfd, CMSG_DATA (c), sizeof mfd);
	struct stat st;
	if (fstat (mfd, &st) != 0 || st.st_size != 100000)
		return 23;
	unsigned char *p = (unsigned char *) mmap (0, 100000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
	if (p == MAP_FAILED || p[0] != 0x11 || p[99999] != 0x22)
		return 24;
	p[50000] = 0x33;
	munmap (p, 100000);
	close (mfd);
	return send (fd, "ok", 2, MSG_NOSIGNAL) == 2 ? 30 : 25;
}

/* (ipc group, ARMPI) the child that dies holding things: 8 MB of shared memory mapped and
 * written, a socket pair whose one end carries the memfd in a message never read; it says "r"
 * on fd, then waits to be killed */
static int ipc_hold (int fd)
{
	int m = memfd_create ("hold", 0);
	if (m < 0 || ftruncate (m, 8 << 20) != 0)
		return 40;
	unsigned char *p = (unsigned char *) mmap (0, 8 << 20, PROT_READ | PROT_WRITE, MAP_SHARED, m, 0);
	if (p == MAP_FAILED)
		return 41;
	for (int i = 0; i < (8 << 20); i += 4096)
		p[i] = 1;
	int x[2];
	if (socketpair (AF_UNIX, SOCK_SEQPACKET, 0, x) != 0)
		return 42;
	char cbuf[CMSG_SPACE (sizeof (int))];
	memset (cbuf, 0, sizeof cbuf);
	struct iovec v = { (void *) "m", 1 };
	struct msghdr h;
	memset (&h, 0, sizeof h);
	h.msg_iov = &v;
	h.msg_iovlen = 1;
	h.msg_control = cbuf;
	h.msg_controllen = sizeof cbuf;
	struct cmsghdr *c = CMSG_FIRSTHDR (&h);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN (sizeof (int));
	memcpy (CMSG_DATA (c), &m, sizeof m);
	if (sendmsg (x[0], &h, 0) != 1)
		return 43;
	close (m);
	if (send (fd, "r", 1, 0) != 1)
		return 44;
	char z;
	recv (fd, &z, 1, 0);			/* (killed while it waits here) */
	for (;;)
		sleep (1);
}

static long stack_touch (int mb);

static int child_main (int argc, char **argv)
{
	const char *mode = argc > 2 ? argv[2] : "";
	if (!strcmp (mode, "exit42"))
		return 42;
	if (!strcmp (mode, "crash"))
	{
		volatile int *volatile bad = (volatile int *) 16;
		*bad = 1;
		return 0;
	}
	if (!strcmp (mode, "env"))
	{
		const char *v = getenv ("POSIXTEST_X");
		return v && !strcmp (v, "hello world") ? 7 : 8;
	}
	if (!strcmp (mode, "argv"))
		return argc == 5 && !strcmp (argv[3], "a b") && !strcmp (argv[4], "\"q\"") ? 9 : 10;
	if (!strcmp (mode, "long"))			/* (ARMPI) bien au-dela des 1 024 octets du noyau */
	{
		const char *v = getenv ("POSIXTEST_LONG");
		return argc == 402 && v && strlen (v) == 9000 && !strcmp (argv[0], "un-nom-a-soi") &&
		       strlen (argv[3]) == 19 && !strcmp (argv[401], "dernier") ? 11 : 12;
	}
	if (!strcmp (mode, "out"))			/* (ARMPI) sortie et erreur, redirigees par le pere */
	{
		write (1, "sur stdout\n", 11);
		write (2, "sur stderr\n", 11);
		return 15;
	}
	if (!strcmp (mode, "out2"))			/* (ARMPI) un petit-fils herite de la redirection */
	{
		char *a[] = { argv[0], "--child", "out", 0 };
		pid_t pid;
		int st = 0;
		if (posix_spawn (&pid, argv[0], 0, 0, a, environ) != 0 || waitpid (pid, &st, 0) != pid)
			return 2;
		write (1, "pere\n", 5);
		return WIFEXITED (st) && WEXITSTATUS (st) == 15 ? 17 : 3;
	}
	if (!strcmp (mode, "in"))			/* (ARMPI) entree redirigee */
	{
		char b[32];
		ssize_t n = read (0, b, sizeof b);
		return n == 7 && !memcmp (b, "entree\n", 7) ? 16 : 4;
	}
	if (!strcmp (mode, "stack"))			/* (ARMPI) la pile du noyau : 64 Mo a la demande */
		return stack_touch (48) == 48 * 256 ? 13 : 14;
	if (!strcmp (mode, "ipc") && argc == 6)
		return ipc_child (atoi (argv[3]), atoi (argv[4]), atoi (argv[5]));
	if (!strcmp (mode, "ipchold") && argc == 4)
		return ipc_hold (atoi (argv[3]));
	if (!strcmp (mode, "halfserver") && argc == 4)		/* (ARMPI) FIN sur le fil : tools ou PC en face */
	{
		struct sockaddr_in a;
		memset (&a, 0, sizeof a);
		a.sin_family = AF_INET;
		a.sin_port = htons ((unsigned short) atoi (argv[3]));
		int on = 1, l = socket (AF_INET, SOCK_STREAM, 0);
		setsockopt (l, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
		if (bind (l, (struct sockaddr *) &a, sizeof a) != 0 || listen (l, 1) != 0)
			return 20;
		int s = accept (l, 0, 0);
		char b[64];
		if (s < 0 || send (s, "hello", 5, 0) != 5 || shutdown (s, SHUT_WR) != 0)
			return 21;
		ssize_t n = recv (s, b, sizeof b, MSG_WAITALL);
		printf ("halfserver : %d octets recus apres shutdown\n", (int) n);
		close (s);
		close (l);
		return n == 4 && !memcmp (b, "back", 4) ? 0 : 22;
	}
	if (!strcmp (mode, "filemap") && argc == 4)		/* (ARMPI) finit sans munmap */
	{
		struct stat st;
		int fd = open (argv[3], O_RDONLY);
		if (fd < 0 || fstat (fd, &st) != 0)
			return 18;
		void *p = mmap (0, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		return p != MAP_FAILED && file_obj_there (fd) ? 17 : 19;
	}
	return 1;
}

/* (ARMPI) 48 Mo de pile dans le thread principal, Mo par Mo */
__attribute__ ((noinline)) static long stack_touch (int mb)
{
	volatile char page[1 << 20];
	for (unsigned i = 0; i < sizeof page; i += 4096)
		page[i] = 1;
	long n = mb > 1 ? stack_touch (mb - 1) : 0;
	for (unsigned i = 0; i < sizeof page; i += 4096)
		n += page[i];
	return n;
}

static int spawn_wait (char *const *argv, char *const *envp, int *status)
{
	pid_t pid;
	int r = posix_spawn (&pid, s_self, 0, 0, argv, envp);
	if (r != 0)
		return -r;
	return waitpid (pid, status, 0) == pid ? 0 : -1000 - errno;
}

static void group_proc (void)
{
	s_group = "proc";
	CHECK ("getpid, getppid", getpid () > 0 && getppid () > 0, "%d %d", getpid (), getppid ());

	setenv ("POSIXTEST_Y", "1", 1);
	char *y = getenv ("POSIXTEST_Y");
	unsetenv ("POSIXTEST_Y");
	CHECK ("setenv / getenv / unsetenv", y && !strcmp (y, "1") && getenv ("POSIXTEST_Y") == 0, "");
	if (!k_env)
		skip ("the environment came from the kernel (HOME, PATH)", NOSYS);
	else
		CHECK ("the environment came from the kernel (HOME, PATH)", getenv ("HOME") && getenv ("PATH"), "");

	int st = 0;
	char *a1[] = { (char *) s_self, "--child", "exit42", 0 };
	int r = spawn_wait (a1, environ, &st);
	CHECK ("posix_spawn of itself, waitpid: exit 42", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 42, "%d status %x", r, st);

	if (!k_proc)
	{
		/* (the old spawn passes one line, no environment, and a crash shows up as exit 0) */
		skip ("argv: an argument with a space, one with quotes", NOSYS);
		skip ("the child gets the environment", NOSYS);
		skip ("a crashing child: WIFSIGNALED, SIGSEGV", NOSYS);
	}
	else
	{
		/* (no empty argument: the kernel's argv block ends at an empty string) */
		char *a2[] = { (char *) s_self, "--child", "argv", "a b", "\"q\"", 0 };
		r = spawn_wait (a2, environ, &st);
		CHECK ("argv: an argument with a space, one with quotes", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 9, "%d status %x", r, st);

		if (!k_env)
			skip ("the child gets the environment", NOSYS);
		else
		{
		setenv ("POSIXTEST_X", "hello world", 1);
		char *a3[] = { (char *) s_self, "--child", "env", 0 };
		r = spawn_wait (a3, environ, &st);
		CHECK ("the child gets the environment", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 7, "%d status %x", r, st);
		unsetenv ("POSIXTEST_X");
		}

		char *a4[] = { (char *) s_self, "--child", "crash", 0 };
		r = spawn_wait (a4, environ, &st);
		CHECK ("a crashing child: WIFSIGNALED, SIGSEGV", r == 0 && WIFSIGNALED (st) && WTERMSIG (st) == SIGSEGV, "%d status %x", r, st);

		/* (ARMPI) argv et environnement par l'objet partage : 8 Ko d'arguments, 9 Ko de variable */
		static char *a5[403];
		static char word[20], big[9000 + 16];
		memset (word, 'x', 19);
		strcpy (big, "POSIXTEST_LONG=");
		memset (big + 15, 'y', 9000);
		a5[0] = "un-nom-a-soi";
		a5[1] = "--child";
		a5[2] = "long";
		for (int i = 3; i < 401; i++)
			a5[i] = word;
		a5[401] = "dernier";
		char *e5[] = { big, 0 };
		r = spawn_wait (a5, e5, &st);
		CHECK ("argv of 8 KB, a 9 KB variable, argv[0] kept", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 11, "%d status %x", r, st);

		char *a6[] = { (char *) s_self, "--child", "stack", 0 };
		r = spawn_wait (a6, environ, &st);
		CHECK ("48 MB of stack in the main thread", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 13, "%d status %x", r, st);

		/* (ARMPI) redirections vers un fichier : adddup2 et addopen */
		const char *rf = "/tmp/posixtest-redir.txt";
		pid_t rp = 0;
		char *a7[] = { (char *) s_self, "--child", "out", 0 };
		posix_spawn_file_actions_t fa;
		posix_spawn_file_actions_init (&fa);
		int rd = open (rf, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		posix_spawn_file_actions_adddup2 (&fa, rd, 1);
		posix_spawn_file_actions_adddup2 (&fa, rd, 2);
		r = posix_spawn (&rp, s_self, &fa, 0, a7, environ);
		posix_spawn_file_actions_destroy (&fa);
		close (rd);
		st = 0;
		if (r == 0)
			waitpid (rp, &st, 0);
		char buf[128];
		memset (buf, 0, sizeof buf);
		int rg = open (rf, O_RDONLY);
		ssize_t rn = rg >= 0 ? read (rg, buf, sizeof buf - 1) : -1;
		if (rg >= 0)
			close (rg);
		CHECK ("adddup2 of a file: stdout and stderr in it", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 15 &&
		       rn == 22 && !strcmp (buf, "sur stdout\nsur stderr\n"), "%d status %x n %ld", r, st, (long) rn);

		char *a8[] = { (char *) s_self, "--child", "out2", 0 };
		posix_spawn_file_actions_init (&fa);
		posix_spawn_file_actions_addopen (&fa, 1, rf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		posix_spawn_file_actions_addopen (&fa, 2, rf, O_WRONLY | O_CREAT | O_APPEND, 0644);
		r = posix_spawn (&rp, s_self, &fa, 0, a8, environ);
		posix_spawn_file_actions_destroy (&fa);
		st = 0;
		if (r == 0)
			waitpid (rp, &st, 0);
		memset (buf, 0, sizeof buf);
		rg = open (rf, O_RDONLY);
		rn = rg >= 0 ? read (rg, buf, sizeof buf - 1) : -1;
		if (rg >= 0)
			close (rg);
		CHECK ("addopen, inherited by a grandchild", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 17 &&
		       rn == 27 && !strcmp (buf, "sur stdout\nsur stderr\npere\n"), "%d status %x n %ld", r, st, (long) rn);

		rd = open (rf, O_WRONLY | O_TRUNC);
		if (rd >= 0)
		{
			write (rd, "entree\n", 7);
			close (rd);
		}
		char *a9[] = { (char *) s_self, "--child", "in", 0 };
		posix_spawn_file_actions_init (&fa);
		posix_spawn_file_actions_addopen (&fa, 0, rf, O_RDONLY, 0);
		r = posix_spawn (&rp, s_self, &fa, 0, a9, environ);
		posix_spawn_file_actions_destroy (&fa);
		st = 0;
		if (r == 0)
			waitpid (rp, &st, 0);
		CHECK ("addopen of stdin", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 16, "%d status %x", r, st);
		unlink (rf);
	}

	CHECK ("waitpid with no child -> ECHILD", waitpid (-1, &st, WNOHANG) == -1 && errno == ECHILD, "errno %d", errno);
	CHECK ("fork -> ENOSYS", fork () == -1 && errno == ENOSYS, "errno %d", errno);
}

/* ================================================================================== ipc == */
/* (ARMPI) the rings under load: messages of 2..4000 bytes through a SEQPACKET pair, read by a thread
 * (the sender waits when the ring is full; the records wrap around it); bytes through a STREAM pair */
#define IPC_STRESS_N	3000
#define IPC_STREAM_LEN	(1 << 20)
static void *ipc_stress_reader (void *arg)
{
	int fd = (int) (long) arg;
	static unsigned char b[4096];
	long ok = 1;
	for (int i = 0; i < IPC_STRESS_N; i++)
	{
		ssize_t n = recv (fd, b, sizeof b, 0);
		int want = 2 + (i * 37) % 3999;
		if (n != want || b[0] != (unsigned char) i || b[n - 1] != (unsigned char) (i * 7))
			ok = 0;
	}
	return (void *) ok;
}

static void *ipc_stream_writer (void *arg)
{
	int fd = (int) (long) arg;
	static unsigned char b[7000];
	long pos = 0;
	while (pos < IPC_STREAM_LEN)
	{
		int n = IPC_STREAM_LEN - pos < 7000 ? (int) (IPC_STREAM_LEN - pos) : 7000;
		for (int k = 0; k < n; k++)
			b[k] = (unsigned char) ((pos + k) * 13);
		ssize_t w = send (fd, b, (size_t) n, 0);
		if (w <= 0)
			return (void *) 0;
		pos += w;
	}
	return (void *) 1;
}

static int send_fds (int s, const char *data, const int *fds, int n)
{
	char cbuf[CMSG_SPACE (8 * sizeof (int))];
	struct iovec v[3] = { { (void *) data, 1 }, { (void *) (data + 1), 1 }, { (void *) (data + 2), strlen (data) - 2 } };
	struct msghdr m;
	memset (&m, 0, sizeof m);
	memset (cbuf, 0, sizeof cbuf);
	m.msg_iov = v;
	m.msg_iovlen = 3;
	if (n > 0)
	{
		m.msg_control = cbuf;
		m.msg_controllen = CMSG_SPACE (n * sizeof (int));
		struct cmsghdr *c = CMSG_FIRSTHDR (&m);
		c->cmsg_level = SOL_SOCKET;
		c->cmsg_type = SCM_RIGHTS;
		c->cmsg_len = CMSG_LEN (n * sizeof (int));
		memcpy (CMSG_DATA (c), fds, n * sizeof (int));
	}
	return (int) sendmsg (s, &m, MSG_NOSIGNAL);
}

/* -> bytes; the fds received in fds (*n in: room, out: count), *mflags */
static int recv_fds (int s, char *data, size_t cap, int *fds, int *n, int *mflags, int flags)
{
	char cbuf[CMSG_SPACE (8 * sizeof (int))];
	struct iovec v = { data, cap };
	struct msghdr m;
	memset (&m, 0, sizeof m);
	m.msg_iov = &v;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = CMSG_LEN (*n * sizeof (int));	/* (room for exactly *n) */
	int r = (int) recvmsg (s, &m, flags);
	int got = 0;
	for (struct cmsghdr *c = r >= 0 ? CMSG_FIRSTHDR (&m) : 0; c; c = CMSG_NXTHDR (&m, c))
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
		{
			int k = (int) ((c->cmsg_len - CMSG_LEN (0)) / sizeof (int));
			memcpy (fds + got, CMSG_DATA (c), k * sizeof (int));
			got += k;
		}
	*n = got;
	*mflags = m.msg_flags;
	return r;
}

static void group_ipc (void)
{
	s_group = "ipc";
	if (!k_ipc)
	{
		int sv[2];
		CHECK ("socketpair (AF_UNIX) without the kernel's v76 -> EOPNOTSUPP",
		       socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == -1 && errno == EOPNOTSUPP, "errno %d", errno);
		skip ("local sockets, SCM_RIGHTS, memfd, shm_open, posix_spawn with descriptors", NOSYS);
		return;
	}
	CHECK ("socket (AF_UNIX) -> EAFNOSUPPORT (no named local sockets)",
	       socket (AF_UNIX, SOCK_STREAM, 0) == -1 && errno == EAFNOSUPPORT, "errno %d", errno);

	/* SEQPACKET (WebKit's connection), CLOEXEC */
	int sp[2];
	int r = socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sp);
	CHECK ("socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC)", r == 0 && (fcntl (sp[0], F_GETFD) & FD_CLOEXEC), "r %d errno %d", r, errno);
	if (r != 0)
		return;
	char b[64];
	send (sp[0], "hello", 5, 0);
	send (sp[0], "you", 3, 0);
	ssize_t a1 = recv (sp[1], b, sizeof b, 0), a2 = recv (sp[1], b + 8, sizeof b - 8, 0);
	CHECK ("SEQPACKET keeps the boundaries", a1 == 5 && a2 == 3 && !memcmp (b, "hello", 5) && !memcmp (b + 8, "you", 3), "%d %d", (int) a1, (int) a2);
	struct pollfd pf = { sp[1], POLLIN, 0 };
	long long t0 = ms_now ();
	int pr = poll (&pf, 1, 100);
	long long dt = ms_now () - t0;
	CHECK ("poll on an empty local socket: the timeout", pr == 0 && dt >= 90 && dt < 400, "%d in %lld ms", pr, dt);
	send (sp[0], "x", 1, 0);
	pr = poll (&pf, 1, 1000);
	CHECK ("poll: POLLIN", pr == 1 && (pf.revents & POLLIN), "%d %x", pr, pf.revents);
	recv (sp[1], b, sizeof b, 0);
	struct ucred cr;
	socklen_t cl = sizeof cr;
	int dom = 0;
	socklen_t dl = sizeof dom;
	memset (&cr, 0, sizeof cr);
	CHECK ("SO_PEERCRED: our pid; SO_DOMAIN AF_UNIX",
	       getsockopt (sp[0], SOL_SOCKET, SO_PEERCRED, &cr, &cl) == 0 && cr.pid == getpid ()
	       && getsockopt (sp[0], SOL_SOCKET, SO_DOMAIN, &dom, &dl) == 0 && dom == AF_UNIX, "pid %d dom %d", (int) cr.pid, dom);
	struct sockaddr_un un;
	socklen_t ul = sizeof un;
	memset (&un, 0, sizeof un);
	CHECK ("getsockname: AF_UNIX, unnamed", getsockname (sp[0], (struct sockaddr *) &un, &ul) == 0 && un.sun_family == AF_UNIX
	       && ul == sizeof (sa_family_t), "len %u", (unsigned) ul);

	/* memfd: ftruncate, fstat, seals, two MAP_SHARED mappings alias, MAP_PRIVATE copies */
	int mfd = memfd_create ("posixtest", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	struct stat st;
	r = mfd >= 0 && ftruncate (mfd, 100000) == 0 && fstat (mfd, &st) == 0 && st.st_size == 100000 && S_ISREG (st.st_mode);
	CHECK ("memfd_create, ftruncate, fstat", r, "fd %d errno %d", mfd, errno);
	unsigned char *m1 = (unsigned char *) mmap (0, 100000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
	unsigned char *m2 = (unsigned char *) mmap (0, 100000, PROT_READ, MAP_SHARED, mfd, 0);
	int alias = m1 != MAP_FAILED && m2 != MAP_FAILED && m1 != m2;
	if (alias)
	{
		m1[0] = 0x11;
		m1[99999] = 0x22;
		alias = m2[0] == 0x11 && m2[99999] == 0x22;
	}
	CHECK ("mmap MAP_SHARED twice: the same pages", alias, "%p %p", (void *) m1, (void *) m2);
	unsigned char *mp = (unsigned char *) mmap (0, 100000, PROT_READ | PROT_WRITE, MAP_PRIVATE, mfd, 0);
	int copy = mp != MAP_FAILED && mp[0] == 0x11;
	if (copy)
	{
		mp[0] = 0x44;
		copy = m1[0] == 0x11;
		munmap (mp, 100000);
	}
	CHECK ("mmap MAP_PRIVATE of it: a copy", copy, "");
	mp = (unsigned char *) mmap (0, 100000, PROT_READ, MAP_PRIVATE, mfd, 0);
	copy = mp != MAP_FAILED && mp[1] == m1[1] && mprotect (mp, 100000, PROT_READ | PROT_WRITE) == 0;
	if (copy)
	{
		mp[0] = 0x55;			/* a page present, read-only: copied on write */
		mp[50000] = 0x66;		/* a page absent */
		copy = m1[0] == 0x11 && m1[50000] == 0 && mp[0] == 0x55 && mprotect (mp, 100000, PROT_READ) == 0
		       && mp[0] == 0x55 && m2[0] == 0x11;
		munmap (mp, 100000);
	}
	CHECK ("mmap MAP_PRIVATE then mprotect: the object does not change", copy, "errno %d", errno);
	r = fcntl (mfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW);
	CHECK ("F_ADD_SEALS / F_GET_SEALS; ftruncate then EPERM", r == 0
	       && fcntl (mfd, F_GET_SEALS) == (F_SEAL_SHRINK | F_SEAL_GROW) && ftruncate (mfd, 5) == -1 && errno == EPERM, "r %d errno %d", r, errno);

	/* sendmsg / recvmsg with SCM_RIGHTS: a memfd and a socket's end, 3 iovecs (ARMPI: a pipe of
	 * vfs is not a kernel object: EOPNOTSUPP) */
	int pp[2];
	pipe (pp);
	int bad[1] = { pp[1] };
	CHECK ("sendmsg SCM_RIGHTS of a pipe (vfs) -> EOPNOTSUPP", send_fds (sp[0], "p", bad, 1) == -1 && errno == EOPNOTSUPP, "errno %d", errno);
	int qq[2];
	socketpair (AF_UNIX, SOCK_STREAM, 0, qq);
	int out[2] = { mfd, qq[1] };
	int sn = send_fds (sp[0], "fds!", out, 2);
	int in[8], n = 8, mf = 0;
	int rn = recv_fds (sp[1], b, sizeof b, in, &n, &mf, 0);
	CHECK ("sendmsg / recvmsg: 3 iovecs, 2 descriptors (CMSG_FIRSTHDR / CMSG_NXTHDR)", sn == 4 && rn == 4 && !memcmp (b, "fds!", 4)
	       && n == 2 && in[0] != mfd && in[1] != pp[1] && mf == 0, "%d %d n %d flags %x", sn, rn, n, mf);
	if (n == 2)
	{
		unsigned char *m3 = (unsigned char *) mmap (0, 100000, PROT_READ, MAP_SHARED, in[0], 0);
		CHECK ("the memfd received maps the same object", m3 != MAP_FAILED && m3[99999] == 0x22, "");
		if (m3 != MAP_FAILED)
			munmap (m3, 100000);
		ssize_t w = write (in[1], "sock", 4);
		char pb[8];
		ssize_t k = read (qq[0], pb, sizeof pb);
		CHECK ("the socket end received writes into our socket", w == 4 && k == 4 && !memcmp (pb, "sock", 4), "%d %d", (int) w, (int) k);
		close (in[0]);
		close (in[1]);
	}
	/* MSG_CTRUNC (room for 1 of 3), MSG_CMSG_CLOEXEC */
	int three[3] = { mfd, qq[0], qq[1] };
	send_fds (sp[0], "abc", three, 3);
	n = 1;
	rn = recv_fds (sp[1], b, sizeof b, in, &n, &mf, MSG_CMSG_CLOEXEC);
	CHECK ("MSG_CTRUNC: room for 1 of 3; MSG_CMSG_CLOEXEC", rn == 3 && n == 1 && (mf & MSG_CTRUNC)
	       && (fcntl (in[0], F_GETFD) & FD_CLOEXEC), "%d n %d flags %x", rn, n, mf);
	if (n == 1)
		close (in[0]);

	/* (ARMPI) the rings under load */
	{
		int st[2];
		socketpair (AF_UNIX, SOCK_SEQPACKET, 0, st);
		pthread_t th;
		pthread_create (&th, 0, ipc_stress_reader, (void *) (long) st[1]);
		static unsigned char sb[4096];
		long long t1 = ms_now ();
		int sent = 0;
		for (int i = 0; i < IPC_STRESS_N; i++)
		{
			int n = 2 + (i * 37) % 3999;
			memset (sb, 0x5A, (size_t) n);
			sb[0] = (unsigned char) i;
			sb[n - 1] = (unsigned char) (i * 7);
			if (send (st[0], sb, (size_t) n, 0) == n)
				sent++;
		}
		void *ok = 0;
		pthread_join (th, &ok);
		long long dt1 = ms_now () - t1;
		CHECK ("SEQPACKET under load: 3000 messages of 2..4000 bytes, a reader thread (wrap, full ring)",
		       sent == IPC_STRESS_N && ok == (void *) 1, "sent %d ok %ld (%lld ms)", sent, (long) ok, dt1);
		close (st[0]);
		close (st[1]);
		socketpair (AF_UNIX, SOCK_STREAM, 0, st);
		pthread_create (&th, 0, ipc_stream_writer, (void *) (long) st[0]);
		static unsigned char rb2[5000];
		long got = 0, bad = 0;
		t1 = ms_now ();
		while (got < IPC_STREAM_LEN)
		{
			ssize_t n = recv (st[1], rb2, sizeof rb2, 0);
			if (n <= 0)
				break;
			for (ssize_t k = 0; k < n; k++)
				if (rb2[k] != (unsigned char) ((got + k) * 13))
					bad++;
			got += n;
		}
		ok = 0;
		pthread_join (th, &ok);
		dt1 = ms_now () - t1;
		CHECK ("SOCK_STREAM under load: 1 MB, sends of 7000 and reads of 5000 bytes", got == IPC_STREAM_LEN && bad == 0
		       && ok == (void *) 1, "got %ld bad %ld ok %ld (%lld ms)", got, bad, (long) ok, dt1);
		close (st[0]);
		close (st[1]);
	}

	/* STREAM and DGRAM pairs */
	int ss[2], ds[2];
	r = socketpair (AF_UNIX, SOCK_STREAM, 0, ss);
	send (ss[0], "ab", 2, 0);
	send (ss[0], "cd", 2, 0);
	usleep (10000);
	ssize_t k = recv (ss[1], b, sizeof b, 0);
	CHECK ("SOCK_STREAM pair: the bytes of two sends", r == 0 && k == 4 && !memcmp (b, "abcd", 4), "%d", (int) k);
	close (ss[0]);
	ssize_t e0 = recv (ss[1], b, sizeof b, 0);
	ssize_t e1 = send (ss[1], "x", 1, MSG_NOSIGNAL);
	CHECK ("SOCK_STREAM: the peer closed -> 0, and EPIPE back", e0 == 0 && e1 == -1 && errno == EPIPE, "%d %d errno %d", (int) e0, (int) e1, errno);
	close (ss[1]);
	r = socketpair (AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, ds);
	CHECK ("SOCK_DGRAM | SOCK_NONBLOCK pair: EAGAIN when empty", r == 0 && recv (ds[1], b, sizeof b, 0) == -1 && errno == EAGAIN, "errno %d", errno);
	if (r == 0)
	{
		close (ds[0]);
		close (ds[1]);
	}

	/* shm_open / shm_unlink */
	shm_unlink ("/posixtest");
	int s1 = shm_open ("/posixtest", O_RDWR | O_CREAT | O_EXCL, 0600);
	int s2 = s1 >= 0 ? shm_open ("/posixtest", O_RDWR, 0) : -1;
	r = s1 >= 0 && s2 >= 0 && ftruncate (s1, 4096) == 0 && (fcntl (s1, F_GETFD) & FD_CLOEXEC);
	unsigned char *q1 = r ? (unsigned char *) mmap (0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, s1, 0) : MAP_FAILED;
	unsigned char *q2 = r ? (unsigned char *) mmap (0, 4096, PROT_READ, MAP_SHARED, s2, 0) : MAP_FAILED;
	int same = q1 != MAP_FAILED && q2 != MAP_FAILED;
	if (same)
	{
		q1[7] = 9;
		same = q2[7] == 9;
	}
	CHECK ("shm_open twice: the same object (FD_CLOEXEC)", same, "%d %d", s1, s2);
	CHECK ("shm_unlink, then the name is gone", shm_unlink ("/posixtest") == 0 && shm_open ("/posixtest", O_RDWR, 0) == -1
	       && errno == ENOENT, "errno %d", errno);
	if (q1 != MAP_FAILED) munmap (q1, 4096);
	if (q2 != MAP_FAILED) munmap (q2, 4096);
	if (s1 >= 0) close (s1);
	if (s2 >= 0) close (s2);

	/* posix_spawn: the client end (no CLOEXEC) at its number; the server end (CLOEXEC) not given;
	 * a dup2 onto 30; the child receives the memfd and writes into it */
	int cs[2];
	socketpair (AF_UNIX, SOCK_SEQPACKET, 0, cs);
	fcntl (cs[0], F_SETFD, FD_CLOEXEC);			/* (the server end: ours only) */
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, cs[1], 30);
	char fdn[3][12];
	snprintf (fdn[0], sizeof fdn[0], "%d", cs[1]);
	snprintf (fdn[1], sizeof fdn[1], "%d", cs[0]);
	snprintf (fdn[2], sizeof fdn[2], "%d", 30);
	char *av[] = { (char *) s_self, "--child", "ipc", fdn[0], fdn[1], fdn[2], 0 };
	pid_t pid = 0;
	r = posix_spawn (&pid, s_self, &fa, 0, av, environ);
	posix_spawn_file_actions_destroy (&fa);
	close (cs[1]);
	int one[1] = { mfd };
	sn = send_fds (cs[0], "mem", one, 1);
	char ok[4] = "";
	ssize_t okn = recv (cs[0], ok, sizeof ok, 0);
	int status = 0;
	pid_t w = r == 0 ? waitpid (pid, &status, 0) : -1;
	CHECK ("posix_spawn: the socket at its number in the child, CLOEXEC kept out, a dup2 onto 30",
	       r == 0 && w == pid && WIFEXITED (status) && WEXITSTATUS (status) == 30, "spawn %d status %x", r, status);
	CHECK ("the child got the memfd, mapped it, wrote; we see it", sn == 3 && okn == 2 && m1 != MAP_FAILED && m1[50000] == 0x33,
	       "%d %d %x", sn, (int) okn, m1 != MAP_FAILED ? m1[50000] : 0);
	close (cs[0]);

	/* (ARMPI) a child killed while it holds 8 MB of shared memory, mapped and in a message never
	 * read, and a socket end: its peer sees the end (POLLHUP, recv 0), the memory comes back */
	int hs[2];
	socketpair (AF_UNIX, SOCK_SEQPACKET, 0, hs);
	fcntl (hs[0], F_SETFD, FD_CLOEXEC);
	long free0 = sysconf (_SC_AVPHYS_PAGES);
	char hn[12];
	snprintf (hn, sizeof hn, "%d", hs[1]);
	char *hv[] = { (char *) s_self, "--child", "ipchold", hn, 0 };
	pid = 0;
	r = posix_spawn (&pid, s_self, 0, 0, hv, environ);
	close (hs[1]);
	char rb[4] = "";
	ssize_t rn2 = r == 0 ? recv (hs[0], rb, sizeof rb, 0) : -1;
	long free1 = sysconf (_SC_AVPHYS_PAGES);
	if (r == 0)
		kill (pid, SIGKILL);
	status = 0;
	w = r == 0 ? waitpid (pid, &status, 0) : -1;
	struct pollfd hp = { hs[0], POLLIN, 0 };
	pr = poll (&hp, 1, 3000);
	ssize_t e2 = recv (hs[0], rb, sizeof rb, MSG_DONTWAIT);
	long free2 = 0;
	for (int i = 0; i < 20; i++)			/* (the kernel's reaper: every 20 ms) */
	{
		usleep (50000);
		free2 = sysconf (_SC_AVPHYS_PAGES);
		if (free2 >= free0 - 256)
			break;
	}
	CHECK ("a killed child (blocked in recv): its socket end closed (POLLHUP, recv 0)", r == 0 && rn2 == 1 && w == pid && pr == 1
	       && (hp.revents & POLLHUP) && e2 == 0, "spawn %d %d w %d poll %d %x recv %d", r, (int) rn2, (int) w, pr, hp.revents, (int) e2);
	CHECK ("a killed child: its 8 MB of shared memory (mapped, in flight) given back", free1 < free0 - 2000
	       && free2 >= free0 - 256, "pages %ld -> %ld -> %ld", free0, free1, free2);
	close (hs[0]);
	close (qq[0]);
	close (qq[1]);
	if (m1 != MAP_FAILED) munmap (m1, 100000);
	if (m2 != MAP_FAILED) munmap (m2, 100000);
	close (mfd);
	close (pp[0]);
	close (pp[1]);
	close (sp[0]);
	close (sp[1]);
}

/* ================================================================================== net == */
static void group_net (void)
{
	s_group = "net";
	test_loopback ();
	struct addrinfo h, *ai = 0;
	memset (&h, 0, sizeof h);
	h.ai_socktype = SOCK_STREAM;
	int g = getaddrinfo ("10.1.2.3", "https", &h, &ai);
	CHECK ("getaddrinfo numeric host + service name", g == 0 && ai && ai->ai_family == AF_INET &&
	       ntohs (((struct sockaddr_in *) ai->ai_addr)->sin_port) == 443 &&
	       ((struct sockaddr_in *) ai->ai_addr)->sin_addr.s_addr == inet_addr ("10.1.2.3"), "%d", g);
	if (ai)
		freeaddrinfo (ai);
	h.ai_family = AF_INET6;
	CHECK ("getaddrinfo AF_INET6 -> EAI_FAMILY", getaddrinfo ("localhost", "80", &h, &ai) == EAI_FAMILY, "");
	h.ai_family = AF_UNSPEC;
	h.ai_flags = AI_NUMERICHOST;
	CHECK ("getaddrinfo AI_NUMERICHOST with a name -> EAI_NONAME", getaddrinfo ("example.com", "80", &h, &ai) == EAI_NONAME, "");

	unsigned char a6[16];
	char s6[64], s4[16];
	struct in_addr a4;
	CHECK ("inet_pton / inet_ntop (IPv4, IPv6)",
	       inet_pton (AF_INET, "192.168.1.20", &a4) == 1 && !strcmp (inet_ntop (AF_INET, &a4, s4, sizeof s4), "192.168.1.20") &&
	       inet_pton (AF_INET6, "2001:db8::1", a6) == 1 && !strcmp (inet_ntop (AF_INET6, a6, s6, sizeof s6), "2001:db8::1"),
	       "%s %s", s4, s6);
	CHECK ("socket (AF_INET6) -> EAFNOSUPPORT", socket (AF_INET6, SOCK_STREAM, 0) == -1 && errno == EAFNOSUPPORT, "errno %d", errno);

	struct addrinfo *probe = 0;
	memset (&h, 0, sizeof h);
	h.ai_socktype = SOCK_STREAM;
	int up = getaddrinfo ("example.com", "80", &h, &probe) == 0;
	if (probe)
		freeaddrinfo (probe);
	if (!up)
	{
		skip ("DNS, TCP connect / send / recv, MSG_PEEK, names, UDP", "no network");
		return;
	}
	memset (&h, 0, sizeof h);
	h.ai_socktype = SOCK_STREAM;
	ai = 0;
	g = getaddrinfo ("example.com", "80", &h, &ai);
	CHECK ("getaddrinfo (\"example.com\") through the kernel's DNS", g == 0 && ai, "%s", gai_strerror (g));
	if (g != 0)
		return;

	int s = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	int c = connect (s, ai->ai_addr, ai->ai_addrlen);
	int ce = errno;
	if (c == -1 && ce == EINPROGRESS)
	{
		struct pollfd p = { s, POLLOUT, 0 };
		int pr = poll (&p, 1, 10000);
		int soerr = -1;
		socklen_t sl = sizeof soerr;
		getsockopt (s, SOL_SOCKET, SO_ERROR, &soerr, &sl);
		CHECK ("non-blocking connect: EINPROGRESS, poll (POLLOUT), SO_ERROR 0", pr == 1 && (p.revents & POLLOUT) && soerr == 0, "poll %d revents %x so_error %d", pr, p.revents, soerr);
	}
	else if (!k_net)
		CHECK ("connect (the old tcp_connect: blocking)", c == 0, "errno %d", ce);
	else
		fail ("non-blocking connect: EINPROGRESS, poll (POLLOUT), SO_ERROR 0", "connect %d errno %d", c, ce);
	freeaddrinfo (ai);

	const char *req = "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n";
	ssize_t w = send (s, req, strlen (req), MSG_NOSIGNAL);
	CHECK ("send", w == (ssize_t) strlen (req), "%zd errno %d", w, errno);
	struct pollfd p = { s, POLLIN, 0 };
	poll (&p, 1, 10000);
	char peek[8] = "", got[8] = "";
	ssize_t pk = recv (s, peek, 7, MSG_PEEK);
	fcntl (s, F_SETFL, 0);
	ssize_t rv = recv (s, got, 7, MSG_WAITALL);
	CHECK ("MSG_PEEK, then the same bytes read", pk == 7 && rv == 7 && !memcmp (peek, got, 7) && !memcmp (got, "HTTP/1.", 7), "'%s' '%s'", peek, got);

	struct sockaddr_in me, peer;
	socklen_t ml = sizeof me, pl = sizeof peer;
	int n1 = getsockname (s, (struct sockaddr *) &me, &ml), n2 = getpeername (s, (struct sockaddr *) &peer, &pl);
	if (!k_net)
		skip ("getsockname / getpeername", NOSYS);
	else
		CHECK ("getsockname / getpeername", n1 == 0 && n2 == 0 && ntohs (peer.sin_port) == 80 && ntohs (me.sin_port) != 0, "%d %d", n1, n2);
	char drain[4096];
	while (recv (s, drain, sizeof drain, 0) > 0)
		;
	close (s);

	int u = socket (AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in dns;
	memset (&dns, 0, sizeof dns);
	dns.sin_family = AF_INET;
	dns.sin_port = htons (53);
	dns.sin_addr.s_addr = inet_addr ("8.8.8.8");
	unsigned char q[] = { 0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1 };
	struct timeval to = { 3, 0 };
	setsockopt (u, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
	ssize_t us = sendto (u, q, sizeof q, 0, (struct sockaddr *) &dns, sizeof dns);
	unsigned char ans[512];
	struct sockaddr_in from;
	socklen_t fl = sizeof from;
	ssize_t ur = recvfrom (u, ans, sizeof ans, 0, (struct sockaddr *) &from, &fl);
	CHECK ("UDP: a DNS query with sendto / recvfrom", us == (ssize_t) sizeof q && ur > 12 && ans[0] == 0x12 && ans[1] == 0x34 && ntohs (from.sin_port) == 53,
	       "sent %zd, got %zd errno %d", us, ur, errno);
	close (u);
}

/* ================================================================================= loop == */
/* TCP and UDP between two sockets of this program on 127.0.0.1 -- not in the default groups:
 * Onyx's network stack may not loop back (the PC bench, tools/tests/posixsim, runs it). */
static void *t_server (void *a)
{
	int ls = (int) (long) a;
	struct pollfd p = { ls, POLLIN, 0 };
	if (poll (&p, 1, 5000) != 1)
		return (void *) 1;
	struct sockaddr_in peer;
	socklen_t pl = sizeof peer;
	int c = accept4 (ls, (struct sockaddr *) &peer, &pl, SOCK_CLOEXEC);
	if (c < 0)
		return (void *) 2;
	char b[16];
	ssize_t n = recv (c, b, sizeof b, MSG_WAITALL & 0);
	if (n != 4 || memcmp (b, "ping", 4))
	{
		close (c);
		return (void *) 3;
	}
	send (c, "pong!", 5, MSG_NOSIGNAL);
	usleep (50000);
	close (c);
	return 0;
}

static void group_loop (void)
{
	s_group = "loop";
	/* (without the v75 sockets: the old tcp_* calls -- a fixed port, a blocking accept) */
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	a.sin_port = k_net ? 0 : htons (47123);
	int ls = socket (AF_INET, SOCK_STREAM | (k_net ? SOCK_NONBLOCK : 0), 0);
	socklen_t al = sizeof a;
	int ok = ls >= 0 && bind (ls, (struct sockaddr *) &a, sizeof a) == 0 && listen (ls, 4) == 0 &&
		 (!k_net || (getsockname (ls, (struct sockaddr *) &a, &al) == 0 && a.sin_port != 0));
	CHECK (k_net ? "socket, bind (port 0), listen, getsockname" : "socket, bind, listen (the old tcp_listen)", ok, "errno %d", errno);
	if (!ok)
		return;
	if (k_net)
	{
		int e = accept (ls, 0, 0);
		CHECK ("accept on a non-blocking socket with nobody -> EAGAIN", e == -1 && errno == EAGAIN, "%d errno %d", e, errno);
	}

	pthread_t th;
	pthread_create (&th, 0, t_server, (void *) (long) ls);
	int s = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	int c = connect (s, (struct sockaddr *) &a, sizeof a);
	int ce = errno;
	struct pollfd p = { s, POLLOUT, 0 };
	int pr = poll (&p, 1, 3000);
	int soerr = -1;
	socklen_t sl = sizeof soerr;
	getsockopt (s, SOL_SOCKET, SO_ERROR, &soerr, &sl);
	CHECK ("non-blocking connect, poll (POLLOUT), SO_ERROR", (c == 0 || ce == EINPROGRESS) && pr == 1 && (p.revents & POLLOUT) && soerr == 0,
	       "connect %d errno %d, poll %d revents %x, so_error %d", c, ce, pr, p.revents, soerr);
	ssize_t w = send (s, "ping", 4, MSG_NOSIGNAL);
	p.events = POLLIN;
	pr = poll (&p, 1, 3000);
	char pk[8] = "", got[8] = "";
	ssize_t r1 = recv (s, pk, 5, MSG_PEEK);
	ssize_t r2 = recv (s, got, 5, 0);
	CHECK ("send, poll (POLLIN), MSG_PEEK then recv", w == 4 && pr == 1 && r1 == 5 && r2 == 5 && !memcmp (pk, "pong!", 5) && !memcmp (got, "pong!", 5),
	       "send %zd poll %d peek %zd recv %zd", w, pr, r1, r2);
	struct sockaddr_in peer;
	socklen_t pl = sizeof peer;
	CHECK ("getpeername", getpeername (s, (struct sockaddr *) &peer, &pl) == 0 && peer.sin_port == a.sin_port, "port %d", ntohs (peer.sin_port));
	if (!k_net)
		fcntl (s, F_SETFL, 0);			/* (the old recv: blocking until the end) */
	pr = poll (&p, 1, 3000);
	ssize_t r3 = recv (s, got, sizeof got, 0);
	CHECK ("the peer closes: poll wakes, recv -> 0", pr == 1 && r3 == 0, "poll %d revents %x recv %zd errno %d", pr, p.revents, r3, errno);
	void *sr = (void *) 9;
	pthread_join (th, &sr);
	CHECK ("the server side: poll on the listening socket, accept4, recv", sr == 0, "%p", sr);
	close (s);
	if (!k_net)
	{
		close (ls);
		skip ("connect to a closed port, UDP", NOSYS);
		return;
	}

	/* nothing listens on the listening socket's port once it is closed */
	close (ls);
	int s2 = socket (AF_INET, SOCK_STREAM, 0);
	int c2 = connect (s2, (struct sockaddr *) &a, sizeof a);
	CHECK ("connect to a closed port -> ECONNREFUSED", c2 == -1 && errno == ECONNREFUSED, "%d errno %d", c2, errno);
	close (s2);

	int u1 = socket (AF_INET, SOCK_DGRAM, 0), u2 = socket (AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in ua = a;
	ua.sin_port = 0;
	al = sizeof ua;
	bind (u1, (struct sockaddr *) &ua, sizeof ua);
	getsockname (u1, (struct sockaddr *) &ua, &al);
	struct timeval to = { 2, 0 };
	setsockopt (u1, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
	ssize_t us = sendto (u2, "hey", 3, 0, (struct sockaddr *) &ua, sizeof ua);
	char ub[8] = "";
	struct sockaddr_in from;
	socklen_t fl = sizeof from;
	ssize_t ur = recvfrom (u1, ub, sizeof ub, 0, (struct sockaddr *) &from, &fl);
	CHECK ("UDP: sendto / recvfrom (SO_RCVTIMEO)", us == 3 && ur == 3 && !memcmp (ub, "hey", 3) && from.sin_addr.s_addr == htonl (INADDR_LOOPBACK),
	       "sent %zd got %zd errno %d", us, ur, errno);
	close (u1);
	close (u2);
}

/* ================================================================================= misc == */
static volatile int s_sig;
static void on_sig (int s) { s_sig = s; }

static int phdr_seen (struct dl_phdr_info *i, size_t size, void *data)
{
	(void) size;
	int *seen = (int *) data;
	*seen = 1;
	for (int n = 0; n < i->dlpi_phnum; n++)
	{
		if (i->dlpi_phdr[n].p_type == PT_LOAD && i->dlpi_phdr[n].p_flags & PF_X)
			*seen |= 2;
		if (i->dlpi_phdr[n].p_type == PT_TLS)
			*seen |= 4;
		if (i->dlpi_phdr[n].p_type == 0x6474E550)	/* PT_GNU_EH_FRAME */
			*seen |= 8;
	}
	return 0;
}

static void group_misc (void)
{
	s_group = "misc";
	CHECK ("sysconf (_SC_NPROCESSORS_ONLN) = 4 (the Pi's cores)", sysconf (_SC_NPROCESSORS_ONLN) == 4, "%ld", sysconf (_SC_NPROCESSORS_ONLN));
	struct utsname u;
	CHECK ("uname: ARMPI, aarch64", uname (&u) == 0 && !strcmp (u.sysname, "ARMPI") && !strcmp (u.machine, "aarch64"), "%s %s", u.sysname, u.machine);
	unsigned char r[32];
	memset (r, 0, sizeof r);
	int nz = 0;
	ssize_t gr = getrandom (r, sizeof r, 0);
	for (int i = 0; i < 32; i++)
		nz += r[i] != 0;
	CHECK ("getrandom, getentropy", gr == 32 && nz > 16 && getentropy (r, 32) == 0, "%zd %d", gr, nz);

	struct sigaction sa, old;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_sig;
	sigaction (SIGUSR1, &sa, &old);
	raise (SIGUSR1);
	CHECK ("sigaction + raise: the handler runs", s_sig == SIGUSR1, "%d", s_sig);
	s_sig = 0;
	kill (getpid (), SIGUSR1);
	CHECK ("kill (getpid (), sig): the handler runs", s_sig == SIGUSR1, "%d", s_sig);
	CHECK ("signal (SIGPIPE, SIG_IGN), kill (self, 0)", signal (SIGPIPE, SIG_IGN) != SIG_ERR && kill (getpid (), 0) == 0, "");
	sigset_t ss;
	sigemptyset (&ss);
	sigaddset (&ss, SIGINT);
	CHECK ("sigprocmask / sigismember", sigprocmask (SIG_BLOCK, &ss, 0) == 0 && sigismember (&ss, SIGINT) == 1, "");

	struct rlimit rl;
	CHECK ("getrlimit (RLIMIT_NOFILE) = 1024", getrlimit (RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur == 1024, "%llu", (unsigned long long) rl.rlim_cur);
	char hn[64];
	CHECK ("gethostname", gethostname (hn, sizeof hn) == 0 && hn[0], "");
	CHECK ("isatty (1) on the console, not on a pipe", isatty (1) == 1, "");
	errno = 0;
	void *dl = dlopen ("libfoo.so", 0);
	CHECK ("dlopen fails cleanly (static programs)", dl == 0 && dlerror () != 0, "");
	CHECK ("strerror (ENOSYS)", strerror (ENOSYS) && strlen (strerror (ENOSYS)) > 3, "");

	int seen = 0;
	CHECK ("dl_iterate_phdr: one object", dl_iterate_phdr (phdr_seen, &seen) == 0 && seen == 1 + 2 + 4 + 8, "%d", seen);
	Dl_info di;
	static char exe[PATH_MAX];
	ssize_t en = readlink ("/proc/self/exe", exe, sizeof exe - 1);
	CHECK ("readlink (/proc/self/exe)", en > 0, "%d", errno);
	if (en > 0)
		exe[en] = 0;
	CHECK ("dladdr: the program itself", dladdr ((void *) group_misc, &di) != 0 && en > 0 && !strcmp (di.dli_fname, exe), "");
	CHECK ("dladdr: not in the program", dladdr ((void *) 16, &di) == 0, "");
	cpu_set_t cs;
	CHECK ("sched_getaffinity: every core", sched_getaffinity (0, sizeof cs, &cs) == 0 && CPU_COUNT (&cs) == sysconf (_SC_NPROCESSORS_ONLN), "%d", errno);
	CHECK ("sched_setaffinity", sched_setaffinity (0, sizeof cs, &cs) == 0, "%d", errno);
	struct statfs fs;
	CHECK ("statfs", statfs ("/tmp", &fs) == 0 && fs.f_bsize == 4096, "%d", errno);
	gid_t gl[4];
	int ng = 4;
	CHECK ("getgrouplist", getgrouplist ("root", 0, gl, &ng) == 1 && ng == 1 && gl[0] == 0, "");
	CHECK ("realpath (/proc/self/exe)", en > 0 && realpath ("/proc/self/exe", exe) != 0, "%d", errno);
	CHECK ("syscall : gettid, ENOSYS", syscall (SYS_gettid) == gettid () && syscall (9999) == -1 && errno == ENOSYS, "%d", errno);
	CHECK ("fnmatch : *, ?, [a-c]", fnmatch ("*.t?t", "a.txt", 0) == 0 && fnmatch ("[a-c]x", "bx", 0) == 0 && fnmatch ("[!a]x", "ax", 0) == FNM_NOMATCH, "");
	CHECK ("fnmatch : FNM_PATHNAME, FNM_PERIOD", fnmatch ("*", "a/b", FNM_PATHNAME) == FNM_NOMATCH && fnmatch ("*", ".a", FNM_PERIOD) == FNM_NOMATCH
	       && fnmatch ("a/*", "a/b", FNM_PATHNAME) == 0, "");
}

/* The C++ part is its own program, built with WP-TC's aarch64-onyx-elf (posixtest-cxx.cpp), next
 * to this one: run it (its PASS / FAIL lines on the same output), one check on its exit status
 * (its number of failures). */
/* ---- signaux : fautes, ucontext, un thread precis, pile de signaux, masques, membarrier ---- */
static volatile unsigned long s_fault_addr;
static volatile int s_alt_ok, s_tid_hits;
static volatile pthread_t s_sig_thread;
static char s_altstack[32768] __attribute__ ((aligned (16)));

static void on_fault (int s, siginfo_t *si, void *uc)
{
	ucontext_t *c = (ucontext_t *) uc;
	s_fault_addr = (unsigned long) si->si_addr;
	s_sig = s;
	c->uc_mcontext.regs[19] = 0x1234;		/* vu au retour */
	c->uc_mcontext.pc += 4;				/* (l'instruction fautive sautee) */
}

static void on_alt (int s, siginfo_t *si, void *uc)
{
	char here;
	ucontext_t *c = (ucontext_t *) uc;
	(void) s; (void) si;
	s_alt_ok = &here >= s_altstack && &here < s_altstack + sizeof s_altstack;
	c->uc_mcontext.pc += 4;
}

static void on_thread (int s)
{
	(void) s;
	if (pthread_equal (pthread_self (), (pthread_t) s_sig_thread))
		s_tid_hits = s_tid_hits + 1;
}

static void *sig_worker (void *a)
{
	(void) a;
	s_sig_thread = pthread_self ();
	while (!s_tid_hits)
		;					/* tourne : le signal l'interrompt */
	return 0;
}

/* un debordement de pile : la page de garde, rattrapee sur la pile de signaux du thread */
static char s_altstack2[32768] __attribute__ ((aligned (16)));
static jmp_buf s_overflow;
static volatile unsigned long s_guard_fault, s_guard_lo, s_guard_size;

static void on_guard (int s, siginfo_t *si, void *uc)
{
	(void) s; (void) uc;
	s_guard_fault = (unsigned long) si->si_addr;
	longjmp (s_overflow, 1);
}

__attribute__ ((noinline)) static int deep (int n)
{
	volatile char b[4096];
	b[0] = (char) n;
	return deep (n + 1) + b[0];
}

static void *t_overflow (void *a)
{
	(void) a;
	pthread_attr_t at;
	void *lo;
	size_t size, guard;
	pthread_getattr_np (pthread_self (), &at);
	pthread_attr_getstack (&at, &lo, &size);
	pthread_attr_getguardsize (&at, &guard);
	pthread_attr_destroy (&at);
	s_guard_lo = (unsigned long) lo;
	s_guard_size = guard;
	stack_t ss;
	memset (&ss, 0, sizeof ss);
	ss.ss_sp = s_altstack2;
	ss.ss_size = sizeof s_altstack2;
	sigaltstack (&ss, 0);
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_sigaction = on_guard;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction (SIGSEGV, &sa, 0);
	if (setjmp (s_overflow) == 0)
		deep (0);
	return 0;
}

static volatile int s_rt;
static void on_rt (int s) { s_rt = s; }

static void *rt_worker (void *a)
{
	(void) a;
	s_sig_thread = pthread_self ();
	while (!s_rt)
		;
	return 0;
}

static void group_signal (void)
{
	s_group = "signal";
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_sigaction = on_fault;
	sa.sa_flags = SA_SIGINFO;
	CHECK ("sigaction SA_SIGINFO", sigaction (SIGSEGV, &sa, 0) == 0, "%d", errno);
	s_sig = 0;
	register unsigned long r19 __asm__ ("x19") = 0;
	__asm__ volatile ("ldr x0, [%1]" : "+r" (r19) : "r" (0x10UL) : "x0", "memory");
	CHECK ("a fault reaches the handler: si_addr", s_sig == SIGSEGV && s_fault_addr == 0x10, "%d %lx", s_sig, (unsigned long) s_fault_addr);
	CHECK ("ucontext: a register changed by the handler", r19 == 0x1234, "%lx", (unsigned long) r19);

	stack_t ss, old;
	memset (&ss, 0, sizeof ss);
	ss.ss_sp = s_altstack;
	ss.ss_size = sizeof s_altstack;
	CHECK ("sigaltstack", sigaltstack (&ss, 0) == 0 && sigaltstack (0, &old) == 0 && old.ss_sp == s_altstack, "%d", errno);
	sa.sa_sigaction = on_alt;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction (SIGSEGV, &sa, 0);
	__asm__ volatile ("mov x9, sp\n\tmov sp, %0\n\tstr xzr, [sp]\n\tmov sp, x9" : : "r" (0x10UL) : "x9", "memory");
	CHECK ("a lost stack: the handler runs on the signal stack", s_alt_ok == 1, "%d", s_alt_ok);
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = 0;
	sigaction (SIGSEGV, &sa, 0);

	sigset_t ss1, cur;
	s_sig = 0;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_sig;
	sigaction (SIGUSR2, &sa, 0);
	sigemptyset (&ss1);
	sigaddset (&ss1, SIGUSR2);
	sigprocmask (SIG_BLOCK, &ss1, 0);
	raise (SIGUSR2);
	sigpending (&cur);
	CHECK ("a blocked signal waits", s_sig == 0 && sigismember (&cur, SIGUSR2) == 1, "%d", s_sig);
	sigprocmask (SIG_UNBLOCK, &ss1, 0);
	CHECK ("delivered when unblocked", s_sig == SIGUSR2, "%d", s_sig);

	pthread_t t;
	sa.sa_handler = on_thread;
	sigaction (SIGUSR1, &sa, 0);
	CHECK ("pthread_create", pthread_create (&t, 0, sig_worker, 0) == 0, "");
	while (!s_sig_thread)
		sched_yield ();
	CHECK ("pthread_kill of another thread", pthread_kill (t, SIGUSR1) == 0, "");
	pthread_join (t, 0);
	CHECK ("the signal reached that thread", s_tid_hits >= 1, "%d", s_tid_hits);

	CHECK ("pthread_create (small stack, guard page)", pthread_create (&t, 0, t_overflow, 0) == 0, "");
	pthread_join (t, 0);
	CHECK ("a stack overflow faults on the guard page", s_guard_size >= 4096 && s_guard_fault >= s_guard_lo - s_guard_size
	       && s_guard_fault < s_guard_lo, "fault %lx lo %lx guard %lx", (unsigned long) s_guard_fault, (unsigned long) s_guard_lo,
	       (unsigned long) s_guard_size);
	sa.sa_handler = SIG_DFL;
	sigaction (SIGSEGV, &sa, 0);

	sigset_t rt;
	sigemptyset (&rt);
	CHECK ("sigset: SIGRTMIN", sigaddset (&rt, SIGRTMIN) == 0 && sigismember (&rt, SIGRTMIN) == 1 && rt == 1UL << SIGRTMIN, "%lx", (unsigned long) rt);
	sa.sa_handler = on_rt;
	CHECK ("sigaction (SIGRTMIN)", sigaction (SIGRTMIN, &sa, 0) == 0, "%d", errno);
	s_sig_thread = 0;
	pthread_create (&t, 0, rt_worker, 0);
	while (!s_sig_thread)
		sched_yield ();
	CHECK ("pthread_kill (SIGRTMIN)", pthread_kill (t, SIGRTMIN) == 0, "");
	pthread_join (t, 0);
	CHECK ("SIGRTMIN delivered", s_rt == SIGRTMIN, "%d", s_rt);

	int q = membarrier (MEMBARRIER_CMD_QUERY, 0, 0);
	CHECK ("membarrier query", q > 0 && (q & MEMBARRIER_CMD_PRIVATE_EXPEDITED), "%d", q);
	CHECK ("membarrier private", membarrier (MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0) == 0
	       && membarrier (MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0) == 0, "%d", errno);
}

static void group_cxx (void)
{
	s_group = "cxx";
	char path[256];
	const char *slash = strrchr (s_self, '/');
	size_t n = slash ? (size_t) (slash - s_self) : 0;
	if (n == 0 || n + sizeof "/posixtest-cxx" > sizeof path)
	{
		strcpy (path, "SD0:/bin");
		n = strlen (path);
	}
	else
		memcpy (path, s_self, n);
	strcpy (path + n, "/posixtest-cxx");
	struct stat st;
	if (stat (path, &st) != 0)
	{
		skip ("std::thread, mutex, condition_variable, thread_local, statics, filesystem, steady_clock",
		      "no posixtest-cxx next to posixtest (built with the aarch64-onyx-elf toolchain, WP-TC)");
		return;
	}
	fflush (stdout);
	char *argv[] = { path, 0 };
	extern char **environ;
	pid_t pid;
	int r = posix_spawn (&pid, path, 0, 0, argv, environ);
	int status = 0;
	if (r == 0 && waitpid (pid, &status, 0) != pid)
		r = errno;
	CHECK ("posixtest-cxx: std::thread, mutex, condition_variable, thread_local, statics, filesystem, "
	       "steady_clock, async / future, exceptions across threads",
	       r == 0 && WIFEXITED (status) && WEXITSTATUS (status) == 0,
	       "spawn %d, status %x: %d failed", r, status, WIFEXITED (status) ? WEXITSTATUS (status) : -1);
}

int main (int argc, char **argv)
{
	if (argc > 2 && !strcmp (argv[1], "--child"))
		return child_main (argc, argv);
	if (argv[0] && strchr (argv[0], ':'))
		s_self = argv[0];

	setvbuf (stdout, 0, _IOLBF, 0);
	probe_kernel ();
	static const char *all[] = { "mem", "thread", "file", "io", "time", "proc", "signal", "ipc", "net", "misc", "cxx", 0 };
	const char *const *groups = all;
	const char *sel[16];
	const char *fileDir = 0;
	if (argc > 1)
	{
		int n = 0;
		for (int i = 1; i < argc && n < 15; i++)
		{
			if (strchr (argv[i], ':') || argv[i][0] == '/')
				fileDir = argv[i];
			else
				sel[n++] = argv[i];
		}
		sel[n] = 0;
		if (n)
			groups = sel;
	}
	for (int i = 0; groups[i]; i++)
	{
		const char *g = groups[i];
		if (!strcmp (g, "mem")) group_mem ();
		else if (!strcmp (g, "thread")) group_thread ();
		else if (!strcmp (g, "file")) group_file (fileDir);
		else if (!strcmp (g, "io")) group_io ();
		else if (!strcmp (g, "time")) group_time ();
		else if (!strcmp (g, "proc")) group_proc ();
		else if (!strcmp (g, "signal")) group_signal ();
		else if (!strcmp (g, "ipc")) group_ipc ();
		else if (!strcmp (g, "net")) group_net ();
		else if (!strcmp (g, "loop")) group_loop ();
		else if (!strcmp (g, "misc")) group_misc ();
		else if (!strcmp (g, "cxx")) group_cxx ();
		else printf ("posixtest: no group '%s' (mem thread file io time proc ipc net misc cxx loop)\n", g);
	}
	printf ("posixtest: %d passed, %d failed, %d skipped\n", s_pass, s_fail, s_skip);
	return s_fail;
}
