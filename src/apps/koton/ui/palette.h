//
// ui/palette.h -- Koton's look: the desktop's theme (it was a dark studio of its own: docs/daw/mockups).
// The uikit widgets draw with the theme as in any app; the hand-drawn views (the grids, the rings, the
// panels) use the shades below, made from the theme's colours by applyTheme (), and the small drawing
// helpers here (fills with opacity, rounded boxes, text). The arrangement's lanes alone stay dark.
//
#ifndef _koton_palette_h
#define _koton_palette_h

#include "ui/view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace kui {

// ---- the colours (0x00RRGGBB) ---------------------------------------------------------------------------------
// the studio's shades: the desktop theme's (applyTheme makes them from its window, field, accent)
static unsigned BG, PANEL, PANEL2, SIDE, FACE, LINE, LANE, LANE2, TEXT, DIM, FAINT, ACC, ACC2, FIELD,
	GRID_BEAT, GRID_BAR, GRID_SUB, NOTE, NOTE_SEL, PAD_ON, BLUE_NOTE;
// the arrangement's lanes stay the dark studio's whatever the theme (the user, 2026-10-03): the
// coloured blocks read best on them
static const unsigned
	ALANE = 0x1C1F26,
	ALANE2 = 0x1F232A,
	ALANE_BG = 0x181B21,
	ALANE_LINE = 0x3A404C,
	ALANE_BAR = 0x2E343E,
	ALANE_CHORD = 0x16191F,
	PLAY = 0xFFC646,
	REC = 0xE2504C,
	GREEN = 0x56CE96,
	FUNC_T = 0x4888E2,
	FUNC_S = 0x46AA84,
	FUNC_D = 0xE28844,
	FUNC_O = 0x8A7AC0;

// the track colours, by track index (a stable, pleasant cycle)
static const unsigned s_trackCols[] = { 0xE2A454, 0xCE627C, 0x76BA70, 0xDE7052, 0xA884DE, 0x5CB8C8, 0xD8C25A, 0x8C9CE0 };
static inline unsigned trackColour (int i) { return s_trackCols[(i < 0 ? 0 : i) % 8]; }
static inline unsigned funcColour (int f) { return f == 0 ? FUNC_T : f == 1 ? FUNC_S : f == 2 ? FUNC_D : FUNC_O; }

static inline unsigned mixc (unsigned a, unsigned b, int t256) { return (unsigned) ui_mix (a, b, t256) & 0x00FFFFFFu; }
static inline unsigned tone (unsigned c, int level) { return (unsigned) ui_tone (c, level) & 0x00FFFFFFu; }
static inline unsigned inkOn (unsigned c) { return (unsigned) ui_ink (c) & 0x00FFFFFFu; }
static inline int bright (unsigned c) { return (((c >> 16) & 255) * 30 + ((c >> 8) & 255) * 59 + (c & 255) * 11) / 100; }
static inline unsigned lighter (unsigned c, int t256) { return mixc (c, 0xFFFFFF, t256); }
static inline unsigned darker (unsigned c, int t256) { return mixc (c, 0x000000, t256); }

// The studio in the desktop's theme: its shades from the theme's colours (the side panels as the
// Media Player's: the window's colour toward the fields').
static inline void applyTheme ()
{
	bool dark = bright (C_BG) < 110;
	PANEL = C_BG; SIDE = mixc (C_BG, C_FIELD, 70); PANEL2 = tone (C_BG, dark ? 150 : 116);
	BG = tone (C_BG, dark ? 100 : 112); FACE = C_BUTTON; LINE = tone (C_BG, dark ? 160 : 100);
	LANE = C_FIELD; LANE2 = mixc (C_FIELD, C_BG, 90); FIELD = C_FIELD;
	TEXT = C_TEXT; DIM = mixc (C_BG, C_TEXT, 150); FAINT = mixc (C_BG, C_TEXT, 90);
	ACC = C_ACCENT; ACC2 = tone (C_ACCENT, 104);
	GRID_SUB = mixc (C_FIELD, C_FIELD_TEXT, 16); GRID_BEAT = mixc (C_FIELD, C_FIELD_TEXT, 34); GRID_BAR = mixc (C_FIELD, C_FIELD_TEXT, 80);
	NOTE = C_ACCENT; PAD_ON = C_ACCENT; NOTE_SEL = 0xE8A21E; BLUE_NOTE = tone (C_ACCENT, dark ? 170 : 110);
}

