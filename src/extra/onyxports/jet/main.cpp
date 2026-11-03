// main.cpp -- Jet sur ARMPI : l'habillage du navigateur d'Onyx (docs/WEBKIT.md, etape 5) sur UIKit
// d'ARMPI (uikit.h, widgets du kit en assembleur), le moteur etant celui d'Onyx (engine.h,
// engine_webkit.cpp : WebKit2 par son API C). Ecrit pour ARMPI ; reprend de user/Apps/jet/main.cpp
// d'Onyx la structure (une page par fenetre, un lien « nouvelle fenetre » relance le programme) et
// le contrat avec le moteur.
//
//   SD0:/apps/jet.app/main [adresse]      (sans adresse : SD0:/apps/jet.app/start.html)
//
// Un seul programme, trois roles : relance par WebKit avec --onyx-webkit-process=web / network, il
// est le process web ou reseau (engine_auxiliary_main). Fenetre : une barre d'outils (precedente,
// suivante, recharger / arreter, accueil), l'adresse, la page (une surface attachee d'Elegant ou
// WebKit peint directement : present.c), la barre d'etat. Ce qui se passe va dans le journal du
// noyau (lignes « web: »).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.

#include "engine.h"
#include "present.h"
#include <uikit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>

#define TB_H	40		// la barre d'outils
#define SB_H	22		// la barre d'etat
#define HOME	"file:///apps/jet.app/start.html"

// (codes de touches d'Onyx que le moteur attend : engine_webkit.cpp)
#define OK_MOD_CTRL	1
#define OK_MOD_SHIFT	2
#define OK_MOD_ALT	4
#define OK_KEY_BACKSPACE 8
#define OK_KEY_TAB	9
#define OK_KEY_ENTER	13
#define OK_KEY_UP	0x100
#define OK_KEY_DOWN	0x101
#define OK_KEY_LEFT	0x102
#define OK_KEY_RIGHT	0x103
#define OK_KEY_HOME	0x104
#define OK_KEY_END	0x105
#define OK_KEY_PGUP	0x106
#define OK_KEY_PGDN	0x107
#define OK_KEY_DEL	0x108
#define OK_KEY_F1	0x110

static struct ui_vars *U;
static long g_tb, g_back, g_fwd, g_reload, g_home, g_url, g_status;
static int g_w, g_h;				// la fenetre
static int g_pw, g_ph;				// la page
static uint32_t *g_px;				// ses pixels (la surface, ou un tampon a soi)
static int g_stride;
static bool g_ownBuffer;
static int g_d0x, g_d0y, g_d1x, g_d1y;		// la zone a repeindre (g_d1x 0 : rien)
static int g_buttons, g_mx, g_my;
static bool g_mouseInPage;			// le pointeur est sur la page (la molette y va)
static bool g_capture;				// un bouton appuye dans la page : elle a la souris
static bool g_pageFocus = true;			// le clavier va a la page (sinon a l'adresse)
static bool g_loading;
static char g_statusText[512];
static char g_lastUrl[2048];

static void say (const char *s)
{
	char b[600];
	int n = snprintf (b, sizeof b, "web: %s\n", s);
	if (n > (int) sizeof b - 1) n = (int) sizeof b - 1;
	fwrite (b, 1, (size_t) n, stderr);
	fflush (stderr);
}

static void status (const char *t)
{
	snprintf (g_statusText, sizeof g_statusText, "%s", t ? t : "");
	ui_w_set_text (g_status, (long) g_statusText);
	ui_w_draw (g_status);
}

// ---- le moteur -> la fenetre ----------------------------------------------------------------------------

static void cb_needsDisplay (int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0) return;
	int x1 = x + w, y1 = y + h;
	if (g_d1x == 0) { g_d0x = x; g_d0y = y; g_d1x = x1; g_d1y = y1; return; }
	if (x < g_d0x) g_d0x = x;
	if (y < g_d0y) g_d0y = y;
	if (x1 > g_d1x) g_d1x = x1;
	if (y1 > g_d1y) g_d1y = y1;
}

// Le process web attend de savoir son image montree : pendant qu'elles arrivent, ne pas dormir 10 ms.
static int g_frames;

static int cb_frame (int buffer, int pitch, long offset, int x, int y, int w, int h)
{
	g_frames = 100;
	return jet_page_show (buffer, pitch, offset, x, y, w, h);
}

