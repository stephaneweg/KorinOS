/* echo - écrit ses arguments : echo [-n] mots (docs/OUTILS.md, comme echo.asm : seul « -n » en
 * premier argument est une option ; une espace entre les mots) */
#include "korinrt.h"

int main(int argc, char **argv)
{
	int i = 1, nl = 1;
	if (argv[1] && str_eq(argv[1], "-n")) {
		nl = 0;
		i = 2;
	}
	for (; i < argc; i++) {
		out_puts(argv[i]);
		if (i + 1 < argc)
			out_putc(' ');
	}
	if (nl)
		out_nl();
	return 0;
}
