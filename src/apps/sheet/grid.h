//
// grid.h -- the grid: the column letters and row numbers (the selection's in the accent; their edges
// dragged to size a column / row, a double click fits it), the cells in up to four panes (the frozen rows
// and columns stay), the selection (a range, whole columns / rows, all), the cursor's cell, the fill
// handle (dragged: a series), the copied range's dashes, the charts over the cells (moved, sized).
// Typing edits a cell in place: a formula's references coloured -- and the same colours framing their
// cells --, cells pointed by the arrows or the mouse while a formula waits for a reference, the function
// being typed offered in a list, its arguments shown as a tip. Enter / Tab put the entry in. All of it drawn
// in one ui_view, no widget per cell; the cells kept in a copy, so that moving the cursor draws no text.
//
#ifndef _sheet_grid_h
#define _sheet_grid_h

#include <time.h>
#include "chart.h"

namespace ss {

enum { MODE_READY, MODE_ENTER, MODE_EDIT, MODE_POINT };
static const unsigned REFCOL[7] = { 0x1A73E8, 0xD93025, 0x188038, 0x9334E6, 0xE37400, 0x12A4AF, 0xC5221F };

// ---- the text being typed ---------------------------------------------------------------------------------
struct EditLine
{
	Buf t;
	int caret = 0, anchor = 0;
	bool on = false, typed = false;			// editing; typed: started by typing (the arrows leave the cell)
	int r = 0, c = 0, sheetId = 0;
	bool pointing = false;				// a reference being pointed: its text [ps, pe), its cells
	int ps = 0, pe = 0, pr0 = 0, pc0 = 0, pr1 = 0, pc1 = 0;
	int s0 () const { return imin (caret, anchor); }
	int s1 () const { return imax (caret, anchor); }
	void start (const char *s, bool typed_, int r_, int c_, int sid)
	{
		t.clear (); t.puts (s); caret = anchor = t.n; on = true; typed = typed_; r = r_; c = c_; sheetId = sid; pointing = false;
	}
	void stop () { on = false; pointing = false; t.clear (); caret = anchor = 0; }
	void replace (int a, int b, const char *s, int n)
	{
		Buf o; o.putn (t.b ? t.b : "", a); o.putn (s, n); o.putn (t.b ? t.b + b : "", t.n - b);
		t.clear (); t.putn (o.b ? o.b : "", o.n);
		caret = anchor = a + n;
	}
	void insert (const char *s, int n) { replace (s0 (), s1 (), s, n); pointing = false; }
	void insert_char (unsigned cp) { char b[4]; int n = u8_enc (cp, b); insert (b, n); }
	int prev (int i) const { if (i <= 0) return 0; i--; while (i > 0 && ((unsigned char) t.b[i] & 0xC0) == 0x80) i--; return i; }
	int next (int i) const { if (i >= t.n) return t.n; int l; u8_dec (t.b + i, t.n - i, &l); return i + l; }
	int word_left (int i) const { while (i > 0 && t.b[i - 1] == ' ') i = prev (i); while (i > 0 && t.b[i - 1] != ' ' && !strchr ("(),;=+-*/&^<>", t.b[i - 1])) i = prev (i); return i; }
	int word_right (int i) const { while (i < t.n && t.b[i] != ' ' && !strchr ("(),;=+-*/&^<>", t.b[i])) i = next (i); while (i < t.n && t.b[i] == ' ') i = next (i); return i; }
	void backspace () { if (s0 () != s1 ()) insert ("", 0); else if (caret > 0) { int p = prev (caret); replace (p, caret, "", 0); } pointing = false; }
	void del () { if (s0 () != s1 ()) insert ("", 0); else if (caret < t.n) { int q = next (caret); replace (caret, q, "", 0); } pointing = false; }
	bool formula () const { return t.n > 0 && (t.b[0] == '=' || ((t.b[0] == '+' || t.b[0] == '-') && t.n > 1)); }
	// Does the text before the caret wait for a reference (after an operator, "(", ",")?
	bool can_point () const
	{
		if (!formula ()) return false;
		if (pointing && caret == pe) return true;
		int i = caret;
		while (i > 0 && t.b[i - 1] == ' ') i--;
		if (i == 0) return false;
		char ch = t.b[i - 1];
		if (i == 1 && (ch == '=' || ch == '+' || ch == '-')) return true;
		return strchr ("=(,;+-*/^&<>:%", ch) != 0;
	}
};

// The references of a formula being typed: where they are, what they point at, their colours.
struct RefHi { int at, len; int sheetId; Rect r; unsigned col; };
static int edit_refs (Book &b, const EditLine &e, RefHi *out, int max)
{
	if (!e.formula ()) return 0;
	int off = e.t.b[0] == '=' ? 1 : 0;
	Lexer L; L.b = &b; L.s = e.t.b + off; L.n = e.t.n - off; L.i = 0; L.tok = 0; L.nt = L.ct = 0; L.err = 0; L.errPos = 0; L.brace = 0;
	L.run ();						// (what reads up to an error is enough)
	int n = 0;
	for (int i = 0; i < L.nt && n < max; i++)
	{
		const Tok &k = L.tok[i];
		if ((k.t != TK_REF && k.t != TK_AREA) || (k.fl & TF_BAD)) continue;
		RefHi &h = out[n];
		h.at = k.at + off; h.len = k.len; h.sheetId = k.sheet ? k.sheet : e.sheetId;
		h.r.r0 = k.r0; h.r.c0 = k.c0; h.r.r1 = k.r1; h.r.c1 = k.c1;
		// the same reference: the same colour
		int ci = n;
		for (int j = 0; j < n; j++) if (out[j].sheetId == h.sheetId && !memcmp (&out[j].r, &h.r, sizeof (Rect))) { ci = -1 - j; break; }
		h.col = ci >= 0 ? REFCOL[ci % 7] : out[-1 - ci].col;
		n++;
	}
	free (L.tok);
	return n;
}
// The function the caret is in, and which of its arguments (-1: none).
static int edit_function (const EditLine &e, int *argIdx)
{
	if (!e.formula ()) return -1;
	int stack[64], args[64], depth = 0;
	bool str = false;
	for (int i = 0; i < e.caret && i < e.t.n; i++)
	{
		char ch = e.t.b[i];
		if (ch == '"') { str = !str; continue; }
		if (str) continue;
		if (ch == '(')
		{
			int j = i; while (j > 0 && (isalnum ((unsigned char) e.t.b[j - 1]) || e.t.b[j - 1] == '.' || e.t.b[j - 1] == '_')) j--;
			int f = j < i ? fn_lookup (e.t.b + j, i - j) : -1;
			if (depth < 64) { stack[depth] = f; args[depth] = 0; depth++; }
		}
		else if (ch == ')') { if (depth > 0) depth--; }
		else if ((ch == ',' || ch == ';') && depth > 0) args[depth - 1]++;
	}
	while (depth > 0 && stack[depth - 1] < 0) depth--;
	if (depth <= 0) return -1;
	*argIdx = args[depth - 1];
	return stack[depth - 1];
}
// The function name being typed at the caret ("=SU|"): its start; -1 none.
static int edit_word (const EditLine &e, char *word, int cap)
{
	if (!e.formula () || e.caret != e.t.n) return -1;
	int j = e.caret;
	while (j > 0 && (isalpha ((unsigned char) e.t.b[j - 1]) || e.t.b[j - 1] == '.')) j--;
	if (j == e.caret || (j > 0 && (isalnum ((unsigned char) e.t.b[j - 1]) || e.t.b[j - 1] == '$' || e.t.b[j - 1] == '"'))) return -1;
	int n = imin (e.caret - j, cap - 1);
	memcpy (word, e.t.b + j, n); word[n] = 0;
	return j;
}

// ---- the grid: a ui_view, its two scroll bars, its state --------------------------------------------------
enum { D_NONE, D_SELECT, D_COLSEL, D_ROWSEL, D_COLSIZE, D_ROWSIZE, D_FILL, D_POINT, D_CHART, D_CHARTSIZE };
enum { HEAD_H = 22, SB = 13 };

static Book g_b;
static long g_view, g_vbar, g_hbar;			// the view, its scroll bars
static int g_vx, g_vy, g_vw, g_vh;			// its place in the window
static int g_z = 100;					// the zoom (%)
static EditLine g_ed;
static bool g_copyMarquee; static Rect g_copyRect; static int g_copySheet;
static int g_selChart = -1;				// the chart chosen (-1 none)
static char g_acWord[40]; static int g_acStart = -1, g_acSel, g_acN, g_acList[12];	// the functions offered while typing
static Rect g_fillTo = { 0, 0, -1, -1 };		// (the fill handle's drag: the range to be)
static Rect g_fillRect; static int g_fitCol, g_fitRow;	// (what an action asked of the app: see grid_context)
static int g_drag = D_NONE, g_dragI, g_dragV, g_dragX0, g_dragY0, g_chartX0, g_chartY0, g_chartW0, g_chartH0, g_corner;
static int g_tabC = -1;					// a row typed with Tab: its first column (Enter goes back there)
static bool g_moved;					// the chart dragged has moved
static bool g_blink = true; static unsigned g_blinkT;
static bool g_wholeCols, g_wholeRows;
static bool g_cellsDirty = true;			// the cells must be drawn again (else: their copy, the selection over it)
static uint32_t *g_layer; static int g_layerW, g_layerH;	// the copy: the cells and charts, as drawn last
static int g_lastTop = -1, g_lastLeft = -1, g_lastZ, g_lastSheet = -1;
static int g_cursor = UC_ARROW;
static int g_rightWhere = -1;				// (the right button is down: where it was pressed)

// (main.cpp)
static void grid_changed ();				// the selection / the typing changed
static void grid_edited ();				// a cell was put in (the book changed)
static void grid_context (int x, int y, int where);	// right click: 0 cells, 1 column headers, 2 row headers, 3 a chart; 4..9 see main.cpp
static void grid_chart_open (int i);			// (a double click: the app opens it when the event is over)
static void grid_filter (int col, int x, int y);	// an AutoFilter's button clicked (x, y: below it, in the window)
static void grid_zoom (int dir);
static bool grid_commit (int r, int c, const char *text);	// the app puts the entry in (undo, format...)

static unsigned ticks_ms ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) t.tv_sec * 1000u + (unsigned) (t.tv_nsec / 1000000);
}

