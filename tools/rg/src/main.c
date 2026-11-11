/*
 * main.c -- rg pour Korin OS (docs/RG.md) : les options de ripgrep 14.1 (celles dont Claude Code se sert,
 * et les courantes), les valeurs par defaut qui dependent du terminal (numeros de ligne et titres par
 * fichier sur un tty, pas dans un tube), les codes de sortie (0 trouve, 1 rien, 2 erreur).
 */
#include "rg.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

struct opts O;

void search_start_clock (void);
int rx_jit_active (void);
int rx_jit_probe (void);

static void usage_err (const char *fmt, const char *a, const char *b)
{
	char tmp[512];
	snprintf (tmp, sizeof tmp, fmt, a, b);
	rg_err ("%s", tmp);
	out_flush ();
	exit (2);
}

static void push (char ***v, int *n, const char *s)
{
	*v = xrealloc (*v, (*n + 1) * sizeof **v);
	(*v)[(*n)++] = (char *) s;
}

static void add_pat (const char *s)
{
	push (&O.pats, &O.npats, xstrdup (s));
}

static void add_pat_file (const char *path)
{
	int fd = strcmp (path, "-") == 0 ? 0 : open (path, O_RDONLY);
	if (fd < 0)
	{
		rg_err ("%s: %s (os error %d)", path, strerror (errno), errno);
		exit (2);
	}
	sbuf b = { 0 };
	char tmp[8192];
	ssize_t r;
	while ((r = read (fd, tmp, sizeof tmp)) > 0)
		sb_put (&b, tmp, r);
	if (fd)
		close (fd);
	size_t i = 0;
	while (i < b.n)
	{
		char *nl = memchr (b.p + i, '\n', b.n - i);
		size_t e = nl ? (size_t) (nl - b.p) : b.n;
		size_t k = e;
		if (k > i && b.p[k - 1] == '\r')
			k--;
		char *s = xstrndup (b.p + i, k - i);
		push (&O.pats, &O.npats, s);
		i = e + 1;
	}
	sb_free (&b);
}

static long long parse_num (const char *flag, const char *v)
{
	char *e;
	if (!*v)
		usage_err ("error parsing flag %s: value is not a valid number: cannot parse integer from empty string", flag, 0);
	errno = 0;
	long long x = strtoll (v, &e, 10);
	if (*e || errno || x < 0)
		usage_err ("error parsing flag %s: value is not a valid number: invalid digit found in string", flag, 0);
	return x;
}

static long long parse_size (const char *flag, const char *v)
{
	char *e;
	errno = 0;
	long long x = strtoll (v, &e, 10);
	if (e == v || errno)
		usage_err ("error parsing flag %s: invalid size", flag, 0);
	if (*e == 'K' || *e == 'k') { x <<= 10; e++; }
	else if (*e == 'M' || *e == 'm') { x <<= 20; e++; }
	else if (*e == 'G' || *e == 'g') { x <<= 30; e++; }
	if (*e)
		usage_err ("error parsing flag %s: invalid size", flag, 0);
	return x;
}

static void print_version (void)
{
	sb_puts (&g_out, "ripgrep " RG_VERSION "\n\n");
	sb_puts (&g_out, "features:+pcre2\n");
	sb_printf (&g_out, "PCRE2 %d.%d is available (JIT is %savailable)\n", PCRE2_MAJOR, PCRE2_MINOR,
		   rx_jit_probe () ? "" : "un");
	sb_puts (&g_out, "Korin OS: rg en C sur PCRE2 (docs/RG.md)\n");
}

