//
// dialogs.h -- the event editor (title, all day, start and end, repeat -- the days of the week --, until, reminder,
// category, place, notes; Delete / Cancel / Save), the task editor, and a question with buttons of its own ("Celle-ci" /
// "Toutes"), each a function and a static state over ui_dialog_*.
//
#ifndef _calendar_dialogs_h
#define _calendar_dialogs_h

static long date_of (int dn) { return (long) dn_y (dn) << 16 | dn_m (dn) << 8 | dn_d (dn); }
static int  dn_of (long v) { return days_from_civil ((int) (v >> 16), (int) (v >> 8 & 0xFF), (int) (v & 0xFF)); }
static const char *text_of (long w) { return (const char *) ui_w_get_text (w); }
static int  val (long w) { return (int) ui_w_get_value (w); }
static void lab (int x, int y, int w, const char *s) { ui_label (x, y, w, ctl_h (), (long) s, 0); }

// "9:30", "930", "9h30", "9" -> minutes (-1: not a time).
static int parse_hm (const char *s)
{
	int h = 0, m = 0, nd = 0; const char *p = s;
	while (*p == ' ') p++;
	while (*p >= '0' && *p <= '9') { h = h * 10 + (*p++ - '0'); nd++; }
	if (nd == 0) return -1;
	if (nd >= 3) { m = h % 100; h /= 100; }
	else if (*p == ':' || *p == 'h' || *p == 'H' || *p == '.')
	{
		p++; int k = 0;
		while (*p >= '0' && *p <= '9' && k < 2) { m = m * 10 + (*p++ - '0'); k++; }
	}
	if (h > 23 || m > 59) return -1;
	return h * 60 + m;
}

// A question with its own buttons: -> 1, 2, 0: cancelled.
static long no_fn (long, long, long) { return 0; }
static int choose (const char *title, const char *text, const char *a, const char *b)
{
	int W = 440, H = 130;
	long d = ui_dialog_begin (W, H, (long) title);
	if (!d) return 0;
	ui_label (16, 14, W - 32, 40, (long) text, ULB_WRAP);
	long e = ui_dialog_button (d, 0, (long) a, 1, UB_DEFAULT, W - 12);
	e = ui_dialog_button (d, 1, (long) b, 2, 0, e);
	ui_dialog_button (d, 2, (long) "Annuler", 0, 0, e);
	ui_dialog_cancel (d, 0);
	int r = (int) ui_dialog_run (d, (long) no_fn, 0);
	ui_dialog_end (d);
	return r;
}

// ---- the event editor --------------------------------------------------------------------------------------

static const char REPEAT[] = "Ne se r\xC3\xA9p\xC3\xA8te pas|Chaque jour|Chaque jour ouvr\xC3\xA9 (lun - ven)|Chaque semaine|Toutes les 2 semaines|Chaque mois|Chaque ann\xC3\xA9" "e";
static const char REPEAT8[] = "Ne se r\xC3\xA9p\xC3\xA8te pas|Chaque jour|Chaque jour ouvr\xC3\xA9 (lun - ven)|Chaque semaine|Toutes les 2 semaines|Chaque mois|Chaque ann\xC3\xA9" "e|Personnalis\xC3\xA9 (import\xC3\xA9)";
static const int  REMIND_MIN[] = { -1, 0, 5, 10, 15, 30, 60, 120, 1440, 2880 };
static const char REMIND[] = "Pas de rappel|Au d\xC3\xA9" "but|5 minutes avant|10 minutes avant|15 minutes avant|30 minutes avant|1 heure avant|2 heures avant|1 jour avant|2 jours avant";

static struct
{
	Event e; bool isNew;
	long dlg, title, allDay, d0, t0, d1, t1, repeat, untilOn, until, day[7], lDays, remind, cat, place, notes, err, save, del, lEnd;
} ed;