static Sheet *S () { return g_b.sh[g_b.active]; }
static int gv_mode () { return !g_ed.on ? MODE_READY : g_ed.pointing ? MODE_POINT : g_ed.typed ? MODE_ENTER : MODE_EDIT; }
static void gv_dirty () { g_cellsDirty = true; ui_view_dirty (g_view); }
static void gv_changed () { ui_view_dirty (g_view); grid_changed (); }
static void gv_focus () { ui_w_focus (g_view); }

// ---- geometry
static int gv_rowhead ()
{
	char t[12]; snprintf (t, sizeof t, "%d", S ()->topR + 60);
	return imax (40, (int) strlen (t) * 8 + 14);
}
static int gv_frozen_w () { Sheet *s = S (); int w = 0; for (int c = 0; c < s->freezeC; c++) w += col_w (s, c) * g_z / 100; return w; }
static int gv_frozen_h () { Sheet *s = S (); int h = 0; for (int r = 0; r < s->freezeR; r++) h += row_h (s, r) * g_z / 100; return h; }
static void gv_panes (PaneView pv[4])				// 0: top-left (frozen both), 1: top, 2: left, 3: the main one
{
	Sheet *s = S ();
	int RW = gv_rowhead (), fw = gv_frozen_w (), fh = gv_frozen_h ();
	int x1 = g_vw - SB, y1 = g_vh - SB;
	int fw2 = imin (fw, x1 - RW), fh2 = imin (fh, y1 - HEAD_H);
	pane_layout (s, g_z, 0, s->freezeC, 0, s->freezeR, RW, HEAD_H, RW + fw2, HEAD_H + fh2, pv[0]);
	pane_layout (s, g_z, imax (s->leftC, s->freezeC), MAXC, 0, s->freezeR, RW + fw2, HEAD_H, x1, HEAD_H + fh2, pv[1]);
	pane_layout (s, g_z, 0, s->freezeC, imax (s->topR, s->freezeR), MAXR, RW, HEAD_H + fh2, RW + fw2, y1, pv[2]);
	pane_layout (s, g_z, imax (s->leftC, s->freezeC), MAXC, imax (s->topR, s->freezeR), MAXR, RW + fw2, HEAD_H + fh2, x1, y1, pv[3]);
}
static Rect gv_sel ()
{
	Sheet *s = S ();
	Rect r = { imin (s->ancR, s->curR), imin (s->ancC, s->curC), imax (s->ancR, s->curR), imax (s->ancC, s->curC) };
	if (g_wholeCols) { r.r0 = 0; r.r1 = MAXR - 1; }
	if (g_wholeRows) { r.c0 = 0; r.c1 = MAXC - 1; }
	// a merge met extends the range (whole rows / columns stay as they are, as in Calc and Excel)
	for (int pass = 0; pass < 4 && !g_wholeCols && !g_wholeRows; pass++)
	{
		bool grew = false;
		for (int i = 0; i < s->nmerge; i++)
		{
			const Rect &m = s->merges[i];
			if (!rect_meets (m, r)) continue;
			if (m.r0 < r.r0) { r.r0 = m.r0; grew = true; }
			if (m.c0 < r.c0) { r.c0 = m.c0; grew = true; }
			if (m.r1 > r.r1) { r.r1 = m.r1; grew = true; }
			if (m.c1 > r.c1) { r.c1 = m.c1; grew = true; }
		}
		if (!grew) break;
	}
	return r;
}
// The cell under (mx, my); false when outside the cells.
static bool gv_cell_at (int mx, int my, int *r, int *c)
{
	static PaneView pv[4]; gv_panes (pv);
	for (int p = 0; p < 4; p++)
	{
		const PaneView &v = pv[p];
		if (mx < v.x0 || mx >= v.x1 || my < v.y0 || my >= v.y1) continue;
		int cc = -1, rr = -1;
		for (int i = 0; i < v.nc; i++) if (mx >= v.col[i].at && mx < v.col[i].at + v.col[i].len) cc = v.col[i].i;
		for (int i = 0; i < v.nr; i++) if (my >= v.row[i].at && my < v.row[i].at + v.row[i].len) rr = v.row[i].i;
		if (cc < 0) cc = v.nc ? v.col[v.nc - 1].i : 0;
		if (rr < 0) rr = v.nr ? v.row[v.nr - 1].i : 0;
		*r = rr; *c = cc;
		return true;
	}
	return false;
}
// A cell's box on the grid (its merge's box), from the pane it is in; false: not shown.
static bool gv_cell_box (int r, int c, int *x, int *y, int *w, int *h)
{
	Sheet *s = S ();
	int mi = merge_at (s, r, c);
	Rect m = mi >= 0 ? s->merges[mi] : Rect { r, c, r, c };
	static PaneView pv[4]; gv_panes (pv);
	int p = (m.r0 < s->freezeR ? 0 : 2) + (m.c0 < s->freezeC ? 0 : 1);
	const PaneView &v = pv[p];
	*x = pane_col_x (s, g_z, v, m.c0); *w = pane_col_x (s, g_z, v, m.c1 + 1) - *x;
	*y = pane_row_y (s, g_z, v, m.r0); *h = pane_row_y (s, g_z, v, m.r1 + 1) - *y;
	return *x < v.x1 && *x + *w > v.x0 && *y < v.y1 && *y + *h > v.y0;
}
// An AutoFilter's button in its header cell (false: not in view).
static bool gv_filter_button (int c, int *bx, int *by, int *bs)
{
	Sheet *s = S ();
	if (!s->af.on || c < s->af.r.c0 || c > s->af.r.c1) return false;
	int x, y, w, h;
	if (!gv_cell_box (s->af.r.r0, c, &x, &y, &w, &h) || w < 12 || h < 10) return false;
	*bs = imin (16 * g_z / 100 + 1, h - 3); if (*bs < 9) *bs = 9;
	*bx = x + w - *bs - 2; *by = y + h - *bs - 2;
	return true;
}
// ---- scrolling
static int gv_vis_rows () { static PaneView pv[4]; gv_panes (pv); return imax (1, pv[3].nr - 1); }
static int gv_vis_cols () { static PaneView pv[4]; gv_panes (pv); return imax (1, pv[3].nc - 1); }
static void gv_ensure_visible (int r, int c)
{
	Sheet *s = S ();
	static PaneView pv[4]; gv_panes (pv);
	if (r >= s->freezeR)
	{
		if (r < s->topR) s->topR = r;
		else
		{
			// (the rows below the top that fit whole)
			int y = pv[3].y0, last = s->topR - 1;
			for (int rr = imax (s->topR, s->freezeR); rr < MAXR; rr++) { int h = row_h (s, rr) * g_z / 100; if (y + h > pv[3].y1) break; y += h; last = rr; }
			while (r > last && s->topR < MAXR - 1)
			{
				s->topR++;
				y = pv[3].y0; last = s->topR - 1;
				for (int rr = s->topR; rr < MAXR; rr++) { int h = row_h (s, rr) * g_z / 100; if (y + h > pv[3].y1) break; y += h; last = rr; }
			}
		}
	}
	if (c >= s->freezeC)
	{
		if (c < s->leftC) s->leftC = c;
		else
		{
			for (int guard = 0; guard < MAXC; guard++)
			{
				int x = pv[3].x0, last = s->leftC - 1;
				for (int cc = imax (s->leftC, s->freezeC); cc < MAXC; cc++) { int w = col_w (s, cc) * g_z / 100; if (x + w > pv[3].x1) break; x += w; last = cc; }
				if (c <= last || s->leftC >= MAXC - 1) break;
				s->leftC++;
			}
		}
	}
	s->topR = iclamp (s->topR, s->freezeR, MAXR - 1); s->leftC = iclamp (s->leftC, s->freezeC, MAXC - 1);
	ui_view_dirty (g_view);
}
static void gv_scroll_by (int dr, int dc)
{
	Sheet *s = S ();
	s->topR = iclamp (s->topR + dr, s->freezeR, MAXR - 1);
	s->leftC = iclamp (s->leftC + dc, s->freezeC, MAXC - 1);
	ui_view_dirty (g_view);
}
// The bars follow the sheet (the used area and a bit more: their range grows as one goes).
static void gv_bars ()
{
	Sheet *s = S ();
	static long was[6];
	sheet_bounds (s);
	long totR = imax (s->maxR + 1, s->topR + gv_vis_rows ()) + 50, totC = imax (s->maxC + 1, s->leftC + gv_vis_cols ()) + 10;
	long now[6] = { totR, gv_vis_rows (), s->topR, totC, gv_vis_cols (), s->leftC };
	if (memcmp (now, was, sizeof now))
	{
		memcpy (was, now, sizeof was);
		ui_w_set_range (g_vbar, 0, totR, now[1], 1); ui_w_set_value (g_vbar, s->topR);
		ui_w_set_range (g_hbar, 0, totC, now[4], 1); ui_w_set_value (g_hbar, s->leftC);
	}
}
// ---- the selection
static void gv_select_cell (int r, int c, bool extend)
{
	Sheet *s = S ();
	r = iclamp (r, 0, MAXR - 1); c = iclamp (c, 0, MAXC - 1);
	g_tabC = -1;
	if (!extend) { g_wholeCols = g_wholeRows = false; s->ancR = r; s->ancC = c; }
	s->curR = r; s->curC = c;
	if (!extend) { int mi = merge_at (s, r, c); if (mi >= 0) { s->curR = s->ancR = s->merges[mi].r0; s->curC = s->ancC = s->merges[mi].c0; } }
	g_selChart = -1;
	gv_ensure_visible (r, c);
	gv_changed ();
}
static void gv_select_range (Rect r)
{
	Sheet *s = S ();
	g_wholeCols = r.r0 == 0 && r.r1 >= MAXR - 1; g_wholeRows = r.c0 == 0 && r.c1 >= MAXC - 1;
	s->ancR = r.r0; s->ancC = r.c0; s->curR = r.r1; s->curC = r.c1;
	if (g_wholeCols) { s->ancR = 0; s->curR = 0; }
	if (g_wholeRows) { s->ancC = 0; s->curC = 0; }
	if (!g_wholeCols && !g_wholeRows) { s->curR = r.r0; s->curC = r.c0; s->ancR = r.r1; s->ancC = r.c1; }
	g_selChart = -1;
	gv_changed ();
}
static void gv_select_cols (int c0, int c1) { Sheet *s = S (); g_wholeCols = true; g_wholeRows = false; s->ancC = c0; s->curC = c1; s->ancR = s->curR = imax (0, s->topR); g_selChart = -1; gv_changed (); }
static void gv_select_rows (int r0, int r1) { Sheet *s = S (); g_wholeRows = true; g_wholeCols = false; s->ancR = r0; s->curR = r1; s->ancC = s->curC = imax (0, s->leftC); g_selChart = -1; gv_changed (); }
static void gv_select_all () { Sheet *s = S (); g_wholeCols = g_wholeRows = true; s->ancR = s->ancC = 0; s->curR = s->curC = 0; g_selChart = -1; gv_changed (); }
static void gv_reset_sel_kind () { g_wholeCols = g_wholeRows = false; }

