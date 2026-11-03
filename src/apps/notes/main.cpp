//
// notes/main.cpp -- Notes of Onyx (user/Apps/notes) on ARMPI's UIKit: quick notes kept on the card, one plain .txt a note in
// SD0:/Notes. The window: the buttons on top (Nouvelle note, Supprimer, Epingler, the six colours), the list of the notes
// on the left (newest first), the selected note's text on the right on its paper, a status line. No Save command: a note
// is written about a second after the typing stops, when another note is chosen and when the window closes; an empty note
// is not kept. Supprimer sends a note to SD0:/Notes/Corbeille; a pinned note is shown on the desktop by the applet stickies
// (src/sys/stickies, the "stickies" service: told to re-read after each change). Files (.txt, .md) dropped on the window
// become new notes; dropped text goes in at the caret. `notes <path>` opens on that note -- in the Notes already running, if
// any: an app has no service of its own here, the path goes through NOTES_OPEN and its window is raised.
// The model (files, notes.ini, names, dates, colours, config.ini) is notesmodel.cpp, Onyx's.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <uikit.h>
#include "notesmodel.h"
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-en.txt : le source est en francais)

extern char **environ;

enum { TB_H = 44, ST_H = 24, INFO_H = 34, MIN_W = 620, MIN_H = 320, HEAD = 44, ROW = 56, SBW = 12, SPLITW = 6 };
enum { AUTOSAVE = 1000, MSG_TIME = 4000 };			// ms
enum { STK_RELOAD = 2, STK_QUIT = 3 };				// the "stickies" service (Onyx's stickies_proto.h)

static struct ui_vars ui;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
#define C_BG	jet (J_FACE)
#define C_FIELD	jet (J_SURFACE)
#define C_TEXT	jet (J_TEXTE1)
#define C_ACCENT jet (J_ACCENT)
static unsigned mix (unsigned a, unsigned b, int n) { return (unsigned) ui_mix (a, b, n); }

static inline long sys_call (long n, long a = 0, long b = 0, long c = 0)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory");
	return x0;
}
enum { SYS_GETPID_ = 0x11, SYS_PROCLIST_ = 0x15, SYS_TIME_MS_ = 0x19, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32 };
static unsigned ticks_ms () { return (unsigned) sys_call (SYS_TIME_MS_); }

// text in a view: the baseline from a box's height
static const int PX = 13;
static long font_of (int bold) { return bold ? (long) ui.font_b : (long) ui.font; }
static int  text_w (const char *s, int bold = 0) { return (int) ui_text_width ((long) s, PX, font_of (bold)); }
static void text_l (int x, int y, int h, const char *s, unsigned c, int bold = 0) { ui_text ((long) s, x, y + h / 2 + PX * 37 / 100, c, PX, font_of (bold), 0); }
static void text_fit (int x, int y, int w, int h, const char *s, unsigned c, int bold = 0) { if (w > 8) ui_text_fit ((long) s, x, y + h / 2 + PX * 37 / 100, c, PX, font_of (bold), w); }
static void rbox (int x, int y, int w, int h, int r, unsigned c) { ui_rbox (x, y, w, h, r, c, c); }

// ---- the state -------------------------------------------------------------------------------------------
static Notes    g_notes;			// the list (~140 KB: static)
static NotesCfg g_cfg;
static int      g_sel = -1;			// the note in the editor (its row), -1 none
static bool     g_dirty;			// the editor's text differs from what is on the card
static bool     g_bad;				// the note could not be read (larger than 64 KB, gone): shown
						// empty, read-only, never written nor thrown away
static unsigned g_edHash;			// the editor's text as last seen
static unsigned g_lastEdit;			// when the text changed last
static bool     g_failed;			// the last save failed (retried at the next pause in typing)
static unsigned g_failedAt;			// ... the g_lastEdit it failed at
static char     g_buf[NOTE_MAX_BYTES + 1];	// a note read
static char     g_msg[160];			// the status line's right part
static unsigned g_msgUntil;			// when it goes away, 0: it stays
static int      g_scroll;			// the list, px scrolled
static long long g_now;				// YYYYMMDDHHMMSS: the list's dates are told against it

static long g_bNew, g_bDel, g_bPin, g_vcol, g_vlist, g_sbar, g_split, g_vinfo, g_ed, g_lcount, g_lmsg;
static int  g_ww, g_wh;

static NoteInfo *cur () { return g_sel >= 0 && g_sel < g_notes.count ? &g_notes.n[g_sel] : 0; }
static void update_chrome ();
static void build_menu ();
static const char *ed_text () { return (const char *) ui_w_get_text (g_ed); }
static int  ed_len () { return (int) ui_w_text_len (g_ed); }
static unsigned hash_of (const char *s, int n) { unsigned h = 2166136261u; for (int i = 0; i < n; i++) { h ^= (unsigned char) s[i]; h *= 16777619u; } return h; }

