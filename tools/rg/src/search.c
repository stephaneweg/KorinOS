/*
 * search.c -- rg (docs/RG.md) : la recherche dans un fichier et les trois sorties de ripgrep 14.1
 * (grep-searcher et grep-printer) : lignes (standard), resumes (-c, -l, --files-without-match, -q) et
 * --json. Le fichier est lu en entier ; la lecture par tampons de 64 Kio de ripgrep est rejouee pour que
 * la detection des fichiers binaires (un octet NUL) s'arrete au meme endroit :
 *  - fichiers trouves par le parcours : « quit » (un NUL dans un tampon lu arrete la recherche, ce qui
 *    est avant reste) ;
 *  - fichiers donnes (au plus 10, lus d'un bloc comme le mmap de ripgrep) et -U : un NUL dans les
 *    64 premiers Kio ou dans une ligne trouvee ; « convert » pour les fichiers donnes (la sortie standard
 *    dit « binary file matches » et s'arrete) ;
 *  - l'entree standard et plus de 10 fichiers donnes : « convert » par tampons (NUL -> fin de ligne).
 * Expressions : PCRE2 (UTF, UCP, MATCH_INVALID_UTF, MULTILINE ; JIT si le systeme donne de la memoire
 * executable, sinon l'interpreteur).
 */
#include "rg.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#define BUFCAP (64 << 10)		/* DEFAULT_BUFFER_CAPACITY de grep-searcher */
#define MAX_LOOK_AHEAD 128

static pcre2_code *s_re;
static pcre2_match_data *s_md;
static pcre2_match_context *s_mctx;
static pcre2_jit_stack *s_jstack;
static int s_jit;
int g_quit_now;
int g_slice_explicit;
long long g_files_listed;

/* ---- l'expression ---- */

static void esc_lit (sbuf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		unsigned char c = s[i];
		if (c < 0x80 && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
		{
			if (c == '\n')
			{
				sb_puts (b, "\\n");
				continue;
			}
			if (c < 0x20 || c == 0x7f)
			{
				sb_printf (b, "\\x{%x}", c);
				continue;
			}
			sb_putc (b, '\\');
		}
		sb_putc (b, (char) c);
	}
}

/* -S : une majuscule litterale (hors echappements, noms de classes \p{..}, groupes nommes) */
static int has_upper (const char *s)
{
	for (size_t i = 0; s[i]; i++)
	{
		unsigned char c = s[i];
		if (c == '\\')
		{
			i++;
			if (!s[i])
				break;
			if ((s[i] == 'p' || s[i] == 'P' || s[i] == 'x' || s[i] == 'u' || s[i] == 'U') && s[i + 1] == '{')
			{
				while (s[i] && s[i] != '}')
					i++;
				if (!s[i])
					break;
			}
			else if (s[i] == 'p' || s[i] == 'P')
				i++;
			continue;
		}
		if (c == '(' && s[i + 1] == '?')
		{
			/* (?P<nom>, (?<nom>, (?i) ... : on saute le nom */
			size_t j = i + 2;
			if (s[j] == 'P')
				j++;
			if (s[j] == '<' && s[j + 1] != '=' && s[j + 1] != '!')
			{
				while (s[j] && s[j] != '>')
					j++;
				i = s[j] ? j : j - 1;
				continue;
			}
		}
		if (c >= 'A' && c <= 'Z')
			return 1;
		if (c >= 0xC0)
		{
			/* une majuscule non ASCII : le cas latin courant (C3 80..9E sauf 97) */
			unsigned char d = s[i + 1];
			if (c == 0xC3 && d >= 0x80 && d <= 0x9E && d != 0x97)
				return 1;
		}
	}
	return 0;
}

int search_init (void)
{
	sbuf p = { 0 };
	int any_lit = 0;
	for (int i = 0; i < O.npats; i++)
	{
		const char *s = O.pats[i];
		if (!O.multiline && strchr (s, '\n'))
		{
			rg_err ("the literal '\"\\n\"' is not allowed in a regex\n\nConsider enabling multiline mode with the --multiline flag (or -U for short).\nWhen multiline mode is enabled, new line characters can be matched.");
			return 2;
		}
		if (*s)
			any_lit = 1;
	}
	(void) any_lit;
	int icase = O.case_mode == 1;
	if (O.case_mode == 2)
	{
		icase = 1;
		for (int i = 0; i < O.npats; i++)
			if (O.fixed ? (strpbrk (O.pats[i], "ABCDEFGHIJKLMNOPQRSTUVWXYZ") != 0) : has_upper (O.pats[i]))
				icase = 0;
	}
	if (O.npats == 0)
		sb_puts (&p, "(*FAIL)");	/* -f d'un fichier vide : rien ne correspond */
	for (int i = 0; i < O.npats; i++)
	{
		if (O.npats > 1)
			sb_puts (&p, i ? "|(?:" : "(?:");
		if (O.fixed)
			esc_lit (&p, O.pats[i], strlen (O.pats[i]));
		else
			sb_puts (&p, O.pats[i]);
		if (O.npats > 1)
			sb_putc (&p, ')');
	}
	sbuf w = { 0 };
	if (O.line_x)
		sb_printf (&w, "^(?:%s)$", p.p);
	else if (O.word)
		sb_printf (&w, "(?<!\\w)(?:%s)(?!\\w)", p.p);
	else
		sb_put (&w, p.p, p.n);
	uint32_t fl = PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF | PCRE2_MULTILINE;
	if (icase)
		fl |= PCRE2_CASELESS;
	if (O.multiline && O.dotall)
		fl |= PCRE2_DOTALL;
	pcre2_compile_context *cc = pcre2_compile_context_create (0);
	pcre2_set_newline (cc, PCRE2_NEWLINE_LF);
	int ec;
	PCRE2_SIZE eo;
	s_re = pcre2_compile ((PCRE2_SPTR) w.p, w.n, fl, &ec, &eo, cc);
	pcre2_compile_context_free (cc);
	if (!s_re)
	{
		PCRE2_UCHAR msg[256];
		pcre2_get_error_message (ec, msg, sizeof msg);
		sbuf e = { 0 };
		sb_printf (&e, "regex parse error:\n    %s\n    ", w.p);
		for (PCRE2_SIZE k = 0; k < eo && k < w.n; k++)
			sb_putc (&e, ' ');
		sb_printf (&e, "^\nerror: %s", (char *) msg);
		rg_err ("%s", e.p);
		return 2;
	}
	sb_free (&p);
	sb_free (&w);
	const char *nojit = getenv ("RG_NO_JIT");
	if (!(nojit && *nojit && *nojit != '0') && pcre2_jit_compile (s_re, PCRE2_JIT_COMPLETE) == 0)
	{
		s_jit = 1;
		s_mctx = pcre2_match_context_create (0);
		s_jstack = pcre2_jit_stack_create (32 << 10, 1 << 20, 0);
		if (s_jstack)
			pcre2_jit_stack_assign (s_mctx, 0, s_jstack);
	}
	s_md = pcre2_match_data_create_from_pattern (s_re, 0);
	return 0;
}

