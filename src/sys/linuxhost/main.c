/*
 * linuxhost -- charge un .so de Linux x86-64 dans un process de Korin et appelle une de ses
 * fonctions (liblinux : src/lib/linux ; docs/etude/WIDEVINE.md, jalon 1).
 *
 *   linuxhost --check FICHIER.so [-v]   ce que le module demande : imports servis ou absents,
 *                                       TLS, syscall directs (sans rien exécuter)
 *   linuxhost FICHIER.so FONCTION [ARG…] le charge, appelle int FONCTION (int argc, char **argv)
 *                                        (argv[0] = FONCTION), rend son résultat
 *   linuxhost run PROGRAMME [ARG…]      un exécutable de Linux x86-64 (glibc, ET_EXEC ou PIE) démarré
 *                                       dans ce process (docs/etude/LINUX-HOST.md) ; argv[0] = PROGRAMME
 *   linuxhost --self-test               le module d'essai embarqué (lxtest.so, construit pour Linux
 *                                       par la gcc de WSL, avec glibc) : rapport, chargement,
 *                                       lx_run ; dit « linuxhost : RÉUSSI » ou « ÉCHEC »
 *   --stubs : un import absent devient un piège qui le nomme quand il est appelé
 *   --stats : à la fin, les appels système de Linux traduits
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../lib/linux/linux.h"

extern const unsigned char lxtest_so_start[], lxtest_so_end[];

static void *read_file (const char *path, size_t *size)
{
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		perror (path);
		return 0;
	}
	size_t cap = 1 << 20, n = 0;
	unsigned char *buf = malloc (cap);
	while (buf)
	{
		size_t k = fread (buf + n, 1, cap - n, f);
		n += k;
		if (n < cap)
			break;
		unsigned char *nb = realloc (buf, cap * 2);
		if (!nb)
		{
			free (buf);
			buf = 0;
			break;
		}
		buf = nb;
		cap *= 2;
	}
	fclose (f);
	if (!buf)
		fprintf (stderr, "%s : mémoire\n", path);
	*size = n;
	return buf;
}

static void stats (void)
{
	printf ("appels système de Linux traduits :");
	int any = 0;
	for (int i = 0; i < 512; i++)
		if (linux_syscall_count[i])
		{
			printf (" %d×%lu", i, linux_syscall_count[i]);
			any = 1;
		}
	printf (any ? "\n" : " aucun\n");
	printf ("syscall directs capturés par le noyau : %lu\n", linux_capture_count);
}

static int run (const void *img, size_t size, const char *name, const char *fn, int argc, char **argv, int flags)
{
	char err[200];
	struct linux_module *m = linux_load (img, size, name, flags, err, sizeof err);
	if (!m)
	{
		fprintf (stderr, "linuxhost : %s : %s\n", name, err);
		return 127;
	}
	printf ("linuxhost : %s chargé à %p\n", name, linux_base (m));
	int (*f) (int, char **) = (int (*) (int, char **)) linux_sym (m, fn);
	if (!f)
	{
		fprintf (stderr, "linuxhost : %s : pas de fonction %s\n", name, fn);
		linux_unload (m);
		return 127;
	}
	fflush (stdout);
	int r = f (argc, argv);
	fflush (stdout);
	linux_unload (m);
	return r;
}

int main (int argc, char **argv)
{
	int flags = 0, show_stats = 0, verbose = 0;
	char *args[64];
	int n = 0;
	for (int i = 1; i < argc && n < 63; i++)
	{
		if (n >= 2 && !strcmp (args[0], "run"))	/* (après run PROGRAMME : les arguments du programme) */
			args[n++] = argv[i];
		else if (!strcmp (argv[i], "--stubs"))
			flags |= LINUX_LOAD_STUBS;
		else if (!strcmp (argv[i], "--stats"))
			show_stats = 1;
		else if (!strcmp (argv[i], "-v"))
			verbose = 1;
		else
			args[n++] = argv[i];
	}
	args[n] = 0;
	if (n >= 1 && !strcmp (args[0], "--self-test"))
	{
		size_t size = (size_t) (lxtest_so_end - lxtest_so_start);
		printf ("linuxhost : module d'essai lxtest.so (%zu octets)\n", size);
		int missing = linux_check (lxtest_so_start, size, stdout, verbose);
		char *targv[] = { "lx_run", "un", "deux", 0 };
		int r = missing ? 1 : run (lxtest_so_start, size, "lxtest.so", "lx_run", 3, targv, flags);
		stats ();
		printf ("linuxhost : %s (%d)\n", r == 0 ? "RÉUSSI" : "ÉCHEC", r);
		return r;
	}
	if (n >= 2 && !strcmp (args[0], "run"))
	{
		extern char **environ;
		char err[300];
		/* un programme de Linux sans DISPLAY se croit sans écran et n'ouvre pas de navigateur (Claude Code :
		   la page de connexion) ; BROWSER le lui donne : xdg-open de Korin, qui ouvre Jet */
		setenv ("BROWSER", "xdg-open", 0);
		linux_exec (args[1], n - 1, args + 1, environ, flags, err, sizeof err);
		fprintf (stderr, "linuxhost : %s : %s\n", args[1], err);
		return 127;
	}
	if (n >= 2 && !strcmp (args[0], "--check"))
	{
		size_t size;
		void *img = read_file (args[1], &size);
		if (!img)
			return 1;
		int r = linux_check (img, size, stdout, verbose);
		free (img);
		return r == 0 ? 0 : 1;
	}
	if (n >= 2)
	{
		size_t size;
		void *img = read_file (args[0], &size);
		if (!img)
			return 1;
		int r = run (img, size, args[0], args[1], n - 1, args + 1, flags);
		free (img);
		if (show_stats)
			stats ();
		return r;
	}
	fprintf (stderr, "linuxhost --check FICHIER [-v] | run PROGRAMME [ARG…] | FICHIER.so FONCTION [ARG…] [--stubs] [--stats]"
		 " | --self-test\n");
	return 2;
}