static void set_msg (const char *s, unsigned time)
{
	snprintf (g_msg, sizeof g_msg, "%s", s ? s : "");
	g_msgUntil = time ? ticks_ms () + time : 0;
	ui_w_set_text (g_lmsg, (long) g_msg);
}
static void spawn (const char *path, const char *arg)
{
	char *argv[3] = { (char *) path, (char *) arg, 0 };
	pid_t pid;
	posix_spawn (&pid, path, 0, 0, argv, environ);
}

// ---- Stickies ---------------------------------------------------------------------------------------------
static void stickies_call (int op)
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) "stickies", 0);
	if (ch < 0) return;
	static long m[64]; m[0] = op;
	sys_call (SYS_SVC_CALL_, ch, (long) m);
}
static void stickies_reload () { stickies_call (STK_RELOAD); }
static bool stickies_runs () { return sys_call (SYS_SVC_OPEN_, (long) "stickies", 0) >= 0; }
// Show / Hide Stickies on the Desktop: the setting kept (the applet reads it at its start, and every minute)
static void stickies_set_shown (bool on)
{
	g_cfg.stickies = on ? 1 : 0;
	notes_cfg_save (g_cfg);
	if (!on) { stickies_call (STK_QUIT); set_msg (TR ("Notes du bureau masqu\xC3\xA9" "es."), MSG_TIME); return; }
	if (!stickies_runs ()) spawn ("SD0:/sys/stickies", "");
	set_msg (TR ("Notes du bureau montr\xC3\xA9" "es."), MSG_TIME);
}

// ---- dates (the info line over the text) -----------------------------------------------------------------
static long long day_number (int y, int m, int d)			// days since 1970-01-01
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
	long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
// "Aujourd'hui, 09:15", "Hier, 18:30", "vendredi 25 septembre, 20:02", "15 septembre, 10:15", "2 mai 2025, 08:00"
static void long_date (long long when, long long now, char *out, int cap)
{
	static const char *const MON[12] = { "janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet", "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre" };
	static const char *const DAY[7] = { "jeudi", "vendredi", "samedi", "dimanche", "lundi", "mardi", "mercredi" };
	out[0] = 0;
	int y = (int) (when / 10000000000LL), mo = (int) (when / 100000000 % 100), d = (int) (when / 1000000 % 100);
	int h = (int) (when / 10000 % 100), mi = (int) (when / 100 % 100);
	if (when <= 0 || mo < 1 || mo > 12 || d < 1 || d > 31) return;
	int ny = (int) (now / 10000000000LL), nmo = (int) (now / 100000000 % 100), nd = (int) (now / 1000000 % 100);
	long long day = day_number (y, mo, d), diff = nmo >= 1 && nmo <= 12 ? day_number (ny, nmo, nd) - day : 1000;
	const char *M = TR (MON[mo - 1]);
	if (diff == 0) snprintf (out, (size_t) cap, TR ("Aujourd'hui, %02d:%02d"), h, mi);
	else if (diff == 1) snprintf (out, (size_t) cap, TR ("Hier, %02d:%02d"), h, mi);
	else if (diff >= 2 && diff <= 6) snprintf (out, (size_t) cap, "%s %d %s, %02d:%02d", TR (DAY[((day % 7) + 7) % 7]), d, M, h, mi);
	else if (y == ny) snprintf (out, (size_t) cap, "%d %s, %02d:%02d", d, M, h, mi);
	else snprintf (out, (size_t) cap, "%d %s %d, %02d:%02d", d, M, y, h, mi);
}

