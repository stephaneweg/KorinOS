//
// cosmozoom -- Cosmo Zoom!: an anti-gravity racer in 3D (Onyx's game, on ARMPI's kits: README.md). Drawn by gk_draw (the
// V3D GPU; the processor without one, at half the size), ships made of parts (a hull, wings, an engine, the paint) in the
// hangar, eight racers, gadgets from the item capsules, star bits, speed pads, jumps, loops and tubes ridden on their
// outside; Grand Prix (4 cups of 4 circuits), single races, time trials. Music and sounds made by the game (AudioKit).
// English and French.
//
//   Full screen (ga::fullscreen). Keyboard: Left / Right steer, Up or X accelerate, Down brake (and, held, throws a
//   gadget backward), Z or C drift, Space the gadget, V look back, Esc or P (in a race) the pause menu (ga::Pause:
//   Resume, Restart, Quit; the || button at the top right too), F12 the frame rate. Menus: arrows, Enter, Esc, R random
//   (hangar). The mouse: a click on what the menus show, the wheel to move.
//   Pads (pad N drives player N; pad 0 is the keyboard): the cross steers and moves in the menus, A accelerates and
//   chooses, B brakes and goes back, X the gadget, Y looks back (random in the hangar), L / R / L2 / R2 drift, Start the
//   pause menu in a race.
//
// Files: SD0:/apps/cosmozoom.app/ tracks/*.trk (the circuits), cups.ini (the cups), models/*.obj (the 3D models);
// SD0:/home/cosmozoom/ save.ini (the trophies, the best times, the players' ships, the options), ghosts/.
// Command line (tests): demo (the title's race only), auto (the players' ships drive themselves), players=N, fps,
// race=<track> (straight into a race), screen=<menu|hangar|cups|options>, seed=N.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "sys.h"
#include "scene.h"
#include "audio.h"
#include "ui.h"
#include "../../inc/gameart.h"
#include <math.h>

using namespace cz;

namespace ga { struct ui_vars ui; }
using ga::ui;
static ga::Pause g_pause;

// ==== the catalogue: the circuits, the cups ============================================================================
struct TrackInfo { char id[32], name[48], nameFr[48]; int style; };
struct CupInfo { char name[48], nameFr[48]; unsigned color; char tracks[4][36]; int unlock; };	// unlock: 0 open, 1 a trophy in cup 2, 2 a trophy in each other
enum { MAXTRACKS = 24, NCUPS = 4 };
static TrackInfo g_tracks[MAXTRACKS]; static int g_ntracks = 0;
static CupInfo g_cups[NCUPS]; static int g_ncups = 0;

static int trackIndex (const char *id)
{
	char base[36]; snprintf (base, sizeof base, "%s", id);
	int L = (int) strlen (base); if (L && base[L - 1] == '*') base[L - 1] = 0;
	for (int i = 0; i < g_ntracks; i++) if (!strcmp (g_tracks[i].id, base)) return i;
	return -1;
}
static bool isRemix (const char *id) { int L = (int) strlen (id); return L && id[L - 1] == '*'; }
static bool g_fr = false;
static const char *trackName (const char *id, char *buf, int cap)
{
	int i = trackIndex (id);
	const char *n = i < 0 ? id : (g_fr && g_tracks[i].nameFr[0] ? g_tracks[i].nameFr : g_tracks[i].name);
	if (isRemix (id)) snprintf (buf, (size_t) cap, "%s %s", n, TR ("Remix"));
	else snprintf (buf, (size_t) cap, "%s", n);
	return buf;
}

static char *trimmed (char *s) { while (*s == ' ') s++; int L = (int) strlen (s); while (L && s[L - 1] == ' ') s[--L] = 0; return s; }
static void loadCatalogue ()
{
	Kv kv; kv.load (APPDIR "cups.ini");
	g_ntracks = g_ncups = 0;
	for (int i = 0; i < kv.n; i++)
	{
		const char *sec = kv.it[i].sec, *k = kv.it[i].key, *v = kv.it[i].val;
		if (!strcmp (sec, "tracks") && g_ntracks < MAXTRACKS)
		{
			// id = Name | Nom | style
			TrackInfo &T = g_tracks[g_ntracks++];
			memset (&T, 0, sizeof T);
			snprintf (T.id, sizeof T.id, "%s", k);
			char buf[160]; snprintf (buf, sizeof buf, "%s", v);
			char *a = buf, *b = strchr (a, '|'), *c = b ? strchr (b + 1, '|') : 0;
			if (b) *b++ = 0;
			if (c) *c++ = 0;
			snprintf (T.name, sizeof T.name, "%s", trimmed (a));
			snprintf (T.nameFr, sizeof T.nameFr, "%s", b ? trimmed (b) : T.name);
			T.style = c ? atoi (c) : 1;
		}
		else if (!strncmp (sec, "cup", 3) && g_ncups <= NCUPS)
		{
			int n = atoi (sec + 3) - 1;
			if (n < 0 || n >= NCUPS) continue;
			if (n >= g_ncups) g_ncups = n + 1;
			CupInfo &C = g_cups[n];
			if (!strcmp (k, "name")) snprintf (C.name, sizeof C.name, "%s", v);
			else if (!strcmp (k, "name_fr")) snprintf (C.nameFr, sizeof C.nameFr, "%s", v);
			else if (!strcmp (k, "color")) C.color = (unsigned) strtoul (v, 0, 16);
			else if (!strcmp (k, "unlock")) C.unlock = atoi (v);
			else if (!strcmp (k, "tracks"))
			{
				char buf[200]; snprintf (buf, sizeof buf, "%s", v);
				int t = 0;
				for (char *p = strtok (buf, " ,"); p && t < 4; p = strtok (0, " ,")) snprintf (C.tracks[t++], sizeof C.tracks[0], "%s", p);
			}
		}
	}
	free (kv.it);
}

// ==== the save ===========================================================================================================
static Kv g_save;
static int saveInt (const char *sec, const char *key, int def) { const char *v = g_save.get (sec, key); return v ? atoi (v) : def; }
static void setInt (const char *sec, const char *key, int v) { char b[16]; snprintf (b, sizeof b, "%d", v); g_save.set (sec, key, b); }
static void writeSave () { g_save.save (SAVE, "Cosmo Zoom! -- the trophies, the best times, the ships, the options"); }

// ==== the platform: the picture, the GPU, the keys ======================================================================
struct GkFrame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
struct GkTex { unsigned *px; int w, h, id, pad; };
static_assert (sizeof (GkFrame) == GKF_SIZE && sizeof (GkTex) == GKT_SIZE, "gpukit.inc");

static unsigned *g_px; static int g_W, g_H, g_stride;
static bool g_gpu = false;
static unsigned *g_soft; static int g_softCap = 0;
static Textures g_tx;
static GkTex g_gkt[TX_COUNT];
static Text g_text;
static Frame g_frame;

static void gpuInit ()
{
	g_gpu = gk_info (0) == 1;
	for (int t = 1; t < TX_COUNT; t++)
	{
		g_gkt[t].px = g_tx.store.px[t]; g_gkt[t].w = g_tx.store.w[t]; g_gkt[t].h = g_tx.store.h[t]; g_gkt[t].id = -1;
		if (g_gpu) g_gkt[t].id = (int) gk_texture (-1, (long) g_gkt[t].px, g_gkt[t].w, g_gkt[t].h, g_gkt[t].w);
		if (g_gkt[t].id < 0) g_gpu = false;
	}
}
// The frame drawn into the picture's rectangle (x, y, w, h); clear: the background. By the GPU at the full size; by the
// processor at half the size, then doubled
static void render3d (Frame &F, int x, int y, int w, int h, unsigned clear, bool keep = false)
{
	if (w <= 0 || h <= 0) return;
	if (g_gpu)
	{
		GkFrame f = { g_px + (long) y * g_stride + x, w, h, g_stride, clear, keep ? (unsigned) GK_F_KEEP : 0u, 0 };
		long r = gk_draw ((long) &f, (long) F.v.p, F.v.n, (long) F.b.p, F.b.n, (long) g_gkt);
		if (r == 1) return;
		g_gpu = false;				// (it stopped: the processor from now on)
		if (r == 0) return;
	}
	int sw = (w + 1) / 2, sh = (h + 1) / 2;
	if (sw * sh > g_softCap)
	{
		free (g_soft);
		g_softCap = sw * sh;
		g_soft = (unsigned *) malloc (sizeof (unsigned) * (size_t) g_softCap);
		if (!g_soft) { g_softCap = 0; return; }
	}
	if (keep)
		for (int j = 0; j < sh; j++)
			for (int i = 0; i < sw; i++) g_soft[j * sw + i] = g_px[(long) (y + j * 2) * g_stride + x + i * 2];
	GkFrame f = { g_soft, sw, sh, sw, clear, keep ? (unsigned) GK_F_KEEP : 0u, 0 };
	gk_draw ((long) &f, (long) F.v.p, F.v.n, (long) F.b.p, F.b.n, (long) g_gkt);
	for (int j = 0; j < h; j++)
	{
		unsigned *d = g_px + (long) (y + j) * g_stride + x; const unsigned *s = g_soft + (j / 2) * sw;
		for (int i = 0; i < w / 2; i++) d[i * 2] = d[i * 2 + 1] = s[i];
		if (w & 1) d[w - 1] = s[sw - 1];
	}
}

// ---- input: the keyboard made into buttons (player 1; the keys held come from the window's events) ---------------------
enum { BT_UP = 1, BT_DOWN = 2, BT_LEFT = 4, BT_RIGHT = 8, BT_OK = 16, BT_BACK = 32, BT_ACC = 64, BT_BRAKE = 128, BT_DRIFT = 256,
       BT_USE = 512, BT_PAUSE = 1024, BT_RANDOM = 2048, BT_LOOK = 4096 };
struct PlayerIn { unsigned held, prev, press; float steer; bool pad; };
static PlayerIn g_in[4];
static unsigned g_anyPress;			// every player's presses together (the menus)
static unsigned g_mousePress;			// what the mouse pressed since the last frame
static bool g_hid[256]; static unsigned char g_hidChar[256], g_char[256];	// held: HID codes; letters, by what the layout says
static bool g_hidUp[256];			// let go since the last frame (a quick tap counts: a frame can be long)
static unsigned g_padTap[4];			// a pad's buttons pressed since the last frame (the same)

static inline bool held_char (int c) { return g_char[c] != 0; }
// a pad's buttons as the game's: pad N (1..3) drives player N - 1 (pad 0, the keyboard, only in a GP_PAD0 test: player 1)
static unsigned padButtons (unsigned p, float &st)
{
	unsigned b = 0;
	if (p & ga::PAD_UP) b |= BT_UP;
	if (p & ga::PAD_DOWN) b |= BT_DOWN;
	if (p & ga::PAD_LEFT) { b |= BT_LEFT; st = -1; }
	if (p & ga::PAD_RIGHT) { b |= BT_RIGHT; st = st < 0 ? 0 : 1; }
	if (p & ga::PAD_A) b |= BT_OK | BT_ACC;
	if (p & ga::PAD_B) b |= BT_BACK | BT_BRAKE;
	if (p & ga::PAD_X) b |= BT_USE;
	if (p & ga::PAD_Y) b |= BT_LOOK | BT_RANDOM;
	if (p & (ga::PAD_L | ga::PAD_R | ga::PAD_L2 | ga::PAD_R2)) b |= BT_DRIFT;
	if (p & ga::PAD_START) b |= BT_OK | BT_PAUSE;	// (in the menus: the pause menu has not taken it)
	return b;
}
static void readInput ()
{
	unsigned b = 0; float st = 0;
	if (g_hid[KEY_UP]) b |= BT_UP | BT_ACC;
	if (held_char ('x')) b |= BT_ACC;
	if (g_hid[KEY_DOWN]) b |= BT_DOWN | BT_BRAKE;
	if (g_hid[KEY_LEFT]) { b |= BT_LEFT; st = -1; }
	if (g_hid[KEY_RIGHT]) { b |= BT_RIGHT; st = st < 0 ? 0 : 1; }
	if (g_hid[KEY_ENTER]) b |= BT_OK;
	if (g_hid[KEY_ESC]) b |= BT_BACK | BT_PAUSE;
	if (held_char ('p')) b |= BT_PAUSE;
	if (held_char ('z') || held_char ('c')) b |= BT_DRIFT;
	if (held_char (' ')) b |= BT_USE | BT_OK;
	if (held_char ('r')) b |= BT_RANDOM;
	if (held_char ('v')) b |= BT_LOOK;
	g_anyPress = 0;
	for (int p = 0; p < 4; p++)
	{
		float ps = 0;
		unsigned pb = padButtons (p < 3 ? g_pause.held[p + 1] | g_padTap[p + 1] : 0, ps);
		if (p == 0) pb |= padButtons (g_pause.held[0] | g_padTap[0], ps);	// (0 unless GP_PAD0)
		PlayerIn &P = g_in[p];
		unsigned all = pb | (p == 0 ? b : 0);
		P.prev = P.held; P.held = all; P.press = (all & ~P.prev) | (p == 0 ? g_mousePress : 0);
		P.steer = p == 0 && st != 0 ? st : ps; P.pad = pb != 0;
		g_anyPress |= P.press;
	}
	g_mousePress = 0; memset (g_padTap, 0, sizeof g_padTap);
	for (int h = 0; h < 256; h++)
		if (g_hidUp[h])
		{
			g_hidUp[h] = false; g_hid[h] = false;
			if (g_hidChar[h]) { g_char[g_hidChar[h]] = 0; g_hidChar[h] = 0; }
		}
}

