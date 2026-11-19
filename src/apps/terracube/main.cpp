//
// TerraCube -- un bac à sable de blocs pour Korin OS (le PC d'abord : le GPU Intel par GlKit, softpipe sans lui).
//
// Un monde sans bord fait de blocs d'un mètre : on s'y promène, on casse et on pose des blocs, on vole ; le monde est
// sauvé dans HOME:/terracube/worlds/<nom>. En option, deux façons de le piloter de l'extérieur :
//   - les scripts (Python, BASIC, tout ce qui ouvre une connexion TCP) : port 4730, une commande par ligne
//     (api.cpp ; data/python/terracube.py, data/basic/terracube.bas) ;
//   - un agent IA (Claude Code, Vibe…) par MCP : port 7312, http://<adresse>:7312/mcp, avec un jeton (mcptools.cpp).
//
//   terracube                       le dernier monde (ou un nouveau)
//   --world nom [--seed n]          ce monde (créé s'il n'existe pas)
//   --distance n                    distance de vue en tronçons (2 à 16)
//   --no-api                        pas de serveur de scripts ; --api-any : ouvert au réseau (sinon 127.0.0.1)
//   --mcp [port]                    le pilotage par un agent dès le départ ; --mcp-token t (« - » : sans jeton)
//   --selftest [s]                  des vérifications écrites sur la sortie (essais sous QEMU), code 0 si tout passe ;
//                                   s : la fenêtre reste ouverte s secondes ensuite
//
// C++ sur UIKit et GlKit ; les couleurs de l'interface viennent des jetons du thème (le HUD), celles du monde sont
// des données du jeu (textures.cpp, ciel).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
//
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>
#include <uikit.h>
#include <netkit_abi.h>
#include <armgl.h>
#include <GLES3/gl3.h>
#include "glview.h"
#include "game.h"
#include "mcp.h"
#include "player.h"
#include "render.h"
#include "../../lib/keyvault/keyvault.h"

#define TR(s) ((const char *) ui_tr ((long) (s)))