// ---- the list ----------------------------------------------------------------------------------------------
static void dot (int x, int y, int d, int c) { unsigned k = notes_colour_dot (c); rbox (x, y, d, d, d / 2, k); }
static int  content_h () { return g_notes.count * ROW + 8; }
static int  view_h () { return g_wh - TB_H - ST_H - HEAD; }
static void clamp_scroll ()
{
	int most = content_h () - view_h ();
	if (g_scroll > most) g_scroll = most;
	if (g_scroll < 0) g_scroll = 0;
	ui_w_set_range (g_sbar, 0, content_h (), view_h (), ROW);
	ui_w_set_value (g_sbar, g_scroll);
}
static void show_sel ()
{
	if (g_sel >= 0)
	{
		int y = 4 + g_sel * ROW;
		if (y < g_scroll) g_scroll = y - 4;
		else if (y + ROW > g_scroll + view_h ()) g_scroll = y + ROW - view_h ();
	}
	clamp_scroll ();
	ui_view_dirty (g_vlist);
}
static long list_paint (long, long wl, long hl)
{
	int width = (int) wl, height = (int) hl;
	unsigned dim = mix (C_FIELD, C_TEXT, 140), faint = mix (C_FIELD, C_TEXT, 30);
	ui_fill (0, 0, width, height, C_FIELD);
	int n = g_notes.count;
	bool focus = ui_w_get_focus () == g_vlist;
	ui_clip (0, HEAD, width, height - HEAD);
	for (int r = 0; r < n; r++)
	{
		int y = HEAD + 4 + r * ROW - g_scroll;
		if (y + ROW <= HEAD) continue;
		if (y >= height) break;
		const NoteInfo &e = g_notes.n[r];
		bool isSel = r == g_sel;
		if (isSel) rbox (6, y, width - 12, ROW - 4, 7, mix (C_FIELD, C_ACCENT, focus ? 90 : 60));
		else if (r + 1 < n && r + 1 != g_sel) ui_fill (34, y + ROW - 3, width - 46, 1, faint);
		dot (16, y + 11, 10, e.colour);
		char date[24] = "";
		if (e.saved) notes_date_label (e.modified, g_now, date, sizeof date);
		int dw = date[0] ? text_w (date) : 0;
		if (e.title[0]) text_fit (34, y + 5, width - 34 - dw - 22, 18, e.title, C_TEXT, 1);
		else ui_text ((long) TR ("Nouvelle note"), 34, y + 19, mix (C_FIELD, C_TEXT, 170), PX, ui_font_get (UFN_BOLDITALIC), 0);
		if (dw) text_l (width - dw - 14, y + 5, 18, date, isSel ? mix (C_FIELD, C_TEXT, 190) : dim);
		const char *pv = e.preview[0] ? e.preview : !e.saved && !e.title[0] ? TR ("Maintenant") : "";
		if (*pv) text_fit (34, y + 26, width - 34 - 14 - (e.pinned ? 22 : 0), 18, pv, dim);
		if (e.pinned) rbox (width - 26, y + 29, 10, 10, 5, isSel ? C_ACCENT : mix (C_FIELD, C_TEXT, 160));
	}
	ui_clip (0, 0, 0, 0);
	text_l (14, 0, HEAD, TR ("Notes"), C_TEXT, 1);
	char c[16]; snprintf (c, sizeof c, "%d", n);
	int cw = text_w (c);
	rbox (width - cw - 30, 13, cw + 16, 19, 9, mix (C_FIELD, C_TEXT, 36));
	text_l (width - cw - 22, 13, 19, c, C_TEXT);
	ui_fill (0, HEAD - 1, width, 1, faint);
	return 0;
}
static void select_row (int r);
static void on_delete ();
static long list_mouse (long, long kind, long, long y, long)
{
	if (kind != 1 || y < HEAD) return 0;
	int r = ((int) y - HEAD - 4 + g_scroll) / ROW;
	if (r >= 0 && r < g_notes.count) select_row (r);
	return 0;
}
static long list_key (long, long hid, long, long)
{
	int n = g_notes.count, page = view_h () / ROW > 1 ? view_h () / ROW - 1 : 1, to = g_sel;
	switch (hid)
	{
	case 0x52: to = g_sel - 1; break;		// up
	case 0x51: to = g_sel + 1; break;		// down
	case 0x4B: to = g_sel - page; break;
	case 0x4E: to = g_sel + page; break;
	case 0x4A: to = 0; break;
	case 0x4D: to = n - 1; break;
	case 0x28: case 0x2B: ui_w_focus (g_ed); return 0;	// Enter, Tab
	case 0x4C: on_delete (); return 0;
	default: return -1;
	}
	if (to < 0) to = 0;
	if (to > n - 1) to = n - 1;
	if (to != g_sel && to >= 0) select_row (to);
	return 0;
}
static long list_wheel (long, long n) { g_scroll -= (int) n * ROW; clamp_scroll (); ui_view_dirty (g_vlist); return 0; }

// The line over the text: when the note was changed, its colour, TR ("Sur le bureau") when pinned.
static long info_paint (long, long wl, long hl)
{
	int width = (int) wl, height = (int) hl;
	ui_fill (0, 0, width, height, C_FIELD);
	unsigned dim = mix (C_FIELD, C_TEXT, 140);
	NoteInfo *e = cur ();
	if (!e) return 0;
	if (!e->saved && !ed_len () && !g_bad) { text_l (18, 0, height, TR ("Nouvelle note \xC2\xB7 \xC3\xA9" "crivez : elle se garde toute seule"), dim); return 0; }
	ui_fill (12, height - 1, width - 24, 1, mix (C_FIELD, C_TEXT, 30));
	if (g_bad) { text_l (18, 0, height, TR ("Plus de 64 Ko : \xC3\xA0 ouvrir dans l'\xC3\xA9" "diteur"), dim); return 0; }
	char d[64], s[120];
	if (e->saved) long_date (e->modified, notes_now (), d, sizeof d); else snprintf (d, sizeof d, "%s", TR ("Nouvelle note"));
	snprintf (s, sizeof s, "%s  \xC2\xB7  %s", d[0] ? d : TR ("Date inconnue"), notes_colour_label (e->colour));
	text_l (18, 0, height, s, dim);
	if (e->pinned) text_l (18 + text_w (s) + 14, 0, height, TR ("Sur le bureau"), C_ACCENT);
	return 0;
}