// ---- the mouse: what the menus drew this frame, to click on --------------------------------------------------------------
enum { HIT_MENU = 0, HIT_OPTION = 10, HIT_ROW = 20, HIT_PAUSE = 30, HIT_CLASS = 40, HIT_CUP = 50, HIT_TRACK = 60, HIT_HINT = 90 };
struct Hit { int x, y, w, h, id; };
static Hit g_hits[64]; static int g_nhits;
static inline void addHit (int x, int y, int w, int h, int id) { if (g_nhits < 64) g_hits[g_nhits++] = Hit { x, y, w, h, id }; }
static int g_clickX, g_clickY; static bool g_clicked;

// ==== the game ============================================================================================================
enum { ST_TITLE, ST_MENU, ST_OPTIONS, ST_HANGAR, ST_SELECT, ST_RACE, ST_RESULTS, ST_PODIUM };
enum { MODE_GP, MODE_SINGLE, MODE_TT };

static Track g_track; static Race g_race; static Scene g_scene; static Camera g_cam[4]; static MiniMap g_map;
static Music g_music; static Sfx g_sfx;
static int g_state = ST_TITLE; static float g_stateT = 0;
static int g_mode = MODE_GP, g_players = 1, g_cls = 1, g_cup = 0, g_raceNo = 0, g_trackSel = 0, g_laps = 3;
static int g_menuSel = 0, g_optSel = 0;
static int g_hangarP = 0, g_hangarRow = 0; static float g_hangarSpin = 0;
static int g_selRow = 0;
static Build g_build[4]; static int g_pilot[4];
static int g_racerPilot[MAXCRAFT]; static Build g_racerBuild[MAXCRAFT]; static int g_points[MAXCRAFT]; static int g_lastPts[MAXCRAFT];
static int g_order[MAXCRAFT];			// the standings
static bool g_demo = false, g_auto = false, g_showFps = false;
static float g_fps = 0;
static unsigned g_seed = 1;
static bool g_inRace = false;			// a real race (not the title's)
static char g_curTrack[36] = "";
static float g_acc = 0;				// the physics' time owed
static int g_assist = 1, g_autoAcc = 0; static bool g_musicOn = true, g_sfxOn = true;
static float g_msgT = 0; static char g_msg[64] = ""; static unsigned g_msgCol = C_WHITE; static int g_msgFor = -1;
static float g_finishT[4];
static bool g_quitNow = false;

static void setState (int s) { g_state = s; g_stateT = 0; }
static const char *cupName (int c) { return g_fr && g_cups[c].nameFr[0] ? g_cups[c].nameFr : g_cups[c].name; }

// a trophy won: 1 gold .. 3 bronze, 0 none
static int trophy (int cup, int cls) { char k[24]; snprintf (k, sizeof k, "cup%d_%d", cup, cls); return saveInt ("trophies", k, 0); }
static bool anyTrophy (int cup) { for (int k = 0; k < NCLASS; k++) if (trophy (cup, k)) return true; return false; }
static bool cupOpen (int c)
{
	if (g_cups[c].unlock == 1) return anyTrophy (1);
	if (g_cups[c].unlock == 2) return anyTrophy (0) && anyTrophy (1) && anyTrophy (2);
	return true;
}
static bool classOpen (int k) { return k != CLASS_MIRROR || trophy (3, 2) == 1 || trophy (3, 3) == 1 || saveInt ("unlocks", "mirror", 0); }

// ---- loading a circuit ------------------------------------------------------------------------------------------------
static bool loadTrack (const char *id, bool mirror)
{
	char path[160]; int ti = trackIndex (id);
	snprintf (path, sizeof path, APPDIR "tracks/%s.trk", ti >= 0 ? g_tracks[ti].id : id);
	if (!g_track.load (path, mirror, isRemix (id))) return false;
	g_scene.load (g_track, g_seed);
	g_map.make (g_track);
	snprintf (g_curTrack, sizeof g_curTrack, "%s", id);
	return true;
}

// The racers: the players' pilots and ships, then the others' (their own ships)
static void makeRacers ()
{
	bool used[NPILOT] = { false };
	for (int p = 0; p < g_players; p++) { g_racerPilot[p] = g_pilot[p]; g_racerBuild[p] = g_build[p]; used[g_pilot[p]] = true; }
	int k = g_players;
	for (int i = 0; i < NPILOT && k < MAXCRAFT; i++) if (!used[i]) { g_racerPilot[k] = i; g_racerBuild[k] = PILOTS[i].build; k++; }
	for (int i = 0; i < MAXCRAFT; i++) { g_points[i] = 0; g_lastPts[i] = 0; g_order[i] = i; }
}

// The Time Trial's ghost: SD0:/home/cosmozoom/ghosts/<circuit>_<class>.gst (a header, then the samples)
static void ghostPath (char *p, int cap) { snprintf (p, (size_t) cap, HOMEDIR "/ghosts/%s_%d.gst", g_curTrack, g_cls); for (char *q = p + strlen (HOMEDIR); *q; q++) if (*q == '*') *q = 'r'; }
static void loadGhost ()
{
	Ghost &G = g_scene.ghost; G.alloc (); G.n = 0; G.show = false; G.nbest = 0;
	char p[200]; ghostPath (p, sizeof p);
	long n = 0; char *b = read_file (p, &n);
	if (b && n > 16 && !memcmp (b, "CZG1", 4))
	{
		int cnt; memcpy (&cnt, b + 4, 4); memcpy (&G.build, b + 8, sizeof (Build)); memcpy (&G.pilot, b + 8 + sizeof (Build), 4);
		long need = 12 + (long) sizeof (Build) + (long) cnt * (long) sizeof (Ghost::Sample);
		if (cnt > 1 && cnt <= Ghost::MAX && n >= need)
		{
			memcpy (G.best, b + 12 + sizeof (Build), (size_t) cnt * sizeof (Ghost::Sample)); G.nbest = cnt; G.show = true;
			buildShip (G.model, g_scene.lib, G.build, G.pilot);
		}
	}
	free (b);
}
static void saveGhost ()
{
	Ghost &G = g_scene.ghost;
	if (!G.rec || G.n < 2) return;
	long size = 12 + (long) sizeof (Build) + (long) G.n * (long) sizeof (Ghost::Sample);
	char *b = (char *) malloc ((size_t) size); if (!b) return;
	memcpy (b, "CZG1", 4); memcpy (b + 4, &G.n, 4); memcpy (b + 8, &g_race.c[0].build, sizeof (Build)); memcpy (b + 8 + sizeof (Build), &g_race.c[0].pilot, 4);
	memcpy (b + 12 + sizeof (Build), G.rec, (size_t) G.n * sizeof (Ghost::Sample));
	mkdir (HOMEDIR "/ghosts", 0777);
	char p[200]; ghostPath (p, sizeof p);
	save_file (p, b, (size_t) size);
	free (b);
}

// A race on the circuit: the humans and the computer's pilots on the grid (by the standings: the leader at the back)
static void startRace (const char *id, bool real)
{
	g_inRace = real;
	bool mirror = real && g_cls == CLASS_MIRROR;
	if (!loadTrack (id, mirror)) return;
	int n = (real && g_mode == MODE_TT) ? 1 : MAXCRAFT;
	int grid[MAXCRAFT];
	for (int i = 0; i < MAXCRAFT; i++) grid[i] = i;
	if (real && g_mode == MODE_GP && g_raceNo > 0)
		for (int i = 0; i < MAXCRAFT; i++) grid[i] = g_order[MAXCRAFT - 1 - i];
	else if (real) for (int i = 0; i < MAXCRAFT; i++) grid[i] = (i + g_players) % MAXCRAFT;	// (the players at the back)
	for (int slot = 0; slot < n; slot++)
	{
		int who = real ? grid[slot] : slot;
		Craft &C = g_race.c[slot];
		memset (&C, 0, sizeof C);
		C.pilot = real ? g_racerPilot[who] : (int) ((slot + g_seed) % NPILOT);
		C.build = real ? g_racerBuild[who] : PILOTS[C.pilot].build;
		C.human = real && who < g_players && !g_auto;
		C.player = C.human ? who : -1;
		if (real && who < g_players) C.player = who;
		C.skill = 0;
	}
	g_race.start (&g_track, n, real ? (g_mode == MODE_SINGLE ? g_laps : 3) : 99, g_cls, g_seed++, !(real && g_mode == MODE_TT));
	g_race.assist = g_assist == 0 ? 0.0f : g_assist == 1 ? 0.5f : 0.9f;
	g_race.autoThrottle = g_autoAcc == 1 || (g_autoAcc == 2 && g_cls == 0);
	if (real && g_mode == MODE_TT) { g_race.c[0].item = IT_TRIPLE; g_race.c[0].itemCount = 3; loadGhost (); }
	else g_scene.ghost.show = false;
	if (!real) g_race.time = 0.01f;		// (the title's race: already going)
	g_scene.ships (g_race);
	for (int p = 0; p < 4; p++) { g_cam[p].init = false; g_finishT[p] = 0; }
	g_acc = 0; g_pause.on = false; g_msgT = 0;
	int ti = trackIndex (id);
	g_music.start (ti >= 0 ? g_tracks[ti].style : 1, (unsigned) ti * 7919u + 13);
}
// the craft slot a player drives
static int slotOf (int player) { for (int i = 0; i < g_race.n; i++) if (g_race.c[i].player == player) return i; return 0; }

static void titleRace ()
{
	if (!g_ntracks) return;
	int ti = (int) (g_seed % (unsigned) g_ntracks);
	startRace (g_tracks[ti].id, false);
	g_music.start (0, 3);
}

// ---- the race goes on -------------------------------------------------------------------------------------------------
static void message (const char *m, unsigned col, float t = 1.6f, int forPlayer = -1)
{
	snprintf (g_msg, sizeof g_msg, "%s", m); g_msgCol = col; g_msgT = t; g_msgFor = forPlayer;
}

static void raceEvents ()
{
	for (int e = 0; e < g_race.nev; e++)
	{
		const Event &E = g_race.ev[e];
		if (!g_inRace) continue;
		bool mine = E.craft >= 0 && g_race.c[E.craft].human;
		int pl = mine ? g_race.c[E.craft].player : -1;
		switch (E.kind)
		{
		case EV_GO: message (TR ("GO!"), C_MINT, 1.0f); break;
		case EV_FINALLAP: if (mine) message (TR ("FINAL LAP!"), C_YELLOW, 2.0f, pl); if (mine) g_music.tempoMul = 1.06f; break;
		case EV_LAP: if (mine) { char b[32]; snprintf (b, sizeof b, TR ("LAP %d"), E.arg); message (b, C_WHITE, 1.2f, pl); } break;
		case EV_START_BOOST: if (mine) message (TR ("GREAT START!"), C_YELLOW, 1.4f, pl); break;
		case EV_FINISH: if (mine && pl >= 0) g_finishT[pl] = 0.001f; break;
		case EV_WARP: if (mine) message (TR ("WARP STAR!"), C_YELLOW, 1.4f, pl); break;
		case EV_NEBULA: if (g_race.c[E.arg].human) message (TR ("STORM INCOMING!"), C_GRAPE, 1.6f, g_race.c[E.arg].player); break;
		}
	}
}

