//
// job.h (printerkit/job.h d'Onyx) -- le fichier des pages d'un travail (SD0:/var/spool/print/<id>.opj), ecrit par
// PrinterKit (src/kits/printerkit/printerkit.asm : le format y est decrit), relu ici pour ce qui imprime : un PDF
// (pdfsink.h) ou des pixels a la resolution de l'imprimante (raster.h). Un travail est le meme quoi qui l'imprime, une
// application ne dessine qu'une fois. Le fichier est lu en flot, jamais entier en memoire.
//
#ifndef PRINTD_JOB_H
#define PRINTD_JOB_H

#include <stdio.h>
#include <string.h>
#include <archivekit_abi.h>
#include "buf.h"
#include "ttf.h"

char *font_path_data (const char *path, unsigned *len);		// (main.cpp : une police de la carte, gardee)

namespace pjob {

enum { R_FONT = 1, R_PAGE, R_RECT, R_GLYPH, R_TEXT, R_IMAGE, R_PATH, R_ENDPAGE, R_END, R_FONTREF };
enum { IMG_DEFLATE = 0, IMG_JPEG = 1, IMG_RAW = 2 };	// (IMG_RAW : apres decompression, ici seulement)
enum { V_MOVE = 0, V_LINE, V_CURVE, V_CLOSE };		// (une courbe : trois points, le verbe sur chacun)
enum { ST_BOLD = 1, ST_ITALIC = 2 };			// un glyphe fabrique gras / penche (la police n'a pas ce style)

struct Glyph { float x, y; unsigned short gid, pad; unsigned uni; };
struct PathPt { unsigned verb; float x, y; };

static inline float mpt (const unsigned char *p) { int v; memcpy (&v, p, 4); return v / 1000.0f; }
static inline unsigned get_u (const unsigned char *p) { unsigned v; memcpy (&v, p, 4); return v; }

// ce a quoi une page est rejouee
struct Sink
{
	virtual ~Sink () {}
	virtual void font (int id, const unsigned char *ttf, unsigned len) = 0;		// (les octets restent jusqu'a la fin du travail)
	virtual void begin_page (float w, float h) = 0;
	virtual void rect (float x, float y, float w, float h, unsigned rgb) = 0;
	virtual void glyphs (const Ttf *f, int font, unsigned style, float size, unsigned rgb, const Glyph *g, int n) = 0;
	virtual void image (float x, float y, float w, float h, int pw, int ph, int kind, const unsigned char *data, unsigned len) = 0;
	virtual void path (const PathPt *pt, int n, unsigned rgb, float strokeW) = 0;
	virtual void end_page () = 0;
};

class Reader
{
public:
	Reader () : m_f (0), m_buf (0), m_cap (0), m_gl (0), m_ngl (0)
	{
		for (int i = 0; i < 64; i++) { m_font[i] = 0; m_own[i] = 0; m_style[i] = 0; }
	}
	~Reader ()
	{
		if (m_f) fclose (m_f);
		for (int i = 0; i < 64; i++) { delete m_font[i]; delete[] m_own[i]; }
		delete[] m_buf; delete[] m_gl;
	}
	bool open (const char *path)
	{
		m_f = fopen (path, "rb");
		char m[4];
		return m_f && fread (m, 1, 4, m_f) == 4 && !memcmp (m, "OPJ2", 4);
	}
	// la page suivante rejouee -> 1, 0 : plus de pages, -1 : fichier abime
	int page (Sink &s)
	{
		bool in = false;
		for (;;)
		{
			unsigned char h[8];
			if (fread (h, 1, 8, m_f) != 8) return in ? -1 : 0;
			unsigned kind = get_u (h), len = get_u (h + 4);
			if (len > 0x40000000u) return -1;
			if (kind == R_END) return 0;
			if (kind == R_FONT)
			{
				if (len < 8) return -1;
				unsigned char t[4];
				if (fread (t, 1, 4, m_f) != 4) return -1;
				unsigned id = get_u (t);
				if (id >= 64) return -1;
				unsigned char *d = new unsigned char[len - 4];
				if (fread (d, 1, len - 4, m_f) != len - 4) { delete[] d; return -1; }
				m_own[id] = d;
				add_font (s, (int) id, d, len - 4, 0);
				continue;
			}
			if (len > m_cap) { delete[] m_buf; m_cap = len + len / 2 + 4096; m_buf = new unsigned char[m_cap]; }
			if (len && fread (m_buf, 1, len, m_f) != len) return -1;
			const unsigned char *p = m_buf;
			switch (kind)
			{
			case R_FONTREF:
			{
				if (len < 9) return -1;
				unsigned id = get_u (p), lf = 0;
				if (id >= 64) return -1;
				m_buf[len - 1] = 0;
				const char *d = font_path_data ((const char *) p + 8, &lf);
				if (d) add_font (s, (int) id, (const unsigned char *) d, lf, get_u (p + 4));
				break;
			}
			case R_PAGE:	if (len < 8) return -1; s.begin_page (mpt (p), mpt (p + 4)); in = true; break;
			case R_RECT:	if (len < 20) return -1; s.rect (mpt (p), mpt (p + 4), mpt (p + 8), mpt (p + 12), get_u (p + 16)); break;
			case R_GLYPH:
			{
				if (len < 32) return -1;
				unsigned f = get_u (p);
				Glyph g; g.x = mpt (p + 12); g.y = mpt (p + 16); g.gid = (unsigned short) get_u (p + 20); g.pad = 0; g.uni = get_u (p + 24);
				s.glyphs (f < 64 ? m_font[f] : 0, (int) f, get_u (p + 4), mpt (p + 8), get_u (p + 28), &g, 1);
				break;
			}
			case R_TEXT:
			{
				if (len < 28) return -1;
				unsigned f = get_u (p), bytes = get_u (p + 24);
				if (bytes > len - 28) return -1;
				text (s, f, get_u (p + 4), mpt (p + 8), mpt (p + 12), mpt (p + 16), get_u (p + 20), p + 28, bytes);
				break;
			}
			case R_IMAGE:
				if (len < 28) return -1;
			{
				int pw = (int) get_u (p + 16), ph = (int) get_u (p + 20), kd = (int) get_u (p + 24);
				if (kd != IMG_DEFLATE) { s.image (mpt (p), mpt (p + 4), mpt (p + 8), mpt (p + 12), pw, ph, kd, p + 28, len - 28); break; }
				if (pw <= 0 || ph <= 0 || (long) pw * ph > 0x10000000) break;
				unsigned raw = (unsigned) pw * ph * 4;
				unsigned char *d = new unsigned char[raw];
				if (ar_inflate ((long) (p + 28), len - 28, (long) d, raw, AR_ZLIB) == (long) raw)
					s.image (mpt (p), mpt (p + 4), mpt (p + 8), mpt (p + 12), pw, ph, IMG_RAW, d, raw);
				delete[] d;
				break;
			}
			case R_PATH:
			{
				if (len < 12) return -1;
				unsigned n = get_u (p + 8);
				if (n > (len - 12) / sizeof (PathPt)) return -1;
				PathPt *pt = (PathPt *) (p + 12);
				for (unsigned i = 0; i < n; i++)
				{
					const unsigned char *q = p + 12 + i * sizeof (PathPt);
					float x = mpt (q + 4), y = mpt (q + 8);
					pt[i].x = x; pt[i].y = y;
				}
				s.path (pt, (int) n, get_u (p), mpt (p + 4));
				break;
			}
			case R_ENDPAGE:	s.end_page (); return 1;
			default:	break;				// (un enregistrement d'une version future : saute)
			}
		}
	}

private:
	FILE *m_f;
	Ttf *m_font[64]; unsigned char *m_own[64]; unsigned m_style[64];
	unsigned char *m_buf; unsigned m_cap;
	Glyph *m_gl; int m_ngl;

