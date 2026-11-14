/*
 * wakebench.c -- banc des reveils perdus de libposix et du noyau (docs/etude/PC-PILOTES.md,
 * « Réveils perdus »). Chaque scenario tourne N fois ; un chien de garde (un thread) arrete tout
 * si rien n'avance pendant WAKEBENCH_WD secondes (defaut 30) : « BLOQUE scenario iteration »,
 * code 2 ; avec WAKEBENCH_DUMP, l'etat des tubes de vfs (V_PIPE_DUMP) va au journal.
 *
 *   wakebench [N] [scenario...]        (defaut : 20, tous)
 *
 * Scenarios (les motifs de git, de subprocess de Python, de WebKit) :
 *   cowfutex   un thread attend un verrou tenu par le thread principal, qui fait un fork puis
 *              rend le verrou (copie sur ecriture de la page du verrou : futex par adresse physique)
 *   forklock   threads qui prennent un verrou et font des malloc en boucle pendant des fork ; le
 *              fils fait un malloc
 *   pipefork   un thread ecrit dans un tube (bloque, plein) pendant un fork + exec d'un lecteur :
 *              la fin du tube doit arriver (reference du thread d'ecriture recopiee dans le fils)
 *   pipeeof    poll (sans delai) d'un tube dont le dernier ecrivain se ferme dans un autre thread
 *   gitdemux   le schema de git en protocole 2 : fils ecrivain, thread qui recopie, fils lecteur,
 *              waitpid pendant ce temps
 *   sockpair   socketpair : un fils ecrit et part ; poll et lecture jusqu'a la fin ; SCM_RIGHTS
 *   sigchld    SIGCHLD et sigsuspend : un fils qui part tout de suite, le signal attendu
 *   condvar    pthread_cond ping-pong entre deux threads, plus un semaphore
 *   waitthr    waitpid dans plusieurs threads a la fois, chacun ses fils
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence: Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions: The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *s_self = "/bin/wakebench";
static volatile unsigned long s_progress;
static const char *volatile s_scen = "";
static volatile int s_iter;

static void tick (void) { __atomic_add_fetch (&s_progress, 1, __ATOMIC_RELAXED); }

static void fail (const char *what)
{
	printf ("ECHEC %s iteration %d : %s (errno %d)\n", s_scen, s_iter, what, errno);
	fflush (stdout);
	_exit (1);
}

/* vfs : l'etat des tubes au journal (op V_PIPE_DUMP de vfs, s'il la connait) */
struct vmsg { long op, a, b, c, d; unsigned len, pid; unsigned char data[464]; };
extern long __armpi_vfs_call (void *);
static void vfs_dump (void)
{
	struct vmsg m;
	memset (&m, 0, sizeof m);
	m.op = 52;
	__armpi_vfs_call (&m);
}

static int s_wd = 30;
static void *watchdog (void *arg)
{
	(void) arg;
	sigset_t all;				/* (les signaux vont aux threads des scenarios) */
	sigfillset (&all);
	pthread_sigmask (SIG_BLOCK, &all, 0);
	unsigned long last = s_progress;
	int still = 0;
	for (;;)
	{
		sleep (1);
		unsigned long p = s_progress;
		if (p != last)
		{
			last = p;
			still = 0;
			continue;
		}
		if (++still >= s_wd)
		{
			printf ("BLOQUE %s iteration %d (%d s sans progres)\n", s_scen, s_iter, s_wd);
			fflush (stdout);
			if (getenv ("WAKEBENCH_DUMP"))
				vfs_dump ();
			_exit (2);
		}
	}
	return 0;
}

static void reap (pid_t pid, int want)
{
	int st = 0;
	pid_t r;
	do
		r = waitpid (pid, &st, 0);
	while (r < 0 && errno == EINTR);
	if (r != pid)
		fail ("waitpid");
	if (want >= 0 && (!WIFEXITED (st) || WEXITSTATUS (st) != want))
	{
		printf ("  statut du fils %d : %x\n", pid, st);
		fail ("code du fils");
	}
}

/* ---- cowfutex */
static pthread_mutex_t s_m = PTHREAD_MUTEX_INITIALIZER;
static volatile int s_got;
static void *cow_waiter (void *arg)
{
	(void) arg;
	pthread_mutex_lock (&s_m);
	s_got = 1;
	pthread_mutex_unlock (&s_m);
	return 0;
}
static void sc_cowfutex (void)
{
	pthread_mutex_lock (&s_m);
	s_got = 0;
	pthread_t t;
	if (pthread_create (&t, 0, cow_waiter, 0))
		fail ("pthread_create");
	usleep (20000);			/* (le thread attend le verrou dans le noyau) */
	pid_t c = fork ();
	if (c < 0)
		fail ("fork");
	if (c == 0)
	{
		usleep (100000);	/* le fils garde les pages partagees un moment */
		_exit (0);
	}
	pthread_mutex_unlock (&s_m);	/* (ecriture : copie de la page du verrou dans le pere) */
	pthread_join (t, 0);
	if (!s_got)
		fail ("verrou");
	reap (c, 0);
}