static void raceUpdate (float dt)
{
	if (g_pause.on || !g_race.T) return;
	Input in[MAXCRAFT]; memset (in, 0, sizeof in);
	for (int i = 0; i < g_race.n; i++)
	{
		const Craft &C = g_race.c[i];
		if (!C.human || C.player < 0) continue;
		const PlayerIn &P = g_in[C.player];
		in[i].steer = P.steer;
		in[i].throttle = (P.held & BT_ACC) ? 1.0f : 0.0f;
		in[i].brake = (P.held & BT_BRAKE) && !(P.held & BT_ACC);
		in[i].drift = (P.held & BT_DRIFT) != 0;
		in[i].use = (P.held & BT_USE) != 0;
		in[i].back = (P.held & BT_DOWN) != 0;
		g_cam[C.player].look = (P.held & BT_LOOK) ? 1.0f : 0.0f;
	}
	g_acc += dt;
	int steps = 0;
	while (g_acc >= 1.0f / 60 && steps < 6)
	{
		g_race.step (1.0f / 60, in);
		if (g_inRace && g_mode == MODE_TT) g_scene.ghost.record (g_race.c[0], g_race.time);
		g_scene.emitFor (g_race, 1.0f / 60);
		raceEvents ();
		g_sfx.events (g_race, g_inRace ? slotOf (0) : -1);
		g_acc -= 1.0f / 60; steps++;
	}
	if (steps == 6) g_acc = 0;
	for (int p = 0; p < 4; p++) if (g_finishT[p] > 0) g_finishT[p] += dt;
	if (g_msgT > 0) g_msgT -= dt;
	int views = g_inRace ? g_players : 1;
	for (int p = 0; p < views; p++)
	{
		// the start: the camera turns round the ship while the lights count, then sits behind it
		if (g_inRace && g_race.time < -1.3f) g_cam[p].orbit (g_race, slotOf (p), g_race.time + 4.0f);
		else g_cam[p].follow (g_race, g_inRace ? slotOf (p) : 0, dt);
	}
	if (g_inRace) { const Craft &C = g_race.c[slotOf (0)]; g_sfx.hum (C.human ? C.v : 0, C.boostT > 0); }
}

// the race's results into the standings
static void scoreRace ()
{
	int ord[MAXCRAFT]; int n = g_race.n;
	for (int i = 0; i < n; i++) ord[i] = i;
	for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (g_race.c[ord[b]].place < g_race.c[ord[a]].place) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
	for (int k = 0; k < MAXCRAFT; k++) g_lastPts[k] = 0;
	// craft slot -> racer: the racer whose pilot it is
	for (int k = 0; k < n; k++)
	{
		int slot = ord[k], who = -1;
		for (int r = 0; r < MAXCRAFT; r++) if (g_racerPilot[r] == g_race.c[slot].pilot) who = r;
		if (who < 0) continue;
		g_lastPts[who] = RACE_POINTS[k];
		g_points[who] += RACE_POINTS[k];
	}
	for (int i = 0; i < MAXCRAFT; i++) g_order[i] = i;
	for (int a = 0; a < MAXCRAFT; a++) for (int b = a + 1; b < MAXCRAFT; b++) if (g_points[g_order[b]] > g_points[g_order[a]]) { int t = g_order[a]; g_order[a] = g_order[b]; g_order[b] = t; }
	// the best times
	for (int i = 0; i < n; i++)
	{
		const Craft &C = g_race.c[i];
		if (!C.human || !C.finished || C.bestLap <= 0) continue;
		char k[64]; snprintf (k, sizeof k, "%s_%d_lap", g_curTrack, g_cls);
		int best = saveInt ("times", k, 0), mine = (int) (C.bestLap * 1000);
		if (!best || mine < best) setInt ("times", k, mine);
		snprintf (k, sizeof k, "%s_%d_race", g_curTrack, g_cls);
		best = saveInt ("times", k, 0); mine = (int) (C.finishTime * 1000);
		if (g_mode == MODE_TT && (!best || mine < best)) { setInt ("times", k, mine); saveGhost (); }
	}
	writeSave ();
}

// ==== drawing ===============================================================================================================
static int g_viewX[4], g_viewY[4], g_viewW[4], g_viewH[4];
static void layoutViews (int n)
{
	for (int p = 0; p < 4; p++) { g_viewX[p] = g_viewY[p] = 0; g_viewW[p] = g_W; g_viewH[p] = g_H; }
	if (n == 2) for (int p = 0; p < 2; p++) { g_viewY[p] = p * (g_H / 2); g_viewH[p] = g_H / 2 - (p == 0 ? 2 : 0); }
	if (n >= 3) for (int p = 0; p < 4; p++) { g_viewX[p] = (p & 1) * (g_W / 2); g_viewY[p] = (p / 2) * (g_H / 2); g_viewW[p] = g_W / 2 - 2 * !(p & 1); g_viewH[p] = g_H / 2 - 2 * !(p / 2); }
}

static void draw3dViews ()
{
	if (!g_race.T) { Surf S { g_px, g_W, g_H, g_stride }; S.noClip (); S.vgrad (0, 0, g_W, g_H, 0x0A0F3A, 0x3A1E6E); return; }
	int views = g_inRace ? g_players : 1;
	layoutViews (views);
	if (views > 1) { Surf S { g_px, g_W, g_H, g_stride }; S.noClip (); S.rect (0, 0, g_W, g_H, C_INK); }
	for (int p = 0; p < views; p++)
	{
		g_frame.clear ();
		g_scene.view (g_frame, g_race, g_cam[p], g_viewW[p], g_viewH[p], p);
		render3d (g_frame, g_viewX[p], g_viewY[p], g_viewW[p], g_viewH[p], g_track.sky[1]);
	}
	if (views == 3)
	{
		// the fourth quarter: the map
		Surf S { g_px, g_W, g_H, g_stride }; S.noClip ();
		S.vgrad (g_viewX[3], g_viewY[3], g_viewW[3], g_viewH[3], 0x2A2F6E, 0x4CC9F0);
		float r = fminf ((float) g_viewW[3], (float) g_viewH[3]) * 0.4f;
		g_map.draw (S, g_race, (float) (g_viewX[3] + g_viewW[3] / 2), (float) (g_viewY[3] + g_viewH[3] / 2), r, -1);
	}
}

