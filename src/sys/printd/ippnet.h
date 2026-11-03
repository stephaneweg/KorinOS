//
// ippnet.h (printerkit/ippnet.h d'Onyx) -- les sockets d'ipp.h : TCP par la couche POSIX (netd). Un envoi continue tant
// que tout n'est pas parti (une imprimante qui imprime en recevant met du temps a prendre la suite), et renonce apres une
// minute sans un octet pris.
//
#ifndef PRINTD_IPPNET_H
#define PRINTD_IPPNET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include "ipp.h"

namespace ipp {

static bool (*net_stop) (void);		// vrai : renoncer (le travail est annule)

static int k_connect (const char *host, int port)
{
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET; a.sin_port = htons ((unsigned short) port);
	if (!inet_aton (host, &a.sin_addr))
	{
		struct hostent *h = gethostbyname (host);
		if (!h || !h->h_addr_list[0]) return -1;
		memcpy (&a.sin_addr, h->h_addr_list[0], 4);
	}
	int s = socket (AF_INET, SOCK_STREAM, 0);
	if (s < 0) return -1;
	if (connect (s, (struct sockaddr *) &a, sizeof a) < 0) { close (s); return -1; }
	return s;
}
static bool k_send (int s, const void *b, unsigned n)
{
	const char *p = (const char *) b; unsigned stalled = 0;
	while (n)
	{
		if (net_stop && net_stop ()) return false;
		struct pollfd f = { s, POLLOUT, 0 };
		int r = poll (&f, 1, 5000);
		if (r < 0) return false;
		if (r == 0) { if (++stalled > 12) return false; continue; }
		long k = send (s, p, n > 32768 ? 32768 : n, 0);
		if (k < 0) return false;
		stalled = k ? 0 : stalled + 1;
		p += k; n -= (unsigned) k;
	}
	return true;
}
static int k_recv (int s, void *b, unsigned n, unsigned wait_ms)
{
	struct pollfd f = { s, POLLIN, 0 };
	int r = poll (&f, 1, (int) wait_ms);
	if (r < 0) return -1;
	if (r == 0) return 0;
	long k = recv (s, b, n, 0);
	return k > 0 ? (int) k : -1;
}
static void k_close (int s) { close (s); }
static const Net KAPI_NET = { k_connect, k_send, k_recv, k_close };

} // namespace ipp

#endif
