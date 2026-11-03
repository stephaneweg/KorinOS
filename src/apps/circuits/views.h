//
// views.h -- Circuits' drawn views (the window's, included by main.cpp after the game's state; Onyx's views.h on ui_view):
// the levels list (the worlds as headers, discs, padlocks, stars), the level's card, the message bar, the truth table (the
// goal, after a Check what the board gives, the current row -- a click on a row sets the switches), the gate count, the
// palette's buttons; and the two cards, which are dialogs here: the lesson of a new gate or idea, the result of a won
// Check.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_views_h
#define _circuits_views_h

static inline int radius (int r) { return ui_theme_flat () ? r : 0; }

// ---- the levels list: the packs as headers, their levels under them --------------------------------------------------
static long g_sbList;					// its scroll bar
struct LevelList
{
	int width, height;
	int sc, hot;					// the scroll (px), the row under the pointer (-1)
	int HEAD, ROW;					// a pack's header, a level's row
	bool down;
	void init () { width = height = 100; sc = 0; hot = -1; HEAD = 30; ROW = 28; down = false; }
	int total () { int n = 0; for (int p = 0; p < g_npacks; p++) n += HEAD + g_packs[p]->n * ROW; return n; }
	int view () { return height - 4; }
	bool scroll () { return total () > view (); }
	void clamp () { int m = total () - view (); if (sc > m) sc = m; if (sc < 0) sc = 0; }
	// The bar follows the list
	void sync ()
	{
		clamp ();
		bool sb = scroll ();
		ui_w_show (g_sbList, sb);
		if (sb) { ui_w_set_range (g_sbList, 0, total (), view (), ROW); ui_w_set_value (g_sbList, sc); }
		view_dirty (g_vList);
	}
	// The row at y (the list's own coordinates, scrolled): *p, *l (-1: a header) -> its index, -1 none
	int row_at (int y, int *P, int *Lv)
	{
		int yy = -sc, idx = 0;
		for (int p = 0; p < g_npacks; p++)
		{
			if (y >= yy && y < yy + HEAD) { *P = p; *Lv = -1; return idx; }
			yy += HEAD; idx++;
			for (int l = 0; l < g_packs[p]->n; l++, yy += ROW, idx++)
				if (y >= yy && y < yy + ROW) { *P = p; *Lv = l; return idx; }
		}
		return -1;
	}
	void paint ()
	{
		ui_fill (0, 0, width, height, C_BG);
		panel (0, 0, width, height, radius (6), C_FIELD, C_LINE);
		clamp ();
		ui_clip (2, 2, width - 4, height - 4);
		int y = 2 - sc, rw = width - 4 - (scroll () ? SB_W + 2 : 0), idx = 0, X0 = 2;
		unsigned dim = mix (C_FIELD, C_FIELD_TEXT, 130);
		for (int p = 0; p < g_npacks; p++, idx++)
		{
			const Pack &pk = *g_packs[p];
			int got = 0; bool any = false;
			for (int l = 0; l < pk.n; l++) { got += g_pr.stars (pk.lv[l]->id); if (level_open (g_pr, pk, l)) any = true; }
			if (y > -HEAD && y < height)
			{
				ui_fill (X0, y, rw, HEAD, mix (C_FIELD, C_FIELD_TEXT, 14));
				char t[16]; snprintf (t, sizeof t, "%d/%d", got, 3 * pk.n); int w = tw (t);
				text_l (X0 + 10, y, HEAD, pk.titleOf (g_lang), any ? C_FIELD_TEXT : dim, 2, PX_UI, rw - 10 - w - 34);
				if (any) { star (X0 + rw - w - 22, y + HEAD / 2, 6, STAR_GOLD, true); text_l (X0 + rw - w - 12, y, HEAD, t, dim); }
				else padlock (X0 + rw - 18, y + HEAD / 2, dim);
				ui_fill (X0, y + HEAD - 1, rw, 1, mix (C_FIELD, C_FIELD_TEXT, 36));
			}
			y += HEAD;
			for (int l = 0; l < pk.n; l++, y += ROW, idx++)
			{
				if (y <= -ROW || y >= height) continue;
				const Level &L = *pk.lv[l];
				bool open = level_open (g_pr, pk, l), sel = p == g_pack && l == g_level;
				int s = g_pr.stars (L.id);
				unsigned ink = open ? C_FIELD_TEXT : C_DIS, back = C_FIELD;
				if (sel) { back = C_ACCENT; rbox (X0 + 3, y + 2, rw - 6, ROW - 4, radius (5), back); ink = ink_on (back); }
				else if (idx + 1 == hot && open) { back = mix (C_FIELD, C_ACCENT, 26); ui_fill (X0 + 3, y + 2, rw - 6, ROW - 4, back); }
				int cx = X0 + 20, cy = y + ROW / 2;
				if (!open) { disc (V (cx), V (cy), V (10), mix (C_FIELD, C_FIELD_TEXT, 26)); padlock (cx, cy, mix (C_FIELD, C_FIELD_TEXT, 110)); }
				else
				{
					unsigned d = sel ? ink : s ? OK_GREEN : mix (C_FIELD, C_FIELD_TEXT, 90);
					disc (V (cx), V (cy), V (10), d);
					char num[8]; snprintf (num, sizeof num, "%d", l + 1);
					text_c (cx - 10, cy - 10, 20, 20, num, sel ? back : ink_on (d), 2, PX_SMALL);
				}
				bool narrow = rw < 170;			// (a narrow list: the title has more room)
				text_l (X0 + 36, y, ROW, L.titleOf (g_lang), ink, sel ? 2 : 0, PX_UI, rw - 38 - (narrow ? 38 : 54));
				if (open)
					for (int k = 0; k < 3; k++)
					{
						if (narrow) star (X0 + rw - 32 + k * 10, cy, 4, k < s ? STAR_GOLD : mix (back, ink, 80), true);
						else star (X0 + rw - 44 + k * 14, cy, 6, k < s ? STAR_GOLD : mix (back, ink, 80), k < s);
					}
			}
		}
		ui_clip (0, 0, 0, 0);
	}
	void mouse (int kind, int mx, int my, int btn)
	{
		if (kind == 3) { if (hot >= 0) { hot = -1; view_dirty (g_vList); } return; }
		int p = -1, l = -1, i = mx < width - (scroll () ? SB_W + 2 : 0) ? row_at (my - 2, &p, &l) : -1;
		if (i != hot) { hot = i; view_dirty (g_vList); }
		if (kind == 1 && (btn & 1) && i >= 0 && l >= 0) level_clicked (p, l);
	}
	void wheel (int n) { sc -= n * ROW; sync (); }
	void showSel ()					// the current level's row in view
	{
		int y = 0;
		for (int p = 0; p < g_npacks; p++)
		{
			if (p == g_pack) { int ry = y + HEAD + g_level * ROW; if (ry < sc + (g_level == 0 ? HEAD : 0)) sc = ry - (g_level == 0 ? HEAD : 0); if (ry + ROW > sc + view ()) sc = ry + ROW - view (); break; }
			y += HEAD + g_packs[p]->n * ROW;
		}
		sync ();
	}
};

