/*
 * relics -- Relics, un jeu de plates-formes a defilement dans l'esprit des jeux d'exploration de la fin des annees 80 :
 * un explorateur traverse quatre mondes (la jungle, la pyramide, la forteresse de glace, le volcan), douze niveaux en
 * tout, entre pieges (pointes cachees, flechettes, rochers qui roulent, stalactites, pilons, jets de feu, sols qui
 * s'effondrent) et ennemis (gardiens, tireurs, chauves-souris, araignees), avec un pistolet a six coups et des
 * batons de dynamite (les murs fissures cedent), des cles pour les portes, des points de passage et des tresors.
 *
 * Sur le cadre des jeux en C (gamefb.h, gamedraw.h, gamepause.h, gamesound.h), comme Arkanoid et Bubbles : image
 * logique de 640 x 384, mise a l'echelle ; tout est genere par le programme (art.h), rien n'est lu ; les niveaux
 * sont dans levels.h (tools/relics/mklevels.py). Le monde est fait de tuiles de 32 points (une case : 20 unites de
 * jeu, grossies de ZK), dessinees au trait comme des tuiles de jeu et faites a la demande ; 20 x 12 cases se voient,
 * le decor du fond defile plus lentement (parallaxe), des fougeres passent devant. Progres et record :
 * HOME:/relics/progress.txt. Essai sur le PC, sans Korin : tools/relics/run.sh (images, et chaque niveau parcouru
 * par la physique de l'explorateur).
 *
 * Clavier : Gauche / Droite marcher, Haut / Bas grimper, Espace (ou Haut hors d'une echelle) sauter, X, Ctrl ou F
 * tirer, D, C ou Bas + tir poser la dynamite, Bas + Espace descendre d'une planche ; P, Echap ou || : la pause.
 * Manette : la croix, A sauter, B ou X tirer, Y la dynamite, Start la pause.
 */
#define GF_W		640
#define GF_H		384
#define GF_BAND_TOP	0x00141210
#define GF_BAND_BOT	0x00060504
#include "../../inc/gamepause.h"
#include <math.h>
#include <stdio.h>

#define TS		20			/* une case, en unites de jeu */
#define ZK		1.6f			/* le grossissement a l'ecran : une case fait TP = 32 points */
#define VW		(GF_W / ZK)		/* ce qui se voit du monde : 400 x 240 unites, 20 x 12 cases */
#define VH		(GF_H / ZK)
#define FK		1.8f			/* les personnages : dessines un peu plus grands que leur boite */
#define MAXW		240
#define MAXH		64
#define HUD_H		24
#define NWORLD		4

#define H_W		12			/* la boite de l'explorateur */
#define H_H		26
#define WALK		1.85f
#define GRAV		0.32f
#define JUMP_V		6.1f
#define MAXFALL		7.0f
#define CLIMB		1.6f

enum { T_NONE, T_BACK, T_ROCK, T_BRICK, T_PLAT, T_LADDER, T_SPIKE, T_SPIKEC, T_BREAK, T_CRUMBLE, T_LIQUID, T_DOOR, T_TORCH, T_DECO,
       T_TRAPF, T_JET, T_DARTL, T_DARTR };

static unsigned char g_map[MAXH][MAXW], g_bk[MAXH][MAXW];	/* la carte : les cases, le fond (1 : le mur) */
static int g_mw, g_mh, g_world;
static int g_iw, g_ih;					/* la taille du monde, en unites */

#include "art.h"
#include "levels.h"

#define NLEVELS		((int) (sizeof levels / sizeof levels[0]))

/* ---- la carte -------------------------------------------------------------------------------------------------- */
static int tile_at (int tx, int ty)
{
	if (tx < 0 || tx >= g_mw || ty < 0) return T_ROCK;
	if (ty >= g_mh) return T_NONE;
	return g_map[ty][tx];
}
static int tile_solid (int t)
{
	return t == T_ROCK || t == T_BRICK || t == T_BREAK || t == T_CRUMBLE || t == T_DOOR || t == T_TRAPF || t == T_JET || t == T_DARTL || t == T_DARTR;
}
static inline int solid_at (int tx, int ty) { return tile_solid (tile_at (tx, ty)); }
/* une planche, ou le haut d'une echelle : on s'y tient, on passe dessous */
static inline int oneway_at (int tx, int ty)
{
	int t = tile_at (tx, ty);
	return t == T_PLAT || (t == T_LADDER && tile_at (tx, ty - 1) != T_LADDER && !tile_solid (tile_at (tx, ty - 1)));
}
static inline int tile_pt (float x, float y) { return tile_at ((int) floorf (x / TS), (int) floorf (y / TS)); }
static inline int ladder_pt (float x, float y) { return tile_pt (x, y) == T_LADDER; }

/* ---- les mouvements ---------------------------------------------------------------------------------------- */
#define MV_GROUND	1
#define MV_WALL		2
#define MV_CEIL		4

/* une boite qui avance (x puis y) contre les cases pleines ; oneway : les planches portent */
static int move_box (float *px, float *py, float w, float h, float *vx, float *vy, int oneway)
{
	int r = 0;
	float x = *px, y = *py;
	if (*vx != 0)
	{
		float nx = x + *vx;
		int ty0 = (int) floorf (y / TS), ty1 = (int) floorf ((y + h - 0.01f) / TS);
		if (*vx > 0)
		{
			int tx = (int) floorf ((nx + w - 0.01f) / TS);
			for (int ty = ty0; ty <= ty1; ty++)
				if (solid_at (tx, ty)) { nx = (float) (tx * TS) - w; r |= MV_WALL; break; }
		}
		else
		{
			int tx = (int) floorf (nx / TS);
			for (int ty = ty0; ty <= ty1; ty++)
				if (solid_at (tx, ty)) { nx = (float) ((tx + 1) * TS); r |= MV_WALL; break; }
		}
		x = nx;
		if (r & MV_WALL) *vx = 0;
	}
	if (*vy != 0)
	{
		float ny = y + *vy;
		int tx0 = (int) floorf (x / TS), tx1 = (int) floorf ((x + w - 0.01f) / TS);
		if (*vy > 0)
		{
			int ty = (int) floorf ((ny + h - 0.01f) / TS), prev = (int) floorf ((y + h - 0.01f) / TS);
			for (int tx = tx0; tx <= tx1; tx++)
				if (solid_at (tx, ty) || (oneway && ty > prev && oneway_at (tx, ty)))
				{
					ny = (float) (ty * TS) - h;
					r |= MV_GROUND;
					*vy = 0;
					break;
				}
		}
		else
		{
			int ty = (int) floorf (ny / TS);
			for (int tx = tx0; tx <= tx1; tx++)
				if (solid_at (tx, ty)) { ny = (float) ((ty + 1) * TS); r |= MV_CEIL; *vy = 0; break; }
		}
		y = ny;
	}
	*px = x;
	*py = y;
	return r;
}

