/* sh_word.c - sh : variables, développement des mots, motifs (docs/SHELL.md), comme sh_word.inc.
 *
 * Un mot est développé dans l'arène (ar_p) : guillemets, $VAR, ${VAR...}, $(...), `...`, $((...)).
 * Un caractère spécial venu d'un texte cité garde devant lui l'octet ESC, pour que le découpage
 * et les motifs ne le voient pas ; il est retiré à la fin. */
#include "sh.h"

long we_mode, we_have; /* mot_etendre : x19, x22 */
char *we_w, *we_fs;    /* x20, x21 */

/* ---------------------------------------------------------------------------
 *  Variables
 * --------------------------------------------------------------------------- */

/* nom, longueur -> entrée, 0 si absente (var_cherche) */
struct var *var_find(const char *name, long len)
{
	struct var *e = vars;
	for (long n = vars_n; n; n--, e++) {
		const char *t = e->text;
		if (!t)
			continue;
		long i;
		for (i = 0; i < len; i++)
			if (t[i] != name[i])
				break;
		if (i == len && t[len] == '=')
			return e;
	}
	return 0;
}

/* nom, longueur -> valeur, 0 si absente (var_valeur) */
char *var_value(const char *name, long len)
{
	struct var *e = var_find(name, len);
	return e ? e->text + len + 1 : 0;
}

/* nom, longueur, valeur, 1 : exportée (0 : comme avant) (var_pose) ; le nouveau texte n'est
 * jamais rendu (peu de variables changent), comme l'asm */
void var_set(const char *name, long len, const char *val, long exp)
{
	struct var *e = var_find(name, len);
	if (!e) {
		if (vars_n >= VARS_MAX)
			return;
		e = &vars[vars_n++];
		e->flag = 0;
	}
	e->flag |= exp;
	char *d = varm_p;
	e->text = d;
	for (long i = 0; i < len; i++)
		*d++ = name[i];
	*d++ = '=';
	if (val)
		while (*val)
			*d++ = *val++;
	*d++ = 0;
	varm_p = d;
}

/* « NOM=VALEUR », drapeau d'exportation (var_pose_kv) ; sans « = » : rien */
void var_set_kv(const char *kv, long exp)
{
	const char *e = kv;
	while (*e && *e != '=')
		e++;
	if (!*e)
		return;
	var_set(kv, e - kv, e + 1, exp);
}

/* nom (chaîne) : retirée (var_retire) */
void var_unset(const char *name)
{
	struct var *e = var_find(name, str_len(name));
	if (e)
		e->text = 0;
}

/* tout (1 : aussi celles qui ne sont pas exportées) -> tableau sur at_p, terminé par 0
 * (env_tableau) */
char **env_array(long all)
{
	char **a = at_p, **d = at_p;
	struct var *e = vars;
	for (long n = vars_n; n; n--, e++)
		if (e->text && (e->flag | all))
			*d++ = e->text;
	*d++ = 0;
	at_p = d;
	return a;
}

/* ---------------------------------------------------------------------------
 *  Mots
 * --------------------------------------------------------------------------- */

/* caractère -> 1 s'il termine un mot (fin_mot) */
int word_end(int c)
{
	switch (c) {
	case 0: case ' ': case 9: case 10: case 13: case ';': case '&': case '|': case '<': case '>':
	case '(': case ')':
		return 1;
	}
	return 0;
}

/* le texte finit au milieu d'une construction : en interactif, une ligne de plus (texte_coupe) */
void text_cut(void)
{
	incomplete = 1;
}

static void skip_dq(void);

/* P juste après « ( » -> juste après la « ) » correspondante (sauter_parens) */
void skip_parens(void)
{
	long depth = 1;
	for (;;) {
		char c = *P;
		if (!c) {
			text_cut();
			return;
		}
		P++;
		if (c == '(')
			depth++;
		else if (c == ')') {
			if (!--depth)
				return;
		} else if (c == '\\') {
			if (*P)
				P++;
		} else if (c == '\'') {
			for (;;) {
				c = *P;
				if (!c) {
					text_cut();
					return;
				}
				P++;
				if (c == '\'')
					break;
			}
		} else if (c == '"')
			skip_dq();
	}
}

