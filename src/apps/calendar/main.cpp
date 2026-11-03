//
// calendar -- the planner of Onyx (user/Apps/calendar) on ARMPI's UIKit: your appointments and your tasks, by the day, the
// week or the month.
//
// On the left, a month to jump to a day (ui_calendar), your calendars (the categories; a click shows or hides one) and
// your tasks (the round box ticks one off; a double click edits it; the field below adds one). In the middle, the period
// chosen on top (Jour / Semaine / Mois, Aujourd'hui, < and >): a day and a week as a grid of the hours -- a double click on
// an empty slot (or a drag down it) makes an event there, a drag moves one, its bottom edge resizes it, a double click
// opens it --, the month as a grid of days. An event: its title, all day or its times, a repetition, a reminder (the agenda
// applet sends it, or the calendar when the applet is not there), a category, a place, notes.
//
// Kept as iCalendar in SD0:/apps/calendar.app/calendar.ics, the very file Onyx's Calendar reads (model.h); Fichier >
// Importer / Exporter exchange .ics files. agenda.txt and reminders.txt are written for the agenda applet (agendafiles.h),
// which is told at each save. An argument "YYYYMMDD" opens that day.
//
#include <stdio.h>
#include "app.h"
#include "model.h"
#include "agendafiles.h"
static long s_cancelBtn;			// (the dialog's Annuler)
static long dlg_fn (long d, long w, long code);
#include "views.h"
#include "dialogs.h"

#define W_WANT		1000
#define H_WANT		660
#define TB_H		46
#define SIDE_W		250

static int  g_view = 1;				// 0 day, 1 week, 2 month
static int  g_anchor;				// the day the period is around
static int  g_ww, g_wh;
static long g_title, g_seg, g_mini, g_addTask, g_bNew, g_bToday, g_bPrev, g_bNext;

static long dlg_fn (long d, long w, long code)
{
	if (code == UN_ACTIVATE && w == s_cancelBtn) { ui_dialog_close (d, 0); return 0; }
	return ed_fn (d, w, code);
}

// ---- the file ------------------------------------------------------------------------------------------

static void default_categories ()
{
	cat_add (TR ("Travail"), 0x003B82F6); cat_add (TR ("Personnel"), 0x0010B981); cat_add (TR ("Famille"), 0x00F59E0B);
	cat_add (TR ("Sport"), 0x008B5CF6); cat_add (TR ("Anniversaires"), 0x00EC4899);
}
// The agenda applet re-reads its files now
static void tell_agenda ()
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) "agenda", 0);
	if (ch < 0) return;
	static long m[64]; m[0] = 1;
	sys_call (SYS_SVC_CALL_, ch, (long) m);
}
static void save ()
{
	int len = 0;
	char *t = ics_write (&len);
	save_file (CAL_ICS, t, len);
	delete [] t;
	cal_write_agenda (g_today);
	tell_agenda ();
}
static void load ()
{
	char *t = cal_read_file (CAL_ICS, 0);
	if (t) { ics_read (t, false); delete [] t; }
	else { default_categories (); save (); }
	if (g_ncat == 0) default_categories ();
}

// ---- what is shown ------------------------------------------------------------------------------------

