/* mkdir - crée des dossiers : mkdir [-p] dossiers (docs/OUTILS.md), comme mkdir.asm : seul le
 * premier argument peut être une option, et toute option vaut -p (parents créés, un dossier déjà
 * là n'est pas une erreur) ; un chemin de 300 octets ou plus n'a pas ses parents créés ; code 1
 * si l'un échoue. */
#include "korinrt.h"

/* chemin : ses dossiers parents créés (chaque « / » qui ne suit pas « : », sauf en tête) */
static void parents(const char *path)
{
	char b[304];
	size_t n = 0;
	for (;;) {
		b[n] = path[n];
		if (!b[n])
			break;
		if (++n >= 300)
			return;
	}
	for (char *p = b + 1; *p; p++) {
		if (*p != '/' || p[-1] == ':')
			continue;
		*p = 0;
		sys_mkdir(b);
		*p = '/';
	}
}

int main(int argc, char **argv)
{
	int i = 1, p = 0, code = 0;
	if (!argv[1])
		return 0;
	if (is_opt(argv[1])) {
		p = 1;
		i = 2;
	}
	for (; i < argc; i++) {
		const char *d = argv[i];
		if (p)
			parents(d);
		if (!sys_mkdir(d))
			continue;
		if (p && path_type(d) == 1) /* -p : déjà là, c'est bon */
			continue;
		err_file(d);
		code = 1;
	}
	return code;
}