// The six colours, on the toolbar: a dot each, a ring on the note's
static long col_paint (long, long wl, long hl)
{
	ui_fill (0, 0, wl, hl, C_BG);
	NoteInfo *e = cur ();
	int c = e ? e->colour : NC_YELLOW;
	for (int k = 0; k < NC_COUNT; k++)
	{
		int x = k * 28 + 4, y = ((int) hl - 20) / 2;
		if (k == c) rbox (x - 3, y - 3, 26, 26, 13, C_ACCENT);
		if (k == c) rbox (x - 1, y - 1, 22, 22, 11, C_BG);
		dot (x, y, 20, k);
	}
	return 0;
}
static void set_colour (int c);
static long col_mouse (long, long kind, long x, long, long) { if (kind == 1) set_colour ((int) x / 28); return 0; }

static void status_counts ()
{
	int saved = 0, pinned = 0;
	for (int i = 0; i < g_notes.count; i++) if (g_notes.n[i].saved) { saved++; if (g_notes.n[i].pinned) pinned++; }
	char l[96];
	const char *notes = TR (saved > 1 ? "notes" : "note");
	if (g_notes.total > g_notes.count) snprintf (l, sizeof l, TR ("Les %d notes les plus r\xC3\xA9" "centes"), g_notes.count);
	else if (!saved) snprintf (l, sizeof l, "%s", TR ("Aucune note"));
	else if (!pinned) snprintf (l, sizeof l, "%d %s", saved, notes);
	else snprintf (l, sizeof l, TR ("%d %s \xC2\xB7 %d sur le bureau"), saved, notes, pinned);
	ui_w_set_text (g_lcount, (long) l);
}

// ---- the notes: load, save, leave ------------------------------------------------------------------------
static void text_changed ()
{
	int len = ed_len ();
	const char *t = ed_text ();
	unsigned h = hash_of (t, len);
	if (h == g_edHash) return;
	g_edHash = h;
	NoteInfo *e = cur ();
	if (!e || g_bad) return;
	g_dirty = true;
	g_lastEdit = ticks_ms ();
	int n = len < NOTE_HEAD ? len : NOTE_HEAD;
	notes_title (t, n, e->title, sizeof e->title);
	notes_preview (t, n, e->preview, sizeof e->preview);
	if (!strcmp (g_msg, TR ("Enregistr\xC3\xA9"))) set_msg ("", 0);
	ui_view_dirty (g_vlist); ui_view_dirty (g_vinfo);
	ui_w_enable (g_bDel, e->saved || len);
}
// The current note written (when changed and not empty). -> false the write failed (the status says so)
static bool save_current ()
{
	NoteInfo *e = cur ();
	if (!e || g_bad || !g_dirty || ed_len () == 0) return true;
	char file[NOTE_FILE]; snprintf (file, sizeof file, "%s", e->file);
	int r = notes_write (g_notes, file, ed_text (), ed_len (), notes_now ());
	if (r != 0)
	{
		g_failed = true; g_failedAt = g_lastEdit;
		set_msg (r == -2 ? TR ("Non enregistr\xC3\xA9 : la note d\xC3\xA9passe 64 Ko") : TR ("Non enregistr\xC3\xA9 : carte pleine ou prot\xC3\xA9g\xC3\xA9" "e"), 0);
		return false;
	}
	notes_save_ini (g_notes);
	g_dirty = false; g_failed = false;
	notes_sort (g_notes);
	g_sel = notes_find (g_notes, file);
	g_now = notes_now ();
	set_msg (TR ("Enregistr\xC3\xA9"), 0);
	stickies_reload ();
	update_chrome ();
	show_sel ();
	return true;
}
// Leaving the current note: saved; empty, it is not kept -- never written: forgotten; emptied by the user: to the Corbeille
static void leave_current ()
{
	NoteInfo *e = cur ();
	if (!e) return;
	save_current ();
	if (!g_bad && ed_len () == 0)
	{
		if (!g_notes.n[g_sel].saved) notes_forget (g_notes, g_sel);
		else if (notes_trash (g_notes, g_notes.n[g_sel].file)) stickies_reload ();
	}
	g_sel = -1; g_dirty = false; g_failed = false;
}
// Row i into the editor.
static void load (int i)
{
	if (i < 0 || i >= g_notes.count) i = g_notes.count ? 0 : -1;
	g_sel = i; g_dirty = false; g_failed = false; g_bad = false;
	g_buf[0] = 0;
	if (i >= 0 && g_notes.n[i].saved)
	{
		int n = notes_read (g_notes.n[i].file, g_buf, sizeof g_buf);
		if (n < 0)
		{
			g_buf[0] = 0; g_bad = true;
			set_msg (n == -2 ? TR ("Cette note d\xC3\xA9passe 64 Ko") : TR ("Cette note ne peut pas \xC3\xAAtre lue"), MSG_TIME);
		}
	}
	ui_w_set_text (g_ed, (long) g_buf);
	ui_w_style (g_ed, UTA_VSCROLL | (g_bad || i < 0 ? UTF_READONLY : 0));
	g_edHash = hash_of (ed_text (), ed_len ());
	if (!strcmp (g_msg, TR ("Enregistr\xC3\xA9"))) set_msg ("", 0);
	g_now = notes_now ();
	show_sel ();
	update_chrome ();
}
static void select_file (const char *file)
{
	char f[NOTE_FILE]; snprintf (f, sizeof f, "%s", file ? file : "");
	NoteInfo *e = cur ();
	if (e && !strcmp (e->file, f)) return;
	leave_current ();
	load (f[0] ? notes_find (g_notes, f) : 0);
	if (g_sel < 0 && notes_add_new (g_notes, notes_now ()) == 0) load (0);
}
static void select_row (int r) { if (r >= 0 && r < g_notes.count && r != g_sel) select_file (g_notes.n[r].file); }

