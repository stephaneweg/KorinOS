//
// widgets.h -- what UIKit does not have, drawn or put together by Courier: a bar of named tabs with their counts and
// marks (the sections of a request, of a response, of a collection), the open documents' tabs, the key / value table
// (UIKit has no editable grid: a pool of real fields for the rows that fit, bound to the data as it scrolls, the last
// row blank -- writing in it adds one), and the authorization panel. Plain functions on structs of handles.
//
#ifndef _courier_widgets_h
#define _courier_widgets_h

#include "ui.h"

namespace cr {

// ---- the tabs of a section: a view ------------------------------------------------------------------------------
enum { BAR_MAX = 8 };
struct Bar
{
	long v; int n, sel, hot;
	const char *name[BAR_MAX];
	int count[BAR_MAX];			// -1: none
	bool dot[BAR_MAX];
	unsigned badge[BAR_MAX];		// the count's colour (0: the usual)
	int x[BAR_MAX + 1];
	void (*extra) (long w, long h);		// (what is drawn at its right)
};
static Bar *s_bars[4]; static int s_nbars;
static Bar *bar_of (long v) { for (int i = 0; i < s_nbars; i++) if (s_bars[i]->v == v) return s_bars[i]; return 0; }
static void bar_measure (Bar &b)
{
	int pad = 18, wid[BAR_MAX], total = 0;
	for (int i = 0; i < b.n; i++)
	{
		wid[i] = tw (TR (b.name[i]), F_UI, 1);
		if (b.count[i] >= 0) { char c[12]; snprintf (c, sizeof c, "%d", b.count[i]); wid[i] += tw (c, F_SMALL, 1) + 6; }
		if (b.dot[i]) wid[i] += 10;
		total += wid[i];
	}
	long w = 0, h = 0; if (b.v) uikit_canvas_pixels (b.v, &w, &h);
	if (b.n && w > 0 && total + 2 * pad * b.n + 8 > w) pad = imax (3, ((int) w - 8 - total) / (2 * b.n));
	int x = 8;
	for (int i = 0; i < b.n; i++) { b.x[i] = x; x += wid[i] + 2 * pad; }
	b.x[b.n] = x;
}
static void bar_paint (long v, long w, long h)
{
	Bar *bp = bar_of (v); if (!bp) return;
	Bar &b = *bp;
	ui_fill (0, 0, w, h, C_FACE);
	ui_fill (0, h - 1, w, 1, C_LINE);
	bar_measure (b);
	for (int i = 0; i < b.n; i++)
	{
		int x = b.x[i], wd = b.x[i + 1] - x;
		bool s = i == b.sel;
		if (i == b.hot && !s) rbox (x + 2, 4, wd - 4, (int) h - 8, 5, mix (C_FACE, C_TEXT, 16));
		int nw = tw (TR (b.name[i]), F_UI, 1), extra = wd - nw;
		int tx0 = x + extra / 2;
		char c[12] = ""; int cw = 0;
		if (b.count[i] >= 0) { snprintf (c, sizeof c, "%d", b.count[i]); cw = tw (c, F_SMALL, 1) + 6; tx0 -= (cw + (b.dot[i] ? 10 : 0)) / 2; }
		else if (b.dot[i]) tx0 -= 5;
		text_v (tx0, 0, (int) h - 2, TR (b.name[i]), s ? C_TEXT : C_DIM, F_UI, s ? 1 : 0);
		int ax = tx0 + nw + 4;
		if (cw) { text_v (ax, 0, (int) h - 2, c, b.badge[i] ? b.badge[i] : on_field (C_OK), F_SMALL, 1); ax += cw; }
		if (b.dot[i]) ui_vp_disc (V (ax + 3), V ((int) h / 2 - 1), V (3), on_field (C_OK));
		if (s) rbox (x + 6, (int) h - 3, wd - 12, 3, 1, C_ACCENT);
	}
	if (b.extra) b.extra (w, h);
}
static long bar_mouse (long v, long kind, long x, long, long info)
{
	Bar *bp = bar_of (v); if (!bp) return 0;
	Bar &b = *bp;
	int hit = -1;
	if (kind != 3) for (int i = 0; i < b.n; i++) if (x >= b.x[i] && x < b.x[i + 1]) hit = i;
	if (hit != b.hot) { b.hot = hit; ui_view_dirty (v); }
	if (kind == 1 && hit >= 0 && (info >> 8 & 1))
	{
		b.hot = -1;
		int k = 0; while (k < s_nbars && s_bars[k] != bp) k++;
		later (L_BAR, k, hit);
	}
	return 0;
}
static void bar_new (Bar &b, long panel, const char *names)		// "a|b|c": the words kept for TR
{
	memset (&b, 0, sizeof b);
	b.v = ui_view (0, 0, 10, 10, 0); b.hot = -1;
	ui_view_on (b.v, (long) bar_paint, (long) bar_mouse, 0, 0);
	in_panel (b.v, panel);
	char *copy = strdup (names), *p = copy;
	while (p && b.n < BAR_MAX)
	{
		char *e = strchr (p, '|'); if (e) *e = 0;
		b.name[b.n] = p; b.count[b.n] = -1; b.n++;
		p = e ? e + 1 : 0;
	}
	s_bars[s_nbars++] = &b;
}
static void bar_count (Bar &b, int i, int n) { if (b.count[i] != n) { b.count[i] = n; ui_view_dirty (b.v); } }
static void bar_mark (Bar &b, int i, bool on) { if (b.dot[i] != on) { b.dot[i] = on; ui_view_dirty (b.v); } }
static void bar_select (Bar &b, int i) { if (b.sel != i) { b.sel = i; ui_view_dirty (b.v); } }

// ---- the documents' tabs: a view --------------------------------------------------------------------------------
struct DocInfo { Str title, method; int kind; bool dirty; };
static long g_docs;
static Vec<DocInfo> s_docs;
static int s_docSel = -1, s_docHot = -1, s_docNewX, s_docFirst; static bool s_docHotClose, s_docHotNew;
enum { DOC_NEW_W = 34 };
static int doc_w (int W) { int n = imax (1, s_docs.size ()); return iclamp ((W - DOC_NEW_W - 8) / n, 96, 210); }
static int doc_first (int W)
{
	int w = doc_w (W), room = imax (1, (W - DOC_NEW_W - 8) / w);
	return s_docSel < room ? 0 : s_docSel - room + 1;
}
static void docs_paint (long, long W, long H)
{
	unsigned bg = C_FACE;
	ui_fill (0, 0, W, H, bg);
	ui_fill (0, H - 1, W, 1, C_LINE);
	int w = doc_w ((int) W), f = doc_first ((int) W), x = 0;
	s_docFirst = f;
	for (int i = f; i < s_docs.size (); i++)
	{
		if (x + w > W - DOC_NEW_W - 4) break;
		DocInfo &t = s_docs[i];
		bool s = i == s_docSel, h = i == s_docHot;
		unsigned tb = s ? mix (bg, C_TEXT, 14) : bg;
		if (s) { rbox (x + 1, 4, w - 2, (int) H - 4, 6, tb); rbox (x + 8, 3, w - 16, 2, 1, C_ACCENT); }
		else if (h) rbox (x + 2, 6, w - 4, (int) H - 10, 6, mix (bg, C_TEXT, 12));
		int tx0 = x + 10;
		if (t.kind == 0)
		{
			char mb[8]; s_copy (mb, method_short (t.method.c ()), sizeof mb);
			text_v (tx0, 2, (int) H - 2, mb, method_color (t.method.c ()), F_SMALL, 1);
			tx0 += tw (mb, F_SMALL, 1) + 6;
		}
		else { icon (t.kind == 1 ? I_ENV : I_COLL, tx0, (int) H / 2 - 7, 15, C_DIM, tb); tx0 += 20; }
		text_v (tx0, 2, (int) H - 2, t.title.c (), s ? C_TEXT : C_DIM, F_UI, 0, x + w - 24 - tx0);
		int cx = x + w - 22, cy = (int) H / 2 - 6;
		if (h && s_docHotClose) rbox (cx - 2, cy - 2, 18, 18, 4, mix (tb, C_TEXT, 40));
		if (t.dirty && !(h && s_docHotClose)) icon (I_DOT, cx, cy, 14, on_field (C_WARN));
		else if (h || s) icon (I_CLOSE, cx, cy, 14, C_DIM);
		x += w;
	}
	s_docNewX = x + 4;
	if (s_docHotNew) rbox (s_docNewX, 6, 28, (int) H - 10, 6, mix (bg, C_TEXT, 20));
	icon (I_PLUS, s_docNewX + 5, (int) H / 2 - 8, 18, C_TEXT);
}
static long docs_mouse (long v, long kind, long mx, long my, long info)
{
	long W = 0, H = 0; uikit_canvas_pixels (v, &W, &H);
	int w = doc_w ((int) W), f = s_docFirst;
	int i = f + (int) mx / w;
	bool overNew = mx >= s_docNewX && mx < s_docNewX + 28;
	if (kind == 3 || i >= s_docs.size () || overNew || ((int) mx / w) * w + w > W - DOC_NEW_W - 4) i = -1;
	bool cl = i >= 0 && mx >= (i - f) * w + w - 26 && mx < (i - f) * w + w - 4;
	if (kind == 3) overNew = false;
	if (i != s_docHot || cl != s_docHotClose || overNew != s_docHotNew) { s_docHot = i; s_docHotClose = cl; s_docHotNew = overNew; ui_view_dirty (v); }
	if (kind == 1)
	{
		int b = (int) (info >> 8 & 255);
		if (overNew && (b & 1)) later (L_DOC_NEW);
		else if (i >= 0 && (b & 4)) later (L_DOC_CLOSE, i);
		else if (i >= 0 && (b & 1)) later (cl ? L_DOC_CLOSE : L_DOC, i);
		else if (i >= 0 && (b & 2)) later (L_DOC_MENU, i, 0, (int) mx, (int) my);
	}
	return 0;
}

// ---- the key / value table ---------------------------------------------------------------------------------------
enum { KV_ROW = 32, KV_HEAD = 22, KV_MAXROWS = 14, KV_BAR = 14, KV_KIND = 58 };
struct KVRow { long on, key, value, desc, kind, del; int idx; };
struct KVTable
{
	long panel, head, bar, bulk;
	KVRow row[KV_MAXROWS]; int pool;
	KVList *rows;
	int top, x, y, w, h;
	bool showDesc, form, inBulk;
	const char *col[3], *ph[2];
	void (*changed) ();
};
static KVTable *s_kv[10]; static int s_nkv;

static int kv_vis (const KVTable &t) { return iclamp ((t.h - KV_HEAD) / KV_ROW, 1, KV_MAXROWS); }
static int kv_count (const KVTable &t)
{
	int n = 0;
	if (t.rows) for (int i = 0; i < t.rows->size (); i++) if ((*t.rows)[i].on && !(*t.rows)[i].key.empty ()) n++;
	return n;
}
static void kv_trail (KVTable &t) { if (t.rows && (!t.rows->size () || !t.rows->last ().blank ())) t.rows->push (); }
// the columns' x, for the rows and their titles
struct KVCols { int key, value, desc, kind, keyW, valueW, descW, del; };
static KVCols kv_cols (const KVTable &t)
{
	KVCols c;
	bool bar = t.rows && t.rows->size () > kv_vis (t);
	int check = 26, del = 36, kind = t.form ? KV_KIND + 4 : 0;
	int w = t.w - (bar ? KV_BAR + 2 : 0);
	int cols = w - check - del - kind - 4;
	c.keyW = t.showDesc ? cols * 3 / 10 : cols * 2 / 5;
	c.valueW = t.showDesc ? cols * 2 / 5 : cols - c.keyW;
	c.descW = cols - c.keyW - c.valueW;
	c.key = t.x + check; c.kind = c.key + c.keyW; c.value = c.kind + kind; c.desc = c.value + c.valueW; c.del = c.desc + (t.showDesc ? c.descW : 0);
	return c;
}
static void kv_head_paint (long v, long w, long h)
{
	ui_fill (0, 0, w, h, C_FACE);
	for (int i = 0; i < s_nkv; i++)
		if (s_kv[i]->head == v)
		{
			KVTable &t = *s_kv[i]; KVCols c = kv_cols (t);
			text_v (c.key - t.x, 0, (int) h, TR (t.col[0]), C_DIM, F_SMALL, 1);
			text_v (c.value - t.x, 0, (int) h, TR (t.col[1]), C_DIM, F_SMALL, 1);
			if (t.showDesc) text_v (c.desc - t.x, 0, (int) h, TR (t.col[2]), C_DIM, F_SMALL, 1);
		}
}
static void kv_new_row (KVTable &t)
{
	KVRow &r = t.row[t.pool++];
	int ch = ctl_h ();
	r.idx = -1;
	r.on = ui_checkbox (0, 0, 22, ch, (long) "", 1);
	r.key = ui_textbox (0, 0, 10, ch, 0); ui_textbox_hint (r.key, (long) TR (t.ph[0]));
	r.value = ui_textbox (0, 0, 10, ch, 0); ui_textbox_hint (r.value, (long) TR (t.ph[1]));
	r.desc = t.showDesc ? ui_textbox (0, 0, 10, ch, 0) : 0;
	r.kind = t.form ? ui_pushbutton (0, 0, KV_KIND, ch, (long) TR ("Text"), 0) : 0;
	r.del = ui_pushbutton (0, 0, 34, ch, (long) "\xC3\x97", UB_GHOST);
	ui_w_tip (r.del, (long) TR ("Remove the row"));
	if (r.kind) ui_w_tip (r.kind, (long) TR ("A text field, or a file"));
	long all[6] = { r.on, r.key, r.value, r.desc, r.kind, r.del };
	for (long w : all) if (w) { ui_w_set_parent (w, t.panel); ui_w_show (w, 0); }
}
static void kv_show_row (KVRow &r, bool on)
{
	long all[6] = { r.on, r.key, r.value, r.desc, r.kind, r.del };
	for (long w : all) if (w) ui_w_show (w, on);
}
static void kv_set_field (long w, const Str &s) { if (w && !s_eq (tx (w), s.c ())) set_text (w, s.c ()); }
static void kv_rebind (KVTable &t)
{
	int shown = t.inBulk || !t.rows ? 0 : imin (kv_vis (t), t.pool);
	int n = t.rows ? t.rows->size () : 0;
	t.top = iclamp (t.top, 0, imax (0, n - shown));
	for (int i = 0; i < t.pool; i++)
	{
		KVRow &r = t.row[i];
		int at = t.top + i;
		bool on = i < shown && at < n;
		r.idx = on ? at : -1;
		kv_show_row (r, on);
		if (!on) continue;
		KV &k = (*t.rows)[at];
		if ((ui_w_get_value (r.on) != 0) != k.on) ui_w_set_value (r.on, k.on);
		kv_set_field (r.key, k.key); kv_set_field (r.value, k.value); kv_set_field (r.desc, k.desc);
		if (r.kind) set_text (r.kind, k.file ? TR ("File") : TR ("Text"));
	}
	bool bar = !t.inBulk && n > shown;
	ui_w_show (t.bar, bar);
	if (bar) { ui_w_set_range (t.bar, 0, n, shown, 1); ui_w_set_value (t.bar, t.top); }
}
static void kv_place (KVTable &t, int x, int y, int w, int h)
{
	t.x = x; t.y = y; t.w = w; t.h = h;
	ui_w_move (t.panel, x, y, w, h);
	ui_w_move (t.head, x, y, w, KV_HEAD);
	ui_w_move (t.bulk, x, y, w, h);
	ui_w_move (t.bar, x + w - KV_BAR, y + KV_HEAD, KV_BAR, h - KV_HEAD);
	while (t.pool < kv_vis (t)) kv_new_row (t);
	KVCols c = kv_cols (t);
	int ch = ctl_h ();
	for (int i = 0; i < t.pool; i++)
	{
		KVRow &r = t.row[i];
		int ry = y + KV_HEAD + i * KV_ROW + 2;
		ui_w_move (r.on, x + 2, ry, 22, ch);
		ui_w_move (r.key, c.key, ry, c.keyW - 4, ch);
		if (r.kind) ui_w_move (r.kind, c.kind, ry, KV_KIND, ch);
		ui_w_move (r.value, c.value, ry, c.valueW - 4, ch);
		if (r.desc) ui_w_move (r.desc, c.desc, ry, c.descW - 4, ch);
		ui_w_move (r.del, c.del, ry, 34, ch);
	}
	ui_view_dirty (t.head);
	kv_rebind (t);
}
// the rows edited, in place (0: none)
static void kv_to_bulk (KVTable &t);
static void kv_set_rows (KVTable &t, KVList *rows)
{
	t.rows = rows; t.top = 0;
	kv_trail (t);
	if (t.inBulk) { if (rows) kv_to_bulk (t); else set_text (t.bulk, ""); }
	kv_rebind (t);
}
// the rows changed by the program
static void kv_refill (KVTable &t) { kv_trail (t); kv_rebind (t); if (t.inBulk) kv_to_bulk (t); }
static void kv_changed (KVTable &t) { if (t.changed) t.changed (); }
// Bulk Edit: a line a row, "key: value"; // turns it off
static void kv_to_bulk (KVTable &t)
{
	Str o;
	if (t.rows) for (int i = 0; i < t.rows->size (); i++)
	{
		const KV &k = (*t.rows)[i];
		if (k.blank ()) continue;
		if (!k.on) o.add ("//");
		o.add (k.key.c ()); o.add (": "); o.add (k.value.c ()); o.add ('\n');
	}
	set_text (t.bulk, o.c ());
}
static void kv_from_bulk (KVTable &t)
{
	if (!t.rows) return;
	KVList keep = *t.rows;
	t.rows->clear ();
	Str text; text.set (tx (t.bulk), (int) ui_w_text_len (t.bulk));
	for (const char *p = text.c (); *p; )
	{
		const char *e = p; while (*e && *e != '\n') e++;
		Str l; s_trim (l, p, (int) (e - p));
		p = *e ? e + 1 : e;
		if (l.empty ()) continue;
		const char *s = l.c ();
		bool off = s_starts (s, "//"); if (off) s += 2;
		const char *c = strchr (s, ':');
		KV kv; kv.on = !off;
		if (c) { s_trim (kv.key, s, (int) (c - s)); s_trim (kv.value, c + 1); } else s_trim (kv.key, s);
		for (int i = 0; i < keep.size (); i++) if (keep[i].key.eq (kv.key.c ())) { kv.desc = keep[i].desc; break; }
		t.rows->push (kv);
	}
	t.rows->push ();
	kv_changed (t);
}
static void kv_bulk (KVTable &t, bool on)
{
	t.inBulk = on;
	if (on) kv_to_bulk (t); else kv_trail (t);
	ui_w_show (t.bulk, on); ui_w_show (t.head, !on);
	kv_rebind (t);
}
static void kv_init (KVTable &t, long parent, const char *c0, const char *c1, const char *c2, const char *p0, const char *p1, bool desc, bool form, void (*changed) ())
{
	memset (&t, 0, sizeof t);
	t.showDesc = desc; t.form = form; t.changed = changed;
	t.col[0] = c0; t.col[1] = c1; t.col[2] = c2; t.ph[0] = p0; t.ph[1] = p1;
	t.panel = ui_panel (0, 0, 10, 10, 0); in_panel (t.panel, parent);
	t.head = ui_view (0, 0, 10, KV_HEAD, 0); ui_view_on (t.head, (long) kv_head_paint, 0, 0, 0); ui_w_set_parent (t.head, t.panel);
	t.bar = ui_scrollbar (0, 0, KV_BAR, 10, 0); ui_w_set_parent (t.bar, t.panel); ui_w_show (t.bar, 0);
	t.bulk = ui_textarea (0, 0, 10, 10, UTF_MONO | UTA_VSCROLL | UTA_FORMTAB); ui_w_set_parent (t.bulk, t.panel); ui_w_show (t.bulk, 0);
	s_kv[s_nkv++] = &t;
}
static bool kv_control (long w, long code, void (*pick_file) (KVTable &, KVRow &))
{
	for (int k = 0; k < s_nkv; k++)
	{
		KVTable &t = *s_kv[k];
		if (w == t.bar) { if (code == UN_CHANGE) { t.top = (int) ui_w_get_value (w); kv_rebind (t); } return true; }
		if (w == t.bulk) { if (code == UN_CHANGE) kv_from_bulk (t); return true; }
		for (int i = 0; i < t.pool; i++)
		{
			KVRow &r = t.row[i];
			if (w != r.on && w != r.key && w != r.value && (w != r.desc || !w) && (w != r.kind || !w) && w != r.del) continue;
			if (r.idx < 0 || !t.rows || r.idx >= t.rows->size ()) return true;
			KV &kv = (*t.rows)[r.idx];
			if (w == r.del) { if (code == UN_ACTIVATE) { t.rows->remove (r.idx); kv_refill (t); kv_changed (t); } return true; }
			if (w == r.kind) { if (code == UN_ACTIVATE) pick_file (t, r); return true; }
			if (code != UN_CHANGE) return true;
			if (w == r.on) kv.on = ui_w_get_value (w) != 0;
			else if (w == r.key) kv.key = tx (w);
			else if (w == r.value) kv.value = tx (w);
			else kv.desc = tx (w);
			if (!t.rows->last ().blank ()) { t.rows->push (); kv_rebind (t); }
			kv_changed (t);
			return true;
		}
	}
	return false;
}

// ---- the auth editor (a request's, a collection's) ---------------------------------------------------------------
struct AuthPanel
{
	long panel, lType, type, help, l1, l2, l3, f1, f2, where, note;
	Auth *auth; bool forColl; void (*changed) ();
	int x, y, w, h;
	Str inherit;			// what a request inherits
};
static void auth_sync (AuthPanel &a);
static void auth_init (AuthPanel &a, long parent, bool coll, void (*changed) ())
{
	a.forColl = coll; a.changed = changed; a.auth = 0;
	int ch = ctl_h ();
	a.panel = ui_panel (0, 0, 10, 10, 0); in_panel (a.panel, parent);
	a.lType = ui_label (0, 0, 10, ch, (long) TR ("Auth Type"), ULB_BOLD); ui_w_set_parent (a.lType, a.panel);
	static char types[120], types2[120];
	if (!types[0])
	{
		for (int i = 0; i < AUTH_COUNT; i++) { if (i) strcat (types, "|"); strcat (types, TR (AUTH_NAMES[i])); }
		for (int i = 1; i < AUTH_COUNT; i++) { if (i > 1) strcat (types2, "|"); strcat (types2, TR (AUTH_NAMES[i])); }
	}
	a.type = ui_dropdown (0, 0, 10, ch, (long) (coll ? types2 : types)); ui_w_set_parent (a.type, a.panel);
	a.help = ui_label (0, 0, 10, 60, (long) "", ULB_DIM | ULB_WRAP); ui_w_set_parent (a.help, a.panel);
	long *ls[3] = { &a.l1, &a.l2, &a.l3 };
	for (int i = 0; i < 3; i++) { *ls[i] = ui_label (0, 0, 10, ch, (long) "", 0); ui_w_set_parent (*ls[i], a.panel); }
	a.f1 = ui_textbox (0, 0, 10, ch, 0); ui_w_set_parent (a.f1, a.panel);
	a.f2 = ui_textbox (0, 0, 10, ch, 0); ui_w_set_parent (a.f2, a.panel);
	char w[60]; snprintf (w, sizeof w, "%s|%s", TR ("Header"), TR ("Query Params"));
	a.where = ui_dropdown (0, 0, 10, ch, (long) w); ui_w_set_parent (a.where, a.panel);
	a.note = ui_label (0, 0, 10, 60, (long) "", ULB_WRAP); ui_w_set_parent (a.note, a.panel);
}
static void auth_place (AuthPanel &a, int x, int y, int w, int h)
{
	a.x = x; a.y = y; a.w = w; a.h = h;
	int ch = ctl_h (), row = ch + 8;
	ui_w_move (a.panel, x, y, w, h);
	int lw = imin (220, w / 3), lx = x + 16, rx = lx + lw + 20, lb = 86, fx = rx + lb, fw = imax (80, x + w - 16 - fx);
	ui_w_move (a.lType, lx, y + 8, lw, ch);
	ui_w_move (a.type, lx, y + 8 + row, lw, ch);
	ui_w_move (a.help, lx, y + 8 + 2 * row, lw, imax (40, h - 16 - 2 * row));
	ui_w_move (a.l1, rx, y + 8, lb, ch); ui_w_move (a.f1, fx, y + 8, fw, ch);
	ui_w_move (a.l2, rx, y + 8 + row, lb, ch); ui_w_move (a.f2, fx, y + 8 + row, fw, ch);
	ui_w_move (a.l3, rx, y + 8 + 2 * row, lb, ch); ui_w_move (a.where, fx, y + 8 + 2 * row, imin (fw, 180), ch);
	ui_w_move (a.note, rx, y + 8, imax (80, x + w - 16 - rx), imax (40, h - 16));
}
static void auth_bind (AuthPanel &a, Auth *au)
{
	a.auth = au;
	if (!au) return;
	ui_w_set_value (a.type, a.forColl ? imax (0, au->type - 1) : au->type);
	ui_w_set_value (a.where, au->inQuery ? 1 : 0);
	auth_sync (a);
}
static void auth_sync (AuthPanel &a)
{
	if (!a.auth) return;
	int t = a.auth->type;
	static const char *const HELP[] = {
		"The authorization header is made from its folder's or its collection's auth.",
		"This request is sent without authorization.",
		"The token is sent as the Authorization header: Bearer <token>.",
		"The username and the password are sent as the Authorization header (Basic, base64).",
		"The key and its value are sent as a header, or added to the URL's query.",
	};
	set_text (a.help, TR (HELP[t]));
	bool one = t == AUTH_BEARER || t == AUTH_BASIC || t == AUTH_APIKEY, two = t == AUTH_BASIC || t == AUTH_APIKEY;
	ui_w_show (a.l1, one); ui_w_show (a.f1, one);
	ui_w_show (a.l2, two); ui_w_show (a.f2, two);
	ui_w_show (a.l3, t == AUTH_APIKEY); ui_w_show (a.where, t == AUTH_APIKEY);
	ui_w_show (a.note, !one);
	ui_w_style (a.f2, t == AUTH_BASIC ? UTF_PASSWORD : 0);
	if (t == AUTH_INHERIT) set_text (a.note, a.inherit.c ());
	if (t == AUTH_NONE) set_text (a.note, TR ("This request does not use any authorization."));
	if (t == AUTH_BEARER) { set_text (a.l1, TR ("Token")); set_text (a.f1, a.auth->token.c ()); }
	else if (t == AUTH_BASIC) { set_text (a.l1, TR ("Username")); set_text (a.l2, TR ("Password")); set_text (a.f1, a.auth->user.c ()); set_text (a.f2, a.auth->pass.c ()); }
	else if (t == AUTH_APIKEY) { set_text (a.l1, TR ("Key")); set_text (a.l2, TR ("Value")); set_text (a.l3, TR ("Add to")); set_text (a.f1, a.auth->key.c ()); set_text (a.f2, a.auth->value.c ()); }
}
static bool auth_control (AuthPanel &a, long w, long code)
{
	if (w != a.type && w != a.where && w != a.f1 && w != a.f2) return false;
	if (!a.auth || code != UN_CHANGE) return true;
	if (w == a.type) { a.auth->type = (int) ui_w_get_value (w) + (a.forColl ? 1 : 0); auth_sync (a); }
	else if (w == a.where) a.auth->inQuery = ui_w_get_value (w) == 1;
	else
	{
		int t = a.auth->type;
		if (t == AUTH_BEARER) a.auth->token = tx (a.f1);
		else if (t == AUTH_BASIC) { a.auth->user = tx (a.f1); a.auth->pass = tx (a.f2); }
		else if (t == AUTH_APIKEY) { a.auth->key = tx (a.f1); a.auth->value = tx (a.f2); }
	}
	if (a.changed) a.changed ();
	return true;
}

} // namespace cr

#endif
