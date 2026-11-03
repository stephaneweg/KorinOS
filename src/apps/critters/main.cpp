//
// critters -- le Critters d'Onyx, facon Lemmings : mener un flot de petites betes d'une trappe a une sortie a travers un
// terrain de pixels, en donnant un role a certaines (grimpeur, planeur, bloqueur, batisseur, creuseur, artificier).
// Douze niveaux (six d'entrainement, six d'expedition, chacun ouvert par le precedent) et tout niveau ecrit dans un
// fichier .level. Le coeur (terrain, level, world, solution, progress) est celui d'Onyx, le lecteur de texte mis a part :
// entiers, deterministe. Habille ici : terrain relief, ciel fait pour chaque niveau, eau et lave qui ondulent.
//
// L'image logique fait 800 x 448 : la zone de jeu (le terrain x2, defilant), la ligne d'etat, la barre des roles ;
// elle est mise a l'echelle de la fenetre. Touches : 1-6 un role, Tab / Entree une bete, P / Espace pause, F accelere,
// N tout faire sauter, - / + le debit, Gauche / Droite defiler, Echap la carte de pause, M le son, F1 l'aide. A la
// souris et au doigt : un role, puis une bete ; glisser sur le terrain ou la mini-carte defile.
// Fichiers : SD0:/apps/critters.app/levels, SD0:/home/critters (les niveaux du joueur, progress.ini).
//
#include "../../inc/gameart.h"
#include "terrain.h"
#include "level.h"
#include "world.h"
#include "solution.h"
#include "progress.h"
#include <dirent.h>
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-en.txt : le source est en francais)

using namespace ga;
using namespace critters;

namespace ga { struct ui_vars ui; }

#define APPDIR		"SD0:/apps/critters.app/"
#define LEVELDIR	APPDIR "levels"
#define HOMEDIR		"SD0:/home/critters"
#define PROGRESS	HOMEDIR "/progress.ini"

#include "draw.h"

enum { K_TAB = 0x2B, K_ENTER = 0x28, K_ESC = 0x29, K_SPACE = 0x2C, K_F1 = 0x3A, K_HOME = 0x4A, K_END = 0x4D, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52 };
enum { PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8, PAD_A = 16, PAD_B = 32, PAD_X = 64, PAD_Y = 128, PAD_L = 256, PAD_R = 512, PAD_START = 8192 };
enum { WW = 800, WH = 448, PLAYH = 320, STATUSY = 320, STATUSH = 24, BARY = 344, BARH = 104, CAPW = 112 };
static const unsigned AMBERC = 0xFFB030, GREENC = 0x60E080, REDC = 0xFF5A4A;

// ---- les niveaux ----------------------------------------------------------------------------------------------------
struct Entry
{
	char path[256], file[96], section[128];
	int group;				// 0 entrainement, 1 expedition, 2 ceux du joueur
	int chain, num;
	bool ok; char err[300];
	char name[2][NAMEC * 4 + 4], hint[2][HINTL];
	int w, h, count, save, timeSec, rate, roles[NROLES];
	uint32_t *thumb; int tw, th;
};
enum { MAXENT = 48 };
static Entry *g_ent[MAXENT]; static int g_nent, g_sel;
static const char *g_chain[MAXENT]; static int g_nchain;
static gkv *g_prog;
static Level *g_scan; static Terrain *g_scanT;
static Snd g_snd;

static bool locked (int i) { const Entry &e = *g_ent[i]; return e.chain > 0 && !progress_open (g_prog, g_chain, g_nchain, e.chain); }
static void save_progress () { mkdir ("SD0:/home", 0777); mkdir (HOMEDIR, 0777); gkv_save (g_prog, PROGRESS, "Critters : niveaux reussis, records, reglages (ecrit par le jeu)"); }
static bool ends_level (const char *name) { int l = (int) strlen (name); return l > 6 && !strcasecmp (name + l - 6, ".level"); }
static bool read_level (const char *path, Level &lv, LoadError &e)
{
	memset (&e, 0, sizeof e);
	long n = 0; char *text = gkv_read_file (path, &n, MAXFILE);
	if (!text) { load_error_file (e, n < 0); return false; }
	bool ok = load_level (text, lv, e);
	free (text);
	return ok;
}
static Entry *load_entry (const char *path, int group)
{
	Entry *e = (Entry *) calloc (1, sizeof (Entry));
	snprintf (e->path, sizeof e->path, "%s", path);
	snprintf (e->file, sizeof e->file, "%s", gkv_basename (path));
	e->group = group; e->chain = -1;
	progress_section (path, group < 2, e->section, sizeof e->section);
	LoadError err;
	if (read_level (path, *g_scan, err))
	{
		const Level &l = *g_scan;
		e->ok = true;
		for (int k = 0; k < 2; k++) { snprintf (e->name[k], sizeof e->name[k], "%s", l.name.get (k)); snprintf (e->hint[k], sizeof e->hint[k], "%s", l.hint.get (k)); }
		e->w = l.w; e->h = l.h; e->count = l.count; e->save = l.save; e->timeSec = l.timeSec; e->rate = l.rate;
		memcpy (e->roles, l.roles, sizeof e->roles);
	}
	else if (err.line > 0) snprintf (e->err, sizeof e->err, TR ("ligne %d : %s"), err.line, err.reason);
	else snprintf (e->err, sizeof e->err, "%s", err.reason);
	return e;
}
static void free_entries () { for (int i = 0; i < g_nent; i++) { free (g_ent[i]->thumb); free (g_ent[i]); } g_nent = 0; g_nchain = 0; }
static int find_entry (const char *path) { for (int i = 0; i < g_nent; i++) if (!strcasecmp (g_ent[i]->path, path)) return i; return -1; }
static void scan_dir (const char *dir, bool shipped)
{
	enum { MAXN = 40 };
	static char names[MAXN][128]; int n = 0;
	DIR *d = opendir (dir);
	if (!d) return;
	struct dirent *de;
	while ((de = readdir (d)) != 0 && n < MAXN)
		if (ends_level (de->d_name)) snprintf (names[n++], sizeof names[0], "%s", de->d_name);
	closedir (d);
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--)
	{ char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	for (int pass = 0; pass < (shipped ? 2 : 1); pass++)
		for (int i = 0; i < n && g_nent < MAXENT; i++)
		{
			bool tr = !strncmp (names[i], "training-", 9);
			if (shipped && (pass == 0) != tr) continue;
			char p[256]; if (snprintf (p, sizeof p, "%s/%s", dir, names[i]) >= (int) sizeof p) continue;
			if (find_entry (p) < 0) g_ent[g_nent++] = load_entry (p, shipped ? pass : 2);
		}
}
static void load_entries (const char *keep)
{
	char keepPath[256]; snprintf (keepPath, sizeof keepPath, "%s", keep ? keep : "");
	free_entries ();
	scan_dir (LEVELDIR, true);
	scan_dir (HOMEDIR, false);
	int num[3] = { 0, 0, 0 };
	for (int i = 0; i < g_nent; i++)
	{
		Entry &e = *g_ent[i];
		e.num = ++num[e.group];
		if (e.group < 2) { e.chain = g_nchain; g_chain[g_nchain++] = e.section; }
	}
	g_sel = 0;
	int k = keepPath[0] ? find_entry (keepPath) : -1;
	if (k < 0)
	{
		const char *last = progress_setting (g_prog, "last", "");
		for (int i = 0; i < g_nent && last[0]; i++) if (!strcmp (g_ent[i]->section, last)) { k = i; break; }
	}
	if (k < 0) for (int i = 0; i < g_nent; i++) if (g_ent[i]->ok && !locked (i) && !progress_get (g_prog, g_ent[i]->section).solved) { k = i; break; }
	if (k >= 0) g_sel = k;
}
static void make_thumb (Entry &e, int pw, int ph)
{
	LoadError err;
	if (!read_level (e.path, *g_scan, err) || !build_terrain (*g_scan, *g_scanT)) return;
	const Terrain &t = *g_scanT;
	e.thumb = (uint32_t *) malloc ((size_t) pw * (size_t) ph * 4);
	if (!e.thumb) return;
	e.tw = pw; e.th = ph;
	for (int y = 0; y < ph; y++)
		for (int x = 0; x < pw; x++)
		{
			int lx = x * t.w / pw, ly = y * t.h / ph;
			e.thumb[y * pw + x] = t.col[ly * t.w + lx];
		}
}

