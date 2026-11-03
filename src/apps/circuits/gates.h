//
// gates.h -- Circuits' drawing pieces (the window's, included by main.cpp only; Onyx's gates.h on UIKit): the colours,
// taken from the theme's tokens, text, the gates' symbols (the ANSI "distinctive shapes", their curves sampled with
// integer arithmetic into ui_vp_poly polygons, 1/16 px), the stars and the padlock, the wrapped text, and the palette's
// icons. Everything draws in the view being painted.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_gates_h
#define _circuits_gates_h

// ---- the theme: UIKit's tokens, never theme.txt -----------------------------------------------------------------------
// (Onyx's board is a pale green paper whatever the theme; here it follows the desktop, Givre included: paper = a field,
// its strips hollow, a lit wire the theme's success, a lamp and a star its warning)
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c) & 0xFFFFFF; }
#define C_BG		jet (J_FACE)
#define C_FIELD		jet (J_SURFACE)
#define C_FIELD_TEXT	jet (J_TEXTE1)
#define C_TEXT		jet (J_TEXTE1)
#define C_DIM		jet (J_TEXTE2)
#define C_DIS		jet (J_TEXTE3)
#define C_LINE		jet (J_LIGNE)
#define C_ACCENT	jet (J_ACCENT)
#define BOARD_BG	jet (J_SURFACE)
#define STRIP_BG	jet (J_CREUX)
#define BOARD_DOT	mix (BOARD_BG, C_TEXT, 70)
#define WIRE_1		jet (J_SUCCES)			// a wire at 1
#define WIRE_0		mix (BOARD_BG, C_TEXT, 170)	// a wire at 0
#define WIRE_X		mix (BOARD_BG, C_TEXT, 80)	// not computed yet (step mode): dashed
#define WIRE_OPEN	mix (BOARD_BG, C_TEXT, 120)	// a pin with no wire
#define GATE_INK	jet (J_TEXTE1)
#define GATE_FACE	jet (J_ELEVE)
#define GATE_FACE1	mix (GATE_FACE, WIRE_1, 70)	// a gate whose output is 1
#define GATE_FACEX	mix (GATE_FACE, BOARD_BG, 170)	// not computed yet (step mode)
#define LAMP_ON		jet (J_ALERTE)
#define LAMP_OFF	mix (BOARD_BG, C_TEXT, 110)
#define ERR_RED		jet (J_DANGER)
#define OK_GREEN	jet (J_SUCCES)
#define STAR_GOLD	jet (J_ALERTE)
#define WARN_TEXT	mix (jet (J_ALERTE), C_TEXT, 120)	// (a word in that colour, readable on a light field too)

// ---- text ------------------------------------------------------------------------------------------------------------
enum { PX_TINY = 9, PX_SMALL = 11, PX_UI = 13, PX_BIG = 18, PX_HUGE = 30 };
static inline const char *TR (const char *s) { return (const char *) ui_tr ((long) s); }
#define TRN(s) s					// a word translated where it is shown
static inline long face (int style) { return style == 2 ? (long) ui.font_b : (long) ui.font; }
static inline int fh (int px = PX_UI) { return px * 5 / 4; }
static inline int tw (const char *s, int style = 0, int px = PX_UI) { return (int) ui_text_width ((long) s, px, face (style)); }
// s, the line's top at y; cut with "..." past w (0: not cut)
static inline void text (int x, int y, const char *s, unsigned c, int style = 0, int px = PX_UI, int w = 0)
{
	if (w > 0) ui_text_fit ((long) s, x, y + px, c, px, face (style), w);
	else ui_text ((long) s, x, y + px, c, px, face (style), 0);
}
// at the left of a box of height h, centred on it; centred in a box
static inline void text_l (int x, int y, int h, const char *s, unsigned c, int style = 0, int px = PX_UI, int w = 0) { text (x, y + (h - fh (px)) / 2, s, c, style, px, w); }
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int style = 0, int px = PX_UI) { text (x + (w - tw (s, style, px)) / 2, y + (h - fh (px)) / 2, s, c, style, px); }

// ---- boxes, in the theme's style: rounded in the flat ones ------------------------------------------------------------
static inline void rbox (int x, int y, int w, int h, int r, unsigned top, unsigned bot) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, top, bot); }
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { rbox (x, y, w, h, r, c, c); }
static void panel (int x, int y, int w, int h, int r, unsigned fill, unsigned line)
{
	rbox (x, y, w, h, r, line); rbox (x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fill);
}

// ---- pens, 1/16 px ----------------------------------------------------------------------------------------------------
#define V(a) ((a) * 16)
static inline void seg (int x0, int y0, int x1, int y1, unsigned c, int w) { ui_vp_line (x0, y0, x1, y1, w, c); }
static inline void disc (int cx, int cy, int r, unsigned c) { ui_vp_disc (cx, cy, r, c); }
static inline int isin (int deg) { return (int) lroundf (sinf ((float) deg * 3.14159265f / 180) * 16384); }
static inline int icos (int deg) { return isin (deg + 90); }
// A circle's outline, its radius r, w wide (ui_vp_disc has no hole)
static void circle_line (int cx, int cy, int r, int w, unsigned c)
{
	int px = cx + r, py = cy;
	for (int a = 15; a <= 360; a += 15)
	{
		int x = cx + (int) ((long long) r * icos (a) / 16384), y = cy + (int) ((long long) r * isin (a) / 16384);
		seg (px, py, x, y, c, w);
		px = x; py = y;
	}
}
static void polyline (const int *xy, int n, int w, unsigned c, bool closed = false)
{
	for (int i = 0; i + 1 < n; i++) seg (xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], c, w);
	if (closed) seg (xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], c, w);
}

