/* sigchldtest -- l'hôte Linux (docs/etude/LINUX-HOST.md) : un fils lancé, puis SIGCHLD à sa fin, comme l'attend Bun
 * sans pidfd (l'outil Bash de Claude Code : la commande finit, mais Claude ne le voit pas sans ce signal).
 *   linuxhost run sigchldtest    -> « SIGCHLD reçu », le code du fils, RÉUSSI */
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;
static volatile sig_atomic_t got;
static void on_chld (int s) { (void) s; got++; }

int main (void)
{
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_chld;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction (SIGCHLD, &sa, 0);
	char *argv[] = { "sh", "-c", "exit 3", 0 };
	pid_t pid;
	if (posix_spawn (&pid, "/bin/sh", 0, 0, argv, environ) != 0)
	{
		printf ("sigchldtest : posix_spawn a échoué\n");
		return 1;
	}
	for (int i = 0; i < 50 && !got; i++)
	{
		struct timespec t = { 0, 100000000 };
		nanosleep (&t, 0);
	}
	int st = 0;
	pid_t w = waitpid (pid, &st, WNOHANG);
	printf ("sigchldtest : SIGCHLD %s, waitpid %d, code %d : %s\n", got ? "reçu" : "JAMAIS reçu en 5 s", (int) w,
		WIFEXITED (st) ? WEXITSTATUS (st) : -1, got && w == pid && WEXITSTATUS (st) == 3 ? "RÉUSSI" : "ÉCHEC");
	return got ? 0 : 1;
}