// ---- editing
static void gv_begin_edit (bool typed, const char *initial)
{
	Sheet *s = S ();
	if (g_ed.on) return;
	if (initial) g_ed.start (initial, typed, s->curR, s->curC, s->id);
	else
	{
		Buf t; cell_edit_text (g_b, s, s->cells.get (s->curR, s->curC), t);
		g_ed.start (t.str (), typed, s->curR, s->curC, s->id);
	}
	g_acStart = -1;
	g_blink = true; g_blinkT = ticks_ms ();
	gv_ensure_visible (s->curR, s->curC);
	gv_changed ();
}
static void gv_cancel_edit () { if (!g_ed.on) return; g_ed.stop (); g_acStart = -1; gv_changed (); }
// Put the entry in; then move (dr, dc). false: the formula cannot be read (the editing goes on).
static bool gv_commit (int dr, int dc)
{
	if (!g_ed.on) return true;
	Sheet *s = book_sheet_by_id (g_b, g_ed.sheetId);
	if (!s) { g_ed.stop (); return true; }
	const char *t = g_ed.t.str ();
	if (!grid_commit (g_ed.r, g_ed.c, t)) return false;
	int r = g_ed.r, c = g_ed.c;
	g_ed.stop (); g_acStart = -1;
	if (g_b.sh[g_b.active] != s) { for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i] == s) g_b.active = i; }
	gv_dirty ();
	grid_edited ();
	if (dr || dc)
	{
		Rect sl = gv_sel ();
		bool multi = (sl.r1 > sl.r0 || sl.c1 > sl.c0) && !g_wholeCols && !g_wholeRows;
		if (multi)						// (within the selection, wrapping)
		{
			int nr = r + dr, nc = c + dc;
			if (nr > sl.r1) { nr = sl.r0; nc++; }
			if (nr < sl.r0) { nr = sl.r1; nc--; }
			if (nc > sl.c1) { nc = sl.c0; nr += dc ? 1 : 0; if (nr > sl.r1) nr = sl.r0; }
			if (nc < sl.c0) { nc = sl.c1; }
			s->curR = iclamp (nr, sl.r0, sl.r1); s->curC = iclamp (nc, sl.c0, sl.c1);
			gv_ensure_visible (s->curR, s->curC);
		}
		else
		{
			int tabC = g_tabC;
			if (dc && !dr && tabC < 0) tabC = c;		// (Tab: the row's first column kept)
			int mi = merge_at (s, r, c);
			if (mi >= 0) { if (dr > 0) r = s->merges[mi].r1; if (dc > 0) c = s->merges[mi].c1; }
			if (dr > 0 && !dc && tabC >= 0) gv_select_cell (r + dr, tabC, false);
			else gv_select_cell (r + dr, c + dc, false);
			g_tabC = dc && !dr ? tabC : -1;
		}
	}
	gv_changed ();
	return true;
}
// A reference pointed (by the arrows or the mouse): put / replaced in the formula at the caret.
static void gv_point_at (int r0, int c0, int r1, int c1)
{
	Sheet *s = S ();
	char ref[80]; Buf o;
	EditLine &ed = g_ed;
	if (s->id != ed.sheetId) { put_sheet_name (o, s->name); o.put ('!'); }
	Rect q = { imin (r0, r1), imin (c0, c1), imax (r0, r1), imax (c0, c1) };
	cell_name (q.r0, q.c0, ref); o.puts (ref);
	if (q.r1 != q.r0 || q.c1 != q.c0) { o.put (':'); cell_name (q.r1, q.c1, ref); o.puts (ref); }
	if (!(ed.pointing && ed.caret == ed.pe)) { ed.ps = ed.pe = ed.caret = ed.anchor = ed.s0 (); }
	ed.replace (ed.ps, ed.pe, o.str (), o.n);
	ed.pe = ed.ps + o.n; ed.caret = ed.anchor = ed.pe;
	ed.pointing = true; ed.pr0 = r0; ed.pc0 = c0; ed.pr1 = r1; ed.pc1 = c1;
	gv_changed ();
}
// F4: the reference at the caret cycles A1 -> $A$1 -> A$1 -> $A1.
static void gv_cycle_absolute ()
{
	EditLine &ed = g_ed;
	if (!ed.formula ()) return;
	RefHi refs[64]; int n = edit_refs (g_b, ed, refs, 64);
	for (int i = 0; i < n; i++)
	{
		if (ed.caret < refs[i].at || ed.caret > refs[i].at + refs[i].len) continue;
		// the text of the reference: each cell's part cycled
		Buf o; const char *p = ed.t.b + refs[i].at, *e = p + refs[i].len;
		const char *bang = 0; for (const char *q = p; q < e; q++) if (*q == '!') bang = q;
		if (bang) { o.putn (p, (int) (bang - p + 1)); p = bang + 1; }
		while (p < e)
		{
			const char *q = p; bool ac = false, ar = false;
			if (*q == '$') { ac = true; q++; }
			const char *ls = q; while (q < e && isalpha ((unsigned char) *q)) q++;
			const char *le = q;
			if (q < e && *q == '$') { ar = true; q++; }
			const char *ds = q; while (q < e && isdigit ((unsigned char) *q)) q++;
			const char *de = q;
			int state = ac && ar ? 1 : !ac && ar ? 2 : ac && !ar ? 3 : 0;
			state = (state + 1) % 4;
			bool nac = state == 1 || state == 3, nar = state == 1 || state == 2;
			if (le > ls) { if (nac) o.put ('$'); o.putn (ls, (int) (le - ls)); }
			if (de > ds) { if (nar) o.put ('$'); o.putn (ds, (int) (de - ds)); }
			if (q < e && *q == ':') { o.put (':'); q++; }
			if (q == p) { o.put (*q); q++; }
			p = q;
		}
		ed.replace (refs[i].at, refs[i].at + refs[i].len, o.str (), o.n);
		ed.pointing = false;
		gv_changed ();
		return;
	}
}
// The functions whose names start with what is typed (the list under the cell).
static void gv_update_autocomplete ()
{
	char w[40];
	int st = edit_word (g_ed, w, sizeof w);
	g_acN = 0;
	if (st < 0 || !w[0]) { g_acStart = -1; return; }
	int wl = (int) strlen (w);
	for (int i = 0; i < NFNS && g_acN < 12; i++)
		if ((int) strlen (FNS[i].name) > wl && ascii_ieq (FNS[i].name, w, wl)) g_acList[g_acN++] = i;
	if (!g_acN) { g_acStart = -1; return; }
	if (g_acStart != st || strcmp (g_acWord, w)) g_acSel = 0;
	g_acStart = st; scpy (g_acWord, w, sizeof g_acWord);
	if (g_acSel >= g_acN) g_acSel = 0;
}
static void gv_accept_autocomplete ()
{
	if (g_acStart < 0 || !g_acN) return;
	const char *nm = FNS[g_acList[g_acSel]].name;
	Buf o; o.puts (nm); o.put ('(');
	g_ed.replace (g_acStart, g_ed.caret, o.str (), o.n);
	g_acStart = -1;
	gv_changed ();
}
static void gv_tick ()
{
	if (!g_ed.on) return;
	unsigned t = ticks_ms ();
	if (t - g_blinkT >= 500) { g_blinkT = t; g_blink = !g_blink; ui_view_dirty (g_view); }
}
static void gv_blink_on () { g_blink = true; g_blinkT = ticks_ms (); }

// A chart's box as the pane shows it (frozen panes: the part in each one, as the cells it lies over; pane 1
// and 3 scroll across, 2 and 3 down).
static void gv_chart_box (Chart *c, int *x, int *y, int *w, int *h, int pane = 3)
{
	Sheet *s = S ();
	static PaneView pv[4]; gv_panes (pv);
	int c0 = pane & 1 ? imax (s->leftC, s->freezeC) : 0, r0 = pane & 2 ? imax (s->topR, s->freezeR) : 0;
	*x = pv[pane].x0 + (c->x - col_x (s, c0)) * g_z / 100;
	*y = pv[pane].y0 + (int) ((c->y - row_y (s, r0)) * g_z / 100);
	*w = c->w * g_z / 100; *h = c->h * g_z / 100;
}
static int gv_pane_at (int mx, int my)
{
	static PaneView pv[4]; gv_panes (pv);
	for (int k = 0; k < 3; k++) if (mx >= pv[k].x0 && mx < pv[k].x1 && my >= pv[k].y0 && my < pv[k].y1) return k;
	return 3;
}
static int gv_chart_at (int mx, int my)
{
	Sheet *s = S ();
	int p = gv_pane_at (mx, my);
	for (int i = s->ncharts - 1; i >= 0; i--)
	{
		int x, y, w, h; gv_chart_box (s->charts[i], &x, &y, &w, &h, p);
		if (mx >= x - 4 && mx < x + w + 4 && my >= y - 4 && my < y + h + 4) return i;
	}
	return -1;
}

