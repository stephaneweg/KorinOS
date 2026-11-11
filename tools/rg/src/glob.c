/*
 * glob.c -- rg (docs/RG.md) : les motifs de fichiers, comme le crate globset de ripgrep 14.1 (un glob
 * devient une expression, compilee par PCRE2 en mode octets), et les fichiers d'exclusion comme le
 * crate ignore (gitignore.rs : !negation, /ancre, dossier/, **, la derniere ligne qui correspond gagne).
 * Formes rapides sans PCRE2 : un nom a toute profondeur, une extension a toute profondeur, « *.ext »,
 * un nom seul.
 */
#include "rg.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

enum { T_LIT, T_ANY, T_STAR, T_RPREFIX, T_RSUFFIX, T_RZOM, T_CLASS, T_ALT };
enum { G_REGEX, G_LITERAL, G_LITERAL_BASE, G_EXT, G_ALL };

struct tok
{
	int t;
	unsigned c;			/* T_LIT : l'octet */
	int neg;			/* T_CLASS */
	unsigned *rng;			/* T_CLASS : paires (debut, fin) */
	int nrng;
	struct toks *alts;		/* T_ALT */
	int nalts;
};

struct toks
{
	struct tok *v;
	int n, cap;
};

static void tk_push (struct toks *s, struct tok t)
{
	if (s->n == s->cap)
	{
		s->cap = s->cap ? s->cap * 2 : 16;
		s->v = xrealloc (s->v, s->cap * sizeof *s->v);
	}
	s->v[s->n++] = t;
}

static void tk_free (struct toks *s)
{
	for (int i = 0; i < s->n; i++)
	{
		free (s->v[i].rng);
		for (int j = 0; j < s->v[i].nalts; j++)
			tk_free (&s->v[i].alts[j]);
		free (s->v[i].alts);
	}
	free (s->v);
	s->v = 0;
	s->n = s->cap = 0;
}

/* l'etat de l'analyse : une pile de suites (la premiere : le glob ; les suivantes : les branches de { }) */
struct parser
{
	const unsigned char *s;
	size_t i, n;
	int prev;			/* le caractere precedent (-1 : aucun) */
	int cur;
	struct toks stack[64];
	int depth;
	int literal_sep, allow_unclosed;
	const char *err;
};

static int p_bump (struct parser *p)
{
	if (p->i >= p->n)
	{
		p->prev = p->cur;
		p->cur = -1;
		return -1;
	}
	p->prev = p->cur;
	p->cur = p->s[p->i++];
	return p->cur;
}

static int p_peek (struct parser *p) { return p->i < p->n ? p->s[p->i] : -1; }

static struct toks *p_top (struct parser *p) { return &p->stack[p->depth - 1]; }

static void p_tok (struct parser *p, int t, unsigned c)
{
	struct tok k = { 0 };
	k.t = t;
	k.c = c;
	tk_push (p_top (p), k);
}

static int is_sep (int c) { return c == '/'; }

static void parse_star (struct parser *p)
{
	int prev = p->prev;
	if (p_peek (p) != '*')
	{
		p_tok (p, T_STAR, 0);
		return;
	}
	p_bump (p);
	if (p_top (p)->n == 0)		/* au debut (du glob ou d'une branche) */
	{
		int c = p_peek (p);
		if (c != -1 && !is_sep (c))
		{
			p_tok (p, T_STAR, 0);
			p_tok (p, T_STAR, 0);
		}
		else
		{
			p_tok (p, T_RPREFIX, 0);
			p_bump (p);
		}
		return;
	}
	if (!(prev != -1 && is_sep (prev)))
	{
		if (p->depth <= 1 || (prev != ',' && prev != '{'))
		{
			p_tok (p, T_STAR, 0);
			p_tok (p, T_STAR, 0);
			return;
		}
	}
	int is_suffix;
	int c = p_peek (p);
	if (c == -1)
	{
		p_bump (p);
		is_suffix = 1;
	}
	else if ((c == ',' || c == '}') && p->depth >= 2)
		is_suffix = 1;
	else if (is_sep (c))
	{
		p_bump (p);
		is_suffix = 0;
	}
	else
	{
		p_tok (p, T_STAR, 0);
		p_tok (p, T_STAR, 0);
		return;
	}
	struct toks *t = p_top (p);
	struct tok last = t->v[--t->n];		/* (le separateur avant **) */
	if (last.t == T_RPREFIX)
		p_tok (p, T_RPREFIX, 0);
	else if (last.t == T_RSUFFIX)
		p_tok (p, T_RSUFFIX, 0);
	else
		p_tok (p, is_suffix ? T_RSUFFIX : T_RZOM, 0);
}