int rx_jit_active (void) { return s_jit; }

/* --version : la memoire executable est-elle donnee (JIT de PCRE2) ? */
int rx_jit_probe (void)
{
	int ec;
	PCRE2_SIZE eo;
	pcre2_code *c = pcre2_compile ((PCRE2_SPTR) "a+b", 3, PCRE2_UTF, &ec, &eo, 0);
	if (!c)
		return 0;
	int ok = pcre2_jit_compile (c, PCRE2_JIT_COMPLETE) == 0;
	pcre2_code_free (c);
	return ok;
}

/* cherche a partir de start dans s[0..n) ; 1 et [*ms, *me) si trouve */
static int rx_find (const char *s, size_t n, size_t start, size_t *ms, size_t *me)
{
	if (start > n)
		return 0;
	int r = pcre2_match (s_re, (PCRE2_SPTR) s, n, start, 0, s_md, s_mctx);
	if (r == PCRE2_ERROR_JIT_STACKLIMIT || r == PCRE2_ERROR_MATCHLIMIT || r == PCRE2_ERROR_DEPTHLIMIT
	    || r == PCRE2_ERROR_HEAPLIMIT)
		r = pcre2_match (s_re, (PCRE2_SPTR) s, n, start, PCRE2_NO_JIT, s_md, 0);
	if (r < 0)
		return 0;
	PCRE2_SIZE *ov = pcre2_get_ovector_pointer (s_md);
	*ms = ov[0];
	*me = ov[1];
	if (*me < *ms)		/* (\K) */
		*me = *ms;
	return 1;
}

/* ---- l'etat d'une recherche ---- */

struct m { size_t s, e; };

static struct
{
	const char *F;			/* le contenu */
	size_t n;
	const char *path;
	size_t pathlen;
	int show_path;
	int sink;			/* 0 standard, 1 resume, 2 json */
	int quit_mode, convert_mode;	/* detection binaire */
	int core_binary;		/* lecture d'un bloc : verifier aussi les lignes trouvees */
	long long bin_off;		/* -1 */
	/* coeur (grep-searcher) */
	size_t last_line_visited;
	long long after_left;
	int has_sunk;
	size_t ln_pos;
	unsigned long long ln;
	/* imprimante */
	long long match_count;
	long long after_remaining;
	int begin_printed;
	size_t file_written;		/* octets ecrits pour ce fichier */
	/* statistiques */
	unsigned long long st_matches, st_lines;
	struct m *mv;
	int mn, mcap;
	struct timespec t0;
} S;

static unsigned long long s_total_written;		/* imprimante standard : tout ce qu'elle a ecrit */
static struct
{
	unsigned long long elapsed_ns, searches, with_match, bytes_searched, bytes_printed, matched_lines, matches;
} T;
static struct timespec s_start;
static struct
{
	unsigned long long matches, lines, with_match, searched, printed, bytes, ns;
} ST;

static void pw (const void *p, size_t n)
{
	sb_put (&g_out, p, n);
	S.file_written += n;
	s_total_written += n;
}

static void pws (const char *s) { pw (s, strlen (s)); }

static void pwc (char c) { pw (&c, 1); }

static void pwnum (unsigned long long v)
{
	char b[32];
	int k = snprintf (b, sizeof b, "%llu", v);
	pw (b, k);
}

static size_t line_end_noterm (size_t ls, size_t le)
{
	if (le > ls && S.F[le - 1] == '\n')
		return le - 1;
	return le;
}

/* les correspondances d'une ligne (ou d'un bloc -U) : find_iter_at_in_context de grep-printer */
static void record_matches (size_t rs, size_t re)
{
	S.mn = 0;
	size_t subj_end;
	if (O.multiline)
	{
		subj_end = S.n;
		if (subj_end - re >= MAX_LOOK_AHEAD)
			subj_end = re + MAX_LOOK_AHEAD;
	}
	else
		subj_end = line_end_noterm (rs, re);
	size_t at = rs;
	long long last_end = -1;
	for (;;)
	{
		size_t ms, me;
		if (at > subj_end || !rx_find (S.F, subj_end, at, &ms, &me))
			break;
		if (ms >= re)
			break;
		if (ms == me)
		{
			at = me + 1;
			if ((long long) me == last_end)
				continue;
		}
		else
			at = me;
		last_end = me;
		if (S.mn == S.mcap)
		{
			S.mcap = S.mcap ? S.mcap * 2 : 16;
			S.mv = xrealloc (S.mv, S.mcap * sizeof *S.mv);
		}
		S.mv[S.mn].s = ms;
		S.mv[S.mn].e = me;
		S.mn++;
	}
}