static inline int ov (float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
	return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* sur une ligne, de x0 a x1 a la hauteur y : rien de plein ? */
static int clear_row (float x0, float x1, float y)
{
	if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
	for (float x = x0; x <= x1; x += 8)
		if (solid_at ((int) floorf (x / TS), (int) floorf (y / TS))) return 0;
	return 1;
}

/* ---- les entites ------------------------------------------------------------------------------------------ */
enum { E_NONE, E_WALKER, E_SHOOTER, E_BAT, E_SPIDER, E_SHOT, E_DART, E_BOULDER, E_ROCK, E_CRUSHER, E_FLAME, E_SPIKES, E_DARTER,
       E_ITEM, E_CHECK, E_EXIT, E_BULLET, E_DYN, E_CRUMB, E_FERN };
enum { IT_COIN, IT_GEM, IT_VASE, IT_RELIC, IT_AMMO, IT_DYN, IT_KEY, IT_LIFE };

struct ent
{
	int type, on, kind, dir, st, t, tx, ty;
	float x, y, vx, vy, w, h, x0, y0, a, b;
};
#define NE		600
static struct ent g_e[NE];
static int g_ne;

static struct ent *ent_new (int type, float x, float y, float w, float h)
{
	struct ent *e = 0;
	int fly = type == E_BULLET || type == E_SHOT || type == E_DART || type == E_DYN;
	for (int i = 0; fly && i < g_ne; i++)		/* (une place libre parmi ce qui vole) */
	{
		int t = g_e[i].type;
		if (!g_e[i].on && (t == E_BULLET || t == E_SHOT || t == E_DART || t == E_DYN)) { e = &g_e[i]; break; }
	}
	if (!e)
	{
		if (g_ne >= NE) return 0;
		e = &g_e[g_ne++];
	}
	memset (e, 0, sizeof *e);
	e->type = type;
	e->on = 1;
	e->x = e->x0 = x;
	e->y = e->y0 = y;
	e->w = w;
	e->h = h;
	return e;
}

/* ---- l'etat du jeu ------------------------------------------------------------------------------------------ */
enum { GS_TITLE, GS_INTRO, GS_PLAY, GS_DEAD, GS_CLEAR, GS_OVER, GS_WIN, GS_PAUSE };

struct hero
{
	float x, y, vx, vy;
	int face, ground, climb, coyote, jbuf, drop, anim, shoot, crouch, inv, dead;
	float walk;
};
static struct hero g_h;
static int g_state, g_prev, g_timer;
static unsigned long g_tick;
static int g_level, g_sel, g_unlocked;
static unsigned long g_score, g_best, g_nextlife;
static int g_lives, g_ammo, g_dyns, g_keys;
static int g_treas, g_ntreas;
static unsigned long g_s_score;			/* au debut du niveau : pour le recommencer */
static int g_s_lives, g_s_ammo, g_s_dyns;
static float g_chkx, g_chky;			/* le point de reprise */
static float g_camx, g_camy;
static int g_shake;
static unsigned long g_bonus;
static int g_fire_cd;
static int g_sim, g_simdead;			/* (tools/relics : l'explorateur seul, sans effets) */

/* l'entree : ce qui est tenu, ce qui vient d'etre appuye */
#define IN_L		1
#define IN_R		2
#define IN_U		4
#define IN_D		8
#define IN_JUMP		16
#define IN_FIRE		32
#define IN_DYN		64
#define IN_OK		128
static unsigned g_in, g_inprev, g_edge;

/* les lueurs d'une explosion */
static struct { int life; float x, y; } g_fx[8];

/* ---- les images -------------------------------------------------------------------------------------------- */
static uint32_t *g_bg[NWORLD];
static gf_sprite_t *g_hero[2][HF_N], *g_hdead[2];
static gf_sprite_t *g_foe[NWORLD][2][5];		/* marche 0..3, tir */
static gf_sprite_t *g_sbat[NWORLD][2], *g_sspider[NWORLD][2], *g_sgem[NWORLD], *g_svase[NWORLD], *g_sexit[NWORLD];
static gf_sprite_t *g_sboulder[NWORLD][4], *g_sstal[NWORLD], *g_scrush[NWORLD], *g_sshot[NWORLD];
static gf_sprite_t *g_scoin, *g_srelic, *g_sammo, *g_sdyn[2], *g_skey, *g_sheart, *g_sbul, *g_scheck[2], *g_stspk, *g_sdart[2];
static gf_sprite_t *g_storch[4], *g_sjet[4];
static gf_sprite_t *g_lt_torch, *g_lt_small, *g_lt_big, *g_lt_relic, *g_lt_exit, *g_lt_lava, *g_lt_shot;
static gf_sprite_t *g_sarrow[2], *g_sfern[NWORLD];
static gf_sprite_t *g_hheart, *g_hbul, *g_hdyn, *g_hkey, *g_hcoin;	/* (les petites, du bandeau) */
static gf_sprite_t *g_tfg[MAXH][MAXW], *g_tbg[MAXH][MAXW];

/* un personnage en images (tools/relics/mksprites.py) : s'il est la, il remplace le dessine */
struct sset { int nw, ns, w, h, ax, ay; gf_sprite_t *f[2][32]; };
static struct sset *g_cfoe[NWORLD];
static const char *g_sprdir = "/apps/relics.app/sprites";

static struct sset *load_spr (const char *name)
{
	char p[300];
	snprintf (p, sizeof p, "%s/%s", g_sprdir, name);
	int fd = open (p, O_RDONLY);
	if (fd < 0) return 0;
	unsigned char hd[20];
	struct sset *ss = 0;
	if (read (fd, hd, 20) == 20 && !memcmp (hd, "RSPR", 4))
	{
		int n = hd[6] | hd[7] << 8, w = hd[8] | hd[9] << 8, h = hd[10] | hd[11] << 8;
		if (n > 0 && n <= 32 && w > 0 && h > 0 && w <= 256 && h <= 256 && hd[16] + hd[17] == n && hd[16])
		{
			ss = (struct sset *) calloc (1, sizeof *ss);
			ss->nw = hd[16]; ss->ns = hd[17]; ss->w = w; ss->h = h;
			ss->ax = hd[12] | hd[13] << 8; ss->ay = hd[14] | hd[15] << 8;
			for (int i = 0; i < n && ss; i++)
			{
				gf_sprite_t *s = ga_new ((unsigned long) w, (unsigned long) h);
				long want = (long) w * h * 4, got = 0, r;
				while (got < want && (r = read (fd, (char *) (s + 2) + got, (size_t) (want - got))) > 0) got += r;
				if (got < want) { free (ss); ss = 0; break; }
				ss->f[0][i] = s;
				ss->f[1][i] = sp_mirror (s, 0);
			}
		}
	}
	close (fd);
	return ss;
}	/* la tuile de chaque case, son fond */
static gf_sprite_t *g_dig[10], *g_digb[10], *g_slash;

/* les textes */
enum { TX_LOGO, TX_SUB, TX_PLAY, TX_CHOOSE, TX_HELP1, TX_HELP2, TX_HELP3, TX_BEST, TX_CLEAR, TX_TREAS, TX_BONUS, TX_OVER,
       TX_CONT, TX_WIN, TX_WIN2, TX_CHECK, TX_KEY, TX_DOOR, TX_LOCK, TX_LIFE, TX_SCORE, TX_READY, TX_NOAMMO, TX_NODYN, TX_RELIC,
       TX_FINAL, TX_N };
static const struct ga_txe s_tx[TX_N] = {
	{ "RELICS", 72, 1, 0x00FFF0B0, 0x00E08820 },
	{ "Les reliques perdues", 18, 0, 0x00F0E0C0, 0x00C8B490 },
	{ "Espace, Entrée ou A : jouer", 18, 1, 0x00FFFFFF, 0x00E0E8F0 },
	{ "Gauche, Droite : choisir le niveau", 13, 0, 0x00C8D0DC, 0x00A8B0C0 },
	{ "Flèches : marcher et grimper    Espace ou Haut : sauter", 13, 0, 0x00B0B8C4, 0x0098A0AC },
	{ "X, Ctrl ou F : tirer    D, C ou Bas + tir : dynamite", 13, 0, 0x00B0B8C4, 0x0098A0AC },
	{ "Bas + Espace : descendre d'une planche    P ou Échap : pause", 13, 0, 0x00B0B8C4, 0x0098A0AC },
	{ "Record", 14, 0, 0x00C8B890, 0x00A89870 },
	{ "Niveau terminé !", 30, 1, 0x00FFF0B0, 0x00F0A030 },
	{ "Trésors", 16, 0, 0x00E8E0D0, 0x00C0B8A8 },
	{ "Bonus", 16, 0, 0x00E8E0D0, 0x00C0B8A8 },
	{ "Partie terminée", 32, 1, 0x00FFB0A0, 0x00E04030 },
	{ "Espace ou A : continuer", 15, 1, 0x00FFFFFF, 0x00D8E0E8 },
	{ "Victoire !", 40, 1, 0x00FFF0B0, 0x00F0A030 },
	{ "Les douze reliques sont à l'abri. Bravo, explorateur !", 16, 0, 0x00F0E8D8, 0x00C8C0B0 },
	{ "Point de passage", 15, 1, 0x00FFE8A0, 0x00F0B040 },
	{ "Une clé !", 15, 1, 0x00FFE8A0, 0x00F0B040 },
	{ "La porte s'ouvre", 15, 1, 0x00FFE8A0, 0x00F0B040 },
	{ "Il faut une clé", 15, 1, 0x00FFC0B0, 0x00F07050 },
	{ "Une vie en plus !", 15, 1, 0x00FFC0D0, 0x00F05070 },
	{ "Score", 16, 0, 0x00E8E0D0, 0x00C0B8A8 },
	{ "Prêt ?", 16, 1, 0x00FFFFFF, 0x00D8E0E8 },
	{ "Plus de balles", 15, 1, 0x00FFC0B0, 0x00F07050 },
	{ "Plus de dynamite", 15, 1, 0x00FFC0B0, 0x00F07050 },
	{ "La relique !", 15, 1, 0x00FFF0A0, 0x00F0C030 },
	{ "Dernier niveau", 13, 0, 0x00FFD0A0, 0x00E09050 },
};
static gf_sprite_t *g_tx[TX_N];
static gf_sprite_t *g_tname[32], *g_tnameb[32], *g_tworld[NWORLD], *g_tnum[32];
static const gf_sprite_t *g_toast;
static int g_toast_t;

static void toast (int i) { g_toast = g_tx[i]; g_toast_t = 110; }

/* ---- le son ------------------------------------------------------------------------------------------------ */
static void snd_jump (void) { gs_note (330, 40, AKW_SQUARE, 50); gs_later (35, 440, 50, AKW_SQUARE, 40); }
static void snd_shoot (void) { gs_note (900, 30, AKW_NOISE, 90); gs_later (20, 220, 60, AKW_SQUARE, 50); }
static void snd_click (void) { gs_note (1200, 15, AKW_SQUARE, 40); }
static void snd_boom (void) { gs_note (90, 300, AKW_NOISE, 160); gs_later (60, 60, 380, AKW_NOISE, 120); }
static void snd_pick (void) { gs_note (988, 50, AKW_TRIANGLE, 90); gs_later (50, 1319, 90, AKW_TRIANGLE, 90); }
static void snd_relic (void) { static const struct gs_ent t[] = { { 0, 784, 90, AKW_TRIANGLE, 100 }, { 90, 988, 90, AKW_TRIANGLE, 100 }, { 180, 1175, 90, AKW_TRIANGLE, 100 }, { 270, 1568, 260, AKW_TRIANGLE, 110 }, { 0, 0, 0, 0, 0 } }; gs_seq (t); }
static void snd_kill (void) { gs_note (180, 90, AKW_NOISE, 80); gs_later (40, 110, 90, AKW_SQUARE, 60); }
static void snd_die (void) { static const struct gs_ent t[] = { { 0, 523, 100, AKW_SQUARE, 100 }, { 100, 392, 100, AKW_SQUARE, 100 }, { 200, 311, 100, AKW_SQUARE, 100 }, { 300, 196, 300, AKW_SQUARE, 110 }, { 0, 0, 0, 0, 0 } }; gs_seq (t); }
static void snd_dart (void) { gs_note (1500, 25, AKW_NOISE, 50); }
static void snd_trap (void) { gs_note (700, 20, AKW_SQUARE, 60); gs_later (30, 2000, 60, AKW_NOISE, 80); }
static void snd_rumble (void) { gs_note (55, 600, AKW_NOISE, 120); }
static void snd_check (void) { gs_note (659, 80, AKW_TRIANGLE, 90); gs_later (80, 880, 160, AKW_TRIANGLE, 90); }
static void snd_door (void) { gs_note (150, 120, AKW_SAW, 80); gs_later (120, 120, 180, AKW_SAW, 70); }
static void snd_start (void) { static const struct gs_ent t[] = { { 0, 392, 120, AKW_TRIANGLE, 100 }, { 130, 523, 120, AKW_TRIANGLE, 100 }, { 260, 659, 120, AKW_TRIANGLE, 100 }, { 390, 587, 120, AKW_TRIANGLE, 100 }, { 520, 784, 300, AKW_TRIANGLE, 110 }, { 0, 0, 0, 0, 0 } }; gs_seq (t); }

/* ---- progres ------------------------------------------------------------------------------------------------ */
#define P_DIR		"HOME:/relics"
#define P_FILE		"HOME:/relics/progress.txt"

static void load_progress (void)
{
	char b[64];
	int fd = open (P_FILE, O_RDONLY);
	g_unlocked = 0;
	g_best = 0;
	if (fd < 0) return;
	long n = read (fd, b, sizeof b - 1);
	close (fd);
	if (n <= 0) return;
	b[n] = 0;
	unsigned long u = 0, s = 0;
	if (sscanf (b, "%lu %lu", &u, &s) >= 1)
	{
		g_unlocked = u < (unsigned long) NLEVELS ? (int) u : NLEVELS - 1;
		g_best = s;
	}
}

static void save_progress (void)
{
	char b[64];
	mkdir ("HOME:", 0755);
	mkdir (P_DIR, 0755);
	int n = snprintf (b, sizeof b, "%d %lu\n", g_unlocked, g_best);
	int fd = open (P_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	if (write (fd, b, (size_t) n) < 0) { }
	close (fd);
}

static void add_score (unsigned long n)
{
	g_score += n;
	if (g_score >= g_nextlife)
	{
		g_nextlife += 15000;
		g_lives++;
		toast (TX_LIFE);
		gs_seq (gs_tab_win);
	}
	if (g_score > g_best) g_best = g_score;
}

/* ---- le chargement d'un niveau ------------------------------------------------------------------------ */
static char g_raw[MAXH][MAXW];

static int map_tile (char c)
{
	switch (c)
	{
	case '#': return T_ROCK;
	case '=': return T_BRICK;
	case '-': return T_PLAT;
	case 'H': return T_LADDER;
	case '^': return T_SPIKE;
	case 'v': return T_SPIKEC;
	case 'B': return T_BREAK;
	case 'C': return T_CRUMBLE;
	case '~': return T_LIQUID;
	case 'D': return T_DOOR;
	case 'i': return T_TORCH;
	case 'w': return T_DECO;
	case 't': return T_TRAPF;
	case 'f': return T_JET;
	case '<': return T_DARTL;
	case '>': return T_DARTR;
	case '.': return T_BACK;
	default: return -1;
	}
}

static void free_level (void)
{
	g_ne = 0;
}

/* au-dessus de (tx, ty), la premiere case pleine : sa rangee */
static int ceiling_above (int tx, int ty)
{
	while (ty > 0 && !solid_at (tx, ty - 1)) ty--;
	return ty;
}

static void parse_level (int n)
{
	const struct level *L = &levels[n];
	g_world = L->world;
	memset (g_raw, ' ', sizeof g_raw);
	int w = 0, h = 0, x = 0;
	for (const char *p = L->map; *p; p++)
	{
		if (*p == '\n') { if (x > w) w = x; x = 0; if (h < MAXH - 1) h++; continue; }
		if (x < MAXW && h < MAXH) g_raw[h][x] = *p;
		x++;
	}
	if (x) { if (x > w) w = x; h++; }
	g_mw = w > MAXW ? MAXW : w;
	g_mh = h > MAXH ? MAXH : h;
	/* le fond : le mur, ou le ciel */
	for (int ty = 0; ty < g_mh; ty++)
		for (int tx = 0; tx < g_mw; tx++)
		{
			char c = g_raw[ty][tx];
			if (c == '.') { g_bk[ty][tx] = 1; continue; }
			if (c == ' ') { g_bk[ty][tx] = 0; continue; }
			int nb = 0, ns = 0;
			for (int r = 1; r <= 3 && nb == ns; r++)
				for (int j = -r; j <= r; j++)
					for (int i = -r; i <= r; i++)
					{
						int X = tx + i, Y = ty + j;
						if (X < 0 || Y < 0 || X >= g_mw || Y >= g_mh) continue;
						if (g_raw[Y][X] == '.') nb++;
						else if (g_raw[Y][X] == ' ') ns++;
					}
			g_bk[ty][tx] = nb >= ns;
		}
	g_ne = 0;
	g_ntreas = 0;
	for (int ty = 0; ty < g_mh; ty++)
		for (int tx = 0; tx < g_mw; tx++)
		{
			char c = g_raw[ty][tx];
			int t = map_tile (c);
			g_map[ty][tx] = (unsigned char) (t >= 0 ? t : g_bk[ty][tx] ? T_BACK : T_NONE);
		}
	/* les entites (apres les cases : les araignees cherchent leur plafond) */
	for (int ty = 0; ty < g_mh; ty++)
		for (int tx = 0; tx < g_mw; tx++)
		{
			char c = g_raw[ty][tx];
			float X = (float) (tx * TS), Y = (float) (ty * TS), B = (float) ((ty + 1) * TS);
			struct ent *e = 0;
			switch (c)
			{
			case 'P':
				g_chkx = X + (TS - H_W) / 2;
				g_chky = B - H_H;
				break;
			case 'E': e = ent_new (E_EXIT, X - 8, B - 44, 36, 44); break;
			case '+': e = ent_new (E_CHECK, X + 4, B - 32, 12, 32); break;
			case '$': case '*': case 'a': case 'd': case 'k': case '1':
				e = ent_new (E_ITEM, X + 2, Y + 2, 16, 16);
				if (!e) break;
				e->kind = c == '*' ? IT_RELIC : c == 'a' ? IT_AMMO : c == 'd' ? IT_DYN : c == 'k' ? IT_KEY : c == '1' ? IT_LIFE : (int) (hashu (tx, ty, 1) % 3);
				if (e->kind == IT_RELIC) { e->x = X; e->y = B - 24; e->w = 20; e->h = 24; }
				if (e->kind <= IT_RELIC) g_ntreas++;
				e->a = (float) (hashu (tx, ty, 2) % 100) * 0.06f;
				break;
			case 'g': case 's':
				e = ent_new (c == 'g' ? E_WALKER : E_SHOOTER, X + (TS - H_W) / 2, B - H_H, H_W, H_H);
				if (e) e->dir = (hashu (tx, ty, 3) & 1) ? 1 : -1;
				break;
			case 'b':
				e = ent_new (E_BAT, X + 2, Y + 5, 16, 10);
				if (e)
				{
					int l = 0, r = 0;
					while (l < 6 && !solid_at (tx - l - 1, ty)) l++;
					while (r < 6 && !solid_at (tx + r + 1, ty)) r++;
					e->a = (float) ((r - l) * TS) * 0.5f;		/* le milieu de son vol */
					e->b = (float) ((r + l) * TS) * 0.5f;		/* son ampleur */
					e->x0 = X + 2 + e->a;
					e->t = (int) (hashu (tx, ty, 4) % 400);
				}
				break;
			case 'p':
				e = ent_new (E_SPIDER, X + 3, Y + 2, 14, 12);
				if (e)
				{
					int top = ceiling_above (tx, ty), d = 0;
					while (d < 7 && !solid_at (tx, ty + d + 1)) d++;
					e->a = (float) (top * TS);			/* le fil part de la */
					e->b = (float) (d * TS);			/* sa descente */
					e->t = (int) (hashu (tx, ty, 5) % 300);
				}
				break;
			case 'o': e = ent_new (E_BOULDER, X + 1, B - 18, 18, 18); break;
			case '!': e = ent_new (E_ROCK, X + 3, Y, 14, 18); break;
			case 'M':
				e = ent_new (E_CRUSHER, X - 2, Y, 24, 26);
				if (e)
				{
					int d = 0;
					while (d < 12 && !solid_at (tx, ty + d + 1)) d++;
					e->b = Y + (float) ((d + 1) * TS) - 26;		/* le plus bas */
					e->t = (int) (hashu (tx, ty, 6) % 120);
				}
				break;
			case 't': e = ent_new (E_SPIKES, X, Y, TS, TS); break;
			case 'f': e = ent_new (E_FLAME, X, Y, TS, TS); if (e) e->t = (int) (hashu (tx, ty, 7) % 160); break;
			case '<': case '>': e = ent_new (E_DARTER, X, Y, TS, TS); if (e) { e->dir = c == '>' ? 1 : -1; e->t = 30 + (int) (hashu (tx, ty, 8) % 60); } break;
			case 'C': e = ent_new (E_CRUMB, X, Y, TS, TS); break;
			case 'F': e = ent_new (E_FERN, X + 10 - 35, B - 42, 70, 45); break;
			}
			if (e) { e->tx = tx; e->ty = ty; }
		}
}

/* ---- les tuiles de chaque case ----------------------------------------------------------------------------- */
static inline int solid_look (int tx, int ty) { int t = tile_at (tx, ty); return tile_solid (t) && t != T_CRUMBLE; }

static int edge_mask (int tx, int ty)
{
	return (solid_look (tx, ty - 1) ? 0 : M_U) | (solid_look (tx, ty + 1) ? 0 : M_D) | (solid_look (tx - 1, ty) ? 0 : M_L) | (solid_look (tx + 1, ty) ? 0 : M_R);
}

/* au coeur d'un mur : 1 (rien de vide autour), 2 (rien a deux cases) */
static int deep_of (int tx, int ty)
{
	for (int r = 1; r <= 2; r++)
		for (int j = -r; j <= r; j++)
			for (int i = -r; i <= r; i++)
				if (!solid_look (tx + i, ty + j)) return r - 1;
	return 2;
}

static void tile_info (int tx, int ty)
{
	int t = g_map[ty][tx], w = g_world, mask = 0;
	unsigned h = hashu (tx, ty, 11);
	gf_sprite_t *fg = 0, *bg = 0;
	if (tile_solid (t) && t != T_CRUMBLE) mask = edge_mask (tx, ty);
	if (g_bk[ty][tx] && !(tile_solid (t) && t != T_CRUMBLE && !mask))
	{
		int sh = (solid_look (tx, ty - 1) ? 1 : 0) | (solid_look (tx - 1, ty) ? 2 : 0);
		bg = tile_get (w, TK_BACK, (int) (h % 3) + 3 * sh);
	}
	switch (t)
	{
	case T_ROCK:
		fg = tile_get (w, TK_ROCK, mask | (int) ((h >> 4) & 1) << 4 | (mask ? 0 : deep_of (tx, ty)) << 5);
		break;
	case T_BRICK:
	{
		int d = mask ? 0 : deep_of (tx, ty), m = (int) (h % 13);
		fg = tile_get (w, TK_BRICK, d ? 80 + d : mask + 16 * (m < 4 ? m + 1 : 0));
		break;
	}
	case T_TRAPF: fg = tile_get (w, tile_at (tx - 1, ty) == T_BRICK || tile_at (tx + 1, ty) == T_BRICK ? TK_TRAPB : TK_TRAPR, mask); break;
	case T_BREAK: fg = tile_get (w, TK_BREAK, mask); break;
	case T_DARTL: fg = tile_get (w, TK_DARTL, mask); break;
	case T_DARTR: fg = tile_get (w, TK_DARTR, mask); break;
	case T_JET: fg = tile_get (w, TK_JET, 0); break;
	case T_PLAT: fg = tile_get (w, TK_PLAT, (tile_at (tx - 1, ty) != T_PLAT ? 1 : 0) | (tile_at (tx + 1, ty) != T_PLAT ? 2 : 0)); break;
	case T_LADDER: fg = tile_get (w, TK_LADDER, 0); break;
	case T_SPIKE: fg = tile_get (w, TK_SPIKE, 0); break;
	case T_SPIKEC: fg = tile_get (w, TK_SPIKEC, 0); break;
	case T_CRUMBLE: fg = tile_get (w, TK_CRUMB, 0); break;
	case T_DOOR: fg = tile_get (w, TK_DOOR, tile_at (tx, ty + 1) != T_DOOR); break;
	case T_TORCH: fg = tile_get (w, TK_TORCH, 0); break;
	case T_DECO: fg = tile_get (w, TK_DECO, (int) (h & 3)); break;
	}
	g_tfg[ty][tx] = fg;
	g_tbg[ty][tx] = bg;
}

static void tiles_refresh (int tx0, int ty0, int tx1, int ty1)
{
	for (int ty = ty0 < 0 ? 0 : ty0; ty <= ty1 && ty < g_mh; ty++)
		for (int tx = tx0 < 0 ? 0 : tx0; tx <= tx1 && tx < g_mw; tx++)
			tile_info (tx, ty);
}

static void build_image (void)
{
	g_iw = g_mw * TS;
	g_ih = g_mh * TS;
	tiles_refresh (0, 0, g_mw - 1, g_mh - 1);
}

/* une case change : son image et celle de ses voisines */
static void set_tile (int tx, int ty, int t)
{
	if (tx < 0 || ty < 0 || tx >= g_mw || ty >= g_mh) return;
	g_map[ty][tx] = (unsigned char) t;
	tiles_refresh (tx - 2, ty - 2, tx + 2, ty + 2);
}

static void camera_snap (void);

static void place_hero (void)
{
	memset (&g_h, 0, sizeof g_h);
	g_h.x = g_chkx;
	g_h.y = g_chky;
	g_h.face = 1;
	g_h.ground = 1;
}

/* la reprise apres une chute : ce qui vole disparait, les ennemis reviennent a leur place, les pieges se rearment */
static void reset_actors (void)
{
	for (int i = 0; i < g_ne; i++)
	{
		struct ent *e = &g_e[i];
		switch (e->type)
		{
		case E_SHOT: case E_DART: case E_BULLET: case E_DYN: e->on = 0; break;
		case E_WALKER: case E_SHOOTER: case E_BAT: case E_SPIDER:
			if (e->on) { e->x = e->x0; e->y = e->y0; e->vx = e->vy = 0; e->st = 0; }
			break;
		case E_SPIKES: e->st = 0; e->t = 0; break;
		case E_BOULDER: if (e->on && e->st) e->on = 0; break;
		case E_ROCK: if (e->on && e->st) e->on = 0; break;
		}
	}
	for (int i = 0; i < 8; i++) g_fx[i].life = 0;
	ga_parts_clear ();
}

static void set_state (int s)
{
	g_state = s;
	g_timer = 0;
	gf_still = s == GS_PAUSE;
	gf_redraw = 1;
}

static void start_level (int n)
{
	free_level ();
	g_level = n;
	parse_level (n);
	build_image ();
	place_hero ();
	g_keys = 0;
	g_treas = 0;
	if (g_ammo < 6) g_ammo = 6;
	if (g_dyns < 6) g_dyns = 6;
	g_s_score = g_score;
	g_s_lives = g_lives;
	g_s_ammo = g_ammo;
	g_s_dyns = g_dyns;
	reset_actors ();
	camera_snap ();
	g_toast_t = 0;
	set_state (GS_INTRO);
	snd_start ();
}

static void new_game (int n)
{
	g_score = 0;
	g_nextlife = 15000;
	g_lives = 5;
	g_ammo = 6;
	g_dyns = 6;
	start_level (n);
}

static void restart_level (void)
{
	if (g_state == GS_TITLE)
	{
		new_game (g_sel);
		return;
	}
	g_score = g_s_score;
	g_lives = g_s_lives;
	g_ammo = g_s_ammo;
	g_dyns = g_s_dyns;
	start_level (g_level);
}

static void goto_title (void)
{
	free_level ();
	g_sel = g_unlocked;
	set_state (GS_TITLE);
}

/* ---- la camera ------------------------------------------------------------------------------------------------ */
static void camera_target (float *tx, float *ty)
{
	float x = g_h.x + H_W / 2 - VW / 2 + (float) g_h.face * 30, y = g_h.y + H_H / 2 - VH * 0.55f;
	float mx = (float) g_iw - VW, my = (float) g_ih - VH;
	if (mx < 0) x = mx / 2; else x = clampf (x, 0, mx);
	if (my < 0) y = my / 2; else y = clampf (y, -HUD_H * 0.0f, my);
	*tx = x;
	*ty = y;
}
static void camera_snap (void) { camera_target (&g_camx, &g_camy); }
static void camera_step (void)
{
	float tx, ty;
	camera_target (&tx, &ty);
	g_camx += (tx - g_camx) * 0.09f;
	g_camy += (ty - g_camy) * 0.12f;
}

/* ---- effets -------------------------------------------------------------------------------------------------- */
static void burst (float x, float y, uint32_t c, int n, int speed, int life, int size, int grav)
{
	ga_burst ((long) x, (long) y, c, n, (long) speed * 1024, life, size, (long) grav * 256);
}

static void flash (float x, float y)
{
	for (int i = 0; i < 8; i++)
		if (!g_fx[i].life) { g_fx[i].life = 18; g_fx[i].x = x; g_fx[i].y = y; return; }
}

static void kill_foe (struct ent *e)
{
	e->on = 0;
	burst (e->x + e->w / 2, e->y + e->h / 2, worlds[g_world].foe.shirt, 22, 160, 22, 2, 12);
	burst (e->x + e->w / 2, e->y + e->h / 2, 0x00FFF0C0, 8, 220, 10, 2, 0);
	add_score (e->type == E_SHOOTER ? 200 : 100);
	snd_kill ();
}

static void hero_die (void);

static void explode (float x, float y)
{
	const float R = 38;
	flash (x, y);
	g_shake = 16;
	snd_boom ();
	burst (x, y, 0x00FFD060, 40, 300, 22, 3, 6);
	burst (x, y, 0x00FF6020, 30, 200, 28, 2, 10);
	burst (x, y, 0x00807068, 24, 120, 40, 3, -2);
	for (int i = 0; i < g_ne; i++)
	{
		struct ent *e = &g_e[i];
		if (!e->on) continue;
		float dx = e->x + e->w / 2 - x, dy = e->y + e->h / 2 - y;
		if (dx * dx + dy * dy > (R + 8) * (R + 8)) continue;
		if (e->type == E_WALKER || e->type == E_SHOOTER || e->type == E_BAT || e->type == E_SPIDER) kill_foe (e);
		else if (e->type == E_BOULDER || e->type == E_ROCK)
		{
			e->on = 0;
			burst (e->x + e->w / 2, e->y + e->h / 2, worlds[g_world].rock, 20, 200, 26, 3, 14);
		}
	}
	int tx0 = (int) floorf ((x - R) / TS), tx1 = (int) floorf ((x + R) / TS), ty0 = (int) floorf ((y - R) / TS), ty1 = (int) floorf ((y + R) / TS);
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++)
		{
			if (tile_at (tx, ty) != T_BREAK) continue;
			float cx = (float) (tx * TS + TS / 2), cy = (float) (ty * TS + TS / 2);
			if ((cx - x) * (cx - x) + (cy - y) * (cy - y) > (R + 14) * (R + 14)) continue;
			set_tile (tx, ty, g_bk[ty][tx] ? T_BACK : T_NONE);
			burst (cx, cy, worlds[g_world].brick, 16, 180, 30, 3, 16);
		}
	if (g_state == GS_PLAY && !g_h.dead)
	{
		float dx = g_h.x + H_W / 2 - x, dy = g_h.y + H_H / 2 - y;
		if (dx * dx + dy * dy < (R + 2) * (R + 2)) hero_die ();
	}
}

/* ---- l'explorateur --------------------------------------------------------------------------------------- */
static void hero_die (void)
{
	if (g_sim) { g_simdead = 1; return; }
	if (g_h.dead || g_state != GS_PLAY) return;
	g_h.dead = 1;
	g_h.climb = 0;
	g_h.vy = -5.2f;
	g_h.vx = -0.6f * (float) g_h.face;
	snd_die ();
	burst (g_h.x + H_W / 2, g_h.y + H_H / 2, 0x00FFFFFF, 12, 160, 14, 2, 0);
	set_state (GS_DEAD);
}

static void fire (void)
{
	if (g_fire_cd) return;
	if (g_ammo <= 0) { snd_click (); toast (TX_NOAMMO); g_fire_cd = 14; return; }
	struct ent *b = ent_new (E_BULLET, g_h.face > 0 ? g_h.x + H_W + 2 : g_h.x - 8, g_h.y + 9, 6, 2);
	if (!b) return;
	b->vx = 7.5f * (float) g_h.face;
	b->t = 46;
	g_ammo--;
	g_h.shoot = 12;
	g_fire_cd = 14;
	snd_shoot ();
	burst (g_h.face > 0 ? g_h.x + H_W + 8 : g_h.x - 8, g_h.y + 10, 0x00FFE8A0, 5, 120, 6, 1, 0);
}

static void drop_dynamite (void)
{
	if (g_fire_cd || !g_h.ground || g_h.climb) return;
	if (g_dyns <= 0) { snd_click (); toast (TX_NODYN); g_fire_cd = 14; return; }
	struct ent *d = ent_new (E_DYN, g_h.x + H_W / 2 - 6 + (float) g_h.face * 6, g_h.y + H_H - 12, 12, 12);
	if (!d) return;
	d->t = 110;
	g_dyns--;
	g_h.crouch = 16;
	g_fire_cd = 18;
	gs_note (1800, 30, AKW_NOISE, 50);
}

/* une case qui tue : pointes, liquide */
static int hero_hazard (void)
{
	float x = g_h.x + 2, y = g_h.y + 3, w = H_W - 4, h = H_H - 3;
	int tx0 = (int) floorf (x / TS), tx1 = (int) floorf ((x + w) / TS), ty0 = (int) floorf (y / TS), ty1 = (int) floorf ((y + h) / TS);
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++)
		{
			int t = tile_at (tx, ty);
			float X = (float) (tx * TS), Y = (float) (ty * TS);
			if (t == T_SPIKE && ov (x, y, w, h, X + 3, Y + 11, TS - 6, 9)) return 1;
			if (t == T_SPIKEC && ov (x, y, w, h, X + 3, Y, TS - 6, 9)) return 1;
			if (t == T_LIQUID && ov (x, y, w, h, X, Y + (tile_at (tx, ty - 1) == T_LIQUID ? 0 : 7), TS, TS)) return 2;
		}
	return g_h.y > (float) g_ih + 40 ? 3 : 0;
}

