/* sed - édite un flux : sed [-nE] [-e script] script [fichiers] (docs/OUTILS.md), comme sed.asm :
 * expressions de src/lib/korinrt/regex.c ; adresses N, $, /re/ (une ou deux, « , ») ; commandes
 * s/re/rempl/[gp] (& et \0..\9, \n ; le délimiteur est le caractère après s), d, p, q ; les autres
 * lettres sont ignorées ; « ; », blancs et fins de ligne séparent les commandes ; 256 commandes au
 * plus (la suite est ignorée). Options en tête, groupées : n, E ou r, e (prend le mot suivant et
 * termine le groupe) ; les autres lettres sont ignorées. Sans -e, le premier mot est le script ;
 * sans fichier, l'entrée standard. Fichier absent : « sed: chemin : inaccessible », code 0 quand même ;
 * usage : code 1.
 *
 * Comme l'asm : -E ne vaut que pour les scripts qui le suivent ; l'adresse 0 vaut « pas d'adresse » ;
 * deux adresses : la seconde n'est cherchée qu'à partir de la ligne qui suit la première (et l'état
 * reste d'un fichier à l'autre, le numéro de ligne non) ; après q, chaque fichier suivant fait encore
 * tourner le script sur sa première ligne (sans l'écrire à la fin) ; une adresse sans commande prend
 * l'octet nul pour commande et la lecture du script continue après lui (sur le mot suivant d'argv). */
#include "korinrt.h"

#define CMD_MAX 256

struct cmd {
	int op;
	long a1;               /* 0 aucune, > 0 numéro, -1 $, -2 motif */
	struct regex *a1r;
	long a2;
	struct regex *a2r;
	int in;                /* 1 : entre les deux adresses */
	struct regex *re;      /* s : motif */
	const char *repl;      /* s : remplacement */
	int flags;             /* s : 1 g, 2 p */
};

static struct cmd *cmds;
static long ncmds;
static int quiet, reflags, quit;
static u64 num;
static const char *cur;   /* le script (x25) */
static char *next_line;   /* la ligne suivante (x28 ; 0 : c'était la dernière) */
static char space[T_LINE];
static char redo[2 * T_LINE];

/* délimiteur -> copie jusqu'à lui (\délimiteur : lui-même), cur après lui (jusqua) */
static char *until(int delim)
{
	char *d = malloc((u64)strlen(cur) + 2), *o = d;
	for (;;) {
		u8 c = (u8)*cur;
		if (!c)
			break;
		cur++;
		if (c == delim)
			break;
		if (c == '\\' && (u8)*cur == delim) {
			c = (u8)*cur;
			cur++;
		}
		*o++ = (char)c;
	}
	*o = 0;
	return d;
}

static void address(long *a, struct regex **r)
{
	u8 c = (u8)*cur;
	if (c == '$') {
		cur++;
		*a = -1;
	} else if ((unsigned)(c - '0') <= 9) {
		long v = 0;
		while ((unsigned)((u8)*cur - '0') <= 9)
			v = v * 10 + (*cur++ - '0');
		*a = v;
	} else if (c == '/') {
		cur++;
		*r = re_compile(until('/'), reflags);
		*a = -2;
	}
}

static void substitution(struct cmd *c)
{
	int delim = (u8)*cur++;
	c->re = re_compile(until(delim), reflags);
	c->repl = until(delim);
	for (;;) {
		if (*cur == 'g')
			c->flags |= 1;
		else if (*cur == 'p')
			c->flags |= 2;
		else
			break;
		cur++;
	}
}

static void script(const char *s)
{
	cur = s;
	for (;;) {
		char c = *cur;
		if (!c)
			return;
		if (c == ' ' || c == 10 || c == ';' || c == 9) {
			cur++;
			continue;
		}
		if (ncmds >= CMD_MAX)
			return;
		struct cmd *k = &cmds[ncmds++];
		memset(k, 0, sizeof *k);
		address(&k->a1, &k->a1r);
		if (*cur == ',') {
			cur++;
			address(&k->a2, &k->a2r);
		}
		while (*cur == ' ')
			cur++;
		k->op = (u8)*cur++; /* (comme l'asm : même l'octet nul) */
		if (k->op == 's')
			substitution(k);
	}
}