// ---- the level's card: its number and title, what to do, the hint ----------------------------------------------------
struct Card
{
	int width, height;
	int btnRoom;					// the room Hint and Lesson take at its right (0: they are on the bench)
	bool titleOnly;					// (a pocket: the level's text is in the message)
	int textW () { return width - 28 - btnRoom; }
	void hint_text (char *h, int cap) { snprintf (h, (size_t) cap, "%s %s", TR ("Hint:"), g_L->hintOf (g_lang)); }
	int need ()
	{
		if (titleOnly) return 32;
		if (!g_L) return 66;
		int h = 7 + fh (PX_BIG) + 3 + wrap_count (g_L->textOf (g_lang), textW (), 4) * (fh () + 2) + 8;
		if (g_showHint) { char t[600]; hint_text (t, sizeof t); h += 2 + wrap_count (t, width - 28, 3) * (fh () + 2); }
		return h < 66 ? 66 : h;
	}
	void paint ()
	{
		ui_fill (0, 0, width, height, C_BG);
		if (!g_L) return;
		panel (0, 0, width, height, radius (10), C_FIELD, C_LINE);
		char num[24]; snprintf (num, sizeof num, "%d.%d", g_pack + 1, g_level + 1);
		int ty = titleOnly ? 4 : 7;
		text (14, ty, num, C_ACCENT, 2, PX_BIG);
		int nw = tw (num, 2, PX_BIG) + 10;
		text (14 + nw, ty, g_L->titleOf (g_lang), C_TEXT, 2, PX_BIG, textW () - nw);
		if (titleOnly) return;
		int y = 7 + fh (PX_BIG) + 3;
		int n = wrap_draw (14, y, textW (), g_L->textOf (g_lang), C_DIM, 4, fh () + 2);
		if (g_showHint) { char h[600]; hint_text (h, sizeof h); wrap_draw (14, y + n * (fh () + 2) + 2, width - 28, h, WARN_TEXT, 3, fh () + 2); }
	}
};