static void open_door (int tx, int ty)
{
	int y0 = ty, y1 = ty;
	while (tile_at (tx, y0 - 1) == T_DOOR) y0--;
	while (tile_at (tx, y1 + 1) == T_DOOR) y1++;
	for (int y = y0; y <= y1; y++)
		set_tile (tx, y, g_bk[y][tx] ? T_BACK : T_NONE);
	burst ((float) (tx * TS + 10), (float) (y0 * TS + (y1 - y0 + 1) * 10), 0x00A07040, 20, 160, 26, 2, 10);
	snd_door ();
	toast (TX_DOOR);
}

static void hero_step (void)
{
	struct hero *H = &g_h;
	unsigned in = g_in, ed = g_edge;
	int L = (in & IN_L) != 0, R = (in & IN_R) != 0, U = (in & IN_U) != 0, D = (in & IN_D) != 0;
	if (L && R) L = R = 0;
	if (H->inv) H->inv--;
	if (H->shoot) H->shoot--;
	if (H->crouch) H->crouch--;
	if (g_fire_cd) g_fire_cd--;
	float cx = H->x + H_W / 2;
	int lad_body = ladder_pt (cx, H->y + 6) || ladder_pt (cx, H->y + H_H - 3);
	int lad_below = ladder_pt (cx, H->y + H_H + 2);
	int jump = (ed & IN_JUMP) || ((ed & IN_U) && !lad_body);
	if (jump) H->jbuf = 7;
	else if (H->jbuf) H->jbuf--;
	if (g_sim) g_fire_cd = 1;

	if (H->climb)
	{
		if (H->jbuf && (L || R))
		{					/* un saut hors de l'echelle */
			H->climb = 0;
			H->vy = -4.4f;
			H->vx = (R ? 1.0f : -1.0f) * WALK;
			H->face = R ? 1 : -1;
			H->jbuf = 0;
			snd_jump ();
		}
		else
		{
			int tx = (int) floorf (cx / TS);
			H->x += ((float) (tx * TS + (TS - H_W) / 2) - H->x) * 0.5f;
			H->vx = 0;
			H->vy = U ? -CLIMB : D ? CLIMB : 0;
			if (U || D) H->walk += 0.12f;
			int r = move_box (&H->x, &H->y, H_W, H_H, &H->vx, &H->vy, 0);
			float fcx = H->x + H_W / 2;
			if (U && !ladder_pt (fcx, H->y + H_H - 1) && ladder_pt (fcx, H->y + H_H + 1))
			{				/* en haut : debout sur l'echelle */
				H->y = floorf ((H->y + H_H + 1) / TS) * TS - H_H;
				H->climb = 0;
				H->ground = 1;
			}
			else if (!ladder_pt (fcx, H->y + 6) && !ladder_pt (fcx, H->y + H_H - 3) && !ladder_pt (fcx, H->y + H_H + 1))
				H->climb = 0;		/* plus d'echelle : on tombe */
			else if (D && (r & MV_GROUND))
				H->climb = 0;
			if (!H->climb) H->vy = 0;
		}
	}
	else if (U && lad_body && !H->crouch)
	{
		H->climb = 1;
		H->vx = 0;
		H->jbuf = 0;
	}
	else if (D && H->ground && lad_below && !lad_body)
	{
		H->climb = 1;
		H->vx = 0;
		H->y += 3;
	}

	if (!H->climb)
	{
		float target = (R ? WALK : 0) - (L ? WALK : 0);
		if (H->crouch) target = 0;
		float acc = H->ground ? 0.5f : 0.24f;
		if (H->vx < target) { H->vx += acc; if (H->vx > target) H->vx = target; }
		else if (H->vx > target) { H->vx -= acc; if (H->vx < target) H->vx = target; }
		if (R && !L) H->face = 1;
		if (L && !R) H->face = -1;
		if (H->ground) H->coyote = 6;
		else if (H->coyote) H->coyote--;
		if (H->drop) H->drop--;
		if (H->jbuf && H->coyote && !H->crouch)
		{
			int tx0 = (int) floorf (H->x / TS), tx1 = (int) floorf ((H->x + H_W - 0.01f) / TS), ty = (int) floorf ((H->y + H_H + 1) / TS);
			int onplank = H->ground && !solid_at (tx0, ty) && !solid_at (tx1, ty) && (oneway_at (tx0, ty) || oneway_at (tx1, ty));
			if (D && onplank)
			{
				H->drop = 10;		/* descendre de la planche */
				H->y += 1;
			}
			else
			{
				H->vy = -JUMP_V;
				snd_jump ();
			}
			H->coyote = 0;
			H->jbuf = 0;
			H->ground = 0;
		}
		if (!(in & (IN_JUMP | IN_U)) && H->vy < -2.4f) H->vy = -2.4f;	/* un petit saut */
		H->vy += GRAV;
		if (H->vy > MAXFALL) H->vy = MAXFALL;
		int r = move_box (&H->x, &H->y, H_W, H_H, &H->vx, &H->vy, !H->drop);
		H->ground = (r & MV_GROUND) != 0;
		if (H->ground) H->walk += fabsf (H->vx) * 0.075f;
		else H->walk = 0;
		/* une porte, contre elle, avec une cle */
		if ((r & MV_WALL) || (L || R))
		{
			int tx = (int) floorf ((H->face > 0 ? H->x + H_W + 1 : H->x - 1) / TS);
			for (int ty = (int) floorf (H->y / TS); ty <= (int) floorf ((H->y + H_H - 1) / TS); ty++)
				if (tile_at (tx, ty) == T_DOOR && (r & MV_WALL) && !g_sim)
				{
					if (g_keys > 0) { g_keys--; open_door (tx, ty); }
					else if (!g_toast_t || g_toast != g_tx[TX_LOCK]) toast (TX_LOCK);
					break;
				}
		}
	}
	/* tirer, poser la dynamite */
	if ((ed & IN_FIRE) && !D && !H->climb) fire ();
	if ((ed & IN_DYN) || ((ed & IN_FIRE) && D)) drop_dynamite ();
	/* le sol qui s'effondre */
	if (H->ground && !g_sim)
	{
		int ty = (int) floorf ((H->y + H_H + 1) / TS);
		for (int i = 0; i < g_ne; i++)
		{
			struct ent *e = &g_e[i];
			if (e->type != E_CRUMB || e->st || e->ty != ty) continue;
			if (H->x + H_W > e->x && H->x < e->x + TS) { e->st = 1; e->t = 26; gs_note (300, 40, AKW_NOISE, 40); }
		}
	}
	int hz = hero_hazard ();
	if (hz)
	{
		if (hz == 2 && !g_sim) burst (H->x + H_W / 2, H->y + H_H, worlds[g_world].liquid2, 24, 200, 20, 2, 12);
		hero_die ();
	}
}

