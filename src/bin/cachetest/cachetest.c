/* cachetest - une lecture voit-elle toujours la dernière écriture, le cache de
 * fatfs étant plein de ce fichier ? Comme cachetest.asm : chaque étape écrit
 * (dans un bloc déjà lu, à cheval sur un bloc, troncature, fichier refait sur
 * les mêmes clusters), puis relit tout par 4, 64 et 256 Ko (deux fois chaque)
 * et compare à la référence ; le fichier est effacé à la fin.
 *	cachetest [chemin]	(/cachetest.bin par défaut)
 * Sortie : une ligne par échec, puis « cachetest : PASS / FAIL (n) » ; codes
 * 0 et 1. Le chemin est la ligne brute du noyau (krt_line), comme l'asm. */
#include "korinrt.h"

#define SIZE 0x40000

static const char *path;
static long fails;
static u64 ref_size; /* taille de la référence (x23) */
static u8 ref[SIZE + 0x10000] __attribute__((aligned(16)));
static u8 got[SIZE * 2 + 0x40000] __attribute__((aligned(16)));

/* graine, position, octets : la référence à cet endroit (motif) */
static void pattern(u64 seed, u64 pos, u64 n)
{
	for (u64 i = 0; i < n; i++) {
		u64 x = i + pos;
		ref[pos + i] = (u8)(((x * seed) ^ (x >> 8) ^ (x >> 16)) + seed);
	}
}

static void write_failed(void)
{
	fails++;
	ak_puts("cachetest : ecriture ratee\n");
}

/* taille des lectures : le fichier relu doit être la référence (relire) */
static void reread(u64 chunk)
{
	long fd = sys_open(path, O_READ);
	if (fd >= 0) {
		u64 n = 0;
		for (;;) {
			long r = sys_read(fd, got + n, chunk);
			if (r <= 0)
				break;
			n += (u64)r;
			if (n >= SIZE * 2)
				break;
		}
		sys_close(fd);
		if (n == ref_size) {
			u64 i = 0;
			while (i < ref_size && ref[i] == got[i])
				i++;
			if (i >= ref_size)
				return;
		}
	}
	fails++;
	ak_puts("cachetest : differe, lectures de ");
	print_dec((long)chunk);
	ak_puts(")\n");
}

/* relu par 4, 64 et 256 Ko, deux fois chaque (verifier) */
static void verify(void)
{
	static const u32 chunks[] = {4096, 65536, 262144, 0};
	for (int i = 0; chunks[i]; i++) {
		reread(chunks[i]);
		reread(chunks[i]);
	}
}

/* graine, position, octets : écrits sur place, puis vérifié (morceau) */
static void piece(u64 seed, u64 pos, u64 n)
{
	pattern(seed, pos, n);
	if (pos + n > ref_size)
		ref_size = pos + n;
	long fd = sys_open(path, O_WRITE);
	if (fd < 0) {
		write_failed();
		return;
	}
	sys_seek(fd, (long)pos, 0);
	if (sys_write(fd, ref + pos, n) != (long)n) {
		sys_close(fd);
		write_failed();
		return;
	}
	sys_close(fd);
	verify();
}

/* toute la référence dans le fichier, refait (tout_ecrire) */
static void write_all(void)
{
	long fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0) {
		long r = sys_write(fd, ref, ref_size);
		sys_close(fd);
		if (r == (long)ref_size)
			return;
	}
	write_failed();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	path = krt_line && *krt_line ? krt_line : "/cachetest.bin";
	ref_size = SIZE;
	pattern(1, 0, ref_size); /* 1 : fichier neuf */
	write_all();
	verify();
	piece(2, 70001, 1000);	   /* 2 : milieu d'un bloc, non aligné */
	piece(3, 0x10000, 0x10000); /* 3 : un bloc entier, aligné */
	piece(4, 0x20000, 512);	   /* 4 : un secteur, aligné */
	piece(5, 0x2FF00, 512);	   /* 5 : à cheval sur deux blocs */
	piece(6, ref_size, 4096);  /* 6 : la fin du fichier grandit */
	ref_size = 100000;	   /* 7 : troncature, plus court */
	pattern(7, 0, ref_size);
	write_all();
	verify();
	sys_unlink(path); /* 8 : effacer, refaire : mêmes clusters */
	ref_size = SIZE;
	pattern(8, 0, ref_size);
	write_all();
	verify();
	sys_unlink(path);
	ak_puts("cachetest : ");
	if (fails) {
		ak_puts("FAIL (");
		print_dec(fails);
		ak_puts(")\n");
		_exit(1);
	}
	ak_puts("PASS\n");
	_exit(0);
}
