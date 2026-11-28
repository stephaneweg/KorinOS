// downloads.h -- la fenêtre des téléchargements de Jet (27/11/2026, demande de l'utilisateur) : ceux en cours (avec leur
// avancée) et l'historique (HOME:/jet/downloads.txt), un bouton pour ouvrir le fichier (son app, ui_assoc_open) ou son
// dossier (Fichiers), annuler un téléchargement en cours, retirer une ligne de l'historique ou tout l'historique.
// Elle s'ouvre d'elle-même quand un téléchargement commence ; la fermer n'arrête rien (les téléchargements sont ceux du
// moteur, dans le process de l'interface) ; menu « Téléchargements » (Ctrl+J). Une seconde fenêtre d'UIKit dans le même
// process : ses événements (EV_WIN) passent par dl_event, jamais par ceux de la page.
#pragma once

#include "engine.h"
#include "present.h"
#include <uikit.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <string>
#include <vector>

enum { DL_RUN, DL_DONE, DL_FAIL, DL_CANCEL, DL_LOST };

struct DlItem
{
	int id;			// celui du moteur (0 : de l'historique)
	std::string path, why;	// le fichier ; pourquoi il a échoué
	long long done, total;
	int state;
	long when;		// le début (time)
};

static std::vector<DlItem> g_dl;	// le plus ancien d'abord
static long g_dlWin, g_dlList, g_dlOpen, g_dlFolder, g_dlCancel, g_dlRemove, g_dlClear;
static long g_dlMain;			// la fenêtre de Jet (dl_init)
static std::string g_dlFile;		// HOME:/jet/downloads.txt

static std::string dl_name (const std::string &p)
{
	size_t s = p.rfind ('/');
	return s == std::string::npos ? p : p.substr (s + 1);
}

static std::string dl_size (long long n)
{
	char b[32];
	if (n < 1024) snprintf (b, sizeof b, "%lld o", n);
	else if (n < 1024 * 1024) snprintf (b, sizeof b, "%.1f Ko", n / 1024.0);
	else if (n < 1024LL * 1024 * 1024) snprintf (b, sizeof b, "%.1f Mo", n / 1048576.0);
	else snprintf (b, sizeof b, "%.2f Go", n / 1073741824.0);
	std::string s = b;
	for (char &c : s) if (c == '.') c = ',';
	return s;
}

// une ligne de la liste : nom, état, date (colonnes séparées par des tabulations)
static std::string dl_row (const DlItem &d)
{
	std::string st;
	if (d.state == DL_RUN)
	{
		if (d.total > 0)
		{
			char b[16];
			snprintf (b, sizeof b, "%d %%", (int) (d.done * 100 / d.total));
			st = std::string (b) + " — " + dl_size (d.done) + " sur " + dl_size (d.total);
		}
		else st = "En cours — " + dl_size (d.done);
	}
	else if (d.state == DL_DONE) st = "Terminé — " + dl_size (d.total ? d.total : d.done);
	else if (d.state == DL_FAIL) st = "Échoué" + (d.why.empty () ? std::string () : " : " + d.why);
	else if (d.state == DL_CANCEL) st = "Annulé";
	else st = "Interrompu";
	char when[32] = "";
	time_t t = (time_t) d.when;
	if (struct tm *tm = localtime (&t))
		strftime (when, sizeof when, "%d/%m %H:%M", tm);
	return dl_name (d.path) + "\t" + st + "\t" + when;
}

static void dl_save ()
{
	if (g_dlFile.empty ()) return;
	if (FILE *f = fopen (g_dlFile.c_str (), "wb"))
	{
		for (const DlItem &d : g_dl)	// (en cours : « interrompu » si Jet se ferme avant la fin)
			fprintf (f, "%ld\t%d\t%lld\t%lld\t%s\t%s\n", d.when, d.state == DL_RUN ? DL_LOST : d.state, d.done, d.total,
				 d.path.c_str (), d.why.c_str ());
		fclose (f);
	}
}