// ---- the commands ----------------------------------------------------------------------------------------
static void on_new ()
{
	NoteInfo *e = cur ();
	if (e && !e->saved && !ed_len ()) { ui_w_focus (g_ed); return; }	// (already on an empty new note)
	leave_current ();
	if (notes_add_new (g_notes, notes_now ()) != 0) { set_msg (TR ("Trop de notes : supprimez-en d'abord"), MSG_TIME); load (0); return; }
	load (0);
	ui_w_focus (g_ed);
}
static void on_delete ()
{
	NoteInfo *e = cur ();
	if (!e || (!e->saved && !ed_len () && !g_bad)) return;		// (a new empty note: nothing to throw away)
	if (!g_bad && g_dirty && !save_current ()) return;		// (the Corbeille gets the latest text)
	e = cur ();
	if (!e->saved) return;
	char file[NOTE_FILE]; snprintf (file, sizeof file, "%s", e->file);
	int at = g_sel;
	if (!notes_trash (g_notes, file)) { set_msg (TR ("La note ne peut pas \xC3\xAAtre mise \xC3\xA0 la corbeille"), MSG_TIME); return; }
	g_sel = -1; g_dirty = false;
	set_msg (TR ("Note mise \xC3\xA0 la corbeille"), MSG_TIME);
	stickies_reload ();
	if (g_notes.count == 0) { notes_add_new (g_notes, notes_now ()); load (0); ui_w_focus (g_ed); return; }
	load (at < g_notes.count ? at : g_notes.count - 1);
}
static void on_pin ()
{
	NoteInfo *e = cur ();
	if (!e) return;
	e->pinned = !e->pinned;
	if (e->saved) { notes_save_ini (g_notes); stickies_reload (); }
	if (e->pinned && g_cfg.stickies && !stickies_runs ()) spawn ("SD0:/sys/stickies", "");
	update_chrome ();
}
static void set_colour (int c)
{
	NoteInfo *e = cur ();
	if (!e || c < 0 || c >= NC_COUNT) return;
	if (e->colour != c)
	{
		e->colour = c;
		if (e->saved) { notes_save_ini (g_notes); stickies_reload (); }
	}
	update_chrome ();
}
static void on_open_editor ()
{
	NoteInfo *e = cur ();
	if (!e) return;
	save_current ();
	e = cur ();
	if (!e || !e->saved) return;
	char p[128]; snprintf (p, sizeof p, "%s/%s", NOTES_DIR, e->file);
	spawn ("SD0:/apps/editor.app/main", p);
}

