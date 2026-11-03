//
// office.h -- what the office files (.docx, .odt, a picture in an .rtf) need around the document: a growing text;
// the kits' calls with their types (DocumentKit: the XML reader, escaping, lengths, colours; ArchiveKit: the ZIP);
// an image's bytes into the document (UIKit decodes BMP and PNG; a JPEG keeps its file and its size, with no pixels:
// it is shown as a frame, and written back as it came) and a PNG made from pixels.
//
#ifndef _writer_office_h
#define _writer_office_h

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uikit.h>
#include <archivekit_abi.h>
#include <documentkit_abi.h>
#ifndef TR
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-fr.txt, or the system's dictionary)
#endif
#include "doc.h"

#define APP_HOME "SD0:/home/letters"
#define DOCS_DIR "SD0:/home/documents"

namespace wr {

// ---- files ---------------------------------------------------------------------------------------------------
// A file's bytes (new[], NUL-terminated), or 0.
static char *file_read (const char *path, int *len)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	struct stat st;
	if (fstat (f, &st) < 0 || S_ISDIR (st.st_mode)) { close (f); return 0; }
	long sz = st.st_size > (32L << 20) ? 32L << 20 : st.st_size, got = 0;
	char *b = new char[sz + 1];
	while (got < sz) { long k = read (f, b + got, sz - got); if (k <= 0) break; got += k; }
	close (f);
	b[got] = 0; *len = (int) got;
	return b;
}
static bool file_write (const char *path, const void *data, long n)
{
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (f < 0) return false;
	const char *p = (const char *) data; long done = 0;
	while (done < n) { long k = write (f, p + done, n - done); if (k <= 0) break; done += k; }
	close (f);
	return done == n;
}
static void say (const char *title, const char *text) { ui_message ((long) TR (title), (long) TR (text), UM_OK); }

// ---- a growing text ----------------------------------------------------------------------------------------
struct XBuf
{
	char *b; int n, cap;
	XBuf () : b (0), n (0), cap (0) {}
	~XBuf () { delete[] b; }
	void reserve (int k) { if (n + k + 1 <= cap) return; int c = cap ? cap * 2 : 256; while (c < n + k + 1) c *= 2; char *t = new char[c]; for (int i = 0; i < n; i++) t[i] = b[i]; delete[] b; b = t; cap = c; }
	void put (char c) { reserve (1); b[n++] = c; b[n] = 0; }
	void putn (const char *s, int k) { reserve (k); for (int i = 0; i < k; i++) b[n++] = s[i]; b[n] = 0; }
	void puts (const char *s) { putn (s, (int) strlen (s)); }
	void putu (unsigned c) { reserve (4); n += (int) dk_utf8_encode (c, (long) (b + n)); b[n] = 0; }	// (a code point, as UTF-8)
	void num (long v) { char t[24]; int j = 0; bool ng = v < 0; unsigned long u = ng ? (unsigned long) -v : (unsigned long) v; do { t[j++] = (char) ('0' + u % 10); u /= 10; } while (u); if (ng) put ('-'); while (j) put (t[--j]); }
	void clear () { n = 0; if (b) b[0] = 0; }
	const char *str () { reserve (0); b[n] = 0; return b; }	// (an empty one: its buffer made, ended)
	char *take () { reserve (0); b[n] = 0; char *r = b; b = 0; n = cap = 0; return r; }
};

// UTF-8 -> code points (a byte not UTF-8: as Latin-1).
static inline int utf8_decode (const char *s, int n, unsigned *out, int cap) { return (int) dk_utf8_to_cp ((long) s, n, (long) out, cap); }

// ---- the XML reader's calls, typed ---------------------------------------------------------------------------
static inline bool x_is (long X, const char *n) { return dk_xml_is (X, (long) n) != 0; }
static inline bool x_isq (long X, const char *n) { return dk_xml_isq (X, (long) n) != 0; }
static inline long x_int (long X, const char *n, long def) { return dk_xml_attr_int (X, (long) n, def); }
static inline bool x_on (long X, const char *n = 0) { return dk_xml_attr_on (X, (long) n) != 0; }
// An attribute into a fixed buffer ("" when absent).
static bool x_attrs (long X, const char *name, char *out, int cap)
{
	long k = dk_xml_attr (X, (long) name, (long) out, cap);
	if (k < 0) { out[0] = 0; return false; }
	return true;
}
static bool x_attr (long X, const char *name, XBuf &o)
{
	long k = dk_xml_attr (X, (long) name, 0, 0);
	if (k < 0) return false;
	o.clear (); o.reserve ((int) k + 1);
	dk_xml_attr (X, (long) name, (long) o.b, k + 1);
	o.n = (int) k;
	return true;
}
// The text event's text added to o.
static void x_text (long X, XBuf &o)
{
	long k = dk_xml_text (X, 0, 0);
	if (k <= 0) { o.reserve (0); return; }
	o.reserve ((int) k + 1);
	dk_xml_text (X, (long) (o.b + o.n), k + 1);
	o.n += (int) k; o.b[o.n] = 0;
}
// The element's name without its prefix.
static void x_local (long X, char *e, int cap)
{
	char nm[64]; dk_xml_name (X, (long) nm, sizeof nm);
	int l = 0;
	for (int i = 0; nm[i] && l < cap - 1; i++) { if (nm[i] == ':') l = 0; else e[l++] = nm[i]; }
	e[l] = 0;
}

