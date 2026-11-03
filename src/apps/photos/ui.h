//
// ui.h -- le dessin de Photos (d'apres user/Apps/photos/ui.h d'Onyx, sur UIKit) : les jetons du theme, les tailles de texte,
// le texte qui tient, les icones (leur geometrie est celle d'Onyx, tracee par ui_vp_*), les listes de zones, les dates.
// Tout se dessine dans la vue en cours (ui_view) : begin_paint donne ses pixels a ce qu'UIKit ne fait pas.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_ui_h
#define _photos_ui_h

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <spawn.h>
#include <uikit.h>
#include "lib.h"

extern char **environ;

namespace photos {

static struct ui_vars ui;
static inline const char *TR (const char *s) { return (const char *) ui_tr ((long) s); }
#define FILES_PROGRAM "SD0:/apps/files.app/main"
#define MAIL_PROGRAM "SD0:/apps/mail.app/main"
#define PAINT_PROGRAM "SD0:/apps/paint.app/main"

// ---- le theme : les jetons d'UIKit -----------------------------------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
#define C_BG		jet (J_FACE)
#define C_FIELD		jet (J_SURFACE)
#define C_TEXT		jet (J_TEXTE1)
#define C_DIM		jet (J_TEXTE2)
#define C_ACCENT	jet (J_ACCENT)
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c) & 0xFFFFFF; }
static unsigned col_dim () { return C_DIM; }
static unsigned col_faint () { return jet (J_TEXTE3); }
static unsigned col_line () { return jet (J_LIGNE); }
static unsigned col_side () { return jet (J_CREUX); }
// le tour de la photo reste sombre ; la barre et le volet du visionneur et de l'editeur suivent le theme
#define D_BG		0x1E2024u
#define D_BAR		C_BG
#define D_PANEL		jet (J_CREUX)
#define D_TEXT		C_TEXT
#define D_DIM		C_DIM
#define D_FAINT		col_faint ()
#define D_LINE		jet (J_LIGNE)
#define D_BTN		jet (J_ELEVE_BAS)
#define D_BTN_H		mix (jet (J_ELEVE_BAS), C_ACCENT, 50)
#define D_BTN_L		jet (J_ELEVE)
#define D_FIELD		C_FIELD
static const unsigned RED = 0xE0483E, AMBER = 0xF2B12E;

// ---- la vue en cours ---------------------------------------------------------------------------------------------
static uint32_t *g_px; static long g_pw, g_ph;
static inline void begin_paint (long view) { g_px = uikit_canvas_pixels (view, &g_pw, &g_ph); }
// une couleur par-dessus les pixels, a sur 255
static void veil (int x, int y, int w, int h, unsigned c, int a)
{
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > g_pw ? (int) g_pw : x + w, y1 = y + h > g_ph ? (int) g_ph : y + h;
	a = a * 256 / 255;
	unsigned rb = (c & 0xFF00FF) * (unsigned) a, gg = (c & 0xFF00) * (unsigned) a; int na = 256 - a;
	for (int j = y0; j < y1; j++)
	{
		uint32_t *p = g_px + (long) j * g_pw;
		for (int i = x0; i < x1; i++)
		{
			unsigned d = p[i];
			p[i] = ((((d & 0xFF00FF) * (unsigned) na + rb) >> 8) & 0xFF00FF) | (((d & 0xFF00) * (unsigned) na + gg) >> 8 & 0xFF00);
		}
	}
}
// (rangee par rangee : les coins arrondis d'un voile)
static void veil_round (int x, int y, int w, int h, int r, unsigned c, int a)
{
	if (r * 2 > h) r = h / 2; if (r * 2 > w) r = w / 2;
	for (int j = 0; j < h; j++)
	{
		int d = j < r ? r - j : j >= h - r ? j - (h - r) + 1 : 0, in = 0;
		if (d) in = r - (int) sqrtf ((float) (r * r - (d - 1) * (d - 1)));
		veil (x + in, y + j, w - 2 * in, 1, c, a);
	}
}
static inline void fill_round (int x, int y, int w, int h, int r, unsigned c, int alpha = 255)
{
	if (w <= 0 || h <= 0) return;
	if (alpha >= 255) ui_rbox (x, y, w, h, r, c, c); else veil_round (x, y, w, h, r, c, alpha);
}
static inline void rbox_a (int x, int y, int w, int h, int r, unsigned top, unsigned, int alpha = 255) { fill_round (x, y, w, h, r, top, alpha); }
static void rline (int x, int y, int w, int h, int, unsigned c, int alpha = 255)
{
	if (alpha >= 255) { ui_frame (x, y, w, h, c); return; }
	veil (x, y, w, 1, c, alpha); veil (x, y + h - 1, w, 1, c, alpha); veil (x, y + 1, 1, h - 2, c, alpha); veil (x + w - 1, y + 1, 1, h - 2, c, alpha);
}
#define V(a) ((a) * 16)
static void disc (int cx, int cy, int r, unsigned c, int alpha = 255)
{
	if (alpha >= 255) { ui_vp_disc (V (cx), V (cy), V (r), c); return; }
	for (int j = -r; j <= r; j++) { int k = (int) sqrtf ((float) (r * r - j * j)); veil (cx - k, cy + j, 2 * k + 1, 1, c, alpha); }
}
static void ring (int cx, int cy, int r, unsigned c, int w = 2)
{
	for (int i = 0; i < 20; i++)
	{
		float a = i * 3.14159265f / 10, b = (i + 1) * 3.14159265f / 10;
		ui_vp_line (V (cx) + (int) (V (r) * cosf (a)), V (cy) + (int) (V (r) * sinf (a)), V (cx) + (int) (V (r) * cosf (b)), V (cy) + (int) (V (r) * sinf (b)), V (w), c);
	}
}

