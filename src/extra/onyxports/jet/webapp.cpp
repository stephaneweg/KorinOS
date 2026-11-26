// webapp.cpp -- une app web sur le moteur de Jet (Korin) : une fenêtre sans barre d'adresse ni onglets, la page seule,
// avec le nom, l'icône et la session de l'app. Lancée par une app qui n'a qu'un lanceur (src/apps/spotify : le lecteur
// web de Spotify, open.spotify.com, par Widevine ; docs/etude/MUSIQUE.md) :
//
//   /apps/jet.app/main --app=<nom> [--title=<titre>] [--gpu | --nogpu] [ADRESSE]
//
// Une coquille commune à toutes les apps web (Spotify ; Netflix, Disney+ plus tard) : chacune est un petit paquet,
// /apps/<nom>.app avec le lanceur commun (src/apps/spotify/main.c) et webapp.txt :
//   title  = Spotify                         le nom de la fenêtre
//   url    = https://open.spotify.com/       l'adresse de départ (ADRESSE la remplace)
//   sites  = spotify.com scdn.co             les domaines dont les nouvelles fenêtres restent dans l'app
//   player = spotify                         un lecteur connu (g_players) : la carte de lecture
//   maximize = 1                             la fenêtre agrandie au départ (la vidéo : Netflix)
//
// Les données des sites (cookies, stockage) : HOME/<nom>/webkit, à part de celles de Jet (engine_set_data_name) ; le
// module Widevine : celui de Jet (/res/widevine, posé par élévation : widevine.h), proposé au premier lancement s'il manque (cdmupdate). Les liens
// vers d'autres fenêtres (target=_blank, window.open) s'ouvrent dans Jet.
//
// Un lecteur connu (le nom de l'app : « spotify ») : son état est lu dans la page une fois par seconde (un script :
// titre, artiste, lecture ou pause, position) et ses boutons sont pressés par un script. Fermer la fenêtre pendant la
// lecture la réduit en carte en bas à droite, au-dessus des autres (comme le Media Player et Jamendo) ; la musique
// continue ; fermer la carte arrête tout. La page reste derrière la carte (ui_surface_overlay : la carte passe
// par-dessus la surface de la page).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
//
#include "engine.h"
#include "present.h"
#include "widevine.h"
#include <uikit.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

#define MINI_W		330
#define MINI_H		92
#define TOP_H		34		// la bande du haut : le nom de l'app, les boutons de la fenêtre (Elegant) à droite

// (codes de touches d'Onyx que le moteur attend : engine_webkit.cpp ; les mêmes que main.cpp)
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

// ---- les lecteurs connus ---------------------------------------------------------------------------------------
// state : un script qui rend « 1 ou 0 (en lecture) \x1f titre \x1f artiste \x1f position \x1f durée » ; les
// boutons : des scripts qui les pressent. Spotify : les data-testid de son lecteur web (la barre du bas) ; le titre
// par Media Session quand WebKit l'a, sinon par la barre.
struct Player { const char *app, *state, *play, *next, *prev; };

#define SP_Q "var q=function(i){return document.querySelector('[data-testid=\"'+i+'\"]')};"
#define SP_CLICK(id) "(function(){" SP_Q "var e=q('" id "');if(e)e.click();return e?'1':''})()"
static const Player g_players[] = {
	{ "spotify",
	  "(function(){" SP_Q "var m=navigator.mediaSession,d=m&&m.metadata,b=q('control-button-playpause'),"
	  "l=b?(b.getAttribute('aria-label')||''):'',s=m?m.playbackState:'';"
	  "var p=!!b&&(s=='playing'||(s!='paused'&&/pause/i.test(l)));var t=d?d.title:'',a=d?d.artist:'';"
	  "if(!t){var e=q('context-item-info-title');if(e)t=e.textContent;e=q('context-item-info-artist');if(e)a=e.textContent}"
	  "var f=function(i){var e=q(i);return e?e.textContent:''};"
	  "return (p?'1':'0')+'\\x1f'+t+'\\x1f'+a+'\\x1f'+f('playback-position')+'\\x1f'+f('playback-duration')})()",
	  SP_CLICK ("control-button-playpause"), SP_CLICK ("control-button-skip-forward"), SP_CLICK ("control-button-skip-back") },
};