namespace tc {

Game g;

namespace {

struct ui_vars ui;
Renderer R;
void *g_ctx;
struct gl_view g_view;
int g_mode;			// ARMGL_GPU / ARMGL_SOFT
long g_win, g_winMain;		// la fenêtre (plein écran : une autre)
bool g_full;
bool g_locked;			// la souris est au jeu
bool g_focus = true;
bool g_keys[256];
unsigned g_buttons;
double g_breakAt, g_placeAt;	// répétition des clics tenus
float g_mdx, g_mdy;
bool g_showInfo = true, g_showDebug;
std::string g_flash;		// le nom du bloc choisi, un instant
double g_flashT;
double g_fps;
Hit g_target;
bool g_selftest;
const unsigned *g_jet;

unsigned jet (int off) { return g_jet[off / 4] & 0xFFFFFF; }

} // namespace

double nowMs ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

void gamePost (const std::string &text)
{
	g.chat.push_back ({ text, nowMs () });
	while (g.chat.size () > 40) g.chat.pop_front ();
	if (g_selftest) printf ("terracube : [chat] %s\n", text.c_str ());
}

void gameFlyChanged ()
{
	g.pl.vy = 0;
	ui_menu_state (14, g.pl.fly ? UMS_CHECKED : 0);
}

void gameTeleport (double x, double y, double z)
{
	if (y < 0) y = 0;
	if (y > CH + 40) y = CH + 40;
	g.pl.x = x, g.pl.y = y, g.pl.z = z;
	g.pl.vx = g.pl.vy = g.pl.vz = 0;
	g.world.ensure (floorDiv ((int) floor (x), CW), floorDiv ((int) floor (z), CW));
}

// ---- réglages, mondes -----------------------------------------------------------------------------------------------

namespace {

const char *const CONF = "HOME:/terracube/terracube.ini";

void mkdirsTo (const std::string &p)
{
	mkdir ("HOME:/terracube", 0755);
	mkdir ("HOME:/terracube/worlds", 0755);
	mkdir (p.c_str (), 0755);
}

bool readIni (const std::string &path, std::vector<std::pair<std::string, std::string>> &kv)
{
	FILE *f = fopen (path.c_str (), "r");
	if (!f) return false;
	char line[512];
	while (fgets (line, sizeof line, f)) {
		std::string l = line;
		while (!l.empty () && (l.back () == '\n' || l.back () == '\r')) l.pop_back ();
		size_t e = l.find ('=');
		if (l.empty () || l[0] == '#' || e == std::string::npos) continue;
		std::string k = l.substr (0, e), v = l.substr (e + 1);
		while (!k.empty () && k.back () == ' ') k.pop_back ();
		while (!v.empty () && v[0] == ' ') v.erase (0, 1);
		kv.push_back ({ k, v });
	}
	fclose (f);
	return true;
}

void saveConf ()
{
	mkdirsTo ("HOME:/terracube/worlds");
	FILE *f = fopen (CONF, "w");
	if (!f) return;
	fprintf (f, "# TerraCube\nworld = %s\ndistance = %d\napi = %d\n", g.world.name.c_str (), g.distance, apiRunning () ? 1 : 0);
	fclose (f);
}

bool saveWorldIni (std::string &err)
{
	mkdirsTo (g.world.dir);
	FILE *f = fopen ((g.world.dir + "/world.ini").c_str (), "w");
	if (!f) {
		err = "cannot write " + g.world.dir + "/world.ini";
		return false;
	}
	fprintf (f, "# TerraCube : un monde\nseed = %u\nx = %.3f\ny = %.3f\nz = %.3f\nyaw = %.2f\npitch = %.2f\nhour = %.3f\ntime = %d\nfly = %d\nhotbar =",
		 g.world.seed, g.pl.x, g.pl.y, g.pl.z, g.pl.yaw, g.pl.pitch, g.hour, g.timeRuns ? 1 : 0, g.pl.fly ? 1 : 0);
	for (int i = 0; i < 9; i++) fprintf (f, " %d", g.hotbar[i]);
	fprintf (f, "\nselected = %d\n", g.selected);
	bool ok = !ferror (f);
	fclose (f);
	if (!ok) err = "write error";
	return ok;
}

// un endroit sec pour apparaître, près de (x, z)
void spawnNear (int x0, int z0)
{
	for (int r = 0; r < 200; r += 8)
		for (int a = 0; a < (r ? 8 : 1); a++) {
			int x = x0 + (int) (r * cos (a * M_PI / 4)), z = z0 + (int) (r * sin (a * M_PI / 4));
			int y = g.world.height (x, z);
			int b = g.world.get (x, y, z);
			if (b != WATER && b != ICE && b != LEAVES && b != PINE_LEAVES && y > SEA) {
				gameTeleport (x + 0.5, y + 1, z + 0.5);
				return;
			}
		}
	gameTeleport (x0 + 0.5, g.world.height (x0, z0) + 1, z0 + 0.5);
}

const int DEFAULT_HOTBAR[9] = { GRASS, DIRT, STONE, COBBLE, PLANKS, LOG, GLASS, BRICK, LAMP };

// le monde de ce nom : relu s'il existe, sinon fait de cette graine
void openWorld (const std::string &name, uint32_t seed, bool seedGiven)
{
	for (auto *c : g.world.freed) R.freeChunk (c), delete c;
	g.world.freed.clear ();
	std::vector<std::pair<std::string, std::string>> kv;
	std::string dir = "HOME:/terracube/worlds/" + name;
	bool exists = readIni (dir + "/world.ini", kv);
	uint32_t sd = seed;
	double px = 0, py = -1, pz = 0;
	g.pl = Player ();
	memcpy (g.hotbar, DEFAULT_HOTBAR, sizeof g.hotbar);
	g.selected = 0;
	g.hour = 9;
	g.timeRuns = true;
	if (exists) {
		for (auto &p : kv) {
			const char *v = p.second.c_str ();
			if (p.first == "seed" && !seedGiven) sd = (uint32_t) strtoul (v, nullptr, 10);
			else if (p.first == "x") px = atof (v);
			else if (p.first == "y") py = atof (v);
			else if (p.first == "z") pz = atof (v);
			else if (p.first == "yaw") g.pl.yaw = (float) atof (v);
			else if (p.first == "pitch") g.pl.pitch = (float) atof (v);
			else if (p.first == "hour") g.hour = (float) atof (v);
			else if (p.first == "time") g.timeRuns = atoi (v) != 0;
			else if (p.first == "fly") g.pl.fly = atoi (v) != 0;
			else if (p.first == "selected") g.selected = atoi (v) % 9;
			else if (p.first == "hotbar") {
				char *s = (char *) v;
				for (int i = 0; i < 9; i++) {
					long b = strtol (s, &s, 10);
					if (b > 0 && b < BLOCK_COUNT) g.hotbar[i] = (int) b;
				}
			}
		}
	}
	g.world.reset (name, sd);
	for (auto *c : g.world.freed) R.freeChunk (c), delete c;
	g.world.freed.clear ();
	g.events.clear ();
	g.edited = false;
	if (py >= 0) gameTeleport (px, py, pz);
	else spawnNear (0, 0);
	// le sol autour, tout de suite
	int cx = floorDiv ((int) floor (g.pl.x), CW), cz = floorDiv ((int) floor (g.pl.z), CW);
	for (int dz = -2; dz <= 2; dz++)
		for (int dx = -2; dx <= 2; dx++) g.world.ensure (cx + dx, cz + dz);
	if (!exists) {
		std::string err;
		saveWorldIni (err);
	}
	ui_menu_state (14, g.pl.fly ? UMS_CHECKED : 0);
	ui_menu_state (15, g.timeRuns ? UMS_CHECKED : 0);
	gamePost (std::string (exists ? TR ("Monde « ") : TR ("Nouveau monde « ")) + name + TR (" », graine ") + std::to_string (sd));
	saveConf ();
}

} // namespace

bool gameSave (std::string &err)
{
	if (!saveWorldIni (err)) return false;
	if (!g.world.saveAll ()) {
		err = "cannot write the chunks in " + g.world.dir;
		return false;
	}
	g.edited = false;
	return true;
}

void gameNewWorld (const std::string &name, uint32_t seed)
{
	std::string err;
	gameSave (err);
	openWorld (name, seed, true);
}

// ---- le pilotage par un agent (MCP) : le jeton, comme Forge --------------------------------------------------------

namespace {

const char *const MCP_CONF = "HOME:/terracube/mcp.txt";
int g_mcpPort = MCP_PORT;

bool mcpInVault ()
{
	static int st = kv_status () >= 0 ? 1 : 0;
	return st;
}

std::string mcpFileToken ()
{
	std::vector<std::pair<std::string, std::string>> kv;
	readIni (MCP_CONF, kv);
	for (auto &p : kv)
		if (p.first == "token") return p.second;
	return "";
}

std::string mcpNewToken ()
{
	unsigned char r[32];
	std::string tok;
	static const char H[] = "0123456789abcdef";
	if (getentropy (r, sizeof r) == 0) {
		for (unsigned i = 0; i < sizeof r; i++) tok += H[r[i] >> 4], tok += H[r[i] & 15];
		memset (r, 0, sizeof r);
		return tok;
	}
	unsigned long long x = (unsigned long long) (nowMs () * 1000) ^ ((unsigned long long) time (nullptr) << 20) ^ (unsigned long long) (size_t) &tok;
	for (int i = 0; i < 48; i++) {
		x ^= x << 13, x ^= x >> 7, x ^= x << 17;
		tok += H[x & 15];
	}
	return tok;
}

std::string mcpToken ()
{
	std::string tok;
	if (mcpInVault ()) {
		char b[256];
		long n = kv_get ("mcp-token", b, sizeof b - 1);
		if (n > 0) {
			tok.assign (b, (size_t) n);
			memset (b, 0, sizeof b);
			return tok;
		}
		static std::string session;
		if (n < 0 && n != -2) {
			if (session.empty ()) session = mcpNewToken ();
			return session;
		}
		tok = mcpNewToken ();
		if (kv_set ("mcp-token", tok.data (), (long) tok.size ()) >= 0) return tok;
	} else {
		tok = mcpFileToken ();
		if (!tok.empty ()) return tok;
		tok = mcpNewToken ();
	}
	mkdirsTo ("HOME:/terracube/worlds");
	FILE *f = fopen (MCP_CONF, "w");
	if (f) {
		fprintf (f, "# TerraCube : le jeton du pilotage par un agent (MCP). Le changer ou effacer ce fichier invalide l'ancien.\ntoken = %s\n", tok.c_str ());
		fclose (f);
	}
	return tok;
}

std::string myAddress ()
{
	static bool open = netkit_kit_open () != 0;
	unsigned char ni[32] = { 0 };
	if (open) net_info ((long) ni);
	uint32_t ip;
	memcpy (&ip, ni, 4);
	char s[32];
	snprintf (s, sizeof s, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255);
	return ip ? s : "127.0.0.1";
}

bool mcpStart (int port, const std::string &token, bool quiet)
{
	std::string err;
	if (!mcp::start (port, token, err)) {
		std::string m = std::string (TR ("Le pilotage par un agent n'a pas pu démarrer : ")) + err;
		if (quiet) fprintf (stderr, "terracube : %s\n", m.c_str ());
		else ui_message ((long) "TerraCube", (long) m.c_str (), UM_OK);
		return false;
	}
	g_mcpPort = port;
	ui_menu_state (21, UMS_CHECKED);
	if (quiet) fprintf (stderr, "terracube : MCP sur http://%s:%d/mcp%s\n", myAddress ().c_str (), port, token.empty () ? " (sans jeton)" : "");
	gamePost (std::string (TR ("Agent IA : en attente sur le port ")) + std::to_string (port));
	return true;
}

void unlock ();

void mcpMenu ()
{
	unlock ();
	if (mcp::running ()) {
		std::string m = std::string (TR ("Un agent peut piloter TerraCube (")) + std::to_string (mcp::served ()) + TR (" messages reçus).\n\nArrêter le pilotage ?");
		if (ui_message ((long) "TerraCube", (long) m.c_str (), UM_YESNO) == 1) {
			mcp::stop ();
			ui_menu_state (21, 0);
			gamePost (TR ("Agent IA : pilotage arrêté"));
		}
		return;
	}
	std::string tok = mcpToken ();
	if (!mcpStart (MCP_PORT, tok, false)) return;
	std::string url = "http://" + myAddress () + ":" + std::to_string (MCP_PORT) + "/mcp";
	std::string cmd = "claude mcp add --transport http terracube " + url + " --header \"Authorization: Bearer " + tok + "\"";
	ui_clip_set ((long) cmd.c_str (), -1);
	std::string m = std::string (TR ("TerraCube attend un agent sur ")) + url + TR (".\n\nPour Claude Code, sur la machine où il tourne :\n\n") + cmd +
			TR ("\n\nLa commande est dans le presse-papiers. Le jeton est gardé dans ") +
			(mcpInVault () ? TR ("le coffre à secrets de Korin") : MCP_CONF) +
			TR (".\n\nL'agent voit le monde (captures), le parcourt, lit et pose des blocs ; vous voyez tout en direct.");
	ui_message ((long) TR ("Piloter TerraCube par un agent"), (long) m.c_str (), UM_OK);
}

void apiMenu ()
{
	if (apiRunning ()) {
		apiStop ();
		ui_menu_state (20, 0);
		gamePost (TR ("Scripts : serveur arrêté"));
	} else {
		std::string err;
		if (apiStart (API_PORT, false, err)) {
			ui_menu_state (20, UMS_CHECKED);
			gamePost (std::string (TR ("Scripts : port ")) + std::to_string (API_PORT) + TR (" (Python : import terracube)"));
		} else {
			unlock ();
			ui_message ((long) "TerraCube", (long) (std::string (TR ("Le serveur des scripts n'a pas pu démarrer : ")) + err).c_str (), UM_OK);
		}
	}
	saveConf ();
}

// ---- la vue et la souris --------------------------------------------------------------------------------------------

void lock ()
{
	if (g_locked || !g_focus) return;
	g_locked = ui_pointer_lock (1) >= 0;
	g_buttons = 0;
}

void unlock ()
{
	if (!g_locked) return;
	ui_pointer_lock (0);
	g_locked = false;
	memset (g_keys, 0, sizeof g_keys);
}

void placeView ()
{
	int w = (int) ui.w, h = (int) ui.h;
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	armgl_make_current (g_ctx, g_view.surf);
	gl_view_place (&g_view, g_win, 0, 0, w, h);
	armgl_make_current (g_ctx, g_view.surf);
}

long onResize (long, long)
{
	placeView ();
	return 0;
}

void fullscreen ()
{
	unlock ();
	if (!g_full) {
		long scr[4];
		ui_screen ((long) scr);
		long w = ui_window_ex (0, 0, scr[0], scr[1], (long) "TerraCube", WF_FRAMELESS | WF_TOP);
		if (w <= 0) return;
		g_win = ui.win;
		g_full = true;
	} else {
		ui_window_close (0);
		ui_window_use (g_winMain);
		g_win = g_winMain;
		g_full = false;
	}
	placeView ();
	ui_menu_state (4, g_full ? UMS_CHECKED : 0);
}

// ---- l'affichage par-dessus la vue (UIKit, aux couleurs du thème) ---------------------------------------------------

struct Zone {
	int x, y, w, h;
};
std::vector<Zone> g_zones, g_prevZones;
std::string g_overlayKey;

const char *biomeName (int x, int z)
{
	int b = g.world.get (x, g.world.height (x, z), z);
	switch (b) {
	case SAND: return TR ("plage ou désert");
	case SNOW: return TR ("neige");
	case WATER: return TR ("eau");
	case STONE: return TR ("montagne");
	default: return TR ("prairie");
	}
}

std::string hourText ()
{
	int h = (int) g.hour, m = (int) ((g.hour - h) * 60);
	char t[16];
	snprintf (t, sizeof t, "%02d:%02d", h, m);
	return t;
}

void panel (int x, int y, int w, int h)
{
	ui_rbox (x, y, w, h, 8, jet (TJ_LINE), jet (TJ_LINE));
	ui_rbox (x + 1, y + 1, w - 2, h - 2, 7, jet (TJ_SURFACE), jet (TJ_SURFACE));
	g_zones.push_back ({ x, y, w, h });
}

void drawOverlay (bool force)
{
	double t = nowMs ();
	// ce qui est montré : ne redessiner que si cela change
	std::vector<std::string> lines, chat;
	if (g_showInfo) {
		char l[160];
		snprintf (l, sizeof l, "%s · %s", g.world.name.c_str (), hourText ().c_str ());
		lines.push_back (l);
		snprintf (l, sizeof l, "x %d  y %d  z %d", (int) floor (g.pl.x), (int) floor (g.pl.y), (int) floor (g.pl.z));
		lines.push_back (l);
		if (g_showDebug) {
			snprintf (l, sizeof l, "%.0f %s · %ld %s · %ld %s", g_fps, TR ("img/s"), R.drawnChunks, TR ("tronçons"), R.drawnQuads, TR ("faces"));
			lines.push_back (l);
			lines.push_back (std::string (g_mode == ARMGL_GPU ? TR ("GPU : ") : TR ("Processeur : ")) + R.rendererName ());
			snprintf (l, sizeof l, "%s %u · %s", TR ("graine"), g.world.seed, biomeName ((int) floor (g.pl.x), (int) floor (g.pl.z)));
			lines.push_back (l);
			if (g_target.hit) {
				snprintf (l, sizeof l, "%s %d %d %d : %s", TR ("visé"), g_target.x, g_target.y, g_target.z, info (g_target.block).label);
				lines.push_back (l);
			}
		}
		std::string pil;
		if (apiRunning ()) pil += std::string (TR ("scripts ")) + std::to_string (apiClients ());
		if (mcp::running ()) pil += std::string (pil.empty () ? "" : " · ") + TR ("agent IA");
		if (g.pl.fly) pil += std::string (pil.empty () ? "" : " · ") + TR ("vol");
		if (!pil.empty ()) lines.push_back (pil);
	}
	for (auto &c : g.chat)
		if (t - c.t < 12000) chat.push_back (c.text);
	while (chat.size () > 6) chat.erase (chat.begin ());
	bool flash = !g_flash.empty () && t - g_flashT < 1500;
	std::string key = std::to_string (ui.w) + "x" + std::to_string (ui.h) + (g_locked ? "L" : "U") + (flash ? g_flash : "");
	for (auto &s : lines) key += "\n" + s;
	key += "\x01";
	for (auto &s : chat) key += "\n" + s;
	if (!force && key == g_overlayKey) return;
	g_overlayKey = key;
	g_prevZones = g_zones;
	g_zones.clear ();
	int W = (int) ui.w, H = (int) ui.h;
	long font = (long) ui.font;
	const int LH = 19, SZ = 13;
	if (!lines.empty ()) {
		int w = 0;
		for (auto &s : lines) w = std::max (w, (int) ui_text_width ((long) s.c_str (), SZ, font));
		int pw = w + 24, ph = (int) lines.size () * LH + 12;
		panel (12, 12, pw, ph);
		for (size_t i = 0; i < lines.size (); i++)
			ui_text ((long) lines[i].c_str (), 24, 12 + 6 + LH * (int) i + 14, i == 0 ? jet (TJ_TEXT1) : jet (TJ_TEXT2), SZ, i == 0 ? (long) ui.font_b : font, 0);
	}
	if (!chat.empty ()) {
		int w = 0;
		for (auto &s : chat) w = std::max (w, (int) ui_text_width ((long) s.c_str (), SZ, font));
		w = std::min (w, W / 2 - 40);
		int pw = w + 24, ph = (int) chat.size () * LH + 12;
		int y = H - 82 - ph;
		panel (12, y, pw, ph);
		for (size_t i = 0; i < chat.size (); i++) ui_text_fit ((long) chat[i].c_str (), 24, y + 6 + LH * (int) i + 14, jet (TJ_TEXT1), SZ, font, w);
	}
	if (flash) {
		int w = (int) ui_text_width ((long) g_flash.c_str (), SZ, (long) ui.font_b) + 24;
		panel ((W - w) / 2, H - 112, w, 28);
		ui_text ((long) g_flash.c_str (), (W - w) / 2 + 12, H - 112 + 19, jet (TJ_TEXT1), SZ, (long) ui.font_b, 0);
	}
	if (!g_locked && !g_selftest) {
		const char *a = TR ("Cliquez dans la vue pour jouer");
		const char *b = TR ("ZQSD ou flèches : marcher · Espace : sauter · F : voler · B : blocs · T : message · Échap : la souris");
		int wa = (int) ui_text_width ((long) a, 16, (long) ui.font_b), wb = (int) ui_text_width ((long) b, SZ, font);
		int pw = std::max (wa, wb) + 40, ph = 70;
		if (pw > W - 20) pw = W - 20;
		int x = (W - pw) / 2, y = H / 2 - ph - 30;
		panel (x, y, pw, ph);
		ui_text ((long) a, (W - wa) / 2, y + 28, jet (TJ_ACCENT), 16, (long) ui.font_b, 0);
		ui_text_fit ((long) b, std::max (x + 12, (W - wb) / 2), y + 52, jet (TJ_TEXT2), SZ, font, pw - 24);
	}
	for (auto &z : g_zones) ui_update (z.x, z.y, z.w, z.h);
	std::vector<uint16_t> rr;
	for (size_t i = 0; i < g_zones.size () && i < 32; i++) {
		rr.push_back ((uint16_t) g_zones[i].x), rr.push_back ((uint16_t) g_zones[i].y);
		rr.push_back ((uint16_t) g_zones[i].w), rr.push_back ((uint16_t) g_zones[i].h);
	}
	ui_surface_overlay ((long) rr.data (), (long) (rr.size () / 4));
}

// ---- jouer ------------------------------------------------------------------------------------------------------

void eyeOf (float eye[3], float dir[3])
{
	eye[0] = (float) g.pl.x, eye[1] = (float) (g.pl.y + Player::EYE), eye[2] = (float) g.pl.z;
	viewDir (g.pl.yaw, g.pl.pitch, dir);
}

int faceOf (const Hit &h)
{
	int dx = h.nx - h.x, dy = h.ny - h.y, dz = h.nz - h.z;
	return dx > 0 ? 0 : dx < 0 ? 1 : dy > 0 ? 2 : dy < 0 ? 3 : dz > 0 ? 4 : 5;
}

void event (int kind, const Hit &h, int block)
{
	g.events.push_back ({ kind, h.x, h.y, h.z, block, faceOf (h) });
	if (g.events.size () > 256) g.events.erase (g.events.begin ());
}

void breakBlock ()
{
	if (!g_target.hit || g_target.block == BEDROCK) return;
	event (1, g_target, g_target.block);
	g.world.set (g_target.x, g_target.y, g_target.z, AIR);
	// une plante sur le bloc cassé tombe avec lui
	int up = g.world.get (g_target.x, g_target.y + 1, g_target.z);
	if (info (up).shape == SH_CROSS) g.world.set (g_target.x, g_target.y + 1, g_target.z, AIR);
	g.edited = true;
}

void placeBlock ()
{
	if (!g_target.hit) return;
	int b = g.hotbar[g.selected];
	int x = g_target.nx, y = g_target.ny, z = g_target.nz;
	// (une plante visée se remplace)
	if (info (g_target.block).shape == SH_CROSS) x = g_target.x, y = g_target.y, z = g_target.z;
	if (y < 0 || y >= CH) return;
	int there = g.world.get (x, y, z);
	if (there != AIR && there != WATER && info (there).shape != SH_CROSS) return;
	if (isSolid (b) && playerInside (g.pl, x, y, z)) return;
	if (info (b).shape == SH_CROSS && !isSolid (g.world.get (x, y - 1, z))) return;
	g.world.set (x, y, z, b);
	Hit h = g_target;
	h.x = x, h.y = y, h.z = z;
	event (2, h, b);
	g.edited = true;
}

void selectSlot (int i)
{
	g.selected = (i % 9 + 9) % 9;
	g_flash = info (g.hotbar[g.selected]).label;
	g_flashT = nowMs ();
}

// la liste des blocs, pour la case choisie (B)
void chooseBlock ()
{
	unlock ();
	std::string labels;
	std::vector<int> ids;
	for (int i = 1; i < BLOCK_COUNT; i++) {
		if (i == BEDROCK || i == WATER) continue;
		labels += info (i).label;
		labels += '\0';
		ids.push_back (i);
	}
	labels += '\0';
	long r = ui_popup_local (ui.w / 2 - 110, 60, (long) labels.data (), 220);
	if (r >= 0 && r < (long) ids.size ()) {
		g.hotbar[g.selected] = ids[(size_t) r];
		selectSlot (g.selected);
	}
}

// T : un message, ou une commande (« /tp x y z », « /time 18 », « /seed », ou une ligne du protocole : « /world.getHeight(0,0) »)
void chatPrompt ()
{
	unlock ();
	char buf[512] = "";
	if (!ui_ask ((long) TR ("TerraCube"), (long) TR ("Message ou commande (/tp x y z, /time h, /fly, /seed, /world.setBlock(x,y,z,stone)…) :"), (long) buf, sizeof buf)) return;
	std::string s = buf;
	if (s.empty ()) return;
	if (s[0] != '/') {
		gamePost (s);
		return;
	}
	std::string c = s.substr (1);
	double a, b, d;
	if (sscanf (c.c_str (), "tp %lf %lf %lf", &a, &b, &d) == 3) gameTeleport (a, b, d);
	else if (sscanf (c.c_str (), "time %lf", &a) == 1) g.hour = (float) fmod (fmod (a, 24) + 24, 24);
	else if (c == "fly") g.pl.fly = !g.pl.fly, gameFlyChanged ();
	else if (c == "seed") gamePost (std::string (TR ("Graine : ")) + std::to_string (g.world.seed));
	else if (c == "spawn") spawnNear (0, 0);
	else {
		bool reply;
		std::string out = apiExec (c, reply);
		gamePost ("> " + c + (out.empty () ? "" : " : " + out.substr (0, 200)));
	}
}

void newWorldDialog ()
{
	unlock ();
	long d = ui_dialog_begin (420, 200, (long) TR ("Nouveau monde"));
	ui_label (16, 44, 120, 24, (long) TR ("Nom"), 0);
	long fName = ui_textbox (130, 40, 274, 30, 0);
	char nm[64];
	snprintf (nm, sizeof nm, "monde-%ld", (long) (time (nullptr) % 100000));
	ui_w_set_text (fName, (long) nm);
	ui_label (16, 88, 120, 24, (long) TR ("Graine"), 0);
	long fSeed = ui_textbox (130, 84, 274, 30, 0);
	unsigned r = 0;
	getentropy (&r, sizeof r);
	ui_w_set_text (fSeed, (long) std::to_string (r % 1000000).c_str ());
	long e = ui_dialog_button (d, 0, (long) TR ("Créer"), 1, UB_DEFAULT, 420 - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, e);
	ui_dialog_cancel (d, 0);
	ui_w_focus (fName);
	long res = ui_dialog_run (d, 0, 0);
	std::string name = (const char *) ui_w_get_text (fName), seed = (const char *) ui_w_get_text (fSeed);
	ui_dialog_end (d);
	if (res != 1) return;
	// un nom de dossier sûr
	std::string n;
	for (char ch : name)
		if (isalnum ((unsigned char) ch) || ch == '-' || ch == '_') n += ch;
		else if (ch == ' ') n += '-';
	if (n.empty ()) n = "monde";
	uint32_t sd = 0;
	char *end;
	unsigned long v = strtoul (seed.c_str (), &end, 10);
	if (*end == 0 && !seed.empty ()) sd = (uint32_t) v;
	else
		for (char ch : seed) sd = sd * 31 + (unsigned char) ch;
	gameNewWorld (n, sd);
}

void openWorldDialog ()
{
	unlock ();
	mkdirsTo ("HOME:/terracube/worlds");
	char path[512] = "";
	if (!ui_folder_choose ((long) "HOME:/terracube/worlds", (long) path, sizeof path)) return;
	std::string p = path;
	while (!p.empty () && p.back () == '/') p.pop_back ();
	std::string name = p.substr (p.rfind ('/') + 1);
	if (name.empty () || name == "worlds") return;
	std::string err;
	gameSave (err);
	openWorld (name, 1, false);
}

void setDistance (int d)
{
	g.distance = std::max (2, std::min (16, d));
	ui_menu_state (11, g.distance <= 4 ? UMS_CHECKED : 0);
	ui_menu_state (12, g.distance > 4 && g.distance <= 8 ? UMS_CHECKED : 0);
	ui_menu_state (13, g.distance > 8 ? UMS_CHECKED : 0);
	saveConf ();
}

const char *MENU =
	"MPartie\nI1\tNouveau monde…\tCtrl+N\nI2\tOuvrir un monde…\tCtrl+O\nI3\tSauver\tCtrl+S\n-\nI4\tPlein écran\tF9\n"
	"MAffichage\nI11\tDistance courte\nI12\tDistance moyenne\nI13\tDistance longue\n-\nI14\tVoler\tF\nI15\tLe temps passe\nI16\tInfos détaillées\tF3\n"
	"MPilotage\nI20\tScripts (Python, BASIC) sur le port 4730\nI21\tPiloter par un agent IA (MCP)…\n-\nI22\tOù sont les exemples ?\n";

bool command (long id)
{
	std::string err;
	switch (id) {
	case 1: newWorldDialog (); break;
	case 2: openWorldDialog (); break;
	case 3:
		if (gameSave (err)) gamePost (TR ("Monde sauvé"));
		else gamePost (std::string (TR ("Sauvegarde impossible : ")) + err);
		break;
	case 4: fullscreen (); break;
	case 11: setDistance (4); break;
	case 12: setDistance (8); break;
	case 13: setDistance (12); break;
	case 14:
		g.pl.fly = !g.pl.fly;
		gameFlyChanged ();
		break;
	case 15:
		g.timeRuns = !g.timeRuns;
		ui_menu_state (15, g.timeRuns ? UMS_CHECKED : 0);
		break;
	case 16:
		g_showDebug = !g_showDebug;
		ui_menu_state (16, g_showDebug ? UMS_CHECKED : 0);
		break;
	case 20: apiMenu (); break;
	case 21: mcpMenu (); break;
	case 22:
		unlock ();
		ui_message ((long) "TerraCube",
			    (long) TR ("Exemples Python : /apps/terracube.app/python (terracube.py et examples/).\n"
				       "  python3 /apps/terracube.app/python/examples/house.py\n\n"
				       "Exemples BASIC : /apps/terracube.app/basic.\n  basic /apps/terracube.app/basic/pyramid.bas\n\n"
				       "Le jeu doit tourner, avec Pilotage > Scripts coché (port 4730)."),
			    UM_OK);
		break;
	default: return false;
	}
	return true;
}

bool isKey (int code, int a, int b = -1) { return code == a || code == b; }

void keyDown (int code, int mods, long id)
{
	if (code == KEY_ESC) {
		if (g_locked) unlock ();
		else if (g_full) fullscreen ();
		return;
	}
	if (code == 0x42) {	// F9
		fullscreen ();
		return;
	}
	if (code == 0x3C) {	// F3
		command (16);
		return;
	}
	if (mods & UI_MOD_CTRL) {
		if (id >= 0) command (id);
		return;
	}
	if (code >= 0x1E && code <= 0x26) selectSlot (code - 0x1E);	// 1 à 9
	else if (code == 0x09) command (14);			// F : voler
	else if (code == 0x05) chooseBlock ();			// B : les blocs
	else if (code == 0x17) chatPrompt ();			// T : un message
	else if (code == 0x08 && g_target.hit) event (3, g_target, g_target.block);	// E : toucher (pour les scripts)
	else if (code == 0x14 && g_target.hit) {		// Q (A en AZERTY) : prendre ce bloc dans la case
		int b = g_target.block;
		if (b != BEDROCK && b != WATER) g.hotbar[g.selected] = b, selectSlot (g.selected);
	}
}

void handleEvent (long t)
{
	if (t == WEV_MENU) {
		command ((long) ui_ev_field (&ui, EV_CODE));
		return;
	}
	if (t == WEV_RESIZE) {
		long code = 0, taken = 0;
		uikit_w_event (&code, &taken);
		placeView ();
		drawOverlay (true);
		return;
	}
	if (t == WEV_FOCUS) {
		if ((long) ui_ev_field (&ui, EV_WIN) == g_win) {
			g_focus = ui_ev_field (&ui, EV_KIND) != 0;
			if (!g_focus) {
				g_locked = false;	// (Elegant a repris le pointeur)
				memset (g_keys, 0, sizeof g_keys);
			}
		}
		return;
	}
	if (t == WEV_KEY) {
		int code = (int) ui_ev_field (&ui, EV_CODE) & 255, kind = (int) ui_ev_field (&ui, EV_KIND), mods = (int) ui_ev_field (&ui, EV_MODS);
		long id = kind && (mods & UI_MOD_CTRL) ? ui_menu_key (0) : -1;
		g_keys[code] = kind != 0;
		if (kind) keyDown (code, mods, id);
		return;
	}
	if (t == WEV_WHEEL) {
		int n = (int) ui_ev_field (&ui, EV_Y);
		if (n) selectSlot (g.selected - (n > 0 ? 1 : -1));
		return;
	}
	if (t == WEV_MOUSE) {
		int kind = (int) ui_ev_field (&ui, EV_KIND);
		if (kind == 3) {	// WEV_M_REL : le déplacement, souris prise
			g_mdx += (float) (int) (uint32_t) ui_ev_field (&ui, EV_X);
			g_mdy += (float) (int) (uint32_t) ui_ev_field (&ui, EV_Y);
			return;
		}
		unsigned b = (unsigned) ui_ev_field (&ui, EV_CODE) & 7;
		if (kind == 1 && !g_locked) {
			// (les 32 px du haut déplacent la fenêtre ; ailleurs, un clic prend la souris)
			if (ui_ev_field (&ui, EV_Y) >= 32 || g_full) lock ();
			g_buttons = b;
			return;
		}
		if (!g_locked) return;
		unsigned pressed = b & ~g_buttons;
		g_buttons = b;
		double t0 = nowMs ();
		if (pressed & 1) breakBlock (), g_breakAt = t0 + 300;
		if (pressed & 2) placeBlock (), g_placeAt = t0 + 300;
		if (pressed & 4 && g_target.hit && g_target.block != BEDROCK) g.hotbar[g.selected] = g_target.block, selectSlot (g.selected);
	}
}

void updateLoading (double budgetMs)
{
	int pcx = floorDiv ((int) floor (g.pl.x), CW), pcz = floorDiv ((int) floor (g.pl.z), CW);
	double t0 = nowMs ();
	int want = g.distance + 1;
	for (int r = 0; r <= want; r++)
		for (int dz = -r; dz <= r; dz++)
			for (int dx = -r; dx <= r; dx++) {
				if (abs (dx) != r && abs (dz) != r) continue;
				if (!g.world.find (pcx + dx, pcz + dz)) {
					g.world.ensure (pcx + dx, pcz + dz);
					if (nowMs () - t0 > budgetMs) return;
				}
			}
	// au loin : oubliés (sauvés s'ils ont changé)
	std::vector<Chunk *> far;
	for (auto &kv : g.world.chunks)
		if (abs (kv.second->cx - pcx) > want + 2 || abs (kv.second->cz - pcz) > want + 2) far.push_back (kv.second);
	for (Chunk *c : far) g.world.unload (c);
}

void frame (double dt)
{
	// la souris tourne la tête
	if (g_locked) {
		g.pl.yaw = fmodf (g.pl.yaw + g_mdx * 0.15f + 360.0f, 360.0f);
		g.pl.pitch -= g_mdy * 0.15f;
		if (g.pl.pitch > 89.9f) g.pl.pitch = 89.9f;
		if (g.pl.pitch < -89.9f) g.pl.pitch = -89.9f;
	}
	g_mdx = g_mdy = 0;
	MoveInput in;
	if (g_locked) {
		auto k = [] (int c) { return g_keys[c]; };
		in.forward = (k (0x1A) || k (0x52) ? 1 : 0) - (k (0x16) || k (0x51) ? 1 : 0);	// W/Z, haut ; S, bas
		in.strafe = (k (0x07) || k (0x4F) ? 1 : 0) - (k (0x04) || k (0x50) ? 1 : 0);	// D, droite ; A/Q, gauche
		in.jump = k (0x2C);
		in.sneak = k (0xE1) || k (0xE5);
		in.sprint = k (0xE0) || k (0xE4) || k (0x15);	// Ctrl ou R
	}
	playerStep (g.world, g.pl, in, dt);
	if (g.timeRuns) g.hour = fmodf (g.hour + (float) dt * 24.0f / 1200.0f, 24.0f);	// une journée : 20 minutes
	// le bloc visé ; les clics tenus se répètent
	float eye[3], dir[3];
	eyeOf (eye, dir);
	g_target = g.world.raycast (eye, dir, 6.0f);
	double t = nowMs ();
	if (g_locked && (g_buttons & 1) && t > g_breakAt) breakBlock (), g_breakAt = t + 250;
	if (g_locked && (g_buttons & 2) && t > g_placeAt) placeBlock (), g_placeAt = t + 250;
}

float daylight (float hour, float *sunAngle)
{
	float a = (hour - 6.0f) / 12.0f * (float) M_PI;	// 6 h : lever, 18 h : coucher
	if (sunAngle) *sunAngle = a;
	float s = sinf (a);
	float d = s * 2.5f + 0.35f;
	return d < 0 ? 0 : d > 1 ? 1 : d;
}

void renderTo (int w, int h, const Camera &cam, bool hotbar = true)
{
	Frame f;
	f.w = w, f.h = h;
	f.daylight = daylight (g.hour, &f.sunAngle);
	f.distance = g.distance;
	f.hasTarget = g_target.hit && !g_selftest;
	f.target = g_target;
	HudState hud;
	memcpy (hud.slots, g.hotbar, sizeof hud.slots);
	hud.selected = g.selected;
	int eb = g.world.get ((int) floor (cam.x), (int) floor (cam.y), (int) floor (cam.z));
	hud.underwater = eb == WATER;
	hud.crosshair = g_locked;
	hud.hotbar = hotbar;
	hud.panel = jet (TJ_SURFACE);
	hud.panelLine = jet (TJ_SUNKEN);
	hud.accent = jet (TJ_ACCENT);
	hud.text = jet (TJ_TEXT1);
	R.draw (g.world, cam, f, hud);
}

Camera playerCamera ()
{
	Camera c;
	c.x = g.pl.x, c.y = g.pl.y + Player::EYE, c.z = g.pl.z;
	c.yaw = g.pl.yaw, c.pitch = g.pl.pitch;
	return c;
}

void freeChunks ()
{
	for (Chunk *c : g.world.freed) R.freeChunk (c), delete c;
	g.world.freed.clear ();
}

} // namespace

bool gameCapture (std::vector<uint32_t> &px, int &w, int &h, const double *eye, const float *yp)
{
	w = g_view.w, h = g_view.h;
	if (w < 16 || h < 16 || !g_view.surf) return false;
	static void *off;
	static int ow, oh;
	if (!off || ow != w || oh != h) {
		off = armgl_surface (w, h);	// (une image hors écran : la vue du joueur ne clignote pas)
		ow = w, oh = h;
	}
	if (!off || !armgl_make_current (g_ctx, off)) {
		armgl_make_current (g_ctx, g_view.surf);
		return false;
	}
	Camera c = playerCamera ();
	if (eye) c.x = eye[0], c.y = eye[1], c.z = eye[2];
	if (yp) c.yaw = yp[0], c.pitch = yp[1];
	if (eye) {
		// le terrain autour de ce point d'abord
		int cx = floorDiv ((int) floor (c.x), CW), cz = floorDiv ((int) floor (c.z), CW);
		for (int dz = -g.distance; dz <= g.distance; dz++)
			for (int dx = -g.distance; dx <= g.distance; dx++) g.world.ensure (cx + dx, cz + dz);
	}
	// tous les maillages en retard (sinon des trous dans la capture)
	R.updateMeshes (g.world, c, g.distance, 4000);
	bool locked = g_locked;
	g_locked = false;	// (pas de viseur)
	renderTo (w, h, c, eye == nullptr);
	g_locked = locked;
	armgl_finish ();
	px.resize ((size_t) w * h);
	bool ok = armgl_read (off, px.data (), w * 4) == 0 || true;
	for (auto &p : px) p &= 0xFFFFFF;
	armgl_make_current (g_ctx, g_view.surf);
	return ok;
}

} // namespace tc