// ---- the message under the board ------------------------------------------------------------------------------------
struct MsgBar
{
	int width, height;
	int kind, stars; char text_[500];
	int px, lines;					// its text's size, how many lines
	bool roomForNext;
	void paint ()
	{
		ui_fill (0, 0, width, height, C_BG);
		if (kind == M_NONE || !text_[0]) return;
		unsigned edge = kind == M_OK ? OK_GREEN : kind == M_ERR ? ERR_RED : C_ACCENT, face = mix (C_FIELD, edge, 26);
		panel (0, 0, width, height, radius (10), face, edge);
		int x = 14;
		if (kind == M_OK && stars) for (int k = 0; k < 3; k++) { star (x + 10, height / 2, 10, k < stars ? STAR_GOLD : mix (face, C_TEXT, 60), true); x += 23; }
		else
		{
			disc (V (x + 10), V (height / 2), V (10), edge);
			text_c (x, height / 2 - 10, 20, 20, kind == M_OK ? "\xE2\x9C\x93" : kind == M_ERR ? "!" : "i", ink_on (edge), 2);
			x += 26;
		}
		int room = width - x - 16 - (roomForNext ? 140 : 0);
		int lh = fh (px) + 1, n = wrap_count (text_, room, lines, 0, px), y = (height - n * lh) / 2;
		wrap_draw (x + 6, y, room, text_, C_TEXT, lines, lh, 0, false, px);
	}
};