static void play_sound (int kind)
{
	switch (kind)
	{
	case E_HATCH_OPEN: g_snd.later (0, 330, 90, SOUND_TRIANGLE, 90); g_snd.later (90, 440, 140, SOUND_TRIANGLE, 90); break;
	case E_OUT: g_snd.note (660, 25, SOUND_TRIANGLE, 50); break;
	case E_ROLE: g_snd.note (880, 40, SOUND_SQUARE, 60); break;
	case E_REFUSE: g_snd.note (160, 90, SOUND_SQUARE, 70); break;
	case E_BRICK_WARN: g_snd.note (1047, 30, SOUND_TRIANGLE, 60); break;
	case E_SPLAT: g_snd.note (110, 120, SOUND_NOISE, 80); break;
	case E_DROWN: g_snd.note (220, 160, SOUND_TRIANGLE, 80); break;
	case E_BURN: g_snd.note (180, 140, SOUND_NOISE, 80); break;
	case E_TICK: g_snd.note (1320, 20, SOUND_SQUARE, 40); break;
	case E_BURST: g_snd.note (90, 220, SOUND_NOISE, 110); break;
	case E_SAVED: g_snd.later (0, 1175, 50, SOUND_TRIANGLE, 70); g_snd.later (60, 1568, 80, SOUND_TRIANGLE, 70); break;
	case E_VOID: g_snd.note (150, 100, SOUND_TRIANGLE, 60); break;
	}
}
static void blip () { g_snd.note (196, 70, SOUND_SQUARE, 70); }

static const char *state_word (const Critter &k)
{
	switch (k.state)
	{
	case S_FALL: return TR (k.flags & F_FLOATER ? "En vol plan\xC3\xA9" : "En chute");
	case S_WALK: return TR ("Marcheuse");
	case S_CLIMB: return TR ("Grimpe");
	case S_BLOCK: return TR ("Bloqueur");
	case S_BUILD: return TR ("B\xC3\xA2tisseur");
	case S_SHRUG: return TR ("Plus de briques");
	case S_DIG: return TR ("Creuseur");
	case S_EXIT: return TR ("Sort");
	default: return "";
	}
}
static const char *refusal_word (int r, int role)
{
	switch (r)
	{
	case NO_COUNT: return TR ("il n'en reste plus");
	case LEAVING: return TR ("elle sort");
	case ALREADY: return TR ("l'a d\xC3\xA9j\xC3\xA0");
	case IS_BLOCKER: return TR ("un bloqueur reste bloqueur");
	case COUNTING_DOWN: return TR ("compte \xC3\xA0 rebours lanc\xC3\xA9");
	case NOT_ON_GROUND: return TR (role == R_BLOCKER ? "ne peut pas bloquer maintenant" : role == R_BUILDER ? "ne peut pas b\xC3\xA2tir maintenant" : "ne peut pas creuser maintenant");
	default: return "";
	}
}

// ---- l'etat ------------------------------------------------------------------------------------------------------------
enum { M_PICKER, M_PLAY };
enum { O_NONE, O_CARD, O_MENU, O_NUKE, O_END, O_HELP };
enum { B_NONE, B_RESUME, B_RESTART, B_BACK, B_NUKE, B_CANCEL, B_RETRY, B_NEXT, B_LEVELS, B_PLAYIT, B_HELPIT, B_PAUSE, B_FAST, B_NUKEASK, B_MINUS, B_PLUS, B_ROLE0 = 40, B_ROW0 = 100 };
static Level *lv; static World *w; static int g_cur = -1;
static int g_mode = M_PICKER, g_overlay = O_NONE, g_prevOverlay;
static int g_vx, g_mx = -1, g_my = -1;
static Clock g_clk; static bool g_userPaused;
static int g_role = -1, g_focusC = -1; static bool g_kbFocus;
static unsigned g_hatchAt, g_endAt; static bool g_ended;
static int g_endSaved, g_endTime; static bool g_newBest, g_hasNext; static int g_nextEntry = -1;
static int g_refuseWho = -1; static unsigned g_refuseUntil;
static int g_miniStep;
static bool g_quit, g_pickDirty = true;
static unsigned g_tick; static int g_listTop;
static Layer g_fb, g_pick;
static Sparks g_sp;				// (en points logiques du niveau x 2)
static int g_hot, g_dragX = -1, g_dragVx; static bool g_dragged, g_miniDrag;
static unsigned g_pad; static unsigned char g_held[256];
static uint32_t g_mini[172 * 64];
static int g_dx0, g_dy0, g_dw = WW, g_dh = WH;		// l'image dans la fenetre

static int ox () { return lv->w * 2 < WW ? (WW - lv->w * 2) / 2 : 0; }
static int SX (int x) { return ox () + (x - g_vx) * 2 + 1; }
static int SY (int y) { return (y + 1) * 2; }
static int max_vx () { int m = lv->w - WW / 2; return m > 0 ? m : 0; }
static void clamp_vx () { if (g_vx > max_vx ()) g_vx = max_vx (); if (g_vx < 0) g_vx = 0; }
static bool running () { return g_mode == M_PLAY && g_overlay == O_NONE && !g_userPaused && g_hatchAt && w->result == PLAYING; }
static bool modal () { return g_overlay != O_NONE; }
static Entry &E () { return *g_ent[g_cur]; }