/* ---- imprimante standard ---- */

static int needs_granularity (void)
{
	return O.only_matching || O.column || O.vimgrep || O.replace || O.stats || O.color == 2;
}

static void write_search_prelude (void)
{
	if (S.file_written > 0)
		return;
	const char *sep = 0;
	if (O.heading)
		sep = "";
	else if (O.after > 0 || O.before > 0)
		sep = O.no_context_sep ? 0 : O.context_sep;
	if (sep && s_total_written > 0)
	{
		pws (sep);
		pwc ('\n');
	}
	if (O.heading && S.show_path)
	{
		pw (S.path, S.pathlen);
		pwc (O.null ? '\0' : '\n');
	}
}

static void write_prelude (int is_match, unsigned long long lineno, long long col, unsigned long long off)
{
	char fsep = is_match ? O.field_match_sep[0] : O.field_context_sep[0];
	const char *fs = is_match ? O.field_match_sep : O.field_context_sep;
	(void) fsep;
	int pending = 0;		/* 0 aucun, 1 champ, 2 fin de chemin */
	if (!O.heading && S.show_path)
	{
		pw (S.path, S.pathlen);
		pending = O.null ? 2 : 1;
	}
#define SEP() do { if (pending == 1) pws (fs); else if (pending == 2) pwc ('\0'); pending = 0; } while (0)
	if (O.line_number)
	{
		SEP ();
		pwnum (lineno);
		pending = 1;
	}
	if (col >= 0)
	{
		SEP ();
		pwnum (col);
		pending = 1;
	}
	if (O.byte_offset)
	{
		SEP ();
		pwnum (off);
		pending = 1;
	}
	SEP ();
#undef SEP
}

static size_t trim_prefix (size_t s, size_t e)
{
	if (!O.trim)
		return s;
	while (s < e && (S.F[s] == ' ' || S.F[s] == '\t' || S.F[s] == '\r' || S.F[s] == '\f' || S.F[s] == '\v'))
		s++;
	return s;
}

static void write_line_content (size_t s, size_t e, int is_match, int nmatches)
{
	s = trim_prefix (s, e);
	if (O.max_columns > 0 && (long long) (e - s) > O.max_columns)
	{
		if (O.max_columns_preview)
		{
			/* les max_columns premiers caracteres (graphemes ~ points de code), puis le reste compte */
			if (e > s && S.F[e - 1] == '\n')
				e--;
			size_t k = s;
			long cnt = 0;
			while (k < e && cnt < O.max_columns)
			{
				k++;
				while (k < e && ((unsigned char) S.F[k] & 0xC0) == 0x80)
					k++;
				cnt++;
			}
			pw (S.F + s, k - s);
			if (!nmatches || !needs_granularity ())
				pws (" [... omitted end of long line]");
			else
			{
				int rem = 0;
				for (int i = 0; i < S.mn; i++)
					if (S.mv[i].s >= k && S.mv[i].s < e)
						rem++;
				char b[64];
				snprintf (b, sizeof b, " [... %d more %s]", rem, rem == 1 ? "match" : "matches");
				pws (b);
			}
			pwc ('\n');
			return;
		}
		if (nmatches > 0 && needs_granularity ())
		{
			char b[64];
			snprintf (b, sizeof b, "[Omitted long line with %d matches]", nmatches);
			pws (b);
		}
		else
			pws (is_match ? "[Omitted long matching line]" : "[Omitted long context line]");
		pwc ('\n');
		return;
	}
	pw (S.F + s, e - s);
	if (e == s || S.F[e - 1] != '\n')
		pwc ('\n');
}

static void std_line (size_t ls, size_t le, unsigned long long lineno, int is_match)
{
	write_search_prelude ();
	if (is_match && (O.only_matching || O.vimgrep))
	{
		for (int i = 0; i < S.mn; i++)
		{
			size_t ms = S.mv[i].s, me = S.mv[i].e;
			/* le numero et la colonne de la ligne ou commence la correspondance */
			unsigned long long n = lineno;
			size_t lstart = ls;
			for (size_t k = ls; k < ms; k++)
				if (S.F[k] == '\n')
				{
					n++;
					lstart = k + 1;
				}
			if (O.vimgrep)
			{
				write_prelude (1, n, O.column || 1 ? (long long) (ms - lstart + 1) : -1, ms);
				size_t lend = lstart;
				while (lend < S.n && S.F[lend] != '\n')
					lend++;
				if (lend < S.n)
					lend++;
				write_line_content (lstart, lend, 1, 1);
				continue;
			}
			if (O.multiline)
			{
				/* une correspondance sur plusieurs lignes : un morceau par ligne, chacun avec son prefixe */
				size_t a = ms;
				while (a < me)
				{
					const char *nl = memchr (S.F + a, '\n', me - a);
					size_t b = nl ? (size_t) (nl - S.F) : me;
					if (b > a)
					{
						write_prelude (1, n, O.column ? (long long) (a - lstart + 1) : -1, a);
						pw (S.F + a, b - a);
						pwc ('\n');
					}
					if (!nl)
						break;
					a = b + 1;
					lstart = a;
					n++;
				}
				continue;
			}
			write_prelude (1, n, O.column ? (long long) (ms - lstart + 1) : -1, ms);
			size_t e = me;
			if (O.max_columns > 0 && (long long) (e - ms) > O.max_columns)
			{
				pws ("[Omitted long line with 1 matches]\n");
				continue;
			}
			pw (S.F + ms, e - ms);
			if (e == ms || S.F[e - 1] != '\n')
				pwc ('\n');
		}
		return;
	}
	if (O.multiline && is_match)
	{
		/* un bloc de lignes : chacune avec son numero */
		size_t p = ls;
		unsigned long long n = lineno;
		while (p < le)
		{
			const char *nl = memchr (S.F + p, '\n', le - p);
			size_t e = nl ? (size_t) (nl - S.F) + 1 : le;
			long long col = -1;
			if (O.column)
				for (int i = 0; i < S.mn; i++)
					if (S.mv[i].e > p && S.mv[i].s < e)
					{
						col = (long long) ((S.mv[i].s > p ? S.mv[i].s : p) - p + 1);
						break;
					}
			write_prelude (1, n, col, p);
			int cnt = 0;
			for (int i = 0; i < S.mn; i++)
				if (S.mv[i].e > p && S.mv[i].s < e)
					cnt++;
			write_line_content (p, e, 1, cnt);
			p = e;
			n++;
		}
		return;
	}
	long long col = -1;
	if (O.column && is_match && S.mn > 0)
		col = (long long) (S.mv[0].s - ls + 1);
	write_prelude (is_match, lineno, col, ls);
	write_line_content (ls, le, is_match, is_match ? S.mn : 0);
}

