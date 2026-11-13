/*
 * arkanoid -- un casse-briques neuf dans l'esprit de LBreakout2 : briques en relief et ombre portee, decors generes
 * par niveau, lumieres qui derivent, balles a trainee, debris, briques dures, indestructibles et explosives, capsules
 * de bonus. Souris, doigt ou fleches ; Espace, clic ou appui : lancer (et tirer au laser). Manette : la croix deplace
 * la raquette, A, B, X, Y (ou haut) lancent et tirent. Plein ecran ; Start, Echap, P ou le bouton || : le menu de
 * pause (gamepause.h).
 *
 * Couche fixe (gf_layer) : decor, ombres, briques, bandeau, messages ; refaite quand elle change. Octet haut d'un
 * point de la couche : 0xFF premier plan (ni ombre ni lumiere dessus), 0x80 deja dans l'ombre, 0 decor. Taille
 * logique 640 x 480 ; tout est genere au demarrage, rien n'est lu.
 *
 * Port en C d'arkanoid.asm (memes niveaux, memes regles, memes dessins, memes sons, meme record
 * SD0:/home/arkanoid/best.txt), un seul source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa
 * validation sur le Pi.
 */
#define GF_W		640
#define GF_H		480
#include "../../inc/gamepause.h"

#define HUD_H		28
#define FIELD_L		20
#define FIELD_R		620
#define FIELD_T		36
#define BRICK_T		48
#define BW		40
#define BH		18
#define BSW		(BW - 1)		/* sprite d'une brique */
#define BSH		(BH - 1)
#define COLS		15
#define ROWS		14
#define NCELL		(COLS * ROWS)
#define SHADOW		5
#define RKT_Y		446
#define RKT_H		14
#define BALL_R		6			/* rayon d'une balle */
#define BALL_D		13
#define NB		8			/* balles */
#define NP		240			/* particules */
#define NC		8			/* capsules */
#define NL		10			/* tirs de laser */
#define NF		8			/* eclairs */
#define NM		20			/* poussieres */
#define HIST		8			/* trainee */
#define CAP_W		30
#define CAP_H		16
#define ORB_D		128
#define FLASH_D		72
#define BANK_SIZE	(512 * 1024)

enum { ST_TITLE, ST_READY, ST_PLAY, ST_PAUSE, ST_LOST, ST_CLEAR, ST_OVER };

/* briques (genre + 1 dans les cases) */
#define BK_SILVER	8
#define BK_GOLD		10
#define BK_STEEL	13
#define BK_BOMB		14
#define NBRICK		15

/* capsules */
enum { CP_WIDE, CP_NARROW, CP_MULTI, CP_SLOW, CP_FAST, CP_THRU, CP_GLUE, CP_LASER, CP_LIFE, NCAP };

enum { ID_NEW = 1, ID_PAUSE, ID_SOUND, ID_QUIT };

#define KEY_ENTER_	0x28
#define KEY_SPACE_	0x2C
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50

#define P_DIR		"SD0:/home/arkanoid"
#define P_BEST		"SD0:/home/arkanoid/best.txt"

/* une balle */
struct ball
{
	long on;
	long x, y;				/* position 16.16 */
	long dx, dy;				/* direction 1.14 */
	long stuck;
	long off;				/* collee : decalage depuis la raquette */
	long t;
	unsigned long hi;			/* dernier point de la trainee */
	uint32_t h[HIST][2];			/* trainee : x, y (pixels) */
};

/* une particule */
struct part { int32_t x, y, vx, vy, life; uint32_t col, kind, pad; };	/* 16.16 ; 0 etincelle, 1 debris */

/* ---- l'etat du jeu ---- */
static long g_state, g_prev, g_timer;
static unsigned long g_tick;
static long g_level, g_lives;
static unsigned long g_score, g_best;
static long g_px, g_psize, g_pw;		/* raquette : gauche, taille (0..3), largeur */
static long g_mx, g_mmoved, g_btn, g_keyspd;
static long g_t_slow, g_t_fast, g_t_thru, g_t_glue, g_t_laser;	/* effets : images restantes */
static long g_laser_cd;
static long g_left;				/* briques a casser */
static long g_combo;
static long g_loop;				/* images sans rien casser */
static long g_fdirty, g_hdirty, g_ldirty;	/* a refaire : briques, bandeau, toute la couche */
static const char *g_label;			/* le dernier bonus (texte) */
static long g_label_t;
static unsigned long g_pnext;
static char *g_bp;				/* pile des sprites */
static uint32_t *g_bg;				/* le decor seul */
static gf_sprite_t *g_sb[NBRICK], *g_spad[12], *g_scap[NCAP], *g_sball[2], *g_sdisc[6], *g_smini, *g_sorb[2];
static gf_sprite_t *g_sflash, *g_smote, *g_slaser, *g_slogo;
static unsigned char g_cells[NCELL], g_fuse[NCELL];
static struct ball g_ball[NB];
static struct { long on, x, y, kind; } g_cap[NC];		/* x, y : 16.16 */
static struct { int32_t on, x, y, pad; } g_las[NL];
static struct { long life, x, y; } g_flash[NF];
static struct { int32_t x, y, v, pad; } g_mote[NM];	/* 16.16 */
static unsigned char g_noise[4096];
static struct part g_parts[NP];
static char g_bank[BANK_SIZE] __attribute__ ((aligned (16)));

/* ---- tables ---- */
static const uint32_t brick_col[NBRICK] = { 0x00E04848, 0x00F08A34, 0x00E8C838, 0x0048C468, 0x003C8CE4, 0x00A45CDC, 0x00E868A8,
	0x0048CCD8, 0x00C0CAD8, 0x00C0CAD8, 0x00D8A830, 0x00D8A830, 0x00D8A830, 0x00687080, 0x00C02818 };
static const unsigned char brick_pts[NBRICK] = { 10, 12, 14, 16, 18, 20, 22, 24, 20, 30, 30, 40, 50, 0, 30 };
static const unsigned char pad_w[4] = { 44, 68, 96, 128 };
static const uint32_t pad_cap[3] = { 0x00E04848, 0x0040D080, 0x00F09030 };
static const unsigned short pad_prof[RKT_H] = { 170, 300, 400, 340, 290, 260, 240, 220, 200, 180, 160, 140, 120, 90 };
static const unsigned short wall_prof[20] = { 150, 300, 340, 330, 300, 280, 260, 250, 240, 235, 230, 225, 220, 210, 200,
	190, 170, 150, 120, 80 };
/* rang 0 a 14 : sinus et cosinus (1.14) de -66 a 66 degres */
static const short dirs[15][2] = { { -14968, 6664 }, { -13674, 9026 }, { -12010, 11144 }, { -10022, 12961 }, { -7764, 14428 },
	{ -5295, 15505 }, { -2684, 16163 }, { 0, 16384 }, { 2684, 16163 }, { 5295, 15505 }, { 7764, 14428 }, { 10022, 12961 },
	{ 12010, 11144 }, { 13674, 9026 }, { 14968, 6664 } };
static const uint32_t cap_col[NCAP] = { 0x0040B860, 0x00D04848, 0x004080E0, 0x0040A0C0, 0x00D07830, 0x00E08020, 0x0040B890,
	0x00D05030, 0x00E04870 };
static const char *const cap_text[NCAP] = { "<>", "><", "x3", "L", "R", "T", "C", "Z", "+1" };
static const unsigned char cap_odds[] = { CP_WIDE, CP_WIDE, CP_WIDE, CP_MULTI, CP_MULTI, CP_MULTI, CP_LASER, CP_LASER, CP_LASER,
	CP_GLUE, CP_GLUE, CP_SLOW, CP_SLOW, CP_THRU, CP_THRU, CP_NARROW, CP_NARROW, CP_FAST, CP_LIFE };
#define CAP_ODDS	(sizeof cap_odds)
static const char *const cap_labels[NCAP] = { "Raquette large", "Raquette courte", "Multi-balle", "Balle lente", "Balle rapide",
	"Balle traversante", "Colle", "Laser", "Vie en plus" };
