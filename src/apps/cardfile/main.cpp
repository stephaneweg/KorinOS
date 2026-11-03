//
// cardfile -- Onyx's card file on ARMPI (src/apps/cardfile/README.md): a small database in the way of
// Microsoft Access, without SQL. One file (.card) holds one FORM -- a title, a description, its fields:
// each a display name, a column name and a type (one-line text, multi-line text, integer, decimal number
// with its decimals, date, colour, yes / no, a choice list with its choices) -- and its RECORDS. Three
// views, from the toolbar's switch or the View menu (F5, F6, F7):
//   * Form (formview.h): a record at a time, on an index card: the fields' names and their editors; the
//     navigator at the foot (first, previous, "Record 3 of 12", next, last, a new one);
//   * List (listview.h): the records in a list, a column per field; a click on a title sorts, again the
//     other way; a double click opens the record in the form;
//   * Design (designview.h): the form itself -- its fields added, removed, moved, named, typed.
// The search box (Ctrl+F) keeps the records holding every word typed (case and accents ignored); the
// sort and the search order both views. Undo / Redo (the whole document at each step: record edits,
// deletions, the form's design), unsaved changes asked about (New, Open, a file dropped), a document
// closed with unsaved changes kept in SD0:/home/cardfile/recovered.card and offered at the next start.
// CSV: File > Export as CSV... (the records shown, as shown), Import CSV... (a new form: the first line
// names the fields, each column's type guessed from its values). "cardfile SD0:/home/documents/x.card"
// opens a file; so does a file dropped on the window. Started without one, Cardfile opens the form it had
// last (SD0:/home/cardfile/last.txt), else a new form in the Design view.
//
// THE FILE (.card): text in Latin-1 (as Onyx writes), one form and its records --
//
//   # Onyx Cardfile ...                    '#' or ';' starts a comment (outside [records])
//   [form]
//   version     = 1
//   title       = Contacts
//   description = Friends, family and work
//   sort        = name                     the views' order: a column (order = descending)
//   [field]                                one section a field, in the form's order
//   column      = birthday                 the column's name: letters, digits, _ and -
//   label       = Birthday                 the display name
//   type        = date                     text, multiline, integer, decimal, date, colour, yesno, choice
//   decimals    = 2                        (decimal) 0 to 6
//   choice      = Family                   (choice) an option a line, in the list's order
//   [records]
//   name<TAB>birthday<TAB>...              the header: the columns (in any order)
//   Alice Martin<TAB>1990-03-14<TAB>...    a record a line, a tab between its values
//
// In a value \t, \n, \\ stand for a tab, a line break, a backslash (and "\[" for a '[' starting a line).
// A value as stored: text as typed; integer 42; decimal 12.50 (the field's decimals); date 2026-09-29
// (ISO -- shown 29/09/2026); colour #3366CC; yes / no: "yes", or empty; choice: the option's text. Read
// leniently: keys in any case, the sections in any order, a column missing from the header (empty
// values) or unknown (ignored), no [field] at all (the header's columns become text fields), a value
// that is not one of its type (kept as it is: the form asks for a valid one when it is edited).
//
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "formview.h"
#include "listview.h"
#include "designview.h"

namespace cf {

#define W 900
#define H 600
#define DOCK_H 96
#define HOME_DIR "SD0:/home/cardfile"
static const char *RECOVER = HOME_DIR "/recovered.card";
static const char *RECOVER_NAME = HOME_DIR "/recovered.txt";
static const char *LAST = HOME_DIR "/last.txt";			// the form opened or saved last
static const char *DOCS = "SD0:/home/documents";

enum {	M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_IMPORT, M_EXPORT,
	M_UNDO = 10, M_REDO, M_CUT, M_COPY, M_PASTE, M_FIND, M_CLEARFIND,
	M_NEWREC = 20, M_DUP, M_DEL, M_FIRST, M_PREV, M_NEXT, M_LAST, M_GOTO, M_MERGE, M_REVERT,
	M_VFORM = 30, M_VLIST, M_VDESIGN, M_FILEORDER,
	M_ADDF = 40, M_REMF, M_FUP, M_FDOWN };

static const char MENUS[] =
	"MFile\n"
	"I1\tNew Form\tCtrl+N\n" "I2\tOpen...\tCtrl+O\n" "-\n" "I3\tSave\tCtrl+S\n" "I4\tSave As...\n" "-\n"
	"I5\tImport CSV...\n" "I6\tExport as CSV...\n"
	"MEdit\n"
	"I10\tUndo\tCtrl+Z\n" "I11\tRedo\tCtrl+Y\n" "-\n" "I12\tCut\tCtrl+X\n" "I13\tCopy\tCtrl+C\n" "I14\tPaste\tCtrl+V\n" "-\n"
	"I15\tSearch...\tCtrl+F\n" "I16\tClear the Search\n"
	"MRecord\n"
	"I20\tNew Record\tCtrl+R\n" "I21\tDuplicate Record\tCtrl+D\n" "I22\tDelete Record...\n" "-\n"
	"I23\tFirst Record\n" "I24\tPrevious Record\n" "I25\tNext Record\n" "I26\tLast Record\n" "I27\tGo to Record...\tCtrl+G\n" "-\n"
	"I28\tMail Merge...\n" "-\n" "I29\tUndo the Record's Changes\n"
	"MView\n"
	"I30\tForm\tF5\n" "I31\tList\tF6\n" "I32\tDesign\tF7\n" "-\n" "I33\tIn the File's Order\n"
	"MDesign\n"
	"I40\tAdd Field\n" "I41\tRemove Field...\n" "I42\tMove Field Up\n" "I43\tMove Field Down\n";

static long     g_tb, g_seg, g_searchBox, g_nav, g_navR, g_lPos, g_lInfo, g_lFile;
static long     g_bUndo, g_bRedo, g_bNewRec, g_bDupRec, g_bDelRec, g_bNew, g_bOpen, g_bSave;
static long     g_bFirst, g_bPrev, g_bNext, g_bLast, g_bAdd;
static unsigned g_state, g_saved, g_seq;		// the document's state (a new one at each change), the one saved
static char     g_msg[128]; static unsigned g_msgT;	// a word in the status bar, since when
static int      g_pinUndo = -1;				// the undo steps when the new record was made
static int      TB_H, NAV_H;

static bool modified () { return g_state != g_saved; }
static void refresh ();
static void focus_view ();

// (10 ms)
static unsigned ticks ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) t.tv_sec * 100u + (unsigned) (t.tv_nsec / 10000000);
}