// ---- drawing ---------------------------------------------------------------------------------------------------------
static inline void clearView (unsigned c) { ui_fill (0, 0, g_pw, g_ph, c); }
static inline void pixel (int x, int y, unsigned c) { if (x >= 0 && y >= 0 && x < g_pw && y < g_ph) g_px[(long) y * g_pw + x] = c; }

// a rectangle of c at opacity a (0..255) over what the canvas holds, clipped
static inline void blendRect (int x, int y, int w, int h, unsigned c, int a)
{
	if (a >= 255) { ui_fill (x, y, w, h, c); return; }
	if (a <= 0) return;
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > g_pw ? g_pw : x + w, y1 = y + h > g_ph ? g_ph : y + h;
	unsigned cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
	for (int yy = y0; yy < y1; yy++)
	{
		unsigned *p = g_px + (long) yy * g_pw;
		for (int xx = x0; xx < x1; xx++)
		{
			unsigned d = p[xx];
			unsigned r = ((d >> 16) & 255) * (255 - a) + cr * a, g = ((d >> 8) & 255) * (255 - a) + cg * a, b = (d & 255) * (255 - a) + cb * a;
			p[xx] = ((r / 255) << 16) | ((g / 255) << 8) | (b / 255);
		}
	}
}
static inline void hline (int x0, int x1, int y, unsigned c) { if (x1 > x0) ui_fill (x0, y, x1 - x0, 1, c); }
static inline void vline (int x, int y0, int y1, unsigned c) { if (y1 > y0) ui_fill (x, y0, 1, y1 - y0, c); }
static inline int V (int px) { return px * 16; }		// (the vector calls: 1/16 px)
static inline void rbox (int x, int y, int w, int h, int r, unsigned top, unsigned bottom, int alpha = 255)
{
	if (alpha < 255) { top = mixc (C_BG, top, alpha + 1); bottom = mixc (C_BG, bottom, alpha + 1); }
	ui_rbox (x, y, w, h, r, top, bottom);
}
static inline void rline (int x, int y, int w, int h, int r, unsigned c, int alpha = 255)
{
	if (alpha < 255) c = mixc (C_BG, c, alpha + 1);
	if (r <= 1) { ui_frame (x, y, w, h, c); return; }
	ui_fill (x + r, y, w - 2 * r, 1, c); ui_fill (x + r, y + h - 1, w - 2 * r, 1, c);
	ui_fill (x, y + r, 1, h - 2 * r, c); ui_fill (x + w - 1, y + r, 1, h - 2 * r, c);
	int a = V (r) / 3, x1 = x + w - 1, y1 = y + h - 1;		// the corners: two strokes
	long cx[4] = { V (x), V (x1), V (x), V (x1) }, cy[4] = { V (y), V (y), V (y1), V (y1) };
	for (int i = 0; i < 4; i++)
	{
		long sx = i & 1 ? -1 : 1, sy = i & 2 ? -1 : 1;
		ui_vp_line (cx[i], cy[i] + sy * V (r), cx[i] + sx * a, cy[i] + sy * a, 16, c);
		ui_vp_line (cx[i] + sx * a, cy[i] + sy * a, cx[i] + sx * V (r), cy[i], 16, c);
	}
}
static inline void sunken (int x, int y, int w, int h, int r, unsigned bg, bool focus = false)
{
	rbox (x, y, w, h, r, bg, bg);
	rline (x, y, w, h, r, focus ? C_ACCENT : C_BORDER);
}
static inline void scrollBar (int x, int y, int w, int h, bool vertical, int pos, int len, unsigned bg, int state = 0)
{
	unsigned groove = mixc (bg, C_TEXT, 16), thumb = mixc (bg, C_TEXT, state ? 130 : 90);
	ui_fill (x, y, w, h, groove);
	if (len <= 0) return;
	if (vertical) rbox (x + 1, y + pos, w - 2, len, 3, thumb, thumb);
	else rbox (x + pos, y + 1, len, h - 2, 3, thumb, thumb);
}
static inline void box (int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, c, c); }
static inline void gbox (int x, int y, int w, int h, int r, unsigned top, unsigned bottom) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, top, bottom); }
static inline void frame (int x, int y, int w, int h, int r, unsigned c, int a = 255) { if (w > 0 && h > 0) rline (x, y, w, h, r, c, a); }

