//
// kv.h -- Circuits' text documents: [section] headers and "key = value" lines, read and written back (a pack of levels,
// the player's progress). Onyx's FileKit has them (kvtext.h); ARMPI has no FileKit and Circuits is the only reader, so
// the code lives here, on open / read / write. The entries are kept in the file's order; each "[...]" line is a block of
// its own (a pack's [level]s); a value may hold new lines -- written "\n" (KV_ESCAPES: the progress) or as the lines that
// follow, each starting with "|" (KV_PIPES: a pack's tables); the line of every value is kept (an error's "line 42").
// Unknown keys are kept: the game writes back what it read with only its own keys changed.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_kv_h
#define _circuits_kv_h

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define KV_ESCAPES	1			// values may hold new lines and '\': written "\n", "\\", read back
#define KV_PIPES	2			// a value may go on in the lines that follow, each starting with "|"

struct kv_entry { char *key, *val; int block, line; };
struct kv_block { char *name; int line; };
struct kv_doc
{
	int flags;
	kv_entry *e; int n, cap;		// the entries, grouped by block in ascending order (the file's order)
	kv_block *b; int nb, capb;		// b[0]: the part before any header; b[1...]: the "[...]" lines
	char *text;				// kv_text's result
};

static inline bool kv_eq_ (const char *a, const char *b) { return !strcmp (a ? a : "", b ? b : ""); }
static inline char *kv_ndup_ (const char *s, int n)
{
	char *d = (char *) malloc ((size_t) n + 1);
	if (!d) return 0;
	memcpy (d, s, (size_t) n); d[n] = 0;
	return d;
}
static inline char *kv_dup_ (const char *s) { return kv_ndup_ (s ? s : "", s ? (int) strlen (s) : 0); }
static inline bool kv_space_ (char c) { return c == ' ' || c == '\t'; }
// A value read with KV_ESCAPES: "\n" -> a new line, "\\" -> '\' (in place; anything else kept)
static inline void kv_unescape_ (char *s)
{
	char *w = s;
	for (char *r = s; *r; r++)
	{
		if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
		else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r++; }
		else *w++ = *r;
	}
	*w = 0;
}
// A new block (its name copied) -> its number, -1 no memory
static inline int kv_add_block_ (kv_doc *kv, const char *name, int nlen, int line)
{
	if (kv->nb == kv->capb)
	{
		int c = kv->capb ? kv->capb * 2 : 16;
		kv_block *nb = (kv_block *) realloc (kv->b, (size_t) c * sizeof (kv_block));
		if (!nb) return -1;
		kv->b = nb; kv->capb = c;
	}
	char *nm = kv_ndup_ (name, nlen);
	if (!nm) return -1;
	kv->b[kv->nb].name = nm; kv->b[kv->nb].line = line;
	return kv->nb++;
}
// An entry put at index `at` (key / value taken, not copied) -> 0, -1 no memory (then they are freed)
static inline int kv_insert_ (kv_doc *kv, int at, char *key, char *val, int block, int line)
{
	if (key && val && kv->n == kv->cap)
	{
		int c = kv->cap ? kv->cap * 2 : 32;
		kv_entry *ne = (kv_entry *) realloc (kv->e, (size_t) c * sizeof (kv_entry));
		if (ne) { kv->e = ne; kv->cap = c; }
	}
	if (!key || !val || kv->n == kv->cap) { free (key); free (val); return -1; }
	memmove (kv->e + at + 1, kv->e + at, (size_t) (kv->n - at) * sizeof (kv_entry));
	kv->e[at].key = key; kv->e[at].val = val; kv->e[at].block = block; kv->e[at].line = line;
	kv->n++;
	return 0;
}
// The first entry of (section, key) -> its index, -1 none
static inline int kv_find_ (const kv_doc *kv, const char *section, const char *key)
{
	if (!kv) return -1;
	for (int i = 0; i < kv->n; i++)
		if (kv_eq_ (kv->b[kv->e[i].block].name, section) && kv_eq_ (kv->e[i].key, key)) return i;
	return -1;
}

