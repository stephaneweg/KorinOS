//
// dialogs.h -- Courier's dialogs on ui_dialog_* (Onyx's Apps/courier/dialogs.h): a question, a name asked, Save Request
// (its name, the collection and the folder it goes to), the code of the request (cURL, HTTP, Python, JavaScript),
// Import (a file, or text pasted: a cURL command, a Postman collection / environment), the cookie jar.
//
#ifndef _courier_dialogs_h
#define _courier_dialogs_h

#include "rail.h"

namespace cr {

static inline void dialog_size (int &W, int &H)
{
	long scr[4] = { 1024, 768, 0, 0 }; ui_screen ((long) scr);
	W = imin (W, (int) scr[0] - 20); H = imin (H, (int) (scr[1] - scr[2] - scr[3]) - 40);
}
static long no_fn (long, long, long) { return 0; }

// ---- a question ----------------------------------------------------------------------------------------------------------
static bool ask (const char *title, const char *text) { return ui_message ((long) title, (long) text, UM_OKCANCEL) == 1; }
static void tell (const char *title, const char *text) { ui_message ((long) title, (long) text, UM_OK); }

// ---- a name asked: false when cancelled (or empty) --------------------------------------------------------------------------
static bool ask_name (Str &out, const char *title, const char *label, const char *initial)
{
	char b[200]; s_copy (b, initial, sizeof b);
	if (ui_ask ((long) title, (long) label, (long) b, sizeof b) != 1) return false;
	Str t; s_trim (t, b);
	if (t.empty ()) return false;
	out = t;
	return true;
}

// ---- Save Request: its name, where -----------------------------------------------------------------------------------------
struct SaveDest { Item *folder; int depth; Collection *coll; };
static Vec<SaveDest> s_dests;
static long s_dlgNew, s_dlgName;
static void dest_folders (Item *f, int depth, Collection *c)
{
	for (int i = 0; i < f->kids.size (); i++)
		if (f->kids[i]->folder) { SaveDest d = { f->kids[i], depth, c }; s_dests.push (d); dest_folders (f->kids[i], depth + 1, c); }
}
static long save_fn (long d, long w, long code)
{
	if (w == s_dlgNew && code == UN_ACTIVATE) ui_dialog_close (d, 3);
	if (w == s_dlgName && code == UN_ACTIVATE) ui_dialog_close (d, 1);
	return 0;
}
// 1: saved to s_dests[*sel], with its name in name; 0: cancelled
static int save_dialog (Str &name, const char *initial, Item *preselect, int *sel)
{
	for (;;)
	{
		int W = 500, H = 430, pad = 16, ch = ctl_h ();
		dialog_size (W, H);
		s_dests.clear ();
		for (int i = 0; i < g_store.colls.size (); i++)
		{
			Collection *c = g_store.colls[i];
			SaveDest d = { &c->root, 0, c }; s_dests.push (d);
			dest_folders (&c->root, 1, c);
		}
		long d = ui_dialog_begin (W, H, (long) TR ("Save Request"));
		ui_label (pad, 12, W - 2 * pad, ch, (long) TR ("Request name"), 0);
		long e = s_dlgName = ui_textbox (pad, 12 + ch, W - 2 * pad, ch, 0);
		set_text (e, name.empty () ? initial : name.c ());
		ui_label (pad, 20 + 2 * ch, W - 2 * pad, ch, (long) TR ("Save to"), 0);
		long l = ui_list (pad, 20 + 3 * ch, W - 2 * pad, H - 20 - 3 * ch - 60, UL_NOSORT);
		int s = -1;
		for (int i = 0; i < s_dests.size (); i++)
		{
			SaveDest &x = s_dests[i];
			Str t; for (int k = 0; k < x.depth; k++) t.add ("    ");
			t.add (x.depth ? x.folder->name.c () : x.coll->name.c ());
			ui_list_add (l, (long) t.c (), i);
			if (x.folder == preselect) s = i;
		}
		if (s < 0 && s_dests.size ()) s = 0;
		if (*sel >= 0 && *sel < s_dests.size ()) s = *sel;
		if (s >= 0) ui_w_set_value (l, s);
		s_dlgNew = ui_pushbutton (pad, H - 44, btn_w (TR ("New Collection")), 0, (long) TR ("New Collection"), 0);
		long x = ui_dialog_button (d, 0, (long) TR ("Save"), 1, UB_DEFAULT, W - 12);
		ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
		ui_dialog_cancel (d, 0);
		ui_w_focus (e);
		long r = ui_dialog_run (d, (long) save_fn, 0);
		s_trim (name, tx (e));
		*sel = (int) ui_w_get_value (l);
		ui_dialog_end (d);
		if (r != 3) return (int) r;
		// a collection made: back here, on it
		int before = g_store.colls.size ();
		app_new_collection ();
		if (g_store.colls.size () > before) *sel = -1, preselect = &g_store.colls.last ()->root;
	}
}

// ---- the request's code -------------------------------------------------------------------------------------------------------
static struct { const Request *req; const Auth *auth; long lang, code, copy; } s_code;
static int s_codeLang = SN_CURL;
static void code_fill ()
{
	Str s; make_snippet (s, s_codeLang, *s_code.req, g_scope, *s_code.auth);
	set_text (s_code.code, s.c ());
}
static long code_fn (long, long w, long code)
{
	if (w == s_code.lang && code == UN_CHANGE) { s_codeLang = (int) ui_w_get_value (w); code_fill (); }
	else if (w == s_code.copy && code == UN_ACTIVATE) { ui_clip_set ((long) tx (s_code.code), ui_w_text_len (s_code.code)); app_status (TR ("The code is in the clipboard.")); }
	return 0;
}
static void code_dialog (const Request &r, const Auth &a)
{
	int W = 760, H = 500, pad = 16, ch = ctl_h ();
	dialog_size (W, H);
	s_code.req = &r; s_code.auth = &a;
	long d = ui_dialog_begin (W, H, (long) TR ("Code"));
	s_code.lang = ui_dropdown (pad, 12, 220, ch, (long) "cURL|HTTP|Python - requests|JavaScript - fetch");
	ui_w_set_value (s_code.lang, s_codeLang);
	ui_label (pad + 232, 12, W - 2 * pad - 232, ch, (long) TR ("The request as code, its variables resolved."), ULB_DIM);
	s_code.code = ui_textarea (pad, 20 + ch, W - 2 * pad, H - 20 - ch - 60, UTF_READONLY | UTF_MONO | UTA_VSCROLL | UTA_HSCROLL);
	code_fill ();
	s_code.copy = ui_pushbutton (pad, H - 44, btn_w (TR ("Copy to Clipboard")), 0, (long) TR ("Copy to Clipboard"), 0);
	ui_dialog_button (d, 0, (long) TR ("Close"), 0, UB_DEFAULT, W - 12);
	ui_dialog_cancel (d, 0);
	ui_dialog_run (d, (long) code_fn, 0);
	ui_dialog_end (d);
}

// ---- Import: 2 a file (path), 1 the text pasted (in text), 0 cancelled ------------------------------------------------------------
static long s_impFile;
static long import_fn (long d, long w, long code)
{
	if (w == s_impFile && code == UN_ACTIVATE) ui_dialog_close (d, 2);
	return 0;
}
static int import_dialog (Str &path, Str &text)
{
	int W = 700, H = 460, pad = 16, ch = ctl_h ();
	dialog_size (W, H);
	long d = ui_dialog_begin (W, H, (long) TR ("Import"));
	s_impFile = ui_pushbutton (pad, 12, 180, 0, (long) TR ("Choose a File..."), 0);
	ui_label (pad + 190, 12, W - 2 * pad - 190, ch, (long) TR ("A Postman collection or environment (.json), a cURL command (.txt)"), ULB_DIM);
	ui_label (pad, 22 + ch, W - 2 * pad, ch, (long) TR ("Or paste it here: a cURL command, or a collection's or an environment's JSON"), 0);
	long t = ui_textarea (pad, 26 + 2 * ch, W - 2 * pad, H - 26 - 2 * ch - 60, UTF_MONO | UTA_VSCROLL | UTA_HSCROLL | UTA_FORMTAB);
	long x = ui_dialog_button (d, 0, (long) TR ("Import"), 1, UB_DEFAULT, W - 12);
	ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	ui_w_focus (t);
	long r = ui_dialog_run (d, (long) import_fn, 0);
	text.set (tx (t), (int) ui_w_text_len (t));
	ui_dialog_end (d);
	if (r == 2)
	{
		char p[256];
		ui_file_filter ((long) "JSON files|*.json|All files|*");
		long ok = ui_file_open ((long) "SD0:/home", (long) p, sizeof p);
		ui_file_filter (0);
		if (!ok) return 0;
		path = p;
	}
	return (int) r;
}

// ---- the cookie jar ----------------------------------------------------------------------------------------------------------------
static long s_ckList, s_ckDel, s_ckHead;
static void cookies_fill ()
{
	ui_list_clear (s_ckList);
	for (int i = 0; i < g_jar.list.size (); i++)
	{
		Cookie &c = g_jar.list[i];
		Str d; cookie_text (d, c);
		if (!c.expires) { d.add ("  "); d.add (TR ("(session)")); }
		list_row (s_ckList, c.name.c (), c.value.c (), d.c ());
	}
	char b[96]; snprintf (b, sizeof b, TR ("%d cookie(s) kept, sent to their domains"), g_jar.list.size ());
	set_text (s_ckHead, b);
}
static long cookies_fn (long, long w, long code)
{
	if (w == s_ckDel && code == UN_ACTIVATE && g_jar.list.size ())
	{
		g_jar.list.clear (); g_jar.dirty = true;
		cookies_fill ();
		app_status (TR ("The cookies were deleted."));
	}
	return 0;
}
static void cookies_dialog ()
{
	int W = 720, H = 440, pad = 16, ch = ctl_h ();
	dialog_size (W, H);
	long d = ui_dialog_begin (W, H, (long) TR ("Cookies"));
	s_ckHead = ui_label (pad, 10, W - 2 * pad, ch, (long) "", ULB_DIM);
	s_ckList = ui_list (pad, 14 + ch, W - 2 * pad, H - 14 - ch - 60, UL_HEADER | UL_NOSORT | UL_HSCROLL);
	ui_list_column (s_ckList, (long) TR ("Name"), 140, 0); ui_list_column (s_ckList, (long) TR ("Value"), 200, 0); ui_list_column (s_ckList, (long) TR ("Domain, path, expiry"), 320, 0);
	cookies_fill ();
	s_ckDel = ui_pushbutton (pad, H - 44, btn_w (TR ("Delete All Cookies")), 0, (long) TR ("Delete All Cookies"), 0);
	ui_dialog_button (d, 0, (long) TR ("Close"), 0, UB_DEFAULT, W - 12);
	ui_dialog_cancel (d, 0);
	ui_dialog_run (d, (long) cookies_fn, 0);
	ui_dialog_end (d);
}

} // namespace cr

#endif