/* ---- --json ---- */

static void json_begin (void)
{
	if (S.begin_printed)
		return;
	S.begin_printed = 1;
	sbuf b = { 0 };
	sb_puts (&b, "{\"type\":\"begin\",\"data\":{\"path\":");
	json_str (&b, S.path, S.pathlen);
	sb_puts (&b, "}}\n");
	pw (b.p, b.n);
	sb_free (&b);
}

static void json_line (const char *type, size_t ls, size_t le, unsigned long long lineno, int with_matches)
{
	sbuf b = { 0 };
	sb_printf (&b, "{\"type\":\"%s\",\"data\":{\"path\":", type);
	json_str (&b, S.path, S.pathlen);
	sb_puts (&b, ",\"lines\":");
	json_str (&b, S.F + ls, le - ls);
	if (O.line_number)
		sb_printf (&b, ",\"line_number\":%llu", lineno);
	else
		sb_puts (&b, ",\"line_number\":null");
	sb_printf (&b, ",\"absolute_offset\":%llu,\"submatches\":[", (unsigned long long) ls);
	if (with_matches)
		for (int i = 0; i < S.mn; i++)
		{
			if (i)
				sb_putc (&b, ',');
			sb_puts (&b, "{\"match\":");
			json_str (&b, S.F + S.mv[i].s, S.mv[i].e - S.mv[i].s);
			sb_printf (&b, ",\"start\":%llu,\"end\":%llu}", (unsigned long long) (S.mv[i].s - ls),
				   (unsigned long long) (S.mv[i].e - ls));
		}
	sb_puts (&b, "]}}\n");
	pw (b.p, b.n);
	sb_free (&b);
}

static void human (sbuf *b, unsigned long long ns)
{
	sb_printf (b, "%.6fs", ns / 1e9);
}

static unsigned long long now_ns (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned long long) t.tv_sec * 1000000000ULL + t.tv_nsec;
}

/* ---- les puits (Sink) ---- */

static int max_reached (void) { return O.max_count >= 0 && S.match_count >= O.max_count; }

static int sink_matched (size_t ls, size_t le, unsigned long long lineno)
{
	if (S.sink == 1)
	{
		/* resume (-c, -l, -q...) */
		long long cnt = 1;
		int need_count = O.multiline || O.mode == MODE_COUNT_MATCHES || O.stats;
		if (need_count)
		{
			if (O.invert)
				S.mn = 0;
			else
				record_matches (ls, le);
			cnt = S.mn;
		}
		if (O.multiline)
			S.match_count += cnt;
		else
			S.match_count += 1;
		S.st_matches += O.invert ? 0 : (unsigned long long) cnt;
		S.st_lines++;
		int quit_early = O.mode == MODE_FILES_WITH || O.mode == MODE_FILES_WITHOUT || O.quiet;
		if (!O.stats && quit_early)
			return 0;
		return !max_reached ();
	}
	S.match_count++;
	if (O.max_count >= 0 && S.match_count > O.max_count)
	{
		if (S.after_remaining > 0)
			S.after_remaining--;
	}
	else
		S.after_remaining = O.after;
	if (O.invert)
		S.mn = 0;
	else
		record_matches (ls, le);
	S.st_matches += S.mn;
	{
		unsigned long long nl = 0;
		for (size_t k = ls; k < le; k++)
			if (S.F[k] == '\n')
				nl++;
		if (le > ls && S.F[le - 1] != '\n')
			nl++;
		S.st_lines += nl ? nl : 1;
	}
	if (S.sink == 2)
	{
		json_begin ();
		json_line ("match", ls, le, lineno, 1);
	}
	else
	{
		if (S.convert_mode && S.bin_off >= 0)
			return 0;
		std_line (ls, le, lineno, 1);
		out_maybe_flush ();
	}
	return !(max_reached () && S.after_remaining == 0);
}

static int sink_context (size_t ls, size_t le, unsigned long long lineno, int after)
{
	if (S.sink == 1)
		return 1;
	if (after && S.after_remaining > 0)
		S.after_remaining--;
	if (S.sink == 2)
	{
		json_begin ();
		if (O.invert)
			record_matches (ls, le);
		json_line ("context", ls, le, lineno, O.invert);
	}
	else
	{
		if (S.convert_mode && S.bin_off >= 0)
			return 0;
		S.mn = 0;
		if (O.invert && needs_granularity ())
			record_matches (ls, le);
		std_line (ls, le, lineno, 0);
		out_maybe_flush ();
	}
	return !(max_reached () && S.after_remaining == 0);
}

static void sink_context_break (void)
{
	if (S.sink == 0 && !O.no_context_sep)
	{
		write_search_prelude ();
		pws (O.context_sep);
		pwc ('\n');
	}
}

