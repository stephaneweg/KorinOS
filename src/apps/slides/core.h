//
// core.h -- Slides' basics: Onyx's growing buffer and UTF-8 (the part of Apps/sheet/core.h Slides used, namespace ss
// kept), and the short forms of the direct calls to DocumentKit (XML) and ArchiveKit (ZIP): the handle stays a long,
// the functions are one line each, only to spare the casts and to close what was opened.
//
#ifndef _slides_core_h
#define _slides_core_h

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <documentkit_abi.h>
#include <archivekit_abi.h>

namespace ss {

static inline int imin (int a, int b) { return a < b ? a : b; }
static inline int imax (int a, int b) { return a > b ? a : b; }
static inline int iclamp (int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- a growing buffer (always NUL-terminated) --------------------------------------------------------------
struct Buf
{
	char *b; int n, cap;
	Buf () : b (0), n (0), cap (0) {}
	~Buf () { free (b); }
	void reserve (int m)
	{
		if (m + 1 <= cap) return;
		int c = cap ? cap : 128;
		while (c < m + 1) c *= 2;
		b = (char *) realloc (b, c); cap = c;
	}
	void put (char c) { reserve (n + 1); b[n++] = c; b[n] = 0; }
	void putn (const char *s, int k) { if (k <= 0) { reserve (n); b[n] = 0; return; } reserve (n + k); memcpy (b + n, s, k); n += k; b[n] = 0; }
	void puts (const char *s) { putn (s, (int) strlen (s)); }
	void putu (unsigned cp);			// a character as UTF-8
	void clear () { n = 0; if (b) b[0] = 0; }
	const char *str () { reserve (n); b[n] = 0; return b; }
private:
	Buf (const Buf &);
	Buf &operator= (const Buf &);
};

static char *sdup (const char *s, int n = -1)
{
	if (!s) s = "";
	if (n < 0) n = (int) strlen (s);
	char *d = (char *) malloc (n + 1);
	memcpy (d, s, n); d[n] = 0;
	return d;
}
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static void scat (char *d, const char *s, int cap) { int n = (int) strlen (d); if (n < cap - 1) scpy (d + n, s, cap - n); }

// A character from s (n bytes at most); *len its bytes. A byte that does not start a valid sequence: Latin-1.
static unsigned u8_dec (const char *s, int n, int *len)
{
	const unsigned char *p = (const unsigned char *) s;
	if (n <= 0) { *len = 0; return 0; }
	unsigned c = p[0];
	if (c < 0x80) { *len = 1; return c; }
	int k = c >= 0xF0 && c < 0xF8 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c < 0xE0 ? 2 : 0;
	if (k == 0 || k > n) { *len = 1; return c; }
	unsigned v = c & (0xFF >> (k + 1));
	for (int i = 1; i < k; i++)
	{
		if ((p[i] & 0xC0) != 0x80) { *len = 1; return c; }
		v = v << 6 | (p[i] & 0x3F);
	}
	*len = k;
	return v;
}
static int u8_enc (unsigned cp, char *o)
{
	if (cp < 0x80) { o[0] = (char) cp; return 1; }
	if (cp < 0x800) { o[0] = (char) (0xC0 | cp >> 6); o[1] = (char) (0x80 | (cp & 0x3F)); return 2; }
	if (cp < 0x10000) { o[0] = (char) (0xE0 | cp >> 12); o[1] = (char) (0x80 | (cp >> 6 & 0x3F)); o[2] = (char) (0x80 | (cp & 0x3F)); return 3; }
	o[0] = (char) (0xF0 | cp >> 18); o[1] = (char) (0x80 | (cp >> 12 & 0x3F)); o[2] = (char) (0x80 | (cp >> 6 & 0x3F)); o[3] = (char) (0x80 | (cp & 0x3F));
	return 4;
}
inline void Buf::putu (unsigned cp) { char t[4]; putn (t, u8_enc (cp, t)); }

// ---- XML: DocumentKit ----------------------------------------------------------------------------------------
enum { X_EOF = DKX_EOF, X_START = DKX_START, X_END = DKX_END, X_TEXT = DKX_TEXT };
// (a reader closed at the end of its block; a long for the kit's functions. 64 open at most: never more than 3 here)
struct XmlFile
{
	long r;
	XmlFile (const char *x, int n) : r (dk_xml_open ((long) x, n)) {}
	~XmlFile () { dk_xml_close (r); }
	operator long () const { return r; }
private:
	XmlFile (const XmlFile &);
	XmlFile &operator= (const XmlFile &);
};
static inline int x_next (long r) { return (int) dk_xml_next (r); }
static inline int x_ev (long r) { return (int) dk_xml_event (r); }
static inline bool x_is (long r, const char *n) { return dk_xml_is (r, (long) n) != 0; }
static inline bool x_isq (long r, const char *n) { return dk_xml_isq (r, (long) n) != 0; }
static inline bool x_empty (long r) { return dk_xml_empty (r) != 0; }
static inline void x_skip (long r) { dk_xml_skip (r); }
static bool x_attr (long r, const char *name, Buf &o)
{
	o.clear ();
	char t[256]; long n = dk_xml_attr (r, (long) name, (long) t, sizeof t);
	if (n < 0) return false;
	if (n < (long) sizeof t) { o.putn (t, (int) n); return true; }
	o.reserve ((int) n + 1);
	dk_xml_attr (r, (long) name, (long) o.b, n + 1); o.n = (int) n;
	return true;
}
static void x_text (long r, Buf &o)
{
	o.clear ();
	long n = dk_xml_text (r, 0, 0);
	if (n <= 0) return;
	o.reserve ((int) n + 1);
	dk_xml_text (r, (long) o.b, n + 1); o.n = (int) n; o.b[n] = 0;
}
static inline int x_attr_int (long r, const char *name, int def) { return (int) dk_xml_attr_int (r, (long) name, def); }
static inline double x_attr_num (long r, const char *name, double def) { Buf b; if (!x_attr (r, name, b)) return def; return strtod (b.str (), 0); }
static inline bool x_attr_bool (long r, const char *name, bool def) { return dk_xml_attr_bool (r, (long) name, def) != 0; }
// Text for XML (a node's text, or an attribute's value: quotes too); control characters dropped.
static void xml_esc (Buf &o, const char *s, int n = -1)
{
	long k = dk_xml_esc ((long) s, n, 0, 0);
	if (k <= 0) return;
	o.reserve (o.n + (int) k + 1);
	dk_xml_esc ((long) s, n, (long) (o.b + o.n), k + 1); o.n += (int) k; o.b[o.n] = 0;
}

// ---- ZIP: ArchiveKit -----------------------------------------------------------------------------------------
static inline void zip_add (long z, const char *name, const void *b, unsigned n, bool deflate) { ar_add_mem (z, (long) name, (long) b, n, deflate ? 6 : 0); }
// The archive made (new []): *len bytes; 0: it failed.
static unsigned char *zip_finish (long z, unsigned *len)
{
	unsigned char *o = 0;
	if (ar_finish (z) >= 0)
	{
		*len = (unsigned) ar_bytes (z);
		o = new unsigned char[*len + 1];
		memcpy (o, (const void *) ar_data (z), *len);
	}
	ar_free (z);
	return o;
}
// A part of an archive (malloc, a 0 after it); 0: absent or damaged.
static char *zip_part (long a, const char *name, int *len)
{
	long i = ar_find (a, (long) name);
	if (i < 0) return 0;
	long t = ar_load (a, i);
	if (t <= 0) return 0;
	long n = ar_bytes (t);
	char *b = (char *) malloc ((size_t) n + 1);
	if (b) { memcpy (b, (const void *) ar_data (t), (size_t) n); b[n] = 0; *len = (int) n; }
	ar_free (t);
	return b;
}

// ---- a PNG (the exported slide, a chart's picture in an .odp) --------------------------------------------------
static void png_chunk (Buf &o, const char *type, const void *d, unsigned n)
{
	unsigned char l[4] = { (unsigned char) (n >> 24), (unsigned char) (n >> 16), (unsigned char) (n >> 8), (unsigned char) n };
	o.putn ((const char *) l, 4);
	long c = ar_crc32 (0, (long) type, 4);
	if (n) c = ar_crc32 (c, (long) d, n);
	o.putn (type, 4); o.putn ((const char *) d, (int) n);
	unsigned u = (unsigned) c; unsigned char e[4] = { (unsigned char) (u >> 24), (unsigned char) (u >> 16), (unsigned char) (u >> 8), (unsigned char) u };
	o.putn ((const char *) e, 4);
}
// px: 0xAARRGGBB -> the file (new []), *len
static unsigned char *png_encode (const unsigned *px, int w, int h, bool alpha, unsigned *len)
{
	int bpp = alpha ? 4 : 3;
	long nb = (long) (1 + w * bpp) * h;
	unsigned char *raw = (unsigned char *) malloc ((size_t) nb);
	if (!raw) return 0;
	unsigned char *q = raw;
	for (int y = 0; y < h; y++)
	{
		*q++ = 0;
		const unsigned *r = px + (long) y * w;
		for (int x = 0; x < w; x++) { unsigned p = r[x]; *q++ = (unsigned char) (p >> 16); *q++ = (unsigned char) (p >> 8); *q++ = (unsigned char) p; if (alpha) *q++ = (unsigned char) (p >> 24); }
	}
	long z = ar_deflate_new ((long) raw, nb, AR_ZLIB, 6);
	free (raw);
	if (z <= 0) return 0;
	Buf o;
	o.putn ("\x89PNG\r\n\x1A\n", 8);
	unsigned char ih[13] = { (unsigned char) (w >> 24), (unsigned char) (w >> 16), (unsigned char) (w >> 8), (unsigned char) w, (unsigned char) (h >> 24), (unsigned char) (h >> 16), (unsigned char) (h >> 8), (unsigned char) h, 8, (unsigned char) (alpha ? 6 : 2), 0, 0, 0 };
	png_chunk (o, "IHDR", ih, 13);
	png_chunk (o, "IDAT", (const void *) ar_data (z), (unsigned) ar_bytes (z));
	png_chunk (o, "IEND", "", 0);
	ar_free (z);
	unsigned char *f = new unsigned char[o.n];
	memcpy (f, o.b, o.n); *len = (unsigned) o.n;
	return f;
}

} // namespace ss

#endif
