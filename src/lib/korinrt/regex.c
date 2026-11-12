/* regex.c : expressions régulières de grep, sed et ed (docs/BIN-C.md),
 * traduction pas à pas de src/inc/regex.inc (même programme, mêmes sauts
 * relatifs, même ordre d'essai : mêmes correspondances et mêmes captures).
 *
 * Le motif devient une suite d'instructions ; CHOIX a, b essaie d'abord
 * l'instruction à +a (récursion), puis continue à +b ; SAUT a ; CAPT n
 * retient la position dans caps[n] (rendue si la suite échoue). */
#include "korinrt.h"

enum { OP_END, OP_CHAR, OP_ANY, OP_SET, OP_BOL, OP_EOL, OP_JUMP, OP_CHOICE, OP_CAPT };

#define RE_MAX 1024 /* instructions */
#define RE_SETS 64

struct ins {
	i32 op;
	i32 a;
	i64 b; /* CHOIX : second écart ; ENS : rang de l'ensemble ; SAUT vers la fin : chaîne */
};

struct regex {
	int flags;
	struct ins prog[RE_MAX];
	u32 sets[RE_SETS][8];
};

/* état de la compilation (x19 : le curseur dans l'asm) */
static struct regex *R;
static const char *cur;
static int pc, nsets, ngr, overflow;

static int emit(int op, int a)
{
	if (pc >= RE_MAX - 1) {
		overflow = 1;
		return pc - 1;
	}
	R->prog[pc].op = op;
	R->prog[pc].a = a;
	R->prog[pc].b = 0;
	return pc++;
}

/* une instruction libre insérée en i (le reste est relatif : il se déplace) */
static void insert(int i)
{
	if (pc >= RE_MAX - 1) {
		overflow = 1;
		return;
	}
	for (int k = pc; k > i; k--)
		R->prog[k] = R->prog[k - 1];
	pc++;
}

static int is_or(void)
{
	if (R->flags & RE_E) {
		if (*cur == '|') {
			cur++;
			return 1;
		}
		return 0;
	}
	if (cur[0] == '\\' && cur[1] == '|') {
		cur += 2;
		return 1;
	}
	return 0;
}

static int branch_end(void)
{
	char c = *cur;
	if (!c)
		return 1;
	if (R->flags & RE_E)
		return c == '|' || c == ')';
	return c == '\\' && (cur[1] == '|' || cur[1] == ')');
}

static void alternatives(void);

static void set_put(u32 *set, u32 c)
{
	set[(c >> 5) & 7] |= 1u << (c & 31);
	if (R->flags & RE_I) {
		if (c - 'a' <= 25)
			c -= 32;
		else if (c - 'A' <= 25)
			c += 32;
		else
			return;
		set[(c >> 5) & 7] |= 1u << (c & 31);
	}
}

/* après le crochet ouvrant */
static void set_parse(void)
{
	if (nsets >= RE_SETS) {
		overflow = 1;
		return;
	}
	int si = nsets++;
	u32 *set = R->sets[si];
	memset(set, 0, 32);
	int neg = 0;
	if (*cur == '^') {
		neg = 1;
		cur++;
	}
	if (*cur == ']') { /* ] en tête : un caractère */
		cur++;
		set_put(set, ']');
	}
	for (;;) {
		u8 c = (u8)*cur;
		if (!c)
			break;
		cur++;
		if (c == ']')
			break;
		u8 d = (u8)cur[1];
		if (cur[0] == '-' && d && d != ']') {
			cur += 2;
			u32 k = c; /* (comme l'asm : au moins une fois, jusqu'à d) */
			do
				set_put(set, k);
			while (++k <= d);
			continue;
		}
		set_put(set, c);
	}
	if (neg)
		for (int k = 0; k < 8; k++)
			set[k] = ~set[k];
	int i = emit(OP_SET, 0);
	R->prog[i].b = si;
}

/* après la parenthèse ouvrante */
static void group(void)
{
	int g = ngr++;
	emit(OP_CAPT, g * 2);
	alternatives();
	if (R->flags & RE_E) {
		if (*cur == ')')
			cur++;
	} else if (*cur == '\\')
		cur += 2;
	emit(OP_CAPT, g * 2 + 1);
}

/* un atome (first : en tête de branche) */
static void atom(int first)
{
	int fl = R->flags;
	u32 c = (u8)*cur++;
	if (c == '^') {
		if (first || (fl & RE_E)) {
			emit(OP_BOL, 0);
			return;
		}
	} else if (c == '$') {
		if (branch_end() || (fl & RE_E)) {
			emit(OP_EOL, 0);
			return;
		}
	} else if (c == '.') {
		emit(OP_ANY, 0);
		return;
	} else if (c == '[') {
		set_parse();
		return;
	} else if (c == '(') {
		if (fl & RE_E) {
			group();
			return;
		}
	} else if (c == '\\') {
		c = (u8)*cur++;
		if (!c) {
			cur--;
			c = '\\';
		} else if (c == '(') {
			if (!(fl & RE_E)) {
				group();
				return;
			}
		} else if (c == 't')
			c = 9;
		else if (c == 'n')
			c = 10;
	}
	if ((fl & RE_I) && c - 'A' <= 25)
		c += 32;
	emit(OP_CHAR, (int)c);
}