using namespace tc;

// ---- les vérifications (--selftest) ---------------------------------------------------------------------------------

static int g_fails;
static void check (bool ok, const std::string &what)
{
	printf ("terracube : %-58s %s\n", what.c_str (), ok ? "PASS" : "FAIL");
	fflush (stdout);
	if (!ok) g_fails++;
}

static void selftest ()
{
	bool r;
	check (apiExec ("ping()", r) == "pong" && r, "protocole : ping");
	int h = atoi (apiExec ("world.getHeight(0,0)", r).c_str ());
	check (h > 10 && h < CH, "protocole : world.getHeight(0,0) = " + std::to_string (h));
	apiExec ("world.setBlocks(0,100,0,4,104,4,stone)", r);
	check (apiExec ("world.getBlock(2,102,2)", r) == "1", "protocole : setBlocks puis getBlock");
	apiExec ("world.setBlock(2,102,2,gold_block)", r);
	check (apiExec ("world.getBlock(2,102,2)", r) == std::to_string (GOLD_BLOCK), "protocole : setBlock par nom");
	check (apiExec ("world.getBlock(2,102,2", r) == std::to_string (GOLD_BLOCK), "protocole : parenthèse manquante tolérée");
	check (apiExec ("world.getBlocks(0,100,0,1,100,1)", r) == "1,1,1,1", "protocole : getBlocks");
	check (apiExec ("world.getBlock(a,b,c)", r).rfind ("error:", 0) == 0, "protocole : erreur d'argument");
	apiExec ("player.setPos(2.5,106,2.5)", r);
	check (apiExec ("player.getTile()", r) == "2,106,2", "protocole : setPos / getTile");
	apiExec ("chat.post(Bonjour depuis le selftest)", r);
	check (!g.chat.empty () && g.chat.back ().text == "Bonjour depuis le selftest", "protocole : chat.post");
	check (apiExec ("world.getBlockTypes()", r).find ("1=stone") != std::string::npos, "protocole : getBlockTypes");
	// MCP
	std::string init = mcpHandle (R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})");
	check (init.find ("\"protocolVersion\":\"2025-06-18\"") != std::string::npos, "MCP : initialize");
	std::string list = mcpHandle (R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
	check (list.find ("\"screenshot\"") != std::string::npos && list.find ("\"fill\"") != std::string::npos, "MCP : tools/list");
	std::string fill = mcpHandle (R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"fill","arguments":{"from":[10,90,10],"to":[12,92,12],"block":"brick","hollow":true}}})");
	check (fill.find ("\\\"placed\\\":26") != std::string::npos, "MCP : fill creux (26 blocs)");
	std::string gb = mcpHandle (R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_block","arguments":{"x":11,"y":91,"z":11}}})");
	check (gb.find ("\\\"block\\\":\\\"air\\\"") != std::string::npos, "MCP : get_block (l'intérieur est vide)");
	std::string bad = mcpHandle (R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"set_block","arguments":{"x":1,"y":1,"z":1,"block":"nope"}}})");
	check (bad.find ("\"isError\":true") != std::string::npos, "MCP : bloc inconnu refusé");
	std::string hm = mcpHandle (R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"get_heightmap","arguments":{"from":[0,0],"to":[3,3]}}})");
	check (hm.find ("heights") != std::string::npos, "MCP : get_heightmap");
	std::string sb = mcpHandle (R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"set_blocks","arguments":{"blocks":[[20,95,20,"lamp"],{"x":21,"y":95,"z":20,"block":"glass"}]}}})");
	check (sb.find ("\\\"placed\\\":2") != std::string::npos && g.world.get (20, 95, 20) == LAMP, "MCP : set_blocks");
	// sauvegarde et relecture d'un tronçon
	std::string err;
	check (gameSave (err), "sauvegarde du monde " + g.world.dir + (err.empty () ? "" : " : " + err));
	Chunk tmp;
	tmp.cx = 0, tmp.cz = 0;
	check (g.world.loadChunk (&tmp) && tmp.at (2, 102, 2) == GOLD_BLOCK, "relecture d'un tronçon sauvé");
	// une capture
	gameTeleport (-6, 108, -6);
	g.pl.yaw = 135, g.pl.pitch = -30;
	std::string shot = mcpHandle (R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"screenshot","arguments":{"max_width":480}}})");
	bool img = shot.find ("\"type\":\"image\"") != std::string::npos;
	check (img, "MCP : screenshot (PNG)");
	std::vector<uint32_t> px;
	int w, hh;
	if (gameCapture (px, w, hh, nullptr, nullptr)) {
		// le ciel en haut, autre chose en bas
		uint32_t top = px[(size_t) 4 * w + w / 2], bottom = px[(size_t) (hh - 4) * w + w / 2];
		char t[96];
		snprintf (t, sizeof t, "image rendue %dx%d (haut %06x, bas %06x)", w, hh, top, bottom);
		check (top != bottom, t);
		// le PNG sur la carte, pour le regarder
		size_t a = shot.find ("\"data\":\"") + 8, e = shot.find ('"', a);
		std::string b64 = shot.substr (a, e - a);
		static const std::string T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string bin;
		unsigned v = 0;
		int bits = 0;
		for (char ch : b64) {
			size_t k = T.find (ch);
			if (k == std::string::npos) continue;
			v = v << 6 | (unsigned) k, bits += 6;
			if (bits >= 8) bits -= 8, bin += (char) (v >> bits & 255);
		}
		FILE *f = fopen ("HOME:/terracube/selftest.png", "wb");
		if (f) fwrite (bin.data (), 1, bin.size (), f), fclose (f);
	} else check (false, "image rendue");
	printf ("terracube : rendu par %s\n", R.rendererName ());
	printf ("terracube : %s (%d échec(s))\n", g_fails ? "FAIL" : "PASS", g_fails);
	fflush (stdout);
}