// ---- XML written ---------------------------------------------------------------------------------------------
// Text for XML (a node's text, or an attribute's value: quotes too); control characters dropped.
static void xml_esc (XBuf &o, const char *s, int n = -1)
{
	long k = dk_xml_esc ((long) s, n, 0, 0);
	if (k <= 0) return;
	o.reserve ((int) k + 1);
	dk_xml_esc ((long) s, n, (long) (o.b + o.n), k + 1);
	o.n += (int) k;
}
// Code points as XML text (UTF-8).
static void xml_escu (XBuf &o, const unsigned *s, int n)
{
	for (int i = 0; i < n; i++)
	{
		unsigned c = s[i];
		if (c == '&') o.puts ("&amp;"); else if (c == '<') o.puts ("&lt;"); else if (c == '>') o.puts ("&gt;"); else if (c == '"') o.puts ("&quot;");
		else if (c >= 32 && !(c >= 0xD800 && c < 0xE000) && c != 0xFFFE && c != 0xFFFF) o.putu (c);
	}
}
// A Latin-1 string (a path, a name) as XML text.
static void xml_esc1 (XBuf &o, const char *s)
{
	char t[512]; long k = dk_latin1_to_utf8 ((long) s, (long) strlen (s), (long) t, sizeof t);
	if (k >= (long) sizeof t) k = sizeof t - 1;
	xml_esc (o, t, (int) k);
}

// ---- ZIP ------------------------------------------------------------------------------------------------------
// An entry of an archive, inflated (new[], NUL-terminated), or 0.
static char *zip_get (const unsigned char *z, unsigned zn, const char *name, int *len)
{
	long a = ar_open_mem ((long) z, zn);
	if (a < 0) return 0;
	char *d = 0;
	long i = ar_find (a, (long) name);
	long t = i >= 0 ? ar_load (a, i) : -1;
	if (t > 0)
	{
		long n = ar_bytes (t);
		if (n >= 0 && n < (1L << 30))
		{
			d = new char[n + 1];
			memcpy (d, (const void *) ar_data (t), n);
			d[n] = 0; *len = (int) n;
		}
		ar_free (t);
	}
	ar_close (a);
	return d;
}
static bool zip_has (const unsigned char *z, unsigned zn, const char *name)
{
	long a = ar_open_mem ((long) z, zn);
	if (a < 0) return false;
	bool ok = ar_find (a, (long) name) >= 0;
	ar_close (a);
	return ok;
}
// The archive written: its bytes (new[]), or 0.
static unsigned char *zip_finish (long zip, unsigned *len)
{
	unsigned char *o = 0;
	if (zip > 0 && ar_finish (zip) >= 0)
	{
		long n = ar_bytes (zip);
		o = new unsigned char[n > 0 ? n : 1];
		memcpy (o, (const void *) ar_data (zip), n);
		*len = (unsigned) n;
	}
	ar_free (zip);
	return o;
}
static inline long zip_add (long zip, const char *name, const void *data, unsigned n, bool pack) { return ar_add_mem (zip, (long) name, (long) data, n, pack ? 6 : 0); }

// A part's path from another's relative one ("word/document.xml" + "media/a.png" -> "word/media/a.png").
static inline void part_path (const char *base, const char *rel, char *out, int cap) { dk_part_path ((long) base, (long) rel, (long) out, cap); }

// ---- lengths, colours --------------------------------------------------------------------------------------------
// "2.54cm", "1in", "12pt", "20mm", "1pc"... -> twips (a bare number: twips).
static inline int len_twips (const char *s) { return (int) dk_len_parse ((long) s, DKU_TWIP, DKU_TWIP, 0); }
// Twips -> "1.2340cm" (an ODF length).
static void twips_cm (XBuf &o, int tw)
{
	char t[32]; int k = (int) dk_len_write (tw, DKU_TWIP, DKU_CM, 4, (long) t, sizeof t);
	o.putn (t, k);
}
// "#RRGGBB" / "RRGGBB" -> 0xRRGGBB (AUTO: "auto", none, not a colour).
static inline unsigned hex_color (const char *s) { return (unsigned) dk_color_parse ((long) s, (long) (unsigned long) AUTO); }
static inline void color_hex (XBuf &o, unsigned c) { char t[10]; int k = (int) dk_color_write (c, 0, (long) t); o.putn (t, k); }