static const struct gs_ent snd_catch[] = { { 0, 660, 50, AKW_TRIANGLE, 90 }, { 50, 880, 70, AKW_TRIANGLE, 90 }, { 0, 0, 0, 0, 0 } };

/* Niveaux : 15 colonnes, 14 lignes au plus (le reste : vide). a-h : couleurs ; S argent (2 coups), G or (3 coups),
 * # acier (indestructible), * explosive, . vide. Style du decor, teinte, nom. */
struct level { unsigned style; uint32_t tint; const char *name; const char *cells; };
static const struct level levels[] = {
	{ 0, 0x00385888, "Premiers pas",
	  "..............."
	  "..............."
	  "aaaaaaaaaaaaaaa"
	  "bbbbbbbbbbbbbbb"
	  "ccccccccccccccc"
	  "ddddddddddddddd"
	  "eeeeeeeeeeeeeee"
	  "fffffffffffffff" },
	{ 1, 0x00806850, "Pyramide",
	  ".......S......."
	  "......SaS......"
	  ".....SaaaS....."
	  "....SbbbbbS...."
	  "...SbbbbbbbS..."
	  "..SccccccccS..."
	  ".SccccccccccS.."
	  "SddddddddddddS."
	  "ddddddddddddddd" },
	{ 2, 0x00407058, "Damier",
	  "..............."
	  "a.b.c.d.e.f.g.h"
	  ".a.b.c.d.e.f.g."
	  "h.a.b.c.d.e.f.g"
	  ".h.a.b.c.d.e.f."
	  "g.h.a.b.c.d.e.f"
	  ".g.h.a.b.c.d.e."
	  "f.g.h.a.b.c.d.e"
	  ".f.g.h.a.b.c.d." },
	{ 3, 0x00604888, "Diamant",
	  ".......h......."
	  "......hSh......"
	  ".....hSeSh....."
	  "....hSeeeSh...."
	  "...hSeeGeeSh..."
	  "..hSeeG*GeeSh.."
	  "...hSeeGeeSh..."
	  "....hSeeeSh...."
	  ".....hSeSh....."
	  "......hSh......"
	  ".......h......." },
	{ 1, 0x00586878, "Forteresse",
	  "..............."
	  "S.S.S.S.S.S.S.S"
	  "SSSSSSSSSSSSSSS"
	  "#ccccccccccccc#"
	  "#c###c###c###c#"
	  "#c#G#c#G#c#G#c#"
	  "#ccccccccccccc#"
	  "#bbbbbb.bbbbbb#"
	  "#bbbbb...bbbbb#"
	  "##...........##" },
	{ 5, 0x00387050, "Envahisseur",
	  "..............."
	  "...d.......d..."
	  "....d.....d...."
	  "...ddddddddd..."
	  "..dd.ddddd.dd.."
	  ".ddddddddddddd."
	  ".d.ddddddddd.d."
	  ".d.d.......d.d."
	  "....dd...dd...."
	  "..............."
	  "SSS.SSS.SSS.SSS" },
	{ 4, 0x00803858, "C\xC5\x93ur",
	  "..............."
	  "...aaa...aaa..."
	  "..aaaaa.aaaaa.."
	  ".aagaaaaaaagaa."
	  ".aaggaaaaaggaa."
	  ".aaagg*a*ggaaa."
	  "..aaaggggggaa.."
	  "...aaaggggaa..."
	  "....aaaggaa...."
	  ".....aaaaa....."
	  "......aaa......"
	  ".......a......." },
	{ 3, 0x00306888, "Vagues",
	  "..............."
	  "ee...ee...ee..."
	  "..ee...ee...ee."
	  "....ee...ee...e"
	  "hh...hh...hh..."
	  "..hh...hh...hh."
	  "....hh...hh...h"
	  "dd...dd...dd..."
	  "..dd...dd...dd."
	  "....dd...dd...d"
	  "..............."
	  "#..#..#..#..#.." },
	{ 5, 0x00507088, "Couloirs",
	  "..............."
	  "#a#b#c#d#e#f#g#"
	  "#a#b#c#d#e#f#g#"
	  "#a#b#c#d#e#f#g#"
	  "#a#b#c#d#e#f#g#"
	  "#a#b#c#d#e#f#g#"
	  "#S#S#S#S#S#S#S#"
	  "#.#.#.#.#.#.#.#"
	  "#.#.#.#.#.#.#.#"
	  "..............." },
	{ 0, 0x00885030, "Poudri\xC3\xA8re",
	  "..............."
	  "bb*bb*bb*bb*bb."
	  "a*bb*bb*bb*bba."
	  "cc*cc*cc*cc*cc."
	  "S.S.S.S.S.S.S.S"
	  ".*.*.*.*.*.*.*."
	  "S.S.S.S.S.S.S.S"
	  "aa*aa*aa*aa*aa." },
	{ 4, 0x00486080, "Tunnel",
	  "###############"
	  "#ffffffffffffe#"
	  "#f###########e#"
	  "#f#ddddddddd#e#"
	  "#f#d#######d#e#"
	  "#f#d#GGGGG#d#e#"
	  "#f#d#######d#e#"
	  "#f#ddddddddd#e#"
	  "#f#####.#####e#"
	  "#fffff...eeeee#"
	  "###.........###" },
	{ 2, 0x00705090, "Spirale",
	  "fffffffffffffff"
	  "f.............S"
	  "f.eeeeeeeeeee.S"
	  "f.e.........d.S"
	  "f.e.ccccccc.d.S"
	  "f.e.c.....b.d.S"
	  "f.e.c.G*G.b.d.S"
	  "f.e.c.....b.d.S"
	  "f.e.bbbbbbb.d.S"
	  "f.e.........d.S"
	  "f.ddddddddddd.S"
	  "f.............S"
	  "SSSSSSSSSSSSSSS" },
	{ 4, 0x00885868, "Cible",
	  "..............."
	  ".....aaaaa....."
	  "...aaSSSSSaa..."
	  "..aSSbbbbbSSa.."
	  ".aSbbGGGGGbbSa."
	  ".aSbGG***GGbSa."
	  ".aSbGG***GGbSa."
	  ".aSbbGGGGGbbSa."
	  "..aSSbbbbbSSa.."
	  "...aaSSSSSaa..."
	  ".....aaaaa....." },
	{ 1, 0x00707078, "Citadelle",
	  "G.G.G.G.G.G.G.G"
	  "GGGGGGGGGGGGGGG"
	  "#*S*S*S*S*S*S*#"
	  "#SSSSSSSSSSSSS#"
	  "#hhh##hhh##hhh#"
	  "#hhh##hhh##hhh#"
	  "#ggg##ggg##ggg#"
	  "#ggg##ggg##ggg#"
	  "#fffffffffffff#"
	  "#ffff#####ffff#"
	  "#ffff#...#ffff#"
	  "##...........##" },
};
#define NLEVELS		(long) (sizeof levels / sizeof levels[0])

static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tPause\tP\nI3c\tSon\n-\nI4\tQuitter\n";

/* ---------------------------------------------------------------------------------------------------------------
 *  Generation des images
 * --------------------------------------------------------------------------------------------------------------- */
/* phase -> 0..255, une onde en paraboles (celle d'arkanoid : 127 au creux) */
static inline unsigned long sin8 (unsigned long p)
{
	p &= 255;
	if (p < 128)
		return (p * (128 - p) >> 5) + 127;
	p -= 128;
	return 127 - (p * (128 - p) >> 5);
}

/* largeur, hauteur -> sprite vide (dans la pile ; la pile est a 0) */
static gf_sprite_t *new_sprite (unsigned long w, unsigned long h)
{
	gf_sprite_t *s = (gf_sprite_t *) g_bp;
	s[0] = (uint32_t) w;
	s[1] = (uint32_t) h;
	g_bp += 8 + w * h * 4;
	return s;
}

