//
// panes.h -- what is round the slide: the slides' thumbnails at the left (grouped in sections, folded with their
// arrow; a click goes to a slide, a drag moves it, a right click its menu), the slide sorter (the whole deck by section:
// each slide's transition and timing, several selected, dragged), the speaker's notes under the slide. The thumbnails
// are drawn by the same layers as the slide (render.h) into small pictures kept until their slide changes -- made
// in the main loop, before the views are painted (text needs UIKit's target, see text.h).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_panes_h
#define _slides_panes_h

#include "view.h"

namespace sl {

// ---- the thumbnails' cache ------------------------------------------------------------------------------------------
static unsigned slide_hash (const Deck &d, const Slide &s, int index)
{
	Hash h;
	h.addi (index); h.addi (s.layout); h.addi (s.masterObjects); h.add (&s.bg, sizeof s.bg); h.addi (s.obj.n);
	h.add (&d.masterBg, sizeof d.masterBg); h.add (d.footerText, (int) strlen (d.footerText)); h.addi (d.footer); h.addi (d.number); h.addi (d.sw); h.addi (d.sh);
	for (int i = 0; i < s.obj.n; i++) { const Object &o = *s.obj[i]; h.addi ((int) obj_hash (d, o, 0, false)); h.addi (o.x); h.addi (o.y); h.addi (o.rot); h.addi (o.flipH); h.addi (o.flipV); }
	for (int i = 0; i < d.decor.n; i++) { h.addi ((int) obj_hash (d, *d.decor[i], 0, false)); h.addi (d.decor[i]->x); h.addi (d.decor[i]->y); }
	return h.h;
}
struct Thumb { Slide *s; unsigned hash; int w, h; unsigned *px; };
struct ThumbCache
{
	Compositor Ct;
	Vec<Thumb> t;
	// (the main loop) the picture of a slide, made or brought up to date
	unsigned *get (int index, int w, int h)
	{
		Slide *s = g_deck.slides[index];
		unsigned hs = slide_hash (g_deck, *s, index);
		for (int i = 0; i < t.n; i++)
			if (t[i].s == s && t[i].w == w && t[i].h == h)
			{
				if (t[i].hash != hs) render (t[i], index, hs);
				return t[i].px;
			}
		Thumb n; n.s = s; n.w = w; n.h = h; n.px = (unsigned *) malloc ((size_t) w * h * 4); n.hash = 0;
		if (!n.px) return 0;
		render (n, index, hs);
		t.push (n);
		return n.px;
	}
	// (a view's painting) only what is ready
	unsigned *peek (int index, int w, int h)
	{
		Slide *s = g_deck.slides[index];
		for (int i = 0; i < t.n; i++) if (t[i].s == s && t[i].w == w && t[i].h == h) return t[i].px;
		return 0;
	}
	void render (Thumb &th, int index, unsigned hs)
	{
		Ct.frame++;
		flatten_slide (Ct, g_deck, *g_deck.slides[index], index, th.px, th.w, th.h, th.w);
		Ct.sweep (0);
		th.hash = hs;
		// the master view's layouts: their placeholders outlined (empty, they draw nothing)
		if (g_master && index > 0)
		{
			const Slide &s = *g_deck.slides[index];
			float k = (float) th.w / g_deck.sw;
			for (int i = 0; i < s.obj.n; i++)
			{
				const Object &o = *s.obj[i];
				if (o.ph == PH_NONE || !o.tb.empty ()) continue;
				int x0 = imax (0, (int) (o.x * k)), y0 = imax (0, (int) (o.y * k)), x1 = imin (th.w - 1, (int) ((o.x + o.w) * k)), y1 = imin (th.h - 1, (int) ((o.y + o.h) * k));
				for (int y = y0; y <= y1; y++)
					for (int x = x0; x <= x1; x++)
					{
						unsigned &p = th.px[y * th.w + x];
						bool edge = x == x0 || x == x1 || y == y0 || y == y1;
						if (edge) { if (((x + y) & 3) < 2) p = 0x8A949C; }
						else if (o.ph == PH_TITLE && y < y0 + (y1 - y0) / 2 && y > y0 + (y1 - y0) / 4 && x < x0 + (x1 - x0) * 2 / 3) p = 0xC8CED3;
						else if (o.ph != PH_TITLE && o.ph != PH_PICTURE && (y - y0) % 6 == 3 && x < x1 - (x1 - x0) / 4) p = 0xD5DADE;
						else if (o.ph == PH_PICTURE) p = 0xE6EAEE;
					}
			}
		}
	}
	void prune ()		// the slides gone
	{
		for (int i = 0; i < t.n; )
		{
			bool alive = g_deck.slides.find (t[i].s) >= 0;
			if (!alive) { free (t[i].px); t.erase (i); } else i++;
		}
	}
	void clear () { for (int i = 0; i < t.n; i++) free (t[i].px); t.clear (); }
};
static ThumbCache g_thumbs;

// The slide's picture into the view being painted, at (x, y), w x h.
static void thumb_blit (unsigned *cv, int cw, int chh, int index, int x, int y, int w, int h)
{
	unsigned *px = g_thumbs.peek (index, w, h);
	if (!px) { ui_fill (x, y, w, h, 0xFFFFFF); return; }
	for (int j = 0; j < h; j++)
	{
		int yy = y + j; if (yy < 0 || yy >= chh) continue;
		for (int i = 0; i < w; i++) { int xx = x + i; if (xx >= 0 && xx < cw) cv[(long) yy * cw + xx] = px[j * w + i] & 0xFFFFFF; }
	}
}
static void veil (unsigned *cv, int cw, int chh, int x, int y, int w, int h, unsigned c, int k)	// (a hidden slide)
{
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { int xx = x + i, yy = y + j; if (xx >= 0 && yy >= 0 && xx < cw && yy < chh) { unsigned &p = cv[(long) yy * cw + xx]; p = mix (p, c, k); } }
}

// ---- the slides' column -------------------------------------------------------------------------------------------
static char g_folded[16][48]; static int g_nfolded;
static bool section_folded (const char *name) { for (int i = 0; i < g_nfolded; i++) if (!strcmp (g_folded[i], name)) return true; return false; }
static void section_fold (const char *name)
{
	for (int i = 0; i < g_nfolded; i++) if (!strcmp (g_folded[i], name)) { memmove (g_folded[i], g_folded[i + 1], sizeof g_folded[0] * (g_nfolded - i - 1)); g_nfolded--; return; }
	if (g_nfolded < 16) scpy (g_folded[g_nfolded++], name, 48);
}
// The section a slide belongs to (the last one started at or before it).
static const char *section_of (int i)
{
	for (int k = i; k >= 0; k--) if (g_deck.slides[k]->section[0]) return g_deck.slides[k]->section;
	return "";
}

enum { VIEW_NORMAL, VIEW_SORTER };
static int g_viewMode = VIEW_NORMAL;
static long sl_v, sl_bar;			// the view, its scroll bar
static int sl_x, sl_y, sl_w, sl_h, sl_top, sl_total, sl_dragFrom = -1, sl_dropAt = -1, sl_y0, sl_hot = -1;
static bool sl_down, sl_rdown, sl_focus;
static bool g_wantSlideMenu; static int g_smX, g_smY;	// its right click (window coordinates): the main loop opens the menu
enum { SL_HEAD = 22, SL_GAP = 12, SB_W_ = 12 };
struct SlRow { int slide; int y, h; bool head; };
static int sl_tw () { return sl_w - 46; }
static int sl_th () { return sl_tw () * g_deck.sh / g_deck.sw; }
static void sl_rows (Vec<SlRow> &r)
{
	r.clear ();
	int y = 8;
	bool folded = false;
	for (int i = 0; i < g_deck.slides.n; i++)
	{
		Slide *s = g_deck.slides[i];
		if (s->section[0]) { SlRow h; h.slide = i; h.y = y; h.h = SL_HEAD; h.head = true; r.push (h); y += SL_HEAD; folded = section_folded (s->section); }
		if (folded && i != g_cur) continue;
		SlRow q; q.slide = i; q.y = y; q.h = sl_th () + SL_GAP; q.head = false; r.push (q); y += q.h;
	}
	sl_total = y + 8;
}
// (the main loop) the pictures of what shows, the range of the bar
static void sl_prepare ()
{
	g_listDirty = false;
	if (sl_w <= 0 || g_viewMode != VIEW_NORMAL) return;
	Vec<SlRow> r; sl_rows (r);
	int maxTop = imax (0, sl_total - sl_h); if (sl_top > maxTop) sl_top = maxTop;
	int W = sl_tw (), H = sl_th ();
	if (W > 0 && H > 0)
		for (int k = 0; k < r.n; k++)
		{
			int y = r[k].y - sl_top;
			if (r[k].head || y + r[k].h < 0 || y > sl_h) continue;
			g_thumbs.get (r[k].slide, W, H);
		}
	ui_w_set_range (sl_bar, 0, sl_total, sl_h, 40);
	ui_w_set_value (sl_bar, sl_top);
	ui_w_show (sl_bar, sl_total > sl_h);
	ui_view_dirty (sl_v);
}
static void sl_paint (long v, long w, long h)
{
	long cw, chh; unsigned *cv = (unsigned *) uikit_canvas_pixels (v, &cw, &chh);
	unsigned P = jet (J_FACE), acc = jet (J_ACCENT), dim = jet (J_TEXTE3);
	ui_fill (0, 0, w, h, P);
	Vec<SlRow> r; sl_rows (r);
	int W = sl_tw (), H = sl_th ();
	for (int k = 0; k < r.n; k++)
	{
		SlRow &q = r[k]; int y = q.y - sl_top;
		if (y + q.h < 0 || y > h) continue;
		Slide *s = g_deck.slides[q.slide];
		if (q.head)
		{
			bool f = section_folded (s->section);
			text_box (f ? "\xE2\x96\xB8" : "\xE2\x96\xBE", 8, y, q.h, dim);
			text_box (s->section, 22, y, q.h, jet (J_TEXTE2), font_bold (), (int) w - 28);
			continue;
		}
		int x = 30;
		bool cur = q.slide == g_cur;
		if (cur) ui_rbox (x - 5, y - 4, W + 10, H + 8, 5, acc, acc);
		else if (q.slide == sl_hot) ui_rbox (x - 5, y - 4, W + 10, H + 8, 5, jet (J_ACCENT_CLAIR), jet (J_ACCENT_CLAIR));
		thumb_blit (cv, (int) cw, (int) chh, q.slide, x, y, W, H);
		ui_frame (x - 1, y - 1, W + 2, H + 2, jet (J_LIGNE_FORTE));
		if (s->hidden) veil (cv, (int) cw, (int) chh, x, y, W, H, P, 150);
		char n[8]; snprintf (n, sizeof n, "%d", q.slide + 1);
		int nw = text_w (n);
		text_box (n, 24 - nw, y, 18, cur ? acc : dim, cur ? font_bold () : 0);
		if (s->hidden) ui_fill (24 - nw - 2, y + 9, nw + 4, 1, dim);
		if (s->anim.n) text_box ("\xE2\x80\xA2", 14, y + 22, 16, dim);
	}
	// the drop place
	if (sl_dragFrom >= 0 && sl_dropAt >= 0)
	{
		int y = -1;
		for (int k = 0; k < r.n; k++) if (!r[k].head && r[k].slide == sl_dropAt) y = r[k].y - sl_top - 6;
		if (sl_dropAt >= g_deck.slides.n && r.n) y = r[r.n - 1].y + r[r.n - 1].h - sl_top - 6;
		if (y >= 0) ui_rbox (20, y - 1, W + 16, 4, 2, acc, acc);
	}
	ui_fill (w - 1, 0, 1, h, jet (J_LIGNE));
}
static int sl_row_at (int my, bool *head)
{
	Vec<SlRow> r; sl_rows (r);
	for (int k = 0; k < r.n; k++) if (my + sl_top >= r[k].y && my + sl_top < r[k].y + r[k].h) { *head = r[k].head; return r[k].slide; }
	*head = false; return -1;
}
static void sl_show_cur ()
{
	Vec<SlRow> r; sl_rows (r);
	for (int k = 0; k < r.n; k++) if (!r[k].head && r[k].slide == g_cur)
	{
		if (r[k].y < sl_top) sl_top = r[k].y - 8;
		else if (r[k].y + r[k].h > sl_top + sl_h) sl_top = r[k].y + r[k].h - sl_h + 8;
	}
	if (sl_top < 0) sl_top = 0;
	g_listDirty = true;
}
static long sl_mouse (long, long kind, long mx, long my, long info)
{
	int buttons = (int) (info >> 8 & 255);
	bool bl = buttons & 1, br = buttons & 2;
	bool in = mx >= 0 && my >= 0 && mx < sl_w && my < sl_h && kind != 3;
	bool head = false; int i = in ? sl_row_at ((int) my, &head) : -1;
	int hot = in && !head ? i : -1;
	if (hot != sl_hot) { sl_hot = hot; ui_view_dirty (sl_v); }
	if (kind == 1 && br && !sl_rdown)
	{
		sl_rdown = true; ui_w_focus (sl_v);
		if (i >= 0) { go_slide (i); after (); }
		g_wantSlideMenu = true; g_smX = sl_x + (int) mx; g_smY = sl_y + (int) my;
		return 0;
	}
	if (!br) sl_rdown = false;
	if (kind == 1 && bl && !sl_down && in)
	{
		sl_down = true; ui_w_focus (sl_v);
		if (i >= 0 && head) { section_fold (g_deck.slides[i]->section); g_listDirty = true; sl_dragFrom = -1; return 0; }
		if (i >= 0) { go_slide (i); sl_dragFrom = i; sl_y0 = (int) my; after (); }
		return 0;
	}
	if (kind == 0 && sl_down && sl_dragFrom >= 0)
	{
		if (abs ((int) my - sl_y0) > 6)
		{
			// the drop: before the slide under the pointer's middle
			Vec<SlRow> r; sl_rows (r);
			int at = g_deck.slides.n;
			for (int k = 0; k < r.n; k++) if (!r[k].head && my + sl_top < r[k].y + r[k].h / 2) { at = r[k].slide; break; }
			if (at != sl_dropAt) { sl_dropAt = at; ui_view_dirty (sl_v); }
			if (my < 10) { sl_top = imax (0, sl_top - 12); g_listDirty = true; }
			if (my > sl_h - 10) { sl_top = imin (imax (0, sl_total - sl_h), sl_top + 12); g_listDirty = true; }
		}
		return 0;
	}
	if (kind == 2 && sl_down)
	{
		sl_down = false;
		if (sl_dragFrom >= 0 && sl_dropAt >= 0)
		{
			int to = sl_dropAt > sl_dragFrom ? sl_dropAt - 1 : sl_dropAt;
			if (to != sl_dragFrom) { move_slide (sl_dragFrom, iclamp (to, 0, g_deck.slides.n - 1)); after (); }
		}
		sl_dragFrom = sl_dropAt = -1;
		ui_view_dirty (sl_v);
	}
	return 0;
}
static long sl_wheel (long, long wheel) { sl_top = imax (0, imin (sl_total - sl_h, sl_top - (int) wheel * 60)); g_listDirty = true; return 0; }
static long sl_key (long, long hid, long ch, long mods)
{
	if ((mods & UI_MOD_CTRL) || hid >= 0xE0) return -1;
	long k = key_code ((int) hid, (int) ch);
	if (k == K_UP || k == K_LEFT || k == K_PGUP) go_slide (g_cur - 1);
	else if (k == K_DOWN || k == K_RIGHT || k == K_PGDN) go_slide (g_cur + 1);
	else if (k == K_HOME) go_slide (0);
	else if (k == K_END) go_slide (g_deck.slides.n - 1);
	else if (k == K_DEL || k == K_BACK) cmd_delete_slide ();
	else if (k == K_ENTER) cmd_new_slide ();
	else return -1;
	sl_show_cur (); after ();
	return 0;
}
static void sl_create (int x, int y, int w, int h)
{
	sl_x = x; sl_y = y; sl_w = w; sl_h = h;
	sl_v = ui_view (x, y, w, h, UVW_FOCUS);
	ui_view_on (sl_v, (long) sl_paint, (long) sl_mouse, (long) sl_key, (long) sl_wheel);
	sl_bar = ui_scrollbar (x + w - SB_W_, y, SB_W_, h, 0);
	ui_w_set_parent (sl_bar, sl_v);
}
static void sl_move (int x, int y, int w, int h)
{
	sl_x = x; sl_y = y; sl_w = w; sl_h = h;
	ui_w_move (sl_v, x, y, w, h); ui_w_move (sl_bar, x + w - SB_W_, y, SB_W_, h);
	g_listDirty = true;
}

// ---- the slide sorter --------------------------------------------------------------------------------------------
static const char *const TR_NAMES[TR_COUNT] = { "None", "Fade", "Push", "Wipe", "Cover", "Uncover", "Split", "Zoom", "Dissolve" };
static Vec<int> g_sorterSel;
static void (*g_onOpenSlide) (int i);		// a double click in the sorter: back to the normal view on it
static long so, so_bar;
static int so_x, so_y, so_w, so_h, so_top, so_total, so_drag = -1, so_drop = -1, so_x0, so_y0;
static bool so_down, so_rdown; static unsigned so_lastT;
static int so_cols () { return so_w >= 900 ? 5 : so_w >= 640 ? 4 : 3; }
static int so_cw () { return (so_w - 60 - (so_cols () - 1) * 22) / so_cols (); }
static int so_ch () { return so_cw () * g_deck.sh / g_deck.sw; }
struct SoCell { int slide, x, y; };
struct SoHead { int slide, y; };
static void so_cells (Vec<SoCell> &c, Vec<SoHead> &hd)
{
	c.clear (); hd.clear ();
	int y = 12, col = 0, W = so_cw (), H = so_ch ();
	for (int i = 0; i < g_deck.slides.n; i++)
	{
		Slide *s = g_deck.slides[i];
		if (s->section[0] || i == 0)
		{
			if (col) { y += H + 34; col = 0; }
			if (s->section[0]) { SoHead h; h.slide = i; h.y = y; hd.push (h); y += 28; }
		}
		SoCell k; k.slide = i; k.x = 30 + col * (W + 22); k.y = y; c.push (k);
		if (++col == so_cols ()) { col = 0; y += H + 34; }
	}
	so_total = y + (col ? H + 34 : 0) + 12;
}
static void so_prepare ()
{
	if (so_w <= 0 || g_viewMode != VIEW_SORTER) return;
	Vec<SoCell> c; Vec<SoHead> hd; so_cells (c, hd);
	int maxTop = imax (0, so_total - so_h); if (so_top > maxTop) so_top = maxTop;
	int W = so_cw (), H = so_ch ();
	if (W > 0 && H > 0)
		for (int k = 0; k < c.n; k++) { int y = c[k].y - so_top; if (y + H + 30 >= 0 && y <= so_h) g_thumbs.get (c[k].slide, W, H); }
	ui_w_set_range (so_bar, 0, so_total, so_h, 40);
	ui_w_set_value (so_bar, so_top);
	ui_w_show (so_bar, so_total > so_h);
	ui_view_dirty (so);
}
static void so_paint (long v, long w, long h)
{
	long cw, chh; unsigned *cv = (unsigned *) uikit_canvas_pixels (v, &cw, &chh);
	unsigned bg = jet (J_CREUX), acc = jet (J_ACCENT), ink = jet (J_TEXTE1), dim = jet (J_TEXTE3);
	ui_fill (0, 0, w, h, bg);
	Vec<SoCell> c; Vec<SoHead> hd; so_cells (c, hd);
	int W = so_cw (), H = so_ch ();
	for (int k = 0; k < hd.n; k++)
	{
		int y = hd[k].y - so_top; Slide *s = g_deck.slides[hd[k].slide];
		text_box ("\xE2\x96\xBE", 16, y, 18, dim);
		text_box (s->section, 30, y, 18, ink, font_bold ());
		int n = 0; for (int i = hd[k].slide; i < g_deck.slides.n; i++) { if (i > hd[k].slide && g_deck.slides[i]->section[0]) break; n++; }
		char t[32]; snprintf (t, sizeof t, n == 1 ? TR ("%d slide") : TR ("%d slides"), n);
		int x = 30 + text_w (s->section, font_bold ()) + 10;
		text_box (t, x, y, 18, dim);
		ui_fill (x + text_w (t) + 12, y + 9, imax (0, (int) w - 30 - (x + text_w (t) + 12)), 1, jet (J_LIGNE));
	}
	for (int k = 0; k < c.n; k++)
	{
		int x = c[k].x, y = c[k].y - so_top, i = c[k].slide;
		if (y + H + 30 < 0 || y > h) continue;
		Slide *s = g_deck.slides[i];
		bool sel = g_sorterSel.find (i) >= 0 || i == g_cur;
		if (sel) { unsigned q = i == g_cur ? acc : jet (J_ACCENT_CLAIR); ui_rbox (x - 5, y - 5, W + 10, H + 10, 6, q, q); }
		thumb_blit (cv, (int) cw, (int) chh, i, x, y, W, H);
		ui_frame (x - 1, y - 1, W + 2, H + 2, jet (J_LIGNE_FORTE));
		if (s->hidden) veil (cv, (int) cw, (int) chh, x, y, W, H, bg, 140);
		char n[8]; snprintf (n, sizeof n, "%d", i + 1);
		text_box (n, x, y + H + 4, 18, sel ? acc : ink, font_bold ());
		text_box (TR (TR_NAMES[(int) s->tr.type]), x + 22, y + H + 4, 18, dim);
		char r[32] = "";
		if (s->tr.after >= 0) snprintf (r, sizeof r, "%d:%02d", s->tr.after / 60000, s->tr.after / 1000 % 60);
		else if (s->anim.n) snprintf (r, sizeof r, s->anim.n == 1 ? TR ("%d effect") : TR ("%d effects"), s->anim.n);
		if (r[0]) text_box (r, x + W - text_w (r), y + H + 4, 18, dim);
		if (s->hidden) { ui_rbox (x + W - 64, y + 6, 58, 18, 9, 0x303030, 0x303030); text_box (TR ("hidden"), x + W - 56, y + 6, 18, 0xFFFFFF); }
	}
	if (so_drag >= 0 && so_drop >= 0)
	{
		for (int k = 0; k < c.n; k++) if (c[k].slide == so_drop) ui_rbox (c[k].x - 13, c[k].y - so_top, 4, H, 2, acc, acc);
		if (so_drop >= g_deck.slides.n && c.n) ui_rbox (c[c.n - 1].x + W + 9, c[c.n - 1].y - so_top, 4, H, 2, acc, acc);
	}
}
static int so_at (int mx, int my)
{
	Vec<SoCell> c; Vec<SoHead> hd; so_cells (c, hd);
	for (int k = 0; k < c.n; k++) if (mx >= c[k].x - 6 && mx < c[k].x + so_cw () + 6 && my + so_top >= c[k].y - 6 && my + so_top < c[k].y + so_ch () + 26) return c[k].slide;
	return -1;
}
static long so_mouse (long, long kind, long mx, long my, long info)
{
	int buttons = (int) (info >> 8 & 255), mods = (int) (info >> 16 & 255);
	bool bl = buttons & 1, br = buttons & 2;
	if (kind == 3) return 0;
	if (kind == 1 && br && !so_rdown)
	{
		so_rdown = true;
		int i = so_at ((int) mx, (int) my);
		if (i >= 0 && g_sorterSel.find (i) < 0) { g_sorterSel.clear (); go_slide (i); }
		g_wantSlideMenu = true; g_smX = so_x + (int) mx; g_smY = so_y + (int) my;
		after ();
		return 0;
	}
	if (!br) so_rdown = false;
	if (kind == 1 && bl && !so_down)
	{
		so_down = true; ui_w_focus (so);
		int i = so_at ((int) mx, (int) my);
		unsigned now = ticks ();
		if (i >= 0 && i == g_cur && now - so_lastT < 40) { if (g_onOpenSlide) g_onOpenSlide (i); so_down = false; return 0; }
		so_lastT = now;
		if (i >= 0)
		{
			if (mods & UI_MOD_CTRL) { int f = g_sorterSel.find (i); if (f >= 0) g_sorterSel.erase (f); else g_sorterSel.push (i); if (g_sorterSel.find (g_cur) < 0 && g_cur != i) g_sorterSel.push (g_cur); }
			else if (mods & UI_MOD_SHIFT) { int a = imin (g_cur, i), b = imax (g_cur, i); g_sorterSel.clear (); for (int k = a; k <= b; k++) g_sorterSel.push (k); }
			else if (g_sorterSel.find (i) < 0) g_sorterSel.clear ();
			g_cur = i;
			so_drag = i; so_x0 = (int) mx; so_y0 = (int) my;
		}
		else g_sorterSel.clear ();
		after ();
		return 0;
	}
	if (kind == 0 && so_down && so_drag >= 0)
	{
		if (abs ((int) mx - so_x0) + abs ((int) my - so_y0) > 8)
		{
			Vec<SoCell> c; Vec<SoHead> hd; so_cells (c, hd);
			int best = g_deck.slides.n;
			for (int k = 0; k < c.n; k++) if (my + so_top >= c[k].y - 10 && my + so_top < c[k].y + so_ch () + 30 && mx < c[k].x + so_cw () / 2) { best = c[k].slide; break; }
			if (best != so_drop) { so_drop = best; ui_view_dirty (so); }
		}
		return 0;
	}
	if (kind == 2 && so_down)
	{
		so_down = false;
		if (so_drag >= 0 && so_drop >= 0)
		{
			// the slides selected (or the one dragged) moved before so_drop
			Vec<int> v; if (g_sorterSel.n > 1) for (int i = 0; i < g_sorterSel.n; i++) v.push (g_sorterSel[i]); else v.push (so_drag);
			for (int i = 1; i < v.n; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { int t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
			begin_change ();
			Vec<Slide *> moving; for (int i = 0; i < v.n; i++) moving.push (g_deck.slides[v[i]]);
			Slide *before = so_drop < g_deck.slides.n ? g_deck.slides[so_drop] : 0;
			for (int i = 0; i < moving.n; i++) g_deck.slides.erase (g_deck.slides.find (moving[i]));
			int at = before ? g_deck.slides.find (before) : g_deck.slides.n;
			if (at < 0) at = g_deck.slides.n;
			for (int i = 0; i < moving.n; i++) g_deck.slides.insert (at + i, moving[i]);
			g_cur = at; g_sorterSel.clear (); for (int i = 0; i < moving.n; i++) g_sorterSel.push (at + i);
			done_change (); notify_slides ();
		}
		so_drag = so_drop = -1;
		after ();
	}
	return 0;
}
static long so_wheel (long, long wheel) { so_top = imax (0, imin (so_total - so_h, so_top - (int) wheel * 80)); g_listDirty = true; return 0; }
static long so_key (long, long hid, long ch, long mods)
{
	if ((mods & UI_MOD_CTRL) || hid >= 0xE0) return -1;
	long k = key_code ((int) hid, (int) ch);
	int c = so_cols ();
	int d = k == K_LEFT ? -1 : k == K_RIGHT ? 1 : k == K_UP ? -c : k == K_DOWN ? c : 0;
	if (d) { g_sorterSel.clear (); go_slide (iclamp (g_cur + d, 0, g_deck.slides.n - 1)); after (); return 0; }
	if (k == K_ENTER) { if (g_onOpenSlide) g_onOpenSlide (g_cur); return 0; }
	if (k == K_DEL || k == K_BACK)
	{
		if (g_sorterSel.n > 1)
		{
			begin_change ();
			for (int i = 1; i < g_sorterSel.n; i++) for (int j = i; j > 0 && g_sorterSel[j] > g_sorterSel[j - 1]; j--) { int t = g_sorterSel[j]; g_sorterSel[j] = g_sorterSel[j - 1]; g_sorterSel[j - 1] = t; }
			for (int i = 0; i < g_sorterSel.n && g_deck.slides.n > 1; i++) { delete g_deck.slides[g_sorterSel[i]]; g_deck.slides.erase (g_sorterSel[i]); }
			g_sorterSel.clear (); g_cur = iclamp (g_cur, 0, g_deck.slides.n - 1);
			done_change (); notify_slides ();
		}
		else cmd_delete_slide ();
		after ();
		return 0;
	}
	return -1;
}
static void so_create (int x, int y, int w, int h)
{
	so_x = x; so_y = y; so_w = w; so_h = h;
	so = ui_view (x, y, w, h, UVW_FOCUS);
	ui_view_on (so, (long) so_paint, (long) so_mouse, (long) so_key, (long) so_wheel);
	so_bar = ui_scrollbar (x + w - SB_W_, y, SB_W_, h, 0);
	ui_w_set_parent (so_bar, so);
}
static void so_move (int x, int y, int w, int h)
{
	so_x = x; so_y = y; so_w = w; so_h = h;
	ui_w_move (so, x, y, w, h); ui_w_move (so_bar, x + w - SB_W_, y, SB_W_, h);
}

// ---- the notes ----------------------------------------------------------------------------------------------------
static long np_panel, np_ta, np_lab;
static int np_slide = -1; static Slide *np_cur; static Buf np_last;
static bool g_showNotes = true;
static void np_create (int x, int y, int w, int h)
{
	np_panel = ui_panel (x, y, w, h, 0);
	np_lab = ui_label (x + 8, y + 1, 100, 18, (long) "Notes", ULB_DIM);
	ui_w_set_parent (np_lab, np_panel);
	np_ta = ui_textarea (x + 6, y + 20, w - 12, h - 24, UTA_VSCROLL);
	ui_w_set_parent (np_ta, np_panel);
}
static void np_move (int x, int y, int w, int h)
{
	ui_w_move (np_panel, x, y, w, h); ui_w_move (np_lab, x + 8, y + 1, 100, 18); ui_w_move (np_ta, x + 6, y + 20, w - 12, h - 24);
}
// the slide's notes shown (another slide)
static void np_load ()
{
	Slide *s = cur_slide (); if (!s) return;
	if (np_slide == g_cur && s == np_cur) return;
	np_slide = g_cur; np_cur = s;
	Buf t; s->notes.text_utf8 (t);
	ui_w_set_text (np_ta, (long) t.str ());
	np_last.clear (); np_last.puts (t.str ());
}
// the typed text kept in the slide
static void np_changed ()
{
	Slide *s = cur_slide (); if (!s || s != np_cur) return;
	const char *now = (const char *) ui_w_get_text (np_ta);
	if (!strcmp (now, np_last.str ())) return;
	begin_change (UK_TYPE);
	s->notes.set_text (now, cf_inherit ());
	np_last.clear (); np_last.puts (now);
	g_deck.changes++;
	notify ();
}
static void np_reset () { np_slide = -1; np_cur = 0; }

} // namespace sl

#endif