/* ---- forklock */
static volatile int s_stop;
static void *fl_worker (void *arg)
{
	(void) arg;
	while (!s_stop)
	{
		pthread_mutex_lock (&s_m);
		void *p = malloc (64 + (rand () & 1023));
		pthread_mutex_unlock (&s_m);
		free (p);
	}
	return 0;
}
static void sc_forklock (void)
{
	pthread_t t[3];
	s_stop = 0;
	for (int i = 0; i < 3; i++)
		pthread_create (&t[i], 0, fl_worker, 0);
	for (int k = 0; k < 8; k++)
	{
		pid_t c = fork ();
		if (c == 0)
		{
			/* malloc dans le fils (son verrou est remis en etat par fork ; s_m, lui, peut etre
			 * reste pris par un thread du pere : POSIX ne permet pas de le prendre ici) */
			void *p = malloc (100);
			free (p);
			_exit (0);
		}
		if (c < 0)
			fail ("fork");
		reap (c, 0);
		tick ();
	}
	s_stop = 1;
	for (int i = 0; i < 3; i++)
		pthread_join (t[i], 0);
}

/* ---- pipefork : le lecteur (fils, apres exec) doit voir la fin */
#define PF_BYTES 65536
static int s_pw;
static void *pf_writer (void *arg)
{
	(void) arg;
	static char buf[PF_BYTES];
	memset (buf, 'x', sizeof buf);
	size_t done = 0;
	while (done < sizeof buf)
	{
		ssize_t k = write (s_pw, buf + done, sizeof buf - done);
		if (k <= 0)
			fail ("write");
		done += (size_t) k;
	}
	close (s_pw);
	return 0;
}
static int reader_main (long want)
{
	char b[4096];
	long n = 0;
	for (;;)
	{
		ssize_t k = read (0, b, sizeof b);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			break;
		n += k;
	}
	return n == want ? 0 : 3;
}
static void sc_pipefork (void)
{
	int p[2];
	if (pipe2 (p, O_CLOEXEC))
		fail ("pipe");
	s_pw = p[1];
	pthread_t t;
	pthread_create (&t, 0, pf_writer, 0);
	usleep (5000);			/* (l'ecrivain est dans write : le tube est plein) */
	pid_t c = fork ();
	if (c < 0)
		fail ("fork");
	if (c == 0)
	{
		dup2 (p[0], 0);
		execl (s_self, s_self, "--reader", "65536", (char *) 0);
		_exit (4);
	}
	close (p[0]);
	pthread_join (t, 0);
	reap (c, 0);
}

/* ---- pipeeof */
static void *pe_closer (void *arg)
{
	int w = *(int *) arg;
	usleep (2000);
	if (write (w, "abc", 3) != 3)
		fail ("write");
	usleep (2000);
	close (w);
	return 0;
}
static void sc_pipeeof (void)
{
	int p[2];
	if (pipe (p))
		fail ("pipe");
	pthread_t t;
	pthread_create (&t, 0, pe_closer, &p[1]);
	int got = 0, eof = 0;
	while (!eof)
	{
		struct pollfd f = { p[0], POLLIN, 0 };
		int r = poll (&f, 1, -1);
		if (r < 0 && errno == EINTR)
			continue;
		if (r != 1)
			fail ("poll");
		char b[16];
		ssize_t k = read (p[0], b, sizeof b);
		if (k < 0)
			fail ("read");
		if (k == 0)
			eof = 1;
		got += (int) k;
	}
	pthread_join (t, 0);
	close (p[0]);
	if (got != 3)
		fail ("octets");
}

