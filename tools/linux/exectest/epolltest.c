/* epolltest -- l'hôte Linux (docs/etude/LINUX-HOST.md) : epoll sur l'entrée standard quand c'est un tube, comme
 * Bun pour --input-format stream-json (Claude Code dans l'app Claude Code) : non bloquante, EPOLLIN | EPOLLET, lue
 * jusqu'à EAGAIN à chaque réveil, jusqu'à la fin du tube.
 *   (sleep 2; echo un; sleep 2; echo deux) | linuxhost run epolltest     -> « lu : un », « lu : deux », « fin » */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

int main (void)
{
	fcntl (0, F_SETFL, fcntl (0, F_GETFL) | O_NONBLOCK);
	int ep = epoll_create1 (EPOLL_CLOEXEC);
	struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.u64 = 42 };
	if (epoll_ctl (ep, EPOLL_CTL_ADD, 0, &ev) < 0)
	{
		printf ("epolltest : epoll_ctl : %s\n", strerror (errno));
		return 1;
	}
	int wakes = 0, lines = 0;
	for (;;)
	{
		struct epoll_event out[4];
		int n = epoll_wait (ep, out, 4, 10000);
		if (n == 0)
		{
			printf ("epolltest : rien en 10 s (%d réveils) : ÉCHEC\n", wakes);
			return 1;
		}
		wakes++;
		for (;;)
		{
			char buf[256];
			ssize_t r = read (0, buf, sizeof buf - 1);
			if (r > 0)
			{
				buf[r] = 0;
				printf ("epolltest : lu : %s", buf);
				lines++;
				continue;
			}
			if (r == 0)
			{
				printf ("epolltest : fin (%d réveils, %d lectures) : %s\n", wakes, lines, lines >= 2 ? "RÉUSSI" : "ÉCHEC");
				return lines >= 2 ? 0 : 1;
			}
			if (errno == EAGAIN)
				break;
			printf ("epolltest : read : %s\n", strerror (errno));
			return 1;
		}
	}
}
