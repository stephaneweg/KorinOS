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
#include "audio.h"
#include "game.h"
#include "items.h"
#include "mcp.h"
#include "player.h"
#include "render.h"
#include "sim.h"
#include "../../lib/keyvault/keyvault.h"

#define TR(s) ((const char *) ui_tr ((long) (s)))

namespace tc {

Game g;

namespace {

struct ui_vars ui;
Renderer R;
void *g_ctx;
// Le rendu : le GPU rend hors écran (une surface d'armgl à la taille de l'écran, la vue dans son coin), l'image est
// relue (glReadPixels), les textes de UIKit y sont dessinés, puis elle est recopiée d'un coup dans la fenêtre
// (ui_update_all), comme Cosmo Zoom. Elegant ne voit jamais une image à moitié faite : pas de scintillement (une
// surface rendue directement dans la fenêtre, armgl_window, avec des textes posés par-dessus, scintillait sur le ZBook).
struct GlTarget {
	void *surf = nullptr;	// la surface hors écran
	int capW = 0, capH = 0;	// sa taille
	int w = 0, h = 0;	// la vue (la fenêtre) : le coin bas gauche de la surface, pour GL
} g_view;
std::vector<uint32_t> g_frame;	// l'image finie (0x00RRGGBB, ligne du haut d'abord)
std::vector<uint8_t> g_read;	// (ce que rend glReadPixels)
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
bool g_console;			// la console de commandes est ouverte
bool g_consoleWasLocked;
std::string g_cin;		// ce qui est tapé
std::vector<std::string> g_hist;
int g_histPos;
float g_mineProg = -1;		// survie : le bloc en train d'être cassé (0 à 1)
double g_hitAt;			// (le son de la pioche, quatre fois par seconde)
float g_activity;		// (casser et poser animent la musique)
float g_stepDist;		// (les pas : la distance marchée depuis le dernier)
double g_thunderAt, g_lastLightning;
int g_mineX, g_mineY = -999, g_mineZ;
double g_flashT;
double g_fps;
Hit g_target;
int g_targetEnt;		// la créature visée (0 : aucune)
double g_attackAt;		// (le prochain coup possible)
float g_hurtFlash;		// (un coup reçu : la vue rougit)
double g_lastHurt;		// (un peu d'invulnérabilité après un coup)
// la main en vue : le geste (0 à 1, -1 : aucun), l'objet qui remonte, la marche
float g_swing = -1, g_equip = 0, g_bob = 0, g_bobAmt = 0;
int g_heldShown = -1;
// les éclats : des petits cubes de la texture d'un bloc ou d'une créature, qui tombent
struct Particle {
	double x, y, z;
	float vx, vy, vz, life, size;
	int tile;
	float u, v;	// (le coin de leur morceau de tuile, en texels)
	bool rise;	// (la fumée monte)
};
std::vector<Particle> g_parts;

void swingArm ()
{
	if (g_swing < 0 || g_swing > 0.55f) g_swing = 0;
}

void addParticle (double x, double y, double z, float vx, float vy, float vz, int tile, float size, bool rise = false)
{
	if (g_parts.size () > 600) return;
	Particle p { x, y, z, vx, vy, vz, rise ? 1.2f : 0.6f + (rand () % 100) / 150.0f, size, tile, (float) (rand () % 12), (float) (rand () % 12), rise };
	g_parts.push_back (p);
}

inline float frnd () { return (rand () % 2001) / 1000.0f - 1.0f; }

// des éclats sur la face touchée (un coup), ou de tout le bloc (cassé)
void blockParticles (const Hit &h, int block, bool broken)
{
	const BlockInfo &bi = info (block);
	int tile = bi.shape == SH_CROSS || bi.shape == SH_SPECIAL ? bi.side : bi.side;
	if (broken) {
		for (int i = 0; i < 18; i++)
			addParticle (h.x + 0.5 + frnd () * 0.4, h.y + 0.5 + frnd () * 0.4, h.z + 0.5 + frnd () * 0.4, frnd () * 2.5f, 1.5f + frnd () * 2, frnd () * 2.5f,
				     tile, 0.07f + (rand () % 5) * 0.012f);
		return;
	}
	double fx = h.x + 0.5 + (h.nx - h.x) * 0.55, fy = h.y + 0.5 + (h.ny - h.y) * 0.55, fz = h.z + 0.5 + (h.nz - h.z) * 0.55;
	for (int i = 0; i < 3; i++)
		addParticle (fx + frnd () * 0.35, fy + frnd () * 0.35, fz + frnd () * 0.35, (h.nx - h.x) * 1.8f + frnd (), 1.2f + frnd (),
			     (h.nz - h.z) * 1.8f + frnd (), tile, 0.06f);
}

int skinOf (int type)
{
	switch (type) {
	case E_CHICKEN: return T_CHICKEN_BODY;
	case E_PIG: return T_PIG_SKIN;
	case E_WOLF: return T_WOLF_FUR;
	case E_RODEUR: return T_RODEUR_SKIN;
	default: return T_SPIDER_BODY;
	}
}
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
	fprintf (f, "# TerraCube\nworld = %s\ndistance = %d\napi = %d\nmusic = %d\nmusicvol = %d\nsound = %d\nsoundvol = %d\n", g.world.name.c_str (),
		 g.distance, apiRunning () ? 1 : 0, g.musicOn ? 1 : 0, (int) (g.musicVol * 100 + 0.5f), g.sfxOn ? 1 : 0, (int) (g.sfxVol * 100 + 0.5f));
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
	fprintf (f, "\nselected = %d\nmode = %s\nweather = %s\nweathercycle = %d\nfuel = %.2f\ninventory = %s\n", g.selected, modeName (g.mode),
		 weatherName (g.weather), g.weatherCycle ? 1 : 0, g.fuel, g.inv.save ().c_str ());
	fprintf (f, "health = %.1f\nhunger = %.1f\nsaturation = %.1f\nspawn = %.2f %.2f %.2f\nmonsters = %d\n", g.health, g.hunger, g.saturation, g.spawnX,
		 g.spawnY, g.spawnZ, g.mobs.monsters ? 1 : 0);
	bool ok = !ferror (f);
	fclose (f);
	FILE *fe = fopen ((g.world.dir + "/entities.txt").c_str (), "w");	// (les animaux, les loups apprivoisés)
	if (fe) {
		std::string t = g.mobs.save ();
		fwrite (t.data (), 1, t.size (), fe);
		fclose (fe);
	}
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
	g.mode = MODE_CREATIVE;
	g.inv.clear ();
	g.fuel = 0;
	g.weather = W_CLEAR;
	g.weatherLevel = 0;
	g.weatherCycle = true;
	g.weatherNext = 0;
	g.health = 20, g.hunger = 20, g.saturation = 5, g.exhaustion = 0, g.air = 15, g.burn = 0;
	g.spawnY = -1;
	g.mobs.clear ();
	g.mobs.monsters = true;
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
			else if (p.first == "mode") g.mode = !strcmp (v, "survival") ? MODE_SURVIVAL : MODE_CREATIVE;
			else if (p.first == "weather") {
				for (int w = 0; w < 4; w++)
					if (!strcmp (v, weatherName (w))) g.weather = w, g.weatherLevel = w ? 0.8f : 0;
			} else if (p.first == "weathercycle") g.weatherCycle = atoi (v) != 0;
			else if (p.first == "fuel") g.fuel = (float) atof (v);
			else if (p.first == "inventory") g.inv.load (p.second);
			else if (p.first == "health") g.health = std::max (1.0f, std::min (20.0f, (float) atof (v)));
			else if (p.first == "hunger") g.hunger = std::max (0.0f, std::min (20.0f, (float) atof (v)));
			else if (p.first == "saturation") g.saturation = (float) atof (v);
			else if (p.first == "spawn") sscanf (v, "%lf %lf %lf", &g.spawnX, &g.spawnY, &g.spawnZ);
			else if (p.first == "monsters") g.mobs.monsters = atoi (v) != 0;
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
	sim::attach (&g.world);
	for (auto *c : g.world.freed) R.freeChunk (c), delete c;
	g.world.freed.clear ();
	g.events.clear ();
	g.edited = false;
	if (py >= 0) gameTeleport (px, py, pz);
	else spawnNear (0, 0);
	if (g.spawnY < 0) g.spawnX = g.pl.x, g.spawnY = g.pl.y, g.spawnZ = g.pl.z;
	if (exists) {
		FILE *fe = fopen ((g.world.dir + "/entities.txt").c_str (), "r");
		if (fe) {
			std::string t;
			char b[4096];
			size_t n;
			while ((n = fread (b, 1, sizeof b, fe)) > 0) t.append (b, n);
			fclose (fe);
			g.mobs.load (t);
		}
	}
	// le sol autour, tout de suite
	int cx = floorDiv ((int) floor (g.pl.x), CW), cz = floorDiv ((int) floor (g.pl.z), CW);
	for (int dz = -2; dz <= 2; dz++)
		for (int dx = -2; dx <= 2; dx++) g.world.ensure (cx + dx, cz + dz);
	if (!exists) {
		std::string err;
		saveWorldIni (err);
	}
	if (g.mode == MODE_SURVIVAL) g.pl.fly = false;
	ui_menu_state (14, g.pl.fly ? UMS_CHECKED : 0);
	ui_menu_state (15, g.timeRuns ? UMS_CHECKED : 0);
	ui_menu_state (5, g.mode == MODE_SURVIVAL ? UMS_CHECKED : 0);
	gamePost (std::string (exists ? TR ("Monde « ") : TR ("Nouveau monde « ")) + name + TR (" », graine ") + std::to_string (sd) +
		  (g.mode == MODE_SURVIVAL ? TR (", mode survie") : TR (", mode créatif")));
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
	g_view.w = std::min (w, g_view.capW);
	g_view.h = std::min (h, g_view.capH);
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

struct CraftUI {
	bool open = false;
	int n = 2;			// 2 ou 3
	bool furnace = false;
	Stack grid[9], fin, cursor;	// la grille, l'entrée du four, ce que tient la souris
	int mx = -100, my = -100;	// la souris dans la vue
	unsigned buttons;
	int dragFrom = -1;		// la case d'où vient ce que tient la souris pendant un glisser (-1 : aucun, -2 : le résultat)
	bool dragOne;			// (bouton droit : un seul objet)
	std::string msg;
	int ox, oy;			// le coin du panneau dans la vue
	bool book = false;		// le livre des recettes, à la place des cases
	int bookScroll = 0;
} C_;

const int CELL = 44, GAP = 4, STEP = CELL + GAP, PAD = 16;
const int PW = PAD * 2 + 9 * STEP - GAP;		// (la largeur de l'inventaire)
const int TOP = 40;					// la grille
const int INV_Y = TOP + 3 * STEP + 36;			// le sac
const int BAR_Y = INV_Y + 3 * STEP + 8;			// la barre
const int PH = BAR_Y + CELL + PAD;

int slotAt (int x, int y);
Stack *slotStack (int id);
const Recipe *gridRecipe ();
const Recipe *furnaceRecipe ();
int resultY ();
std::string craftTipText ();

void blendRect (int x, int y, int w, int h, unsigned rgb, int alpha);

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

// les textes du panneau de fabrication : posés par UIKit par-dessus la vue, sur un fond transparent qui a la couleur du
// panneau (les bords des lettres s'y mélangent : ils restent nets sur le panneau dessiné par le GPU)
void craftText (const std::string &t, int x, int base, unsigned col, int size, long font, bool clear = true)
{
	long w = ui_text_width ((long) t.c_str (), size, font);
	int y = base - size - 2, h = size + 8;
	(void) clear;	// (le panneau, dessiné par le GPU, est déjà dessous)
	ui_text ((long) t.c_str (), x, base, col, size, font, 0);
	g_zones.push_back ({ x - 2, y, (int) w + 4, h });
}

void craftTexts ()
{
	long f = (long) ui.font, fb = (long) ui.font_b;
	int x0 = C_.ox + PAD;
	if (C_.book) {	// le livre des recettes, à la place des cases
		ui_fill (C_.ox, C_.oy, PW, PH, jet (TJ_SURFACE));
		g_zones.push_back ({ C_.ox, C_.oy, PW, PH });
		ui_text ((long) TR ("Livre des recettes"), x0, C_.oy + 28, jet (TJ_TEXT1), 15, fb, 0);
		ui_text ((long) TR ("molette : faire défiler · clic ou R : refermer"), x0 + 170, C_.oy + 28, jet (TJ_TEXT3), 12, f, 0);
		std::vector<std::string> lines;
		for (auto &r : recipes ())
			if (r.station != ST_FURNACE)
				lines.push_back (std::string (r.outN > 1 ? std::to_string (r.outN) + " × " : "") + itemInfo (r.out).label +
						 (r.station == ST_WORKBENCH ? TR (" (établi)") : "") + " : " + recipeText (r));
		lines.push_back (TR ("Au four : minerai → lingot, sable → verre, pavé → pierre, argile → briques, tronc → charbon"));
		const int LH = 17, rows = (PH - 56) / LH;
		C_.bookScroll = std::max (0, std::min (C_.bookScroll, (int) lines.size () - rows));
		for (int i = 0; i < rows && C_.bookScroll + i < (int) lines.size (); i++)
			ui_text_fit ((long) lines[(size_t) (C_.bookScroll + i)].c_str (), x0, C_.oy + 54 + i * LH, jet (TJ_TEXT2), 12, f, PW - 2 * PAD);
		return;
	}
	craftText (C_.n == 3 ? TR ("Fabrication · établi") : TR ("Fabrication · à la main"), x0, C_.oy + 28, jet (TJ_TEXT1), 14, fb);
	std::string hint = TR ("R : recettes");
	craftText (hint, C_.ox + PW - PAD - (int) ui_text_width ((long) hint.c_str (), 12, f), C_.oy + 28, jet (TJ_TEXT3), 12, f);
	if (C_.furnace) craftText (std::string (TR ("Four · chauffe ")) + std::to_string ((int) g.fuel), C_.ox + PW - PAD - 2 * CELL - 60, C_.oy + TOP + STEP - 10,
				   jet (TJ_TEXT2), 12, f);
	if (const Recipe *r = gridRecipe ())
		craftText (itemInfo (r->out).label, C_.ox + PAD + C_.n * STEP + 52 - 8, resultY () + CELL + 18, jet (TJ_ACCENT), 12, f);
	craftText (TR ("Inventaire"), x0, C_.oy + INV_Y - 10, jet (TJ_TEXT1), 14, fb);
	// sous le panneau, le dernier message, sur un fond à demi transparent
	if (!C_.msg.empty ()) {
		long w = std::min ((long) PW, ui_text_width ((long) C_.msg.c_str (), 12, f) + 20);
		int x = C_.ox + (PW - (int) w) / 2, y = C_.oy + PH + 8;
		blendRect (x, y, (int) w, 24, jet (TJ_SURFACE), 175);
		ui_text_fit ((long) C_.msg.c_str (), x + 10, y + 17, jet (TJ_TEXT1), 12, f, (int) w - 20);
		g_zones.push_back ({ x, y, (int) w, 24 });
	}
	// le nom de l'objet survolé
	std::string tip = craftTipText ();
	if (!tip.empty ()) {
		long w = ui_text_width ((long) tip.c_str (), 12, f) + 14;
		int x = std::min (C_.mx + 14, (int) ui.w - (int) w - 4), y = C_.my - 30;
		panel (x, y, (int) w, 22);
		ui_text ((long) tip.c_str (), x + 7, y + 16, jet (TJ_TEXT1), 12, f, 0);
	}
}

// un rectangle à demi transparent (opacité 0 à 255), mélangé à l'image (la console, les messages)
void blendRect (int x, int y, int w, int h, unsigned rgb, int alpha)
{
	int x0 = std::max (0, x), y0 = std::max (0, y), x1 = std::min ((int) ui.w, x + w), y1 = std::min ((int) ui.h, y + h);
	unsigned a = (unsigned) alpha, ia = 255 - a;
	unsigned cr = rgb >> 16 & 255, cg = rgb >> 8 & 255, cb = rgb & 255;
	for (int yy = y0; yy < y1; yy++) {
		uint32_t *p = ui.surf + (size_t) yy * ui.w;
		for (int xx = x0; xx < x1; xx++) {
			uint32_t d = p[xx];
			p[xx] = ((d >> 16 & 255) * ia + cr * a) / 255 << 16 | ((d >> 8 & 255) * ia + cg * a) / 255 << 8 | ((d & 255) * ia + cb * a) / 255;
		}
	}
}

// les textes par-dessus la vue, dans l'image en cours (present)
void drawTexts ()
{
	double t = nowMs ();
	// ce qui est montré : ne redessiner que si cela change
	std::vector<std::string> lines, chat;
	if (g_showInfo) {
		char l[160];
		static const char *WN[4] = { "", " · pluie", " · neige", " · orage" };
		snprintf (l, sizeof l, "%s · %s · %s%s", g.world.name.c_str (), g.mode == MODE_SURVIVAL ? TR ("survie") : TR ("créatif"),
			  hourText ().c_str (), TR (WN[g.weather & 3]));
		lines.push_back (l);
		snprintf (l, sizeof l, "x %d  y %d  z %d", (int) floor (g.pl.x), (int) floor (g.pl.y), (int) floor (g.pl.z));
		lines.push_back (l);
		if (g_showDebug) {
			snprintf (l, sizeof l, "%.0f %s · %ld %s · %ld %s", g_fps, TR ("img/s"), R.drawnChunks, TR ("tronçons"), R.drawnQuads, TR ("faces"));
			lines.push_back (l);
			lines.push_back (std::string (g_mode == ARMGL_GPU ? TR ("GPU : ") : TR ("Processeur : ")) + R.rendererName ());
			snprintf (l, sizeof l, "%s %u · %s", TR ("graine"), g.world.seed, biomeName ((int) floor (g.pl.x), (int) floor (g.pl.z)));
			lines.push_back (l);
			if (Entity *e = g_targetEnt ? g.mobs.get (g_targetEnt) : nullptr) {
				snprintf (l, sizeof l, "%s : %s%s, %s %.0f", TR ("visé"), entityInfo (e->type).label, e->tamed ? TR (" apprivoisé") : "", TR ("santé"), e->health);
				lines.push_back (l);
			} else if (g_target.hit) {
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
		if (t - c.t < 12000 && !g_console) chat.push_back (c.text);
	while (chat.size () > 6) chat.erase (chat.begin ());
	bool flash = !g_flash.empty () && t - g_flashT < 1500;
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
	if (g_console) {
		// la console, semi-transparente : son fond mélangé à l'image ; le texte, opaque
		const int N = 12;
		int pw = std::min (W - 24, 900), ph = (N + 1) * LH + 22;
		int x = 12, y = H - 78 - ph;
		// (un fond sombre et un texte clair, quel que soit le thème : ses propres jetons, inversés en thème clair)
		bool dark = g_jet[TJ_DARK / 4] != 0;
		unsigned bg = dark ? jet (TJ_SURFACE) : jet (TJ_TEXT1), fg = dark ? jet (TJ_TEXT1) : jet (TJ_SURFACE), fg2 = dark ? jet (TJ_TEXT2) : jet (TJ_SUNKEN);
		blendRect (x, y, pw, ph, bg, 167);
		g_zones.push_back ({ x, y, pw, ph });
		size_t first = g.console.size () > (size_t) N ? g.console.size () - N : 0;
		long mono = ui_font_get (UFN_MONO);
		for (size_t i = first; i < g.console.size (); i++)
			ui_text_fit ((long) g.console[i].c_str (), x + 12, y + 8 + LH * (int) (i - first) + 14,
				     g.console[i].rfind ("> ", 0) == 0 ? fg : fg2, SZ, mono, pw - 24);
		int ly = y + ph - LH - 6;
		blendRect (x + 8, ly - 2, pw - 16, LH + 2, bg, 120);
		std::string in = "> " + g_cin + (((long) (t / 530)) % 2 ? "▏" : " ");
		ui_text_fit ((long) in.c_str (), x + 12, ly + 13, fg, SZ, mono, pw - 24);
	}
	if (C_.open) craftTexts ();
	if (flash) {
		int w = (int) ui_text_width ((long) g_flash.c_str (), SZ, (long) ui.font_b) + 24;
		panel ((W - w) / 2, H - 112, w, 28);
		ui_text ((long) g_flash.c_str (), (W - w) / 2 + 12, H - 112 + 19, jet (TJ_TEXT1), SZ, (long) ui.font_b, 0);
	}
	if (!g_locked && !g_selftest && !g_console && !C_.open) {
		const char *a = TR ("Cliquez dans la vue pour jouer");
		const char *b = g.mode == MODE_SURVIVAL ? TR ("ZQSD : marcher · Espace : sauter · clic tenu : casser · I : inventaire et fabrication · T : console · Échap : la souris")
							: TR ("ZQSD ou flèches : marcher · Espace : sauter · F : voler · B : blocs · T : console · Échap : la souris");
		int wa = (int) ui_text_width ((long) a, 16, (long) ui.font_b), wb = (int) ui_text_width ((long) b, SZ, font);
		int pw = std::max (wa, wb) + 40, ph = 70;
		if (pw > W - 20) pw = W - 20;
		int x = (W - pw) / 2, y = H / 2 - ph - 30;
		panel (x, y, pw, ph);
		ui_text ((long) a, (W - wa) / 2, y + 28, jet (TJ_ACCENT), 16, (long) ui.font_b, 0);
		ui_text_fit ((long) b, std::max (x + 12, (W - wb) / 2), y + 52, jet (TJ_TEXT2), SZ, font, pw - 24);
	}
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

bool survival () { return g.mode == MODE_SURVIVAL; }

// l'objet d'une case de la barre (créatif : un bloc ; survie : la pile de l'inventaire)
int slotItem (int i) { return survival () ? (g.inv.s[i].empty () ? 0 : g.inv.s[i].item) : g.hotbar[i]; }

// ce qui tombe d'un bloc cassé, dans l'inventaire ; l'outil s'use
// gauche (-1) ou droite (1) d'un bloc vu du joueur, pour le son
float panOf (int x, int z)
{
	double dx = x + 0.5 - g.pl.x, dz = z + 0.5 - g.pl.z, d = sqrt (dx * dx + dz * dz);
	if (d < 0.5) return 0;
	double yaw = g.pl.yaw * M_PI / 180.0;
	return (float) ((dx * cos (yaw) + dz * sin (yaw)) / d * 0.7);
}

void collect (int b)
{
	Stack &held = g.inv.s[g.selected];
	bool harvest;
	mineTime (b, held.empty () ? 0 : held.item, &harvest);
	Mining m = mining (b);
	if (b == TALL_GRASS && rand () % 100 < 15) g.inv.add (I_SEEDS, 1);	// (des graines, pour les poules)
	if (b == LEAVES && rand () % 100 < 6) g.inv.add (I_APPLE, 1);		// (une pomme, pour soi ou les cochons)
	g.exhaustion += 0.005f;
	if (harvest && m.drop && (m.chance >= 1 || rand () % 1000 < (int) (m.chance * 1000))) {
		if (g.inv.add (m.drop, m.dropN) > 0) gamePost (TR ("Inventaire plein : l'objet est perdu"));
		else audio::play (audio::S_PICKUP, 0, 0.7f);
	} else if (!harvest && m.tier > 0)
		gamePost (std::string (info (b).label) + TR (" : il faut une meilleure pioche pour le ramasser"));
	ItemInfo ti = itemInfo (held.empty () ? 0 : held.item);
	if (!held.empty () && ti.durability && ++held.dur >= ti.durability) {
		gamePost (std::string (ti.label) + TR (" s'est cassé"));
		audio::play (audio::S_TOOLBREAK);
		held = Stack ();
	}
}

void doBreak ()
{
	int b = g_target.block, m = g.world.meta (g_target.x, g_target.y, g_target.z);
	event (1, g_target, b);
	blockParticles (g_target, b, true);
	swingArm ();
	audio::play (audio::S_BREAK, audio::materialOf (b), 1.0f, panOf (g_target.x, g_target.z));
	g_activity = std::min (1.0f, g_activity + 0.12f);
	g.world.set (g_target.x, g_target.y, g_target.z, AIR);
	int base = sim::broken (g_target.x, g_target.y, g_target.z, b, m);	// (la tête d'un piston : le piston avec)
	if (survival ()) {
		collect (b);
		if (base && g.inv.add (base, 1) > 0) gamePost (TR ("Inventaire plein : l'objet est perdu"));
	}
	// une plante sur le bloc cassé tombe avec lui
	int up = g.world.get (g_target.x, g_target.y + 1, g_target.z);
	if (info (up).shape == SH_CROSS) {
		g.world.set (g_target.x, g_target.y + 1, g_target.z, AIR);
		if (survival () && mining (up).drop && g.inv.add (mining (up).drop, 1) > 0) gamePost (TR ("Inventaire plein : l'objet est perdu"));
	}
	g.edited = true;
}

// frapper la créature visée (une fois toutes les 0,45 s)
void attackEntity ()
{
	Entity *e = g.mobs.get (g_targetEnt);
	double t = nowMs ();
	if (!e || t < g_attackAt) return;
	g_attackAt = t + 450;
	int held = survival () ? slotItem (g.selected) : 0;
	float dmg = survival () ? attackDamage (held) : 5.0f;
	g.mobs.hurtEntity (*e, dmg, g.pl.x, g.pl.z, -1);
	audio::play (audio::S_HIT, audio::M_SOFT, 0.7f, panOf ((int) floor (e->x), (int) floor (e->z)));
	swingArm ();
	float hh = entityInfo (e->type).height * (e->age < 0 ? 0.6f : 1.0f);
	for (int i = 0; i < 5; i++)
		addParticle (e->x + frnd () * 0.3, e->y + hh * 0.6 + frnd () * 0.2, e->z + frnd () * 0.3, frnd () * 2, 1.5f + frnd (), frnd () * 2, skinOf (e->type), 0.06f);
	if (!survival ()) return;
	g.exhaustion += 0.1f;
	Stack &st = g.inv.s[g.selected];
	ItemInfo ti = itemInfo (st.empty () ? 0 : st.item);
	if (!st.empty () && ti.durability && (st.dur += ti.tool == TOOL_SWORD ? 1 : 2) >= ti.durability) {
		gamePost (std::string (ti.label) + TR (" s'est cassé"));
		audio::play (audio::S_TOOLBREAK);
		st = Stack ();
	}
}

// créatif : casser tout de suite (le mode survie casse peu à peu, dans frame)
void breakBlock ()
{
	if (g_targetEnt) {
		attackEntity ();
		return;
	}
	if (!g_target.hit || g_target.block == BEDROCK || survival ()) return;
	doBreak ();
}

void inventoryWindow (bool workbench = false);

// un seau : vide, il prend la source visée (eau ou lave) ; plein, il la pose
bool useBucket (int item)
{
	float eye[3], dir[3];
	eyeOf (eye, dir);
	Hit h = g.world.raycast (eye, dir, survival () ? 5.0f : 6.0f, true);
	if (!h.hit) return false;
	Stack &st = g.inv.s[g.selected];
	if (item == I_BUCKET) {
		if (!isLiquid (h.block) || g.world.meta (h.x, h.y, h.z) != 0) return false;	// (une source seulement)
		int full = h.block == WATER ? I_WATER_BUCKET : I_LAVA_BUCKET;
		g.world.set (h.x, h.y, h.z, AIR);
		if (st.count > 1) {
			st.count--;
			if (g.inv.add (full, 1) > 0) gamePost (TR ("Inventaire plein : le seau est perdu"));
		} else
			st = Stack (), st.item = full, st.count = 1;
		audio::play (audio::S_SPLASH, 0, 0.5f, panOf (h.x, h.z));
		g.edited = true;
		return true;
	}
	int liquid = item == I_WATER_BUCKET ? WATER : LAVA;
	int x = h.nx, y = h.ny, z = h.nz;
	if (isLiquid (h.block) || info (h.block).shape == SH_CROSS) x = h.x, y = h.y, z = h.z;
	int there = g.world.get (x, y, z);
	if (there != AIR && !isLiquid (there) && info (there).shape != SH_CROSS && info (there).shape != SH_SPECIAL) return false;
	if (y < 0 || y >= CH) return false;
	g.world.set (x, y, z, liquid, 0);
	st = Stack (), st.item = I_BUCKET, st.count = 1;
	audio::play (audio::S_SPLASH, 0, 0.5f, panOf (x, z));
	g.edited = true;
	return true;
}

// manger ce qu'on tient (survie) -> vrai s'il a mangé
bool eatHeld ()
{
	if (!survival ()) return false;
	Stack &st = g.inv.s[g.selected];
	if (st.empty ()) return false;
	Food f = foodOf (st.item);
	if (!f.hunger || g.hunger >= 20) return false;
	g.hunger = std::min (20.0f, g.hunger + f.hunger);
	g.saturation = std::min (g.hunger, g.saturation + f.saturation);
	audio::play (audio::S_EAT, 0, 0.8f);
	if (--st.count <= 0) st = Stack ();
	return true;
}

void placeBlock ()
{
	// une créature visée : la nourrir, l'apprivoiser, l'asseoir…
	if (g_targetEnt) {
		Entity *e = g.mobs.get (g_targetEnt);
		int held = slotItem (g.selected);
		if (e && !survival ())	// (créatif : la barre n'a que des blocs ; on fait comme si on tenait ce qu'il faut)
			held = e->type == E_WOLF ? (e->tamed ? 0 : I_BONE) : e->type == E_CHICKEN ? I_SEEDS : e->type == E_PIG ? I_APPLE : 0;
		bool used;
		std::string msg;
		if (e && g.mobs.interact (*e, held, used, msg)) {
			swingArm ();
			if (!msg.empty ()) gamePost (msg);
			if (used && survival ()) {
				Stack &st = g.inv.s[g.selected];
				if (--st.count <= 0) st = Stack ();
			}
			return;
		}
	}
	if (eatHeld ()) {
		swingArm ();
		return;
	}
	if (!g_target.hit) return;
	bool sneak = g_keys[0xE1] || g_keys[0xE5];
	// survie : un clic droit sur un établi ou un four les ouvre (Maj : poser contre eux)
	if (survival () && (g_target.block == WORKBENCH || g_target.block == FURNACE) && !sneak) {
		g_buttons = 0;
		inventoryWindow (g_target.block == WORKBENCH);
		return;
	}
	int b = slotItem (g.selected);
	// un levier, un bouton, un répéteur… servent (Maj : poser contre eux)
	if (!sneak && sim::use (g_target.x, g_target.y, g_target.z, b)) {
		swingArm ();
		g_activity = std::min (1.0f, g_activity + 0.05f);
		g.edited = true;
		return;
	}
	if (survival () && (b == I_BUCKET || b == I_WATER_BUCKET || b == I_LAVA_BUCKET)) {
		useBucket (b);
		return;
	}
	if (!isBlockItem (b)) return;
	int x = g_target.nx, y = g_target.ny, z = g_target.nz;
	// (une plante visée se remplace)
	if (info (g_target.block).shape == SH_CROSS) x = g_target.x, y = g_target.y, z = g_target.z;
	if (y < 0 || y >= CH) return;
	int there = g.world.get (x, y, z);
	if (there != AIR && !isLiquid (there) && info (there).shape != SH_CROSS) return;
	if (isSolid (b) && playerInside (g.pl, x, y, z)) return;
	if (info (b).shape == SH_CROSS && !isSolid (g.world.get (x, y - 1, z))) return;
	int dx = x - g_target.x, dy = y - g_target.y, dz = z - g_target.z;
	int face = dx > 0 ? 0 : dx < 0 ? 1 : dy > 0 ? 2 : dy < 0 ? 3 : dz > 0 ? 4 : dz < 0 ? 5 : -1;
	int m = sim::placeMeta (b, x, y, z, face, g.pl.yaw, g.pl.pitch);
	if (m < 0) return;	// (pas de support)
	g.world.set (x, y, z, b, m);
	if (survival ()) {
		Stack &st = g.inv.s[g.selected];
		if (--st.count <= 0) st = Stack ();
	}
	Hit h = g_target;
	h.x = x, h.y = y, h.z = z;
	event (2, h, b);
	swingArm ();
	audio::play (audio::S_PLACE, audio::materialOf (b), 1.0f, panOf (x, z));
	g_activity = std::min (1.0f, g_activity + 0.12f);
	g.edited = true;
}

// ---- la simulation : ses sons, ce qui tombe, ce qu'elle pousse --------------------------------------------------

void simSound (int s, int arg, int x, int y, int z)
{
	double dx = x + 0.5 - g.pl.x, dy = y + 0.5 - (g.pl.y + 1), dz = z + 0.5 - g.pl.z;
	double d = sqrt (dx * dx + dy * dy + dz * dz);
	float vol = (float) std::max (0.0, 1.0 - d / (s == sim::SS_EXPLODE ? 48.0 : 20.0));
	if (vol <= 0.01f) return;
	float pan = panOf (x, z);
	switch (s) {
	case sim::SS_CLICK: audio::play (audio::S_CLICK_ON, arg, vol, pan); break;
	case sim::SS_PISTON_OUT: audio::play (audio::S_PISTON_OUT, 0, vol, pan); break;
	case sim::SS_PISTON_IN: audio::play (audio::S_PISTON_IN, 0, vol, pan); break;
	case sim::SS_FIZZ: audio::play (audio::S_FIZZ, 0, vol, pan); break;
	case sim::SS_EXPLODE: audio::play (audio::S_EXPLODE, 0, vol, pan); break;
	case sim::SS_FUSE: audio::play (audio::S_FUSE, 0, vol, pan); break;
	case sim::SS_NOTE: {	// l'instrument : selon le bloc dessous
		int under = g.world.get (x, y - 1, z), in = audio::IN_HARP;
		switch (audio::materialOf (under)) {
		case audio::M_WOOD: in = audio::IN_BASS; break;
		case audio::M_STONE: in = audio::IN_DRUM; break;
		case audio::M_SAND: in = audio::IN_SNARE; break;
		case audio::M_GLASS: in = audio::IN_CLICK; break;
		case audio::M_METAL: in = audio::IN_BELL; break;
		case audio::M_SOFT: in = audio::IN_GUITAR; break;
		case audio::M_SNOW: in = audio::IN_CHIME; break;
		}
		audio::playNote (arg, in, vol, pan);
		g_activity = std::min (1.0f, g_activity + 0.02f);
		break;
	}
	}
}

void simDrop (int item, int x, int y, int z)
{
	if (!survival ()) return;
	double dx = x + 0.5 - g.pl.x, dz = z + 0.5 - g.pl.z;
	if (dx * dx + dz * dz > 64) return;	// (loin : perdu)
	g.inv.add (item, 1);
}

void hurtPlayer (float dmg, const char *cause, double fx, double fz, bool periodic = false);

void simPush (int x, int y, int z, int dir)
{
	for (auto &e : g.mobs.list)	// (les créatures dans la case poussée)
		if ((int) floor (e.x) == x && (int) floor (e.z) == z && (int) floor (e.y + 0.3) == y) e.x += sim::DX[dir], e.y += sim::DY[dir], e.z += sim::DZ[dir];
	if (!playerInside (g.pl, x, y, z)) return;
	g.pl.x += sim::DX[dir], g.pl.y += sim::DY[dir], g.pl.z += sim::DZ[dir];
	if (sim::DY[dir] > 0) g.pl.vy = std::max (g.pl.vy, 0.0);
}

void simBlast (double x, double y, double z, float power)
{
	double dx = g.pl.x - x, dy = g.pl.y + 1 - y, dz = g.pl.z - z;
	double d = sqrt (dx * dx + dy * dy + dz * dz);
	if (d > power * 2 || d < 0.01) return;
	double k = (1 - d / (power * 2)) * 18.0 / d;
	g.pl.vx += dx * k, g.pl.vy += dy * k + 4, g.pl.vz += dz * k;
}

void simBlastAll (double x, double y, double z, float power)
{
	simBlast (x, y, z, power);
	g.mobs.blast (x, y, z, power);
	double dx = g.pl.x - x, dy = g.pl.y + 1 - y, dz = g.pl.z - z, d = sqrt (dx * dx + dy * dy + dz * dz);
	if (d < power * 2) hurtPlayer ((float) floor ((1 - d / (power * 2)) * 20), TR ("une explosion"), x, z);
}

bool simOccupied (int x, int y, int z)
{
	if ((int) floor (g.pl.x) == x && (int) floor (g.pl.z) == z && g.pl.y >= y - 0.01 && g.pl.y < y + 0.6) return true;
	for (auto &e : g.mobs.list)
		if (e.dead < 0 && (int) floor (e.x) == x && (int) floor (e.z) == z && e.y >= y - 0.01 && e.y < y + 0.6) return true;
	return false;
}

void selectSlot (int i)
{
	g.selected = (i % 9 + 9) % 9;
	int it = slotItem (g.selected);
	g_flash = it ? itemInfo (it).label : TR ("Main vide");
	g_flashT = nowMs ();
}

// le bloc visé dans la main : créatif, dans la case ; survie, la case qui en a (ou il y vient du sac)
void pickBlock ()
{
	if (!g_target.hit) return;
	int b = g_target.block;
	if (b == BEDROCK || isLiquid (b) || b == PISTON_HEAD) return;
	if (!survival ()) {
		g.hotbar[g.selected] = b;
		selectSlot (g.selected);
		return;
	}
	for (int i = 0; i < Inventory::N; i++)
		if (!g.inv.s[i].empty () && g.inv.s[i].item == b) {
			if (i < 9) selectSlot (i);
			else {
				std::swap (g.inv.s[i], g.inv.s[g.selected]);
				selectSlot (g.selected);
			}
			return;
		}
}

// la liste des blocs, pour la case choisie (B, mode créatif)
void chooseBlock ()
{
	unlock ();
	std::string labels;
	std::vector<int> ids;
	for (int i = 1; i < BLOCK_COUNT; i++) {
		if (i == BEDROCK || i == PISTON_HEAD) continue;
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

// ---- l'inventaire et la table de fabrication (mode survie), dessinés dans la vue du jeu ---------------------------
//
// Par-dessus la vue (assombrie), un panneau dessiné par le GPU (render.cpp, CraftDraw) : en haut la table, une grille
// 2 × 2 à la main ou 3 × 3 à l'établi, et son résultat ; le four à droite s'il y en a un près ; en bas l'inventaire,
// le sac puis la barre. Les textes (titres, nom de l'objet survolé) sont posés par UIKit sur un fond transparent
// (drawTexts, dans l'image en cours). La souris est rendue au joueur pendant ce temps.
// Le glisser-déposer : appuyer sur une case prend sa pile (bouton droit : un seul objet), la lâcher sur une autre la
// pose ; lâchée ailleurs, elle revient. Un clic sans glisser garde l'objet sous la souris, un second clic le pose.
// Sur le résultat : le prendre (Maj : le plus possible, dans l'inventaire). 1 à 9 sur une case : vers cette case de la
// barre. R : le livre des recettes. Échap, I ou E : fermer ; ce qui reste dans la grille revient dans l'inventaire.

void craftPlace ()
{
	C_.ox = std::max (8, (g_view.w - PW) / 2);
	C_.oy = std::max (8, (g_view.h - PH) / 2 - 16);
}

// les cases : 0-8 la grille, 9 le résultat, 10 l'entrée du four, 11 son résultat, 20-55 l'inventaire
// (20-46 le sac, 47-55 la barre : cases 9-35 puis 0-8 de l'inventaire) ; en coordonnées de la vue
struct Slot {
	int id, x, y;
};

int resultY () { return C_.oy + TOP + (C_.n * STEP - GAP - CELL) / 2; }

std::vector<Slot> craftSlots ()
{
	std::vector<Slot> v;
	int x0 = C_.ox + PAD, y0 = C_.oy + TOP;
	for (int r = 0; r < C_.n; r++)
		for (int c = 0; c < C_.n; c++) v.push_back ({ r * C_.n + c, x0 + c * STEP, y0 + r * STEP });
	v.push_back ({ 9, x0 + C_.n * STEP + 52, resultY () });
	if (C_.furnace) {
		int fy = C_.oy + TOP + STEP;
		v.push_back ({ 10, C_.ox + PW - PAD - 2 * CELL - 60, fy });
		v.push_back ({ 11, C_.ox + PW - PAD - CELL, fy });
	}
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 9; c++) v.push_back ({ 20 + r * 9 + c, x0 + c * STEP, C_.oy + INV_Y + r * STEP });
	for (int c = 0; c < 9; c++) v.push_back ({ 47 + c, x0 + c * STEP, C_.oy + BAR_Y });
	return v;
}

Stack *slotStack (int id)
{
	if (id >= 0 && id < 9) return &C_.grid[id];
	if (id == 10) return &C_.fin;
	if (id >= 20 && id < 47) return &g.inv.s[9 + id - 20];
	if (id >= 47 && id < 56) return &g.inv.s[id - 47];
	return nullptr;
}

const Recipe *gridRecipe ()
{
	int it[9] = { 0 };
	for (int i = 0; i < C_.n * C_.n; i++) it[i] = C_.grid[i].empty () ? 0 : C_.grid[i].item;
	return matchGrid (it, C_.n);
}

const Recipe *furnaceRecipe ()
{
	if (C_.fin.empty ()) return nullptr;
	for (auto &r : recipes ())
		if (r.station == ST_FURNACE && r.in[0].item == C_.fin.item) return &r;
	return nullptr;
}

std::string craftTipText ()
{
	if (!C_.cursor.empty () || C_.book) return "";
	Stack *p = slotStack (slotAt (C_.mx, C_.my));
	if (!p || p->empty ()) return "";
	ItemInfo ii = itemInfo (p->item);
	std::string t = ii.label;
	if (ii.durability) t += " · " + std::to_string (ii.durability - p->dur) + TR (" coups");
	return t;
}

// ce que le GPU dessine (render.cpp)
void craftDraw (CraftDraw &d)
{
	d.x = C_.ox, d.y = C_.oy, d.w = PW, d.h = PH;
	d.book = C_.book;
	const Recipe *r = gridRecipe (), *fr = furnaceRecipe ();
	for (auto &sl : craftSlots ()) {
		CraftDraw::Cell c;
		c.x = sl.x, c.y = sl.y, c.size = CELL;
		Stack s;
		if (sl.id == 9 && r) s.item = r->out, s.count = r->outN;
		else if (sl.id == 11 && fr) s.item = fr->out, s.count = fr->outN;
		else if (Stack *p = slotStack (sl.id)) s = *p;
		c.item = s.empty () ? 0 : s.item;
		c.count = s.count;
		int dur = c.item ? itemInfo (c.item).durability : 0;
		c.wear = dur ? (float) s.dur / dur : -1;
		c.hover = C_.mx >= sl.x && C_.mx < sl.x + CELL && C_.my >= sl.y && C_.my < sl.y + CELL;
		c.result = sl.id == 9 || sl.id == 11;
		c.selected = sl.id >= 47 && sl.id - 47 == g.selected;
		d.cells.push_back (c);
	}
	// les flèches : la grille vers son résultat, l'entrée du four vers le sien
	int gx = C_.ox + PAD + C_.n * STEP - GAP;
	d.arrows.push_back ({ gx + 12, resultY () + CELL / 2, 52 - 24 - GAP });
	if (C_.furnace) d.arrows.push_back ({ C_.ox + PW - PAD - CELL - 60 + 10, C_.oy + TOP + STEP + CELL / 2, 40 });
	if (!C_.cursor.empty ()) {
		d.cursorItem = C_.cursor.item, d.cursorCount = C_.cursor.count;
		int dur = itemInfo (C_.cursor.item).durability;
		d.cursorWear = dur ? (float) C_.cursor.dur / dur : -1;
		d.mx = C_.mx, d.my = C_.my;
	}
}

// prendre le résultat de la grille une fois -> faux s'il n'y a rien ou plus de place sous la souris
bool takeGrid (bool toInventory)
{
	const Recipe *r = gridRecipe ();
	if (!r) return false;
	int mx = itemInfo (r->out).maxStack;
	if (toInventory) {
		Inventory t = g.inv;
		if (t.add (r->out, r->outN) > 0) {
			C_.msg = TR ("L'inventaire est plein");
			return false;
		}
		g.inv = t;
	} else {
		if (!C_.cursor.empty () && (C_.cursor.item != r->out || C_.cursor.count + r->outN > mx)) return false;
		if (C_.cursor.empty ()) C_.cursor = Stack (), C_.cursor.item = r->out;
		C_.cursor.count += r->outN;
	}
	for (int i = 0; i < C_.n * C_.n; i++)
		if (!C_.grid[i].empty () && --C_.grid[i].count <= 0) C_.grid[i] = Stack ();
	g.edited = true;
	C_.msg = std::string (TR ("Fabriqué : ")) + std::to_string (r->outN) + " × " + itemInfo (r->out).label;
	audio::play (audio::S_CRAFT);
	return true;
}

// cuire l'entrée du four une fois (le combustible : le premier objet qui brûle dans l'inventaire)
bool takeFurnace (bool toInventory)
{
	const Recipe *r = furnaceRecipe ();
	if (!r) return false;
	if (!toInventory && !C_.cursor.empty () && (C_.cursor.item != r->out || C_.cursor.count + r->outN > itemInfo (r->out).maxStack)) return false;
	if (g.fuel < 1) {
		int best = 0;
		for (int f : { (int) I_COAL, (int) LOG, (int) PINE_LOG, (int) PLANKS, (int) I_STICK })
			if (g.inv.count (f) > 0) {
				best = f;
				break;
			}
		if (!best) {
			C_.msg = TR ("Le four n'a pas de combustible : du charbon, du bois, des planches ou des bâtons dans l'inventaire");
			return false;
		}
		g.inv.take (best, 1);
		g.fuel += fuelValue (best);
	}
	if (toInventory) {
		if (g.inv.add (r->out, r->outN) > 0) return false;
	} else {
		if (C_.cursor.empty ()) C_.cursor = Stack (), C_.cursor.item = r->out;
		C_.cursor.count += r->outN;
	}
	g.fuel -= 1;
	if (--C_.fin.count <= 0) C_.fin = Stack ();
	C_.msg = std::string (TR ("Cuit : ")) + itemInfo (r->out).label;
	audio::play (audio::S_CRAFT, 0, 0.7f);
	g.edited = true;
	return true;
}

int slotAt (int x, int y)
{
	for (auto &sl : craftSlots ())
		if (x >= sl.x && x < sl.x + CELL && y >= sl.y && y < sl.y + CELL) return sl.id;
	return -1;
}

// poser ce que tient la souris sur une case (tout, ou un seul) ; une autre sorte d'objet : échange
void dropOn (Stack *s, bool one)
{
	Stack &c = C_.cursor;
	if (c.empty () || !s) return;
	int mx = itemInfo (c.item).maxStack;
	if (one) {
		if (s->empty ()) *s = c, s->count = 0;
		else if (s->item != c.item || s->count >= mx) return;
		s->count++;
		if (--c.count <= 0) c = Stack ();
	} else if (s->empty ()) std::swap (c, *s);
	else if (s->item == c.item) {
		int k = std::min (c.count, mx - s->count);
		s->count += k, c.count -= k;
		if (c.count <= 0) c = Stack ();
	} else std::swap (c, *s);
}

// ce que tient la souris revient d'où il vient (ou dans l'inventaire)
void returnCursor ()
{
	if (C_.cursor.empty ()) return;
	Stack *o = slotStack (C_.dragFrom);
	if (o && (o->empty () || (o->item == C_.cursor.item && o->count + C_.cursor.count <= itemInfo (o->item).maxStack))) dropOn (o, false);
	if (!C_.cursor.empty () && g.inv.add (C_.cursor.item, C_.cursor.count) == 0) C_.cursor = Stack ();
}

// la souris dans le panneau : kind 0 déplacement, 1 appui, 2 relâché ; infos = boutons << 8 | modificateurs << 16
long craftMouse (long, long kind, long x, long y, long infos)
{
	C_.mx = (int) x, C_.my = (int) y;
	unsigned b = (unsigned) (infos >> 8) & 0xFF, mods = (unsigned) (infos >> 16);
	unsigned pressed = kind == 1 ? b & ~C_.buttons : 0;
	C_.buttons = b;
	if (C_.book) {
		if (pressed) C_.book = false;	// (un clic referme le livre)
		return 0;
	}
	int id = slotAt ((int) x, (int) y);
	if (kind == 2 && C_.dragFrom != -1) {	// (lâché ; -2 : le résultat)
		int from = C_.dragFrom;
		C_.dragFrom = -1;
		if (id == from || (from == -2 && (id == 9 || id == 11))) return 0;	// (un simple clic : l'objet reste sous la souris)
		if (id >= 0 && id != 9 && id != 11) {
			dropOn (slotStack (id), C_.dragOne);
			if (!C_.cursor.empty () && C_.dragOne) return 0;
		}
		C_.dragFrom = from;
		returnCursor ();
		C_.dragFrom = -1;
		return 0;
	}
	if (!pressed || id < 0) return 0;
	bool left = pressed & 1, shift = mods & UI_MOD_SHIFT;
	C_.msg.clear ();
	if (id == 9 || id == 11) {
		bool (*take) (bool) = id == 9 ? takeGrid : takeFurnace;
		if (shift) {
			int k = 0;
			while (k < 4096 && take (true)) k++;
		} else if (take (false) && C_.dragFrom < 0) C_.dragFrom = -2, C_.dragOne = false;	// (le résultat se glisse aussi)
		return 0;
	}
	Stack *s = slotStack (id);
	if (!s) return 0;
	if (shift && !s->empty () && id >= 20) {	// (Maj + clic dans l'inventaire : entre le sac et la barre)
		bool bar = id >= 47;
		for (int i = bar ? 9 : 0; i < (bar ? 36 : 9); i++)
			if (g.inv.s[i].empty ()) {
				g.inv.s[i] = *s, *s = Stack ();
				break;
			}
		return 0;
	}
	if (!C_.cursor.empty ()) {	// (un objet déjà sous la souris : le poser)
		dropOn (s, !left);
		audio::play (audio::S_CLICK);
		return 0;
	}
	if (s->empty ()) return 0;
	// prendre : toute la pile, ou un seul objet (bouton droit), et glisser
	C_.dragOne = !left;
	C_.cursor = *s;
	if (left) *s = Stack ();
	else {
		C_.cursor.count = 1;
		if (--s->count <= 0) *s = Stack ();
	}
	C_.dragFrom = id;
	audio::play (audio::S_CLICK);
	return 0;
}

void craftClose ()
{
	if (!C_.open) return;
	C_.open = false;
	C_.book = false;
	// ce qui reste dans la grille, le four et sous la souris revient dans l'inventaire
	int lost = 0;
	for (auto &s : C_.grid)
		if (!s.empty ()) lost += g.inv.add (s.item, s.count), s = Stack ();
	if (!C_.fin.empty ()) lost += g.inv.add (C_.fin.item, C_.fin.count), C_.fin = Stack ();
	if (!C_.cursor.empty ()) lost += g.inv.add (C_.cursor.item, C_.cursor.count), C_.cursor = Stack ();
	C_.dragFrom = -1;
	if (lost) gamePost (TR ("Inventaire plein : des objets sont perdus"));
	selectSlot (g.selected);
	lock ();
}

// une touche pendant que le panneau est ouvert
void craftKey (int code)
{
	if (code == KEY_ESC || code == 0x0C || code == 0x08 || code == 0x05) craftClose ();	// Échap, I, E, B
	else if (code == 0x15) C_.book = !C_.book, C_.bookScroll = 0;			// R : le livre des recettes
	else if (code >= 0x1E && code <= 0x26) {	// 1 à 9 sur une case : vers cette case de la barre
		int id = slotAt (C_.mx, C_.my);
		Stack *s = slotStack (id);
		int k = code - 0x1E;
		if (s && s != &g.inv.s[k]) std::swap (*s, g.inv.s[k]);
	}
}

void inventoryWindow (bool workbench)
{
	if (!survival ()) {
		chooseBlock ();
		return;
	}
	if (C_.open) {
		craftClose ();
		return;
	}
	bool wb, fu;
	gameStations (wb, fu);
	C_.n = workbench || wb ? 3 : 2;
	C_.furnace = fu;
	for (auto &s : C_.grid) s = Stack ();
	C_.fin = C_.cursor = Stack ();
	C_.msg = TR ("Glissez les objets vers la grille (bouton droit : un seul) · Maj + clic sur le résultat : le plus possible · R : recettes");
	C_.buttons = 0;
	C_.dragFrom = -1;
	C_.book = false;
	C_.open = true;
	craftPlace ();
	unlock ();		// (la souris au joueur)
	g_buttons = 0;
}

// ---- la console de commandes (T, / ou ²) ------------------------------------------------------------------------

void consoleOpen (const char *prefill)
{
	g_console = true;
	g_cin = prefill;
	g_histPos = (int) g_hist.size ();
	g_consoleWasLocked = g_locked;
	unlock ();
	if (g.console.empty ()) g.console.push_back (TR ("Console : help pour la liste des commandes, Échap pour fermer."));
	if (g_selftest) printf ("terracube : [console] ouverte\n"), fflush (stdout);
}

void consoleClose ()
{
	g_console = false;
	if (g_selftest) printf ("terracube : [console] fermée\n"), fflush (stdout);
	if (g_consoleWasLocked) lock ();
}

void consoleLog (const std::string &s)
{
	g.console.push_back (s);
	while (g.console.size () > 200) g.console.pop_front ();
}

void consoleRun ()
{
	std::string line = g_cin;
	g_cin.clear ();
	if (line.empty ()) return;
	if (g_hist.empty () || g_hist.back () != line) g_hist.push_back (line);
	g_histPos = (int) g_hist.size ();
	consoleLog ("> " + line);
	auto out = consoleExec (line);
	for (auto &l : out) consoleLog (l);
	if (g_selftest) printf ("terracube : [console] > %s (%zu lignes)\n", line.c_str (), out.size ()), fflush (stdout);
}

void utf8Put (std::string &s, unsigned cp)
{
	if (cp < 0x80) s += (char) cp;
	else if (cp < 0x800) s += (char) (0xC0 | cp >> 6), s += (char) (0x80 | (cp & 63));
	else s += (char) (0xE0 | cp >> 12), s += (char) (0x80 | (cp >> 6 & 63)), s += (char) (0x80 | (cp & 63));
}

void consoleKey (int code, unsigned ch)
{
	if (code == KEY_ESC) consoleClose ();
	else if (code == KEY_ENTER || code == 0x58) consoleRun ();	// (Entrée, Entrée du pavé)
	else if (code == KEY_BACKSPACE) {
		while (!g_cin.empty () && ((unsigned char) g_cin.back () & 0xC0) == 0x80) g_cin.pop_back ();
		if (!g_cin.empty ()) g_cin.pop_back ();
	} else if (code == KEY_UP && g_histPos > 0) g_cin = g_hist[(size_t) --g_histPos];
	else if (code == KEY_DOWN) {
		if (g_histPos + 1 < (int) g_hist.size ()) g_cin = g_hist[(size_t) ++g_histPos];
		else g_histPos = (int) g_hist.size (), g_cin.clear ();
	} else if (ch >= 32 && ch != 127 && g_cin.size () < 400) utf8Put (g_cin, ch);
}

void newWorldDialog ()
{
	unlock ();
	long d = ui_dialog_begin (420, 250, (long) TR ("Nouveau monde"));
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
	ui_label (16, 132, 120, 24, (long) TR ("Mode"), 0);
	long fMode = ui_segmented (130, 128, 274, 30, (long) TR ("Créatif|Survie"));
	ui_w_set_value (fMode, g.mode == MODE_SURVIVAL ? 1 : 0);
	long e = ui_dialog_button (d, 0, (long) TR ("Créer"), 1, UB_DEFAULT, 420 - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, e);
	ui_dialog_cancel (d, 0);
	ui_w_focus (fName);
	long res = ui_dialog_run (d, 0, 0);
	std::string name = (const char *) ui_w_get_text (fName), seed = (const char *) ui_w_get_text (fSeed);
	int mode = ui_w_get_value (fMode) == 1 ? MODE_SURVIVAL : MODE_CREATIVE;
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
	gameSetMode (mode);
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
	"MPartie\nI1\tNouveau monde…\tCtrl+N\nI2\tOuvrir un monde…\tCtrl+O\nI3\tSauver\tCtrl+S\n-\nI5\tMode survie\n"
	"I6\tInventaire et fabrication\tI\nI7\tConsole de commandes\tT\n-\nI4\tPlein écran\tF9\n"
	"MAffichage\nI11\tDistance courte\nI12\tDistance moyenne\nI13\tDistance longue\n-\nI14\tVoler\tF\nI15\tLe temps passe\nI16\tInfos détaillées\tF3\n"
	"MSon\nI30\tMusique\nI31\tEffets sonores\n-\nI32\tMusique plus fort\nI33\tMusique moins fort\n"
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
	case 5: gameSetMode (survival () ? MODE_CREATIVE : MODE_SURVIVAL); break;
	case 6: inventoryWindow (); break;
	case 7: consoleOpen (""); break;
	case 11: setDistance (4); break;
	case 12: setDistance (8); break;
	case 13: setDistance (12); break;
	case 14:
		if (survival ()) gamePost (TR ("Le vol est réservé au mode créatif (console : mode creative)"));
		else g.pl.fly = !g.pl.fly;
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
	case 30: g.musicOn = !g.musicOn, gameAudioChanged (); break;
	case 31: g.sfxOn = !g.sfxOn, gameAudioChanged (); break;
	case 32: g.musicVol = std::min (1.0f, g.musicVol + 0.1f), g.musicOn = true, gameAudioChanged (); break;
	case 33: g.musicVol = std::max (0.0f, g.musicVol - 0.1f), gameAudioChanged (); break;
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

void keyDown (int code, int mods, long id, unsigned ch)
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
	if (ch == '/') consoleOpen ("/");				// / : la console, la commande commencée
	else if (code == 0x17 || code == 0x35) consoleOpen ("");	// T, ² : la console
	else if (code >= 0x1E && code <= 0x26) selectSlot (code - 0x1E);	// 1 à 9
	else if (code == 0x09) command (14);			// F : voler
	else if (code == 0x05) survival () ? inventoryWindow () : chooseBlock ();	// B : les blocs
	else if (code == 0x0C || code == 0x06) inventoryWindow ();	// I, C : inventaire et fabrication
	else if (code == 0x08 && g_target.hit) event (3, g_target, g_target.block);	// E : toucher (pour les scripts)
	else if (code == 0x14) pickBlock ();			// Q (A en AZERTY) : prendre ce bloc
}

// les boutons de la souris pendant que le jeu la tient : un bouton qui vient d'être enfoncé agit une fois, ceux qui
// restent enfoncés se répètent (frame) ; g_buttons suit l'état reçu, y compris quand ils sont relâchés
void mouseButtons (unsigned b)
{
	unsigned pressed = b & ~g_buttons;
	g_buttons = b;
	double t0 = nowMs ();
	if (pressed & 1) breakBlock (), g_breakAt = t0 + 300, swingArm ();
	if (pressed & 2) placeBlock (), g_placeAt = t0 + 300;
	if (pressed & 4) pickBlock ();
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
		unsigned ch = (unsigned) ui_ev_field (&ui, EV_CHAR);
		if (g_console) {
			if (kind) consoleKey (code, ch);
			return;
		}
		if (C_.open) {
			if (kind) {
				if (mods & UI_MOD_CTRL) {
					long mid = ui_menu_key (0);
					if (mid >= 0) command (mid);
				} else craftKey (code);
			}
			return;
		}
		long id = kind && (mods & UI_MOD_CTRL) ? ui_menu_key (0) : -1;
		g_keys[code] = kind != 0;
		if (kind) keyDown (code, mods, id, ch);
		return;
	}
	if (t == WEV_WHEEL && C_.open) {
		if (C_.book) C_.bookScroll -= (int) ui_ev_field (&ui, EV_Y) * 3;
		return;
	}
	if (t == WEV_WHEEL) {
		int n = (int) ui_ev_field (&ui, EV_Y);
		if (n) selectSlot (g.selected - (n > 0 ? 1 : -1));
		return;
	}
	if (t == WEV_MOUSE) {
		int kind = (int) ui_ev_field (&ui, EV_KIND);
		if (kind == 3) {	// WEV_M_REL : la souris prise ; le déplacement et l'état des boutons (pas d'appui ni de relâché à part)
			g_mdx += (float) (int) (uint32_t) ui_ev_field (&ui, EV_X);
			g_mdy += (float) (int) (uint32_t) ui_ev_field (&ui, EV_Y);
			if (g_locked && !C_.open) mouseButtons ((unsigned) ui_ev_field (&ui, EV_CODE) & 7);
			return;
		}
		unsigned b = (unsigned) ui_ev_field (&ui, EV_CODE) & 7;
		if (C_.open) {	// (le panneau de fabrication a la souris)
			long mods = (long) ui_ev_field (&ui, EV_MODS);
			craftMouse (0, kind, (long) (int) ui_ev_field (&ui, EV_X), (long) (int) ui_ev_field (&ui, EV_Y), (long) b << 8 | mods << 16);
			return;
		}
		if (kind == 1 && !g_locked) {
			// (les 32 px du haut déplacent la fenêtre ; ailleurs, un clic prend la souris)
			if (g_console) consoleClose ();
			else if (ui_ev_field (&ui, EV_Y) >= 32 || g_full) lock ();
			g_buttons = b;
			return;
		}
		if (g_locked) mouseButtons (b);
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

// la météo : le niveau suit le temps voulu ; le cycle en choisit un autre de temps en temps ; les éclairs de l'orage
double randRange (double a, double b) { return a + (b - a) * (rand () % 10000) / 10000.0; }

void weatherStep (double dt)
{
	if (g.weatherNext > 0) {
		g.weatherNext -= dt;
		if (g.weatherNext <= 0) {
			if (g.weatherCycle) {
				int r = rand () % 100;
				g.weather = r < 55 ? W_CLEAR : r < 80 ? W_RAIN : r < 88 ? W_SNOW : W_STORM;
				g.weatherNext = g.weather == W_CLEAR ? randRange (300, 900) : randRange (120, 420);
			} else {
				g.weather = W_CLEAR;
				g.weatherNext = 0;
			}
		}
	} else if (g.weatherCycle)
		g.weatherNext = randRange (300, 900);
	float target = g.weather == W_CLEAR ? 0.0f : g.weather == W_STORM ? 1.0f : 0.8f;
	g.weatherLevel += (target - g.weatherLevel) * (float) std::min (1.0, dt * 0.2);
	if (g.weather == W_STORM && g.weatherLevel > 0.6f && rand () % 10000 < (int) (dt * 900)) g.lightning = nowMs ();
}

float daylight (float hour, float *sunAngle);

// ---- la vie du joueur (mode survie) ----------------------------------------------------------------------------

void respawn ()
{
	g.health = 20, g.hunger = 20, g.saturation = 5, g.exhaustion = 0, g.air = 15, g.burn = 0;
	if (g.spawnY >= 0) gameTeleport (g.spawnX, g.spawnY, g.spawnZ);
	else spawnNear (0, 0);
	g.pl.vx = g.pl.vy = g.pl.vz = 0;
	g_hurtFlash = 0;
}

void hurtPlayer (float dmg, const char *cause, double fx, double fz, bool periodic)
{
	if (!survival () || dmg <= 0 || g.health <= 0) return;
	double t = nowMs ();
	if (!periodic && t - g_lastHurt < 500) return;
	g_lastHurt = t;
	g.health = std::max (0.0f, g.health - dmg);
	g_hurtFlash = 1;
	g.exhaustion += 0.1f;
	audio::play (audio::S_PLAYER_HURT, 0, 0.9f);
	double dx = g.pl.x - fx, dz = g.pl.z - fz, d = sqrt (dx * dx + dz * dz);
	if (d > 0.05) g.pl.vx += dx / d * 7, g.pl.vz += dz / d * 7, g.pl.vy = std::max (g.pl.vy, 5.0);
	if (g.health <= 0) {
		gamePost (std::string (TR ("Tu es mort (")) + cause + TR (") : retour au point de départ, l'inventaire gardé"));
		respawn ();
	}
}

// chaque image : la chute, la lave, le feu, l'eau, la faim, la guérison
void vitals (double dt, bool wasGround, bool sprint, double dist)
{
	static double fallTop = 0, lavaT = 0, burnT = 0, drownT = 0, regenT = 0, starveT = 0;
	g_hurtFlash = std::max (0.0f, g_hurtFlash - (float) dt * 2.5f);
	if (!survival ()) {
		g.health = 20, g.hunger = std::max (g.hunger, 20.0f), g.air = 15, g.burn = 0;
		fallTop = g.pl.y;
		return;
	}
	// la chute : plus de 3 blocs, un demi-cœur par bloc de plus (l'eau l'arrête)
	if (g.pl.fly || g.pl.inWater) fallTop = g.pl.y;
	else if (g.pl.onGround) {
		if (!wasGround && fallTop - g.pl.y > 3.5) hurtPlayer ((float) floor (fallTop - g.pl.y - 3), TR ("une chute"), g.pl.x, g.pl.z, true);
		fallTop = g.pl.y;
	} else
		fallTop = std::max (fallTop, g.pl.y);
	// la lave, le feu qui reste ensuite
	if (g.pl.inLava) {
		g.burn = 5;
		if ((lavaT += dt) >= 0.5) lavaT = 0, hurtPlayer (2, TR ("la lave"), g.pl.x, g.pl.z, true);
	} else if (g.burn > 0) {
		bool wet = g.pl.inWater || (g.weather != W_CLEAR && g.weatherLevel > 0.5f && g.world.skyLit ((int) floor (g.pl.x), (int) floor (g.pl.y + 1), (int) floor (g.pl.z)));
		g.burn = wet ? 0 : g.burn - (float) dt;
		if ((burnT += dt) >= 1) burnT = 0, hurtPlayer (1, TR ("le feu"), g.pl.x, g.pl.z, true);
	}
	// l'air sous l'eau : 15 s, puis un cœur par seconde
	int eb = g.world.get ((int) floor (g.pl.x), (int) floor (g.pl.y + Player::EYE), (int) floor (g.pl.z));
	if (eb == WATER) {
		g.air = std::max (0.0f, g.air - (float) dt);
		if (g.air <= 0 && (drownT += dt) >= 1) drownT = 0, hurtPlayer (2, TR ("noyé"), g.pl.x, g.pl.z, true);
	} else
		g.air = std::min (15.0f, g.air + (float) dt * 6);
	// la faim : la fatigue use la satiété, puis la faim
	g.exhaustion += (float) dt * 0.015f + (sprint ? (float) dist * 0.1f : 0.0f);
	while (g.exhaustion >= 4) {
		g.exhaustion -= 4;
		if (g.saturation > 0) g.saturation = std::max (0.0f, g.saturation - 1);
		else g.hunger = std::max (0.0f, g.hunger - 1);
	}
	// guérir (bien nourri), dépérir (affamé : jusqu'à un demi-cœur)
	if (g.hunger >= 18 && g.health < 20) {
		if ((regenT += dt) >= 3) regenT = 0, g.health = std::min (20.0f, g.health + 1), g.exhaustion += 6;
	} else
		regenT = 0;
	if (g.hunger <= 0 && g.health > 1) {
		if ((starveT += dt) >= 4) starveT = 0, hurtPlayer (1, TR ("la faim"), g.pl.x, g.pl.z, true);
	} else
		starveT = 0;
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
	if (survival ()) g.pl.fly = false;
	bool wasGround = g.pl.onGround, wasWater = g.pl.inWater;
	double vy0 = g.pl.vy, px0 = g.pl.x, pz0 = g.pl.z;
	playerStep (g.world, g.pl, in, dt);
	{	// les sons du corps : pas, saut, chute, l'eau
		int under = g.world.get ((int) floor (g.pl.x), (int) floor (g.pl.y - 0.2), (int) floor (g.pl.z));
		if (g.pl.onGround && !g.pl.fly && !g.pl.inWater) {
			g_stepDist += (float) sqrt ((g.pl.x - px0) * (g.pl.x - px0) + (g.pl.z - pz0) * (g.pl.z - pz0));
			if (g_stepDist > 1.7f) g_stepDist = 0, audio::play (audio::S_STEP, audio::materialOf (under), 1.0f, ((rand () & 1) ? 0.15f : -0.15f));
		}
		if (wasGround && !g.pl.onGround && g.pl.vy > 5 && !g.pl.fly) audio::play (audio::S_JUMP, audio::materialOf (under));
		if (!wasGround && g.pl.onGround && vy0 < -7) audio::play (audio::S_LAND, audio::materialOf (under), (float) std::min (1.0, -vy0 / 20.0));
		if (!wasWater && g.pl.inWater && vy0 < -3) audio::play (audio::S_SPLASH, 0, (float) std::min (1.0, -vy0 / 12.0));
	}
	g_activity *= (float) exp (-dt / 25.0);
	// la simulation : les liquides, les circuits (20 pas par seconde)
	sim::step (dt, daylight (g.hour, nullptr) * (1.0f - 0.5f * g.weatherLevel), g.pl.x, g.pl.y, g.pl.z);
	// la vie du joueur, les créatures
	{
		double moved = sqrt ((g.pl.x - px0) * (g.pl.x - px0) + (g.pl.z - pz0) * (g.pl.z - pz0));
		if (wasGround && !g.pl.onGround && g.pl.vy > 5 && !g.pl.fly) g.exhaustion += in.sprint ? 0.2f : 0.05f;
		vitals (dt, wasGround, in.sprint && g.pl.onGround, moved);
	}
	g.mobs.update (g.world, dt);
	// la main : le geste, l'objet qui remonte après un changement, la marche ; les éclats ; la fumée des créatures mortes
	if (g_swing >= 0 && (g_swing += (float) dt / 0.28f) >= 1) g_swing = -1;
	{
		int held = slotItem (g.selected);
		if (held != g_heldShown) g_equip = g_heldShown < 0 ? 0 : 1, g_heldShown = held;
		g_equip = std::max (0.0f, g_equip - (float) dt * 4);
		double moved = sqrt ((g.pl.x - px0) * (g.pl.x - px0) + (g.pl.z - pz0) * (g.pl.z - pz0));
		bool walking = g.pl.onGround && moved > dt * 0.5;
		g_bob += (float) moved * 2.2f;
		g_bobAmt += ((walking ? 1.0f : 0.0f) - g_bobAmt) * (float) std::min (1.0, dt * 6);
	}
	for (auto &e : g.mobs.list)
		if (e.dead >= 0.5f && e.dead - dt < 0.5f && e.dead < 5)
			for (int i = 0; i < 8; i++) addParticle (e.x + frnd () * 0.4, e.y + 0.3 + frnd () * 0.3, e.z + frnd () * 0.4, frnd () * 0.4f, 0.8f + frnd () * 0.3f, frnd () * 0.4f, T_SMOKE, 0.12f, true);
	for (size_t i = 0; i < g_parts.size ();) {
		Particle &p = g_parts[i];
		p.life -= (float) dt;
		if (p.life <= 0) {
			g_parts[i] = g_parts.back ();
			g_parts.pop_back ();
			continue;
		}
		if (p.rise) p.vy = std::min (p.vy + (float) dt * 0.5f, 1.2f), p.size += (float) dt * 0.06f;
		else p.vy -= (float) dt * 16.0f;
		double nx = p.x + p.vx * dt, ny = p.y + p.vy * dt, nz = p.z + p.vz * dt;
		if (isSolid (g.world.get ((int) floor (nx), (int) floor (ny), (int) floor (nz)))) {	// (posé : il glisse un peu et s'arrête)
			if (p.vy < 0 && !isSolid (g.world.get ((int) floor (nx), (int) floor (p.y), (int) floor (nz)))) p.vy = 0, ny = p.y;
			p.vx *= 0.3f, p.vz *= 0.3f;
			if (isSolid (g.world.get ((int) floor (nx), (int) floor (ny), (int) floor (nz)))) nx = p.x, nz = p.z, ny = p.y;
		}
		p.x = nx, p.y = ny, p.z = nz;
		i++;
	}
	for (auto &e : g.mobs.list)	// (une créature sur une plaque)
		if (e.dead < 0 && e.onGround) sim::press ((int) floor (e.x), (int) floor (e.y + 0.01), (int) floor (e.z));
	if (g.timeRuns) g.hour = fmodf (g.hour + (float) dt * 24.0f / 1200.0f, 24.0f);	// une journée : 20 minutes
	weatherStep (dt);
	// le bloc visé ; les clics tenus se répètent
	float eye[3], dir[3];
	eyeOf (eye, dir);
	g_target = g.world.raycast (eye, dir, survival () ? 5.0f : 6.0f);
	{	// une créature plus proche que le bloc visé
		float ed;
		int id = g.mobs.pick (eye, dir, survival () ? 3.5f : 5.0f, ed);
		g_targetEnt = id && (!g_target.hit || ed < g_target.dist) ? id : 0;
	}
	double t = nowMs ();
	if (g_locked && (g_buttons & 1) && g_targetEnt) attackEntity ();
	if (survival ()) {
		// casser peu à peu : le temps dépend du bloc et de l'outil en main
		bool on = g_locked && (g_buttons & 1) && g_target.hit && !g_targetEnt;
		if (on && (g_target.x != g_mineX || g_target.y != g_mineY || g_target.z != g_mineZ)) {
			g_mineX = g_target.x, g_mineY = g_target.y, g_mineZ = g_target.z;
			g_mineProg = 0;
		}
		float need = on ? mineTime (g_target.block, slotItem (g.selected), nullptr) : -1;
		if (on && need >= 0) {
			if (t > g_hitAt) {	// un coup : le son, le geste, des éclats
				g_hitAt = t + 250;
				audio::play (audio::S_HIT, audio::materialOf (g_target.block), 1.0f, panOf (g_target.x, g_target.z));
				g_swing = 0;
				blockParticles (g_target, g_target.block, false);
			}
			g_mineProg += (float) (dt / std::max (0.05f, need));
			if (g_mineProg >= 1) {
				doBreak ();
				g_mineProg = -1;
				g_mineY = -999;
			}
		} else {
			g_mineProg = -1;
			g_mineY = -999;
		}
	} else if (g_locked && (g_buttons & 1) && t > g_breakAt && !g_targetEnt) breakBlock (), g_breakAt = t + 250;
	// le tonnerre, un peu après l'éclair (la distance de l'orage)
	if (g.lightning != g_lastLightning) g_lastLightning = g.lightning, g_thunderAt = t + 300 + rand () % 1800;
	if (g_thunderAt > 0 && t >= g_thunderAt) g_thunderAt = 0, audio::play (audio::S_THUNDER, 0, 1.0f, (rand () % 200 - 100) / 150.0f);
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

// la situation, pour la musique et l'ambiance (audio.h)
void soundMood ()
{
	audio::Mood m;
	m.daylight = daylight (g.hour, nullptr);
	m.rain = g.weather != W_CLEAR || g.weatherLevel > 0.01f ? g.weatherLevel : 0;
	m.storm = g.weather == W_STORM ? g.weatherLevel : 0;
	int x = (int) floor (g.pl.x), z = (int) floor (g.pl.z), ey = (int) floor (g.pl.y + Player::EYE);
	int top = g.world.get (x, g.world.height (x, z), z);
	m.snowing = g.weather == W_SNOW || top == SNOW || top == ICE || top == PINE_LEAVES || ey > SEA + 52;
	m.biome = top == SAND || top == CACTUS ? audio::BI_DESERT : m.snowing && g.weather != W_SNOW ? audio::BI_SNOW : top == SNOW || top == ICE ? audio::BI_SNOW : audio::BI_TEMPERATE;
	// sous terre : un bloc opaque (pas des feuilles) au-dessus, et assez de terre entre le ciel et l'œil
	int rock = -1;
	for (int y = std::min (CH - 1, ey + 40); y > ey; y--) {
		int b = g.world.get (x, y, z);
		if (isOpaque (b)) {
			rock = y;
			break;
		}
	}
	m.cave = rock < 0 ? 0 : std::max (0.0f, std::min (1.0f, (g.world.height (x, z) - ey - 3) / 10.0f));
	m.underwater = g.world.get (x, ey, z) == WATER;
	int lava = 0;	// (la lave tout près : elle bouillonne)
	for (int dy = -3; dy <= 3; dy++)
		for (int dz = -4; dz <= 4; dz++)
			for (int dx = -4; dx <= 4; dx++) lava += g.world.get (x + dx, ey + dy, z + dz) == LAVA;
	m.lava = std::min (1.0f, lava / 12.0f);
	m.altitude = std::max (0.0f, std::min (1.0f, (ey - 75) / 40.0f));
	m.activity = g_activity;
	m.seed = g.world.seed;
	for (auto &e : g.mobs.list)	// (une créature de la nuit tout près, en survie : la musique se tend)
		if (entityInfo (e.type).hostile && e.dead < 0 && survival () && (e.x - g.pl.x) * (e.x - g.pl.x) + (e.z - g.pl.z) * (e.z - g.pl.z) < 14 * 14) m.danger = 1;
	audio::setMood (m);
}

void renderTo (int w, int h, const Camera &cam, bool hotbar = true)
{
	Frame f;
	f.w = w, f.h = h;
	f.daylight = daylight (g.hour, &f.sunAngle);
	f.distance = g.distance;
	f.hasTarget = g_target.hit && !g_selftest;
	f.target = g_target;
	// la météo : pluie, neige là où il fait froid (neige ou glace au sol, ou en altitude)
	f.precipLevel = g.weatherLevel;
	f.gloom = g.weatherLevel * (g.weather == W_STORM ? 1.0f : 0.7f);
	f.daylight *= 1.0f - 0.45f * f.gloom;
	double fl = nowMs () - g.lightning;
	if (fl >= 0 && fl < 350) f.flash = (float) (1.0 - fl / 350.0) * ((int) (fl / 60) % 2 ? 0.5f : 1.0f);
	static uint8_t hm[HMAP * HMAP];
	if (g.weather != W_CLEAR || g.weatherLevel > 0.01f) {
		int bx = (int) floor (cam.x), bz = (int) floor (cam.z);
		int top = g.world.get (bx, g.world.height (bx, bz), bz);
		bool cold = g.weather == W_SNOW || top == SNOW || top == ICE || top == PINE_LEAVES || cam.y > SEA + 52;
		f.precip = cold ? 2 : 1;
		for (int z = 0; z < HMAP; z++)
			for (int x = 0; x < HMAP; x++) {
				int wx = bx + x - HMAP / 2, wz = bz + z - HMAP / 2;
				const Chunk *c = g.world.find (floorDiv (wx, CW), floorDiv (wz, CW));
				int tp = c ? c->top[(wz - c->cz * CW) * CW + (wx - c->cx * CW)] + 1 : 0;
				hm[z * HMAP + x] = (uint8_t) std::max (0, std::min (255, tp));
			}
		f.heights = hm;
	}
	HudState hud;
	for (int i = 0; i < 9; i++) {
		hud.slots[i] = slotItem (i);
		const Stack &st = g.inv.s[i];
		hud.counts[i] = survival () && !st.empty () ? st.count : -1;
		int dur = survival () && !st.empty () ? itemInfo (st.item).durability : 0;
		hud.wear[i] = dur ? (float) st.dur / dur : -1;
	}
	hud.mining = g_mineProg;
	if (survival ()) {
		hud.health = g.health, hud.hunger = g.hunger;
		int ebw = g.world.get ((int) floor (cam.x), (int) floor (cam.y), (int) floor (cam.z));
		hud.air = ebw == WATER || g.air < 15 ? g.air / 1.5f : -1;
	}
	hud.hurtFlash = g_hurtFlash;
	static std::vector<EVertex> ev;
	static std::vector<EDraw> ed;
	buildEntityMeshes (g.mobs, g.world, floor (cam.x), floor (cam.z), ev, ed);
	{	// les éclats : des petits cubes, un morceau de leur tuile
		EDraw pd;
		pd.first = (int) ev.size ();
		pd.tint[0] = pd.tint[1] = pd.tint[2] = 1;
		double bx = floor (cam.x), bz = floor (cam.z);
		static const float SH[6] = { 0.8f, 0.8f, 1.0f, 0.55f, 0.7f, 0.7f };
		for (auto &p : g_parts) {
			float sky = g.world.skyLit ((int) floor (p.x), (int) floor (p.y), (int) floor (p.z)) ? 1.0f : 0.0f, l = 0.25f + 0.75f * sky;
			float h = p.size * 8;	// (demi-côté, en 1/16)
			float cx = (float) ((p.x - bx) * 16), cy = (float) (p.y * 16), cz = (float) ((p.z - bz) * 16);
			float tu = (float) ((p.tile & 15) * 256) + p.u * 16, tv = (float) ((p.tile >> 4) * 256) + p.v * 16;
			float P[8][3] = { { cx - h, cy - h, cz - h }, { cx + h, cy - h, cz - h }, { cx + h, cy + h, cz - h }, { cx - h, cy + h, cz - h },
					  { cx - h, cy - h, cz + h }, { cx + h, cy - h, cz + h }, { cx + h, cy + h, cz + h }, { cx - h, cy + h, cz + h } };
			static const int F[6][4] = { { 1, 5, 6, 2 }, { 4, 0, 3, 7 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 }, { 5, 4, 7, 6 }, { 0, 1, 2, 3 } };
			static const float UV[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
			for (int fc = 0; fc < 6; fc++)
				for (int k = 0; k < 4; k++) {
					const float *q = P[F[fc][k]];
					EVertex v { q[0], q[1], q[2], l * SH[fc], 0, tu + UV[k][0] * 48, tv + UV[k][1] * 48 };
					ev.push_back (v);
				}
		}
		pd.quads = ((int) ev.size () - pd.first) / 4;
		if (pd.quads) ed.push_back (pd);
	}
	f.ents = &ev, f.entDraws = &ed;
	// les fissures du bloc qu'on casse (survie)
	static std::vector<EVertex> ov;
	ov.clear ();
	if (g_mineProg >= 0 && g_target.hit && hotbar) {
		int tile = T_CRACK0 + std::min (4, (int) (g_mineProg * 5));
		double bx = floor (cam.x), bz = floor (cam.z);
		float x0 = (float) ((g_target.x - bx) * 16) - 0.05f, y0 = g_target.y * 16.0f - 0.05f, z0 = (float) ((g_target.z - bz) * 16) - 0.05f;
		float x1 = x0 + 16.1f, y1 = y0 + 16.1f, z1 = z0 + 16.1f;
		float P[8][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 }, { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
		static const int F[6][4] = { { 1, 5, 6, 2 }, { 4, 0, 3, 7 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 }, { 5, 4, 7, 6 }, { 0, 1, 2, 3 } };
		static const float UV[4][2] = { { 0, 16 }, { 16, 16 }, { 16, 0 }, { 0, 0 } };
		float tu = (float) ((tile & 15) * 256), tv = (float) ((tile >> 4) * 256);
		for (int fc = 0; fc < 6; fc++)
			for (int k = 0; k < 4; k++) {
				const float *q = P[F[fc][k]];
				EVertex v { q[0], q[1], q[2], 1.0f, 0, tu + UV[k][0] * 16, tv + UV[k][1] * 16 };
				ov.push_back (v);
			}
	}
	f.overlay = &ov;
	// ce que le joueur tient (pas dans les captures d'un autre point de vue)
	f.hand.show = hotbar && !C_.open;
	f.hand.item = slotItem (g.selected);
	f.hand.swing = g_swing, f.hand.equip = g_equip, f.hand.bob = g_bob, f.hand.bobAmt = g_bobAmt;
	f.hand.light = g.world.skyLit ((int) floor (cam.x), (int) floor (cam.y), (int) floor (cam.z)) ? 1.0f : 0.3f;
	hud.selected = g.selected;
	int eb = g.world.get ((int) floor (cam.x), (int) floor (cam.y), (int) floor (cam.z));
	hud.underwater = eb == WATER;
	hud.inLava = eb == LAVA;
	hud.crosshair = g_locked;
	hud.hotbar = hotbar;
	hud.panel = jet (TJ_SURFACE);
	hud.panelLine = jet (TJ_SUNKEN);
	hud.accent = jet (TJ_ACCENT);
	hud.text = jet (TJ_TEXT1);
	hud.sunken = jet (TJ_SUNKEN);
	hud.line = jet (TJ_LINE);
	hud.hoverCol = jet (TJ_HOVER);
	static CraftDraw cd;
	if (C_.open && hotbar) {
		cd = CraftDraw ();
		craftDraw (cd);
		hud.craft = &cd;
		hud.crosshair = false;
	}
	hud.good = jet (TJ_SUCCESS);
	hud.warn = jet (TJ_WARNING);
	hud.bad = jet (TJ_DANGER);
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

void gameHurtPlayer (float dmg, const char *cause, double fromX, double fromZ) { hurtPlayer (dmg, cause, fromX, fromZ); }

void gameAudioChanged ()
{
	audio::setMusic (g.musicOn, g.musicVol);
	audio::setSfx (g.sfxOn, g.sfxVol);
	ui_menu_state (30, g.musicOn ? UMS_CHECKED : 0);
	ui_menu_state (31, g.sfxOn ? UMS_CHECKED : 0);
	saveConf ();
}

void gameSetMode (int mode)
{
	if (mode != MODE_SURVIVAL) mode = MODE_CREATIVE;
	if (mode == g.mode) return;
	g.mode = mode;
	if (mode == MODE_SURVIVAL) g.pl.fly = false;
	g_mineProg = -1;
	gameFlyChanged ();
	ui_menu_state (5, mode == MODE_SURVIVAL ? UMS_CHECKED : 0);
	gamePost (mode == MODE_SURVIVAL ? TR ("Mode survie : les blocs se ramassent ; I pour l'inventaire et la fabrication")
					: TR ("Mode créatif : tous les blocs, B pour la liste ; F pour voler"));
	selectSlot (g.selected);
	g.edited = true;
}

void gameSetWeather (int w, double seconds)
{
	g.weather = w & 3;
	g.weatherNext = seconds > 0 ? seconds : g.weatherCycle ? randRange (300, 900) : 0;
	g.edited = true;
}

int gameHeldItem () { return slotItem (g.selected); }

bool gameStations (bool &workbench, bool &furnace)
{
	workbench = furnace = false;
	int px = (int) floor (g.pl.x), py = (int) floor (g.pl.y + 1), pz = (int) floor (g.pl.z);
	for (int y = py - 4; y <= py + 4; y++)
		for (int z = pz - 4; z <= pz + 4; z++)
			for (int x = px - 4; x <= px + 4; x++) {
				int b = g.world.get (x, y, z);
				workbench |= b == WORKBENCH;
				furnace |= b == FURNACE;
			}
	return workbench || furnace;
}

// l'image rendue : relue, les textes dessinés dessus, puis recopiée d'un coup dans la fenêtre
void present ()
{
	int w = g_view.w, h = g_view.h;
	if (w < 1 || h < 1 || ui.w < 1 || ui.h < 1) return;
	g_read.resize ((size_t) w * h * 4);
	glPixelStorei (GL_PACK_ALIGNMENT, 4);
	glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_read.data ());
	g_frame.resize ((size_t) (ui.w * ui.h));
	int cw = std::min (w, (int) ui.w), chh = std::min (h, (int) ui.h);
	for (int y = 0; y < chh; y++) {
		const uint8_t *s = &g_read[(size_t) (h - 1 - y) * w * 4];	// (GL : la ligne du bas d'abord)
		uint32_t *d = &g_frame[(size_t) y * ui.w];
		for (int x = 0; x < cw; x++, s += 4) d[x] = (uint32_t) s[0] << 16 | (uint32_t) s[1] << 8 | s[2];
	}
	uint32_t *front = ui.surf;
	ui.surf = g_frame.data ();	// (UIKit dessine dans ui.surf : les textes vont dans l'image, comme ui_canvas_begin)
	drawTexts ();
	ui.surf = front;
	memcpy (front, g_frame.data (), g_frame.size () * 4);
	ui_update_all ();
}

bool gameCapture (std::vector<uint32_t> &px, int &w, int &h, const double *eye, const float *yp)
{
	w = g_view.w, h = g_view.h;
	if (w < 16 || h < 16 || !g_view.surf) return false;	// (une image à part : la vue du joueur ne change pas)
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
	// la console, le mode survie, la fabrication, le minage, la météo
	auto con = [] (const char *c) { std::string o; for (auto &l : consoleExec (c)) o += l + "\n"; return o; };
	con ("time 18:30");
	check (fabsf (g.hour - 18.5f) < 0.01f, "console : time 18:30");
	con ("/weather storm 60");
	check (g.weather == W_STORM, "console : weather storm");
	con ("mode survival");
	check (g.mode == MODE_SURVIVAL && !g.pl.fly, "console : mode survival");
	con ("clear");
	con ("give log 3");
	check (g.inv.count (LOG) == 3, "console : give log 3");
	con ("craft planks 3");
	check (g.inv.count (PLANKS) == 12 && g.inv.count (LOG) == 0, "fabrication : 3 troncs -> 12 planches");
	con ("craft stick");
	check (g.inv.count (I_STICK) == 4 && g.inv.count (PLANKS) == 10, "fabrication : 2 planches -> 4 bâtons");
	std::string refused = con ("craft wood_pickaxe");
	check (g.inv.count (I_WOOD_PICKAXE) == 0 && refused.find ("établi") != std::string::npos, "fabrication : la pioche demande un établi");
	bool harv;
	float bare = mineTime (STONE, 0, &harv);
	check (!harv && bare > mineTime (STONE, I_WOOD_PICKAXE, nullptr), "minage : la pierre à la main est lente et ne tombe pas");
	mineTime (IRON_ORE, I_WOOD_PICKAXE, &harv);
	bool h2;
	mineTime (IRON_ORE, I_STONE_PICKAXE, &h2);
	check (!harv && h2, "minage : le fer demande une pioche en pierre");
	check (apiExec ("game.getMode()", r) == "survival", "protocole : game.getMode");
	check (apiExec ("player.getInventory()", r).find ("planks:10") != std::string::npos, "protocole : player.getInventory");
	std::string cmd = mcpHandle (R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"weather clear"}}})");
	check (g.weather == W_CLEAR && cmd.find ("clear") != std::string::npos, "MCP : run_command weather clear");
	std::string rec = mcpHandle (R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"list_recipes","arguments":{}}})");
	check (rec.find ("wood_pickaxe") != std::string::npos, "MCP : list_recipes");
	// la table de fabrication, à la souris (clics simulés sur les cases de la vue)
	{
		con ("clear");
		con ("give planks 8");
		con ("give stick 4");
		C_.n = 3, C_.furnace = false;
		craftPlace ();
		for (auto &st : C_.grid) st = Stack ();
		C_.cursor = C_.fin = Stack ();
		C_.buttons = 0;
		auto at = [] (int id, int &x, int &y) {
			for (auto &sl : craftSlots ())
				if (sl.id == id) x = sl.x + CELL / 2, y = sl.y + CELL / 2;
		};
		auto click = [&] (int id, int button) {
			int x = 0, y = 0;
			at (id, x, y);
			craftMouse (0, 1, x, y, (long) button << 8);
			craftMouse (0, 2, x, y, 0);
		};
		click (47, 1);					// les planches (barre 1)
		click (0, 2), click (1, 2), click (2, 2);	// une sur chaque case du haut
		click (47, 1);					// le reste revient
		click (48, 1);					// les bâtons (barre 2)
		click (4, 2), click (7, 2);			// au milieu, en dessous
		click (48, 1);
		const Recipe *rr = gridRecipe ();
		check (rr && !strcmp (rr->name, "wood_pickaxe"), "table : la forme d'une pioche donne une pioche");
		// glisser-déposer : une pile de la barre lâchée sur une case vide de la grille, puis ramenée
		auto drag = [&] (int from, int to, int button) {
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			at (from, x0, y0), at (to, x1, y1);
			craftMouse (0, 1, x0, y0, (long) button << 8);
			craftMouse (0, 0, x1, y1, (long) button << 8);
			craftMouse (0, 2, x1, y1, 0);
		};
		drag (47, 3, 1);
		check (C_.grid[3].item == PLANKS && C_.grid[3].count == 5 && g.inv.s[0].empty () && C_.cursor.empty (), "table : glisser une pile de l'inventaire vers la grille");
		drag (3, 47, 1);
		drag (48, 6, 2);
		check (C_.grid[6].count == 1 && g.inv.s[1].count == 1 && C_.cursor.empty (), "table : glisser un seul objet (bouton droit)");
		drag (6, 48, 1);
		check (g.inv.s[0].count == 5 && g.inv.s[1].count == 2 && gridRecipe () == rr, "table : tout revient à sa place");
		{	// (le résultat glissé directement sur une case de la barre, puis remis dans la grille pour la suite)
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			at (9, x0, y0), at (50, x1, y1);
			craftMouse (0, 1, x0, y0, 1 << 8);
			craftMouse (0, 0, x1, y1, 1 << 8);
			craftMouse (0, 2, x1, y1, 0);
			check (g.inv.s[3].item == I_WOOD_PICKAXE && C_.cursor.empty () && !gridRecipe (), "table : glisser le résultat dans l'inventaire");
			g.inv.s[3] = Stack ();
			for (int i : { 0, 1, 2 }) C_.grid[i].item = PLANKS, C_.grid[i].count = 1;
			for (int i : { 4, 7 }) C_.grid[i].item = I_STICK, C_.grid[i].count = 1;
		}
		click (9, 1);					// le résultat
		check (C_.cursor.item == I_WOOD_PICKAXE && C_.grid[0].empty () && C_.grid[4].empty (), "table : la pioche prise, la grille consommée");
		click (49, 1);					// posée en barre 3
		check (g.inv.s[2].item == I_WOOD_PICKAXE && g.inv.count (PLANKS) == 5 && g.inv.count (I_STICK) == 2, "table : la pioche dans l'inventaire, le reste aussi");
		C_.n = 2;
		craftPlace ();
		click (47, 1), click (0, 2), click (2, 2), click (47, 1);	// deux planches en colonne -> bâtons
		rr = gridRecipe ();
		check (rr && !strcmp (rr->name, "stick"), "table 2 × 2 : deux planches l'une sur l'autre donnent des bâtons");
		for (auto &st : C_.grid) st = Stack ();
		// le panneau ouvert dans le jeu : le livre des recettes (R), ses textes, la fermeture
		C_.n = 3;
		craftPlace ();
		C_.open = true;
		craftKey (0x15);
			drawTexts ();
		check (C_.book && !g_zones.empty (), "table : R ouvre le livre des recettes");
		craftKey (0x15);
		g.inv.s[8].item = PLANKS, g.inv.s[8].count = 1;
		C_.grid[4] = g.inv.s[8], g.inv.s[8] = Stack ();
		int before = g.inv.count (PLANKS);
		craftClose ();
		check (!C_.open && !C_.book && g.inv.count (PLANKS) == before + 1, "table : fermée, la grille revient dans l'inventaire");
	}
	con ("mode creative");
	check (g.mode == MODE_CREATIVE, "console : mode creative");
	con ("music 35");
	con ("sound off");
	check (g.musicOn && fabsf (g.musicVol - 0.35f) < 0.01f && !g.sfxOn, "console : music 35, sound off");
	con ("sound on");
	printf ("terracube : son %s\n", audio::running () ? "en marche (AudioKit)" : "absent");
	con ("time 11");
	// sauvegarde et relecture d'un tronçon
	std::string err;
	check (gameSave (err), "sauvegarde du monde " + g.world.dir + (err.empty () ? "" : " : " + err));
	Chunk tmp;
	tmp.cx = 0, tmp.cz = 0;
	check (g.world.loadChunk (&tmp) && tmp.at (2, 102, 2) == GOLD_BLOCK, "relecture d'un tronçon sauvé");
	// les circuits et les liquides (sim.cpp), en l'air au-dessus d'une dalle
	{
		auto put = [] (int x, int y, int z, int b, int m = 0) { g.world.set (x, y, z, b, m); };
		auto lit = [] (int x, int y, int z) { return (g.world.meta (x, y, z) & M_ON) != 0; };
		for (int y = 115; y <= 122; y++)
			for (int z = -30; z <= -10; z++)
				for (int x = 10; x <= 30; x++) put (x, y, z, y == 115 ? STONE : AIR);
		sim::runTicks (2);
		put (10, 116, -30, LEVER, sim::D_DOWN);
		for (int x = 11; x <= 15; x++) put (x, 116, -30, WIRE);
		put (16, 116, -30, SIGNAL_LAMP);
		sim::runTicks (2);
		sim::use (10, 116, -30, 0);
		sim::runTicks (1);
		check ((g.world.meta (15, 116, -30) & 15) == 11 && lit (16, 116, -30), "circuits : levier, fil (15 -> 11), lampe allumée");
		check (atoi (apiExec ("world.getPower(13,116,-30)", r).c_str ()) == 13, "protocole : world.getPower");
		sim::use (10, 116, -30, 0);
		sim::runTicks (6);
		check (!lit (16, 116, -30), "circuits : levier coupé, lampe éteinte");
		put (10, 116, -28, STONE);
		put (10, 117, -28, LEVER, sim::D_DOWN);
		put (11, 116, -28, COPPER_TORCH, sim::D_WEST);
		put (12, 116, -28, REPEATER, sim::D_EAST | (3 << 3));
		put (13, 116, -28, SIGNAL_LAMP);
		sim::runTicks (14);
		check (lit (13, 116, -28), "circuits : torche -> répéteur -> lampe");
		sim::use (10, 117, -28, 0);
		sim::runTicks (3);
		check ((g.world.meta (11, 116, -28) & M_ON) && lit (13, 116, -28), "circuits : la torche s'éteint (inverseur), le répéteur retient");
		sim::runTicks (12);
		check (!lit (13, 116, -28), "circuits : la lampe s'éteint après le délai du répéteur");
		put (10, 118, -26, STICKY_PISTON, sim::D_EAST);
		put (11, 118, -26, GLUE_BLOCK);
		put (11, 119, -26, GOLD_BLOCK);
		put (9, 118, -26, LEVER, sim::D_EAST);
		sim::runTicks (2);
		sim::use (9, 118, -26, 0);
		sim::runTicks (3);
		check (g.world.get (11, 118, -26) == PISTON_HEAD && g.world.get (12, 118, -26) == GLUE_BLOCK && g.world.get (12, 119, -26) == GOLD_BLOCK,
		       "circuits : piston collant, la colle entraîne l'or");
		sim::use (9, 118, -26, 0);
		sim::runTicks (3);
		check (g.world.get (11, 118, -26) == GLUE_BLOCK && g.world.get (11, 119, -26) == GOLD_BLOCK, "circuits : le piston collant les ramène");
		put (10, 116, -22, OBSERVER, sim::D_EAST);
		put (9, 116, -22, NOTE_BLOCK);
		sim::runTicks (4);
		put (11, 116, -22, DIRT);
		sim::runTicks (3);
		check (g.world.meta (9, 116, -22) & M_POW, "circuits : l'observateur voit un bloc posé, le bloc musical joue");
		put (20, 116, -20, WATER);
		sim::runTicks (60);
		check (g.world.get (27, 116, -20) == WATER && g.world.meta (27, 116, -20) == 7 && g.world.get (28, 116, -20) == AIR, "liquides : l'eau s'étale de 7 blocs");
		put (20, 116, -14, LAVA);
		put (21, 116, -14, WATER);
		sim::runTicks (2);
		check (g.world.get (20, 116, -14) == OBSIDIAN, "liquides : lave et eau font de l'obsidienne");
		{	// une machine qui avance seule : observateurs, piston, piston collant, colle (en l'air)
			int Y = 120, Z = -12;
			put (10, Y + 1, Z, GLUE_BLOCK), put (11, Y + 1, Z, GLUE_BLOCK);
			put (11, Y, Z, PISTON, sim::D_EAST);
			put (12, Y, Z, GLUE_BLOCK), put (13, Y, Z, GLUE_BLOCK);
			put (12, Y + 1, Z, STICKY_PISTON, sim::D_WEST);
			put (13, Y + 1, Z, OBSERVER, sim::D_EAST);
			put (10, Y, Z, OBSERVER, sim::D_WEST);
			sim::runTicks (2);
			put (9, Y, Z, DIRT), put (9, Y, Z, AIR);
			sim::runTicks (40);
			int minx = 99;
			for (int x = 9; x <= 30; x++)
				if (g.world.get (x, Y, Z) == OBSERVER) minx = std::min (minx, x);
			check (minx >= 13 && minx < 99, "circuits : la machine à colle avance seule (" + std::to_string (minx - 10) + " blocs en 2 s)");
		}
		con ("mode survival");
		con ("clear");
		con ("give copper_ore 1");
		bool hv;
		mineTime (COPPER_ORE, I_IRON_PICKAXE, &hv);
		check (hv && mining (COPPER_ORE).drop == WIRE && mining (COPPER_ORE).dropN == 4, "minage : le cuivre (pioche en fer) donne 4 poudres");
		con ("give wire 9");
		con ("give stick 1");
		std::string tc = con ("craft copper_torch");
		check (g.inv.count (COPPER_TORCH) == 1, "fabrication : une torche de cuivre (poudre sur un bâton)");
		con ("clear");
		con ("mode creative");
		// une scène pour la capture
		for (int z = 3; z <= 9; z++)
			for (int x = -4; x <= 4; x++) put (x, 104, z, z == 9 || x == 4 ? STONE_BRICK : STONE);
		put (-3, 105, 4, LEVER, sim::D_DOWN | M_ON);
		for (int x = -2; x <= 0; x++) put (x, 105, 4, WIRE);
		put (1, 105, 4, REPEATER, sim::D_EAST | (1 << 3));
		put (2, 105, 4, WIRE);
		put (3, 105, 4, SIGNAL_LAMP);
		put (-3, 105, 6, COPPER_TORCH, sim::D_DOWN);
		put (-2, 105, 6, WIRE), put (-2, 105, 7, WIRE), put (-1, 105, 7, WIRE);
		put (0, 105, 7, PISTON, sim::D_EAST);
		put (1, 105, 7, COBBLE);
		put (-3, 105, 8, LAVA);
		put (2, 105, 8, WATER);
		put (3, 105, 6, TNT);
		put (-1, 105, 5, STONE_PLATE), put (0, 105, 5, OBSERVER, sim::D_UP), put (1, 105, 5, DAYLIGHT_SENSOR);
		put (2, 105, 6, COMPARATOR, sim::D_NORTH);
		sim::runTicks (40);
		check (lit (3, 105, 4) && g.world.get (1, 105, 7) == PISTON_HEAD, "scène : la lampe allumée, le piston sorti");
	}
	// les créatures, la santé, la faim (entity.cpp)
	{
		con ("mode survival");
		con ("mobs on");
		for (int cz = 1; cz <= 3; cz++)
			for (int cx = 1; cx <= 3; cx++) g.world.ensure (cx, cz);	// (les créatures d'un tronçon pas fait restent figées)
		for (int y = 100; y <= 104; y++)	// (une dalle en l'air : rien entre eux)
			for (int z = 40; z <= 48; z++)
				for (int x = 38; x <= 56; x++) g.world.set (x, y, z, y == 100 ? STONE : AIR);
		gameTeleport (41.5, 101, 44.5);
		g.health = 20, g.hunger = 20;
		g.hour = 23;
		int rid = g.mobs.spawn (E_RODEUR, 47.5, 101, 44.5)->id;
		for (int i = 0; i < 200 && g.health >= 20; i++) g.mobs.update (g.world, 0.05);
		check (g.health < 20, "créatures : le rôdeur vient frapper la nuit (santé " + std::to_string ((int) g.health) + ")");
		if (Entity *rr = g.mobs.get (rid)) rr->dead = 9;
		g.mobs.update (g.world, 0.05);
		con ("heal");
		con ("clear");
		con ("give cooked_chicken 2");
		g.hunger = 10, g.saturation = 0;
		g.selected = 0;
		check (eatHeld () && g.hunger == 16 && g.inv.count (I_COOKED_CHICKEN) == 1, "faim : manger un poulet rôti (+6)");
		con ("give planks 2");
		con ("give stick 1");
		g.world.set ((int) floor (g.pl.x) + 2, (int) floor (g.pl.y), (int) floor (g.pl.z), WORKBENCH);
		g.world.set ((int) floor (g.pl.x) - 2, (int) floor (g.pl.y), (int) floor (g.pl.z), FURNACE);
		con ("craft wood_sword");
		check (g.inv.count (I_WOOD_SWORD) == 1 && attackDamage (I_WOOD_SWORD) == 4, "fabrication : une épée en bois (4 de dégâts)");
		con ("give raw_pork 1");
		con ("give coal 1");
		con ("craft cooked_pork");
		check (g.inv.count (I_COOKED_PORK) == 1, "four : une côtelette grillée");
		Entity *w = g.mobs.spawn (E_WOLF, g.pl.x + 2, g.pl.y, g.pl.z + 2);
		bool used;
		std::string msg;
		for (int i = 0; i < 40 && !w->tamed; i++) g.mobs.interact (*w, I_BONE, used, msg);
		check (w->tamed && w->sitting, "créatures : un loup apprivoisé avec des os");
		std::string el = apiExec ("entities.list(16)", r);
		check (el.find ("wolf") != std::string::npos && el.find (",1") != std::string::npos, "protocole : entities.list");
		std::string ms = mcpHandle (R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"spawn_entity","arguments":{"type":"pig","position":[44,80,40],"count":2}}})");
		check (ms.find ("ids") != std::string::npos && g.mobs.count (E_PIG, 64) >= 2, "MCP : spawn_entity (deux cochons)");
		con ("kill all");
		g.mobs.update (g.world, 0.05);
		con ("mode creative");
		g.hour = 11;
		// pour la capture : une poule, un cochon, un loup apprivoisé assis, un rôdeur, une araignée, au bord de la dalle
		auto put = [] (int type, double x, double z, float yaw) {
			Entity *e = g.mobs.spawn (type, x, 105, z);
			e->yaw = yaw, e->onGround = true;
			return e;
		};
		put (E_CHICKEN, -3.2, 3.5, 200);
		put (E_PIG, -1.4, 3.5, 160);
		Entity *ww = put (E_WOLF, 0.7, 3.4, 225);
		ww->tamed = ww->sitting = true;
		put (E_RODEUR, 2.6, 3.4, 210);
		put (E_SPIDER, 2.8, 9.6, 250);
		for (int z = 3; z <= 9; z++) g.world.set (-4, 105, z, AIR);
	}
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
		else if (p.first == "music") g.musicOn = p.second != "0";
		else if (p.first == "sound") g.sfxOn = p.second != "0";
		else if (p.first == "musicvol") g.musicVol = std::max (0, std::min (100, atoi (p.second.c_str ()))) / 100.0f;
		else if (p.first == "soundvol") g.sfxVol = std::max (0, std::min (100, atoi (p.second.c_str ()))) / 100.0f;
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
	if (!g_ctx || !(g_view.capW = (int) std::max (scr[0], sw), g_view.capH = (int) std::max (scr[1], sh),
					g_view.surf = armgl_surface (g_view.capW, g_view.capH)) || !armgl_make_current (g_ctx, g_view.surf)) {
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
	sim::soundHook = simSound;
	sim::dropHook = simDrop;
	sim::pushHook = simPush;
	sim::blastHook = simBlastAll;
	sim::occupiedHook = simOccupied;
	if (audio::start ()) gameAudioChanged ();
	else fprintf (stderr, "terracube : pas de son (AudioKit ou sa sortie absents)\n");
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
		soundMood ();
		R.updateMeshes (g.world, playerCamera (), g.distance, g_selftest ? 400 : 6);
		freeChunks ();
		renderTo (g_view.w, g_view.h, playerCamera ());
		present ();
		if (armgl_lost ()) {
			fprintf (stderr, "terracube : image refusée ou GPU perdu\n");
		}
		frames++;
		if (now - fpsT > 1000) {
			g_fps = frames * 1000.0 / (now - fpsT);
			frames = 0, fpsT = now;
		}
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
	audio::stop ();
	return g_selftest ? (g_fails ? 1 : 0) : 0;
}
