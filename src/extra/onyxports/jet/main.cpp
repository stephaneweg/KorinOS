// main.cpp -- Jet sur Korin : l'habillage du navigateur d'Onyx (docs/WEBKIT.md, etape 5) sur UIKit
// de Korin (uikit.h, widgets du kit), le moteur etant celui d'Onyx (engine.h, engine_webkit.cpp :
// WebKit2 par son API C). Ecrit pour Korin ; reprend de user/Apps/jet/main.cpp d'Onyx le contrat avec
// le moteur.
//
//   SD0:/apps/jet.app/main [adresse...]      (une adresse par onglet ; sans adresse : SD0:/apps/jet.app/start.html)
//
// Un seul programme, des roles : relance par WebKit avec --onyx-webkit-process=web / network, il est
// un process web ou le process reseau (engine_auxiliary_main). Fenetre, comme Safari : la barre d'outils
// (precedente, suivante, recharger / arreter, accueil, l'adresse, le moteur de recherche), dessous la
// rangee des onglets (largeur egale, une largeur minimale puis la rangee defile : < >, le +), la page
// (une surface attachee d'Elegant ou WebKit peint directement : present.c), la barre d'etat. Chaque
// onglet a son process web (engine.h) ; s'il meurt (plantage, kill), l'onglet se ferme ; le dernier
// ferme, un onglet neuf s'ouvre. Ce qui se passe va dans le journal du noyau (lignes « web: »).
// Traces : --trace-media, --trace-net (ou SD0:/etc/web-trace-media, web-trace-net) ; la taille des videos
// offertes aux sites : SD0:/etc/web-video-max (une hauteur : 480, 720...).
// Reglages : SD0:/home/jet/settings.ini (search=google|bing|duckduckgo|yahoo|qwant|ecosia|startpage|brave) ; les
// donnees des sites (cookies, localStorage, IndexedDB...) : SD0:/home/jet/webkit, le cache : SD0:/home/jet/cache
// (engine_webkit.cpp, make_data_store). Menus d'Elegant : Fichier, Navigation, Reglages.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.

#include "engine.h"
#include "present.h"
#include <uikit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

#define TB_H	40		// la barre d'outils
#define TAB_H	32		// la rangee des onglets
#define PAGE_Y	(TB_H + TAB_H)	// le haut de la page
#define SB_H	22		// la barre d'etat
#define TAB_MIN	150		// la largeur minimale d'un onglet
#define TAB_BTN	28		// les boutons < > + au bout de la rangee
#define HOME	"file:///apps/jet.app/start.html"
#define SETTINGS_DIR	"SD0:/home/jet"
#define SETTINGS	"SD0:/home/jet/settings.ini"

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
static long g_tb, g_back, g_fwd, g_reload, g_home, g_url, g_status, g_stb, g_searchBtn;
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

static uint32_t tok (int j)
{
	const uint32_t *t = (const uint32_t *) ui_tokens ();
	return t ? t[j / 4] : 0x808080;
}

// ---- les moteurs de recherche ------------------------------------------------------------------------------

struct SearchEngine { const char *key, *name, *prefix; };
static const SearchEngine g_engines[] = {
	{ "google", "Google", "https://www.google.com/search?q=" },
	{ "bing", "Bing", "https://www.bing.com/search?q=" },
	{ "duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q=" },
	{ "yahoo", "Yahoo", "https://search.yahoo.com/search?p=" },
	{ "qwant", "Qwant", "https://www.qwant.com/?q=" },
	{ "ecosia", "Ecosia", "https://www.ecosia.org/search?q=" },
	{ "startpage", "Startpage", "https://www.startpage.com/do/search?q=" },
	{ "brave", "Brave Search", "https://search.brave.com/search?q=" },
};
#define N_ENGINES ((int) (sizeof g_engines / sizeof g_engines[0]))
static int g_engine;				// (0 : Google, le defaut)
static char g_hint[96];

static void search_apply ()
{
	engine_set_search (g_engines[g_engine].prefix);
	snprintf (g_hint, sizeof g_hint, "Rechercher avec %s ou saisir une adresse", g_engines[g_engine].name);
	if (g_url) { ui_textbox_hint (g_url, (long) g_hint); ui_w_draw (g_url); }
}

// SD0:/home/jet/settings.ini : « search=<cle> » (les autres lignes gardees)
static void settings_load ()
{
	g_engine = 0;
	if (FILE *f = fopen (SETTINGS, "rb"))
	{
		char line[256];
		while (fgets (line, sizeof line, f))
		{
			line[strcspn (line, "\r\n")] = 0;
			if (strncmp (line, "search=", 7)) continue;
			for (int i = 0; i < N_ENGINES; i++)
				if (!strcmp (line + 7, g_engines[i].key)) g_engine = i;
		}
		fclose (f);
	}
	char b[96];
	snprintf (b, sizeof b, "search engine: %s", g_engines[g_engine].name);
	say (b);
}