static void sink_finish (unsigned long long bytes_searched)
{
	if (S.sink == 0)
	{
		if (S.bin_off >= 0 && S.match_count > 0)
		{
			if (S.show_path)
			{
				pw (S.path, S.pathlen);
				pws (": ");
			}
			char b[160];
			if (S.quit_mode)
				snprintf (b, sizeof b, "WARNING: stopped searching binary file after match (found \"\\0\" byte around offset %lld)\n", S.bin_off);
			else
				snprintf (b, sizeof b, "binary file matches (found \"\\0\" byte around offset %lld)\n", S.bin_off);
			pws (b);
		}
	}
	else if (S.sink == 1)
	{
		if (S.bin_off >= 0 && S.quit_mode)
		{
			S.match_count = 0;
			return;
		}
		int show = O.include_zero || S.match_count > 0;
		switch (O.mode)
		{
		case MODE_COUNT:
		case MODE_COUNT_MATCHES:
			if (show)
			{
				if (S.show_path)
				{
					pw (S.path, S.pathlen);
					if (O.null)
						pwc ('\0');
					else
						pws (O.field_match_sep);
				}
				pwnum (O.mode == MODE_COUNT ? (unsigned long long) S.match_count : S.st_matches);
				pwc ('\n');
			}
			break;
		case MODE_FILES_WITH:
			if (S.match_count > 0)
			{
				pw (S.path, S.pathlen);
				pwc (O.null ? '\0' : '\n');
			}
			break;
		case MODE_FILES_WITHOUT:
			if (S.match_count == 0)
			{
				pw (S.path, S.pathlen);
				pwc (O.null ? '\0' : '\n');
			}
			break;
		}
	}
	else
	{
		if (!S.begin_printed)
			return;
		unsigned long long el = now_ns () - ((unsigned long long) S.t0.tv_sec * 1000000000ULL + S.t0.tv_nsec);
		unsigned long long printed = S.file_written;
		T.elapsed_ns += el;
		T.searches++;
		if (S.match_count > 0)
			T.with_match++;
		T.bytes_searched += bytes_searched;
		T.bytes_printed += printed;
		T.matched_lines += S.st_lines;
		T.matches += S.st_matches;
		sbuf b = { 0 };
		sb_puts (&b, "{\"type\":\"end\",\"data\":{\"path\":");
		json_str (&b, S.path, S.pathlen);
		if (S.bin_off >= 0)
			sb_printf (&b, ",\"binary_offset\":%lld", S.bin_off);
		else
			sb_puts (&b, ",\"binary_offset\":null");
		sb_printf (&b, ",\"stats\":{\"elapsed\":{\"secs\":%llu,\"nanos\":%llu,\"human\":\"", el / 1000000000ULL,
			   el % 1000000000ULL);
		human (&b, el);
		sb_printf (&b, "\"},\"searches\":1,\"searches_with_match\":%d,\"bytes_searched\":%llu,\"bytes_printed\":%llu,"
			   "\"matched_lines\":%llu,\"matches\":%llu}}}\n", S.match_count > 0, bytes_searched, printed,
			   S.st_lines, S.st_matches);
		pw (b.p, b.n);
		sb_free (&b);
	}
}

/* ---- le coeur (grep-searcher core.rs) ---- */

static void count_lines (size_t upto)
{
	if (upto <= S.ln_pos)
		return;
	const char *p = S.F + S.ln_pos, *e = S.F + upto;
	while ((p = memchr (p, '\n', e - p)) != 0)
	{
		S.ln++;
		p++;
	}
	S.ln_pos = upto;
}

/* 1 : arreter (detection binaire) */
static int detect_binary (size_t s, size_t e)
{
	if (S.bin_off >= 0)
		return S.quit_mode;
	if (!S.quit_mode && !S.convert_mode)
		return 0;
	const char *z = memchr (S.F + s, 0, e - s);
	if (!z)
		return 0;
	S.bin_off = z - S.F;
	return S.quit_mode;
}

static int core_break_context (size_t start)
{
	int any = O.before > 0 || O.after > 0;
	if (!any || !S.has_sunk || !(S.last_line_visited < start))
		return 1;
	sink_context_break ();
	return 1;
}

static int core_matched (size_t ls, size_t le)
{
	if (S.core_binary && detect_binary (ls, le))
		return 0;
	if (!core_break_context (ls))
		return 0;
	count_lines (ls);
	if (!sink_matched (ls, le, S.ln + 1))
		return 0;
	S.last_line_visited = le;
	S.after_left = O.after;
	S.has_sunk = 1;
	return 1;
}

static int core_before_ctx_line (size_t ls, size_t le)
{
	if (S.core_binary && detect_binary (ls, le))
		return 0;
	count_lines (ls);
	if (!sink_context (ls, le, S.ln + 1, 0))
		return 0;
	S.last_line_visited = le;
	S.has_sunk = 1;
	return 1;
}

static int core_after_ctx_line (size_t ls, size_t le)
{
	if (S.core_binary && detect_binary (ls, le))
		return 0;
	count_lines (ls);
	if (!sink_context (ls, le, S.ln + 1, 1))
		return 0;
	S.last_line_visited = le;
	S.after_left--;
	S.has_sunk = 1;
	return 1;
}

static size_t next_line_end (size_t p, size_t end)
{
	const char *nl = memchr (S.F + p, '\n', end - p);
	return nl ? (size_t) (nl - S.F) + 1 : end;
}

