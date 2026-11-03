//
// contactsui.h -- Mail's Contacts, in place of the list and the reading pane (Onyx's Apps/mail/contactsui.h): the people
// A to Z, a search; the one chosen as Cardfile shows a card (name, e-mails, phones, company, address, birthday, notes);
// Write, Edit, Delete, a new one, Open in Cardfile (the same file: SD0:/home/documents/Contacts.card -- contacts.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef _mail_contactsui_h
#define _mail_contactsui_h

#include "wizard.h"

namespace mailapp {

static long g_kp, g_kSearch, g_kNew, g_kList, g_kCard, g_kWrite, g_kEdit, g_kDel, g_kFile;
static int  s_kOrder[4096], s_kN, s_kAll[4096];
static int  s_kX, s_kY, s_kW, s_kH;
static HitList s_kHits;
#define K_LW	imin (260, s_kW * 2 / 5)

static int k_sel () { long v = ui_w_get_value (g_kList); return v >= 0 && v < s_kN ? (int) v : -1; }
static long k_text (long, long row)
{
	static char t[300];
	if (row < 0 || row >= s_kN) return (long) "";
	Contact c; g_m.contacts.get (s_kOrder[row], c);
	scpy (t, c.name[0] ? c.name : c.email, sizeof t);
	return (long) t;
}
static long k_cell (long, long row, long, long x, long y, long w, long h, long sel)
{
	if (row < 0 || row >= s_kN) return 0;
	Contact c; g_m.contacts.get (s_kOrder[row], c);
	unsigned fg = sel ? ink_on (C_ACCENT) : C_TEXT;
	const char *n = c.name[0] ? c.name : c.email;
	int nw = imin (tw (n, F_UI, 1), (int) w * 3 / 5);
	text_v ((int) x + 8, (int) y, (int) h, n, fg, F_UI, 1, (int) w - 16);
	if (c.name[0]) text_v ((int) x + 16 + nw, (int) y, (int) h, c.email, sel ? fg : C_DIM, F_SMALL, 0, (int) w - nw - 24);
	return 1;
}
static void k_reload ()
{
	int all = imin (g_m.contacts.count (), 4096);
	static int *tmp; if (!tmp) tmp = (int *) malloc (sizeof (int) * (65536));
	if (g_m.contacts.count () < 65536) g_m.contacts.sorted (tmp);
	const char *q = tx (g_kSearch);
	s_kN = 0;
	for (int i = 0; i < all; i++)
	{
		if (q[0])
		{
			Contact c; g_m.contacts.get (tmp[i], c);
			if (!ifind (c.name, q) && !ifind (c.email, q) && !ifind (c.company, q)) continue;
		}
		s_kOrder[s_kN++] = tmp[i];
	}
	long v = ui_w_get_value (g_kList);
	ui_list_virtual (g_kList, s_kN, (long) k_text);
	ui_w_set_value (g_kList, v >= 0 && v < s_kN ? v : s_kN ? 0 : -1);
	ui_view_dirty (g_kCard);
	layout ();
}
static void k_card_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FIELD);
	s_kHits.clear ();
	int sel = k_sel ();
	int x = 30, W_ = (int) w - 2 * x;
	if (x * 2 + 200 > w) { x = 12; W_ = (int) w - 24; }
	if (sel < 0)
	{
		char t[200]; snprintf (t, sizeof t, TR ("Contacts are kept in %s"), CONTACTS_FILE);
		text_c (0, (int) h / 2 - 30, (int) w, 24, t, C_DIM, F_SMALL);
		text_c (0, (int) h / 2, (int) w, 24, TR ("Cardfile opens the same file."), C_DIM, F_SMALL);
		return;
	}
	Contact c; g_m.contacts.get (s_kOrder[sel], c);
	int cy = 20;
	avatar (x + 30, cy + 30, 30, c.name[0] ? c.name : c.email, c.email);
	text (x + 74, cy + 8, c.name[0] ? c.name : c.email, C_TEXT, F_H2, 1, W_ - 74);
	if (c.company[0]) text (x + 74, cy + 34, c.company, C_DIM, F_UI, 0, W_ - 74);
	cy += 72 + 46;			// (the buttons: widgets, children of the view)
	const char *L[8] = { TR ("E-mail"), TR ("Other e-mail"), TR ("Phone"), TR ("Mobile"), TR ("Company"), TR ("Address"), TR ("Birthday"), TR ("Notes") };
	const char *V[8] = { c.email, c.email2, c.phone, c.mobile, c.company, c.address, c.birthday, c.notes };
	for (int k = 0; k < 8; k++)
	{
		if (!V[k][0]) continue;
		text (x, cy, L[k], C_DIM, F_SMALL, 1, 112);
		char v[600]; scpy (v, V[k], sizeof v);
		if (k == 6) { int yy, mm, dd; if (sscanf (v, "%d-%d-%d", &yy, &mm, &dd) == 3 && mm >= 1) snprintf (v, sizeof v, "%d %s %d", dd, TR (MONTHS3[(mm - 1) % 12]), yy); }
		int lines = 0;
		for (char *ln = v; ln; lines++)
		{
			char *e = strchr (ln, '\n'); if (e) *e = 0;
			text (x + 120, cy + lines * 20, ln, k <= 1 ? col_accent_text () : C_TEXT, F_UI, 0, W_ - 120);
			ln = e ? e + 1 : 0;
			if (lines > 5) break;
		}
		if (k <= 1) s_kHits.add (x + 120, cy, imin (tw (V[k]), W_ - 120), 20, 1, k);
		cy += (lines ? lines : 1) * 20 + 12;
		ui_fill (x, cy - 6, W_, 1, C_LINE);
	}
}
static long k_card_mouse (long, long kind, long x, long y, long info)
{
	if (kind != 1 || !(info >> 8 & 1)) return 0;
	const Hit *h = s_kHits.at ((int) x, (int) y);
	if (h) later (L_CARD, *h);
	return 0;
}
static void k_create ()
{
	g_kp = ui_panel (0, 0, 10, 10, 0);
	g_kSearch = ui_textbox (0, 0, 10, ctl_h (), 0); ui_textbox_hint (g_kSearch, (long) TR ("Search the contacts")); ui_w_set_parent (g_kSearch, g_kp);
	g_kNew = ui_pushbutton (0, 0, 58, 0, (long) TR ("New"), 0); ui_w_set_parent (g_kNew, g_kp);
	g_kList = ui_list (0, 0, 10, 10, UL_NOSORT); ui_w_set_parent (g_kList, g_kp);
	ui_list_virtual (g_kList, 0, (long) k_text); ui_list_cell (g_kList, (long) k_cell);
	g_kCard = ui_view (0, 0, 10, 10, 0); ui_view_on (g_kCard, (long) k_card_paint, (long) k_card_mouse, 0, 0); ui_w_set_parent (g_kCard, g_kp);
	g_kWrite = ui_pushbutton (0, 0, 80, 0, (long) TR ("Write"), UB_DEFAULT); ui_w_set_parent (g_kWrite, g_kCard);
	g_kEdit = ui_pushbutton (0, 0, 80, 0, (long) TR ("Edit"), 0); ui_w_set_parent (g_kEdit, g_kCard);
	g_kDel = ui_pushbutton (0, 0, 80, 0, (long) TR ("Delete"), 0); ui_w_set_parent (g_kDel, g_kCard);
	g_kFile = ui_pushbutton (0, 0, 130, 0, (long) TR ("Open in Cardfile"), UB_GHOST); ui_w_set_parent (g_kFile, g_kCard);
}
static void k_place (int x, int y, int w, int h)
{
	s_kX = x; s_kY = y; s_kW = w; s_kH = h;
	int lw = K_LW, c = ctl_h ();
	ui_w_move (g_kp, x, y, w, h);
	ui_w_move (g_kSearch, x + 8, y + 8, lw - 16 - 64, c);
	ui_w_move (g_kNew, x + lw - 8 - 58, y + 8, 58, c);
	ui_w_move (g_kList, x, y + c + 16, lw, h - c - 16);
	ui_w_move (g_kCard, x + lw, y, w - lw, h);
	int bx = x + lw + (w - lw < 260 ? 12 : 30), by = y + 20 + 72 + 4;
	bool any = k_sel () >= 0;
	ui_w_show (g_kWrite, any); ui_w_show (g_kEdit, any); ui_w_show (g_kDel, any);
	ui_w_move (g_kWrite, bx, by, 72, c); ui_w_move (g_kEdit, bx + 78, by, 72, c); ui_w_move (g_kDel, bx + 156, by, 76, c);
	ui_w_move (g_kFile, any ? bx + 238 : x + lw + (w - lw - 130) / 2, any ? by : y + h / 2 + 30, 130, c);
	ui_view_dirty (g_kCard);
}

