//
// formview.h -- the Form view: the record shown as an index card -- on top the form's title and its
// description and the record's place ("3 / 12"), then each field: its name, and the editor its type
// takes. The card is a ui_view (fv): the program paints the card and the names; the editors are UIKit's
// controls set on the view, the view cuts them when the card scrolls.
//
//   text     ui_textbox                    multi-line  ui_textarea
//   integer  ui_textbox, on the right      decimal     the same
//   date     ui_textbox + a button: ui_date_popup      (ui_datepicker can neither be empty nor be typed in)
//   colour   a ui_colorpicker + ui_textbox ("" none; a code or a name typed)
//   yes/no   ui_checkbox                   choice      ui_dropdown, "(none)" first
//
// The editors' values go into the record when it is left (commit: another record, another view, Save...):
// a value that is not one of its type is said, outlined in red, and its editor keeps the keyboard. Tab /
// Shift+Tab (UIKit), Enter, Up / Down move between the fields; Page Up / Page Down show the previous /
// next record; Esc puts the record's values back. The card scrolls when the form is taller than the
// window (the wheel, its scroll bar); a click on a field's name gives its editor the keyboard (a check
// box's: ticks it).
//
#ifndef _cardfile_formview_h
#define _cardfile_formview_h

#include "app.h"

namespace cf {

enum { CARD_Y = 10, HEAD_H = 60, PAD = 22, FV_GAP = 8, MEMO_H = 84, SB_W = 10, SB_ROOM = 16, MAXW = 820, DATE_BTN = 30, SWATCH_W = 44 };

static long fv, fv_bar, fv_btn;			// the view, its scroll bar, the button of "no record"
static long fv_ed[MAXF], fv_aux[MAXF];		// an editor, its helper (the date's button, the colour's swatch)
static bool fv_err[MAXF];			// outlined in red
static int  fv_ne, fv_y[MAXF], fv_h[MAXF], fv_w[MAXF];
static unsigned fv_ver;
static int  fv_rec = -2;
static int  fv_x, fv_top, fv_vw, fv_vh;		// the view's place in the window
static int  fv_cardX, fv_cardW, fv_cardH, fv_labW, fv_edX, fv_content, fv_scroll;

// The body is what the card shows: a record, or the words of "none".
static bool fv_shows () { return g_cur >= 0 && g_cur < g_doc.nr && fv_ne > 0; }

// ---- where things go ------------------------------------------------------------------------------------
static void fv_apply ()
{
	int oy = fv_top - fv_scroll;
	bool show = fv_shows ();
	for (int k = 0; k < fv_ne; k++)
	{
		int x = fv_edX, y = oy + fv_y[k], w = fv_w[k], h = fv_h[k];
		switch (g_doc.f[k].type)
		{
		case FT_DATE:
			ui_w_move (fv_ed[k], x, y, w - DATE_BTN - 4, h);
			ui_w_move (fv_aux[k], x + w - DATE_BTN, y, DATE_BTN, h);
			break;
		case FT_COLOR:
			ui_w_move (fv_aux[k], x, y, SWATCH_W, h);
			ui_w_move (fv_ed[k], x + SWATCH_W + 6, y, w - SWATCH_W - 6, h);
			break;
		default:
			ui_w_move (fv_ed[k], x, y, w, h);
		}
	}
	if (fv_btn) ui_w_move (fv_btn, fv_x + fv_cardX + (fv_cardW - 160) / 2, oy + CARD_Y + HEAD_H + 58, 160, ctl_h ());
	bool bar = fv_content > fv_vh;
	ui_w_move (fv_bar, fv_x + fv_vw - SB_W - 2, fv_top + 2, SB_W, fv_vh - 4);
	ui_w_set_range (fv_bar, 0, fv_content, fv_vh, 24);
	ui_w_set_value (fv_bar, fv_scroll);
	ui_w_show (fv_bar, bar);
	(void) show;
	ui_view_dirty (fv);
}
static void fv_clamp () { fv_scroll = iclamp (fv_scroll, 0, imax (0, fv_content - fv_vh)); }
static void fv_scroll_to (int y) { fv_scroll = y; fv_clamp (); fv_apply (); }

// The card: as wide as the view lets it (up to MAXW), centred; the names' column as wide as the longest
// name; each editor as its type needs.
static void fv_place ()
{
	int px = ui_px (), edh = ctl_h ();
	fv_cardW = imin (fv_vw - 2 * 10 - SB_ROOM, MAXW);
	if (fv_cardW < 280) fv_cardW = imax (120, fv_vw - 8 - SB_ROOM);
	fv_cardX = imax (4, (fv_vw - SB_ROOM - fv_cardW) / 2);
	fv_labW = 60;
	for (int k = 0; k < g_doc.nf; k++) fv_labW = imax (fv_labW, (int) ui_text_width ((long) U8 (g_doc.f[k].label), px, font_bold ()) + 1);	// (the lit name is bold)
	fv_labW = imin (fv_labW, imax (60, fv_cardW / 3));
	int x = fv_cardX + PAD + fv_labW + 14, full = imax (60, fv_cardX + fv_cardW - PAD - x);
	fv_edX = fv_x + x;
	int y = CARD_Y + HEAD_H;
	for (int k = 0; k < fv_ne; k++)
	{
		const Field &f = g_doc.f[k];
		int w = full, h = edh;
		switch (f.type)
		{
		case FT_MEMO: h = MEMO_H; break;
		case FT_INT: w = 170; break;
		case FT_DEC: w = 190; break;
		case FT_DATE: w = 170; break;
		case FT_COLOR: w = 190; break;
		case FT_BOOL: w = 24; break;
		case FT_CHOICE:
			w = 200;
			for (int i = 0; i < f.nch; i++) w = imax (w, (int) ui_text_width ((long) U8 (f.ch[i]), px, font ()) + 50);
			break;
		}
		w = imin (w, full);
		fv_y[k] = y; fv_h[k] = h; fv_w[k] = w;
		y += h + FV_GAP;
	}
	if (!fv_shows ()) y = CARD_Y + HEAD_H + 110;
	fv_cardH = y - FV_GAP + 18 - CARD_Y;
	fv_content = CARD_Y + fv_cardH + 14;
	fv_clamp ();
	fv_apply ();
}

// ---- the editors ----------------------------------------------------------------------------------------
static void fv_build ()
{
	for (int k = 0; k < fv_ne; k++)
	{
		ui_w_destroy (fv_ed[k]);
		if (fv_aux[k]) ui_w_destroy (fv_aux[k]);
		fv_aux[k] = 0;
	}
	fv_ne = 0;
	for (int k = 0; k < g_doc.nf; k++)
	{
		const Field &f = g_doc.f[k];
		int h = ctl_h ();
		long w = 0, a = 0;
		switch (f.type)
		{
		case FT_MEMO: w = ui_textarea (0, 0, 100, MEMO_H, UTA_VSCROLL | UTA_FORMTAB); break;
		case FT_INT: case FT_DEC: w = ui_textbox (0, 0, 100, h, UTF_RIGHT); break;
		case FT_DATE:
			w = ui_textbox (0, 0, 100, h, 0);
			ui_textbox_hint (w, (long) TR ("DD/MM/YYYY"));
			a = ui_pushbutton (0, 0, DATE_BTN, h, (long) "\xE2\x96\xBE", 0);	// (a down triangle)
			ui_w_tip (a, (long) "Pick the date on a calendar");
			break;
		case FT_COLOR:
			a = ui_colorpicker (0, 0, SWATCH_W, h, 0);
			ui_w_set_range (a, 0, 0x1000000, 0, 0);				// (ui_w_set_value keeps within the range)
			w = ui_textbox (0, 0, 100, h, 0);
			ui_textbox_hint (w, (long) TR ("None"));
			break;
		case FT_BOOL: w = ui_checkbox (0, 0, 24, h, (long) "", 0); break;
		case FT_CHOICE:
			w = ui_dropdown (0, 0, 100, h, (long) TR ("(none)"));
			for (int i = 0; i < f.nch; i++) ui_list_add (w, (long) U8 (f.ch[i]), 0);
			break;
		default: w = ui_textbox (0, 0, 100, h, 0); break;
		}
		fv_ed[k] = w; fv_aux[k] = a; fv_err[k] = false;
		ui_w_set_parent (w, fv);
		if (a) ui_w_set_parent (a, fv);
		fv_ne++;
	}
	fv_ver = g_fieldsVer;
	fv_rec = -2;
	fv_place ();
}

static void fv_set_error (int k, bool on)
{
	if (fv_err[k] == on) return;
	fv_err[k] = on;
	int t = g_doc.f[k].type;
	long st = t == FT_INT || t == FT_DEC ? UTF_RIGHT : 0;
	if (t != FT_MEMO && t != FT_BOOL && t != FT_CHOICE) ui_w_style (fv_ed[k], st | (on ? UTF_ERROR : 0));
}

// The record's value k into its editor.
static void fv_set_editor (int k, const char *v)
{
	long w = fv_ed[k];
	const Field &f = g_doc.f[k];
	fv_set_error (k, false);
	switch (f.type)
	{
	case FT_MEMO: w_set (w, v); break;
	case FT_COLOR:
	{
		unsigned c = jet (J_SURFACE);
		if (*v) parse_color (v, &c);
		w_set (w, v);
		ui_w_set_value (fv_aux[k], c);
		break;
	}
	case FT_BOOL: ui_w_set_value (w, v[0] != 0); break;
	case FT_CHOICE:
	{
		int base = 1 + f.nch;
		while (ui_list_count (w) > base) ui_list_remove (w, ui_list_count (w) - 1);
		int sel = 0;
		if (*v)
		{
			for (int i = 0; i < f.nch; i++) if (seq (f.ch[i], v)) sel = i + 1;
			if (!sel) { sel = (int) ui_list_add (w, (long) U8 (v), 0); }	// (a value not in the list: kept)
		}
		ui_w_set_value (w, sel);
		break;
	}
	default:
	{
		char s[VAL_MAX]; value_show (f, v, s, sizeof s);
		w_set (w, s);
	}
	}
}
// Editor k's content, as the field's value would be typed (Latin-1; good until the next few calls).
static const char *fv_editor_text (int k)
{
	long w = fv_ed[k];
	switch (g_doc.f[k].type)
	{
	case FT_BOOL: return ui_w_get_value (w) ? "yes" : "";
	case FT_CHOICE:
	{
		long i = ui_w_get_value (w), data;
		if (i <= 0) return "";
		long t = uikit_list_get (w, i, &data);
		return t ? L1 ((const char *) t) : "";
	}
	}
	return w_text (w);
}
// Does editor k still hold the record's value? (the text shown for it, or the same value typed otherwise)
static bool fv_same (int k)
{
	const Field &f = g_doc.f[k];
	const char *v = g_doc.r[fv_rec][k];
	const char *e = fv_editor_text (k);
	if (f.type == FT_TEXT || f.type == FT_MEMO)
	{
		char *p = value_new (f, e);
		bool s = p && seq (p, v);
		sfree (p);
		return s;
	}
	char shown[VAL_MAX]; value_show (f, v, shown, sizeof shown);
	if (seq (e, shown)) return true;
	char *p = value_new (f, e, 0, true);
	bool s = p && seq (p, v);
	sfree (p);
	return s;
}
static bool fv_live () { return fv_rec >= 0 && fv_rec == g_cur && fv_ver == g_fieldsVer && g_cur < g_doc.nr && fv_ne == g_doc.nf; }
static bool fv_dirty () { if (!fv_live ()) return false; for (int k = 0; k < fv_ne; k++) if (!fv_same (k)) return true; return false; }

static int fv_focused ()
{
	long f = ui_w_get_focus ();
	if (f) for (int k = 0; k < fv_ne; k++) if (f == fv_ed[k] || f == fv_aux[k]) return k;
	return -1;
}
static void fv_ensure_visible (int k)
{
	if (k < 0 || k >= fv_ne) return;
	int top = fv_y[k] - 12, bot = fv_y[k] + fv_h[k] + 12, y = fv_scroll;
	if (k == 0) top = 0;
	if (top < y) y = top;
	if (bot > y + fv_vh) y = bot - fv_vh;
	if (y != fv_scroll) fv_scroll_to (y);
}
static void fv_focus (int k, bool selectAll = true)
{
	bool show = fv_shows ();
	if (k < 0 || k >= fv_ne || !show) { if (!show && fv_btn) ui_w_focus (fv_btn); return; }
	ui_w_focus (fv_ed[k]);
	int t = g_doc.f[k].type;
	if (selectAll && (t == FT_TEXT || t == FT_INT || t == FT_DEC || t == FT_DATE))
		ui_textbox_select (fv_ed[k], 0, ui_w_text_len (fv_ed[k]));
	fv_ensure_visible (k);
}
static void fv_focus_first () { fv_focus (0, false); }

// The shown record's values into its editors (fields changed: the editors made again first). The field
// that had the keyboard keeps it.
static void fv_load ()
{
	int focused = fv_focused ();
	if (fv_ver != g_fieldsVer || fv_ne != g_doc.nf) fv_build ();
	bool show = g_cur >= 0 && g_cur < g_doc.nr;
	fv_rec = show ? g_cur : -1;
	for (int k = 0; k < fv_ne; k++)
	{
		ui_w_show (fv_ed[k], show);
		if (fv_aux[k]) ui_w_show (fv_aux[k], show);
		if (show) fv_set_editor (k, g_doc.r[g_cur][k]);
	}
	ui_w_show (fv_btn, !fv_shows () && g_doc.nf > 0);
	ui_w_set_text (fv_btn, (long) TR (g_doc.nr ? "Clear Search" : "New Record"));
	fv_place ();
	if (show && focused >= 0 && focused < fv_ne) ui_w_focus (fv_ed[focused]);
}

static void fv_invalid (int k)
{
	const Field &f = g_doc.f[k];
	int t = f.type;
	fv_set_error (k, true);
	fv_focus (k);
	char msg[300];
	const char *w;
	switch (t)
	{
	case FT_INT: w = "\"%s\" takes a whole number, such as 42 or -7."; break;
	case FT_DEC: w = "\"%s\" takes a number, such as 12.5 or 1 234,75."; break;
	case FT_DATE: w = "\"%s\" takes a date that exists, typed as DD/MM/YYYY (29/09/2026, 29.9.26, 2026-09-29) or picked on the calendar."; break;
	case FT_COLOR: w = "\"%s\" takes a colour: #3366CC, or a name such as navy."; break;
	default: w = "\"%s\": this value is not valid."; break;
	}
	snprintf (msg, sizeof msg, TL (w), f.label);
	ask ("Not a valid value", msg, UM_OK);
	fv_focus (k);
}
// The editors' values into the record. False: a value is not one of its type (said unless `quiet`; its
// editor outlined and given the keyboard) -- the record unchanged.
static bool fv_commit (bool quiet)
{
	if (!fv_live ()) return true;
	char *nv[MAXF]; bool any = false;
	for (int k = 0; k < fv_ne; k++) nv[k] = 0;
	for (int k = 0; k < fv_ne; k++)
	{
		if (fv_same (k)) continue;
		const Field &f = g_doc.f[k];
		bool exact = true;
		char *v = value_new (f, fv_editor_text (k), &exact, true);
		if (!v || (f.type == FT_INT && !exact))
		{
			sfree (v);
			for (int j = 0; j < k; j++) sfree (nv[j]);
			if (!quiet) fv_invalid (k);
			return false;
		}
		nv[k] = v; any = true;
	}
	if (!any) return true;
	undo_mark (-1);
	for (int k = 0; k < fv_ne; k++) if (nv[k]) value_set (g_doc.r[fv_rec][k], nv[k]);
	doc_changed (false);
	return true;
}
// Esc: the record's values back in the editors.
static void fv_revert ()
{
	if (!fv_dirty ()) return;
	int k = fv_focused ();
	fv_load ();
	if (k >= 0) fv_focus (k, false);
	status ("The record's changes were undone");
}

// ---- drawing --------------------------------------------------------------------------------------------
static void fv_paint (long, long w, long h)
{
	int px = ui_px ();
	unsigned face = jet (J_FACE), ink = jet (J_TEXTE1), dim = jet (J_TEXTE2);
	ui_fill (0, 0, w, h, jet (J_CREUX));
	int x = fv_cardX, y = CARD_Y - fv_scroll, cw = fv_cardW, ch = fv_cardH;
	ui_rbox (x, y, cw, ch, 10, jet (J_LIGNE), jet (J_LIGNE));
	ui_rbox (x + 1, y + 1, cw - 2, ch - 2, 9, face, face);		// (FACE: the editors are made for it)
	// the head: a band of the accent, the title, the description, the record's place
	unsigned band = ui_mix (face, jet (J_ACCENT), 46);
	ui_rbox (x + 1, y + 1, cw - 2, HEAD_H - 14, 9, band, band);
	ui_fill (x + 1, y + HEAD_H - 24, cw - 2, 11, band);
	ui_fill (x + 1, y + HEAD_H - 13, cw - 2, 1, ui_mix (face, jet (J_ACCENT), 110));
	char badge[40] = "";
	int pos = cur_pos ();
	if (g_cur >= 0 && pos >= 0)
	{
		if (g_cur == g_pin && rec_empty (g_doc, g_cur)) scpy (badge, TR ("New"), sizeof badge);
		else { itoa10 (pos + 1, badge); scat (badge, " / ", sizeof badge); scat_num (badge, g_nord, sizeof badge); }
	}
	int bw = badge[0] ? (int) ui_text_width ((long) badge, px, font_bold ()) + 22 : 0;
	int room = imax (1, cw - 2 * PAD - bw - 8);
	text_box (U8 (g_doc.title), x + PAD, y + 7, 20, ink, 15, font_bold (), room);
	if (g_doc.info[0]) text_box (U8 (g_doc.info), x + PAD, y + 27, 18, dim, px, font (), room);
	if (bw)
	{
		int bx = x + cw - PAD - bw + 8, by = y + 11;
		ui_rbox (bx, by, bw, 24, 12, jet (J_ACCENT), jet (J_ACCENT));
		text_box (badge, bx + 11, by, 24, 0xFFFFFF, px, font_bold (), bw - 16);
	}
	// the fields' names (the focused editor's in the accent), on the right of their column
	if (fv_shows ())
	{
		int fk = fv_focused ();
		for (int k = 0; k < fv_ne; k++)
		{
			const char *l = U8 (g_doc.f[k].label);
			bool f = k == fk;
			long fnt = f ? font_bold () : font ();
			int lw = imin (fv_labW, (int) ui_text_width ((long) l, px, fnt));
			text_box (l, x + PAD + imax (0, fv_labW - lw), fv_y[k] - fv_scroll, ctl_h (), f ? jet (J_ACCENT) : ink, px, fnt, fv_labW + 4);
		}
	}
	else
	{
		const char *a = TR (g_doc.nr ? "No record matches the search." : "This form has no records yet.");
		const char *b = TR (g_doc.nr ? "Clear the search to see them all." : "New Record (Ctrl+R) adds the first one.");
		if (!g_doc.nf) { a = TR ("This form has no fields."); b = TR ("The Design view adds them."); }
		int aw = (int) ui_text_width ((long) a, 13, font_bold ()), bwid = (int) ui_text_width ((long) b, px, font ());
		text_box (a, x + (cw - aw) / 2, y + HEAD_H + 6, 20, ink, 13, font_bold (), 0);
		text_box (b, x + (cw - bwid) / 2, y + HEAD_H + 28, 20, dim, px, font (), 0);
	}
}

static long fv_mouse (long, long kind, long mx, long my, long)
{
	if (kind != 1 || !fv_shows ()) return 0;
	int cy = (int) my + fv_scroll;				// a click on a field's name: its editor
	if (mx >= fv_cardX && mx < fv_cardX + PAD + fv_labW + 14)
		for (int k = 0; k < fv_ne; k++)
			if (cy >= fv_y[k] && cy < fv_y[k] + ctl_h ())
			{
				if (g_doc.f[k].type == FT_BOOL) ui_w_set_value (fv_ed[k], !ui_w_get_value (fv_ed[k]));
				fv_focus (k);
				ui_view_dirty (fv);
				return 0;
			}
	return 0;
}
static long fv_wheel (long, long n) { fv_scroll_to (fv_scroll - (int) n * 40); return 0; }

// ---- the keyboard, the controls' news (main.cpp's loop) -----------------------------------------------------
static void fv_next (int dir)
{
	if (!fv_ne || fv_rec < 0) return;
	int f = fv_focused ();
	int k = f < 0 ? (dir > 0 ? 0 : fv_ne - 1) : f + dir;
	fv_focus (iclamp (k, 0, fv_ne - 1));
}
// A key no control took.
static bool fv_key (int hid, int mods)
{
	bool ctrl = (mods & UI_MOD_CTRL) != 0;
	int f = fv_focused ();
	switch (hid)
	{
	case KEY_ENTER: if (f >= 0) { fv_next (1); return true; } return false;
	case KEY_DOWN: if (f >= 0) { fv_next (1); return true; } return false;
	case KEY_UP: if (f >= 0) { fv_next (-1); return true; } return false;
	case KEY_PGUP: goto_pos (cur_pos () - 1); return true;
	case KEY_PGDN: goto_pos (cur_pos () + 1); return true;
	case KEY_HOME: if (!ctrl) return false; goto_pos (0); return true;
	case KEY_END: if (!ctrl) return false; goto_pos (g_nord - 1); return true;
	case KEY_ESC: fv_revert (); return true;
	}
	return false;
}
static void fv_pick_date (int k)
{
	int y = 0, m = 0, d = 0, ty, tm, td;
	today (&ty, &tm, &td);
	long now = ty << 16 | tm << 8 | td;
	long date = parse_date (w_text (fv_ed[k]), &y, &m, &d) ? (y << 16 | m << 8 | d) : now;
	long r = ui_date_popup (fv_aux[k], date, now);
	if (r >= 0)
	{
		char s[12]; show_date ((int) (r >> 16), (int) (r >> 8 & 255), (int) (r & 255), s);
		w_set (fv_ed[k], s);
		fv_set_error (k, false);
	}
	ui_w_focus (fv_ed[k]);
}
// A control's news -> true: it was one of the form's.
static bool fv_event (long w, long code)
{
	if (w == fv_bar) { if (code == UN_CHANGE) fv_scroll_to ((int) ui_w_get_value (fv_bar)); return true; }
	if (w == fv_btn) { if (code == UN_ACTIVATE) { if (g_doc.nr) cmd_clear_search (); else cmd_new_record (); } return true; }
	for (int k = 0; k < fv_ne; k++)
	{
		if (w == fv_aux[k])
		{
			if (g_doc.f[k].type == FT_DATE && code == UN_ACTIVATE) fv_pick_date (k);
			if (g_doc.f[k].type == FT_COLOR && code == UN_CHANGE)
			{
				char s[8]; fmt_color ((unsigned) ui_w_get_value (w) & 0xFFFFFF, s);
				w_set (fv_ed[k], s);
				fv_set_error (k, false);
			}
			return true;
		}
		if (w == fv_ed[k])
		{
			int t = g_doc.f[k].type;
			if (code == UN_ACTIVATE && t != FT_MEMO) fv_next (1);		// (Enter in a line: the next field)
			if (code == UN_CHANGE)
			{
				fv_set_error (k, false);
				if (t == FT_COLOR)
				{
					unsigned c;
					if (parse_color (w_text (w), &c)) ui_w_set_value (fv_aux[k], c);
					else if (!w_text (w)[0]) ui_w_set_value (fv_aux[k], jet (J_SURFACE));
				}
			}
			return true;
		}
	}
	return false;
}

static void fv_create (int x, int y, int w, int h)
{
	fv_x = x; fv_top = y; fv_vw = w; fv_vh = h;
	fv = ui_view (x, y, w, h, 0);
	ui_view_on (fv, (long) fv_paint, (long) fv_mouse, 0, (long) fv_wheel);
	fv_bar = ui_scrollbar (x + w - SB_W - 2, y + 2, SB_W, h - 4, 0);
	ui_w_set_parent (fv_bar, fv);
	fv_btn = ui_pushbutton (0, 0, 160, ctl_h (), (long) "New Record", 0);
	ui_w_set_parent (fv_btn, fv);
	ui_w_show (fv_btn, 0);
}
static void fv_move (int x, int y, int w, int h)
{
	fv_x = x; fv_top = y; fv_vw = w; fv_vh = h;
	ui_w_move (fv, x, y, w, h);
	fv_place ();
}

} // namespace cf

#endif
