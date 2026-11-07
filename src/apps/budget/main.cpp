//
// budget/main.cpp -- Budget mensuel, le port de l'app Windows de l'utilisateur (C:\Projects\gestion\BudgetApp, C#
// WinForms) en C++ sur UIKit. Memes donnees (SD0:/home/budget/budget.json, le meme JSON que sous Windows), memes
// calculs (model.h), memes commandes ; l'ecran de 800 x 600 range le tout en deux onglets :
//
//   [Mois v] [+ Nouveau mois]  du [23/10/2026] au [23/11/2026]  [Modele...]
//   [ Reste a payer ][ Reste disponible ][ Par jour ][ Ecart vs prevision ]     les quatre indicateurs
//   ( Charges et suivi | Depenses courantes )                                  Ctrl+1, Ctrl+2
//   1 : la grille des charges, ses totaux, le suivi (soldes, jours couverts, Budget initial / Prevision / Reel)
//   2 : la grille des depenses courantes, les totaux par categorie, la moyenne par jour
//
// Tout s'enregistre seul (0,7 s apres la derniere modification, et a la fermeture) ; une copie par jour dans
// SD0:/home/budget/sauvegardes (les 60 dernieres).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include "model.h"
#include "grid.h"
#define TR(s)	((const char *) ui_tr ((long) (s)))

static const char *const DIR_ = "SD0:/home/budget";
static const char *const FILE_ = "SD0:/home/budget/budget.json";
static const char *const BACKUPS = "SD0:/home/budget/sauvegardes";
static const char *const CONF = "SD0:/home/budget/budget.ini";
enum { SAVE_DELAY = 700, MIN_W = 640, MIN_H = 440 };

static inline long sys_call (long n, long a = 0)
{
	register long x0 __asm__ ("x0") = a;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x8) : "memory");
	return x0;
}
static unsigned ticks_ms () { return (unsigned) sys_call (0x19); }
static long local_seconds () { return sys_call (0x1B, 2); }			// SYS_CLOCK_GET : secondes locales

// ---- l'etat ------------------------------------------------------------------------------------------------
static BudgetData g_d;
static int g_cur = -1;				// le mois montre (index dans g_d.months), -1 : aucun
static int g_today;
static bool g_dirty;
static unsigned g_lastChange;
static int g_tab;				// 0 : charges et suivi, 1 : depenses courantes
static std::vector<int> g_order;		// les mois du menu deroulant, par debut
static Month *cur () { return g_cur >= 0 && g_cur < (int) g_d.months.size () ? &g_d.months[(size_t) g_cur] : 0; }

static long g_ddMonth, g_bNew, g_lFrom, g_tStart, g_lTo, g_tEnd, g_bTpl, g_vHero, g_seg, g_lEmpty, g_lStatus;
static long g_lLinesTot, g_lBank, g_tBank, g_lMeal, g_tMeal, g_lCovered, g_nCovered, g_lToday, g_vSum, g_lSpendTot1, g_lSpendTot2;
static int g_ww, g_wh;
static bool g_ddWide = true;		// le menu deroulant des mois montre aussi la periode

static void refresh ();
static void schedule_save ();
static void status (const char *s) { ui_w_set_text (g_lStatus, (long) s); }

// ---- les reglages : largeurs des colonnes ------------------------------------------------------------------
static std::vector<int> g_wLines, g_wSpend, g_wTpl;
static void conf_load ()
{
	FILE *f = fopen (CONF, "r");
	if (!f) return;
	char l[512];
	while (fgets (l, sizeof l, f))
	{
		std::vector<int> *v = !strncmp (l, "lines=", 6) ? &g_wLines : !strncmp (l, "spend=", 6) ? &g_wSpend : !strncmp (l, "model=", 6) ? &g_wTpl : 0;
		if (!v) continue;
		v->clear ();
		for (char *p = l + 6; *p; ) { char *e; long n = strtol (p, &e, 10); if (e == p) break; v->push_back ((int) n); p = e; while (*p == ' ' || *p == ',') p++; }
	}
	fclose (f);
}
static void conf_save ()
{
	mkdir (DIR_, 0755);
	FILE *f = fopen (CONF, "w");
	if (!f) return;
	fprintf (f, "# Budget mensuel : largeurs des colonnes\n");
	const char *names[3] = { "lines", "spend", "model" };
	std::vector<int> *vs[3] = { &g_wLines, &g_wSpend, &g_wTpl };
	for (int i = 0; i < 3; i++) { fprintf (f, "%s=", names[i]); for (size_t k = 0; k < vs[i]->size (); k++) fprintf (f, "%s%d", k ? "," : "", (*vs[i])[k]); fprintf (f, "\n"); }
	fclose (f);
}
static void apply_widths (Grid &g, const std::vector<int> &w) { for (size_t i = 0; i < g.cols.size () && i < w.size (); i++) if (w[i] >= 30 && w[i] <= 600) g.cols[i].w = w[i]; }
static void keep_widths (const Grid &g, std::vector<int> &w) { w.clear (); for (auto &c : g.cols) w.push_back (c.w); conf_save (); }