/* ---- gitdemux : fils ecrivain -> thread qui recopie -> fils lecteur (apres exec) */
struct gd { int in, out; };
static void *gd_copier (void *arg)
{
	struct gd *g = (struct gd *) arg;
	char b[1000];
	for (;;)
	{
		ssize_t k = read (g->in, b, sizeof b);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			break;
		for (ssize_t d = 0; d < k;)
		{
			ssize_t w = write (g->out, b + d, (size_t) (k - d));
			if (w <= 0)
				fail ("write (copie)");
			d += w;
		}
	}
	close (g->in);
	close (g->out);
	return 0;
}
static void sc_gitdemux (void)
{
	int a[2], b[2];
	if (pipe (a) || pipe (b))
		fail ("pipe");
	pid_t w = fork ();		/* l'aide (git-remote-http) : ecrit 100 Ko puis part */
	if (w == 0)
	{
		close (a[0]);
		close (b[0]);
		close (b[1]);
		static char buf[100000];
		memset (buf, 'p', sizeof buf);
		size_t d = 0;
		while (d < sizeof buf)
		{
			ssize_t k = write (a[1], buf + d, sizeof buf - d);
			if (k <= 0)
				_exit (5);
			d += (size_t) k;
		}
		_exit (0);
	}
	close (a[1]);
	struct gd g = { a[0], b[1] };
	fcntl (b[1], F_SETFD, FD_CLOEXEC);	/* (git : set_cloexec (proc_out)) */
	pthread_t t;
	pthread_create (&t, 0, gd_copier, &g);
	pid_t r = fork ();		/* index-pack */
	if (r == 0)
	{
		dup2 (b[0], 0);
		close (b[0]);
		execl (s_self, s_self, "--reader", "100000", (char *) 0);
		_exit (4);
	}
	close (b[0]);
	reap (w, 0);
	reap (r, 0);
	pthread_join (t, 0);
}

/* ---- sockpair */
static void sc_sockpair (void)
{
	int s[2];
	if (socketpair (AF_UNIX, SOCK_STREAM, 0, s))
		fail ("socketpair");
	pid_t c = fork ();
	if (c == 0)
	{
		close (s[0]);
		for (int i = 0; i < 50; i++)
			if (write (s[1], "0123456789", 10) != 10)
				_exit (5);
		/* un descripteur passe : l'autre bout d'un nouveau socketpair */
		int x[2];
		if (socketpair (AF_UNIX, SOCK_STREAM, 0, x))
			_exit (6);
		char cb[CMSG_SPACE (sizeof (int))];
		memset (cb, 0, sizeof cb);
		struct iovec v = { (void *) "F", 1 };
		struct msghdr mh;
		memset (&mh, 0, sizeof mh);
		mh.msg_iov = &v;
		mh.msg_iovlen = 1;
		mh.msg_control = cb;
		mh.msg_controllen = sizeof cb;
		struct cmsghdr *cm = CMSG_FIRSTHDR (&mh);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_RIGHTS;
		cm->cmsg_len = CMSG_LEN (sizeof (int));
		memcpy (CMSG_DATA (cm), &x[1], sizeof (int));
		if (sendmsg (s[1], &mh, 0) != 1)
			_exit (7);
		close (x[1]);
		if (write (x[0], "ok", 2) != 2)
			_exit (8);
		close (x[0]);
		_exit (0);
	}
	close (s[1]);
	int n = 0, fd = -1;
	for (;;)
	{
		struct pollfd f = { s[0], POLLIN, 0 };
		if (poll (&f, 1, -1) < 0 && errno != EINTR)
			fail ("poll");
		char b[64], cb[CMSG_SPACE (sizeof (int))];
		struct iovec v = { b, n < 500 ? (size_t) (500 - n) : 1 };
		if (v.iov_len > sizeof b)
			v.iov_len = sizeof b;
		struct msghdr mh;
		memset (&mh, 0, sizeof mh);
		mh.msg_iov = &v;
		mh.msg_iovlen = 1;
		mh.msg_control = cb;
		mh.msg_controllen = sizeof cb;
		ssize_t k = recvmsg (s[0], &mh, 0);
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0)
			fail ("recvmsg");
		if (k == 0)
			break;
		struct cmsghdr *cm = CMSG_FIRSTHDR (&mh);
		if (cm && cm->cmsg_type == SCM_RIGHTS)
			memcpy (&fd, CMSG_DATA (cm), sizeof (int));
		n += (int) k;
	}
	close (s[0]);
	if (n != 501)
		fail ("octets du socket");
	if (fd < 0)
		fail ("SCM_RIGHTS");
	char b[8];
	int m = 0;
	ssize_t k;
	while ((k = read (fd, b, sizeof b)) > 0)
		m += (int) k;
	close (fd);
	if (m != 2)
		fail ("descripteur passe");
	reap (c, 0);
}

/* ---- sigchld */
static volatile sig_atomic_t s_chld;
static void on_chld (int sig) { (void) sig; s_chld++; }
static void sc_sigchld (void)
{
	sigset_t blk, old;
	sigemptyset (&blk);
	sigaddset (&blk, SIGCHLD);
	sigprocmask (SIG_BLOCK, &blk, &old);
	int before = s_chld;
	pid_t c = fork ();
	if (c == 0)
		_exit (0);
	if (c < 0)
		fail ("fork");
	sigset_t none = old;
	sigdelset (&none, SIGCHLD);
	while (s_chld == before)
		sigsuspend (&none);
	sigprocmask (SIG_SETMASK, &old, 0);
	reap (c, 0);
}