static void settings_save ()
{
	std::string keep;
	if (FILE *f = fopen (SETTINGS, "rb"))
	{
		char line[256];
		while (fgets (line, sizeof line, f))
			if (strncmp (line, "search=", 7)) keep += line;
		fclose (f);
	}
	mkdir ("SD0:/home", 0755);
	mkdir (SETTINGS_DIR, 0755);
	if (FILE *f = fopen (SETTINGS, "wb"))
	{
		fprintf (f, "%ssearch=%s\n", keep.c_str (), g_engines[g_engine].key);
		fclose (f);
	}
}

// ---- les onglets (cote fenetre) --------------------------------------------------------------------------

struct ChromeTab
{
	int id;				// celui du moteur
	std::string title, url;
	bool loading;
	double progress;
	bool back, fwd;
	bool dead;			// son process web est mort : ferme a la fin du tour
	std::string typed;		// l'adresse en cours de frappe quand on a change d'onglet
	bool fresh;			// ouvert par +, Ctrl+T : montre, l'adresse vide attend la frappe
};
static std::vector<ChromeTab> g_tabs;
static int g_cur = -1;				// l'index de l'onglet montre
static int g_tabScroll;				// le defilement de la rangee (px)
static int g_tabHot = -1, g_tabHotClose;	// l'onglet survole (et sa croix), -1 : aucun
static int g_btnHot = -1;			// le bouton survole : 0 <, 1 >, 2 +
static unsigned g_spin;				// le pas de l'indicateur de chargement
static bool g_tabsDirty;			// la rangee est a redessiner

static int tab_index (int id)
{
	for (size_t i = 0; i < g_tabs.size (); i++)
		if (g_tabs[i].id == id) return (int) i;
	return -1;
}

static ChromeTab *tab_by_id (int id)
{
	int i = tab_index (id);
	return i < 0 ? nullptr : &g_tabs[(size_t) i];
}

static bool is_cur (int id) { return g_cur >= 0 && g_cur < (int) g_tabs.size () && g_tabs[(size_t) g_cur].id == id; }

// la geometrie de la rangee : la zone des onglets [0, area), puis < > +
static int tabs_area () { int a = g_w - 3 * TAB_BTN - 8; return a < TAB_MIN ? TAB_MIN : a; }
static int tab_width ()
{
	int n = (int) g_tabs.size ();
	if (n <= 0) return TAB_MIN;
	int w = tabs_area () / n;
	return w < TAB_MIN ? TAB_MIN : w;
}
static int tabs_total () { return tab_width () * (int) g_tabs.size (); }
static int scroll_max () { int m = tabs_total () - tabs_area (); return m < 0 ? 0 : m; }
static int btn_x (int b) { return tabs_area () + 4 + b * TAB_BTN; }

static void forget_hover () { g_tabHot = g_btnHot = -1; g_tabHotClose = 0; }

static void scroll_to_current ()
{
	forget_hover ();
	int tw = tab_width (), x0 = g_cur * tw, x1 = x0 + tw, area = tabs_area ();
	if (x0 < g_tabScroll) g_tabScroll = x0;
	if (x1 > g_tabScroll + area) g_tabScroll = x1 - area;
	if (g_tabScroll > scroll_max ()) g_tabScroll = scroll_max ();
	if (g_tabScroll < 0) g_tabScroll = 0;
}

// l'indicateur de chargement : huit points en cercle, le plus vif tourne
static void draw_spinner (int cx, int cy)
{
	static const signed char d[8][2] = { { 0, -5 }, { 4, -4 }, { 5, 0 }, { 4, 4 }, { 0, 5 }, { -4, 4 }, { -5, 0 }, { -4, -4 } };
	uint32_t a = tok (TJ_ACCENT), bg = tok (TJ_SUNKEN);
	for (int i = 0; i < 8; i++)
	{
		int age = (int) ((g_spin - (unsigned) i) & 7);	// 0 : le plus vif
		uint32_t c = (uint32_t) ui_mix (a, bg, age * 28);
		ui_rbox (cx + d[i][0] - 1, cy + d[i][1] - 1, 3, 3, 1, c, c);
	}
}

