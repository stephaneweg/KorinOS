/* ringtest -- les tubes en anneaux partagés de libposix et vfs (PC, docs/etude/ANNEAUX.md § 4).
 *   ringtest                 les essais : flux vérifié, deux écrivains (PIPE_BUF d'un seul morceau), fin, EPIPE,
 *                            poll, O_NONBLOCK -> une ligne par essai, puis RÉUSSI ou ÉCHEC
 *   ringtest gen MO          MO mégaoctets du motif sur la sortie (gen 64 | ringtest check 64 : un flux mesuré)
 *   ringtest check MO        lit l'entrée, vérifie le motif, dit le débit */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static unsigned char pat (unsigned long long i)
{
	return (unsigned char) (i * 7 + (i >> 9));
}

static int write_all (int fd, const unsigned char *b, size_t n)
{
	while (n)
	{
		ssize_t k = write (fd, b, n);
		if (k <= 0)
		{
			fprintf (stderr, "ringtest : write : %s\n", k < 0 ? strerror (errno) : "0");
			return -1;
		}
		b += k;
		n -= (size_t) k;
	}
	return 0;
}

/* MO du motif, morceaux de tailles changeantes */
static int gen (int fd, unsigned long long bytes)
{
	static unsigned char b[200000];
	unsigned long long pos = 0;
	unsigned seed = 1;
	while (pos < bytes)
	{
		seed = seed * 1103515245u + 12345u;
		size_t c = 1 + (seed >> 8) % sizeof b;
		if (c > bytes - pos)
			c = (size_t) (bytes - pos);
		for (size_t i = 0; i < c; i++)
			b[i] = pat (pos + i);
		if (write_all (fd, b, c))
			return -1;
		pos += c;
	}
	return 0;
}

/* -> octets lus, -1 : motif faux */
static long long check (int fd)
{
	static unsigned char b[131072];
	unsigned long long pos = 0;
	unsigned seed = 7;
	for (;;)
	{
		seed = seed * 1103515245u + 12345u;
		size_t want = 1 + (seed >> 8) % sizeof b;
		ssize_t k = read (fd, b, want);
		if (k < 0)
		{
			fprintf (stderr, "ringtest : read : %s (à l'octet %llu)\n", strerror (errno), pos);
			return -1;
		}
		if (k == 0)
			return (long long) pos;
		for (ssize_t i = 0; i < k; i++)
			if (b[i] != pat (pos + (unsigned long long) i))
			{
				fprintf (stderr, "ringtest : octet %llu faux\n", pos + (unsigned long long) i);
				return -1;
			}
		pos += (unsigned long long) k;
	}
}

static int fails;
static void verdict (const char *what, int ok)
{
	printf ("ringtest : %-34s %s\n", what, ok ? "bon" : "ÉCHEC");
	if (!ok)
		fails++;
}

static void t_stream (void)
{
	int p[2];
	pipe (p);
	unsigned long long bytes = 64ULL << 20;
	double t0 = now ();
	pid_t c = fork ();
	if (c == 0)
	{
		close (p[1]);
		long long got = check (p[0]);
		_exit (got == (long long) bytes ? 0 : 1);
	}
	close (p[0]);
	int w = gen (p[1], bytes);
	close (p[1]);
	int st = 0;
	waitpid (c, &st, 0);
	double dt = now () - t0;
	char what[80];
	snprintf (what, sizeof what, "flux de 64 Mo (%.1f Mo/s)", 64 / dt);
	verdict (what, w == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 0);
}

/* deux écrivains, des enregistrements de PIPE_BUF octets : jamais mêlés */
static void t_atomic (void)
{
	int p[2];
	pipe (p);
	enum { REC = PIPE_BUF, N = 4000 };
	pid_t c[2];
	for (int k = 0; k < 2; k++)
		if ((c[k] = fork ()) == 0)
		{
			close (p[0]);
			unsigned char r[REC];
			memset (r, 'a' + k, sizeof r);
			for (int i = 0; i < N; i++)
				if (write_all (p[1], r, sizeof r))
					_exit (1);
			_exit (0);
		}
	close (p[1]);
	static unsigned char rec[REC];
	size_t have = 0;
	long recs = 0, bad = 0;
	for (;;)
	{
		ssize_t k = read (p[0], rec + have, REC - have);
		if (k <= 0)
			break;
		have += (size_t) k;
		if (have == REC)
		{
			for (int i = 1; i < REC; i++)
				if (rec[i] != rec[0])
				{
					bad++;
					break;
				}
			recs++;
			have = 0;
		}
	}
	close (p[0]);
	int ok = 1;
	for (int k = 0; k < 2; k++)
	{
		int st = 0;
		waitpid (c[k], &st, 0);
		ok &= WIFEXITED (st) && WEXITSTATUS (st) == 0;
	}
	char what[80];
	snprintf (what, sizeof what, "deux écrivains (%ld enr., %ld mêlés)", recs, bad);
	verdict (what, ok && recs == 2 * N && bad == 0 && have == 0);
}