static size_t preceding (size_t base, size_t pos, long count)
{
	/* lines::preceding_by_pos sur S.F[base..pos) ; rend une position absolue */
	if (pos == base)
		return base;
	if (S.F[pos - 1] == '\n')
		pos--;
	for (;;)
	{
		const char *z = 0;
		for (size_t k = pos; k > base; k--)
			if (S.F[k - 1] == '\n')
			{
				z = S.F + k - 1;
				break;
			}
		if (!z)
			return base;
		size_t i = z - S.F;
		if (count == 0)
			return i + 1;
		if (i == base)
			return base;
		count--;
		pos = i;
	}
}

static int before_context_by_line (size_t upto)
{
	if (O.before == 0)
		return 1;
	if (S.last_line_visited >= upto)
		return 1;
	size_t start = preceding (S.last_line_visited, upto, O.before - 1);
	size_t p = start;
	while (p < upto)
	{
		size_t e = next_line_end (p, upto);
		if (!core_break_context (p))
			return 0;
		if (!core_before_ctx_line (p, e))
			return 0;
		p = e;
	}
	return 1;
}

static int after_context_by_line (size_t upto)
{
	if (S.after_left == 0)
		return 1;
	size_t p = S.last_line_visited;
	while (p < upto)
	{
		size_t e = next_line_end (p, upto);
		if (!core_after_ctx_line (p, e))
			return 0;
		if (S.after_left == 0)
			break;
		p = e;
	}
	return 1;
}

/* la prochaine ligne qui correspond dans [pos, end) (find_by_line_fast) ; 0 si aucune */
static int find_by_line (size_t pos, size_t end, size_t *ls, size_t *le)
{
	while (pos < end)
	{
		size_t ms, me;
		if (!rx_find (S.F, end, pos, &ms, &me))
			return 0;
		size_t s = ms;
		while (s > pos && S.F[s - 1] != '\n')
			s--;
		size_t e = next_line_end (ms, end);
		if (s == end)
			return 0;
		/* confirmation sur la ligne seule (sans sa fin) */
		size_t t = line_end_noterm (s, e);
		size_t a, b;
		if (rx_find (S.F + s, t - s, 0, &a, &b))
		{
			*ls = s;
			*le = e;
			return 1;
		}
		pos = e;
	}
	return 0;
}

static size_t s_pos;		/* core.pos */

/* match_by_line_fast sur [s_pos, end) ; 0 : arreter */
static int match_by_line (size_t end)
{
	while (s_pos < end)
	{
		if (O.invert)
		{
			size_t ls, le, is, ie;
			if (find_by_line (s_pos, end, &ls, &le))
			{
				is = s_pos;
				ie = ls;
				s_pos = le;
			}
			else
			{
				is = s_pos;
				ie = end;
				s_pos = end;
			}
			if (is == ie)
				continue;
			if (!after_context_by_line (is))
				return 0;
			if (!before_context_by_line (is))
				return 0;
			size_t p = is;
			while (p < ie)
			{
				size_t e = next_line_end (p, ie);
				if (!core_matched (p, e))
					return 0;
				p = e;
			}
			continue;
		}
		size_t ls, le;
		if (!find_by_line (s_pos, end, &ls, &le))
			break;
		if (O.after > 0 || O.before > 0)
		{
			if (!after_context_by_line (ls))
				return 0;
			if (!before_context_by_line (ls))
				return 0;
		}
		s_pos = le;
		if (!core_matched (ls, le))
			return 0;
	}
	if (O.after > 0 || O.before > 0)
		if (!after_context_by_line (end))
			return 0;
	s_pos = end;
	return 1;
}

/* -U : MultiLine de grep-searcher sur [0, n) */
static void locate (size_t ms, size_t me, size_t *ls, size_t *le)
{
	size_t s = ms;
	while (s > 0 && S.F[s - 1] != '\n')
		s--;
	*ls = s;
	if (me > s && S.F[me - 1] == '\n')
		*le = me;
	else
		*le = next_line_end (me, S.n);
}

static int ml_sink_context (size_t start)
{
	if (!after_context_by_line (start))
		return 0;
	if (!before_context_by_line (start))
		return 0;
	return 1;
}

static int ml_sink_matched (size_t ls, size_t le)
{
	if (ls == le)
		return 0;
	return core_matched (ls, le);
}

static void multiline_run (void)
{
	size_t n = S.n;
	int have_last = 0;
	size_t lls = 0, lle = 0;
	int keep = 1;
	s_pos = 0;
	while (s_pos < n && keep)
	{
		size_t ms, me;
		int found = rx_find (S.F, n, s_pos, &ms, &me);
		if (O.invert)
		{
			size_t is, ie;
			if (!found)
			{
				is = s_pos;
				ie = n;
				s_pos = n;
			}
			else
			{
				size_t ls, le;
				locate (ms, me, &ls, &le);
				is = s_pos;
				ie = ls;
				s_pos = (ls == le) ? le + 1 : le;
			}
			if (is >= ie)
				continue;
			if (!ml_sink_context (is))
			{
				keep = 0;
				break;
			}
			size_t p = is;
			while (p < ie)
			{
				size_t e = next_line_end (p, ie);
				if (!ml_sink_matched (p, e))
				{
					keep = 0;
					break;
				}
				p = e;
			}
			continue;
		}
		if (!found)
		{
			s_pos = n;
			break;
		}
		s_pos = (ms == me) ? me + 1 : me;
		size_t ls, le;
		locate (ms, me, &ls, &le);
		if (!have_last)
		{
			have_last = 1;
			lls = ls;
			lle = le;
			continue;
		}
		if (lle > ls)
		{
			lle = le;
			continue;
		}
		size_t pls = lls, ple = lle;
		lls = ls;
		lle = le;
		if (!ml_sink_context (pls) || !ml_sink_matched (pls, ple))
			keep = 0;
	}
	if (keep && have_last)
		keep = ml_sink_context (lls) && ml_sink_matched (lls, lle);
	if (keep)
		after_context_by_line (n);
}

/* ---- lecture ---- */

