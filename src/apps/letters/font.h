//
// font.h -- Letters' text on ARMPI: UIKit's fonts (DejaVu: sans, serif, mono; bold, italic). The document keeps its
// fonts' names; on the screen a name is a family (layout.h: resolve_font).
//
// The layout needs a character's advance often and fast: each one is measured once by ui_text_width at 1000 px (a
// design advance, in 1/1000 em) and kept, a table a face for the Latin letters, a small hash table for the others.
// An advance at any size and zoom is that value scaled: no call to UIKit, and a line breaks at the same word at every
// zoom. Kerning is not applied in the layout.
//
#ifndef _writer_font_h
#define _writer_font_h

#include <uikit.h>

namespace wr {

enum { FAM_SANS, FAM_SERIF, FAM_MONO, FAM_COUNT };
enum { TF_BOLD = 1, TF_ITALIC = 2 };
enum { TF_EM = 1000, TF_FACES = 10, TF_DIRECT = 0x250, TF_WIDE = 1 << 12 };

struct TFont
{
	int face, size64;			// UFN_*, the size (1/64 px)
	int px;					// the size it is drawn at
	long ui;				// UIKit's font
	int ascent, descent, height;		// 1/64 px
	int ulPos, ulThick, xHeight;		// the underline's, under the baseline
	TFont *next;
};

static TFont *s_fonts;
static unsigned short s_adv[TF_FACES][TF_DIRECT];	// +1 (0: not measured)
static struct { unsigned key; unsigned short adv; } s_wide[TF_WIDE];

static int tf_utf8 (unsigned c, char *o)
{
	if (c < 0x80) { o[0] = (char) c; o[1] = 0; return 1; }
	if (c < 0x800) { o[0] = (char) (0xC0 | c >> 6); o[1] = (char) (0x80 | (c & 63)); o[2] = 0; return 2; }
	if (c < 0x10000) { o[0] = (char) (0xE0 | c >> 12); o[1] = (char) (0x80 | (c >> 6 & 63)); o[2] = (char) (0x80 | (c & 63)); o[3] = 0; return 3; }
	o[0] = (char) (0xF0 | c >> 18); o[1] = (char) (0x80 | (c >> 12 & 63)); o[2] = (char) (0x80 | (c >> 6 & 63)); o[3] = (char) (0x80 | (c & 63)); o[4] = 0;
	return 4;
}
// A character's advance in a face, 1/1000 em.
static int tf_design (int face, long ui, unsigned c)
{
	if (c < 32) return 0;
	unsigned short *slot;
	if (c < TF_DIRECT) slot = &s_adv[face][c];
	else
	{
		unsigned key = (unsigned) face << 24 ^ c, h = (c * 2654435761u >> 20) & (TF_WIDE - 1);
		int probes = 0;
		while (s_wide[h].key && s_wide[h].key != key && probes < 32) { h = (h + 1) & (TF_WIDE - 1); probes++; }
		if (s_wide[h].key != key) { s_wide[h].key = key; s_wide[h].adv = 0; }
		slot = &s_wide[h].adv;
	}
	if (!*slot)
	{
		char u[8]; tf_utf8 (c, u);
		long w = ui_text_width ((long) u, TF_EM, ui);
		*slot = (unsigned short) (w < 0 ? 1 : w > 60000 ? 60001 : w + 1);
	}
	return *slot - 1;
}

// A family in a style at a size.
static TFont *tf_get (int fam, int style, int size64)
{
	static const unsigned char FACE[FAM_COUNT][4] = {
		{ UFN_SANS, UFN_BOLD, UFN_ITALIC, UFN_BOLDITALIC },
		{ UFN_SERIF, UFN_SERIFBOLD, UFN_SERIFITALIC, UFN_SERIFBOLDITALIC },
		{ UFN_MONO, UFN_MONOBOLD, UFN_MONO, UFN_MONOBOLD } };
	int face = FACE[fam < 0 || fam >= FAM_COUNT ? 0 : fam][style & 3];
	if (size64 < 64) size64 = 64;
	for (TFont *f = s_fonts; f; f = f->next) if (f->face == face && f->size64 == size64) return f;
	TFont *f = new TFont;
	f->face = face; f->size64 = size64; f->px = (size64 + 32) >> 6; if (f->px < 1) f->px = 1;
	f->ui = ui_font_get (face);
	f->ascent = size64 * 928 / 1000; f->descent = size64 * 236 / 1000;	// (DejaVu's)
	f->height = f->ascent + f->descent;
	f->ulPos = size64 * 90 / 1000; f->ulThick = size64 * 48 / 1000; f->xHeight = size64 * 530 / 1000;
	f->next = s_fonts; s_fonts = f;
	return f;
}
static inline int tf_advance (const TFont *f, unsigned c)
{
	return (int) ((long long) tf_design (f->face, f->ui, c) * f->size64 / TF_EM);
}
// UTF-8 at (x, baseline), pixels of the view.
static inline void tf_text (const TFont *f, const char *u8, int x, int base, unsigned col)
{
	ui_text ((long) u8, x, base, col, f->px, f->ui, 0);
}

} // namespace wr

#endif
