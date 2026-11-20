/*
 * fbench.c -- la vitesse de lecture d'un gros fichier par stdio et par read (), sous Korin (libposix) :
 *
 *   fbench FICHIER
 *
 * fread par blocs de 64 Ko avec le tampon du FILE par défaut (celui que newlib dimensionne d'après st_blksize de
 * fstat), le même après setvbuf (64 Ko), puis read () par blocs de 4 Mo. Le 20/11/2026, sous QEMU PC, un fichier de
 * 21,7 Mo se lisait en 11 s par fread (st_blksize = 512 : un read () de 512 octets par remplissage) et en 1 s par
 * read () (docs/POSIX.md, « stdio et st_blksize »).
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (double) t.tv_sec + t.tv_nsec / 1e9;
}

static void report (const char *how, double t, size_t n)
{
	fprintf (stderr, "fbench : %-28s %8.2f s  %7.1f Mo/s  (%zu octets)\n", how, t, n / 1048576.0 / (t > 0 ? t : 1e-9), n);
}

static size_t by_fread (const char *path, int vbuf)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return 0;
	static char vb[65536];
	if (vbuf)
		setvbuf (f, vb, _IOFBF, sizeof vb);
	static char b[65536];
	size_t n = 0, k;
	while ((k = fread (b, 1, sizeof b, f)) > 0)
		n += k;
	fclose (f);
	return n;
}

static size_t by_read (const char *path)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0)
		return 0;
	static char b[4 << 20];
	size_t n = 0;
	ssize_t k;
	while ((k = read (fd, b, sizeof b)) > 0)
		n += (size_t) k;
	close (fd);
	return n;
}

int main (int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf (stderr, "usage : fbench FICHIER\n");
		return 2;
	}
	struct stat st;
	if (stat (argv[1], &st) != 0)
	{
		fprintf (stderr, "fbench : %s introuvable\n", argv[1]);
		return 1;
	}
	fprintf (stderr, "fbench : %s, %lld octets, st_blksize %ld\n", argv[1], (long long) st.st_size, (long) st.st_blksize);
	double t = now ();
	size_t n = by_fread (argv[1], 0);
	report ("fread (tampon par défaut)", now () - t, n);
	t = now ();
	n = by_fread (argv[1], 1);
	report ("fread (setvbuf 64 Ko)", now () - t, n);
	t = now ();
	n = by_read (argv[1]);
	report ("read (4 Mo)", now () - t, n);
	fprintf (stderr, "fbench : fini\n");
	return 0;
}