/* P juste après un « " » -> juste après le « " » fermant (sauter_dq) */
static void skip_dq(void)
{
	for (;;) {
		char c = *P;
		if (!c) {
			text_cut();
			return;
		}
		P++;
		if (c == '"')
			return;
		if (c == '\\') {
			if (*P)
				P++;
		} else if (c == '$') {
			if (*P == '(') {
				P++;
				skip_parens();
			}
		} else if (c == '`') {
			for (;;) {
				c = *P;
				if (!c) {
					text_cut();
					return;
				}
				P++;
				if (c == '`')
					break;
			}
		}
	}
}

/* jusqu'au caractère c (compris) ; fin du texte : coupé -> 0 */
static int skip_to(char end)
{
	for (;;) {
		char c = *P;
		if (!c) {
			text_cut();
			return 0;
		}
		P++;
		if (c == end)
			return 1;
	}
}

/* P sur un mot : le passe, sans rien développer (mot_sauter) */
void word_skip(void)
{
	for (;;) {
		char c = *P;
		if (word_end(c))
			return;
		P++;
		if (c == '\\') {
			if (!*P)
				return;
			P++;
		} else if (c == '\'') {
			if (!skip_to('\''))
				return;
		} else if (c == '"')
			skip_dq();
		else if (c == '$') {
			if (*P == '(') {
				P++;
				skip_parens();
			} else if (*P == '{') {
				if (!skip_to('}'))
					return;
			}
		} else if (c == '`') {
			if (!skip_to('`'))
				return;
		}
	}
}

/* caractère : posé ; venu d'un texte cité (pose_cite) */
static void put_quoted(int c)
{
	if (c == '*' || c == '?' || c == '[' || c == ']' || c == ESC)
		*we_w++ = ESC;
	*we_w++ = (char)c;
}

/* chaîne : les ESC retirés, sur place (sans_esc) */
static void no_esc(char *s)
{
	char *r = s;
	for (;;) {
		char c = *r++;
		if (c == ESC)
			c = *r++;
		*s++ = c;
		if (!c)
			return;
	}
}

void at_push(char *p)
{
	*at_p++ = p;
}

/* champ -> 1 s'il contient * ? [ hors citation (a_motif) */
static int has_pattern(const char *s)
{
	for (;;) {
		char c = *s++;
		if (!c)
			return 0;
		if (c == ESC) {
			if (!*s++)
				return 0;
			continue;
		}
		if (c == '*' || c == '?' || c == '[')
			return 1;
	}
}

static int glob_expand(void);

/* (dans word_expand, mode 0) le champ [we_fs, we_w[ : motif développé ou posé ; champ suivant
 * (finir_champ) */
static void field_end(void)
{
	*we_w++ = 0;
	if (!has_pattern(we_fs) || !glob_expand()) {
		no_esc(we_fs);
		at_push(we_fs);
	}
	we_fs = we_w;
	we_have = 0;
}

/* valeur, 1 : entre guillemets ; dans word_expand : posée, découpée aux blancs sinon
 * (pose_valeur) */
void put_value(const char *v, long quoted)
{
	if (!v)
		return;
	for (;;) {
		char c = *v++;
		if (!c)
			return;
		if (quoted) {
			put_quoted((u8)c);
			continue;
		}
		if (we_mode || (c != ' ' && c != 9 && c != 10)) { /* (une chaîne : pas de découpage) */
			*we_w++ = c;
			continue;
		}
		if (we_w == we_fs && !we_have)
			continue;
		field_end();
	}
}

/* P : passe un nom de variable (lettres, chiffres, _) (nom_passer) */
void name_skip(void)
{
	for (;;) {
		u8 c = (u8)*P;
		if (c != '_' && (u8)(c - '0') > 9 && (u8)((c | 0x20) - 'a') > 25)
			return;
		P++;
	}
}

/* code du système -> code du shell, de 0 à 255 (-9 : 137...) (code_shell) */
long shell_code(long c)
{
	if (c < 0)
		c = -c + 128;
	return c & 255;
}

/* nom, longueur -> valeur (positionnels et spéciaux compris), 0 si absente (val_nommee) */
static char *named_value(const char *name, long len)
{
	if (len == 1) {
		if (name[0] == '?') {
			fmt_dec((u64)shell_code(last_code), tmp_num);
			return tmp_num;
		}
		u64 d = (u8)(name[0] - '0');
		if (d <= 9)
			return d > (u64)pos_n ? 0 : pos_v[d];
	}
	return var_value(name, len);
}

