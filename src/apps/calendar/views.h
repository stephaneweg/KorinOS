//
// views.h -- the planner's views, each a ui_view: the time grid (a day or a week: the hours down the side, the events as
// coloured blocks -- a drag moves one, its bottom edge resizes it, a double click opens it; a drag on the empty grid
// draws a new one; the all-day events in a row on top; now as a red line), the month grid, the categories and the tasks.
// What opens a dialog is kept in g_todo and done by the main loop, out of the view's function.
//
#ifndef _calendar_views_h
#define _calendar_views_h

static int  g_today, g_nowMin;			// today (a day number), now (minutes into the day)
static int  g_sel = -1, g_selStart = 0;		// the event selected (its index, the occurrence's start)
static void event_moved (int ev, int oldOccStart, int newStart, int newEnd);
static void data_changed (bool save);
static void goto_day (int dn, int view);	// show that day in that view (-1: the view kept)
static const int RED = 0x00E0453A;

// What the main loop does next
enum { T_NONE, T_OPEN, T_NEW, T_NEWDAY, T_TASK };
static struct { int what, a, b, c; } g_todo;
static void todo (int what, int a = 0, int b = 0, int c = 0) { g_todo.what = what; g_todo.a = a; g_todo.b = b; g_todo.c = c; }

static long g_vgrid, g_vmonth, g_vcats, g_vtasks, g_sbar;

static unsigned soft_ink () { return mix (C_TEXT, C_FIELD, 130); }
static unsigned grid_line () { return mix (C_FIELD, C_TEXT, 30); }
static unsigned face_soft () { return mix (C_TEXT, C_BG, 110); }
static unsigned ev_fill (unsigned c, bool past) { return mix (C_FIELD, c, past ? 34 : 58); }
static unsigned ev_ink (unsigned c, bool past)
{
	unsigned k = mix (c, s_jet[22] ? 0x00FFFFFFu : 0x00000000u, 120);	// (TJ_SOMBRE: a dark theme, lighter)
	return past ? mix (k, C_FIELD, 90) : k;
}
static unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c); }

// Word wrap: the next row of s that fits in w px: its length in bytes; *next: where the next row starts
static int u8_next (const char *s, int i, int n) { i++; while (i < n && (s[i] & 0xC0) == 0x80) i++; return i; }
static int wrap_row (const char *s, int w, const char **next, int style = 0)
{
	int len = 0; while (s[len] && s[len] != '\n') len++;
	if (text_wn (s, len, style) <= w) { *next = s[len] == '\n' ? s + len + 1 : s + len; return len; }
	int fit = 0;					// the most bytes that fit (whole characters)
	for (int i = u8_next (s, 0, len); i <= len && text_wn (s, i, style) <= w; i = u8_next (s, i, len))
	{ fit = i; if (i == len) break; }
	if (fit == 0) fit = u8_next (s, 0, len);
	int cut = fit; while (cut > 0 && s[cut] != ' ') cut--;
	if (cut < fit / 3) cut = fit;			// (no space: the word cut)
	const char *p = s + cut; while (*p == ' ') p++;
	*next = p;
	return cut;
}
static int iso_week (int dn)
{
	int th = dn - wday (dn) + 3;			// (the week's Thursday)
	int j1 = days_from_civil (dn_y (th), 1, 1);
	return (th - j1) / 7 + 1;
}

// ---- the time grid (a day, a week) ----------------------------------------------------------------------

#define GUTTER	54		// the hours' column
#define HDR_H	58		// the days' names
#define ADROW	22		// an all-day row
#define SBW	12		// the scroll bar

struct TimeGrid
{
	int ndays = 7, day0 = 0, width = 0, height = 0;
	int hourH = 46, scrollY = 8 * 46;
	struct Hit { int x, y, w, h, ev, start, end; bool allDay; };
	Hit hits[400]; int nhits = 0;
	int adH = ADROW + 8, y0 = HDR_H + ADROW + 8;
	// the drag: 0 none, 1 pressed on an event, 2 moving it, 3 resizing it, 5 a new range, 6 pressed on the empty grid
	int drag = 0, dx0 = 0, dy0 = 0, dHit = -1, pvStart = 0, pvEnd = 0, dayAtPress = 0, minAtPress = 0;

