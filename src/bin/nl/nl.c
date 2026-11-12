/* nl - numérote les lignes : nl [-b a] [fichiers] (docs/OUTILS.md, d'après nl.c d'Onyx), comme
 * nl.asm : chaque ligne précédée de son numéro (6 colonnes) et d'une tabulation ; une ligne vide
 * n'est pas numérotée (-b a, -ba : toutes le sont ; -b X, X autre que a : comme sans option) ;
 * la numérotation continue d'un fichier à l'autre ; « -b » seul : usage et code 2 ; code 1 si
 * un fichier manque. */
#include "korinrt.h"

static int all, code;
static u64 num;

static void one_file(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		code = 1;
		return;
	}
	rd_init(&krt_in, fd);
	u64 len;
	char *l;
	while ((l = rd_line(&krt_in, &len))) {
		if (len || all) {
			out_putw(++num, 6);
			out_putc(9);
		}
		out_puts(l);
		out_nl();
	}
	in_close(fd);
}

int main(int argc, char **argv)
{
	int i = 1;
	if (argc >= 2) {
		if (str_eq(argv[1], "-ba")) {
			all = 1;
			i = 2;
		} else if (str_eq(argv[1], "-b")) {
			if (argc < 3) {
				err_msg("usage : nl [-b a] [fichiers]");
				return 2;
			}
			all = argv[2][0] == 'a';
			i = 3;
		}
	}
	if (i >= argc) {
		one_file(0);
		return code;
	}
	for (; i < argc; i++)
		one_file(argv[i]);
	return code;
}