/* la chute apres la mort : il saute en arriere et traverse l'ecran */
static void dead_step (void)
{
	g_h.vy += 0.28f;
	g_h.y += g_h.vy;
	g_h.x += g_h.vx;
	if (++g_timer < 95) return;
	g_lives--;
	if (g_lives <= 0)
	{
		save_progress ();
		set_state (GS_OVER);
		gs_seq (gs_tab_lose);
		return;
	}
	reset_actors ();
	place_hero ();
	g_h.inv = 90;
	if (g_ammo < 3) g_ammo = 3;
	if (g_dyns < 2) g_dyns = 2;
	camera_snap ();
	set_state (GS_PLAY);
}

/* ---- le monde ------------------------------------------------------------------------------------------------ */
static int hero_box_hit (float x, float y, float w, float h)
{
	if (g_state != GS_PLAY || g_h.dead) return 0;
	return ov (g_h.x + 2, g_h.y + 3, H_W - 4, H_H - 4, x, y, w, h);
}

/* l'explorateur se voit d'ici (meme hauteur, rien entre) ? */
static int sees_hero (const struct ent *e, float range)
{
	if (g_h.dead) return 0;
	float ey = e->y + 10, hy = g_h.y + 10, dx = g_h.x + H_W / 2 - (e->x + e->w / 2);
	if (fabsf (hy - ey) > 18 || fabsf (dx) > range) return 0;
	return clear_row (e->x + e->w / 2, g_h.x + H_W / 2, ey);
}

static void foe_walk (struct ent *e, float speed)
{
	e->vx = speed * (float) e->dir;
	e->vy += GRAV;
	if (e->vy > MAXFALL) e->vy = MAXFALL;
	int r = move_box (&e->x, &e->y, e->w, e->h, &e->vx, &e->vy, 1);
	if (r & MV_GROUND)
	{
		float fx = e->dir > 0 ? e->x + e->w + 2 : e->x - 2, fy = e->y + e->h + 3;
		int tx = (int) floorf (fx / TS), ty = (int) floorf (fy / TS);
		int t = tile_at (tx, ty), tf = tile_at (tx, (int) floorf ((e->y + e->h - 2) / TS));
		if ((!tile_solid (t) && !oneway_at (tx, ty)) || t == T_LIQUID || tf == T_SPIKE || tf == T_LIQUID) r |= MV_WALL;
	}
	if (r & MV_WALL) e->dir = -e->dir;
	e->a += fabsf (speed) * 0.08f;
	if (e->y > (float) g_ih + 40) e->on = 0;
}

