//
// pinball -- le Pinball d'Onyx : un flipper a la vraie physique (table.cpp, physics.cpp, rules.cpp : ceux d'Onyx, le
// lecteur de texte mis a part), ses trois tables et toute table ecrite dans un fichier .table ; habille ici : plateau
// eclaire, ombres, chrome, lueurs des lampes, etincelles, trainee de la bille, afficheur a points.
//
// Ecran de jeu : le plateau a gauche, le panneau a droite (afficheur, bonus, multiplicateur, record, inclinaison, but,
// regles, boutons). Avant une partie, le choix de la table : la liste, l'apercu, le but, les 5 meilleurs scores.
//
// Touches : Gauche / Z et Droite / M les batteurs, Espace / Bas / Entree le lanceur (tenir pour tirer), Haut / N
// secouer (trois fois en 5 s : TILT), P pause, Echap la carte de pause, S le son. Au doigt : les moities du plateau pour
// les batteurs ; Lancer et Secouer dans le panneau. Manette : L / gauche et R / B les batteurs, A le lanceur, Y secouer,
// Start pause. Fichiers : SD0:/apps/pinball.app/tables, SD0:/home/pinball (les tables du joueur, scores.ini).
//
#include "../../inc/gameart.h"
#include "table.h"
#include "physics.h"
#include "rules.h"
#include "scores.h"
#include <dirent.h>
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-en.txt : le source est en francais)

using namespace ga;
using pinball::Table;
using pinball::Game;
using pinball::World;
using pinball::Vec;

namespace ga { struct ui_vars ui; }

#define APPDIR		"SD0:/apps/pinball.app/"
#define TABLEDIR	APPDIR "tables"
#define HOMEDIR		"SD0:/home/pinball"
#define SCORES		HOMEDIR "/scores.ini"

static const double PB_RAD = M_PI / 180;
#include "draw.h"

enum { PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8, PAD_A = 16, PAD_B = 32, PAD_X = 64, PAD_Y = 128, PAD_L = 256, PAD_R = 512,
       PAD_L2 = 1024, PAD_R2 = 2048, PAD_SELECT = 4096, PAD_START = 8192 };
enum { K_ENTER = 0x28, K_ESC = 0x29, K_SPACE = 0x2C, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52 };
enum { CAPW = 112 };				// les boutons de la fenetre, en haut a droite

// ---- les tables --------------------------------------------------------------------------------------------------
struct TableEntry
{
	char path[256], file[96], section[128];
	bool shipped;
	Table *t;				// 0 : refusee par le lecteur
	Looks look;
	char err[300];
	long best;
	uint32_t *thumb; int tw, th;		// sa petite image
	uint32_t *prev; int pw, ph;		// son apercu
};
enum { MAXENT = 32 };
static TableEntry *g_ent[MAXENT]; static int g_nent, g_sel;
static gkv *g_scores;
static Snd g_snd;

static void save_scores () { mkdir ("SD0:/home", 0777); mkdir (HOMEDIR, 0777); gkv_save (g_scores, SCORES, "Pinball : meilleurs scores (ecrit par le jeu)"); }
static long best_of (const char *section)
{
	pinball::ScoreLine sl[pinball::TOPN];
	return pinball::scores_read (g_scores, section, sl) > 0 ? sl[0].score : 0;
}
static bool ends_table (const char *name) { int l = (int) strlen (name); return l > 6 && !strcasecmp (name + l - 6, ".table"); }
static void error_text (const pinball::LoadError &e, char *out, int cap)
{
	if (e.line > 0) snprintf (out, (size_t) cap, TR ("ligne %d : %s"), e.line, e.reason);
	else snprintf (out, (size_t) cap, "%s", e.reason);
}
static TableEntry *load_entry (const char *path, bool shipped)
{
	TableEntry *e = (TableEntry *) calloc (1, sizeof (TableEntry));
	snprintf (e->path, sizeof e->path, "%s", path);
	snprintf (e->file, sizeof e->file, "%s", gkv_basename (path));
	e->shipped = shipped;
	pinball::scores_section (path, shipped, e->section, sizeof e->section);
	pinball::LoadError err; memset (&err, 0, sizeof err);
	long n = 0; char *text = gkv_read_file (path, &n, pinball::MAXFILE);
	if (!text) pinball::load_error_file (err, n < 0);
	else
	{
		Table *t = new Table;
		if (pinball::load_table (text, *t, err)) { e->t = t; looks_of (*t, e->look); }
		else delete t;
		free (text);
	}
	if (!e->t) error_text (err, e->err, sizeof e->err);
	e->best = best_of (e->section);
	return e;
}
static void free_entries ()
{
	for (int i = 0; i < g_nent; i++) { delete g_ent[i]->t; free (g_ent[i]->thumb); free (g_ent[i]->prev); free (g_ent[i]); }
	g_nent = 0;
}
static int find_entry (const char *path) { for (int i = 0; i < g_nent; i++) if (!strcasecmp (g_ent[i]->path, path)) return i; return -1; }
static void scan_dir (const char *dir, bool shipped)
{
	enum { MAXN = 32 };
	static char names[MAXN][128]; int n = 0;
	DIR *d = opendir (dir);
	if (!d) return;
	struct dirent *de;
	while ((de = readdir (d)) != 0 && n < MAXN)
		if (ends_table (de->d_name)) snprintf (names[n++], sizeof names[0], "%s", de->d_name);
	closedir (d);
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--)
	{ char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	for (int i = 0; i < n && g_nent < MAXENT; i++)
	{
		char p[256]; if (snprintf (p, sizeof p, "%s/%s", dir, names[i]) >= (int) sizeof p) continue;
		if (find_entry (p) < 0) g_ent[g_nent++] = load_entry (p, shipped);
	}
}
static void load_entries (const char *keep)
{
	char keepPath[256]; snprintf (keepPath, sizeof keepPath, "%s", keep ? keep : "");
	free_entries ();
	scan_dir (TABLEDIR, true);
	scan_dir (HOMEDIR, false);
	g_sel = 0;
	int k = keepPath[0] ? find_entry (keepPath) : -1;
	if (k < 0)
	{
		const char *last = pinball::scores_setting (g_scores, "table", "");
		for (int i = 0; i < g_nent && last[0]; i++) if (!strcmp (g_ent[i]->section, last)) { k = i; break; }
	}
	if (k >= 0) g_sel = k;
}