// ---- l'état ----------------------------------------------------------------------------------------------------
static struct ui_vars *U;
static std::string g_name, g_title, g_url;
static std::vector<std::string> g_sites;
static bool g_maximize;		// (webapp.txt : maximize = 1)
static const Player *g_player;
static int g_winW, g_winH;			// la fenêtre
static int g_w, g_h;				// la page (sous la bande du haut)
static uint32_t *g_px;				// les pixels de la page (la surface, ou un tampon à soi)
static int g_stride;
static bool g_ownBuffer;
static int g_d0x, g_d0y, g_d1x, g_d1y;		// la zone à repeindre (g_d1x 0 : rien)
static int g_buttons, g_mx, g_my, g_frames;
static int g_tab;				// l'onglet du moteur (0 : aucun)
static bool g_quit, g_mini;
static int g_restore[2], g_restorePos[2] = { -1, -1 };
static EngineClient g_client;

struct PlayState { bool playing; std::string title, artist, pos, dur; };
static PlayState g_play;
static bool g_stateBusy;
static double g_lastState;

static void say (const char *s)
{
	char b[600];
	int n = snprintf (b, sizeof b, "webapp: %s\n", s);
	if (n > (int) sizeof b - 1) n = (int) sizeof b - 1;
	fwrite (b, 1, (size_t) n, stderr);
	fflush (stderr);
}

static double now ()
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static uint32_t tok (int j)
{
	const uint32_t *t = (const uint32_t *) ui_tokens ();
	return t ? t[j / 4] : 0x808080;
}

// ---- la page ---------------------------------------------------------------------------------------------------

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
	if (g_d1x == 0 || g_px == 0 || g_mini) return;	// (la carte : la page reste derrière, à son ancienne taille)
	int x = g_d0x < 0 ? 0 : g_d0x, y = g_d0y < 0 ? 0 : g_d0y;
	int x1 = g_d1x > g_w ? g_w : g_d1x, y1 = g_d1y > g_h ? g_h : g_d1y;
	g_d1x = 0;
	if (x1 <= x || y1 <= y) return;
	engine_paint (g_px, g_stride, x, y, x1 - x, y1 - y);
	jet_present (g_px, g_stride, x, y, x1 - x, y1 - y);
}

static void clear_page ()
{
	uint32_t c = tok (TJ_FACE);
	for (int y = 0; y < g_h; y++)
		for (int x = 0; x < g_w; x++)
			g_px[(long) y * g_stride + x] = c;
	jet_present (g_px, g_stride, 0, 0, g_w, g_h);
}

// un mot sur la page encore vide (le module Widevine se télécharge...)
static void page_note (const char *text)
{
	clear_page ();
	long w = ui_text_width ((long) text, 14, (long) U->font);
	// (dans le tampon de la fenêtre : la surface vide est transparente là où rien n'est peint ; une zone par-dessus)
	int y0 = TOP_H + g_h / 2 - 30;
	ui_fill (0, y0, g_w, 50, tok (TJ_FACE));
	ui_text ((long) text, (g_w - w) / 2, y0 + 30, tok (TJ_TEXT2), 14, (long) U->font, 0);
	ui_update (0, y0, g_w, 50);
	uint16_t z[4] = { 0, (uint16_t) y0, (uint16_t) g_w, 50 };
	ui_surface_overlay ((long) z, text[0] ? 1 : 0);
}

static int cb_frame (int buffer, int pitch, long offset, int x, int y, int w, int h)
{
	g_frames = 100;
	return jet_page_show (buffer, pitch, offset, x, y, w, h);
}

// la bande du haut : le nom de l'app à gauche (les boutons de la fenêtre sont à droite, dessinés par Elegant)
static void draw_top ()
{
	ui_fill (0, 0, g_winW, TOP_H, tok (TJ_FACE));
	ui_fill (0, TOP_H - 1, g_winW, 1, tok (TJ_LINE));
	ui_text_fit ((long) g_title.c_str (), 14, TOP_H / 2 + 5, tok (TJ_TEXT1), 13, (long) U->font_b, g_winW - FRAME_CAP_W - 28);
	ui_update (0, 0, g_winW, TOP_H);
}

static void show_title (const char *t) { jet_window_title (t && *t ? t : g_title.c_str ()); }
static void cb_title (int, const char *t) { if (!g_mini) show_title (t); }
static void cb_url (int, const char *) { }
static void cb_loading (int, bool, double) { }
static void cb_history (int, bool, bool) { }
static void cb_status (const char *) { }

