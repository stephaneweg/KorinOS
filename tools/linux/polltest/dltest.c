/* dltest -- un téléchargement HTTP par des sockets POSIX bloquantes (recv attend dans netd) : octets et débit.
 * Pour comparer deux netd sous QEMU PC (tools/linux/polltest/run.sh) : le serveur de l'hôte doit servir GET /big.
 *   dltest [hôte port chemin]   (10.0.2.2 18765 /big par défaut) */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main (int argc, char **argv)
{
	const char *host = argc > 3 ? argv[1] : "10.0.2.2", *path = argc > 3 ? argv[3] : "/big";
	int port = argc > 3 ? atoi (argv[2]) : 18765;
	for (int round = 0; round < 3; round++)
	{
		int s = socket (AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in a;
		memset (&a, 0, sizeof a);
		a.sin_family = AF_INET;
		a.sin_port = htons ((unsigned short) port);
		inet_pton (AF_INET, host, &a.sin_addr);
		double t0 = now ();
		if (connect (s, (struct sockaddr *) &a, sizeof a) != 0) { perror ("connect"); return 1; }
		char req[256];
		int n = snprintf (req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
		if (write (s, req, (size_t) n) != n) { perror ("write"); return 1; }
		static char buf[65536];
		long total = 0;
		for (;;)
		{
			ssize_t k = recv (s, buf, sizeof buf, 0);
			if (k <= 0) break;
			total += k;
		}
		double dt = now () - t0;
		close (s);
		printf ("dltest %d : %ld octets en %.2f s, %.1f Mo/s\n", round + 1, total, dt, total / dt / 1e6);
	}
	return 0;
}