static void dollar(long quoted);

/* P après l'opérateur de ${VAR:-mot} : le mot, développé, jusqu'à « } » (acc_mot) */
static void brace_word(long quoted)
{
	for (;;) {
		char c = *P;
		if (!c) {
			text_cut();
			return;
		}
		P++;
		if (c == '}')
			return;
		if (c == '"' || c == '\'')
			continue;
		if (c == '$') {
			P--;
			dollar(quoted);
			continue;
		}
		if (!quoted)
			*we_w++ = c;
		else
			put_quoted((u8)c);
	}
}

/* P après l'opérateur : jusqu'après le « } » (acc_sauter) */
static void brace_skip(void)
{
	long depth = 1;
	for (;;) {
		char c = *P;
		if (!c) {
			text_cut();
			return;
		}
		P++;
		if (c == '{')
			depth++;
		else if (c == '}' && !--depth)
			return;
	}
}

/* P après un « ` », entre guillemets (guillemet_arriere) */
static void backquote(long quoted)
{
	const char *s = P;
	for (;;) {
		char c = *P;
		if (!c) {
			text_cut();
			return;
		}
		P++;
		if (c == '`') {
			capture(s, P - 1, quoted);
			return;
		}
	}
}

/* ---------------------------------------------------------------------------
 *  $(( expression )) : + - * / % ( ) et variables
 * --------------------------------------------------------------------------- */

static void skip_blank_arith(void)
{
	while (*P == ' ' || *P == 9 || *P == 10)
		P++;
}

static long arith(void);

static long arith_base(void)
{
	skip_blank_arith();
	char c = *P;
	if (c == '(') {
		P++;
		long v = arith();
		skip_blank_arith();
		if (*P == ')')
			P++;
		return v;
	}
	if (c == '-') {
		P++;
		return -arith_base();
	}
	if (c == '$')
		P++;
	const char *s = P;
	name_skip();
	if (P == s)
		return 0;
	if ((u8)(s[0] - '0') <= 9)
		return signed_dec(s);
	const char *v = named_value(s, P - s);
	return v ? signed_dec(v) : 0;
}

/* sdiv, msub d'AArch64 : INT64_MIN / -1 donne INT64_MIN (pas de faute) */
static long sdiv(long a, long b)
{
	if (b == -1)
		return (long)(0 - (u64)a);
	return a / b;
}

static long arith_term(void)
{
	long v = arith_base();
	for (;;) {
		skip_blank_arith();
		char op = *P;
		if (op != '*' && op != '/' && op != '%')
			return v;
		P++;
		long b = arith_base();
		if (op == '*')
			v = (long)((u64)v * (u64)b);
		else if (b) { /* (division par zéro : ignorée) */
			long q = sdiv(v, b);
			v = op == '/' ? q : (long)((u64)v - (u64)q * (u64)b);
		}
	}
}

static long arith(void)
{
	long v = arith_term();
	for (;;) {
		skip_blank_arith();
		char op = *P;
		if (op != '+' && op != '-')
			return v;
		P++;
		long b = arith_term();
		v = op == '+' ? (long)((u64)v + (u64)b) : (long)((u64)v - (u64)b);
	}
}

