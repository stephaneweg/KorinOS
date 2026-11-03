//
// listview.h -- the List view: the records shown in a virtual ui_list, a column per field, the display
// names on top. A click on a name sorts by it, again the other way, a third time back to the file's order;
// a column's edge dragged widens it; the columns scroll sideways (UL_HSCROLL). A double click (or Enter)
// opens the record in the form; Delete deletes the selected one (after asking); a right click: Open, New
// Record, Duplicate, Delete. The cells show the values as the form does: numbers on the right, a date
// 29/09/2026, a colour's swatch and code, a yes / no's check mark, a text of several lines on one.
//
// (ui_list has no way to drop a column: the fields changed, the widget is made again.)
//
#ifndef _cardfile_listview_h
#define _cardfile_listview_h

#include "app.h"

namespace cf {

static long lv_panel, lv;			// the page, the list
static int  lv_x, lv_y, lv_w, lv_h, lv_ncol;
static unsigned lv_ver;
static int  lv_cached = -1;			// the row in the buffer (UIKit asks for a row once a column)
static char lv_row[MAXF * 200 + 1];

static const char *lv_row_text (long, long row)
{
	if (row == lv_cached) return lv_row;
	lv_cached = -1;
	if (row < 0 || row >= g_nord) return "";
	char *o = lv_row; int n = 0;
	char v[VAL_MAX];
	for (int k = 0; k < g_doc.nf; k++)
	{
		value_show (g_doc.f[k], g_doc.r[g_ord[row]][k], v, sizeof v, false, "  \xB7  ");
		const char *u = U8 (v);
		if (k) o[n++] = '\t';
		for (int i = 0; u[i] && i < 190; i++) o[n++] = u[i] == '\t' ? ' ' : u[i];
		// (a cut inside a letter: UIKit's text stops at the broken byte's glyph, nothing worse)
	}
	o[n] = '\0';
	lv_cached = (int) row;
	return lv_row;
}

// A colour's swatch, a yes / no's mark; the rest: UIKit's text.
static long lv_cell (long, long row, long col, long x, long y, long w, long h, long white)
{
	if (row < 0 || row >= g_nord || col >= g_doc.nf) return 0;
	const Field &f = g_doc.f[col];
	const char *v = g_doc.r[g_ord[row]][col];
	unsigned ink = white ? 0xFFFFFF : jet (J_TEXTE1);
	if (f.type == FT_BOOL)
	{
		if (v[0]) text_box ("\xE2\x9C\x93", (int) (x + (w - 12) / 2), (int) y, (int) h, ink, 14, (long) ui.font, 0);
		return 1;
	}
	if (f.type == FT_COLOR)
	{
		unsigned c;
		if (!v[0]) return 1;
		if (!parse_color (v, &c)) return 0;
		ui_rbox (x + 10, y + 5, 26, h - 10, 3, jet (J_LIGNE_FORTE), jet (J_LIGNE_FORTE));
		ui_rbox (x + 11, y + 6, 24, h - 12, 2, c, c);
		text_box (v, (int) x + 42, (int) y, (int) h, white ? ink : jet (J_TEXTE3), 12, font (), (int) w - 48);
		return 1;
	}
	return 0;
}

static int lv_measure (int k, int lo, int hi)
{
	int w = (int) ui_text_width ((long) U8 (g_doc.f[k].label), 12, font_bold ()) + 36, n = imin (g_nord, 200);
	char v[VAL_MAX];
	for (int i = 0; i < n; i++)
	{
		value_show (g_doc.f[k], g_doc.r[g_ord[i]][k], v, sizeof v, false, "  \xB7  ");
		w = imax (w, (int) ui_text_width ((long) U8 (v), 13, font ()) + 26);
	}
	return iclamp (w, lo, hi);
}
// Each column as wide as its values need (within limits by type); what is left of the list's width goes
// to the widest text.
static void lv_columns ()
{
	int width[MAXF], total = 0, wide = -1;
	for (int k = 0; k < g_doc.nf; k++)
	{
		const Field &f = g_doc.f[k];
		int tw = (int) ui_text_width ((long) U8 (f.label), 12, font_bold ()) + 36;
		switch (f.type)
		{
		case FT_MEMO: width[k] = lv_measure (k, 140, 260); break;
		case FT_INT: case FT_DEC: width[k] = lv_measure (k, 70, 170); break;
		case FT_DATE: width[k] = imax (tw, 100); break;
		case FT_COLOR: width[k] = imax (tw, 112); break;
		case FT_BOOL: width[k] = imax (tw, 60); break;
		case FT_CHOICE: width[k] = lv_measure (k, 80, 200); break;
		default: width[k] = lv_measure (k, 90, 240); break;
		}
		total += width[k];
		if ((f.type == FT_TEXT || f.type == FT_MEMO) && (wide < 0 || width[k] > width[wide])) wide = k;
	}
	int room = lv_w - 2 - 12 - total;
	if (room > 0 && g_doc.nf) width[wide >= 0 ? wide : g_doc.nf - 1] += room;
	for (int k = 0; k < g_doc.nf; k++)
	{
		const Field &f = g_doc.f[k];
		ui_list_column (lv, (long) U8 (f.label), width[k], f.type == FT_INT || f.type == FT_DEC ? ULC_RIGHT : 0);
	}
}
static void lv_build ()
{
	if (lv) ui_w_destroy (lv);
	lv = ui_list (lv_x, lv_y, lv_w, lv_h, UL_HEADER | UL_STRIPES | UL_HSCROLL);
	ui_w_set_parent (lv, lv_panel);
	lv_columns ();
	ui_list_cell (lv, (long) lv_cell);
	lv_ncol = g_doc.nf;
	lv_ver = g_fieldsVer;
}
// The columns (the fields changed), the rows, the selection: as the document is.
static void lv_sync ()
{
	if (lv_ver != g_fieldsVer || lv_ncol != g_doc.nf || !lv) lv_build ();
	lv_cached = -1;
	ui_list_virtual (lv, g_nord, (long) lv_row_text);
	ui_list_sort (lv, g_doc.sort, g_doc.sortDesc ? 1 : 0);
	ui_w_set_value (lv, cur_pos ());
}

// UN_SORT: a click on a column's title, which UIKit has noted; the cycle is ours.
static void lv_on_sort ()
{
	long desc;
	int c = (int) uikit_list_sorted (lv, &desc);
	if (c < 0 || c >= g_doc.nf) return;
	if (g_doc.sort != c) { g_doc.sort = c; g_doc.sortDesc = false; }
	else if (!g_doc.sortDesc) g_doc.sortDesc = true;
	else { g_doc.sort = -1; g_doc.sortDesc = false; }
	g_pin = -1;
	doc_changed (false);
	char m[120];
	if (g_doc.sort < 0) scpy (m, "In the file's order", sizeof m);
	else snprintf (m, sizeof m, TL (g_doc.sortDesc ? "Sorted by %s, descending" : "Sorted by %s"), g_doc.f[c].label);
	status (m);
}
static void lv_on_context ()
{
	bool on = ui_w_get_value (lv) >= 0;
	// (labels one after another; 0x01 first: greyed; "-": a line, which has its number too)
	char t[200]; int n = 0;
	const struct { const char *s; bool en; } L[] = {
		{ TR ("Open in the Form"), on }, { "-", true }, { TR ("New Record"), g_doc.nf > 0 },
		{ TR ("Duplicate Record"), on }, { TR ("Delete Record..."), on } };
	for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++)
	{
		if (!L[i].en) t[n++] = 1;
		for (const char *p = L[i].s; *p; p++) t[n++] = *p;
		t[n++] = '\0';
	}
	t[n] = '\0';
	switch (ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), (long) t, 200))
	{
	case 0: show_view (V_FORM); break;
	case 2: cmd_new_record (); break;
	case 3: cmd_dup_record (); break;
	case 4: cmd_del_record (); break;
	}
}
static bool lv_event (long w, long code)
{
	if (w != lv) return false;
	if (code == UN_CHANGE)
	{
		long s = ui_w_get_value (lv);
		if (s >= 0 && s < g_nord) select_record (g_ord[s]);
	}
	else if (code == UN_ACTIVATE)
	{
		long s = ui_w_get_value (lv);
		if (s >= 0 && s < g_nord) select_record (g_ord[s]);
		show_view (V_FORM);
	}
	else if (code == UN_SORT) lv_on_sort ();
	else if (code == UN_CONTEXT)
	{
		long s = ui_w_get_value (lv);
		if (s >= 0 && s < g_nord) select_record (g_ord[s]);
		lv_on_context ();
	}
	return true;
}
static bool lv_key (int hid)
{
	if (hid == KEY_DELETE && ui_w_get_value (lv) >= 0) { cmd_del_record (); return true; }
	return false;
}
static void lv_create (int x, int y, int w, int h)
{
	lv_panel = ui_panel (x, y, w, h, 0);
	lv_x = x + 10; lv_y = y + 10; lv_w = w - 20; lv_h = h - 20;
	lv_build ();
}
static void lv_move (int x, int y, int w, int h)
{
	ui_w_move (lv_panel, x, y, w, h);
	lv_x = x + 10; lv_y = y + 10; lv_w = w - 20; lv_h = h - 20;
	ui_w_move (lv, lv_x, lv_y, lv_w, lv_h);
}

} // namespace cf

#endif