/* une suite d'atomes quantifiés */
static void branch(void)
{
	int first = 1;
	while (!branch_end() && !overflow) {
		int start = pc;
		atom(first);
		first = 0;
		for (;;) {
			char c = *cur;
			int q = 0; /* 1 *, 2 +, 3 ? */
			if (c == '*')
				q = 1;
			else if (R->flags & RE_E)
				q = c == '+' ? 2 : c == '?' ? 3 : 0;
			else if (c == '\\' && cur[1] == '+')
				cur++, q = 2;
			else if (c == '\\' && cur[1] == '?')
				cur++, q = 3;
			if (!q)
				break;
			cur++;
			if (q == 1) { /* CHOIX atome SAUT-arrière */
				insert(start);
				emit(OP_JUMP, start - pc);
				R->prog[start].op = OP_CHOICE;
				R->prog[start].a = 1;
				R->prog[start].b = pc - start;
			} else if (q == 2) { /* atome CHOIX-arrière */
				int i = emit(OP_CHOICE, start - pc);
				R->prog[i].b = 1;
			} else { /* CHOIX atome */
				insert(start);
				R->prog[start].op = OP_CHOICE;
				R->prog[start].a = 1;
				R->prog[start].b = pc - start;
			}
			if (overflow)
				return;
		}
	}
}

/* a|b : jusqu'à la fin, ou la parenthèse fermante */
static void alternatives(void)
{
	int start = pc;
	long jumps = -1; /* sauts vers la fin, chaînés dans b */
	for (;;) {
		branch();
		if (overflow || !is_or())
			break;
		insert(start); /* CHOIX devant la branche */
		int j = emit(OP_JUMP, 0); /* après la branche : vers la fin */
		R->prog[j].b = jumps;
		jumps = j;
		R->prog[start].op = OP_CHOICE;
		R->prog[start].a = 1;
		R->prog[start].b = pc - start;
		start = pc;
	}
	while (jumps >= 0) {
		long next = R->prog[jumps].b;
		R->prog[jumps].a = (i32)(pc - jumps);
		jumps = next;
	}
}

struct regex *re_compile(const char *pat, int flags)
{
	struct regex *re = malloc(sizeof *re);
	if (!re)
		return 0;
	R = re;
	re->flags = flags;
	cur = pat;
	pc = 0;
	nsets = 0;
	ngr = 1;
	overflow = 0;
	emit(OP_CAPT, 0);
	alternatives();
	emit(OP_CAPT, 1);
	emit(OP_END, 0);
	if (overflow) {
		free(re);
		return 0;
	}
	return re;
}

/* ---- exécution */

static const struct regex *X;
static const char *x_line;
static const char **x_caps;

static int exec(const struct ins *p, const char *s)
{
	for (;;) {
		u32 c;
		switch (p->op) {
		case OP_CHAR:
			c = (u8)*s;
			if (!c)
				return 0;
			if ((X->flags & RE_I) && c - 'A' <= 25)
				c += 32;
			if (c != (u32)p->a)
				return 0;
			s++;
			p++;
			break;
		case OP_ANY:
			if (!*s)
				return 0;
			s++;
			p++;
			break;
		case OP_SET:
			c = (u8)*s;
			if (!c || !((X->sets[p->b][c >> 5] >> (c & 31)) & 1))
				return 0;
			s++;
			p++;
			break;
		case OP_BOL:
			if (s != x_line)
				return 0;
			p++;
			break;
		case OP_EOL:
			if (*s)
				return 0;
			p++;
			break;
		case OP_JUMP:
			p += p->a;
			break;
		case OP_CHOICE:
			if (exec(p + p->a, s))
				return 1;
			p += p->b;
			break;
		case OP_CAPT:
			if ((u32)p->a >= RE_CAPS) {
				p++;
				break;
			} else {
				const char *old = x_caps[p->a];
				x_caps[p->a] = s;
				if (exec(p + 1, s))
					return 1;
				x_caps[p->a] = old;
				return 0;
			}
		default: /* OP_END */
			return 1;
		}
	}
}

int re_search(struct regex *re, const char *line, const char *start, const char *caps[RE_CAPS])
{
	X = re;
	x_line = line;
	x_caps = caps;
	const char *s = start;
	for (;;) {
		for (int i = 0; i < RE_CAPS; i++)
			caps[i] = 0;
		if (exec(re->prog, s))
			return 1;
		if (!*s++)
			return 0;
	}
}