	int colW () const { int w = (width - GUTTER - SBW - 2) / ndays; return w < 20 ? 20 : w; }
	int colX (int i) const { return GUTTER + i * colW (); }
	int bodyH () const { return height - y0; }
	void sync_bar ()
	{
		int m = 24 * hourH - bodyH (); if (m < 0) m = 0;
		if (scrollY > m) scrollY = m;
		if (scrollY < 0) scrollY = 0;
		ui_w_set_range (g_sbar, 0, 24 * hourH, bodyH (), hourH / 2);
		ui_w_set_value (g_sbar, scrollY);
	}
	void scrollToHour (int h) { scrollY = h * hourH; sync_bar (); ui_view_dirty (g_vgrid); }

	void add_hit (int x, int y, int w, int h, const Occ &o, bool ad)
	{
		if (nhits >= 400) return;
		Hit &t = hits[nhits++];
		t.x = x; t.y = y; t.w = w; t.h = h; t.ev = o.ev; t.start = o.start; t.end = o.end; t.allDay = ad;
	}
	void draw_block (const Occ &o, int x, int y, int w, int h, int s, int en)
	{
		const Event &e = g_ev[o.ev];
		unsigned c = cat_color (e.cat);
		bool past = o.end <= g_today * 1440 + g_nowMin;
		bool sel = o.ev == g_sel && o.start == g_selStart;
		add_hit (x, y, w, h, o, false);
		if (y + h < y0 || y > height) return;
		unsigned fill = sel ? c : ev_fill (c, past), ink = sel ? ink_on (c) : ev_ink (c, past);
		rbox (x, y, w, h, 5, fill);
		ui_fill (x, y + 2, 4, h > 4 ? h - 4 : 1, c);
		if (h < 16) return;
		// The title (wrapped over the rows it may take), the time, the place -- what fits.
		const char *ti = e.title[0] ? e.title : TR ("(Sans titre)");
		int fh = 16, rows = (h - 4) / (fh + 1), tw = w - 11, ty = y + 2;
		char a[8], b[8], t[40]; fmt_hm (s, a, sizeof a); fmt_hm (en, b, sizeof b);
		scpy (t, sizeof t, a); scat (t, sizeof t, " - "); scat (t, sizeof t, b);
		if (text_w (t) > tw) scpy (t, sizeof t, a);
		int trows = rows <= 1 ? 1 : rows - 1 - (e.place[0] && rows >= 4 ? 1 : 0);
		if (trows > 3) trows = 3;
		const char *p = ti;
		for (int r = 0; r < trows && *p; r++)
		{
			const char *nx; int n = wrap_row (p, tw, &nx, 2);
			char row[200]; int k = 0; for (; k < n && k < 199; k++) row[k] = p[k]; row[k] = '\0';
			if (r == trows - 1 && *nx) scpy (row, sizeof row, p);	// (the last row: cut with an ellipsis)
			text_fit (x + 8, ty, tw, fh, row, ink, 2); ty += fh + 1;
			p = nx;
		}
		if (rows >= 2) { text_fit (x + 8, ty, tw, fh, t, ink); ty += fh + 1; }
		if (e.place[0] && ty + fh <= y + h) text_fit (x + 8, ty, tw, fh, e.place, ink);
	}
	// The events' blocks in one day's column: overlapping ones side by side.
	void lay_day (Occ *occ, int n, int di)
	{
		int dn = day0 + di, x0 = colX (di) + 1, w0 = colW () - 3;
		struct B { int k, s, e, col; } b[96]; int nb = 0;
		for (int k = 0; k < n && nb < 96; k++)
		{
			const Event &e = g_ev[occ[k].ev];
			if (e.allDay || occ[k].end - occ[k].start >= 1440) continue;
			int s = occ[k].start, en = occ[k].end;
			if (drag >= 2 && drag <= 3 && dHit >= 0 && occ[k].ev == dragEv && occ[k].start == dragStart) { s = pvStart; en = pvEnd; }
			if (s >= (dn + 1) * 1440 || en <= dn * 1440) continue;
			if (s < dn * 1440) s = dn * 1440;
			if (en > (dn + 1) * 1440) en = (dn + 1) * 1440;
			if (en - s < 20) en = s + 20;
			b[nb].k = k; b[nb].s = s; b[nb].e = en; b[nb].col = 0; nb++;
		}
		for (int i = 1; i < nb; i++) { B t = b[i]; int j = i; while (j > 0 && b[j - 1].s > t.s) { b[j] = b[j - 1]; j--; } b[j] = t; }
		int i = 0;
		while (i < nb)					// a cluster: the ones that overlap one another
		{
			int j = i, cend = b[i].e, colEnd[16], ncol = 0;
			while (j < nb && (j == i || b[j].s < cend))
			{
				int c = 0; while (c < ncol && colEnd[c] > b[j].s) c++;
				if (c == ncol && ncol < 16) ncol++;
				if (c >= 16) c = 15;
				colEnd[c] = b[j].e; b[j].col = c;
				if (b[j].e > cend) cend = b[j].e;
				j++;
			}
			for (int k = i; k < j; k++)
			{
				int cw = w0 / ncol;
				int y = y0 + (b[k].s - dn * 1440) * hourH / 60 - scrollY;
				int h = (b[k].e - b[k].s) * hourH / 60 - 2;
				draw_block (occ[b[k].k], x0 + b[k].col * cw, y, b[k].col == ncol - 1 ? w0 - b[k].col * cw : cw - 2, h, b[k].s, b[k].e);
			}
			i = j;
		}
	}
	int dragEv = -1, dragStart = 0, dragEnd = 0;

