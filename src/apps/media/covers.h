//
// covers.h -- the albums' covers (Onyx's Apps/media/covers.h): the picture inside a song (ID3 APIC, FLAC PICTURE) or the
// folder's (cover.jpg, folder.jpg...), decoded by a thread of its own (image.c: JPEG, PNG, BMP), cropped square and
// brought to BASE px; then each size asked for made once and kept (a small cache, the oldest dropped). An album without
// a picture gets one drawn from its name (two colours, shapes: always the same for the same album). Nothing is
// downloaded.
//
//   g_covers.draw (albumIndex, x, y, size, radius, bg);   (a placeholder until it is loaded)
//
#ifndef _media_covers_h
#define _media_covers_h

#include "lib.h"
#include "ui.h"

extern "C" unsigned *image_decode (const unsigned char *b, int n, int *w, int *h);

namespace media {

static inline unsigned hash_str (const char *s, unsigned h = 2166136261u) { for (; s && *s; s++) h = (h ^ (unsigned char) (*s >= 'A' && *s <= 'Z' ? *s + 32 : *s)) * 16777619u; return h; }

// pixels of a picture being made, away from the window
static inline void px_rect (unsigned *px, int n, int x, int y, int w, int h, unsigned c)
{
	for (int j = y < 0 ? 0 : y; j < y + h && j < n; j++) for (int i = x < 0 ? 0 : x; i < x + w && i < n; i++) px[(long) j * n + i] = c;
}
static inline void px_disc (unsigned *px, int n, int cx, int cy, int r, unsigned c)
{
	for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r && cx + i >= 0 && cx + i < n && cy + j >= 0 && cy + j < n) px[(long) (cy + j) * n + cx + i] = c;
}
// src (sw x sh) -> dst (dw x dh), the middle cut to dst's shape, each pixel an average of a few; the clear pixels on white
static void fit_cover (const unsigned *src, int sw, int sh, unsigned *dst, int dw, int dh)
{
	int cw = sw, ch = sw * dh / dw; if (ch > sh) { ch = sh; cw = sh * dw / dh; }
	int ox = (sw - cw) / 2, oy = (sh - ch) / 2;
	for (int y = 0; y < dh; y++)
	{
		int y0 = oy + y * ch / dh, y1 = oy + (y + 1) * ch / dh; if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < dw; x++)
		{
			int x0 = ox + x * cw / dw, x1 = ox + (x + 1) * cw / dw; if (x1 <= x0) x1 = x0 + 1;
			unsigned r = 0, g = 0, b = 0, c = 0;
			for (int yy = y0; yy < y1; yy += 1 + (y1 - y0) / 3) for (int xx = x0; xx < x1; xx += 1 + (x1 - x0) / 3)
			{
				unsigned p = src[(size_t) yy * sw + xx], a = p >> 24;
				if (a == 0) a = 255;				// (a picture without alpha)
				r += ((p >> 16 & 255) * a + 255 * (255 - a)) / 255; g += ((p >> 8 & 255) * a + 255 * (255 - a)) / 255; b += ((p & 255) * a + 255 * (255 - a)) / 255; c++;
			}
			dst[(size_t) y * dw + x] = (r / c) << 16 | (g / c) << 8 | (b / c);
		}
	}
}

class Covers
{
public:
	enum { BASE = 320, NBASE = 96, NSCALED = 220, QN = 64 };
	Library *lib;
	bool loaded;				// the window's: something came, to redraw
	Covers () : lib (0), loaded (false), m_tick (0), m_qn (0), m_quit (false), m_have (false), m_ndone (0)
	{ memset (m_base, 0, sizeof m_base); memset (m_sc, 0, sizeof m_sc); pthread_mutex_init (&m_lk, 0); }

	void start () { m_have = pthread_create (&m_tid, 0, thread_main, this) == 0; }
	void quit () { m_quit = true; if (m_have) pthread_join (m_tid, 0); m_have = false; }
	// the library changed: the albums' numbers are new (the pictures are kept: by name)
	void set_library (Library *l) { pthread_mutex_lock (&m_lk); lib = l; m_qn = 0; pthread_mutex_unlock (&m_lk); }

	unsigned key_of (int album) const { const Album &a = lib->al[album]; return hash_str (a.artist, hash_str (a.title)) | 1; }