// ---- a contact edited: a dialogue ----------------------------------------------------------------------------------------------
static long no_dialog_fn (long, long, long) { return 0; }
static bool contact_box (Contact &c, const char *title)
{
	int W_ = 480, H_ = 430, pad = 16, ch = ctl_h ();
	long scr[4] = { 1024, 768, 0, 0 }; ui_screen ((long) scr);
	if (H_ > scr[1] - 60) H_ = (int) scr[1] - 60;
	long d = ui_dialog_begin (W_, H_, (long) title);
	char *v[8] = { c.name, c.email, c.email2, c.phone, c.mobile, c.company, c.address, c.birthday };
	int cap[8] = { sizeof c.name, sizeof c.email, sizeof c.email2, sizeof c.phone, sizeof c.mobile, sizeof c.company, sizeof c.address, sizeof c.birthday };
	static const char *const L[9] = { "Name", "E-mail", "Other e-mail", "Phone", "Mobile", "Company", "Address", "Birthday", "Notes" };
	long t[8]; int y = 12, row = (H_ - 56 - 12 - 54) / 8;
	for (int i = 0; i < 8; i++, y += row)
	{
		ui_label (pad, y, 110, ch, (long) TR (L[i]), 0);
		t[i] = ui_textbox (pad + 112, y, W_ - 2 * pad - 112, ch, 0);
		ui_w_set_range (t[i], 0, cap[i] - 1, 0, 0);
		set_text (t[i], v[i]);
	}
	ui_label (pad, y, 110, ch, (long) TR (L[8]), 0);
	long notes = ui_textarea (pad + 112, y, W_ - 2 * pad - 112, 50, UTA_FORMTAB); set_text (notes, c.notes);
	long x = ui_dialog_button (d, 0, (long) TR ("Keep"), 1, UB_DEFAULT, W_ - 12);
	ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	ui_w_focus (t[0]);
	long r = ui_dialog_run (d, (long) no_dialog_fn, 0);
	if (r == 1)
	{
		for (int i = 0; i < 8; i++) scpy (v[i], tx (t[i]), cap[i]);
		scpy (c.notes, tx (notes), sizeof c.notes);
	}
	ui_dialog_end (d);
	return r == 1;
}
static void k_new ()
{
	Contact c; memset (&c, 0, sizeof c);
	if (!contact_box (c, TR ("A new contact")) || (!c.name[0] && !c.email[0])) return;
	g_m.contacts.add (c); g_m.contacts.save ();
	set_text (g_kSearch, "");
	k_reload ();
	for (int i = 0; i < s_kN; i++) { Contact x; g_m.contacts.get (s_kOrder[i], x); if (!strcmp (x.name, c.name) && !strcmp (x.email, c.email)) ui_w_set_value (g_kList, i); }
	layout ();
}
static bool k_control (long w, long code)
{
	int sel = k_sel ();
	Contact c; if (sel >= 0) g_m.contacts.get (s_kOrder[sel], c);
	if (w == g_kSearch) { if (code == UN_CHANGE) k_reload (); return true; }
	if (w == g_kList) { layout (); return true; }
	if (w == g_kNew && code == UN_ACTIVATE) { k_new (); return true; }
	if (w == g_kFile && code == UN_ACTIVATE) { g_m.contacts.save (); spawn_app ("SD0:/apps/cardfile.app/main", CONTACTS_FILE); return true; }
	if (sel < 0) return false;
	if (w == g_kWrite && code == UN_ACTIVATE) { compose_to (c.name, c.email); return true; }
	if (w == g_kEdit && code == UN_ACTIVATE) { if (contact_box (c, c.name[0] ? c.name : TR ("Contact"))) { g_m.contacts.set (s_kOrder[sel], c); g_m.contacts.save (); k_reload (); } return true; }
	if (w == g_kDel && code == UN_ACTIVATE)
	{
		char q[300]; snprintf (q, sizeof q, TR ("Delete %s from the contacts?"), c.name[0] ? c.name : c.email);
		if (ask (q, UM_YESNO) == 1) { g_m.contacts.remove (s_kOrder[sel]); g_m.contacts.save (); k_reload (); }
		return true;
	}
	return false;
}

} // namespace mailapp

#endif