	void draw ()
	{
		ui_fill (0, 0, width, height, C_FIELD);
		nhits = 0;
		static Occ occ[600];
		int n = occurrences (day0, day0 + ndays, occ, 600);
		int cw = colW ();

		// The all-day row(s): each item on the first free row along its days.
		struct A { int k, a, b, row; } ad[64]; int nad = 0, rows = 0;
		unsigned char used[8][7]; for (int r = 0; r < 8; r++) for (int d = 0; d < 7; d++) used[r][d] = 0;
		for (int k = 0; k < n && nad < 64; k++)
		{
			const Event &e = g_ev[occ[k].ev];
			if (!e.allDay && occ[k].end - occ[k].start < 1440) continue;
			int a = occ[k].start / 1440 - day0, b = (occ[k].end - 1) / 1440 - day0;
			if (a < 0) a = 0;
			if (b >= ndays) b = ndays - 1;
			if (b < a) b = a;
			int r = 0;
			for (; r < 8; r++) { bool free = true; for (int d = a; d <= b; d++) if (used[r][d]) free = false; if (free) break; }
			if (r >= 8) continue;
			for (int d = a; d <= b; d++) used[r][d] = 1;
			ad[nad].k = k; ad[nad].a = a; ad[nad].b = b; ad[nad].row = r; nad++;
			if (r + 1 > rows) rows = r + 1;
		}
		adH = (rows < 1 ? 1 : rows) * ADROW + 8;
		y0 = HDR_H + adH;
		sync_bar ();

		// The body: the hours.
		int bw = cw * ndays;
		for (int di = 0; di < ndays; di++)
		{
			int dn = day0 + di, x = colX (di);
			unsigned bg = dn == g_today ? mix (C_FIELD, C_ACCENT, 16) : wday (dn) >= 5 ? mix (C_FIELD, C_TEXT, 7) : C_FIELD;
			ui_fill (x, y0, cw, height - y0, bg);
			// (the evening and the night a shade darker)
			int ya = y0 - scrollY, yb = y0 + 8 * hourH - scrollY, yc = y0 + 19 * hourH - scrollY;
			unsigned night = mix (bg, C_TEXT, 8);
			if (yb > y0) ui_fill (x, ya < y0 ? y0 : ya, cw, yb - (ya < y0 ? y0 : ya), night);
			if (yc < height) ui_fill (x, yc < y0 ? y0 : yc, cw, height - (yc < y0 ? y0 : yc), night);
		}
		int fh = 16;
		for (int h = 0; h <= 24; h++)
		{
			int y = y0 + h * hourH - scrollY;
			if (y >= y0 - fh && y < height)
			{
				if (y >= y0) ui_fill (GUTTER - 6, y, bw + 6, 1, grid_line ());
				if (h > 0 && h < 24 && y - fh / 2 >= y0)
				{
					char t[8]; fmt_hm (h * 60, t, sizeof t);
					text_l (GUTTER - 10 - text_w (t), y - fh / 2 - 1, fh, t, soft_ink ());
				}
			}
			int yh = y + hourH / 2;					// (the half hour: dotted)
			if (h < 24 && yh >= y0 && yh < height)
				for (int x = GUTTER; x < GUTTER + bw; x += 3) ui_pixel (x, yh, mix (C_FIELD, C_TEXT, 18));
		}
		for (int di = 0; di <= ndays; di++) ui_fill (colX (di) - (di == ndays ? 1 : 0), y0, 1, height - y0, grid_line ());

		// A range being drawn (a new event).
		if (drag == 5)
		{
			int di = pvStart / 1440 - day0;
			int ya = y0 + (pvStart % 1440) * hourH / 60 - scrollY, yb = y0 + (pvEnd - (pvStart / 1440) * 1440) * hourH / 60 - scrollY;
			rbox (colX (di) + 1, ya, cw - 3, yb - ya - 1, 5, mix (C_FIELD, C_ACCENT, 90));
			char a[8], b[8], t[24]; fmt_hm (pvStart, a, sizeof a); fmt_hm (pvEnd, b, sizeof b);
			scpy (t, sizeof t, a); scat (t, sizeof t, " - "); scat (t, sizeof t, b);
			text_fit (colX (di) + 8, ya + 2, cw - 12, fh, t, ink_on (mix (C_FIELD, C_ACCENT, 90)), 2);
		}
		ui_clip (0, y0, width, height - y0);
		for (int di = 0; di < ndays; di++) lay_day (occ, n, di);

		// Now: a red line across today.
		if (g_today >= day0 && g_today < day0 + ndays)
		{
			int y = y0 + g_nowMin * hourH / 60 - scrollY;
			if (y >= y0 && y < height)
			{
				int x = colX (g_today - day0);
				ui_fill (x, y - 1, cw, 2, RED);
				disc (x - 5, y - 5, 10, RED);
			}
		}
		ui_clip (0, 0, 0, 0);

		// The header over the body: the days' names and numbers, the all-day row.
		ui_fill (0, 0, width, y0, C_FIELD);
		for (int di = 0; di < ndays; di++)
		{
			int dn = day0 + di, x = colX (di);
			bool today = dn == g_today;
			unsigned ink = today ? C_ACCENT : wday (dn) >= 5 ? soft_ink () : C_TEXT;
			if (ndays == 1) text_l (x + 12, 6, fh, WDAY[wday (dn)], today ? C_ACCENT : soft_ink (), 2);
			else text_c (x, 6, cw, fh, WD3[wday (dn)], today ? C_ACCENT : soft_ink (), 2);
			char num[4] = ""; scatn (num, sizeof num, dn_d (dn));
			int s = 30, cx = ndays == 1 ? x + 12 : x + (cw - s) / 2, cy = 6 + fh + 2;
			if (today) { disc (cx, cy, s, C_ACCENT); text_c (cx, cy, s, s, num, ink_on (C_ACCENT), 2); }
			else text_c (cx, cy, s, s, num, ink, 2);
			if (di > 0) ui_fill (x, HDR_H - 8, 1, y0 - HDR_H + 8, grid_line ());
		}
		text_l (2, HDR_H + 3, ADROW - 4, TR ("journé" "e"), soft_ink ());
		{ char wk[12]; scpy (wk, sizeof wk, TR ("S")); scatn (wk, sizeof wk, iso_week (day0)); text_c (0, 6, GUTTER, fh, wk, soft_ink ()); }
		for (int i = 0; i < nad; i++)
		{
			const Occ &o = occ[ad[i].k]; const Event &e = g_ev[o.ev];
			unsigned c = cat_color (e.cat);
			int x = colX (ad[i].a) + 2, w = (ad[i].b - ad[i].a + 1) * cw - 5, y = HDR_H + 2 + ad[i].row * ADROW;
			bool sel = o.ev == g_sel && o.start == g_selStart;
			unsigned fill = sel ? c : mix (C_FIELD, c, 170);
			rbox (x, y, w, ADROW - 3, 5, fill);
			text_fit (x + 7, y, w - 10, ADROW - 3, e.title[0] ? e.title : TR ("(Sans titre)"), ink_on (fill), 2);
			add_hit (x, y, w, ADROW - 3, o, true);
		}
		ui_fill (0, y0 - 1, width, 1, mix (C_FIELD, C_TEXT, 60));
	}
	int hit_at (int mx, int my) const
	{
		for (int i = nhits - 1; i >= 0; i--)
		{
			const Hit &h = hits[i];
			if (!h.allDay && my < y0) continue;
			if (mx >= h.x && mx < h.x + h.w && my >= h.y && my < h.y + h.h) return i;
		}
		return -1;
	}
	int day_at (int mx) const { int d = (mx - GUTTER) / colW (); if (mx < GUTTER) d = 0; if (d >= ndays) d = ndays - 1; return day0 + d; }
	int min_at (int my) const { int m = (my - y0 + scrollY) * 60 / hourH; if (m < 0) m = 0; if (m > 1439) m = 1439; return m; }