static void print_help (void)
{
	sb_puts (&g_out,
		 "ripgrep " RG_VERSION " (Korin OS : rg en C sur PCRE2, docs/RG.md)\n"
		 "Recursively search the current directory for lines matching a pattern.\n\n"
		 "USAGE:\n    rg [OPTIONS] PATTERN [PATH ...]\n    rg [OPTIONS] -e PATTERN ... [PATH ...]\n"
		 "    rg [OPTIONS] -f PATTERNFILE ... [PATH ...]\n    rg [OPTIONS] --files [PATH ...]\n"
		 "    rg [OPTIONS] --type-list\n\n"
		 "OPTIONS:\n"
		 "    -e, --regexp=PATTERN      A pattern to search for (repeatable)\n"
		 "    -f, --file=PATTERNFILE    Search for patterns from the given file, one per line\n"
		 "    -i, --ignore-case         Case insensitive search\n"
		 "    -S, --smart-case          Case insensitive unless the pattern has an uppercase letter\n"
		 "    -s, --case-sensitive      Case sensitive search (default)\n"
		 "    -F, --fixed-strings       Treat patterns as literal strings\n"
		 "    -w, --word-regexp         Only show matches surrounded by word boundaries\n"
		 "    -x, --line-regexp         Only show matches surrounded by line boundaries\n"
		 "    -v, --invert-match        Invert matching\n"
		 "    -U, --multiline           Enable matching across multiple lines\n"
		 "        --multiline-dotall    Make '.' match line terminators in multiline mode\n"
		 "    -m, --max-count=NUM       Limit the number of matching lines per file\n"
		 "    -A/-B/-C NUM              Show NUM lines after/before/around each match\n"
		 "    -n, --line-number         Show line numbers (default on a terminal)\n"
		 "    -N, --no-line-number      Suppress line numbers\n"
		 "        --column              Show column numbers\n"
		 "    -b, --byte-offset         Show the byte offset of each line\n"
		 "    -H, --with-filename       Print the file path with each match\n"
		 "    -I, --no-filename         Never print the file path\n"
		 "        --heading/--no-heading  Group matches under each file name\n"
		 "    -o, --only-matching       Print only the matched parts of a line\n"
		 "    -l, --files-with-matches  Print the paths with at least one match\n"
		 "        --files-without-match Print the paths with no match\n"
		 "    -c, --count               Show the number of matching lines per file\n"
		 "        --count-matches       Show the number of matches per file\n"
		 "    -q, --quiet               Do not print anything; exit 0 on the first match\n"
		 "        --json                Show search results in a JSON Lines format\n"
		 "    -0, --null                Follow file paths with a NUL byte\n"
		 "    -M, --max-columns=NUM     Omit lines longer than NUM bytes\n"
		 "        --max-columns-preview Print a preview of omitted lines\n"
		 "        --trim                Trim leading whitespace\n"
		 "        --vimgrep             Show every match on its own line, with line and column\n"
		 "        --files               Print each file that would be searched\n"
		 "    -g, --glob=GLOB           Include or (with !) exclude files and directories\n"
		 "        --iglob=GLOB          Like --glob, ignoring case\n"
		 "    -t, --type=TYPE           Only search files of TYPE (see --type-list)\n"
		 "    -T, --type-not=TYPE       Do not search files of TYPE\n"
		 "        --type-add=TYPE:GLOB  Add a file type definition\n"
		 "        --type-list           Show all supported file types\n"
		 "    -., --hidden              Search hidden files and directories\n"
		 "        --no-ignore           Do not respect .gitignore, .ignore, .rgignore\n"
		 "        --no-ignore-vcs       Do not respect .gitignore and .git/info/exclude\n"
		 "        --no-ignore-parent    Do not respect ignore files in parent directories\n"
		 "    -u, --unrestricted        -u: --no-ignore, -uu: + --hidden, -uuu: + --binary\n"
		 "    -L, --follow              Follow symbolic links\n"
		 "    -d, --max-depth=NUM       Descend at most NUM directories\n"
		 "        --max-filesize=SIZE   Ignore files larger than SIZE (K, M, G suffixes)\n"
		 "    -a, --text                Search binary files as if they were text\n"
		 "        --binary              Search binary files found while walking\n"
		 "        --sort=SORTBY         path, modified, accessed, created, none\n"
		 "        --sortr=SORTBY        Sort in reverse order\n"
		 "        --color=WHEN          never, auto (no color is ever written by this rg)\n"
		 "    -j, --threads=NUM         Accepted; this rg is single-threaded\n"
		 "        --no-messages         Suppress some error messages\n"
		 "        --line-buffered       Flush stdout after every line\n"
		 "    -V, --version             Print version information\n"
		 "    -h, --help                Print this help\n");
}