static void parse_class (struct parser *p)
{
	size_t save_i = p->i;
	int save_cur = p->cur, save_prev = p->prev;
	unsigned *r = 0;
	int nr = 0, cap = 0, neg = 0, first = 1, in_range = 0;
	int c = p_peek (p);
	if (c == '!' || c == '^')
	{
		p_bump (p);
		neg = 1;
	}
	for (;;)
	{
		c = p_bump (p);
		if (c == -1)
		{
			free (r);
			if (p->allow_unclosed)
			{
				/* le [ est un caractere ordinaire, la suite est relue */
				p->i = save_i;
				p->cur = save_cur;
				p->prev = save_prev;
				p_tok (p, T_LIT, '[');
				return;
			}
			p->err = "unclosed character class; missing ']'";
			return;
		}
		if (nr + 2 > cap)
		{
			cap = cap ? cap * 2 : 16;
			r = xrealloc (r, cap * 2 * sizeof *r);
		}
		if (c == ']')
		{
			if (first)
			{
				r[2 * nr] = r[2 * nr + 1] = ']';
				nr++;
			}
			else
				break;
		}
		else if (c == '-')
		{
			if (first)
			{
				r[2 * nr] = r[2 * nr + 1] = '-';
				nr++;
			}
			else if (in_range)
			{
				r[2 * nr - 1] = '-';
				if (r[2 * nr - 1] < r[2 * nr - 2])
				{
					p->err = "invalid range";
					free (r);
					return;
				}
				in_range = 0;
			}
			else
				in_range = 1;
		}
		else
		{
			if (in_range)
			{
				r[2 * nr - 1] = c;
				if (r[2 * nr - 1] < r[2 * nr - 2])
				{
					p->err = "invalid range";
					free (r);
					return;
				}
			}
			else
			{
				r[2 * nr] = r[2 * nr + 1] = c;
				nr++;
			}
			in_range = 0;
		}
		first = 0;
	}
	if (in_range)
	{
		r[2 * nr] = r[2 * nr + 1] = '-';
		nr++;
	}
	struct tok k = { 0 };
	k.t = T_CLASS;
	k.neg = neg;
	k.rng = r;
	k.nrng = nr;
	tk_push (p_top (p), k);
}