static void shoot_at_hero (struct ent *e)
{
	struct ent *s = ent_new (E_SHOT, e->dir > 0 ? e->x + e->w + 2 : e->x - 8, e->y + 9, 6, 5);
	if (!s) return;
	s->vx = 3.2f * (float) e->dir;
	s->t = 220;
	gs_note (520, 40, AKW_TRIANGLE, 60);
}

static int on_screen (const struct ent *e, float m)
{
	return e->x + e->w > g_camx - m && e->x < g_camx + VW + m && e->y + e->h > g_camy - m && e->y < g_camy + VH + m;
}

static void world_step (void)
{
	float hcx = g_h.x + H_W / 2, hcy = g_h.y + H_H / 2;
	for (int i = 0; i < g_ne; i++)
	{
		struct ent *e = &g_e[i];
		if (!e->on) continue;
		switch (e->type)
		{
		case E_WALKER:
			if (!on_screen (e, 200)) break;
			foe_walk (e, 0.7f);
			if (hero_box_hit (e->x + 2, e->y + 3, e->w - 4, e->h - 3) && !g_h.inv) hero_die ();
			break;
		case E_SHOOTER:
			if (!on_screen (e, 200)) break;
			if (sees_hero (e, 240))
			{
				e->dir = hcx > e->x + e->w / 2 ? 1 : -1;
				e->vx = 0;
				e->vy += GRAV;
				move_box (&e->x, &e->y, e->w, e->h, &e->vx, &e->vy, 1);
				if (++e->st > 70) { e->st = 0; shoot_at_hero (e); e->t = 16; }
			}
			else
			{
				e->st = 40;
				foe_walk (e, 0.55f);
			}
			if (e->t) e->t--;
			if (hero_box_hit (e->x + 2, e->y + 3, e->w - 4, e->h - 3) && !g_h.inv) hero_die ();
			break;
		case E_BAT:
			e->t++;
			{
				float ph = (float) e->t * 0.018f;
				float nx = e->x0 + sinf (ph) * (e->b - 6);
				e->vx = nx - e->x;
				e->x = nx;
				e->y = e->y0 + sinf ((float) e->t * 0.09f) * 7;
			}
			if (hero_box_hit (e->x + 2, e->y + 1, e->w - 4, e->h - 2) && !g_h.inv) hero_die ();
			break;
		case E_SPIDER:
			e->t++;
			e->y = e->y0 + (0.5f - 0.5f * cosf ((float) e->t * 0.022f)) * (e->b - 4);
			if (hero_box_hit (e->x + 1, e->y + 1, e->w - 2, e->h - 2) && !g_h.inv) hero_die ();
			break;
		case E_SHOT: case E_DART:
			e->x += e->vx;
			if (--e->t <= 0 || solid_at ((int) floorf ((e->x + (e->vx > 0 ? e->w : 0)) / TS), (int) floorf ((e->y + e->h / 2) / TS)))
			{
				e->on = 0;
				burst (e->x + e->w / 2, e->y + e->h / 2, 0x00D0C0A0, 5, 100, 8, 1, 4);
				break;
			}
			if (hero_box_hit (e->x, e->y, e->w, e->h) && !g_h.inv) { e->on = 0; hero_die (); }
			break;
		case E_BULLET:
			e->x += e->vx;
			if (--e->t <= 0 || solid_at ((int) floorf ((e->x + (e->vx > 0 ? e->w : 0)) / TS), (int) floorf ((e->y + 1) / TS)))
			{
				e->on = 0;
				burst (e->x + (e->vx > 0 ? e->w : 0), e->y, 0x00FFE0A0, 6, 140, 8, 1, 6);
				break;
			}
			for (int j = 0; j < g_ne; j++)
			{
				struct ent *f = &g_e[j];
				if (!f->on || (f->type != E_WALKER && f->type != E_SHOOTER && f->type != E_BAT && f->type != E_SPIDER)) continue;
				if (ov (e->x, e->y - 2, e->w, e->h + 4, f->x, f->y, f->w, f->h)) { kill_foe (f); e->on = 0; break; }
			}
			break;
		case E_DYN:
			if (e->st == 0)
			{
				e->vy += GRAV;
				move_box (&e->x, &e->y, e->w, e->h, &e->vx, &e->vy, 1);
				if (e->t % 10 == 0) gs_note (2400, 8, AKW_SQUARE, 25);
				if (e->t % 3 == 0) ga_part ((long) (e->x + 8), (long) e->y, (long) (gf_below (40000)) - 20000, -40000, 0x00FFE080, 10, 1, 0);
				if (--e->t <= 0) { e->on = 0; explode (e->x + e->w / 2, e->y + e->h / 2); }
			}
			break;
		case E_BOULDER:
			if (e->st == 0)
			{
				float bcx = e->x + e->w / 2, bcy = e->y + e->h / 2;
				if (!g_h.dead && fabsf (hcx - bcx) < 7 * TS && fabsf (hcy - bcy) < 3 * TS && clear_row (bcx, hcx, bcy))
				{
					e->st = 1;
					e->dir = hcx > bcx ? 1 : -1;
					snd_rumble ();
					g_shake = 6;
				}
				break;
			}
			e->vx += 0.05f * (float) e->dir;
			if (fabsf (e->vx) > 1.65f) e->vx = 1.65f * (float) e->dir;
			e->vy += GRAV;
			{
				float ovx = e->vx;
				int r = move_box (&e->x, &e->y, e->w, e->h, &e->vx, &e->vy, 1);
				e->a += ovx / 9;
				if ((r & MV_WALL) || e->y > (float) g_ih + 40 || tile_pt (e->x + e->w / 2, e->y + e->h) == T_LIQUID)
				{
					e->on = 0;
					burst (e->x + e->w / 2, e->y + e->h / 2, worlds[g_world].rock, 26, 220, 30, 3, 14);
					gs_note (80, 200, AKW_NOISE, 120);
					g_shake = 8;
					break;
				}
				if ((g_tick & 15) == 0 && on_screen (e, 0)) g_shake = 2;
			}
			if (hero_box_hit (e->x + 2, e->y + 2, e->w - 4, e->h - 2)) hero_die ();
			for (int j = 0; j < g_ne; j++)
			{
				struct ent *f = &g_e[j];
				if (f->on && (f->type == E_WALKER || f->type == E_SHOOTER) && ov (e->x, e->y, e->w, e->h, f->x, f->y, f->w, f->h)) kill_foe (f);
			}
			break;
		case E_ROCK:
			if (e->st == 0)
			{
				float rcx = e->x + e->w / 2;
				if (!g_h.dead && fabsf (hcx - rcx) < 20 && g_h.y > e->y && g_h.y < e->y + 9 * TS)
				{
					int ok = 1;
					for (float y = e->y + e->h; y < g_h.y; y += 10)
						if (solid_at ((int) floorf (rcx / TS), (int) floorf (y / TS))) { ok = 0; break; }
					if (ok) { e->st = 1; e->t = 16; gs_note (240, 120, AKW_NOISE, 60); }
				}
				break;
			}
			if (e->st == 1)
			{
				if (--e->t <= 0) e->st = 2;
				break;
			}
			e->vy += 0.42f;
			if (e->vy > 8) e->vy = 8;
			{
				int r = move_box (&e->x, &e->y, e->w, e->h, &e->vx, &e->vy, 0);
				if ((r & MV_GROUND) || e->y > (float) g_ih)
				{
					e->on = 0;
					burst (e->x + e->w / 2, e->y + e->h, worlds[g_world].rock, 18, 200, 24, 2, 14);
					gs_note (400, 80, AKW_NOISE, 80);
					break;
				}
			}
			if (hero_box_hit (e->x + 2, e->y, e->w - 4, e->h)) hero_die ();
			break;
		case E_CRUSHER:
			e->t++;
			if (e->st == 0) { if (e->t > 100) { e->st = 1; e->vy = 0; } }
			else if (e->st == 1)
			{
				e->vy += 0.7f;
				e->y += e->vy;
				if (e->y >= e->b)
				{
					e->y = e->b;
					e->st = 2;
					e->t = 0;
					if (on_screen (e, 0)) { g_shake = 5; gs_note (70, 160, AKW_NOISE, 120); }
					burst (e->x + 4, e->y + e->h, 0x00A09080, 6, 120, 16, 2, 4);
					burst (e->x + e->w - 4, e->y + e->h, 0x00A09080, 6, 120, 16, 2, 4);
				}
			}
			else if (e->st == 2) { if (e->t > 40) e->st = 3; }
			else
			{
				e->y -= 0.9f;
				if (e->y <= e->y0) { e->y = e->y0; e->st = 0; e->t = 0; }
			}
			if (hero_box_hit (e->x + 2, e->y, e->w - 4, e->h - 1)) hero_die ();
			break;
		case E_FLAME:
		{
			int p = (int) ((g_tick + (unsigned long) e->t) % 170);
			e->st = p < 100 ? 0 : p < 124 ? 1 : 2;
			if (p == 124 && on_screen (e, 0)) gs_note (160, 400, AKW_NOISE, 70);
			if (e->st == 2 && hero_box_hit (e->x + 4, e->y - 44, 12, 44)) hero_die ();
			break;
		}
		case E_SPIKES:
			if (e->st == 0)
			{
				if (!g_h.dead && g_h.ground && g_h.x + H_W > e->x + 3 && g_h.x < e->x + TS - 3 && fabsf (g_h.y + H_H - e->y) < 2)
				{
					e->st = 1;
					e->t = 16;
					snd_click ();
				}
			}
			else if (e->st == 1) { if (--e->t <= 0) { e->st = 2; e->t = 50; snd_trap (); } }
			else if (e->st == 2)
			{
				if (hero_box_hit (e->x + 1, e->y - 14, TS - 2, 14)) hero_die ();
				if (--e->t <= 0) { e->st = 3; e->t = 40; }
			}
			else if (--e->t <= 0) e->st = 0;
			break;
		case E_DARTER:
			if (e->t) e->t--;
			if (!e->t && !g_h.dead && g_state == GS_PLAY)
			{
				float y0 = e->y, front = e->dir > 0 ? e->x + TS : e->x;
				float dx = (hcx - front) * (float) e->dir;
				if (g_h.y < y0 + TS - 2 && g_h.y + H_H > y0 + 2 && dx > 0 && dx < 13 * TS && clear_row (front + (float) e->dir * 4, hcx, y0 + 9))
				{
					struct ent *d = ent_new (E_DART, e->dir > 0 ? front : front - 12, y0 + 8, 12, 4);
					if (d) { d->vx = 4.4f * (float) e->dir; d->t = 300; }
					snd_dart ();
					e->t = 95;
				}
			}
			break;
		case E_CRUMB:
			if (e->st == 1)
			{
				if ((e->t & 3) == 0) ga_part ((long) (e->x + (float) gf_below (20)), (long) (e->y + 10), 0, 30000, worlds[g_world].rock, 18, 2, 6000);
				if (--e->t <= 0)
				{
					e->st = 2;
					e->t = 360;
					set_tile (e->tx, e->ty, g_bk[e->ty][e->tx] ? T_BACK : T_NONE);
					burst (e->x + 10, e->y + 6, ga_mix (worlds[g_world].rock, 0x00B09878, 110), 18, 120, 30, 3, 16);
					gs_note (120, 160, AKW_NOISE, 80);
				}
			}
			else if (e->st == 2 && --e->t <= 0)
			{
				if (!ov (g_h.x, g_h.y, H_W, H_H, e->x, e->y, TS, TS)) { e->st = 0; set_tile (e->tx, e->ty, T_CRUMBLE); }
				else e->t = 30;
			}
			break;
		case E_ITEM:
			e->a += 0.08f;
			if (hero_box_hit (e->x, e->y, e->w, e->h))
			{
				e->on = 0;
				float x = e->x + e->w / 2, y = e->y + e->h / 2;
				switch (e->kind)
				{
				case IT_COIN: case IT_GEM: case IT_VASE:
					add_score (e->kind == IT_COIN ? 200 : e->kind == IT_GEM ? 400 : 300);
					g_treas++;
					snd_pick ();
					burst (x, y, 0x00FFF0A0, 14, 160, 16, 2, 0);
					break;
				case IT_RELIC:
					add_score (2000);
					g_treas++;
					snd_relic ();
					toast (TX_RELIC);
					burst (x, y, 0x00FFE060, 40, 220, 30, 2, 0);
					flash (x, y);
					break;
				case IT_AMMO: g_ammo += 6; if (g_ammo > 30) g_ammo = 30; snd_pick (); break;
				case IT_DYN: g_dyns += 3; if (g_dyns > 30) g_dyns = 30; snd_pick (); break;
				case IT_KEY: g_keys++; snd_check (); toast (TX_KEY); break;
				case IT_LIFE: g_lives++; gs_seq (gs_tab_win); toast (TX_LIFE); break;
				}
			}
			break;
		case E_CHECK:
			if (!e->st && hero_box_hit (e->x - 4, e->y, e->w + 8, e->h))
			{
				e->st = 1;
				g_chkx = e->x + e->w / 2 - H_W / 2;
				g_chky = e->y + e->h - H_H;
				snd_check ();
				toast (TX_CHECK);
				burst (e->x + 6, e->y + 4, 0x00FFD060, 20, 160, 20, 2, -2);
			}
			break;
		case E_EXIT:
			if (g_state == GS_PLAY && !g_h.dead && ov (g_h.x + 4, g_h.y, 4, H_H, e->x + 10, e->y + 14, 16, 30))
			{
				g_bonus = (unsigned long) g_ammo * 50 + (unsigned long) g_dyns * 100 + (g_treas == g_ntreas ? 3000 : 0);
				add_score (g_bonus);
				if (g_level + 1 > g_unlocked && g_level + 1 < NLEVELS) g_unlocked = g_level + 1;
				save_progress ();
				gs_seq (gs_tab_win);
				set_state (g_level + 1 >= NLEVELS ? GS_WIN : GS_CLEAR);
			}
			break;
		}
	}
}