static void refresh ()
{
	char t[96] = "";
	int y, m, d; civil_from_days (g_anchor, y, m, d);
	ui_w_show (g_vgrid, g_view != 2); ui_w_show (g_sbar, g_view != 2); ui_w_show (g_vmonth, g_view == 2);
	if (g_view == 2)
	{
		g_month->year = y; g_month->month = m; g_month->sel = g_anchor;
		scat (t, sizeof t, MONTH[m - 1]); scat (t, sizeof t, " "); scatn (t, sizeof t, y);
		ui_view_dirty (g_vmonth);
	}
	else
	{
		int d0 = g_view == 1 ? g_anchor - wday (g_anchor) : g_anchor;
		g_grid->ndays = g_view == 1 ? 7 : 1; g_grid->day0 = d0;
		if (g_view == 0)
		{
			scat (t, sizeof t, WDAY[wday (d0)]); scat (t, sizeof t, " "); scatn (t, sizeof t, d); scat (t, sizeof t, " ");
			scat (t, sizeof t, MONTH[m - 1]); scat (t, sizeof t, " "); scatn (t, sizeof t, y);
		}
		else
		{
			int d1 = d0 + 6;
			scatn (t, sizeof t, dn_d (d0));
			if (dn_m (d1) != dn_m (d0)) { scat (t, sizeof t, " "); scat (t, sizeof t, MONTH[dn_m (d0) - 1]); }
			scat (t, sizeof t, " - "); scatn (t, sizeof t, dn_d (d1)); scat (t, sizeof t, " "); scat (t, sizeof t, MONTH[dn_m (d1) - 1]);
			scat (t, sizeof t, " "); scatn (t, sizeof t, dn_y (d1));
		}
		ui_view_dirty (g_vgrid);
	}
	ui_w_set_text (g_title, (long) t);
	ui_w_set_value (g_seg, g_view);
	ui_calendar_show (g_mini, date_of (g_anchor));
	ui_w_draw (g_mini);
	ui_view_dirty (g_vtasks); ui_view_dirty (g_vcats);
}
static void goto_day (int dn, int view) { g_anchor = dn; if (view >= 0) g_view = view; refresh (); }
static void data_changed (bool s) { if (s) save (); refresh (); }
static void step (int dir)
{
	if (g_view == 0) g_anchor += dir;
	else if (g_view == 1) g_anchor += 7 * dir;
	else g_anchor = add_months (g_anchor, dir);
	refresh ();
}

// ---- events ---------------------------------------------------------------------------------------------