	void press (int mx, int my, int clicks)
	{
		bool dbl = (clicks & 0xFF) >= 2;
		int i = hit_at (mx, my);
		if (i >= 0)
		{
			const Hit &h = hits[i];
			g_sel = h.ev; g_selStart = h.start;
			if (dbl) { todo (T_OPEN, h.ev, h.start); return; }
			drag = 1; dHit = i; dx0 = mx; dy0 = my; dayAtPress = day_at (mx);
			dragEv = h.ev; dragStart = h.start; dragEnd = h.end; pvStart = h.start; pvEnd = h.end;
			ui_view_dirty (g_vgrid);
			return;
		}
		if (my < HDR_H && mx >= GUTTER) { if (ndays > 1) goto_day (day_at (mx), 0); return; }
		g_sel = -1;
		if (my < y0 && mx >= GUTTER) { if (dbl) todo (T_NEWDAY, day_at (mx) * 1440); ui_view_dirty (g_vgrid); return; }
		if (mx >= GUTTER && my >= y0)
		{
			if (dbl) { todo (T_NEW, day_at (mx) * 1440 + min_at (my) / 30 * 30, -1); return; }
			dayAtPress = day_at (mx); minAtPress = min_at (my) / 15 * 15; dx0 = mx; dy0 = my;
			drag = 6;				// (a press on the empty grid: a drag draws a new event)
		}
		ui_view_dirty (g_vgrid);
	}
	void move (int mx, int my)
	{
		if (drag == 6 && (my - dy0) * (my - dy0) > 64) drag = 5;
		if (drag == 5)
		{
			int m = (min_at (my) + 7) / 15 * 15;
			int a = dayAtPress * 1440 + minAtPress, b = dayAtPress * 1440 + m;
			if (b < a) { int t = a; a = b; b = t; }
			if (b - a < 15) b = a + 15;
			pvStart = a; pvEnd = b;
			ui_view_dirty (g_vgrid);
			return;
		}
		if (drag == 1 && (mx - dx0) * (mx - dx0) + (my - dy0) * (my - dy0) > 25 && !hits[dHit].allDay)
			drag = (dy0 >= hits[dHit].y + hits[dHit].h - 6) ? 3 : 2;
		if (drag == 2)
		{
			int dm = (my - dy0) * 60 / hourH; dm = (dm >= 0 ? dm + 7 : dm - 7) / 15 * 15;
			int dd = day_at (mx) - dayAtPress;
			pvStart = dragStart + dd * 1440 + dm; pvEnd = dragEnd + dd * 1440 + dm;
			ui_view_dirty (g_vgrid);
		}
		else if (drag == 3)
		{
			int e = (day_at (dx0) * 1440) + (min_at (my) + 7) / 15 * 15;
			if (e < dragStart + 15) e = dragStart + 15;
			pvStart = dragStart; pvEnd = e;
			ui_view_dirty (g_vgrid);
		}
	}
	void release ()
	{
		int d = drag; drag = 0;
		if (d == 5) todo (T_NEW, pvStart, pvEnd);
		else if ((d == 2 || d == 3) && (pvStart != dragStart || pvEnd != dragEnd)) event_moved (dragEv, dragStart, pvStart, pvEnd);
		ui_view_dirty (g_vgrid);
	}
};
static TimeGrid *g_grid;