static void draw_tabs ()
{
	g_tabsDirty = false;
	int y = TB_H, area = tabs_area (), tw = tab_width ();
	uint32_t sunken = tok (TJ_SUNKEN), line = tok (TJ_LINE);
	ui_fill (0, y, g_w, TAB_H, sunken);
	ui_fill (0, y + TAB_H - 1, g_w, 1, line);
	ui_clip (0, y, area, TAB_H);
	for (int i = 0; i < (int) g_tabs.size (); i++)
	{
		const ChromeTab &t = g_tabs[(size_t) i];
		int x = i * tw - g_tabScroll;
		if (x + tw <= 0 || x >= area) continue;
		bool active = i == g_cur, hot = i == g_tabHot;
		uint32_t ink;
		if (active)
		{
			// actif : SURFACE arrondi en haut, pose sur le trait du bas (comme ui_tabstrip)
			ui_rbox (x + 2, y + 3, tw - 4, TAB_H - 2, 6, line, line);
			uint32_t s = tok (TJ_SURFACE);
			ui_rbox (x + 3, y + 4, tw - 6, TAB_H - 3, 5, s, s);
			ink = tok (TJ_TEXT1);
		}
		else
		{
			if (hot)
			{
				uint32_t h = (uint32_t) ui_tone (sunken, 116);
				ui_rbox (x + 3, y + 4, tw - 6, TAB_H - 8, 5, h, h);
			}
			else if (i + 1 != g_cur)
				ui_fill (x + tw - 1, y + 9, 1, TAB_H - 18, line);
			ink = hot ? tok (TJ_TEXT1) : tok (TJ_TEXT2);
		}
		// la croix (a gauche, comme Safari) : l'onglet actif et l'onglet survole ; l'indicateur de
		// chargement a droite
		int pad = 28;
		if (active || hot)
		{
			uint32_t cc = hot && g_tabHotClose ? tok (TJ_TEXT1) : tok (TJ_TEXT3);
			if (hot && g_tabHotClose)
			{
				uint32_t h = (uint32_t) ui_tone (active ? tok (TJ_SURFACE) : sunken, 112);
				ui_rbox (x + 7, y + 8, 18, 18, 4, h, h);
			}
			ui_glyph ((long) "glyph-tb-cross", x + 10, y + 11, 12, cc);
		}
		if (t.loading)
			draw_spinner (x + tw - 16, y + TAB_H / 2 + 1);
		const char *title = !t.title.empty () ? t.title.c_str () : !t.url.empty () ? t.url.c_str () : "Nouvel onglet";
		int maxw = tw - 2 * pad;
		if (maxw > 8)
		{
			int w = (int) ui_text_width ((long) title, 13, (long) U->font);
			if (w > maxw) w = maxw;
			ui_text_fit ((long) title, x + (tw - w) / 2, y + TAB_H / 2 + 6, ink, 13, (long) U->font, maxw);
		}
	}
	ui_clip (0, 0, 0, 0);
	// < > : grises quand rien n'est de ce cote ; + : un onglet neuf
	const char *glyph[3] = { "glyph-tb-left", "glyph-tb-right", "glyph-tb-plus" };
	bool on[3] = { g_tabScroll > 0, g_tabScroll < scroll_max (), true };
	for (int b = 0; b < 3; b++)
	{
		int bx = btn_x (b);
		if (on[b] && g_btnHot == b)
		{
			uint32_t h = (uint32_t) ui_tone (sunken, 116);
			ui_rbox (bx + 2, y + 4, TAB_BTN - 4, TAB_H - 8, 6, h, h);
		}
		ui_glyph ((long) glyph[b], bx + (TAB_BTN - 14) / 2, y + (TAB_H - 14) / 2, 14, on[b] ? tok (TJ_TEXT1) : tok (TJ_TEXT3));
	}
	ui_update (0, y, g_w, TAB_H);
}

static void status (const char *t)
{
	snprintf (g_statusText, sizeof g_statusText, "%s", t ? t : "");
	ui_w_set_text (g_status, (long) g_statusText);
	ui_w_draw (g_status);
}

// ---- la page : peinture ------------------------------------------------------------------------------------

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
	uint32_t c = tok (TJ_FACE);
	for (int y = 0; y < g_ph; y++)
		for (int x = 0; x < g_pw; x++)
			g_px[(long) y * g_stride + x] = c;
	jet_present (g_px, g_stride, 0, 0, g_pw, g_ph);
}

// ---- le moteur -> la fenetre ----------------------------------------------------------------------------

// Le process web attend de savoir son image montree : pendant qu'elles arrivent, ne pas dormir 10 ms.
static int g_frames;

static int cb_frame (int buffer, int pitch, long offset, int x, int y, int w, int h)
{
	g_frames = 100;
	return jet_page_show (buffer, pitch, offset, x, y, w, h);
}

static void show_title ()
{
	const ChromeTab *t = g_cur >= 0 ? &g_tabs[(size_t) g_cur] : nullptr;
	jet_window_title (t && !t->title.empty () ? t->title.c_str () : "Jet");
}

static void cb_title (int id, const char *t)
{
	ChromeTab *c = tab_by_id (id);
	if (!c) return;
	c->title = t ? t : "";
	g_tabsDirty = true;
	if (is_cur (id)) show_title ();
}

// L'adresse montree d'un onglet : la sienne ; vide pour la page d'accueil (un onglet neuf : l'invite)
static const char *shown_url (const ChromeTab &t) { return t.url.empty () || t.url == HOME ? "" : t.url.c_str (); }

static void show_url ()
{
	if (g_cur < 0) return;
	snprintf (g_lastUrl, sizeof g_lastUrl, "%s", shown_url (g_tabs[(size_t) g_cur]));
	if (g_pageFocus) { ui_w_set_text (g_url, (long) g_lastUrl); ui_w_draw (g_url); }
}

static void cb_url (int id, const char *u)
{
	ChromeTab *c = tab_by_id (id);
	if (!c) return;
	c->url = u ? u : "";
	if (c->title.empty ()) g_tabsDirty = true;
	if (is_cur (id)) show_url ();
}