/* la fin après 1 Mo (anneau déjà là), puis EPIPE quand le lecteur s'en va */
static void t_end (void)
{
	int p[2];
	pipe (p);
	pid_t c = fork ();
	if (c == 0)
	{
		close (p[0]);
		gen (p[1], 1 << 20);
		_exit (0);
	}
	close (p[1]);
	long long got = check (p[0]);
	close (p[0]);
	waitpid (c, 0, 0);
	verdict ("fin (lecture -> 0)", got == 1 << 20);

	signal (SIGPIPE, SIG_IGN);
	pipe (p);
	c = fork ();
	if (c == 0)
	{
		close (p[1]);
		static unsigned char b[65536];
		size_t total = 0;
		while (total < (1 << 20))
		{
			ssize_t k = read (p[0], b, sizeof b);
			if (k <= 0)
				break;
			total += (size_t) k;
		}
		_exit (0);	/* (le lecteur s'en va) */
	}
	close (p[0]);
	static unsigned char b[65536];
	memset (b, 1, sizeof b);
	int e = 0;
	double t0 = now ();
	for (;;)
	{
		if (write (p[1], b, sizeof b) < 0)
		{
			e = errno;
			break;
		}
		if (now () - t0 > 10)
			break;
	}
	close (p[1]);
	waitpid (c, 0, 0);
	verdict ("plus de lecteur (EPIPE)", e == EPIPE);
}

/* poll réveillé vite par une écriture ; O_NONBLOCK sur un tube vide : EAGAIN */
static void t_poll (void)
{
	int p[2];
	pipe (p);
	pid_t c = fork ();
	if (c == 0)
	{
		close (p[0]);
		gen (p[1], 1 << 20);	/* (l'anneau) */
		struct timespec d = {0, 300000000};
		nanosleep (&d, 0);
		write (p[1], "x", 1);
		nanosleep (&d, 0);
		_exit (0);
	}
	close (p[1]);
	static unsigned char b[1 << 20];
	size_t total = 0;
	while (total < (1 << 20))
	{
		ssize_t k = read (p[0], b, (1 << 20) - total);
		if (k <= 0)
			break;
		total += (size_t) k;
	}
	fcntl (p[0], F_SETFL, fcntl (p[0], F_GETFL) | O_NONBLOCK);
	errno = 0;
	ssize_t k = read (p[0], b, 10);
	verdict ("O_NONBLOCK, vide : EAGAIN", k < 0 && errno == EAGAIN);
	struct pollfd f = {p[0], POLLIN, 0};
	double t0 = now ();
	int r = poll (&f, 1, 3000);
	double dt = now () - t0;
	char what[80];
	snprintf (what, sizeof what, "poll (%.0f ms, attendu ~300)", dt * 1000);
	verdict (what, r == 1 && (f.revents & POLLIN) && dt > 0.2 && dt < 0.5);
	read (p[0], b, 10);
	close (p[0]);
	waitpid (c, 0, 0);
}

int main (int argc, char **argv)
{
	if (argc == 3 && !strcmp (argv[1], "gen"))
		return gen (1, (unsigned long long) atoi (argv[2]) << 20) ? 1 : 0;
	if (argc == 3 && !strcmp (argv[1], "check"))
	{
		double t0 = now ();
		long long got = check (0);
		double dt = now () - t0;
		unsigned long long want = (unsigned long long) atoi (argv[2]) << 20;
		printf ("ringtest check : %lld octets, %.1f Mo/s : %s\n", got, got / dt / 1048576.0,
			got == (long long) want ? "RÉUSSI" : "ÉCHEC");
		return got == (long long) want ? 0 : 1;
	}
	t_stream ();
	t_atomic ();
	t_end ();
	t_poll ();
	printf ("ringtest : %s\n", fails ? "ÉCHEC" : "RÉUSSI");
	return fails ? 1 : 0;
}
