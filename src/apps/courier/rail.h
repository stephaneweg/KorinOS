//
// rail.h -- Courier's sidebar: a rail of three places (Collections, Environments, History) and the
// list of the place chosen -- the collections as a tree (folders, requests with their method), the
// environments (the globals first, the one chosen ticked), the history by day -- a filter above it,
// each row's menu (a right click, or its "..." button). Onyx's Apps/courier/rail.h on a ui_view.
//
#ifndef _courier_rail_h
#define _courier_rail_h

#include "views.h"

namespace cr {

enum { SB_COLL, SB_ENV, SB_HIST };
enum { R_COLL, R_FOLDER, R_REQ, R_GLOBALS, R_ENV, R_DAY, R_HIST, R_EMPTY };

struct SideRow { int kind; void *ptr; int idx; int depth; };

enum { HEAD_H = 40, ROW_H = 30 };
static long g_side, g_sFilter, g_sBar;
static int s_mode, s_top, s_sw, s_sh, s_hot = -1, s_hotRail = -1, s_railW = 78, s_sx, s_sy;
static bool s_hotMore, s_hotHead;
static Vec<SideRow> s_rows;
static Str s_filter, s_tip;

static inline int list_y () { return HEAD_H + ctl_h () + 8; }
static inline int vis_rows () { return imax (1, (s_sh - list_y ()) / ROW_H); }
static inline bool matches (const char *s) { return s_filter.empty () || s_findi (s, (int) strlen (s), s_filter.c ()) >= 0; }
static bool any_match (Item *it)
{
	if (matches (it->name.c ())) return true;
	for (int i = 0; i < it->kids.size (); i++) if (any_match (it->kids[i])) return true;
	return false;
}
static void add_items (Item *parent, int depth)
{
	bool filt = !s_filter.empty ();
	// the folders first, as Postman lists them
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < parent->kids.size (); i++)
		{
			Item *it = parent->kids[i];
			if ((pass == 0) != it->folder) continue;
			if (filt && !any_match (it)) continue;
			SideRow r; r.kind = it->folder ? R_FOLDER : R_REQ; r.ptr = it; r.idx = 0; r.depth = depth;
			s_rows.push (r);
			if (it->folder && (it->open || filt)) add_items (it, depth + 1);
		}
}
static void side_bar_sync ()
{
	bool bar = s_rows.size () > vis_rows ();
	ui_w_show (g_sBar, bar);
	if (bar) { ui_w_set_range (g_sBar, 0, s_rows.size (), vis_rows (), 1); ui_w_set_value (g_sBar, s_top); }
}
static void side_rebuild ()
{
	s_rows.clear ();
	SideRow r; r.idx = 0; r.depth = 0; r.ptr = 0;
	if (s_mode == SB_COLL)
	{
		bool filt = !s_filter.empty ();
		for (int i = 0; i < g_store.colls.size (); i++)
		{
			Collection *c = g_store.colls[i];
			if (filt && !matches (c->name.c ()) && !any_match (&c->root)) continue;
			r.kind = R_COLL; r.ptr = c; r.idx = i; r.depth = 0; s_rows.push (r);
			if (c->root.open || filt) add_items (&c->root, 1);
		}
		if (!s_rows.size ()) { r.kind = R_EMPTY; r.ptr = 0; s_rows.push (r); }
	}
	else if (s_mode == SB_ENV)
	{
		if (matches (TR ("Globals"))) { r.kind = R_GLOBALS; r.idx = -1; s_rows.push (r); }
		for (int i = 0; i < g_store.envs.size (); i++)
			if (matches (g_store.envs[i]->name.c ())) { r.kind = R_ENV; r.idx = i; r.ptr = g_store.envs[i]; s_rows.push (r); }
	}
	else
	{
		int lastDay = -1;
		long long now = now_unix ();
		for (int i = 0; i < g_store.history.size (); i++)
		{
			HistoryEntry &h = g_store.history[i];
			if (!matches (h.req.url.c ()) && !matches (h.req.method.c ())) continue;
			int day = (int) (h.when / 86400);
			if (day != lastDay) { r.kind = R_DAY; r.idx = day; r.depth = (int) (now / 86400) - day; s_rows.push (r); lastDay = day; }
			r.kind = R_HIST; r.idx = i; r.depth = 0; s_rows.push (r);
		}
		if (!g_store.history.size ()) { r.kind = R_EMPTY; s_rows.push (r); }
	}
	s_top = iclamp (s_top, 0, imax (0, s_rows.size () - vis_rows ()));
	side_bar_sync ();
	ui_view_dirty (g_side);
}
static void side_mode (int m)
{
	s_mode = m; s_top = 0;
	set_text (g_sFilter, ""); s_filter.clear ();
	side_rebuild ();
}
// is this row the tab shown's?
static bool row_current (const SideRow &r)
{
	Tab *t = cur_tab (); if (!t) return false;
	if (r.kind == R_REQ && t->kind == TAB_REQUEST) return t->itemId.eq (((Item *) r.ptr)->id.c ());
	if (r.kind == R_COLL && t->kind == TAB_COLL) return t->collId.eq (((Collection *) r.ptr)->id.c ());
	if (r.kind == R_ENV && t->kind == TAB_ENV) return t->envId.eq (((Environment *) r.ptr)->id.c ());
	if (r.kind == R_GLOBALS && t->kind == TAB_ENV) return t->envId.eq ("globals");
	return false;
}
static void draw_method (int x, int y, int h, const char *m, int w)
{
	char b[8]; s_copy (b, method_short (m), sizeof b);
	for (int i = 0; b[i]; i++) if (b[i] >= 'a' && b[i] <= 'z') b[i] -= 32;
	text_v (x + w - tw (b, F_SMALL, 1), y, h, b, method_color (m), F_SMALL, 1);
}
static void draw_row (SideRow &r, int y, int rw, bool hot)
{
	unsigned bg = C_SIDE, ink = C_TEXT, dim = C_DIM;
	int x = s_railW + 6;
	bool cur = row_current (r);
	unsigned here = bg;
	if (r.kind != R_DAY && r.kind != R_EMPTY)
	{
		if (cur) { here = mix (bg, C_ACCENT, 60); rbox (x - 2, y + 1, rw - x - 2, ROW_H - 2, 6, here); }
		else if (hot) { here = mix (bg, ink, 18); rbox (x - 2, y + 1, rw - x - 2, ROW_H - 2, 6, here); }
	}
	int more = (hot || cur) && r.kind != R_DAY && r.kind != R_EMPTY ? 26 : 0;
	switch (r.kind)
	{
	case R_COLL: case R_FOLDER:
	{
		bool coll = r.kind == R_COLL;
		const char *nm = coll ? ((Collection *) r.ptr)->name.c () : ((Item *) r.ptr)->name.c ();
		bool open = coll ? ((Collection *) r.ptr)->root.open : ((Item *) r.ptr)->open;
		if (!s_filter.empty ()) open = true;
		int ix = x + r.depth * 12;
		icon (open ? I_CHEV_D : I_CHEV_R, ix, y + ROW_H / 2 - 7, 14, dim);
		icon (coll ? I_COLL : I_FOLDER, ix + 16, y + ROW_H / 2 - 8, 16, coll ? col_accent_text () : mix (bg, ink, 130), here);
		text_v (ix + 37, y, ROW_H, nm, ink, F_UI, coll ? 1 : 0, rw - ix - 37 - 6 - more);
		break;
	}
	case R_REQ:
	{
		Item *it = (Item *) r.ptr;
		int ix = x + r.depth * 12;
		draw_method (ix, y, ROW_H, it->req.method.c (), 34);
		text_v (ix + 40, y, ROW_H, it->name.c (), ink, F_UI, 0, rw - ix - 40 - 6 - more);
		break;
	}
	case R_GLOBALS: case R_ENV:
	{
		bool glob = r.kind == R_GLOBALS;
		const char *nm = glob ? TR ("Globals") : ((Environment *) r.ptr)->name.c ();
		icon (glob ? I_GLOBE : I_ENV, x + 4, y + ROW_H / 2 - 8, 16, dim, here);
		text_v (x + 26, y, ROW_H, nm, ink, F_UI, glob ? 1 : 0, rw - x - 26 - 28 - more);
		if (!glob && r.idx == g_store.activeEnv) icon (I_CHECK, rw - 20 - more, y + ROW_H / 2 - 7, 14, col_accent_text ());
		break;
	}
	case R_DAY:
	{
		char b[48];
		if (r.depth == 0) s_copy (b, TR ("Today"), sizeof b);
		else if (r.depth == 1) s_copy (b, TR ("Yesterday"), sizeof b);
		else { int yy, mm, dd; civil_from_days (r.idx, &yy, &mm, &dd); snprintf (b, sizeof b, "%02d/%02d/%04d", dd, mm, yy); }
		text_v (x + 4, y + 4, ROW_H - 4, b, dim, F_UI, 1);
		break;
	}
	case R_HIST:
	{
		HistoryEntry &h = g_store.history[r.idx];
		draw_method (x, y, ROW_H, h.req.method.c (), 34);
		char st[16]; st[0] = 0;
		if (h.status > 0) snprintf (st, sizeof st, "%d", h.status); else if (h.status < 0) s_copy (st, "ERR", sizeof st);
		int sw = st[0] ? tw (st) + 6 : 0;
		const char *u = h.req.url.c ();
		if (s_startsi (u, "https://")) u += 8; else if (s_startsi (u, "http://")) u += 7;
		text_v (x + 40, y, ROW_H, u, ink, F_UI, 0, rw - x - 40 - 8 - sw - more);
		if (st[0] && !more) text_v (rw - 6 - sw, y, ROW_H, st, h.status >= 0 ? status_color (h.status) : on_field (C_BAD));
		break;
	}
	case R_EMPTY:
	{
		const char *a = s_mode == SB_COLL ? TR ("No collections yet") : TR ("Nothing sent yet");
		const char *b = s_mode == SB_COLL ? TR ("Save a request (Ctrl+S), click +, or import a Postman collection.") : TR ("The requests sent are listed here, by day.");
		text (x + 6, y + 12, a, ink, F_UI, 1);
		text_wrap (x + 6, y + 12 + fh () + 6, s_sw - x - 16, b, dim);
		break;
	}
	}
	if (more) icon (I_MORE, rw - 26, y + ROW_H / 2 - 9, 18, dim);
}
static int head_w () { return s_mode != SB_HIST ? 32 : tw (TR ("Clear")) + 16; }	// the head's button: + (New), Clear
static void side_paint (long, long W, long H)
{
	unsigned bg = C_SIDE;
	ui_fill (0, 0, W, H, bg);
	// the rail
	unsigned rail = mix (bg, C_TEXT, 10);
	ui_fill (0, 0, s_railW, H, rail);
	ui_fill (s_railW, 0, 1, H, C_LINE);
	static const char *const NAMES[] = { "Collections", "Environments", "History" };
	static const int IC[] = { I_COLL, I_ENV, I_HISTORY };
	bool wide = s_railW >= 70;
	int step = wide ? 66 : 48;
	for (int i = 0; i < 3; i++)
	{
		int y = 8 + i * step, bh = step - 6;
		bool s = i == s_mode, h = i == s_hotRail;
		unsigned box = s ? mix (rail, C_ACCENT, 70) : h ? mix (rail, C_TEXT, 20) : rail;
		if (s || h) rbox (5, y, s_railW - 10, bh, 8, box);
		unsigned c = s ? ink_on (box) : C_DIM;
		icon (IC[i], s_railW / 2 - 10, y + (wide ? 8 : (bh - 20) / 2), 20, c, box);
		if (wide) text_c (0, y + 34, s_railW, 16, TR (NAMES[i]), c, F_SMALL);
	}
	// the list's head
	text_v (s_railW + 12, 0, HEAD_H, TR (NAMES[s_mode]), C_TEXT, F_UI, 1, (int) W - s_railW - 20 - head_w ());
	if (s_hotHead) rbox ((int) W - 8 - head_w (), 7, head_w (), HEAD_H - 14, 6, mix (bg, C_TEXT, 24));
	if (s_mode != SB_HIST) icon (I_PLUS, (int) W - 8 - 25, HEAD_H / 2 - 9, 18, C_TEXT);
	else text_c ((int) W - 8 - head_w (), 0, head_w (), HEAD_H, TR ("Clear"), C_DIM);
	// the rows
	int y0 = list_y ();
	ui_fill (s_railW + 1, y0 - 4, W - s_railW - 1, 1, C_LINE);
	int vr = vis_rows ();
	int rw = (int) W - (s_rows.size () > vr ? 12 : 0);
	ui_clip (s_railW + 1, y0, W - s_railW - 1, H - y0);
	for (int k = 0; k <= vr; k++)
	{
		int i = s_top + k;
		if (i >= s_rows.size ()) break;
		draw_row (s_rows[i], y0 + k * ROW_H, rw, i == s_hot);
	}
	ui_clip (0, 0, 0, 0);
}
static long side_mouse (long v, long kind, long mx, long my, long info)
{
	bool wide = s_railW >= 70;
	int step = wide ? 66 : 48;
	int hr = kind != 3 && mx < s_railW && my >= 8 && my < 8 + 3 * step ? ((int) my - 8) / step : -1;
	int y0 = list_y ();
	int h = kind != 3 && mx > s_railW && my >= y0 ? s_top + ((int) my - y0) / ROW_H : -1;
	if (h >= s_rows.size ()) h = -1;
	int rw = s_sw - (s_rows.size () > vis_rows () ? 12 : 0);
	bool more = h >= 0 && mx >= rw - 32 && mx < rw - 2;
	bool hh = kind != 3 && my < HEAD_H && mx >= s_sw - 8 - head_w () && mx < s_sw - 8;
	if (h != s_hot || hr != s_hotRail || more != s_hotMore || hh != s_hotHead) { s_hot = h; s_hotRail = hr; s_hotMore = more; s_hotHead = hh; ui_view_dirty (v); }
	if (h >= 0 && s_rows[h].kind == R_HIST) { s_tip = g_store.history[s_rows[h].idx].req.url; ui_w_tip (v, (long) s_tip.c ()); }
	else ui_w_tip (v, 0);
	if (kind == 1)
	{
		int b = (int) (info >> 8 & 255), clicks = (int) (info & 255);
		if (hh && (b & 1)) later (L_SIDE_HEAD);
		else if (hr >= 0 && (b & 1)) later (L_SIDE_RAIL, hr);
		else if (h >= 0 && ((b & 2) || more)) later (L_SIDE_MENU, h, 0, (int) mx, (int) my);
		else if (h >= 0 && (b & 1)) later (L_SIDE_ROW, h, clicks >= 2, (int) mx);
	}
	return 0;
}
static long side_wheel (long, long steps)
{
	int t = iclamp (s_top + (int) steps * 2, 0, imax (0, s_rows.size () - vis_rows ()));
	if (t != s_top) { s_top = t; side_bar_sync (); ui_view_dirty (g_side); }
	return 0;
}
static void side_click (const SideRow &r, int mx, bool dbl)
{
	switch (r.kind)
	{
	case R_COLL:
	{
		Collection *c = (Collection *) r.ptr;
		if (dbl) { app_open_coll (c); return; }
		c->root.open = !c->root.open; side_rebuild ();
		break;
	}
	case R_FOLDER: { Item *it = (Item *) r.ptr; it->open = !it->open; side_rebuild (); break; }
	case R_REQ: app_open_item ((Item *) r.ptr); break;
	case R_GLOBALS: app_open_env (-1); break;
	case R_ENV:
		if (mx >= s_sw - 56 && mx < s_sw - 30) app_set_env (r.idx == g_store.activeEnv ? -1 : r.idx);
		else app_open_env (r.idx);
		break;
	case R_HIST: app_open_history (r.idx); break;
	}
	ui_view_dirty (g_side);
}
static long popup (int x, int y, std::initializer_list<const char *> items, int width = 200)
{
	char t[1200]; int n = 0;
	for (const char *s : items)
	{
		int l = (int) strlen (s);
		if (n + l + 2 >= (int) sizeof t) break;
		memcpy (t + n, s, (size_t) l + 1); n += l + 1;
	}
	t[n] = 0;
	return ui_popup_local (x, y, (long) t, width);
}
static void side_menu (const SideRow &r, int x, int y)
{
	enum { OPEN = 1, NEWREQ, NEWFOLDER, RENAME, DUP, DEL, EXPORT, UP, DOWN, SETENV, UNSETENV, CLEARH };
	long c = -1; int id = -1;
	Str a; const char *sep = "-";
	switch (r.kind)
	{
	case R_COLL:
	{
		static const int IDS[] = { OPEN, 0, NEWREQ, NEWFOLDER, 0, RENAME, DUP, EXPORT, 0, DEL };
		c = popup (x, y, { TR ("Open (variables, auth)"), sep, TR ("Add Request"), TR ("Add Folder"), sep, TR ("Rename..."), TR ("Duplicate"), TR ("Export..."), sep, TR ("Delete...") }, 260);
		if (c >= 0) id = IDS[c];
		break;
	}
	case R_FOLDER:
	{
		static const int IDS[] = { NEWREQ, NEWFOLDER, 0, RENAME, DUP, UP, DOWN, 0, DEL };
		c = popup (x, y, { TR ("Add Request"), TR ("Add Folder"), sep, TR ("Rename..."), TR ("Duplicate"), TR ("Move Up"), TR ("Move Down"), sep, TR ("Delete...") });
		if (c >= 0) id = IDS[c];
		break;
	}
	case R_REQ:
	{
		static const int IDS[] = { OPEN, 0, RENAME, DUP, UP, DOWN, 0, DEL };
		c = popup (x, y, { TR ("Open"), sep, TR ("Rename..."), TR ("Duplicate"), TR ("Move Up"), TR ("Move Down"), sep, TR ("Delete...") });
		if (c >= 0) id = IDS[c];
		break;
	}
	case R_GLOBALS:
		c = popup (x, y, { TR ("Open"), TR ("Export...") });
		if (c >= 0) id = c == 0 ? OPEN : EXPORT;
		break;
	case R_ENV:
	{
		bool on = r.idx == g_store.activeEnv;
		static const int IDS[] = { OPEN, 0, 0, RENAME, DUP, EXPORT, 0, DEL };
		c = popup (x, y, { TR ("Open"), on ? TR ("Stop Using It") : TR ("Use It (Set Active)"), sep, TR ("Rename..."), TR ("Duplicate"), TR ("Export..."), sep, TR ("Delete...") }, 240);
		if (c == 1) id = on ? UNSETENV : SETENV; else if (c >= 0) id = IDS[c];
		break;
	}
	case R_HIST:
		c = popup (x, y, { TR ("Open"), TR ("Delete"), sep, TR ("Clear the History...") });
		if (c >= 0) { static const int IDS[] = { OPEN, DEL, 0, CLEARH }; id = IDS[c]; }
		break;
	default: return;
	}
	if (id <= 0) return;
	Item *it = (r.kind == R_FOLDER || r.kind == R_REQ) ? (Item *) r.ptr : 0;
	Collection *cl = r.kind == R_COLL ? (Collection *) r.ptr : 0;
	switch (id)
	{
	case OPEN:
		if (cl) app_open_coll (cl); else if (r.kind == R_REQ) app_open_item (it);
		else if (r.kind == R_GLOBALS) app_open_env (-1); else if (r.kind == R_ENV) app_open_env (r.idx);
		else if (r.kind == R_HIST) app_open_history (r.idx);
		break;
	case NEWREQ: app_new_request (cl ? &cl->root : it); break;
	case NEWFOLDER: app_new_folder (cl ? &cl->root : it); break;
	case RENAME: if (cl) app_rename_coll (cl); else if (it) app_rename_item (it); else if (r.kind == R_ENV) app_rename_env (r.idx); break;
	case DUP: if (cl) app_duplicate_coll (cl); else if (it) app_duplicate_item (it); else if (r.kind == R_ENV) app_duplicate_env (r.idx); break;
	case DEL:
		if (cl) app_delete_coll (cl); else if (it) app_delete_item (it); else if (r.kind == R_ENV) app_delete_env (r.idx);
		else if (r.kind == R_HIST) app_delete_history (r.idx);
		break;
	case EXPORT: if (cl) app_export_coll (cl); else if (r.kind == R_ENV) app_export_env (r.idx); else if (r.kind == R_GLOBALS) app_export_env (-1); break;
	case UP: app_move_item (it, -1); break;
	case DOWN: app_move_item (it, 1); break;
	case SETENV: app_set_env (r.idx); break;
	case UNSETENV: app_set_env (-1); break;
	case CLEARH: app_clear_history (); break;
	}
}
static void side_build ()
{
	int ch = ctl_h ();
	g_side = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_side, (long) side_paint, (long) side_mouse, 0, (long) side_wheel);
	g_sFilter = ui_textbox (0, 0, 10, ch, 0); ui_w_set_parent (g_sFilter, g_side); ui_textbox_hint (g_sFilter, (long) TR ("Filter"));
	g_sBar = ui_scrollbar (0, 0, 12, 10, 0); ui_w_set_parent (g_sBar, g_side); ui_w_show (g_sBar, 0);
}
static void side_place (int x, int y, int w, int h)
{
	s_sx = x; s_sy = y; s_sw = w; s_sh = h;
	s_railW = w >= 280 ? imax (78, tw (TR ("Environments"), F_SMALL) + 12) : 44;
	int ch = ctl_h ();
	ui_w_move (g_side, x, y, w, h);
	ui_w_move (g_sFilter, x + s_railW + 8, y + HEAD_H, w - s_railW - 16, ch);
	ui_w_move (g_sBar, x + w - 12, y + list_y (), 12, h - list_y ());
	s_top = iclamp (s_top, 0, imax (0, s_rows.size () - vis_rows ()));
	side_bar_sync ();
	ui_view_dirty (g_side);
}
static bool side_control (long w, long code)
{
	if (w == g_sFilter) { if (code == UN_CHANGE) { s_filter = tx (w); s_top = 0; side_rebuild (); } return true; }
	if (w == g_sBar) { if (code == UN_CHANGE) { s_top = (int) ui_w_get_value (w); ui_view_dirty (g_side); } return true; }
	return false;
}

} // namespace cr

#endif
