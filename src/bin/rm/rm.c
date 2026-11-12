/* rm - efface des fichiers : rm [-f] [-r] chemins (docs/OUTILS.md), comme rm.asm : options en
 * tête (f, r ou R ; les autres lettres sont ignorées) ; -r vide un dossier entrée par entrée (il
 * est rouvert après chaque effacement) et s'arrête à la première qui résiste ; -f tait les
 * erreurs ; code 1 si l'un échoue. */
#include "korinrt.h"

static int opts; /* bits : 1 -f, 2 -r */

static int erase(const char *path);

/* dossier : tout ce qu'il contient, effacé */
static void empty_dir(const char *dir)
{
	struct dirent e;
	char path[320];
	for (;;) {
		long fd = sys_open(dir, O_READ);
		if (fd < 0)
			return;
		for (;;) {
			if (sys_readdir(fd, &e) != 1) {
				sys_close(fd);
				return;
			}
			if (e.name[0] == '.' && (!e.name[1] || (e.name[1] == '.' && !e.name[2])))
				continue;
			break;
		}
		sys_close(fd);
		path_join2(path, dir, e.name);
		if (erase(path))
			return;
	}
}

/* chemin -> 0, 1 : erreur */
static int erase(const char *path)
{
	if ((opts & 2) && path_type(path) == 1)
		empty_dir(path);
	if (!sys_unlink(path) || (opts & 1))
		return 0;
	err_file(path);
	return 1;
}

int main(int argc, char **argv)
{
	int i = 1, code = 0;
	while (i < argc && is_opt(argv[i])) {
		for (const char *o = argv[i++] + 1; *o; o++) {
			if (*o == 'f')
				opts |= 1;
			else if (*o == 'r' || *o == 'R')
				opts |= 2;
		}
	}
	for (; i < argc; i++)
		code |= erase(argv[i]);
	return code;
}
