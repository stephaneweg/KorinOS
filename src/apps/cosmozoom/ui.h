//
// ui.h -- the game's flat drawing: the text (UIKit's fonts, outlined), the panels and buttons of the
// menus, the gadgets' icons, the pilots' helmets, and the race's HUD (the place, the racers, the gadget's slot,
// the laps and times, the map, the speed, the messages). Drawn over the 3D picture; laid out on a 1280 x 720
// page scaled to the screen (UiPage).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_UI_H
#define CZ_UI_H

#include "sys.h"
#include "draw2d.h"
#include "race.h"

namespace cz {

// the colours of the interface (DESIGN.md: ink, white, the candy accents)
enum : unsigned
{
	C_INK = 0x1E1838, C_WHITE = 0xFFFFFF, C_PINK = 0xFF3D9A, C_YELLOW = 0xFFD23F, C_MINT = 0x3DDC97, C_SKY = 0x4CC9F0,
	C_ORANGE = 0xFF8C2B, C_GRAPE = 0x8B5CF6, C_RED = 0xF2384B, C_PANEL = 0xF4F6FF, C_DIM = 0x8A8FB0, C_TEAL = 0x22C3B5
};

// ---- the theme: UIKit's tokens, never theme.txt. The panels, what is written on them, the dim behind the menus follow it;
// the game's own world (the circuits, the ships, the hangar's sky, the accents of its choices) stays as it is drawn
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3, J_ACCENT };
static const unsigned *s_jet; static bool s_light;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static void theme_load ()
{
	s_jet = (const unsigned *) ui_jetons ();
	unsigned c = jet (J_ELEVE);
	s_light = ((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10 > 128;
}
#define C_TXT jet (J_TEXTE1)
#define C_DIMTXT jet (J_TEXTE2)

// ---- text ---------------------------------------------------------------------------------------------------------
struct Text
{
	long fontB = 0, fontBI = 0, fontR = 0;
	bool italic = false;			// (the HUD's numbers: bold italic)
	long face (bool bold)
	{
		if (!fontB) { fontB = ui_font_get (UFN_BOLD); fontBI = ui_font_get (UFN_BOLDITALIC); fontR = ui_font_get (UFN_SANS); if (!fontBI) fontBI = fontB; }
		return !bold ? fontR : italic ? fontBI : fontB;
	}
	static int size (int px) { return px < 8 ? 8 : px > 160 ? 160 : px; }
	int width (const char *s, int px, bool bold = true) { return (int) ui_text_width ((long) s, size (px), face (bold)); }
	// align: 0 left, 1 centre, 2 right (x is that point); outline: its thickness (0 none). On a panel (no outline) an
	// accent is taken nearer the theme's ink: a light panel would swallow it
	void draw (Surf &S, int x, int y, int px, const char *s, unsigned col, int align = 0, int outline = 0, bool bold = true)
	{
		if (!s || !*s) return;
		px = size (px);
		long f = face (bold);
		int w = (int) ui_text_width ((long) s, px, f);
		int x0 = align == 1 ? x - w / 2 : align == 2 ? x - w : x, base = y + px;
		bool clip = S.cx0 > 0 || S.cy0 > 0 || S.cx1 < S.w || S.cy1 < S.h;
		if (clip) ui_clip (S.cx0, S.cy0, S.cx1 - S.cx0, S.cy1 - S.cy0);
		if (outline > 0)
		{
			if (col == C_TXT) col = C_WHITE;	// (outlined: over the world, not on a panel)
			unsigned ink = (unsigned) ui_ink (col) & 0xFFFFFF;
			static const int D[4][2] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
			for (int k = 0; k < 4; k++) ui_text ((long) s, x0 + D[k][0] * outline, base + D[k][1] * outline, ink, px, f, 0);
		}
		else if (s_light && col != C_TXT) col = (unsigned) ui_mix (col, C_TXT, 130) & 0xFFFFFF;
		ui_text ((long) s, x0, base, col, px, f, 0);
		if (clip) ui_clip (0, 0, 0, 0);
	}
};

// ---- the page: a 1280 x 720 layout scaled to the screen (or a view) ------------------------------------------------
struct UiPage
{
	float k = 1; int ox = 0, oy = 0, w = 1280, h = 720;
	void fit (int W, int H, int x0 = 0, int y0 = 0)
	{
		k = fminf ((float) W / 1280.0f, (float) H / 720.0f);
		w = (int) (1280 * k); h = (int) (720 * k);
		ox = x0 + (W - w) / 2; oy = y0 + (H - h) / 2;
	}
	int X (float x) const { return ox + (int) (x * k); }
	int Y (float y) const { return oy + (int) (y * k); }
	int L (float v) const { int r = (int) (v * k + 0.5f); return r < 1 ? 1 : r; }
};

// ---- the HUD's panels: dark, see-through, a neon frame ------------------------------------------------------------------
static void neonPanel (Surf &S, int x, int y, int w, int h, int r, unsigned accent = C_SKY, int alpha = 170)
{
	// the soft look: a rounded see-through panel of the theme, a soft drop shadow, its rim (the accent when chosen)
	int rr = r + h / 6;
	bool plain = accent == C_SKY || accent == 0x3A4A8A;
	S.rrect (x + 2, y + 4, w, h, rr, 0x101830, alpha / 3);
	S.rrect (x - 2, y - 2, w + 4, h + 4, rr + 2, plain ? jet (J_LIGNE_FORTE) : accent, plain ? 170 : 255);
	S.rrect (x, y, w, h, rr, jet (J_ELEVE), alpha + 40 > 255 ? 255 : alpha + 40);
	if (h < 80 && !s_light) S.rrect (x + rr / 2, y + 2, w - rr, h / 3 > 3 ? h / 3 : 3, rr / 2, 0xFFFFFF, 24);
}

// ---- a pilot's helmet (the racers' list, the menus) ------------------------------------------------------------------
static void helmet (Surf &S, float cx, float cy, float r, unsigned col, unsigned trim, int style = 0)
{
	S.disc (cx, cy + r * 0.05f, r + 2.5f, C_INK);
	S.disc (cx, cy, r, col);
	// a stripe over the top
	if (style % 3 == 1) { S.rect ((int) (cx - r * 0.18f), (int) (cy - r), (int) (r * 0.36f), (int) (r * 0.9f), trim); }
	else if (style % 3 == 2) { float p[8] = { cx - r * 0.9f, cy - r * 0.15f, cx + r * 0.9f, cy - r * 0.15f, cx + r * 0.8f, cy - r * 0.38f, cx - r * 0.8f, cy - r * 0.38f }; S.poly (p, 4, trim); }
	// the visor: dark, a glint
	float v[12] = { cx - r * 0.78f, cy - r * 0.02f, cx + r * 0.78f, cy - r * 0.02f, cx + r * 0.66f, cy + r * 0.48f, cx + r * 0.2f, cy + r * 0.62f, cx - r * 0.2f, cy + r * 0.62f, cx - r * 0.66f, cy + r * 0.48f };
	S.poly (v, 6, 0x232045);
	S.line (cx - r * 0.5f, cy + r * 0.12f, cx - r * 0.15f, cy + r * 0.12f, r * 0.12f, 0x9FE8FF, 200);
	S.disc (cx - r * 0.35f, cy - r * 0.5f, r * 0.15f, 0xFFFFFF, 120);
}

// ---- the gadgets' icons ---------------------------------------------------------------------------------------------
static void chevron (Surf &S, float x, float y, float r, unsigned col)
{
	float p[12] = { x - r, y - r * 0.6f, x, y + r * 0.2f, x + r, y - r * 0.6f, x + r, y - r * 0.05f, x, y + r * 0.75f, x - r, y - r * 0.05f };
	S.poly (p, 6, col);
}
static void chevronH (Surf &S, float x, float y, float r, unsigned col, bool left)
{
	float d = left ? -1.0f : 1.0f;
	float p[12] = { x - d * r * 0.6f, y - r, x - d * r * 0.1f, y - r, x + d * r * 0.6f, y, x - d * r * 0.1f, y + r, x - d * r * 0.6f, y + r, x + d * r * 0.0f, y };
	S.poly (p, 6, col);
}
static void itemIcon (Surf &S, int it, float cx, float cy, float r, float t = 0)
{
	if (it <= IT_NONE || it >= IT_COUNT) return;
	unsigned c = ITEMS[it].color;
	switch (ITEMS[it].effect)
	{
	case FX_BOOST:
		if (it == IT_TRIPLE) for (int k = 0; k < 3; k++) chevron (S, cx, cy - r * 0.55f + (float) k * r * 0.42f, r * 0.55f, k == 1 ? C_YELLOW : c);
		else { chevron (S, cx, cy - r * 0.3f, r * 0.7f, C_YELLOW); chevron (S, cx, cy + r * 0.2f, r * 0.7f, c); }
		break;
	case FX_SHIELD:
		S.ring (cx, cy, r * 0.85f, r * 0.22f, c); S.disc (cx, cy, r * 0.5f, c, 90); S.disc (cx - r * 0.3f, cy - r * 0.35f, r * 0.15f, C_WHITE, 200);
		break;
	case FX_SHOT:
		S.disc (cx, cy, r * 0.8f, c, 170); S.ring (cx, cy, r * 0.8f, r * 0.1f, C_WHITE); S.disc (cx - r * 0.3f, cy - r * 0.3f, r * 0.18f, C_WHITE, 230);
		break;
	case FX_DROP:
	{
		float p[16];
		for (int k = 0; k < 8; k++) { float a = (float) k * PI / 4; float rr = r * (0.62f + 0.18f * sinf ((float) k * 2.3f)); p[k * 2] = cx + cosf (a) * rr; p[k * 2 + 1] = cy + sinf (a) * rr * 0.75f + r * 0.1f; }
		S.poly (p, 8, c); S.disc (cx + r * 0.25f, cy - r * 0.05f, r * 0.14f, C_WHITE, 200); S.disc (cx - r * 0.25f, cy + r * 0.15f, r * 0.09f, C_WHITE, 160);
		break;
	}
	case FX_WELL:
		for (int k = 0; k < 3; k++) S.ring (cx, cy, r * (0.85f - 0.25f * (float) k), r * 0.1f, k & 1 ? C_WHITE : c);
		S.disc (cx, cy, r * 0.15f, C_INK);
		break;
	case FX_BEAM:
		S.line (cx - r * 0.8f, cy + r * 0.6f, cx + r * 0.8f, cy - r * 0.6f, r * 0.35f, c, 200);
		S.line (cx - r * 0.8f, cy + r * 0.6f, cx + r * 0.8f, cy - r * 0.6f, r * 0.12f, C_WHITE);
		S.disc (cx + r * 0.75f, cy - r * 0.55f, r * 0.2f, C_WHITE);
		break;
	case FX_EMP:
		S.star (cx, cy, r * 0.9f, c, 255, t, 0.35f); S.star (cx, cy, r * 0.45f, C_WHITE, 255, -t, 0.4f);
		break;
	case FX_NEBULA:
		S.disc (cx - r * 0.35f, cy, r * 0.42f, c); S.disc (cx + r * 0.35f, cy - r * 0.05f, r * 0.45f, c); S.disc (cx, cy - r * 0.3f, r * 0.45f, c);
		{ float p[8] = { cx + r * 0.05f, cy + r * 0.15f, cx - r * 0.2f, cy + r * 0.6f, cx + r * 0.05f, cy + r * 0.55f, cx - r * 0.1f, cy + r * 0.95f }; S.poly (p, 4, C_YELLOW); }
		break;
	case FX_WARP:
		S.star (cx, cy, r * 0.95f, C_INK, 255, t * 0.3f); S.star (cx, cy, r * 0.82f, c, 255, t * 0.3f); S.star (cx, cy, r * 0.4f, C_WHITE, 220, t * 0.3f);
		break;
	}
}

// ---- ordinals ---------------------------------------------------------------------------------------------------------
static const char *ordinal (int place, char *buf, int cap)
{
	// English: 1st 2nd 3rd 4th; French: 1er 2e 3e
	bool fr = !strcmp ((const char *) ui_lang (), "fr");
	if (fr) snprintf (buf, (size_t) cap, place == 1 ? "%der" : "%de", place);
	else snprintf (buf, (size_t) cap, "%d%s", place, place == 1 ? "st" : place == 2 ? "nd" : place == 3 ? "rd" : "th");
	return buf;
}
static void fmtTime (float t, char *b, int cap)
{
	if (t < 0) t = 0;
	int ms = (int) (t * 100 + 0.5f);
	snprintf (b, (size_t) cap, "%d:%02d.%02d", ms / 6000, (ms / 100) % 60, ms % 100);
}

// ---- the map: the track's centre line seen from above ---------------------------------------------------------------
struct MiniMap
{
	enum { N = 160 };
	float px[N], pz[N], rx[N], rz[N], bl[2][N], bh[2][N]; bool fk[N]; int n = 0;	// (a fork: its branches across, their heights)
	float lox, loz, sc;
	void make (const Track &T)
	{
		n = 0;
		float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
		for (int k = 0; k < N; k++)
		{
			const Seg &S = T.seg[k * T.n / N];
			const V3 &p = S.c;
			px[k] = p.x; pz[k] = p.z; n++;
			V3 r = norm (V3 (S.r.x, 0, S.r.z)); rx[k] = r.x; rz[k] = r.z;
			fk[k] = S.fork > 0; for (int b = 0; b < 2; b++) { bl[b][k] = S.bl[b]; bh[b][k] = S.bh[b]; }
			x0 = fminf (x0, p.x); x1 = fmaxf (x1, p.x); z0 = fminf (z0, p.z); z1 = fmaxf (z1, p.z);
		}
		lox = (x0 + x1) * 0.5f; loz = (z0 + z1) * 0.5f; sc = fmaxf (x1 - x0, z1 - z0) * 0.5f + 1;
	}
	// a world point -> the map's square (cx, cy, half size r); x mirrored so that the map is as seen from above
	void at (float wx, float wz, float cx, float cy, float r, float *ox, float *oy) const
	{
		*ox = cx - (wx - lox) / sc * r; *oy = cy - (wz - loz) / sc * r;
	}
	void draw (Surf &S, const Race &R, float cx, float cy, float r, int me)
	{
		// (a fork: its two branches, thinner; the lower one first, the upper one over it where they cross)
		for (int layer = 0; layer < 2; layer++)
			for (int pass = 0; pass < 2; pass++)
				for (int k = 0; k < n; k++)
				{
					int j = (k + 1) % n;
					bool f = fk[k] || fk[j];
					for (int b = 0; b < (f ? 2 : 1); b++)
					{
						if (f && (bh[b][k] > bh[1 - b][k] + 1.0f) != (layer == 1)) continue;
						if (!f && layer) continue;
						float ax, ay, bx, by, lk = f ? bl[b][k] : 0, lj = f ? bl[b][j] : 0;
						at (px[k] + rx[k] * lk, pz[k] + rz[k] * lk, cx, cy, r, &ax, &ay); at (px[j] + rx[j] * lj, pz[j] + rz[j] * lj, cx, cy, r, &bx, &by);
						float wd = f ? (pass ? r * 0.035f : r * 0.08f) : pass ? r * 0.05f : r * 0.11f;
						S.line (ax, ay, bx, by, wd, pass ? C_WHITE : C_INK);
					}
				}
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < R.n; i++)
			{
				bool big = i == me || R.c[i].human;
				if ((pass == 1) != big) continue;
				V3 p = R.T->pointAt (R.c[i].s, R.T->forkAt (R.c[i].s) > 0 ? R.c[i].l : 0, 0);	// (a fork: on its branch)
				float x, y; at (p.x, p.z, cx, cy, r, &x, &y);
				float d = big ? r * 0.1f : r * 0.065f;
				S.disc (x, y, d + 2, C_INK);
				S.disc (x, y, d, PILOTS[R.c[i].pilot].color);
			}
	}
};

} // namespace cz

#endif
