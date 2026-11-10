/*
 * ttytest -- essais des terminaux de Korin OS (service vterm, docs/VTERM.md ; libposix tty.c).
 *
 *   ttytest          essais automatiques : isatty (pty et tube), termios, groupes, puis un pty
 *                    a soi (posix_openpt) : mode canonique et edition de ligne, echo, mode brut
 *                    octet par octet, VMIN / VTIME, ONLCR, SIGINT au groupe de premier plan,
 *                    EINTR, TIOCSWINSZ et SIGWINCH, tcsetpgrp, poll, FIONREAD, VEOF
 *   ttytest size     la taille du terminal, puis chaque SIGWINCH (20 s ; redimensionner la fenetre)
 *   ttytest raw      mode brut : chaque touche en hexadecimal, q pour finir
 *   ttytest sigint   attend Ctrl+C (SIGINT) dans une lecture (30 s)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <termios.h>
#include <time.h>
#include <sys/ioctl.h>

static int s_pass, s_fail;
static volatile int s_sigint, s_sigwinch;

static void check (int ok, const char *what)
{
	printf ("%s : %s\n", ok ? "PASS" : "FAIL", what);
	if (ok)
		s_pass++;
	else
		s_fail++;
}

static void on_sigint (int s) { (void) s; s_sigint++; }
static void on_sigwinch (int s) { (void) s; s_sigwinch++; }

static long ms_now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* tout ce que le maitre a a lire (sans attendre, apres un court delai) */
static int drain (int fd, char *buf, int cap)
{
	int n = 0;
	usleep (50000);
	int fl = fcntl (fd, F_GETFL);
	fcntl (fd, F_SETFL, fl | O_NONBLOCK);
	for (;;)
	{
		int r = (int) read (fd, buf + n, (size_t) (cap - 1 - n));
		if (r <= 0)
			break;
		n += r;
	}
	fcntl (fd, F_SETFL, fl);
	buf[n] = 0;
	return n;
}

static void put (int fd, const char *s) { write (fd, s, strlen (s)); }

struct later { int fd; const char *s; int ms; };
static void *write_later (void *p)
{
	struct later *l = (struct later *) p;
	usleep ((useconds_t) l->ms * 1000);
	put (l->fd, l->s);
	return 0;
}