/* les options : nom long, court, prend une valeur */
enum
{
	F_REGEXP = 1, F_FILE, F_ICASE, F_SMART, F_CASESENS, F_FIXED, F_NOFIXED, F_WORD, F_LINEX, F_INVERT, F_NOINVERT,
	F_LN, F_NOLN, F_COLUMN, F_NOCOLUMN, F_WITHFN, F_NOFN, F_HEADING, F_NOHEADING, F_FILESWITH, F_FILESWITHOUT,
	F_COUNT, F_COUNTMATCHES, F_ONLY, F_FILES, F_AFTER, F_BEFORE, F_CONTEXT, F_MAXCOUNT, F_MULTILINE, F_NOMULTILINE,
	F_DOTALL, F_NODOTALL, F_MAXCOL, F_MAXCOLPREV, F_NOMAXCOLPREV, F_SORT, F_SORTR, F_SORTFILES, F_NULL, F_JSON,
	F_NOJSON, F_COLOR, F_GLOB, F_IGLOB, F_GLOBICASE, F_NOGLOBICASE, F_TYPE, F_TYPENOT, F_TYPELIST, F_TYPEADD,
	F_TYPECLEAR, F_HIDDEN, F_NOHIDDEN, F_NOIGNORE, F_IGNORE, F_NOIGNOREVCS, F_IGNOREVCS, F_NOIGNOREDOT,
	F_NOIGNOREPARENT, F_NOIGNOREGLOBAL, F_NOIGNOREEXCLUDE, F_NOIGNOREFILES, F_IGNOREFILE, F_IGNOREFILEICASE,
	F_NOREQUIREGIT, F_UNRESTRICTED, F_FOLLOW, F_NOFOLLOW, F_MAXDEPTH, F_MAXFILESIZE, F_TEXT, F_NOTEXT, F_BINARY,
	F_NOBINARY, F_QUIET, F_NOMESSAGES, F_MESSAGES, F_NOCONFIG, F_LINEBUF, F_BLOCKBUF, F_THREADS, F_VERSION, F_HELP,
	F_STATS, F_NOSTATS, F_DEBUG, F_PCRE2, F_NOPCRE2, F_ENGINE, F_VIMGREP, F_BYTEOFF, F_NOBYTEOFF, F_TRIM, F_NOTRIM,
	F_CRLF, F_NOCRLF, F_INCLUDEZERO, F_CTXSEP, F_NOCTXSEP, F_FMATCHSEP, F_FCTXSEP, F_PATHSEP, F_PRETTY,
	F_ONEFS, F_NOUNICODE, F_UNICODE, F_ENCODING, F_MMAP, F_NOMMAP, F_IGNORECASE_NO, F_REPLACE, F_PASSTHRU,
	F_COLORS, F_HIDDEN_DOT, F_SEARCHZIP, F_NOSEARCHZIP, F_NOHEADING_ALIAS,
};

struct flag
{
	const char *lng;
	char sh;
	int val;			/* prend une valeur */
	int id;
};