static void gen_noise (void)
{
	for (int i = 0; i < 4096; i++)
		g_noise[i] = (unsigned char) (gf_rand () >> 24);
}

/* sprite de brique, colonne de depart : une fissure de haut en bas */
static void crack (gf_sprite_t *s, long x)
{
	uint32_t *p = s + 2;
	for (long r = 1; r < BSH - 1; r++)
	{
		x += (long) gf_below (3) - 1;
		p[r * BSW + x] = 0xFF181010;
	}
}

/* genre -> sprite */
static gf_sprite_t *gen_brick (unsigned long k)
{
	uint32_t col = brick_col[k];
	gf_sprite_t *s = new_sprite (BSW, BSH);
	uint32_t *p = s + 2;
	for (uint32_t y = 0; y < BSH; y++)
		for (uint32_t x = 0; x < BSW; x++, p++)
		{
			if ((x == 0 || x == BSW - 1) && (y == 0 || y == BSH - 1))
				continue;		/* coins arrondis */
			uint32_t f = 300 - y * 110 / (BSH - 1);	/* degrade du haut vers le bas */
			if (y < 6 && x >= 2 && x < BSW - 2)	/* reflet */
				f += 36;
			if (y == 0 || x == 0)		/* biseau */
				f = 450;
			else if (y == 1 || x == 1)
				f += 70;
			if (y == BSH - 1 || x == BSW - 1)
				f = 104;
			else if (y == BSH - 2 || x == BSW - 2)
				f -= 70;
			uint32_t c = col;
			if (k == BK_STEEL)
			{				/* acier : hachures, rivets */
				if (((x + y) & 7) < 2)
					f -= 40;
				if ((y == 3 || y == BSH - 4) && (x == 3 || x == BSW - 4))
					f = 520;
			}
			else if (k == BK_BOMB)
			{				/* explosive : rayures aux bouts, point au milieu */
				if (x < 8 || x >= BSW - 8)
					c = ((x + y) & 7) < 4 ? 0x00F0C020 : 0x00301008;
				else
				{
					long dx = (long) x - BSW / 2, dy = (long) y - BSH / 2;
					if ((unsigned long) (dx * dx + dy * dy) <= 14)
						c = 0x00FFE040;
				}
			}
			*p = ga_shade (c, f) | 0xFF000000;
		}
	if (k == BK_GOLD + 2)				/* les fissures */
		crack (s, 9);
	if (k == BK_SILVER + 1 || k == BK_GOLD + 1 || k == BK_GOLD + 2)
		crack (s, 25);
	return s;
}

/* taille 0..3, style 0 normal, 1 colle, 2 laser -> sprite */
static gf_sprite_t *gen_paddle (unsigned long size, unsigned long style)
{
	unsigned long w = pad_w[size];
	uint32_t cap = pad_cap[style];
	gf_sprite_t *s = new_sprite (w, RKT_H);
	uint32_t *p = s + 2;
	for (unsigned long y = 0; y < RKT_H; y++)
		for (unsigned long x = 0; x < w; x++, p++)
		{
			unsigned long e = x < w - 1 - x ? x : w - 1 - x;	/* distance au bord */
			if (e < 7)
			{				/* bouts ronds (rayon 7) */
				long a = 7 - (long) e, b = (long) y - 6;
				if ((unsigned long) (a * a + b * b) > 52)
					continue;
			}
			uint32_t f = pad_prof[y], c = 0x00B4BCC8;	/* profil vertical ; les bouts en couleur */
			if (e < 11)
				c = cap;
			if (e == 11)
				f = 70;
			*p = ga_shade (c, f) | 0xFF000000;
		}
	return s;
}

/* diametre, couleur -> une sphere eclairee d'en haut a gauche */
static gf_sprite_t *gen_ball (unsigned long d, uint32_t c)
{
	gf_sprite_t *s = new_sprite (d, d);
	uint32_t *p = s + 2;
	unsigned long dd = d * d;
	for (unsigned long y = 0; y < d; y++)
		for (unsigned long x = 0; x < d; x++, p++)
		{
			long dx = (long) (x << 1) + 1 - (long) d, dy = (long) (y << 1) + 1 - (long) d;
			if ((unsigned long) (dx * dx + dy * dy) > dd)
				continue;
			dx += (long) (d >> 1);		/* lumiere vers le coin haut gauche */
			dy += (long) (d >> 1);
			unsigned long q = (unsigned long) (dx * dx + dy * dy) * 300 / dd;	/* 0 au reflet */
			unsigned long f = q > 440 ? 60 : 440 - q;
			if (f < 60)
				f = 60;
			*p = ga_shade (c, (uint32_t) f) | 0xFF000000;
		}
	return s;
}

/* diametre, couleur -> une lumiere ronde (a ajouter) */
static gf_sprite_t *gen_disc (unsigned long d, uint32_t c)
{
	gf_sprite_t *s = new_sprite (d, d);
	uint32_t *p = s + 2;
	unsigned long dd = d * d;
	for (unsigned long y = 0; y < d; y++)
		for (unsigned long x = 0; x < d; x++, p++)
		{
			long dx = (long) (x << 1) + 1 - (long) d, dy = (long) (y << 1) + 1 - (long) d;
			unsigned long q = (unsigned long) (dx * dx + dy * dy);
			if (q >= dd)
				continue;
			unsigned long k = 256 - q * 256 / dd;	/* 256 au centre, 0 au bord, en cloche */
			*p = ga_shade (c, (uint32_t) (k * k >> 8));
		}
	return s;
}

/* les capsules : une pastille, sa lettre (ecrite par UIKit dans la couche) */
static void gen_capsules (void)
{
	ui_canvas_begin (gf_cv);
	for (int i = 0; i < NCAP; i++)
	{
		gf_sprite_t *s = new_sprite (CAP_W, CAP_H);
		g_scap[i] = s;
		uint32_t c = cap_col[i], *p = s + 2;
		for (long y = 0; y < CAP_H; y++)
			for (long x = 0; x < CAP_W; x++, p++)
			{
				long e = x < CAP_W - 1 - x ? x : CAP_W - 1 - x;
				if (e < 8)
				{			/* bouts ronds (rayon 8) */
					long a = 8 - e, b = ((y << 1) - 15) >> 1;
					if ((unsigned long) (a * a + b * b) > 62)
						continue;
				}
				uint32_t f = 330 - (uint32_t) y * 18;	/* degrade, reflet en haut */
				if (y <= 3)
					f += 110;
				*p = ga_shade (c, f) | 0xFF000000;
			}
		ui_fill (0, 0, CAP_W, CAP_H, 0);	/* la lettre, en blanc sur noir */
		gf_text_c (cap_text[i], CAP_W / 2, 12, 0x00FFFFFF, 11, (long) ui.font_b);
		p = s + 2;				/* sa couverture devient du clair */
		const uint32_t *l = gf_layer;
		for (long y = 0; y < CAP_H; y++, l += GF_W)
			for (long x = 0; x < CAP_W; x++, p++)
			{
				uint32_t v = l[x] & 0xFF;
				if (v < 70 || !*p)
					continue;
				*p = v | v << 8 | v << 16 | 0xFF000000;
			}
	}
	ui_canvas_end ();
}

static void gen_sprites (void)
{
	for (unsigned long i = 0; i < NBRICK; i++)	/* les briques */
		g_sb[i] = gen_brick (i);
	for (unsigned long i = 0; i < 12; i++)		/* les raquettes */
		g_spad[i] = gen_paddle (i & 3, i >> 2);
	g_sball[0] = gen_ball (BALL_D, 0x0090B8F0);	/* les balles */
	g_sball[1] = gen_ball (BALL_D, 0x00FF8830);
	g_smini = gen_ball (9, 0x00E0E8F4);
	g_sdisc[0] = gen_disc (11, 0x00304870);		/* les trainees (lumieres) */
	g_sdisc[1] = gen_disc (9, 0x00203450);
	g_sdisc[2] = gen_disc (7, 0x00142038);
	g_sdisc[3] = gen_disc (11, 0x00804010);
	g_sdisc[4] = gen_disc (9, 0x00602808);
	g_sdisc[5] = gen_disc (7, 0x00401804);
	g_sflash = gen_disc (FLASH_D, 0x00FFA040);
	g_smote = gen_disc (7, 0x00506070);
	g_sorb[0] = gen_disc (ORB_D, 0x00203040);	/* deux lumieres, teintes par niveau */
	g_sorb[1] = gen_disc (ORB_D, 0x00301830);
	g_slaser = new_sprite (4, 14);			/* le tir de laser */
	for (int i = 0; i < 56; i++)
		g_slaser[2 + i] = 0x00FFE080;
	gen_capsules ();
}