static int auto_tests (void)
{
	char buf[512];
	setpgid (0, 0);						/* un groupe a soi */
	check (getpgrp () == getpid (), "setpgid (0, 0) : getpgrp () == getpid ()");

	/* isatty : vrai sur le pty, faux sur un tube */
	int stdtty = isatty (0);
	printf ("info : isatty (0) = %d, isatty (1) = %d, TERM=%s\n", stdtty, isatty (1),
		getenv ("TERM") ? getenv ("TERM") : "(rien)");
	int p[2];
	if (pipe (p) == 0)
	{
		check (!isatty (p[0]) && !isatty (p[1]) && errno == ENOTTY, "isatty faux sur un tube (ENOTTY)");
		close (p[0]);
		close (p[1]);
	}
	if (stdtty)
	{
		struct winsize w;
		check (ioctl (0, TIOCGWINSZ, &w) == 0 && w.ws_row > 0 && w.ws_col > 0, "TIOCGWINSZ sur l'entree standard");
		printf ("info : %d lignes, %d colonnes ; ttyname (0) = %s\n", w.ws_row, w.ws_col, ttyname (0));
		check (getenv ("TERM") && strstr (getenv ("TERM"), "xterm"), "TERM d'un xterm sur un pty");
	}

	/* un pty a soi */
	int m = posix_openpt (O_RDWR | O_NOCTTY);
	check (m >= 0, "posix_openpt");
	if (m < 0)
		return 1;
	check (grantpt (m) == 0 && unlockpt (m) == 0, "grantpt, unlockpt");
	char *name = ptsname (m);
	check (name && !strncmp (name, "/dev/pts/", 9), "ptsname");
	int s = open (name ? name : "/dev/pts/0", O_RDWR);
	check (s >= 0 && isatty (s) && isatty (m), "open de l'esclave, isatty des deux bouts");
	if (s < 0)
		return 1;
	struct termios t, t0;
	check (tcgetattr (s, &t0) == 0 && (t0.c_lflag & ICANON) && (t0.c_lflag & ECHO) &&
	       (t0.c_lflag & ISIG) && (t0.c_oflag & ONLCR) && (t0.c_iflag & ICRNL), "termios par defaut (ICANON ECHO ISIG ONLCR ICRNL)");

	/* canonique : edition de ligne, echo */
	put (m, "ab\x7f" "c\r");				/* a b, effacer, c, Entree (CR -> LF) */
	int n = (int) read (s, buf, sizeof buf - 1);
	buf[n < 0 ? 0 : n] = 0;
	check (n == 3 && !memcmp (buf, "ac\n", 3), "canonique : retour arriere, CR -> LF (ICRNL)");
	n = drain (m, buf, sizeof buf);
	check (strstr (buf, "ab\b \bc\r\n") != 0, "echo, ECHOE (BS espace BS), LF -> CR LF a l'echo");
	put (m, "un deux\x17trois\n");				/* ^W efface « deux » */
	n = (int) read (s, buf, sizeof buf - 1);
	buf[n < 0 ? 0 : n] = 0;
	check (!strcmp (buf, "un trois\n"), "VWERASE (^W)");
	put (m, "perdu\x15garde\n");				/* ^U efface la ligne */
	n = (int) read (s, buf, sizeof buf - 1);
	buf[n < 0 ? 0 : n] = 0;
	check (!strcmp (buf, "garde\n"), "VKILL (^U)");
	drain (m, buf, sizeof buf);

	/* echo coupe */
	t = t0;
	t.c_lflag &= ~(tcflag_t) ECHO;
	tcsetattr (s, TCSANOW, &t);
	put (m, "secret\n");
	n = (int) read (s, buf, sizeof buf - 1);
	int e = drain (m, buf + 100, 300);
	check (n == 7 && e == 0, "ECHO coupe : rien a l'echo");
	tcsetattr (s, TCSANOW, &t0);

	/* VEOF sur une ligne vide : fin */
	put (m, "\x04");
	n = (int) read (s, buf, sizeof buf);
	check (n == 0, "VEOF (^D) sur une ligne vide : read rend 0");
	put (m, "xy\x04");
	n = (int) read (s, buf, sizeof buf);
	check (n == 2 && !memcmp (buf, "xy", 2), "VEOF apres du texte : la ligne sans fin de ligne");
	drain (m, buf, sizeof buf);

	/* sortie : ONLCR */
	put (s, "a\nb");
	n = drain (m, buf, sizeof buf);
	check (n == 4 && !memcmp (buf, "a\r\nb", 4), "OPOST ONLCR : LF -> CR LF");

	/* mode brut : octet par octet */
	t = t0;
	cfmakeraw (&t);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	check (tcsetattr (s, TCSAFLUSH, &t) == 0, "tcsetattr : mode brut (cfmakeraw)");
	put (m, "q\x03\r");
	char c[4];
	int k1 = (int) read (s, c, 1);
	int k2 = (int) read (s, c + 1, 1);
	int k3 = (int) read (s, c + 2, 1);
	check (k1 == 1 && k2 == 1 && k3 == 1 && c[0] == 'q' && c[1] == 3 && c[2] == '\r',
	       "brut : chaque octet tel quel (q, ^C sans signal, CR sans ICRNL)");
	check (drain (m, buf, sizeof buf) == 0, "brut : pas d'echo");
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 3;
	tcsetattr (s, TCSANOW, &t);
	long t1 = ms_now ();
	n = (int) read (s, c, 1);
	long dt = ms_now () - t1;
	check (n == 0 && dt >= 200 && dt < 2000, "VMIN 0, VTIME 3 : 0 au bout de 0,3 s");
	printf ("info : VTIME 3 -> %ld ms\n", dt);
	put (m, "xyz");
	int fr = -1;
	usleep (50000);
	check (ioctl (s, FIONREAD, &fr) == 0 && fr == 3, "FIONREAD");
	struct pollfd pf = { s, POLLIN, 0 };
	check (poll (&pf, 1, 100) == 1 && (pf.revents & POLLIN), "poll : POLLIN");
	read (s, buf, sizeof buf);
	pf.revents = 0;
	check (poll (&pf, 1, 100) == 0, "poll : rien quand c'est vide");
	tcsetattr (s, TCSANOW, &t0);

	/* groupe de premier plan, SIGINT par la discipline */
	check (tcsetpgrp (s, getpgrp ()) == 0 && tcgetpgrp (s) == getpgrp (), "tcsetpgrp, tcgetpgrp");
	pid_t g = 0;
	check (ioctl (m, TIOCGPGRP, &g) == 0 && g == getpgrp (), "TIOCGPGRP sur le maitre");
	signal (SIGINT, on_sigint);
	put (m, "abc\x03");
	usleep (100000);
	check (s_sigint == 1, "Ctrl+C (VINTR) : SIGINT au groupe de premier plan");
	n = drain (m, buf, sizeof buf);
	check (strstr (buf, "^C") != 0, "echo de ^C (ECHOCTL)");
	put (m, "\n");
	n = (int) read (s, buf, sizeof buf);
	check (n == 1, "l'entree videe par le signal (abc perdu)");

	/* EINTR : une lecture qui attend, interrompue par Ctrl+C */
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_sigint;
	sigaction (SIGINT, &sa, 0);				/* (sans SA_RESTART) */
	pthread_t th;
	struct later l = { m, "\x03", 300 };
	pthread_create (&th, 0, write_later, &l);
	errno = 0;
	n = (int) read (s, buf, sizeof buf);
	check (n == -1 && errno == EINTR && s_sigint == 2, "read en attente : EINTR apres SIGINT");
	pthread_join (th, 0);
	drain (m, buf, sizeof buf);

	/* taille : TIOCSWINSZ -> SIGWINCH */
	signal (SIGWINCH, on_sigwinch);
	struct winsize w = { 33, 111, 0, 0 }, w2;
	check (ioctl (m, TIOCSWINSZ, &w) == 0, "TIOCSWINSZ sur le maitre");
	usleep (100000);
	check (s_sigwinch == 1, "SIGWINCH au groupe de premier plan");
	check (ioctl (s, TIOCGWINSZ, &w2) == 0 && w2.ws_row == 33 && w2.ws_col == 111, "TIOCGWINSZ : la nouvelle taille");

	/* le maitre ferme : l'esclave lit la fin */
	signal (SIGHUP, SIG_IGN);
	close (m);
	n = (int) read (s, buf, sizeof buf);
	check (n == 0, "maitre ferme : l'esclave lit la fin");
	close (s);
	printf ("ttytest : %d PASS, %d FAIL\n", s_pass, s_fail);
	return s_fail ? 1 : 0;
}