// ---- l'etat du jeu ---------------------------------------------------------------------------------------------------
enum { M_PICKER, M_PLAY };
enum { O_NONE, O_PAUSE, O_OVER, O_SCORES };
enum { B_NONE, B_LAUNCH, B_NUDGE, B_PAUSE, B_RESUME, B_BACK, B_PLAYIT, B_OPEN, B_ROW0 = 100 };
static const unsigned RED = 0xFF5040, AMBER = 0xFFB030;

static Game *g; static int g_cur = -1;
static int g_mode = M_PICKER, g_overlay = O_NONE;
static Layer g_layer, g_dmd, g_pick;
static int g_layerFor = -1, g_layerW, g_layerH;
static bool g_pickDirty = true;
static View g_v;
static unsigned g_accUs, g_tick;
static bool g_tap, g_nudgeKey;
static unsigned g_pad, g_padEdge;
static int g_shake;
static char g_msg[64]; static unsigned g_msgUntil; static bool g_msgRed;
static long g_final; static int g_rank = -1;
static unsigned char g_role[256];		// touches tenues : 1 batteur gauche, 2 droit, 3 lanceur
static bool g_tl, g_tr, g_tp;			// au doigt
static int g_hot;				// le bouton tenu
static bool g_quit;
static Sparks g_sp;
enum { TRAIL = 8 };
static Vec g_trail[pinball::MAXBALL][TRAIL]; static int g_tn[pinball::MAXBALL];
static char g_dmdTop[32], g_dmdBot[64]; static int g_dmdW, g_dmdH; static unsigned g_dmdCol;

struct Btn { int x, y, w, h, id; };
enum { MAXBTN = 48 };
static Btn g_btn[MAXBTN]; static int g_nbtn;
static void add_btn (int x, int y, int w, int h, int id) { if (g_nbtn < MAXBTN) { Btn &b = g_btn[g_nbtn++]; b.x = x; b.y = y; b.w = w; b.h = h; b.id = id; } }
static int hit_btn (int x, int y) { for (int i = g_nbtn - 1; i >= 0; i--) { const Btn &b = g_btn[i]; if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return b.id; } return 0; }
// un bouton du jeu, avec son libelle
static void button (Img &im, int x, int y, int w, int h, int id, const char *s, bool accent = false, int px = 14)
{
	add_btn (x, y, w, h, id);
	bool hot = g_hot == id;
	unsigned top = accent ? 0xF0A030 : 0x3C4658, bot = accent ? 0xC06A18 : 0x262E3C;
	if (hot) { top = shade (top, 340); bot = shade (bot, 340); }
	im.rbox (x, y, w, h, 9, top, bot, 256, accent ? 0xFFD890 : 0x7C8AA0);
	textCS (x + w / 2, y + h / 2 + px * 36 / 100, TR (s), accent ? 0xFFFFFF : 0xE8ECF4, px);
}

static const Table &T () { return *g_ent[g_cur]->t; }
static TableEntry &E () { return *g_ent[g_cur]; }

static void message (const char *s, unsigned ms = 2000, bool red = false)
{
	snprintf (g_msg, sizeof g_msg, "%s", TR (s)); g_msgUntil = now_ms () + ms; if (!g_msgUntil) g_msgUntil = 1; g_msgRed = red;
}
static void set_overlay (int o) { g_overlay = o; if (o == O_NONE) g_accUs = 0; }
static void go_picker ()
{
	char keep[256] = "";
	if (g_cur >= 0 && g_cur < g_nent) snprintf (keep, sizeof keep, "%s", g_ent[g_cur]->path);
	g_mode = M_PICKER; g_cur = -1; g_overlay = O_NONE; g_layerFor = -1; g_pickDirty = true;
	load_entries (keep);
	g_sp.clear ();
}
static void start_game (int i)
{
	TableEntry &e = *g_ent[i];
	if (!e.t) { g_snd.note (196, 70, SOUND_SQUARE, 70); return; }
	if (strcmp (pinball::scores_setting (g_scores, "table", ""), e.section)) { pinball::scores_set_setting (g_scores, "table", e.section); save_scores (); }
	g_cur = i; g_mode = M_PLAY; g_overlay = O_NONE;
	g->start (*e.t, (now_ms () * 2654435761u) ^ 0x5EEDu);
	g_accUs = 0; g_tap = g_nudgeKey = false; g_shake = 0; g_msgUntil = 0; g_msg[0] = 0; g_layerFor = -1;
	memset (g_tn, 0, sizeof g_tn);
	g_sp.clear ();
}
static void toggle_sound ()
{
	g_snd.toggle ();
	pinball::scores_set_setting (g_scores, "sound", g_snd.mute ? "0" : "1");
	save_scores ();
	ui_menu_state (5, g_snd.mute ? 0 : UMS_CHECKED);
}
static void toggle_pause () { if (g_mode != M_PLAY) return; if (g_overlay == O_NONE) set_overlay (O_PAUSE); else if (g_overlay == O_PAUSE) set_overlay (O_NONE); }