static void dl_load ()
{
	g_dl.clear ();
	FILE *f = fopen (g_dlFile.c_str (), "rb");
	if (!f) return;
	char line[2048];
	while (fgets (line, sizeof line, f))
	{
		line[strcspn (line, "\r\n")] = 0;
		DlItem d = {};
		char *p = line, *fld[6] = {};
		for (int i = 0; i < 6 && p; i++)
		{
			fld[i] = p;
			char *t = strchr (p, '\t');
			if (t) *t++ = 0;
			p = t;
		}
		if (!fld[4] || !*fld[4]) continue;
		d.when = atol (fld[0]);
		d.state = atoi (fld[1]);
		d.done = atoll (fld[2]);
		d.total = atoll (fld[3]);
		d.path = fld[4];
		d.why = fld[5] ? fld[5] : "";
		g_dl.push_back (d);
	}
	fclose (f);
	if (g_dl.size () > 200) g_dl.erase (g_dl.begin (), g_dl.end () - 200);	// (les 200 derniers)
}

// le rang dans g_dl de la ligne choisie (la liste : le plus récent en haut) -> -1 : aucune
static int dl_selected ()
{
	if (!g_dlWin) return -1;
	long r = ui_w_get_value (g_dlList);
	if (r < 0 || r >= (long) g_dl.size ()) return -1;
	return (int) g_dl.size () - 1 - (int) r;
}

static bool dl_exists (const std::string &p)
{
	struct stat st;
	return !p.empty () && stat (p.c_str (), &st) == 0;
}

static void dl_buttons ()
{
	int i = dl_selected ();
	const DlItem *d = i >= 0 ? &g_dl[(size_t) i] : nullptr;
	ui_w_enable (g_dlOpen, d && d->state == DL_DONE && dl_exists (d->path));
	ui_w_enable (g_dlFolder, d && !d->path.empty ());
	ui_w_enable (g_dlCancel, d && d->state == DL_RUN);
	ui_w_enable (g_dlRemove, d && d->state != DL_RUN);
	bool any = false;
	for (const DlItem &x : g_dl) any |= x.state != DL_RUN;
	ui_w_enable (g_dlClear, any);
}

// la liste refaite (ou une ligne : row >= 0, rang dans g_dl)
static void dl_refresh (int row = -1)
{
	if (!g_dlWin) return;
	long prev = ui_window_use (g_dlWin);
	if (row >= 0 && ui_list_count (g_dlList) == (long) g_dl.size ())
		ui_list_set (g_dlList, (long) g_dl.size () - 1 - row, (long) dl_row (g_dl[(size_t) row]).c_str ());
	else
	{
		long sel = ui_w_get_value (g_dlList);
		ui_list_clear (g_dlList);
		for (size_t i = g_dl.size (); i-- > 0;)
			ui_list_add (g_dlList, (long) dl_row (g_dl[i]).c_str (), (long) i);
		if (sel >= 0 && sel < (long) g_dl.size ()) ui_w_set_value (g_dlList, sel);
	}
	dl_buttons ();
	ui_w_draw (0);
	ui_window_use (prev ? prev : g_dlMain);
}

static void dl_close ()
{
	if (!g_dlWin) return;
	ui_window_close (g_dlWin);
	g_dlWin = 0;
	ui_window_use (g_dlMain);
}

static void dl_on_widget (long w, long code)
{
	int i = dl_selected ();
	DlItem *d = i >= 0 ? &g_dl[(size_t) i] : nullptr;
	if (w == g_dlList)
	{
		if (code == UN_ACTIVATE && d && d->state == DL_DONE && dl_exists (d->path))
			ui_assoc_open ((long) d->path.c_str ());
		dl_buttons ();
		ui_w_draw (0);
	}
	else if (w == g_dlOpen && d && dl_exists (d->path))
		ui_assoc_open ((long) d->path.c_str ());
	else if (w == g_dlFolder && d)
	{
		size_t s = d->path.rfind ('/');
		std::string dir = s == std::string::npos || s == 0 ? "/" : d->path.substr (0, s);
		ui_assoc_open ((long) dir.c_str ());		// (un dossier : Fichiers)
	}
	else if (w == g_dlCancel && d && d->state == DL_RUN)
		engine_download_cancel (d->id);
	else if (w == g_dlRemove && d && d->state != DL_RUN)
	{
		g_dl.erase (g_dl.begin () + i);
		dl_save ();
		dl_refresh ();
	}
	else if (w == g_dlClear)
	{
		std::vector<DlItem> keep;
		for (const DlItem &x : g_dl)
			if (x.state == DL_RUN) keep.push_back (x);
		g_dl.swap (keep);
		dl_save ();
		dl_refresh ();
	}
}