/* P sur « $ », 1 : entre guillemets (dollar) */
static void dollar(long quoted)
{
	char buf[32];
	char c = P[1];
	if (c == '(') {
		if (P[2] == '(') { /* $(( ... )) */
			P += 3;
			long v = arith();
			char *b = buf;
			if (v < 0) {
				v = -v;
				*b++ = '-';
			}
			fmt_dec((u64)v, b);
			skip_blank_arith();
			if (*P == ')')
				P++;
			if (*P == ')')
				P++;
			put_value(buf, quoted);
			return;
		}
		P += 2;
		const char *s = P;
		skip_parens();
		capture(s, P - 1, quoted);
		return;
	}
	if (c == '{') {
		P += 2;
		const char *nm = P; /* le nom */
		if (*P == '#') { /* ${#VAR} */
			P++;
			nm = P;
			name_skip();
			if (*P != '}') {
				text_cut();
				return;
			}
			const char *v = named_value(nm, P - nm);
			P++;
			fmt_dec(v ? (u64)str_len(v) : 0, buf);
			put_value(buf, quoted);
			return;
		}
		name_skip();
		if (P == nm) /* ${?}, ${@}... (comme l'asm : ${12} est la variable « 12 ») */
			P++;
		long len = P - nm;
		char op = *P;
		if (op == '}') {
			P++;
			put_value(named_value(nm, len), quoted);
			return;
		}
		int colon = 0; /* « : » : vide compte comme absent */
		if (op == ':') {
			colon = 1;
			P++;
			op = *P;
		}
		P++;
		const char *v = named_value(nm, len); /* (0 : absente) */
		int present = v && !(colon && !*v);
		if (present ? op == '+' : op != '+') { /* le mot qui suit l'opérateur */
			brace_word(quoted);
			if (op == '=') { /* ${VAR:=x} (comme l'asm : tout le champ jusque-là) */
				*we_w = 0;
				var_set(nm, len, we_fs, 0);
			}
			return;
		}
		brace_skip();
		put_value(op == '+' ? 0 : v, quoted);
		return;
	}
	if (c == '?') {
		P += 2;
		fmt_dec((u64)shell_code(last_code), buf);
		put_value(buf, quoted);
		return;
	}
	if (c == '@' || c == '*') {
		P += 2;
		long n = pos_n;
		for (long i = 1;;) {
			if (i > n)
				return;
			put_value(pos_v[i], quoted);
			i++;
			if (i > n)
				return;
			if (!quoted) {
				if (we_w == we_fs)
					continue;
				if (!we_mode) {
					field_end();
					continue;
				}
			} else if (c == '@') { /* "$@" : un champ par paramètre (comme l'asm, en chaîne aussi) */
				we_have = 1;
				field_end();
				we_have = 1;
				continue;
			}
			*we_w++ = ' ';
		}
	}
	if (c == '#') {
		P += 2;
		fmt_dec((u64)pos_n, buf);
		put_value(buf, quoted);
		return;
	}
	if (c == '$') {
		P += 2;
		fmt_dec((u64)sys_getpid(), buf);
		put_value(buf, quoted);
		return;
	}
	P++;
	const char *nm = P;
	if ((u8)(c - '0') <= 9) /* $0 à $9 : un seul chiffre */
		P++;
	else {
		name_skip();
		if (P == nm) { /* un $ seul */
			*we_w++ = '$';
			return;
		}
	}
	put_value(named_value(nm, P - nm), quoted);
}

/* mode : 0 : champs (découpage, motifs) posés sur at_p -> 0 ; 1 : une chaîne -> son adresse ;
 * 2 : une chaîne, ESC gardés (motif de case) (mot_etendre) */
char *word_expand(long mode)
{
	long s_mode = we_mode, s_have = we_have; /* (registres gardés, comme l'asm) */
	char *s_w = we_w, *s_fs = we_fs;
	char *r = 0;
	we_mode = mode;
	we_w = ar_p;
	we_fs = we_w; /* début du champ */
	we_have = 0;  /* 1 : le champ existe, même vide */
	for (;;) {
		char c = *P;
		if (word_end(c))
			goto fin;
		P++;
		if (c == '\\') {
			c = *P;
			if (!c)
				goto fin;
			P++;
			if (c == 10) /* (suite de ligne) */
				continue;
			put_quoted((u8)c);
			we_have = 1;
			continue;
		}
		if (c == '\'') {
			we_have = 1;
			for (;;) {
				c = *P;
				if (!c)
					goto cut;
				P++;
				if (c == '\'')
					break;
				put_quoted((u8)c);
			}
			continue;
		}
		if (c == '"') {
			we_have = 1;
			for (;;) {
				c = *P;
				if (!c)
					goto cut;
				P++;
				if (c == '"')
					break;
				if (c == '\\') {
					char n = *P;
					if (n == '"' || n == '\\' || n == '$' || n == '`') { /* \ ne protège que ceux-ci */
						P++;
						put_quoted((u8)n);
					} else if (n == 10)
						P++;
					else
						put_quoted('\\');
				} else if (c == '$') {
					P--;
					dollar(1);
				} else if (c == '`')
					backquote(1);
				else
					put_quoted((u8)c);
			}
			continue;
		}
		if (c == '$') {
			P--;
			dollar(0);
			continue;
		}
		if (c == '`') {
			backquote(0);
			continue;
		}
		*we_w++ = c;
	}
cut:
	text_cut();
fin:
	if (!mode) {
		if (we_w != we_fs || we_have)
			field_end();
		ar_p = we_w;
	} else {
		*we_w++ = 0;
		ar_p = we_w;
		r = we_fs;
		if (mode != 2)
			no_esc(we_fs);
	}
	we_mode = s_mode;
	we_have = s_have;
	we_w = s_w;
	we_fs = s_fs;
	return r;
}