static void game_over (long score)
{
	g_final = score;
	g_snd.lose ();
	message ("Fin de partie", 600000);
	if (pinball::scores_qualifies (g_scores, E ().section, score))
	{
		char name[pinball::NAMEL]; snprintf (name, sizeof name, "%s", pinball::scores_setting (g_scores, "name", TR ("Joueur")));
		ui_ask ((long) TR ("Nouveau record"), (long) TR ("Votre nom"), (long) name, sizeof name);
		char clean[pinball::NAMEL]; pinball::scores_clean_name (name, clean);
		g_rank = pinball::scores_add (g_scores, E ().section, score, clean);
		pinball::scores_set_setting (g_scores, "name", clean);
		save_scores ();
		E ().best = best_of (E ().section);
		set_overlay (O_SCORES);
	}
	else { g_rank = -1; set_overlay (O_OVER); }
}

// les sons des evenements de la table (les memes qu'Onyx)
static void play_sound (int k)
{
	switch (k)
	{
	case pinball::P_BUMPER: g_snd.note (880, 60, SOUND_SQUARE, 70); break;
	case pinball::P_SLING: g_snd.note (660, 45, SOUND_SQUARE, 60); break;
	case pinball::P_TARGET: case pinball::P_DROP: g_snd.note (523, 60, SOUND_TRIANGLE, 90); break;
	case pinball::P_BANK: g_snd.later (0, 523, 80); g_snd.later (90, 784, 140); break;
	case pinball::P_LANE: g_snd.note (1175, 35, SOUND_TRIANGLE, 60); break;
	case pinball::P_LANES: g_snd.later (0, 784, 70); g_snd.later (80, 988, 70); g_snd.later (160, 1175, 120); break;
	case pinball::P_RAMP: g_snd.later (0, 392, 60); g_snd.later (60, 523, 60); g_snd.later (120, 659, 110); break;
	case pinball::P_SAUCER: g_snd.note (262, 160, SOUND_TRIANGLE, 100); break;
	case pinball::P_EJECT: g_snd.note (180, 90, SOUND_NOISE, 90); break;
	case pinball::P_FLIPPER_UP: g_snd.note (140, 30, SOUND_NOISE, 45); break;
	case pinball::P_LAUNCH: g_snd.note (220, 140, SOUND_NOISE, 90); break;
	case pinball::P_DRAIN: g_snd.note (196, 120, SOUND_TRIANGLE, 110); g_snd.later (130, 147, 260, SOUND_TRIANGLE, 110); break;
	}
}

// ce qu'a dit le monde : sons, etincelles, messages
static void events ()
{
	const Table &t = T ();
	for (int i = 0; i < g->w.nev; i++)
	{
		const pinball::PEvent &e = g->w.ev[i];
		if (e.ball < 0 || e.ball >= pinball::MAXBALL) continue;
		float x = g_v.X (g->w.ball[e.ball].p.x), y = g_v.Y (g->w.ball[e.ball].p.y);
		if (e.kind == pinball::P_BUMPER) g_sp.burst (x, y, mix (t.circ[e.index].colour, 0xFFFFFF, 90), 10, 2.2f, 12, 0.02f, 1.3f);
		else if (e.kind == pinball::P_SLING) g_sp.burst (x, y, 0xFFFFFF, 7, 2, 9, 0.02f, 1.1f);
		else if (e.kind == pinball::P_DROP) g_sp.burst (x, y, t.tgt[e.index].colour, 12, 1.8f, 14, 0.05f, 1.4f);
		else if (e.kind == pinball::P_RAMP_OUT) g_sp.burst (x, y, 0xFFE080, 12, 1.6f, 14, 0.03f, 1.2f);
	}
	for (int i = 0; i < g->nev; i++)
	{
		const pinball::GEvent &e = g->ev[i];
		switch (e.kind)
		{
		case pinball::GE_SOUND: play_sound (e.index); break;
		case pinball::GE_MESSAGE: message (t.rule[e.index].message.get (g_lang)); break;
		case pinball::GE_MULTIBALL: if (e.index < 0) message ("MULTIBALL !"); g_snd.win (); break;
		case pinball::GE_EXTRABALL: if (e.index < 0) message ("Bille en plus !"); g_snd.win (); break;
		case pinball::GE_BALLSAVED: message ("Bille sauvee"); break;
		case pinball::GE_TILTWARN: message ("Attention au tilt", 2000, true); g_snd.note (98, 220, SOUND_SQUARE, 110); break;
		case pinball::GE_TILT: message ("TILT", 3000, true); g_snd.note (73, 500, SOUND_SQUARE, 120); break;
		case pinball::GE_BALLEND: message ("Bille perdue", 1500); break;
		case pinball::GE_NEWBALL: g_msgUntil = 0; break;
		case pinball::GE_GAMEOVER: game_over (e.value); break;
		}
	}
}