static int ask (const char *title, const char *text, int buttons)
{
	const char *m = TR (text);					// (a text put together: Latin-1, not in the dictionary)
	return (int) ui_message ((long) TR (title), (long) (m != text ? m : U8 (text)), buttons);
}

// ---- undo: the whole document kept before each change --------------------------------------------------------------
struct Snap { char *text; int len; unsigned state; int cur, fsel; };
enum { MAXUNDO = 100, UNDO_BYTES = 24 << 20 };
static Snap g_undo[MAXUNDO], g_redo[MAXUNDO];
static int  g_nundo, g_nredo, g_lastKey = -1;

static void snap_take (Snap &s)
{
	Out o; doc_write (g_doc, o);
	s.len = o.n; s.text = o.take (); s.state = g_state; s.cur = g_cur; s.fsel = g_fsel;
	if (!s.text) { s.text = new char[1]; s.text[0] = '\0'; }
}
static void snap_free (Snap &s) { delete [] s.text; s.text = 0; s.len = 0; }
static void drop_oldest (Snap *st, int &n) { if (!n) return; snap_free (st[0]); for (int i = 1; i < n; i++) st[i - 1] = st[i]; n--; }
static long undo_bytes () { long b = 0; for (int i = 0; i < g_nundo; i++) b += g_undo[i].len; for (int i = 0; i < g_nredo; i++) b += g_redo[i].len; return b; }
static void clear_history ()
{
	for (int i = 0; i < g_nundo; i++) snap_free (g_undo[i]);
	for (int i = 0; i < g_nredo; i++) snap_free (g_redo[i]);
	g_nundo = g_nredo = 0; g_lastKey = -1; g_pinUndo = -1;
}
static void undo_mark (int key)
{
	if (!(key >= 0 && key == g_lastKey))
	{
		if (g_nundo == MAXUNDO) drop_oldest (g_undo, g_nundo);
		snap_take (g_undo[g_nundo++]);
		for (int i = 0; i < g_nredo; i++) snap_free (g_redo[i]);
		g_nredo = 0;
		while (undo_bytes () > UNDO_BYTES && g_nundo > 1) { drop_oldest (g_undo, g_nundo); if (g_pinUndo > 0) g_pinUndo--; }
	}
	g_lastKey = key;
	g_state = ++g_seq;
}
static void undo_drop_last () { if (g_nundo) { g_state = g_undo[g_nundo - 1].state; snap_free (g_undo[--g_nundo]); } g_lastKey = -1; }
static void undo_break () { g_lastKey = -1; }

// ---- the order, the record shown ----------------------------------------------------------------------------------
static int cur_pos () { for (int i = 0; i < g_nord; i++) if (g_ord[i] == g_cur) return i; return -1; }

static void rebuild_order ()
{
	if (g_ordCap < g_doc.nr + 1) { delete [] g_ord; g_ordCap = g_doc.nr + 256; g_ord = new int[g_ordCap]; }
	if (g_pin >= g_doc.nr) g_pin = -1;
	if (g_cur >= g_doc.nr) g_cur = -1;
	g_nord = build_order (g_doc, g_search, g_pin, g_ord);
	if (cur_pos () < 0) g_cur = g_nord ? g_ord[0] : -1;
}

static void refresh_views ()
{
	switch (g_view)
	{
	case V_FORM: fv_load (); break;
	case V_LIST: lv_sync (); break;
	case V_DESIGN: dv_sync (); break;
	}
}

static void doc_changed (bool fields)
{
	if (fields) g_fieldsVer++;
	rebuild_order ();
	refresh_views ();
	refresh ();
}

// A new record left without a value (the one New Record made): dropped, as a database does -- and its
// undo step with it (nothing happened).
static bool drop_if_untouched (int rec)
{
	if (rec < 0 || rec != g_pin || rec >= g_doc.nr || !rec_empty (g_doc, rec)) return false;
	doc_del_record (g_doc, rec);
	if (g_cur > rec) g_cur--;
	else if (g_cur == rec) g_cur = -1;
	g_pin = -1;
	if (g_nundo == g_pinUndo && g_pinUndo > 0) undo_drop_last ();
	return true;
}

static void restore (Snap &s)
{
	char sortCol[COL_MAX] = ""; bool desc = g_doc.sortDesc;
	if (g_doc.sort >= 0) scpy (sortCol, g_doc.f[g_doc.sort].column, sizeof sortCol);	// (the order is the view's: kept)
	doc_read (g_doc, s.text, s.len, 0);
	g_doc.sort = sortCol[0] ? find_column (g_doc, sortCol) : -1;
	g_doc.sortDesc = g_doc.sort >= 0 && desc;
	g_state = s.state; g_cur = s.cur; g_fsel = s.fsel; g_pin = -1; g_lastKey = -1; g_pinUndo = -1;
	doc_changed (true);
}
static bool commit_edits () { return g_view != V_FORM || fv_commit (false); }
static void cmd_undo ()
{
	if (!commit_edits ()) return;
	if (!g_nundo) { status ("Nothing to undo"); return; }
	if (g_nredo == MAXUNDO) drop_oldest (g_redo, g_nredo);
	snap_take (g_redo[g_nredo++]);
	Snap s = g_undo[--g_nundo];
	restore (s);
	snap_free (s);
	status ("Undone");
}
static void cmd_redo ()
{
	if (!commit_edits ()) return;
	if (!g_nredo) { status ("Nothing to redo"); return; }
	if (g_nundo == MAXUNDO) drop_oldest (g_undo, g_nundo);
	snap_take (g_undo[g_nundo++]);
	Snap s = g_redo[--g_nredo];
	restore (s);
	snap_free (s);
	status ("Redone");
}