static const struct flag FLAGS[] = {
	{ "regexp", 'e', 1, F_REGEXP }, { "file", 'f', 1, F_FILE }, { "ignore-case", 'i', 0, F_ICASE },
	{ "smart-case", 'S', 0, F_SMART }, { "case-sensitive", 's', 0, F_CASESENS }, { "fixed-strings", 'F', 0, F_FIXED },
	{ "no-fixed-strings", 0, 0, F_NOFIXED }, { "word-regexp", 'w', 0, F_WORD }, { "line-regexp", 'x', 0, F_LINEX },
	{ "invert-match", 'v', 0, F_INVERT }, { "no-invert-match", 0, 0, F_NOINVERT }, { "line-number", 'n', 0, F_LN },
	{ "no-line-number", 'N', 0, F_NOLN }, { "column", 0, 0, F_COLUMN }, { "no-column", 0, 0, F_NOCOLUMN },
	{ "with-filename", 'H', 0, F_WITHFN }, { "no-filename", 'I', 0, F_NOFN }, { "heading", 0, 0, F_HEADING },
	{ "no-heading", 0, 0, F_NOHEADING }, { "files-with-matches", 'l', 0, F_FILESWITH },
	{ "files-without-match", 0, 0, F_FILESWITHOUT }, { "count", 'c', 0, F_COUNT },
	{ "count-matches", 0, 0, F_COUNTMATCHES }, { "only-matching", 'o', 0, F_ONLY }, { "files", 0, 0, F_FILES },
	{ "after-context", 'A', 1, F_AFTER }, { "before-context", 'B', 1, F_BEFORE }, { "context", 'C', 1, F_CONTEXT },
	{ "max-count", 'm', 1, F_MAXCOUNT }, { "multiline", 'U', 0, F_MULTILINE }, { "no-multiline", 0, 0, F_NOMULTILINE },
	{ "multiline-dotall", 0, 0, F_DOTALL }, { "no-multiline-dotall", 0, 0, F_NODOTALL },
	{ "max-columns", 'M', 1, F_MAXCOL }, { "max-columns-preview", 0, 0, F_MAXCOLPREV },
	{ "no-max-columns-preview", 0, 0, F_NOMAXCOLPREV }, { "sort", 0, 1, F_SORT }, { "sortr", 0, 1, F_SORTR },
	{ "sort-files", 0, 0, F_SORTFILES }, { "null", '0', 0, F_NULL }, { "json", 0, 0, F_JSON }, { "no-json", 0, 0, F_NOJSON },
	{ "color", 0, 1, F_COLOR }, { "colour", 0, 1, F_COLOR }, { "colors", 0, 1, F_COLORS }, { "glob", 'g', 1, F_GLOB },
	{ "iglob", 0, 1, F_IGLOB }, { "glob-case-insensitive", 0, 0, F_GLOBICASE },
	{ "no-glob-case-insensitive", 0, 0, F_NOGLOBICASE }, { "type", 't', 1, F_TYPE }, { "type-not", 'T', 1, F_TYPENOT },
	{ "type-list", 0, 0, F_TYPELIST }, { "type-add", 0, 1, F_TYPEADD }, { "type-clear", 0, 1, F_TYPECLEAR },
	{ "hidden", '.', 0, F_HIDDEN }, { "no-hidden", 0, 0, F_NOHIDDEN }, { "no-ignore", 0, 0, F_NOIGNORE },
	{ "ignore", 0, 0, F_IGNORE }, { "no-ignore-vcs", 0, 0, F_NOIGNOREVCS }, { "ignore-vcs", 0, 0, F_IGNOREVCS },
	{ "no-ignore-dot", 0, 0, F_NOIGNOREDOT }, { "no-ignore-parent", 0, 0, F_NOIGNOREPARENT },
	{ "no-ignore-global", 0, 0, F_NOIGNOREGLOBAL }, { "no-ignore-exclude", 0, 0, F_NOIGNOREEXCLUDE },
	{ "no-ignore-files", 0, 0, F_NOIGNOREFILES }, { "ignore-file", 0, 1, F_IGNOREFILE },
	{ "ignore-file-case-insensitive", 0, 0, F_IGNOREFILEICASE }, { "no-require-git", 0, 0, F_NOREQUIREGIT },
	{ "unrestricted", 'u', 0, F_UNRESTRICTED }, { "follow", 'L', 0, F_FOLLOW }, { "no-follow", 0, 0, F_NOFOLLOW },
	{ "max-depth", 'd', 1, F_MAXDEPTH }, { "maxdepth", 0, 1, F_MAXDEPTH }, { "max-filesize", 0, 1, F_MAXFILESIZE },
	{ "text", 'a', 0, F_TEXT }, { "no-text", 0, 0, F_NOTEXT }, { "binary", 0, 0, F_BINARY },
	{ "no-binary", 0, 0, F_NOBINARY }, { "quiet", 'q', 0, F_QUIET }, { "no-messages", 0, 0, F_NOMESSAGES },
	{ "messages", 0, 0, F_MESSAGES }, { "no-config", 0, 0, F_NOCONFIG }, { "line-buffered", 0, 0, F_LINEBUF },
	{ "block-buffered", 0, 0, F_BLOCKBUF }, { "threads", 'j', 1, F_THREADS }, { "version", 'V', 0, F_VERSION },
	{ "help", 'h', 0, F_HELP }, { "stats", 0, 0, F_STATS }, { "no-stats", 0, 0, F_NOSTATS }, { "debug", 0, 0, F_DEBUG },
	{ "trace", 0, 0, F_DEBUG }, { "pcre2", 'P', 0, F_PCRE2 }, { "no-pcre2", 0, 0, F_NOPCRE2 }, { "engine", 0, 1, F_ENGINE },
	{ "auto-hybrid-regex", 0, 0, F_PCRE2 }, { "no-auto-hybrid-regex", 0, 0, F_NOPCRE2 }, { "vimgrep", 0, 0, F_VIMGREP },
	{ "byte-offset", 'b', 0, F_BYTEOFF }, { "no-byte-offset", 0, 0, F_NOBYTEOFF }, { "trim", 0, 0, F_TRIM },
	{ "no-trim", 0, 0, F_NOTRIM }, { "crlf", 0, 0, F_CRLF }, { "no-crlf", 0, 0, F_NOCRLF },
	{ "include-zero", 0, 0, F_INCLUDEZERO }, { "context-separator", 0, 1, F_CTXSEP },
	{ "no-context-separator", 0, 0, F_NOCTXSEP }, { "field-match-separator", 0, 1, F_FMATCHSEP },
	{ "field-context-separator", 0, 1, F_FCTXSEP }, { "path-separator", 0, 1, F_PATHSEP }, { "pretty", 'p', 0, F_PRETTY },
	{ "one-file-system", 0, 0, F_ONEFS }, { "no-unicode", 0, 0, F_NOUNICODE }, { "unicode", 0, 0, F_UNICODE },
	{ "encoding", 'E', 1, F_ENCODING }, { "mmap", 0, 0, F_MMAP }, { "no-mmap", 0, 0, F_NOMMAP },
	{ "replace", 'r', 1, F_REPLACE }, { "passthru", 0, 0, F_PASSTHRU }, { "passthrough", 0, 0, F_PASSTHRU },
	{ "search-zip", 'z', 0, F_SEARCHZIP }, { "no-search-zip", 0, 0, F_NOSEARCHZIP },
	{ "no-pcre2-unicode", 0, 0, F_NOUNICODE }, { "pcre2-unicode", 0, 0, F_UNICODE },
	{ 0, 0, 0, 0 }
};