int main (int argc, char **argv)
{
	std::string worldName, mcpTok;
	uint32_t seed = 0;
	bool seedGiven = false, api = true, apiAny = false, tokGiven = false;
	int mcpPort = 0, dist = -1;
	double selfSecs = -1;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&] () { return i + 1 < argc ? std::string (argv[++i]) : std::string (); };
		if (a == "--world") worldName = next ();
		else if (a == "--seed") seed = (uint32_t) strtoul (next ().c_str (), nullptr, 10), seedGiven = true;
		else if (a == "--distance") dist = atoi (next ().c_str ());
		else if (a == "--no-api") api = false;
		else if (a == "--api-any") apiAny = true;
		else if (a == "--mcp") mcpPort = i + 1 < argc && isdigit ((unsigned char) argv[i + 1][0]) ? atoi (argv[++i]) : MCP_PORT;
		else if (a == "--mcp-token") mcpTok = next (), tokGiven = true;
		else if (a == "--selftest") selfSecs = i + 1 < argc && isdigit ((unsigned char) argv[i + 1][0]) ? atof (argv[++i]) : 0;
		else {
			fprintf (stderr, "usage: terracube [--world nom] [--seed n] [--distance n] [--no-api] [--api-any] [--mcp [port]] [--mcp-token t] [--selftest [s]]\n");
			return 2;
		}
	}
	g_selftest = selfSecs >= 0;
	if (!uikit_open (&ui)) return 1;
	g_jet = (const unsigned *) ui_tokens ();
	g_mode = armgl_init (0);
	if (g_mode == ARMGL_NONE) {
		ui_message ((long) "TerraCube", (long) TR ("TerraCube demande OpenGL ES 3 (GlKit) : pas de GPU ni de rendu par le processeur ici."), UM_OK);
		return 1;
	}
	// réglages
	std::vector<std::pair<std::string, std::string>> conf;
	readIni (CONF, conf);
	std::string last;
	for (auto &p : conf) {
		if (p.first == "world") last = p.second;
		else if (p.first == "distance" && dist < 0) dist = atoi (p.second.c_str ());
		else if (p.first == "api" && p.second == "0" && api) api = g_selftest;
	}
	if (dist < 0) dist = g_mode == ARMGL_SOFT ? 4 : 8;	// (le processeur : moins loin)
	if (g_selftest && dist > 4) dist = 4;
	g.distance = std::max (2, std::min (16, dist));
	if (worldName.empty ()) worldName = g_selftest ? "selftest" : last.empty () ? "monde" : last;
	if (!seedGiven && !g_selftest) {
		unsigned r = 12345;
		getentropy (&r, sizeof r);
		seed = r % 1000000;
	}
	if (g_selftest && !seedGiven) seed = 4242, seedGiven = true;

	long sw = 1024, sh = 640;
	g_winMain = ui_window_ex (-1, -1, sw, sh, (long) "TerraCube", WF_RESIZABLE | WF_GLYPH_LIGHT);
	if (g_winMain <= 0) return 1;
	g_win = g_winMain;
	ui_min_size (480, 320);
	ui_on_resize ((long) onResize, 0);
	ui_menu ((long) MENU);
	g_ctx = armgl_context (3, 0, nullptr);
	long scr[4] = { 1920, 1080, 0, 0 };
	ui_screen ((long) scr);
	if (!g_ctx || !gl_view_open (&g_view, g_win, 0, 0, (int) std::max (scr[0], sw), (int) std::max (scr[1], sh)) || !armgl_make_current (g_ctx, g_view.surf)) {
		ui_message ((long) "TerraCube", (long) TR ("Pas de surface OpenGL pour la fenêtre."), UM_OK);
		return 1;
	}
	placeView ();
	std::string err;
	if (!R.init (err)) {
		fprintf (stderr, "terracube : %s\n", err.c_str ());
		ui_message ((long) "TerraCube", (long) (std::string (TR ("Les shaders ne se compilent pas : ")) + err).c_str (), UM_OK);
		return 1;
	}
	printf ("terracube : %s, rendu par %s\n", g_mode == ARMGL_GPU ? "GPU" : "processeur (softpipe)", R.rendererName ());
	fflush (stdout);
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	setDistance (g.distance);
	openWorld (worldName, seed, seedGiven);
	if (api) {
		if (apiStart (API_PORT, apiAny, err)) ui_menu_state (20, UMS_CHECKED);
		else fprintf (stderr, "terracube : scripts : %s\n", err.c_str ());
	}
	if (mcpPort) mcpStart (mcpPort, tokGiven ? (mcpTok == "-" ? "" : mcpTok) : mcpToken (), true);
	if (g_mode == ARMGL_SOFT) gamePost (TR ("Rendu par le processeur (pas de GPU) : distance de vue réduite"));
	if (!g_selftest) gamePost (TR ("Bienvenue ! Cliquez dans la vue pour jouer, B pour choisir les blocs."));

	double last_t = nowMs (), lastSave = last_t, fpsT = last_t, selfStart = last_t;
	long frames = 0;
	bool selfDone = false;
	for (bool run = true; run;) {
		long t;
		while ((t = ui_event (0)) != 0) {
			if (t == WEV_CLOSE) {
				run = false;
				break;
			}
			handleEvent (t);
		}
		if (!run) break;
		double now = nowMs (), dt = (now - last_t) / 1000.0;
		last_t = now;
		// le monde : d'abord ce qui est près
		updateLoading (g_selftest ? 200 : 6);
		apiPump (8);
		mcp::poll ([] (const std::string &in) { return mcpHandle (in); });
		frame (dt);
		R.updateMeshes (g.world, playerCamera (), g.distance, g_selftest ? 400 : 6);
		freeChunks ();
		renderTo (g_view.w, g_view.h, playerCamera ());
		if (armgl_swap (g_view.surf) || armgl_lost ()) {
			fprintf (stderr, "terracube : image refusée ou GPU perdu\n");
		}
		frames++;
		if (now - fpsT > 1000) {
			g_fps = frames * 1000.0 / (now - fpsT);
			frames = 0, fpsT = now;
		}
		drawOverlay (false);
		if (g.edited && now - lastSave > 60000) {
			gameSave (err);
			lastSave = now;
		}
		if (g_selftest && !selfDone) {
			// le terrain autour fait et maillé, puis les vérifications
			bool ready = now - selfStart > 1000;
			for (auto &kv : g.world.chunks) ready &= !kv.second->dirty || abs (kv.second->cx) > g.distance || abs (kv.second->cz) > g.distance;
			if (ready || now - selfStart > 120000) {
				selftest ();
				selfDone = true;
				selfStart = now;
			}
		}
		if (g_selftest && selfDone && now - selfStart > selfSecs * 1000) break;
		// au plus 60 images par seconde
		double spent = nowMs () - now;
		if (spent < 16) usleep ((useconds_t) ((16 - spent) * 1000));
	}
	unlock ();
	gameSave (err);
	saveConf ();
	apiStop ();
	mcp::stop ();
	return g_selftest ? (g_fails ? 1 : 0) : 0;
}