	// the cover of album, size x size at (x, y), its corners rounded over bg
	void draw (int album, int x, int y, int size, int radius, unsigned bg)
	{
		if (!lib || album < 0 || album >= lib->nal || size <= 0) return;
		const unsigned *px = scaled (key_of (album), album, size);
		if (px)
			for (int j = 0; j < size; j++)
			{
				int yy = y + j; if (yy < 0 || yy >= g_ph) continue;
				uint32_t *row = g_px + (long) yy * g_pw;
				for (int i = 0; i < size; i++) { int xx = x + i; if (xx >= 0 && xx < g_pw) row[xx] = px[j * size + i]; }
			}
		else ui_fill (x, y, size, size, mix (bg, 0x808080, 60));
		round_corners (x, y, size, size, radius, bg);
	}
	// the cover's main colour (a page's band, now playing's backdrop)
	unsigned tone (int album)
	{
		if (!lib || album < 0 || album >= lib->nal) return 0x303848;
		const unsigned *px = scaled (key_of (album), album, 8);
		if (!px) return 0x303848;
		unsigned r = 0, g = 0, b = 0;
		for (int i = 0; i < 64; i++) { r += px[i] >> 16 & 255; g += px[i] >> 8 & 255; b += px[i] & 255; }
		return (r / 64) << 16 | (g / 64) << 8 | (b / 64);
	}
	// the window's turn: the pictures the thread made
	void pump ()
	{
		for (;;)
		{
			pthread_mutex_lock (&m_lk);
			bool any = m_ndone > 0; Done d = m_done[0];
			if (any) { m_ndone--; memmove (m_done, m_done + 1, sizeof (Done) * m_ndone); }
			pthread_mutex_unlock (&m_lk);
			if (!any) return;
			Base *b = base_of (d.key);
			if (b && b->pending)
			{
				b->pending = false;
				if (d.px) b->px = d.px; else { b->px = new unsigned[BASE * BASE]; memset (b->px, 0x50, sizeof (unsigned) * BASE * BASE); }
				d.px = 0;
			}
			delete[] d.px;
			loaded = true;
		}
	}

private:
	struct Base { unsigned key; unsigned *px; unsigned use; bool pending; };
	struct Scaled { unsigned key; int size; unsigned *px; unsigned use; };
	struct Req { unsigned key; char path[300]; u64 off; unsigned len; };
	struct Done { unsigned key; unsigned *px; };
	Base m_base[NBASE]; Scaled m_sc[NSCALED]; unsigned m_tick;
	pthread_mutex_t m_lk;
	Req m_q[QN]; int m_qn;
	pthread_t m_tid; volatile bool m_quit; bool m_have;
	Done m_done[QN]; int m_ndone;