// ---- the truth table: the goal, what the board gives after a Check, the current row ------------------------------------
struct TruthTable
{
	int width, height;
	int hot, rh;					// the row under the pointer; the rows' height (RH; less on a low screen)
	int first;					// the first row shown (too many rows: the current one stays in sight)
	enum { RH = 21, Y0 = 46, MARK = 26 };
	int need () { return g_L ? Y0 + g_L->rows () * rh + 8 : 60; }
	int cols () { return g_L->ninputs + g_L->noutputs * (g_checked ? 2 : 1); }
	int colW () { int cw = (width - 16 - MARK) / cols (); return cw > 44 ? 44 : cw; }
	int x0 () { return (width - (cols () * colW () + MARK)) / 2 + 4; }
	int shown () { int v = (height - Y0 - 4) / rh, n = g_L->rows (); return v < n ? v : n; }
	void scroll ()
	{
		int v = shown (), n = g_L->rows (), r = (int) g_row;
		if (v >= n) { first = 0; return; }
		if (r < first + 1) first = r - 1;		// (a row more beyond it: a click there goes on)
		if (r > first + v - 2) first = r - v + 2;
		if (first > n - v) first = n - v;
		if (first < 0) first = 0;
	}
	void paint ()
	{
		ui_fill (0, 0, width, height, C_BG);
		panel (0, 0, width, height, radius (6), C_FIELD, C_LINE);
		if (!g_L) return;
		const Level &L = *g_L;
		int rows = L.rows (), nc = cols (), cw = colW (), w = nc * cw + MARK, X0 = x0 ();
		unsigned dim = mix (C_FIELD, C_FIELD_TEXT, 130), grid = mix (C_FIELD, C_FIELD_TEXT, 30);
		int xo = X0 + L.ninputs * cw, xg = xo + L.noutputs * cw;
		text_c (X0, 6, L.ninputs * cw, 14, L.ninputs > 1 ? TR ("IN") : TRC ("one", "IN"), dim, 2, PX_SMALL);
		text_c (xo, 6, L.noutputs * cw, 14, TR ("GOAL"), dim, 2, PX_SMALL);
		if (g_checked) text_c (xg, 6, L.noutputs * cw, 14, TR ("YOURS"), dim, 2, PX_SMALL);
		int hp = cw < 24 ? PX_TINY : PX_UI;		// (eleven columns: the names smaller)
		for (int k = 0; k < L.ninputs; k++) text_c (X0 + k * cw, 22, cw, 20, L.inName[k], C_FIELD_TEXT, 2, hp);
		for (int k = 0; k < L.noutputs; k++) text_c (xo + k * cw, 22, cw, 20, L.outName[k], C_FIELD_TEXT, 2, hp);
		if (g_checked) for (int k = 0; k < L.noutputs; k++) text_c (xg + k * cw, 22, cw, 20, L.outName[k], C_FIELD_TEXT, 2, hp);
		ui_fill (X0 - 4, Y0 - 2, w + 2, 1, mix (C_FIELD, C_FIELD_TEXT, 70));
		scroll ();
		int last = first + shown ();
		for (int r = first; r < last; r++)
		{
			int y = Y0 + (r - first) * rh;
			bool wrong = g_checked && ((g_res.wrong >> r) & 1);
			if (wrong) rbox (X0 - 4, y + 1, w + 2, rh - 2, radius (4), mix (C_FIELD, ERR_RED, 60));
			else if (r == hot) ui_fill (X0 - 4, y + 1, w + 2, rh - 2, mix (C_FIELD, C_ACCENT, 26));
			else if (r & 1) ui_fill (X0 - 4, y + 1, w + 2, rh - 2, mix (C_FIELD, C_FIELD_TEXT, 8));
			if (r == (int) g_row) ui_frame (X0 - 4, y + 1, w + 2, rh - 2, wrong ? ERR_RED : C_ACCENT);
			for (int k = 0; k < L.ninputs; k++)
			{ int b = (r >> (L.ninputs - 1 - k)) & 1; text_c (X0 + k * cw, y, cw, rh, b ? "1" : "0", b ? C_FIELD_TEXT : dim); }
			for (int k = 0; k < L.noutputs; k++)
			{ int b = (L.want[k] >> r) & 1; text_c (xo + k * cw, y, cw, rh, b ? "1" : "0", b ? C_FIELD_TEXT : dim, 2); }
			int mx = X0 + nc * cw + MARK / 2 - 2, my = y + rh / 2;
			if (g_checked)
			{
				for (int k = 0; k < L.noutputs; k++)
				{
					int b = (g_res.got[k] >> r) & 1, wb = (L.want[k] >> r) & 1;
					text_c (xg + k * cw, y, cw, rh, b ? "1" : "0", b != wb ? ERR_RED : b ? C_FIELD_TEXT : dim, 2);
				}
				if (wrong) { disc (V (mx), V (my), V (7), ERR_RED); text_c (mx - 7, my - 7, 14, 14, "\xC3\x97", ink_on (ERR_RED), 2, PX_UI); }
				else text_c (mx - 8, my - 8, 16, 16, "\xE2\x9C\x93", OK_GREEN, 2);
			}
			else if (r == (int) g_row) text_c (mx - 8, my - 8, 16, 16, "\xE2\x97\x80", C_ACCENT, 0, PX_SMALL);
		}
		ui_fill (xo - 2, 22, 1, shown () * rh + 24, grid);
		if (g_checked) ui_fill (xg, 22, 1, shown () * rh + 24, grid);
	}
	void mouse (int kind, int my, int btn)
	{
		if (kind == 3 || !g_L) { if (hot >= 0) { hot = -1; view_dirty (g_vTable); } return; }
		int r = my >= Y0 ? (my - Y0) / rh + first : -1;
		if (r >= first + shown ()) r = -1;
		if (r != hot) { hot = r; view_dirty (g_vTable); }
		if (kind == 1 && (btn & 1) && r >= 0) set_row ((unsigned) r);
	}
};