// ---- drawing ----------------------------------------------------------------------------------------------
static void gv_draw_headers (const PaneView *pv)
{
	Sheet *s = S ();
	Rect sl = gv_sel ();
	int RW = gv_rowhead ();
	unsigned face = jet (J_CREUX), line = jet (J_LIGNE), accent = jet (J_ACCENT), text = jet (J_TEXTE1);
	unsigned hot = mixc (face, accent, 70), hotAll = jet (J_ACCENT);
	unsigned inkHot = mixc (text, accent, 120);
	Font f = ui_font (12), fb = ui_font (12, true);
	Rect clipAll = { 0, 0, g_vh - 1, g_vw - 1 };
	cv_fill (0, 0, RW, HEAD_H, face);
	{ int tri[6] = { V (RW - 4), V (HEAD_H - 4), V (RW - 4), V (HEAD_H - 14), V (RW - 14), V (HEAD_H - 4) }; ui_clip (0, 0, 0, 0); ui_vp_poly ((long) tri, 3, jet (J_LIGNE_FORTE)); }
	// the column letters (panes 0/2 give the frozen columns, 1/3 the others)
	for (int pi = 0; pi < 2; pi++)
	{
		const PaneView &v = pv[pi == 0 ? 2 : 3];
		Rect clip = { 0, v.x0, HEAD_H - 1, v.x1 - 1 };
		cv_fill (v.x0, 0, v.x1 - v.x0, HEAD_H, face);
		for (int i = 0; i < v.nc; i++)
		{
			int c = v.col[i].i, x = v.col[i].at, w = v.col[i].len;
			bool in = c >= sl.c0 && c <= sl.c1;
			bool all = in && g_wholeCols;
			if (in) { int X0 = imax (x, clip.c0), X1 = imin (x + w, clip.c1 + 1); if (X1 > X0) cv_fill (X0, 0, X1 - X0, HEAD_H, all ? hotAll : hot); }
			if (in && !all) { hline (x, x + w - 1, HEAD_H - 2, accent, BS_THIN, clip); hline (x, x + w - 1, HEAD_H - 1, accent, BS_THIN, clip); }
			vline (x + w - 1, 3, HEAD_H - 3, line, BS_THIN, clip);
			char t[8]; col_name (c, t);
			unsigned ink = all ? 0xFFFFFF : in ? inkHot : text;
			text_at (in ? fb : f, x + w / 2, HEAD_H / 2 + 5, t, ink, 1, clip);
		}
	}
	hline (0, g_vw - 1, HEAD_H - 1, line, BS_THIN, clipAll);
	// the row numbers
	for (int pi = 0; pi < 2; pi++)
	{
		const PaneView &v = pv[pi == 0 ? 1 : 3];
		Rect clip = { v.y0, 0, v.y1 - 1, RW - 1 };
		cv_fill (0, v.y0, RW, v.y1 - v.y0, face);
		for (int i = 0; i < v.nr; i++)
		{
			int r = v.row[i].i, y = v.row[i].at, h = v.row[i].len;
			bool in = r >= sl.r0 && r <= sl.r1;
			bool all = in && g_wholeRows;
			if (in) { int Y0 = imax (y, clip.r0), Y1 = imin (y + h, clip.r1 + 1); if (Y1 > Y0) cv_fill (0, Y0, RW, Y1 - Y0, all ? hotAll : hot); }
			if (in && !all) { vline (RW - 2, y, y + h - 1, accent, BS_THIN, clip); vline (RW - 1, y, y + h - 1, accent, BS_THIN, clip); }
			hline (3, RW - 4, y + h - 1, line, BS_THIN, clip);
			char t[12]; snprintf (t, sizeof t, "%d", r + 1);
			unsigned ink = all ? 0xFFFFFF : in ? inkHot : text;
			if (!all && !in && s->af.on && s->af.n && r > s->af.r.r0 && r <= s->af.r.r1) ink = jet (J_ACCENT_VIF);	// (a filtered table's rows: blue, as Excel shows them)
			text_at (in ? fb : f, RW / 2, y + h / 2 + 5, t, ink, 1, clip);
		}
	}
	vline (RW - 1, 0, g_vh - SB - 1, line, BS_THIN, clipAll);
}

static void frame2 (int x0, int y0, int x1, int y1, unsigned col, int bs, const Rect &clip)
{
	for (int t = 0; t < 2; t++)
	{
		hline (x0 - 1, x1 - 1, y0 - 1 + t, col, bs, clip); hline (x0 - 1, x1 - 1, y1 - 2 + t, col, bs, clip);
		vline (x0 - 1 + t, y0 - 1, y1 - 1, col, bs, clip); vline (x1 - 2 + t, y0 - 1, y1 - 1, col, bs, clip);
	}
}
static void gv_draw_selection (const PaneView &v)
{
	Sheet *s = S ();
	Rect clip = { v.y0, v.x0, v.y1 - 1, v.x1 - 1 };
	if (v.nc == 0 || v.nr == 0) return;
	unsigned accent = jet (J_ACCENT);
	// the references of a formula being typed: framed in their colours
	if (g_ed.on && g_ed.formula ())
	{
		RefHi refs[64]; int n = edit_refs (g_b, g_ed, refs, 64);
		for (int i = 0; i < n; i++)
		{
			if (refs[i].sheetId != s->id) continue;
			Rect r = refs[i].r;
			if (r.r1 < v.row[0].i || r.r0 > v.row[v.nr - 1].i + 1 || r.c1 < v.col[0].i || r.c0 > v.col[v.nc - 1].i + 1) continue;
			int x0 = pane_col_x (s, g_z, v, r.c0), x1 = pane_col_x (s, g_z, v, imin (r.c1, MAXC - 1) + 1);
			int y0 = pane_row_y (s, g_z, v, r.r0), y1 = pane_row_y (s, g_z, v, imin (r.r1, MAXR - 1) + 1);
			unsigned col = refs[i].col;
			int X0 = imax (x0, clip.c0), X1 = imin (x1, clip.c1 + 1), Y0 = imax (y0, clip.r0), Y1 = imin (y1, clip.r1 + 1);
			if (X1 > X0 && Y1 > Y0) cv_tint (X0, Y0, X1 - X0, Y1 - Y0, col, 28);
			frame2 (x0, y0, x1, y1, col, BS_THIN, clip);
		}
	}
	// the copied range's dashes
	if (g_copyMarquee && g_copySheet == s->id)
	{
		Rect r = g_copyRect;
		int x0 = pane_col_x (s, g_z, v, r.c0), x1 = pane_col_x (s, g_z, v, imin (r.c1, MAXC - 1) + 1);
		int y0 = pane_row_y (s, g_z, v, r.r0), y1 = pane_row_y (s, g_z, v, imin (r.r1, MAXR - 1) + 1);
		frame2 (x0, y0, x1, y1, accent, BS_DASHED, clip);
	}
	if (g_ed.on && g_ed.pointing) return;				// (the pointed range shows as a reference)
	Rect sl = gv_sel ();
	int x0 = pane_col_x (s, g_z, v, sl.c0), x1 = pane_col_x (s, g_z, v, imin (sl.c1, MAXC - 1) + 1);
	int y0 = pane_row_y (s, g_z, v, sl.r0), y1 = pane_row_y (s, g_z, v, imin (sl.r1, MAXR - 1) + 1);
	if (g_wholeCols) { y0 = v.y0 - 2; y1 = v.y1 + 2; }
	if (g_wholeRows) { x0 = v.x0 - 2; x1 = v.x1 + 2; }
	// the range tinted but the cursor's cell
	int cx, cy, cw, ch;
	bool curShown = gv_cell_box (s->curR, s->curC, &cx, &cy, &cw, &ch);
	bool multi = sl.r1 > sl.r0 || sl.c1 > sl.c0;
	if (multi && !g_ed.on)
	{
		int X0 = imax (x0, clip.c0), X1 = imin (x1, clip.c1 + 1), Y0 = imax (y0, clip.r0), Y1 = imin (y1, clip.r1 + 1);
		if (X1 > X0)
			for (int yy = Y0; yy < Y1; yy++)
			{
				if (curShown && yy >= cy && yy < cy + ch)
				{
					cv_tint (X0, yy, imin (cx, X1) - X0, 1, accent, 46);
					int a = imax (cx + cw, X0);
					if (a < X1) cv_tint (a, yy, X1 - a, 1, accent, 46);
				}
				else cv_tint (X0, yy, X1 - X0, 1, accent, 46);
			}
	}
	// the frame (2 px) and the fill handle
	frame2 (x0, y0, x1, y1, accent, BS_THIN, clip);
	if (!g_ed.on && !g_wholeCols && !g_wholeRows)
	{
		int hx = x1 - 4, hy = y1 - 4;
		for (int yy = hy; yy < hy + 7; yy++) for (int xx = hx; xx < hx + 7; xx++)
			if (xx >= clip.c0 && xx <= clip.c1 && yy >= clip.r0 && yy <= clip.r1) g_cv.px[yy * g_cv.w + xx] = (yy == hy || yy == hy + 6 || xx == hx || xx == hx + 6) ? 0xFFFFFF : accent;
	}
	// the fill handle dragged: the range it will fill
	if (g_drag == D_FILL && g_fillTo.r1 >= g_fillTo.r0)
	{
		Rect r = g_fillTo;
		int a0 = pane_col_x (s, g_z, v, r.c0), a1 = pane_col_x (s, g_z, v, r.c1 + 1), b0 = pane_row_y (s, g_z, v, r.r0), b1 = pane_row_y (s, g_z, v, r.r1 + 1);
		hline (a0 - 1, a1 - 1, b0 - 1, 0x606060, BS_DASHED, clip); hline (a0 - 1, a1 - 1, b1 - 1, 0x606060, BS_DASHED, clip);
		vline (a0 - 1, b0 - 1, b1 - 1, 0x606060, BS_DASHED, clip); vline (a1 - 1, b0 - 1, b1 - 1, 0x606060, BS_DASHED, clip);
	}
}