static void cb_title (const char *t) { jet_window_title (t && *t ? t : "Jet"); }

static void cb_url (const char *u)
{
	snprintf (g_lastUrl, sizeof g_lastUrl, "%s", u ? u : "");
	if (g_pageFocus) { ui_w_set_text (g_url, (long) g_lastUrl); ui_w_draw (g_url); }
}

static void cb_loading (bool loading, double p)
{
	g_loading = loading;
	if (loading)
	{
		char b[64];
		snprintf (b, sizeof b, "Chargement… %d %%", (int) (p * 100));
		status (b);
	}
	else if (!strncmp (g_statusText, "Chargement", 10))
		status ("");
}

static void cb_history (bool back, bool fwd)
{
	ui_w_enable (g_back, back);
	ui_w_enable (g_fwd, fwd);
}

static void cb_status (const char *t) { if (!g_loading) status (t); }
static void cb_open (const char *url) { engine_new_window (url); }

static void cb_failed (const char *url, const char *why)
{
	char b[512];
	snprintf (b, sizeof b, "Échec : %s (%s)", url ? url : "", why ? why : "");
	status (b);
}

static void cb_ended () { status ("Le process web s'est arrêté : Recharger relance la page."); }
static void cb_alert (const char *t) { ui_message ((long) "Jet", (long) (t ? t : ""), UM_OK); }
static bool cb_confirm (const char *t) { return ui_message ((long) "Jet", (long) (t ? t : ""), UM_OKCANCEL) == 1; }

// une liste <select> : montree apres engine_cycle (hors de l'appel du moteur)
static std::vector<std::string> g_popItems;
static int g_popX = -1, g_popY, g_popW;

static void cb_popup (const char *const *items, const unsigned char *flags, int count, int selected, int x, int y, int w, int h)
{
	(void) flags; (void) selected;
	g_popItems.clear ();
	for (int i = 0; i < count; i++)
		g_popItems.push_back (items[i] ? items[i] : "");
	g_popX = x; g_popY = TB_H + y + h; g_popW = w < 120 ? 120 : w;
}

static void cb_hide_popup () { }

static void show_pending_popup ()
{
	if (g_popX < 0) return;
	std::string labels;
	for (auto &s : g_popItems) { labels += s.empty () ? " " : s; labels += '\0'; }
	labels += '\0';
	long r = ui_popup_local (g_popX, g_popY, (long) labels.data (), g_popW);
	g_popX = -1;
	engine_popup_select (r >= 0 && r < (long) g_popItems.size () ? (int) r : -1);
}

static void cb_download (int id, int event, const char *text, long long done, long long total)
{
	(void) id; (void) done; (void) total;
	char b[512];
	if (event == ENGINE_DL_FINISHED) snprintf (b, sizeof b, "Téléchargé : %s", text ? text : "");
	else if (event == ENGINE_DL_FAILED) snprintf (b, sizeof b, "Téléchargement échoué : %s", text ? text : "");
	else if (event == ENGINE_DL_STARTED) snprintf (b, sizeof b, "Téléchargement : %s", text ? text : "");
	else return;
	status (b);
}

static void cb_key_not_handled (long key, unsigned mods);

static EngineClient g_client;

// ---- la page : peinture ------------------------------------------------------------------------------------

static void flush_paint ()
{
	if (g_d1x == 0 || g_px == 0) return;
	int x = g_d0x < 0 ? 0 : g_d0x, y = g_d0y < 0 ? 0 : g_d0y;
	int x1 = g_d1x > g_pw ? g_pw : g_d1x, y1 = g_d1y > g_ph ? g_ph : g_d1y;
	g_d1x = 0;
	if (x1 <= x || y1 <= y) return;
	engine_paint (g_px, g_stride, x, y, x1 - x, y1 - y);
	jet_present (g_px, g_stride, x, y, x1 - x, y1 - y);
}

// La page encore vide : la couleur de fond de la fenetre (pas de noir le temps que WebKit peigne)
static void clear_page ()
{
	const uint32_t *tok = (const uint32_t *) ui_jetons ();
	uint32_t c = tok ? tok[TJ_FACE / 4] : 0xF0F0F0;
	for (int y = 0; y < g_ph; y++)
		for (int x = 0; x < g_pw; x++)
			g_px[(long) y * g_stride + x] = c;
	jet_present (g_px, g_stride, 0, 0, g_pw, g_ph);
}

