/* writebench - débit d'écriture d'un fichier (diagnostic de la carte), comme
 * writebench.asm :
 *	writebench [chemin]	(/writebench.bin par défaut, jamais /media/sd1)
 * écrit 8 Mo par requêtes de 64 Ko dans un fichier neuf (clusters à allouer),
 * les réécrit sur place, réécrit 200 secteurs isolés, le tronque, le refait et
 * l'efface ; affiche ms et Ko/s de chacun. Une erreur : « writebench : erreur »
 * et le résultat de l'appel, code 1 (le fichier reste). Le chemin est la ligne
 * brute du noyau (krt_line), comme l'asm. */
#include "korinrt.h"

#define TOTAL (8 * 1024 * 1024)
#define CHUNK 65536
#define SECTORS 200

static const char *path;
static u8 buffer[CHUNK] __attribute__((aligned(16)));

static NORETURN void failed(long e)
{
	ak_puts("writebench : erreur ");
	print_dec(e);
	ak_puts("\n");
	_exit(1);
}

/* libellé, départ : durée affichée (elapsed) */
static void elapsed(const char *label, long start)
{
	ak_puts(label);
	print_dec(sys_time_ms() - start);
	ak_puts(" ms\n");
}

/* drapeaux d'ouverture, libellé : 8 Mo écrits, durée affichée (pass) */
static void pass(long flags, const char *label)
{
	long fd = sys_open(path, flags);
	if (fd < 0)
		failed(fd);
	long t = sys_time_ms();
	u64 n = 0;
	do {
		long r = sys_write(fd, buffer, CHUNK);
		if (r != CHUNK)
			failed(r);
		n += CHUNK;
	} while (n < TOTAL);
	sys_close(fd);
	t = sys_time_ms() - t;
	ak_puts(label);
	print_dec(t);
	ak_puts(" ms, ");
	print_dec((long)((n >> 10) * 1000 / (u64)(t + 1)));
	ak_puts(" Ko/s\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	path = krt_line && *krt_line ? krt_line : "/writebench.bin";
	pass(O_WRITE | O_CREATE | O_TRUNC, "fichier neuf : ");
	pass(O_WRITE, "sur place : ");
	long fd = sys_open(path, O_WRITE);
	if (fd < 0)
		failed(fd);
	long t = sys_time_ms();
	for (long i = 0; i < SECTORS; i++) {
		sys_seek(fd, i << 12, 0);
		long r = sys_write(fd, buffer, 512);
		if (r != 512)
			failed(r);
	}
	sys_close(fd);
	t = sys_time_ms() - t;
	ak_puts("secteurs isoles : ");
	print_dec(t);
	ak_puts(" ms, ");
	print_dec(SECTORS);
	ak_puts("\n");
	t = sys_time_ms();
	fd = sys_open(path, O_WRITE | O_TRUNC);
	if (fd < 0)
		failed(fd);
	sys_close(fd);
	elapsed("troncature de 8 Mo : ", t);
	pass(O_WRITE | O_CREATE | O_TRUNC, "fichier neuf : ");
	t = sys_time_ms();
	long r = sys_unlink(path);
	if (r)
		failed(r);
	elapsed("effacement de 8 Mo : ", t);
	_exit(0);
}