// ---- the status ---------------------------------------------------------------------------------------------------
static void status (const char *msg) { const char *t = TR (msg); scpy (g_msg, t != msg ? L1 (t) : msg, sizeof g_msg); g_msgT = ticks (); refresh (); }

static const char *base_name (const char *p)
{
	const char *b = p;
	for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1;
	return b;
}
static bool has_ext (const char *p, const char *ext)
{
	int n = slen (p), e = slen (ext);
	if (n < e) return false;
	for (int i = 0; i < e; i++) if (fold ((unsigned char) p[n - e + i]) != fold ((unsigned char) ext[i])) return false;
	return true;
}

// A label's text, when it changed (each change redraws it). [Latin-1]
static void label_set (long w, char *was, int cap, const char *s)
{
	if (seq (was, s)) return;
	scpy (was, s, cap);
	w_set (w, s);
}
static void tool_enable (long w, bool on, int *was)
{
	if (*was == (on ? 1 : 0)) return;
	*was = on ? 1 : 0;
	ui_w_enable (w, on);
}
static void menu_enable (int id, bool on, int *was)
{
	if (*was == (on ? 1 : 0)) return;
	*was = on ? 1 : 0;
	ui_menu_state (id, on ? 0 : UMS_DISABLED);
}

static void refresh ()
{
	if (!g_nav) return;
	static char sPos[48], sInfo[128], sFile[140];
	static int eFirst = -1, ePrev = -1, eNext = -1, eLast = -1, eUndo = -1, eRedo = -1, eNew = -1, eDup = -1, eDel = -1, mUndo = -1, mRedo = -1, mDup = -1, mDel = -1;
	static int seg = -1;
	char pos[48], info[128], file[140];
	int p = cur_pos ();
	if (!g_nord) scpy (pos, TL (g_doc.nr ? "No match" : "No record"), sizeof pos);
	else snprintf (pos, sizeof pos, TL ("Record %d of %d"), p + 1, g_nord);
	info[0] = '\0';
	if (g_msg[0] && ticks () - g_msgT < 400) scpy (info, g_msg, sizeof info);
	else if (g_view == V_DESIGN)
	{
		char a[48], b[48];
		snprintf (a, sizeof a, TL (g_doc.nf == 1 ? "%d field" : "%d fields"), g_doc.nf);
		snprintf (b, sizeof b, TL (g_doc.nr == 1 ? "%d record" : "%d records"), g_doc.nr);
		snprintf (info, sizeof info, "%s, %s", a, b);
	}
	else
	{
		if (g_search[0]) snprintf (info, sizeof info, TL ("%d of %d records match"), g_nord, g_doc.nr);
		else snprintf (info, sizeof info, TL (g_doc.nr == 1 ? "%d record" : "%d records"), g_doc.nr);
		if (g_doc.sort >= 0) { char o[96]; snprintf (o, sizeof o, TL (g_doc.sortDesc ? ", sorted by %s (descending)" : ", sorted by %s"), g_doc.f[g_doc.sort].label); scat (info, o, sizeof info); }
	}
	scpy (file, g_path[0] ? base_name (g_path) : TL ("Not saved yet"), sizeof file);
	bool edits = g_view == V_FORM && fv_dirty ();		// (edits not yet in the record: Undo takes them too)
	if (modified () || edits) scat (file, " *", sizeof file);
	label_set (g_lPos, sPos, sizeof sPos, pos);
	label_set (g_lInfo, sInfo, sizeof sInfo, info);
	label_set (g_lFile, sFile, sizeof sFile, file);
	bool nav = g_view != V_DESIGN;
	tool_enable (g_bFirst, nav && p > 0, &eFirst);
	tool_enable (g_bPrev, nav && p > 0, &ePrev);
	tool_enable (g_bNext, nav && p >= 0 && p < g_nord - 1, &eNext);
	tool_enable (g_bLast, nav && p >= 0 && p < g_nord - 1, &eLast);
	tool_enable (g_bUndo, g_nundo > 0 || edits, &eUndo);
	tool_enable (g_bRedo, g_nredo > 0, &eRedo);
	tool_enable (g_bNewRec, g_doc.nf > 0, &eNew);
	tool_enable (g_bDupRec, g_cur >= 0 && g_view != V_DESIGN, &eDup);
	tool_enable (g_bDelRec, g_cur >= 0 && g_view != V_DESIGN, &eDel);
	menu_enable (M_UNDO, g_nundo > 0 || edits, &mUndo);
	menu_enable (M_REDO, g_nredo > 0, &mRedo);
	menu_enable (M_DUP, g_cur >= 0 && g_view != V_DESIGN, &mDup);
	menu_enable (M_DEL, g_cur >= 0 && g_view != V_DESIGN, &mDel);
	if (seg != g_view) { seg = g_view; ui_w_set_value (g_seg, g_view); }
}

// ---- the views ------------------------------------------------------------------------------------------------------
static void focus_view ()
{
	if (g_view == V_FORM) { if (fv_focused () < 0) fv_focus_first (); }
	else if (g_view == V_LIST) ui_w_focus (lv);
	else ui_w_focus (dv_list);
}
static void show_pages (int v)
{
	ui_w_show (fv, v == V_FORM);
	ui_w_show (lv_panel, v == V_LIST);
	ui_w_show (dv, v == V_DESIGN);
}
static void show_view (int v)
{
	if (v == g_view) { focus_view (); return; }
	if (g_view == V_FORM)
	{
		if (!fv_commit (false)) { ui_w_set_value (g_seg, V_FORM); return; }
		drop_if_untouched (g_cur);
	}
	undo_break ();
	g_view = v;
	rebuild_order ();
	refresh_views ();
	show_pages (v);
	refresh ();
	focus_view ();
}

