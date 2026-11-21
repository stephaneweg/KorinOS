/* polltest -- est-ce que select() et poll() de libposix attendent vraiment ? (Korin PC, sous QEMU : run.sh)
 * Le process réseau de Jet (curl : son réveil est un tube non bloquant, il attend par select() faute de
 * HAVE_POLL_FINE) tournait à vide, 40 000 appels par seconde, sur une page au repos. Chaque essai attend 2 s
 * sur des descripteurs où rien n'arrive et dit combien de temps il a vraiment attendu et ce qui a été rendu.
 *   polltest [hôte port]   (une connexion TCP au repos vers hôte:port, 10.0.2.2 18765 par défaut) */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void t_poll (const char *what, int *fds, int n)
{
	struct pollfd p[4];
	for (int i = 0; i < n; i++) { p[i].fd = fds[i]; p[i].events = POLLIN; p[i].revents = 0; }
	double t = now ();
	int r = poll (p, (nfds_t) n, 2000);
	printf ("poll   %-28s -> %d en %.3f s, revents", what, r, now () - t);
	for (int i = 0; i < n; i++) printf (" %#x", p[i].revents);
	printf ("\n");
}

static void t_select (const char *what, int *fds, int n)
{
	fd_set r, e;
	FD_ZERO (&r); FD_ZERO (&e);
	int mx = -1;
	for (int i = 0; i < n; i++) { FD_SET (fds[i], &r); FD_SET (fds[i], &e); if (fds[i] > mx) mx = fds[i]; }
	struct timeval tv = { 2, 0 };
	double t = now ();
	int k = select (mx + 1, &r, 0, &e, &tv);
	printf ("select %-28s -> %d en %.3f s, prêts", what, k, now () - t);
	for (int i = 0; i < n; i++) printf (" %d%s", FD_ISSET (fds[i], &r) ? 1 : 0, FD_ISSET (fds[i], &e) ? "e" : "");
	printf ("\n");
}

/* combien de select() en 3 s, comme une boucle d'attente (délai de 1 s chacun) */
static void t_loop (const char *what, int *fds, int n)
{
	int count = 0;
	double t = now ();
	while (now () - t < 3) {
		fd_set r;
		FD_ZERO (&r);
		int mx = -1;
		for (int i = 0; i < n; i++) { FD_SET (fds[i], &r); if (fds[i] > mx) mx = fds[i]; }
		struct timeval tv = { 1, 0 };
		select (mx + 1, &r, 0, 0, &tv);
		count++;
	}
	printf ("boucle %-28s -> %d select() en 3 s (attendu : 3 ou 4)\n", what, count);
}

static void *writer (void *a) { sleep (2); (void) !write (*(int *) a, "w", 1); return 0; }

int main (int argc, char **argv)
{
	setvbuf (stdout, 0, _IONBF, 0);
	const char *host = argc > 2 ? argv[1] : "10.0.2.2";
	int port = argc > 2 ? atoi (argv[2]) : 18765;
	/* sans aucun descripteur : une simple attente (Curl_wait_ms de curl quand il n'a rien à surveiller) */
	double t0 = now ();
	int r0 = poll (0, 0, 1000);
	printf ("poll   %-28s -> %d en %.3f s (attendu : 0 en 1 s)\n", "sans descripteur, 1 s", r0, now () - t0);
	struct timeval tv0 = { 1, 0 };
	t0 = now ();
	r0 = select (0, 0, 0, 0, &tv0);
	printf ("select %-28s -> %d en %.3f s (attendu : 0 en 1 s)\n", "sans descripteur, 1 s", r0, now () - t0);
	int p[2];
	if (pipe2 (p, O_NONBLOCK) != 0) { perror ("pipe2"); return 1; }
	/* un très long délai (curl : INT_MAX ms de WebKit, en timeval pour select) sur un tube vide : doit attendre ;
	 * un thread y écrit au bout de 2 s (alarm() n'interrompt pas select() ici : un autre écart, noté à part) */
	{
		pthread_t th;
		pthread_create (&th, 0, writer, &p[1]);
		fd_set r;
		FD_ZERO (&r);
		FD_SET (p[0], &r);
		struct timeval tv = { 2147483, 647000 };
		t0 = now ();
		r0 = select (p[0] + 1, &r, 0, 0, &tv);
		printf ("select %-28s -> %d en %.3f s (attendu : 1 en 2 s)\n", "tube, INT_MAX ms", r0, now () - t0);
		pthread_join (th, 0);
		char d;
		(void) !read (p[0], &d, 1);
		pthread_create (&th, 0, writer, &p[1]);
		struct pollfd pf = { p[0], POLLIN, 0 };
		t0 = now ();
		r0 = poll (&pf, 1, 2147483647);
		printf ("poll   %-28s -> %d en %.3f s (attendu : 1 en 2 s)\n", "tube, INT_MAX ms", r0, now () - t0);
		pthread_join (th, 0);
		(void) !read (p[0], &d, 1);
	}
	t_poll ("tube vide", p, 1);
	t_select ("tube vide", p, 1);
	char c = 'x';
	(void) !write (p[1], &c, 1);
	t_poll ("tube avec 1 octet", p, 1);
	(void) !read (p[0], &c, 1);
	t_select ("tube vidé", p, 1);

	int s = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons ((unsigned short) port);
	inet_pton (AF_INET, host, &a.sin_addr);
	if (connect (s, (struct sockaddr *) &a, sizeof a) != 0) { printf ("connect %s:%d : %s\n", host, port, strerror (errno)); return 1; }
	fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK);
	int both[2] = { p[0], s };
	t_poll ("TCP au repos", &s, 1);
	t_select ("TCP au repos", &s, 1);
	t_poll ("tube + TCP", both, 2);
	t_select ("tube + TCP", both, 2);
	t_loop ("tube + TCP", both, 2);
	/* une réponse lue en entier, la connexion gardée ouverte (keep-alive) : ce que garde le cache de curl */
	const char *req = "GET /dl HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n";
	(void) !write (s, req, strlen (req));
	usleep (500000);
	char buf[4096];
	ssize_t got = read (s, buf, sizeof buf);
	printf ("réponse lue : %zd octets\n", got);
	t_poll ("TCP après la réponse", &s, 1);
	t_select ("TCP après la réponse", &s, 1);
	t_loop ("tube + TCP après réponse", both, 2);
	close (s);
	return 0;
}