// ---- the menus ---------------------------------------------------------------------------------------------
enum { M_NEW = 1, M_EDITOR, M_DELETE, M_QUIT, M_CUT, M_COPY, M_PASTE, M_ALL, M_COPYNOTE, M_PIN, M_STICKIES, M_COL0 = 20 };
static void build_menu ()
{
	static char tt[2][1024];			// (deux tampons : UIKit retient la traduction d'un menu par son adresse)
	static int w;
	char *t = tt[w ^= 1];
	const size_t cap = sizeof tt[0];
	NoteInfo *e = cur ();
	int n = snprintf (t, cap,
		"MFichier\n" "I%d\tNouvelle note\tCtrl+N\n" "-\n" "I%d\tOuvrir dans l'\xC3\xA9" "diteur\tCtrl+E\n" "-\n" "I%d\tSupprimer la note\tCtrl+D\n" "-\n" "I%d\tQuitter\n"
		"M\xC3\x89" "dition\n" "I%d\tCouper\tCtrl+X\n" "I%d\tCopier\tCtrl+C\n" "I%d\tColler\tCtrl+V\n" "-\n" "I%d\tTout s\xC3\xA9lectionner\tCtrl+A\n" "I%d\tCopier la note\n"
		"MNote\n" "I%d\t%s\tCtrl+P\n" "-\n",
		M_NEW, M_EDITOR, M_DELETE, M_QUIT, M_CUT, M_COPY, M_PASTE, M_ALL, M_COPYNOTE, M_PIN, e && e->pinned ? "D\xC3\xA9tacher du bureau" : "\xC3\x89pingler au bureau");
	for (int c = 0; c < NC_COUNT; c++) n += snprintf (t + n, cap - (size_t) n, "I%d\t%s\n", M_COL0 + c, notes_colour_label (c));
	snprintf (t + n, cap - (size_t) n, "MPr\xC3\xA9sentation\n" "I%d\t%s\n", M_STICKIES, g_cfg.stickies ? "Masquer les notes du bureau" : "Montrer les notes du bureau");
	ui_menu ((long) t);
}
static bool command (int id)
{
	switch (id)
	{
	case M_NEW: on_new (); break;
	case M_EDITOR: on_open_editor (); break;
	case M_DELETE: on_delete (); break;
	case M_QUIT: return false;
	case M_CUT: ui_textarea_edit (g_ed, UTE_CUT); text_changed (); ui_w_focus (g_ed); break;
	case M_COPY: ui_textarea_edit (g_ed, UTE_COPY); ui_w_focus (g_ed); break;
	case M_PASTE: ui_textarea_edit (g_ed, UTE_PASTE); text_changed (); ui_w_focus (g_ed); break;
	case M_ALL: ui_textarea_edit (g_ed, UTE_ALL); ui_w_focus (g_ed); break;
	case M_COPYNOTE: if (ed_len ()) ui_clip_set ((long) ed_text (), ed_len ()); break;
	case M_PIN: on_pin (); break;
	case M_STICKIES: stickies_set_shown (!g_cfg.stickies); build_menu (); break;
	default: if (id >= M_COL0 && id < M_COL0 + NC_COUNT) set_colour (id - M_COL0); break;
	}
	return true;
}
// Everything that shows the current note's state
static void update_chrome ()
{
	NoteInfo *e = cur ();
	int c = e ? e->colour : NC_YELLOW;
	ui_w_set_text (g_bPin, (long) (e && e->pinned ? TR ("D\xC3\xA9tacher") : TR ("\xC3\x89pingler")));
	ui_w_enable (g_bPin, e != 0);
	ui_w_enable (g_bDel, e && (e->saved || ed_len () || g_bad));
	ui_textarea_paper (g_ed, mix (C_FIELD, notes_colour_paper (c), 150), NOTE_INK);
	ui_w_draw (g_ed);
	ui_view_dirty (g_vcol); ui_view_dirty (g_vlist); ui_view_dirty (g_vinfo);
	status_counts ();
	build_menu ();
}

// ---- dropped files: new notes ----------------------------------------------------------------------------
static bool text_name (const char *p)
{
	int n = (int) strlen (p);
	return (n > 4 && !strcasecmp (p + n - 4, ".txt")) || (n > 3 && !strcasecmp (p + n - 3, ".md"));
}
static void import_files (const char *data)
{
	char last[NOTE_FILE] = "", prev[NOTE_FILE] = "";
	int done = 0, refused = 0;
	if (NoteInfo *e = cur ()) snprintf (prev, sizeof prev, "%s", e->file);
	leave_current ();
	for (const char *p = data; *p; )
	{
		char path[256]; int n = 0;
		while (p[n] && p[n] != '\n') n++;
		snprintf (path, sizeof path, "%.*s", n < (int) sizeof path - 1 ? n : (int) sizeof path - 1, p);
		p += n; if (*p == '\n') p++;
		if (!path[0]) continue;
		int len = text_name (path) ? notes_read (path, g_buf, sizeof g_buf) : -1;
		if (len < 0) { refused++; continue; }
		if (notes_add_new (g_notes, notes_now ()) != 0) break;
		char file[NOTE_FILE]; snprintf (file, sizeof file, "%s", g_notes.n[0].file);
		if (notes_write (g_notes, file, g_buf, len, notes_now ()) != 0)
		{ notes_forget (g_notes, 0); set_msg (TR ("Non enregistr\xC3\xA9 : carte pleine ou prot\xC3\xA9g\xC3\xA9" "e"), MSG_TIME); break; }
		snprintf (last, sizeof last, "%s", file); done++;
	}
	if (done) { notes_save_ini (g_notes); stickies_reload (); }
	load (notes_find (g_notes, last[0] ? last : prev));
	if (g_sel < 0 && notes_add_new (g_notes, notes_now ()) == 0) load (0);
	if (refused) ui_message ((long) TR ("Importer une note"), (long) TR ("Seuls les fichiers texte (.txt, .md) de 64 Ko au plus deviennent des notes."), UM_OK);
}
static void on_drop ()
{
	static char d[8192];
	long kind = 0, n = uikit_drop_get (d, sizeof d - 1, &kind);
	if (n <= 0) return;
	d[n] = 0;
	if (kind == UDT_FILES) import_files (d);
	else if (ui_w_get_focus () == g_ed || !g_bad) { ui_textarea_insert (g_ed, (long) d); text_changed (); ui_w_focus (g_ed); }
}

