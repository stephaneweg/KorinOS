/* uniq - retire les lignes répétées : uniq [-c] [-d] [-u] [fichier] (docs/OUTILS.md), comme
 * uniq.asm : options en tête (toute lettre autre que c et d vaut -u) ; un seul fichier (les
 * suivants sont ignorés) ; -c : le nombre puis une espace ; code 1 si le fichier manque. */
#include "korinrt.h"

static int opts; /* bits : 1 -c, 2 -d, 4 -u */
static u64 count;
static char kept[T_LINE];

static void emit(void)
{
	if ((opts & 2) && count <= 1)
		return;
	if ((opts & 4) && count != 1)
		return;
	if (opts & 1) {
		out_putn(count);
		out_putc(' ');
	}
	out_puts(kept);
	out_nl();
}

int main(int argc, char **argv)
{
	int i = 1;
	while (i < argc && is_opt(argv[i])) {
		for (const char *o = argv[i++] + 1; *o; o++)
			opts |= *o == 'c' ? 1 : *o == 'd' ? 2 : 4;
	}
	long fd = in_open(i < argc ? argv[i] : 0);
	if (fd < 0) {
		err_file(argv[i]);
		return 1;
	}
	rd_init(&krt_in, fd);
	char *l;
	while ((l = rd_line(&krt_in, 0))) {
		if (count) {
			if (!strcmp(l, kept)) {
				count++;
				continue;
			}
			emit();
		}
		strcpy(kept, l);
		count = 1;
	}
	if (count)
		emit();
	return 0;
}