static void cb_loading (int id, bool loading, double p)
{
	ChromeTab *c = tab_by_id (id);
	if (!c) return;
	if (c->loading != loading) g_tabsDirty = true;
	c->loading = loading;
	c->progress = p;
	if (!is_cur (id)) return;
	if (loading)
	{
		char b[64];
		snprintf (b, sizeof b, "Chargement… %d %%", (int) (p * 100));
		status (b);
	}
	else if (!strncmp (g_statusText, "Chargement", 10))
		status ("");
}

static void show_history ()
{
	const ChromeTab *t = g_cur >= 0 ? &g_tabs[(size_t) g_cur] : nullptr;
	ui_w_enable (g_back, t && t->back);
	ui_w_enable (g_fwd, t && t->fwd);
}

static void cb_history (int id, bool back, bool fwd)
{
	ChromeTab *c = tab_by_id (id);
	if (!c) return;
	c->back = back;
	c->fwd = fwd;
	if (is_cur (id)) show_history ();
}

static void cb_status (const char *t) { if (g_cur >= 0 && !g_tabs[(size_t) g_cur].loading) status (t); }

static void open_tab (const char *url, int at, bool show, bool fresh);

// target=_blank, window.open, un clic du milieu ou avec Ctrl : un onglet apres celui qui l'ouvre
static void cb_open (int from, const char *url, bool background)
{
	int i = tab_index (from);
	open_tab (url, i < 0 ? (int) g_tabs.size () : i + 1, !background, false);
}

static void cb_failed (int id, const char *url, const char *why)
{
	if (!is_cur (id)) return;
	char b[512];
	snprintf (b, sizeof b, "Échec : %s (%s)", url ? url : "", why ? why : "");
	status (b);
}

// le process web d'un onglet est mort : l'onglet se ferme (a la fin du tour : close_dead_tabs)
static void cb_ended (int id)
{
	ChromeTab *c = tab_by_id (id);
	if (c) c->dead = true;
}

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
	g_popX = x; g_popY = PAGE_Y + y + h; g_popW = w < 120 ? 120 : w;
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

// ---- les onglets : ouvrir, montrer, fermer ----------------------------------------------------------------

static void focus_page ();
static void focus_address ();

static void show_tab (int i)
{
	if (i < 0 || i >= (int) g_tabs.size ()) return;
	// l'adresse en cours de frappe reste a son onglet
	if (g_cur >= 0 && g_cur < (int) g_tabs.size () && g_cur != i)
	{
		const char *t = (const char *) ui_w_get_text (g_url);
		g_tabs[(size_t) g_cur].typed = !g_pageFocus && t ? t : "";
	}
	g_cur = i;
	ChromeTab &t = g_tabs[(size_t) i];
	engine_tab_show (t.id);
	scroll_to_current ();
	g_tabsDirty = true;
	show_title ();
	show_history ();
	status (t.loading ? "Chargement…" : "");
	g_popX = -1;
	clear_page ();
	cb_needsDisplay (0, 0, g_pw, g_ph);
	if (!t.typed.empty ())
	{
		ui_w_set_text (g_url, (long) t.typed.c_str ());
		focus_address ();
		t.typed.clear ();
	}
	else if (t.fresh)
	{
		// un onglet neuf : l'adresse vide (l'invite « Rechercher avec ... »), prete a la frappe
		t.fresh = false;
		g_lastUrl[0] = 0;
		ui_w_set_text (g_url, (long) "");
		focus_address ();
	}
	else
	{
		g_pageFocus = true;
		ui_w_focus (0);
		show_url ();
		engine_set_active (true);
	}
}

static void open_tab (const char *url, int at, bool show, bool fresh = false)
{
	int id = engine_tab_new (url);
	if (!id)
	{
		status ("Pas d'onglet de plus : WebKit n'a pas pu en ouvrir.");
		return;
	}
	ChromeTab t = { };
	t.id = id;
	t.fresh = fresh;
	if (at < 0 || at > (int) g_tabs.size ()) at = (int) g_tabs.size ();
	g_tabs.insert (g_tabs.begin () + at, t);
	if (g_cur >= at && g_cur >= 0) g_cur++;		// (l'onglet montre a glisse)
	forget_hover ();
	char b[96];
	snprintf (b, sizeof b, "tab %d opened (%d tabs)", id, (int) g_tabs.size ());
	say (b);
	if (show || g_cur < 0)
		show_tab (at);
	else
		g_tabsDirty = true;
}

// Fermer l'onglet i : le voisin de droite (sinon de gauche) est montre ; le dernier ferme, un onglet neuf
static void close_tab (int i)
{
	if (i < 0 || i >= (int) g_tabs.size ()) return;
	int id = g_tabs[(size_t) i].id;
	bool wasCur = i == g_cur;
	g_tabs.erase (g_tabs.begin () + i);
	forget_hover ();
	if (g_cur > i) g_cur--;
	if (wasCur)
		g_cur = -1;
	engine_tab_close (id);
	if (g_tabs.empty ())
	{
		g_tabScroll = 0;
		open_tab (HOME, 0, true, true);
		return;
	}
	if (wasCur)
		show_tab (i < (int) g_tabs.size () ? i : i - 1);
	if (g_tabScroll > scroll_max ()) g_tabScroll = scroll_max ();
	g_tabsDirty = true;
}

