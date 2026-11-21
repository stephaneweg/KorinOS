/* bashtooltest -- l'hôte Linux (docs/etude/LINUX-HOST.md) : une commande lancée comme l'outil Bash de Claude Code
 * (Bun) : bash -c -l, l'entrée sur /dev/null, la sortie dans un tube (ou une paire de sockets) non bloquant lu par
 * epoll, un tube « | head » dans la commande, puis waitpid. Sur le ZBook, ce bash tournait à vide sans lancer ls.
 *   linuxhost run bashtooltest [pipe|socket] [commande]     -> la sortie (ls par défaut), le code, RÉUSSI */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/wait.h>

extern char **environ;

int main (int argc, char **argv)
{
	int sock = argc > 1 && !strcmp (argv[1], "socket");
	int fd[2];
	if (sock ? socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fd) : pipe2 (fd, O_CLOEXEC))
	{
		printf ("bashtooltest : %s : %s\n", sock ? "socketpair" : "pipe2", strerror (errno));
		return 1;
	}
	fcntl (fd[0], F_SETFL, O_NONBLOCK);
	if (sock)
		fcntl (fd[1], F_SETFL, O_NONBLOCK);
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_addopen (&fa, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2 (&fa, fd[1], 1);
	posix_spawn_file_actions_adddup2 (&fa, fd[1], 2);
	char *cmd = argc > 2 ? argv[2] : "{ shopt -u extglob; } >/dev/null 2>&1 || true && eval 'ls /etc 2>&1 | head -3' < /dev/null && pwd -P >| /tmp/bashtool-cwd";
	char *av[] = { "bash", "-c", "-l", cmd, 0 };
	pid_t pid;
	int e = posix_spawn (&pid, "/bin/bash", &fa, 0, av, environ);
	if (e)
	{
		printf ("bashtooltest : posix_spawn : %s\n", strerror (e));
		return 1;
	}
	close (fd[1]);
	int ep = epoll_create1 (0);
	struct epoll_event ev = { .events = EPOLLIN | EPOLLET };
	epoll_ctl (ep, EPOLL_CTL_ADD, fd[0], &ev);
	int bytes = 0, eof = 0;
	time_t t0 = time (0);
	while (!eof && time (0) - t0 < 20)
	{
		struct epoll_event out;
		if (epoll_wait (ep, &out, 1, 1000) <= 0)
			continue;
		for (;;)
		{
			char b[512];
			ssize_t r = read (fd[0], b, sizeof b);
			if (r > 0) { fwrite (b, 1, r, stdout); bytes += r; continue; }
			if (r == 0) eof = 1;
			break;
		}
	}
	int st = -1;
	pid_t w = 0;
	for (int i = 0; i < 50 && w == 0; i++)
	{
		w = waitpid (pid, &st, WNOHANG);
		if (w == 0) { struct timespec t = { 0, 100000000 }; nanosleep (&t, 0); }
	}
	int ok = eof && bytes > 0 && w == pid && WIFEXITED (st) && WEXITSTATUS (st) == 0;
	printf ("bashtooltest (%s) : %d octets, fin du tube %s, bash %s, code %d en %ld s : %s\n", sock ? "socket" : "pipe", bytes,
		eof ? "oui" : "NON", w == pid ? "fini" : "TOUJOURS VIVANT", w == pid && WIFEXITED (st) ? WEXITSTATUS (st) : -1,
		(long) (time (0) - t0), ok ? "RÉUSSI" : "ÉCHEC");
	return ok ? 0 : 1;
}