// La fenetre a change de taille (bords, bouton agrandir : Elegant s'arrete au dock et a la barre de
// menus ; UIKit a deja un tampon neuf, blanc) : les widgets replaces, la page suit.
static void on_resize (long w, long h)
{
	g_w = (int) w;
	g_h = (int) h;
	const uint32_t *tok = (const uint32_t *) ui_jetons ();
	ui_fill (0, 0, g_w, g_h, tok ? tok[TJ_FACE / 4] : 0xF0F0F0);
	ui_w_move (g_url, 154, 6, g_w - 266, TB_H - 12);	// (left of the window's buttons)
	ui_w_move (g_status, 8, g_h - SB_H, g_w - 16, SB_H);
	ui_w_draw (0);
	ui_update_all ();
	g_pw = g_w;
	g_ph = g_h - TB_H - SB_H;
	uint32_t *px = g_ownBuffer ? 0 : jet_page_area (0, TB_H, g_pw, g_ph, &g_stride);
	if (px == 0)
	{
		if (g_ownBuffer) free (g_px);
		px = (uint32_t *) calloc ((size_t) g_pw * g_ph, 4);
		g_stride = g_pw;
		g_ownBuffer = true;
	}
	g_px = px;
	clear_page ();
	engine_resize (g_pw, g_ph);
	cb_needsDisplay (0, 0, g_pw, g_ph);
}

// ---- les widgets ---------------------------------------------------------------------------------------------

static void focus_page ()
{
	g_pageFocus = true;
	ui_w_focus (0);
	if (g_lastUrl[0]) { ui_w_set_text (g_url, (long) g_lastUrl); ui_w_draw (g_url); }
	engine_set_active (true);
}

static void focus_address ()
{
	g_pageFocus = false;
	ui_w_focus (g_url);
	const char *t = (const char *) ui_w_get_text (g_url);
	ui_textbox_select (g_url, 0, t ? (long) strlen (t) : 0);
	ui_w_draw (g_url);
}

static void on_widget (long w, long code)
{
	if (w == g_back) engine_back ();
	else if (w == g_fwd) engine_forward ();
	else if (w == g_reload) { if (g_loading) engine_stop (); else engine_reload (); }
	else if (w == g_home) engine_load (HOME);
	else if (w == g_url && code == UN_ACTIVATE)
	{
		const char *t = (const char *) ui_w_get_text (g_url);
		if (t && *t) engine_load (t);
		focus_page ();
	}
}

static void cb_key_not_handled (long key, unsigned mods)
{
	if ((mods & OK_MOD_CTRL) && key == 12) focus_address ();		// Ctrl+L
	else if ((mods & OK_MOD_CTRL) && key == 18) engine_reload ();		// Ctrl+R
	else if (key == OK_KEY_F1 + 4) engine_reload ();			// F5
	else if ((mods & OK_MOD_ALT) && key == OK_KEY_LEFT) engine_back ();
	else if ((mods & OK_MOD_ALT) && key == OK_KEY_RIGHT) engine_forward ();
	else if (key == OK_KEY_BACKSPACE) engine_back ();
}

// ---- la fenetre -> le moteur -------------------------------------------------------------------------------

// modificateurs HID d'ARMPI (gauche bits 0-3, droite 4-7 : Ctrl, Maj, Alt, GUI) -> ceux d'Onyx
static unsigned onyx_mods (int m)
{
	unsigned r = 0;
	if (m & 0x11) r |= OK_MOD_CTRL;
	if (m & 0x22) r |= OK_MOD_SHIFT;
	if (m & 0x44) r |= OK_MOD_ALT;
	return r;
}

// une touche d'ARMPI (code HID, caractere Unicode) -> le code d'Onyx (0 : aucun)
static long onyx_key (int hid, int ch, unsigned mods)
{
	if ((mods & OK_MOD_CTRL) && hid >= 0x04 && hid <= 0x1D) return hid - 0x04 + 1;	// Ctrl+lettre : ^A = 1
	switch (hid)
	{
	case 0x28: case 0x58: return OK_KEY_ENTER;
	case 0x29: return 27;
	case 0x2A: return OK_KEY_BACKSPACE;
	case 0x2B: return OK_KEY_TAB;
	case 0x4C: return OK_KEY_DEL;
	case 0x4F: return OK_KEY_RIGHT;
	case 0x50: return OK_KEY_LEFT;
	case 0x51: return OK_KEY_DOWN;
	case 0x52: return OK_KEY_UP;
	case 0x4A: return OK_KEY_HOME;
	case 0x4D: return OK_KEY_END;
	case 0x4B: return OK_KEY_PGUP;
	case 0x4E: return OK_KEY_PGDN;
	}
	if (hid >= 0x3A && hid <= 0x45) return OK_KEY_F1 + (hid - 0x3A);	// F1..F12
	return ch >= 32 ? ch : 0;
}