static void close_dead_tabs ()
{
	for (int i = 0; i < (int) g_tabs.size (); i++)
		if (g_tabs[(size_t) i].dead)
		{
			char b[96];
			snprintf (b, sizeof b, "tab %d closed: its web process ended", g_tabs[(size_t) i].id);
			say (b);
			close_tab (i);
			status ("Le process web d'un onglet s'est arrêté : l'onglet est fermé.");
			i = -1;				// (la liste a change)
		}
}

static void new_tab ()
{
	open_tab (HOME, (int) g_tabs.size (), true, true);	// (show_tab : l'adresse vide attend la frappe)
}

// ---- le moteur de recherche (le bouton ⌕ a droite de l'adresse) -------------------------------------------

static void menu_checks ();

static void set_engine (int e)
{
	if (e < 0 || e >= N_ENGINES || e == g_engine) return;
	g_engine = e;
	search_apply ();
	settings_save ();
	menu_checks ();
	char b[96];
	snprintf (b, sizeof b, "search engine: %s", g_engines[g_engine].name);
	say (b);
	snprintf (b, sizeof b, "Moteur de recherche : %s", g_engines[g_engine].name);
	status (b);
}

static void site_data_cleared () { status ("Cookies et données des sites effacés."); }

// « Effacer les cookies et données des sites » : demande, puis WebKit efface (engine_clear_site_data)
static void clear_site_data ()
{
	if (ui_message ((long) "Jet", (long) "Effacer les cookies et les données de tous les sites (connexions, "
			"localStorage, IndexedDB, cache) ?\nLes sites ouverts devront peut-être être rechargés.", UM_OKCANCEL) != 1)
		return;
	status ("Effacement des cookies et données des sites…");
	engine_clear_site_data (site_data_cleared);
}

static void choose_search ()
{
	std::string labels;
	for (int i = 0; i < N_ENGINES; i++)
	{
		labels += i == g_engine ? "✓ " : "    ";
		labels += g_engines[i].name;
		labels += '\0';
	}
	labels += "-";
	labels += '\0';
	labels += "    Effacer les cookies et données des sites…";
	labels += '\0';
	labels += '\0';
	long r = ui_popup_local (g_w - 188 - 160, TB_H - 4, (long) labels.data (), 300);
	if (r >= 0 && r < N_ENGINES)
		set_engine ((int) r);
	else if (r == N_ENGINES + 1)
		clear_site_data ();
}

// ---- les menus d'Elegant (la barre du haut) -------------------------------------------------------------

enum { M_NEWTAB = 1, M_NEWWIN, M_CLOSETAB, M_QUIT, M_BACK, M_FWD, M_RELOAD, M_HOME, M_NEXTTAB, M_PREVTAB, M_ADDRESS,
       M_CLEAR, M_ENGINE = 100 };

static void build_menu ()
{
	static char t[2048];
	int n = snprintf (t, sizeof t,
		"MFichier\n" "I%d\tNouvel onglet\tCtrl+T\n" "I%d\tNouvelle fenêtre\n" "I%d\tFermer l'onglet\tCtrl+W\n" "-\n" "I%d\tQuitter\n"
		"MNavigation\n" "I%d\tPrécédente\tAlt+←\n" "I%d\tSuivante\tAlt+→\n" "I%d\tRecharger\tCtrl+R\n" "I%d\tAccueil\n" "-\n"
		"I%d\tOnglet suivant\tCtrl+Tab\n" "I%d\tOnglet précédent\tCtrl+Maj+Tab\n" "I%d\tAller à l'adresse\tCtrl+L\n"
		"MRéglages\n",
		M_NEWTAB, M_NEWWIN, M_CLOSETAB, M_QUIT, M_BACK, M_FWD, M_RELOAD, M_HOME, M_NEXTTAB, M_PREVTAB, M_ADDRESS);
	for (int i = 0; i < N_ENGINES && n < (int) sizeof t - 100; i++)
		n += snprintf (t + n, sizeof t - (size_t) n, "I%d\tRechercher avec %s\n", M_ENGINE + i, g_engines[i].name);
	snprintf (t + n, sizeof t - (size_t) n, "-\n" "I%d\tEffacer les cookies et données des sites…\n", M_CLEAR);
	ui_menu ((long) t);
	menu_checks ();
}

static void menu_checks ()
{
	for (int i = 0; i < N_ENGINES; i++)
		ui_menu_state (M_ENGINE + i, i == g_engine ? UMS_CHECKED : 0);
}

// ---- la fenetre -------------------------------------------------------------------------------------------