// ---- the gate count under the table, the stars it needs ----------------------------------------------------------------
static const char *gates_word (int n) { return n == 1 ? TR ("gate") : TR ("gates"); }
struct CountView
{
	int width, height;
	void paint ()
	{
		ui_fill (0, 0, width, height, C_BG);
		if (!g_L) return;
		int n = g_c.gates ();
		char t[16]; snprintf (t, sizeof t, "%d", n);
		int X = 4;
		text (X, 0, t, n <= g_L->par3 ? OK_GREEN : C_TEXT, 2, PX_HUGE);
		int x = X + 6 + tw (t, 2, PX_HUGE);
		text (x, 4, gates_word (n), C_TEXT, 2);
		char a[40], b[40]; snprintf (a, sizeof a, "\xE2\x89\xA4 %d", g_L->par3); snprintf (b, sizeof b, "\xE2\x89\xA4 %d", g_L->par2);
		int y = 22; unsigned dim = mix (C_BG, C_TEXT, 170);
		for (int k = 0; k < 3; k++) star (x + 6 + k * 12, y + 8, 5, STAR_GOLD, true);
		text (x + 42, y, a, dim);
		int x2 = x + 42 + tw (a) + 14;
		for (int k = 0; k < 2; k++) star (x2 + 6 + k * 12, y + 8, 5, STAR_GOLD, true);
		text (x2 + 30, y, b, dim);
		if (n)
		{
			char d[120]; snprintf (d, sizeof d, TR ("Depth %d \xC2\xB7 Step (F8) to watch it"), g_ev.maxDepth);
			text (X, 46, d, dim, 0, PX_UI, width - 8 - X);
		}
	}
};

// ---- the palette: Select, then the gates the level allows -- a small view each, inside the toolbar ------------------------
static long g_pal[P_COUNT_];				// [P_SWITCH]: Select; [P_NOT .. P_NOR]: the gates
static int g_palHot = -1, g_palPress = -1;
static int pal_index (long v) { for (int t = 0; t < P_COUNT_; t++) if (g_pal[t] == v) return t; return -1; }
static int pal_type (int t) { return t == P_SWITCH ? -1 : t; }
static void pal_paint (long v, long w, long h)
{
	int t = pal_index (v), type = pal_type (t);
	bool on = g_armed == type;
	unsigned back = on ? mix (C_BG, C_ACCENT, 56) : t == g_palHot ? mix (C_BG, C_ACCENT, 22) : C_BG;
	ui_fill (0, 0, w, h, C_BG);
	panel (0, 0, w, h, radius (6), back, on ? C_ACCENT : back);
	circuits_icon (type < 0 ? (int) IC_SELECT : type, 2, 2, w - 4, C_TEXT, back);
}
// (a press arms; the drag that goes on is the board's: Onyx's "drag a gate from the palette")
static long pal_mouse (long v, long kind, long x, long y, long info);