static int read_all (int fd, char **buf, size_t *len, size_t hint)
{
	size_t cap = hint + 1 > 4096 ? hint + 1 : 4096, n = 0;
	char *b = xmalloc (cap + 1);
	for (;;)
	{
		if (n == cap)
		{
			cap *= 2;
			b = xrealloc (b, cap + 1);
		}
		ssize_t r = read (fd, b + n, cap - n);
		if (r < 0)
		{
			if (errno == EINTR)
				continue;
			int e = errno;
			free (b);
			errno = e;
			return -1;
		}
		if (r == 0)
			break;
		n += r;
	}
	b[n] = 0;
	*buf = b;
	*len = n;
	return 0;
}

/* la lecture par tampons de grep-searcher (LineBuffer, glue ReadByLine) : cherche fenetre par fenetre */
static unsigned long long read_by_line (void)
{
	size_t abs = 0, end = 0, cap = BUFCAP;	/* tampon = F[abs, abs+end) */
	size_t pos = 0;				/* deja cherche dans le tampon (relatif) */
	size_t n = S.n;
	int binary_seen = 0;
	long max_ctx = O.after > O.before ? O.after : O.before;
	for (;;)
	{
		/* roll : ce qui a ete consomme sort du tampon */
		size_t consumed;
		if (max_ctx == 0)
			consumed = pos;
		else
		{
			size_t cs = preceding (abs, abs + pos, max_ctx) - abs;
			size_t lv = S.last_line_visited > abs ? S.last_line_visited - abs : 0;
			consumed = cs > lv ? cs : lv;
			if (consumed > pos)
				consumed = pos;
		}
		size_t old_len = pos;	/* (longueur de buffer() avant) */
		abs += consumed;
		end -= consumed;
		size_t keep = pos - consumed;	/* deja cherche, garde pour le contexte */
		/* fill */
		if (S.quit_mode && binary_seen)
			break;
		size_t last_term = 0;
		int got = 0, eof = 0;
		for (;;)
		{
			if (end >= cap)
				cap *= 2;
			size_t avail = n - (abs + end);
			size_t readlen = cap - end < avail ? cap - end : avail;
			if (readlen == 0)
			{
				last_term = end;
				eof = 1;
				got = end > 0;
				break;
			}
			size_t ns = abs + end;
			end += readlen;
			if (S.quit_mode)
			{
				const char *z = memchr (S.F + ns, 0, readlen);
				if (z)
				{
					end = (z - S.F) - abs;
					last_term = end;
					S.bin_off = z - S.F;
					binary_seen = 1;
					got = keep < end;
					break;
				}
			}
			else if (S.convert_mode)
			{
				char *z = memchr (S.F + ns, 0, readlen);
				if (z)
				{
					if (S.bin_off < 0)
						S.bin_off = z - S.F;
					for (char *q = z; q < S.F + ns + readlen; q++)
						if (!*q)
							*q = '\n';
				}
			}
			const char *nl = 0;
			for (size_t k = abs + end; k > ns; k--)
				if (S.F[k - 1] == '\n')
				{
					nl = S.F + k - 1;
					break;
				}
			if (nl)
			{
				last_term = (nl - S.F) + 1 - abs;
				got = 1;
				break;
			}
		}
		/* glue fill() */
		if (binary_seen && S.quit_mode)
			break;
		if (!got)
			break;
		if (consumed == 0 && old_len == last_term && eof)
			break;
		/* recherche sur [abs+keep, abs+last_term) */
		s_pos = abs + keep;
		if (!match_by_line (abs + last_term))
			return abs;
		pos = last_term;
		if (eof)
			return n;	/* (fin normale : tout est consomme) */
	}
	if (!binary_seen)
		return n;
	return abs;
}

/* -U sur un fichier du parcours ou l'entree standard : grep-searcher lit tout par tampons ; avec un NUL
   (mode quit), ce qui precede le tampon ou il apparait ; en mode convert, les NUL deviennent des fins de ligne */
static size_t reader_limit (void)
{
	size_t abs = 0, end = 0, cap = BUFCAP, n = S.n;
	for (;;)
	{
		size_t last_term = 0;
		for (;;)
		{
			if (end >= cap)
				cap *= 2;
			size_t avail = n - (abs + end);
			size_t readlen = cap - end < avail ? cap - end : avail;
			if (readlen == 0)
				return n;
			size_t ns = abs + end;
			end += readlen;
			char *z = memchr (S.F + ns, 0, readlen);
			if (z && S.quit_mode)
			{
				S.bin_off = z - S.F;
				return abs;
			}
			if (z && S.convert_mode)
			{
				if (S.bin_off < 0)
					S.bin_off = z - S.F;
				for (char *q = z; q < S.F + ns + readlen; q++)
					if (!*q)
						*q = '\n';
			}
			const char *nl = 0;
			for (size_t k = abs + end; k > ns; k--)
				if (S.F[k - 1] == '\n')
				{
					nl = S.F + k - 1;
					break;
				}
			if (nl)
			{
				last_term = (nl - S.F) + 1 - abs;
				break;
			}
		}
		abs += last_term;
		end -= last_term;
	}
}

static unsigned long long slice_search (void)
{
	/* SliceByLine / MultiLine : detection sur les 64 premiers Kio */
	size_t upto = S.n < BUFCAP ? S.n : BUFCAP;
	if (S.quit_mode || S.convert_mode)
	{
		const char *z = memchr (S.F, 0, upto);
		if (z)
		{
			S.bin_off = z - S.F;
			if (S.quit_mode)
				return 0;
		}
	}
	if (O.multiline)
		multiline_run ();
	else
	{
		s_pos = 0;
		match_by_line (S.n);
	}
	if (S.bin_off >= 0 && S.convert_mode && !O.multiline)
		return (unsigned long long) S.bin_off;
	return S.n;
}

