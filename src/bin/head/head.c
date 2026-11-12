/* head - les premières lignes : head [-n N | -N] [fichiers] (docs/OUTILS.md), comme head.asm :
 * seul le premier argument peut être une option ; « -n » sans nombre n'en est pas une (il est
 * alors pris pour un fichier) ; toute autre option « -X » vaut -N avec N = parse_dec(X) ; pas
 * d'en-tête entre les fichiers ; code 1 si un fichier manque (l'entrée standard : toujours 0). */
#include "korinrt.h"

static u64 count = 10;

static void process(void)
{
	for (u64 n = count; n;) {
		int c = rd_getc(&krt_in);
		if (c < 0)
			break;
		if (c == 10)
			n--;
		out_putc(c);
	}
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
