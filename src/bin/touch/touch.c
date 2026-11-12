/* touch - crée un fichier ou remet sa date à maintenant : touch [-c] fichiers (docs/OUTILS.md),
 * comme touch.asm : seul le premier argument peut être une option, et toute option vaut -c (ne
 * pas créer) ; un dossier prend aussi la date ; code 1 si l'un échoue. */
#include "korinrt.h"

int main(int argc, char **argv)
{
	int i = 1, no_create = 0, code = 0;
	if (!argv[1])
		return 0;
	if (is_opt(argv[1])) {
		no_create = 1;
		i = 2;
	}
	for (; i < argc; i++) {
		const char *p = argv[i];
		if (path_type(p) >= 0) {
			if (!vfs_utime(p, (u64)sys_clock(0)))
				continue;
		} else {
			if (no_create)
				continue;
			long fd = sys_open(p, O_WRITE | O_CREATE);
			if (fd >= 0) {
				sys_close(fd);
				continue;
			}
		}
		err_file(p);
		code = 1;
	}
	return code;
}