	Base *base_of (unsigned key) { for (int i = 0; i < NBASE; i++) if (m_base[i].key == key) return &m_base[i]; return 0; }
	Base *base_slot ()
	{
		int best = -1;
		for (int i = 0; i < NBASE; i++) { if (!m_base[i].key) return &m_base[i]; if (!m_base[i].pending && (best < 0 || m_base[i].use < m_base[best].use)) best = i; }
		if (best < 0) return 0;
		Base &b = m_base[best]; delete[] b.px;
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == b.key) { delete[] m_sc[i].px; memset (&m_sc[i], 0, sizeof m_sc[i]); }
		memset (&b, 0, sizeof b); return &b;
	}
	const unsigned *scaled (unsigned key, int album, int size)
	{
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == key && m_sc[i].size == size && m_sc[i].px) { m_sc[i].use = ++m_tick; return m_sc[i].px; }
		Base *b = base_of (key);
		if (!b)
		{
			b = base_slot (); if (!b) return 0;
			b->key = key; b->use = ++m_tick;
			const Album &a = lib->al[album];
			if (a.cover >= 0) { b->pending = true; ask (key, lib->s[a.cover]); }
			else b->px = generated (a.title, a.artist);
		}
		b->use = ++m_tick;
		if (!b->px) return 0;					// (loading)
		int k = -1; unsigned old = ~0u;
		for (int i = 0; i < NSCALED; i++) { if (!m_sc[i].px) { k = i; break; } if (m_sc[i].use < old) { old = m_sc[i].use; k = i; } }
		Scaled &s = m_sc[k]; delete[] s.px;
		s.key = key; s.size = size; s.use = ++m_tick; s.px = new unsigned[(unsigned) size * size];
		fit_cover (b->px, BASE, BASE, s.px, size, size);
		return s.px;
	}
	void ask (unsigned key, const Song &x)
	{
		pthread_mutex_lock (&m_lk);
		if (m_qn < QN)
		{
			Req &r = m_q[m_qn++]; r.key = key;
			if (x.coverLen) { scopy (r.path, x.path, sizeof r.path); r.off = x.coverOff; r.len = x.coverLen; }
			else { scopy (r.path, x.folderCover, sizeof r.path); r.off = 0; r.len = 0; }
		}
		pthread_mutex_unlock (&m_lk);
	}
	// a picture drawn from the album's name: two colours, a gradient, shapes (no colour of the theme: it is the album's)
	static unsigned *generated (const char *title, const char *artist)
	{
		static const unsigned PAL[10][3] = { { 0x16284F, 0x28AAA0, 0xB4F0C8 }, { 0xF0E4CD, 0x28466E, 0xDC5A46 }, { 0x1E3C46, 0x5A96A0, 0xE6E6DC },
			{ 0xFAC85A, 0xC8503C, 0x3C2832 }, { 0x3C3C42, 0x969696, 0xE67828 }, { 0x141E3C, 0x3C5AAA, 0xF0C878 }, { 0xF5F0E1, 0x785A32, 0x281E14 },
			{ 0x5A1E46, 0xDC5A8C, 0xFAD2AA }, { 0xC8DCE6, 0x5A788C, 0xE68C5A }, { 0x1E1432, 0x7850C8, 0x78E6DC } };
		unsigned h = hash_str (artist, hash_str (title));
		const unsigned *c = PAL[h % 10];
		unsigned *px = new unsigned[BASE * BASE];
		for (int y = 0; y < BASE; y++) px_rect (px, BASE, 0, y, BASE, 1, mix (c[0], mix (c[0], c[1], 90), y * 256 / BASE));
		int style = (int) ((h >> 8) % 4);
		unsigned r = h;
		auto rnd = [&r] (int m) { r = r * 1103515245u + 12345u; return (int) ((r >> 16) % (unsigned) m); };
		if (style == 0)
			for (int k = 0; k < 6; k++)
			{
				int y0 = 70 + k * 30 + rnd (20), ph = rnd (60);
				for (int x = 0; x < BASE; x += 2) px_disc (px, BASE, x, y0 + (int) (20 * sinf ((x + ph) * 0.02f + k)), 5, k % 2 ? c[1] : c[2]);
			}
		else if (style == 1) { px_disc (px, BASE, BASE / 2, BASE / 2 - 16, 96, c[1]); px_disc (px, BASE, BASE / 2, BASE / 2 - 16, 44, c[2]); px_rect (px, BASE, 0, BASE * 3 / 4, BASE, BASE / 4, c[2]); }
		else if (style == 2)
			for (int y = 0; y < BASE; y++) for (int k = -9; k < 12; k++) px_rect (px, BASE, k * 40 + y, y, 18, 1, k % 3 ? c[1] : c[2]);
		else
			for (int yy = 0; yy < 6; yy++)
				for (int xx = 0; xx < 6; xx++)
				{
					int cx = xx * BASE / 6 + BASE / 12, cy = yy * BASE / 6 + BASE / 12, rr = 10 + rnd (14);
					unsigned col = (xx + yy) % 3 ? c[1] : c[2];
					if ((xx + yy + (int) (h & 7)) % 3) px_disc (px, BASE, cx, cy, rr, col); else px_rect (px, BASE, cx - rr, cy - rr, 2 * rr, 2 * rr, col);
				}
		return px;
	}
	// ---- the loader's thread ----
	static void *thread_main (void *p) { ((Covers *) p)->run (); return 0; }
	void run ()
	{
		while (!m_quit)
		{
			Req r; bool have = false;
			pthread_mutex_lock (&m_lk);
			if (m_qn > 0) { r = m_q[0]; memmove (m_q, m_q + 1, sizeof (Req) * (m_qn - 1)); m_qn--; have = true; }
			pthread_mutex_unlock (&m_lk);
			if (!have) { sleep_ms (40); continue; }
			Done d = { r.key, load (r) };
			pthread_mutex_lock (&m_lk);
			if (m_ndone < QN) m_done[m_ndone++] = d; else delete[] d.px;
			pthread_mutex_unlock (&m_lk);
		}
	}
	static unsigned *load (const Req &r)
	{
		unsigned char *b = 0; unsigned n = 0;
		if (r.len)
		{
			if (r.len > (16u << 20)) return 0;
			Src s; if (!s.open (r.path) || !s.seek (r.off)) return 0;
			b = (unsigned char *) malloc (r.len); n = r.len;
			if (!b || s.read (b, r.len) != r.len) { free (b); return 0; }
		}
		else { b = (unsigned char *) read_all (r.path, 32u << 20, &n); if (!b) return 0; }
		int w = 0, h = 0;
		unsigned *im = image_decode (b, (int) n, &w, &h);
		free (b);
		if (!im) return 0;
		unsigned *px = new unsigned[BASE * BASE];
		fit_cover (im, w, h, px, BASE, BASE);
		free (im);
		return px;
	}
};

} // namespace media

#endif