// Another record: the record at place `pos` of the order (the edits of the one shown kept first).
static void goto_pos (int pos)
{
	if (!g_nord) return;
	pos = iclamp (pos, 0, g_nord - 1);
	if (pos == cur_pos ()) return;
	int target = g_ord[pos];
	if (g_view == V_FORM && !fv_commit (false)) return;
	int was = g_cur;
	g_cur = target;
	if (drop_if_untouched (was) && target > was) g_cur = target - 1;
	if (g_pin != g_cur) g_pin = -1;
	undo_break ();
	rebuild_order ();
	refresh_views ();
	refresh ();
}
static void select_record (int rec)
{
	if (rec == g_cur) return;
	int was = g_cur;
	g_cur = rec;
	if (drop_if_untouched (was)) { if (rec > was) g_cur = rec - 1; rebuild_order (); refresh_views (); }
	refresh ();
}
static void cmd_goto ()
{
	if (!g_nord) return;
	if (g_view == V_FORM && !fv_commit (false)) return;
	char b[24]; itoa10 (cur_pos () + 1, b);
	char q[64]; snprintf (q, sizeof q, TR ("Record (1 to %d):"), g_nord);
	if (ui_ask ((long) TR ("Go to Record"), (long) q, (long) b, sizeof b) == 1)
	{
		long long n = 0; bool ex;
		if (parse_num (b, 0, &n, &ex)) goto_pos ((int) (n < 1 ? 1 : n > g_nord ? g_nord : n) - 1);
	}
	focus_view ();
}

// ---- the records ------------------------------------------------------------------------------------------------------
static void cmd_new_record ()
{
	if (!g_doc.nf) { ask ("New Record", "This form has no fields yet: add them in the Design view first.", UM_OK); return; }
	if (g_view == V_FORM && !fv_commit (false)) return;
	if (g_cur >= 0 && g_cur == g_pin && rec_empty (g_doc, g_cur))		// (a new one is there already)
	{
		if (g_view != V_FORM) show_view (V_FORM);
		fv_focus_first ();
		return;
	}
	if (g_view != V_FORM)
	{
		show_view (V_FORM);
		if (g_view != V_FORM) return;
	}
	undo_mark (-1);
	g_pinUndo = g_nundo;
	g_cur = doc_add_record (g_doc, -1);
	g_pin = g_cur;
	doc_changed (false);
	fv_focus_first ();
	status ("New record: type its values");
}
static void cmd_dup_record ()
{
	if (g_cur < 0 || g_view == V_DESIGN) return;
	if (g_view == V_FORM && !fv_commit (false)) return;
	undo_mark (-1);
	int src = g_cur, at = doc_add_record (g_doc, src + 1);
	for (int k = 0; k < g_doc.nf; k++) g_doc.r[at][k] = sdup (g_doc.r[src][k]);
	if (g_pin >= at) g_pin++;
	g_cur = at; g_pin = at;
	doc_changed (false);
	status ("Record duplicated");
}
static void cmd_del_record ()
{
	if (g_cur < 0 || g_view == V_DESIGN) return;
	char msg[200] = "", v0[60] = "";
	for (int k = 0; k < g_doc.nf; k++)
		if ((g_doc.f[k].type == FT_TEXT || g_doc.f[k].type == FT_CHOICE) && g_doc.r[g_cur][k][0])
		{
			char v[48]; scpy (v, g_doc.r[g_cur][k], sizeof v);
			snprintf (v0, sizeof v0, " \"%s\"", v);
			break;
		}
	snprintf (msg, sizeof msg, TL ("Delete the record%s? Edit > Undo brings it back."), v0);
	if (ask ("Delete Record", msg, UM_YESNO) != 1) { focus_view (); return; }
	int p = cur_pos (), del = g_cur;
	int next = p + 1 < g_nord ? g_ord[p + 1] : p > 0 ? g_ord[p - 1] : -1;
	undo_mark (-1);
	doc_del_record (g_doc, del);
	if (g_pin == del) g_pin = -1; else if (g_pin > del) g_pin--;
	g_cur = next > del ? next - 1 : next;
	doc_changed (false);
	focus_view ();
	status ("Record deleted");
}

// ---- the search -------------------------------------------------------------------------------------------------------
static void on_search ()
{
	if (g_view == V_FORM && !fv_commit (false)) { w_set (g_searchBox, g_search); return; }
	if (g_view == V_FORM) drop_if_untouched (g_cur);
	scpy (g_search, w_text (g_searchBox), sizeof g_search);
	g_pin = -1;
	rebuild_order ();
	refresh_views ();
	refresh ();
}
static void cmd_clear_search () { ui_w_set_text (g_searchBox, (long) ""); on_search (); focus_view (); }
static void cmd_find () { ui_w_focus (g_searchBox); ui_textbox_select (g_searchBox, 0, ui_w_text_len (g_searchBox)); }
static void cmd_file_order () { g_doc.sort = -1; g_doc.sortDesc = false; rebuild_order (); refresh_views (); status ("In the file's order"); }

// ---- the clipboard: to the control with the keyboard, as Onyx ---------------------------------------------------------
static void key_to_focus (int hid)
{
	int e[8] = { WEV_KEY, 1, 0, 0, hid, 0, UI_MOD_CTRL, ui_ev_field (&ui, EV_WIN) };
	uikit_call3 ((long (*) ()) ui_w_event, (long) e, 0, 0, 0, 0, 0);
}

