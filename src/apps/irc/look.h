//
// look.h -- what IRC's views share: UIKit's tokens, the nicknames' colours (made from the theme, readable on it),
// the text cut to a width in pixels (UTF-8, a proportional font: Onyx counted characters).
//
#ifndef _irc_look_h
#define _irc_look_h

#include "irc.h"

namespace irc {

static struct ui_vars ui;

// ---- the theme: UIKit's tokens, never theme.txt ---------------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned bg) { return (unsigned) ui_ink (bg) & 0xFFFFFF; }
static inline unsigned danger_text () { return mix (jet (J_DANGER), jet (J_TEXTE1), 80); }
static inline unsigned alert_text () { return mix (jet (J_ALERTE), jet (J_TEXTE1), 128); }

static int imin (int a, int b) { return a < b ? a : b; }
static int imax (int a, int b) { return a > b ? a : b; }

static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)
static int ui_px () { return ui_echelle () >= 2 ? 12 : 13; }
#define PX_SMALL	12
static long f_sans () { return (long) ui.font; }
static long f_bold () { return (long) ui.font_b; }
static long f_mono () { return (long) ui.font_m; }
static long f_ital () { static long f; if (!f) f = ui_font_get (UFN_ITALIC); return f; }
static long f_boldital () { static long f; if (!f) f = ui_font_get (UFN_BOLDITALIC); return f; }

// A line of text in a box, centred on its height (ui_text takes the baseline).
static void text_box (const char *s, int x, int y, int h, unsigned ink, int px, long fnt, int maxw)
{
	ui_text_fit ((long) s, x, y + h / 2 + px * 37 / 100, ink, px, fnt, maxw > 0 ? maxw : 4000);
}
static int text_w (const char *s, int px, long fnt) { return (int) ui_text_width ((long) s, px, fnt); }
static void text_center (const char *s, int x, int y, int w, int h, unsigned ink, int px, long fnt)
{
	int t = text_w (s, px, fnt);
	text_box (s, x + (w - t) / 2, y, h, ink, px, fnt, w);
}
static void text_right (const char *s, int x, int y, int w, int h, unsigned ink, int px, long fnt)
{
	int t = imin (text_w (s, px, fnt), w);
	text_box (s, x + w - t, y, h, ink, px, fnt, w);
}

// ---- the nicknames' colours ------------------------------------------------------------------------------------
// Twelve hues mixed towards the theme's text until they read at 5:1 on its surface: dark on a light theme, light on
// a dark one, never written down. Made once: the theme does not change under a running program.
static unsigned s_nc[12];
static double lum (unsigned c)
{
	int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
	int y = (54 * r + 183 * g + 19 * b) >> 8;
	return y * y / 65025.0;
}
static double contrast (unsigned a, unsigned b)
{
	double x = lum (a), y = lum (b);
	return x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05);
}
static void nick_colors_make ()
{
	for (int k = 0; k < 12; k++)
	{
		int h = k * 30, sector = h / 60, f = (h % 60) * 255 / 60;
		int r = 0, g = 0, b = 0;
		switch (sector)
		{
		case 0: r = 255; g = f; break;
		case 1: r = 255 - f; g = 255; break;
		case 2: g = 255; b = f; break;
		case 3: g = 255 - f; b = 255; break;
		case 4: r = f; b = 255; break;
		default: r = 255; b = 255 - f; break;
		}
		unsigned pure = (unsigned) (r << 16 | g << 8 | b), c = pure;
		for (int t = 96; t <= 224; t += 16)
		{
			c = mix (pure, jet (J_TEXTE1), t);
			if (contrast (c, jet (J_SURFACE)) >= 5.0) break;
		}
		s_nc[k] = c;
	}
}
static unsigned nick_color (const char *nick)
{
	if (ieq (nick, g_nick)) return jet (J_ACCENT);
	unsigned h = 5381;
	for (int i = 0; nick[i]; i++) h = h * 33 + (unsigned char) lc (nick[i]);
	return s_nc[h % 12];
}
static unsigned nick_disc (const char *nick)		// (an avatar: never the accent)
{
	unsigned h = 5381;
	for (int i = 0; nick[i]; i++) h = h * 33 + (unsigned char) lc (nick[i]);
	return s_nc[h % 12];
}

// A round avatar: the nickname's colour, its first letter.
static void avatar (int x, int y, int s, const char *nick)
{
	unsigned c = nick_disc (nick);
	ui_vp_disc ((x * 2 + s) * 8, (y * 2 + s) * 8, s * 8, c);
	const char *p = nick; while (*p && !is_word_char (*p)) p++;
	char l[2] = { *p ? *p : '?', 0 };
	if (l[0] >= 'a' && l[0] <= 'z') l[0] = (char) (l[0] - 32);
	text_center (l, x, y, s, s, ink_on (c), s > 40 ? 22 : 13, f_bold ());
}

// ---- text cut to a width ----------------------------------------------------------------------------------------
static int prefix_w (const char *s, int n, int px, long fnt) { return (int) ui_text_prefix ((long) s, n, px, fnt); }

// How many bytes of s go on a row of maxw px: up to a space when one is near; *next: where the next row starts.
static int wrap_row (const char *s, int maxw, int px, long fnt, const char **next)
{
	int len = slen (s);
	if (maxw < 24) maxw = 24;
	if (prefix_w (s, len, px, fnt) <= maxw) { *next = s + len; return len; }
	long xs;
	int n = (int) uikit_text_hit (s, px, fnt, maxw, &xs);
	while (n > 0 && prefix_w (s, n, px, fnt) > maxw) { n--; while (n > 0 && ((unsigned char) s[n] & 0xC0) == 0x80) n--; }
	if (n <= 0) { n = 1; while (s[n] && ((unsigned char) s[n] & 0xC0) == 0x80) n++; }
	while (n < len && ((unsigned char) s[n] & 0xC0) == 0x80) n--;	// (never inside a character)
	int cut = n;
	while (cut > 0 && s[cut] != ' ') cut--;
	if (cut < n / 3) cut = n;				// one long word: cut it
	const char *p = s + cut; while (*p == ' ') p++;
	*next = p;
	return cut;
}
// Rows of s in maxw px (the first first px shorter); *widest: the widest row.
static int wrap_rows (const char *s, int maxw, int px, long fnt, int first = 0, int *widest = 0)
{
	int rows = 0, wmax = 0;
	if (!*s) { if (widest) *widest = 0; return 1; }
	while (*s)
	{
		const char *nx;
		int n = wrap_row (s, rows == 0 ? maxw - first : maxw, px, fnt, &nx);
		if (widest) { int w = prefix_w (s, n, px, fnt); if (w > wmax) wmax = w; }
		rows++; s = nx;
	}
	if (widest) *widest = wmax;
	return rows;
}
static void draw_n (const char *s, int n, int x, int y, int rowh, unsigned ink, int px, long fnt)
{
	char t[TEXT_MAX + 8];
	if (n > TEXT_MAX) n = TEXT_MAX;
	memcpy (t, s, (size_t) n); t[n] = 0;
	ui_text ((long) t, x, y + rowh / 2 + px * 37 / 100, ink, px, fnt, 4000);
}

// The same word list twice in the code: the conversations' name without its '#', UL_SIDEBAR takes "#x" for a heading.
static const char *bare_name (const char *n) { return n[0] == '#' ? n + 1 : n; }

} // namespace irc

#endif