static void layout_widgets ()
{
	ui_w_move (g_url, 154, 6, g_w - 346, TB_H - 12);	// (a gauche du moteur de recherche et des boutons de la fenetre)
	ui_w_move (g_stb, g_w - 188, 0, 36, TB_H);
	ui_w_move (g_status, 8, g_h - SB_H, g_w - 16, SB_H);
}

// La fenetre a change de taille (bords, bouton agrandir : Elegant s'arrete au dock et a la barre de
// menus ; UIKit a deja un tampon neuf, blanc) : les widgets replaces, la page suit.
static void on_resize (long w, long h)
{
	g_w = (int) w;
	g_h = (int) h;
	ui_fill (0, 0, g_w, g_h, tok (TJ_FACE));
	layout_widgets ();
	ui_w_draw (0);
	draw_tabs ();
	ui_update_all ();
	g_pw = g_w;
	g_ph = g_h - PAGE_Y - SB_H;
	uint32_t *px = g_ownBuffer ? 0 : jet_page_area (0, PAGE_Y, g_pw, g_ph, &g_stride);
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
	if (g_tabScroll > scroll_max ()) g_tabScroll = scroll_max ();
	scroll_to_current ();
	draw_tabs ();
	cb_needsDisplay (0, 0, g_pw, g_ph);
}

