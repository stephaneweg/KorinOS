/* basename - le dernier composant d'un chemin : basename chemin [suffixe] (docs/OUTILS.md,
 * comme basename.asm : sans argument, rien et code 0 ; « a/ » rend « a/ » ; le suffixe n'est
 * retiré que s'il est plus court que le nom) */
#include "korinrt.h"

int main(int argc, char **argv)
{
	(void)argc;
	if (!argv[1])
		return 0;
	char *b = path_base(argv[1]);
	const char *suf = argv[2];
	if (suf) {
		size_t lb = strlen(b), ls = strlen(suf);
		if (ls < lb && !strcmp(b + lb - ls, suf))
			b[lb - ls] = 0;
	}
	out_puts(b);
	out_nl();
	return 0;
}