// ---- images ------------------------------------------------------------------------------------------------------
// An image's pixels (0xAARRGGBB, new[]) from its file's bytes: 0 when it cannot be read. JPEG: jpeg.c; the others: UIKit.
extern "C" unsigned *jpeg_decode (const unsigned char *b, int n, int *w, int *h);
static unsigned *image_pixels (const unsigned char *b, unsigned n, int *w, int *h)
{
	if (n > 3 && b[0] == 0xFF && b[1] == 0xD8)
	{
		unsigned *m = jpeg_decode (b, (int) n, w, h);
		if (!m) return 0;
		unsigned *px = new unsigned[(long) *w * *h];
		memcpy (px, m, (long) *w * *h * 4);
		free (m);
		return px;
	}
	long im = ui_image_decode ((long) b, n);
	if (im <= 0) return 0;
	long ih = 0, ipx = 0, iw = uikit_image_size (im, &ih, &ipx);
	unsigned *px = 0;
	if (iw > 0 && ih > 0 && iw * ih <= (64L << 20) && ipx)
	{
		px = new unsigned[iw * ih];
		memcpy (px, (const void *) ipx, iw * ih * 4);
		*w = (int) iw; *h = (int) ih;
	}
	ui_image_free (im);
	return px;
}
// An image's bytes read: into the document (its index + 1; 0: not an image). PNG and JPEG keep their file, written back as it came.
static int image_from_bytes (Doc &d, const unsigned char *b, unsigned n)
{
	if (!n) return 0;
	bool png = n > 4 && b[0] == 0x89 && b[1] == 'P', jpeg = n > 2 && b[0] == 0xFF && b[1] == 0xD8;
	int w = 0, h = 0;
	unsigned *px = 0;
	if (!(px = image_pixels (b, n, &w, &h))) return 0;
	unsigned char *data = 0;
	if (png || jpeg) { data = new unsigned char[n]; memcpy (data, b, n); }
	return doc_image (d, px, w, h, data, png || jpeg ? n : 0, jpeg) + 1;
}
// A PNG from pixels (new[]; 8 bits, RGBA): ArchiveKit's zlib and CRC.
static unsigned char *png_make (const unsigned *px, int w, int h, unsigned *len)
{
	long raw = (w * 4L + 1) * h;
	unsigned char *r = (unsigned char *) malloc (raw);
	if (!r) return 0;
	unsigned char *q = r;
	for (int y = 0; y < h; y++)
	{
		*q++ = 0;
		for (int x = 0; x < w; x++) { unsigned p = px[y * w + x]; *q++ = (unsigned char) (p >> 16); *q++ = (unsigned char) (p >> 8); *q++ = (unsigned char) p; *q++ = (unsigned char) (p >> 24); }
	}
	long t = ar_deflate_new ((long) r, raw, AR_ZLIB, 6);
	free (r);
	if (t <= 0) return 0;
	long zn = ar_bytes (t);
	unsigned char *o = new unsigned char[zn + 64], *e = o;
	auto be = [&] (unsigned v) { e[0] = (unsigned char) (v >> 24); e[1] = (unsigned char) (v >> 16); e[2] = (unsigned char) (v >> 8); e[3] = (unsigned char) v; e += 4; };
	auto chunk = [&] (const char *ty, const void *d, unsigned n) {
		be (n); unsigned char *s = e; memcpy (e, ty, 4); e += 4;
		if (n) { memcpy (e, d, n); e += n; }
		be ((unsigned) ar_crc32 (0, (long) s, n + 4));
	};
	static const unsigned char SIG[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
	memcpy (e, SIG, 8); e += 8;
	unsigned char ih[13] = { (unsigned char) (w >> 24), (unsigned char) (w >> 16), (unsigned char) (w >> 8), (unsigned char) w,
				 (unsigned char) (h >> 24), (unsigned char) (h >> 16), (unsigned char) (h >> 8), (unsigned char) h, 8, 6, 0, 0, 0 };
	chunk ("IHDR", ih, 13);
	chunk ("IDAT", (const void *) ar_data (t), (unsigned) zn);
	chunk ("IEND", 0, 0);
	ar_free (t);
	*len = (unsigned) (e - o);
	return o;
}

} // namespace wr

#endif