static void step_game (unsigned dt)
{
	if (g_mode != M_PLAY || g_cur < 0 || g_overlay != O_NONE) return;
	if (g->state == pinball::G_BALLEND && g_padEdge) g->skip ();
	if (g_padEdge & PAD_Y) g_nudgeKey = true;
	pinball::Input in;
	in.left = g_tl || (g_pad & (PAD_L | PAD_L2 | PAD_LEFT)); in.right = g_tr || (g_pad & (PAD_R | PAD_R2 | PAD_B)); in.plunger = g_tp || (g_pad & PAD_A);
	for (int i = 0; i < 256; i++) { if (g_role[i] == 1) in.left = true; else if (g_role[i] == 2) in.right = true; else if (g_role[i] == 3) in.plunger = true; }
	g_accUs += dt * 1000;
	int n = 0;
	while (g_accUs >= 16667 && n < 3)
	{
		in.tap = g_tap && !in.plunger;
		bool nd = g_nudgeKey && (g->state == pinball::G_PLAY || g->state == pinball::G_READY) && !g->tilted;
		g->frame (in, g_nudgeKey);
		g_tap = g_nudgeKey = false;
		if (nd) g_shake = 4;
		else if (g_shake > 0) g_shake--;
		events ();
		g_sp.step ();
		for (int b = 0; b < pinball::MAXBALL; b++)		// la trainee
		{
			const pinball::Ball &B = g->w.ball[b];
			if (B.state != pinball::B_PLAY) { g_tn[b] = 0; continue; }
			memmove (g_trail[b] + 1, g_trail[b], sizeof (Vec) * (TRAIL - 1));
			g_trail[b][0] = B.p;
			if (g_tn[b] < TRAIL) g_tn[b]++;
		}
		g_accUs -= 16667; n++;
		if (g_overlay != O_NONE || g_mode != M_PLAY) break;
	}
	if (n == 3 && g_accUs >= 16667) g_accUs = 0;
}

// ---- l'ecran de jeu --------------------------------------------------------------------------------------------------
static int panel_w (int W) { int p = W * 42 / 100; return p < 200 ? 200 : p > 280 ? 280 : p; }

// le decor : le plateau et le panneau, tout ce qui ne bouge pas
static void build_layer ()
{
	int W = (int) ui.w, H = (int) ui.h;
	const Table &t = T ();
	if (!g_layer.alloc (W, H)) return;
	g_layerFor = g_cur; g_layerW = W; g_layerH = H;
	int PW = panel_w (W), fw = W - PW;
	g_v = fit_view (t, 0, 0, fw, H);
	Img &im = g_layer.img;
	im.vgrad (0, 0, W, H, 0x20242C, 0x0C0E12);
	for (int x = 0; x < W; x += 4) im.rect (x, 0, 1, H, 0x000000, 26);
	g_layer.begin ();
	draw_static (g_layer, t, g_v, false);
	int tx = (int) g_v.ox, tw = (int) ((float) t.w * g_v.s);		// le plateau dans son coffre
	im.rect (tx - 3, 0, 3, H, 0x484E58); im.rect (tx + tw, 0, 3, H, 0x484E58);
	im.rect (tx - 1, 0, 1, H, 0x90969F); im.rect (tx + tw + 2, 0, 1, H, 0x22262C);
	int px = fw + 10, pw = PW - 20;
	im.rbox (fw + 2, 2, PW - 4, H - 4, 12, 0x2C3240, 0x161A22, 256, 0x586070);
	unsigned sw = E ().look.lampCol;
	im.disc ((float) (px + 7), 26, 5, sw); im.disc ((float) (px + 7), 26, 2, 0xFFFFFF, 180);
	textS (px + 18, 31, t.name.get (g_lang), 0xF0F2F8, pw > 230 ? 15 : 13);
	// l'afficheur : son boitier, ses points eteints
	int dx = px, dy = 42, dh = 76;
	im.rbox (dx, dy, pw, dh, 7, 0x04040A, 0x0A0A10, 256, 0x484E58);
	g_dmdW = (pw - 12) / 2; g_dmdH = (dh - 12) / 2;
	for (int y = 0; y < g_dmdH; y++) for (int x = 0; x < g_dmdW; x++) im.rect (dx + 6 + x * 2, dy + 6 + y * 2, 1, 1, 0x241808);
	if (!g_dmd.alloc (g_dmdW, g_dmdH)) { g_dmdW = 0; }
	g_dmdTop[0] = 0; g_dmdBot[0] = 1;
	// bonus, multiplicateur, record
	int sy = dy + dh + 8;
	im.rbox (px, sy, pw, 60, 7, 0x1C2028, 0x14181E, 256, 0x384048);
	text (px + 10, sy + 16, TR ("Bonus"), 0x98A0B0, 11); text (px + pw - 66, sy + 16, TR ("Multi"), 0x98A0B0, 11);
	text (px + 10, sy + 50, TR ("Record"), 0x98A0B0, 11);
	// le but
	int gy = sy + 68;
	text (px + 2, gy + 11, TR ("But"), AMBER, 12);
	int gh = wrap (px + 2, gy + 16, pw - 4, t.goal.get (g_lang), 0xC8CED8, 11, false);
	(void) gh;
	g_layer.end ();
}