struct Btn { int x, y, w, h, id; };
enum { MAXBTN = 96 };
static Btn g_btn[MAXBTN]; static int g_nbtn;
static void add_btn (int x, int y, int bw, int h, int id) { if (g_nbtn < MAXBTN) { Btn &b = g_btn[g_nbtn++]; b.x = x; b.y = y; b.w = bw; b.h = h; b.id = id; } }
static int hit_btn (int x, int y) { for (int i = g_nbtn - 1; i >= 0; i--) { const Btn &b = g_btn[i]; if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return b.id; } return 0; }
static void button (Img &im, int x, int y, int bw, int h, int id, const char *s, bool accent = false, int px = 13)
{
	add_btn (x, y, bw, h, id);
	bool hot = g_hot == id;
	unsigned top = accent ? 0x48B060 : 0x4A4238, bot = accent ? 0x2E8042 : 0x302A24;
	if (hot) { top = shade (top, 340); bot = shade (bot, 340); }
	im.rbox (x, y, bw, h, 8, top, bot, 256, accent ? 0xA8E8B8 : 0x8C7E6E);
	textCS (x + bw / 2, y + h / 2 + px * 36 / 100, TR (s), 0xFFFFFF, px);
}
static void go_picker ()
{
	char keep[256] = "";
	if (g_cur >= 0 && g_cur < g_nent) snprintf (keep, sizeof keep, "%s", g_ent[g_cur]->path);
	g_mode = M_PICKER; g_cur = -1; g_overlay = O_NONE; g_pickDirty = true;
	load_entries (keep);
}
static void set_overlay (int o) { g_overlay = o; g_clk.acc3 = 0; }
static void open_hatch (bool sound) { if (!g_hatchAt) { g_hatchAt = now_ms () | 1; if (sound) play_sound (E_HATCH_OPEN); } }
static void start_playing () { set_overlay (O_NONE); open_hatch (true); }
static void start_level (int i)
{
	LoadError err;
	if (!read_level (g_ent[i]->path, *lv, err) || !w->reset (*lv)) { blip (); return; }
	paint_labels (*lv, w->t);
	w->t.take_dirty ();
	g_brickCol = lv->brick;
	g_cur = i; g_mode = M_PLAY; g_overlay = O_CARD;
	g_vx = lv->start >= 0 ? lv->start : lv->hatch[0].at.x - WW / 4;
	clamp_vx ();
	g_clk = Clock (); g_userPaused = false; g_role = -1; g_focusC = -1; g_kbFocus = false;
	g_hatchAt = 0; g_endAt = 0; g_ended = false; g_newBest = false; g_hasNext = false; g_nextEntry = -1;
	g_refuseWho = -1; g_miniStep = -100; g_sp.clear ();
	for (int r = 0; r < NBUILT; r++) if (w->roles[r] > 0) { g_role = r; break; }
	free (g_lit); g_lit = (uint32_t *) malloc ((size_t) lv->w * (size_t) lv->h * 4);
	if (g_lit) relight (w->t, 0, 0, lv->w - 1, lv->h - 1);
	make_sky (*lv, WW / 2);
	progress_set_setting (g_prog, "last", g_ent[i]->section); save_progress ();
}
static void on_end (bool live)
{
	if (g_ended) return;
	g_ended = true; g_endAt = now_ms () | 1;
	g_endSaved = w->saved; g_endTime = w->endStep / STEPS_PER_SEC;
	bool won = w->result == WON;
	if (won)
	{
		g_newBest = progress_won (g_prog, E ().section, w->saved, g_endTime);
		save_progress ();
		int c = E ().chain;
		if (c >= 0) for (int k = 0; k < g_nent; k++) if (g_ent[k]->chain == c + 1 && g_ent[k]->ok) { g_nextEntry = k; break; }
		g_hasNext = g_nextEntry >= 0;
	}
	if (live) { if (won) g_snd.win (); else g_snd.lose (); }
}
static void events ()
{
	int sounds = 0;
	unsigned earth = lv->nshape ? lv->shape[0].colour : 0x8A5A34;
	for (int i = 0; i < w->nev; i++)
	{
		const Event &e = w->ev[i];
		float x = (float) (e.x * 2), y = (float) (e.y * 2);
		if (e.kind == E_BURST)
		{
			g_sp.burst (x, y - 8, earth, 16, 2.6f, 16, 0.09f, 1.8f); g_sp.burst (x, y - 8, 0xFFE060, 10, 3.2f, 12, 0.05f, 1.4f); g_sp.burst (x, y - 8, 0xFF8A30, 10, 2.4f, 14, 0.05f, 1.6f);
		}
		else if (e.kind == E_SAVED) g_sp.burst (x, y - 12, 0x80FFE0, 8, 1.2f, 16, -0.02f, 1.1f);
		else if (e.kind == E_SPLAT) g_sp.burst (x, y - 2, 0xFFC24A, 8, 1.4f, 12, 0.08f, 1.3f);
		else if (e.kind == E_DROWN) g_sp.burst (x, y - 2, 0xBFE4FF, 10, 1.3f, 14, 0.07f, 1.3f);
		else if (e.kind == E_BURN) g_sp.burst (x, y - 2, 0xFF8030, 12, 1.5f, 14, -0.03f, 1.3f);
		if (e.kind == E_END) { on_end (true); continue; }
		if (e.kind == E_HATCH_OPEN) continue;
		if (sounds < 3) { play_sound (e.kind); sounds++; }
	}
}
static void give (int who)
{
	if (who < 0) return;
	if (g_userPaused || !running ()) { blip (); return; }
	if (g_role < 0) { blip (); return; }
	int r = w->assign (who, g_role);
	play_sound (r == OK ? E_ROLE : E_REFUSE);
	if (r != OK) { g_refuseWho = who; g_refuseUntil = now_ms () + 400; }
}
static void choose_role (int r) { if (g_mode != M_PLAY) return; if (r < 0 || r >= NBUILT || w->roles[r] <= 0) { blip (); return; } g_role = r; }
static int under ()
{
	if (g_mode != M_PLAY || g_mx < ox () || g_my < 0 || g_my >= PLAYH) return -1;
	return w->pick ((g_mx - ox ()) / 2 + g_vx, g_my / 2, g_role);
}
static int target () { return g_kbFocus ? g_focusC : under (); }
static bool in_view (int i) { int sx = SX (w->c[i].x); return w->alive (i) && sx >= 0 && sx < WW; }
static void tab_focus (bool back)
{
	int list[MAXCRIT], n = 0;
	for (int i = 0; i < w->nout; i++) if (in_view (i) && w->c[i].state != S_EXIT) list[n++] = i;
	for (int a = 1; a < n; a++) for (int b = a; b > 0 && w->c[list[b - 1]].x > w->c[list[b]].x; b--) { int t = list[b]; list[b] = list[b - 1]; list[b - 1] = t; }
	if (!n) { g_focusC = -1; g_kbFocus = false; return; }
	int at = -1; for (int k = 0; k < n; k++) if (list[k] == g_focusC) at = k;
	at = at < 0 ? (back ? n - 1 : 0) : (at + (back ? n - 1 : 1)) % n;
	g_focusC = list[at]; g_kbFocus = true;
}
static void toggle_pause () { if (g_mode == M_PLAY && !modal () && g_hatchAt) g_userPaused = !g_userPaused; }
static void toggle_fast () { if (g_mode == M_PLAY && !modal ()) g_clk.fast = !g_clk.fast; }
static void ask_nuke ()
{
	if (g_mode != M_PLAY || modal () || w->result != PLAYING) return;
	if (w->nuking || (w->inPlay () == 0 && w->nout >= lv->count) || !g_hatchAt) { blip (); return; }
	set_overlay (O_NUKE);
}
static void nuke_now () { if (g_overlay == O_NUKE) { set_overlay (O_NONE); w->nuke (); } }
static void set_rate (int d) { if (g_mode == M_PLAY && w->result == PLAYING) w->set_rate (w->rate + d); }
static void help () { if (g_mode == M_PLAY && g_overlay != O_HELP) { g_prevOverlay = g_overlay; set_overlay (O_HELP); } else if (g_mode != M_PLAY) { g_overlay = g_overlay == O_HELP ? O_NONE : O_HELP; } }
static void toggle_sound ()
{
	g_snd.toggle ();
	progress_set_setting (g_prog, "sound", g_snd.mute ? "0" : "1");
	save_progress ();
	ui_menu_state (7, g_snd.mute ? 0 : UMS_CHECKED);
}

static void step_game (unsigned dt)
{
	if (g_mode != M_PLAY || g_cur < 0) return;
	if (!modal ())
	{
		int sp = 4;
		if (g_held[K_LEFT] || (g_pad & PAD_LEFT)) g_vx -= sp;
		if (g_held[K_RIGHT] || (g_pad & PAD_RIGHT)) g_vx += sp;
		if (g_dragX < 0 && g_my >= 0 && g_my < PLAYH) { if (g_mx >= 0 && g_mx < 6) g_vx -= sp; else if (g_mx >= WW - 6) g_vx += sp; }
		clamp_vx ();
	}
	g_clk.paused = !running ();
	for (int n = g_clk.due (dt); n > 0 && running (); n--) { w->tick (); events (); }
	if (g_ended && g_overlay == O_NONE && (int) (now_ms () - g_endAt) >= 500) set_overlay (O_END);
	g_sp.step ();
	if (w->t.dirty.x1 >= w->t.dirty.x0)				// la terre a change : la lumiere suit
	{
		bool mini = w->step - g_miniStep >= 10;
		if (mini) g_miniStep = -100;
		Rect d = w->t.take_dirty ();
		if (g_lit) relight (w->t, d.x0 - 1, d.y0 - 1, d.x1 + 1, d.y1 + 1);
	}
	if (g_focusC >= 0 && (!in_view (g_focusC) || w->c[g_focusC].state == S_EXIT)) { g_focusC = -1; g_kbFocus = false; }
}

