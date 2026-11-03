//
// html_ui.h -- Mail's HTML renderer on UIKit, in place of Onyx's html_ft.h (FreeType and uikit::Canvas): the Host (UIKit's
// DejaVu fonts measured: ui_text_width, at 1000 px once for each character, then scaled) and the Painter (rectangles
// blended and pictures scaled in the target's pixels, the text by ui_text). The families: sans (Arial, Helvetica...)
// -> DejaVu Sans, serif (Times, Georgia) -> DejaVu Serif, Courier / monospace -> DejaVu Sans Mono, Segoe -> Sans. The
// pictures come from the app (Pictures).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef _mail_html_ui_h
#define _mail_html_ui_h

#include "html.h"
#include "ui.h"

namespace mail {
namespace html {

// the pictures: a src -> its pixels (0xAARRGGBB, w x h; 0: not shown)
struct Pictures
{
	virtual ~Pictures () {}
	virtual const unsigned *get (const char *src, int *w, int *h) = 0;
};

struct UiHost : Host, Painter
{
	struct F { int ufn, px; };
	F fonts[96]; int nf;
	long faces[10]; unsigned short *adv[10];		// per UFN_*: the font, the advances of U+0000..U+07FF (1/1000 em; 0xFFFF: not measured)
	uint32_t *px; int pw, ph;				// the target's pixels
	int cx0, cy0, cx1, cy1;
	Pictures *pics;

	UiHost () : nf (0), px (0), pw (0), ph (0), cx0 (0), cy0 (0), cx1 (0), cy1 (0), pics (0) { memset (faces, 0, sizeof faces); memset (adv, 0, sizeof adv); }
	void target (uint32_t *p, int w, int h) { px = p; pw = w; ph = h; }
	long font_of (int ufn)
	{
		if (!faces[ufn]) faces[ufn] = ufn == UFN_SANS ? (long) mailapp::ui.font : ufn == UFN_BOLD ? (long) mailapp::ui.font_b : ufn == UFN_MONO ? (long) mailapp::ui.font_m : ui_font_get (ufn);
		return faces[ufn];
	}
	int advance (int ufn, unsigned cp)
	{
		if (cp == 0xA0) cp = ' ';
		if (cp >= 0x800)
		{
			char t[5]; int k = u8put (t, cp); t[k] = 0;
			return (int) ui_text_width ((long) t, 1000, font_of (ufn));
		}
		if (!adv[ufn]) { adv[ufn] = (unsigned short *) malloc (0x800 * 2); if (!adv[ufn]) return 600; memset (adv[ufn], 0xFF, 0x800 * 2); }
		unsigned short &a = adv[ufn][cp];
		if (a == 0xFFFF)
		{
			char t[5]; int k = u8put (t, cp ? cp : ' '); t[k] = 0;
			long w = ui_text_width ((long) t, 1000, font_of (ufn));
			a = (unsigned short) (w < 0 ? 0 : w > 60000 ? 60000 : w);
		}
		return a;
	}
	// ---- Host ----
	int font (int family, int size, bool bold, bool italic) override
	{
		if (size < 4) size = 4;
		int base = family == FF_SERIF || family == FF_GEORGIA ? UFN_SERIF : family == FF_MONO ? UFN_MONO : UFN_SANS;
		int u;
		if (base == UFN_SERIF) u = bold ? (italic ? UFN_SERIFBOLDITALIC : UFN_SERIFBOLD) : (italic ? UFN_SERIFITALIC : UFN_SERIF);
		else if (base == UFN_MONO) u = bold ? UFN_MONOBOLD : UFN_MONO;
		else u = bold ? (italic ? UFN_BOLDITALIC : UFN_BOLD) : (italic ? UFN_ITALIC : UFN_SANS);
		for (int i = 0; i < nf; i++) if (fonts[i].ufn == u && fonts[i].px == size) return i;
		if (nf == 96) return 0;
		fonts[nf].ufn = u; fonts[nf].px = size;
		return nf++;
	}
	int width (int h, const char *s, int n) override
	{
		if (h < 0 || h >= nf) return n * 7;
		long sum = 0; const char *e = s + n;
		while (s < e) { unsigned cp = u8get (s); if (s > e) break; sum += advance (fonts[h].ufn, cp); }
		return (int) ((sum * fonts[h].px + 500) / 1000);
	}
	void metrics (int h, int *asc, int *desc) override
	{
		int p = h >= 0 && h < nf ? fonts[h].px : 14;
		*asc = (p * 928 + 999) / 1000; *desc = (p * 236 + 999) / 1000;		// (DejaVu's: the ABI does not give them)
	}
	bool image (const char *src, int *w, int *h) override
	{
		*w = *h = 0;
		return pics && pics->get (src, w, h) != 0;
	}
	// ---- Painter ----
	void clip (int x0, int y0, int x1, int y1) override
	{
		cx0 = x0 < 0 ? 0 : x0; cy0 = y0 < 0 ? 0 : y0;
		cx1 = x1 > pw ? pw : x1; cy1 = y1 > ph ? ph : y1;
	}
	static inline void blend (uint32_t &d, unsigned c, unsigned a)
	{
		unsigned na = 255 - a, o = d;
		unsigned r = ((c >> 16 & 255) * a + (o >> 16 & 255) * na) / 255, g = ((c >> 8 & 255) * a + (o >> 8 & 255) * na) / 255, b = ((c & 255) * a + (o & 255) * na) / 255;
		d = r << 16 | g << 8 | b;
	}
	void fill (int x, int y, int w, int h, unsigned c) override
	{
		if (!px) return;
		int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
		if (x0 >= x1 || y0 >= y1) return;
		unsigned a = c >> 24;
		for (int j = y0; j < y1; j++)
		{
			uint32_t *d = px + (long) j * pw;
			if (a >= 255) for (int i = x0; i < x1; i++) d[i] = c & 0xFFFFFF;
			else for (int i = x0; i < x1; i++) blend (d[i], c, a);
		}
	}
	void text (int h, int x, int base, const char *s, int n, unsigned c) override
	{
		if (h < 0 || h >= nf || n <= 0) return;
		char st[512], *t = n < (int) sizeof st ? st : (char *) malloc ((size_t) n + 1);
		if (!t) return;
		memcpy (t, s, (size_t) n); t[n] = 0;
		for (int i = 0; i + 1 < n; i++) if ((unsigned char) t[i] == 0xC2 && (unsigned char) t[i + 1] == 0xA0) { t[i] = ' '; t[i + 1] = ' '; }	// (a word cut there: no break)
		ui_clip (cx0, cy0, cx1 - cx0, cy1 - cy0);
		ui_text ((long) t, x, base, c & 0xFFFFFF, fonts[h].px, font_of (fonts[h].ufn), 4000);
		ui_clip (0, 0, 0, 0);
		if (t != st) free (t);
	}
	void picture (const char *src, int x, int y, int w, int h) override
	{
		if (!pics || !px || w <= 0 || h <= 0) return;
		int iw, ih; const unsigned *p = pics->get (src, &iw, &ih);
		if (!p || iw <= 0 || ih <= 0) return;
		int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
		for (int j = y0; j < y1; j++)
		{
			const unsigned *srow = p + (long) ((long) (j - y) * ih / h) * iw;
			uint32_t *d = px + (long) j * pw;
			for (int i = x0; i < x1; i++)
			{
				unsigned s = srow[(long) (i - x) * iw / w], a = s >> 24;
				if (a >= 255) d[i] = s & 0xFFFFFF; else if (a) blend (d[i], s, a);
			}
		}
	}
};

} // namespace html
} // namespace mail

#endif
