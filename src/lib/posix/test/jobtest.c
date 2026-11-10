/*
 * jobtest -- essais du controle des taches de Korin OS (src/kernel/jobs.asm, service vterm,
 * libposix proc.c, signal.c, tty.c ; docs/POSIX.md, docs/VTERM.md).
 *
 *   jobtest   sans terminal : le process devient meneur de session (setsid), ouvre un pty a lui
 *             (posix_openpt, TIOCSCTTY) et joue le terminal. Ctrl+Z (VSUSP) -> SIGTSTP au groupe
 *             de premier plan, waitpid WUNTRACED (WIFSTOPPED, WSTOPSIG), SIGCONT et WCONTINUED,
 *             SIGCHLD a l'arret, SA_NOCLDSTOP, SIGKILL d'un fils arrete, lecture en arriere-plan
 *             (SIGTTIN), ecriture avec TOSTOP (SIGTTOU), SIGTTOU ignore, tcsetpgrp en
 *             arriere-plan (EIO dans un groupe orphelin, permis avec SIGTTOU masque), fils a
 *             plusieurs threads arrete, killpg, raise (SIGTSTP), arret jete dans un groupe
 *             orphelin, SIGHUP + SIGCONT a un groupe devenu orphelin avec un membre arrete.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <termios.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

static int s_pass, s_fail;
static volatile int s_chld, s_hup;

static void check (int ok, const char *what)
{
	printf ("%s : %s\n", ok ? "PASS" : "FAIL", what);
	fflush (stdout);
	if (ok)
		s_pass++;
	else
		s_fail++;
}

static void on_chld (int s) { (void) s; s_chld++; }
static void on_hup (int s) { (void) s; s_hup++; }

static long ms_now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* waitpid avec un delai (WNOHANG toutes les 10 ms) : -> pid, 0 au bout du delai, -1 */
static pid_t wait_ms (pid_t pid, int *st, int opts, int ms)
{
	long end = ms_now () + ms;
	for (;;)
	{
		pid_t r = waitpid (pid, st, opts | WNOHANG);
		if (r != 0 || ms_now () >= end)
			return r;
		usleep (10000);
	}
}

static int s_m, s_s;				/* le pty : maitre, esclave */

/* tout ce que le maitre a a lire */
static int drain (char *buf, int cap)
{
	int n = 0;
	usleep (50000);
	int fl = fcntl (s_m, F_GETFL);
	fcntl (s_m, F_SETFL, fl | O_NONBLOCK);
	for (;;)
	{
		int r = (int) read (s_m, buf + n, (size_t) (cap - 1 - n));
		if (r <= 0)
			break;
		n += r;
	}
	fcntl (s_m, F_SETFL, fl);
	buf[n] = 0;
	return n;
}

static void fg_self (void)			/* (SIGTTOU masque : comme un shell) */
{
	sigset_t b, old;
	sigemptyset (&b);
	sigaddset (&b, SIGTTOU);
	sigprocmask (SIG_BLOCK, &b, &old);
	tcsetpgrp (s_s, getpgrp ());
	sigprocmask (SIG_SETMASK, &old, 0);
}

/* un fils dans son groupe, qui attend ; -> pid */
static pid_t child_idle (void)
{
	pid_t c = fork ();
	if (c == 0)
	{
		setpgid (0, 0);
		for (;;)
			usleep (10000);
	}
	setpgid (c, c);
	return c;
}

static void *ticker (void *p)
{
	int fd = (int) (long) p >> 8;
	char ch = (char) ((long) p & 0xFF);
	for (;;)
	{
		write (fd, &ch, 1);
		usleep (10000);
	}
	return 0;
}