// ---- les textes -------------------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_H2, F_H1, F_TINY, F_N };
static const int FACE_PX[F_N] = { 13, 11, 14, 16, 22, 9 };
static inline long face (int style) { return style ? (long) ui.font_b : (long) ui.font; }
static inline int tw (const char *s, int f = F_UI, int style = 0) { return (int) ui_text_width ((long) s, FACE_PX[f], face (style)); }
static inline int fh (int f = F_UI) { return FACE_PX[f] * 5 / 4; }
static inline void text (int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	int px = FACE_PX[f];
	if (w > 0) ui_text_fit ((long) s, x, y + px, c, px, face (style), w);
	else ui_text ((long) s, x, y + px, c, px, face (style), 0);
}
static inline void text_v (int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_r (int xr, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (xr - tw (s, f, style), y + (h - fh (f)) / 2, s, c, f, style); }
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }

// ---- les listes de zones ---------------------------------------------------------------------------------------------
struct Hit { int x, y, w, h, kind, a, b; };
struct HitList
{
	Hit *h; int n, cap;
	HitList () : h (0), n (0), cap (0) {}
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0, int b = 0)
	{
		if (n == cap) { int c = cap ? cap * 2 : 256; Hit *t = (Hit *) realloc (h, sizeof (Hit) * c); if (!t) return; h = t; cap = c; }
		h[n++] = Hit { x, y, w, hh, kind, a, b };
	}
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};
// un menu en un point de la fenetre (ui_popup_local)
struct PopMenu
{
	char b[1400]; int n, cnt; int id[32];
	PopMenu () : n (0), cnt (0) {}
	void add (const char *label, int ident, bool on = true)
	{
		if (cnt >= 32 || n + (int) strlen (label) + 3 >= (int) sizeof b) return;
		if (!on) b[n++] = 1;
		n += snprintf (b + n, sizeof b - n, "%s", label) + 1;
		id[cnt++] = ident;
	}
	void separator () { add ("-", -1); }
	int run (int x, int y, int w = 240)
	{
		b[n] = 0;
		long r = ui_popup_local (x, y, (long) b, w);
		return r >= 0 && r < cnt ? id[r] : -1;
	}
};

// ---- les icones -------------------------------------------------------------------------------------------------------
enum { I_PHOTOS, I_HEART, I_HEART_O, I_CLOCK, I_ALBUM, I_FOLDER, I_PLAY, I_PAUSE, I_SEARCH, I_INFO, I_ROTATE_L, I_ROTATE_R, I_EDIT, I_SHARE, I_TRASH,
       I_BACK, I_NEXT, I_CROP, I_SUN, I_MAGIC, I_PLUS, I_CHECK, I_CLOSE, I_FILTERS, I_MINUS, I_GRID, I_SMALLPIC, I_BIGPIC };