// ---- making and freeing ------------------------------------------------------------------------------------------
static inline kv_doc *kv_new (int flags)
{
	kv_doc *kv = (kv_doc *) calloc (1, sizeof (kv_doc));
	if (!kv) return 0;
	kv->flags = flags;
	if (kv_add_block_ (kv, "", 0, 0) < 0) { free (kv); return 0; }
	return kv;
}
static inline void kv_free (kv_doc *kv)
{
	if (!kv) return;
	for (int i = 0; i < kv->n; i++) { free (kv->e[i].key); free (kv->e[i].val); }
	for (int i = 0; i < kv->nb; i++) free (kv->b[i].name);
	free (kv->e); free (kv->b); free (kv->text); free (kv);
}
// Never fails on syntax (lines without '=' skipped); 0: no memory
static inline kv_doc *kv_parse (const char *text, int flags)
{
	kv_doc *kv = kv_new (flags);
	const char *p = text ? text : "";
	int line = 0, block = 0;
	int pipe = -1; bool piped = false;		// the entry "|" lines go on (-1: none); its value already has "|" lines
	if (!kv) return 0;
	if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF) p += 3;	// (a UTF-8 BOM)
	while (*p)
	{
		const char *s = p, *e = p;
		while (*e && *e != '\n') e++;
		p = *e ? e + 1 : e;
		line++;
		int n = (int) (e - s);
		if (n && s[n - 1] == '\r') n--;
		while (n && kv_space_ (*s)) { s++; n--; }
		if ((flags & KV_PIPES) && n && *s == '|')
		{
			if (pipe < 0) continue;			// (a "|" line with nothing before it)
			s++; n--;
			if (n && *s == ' ') { s++; n--; }
			char *v = kv_ndup_ (s, n);
			if (!v) { kv_free (kv); return 0; }
			if (flags & KV_ESCAPES) kv_unescape_ (v);
			if (!piped && !kv->e[pipe].val[0])	// "key =" empty: the value starts on this line
			{
				free (kv->e[pipe].val); kv->e[pipe].val = v; kv->e[pipe].line = line;
			}
			else
			{
				int ol = (int) strlen (kv->e[pipe].val), vl = (int) strlen (v);
				char *nv = (char *) realloc (kv->e[pipe].val, (size_t) ol + vl + 2);
				if (!nv) { free (v); kv_free (kv); return 0; }
				nv[ol] = '\n'; memcpy (nv + ol + 1, v, (size_t) vl + 1);
				kv->e[pipe].val = nv; free (v);
			}
			piped = true;
			continue;
		}
		pipe = -1; piped = false;
		if (!n || *s == '#' || *s == ';') continue;
		if (*s == '[')
		{
			const char *nm = s + 1; int nl = 0;
			while (nl < n - 1 && nm[nl] != ']') nl++;
			while (nl && kv_space_ (*nm)) { nm++; nl--; }
			while (nl && kv_space_ (nm[nl - 1])) nl--;
			block = kv_add_block_ (kv, nm, nl, line);
			if (block < 0) { kv_free (kv); return 0; }
			continue;
		}
		int eq = 0;
		while (eq < n && s[eq] != '=') eq++;
		if (eq == n) continue;				// (no '=': skipped)
		int kl = eq; while (kl && kv_space_ (s[kl - 1])) kl--;
		if (!kl) continue;
		int vs = eq + 1; while (vs < n && kv_space_ (s[vs])) vs++;
		int vl = n; while (vl > vs && kv_space_ (s[vl - 1])) vl--;
		char *key = kv_ndup_ (s, kl), *val = kv_ndup_ (s + vs, vl - vs);
		if (val && (flags & KV_ESCAPES)) kv_unescape_ (val);
		if (kv_insert_ (kv, kv->n, key, val, block, line)) { kv_free (kv); return 0; }
		pipe = kv->n - 1;
	}
	return kv;
}
// A file read and parsed; 0: no such file, unreadable, no memory
static inline kv_doc *kv_load (const char *path, int flags)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	struct stat st;
	if (fstat (f, &st) != 0) { close (f); return 0; }
	size_t n = (size_t) st.st_size, done = 0;
	char *buf = (char *) malloc (n + 1);
	if (!buf) { close (f); return 0; }
	while (done < n) { long r = read (f, buf + done, n - done); if (r <= 0) break; done += (size_t) r; }
	close (f);
	if (done < n) { free (buf); return 0; }
	buf[n] = 0;
	kv_doc *kv = kv_parse (buf, flags);
	free (buf);
	return kv;
}