static int s_files_mode, s_typelist, s_count, s_count_matches, s_fwith, s_fwithout, s_unrestricted;
static int s_line_number = -1, s_with_filename = -1, s_heading = -1, s_color = 1;
static int s_pos_mode;		/* le premier argument libre est le motif */

/* « -e » : les valeurs de -e/-f et du motif lu ; rend 1 si un motif a ete donne par option */
static int s_pat_by_flag;

static int unescape_sep (const char *s, char **out)
{
	/* \n \t \\ \0 \xNN dans --context-separator & co */
	sbuf b = { 0 };
	for (; *s; s++)
	{
		if (*s == '\\' && s[1])
		{
			s++;
			switch (*s)
			{
			case 'n': sb_putc (&b, '\n'); break;
			case 't': sb_putc (&b, '\t'); break;
			case 'r': sb_putc (&b, '\r'); break;
			case '0': sb_putc (&b, '\0'); break;
			case '\\': sb_putc (&b, '\\'); break;
			case 'x':
				if (s[1] && s[2])
				{
					char h[3] = { s[1], s[2], 0 };
					sb_putc (&b, (char) strtol (h, 0, 16));
					s += 2;
					break;
				}
				/* fallthrough */
			default:
				sb_putc (&b, '\\');
				sb_putc (&b, *s);
			}
		}
		else
			sb_putc (&b, *s);
	}
	*out = b.p ? b.p : xstrdup ("");
	return 0;
}