/* ---- le pas ------------------------------------------------------------------------------------------------ */
static void read_input (void)
{
	unsigned b = 0;
	unsigned long p = gp_now;
	const unsigned char *k = gf_held;
	if (k[0x50] || (p & PAD_LEFT)) b |= IN_L;
	if (k[0x4F] || (p & PAD_RIGHT)) b |= IN_R;
	if (k[0x52] || (p & PAD_UP)) b |= IN_U;
	if (k[0x51] || (p & PAD_DOWN)) b |= IN_D;
	if (k[0x2C] || (p & PAD_A)) b |= IN_JUMP;
	if (k[0xE0] || k[0xE4] || k[0x1B] || k[0x09] || (p & (PAD_B | PAD_X))) b |= IN_FIRE;
	if (k[0x07] || k[0x06] || (p & PAD_Y)) b |= IN_DYN;
	if (k[0x2C] || k[0x28] || k[0x58] || (p & PAD_A)) b |= IN_OK;
	g_inprev = g_in;
	g_in = b;
	g_edge = b & ~g_inprev;
}

static void step (void)
{
	g_tick++;
	read_input ();
	if (g_toast_t) g_toast_t--;
	for (int i = 0; i < 8; i++) if (g_fx[i].life) g_fx[i].life--;
	if (g_shake) g_shake--;
	switch (g_state)
	{
	case GS_TITLE:
		if (g_edge & IN_L) { if (g_sel > 0) { g_sel--; snd_click (); } }
		if (g_edge & IN_R) { if (g_sel < g_unlocked) { g_sel++; snd_click (); } }
		if (g_edge & (IN_OK | IN_FIRE)) new_game (g_sel);
		break;
	case GS_INTRO:
		g_timer++;
		world_step ();
		camera_step ();
		if (g_timer > 120 || (g_timer > 20 && (g_edge & (IN_OK | IN_FIRE | IN_L | IN_R | IN_U)))) set_state (GS_PLAY);
		break;
	case GS_PLAY:
		g_timer++;
		hero_step ();
		if (g_state == GS_PLAY || g_state == GS_DEAD) world_step ();
		camera_step ();
		break;
	case GS_DEAD:
		dead_step ();
		if (g_state == GS_DEAD) world_step ();
		break;
	case GS_CLEAR:
		g_timer++;
		if (g_timer > 50 && (g_edge & (IN_OK | IN_FIRE))) start_level (g_level + 1);
		break;
	case GS_OVER: case GS_WIN:
		g_timer++;
		if (g_timer > 50 && (g_edge & (IN_OK | IN_FIRE))) goto_title ();
		break;
	}
	ga_parts_step ();
	if (g_state != GS_TITLE)
		for (int i = 0; i < GA_NP; i++)		/* les debris s'arretent sur ce qui est plein */
		{
			struct ga_part_t *p = &ga_parts[i];
			if (p->life && p->vy > 0 && solid_at ((p->x >> 16) / TS, (p->y >> 16) / TS)) p->life = 0;
		}
}

/* ---- le dessin ----------------------------------------------------------------------------------------------
 * Le monde est en unites de jeu (une case : TS = 20) ; a l'ecran tout est grossi de ZK (une case : TP = 32 points) :
 * 20 x 12 cases se voient, et l'explorateur fait a peu pres une case de large. */
static int g_cx, g_cy;					/* la camera de cette image, en points de l'ecran (secousse comprise) */

static inline long SX (float x) { return (long) floorf (x * ZK) - g_cx; }
static inline long SY (float y) { return (long) floorf (y * ZK) - g_cy; }
static inline void blit_w (float x, float y, const gf_sprite_t *s) { ga_ablit (SX (x), SY (y), s); }
/* un personnage : centre sur sa boite (x, largeur), les pieds sur son bas */
static inline void blit_fig (float x, float w, float bottom, const gf_sprite_t *s)
{
	ga_ablit (SX (x + w / 2) - (long) (s[0] >> 1) + (long) (0.5f * FK), SY (bottom) - (long) s[1] + (long) (0.5f * FK), s);
}
static inline void glow_w (float cx, float cy, const gf_sprite_t *s) { gf_glow (SX (cx) - (long) (s[0] >> 1), SY (cy) - (long) (s[1] >> 1), s); }

/* un sprite opaque (une tuile du fond), recopie par rangees */
static void copy_tile (long x, long y, const gf_sprite_t *s)
{
	long w = s[0], h = s[1], c0 = x < 0 ? -x : 0, c1 = w, r0 = y < 0 ? -y : 0, r1 = h;
	if (c1 > GF_W - x) c1 = GF_W - x;
	if (r1 > GF_H - y) r1 = GF_H - y;
	if (c0 >= c1 || r0 >= r1) return;
	for (long r = r0; r < r1; r++)
		memcpy (gf_frame + (y + r) * GF_W + x + c0, s + 2 + r * w + c0, (size_t) (c1 - c0) * 4);
}

/* les rangees r0..r1 d'un sprite seulement (les pointes qui sortent) */
static void ablit_rows (long x, long y, const gf_sprite_t *s, long r0, long r1)
{
	long w = s[0];
	for (long r = r0; r < r1 && r < (long) s[1]; r++)
	{
		long Y = y + r - r0;
		if ((unsigned long) Y >= GF_H) continue;
		for (long c = 0; c < w; c++)
		{
			long X = x + c;
			if ((unsigned long) X >= GF_W) continue;
			uint32_t v = s[2 + r * w + c], a = v >> 24;
			if (!a) continue;
			uint32_t *p = gf_frame + Y * GF_W + X, o = *p, ia = 256 - a;
			*p = (v & 0xFFFFFF) + (((((o & 0xFF00FF) * ia) >> 8) & 0xFF00FF) | ((((o & 0xFF00) * ia) >> 8) & 0xFF00));
		}
	}
}

static void draw_bg (int world, int cx, int cy)
{
	if (!g_bg[world])				/* (fait a la premiere vue de son monde) */
	{
		g_bg[world] = (uint32_t *) malloc (BG_W * BG_H * 4);
		if (!g_bg[world]) { memset (gf_frame, 0, sizeof gf_frame); return; }
		gen_bg (g_bg[world], world);
	}
	const uint32_t *bg = g_bg[world];
	int bx = ((cx * 35 / 100) % BG_W + BG_W) % BG_W;
	int maxy = BG_H - GF_H, by = cy * 15 / 100 + maxy / 2;
	if (by < 0) by = 0;
	if (by > maxy) by = maxy;
	for (int y = 0; y < GF_H; y++)
	{
		const uint32_t *s = bg + (by + y) * BG_W;
		uint32_t *d = gf_frame + y * GF_W;
		int n1 = BG_W - bx;
		if (n1 >= GF_W) memcpy (d, s + bx, GF_W * 4);
		else { memcpy (d, s + bx, (size_t) n1 * 4); memcpy (d + n1, s, (size_t) (GF_W - n1) * 4); }
	}
}

/* les tuiles visibles : le fond (mur ou ciel), puis la case */
static void draw_tiles (void)
{
	int tx0 = (int) floorf ((float) g_cx / TP), ty0 = (int) floorf ((float) g_cy / TP);
	int f = (int) (g_tick / 10) & 3;
	for (int ty = ty0; ty <= ty0 + GF_H / TP + 1; ty++)
		for (int tx = tx0; tx <= tx0 + GF_W / TP + 1; tx++)
		{
			long x = (long) tx * TP - g_cx, y = (long) ty * TP - g_cy;
			if (tx < 0 || ty < 0 || tx >= g_mw || ty >= g_mh)
			{
				if (ty >= 0 && (tx < 0 || tx >= g_mw)) ga_ablit (x, y, tile_get (g_world, TK_ROCK, 32 * 2));	/* (le bord : du plein) */
				continue;
			}
			const gf_sprite_t *b = g_tbg[ty][tx], *s = g_tfg[ty][tx];
			if (b) copy_tile (x, y, b);
			if (g_map[ty][tx] == T_LIQUID)
				ga_ablit (x, y, tile_get (g_world, tile_at (tx, ty - 1) == T_LIQUID ? TK_LIQ : TK_LIQS, (f + tx) & 3));
			if (s) ga_ablit (x, y, s);
		}
}

/* ce qui bouge dans les cases : flammes des torches, lueur de la lave */
static void draw_tile_fx (void)
{
	const struct world *W = &worlds[g_world];
	int tx0 = g_cx / TP - 1, tx1 = (g_cx + GF_W) / TP + 1, ty0 = g_cy / TP - 1, ty1 = (g_cy + GF_H) / TP + 1;
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++)
		{
			if (ty < 0 || ty >= g_mh || tx < 0 || tx >= g_mw) continue;
			int t = g_map[ty][tx];
			float X = (float) (tx * TS), Y = (float) (ty * TS);
			if (t == T_TORCH)
			{
				int f = (int) ((g_tick / 5 + (unsigned long) tx * 3) & 3);
				blit_w (X + 5, Y - 5, g_storch[f]);
				glow_w (X + 10, Y + 2, g_lt_torch);
			}
			else if (t == T_LIQUID && W->lava && tile_at (tx, ty - 1) != T_LIQUID && ((tx + (int) (g_tick >> 4)) % 3) == 0)
				glow_w (X + 10, Y + 4, g_lt_lava);
		}
}

static void draw_foe (const struct ent *e)
{
	int d = e->dir < 0;
	const struct sset *ss = g_cfoe[g_world];
	if (ss)
	{
		int i;
		if (e->type == E_SHOOTER && ss->ns && (e->t || e->vx == 0)) i = ss->nw + (e->t ? (16 - e->t) * ss->ns / 17 : 0);
		else i = (int) (e->a * 2) % ss->nw;
		long ax = d ? ss->w - ss->ax : ss->ax;
		ga_ablit (SX (e->x + e->w / 2) - ax, SY (e->y + e->h) - ss->ay, ss->f[d][i]);
		return;
	}
	const gf_sprite_t *s;
	if (e->type == E_SHOOTER && (e->t || e->vx == 0)) s = g_foe[g_world][d][4];
	else s = g_foe[g_world][d][(int) e->a & 3];
	blit_fig (e->x, e->w, e->y + e->h, s);
}

