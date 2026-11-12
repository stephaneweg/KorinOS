/* tail - les dernières lignes : tail [-n N | -N] [fichiers] (docs/OUTILS.md), comme tail.asm :
 * mêmes options que head ; tout le fichier en mémoire (une région à soi, 1 Go à la demande, par
 * fichier), puis les N dernières lignes ; -0 se comporte comme -1 ; pas d'en-tête entre les
 * fichiers ; code 1 si un fichier manque (l'entrée standard : toujours 0). */
#include "korinrt.h"

static u64 count = 10;

static void process(void)
{
	u8 *base = sys_vm_map((u64)1 << 30), *end = base;
	for (;;) {
		int c = rd_getc(&krt_in);
		if (c < 0)
			break;
		*end++ = (u8)c;
	}
	if (end == base)
		return;
	u8 *p = end;
	if (p[-1] == 10)
		p--; /* (la dernière fin de ligne) */
	u64 n = count;
	for (;;) {
		if (p <= base) {
			p = base;
			break;
		}
		if (p[-1] == 10) {
			if (n <= 1) /* (subs puis b.ls, comme l'asm : 0 compte comme 1) */
				break;
			n--;
		}
		p--;
	}
	out_write(p, (u64)(end - p));
}

static int one_file(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		return 1;
	}
	rd_init(&krt_in, fd);
	process();
	in_close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int i = 1, code = 0;
	char *a = argv[1];
	if (a && is_opt(a)) {
		if (a[1] == 'n') {
			if (argv[2]) {
				count = parse_dec(argv[2]);
				i = 3;
			}
		} else {
			count = parse_dec(a + 1);
			i = 2;
		}
	}
	if (i >= argc) {
		one_file(0);
		return 0;
	}
	for (; i < argc; i++)
		code |= one_file(argv[i]);
	return code;
}
