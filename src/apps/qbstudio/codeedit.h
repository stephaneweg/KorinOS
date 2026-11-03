//
// codeedit.h -- l'éditeur de code de QBStudio : le texte multiligne de UIKit réglé pour lui (ui_textarea en mono, numéros de
// ligne, couleurs de BASIC, mots de bas::wordList), et la complétion : après le nom d'un contrôle et un point, ou à la
// demande, un menu (ui_popup_local) ou, s'il y a trop de noms, un dialogue à liste, puis ui_textarea_insert.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_codeedit_h
#define _qbstudio_codeedit_h

#include "app.h"

namespace qs {

static const long ED_FLAGS = UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_CODE | UTA_BASIC;
static inline const char *ed_text (long ed) { return (const char *) ui_w_get_text (ed); }

// Une complétion : ce qui peut venir ici. kind : 'p' propriété, 'm' méthode, 'c' contrôle, 's' SUB, 'k' mot, 'f' fonction de
// kit, 't' sa structure, 'K' kit
struct Compl { char name[40]; char kind; char detail[100]; };
typedef void (*ComplFn) (const char *object, Vec<Compl> &out);	// object : le nom avant le point ("" : aucun)
static ComplFn g_complete;

static inline char upc (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
static inline bool name_ch (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

// Le décalage du début d'une ligne (0 d'abord)
static long line_offset (const char *t, int ln)
{
	long p = 0;
	for (; ln > 0 && t[p]; p++) if (t[p] == '\n') ln--;
	return p;
}
static long ed_caret (long ed)
{
	long col = 0, line = uikit_textarea_caret (ed, &col);
	return line_offset (ed_text (ed), (int) line) + col;
}
// Le texte remplacé en une opération pour Annuler (le concepteur a écrit le formulaire)
static void ed_replace (long ed, const char *text, long caret)
{
	ui_textarea_edit (ed, UTE_ALL);
	ui_textarea_insert (ed, (long) text);
	ui_textarea_goto (ed, caret);
}

// Beaucoup de noms (un kit) : un dialogue, leur liste et ce qu'ils prennent
static long s_clist;
static long cl_fn (long d, long w, long code) { if (w == s_clist && code == UN_ACTIVATE) ui_dialog_close (d, 1); return 0; }
static int compl_dialog (const Vec<Compl> &c, const int *pick, int n)
{
	enum { DW = 560, DH = 400 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Complete"));
	s_clist = ui_list (12, 12, DW - 24, DH - 70, UL_HEADER | UL_NOSORT);
	ui_list_column (s_clist, (long) TR ("Name"), 200, 0);
	ui_list_column (s_clist, (long) TR ("Takes"), DW - 240, 0);
	for (int i = 0; i < n; i++) { char s[160]; snprintf (s, sizeof s, "%s\t%s", c[pick[i]].name, c[pick[i]].detail); ui_list_add (s_clist, (long) s, 0); }
	ui_w_set_value (s_clist, 0);
	long x = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	ui_w_focus (s_clist);
	long r = ui_dialog_run (d, (long) cl_fn, 0);
	long sel = ui_w_get_value (s_clist);
	ui_dialog_end (d);
	return r == 1 ? (int) sel : -1;
}
// La complétion au curseur : le préfixe déjà tapé, l'objet avant le point. (x, y) : où poser le menu. -> true : du texte est
// entré dans l'éditeur.
static bool qs_complete_at (long ed, int x, int y, bool dotOnly)
{
	if (!g_complete) return false;
	const char *t = ed_text (ed);
	long o = ed_caret (ed), p = o;
	while (p > 0 && name_ch (t[p - 1])) p--;
	char obj[40] = "", pre[40]; int pl = 0;
	for (long i = p; i < o && pl < 39; i++) pre[pl++] = t[i];
	pre[pl] = 0;
	if (p > 0 && t[p - 1] == '.')
	{
		long b = p - 1, a = b;
		while (a > 0 && name_ch (t[a - 1])) a--;
		int k = 0; for (long i = a; i < b && k < 39; i++) obj[k++] = t[i];
		obj[k] = 0;
		if (!k) return false;
	}
	else if (dotOnly) return false;
	Vec<Compl> all; g_complete (obj, all);
	int *pick = new int[all.n + 1], n = 0;
	for (int i = 0; i < all.n; i++) if (!strncasecmp (all[i].name, pre, pl)) pick[n++] = i;
	int c = -1;
	if (n && n <= 22)
	{
		static char L[22 * 44 + 2]; int k = 0;
		int wide = 120;
		for (int i = 0; i < n; i++)
		{
			const char *s = all[pick[i]].name;
			wide = imax (wide, tw (s) + 40);
			while (*s) L[k++] = *s++;
			L[k++] = 0;
		}
		L[k] = 0;
		c = (int) ui_popup_local (x, y, (long) L, wide);
	}
	else if (n) c = compl_dialog (all, pick, n);
	bool done = false;
	if (c >= 0 && c < n)
	{
		ui_w_focus (ed);
		ui_textarea_insert (ed, (long) (all[pick[c]].name + pl));
		done = true;
	}
	delete [] pick;
	return done;
}

} // namespace qs

#endif