// la fenêtre ouverte (ou montrée, si elle l'est déjà), la ligne row choisie (rang dans g_dl ; -1 : rien)
static void dl_show (int row = -1)
{
	if (!g_dlWin)
	{
		const long W = 640, H = 380, BH = 30, BY = H - BH - 10, TOP = 44;	// (TOP : les boutons du cadre, sur le contenu)
		long prev = ui_window_use (g_dlMain);
		(void) prev;
		g_dlWin = ui_window_ex (-1, -1, W, H, (long) "Téléchargements", 0);	// (ui_window : la première fenêtre seulement)
		if (g_dlWin <= 0) { g_dlWin = 0; ui_window_use (g_dlMain); return; }
		ui_window_use (g_dlWin);
		struct ui_vars *v = (struct ui_vars *) jet_ui_vars ();
		const uint32_t *tk = (const uint32_t *) ui_tokens ();	// (le fond : celui du thème, comme Jet)
		ui_fill (0, 0, v->w, v->h, tk ? (long) tk[TJ_FACE / 4] : 0x808080);
		ui_label (14, 12, 300, 22, (long) "Téléchargements", 0);
		g_dlList = ui_list (10, TOP, W - 20, BY - TOP - 10, UL_HEADER | UL_NOSORT | UL_STRIPES);
		ui_list_column (g_dlList, (long) "Fichier", 260, 0);
		ui_list_column (g_dlList, (long) "État", 250, 0);
		ui_list_column (g_dlList, (long) "Date", 100, 0);
		long x = 10;
		auto btn = [&] (const char *t, long w) { long b = ui_pushbutton (x, BY, w, BH, (long) t, 0); x += w + 8; return b; };	// (ui_button ne fait que dessiner)
		g_dlOpen = btn ("Ouvrir", 80);
		g_dlFolder = btn ("Ouvrir le dossier", 140);
		g_dlCancel = btn ("Annuler", 86);
		g_dlRemove = btn ("Retirer", 80);
		g_dlClear = btn ("Effacer l'historique", 196);
		for (long w : { g_dlList, g_dlOpen, g_dlFolder, g_dlCancel, g_dlRemove, g_dlClear })
			ui_w_on (w, (long) dl_on_widget, 0);
		ui_window_use (g_dlMain);
		dl_refresh ();
	}
	if (row >= 0)
	{
		ui_window_use (g_dlWin);
		ui_w_set_value (g_dlList, (long) g_dl.size () - 1 - row);
		dl_buttons ();
		ui_w_draw (0);
		ui_window_use (g_dlMain);
	}
}

// un événement de la fenêtre des téléchargements (EV_WIN) : ses widgets ; la fermer, Échap
static void dl_event (long t, int code)
{
	if (t == WEV_CLOSE || (t == WEV_KEY && code == 27)) { dl_close (); return; }
	ui_window_use (g_dlWin);
	ui_w_event (0);
	if (g_dlWin) ui_window_use (g_dlMain);
}

// au démarrage : la fenêtre de Jet, l'historique
static void dl_init (long mainWin, const std::string &file)
{
	g_dlMain = mainWin;
	g_dlFile = file;
	dl_load ();
}

// un événement d'un téléchargement (EngineClient::download)
static void dl_engine (int id, int event, const char *text, long long done, long long total)
{
	int i = -1;
	for (size_t k = 0; k < g_dl.size (); k++)
		if (g_dl[k].id == id && g_dl[k].state == DL_RUN) i = (int) k;
	if (event == ENGINE_DL_STARTED && i < 0)
	{
		DlItem d = {};
		d.id = id;
		d.path = text ? text : "";
		d.state = DL_RUN;
		d.when = (long) time (nullptr);
		g_dl.push_back (d);
		dl_save ();
		dl_show ((int) g_dl.size () - 1);	// (elle s'ouvre d'elle-même)
		return;
	}
	if (i < 0) return;
	DlItem &d = g_dl[(size_t) i];
	d.done = done;
	d.total = total;
	if (event == ENGINE_DL_PROGRESS) { dl_refresh (i); return; }
	if (event == ENGINE_DL_FINISHED) d.state = DL_DONE;
	else if (event == ENGINE_DL_FAILED) { d.state = DL_FAIL; d.why = text ? text : ""; }
	else if (event == ENGINE_DL_CANCELLED) d.state = DL_CANCEL;
	dl_save ();
	dl_refresh ();
}
