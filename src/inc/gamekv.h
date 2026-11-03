//
// gamekv.h -- the little "[block] / key = value" text of the games in C++ (pinball's tables, scores; critters' levels,
// progress): a block's name may come many times, each header is one block; every entry keeps its line for the error
// messages. Lines starting with # or ; are comments.
//
#ifndef GAMEKV_H
#define GAMEKV_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

struct gkv_entry { char *key, *val; int block, line; };
struct gkv_block_t { char *name; int line; };
struct gkv { gkv_entry *e; int n, cap; gkv_block_t *b; int nb, capb; };

static inline char *gkv_ndup (const char *s, int n) { char *d = (char *) malloc ((size_t) n + 1); if (d) { memcpy (d, s, (size_t) n); d[n] = 0; } return d; }
static inline bool gkv_space (char c) { return c == ' ' || c == '\t'; }
static inline gkv *gkv_new () { return (gkv *) calloc (1, sizeof (gkv)); }
static inline int gkv_add_block (gkv *kv, const char *name, int n, int line)
{
	if (kv->nb == kv->capb) { kv->capb = kv->capb ? kv->capb * 2 : 32; kv->b = (gkv_block_t *) realloc (kv->b, sizeof (gkv_block_t) * (size_t) kv->capb); }
	kv->b[kv->nb].name = gkv_ndup (name, n); kv->b[kv->nb].line = line;
	return ++kv->nb;
}
static inline void gkv_insert (gkv *kv, int at, char *key, char *val, int block, int line)
{
	if (kv->n == kv->cap) { kv->cap = kv->cap ? kv->cap * 2 : 64; kv->e = (gkv_entry *) realloc (kv->e, sizeof (gkv_entry) * (size_t) kv->cap); }
	for (int i = kv->n; i > at; i--) kv->e[i] = kv->e[i - 1];
	kv->e[at].key = key; kv->e[at].val = val; kv->e[at].block = block; kv->e[at].line = line;
	kv->n++;
}
static inline gkv *gkv_parse (const char *text)
{
	gkv *kv = gkv_new (); if (!kv) return 0;
	const char *p = text ? text : "";
	int line = 0, block = 0;
	if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF) p += 3;
	while (*p)
	{
		const char *s = p, *e = p;
		while (*e && *e != '\n') e++;
		p = *e ? e + 1 : e;
		line++;
		int n = (int) (e - s);
		if (n && s[n - 1] == '\r') n--;
		while (n && gkv_space (*s)) { s++; n--; }
		if (!n || *s == '#' || *s == ';') continue;
		if (*s == '[')
		{
			const char *nm = s + 1; int nl = 0;
			while (nl < n - 1 && nm[nl] != ']') nl++;
			while (nl && gkv_space (*nm)) { nm++; nl--; }
			while (nl && gkv_space (nm[nl - 1])) nl--;
			block = gkv_add_block (kv, nm, nl, line);
			continue;
		}
		int eq = 0; while (eq < n && s[eq] != '=') eq++;
		if (eq == n) continue;
		int kl = eq; while (kl && gkv_space (s[kl - 1])) kl--;
		if (!kl) continue;
		int vs = eq + 1; while (vs < n && gkv_space (s[vs])) vs++;
		int vl = n; while (vl > vs && gkv_space (s[vl - 1])) vl--;
		gkv_insert (kv, kv->n, gkv_ndup (s, kl), gkv_ndup (s + vs, vl - vs), block, line);
	}
	return kv;
}
static inline void gkv_free (gkv *kv)
{
	if (!kv) return;
	for (int i = 0; i < kv->n; i++) { free (kv->e[i].key); free (kv->e[i].val); }
	for (int i = 0; i < kv->nb; i++) free (kv->b[i].name);
	free (kv->e); free (kv->b); free (kv);
}
static inline int gkv_count (const gkv *kv) { return kv->n; }
static inline const char *gkv_key (const gkv *kv, int i) { return i >= 0 && i < kv->n ? kv->e[i].key : ""; }
static inline const char *gkv_value (const gkv *kv, int i) { return i >= 0 && i < kv->n ? kv->e[i].val : ""; }
static inline int gkv_block (const gkv *kv, int i) { return i >= 0 && i < kv->n ? kv->e[i].block : 0; }
static inline int gkv_line (const gkv *kv, int i) { return i >= 0 && i < kv->n ? kv->e[i].line : 0; }
static inline int gkv_blocks (const gkv *kv) { return kv->nb; }
static inline const char *gkv_block_name (const gkv *kv, int b) { return b >= 1 && b <= kv->nb ? kv->b[b - 1].name : ""; }
static inline int gkv_block_line (const gkv *kv, int b) { return b >= 1 && b <= kv->nb ? kv->b[b - 1].line : 0; }
static inline int gkv_section (const gkv *kv, const char *section)
{
	for (int b = 1; b <= kv->nb; b++) if (!strcmp (kv->b[b - 1].name, section)) return b;
	return 0;
}
static inline const char *gkv_get (const gkv *kv, const char *section, const char *key, const char *def)
{
	int b = gkv_section (kv, section);
	if (b) for (int i = 0; i < kv->n; i++) if (kv->e[i].block == b && !strcmp (kv->e[i].key, key)) return kv->e[i].val;
	return def;
}
static inline void gkv_set (gkv *kv, const char *section, const char *key, const char *value)
{
	int b = gkv_section (kv, section);
	if (!b) b = gkv_add_block (kv, section, (int) strlen (section), 0);
	int at = kv->n;
	for (int i = 0; i < kv->n; i++)
	{
		if (kv->e[i].block != b) continue;
		if (!strcmp (kv->e[i].key, key)) { free (kv->e[i].val); kv->e[i].val = gkv_ndup (value, (int) strlen (value)); return; }
		at = i + 1;
	}
	gkv_insert (kv, at, gkv_ndup (key, (int) strlen (key)), gkv_ndup (value, (int) strlen (value)), b, 0);
}
static inline int gkv_remove (gkv *kv, const char *section, const char *key)
{
	int b = gkv_section (kv, section);
	if (b) for (int i = 0; i < kv->n; i++)
		if (kv->e[i].block == b && !strcmp (kv->e[i].key, key))
		{
			free (kv->e[i].key); free (kv->e[i].val);
			for (int j = i + 1; j < kv->n; j++) kv->e[j - 1] = kv->e[j];
			kv->n--;
			return 1;
		}
	return 0;
}
// the whole file (malloc, a 0 after it) -> 0: none, or over cap (*size = -1)
static inline char *gkv_read_file (const char *path, long *size, long cap = 1 << 24)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	struct stat st;
	if (fstat (f, &st) != 0 || st.st_size < 0 || st.st_size > cap) { close (f); if (size) *size = -1; return 0; }
	char *b = (char *) malloc ((size_t) st.st_size + 1);
	long got = 0;
	if (b) while (got < st.st_size) { long r = read (f, b + got, (size_t) (st.st_size - got)); if (r <= 0) break; got += r; }
	close (f);
	if (!b) return 0;
	b[got] = 0;
	if (size) *size = got;
	return b;
}
static inline gkv *gkv_load (const char *path)
{
	char *t = gkv_read_file (path, 0); if (!t) return 0;
	gkv *kv = gkv_parse (t); free (t);
	return kv;
}
static inline int gkv_save (gkv *kv, const char *path, const char *comment)
{
	size_t cap = 256; for (int i = 0; i < kv->n; i++) cap += strlen (kv->e[i].key) + strlen (kv->e[i].val) + 8;
	for (int i = 0; i < kv->nb; i++) cap += strlen (kv->b[i].name) + 8;
	char *b = (char *) malloc (cap); if (!b) return -1;
	int m = snprintf (b, cap, "# %s\n", comment);
	for (int k = 1; k <= kv->nb; k++)
	{
		m += snprintf (b + m, cap - (size_t) m, "[%s]\n", kv->b[k - 1].name);
		for (int i = 0; i < kv->n; i++) if (kv->e[i].block == k) m += snprintf (b + m, cap - (size_t) m, "%s = %s\n", kv->e[i].key, kv->e[i].val);
	}
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (f < 0) { free (b); return -1; }
	long r = write (f, b, (size_t) m);
	close (f); free (b);
	return r == m ? 0 : -1;
}
static inline const char *gkv_basename (const char *p)
{
	const char *b = p;
	for (; *p; p++) if (*p == '/' || *p == ':') b = p + 1;
	return b;
}
#endif