int main (void)
{
	char buf[512];
	int st;
	signal (SIGHUP, SIG_IGN);

	/* --- session, terminal de controle --- */
	pid_t me = getpid ();
	check (setsid () == me && getsid (0) == me && getpgrp () == me, "setsid : session et groupe = pid");
	errno = 0;
	check (setsid () == -1 && errno == EPERM, "setsid d'un meneur de groupe : EPERM");
	s_m = posix_openpt (O_RDWR | O_NOCTTY);
	s_s = s_m >= 0 ? open (ptsname (s_m), O_RDWR) : -1;
	check (s_m >= 0 && s_s >= 0, "posix_openpt, esclave ouvert");
	if (s_s < 0)
		return 1;
	check (ioctl (s_s, TIOCSCTTY, 0) == 0, "TIOCSCTTY : le pty devient le terminal de la session");
	check (tcgetsid (s_s) == me && tcgetpgrp (s_s) == me, "tcgetsid, tcgetpgrp : la session, son groupe");

	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_chld;
	sigaction (SIGCHLD, &sa, 0);

	/* --- Ctrl+Z au premier plan --- */
	pid_t c = child_idle ();
	check (getsid (c) == me && getpgid (c) == c, "fils : meme session, son groupe");
	check (tcsetpgrp (s_s, c) == 0 && tcgetpgrp (s_s) == c, "tcsetpgrp : le fils au premier plan");
	int n0 = s_chld;
	write (s_m, "\x1a", 1);					/* Ctrl+Z (VSUSP) */
	pid_t r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st) && WSTOPSIG (st) == SIGTSTP, "Ctrl+Z : WIFSTOPPED, WSTOPSIG = SIGTSTP");
	usleep (50000);
	check (s_chld > n0, "SIGCHLD a l'arret du fils");
	check (waitpid (c, &st, WNOHANG | WUNTRACED) == 0, "arret vu une fois (WNOHANG : 0 ensuite)");

	/* le pere en arriere-plan : tcsetpgrp -> EIO (groupe orphelin), permis avec SIGTTOU masque */
	errno = 0;
	check (tcsetpgrp (s_s, me) == -1 && errno == EIO, "tcsetpgrp en arriere-plan, groupe orphelin : EIO");
	fg_self ();
	check (tcgetpgrp (s_s) == me, "tcsetpgrp en arriere-plan, SIGTTOU masque : fait");

	/* SIGCONT : WIFCONTINUED */
	n0 = s_chld;
	kill (c, SIGCONT);
	r = wait_ms (c, &st, WCONTINUED, 3000);
	check (r == c && WIFCONTINUED (st), "SIGCONT : WIFCONTINUED");
	usleep (50000);
	check (s_chld > n0, "SIGCHLD a la reprise du fils");
	check (waitpid (c, &st, WNOHANG | WUNTRACED | WCONTINUED) == 0, "rien de plus (WNOHANG)");

	/* SIGKILL d'un fils arrete */
	kill (c, SIGSTOP);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st) && WSTOPSIG (st) == SIGSTOP, "SIGSTOP : WSTOPSIG = SIGSTOP");
	kill (c, SIGKILL);
	r = wait_ms (c, &st, 0, 3000);
	check (r == c && WIFSIGNALED (st) && WTERMSIG (st) == SIGKILL, "SIGKILL d'un fils arrete : WIFSIGNALED, SIGKILL");

	/* --- lecture en arriere-plan : SIGTTIN ; au premier plan et repris, la lecture se fait --- */
	c = fork ();
	if (c == 0)
	{
		setpgid (0, 0);
		int k = (int) read (s_s, buf, sizeof buf);
		_exit (k > 0 && buf[0] == 'x' ? 42 : 1);
	}
	setpgid (c, c);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st) && WSTOPSIG (st) == SIGTTIN, "lecture en arriere-plan : SIGTTIN, arrete");
	tcsetpgrp (s_s, c);
	write (s_m, "x\n", 2);
	kill (c, SIGCONT);
	r = wait_ms (c, &st, 0, 3000);
	check (r == c && WIFEXITED (st) && WEXITSTATUS (st) == 42, "au premier plan, SIGCONT : la lecture se fait");
	fg_self ();
	drain (buf, sizeof buf);

	/* --- TOSTOP : SIGTTOU a l'ecriture en arriere-plan --- */
	struct termios t, t0;
	tcgetattr (s_s, &t0);
	t = t0;
	t.c_lflag |= TOSTOP;
	check (tcsetattr (s_s, TCSANOW, &t) == 0, "tcsetattr TOSTOP (au premier plan)");
	c = fork ();
	if (c == 0)
	{
		setpgid (0, 0);
		_exit (write (s_s, "bg\n", 3) == 3 ? 0 : 1);
	}
	setpgid (c, c);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st) && WSTOPSIG (st) == SIGTTOU, "ecriture en arriere-plan, TOSTOP : SIGTTOU");
	tcsetpgrp (s_s, c);
	kill (c, SIGCONT);
	r = wait_ms (c, &st, 0, 3000);
	check (r == c && WIFEXITED (st) && WEXITSTATUS (st) == 0, "au premier plan, SIGCONT : l'ecriture se fait");
	fg_self ();
	drain (buf, sizeof buf);
	check (strstr (buf, "bg") != 0, "le maitre lit l'ecriture");
	c = fork ();
	if (c == 0)
	{
		setpgid (0, 0);
		signal (SIGTTOU, SIG_IGN);
		_exit (write (s_s, "ign\n", 4) == 4 ? 0 : 1);
	}
	setpgid (c, c);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFEXITED (st) && WEXITSTATUS (st) == 0, "TOSTOP, SIGTTOU ignore : l'ecriture se fait");
	c = fork ();
	if (c == 0)
	{
		setpgid (0, 0);
		struct termios u;
		tcgetattr (s_s, &u);
		_exit (tcsetattr (s_s, TCSANOW, &u) == 0 ? 0 : 1);
	}
	setpgid (c, c);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st) && WSTOPSIG (st) == SIGTTOU, "tcsetattr en arriere-plan : SIGTTOU");
	kill (c, SIGKILL);
	wait_ms (c, &st, 0, 3000);
	tcsetattr (s_s, TCSANOW, &t0);
	drain (buf, sizeof buf);

	/* --- SA_NOCLDSTOP --- */
	sa.sa_flags = SA_NOCLDSTOP;
	sigaction (SIGCHLD, &sa, 0);
	c = child_idle ();
	n0 = s_chld;
	kill (c, SIGSTOP);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	usleep (100000);
	check (r == c && WIFSTOPPED (st) && s_chld == n0, "SA_NOCLDSTOP : arret vu par waitpid, sans SIGCHLD");
	kill (c, SIGKILL);
	wait_ms (c, &st, 0, 3000);
	usleep (100000);
	check (s_chld > n0, "SA_NOCLDSTOP : SIGCHLD a la fin");
	sa.sa_flags = 0;
	sigaction (SIGCHLD, &sa, 0);

	/* --- killpg : SIGSTOP, SIGCONT au groupe --- */
	c = child_idle ();
	check (killpg (c, SIGSTOP) == 0, "killpg (SIGSTOP)");
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st), "killpg : le groupe arrete");
	check (kill (-c, SIGCONT) == 0, "kill (-groupe, SIGCONT)");
	r = wait_ms (c, &st, WCONTINUED, 3000);
	check (r == c && WIFCONTINUED (st), "kill (-groupe, SIGCONT) : WIFCONTINUED");
	kill (c, SIGKILL);
	wait_ms (c, &st, 0, 3000);

	/* --- fils a plusieurs threads : tous arretes, tous repris --- */
	int p[2];
	pipe (p);
	c = fork ();
	if (c == 0)
	{
		setpgid (0, 0);
		close (p[0]);
		pthread_t a, b;
		pthread_create (&a, 0, ticker, (void *) (long) ((p[1] << 8) | 'a'));
		pthread_create (&b, 0, ticker, (void *) (long) ((p[1] << 8) | 'b'));
		ticker ((void *) (long) ((p[1] << 8) | 'm'));
		_exit (0);
	}
	setpgid (c, c);
	close (p[1]);
	fcntl (p[0], F_SETFL, O_NONBLOCK);
	usleep (200000);
	kill (c, SIGTSTP);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st) && WSTOPSIG (st) == SIGTSTP, "fils a 3 threads : SIGTSTP, arrete");
	usleep (100000);
	while (read (p[0], buf, sizeof buf) > 0)
		;
	usleep (300000);
	int k = (int) read (p[0], buf, sizeof buf);
	check (k < 0 && errno == EAGAIN, "fils a 3 threads : plus rien n'avance");
	kill (c, SIGCONT);
	usleep (300000);
	k = (int) read (p[0], buf, sizeof buf - 1);
	buf[k > 0 ? k : 0] = 0;
	check (k > 0 && strchr (buf, 'a') && strchr (buf, 'b') && strchr (buf, 'm'), "fils a 3 threads : les trois repartent");
	kill (c, SIGKILL);
	r = wait_ms (c, &st, 0, 3000);
	check (r == c && WIFSIGNALED (st), "fils a 3 threads : SIGKILL");
	close (p[0]);

	/* --- raise (SIGTSTP) dans son groupe ; jete dans un groupe orphelin --- */
	c = fork ();
	if (c == 0)
	{
		setpgid (0, 0);
		raise (SIGTSTP);
		_exit (7);
	}
	setpgid (c, c);
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFSTOPPED (st) && WSTOPSIG (st) == SIGTSTP, "raise (SIGTSTP) : arrete");
	kill (c, SIGCONT);
	r = wait_ms (c, &st, 0, 3000);
	check (r == c && WIFEXITED (st) && WEXITSTATUS (st) == 7, "repris : la suite du programme");
	c = fork ();
	if (c == 0)
	{
		raise (SIGTSTP);			/* (groupe du pere : orphelin) */
		_exit (8);
	}
	r = wait_ms (c, &st, WUNTRACED, 3000);
	check (r == c && WIFEXITED (st) && WEXITSTATUS (st) == 8, "groupe orphelin : SIGTSTP jete");

	/* --- groupe devenu orphelin avec un membre arrete : SIGHUP puis SIGCONT --- */
	pipe (p);
	c = fork ();
	if (c == 0)
	{
		pid_t g = fork ();
		if (g == 0)
		{
			setpgid (0, 0);
			close (p[0]);
			struct sigaction h;
			memset (&h, 0, sizeof h);
			h.sa_handler = on_hup;		/* (un gestionnaire : ne pas mourir) */
			sigaction (SIGHUP, &h, 0);
			raise (SIGSTOP);
			write (p[1], s_hup ? "H" : "-", 1);
			_exit (0);
		}
		setpgid (g, g);
		int s2;
		waitpid (g, &s2, WUNTRACED);
		_exit (WIFSTOPPED (s2) ? 0 : 1);
	}
	close (p[1]);
	fcntl (p[0], F_SETFL, O_NONBLOCK);
	r = wait_ms (c, &st, 0, 3000);
	check (r == c && WIFEXITED (st) && WEXITSTATUS (st) == 0, "intermediaire : petit-fils arrete, puis fin");
	long end = ms_now () + 3000;
	k = 0;
	while (ms_now () < end && (k = (int) read (p[0], buf, 1)) <= 0)
		usleep (20000);
	check (k == 1 && buf[0] == 'H', "groupe orphelin et arrete : SIGHUP, SIGCONT");
	close (p[0]);

	printf ("jobtest : %d PASS, %d FAIL\n", s_pass, s_fail);
	fflush (stdout);
	close (s_s);
	close (s_m);
	return s_fail ? 1 : 0;
}