static void update_dmd (int x, int y)
{
	if (!g_dmdW) return;
	char top[32], bot[64]; fmt (g->score, top, sizeof top);
	unsigned now = now_ms ();
	if (g_msgUntil && (int) (now - g_msgUntil) >= 0) g_msgUntil = 0;
	bool red = false;
	if (g_msgUntil) { snprintf (bot, sizeof bot, "%s", g_msg); red = g_msgRed; }
	else if (g->state == pinball::G_READY) snprintf (bot, sizeof bot, TR ("Bille %d : lancez-la !"), g->ball);
	else if (g->tilted) { snprintf (bot, sizeof bot, "TILT"); red = true; }
	else snprintf (bot, sizeof bot, TR ("Bille %d / %d"), g->ball, T ().balls);
	g_dmdCol = red ? 0xFF4030 : 0xFF9A20;
	if (strcmp (top, g_dmdTop) || strcmp (bot, g_dmdBot))
	{
		snprintf (g_dmdTop, sizeof g_dmdTop, "%s", top); snprintf (g_dmdBot, sizeof g_dmdBot, "%s", bot);
		g_dmd.begin ();
		ui_fill (0, 0, g_dmdW, g_dmdH, 0);
		int px = 18; while (px > 10 && textW (top, px) > g_dmdW - 2) px--;
		textC (g_dmdW / 2, 16, top, 0xFFFFFF, px);
		px = 10; while (px > 7 && textW (bot, px) > g_dmdW - 2) px--;
		textC (g_dmdW / 2, g_dmdH - 3, bot, 0xFFFFFF, px, true);
		g_dmd.end ();
	}
	Img im = window();
	unsigned dim = shade (g_dmdCol, 110);
	const Img &d = g_dmd.img;
	for (int j = 0; j < g_dmdH; j++)
		for (int i = 0; i < g_dmdW; i++)
		{
			if ((d.px[j * d.stride + i] & 255) < 96) continue;
			int X = x + i * 2, Y = y + j * 2;
			im.px[(long) Y * im.stride + X] = g_dmdCol;
			im.px[(long) Y * im.stride + X + 1] = dim;
			im.px[(long) (Y + 1) * im.stride + X] = dim;
		}
}

static void draw_panel (Img &im)
{
	int W = (int) ui.w, H = (int) ui.h, PW = panel_w (W), fw = W - PW, px = fw + 10, pw = PW - 20;
	const Table &t = T (); char b[64];
	update_dmd (px + 6, 48);
	int sy = 42 + 76 + 8;
	fmt (g->bonus, b, sizeof b); text (px + 10, sy + 34, b, 0xFFFFFF, 16);
	snprintf (b, sizeof b, "\xC3\x97%d", g->mult); text (px + pw - 66, sy + 34, b, AMBER, 16);
	long best = E ().best; if (best > 0) { fmt (best, b, sizeof b); textR (px + pw - 10, sy + 50, b, 0xE8ECF4, 12); }
	int nud = g->tilted ? 3 : g->nudges ();			// l'inclinaison
	for (int i = 0; i < 3; i++) im.disc ((float) (px + 62 + i * 12), (float) (sy + 46), 4, i < nud ? RED : 0x3A4048);
	// les regles
	int gy = sy + 68, gh = wrap (px + 2, gy + 16, pw - 4, t.goal.get (g_lang), 0, 11, false, true);
	int y = gy + 22 + gh, bottom = H - 112;
	for (int i = 0; i < t.nrule && y + 18 <= bottom; i++)
	{
		const pinball::Rule &r = t.rule[i]; const char *m = r.message.get (g_lang);
		if (!m[0]) continue;
		int x = px + 2; bool done = g->ruleDone[i];
		if (done) { im.disc ((float) (x + 5), (float) (y + 7), 5, 0x40C060); x += 14; }
		else if (r.count <= 6) for (int q = 0; q < r.count; q++) { im.disc ((float) (x + 4), (float) (y + 7), 3.4f, q < g->ruleCount[i] ? E ().look.lampCol : 0x3A4048); x += 9; }
		else { snprintf (b, sizeof b, "%d/%d", g->ruleCount[i], r.count); text (x, y + 11, b, 0xD0D6E0, 11); x += textW (b, 11) + 2; }
		ui_text_fit ((long) m, x + 4, y + 11, done ? 0x707888 : 0xD0D6E0, 11, F (false), px + pw - x - 4);
		y += 18;
	}
	// les boutons d'ecran
	bool ready = g->w.plungerBusy ();
	button (im, px, H - 104, pw, 46, B_LAUNCH, "Lancer", ready);
	button (im, px, H - 52, pw / 2 - 4, 42, B_NUDGE, "Secouer", false, 13);
	button (im, px + pw / 2 + 4, H - 52, pw / 2 - 4, 42, B_PAUSE, "Pause", false, 13);
}

static void card (Img &im, int w, int h, const char *title, int &x, int &y)
{
	int W = (int) ui.w, H = (int) ui.h;
	x = (W - w) / 2; y = (H - h) / 2;
	im.rbox (x, y, w, h, 14, 0x343C4C, 0x1C222C, 245, 0x8894A8);
	textCS (x + w / 2, y + 28, TR (title), 0xFFE0A0, 18);
	y += 40;
}