struct Pen
{
	int X, Y, u; unsigned c;
	int px (int a) const { return X + a * u; }
	int py (int b) const { return Y + b * u; }
	void poly (const int *xy, int n) const
	{
		int t[40]; if (n > 20) n = 20;
		for (int i = 0; i < n; i++) { t[2 * i] = px (xy[2 * i]); t[2 * i + 1] = py (xy[2 * i + 1]); }
		ui_vp_poly ((long) t, n, c);
	}
	void rect (int x, int y, int w, int h) const { int t[] = { x, y, x + w, y, x + w, y + h, x, y + h }; poly (t, 4); }
	void line (int a, int b, int d, int e, int th) const { ui_vp_line (px (a), py (b), px (d), py (e), th * u, c); }
	void polyline (const int *xy, int n, int th, bool closed = false) const
	{
		for (int i = 0; i + 1 < n; i++) line (xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], th);
		if (closed) line (xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], th);
	}
	void circle (int x, int y, int r) const { ui_vp_disc (px (x), py (y), r * u, c); }
	void arc (int x, int y, int r, int a0, int a1, int th) const
	{
		float d = 3.14159265f / 180;
		int n = (a1 - a0) / 24 + 1;
		for (int i = 0; i < n; i++)
		{
			float s = (a0 + (a1 - a0) * i / n) * d, e = (a0 + (a1 - a0) * (i + 1) / n) * d;
			ui_vp_line (px (x) + (int) (r * u * cosf (s)), py (y) + (int) (r * u * sinf (s)), px (x) + (int) (r * u * cosf (e)), py (y) + (int) (r * u * sinf (e)), th * u, c);
		}
	}
};
static void icon (int id, int x, int y, int s, unsigned c)
{
	Pen p = { V (x), V (y), V (s) / 24, c };
	Pen h = p; h.c = mix (c, 0xFFFFFF, 200);
	switch (id)
	{
	case I_PHOTOS: case I_SMALLPIC: case I_BIGPIC:
		p.rect (2, 4, 20, 16);
		{ int t[] = { 4, 18, 10, 10, 14, 15, 16, 13, 20, 18 }; h.poly (t, 5); h.circle (16, 8, 2); }
		break;
	case I_HEART: p.circle (7, 8, 5); p.circle (17, 8, 5); { int t[] = { 2, 10, 22, 10, 12, 21 }; p.poly (t, 3); } break;
	case I_HEART_O: { int t[] = { 12, 21, 3, 12, 2, 8, 4, 4, 8, 3, 12, 6, 16, 3, 20, 4, 22, 8, 21, 12 }; p.polyline (t, 10, 2, true); } break;
	case I_CLOCK: p.arc (12, 12, 9, 0, 360, 2); p.line (12, 7, 12, 12, 2); p.line (12, 12, 16, 14, 2); break;
	case I_ALBUM: { Pen q = p; q.c = mix (c, 0xFFFFFF, 90); q.rect (7, 3, 14, 14); p.rect (3, 7, 14, 14); } break;
	case I_FOLDER: p.rect (1, 6, 22, 15); p.rect (1, 3, 9, 5); break;
	case I_PLAY: { int t[] = { 6, 4, 20, 12, 6, 20 }; p.poly (t, 3); } break;
	case I_PAUSE: p.rect (6, 4, 4, 16); p.rect (14, 4, 4, 16); break;
	case I_SEARCH: p.arc (10, 10, 6, 0, 360, 2); p.line (15, 15, 21, 21, 3); break;
	case I_INFO: p.arc (12, 12, 9, 0, 360, 2); p.rect (11, 10, 2, 7); p.rect (11, 6, 2, 2); break;
	case I_ROTATE_L: p.arc (12, 13, 7, 200, 470, 2); { int t[] = { 2, 9, 8, 5, 9, 12 }; p.poly (t, 3); } break;
	case I_ROTATE_R: p.arc (12, 13, 7, 70, 340, 2); { int t[] = { 22, 9, 16, 5, 15, 12 }; p.poly (t, 3); } break;
	case I_EDIT: { int t[] = { 4, 20, 5, 15, 16, 4, 20, 8, 9, 19 }; p.poly (t, 5); } break;
	case I_SHARE:
		p.line (12, 3, 12, 15, 2);
		{ int t[] = { 7, 8, 12, 3, 17, 8 }; p.polyline (t, 3, 2); }
		{ int t[] = { 8, 11, 4, 11, 4, 21, 20, 21, 20, 11, 16, 11 }; p.polyline (t, 6, 2); }
		break;
	case I_TRASH: p.rect (3, 5, 18, 2); p.rect (9, 2, 6, 3); p.rect (5, 8, 14, 14); h.rect (9, 11, 2, 8); h.rect (13, 11, 2, 8); break;
	case I_BACK: { int t[] = { 15, 5, 8, 12, 15, 19 }; p.polyline (t, 3, 3); } break;
	case I_NEXT: { int t[] = { 9, 5, 16, 12, 9, 19 }; p.polyline (t, 3, 3); } break;
	case I_CROP: { int a[] = { 6, 2, 6, 18, 22, 18 }; p.polyline (a, 3, 2); int b[] = { 2, 6, 18, 6, 18, 22 }; p.polyline (b, 3, 2); } break;
	case I_SUN:
		p.circle (12, 12, 4);
		{ static const int D[8][4] = { { 12, 2, 12, 5 }, { 12, 19, 12, 22 }, { 2, 12, 5, 12 }, { 19, 12, 22, 12 }, { 5, 5, 7, 7 }, { 17, 17, 19, 19 }, { 5, 19, 7, 17 }, { 17, 7, 19, 5 } };
		  for (int k = 0; k < 8; k++) p.line (D[k][0], D[k][1], D[k][2], D[k][3], 2); }
		break;
	case I_MAGIC:
		p.line (3, 21, 15, 9, 3);
		{ int t[] = { 18, 2, 19, 5, 22, 6, 19, 7, 18, 10, 17, 7, 14, 6, 17, 5 }; p.poly (t, 8); }
		p.circle (21, 13, 1); p.circle (10, 4, 1);
		break;
	case I_PLUS: p.rect (5, 11, 14, 2); p.rect (11, 5, 2, 14); break;
	case I_MINUS: p.rect (5, 11, 14, 2); break;
	case I_CHECK: { int t[] = { 4, 12, 10, 18, 20, 6 }; p.polyline (t, 3, 3); } break;
	case I_CLOSE: p.line (6, 6, 18, 18, 2); p.line (18, 6, 6, 18, 2); break;
	case I_FILTERS: p.arc (9, 9, 6, 0, 360, 2); p.arc (15, 9, 6, 0, 360, 2); p.arc (12, 15, 6, 0, 360, 2); break;
	case I_GRID: for (int k = 0; k < 4; k++) p.rect (3 + (k & 1) * 10, 3 + (k >> 1) * 10, 8, 8); break;
	}
}
// la marque de la selection : un disque coche, ou un anneau vide
static void check_mark (int x, int y, bool on, unsigned ringCol)
{
	if (on) { disc (x + 11, y + 11, 11, C_ACCENT); ring (x + 11, y + 11, 11, 0xFFFFFF, 2); icon (I_CHECK, x + 4, y + 4, 14, 0xFFFFFF); }
	else { if (ringCol == 0xFFFFFF) disc (x + 11, y + 11, 10, 0, 40); ring (x + 11, y + 11, 10, ringCol, 2); }
}
static bool spawn_app (const char *path, const char *a1, const char *a2)
{
	char *argv[] = { (char *) path, (char *) a1, (char *) a2, 0 };
	pid_t p;
	return posix_spawn (&p, path, 0, 0, argv, environ) == 0;
}
static bool spawn_app (const char *path, const char *arg) { return spawn_app (path, arg, 0); }