// ---- le dessin -----------------------------------------------------------------------------------------------------------
static void card (Img &im, int cw, int ch, const char *title, int &x, int &y)
{
	x = (WW - cw) / 2; y = (PLAYH - ch) / 2 + 2;
	im.rbox (x, y, cw, ch, 14, 0x4C4034, 0x2A241E, 248, 0xB8A488);
	textCS (x + cw / 2, y + 26, TR (title), 0xFFE8B0, 17);
	y += 36;
}
static void draw_help (Img &im)
{
	int x, y; card (im, 640, 296, "Comment jouer", x, y);
	wrap (x + 20, y, 600, TR ("Les bestioles sortent de la trappe et marchent droit devant. Touchez un r\xC3\xB4le, puis une bestiole, pour lui donner un travail. Menez-en assez jusqu'\xC3\xA0 la sortie lumineuse avant la fin du temps."), 0xF0E8DC, 12, false);
	static const char *const DESC[NBUILT] = { "monte aux murs au lieu de tourner", "survit \xC3\xA0 toute chute sous sa feuille", "reste sur place : les autres font demi-tour",
						  "pose un escalier de 12 briques", "creuse tout droit dans la terre", "\xC3\xA9""clate apr\xC3\xA8s 5 secondes, avec la terre" };
	for (int r = 0; r < NBUILT; r++)
	{
		int rx = x + 16 + (r / 3) * 310, ry = y + 58 + (r % 3) * 54;
		im.rbox (rx, ry, 48, 48, 7, 0x302820, 0x201A14, 256, 0x6A5C4C);
		blend_spr (im, icon_of (r, false), rx + 2, ry + 2);
		char b[64]; snprintf (b, sizeof b, "%d  %s", r + 1, ROLE_NAME[r]);
		text (rx + 58, ry + 18, b, 0xFFFFFF, 13);
		wrap (rx + 58, ry + 24, 240, TR (DESC[r]), 0xD0C4B4, 11, false);
	}
	add_btn (0, 0, WW, WH, B_CANCEL);
}
static void draw_cards (Img &im)
{
	char b[200], tm[16];
	int x, y;
	if (g_overlay != O_CARD && g_overlay != O_HELP) im.dim (140);
	if (g_overlay == O_HELP) im.dim (120);
	switch (g_overlay)
	{
	case O_CARD:
	{
		const char *h = lv->hint.get (g_lang);
		int hh = *h ? wrap (0, 0, 392, h, 0, 12, false, true) : 0;
		int cw = 440, ch = 158 + hh;
		card (im, cw, ch, lv->name.get (g_lang), x, y);
		mmss (lv->timeSec, tm, sizeof tm);
		snprintf (b, sizeof b, TR ("Sauvez-en %d sur %d   \xC2\xB7   %s"), lv->save, lv->count, tm);
		textC (x + cw / 2, y + 12, b, 0xFFFFFF, 14);
		int n = 0; for (int r = 0; r < NBUILT; r++) if (lv->roles[r]) n++;
		int rx = x + (cw - n * 56) / 2;
		for (int r = 0; r < NBUILT; r++) if (lv->roles[r])
		{
			im.rbox (rx + 4, y + 22, 48, 48, 7, 0x302820, 0x201A14, 256, 0x6A5C4C);
			blend_spr (im, icon_of (r, false), rx + 6, y + 24);
			snprintf (b, sizeof b, "\xC3\x97%d", lv->roles[r]); textC (rx + 28, y + 84, b, 0xFFFFFF, 12); rx += 56;
		}
		wrap (x + 24, y + 90, 392, h, 0xF0E8DC, 12, false);
		textC (x + cw / 2, y + 104 + hh, TR ("Touchez ou appuyez sur une touche pour commencer"), 0xC8BCAC, 11, false);
		add_btn (0, 0, WW, WH, B_RESUME);
		break;
	}
	case O_MENU:
		card (im, 260, 176, "Pause", x, y);
		button (im, x + 30, y + 2, 200, 38, B_RESUME, "Reprendre", true);
		button (im, x + 30, y + 46, 200, 38, B_RESTART, "Recommencer le niveau");
		button (im, x + 30, y + 90, 200, 38, B_BACK, "Retour aux niveaux");
		break;
	case O_NUKE:
		card (im, 420, 176, "Tout faire sauter ?", x, y);
		nuke_icon (im, x + 40, y + 34, 40, false);
		wrap (x + 76, y + 4, 330, TR ("Chaque bestiole encore dehors \xC3\xA9""clate apr\xC3\xA8s un compte \xC3\xA0 rebours de 5 secondes, et il n'en sort plus. Le niveau s'ach\xC3\xA8ve alors."), 0xF0E8DC, 12, false);
		button (im, x + 96, y + 84, 150, 40, B_NUKE, "Tout faire sauter", true);
		button (im, x + 256, y + 84, 110, 40, B_CANCEL, "Annuler");
		break;
	case O_END:
	{
		bool won = w->result == WON; int cw = 400;
		card (im, cw, 244, won ? "Niveau r\xC3\xA9ussi !" : "Pas assez de bestioles sauv\xC3\xA9""es", x, y);
		snprintf (b, sizeof b, "%d / %d", g_endSaved, lv->count);
		textCS (x + cw / 2, y + 30, b, won ? 0xFFFFFF : REDC, 28);
		snprintf (b, sizeof b, TR ("sauv\xC3\xA9""es (%d %%)"), lv->count ? g_endSaved * 100 / lv->count : 0);
		textC (x + cw / 2, y + 50, b, 0xD0C4B4, 12, false);
		im.rbox (x + 24, y + 60, cw - 48, 34, 7, 0x302820, 0x201A14, 256, 0x6A5C4C);
		snprintf (b, sizeof b, TR ("Requises  %d"), lv->save); textC (x + cw / 4 + 10, y + 82, b, 0xFFFFFF, 12);
		mmss (g_endTime, tm, sizeof tm); snprintf (b, sizeof b, TR ("Temps  %s"), tm); textC (x + cw * 3 / 4 - 10, y + 82, b, 0xFFFFFF, 12);
		const char *m = TR (won && g_newBest ? "Nouveau record !" : won ? (g_hasNext ? "Le niveau suivant est ouvert." : "Bravo !") : "Essayez un autre r\xC3\xB4le, ou donnez-le plus t\xC3\xB4t.");
		textC (x + cw / 2, y + 116, m, won && g_newBest ? AMBERC : 0xD0C4B4, 12, won && g_newBest);
		int n = g_hasNext ? 3 : 2, bw = 112, gap = 12, bx = x + (cw - n * bw - (n - 1) * gap) / 2;
		button (im, bx, y + 134, bw, 42, B_RETRY, "Rejouer", !g_hasNext); bx += bw + gap;
		if (g_hasNext) { button (im, bx, y + 134, bw, 42, B_NEXT, "Suivant", true); bx += bw + gap; }
		button (im, bx, y + 134, bw, 42, B_LEVELS, "Niveaux");
		break;
	}
	case O_HELP: draw_help (im); break;
	}
}
static void draw_status (Img &im)
{
	char b[160], tm[16];
	im.vgrad (0, STATUSY, WW, STATUSH, 0x3C342C, 0x28221C);
	im.rect (0, STATUSY, WW, 1, 0x7A6C5C);
	int x = 10, base = STATUSY + 17;
	if (g_role >= 0)
	{
		snprintf (b, sizeof b, TR ("%s : encore %d"), ROLE_NAME[g_role], w->roles[g_role]);
		text (x, base, b, 0xFFE0A0, 12); x += textW (b, 12) + 16;
	}
	int tg = target ();
	if (tg >= 0)
	{
		const Critter &k = w->c[tg];
		int r = g_role >= 0 ? w->can_take (tg, g_role) : OK;
		const char *why = r != OK ? refusal_word (r, g_role) : "";
		snprintf (b, sizeof b, "%s%s%s", state_word (k), *why ? " \xC2\xB7 " : "", why);
		text (x, base, b, *why ? REDC : 0xFFFFFF, 12, false);
	}
	else if (w->onlyBlockersLeft () && w->result == PLAYING) text (x, base, TR ("Plus que des bloqueurs : N pour tout faire sauter."), AMBERC, 12, false);
	bool enough = w->saved >= lv->save;
	mmss ((w->timeLeft () + STEPS_PER_SEC - 1) / STEPS_PER_SEC, tm, sizeof tm);
	snprintf (b, sizeof b, "%s", tm); textR (WW - 12, base, b, w->timeLeft () < STEPS_PER_SEC * 20 ? REDC : 0xFFFFFF, 13); text (WW - 96, base, TR ("Temps"), 0xB8AC9C, 11, false);
	snprintf (b, sizeof b, "%d / %d", w->saved, lv->save); textR (WW - 108, base, b, enough ? GREENC : 0xFFFFFF, 13); text (WW - 220, base, TR ("Sauv\xC3\xA9""es"), 0xB8AC9C, 11, false);
	snprintf (b, sizeof b, "%d", w->inPlay ()); textR (WW - 232, base, b, 0xFFFFFF, 13); text (WW - 300, base, TR ("Dehors"), 0xB8AC9C, 11, false);
}
static void slot_frame (Img &im, int x, int y, int sw, int sh, bool sel, bool hot)
{
	im.rbox (x, y, sw, sh, 8, sel ? 0x7A6840 : hot ? 0x5C5040 : 0x484036, sel ? 0x5C4C2C : 0x2E2820, 256, sel ? 0xFFD880 : 0x6E6252);
}
static void build_mini ()
{
	const Terrain &t = w->t;
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 172; x++)
		{
			int lx = x * t.w / 172, ly = y * t.h / 64, m = t.m[ly * t.w + lx];
			g_mini[y * 172 + x] = m == M_EMPTY ? 0x101820 : g_lit ? g_lit[ly * t.w + lx] : t.col[ly * t.w + lx];
		}
	g_miniStep = w->step;
}
static void draw_bar (Img &im)
{
	im.vgrad (0, BARY, WW, BARH, 0x322A24, 0x1C1814);
	int sw = 60, sh = 92, y = BARY + 6, x = 6; char b[16];
	for (int r = 0; r < NBUILT; r++, x += sw + 2)
	{
		int n = w->roles[r]; bool sel = r == g_role;
		add_btn (x, y, sw, sh, B_ROLE0 + r);
		slot_frame (im, x, y, sw, sh, sel, g_hot == B_ROLE0 + r);
		blend_spr (im, icon_of (r, n <= 0), x + 8, y + 20);
		snprintf (b, sizeof b, "%d", r + 1); text (x + 6, y + 14, b, 0xB8AC9C, 10);
		snprintf (b, sizeof b, "%d", n); textR (x + sw - 6, y + 16, b, n > 0 ? 0xFFFFFF : 0x807468, 15);
		ui_text_fit ((long) ROLE_NAME[r], x + 4, y + 86, sel ? 0xFFE0A0 : 0xC8BCAC, 9, F (false), sw - 6);
	}
	x += 6;								// le debit
	add_btn (x, y + 50, 38, 42, B_MINUS); add_btn (x + 40, y + 50, 38, 42, B_PLUS);
	slot_frame (im, x, y, 78, 46, false, false);
	text (x + 8, y + 14, TR ("D\xC3\x89""BIT"), 0xB8AC9C, 9);
	snprintf (b, sizeof b, "%d", w->rate); textC (x + 39, y + 38, b, 0xFFFFFF, 17);
	slot_frame (im, x, y + 50, 38, 42, false, g_hot == B_MINUS); textC (x + 19, y + 79, "-", 0xFFFFFF, 20);
	slot_frame (im, x + 40, y + 50, 38, 42, false, g_hot == B_PLUS); textC (x + 59, y + 79, "+", 0xFFFFFF, 20);
	x += 86;
	add_btn (x, y, 56, 46, B_PAUSE); slot_frame (im, x, y, 56, 46, g_userPaused, g_hot == B_PAUSE);
	im.rect (x + 20, y + 13, 6, 20, 0xFFFFFF); im.rect (x + 31, y + 13, 6, 20, 0xFFFFFF);
	add_btn (x + 60, y, 56, 46, B_FAST); slot_frame (im, x + 60, y, 56, 46, g_clk.fast, g_hot == B_FAST);
	{ float t1[6] = { (float) x + 74, (float) y + 13, (float) x + 74, (float) y + 33, (float) x + 88, (float) y + 23 }; im.poly (t1, 3, 0xFFFFFF);
	  float t2[6] = { (float) x + 88, (float) y + 13, (float) x + 88, (float) y + 33, (float) x + 102, (float) y + 23 }; im.poly (t2, 3, 0xFFFFFF); }
	add_btn (x, y + 50, 116, 42, B_NUKEASK); slot_frame (im, x, y + 50, 116, 42, false, g_hot == B_NUKEASK);
	nuke_icon (im, x + 22, y + 71, 26, w->nuking);
	text (x + 40, y + 75, TR ("Tout sauter"), 0xFFD8C0, 10);
	x += 124;
	if (w->step - g_miniStep >= 10) build_mini ();			// la mini-carte
	int mx = x, my = BARY + 12, mw = 172, mh = 64;
	if (mx + mw > WW - 4) mx = WW - 4 - mw;
	im.rbox (mx - 3, my - 3, mw + 6, mh + 6, 5, 0x0A0806, 0x141008, 256, 0x7A6C5C);
	for (int j = 0; j < mh; j++) memcpy (im.px + (long) (my + j) * im.stride + mx, g_mini + j * mw, (size_t) mw * 4);
	for (int i = 0; i < w->nout; i++)
	{
		const Critter &k = w->c[i]; if (k.state == S_SAVED || k.state == S_DEAD) continue;
		im.rect (mx + k.x * mw / lv->w, my + k.y * mh / lv->h - 1, 2, 2, 0xFFD060);
	}
	for (int i = 0; i < lv->nexit; i++) im.rect (mx + lv->exit[i].x * mw / lv->w - 1, my + lv->exit[i].y * mh / lv->h - 4, 3, 5, 0x60F0D8);
	int fx = g_vx * mw / lv->w, fw = (WW / 2) * mw / lv->w; if (fw > mw) fw = mw;
	im.rect (mx + fx, my, fw, 1, 0xFFFFFF); im.rect (mx + fx, my + mh - 1, fw, 1, 0xFFFFFF); im.rect (mx + fx, my, 1, mh, 0xFFFFFF); im.rect (mx + fx + fw - 1, my, 1, mh, 0xFFFFFF);
}
static void draw_play ()
{
	Img im = g_fb.img;
	g_nbtn = 0;
	int o = ox ();
	im.clip (0, 0, WW, PLAYH);
	blit_terrain (im, w->t, g_vx, o, PLAYH, g_tick);
	int ph = (int) (g_tick * 16 % 1000), pulse = ph < 500 ? ph * 255 / 500 : (1000 - ph) * 255 / 500;
	for (int i = 0; i < lv->nexit; i++) portal (im, SX (lv->exit[i].x), SY (lv->exit[i].y), pulse, g_tick);
	for (int i = 0; i < lv->nhatch; i++) hatch (im, SX (lv->hatch[i].at.x), SY (lv->hatch[i].at.y) - 4, g_hatchAt != 0);
	for (int i = 0; i < w->nout; i++)
	{
		const Critter &k = w->c[i];
		if (k.state == S_SAVED || k.state == S_DEAD || k.state == S_BURST) continue;
		int sx = SX (k.x), sy = SY (k.y);
		if (sx < -30 || sx > WW + 30) continue;
		Look lk = { D_WALK, (int) (k.frame / 3), k.dir >= 0 ? 1 : -1, k.flags & 3, k.fuse >= 0 && k.fuse <= FUSE_TICK, 0 };
		int shrink = 0, sink = 0;
		switch (k.state)
		{
		case S_FALL: lk.state = (k.flags & F_FLOATER) && k.y - k.fallFrom >= 6 ? D_FLOAT : D_FALL; lk.frame = 0; break;
		case S_CLIMB: lk.state = D_CLIMB; lk.frame = k.frame / 4; break;
		case S_BLOCK: lk.state = D_BLOCK; lk.frame = 0; break;
		case S_BUILD: lk.state = D_BUILD; lk.frame = k.timer / 2; break;
		case S_SHRUG: lk.state = D_SHRUG; lk.frame = 0; break;
		case S_DIG: lk.state = D_DIG; lk.frame = k.frame / 2; break;
		case S_SPLAT: lk.state = D_SPLAT; lk.frame = 0; lk.flags = 0; break;
		case S_EXIT: lk.frame = 0; shrink = k.timer * NSHRINK / EXIT_STEPS; if (shrink >= NSHRINK) shrink = NSHRINK - 1; break;
		case S_DROWN: lk.frame = 0; sink = k.timer * 2; break;
		case S_BURN: lk.frame = 0; lk.hot = true; break;
		default: break;
		}
		if (k.state == S_WALK || k.state == S_BLOCK || k.state == S_BUILD || k.state == S_DIG || k.state == S_SHRUG)	// son ombre sur le sol
			im.ellipse ((float) sx + 1, (float) sy, 9, 2.4f, 0x000000, 90);
		blend_spr (im, frame_of (lk, shrink), sx - SPX, sy - SPY, sink);
		if (k.state == S_DROWN) for (int b = 0; b < 3; b++) el (im, sx - 4 + b * 4, sy - 6 - ((k.timer * 2 + b * 5) % 18), 1.4, 1.4, 0xBFE4FF, 200);
		if (k.state == S_BURN) for (int b = 0; b < 4; b++) el (im, sx - 6 + b * 4, sy - 8 - ((k.timer * 3 + b * 7) % 20), 1.3, 1.3, b & 1 ? 0xFFB040 : 0xFF6020, 230);
		if (k.fuse > 0 && w->alive (i) && k.state != S_EXIT)
		{
			char b[4]; int dg = (k.fuse + FUSE_TICK - 1) / FUSE_TICK; snprintf (b, sizeof b, "%d", dg);
			digit (sx, sy - 36 + (k.state == S_DIG ? 4 : 0), b, 13, dg == 1 ? 0xFF6A50 : 0xFFFFFF);
		}
	}
	{								// les eclats, en coordonnees du niveau
		float dx = (float) (o - g_vx * 2);
		for (int i = 0; i < g_sp.n; i++)
		{
			const Spark &p = g_sp.s[i];
			im.disc (p.x + dx, p.y, p.size, p.col, p.life * 256 / p.max);
		}
	}
	if (!modal ())
	{
		int tg = target ();
		if (tg >= 0)
		{
			unsigned now = now_ms ();
			bool red = (g_role >= 0 && w->can_take (tg, g_role) != OK) || (tg == g_refuseWho && (int) (now - g_refuseUntil) < 0 && (now / 100) % 2 == 0);
			brackets (im, SX (w->c[tg].x), SY (w->c[tg].y), red ? REDC : 0xFFFFFF, g_kbFocus);
		}
	}
	if (g_userPaused && !modal ())
	{
		im.dim (190);
		im.rbox ((WW - 240) / 2, 10, 240, 30, 15, 0x4C4034, 0x342C24, 240, 0xB8A488);
		textC (WW / 2, 30, TR ("Pause  \xC2\xB7  P ou Espace : reprendre"), 0xFFFFFF, 11);
	}
	if (g_clk.fast && !modal ()) { im.rbox (WW - 196, 8, 62, 24, 12, 0xF0A030, 0xC06A18, 235); textC (WW - 165, 25, "\xC3\x97" "3", 0xFFFFFF, 13); }
	im.noClip ();
	draw_status (im);
	draw_bar (im);
	if (g_overlay != O_NONE) { im.clip (0, 0, WW, PLAYH); draw_cards (im); im.noClip (); }
}