// ---- the HUD of one player's view (DESIGN.md 9: the user's reference picture) --------------------------------------------
// top left: the shield and its energy, the boost, the times; top centre: the gadget, "BOOST ACTIVE"; top right: the
// leaderboard; bottom left: the speed dial and the digital speed; bottom right: the map, the lap, the place.
static void hud (int p)
{
	Surf S { g_px, g_W, g_H, g_stride };
	S.setClip (g_viewX[p], g_viewY[p], g_viewX[p] + g_viewW[p], g_viewY[p] + g_viewH[p]);
	UiPage U;
	U.k = fminf ((float) g_viewW[p] / 1280.0f, (float) g_viewH[p] / 720.0f);
	if (g_players > 1) U.k = fmaxf (U.k, 0.42f);
	U.ox = g_viewX[p]; U.oy = g_viewY[p];
	float PW = (float) g_viewW[p] / U.k, PH = (float) g_viewH[p] / U.k;	// the page's size in its units
	int i = slotOf (p);
	const Craft &C = g_race.c[i];
	char b[96];
	bool small = g_players > 1;
	g_text.italic = true;
	float T = g_race.time;
	// ---- the shield and its energy
	{
		float cx = 62, cy = 58;
		S.disc ((float) U.X (cx), (float) U.Y (cy), (float) U.L (44), C_SKY, 80);
		S.disc ((float) U.X (cx), (float) U.Y (cy), (float) U.L (40), 0x0A1030, 200);
		S.ring ((float) U.X (cx), (float) U.Y (cy), (float) U.L (40), (float) U.L (4), C.shieldT > 0 ? C_MINT : C_SKY);
		// a shield shape
		float k = U.k, x = (float) U.X (cx), y = (float) U.Y (cy);
		float sh[12] = { x - 20 * k, y - 22 * k, x + 20 * k, y - 22 * k, x + 20 * k, y + 2 * k, x + 8 * k, y + 18 * k, x, y + 24 * k, x - 8 * k, y + 18 * k };
		unsigned scol = C.shieldT > 0 ? C_MINT : C.energy < 30 ? (((int) (T * 6)) & 1 ? C_RED : 0x802030) : 0x4CC9F0;
		S.poly (sh, 6, scol);
		float sh2[12] = { x - 12 * k, y - 15 * k, x + 12 * k, y - 15 * k, x + 12 * k, y + 0 * k, x + 4 * k, y + 11 * k, x, y + 15 * k, x - 4 * k, y + 11 * k };
		S.poly (sh2, 6, rgbMix (scol, 0xFFFFFF, 0.45f));
		// the energy: 10 segments, green to red
		neonPanel (S, U.X (112), U.Y (24), U.L (250), U.L (30), U.L (6));
		int lit = (int) ceilf (C.energy / 10.0f);
		for (int k2 = 0; k2 < 10; k2++)
		{
			unsigned c = hueColor (0.33f - 0.33f * (float) k2 / 9.0f);
			c = rgbMix (0xFF2030, 0x40FF60, 1.0f - (float) k2 / 9.0f);
			if (k2 >= lit) c = 0x2A2F55;
			float x0 = 120 + (float) k2 * 24;
			float pp[8] = { (float) U.X (x0 + 4), (float) U.Y (29), (float) U.X (x0 + 24), (float) U.Y (29), (float) U.X (x0 + 20), (float) U.Y (49), (float) U.X (x0), (float) U.Y (49) };
			S.poly (pp, 4, c);
		}
		// the boost / drift charge
		float boost = C.boostT > 0 ? clampf (C.boostT / 1.5f, 0, 1) : C.driftDir ? clampf (C.driftCharge / 2.6f, 0, 1) : 0;
		neonPanel (S, U.X (112), U.Y (62), U.L (200), U.L (14), U.L (5), C_ORANGE);
		if (boost > 0) S.rrect (U.X (114), U.Y (64), U.L (196 * boost), U.L (10), U.L (4), C.driftLevel >= 3 ? C_PINK : C_ORANGE);
		g_text.italic = false;
		g_text.draw (S, U.X (96), U.Y (58), U.L (14), "\xE2\x9A\xA1", C_YELLOW, 1, 0);
		g_text.italic = true;
	}
	// ---- the times
	if (!small)
	{
		neonPanel (S, U.X (20), U.Y (110), U.L (250), U.L (70), U.L (8));
		g_text.draw (S, U.X (34), U.Y (116), U.L (17), g_mode == MODE_TT ? TR ("TIME TRIAL") : TR ("RACE"), C_TXT, 0, 0);
		float lapT = C.finished ? (C.lap >= 2 ? C.lapTimes[(C.lap - 2) & 7] : 0) : (T > 0 ? T - C.lapStart : 0);
		char t1[24], t2[24]; fmtTime (lapT, t1, sizeof t1); fmtTime (C.finished ? C.finishTime : (T > 0 ? T : 0), t2, sizeof t2);
		g_text.draw (S, U.X (34), U.Y (138), U.L (17), TR ("LAP"), C_MINT, 0, 0);
		g_text.draw (S, U.X (256), U.Y (138), U.L (17), t1, C_TXT, 2, 0);
		g_text.draw (S, U.X (34), U.Y (158), U.L (17), TR ("TIME"), C_YELLOW, 0, 0);
		g_text.draw (S, U.X (256), U.Y (158), U.L (17), t2, C_TXT, 2, 0);
	}
	// ---- the gadget's slot, the banner
	{
		float cx = PW * 0.5f, cy = 62;
		neonPanel (S, U.X (cx - 46), U.Y (cy - 46), U.L (92), U.L (92), U.L (16), C.rollT > 0 ? C_PINK : C_SKY);
		int it = C.item;
		if (C.rollT > 0) it = 1 + (int) (T * 14) % (IT_COUNT - 1);
		if (it != IT_NONE) itemIcon (S, it, (float) U.X (cx), (float) U.Y (cy), (float) U.L (34), T * 2);
		if (C.itemCount > 1 && C.rollT <= 0)
		{
			snprintf (b, sizeof b, "x%d", C.itemCount);
			g_text.draw (S, U.X (cx + 56), U.Y (cy - 12), U.L (22), b, C_TXT, 0, U.L (2));
		}
		if (C.empT > 0) for (int k = 0; k < 12; k++) S.star ((float) U.X (cx - 40 + (float) ((k * 37) % 80)), (float) U.Y (cy - 40 + (float) ((k * 53) % 80)), (float) U.L (7), C_WHITE, 200, T * 5 + (float) k);
		const char *ban = C.warpT > 0 ? TR ("WARP STAR") : C.boostT > 0 ? TR ("BOOST ACTIVE") : C.beamT > 0 ? TR ("TRACTOR BEAM") : C.shieldT > 0 && !small ? TR ("SHIELD UP") : 0;
		if (ban)
		{
			int tw = g_text.width (ban, U.L (24)) / (int) (U.k > 0 ? 1 : 1);
			float w = (float) tw / U.k + 60;
			float y = 128;
			neonPanel (S, U.X (cx - w / 2), U.Y (y), U.L (w), U.L (40), U.L (8));
			g_text.draw (S, U.X (cx), U.Y (y + 6), U.L (24), ban, C_TXT, 1, 0);
			float ph = fmodf (T * 3, 1.0f);
			for (int k = 0; k < 3; k++)
			{
				float o = (float) k * 16 + ph * 16;
				unsigned c = rgbScale (C_SKY, 1.0f - (float) k * 0.25f);
				chevronH (S, (float) U.X (cx - w / 2 - 14 - o), (float) U.Y (y + 20), (float) U.L (12), c, true);
				chevronH (S, (float) U.X (cx + w / 2 + 14 + o), (float) U.Y (y + 20), (float) U.L (12), c, false);
			}
		}
	}
	// ---- the leaderboard
	if (!small && g_race.n > 1)
	{
		int rows = g_race.n < 4 ? g_race.n : 4;
		bool meIn = C.place < rows;
		int nrows = meIn ? rows : rows + 1;
		float x = PW - 300, y = 18;
		neonPanel (S, U.X (x), U.Y (y), U.L (282), U.L (36 + 28 * (float) nrows), U.L (8));
		g_text.draw (S, U.X (x + 14), U.Y (y + 6), U.L (18), TR ("LEADERBOARD"), C_TXT, 0, 0);
		for (int r = 0; r < nrows; r++)
		{
			int place = r < rows ? r : C.place;
			for (int j = 0; j < g_race.n; j++)
			{
				if (g_race.c[j].place != place) continue;
				float ry = y + 32 + (float) r * 28;
				char o[16]; ordinal (place + 1, o, sizeof o);
				bool me = j == i;
				unsigned col = me ? C_YELLOW : C_TXT;
				g_text.draw (S, U.X (x + 14), U.Y (ry), U.L (18), o, col, 0, 0);
				S.rrect (U.X (x + 64), U.Y (ry + 5), U.L (12), U.L (14), U.L (3), PILOTS[g_race.c[j].pilot].color);
				const char *nm = g_race.c[j].human ? (g_players > 1 ? 0 : TR ("YOU")) : 0;
				char who[48];
				if (g_race.c[j].human && g_players > 1) { snprintf (who, sizeof who, TR ("PLAYER %d"), g_race.c[j].player + 1); nm = who; }
				if (!nm) nm = TR (HULLS[g_race.c[j].build.hull].name);
				snprintf (b, sizeof b, "[%s]", nm);
				g_text.draw (S, U.X (x + 84), U.Y (ry), U.L (18), b, col, 0, 0);
			}
		}
	}
	// ---- the speed: a dial (0..10) and the digital speed
	{
		float cx = 92, cy = PH - 92, r = 74;
		S.disc ((float) U.X (cx), (float) U.Y (cy), (float) U.L (r + 4), C_SKY, 90);
		S.disc ((float) U.X (cx), (float) U.Y (cy), (float) U.L (r), 0x0A1030, 210);
		float top = CLASSES[g_race.cls].top * 1.6f;
		for (int k = 0; k <= 10; k++)
		{
			float a = PI * 0.75f + (float) k / 10 * PI * 1.5f;
			unsigned c = k >= 8 ? C_RED : C_TXT;
			S.line ((float) U.X (cx + cosf (a) * (r - 14)), (float) U.Y (cy + sinf (a) * (r - 14)), (float) U.X (cx + cosf (a) * (r - 4)), (float) U.Y (cy + sinf (a) * (r - 4)), (float) U.L (3), c);
			char n[4]; snprintf (n, sizeof n, "%d", k);
			g_text.draw (S, U.X (cx + cosf (a) * (r - 26)), U.Y (cy + sinf (a) * (r - 26) - 8), U.L (12), n, c, 1, 0);
		}
		// the red zone's arc
		for (int k = 0; k < 8; k++)
		{
			float a0 = PI * 0.75f + (0.8f + 0.025f * (float) k) * PI * 1.5f;
			S.disc ((float) U.X (cx + cosf (a0) * (r - 2)), (float) U.Y (cy + sinf (a0) * (r - 2)), (float) U.L (3), C_RED);
		}
		float f = clampf (C.v / top, 0, 1.05f), a = PI * 0.75f + f * PI * 1.5f;
		S.line ((float) U.X (cx), (float) U.Y (cy), (float) U.X (cx + cosf (a) * (r - 12)), (float) U.Y (cy + sinf (a) * (r - 12)), (float) U.L (4), C_RED);
		S.disc ((float) U.X (cx), (float) U.Y (cy), (float) U.L (7), C_WHITE);
		// the digital speed
		neonPanel (S, U.X (176), U.Y (PH - 78), U.L (190), U.L (58), U.L (10));
		g_text.draw (S, U.X (186), U.Y (PH - 104), U.L (18), TR ("SPEED"), C_TXT, 0, U.L (2));
		snprintf (b, sizeof b, "%d", (int) (C.v * 3.6f));
		g_text.draw (S, U.X (300), U.Y (PH - 72), U.L (38), b, C_SKY, 2, 0);
		g_text.draw (S, U.X (306), U.Y (PH - 50), U.L (14), "km/h", C_SKY, 0, 0);
		// the star bits
		S.star ((float) U.X (200), (float) U.Y (PH - 128), (float) U.L (11), C_YELLOW);
		snprintf (b, sizeof b, "%d", C.bits);
		g_text.draw (S, U.X (216), U.Y (PH - 141), U.L (18), b, C_TXT, 0, U.L (2));
	}
	// ---- the map, the lap, the place
	{
		int lap = C.lap < 1 ? 1 : C.lap > g_race.laps ? g_race.laps : C.lap;
		if (!small)
		{
			float mx = PW - 190, my = PH - 330;
			neonPanel (S, U.X (mx), U.Y (my), U.L (170), U.L (150), U.L (10));
			g_map.draw (S, g_race, (float) U.X (mx + 85), (float) U.Y (my + 75), (float) U.L (62), i);
		}
		float x0 = PW - 330, y0 = PH - 120;
		g_text.draw (S, U.X (x0 + 70), U.Y (y0), U.L (22), TR ("LAP"), C_TXT, 1, U.L (2));
		snprintf (b, sizeof b, "%d/%d", lap, g_race.laps);
		g_text.draw (S, U.X (x0 + 70), U.Y (y0 + 24), U.L (48), b, C_TXT, 1, U.L (3));
		g_text.draw (S, U.X (x0 + 230), U.Y (y0), U.L (22), TR ("PLACE"), C_TXT, 1, U.L (2));
		char o[16]; ordinal (C.place + 1, o, sizeof o);
		unsigned pc = C.place == 0 ? C_YELLOW : C_TXT;
		g_text.draw (S, U.X (x0 + 230), U.Y (y0 + 24), U.L (48), o, pc, 1, U.L (3));
	}
	// ---- the messages in the middle
	{
		const char *m = 0; unsigned col = C_TXT;
		char cnt[8];
		if (T < 0 && g_race.count >= 1 && g_race.count <= 3) { snprintf (cnt, sizeof cnt, "%d", g_race.count); m = cnt; col = g_race.count == 1 ? C_MINT : C_YELLOW; }
		else if (g_msgT > 0 && (g_msgFor < 0 || g_msgFor == p)) { m = g_msg; col = g_msgCol; }
		if (C.wrongWay && !C.finished) { m = TR ("WRONG WAY!"); col = C_RED; }
		if (g_finishT[p] > 0)
		{
			char o[16]; ordinal (C.place + 1, o, sizeof o);
			snprintf (b, sizeof b, "%s!", o); m = b; col = C.place == 0 ? C_YELLOW : C_TXT;
		}
		if (m) g_text.draw (S, U.X (PW * 0.5f), U.Y (PH * 0.32f), U.L (g_finishT[p] > 0 ? 96 : 84), m, col, 1, U.L (6));
		if (g_finishT[p] > 1.5f && g_race.over) g_text.draw (S, U.X (PW * 0.5f), U.Y (PH * 0.55f), U.L (24), TR ("Press A / Enter"), C_TXT, 1, U.L (3));
	}
	g_text.italic = false;
	S.noClip ();
}

// ---- the menus' look: dark see-through panels with neon frames over the city, italic white titles ------------------------
static UiPage g_U;
static Surf screenSurf () { Surf S { g_px, g_W, g_H, g_stride }; S.noClip (); return S; }

static void titleText (Surf &S, const char *t, float y, int px = 64, unsigned col = 0xFFFFFFFF)
{
	if (col == 0xFFFFFFFF) col = C_TXT;
	g_text.italic = true;
	g_text.draw (S, g_U.X (640) - g_U.L (3), g_U.Y (y), g_U.L (px), t, C_PINK, 1, 1);
	g_text.draw (S, g_U.X (640) + g_U.L (3), g_U.Y (y), g_U.L (px), t, C_SKY, 1, 1);
	g_text.draw (S, g_U.X (640), g_U.Y (y), g_U.L (px), t, col, 1, g_U.L (2));
	g_text.italic = false;
}
static void button (Surf &S, float x, float y, float w, float h, const char *t, bool sel, unsigned col = C_SKY, bool enabled = true)
{
	float wob = sel ? sinf (g_stateT * 6) * 2 : 0;
	neonPanel (S, g_U.X (x - wob), g_U.Y (y), g_U.L (w + 2 * wob), g_U.L (h), g_U.L (10), sel ? col : 0x3A4A8A, sel ? 215 : 170);
	if (sel) S.rrect (g_U.X (x - wob) + 2, g_U.Y (y) + 2, g_U.L (w + 2 * wob) - 4, g_U.L (h) - 4, g_U.L (8), col, 60);
	g_text.italic = true;
	g_text.draw (S, g_U.X (x + w / 2), g_U.Y (y + h * 0.2f), g_U.L (h * 0.42f), t, !enabled ? C_DIMTXT : C_TXT, 1, 0);
	g_text.italic = false;
}
static void hint (Surf &S, const char *t)
{
	neonPanel (S, g_U.X (40), g_U.Y (668), g_U.L (1200), g_U.L (36), g_U.L (10), 0x3A4A8A, 190);
	addHit (g_U.X (40), g_U.Y (668), g_U.L (1200), g_U.L (36), HIT_HINT);
	g_text.draw (S, g_U.X (640), g_U.Y (674), g_U.L (17), t, C_TXT, 1);
}
static void logo (Surf &S, float y, float scale)
{
	// COSMO ZOOM: slanted white letters with magenta and cyan offsets, a light sweep, chevrons
	int px = (int) (118 * scale);
	g_text.italic = true;
	const char *t = "COSMO ZOOM";
	int off = g_U.L (5 * scale);
	float sweep = fmodf (g_stateT * 0.6f, 2.0f);
	g_text.draw (S, g_U.X (640) - off, g_U.Y (y) + off / 2, g_U.L (px), t, C_PINK, 1, 0);
	g_text.draw (S, g_U.X (640) + off, g_U.Y (y) - off / 2, g_U.L (px), t, C_SKY, 1, 0);
	g_text.draw (S, g_U.X (640), g_U.Y (y), g_U.L (px), t, C_TXT, 1, g_U.L (3 * scale));
	int w = g_text.width (t, g_U.L (px));
	if (sweep < 1)
	{
		int sx = g_U.X (640) - w / 2 + (int) ((float) w * sweep);
		for (int k = 0; k < g_U.L (14 * scale); k++) S.rect (sx + k, g_U.Y (y), 1, g_U.L (px * 1.25f), C_WHITE, 120 - k * 8 > 0 ? 120 - k * 8 : 0);
	}
	float cy = (float) g_U.Y (y + px * 1.45f);
	for (int k = 0; k < 6; k++)
	{
		float ph = fmodf (g_stateT * 2 + (float) k * 0.2f, 1.0f);
		chevronH (S, (float) (g_U.X (640) - w / 2) - (float) k * 22 * scale * g_U.k, cy, 11 * scale * g_U.k, rgbScale (C_PINK, 0.4f + 0.6f * ph), true);
		chevronH (S, (float) (g_U.X (640) + w / 2) + (float) k * 22 * scale * g_U.k, cy, 11 * scale * g_U.k, rgbScale (C_SKY, 0.4f + 0.6f * ph), false);
	}
	g_text.draw (S, g_U.X (640), (int) cy - g_U.L (12 * scale), g_U.L (22 * scale), TR ("THE VELOCITY GRID"), C_YELLOW, 1, g_U.L (2));
	g_text.italic = false;
}