// ---- the window ------------------------------------------------------------------------------------------
static void lay_out (long w, long h)
{
	g_ww = (int) w; g_wh = (int) h;
	int by = (TB_H - 30) / 2, x = 10;
	ui_w_move (g_bNew, x, by, 130, 30); x += 138;
	ui_w_move (g_bDel, x, by, 96, 30); x += 104;
	ui_w_move (g_bPin, x, by, 90, 30); x += 98;
	ui_w_move (g_vcol, x, by - 3, NC_COUNT * 28 + 6, 36);
	int split = g_cfg.split < 180 ? 180 : g_cfg.split > w - 260 ? (int) w - 260 : g_cfg.split;
	g_cfg.split = split;
	int bh = (int) h - TB_H - ST_H;
	ui_w_move (g_vlist, 0, TB_H, split - SBW, bh);
	ui_w_move (g_sbar, split - SBW, TB_H + HEAD, SBW, bh - HEAD);
	ui_w_move (g_split, split, TB_H, SPLITW, bh);
	ui_w_set_value (g_split, split);
	int rx = split + SPLITW, rw = (int) w - rx;
	ui_w_move (g_vinfo, rx, TB_H, rw, INFO_H);
	ui_w_move (g_ed, rx + 8, TB_H + INFO_H + 6, rw - 16, bh - INFO_H - 14);
	ui_w_move (g_lcount, 12, (int) h - ST_H, split, ST_H);
	ui_w_move (g_lmsg, split, (int) h - ST_H, (int) w - split - 12, ST_H);
	clamp_scroll ();
	ui_view_dirty (g_vlist); ui_view_dirty (g_vinfo); ui_view_dirty (g_vcol);
}
static void repaint (long w, long h) { ui_fill (0, 0, w, h, C_BG); lay_out (w, h); ui_w_draw (0); ui_update_all (); }
static void on_resize (long w, long h) { repaint (w, h); }

static void control (long w, long code)
{
	if (code == UN_ACTIVATE && w == g_bNew) on_new ();
	else if (code == UN_ACTIVATE && w == g_bDel) on_delete ();
	else if (code == UN_ACTIVATE && w == g_bPin) on_pin ();
	else if (w == g_ed && code == UN_CHANGE) text_changed ();
	else if (w == g_sbar && code == UN_CHANGE) { g_scroll = (int) ui_w_get_value (g_sbar); ui_view_dirty (g_vlist); }
	else if (w == g_split && code == UN_CHANGE) { g_cfg.split = (int) ui_w_get_value (g_split); repaint (g_ww, g_wh); }
}