// ---- a list's scroll bar in a hand-drawn view -----------------------------------------------------------------------------
static const int SBW = 10;			// the right-edge gutter it takes
struct Thumb_ { bool show; int y, h; };		// its thumb in a track of trackH px
static inline Thumb_ scrollThumb (long total, long view, long pos, int trackH)
{
	Thumb_ t;
	t.show = total > view && view > 0 && trackH > 6;
	if (!t.show) { t.y = 0; t.h = trackH; return t; }
	int h = (int) (view * trackH / total);
	if (h < 14) h = 14;
	if (h > trackH) h = trackH;
	long range = total - view;
	int trange = trackH - h, y = range > 0 ? (int) (pos * trange / range) : 0;
	t.h = h; t.y = y < 0 ? 0 : y > trange ? trange : y;
	return t;
}
static inline long scrollThumbPos (int cy, int trackH, long total, long view, int thumbH)
{
	int trange = trackH - thumbH; if (trange < 1) trange = 1;
	int p = cy - thumbH / 2;
	p = p < 0 ? 0 : p > trange ? trange : p;
	long range = total - view; if (range < 0) range = 0;
	return range * p / trange;
}
static inline void drawVScroll (int x, int y, int w, int trackH, const Thumb_ &t, unsigned bg, bool hot = false)
{
	scrollBar (x, y, w, trackH, true, t.y, t.h < trackH ? t.h : 0, bg, hot ? 1 : 0);
}
struct BarDrag					// the thumb dragged, a press on the groove a page
{
	bool held, wasDown;
	BarDrag () : held (false), wasDown (false) {}
	bool mouse (int mx, int my, int bl, int barX, int barW, int ty, int trackH, long total, long view, long *pos)
	{
		bool down = bl != 0, press = down && !wasDown;
		wasDown = down;
		if (held)
		{
			if (!down || mx < 0) held = false;
			else { Thumb_ t = scrollThumb (total, view, *pos, trackH); *pos = scrollThumbPos (my - ty, trackH, total, view, t.h); }
			return true;
		}
		if (total <= view || mx < barX || mx >= barX + barW || my < ty || my >= ty + trackH) return false;
		if (press)
		{
			Thumb_ t = scrollThumb (total, view, *pos, trackH);
			int cy = my - ty;
			if (cy >= t.y && cy < t.y + t.h) held = true;
			else
			{
				long p = *pos + (cy < t.y ? -view : view), most = total - view;
				*pos = p < 0 ? 0 : p > most ? most : p;
			}
		}
		return true;
	}
};

// text: left / centred / right in a box of height h
static inline void textL (int x, int y, int h, const char *s, unsigned c, int style = 0)
{
	if (!s || !*s) return;
	ui_text ((long) s, x, y + (h - th ()) / 2 + textPx (), c, textPx (), textFont (style), 0);
}
static inline void textC (int x, int y, int w, int h, const char *s, unsigned c, int style = 0) { textL (x + (w - tw (s, style)) / 2, y, h, s, c, style); }
static inline void textR (int xr, int y, int h, const char *s, unsigned c, int style = 0) { textL (xr - tw (s, style), y, h, s, c, style); }
// a string cut to fit w px ("..." when cut)
static inline void textFit (int x, int y, int w, int h, const char *s, unsigned c, int style = 0)
{
	if (w <= 4) return;
	if (tw (s, style) <= w) { textL (x, y, h, s, c, style); return; }
	char b[160]; int n = 0;
	while (s[n] && n < 150) { b[n] = s[n]; n++; }
	b[n] = 0;
	while (n > 0)
	{
		b[--n] = 0;
		while (n > 0 && ((unsigned char) b[n - 1] & 0xC0) == 0x80) b[--n] = 0;	// a whole UTF-8 character
		char t[164]; snprintf (t, sizeof t, "%s...", b);
		if (tw (t, style) <= w || n == 0) { textL (x, y, h, t, c, style); return; }
	}
}