static void draw_ents (int pass)
{
	float vx0 = (float) g_cx / ZK, vy0 = (float) g_cy / ZK;
	for (int i = 0; i < g_ne; i++)
	{
		const struct ent *e = &g_e[i];
		if (!e->on) continue;
		if (e->x + e->w + 60 < vx0 || e->x - 60 > vx0 + VW || e->y + e->h + 80 < vy0 || e->y - 80 > vy0 + VH) continue;
		if (pass == 2)
		{					/* devant tout : les fougeres */
			if (e->type == E_FERN) blit_w (e->x, e->y, g_sfern[g_world]);
			continue;
		}
		if (pass == 0)
		{					/* derriere : sortie, points de passage, pieges de cases */
			switch (e->type)
			{
			case E_EXIT:
				glow_w (e->x + 18, e->y + 26, g_lt_exit);
				blit_w (e->x, e->y, g_sexit[g_world]);
				break;
			case E_CHECK:
				blit_w (e->x, e->y, g_scheck[e->st]);
				if (e->st)
				{
					blit_w (e->x + 1, e->y - 12, g_storch[(g_tick / 5) & 3]);
					glow_w (e->x + 6, e->y, g_lt_torch);
				}
				break;
			case E_SPIKES:
				if (e->st >= 1)
				{
					int h = e->st == 1 ? 3 : e->st == 2 ? (e->t > 44 ? (50 - e->t) * 3 : 15) : e->t * 15 / 40;
					if (h > 15) h = 15;
					long hp = (long) ((float) h * ZK);
					if (hp > 0) ablit_rows (SX (e->x), SY (e->y) - hp, g_stspk, 0, hp);
				}
				break;
			case E_FLAME:
				if (e->st == 1 && (g_tick & 3) == 0)
					ga_part ((long) (e->x + 6 + (float) gf_below (8)), (long) e->y, 0, -60000, 0x00A0A0A0, 16, 2, 0);
				if (e->st == 2)
				{
					blit_w (e->x + 2, e->y - 44, g_sjet[(g_tick / 3) & 3]);
					glow_w (e->x + 10, e->y - 20, g_lt_big);
				}
				break;
			case E_CRUSHER:
			{
				gf_dst = gf_frame;
				long cx = SX (e->x + 12);
				for (long y = SY (e->y0); y < SY (e->y); y++)
					if ((unsigned long) y < GF_H && cx >= 2 && cx < GF_W - 2)
					{
						uint32_t c = ((y + g_cy) & 7) < 4 ? 0x00A0A4A8 : 0x00606468;
						gf_frame[y * GF_W + cx - 2] = 0x00181818;
						gf_frame[y * GF_W + cx - 1] = c;
						gf_frame[y * GF_W + cx] = ga_shade (c, 300);
						gf_frame[y * GF_W + cx + 1] = c;
						gf_frame[y * GF_W + cx + 2] = 0x00181818;
					}
				blit_w (e->x, e->y, g_scrush[g_world]);
				break;
			}
			}
			continue;
		}
		switch (e->type)
		{
		case E_WALKER: case E_SHOOTER: draw_foe (e); break;
		case E_BAT: blit_w (e->x - 5, e->y - 3, g_sbat[g_world][(g_tick >> 3) & 1]); break;
		case E_SPIDER:
		{
			gf_dst = gf_frame;
			long cx = SX (e->x + e->w / 2);
			for (long y = SY (e->a); y < SY (e->y) + 6; y++)
				if ((unsigned long) y < GF_H && (unsigned long) cx < GF_W) gf_frame[y * GF_W + cx] = ga_mix (gf_frame[y * GF_W + cx], 0x00E0E0E0, 160);
			blit_w (e->x - 4, e->y - 2, g_sspider[g_world][(g_tick >> 4) & 1]);
			break;
		}
		case E_SHOT:
			blit_w (e->x, e->y - 1, g_sshot[g_world]);
			glow_w (e->x + 3, e->y + 2, g_lt_shot);
			break;
		case E_DART: blit_w (e->x - 2, e->y - 1, g_sdart[e->vx > 0]); break;
		case E_BULLET:
			gf_dst = gf_frame;
			gf_rect (SX (e->x), SY (e->y), 10, 3, 0x00FFF4C0);
			glow_w (e->x + 3, e->y + 1, g_lt_shot);
			break;
		case E_DYN:
			blit_w (e->x - 2, e->y - 4, g_sdyn[(e->t >> 2) & 1]);
			if (e->t < 40 && (e->t & 4)) glow_w (e->x + 6, e->y + 4, g_lt_small);
			break;
		case E_BOULDER:
			blit_w (e->x - 1, e->y - 1, g_sboulder[g_world][((int) floorf (e->a * 4 / 6.2832f) % 4 + 4) % 4]);
			break;
		case E_ROCK:
			blit_w (e->x + (e->st == 1 ? (float) ((g_tick & 2) ? 1 : -1) : 0), e->y, g_sstal[g_world]);
			break;
		case E_ITEM:
		{
			float bob = sinf (e->a) * 2;
			const gf_sprite_t *s;
			switch (e->kind)
			{
			case IT_COIN: s = g_scoin; break;
			case IT_GEM: s = g_sgem[g_world]; break;
			case IT_VASE: s = g_svase[g_world]; break;
			case IT_RELIC: s = g_srelic; glow_w (e->x + 10, e->y + 10, g_lt_relic); break;
			case IT_AMMO: s = g_sammo; break;
			case IT_DYN: s = g_sdyn[0]; break;
			case IT_KEY: s = g_skey; break;
			default: s = g_sheart; break;
			}
			blit_w (e->x + (e->w - (float) s[0] / ZK) / 2, e->y + (e->h - (float) s[1] / ZK) / 2 + (e->kind == IT_RELIC ? 0 : bob), s);
			if ((e->kind == IT_GEM || e->kind == IT_COIN) && ((g_tick + (unsigned long) e->tx * 13) % 90) < 6)
				glow_w (e->x + 4, e->y + 5, g_lt_small);
			break;
		}
		}
	}
}

static void draw_hero (void)
{
	const struct hero *H = &g_h;
	if (H->inv && (H->inv & 4)) return;
	int d = H->face < 0;
	const gf_sprite_t *s;
	if (H->dead) s = g_hdead[d];
	else if (H->climb) s = g_hero[d][((int) H->walk & 1) ? HF_CLIMB1 : HF_CLIMB0];
	else if (H->crouch) s = g_hero[d][HF_DROP];
	else if (!H->ground) s = g_hero[d][H->vy < 0 ? HF_JUMP : HF_FALL];
	else if (H->shoot) s = g_hero[d][HF_SHOOT];
	else if (fabsf (H->vx) > 0.2f) s = g_hero[d][HF_WALK0 + ((int) H->walk & 3)];
	else s = g_hero[d][HF_STAND];
	if (H->dead) { blit_w (H->x - SPOX, H->y - SPOY, s); return; }
	blit_fig (H->x, H_W, H->y + H_H, s);
}

static void draw_parts (float k)
{
	gf_dst = gf_frame;
	for (int i = 0; i < GA_NP; i++)
	{
		struct ga_part_t *p = &ga_parts[i];
		if (!p->life) continue;
		uint32_t f = (uint32_t) p->life << 4;
		if (f > 256) f = 256;
		long sz = (long) ((float) p->size * k + 0.5f);
		gf_rect ((long) ((float) (p->x >> 16) * k) - g_cx, (long) ((float) (p->y >> 16) * k) - g_cy, sz, sz, ga_shade (p->col, f));
	}
	for (int i = 0; i < 8; i++)
		if (g_fx[i].life)
		{
			glow_w (g_fx[i].x, g_fx[i].y, g_lt_big);
			if (g_fx[i].life > 10) glow_w (g_fx[i].x, g_fx[i].y, g_lt_big);
		}
}

static void hud_num (unsigned long v, long x, long y) { ga_num (v, x, y, g_dig, 4); }

static void draw_hud (void)
{
	gf_dst = gf_frame;
	ga_panel (0, 0, GF_W, HUD_H, 0, 0x00302820, 0x00120E0A, 210, 0);
	gf_rect (0, HUD_H - 1, GF_W, 1, ga_mix (gf_tok[TJ_ACCENT / 4], 0x00C08040, 128));
	long x = 8;
	ga_ablit (x, 4, g_hheart);
	hud_num ((unsigned long) g_lives, x + 19, 2);
	x += 54;
	ga_ablit (x, 5, g_hbul);
	hud_num ((unsigned long) g_ammo, x + 11, 2);
	x += 48;
	ga_ablit (x, 4, g_hdyn);
	hud_num ((unsigned long) g_dyns, x + 18, 2);
	x += 52;
	if (g_keys)
	{
		ga_ablit (x, 6, g_hkey);
		if (g_keys > 1) hud_num ((unsigned long) g_keys, x + 18, 2);
		x += 40;
	}
	ga_ablit (x, 4, g_hcoin);
	ga_num ((unsigned long) g_treas, x + 19, 2, g_dig, 4);
	const gf_sprite_t *nm = g_tname[g_level];
	ga_shadow ((GF_W - (long) nm[0]) / 2 + 40 + 1, 4 + 2, nm);
	ga_ablit ((GF_W - (long) nm[0]) / 2 + 40, 4, nm);
	ga_num (g_score, GF_W - 44, 2, g_dig, 6);
}

static void panel (long cx, long y, long w, long h)
{
	gf_dst = gf_frame;
	ga_panel (cx - w / 2 + 5, y + 8, (unsigned long) w, (unsigned long) h, 18, 0, 0, 110, 0);
	ga_panel (cx - w / 2, y, (unsigned long) w, (unsigned long) h, 18, 0x003A3020, 0x00181208, 232, 0x00A08050);
}

static void draw_toast (void)
{
	if (!g_toast_t || !g_toast) return;
	long w = (long) g_toast[0] + 36, y = HUD_H + 10;
	int a = g_toast_t > 100 ? (110 - g_toast_t) * 25 : g_toast_t < 20 ? g_toast_t * 12 : 240;
	gf_dst = gf_frame;
	ga_panel (GF_W / 2 - w / 2, y, (unsigned long) w, 28, 14, 0x00403420, 0x00201810, (unsigned long) a, 0x00A08050);
	if (a > 120) ga_show (g_toast, GF_W / 2, y + 14 - (long) (g_toast[1] >> 1));
}

static void draw_play (void)
{
	int sx = 0, sy = 0;
	if (g_shake) { sx = (int) gf_below (7) - 3; sy = (int) gf_below (7) - 3; }
	g_cx = (int) floorf (g_camx * ZK) + sx;
	g_cy = (int) floorf (g_camy * ZK) + sy;
	draw_bg (g_world, g_cx, g_cy);
	draw_tiles ();
	draw_tile_fx ();
	draw_ents (0);
	draw_ents (1);
	draw_hero ();
	draw_parts (ZK);
	draw_ents (2);
	draw_hud ();
	draw_toast ();
	if (g_state == GS_INTRO)
	{
		long y = 120;
		panel (GF_W / 2, y, 440, 120);
		const gf_sprite_t *num = g_tnum[g_level], *wn = g_tworld[g_world], *nb = g_tnameb[g_level];
		ga_show (wn, GF_W / 2, y + 14);
		ga_show (nb, GF_W / 2, y + 40);
		ga_show (num, GF_W / 2, y + 82);
	}
	else if (g_state == GS_CLEAR || g_state == GS_WIN)
	{
		long y = g_state == GS_WIN ? 90 : 110;
		long h = g_state == GS_WIN ? 210 : 170;
		panel (GF_W / 2, y, 460, h);
		long ty = y + 16;
		if (g_state == GS_WIN) { ga_show (g_tx[TX_WIN], GF_W / 2, ty); ty += 58; ga_show (g_tx[TX_WIN2], GF_W / 2, ty); ty += 32; }
		else { ga_show (g_tx[TX_CLEAR], GF_W / 2, ty); ty += 50; }
		ga_show (g_tx[TX_TREAS], GF_W / 2 - 110, ty);
		ga_num ((unsigned long) g_treas, GF_W / 2 - 40, ty - 2, g_digb, 4);
		ga_ablit (GF_W / 2 - 16, ty - 2, g_slash);
		ga_num ((unsigned long) g_ntreas, GF_W / 2 + 4, ty - 2, g_digb, 4);
		ga_show (g_tx[TX_BONUS], GF_W / 2 + 90, ty);
		ga_num (g_bonus, GF_W / 2 + 130, ty - 2, g_digb, 4);
		ty += 34;
		if (g_timer > 50 && (g_timer & 32)) ga_show (g_tx[TX_CONT], GF_W / 2, ty);
	}
	else if (g_state == GS_OVER)
	{
		panel (GF_W / 2, 120, 420, 150);
		ga_show (g_tx[TX_OVER], GF_W / 2, 136);
		ga_show (g_tx[TX_SCORE], GF_W / 2 - 50, 194);
		ga_num (g_score, GF_W / 2 + 10, 192, g_digb, 4);
		if (g_timer > 50 && (g_timer & 32)) ga_show (g_tx[TX_CONT], GF_W / 2, 232);
	}
}