static void draw_play ()
{
	if (g_layerFor != g_cur || g_layerW != ui.w || g_layerH != ui.h) build_layer ();
	int dx = g_shake > 0 ? (g_shake & 1 ? 3 : -3) : 0, dy = g_shake > 0 ? -2 : 0;
	g_layer.toWindow (0, 0);
	Img im = window ();
	const Table &t = T ();
	View vv = g_v; vv.ox += (float) dx; vv.oy += (float) dy;
	int PW = panel_w ((int) ui.w);
	im.clip (0, 0, (int) ui.w - PW, (int) ui.h);
	draw_dynamic (im, t, E ().look, vv, g, g_tick);
	for (int b = 0; b < pinball::MAXBALL; b++)			// la trainee, de la lumiere
		for (int i = 2; i < g_tn[b]; i += 2)
		{
			float k = 1 - (float) i / (float) TRAIL;
			im.glow (vv.X (g_trail[b][i].x), vv.Y (g_trail[b][i].y), vv.L (t.ball * (0.6f + k)), shade (0x486890, (int) (k * 160)));
		}
	draw_balls (im, t, vv, &g->w);
	g_sp.draw (im);
	im.noClip ();
	g_nbtn = 0;
	draw_panel (im);
	char b[96], n[32], c[32];
	if (g->state == pinball::G_BALLEND && g_overlay == O_NONE)	// le compte du bonus
	{
		int w = 240, h = 84, x = ((int) ui.w - PW - w) / 2, y = (int) ui.h * 62 / 100;
		im.rbox (x, y, w, h, 10, 0x343C4C, 0x1C222C, 235, 0x8894A8);
		textC (x + w / 2, y + 22, TR ("Bonus"), 0x98A0B0, 13);
		fmt (g->bonus, n, sizeof n); fmt (g->lastBonus, c, sizeof c);
		if (g->tilted) snprintf (b, sizeof b, "TILT");
		else snprintf (b, sizeof b, "%s \xC3\x97 %d = %s", n, g->mult, c);
		textC (x + w / 2, y + 50, b, 0xFFFFFF, 18);
		textC (x + w / 2, y + 72, TR ("une touche : passer"), 0x98A0B0, 10, false);
	}
	if (g_overlay == O_NONE) return;
	im.dim (130);
	int x, y;
	switch (g_overlay)
	{
	case O_PAUSE:
		card (im, 250, 170, "Pause", x, y);
		button (im, x + 24, y + 4, 202, 44, B_RESUME, "Reprendre", true);
		button (im, x + 24, y + 58, 202, 44, B_BACK, "Retour aux tables");
		break;
	case O_OVER:
		card (im, 320, 170, "Fin de partie", x, y);
		fmt (g_final, n, sizeof n); textC (x + 160, y + 36, n, 0xFFFFFF, 26);
		button (im, x + 40, y + 60, 240, 44, B_BACK, "Retour aux tables", true);
		break;
	case O_SCORES:
	{
		card (im, 320, 268, "Meilleurs scores", x, y);
		pinball::ScoreLine sl[pinball::TOPN]; int k = pinball::scores_read (g_scores, E ().section, sl);
		for (int i = 0; i < pinball::TOPN; i++)
		{
			int ry = y + i * 30;
			if (i == g_rank) im.rbox (x + 12, ry, 296, 26, 6, 0xF0A030, 0xC06A18);
			snprintf (b, sizeof b, "%d", i + 1); text (x + 24, ry + 18, b, 0xD0D6E0, 13);
			if (i >= k) continue;
			text (x + 48, ry + 18, sl[i].name, 0xFFFFFF, 13, false);
			fmt (sl[i].score, n, sizeof n); textR (x + 296, ry + 18, n, 0xFFFFFF, 13);
		}
		button (im, x + 40, y + 164, 240, 44, B_BACK, "Retour aux tables", true);
		break;
	}
	}
}

// ---- le choix de la table ---------------------------------------------------------------------------------------------
// x, y : une image a soi (malloc) -> dessin de la table au repos
static uint32_t *table_image (const TableEntry &e, int w, int h)
{
	uint32_t *p = (uint32_t *) malloc ((size_t) w * (size_t) h * 4);
	if (!p) return 0;
	Layer l; l.img.set (p, w, h, w);
	l.img.rect (0, 0, w, h, shade (e.t->background, 90));
	View v = fit_view (*e.t, 0, 0, w, h);
	draw_static (l, *e.t, v, true);
	draw_dynamic (l.img, *e.t, e.look, v, 0, 0);
	return p;
}
static void blit (Img &im, const uint32_t *p, int w, int h, int x, int y)
{
	for (int j = 0; j < h; j++) if (y + j >= 0 && y + j < im.h) memcpy (im.px + (long) (y + j) * im.stride + x, p + (long) j * w, (size_t) w * 4);
}
static void draw_picker ()
{
	int W = (int) ui.w, H = (int) ui.h;
	if (g_pickDirty || g_pick.img.w != W || g_pick.img.h != H)
	{
		if (!g_pick.alloc (W, H)) return;
		Img &p = g_pick.img;
		for (int y = 0; y < H; y++)
		{
			uint32_t *r = p.px + (long) y * p.stride;
			for (int x = 0; x < W; x++)
			{
				float fx = (float) x / (float) W, fy = (float) y / (float) H, n = fbm (fx * 4, fy * 3, 7);
				float l = 0.28f + n * 0.55f - fy * 0.2f;
				r[x] = mix (0x0C1020, 0x303C78, (int) (clampf (l, 0, 1) * 256));
			}
		}
		g_pick.begin ();
		textS (22, 46, "Pinball", 0xFFD080, 32);
		textS (160, 46, TR ("Choisir une table"), 0xC8D0E0, 14);
		g_pick.end ();
		g_pickDirty = false;
	}
	g_pick.toWindow ();
	Img im = window ();
	g_nbtn = 0;
	int L = W * 45 / 100; if (L > 340) L = 340;
	int ch = 72, y = 62;
	for (int i = 0; i < g_nent && y + ch < H - 8; i++, y += ch + 6)
	{
		TableEntry &e = *g_ent[i];
		bool sel = i == g_sel;
		add_btn (14, y, L - 14, ch, B_ROW0 + i);
		im.rbox (14, y, L - 14, ch, 10, sel ? 0x4A5880 : 0x2A3044, sel ? 0x384468 : 0x1E2434, 235, sel ? 0xFFC060 : 0x586078);
		if (e.t)
		{
			int th = ch - 10, tw = th / 2 + 2;
			if (!e.thumb) { e.thumb = table_image (e, tw, th); e.tw = tw; e.th = th; }
			if (e.thumb) blit (im, e.thumb, e.tw, e.th, 20, y + 5);
			textS (20 + tw + 12, y + 28, e.t->name.get (g_lang), 0xFFFFFF, 15);
			char b[48], n[32];
			if (e.best > 0) { fmt (e.best, n, sizeof n); snprintf (b, sizeof b, TR ("Record : %s"), n); }
			else snprintf (b, sizeof b, "%s", TR ("Pas encore de record"));
			text (20 + tw + 12, y + 50, b, 0xB0B8C8, 11, false);
		}
		else
		{
			text (24, y + 26, e.file, 0xA0A8B8, 13);
			ui_text_fit ((long) e.err, 24, y + 48, RED, 11, F (false), L - 44);
		}
	}
	int rx = L + 16, rw = W - rx - 14;
	if (g_sel < g_nent && g_ent[g_sel]->t)
	{
		TableEntry &e = *g_ent[g_sel];
		int ph = H - 62 - 66, pw = ph / 2 + 2;
		if (pw > rw * 55 / 100) { pw = rw * 55 / 100; ph = pw * 2; }
		if (!e.prev || e.pw != pw || e.ph != ph) { free (e.prev); e.prev = table_image (e, pw, ph); e.pw = pw; e.ph = ph; }
		im.rbox (rx - 3, 59, pw + 6, ph + 6, 5, 0x687088, 0x383E50);
		if (e.prev) blit (im, e.prev, pw, ph, rx, 62);
		int ix = rx + pw + 12, iw = W - ix - 14, iy = 76;
		iy += wrap (ix, iy, iw, e.t->name.get (g_lang), 0xFFE0A0, 15, true) + 4;
		iy += wrap (ix, iy, iw, e.t->goal.get (g_lang), 0xD0D6E0, 12, false) + 12;
		text (ix, iy + 10, TR ("Meilleurs scores"), AMBER, 12); iy += 18;
		pinball::ScoreLine sl[pinball::TOPN]; int k = pinball::scores_read (g_scores, e.section, sl);
		if (!k) text (ix, iy + 10, TR ("Aucun pour l'instant"), 0x98A0B0, 11, false);
		for (int i = 0; i < k; i++)
		{
			char b[32]; fmt (sl[i].score, b, sizeof b);
			ui_text_fit ((long) sl[i].name, ix, iy + 11, 0xFFFFFF, 11, F (false), iw - 74);
			textR (ix + iw, iy + 11, b, 0xFFFFFF, 11); iy += 17;
		}
		button (im, rx, H - 56, rw, 44, B_PLAYIT, "Jouer", true, 16);
	}
}

