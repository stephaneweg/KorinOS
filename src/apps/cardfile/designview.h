//
// designview.h -- the Design view: the form itself. On the left its fields in their order (a ui_list: the
// number, the display name, the type); Add Field (after the one chosen), Remove, Move Up, Move Down. On
// the right the field chosen -- its display name (the form's and the list's), its column name (the
// file's: letters, digits, _ and -; made from the display name as long as it was not typed), its type,
// and the type's option: a decimal number's decimals, a choice list's choices (one a line) -- then the
// form's title and description. A change applies at once (Undo takes it back). A new type converts the
// values through their text: what reads as the new type stays, the rest is emptied (asked first when
// some would be); to a choice list, the values become its choices.
//
#ifndef _cardfile_designview_h
#define _cardfile_designview_h

#include "app.h"

namespace cf {

static const char *const TYPE_HINT[FT_COUNT] = {
	"One line of text.",
	"Several lines.",
	"A whole number: 42, -7, 1 000 000.",
	"",
	"A date: typed as DD/MM/YYYY, or picked on a calendar.",
	"A colour: from a palette, a code or a name.",
	"A check box: yes or no.",
	"" };

static long dv, dv_gFields, dv_gField, dv_gForm, dv_list, dv_b[4];
static long dv_eLabel, dv_eColumn, dv_cType, dv_nDec, dv_mChoices, dv_eTitle, dv_eInfo;
static long dv_l[6], dv_lOpt, dv_lHint, dv_lDecHint, dv_lChHint, dv_lStats;
static int  dv_x, dv_y, dv_w, dv_h, dv_rows;
static bool dv_editing;				// (a control's change: the document follows, not the controls)

static const char *dv_row (long, long row)
{
	static char b[LABEL_MAX * 2 + 64];
	if (row < 0 || row >= g_doc.nf) return "";
	const Field &f = g_doc.f[row];
	itoa10 (row + 1, b);
	scat (b, "\t", sizeof b);
	scat (b, U8 (f.label), sizeof b);
	scat (b, "\t", sizeof b);
	scat (b, TYPE_NAME[f.type], sizeof b);
	if (f.type == FT_DEC) { scat (b, " (", sizeof b); scat_num (b, f.prec, sizeof b); scat (b, ")", sizeof b); }
	if (f.type == FT_CHOICE) { scat (b, " (", sizeof b); scat_num (b, f.nch, sizeof b); scat (b, ")", sizeof b); }
	return b;
}

static void dv_dec_hint (int p)
{
	char b[64], n[24];
	fmt_num (123450000LL / ipow10 (6 - p), p, n);
	snprintf (b, sizeof b, TR ("Shown as %s"), n);
	ui_w_set_text (dv_lDecHint, (long) b);
}
static void dv_load_field ()
{
	bool has = g_doc.nf > 0;
	long props[] = { dv_eLabel, dv_eColumn, dv_cType, dv_nDec, dv_mChoices };
	for (unsigned i = 0; i < sizeof props / sizeof props[0]; i++) ui_w_enable (props[i], has);
	if (!has)
	{
		ui_w_set_text (dv_eLabel, (long) ""); ui_w_set_text (dv_eColumn, (long) "");
		ui_w_set_text (dv_lHint, (long) TR ("No field: Add Field makes one."));
		ui_w_show (dv_nDec, 0); ui_w_show (dv_mChoices, 0); ui_w_show (dv_lChHint, 0); ui_w_show (dv_lDecHint, 0);
		ui_w_show (dv_lOpt, 0); ui_w_show (dv_lHint, 1);
	}
	else
	{
		const Field &f = g_doc.f[g_fsel];
		w_set (dv_eLabel, f.label);
		w_set (dv_eColumn, f.column);
		ui_w_style (dv_eColumn, 0);
		ui_w_set_value (dv_cType, f.type);
		bool dec = f.type == FT_DEC, ch = f.type == FT_CHOICE;
		ui_w_show (dv_nDec, dec); ui_w_show (dv_lDecHint, dec);
		ui_w_show (dv_mChoices, ch); ui_w_show (dv_lChHint, ch);
		ui_w_show (dv_lOpt, dec || ch); ui_w_show (dv_lHint, !dec && !ch);
		ui_w_set_text (dv_lOpt, (long) TR (dec ? "Decimals" : "Choices"));
		if (dec) { ui_w_set_value (dv_nDec, f.prec); dv_dec_hint (f.prec); }
		if (ch)
		{
			Out o;
			for (int i = 0; i < f.nch; i++) { if (i) o.put ('\n'); o.puts (f.ch[i]); }
			w_set (dv_mChoices, o.b ? o.b : "");
		}
		ui_w_set_text (dv_lHint, (long) TR (TYPE_HINT[f.type]));
		// what the records hold in it
		int filled = 0;
		for (int i = 0; i < g_doc.nr; i++) if (g_doc.r[i][g_fsel][0]) filled++;
		char st[96];
		snprintf (st, sizeof st, TR (f.type == FT_BOOL ? "In the records: %d yes, %d no" : filled == 1 ? "In the records: %d value, %d empty" : "In the records: %d values, %d empty"), filled, g_doc.nr - filled);
		ui_w_set_text (dv_lStats, (long) st);
	}
	ui_w_show (dv_lStats, has);
	w_set (dv_eTitle, g_doc.title);
	w_set (dv_eInfo, g_doc.info);
	ui_w_draw (dv);					// (a control hidden leaves nothing)
}
// The list and (not while one of them is being edited) the field's properties, as the document is.
static void dv_sync ()
{
	g_fsel = iclamp (g_fsel, 0, imax (0, g_doc.nf - 1));
	ui_list_virtual (dv_list, g_doc.nf, (long) dv_row);
	ui_w_set_value (dv_list, g_doc.nf ? g_fsel : -1);
	if (!dv_editing) dv_load_field ();
}
static void dv_focus_label () { ui_w_focus (dv_eLabel); ui_textbox_select (dv_eLabel, 0, ui_w_text_len (dv_eLabel)); }

// ---- the commands ----------------------------------------------------------------------------------------
static void dv_add ()
{
	if (g_doc.nf >= MAXF) { ask ("Add Field", "A form holds 64 fields at most.", UM_OK); return; }
	undo_mark (-1);
	Field f; char lab[LABEL_MAX]; snprintf (lab, sizeof lab, g_wField, g_doc.nf + 1);
	char col[COL_MAX]; make_column (lab, col, sizeof col);
	field_init (f, lab, col, FT_TEXT);
	g_fsel = doc_insert_field (g_doc, g_doc.nf ? g_fsel + 1 : 0, f);
	doc_changed (true);
	dv_focus_label ();
	status ("Field added: name it, then choose its type");
}
static void dv_remove ()
{
	if (!g_doc.nf) return;
	const Field &f = g_doc.f[g_fsel];
	int n = 0;
	for (int i = 0; i < g_doc.nr; i++) if (g_doc.r[i][g_fsel][0]) n++;
	char msg[260];
	if (n) snprintf (msg, sizeof msg, TL (n == 1 ? "Remove the field %s? Its values in %d record go with it." : "Remove the field %s? Its values in %d records go with it."), f.label, n);
	else snprintf (msg, sizeof msg, TL ("Remove the field %s?"), f.label);
	if (ask ("Remove Field", msg, UM_YESNO) != 1) return;
	undo_mark (-1);
	doc_remove_field (g_doc, g_fsel);
	g_fsel = imin (g_fsel, imax (0, g_doc.nf - 1));
	doc_changed (true);
	ui_w_focus (dv_list);
}
static void dv_move_field (int dir)
{
	if (g_fsel + dir < 0 || g_fsel + dir >= g_doc.nf) return;
	undo_mark (-1);
	doc_move_field (g_doc, g_fsel, dir);
	g_fsel += dir;
	doc_changed (true);
	ui_w_focus (dv_list);
}

static void dv_applied (bool fields) { dv_editing = true; doc_changed (fields); dv_editing = false; }
static void dv_on_label ()
{
	if (!g_doc.nf) return;
	Field &f = g_doc.f[g_fsel];
	char was[COL_MAX]; make_column (f.label, was, sizeof was); unique_column (g_doc, g_fsel, was, sizeof was);
	bool follow = seq (was, f.column);			// (the column made from the name: it follows)
	undo_mark (1000 + g_fsel);
	scpy (f.label, w_text (dv_eLabel), sizeof f.label);
	if (follow)
	{
		make_column (f.label[0] ? f.label : "field", f.column, sizeof f.column);
		unique_column (g_doc, g_fsel, f.column, sizeof f.column);
		w_set (dv_eColumn, f.column); ui_w_style (dv_eColumn, 0);
	}
	dv_applied (true);
}
static void dv_on_column ()
{
	if (!g_doc.nf) return;
	Field &f = g_doc.f[g_fsel];
	char t[COL_MAX]; scpy (t, w_text (dv_eColumn), sizeof t);
	bool ok = t[0] != 0;
	for (const char *p = t; *p; p++) if (!col_char (*p)) ok = false;
	if (!ok) { ui_w_style (dv_eColumn, UTF_ERROR); status (t[0] ? "A column's name: letters, digits, _ and -" : "A column needs a name"); return; }
	int other = find_column (g_doc, t, g_fsel);
	if (other >= 0)
	{
		ui_w_style (dv_eColumn, UTF_ERROR);
		char m[160]; snprintf (m, sizeof m, TL ("The field %s has this column name"), g_doc.f[other].label);
		status (m);
		return;
	}
	ui_w_style (dv_eColumn, 0);
	undo_mark (2000 + g_fsel);
	scpy (f.column, t, sizeof f.column);
	dv_applied (true);
}
static void dv_on_type ()
{
	if (!g_doc.nf) return;
	Field &f = g_doc.f[g_fsel];
	int nt = (int) ui_w_get_value (dv_cType);
	if (nt == f.type || nt < 0 || nt >= FT_COUNT) return;
	int lost, rounded;
	convert_count (g_doc, g_fsel, nt, f.prec, &lost, &rounded);
	if (lost)
	{
		char m[300], what[48];
		static const char *const WHAT[FT_COUNT] = { "text", "text", "whole number", "number", "date", "colour", "yes / no", "choice" };
		scpy (what, TL (WHAT[nt]), sizeof what);
		snprintf (m, sizeof m, TL (lost == 1 ? "%d value of \"%s\" is not a %s: it will be emptied. Change the type?" : "%d values of \"%s\" are not a %s: they will be emptied. Change the type?"), lost, f.label, what);
		if (ask ("Change the Type", m, UM_YESNO) != 1) { ui_w_set_value (dv_cType, f.type); return; }
	}
	undo_mark (-1);
	convert_field (g_doc, g_fsel, nt, f.prec);
	doc_changed (true);					// (the type's rows shown: the controls loaded again)
	if (rounded)
	{
		char m[80]; snprintf (m, sizeof m, TL (rounded == 1 ? "%d value rounded" : "%d values rounded"), rounded);
		status (m);
	}
	else status ("Type changed");
	ui_w_focus (dv_cType);
}
static void dv_on_dec ()
{
	if (!g_doc.nf || g_doc.f[g_fsel].type != FT_DEC) return;
	int p = (int) ui_w_get_value (dv_nDec);
	Field &f = g_doc.f[g_fsel];
	if (p == f.prec) return;
	int lost, rounded;
	convert_count (g_doc, g_fsel, FT_DEC, p, &lost, &rounded);
	undo_mark (5000 + g_fsel);
	convert_field (g_doc, g_fsel, FT_DEC, p);
	dv_dec_hint (p);
	dv_applied (true);
	if (rounded) { char m[80]; snprintf (m, sizeof m, TL (rounded == 1 ? "%d value rounded" : "%d values rounded"), rounded); status (m); }
}
static void dv_on_choices ()
{
	if (!g_doc.nf || g_doc.f[g_fsel].type != FT_CHOICE) return;
	const char *s = w_text (dv_mChoices);
	undo_mark (3000 + g_fsel);
	Field &f = g_doc.f[g_fsel];
	field_free (f);
	bool full = false;
	while (*s)
	{
		char one[VAL_MAX]; int n = 0;
		while (*s && *s != '\n') { if (n < VAL_MAX - 1) one[n++] = *s; s++; }
		one[n] = '\0';
		if (*s) s++;
		char t[VAL_MAX]; trim_copy (t, one, sizeof t);
		if (t[0] && !choice_add (f, t)) full = true;
	}
	dv_applied (true);
	if (full) status ("A choice list holds 64 choices at most");
}
static void dv_on_title ()
{
	undo_mark (4000);
	scpy (g_doc.title, w_text (dv_eTitle), sizeof g_doc.title);
	dv_applied (false);
}
static void dv_on_info ()
{
	undo_mark (4001);
	scpy (g_doc.info, w_text (dv_eInfo), sizeof g_doc.info);
	dv_applied (false);
}

// A control's news -> true: it was one of the design's.
static bool dv_event (long w, long code)
{
	if (w == dv_list)
	{
		long s = ui_w_get_value (dv_list);
		if (code == UN_CHANGE && s >= 0 && s < g_doc.nf && s != g_fsel) { g_fsel = (int) s; undo_break (); dv_load_field (); }
		if (code == UN_ACTIVATE) dv_focus_label ();
		return true;
	}
	if (code == UN_ACTIVATE)
	{
		if (w == dv_b[0]) dv_add ();
		else if (w == dv_b[1]) dv_remove ();
		else if (w == dv_b[2]) dv_move_field (-1);
		else if (w == dv_b[3]) dv_move_field (1);
		else return false;
		return true;
	}
	if (code != UN_CHANGE) return false;
	if (w == dv_eLabel) dv_on_label ();
	else if (w == dv_eColumn) dv_on_column ();
	else if (w == dv_cType) dv_on_type ();
	else if (w == dv_nDec) dv_on_dec ();
	else if (w == dv_mChoices) dv_on_choices ();
	else if (w == dv_eTitle) dv_on_title ();
	else if (w == dv_eInfo) dv_on_info ();
	else return false;
	return true;
}
static bool dv_key (int hid)
{
	if (hid == KEY_DELETE && ui_w_get_focus () == dv_list) { dv_remove (); return true; }
	return false;
}

// ---- the page -----------------------------------------------------------------------------------------------
static long dv_label (long parent, const char *s, long flags = 0)
{
	long l = ui_label (0, 0, 100, 20, (long) s, flags);
	ui_w_set_parent (l, parent);
	return l;
}
static long dv_put (long w, long parent) { ui_w_set_parent (w, parent); return w; }
static void dv_layout ()
{
	int h = ctl_h (), step = h + 6;
	int lw = imin (420, dv_w * 45 / 100), gx = 12 + lw + 12, gw = dv_w - gx - 12;
	int x0 = dv_x, y0 = dv_y;
	ui_w_move (dv, x0, y0, dv_w, dv_h);
	ui_w_move (dv_gFields, x0 + 12, y0 + 6, lw, dv_h - 12);
	int top = y0 + 6 + 28, by = y0 + dv_h - 6 - 8 - h;
	ui_w_move (dv_list, x0 + 24, top, lw - 24, by - top - 8);
	static const int BW[4] = { 104, 86, 66, 78 };
	int bx = x0 + 24, room = lw - 24 - 3 * 4;
	for (int i = 0; i < 4; i++)
	{
		int w = BW[i] * room / 334;
		ui_w_move (dv_b[i], bx, by, w, h);
		bx += w + 4;
	}
	// the field chosen; the form's own, under it
	int cx = x0 + gx + 112, cw = gw - 112 - 12, lx = x0 + gx + 12;
	int gy = y0 + 6, y = gy + 28;
	int chH = imax (54, dv_h - 6 - 6 - 28 - 4 * step - 22 - 22 - 12 - (28 + 2 * step + 10));	// (what the screen leaves the choices)
	chH = imin (chH, 108);
	int fieldH = 28 + 3 * step + chH + 6 + 18 + 4 + 20 + 8;
	ui_w_move (dv_gField, x0 + gx, gy, gw, fieldH);
	ui_w_move (dv_l[0], lx, y, 100, h); ui_w_move (dv_eLabel, cx, y, cw, h); y += step;
	ui_w_move (dv_l[1], lx, y, 100, h); ui_w_move (dv_eColumn, cx, y, cw, h); y += step;
	ui_w_move (dv_l[2], lx, y, 100, h); ui_w_move (dv_cType, cx, y, imin (cw, 210), h); y += step;
	ui_w_move (dv_lOpt, lx, y, 100, h);
	ui_w_move (dv_nDec, cx, y, 90, h);
	ui_w_move (dv_lDecHint, cx + 100, y, imax (20, cw - 100), h);
	ui_w_move (dv_lHint, cx, y, cw, h);
	ui_w_move (dv_mChoices, cx, y, cw, chH);
	y += chH + 6;
	ui_w_move (dv_lChHint, cx, y, cw, 18);
	y += 18 + 4;
	ui_w_move (dv_lStats, lx, y, gw - 24, 20);
	int fy = gy + fieldH + 8;
	ui_w_move (dv_gForm, x0 + gx, fy, gw, 28 + 2 * step + 4);
	y = fy + 28;
	ui_w_move (dv_l[3], lx, y, 100, h); ui_w_move (dv_eTitle, cx, y, cw, h); y += step;
	ui_w_move (dv_l[4], lx, y, 100, h); ui_w_move (dv_eInfo, cx, y, cw, h);
}
static void dv_create (int x, int y, int w, int h)
{
	dv_x = x; dv_y = y; dv_w = w; dv_h = h;
	dv = ui_panel (x, y, w, h, 0);
	dv_gFields = dv_put (ui_groupbox (0, 0, 10, 10, (long) TR ("Fields")), dv);
	dv_list = dv_put (ui_list (0, 0, 100, 100, UL_HEADER | UL_NOSORT | UL_STRIPES), dv_gFields);
	ui_list_column (dv_list, (long) "#", 44, ULC_RIGHT);
	ui_list_column (dv_list, (long) TR ("Field"), 170, 0);
	ui_list_column (dv_list, (long) TR ("Type"), 150, 0);
	ui_list_virtual (dv_list, 0, (long) dv_row);
	static const char *const BT[4] = { "Add Field", "Remove", "Up", "Down" };
	for (int i = 0; i < 4; i++) dv_b[i] = dv_put (ui_pushbutton (0, 0, 80, ctl_h (), (long) BT[i], 0), dv_gFields);

	dv_gField = dv_put (ui_groupbox (0, 0, 10, 10, (long) TR ("Field")), dv);
	dv_l[0] = dv_label (dv_gField, "Display name");
	dv_eLabel = dv_put (ui_textbox (0, 0, 100, ctl_h (), 0), dv_gField);
	ui_w_set_range (dv_eLabel, 0, LABEL_MAX - 1, 0, 0);
	dv_l[1] = dv_label (dv_gField, "Column name");
	dv_eColumn = dv_put (ui_textbox (0, 0, 100, ctl_h (), 0), dv_gField);
	ui_w_set_range (dv_eColumn, 0, COL_MAX - 1, 0, 0);
	ui_w_tip (dv_eColumn, (long) "The column's name in the file: letters, digits, _ and -");
	dv_l[2] = dv_label (dv_gField, "Type");
	dv_cType = dv_put (ui_dropdown (0, 0, 100, ctl_h (), (long) TR ("Text|Multi-line text|Integer|Decimal number|Date|Colour|Yes / No|Choice list")), dv_gField);
	dv_lOpt = dv_label (dv_gField, "Decimals");
	dv_nDec = dv_put (ui_numeric (0, 0, 90, ctl_h (), 0, MAXDEC, 2), dv_gField);
	dv_lDecHint = dv_label (dv_gField, "", ULB_DIM);
	dv_mChoices = dv_put (ui_textarea (0, 0, 100, 80, UTA_VSCROLL | UTA_FORMTAB), dv_gField);
	dv_lChHint = dv_label (dv_gField, "One choice a line, in their order.", ULB_DIM);
	dv_lStats = dv_label (dv_gField, "", ULB_DIM);
	dv_lHint = dv_label (dv_gField, "", ULB_DIM);

	dv_gForm = dv_put (ui_groupbox (0, 0, 10, 10, (long) TR ("Form")), dv);
	dv_l[3] = dv_label (dv_gForm, "Title");
	dv_eTitle = dv_put (ui_textbox (0, 0, 100, ctl_h (), 0), dv_gForm);
	ui_w_set_range (dv_eTitle, 0, TITLE_MAX - 1, 0, 0);
	dv_l[4] = dv_label (dv_gForm, "Description");
	dv_eInfo = dv_put (ui_textbox (0, 0, 100, ctl_h (), 0), dv_gForm);
	ui_w_set_range (dv_eInfo, 0, INFO_MAX - 1, 0, 0);
	ui_textbox_hint (dv_eInfo, (long) TR ("(none)"));
	dv_layout ();
}
static void dv_move (int x, int y, int w, int h) { dv_x = x; dv_y = y; dv_w = w; dv_h = h; dv_layout (); }

} // namespace cf

#endif