// ---- the files ------------------------------------------------------------------------------------------------------------
static bool read_all (const char *path, char **out, int *len)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0) return false;
	struct stat st;
	long sz = fstat (fd, &st) == 0 ? (long) st.st_size : 0;
	if (sz > 64L << 20) sz = 64L << 20;
	char *b = new char[sz + 1];
	long n = read (fd, b, (size_t) sz);
	close (fd);
	if (n < 0) n = 0;
	b[n] = '\0';
	*out = b; *len = (int) n;
	return true;
}
static bool save_file (const char *path, const char *b, int n)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return false;
	long w = n ? write (fd, b, (size_t) n) : 0;
	return close (fd) == 0 && w == n;
}

// A text in UTF-8 (a .csv from elsewhere): the model's Latin-1, in place. Latin-1 itself is left alone:
// its accents do not read as valid UTF-8.
static void utf8_to_model (char *t, int *len)
{
	bool high = false;
	for (int i = 0; i < *len; )
	{
		unsigned char c = (unsigned char) t[i];
		if (c < 0x80) { i++; continue; }
		int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : -1;
		if (more < 0 || i + more >= *len + 1) return;
		for (int k = 1; k <= more; k++) if (((unsigned char) t[i + k] & 0xC0) != 0x80) return;
		high = true;
		i += more + 1;
	}
	if (!high) return;
	const char *o = L1 (t);
	*len = slen (o);
	memcpy (t, o, *len + 1);
}

// A document just read (or made): the state from scratch.
static void doc_fresh (const char *path, bool saved, int view)
{
	scpy (g_path, path, sizeof g_path);
	clear_history ();
	g_state = ++g_seq; g_saved = saved ? g_state : 0;
	g_cur = -1; g_pin = -1; g_fsel = 0;
	g_search[0] = '\0';
	if (g_searchBox) ui_w_set_text (g_searchBox, (long) "");
	g_fieldsVer++;
	g_view = view;
	rebuild_order ();
	refresh_views ();
	show_pages (view);
	refresh ();
	focus_view ();
	char t[220] = "Cardfile";
	if (g_path[0]) { scat (t, " - ", sizeof t); scat (t, base_name (g_path), sizeof t); }
	ui_title ((long) U8 (t));
}

