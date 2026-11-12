/* ed - l'éditeur de lignes : ed [-p invite] [fichier] (docs/OUTILS.md), comme ed.asm (d'après ed.c
 * d'Onyx). Le texte est une liste de lignes numérotées ; l'une est la ligne courante (« . »).
 *   [adresse[,adresse]]commande[paramètres]
 * Adresses : N . $ +N -N /re/ ?re? 'x ; « , » seul : 1,$ ; « ; » : .,$ (expressions de
 * src/lib/korinrt/regex.c, syntaxe de base). Commandes : a i c d j p n l s m t k g v r w W e E f = u
 * q Q h H, wq ; une adresse seule ou une ligne vide : aller à la ligne et l'afficher. Erreur : « ? »
 * (et la raison après H). Après un changement, q et e demandent une fois. Code de sortie : 0.
 *
 * Comme l'asm : seul « -p » en premier mot donne l'invite (et seulement s'il est suivi d'un mot) ;
 * seul le premier fichier compte ; q sous g est sans effet ; s sans / final affiche la ligne ; « j »
 * sur une seule ligne ne change rien mais compte pour u ; r garde le nom du texte (sauf s'il n'en
 * avait pas) ; « m » vers une ligne du bloc (sauf la dernière) : « destination invalide » ; un nom
 * de fichier garde 255 octets, sans ses blancs de la fin ; H n'affiche pas la dernière erreur. */
#include "korinrt.h"

static char **ln; /* les lignes, ln[1..n] */
static long n, cap, cur_line, dirty, warned, verbose;
static const char *errmsg, *prompt;
static struct regex *last_re, *s_re;
static const char *s_repl;
static long s_g, s_nth, s_p;
static char **u_ln;
static long u_n, u_cur, u_ok;
static char *marks[26];
static char file[256], keep[256];
static char redo[2 * T_LINE];

static const char E_MARK[] = "marque invalide";
static const char E_GONE[] = "la ligne marquée n'existe plus";
static const char E_NOPAT[] = "pas de motif précédent";
static const char E_NOMATCH[] = "aucune correspondance";
static const char E_ADDR[] = "adresse invalide";
static const char E_DELIM[] = "délimiteur invalide";
static const char E_UNTERM[] = "commande s non terminée";
static const char E_SFLAG[] = "drapeau de s invalide";
static const char E_NOPREV[] = "pas de substitution précédente";
static const char E_DEST[] = "destination invalide";
static const char E_GG[] = "g dans g";
static const char E_NOFILE[] = "pas de nom de fichier";
static const char E_OPEN[] = "fichier illisible";
static const char E_WRITE[] = "écriture impossible";
static const char E_UNSAVED_E[] = "texte non enregistré (e encore pour l'abandonner)";
static const char E_UNSAVED_Q[] = "texte non enregistré (q encore pour quitter)";
static const char E_UNDO[] = "rien à annuler";
static const char E_NONE[] = "pas d'erreur";
static const char E_UNKNOWN[] = "commande inconnue";

static int command(char *s, int under_g);

/* ---- le texte */

/* de la place pour ln[0..k+1] */
static void reserve(long k)
{
	long need = k + 2;
	if ((u64)need <= (u64)cap)
		return;
	long c = need * 2 + 64;
	char **v = malloc((u64)c * 8);
	if (ln)
		for (long i = 0; i <= n + 1; i++)
			v[i] = ln[i];
	ln = v;
	cap = c;
}

static void insert(long after, char *text)
{
	reserve(n + 1);
	for (long i = n; (u64)i > (u64)after; i--)
		ln[i + 1] = ln[i];
	ln[after + 1] = text;
	n++;
}

static void delete(long a, long b)
{
	long k = b - a + 1;
	for (long i = b + 1; (u64)i <= (u64)n; i++)
		ln[i - k] = ln[i];
	n -= k;
}

/* le texte d'avant la commande (u) : les lignes sont partagées, jamais changées sur place */
static void snapshot(void)
{
	char **v = malloc((u64)(n + 2) * 8);
	for (long i = 1; (u64)i <= (u64)n; i++)
		v[i] = ln[i];
	u_ln = v;
	u_n = n;
	u_cur = cur_line;
	u_ok = 1;
}