// Un autre theme (Givre, Onyx Noir...) : tout redessine dans ses couleurs
static void on_theme ()
{
	ui_fill (0, 0, g_w, g_h, tok (TJ_FACE));
	ui_w_draw (0);
	draw_tabs ();
	ui_update_all ();
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

static bool g_wantSearchMenu;

static void on_widget (long w, long code)
{
	if (w == g_back) engine_back ();
	else if (w == g_fwd) engine_forward ();
	else if (w == g_reload) { if (g_cur >= 0 && g_tabs[(size_t) g_cur].loading) engine_stop (); else engine_reload (); }
	else if (w == g_home) engine_load (HOME);
	else if (w == g_searchBtn) g_wantSearchMenu = true;	// (le menu hors de l'evenement du widget)
	else if (w == g_url && code == UN_ACTIVATE)
	{
		const char *t = (const char *) ui_w_get_text (g_url);
		if (t && *t)
		{
			if (getenv ("WEBKIT_TRACE_NET"))	// (ce qui est tape : au journal avec --trace-net seulement)
			{
				std::string u = engine_typed_url (t);
				char b[600];
				snprintf (b, sizeof b, "typed: %.200s -> %.380s", t, u.c_str ());
				say (b);
			}
			engine_load (t);
		}
		focus_page ();
	}
}

static void next_tab (int step)
{
	int n = (int) g_tabs.size ();
	if (n > 1) show_tab (((g_cur + step) % n + n) % n);
}

static bool g_quit;

static void menu_command (int id)
{
	switch (id)
	{
	case M_NEWTAB: new_tab (); break;
	case M_NEWWIN: engine_new_window (HOME); break;
	case M_CLOSETAB: close_tab (g_cur); break;
	case M_QUIT: g_quit = true; break;
	case M_BACK: engine_back (); break;
	case M_FWD: engine_forward (); break;
	case M_RELOAD: engine_reload (); break;
	case M_HOME: engine_load (HOME); break;
	case M_NEXTTAB: next_tab (1); break;
	case M_PREVTAB: next_tab (-1); break;
	case M_ADDRESS: focus_address (); break;
	case M_CLEAR: clear_site_data (); break;
	default:
		if (id >= M_ENGINE && id < M_ENGINE + N_ENGINES) set_engine (id - M_ENGINE);
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

// modificateurs HID de Korin (gauche bits 0-3, droite 4-7 : Ctrl, Maj, Alt, GUI) -> ceux d'Onyx
static unsigned onyx_mods (int m)
{
	unsigned r = 0;
	if (m & 0x11) r |= OK_MOD_CTRL;
	if (m & 0x22) r |= OK_MOD_SHIFT;
	if (m & 0x44) r |= OK_MOD_ALT;
	return r;
}

// une touche de Korin (code HID, caractere Unicode) -> le code d'Onyx (0 : aucun)
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

// La lettre d'un raccourci Ctrl+lettre : celle de la position de la touche (HID, celle d'un clavier US),
// comme les autres programmes de Korin (Ctrl+L, et Ctrl+W de l'editeur : 0x1A) et comme le moteur.
static int shortcut_letter (int hid, int ch)
{
	(void) ch;
	if (hid >= 0x04 && hid <= 0x1D) return 'a' + hid - 0x04;
	return 0;
}

// Les raccourcis des onglets (avant la page et l'adresse) -> 1 si pris
static bool tab_shortcut (int hid, int ch, unsigned mods)
{
	if (!(mods & OK_MOD_CTRL)) return false;
	if (hid == 0x2B) { next_tab ((mods & OK_MOD_SHIFT) ? -1 : 1); return true; }	// Ctrl+Tab, Ctrl+Maj+Tab
	if (hid == 0x4E) { next_tab (1); return true; }				// Ctrl+PgDn
	if (hid == 0x4B) { next_tab (-1); return true; }			// Ctrl+PgUp
	int l = shortcut_letter (hid, ch);
	if (l == 't') { new_tab (); return true; }
	if (l == 'w') { close_tab (g_cur); return true; }
	return false;
}

static bool in_page (int x, int y) { return x >= 0 && x < g_pw && y >= PAGE_Y && y < PAGE_Y + g_ph; }
static bool in_tabs (int x, int y) { return x >= 0 && x < g_w && y >= TB_H && y < PAGE_Y; }

static void page_mouse (int kind, int x, int y, int code, unsigned mods)
{
	int px = x, py = y - PAGE_Y;
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

// La souris sur la rangee des onglets : survol, choisir, fermer (la croix, le bouton du milieu), < > +
static void tabs_mouse (int kind, int x, int y, int code)
{
	int area = tabs_area (), tw = tab_width ();
	int hot = -1, hotClose = 0, btn = -1;
	if (x < area)
	{
		int i = (x + g_tabScroll) / tw;
		if (i >= 0 && i < (int) g_tabs.size ())
		{
			hot = i;
			int lx = x + g_tabScroll - i * tw;
			hotClose = lx >= 6 && lx < 28;
		}
	}
	else
		for (int b = 0; b < 3; b++)
			if (x >= btn_x (b) && x < btn_x (b) + TAB_BTN) btn = b;
	(void) y;
	if (hot != g_tabHot || hotClose != g_tabHotClose || btn != g_btnHot)
	{
		g_tabHot = hot;
		g_tabHotClose = hotClose;
		g_btnHot = btn;
		g_tabsDirty = true;
	}
	ui_cursor (UC_ARROW);
	if (kind != 1) return;
	if (code & 4)					// le bouton du milieu : fermer
	{
		if (hot >= 0) close_tab (hot);
		return;
	}
	if (!(code & 1)) return;
	if (btn == 0 && g_tabScroll > 0) { g_tabScroll -= tw; if (g_tabScroll < 0) g_tabScroll = 0; g_tabsDirty = true; }
	else if (btn == 1 && g_tabScroll < scroll_max ()) { g_tabScroll += tw; if (g_tabScroll > scroll_max ()) g_tabScroll = scroll_max (); g_tabsDirty = true; }
	else if (btn == 2) new_tab ();
	else if (hot >= 0)
	{
		if (hotClose && (hot == g_cur || hot == g_tabHot)) close_tab (hot);
		else if (hot != g_cur) show_tab (hot);
	}
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
			if (g_tabHot >= 0 || g_btnHot >= 0) { g_tabHot = g_btnHot = -1; g_tabsDirty = true; }
			page_mouse (kind, x, y, code, mods);
			return;
		}
		if (in_tabs (x, y))
		{
			tabs_mouse (kind, x, y, code);
			return;
		}
		if (g_tabHot >= 0 || g_btnHot >= 0) { g_tabHot = g_btnHot = -1; g_tabsDirty = true; }
		break;
	case WEV_WHEEL:
		if (g_mouseInPage)
		{
			engine_wheel (g_mx, g_my, y, mods);	// (+12 : crans ; la position : celle du pointeur)
			return;
		}
		if (in_tabs (x, y) || g_tabHot >= 0)		// la molette sur la rangee : elle defile
		{
			g_tabScroll -= y * tab_width () / 2;
			if (g_tabScroll < 0) g_tabScroll = 0;
			if (g_tabScroll > scroll_max ()) g_tabScroll = scroll_max ();
			g_tabsDirty = true;
			return;
		}
		break;
	case WEV_KEY:
	{
		if (kind == 0) return;				// (relache : le moteur fait les deux)
		if (tab_shortcut (code, ch, mods)) return;
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
	case WEV_THEME:
		on_theme ();
		return;
	case WEV_MENU:
		menu_command (code);
		return;
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
	// Le dossier de travail : SD0:/, dans chaque process (les fils en heritent aussi). Les chemins sans volume
	// de WebKit (les donnees des sites : /home/jet/webkit, engine_webkit.cpp ; file:///x ; /Downloads) sont
	// alors sur SD0: quel que soit le dossier d'ou Jet est lance (le Terminal dans SD1:/...).
	chdir ("SD0:/");
	// Les traces (au journal du noyau ; les process web et reseau, lances avec cet environnement, les font
	// aussi) : --trace-media (une ligne par seconde d'une video : images decodees, peintes, sautees, tampons,
	// etat du lecteur ; aussi le fichier SD0:/etc/web-trace-media), --trace-net (chaque requete, et les
	// adresses entieres ; aussi SD0:/etc/web-trace-net). Sans elles, ni requetes ni adresses entieres.
	for (int i = 1; i < argc; i++)
		if (!strcmp (argv[i], "--trace-media"))
			setenv ("WEBKIT_TRACE_MEDIA", "1", 1);
		else if (!strcmp (argv[i], "--trace-net"))
			setenv ("WEBKIT_TRACE_NET", "1", 1);
	if (access ("SD0:/etc/web-trace-net", F_OK) == 0)
		setenv ("WEBKIT_TRACE_NET", "1", 1);
	if (engine_is_auxiliary (argc, argv))
		return engine_auxiliary_main (argc, argv);

	// Le compositeur (gpucomp sur GpuKit, repli CPU sans GPU) par defaut, comme Jet chez Onyx ; le
	// chemin logiciel (la page peinte et recopiee par le processeur) : --nogpu ou SD0:/etc/web-nogpu,
	// --gpu le demande quoi que dise le fichier.
	std::vector<const char *> urls;
#if defined(__x86_64__)
	// Korin PC : le compositeur sur le GPU Intel (gpucomp sur OpenGL ES par GlKit : tools/webkit/glue/gpucomp_gl.c ;
	// la page en tuiles, la video en couche, le defilement sans repeindre) quand le service du GPU repond ; sinon
	// le chemin logiciel (la page peinte par le processeur). SD0:/etc/web-nogpu, --nogpu : le chemin logiciel ;
	// SD0:/etc/web-gpu, --gpu : le compositeur quoi qu'il arrive (sans GPU : gpucomp compose sur le processeur, ou
	// sur softpipe avec SD0:/etc/web-gpu-soft).
	bool gpu = access ("SD0:/etc/web-gpu", F_OK) == 0 || (access ("SD0:/etc/web-nogpu", F_OK) != 0 && jet_intel_gpu ());
#else
	bool gpu = access ("SD0:/etc/web-nogpu", F_OK) != 0;
#endif
	for (int i = 1; i < argc; i++)
		if (strcmp (argv[i], "--gpu") == 0)
			gpu = true;
		else if (strcmp (argv[i], "--nogpu") == 0)
			gpu = false;
		else if (strncmp (argv[i], "--", 2) != 0)
			urls.push_back (argv[i]);
	if (urls.empty ())
		urls.push_back (HOME);
	engine_set_compositing (gpu);
	if (gpu)
		engine_set_frame_client (cb_frame);
	settings_load ();

	if (!jet_window_open (0, 0, "Jet", &g_w, &g_h))
	{
		say ("pas de fenetre (UIKit, Elegant)");
		return 1;
	}
	U = (struct ui_vars *) jet_ui_vars ();
	ui_fill (0, 0, g_w, g_h, tok (TJ_FACE));

	g_tb = ui_toolbar (0, 0, 150, TB_H);
	ui_w_style (g_tb, 1);		// (UTBS_NOLINE : it stops at the address bar)
	g_back = ui_toolbar_add (g_tb, (long) "←", 0, 0);
	g_fwd = ui_toolbar_add (g_tb, (long) "→", 0, 0);
	g_reload = ui_toolbar_add (g_tb, (long) "↻", 0, 0);
	g_home = ui_toolbar_add (g_tb, (long) "⌂", 0, 0);
	g_url = ui_textbox (154, 6, g_w - 346, TB_H - 12, 0);
	g_stb = ui_toolbar (g_w - 188, 0, 36, TB_H);
	ui_w_style (g_stb, 1);
	g_searchBtn = ui_toolbar_add (g_stb, (long) "⌕", 0, 0);	// (le moteur de recherche)
	search_apply ();
	g_status = ui_label (8, g_h - SB_H, g_w - 16, SB_H, (long) "", ULB_DIM);
	for (long w : { g_back, g_fwd, g_reload, g_home, g_url, g_searchBtn })
		ui_w_on (w, (long) on_widget, 0);
	ui_w_enable (g_back, 0);
	ui_w_enable (g_fwd, 0);
	ui_w_draw (0);
	draw_tabs ();
	ui_update_all ();

	g_pw = g_w;
	g_ph = g_h - PAGE_Y - SB_H;
	g_px = jet_page_area (0, PAGE_Y, g_pw, g_ph, &g_stride);
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
	build_menu ();
	status ("Démarrage de WebKit…");
	for (const char *u : urls)
		open_tab (u, (int) g_tabs.size (), g_tabs.empty ());

	unsigned tick = 0;
	for (;;)
	{
		long t = ui_event (g_frames > 0 ? 1 : 10);
		if (g_frames > 0)
			g_frames--;
		if (t == WEV_CLOSE || g_quit)
			break;
		if (t > 0)
			handle_event (t);
		else
			ui_w_idle ();
		engine_cycle ();
		close_dead_tabs ();
		if (g_wantSearchMenu)
		{
			g_wantSearchMenu = false;
			choose_search ();
		}
		show_pending_popup ();
		flush_paint ();
		// l'indicateur de chargement tourne (un pas toutes les ~80 ms)
		bool anyLoading = false;
		for (const ChromeTab &c : g_tabs)
			anyLoading |= c.loading;
		struct timespec ts;
		clock_gettime (CLOCK_MONOTONIC, &ts);
		unsigned step = (unsigned) (ts.tv_sec * 12 + ts.tv_nsec / 83333333);
		if (anyLoading && step != tick) { tick = step; g_spin++; g_tabsDirty = true; }
		if (g_tabsDirty)
			draw_tabs ();
	}
	jet_window_close ();
	return 0;
}