// ---- the entries, the blocks -----------------------------------------------------------------------------------------
static inline int kv_count (const kv_doc *kv) { return kv ? kv->n : 0; }
static inline const char *kv_key (const kv_doc *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].key : ""; }
static inline const char *kv_value (const kv_doc *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].val : ""; }
static inline int kv_block (const kv_doc *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].block : 0; }
// The 1-based line of the value's first line: the key's, or its first "|" line when "key =" is empty; 0: made, not read
static inline int kv_line (const kv_doc *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].line : 0; }
static inline int kv_blocks (const kv_doc *kv) { return kv ? kv->nb - 1 : 0; }
static inline const char *kv_block_name (const kv_doc *kv, int b) { return kv && b > 0 && b < kv->nb ? kv->b[b].name : ""; }
static inline int kv_block_line (const kv_doc *kv, int b) { return kv && b > 0 && b < kv->nb ? kv->b[b].line : 0; }

// ---- looking up and changing -------------------------------------------------------------------------------------------
static inline const char *kv_get (const kv_doc *kv, const char *section, const char *key, const char *def)
{
	int i = kv_find_ (kv, section, key);
	return i >= 0 ? kv->e[i].val : def;
}
// The first match's value replaced, or a new entry at the end of that section's first block -> 0, -1 (no memory, no key)
static inline int kv_set (kv_doc *kv, const char *section, const char *key, const char *value)
{
	if (!kv || !key || !key[0]) return -1;
	int i = kv_find_ (kv, section, key);
	if (i >= 0)
	{
		char *v = kv_dup_ (value);
		if (!v) return -1;
		free (kv->e[i].val); kv->e[i].val = v;
		return 0;
	}
	if (!section) section = "";
	int b = -1;
	if (!section[0]) b = 0;
	else for (i = 1; i < kv->nb; i++) if (kv_eq_ (kv->b[i].name, section)) { b = i; break; }
	if (b < 0 && (b = kv_add_block_ (kv, section, (int) strlen (section), 0)) < 0) return -1;
	int at = 0;
	while (at < kv->n && kv->e[at].block <= b) at++;	// (after the block's last entry)
	return kv_insert_ (kv, at, kv_dup_ (key), kv_dup_ (value), b, 0);
}

// ---- writing -----------------------------------------------------------------------------------------------------------
struct kv_out
{
	char *d; int n, cap; bool bad;
	void put (const char *s, int m)
	{
		if (bad) return;
		if (n + m + 1 > cap)
		{
			int c = cap ? cap : 1024;
			while (n + m + 1 > c) c *= 2;
			char *nd = (char *) realloc (d, (size_t) c);
			if (!nd) { bad = true; return; }
			d = nd; cap = c;
		}
		memcpy (d + n, s, (size_t) m); n += m; d[n] = 0;
	}
	void puts (const char *s) { put (s, (int) strlen (s)); }
};
static inline void kv_put_entry_ (kv_out &o, const kv_entry &e, int flags)
{
	const char *v = e.val;
	o.puts (e.key);
	if (flags & KV_ESCAPES)
	{
		o.puts (" = ");
		for (; *v; v++)
		{
			if (*v == '\n') o.put ("\\n", 2);
			else if (*v == '\\') o.put ("\\\\", 2);
			else if (*v != '\r') o.put (v, 1);
		}
		o.put ("\n", 1);
	}
	else if ((flags & KV_PIPES) && strchr (v, '\n'))
	{
		o.puts (" =\n");
		for (;;)
		{
			const char *q = v;
			while (*q && *q != '\n') q++;
			o.put ("| ", 2); o.put (v, (int) (q - v)); o.put ("\n", 1);
			if (!*q) break;
			v = q + 1;
		}
	}
	else
	{
		o.puts (v[0] ? " = " : " =");
		for (; *v; v++) o.put (*v == '\n' ? " " : v, 1);
		o.put ("\n", 1);
	}
}
// The document written to path, the comment as a first line -> 0, -1
static inline int kv_save (kv_doc *kv, const char *path, const char *comment)
{
	if (!kv) return -1;
	kv_out o = { 0, 0, 0, false };
	o.put ("", 0);
	if (comment && comment[0]) { if (comment[0] != '#') o.puts ("# "); o.puts (comment); o.put ("\n", 1); }
	int i = 0;
	for (int b = 0; b < kv->nb; b++)
	{
		if (b > 0)
		{
			if (o.n) o.put ("\n", 1);
			o.put ("[", 1); o.puts (kv->b[b].name); o.put ("]\n", 2);
		}
		for (; i < kv->n && kv->e[i].block == b; i++) kv_put_entry_ (o, kv->e[i], kv->flags);
	}
	if (o.bad) { free (o.d); return -1; }
	free (kv->text); kv->text = o.d;
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f < 0) return -1;
	long w = o.n ? write (f, o.d, (size_t) o.n) : 0;
	return close (f) == 0 && w == o.n ? 0 : -1;
}

#endif
