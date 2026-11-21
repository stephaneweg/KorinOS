/* hello -- jalon 1 de l'hôte Linux (docs/etude/LINUX-HOST.md) : un exécutable de Linux lié à glibc, démarré par
 * « linuxhost run » : argc, argv, l'environnement, getauxval, un constructeur, atexit, le code de sortie.
 * Construit pour Linux (gcc d'Ubuntu) par tools/linux/exectest/run.sh : hello (PIE) et hello-nopie (ET_EXEC). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <fcntl.h>
#include <unistd.h>

static int ctor_ran;
static void __attribute__ ((constructor)) ctor (void) { ctor_ran = 1; }
static void bye (void) { printf ("hello : atexit appelé\n"); }
int data_value = 1234;

int main (int argc, char **argv, char **envp)
{
	int envc = 0;
	while (envp[envc])
		envc++;
	printf ("hello : main à %p, argc %d, argv[0] %s, %d variables, HOME=%s\n", (void *) main, argc, argv[0], envc,
		getenv ("HOME") ? getenv ("HOME") : "(absent)");
	for (int i = 1; i < argc; i++)
		printf ("hello : argv[%d] = %s\n", i, argv[i]);
	printf ("hello : constructeur %s, donnée %d, AT_PAGESZ %lu, AT_RANDOM %s\n", ctor_ran ? "appelé" : "PAS APPELÉ",
		data_value, getauxval (AT_PAGESZ), getauxval (AT_RANDOM) ? "là" : "absent");
	atexit (bye);
	/* le vrai chemin d'un fichier ouvert (Bun s'en sert pour ses realpath, et Claude Code pour ses verrous) */
	char link[256] = "", fdp[64];
	int fd = open ("/etc/services.txt", O_RDONLY);
	snprintf (fdp, sizeof fdp, "/proc/self/fd/%d", fd);
	ssize_t ln = fd >= 0 ? readlink (fdp, link, sizeof link - 1) : -1;
	if (ln >= 0)
		link[ln] = 0;
	printf ("hello : readlink %s = %s\n", fdp, link);
	if (fd >= 0)
		close (fd);
	int ok = ctor_ran && data_value == 1234 && argc >= 1 && getauxval (AT_PAGESZ) == 4096
		&& !strcmp (link, "/etc/services.txt");
	printf ("hello : %s\n", ok ? "RÉUSSI" : "ÉCHEC");
	return ok ? 7 : 1;	/* (7 : le code de sortie doit revenir) */
}