static int parse_glob (struct parser *p, struct toks *out)
{
	p->depth = 1;
	memset (&p->stack[0], 0, sizeof p->stack[0]);
	p->prev = p->cur = -1;
	int c;
	while (!p->err && (c = p_bump (p)) != -1)
	{
		switch (c)
		{
		case '?':
			p_tok (p, T_ANY, 0);
			break;
		case '*':
			parse_star (p);
			break;
		case '[':
			parse_class (p);
			break;
		case '{':
			if (p->depth > 1)
			{
				p->err = "nested alternate groups are not allowed";
				break;
			}
			memset (&p->stack[p->depth], 0, sizeof p->stack[0]);
			p->depth++;
			break;
		case '}':
			if (p->depth <= 1)
			{
				p_tok (p, T_LIT, '}');
				break;
			}
			{
				struct tok k = { 0 };
				k.t = T_ALT;
				k.nalts = p->depth - 1;
				k.alts = xmalloc (k.nalts * sizeof *k.alts);
				for (int j = 0; j < k.nalts; j++)
					k.alts[j] = p->stack[1 + j];
				p->depth = 1;
				tk_push (p_top (p), k);
			}
			break;
		case ',':
			if (p->depth <= 1)
				p_tok (p, T_LIT, ',');
			else
			{
				if (p->depth >= 64)
				{
					p->err = "too many alternates";
					break;
				}
				memset (&p->stack[p->depth], 0, sizeof p->stack[0]);
				p->depth++;
			}
			break;
		case '\\':
			c = p_bump (p);
			if (c == -1)
				p->err = "dangling '\\'";
			else
				p_tok (p, T_LIT, c);
			break;
		default:
			p_tok (p, T_LIT, c);
		}
	}
	if (!p->err && p->depth > 1)
		p->err = "unclosed alternate group; missing '}' (maybe escape '{' with '[{]'?)";
	if (p->err)
	{
		for (int j = 0; j < p->depth; j++)
			tk_free (&p->stack[j]);
		return -1;
	}
	*out = p->stack[0];
	return 0;
}

static void re_lit (sbuf *b, unsigned c)
{
	if (c < 0x80 && !isalnum (c))
		sb_putc (b, '\\');
	if (c >= 0x80)
		sb_printf (b, "\\x{%02x}", c);
	else
		sb_putc (b, (char) c);
}

static void to_regex (sbuf *b, const struct toks *t, int litsep)
{
	for (int i = 0; i < t->n; i++)
	{
		const struct tok *k = &t->v[i];
		switch (k->t)
		{
		case T_LIT:
			re_lit (b, k->c);
			break;
		case T_ANY:
			sb_puts (b, litsep ? "[^/]" : ".");
			break;
		case T_STAR:
			sb_puts (b, litsep ? "[^/]*" : ".*");
			break;
		case T_RPREFIX:
			sb_puts (b, "(?:/?|.*/)");
			break;
		case T_RSUFFIX:
			sb_puts (b, "/.*");
			break;
		case T_RZOM:
			sb_puts (b, "(?:/|/.*/)");
			break;
		case T_CLASS:
			sb_putc (b, '[');
			if (k->neg)
				sb_putc (b, '^');
			for (int j = 0; j < k->nrng; j++)
			{
				re_lit (b, k->rng[2 * j]);
				if (k->rng[2 * j + 1] != k->rng[2 * j])
				{
					sb_putc (b, '-');
					re_lit (b, k->rng[2 * j + 1]);
				}
			}
			sb_putc (b, ']');
			break;
		case T_ALT:
			sb_puts (b, "(?:");
			for (int j = 0; j < k->nalts; j++)
			{
				if (j)
					sb_putc (b, '|');
				to_regex (b, &k->alts[j], litsep);
			}
			sb_putc (b, ')');
			break;
		}
	}
}

/* les literaux qui suivent, a partir de i : 1 si tout le reste n'est fait que de literaux */
static int all_lit_from (const struct toks *t, int i, sbuf *lit)
{
	for (; i < t->n; i++)
	{
		if (t->v[i].t != T_LIT)
			return 0;
		sb_putc (lit, (char) t->v[i].c);
	}
	return 1;
}