static void gen_motes (void)
{
	for (int i = 0; i < NM; i++)
	{
		g_mote[i].x = (int32_t) ((gf_below (FIELD_R - FIELD_L - 8) + FIELD_L) << 16);
		g_mote[i].y = (int32_t) ((gf_below (GF_H - HUD_H) + HUD_H) << 16);
		g_mote[i].v = (int32_t) (gf_below (40000) + 12000);
	}
}

/* x, y, log2 du pas -> bruit lisse 0..255 (sans adoucir les fractions) */
static long noise2 (unsigned long x, unsigned long y, unsigned s)
{
	unsigned long m = (1ul << s) - 1, fx = x & m, fy = y & m;
	x = (x >> s) & 63;
	y = (y >> s) & 63;
	long a = g_noise[x + (y << 6)], b = g_noise[((x + 1) & 63) + (y << 6)];
	long top = a + (((b - a) * (long) fx) >> s);
	y = (y + 1) & 63;
	a = g_noise[x + (y << 6)];
	b = g_noise[((x + 1) & 63) + (y << 6)];
	long bot = a + (((b - a) * (long) fx) >> s);
	return top + (((bot - top) * (long) fy) >> s);
}

/* x, y, style -> valeur 0..255 du decor */
static unsigned long texture (unsigned long x, unsigned long y, unsigned style)
{
	if (style == 0)
	{						/* plasma */
		unsigned long v = sin8 (x >> 1) + sin8 (y) + sin8 ((x + y) >> 2);
		v += sin8 ((unsigned long) (((long) x - (long) (y << 1)) >> 3));	/* (decalage signe, comme asr) */
		return v >> 2;
	}
	if (style == 1)
	{						/* paves */
		unsigned long r = y / 24, fy = y % 24, bx = (r & 1) ? x + 24 : x, c = bx / 48, fx = bx % 48;
		if (fy < 2 || fx < 2)
			return 20;
		unsigned long v = (unsigned long) (g_noise[(r * 37 + c * 101) & 4095] >> 2) + 84;	/* teinte du pave */
		if (fy == 2 || fx == 2)
			v += 50;
		if (fy == 23 || fx == 47)
			v -= 44;
		return v;
	}
	if (style == 2)
	{						/* tissage */
		unsigned long a = ((x + y) >> 4) ^ ((x + 1024 - y) >> 4);
		return (sin8 (y >> 1) >> 3) + ((a & 1) ? 110 : 62);
	}
	if (style == 3)					/* nuages : deux octaves */
		return (unsigned long) (noise2 (x, y, 5) * 3 + noise2 (x, y, 4)) >> 2;
	if (style == 4)
	{						/* anneaux */
		long dx = (long) x - 320, dy = (long) y - 200;
		return (sin8 ((unsigned long) (dx * dx + dy * dy) >> 9) >> 1) + 44;
	}
	unsigned long cx = x / 24, fx = x % 24, cy = y / 24, fy = y % 24;	/* circuit */
	if (fx < 3 && fy < 3)
		return 170;
	if (fx == 1 || fy == 1)
		return 92;
	return (unsigned long) (g_noise[(cy * 53 + cx) & 4095] >> 3) + 26;
}

/* niveau : le decor, puis les murs */
static void gen_bg (const struct level *lv)
{
	uint32_t *p = g_bg;
	for (unsigned long y = 0; y < GF_H; y++)
		for (unsigned long x = 0; x < GF_W; x++)
		{
			uint32_t v = (uint32_t) texture (x, y, lv->style);
			uint32_t f = ((190 + (uint32_t) ((GF_H - y) >> 3)) * v >> 8) + 24;	/* plus sombre vers le bas */
			*p++ = ga_shade (lv->tint, f);
		}
	p = g_bg;					/* les murs : gauche et droit, puis le haut */
	for (unsigned long y = 0; y < GF_H; y++, p += GF_W)
		for (unsigned long x = 0; x < FIELD_L; x++)
		{
			uint32_t f = wall_prof[x];
			if ((y & 63) <= 1)		/* un joint de temps en temps */
				f >>= 1;
			uint32_t c = ga_shade (0x00687484, f) | 0xFF000000;
			p[x] = c;
			p[GF_W - 1 - x] = c;
		}
	p = g_bg + GF_W * HUD_H;
	for (unsigned long y = 0; y < FIELD_T - HUD_H; y++, p += GF_W)
	{
		uint32_t c = ga_shade (0x00687484, wall_prof[y]) | 0xFF000000;
		for (long x = FIELD_L; x < FIELD_R; x++)
			p[x] = c;
	}
}

/* ---------------------------------------------------------------------------------------------------------------
 *  La couche fixe
 * --------------------------------------------------------------------------------------------------------------- */
/* les briques et leurs ombres, sur le decor */
static void draw_field (void)
{
	gf_copy (gf_layer + BRICK_T * GF_W, g_bg + BRICK_T * GF_W, (ROWS * BH + SHADOW + 3) * GF_PITCH);
	const unsigned char *cell = g_cells;
	for (long r = 0; r < ROWS; r++)			/* les ombres d'abord */
		for (long c = 0; c < COLS; c++)
		{
			if (!*cell++)
				continue;
			uint32_t *o = gf_layer + (r * BH + BRICK_T + SHADOW) * GF_W + c * BW + FIELD_L + SHADOW;
			for (long y = 0; y < BSH; y++, o += GF_W)
				for (long x = 0; x < BSW; x++)
				{
					uint32_t v = o[x];
					if (v >> 24)
						continue;
					o[x] = ((v >> 1) & 0x7F7F7F7F) | 0x80000000;
				}
		}
	cell = g_cells;
	for (long r = 0; r < ROWS; r++)			/* puis les briques */
		for (long c = 0; c < COLS; c++)
		{
			unsigned k = *cell++;
			if (k)
				gf_sprite (c * BW + FIELD_L, r * BH + BRICK_T, g_sb[k - 1]);
		}
}

/* destination, morceau de texte -> la fin : traduit et ajoute. Les espaces autour du morceau restent ici (un
 * dictionnaire ne garde pas ceux d'une cle). */
static char *tr_cat (char *d, const char *s)
{
	char key[240];
	while (*s == ' ')				/* espaces de tete */
		*d++ = *s++;
	long n = 0, k = 0;				/* longueur sans ceux de queue */
	for (; s[k] && k < 239; k++)
	{
		key[k] = s[k];
		if (s[k] != ' ')
			n = k + 1;
	}
	key[n] = 0;
	s += n;
	const char *t = gf_tr (key);
	while (*t)
		*d++ = *t++;
	while ((*d = *s++) != 0)
		d++;
	return d;
}

/* valeur -> decimal a d ; -> la fin */
static char *dec (char *d, unsigned long v)
{
	char t[24];
	int n = 0;
	do
		t[n++] = (char) ('0' + v % 10);
	while ((v /= 10) && n < 22);
	while (n)
		*d++ = t[--n];
	*d = 0;
	return d;
}

/* etiquette, x, valeur -> x de fin */
static long hud_item (const char *label, long x, unsigned long v)
{
	char b[32];
	label = gf_tr (label);
	gf_text (label, x, 19, 0x00A8B4C4, 13, (long) ui.font);
	x += ui_text_width ((long) label, 13, (long) ui.font) + 6;
	dec (b, v);
	gf_text (b, x, 19, 0x00FFFFFF, 14, (long) ui.font_b);
	return x + ui_text_width ((long) b, 14, (long) ui.font_b);
}

