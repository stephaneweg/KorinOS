/* dirname - le dossier d'un chemin : dirname chemin (docs/OUTILS.md, comme dirname.asm : « . »
 * sans dossier ou sans argument ; « /x » donne « / », « SD0:x » donne « / » ; « a/ » donne
 * « . » ; « a//b » donne « a/ ») */
#include "korinrt.h"

int main(int argc, char **argv)
{
	(void)argc;
	char *p = argv[1];
	char *b = p ? path_base(p) : 0;
	if (!p || b == p) {
		out_putc('.');
	} else {
		char *s = b - 1; /* le séparateur */
		if (*s == ':' || s == p)
			s++;
		*s = 0;
		out_puts(p);
	}
	out_nl();
	return 0;
}