int search_file (const struct target *t)
{
	if (O.mode == MODE_FILES)
	{
		sb_puts (&g_out, t->path);
		sb_putc (&g_out, O.null ? '\0' : '\n');
		out_maybe_flush ();
		g_files_listed++;
		return 1;
	}
	char *buf;
	size_t len;
	if (t->is_stdin)
	{
		if (read_all (0, &buf, &len, 0) < 0)
		{
			rg_err ("<stdin>: %s (os error %d)", strerror (errno), errno);
			g_errored = 1;
			return 0;
		}
	}
	else
	{
		int fd = open (t->path, O_RDONLY);
		if (fd < 0)
		{
			rg_err ("%s: %s (os error %d)", t->path, strerror (errno), errno);
			g_errored = 1;
			return 0;
		}
		struct stat st;
		size_t hint = 0;
		if (fstat (fd, &st) == 0)
		{
			if (S_ISDIR (st.st_mode))
			{
				close (fd);
				return 0;
			}
			hint = st.st_size;
		}
		if (read_all (fd, &buf, &len, hint) < 0)
		{
			rg_err ("%s: %s (os error %d)", t->path, strerror (errno), errno);
			g_errored = 1;
			close (fd);
			return 0;
		}
		close (fd);
	}
	int mmn = S.mcap;
	struct m *mv = S.mv;
	memset (&S, 0, sizeof S);
	S.mv = mv;
	S.mcap = mmn;
	S.F = buf;
	S.n = len;
	S.path = t->path;
	S.pathlen = strlen (t->path);
	if (O.path_sep && O.path_sep != '/')
	{
		char *q = xstrdup (t->path);
		for (char *c = q; *c; c++)
			if (*c == '/')
				*c = O.path_sep;
		S.path = q;
	}
	S.show_path = O.with_filename;
	S.bin_off = -1;
	if (O.json)
		S.sink = 2;
	else if (O.mode == MODE_SEARCH && !O.quiet)
		S.sink = 0;
	else
		S.sink = 1;
	if (!O.text)
	{
		if (t->explicit || O.binary)
			S.convert_mode = 1;
		else
			S.quit_mode = 1;
	}
	clock_gettime (CLOCK_MONOTONIC, &S.t0);
	unsigned long long searched;
	int slice = t->explicit && !t->is_stdin && g_slice_explicit;
	S.core_binary = slice;
	if (O.max_count == 0)
		searched = 0;
	else if (slice)
		searched = slice_search ();
	else if (O.multiline)
	{
		size_t full = S.n;
		S.n = reader_limit ();
		searched = S.n;
		S.core_binary = 1;
		int qm = S.quit_mode;
		long long bo = S.bin_off;
		S.bin_off = -1;
		S.quit_mode = 0;	/* (le NUL est hors de la zone) */
		if (!qm)
			S.bin_off = bo;
		if (!(qm && bo >= 0 && S.n == 0))
			multiline_run ();
		S.quit_mode = qm;
		if (qm)
			S.bin_off = bo;
		S.n = full;
	}
	else
		searched = read_by_line ();
	size_t before_fin = S.file_written;
	sink_finish (searched);
	out_maybe_flush ();
	int matched = S.match_count > 0;
	if (O.stats)
	{
		ST.matches += S.st_matches;
		ST.lines += S.st_lines;
		ST.with_match += S.st_lines > 0;
		ST.searched++;
		ST.printed += S.sink == 0 ? S.file_written : S.sink == 2 ? before_fin : 0;
		ST.bytes += searched;
		ST.ns += now_ns () - ((unsigned long long) S.t0.tv_sec * 1000000000ULL + S.t0.tv_nsec);
	}
	if (S.path != t->path)
		free ((char *) S.path);
	free (buf);
	if (matched && O.quiet && !O.stats)
		g_quit_now = 1;
	return matched;
}

void search_finish (void)
{
	if (O.stats && !O.json)
	{
		unsigned long long tot = now_ns () - ((unsigned long long) s_start.tv_sec * 1000000000ULL + s_start.tv_nsec);
		sb_printf (&g_out, "\n%llu matches\n%llu matched lines\n%llu files contained matches\n%llu files searched\n"
			   "%llu bytes printed\n%llu bytes searched\n%.6f seconds spent searching\n%.6f seconds\n",
			   ST.matches, ST.lines, ST.with_match, ST.searched, ST.printed, ST.bytes, ST.ns / 1e9, tot / 1e9);
	}
	if (O.json)
	{
		unsigned long long tot = now_ns () - ((unsigned long long) s_start.tv_sec * 1000000000ULL + s_start.tv_nsec);
		sbuf b = { 0 };
		sb_puts (&b, "{\"data\":{\"elapsed_total\":{\"human\":\"");
		human (&b, tot);
		sb_printf (&b, "\",\"nanos\":%llu,\"secs\":%llu},\"stats\":{\"bytes_printed\":%llu,\"bytes_searched\":%llu,"
			   "\"elapsed\":{\"human\":\"", tot % 1000000000ULL, tot / 1000000000ULL, T.bytes_printed, T.bytes_searched);
		human (&b, T.elapsed_ns);
		sb_printf (&b, "\",\"nanos\":%llu,\"secs\":%llu},\"matched_lines\":%llu,\"matches\":%llu,\"searches\":%llu,"
			   "\"searches_with_match\":%llu}},\"type\":\"summary\"}\n", T.elapsed_ns % 1000000000ULL,
			   T.elapsed_ns / 1000000000ULL, T.matched_lines, T.matches, T.searches, T.with_match);
		sb_put (&g_out, b.p, b.n);
		sb_free (&b);
	}
}

void search_start_clock (void) { clock_gettime (CLOCK_MONOTONIC, &s_start); }