// ---- the title, the menu, the options ------------------------------------------------------------------------------------
static const char *MENU[] = { TRN ("Grand Prix"), TRN ("Single Race"), TRN ("Time Trial"), TRN ("Options"), TRN ("Quit") };
static void drawTitle ()
{
	Surf S = screenSurf ();
	S.vgrad (0, 0, g_W, g_H / 3, 0x1A2A5A, 0x1A2A5A, 40);
	logo (S, 150, 1.0f);
	if (((int) (g_stateT * 2)) & 1 || g_stateT < 0.5f) titleText (S, TR ("PRESS START"), 480, 36, C_YELLOW);
	g_text.draw (S, g_U.X (640), g_U.Y (684), g_U.L (15), TR ("An anti-gravity racer for Korin OS"), C_TXT, 1, g_U.L (2));
}
static void drawMenu ()
{
	Surf S = screenSurf ();
	S.shade (70, jet (J_FACE));
	logo (S, 30, 0.55f);
	static const unsigned COL[5] = { C_PINK, C_SKY, C_MINT, C_GRAPE, C_ORANGE };
	for (int k = 0; k < 5; k++) { button (S, 440, 200 + (float) k * 78, 400, 62, TR (MENU[k]), g_menuSel == k, COL[k]); addHit (g_U.X (440), g_U.Y (200 + (float) k * 78), g_U.L (400), g_U.L (62), HIT_MENU + k); }
	char b[48]; snprintf (b, sizeof b, g_players == 1 ? TR ("< %d player >") : TR ("< %d players >"), g_players);
	if (g_menuSel < 2) { g_text.italic = true; g_text.draw (S, g_U.X (640), g_U.Y (602), g_U.L (24), b, C_YELLOW, 1, g_U.L (2)); g_text.italic = false; }
	hint (S, TR ("Up / Down: choose   Left / Right: players   A / Enter: go   B / Esc: back"));
}
static const char *ASSIST[] = { TRN ("Off"), TRN ("Some"), TRN ("Lots") };
static const char *AUTOACC[] = { TRN ("Off"), TRN ("On"), TRN ("Orbit only") };
static void drawOptions ()
{
	Surf S = screenSurf ();
	S.shade (90, jet (J_FACE));
	titleText (S, TR ("OPTIONS"), 50, 56);
	const char *lab[5] = { TR ("Music"), TR ("Sounds"), TR ("Steering help"), TR ("Auto-accelerate"), TR ("Back") };
	const char *val[5] = { g_musicOn ? TR ("On") : TR ("Off"), g_sfxOn ? TR ("On") : TR ("Off"), TR (ASSIST[g_assist]), TR (AUTOACC[g_autoAcc]), "" };
	for (int k = 0; k < 5; k++)
	{
		bool sel = g_optSel == k;
		float y = 170 + (float) k * 84;
		neonPanel (S, g_U.X (300), g_U.Y (y), g_U.L (680), g_U.L (62), g_U.L (10), sel ? C_GRAPE : 0x3A4A8A, sel ? 210 : 170);
		addHit (g_U.X (300), g_U.Y (y), g_U.L (680), g_U.L (62), HIT_OPTION + k);
		g_text.draw (S, g_U.X (330), g_U.Y (y + 14), g_U.L (26), lab[k], C_TXT, 0, 0);
		if (val[k][0]) { char b[48]; snprintf (b, sizeof b, "< %s >", val[k]); g_text.draw (S, g_U.X (950), g_U.Y (y + 14), g_U.L (26), b, sel ? C_YELLOW : C_SKY, 2, 0); }
	}
	hint (S, TR ("Up / Down: choose   Left / Right: change   B / Esc: back"));
}

// ---- the hangar: one player's pilot and ship ---------------------------------------------------------------------------
enum { HR_PILOT, HR_HULL, HR_WING, HR_ENGINE, HR_PAINT, HR_TRIM, HR_PATTERN, HR_COUNT };
static ShipModel g_hangarShip; static Build g_hangarBuilt = { -1, -1, -1, -1, -1, -1 }; static int g_hangarBuiltPilot = -1;
static void hangarModel ()
{
	Build &b = g_build[g_hangarP];
	if (memcmp (&b, &g_hangarBuilt, sizeof b) || g_pilot[g_hangarP] != g_hangarBuiltPilot)
	{
		buildShip (g_hangarShip, g_scene.lib, b, g_pilot[g_hangarP]);
		g_hangarBuilt = b; g_hangarBuiltPilot = g_pilot[g_hangarP];
	}
}
static void drawHangar ()
{
	Surf S = screenSurf ();
	S.vgrad (0, 0, g_W, g_H, 0x6FB8F2, 0xD8EEFF);
	// the ship on its lit platform, drawn by the 3D in the left part
	hangarModel ();
	int vx = 0, vy = 0, vw = g_W, vh = g_H;		// (the whole screen, the ship drawn left of centre, over the sky)
	{
		Frame &F = g_frame; F.clear ();
		float a = g_hangarSpin;
		float L = len (g_hangarShip.mesh.hi - g_hangarShip.mesh.lo);
		V3 eye (sinf (a) * L * 1.55f, L * 0.62f, cosf (a) * L * 1.55f);
		M4 shift = M4::identity (); shift.m[3] = -0.45f;	// (clip x - 0.47 w: the picture moved left)
		M4 VP = shift * M4::perspective (0.62f, (float) vw / (float) vh, 0.3f, 200) * M4::lookAt (eye, V3 (0, 0.5f, 0), V3 (0, 1, 0));
		// the platform: a hexagonal pad with a glowing rim, a floor grid
		F.begin (VP, 0, B_CULL);
		const int K = 6;
		float R = L * 0.5f;
		for (int k = 0; k < K; k++)
		{
			float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K;
			V3 c0 (0, -0.05f, 0), p0 (cosf (a0) * R, -0.05f, sinf (a0) * R), p1 (cosf (a1) * R, -0.05f, sinf (a1) * R);
			F.tri (c0, p1, p0, (k & 1) ? 0x47B9AE : 0x3FA9A0);
			F.quad (p0, p1, p1 - V3 (0, 0.5f, 0), p0 - V3 (0, 0.5f, 0), 0x2E8A82);
		}
		F.end ();
		M4 M = M4::translate (V3 (0, 0.55f + sinf (g_stateT * 2) * 0.06f, 0));
		F.begin (VP * M, 0, B_CULL); F.mesh (g_hangarShip.mesh, M4::identity ()); F.end ();
		F.begin (VP * M, 0, B_ALPHA | B_NOZWRITE | B_CULL); F.mesh (g_hangarShip.mesh, M4::identity (), 255, 0, 1); F.end ();
		F.begin (VP * M, 0, B_ADD | B_NOZWRITE); F.mesh (g_hangarShip.mesh, M4::identity (), 255, 0, 2); F.end ();
		F.begin (VP, 0, B_ADD | B_NOZWRITE);
		for (int k = 0; k < K; k++)
		{
			float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K;
			V3 p0 (cosf (a0) * R, 0, sinf (a0) * R), p1 (cosf (a1) * R, 0, sinf (a1) * R);
			F.quad (p0, p1, p1 * 1.04f, p0 * 1.04f, 0x406080);
			F.quad (p0, p1, p1 + V3 (0, 0.06f, 0), p0 + V3 (0, 0.06f, 0), 0x406080);
		}
		for (int k = -8; k <= 8; k++)
		{
			float x = (float) k * 2.5f;
			F.quad (V3 (x - 0.03f, -0.6f, -20), V3 (x + 0.03f, -0.6f, -20), V3 (x + 0.03f, -0.6f, 20), V3 (x - 0.03f, -0.6f, 20), 0x203858);
			F.quad (V3 (-20, -0.6f, x - 0.03f), V3 (-20, -0.6f, x + 0.03f), V3 (20, -0.6f, x + 0.03f), V3 (20, -0.6f, x - 0.03f), 0x203858);
		}
		F.end ();
		render3d (F, vx, vy, vw, vh, 0x9FD3F7, true);
	}
	char b[96];
	snprintf (b, sizeof b, g_players > 1 ? TR ("HANGAR -- PLAYER %d") : TR ("HANGAR"), g_hangarP + 1);
	g_text.italic = true;
	g_text.draw (S, g_U.X (40), g_U.Y (22), g_U.L (42), b, C_TXT, 0, g_U.L (3));
	g_text.italic = false;
	// the rows
	const Build &B = g_build[g_hangarP];
	const char *lab[HR_COUNT] = { TR ("Pilot"), TR ("Ship"), TR ("Wings"), TR ("Engine"), TR ("Colour"), TR ("Trim"), TR ("Pattern") };
	const Pilot &P = PILOTS[g_pilot[g_hangarP]];
	char pil[80]; snprintf (pil, sizeof pil, "%s  #%s", P.name, P.number);
	const char *val[HR_COUNT] = { pil, TR (HULLS[B.hull].name), TR (WINGS[B.wing].name), TR (ENGINES[B.engine].name), TR (PAINT_NAMES[B.paint]), TR (PAINT_NAMES[B.paint2]), TR (PATTERNS[B.pattern]) };
	for (int k = 0; k < HR_COUNT; k++)
	{
		bool sel = g_hangarRow == k;
		float y = 92 + (float) k * 56;
		neonPanel (S, g_U.X (690), g_U.Y (y), g_U.L (550), g_U.L (46), g_U.L (8), sel ? C_PINK : 0x3A4A8A, sel ? 215 : 170);
		addHit (g_U.X (690), g_U.Y (y), g_U.L (550), g_U.L (46), HIT_ROW + k);
		g_text.draw (S, g_U.X (708), g_U.Y (y + 11), g_U.L (19), lab[k], sel ? C_TXT : C_SKY, 0, 0);
		snprintf (b, sizeof b, sel ? "< %s >" : "%s", val[k]);
		g_text.draw (S, g_U.X (1224), g_U.Y (y + 9), g_U.L (22), b, C_TXT, 2, 0);
		if (k == HR_PILOT) helmet (S, (float) g_U.X (850), (float) g_U.Y (y + 23), (float) g_U.L (15), P.color, P.trim, g_pilot[g_hangarP]);
		if (k == HR_PAINT || k == HR_TRIM) { unsigned c = PAINTS[k == HR_PAINT ? B.paint : B.paint2]; S.disc ((float) g_U.X (850), (float) g_U.Y (y + 23), (float) g_U.L (13), C_WHITE); S.disc ((float) g_U.X (850), (float) g_U.Y (y + 23), (float) g_U.L (11), c); }
	}
	// the stats: five bars each
	Stats st = statsOf (B);
	const char *sl[4] = { TR ("Speed"), TR ("Acceleration"), TR ("Handling"), TR ("Weight") };
	int sv[4] = { st.speed, st.accelPts, st.handling, st.weightPts };
	neonPanel (S, g_U.X (690), g_U.Y (494), g_U.L (550), g_U.L (156), g_U.L (10));
	for (int k = 0; k < 4; k++)
	{
		float y = 506 + (float) k * 34;
		g_text.draw (S, g_U.X (708), g_U.Y (y), g_U.L (19), sl[k], C_TXT, 0, 0);
		for (int j = 0; j < 5; j++)
		{
			float x0 = 920 + (float) j * 60;
			float pp[8] = { (float) g_U.X (x0 + 6), (float) g_U.Y (y + 3), (float) g_U.X (x0 + 56), (float) g_U.Y (y + 3), (float) g_U.X (x0 + 50), (float) g_U.Y (y + 23), (float) g_U.X (x0), (float) g_U.Y (y + 23) };
			S.poly (pp, 4, j < sv[k] ? (k == 3 ? C_GRAPE : rgbMix (C_SKY, C_MINT, (float) j / 4)) : 0x2A2F55);
		}
	}
	const char *perk = g_hangarRow == HR_WING ? WINGS[B.wing].perk : g_hangarRow == HR_ENGINE ? ENGINES[B.engine].perk : g_hangarRow == HR_HULL ? HULLS[B.hull].look : g_hangarRow == HR_PILOT ? (g_fr ? P.teamFr : P.team) : "";
	if (perk && *perk) { g_text.italic = true; g_text.draw (S, g_U.X (330), g_U.Y (612), g_U.L (24), TR (perk), C_YELLOW, 1, g_U.L (2)); g_text.italic = false; }
	hint (S, TR ("Up / Down: choose   Left / Right: change   Y / R: random   A / Enter: ready   B / Esc: back"));
}