// url sur un des domaines de l'app (sites de webapp.txt) ?
static bool own_site (const std::string &url)
{
	size_t b = url.find ("://");
	if (b == std::string::npos) return false;
	b += 3;
	size_t e = url.find_first_of ("/:?#", b);
	std::string host = url.substr (b, e == std::string::npos ? std::string::npos : e - b);
	for (const std::string &s : g_sites)
		if (host.size () >= s.size () && host.compare (host.size () - s.size (), s.size (), s) == 0
		    && (host.size () == s.size () || host[host.size () - s.size () - 1] == '.'))
			return true;
	return false;
}

// une autre fenêtre (target=_blank, window.open) : sur les domaines de l'app, dans sa fenêtre ; ailleurs, dans Jet
static void cb_open (int, const char *url, bool)
{
	if (!url || !*url) return;
	if (own_site (url)) engine_load (url);
	else engine_new_window (url);
}

// /apps/<nom>.app/webapp.txt (« clé = valeur », # : commentaire)
static void read_config (const char *name, std::string &title, std::string &url, std::string &player)
{
	std::string p = std::string ("/apps/") + name + ".app/webapp.txt";
	FILE *f = fopen (p.c_str (), "rb");
	if (!f) return;
	char line[1024];
	while (fgets (line, sizeof line, f))
	{
		std::string l = line;
		while (!l.empty () && (l.back () == '\n' || l.back () == '\r' || l.back () == ' ' || l.back () == '\t')) l.pop_back ();
		size_t eq = l.find ('=');
		if (l.empty () || l[0] == '#' || eq == std::string::npos) continue;
		std::string k = l.substr (0, eq), v = l.substr (eq + 1);
		while (!k.empty () && (k.back () == ' ' || k.back () == '\t')) k.pop_back ();
		size_t vb = v.find_first_not_of (" \t");
		v = vb == std::string::npos ? "" : v.substr (vb);
		if (k == "title") title = v;
		else if (k == "url") url = v;
		else if (k == "player") player = v;
		else if (k == "maximize") g_maximize = v == "1" || v == "oui" || v == "yes";
		else if (k == "sites")
		{
			size_t i = 0;
			while (i < v.size ())
			{
				size_t j = v.find_first_of (" \t,", i);
				if (j == std::string::npos) j = v.size ();
				if (j > i) g_sites.push_back (v.substr (i, j - i));
				i = j + 1;
			}
		}
	}
	fclose (f);
}

static void cb_failed (int, const char *url, const char *why)
{
	char b[512];
	snprintf (b, sizeof b, "load failed: %s (%s)", url ? url : "", why ? why : "");
	say (b);
}

static bool g_dead;
static void cb_ended (int) { g_dead = true; }
static void cb_alert (const char *t) { ui_message ((long) g_title.c_str (), (long) (t ? t : ""), UM_OK); }
static bool cb_confirm (const char *t) { return ui_message ((long) g_title.c_str (), (long) (t ? t : ""), UM_OKCANCEL) == 1; }

static std::vector<std::string> g_popItems;
static int g_popX = -1, g_popY, g_popW;