// ---- the gates' names on screen (the pack and the circuit text keep the English ones: the engine's) ---------------------
static const char *const GATE_WORD[P_COUNT_] = { "", "", TRN ("NOT"), TRN ("AND"), TRN ("OR"), TRN ("XOR"), TRN ("NAND"), TRN ("NOR") };
static const char *gate_word (int t) { return t >= P_NOT && t < P_COUNT_ ? TR (GATE_WORD[t]) : ""; }

// ---- the gates' outlines -----------------------------------------------------------------------------------------------
// A quadratic curve (x0, y0) .. (x2, y2) pulled by (x1, y1), sampled at steps + 1 points (skipFirst: its first
// point is the previous curve's last) into xy from n -> the new n. Coordinates in 1/16 px.
static int qbez (int *xy, int n, int x0, int y0, int x1, int y1, int x2, int y2, int steps, bool skipFirst)
{
	int d = steps * steps;
	for (int i = skipFirst ? 1 : 0; i <= steps; i++)
	{
		int a = (steps - i) * (steps - i), b = 2 * (steps - i) * i, c = i * i;
		xy[2 * n] = (a * x0 + b * x1 + c * x2 + d / 2) / d;
		xy[2 * n + 1] = (a * y0 + b * y1 + c * y2 + d / 2) / d;
		n++;
	}
	return n;
}
// The body of a gate of `type` in the box (L, T)-(R, B), the bubble's radius rb (NOT, NAND, NOR: the body ends
// before it): its closed outline's points in xy (room for 40) -> how many.
static int gate_outline (int type, int L, int T, int R, int B, int rb, int *xy)
{
	int cy = (T + B) / 2, h2 = (B - T) / 2, n = 0;
	bool bub = type == P_NOT || type == P_NAND || type == P_NOR;
	int Rb = bub ? R - 2 * rb : R;
	switch (type)
	{
	case P_NOT:
		xy[0] = L; xy[1] = cy - h2 * 78 / 100; xy[2] = Rb; xy[3] = cy; xy[4] = L; xy[5] = cy + h2 * 78 / 100;
		return 3;
	case P_AND: case P_NAND:			// a flat back, a half disc in front
	{
		int r = h2, cx = Rb - r;
		xy[2 * n] = L; xy[2 * n + 1] = T; n++;
		for (int a = -90; a <= 90; a += 10)
		{
			xy[2 * n] = cx + (int) ((long long) r * icos (a) / 16384);
			xy[2 * n + 1] = cy + (int) ((long long) r * isin (a) / 16384);
			n++;
		}
		xy[2 * n] = L; xy[2 * n + 1] = B; n++;
		return n;
	}
	default:					// OR, NOR, XOR: the shield (XOR's moved 12 % right: its back curve before it)
	{
		int l = type == P_XOR ? L + (R - L) * 12 / 100 : L;
		n = qbez (xy, n, l, T, l + (Rb - l) * 58 / 100, T, Rb, cy, 10, false);
		n = qbez (xy, n, Rb, cy, l + (Rb - l) * 58 / 100, B, l, B, 10, true);
		n = qbez (xy, n, l, B, l + (Rb - l) * 26 / 100, cy, l, T, 8, true);
		return n - 1;				// (the last point is the first)
	}
	}
}
// A gate's symbol drawn in the box (L, T)-(R, B) (1/16 px): its body in `face`, its outline (sw wide) in `ink`, XOR's
// back curve, the bubble.
static void draw_gate_shape (int type, int L, int T, int R, int B, int rb, unsigned face, unsigned ink, int sw)
{
	int xy[80], m = gate_outline (type, L, T, R, B, rb, xy), cy = (T + B) / 2;
	ui_vp_poly ((long) xy, m, face);
	polyline (xy, m, sw, ink, true);
	if (type == P_XOR)
	{
		int bx[40], k = qbez (bx, 0, L, B, L + (R - L) * 24 / 100, cy, L, T, 8, false);
		polyline (bx, k, sw, ink);
	}
	if (type == P_NOT || type == P_NAND || type == P_NOR)
	{
		disc (R - rb, cy, rb, ink);
		disc (R - rb, cy, rb - sw, face);
	}
}