// ---- the circuit / cup selection ------------------------------------------------------------------------------------------
static void drawSelect ()
{
	Surf S = screenSurf ();
	S.shade (95, jet (J_FACE));
	char b[96];
	titleText (S, g_mode == MODE_GP ? TR ("CHOOSE A CUP") : TR ("CHOOSE A CIRCUIT"), 20, 46);
	for (int k = 0; k < NCLASS; k++)
	{
		bool sel = g_cls == k, open = classOpen (k);
		float x = 260 + (float) k * 196;
		neonPanel (S, g_U.X (x), g_U.Y (96), g_U.L (180), g_U.L (46), g_U.L (10), sel ? (g_selRow == 0 ? C_PINK : C_GRAPE) : 0x3A4A8A, sel ? 215 : 160);
		addHit (g_U.X (x), g_U.Y (96), g_U.L (180), g_U.L (46), HIT_CLASS + k);
		g_text.italic = true;
		g_text.draw (S, g_U.X (x + 90), g_U.Y (106), g_U.L (22), open ? TR (CLASSES[k].name) : "?", sel ? C_TXT : C_SKY, 1, 0);
		g_text.italic = false;
	}
	if (g_mode == MODE_GP)
	{
		for (int c = 0; c < g_ncups; c++)
		{
			bool sel = g_cup == c, open = cupOpen (c);
			float x = 70 + (float) c * 290, y = 176;
			unsigned cc = g_cups[c].color ? g_cups[c].color : C_SKY;
			neonPanel (S, g_U.X (x), g_U.Y (y), g_U.L (270), g_U.L (420), g_U.L (16), sel ? cc : 0x3A4A8A, sel ? 220 : 170);
			addHit (g_U.X (x), g_U.Y (y), g_U.L (270), g_U.L (420), HIT_CUP + c);
			float cx = (float) g_U.X (x + 135), cy = (float) g_U.Y (y + 82);
			int tr = trophy (c, g_cls);
			unsigned tc = tr == 1 ? 0xFFD23F : tr == 2 ? 0xDDE6FF : tr == 3 ? 0xFFA45C : 0x3A4A8A;
			float k = g_U.k;
			S.disc (cx, cy - 6 * k, 52 * k, cc, sel ? 90 : 40);
			float cupP[8] = { cx - 40 * k, cy - 38 * k, cx + 40 * k, cy - 38 * k, cx + 22 * k, cy + 10 * k, cx - 22 * k, cy + 10 * k };
			S.poly (cupP, 4, open ? tc : 0x2A2F55); S.rect ((int) (cx - 7 * k), (int) (cy + 8 * k), (int) (14 * k), (int) (20 * k), open ? tc : 0x2A2F55); S.rect ((int) (cx - 26 * k), (int) (cy + 26 * k), (int) (52 * k), (int) (10 * k), open ? tc : 0x2A2F55);
			if (!open) g_text.draw (S, (int) cx, (int) (cy - 34 * k), g_U.L (40), "?", C_TXT, 1);
			g_text.italic = true;
			g_text.draw (S, g_U.X (x + 135), g_U.Y (y + 146), g_U.L (22), cupName (c), C_TXT, 1, 0);
			g_text.italic = false;
			for (int t = 0; t < 4; t++) g_text.draw (S, g_U.X (x + 135), g_U.Y (y + 200 + (float) t * 48), g_U.L (17), open ? trackName (g_cups[c].tracks[t], b, sizeof b) : "...", sel ? C_TXT : C_SKY, 1, 0);
		}
	}
	else
	{
		for (int t = 0; t < g_ntracks; t++)
		{
			bool sel = g_trackSel == t;
			float x = 150 + (float) (t / 7) * 500, y = 172 + (float) (t % 7) * 62;
			neonPanel (S, g_U.X (x), g_U.Y (y), g_U.L (470), g_U.L (50), g_U.L (10), sel ? C_SKY : 0x3A4A8A, sel ? 215 : 160);
			addHit (g_U.X (x), g_U.Y (y), g_U.L (470), g_U.L (50), HIT_TRACK + t);
			g_text.draw (S, g_U.X (x + 20), g_U.Y (y + 12), g_U.L (22), trackName (g_tracks[t].id, b, sizeof b), C_TXT, 0, 0);
			char k[64]; snprintf (k, sizeof k, "%s_%d_%s", g_tracks[t].id, g_cls, g_mode == MODE_TT ? "race" : "lap");
			int best = saveInt ("times", k, 0);
			if (best) { char tb[24]; fmtTime ((float) best / 1000.0f, tb, sizeof tb); g_text.draw (S, g_U.X (x + 450), g_U.Y (y + 14), g_U.L (18), tb, C_YELLOW, 2, 0); }
		}
		if (g_mode == MODE_SINGLE)
		{
			snprintf (b, sizeof b, g_laps == 1 ? TR ("< %d lap >") : TR ("< %d laps >"), g_laps);
			neonPanel (S, g_U.X (520), g_U.Y (612), g_U.L (240), g_U.L (40), g_U.L (10), g_selRow == 2 ? C_PINK : 0x3A4A8A);
			g_text.draw (S, g_U.X (640), g_U.Y (619), g_U.L (20), b, C_TXT, 1);
		}
	}
	hint (S, TR ("Up / Down: class, choice, laps   Left / Right: change   A / Enter: race!   B / Esc: back"));
}

// ---- the results, the podium -------------------------------------------------------------------------------------------
static void drawResults ()
{
	Surf S = screenSurf ();
	S.shade (110, jet (J_FACE));
	char b[96], c2[48];
	if (g_mode == MODE_GP) { snprintf (b, sizeof b, TR ("RACE %d OF 4 -- %s"), g_raceNo + 1, trackName (g_curTrack, c2, sizeof c2)); titleText (S, b, 20, 36); }
	else titleText (S, trackName (g_curTrack, c2, sizeof c2), 20, 36);
	for (int k = 0; k < g_race.n; k++)
		for (int i = 0; i < g_race.n; i++)
		{
			const Craft &C = g_race.c[i];
			if (C.place != k) continue;
			float y = 92 + (float) k * 66;
			float appear = clampf (g_stateT * 4 - (float) k * 0.4f, 0, 1);
			float x = 160 + (1 - appear) * 900;
			neonPanel (S, g_U.X (x), g_U.Y (y), g_U.L (960), g_U.L (54), g_U.L (10), C.human ? C_YELLOW : 0x3A4A8A, C.human ? 200 : 170);
			char o[16]; ordinal (k + 1, o, sizeof o);
			g_text.italic = true;
			g_text.draw (S, g_U.X (x + 24), g_U.Y (y + 10), g_U.L (28), o, k == 0 ? C_YELLOW : C_TXT, 0);
			g_text.italic = false;
			const Pilot &P = PILOTS[C.pilot];
			helmet (S, (float) g_U.X (x + 136), (float) g_U.Y (y + 27), (float) g_U.L (19), P.color, P.trim, C.pilot);
			g_text.draw (S, g_U.X (x + 170), g_U.Y (y + 7), g_U.L (22), P.name, C_TXT, 0);
			g_text.draw (S, g_U.X (x + 170), g_U.Y (y + 31), g_U.L (14), TR (HULLS[C.build.hull].name), C_SKY, 0, 0, false);
			if (C.finished && C.finishTime < 900) { fmtTime (C.finishTime, b, sizeof b); g_text.draw (S, g_U.X (x + 560), g_U.Y (y + 14), g_U.L (22), b, C_TXT, 0); }
			if (g_mode == MODE_GP)
			{
				int who = -1; for (int r = 0; r < MAXCRAFT; r++) if (g_racerPilot[r] == C.pilot) who = r;
				if (who >= 0)
				{
					snprintf (b, sizeof b, "+%d", g_lastPts[who]);
					g_text.draw (S, g_U.X (x + 780), g_U.Y (y + 12), g_U.L (24), b, C_PINK, 0, g_U.L (2));
					snprintf (b, sizeof b, "%d", g_points[who]);
					g_text.draw (S, g_U.X (x + 930), g_U.Y (y + 10), g_U.L (28), b, C_TXT, 2);
				}
			}
		}
	hint (S, TR ("A / Enter: continue"));
}
static Particles g_confetti;
static void drawPodium ()
{
	Surf S = screenSurf ();
	S.vgrad (0, 0, g_W, g_H, 0x6FB8F2, 0xFFE3F0);
	// light beams
	for (int k = 0; k < 6; k++)
	{
		float a = sinf (g_stateT * 0.7f + (float) k) * 0.4f, x = 140 + (float) k * 200;
		float p[8] = { (float) g_U.X (x - 20), (float) g_U.Y (720), (float) g_U.X (x + 20), (float) g_U.Y (720), (float) g_U.X (x + 400 * a + 90), (float) g_U.Y (0), (float) g_U.X (x + 400 * a - 90), (float) g_U.Y (0) };
		S.poly (p, 4, k & 1 ? C_PINK : C_SKY, 40);
	}
	char b[96];
	titleText (S, cupName (g_cup), 18, 50);
	static const float SX[3] = { 640, 400, 880 }, SH[3] = { 230, 170, 130 };
	static const unsigned SC[3] = { 0xFFD23F, 0xDDE6FF, 0xFFA45C };
	for (int k = 0; k < 3; k++)
	{
		int who = g_order[k];
		float x = SX[k], h = SH[k] * clampf (g_stateT * 1.5f, 0, 1);
		neonPanel (S, g_U.X (x - 110), g_U.Y (620 - h), g_U.L (220), g_U.L (h + 60), g_U.L (10), SC[k], 220);
		snprintf (b, sizeof b, "%d", k + 1);
		g_text.italic = true;
		g_text.draw (S, g_U.X (x), g_U.Y (630 - h), g_U.L (60), b, SC[k], 1, g_U.L (4));
		g_text.italic = false;
		const Pilot &P = PILOTS[g_racerPilot[who]];
		float jump = fabsf (sinf (g_stateT * 5 + (float) k)) * 30;
		helmet (S, (float) g_U.X (x), (float) g_U.Y (520 - h - jump), (float) g_U.L (52), P.color, P.trim, g_racerPilot[who]);
		g_text.draw (S, g_U.X (x), g_U.Y (586 - h - jump), g_U.L (24), P.name, who < g_players ? C_YELLOW : C_TXT, 1, g_U.L (3));
		snprintf (b, sizeof b, TR ("%d points"), g_points[who]);
		g_text.draw (S, g_U.X (x), g_U.Y (684 - h * 0.4f), g_U.L (20), b, C_TXT, 1, g_U.L (2));
	}
	for (int k = 0; k < g_confetti.n; k++)
	{
		const Particle &q = g_confetti.p[k];
		S.rect (g_U.X (q.p.x), g_U.Y (q.p.y), g_U.L (10), g_U.L (6), q.col);
	}
	int best = 99; for (int p = 0; p < g_players; p++) for (int k = 0; k < MAXCRAFT; k++) if (g_order[k] == p && k < best) best = k;
	if (best < 3) titleText (S, best == 0 ? TR ("GOLD TROPHY!") : best == 1 ? TR ("SILVER TROPHY!") : TR ("BRONZE TROPHY!"), 96, 40, C_YELLOW);
	hint (S, TR ("A / Enter: continue"));
}