static void cb_popup (const char *const *items, const unsigned char *, int count, int, int x, int y, int w, int h)
{
	g_popItems.clear ();
	for (int i = 0; i < count; i++)
		g_popItems.push_back (items[i] ? items[i] : "");
	g_popX = x; g_popY = TOP_H + y + h; g_popW = w < 120 ? 120 : w;
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

static void enter_mini (bool on);

static void cb_key_not_handled (long key, unsigned mods)
{
	if ((mods & OK_MOD_CTRL) && key == 18) engine_reload ();		// Ctrl+R
	else if (key == OK_KEY_F1 + 4) engine_reload ();			// F5
	else if ((mods & OK_MOD_ALT) && key == OK_KEY_LEFT) engine_back ();
	else if ((mods & OK_MOD_ALT) && key == OK_KEY_RIGHT) engine_forward ();
	else if ((mods & OK_MOD_CTRL) && key == 17) g_quit = true;		// Ctrl+Q
	else if ((mods & OK_MOD_CTRL) && key == 23)				// Ctrl+W : comme fermer la fenêtre
	{
		if (g_play.playing) enter_mini (true);
		else g_quit = true;
	}
}

// ---- le lecteur ------------------------------------------------------------------------------------------------

static void draw_card ();

static void state_done (void *, const char *r)
{
	g_stateBusy = false;
	PlayState s;
	std::string a = r ? r : "";
	std::vector<std::string> f;
	size_t b = 0;
	for (;;)
	{
		size_t e = a.find ('\x1f', b);
		f.push_back (a.substr (b, e == std::string::npos ? std::string::npos : e - b));
		if (e == std::string::npos) break;
		b = e + 1;
	}
	if (f.size () < 5) return;
	s.playing = f[0] == "1";
	s.title = f[1]; s.artist = f[2]; s.pos = f[3]; s.dur = f[4];
	bool changed = s.playing != g_play.playing || s.title != g_play.title || s.artist != g_play.artist || s.pos != g_play.pos;
	if (s.title != g_play.title || s.playing != g_play.playing)
	{
		char m[300];
		snprintf (m, sizeof m, "player: %s \"%s\" (%s)", s.playing ? "playing" : "paused", s.title.c_str (), s.artist.c_str ());
		say (m);
	}
	g_play = s;
	if (changed && g_mini) draw_card ();
}

static void poll_state ()
{
	if (!g_player || !g_tab || g_stateBusy) return;
	double t = now ();
	if (t - g_lastState < 1) return;
	g_lastState = t;
	g_stateBusy = true;
	engine_run_script (g_player->state, state_done, nullptr);
}

static void clicked (void *, const char *) { g_lastState = 0; }	// (l'état relu au tour suivant)

static void player_do (const char *js)
{
	if (g_player && g_tab && js) engine_run_script (js, clicked, nullptr);
}

// ---- la carte --------------------------------------------------------------------------------------------------

enum { H_PREV = 1, H_PLAY, H_NEXT, H_EXPAND };
struct Hit { int x, y, w, h, kind; };
static std::vector<Hit> g_hits;
static int g_hot = -1;

static void tri (int x, int y, int s, bool left, uint32_t c)
{
	int32_t p[6];
	if (left) { p[0] = (x + s) * 16; p[1] = y * 16; p[2] = (x + s) * 16; p[3] = (y + s) * 16; p[4] = x * 16; p[5] = (y + s / 2) * 16; }
	else { p[0] = x * 16; p[1] = y * 16; p[2] = x * 16; p[3] = (y + s) * 16; p[4] = (x + s) * 16; p[5] = (y + s / 2) * 16; }
	ui_vp_poly ((long) p, 3, c);
}

static void draw_card ()
{
	int W = (int) U->w, H = (int) U->h;
	uint32_t bg = tok (TJ_SURFACE), ink = tok (TJ_TEXT1), dim = tok (TJ_TEXT2), acc = tok (TJ_ACCENT);
	ui_fill (0, 0, W, H, bg);
	g_hits.clear ();
	auto hot = [&] (int k) { return g_hot >= 0 && g_hot < (int) g_hits.size () && g_hits[(size_t) g_hot].kind == k; };
	int by = 52, bx = 14;
	g_hits.push_back ({ bx, by, 32, 32, H_PREV });
	tri (bx + 9, by + 9, 14, true, hot (H_PREV) ? acc : ink);
	ui_fill (bx + 7, by + 9, 2, 14, hot (H_PREV) ? acc : ink);
	int px = bx + 38;
	g_hits.push_back ({ px, by - 2, 36, 36, H_PLAY });
	ui_vp_disc ((px + 18) * 16, (by + 16) * 16, 18 * 16, acc);
	if (g_play.playing) { ui_fill (px + 12, by + 8, 4, 16, 0xFFFFFF); ui_fill (px + 20, by + 8, 4, 16, 0xFFFFFF); }
	else tri (px + 13, by + 8, 16, false, 0xFFFFFF);
	int nx = px + 44;
	g_hits.push_back ({ nx, by, 32, 32, H_NEXT });
	tri (nx + 9, by + 9, 14, false, hot (H_NEXT) ? acc : ink);
	ui_fill (nx + 23, by + 9, 2, 14, hot (H_NEXT) ? acc : ink);
	int tx = 14, tw = W - tx - 40;
	bool has = !g_play.title.empty ();
	ui_text_fit ((long) (has ? g_play.title.c_str () : g_title.c_str ()), tx, 24, ink, 14, (long) U->font_b, tw);
	if (has) ui_text_fit ((long) g_play.artist.c_str (), tx, 42, dim, 12, (long) U->font, tw);
	if (!g_play.pos.empty ())
	{
		std::string t = g_play.pos + (g_play.dur.empty () ? "" : " / " + g_play.dur);
		ui_text_fit ((long) t.c_str (), nx + 44, by + 21, dim, 12, (long) U->font, W - nx - 58);
	}
	g_hits.push_back ({ W - 32, 6, 26, 26, H_EXPAND });
	uint32_t c = hot (H_EXPAND) ? acc : dim;
	ui_frame (W - 26, 12, 14, 12, c);
	ui_fill (W - 26, 12, 14, 3, c);
	ui_update_all ();
	uint16_t z[4] = { 0, 0, (uint16_t) W, (uint16_t) H };	// (par-dessus la surface de la page)
	ui_surface_overlay ((long) z, 1);
}

static void card_mouse (int kind, int x, int y, int code)
{
	int nh = -1;
	for (size_t i = 0; i < g_hits.size (); i++)
	{
		const Hit &h = g_hits[i];
		if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) nh = (int) i;
	}
	if (nh != g_hot) { g_hot = nh; draw_card (); }
	if (kind != 1 || !(code & 1) || nh < 0) return;
	switch (g_hits[(size_t) nh].kind)
	{
	case H_PREV: player_do (g_player ? g_player->prev : nullptr); break;
	case H_PLAY: player_do (g_player ? g_player->play : nullptr); g_play.playing = !g_play.playing; draw_card (); break;
	case H_NEXT: player_do (g_player ? g_player->next : nullptr); break;
	case H_EXPAND: enter_mini (false); break;
	}
}