static void gv_draw_charts (const PaneView &v, int pane)
{
	Sheet *s = S ();
	for (int i = 0; i < s->ncharts; i++)
	{
		Chart *c = s->charts[i];
		int x, y, w, h; gv_chart_box (c, &x, &y, &w, &h, pane);
		int X0 = imax (x, v.x0), Y0 = imax (y, v.y0), X1 = imin (x + w, v.x1), Y1 = imin (y + h, v.y1);
		if (X1 <= X0 || Y1 <= Y0) continue;
		draw_chart (g_b, c, x, y, w, h, g_z, Rect { Y0, X0, Y1 - 1, X1 - 1 });
	}
}
// The chart chosen: its frame and handles; the charts stay over the selection (their pixels back from the copy).
static void gv_charts_over (const PaneView &v, int pane)
{
	Sheet *s = S ();
	Rect clip = { v.y0, v.x0, v.y1 - 1, v.x1 - 1 };
	unsigned accent = jet (J_ACCENT);
	for (int i = 0; i < s->ncharts; i++)
	{
		Chart *c = s->charts[i];
		int x, y, w, h; gv_chart_box (c, &x, &y, &w, &h, pane);
		int X0 = imax (x, v.x0), Y0 = imax (y, v.y0), X1 = imin (x + w, v.x1), Y1 = imin (y + h, v.y1);
		if (X1 <= X0 || Y1 <= Y0) continue;
		for (int yy = Y0; yy < Y1; yy++) memcpy (g_cv.px + yy * g_cv.w + X0, g_layer + yy * g_cv.w + X0, (X1 - X0) * 4);
		if (i != g_selChart) continue;
		for (int t = 0; t < 2; t++)
		{
			hline (x - 1 - t, x + w + t, y - 1 - t, accent, BS_THIN, clip); hline (x - 1 - t, x + w + t, y + h + t, accent, BS_THIN, clip);
			vline (x - 1 - t, y - 1 - t, y + h + t, accent, BS_THIN, clip); vline (x + w + t, y - 1 - t, y + h + t, accent, BS_THIN, clip);
		}
		int hx[4] = { x - 4, x + w - 3, x - 4, x + w - 3 }, hy[4] = { y - 4, y - 4, y + h - 3, y + h - 3 };
		for (int k = 0; k < 4; k++)
			for (int yy = hy[k]; yy < hy[k] + 8; yy++) for (int xx = hx[k]; xx < hx[k] + 8; xx++)
				if (xx >= clip.c0 && xx <= clip.c1 && yy >= clip.r0 && yy <= clip.r1) g_cv.px[yy * g_cv.w + xx] = (yy == hy[k] || yy == hy[k] + 7 || xx == hx[k] || xx == hx[k] + 7) ? accent : 0xFFFFFF;
	}
}

static void panel_in (int x, int y, int w, int h, int r, unsigned face)
{
	ui_clip (0, 0, 0, 0);
	ui_rbox (x, y, w, h, r, jet (J_LIGNE_FORTE), jet (J_LIGNE_FORTE));
	ui_rbox (x + 1, y + 1, w - 2, h - 2, r - 1, face, face);
}
static void gv_draw_editor ()
{
	EditLine &ed = g_ed;
	if (!ed.on) return;
	Sheet *s = book_sheet_by_id (g_b, ed.sheetId);
	if (!s || s != S ()) return;
	int x, y, w, h;
	if (!gv_cell_box (ed.r, ed.c, &x, &y, &w, &h)) return;
	Cell *cell = s->cells.get (ed.r, ed.c);
	const Style &st = cell_style (g_b, s, cell, ed.r, ed.c);
	Font f = style_font (g_b, st, g_z);
	const char *t = ed.t.str (); int n = ed.t.n;
	// its lines (line breaks typed), the box grown to them
	int ls[64], le[64], nl = 0;
	for (int i = 0; i <= n && nl < 64; ) { int j = i; while (j < n && t[j] != '\n') j++; ls[nl] = i; le[nl] = j; nl++; if (j >= n) break; i = j + 1; }
	int tw = 0; for (int i = 0; i < nl; i++) tw = imax (tw, u8_width (f, t + ls[i], le[i] - ls[i]));
	int lh = f.lh;
	int bw = imax (w, tw + 10), bh = imax (h, nl * lh + 6);
	int maxW = g_vw - SB - x - 2;
	if (bw > maxW) bw = imax (w, maxW);
	Rect clip = { imax (y - 1, HEAD_H), imax (x - 1, gv_rowhead ()), imin (y + bh + 1, g_vh - SB - 1), imin (x + bw + 1, g_vw - SB - 1) };
	unsigned accent = jet (J_ACCENT);
	cv_fill (clip.c0, clip.r0, clip.c1 - clip.c0 + 1, clip.r1 - clip.r0 + 1, st.fill == AUTO ? 0xFFFFFF : st.fill);
	for (int k = 0; k < 2; k++)
	{
		hline (x - 1 - k, x + bw + k, y - 1 - k, accent, BS_THIN, clip); hline (x - 1 - k, x + bw + k, y + bh + k, accent, BS_THIN, clip);
		vline (x - 1 - k, y - 1 - k, y + bh + k, accent, BS_THIN, clip); vline (x + bw + k, y - 1 - k, y + bh + k, accent, BS_THIN, clip);
	}
	// the text: the references in their colours, the selection, the caret
	RefHi refs[64]; int nr = edit_refs (g_b, ed, refs, 64);
	unsigned ink = st.color == AUTO ? 0 : st.color;
	int asc = f.asc;
	int base0 = y + 3 + asc;
	if (nl == 1 && bh == h && st.va == VA_BOTTOM) base0 = y + h - 3 - f.desc;
	bool caretNow = g_blink && ui_w_get_focus () == g_view;
	for (int li = 0; li < nl; li++)
	{
		int base = base0 + li * lh, x0 = x + 4;
		// the selection under
		int a = imax (ed.s0 (), ls[li]), b = imin (ed.s1 (), le[li]);
		if (b > a) { int X0 = x0 + u8_width (f, t + ls[li], a - ls[li]), X1 = x0 + u8_width (f, t + ls[li], b - ls[li]); for (int yy = imax (base - asc, clip.r0); yy < imin (base - asc + lh, clip.r1 + 1); yy++) cv_fill (imax (X0, clip.c0), yy, imin (X1, clip.c1 + 1) - imax (X0, clip.c0), 1, 0xB9D4F5); }
		// runs of one colour
		for (int i = ls[li]; i < le[li]; )
		{
			unsigned c = ink; int run = le[li];
			for (int k = 0; k < nr; k++)
			{
				int ra = refs[k].at, rb = refs[k].at + refs[k].len;
				if (i >= ra && i < rb) { c = refs[k].col; run = imin (run, rb); }
				else if (ra > i && ra < run) run = ra;
			}
			u8_draw (f, x0 + u8_width (f, t + ls[li], i - ls[li]), base, t + i, run - i, c, clip.c0, clip.r0, clip.c1 + 1, clip.r1 + 1);
			i = run;
		}
		if (caretNow && ed.caret >= ls[li] && ed.caret <= le[li]) vline (x0 + u8_width (f, t + ls[li], ed.caret - ls[li]), base - asc, base - asc + lh - 1, 0, BS_THIN, clip);
	}
	// the function's arguments (a tip below), or the functions offered
	int ay = y + bh + 4;
	int arg = 0, fi = edit_function (ed, &arg);
	Font uf = ui_font (12), ub = ui_font (12, true);
	unsigned tipFace = jet (J_ELEVE), tipInk = jet (J_TEXTE1);
	if (g_acStart >= 0 && g_acN)
	{
		int rowH = 20, lw = 0;
		for (int i = 0; i < g_acN; i++) lw = imax (lw, s_width (ub, FNS[g_acList[i]].name));
		int bw2 = imax (lw + 24, 200), bh2 = g_acN * rowH + 8;
		int bx = x;
		if (ay + bh2 > g_vh - SB) ay = y - bh2 - 4;
		if (bx + bw2 > g_vw - SB) bx = g_vw - SB - bw2;
		panel_in (bx, ay, bw2, bh2, 6, tipFace);
		Rect c2 = { ay, bx, ay + bh2 - 1, bx + bw2 - 1 };
		for (int i = 0; i < g_acN; i++)
		{
			int yy = ay + 4 + i * rowH;
			bool hot = i == g_acSel;
			if (hot) { ui_clip (0, 0, 0, 0); ui_rbox (bx + 4, yy, bw2 - 8, rowH, 4, accent, accent); }
			text_at (ub, bx + 10, yy + 15, FNS[g_acList[i]].name, hot ? 0xFFFFFF : tipInk, 0, c2);
		}
		if (g_acSel < g_acN)					// (what it does, beside)
		{
			const FnDef &d = FNS[g_acList[g_acSel]];
			const char *help = TR (d.help);
			int tw2 = imin (s_width (uf, help) + 16, g_vw - SB - 8);
			int tx = bx + bw2 + 4; if (tx + tw2 > g_vw - SB) tx = imax (gv_rowhead (), bx - tw2 - 4);
			panel_in (tx, ay, tw2, 24, 5, tipFace);
			text_at (uf, tx + 8, ay + 16, help, tipInk, 0, Rect { ay, tx, ay + 23, tx + tw2 - 2 });
		}
	}
	else if (fi >= 0)
	{
		const FnDef &d = FNS[fi];
		// "SUM(number1, [number2], ...)" with the current argument in bold
		Buf full; full.puts (d.name); full.put ('(');
		const char *fa = TR (d.args);			// (its names translated: the same commas)
		int an = (int) strlen (fa), k = 0, cur0 = -1, cur1 = -1, ai = 0;
		for (int i = 0; i <= an; i++)
		{
			if (i == an || fa[i] == ',')
			{
				int s0 = full.n;
				int a0 = k; while (a0 < i && fa[a0] == ' ') a0++;
				full.putn (fa + a0, i - a0);
				bool last = i == an || strstr (fa + i, "...") != 0;
				if (ai == arg || (last && arg > ai && strstr (fa + a0, "..."))) { cur0 = s0; cur1 = full.n; }
				if (i < an) full.puts (", ");
				k = i + 1; ai++;
			}
		}
		full.put (')');
		int tw3 = u8_width (uf, full.str (), full.n) + 20;
		int tx = x; if (tx + tw3 > g_vw - SB) tx = imax (gv_rowhead (), g_vw - SB - tw3);
		if (ay + 24 > g_vh - SB) ay = y - 28;
		panel_in (tx, ay, tw3, 24, 5, tipFace);
		int X1 = tx + tw3 - 2, xx = tx + 10;
		if (cur0 < 0) { cur0 = cur1 = full.n; }
		u8_draw (uf, xx, ay + 16, full.b, cur0, tipInk, tx, ay, X1, ay + 24); xx += u8_width (uf, full.b, cur0);
		u8_draw (ub, xx, ay + 16, full.b + cur0, cur1 - cur0, tipInk, tx, ay, X1, ay + 24); xx += u8_width (ub, full.b + cur0, cur1 - cur0);
		u8_draw (uf, xx, ay + 16, full.b + cur1, full.n - cur1, tipInk, tx, ay, X1, ay + 24);
	}
}

