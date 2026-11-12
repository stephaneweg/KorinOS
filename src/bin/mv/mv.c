/* mv - déplace ou renomme : mv [-f] source... destination (docs/OUTILS.md), comme mv.asm : seul
 * le premier argument peut être une option, ignorée ; une destination qui est un dossier reçoit
 * la source sous son nom ; d'un volume à l'autre (E_XDEV), un fichier est recopié puis effacé ;
 * code 1 si l'un échoue, 1 et l'usage s'il manque un argument. */
#include "korinrt.h"

static const char *dest;

/* source -> 0, 1 */
static int move(char *src)
{
	char b[320];
	const char *to = dest;
	if (path_type(dest) == 1) {
		path_join2(b, dest, path_base(src));
		to = b;
	}
	long r = vfs_rename(src, to);
	if (!r)
		return 0;
	if (r == E_XDEV && !copy_file(src, to)) {
		sys_unlink(src);
		return 0;
	}
	err_file(src);
	return 1;
}

int main(int argc, char **argv)
{
	int i = 1, code = 0;
	if (!argv[1])
		goto usage;
	if (is_opt(argv[1]))
		i = 2;
	if (i >= argc - 1)
		goto usage;
	dest = argv[argc - 1];
	for (; i < argc - 1; i++)
		code |= move(argv[i]);
	return code;
usage:
	err_msg("usage : mv source... destination");
	return 1;
}