// ---- la fenêtre ------------------------------------------------------------------------------------------------

static void page_area ()
{
	uint32_t *px = g_ownBuffer ? 0 : jet_page_area (0, TOP_H, g_w, g_h, &g_stride);
	if (px == 0)
	{
		if (g_ownBuffer) free (g_px);
		px = (uint32_t *) calloc ((size_t) g_w * g_h, 4);
		g_stride = g_w;
		g_ownBuffer = true;
	}
	g_px = px;
}

static void on_resize (long w, long h)
{
	if (g_mini) { draw_card (); return; }
	g_winW = (int) w;
	g_winH = (int) h;
	g_w = g_winW;
	g_h = g_winH - TOP_H > 1 ? g_winH - TOP_H : 1;
	ui_fill (0, 0, g_winW, g_winH, tok (TJ_FACE));
	ui_update_all ();
	draw_top ();
	page_area ();
	clear_page ();
	engine_resize (g_w, g_h);
	cb_needsDisplay (0, 0, g_w, g_h);
}

static void enter_mini (bool on)
{
	if (on == g_mini) return;
	g_hot = -1;
	if (on)
	{
		g_mini = true;
		g_restore[0] = g_winW;
		g_restore[1] = g_winH;
		if (ui_window_pos ((long) g_restorePos) < 0) g_restorePos[0] = -1;
		engine_set_active (false);
		ui_min_size (MINI_W, MINI_H);
		ui_resize (MINI_W, MINI_H);
		long scr[4] = { 0, 0, 0, 0 };
		if (ui_screen ((long) scr) >= 0 && scr[0] > 0)
			ui_window_place (scr[0] - MINI_W - FRAME_BORDER - 16, scr[1] - scr[3] - MINI_H - FRAME_BORDER - 16);
		ui_window_top (1);
		jet_window_title (g_title.c_str ());
		draw_card ();
		say ("card");
	}
	else
	{
		g_mini = false;
		ui_surface_overlay (0, 0);
		ui_window_top (0);
		ui_min_size (420, 300);
		ui_resize (g_restore[0], g_restore[1]);
		if (g_restorePos[0] >= 0) ui_window_place (g_restorePos[0], g_restorePos[1]);
		on_resize (U->w, U->h);
		engine_set_active (true);
		show_title (nullptr);
		say ("window");
	}
}

// modificateurs HID de Korin -> ceux d'Onyx ; une touche -> le code d'Onyx (comme main.cpp)
static unsigned onyx_mods (int m)
{
	unsigned r = 0;
	if (m & 0x11) r |= OK_MOD_CTRL;
	if (m & 0x22) r |= OK_MOD_SHIFT;
	if (m & 0x44) r |= OK_MOD_ALT;
	return r;
}

static long onyx_key (int hid, int ch, unsigned mods)
{
	if ((mods & OK_MOD_CTRL) && hid >= 0x04 && hid <= 0x1D) return hid - 0x04 + 1;
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
	if (hid >= 0x3A && hid <= 0x45) return OK_KEY_F1 + (hid - 0x3A);
	return ch >= 32 ? ch : 0;
}