static void draw_title (void)
{
	int w = levels[g_sel].world;
	const int GY = 384 - 2 * TP;				/* le haut du sol */
	g_cx = (int) (g_tick * 2);
	g_cy = 0;
	draw_bg (w, g_cx / 2, 40);
	/* le sol : deux rangees de tuiles qui defilent */
	for (int i = -1; i <= GF_W / TP + 1; i++)
	{
		int tx = i + g_cx / TP;
		long x = (long) i * TP - g_cx % TP;
		ga_ablit (x, GY, tile_get (w, TK_ROCK, M_U | ((hashu (tx, 0, 1) & 1) << 4)));
		ga_ablit (x, GY + TP, tile_get (w, TK_ROCK, ((hashu (tx, 1, 1) & 1) << 4) | 32));
	}
	/* un rocher poursuit l'explorateur */
	{
		static gf_sprite_t *big;
		if (!big)
		{
			float k = sp_k;
			sp_k = 1;
			big = sp_new (72, 72);
			sp_disc (36, 36, 34, 0x00241A10);
			sp_disc_g (36, 36, 32, ga_shade (0x00908070, 320), ga_shade (0x00908070, 140));
			sp_disc (24, 22, 7, ga_shade (0x00908070, 380));
			sp_k = k;
		}
		long bx = 210 + (long) (sinf ((float) g_tick * 0.05f) * 6);
		ga_shadow (bx + 4, GY - 70, big);
		ga_ablit (bx, GY - 72, big);
		float a = (float) g_tick * 0.08f;
		gf_dst = gf_frame;
		for (int i = 0; i < 3; i++)
		{
			float q = a + (float) i * 2.1f;
			for (int r = 6; r < 28; r++)
			{
				long px = bx + 36 + (long) (cosf (q + (float) r * 0.02f) * (float) r), py = GY - 36 + (long) (sinf (q + (float) r * 0.02f) * (float) r);
				if ((unsigned long) px < GF_W && (unsigned long) py < GF_H) gf_frame[py * GF_W + px] = 0x00241A10;
			}
		}
		if ((g_tick & 3) == 0) ga_part (bx + 10 + (long) gf_below (50), GY - 2, -(long) gf_below (60000), -(long) gf_below (60000), 0x00908070, 20, 3, 6000);
	}
	int f = (int) (g_tick / 6) & 3;
	ga_ablit (330, GY - (long) g_hero[0][HF_WALK0 + f][1], g_hero[0][HF_WALK0 + f]);
	g_cx = g_cy = 0;
	draw_parts (1);
	/* le titre */
	ga_show (g_tx[TX_LOGO], GF_W / 2, 16);
	ga_show (g_tx[TX_SUB], GF_W / 2, 100);
	panel (GF_W / 2, 128, 470, 112);
	ga_show (g_tworld[w], GF_W / 2, 134);
	ga_show (g_tnameb[g_sel], GF_W / 2, 154);
	ga_show (g_tnum[g_sel], GF_W / 2, 188);
	if (g_sel > 0) ga_ablit (GF_W / 2 - 210, 160, g_sarrow[0]);
	if (g_sel < g_unlocked) ga_ablit (GF_W / 2 + 194, 160, g_sarrow[1]);
	if (g_tick & 32) ga_show (g_tx[TX_PLAY], GF_W / 2, 248);
	long bw = (long) g_tx[TX_BEST][0];
	ga_ablit (GF_W - 140 - bw, 6, g_tx[TX_BEST]);
	ga_num (g_best, GF_W - 128, 4, g_dig, 4);
	ga_show (g_tx[TX_CHOOSE], GF_W / 2, 214);
	gf_dst = gf_frame;
	ga_panel (0, GY + 6, GF_W, GF_H - GY - 6, 0, 0x00100C08, 0x00080604, 190, 0);
	ga_ablit_c (GF_W / 2, GY + 12, g_tx[TX_HELP1]);
	ga_ablit_c (GF_W / 2, GY + 30, g_tx[TX_HELP2]);
	ga_ablit_c (GF_W / 2, GY + 48, g_tx[TX_HELP3]);
}

static void draw (void)
{
	if (g_state == GS_TITLE) draw_title ();
	else draw_play ();
}

/* ---- les evenements ---------------------------------------------------------------------------------------- */
enum { ID_NEW = 1, ID_RESTART, ID_PAUSE, ID_SOUND, ID_QUIT };
static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tRecommencer le niveau\tCtrl+R\nI3\tPause\tP\nI4c\tSon\tS\n-\nI5\tQuitter\n";

static long game_pause (void)
{
	if (g_state != GS_PLAY && g_state != GS_INTRO && g_state != GS_DEAD) return 0;
	g_prev = g_state;
	g_state = GS_PAUSE;
	gf_still = 1;
	gf_redraw = 1;
	return 1;
}

static void game_resume (void)
{
	if (g_state != GS_PAUSE) return;
	g_state = g_prev;
	gf_still = 0;
	gf_redraw = 1;
	g_in = g_inprev = 0;
}

static void game_new (void) { gf_still = 0; restart_level (); }

static void toggle_sound (void) { ui_menu_state (ID_SOUND, gs_toggle () ? UMS_CHECKED : 0); }

static void do_menu (long id)
{
	if (id == ID_QUIT) gf_quit = 1;
	else if (id == ID_NEW) { gf_still = 0; if (gp_on) gp_close (); goto_title (); }
	else if (id == ID_RESTART) { if (g_state != GS_TITLE) { gf_still = 0; restart_level (); } }
	else if (id == ID_PAUSE) gp_open ();
	else if (id == ID_SOUND) toggle_sound ();
}

static void on_pad (void) { }

static void on_event (long t)
{
	if (gp_event (t)) return;
	if (t == WEV_MENU) do_menu ((uint32_t) GF_EV (EV_CODE));
	else if (t == WEV_KEY)
	{
		if (!GF_EV (EV_KIND)) return;
		long id = ui_menu_key (0);
		if (id >= 0) { do_menu (id); return; }
		unsigned ch = (uint32_t) GF_EV (EV_CHAR) | 0x20;
		if (ch == 'p') gp_open ();
		else if (ch == 's') toggle_sound ();
	}
	else if (t == WEV_MOUSE)
	{
		long mx, my;
		gf_mouse (&mx, &my);
		if (GF_EV (EV_KIND) != 1 || !(GF_EV (EV_CODE) & 1)) return;
		if (g_state == GS_TITLE)
		{
			if (my > 160 && my < 200 && mx < GF_W / 2 - 160) { if (g_sel > 0) g_sel--; }
			else if (my > 160 && my < 200 && mx > GF_W / 2 + 160) { if (g_sel < g_unlocked) g_sel++; }
			else new_game (g_sel);
		}
		else if ((g_state == GS_CLEAR || g_state == GS_OVER || g_state == GS_WIN) && g_timer > 50)
		{
			if (g_state == GS_CLEAR) start_level (g_level + 1);
			else goto_title ();
		}
	}
}

/* ---- la generation ---------------------------------------------------------------------------------------- */
static void gen_sprites (void)
{
	sp_k = ZK;						/* (les sprites du monde : grossis comme lui) */
	static const struct look hero = { 0x007A4E2A, 0x00E8B48A, 0x00D8C08A, 0x00C83A2A, 0x005A4632, 0x003A2A1E, 0x006A5030, 0x00404448, LK_HERO };
	sp_k = FK;
	for (int f = 0; f < HF_N; f++)
	{
		sp_new (SPW, SPH);
		draw_figure (&hero, &hero_pose[f], 0);
		g_hero[0][f] = sp_cur;
		g_hero[1][f] = sp_mirror (sp_cur, 0);
	}
	g_hdead[0] = sp_mirror (g_hero[0][HF_FALL], 1);
	g_hdead[1] = sp_mirror (g_hdead[0], 0);
	sp_k = ZK;
	for (int w = 0; w < NWORLD; w++)
	{
		const struct world *W = &worlds[w];
		sp_k = FK;
		for (int f = 0; f < 5; f++)
		{
			sp_new (SPW, SPH);
			draw_figure (&W->foe, &hero_pose[f < 4 ? HF_WALK0 + f : HF_SHOOT], f == 4);
			g_foe[w][0][f] = sp_cur;
			g_foe[w][1][f] = sp_mirror (sp_cur, 0);
		}
		sp_k = ZK;
		g_sbat[w][0] = gen_bat (W->bat, 1);
		g_sbat[w][1] = gen_bat (W->bat, 0);
		g_sspider[w][0] = gen_spider (W->spider, 0);
		g_sspider[w][1] = gen_spider (W->spider, 1);
		g_sgem[w] = gen_gem (W->gem);
		g_svase[w] = gen_vase (w == 2 ? 0x006080C0 : w == 3 ? 0x00805040 : 0x00B86038);
		g_sexit[w] = gen_exit (w == 3 ? 0x00707880 : W->brick);
		for (int r = 0; r < 4; r++) g_sboulder[w][r] = gen_boulder (ga_mix (W->rock, 0x00908070, 100), (float) r * 1.5708f);
		g_sstal[w] = gen_stalactite (w == 2 ? 0x00C0E0FF : W->rock);
		g_scrush[w] = gen_crusher (W->metal);
		sp_new (6, 6);
		sp_disc (3, 3, 2.6f, W->shot);
		sp_disc (2.2f, 2.2f, 1.0f, 0x00FFFFFF);
		g_sshot[w] = sp_cur;
	}
	g_scoin = gen_coin ();
	g_srelic = gen_relic ();
	g_sammo = gen_ammo ();
	g_sdyn[0] = gen_dynamite (0);
	g_sdyn[1] = gen_dynamite (1);
	g_skey = gen_key ();
	g_sheart = gen_heart ();
	g_sbul = gen_bullet_icon ();
	g_scheck[0] = gen_check (0);
	g_scheck[1] = gen_check (1);
	g_stspk = gen_trapspikes ();
	g_sdart[0] = gen_dart (0);
	g_sdart[1] = gen_dart (1);
	for (int f = 0; f < 4; f++)
	{
		g_storch[f] = gen_flame (10, 16, f, 0x00F07020, 0x00FFD040);
		g_sjet[f] = gen_flame (16, 46, f, 0x00F05018, 0x00FFC030);
	}
	g_lt_torch = sp_light (110, 0x00503010);
	g_lt_small = sp_light (34, 0x00A0A070);
	g_lt_big = sp_light (200, 0x00905020);
	g_lt_relic = sp_light (90, 0x00504010);
	g_lt_exit = sp_light (110, 0x00103850);
	g_lt_lava = sp_light (76, 0x00602808);
	g_lt_shot = sp_light (22, 0x00604830);
	for (int w = 0; w < NWORLD; w++)			/* les personnages en images, s'il y en a */
	{
		char n[32];
		snprintf (n, sizeof n, "foe%d.spr", w);
		g_cfoe[w] = load_spr (n);
	}
	sp_k = 1;						/* (le bandeau, le titre : a leur taille) */
	g_hheart = gen_heart ();
	g_hbul = gen_bullet_icon ();
	g_hdyn = gen_dynamite (0);
	g_hkey = gen_key ();
	g_hcoin = gen_coin ();
	for (int w = 0; w < NWORLD; w++) g_sfern[w] = gen_fern (w);
	g_sarrow[0] = sp_new (16, 22);
	sp_tri (14, 1, 14, 21, 2, 11, 0x00FFD070);
	sp_outline (0x00201008, 0.9f);
	g_sarrow[1] = sp_mirror (g_sarrow[0], 0);
}

static void gen_texts (void)
{
	char b[160];
	ga_texts (s_tx, g_tx, TX_N);
	ga_digits (g_dig, 15, (long) ui.font_b, 0x00FFFFFF, 0x00D8D0C0);
	ga_digits (g_digb, 18, (long) ui.font_b, 0x00FFF0C0, 0x00F0B040);
	g_slash = ga_text ("/", 18, (long) ui.font_b, 0x00C8C0B0, 0x00A09888);
	for (int w = 0; w < NWORLD; w++)
		g_tworld[w] = ga_text (worlds[w].name, 15, (long) ui.font, 0x00E8D8B0, 0x00C0A878);
	for (int i = 0; i < NLEVELS && i < 32; i++)
	{
		g_tname[i] = ga_text (levels[i].name, 13, (long) ui.font_b, 0x00F0E8D8, 0x00C8C0B0);
		g_tnameb[i] = ga_text (levels[i].name, 26, (long) ui.font_b, 0x00FFFFFF, 0x00E8D8C0);
		snprintf (b, sizeof b, "%s %d - %d", gf_tr ("Niveau"), levels[i].world + 1, i % 3 + 1);
		g_tnum[i] = ga_text (b, 14, (long) ui.font, 0x00D0C8B8, 0x00A8A090);
	}
}

#ifndef RELICS_NO_MAIN
int main (void)
{
	if (gf_open ("Relics") < 0)
		return 0;
	ui_menu ((long) s_menus);
	ui_menu_state (ID_SOUND, UMS_CHECKED);
	gs_open ();
	if (!ga_init (14u << 20))
		return 0;
	gen_sprites ();
	load_progress ();
	gp_init (game_pause, game_resume, game_new, GF_W - 32, 0, on_pad);
	ui_canvas_begin (gf_cv);
	gen_texts ();
	ui_canvas_end ();
	goto_title ();
	gf_run (on_event, step, draw);
	gs_close ();
	return 0;
}
#endif