/* ---- condvar */
static pthread_mutex_t s_cm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_cv = PTHREAD_COND_INITIALIZER;
static int s_turn;
static sem_t s_sem;
#define CV_ROUNDS 200
static void *cv_peer (void *arg)
{
	(void) arg;
	for (int i = 0; i < CV_ROUNDS; i++)
	{
		pthread_mutex_lock (&s_cm);
		while (s_turn != 1)
			pthread_cond_wait (&s_cv, &s_cm);
		s_turn = 0;
		pthread_cond_broadcast (&s_cv);
		pthread_mutex_unlock (&s_cm);
		sem_post (&s_sem);
	}
	return 0;
}
static void sc_condvar (void)
{
	s_turn = 0;
	sem_init (&s_sem, 0, 0);
	pthread_t t;
	pthread_create (&t, 0, cv_peer, 0);
	for (int i = 0; i < CV_ROUNDS; i++)
	{
		pthread_mutex_lock (&s_cm);
		while (s_turn != 0)
			pthread_cond_wait (&s_cv, &s_cm);
		s_turn = 1;
		pthread_cond_signal (&s_cv);
		pthread_mutex_unlock (&s_cm);
		while (sem_wait (&s_sem) && errno == EINTR)
			;
		tick ();
	}
	pthread_join (t, 0);
	sem_destroy (&s_sem);
}

/* ---- waitthr */
static void *wt_body (void *arg)
{
	(void) arg;
	for (int i = 0; i < 3; i++)
	{
		pid_t c = fork ();
		if (c == 0)
		{
			usleep (1000 * (rand () % 5));
			_exit (7);
		}
		if (c < 0)
			fail ("fork");
		reap (c, 7);
		tick ();
	}
	return 0;
}
static void sc_waitthr (void)
{
	pthread_t t[3];
	for (int i = 0; i < 3; i++)
		pthread_create (&t[i], 0, wt_body, 0);
	for (int i = 0; i < 3; i++)
		pthread_join (t[i], 0);
}

static const struct { const char *name; void (*fn) (void); } s_tab[] = {
	{ "cowfutex", sc_cowfutex }, { "forklock", sc_forklock }, { "pipefork", sc_pipefork },
	{ "pipeeof", sc_pipeeof }, { "gitdemux", sc_gitdemux }, { "sockpair", sc_sockpair },
	{ "sigchld", sc_sigchld }, { "condvar", sc_condvar }, { "waitthr", sc_waitthr },
};
#define NSC ((int) (sizeof s_tab / sizeof s_tab[0]))

int main (int argc, char **argv)
{
	if (argc >= 3 && !strcmp (argv[1], "--reader"))
		return reader_main (atol (argv[2]));
	if (argv[0] && strchr (argv[0], '/'))
		s_self = argv[0];
	if (getenv ("WAKEBENCH_SELF"))
		s_self = getenv ("WAKEBENCH_SELF");
	if (getenv ("WAKEBENCH_WD"))
		s_wd = atoi (getenv ("WAKEBENCH_WD"));
	int n = argc > 1 ? atoi (argv[1]) : 20;
	if (n <= 0)
		n = 20;
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_chld;
	sigaction (SIGCHLD, &sa, 0);
	signal (SIGPIPE, SIG_IGN);
	srand ((unsigned) time (0) ^ (unsigned) getpid ());
	pthread_t wd;
	pthread_create (&wd, 0, watchdog, 0);
	struct timespec t0, t1;
	clock_gettime (CLOCK_MONOTONIC, &t0);
	int ran = 0;
	for (int k = 0; k < NSC; k++)
	{
		if (argc > 2)
		{
			int want = 0;
			for (int a = 2; a < argc; a++)
				want |= !strcmp (argv[a], s_tab[k].name);
			if (!want)
				continue;
		}
		s_scen = s_tab[k].name;
		struct timespec a, b;
		clock_gettime (CLOCK_MONOTONIC, &a);
		for (int i = 0; i < n; i++)
		{
			s_iter = i;
			s_tab[k].fn ();
			tick ();
		}
		clock_gettime (CLOCK_MONOTONIC, &b);
		printf ("OK %-9s %d fois, %ld ms\n", s_scen, n,
			(long) ((b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000));
		fflush (stdout);
		ran++;
	}
	clock_gettime (CLOCK_MONOTONIC, &t1);
	printf ("FIN %d scenario(s), %ld ms\n", ran,
		(long) ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000));
	return 0;
}
