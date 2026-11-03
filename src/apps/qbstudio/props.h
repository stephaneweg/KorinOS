//
// props.h -- les propriétés de QBStudio : celles de l'élément choisi (son nom, son texte, sa mise en page, son aspect),
// une ligne chacune -- un texte tapé sur place (une ui_textbox enfant de la vue), une case cochée, un choix (ui_popup_local)
// --, et ses événements : la SUB de chacun (un clic l'écrit si elle n'existe pas). Un changement est fait à l'élément, puis
// dit à l'application (g_onFormEdited). La grille est une ui_view.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_props_h
#define _qbstudio_props_h

#include "designer.h"
#include "gen.h"

namespace qs {

enum { PR_NAME, PR_TEXT, PR_VALUE, PR_FLAG, PR_CHOICE, PR_HEAD };
struct PropRow { int type; const char *key; const char *label; const char *tip; };
static bool (*g_subExists) (const char *name);		// l'application : y a-t-il une telle SUB dans le code ?
static void (*g_openSub) (const char *name);		// ... la montrer (écrite d'abord s'il le faut)
static const char *g_formName = "Main";

// Les lignes d'un élément
static void rows_of (const El *e, Vec<PropRow> &r)
{
	auto add = [&] (int t, const char *k, const char *l, const char *tip) { PropRow p = { t, k, l, tip }; r.push (p); };
	int k = e->kind;
	add (PR_HEAD, 0, "Common", 0);
	if (k != K_SPACER && k != K_MENU && k != K_SEP && k != K_COMMENT) add (PR_NAME, "name", "Name", "the object in the code; its events' SUBs start so");
	if (k == K_WINDOW || k == K_LABEL || k == K_BUTTON || k == K_TEXTBOX || k == K_CHECKBOX || k == K_GROUP || k == K_STATUSBAR || k == K_MENUTITLE || k == K_MENUITEM)
		add (PR_TEXT, "text", k == K_WINDOW ? "Title" : "Text", 0);
	if (k == K_LISTBOX || k == K_DROPDOWN) add (PR_VALUE, "items", "Items", "a|b|c");
	if (k == K_MENUITEM) add (PR_VALUE, "key", "Shortcut", "Ctrl+Q");
	if (k == K_CHECKBOX) add (PR_FLAG, "checked", "Checked", 0);
	if (k == K_TEXTBOX) add (PR_FLAG, "readonly", "Read-only", 0);
	if (k == K_BUTTON) { add (PR_FLAG, "default", "Default", "Enter clicks it"); add (PR_FLAG, "cancel", "Cancel", "Esc clicks it"); }
	if (k == K_SLIDER) { add (PR_VALUE, "max", "Maximum", 0); add (PR_VALUE, "value", "Value", 0); }
	if (k == K_PROGRESS) add (PR_VALUE, "value", "Value", "0..100");
	if (k == K_HOST) add (PR_VALUE, "content", "Shows", "a user control's name (or by code: host.Content = Name)");
	if (is_control (k)) { add (PR_FLAG, "disabled", "Disabled", 0); add (PR_FLAG, "hidden", "Hidden", 0); }
	if (k == K_WINDOW && e->uc) { add (PR_HEAD, 0, "User control", 0); add (PR_VALUE, "size", "Size", "320x200: as drawn (a Host gives it its own)"); }
	else if (k == K_WINDOW) { add (PR_HEAD, 0, "Window", 0); add (PR_VALUE, "size", "Size", "380x260 (empty: as its contents)"); add (PR_VALUE, "min", "Smallest", "320x220"); add (PR_FLAG, "resizable", "Resizable", 0); }
	if (k == K_MENU || k == K_MENUTITLE || k == K_SEP || k == K_COMMENT || k == K_WINDOW) return;
	add (PR_HEAD, 0, "Layout", 0);
	// (un conteneur aussi : une Column de 200 pixels dans une Row, une autre qui remplit le reste)
	add (PR_VALUE, "width", "Width", is_container (k) ? "(as its contents)" : "(as its text)"); add (PR_VALUE, "height", "Height", is_container (k) ? "(as its contents)" : "(as its text)");
	add (PR_FLAG, "fill", "Fill", "the room left (a Row's), the width (a Column's)"); add (PR_VALUE, "grow", "Grow", "its share of the room");
	if (is_control (k) || k == K_SPACER || k == K_CANVAS) add (PR_CHOICE, "align", "Align", 0);
	// sa place en travers de son conteneur (une Column : halign ; une Row : valign ; une case de Grid : les deux)
	add (PR_VALUE, "halign", "H. align", "left, center, right, stretch");
	add (PR_VALUE, "valign", "V. align", "top, center, bottom, stretch");
	if (k == K_COLUMN || k == K_ROW || k == K_GROUP || k == K_GRID || k == K_TOOLBAR) { add (PR_VALUE, "padding", "Padding", 0); add (PR_VALUE, "gap", "Gap", 0); }
	if (k == K_ROW) { add (PR_CHOICE, "align", "Align", "its children packed"); add (PR_VALUE, "widths", "Widths", "200,*,100: its children's, in order"); }
	if (k == K_COLUMN || k == K_GROUP) add (PR_VALUE, "heights", "Heights", "40,*,auto: its children's, in order");
	if (k == K_GRID) { add (PR_VALUE, "cols", "Columns", 0); add (PR_VALUE, "widths", "Widths", "200,*,2*: pixels, shares of the rest, auto"); add (PR_VALUE, "heights", "Heights", "auto,*: the rows, the same way"); }
	if (k == K_CANVAS) add (PR_VALUE, "size", "Size", "200x120");
	if (e->parent && e->parent->kind == K_GRID) add (PR_VALUE, "cell", "Cell", "column,row");
	if (e->parent && e->parent->kind == K_CANVAS) add (PR_VALUE, "at", "At", "x,y");
}
static const char *const ALIGNS[] = { "(none)", "left", "center", "right" };

// Les événements d'un élément : les noms de leurs SUB
static int events_of (const El *e, char names[][64], const char *labels[], int cap)
{
	int n = 0;
	if (e->kind == K_WINDOW)
	{
		static const char *const W[] = { "Load", "Resize", "Close" }, *const U[] = { "Load", "Resize", "Show" };	// (un contrôle utilisateur : fait, placé, montré dans un Host)
		for (int i = 0; i < 3 && n < cap; i++) { const char *w = e->uc ? U[i] : W[i]; snprintf (names[n], 64, "%s_%s", g_formName, w); labels[n++] = w; }
		return n;
	}
	const char *ev = event_of (e->kind);
	if (!ev || !e->name[0]) return 0;
	snprintf (names[n], 64, "%s_%s", e->name, ev); labels[n++] = ev;
	return n;
}

// ---- la grille --------------------------------------------------------------------------------------------------------
enum { PG_TABS_H = 30, PG_SBW = 10 };
struct PropGrid
{
	long view, edit, bar;				// la vue, le champ d'un texte tapé (caché sinon), sa barre
	int X, Y, W, H;
	El *el;
	int tab;					// 0 propriétés, 1 événements
	int editRow, top;
	bool pend;					// un changement à dire au tour suivant
	Vec<PropRow> rows;
};
static PropGrid P;

static int pg_rh () { return ui_px () + 11; }
static int pg_head () { return ui_px () * 2 + 34; }
static int pg_lw () { return P.W * 42 / 100; }
static int pg_room () { return imax (1, P.H - pg_head ()); }
static int pg_content () { return P.tab ? 0 : P.rows.n * pg_rh (); }
static void pg_dirty () { ui_view_dirty (P.view); }
static void pg_bar ()
{
	int c = pg_content (), r = pg_room ();
	P.top = imax (0, imin (P.top, c - r));
	ui_w_move (P.bar, P.X + P.W - PG_SBW, P.Y + pg_head (), PG_SBW, r);
	ui_w_set_range (P.bar, 0, imax (c, 1), r, pg_rh ());
	ui_w_set_value (P.bar, P.top);
	ui_w_show (P.bar, c > r);
}
static void pg_value (const PropRow &r, char *o, int cap)
{
	o[0] = 0;
	if (!P.el) return;
	if (r.type == PR_NAME) cpy (o, P.el->name, cap);
	else if (r.type == PR_TEXT) cpy (o, P.el->text, cap);
	else { const char *v = P.el->get (r.key); if (v) cpy (o, v, cap); }
}
static bool name_ok (const char *s)
{
	if (s[0] >= '0' && s[0] <= '9') return false;
	for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')) return false;
	return true;
}
// Le champ retiré sans rien écrire (le formulaire a changé sous lui)
static void pg_drop_edit () { if (P.editRow >= 0) { P.editRow = -1; ui_w_show (P.edit, 0); pg_dirty (); } }
// La valeur tapée écrite dans l'élément
static void pg_commit ()
{
	if (P.editRow < 0) return;
	int i = P.editRow;
	char t[128]; cpy (t, (const char *) ui_w_get_text (P.edit), sizeof t);
	P.editRow = -1; ui_w_show (P.edit, 0); pg_dirty ();
	if (P.el && i < P.rows.n)
	{
		const PropRow &r = P.rows[i];
		char v[128]; pg_value (r, v, sizeof v);
		if (strcmp (v, t))
		{
			if (r.type == PR_NAME) { if (name_ok (t) && (!g_isWord || !g_isWord (t))) cpy (P.el->name, t, sizeof P.el->name); }
			else if (r.type == PR_TEXT) { cpy (P.el->text, t, sizeof P.el->text); P.el->hasText = true; }
			else P.el->set (r.key, t[0] ? t : 0);
			P.pend = true;			// (dit au tour suivant : l'application relit le formulaire)
		}
	}
}
static void pg_show (El *e)
{
	pg_commit ();
	P.el = e; P.rows.clear (); P.top = 0;
	if (e) rows_of (e, P.rows);
	pg_bar ();
	pg_dirty ();
}
// Le champ qui a perdu le clavier : ce qui est tapé est gardé (le tour de l'application)
static void pg_tick ()
{
	if (P.editRow >= 0 && ui_w_get_focus () != P.edit) pg_commit ();
	if (P.pend) { P.pend = false; if (g_onFormEdited) g_onFormEdited (); }
}
static void pg_changed () { pg_dirty (); if (g_onFormEdited) g_onFormEdited (); }

static long pg_paint (long, long W, long H)
{
	unsigned bg = jet (TJ_FACE), dim = jet (TJ_TEXTE3), line = jet (TJ_LIGNE), ink = jet (TJ_TEXTE1);
	ui_fill (0, 0, W, H, bg);
	if (!P.el) { text_c (TR ("Choose an element of the window"), 0, 0, (int) W, 80, dim); return 0; }
	// la tête : son nom, son genre
	int fh = ui_px () + 3;
	boxed (6, 6, (int) W - 12, fh + 12, 5, line, jet (TJ_SURFACE), jet (TJ_SURFACE));
	char n[64]; snprintf (n, sizeof n, "%s", P.el->name[0] ? P.el->name : P.el->kind == K_MENUITEM || P.el->kind == K_MENUTITLE ? P.el->text : "(no name)");
	text_box (n, 14, 6, fh + 12, ink, true, (int) W - 28);
	const char *kn = P.el->kind == K_MENUTITLE ? "Menu" : P.el->kind == K_MENUITEM ? "Menu item" : KIND_NAMES[P.el->kind];
	if (22 + tw (n, true) + tw (kn) < W - 12) text_box (kn, 22 + tw (n, true), 6, fh + 12, dim);
	// les onglets
	int ty = fh + 24, tabw = ((int) W - 12) / 2;
	const char *const TT[2] = { TR ("Properties"), TR ("Events") };
	for (int i = 0; i < 2; i++)
	{
		unsigned c = i == P.tab ? jet (TJ_ACCENT) : jet (TJ_SURFACE);
		ui_rbox (6 + i * tabw, ty, tabw, PG_TABS_H - 6, 4, c, c);
		text_c (TT[i], 6 + i * tabw, ty, tabw, PG_TABS_H - 6, i == P.tab ? ink_on (jet (TJ_ACCENT)) : ink);
	}
	int y0 = pg_head (), RH = pg_rh (), LW = pg_lw(), rw = (int) W - (pg_content () > pg_room () ? PG_SBW : 0);
	ui_clip (0, y0, W, imax (1, H - y0));
	if (P.tab == 0)
	{
		for (int i = 0; i < P.rows.n; i++)
		{
			int y = y0 + i * RH - P.top; if (y + RH < y0 || y > H) continue;
			const PropRow &r = P.rows[i];
			if (r.type == PR_HEAD) { text_box (TR (r.label), 8, y, RH, ink, true); continue; }
			if (i == P.editRow) ui_fill (0, y, rw, RH, jet (TJ_ACCENT_CLAIR));
			text_box (TR (r.label), 16, y, RH, ink, false, LW - 20);
			char v[128]; pg_value (r, v, sizeof v);
			if (r.type == PR_FLAG) check_mark (LW, y + (RH - 14) / 2, 14, P.el->flag (r.key));
			else if (!v[0] && r.tip) text_box (TR (r.tip), LW, y, RH, dim, false, rw - LW - 6);
			else
			{
				text_box (v, LW, y, RH, ink, false, rw - LW - (r.type == PR_CHOICE ? 20 : 6));
				if (r.type == PR_CHOICE) text_box ("\xE2\x96\xBE", rw - 16, y, RH, dim);
			}
			ui_fill (8, y + RH - 1, rw - 16, 1, line);
		}
	}
	else
	{
		char names[4][64]; const char *labels[4];
		int n2 = events_of (P.el, names, labels, 4);
		if (!n2) text_box (P.el->name[0] ? TR ("No events") : TR ("No events: give it a name"), 12, y0 + 4, RH, dim);
		for (int i = 0; i < n2; i++)
		{
			int y = y0 + i * RH;
			text_box (labels[i], 16, y, RH, ink);
			bool has = g_subExists && g_subExists (names[i]);
			if (has) { text_box (names[i], LW, y, RH, jet (TJ_ACCENT), false, rw - LW - 6); ui_fill (LW, y + RH - 6, imin (tw (names[i]), rw - LW - 6), 1, jet (TJ_ACCENT)); }
			else text_box (TR ("(click: a new SUB)"), LW, y, RH, dim, false, rw - LW - 6);
			ui_fill (8, y + RH - 1, rw - 16, 1, line);
		}
	}
	ui_clip (0, 0, 0, 0);
	return 0;
}
static long pg_mouse (long, long kind, long lx, long ly, long)
{
	if (kind != 1 || !P.el) return 0;
	int mx = (int) lx, my = (int) ly;
	int ty = ui_px () + 27;
	if (my >= ty && my < ty + PG_TABS_H) { pg_commit (); P.tab = mx < P.W / 2 ? 0 : 1; P.top = 0; pg_bar (); pg_dirty (); return 0; }
	int y0 = pg_head (); if (my < y0) return 0;
	int i = (my - y0 + (P.tab == 0 ? P.top : 0)) / pg_rh ();
	if (P.tab == 1)
	{
		char names[4][64]; const char *labels[4];
		int n = events_of (P.el, names, labels, 4);
		if (i >= 0 && i < n && g_openSub) g_openSub (names[i]);
		return 0;
	}
	if (i < 0 || i >= P.rows.n) { pg_commit (); return 0; }
	if (P.rows[i].type == PR_HEAD) return 0;
	pg_commit ();
	if (P.pend) { P.pend = false; if (g_onFormEdited) g_onFormEdited (); if (!P.el || i >= P.rows.n) return 0; }
	const PropRow &r = P.rows[i];
	if (r.type == PR_FLAG) { P.el->setFlag (r.key, !P.el->flag (r.key)); pg_changed (); return 0; }
	if (r.type == PR_CHOICE)
	{
		static char L[64]; int k = 0;
		for (int q = 0; q < 4; q++) { const char *s = ALIGNS[q]; while (*s) L[k++] = *s++; L[k++] = 0; }
		L[k] = 0;
		long c = ui_popup_local (P.X + pg_lw (), P.Y + y0 + (i + 1) * pg_rh () - P.top, (long) L, 120);
		if (c >= 0 && c < 4) { P.el->set (r.key, c == 0 ? 0 : ALIGNS[c]); pg_changed (); }
		return 0;
	}
	// un texte : tapé sur place
	char v[128]; pg_value (r, v, sizeof v);
	P.editRow = i;
	ui_w_set_text (P.edit, (long) v);
	int rw = P.W - (pg_content () > pg_room () ? PG_SBW : 0), h = imin (pg_rh () + 2, ctl_h ());
	ui_w_move (P.edit, P.X + pg_lw () - 4, P.Y + y0 + i * pg_rh () - P.top + (pg_rh () - h) / 2, rw - pg_lw (), h);
	ui_w_show (P.edit, 1);
	ui_w_focus (P.edit);
	ui_textbox_select (P.edit, 0, ui_w_text_len (P.edit));
	pg_dirty ();
	return 0;
}
static long pg_wheel (long, long n)
{
	pg_commit ();
	P.top = imax (0, P.top - (int) n * pg_rh ());
	pg_bar (); pg_dirty ();
	return 0;
}
static void pg_on_bar (long w, long) { P.top = (int) ui_w_get_value (w); if (P.editRow >= 0) pg_commit (); pg_dirty (); }
static void pg_on_edit (long, long code) { if (code == UN_ACTIVATE) { pg_commit (); ui_w_focus (P.view); } }
static void pg_create ()
{
	P.editRow = -1;
	P.view = ui_view (0, 0, 100, 100, UVW_FOCUS);
	ui_view_on (P.view, (long) pg_paint, (long) pg_mouse, 0, (long) pg_wheel);
	P.edit = ui_textbox (0, 0, 100, ctl_h (), 0);
	ui_w_set_parent (P.edit, P.view);
	ui_w_on (P.edit, (long) pg_on_edit, 0);
	ui_w_show (P.edit, 0);
	P.bar = ui_scrollbar (0, 0, PG_SBW, 100, 0);
	ui_w_set_parent (P.bar, P.view);
	ui_w_on (P.bar, (long) pg_on_bar, 0);
	ui_w_show (P.bar, 0);
}
static void pg_place (int x, int y, int w, int h) { pg_commit (); P.X = x; P.Y = y; P.W = w; P.H = h; ui_w_move (P.view, x, y, w, h); pg_bar (); pg_dirty (); }

} // namespace qs

#endif