// text wrapped at word boundaries in w px, lines lineH apart -> the height used
static inline int wrapText (int x, int y, int w, const char *s, unsigned c, int lineH, int style = 0)
{
	int yy = y;
	while (*s)
	{
		char line[400]; int n = 0, lastSpace = -1;
		const char *p = s;
		while (*p && *p != '\n')
		{
			if (n >= 398) { if (lastSpace > 0) { n = lastSpace; p = s + lastSpace + 1; } break; }
			line[n] = *p; line[n + 1] = 0;
			if (*p == ' ') lastSpace = n;
			if (tw (line, style) > w && n > 0)
			{
				if (lastSpace > 0) { n = lastSpace; p = s + lastSpace + 1; }
				break;
			}
			n++; p++;
		}
		line[n] = 0;
		textL (x, yy, lineH, line, c, style);
		yy += lineH;
		if (*p == '\n') p++;
		if (p == s) p++;			// (a word longer than the line)
		s = p;
	}
	return yy - y;
}

// a small filled triangle (a play mark, a disclosure arrow): dir 0 right, 1 down, 2 left, 3 up
static inline void tri (int cx, int cy, int s, int dir, unsigned c)
{
	for (int i = 0; i <= s; i++)
	{
		int len = s - i;
		switch (dir)
		{
		case 0: vline (cx - s / 2 + i, cy - len, cy + len + 1, c); break;
		case 1: hline (cx - len, cx + len + 1, cy - s / 2 + i, c); break;
		case 2: vline (cx + s / 2 - i, cy - len, cy + len + 1, c); break;
		default: hline (cx - len, cx + len + 1, cy + s / 2 - i, c); break;
		}
	}
}

// outlines joined and filled (1/16 px) -- ui/vpath.cpp
int vsin (int deg);
static inline int vcos (int deg) { return vsin (deg + 90); }
class VPath
{
public:
	VPath ();
	~VPath ();
	void clear ();
	void poly (const int *xy, int n);
	void rect (int x, int y, int w, int h);
	void rrect (int x, int y, int w, int h, int r);
	void circle (int cx, int cy, int r);
	void hole (int cx, int cy, int r);
	void ellipse (int cx, int cy, int rx, int ry);
	void line (int x0, int y0, int x1, int y1, int w);
	void polyline (const int *xy, int n, int w, bool closed = false);
	void arc (int cx, int cy, int r, int a0, int a1, int w);
	void arrowHead (int x, int y, int deg, int len, int half);
	void fill (unsigned c, int alpha = 255, int dx = 0, int dy = 0);
private:
	int *m_x, *m_y, m_n, m_cap;
	int *m_cs, m_nc, m_ccap;
	void add (int x, int y);
	void begin ();
	void end (bool asHole = false);
};

// an anti-aliased filled circle and a ring (the polyrhythm wheels, the knobs)
static inline void disc (int cx, int cy, int r, unsigned c, int a = 255)
{
	VPath p; p.circle (V (cx), V (cy), V (r)); p.fill (c, a);
}
static inline void ringArc (int cx, int cy, int r, int a0, int a1, int w, unsigned c, int a = 255)
{
	VPath p; p.arc (V (cx), V (cy), V (r), a0, a1, V (w)); p.fill (c, a);
}
static inline void aline (int x0, int y0, int x1, int y1, int w, unsigned c, int a = 255)
{
	VPath p; p.line (V (x0), V (y0), V (x1), V (y1), V (w < 1 ? 1 : w)); p.fill (c, a);
}

// ---- a drag helper: the mouse's buttons as edges (uikit gives their state) ------------------------------
struct Buttons
{
	int l, r;
	Buttons () : l (0), r (0) {}
	// -> 1 pressed now, -1 released now, 0 no change (left); rightDown: the right one pressed now
	int edge (int bl, int br, bool *rightDown = 0)
	{
		int e = (bl && !l) ? 1 : (!bl && l) ? -1 : 0;
		if (rightDown) *rightDown = br && !r;
		l = bl; r = br;
		return e;
	}
};

} // namespace kui

#endif
