/* cp - copie : cp [-r] source... destination (docs/OUTILS.md), comme cp.asm : options en tête
 * (r, R ou a : récursif ; les autres lettres, -f compris, sont ignorées) ; une destination qui est
 * un dossier reçoit la source sous son nom ; -r crée les dossiers (sans erreur s'ils existent) et
 * continue après une erreur ; code 1 si l'une échoue, 1 et l'usage s'il manque un argument. */
#include "korinrt.h"

static int recursive;

/* source, destination exacte -> 0, 1 */
static int copy_tree(const char *src, const char *dst)
{
	struct dirent e;
	char a[320], b[320];
	int t = path_type(src);
	if (t < 0)
		goto err;
	if (t == 0) {
		long r = copy_file(src, dst);
		if (!r)
			return 0;
		if (r == E_NOSPC) { /* (la destination, tronquée, reste : comme GNU cp) */
			err_file_e(dst, r);
			return 1;
		}
		goto err;
	}
	if (!recursive) {
		err_msg("un dossier demande -r");
		return 1;
	}
	sys_mkdir(dst);
	long fd = sys_open(src, O_READ);
	if (fd < 0)
		goto err;
	int code = 0;
	while (sys_readdir(fd, &e) == 1) {
		if (e.name[0] == '.' && (!e.name[1] || (e.name[1] == '.' && !e.name[2])))
			continue;
		path_join2(a, src, e.name);
		path_join2(b, dst, e.name);
		code |= copy_tree(a, b);
	}
	sys_close(fd);
	return code;
err:
	err_file(src);
	return 1;
}

/* source, destination (si c'est un dossier : dedans) -> 0, 1 */
static int copy_to(char *src, const char *dst)
{
	char b[320];
	if (path_type(dst) == 1) {
		path_join2(b, dst, path_base(src));
		dst = b;
	}
	return copy_tree(src, dst);
}

int main(int argc, char **argv)
{
	int i = 1, code = 0;
	while (i < argc && is_opt(argv[i])) {
		for (const char *o = argv[i++] + 1; *o; o++)
			if (*o == 'r' || *o == 'R' || *o == 'a')
				recursive = 1;
	}
	if (i >= argc || i >= argc - 1) {
		err_msg("usage : cp [-r] source... destination");
		return 1;
	}
	const char *dst = argv[argc - 1];
	for (; i < argc - 1; i++)
		code |= copy_to(argv[i], dst);
	return code;
}