static long grid_paint (long, long w, long h) { g_grid->width = (int) w; g_grid->height = (int) h; g_grid->draw (); return 0; }
static long grid_mouse (long, long kind, long x, long y, long info)
{
	if (kind == 1) g_grid->press ((int) x, (int) y, (int) info);
	else if (kind == 0 && g_grid->drag) g_grid->move ((int) x, (int) y);
	else if (kind == 2) g_grid->release ();
	return 0;
}
static long grid_key (long, long hid, long, long)
{
	if (hid == 0x4B) { g_grid->scrollY -= g_grid->bodyH () * 3 / 4; g_grid->sync_bar (); ui_view_dirty (g_vgrid); return 0; }
	if (hid == 0x4E) { g_grid->scrollY += g_grid->bodyH () * 3 / 4; g_grid->sync_bar (); ui_view_dirty (g_vgrid); return 0; }
	if (hid == 0x28 && g_sel >= 0) { todo (T_OPEN, g_sel, g_selStart); return 0; }
	return -1;
}
static long grid_wheel (long, long n) { g_grid->scrollY -= (int) n * g_grid->hourH; g_grid->sync_bar (); ui_view_dirty (g_vgrid); return 0; }

// ---- the month grid ---------------------------------------------------------------------------------------

#define MHDR	30

