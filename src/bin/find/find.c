/* find - parcourt des dossiers : find chemins [-name motif] [-type f|d] [-maxdepth n] (docs/OUTILS.md),
 * comme find.asm : les chemins jusqu'au premier mot qui commence par « - » (aucun : « . ») ; puis
 * -name (motif * ? sur le dernier composant, avec la casse), -type (f : fichiers, toute autre lettre :
 * dossiers), -maxdepth (décimal) ; les autres mots sont ignorés (-print aussi). Chaque chemin est
 * écrit avant ce qu'il contient, dans l'ordre du dossier ; « . » et « .. » sautés. Un chemin absent :
 * « find: chemin : inaccessible », et le code reste 0 (comme l'asm : toujours 0).
 *
 * Comme l'asm : « -name » sans motif n'en prend pas ; « -type » sans lettre lit l'adresse 0 (faute). */
#include "korinrt.h"

static const char *pattern;
static int type; /* 1 fichiers, 2 dossiers */
static u64 maxdepth = 0x7FFFFFFF;

/* motif (* ?), nom -> 1 (motif) */
static int match(const char *p, const char *s)
{
	for (; *p; p++, s++) {
		if (*p == '*') {
			p++;
			for (;;) {
				if (match(p, s))
					return 1;
				if (!*s++)
					return 0;
			}
		}
		if (!*s)
			return 0;
		if (*p != '?' && *p != *s)
			return 0;
	}
	return !*s;
}

static void visit(char *path, u64 depth)
{
	struct dirent e;
	char sub[320];
	int t = path_type(path);
	if (t < 0) {
		err_file(path);
		return;
	}
	if ((!type || (type == 2) == t) && (!pattern || match(pattern, path_base(path)))) {
		out_puts(path);
		out_nl();
	}
	if (!t || depth >= maxdepth)
		return;
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return;
	while (sys_readdir(fd, &e) == 1) {
		if (e.name[0] == '.' && (!e.name[1] || (e.name[1] == '.' && !e.name[2])))
			continue;
		path_join2(sub, path, e.name);
		visit(sub, depth + 1);
	}
	sys_close(fd);
}

int main(int argc, char **argv)
{
	int n = 1;
	while (n < argc && argv[n][0] != '-')
		n++;
	for (int i = n; i < argc;) {
		const char *o = argv[i++];
		if (str_eq(o, "-name"))
			pattern = argv[i++];
		else if (str_eq(o, "-type")) {
			const volatile char *p = argv[i++]; /* (comme l'asm : sans lettre, faute) */
			type = *p == 'f' ? 1 : 2;
		} else if (str_eq(o, "-maxdepth"))
			maxdepth = parse_dec(argv[i++]);
	}
	if (n == 1) {
		static char dot[] = ".";
		visit(dot, 0);
	}
	for (int i = 1; i < n; i++)
		visit(argv[i], 0);
	return 0;
}