static bool in_page (int x, int y) { return x >= 0 && x < g_pw && y >= TB_H && y < TB_H + g_ph; }

static void page_mouse (int kind, int x, int y, int code, unsigned mods)
{
	int px = x, py = y - TB_H;
	g_mx = px; g_my = py;
	if (kind == 0)
	{
		engine_mouse (px, py, g_buttons, 0, mods);
		ui_cursor (engine_cursor () == ENGINE_CUR_HAND ? UC_HAND : engine_cursor () == ENGINE_CUR_TEXT ? UC_TEXT : UC_ARROW);
		return;
	}
	int changed;
	if (kind == 1)
	{
		changed = code & ~g_buttons & 7;
		if (!changed) changed = 1;
		if (!g_pageFocus) focus_page ();
		g_capture = true;
	}
	else
	{
		changed = g_buttons & ~code & 7;
		if (!changed) changed = g_buttons ? g_buttons : 1;
	}
	for (int bit = 1; bit <= 4; bit <<= 1)		// un evenement par bouton
		if (changed & bit)
		{
			g_buttons ^= bit;
			engine_mouse (px, py, g_buttons & 7, bit, mods);
		}
	if (g_buttons == 0) g_capture = false;
}

static void handle_event (long t)
{
	int kind = ui_ev_field (U, EV_KIND), x = ui_ev_field (U, EV_X), y = ui_ev_field (U, EV_Y);
	int code = ui_ev_field (U, EV_CODE), ch = ui_ev_field (U, EV_CHAR);
	unsigned mods = onyx_mods (ui_ev_field (U, EV_MODS));
	switch (t)
	{
	case WEV_MOUSE:
		g_mouseInPage = in_page (x, y);
		if (g_capture || g_mouseInPage)
		{
			page_mouse (kind, x, y, code, mods);
			return;
		}
		break;
	case WEV_WHEEL:
		if (g_mouseInPage)
		{
			engine_wheel (g_mx, g_my, y, mods);	// (+12 : crans ; la position : celle du pointeur)
			return;
		}
		break;
	case WEV_KEY:
	{
		if (kind == 0) return;				// (relache : le moteur fait les deux)
		long k = onyx_key (code, ch, mods);
		if ((mods & OK_MOD_CTRL) && k == 12) { focus_address (); return; }	// Ctrl+L
		if (!g_pageFocus)
		{
			if (k == 27) { focus_page (); return; }
			break;					// (l'adresse : ses widgets)
		}
		if (k) engine_key (k, mods);
		return;
	}
	case WEV_FOCUS:
		engine_set_active (kind != 0);
		break;
	}
	bool urlBefore = ui_w_get_focus () == g_url;
	ui_w_event (0);
	if (t == WEV_MOUSE && kind == 1 && ui_w_get_focus () == g_url)
	{
		g_pageFocus = false;
		if (!urlBefore)				// (un premier clic : toute l'adresse choisie,
		{					//  la frappe la remplace, comme un navigateur)
			const char *a = (const char *) ui_w_get_text (g_url);
			ui_textbox_select (g_url, 0, a ? (long) strlen (a) : 0);
			ui_w_draw (g_url);
		}
	}
}

// ---- demarrage ------------------------------------------------------------------------------------------------