struct MonthGrid
{
	int year = 2026, month = 1, width = 0, height = 0;
	int sel = 0;					// the day selected (a day number)
	struct Hit { int x, y, w, h, ev, start; }; Hit hits[300]; int nhits = 0;
	int first () const { int d = days_from_civil (year, month, 1); return d - wday (d); }
	int cellW () const { return (width - 2) / 7; }
	int cellH () const { return (height - MHDR - 1) / 6; }
	static bool touches (const Occ &o, int dn)
	{ return o.start < (dn + 1) * 1440 && (o.end > dn * 1440 || (o.end == o.start && o.start >= dn * 1440)); }
	void draw ()
	{
		ui_fill (0, 0, width, height, C_FIELD);
		nhits = 0;
		int cw = cellW (), ch = cellH (), d0 = first (), fh = 16;
		for (int i = 0; i < 7; i++) text_l (1 + i * cw + 10, 0, MHDR, WD3[i], soft_ink (), 2);
		static Occ occ[800];
		int n = occurrences (d0, d0 + 42, occ, 800);
		int rows = (ch - 30) / (fh + 3);
		for (int r = 0; r < 6; r++)
			for (int c = 0; c < 7; c++)
			{
				int dn = d0 + r * 7 + c, x = 1 + c * cw, y = MHDR + r * ch;
				bool in = dn_m (dn) == month, today = dn == g_today;
				unsigned bg = !in ? mix (C_FIELD, C_TEXT, 12) : c >= 5 ? mix (C_FIELD, C_TEXT, 6) : C_FIELD;
				if (dn == sel) bg = mix (bg, C_ACCENT, 22);
				ui_fill (x, y, cw, ch, bg);
				ui_fill (x, y, cw, 1, grid_line ());
				ui_fill (x, y, 1, ch, grid_line ());
				char num[20] = "";
				scatn (num, sizeof num, dn_d (dn));
				if (dn_d (dn) == 1) { scat (num, sizeof num, " "); scat (num, sizeof num, MON3[dn_m (dn) - 1]); }
				if (today)
				{
					int w = text_w (num, 2) + 14; if (w < 24) w = 24;
					rbox (x + 5, y + 4, w, 22, 11, C_ACCENT);
					text_c (x + 5, y + 4, w, 22, num, ink_on (C_ACCENT), 2);
				}
				else text_l (x + 10, y + 4, 22, num, in ? C_TEXT : soft_ink (), 2);
				// The day's events.
				int shown = 0, total = 0, yy = y + 29;
				for (int k = 0; k < n; k++) if (touches (occ[k], dn)) total++;
				int room = total > rows ? rows - 1 : rows;	// (a row for "+N autres")
				for (int k = 0; k < n; k++)
				{
					if (!touches (occ[k], dn) || shown >= room) continue;
					const Event &e = g_ev[occ[k].ev];
					unsigned col = cat_color (e.cat);
					bool selEv = occ[k].ev == g_sel && occ[k].start == g_selStart;
					int ex = x + 4, ew = cw - 8, eh = fh + 1;
					if (e.allDay || occ[k].end - occ[k].start >= 1440)
					{
						unsigned f = selEv ? col : mix (C_FIELD, col, 170);
						rbox (ex, yy, ew, eh, 4, f);
						text_fit (ex + 6, yy, ew - 9, eh, e.title, ink_on (f), 2);
					}
					else
					{
						if (selEv) rbox (ex, yy, ew, eh, 4, mix (C_FIELD, col, 60));
						disc (ex + 3, yy + (eh - 7) / 2, 7, col);
						char t[140]; fmt_hm (occ[k].start, t, sizeof t); scat (t, sizeof t, " ");
						int tw = ew >= 150 ? text_w (t) : 0;	// (a narrow cell: the title alone)
						if (tw) text_l (ex + 14, yy, eh, t, soft_ink ());
						text_fit (ex + 14 + tw, yy, ew - 16 - tw, eh, e.title, C_TEXT);
					}
					if (nhits < 300) { Hit &h = hits[nhits++]; h.x = ex; h.y = yy; h.w = ew; h.h = eh; h.ev = occ[k].ev; h.start = occ[k].start; }
					yy += eh + 2; shown++;
				}
				if (total > shown)
				{
					char t[24] = "+"; scatn (t, sizeof t, total - shown); scat (t, sizeof t, total - shown > 1 ? TR (" autres") : TR (" autre"));
					text_l (x + 8, yy, fh + 1, t, soft_ink (), 2);
				}
			}
		ui_fill (1 + 7 * cw, MHDR, 1, 6 * ch, grid_line ());
		ui_fill (1, MHDR + 6 * ch, 7 * cw, 1, grid_line ());
	}
	void press (int mx, int my, int clicks)
	{
		bool dbl = (clicks & 0xFF) >= 2;
		for (int i = nhits - 1; i >= 0; i--)
		{
			const Hit &h = hits[i];
			if (mx >= h.x && mx < h.x + h.w && my >= h.y && my < h.y + h.h)
			{
				g_sel = h.ev; g_selStart = h.start;
				if (dbl) todo (T_OPEN, h.ev, h.start);
				ui_view_dirty (g_vmonth);
				return;
			}
		}
		if (my < MHDR) return;
		int c = (mx - 1) / cellW (), r = (my - MHDR) / cellH ();
		if (c > 6) c = 6;
		if (r > 5) r = 5;
		int dn = first () + r * 7 + c;
		sel = dn; g_sel = -1;
		if (dbl) goto_day (dn, 0);
		ui_view_dirty (g_vmonth);
	}
};
static MonthGrid *g_month;
static long month_paint (long, long w, long h) { g_month->width = (int) w; g_month->height = (int) h; g_month->draw (); return 0; }
static long month_mouse (long, long kind, long x, long y, long info) { if (kind == 1) g_month->press ((int) x, (int) y, (int) info); return 0; }
static long month_wheel (long, long n) { goto_day (add_months (days_from_civil (g_month->year, g_month->month, 1), n > 0 ? -1 : 1), -1); return 0; }