// ---- les evenements ---------------------------------------------------------------------------------------------------
static void action (int id)
{
	switch (id)
	{
	case B_NUDGE: g_nudgeKey = true; break;
	case B_PAUSE: toggle_pause (); break;
	case B_RESUME: set_overlay (O_NONE); break;
	case B_BACK: go_picker (); break;
	case B_PLAYIT: start_game (g_sel); break;
	default:
		if (id >= B_ROW0 && id - B_ROW0 < g_nent) { if (g_sel == id - B_ROW0) start_game (g_sel); else g_sel = id - B_ROW0; }
		break;
	}
}
static void command (int id)
{
	switch (id)
	{
	case 1: if (g_mode == M_PLAY && g_cur >= 0) start_game (g_cur); else start_game (g_sel); break;
	case 2: toggle_pause (); break;
	case 3: if (g_mode == M_PLAY) { if (g_overlay == O_NONE) set_overlay (O_PAUSE); else if (g_overlay != O_PAUSE) go_picker (); } break;
	case 4:
	{
		char path[256] = "";
		if (g_mode == M_PLAY && g_overlay == O_NONE) set_overlay (O_PAUSE);
		ui_file_filter ((long) "*.table");
		if (ui_file_open ((long) HOMEDIR, (long) path, sizeof path) == 1 && path[0] && g_nent < MAXENT)
		{
			int i = find_entry (path);
			if (i < 0) { g_ent[g_nent] = load_entry (path, false); i = g_nent++; }
			if (g_ent[i]->t) start_game (i);
			else { g_mode = M_PICKER; g_cur = -1; g_overlay = O_NONE; g_sel = i; }
		}
		break;
	}
	case 5: toggle_sound (); break;
	case 6: g_quit = true; break;
	}
}
static void on_key (int code, int ch, bool down)
{
	if (code < 0 || code > 255) return;
	if (!down) { g_role[code] = 0; return; }
	int c = ch | 0x20;
	if (g_mode == M_PICKER)
	{
		if (code == K_ESC) g_quit = true;
		else if (code == K_UP) { if (g_sel > 0) g_sel--; }
		else if (code == K_DOWN) { if (g_sel < g_nent - 1) g_sel++; }
		else if (code == K_ENTER || code == K_SPACE) start_game (g_sel);
		else if (c == 's') toggle_sound ();
		return;
	}
	if (g_overlay == O_PAUSE)
	{
		if (c == 'p' || code == K_ESC || code == K_ENTER || code == K_SPACE) set_overlay (O_NONE);
		return;
	}
	if (g_overlay != O_NONE) { if (code == K_ENTER || code == K_SPACE || code == K_ESC) go_picker (); return; }
	if (g->state == pinball::G_BALLEND) { g->skip (); return; }
	if (g_role[code]) return;				// (la repetition du clavier)
	if (code == K_LEFT || c == 'z') g_role[code] = 1;
	else if (code == K_RIGHT || c == 'm') g_role[code] = 2;
	else if (code == K_SPACE || code == K_DOWN || code == K_ENTER) { g_role[code] = 3; g_tap = true; }
	else if (code == K_UP || c == 'n') g_nudgeKey = true;
	else if (c == 'p') toggle_pause ();
	else if (code == K_ESC) set_overlay (O_PAUSE);
	else if (c == 's') toggle_sound ();
}
static void on_mouse (int kind, int x, int y, int buttons)
{
	if (kind == 2)
	{
		g_tl = g_tr = g_tp = false; g_hot = 0;
		return;
	}
	if (kind != 1 || !(buttons & 1)) return;
	int id = hit_btn (x, y);
	if (id) { g_hot = id; if (id == B_LAUNCH) { g_tp = true; g_tap = true; } else action (id); return; }
	if (y < 30 && x < (long) ui.w - CAPW) { ui_window_drag (); return; }
	if (g_mode != M_PLAY) return;
	if (g_overlay != O_NONE) return;
	if (g->state == pinball::G_BALLEND) { g->skip (); return; }
	int fw = (int) ui.w - panel_w ((int) ui.w);
	if (x < fw) { if (x < fw / 2) g_tl = true; else g_tr = true; }
}
static void on_pad ()
{
	if (ui_ev_field (&ui, 4) == 0) return;			// (manette 0 : le clavier, deja en touches)
	unsigned b = (unsigned) ui_ev_field (&ui, 8);
	g_padEdge |= b & ~g_pad; g_pad = b;
	unsigned e = g_padEdge;
	if (g_mode == M_PICKER)
	{
		if ((e & PAD_UP) && g_sel > 0) g_sel--;
		if ((e & PAD_DOWN) && g_sel < g_nent - 1) g_sel++;
		if (e & (PAD_A | PAD_START)) start_game (g_sel);
		g_padEdge = 0;
		return;
	}
	if (g_overlay == O_PAUSE) { if (e & (PAD_START | PAD_B)) set_overlay (O_NONE); else if (e & PAD_A) set_overlay (O_NONE); else if (e & PAD_SELECT) go_picker (); g_padEdge = 0; return; }
	if (g_overlay != O_NONE) { if (e & (PAD_A | PAD_START)) go_picker (); g_padEdge = 0; return; }
	if (e & (PAD_START | PAD_SELECT)) { set_overlay (O_PAUSE); g_padEdge = 0; return; }
	if (e & PAD_A) g_tap = true;
}
static void one_event (long t)
{
	if (t == WEV_CLOSE) { g_quit = true; return; }
	if (t == WEV_RESIZE) { long c, k; uikit_w_event (&c, &k); g_pickDirty = true; g_sp.clear (); return; }
	if (t == WEV_MENU) { command (ui_ev_field (&ui, EV_CODE)); return; }
	if (t == WEV_FOCUS) { if (ui_ev_field (&ui, EV_KIND) == 0) { memset (g_role, 0, sizeof g_role); g_tl = g_tr = g_tp = false; g_pad = 0; if (g_mode == M_PLAY && g_overlay == O_NONE) set_overlay (O_PAUSE); } return; }
	if (t == WEV_MOUSE) { on_mouse (ui_ev_field (&ui, EV_KIND), ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), ui_ev_field (&ui, EV_CODE)); return; }
	if (t == WEV_PAD) { on_pad (); return; }
	if (t == WEV_KEY)
	{
		bool down = ui_ev_field (&ui, EV_KIND) != 0;
		if (down) { long id = ui_menu_key (0); if (id >= 0) { command ((int) id); return; } }
		on_key (ui_ev_field (&ui, EV_CODE), ui_ev_field (&ui, EV_CHAR), down);
	}
}