static int search(struct regex *re, const char *line, const char *start, const char *caps[RE_CAPS])
{
	if (!re) /* (motif trop long : l'asm débordait) */
		return 0;
	return re_search(re, line, start, caps);
}

/* adresse -> 1 si elle désigne cette ligne (adr_ok) */
static int addr_ok(long a, struct regex *r)
{
	const char *caps[RE_CAPS];
	if (a == -1)
		return next_line == 0;
	if (a == -2)
		return search(r, space, space, caps);
	return (u64)a == num;
}

/* (selection) */
static int selected(struct cmd *c)
{
	if (!c->a1)
		return 1;
	if (!c->a2)
		return addr_ok(c->a1, c->a1r);
	if (!c->in) {
		if (!addr_ok(c->a1, c->a1r))
			return 0;
		c->in = 1;
		return 1;
	}
	if (addr_ok(c->a2, c->a2r))
		c->in = 0;
	return 1;
}

static char *copy(char *o, const char *a, const char *b)
{
	while (a < b)
		*o++ = *a++;
	return o;
}

/* s : l'espace de travail refait (substituer) */
static void substitute(struct cmd *c)
{
	const char *caps[RE_CAPS];
	const char *r = space;
	char *o = redo;
	int done = 0;
	while (search(c->re, space, r, caps)) {
		done = 1;
		const char *b = caps[0], *e = caps[1];
		o = copy(o, r, b);
		r = b;
		for (const char *p = c->repl;;) {
			u8 ch = (u8)*p++;
			if (!ch)
				break;
			if (ch == '&') {
				o = copy(o, b, e);
				continue;
			}
			if (ch == '\\') {
				ch = (u8)*p++;
				if (!ch)
					break;
				if ((unsigned)(ch - '0') <= 9) {
					int g = ch - '0';
					if (caps[2 * g])
						o = copy(o, caps[2 * g], caps[2 * g + 1]);
					continue;
				}
				if (ch == 'n')
					ch = 10;
			}
			*o++ = (char)ch;
		}
		r = e;
		if (b == e) { /* motif vide : avance d'un caractère */
			if (!*r)
				break;
			*o++ = *r++;
		}
		if (!(c->flags & 1))
			break;
	}
	while ((*o++ = *r++))
		;
	if (!done)
		return;
	strcpy(space, redo);
	if (c->flags & 2) {
		out_puts(space);
		out_nl();
	}
}

/* -> 1 : ligne à écrire (cycle) */
static int cycle(void)
{
	for (long i = 0; i < ncmds; i++) {
		struct cmd *c = &cmds[i];
		if (!selected(c))
			continue;
		if (c->op == 'd')
			return 0;
		if (c->op == 'p') {
			out_puts(space);
			out_nl();
		} else if (c->op == 'q') {
			if (!quiet) {
				out_puts(space);
				out_nl();
			}
			quit = 1;
			return 0;
		} else if (c->op == 's')
			substitute(c);
	}
	return 1;
}

static void stream(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		return;
	}
	rd_init(&krt_in, fd);
	num = 0;
	char *l = rd_line(&krt_in, 0); /* (une ligne d'avance : pour $) */
	while (l) {
		strcpy(space, l);
		num++;
		next_line = rd_line(&krt_in, 0);
		int w = cycle();
		if (quit)
			break;
		if (w && !quiet) {
			out_puts(space);
			out_nl();
		}
		l = next_line;
	}
	in_close(fd);
}

int main(int argc, char **argv)
{
	int i = 1, given = 0;
	cmds = malloc(CMD_MAX * sizeof *cmds);
	while (i < argc && is_opt(argv[i])) {
		for (const char *o = argv[i++] + 1; *o; o++) {
			if (*o == 'n')
				quiet = 1;
			else if (*o == 'E' || *o == 'r')
				reflags |= RE_E;
			else if (*o == 'e') {
				const char *s = argv[i++];
				if (!s)
					goto usage;
				script(s);
				given = 1;
				break;
			}
		}
	}
	if (!given) {
		if (i >= argc)
			goto usage;
		script(argv[i++]);
	}
	if (i >= argc)
		stream(0);
	for (; i < argc; i++)
		stream(argv[i]);
	return 0;
usage:
	err_msg("usage : sed [-nE] [-e script] script [fichiers]");
	return 1;
}