// ---- the categories ------------------------------------------------------------------------------------

#define CATH	26
static int g_catHot = -1;
static const int HEADH = 22;

static void heading (int w, const char *s, const char *right)
{
	text_l (2, 0, HEADH - 2, s, face_soft (), 2);
	if (right[0]) text_l (w - 2 - text_w (right), 0, HEADH - 2, right, face_soft ());
	ui_fill (0, HEADH - 1, w, 1, mix (C_BG, C_TEXT, 40));
}
static long cats_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_BG);
	heading ((int) w, TR ("CALENDRIERS"), "");
	for (int i = 0; i < g_ncat; i++)
	{
		int y = HEADH + 2 + i * CATH;
		if (i == g_catHot) rbox (0, y, (int) w, CATH, 5, mix (C_BG, C_TEXT, 22));
		unsigned c = g_cat[i].color;
		int bx = 6, by = y + (CATH - 16) / 2;
		rbox (bx, by, 16, 16, 4, c);
		if (!g_cat[i].shown) rbox (bx + 2, by + 2, 12, 12, 3, C_BG);
		text_fit (30, y, (int) w - 34, CATH, g_cat[i].name, C_TEXT);
	}
	return 0;
}
static long cats_mouse (long, long kind, long x, long y, long)
{
	int r = kind == 3 ? -1 : ((int) y - HEADH - 2) / CATH;
	if (y < HEADH + 2 || r >= g_ncat) r = -1;
	if (r != g_catHot) { g_catHot = r; ui_view_dirty (g_vcats); }
	if (kind == 1 && r >= 0) { g_cat[r].shown = !g_cat[r].shown; data_changed (true); }
	return 0;
}

