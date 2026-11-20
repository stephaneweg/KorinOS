/* grep - cherche un motif : grep [-Eivnclq] [-e motif] motif [fichiers] (docs/OUTILS.md), comme
 * grep.asm : expressions de src/lib/korinrt/regex.c (celles de regex.inc) ; options en tête,
 * groupées (-in) ; -e prend l'argument suivant et termine son groupe ; les autres lettres (-s,
 * -F...) sont ignorées ; plusieurs fichiers : leur nom devant chaque ligne (et devant le compte
 * de -c) ; -l : le nom du fichier dès la première ligne trouvée (rien pour l'entrée standard) ;
 * -q : code 0 à la première ligne trouvée. Code : 0 si une ligne est trouvée après la dernière
 * erreur, 2 si un fichier manque après la dernière ligne trouvée, 1 sinon ; usage : code 2. */
#include "korinrt.h"

static int opts; /* bits : 1 -v, 2 -n, 4 -c, 8 -l, 16 -q, 32 nom devant */
static int code = 1;
static struct regex *re;

/* une ligne qui contient des octets nuls (un fichier binaire : une base SQLite commence par « SQLite format 3 » et
 * un nul) : chaque morceau entre deux nuls est cherché, comme le texte entier de grep -a (un motif ne contient
 * jamais de nul) ; sans cela tout ce qui suit le premier nul de la ligne était invisible */
static int match(char *l, u64 len, const char *caps[RE_CAPS])
{
	for (char *e = l + len;;) {
		if (re_search(re, l, l, caps))
			return 1;
		l += strlen(l) + 1;
		if (l > e)
			return 0;
	}
}

static void process(const char *name)
{
	u64 lnum = 0, found = 0, len;
	const char *caps[RE_CAPS];
	char *l;
	while ((l = rd_line(&krt_in, &len))) {
		lnum++;
		int m = match(l, len, caps);
		if (opts & 1)
			m ^= 1;
		if (!m)
			continue;
		found++;
		code = 0;
		if (opts & 16)
			exit(0);
		if (opts & 8)
			break;
		if (opts & 4)
			continue;
		if (opts & 32) {
			out_puts(name);
			out_putc(':');
		}
		if (opts & 2) {
			out_putn(lnum);
			out_putc(':');
		}
		out_write(l, len); /* (la ligne entière, ses nuls compris, comme grep -a) */
		out_nl();
	}
	if (opts & 8) {
		if (found && name) {
			out_puts(name);
			out_nl();
		}
	} else if (opts & 4) {
		if (opts & 32) {
			out_puts(name);
			out_putc(':');
		}
		out_putn(found);
		out_nl();
	}
}

static void one_file(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		code = 2;
		return;
	}
	rd_init(&krt_in, fd);
	process(path);
	in_close(fd);
}

int main(int argc, char **argv)
{
	int i = 1, flags = 0;
	const char *pat = 0;
	while (i < argc && is_opt(argv[i])) {
		for (const char *o = argv[i++] + 1; *o; o++) {
			if (*o == 'E')
				flags |= RE_E;
			else if (*o == 'i')
				flags |= RE_I;
			else if (*o == 'v')
				opts |= 1;
			else if (*o == 'n')
				opts |= 2;
			else if (*o == 'c')
				opts |= 4;
			else if (*o == 'l')
				opts |= 8;
			else if (*o == 'q')
				opts |= 16;
			else if (*o == 'e') {
				pat = argv[i++];
				break;
			}
		}
	}
	if (!pat) {
		if (i >= argc)
			goto usage;
		pat = argv[i++];
	}
	re = re_compile(pat, flags);
	if (!re)
		goto usage;
	if ((unsigned)(argc - i) > 1)
		opts |= 32;
	if (i >= argc)
		one_file(0);
	for (; i < argc; i++)
		one_file(argv[i]);
	return code;
usage:
	err_msg("usage : grep [-Eivnclq] [-e motif] motif [fichiers]");
	return 2;
}