static void msg (const char *m) { ui_message ((long) TR ("Photos"), (long) m, UM_OK); }
static bool ask_yes (const char *q) { return ui_message ((long) TR ("Photos"), (long) q, UM_YESNO) == 1; }

// ---- les dates ------------------------------------------------------------------------------------------------------------
static long long day_of (long long t) { return t >= 0 ? t / 86400 : (t - 86399) / 86400; }
// « samedi 27 septembre 2026 »
static void fmt_day (long long t, char *b, int cap)
{
	const char *MONTHS[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
	const char *WDAYS[7] = { "Thursday", "Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday" };
	long long d = day_of (t); int y, m, dd; civil (d, &y, &m, &dd);
	long long today = day_of (now_local ());
	if (today && d == today) snprintf (b, cap, "%s", TR ("Today"));
	else if (today && d == today - 1) snprintf (b, cap, "%s", TR ("Yesterday"));
	else snprintf (b, cap, "%s %d %s %d", TR (WDAYS[((d % 7) + 7) % 7]), dd, TR (MONTHS[m - 1]), y);
}
static void fmt_time (long long t, char *b, int cap) { long long s = t - day_of (t) * 86400; snprintf (b, cap, "%02d:%02d", (int) (s / 3600), (int) (s / 60 % 60)); }
static int year_of (long long t) { int y, m, d; civil (day_of (t), &y, &m, &d); return y; }
static void fmt_size (unsigned long long n, char *b, int cap)
{
	if (n < 1024) snprintf (b, cap, TR ("%llu bytes"), n);
	else if (n < 1024 * 1024) snprintf (b, cap, TR ("%llu KB"), (n + 512) / 1024);
	else snprintf (b, cap, TR ("%llu.%llu MB"), n / 1048576, n % 1048576 * 10 / 1048576);
}

} // namespace photos

#endif