// The view's drawing: the cells only when they changed -- a move of the cursor is their copy, the selection and
// the headers over it.
static long gv_paint (long v, long w, long h)
{
	(void) w; (void) h;
	cv_begin (v);
	Sheet *s = S ();
	static PaneView pv[4]; gv_panes (pv);
	int n = g_cv.w * g_cv.h;
	if (g_layerW != g_cv.w || g_layerH != g_cv.h)
	{
		free (g_layer); g_layer = (uint32_t *) malloc (n * 4 + 4); g_layerW = g_cv.w; g_layerH = g_cv.h; g_cellsDirty = true;
	}
	if (g_lastTop != s->topR || g_lastLeft != s->leftC || g_lastZ != g_z || g_lastSheet != s->id) g_cellsDirty = true;
	if (g_cellsDirty || !g_layer)
	{
		g_cellsDirty = false;
		g_lastTop = s->topR; g_lastLeft = s->leftC; g_lastZ = g_z; g_lastSheet = s->id;
		cv_fill (0, 0, g_cv.w, g_cv.h, 0xFFFFFF);
		for (int p = 0; p < 4; p++) if (pv[p].x1 > pv[p].x0 && pv[p].y1 > pv[p].y0) paint_pane (g_b, s, g_z, pv[p], s->grid);
		for (int p = 0; p < 4; p++) if (pv[p].x1 > pv[p].x0 && pv[p].y1 > pv[p].y0) gv_draw_charts (pv[p], p);
		// the AutoFilter's buttons (a column filtered: the accent's)
		if (s->af.on)
			for (int c = s->af.r.c0; c <= s->af.r.c1; c++)
			{
				int bx, by, bs;
				if (!gv_filter_button (c, &bx, &by, &bs)) continue;
				bool on = af_index (s, c) >= 0;
				ui_clip (0, 0, 0, 0);
				ui_rbox (bx, by, bs, bs, 3, on ? 0x4472C4 : 0xE8E8E8, on ? 0x4472C4 : 0xD0D0D0);
				int tri[6] = { V (bx + bs / 4), V (by + bs / 3), V (bx + bs - bs / 4), V (by + bs / 3), V (bx + bs / 2), V (by + bs - bs / 3) };
				ui_vp_poly ((long) tri, 3, on ? 0xFFFFFF : 0x404040);
			}
		// the frozen panes' edges
		Rect all = { 0, 0, g_cv.h - 1, g_cv.w - 1 };
		if (s->freezeC) vline (pv[1].x0 - 1, HEAD_H, g_cv.h - SB - 1, 0x9AA0A6, BS_THIN, all);
		if (s->freezeR) hline (gv_rowhead (), g_cv.w - SB - 1, pv[2].y0 - 1, 0x9AA0A6, BS_THIN, all);
		if (g_layer) memcpy (g_layer, g_cv.px, n * 4);
	}
	else memcpy (g_cv.px, g_layer, n * 4);
	for (int p = 0; p < 4; p++) if (pv[p].x1 > pv[p].x0 && pv[p].y1 > pv[p].y0) gv_draw_selection (pv[p]);
	if (g_layer && s->ncharts) for (int p = 0; p < 4; p++) if (pv[p].x1 > pv[p].x0 && pv[p].y1 > pv[p].y0) gv_charts_over (pv[p], p);
	gv_draw_headers (pv);
	gv_draw_editor ();
	// where the scroll bars lie
	cv_fill (g_cv.w - SB, 0, SB, g_cv.h, jet (J_FACE));
	cv_fill (0, g_cv.h - SB, g_cv.w, SB, jet (J_FACE));
	cv_end ();
	return 0;
}

// ---- the keys ---------------------------------------------------------------------------------------------
// Ctrl + an arrow: to the edge of the data (the last filled cell before an empty one, or the next filled).
static void gv_data_edge (int *r, int *c, int dr, int dc)
{
	Sheet *s = S ();
	sheet_bounds (s);
	auto filled = [&] (int rr, int cc) { Cell *x = s->cells.get (rr, cc); return x && x->kind != K_NONE; };
	int rr = *r, cc = *c;
	int limR = dr > 0 ? imax (s->maxR, rr) : 0, limC = dc > 0 ? imax (s->maxC, cc) : 0;
	bool here = filled (rr, cc), nextF = filled (iclamp (rr + dr, 0, MAXR - 1), iclamp (cc + dc, 0, MAXC - 1));
	if (here && nextF)
	{
		while (true)
		{
			int nr = rr + dr, nc = cc + dc;
			if (nr < 0 || nc < 0 || nr >= MAXR || nc >= MAXC || !filled (nr, nc)) break;
			rr = nr; cc = nc;
		}
	}
	else
	{
		while (true)
		{
			int nr = rr + dr, nc = cc + dc;
			if (nr < 0 || nc < 0 || nr >= MAXR || nc >= MAXC) break;
			rr = nr; cc = nc;
			if (filled (rr, cc)) break;
			if ((dr > 0 && rr > limR) || (dc > 0 && cc > limC)) { rr = dr > 0 ? MAXR - 1 : rr; cc = dc > 0 ? MAXC - 1 : cc; break; }
		}
	}
	*r = rr; *c = cc;
}
static void gv_key_move (int dr, int dc, bool shift, bool ctrl)
{
	Sheet *s = S ();
	int r = s->curR, c = s->curC;
	if (ctrl) gv_data_edge (&r, &c, dr, dc);
	else
	{
		// past a merge and the hidden rows / columns
		int mi = merge_at (s, r, c);
		if (mi >= 0 && !shift) { if (dr > 0) r = s->merges[mi].r1; if (dc > 0) c = s->merges[mi].c1; if (dr < 0) r = s->merges[mi].r0; if (dc < 0) c = s->merges[mi].c0; }
		r += dr; c += dc;
		while (dr && r > 0 && r < MAXR - 1 && row_h (s, r) == 0) r += dr;
		while (dc && c > 0 && c < MAXC - 1 && col_w (s, c) == 0) c += dc;
	}
	if (shift) { if (g_wholeCols) c = iclamp (c, 0, MAXC - 1); gv_select_cell (r, c, true); }
	else gv_select_cell (r, c, false);
}
enum { HID_F2 = 0x3B, HID_F4 = 0x3D, HID_A = 0x04 };
// The grid's keys, as they come from ui_w_event. -1: not the grid's (the menus' shortcuts, Ctrl+1...: main.cpp).
static long gv_key (long v, long hid, long ch, long m)
{
	(void) v;
	Sheet *s = S ();
	int mods = (int) m;
	bool shift = mods & UI_MOD_SHIFT, ctrl = mods & UI_MOD_CTRL, alt = mods & 0x04;
	if (hid >= 0xE0) return -1;				// (a modifier alone)
	if (g_selChart >= 0 && !g_ed.on)
	{
		if (hid == KEY_DELETE || hid == KEY_BACKSPACE) { grid_context (-1, -1, 4); return 0; }	// (the app deletes it)
		if (hid == KEY_ESC) { g_selChart = -1; gv_changed (); return 0; }
		if (hid == KEY_ENTER) { grid_chart_open (g_selChart); return 0; }
	}
	EditLine &ed = g_ed;
	if (ed.on)
	{
		gv_blink_on ();
		if (g_acStart >= 0 && g_acN)
		{
			if (hid == KEY_DOWN) { g_acSel = (g_acSel + 1) % g_acN; ui_view_dirty (g_view); return 0; }
			if (hid == KEY_UP) { g_acSel = (g_acSel + g_acN - 1) % g_acN; ui_view_dirty (g_view); return 0; }
			if (hid == KEY_TAB || (hid == KEY_ENTER && !shift)) { gv_accept_autocomplete (); return 0; }
			if (hid == KEY_ESC) { g_acStart = -1; ui_view_dirty (g_view); return 0; }
		}
		switch (hid)
		{
		case KEY_ENTER:
			if (alt) { ed.insert ("\n", 1); gv_changed (); return 0; }
			gv_commit (shift ? -1 : 1, 0); return 0;
		case KEY_TAB: gv_commit (0, shift ? -1 : 1); return 0;
		case KEY_ESC: gv_cancel_edit (); return 0;
		case HID_F2: ed.typed = !ed.typed; ed.pointing = false; gv_changed (); return 0;
		case HID_F4: gv_cycle_absolute (); return 0;
		case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT:
		{
			int dr = hid == KEY_UP ? -1 : hid == KEY_DOWN ? 1 : 0, dc = hid == KEY_LEFT ? -1 : hid == KEY_RIGHT ? 1 : 0;
			if (ed.typed && ed.can_point ())
			{
				int r = ed.pointing ? ed.pr1 : ed.r, c = ed.pointing ? ed.pc1 : ed.c;
				int r0 = ed.pointing ? ed.pr0 : ed.r, c0 = ed.pointing ? ed.pc0 : ed.c;
				if (ctrl) gv_data_edge (&r, &c, dr, dc);
				else { r = iclamp (r + dr, 0, MAXR - 1); c = iclamp (c + dc, 0, MAXC - 1); }
				if (shift && ed.pointing) gv_point_at (r0, c0, r, c); else gv_point_at (r, c, r, c);
				gv_ensure_visible (r, c);
				return 0;
			}
			if (ed.typed) { gv_commit (dr, dc); return 0; }
			if (dc)
			{
				int to = dc < 0 ? (ctrl ? ed.word_left (ed.caret) : ed.prev (ed.caret)) : (ctrl ? ed.word_right (ed.caret) : ed.next (ed.caret));
				if (!shift && ed.s0 () != ed.s1 ()) to = dc < 0 ? ed.s0 () : ed.s1 ();
				ed.caret = to; if (!shift) ed.anchor = to;
			}
			else
			{
				// up / down between the lines of a text with line breaks
				int ls = ed.caret; while (ls > 0 && ed.t.b[ls - 1] != '\n') ls--;
				int col = ed.caret - ls;
				if (dr < 0 && ls > 0) { int ps = ls - 1; while (ps > 0 && ed.t.b[ps - 1] != '\n') ps--; ed.caret = imin (ps + col, ls - 1); }
				else if (dr > 0) { int e = ed.caret; while (e < ed.t.n && ed.t.b[e] != '\n') e++; if (e < ed.t.n) { int ne = e + 1; while (ne < ed.t.n && ed.t.b[ne] != '\n') ne++; ed.caret = imin (e + 1 + col, ne); } }
				if (!shift) ed.anchor = ed.caret;
			}
			ed.pointing = false;
			gv_changed ();
			return 0;
		}
		case KEY_HOME: ed.caret = 0; if (!shift) ed.anchor = 0; ed.pointing = false; gv_changed (); return 0;
		case KEY_END: ed.caret = ed.t.n; if (!shift) ed.anchor = ed.t.n; ed.pointing = false; gv_changed (); return 0;
		case KEY_BACKSPACE: ed.backspace (); gv_update_autocomplete (); gv_changed (); return 0;
		case KEY_DELETE: ed.del (); gv_update_autocomplete (); gv_changed (); return 0;
		}
		if (ctrl && hid == HID_A) { ed.anchor = 0; ed.caret = ed.t.n; gv_changed (); return 0; }
		if (ctrl) return -1;
		if (ch >= 32 && ch != 127)
		{
			ed.insert_char ((unsigned) ch);
			gv_update_autocomplete ();
			gv_changed ();
			return 0;
		}
		return 0;
	}
	// ---- not editing
	switch (hid)
	{
	case KEY_UP: gv_key_move (-1, 0, shift, ctrl); return 0;
	case KEY_DOWN: gv_key_move (1, 0, shift, ctrl); return 0;
	case KEY_LEFT: gv_key_move (0, -1, shift, ctrl); return 0;
	case KEY_RIGHT: gv_key_move (0, 1, shift, ctrl); return 0;
	case KEY_HOME:
		if (ctrl) { gv_select_cell (0, 0, shift); s->topR = s->freezeR; s->leftC = s->freezeC; ui_view_dirty (g_view); }
		else gv_select_cell (s->curR, 0, shift);
		return 0;
	case KEY_END:
		if (ctrl) { sheet_bounds (s); gv_select_cell (imax (0, s->maxR), imax (0, s->maxC), shift); }
		else { int r = s->curR, c = s->curC; gv_data_edge (&r, &c, 0, 1); gv_select_cell (r, c, shift); }
		return 0;
	case KEY_PGUP: case KEY_PGDN:
	{
		int d = hid == KEY_PGUP ? -1 : 1;
		if (ctrl) { grid_context (d, 0, 5); return 0; }		// (the previous / next sheet: the app)
		if (alt) { int n = gv_vis_cols (); gv_scroll_by (0, d * n); gv_select_cell (s->curR, iclamp (s->curC + d * n, 0, MAXC - 1), shift); return 0; }
		int n = gv_vis_rows ();
		gv_scroll_by (d * n, 0);
		gv_select_cell (iclamp (s->curR + d * n, 0, MAXR - 1), s->curC, shift);
		return 0;
	}
	case KEY_ENTER: case KEY_TAB:
	{
		Rect sl = gv_sel ();
		bool down = hid == KEY_ENTER;
		int dr = down ? (shift ? -1 : 1) : 0, dc = down ? 0 : (shift ? -1 : 1);
		if ((sl.r1 > sl.r0 || sl.c1 > sl.c0) && !g_wholeCols && !g_wholeRows)
		{
			int r = s->curR + dr, c = s->curC + dc;
			if (r > sl.r1) { r = sl.r0; c++; }
			if (r < sl.r0) { r = sl.r1; c--; }
			if (c > sl.c1) { c = sl.c0; r++; if (r > sl.r1) r = sl.r0; }
			if (c < sl.c0) { c = sl.c1; r--; if (r < sl.r0) r = sl.r1; }
			s->curR = iclamp (r, sl.r0, sl.r1); s->curC = iclamp (c, sl.c0, sl.c1);
			gv_ensure_visible (s->curR, s->curC); gv_changed ();
		}
		else
		{
			int tabC = g_tabC;
			if (!down && tabC < 0) tabC = s->curC;
			if (down && dr > 0 && tabC >= 0) gv_select_cell (s->curR + 1, tabC, false);
			else gv_key_move (dr, dc, false, false);
			g_tabC = down ? -1 : tabC;
		}
		return 0;
	}
	case HID_F2: gv_begin_edit (false, 0); return 0;
	case KEY_ESC: if (g_copyMarquee) { g_copyMarquee = false; gv_changed (); } return 0;
	}
	if (hid == 0x2C && ctrl) { Rect sl = gv_sel (); gv_select_cols (sl.c0, sl.c1); return 0; }	// (Ctrl + space)
	if (hid == 0x2C && shift) { Rect sl = gv_sel (); gv_select_rows (sl.r0, sl.r1); return 0; }	// (Shift + space)
	if (hid == KEY_BACKSPACE) { gv_begin_edit (true, ""); return 0; }
	if (ctrl) return -1;
	if (ch >= 32 && ch != 127)
	{
		char t[5]; int n = u8_enc ((unsigned) ch, t); t[n] = 0;
		gv_begin_edit (true, t);
		gv_update_autocomplete ();
		return 0;
	}
	return -1;
}

