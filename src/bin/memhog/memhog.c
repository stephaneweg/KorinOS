/* memhog - essai de la mémoire rare : un cache plein doit se vider à temps (comme memhog.asm).
 *	memhog read <chemin>	lit tout le fichier (remplit le cache de fatfs)
 *	memhog touch <Mo>	mmap et touche toute la mémoire libre + <Mo>
 *	memhog sbrk <Mo>	la même chose d'un seul appel sbrk
 * touch et sbrk ne passent que si le cache rend au moins <Mo> : sinon le process est tué, ou sbrk
 * échoue. Sortie : pages obtenues et temps, puis la mémoire reste prise jusqu'à la fin du programme.
 *
 * Comme l'asm : seule la ligne brute du noyau compte (krt_line), coupée au premier espace ; un mot
 * seul (« memhog read ») donne l'usage, code 1 ; <Mo> est lu chiffre par chiffre jusqu'au premier
 * qui n'en est pas un (« touch x » : 0 Mo de plus) ; les messages vont par puts d'AppKit (sans tampon). */
#include "korinrt.h"

#define CHUNK 0x100000
#define VS_FREE_LOW 48
#define VS_FREE_HIGH 56

static u64 stats[8];
static u8 buffer[CHUNK] __attribute__((aligned(16)));
static char line[4096];

NORETURN static void fail(long e)
{
	ak_puts("memhog : erreur ");
	print_dec(e);
	ak_puts("\n");
	_exit(1);
}

NORETURN static void usage(void)
{
	ak_puts("memhog read <chemin> | touch <Mo> | sbrk <Mo>\n");
	_exit(1);
}

NORETURN static void do_read(const char *path)
{
	long fd = ksys(SYS_OPEN, (long)path, O_READ, 0, 0, 0, 0);
	if (fd < 0)
		fail(fd);
	u64 total = 0;
	for (;;) {
		long n = ksys(SYS_READ, fd, (long)buffer, CHUNK, 0, 0, 0);
		if (n <= 0)
			break;
		total += n;
	}
	ksys(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	print_dec(total >> 20);
	ak_puts(" Mo lus\n");
	_exit(0);
}

/* touch : 1 (VM_MAP puis un mot par page), 0 : sbrk */
NORETURN static void take(const char *p, int touch)
{
	u64 mb = 0;
	for (;;) {
		u32 d = (u8)*p++ - '0';
		if (d > 9)
			break;
		mb = mb * 10 + d;
	}
	long r = korin_sys2(SYS_VM_STATS, 0, stats);
	if (r < 0)
		fail(r);
	u64 free_pages = stats[VS_FREE_LOW / 8] + stats[VS_FREE_HIGH / 8];
	u64 want = free_pages + (mb << 8);
	print_dec(free_pages >> 8);
	ak_puts(" Mo libres, ");
	print_dec(want >> 8);
	ak_puts(" Mo demandes\n");
	long t0 = korin_sys0(SYS_TIME_MS);
	if (touch) {
		long a = korin_sys4(SYS_VM_MAP, 0, want << 12, PROT_READ | PROT_WRITE, 0);
		/* (comme l'asm : son « cmp x0,0 ; b.lo » n'est jamais pris, une erreur n'est pas vue) */
		volatile u32 *q = (volatile u32 *)a;
		u64 n = want;
		do {
			*q = 1;
			q += 1024;
		} while (--n);
	} else {
		long a = korin_sys1(SYS_SBRK, want << 12);
		if (a < 0)
			fail(a);
	}
	long dt = korin_sys0(SYS_TIME_MS) - t0;
	ak_puts("memhog : obtenu en ");
	print_dec(dt);
	ak_puts(" ms\n");
	_exit(0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line)
		usage();
	strlcpy(line, krt_line, sizeof line);
	char *w = line;
	for (;;) { /* deuxième mot : après le premier espace */
		char c = *w;
		if (!c)
			usage();
		w++;
		if (c == ' ')
			break;
	}
	w[-1] = 0;
	if (str_eq(line, "read"))
		do_read(w);
	long t = str_eq(line, "touch");
	if (t | str_eq(line, "sbrk"))
		take(w, t != 0);
	usage();
}
