/* cut - colonnes : cut -d C -f liste, ou cut -c liste [fichiers] (docs/OUTILS.md), comme
 * cut.asm : options en tête, valeur collée (-d,) ou dans l'argument suivant ; -d prend le premier
 * octet de sa valeur (tabulation par défaut) ; -c choisit des caractères (octets), toute autre
 * lettre (-f...) des champs ; liste « 1,3-5,7- » (rangs de 1 à 1023, « -3 » : de 1 à 3) ; une
 * ligne sans délimiteur sort entière ; sans liste : usage et code 1 ; code 1 si un fichier manque.
 * Comme l'asm, « -d » en dernier argument lit une valeur absente (faute). */
#include "korinrt.h"

static u8 wanted[1024];
static int chars, delim = 9;

/* *p : nombre décimal -> sa valeur, *p après */
static u64 number(const char **p)
{
	u64 v = 0;
	while ((u32)((u8)**p - '0') <= 9)
		v = v * 10 + (u64)((u8)*(*p)++ - '0');
	return v;
}

static void parse_list(const char *p)
{
	while (*p) {
		u64 a = number(&p), b;
		if (!a)
			a = 1;
		if (*p == '-') {
			p++;
			b = number(&p);
			if (!b)
				b = 1023;
		} else
			b = a;
		for (; a <= b && a <= 1023; a++)
			wanted[a] = 1;
		if (!*p)
			break;
		p++;
	}
}

static void process(void)
{
	char *line;
	while ((line = rd_line(&krt_in, 0))) {
		u64 r = 1;
		const char *p = line;
		if (chars) {
			for (;;) {
				u8 c = (u8)*p++;
				if (!c || r > 1023)
					break;
				if (wanted[r++])
					out_putc(c);
			}
		} else if (!strchr(line, delim) || !delim) {
			out_puts(line); /* sans délimiteur : la ligne entière */
		} else {
			int wrote = 0;
			for (;;) {
				const char *s = p;
				while (*p && *p != (char)delim)
					p++;
				if (r <= 1023 && wanted[r]) {
					if (wrote)
						out_putc(delim);
					wrote = 1;
					out_write(s, (u64)(p - s));
				}
				r++;
				if (!*p++)
					break;
			}
		}
		out_nl();
	}
}

static int one_file(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		return 1;
	}
	rd_init(&krt_in, fd);
	process();
	in_close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int i = 1, code = 0;
	const char *list = 0;
	while (i < argc && is_opt(argv[i])) {
		const char *a = argv[i++];
		const char *v = a + 2; /* valeur collée, ou argument suivant */
		if (!*v)
			v = argv[i++];
		if (a[1] == 'd') {
			delim = *(volatile const u8 *)v;
			continue;
		}
		if (a[1] == 'c')
			chars = 1;
		list = v;
	}
	if (!list) {
		err_msg("usage : cut -d C -f liste, ou cut -c liste");
		return 1;
	}
	parse_list(list);
	if (i >= argc) {
		one_file(0);
		return 0;
	}
	for (; i < argc; i++)
		code |= one_file(argv[i]);
	return code;
}