static char *strndup_(const char *s, u64 len)
{
	char *d = malloc(len + 1);
	for (u64 i = 0; i < len; i++)
		d[i] = s[i];
	d[len] = 0;
	return d;
}

/* chemin -> contenu (un 0 après), *len octets ; 0 : illisible ou dossier */
static char *slurp(const char *path, u64 *len)
{
	long st[2];
	if (sys_stat(path, st) < 0 || (st[1] & ATTR_DIR))
		return 0;
	u64 size = (u64)st[0];
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	char *buf = malloc(size + 1);
	if (!buf) {
		sys_close(fd);
		return 0;
	}
	u64 got = 0;
	while (got != size) {
		long r = sys_read(fd, buf + got, size - got);
		if (r <= 0)
			break;
		got += (u64)r;
	}
	buf[got] = 0;
	sys_close(fd);
	*len = got;
	return buf;
}

/* texte (les fins de ligne deviennent des 0), octets -> lignes, *count */
static char **split(char *t, u64 len, long *count)
{
	u64 c = 0;
	for (u64 i = 0; i < len; i++)
		if (t[i] == 10)
			c++;
	if (len && t[len - 1] != 10)
		c++;
	char **v = malloc((c + 1) * 8);
	u64 k = 0;
	char *s = t;
	for (u64 i = 0; i < len; i++)
		if (t[i] == 10) {
			t[i] = 0;
			v[k++] = s;
			s = t + i + 1;
		}
	if (k < c)
		v[k] = s;
	*count = (long)c;
	return v;
}

/* chemin, après la ligne -> octets lus, -1 : illisible */
static long read_file(const char *path, long after)
{
	u64 len;
	char *t = slurp(path, &len);
	if (!t)
		return -1;
	long k;
	char **v = split(t, len, &k);
	reserve(n + k);
	for (long i = n; (u64)i > (u64)after; i--)
		ln[i + k] = ln[i];
	for (long i = 0; i < k; i++)
		ln[after + 1 + i] = v[i];
	n += k;
	cur_line = after + k;
	return (long)len;
}

