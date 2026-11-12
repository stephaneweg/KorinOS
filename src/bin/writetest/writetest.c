/* writetest - écrit un fichier de motif sur la carte, le relit et compare,
 * comme writetest.asm. Le fichier reste (SD0:/writetest.bin) : tools ou PC
 * peuvent le relire ; le motif : mot n du fichier = n x 0x9E3779B97F4A7C15,
 * sur 64 bits. Sortie : durée d'écriture, puis « writetest : PASS / FAIL ».
 * Codes : 0 bon, 1 relu faux (« FAIL a » position du morceau), 2 erreur
 * d'ouverture, d'écriture ou de lecture courte à l'ouverture (« erreur » et
 * le résultat de l'appel). Pas d'argument (comme l'asm). */
#include "korinrt.h"

#define TOTAL 0x800000 /* 8 Mo */
#define CHUNK 0x40000  /* 256 Ko */
#define MULT 0x9E3779B97F4A7C15ull

static u64 buf[CHUNK / 8] __attribute__((aligned(16)));
static u64 back[CHUNK / 8] __attribute__((aligned(16)));

/* position dans le fichier -> buf : le motif de ce morceau (remplit) */
static void fill(u64 pos)
{
	u64 w = pos >> 3;
	for (u64 i = 0; i < CHUNK / 8; i++)
		buf[i] = (w + i) * MULT;
}

static NORETURN void fail_err(long e)
{
	ak_puts("writetest : erreur ");
	print_dec(e);
	ak_puts("\n");
	_exit(2);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long fd = sys_open("SD0:/writetest.bin", O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		fail_err(fd);
	long t = sys_time_ms();
	for (u64 done = 0; done < TOTAL; done += CHUNK) {
		fill(done);
		long r = sys_write(fd, buf, CHUNK);
		if (r != CHUNK)
			fail_err(r);
	}
	sys_close(fd);
	t = sys_time_ms() - t;
	ak_puts("writetest : ecrit en ms : ");
	print_dec(t);
	ak_puts("\n");
	fd = sys_open("SD0:/writetest.bin", O_READ); /* relecture */
	if (fd < 0)
		fail_err(fd);
	for (u64 done = 0; done < TOTAL; done += CHUNK) {
		fill(done);
		long r = sys_read(fd, back, CHUNK);
		int ok = r == CHUNK;
		for (u64 i = 0; ok && i < CHUNK / 8; i++)
			ok = buf[i] == back[i];
		if (!ok) {
			ak_puts("writetest : FAIL a ");
			print_dec((long)done);
			ak_puts("\n");
			_exit(1);
		}
	}
	ak_puts("writetest : PASS\n");
	_exit(0);
}
