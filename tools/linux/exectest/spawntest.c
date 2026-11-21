/* spawntest -- jalon 3 de l'hôte Linux : un programme de Linux lance un programme de Korin (posix_spawnp, comme Bun
 * pour xdg-open, bash, git), avec ses arguments et son environnement, et attend sa fin ; puis fork et execve.
 *   spawntest [PROGRAMME ARG…]   (par défaut : /bin/echo bonjour depuis Linux) */
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int main (int argc, char **argv)
{
	char *def[] = { "echo", "bonjour", "depuis", "Linux", 0 };
	char **args = argc > 1 ? argv + 1 : def;
	setenv ("SPAWNTEST", "oui", 1);
	pid_t pid;
	int r = posix_spawnp (&pid, args[0], 0, 0, args, environ);
	if (r)
	{
		printf ("spawntest : posix_spawnp : %s\n", strerror (r));
		return 1;
	}
	int st = 0;
	waitpid (pid, &st, 0);
	printf ("spawntest : posix_spawnp -> pid %d, fin %s %d\n", pid, WIFEXITED (st) ? "code" : "signal",
		WIFEXITED (st) ? WEXITSTATUS (st) : WTERMSIG (st));
	pid = fork ();
	if (pid == 0)
	{
		execlp ("echo", "echo", "fils", "de", "fork", (char *) 0);
		_exit (127);
	}
	waitpid (pid, &st, 0);
	printf ("spawntest : fork + execlp -> fin code %d\n", WEXITSTATUS (st));
	printf ("spawntest : %s\n", WIFEXITED (st) && WEXITSTATUS (st) == 0 ? "RÉUSSI" : "ÉCHEC");
	return 0;
}