static void apply (int id, const char *v, const char *name)
{
	switch (id)
	{
	case F_REGEXP: add_pat (v); s_pat_by_flag = 1; break;
	case F_FILE: add_pat_file (v); s_pat_by_flag = 1; break;
	case F_ICASE: O.case_mode = 1; break;
	case F_SMART: O.case_mode = 2; break;
	case F_CASESENS: O.case_mode = 0; break;
	case F_FIXED: O.fixed = 1; break;
	case F_NOFIXED: O.fixed = 0; break;
	case F_WORD: O.word = 1; O.line_x = 0; break;
	case F_LINEX: O.line_x = 1; O.word = 0; break;
	case F_INVERT: O.invert = 1; break;
	case F_NOINVERT: O.invert = 0; break;
	case F_LN: s_line_number = 1; break;
	case F_NOLN: s_line_number = 0; break;
	case F_COLUMN: O.column = 1; break;
	case F_NOCOLUMN: O.column = 0; break;
	case F_WITHFN: s_with_filename = 1; break;
	case F_NOFN: s_with_filename = 0; break;
	case F_HEADING: s_heading = 1; break;
	case F_NOHEADING: s_heading = 0; break;
	case F_FILESWITH: s_fwith = 1; s_fwithout = 0; break;
	case F_FILESWITHOUT: s_fwithout = 1; s_fwith = 0; break;
	case F_COUNT: s_count = 1; s_count_matches = 0; break;
	case F_COUNTMATCHES: s_count_matches = 1; s_count = 0; break;
	case F_ONLY: O.only_matching = 1; break;
	case F_FILES: s_files_mode = 1; break;
	case F_AFTER: O.after = parse_num (name, v); break;
	case F_BEFORE: O.before = parse_num (name, v); break;
	case F_CONTEXT: O.after = O.before = parse_num (name, v); break;
	case F_MAXCOUNT: O.max_count = parse_num (name, v); break;
	case F_MULTILINE: O.multiline = 1; break;
	case F_NOMULTILINE: O.multiline = 0; break;
	case F_DOTALL: O.dotall = 1; break;
	case F_NODOTALL: O.dotall = 0; break;
	case F_MAXCOL: O.max_columns = parse_num (name, v); break;
	case F_MAXCOLPREV: O.max_columns_preview = 1; break;
	case F_NOMAXCOLPREV: O.max_columns_preview = 0; break;
	case F_SORT: case F_SORTR:
		O.sort_reverse = id == F_SORTR;
		if (strcmp (v, "path") == 0) O.sort = SORT_PATH;
		else if (strcmp (v, "modified") == 0) O.sort = SORT_MODIFIED;
		else if (strcmp (v, "accessed") == 0) O.sort = SORT_ACCESSED;
		else if (strcmp (v, "created") == 0) O.sort = SORT_CREATED;
		else if (strcmp (v, "none") == 0) O.sort = SORT_NONE;
		else usage_err ("error parsing flag %s: choice '%s' is unrecognized", name, v);
		break;
	case F_SORTFILES: O.sort = SORT_PATH; O.sort_reverse = 0; break;
	case F_NULL: O.null = 1; break;
	case F_JSON: O.json = 1; break;
	case F_NOJSON: O.json = 0; break;
	case F_COLOR:
		if (strcmp (v, "never") == 0) s_color = 0;
		else if (strcmp (v, "auto") == 0) s_color = 1;
		else if (strcmp (v, "always") == 0 || strcmp (v, "ansi") == 0) s_color = 2;
		else usage_err ("error parsing flag %s: choice '%s' is unrecognized", name, v);
		break;
	case F_COLORS: break;
	case F_GLOB: case F_IGLOB:
		O.globs = xrealloc (O.globs, (O.nglobs + 1) * sizeof *O.globs);
		O.globs_icase = xrealloc (O.globs_icase, (O.nglobs + 1) * sizeof *O.globs_icase);
		O.globs[O.nglobs] = xstrdup (v);
		O.globs_icase[O.nglobs] = id == F_IGLOB;
		O.nglobs++;
		break;
	case F_GLOBICASE: O.glob_icase = 1; break;
	case F_NOGLOBICASE: O.glob_icase = 0; break;
	case F_TYPE: case F_TYPENOT:
		O.types = xrealloc (O.types, (O.ntypes + 1) * sizeof *O.types);
		O.types_neg = xrealloc (O.types_neg, (O.ntypes + 1) * sizeof *O.types_neg);
		O.types[O.ntypes] = xstrdup (v);
		O.types_neg[O.ntypes] = id == F_TYPENOT;
		O.ntypes++;
		break;
	case F_TYPELIST: s_typelist = 1; break;
	case F_TYPEADD: push (&O.type_adds, &O.ntype_adds, xstrdup (v)); break;
	case F_TYPECLEAR: push (&O.type_clears, &O.ntype_clears, xstrdup (v)); break;
	case F_HIDDEN: O.hidden = 1; break;
	case F_NOHIDDEN: O.hidden = 0; break;
	case F_NOIGNORE: O.no_ignore = 1; break;
	case F_IGNORE: O.no_ignore = 0; break;
	case F_NOIGNOREVCS: O.no_ignore_vcs = 1; break;
	case F_IGNOREVCS: O.no_ignore_vcs = 0; break;
	case F_NOIGNOREDOT: O.no_ignore_dot = 1; break;
	case F_NOIGNOREPARENT: O.no_ignore_parent = 1; break;
	case F_NOIGNOREGLOBAL: O.no_ignore_global = 1; break;
	case F_NOIGNOREEXCLUDE: O.no_ignore_exclude = 1; break;
	case F_NOIGNOREFILES: O.no_ignore_files = 1; break;
	case F_IGNOREFILE: push (&O.ignore_files, &O.nignore_files, xstrdup (v)); break;
	case F_IGNOREFILEICASE: O.ignore_file_icase = 1; break;
	case F_NOREQUIREGIT: O.no_require_git = 1; break;
	case F_UNRESTRICTED:
		s_unrestricted++;
		if (s_unrestricted == 1) O.no_ignore = 1;
		else if (s_unrestricted == 2) O.hidden = 1;
		else O.binary = 1;
		break;
	case F_FOLLOW: O.follow = 1; break;
	case F_NOFOLLOW: O.follow = 0; break;
	case F_MAXDEPTH: O.max_depth = parse_num (name, v); break;
	case F_MAXFILESIZE: O.max_filesize = parse_size (name, v); break;
	case F_TEXT: O.text = 1; break;
	case F_NOTEXT: O.text = 0; break;
	case F_BINARY: O.binary = 1; break;
	case F_NOBINARY: O.binary = 0; break;
	case F_QUIET: O.quiet = 1; break;
	case F_NOMESSAGES: g_no_messages = 1; break;
	case F_MESSAGES: g_no_messages = 0; break;
	case F_LINEBUF: g_line_buffered = 1; break;
	case F_BLOCKBUF: g_line_buffered = 0; break;
	case F_THREADS: parse_num (name, v); break;
	case F_VERSION: print_version (); out_flush (); exit (0);
	case F_HELP: print_help (); out_flush (); exit (0);
	case F_STATS: O.stats = 1; break;
	case F_PCRE2: O.pcre2 = 1; break;
	case F_NOPCRE2: O.pcre2 = 0; break;
	case F_ENGINE: O.pcre2 = strcmp (v, "pcre2") == 0; break;
	case F_NOSTATS: O.stats = 0; break;
	case F_VIMGREP: O.vimgrep = 1; break;
	case F_BYTEOFF: O.byte_offset = 1; break;
	case F_NOBYTEOFF: O.byte_offset = 0; break;
	case F_TRIM: O.trim = 1; break;
	case F_NOTRIM: O.trim = 0; break;
	case F_CRLF: O.crlf = 1; break;
	case F_NOCRLF: O.crlf = 0; break;
	case F_INCLUDEZERO: O.include_zero = 1; break;
	case F_CTXSEP: { char *s; unescape_sep (v, &s); O.context_sep = s; O.no_context_sep = 0; } break;
	case F_NOCTXSEP: O.no_context_sep = 1; break;
	case F_FMATCHSEP: { char *s; unescape_sep (v, &s); O.field_match_sep = s; } break;
	case F_FCTXSEP: { char *s; unescape_sep (v, &s); O.field_context_sep = s; } break;
	case F_PATHSEP:
		if (strlen (v) != 1)
			usage_err ("error parsing flag %s: A path separator must be exactly one byte, but the given separator is %s bytes", name, "several");
		O.path_sep = v[0];
		break;
	case F_PRETTY: s_heading = 1; s_line_number = 1; break;
	case F_REPLACE:
		usage_err ("error parsing flag %s: --replace is not supported by this rg (Korin OS, docs/RG.md)", name, 0);
		break;
	case F_PASSTHRU:
		usage_err ("error parsing flag %s: --passthru is not supported by this rg (Korin OS, docs/RG.md)", name, 0);
		break;
	case F_SEARCHZIP:
		usage_err ("error parsing flag %s: --search-zip is not supported by this rg (Korin OS, docs/RG.md)", name, 0);
		break;
	default:
		break;		/* acceptees sans effet : --no-config, -j, --debug, -P, --engine, --mmap, --encoding... */
	}
}