// ==== the states' logic =====================================================================================================
static void randomBuild (int p)
{
	unsigned r = clock_us () * 2654435761u + (unsigned) p;
	Rng R (r);
	g_build[p].hull = R.n (NHULL); g_build[p].wing = R.n (NWING); g_build[p].engine = R.n (NENGINE);
	g_build[p].paint = R.n (NPAINT); g_build[p].paint2 = R.n (NPAINT); g_build[p].pattern = R.n (NPATTERN);
}
static void saveBuilds ()
{
	for (int p = 0; p < 4; p++)
	{
		char sec[8]; snprintf (sec, sizeof sec, "p%d", p + 1);
		setInt (sec, "pilot", g_pilot[p]); setInt (sec, "hull", g_build[p].hull); setInt (sec, "wing", g_build[p].wing);
		setInt (sec, "engine", g_build[p].engine); setInt (sec, "paint", g_build[p].paint); setInt (sec, "trim", g_build[p].paint2); setInt (sec, "pattern", g_build[p].pattern);
	}
	writeSave ();
}
static void loadBuilds ()
{
	// (the ships' colours of before 1.0.5 -- not the user's models' -- forgotten once: the defaults again)
	bool fresh = saveInt ("options", "builds", 0) < 2;
	if (fresh) setInt ("options", "builds", 2);
	for (int p = 0; p < 4; p++)
	{
		char sec[8]; snprintf (sec, sizeof sec, "p%d", p + 1);
		g_pilot[p] = clampi (saveInt (sec, "pilot", p), 0, NPILOT - 1);
		Build d = PILOTS[g_pilot[p]].build;
		if (fresh) { g_build[p] = d; continue; }
		g_build[p].hull = clampi (saveInt (sec, "hull", d.hull), 0, NHULL - 1);
		g_build[p].wing = clampi (saveInt (sec, "wing", d.wing), 0, NWING - 1);
		g_build[p].engine = clampi (saveInt (sec, "engine", d.engine), 0, NENGINE - 1);
		g_build[p].paint = clampi (saveInt (sec, "paint", d.paint), 0, NPAINT - 1);
		g_build[p].paint2 = clampi (saveInt (sec, "trim", d.paint2), 0, NPAINT - 1);
		g_build[p].pattern = clampi (saveInt (sec, "pattern", d.pattern), 0, NPATTERN - 1);
	}
	g_musicOn = saveInt ("options", "music", 1) != 0; g_sfxOn = saveInt ("options", "sounds", 1) != 0;
	g_assist = clampi (saveInt ("options", "assist", 1), 0, 2); g_autoAcc = clampi (saveInt ("options", "autoacc", 2), 0, 2);
}
static void saveOptions ()
{
	setInt ("options", "music", g_musicOn); setInt ("options", "sounds", g_sfxOn); setInt ("options", "assist", g_assist); setInt ("options", "autoacc", g_autoAcc);
	writeSave ();
}

static void beginRaces ()
{
	makeRacers ();
	g_raceNo = 0;
	const char *id = g_mode == MODE_GP ? g_cups[g_cup].tracks[0] : g_tracks[g_trackSel].id;
	startRace (id, true);
	setState (ST_RACE);
}
static void toTitle () { g_inRace = false; titleRace (); setState (ST_TITLE); }

// A click: what it hit becomes the choice and a press of the buttons the keyboard would use
static void mouseClick ()
{
	if (!g_clicked) return;
	g_clicked = false;
	if (g_state == ST_TITLE || g_state == ST_RESULTS || g_state == ST_PODIUM || (g_state == ST_RACE && g_race.over)) { g_anyPress |= BT_OK; g_in[0].press |= BT_OK; return; }
	int id = -1, hx = 0, hw = 1;
	for (int i = g_nhits - 1; i >= 0 && id < 0; i--)
	{
		const Hit &H = g_hits[i];
		if (g_clickX >= H.x && g_clickY >= H.y && g_clickX < H.x + H.w && g_clickY < H.y + H.h) { id = H.id; hx = H.x; hw = H.w; }
	}
	if (id < 0) return;
	unsigned p = 0;
	if (id == HIT_HINT) p = BT_OK;
	else if (id >= HIT_TRACK) { int t = id - HIT_TRACK; g_selRow = 1; if (t == g_trackSel) p = BT_OK; else g_trackSel = t; }
	else if (id >= HIT_CUP) { int c = id - HIT_CUP; g_selRow = 1; if (c == g_cup) p = BT_OK; else g_cup = c; }
	else if (id >= HIT_CLASS) { if (classOpen (id - HIT_CLASS)) g_cls = id - HIT_CLASS; }
	else if (id >= HIT_ROW) { g_hangarRow = id - HIT_ROW; p = g_clickX < hx + hw / 2 ? BT_LEFT : BT_RIGHT; }
	else if (id >= HIT_OPTION) { g_optSel = id - HIT_OPTION; p = BT_OK; }
	else { g_menuSel = id - HIT_MENU; p = BT_OK; }
	g_anyPress |= p; g_in[0].press |= p;
}

static void update (float dt)
{
	g_stateT += dt;
	mouseClick ();
	g_sfx.on = g_sfxOn; g_music.on = g_musicOn;
	if (!g_musicOn && g_music.playing) g_music.stop ();
	g_music.update (dt);
	g_sfx.tick ();
	unsigned pr = g_anyPress;
	switch (g_state)
	{
	case ST_TITLE:
		raceUpdate (dt);
		if (g_demo) break;
		if (pr & (BT_OK | BT_PAUSE | BT_ACC)) { setState (ST_MENU); g_sfx.play (1046, 90, SOUND_TRIANGLE, 100); }
		break;
	case ST_MENU:
		raceUpdate (dt);
		if (pr & BT_UP) { g_menuSel = (g_menuSel + 4) % 5; g_sfx.play (880, 40, SOUND_TRIANGLE, 80); }
		if (pr & BT_DOWN) { g_menuSel = (g_menuSel + 1) % 5; g_sfx.play (880, 40, SOUND_TRIANGLE, 80); }
		if (pr & BT_LEFT) { g_players = g_players > 1 ? g_players - 1 : 4; }
		if (pr & BT_RIGHT) { g_players = g_players < 4 ? g_players + 1 : 1; }
		if (pr & BT_BACK) setState (ST_TITLE);
		if (pr & BT_OK)
		{
			g_sfx.play (1318, 90, SOUND_TRIANGLE, 100);
			if (g_menuSel == 4) { g_quitNow = true; break; }
			if (g_menuSel == 3) { g_optSel = 0; setState (ST_OPTIONS); break; }
			g_mode = g_menuSel == 0 ? MODE_GP : g_menuSel == 1 ? MODE_SINGLE : MODE_TT;
			if (g_mode == MODE_TT) g_players = 1;
			g_hangarP = 0; g_hangarRow = 0; setState (ST_HANGAR);
		}
		break;
	case ST_OPTIONS:
		raceUpdate (dt);
		if (pr & BT_UP) g_optSel = (g_optSel + 4) % 5;
		if (pr & BT_DOWN) g_optSel = (g_optSel + 1) % 5;
		if (pr & (BT_LEFT | BT_RIGHT | BT_OK))
		{
			int d = (pr & BT_LEFT) ? -1 : 1;
			if (g_optSel == 0) { g_musicOn = !g_musicOn; if (g_musicOn) g_music.start (0, 3); }
			if (g_optSel == 1) g_sfxOn = !g_sfxOn;
			if (g_optSel == 2) g_assist = (g_assist + d + 3) % 3;
			if (g_optSel == 3) g_autoAcc = (g_autoAcc + d + 3) % 3;
			if (g_optSel == 4 && (pr & BT_OK)) { saveOptions (); setState (ST_MENU); }
		}
		if (pr & BT_BACK) { saveOptions (); setState (ST_MENU); }
		break;
	case ST_HANGAR:
	{
		g_hangarSpin += dt * 0.6f;
		// the player in the hangar uses its own pad (and the keyboard for the first)
		unsigned hp = g_in[g_hangarP].press | (g_hangarP == 0 ? 0 : g_in[0].press & 0);
		if (g_hangarP == 0) hp |= g_in[0].press;
		Build &B = g_build[g_hangarP];
		if (hp & BT_UP) g_hangarRow = (g_hangarRow + HR_COUNT - 1) % HR_COUNT;
		if (hp & BT_DOWN) g_hangarRow = (g_hangarRow + 1) % HR_COUNT;
		int d = (hp & BT_LEFT) ? -1 : (hp & BT_RIGHT) ? 1 : 0;
		if (d)
		{
			g_sfx.play (d > 0 ? 988 : 784, 50, SOUND_TRIANGLE, 90);
			switch (g_hangarRow)
			{
			case HR_PILOT:
			{
				int p = g_pilot[g_hangarP];
				for (int tries = 0; tries < NPILOT; tries++)
				{
					p = (p + d + NPILOT) % NPILOT;
					bool taken = false; for (int q = 0; q < g_hangarP; q++) if (g_pilot[q] == p) taken = true;
					if (!taken) break;
				}
				g_pilot[g_hangarP] = p;
				break;
			}
			case HR_HULL: B.hull = (B.hull + d + NHULL) % NHULL; break;
			case HR_WING: B.wing = (B.wing + d + NWING) % NWING; break;
			case HR_ENGINE: B.engine = (B.engine + d + NENGINE) % NENGINE; break;
			case HR_PAINT: B.paint = (B.paint + d + NPAINT) % NPAINT; break;
			case HR_TRIM: B.paint2 = (B.paint2 + d + NPAINT) % NPAINT; break;
			case HR_PATTERN: B.pattern = (B.pattern + d + NPATTERN) % NPATTERN; break;
			}
		}
		if (hp & BT_RANDOM) { randomBuild (g_hangarP); g_sfx.play (1568, 60, SOUND_TRIANGLE, 90); }
		if (hp & BT_OK)
		{
			g_sfx.play (1318, 90, SOUND_TRIANGLE, 100);
			if (g_hangarP + 1 < g_players) { g_hangarP++; g_hangarRow = 0; g_stateT = 0; }
			else { saveBuilds (); g_selRow = 1; setState (ST_SELECT); }
		}
		if (hp & BT_BACK) { if (g_hangarP > 0) g_hangarP--; else setState (ST_MENU); }
		break;
	}
	case ST_SELECT:
	{
		raceUpdate (dt);
		int rows = g_mode == MODE_SINGLE ? 3 : 2;
		if (pr & BT_UP) g_selRow = (g_selRow + rows - 1) % rows;
		if (pr & BT_DOWN) g_selRow = (g_selRow + 1) % rows;
		int d = (pr & BT_LEFT) ? -1 : (pr & BT_RIGHT) ? 1 : 0;
		if (d)
		{
			g_sfx.play (880, 40, SOUND_TRIANGLE, 80);
			if (g_selRow == 0) { int k = g_cls; do k = (k + d + NCLASS) % NCLASS; while (!classOpen (k)); g_cls = k; }
			else if (g_selRow == 1 && g_mode == MODE_GP) g_cup = (g_cup + d + g_ncups) % (g_ncups ? g_ncups : 1);
			else if (g_selRow == 1) g_trackSel = (g_trackSel + d + g_ntracks) % (g_ntracks ? g_ntracks : 1);
			else if (g_selRow == 2) g_laps = clampi (g_laps + d, 1, 5);
		}
		if (pr & BT_OK)
		{
			if (g_mode == MODE_GP && !cupOpen (g_cup)) { g_sfx.play (200, 150, SOUND_SQUARE, 90); break; }
			g_sfx.play (1568, 120, SOUND_TRIANGLE, 110);
			beginRaces ();
		}
		if (pr & BT_BACK) { g_hangarP = 0; setState (ST_HANGAR); }
		break;
	}
	case ST_RACE:
		if (g_pause.on) break;
		if (pr & BT_PAUSE) { g_pause.open (); if (g_pause.on) break; }
		raceUpdate (dt);
		if (g_race.over && (pr & (BT_OK | BT_ACC)) && g_stateT > 1)
		{
			g_sfx.hum (0, false);
			scoreRace ();
			setState (ST_RESULTS);
		}
		break;
	case ST_RESULTS:
		g_race.time += 0;		// (the picture stays)
		if ((pr & (BT_OK | BT_ACC)) && g_stateT > 0.8f)
		{
			if (g_mode == MODE_GP && g_raceNo < 3)
			{
				g_raceNo++;
				startRace (g_cups[g_cup].tracks[g_raceNo], true);
				setState (ST_RACE);
			}
			else if (g_mode == MODE_GP)
			{
				// the trophy for the best human
				int best = 99; for (int k = 0; k < MAXCRAFT; k++) if (g_order[k] < g_players && k < best) best = k;
				if (best < 3)
				{
					char key[24]; snprintf (key, sizeof key, "cup%d_%d", g_cup, g_cls);
					int had = saveInt ("trophies", key, 0);
					if (!had || best + 1 < had) setInt ("trophies", key, best + 1);
					if (g_cup == 3 && best == 0) setInt ("unlocks", "mirror", 1);
					writeSave ();
				}
				g_confetti.clear ();
				g_music.start (8, 5);
				setState (ST_PODIUM);
			}
			else { toTitle (); setState (ST_SELECT); g_selRow = 1; }
		}
		break;
	case ST_PODIUM:
	{
		Rng r ((unsigned) (g_stateT * 1000) + 7);
		if (g_confetti.n < 300) for (int k = 0; k < 3; k++) g_confetti.add (V3 (r.range (0, 1280), -10, 0), V3 (r.range (-40, 40), r.range (60, 160), 0), 6, 1, hueColor (r.f ()), 0);
		for (int k = 0; k < g_confetti.n; k++) { Particle &q = g_confetti.p[k]; q.p += q.v * dt; q.p.x += sinf (q.life * 4) * 30 * dt; if (q.p.y > 730) q.life = 0; }
		g_confetti.step (0);
		for (int k = 0; k < g_confetti.n; k++) g_confetti.p[k].life -= dt;
		if ((pr & (BT_OK | BT_ACC)) && g_stateT > 1.5f) { toTitle (); g_selRow = 1; setState (ST_SELECT); }
		break;
	}
	}
}