// ---- the lesson: a new gate (its symbol, its truth table) or a new idea (the level's table), a dialog ----------------------
static const Lesson *g_shown;				// the lesson of the dialog open
// a small truth table in a box at (tx, ty): names, rows of bits; the outputs' 1 in green
static int lesson_table (int tx, int ty, int ni, int no, const char *const *names, const unsigned *want)
{
	int rows = 1 << ni, nc = ni + no, cw = nc > 4 ? 28 : 34, RH = rows > 8 ? 15 : rows > 4 ? 18 : 21;
	rbox (tx - 8, ty - 4, nc * cw + 16, (rows + 1) * RH + 12, radius (6), jet (J_CREUX));
	for (int k = 0; k < nc; k++) text_c (tx + k * cw, ty, cw, RH, names[k], C_DIM, 2);
	ui_fill (tx, ty + RH, nc * cw, 1, C_LINE);
	for (int r = 0; r < rows; r++)
		for (int k = 0; k < nc; k++)
		{
			int b = k < ni ? (r >> (ni - 1 - k)) & 1 : (want[k - ni] >> r) & 1;
			text_c (tx + k * cw, ty + RH + 2 + r * RH, cw, RH, b ? "1" : "0", k >= ni && b ? WIRE_1 : C_TEXT, k >= ni ? 2 : 0);
		}
	return nc * cw;
}
static void lesson_paint (long, long width, long height)
{
	const Lesson *ls = g_shown;
	ui_fill (0, 0, width, height, C_BG);
	if (!ls) return;
	int textY = 150, textX = 20, textW = (int) width - 40;
	if (ls->gate >= 0)				// the symbol, big, with its pins named; its table beside it
	{
		int L = 46, T = 24, sc = 18, g = ls->gate;
		int l = (L + sc) * 16, r = (L + sc * 41 / 10) * 16, cy = (T + 2 * sc) * 16, h2 = sc * 16 * 145 / 100;
		rbox (12, 8, 196, 128, radius (8), BOARD_BG);
		if (g == P_NOT) seg (V (L - 8), cy, V (L + sc + 4), cy, WIRE_0, V (2) + 8);
		else { seg (V (L - 8), V (T + sc), V (L + sc + 4), V (T + sc), WIRE_0, V (2) + 8); seg (V (L - 8), V (T + 3 * sc), V (L + sc + 4), V (T + 3 * sc), WIRE_0, V (2) + 8); }
		seg (r - V (2), cy, V (L + sc * 5 + 10), cy, WIRE_0, V (2) + 8);
		draw_gate_shape (g, l, cy - h2, r, cy + h2, sc * 16 * 3 / 10, GATE_FACE, GATE_INK, V (2));
		if (g == P_NOT) text_l (L - 24, cy / 16 - 10, 20, "A", C_TEXT, 2);
		else { text_l (L - 24, T + sc - 10, 20, "A", C_TEXT, 2); text_l (L - 24, T + 3 * sc - 10, 20, "B", C_TEXT, 2); }
		text_l (L + sc * 5 + 16, cy / 16 - 10, 30, TR ("Out"), C_TEXT, 2);
		text_c (L + sc + 2, cy / 16 - 8, sc * 2, 16, gate_word (g), GATE_INK, 2, PX_SMALL);
		int ni = g == P_NOT ? 1 : 2; unsigned want = 0;
		for (int rr = 0; rr < (1 << ni); rr++)
		{
			int a = ni == 2 ? rr >> 1 : rr, b = rr & 1, o = 0;
			switch (g) { case P_NOT: o = !a; break; case P_AND: o = a & b; break; case P_OR: o = a | b; break;
				     case P_XOR: o = a ^ b; break; case P_NAND: o = !(a & b); break; case P_NOR: o = !(a | b); break; }
			want |= (unsigned) o << rr;
		}
		const char *nm1[2] = { "A", "Out" }, *nm2[3] = { "A", "B", "Out" };
		lesson_table (236, 14, ni, 1, ni == 1 ? nm1 : nm2, &want);
	}
	else if (g_L)					// an idea: the level's own table at the left, the text beside it
	{
		const char *names[2 * MAXIO];
		for (int k = 0; k < g_L->ninputs; k++) names[k] = g_L->inName[k];
		for (int k = 0; k < g_L->noutputs; k++) names[g_L->ninputs + k] = g_L->outName[k];
		int w = lesson_table (30, 16, g_L->ninputs, g_L->noutputs, names, g_L->want);
		textX = 30 + w + 30; textY = 14; textW = (int) width - textX - 20;
	}
	wrap_draw (textX, textY, textW, ls->text[g_lang], C_TEXT, ls->gate >= 0 ? 6 : 12, fh () + 3);
}
static void lesson_dialog (const Lesson *ls)
{
	enum { W = 500 };
	char t[160];
	if (ls->gate >= 0) snprintf (t, sizeof t, "%s  %s", TR ("New gate:"), gate_word (ls->gate));
	else snprintf (t, sizeof t, "%s  %s", TR ("New idea:"), ls->title[g_lang]);
	g_shown = ls;
	// (its height: the text's, the symbol or the table's)
	int H;
	if (ls->gate >= 0) H = 150 + wrap_count (ls->text[g_lang], W - 40, 6) * (fh () + 3);
	else
	{
		int rows = g_L ? g_L->rows () : 0, nc = g_L ? g_L->ninputs + g_L->noutputs : 2, rh = rows > 8 ? 15 : rows > 4 ? 18 : 21;
		int cw = nc > 4 ? 28 : 34, tx = 30 + nc * cw + 30, th = 16 + (rows + 1) * rh + 16, tt = 14 + wrap_count (ls->text[g_lang], W - tx - 20, 12) * (fh () + 3);
		H = th > tt ? th : tt;
	}
	H += 14 + 56;
	long d = ui_dialog_begin (W, H, (long) t);
	long v = ui_view (0, 0, W, H - 56, 0);
	ui_view_on (v, (long) lesson_paint, 0, 0, 0);
	ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, W - 12);
	ui_dialog_cancel (d, 0);
	ui_dialog_run (d, 0, 0);
	ui_dialog_end (d);
}