static const struct flag *find_long (const char *s, size_t n)
{
	for (const struct flag *f = FLAGS; f->lng; f++)
		if (strlen (f->lng) == n && memcmp (f->lng, s, n) == 0)
			return f;
	return 0;
}

static const struct flag *find_short (char c)
{
	for (const struct flag *f = FLAGS; f->lng; f++)
		if (f->sh == c)
			return f;
	return 0;
}

static void parse_args (int argc, char **argv)
{
	char **free_args = 0;
	int nfree = 0;
	int i;
	for (i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (strcmp (a, "--") == 0)
		{
			for (i++; i < argc; i++)
				push (&free_args, &nfree, argv[i]);
			break;
		}
		if (a[0] == '-' && a[1] == '-')
		{
			const char *eq = strchr (a + 2, '=');
			size_t n = eq ? (size_t) (eq - a - 2) : strlen (a + 2);
			const struct flag *f = find_long (a + 2, n);
			char name[128];
			snprintf (name, sizeof name, "--%.*s", (int) n, a + 2);
			if (!f)
				usage_err ("unrecognized flag %s", name, 0);
			const char *v = 0;
			if (f->val)
			{
				if (eq)
					v = eq + 1;
				else if (i + 1 < argc)
					v = argv[++i];
				else
					usage_err ("error parsing flag %s: missing value", name, 0);
			}
			else if (eq)
				usage_err ("error parsing flag %s: unexpected argument", name, 0);
			apply (f->id, v, name);
			continue;
		}
		if (a[0] == '-' && a[1])
		{
			for (int k = 1; a[k]; k++)
			{
				const struct flag *f = find_short (a[k]);
				char name[4] = { '-', a[k], 0 };
				if (!f)
				{
					char nm[8];
					snprintf (nm, sizeof nm, "-%c", a[k]);
					usage_err ("unrecognized flag %s", nm, 0);
				}
				if (f->val)
				{
					const char *v;
					if (a[k + 1])
						v = a[k + 1] == '=' ? a + k + 2 : a + k + 1;
					else if (i + 1 < argc)
						v = argv[++i];
					else
						usage_err ("error parsing flag %s: missing value", name, 0);
					apply (f->id, v, name);
					break;
				}
				apply (f->id, 0, name);
			}
			continue;
		}
		push (&free_args, &nfree, a);
	}
	O.mode = MODE_SEARCH;
	if (s_typelist)
		O.mode = MODE_TYPE_LIST;
	else if (s_files_mode)
		O.mode = MODE_FILES;
	else if (s_fwith)
		O.mode = MODE_FILES_WITH;
	else if (s_fwithout)
		O.mode = MODE_FILES_WITHOUT;
	else if (s_count_matches)
		O.mode = MODE_COUNT_MATCHES;
	else if (s_count)
		O.mode = MODE_COUNT;
	if (s_count && O.only_matching && O.mode == MODE_COUNT)
		O.mode = MODE_COUNT_MATCHES;	/* (-c -o : comme --count-matches) */
	int first = 0;
	if (O.mode != MODE_FILES && O.mode != MODE_TYPE_LIST && !s_pat_by_flag)
	{
		if (nfree == 0)
		{
			rg_err ("ripgrep requires at least one pattern to execute a search");
			out_flush ();
			exit (2);
		}
		add_pat (free_args[0]);
		first = 1;
	}
	for (int k = first; k < nfree; k++)
		push (&O.paths, &O.npaths, free_args[k]);
	(void) s_pos_mode;
}