static void run_editor (const Event &src, int index, int occStart)
{
	int r = event_dialog (src, index < 0);
	if (r == 1)
	{
		if (index < 0) { Event &e = ev_new (); e = ed.e; g_sel = g_nev - 1; g_selStart = e.start; goto_day (e.start / 1440, -1); }
		else if (g_ev[index].freq != RP_NONE)		// an occurrence of a series was edited
		{
			int c = choose ("Modifier un \xC3\xA9v\xC3\xA9nement r\xC3\xA9p\xC3\xA9t\xC3\xA9", "Modifier seulement celui-ci, ou tous ?", "Celui-ci", "Tous");
			if (c == 0) return;
			Event &orig = g_ev[index];
			if (c == 1)				// this one: out of the series, an event of its own
			{
				if (orig.nex < MAXEX) orig.exdate[orig.nex++] = occStart / 1440;
				Event one = ed.e; new_uid (one.uid, sizeof one.uid);
				one.freq = RP_NONE; one.nex = 0; one.until = 0; one.count = 0;
				Event &n = ev_new (); n = one;
				g_sel = g_nev - 1; g_selStart = one.start;
			}
			else					// all: the series moved by as much as this one was
			{
				int dur = ed.e.end - ed.e.start, st = orig.start + (ed.e.start - occStart);
				orig = ed.e; orig.start = st; orig.end = st + dur;
				g_selStart = ed.e.start;
			}
		}
		else g_ev[index] = ed.e;
		data_changed (true);
	}
	else if (r == 3 && index >= 0)
	{
		Event &e = g_ev[index];
		if (e.freq != RP_NONE)
		{
			int c = choose ("Supprimer un \xC3\xA9v\xC3\xA9nement r\xC3\xA9p\xC3\xA9t\xC3\xA9", "Supprimer seulement celui-ci, ou tous ?", "Celui-ci", "Tous");
			if (c == 0) return;
			if (c == 1 && e.nex < MAXEX) { e.exdate[e.nex++] = occStart / 1440; g_sel = -1; data_changed (true); return; }
		}
		ev_delete (index); g_sel = -1;
		data_changed (true);
	}
}
static void open_event (int ev, int occStart)
{
	if (ev < 0 || ev >= g_nev) return;
	static Event copy; copy = g_ev[ev];
	if (copy.freq != RP_NONE)			// (shown on the occurrence's day)
	{
		int delta = occStart - copy.start;
		copy.start += delta; copy.end += delta;
	}
	run_editor (copy, ev, occStart);
}
static void new_event_at (int start, bool allDay, int end = -1)
{
	static Event e; ev_init (e);
	e.cat = g_ncat ? 0 : -1;
	for (int i = 0; i < g_ncat; i++) if (g_cat[i].shown) { e.cat = i; break; }
	if (allDay) { e.allDay = true; e.start = start / 1440 * 1440; e.end = e.start + 1440; }
	else { e.start = start; e.end = end > start ? end : start + 60; e.remind = 15; }
	run_editor (e, -1, e.start);
}
static void event_moved (int ev, int oldOcc, int ns, int ne)
{
	Event &e = g_ev[ev];
	if (e.freq != RP_NONE)
	{
		int c = choose ("D\xC3\xA9placer un \xC3\xA9v\xC3\xA9nement r\xC3\xA9p\xC3\xA9t\xC3\xA9", "D\xC3\xA9placer seulement celui-ci, ou tous ?", "Celui-ci", "Tous");
		if (c == 0) { refresh (); return; }
		if (c == 1)
		{
			if (e.nex < MAXEX) e.exdate[e.nex++] = oldOcc / 1440;
			static Event one; one = e; new_uid (one.uid, sizeof one.uid);
			one.freq = RP_NONE; one.nex = 0; one.until = 0; one.count = 0; one.start = ns; one.end = ne;
			Event &n = ev_new (); n = one;
			g_sel = g_nev - 1; g_selStart = ns;
			data_changed (true);
			return;
		}
		int dur = ne - ns;
		e.start += ns - oldOcc; e.end = e.start + dur;
	}
	else { e.start = ns; e.end = ne; }
	g_selStart = ns;
	data_changed (true);
}
static void del_event ()
{
	if (g_sel < 0 || g_sel >= g_nev) return;
	Event &e = g_ev[g_sel];
	if (e.freq != RP_NONE)
	{
		int c = choose ("Supprimer un \xC3\xA9v\xC3\xA9nement r\xC3\xA9p\xC3\xA9t\xC3\xA9", "Supprimer seulement celui-ci, ou tous ?", "Celui-ci", "Tous");
		if (c == 0) return;
		if (c == 1 && e.nex < MAXEX) { e.exdate[e.nex++] = g_selStart / 1440; g_sel = -1; data_changed (true); return; }
	}
	ev_delete (g_sel); g_sel = -1;
	data_changed (true);
}

// ---- tasks ------------------------------------------------------------------------------------------------

static void task_edit (int i)
{
	if (i < 0 || i >= g_ntk) return;
	int r = task_dialog (g_tk[i], false);
	if (r == 1) { g_tk[i] = tk.t; data_changed (true); }
	else if (r == 3) { tk_delete (i); data_changed (true); }
}
static void task_new ()
{
	Task t; t.title[0] = '\0'; new_uid (t.uid, sizeof t.uid); t.due = g_anchor; t.done = false; t.cat = -1;
	if (task_dialog (t, true) == 1) { Task &n = tk_new (); n = tk.t; data_changed (true); }
}
static void add_task ()
{
	const char *s = text_of (g_addTask);
	if (!s[0]) return;
	Task &t = tk_new ();
	scpy (t.title, sizeof t.title, s);
	ui_w_set_text (g_addTask, 0);
	data_changed (true);
}

// ---- the commands ------------------------------------------------------------------------------------------

