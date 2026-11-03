//
// ui.h -- Letters' controls around the page: the two ui_toolbars (their tools placed by ui_tools_place: in a narrow
// window the lists shrink and what matters least leaves the row -- it stays in the menus), the ruler (a ui_view: the
// text's width on the page, the indents' markers dragged, a table's columns, the margins), the status bar (a ui_view:
// the file, the page, the words, the zoom).
//
#ifndef _writer_ui_h
#define _writer_ui_h

#include "view.h"

namespace wr {

enum { RULER_H = 26, STATUS_H = 24 };

// ---- a row of tools -----------------------------------------------------------------------------------
struct Tool { long w; int width, minw, prio, gap; };	// prio: 1 always; the highest leaves first
enum { MAXTOOLS = 40 };
struct Row { Tool t[MAXTOOLS]; int n; };

static void row_add (Row &r, long w, int width, int prio, int gap = 2, int minw = 0)
{
	if (r.n >= MAXTOOLS) return;
	Tool &t = r.t[r.n++];
	t.w = w; t.width = width; t.minw = minw ? minw : width; t.prio = prio; t.gap = gap;
}
// The row's tools placed in [8, w): the lists narrowed, then the tools of the highest priorities hidden.
static void row_place (Row &r, int y, int h, int w)
{
	int total = 8, flex = 0;
	for (int i = 0; i < r.n; i++) { total += r.t[i].width + r.t[i].gap; flex += r.t[i].width - r.t[i].minw; }
	int over = total - w, shrink = over <= 0 ? 0 : wmin (over, flex);
	over -= shrink;
	int top = 1;
	for (int i = 0; i < r.n; i++) top = wmax (top, r.t[i].prio);
	bool hide[MAXTOOLS];
	for (int i = 0; i < r.n; i++) hide[i] = false;
	while (over > 0 && top > 1)
	{
		for (int i = r.n - 1; i >= 0 && over > 0; i--)
			if (r.t[i].prio == top && !hide[i]) { hide[i] = true; over -= r.t[i].minw + r.t[i].gap; }
		top--;
	}
	int x = 8;
	for (int i = 0; i < r.n; i++)
	{
		Tool &t = r.t[i];
		ui_w_show (t.w, !hide[i]);
		if (hide[i]) continue;
		int tw = t.width;
		if (flex && t.width > t.minw) tw = t.width - (t.width - t.minw) * shrink / flex;
		ui_w_move (t.w, x + t.gap, y, tw, h);
		x += t.gap + tw;
	}
}

// ---- the ruler -------------------------------------------------------------------------------------------
// The page's width at the view's zoom and place: the margins grey, the paragraph's box white (the text's, or a
// table's cell's), a centimetre's numbers from its left edge; the paragraph's indents: the first line's (a triangle on
// top), the hanging one's (below; its little box moves both), the right one's; in a table, its columns' edges. Dragged,
// they change the selected paragraphs' indents (one edit), a column's width; the margins' edges drag the page's margins.
enum { RM_NONE, RM_FIRST, RM_HANG, RM_LEFT, RM_RIGHT, RM_MLEFT, RM_MRIGHT, RM_COL };
static bool g_inches;				// the ruler's unit (else centimetres)
static long g_ruler;
static int  r_drag, r_hot, r_col, r_colX, r_w;

static const Para *ruler_box (int *bl, int *br, const Table **t)
{
	relayout ();
	placeStory ();
	const Para *q = L.d->p[sel_a ().p];
	int ox = originX ();
	*bl = ox + (q->x64 >> 6); *br = *bl + (q->w64 >> 6);
	*t = L.d->cur == SY_BODY ? para_table (*L.d, q) : 0;
	return q;
}
static int colEdge (const Table *t, int c) { return originX () + ((t->x64 + t->colX64[c]) >> 6); }
static void r_fill (int x, int y, int w, int h, unsigned c)
{
	int x1 = wmin (x + w, r_w); x = wmax (x, 0);
	if (x1 > x) ui_fill (x, y, x1 - x, h, c);
}
static void r_triangle (int x, int y, bool down, unsigned c)
{
	int p[6] = { (x - 4) * 16, (down ? y : y + 6) * 16, (x + 5) * 16, (down ? y : y + 6) * 16, x * 16 + 8, (down ? y + 6 : y) * 16 };
	ui_vp_poly ((long) p, 3, c);
}
static void ruler_paint (long, long w, long h)
{
	r_w = (int) w;
	ui_fill (0, 0, w, h, jet (J_FACE));
	int bl, br; const Table *t;
	const Para *q = ruler_box (&bl, &br, &t);
	int ox = originX ();
	int x0 = ox, x1 = ox + L.pageW;
	int tl = ox + (L.ml64 >> 6), tr = ox + L.pageW - (L.mr64 >> 6);
	int y0 = 5, rh = (int) h - 10;
	unsigned band = ui_mix (jet (J_FACE), 0xFFFFFF, 90), white = 0xFFFFFF, inkc = 0x555555;
	r_fill (x0, y0, x1 - x0, rh, band);
	r_fill (t ? bl : tl, y0, (t ? br - bl : tr - tl), rh, white);
	r_fill (x0, y0, x1 - x0, 1, jet (J_LIGNE));
	r_fill (x0, y0 + rh - 1, x1 - x0, 1, jet (J_LIGNE));
	// the ticks: from the box's left edge both ways
	int unitTw = g_inches ? 1440 : 567, sub = g_inches ? 8 : 4;
	int unitPx = tw2px64 (unitTw) >> 6;
	int every = unitPx >= 26 ? 1 : unitPx >= 13 ? 2 : unitPx >= 6 ? 5 : 10;
	bool subs = unitPx / sub >= 4;
	for (int dir = -1; dir <= 1; dir += 2)
		for (int k = dir < 0 ? 1 : 0; ; k++)
		{
			int tw = dir * k * unitTw / sub;
			int x = bl + (tw2px64 (tw) >> 6);
			if (x < x0 || x > x1) break;
			if (x < 0 || x >= w) continue;
			int m = k % sub, u = k / sub;
			if (m == 0 && k && u % every == 0)
			{
				char b[8]; snprintf (b, sizeof b, "%d", u);
				int tw2 = (int) ui_text_width ((long) b, 11, font ());
				ui_text ((long) b, x - tw2 / 2, y0 + rh / 2 + 4, inkc, 11, font (), 0);
			}
			else if (k && (m == 0 || subs))
			{
				int th = m == 0 ? 5 : m == sub / 2 ? 5 : 2;
				ui_fill (x, y0 + rh / 2 - th / 2, 1, th, 0x888888);
			}
		}
	// a table's column edges
	if (t)
		for (int c = 0; c <= t->ncols; c++)
		{
			int x = colEdge (t, c);
			bool hot = c > 0 && ((r_hot == RM_COL && r_col == c - 1) || (r_drag == RM_COL && r_col == c - 1));
			if (r_drag == RM_COL && r_col == c - 1) x = r_colX;
			r_fill (x - 3, y0 + 1, 7, rh - 2, hot ? jet (J_ACCENT) : jet (J_LIGNE_FORTE));
			r_fill (x - 1, y0 + 3, 3, rh - 6, hot ? 0xFFFFFF : jet (J_FACE));
		}
	// the markers (the paragraph at the caret)
	int lx = bl + (tw2px64 (q->pf.left) >> 6), fx = bl + (tw2px64 (q->pf.left + q->pf.first) >> 6);
	int rx = br - (tw2px64 (q->pf.right) >> 6);
	unsigned mk = jet (J_TEXTE2), mkHot = jet (J_ACCENT);
	r_triangle (fx, y0 - 1, true, r_hot == RM_FIRST || r_drag == RM_FIRST ? mkHot : mk);
	r_triangle (lx, y0 + rh - 7, false, r_hot == RM_HANG || r_drag == RM_HANG ? mkHot : mk);
	r_fill (lx - 4, y0 + rh, 9, 4, r_hot == RM_LEFT || r_drag == RM_LEFT ? mkHot : mk);
	r_triangle (rx, y0 + rh - 7, false, r_hot == RM_RIGHT || r_drag == RM_RIGHT ? mkHot : mk);
	if (r_drag == RM_MLEFT || r_drag == RM_MRIGHT || r_hot == RM_MLEFT || r_hot == RM_MRIGHT)
		r_fill ((r_hot == RM_MLEFT || r_drag == RM_MLEFT ? tl : tr) - 1, y0, 2, rh, jet (J_ACCENT));
}
static void refresh ();
static long ruler_mouse (long, long kind, long mx0, long my0, long)
{
	if (kind == 3) { if (r_hot) { r_hot = RM_NONE; ui_view_dirty (g_ruler); } return 0; }
	int mx = (int) mx0, my = (int) my0;
	int bl, br; const Table *t;
	const Para *q = ruler_box (&bl, &br, &t);
	int ox = originX ();
	int tl = ox + (L.ml64 >> 6), tr = ox + L.pageW - (L.mr64 >> 6);
	if (r_drag == RM_NONE)
	{
		int lx = bl + (tw2px64 (q->pf.left) >> 6), fx = bl + (tw2px64 (q->pf.left + q->pf.first) >> 6), rx = br - (tw2px64 (q->pf.right) >> 6);
		int hot = RM_NONE;
		if (my < RULER_H / 2 && mx >= fx - 5 && mx <= fx + 5) hot = RM_FIRST;
		else if (my >= RULER_H - 6 && mx >= lx - 5 && mx <= lx + 5) hot = RM_LEFT;
		else if (my >= RULER_H / 2 && mx >= lx - 5 && mx <= lx + 5) hot = RM_HANG;
		else if (my >= RULER_H / 2 && mx >= rx - 5 && mx <= rx + 5) hot = RM_RIGHT;
		if (!hot && t)
			for (int c = 1; c <= t->ncols; c++) { int x = colEdge (t, c); if (mx >= x - 3 && mx <= x + 3) { hot = RM_COL; r_col = c - 1; break; } }
		if (!hot && !t && mx >= tl - 3 && mx <= tl + 3) hot = RM_MLEFT;
		else if (!hot && !t && mx >= tr - 3 && mx <= tr + 3) hot = RM_MRIGHT;
		if (hot != r_hot) { r_hot = hot; ui_view_dirty (g_ruler); }
		if (kind == 1 && hot != RM_NONE)
		{
			r_drag = hot; r_colX = mx;
			Pos a = sel_a (), b = sel_b ();
			if (hot <= RM_RIGHT) ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
		}
		return 0;
	}
	// dragging: the new place, in twips from the box's left edge (snapped to a 16th of the unit)
	int snap = g_inches ? 90 : 71;
	if (r_drag == RM_COL)
	{
		r_colX = mx;
		ui_view_dirty (g_ruler);
		if (kind == 2 && t)
		{
			int xTw = px642tw ((mx - ox) * 64 - t->x64);
			xTw = (xTw + snap / 2) / snap * snap;
			r_drag = RM_NONE;
			ed_table_col_edge (sel_a ().p, r_col, xTw);
			ui_view_dirty (g_view);
			refresh ();
		}
		return 0;
	}
	int tw = px642tw ((mx - bl) << 6);
	tw = (tw + (tw >= 0 ? snap / 2 : -snap / 2)) / snap * snap;
	Pos a = sel_a (), b = sel_b ();
	int textTw = px642tw (q->w64);
	for (int p = a.p; p <= b.p && r_drag <= RM_RIGHT; p++)
	{
		Para *pq = L.d->p[p];
		int first = pq->pf.left + pq->pf.first;		// (the first line's absolute place)
		switch (r_drag)
		{
		case RM_FIRST: pq->pf.first = (short) wclamp (tw - pq->pf.left, -pq->pf.left, textTw - pq->pf.left - pq->pf.right - 200); break;
		case RM_HANG: { int l = wclamp (tw, 0, textTw - pq->pf.right - 300); pq->pf.left = (short) l; pq->pf.first = (short) (first - l); break; }
		case RM_LEFT: { int l = wclamp (tw, wmax (0, -pq->pf.first), textTw - pq->pf.right - 300); pq->pf.left = (short) l; break; }
		case RM_RIGHT: pq->pf.right = (short) wclamp (textTw - tw, 0, textTw - pq->pf.left - 300); break;
		}
		pq->dirty = true;
	}
	if (r_drag == RM_MLEFT || r_drag == RM_MRIGHT)
	{
		PageSetup &pw = L.d->page;
		int abs = px642tw ((mx - ox) << 6);
		abs = (abs + snap / 2) / snap * snap;
		if (r_drag == RM_MLEFT) pw.left = wclamp (abs, 0, pw.w - pw.right - 1440);
		else pw.right = wclamp (pw.w - abs, 0, pw.w - pw.left - 1440);
		doc_dirty_all (*L.d);
	}
	g_relayout = true;
	ui_view_dirty (g_view); ui_view_dirty (g_ruler);
	if (kind == 2)
	{
		if (r_drag <= RM_RIGHT) doc_end_edit (*L.d, b.p - a.p + 1, g_caret, g_anchor);
		else L.d->changes++;
		r_drag = RM_NONE;
		changed ();
	}
	return 0;
}

// ---- the status bar ---------------------------------------------------------------------------------------
static long g_status;
static char st_left[160], st_mid[80];
static int  st_w;
static void (*g_zoomBy) (int dir);

static void status_set (const char *l, const char *m)
{
	if (!strcmp (l, st_left) && !strcmp (m, st_mid)) return;
	scpy (st_left, l, sizeof st_left); scpy (st_mid, m, sizeof st_mid);
	ui_view_dirty (g_status);
}
static void status_paint (long, long w, long h)
{
	st_w = (int) w;
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (0, 0, w, 1, jet (J_LIGNE));
	unsigned dim = jet (J_TEXTE2);
	int px = ui_px (), base = (int) h / 2 + px * 37 / 100;
	int zw = 52, zx = (int) w - 12 - 18 - zw - 18;
	int mw = (int) ui_text_width ((long) st_mid, px, font ()), mx = (int) w / 2 - mw / 2;
	int lw = (int) ui_text_width ((long) st_left, px, font ());
	if (mx < 10 + lw + 16) mx = 10 + lw + 16;
	if (mx + mw > zx - 10) mx = zx - 10 - mw;
	ui_text_fit ((long) st_left, 10, base, dim, px, font (), wmax (24, mx - 26));
	ui_text ((long) st_mid, mx, base, dim, px, font (), 0);
	char z[8]; snprintf (z, sizeof z, "%d%%", L.zoom);
	ui_text ((long) "-", zx + 6, base, dim, px + 2, font (), 0);
	ui_text ((long) z, zx + 18 + (zw - (int) ui_text_width ((long) z, px, font ())) / 2, base, dim, px, font (), 0);
	ui_text ((long) "+", zx + 18 + zw + 5, base, dim, px + 2, font (), 0);
}
static long status_mouse (long, long kind, long mx, long, long)
{
	int zw = 52, x = st_w - 12 - 18 - zw - 18;
	if (kind == 1 && g_zoomBy)
	{
		if (mx >= x && mx < x + 18) g_zoomBy (-1);
		else if (mx >= x + 18 + zw && mx < x + 36 + zw) g_zoomBy (1);
	}
	return 0;
}

} // namespace wr

#endif