static void draw_hud (void)
{
	for (uint32_t y = 0; y < HUD_H; y++)		/* (degrade du bandeau) */
		gf_rect (0, y, GF_W, 1, 0xFF343840 - 0x00010101 * y);
	gf_rect (0, HUD_H - 1, GF_W, 1, gf_tok[TJ_ACCENT / 4] | 0xFF000000);
	hud_item ("Score", 24, g_score);
	hud_item ("Record", 140, g_best);
	hud_item ("Niveau", 264, (unsigned long) g_level + 1);
	for (long i = 0; i < g_lives && i < 5; i++)	/* les vies */
		gf_sprite (12 * i + 356, 9, g_smini);
	if (g_label_t)					/* le dernier bonus */
		gf_text (gf_tr (g_label), 422, 19, gf_tok[TJ_ACCENT_LIGHT / 4], 12, (long) ui.font_b);
}

/* y, hauteur : un panneau sombre pour un message */
static void panel (long y, long h)
{
	ga_panel (104, y + 7, 440, (unsigned long) h, 18, 0, 0, 110, 0);	/* l'ombre, puis le verre */
	ga_panel (100, y, 440, (unsigned long) h, 18, 0x002A3452, 0x00121A2E, 230, 0x00687EB0);
	ga_mark (100, y, 440, h);			/* premier plan : ni ombre ni lumiere */
}

/* titre, deuxieme ligne : un message au milieu du terrain */
static void message (const char *title, const char *sub)
{
	title = gf_tr (title);
	sub = sub ? gf_tr (sub) : 0;
	panel (190, 90);
	gf_text_c (title, GF_W / 2, 232, 0x00FFFFFF, 26, (long) ui.font_b);
	if (sub)
		gf_text_c (sub, GF_W / 2, 260, 0x00C8D2DE, 14, (long) ui.font);
}

static void title_screen (void)
{
	char b[48];
	panel (120, 210);
	ga_show (g_slogo, GF_W / 2, 200 - 65);
	gf_text_c (gf_tr ("Casse-briques"), GF_W / 2, 232, 0x00C8D2DE, 15, (long) ui.font);
	gf_text_c (gf_tr ("Espace, A ou toucher pour jouer"), GF_W / 2, 272, 0x00FFFFFF, 18, (long) ui.font_b);
	dec (tr_cat (b, "Record : "), g_best);
	gf_text_c (b, GF_W / 2, 304, 0x00A8B4C4, 14, (long) ui.font);
	ga_mark (100, 120, 440, 210);
}

/* -> le niveau courant */
static const struct level *level_ptr (void) { return &levels[(unsigned long) g_level % NLEVELS]; }