const char *glob_compile (const char *glob, int literal_sep, int icase, int allow_unclosed, struct glob_re *out)
{
	struct parser p;
	memset (&p, 0, sizeof p);
	p.s = (const unsigned char *) glob;
	p.n = strlen (glob);
	p.literal_sep = literal_sep;
	p.allow_unclosed = allow_unclosed;
	struct toks t;
	memset (out, 0, sizeof *out);
	if (parse_glob (&p, &t) < 0)
		return p.err;
	/* formes rapides */
	if (!icase)
	{
		sbuf lit = { 0 };
		if (t.n >= 1 && all_lit_from (&t, 0, &lit))
		{
			out->kind = G_LITERAL;
		}
		else if (t.n >= 2 && t.v[0].t == T_RPREFIX && (lit.n = 0, all_lit_from (&t, 1, &lit)))
			out->kind = G_LITERAL_BASE;
		else if (t.n >= 3 && t.v[0].t == T_RPREFIX && t.v[1].t == T_STAR && (lit.n = 0, all_lit_from (&t, 2, &lit))
			 && !memchr (lit.p, '/', lit.n))
			out->kind = G_EXT;
		else if (!literal_sep && t.n >= 2 && t.v[0].t == T_STAR && (lit.n = 0, all_lit_from (&t, 1, &lit)))
			out->kind = G_EXT;
		else if (t.n == 1 && t.v[0].t == T_RPREFIX)
			out->kind = G_ALL;
		else
			out->kind = G_REGEX;
		if (out->kind != G_REGEX)
		{
			if (out->kind != G_ALL)
			{
				out->lit = lit.p ? lit.p : xstrdup ("");
				out->litlen = lit.n;
			}
			else
				sb_free (&lit);
			tk_free (&t);
			return 0;
		}
		sb_free (&lit);
	}
	sbuf re = { 0 };
	if (t.n == 1 && t.v[0].t == T_RPREFIX)
		sb_puts (&re, "^.*$");
	else
	{
		sb_putc (&re, '^');
		to_regex (&re, &t, literal_sep);
		sb_putc (&re, '$');
	}
	tk_free (&t);
	int ec;
	PCRE2_SIZE eo;
	out->kind = G_REGEX;
	out->re = pcre2_compile ((PCRE2_SPTR) re.p, re.n, PCRE2_DOTALL | PCRE2_DOLLAR_ENDONLY | (icase ? PCRE2_CASELESS : 0),
				 &ec, &eo, 0);
	sb_free (&re);
	if (!out->re)
		return "invalid glob";
	pcre2_jit_compile (out->re, PCRE2_JIT_COMPLETE);
	return 0;
}

static pcre2_match_data *s_md;

int glob_match (const struct glob_re *g, const char *s, size_t n)
{
	switch (g->kind)
	{
	case G_ALL:
		return 1;
	case G_LITERAL:
		return n == g->litlen && memcmp (s, g->lit, n) == 0;
	case G_LITERAL_BASE:
		if (n == g->litlen)
			return memcmp (s, g->lit, n) == 0;
		return n > g->litlen && s[n - g->litlen - 1] == '/' && memcmp (s + n - g->litlen, g->lit, g->litlen) == 0;
	case G_EXT:
		return n >= g->litlen && memcmp (s + n - g->litlen, g->lit, g->litlen) == 0;
	}
	if (!s_md)
		s_md = pcre2_match_data_create (4, 0);
	return pcre2_match (g->re, (PCRE2_SPTR) s, n, 0, 0, s_md, 0) >= 0;
}

void glob_free (struct glob_re *g)
{
	if (g->re)
		pcre2_code_free (g->re);
	free (g->lit);
	memset (g, 0, sizeof *g);
}

/* ---- gitignore ---- */

void gi_init (struct gi *m, const char *root)
{
	memset (m, 0, sizeof *m);
	if (root[0] == '.' && root[1] == '/' && root[2])
		root += 2;
	if (!root[0] || strcmp (root, "./") == 0)
		root = ".";
	m->root = xstrdup (root);
	m->rootlen = strlen (m->root);
}