enum { M_NEW = 1, M_TASK, M_IMPORT, M_EXPORT, M_OPEN, M_DELETE, M_DAY, M_WEEK, M_MONTH, M_TODAY, M_PREV, M_NEXT, M_QUIT };
static void on_new () { new_event_at (g_anchor * 1440 + (g_anchor == g_today ? (g_nowMin / 60 + 1) * 60 : 9 * 60), false); }
static void on_today () { goto_day (g_today, -1); if (g_view != 2) g_grid->scrollToHour (g_nowMin / 60 > 2 ? g_nowMin / 60 - 2 : 0); }
static void m_import ()
{
	static char path[256];
	ui_file_filter ((long) "*.ics");
	long ok = ui_file_open ((long) "SD0:/home", (long) path, sizeof path);
	ui_file_filter (0);
	if (!ok) return;
	char *t = cal_read_file (path, 0);
	if (!t) { ui_message ((long) TR ("Importer"), (long) TR ("Le fichier ne peut pas \xC3\xAAtre lu."), UM_OK); return; }
	int n = ics_read (t, true);
	delete [] t;
	data_changed (true);
	char m[80] = ""; scatn (m, sizeof m, n); scat (m, sizeof m, TR (" \xC3\xA9l\xC3\xA9ment(s) import\xC3\xA9(s)."));
	ui_message ((long) TR ("Importer"), (long) m, UM_OK);
}
static void m_export ()
{
	static char path[256];
	ui_file_filter ((long) "*.ics");
	long ok = ui_file_save ((long) "SD0:/home", (long) "calendar.ics", (long) path, sizeof path);
	ui_file_filter (0);
	if (!ok) return;
	int len = 0;
	char *t = ics_write (&len);
	bool done = save_file (path, t, len);
	delete [] t;
	ui_message ((long) TR ("Exporter"), (long) (done ? TR ("Le calendrier est export\xC3\xA9.") : TR ("Le fichier ne peut pas \xC3\xAAtre \xC3\xA9" "crit.")), UM_OK);
}
static bool command (int id)
{
	switch (id)
	{
	case M_NEW: on_new (); break;
	case M_TASK: task_new (); break;
	case M_IMPORT: m_import (); break;
	case M_EXPORT: m_export (); break;
	case M_OPEN: if (g_sel >= 0) open_event (g_sel, g_selStart); break;
	case M_DELETE: del_event (); break;
	case M_DAY: g_view = 0; refresh (); break;
	case M_WEEK: g_view = 1; refresh (); break;
	case M_MONTH: g_view = 2; refresh (); break;
	case M_TODAY: on_today (); break;
	case M_PREV: step (-1); break;
	case M_NEXT: step (1); break;
	case M_QUIT: return false;
	}
	return true;
}
static void control (long w, long code)
{
	if (w == g_addTask && code == UN_ACTIVATE) add_task ();
	else if (code == UN_ACTIVATE && w == g_bNew) on_new ();
	else if (code == UN_ACTIVATE && w == g_bToday) on_today ();
	else if (code == UN_ACTIVATE && w == g_bPrev) step (-1);
	else if (code == UN_ACTIVATE && w == g_bNext) step (1);
	else if (w == g_seg && code == UN_CHANGE) { g_view = val (g_seg); refresh (); }
	else if (w == g_mini && code == UN_CHANGE) goto_day (dn_of (ui_w_get_value (g_mini)), -1);
	else if (w == g_sbar && code == UN_CHANGE) { g_grid->scrollY = val (g_sbar); ui_view_dirty (g_vgrid); }
}

// ---- reminders: a notification when one is due, if the agenda applet is not there --------------------------------

static void notify (const char *title, const char *msg)
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) "notify", 0);
	if (ch < 0) return;
	static char m[512];
	*(long *) m = 1;
	char *o = m + 48, *end = m + 510;
	while (*title && o < end) *o++ = *title++;
	*o++ = 0;
	while (*msg && o < end) *o++ = *msg++;
	*o = 0;
	sys_call (SYS_SVC_CALL_, ch, (long) m);
}
static void reminders ()
{
	static int lastMin = -1;
	int now = g_today * 1440 + g_nowMin;
	if (now == lastMin) return;
	if (sys_call (SYS_SVC_OPEN_, (long) "agenda", 0) >= 0 || lastMin < 0) { lastMin = now; return; }	// (the applet sends them)
	static Occ occ[200];
	int n = occurrences (g_today, g_today + 3, occ, 200, true);
	for (int i = 0; i < n; i++)
	{
		const Event &e = g_ev[occ[i].ev];
		if (e.remind < 0) continue;
		int at = occ[i].start - e.remind;
		if (at > lastMin && at <= now)
		{
			char t[200]; cal_reminder_text (e, occ[i].start, t, sizeof t);
			notify (TR ("Calendrier"), t);
		}
	}
	lastMin = now;
}
static void clock_now (int &today, int &now)
{
	long s = sys_call (SYS_CLOCK_GET_, 2);
	today = (int) (s / 86400); now = (int) (s % 86400 / 60);
}

