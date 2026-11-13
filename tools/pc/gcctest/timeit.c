/* timeit : lance une commande et dit sa durée (essais de tools/pc/gcctest, compilé dans Korin) :
 *     timeit commande [arguments...]   ->   [temps] commande : 1.23 s (code 0) */
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>

extern char **environ;

int main (int argc, char **argv)
{
	if (argc < 2) {
		fprintf (stderr, "timeit commande [arguments]\n");
		return 2;
	}
	struct timespec a, b;
	clock_gettime (CLOCK_MONOTONIC, &a);
	pid_t pid;
	if (posix_spawnp (&pid, argv[1], 0, 0, argv + 1, environ)) {
		perror (argv[1]);
		return 127;
	}
	int st = 0;
	waitpid (pid, &st, 0);
	clock_gettime (CLOCK_MONOTONIC, &b);
	double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
	int code = WIFEXITED (st) ? WEXITSTATUS (st) : 128;
	printf ("[temps] %s : %.2f s (code %d)\n", argv[1], s, code);
	fflush (stdout);
	return code;
}