static int size_mode (void)
{
	struct winsize w;
	if (ioctl (0, TIOCGWINSZ, &w) != 0)
	{
		perror ("TIOCGWINSZ");
		return 1;
	}
	printf ("taille : %d lignes, %d colonnes\n", w.ws_row, w.ws_col);
	fflush (stdout);
	signal (SIGWINCH, on_sigwinch);
	int seen = 0;
	long end = ms_now () + 20000;
	while (ms_now () < end)
	{
		usleep (50000);
		if (s_sigwinch != seen)
		{
			seen = s_sigwinch;
			ioctl (0, TIOCGWINSZ, &w);
			printf ("SIGWINCH : %d lignes, %d colonnes\n", w.ws_row, w.ws_col);
			fflush (stdout);
		}
	}
	return 0;
}

static int raw_mode (void)
{
	struct termios t0, t;
	if (tcgetattr (0, &t0) != 0)
	{
		perror ("tcgetattr");
		return 1;
	}
	t = t0;
	cfmakeraw (&t);
	t.c_oflag |= OPOST;
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr (0, TCSAFLUSH, &t);
	printf ("mode brut : tapez des touches, q pour finir\n");
	fflush (stdout);
	for (;;)
	{
		unsigned char c;
		if (read (0, &c, 1) != 1)
			break;
		printf ("touche : 0x%02X%s%c\n", c, c >= 32 && c < 127 ? " " : "", c >= 32 && c < 127 ? c : ' ');
		fflush (stdout);
		if (c == 'q')
			break;
	}
	tcsetattr (0, TCSANOW, &t0);
	return 0;
}

static int sigint_mode (void)
{
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_sigint;
	sigaction (SIGINT, &sa, 0);
	printf ("Ctrl+C attendu (groupe %d, premier plan %d)\n", (int) getpgrp (), (int) tcgetpgrp (0));
	fflush (stdout);
	char buf[64];
	errno = 0;
	int n = (int) read (0, buf, sizeof buf);
	printf ("read -> %d, %s ; SIGINT recus : %d\n", n, n < 0 ? strerror (errno) : "donnees", s_sigint);
	return s_sigint ? 0 : 1;
}

int main (int argc, char **argv)
{
	if (argc > 1 && !strcmp (argv[1], "size"))
		return size_mode ();
	if (argc > 1 && !strcmp (argv[1], "raw"))
		return raw_mode ();
	if (argc > 1 && !strcmp (argv[1], "sigint"))
		return sigint_mode ();
	return auto_tests ();
}