static void cat_list (char *o, int cap)
{
	o[0] = '\0';
	for (int i = 0; i < g_ncat; i++) { scat (o, cap, g_cat[i].name); scat (o, cap, "|"); }
	scat (o, cap, TR ("Aucune"));
}
static int repeat_option ()
{
	const Event &e = ed.e;
	if (e.freq == RP_NONE) return 0;
	if (e.count) return 7;
	if (e.freq == RP_DAILY && e.interval == 1) return 1;
	if (e.freq == RP_WEEKLY && e.interval == 1 && e.wmask == 0x1F) return 2;
	if (e.freq == RP_WEEKLY && e.interval == 1) return 3;
	if (e.freq == RP_WEEKLY && e.interval == 2) return 4;
	if (e.freq == RP_MONTHLY && e.interval == 1) return 5;
	if (e.freq == RP_YEARLY && e.interval == 1) return 6;
	return 7;
}
static void ed_sync ()						// what shows follows the choices
{
	bool ad = val (ed.allDay) != 0;
	ui_w_show (ed.t0, !ad); ui_w_show (ed.t1, !ad);
	int r = val (ed.repeat);
	ui_w_show (ed.untilOn, r != 0);
	ui_w_show (ed.until, r != 0 && val (ed.untilOn));
	bool weekly = r == 3 || r == 4;
	ui_w_show (ed.lDays, weekly);
	for (int i = 0; i < 7; i++) ui_w_show (ed.day[i], weekly);
}
static bool ed_collect ()					// the fields -> e; false (and why) if they do not hold
{
	Event &e = ed.e;
	scpy (e.title, sizeof e.title, text_of (ed.title));
	scpy (e.place, sizeof e.place, text_of (ed.place));
	scpy (e.notes, sizeof e.notes, text_of (ed.notes));
	e.allDay = val (ed.allDay) != 0;
	int sd = dn_of (ui_w_get_value (ed.d0)), dd = dn_of (ui_w_get_value (ed.d1));
	if (e.allDay)
	{
		if (dd < sd) { ui_w_set_text (ed.err, (long) TR ("La fin est avant le d\xC3\xA9" "but.")); return false; }
		e.start = sd * 1440; e.end = (dd + 1) * 1440;
	}
	else
	{
		int a = parse_hm (text_of (ed.t0)), b = parse_hm (text_of (ed.t1));
		if (a < 0 || b < 0) { ui_w_set_text (ed.err, (long) TR ("Une heure n'est pas valable (9:30).")); return false; }
		e.start = sd * 1440 + a; e.end = dd * 1440 + b;
		if (e.end <= e.start) { ui_w_set_text (ed.err, (long) TR ("La fin est avant le d\xC3\xA9" "but.")); return false; }
	}
	int wm = 0; for (int i = 0; i < 7; i++) if (val (ed.day[i])) wm |= 1 << i;
	if (!wm) wm = 1 << wday (sd);
	int r = val (ed.repeat);
	switch (r)
	{
	case 0: e.freq = RP_NONE; break;
	case 1: e.freq = RP_DAILY; e.interval = 1; e.wmask = 0; break;
	case 2: e.freq = RP_WEEKLY; e.interval = 1; e.wmask = 0x1F; break;
	case 3: e.freq = RP_WEEKLY; e.interval = 1; e.wmask = wm; break;
	case 4: e.freq = RP_WEEKLY; e.interval = 2; e.wmask = wm; break;
	case 5: e.freq = RP_MONTHLY; e.interval = 1; e.wmask = 0; break;
	case 6: e.freq = RP_YEARLY; e.interval = 1; e.wmask = 0; break;
	default: break;						// (custom: as it came)
	}
	if (r != 7) e.count = 0;
	e.until = e.freq != RP_NONE && val (ed.untilOn) ? dn_of (ui_w_get_value (ed.until)) : 0;
	if (e.until && e.until < sd) { ui_w_set_text (ed.err, (long) TR ("La r\xC3\xA9p\xC3\xA9tition finit avant le d\xC3\xA9" "but.")); return false; }
	e.remind = REMIND_MIN[val (ed.remind)];
	e.cat = val (ed.cat) < g_ncat ? val (ed.cat) : -1;
	return true;
}
static long ed_fn (long d, long w, long code)
{
	if (code == UN_CHANGE && (w == ed.allDay || w == ed.repeat || w == ed.untilOn)) ed_sync ();
	if (code != UN_ACTIVATE) return 0;
	if (w == ed.del) ui_dialog_close (d, 3);
	else if ((w == ed.save || w == ed.title || w == ed.place) && ed_collect ()) ui_dialog_close (d, 1);
	return 0;
}
// -> 1 saved (ed.e), 3 delete, 0 cancelled
static int event_dialog (const Event &src, bool isNew)
{
	ed.e = src; ed.isNew = isNew;
	const Event &e = ed.e;
	int W = 600, H = 446, c = ctl_h(), x = 16, lw = 80, W2 = W - 32, y = 12;
	long d = ui_dialog_begin (W, H, (long) (isNew ? "Nouvel \xC3\xA9v\xC3\xA9nement" : "Modifier l'\xC3\xA9v\xC3\xA9nement"));
	if (!d) return 0;
	ed.title = ui_textbox (x, y, W2 - FRAME_CAP_W + 16, c + 2, 0);
	ui_textbox_hint (ed.title, (long) TR ("Ajouter un titre"));
	ui_w_set_text (ed.title, (long) e.title);
	y += c + 10;
	ed.allDay = ui_checkbox (x + lw, y, 160, 24, (long) TR ("Toute la journ\xC3\xA9" "e"), e.allDay);
	y += 30;
	int sd = e.start / 1440, dd = e.allDay ? (e.end - 1) / 1440 : e.end / 1440;
	if (dd < sd) dd = sd;
	char hm[8];
	lab (x, y, lw, "D\xC3\xA9" "but");
	ed.d0 = ui_datepicker (x + lw, y, 130, c, date_of (sd));
	fmt_hm (e.start, hm, sizeof hm);
	ed.t0 = ui_textbox (x + lw + 138, y, 62, c, 0); ui_w_set_text (ed.t0, (long) hm);
	ed.lEnd = ui_label (x + lw + 208, y, 36, c, (long) "Fin", 0);
	ed.d1 = ui_datepicker (x + lw + 246, y, 130, c, date_of (dd));
	fmt_hm (e.end, hm, sizeof hm);
	ed.t1 = ui_textbox (x + lw + 384, y, 62, c, 0); ui_w_set_text (ed.t1, (long) hm);
	y += c + 8;
	lab (x, y, lw, "R\xC3\xA9p\xC3\xA9tition");
	int ro = repeat_option ();
	ed.repeat = ui_dropdown (x + lw, y, 240, c, (long) TR (ro == 7 ? REPEAT8 : REPEAT));
	ui_w_set_value (ed.repeat, ro);
	ed.untilOn = ui_checkbox (x + lw + 252, y + 2, 90, 24, (long) TR ("Jusqu'au"), e.until != 0);
	int ud = e.until ? e.until : add_months (sd, 3);
	ed.until = ui_datepicker (x + lw + 346, y, 130, c, date_of (ud));
	y += c + 8;
	ed.lDays = ui_label (x, y, lw, c, (long) "Les", 0);
	int wm = e.wmask ? e.wmask : 1 << wday (sd);
	for (int i = 0; i < 7; i++) ed.day[i] = ui_checkbox (x + lw + i * 56, y + 2, 54, 24, (long) WD3[i], (wm >> i) & 1);
	y += c + 8;
	lab (x, y, lw, "Rappel");
	ed.remind = ui_dropdown (x + lw, y, 180, c, (long) TR (REMIND));
	int ri = 0; for (int i = 0; i < 10; i++) if (REMIND_MIN[i] == e.remind) ri = i;
	ui_w_set_value (ed.remind, ri);
	lab (x + lw + 194, y, 84, "Cat\xC3\xA9gorie");
	char cats[MAXCAT * 34 + 16]; cat_list (cats, sizeof cats);
	ed.cat = ui_dropdown (x + lw + 282, y, W2 - lw - 282, c, (long) cats);
	ui_w_set_value (ed.cat, e.cat >= 0 && e.cat < g_ncat ? e.cat : g_ncat);
	y += c + 8;
	lab (x, y, lw, "Lieu");
	ed.place = ui_textbox (x + lw, y, W2 - lw, c, 0);
	ui_textbox_hint (ed.place, (long) TR ("Ajouter un lieu"));
	ui_w_set_text (ed.place, (long) e.place);
	y += c + 8;
	lab (x, y, lw, "Notes");
	ed.notes = ui_textarea (x + lw, y, W2 - lw, H - y - 54, UTA_VSCROLL | UTA_FORMTAB);
	ui_w_set_text (ed.notes, (long) e.notes);
	int by = H - 42;
	ed.del = 0;
	int ex = x;
	if (!isNew) { ed.del = ui_pushbutton (x, by, 110, 30, (long) "Supprimer", 0); ex = x + 118; }
	ed.err = ui_label (ex, by, W - ex - 230, 30, (long) "", ULB_DIM);
	ed.save = ui_pushbutton (W - 16 - 110, by, 110, 30, (long) "Enregistrer", UB_DEFAULT);
	long ca = ui_pushbutton (W - 16 - 110 - 8 - 96, by, 96, 30, (long) "Annuler", 0);
	ed.dlg = d;
	s_cancelBtn = ca;
	ui_dialog_cancel (d, 0);
	ed_sync ();
	ui_w_focus (ed.title);
	int r = (int) ui_dialog_run (d, (long) dlg_fn, 0);
	ui_dialog_end (d);
	return r;
}