// ---- the tasks ---------------------------------------------------------------------------------------------

#define TASKH	28
static int g_order[512], g_nord, g_ttop, g_thot = -1;
static bool task_less (int a, int b)
{
	const Task &x = g_tk[a], &y = g_tk[b];
	if (x.done != y.done) return !x.done;
	int dx = x.due ? x.due : 1 << 30, dy = y.due ? y.due : 1 << 30;
	return dx < dy;
}
static void task_sort ()			// to do first (the earliest due first, no date last), done at the end
{
	g_nord = 0;
	for (int i = 0; i < g_ntk && g_nord < 512; i++) g_order[g_nord++] = i;
	for (int i = 1; i < g_nord; i++)
	{
		int t = g_order[i], j = i;
		while (j > 0 && task_less (t, g_order[j - 1])) { g_order[j] = g_order[j - 1]; j--; }
		g_order[j] = t;
	}
}
static long tasks_paint (long, long wl, long hl)
{
	int w = (int) wl, h = (int) hl;
	task_sort ();
	ui_fill (0, 0, w, h, C_BG);
	int open = 0; for (int i = 0; i < g_ntk; i++) if (!g_tk[i].done) open++;
	char c[16] = ""; if (open) scatn (c, sizeof c, open);
	heading (w, TR ("T\xC3\x82" "CHES"), c);
	int rows = (h - HEADH - 2) / TASKH;
	if (g_ttop > g_nord - rows) g_ttop = g_nord - rows;
	if (g_ttop < 0) g_ttop = 0;
	if (g_nord == 0) { text_c (0, HEADH + 6, w, 16, TR ("Rien \xC3\xA0 faire"), face_soft ()); return 0; }
	for (int r = 0; r < rows && g_ttop + r < g_nord; r++)
	{
		int i = g_order[g_ttop + r], y = HEADH + 2 + r * TASKH;
		const Task &t = g_tk[i];
		if (g_ttop + r == g_thot) rbox (0, y, w, TASKH, 5, mix (C_BG, C_TEXT, 22));
		int cy = y + (TASKH - 16) / 2;
		if (t.done) disc (6, cy, 16, C_ACCENT);
		else { disc (6, cy, 16, t.cat >= 0 ? cat_color (t.cat) : face_soft ()); disc (8, cy + 2, 12, C_BG); }
		char due[24] = ""; unsigned dc = face_soft ();
		if (t.due && !t.done)
		{
			if (t.due == g_today) { scpy (due, sizeof due, TR ("Aujourd'hui")); dc = C_ACCENT; }
			else if (t.due == g_today + 1) scpy (due, sizeof due, TR ("Demain"));
			else { scatn (due, sizeof due, dn_d (t.due)); scat (due, sizeof due, " "); scat (due, sizeof due, MON3[dn_m (t.due) - 1]); if (t.due < g_today) dc = RED; }
		}
		int dw = due[0] ? text_w (due, t.due <= g_today ? 2 : 0) + 8 : 0;
		unsigned ink = t.done ? face_soft () : C_TEXT;
		text_fit (30, y, w - 32 - dw, TASKH, t.title, ink);
		if (t.done) { int tw = text_w (t.title); if (tw > w - 32 - dw) tw = w - 32 - dw; ui_fill (30, y + TASKH / 2, tw, 1, ink); }
		if (due[0]) text_l (w - dw + 2, y, TASKH, due, dc, t.due <= g_today ? 2 : 0);
	}
	return 0;
}
static long tasks_mouse (long, long kind, long x, long y, long info)
{
	int r = kind == 3 || y < HEADH + 2 ? -1 : g_ttop + ((int) y - HEADH - 2) / TASKH;
	if (r >= g_nord) r = -1;
	if (r != g_thot) { g_thot = r; ui_view_dirty (g_vtasks); }
	if (kind != 1 || r < 0) return 0;
	int i = g_order[r];
	if (x < 28) { g_tk[i].done = !g_tk[i].done; data_changed (true); return 0; }
	if ((info & 0xFF) >= 2) todo (T_TASK, i);
	return 0;
}
static long tasks_wheel (long, long n) { g_ttop -= (int) n; ui_view_dirty (g_vtasks); return 0; }

#endif