/* ---------------------------------------------------------------------------
 *  Motifs
 * --------------------------------------------------------------------------- */

/* motif (avec ESC), texte -> 1 s'il correspond (motif_ok) */
int pattern_match(const char *p, const char *t)
{
	for (;;) {
		u8 c = (u8)*p;
		if (!c)
			return !*t;
		if (c == '*') {
			p++;
			for (;;) {
				if (pattern_match(p, t))
					return 1;
				if (!*t++)
					return 0;
			}
		}
		u8 tc = (u8)*t;
		if (!tc)
			return 0;
		if (c == '?') {
			p++;
			t++;
			continue;
		}
		if (c == '[') { /* [abc], [a-z], [!x] */
			const u8 *q = (const u8 *)p + 1;
			int seen = 0, neg = 0;
			if (*q == '!') {
				neg = 1;
				q++;
			}
			for (;;) {
				u8 c2 = *q++;
				if (!c2)
					goto lit; /* (pas de « ] » : un « [ » ordinaire) */
				if (c2 == ']')
					break;
				if (q[0] == '-' && q[1] != ']') {
					u8 hi = q[1];
					q += 2;
					if (tc >= c2 && tc <= hi)
						seen = 1;
					continue;
				}
				if (tc == c2)
					seen = 1;
			}
			if (seen == neg)
				return 0;
			p = (const char *)q;
			t++;
			continue;
		}
	lit:
		if (c == ESC)
			c = (u8)*++p;
		if (c != tc)
			return 0;
		p++;
		t++;
	}
}

/* tableau de chaînes [a, e[ : tri par insertion (trier) */
static void sort_strs(char **a, char **e)
{
	for (char **i = a + 1; i < e; i++) {
		char *v = *i;
		char **j = i;
		for (; j > a; j--) {
			const u8 *x = (const u8 *)j[-1], *y = (const u8 *)v;
			while (*x == *y && *x)
				x++, y++;
			if (*x <= *y) /* (égaux ou plus petit : à sa place) */
				break;
			*j = j[-1];
		}
		*j = v;
	}
}

/* (dans word_expand) le motif en we_fs, sur un seul niveau (« rep/x*.c », pas plus) : les noms trouvés,
 * triés, sur at_p -> 1 si au moins un (motif_etendre) */
static int glob_expand(void)
{
	char *slash = 0; /* dernier « / » */
	for (char *s = we_fs;;) {
		char c = *s++;
		if (!c)
			break;
		if (c == ESC) {
			if (!*s++)
				break;
			continue;
		}
		if (c == '/')
			slash = s - 1;
	}
	char *pat = we_fs; /* le motif du dernier composant */
	const char *dir = ".";
	if (slash) {
		pat = slash + 1;
		*slash = 0; /* (le dossier) */
		no_esc(we_fs);
		dir = we_fs;
	}
	long fd = sys_open(dir, O_READ);
	char **first = at_p;
	if (fd >= 0) {
		while (sys_readdir(fd, &dirent_buf) == 1) {
			if (dirent_buf.name[0] == '.' && pat[0] != '.') /* caché, sauf si le motif le demande */
				continue;
			if (!pattern_match(pat, dirent_buf.name))
				continue;
			at_push(we_w); /* « dossier/nom » */
			if (slash) {
				for (const char *s = dir; *s; s++)
					*we_w++ = *s;
				*we_w++ = '/';
			}
			const char *s = dirent_buf.name;
			while ((*we_w++ = *s++))
				;
		}
		sys_close(fd);
	}
	if (first != at_p) {
		sort_strs(first, at_p);
		return 1;
	}
	if (slash) /* rien : le texte du motif tel quel (comme l'asm : sans les ESC du dossier) */
		*slash = '/';
	return 0;
}