static void build_pick ()
{
	if (!g_pick.alloc (WW, WH)) return;
	Img &p = g_pick.img;
	for (int y = 0; y < WH; y++)
	{
		uint32_t *r = p.px + (long) y * p.stride;
		for (int x = 0; x < WW; x++)
		{
			float h = 330 + fbm ((float) x * 0.008f, 0.5f, 3) * 90;
			float h2 = 380 + fbm ((float) x * 0.013f, 2.5f, 8) * 60;
			unsigned c = mix (0x182848, 0x7A5868, y * 256 / WH);
			float n = fbm ((float) x * 0.01f, (float) y * 0.03f, 6);
			if (n > 0.5f) c = mix (c, 0xC0A8B8, (int) ((n - 0.5f) * 200));
			if ((float) y > h) c = mix (0x4A6038, 0x2A3822, (int) (clampf (((float) y - h) * 2, 0, 256)));
			if ((float) y > h2) c = mix (0x5C4028, 0x38261A, (int) (clampf (((float) y - h2) * 3, 0, 256)));
			r[x] = c;
		}
	}
	g_pick.begin ();
	textS (18, 42, TR ("Bestioles"), 0xFFD080, 30);
	textS (190, 42, TR ("Choisir un niveau"), 0xE0D8E0, 13);
	g_pick.end ();
	g_pickDirty = false;
}
static void draw_picker ()
{
	Img im = g_fb.img;
	g_nbtn = 0;
	memcpy (im.px, g_pick.img.px, (size_t) WW * WH * 4);
	static const char *const GROUP[3] = { TR ("Entra\xC3\xAEnement"), TR ("Exp\xC3\xA9""dition"), TR ("Mes niveaux") };
	int L = 330, y = 56 - g_listTop, last = -1;
	im.rbox (10, 52, L, WH - 62, 10, 0x1C2030, 0x141620, 225, 0x586078);
	im.clip (12, 54, L + 8, WH - 12);
	for (int i = 0; i < g_nent; i++)
	{
		Entry &e = *g_ent[i];
		if (e.group != last) { last = e.group; y += 4; if (y > 40 && y < WH) text (20, y + 12, GROUP[e.group], 0xFFC870, 12); y += 18; }
		bool sel = i == g_sel, lock = locked (i);
		if (y + 24 > 54 && y < WH - 12)
		{
			if (y >= 54 && y + 24 <= WH - 12) add_btn (14, y, L - 8, 24, B_ROW0 + i);
			if (sel) im.rbox (14, y, L - 8, 24, 6, 0x58688C, 0x445478, 256, 0xFFC860);
			char b[96]; snprintf (b, sizeof b, "%d  %s", e.num, e.ok ? e.name[g_lang] : e.file);
			text (24, y + 17, b, !e.ok ? REDC : lock ? 0x707888 : 0xF0F2F8, 12, false);
			Best bs = e.ok ? progress_get (g_prog, e.section) : Best ();
			if (e.ok && bs.solved) { im.disc ((float) (L - 18), (float) (y + 12), 6, 0x40C060); im.line ((float) (L - 21), (float) (y + 12), (float) (L - 19), (float) (y + 15), 1.6f, 0xFFFFFF); im.line ((float) (L - 19), (float) (y + 15), (float) (L - 15), (float) (y + 9), 1.6f, 0xFFFFFF); }
			else if (lock) { im.rbox (L - 23, y + 10, 10, 9, 2, 0x707888, 0x586070); im.ring ((float) (L - 18), (float) (y + 10), 3, 1.4f, 0x707888); }
			else if (e.ok) textR (L - 12, y + 16, "nouveau", 0xFFC870, 9, false);
		}
		y += 26;
	}
	im.noClip ();
	if (g_sel < g_nent)
	{
		Entry &e = *g_ent[g_sel];
		int rx = L + 22, rw = WW - rx - 12, ry = 52;
		im.rbox (rx - 4, ry, rw + 8, WH - 62, 10, 0x1C2030, 0x141620, 225, 0x586078);
		if (e.ok)
		{
			int pw = rw - 12, ph = pw * e.h / e.w; if (ph > 110) { ph = 110; pw = ph * e.w / e.h; }
			if (!e.thumb) make_thumb (e, pw, ph);
			if (e.thumb) for (int j = 0; j < e.th; j++) memcpy (im.px + (long) (ry + 8 + j) * im.stride + rx + 2 + (rw - 4 - e.tw) / 2, e.thumb + j * e.tw, (size_t) e.tw * 4);
			int iy = ry + 16 + ph;
			iy += wrap (rx + 4, iy, rw - 8, e.name[g_lang], 0xFFE0A0, 15, true) + 4;
			char b[120], tm[16]; mmss (e.timeSec, tm, sizeof tm);
			snprintf (b, sizeof b, TR ("\xC3\x80 sauver %d sur %d  \xC2\xB7  %s"), e.save, e.count, tm); text (rx + 4, iy + 11, b, 0xFFFFFF, 12, false); iy += 18;
			Best bs = progress_get (g_prog, e.section);
			if (bs.solved) { mmss (bs.timeSec, tm, sizeof tm); snprintf (b, sizeof b, TR ("Record : %d sauv\xC3\xA9""es  \xC2\xB7  %s"), bs.saved, tm); text (rx + 4, iy + 11, b, 0x80E8A0, 12, false); }
			else text (rx + 4, iy + 11, TR ("Pas encore r\xC3\xA9ussi."), 0xA8B0C0, 12, false);
			iy += 20;
			int cx = rx + 4; for (int r = 0; r < NBUILT; r++) if (e.roles[r])
			{
				blend_spr (im, icon_of (r, false), cx - 4, iy - 2);
				snprintf (b, sizeof b, "\xC3\x97%d", e.roles[r]); textC (cx + 18, iy + 50, b, 0xFFFFFF, 11); cx += 54;
			}
			iy += 56;
			wrap (rx + 4, iy, rw - 8, e.hint[g_lang], 0xD0D6E0, 11, false);
			if (locked (g_sel)) textC (rx + rw / 2, WH - 30, TR ("R\xC3\xA9ussissez le niveau d'avant pour ouvrir celui-ci."), 0xC8BCAC, 11, false);
			else button (im, rx + rw - 150, WH - 56, 146, 38, B_PLAYIT, "Jouer", true, 15);
		}
		else
		{
			text (rx + 4, ry + 24, TR ("Ce niveau ne peut pas \xC3\xAAtre jou\xC3\xA9."), 0xFFFFFF, 13);
			wrap (rx + 4, ry + 34, rw - 8, e.err, REDC, 12, false);
		}
		button (im, rx + 4, WH - 56, 100, 38, B_HELPIT, "Aide");
	}
	if (g_overlay == O_HELP) draw_cards (im);
}