static const char MENU[] = "MPartie\n"
	"I1\tNouvelle partie\tCtrl+N\n"
	"I2\tPause\tP\n"
	"I3\tChoisir une table...\tEchap\n"
	"I4\tOuvrir un fichier de table...\tCtrl+O\n"
	"-\n"
	"I5c\tSon\tS\n"
	"-\n"
	"I6\tQuitter\n";

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/pinball.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	audiokit_kit_open ();
	g_lang = !strcmp ((const char *) ui_lang (), "fr");
	mkdir ("SD0:/home", 0777); mkdir (HOMEDIR, 0777);
	g_scores = gkv_load (SCORES);
	if (!g_scores) g_scores = gkv_new ();
	g_snd.open ();
	g_snd.mute = !strcmp (pinball::scores_setting (g_scores, "sound", "1"), "0");
	g = new Game;
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int wh = (int) (scr[1] - scr[2] - scr[3]) - 96; if (wh > 700) wh = 700;
	int ww = wh / 2 + 280; if (ww < 620) ww = 620;
	if (ww > scr[0] - 8) ww = (int) scr[0] - 8;
	ui_window_ex (-1, -1, ww, wh, (long) "Pinball", WF_RESIZABLE | WF_GLYPH_LIGHT | WF_BODY_DARK);
	ui_min_size (480, 360);
	ui_menu ((long) MENU);
	ui_menu_state (5, g_snd.mute ? 0 : UMS_CHECKED);
	load_entries (0);
	if (argc > 1 && ends_table (argv[1]) && g_nent < MAXENT)
	{
		int i = find_entry (argv[1]);
		if (i < 0) { g_ent[g_nent] = load_entry (argv[1], false); i = g_nent++; }
		g_sel = i;
		if (g_ent[i]->t) start_game (i);
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
		g_padEdge = 0;
		if (g_mode == M_PLAY && g_cur >= 0) draw_play ();
		else draw_picker ();
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