static void page_mouse (int kind, int x, int y, int code, unsigned mods)
{
	g_mx = x; g_my = y;
	if (kind == 0)
	{
		engine_mouse (x, y, g_buttons, 0, mods);
		ui_cursor (engine_cursor () == ENGINE_CUR_HAND ? UC_HAND : engine_cursor () == ENGINE_CUR_TEXT ? UC_TEXT : UC_ARROW);
		return;
	}
	int changed;
	if (kind == 1)
	{
		changed = code & ~g_buttons & 7;
		if (!changed) changed = 1;
	}
	else
	{
		changed = g_buttons & ~code & 7;
		if (!changed) changed = g_buttons ? g_buttons : 1;
	}
	for (int bit = 1; bit <= 4; bit <<= 1)
		if (changed & bit)
		{
			g_buttons ^= bit;
			engine_mouse (x, y, g_buttons & 7, bit, mods);
		}
}

// ---- le module Widevine ----------------------------------------------------------------------------------------

static pid_t g_wvPid = -1;
static int g_wvOut = -1;
static std::string g_wvText;

// (cdmupdate élevé : le module va dans /res/widevine, widevine.h) -> 1 lancé, 0 échec, -2 élévation refusée
static int wv_start ()
{
	int out = -1;
	pid_t pid = wv_spawn (nullptr, true, ("Jet veut installer le module Widevine de Google dans le système "
		"(/res/widevine, pour tous les comptes), pour " + g_title + ".").c_str (), &out);
	if (pid == -2) return -2;
	if (pid < 0) return 0;
	g_wvPid = pid;
	g_wvOut = out;
	return 1;
}

// -> true quand cdmupdate a fini (ou ne tourne pas)
static bool wv_poll ()
{
	if (g_wvPid <= 0) return true;
	char b[1024];
	ssize_t n;
	while ((n = read (g_wvOut, b, sizeof b)) > 0)
		g_wvText.append (b, (size_t) n);
	int st = 0;
	if (waitpid (g_wvPid, &st, WNOHANG) != g_wvPid) return false;
	while ((n = read (g_wvOut, b, sizeof b)) > 0)
		g_wvText.append (b, (size_t) n);
	close (g_wvOut);
	g_wvOut = -1;
	g_wvPid = -1;
	bool ok = WIFEXITED (st) && WEXITSTATUS (st) == 0;
	std::string last = g_wvText;
	while (!last.empty () && (last.back () == '\n' || last.back () == '\r')) last.pop_back ();
	size_t nl = last.rfind ('\n');
	if (nl != std::string::npos) last = last.substr (nl + 1);
	fprintf (stderr, "webapp: widevine: %s (%s)\n", ok ? "ok" : "failed", last.c_str ());
	if (!ok)
		ui_message ((long) g_title.c_str (), (long) ("Le module Widevine n'a pas pu être installé :\n" + last +
			"\n\nLa page s'ouvre quand même, mais ne jouera pas.").c_str (), UM_OK);
	return true;
}

// au démarrage : le module manque -> le proposer -> true si son téléchargement commence
static bool wv_offer ()
{
	if (!engine_protected_content ())
	{
		ui_message ((long) g_title.c_str (), (long) "Ce Jet ne lit pas les contenus protégés (Widevine) : la page s'ouvre, "
			"mais ne jouera pas.", UM_OK);
		return false;
	}
	wv_forget_old ();	// (un module d'avant, du dossier du compte : plus jamais chargé)
	if (wv_installed ()) return false;
	std::string q = g_title + " joue par le module Widevine de Google (contenus protégés), un logiciel propriétaire que seul "
		"Google distribue.\n\nLe télécharger chez Google (environ 22 Mo) et l'installer dans le système (/res/widevine, "
		"pour tous les comptes) ? Une autorisation est demandée. Jet s'en sert aussi (Réglages, « Lire les contenus "
		"protégés »).";
	if (ui_message ((long) g_title.c_str (), (long) q.c_str (), UM_OKCANCEL) != 1) return false;
	int r = wv_start ();
	if (r == 1) return true;
	if (r == -2)
	{
		ui_message ((long) g_title.c_str (), (long) "Autorisation refusée : la page s'ouvre, mais ne jouera pas.", UM_OK);
		return false;
	}
	ui_message ((long) g_title.c_str (), (long) "Le téléchargement n'a pas pu commencer (" WV_UPDATER ").", UM_OK);
	return false;
}