// ---- the result of a won Check -> true: the next level -----------------------------------------------------------------
static struct { int stars, gates; bool record, last; } g_result;
static void result_paint (long, long width, long height)
{
	ui_fill (0, 0, width, height, C_BG);
	if (!g_L) return;
	int cx = (int) width / 2;
	for (int k = 0; k < 3; k++) star (cx - 52 + k * 52, 40, k == 1 ? 26 : 22, k < g_result.stars ? STAR_GOLD : mix (C_BG, C_TEXT, 60), true);
	int y = 72;
	if (g_result.record) { text_c (0, 66, (int) width, 16, TR ("New record!"), WARN_TEXT, 2); y = 84; }
	char t[160]; snprintf (t, sizeof t, TR ("%d %s \xE2\x80\x94 every row right"), g_result.gates, gates_word (g_result.gates));
	text_c (0, y, (int) width, 26, t, C_TEXT, 2, PX_BIG);
	char s[300];
	if (g_result.last) snprintf (s, sizeof s, "%s", TR ("You finished every level!"));
	else if (g_result.stars < 3) snprintf (s, sizeof s, TR ("Three stars need %d %s or fewer. The best known circuit is smaller: can you find it?"), g_L->par3, gates_word (g_L->par3));
	else snprintf (s, sizeof s, TR ("The best possible: %d %s."), g_result.gates, gates_word (g_result.gates));
	wrap_draw (25, y + 32, (int) width - 50, s, C_DIM, 3, 19, 0, true);
}
static bool result_dialog ()
{
	enum { W = 420, H = 226 };
	long d = ui_dialog_begin (W, H, (long) TR ("Level complete!"));
	long v = ui_view (0, 0, W, H - 56, 0);
	ui_view_on (v, (long) result_paint, 0, 0, 0);
	long edge = W - 12;
	if (!g_result.last) edge = ui_dialog_button (d, 0, (long) TR ("Next level"), 1, UB_DEFAULT, edge);
	ui_dialog_button (d, 1, (long) (g_result.stars == 3 ? TR ("Stay here") : TR ("Try for \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85")), 0, g_result.last ? UB_DEFAULT : 0, edge);
	ui_dialog_cancel (d, 0);
	long r = ui_dialog_run (d, 0, 0);
	ui_dialog_end (d);
	return r == 1;
}

#endif