// ---- the mouse ----------------------------------------------------------------------------------------------
static void gv_set_cursor (int c) { if (c != g_cursor) { g_cursor = c; ui_cursor (c); } }
// The pointer's shape: what a press here would do.
static void gv_hover (int mx, int my)
{
	int RW = gv_rowhead ();
	int shape = UC_ARROW;
	static PaneView pv[4];
	if (my < HEAD_H && mx >= RW)				// a column's edge
	{
		gv_panes (pv);
		for (int p = 2; p <= 3; p++)
			for (int i = 0; i < pv[p].nc; i++)
				if (abs (mx - (pv[p].col[i].at + pv[p].col[i].len)) <= 3) shape = UC_HRESIZE;
	}
	else if (mx < RW && my >= HEAD_H)			// a row's edge
	{
		gv_panes (pv);
		for (int p = 1; p <= 3; p += 2)
			for (int i = 0; i < pv[p].nr; i++)
				if (abs (my - (pv[p].row[i].at + pv[p].row[i].len)) <= 2) shape = UC_VRESIZE;
	}
	else if (mx >= RW && my >= HEAD_H && mx < g_vw - SB && my < g_vh - SB)
	{
		Rect sl = gv_sel ();				// the fill handle, a chart, else the cells
		int x0, y0, w0, h0;
		if (!g_wholeCols && !g_wholeRows && gv_cell_box (sl.r1, sl.c1, &x0, &y0, &w0, &h0)
		    && abs (mx - (x0 + w0 - 1)) <= 4 && abs (my - (y0 + h0 - 1)) <= 4) shape = UC_CROSS;
		else if (gv_chart_at (mx, my) >= 0) shape = UC_DRAG;
	}
	gv_set_cursor (shape);
}
static void gv_drag_move (int mx, int my)
{
	Sheet *s = S ();
	int RW = gv_rowhead ();
	int r, c;
	int cx = iclamp (mx, RW, g_vw - SB - 1), cy = iclamp (my, HEAD_H, g_vh - SB - 1);
	switch (g_drag)
	{
	case D_SELECT:
		if (!gv_cell_at (cx, cy, &r, &c)) break;
		if (mx >= g_vw - SB) gv_scroll_by (0, 1);
		if (my >= g_vh - SB) gv_scroll_by (1, 0);
		if (mx < RW && s->leftC > s->freezeC) gv_scroll_by (0, -1);
		if (my < HEAD_H && s->topR > s->freezeR) gv_scroll_by (-1, 0);
		if (r != s->curR || c != s->curC) { s->curR = r; s->curC = c; gv_changed (); }
		break;
	case D_POINT:
		if (!gv_cell_at (cx, cy, &r, &c)) break;
		if (r != g_ed.pr1 || c != g_ed.pc1) gv_point_at (g_ed.pr0, g_ed.pc0, r, c);
		break;
	case D_COLSEL:
		if (gv_cell_at (cx, cy, &r, &c)) { s->curC = c; gv_changed (); }
		break;
	case D_ROWSEL:
		if (gv_cell_at (cx, cy, &r, &c)) { s->curR = r; gv_changed (); }
		break;
	case D_COLSIZE:
	{
		int w = (g_dragV + (mx - g_dragX0)) * 100 / g_z;
		s->colW[g_dragI] = (unsigned short) iclamp (w, 4, 2000); s->colFl[g_dragI] &= ~RF_HIDDEN;
		cols_changed (s); gv_dirty ();
		break;
	}
	case D_ROWSIZE:
	{
		int h = (g_dragV + (my - g_dragY0)) * 100 / g_z;
		RowInfo *ri = row_add (s, g_dragI); ri->h = (unsigned short) iclamp (h, 4, 2000); ri->fl |= RF_CUSTOM; ri->fl &= ~RF_HIDDEN;
		rows_changed (s); gv_dirty ();
		break;
	}
	case D_FILL:
	{
		if (!gv_cell_at (cx, cy, &r, &c)) break;
		Rect sl = gv_sel ();
		Rect t = sl;
		int dR = r > sl.r1 ? r - sl.r1 : r < sl.r0 ? r - sl.r0 : 0, dC = c > sl.c1 ? c - sl.c1 : c < sl.c0 ? c - sl.c0 : 0;
		if (abs (dR) >= abs (dC)) { if (dR > 0) t.r1 = r; else if (dR < 0) t.r0 = r; }
		else { if (dC > 0) t.c1 = c; else if (dC < 0) t.c0 = c; }
		if (dR == 0 && dC == 0) t.r1 = t.r0 - 1;
		g_fillTo = t;
		if (mx >= g_vw - SB) gv_scroll_by (0, 1);
		if (my >= g_vh - SB) gv_scroll_by (1, 0);
		ui_view_dirty (g_view);
		break;
	}
	case D_CHART: case D_CHARTSIZE:
	{
		Chart *ch = g_selChart >= 0 && g_selChart < s->ncharts ? s->charts[g_selChart] : 0;
		if (!ch) break;
		int dx = (mx - g_dragX0) * 100 / g_z, dy = (my - g_dragY0) * 100 / g_z;
		if (!g_moved)					// (the first move: the app keeps an undo step)
		{
			if (!dx && !dy) break;
			g_moved = true;
			grid_context (0, 0, 8);
		}
		if (g_drag == D_CHART) { ch->x = imax (0, g_chartX0 + dx); ch->y = imax (0, g_chartY0 + dy); chart_anchor (s, ch); }
		else
		{
			int x0 = g_chartX0, y0 = g_chartY0, x1 = g_chartX0 + g_chartW0, y1 = g_chartY0 + g_chartH0;
			if (g_corner & 1) x1 += dx; else x0 += dx;
			if (g_corner & 2) y1 += dy; else y0 += dy;
			if (x1 - x0 < 120) { if (g_corner & 1) x1 = x0 + 120; else x0 = x1 - 120; }
			if (y1 - y0 < 90) { if (g_corner & 2) y1 = y0 + 90; else y0 = y1 - 90; }
			ch->x = imax (0, x0); ch->y = imax (0, y0); ch->w = x1 - x0; ch->h = y1 - y0;
			chart_anchor (s, ch);
		}
		gv_dirty ();
		break;
	}
	}
}
static void gv_right_press (int mx, int my)
{
	int RW = gv_rowhead ();
	if (g_ed.on) return;
	int ci = gv_chart_at (mx, my);
	if (ci >= 0) { g_selChart = ci; gv_changed (); g_rightWhere = 3; return; }
	int r, c;
	int where = my < HEAD_H && mx >= RW ? 1 : mx < RW && my >= HEAD_H ? 2 : 0;
	if (gv_cell_at (iclamp (mx, RW, g_vw - SB - 1), iclamp (my, HEAD_H, g_vh - SB - 1), &r, &c))
	{
		Rect sl = gv_sel ();
		if (where == 1 && !(g_wholeCols && c >= sl.c0 && c <= sl.c1)) gv_select_cols (c, c);
		else if (where == 2 && !(g_wholeRows && r >= sl.r0 && r <= sl.r1)) gv_select_rows (r, r);
		else if (where == 0 && !rect_has (sl, r, c)) gv_select_cell (r, c, false);
	}
	g_rightWhere = where;
}
static void gv_left_press (int mx, int my, int clicks, int mods)
{
	Sheet *s = S ();
	int RW = gv_rowhead ();
	bool dbl = clicks >= 2;
	// the corner: all
	if (mx < RW && my < HEAD_H) { if (g_ed.on && !gv_commit (0, 0)) return; gv_select_all (); return; }
	// an AutoFilter's button
	if (!g_ed.on && s->af.on)
		for (int c = s->af.r.c0; c <= s->af.r.c1; c++)
		{
			int bx, by, bs;
			if (gv_filter_button (c, &bx, &by, &bs) && mx >= bx && mx < bx + bs && my >= by && my < by + bs)
			{
				grid_filter (c, g_vx + bx, g_vy + by + bs + 1);
				return;
			}
		}
	// a chart (its handles size it)
	if (!g_ed.on)
	{
		int ci = gv_chart_at (mx, my);
		if (ci >= 0)
		{
			Chart *ch = s->charts[ci];
			int x, y, w, h; gv_chart_box (ch, &x, &y, &w, &h, gv_pane_at (mx, my));
			if (dbl && ci == g_selChart) { grid_chart_open (ci); return; }
			g_selChart = ci;
			g_dragX0 = mx; g_dragY0 = my; g_chartX0 = ch->x; g_chartY0 = ch->y; g_chartW0 = ch->w; g_chartH0 = ch->h; g_moved = false;
			int cx = mx < x + 8 ? 0 : mx > x + w - 8 ? 1 : -1, cy = my < y + 8 ? 0 : my > y + h - 8 ? 2 : -1;
			if (cx >= 0 && cy >= 0) { g_drag = D_CHARTSIZE; g_corner = cx | cy; }
			else g_drag = D_CHART;
			gv_changed ();
			return;
		}
	}
	int r, c;
	static PaneView pv[4];
	// the column letters: select (drag: several), or their edge: size
	if (my < HEAD_H)
	{
		gv_panes (pv);
		for (int p = 2; p <= 3; p++)
			for (int i = 0; i < pv[p].nc; i++)
			{
				int e = pv[p].col[i].at + pv[p].col[i].len;
				if (abs (mx - e) <= 3)
				{
					int cc = pv[p].col[i].i;
					if (dbl) { g_fitCol = cc; grid_context (0, 0, 7); return; }		// a double click: fitted
					g_drag = D_COLSIZE; g_dragI = cc; g_dragV = pv[p].col[i].len; g_dragX0 = mx;
					grid_context (0, 0, 8);				// (the app keeps an undo step)
					return;
				}
			}
		if (g_ed.on && !gv_commit (0, 0)) return;
		if (gv_cell_at (iclamp (mx, RW, g_vw - SB - 1), HEAD_H + 1, &r, &c))
		{
			if (mods & UI_MOD_SHIFT) { Rect sl = gv_sel (); gv_select_cols (imin (sl.c0, c) == c ? sl.c1 : sl.c0, c); }
			else gv_select_cols (c, c);
			g_drag = D_COLSEL;
		}
		return;
	}
	if (mx < RW)
	{
		gv_panes (pv);
		for (int p = 1; p <= 3; p += 2)
			for (int i = 0; i < pv[p].nr; i++)
			{
				int e = pv[p].row[i].at + pv[p].row[i].len;
				if (abs (my - e) <= 2)
				{
					int rr = pv[p].row[i].i;
					if (dbl) { g_fitRow = rr; grid_context (0, 0, 9); return; }
					g_drag = D_ROWSIZE; g_dragI = rr; g_dragV = pv[p].row[i].len; g_dragY0 = my;
					grid_context (0, 0, 8);
					return;
				}
			}
		if (g_ed.on && !gv_commit (0, 0)) return;
		if (gv_cell_at (RW + 1, my, &r, &c))
		{
			if (mods & UI_MOD_SHIFT) { Rect sl = gv_sel (); gv_select_rows (sl.r0, r); }
			else gv_select_rows (r, r);
			g_drag = D_ROWSEL;
		}
		return;
	}
	if (!gv_cell_at (mx, my, &r, &c)) return;
	// while a formula waits for a reference: point at the cell (drag: a range)
	if (g_ed.on && g_ed.can_point ())
	{
		gv_point_at (r, c, r, c);
		g_drag = D_POINT;
		return;
	}
	if (g_ed.on)
	{
		if (g_ed.r == r && g_ed.c == c && book_sheet_by_id (g_b, g_ed.sheetId) == s) { g_ed.typed = false; gv_changed (); return; }	// (a click in the cell: edit)
		if (!gv_commit (0, 0)) return;
	}
	// the fill handle
	{
		Rect sl = gv_sel ();
		int x0, y0, w0, h0;
		if (!g_wholeCols && !g_wholeRows && gv_cell_box (sl.r1, sl.c1, &x0, &y0, &w0, &h0))
		{
			int hx = x0 + w0 - 1, hy = y0 + h0 - 1;
			if (abs (mx - hx) <= 4 && abs (my - hy) <= 4) { g_drag = D_FILL; g_fillTo.r1 = g_fillTo.r0 - 1; return; }
		}
	}
	if (dbl) { gv_select_cell (r, c, false); gv_begin_edit (false, 0); return; }
	gv_select_cell (r, c, (mods & UI_MOD_SHIFT) != 0);
	g_drag = D_SELECT;
}
// (kind: 0 a move, 1 a press, 2 a release, 3 out; info: clicks | buttons << 8 | mods << 16)
static long gv_mouse (long v, long kind, long x, long y, long info)
{
	(void) v;
	int mx = (int) x, my = (int) y, clicks = info & 255, btn = info >> 8 & 255, mods = (int) (info >> 16);
	if (kind == 3) { if (g_drag == D_NONE) gv_set_cursor (UC_ARROW); return 0; }
	if (kind == 0)
	{
		if (g_drag != D_NONE) gv_drag_move (mx, my);
		else if (mx >= 0 && my >= 0 && mx < g_vw && my < g_vh) gv_hover (mx, my);
		return 0;
	}
	if (kind == 1)
	{
		gv_focus ();
		if (btn & 2) { gv_right_press (mx, my); return 0; }
		if (btn & 1) gv_left_press (mx, my, clicks, mods);
		return 0;
	}
	// a release: the menu of the right button (at its press it would take this release for a choice), what was dragged ends
	if (g_rightWhere >= 0) { int w = g_rightWhere; g_rightWhere = -1; grid_context (mx, my, w); return 0; }
	if (g_drag != D_NONE)
	{
		int was = g_drag;
		g_drag = D_NONE;
		if (was == D_FILL && g_fillTo.r1 >= g_fillTo.r0) { Rect t = g_fillTo; g_fillTo.r1 = g_fillTo.r0 - 1; g_fillRect = t; grid_context (0, 0, 6); }	// (the app fills)
		if (was == D_COLSIZE || was == D_ROWSIZE || was == D_CHART || was == D_CHARTSIZE) grid_edited ();
		gv_changed ();
	}
	return 0;
}
static long gv_wheel (long v, long n)
{
	(void) v;
	int mods = ui_ev_field (&ui, EV_MODS);
	if (mods & UI_MOD_CTRL) { grid_zoom (n > 0 ? 1 : -1); return 0; }
	if (mods & UI_MOD_SHIFT) gv_scroll_by (0, (int) -n * 2); else gv_scroll_by ((int) -n * 3, 0);
	return 0;
}