// ---- les menus -------------------------------------------------------------------------------------------------

enum { M_PLAY = 1, M_NEXT, M_PREV, M_MINI, M_RELOAD, M_INJET, M_LOGOUT, M_QUIT, M_BACK, M_MAXIMIZE };

static void open_page ()
{
	if (g_tab) return;
	g_tab = engine_tab_new (g_url.c_str ());
	if (g_tab) engine_tab_show (g_tab);
	page_note ("");
	clear_page ();
	cb_needsDisplay (0, 0, g_w, g_h);
}

static void logged_out () { if (g_tab) engine_load (g_url.c_str ()); }

static void command (long id)
{
	switch (id)
	{
	case M_PLAY: if (g_player) { player_do (g_player->play); g_play.playing = !g_play.playing; } break;
	case M_NEXT: if (g_player) player_do (g_player->next); break;
	case M_PREV: if (g_player) player_do (g_player->prev); break;
	case M_MINI: enter_mini (!g_mini); break;
	case M_RELOAD: engine_reload (); break;
	case M_BACK: engine_back (); break;
	case M_INJET: engine_new_window (g_url.c_str ()); break;
	case M_LOGOUT:
		if (ui_message ((long) g_title.c_str (), (long) "Se déconnecter et effacer les données de cette app (cookies, "
				"stockage) ? Celles de Jet ne changent pas.", UM_OKCANCEL) == 1)
			engine_clear_site_data (logged_out);
		break;
	case M_MAXIMIZE: if (!g_mini) ui_maximize (); break;
	case M_QUIT: g_quit = true; break;
	}
	if (g_mini) draw_card ();
}

static void build_menu ()
{
	static char t[1024];
	int n = snprintf (t, sizeof t, "MFichier\n" "I%d\tRecharger\tCtrl+R\n" "I%d\tPrécédente\tAlt+←\n" "I%d\tAgrandir\tF11\n"
		"I%d\tOuvrir dans Jet\n" "-\n" "I%d\tSe déconnecter (effacer les données)…\n" "-\n" "I%d\tQuitter\tCtrl+Q\n",
		M_RELOAD, M_BACK, M_MAXIMIZE, M_INJET, M_LOGOUT, M_QUIT);
	if (g_player)
		snprintf (t + n, sizeof t - (size_t) n, "MLecture\n" "I%d\tLecture / pause\n" "I%d\tSuivant\n" "I%d\tPrécédent\n" "-\n"
			"I%d\tRéduire en carte\tCtrl+W\n", M_PLAY, M_NEXT, M_PREV, M_MINI);
	ui_menu ((long) t);
}

// ---- la boucle -------------------------------------------------------------------------------------------------

