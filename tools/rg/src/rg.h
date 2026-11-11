/*
 * rg.h -- rg pour Korin OS : une reecriture en C de ripgrep 14.1 sur PCRE2 (docs/RG.md).
 * Les declarations communes aux fichiers de tools/rg/src.
 */
#ifndef RG_H
#define RG_H

#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#define PCRE2_STATIC
#include "pcre2.h"

#define RG_VERSION "14.1.0"

/* ---- util.c : tampons, sorties, erreurs ---- */
typedef struct
{
	char *p;
	size_t n, cap;
} sbuf;

void sb_grow (sbuf *b, size_t need);
void sb_put (sbuf *b, const void *s, size_t n);
void sb_putc (sbuf *b, char c);
void sb_puts (sbuf *b, const char *s);
void sb_printf (sbuf *b, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void sb_free (sbuf *b);
char *xstrdup (const char *s);
char *xstrndup (const char *s, size_t n);
void *xmalloc (size_t n);
void *xrealloc (void *p, size_t n);

extern sbuf g_out;			/* la sortie standard, videe par paquets */
extern int g_line_buffered;
void out_flush (void);
void out_maybe_flush (void);
void rg_err (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));	/* « rg: ... » sur stderr */
extern int g_errored;			/* une erreur (sortie 2) */
extern int g_no_messages;

/* ---- glob.c : motifs de fichiers (globset), fichiers d'exclusion (gitignore), surcharges (-g) ---- */
enum { M_NONE = 0, M_IGNORE = 1, M_WHITELIST = 2 };

struct glob_re
{
	pcre2_code *re;
	int kind;			/* G_REGEX, G_LITERAL_BASE, G_EXT, G_ALL */
	char *lit;			/* pour les formes rapides */
	size_t litlen;
};

/* traduit un glob en expression (globset) ; 0 si correct, sinon le message d'erreur (statique) */
const char *glob_compile (const char *glob, int literal_sep, int icase, int allow_unclosed, struct glob_re *out);
int glob_match (const struct glob_re *g, const char *s, size_t n);
void glob_free (struct glob_re *g);

struct gi_pat
{
	struct glob_re g;
	int whitelist, only_dir;
};

struct gi
{
	char *root;			/* le dossier ou il s'applique (« . » si aucun) */
	size_t rootlen;
	struct gi_pat *v;
	int n, cap;
	int icase;
};

void gi_init (struct gi *m, const char *root);
/* ajoute une ligne de gitignore ; 0, ou le message d'erreur */
const char *gi_add_line (struct gi *m, const char *line, size_t len, int allow_unclosed);
int gi_add_file (struct gi *m, const char *path);	/* 1 si le fichier existe */
int gi_matched (const struct gi *m, const char *path, int is_dir);
void gi_free (struct gi *m);

/* ---- types.c ---- */
struct rg_type
{
	const char *name;
	const char *globs;		/* motifs separes par \0, termines par \0\0 */
};
extern const struct rg_type rg_default_types[];

/* ---- main.c : les options ---- */
enum { MODE_SEARCH, MODE_FILES, MODE_TYPE_LIST, MODE_FILES_WITH, MODE_FILES_WITHOUT, MODE_COUNT, MODE_COUNT_MATCHES };
enum { SORT_NONE, SORT_PATH, SORT_MODIFIED, SORT_ACCESSED, SORT_CREATED };

struct opts
{
	char **pats;
	int npats, pats_cap;
	int fixed, word, line_x, invert, case_mode;	/* case_mode : 0 sensible, 1 -i, 2 -S */
	int multiline, dotall, crlf;
	int line_number;		/* -1 : par defaut */
	int column, with_filename, heading, null, json, vimgrep, byte_offset, trim;
	int mode, only_matching, quiet, include_zero, stats, passthru;
	long after, before;
	long long max_count;		/* -1 : sans limite */
	long max_columns, max_columns_preview;
	int sort, sort_reverse;
	int color;			/* 0 never, 1 auto, 2 always */
	int hidden, no_ignore, no_ignore_vcs, no_ignore_dot, no_ignore_parent, no_ignore_global, no_ignore_exclude;
	int no_ignore_files, ignore_file_icase, no_require_git;
	int follow, one_file_system;
	long max_depth;			/* -1 : sans limite */
	long long max_filesize;		/* -1 : sans limite */
	int text, binary;
	int pcre2;			/* -P : retours arriere et regards autour permis (sinon refuses, comme ripgrep) */
	int glob_icase;
	const char *context_sep, *field_match_sep, *field_context_sep;
	int no_context_sep;
	char path_sep;			/* 0 : tel quel */
	const char *replace;
	char **globs; int *globs_icase; int nglobs;
	char **types; int *types_neg; int ntypes;
	char **type_adds; int ntype_adds;
	char **type_clears; int ntype_clears;
	char **ignore_files; int nignore_files;
	char **paths; int npaths;
};
extern struct opts O;

/* ---- search.c ---- */
int search_init (void);			/* compile le motif ; 0 ou 2 */
struct target
{
	const char *path;		/* tel qu'affiche */
	int explicit;			/* donne sur la ligne de commande */
	int is_stdin;
};
int search_file (const struct target *t);	/* 1 s'il y a eu une correspondance */
void search_finish (void);		/* --json : le resume, --stats */
extern int g_quit_now;			/* -q : une correspondance suffit */
extern int g_slice_explicit;		/* <= 10 chemins explicites : lus d'un bloc (mmap de ripgrep) */
extern long long g_files_listed;

/* ---- walk.c ---- */
int walk_all (void);			/* 1 s'il y a eu une correspondance (ou un fichier pour --files) */
int types_init (void);			/* 0 ou 2 */
int overrides_init (void);		/* 0 ou 2 */
extern int g_strip_dot;			/* chemin implicite : « ./ » ote a l'affichage */

/* json */
void json_str (sbuf *b, const char *s, size_t n);		/* {"text":...} ou {"bytes":...} */
int utf8_valid (const unsigned char *s, size_t n);

#endif