/* chemin, première, dernière ligne, 1 : ajouter -> octets écrits, -1 : impossible */
static long write_file(const char *path, long a, long b, int append)
{
	u64 total = 0;
	for (long i = a; i <= b; i++)
		total += (u64)strlen(ln[i]) + 1;
	char *buf = malloc(total + 1);
	if (!buf)
		return -1;
	u64 k = 0;
	for (long i = a; i <= b; i++) {
		for (const char *p = ln[i]; *p; p++)
			buf[k++] = *p;
		buf[k++] = 10;
	}
	long fd = sys_open(path, append ? O_WRITE | O_CREATE | O_APPEND : O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return -1;
	u64 done = 0;
	while (done != total) {
		u64 c = total - done;
		if (c > 65536)
			c = 65536;
		long r = sys_write(fd, buf + done, c);
		if (r <= 0) {
			sys_close(fd);
			return -1;
		}
		done += (u64)r;
	}
	sys_close(fd);
	return (long)total;
}

/* ---- petits outils */

static char *skipb(char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	return s;
}

/* texte (sur un chiffre) -> nombre, *end après */
static long parse_num(char *s, char **end)
{
	long v = 0;
	while ((unsigned)((u8)*s - '0') <= 9)
		v = v * 10 + (*s++ - '0');
	*end = s;
	return v;
}

/* texte après la commande -> file (le nom donné, ou celui d'avant) */
static char *filename(const char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	if (!*s)
		return file;
	long k = 0;
	while (s[k] && k < 255) {
		file[k] = s[k];
		k++;
	}
	while (k && (file[k - 1] == ' ' || file[k - 1] == 9))
		k--;
	file[k] = 0;
	return file;
}

/* raison : « ? », la raison si H -> -1 */
static int fail(const char *why)
{
	errmsg = why;
	out_puts("?\n");
	if (verbose) {
		out_say(why);
		out_nl();
	}
	return -1;
}

static int search_re(struct regex *re, const char *line, const char *start, const char *caps[RE_CAPS])
{
	if (!re) /* (motif trop long : l'asm débordait) */
		return 0;
	return re_search(re, line, start, caps);
}

static int matches(struct regex *re, const char *line)
{
	const char *caps[RE_CAPS];
	return search_re(re, line, line, caps);
}

/* texte, délimiteur -> morceau (copie ; \délim -> délim), *end après, *closed 1 si fermé */
static char *re_piece(char *s, int delim, char **end, int *closed)
{
	char *d = malloc((u64)strlen(s) + 1), *o = d;
	*closed = 0;
	for (;;) {
		u8 c = (u8)*s;
		if (!c)
			break;
		s++;
		if (c == delim) {
			*closed = 1;
			break;
		}
		if (c == '\\') {
			u8 c2 = (u8)*s;
			if (c2) {
				s++;
				if (c2 != delim)
					*o++ = '\\';
				*o++ = (char)c2;
				continue;
			}
		}
		*o++ = (char)c;
	}
	*o = 0;
	*end = s;
	return d;
}

/* programme, 1 : en arrière -> la ligne, 0 : aucune (en bouclant) */
static long search(struct regex *re, int back)
{
	long i = cur_line;
	for (long k = 0; (u64)k < (u64)n; k++) {
		if (back) {
			if (--i < 1)
				i = n;
		} else if (++i > n)
			i = 1;
		if (matches(re, ln[i]))
			return i;
	}
	return 0;
}

/* ---- adresses */

/* texte -> 1 (*val), 0 : aucune, -1 : erreur (dite) ; *end après */
static int address(char *s, char **end, long *val)
{
	long a = 0;
	int have = 0;
	s = skipb(s);
	u8 c = (u8)*s;
	if ((unsigned)(c - '0') <= 9) {
		a = parse_num(s, &s);
		have = 1;
	} else if (c == '.') {
		a = cur_line;
		s++;
		have = 1;
	} else if (c == '$') {
		a = n;
		s++;
		have = 1;
	} else if (c == '\'') {
		unsigned m = (u8)s[1] - 'a';
		if (m > 25 || !marks[m]) {
			*end = s;
			return fail(E_MARK);
		}
		for (a = 1; (u64)a <= (u64)n && ln[a] != marks[m]; a++)
			;
		if ((u64)a > (u64)n) {
			*end = s;
			return fail(E_GONE);
		}
		s += 2;
		have = 1;
	} else if (c == '/' || c == '?') {
		int closed;
		char *p = re_piece(s + 1, c, &s, &closed);
		if (p[0])
			last_re = re_compile(p, 0);
		else if (!last_re) {
			*end = s;
			return fail(E_NOPAT);
		}
		a = search(last_re, c == '?');
		if (!a) {
			*end = s;
			return fail(E_NOMATCH);
		}
		have = 1;
	}
	for (;;) { /* +N -N + - */
		s = skipb(s);
		c = (u8)*s;
		if (c != '+' && c != '-')
			break;
		s++;
		if (!have) {
			a = cur_line;
			have = 1;
		}
		long k = 1;
		if ((unsigned)((u8)*s - '0') <= 9)
			k = parse_num(s, &s);
		a = c == '-' ? a - k : a + k;
	}
	*end = s;
	if (!have) {
		*val = 0;
		return 0;
	}
	if (a < 0 || a > n)
		return fail(E_ADDR);
	*val = a;
	return 1;
}

/* ---- commandes */

/* première, dernière ligne, 'p' 'n' ou 'l' ; la dernière devient la courante */
static void print(long a, long b, int mode)
{
	for (long i = a; i <= b; i++) {
		if (mode == 'n') {
			out_putn((u64)i);
			out_putc(9);
		}
		const u8 *p = (const u8 *)ln[i];
		if (mode != 'l')
			out_puts((const char *)p);
		else {
			for (u8 c; (c = *p++);) {
				if (c == 9) {
					out_putc('\\');
					out_putc('t');
				} else if (c == '\\') {
					out_putc('\\');
					out_putc('\\');
				} else {
					if (c < 32) {
						out_putc('^');
						c += 64;
					}
					out_putc(c);
				}
			}
			out_putc('$');
		}
		out_nl();
	}
	cur_line = b;
}

/* après la ligne : les lignes tapées jusqu'à « . » -> combien */
static long input_lines(long after)
{
	long k = 0;
	for (;;) {
		out_flush();
		u64 len;
		char *l = rd_line(&krt_in, &len);
		if (!l || (len == 1 && l[0] == '.'))
			break;
		insert(after + k, strndup_(l, len));
		k++;
	}
	cur_line = after + k;
	return k;
}

static char *copy(char *o, const char *a, const char *b)
{
	while (a < b)
		*o++ = *a++;
	return o;
}

/* ligne -> 1 si changée (le résultat dans redo) */
static int subst_line(const char *line)
{
	const char *caps[RE_CAPS];
	const char *r = line;
	char *o = redo;
	int changed = 0;
	long count = 0;
	while (search_re(s_re, line, r, caps)) {
		count++;
		const char *b = caps[0], *e = caps[1];
		o = copy(o, r, b);
		r = b;
		if (count == s_nth || ((u64)count > (u64)s_nth && s_g)) {
			changed = 1;
			for (const char *p = s_repl;;) {
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
		} else
			o = copy(o, b, e); /* (pas celle-ci : telle quelle) */
		r = e;
		if (b == e) { /* correspondance vide : avance d'un caractère */
			if (!*r)
				break;
			*o++ = *r++;
		}
		if (!s_g && (u64)count >= (u64)s_nth)
			break;
	}
	while ((*o++ = *r++))
		;
	return changed;
}

/* texte après « s », première, dernière ligne -> 0, -1 */
static int substitute(char *s, long a, long b)
{
	u8 c = (u8)*s;
	if (!c || c == 10) {
		if (!s_re)
			return fail(E_NOPREV);
	} else {
		s++;
		if (c == ' ' || c == '\\')
			return fail(E_DELIM);
		int closed, closed2;
		char *pat = re_piece(s, c, &s, &closed);
		if (!closed)
			return fail(E_UNTERM);
		char *repl = re_piece(s, c, &s, &closed2);
		if (pat[0])
			last_re = re_compile(pat, 0);
		else if (!last_re)
			return fail(E_NOPAT);
		s_re = last_re;
		s_repl = repl;
		s_g = 0;
		s_p = 0;
		s_nth = 1;
		for (;;) {
			u8 f = (u8)*s;
			if (!f)
				break;
			if (f == 'g') {
				s_g = 1;
				s++;
			} else if (f == 'p') {
				s_p = 1;
				s++;
			} else if ((unsigned)(f - '0') <= 9)
				s_nth = parse_num(s, &s);
			else
				return fail(E_SFLAG);
		}
		if (!closed2) /* (s/a/b sans / final : affiche, comme ed) */
			s_p = 1;
	}
	int any = 0;
	for (long i = a; i <= b; i++) {
		if (!subst_line(ln[i]))
			continue;
		delete(i, i);
		long at = i - 1; /* (un \n du remplacement coupe la ligne) */
		const char *p = redo;
		for (;;) {
			const char *q = p;
			while (*q && *q != 10)
				q++;
			insert(at, strndup_(p, (u64)(q - p)));
			at++;
			if (!*q)
				break;
			p = q + 1;
			b++;
		}
		i = at;
		cur_line = at;
		any = 1;
	}
	if (!any)
		return fail(E_NOMATCH);
	dirty = 1;
	if (s_p)
		print(cur_line, cur_line, 'p');
	return 0;
}

/* g/re/cmd, v/re/cmd : texte après g, première, dernière ligne, 1 pour v -> 0, -1. Les lignes
 * choisies sont notées d'abord, puis cmd tourne sur chacune encore là. */
static int global_cmd(char *s, long a, long b, int inv)
{
	u8 c = (u8)*s;
	if (!c || c == ' ')
		return fail(E_DELIM);
	int closed;
	char *p = re_piece(s + 1, c, &s, &closed);
	if (p[0])
		last_re = re_compile(p, 0);
	else if (!last_re)
		return fail(E_NOPAT);
	if (!*s) {
		static char pcmd[] = "p";
		s = pcmd;
	}
	char **sel = malloc((u64)(b - a + 2) * 8);
	long k = 0;
	for (long i = a; i <= b; i++)
		if (matches(last_re, ln[i]) != inv)
			sel[k++] = ln[i];
	int r = 0;
	for (long j = 0; j < k && !r; j++) {
		long i;
		for (i = 1; (u64)i <= (u64)n && ln[i] != sel[j]; i++)
			;
		if ((u64)i > (u64)n)
			continue;
		cur_line = i;
		if (command(s, 1) < 0)
			r = -1;
	}
	return r;
}

/* une ligne de commande, 1 sous g -> 0, -1 erreur (dite), 1 quitter */
static int command(char *s, int under_g)
{
	long a = 0, b = 0, v;
	int given = 0, r;
	s = skipb(s);
	if (*s == ',' || *s == ';') {
		a = *s == ',' ? 1 : cur_line; /* , = 1,$   ; = .,$ */
		b = n;
		if (!b)
			a = 0;
		given = 2;
		r = address(s + 1, &s, &v);
		if (r < 0)
			return -1;
		if (r)
			b = v;
	} else {
		r = address(s, &s, &v);
		if (r < 0)
			return -1;
		if (r) {
			a = b = v;
			given = 1;
			s = skipb(s);
			if (*s == ',' || *s == ';') {
				if (*s == ';')
					cur_line = a;
				given = 2;
				r = address(s + 1, &s, &v);
				if (r < 0)
					return -1;
				b = n;
				if (r)
					b = v;
			}
		}
	}
	if (given == 2 && a > b)
		return fail(E_ADDR);
	s = skipb(s);
	int cmd = (u8)*s;
	if (cmd)
		s++;
	if (!under_g && cmd && strchr("aicdjsmtgvr", cmd)) /* (une commande qui change le texte : u l'annule) */
		snapshot();
	if (cmd != 'q' && cmd != 'e')
		warned = 0;
#define DEFAULTS(d1, d2)                                                                           \
	do {                                                                                       \
		long d1_ = (d1), d2_ = (d2);                                                       \
		if (!given) {                                                                      \
			a = d1_;                                                                   \
			b = d2_;                                                                   \
		}                                                                                  \
	} while (0)
#define NEED()                                                                                     \
	do {                                                                                       \
		if (a < 1 || b > n || a > b)                                                       \
			return fail(E_ADDR);                                                       \
	} while (0)
	switch (cmd) {
	case 0: /* une adresse seule, une ligne vide */
		if (!given)
			b = cur_line + 1;
		if (b < 1 || b > n)
			return fail(E_ADDR);
		print(b, b, 'p');
		return 0;
	case 'a':
		DEFAULTS(cur_line, cur_line);
		if (input_lines(b))
			dirty = 1;
		return 0;
	case 'i':
		DEFAULTS(cur_line, cur_line);
		if (input_lines(b - 1 < 0 ? 0 : b - 1))
			dirty = 1;
		return 0;
	case 'c':
		DEFAULTS(cur_line, cur_line);
		NEED();
		delete(a, b);
		dirty = 1;
		input_lines(a - 1);
		return 0;
	case 'd':
		DEFAULTS(cur_line, cur_line);
		NEED();
		delete(a, b);
		dirty = 1;
		cur_line = a <= n ? a : n;
		return 0;
	case 'j': {
		DEFAULTS(cur_line, cur_line + 1);
		NEED();
		if (a == b)
			return 0;
		u64 len = 0;
		for (long i = a; i <= b; i++)
			len += (u64)strlen(ln[i]);
		char *j = malloc(len + 1), *o = j;
		for (long i = a; i <= b; i++)
			for (const char *p = ln[i]; *p; p++)
				*o++ = *p;
		*o = 0;
		delete(a, b);
		insert(a - 1, j);
		cur_line = a;
		dirty = 1;
		return 0;
	}
	case 'p':
	case 'n':
	case 'l':
		DEFAULTS(cur_line, cur_line);
		NEED();
		print(a, b, cmd);
		return 0;
	case 's':
		DEFAULTS(cur_line, cur_line);
		NEED();
		return substitute(s, a, b);
	case 'm':
	case 't': {
		DEFAULTS(cur_line, cur_line);
		NEED();
		r = address(s, &s, &v);
		if (r < 0)
			return -1;
		long dest = r ? v : cur_line;
		long k = b - a + 1;
		char **blk = malloc((u64)k * 8);
		for (long i = 0; (u64)i < (u64)k; i++)
			blk[i] = cmd == 't' ? strdup(ln[a + i]) : ln[a + i];
		if (cmd == 'm') {
			if (dest >= a && dest < b)
				return fail(E_DEST);
			delete(a, b);
			if (dest >= b)
				dest -= k;
		}
		for (long i = 0; (u64)i < (u64)k; i++)
			insert(dest + i, blk[i]);
		cur_line = dest + k;
		dirty = 1;
		return 0;
	}
	case 'k': {
		DEFAULTS(cur_line, cur_line);
		NEED();
		unsigned m = (u8)*s - 'a';
		if (m > 25)
			return fail(E_MARK);
		marks[m] = ln[b];
		return 0;
	}
	case 'g':
	case 'v':
		if (under_g)
			return fail(E_GG);
		DEFAULTS(1, n);
		if (!n)
			return 0;
		NEED();
		return global_cmd(s, a, b, cmd == 'v');
	case 'r': {
		DEFAULTS(n, n);
		strcpy(keep, file); /* (r ne renomme pas le texte) */
		char *f = filename(s);
		if (!f[0])
			return fail(E_NOFILE);
		long got = read_file(f, b);
		if (keep[0])
			strcpy(file, keep);
		if (got < 0)
			return fail(E_OPEN);
		out_putn((u64)got);
		out_nl();
		dirty = 1;
		return 0;
	}
	case 'w':
	case 'W': {
		int wq = 0;
		if (*s == 'q') {
			wq = 1;
			s++;
		}
		DEFAULTS(1, n);
		if (n)
			NEED();
		char *f = filename(s);
		if (!f[0])
			return fail(E_NOFILE);
		long put = write_file(f, a, n ? b : 0, cmd == 'W');
		if (put < 0)
			return fail(E_WRITE);
		out_putn((u64)put);
		out_nl();
		if (!given)
			dirty = 0;
		return wq;
	}
	case 'e':
	case 'E': {
		if (cmd == 'e' && dirty && !warned) {
			warned = 1;
			return fail(E_UNSAVED_E);
		}
		char *f = filename(s);
		if (!f[0])
			return fail(E_NOFILE);
		n = 0;
		cur_line = 0;
		dirty = 0;
		u_ok = 0;
		long got = read_file(f, 0);
		if (got < 0)
			return fail(E_OPEN);
		out_putn((u64)got);
		out_nl();
		return 0;
	}
	case 'f':
		out_puts(filename(s));
		out_nl();
		return 0;
	case '=':
		DEFAULTS(n, n);
		out_putn((u64)b);
		out_nl();
		return 0;
	case 'u': {
		if (!u_ok)
			return fail(E_UNDO);
		/* le texte d'avant, et celui-ci pour u encore */
		char **t = malloc((u64)(n + 2) * 8);
		for (long i = 1; (u64)i <= (u64)n; i++)
			t[i] = ln[i];
		long old_n = n;
		reserve(u_n);
		for (long i = 1; (u64)i <= (u64)u_n; i++)
			ln[i] = u_ln[i];
		n = u_n;
		long c = cur_line;
		cur_line = u_cur;
		u_ln = t;
		u_n = old_n;
		u_cur = c;
		dirty = 1;
		return 0;
	}
	case 'q':
		if (dirty && !warned) {
			warned = 1;
			return fail(E_UNSAVED_Q);
		}
		return 1;
	case 'Q':
		return 1;
	case 'h':
		out_say(errmsg && errmsg[0] ? errmsg : E_NONE);
		out_nl();
		return 0;
	case 'H':
		verbose ^= 1;
		return 0;
	}
	return fail(E_UNKNOWN);
}

int main(int argc, char **argv)
{
	int i = 1;
	prompt = "";
	errmsg = "";
	s_nth = 1;
	if (argc >= 3 && str_eq(argv[1], "-p")) {
		prompt = argv[2];
		i = 3;
	}
	reserve(0);
	if (i < argc) {
		char *f = filename(argv[i]);
		long got = read_file(f, 0);
		if (got < 0) {
			out_puts(file);
			out_say(" : nouveau fichier\n");
		} else {
			out_putn((u64)got);
			out_nl();
		}
	}
	rd_init(&krt_in, 0);
	for (;;) {
		out_puts(prompt);
		out_flush();
		u64 len;
		char *l = rd_line(&krt_in, &len);
		if (!l)
			break;
		/* (les commandes a, i, c lisent d'autres lignes) */
		if (command(strndup_(l, len), 0) > 0)
			break;
		out_flush();
	}
	return 0;
}