int webapp_main (const char *name, const char *title, const char *url, bool gpu)
{
	g_name = name;
	std::string cTitle, cUrl, cPlayer;
	read_config (name, cTitle, cUrl, cPlayer);
	g_title = title && *title ? title : !cTitle.empty () ? cTitle : name;
	g_url = url && *url ? url : !cUrl.empty () ? cUrl : "about:blank";
	if (cPlayer.empty ()) cPlayer = name;
	for (const Player &p : g_players)
		if (cPlayer == p.app) g_player = &p;
	char m[300];
	snprintf (m, sizeof m, "%s (%s), data in HOME/%s, %s", g_title.c_str (), g_url.c_str (), name, gpu ? "GPU" : "software");
	say (m);
	engine_set_data_name (name);
	// la feuille de style de l'app, s'il y en a une (/apps/<nom>.app/user.css) : ajoutée au cadre principal de chaque page,
	// avant celles du site (TF1+ : la bascule de pointer-events sur <body> rendue sans effet, engine.h)
	{
		std::string p = std::string ("/apps/") + name + ".app/user.css";
		if (FILE *f = fopen (p.c_str (), "rb"))
		{
			std::string css;
			char b[4096];
			size_t n;
			while ((n = fread (b, 1, sizeof b, f)) > 0 && css.size () < 65536)
				css.append (b, n);
			fclose (f);
			if (!css.empty ())
			{
				engine_set_user_css (css.c_str ());
				snprintf (m, sizeof m, "user.css (%u bytes) in every page", (unsigned) css.size ());
				say (m);
			}
		}
	}
	// (les process de WebKit lancés par le lanceur de l'app, qui redevient Jet sous le nom de l'app : main.c)
	std::string launcher = std::string ("/apps/") + name + ".app/main";
	if (access (launcher.c_str (), X_OK) == 0) engine_set_helper_path (launcher.c_str ());
	engine_set_compositing (gpu);
	if (gpu) engine_set_frame_client (cb_frame);
	if (!jet_window_open (0, 0, g_title.c_str (), &g_winW, &g_winH))
	{
		say ("no window (UIKit, Elegant)");
		return 1;
	}
	U = (struct ui_vars *) jet_ui_vars ();
	g_w = g_winW;
	g_h = g_winH - TOP_H > 1 ? g_winH - TOP_H : 1;
	ui_fill (0, 0, g_winW, g_winH, tok (TJ_FACE));
	ui_update_all ();
	draw_top ();
	page_area ();
	if (g_px == 0) return 1;
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
	engine_set_webapp (true);	// (E58)
	if (!engine_init (&g_client, g_w, g_h))
	{
		ui_message ((long) g_title.c_str (), (long) "WebKit n'a pas pu démarrer.", UM_OK);
		return 1;
	}
	ui_on_resize ((long) on_resize, 0);
	build_menu ();
	if (g_maximize) ui_maximize ();		// (on_resize suit)
	if (wv_offer ()) page_note ("Téléchargement du module Widevine chez Google…");
	else open_page ();

	for (;;)
	{
		long t = ui_event (g_frames > 0 ? 1 : 10);
		if (g_frames > 0) g_frames--;
		if (t == WEV_CLOSE)
		{
			if (!g_mini && g_play.playing) enter_mini (true);
			else break;
		}
		if (g_quit) break;
		int kind = ui_ev_field (U, EV_KIND), x = ui_ev_field (U, EV_X), y = ui_ev_field (U, EV_Y);
		int code = ui_ev_field (U, EV_CODE), ch = ui_ev_field (U, EV_CHAR);
		unsigned mods = onyx_mods (ui_ev_field (U, EV_MODS));
		switch (t)
		{
		case WEV_MOUSE:
			if (g_mini) card_mouse (kind, x, y, code);
			else if (g_tab && (y >= TOP_H || g_buttons)) page_mouse (kind, x, y - TOP_H, code, mods);
			else if (y < TOP_H) ui_cursor (UC_ARROW);
			break;
		case WEV_WHEEL:
			if (!g_mini && g_tab) engine_wheel (g_mx, g_my, y, mods);
			break;
		case WEV_KEY:
			if (kind == 0) break;
			if (g_mini)
			{
				if (code == 0x2C) command (M_PLAY);			// Espace
				else if (code == 0x28) enter_mini (false);		// Entrée
				break;
			}
			if ((mods & OK_MOD_CTRL) && code == 0x1A) { cb_key_not_handled (23, OK_MOD_CTRL); break; }	// Ctrl+W
			if ((mods & OK_MOD_CTRL) && code == 0x14) { g_quit = true; break; }			// Ctrl+Q
			if (code == 0x44 && !mods) { command (M_MAXIMIZE); break; }				// F11
			if (g_tab)
			{
				long k = onyx_key (code, ch, mods);
				if (k) engine_key (k, mods);
			}
			break;
		case WEV_FOCUS:
			if (!g_mini) engine_set_active (kind != 0);
			break;
		case WEV_THEME:
			if (g_mini) draw_card ();
			else { draw_top (); cb_needsDisplay (0, 0, g_w, g_h); }
			break;
		case WEV_MENU:
			command (code);
			break;
		default:
			// (le reste à UIKit : WEV_RESIZE -- les bords, le bouton agrandir, ui_maximize -- appelle on_resize)
			if (t > 0) ui_w_event (0);
			break;
		}
		if (g_quit) break;
		if (g_wvPid > 0 && wv_poll ()) open_page ();
		engine_cycle ();
		if (g_dead && g_tab)			// (le process web est mort : la page de nouveau)
		{
			g_dead = false;
			say ("web process ended: the page again");
			engine_tab_close (g_tab);
			g_tab = 0;
			g_play = PlayState ();
			if (g_mini) enter_mini (false);
			open_page ();
		}
		show_pending_popup ();
		poll_state ();
		flush_paint ();
	}
	if (g_tab) engine_tab_close (g_tab);
	jet_window_close ();
	return 0;
}