// The view and its bars, at (x, y) in the window.
static void gv_create (int x, int y, int w, int h)
{
	g_vx = x; g_vy = y; g_vw = w; g_vh = h;
	g_view = ui_view (x, y, w, h, UVW_FOCUS);
	ui_view_on (g_view, (long) gv_paint, (long) gv_mouse, (long) gv_key, (long) gv_wheel);
	g_vbar = ui_scrollbar (x + w - SB + 1, y + HEAD_H, SB - 2, h - HEAD_H - SB, 0);
	g_hbar = ui_scrollbar (x + 44, y + h - SB + 1, w - 44 - SB, SB - 2, USB_HORIZONTAL);
	ui_w_set_parent (g_vbar, g_view); ui_w_set_parent (g_hbar, g_view);
}
static void gv_move (int x, int y, int w, int h)
{
	g_vx = x; g_vy = y; g_vw = w; g_vh = h;
	ui_w_move (g_view, x, y, w, h);
	ui_w_move (g_vbar, x + w - SB + 1, y + HEAD_H, SB - 2, h - HEAD_H - SB);
	ui_w_move (g_hbar, x + 44, y + h - SB + 1, w - 44 - SB, SB - 2);
	gv_dirty ();
}
// A scroll bar moved.
static bool gv_control (long w, long code)
{
	if (code != UN_CHANGE) return false;
	Sheet *s = S ();
	if (w == g_vbar) { s->topR = iclamp ((int) ui_w_get_value (g_vbar), s->freezeR, MAXR - 1); ui_view_dirty (g_view); return true; }
	if (w == g_hbar) { s->leftC = iclamp ((int) ui_w_get_value (g_hbar), s->freezeC, MAXC - 1); ui_view_dirty (g_view); return true; }
	return false;
}

} // namespace ss

#endif