// ---- the task editor -------------------------------------------------------------------------------------

static struct { Task t; long title, dueOn, due, cat, done, save, del; } tk;
static long tk_fn (long d, long w, long code)
{
	if (code != UN_ACTIVATE) return 0;
	if (w == s_cancelBtn) ui_dialog_close (d, 0);
	else if (w == tk.del) ui_dialog_close (d, 3);
	else if ((w == tk.save || w == tk.title) && text_of (tk.title)[0])
	{
		Task &t = tk.t;
		scpy (t.title, sizeof t.title, text_of (tk.title));
		t.due = val (tk.dueOn) ? dn_of (ui_w_get_value (tk.due)) : 0;
		t.done = val (tk.done) != 0;
		t.cat = val (tk.cat) < g_ncat ? val (tk.cat) : -1;
		ui_dialog_close (d, 1);
	}
	return 0;
}
// -> 1 saved (tk.t), 3 delete, 0 cancelled
static int task_dialog (const Task &src, bool isNew)
{
	tk.t = src;
	const Task &t = tk.t;
	int W = 480, H = 190, c = ctl_h (), x = 16, lw = 84, y = 12;
	long d = ui_dialog_begin (W, H, (long) (isNew ? "Nouvelle t\xC3\xA2" "che" : "Modifier la t\xC3\xA2" "che"));
	if (!d) return 0;
	tk.title = ui_textbox (x, y, W - 16 - FRAME_CAP_W, c + 2, 0);
	ui_textbox_hint (tk.title, (long) TR ("Que faut-il faire ?"));
	ui_w_set_text (tk.title, (long) t.title);
	y += c + 12;
	tk.dueOn = ui_checkbox (x, y + 2, lw, 24, (long) TR ("Pour le"), t.due != 0);
	tk.due = ui_datepicker (x + lw, y, 140, c, date_of (t.due ? t.due : g_today));
	tk.done = ui_checkbox (x + lw + 160, y + 2, 100, 24, (long) TR ("Faite"), t.done);
	y += c + 8;
	lab (x, y, lw, "Cat\xC3\xA9gorie");
	char cats[MAXCAT * 34 + 16]; cat_list (cats, sizeof cats);
	tk.cat = ui_dropdown (x + lw, y, 180, c, (long) cats);
	ui_w_set_value (tk.cat, t.cat >= 0 && t.cat < g_ncat ? t.cat : g_ncat);
	int by = H - 42;
	tk.del = 0;
	if (!isNew) tk.del = ui_pushbutton (x, by, 110, 30, (long) "Supprimer", 0);
	tk.save = ui_pushbutton (W - 16 - 110, by, 110, 30, (long) "Enregistrer", UB_DEFAULT);
	s_cancelBtn = ui_pushbutton (W - 16 - 110 - 8 - 96, by, 96, 30, (long) "Annuler", 0);
	ui_dialog_cancel (d, 0);
	ui_w_focus (tk.title);
	int r = (int) ui_dialog_run (d, (long) tk_fn, 0);
	ui_dialog_end (d);
	return r;
}

#endif
