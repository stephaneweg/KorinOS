/* sort - trie des lignes : sort [-r] [-n] [-u] [fichiers] (docs/OUTILS.md), comme sort.asm :
 * options en tête (les autres lettres sont ignorées) ; ordre des octets ; -n : le nombre au début
 * (espaces, signe -, chiffres ; 0 sans chiffre), à égalité l'ordre des octets ; -r inverse ; -u :
 * une ligne égale à la précédente (même comparaison) n'est pas écrite ; tri fusion stable ; un
 * fichier qui manque est signalé, le code reste 0. */
#include "korinrt.h"

static int opts; /* bits : 1 -r, 2 -n, 4 -u */
static char *text;  /* les lignes à la suite (région de 1 Go à la demande) */
static char **lines; /* (région de 256 Mo) */
static u64 nlines;

static i64 number(const char *s)
{
	u64 v = 0;
	int neg = 0;
	while (*s == ' ')
		s++;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	for (;;) {
		u32 d = (u32)(u8)*s++ - '0';
		if (d > 9)
			break;
		v = v * 10 + d;
	}
	return (i64)(neg ? -v : v);
}

/* -> < 0, 0, > 0 (le signe seulement : la différence de l'asm, en 64 bits) */
static int compare(const void *x, const void *y, void *ctx)
{
	(void)ctx;
	const u8 *a = x, *b = y;
	i64 d;
	if (opts & 2) {
		d = (i64)((u64)number(x) - (u64)number(y));
		if (d)
			goto sens;
	}
	for (;; a++, b++) {
		if (*a != *b) {
			d = (i64)*a - (i64)*b;
			goto sens;
		}
		if (!*a)
			return 0;
	}
sens:
	if (opts & 1)
		d = -d;
	return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static void read_file(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		return;
	}
	rd_init(&krt_in, fd);
	char *l;
	while ((l = rd_line(&krt_in, 0))) {
		lines[nlines++] = text;
		while ((*text++ = *l++))
			;
	}
	in_close(fd);
}

int main(int argc, char **argv)
{
	int i = 1;
	while (i < argc && is_opt(argv[i])) {
		for (const char *o = argv[i++] + 1; *o; o++)
			opts |= *o == 'r' ? 1 : *o == 'n' ? 2 : *o == 'u' ? 4 : 0;
	}
	text = sys_vm_map((u64)1 << 30);
	lines = sys_vm_map((u64)1 << 28);
	if (i >= argc)
		read_file(0);
	for (; i < argc; i++)
		read_file(argv[i]);
	sort_ptrs((void **)lines, nlines, compare, 0);
	for (u64 k = 0; k < nlines; k++) {
		if (k && (opts & 4) && !compare(lines[k - 1], lines[k], 0))
			continue;
		out_puts(lines[k]);
		out_nl();
	}
	return 0;
}