int main (int argc, char **argv)
{
	// Le cache DNS de curl garde un nom introuvable la moitie de sa duree (5 min chez WebKit) : ouvert
	// avant que le reseau soit pret, Jet ne retrouvait pas un site pendant 2 min 30. Ici 15 s. (Lu
	// par le process reseau : avant tout le reste, dans chaque process.)
	setenv ("WEBKIT_CURL_DNS_CACHE_TIMEOUT", "30", 1);
	if (engine_is_auxiliary (argc, argv))
		return engine_auxiliary_main (argc, argv);

	// Le compositeur (gpucomp sur GpuKit, repli CPU sans GPU) par defaut, comme Jet chez Onyx ; le
	// chemin logiciel (la page peinte et recopiee par le processeur) : --nogpu ou SD0:/etc/web-nogpu,
	// --gpu le demande quoi que dise le fichier.
	const char *url = HOME;
	bool gpu = access ("SD0:/etc/web-nogpu", F_OK) != 0;
	for (int i = 1; i < argc; i++)
		if (strcmp (argv[i], "--gpu") == 0)
			gpu = true;
		else if (strcmp (argv[i], "--nogpu") == 0)
			gpu = false;
		else if (strncmp (argv[i], "--", 2) != 0)
			url = argv[i];
	engine_set_compositing (gpu);
	if (gpu)
		engine_set_frame_client (cb_frame);

	if (!jet_window_open (0, 0, "Jet", &g_w, &g_h))
	{
		say ("pas de fenetre (UIKit, Elegant)");
		return 1;
	}
	U = (struct ui_vars *) jet_ui_vars ();
	const uint32_t *tok = (const uint32_t *) ui_jetons ();
	ui_fill (0, 0, g_w, g_h, tok ? tok[TJ_FACE / 4] : 0xF0F0F0);

	g_tb = ui_toolbar (0, 0, 150, TB_H);
	ui_w_style (g_tb, 1);		// (UTBS_NOLINE : it stops at the address bar)
	g_back = ui_toolbar_add (g_tb, (long) "←", 0, 0);
	g_fwd = ui_toolbar_add (g_tb, (long) "→", 0, 0);
	g_reload = ui_toolbar_add (g_tb, (long) "↻", 0, 0);
	g_home = ui_toolbar_add (g_tb, (long) "⌂", 0, 0);
	g_url = ui_textbox (154, 6, g_w - 266, TB_H - 12, 0);
	ui_textbox_hint (g_url, (long) "Adresse ou recherche");
	g_status = ui_label (8, g_h - SB_H, g_w - 16, SB_H, (long) "", ULB_DIM);
	for (long w : { g_back, g_fwd, g_reload, g_home, g_url })
		ui_w_on (w, (long) on_widget, 0);
	ui_w_enable (g_back, 0);
	ui_w_enable (g_fwd, 0);
	ui_w_draw (0);
	ui_update_all ();

	g_pw = g_w;
	g_ph = g_h - TB_H - SB_H;
	g_px = jet_page_area (0, TB_H, g_pw, g_ph, &g_stride);
	if (g_px == 0)					// (pas de surface : un tampon a soi, copie)
	{
		g_px = (uint32_t *) calloc ((size_t) g_pw * g_ph, 4);
		g_stride = g_pw;
		g_ownBuffer = true;
		say ("pas de surface attachee : copie dans la fenetre");
	}
	if (g_px == 0)
		return 1;
	clear_page ();

	g_client.needsDisplay = cb_needsDisplay;
	g_client.titleChanged = cb_title;
	g_client.urlChanged = cb_url;
	g_client.loadingChanged = cb_loading;
	g_client.historyChanged = cb_history;
	g_client.statusText = cb_status;
	g_client.openWindow = cb_open;
	g_client.loadFailed = cb_failed;
	g_client.processEnded = cb_ended;
	g_client.alert = cb_alert;
	g_client.confirm = cb_confirm;
	g_client.keyNotHandled = cb_key_not_handled;
	g_client.showPopup = cb_popup;
	g_client.hidePopup = cb_hide_popup;
	g_client.download = cb_download;
	if (!engine_init (&g_client, g_pw, g_ph))
	{
		ui_message ((long) "Jet", (long) "WebKit n'a pas pu démarrer.", UM_OK);
		return 1;
	}
	ui_on_resize ((long) on_resize, 0);
	status ("Démarrage de WebKit…");
	engine_load (url);

	for (;;)
	{
		long t = ui_event (g_frames > 0 ? 1 : 10);
		if (g_frames > 0)
			g_frames--;
		if (t == WEV_CLOSE)
			break;
		if (t > 0)
			handle_event (t);
		else
			ui_w_idle ();
		engine_cycle ();
		show_pending_popup ();
		flush_paint ();
	}
	jet_window_close ();
	return 0;
}
