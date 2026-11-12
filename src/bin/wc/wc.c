/* wc - compte lignes, mots, octets : wc [-l] [-w] [-c] [fichiers] (docs/OUTILS.md), comme wc.asm :
 * options en tête (toute lettre autre que l et w compte les octets) ; mots séparés par espace,
 * tabulation ou fin de ligne ; sans nom (entrée standard), une espace suit chaque nombre quand
 * il y en a plusieurs (aussi le dernier) ; code 1 si un fichier manque (l'entrée standard : 0). */
#include "korinrt.h"

static int what; /* bits : 1 lignes, 2 mots, 4 octets */

static void value(u64 v, const char *name)
{
	out_putn(v);
	if (name || (what & (what - 1)))
		out_putc(' ');
}

static void process(const char *name)
{
	u64 lines = 0, words = 0, bytes = 0;
	int in_word = 0;
	for (;;) {
		int c = rd_getc(&krt_in);
		if (c < 0)
			break;
		bytes++;
		if (c == 10)
			lines++;
		if (c == ' ' || c == 9 || c == 10)
			in_word = 0;
		else if (!in_word) {
			in_word = 1;
			words++;
		}
	}
	if (what & 1)
		value(lines, name);
	if (what & 2)
		value(words, name);
	if (what & 4)
		value(bytes, name);
	if (name)
		out_puts(name);
	out_nl();
}

static int one_file(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		return 1;
	}
	rd_init(&krt_in, fd);
	process(path);
	in_close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int i = 1, code = 0;
	while (i < argc && is_opt(argv[i])) {
		for (const char *o = argv[i++] + 1; *o; o++)
			what |= *o == 'l' ? 1 : *o == 'w' ? 2 : 4;
	}
	if (!what)
		what = 7;
	if (i >= argc) {
		one_file(0);
		return 0;
	}
	for (; i < argc; i++)
		code |= one_file(argv[i]);
	return code;
}