// ---- l'enregistrement (Storage.cs) -----------------------------------------------------------------------------
static bool line_empty (const Line &l) { return l.label.empty () && !has (l.income) && !has (l.expense) && !has (l.paid) && !l.done; }
static bool copy_file (const char *from, const char *to)
{
	int a = open (from, O_RDONLY);
	if (a < 0) return false;
	int b = open (to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (b < 0) { close (a); return false; }
	static char buf[16384]; ssize_t n; bool ok = true;
	while ((n = read (a, buf, sizeof buf)) > 0) if (write (b, buf, (size_t) n) != n) { ok = false; break; }
	close (a); close (b);
	return ok;
}
static bool save_now ()
{
	g_dirty = false;
	BudgetData d = g_d;				// les lignes vides ne sont pas gardees (comme ReadLines)
	for (Month &m : d.months)
	{
		m.lines.erase (std::remove_if (m.lines.begin (), m.lines.end (), line_empty), m.lines.end ());
		m.spendings.erase (std::remove_if (m.spendings.begin (), m.spendings.end (), [] (const Spending &s) { return s.label.empty () && s.amounts.empty (); }), m.spendings.end ());
	}
	d.tpl.lines.erase (std::remove_if (d.tpl.lines.begin (), d.tpl.lines.end (), line_empty), d.tpl.lines.end ());
	mkdir (DIR_, 0755);
	struct stat st;
	if (stat (FILE_, &st) == 0)
	{
		// une copie de sauvegarde par jour, on garde les 60 dernieres
		mkdir (BACKUPS, 0755);
		int y, mo, dd; ymd_of (g_today, &y, &mo, &dd);
		char b[160]; snprintf (b, sizeof b, "%s/budget-%04d-%02d-%02d.json", BACKUPS, y, mo, dd);
		if (stat (b, &st) != 0) copy_file (FILE_, b);
		std::vector<std::string> olds;
		if (DIR *dir = opendir (BACKUPS))
		{
			while (struct dirent *e = readdir (dir))
			{
				size_t n = strlen (e->d_name);
				if (!strncasecmp (e->d_name, "budget-", 7) && n > 5 && !strcasecmp (e->d_name + n - 5, ".json")) olds.push_back (e->d_name);
			}
			closedir (dir);
		}
		std::sort (olds.begin (), olds.end (), [] (const std::string &a, const std::string &b) { return strcasecmp (a.c_str (), b.c_str ()) > 0; });
		for (size_t i = 60; i < olds.size (); i++) { std::string p = std::string (BACKUPS) + "/" + olds[i]; unlink (p.c_str ()); }
	}
	std::string j = budget_json (d);
	std::string tmp = std::string (FILE_) + ".tmp";
	int f = open (tmp.c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	bool ok = f >= 0 && write (f, j.data (), j.size ()) == (ssize_t) j.size ();
	if (f >= 0) close (f);
	if (ok) ok = rename (tmp.c_str (), FILE_) == 0;
	char msg[200];
	if (ok)
	{
		long s = local_seconds () % 86400;
		snprintf (msg, sizeof msg, TR ("Enregistr\xC3\xA9 \xC3\xA0 %02ld:%02ld:%02ld \xE2\x80\x94 %s"), s / 3600, s / 60 % 60, s % 60, FILE_);
	}
	else snprintf (msg, sizeof msg, TR ("Erreur d'enregistrement : carte pleine ou prot\xC3\xA9g\xC3\xA9" "e (%s)"), FILE_);
	status (msg);
	return ok;
}
static void schedule_save () { g_dirty = true; g_lastChange = ticks_ms (); status (TR ("Modifications en cours\xE2\x80\xA6")); }

// ---- la grille des charges ---------------------------------------------------------------------------------
enum { LC_DONE, LC_DAY, LC_LABEL, LC_INCOME, LC_EXPENSE, LC_PAID, LC_REMAINING, LC_MEAL, LC_BALANCE };
static std::string money_cell (Money v) { return has (v) ? num2 (v) : ""; }
static void lines_changed () { refresh (); schedule_save (); }
static void add_payment ();
static void set_done (bool done);
static void insert_line ();
static void delete_line ();
static void sort_spendings ();
static void insert_spending ();
static void delete_spending ();
// un menu au point (fenetre) : libelles separes par des 0, traduits ; -> rang choisi, -1 : aucun
static int popup (Grid *g, int wx, int wy, const char *labels)
{
	(void) g;
	static char t[1024];
	int k = 0, w = 0;
	for (const char *p = labels; *p; p += strlen (p) + 1)
	{
		const char *s = TR (p);
		int n = (int) strlen (s);
		if (k + n + 2 > (int) sizeof t) break;
		memcpy (t + k, s, (size_t) n + 1);
		k += n + 1;
		int tw = text_w (s) + 48; if (tw > w) w = tw;
	}
	t[k] = 0;
	return (int) ui_popup_local (wx, wy, (long) t, w < 200 ? 200 : w);
}
// une saisie d'une ligne sous un texte de plusieurs lignes (l'InputBox de l'app Windows) -> true : OK
static long g_askField;
static long ask_fn (long dlg, long w, long code) { if (w == g_askField && code == UN_ACTIVATE) ui_dialog_close (dlg, 1); return 0; }
static bool ask (const char *title, const char *text, char *buf, int cap)
{
	const int W = 470, c = ctl_h ();
	// la hauteur du texte : ses lignes, chacune coupee a la largeur (interligne 1,5 de ui_text_wrap)
	int lines = 0;
	for (const char *p = text; ; )
	{
		const char *e = strchr (p, '\n');
		std::string l = e ? std::string (p, (size_t) (e - p)) : std::string (p);
		int w = text_w (l.c_str (), 0, 14);
		lines += w / (W - 60) + 1;
		if (!e) break;
		p = e + 1;
	}
	int th = lines * 21;
	if (th > 320) th = 320;
	int H = 16 + th + 12 + c + 16 + c + 20;
	long dlg = ui_dialog_begin (W, H, (long) title);
	if (!dlg) return false;
	ui_label (20, 14, W - 40 - (FRAME_CAP_W > 60 ? 40 : 0), th + 4, (long) text, ULB_WRAP);
	g_askField = ui_textbox (20, 16 + th + 10, W - 40, c, 0);
	ui_w_set_text (g_askField, (long) buf);
	ui_textbox_select (g_askField, 0, (long) strlen (buf));
	int edge = ui_dialog_button (dlg, 0, (long) TR ("OK"), 1, UB_DEFAULT, W - 20);
	ui_dialog_button (dlg, 1, (long) TR ("Annuler"), 0, 0, edge);
	ui_dialog_cancel (dlg, 0);
	ui_w_focus (g_askField);
	long r = ui_dialog_run (dlg, (long) ask_fn, 0);
	if (r == 1) snprintf (buf, (size_t) cap, "%s", (const char *) ui_w_get_text (g_askField));
	ui_dialog_end (dlg);
	return r == 1;
}

struct LinesModel : GridModel
{
	std::vector<Line> *L () { Month *m = cur (); return m ? &m->lines : 0; }
	int rows () override { auto l = L (); return l ? (int) l->size () : 0; }
	Money balance (int r) { Money run = 0; auto &l = *L (); for (int i = 0; i <= r; i++) run += nz (l[(size_t) i].income) - nz (l[(size_t) i].expense); return run; }
	std::string text (int r, int c) override
	{
		const Line &l = (*L ())[(size_t) r];
		switch (c)
		{
		case LC_DAY: { if (!l.day) return ""; char b[8]; snprintf (b, sizeof b, "%d", l.day); return b; }
		case LC_LABEL: return l.label;
		case LC_INCOME: return money_cell (l.income);
		case LC_EXPENSE: return money_cell (l.expense);
		case LC_PAID: return money_cell (l.paid);
		case LC_REMAINING: return l.remaining () ? num2 (l.remaining ()) : "";
		case LC_BALANCE: return num2 (balance (r));
		}
		return "";
	}
	const char *set (int r, int c, const std::string &t) override
	{
		auto &l = *L ();
		Money v = NUL; int day = 0;
		std::string tt = trim (t);
		if (c == LC_DAY && !try_day (tt, &day)) return TR ("Jour invalide (1 \xC3\xA0 31) \xE2\x80\x94 \xC3\x89" "chap pour annuler");
		bool cumul = c == LC_PAID && !tt.empty () && tt[0] == '+' && r < (int) l.size () && has (l[(size_t) r].paid);
		if (c == LC_INCOME || c == LC_EXPENSE || c == LC_PAID)
			if (!try_amount (cumul ? tt.substr (1) : tt, &v)) return TR ("Montant invalide (ex. 12,50 ou 22*8 ou +75) \xE2\x80\x94 \xC3\x89" "chap pour annuler");
		if (r >= (int) l.size ())
		{
			if (tt.empty ()) return 0;
			l.push_back (Line ());
		}
		Line &x = l[(size_t) r];
		switch (c)
		{
		case LC_DAY: x.day = day; break;
		case LC_LABEL: x.label = tt; break;
		case LC_INCOME: x.income = null_if_zero (v); break;
		case LC_EXPENSE: x.expense = null_if_zero (v); break;
		case LC_PAID:
			x.paid = null_if_zero (cumul ? x.paid + nz (v) : v);
			// paiement qui couvre tout le montant -> coche
			if (x.planned () > 0 && nz (x.paid) >= x.planned ()) x.done = true;
			break;
		}
		lines_changed ();
		return 0;
	}
	bool checked (int r, int c) override { const Line &l = (*L ())[(size_t) r]; return c == LC_DONE ? l.done : l.meal; }
	void toggle (int r, int c) override { Line &l = (*L ())[(size_t) r]; if (c == LC_DONE) l.done = !l.done; else l.meal = !l.meal; lines_changed (); }
	void clear (int r, int c) override
	{
		Line &l = (*L ())[(size_t) r];
		switch (c)
		{
		case LC_DONE: l.done = false; break; case LC_MEAL: l.meal = false; break;
		case LC_DAY: l.day = 0; break; case LC_LABEL: l.label.clear (); break;
		case LC_INCOME: l.income = NUL; break; case LC_EXPENSE: l.expense = NUL; break; case LC_PAID: l.paid = NUL; break;
		default: return;
		}
		lines_changed ();
	}
	void style (int r, int c, CellStyle &s) override
	{
		const Line &l = (*L ())[(size_t) r];
		Money inc = nz (l.income), exp = nz (l.expense), paid = nz (l.paid), rem = l.remaining ();
		bool fullyDone = l.done || (inc + exp > 0 && rem == 0);
		bool partial = !l.done && paid > 0 && rem > 0;
		if (fullyDone) s.fg = C_DIM;
		if (c == LC_REMAINING) s.fg = inc > 0 ? C_GREEN : partial ? C_ORANGE : s.fg;
		if (c == LC_INCOME) s.fg = fullyDone ? C_DIM : C_GREEN;
		if (c == LC_LABEL && rem > 0 && exp > 0) s.bold = true;
		if (c == LC_BALANCE && balance (r) < 0) s.fg = C_RED;
	}
	void insert_row (int r) override { (void) r; insert_line (); }
	void delete_row (int r) override { (void) r; delete_line (); }
	void context (Grid *g, int r, int c, int wx, int wy) override
	{
		(void) r; (void) c;
		static const char items[] = "Enregistrer un paiement\xE2\x80\xA6\0Marquer comme pay\xC3\xA9 totalement\0Marquer comme non pay\xC3\xA9 (remettre \xC3\xA0 z\xC3\xA9ro)\0-\0Ins\xC3\xA9rer une ligne au-dessus\0Supprimer la ligne\0";
		switch (popup (g, wx, wy, items))
		{
		case 0: add_payment (); break;
		case 1: set_done (true); break;
		case 2: set_done (false); break;
		case 4: insert_line (); break;
		case 5: delete_line (); break;
		}
	}
	void message (const char *s) override { if (s && *s) status (s); }
	void widths_changed () override;
	void focus_out (int dir) override { ui_w_focus (dir > 0 ? g_tBank : g_ddMonth); }
};

// ---- la grille des depenses courantes ----------------------------------------------------------------------
static int default_date ()
{
	Month *m = cur ();
	return m && (g_today < m->start || g_today > m->end) ? m->start : g_today;
}
struct SpendModel : GridModel
{
	std::vector<Spending> *S () { Month *m = cur (); return m ? &m->spendings : 0; }
	int rows () override { auto s = S (); return s ? (int) s->size () : 0; }
	std::string text (int r, int c) override
	{
		const Spending &s = (*S ())[(size_t) r];
		if (c == 0) return s.date == NODATE ? "" : fmt_ddd_ddmm (s.date);
		if (c == 1) return s.label;
		return money_cell (s.get (cur ()->categories[(size_t) (c - 2)]));
	}
	std::string edit_text (int r, int c) override
	{
		if (c == 0) { const Spending &s = (*S ())[(size_t) r]; return s.date == NODATE ? "" : fmt_ddmmyyyy (s.date); }
		return text (r, c);
	}
	const char *set (int r, int c, const std::string &t) override
	{
		auto &l = *S ();
		Month *m = cur ();
		std::string tt = trim (t);
		int date = NODATE; Money v = NUL;
		if (c == 0 && !try_date (tt, m, g_today, &date)) return TR ("Date invalide (ex. 7, 7/11 ou 07/11/2026) \xE2\x80\x94 \xC3\x89" "chap pour annuler");
		if (c >= 2 && !try_amount (tt, &v)) return TR ("Montant invalide (ex. 12,50 ou 22*8) \xE2\x80\x94 \xC3\x89" "chap pour annuler");
		if (r >= (int) l.size ())
		{
			if (tt.empty ()) return 0;
			Spending s; s.date = default_date ();
			l.push_back (s);
		}
		Spending &s = l[(size_t) r];
		if (c == 0) s.date = date;
		else if (c == 1) s.label = tt;
		else s.set (m->categories[(size_t) (c - 2)], v);
		lines_changed ();
		return 0;
	}
	void clear (int r, int c) override
	{
		Spending &s = (*S ())[(size_t) r];
		if (c == 0) s.date = NODATE; else if (c == 1) s.label.clear (); else s.set (cur ()->categories[(size_t) (c - 2)], NUL);
		lines_changed ();
	}
	void complete (int c, std::vector<std::string> &out) override
	{
		if (c != 1) return;
		for (auto &m : g_d.months) for (auto &s : m.spendings) if (!s.label.empty ()) out.push_back (s.label);
	}
	void insert_row (int r) override { (void) r; insert_spending (); }
	void delete_row (int r) override { (void) r; delete_spending (); }
	void context (Grid *g, int r, int c, int wx, int wy) override
	{
		(void) r; (void) c;
		static const char items[] = "Ins\xC3\xA9rer une ligne au-dessus\0Supprimer la ligne\0-\0Trier par date\0";
		switch (popup (g, wx, wy, items))
		{
		case 0: insert_spending (); break;
		case 1: delete_spending (); break;
		case 3: sort_spendings (); break;
		}
	}
	void message (const char *s) override { if (s && *s) status (s); }
	void widths_changed () override;
	void focus_out (int dir) override { (void) dir; ui_w_focus (g_ddMonth); }
};

static LinesModel g_mLines;
static SpendModel g_mSpend;
static Grid g_gLines, g_gSpend;
void LinesModel::widths_changed () { keep_widths (g_gLines, g_wLines); }
void SpendModel::widths_changed () { keep_widths (g_gSpend, g_wSpend); }

static void build_lines_cols ()
{
	g_gLines.cols = {
		{ "R\xC3\xA9gl\xC3\xA9", 48, CK_CHECK, false, false, 0 },
		{ "Jour", 42, CK_INT, false, false, 0 },
		{ "Libell\xC3\xA9", 140, CK_TEXT, false, true, 0 },
		{ "Revenu", 82, CK_MONEY, false, false, 0 },
		{ "D\xC3\xA9pense", 82, CK_MONEY, false, false, 0 },
		{ "D\xC3\xA9j\xC3\xA0 pay\xC3\xA9", 84, CK_MONEY, false, false, 0 },
		{ "Reste \xC3\xA0 payer", 98, CK_MONEY, true, false, 0 },
		{ "Ch. repas", 68, CK_CHECK, false, false, 0 },
		{ "Solde", 86, CK_MONEY, true, false, 0 },
	};
	apply_widths (g_gLines, g_wLines);
}
static void build_spend_cols ()
{
	Month *m = cur ();
	g_gSpend.cols = { { "Date", 88, CK_DATE, false, false, 0 }, { "Libell\xC3\xA9", 160, CK_TEXT, false, true, 0 } };
	if (m) for (auto &c : m->categories) g_gSpend.cols.push_back (GCol { c, 92, CK_MONEY, false, false, 0 });
	apply_widths (g_gSpend, g_wSpend);
}

// ---- les commandes des lignes ----------------------------------------------------------------------------
static void add_payment ()
{
	Month *m = cur ();
	Grid &g = g_gLines;
	if (!m || g.curR >= (int) m->lines.size ()) return;
	Line &l = m->lines[(size_t) g.curR];
	Money planned = l.planned (), paid = nz (l.paid);
	char q[512], buf[64] = "";
	snprintf (q, sizeof q, TR ("%s\n\nPr\xC3\xA9vu : %s\nD\xC3\xA9j\xC3\xA0 pay\xC3\xA9 : %s\nReste : %s\n\nMontant pay\xC3\xA9 maintenant :"),
		l.label.c_str (), eur (planned).c_str (), eur (paid).c_str (), eur (planned - paid > 0 ? planned - paid : 0).c_str ());
	if (!ask (TR ("Enregistrer un paiement"), q, buf, sizeof buf)) return;
	Money v;
	if (trim (buf).empty ()) return;
	if (!try_amount (buf, &v) || !has (v)) { ui_message ((long) "Budget", (long) TR ("Montant invalide."), UM_OK); return; }
	l.paid = null_if_zero (paid + v);
	if (planned > 0 && paid + v >= planned) l.done = true;
	lines_changed ();
}
static void set_done (bool done)
{
	Month *m = cur ();
	if (!m || g_gLines.curR >= (int) m->lines.size ()) return;
	Line &l = m->lines[(size_t) g_gLines.curR];
	l.done = done;
	if (!done) l.paid = NUL;
	lines_changed ();
}
static void insert_line ()
{
	Month *m = cur ();
	if (!m) return;
	Grid &g = g_gLines;
	g.cancel_edit ();
	int at = g.curR < (int) m->lines.size () ? g.curR : (int) m->lines.size ();
	m->lines.insert (m->lines.begin () + at, Line ());
	g.curR = at; g.curC = LC_LABEL;
	g.ensure_visible ();
	g.begin_edit (false, 0);
	lines_changed ();
}
static void delete_line ()
{
	Month *m = cur ();
	Grid &g = g_gLines;
	if (!m || g.curR >= (int) m->lines.size ()) return;
	g.cancel_edit ();
	m->lines.erase (m->lines.begin () + g.curR);
	g.clamp ();
	lines_changed ();
}
static void insert_spending ()
{
	Month *m = cur ();
	if (!m) return;
	Grid &g = g_gSpend;
	g.cancel_edit ();
	int at = g.curR < (int) m->spendings.size () ? g.curR : (int) m->spendings.size ();
	Spending s; s.date = default_date ();
	m->spendings.insert (m->spendings.begin () + at, s);
	g.curR = at; g.curC = 1;
	g.ensure_visible ();
	g.begin_edit (false, 0);
	lines_changed ();
}
static void delete_spending ()
{
	Month *m = cur ();
	Grid &g = g_gSpend;
	if (!m || g.curR >= (int) m->spendings.size ()) return;
	g.cancel_edit ();
	m->spendings.erase (m->spendings.begin () + g.curR);
	g.clamp ();
	lines_changed ();
}
static void sort_spendings ()
{
	Month *m = cur ();
	if (!m) return;
	g_gSpend.cancel_edit ();
	std::stable_sort (m->spendings.begin (), m->spendings.end (), [] (const Spending &a, const Spending &b)
	{
		int x = a.date == NODATE ? 0x7FFFFFFF : a.date, y = b.date == NODATE ? 0x7FFFFFFF : b.date;
		return x < y;
	});
	lines_changed ();
}

// ---- les indicateurs et le suivi (vues dessinees) ---------------------------------------------------------------
static Summary g_s;
static long hero_paint (long, long wl, long hl)
{
	int W = (int) wl, H = (int) hl;
	ui_fill (0, 0, W, H, C_BG);
	Month *m = cur ();
	struct Card { const char *cap; std::string val, sub; unsigned col; };
	Card cards[4] = {
		{ "Reste \xC3\xA0 payer ce mois", "\xE2\x80\x94", "", C_TEXT },
		{ "Reste disponible (r\xC3\xA9" "el)", "\xE2\x80\x94", "", C_TEXT },
		{ "Je peux d\xC3\xA9penser par jour", "\xE2\x80\x94", "", C_TEXT },
		{ "\xC3\x89" "cart / pr\xC3\xA9vision", "\xE2\x80\x94", "", C_DIM } };
	if (m)
	{
		const Summary &s = g_s;
		char b[160];
		cards[0].val = eur (s.unpaid);
		if (s.unpaidCount == 0) cards[0].sub = TR ("Tout est pay\xC3\xA9");
		else { snprintf (b, sizeof b, TR ("%d poste(s) pas encore r\xC3\xA9gl\xC3\xA9(s)"), s.unpaidCount); cards[0].sub = b; }
		double rest = s.hasReal ? s.real.rest : s.estimated.rest, perDay = s.hasReal ? s.real.perDay : s.estimated.perDay;
		cards[1].val = eurd (rest); cards[1].col = mof (rest) < 0 ? C_RED : C_TEXT;
		cards[1].sub = s.hasReal ? TR ("Banque + ch. repas \xE2\x88\x92 reste \xC3\xA0 payer") : TR ("Estimation \xE2\x80\x94 saisissez vos soldes");
		cards[2].val = eurd (perDay); cards[2].col = mof (perDay) < 0 ? C_RED : C_GREEN;
		int days = s.remaining - (s.hasReal ? m->coveredDays : 0); if (days < 1) days = 1;
		snprintf (b, sizeof b, TR ("pendant %d jour(s) \xE2\x80\x94 initial : %s/jour"), days, eurd (s.initial.perDay).c_str ()); cards[2].sub = b;
		if (s.hasGap)
		{
			cards[3].val = (mof (s.gap) >= 0 ? "+" : "") + eurd (s.gap);
			cards[3].col = mof (s.gap) >= 0 ? C_GREEN : C_RED;
			cards[3].sub = mof (s.gap) >= 0 ? TR ("En avance sur la pr\xC3\xA9vision") : TR ("En retard sur la pr\xC3\xA9vision");
		}
		else cards[3].sub = TR ("Saisissez vos soldes pour comparer");
	}
	int gap = 8, cw = (W - 3 * gap) / 4;
	for (int i = 0; i < 4; i++)
	{
		int x = i * (cw + gap);
		rbox (x, 0, cw, H, 8, C_LINE);
		rbox (x + 1, 1, cw - 2, H - 2, 7, C_FIELD);
		ui_text_fit ((long) TR (cards[i].cap), x + 12, 19, C_DIM, 12, font_of (0), cw - 20);
		int vp = 22;
		while (vp > 14 && text_w (cards[i].val.c_str (), 1, vp) > cw - 20) vp--;
		ui_text ((long) cards[i].val.c_str (), x + 12, H / 2 + vp / 2 + 1, cards[i].col, vp, font_of (1), 0);
		ui_text_fit ((long) cards[i].sub.c_str (), x + 12, H - 7, C_DIM, 11, font_of (0), cw - 20);
	}
	return 0;
}
static long sum_paint (long, long wl, long hl)
{
	int W = (int) wl, H = (int) hl;
	ui_fill (0, 0, W, H, C_FIELD);
	Month *m = cur ();
	if (!m || !g_s.initial.name) return 0;		// (pas encore calcule)
	const char *heads[7] = { "", "Banque", "Ch. repas", "\xC3\x80 recevoir", "Factures restantes", "Reste global", "Par jour" };
	int rows = 4, rh = H / rows;
	int nameW = W * 19 / 100, fw = text_w (TR ("Factures restantes"), 1) + 14, cw = (W - nameW - fw) / 5;
	int cx[7]; cx[0] = nameW; for (int c = 1; c < 7; c++) cx[c] = cx[c - 1] + (c == 4 ? fw : cw);	// (bord droit de chaque colonne)
	unsigned head = mix (C_FIELD, C_TEXT, 16), grid = mix (C_FIELD, C_TEXT, 28);
	ui_fill (0, 0, W, rh, head);
	for (int c = 1; c < 7; c++)
	{
		const char *t = TR (heads[c]);
		int x = cx[c];
		ui_clip (cx[c - 1], 0, x - cx[c - 1], rh);
		text_at (x - 6 - text_w (t, 1), 0, rh, t, C_TEXT, 1);
		ui_clip (0, 0, 0, 0);
	}
	const SummaryRow *rs[3] = { &g_s.initial, &g_s.estimated, &g_s.real };
	for (int r = 0; r < 3; r++)
	{
		const SummaryRow &s = *rs[r];
		int y = (r + 1) * rh;
		if (r == 2) ui_fill (0, y, W, rh, mix (C_FIELD, C_ACCENT, 26));
		ui_fill (0, y, W, 1, grid);
		text_fit (8, y, nameW - 10, rh, TR (s.name), C_TEXT, 1);
		struct { bool h; double v; bool show; } v[6] = { { s.hBank, s.bank, true }, { s.hMeal, s.meal, true }, { s.hPending, s.pending, r != 0 }, { true, s.bills, true }, { s.hRest, s.rest, true }, { s.hPerDay, s.perDay, true } };
		for (int c = 0; c < 6; c++)
		{
			if (!v[c].show) continue;
			std::string t = v[c].h ? eurd (v[c].v) : "\xE2\x80\x94";
			unsigned col = C_TEXT;
			if (r == 2 && !g_s.hasReal && c < 2) { t = TR ("\xC3\xA0 saisir"); col = C_DIM; }
			bool bold = c >= 4;
			if (bold && v[c].h && mof (v[c].v) < 0) col = C_RED;
			int x = cx[c + 1];
			text_at (x - 6 - text_w (t.c_str (), bold), y, rh, t.c_str (), col, bold);
		}
	}
	rbox (0, 0, W, 1, 0, grid);
	return 0;
}

// ---- l'affichage d'un mois -----------------------------------------------------------------------------------
static std::string month_display (const Month &m) { return g_ddWide ? m.name + "   (" + fmt_ddmm (m.start) + " \xE2\x86\x92 " + fmt_ddmmyyyy (m.end) + ")" : m.name; }
static void fill_month_list ()
{
	g_order.clear ();
	for (int i = 0; i < (int) g_d.months.size (); i++) g_order.push_back (i);
	std::stable_sort (g_order.begin (), g_order.end (), [] (int a, int b) { return g_d.months[(size_t) a].start < g_d.months[(size_t) b].start; });
	ui_list_clear (g_ddMonth);
	int sel = -1;
	for (int k = 0; k < (int) g_order.size (); k++)
	{
		ui_list_add (g_ddMonth, (long) month_display (g_d.months[(size_t) g_order[(size_t) k]]).c_str (), g_order[(size_t) k]);
		if (g_order[(size_t) k] == g_cur) sel = k;
	}
	ui_w_set_value (g_ddMonth, sel);
	ui_w_draw (g_ddMonth);
}
static void lay_out ();
static void field_ok (long t) { ui_w_style (t, UTF_RIGHT); ui_w_draw (t); }
static void load_month (int i)
{
	g_gLines.cancel_edit (); g_gSpend.cancel_edit ();
	g_cur = i;
	g_gLines.reset (); g_gSpend.reset ();
	build_spend_cols ();
	Month *m = cur ();
	ui_w_set_text (g_tStart, (long) (m ? fmt_ddmmyyyy (m->start).c_str () : ""));
	ui_w_set_text (g_tEnd, (long) (m ? fmt_ddmmyyyy (m->end).c_str () : ""));
	ui_w_set_text (g_tBank, (long) (m && has (m->bank) ? num2 (m->bank).c_str () : ""));
	ui_w_set_text (g_tMeal, (long) (m && has (m->mealBalance) ? num2 (m->mealBalance).c_str () : ""));
	field_ok (g_tBank); field_ok (g_tMeal);
	ui_w_set_value (g_nCovered, m ? (m->coveredDays < 0 ? 0 : m->coveredDays > 31 ? 31 : m->coveredDays) : 0);
	ui_w_enable (g_tStart, m != 0); ui_w_enable (g_tEnd, m != 0);
	if (m) g_s = compute (*m, g_today);
	fill_month_list ();
	lay_out ();
	refresh ();
}
// tout ce qui se calcule : indicateurs, totaux, suivi
static void refresh ()
{
	Month *m = cur ();
	char b[400];
	if (m) g_s = compute (*m, g_today);
	if (m)
	{
		snprintf (b, sizeof b, TR ("Revenus : %s    Charges : %s    Solde apr\xC3\xA8s charges : %s    Reste \xC3\xA0 payer : %s"),
			eur (g_s.totalIncome).c_str (), eur (g_s.totalExpense).c_str (), eur (g_s.net).c_str (), eur (g_s.unpaid).c_str ());
		ui_w_set_text (g_lLinesTot, (long) b);
		if (g_today < m->start) snprintf (b, sizeof b, "%s", TR ("La p\xC3\xA9riode n'a pas encore commenc\xC3\xA9."));
		else if (g_today > m->end) snprintf (b, sizeof b, "%s", TR ("Cette p\xC3\xA9riode est termin\xC3\xA9" "e."));
		else snprintf (b, sizeof b, TR ("Aujourd'hui : %s %s \xE2\x80\x94 jour %d sur %d, %d jours restants"), TR (DAY_LONG[weekday (g_today)]), fmt_ddmm (g_today).c_str (), g_s.elapsed, g_s.totalDays, g_s.remaining);
		ui_w_set_text (g_lToday, (long) b);
		std::string parts;
		for (size_t i = 0; i < m->categories.size (); i++)
		{
			Money v = g_s.categoryTotals[i];
			if (!parts.empty ()) parts += "    ";
			if (i == 0 && g_s.mealUsed > 0) { snprintf (b, sizeof b, TR ("%s : %s (dont ch. repas %s)"), m->categories[i].c_str (), eur (v + g_s.mealUsed).c_str (), eur (g_s.mealUsed).c_str ()); parts += b; }
			else parts += m->categories[i] + " : " + eur (v);
		}
		ui_w_set_text (g_lSpendTot1, (long) parts.c_str ());
		snprintf (b, sizeof b, TR ("Total : %s    Moyenne : %s / jour sur %d jour(s)"), eur (g_s.spendingTotal).c_str (), eurd (g_s.spendingPerDay).c_str (), g_s.spendingDays);
		ui_w_set_text (g_lSpendTot2, (long) b);
	}
	ui_view_dirty (g_vHero); ui_view_dirty (g_vSum);
	ui_view_dirty (g_gLines.view); ui_view_dirty (g_gSpend.view);
	ui_view_flush ();
}

// ---- les champs : soldes, periode ----------------------------------------------------------------------------
static bool check_balance (long t, Money *field)
{
	Month *m = cur ();
	if (!m) return true;
	Money v;
	if (!try_amount ((const char *) ui_w_get_text (t), &v))
	{
		ui_w_style (t, UTF_RIGHT | UTF_ERROR); ui_w_draw (t);
		status (TR ("Montant invalide (ex. 155,33 ou 200-44,67)"));
		return false;
	}
	field_ok (t);
	ui_w_set_text (t, (long) (has (v) ? num2 (v).c_str () : ""));
	if (v != *field) { *field = v; refresh (); schedule_save (); }
	return true;
}
static bool check_period (long t)
{
	Month *m = cur ();
	if (!m) return true;
	int d;
	if (!try_date ((const char *) ui_w_get_text (t), 0, g_today, &d) || d == NODATE)
	{
		ui_w_style (t, UTF_ERROR); ui_w_draw (t);
		status (TR ("Date invalide (ex. 23/10/2026)"));
		return false;
	}
	ui_w_style (t, 0);
	int &field = t == g_tStart ? m->start : m->end;
	if (d != field)
	{
		field = d;
		if (m->end <= m->start) m->end = add_months (m->start, 1);
		fill_month_list ();
		refresh (); schedule_save ();
	}
	ui_w_set_text (g_tStart, (long) fmt_ddmmyyyy (m->start).c_str ());
	ui_w_set_text (g_tEnd, (long) fmt_ddmmyyyy (m->end).c_str ());
	return true;
}
static bool check_field (long t)
{
	Month *m = cur ();
	if (!m) return true;
	if (t == g_tBank) return check_balance (t, &m->bank);
	if (t == g_tMeal) return check_balance (t, &m->mealBalance);
	if (t == g_tStart || t == g_tEnd) return check_period (t);
	return true;
}

// ---- les commandes des mois ---------------------------------------------------------------------------------
static int last_month ()
{
	int last = -1;
	for (int i = 0; i < (int) g_d.months.size (); i++) if (last < 0 || g_d.months[(size_t) i].start >= g_d.months[(size_t) last].start) last = i;
	return last;
}
static void cmd_new_month ()
{
	if (g_d.tpl.lines.empty () && ui_message ((long) TR ("Nouveau mois"), (long) TR ("Le mod\xC3\xA8le est vide. Cr\xC3\xA9" "er quand m\xC3\xAAme un mois vide ?\n\n(Conseil : remplissez d'abord le \xC2\xAB Mod\xC3\xA8le \xC2\xBB)"), UM_YESNO) != 1)
		return;
	g_d.months.push_back (new_month (g_d, g_today));
	load_month ((int) g_d.months.size () - 1);
	schedule_save ();
}
static void cmd_delete_month ()
{
	Month *m = cur ();
	if (!m) return;
	char q[300];
	snprintf (q, sizeof q, TR ("Supprimer \xC2\xAB %s \xC2\xBB et toutes ses donn\xC3\xA9" "es ?\n\n(Une copie de sauvegarde du jour est conserv\xC3\xA9" "e dans le dossier des donn\xC3\xA9" "es.)"), m->name.c_str ());
	if (ui_message ((long) TR ("Supprimer le mois"), (long) q, UM_YESNO) != 1) return;
	g_d.months.erase (g_d.months.begin () + g_cur);
	load_month (last_month ());
	schedule_save ();
}
static void cmd_month_to_template ()
{
	Month *m = cur ();
	if (!m) return;
	if (ui_message ((long) TR ("Mod\xC3\xA8le"), (long) TR ("Remplacer le mod\xC3\xA8le par les postes et cat\xC3\xA9gories de ce mois ?\n\nLes mois suivants partiront de cette liste. Les montants pay\xC3\xA9s ne sont pas copi\xC3\xA9s."), UM_YESNO) != 1)
		return;
	g_d.tpl.lines.clear ();
	for (const Line &l : m->lines) { if (line_empty (l)) continue; Line c = l; c.done = false; c.paid = NUL; g_d.tpl.lines.push_back (c); }
	g_d.tpl.categories = m->categories;
	schedule_save ();
	status (TR ("Le mod\xC3\xA8le reprend ce mois."));
}
static void cmd_categories ()
{
	Month *m = cur ();
	if (!m) return;
	char buf[400]; snprintf (buf, sizeof buf, "%s", join_categories (m->categories).c_str ());
	if (!ask (TR ("Cat\xC3\xA9gories"), TR ("Colonnes des d\xC3\xA9penses courantes pour ce mois, s\xC3\xA9par\xC3\xA9" "es par des \xC2\xAB ; \xC2\xBB.\n\nRenommer une cat\xC3\xA9gorie garde ses montants (on se base sur la position)."), buf, sizeof buf)) return;
	std::vector<std::string> cats = split_categories (buf);
	if (cats.empty ()) return;
	g_gSpend.cancel_edit ();
	set_categories (*m, cats);
	build_spend_cols ();
	g_gSpend.clamp ();
	lay_out ();
	refresh ();
	schedule_save ();
}
static void cmd_prev_next (int d)
{
	if (g_order.empty ()) return;
	int k = 0;
	for (int i = 0; i < (int) g_order.size (); i++) if (g_order[(size_t) i] == g_cur) k = i;
	k += d;
	if (k < 0 || k >= (int) g_order.size ()) return;
	load_month (g_order[(size_t) k]);
}

// ---- le modele (TemplateForm.cs) : un dialogue -------------------------------------------------------------------
enum { TC_DAY, TC_LABEL, TC_INCOME, TC_EXPENSE, TC_MEAL };
static long g_tplTotals;
static void tpl_totals ();
struct TplModel : GridModel
{
	std::vector<Line> lines;
	int rows () override { return (int) lines.size (); }
	std::string text (int r, int c) override
	{
		const Line &l = lines[(size_t) r];
		switch (c)
		{
		case TC_DAY: { if (!l.day) return ""; char b[8]; snprintf (b, sizeof b, "%d", l.day); return b; }
		case TC_LABEL: return l.label;
		case TC_INCOME: return money_cell (l.income);
		case TC_EXPENSE: return money_cell (l.expense);
		}
		return "";
	}
	const char *set (int r, int c, const std::string &t) override
	{
		std::string tt = trim (t);
		Money v = NUL; int day = 0;
		if (c == TC_DAY && !try_day (tt, &day)) return TR ("Jour invalide (1 \xC3\xA0 31) \xE2\x80\x94 \xC3\x89" "chap pour annuler");
		if ((c == TC_INCOME || c == TC_EXPENSE) && !try_amount (tt, &v)) return TR ("Montant invalide (ex. 12,50 ou 22*8) \xE2\x80\x94 \xC3\x89" "chap pour annuler");
		if (r >= (int) lines.size ()) { if (tt.empty ()) return 0; lines.push_back (Line ()); }
		Line &l = lines[(size_t) r];
		if (c == TC_DAY) l.day = day; else if (c == TC_LABEL) l.label = tt; else if (c == TC_INCOME) l.income = null_if_zero (v); else if (c == TC_EXPENSE) l.expense = null_if_zero (v);
		tpl_totals ();
		return 0;
	}
	bool checked (int r, int c) override { (void) c; return lines[(size_t) r].meal; }
	void toggle (int r, int c) override { (void) c; lines[(size_t) r].meal = !lines[(size_t) r].meal; }
	void clear (int r, int c) override
	{
		Line &l = lines[(size_t) r];
		if (c == TC_DAY) l.day = 0; else if (c == TC_LABEL) l.label.clear (); else if (c == TC_INCOME) l.income = NUL; else if (c == TC_EXPENSE) l.expense = NUL; else l.meal = false;
		tpl_totals ();
	}
	void insert_row (int r) override;
	void delete_row (int r) override { if (r < (int) lines.size ()) { lines.erase (lines.begin () + r); tpl_totals (); } }
	void context (Grid *g, int r, int c, int wx, int wy) override
	{
		(void) c;
		static const char items[] = "Ins\xC3\xA9rer une ligne au-dessus\0Supprimer la ligne\0";
		int k = popup (g, wx, wy, items);
		if (k == 0) insert_row (r);
		else if (k == 1) { delete_row (r); g->clamp (); g->dirty (); }
	}
	void message (const char *s) override;
	void widths_changed () override;
};
static TplModel g_mTpl;
static Grid g_gTpl;
static long g_tplMsg;
void TplModel::insert_row (int r)
{
	g_gTpl.cancel_edit ();
	int at = r < (int) lines.size () ? r : (int) lines.size ();
	lines.insert (lines.begin () + at, Line ());
	g_gTpl.curR = at; g_gTpl.curC = TC_LABEL;
	g_gTpl.ensure_visible ();
	g_gTpl.begin_edit (false, 0);
}
void TplModel::message (const char *s) { ui_w_set_text (g_tplMsg, (long) (s && *s ? s : " ")); }
void TplModel::widths_changed () { keep_widths (g_gTpl, g_wTpl); }
static void tpl_totals ()
{
	Money inc = 0, exp = 0;
	for (auto &l : g_mTpl.lines) { inc += nz (l.income); exp += nz (l.expense); }
	char b[300];
	snprintf (b, sizeof b, TR ("Revenus : %s    Charges : %s    Reste pour le mois : %s    (\xE2\x89\x88 %s / jour)"), eur (inc).c_str (), eur (exp).c_str (), eur (inc - exp).c_str (), eurd (mdbl (inc - exp) / 30).c_str ());
	ui_w_set_text (g_tplTotals, (long) b);
}
static long tpl_fn (long) { ui_view_flush (); return 0; }
static long tpl_tick (long) { ui_view_flush (); return 0; }
static void cmd_template ()
{
	g_gLines.cancel_edit (); g_gSpend.cancel_edit ();
	const int W = 780, H = 500, c = ctl_h ();
	long dlg = ui_dialog_begin (W, H, (long) TR ("Mod\xC3\xA8le mensuel"));
	if (!dlg) return;
	int y = 12;
	{ const char *hint = TR ("Ce mod\xC3\xA8le est recopi\xC3\xA9 \xC3\xA0 chaque \xC2\xAB Nouveau mois \xC2\xBB. Le modifier ne change pas les mois d\xC3\xA9j\xC3\xA0 cr\xC3\xA9\xC3\xA9s ; chaque mois peut ensuite \xC3\xAAtre adapt\xC3\xA9 librement. Montants : un calcul est permis, ex. 22*8."); int hh = (int) ui_text_wrap ((long) hint, 0, 0, W - 32 - FRAME_CAP_W, 0, (long) ui.font, 14, 1); ui_label (16, y, W - 32 - FRAME_CAP_W, hh + 4, (long) hint, ULB_WRAP | ULB_DIM); y += hh + 10; }
	int l1 = text_w (TR ("Le mois commence le jour :")) + 28, l2 = text_w (TR ("Catégories (séparé" "es par ;) :")) + 28;
	ui_label (16, y, l1, c, (long) TR ("Le mois commence le jour :"), 0);
	long num = ui_numeric (16 + l1, y, 116, c, 1, 31, g_d.tpl.startDay < 1 ? 1 : g_d.tpl.startDay > 31 ? 31 : g_d.tpl.startDay);
	ui_label (16 + l1 + 132, y, l2, c, (long) TR ("Cat\xC3\xA9gories (s\xC3\xA9par\xC3\xA9" "es par ;) :"), 0);
	long cats = ui_textbox (16 + l1 + 132 + l2, y, W - 32 - (l1 + 132 + l2), c, 0);
	ui_w_set_text (cats, (long) join_categories (g_d.tpl.categories).c_str ());
	y += c + 10;
	int gh = H - y - c - 70;
	g_mTpl.lines = g_d.tpl.lines;
	g_gTpl = Grid ();
	g_gTpl.cols = {
		{ "Jour", 50, CK_INT, false, false, 0 },
		{ "Libell\xC3\xA9", 240, CK_TEXT, false, true, 0 },
		{ "Revenu", 110, CK_MONEY, false, false, 0 },
		{ "D\xC3\xA9pense", 110, CK_MONEY, false, false, 0 },
		{ "Ch. repas", 76, CK_CHECK, false, false, 0 } };
	apply_widths (g_gTpl, g_wTpl);
	grid_create (g_gTpl, &g_mTpl);
	g_gTpl.place (16, y, W - 32, gh);
	y += gh + 6;
	g_tplTotals = ui_label (16, y, W - 32, 22, (long) " ", ULB_BOLD);
	g_tplMsg = ui_label (16, y + 22, W - 32, 20, (long) " ", ULB_DIM);
	tpl_totals ();
	int edge = ui_dialog_button (dlg, 0, (long) TR ("Enregistrer le mod\xC3\xA8le"), 1, 0, W - 16);
	ui_dialog_button (dlg, 1, (long) TR ("Annuler"), 0, 0, edge);
	ui_dialog_cancel (dlg, 0);
	ui_dialog_timer (dlg, (long) tpl_tick, 100);
	ui_w_focus (g_gTpl.view);
	ui_view_dirty (g_gTpl.view);
	long r = ui_dialog_run (dlg, (long) tpl_fn, 0);
	if (r == 1)
	{
		g_gTpl.commit ();
		g_d.tpl.startDay = (int) ui_w_get_value (num);
		std::vector<std::string> cs = split_categories ((const char *) ui_w_get_text (cats));
		if (!cs.empty ()) g_d.tpl.categories = cs;
		g_d.tpl.lines.clear ();
		for (auto &l : g_mTpl.lines) if (!(l.label.empty () && !has (l.income) && !has (l.expense))) g_d.tpl.lines.push_back (l);
		schedule_save ();
	}
	ui_dialog_end (dlg);
	g_gTpl.view = 0;
	for (int i = 0; i < g_ngrids; i++) if (g_grids[i] == &g_gTpl) { g_grids[i] = g_grids[--g_ngrids]; break; }
	ui_w_focus (g_tab == 0 ? g_gLines.view : g_gSpend.view);
	refresh ();
}

// ---- les menus ------------------------------------------------------------------------------------------------
enum { M_NEW = 1, M_DELMONTH, M_PREV, M_NEXT, M_TPL, M_TOTPL, M_CATS, M_PERIOD, M_FOLDER, M_QUIT,
       M_PAY, M_DONE, M_UNDONE, M_INSERT, M_DELETE, M_SORT, M_TAB1, M_TAB2, M_BANK, M_GRID };
static void build_menu ()
{
	static char t[2048];
	snprintf (t, sizeof t,
		"MFichier\n" "I%d\tDossier des donn\xC3\xA9" "es\n" "-\n" "I%d\tQuitter\n"
		"MMois\n" "I%d\tNouveau mois\tCtrl+N\n" "I%d\tSupprimer ce mois\n" "-\n" "I%d\tMois pr\xC3\xA9" "c\xC3\xA9" "dent (Ctrl+PgPr\xC3\xA9" "c)\n" "I%d\tMois suivant (Ctrl+PgSuiv)\n" "-\n"
		"I%d\tP\xC3\xA9riode du mois\n" "I%d\tCat\xC3\xA9gories\xE2\x80\xA6\n" "-\n" "I%d\tMod\xC3\xA8le\xE2\x80\xA6\tCtrl+M\n" "I%d\tCopier ce mois dans le mod\xC3\xA8le\n"
		"MLignes\n" "I%d\tEnregistrer un paiement\xE2\x80\xA6\tCtrl+P\n" "I%d\tMarquer comme pay\xC3\xA9 totalement\tCtrl+D\n" "I%d\tMarquer comme non pay\xC3\xA9\tCtrl+U\n" "-\n"
		"I%d\tIns\xC3\xA9rer une ligne (Inser)\n" "I%d\tSupprimer la ligne (Ctrl+Suppr)\n" "-\n" "I%d\tTrier les d\xC3\xA9penses par date\n"
		"MPr\xC3\xA9sentation\n" "I%d\tCharges et suivi\tCtrl+1\n" "I%d\tD\xC3\xA9penses courantes\tCtrl+2\n" "-\n" "I%d\tSoldes du suivi\tCtrl+B\n" "I%d\tRetour au tableau\tCtrl+G\n",
		M_FOLDER, M_QUIT, M_NEW, M_DELMONTH, M_PREV, M_NEXT, M_PERIOD, M_CATS, M_TPL, M_TOTPL,
		M_PAY, M_DONE, M_UNDONE, M_INSERT, M_DELETE, M_SORT, M_TAB1, M_TAB2, M_BANK, M_GRID);
	ui_menu ((long) t);
}
static void set_tab (int t)
{
	g_gLines.cancel_edit (); g_gSpend.cancel_edit ();
	g_tab = t;
	ui_w_set_value (g_seg, t);
	lay_out ();
	ui_w_focus (t == 0 ? g_gLines.view : g_gSpend.view);
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	ui_w_draw (0);
	refresh ();
	ui_update_all ();
}
static bool command (int id)
{
	Grid &g = g_tab == 0 ? g_gLines : g_gSpend;
	switch (id)
	{
	case M_QUIT: return false;
	case M_FOLDER: mkdir (DIR_, 0755); ui_assoc_open ((long) DIR_); break;
	case M_NEW: cmd_new_month (); break;
	case M_DELMONTH: cmd_delete_month (); break;
	case M_PREV: cmd_prev_next (-1); break;
	case M_NEXT: cmd_prev_next (1); break;
	case M_PERIOD: ui_w_focus (g_tStart); ui_textbox_select (g_tStart, 0, ui_w_text_len (g_tStart)); ui_w_draw (g_tStart); break;
	case M_CATS: cmd_categories (); break;
	case M_TPL: cmd_template (); break;
	case M_TOTPL: cmd_month_to_template (); break;
	case M_PAY: if (g_tab != 0) set_tab (0); add_payment (); break;
	case M_DONE: if (g_tab != 0) set_tab (0); set_done (true); break;
	case M_UNDONE: if (g_tab != 0) set_tab (0); set_done (false); break;
	case M_INSERT: if (g_tab == 0) insert_line (); else insert_spending (); break;
	case M_DELETE: (void) g; if (g_tab == 0) delete_line (); else delete_spending (); break;
	case M_SORT: if (g_tab != 1) set_tab (1); sort_spendings (); break;
	case M_TAB1: set_tab (0); break;
	case M_TAB2: set_tab (1); break;
	case M_BANK: if (g_tab != 0) set_tab (0); ui_w_focus (g_tBank); ui_textbox_select (g_tBank, 0, ui_w_text_len (g_tBank)); ui_w_draw (g_tBank); break;
	case M_GRID: ui_w_focus (g_tab == 0 ? g_gLines.view : g_gSpend.view); g.dirty (); break;
	}
	return true;
}

// ---- la fenetre -------------------------------------------------------------------------------------------------
static void lay_out ()
{
	int W = g_ww, H = g_wh, c = ctl_h (), m = 10;
	int y = 8, x = m;
	// la barre du haut, hors des boutons du cadre (FRAME_CAP_W) ; le menu deroulant prend ce qui reste
	int bNewW = text_w (TR ("+ Nouveau mois"), 1) + 28, bTplW = text_w (TR ("Mod\xC3\xA8le\xE2\x80\xA6"), 1) + 28, dateW = text_w ("00/00/0000") + 22;
	int fromW = text_w (TR ("du")) + 8, toW = text_w (TR ("au")) + 8, gap = 6;
	int avail = W - 2 * m - FRAME_CAP_W;
	int ddW = avail - (bNewW + fromW + dateW + toW + dateW + bTplW + 6 * gap);
	if (ddW > 320) ddW = 320;
	if (ddW < 150) ddW = 150;
	if (g_ddWide != (ddW >= 270)) { g_ddWide = ddW >= 270; fill_month_list (); }
	ui_w_move (g_ddMonth, x, y, ddW, c); x += ddW + gap;
	ui_w_move (g_bNew, x, y, bNewW, c); x += bNewW + gap;
	ui_w_move (g_lFrom, x, y, fromW, c); x += fromW;
	ui_w_move (g_tStart, x, y, dateW, c); x += dateW + gap;
	ui_w_move (g_lTo, x, y, toW, c); x += toW;
	ui_w_move (g_tEnd, x, y, dateW, c); x += dateW + gap;
	ui_w_move (g_bTpl, x, y, bTplW, c);
	ui_w_show (g_bTpl, x + bTplW <= W - m - FRAME_CAP_W + 4);
	y += c + 8;
	int heroH = 70;
	ui_w_move (g_vHero, m, y, W - 2 * m, heroH); y += heroH + 8;
	int segW = text_w (TR ("Charges et suivi"), 1) + text_w (TR ("D\xC3\xA9penses courantes"), 1) + 64;
	ui_w_move (g_seg, m, y, segW, c);
	ui_w_move (g_lToday, m + segW + 14, y, W - 2 * m - segW - 14, c);
	y += c + 6;
	int stH = 20;
	ui_w_move (g_lStatus, m, H - stH - 2, W - 2 * m, stH);
	int bottom = H - stH - 4;
	bool any = cur () != 0;
	ui_w_show (g_lEmpty, !any);
	ui_w_move (g_lEmpty, m, y + 20, W - 2 * m, 120);
	// onglet 1 : charges et suivi
	bool t1 = any && g_tab == 0, t2 = any && g_tab == 1;
	int sumH = 4 * 24;
	int inY = bottom - sumH - 6 - c;
	int totY = inY - 26;
	g_gLines.place (m, y, W - 2 * m, totY - y - 2);
	ui_w_move (g_lLinesTot, m, totY + 2, W - 2 * m, 20);
	int ix = m;
	int lb = text_w (TR ("Solde banque :"), 1) + 8, lm = text_w (TR ("Ch. repas :"), 1) + 8, lc = text_w (TR ("Jours couverts :")) + 8;
	ui_w_move (g_lBank, ix, inY, lb, c); ix += lb;
	ui_w_move (g_tBank, ix, inY, 104, c); ix += 104 + 14;
	ui_w_move (g_lMeal, ix, inY, lm, c); ix += lm;
	ui_w_move (g_tMeal, ix, inY, 96, c); ix += 96 + 14;
	ui_w_move (g_lCovered, ix, inY, lc, c); ix += lc;
	ui_w_move (g_nCovered, ix, inY, 116, c);
	ui_w_move (g_vSum, m, bottom - sumH, W - 2 * m, sumH);
	ui_w_show (g_lToday, any);
	long w1[] = { g_gLines.view, g_lLinesTot, g_lBank, g_tBank, g_lMeal, g_tMeal, g_lCovered, g_nCovered, g_vSum };
	for (long w : w1) ui_w_show (w, t1);
	// onglet 2 : depenses courantes
	g_gSpend.place (m, y, W - 2 * m, bottom - 44 - y);
	ui_w_move (g_lSpendTot1, m, bottom - 42, W - 2 * m, 20);
	ui_w_move (g_lSpendTot2, m, bottom - 21, W - 2 * m, 20);
	long w2[] = { g_gSpend.view, g_lSpendTot1, g_lSpendTot2 };
	for (long w : w2) ui_w_show (w, t2);
	ui_w_show (g_seg, any);
}
static void repaint ()
{
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	lay_out ();
	ui_w_draw (0);
	refresh ();
	ui_update_all ();
}
static void on_resize (long w, long h) { g_ww = (int) w; g_wh = (int) h; repaint (); }

static long g_lastFocus;
static void control (long w, long code)
{
	if (w == g_bNew && code == UN_ACTIVATE) cmd_new_month ();
	else if (w == g_bTpl && code == UN_ACTIVATE) cmd_template ();
	else if (w == g_ddMonth && code == UN_CHANGE)
	{
		long k = ui_w_get_value (g_ddMonth);
		if (k >= 0 && k < (long) g_order.size () && g_order[(size_t) k] != g_cur) load_month (g_order[(size_t) k]);
	}
	else if (w == g_seg && code == UN_CHANGE) set_tab ((int) ui_w_get_value (g_seg));
	else if (w == g_nCovered && code == UN_CHANGE)
	{
		Month *m = cur ();
		if (m) { m->coveredDays = (int) ui_w_get_value (g_nCovered); refresh (); schedule_save (); }
	}
	else if ((w == g_tBank || w == g_tMeal || w == g_tStart || w == g_tEnd) && code == UN_ACTIVATE)
	{
		if (check_field (w))
		{
			// Entree : au champ suivant, comme une saisie de formulaire
			if (w == g_tBank) { ui_w_focus (g_tMeal); ui_textbox_select (g_tMeal, 0, ui_w_text_len (g_tMeal)); }
			else if (w == g_tMeal) ui_w_focus (g_nCovered);
			else if (w == g_tStart) { ui_w_focus (g_tEnd); ui_textbox_select (g_tEnd, 0, ui_w_text_len (g_tEnd)); }
			else ui_w_focus (g_gLines.view);
			g_lastFocus = ui_w_get_focus ();
		}
	}
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/budget.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	s_jet = (const unsigned *) ui_jetons ();
	g_today = (int) (local_seconds () / 86400);
	const char *wantMonth = 0;
	// --today AAAA-MM-JJ (essais), --month "Novembre 2026" : le mois montre au depart
	for (int i = 1; i + 1 < argc; i++)
	{
		int y, mo, d;
		if (!strcmp (argv[i], "--today") && sscanf (argv[i + 1], "%d-%d-%d", &y, &mo, &d) == 3) g_today = day_number (y, mo, d);
		if (!strcmp (argv[i], "--month")) wantMonth = argv[i + 1];
	}
	// les donnees : un fichier illisible n'est jamais touche
	{
		std::string t;
		FILE *f = fopen (FILE_, "rb");
		if (f)
		{
			char b[8192]; size_t n;
			while ((n = fread (b, 1, sizeof b, f)) > 0) t.append (b, n);
			fclose (f);
			std::string err;
			if (!budget_parse (t.data (), t.size (), g_d, &err))
			{
				ui_window_ex (-1, -1, 420, 200, (long) "Budget mensuel", 0);
				char m[600];
				snprintf (m, sizeof m, TR ("Impossible de lire le fichier de donn\xC3\xA9" "es :\n%s\n\n%s\n\nLe fichier n'a pas \xC3\xA9t\xC3\xA9 modifi\xC3\xA9. Vous pouvez restaurer une copie depuis le dossier \xC2\xAB sauvegardes \xC2\xBB."), FILE_, err.c_str ());
				ui_message ((long) "Budget", (long) m, UM_OK);
				return 1;
			}
		}
	}
	conf_load ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int w = (int) scr[0] - 16, h = (int) (scr[1] - scr[2] - scr[3] - 16);
	if (w > 1100) w = 1100;
	if (h > 760) h = 760;
	if (ui_window_ex (-1, -1, w, h, (long) "Budget mensuel", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (MIN_W, MIN_H);
	if (scr[0] <= 1024) ui_maximize ();			// (le PocketTerm : 800 x 600, toute la place)
	g_ww = (int) ui.w; g_wh = (int) ui.h;

	int c = ctl_h ();
	g_ddMonth = ui_dropdown (0, 0, 236, c, (long) "");
	g_bNew = ui_pushbutton (0, 0, 128, c, (long) TR ("+ Nouveau mois"), UB_DEFAULT);
	g_lFrom = ui_label (0, 0, 22, c, (long) TR ("du"), ULB_DIM);
	g_tStart = ui_textbox (0, 0, 96, c, 0);
	g_lTo = ui_label (0, 0, 20, c, (long) TR ("au"), ULB_DIM);
	g_tEnd = ui_textbox (0, 0, 96, c, 0);
	g_bTpl = ui_pushbutton (0, 0, 120, c, (long) TR ("Mod\xC3\xA8le\xE2\x80\xA6"), 0);
	g_vHero = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vHero, (long) hero_paint, 0, 0, 0);
	g_seg = ui_segmented (0, 0, 360, c, (long) TR ("Charges et suivi|D\xC3\xA9penses courantes"));
	g_lEmpty = ui_label (0, 0, 10, 10, (long) TR ("Aucun mois pour l'instant.\n\nRemplissez d'abord le \xC2\xAB Mod\xC3\xA8le \xC2\xBB (revenus et charges r\xC3\xA9" "currents), puis \xC2\xAB + Nouveau mois \xC2\xBB (Ctrl+N)."), ULB_CENTER | ULB_WRAP | ULB_DIM);
	build_lines_cols ();
	grid_create (g_gLines, &g_mLines);
	g_lLinesTot = ui_label (0, 0, 10, 20, (long) " ", ULB_BOLD);
	g_lBank = ui_label (0, 0, 98, c, (long) TR ("Solde banque :"), ULB_BOLD);
	g_tBank = ui_textbox (0, 0, 100, c, UTF_RIGHT);
	g_lMeal = ui_label (0, 0, 72, c, (long) TR ("Ch. repas :"), ULB_BOLD);
	g_tMeal = ui_textbox (0, 0, 92, c, UTF_RIGHT);
	g_lCovered = ui_label (0, 0, 108, c, (long) TR ("Jours couverts :"), 0);
	g_nCovered = ui_numeric (0, 0, 84, c, 0, 31, 0);
	g_lToday = ui_label (0, 0, 10, c, (long) " ", ULB_DIM);
	g_vSum = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vSum, (long) sum_paint, 0, 0, 0);
	grid_create (g_gSpend, &g_mSpend);
	g_lSpendTot1 = ui_label (0, 0, 10, 20, (long) " ", ULB_BOLD);
	g_lSpendTot2 = ui_label (0, 0, 10, 20, (long) " ", ULB_BOLD);
	g_lStatus = ui_label (0, 0, 10, 20, (long) " ", ULB_DIM);
	ui_w_tip (g_tBank, (long) TR ("Ce que vous avez r\xC3\xA9" "ellement sur le compte aujourd'hui"));
	ui_w_tip (g_nCovered, (long) TR ("Jours d\xC3\xA9j\xC3\xA0 couverts par des courses faites \xC3\xA0 l'avance"));
	ui_on_resize ((long) on_resize, 0);
	build_menu ();

	// le mois de depart : le dernier, comme sous Windows (ou celui de --month)
	int start = last_month ();
	if (wantMonth) for (int i = 0; i < (int) g_d.months.size (); i++) if (!strncasecmp (g_d.months[(size_t) i].name.c_str (), wantMonth, strlen (wantMonth))) { start = i; break; }
	load_month (start);
	{ char b[200]; snprintf (b, sizeof b, TR ("Donn\xC3\xA9" "es : %s"), FILE_); status (b); }
	ui_w_focus (cur () ? g_gLines.view : g_bNew);
	repaint ();
	g_lastFocus = ui_w_get_focus ();

	bool run = true;
	while (run)
	{
		long t = ui_event (g_dirty ? 100 : 300);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) run = command ((int) ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w2 = uikit_w_event (&code, &taken);
			if (code) control (w2, code);
			if (press && !taken)
			{
				int hid = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
				long id = ui_menu_key (0);
				if (id >= 0) run = command ((int) id);
				else if (mods & UI_MOD_CTRL)
				{
					if (hid == K_PGUP) cmd_prev_next (-1);
					else if (hid == K_PGDN) cmd_prev_next (1);
					else if (hid == 0x1E) set_tab (0);		// (Ctrl+1, Ctrl+2 : au cas ou le menu ne les prend pas)
					else if (hid == 0x1F) set_tab (1);
				}
			}
			// un champ quitte : sa valeur relue (refusee : il garde le focus)
			long f = ui_w_get_focus ();
			if (f != g_lastFocus)
			{
				long was = g_lastFocus;
				g_lastFocus = f;
				if ((was == g_tBank || was == g_tMeal || was == g_tStart || was == g_tEnd) && !check_field (was)) { ui_w_focus (was); g_lastFocus = was; }
				ui_view_dirty (g_gLines.view); ui_view_dirty (g_gSpend.view);
			}
		}
		else ui_w_idle ();
		if (g_dirty && ticks_ms () - g_lastChange >= SAVE_DELAY) save_now ();
		ui_view_flush ();
	}
	g_gLines.commit (); g_gSpend.commit ();
	long f = ui_w_get_focus ();
	if (f == g_tBank || f == g_tMeal || f == g_tStart || f == g_tEnd) check_field (f);
	if (g_dirty) save_now ();
	return 0;
}