static Doc s_tmp;
static void remember (const char *path) { save_file (LAST, path, slen (path)); }
// A .card file (or a CSV imported) as the document; quiet: no word when it cannot be read.
static bool load_path (const char *path, bool quiet = false)
{
	char *b; int n;
	if (!read_all (path, &b, &n)) { if (!quiet) ask ("Open", "This file could not be read.", UM_OK); return false; }
	bool csv = has_ext (path, ".csv") || has_ext (path, ".tsv");
	const char *why = "This file is not a Cardfile document.";
	bool ok;
	doc_init (s_tmp);
	utf8_to_model (b, &n);
	if (csv)
	{
		char title[TITLE_MAX]; scpy (title, base_name (path), sizeof title);
		int t = slen (title); while (t > 0 && title[t - 1] != '.') t--;
		if (t > 1) title[t - 1] = '\0';
		if (title[0] >= 'a' && title[0] <= 'z') title[0] = (char) (title[0] - 32);
		ok = csv_read (s_tmp, b, n, title);
		why = "This file holds no CSV table.";
	}
	else ok = doc_read (s_tmp, b, n, &why);
	delete [] b;
	if (!ok) { doc_clear (s_tmp); if (!quiet) ask ("Open", why, UM_OK); return false; }
	doc_clear (g_doc);
	g_doc = s_tmp;				// (the fields and the records handed over)
	doc_init (s_tmp);
	int view = g_view == V_DESIGN ? V_FORM : g_view;
	if (csv) view = V_LIST;
	doc_fresh (csv ? "" : path, !csv, view);
	if (!csv) remember (path);
	if (csv)
	{
		char m[96]; snprintf (m, sizeof m, TL ("Imported: %d records, %d fields"), g_doc.nr, g_doc.nf);
		status (m);
	}
	return true;
}
static bool write_path (const char *path)
{
	Out o; doc_write (g_doc, o);
	if (!save_file (path, o.b, o.n)) { ask ("Save", "The file could not be written.", UM_OK); return false; }
	scpy (g_path, path, sizeof g_path);
	g_saved = g_state;
	remember (path);
	char m[160]; snprintf (m, sizeof m, TL ("Saved %s"), base_name (path));
	status (m);
	return true;
}
// A file's name from the form's title: "My Books" -> "My Books.card"
static void default_name (char *out, int cap, const char *ext)
{
	if (g_path[0])
	{
		scpy (out, base_name (g_path), cap);
		int n = slen (out); while (n > 0 && out[n - 1] != '.') n--;
		if (n > 1) out[n - 1] = '\0';
	}
	else
	{
		int n = 0;
		for (const char *p = g_doc.title; *p && n < cap - 8; p++)
			if (*p != '/' && *p != '\\' && *p != ':' && *p != '*' && *p != '?' && *p != '"' && *p != '<' && *p != '>' && *p != '|') out[n++] = *p;
		out[n] = '\0';
		if (!n) scpy (out, g_wUntitled, cap);
	}
	scat (out, ext, cap);
}
static void cmd_save_as ()
{
	if (!commit_edits ()) return;
	char path[200], def[96], u[120];
	default_name (def, sizeof def, ".card");
	scpy (u, U8 (def), sizeof u);
	ui_file_filter ((long) "*.card");
	if (ui_file_save ((long) DOCS, (long) u, (long) path, sizeof path))
	{
		if (!has_ext (path, ".card")) scat (path, ".card", sizeof path);
		write_path (path);
	}
	ui_file_filter (0);
	focus_view ();
}
static void cmd_save ()
{
	if (!commit_edits ()) return;
	if (!g_path[0] || !has_ext (g_path, ".card")) { cmd_save_as (); return; }
	write_path (g_path);
	focus_view ();
}
// Unsaved changes: saved (Yes), dropped (No) or kept (Cancel -> false).
static bool confirm_discard ()
{
	if (!commit_edits ()) return false;
	if (!modified ()) return true;
	char m[200]; snprintf (m, sizeof m, TL ("Save the changes to %s?"), g_path[0] ? base_name (g_path) : g_doc.title);
	int r = ask ("Unsaved changes", m, UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { cmd_save (); return !modified (); }
	return true;
}
static void cmd_new ()
{
	if (!confirm_discard ()) { focus_view (); return; }
	doc_new (g_doc);
	doc_fresh ("", true, V_DESIGN);
	dv_focus_label ();
	status ("A new form: name its fields and choose their types, then add records");
}
static void cmd_open ()
{
	if (!confirm_discard ()) { focus_view (); return; }
	char path[200];
	ui_file_filter ((long) "*.card;*.csv;*.tsv");
	if (ui_file_open ((long) DOCS, (long) path, sizeof path)) load_path (path);
	ui_file_filter (0);
	focus_view ();
}
static void cmd_export_csv ()
{
	if (!commit_edits ()) return;
	char path[200], def[96], u[120];
	default_name (def, sizeof def, ".csv");
	scpy (u, U8 (def), sizeof u);
	ui_file_filter ((long) "*.csv");
	if (ui_file_save ((long) DOCS, (long) u, (long) path, sizeof path))
	{
		if (!has_ext (path, ".csv")) scat (path, ".csv", sizeof path);
		Out o; csv_write (g_doc, g_ord, g_nord, o);
		if (!save_file (path, o.b ? o.b : "", o.n)) ask ("Export", "The file could not be written.", UM_OK);
		else { char m[96]; snprintf (m, sizeof m, TL (g_nord == 1 ? "Exported %d record" : "Exported %d records"), g_nord); status (m); }
	}
	ui_file_filter (0);
	focus_view ();
}
// Mail Merge: Letters' letter (its fields the form's) filled with this record, or each record shown: one document in
// Letters, each letter on a new page (its own Tools > Mail Merge writes them as files). Letters does it: "letters --merge
// JOB" (src/apps/letters/merge.h), JOB and the records written in SD0:/home/cardfile/.
static void cmd_mail_merge ()
{
	if (!commit_edits ()) return;
	if (g_doc.nf == 0 || g_nord == 0) { ask ("Mail Merge", "The form has no records to merge.", UM_OK); focus_view (); return; }
	char letter[200] = "";
	ui_file_filter ((long) "*.rtf;*.docx;*.odt");
	long got = ui_file_open ((long) DOCS, (long) letter, sizeof letter);
	ui_file_filter (0);
	if (!got) { focus_view (); return; }
	char q[120]; snprintf (q, sizeof q, TL ("Merge all the %d records shown? (No: this record only)"), g_nord);
	int a = ask ("Mail Merge", q, UM_YESNOCANCEL);
	if (a == 0) { focus_view (); return; }
	bool one = a != 1 && g_cur >= 0;
	int n = one ? 1 : g_nord;
	char ***keep = g_doc.r; int keepN = g_doc.nr;
	char ***sub = new char **[n];
	if (one) sub[0] = g_doc.r[g_cur]; else for (int i = 0; i < n; i++) sub[i] = g_doc.r[g_ord[i]];
	g_doc.r = sub; g_doc.nr = n;
	Out o; doc_write (g_doc, o);
	g_doc.r = keep; g_doc.nr = keepN;
	delete [] sub;
	static const char *DATA = HOME_DIR "/merge.card", *JOB = HOME_DIR "/merge.job";
	bool ok = save_file (DATA, o.b ? o.b : "", o.n);
	char job[600];
	snprintf (job, sizeof job, "template = %s\ndata = %s\nrecords = all\noutput = open\n", letter, DATA);
	if (ok) ok = save_file (JOB, job, (int) strlen (job));
	char *argv[] = { (char *) "SD0:/apps/letters.app/main", (char *) "--merge", (char *) JOB, 0 };
	pid_t pid;
	if (!ok || posix_spawn (&pid, argv[0], 0, 0, argv, environ) != 0) ask ("Mail Merge", "Letters could not be started.", UM_OK);
	else status (one ? "Mail merge: the record sent to Letters" : "Mail merge: the records sent to Letters");
	focus_view ();
}

// A file dropped on the window.
static void on_drop ()
{
	char b[468]; long kind = 0;
	long n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = '\0';
	for (int i = 0; b[i]; i++) if (b[i] == '\n' || b[i] == '\r') { b[i] = '\0'; break; }
	struct stat st;
	if (!b[0] || stat (b, &st) != 0 || S_ISDIR (st.st_mode)) return;
	if (confirm_discard ()) load_path (b);
}

// ---- the commands --------------------------------------------------------------------------------------------------------
static bool to_design () { show_view (V_DESIGN); return g_view == V_DESIGN; }
static void command (int id)
{
	switch (id)
	{
	case M_NEW: cmd_new (); break;
	case M_OPEN: case M_IMPORT: cmd_open (); break;
	case M_SAVE: cmd_save (); break;
	case M_SAVEAS: cmd_save_as (); break;
	case M_EXPORT: cmd_export_csv (); break;
	case M_UNDO: cmd_undo (); break;
	case M_REDO: cmd_redo (); break;
	case M_CUT: key_to_focus (0x1B); break;
	case M_COPY: key_to_focus (0x06); break;
	case M_PASTE: key_to_focus (0x19); break;
	case M_FIND: cmd_find (); break;
	case M_CLEARFIND: cmd_clear_search (); break;
	case M_NEWREC: cmd_new_record (); break;
	case M_DUP: cmd_dup_record (); break;
	case M_DEL: cmd_del_record (); break;
	case M_FIRST: goto_pos (0); break;
	case M_PREV: goto_pos (cur_pos () - 1); break;
	case M_NEXT: goto_pos (cur_pos () + 1); break;
	case M_LAST: goto_pos (g_nord - 1); break;
	case M_GOTO: cmd_goto (); break;
	case M_MERGE: cmd_mail_merge (); break;
	case M_REVERT: if (g_view == V_FORM) fv_revert (); break;
	case M_VFORM: show_view (V_FORM); break;
	case M_VLIST: show_view (V_LIST); break;
	case M_VDESIGN: show_view (V_DESIGN); break;
	case M_FILEORDER: cmd_file_order (); break;
	case M_ADDF: if (to_design ()) dv_add (); break;
	case M_REMF: if (to_design ()) dv_remove (); break;
	case M_FUP: if (to_design ()) dv_move_field (-1); break;
	case M_FDOWN: if (to_design ()) dv_move_field (1); break;
	}
}

// A control's news.
static void control (long w, long code)
{
	if (code == UN_DROP) { on_drop (); return; }
	if (w == g_searchBox)
	{
		if (code == UN_CHANGE) on_search ();
		else if (code == UN_ACTIVATE) focus_view ();
		return;
	}
	if (w == g_seg) { if (code == UN_CHANGE) show_view ((int) ui_w_get_value (g_seg)); return; }
	if (code == UN_ACTIVATE)
	{
		static const struct { long *w; int id; } T[] = {
			{ &g_bNew, M_NEW }, { &g_bOpen, M_OPEN }, { &g_bSave, M_SAVE }, { &g_bUndo, M_UNDO }, { &g_bRedo, M_REDO },
			{ &g_bNewRec, M_NEWREC }, { &g_bDupRec, M_DUP }, { &g_bDelRec, M_DEL }, { &g_bFirst, M_FIRST }, { &g_bPrev, M_PREV },
			{ &g_bNext, M_NEXT }, { &g_bLast, M_LAST }, { &g_bAdd, M_NEWREC } };
		for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (w == *T[i].w) { command (T[i].id); return; }
		if (w == g_lPos) { command (M_GOTO); return; }
	}
	if (fv_event (w, code) || lv_event (w, code)) return;
	dv_event (w, code);
}
// A key no control took.
static void key (int hid, int mods)
{
	if (g_view == V_FORM && fv_key (hid, mods)) return;
	if (g_view == V_LIST && lv_key (hid)) return;
	if (g_view == V_DESIGN && dv_key (hid)) return;
	if (hid == KEY_ESC && ui_w_get_focus () == g_searchBox) { cmd_clear_search (); return; }
}

// ---- the window ---------------------------------------------------------------------------------------------------------
static void layout (int w, int h)
{
	int vy = TB_H, vh = h - TB_H - NAV_H;
	ui_w_move (g_tb, 0, 0, w, TB_H);
	int sw = imin (230, imax (120, w - 630));
	ui_w_move (g_searchBox, w - sw - 112, (TB_H - ctl_h ()) / 2, sw, ctl_h ());	// (left of the window's buttons)
	fv_move (0, vy, w, vh);
	lv_move (0, vy, w, vh);
	dv_move (0, vy, w, vh);
	int ny = h - NAV_H, cy = ny + (NAV_H - ctl_h ()) / 2;
	ui_w_move (g_nav, 0, ny, w, NAV_H);
	// (the navigator: toolbars keep their buttons where ui_toolbar_add put them)
	ui_w_move (g_lFile, w - 190, cy, 180, ctl_h ());
	ui_w_move (g_lInfo, 340, cy, imax (20, w - 340 - 200), ctl_h ());
}
static void on_resize (long w, long h)
{
	layout ((int) w, (int) h);
	if (g_view == V_LIST) lv_sync ();
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_view_dirty (fv);
	ui_w_draw (0);
	ui_update_all ();
}

static void build_window ()
{
	s_jet = (const unsigned *) ui_jetons ();
	TB_H = ctl_h () + 16; NAV_H = ctl_h () + 10;
	int w = (int) ui.w, h = (int) ui.h, vy = TB_H, vh = h - TB_H - NAV_H;
	ui_fill (0, 0, w, h, jet (J_FACE));

	// the toolbar (its buttons take no keyboard: the field being edited keeps it)
	g_tb = ui_toolbar (0, 0, w, TB_H);
	const struct { long *w; const char *glyph, *tip; long fl; } T[] = {
		{ &g_bNew, "\xE2\x98\x90", "New form (Ctrl+N)", 0 }, { &g_bOpen, "\xE2\x87\xA7", "Open... (Ctrl+O)", 0 }, { &g_bSave, "\xE2\x87\xA9", "Save (Ctrl+S)", 0 },
		{ &g_bUndo, "\xE2\x86\xB6", "Undo (Ctrl+Z)", UTB_SEP }, { &g_bRedo, "\xE2\x86\xB7", "Redo (Ctrl+Y)", 0 },
		{ &g_bNewRec, "+", "New record (Ctrl+R)", UTB_SEP }, { &g_bDupRec, "\xE2\x8A\x9E", "Duplicate the record (Ctrl+D)", 0 }, { &g_bDelRec, "\xE2\x9C\x95", "Delete the record...", 0 } };
	for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++)
	{
		*T[i].w = ui_toolbar_add (g_tb, (long) T[i].glyph, 0, T[i].fl);
		ui_w_tip (*T[i].w, (long) T[i].tip);
	}
	// (set on the bar, not just over it: a button of the bar that changes redraws the bar)
	g_seg = ui_segmented (8 + 8 * (ctl_h () + 2) + 2 * 12 + 14, (TB_H - ctl_h ()) / 2, 0, 0, (long) TR ("Form|List|Design"));
	ui_w_set_parent (g_seg, g_tb);
	ui_w_tip (g_seg, (long) "The view: Form (F5), List (F6), Design (F7)");
	g_searchBox = ui_textbox (0, 0, 200, ctl_h (), 0);
	ui_textbox_hint (g_searchBox, (long) TR ("Search (Ctrl+F)"));
	ui_w_set_parent (g_searchBox, g_tb);

	// the views
	fv_create (0, vy, w, vh);
	lv_create (0, vy, w, vh);
	dv_create (0, vy, w, vh);

	// the navigator
	int ny = h - NAV_H;
	g_nav = ui_toolbar (0, ny, w, NAV_H);
	g_bFirst = ui_toolbar_add (g_nav, (long) "\xE2\x87\xA4", 0, 0);
	g_bPrev = ui_toolbar_add (g_nav, (long) "\xE2\x86\x90", 0, 0);
	int x = 8 + 2 * (ctl_h () + 2) + 2, cy = ny + (NAV_H - ctl_h ()) / 2;
	g_lPos = ui_pushbutton (x, cy, 132, ctl_h (), (long) "", UB_GHOST);
	ui_w_set_parent (g_lPos, g_nav);
	ui_w_tip (g_lPos, (long) "Go to Record (Ctrl+G)");
	g_navR = ui_toolbar (x + 134, ny, 3 * (ctl_h () + 2) + 12, NAV_H);
	ui_w_set_parent (g_navR, g_nav);
	g_bNext = ui_toolbar_add (g_navR, (long) "\xE2\x86\x92", 0, 0);
	g_bLast = ui_toolbar_add (g_navR, (long) "\xE2\x87\xA5", 0, 0);
	g_bAdd = ui_toolbar_add (g_navR, (long) "+", 0, 0);
	ui_w_tip (g_bAdd, (long) "New record (Ctrl+R)");
	g_lInfo = ui_label (340, cy, 300, ctl_h (), (long) "", ULB_DIM);
	g_lFile = ui_label (w - 190, cy, 180, ctl_h (), (long) "", ULB_DIM | ULB_RIGHT);
	ui_w_set_parent (g_lInfo, g_nav);
	ui_w_set_parent (g_lFile, g_nav);

	layout (w, h);
	show_pages (g_view);
	ui_menu ((long) MENUS);
	refresh ();
	ui_view_dirty (fv);
	ui_w_draw (0);					// (a question may come before the first file: over a whole window)
	ui_update_all ();
}

} // namespace cf