// ---- stars, padlock ----------------------------------------------------------------------------------------------------
static void star (int cx, int cy, int r, unsigned c, bool filled)
{
	int pts[20];
	for (int i = 0; i < 10; i++)
	{
		int rr = i & 1 ? r * 2 / 5 : r, a = i * 36;
		pts[2 * i] = V (cx) + isin (a) * rr * 16 / 16384;
		pts[2 * i + 1] = V (cy) - icos (a) * rr * 16 / 16384;
	}
	if (filled) ui_vp_poly ((long) pts, 10, c);
	else polyline (pts, 10, V (1) + 8, c, true);
}
static void padlock (int cx, int cy, unsigned c)
{
	rbox (cx - 5, cy - 1, 10, 8, 2, c);
	int y = V (cy - 2), r = V (3) + 8, px = V (cx) - r, py = y;
	for (int a = 195; a <= 360; a += 15)
	{
		int x = V (cx) + (int) ((long long) r * icos (a) / 16384), yy = y + (int) ((long long) r * isin (a) / 16384);
		seg (px, py, x, yy, c, V (1) + 8);
		px = x; py = yy;
	}
	seg (V (cx) - r, y, V (cx) - r, V (cy), c, V (1) + 8);
	seg (V (cx) + r, y, V (cx) + r, V (cy), c, V (1) + 8);
}

// ---- wrapped text (ui_text_wrap has no limit of lines and no centring): the lines' starts and lengths --------------------
enum { MAXLINES = 16 };
static int wrap_lines (const char *s, int w, int maxL, int style, int px, int *st, int *ln)
{
	int n = 0, len = (int) strlen (s), i = 0;
	if (maxL > MAXLINES) maxL = MAXLINES;
	while (i < len && n < maxL)
	{
		int end = i, best = -1;				// (the last space that fits)
		while (s[end] && s[end] != '\n')
		{
			int e = end;
			while (s[e] == ' ') e++;
			while (s[e] && s[e] != ' ' && s[e] != '\n') e++;
			if ((int) ui_text_prefix ((long) (s + i), e - i, px, face (style)) > w) break;
			end = e; best = e;
		}
		if (best < 0) { best = end; if (best == i) while (s[best] && s[best] != ' ' && s[best] != '\n') best++; }	// (a word longer than the line)
		st[n] = i; ln[n] = best - i; n++;
		i = best;
		while (s[i] == ' ') i++;
		if (s[i] == '\n') i++;
	}
	return n;
}
static int wrap_count (const char *s, int w, int maxL, int style = 0, int px = PX_UI)
{ int st[MAXLINES], ln[MAXLINES]; return wrap_lines (s, w, maxL, style, px, st, ln); }
// The lines from y, lh apart; centred in w when centre -> how many
static int wrap_draw (int x, int y, int w, const char *s, unsigned c, int maxL, int lh, int style = 0, bool centre = false, int px = PX_UI)
{
	int st[MAXLINES], ln[MAXLINES], n = wrap_lines (s, w, maxL, style, px, st, ln);
	for (int i = 0; i < n; i++)
	{
		char l[400]; int m = ln[i] < 399 ? ln[i] : 399; memcpy (l, s + st[i], (size_t) m); l[m] = 0;
		if (centre) text_c (x, y + i * lh, w, lh, l, c, style, px); else text (x, y + i * lh, l, c, style, px);
	}
	return n;
}

// ---- the palette's icons: the select arrow, the gates; a size >= 30 has its name under the symbol -------------------------
enum { IC_SELECT = 100 };
static void arrow_pointer (int px, int py, unsigned c)
{
	int pts[] = { V (px), V (py), V (px), V (py + 15), V (px + 4), V (py + 11), V (px + 7), V (py + 17), V (px + 9), V (py + 16),
		      V (px + 6), V (py + 10), V (px + 11), V (py + 10) };
	ui_vp_poly ((long) pts, 7, c);
}
static void circuits_icon (int id, int x, int y, int size, unsigned ink, unsigned hollow)
{
	if (id == IC_SELECT)
	{
		text_c (x - 8, y + size - 12, size + 16, 12, TR ("Select"), ink, 0, PX_TINY + 1);
		arrow_pointer (x + size / 2 - 5, y + 1, ink);
		return;
	}
	if (id < P_NOT || id >= P_COUNT_) return;
	const char *nm = gate_word (id);
	text_c (x - 8, y + size - 12, size + 16, 12, nm, ink, 0, tw (nm, 0, PX_TINY + 1) > size ? PX_TINY : PX_TINY + 1);
	int s2 = size * 2 / 3; x += (size - s2) / 2; y += 1; size = s2;
	int s = size * 16;
	int L = x * 16 + s * 22 / 100, R = x * 16 + s * 86 / 100, cy = y * 16 + s / 2, h2 = s * 30 / 100, rb = s * 7 / 100;
	if (id == P_NOT) seg (x * 16, cy, L + 8, cy, ink, V (1) + 4);
	else { seg (x * 16, cy - h2 * 55 / 100, L + 24, cy - h2 * 55 / 100, ink, V (1) + 4); seg (x * 16, cy + h2 * 55 / 100, L + 24, cy + h2 * 55 / 100, ink, V (1) + 4); }
	seg (R - 8, cy, (x + size) * 16, cy, ink, V (1) + 4);
	draw_gate_shape (id, L, cy - h2, R, cy + h2, rb, hollow, ink, V (1) + 6);
}

#endif