	void add_font (Sink &s, int id, const unsigned char *d, unsigned len, unsigned synth)
	{
		Ttf *t = new Ttf;
		if (!t->open (d, len)) { delete t; return; }
		delete m_font[id];
		m_font[id] = t; m_style[id] = synth;
		s.font (id, d, len);
	}
	// un texte : ses glyphes par cmap et hmtx
	void text (Sink &s, unsigned f, unsigned style, float size, float x, float y, unsigned rgb, const unsigned char *t, unsigned bytes)
	{
		Ttf *tt = f < 64 ? m_font[f] : 0;
		if (!tt) return;
		if ((int) bytes > m_ngl) { delete[] m_gl; m_ngl = (int) bytes + 256; m_gl = new Glyph[m_ngl]; }
		float k = size / tt->upem, pen = x;
		int n = 0;
		for (unsigned i = 0; i < bytes; )
		{
			unsigned c = t[i++];
			if (c >= 0xC0) { int e = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1; c &= 0x3Fu >> e; while (e-- && i < bytes && (t[i] & 0xC0) == 0x80) c = c << 6 | (t[i++] & 0x3F); }
			unsigned g = tt->gid (c);
			Glyph &o = m_gl[n++];
			o.x = pen; o.y = y; o.gid = (unsigned short) g; o.pad = 0; o.uni = c;
			pen += tt->advance (g) * k;
		}
		s.glyphs (tt, (int) f, style, size, rgb, m_gl, n);
	}
};

} // namespace pjob

#endif