using namespace cf;

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{
		char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/cardfile.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang);
		g_wYes = strdup (TL (g_wYes)); g_wNo = strdup (TL (g_wNo)); g_wUntitled = strdup (TL (g_wUntitled));
		g_wName = strdup (TL (g_wName)); g_wNotes = strdup (TL (g_wNotes)); g_wField = strdup (TL (g_wField));
		for (int i = 0; i < FT_COUNT; i++) TYPE_NAME[i] = TR (TYPE_NAME[i]);			// (UTF-8 : la liste des champs)
	}
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	// (the frame takes 32; the dock floats over the desktop's foot, reserving nothing: the navigator stays above it)
	int ww = imin (W, (int) scr[0] - 8), wh = imin (H, (int) (scr[1] - scr[3] - scr[2]) - 32 - DOCK_H);
	if (ui_window_ex (0, scr[3], ww, wh, (long) "Cardfile", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (620, 420);
	ui_on_resize ((long) on_resize, 0);
	mkdir ("SD0:/home", 0755);
	mkdir (HOME_DIR, 0755);
	mkdir (DOCS, 0755);
	doc_init (g_doc);
	doc_init (s_tmp);
	build_window ();

	// A file named on the command line; else the form kept at the last close (asked); else the form
	// opened last time; else a new form.
	bool opened = argc > 1 && argv[1][0] && load_path (argv[1]);
	if (!opened)
	{
		doc_new (g_doc);
		doc_fresh ("", true, V_DESIGN);
		char *b; int n;
		if (read_all (RECOVER, &b, &n))
		{
			if (n > 0 && ask ("Cardfile", "Cardfile was closed with unsaved changes. Open the form it kept?", UM_YESNO) == 1 && doc_read (s_tmp, b, n, 0))
			{
				doc_clear (g_doc); g_doc = s_tmp; doc_init (s_tmp);
				char *p; int pn; char was[200] = "";
				if (read_all (RECOVER_NAME, &p, &pn)) { scpy (was, p, sizeof was); delete [] p; }
				doc_fresh (was, false, V_FORM);
				opened = true;
			}
			delete [] b;
			unlink (RECOVER); unlink (RECOVER_NAME);
		}
		if (!opened && read_all (LAST, &b, &n))
		{
			char last[200]; trim_copy (last, b, sizeof last);
			delete [] b;
			if (last[0]) opened = load_path (last, true);
		}
		if (!opened) dv_focus_label ();
	}
	refresh ();
	ui_view_dirty (fv);
	ui_w_draw (0);
	ui_update_all ();

	unsigned tick = 0; long lastFocus = 0;
	for (;;)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
				else key (hid, mods);
			}
		}
		else ui_w_idle ();
		long f = ui_w_get_focus ();
		if (f != lastFocus)					// (the keyboard moved: the field's name lit, the field shown)
		{
			lastFocus = f;
			if (g_view == V_FORM) { fv_ensure_visible (fv_focused ()); ui_view_dirty (fv); }
		}
		unsigned now = ticks ();
		if (now - tick >= 20)					// (the record's edits, a status word's end)
		{
			tick = now;
			if (g_msg[0] && now - g_msgT >= 400) g_msg[0] = '\0';
			refresh ();
		}
		ui_view_flush ();
	}

	// Closed with unsaved changes: the document kept for the next start.
	if (g_view == V_FORM) fv_commit (true);
	if (modified ())
	{
		Out o; doc_write (g_doc, o);
		save_file (RECOVER, o.b, o.n);
		save_file (RECOVER_NAME, g_path, slen (g_path));
	}
	return 0;
}