// ---- the window ----------------------------------------------------------------------------------------------

static void lay_out (long w, long h)
{
	g_ww = (int) w; g_wh = (int) h;
	int c = ctl_h (), by = (TB_H - 30) / 2, side = w < 640 ? 0 : w < 900 ? 236 : SIDE_W;
	int x = 10;
	ui_w_move (g_bNew, x, by, 150, 30); x += 150 + 12;
	ui_w_move (g_bToday, x, by, 104, 30); x += 104 + 6;
	ui_w_move (g_bPrev, x, by, 30, 30); x += 30 + 2;
	ui_w_move (g_bNext, x, by, 30, 30); x += 30 + 10;
	int sx = (int) w - FRAME_CAP_W - 8 - 216;
	ui_w_move (g_seg, sx, by, 216, 30);
	ui_w_move (g_title, x, by, sx - x - 8 > 0 ? sx - x - 8 : 0, 30);
	int bh = (int) h - TB_H;
	bool on = side > 0;
	ui_w_show (g_mini, on); ui_w_show (g_vcats, on); ui_w_show (g_vtasks, on); ui_w_show (g_addTask, on);
	if (on)
	{
		int cw = side - 12, sy = TB_H + 2, ch = 30 + 22 + 6 * 24;
		ui_w_move (g_mini, 6, sy, cw, ch);
		sy += ch + 8;
		int cats = HEADH + 2 + g_ncat * CATH;
		ui_w_move (g_vcats, 10, sy, side - 20, cats);
		sy += cats + 8;
		ui_w_move (g_vtasks, 10, sy, side - 20, (int) h - sy - c - 12);
		ui_w_move (g_addTask, 10, (int) h - c - 8, side - 20, c);
	}
	ui_w_move (g_vgrid, side, TB_H, (int) w - side - 12, bh);
	ui_w_move (g_vmonth, side, TB_H, (int) w - side, bh);
	ui_w_move (g_sbar, (int) w - 12, TB_H, 12, bh);
	ui_view_dirty (g_vgrid); ui_view_dirty (g_vmonth); ui_view_dirty (g_vcats); ui_view_dirty (g_vtasks);
}
static void on_resize (long w, long h) { ui_fill (0, 0, w, h, C_BG); lay_out (w, h); ui_w_draw (0); }

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{
		char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/calendar.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang);
		for (int i = 0; i < 12; i++) { MONTH[i] = TR (MONTH[i]); MON3[i] = TR (MON3[i]); }
		for (int i = 0; i < 7; i++) { WDAY[i] = TR (WDAY[i]); WD3[i] = TR (WD3[i]); }
	}
	clock_now (g_today, g_nowMin);
	g_utcOffset = (int) (sys_call (SYS_CLOCK_GET_, 1) / 60);
	g_anchor = g_today;
	if (argc > 1 && strlen (argv[1]) >= 8 && argv[1][0] >= '1' && argv[1][0] <= '9')
	{
		const char *a = argv[1];
		int ay = atoi_n (a, 4), am = atoi_n (a + 4, 2), ad = atoi_n (a + 6, 2);
		if (am >= 1 && am <= 12 && ad >= 1 && ad <= 31) { g_anchor = days_from_civil (ay, am, ad); g_view = 0; }
	}
	mkdir (CAL_DIR, 0755);
	load ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = W_WANT < scr[0] - 16 ? W_WANT : (int) scr[0] - 16, wh = H_WANT < scr[1] - scr[2] - scr[3] - 16 ? H_WANT : (int) (scr[1] - scr[2] - scr[3] - 16);
	if (ui_window_ex (-1, -1, ww, wh, (long) TR ("Calendrier"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (520, 400);
	ui_menu ((long)
		"MFichier\n" "I1\tNouvel \xC3\xA9v\xC3\xA9nement\tCtrl+N\n" "I2\tNouvelle t\xC3\xA2" "che...\n" "-\n"
		"I3\tImporter iCalendar...\n" "I4\tExporter iCalendar...\n" "-\n" "I13\tQuitter\n"
		"M\xC3\x89v\xC3\xA9nement\n" "I5\tOuvrir\tEntr\xC3\xA9" "e\n" "I6\tSupprimer\n"
		"MPr\xC3\xA9sentation\n" "I7\tJour\n" "I8\tSemaine\n" "I9\tMois\n" "-\n"
		"I10\tAujourd'hui\tCtrl+T\n" "I11\tPr\xC3\xA9" "c\xC3\xA9" "dent\n" "I12\tSuivant\n");
	g_grid = new TimeGrid; g_month = new MonthGrid;

	g_bNew = ui_pushbutton (0, 0, 150, 30, (long) "Nouvel \xC3\xA9v\xC3\xA9nement", UB_DEFAULT);
	g_bToday = ui_pushbutton (0, 0, 104, 30, (long) "Aujourd'hui", 0);
	g_bPrev = ui_pushbutton (0, 0, 30, 30, (long) "<", 0);
	g_bNext = ui_pushbutton (0, 0, 30, 30, (long) ">", 0);
	g_title = ui_label (0, 0, 100, 30, (long) "", ULB_BOLD);
	g_seg = ui_segmented (0, 0, 216, 30, (long) TR ("Jour|Semaine|Mois"));
	g_mini = ui_calendar (0, 0, 200, 200, date_of (g_anchor));
	ui_calendar_today (g_mini, date_of (g_today));
	g_vcats = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vcats, (long) cats_paint, (long) cats_mouse, 0, 0);
	g_vtasks = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vtasks, (long) tasks_paint, (long) tasks_mouse, 0, (long) tasks_wheel);
	g_addTask = ui_textbox (0, 0, 10, 28, 0);
	ui_textbox_hint (g_addTask, (long) TR ("+ Ajouter une t\xC3\xA2" "che"));
	g_vgrid = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_vgrid, (long) grid_paint, (long) grid_mouse, (long) grid_key, (long) grid_wheel);
	g_vmonth = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_vmonth, (long) month_paint, (long) month_mouse, 0, (long) month_wheel);
	g_sbar = ui_scrollbar (0, 0, 12, 10, 0);
	ui_on_resize ((long) on_resize, 0);
	lay_out (ui.w, ui.h);
	refresh ();
	ui_w_focus (g_vgrid);
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	ui_w_draw (0);
	ui_update_all ();

	bool run = true;
	while (run)
	{
		long t = ui_event (500);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) run = command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) run = command ((int) id);
			}
		}
		else
		{
			ui_w_idle ();
			int today, now; clock_now (today, now);
			if (today != g_today || now != g_nowMin)
			{
				bool newDay = today != g_today;
				g_today = today; g_nowMin = now;
				if (g_view != 2) ui_view_dirty (g_vgrid);
				if (newDay) { ui_calendar_today (g_mini, date_of (g_today)); refresh (); cal_write_agenda (g_today); tell_agenda (); }
				reminders ();
			}
		}
		ui_view_flush ();
		if (g_todo.what)
		{
			int k = g_todo.what, a = g_todo.a, b = g_todo.b; g_todo.what = T_NONE;
			if (k == T_OPEN) open_event (a, b);
			else if (k == T_NEW) new_event_at (a, false, b);
			else if (k == T_NEWDAY) new_event_at (a, true);
			else if (k == T_TASK) task_edit (a);
			ui_view_flush ();
		}
	}
	return 0;
}