static void present ()
{
	int W = (int) ui.w, H = (int) ui.h;
	int dw = W, dh = dw * WH / WW;
	if (dh > H) { dh = H; dw = dh * WW / WH; }
	g_dw = dw; g_dh = dh; g_dx0 = (W - dw) / 2; g_dy0 = (H - dh) / 2;
	if (dw == WW && dh == WH)
	{
		for (int y = 0; y < H; y++)
		{
			uint32_t *d = ui.surf + (long) y * W;
			if (y < g_dy0 || y >= g_dy0 + WH) { memset (d, 0, (size_t) W * 4); continue; }
			memset (d, 0, (size_t) g_dx0 * 4); memset (d + g_dx0 + WW, 0, (size_t) (W - g_dx0 - WW) * 4);
			memcpy (d + g_dx0, g_fb.img.px + (long) (y - g_dy0) * g_fb.img.stride, WW * 4);
		}
		return;
	}
	static int xmap[4096]; for (int x = 0; x < dw && x < 4096; x++) xmap[x] = x * WW / dw;
	int prev = -1;
	for (int y = 0; y < H; y++)
	{
		uint32_t *d = ui.surf + (long) y * W;
		if (y < g_dy0 || y >= g_dy0 + dh) { memset (d, 0, (size_t) W * 4); continue; }
		memset (d, 0, (size_t) g_dx0 * 4); memset (d + g_dx0 + dw, 0, (size_t) (W - g_dx0 - dw) * 4);
		int sy = (y - g_dy0) * WH / dh;
		if (sy == prev) { memcpy (d + g_dx0, d - W + g_dx0, (size_t) dw * 4); continue; }
		prev = sy;
		const uint32_t *s = g_fb.img.px + (long) sy * g_fb.img.stride;
		for (int x = 0; x < dw && x < 4096; x++) d[g_dx0 + x] = s[xmap[x]];
	}
}

