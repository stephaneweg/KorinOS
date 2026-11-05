//
// app.h -- what Cardfile's views share: the document, the record shown, the records shown and their
// order, the window's look, and the app's functions they call (main.cpp).
//
// model.h keeps Latin-1, as the .card file does; UIKit speaks UTF-8. U8 () and L1 () are the only
// crossing: a text goes through one of them on its way to or from a widget.
//
#ifndef _cardfile_app_h
#define _cardfile_app_h

#include <stdlib.h>
#include <uikit.h>
#include "model.h"

namespace cf {

enum { V_FORM, V_LIST, V_DESIGN };

static struct ui_vars ui;
static Doc      g_doc;
static char     g_path[200];			// "" : not saved yet
static int      g_cur = -1;			// the record shown (its index in g_doc.r), -1: none
static int     *g_ord, g_nord, g_ordCap;	// the records shown (their indexes), in the views' order
static char     g_search[64];			// the search's words, Latin-1 ("" : every record)
static int      g_pin = -1;			// a record just made: shown whatever the search
static unsigned g_fieldsVer = 1;		// the fields' version (a view built for an older one: rebuilt)
static int      g_fsel;				// the field chosen in the design view
static int      g_view = V_FORM;

// ---- the theme: UIKit's tokens, never theme.txt ---------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

// The heights of UIKit's scales (uk_mt_tab): a control's, the interface's text.
static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)
static int ui_px () { return ui_echelle () >= 2 ? 12 : 13; }
static long font () { return (long) ui.font; }
static long font_bold () { return (long) ui.font_b; }

// ---- text: Latin-1 (the model) <-> UTF-8 (UIKit) ----------------------------------------------------------
// The answers live in a few buffers taken in turn: good until the next few calls.
static char *ring_buf (int need)
{
	static char *b[6]; static int cap[6], next;
	int i = next; next = (next + 1) % 6;
	if (cap[i] < need)
	{
		free (b[i]);
		cap[i] = need < 512 ? 512 : need;
		b[i] = (char *) malloc (cap[i]);
		if (!b[i]) { cap[i] = 0; static char none[1]; none[0] = 0; return none; }
	}
	return b[i];
}
static const char *U8 (const char *s)
{
	int n = 0;
	for (const char *p = s; *p; p++) n += (unsigned char) *p < 0x80 ? 1 : 2;
	char *o = ring_buf (n + 1), *q = o;
	for (; *s; s++)
	{
		unsigned char c = (unsigned char) *s;
		if (c < 0x80) *q++ = (char) c;
		else { *q++ = (char) (0xC0 | c >> 6); *q++ = (char) (0x80 | (c & 63)); }
	}
	*q = '\0';
	return o;
}
// (a character Latin-1 has not: '?'; the .card file could not hold it)
static const char *L1 (const char *s)
{
	char *o = ring_buf (slen (s) + 1), *q = o;
	while (*s)
	{
		unsigned char c = (unsigned char) *s++;
		if (c < 0x80) { *q++ = (char) c; continue; }
		int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		unsigned u = more == 1 ? c & 31 : more == 2 ? c & 15 : c & 7;
		for (int i = 0; i < more && (*s & 0xC0) == 0x80; i++) u = u << 6 | (*s++ & 63);
		*q++ = !more || u > 0xFF ? '?' : (char) u;
	}
	*q = '\0';
	return o;
}
// The texts: the source is in English, data/lang-fr.txt says them in French (UTF-8). TR for UIKit, TL for the model's flow (Latin-1).
#define TR(s)	((const char *) ui_tr ((long) (s)))
#define TL(s)	L1 (TR (s))
static const char *w_text (long w) { return L1 ((const char *) ui_w_get_text (w)); }	// a widget's text, Latin-1
static void w_set (long w, const char *latin1) { ui_w_set_text (w, (long) U8 (latin1)); }

// A line of text in a box, centred on its height (ui_text takes the baseline).
static void text_box (const char *u8, int x, int y, int h, unsigned ink, int px, long fnt, int maxw)
{
	ui_text_fit ((long) u8, x, y + h / 2 + px * 37 / 100, ink, px, fnt, maxw > 0 ? maxw : 2000);
}

// (main.cpp)
static int  cur_pos ();				// the record shown's place in g_ord, -1
static void goto_pos (int pos);			// show the record at that place (the edits kept first)
static void undo_mark (int key);		// before a change: the document kept for Undo (key >= 0: the
						// same edit going on -- one step for all of it)
static void doc_changed (bool fields);		// after a change: the order and the views follow
static void select_record (int rec);		// another record chosen (in the list)
static void undo_break ();			// the next change a step of its own
static void status (const char *msg);		// a word in the status bar for a few seconds
static void cmd_new_record ();
static void cmd_dup_record ();
static void cmd_del_record ();
static void cmd_clear_search ();
static void show_view (int v);
static int  ask (const char *title, const char *text, int buttons);	// (Latin-1) -> as ui_message

} // namespace cf

#endif