int main (int argc, char **argv)
{
	O.line_number = -1;
	O.max_count = -1;
	O.max_depth = -1;
	O.max_filesize = -1;
	O.context_sep = "--";
	O.field_match_sep = ":";
	O.field_context_sep = "-";
	search_start_clock ();
	parse_args (argc, argv);

	int out_tty = isatty (1);
	int implicit_path = 0;
	if (O.npaths == 0 && O.mode != MODE_TYPE_LIST)
	{
		/* ni chemin ni terminal en entree : l'entree standard (un fichier ou un tube) */
		struct stat st;
		if (O.mode != MODE_FILES && fstat (0, &st) == 0 && (S_ISREG (st.st_mode) || S_ISFIFO (st.st_mode)) && !isatty (0))
			push (&O.paths, &O.npaths, "-");
		else
		{
			push (&O.paths, &O.npaths, "./");
			implicit_path = 1;
		}
	}
	/* les valeurs par defaut */
	O.color = s_color == 2 ? 2 : (s_color == 1 && out_tty ? 1 : 0);
	if (O.column && s_line_number < 0)
		s_line_number = 1;	/* (--column implique -n) */
	if (s_line_number >= 0)
		O.line_number = s_line_number;
	else
		O.line_number = (out_tty || O.json || O.vimgrep) && !(O.mode != MODE_SEARCH);
	if (O.vimgrep)
		O.column = 1;
	int single_file = 0;
	if (O.npaths == 1)
	{
		struct stat st;
		if (strcmp (O.paths[0], "-") == 0 || (stat (O.paths[0], &st) == 0 && !S_ISDIR (st.st_mode)))
			single_file = 1;
	}
	if (s_with_filename >= 0)
		O.with_filename = s_with_filename;
	else
		O.with_filename = !single_file || O.vimgrep;
	if (s_heading >= 0)
		O.heading = s_heading;
	else
		O.heading = out_tty && !O.vimgrep;
	if (O.vimgrep)
		O.heading = 0;
	if (O.json)
		O.heading = 0;
	/* fichiers donnes : lus d'un bloc s'ils sont au plus 10 et tous des fichiers (le mmap de ripgrep) */
	if (O.npaths <= 10)
	{
		int all_files = 1;
		for (int k = 0; k < O.npaths; k++)
		{
			struct stat st;
			if (strcmp (O.paths[k], "-") == 0 || stat (O.paths[k], &st) < 0 || !S_ISREG (st.st_mode))
				all_files = 0;
		}
		g_slice_explicit = all_files;
	}
	if (O.mode == MODE_TYPE_LIST)
	{
		int r = types_init ();
		out_flush ();
		return r;
	}
	if (types_init () || overrides_init ())
	{
		out_flush ();
		return 2;
	}
	if (O.mode != MODE_FILES && search_init ())
	{
		out_flush ();
		return 2;
	}
	/* le chemin implicite « ./ » s'affiche sans son « ./ » : on cherche « . » depuis le dossier courant */
	if (implicit_path)
		g_strip_dot = 1;
	int matched = walk_all ();
	search_finish ();
	out_flush ();
	if (O.mode == MODE_FILES)
		matched = g_files_listed > 0;
	if (matched && (O.quiet || !g_errored))
		return 0;
	if (g_errored)
		return 2;
	return 1;
}