// ---- les evenements ---------------------------------------------------------------------------------------------------
static void action (int id)
{
	switch (id)
	{
	case B_RESUME: if (g_overlay == O_CARD) start_playing (); else set_overlay (O_NONE); break;
	case B_RESTART: case B_RETRY: if (g_cur >= 0) start_level (g_cur); break;
	case B_BACK: case B_LEVELS: go_picker (); break;
	case B_NUKE: nuke_now (); break;
	case B_CANCEL: if (g_overlay == O_HELP && g_mode == M_PLAY) set_overlay (g_prevOverlay); else set_overlay (O_NONE); break;
	case B_NEXT: if (g_nextEntry >= 0) start_level (g_nextEntry); break;
	case B_PLAYIT: if (g_sel < g_nent && g_ent[g_sel]->ok && !locked (g_sel)) start_level (g_sel); else blip (); break;
	case B_HELPIT: help (); break;
	case B_PAUSE: toggle_pause (); break;
	case B_FAST: toggle_fast (); break;
	case B_NUKEASK: ask_nuke (); break;
	case B_MINUS: set_rate (-5); break;
	case B_PLUS: set_rate (5); break;
	default:
		if (id >= B_ROW0) { int i = id - B_ROW0; if (i == g_sel) action (B_PLAYIT); else if (i < g_nent) g_sel = i; }
		else if (id >= B_ROLE0) choose_role (id - B_ROLE0);
		break;
	}
}
static void command (int id)
{
	switch (id)
	{
	case 1: if (g_mode == M_PLAY && g_cur >= 0) start_level (g_cur); break;
	case 2: toggle_pause (); break;
	case 3: toggle_fast (); break;
	case 4: ask_nuke (); break;
	case 5: if (g_mode == M_PLAY) { if (w->result != PLAYING || g_overlay != O_NONE) go_picker (); else set_overlay (O_MENU); } break;
	case 6:
	{
		char path[256] = "";
		ui_file_filter ((long) "*.level");
		if (ui_file_open ((long) HOMEDIR, (long) path, sizeof path) == 1 && path[0] && g_nent < MAXENT)
		{
			int i = find_entry (path);
			if (i < 0) { g_ent[g_nent] = load_entry (path, 2); i = g_nent++; }
			if (g_ent[i]->ok) start_level (i);
			else { g_mode = M_PICKER; g_cur = -1; g_overlay = O_NONE; g_sel = i; }
		}
		break;
	}
	case 7: toggle_sound (); break;
	case 8: help (); break;
	case 9: g_quit = true; break;
	}
}
static void on_key (int code, int ch, int mods)
{
	int c = ch | 0x20;
	if (g_mode == M_PICKER)
	{
		if (g_overlay == O_HELP) { if (code == K_ESC || code == K_F1 || code == K_ENTER || code == K_SPACE) g_overlay = O_NONE; return; }
		if (code == K_ESC) g_quit = true;
		else if (code == K_UP) { if (g_sel > 0) g_sel--; }
		else if (code == K_DOWN) { if (g_sel < g_nent - 1) g_sel++; }
		else if (code == K_ENTER || code == K_SPACE) action (B_PLAYIT);
		else if (code == K_F1) help ();
		else if (c == 'm') toggle_sound ();
		return;
	}
	switch (g_overlay)
	{
	case O_CARD:
		if (code == K_ESC) go_picker ();
		else if (code == K_F1) help ();
		else start_playing ();
		return;
	case O_HELP: if (code == K_ESC || code == K_F1 || code == K_ENTER || code == K_SPACE) set_overlay (g_prevOverlay); return;
	case O_MENU: if (code == K_ESC || code == K_ENTER || code == K_SPACE) set_overlay (O_NONE); return;
	case O_NUKE: if (code == K_ESC) set_overlay (O_NONE); else if (c == 'n' || code == K_ENTER) nuke_now (); return;
	case O_END:
		if (code == K_ESC) go_picker ();
		else if (code == K_ENTER || code == K_SPACE) action (g_hasNext ? B_NEXT : B_RETRY);
		return;
	}
	if (ch >= '1' && ch <= '6') { choose_role (ch - '1'); return; }
	if (code == K_TAB) tab_focus ((mods & UI_MOD_SHIFT) != 0);
	else if (code == K_ENTER) { if (g_kbFocus && g_focusC >= 0) give (g_focusC); else blip (); }
	else if (code == K_SPACE || c == 'p') toggle_pause ();
	else if (c == 'f') toggle_fast ();
	else if (c == 'n') ask_nuke ();
	else if (ch == '-' || ch == '_') set_rate (-5);
	else if (ch == '+' || ch == '=') set_rate (5);
	else if (code == K_HOME) g_vx = 0;
	else if (code == K_END) g_vx = max_vx ();
	else if (code == K_ESC) set_overlay (O_MENU);
	else if (c == 'm') toggle_sound ();
	else if (code == K_F1) help ();
}
static void mini_scroll (int x)
{
	int mx = 6 + NBUILT * 62 + 6 + 86 + 124; if (mx + 172 > WW - 4) mx = WW - 4 - 172;
	g_vx = (x - mx) * lv->w / 172 - WW / 4; clamp_vx ();
}
static void on_mouse (int kind, int wx, int wy, int buttons)
{
	int x = g_dw ? (wx - g_dx0) * WW / g_dw : wx, y = g_dh ? (wy - g_dy0) * WH / g_dh : wy;
	if (kind == 2)
	{
		g_hot = 0; g_miniDrag = false;
		if (g_dragX >= 0 && !g_dragged && g_mode == M_PLAY && !modal ()) { g_mx = x; g_my = y; g_kbFocus = false; int t = under (); if (t >= 0) give (t); }
		g_dragX = -1;
		return;
	}
	g_mx = x; g_my = y;
	if (kind == 0)
	{
		if (g_miniDrag) mini_scroll (x);
		else if (g_dragX >= 0)
		{
			if (abs (x - g_dragX) > 8) g_dragged = true;
			if (g_dragged) { g_vx = g_dragVx - (x - g_dragX) / 2; clamp_vx (); }
		}
		g_kbFocus = false;
		return;
	}
	if (!(buttons & 1)) return;
	int id = hit_btn (x, y);
	if (id) { g_hot = id; action (id); return; }
	if (wy < 30 && wx < (long) ui.w - CAPW && (g_mode != M_PLAY || under () < 0)) { ui_window_drag (); return; }
	if (g_mode != M_PLAY) return;
	if (modal ()) return;
	if (y >= BARY) { g_miniDrag = true; mini_scroll (x); return; }
	if (y < PLAYH) { g_dragX = x; g_dragVx = g_vx; g_dragged = false; }
}
static void on_pad ()
{
	if (ui_ev_field (&ui, 4) == 0) return;
	unsigned b = (unsigned) ui_ev_field (&ui, 8), e = b & ~g_pad;
	g_pad = b;
	if (g_mode == M_PICKER)
	{
		if ((e & PAD_UP) && g_sel > 0) g_sel--;
		if ((e & PAD_DOWN) && g_sel < g_nent - 1) g_sel++;
		if (e & (PAD_A | PAD_START)) action (B_PLAYIT);
		return;
	}
	if (g_overlay == O_CARD) { if (e & (PAD_A | PAD_START)) start_playing (); return; }
	if (g_overlay == O_MENU) { if (e & (PAD_A | PAD_START)) set_overlay (O_NONE); else if (e & PAD_B) go_picker (); return; }
	if (g_overlay == O_NUKE) { if (e & PAD_A) nuke_now (); else if (e & PAD_B) set_overlay (O_NONE); return; }
	if (g_overlay == O_END) { if (e & (PAD_A | PAD_START)) action (g_hasNext ? B_NEXT : B_RETRY); else if (e & PAD_B) go_picker (); return; }
	if (g_overlay == O_HELP) { if (e) set_overlay (g_prevOverlay); return; }
	if (e & PAD_START) set_overlay (O_MENU);
	if (e & PAD_A) { if (g_kbFocus && g_focusC >= 0) give (g_focusC); else tab_focus (false); }
	if (e & PAD_X) tab_focus (false);
	if (e & PAD_Y) toggle_fast ();
	if (e & PAD_L) for (int k = 1; k <= NBUILT; k++) { int r = (g_role + NBUILT * 2 - k) % NBUILT; if (w->roles[r] > 0) { g_role = r; break; } }
	if (e & PAD_R) for (int k = 1; k <= NBUILT; k++) { int r = (g_role + k) % NBUILT; if (w->roles[r] > 0) { g_role = r; break; } }
}
static void one_event (long t)
{
	if (t == WEV_CLOSE) { g_quit = true; return; }
	if (t == WEV_RESIZE) { long c, k; uikit_w_event (&c, &k); return; }
	if (t == WEV_MENU) { command (ui_ev_field (&ui, EV_CODE)); return; }
	if (t == WEV_FOCUS) { if (ui_ev_field (&ui, EV_KIND) == 0) { memset (g_held, 0, sizeof g_held); g_mx = g_my = -1; } return; }
	if (t == WEV_WHEEL) { if (g_mode == M_PLAY && !modal ()) { g_vx -= ui_ev_field (&ui, EV_Y) * 24; clamp_vx (); } else if (g_mode == M_PICKER) { g_listTop -= ui_ev_field (&ui, EV_Y) * 26; if (g_listTop < 0) g_listTop = 0; if (g_listTop > 400) g_listTop = 400; } return; }
	if (t == WEV_MOUSE) { on_mouse (ui_ev_field (&ui, EV_KIND), ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), ui_ev_field (&ui, EV_CODE)); return; }
	if (t == WEV_PAD) { on_pad (); return; }
	if (t == WEV_KEY)
	{
		int code = ui_ev_field (&ui, EV_CODE);
		bool down = ui_ev_field (&ui, EV_KIND) != 0;
		if (code >= 0 && code < 256) g_held[code] = down;
		if (!down) return;
		long id = ui_menu_key (0);
		if (id >= 0) { command ((int) id); return; }
		on_key (code, ui_ev_field (&ui, EV_CHAR), ui_ev_field (&ui, EV_MODS));
	}
}