static int is_space (unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

const char *gi_add_line (struct gi *m, const char *line, size_t len, int allow_unclosed)
{
	if (len && line[0] == '#')
		return 0;
	if (!(len >= 2 && line[len - 2] == '\\' && line[len - 1] == ' '))
		while (len && is_space ((unsigned char) line[len - 1]))
			len--;
	if (!len)
		return 0;
	int whitelist = 0, only_dir = 0, absolute = 0;
	if (len >= 2 && line[0] == '\\' && (line[1] == '!' || line[1] == '#'))
	{
		line++;
		len--;
		absolute = line[0] == '/';
	}
	else
	{
		if (line[0] == '!')
		{
			whitelist = 1;
			line++;
			len--;
		}
		if (len && line[0] == '/')
		{
			line++;
			len--;
			absolute = 1;
		}
	}
	if (len && line[len - 1] == '/')
	{
		only_dir = 1;
		len--;
		if (len && line[len - 1] == '\\')
			len--;
	}
	sbuf a = { 0 };
	int has_slash = memchr (line, '/', len) != 0;
	if (!absolute && !has_slash)
	{
		int dsp = (len >= 3 && memcmp (line, "**/", 3) == 0) || (len == 2 && memcmp (line, "**", 2) == 0);
		if (!dsp)
			sb_puts (&a, "**/");
	}
	sb_put (&a, line, len);
	if (a.n >= 3 && memcmp (a.p + a.n - 3, "/**", 3) == 0)
		sb_puts (&a, "/*");
	if (!a.p)
		sb_puts (&a, "");
	struct gi_pat g = { 0 };
	const char *e = glob_compile (a.p, 1, m->icase, allow_unclosed, &g.g);
	sb_free (&a);
	if (e)
		return e;
	g.whitelist = whitelist;
	g.only_dir = only_dir;
	if (m->n == m->cap)
	{
		m->cap = m->cap ? m->cap * 2 : 8;
		m->v = xrealloc (m->v, m->cap * sizeof *m->v);
	}
	m->v[m->n++] = g;
	return 0;
}

int gi_add_file (struct gi *m, const char *path)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0)
		return 0;
	struct stat st;
	if (fstat (fd, &st) < 0 || S_ISDIR (st.st_mode))
	{
		close (fd);
		return 0;
	}
	sbuf b = { 0 };
	char tmp[8192];
	ssize_t r;
	while ((r = read (fd, tmp, sizeof tmp)) > 0)
		sb_put (&b, tmp, r);
	close (fd);
	size_t i = 0;
	/* (ripgrep enleve un BOM UTF-8 en tete) */
	if (b.n >= 3 && (unsigned char) b.p[0] == 0xEF && (unsigned char) b.p[1] == 0xBB && (unsigned char) b.p[2] == 0xBF)
		i = 3;
	while (i < b.n)
	{
		char *nl = memchr (b.p + i, '\n', b.n - i);
		size_t e = nl ? (size_t) (nl - b.p) : b.n;
		const char *err = gi_add_line (m, b.p + i, e - i, 1);
		if (err && !g_no_messages)
			rg_err ("%s: error parsing glob '%.*s': %s", path, (int) (e - i), b.p + i, err);
		i = e + 1;
	}
	sb_free (&b);
	return 1;
}

static int is_file_name (const char *p, size_t n)
{
	return !memchr (p, '/', n);
}

int gi_matched (const struct gi *m, const char *path, int is_dir)
{
	if (!m->n)
		return M_NONE;
	size_t n = strlen (path);
	if (n >= 2 && path[0] == '.' && path[1] == '/')
	{
		path += 2;
		n -= 2;
	}
	if (!(m->rootlen == 1 && m->root[0] == '.') && !is_file_name (path, n) && n >= m->rootlen
	    && memcmp (path, m->root, m->rootlen) == 0)
	{
		path += m->rootlen;
		n -= m->rootlen;
		if (n && path[0] == '/')
		{
			path++;
			n--;
		}
	}
	for (int i = m->n - 1; i >= 0; i--)
	{
		const struct gi_pat *g = &m->v[i];
		if (g->only_dir && !is_dir)
			continue;
		if (glob_match (&g->g, path, n))
			return g->whitelist ? M_WHITELIST : M_IGNORE;
	}
	return M_NONE;
}

void gi_free (struct gi *m)
{
	for (int i = 0; i < m->n; i++)
		glob_free (&m->v[i].g);
	free (m->v);
	free (m->root);
	memset (m, 0, sizeof *m);
}