// Another Notes running (one at a time): it is given the note and raised. -> true: this one ends
static bool other_notes (const char *arg)
{
	static unsigned char p[64 * 64];
	long n = sys_call (SYS_PROCLIST_, (long) p, 64), me = sys_call (SYS_GETPID_);
	long other = 0;
	for (long i = 0; i < n; i++)
	{
		unsigned char *e = p + i * 64;
		if (!strcmp ((char *) e + 24, "notes") && *(int *) e != me) other = *(int *) e;
	}
	if (!other) return false;
	int f = open (NOTES_OPEN, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f >= 0) { if (arg) write (f, arg, strlen (arg)); close (f); }
	static unsigned char wl[WL_PER_MSG * WL_SIZE];
	for (long first = 0; ; first += WL_PER_MSG)
	{
		long t = ui_winlist (first, (long) wl);
		if (t <= first) break;
		for (long i = 0; i < WL_PER_MSG && first + i < t; i++)
		{
			unsigned char *e = wl + i * WL_SIZE;
			if (*(int *) (e + WL_PID) == other) { ui_raise (*(long *) (e + WL_ID)); return true; }
		}
	}
	return true;
}
// (each second) A note handed by a second Notes, or by the applet
static void poll_open ()
{
	int f = open (NOTES_OPEN, O_RDONLY);
	if (f < 0) return;
	char m[160]; int n = read (f, m, sizeof m - 1);
	close (f);
	unlink (NOTES_OPEN);
	if (n <= 0) return;
	m[n] = 0;
	if (notes_find (g_notes, m) >= 0) select_file (g_notes.n[notes_find (g_notes, m)].file);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/notes.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	const char *arg = argc > 1 ? argv[1] : "";
	if (other_notes (arg)) return 0;
	s_jet = (const unsigned *) ui_jetons ();
	notes_cfg_load (g_cfg);
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int w = g_cfg.width < MIN_W ? MIN_W : g_cfg.width > 2000 ? 2000 : g_cfg.width;
	int h = g_cfg.height < MIN_H ? MIN_H : g_cfg.height > 1400 ? 1400 : g_cfg.height;
	if (w > scr[0] - 16) w = (int) scr[0] - 16;
	if (h > scr[1] - scr[2] - scr[3] - 16) h = (int) (scr[1] - scr[2] - scr[3] - 16);
	if (ui_window_ex (-1, -1, w, h, (long) "Notes", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (MIN_W, MIN_H);

	g_bNew = ui_pushbutton (0, 0, 130, 30, (long) "Nouvelle note", UB_DEFAULT);
	g_bDel = ui_pushbutton (0, 0, 96, 30, (long) "Supprimer", 0);
	g_bPin = ui_pushbutton (0, 0, 90, 30, (long) TR ("\xC3\x89pingler"), 0);
	g_vcol = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vcol, (long) col_paint, (long) col_mouse, 0, 0);
	g_vlist = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_vlist, (long) list_paint, (long) list_mouse, (long) list_key, (long) list_wheel);
	g_sbar = ui_scrollbar (0, 0, SBW, 10, 0);
	g_split = ui_splitter (0, 0, SPLITW, 10, 180, 2000);
	g_vinfo = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vinfo, (long) info_paint, 0, 0, 0);
	g_ed = ui_textarea (0, 0, 10, 10, UTA_VSCROLL);
	ui_textarea_font (g_ed, 15);
	g_lcount = ui_label (0, 0, 10, ST_H, (long) "", ULB_DIM);
	g_lmsg = ui_label (0, 0, 10, ST_H, (long) "", ULB_DIM | ULB_RIGHT);
	ui_on_resize ((long) on_resize, 0);
	lay_out (ui.w, ui.h);

	// the notes, and the one to show: the argument's, else the one left last time, else the newest; none: a new empty note
	notes_scan (g_notes);
	int i = arg[0] ? notes_find (g_notes, arg) : -1;
	if (i < 0 && g_cfg.last[0]) i = notes_find (g_notes, g_cfg.last);
	if (i < 0 && g_notes.count) i = 0;
	if (i < 0 && notes_add_new (g_notes, notes_now ()) == 0) i = 0;
	load (i);
	NoteInfo *e = cur ();
	ui_w_focus (e && !e->saved ? g_ed : g_vlist);
	repaint (ui.w, ui.h);

	bool run = true;
	unsigned lastPoll = 0;
	while (run)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) run = command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP) on_drop ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w2 = uikit_w_event (&code, &taken);
			if (code) control (w2, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) run = command ((int) id);
			}
			if (ui_w_get_focus () == g_ed) text_changed ();		// (Ctrl+X, V and the like inside the field)
		}
		else ui_w_idle ();
		unsigned now = ticks_ms ();
		if (g_msgUntil && (int) (now - g_msgUntil) >= 0) set_msg ("", 0);
		// the save after a pause in typing (after a failure: at the next pause, not each second)
		if (g_dirty && now - g_lastEdit >= AUTOSAVE && !(g_failed && g_failedAt == g_lastEdit)) save_current ();
		if (now - lastPoll >= 1000) { lastPoll = now; poll_open (); }
		ui_view_flush ();
	}

	// The window closed: the note saved -- a save that fails puts its text on the clipboard --, or, empty, not kept
	e = cur ();
	if (e && !g_bad && g_dirty && ed_len () && !save_current ()) { ui_clip_set ((long) ed_text (), ed_len ()); g_dirty = false; }
	e = cur ();
	snprintf (g_cfg.last, sizeof g_cfg.last, "%s", e && e->saved && (ed_len () || g_bad) ? e->file : "");
	leave_current ();
	g_cfg.width = g_ww; g_cfg.height = g_wh;
	notes_cfg_save (g_cfg);
	return 0;
}