static void drawAll ()
{
	g_U.fit (g_W, g_H);
	g_nhits = 0;
	switch (g_state)
	{
	case ST_HANGAR: drawHangar (); break;
	case ST_PODIUM: drawPodium (); break;
	default:
		draw3dViews ();
		if (g_state == ST_RACE) for (int p = 0; p < g_players; p++) hud (p);
		else if (g_state == ST_TITLE) drawTitle ();
		else if (g_state == ST_MENU) drawMenu ();
		else if (g_state == ST_OPTIONS) drawOptions ();
		else if (g_state == ST_SELECT) drawSelect ();
		else if (g_state == ST_RESULTS) drawResults ();
	}
	if (g_showFps)
	{
		Surf S = screenSurf (); char b[48];
		snprintf (b, sizeof b, "%d fps %s", (int) (g_fps + 0.5f), g_gpu ? "GPU" : "CPU");
		g_text.draw (S, g_W - 8, g_H - 30, 16, b, C_TXT, 2, 2);
	}
	// the pause menu, or its || button (in a real race)
	ga::Img im = ga::window ();
	float k = ga::Pause::scaleFor (g_W, g_H);
	g_pause.k = k;
	bool race = g_state == ST_RACE && g_inRace;
	g_pause.bx = race ? g_W - g_pause.S (28) - 12 : -1; g_pause.by = 12;
	g_pause.draw (im, k);
}
// the pause menu: in a real race only (Esc, Start go back in the menus); losing the focus opens it while the race goes on
static bool pauseStop (bool lost)
{
	if (g_state != ST_RACE || !g_inRace || (lost && g_race.over)) return false;
	g_sfx.hum (0, false);
	memset (g_hid, 0, sizeof g_hid); memset (g_char, 0, sizeof g_char);
	return true;
}
static void pauseGo () { memset (g_hid, 0, sizeof g_hid); memset (g_char, 0, sizeof g_char); }
static void pauseNew () { startRace (g_curTrack, true); setState (ST_RACE); }
static void pauseBlip () { g_sfx.play (880, 25, AKW_SINE, 70); }

// ==== the window ===========================================================================================================
enum { KEY_F12 = 0x45 };
enum { C_PAUSE = 1, C_QUIT, C_FPS, C_MUSIC, C_SOUNDS };

static void publishMenu ()
{
	static char b[600]; int n = 0;
	auto M = [&] (const char *t) { n += snprintf (b + n, sizeof b - n, "M%s\n", t); };
	auto I = [&] (int id, const char *t, const char *key) { n += snprintf (b + n, sizeof b - n, "I%d\t%s%s%s\n", id, t, key[0] ? "\t" : "", key); };
	M (TR ("Game")); I (C_PAUSE, TR ("Pause"), ""); n += snprintf (b + n, sizeof b - n, "-\n"); I (C_QUIT, TR ("Quit"), "Ctrl+Q");
	M (TR ("View")); I (C_FPS, TR ("Show Frame Rate"), "F12");
	M (TR ("Sound")); I (C_MUSIC, TR ("Music On / Off"), ""); I (C_SOUNDS, TR ("Sounds On / Off"), "");
	ui_menu ((long) b);
}


static void command (int id)
{
	switch (id)
	{
	case C_PAUSE: g_pause.toggle (); break;
	case C_QUIT: g_quitNow = true; break;
	case C_FPS: g_showFps = !g_showFps; break;
	case C_MUSIC: g_musicOn = !g_musicOn; if (g_musicOn) g_music.start (0, 3); saveOptions (); break;
	case C_SOUNDS: g_sfxOn = !g_sfxOn; saveOptions (); break;
	}
}

static void oneEvent (long t)
{
	if (t == WEV_CLOSE) { g_quitNow = true; return; }
	if (g_pause.event (t, ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y))) return;
	if (t == WEV_MENU) { command (ui_ev_field (&ui, EV_CODE)); return; }
	if (t == WEV_PAD) { g_padTap[g_pause.pad] |= g_pause.edge; return; }
	if (t == WEV_FOCUS && ui_ev_field (&ui, EV_KIND) == 0) { memset (g_hid, 0, sizeof g_hid); memset (g_char, 0, sizeof g_char); return; }
	if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1) { g_clickX = ui_ev_field (&ui, EV_X); g_clickY = ui_ev_field (&ui, EV_Y); g_clicked = true; return; }
	if (t == WEV_WHEEL) { g_mousePress |= ui_ev_field (&ui, EV_Y) > 0 ? BT_UP : BT_DOWN; return; }
	if (t != WEV_KEY) return;
	int hid = ui_ev_field (&ui, EV_CODE) & 255, ch = ui_ev_field (&ui, EV_CHAR);
	if (ui_ev_field (&ui, EV_KIND) == 0) { g_hidUp[hid] = true; return; }
	long id = ui_menu_key (0);
	if (id > 0) { command ((int) id); return; }
	if (hid == KEY_F12) { g_showFps = !g_showFps; return; }
	g_hid[hid] = true;
	if (ch >= 'A' && ch <= 'Z') ch += 32;
	if (ch > 0 && ch < 128) { g_hidChar[hid] = (unsigned char) ch; g_char[ch] = 1; }
}
static void pumpEvents () { long t; while ((t = ui_event (0)) > 0) oneEvent (t); }

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	audiokit_kit_open ();
	if (!gpukit_kit_open ()) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, APPDIR "lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	g_fr = !strcmp ((const char *) ui_lang (), "fr");
	theme_load ();
	char start[32] = "", raceId[36] = "";
	for (int i = 1; i < argc; i++)
	{
		const char *p = argv[i];
		if (!strcmp (p, "demo")) g_demo = true;
		else if (!strcmp (p, "auto")) g_auto = true;
		else if (!strcmp (p, "fps")) g_showFps = true;
		else if (!strncmp (p, "players=", 8)) g_players = clampi (atoi (p + 8), 1, 4);
		else if (!strncmp (p, "seed=", 5)) g_seed = (unsigned) atoi (p + 5);
		else if (!strncmp (p, "race=", 5)) snprintf (raceId, sizeof raceId, "%s", p + 5);
		else if (!strncmp (p, "screen=", 7)) snprintf (start, sizeof start, "%s", p + 7);
	}
	g_fileReader = read_file;
	mkdir ("SD0:/home", 0777); mkdir (HOMEDIR, 0777);
	g_save.load (SAVE);
	loadBuilds ();
	loadCatalogue ();
	snprintf (g_scene.lib.dir, sizeof g_scene.lib.dir, APPDIR "models");
	g_tx.make ();
	gpuInit ();
	if (ga::fullscreen ("Cosmo Zoom!") < 0) return 1;
	g_pause.onPause = pauseStop; g_pause.onResume = pauseGo; g_pause.onRestart = pauseNew; g_pause.onSound = pauseBlip;
	publishMenu ();
	// (the models, the world: seconds; the window says so meanwhile)
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	{ const char *t = TR ("Loading..."); ui_text ((long) t, 24, 48, jet (J_TEXTE1), 20, (long) ui.font_b, 0); }
	ui_update_all ();
	sound_start ();
	if (!g_ntracks) { fprintf (stderr, "cosmozoom: no circuit (cups.ini)\n"); }
	titleRace ();
	// the test entries
	if (raceId[0]) { makeRacers (); g_mode = MODE_SINGLE; g_trackSel = trackIndex (raceId) < 0 ? 0 : trackIndex (raceId); startRace (raceId, true); setState (ST_RACE); }
	else if (!strcmp (start, "menu")) setState (ST_MENU);
	else if (!strcmp (start, "hangar")) setState (ST_HANGAR);
	else if (!strcmp (start, "cups")) { g_selRow = 1; setState (ST_SELECT); }
	else if (!strcmp (start, "options")) setState (ST_OPTIONS);
	unsigned last = clock_us (), fpsT = last; int frames = 0;
	while (!g_quitNow && !g_pause.quit)
	{
		pumpEvents ();
		unsigned now = clock_us ();
		float dt = (float) (now - last) / 1e6f; last = now;
		if (dt > 0.1f) dt = 0.1f;
		readInput ();
		update (dt);
		g_px = ui.surf; g_W = (int) ui.w; g_H = (int) ui.h; g_stride = g_W;
		drawAll ();
		ui_update_all ();
		frames++;
		if (now - fpsT > 1000000) { g_fps = (float) frames * 1e6f / (float) (now - fpsT); frames = 0; fpsT = now; }
		if (g_demo && g_stateT > 600) break;
		// the next picture when Elegant has taken this one (the window's pixels are drawn in place)
		if (ui_frame_request ())
			for (unsigned t0 = clock_us (); !g_quitNow && !g_pause.quit && clock_us () - t0 < 200000; )
			{
				long t = ui_event (10);
				if (t == WEV_FRAME) break;
				if (t > 0) oneEvent (t);
			}
	}
	g_sfx.silence (); g_music.stop ();
	sound_stop ();
	return 0;
}