static const char MENU[] = "MPartie\n"
	"I1\tRecommencer\tCtrl+R\n"
	"I2\tPause\tP\n"
	"I3\tAcc\xC3\xA9l\xC3\xA9r\xC3\xA9\tF\n"
	"I4\tTout faire sauter\tN\n"
	"I5\tNiveaux...\tEchap\n"
	"I6\tOuvrir un fichier de niveau...\tCtrl+O\n"
	"-\n"
	"I7c\tSon\tM\n"
	"I8\tComment jouer\tF1\n"
	"-\n"
	"I9\tQuitter\n";

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/critters.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	for (int i = 0; i < critters::NROLES; i++) ROLE_NAME[i] = TR (ROLE_NAME[i]);
	audiokit_kit_open ();
	g_lang = !strcmp ((const char *) ui_lang (), "fr");
	mkdir ("SD0:/home", 0777); mkdir (HOMEDIR, 0777);
	g_prog = gkv_load (PROGRESS);
	if (!g_prog) g_prog = gkv_new ();
	g_snd.open ();
	g_snd.mute = !strcmp (progress_setting (g_prog, "sound", "1"), "0");
	g_scan = new Level; g_scanT = new Terrain; lv = new Level; w = new World;
	memset (lv, 0, sizeof *lv);
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = WW, wh = WH;
	if (scr[1] - scr[2] - scr[3] - 96 < wh) { wh = (int) (scr[1] - scr[2] - scr[3]) - 96; ww = wh * WW / WH; }
	ui_window_ex (-1, -1, ww, wh, (long) "Critters", WF_RESIZABLE | WF_GLYPH_LIGHT | WF_BODY_DARK);
	ui_min_size (480, 270);
	ui_menu ((long) MENU);
	ui_menu_state (7, g_snd.mute ? 0 : UMS_CHECKED);
	if (!g_fb.alloc (WW, WH)) return 1;
	load_entries (0);
	if (argc > 1 && ends_level (argv[1]) && g_nent < MAXENT)
	{
		int i = find_entry (argv[1]);
		if (i < 0) { g_ent[g_nent] = load_entry (argv[1], 2); i = g_nent++; }
		g_sel = i;
		if (g_ent[i]->ok) start_level (i);
	}
	unsigned last = now_ms ();
	while (!g_quit)
	{
		long t; while ((t = ui_event (0)) > 0) one_event (t);
		unsigned now = now_ms (), dt = now - last; last = now;
		if (dt > 100) dt = 100;
		g_tick++;
		g_snd.tick ();
		step_game (dt);
		if (g_mode != M_PLAY && g_pickDirty) build_pick ();
		g_fb.begin ();
		if (g_mode == M_PLAY && g_cur >= 0) draw_play ();
		else draw_picker ();
		g_fb.end ();
		present ();
		ui_update_all ();
		if (ui_frame_request ())
			for (unsigned t0 = now_ms (); !g_quit && now_ms () - t0 < 200; )
			{
				long e = ui_event (10);
				if (e == WEV_FRAME) break;
				if (e > 0) one_event (e);
			}
	}
	g_snd.close ();
	return 0;
}