/* toute la couche */
static void build_layer (void)
{
	char b[64];
	g_ldirty = g_fdirty = g_hdirty = 0;
	gf_dst = gf_layer;
	gf_copy (gf_layer, g_bg, GF_PITCH * GF_H);
	ui_canvas_begin (gf_cv);
	draw_field ();
	draw_hud ();
	if (g_state == ST_TITLE)
		title_screen ();
	else if (g_state == ST_READY)
		message (level_ptr ()->name, "Espace, A ou toucher pour lancer");
	else if (g_state == ST_OVER)			/* (la pause : gamepause) */
		message ("Partie termin\xC3\xA9" "e", "Espace, A ou toucher pour rejouer");
	else if (g_state == ST_CLEAR)
	{
		tr_cat (dec (tr_cat (b, "Niveau "), (unsigned long) g_level + 1), " termin\xC3\xA9");
		message (b, "Bonus 500");
	}
	ui_canvas_end ();
	gf_dst = gf_frame;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Parties et niveaux
 * --------------------------------------------------------------------------------------------------------------- */
static void set_state (long s)
{
	g_state = s;
	g_timer = 0;
	g_ldirty = 1;
	gf_still = s == ST_PAUSE;
	gf_redraw = 1;
}

/* taille 0..3 de la raquette (elle garde son milieu) */
static void set_psize (long s)
{
	g_psize = s;
	long w = pad_w[s];
	g_px += (g_pw - w) >> 1;
	g_pw = w;
}

/* rang 0..14, balle : direction vers le haut de la table */
static void set_dir (unsigned long i, struct ball *b)
{
	b->dx = dirs[i][0];
	b->dy = -(long) dirs[i][1];
}

/* plus rien en jeu, une balle sur la raquette */
static void reset_round (void)
{
	for (int i = 0; i < NB; i++)
		g_ball[i].on = 0;
	for (int i = 0; i < NC; i++)
		g_cap[i].on = 0;
	for (int i = 0; i < NL; i++)
		g_las[i].on = 0;
	g_t_slow = g_t_fast = g_t_thru = g_t_glue = g_t_laser = 0;
	g_combo = g_loop = 0;
	set_psize (1);
	g_px = (long) ((unsigned long) (GF_W - g_pw) >> 1);	/* au milieu */
	struct ball *b = &g_ball[0];
	b->on = 1;
	b->stuck = 1;
	b->off = (long) ((unsigned long) g_pw >> 1);
	b->t = 0;
	b->dx = 0;					/* (vers le haut) */
	b->dy = -16384;
}

/* le niveau courant : les briques, le decor, une balle sur la raquette */
static void load_level (void)
{
	const struct level *lv = level_ptr ();
	gen_bg (lv);
	long left = 0;					/* a casser */
	const char *s = lv->cells;
	for (int i = 0; i < NCELL; i++)
	{
		char ch = *s ? *s++ : '.';
		unsigned char v;
		if (ch == '.')
			v = 0;
		else if (ch == '#')
			v = BK_STEEL + 1;
		else
		{
			v = ch == '*' ? BK_BOMB + 1 : ch == 'S' ? BK_SILVER + 1 : ch == 'G' ? BK_GOLD + 1
				: (unsigned char) (ch - 'a' + 1);
			left++;
		}
		g_cells[i] = v;
		g_fuse[i] = 0;
	}
	g_left = left;
	reset_round ();
}

static void goto_title (void)
{
	g_level = 0;
	g_score = 0;
	g_lives = 3;
	load_level ();
	g_ball[0].on = 0;				/* (pas de balle au titre) */
	set_state (ST_TITLE);
}

static void new_game (void)
{
	g_level = 0;
	g_score = 0;
	g_lives = 3;
	load_level ();
	set_state (ST_READY);
}

static void keep_best (void)
{
	if (g_score <= g_best)
		return;
	g_best = g_score;
	gf_save_best (P_DIR, P_BEST, g_best);
}

static void add_score (unsigned long n)
{
	g_score += n;
	g_hdirty = 1;
}

/* les balles collees partent */
static void launch (void)
{
	for (int i = 0; i < NB; i++)
	{
		struct ball *b = &g_ball[i];
		if (!b->on || !b->stuck)
			continue;
		b->stuck = 0;
		set_dir (gf_below (5) + 5, b);
	}
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Effets
 * --------------------------------------------------------------------------------------------------------------- */
/* x, y (pixels), genre, couleur */
static void spawn_part (long x, long y, uint32_t kind, uint32_t col)
{
	unsigned long i = g_pnext;
	g_pnext = i + 1 >= NP ? 0 : i + 1;
	struct part *p = &g_parts[i];
	p->x = (int32_t) ((uint32_t) x << 16);
	p->y = (int32_t) ((uint32_t) y << 16);
	p->col = col;
	p->kind = kind;
	p->vx = (int32_t) (gf_below (0x60000) - 0x30000);	/* vitesses au hasard */
	p->vy = (int32_t) (gf_below (0x60000) - 0x40000);
	p->life = (int32_t) (gf_below (40) + 24);
}

/* x, y, nombre, genre, couleur */
static void burst (long x, long y, long n, uint32_t kind, uint32_t col)
{
	do
	{
		long px = (long) gf_below (BW) - BW / 2 + x;
		long py = (long) gf_below (BH) - BH / 2 + y;
		spawn_part (px, py, kind, col);
	}
	while (--n);
}

/* x, y : un eclair */
static void add_flash (long x, long y)
{
	for (int i = 0; i < NF; i++)
		if (!g_flash[i].life)
		{
			g_flash[i].life = 12;
			g_flash[i].x = x;
			g_flash[i].y = y;
			return;
		}
}

/* case : son centre */
static void cell_xy (unsigned long c, long *x, long *y)
{
	*x = (long) (c % COLS) * BW + FIELD_L + BW / 2;
	*y = (long) (c / COLS) * BH + BRICK_T + BH / 2;
}

/* x, y : peut-etre une capsule */
static void maybe_capsule (long x, long y)
{
	if (gf_below (5))
		return;
	for (int i = 0; i < NC; i++)
		if (!g_cap[i].on)
		{
			g_cap[i].on = 1;
			g_cap[i].x = (x - CAP_W / 2) << 16;
			g_cap[i].y = y << 16;
			g_cap[i].kind = cap_odds[gf_below (CAP_ODDS)];
			return;
		}
}

/* case : la brique disparait */
static void destroy_cell (unsigned long c)
{
	unsigned k = g_cells[c];
	if (!k)
		return;
	k--;						/* genre */
	g_cells[c] = 0;
	g_fuse[c] = 0;
	g_fdirty = 1;
	g_left--;
	g_loop = 0;
	unsigned long pts = brick_pts[k] + ((unsigned long) g_combo << 1);
	g_combo = g_combo + 1 > 20 ? 20 : g_combo + 1;
	add_score (pts);
	long cx, cy;
	cell_xy (c, &cx, &cy);
	burst (cx, cy, 10, 1, brick_col[k]);
	burst (cx, cy, 6, 0, 0x00FFFFFF);
	if (k != BK_BOMB)
	{
		gs_note ((long) (ROWS - c / COLS) * 28 + 420, 40, AKW_SQUARE, 56);	/* le son monte avec la ligne */
		maybe_capsule (cx, cy);
		return;
	}
	add_flash (cx, cy);
	burst (cx, cy, 18, 0, 0x00FFA030);
	gs_note (120, 300, AKW_NOISE, 170);
	unsigned long r = c / COLS, col = c % COLS;	/* les voisines : la meche */
	for (long dy = -1; dy <= 1; dy++)
		for (long dx = -1; dx <= 1; dx++)
		{
			unsigned long rr = r + (unsigned long) dy, cc = col + (unsigned long) dx;
			if (rr >= ROWS || cc >= COLS)
				continue;
			unsigned long i = rr * COLS + cc;
			if (!g_cells[i] || g_cells[i] == BK_STEEL + 1 || g_fuse[i])
				continue;
			g_fuse[i] = 7;
		}
}

/* case, 1 : la balle traverse -> 1 si le projectile rebondit */
static long hit_brick (unsigned long c, long thru)
{
	unsigned k = g_cells[c] - 1u;			/* genre */
	if (thru)
	{
		if (k != BK_STEEL)
			destroy_cell (c);
		return 0;
	}
	if (k == BK_STEEL)
	{
		gs_note (1800, 16, AKW_TRIANGLE, 60);
		return 1;
	}
	if (k == BK_SILVER || k == BK_GOLD || k == BK_GOLD + 1)
	{						/* une fissure de plus */
		g_cells[c] = (unsigned char) (k + 2);
		g_fdirty = 1;
		g_loop = 0;
		gs_note (1200, 24, AKW_TRIANGLE, 70);
		return 1;
	}
	destroy_cell (c);
	return 1;
}

/* x, y (pixels), 1 : traverse -> 1 rebond */
static long try_hit (long x, long y, long thru)
{
	unsigned long ux = (unsigned long) (x - FIELD_L), uy = (unsigned long) (y - BRICK_T);
	if (ux >= COLS * BW || uy >= ROWS * BH)
		return 0;
	unsigned long c = uy / BH * COLS + ux / BW;
	if (!g_cells[c])
		return 0;
	return hit_brick (c, thru);
}

static void wall_sound (void) { gs_note (520, 16, AKW_TRIANGLE, 40); }

/* balle : contre la raquette */
static void paddle_hit (struct ball *b)
{
	long off = (b->x >> 16) - g_px;		/* decalage */
	b->y = (long) (RKT_Y - BALL_R) << 16;
	long i = off * 15 / g_pw;
	if (i < 0) i = 0;
	if (i > 14) i = 14;
	set_dir ((unsigned long) i, b);
	g_combo = 0;
	g_loop = 0;
	if (g_t_glue)
	{
		b->stuck = 1;
		b->off = off;
		b->t = 0;
	}
	gs_note (300, 30, AKW_SQUARE, 70);
	burst (b->x >> 16, RKT_Y, 4, 0, 0x00C0D8FF);
}

/* balle, vitesse (8.8) : un demi-pas */
static void ball_sub (struct ball *b, long sp)
{
	long thru = g_t_thru != 0;			/* traversante ? */
	/* --- en x */
	long dx = b->dx, nx = b->x + (dx * sp >> 7), edge = (nx >> 16) + (dx < 0 ? -BALL_R : BALL_R);	/* bord qui avance */
	if (edge < FIELD_L)
	{
		b->dx = dx < 0 ? -dx : dx;
		nx = (long) (FIELD_L + BALL_R) << 16;
		wall_sound ();
	}
	else if (edge >= FIELD_R)
	{
		b->dx = dx > 0 ? -dx : dx;
		nx = (long) (FIELD_R - BALL_R - 1) << 16;
		wall_sound ();
	}
	else if (try_hit (edge, (b->y >> 16) - 3, thru) || try_hit (edge, (b->y >> 16) + 3, thru))
	{
		b->dx = -dx;
		nx = b->x;
	}
	b->x = nx;
	/* --- en y */
	long dy = b->dy, ny = b->y + (dy * sp >> 7);
	edge = (ny >> 16) + (dy < 0 ? -BALL_R : BALL_R);
	if (edge < FIELD_T)
	{
		b->dy = -dy;
		ny = (long) (FIELD_T + BALL_R) << 16;
		wall_sound ();
	}
	else if (try_hit ((b->x >> 16) - 3, edge, thru) || try_hit ((b->x >> 16) + 3, edge, thru))
	{
		b->dy = -dy;
		ny = b->y;
	}
	b->y = ny;
	/* --- la raquette, le bas */
	if (b->dy <= 0)
		return;
	long cy = ny >> 16;
	if (cy + BALL_R < RKT_Y)
		return;
	if (cy - BALL_R < RKT_Y + RKT_H)
	{
		long bx = b->x >> 16;
		if (bx >= g_px - BALL_R && bx <= g_px + g_pw + BALL_R)
		{
			paddle_hit (b);
			return;
		}
	}
	if (cy >= GF_H + 8)
		b->on = 0;
}

/* -> vitesse des balles (pixels par image, 8.8) */
static long ball_speed (void)
{
	unsigned long v = (unsigned long) g_level * 28 + 900;
	if (v > 1380)
		v = 1380;
	if (g_t_slow)
		v = (v + (v << 1)) >> 2;
	if (g_t_fast)
		v += v >> 1;
	return (long) v;
}

static void life_lost (void)
{
	g_lives--;
	g_hdirty = 1;
	burst (g_px + (long) ((unsigned long) g_pw >> 1), RKT_Y, 30, 1, 0x00B4BCC8);
	gs_seq (gs_tab_lose);
	set_state (ST_LOST);
}

static void balls_step (void)
{
	long sp = ball_speed (), alive = 0;		/* balles vivantes */
	for (int i = 0; i < NB; i++)
	{
		struct ball *b = &g_ball[i];
		if (!b->on)
			continue;
		if (b->stuck)
		{					/* collee a la raquette */
			b->x = (g_px + b->off) << 16;
			b->y = (long) (RKT_Y - BALL_R) << 16;
			for (int k = 0; k < HIST; k++)	/* (la trainee partira d'ici) */
			{
				b->h[k][0] = (uint32_t) (b->x >> 16);
				b->h[k][1] = RKT_Y - BALL_R;
			}
			alive++;
			if (g_state != ST_PLAY)
				continue;
			if (++b->t >= 180)		/* part toute seule au bout de 3 s */
				launch ();
			continue;
		}
		b->hi = (b->hi + 1) & (HIST - 1);	/* la trainee */
		b->h[b->hi][0] = (uint32_t) (b->x >> 16);
		b->h[b->hi][1] = (uint32_t) (b->y >> 16);
		ball_sub (b, sp);
		if (!b->stuck)
		{
			if (!b->on)
				continue;
			ball_sub (b, sp);
			if (!b->on)
				continue;
		}
		alive++;
	}
	if (!alive)
	{
		life_lost ();
		return;
	}
	if (++g_loop < 1200)				/* la balle tourne en rond : un coup de pouce */
		return;
	g_loop = 0;
	for (int i = 0; i < NB; i++)
		if (g_ball[i].on)
			set_dir (gf_below (15), &g_ball[i]);
}

/* deux balles de plus, au depart de la premiere */
static void multi_ball (void)
{
	struct ball *src = 0;
	for (int i = 0; i < NB && !src; i++)
		if (g_ball[i].on)
			src = &g_ball[i];
	if (!src)
		return;
	long made = 2;					/* a creer */
	for (int i = 0; i < NB; i++)
	{
		struct ball *b = &g_ball[i];
		if (b->on)
			continue;
		*b = *src;				/* toute la balle, sa trainee comprise */
		b->stuck = 0;
		set_dir (gf_below (11) + 2, b);
		if (!--made)
			return;
	}
}

/* genre : la capsule est prise */
static void catch_cap (long k)
{
	g_label = cap_labels[k];			/* son nom */
	g_label_t = 180;
	add_score (50);
	gs_seq (snd_catch);
	switch (k)
	{
	case CP_WIDE:
		if (g_psize + 1 <= 3)
			set_psize (g_psize + 1);
		break;
	case CP_NARROW:
		if (g_psize)
			set_psize (g_psize - 1);
		break;
	case CP_MULTI:
		multi_ball ();
		break;
	case CP_SLOW:
		g_t_slow = 600;
		g_t_fast = 0;
		break;
	case CP_FAST:
		g_t_fast = 480;
		g_t_slow = 0;
		break;
	case CP_THRU:
		g_t_thru = 480;
		break;
	case CP_GLUE:
		g_t_glue = 900;
		break;
	case CP_LASER:
		g_t_laser = 900;
		break;
	default:
		if (g_lives < 9)
			g_lives++;
	}
	g_hdirty = 1;
}

static void capsules_step (void)
{
	for (int i = 0; i < NC; i++)
	{
		if (!g_cap[i].on)
			continue;
		g_cap[i].y += 0x18000;			/* 1,5 point par image */
		long y = g_cap[i].y >> 16;
		if (y + CAP_H < RKT_Y)
			continue;
		if (y >= RKT_Y + RKT_H)
		{
			if (y >= GF_H)
				g_cap[i].on = 0;
			continue;
		}
		long x = g_cap[i].x >> 16;
		if (x + CAP_W < g_px || x > g_px + g_pw)
			continue;
		g_cap[i].on = 0;
		catch_cap (g_cap[i].kind);
	}
}

/* x : un tir */
static void laser_add (long x)
{
	for (int i = 0; i < NL; i++)
		if (!g_las[i].on)
		{
			g_las[i].on = 1;
			g_las[i].x = (int32_t) x;
			g_las[i].y = RKT_Y - 14;
			return;
		}
}

/* les tirs de laser */
static void lasers_step (void)
{
	if (g_laser_cd)
		g_laser_cd--;
	else if (g_t_laser && (g_btn | gf_held[KEY_SPACE_] | (gp_now & (PAD_A | PAD_B | PAD_X | PAD_Y))))
	{						/* (A, B, X ou Y tenus) */
		g_laser_cd = 16;
		laser_add (g_px + 6);
		laser_add (g_px + g_pw - 10);
		gs_note (1400, 40, AKW_SAW, 50);
	}
	for (int i = 0; i < NL; i++)
	{
		if (!g_las[i].on)
			continue;
		g_las[i].y -= 8;
		if (g_las[i].y < FIELD_T || try_hit ((long) (uint32_t) g_las[i].x + 2, g_las[i].y, 0))
			g_las[i].on = 0;
	}
}

/* les meches des explosions */
static void fuses_step (void)
{
	for (unsigned long i = 0; i < NCELL; i++)
		if (g_fuse[i] && !--g_fuse[i])
			destroy_cell (i);
}

static void dec_t (long *t)
{
	if (*t)
		(*t)--;
}

static void paddle_step (void)
{
	long l = gf_held[KEY_LEFT_] | ((gp_now >> 2) & 1), r = gf_held[KEY_RIGHT_] | ((gp_now >> 3) & 1);	/* (ou la croix) */
	long d = r - l;					/* -1, 0, 1 */
	if (!d)
	{
		g_keyspd = 4;
		if (g_mmoved)
		{
			g_mmoved = 0;
			g_px = g_mx - (long) ((unsigned long) g_pw >> 1);
		}
	}
	else
	{
		long s = g_keyspd + 1;			/* ca accelere */
		if (s > 11)
			s = 11;
		g_keyspd = s;
		g_px += d * s;
	}
	if (g_px < FIELD_L)
		g_px = FIELD_L;
	if (g_px > FIELD_R - g_pw)
		g_px = FIELD_R - g_pw;
}

static void parts_step (void)
{
	for (int i = 0; i < NP; i++)
	{
		struct part *p = &g_parts[i];
		if (!p->life)
			continue;
		p->life--;
		p->x = (int32_t) ((uint32_t) p->x + (uint32_t) p->vx);
		p->y = (int32_t) ((uint32_t) p->y + (uint32_t) p->vy);
		p->vy = (int32_t) ((uint32_t) p->vy + (p->kind ? 0x3800u : 0x1800u));	/* pesanteur */
	}
}

static void motes_step (void)
{
	for (int i = 0; i < NM; i++)
	{
		uint32_t y = (uint32_t) g_mote[i].y - (uint32_t) g_mote[i].v;
		if (y < 0x200000)
			y = 0x1D00000;
		g_mote[i].y = (int32_t) y;
	}
}

static void next_level (void)
{
	g_level++;
	keep_best ();
	load_level ();
	set_state (ST_READY);
}

/* une image de jeu */
static void step (void)
{
	g_tick++;
	parts_step ();
	motes_step ();
	for (int i = 0; i < NF; i++)
		if (g_flash[i].life)
			g_flash[i].life--;
	if (g_label_t && !--g_label_t)			/* le nom du bonus s'efface */
		g_hdirty = 1;
	if (g_state == ST_READY)
	{
		paddle_step ();
		balls_step ();
	}
	else if (g_state == ST_PLAY)
	{
		paddle_step ();
		dec_t (&g_t_slow);
		dec_t (&g_t_fast);
		dec_t (&g_t_thru);
		dec_t (&g_t_glue);
		dec_t (&g_t_laser);
		balls_step ();
		if (g_state != ST_PLAY)
			return;
		capsules_step ();
		lasers_step ();
		fuses_step ();
		if (g_left)
			return;
		add_score (500);
		set_state (ST_CLEAR);
		gs_seq (gs_tab_win);
	}
	else if (g_state == ST_LOST)
	{
		if (++g_timer < 50)
			return;
		if (g_lives)
		{
			reset_round ();
			set_state (ST_READY);
		}
		else
		{
			keep_best ();
			set_state (ST_OVER);
		}
	}
	else if (g_state == ST_CLEAR)
	{
		if (++g_timer >= 110)
			next_level ();
	}
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Evenements
 * --------------------------------------------------------------------------------------------------------------- */
/* (gamepause) la partie s'arrete (balle en jeu ou pas encore lancee) -> 1 si elle etait en cours */
static long game_pause (void)
{
	g_btn = 0;
	long s = g_state;
	if (s != ST_PLAY && s != ST_READY)
		return 0;
	g_prev = s;
	set_state (ST_PAUSE);
	return 1;
}

/* (gamepause) la partie repart */
static void game_resume (void)
{
	if (g_state == ST_PAUSE)
		set_state (g_prev);
}

static void do_menu (long id)
{
	if (id == ID_QUIT)
		gf_quit = 1;
	else if (id == ID_NEW)
		new_game ();
	else if (id == ID_PAUSE)
		gp_open ();
	else if (id == ID_SOUND)
		ui_menu_state (ID_SOUND, gs_toggle () ? UMS_CHECKED : 0);
}

/* Espace, Entree, clic, appui : selon l'etat */
static void action (void)
{
	long s = g_state;
	if (s == ST_TITLE || s == ST_OVER)
		new_game ();
	else if (s == ST_READY)
	{
		launch ();
		set_state (ST_PLAY);
	}
	else if (s == ST_PLAY)
		launch ();
	else if (s == ST_CLEAR && g_timer >= 30)	/* (pas d'un coup de doigt egare) */
		next_level ();
}

/* la manette : A, B, X, Y ou haut (fronts) agissent comme Espace ; la croix tenue deplace la raquette, A, B, X,
 * Y tenus tirent au laser (step) */
static void on_pad (void)
{
	unsigned long n = gp_new;
	if (n & (PAD_A | PAD_B | PAD_X | PAD_Y))
		action ();
	else if ((n & PAD_UP) && g_state != ST_PLAY)	/* (haut : seulement pour lancer) */
		action ();
}

static void on_event (long t)
{
	if (gp_event (t))
		return;
	if (t == WEV_PAD)
		on_pad ();
	else if (t == WEV_MENU)
		do_menu ((uint32_t) GF_EV (EV_CODE));
	else if (t == WEV_KEY)
	{
		if (!GF_EV (EV_KIND))
			return;
		long id = ui_menu_key (0);
		if (id >= 0)
		{
			do_menu (id);
			return;
		}
		if (((uint32_t) GF_EV (EV_CHAR) | 0x20) == 'p')
		{
			gp_open ();
			return;
		}
		uint32_t code = (uint32_t) GF_EV (EV_CODE);
		if (code == KEY_SPACE_ || code == KEY_ENTER_)
			action ();
	}
	else if (t == WEV_FOCUS)
		g_btn = 0;
	else if (t == WEV_MOUSE)
	{
		long mx, my;
		gf_mouse (&mx, &my);
		long kind = GF_EV (EV_KIND);
		if (kind == 2)
		{
			g_btn = 0;
			return;
		}
		g_mx = mx;
		g_mmoved = 1;
		if (!kind || !(GF_EV (EV_CODE) & 1))
			return;
		if (gf_drag_top (HUD_H))
			return;
		g_btn = 1;
		action ();
	}
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Une image
 * --------------------------------------------------------------------------------------------------------------- */
static long bar (long x, long t, uint32_t c)
{
	if (!t)
		return x;
	long w = (long) ((unsigned long) t >> 3);
	gf_rect (x, GF_H - 5, w, 3, c);
	return x + w + 6;
}

static void draw (void)
{
	if (g_ldirty)
		build_layer ();
	else if (g_fdirty || g_hdirty)
	{
		gf_dst = gf_layer;
		if (g_fdirty)
		{
			g_fdirty = 0;
			draw_field ();
		}
		if (g_hdirty)
		{
			g_hdirty = 0;
			ui_canvas_begin (gf_cv);
			draw_hud ();
			ui_canvas_end ();
		}
		gf_dst = gf_frame;
	}
	gf_copy (gf_frame, gf_layer, GF_PITCH * GF_H);
	unsigned long t = g_tick;			/* deux lumieres derivent sur le decor */
	gf_bglow ((long) (sin8 (t >> 2) << 1) + 50, (long) sin8 ((t >> 3) + 40) + 80, g_sorb[0]);
	unsigned long d = sin8 ((t >> 2) + 200);
	gf_bglow ((long) (sin8 ((t >> 3) + 128) << 1) + 10, (long) (d + (d >> 1)), g_sorb[1]);
	for (int i = 0; i < NM; i++)			/* les poussieres */
		gf_bglow ((uint32_t) (g_mote[i].x >> 16), (uint32_t) (g_mote[i].y >> 16), g_smote);
	if (g_state != ST_TITLE)			/* le jeu lui-meme */
	{
		for (int i = 0; i < NC; i++)		/* capsules */
			if (g_cap[i].on)
				gf_sprite (g_cap[i].x >> 16, g_cap[i].y >> 16, g_scap[g_cap[i].kind]);
		for (int i = 0; i < NL; i++)		/* lasers */
			if (g_las[i].on)
				gf_sprite ((uint32_t) g_las[i].x, (uint32_t) g_las[i].y, g_slaser);
		if (g_state != ST_OVER && g_state != ST_LOST)
		{					/* raquette */
			long st = g_t_laser ? 2 : g_t_glue ? 1 : 0;
			gf_sprite (g_px, RKT_Y, g_spad[g_psize + (st << 2)]);
		}
		long thru = g_t_thru != 0;		/* 1 : balle traversante */
		for (int i = 0; i < NB; i++)
		{
			struct ball *b = &g_ball[i];
			if (!b->on)
				continue;
			if (!b->stuck)
				for (unsigned long j = 0; j < 3; j++)	/* la trainee : 3 points sur la longueur */
				{
					unsigned long k = (b->hi - ((j + 1) << 1)) & (HIST - 1);
					const gf_sprite_t *s = g_sdisc[j + (unsigned long) thru * 3];
					gf_glow ((long) b->h[k][0] - (long) (s[0] >> 1), (long) b->h[k][1] - (long) (s[1] >> 1), s);
				}
			gf_sprite ((b->x >> 16) - BALL_R, (b->y >> 16) - BALL_R, g_sball[thru]);
		}
	}
	for (int i = 0; i < NF; i++)			/* eclairs */
		if (g_flash[i].life)
			gf_glow (g_flash[i].x - FLASH_D / 2, g_flash[i].y - FLASH_D / 2, g_sflash);
	for (int i = 0; i < NP; i++)			/* particules */
	{
		struct part *p = &g_parts[i];
		if (!p->life)
			continue;
		uint32_t f = (uint32_t) p->life << 4;
		if (f > 256)
			f = 256;
		gf_rect (p->x >> 16, p->y >> 16, p->kind + 2, p->kind + 2, ga_shade (p->col, f));
	}
	long x = 28;					/* le temps des bonus, en bas */
	x = bar (x, g_t_thru, 0x00FF8830);
	x = bar (x, g_t_glue, 0x0040D080);
	x = bar (x, g_t_laser, 0x00F06040);
	x = bar (x, g_t_slow, 0x0060A0F0);
	bar (x, g_t_fast, 0x00F0D040);
}

int main (void)
{
	if (gf_open ("Arkanoid") < 0)
		return 0;
	ui_menu ((long) s_menus);
	g_bg = (uint32_t *) malloc (GF_PITCH * GF_H);
	if (!g_bg)
		return 0;
	gs_open ();
	g_best = gf_load_best (P_BEST);
	g_bp = g_bank;
	gen_noise ();
	gen_sprites ();
	gen_motes ();
	if (!ga_init (512 * 1024))
		return 0;
	gp_init (game_pause, game_resume, new_game, GF_W - 32, 0, on_pad);
	ui_canvas_begin (gf_cv);
	g_slogo = ga_text ("Arkanoid", 64, (long) ui.font_b, 0x00FFF0B8, 0x00F08828);
	ui_canvas_end ();
	goto_title ();
	gf_run (on_event, step, draw);
	gs_close ();
	return 0;
}
