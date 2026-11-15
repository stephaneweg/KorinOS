/*
 * tetris -- le Tetris d'Onyx (user/Apps/tetris/main.cpp), memes pieces, memes regles (gravite 30 - lignes images, 5
 * au mieux ; 100 points par ligne ; rotation qui se decale d'une case, deux pour la barre), habille : blocs en
 * relief, ciel genere qui change tous les 10 lignes, fantome, reserve, trois pieces a venir, animation de ligne,
 * particules, son, record.
 *
 * Fleches ou boutons a l'ecran ; haut, clic dans le puits : tourner ; Espace : chute ; C : reserve ; P : pause ;
 * R : recommencer. Manette : croix, A tourne, B chute, X reserve. Plein ecran ; Start, Echap, P ou le bouton
 * Pause : le menu de pause (gamepause.h).
 *
 * Couche fixe (gf_layer) : ciel, puits, panneaux, libelles ; refaite a chaque niveau. Tout ce qui bouge est en
 * sprites (gamedraw.h). Taille logique 640 x 480.
 *
 * Port en C++ depuis le source d'Onyx : les pieces (PIECES), collide, lock_piece, clear_lines (ici en deux temps,
 * animees), spawn, step_down, try_rotate et la gravite viennent d'Onyx ; la presentation vient de tetris.asm de
 * Korin, par gamefb.h, gamedraw.h, gamepause.h, gamesound.h (memes calculs entiers, donc les memes pixels), avec son
 * generateur (celui de gamefb, pas le congruentiel d'Onyx), la reserve, les pieces a venir, le niveau, son record
 * HOME:/tetris/best.txt, ses sons, ses boutons d'ecran et ses menus. Un seul source pour le Pi et le PC ; l'asm
 * reste dans le dossier jusqu'a la validation sur le Pi.
 */
#define GF_W		640
#define GF_H		480
#include "../../inc/gamepause.h"

#define COLS		10
#define ROWS		20
#define CELL		22
#define MINI		16
#define WX		210			/* le puits */
#define WY		20
#define WW		(COLS * CELL)
#define WH		(ROWS * CELL)
#define NRAIN		9			/* pieces qui tombent sur l'ecran titre */
#define NM		18			/* poussieres */
#define NTHEME		6
#define LOGO_X		40
#define LOGO_Y		72

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_CLEAR, ST_DYING, ST_OVER };	/* ST_CLEAR : lignes en train de partir */
enum { AC_LEFT = 1, AC_RIGHT, AC_DOWN, AC_ROT, AC_DROP, AC_HOLD, AC_PAUSE };	/* aussi les zones d'appui */
enum { ID_NEW = 1, ID_PAUSE, ID_SOUND, ID_QUIT, ID_MUSIC };
enum { TX_PROMPT, TX_PAUSE, TX_RESUME, TX_OVER, TX_AGAIN, TX_TETRIS, TX_LEVEL, TX_RECORD, TX_BEST, TX_SUB, NTX };

#define KEY_ENTER_	0x28
#define KEY_SPACE_	0x2C
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50
#define KEY_DOWN_	0x51
#define KEY_UP_		0x52

#define P_DIR		"HOME:/tetris"
#define P_BEST		"HOME:/tetris/best.txt"

/* ---- l'etat ---- */
static long g_state, g_timer;
static unsigned long g_tick;
static long g_type, g_rot, g_px, g_py;
static long g_hold, g_hused;			/* piece en reserve (-1 : aucune), deja prise pour cette piece */
static unsigned long g_score, g_show, g_best;	/* (le score affiche rattrape) */
static long g_lines, g_level, g_newbest;
static long g_frames, g_last;
static long g_nclr;
static long g_grey;				/* lignes figees (depuis le bas) */
static long g_shake;
static long g_pop;				/* texte qui monte : reste, sprite */
static const gf_sprite_t *g_popspr;
static long g_rep, g_rept, g_repk;		/* action qui se repete, son delai, sa touche (0 : bouton, -1 : manette) */
static long g_rotk;
static long g_btn;				/* bouton d'ecran tenu */
static long g_ldirty;
static gf_sprite_t *g_sb[8], *g_sm[8], *g_sg[7];	/* blocs (7 couleurs, gris), petits blocs, fantomes */
static gf_sprite_t *g_sglow, *g_sorb[2], *g_smote;
static gf_sprite_t *g_dbig[10], *g_dsmall[10];
static gf_sprite_t *g_tx[NTX];
static long g_next[3];
static unsigned char g_pc[8];			/* les quatre cases d'une piece : x, y */
static long g_off[ROWS];			/* decalage de chaque ligne quand elle tombe */
static unsigned char g_clr[ROWS];
static unsigned char g_field[ROWS * COLS];	/* 0 vide, sinon piece + 1 */
static unsigned char g_flash[ROWS * COLS];
static struct { long x, y, v; uint32_t type, rot; } g_rain[NRAIN];	/* x, y : 16.16 */
static struct { int32_t x, y, v, pad; } g_mote[NM];

/* ---- tables ---- */
/* les sept pieces d'Onyx, quatre rotations : bit 15 - (y * 4 + x) */
static const unsigned short PIECES[7][4] = {
	{ 0x0F00, 0x2222, 0x00F0, 0x4444 },	/* I */
	{ 0xCC00, 0xCC00, 0xCC00, 0xCC00 },	/* O */
	{ 0x0E40, 0x4C40, 0x4E00, 0x4640 },	/* T */
	{ 0x06C0, 0x8C40, 0x6C00, 0x4620 },	/* S */
	{ 0x0C60, 0x4C80, 0xC600, 0x2640 },	/* Z */
	{ 0x44C0, 0x8E00, 0xC880, 0x0E20 },	/* J */
	{ 0x4460, 0x0E80, 0xC440, 0x2E00 },	/* L */
};
static const uint32_t colors[8] = { 0x0020C8E0, 0x00F0C820, 0x00A848E8, 0x0040C850, 0x00E84040, 0x004070F0, 0x00F08A28,
	0x00687080 };
static const signed char kicks[] = { 0, -1, 1, -2, 2 };
/* x, y, l, h, action */
static const struct ga_zone_t zones[] = {
	{ 16, 372, 85, 88, AC_LEFT }, { 109, 372, 85, 88, AC_RIGHT }, { 446, 300, 178, 64, AC_ROT },
	{ 446, 372, 85, 88, AC_DOWN }, { 539, 372, 85, 88, AC_DROP }, { 446, 260, 178, 32, AC_PAUSE },
	{ 16, 20, 178, 104, AC_HOLD }, { 0, 0, 0, 0, 0 } };
/* ciels : haut, bas, nuages */
static const uint32_t themes[NTHEME][3] = {
	{ 0x00101C3C, 0x00061024, 0x00304C80 }, { 0x00301438, 0x00100820, 0x00783868 },
	{ 0x000C3034, 0x00061414, 0x00207870 }, { 0x00382010, 0x00180C08, 0x00885830 },
	{ 0x00142C14, 0x00081408, 0x00388040 }, { 0x00301020, 0x00180410, 0x00903048 } };
static const unsigned char pad_bits[4] = { 0, 2, 3, 1 };	/* AC_LEFT, AC_RIGHT, AC_DOWN -> bit PAD_* */
static const unsigned char logo_col[6] = { 0, 4, 1, 3, 2, 6 };
/* T E T R I S, 5 x 7 */
static const unsigned char logo[6][7] = {
	{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
	{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
	{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } };
static const struct gs_ent snd_line[] = { { 0, 523, 70, AKW_TRIANGLE, 110 }, { 70, 784, 110, AKW_TRIANGLE, 110 },
	{ 0, 0, 0, 0, 0 } };
static const struct gs_ent snd_tetris[] = { { 0, 523, 70, AKW_SQUARE, 90 }, { 70, 659, 70, AKW_SQUARE, 90 },
	{ 140, 784, 70, AKW_SQUARE, 90 }, { 210, 1047, 240, AKW_SQUARE, 100 }, { 0, 0, 0, 0, 0 } };
static const struct ga_txe tx_table[NTX] = {
	{ "Espace, A ou toucher pour jouer", 18, 1, 0x00FFFFFF, 0x00C8DCF8 },
	{ "Pause", 26, 1, 0x00FFFFFF, 0x00A8D8FF },
	{ "P ou toucher pour reprendre", 12, 0, 0x00C8D4E4, 0x00A8B4C8 },
	{ "Partie termin\xC3\xA9" "e", 22, 1, 0x00FFD070, 0x00F08840 },
	{ "Espace, A ou toucher : rejouer", 12, 0, 0x00C8D4E4, 0x00A8B4C8 },
	{ "TETRIS !", 34, 1, 0x00FFF0A0, 0x00FF9030 },
	{ "Niveau sup\xC3\xA9rieur", 24, 1, 0x00C0FFD0, 0x0060D890 },
	{ "Record", 15, 1, 0x00B8C8E0, 0x008C9CB8 },
	{ "Nouveau record !", 15, 1, 0x00FFE080, 0x00F0B040 },
	{ "Les pi\xC3\xA8" "ces tombent, les lignes s'en vont", 14, 0, 0x00D0DCF0, 0x00A0B0C8 },
};
/* libelles des boutons, dans l'ordre des actions */
static const char *const glyphs[7] = { "\xE2\x97\x80", "\xE2\x96\xB6", "\xE2\x96\xBC", "\xE2\x86\xBB", "\xE2\x87\x93", "",
	"Pause" };
static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tPause\tP\nI3c\tSon\nI5c\tMusique\n-\nI4\tQuitter\n";

/* ---------------------------------------------------------------------------------------------------------------
 *  Sprites
 * --------------------------------------------------------------------------------------------------------------- */
static void gen_sprites (void)
{
	for (int i = 0; i < 8; i++)
	{
		g_sb[i] = ga_block (CELL - 1, CELL - 1, 4, colors[i]);
		g_sm[i] = ga_block (MINI - 1, MINI - 1, 3, colors[i]);
		if (i < 7)
		{				/* le fantome : un bloc tres estompe */
			g_sg[i] = ga_block (CELL - 1, CELL - 1, 4, colors[i]);
			ga_fade (g_sg[i], 62);
		}
	}
	g_sglow = ga_disc (CELL + 6, 0x00707078);	/* lueur d'un bloc qui se pose */
	g_sorb[0] = ga_disc (148, 0x00203048);
	g_sorb[1] = ga_disc (120, 0x00301838);
	g_smote = ga_disc (5, 0x00607088);
	ui_canvas_begin (gf_cv);
	ga_digits (g_dbig, 22, (long) ui.font_b, 0x00FFFFFF, 0x00A8D8FF);
	ga_digits (g_dsmall, 15, (long) ui.font_b, 0x00E8F0FF, 0x00B8C8E0);
	ga_texts (tx_table, g_tx, NTX);
	ui_canvas_end ();
}

static void gen_motes (void)
{
	for (int i = 0; i < NM; i++)
	{
		g_mote[i].x = (int32_t) (gf_below (GF_W) << 16);
		g_mote[i].y = (int32_t) (gf_below (GF_H) << 16);
		g_mote[i].v = (int32_t) (gf_below (30000) + 9000);
	}
}

/* une piece de la pluie, en haut */
static void rain_new (int i)
{
	g_rain[i].x = (long) (gf_below (GF_W - 40) << 16);
	g_rain[i].y = -60l * 65536;
	g_rain[i].v = (long) gf_below (50000) + 24000;
	g_rain[i].type = (uint32_t) gf_below (7);
	g_rain[i].rot = (uint32_t) gf_below (4);
}

static void gen_rain (void)
{
	for (int i = 0; i < NRAIN; i++)
	{
		rain_new (i);
		g_rain[i].y = (long) (gf_below (GF_H) << 16);
	}
}

/* ---------------------------------------------------------------------------------------------------------------
 *  La couche fixe
 * --------------------------------------------------------------------------------------------------------------- */
/* un panneau de verre */
static void glass (long x, long y, long w, long h)
{
	ga_panel (x, y, (unsigned long) w, (unsigned long) h, 10, 0x00202C40, 0x00121A28, 210, 0x00708098);
	ga_mark (x, y, w, h);
}

/* texte, x, base : un libelle discret (canevas) */
static void t_label (const char *s, long x, long base) { gf_text (s, x, base, 0x00A8B4C4, 12, (long) ui.font_b); }

static void build_layer (void)
{
	g_ldirty = 0;
	const uint32_t *th = themes[(unsigned long) g_level % NTHEME];	/* le ciel du niveau */
	ga_sky (th[0], th[1], th[2], 140);
	gf_dst = gf_layer;
	if (g_state == ST_TITLE)
	{						/* l'ecran titre : un voile sous le texte */
		ga_panel (120, 214, 400, 170, 18, 0x00101828, 0x00080C18, 180, 0x00506080);
		gf_dst = gf_frame;
		return;
	}
	/* le puits : un cadre de metal, le fond sombre */
	ga_panel (WX - 8, WY - 8, WW + 16, WH + 16, 10, 0x005C6A84, 0x0028303E, 256, 0x00A0B0C8);
	ga_panel (WX - 2, WY - 2, WW + 3, WH + 3, 3, 0x00080C14, 0x00121826, 256, 0);
	for (long i = 1; i < COLS; i++)			/* les colonnes */
		gf_rect (i * CELL + WX - 1, WY, 1, WH, 0x00182030);
	for (long i = 1; i < ROWS; i++)			/* les lignes */
		gf_rect (WX, i * CELL + WY - 1, WW, 1, 0x00182030);
	for (unsigned long i = 0; i < 14; i++)		/* l'ombre du bord haut */
		ga_panel (WX, (long) i + WY, WW, 1, 0, 0, 0, (16 - i) << 4, 0);
	ga_mark (WX - 8, WY - 8, WW + 16, WH + 16);
	glass (16, 20, 178, 104);			/* reserve */
	glass (16, 134, 178, 62);			/* score */
	glass (16, 204, 86, 62);			/* lignes */
	glass (108, 204, 86, 62);			/* niveau */
	glass (446, 36, 178, 158);			/* a venir */
	glass (446, 202, 178, 50);			/* record */
	for (const struct ga_zone_t *z = zones; z->id; z++)	/* les boutons d'ecran (sauf la reserve) */
		if (z->id != AC_HOLD)
		{
			ga_zone_panel (z, 10, 0x00303C54, 150, 0x008090A8);
			ga_mark (z->x, z->y, z->w, z->h);
		}
	ui_canvas_begin (gf_cv);
	t_label ("R\xC3\xA9serve", 28, 38);
	t_label ("Score", 28, 152);
	t_label ("Lignes", 28, 222);
	t_label ("Niveau", 120, 222);
	t_label ("A venir", 458, 54);
	t_label ("Record", 458, 220);
	for (const struct ga_zone_t *z = zones; z->id; z++)	/* les libelles des boutons */
	{
		if (z->id == AC_HOLD)
			continue;
		long cx = z->x + (z->w >> 1), base = z->y + (z->h >> 1) + 8, px = 22;
		if (z->id == AC_PAUSE)
		{
			px = 13;
			base -= 3;
		}
		gf_text_c (glyphs[z->id - 1], cx, base, 0x00E0E8F4, px, (long) ui.font_b);
	}
	ui_canvas_end ();
	gf_dst = gf_frame;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Regles (celles d'Onyx)
 * --------------------------------------------------------------------------------------------------------------- */
/* piece, rotation -> g_pc : ses quatre cases */
static void cells_of (long type, long rot)
{
	unsigned m = PIECES[type][rot];
	unsigned char *p = g_pc;
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			if ((m >> (15 - (y * 4 + x))) & 1)
			{
				*p++ = (unsigned char) x;
				*p++ = (unsigned char) y;
			}
}

/* piece, rotation, x, y -> 1 si elle touche */
static int collide (long type, long rot, long px, long py)
{
	cells_of (type, rot);
	for (int i = 0; i < 4; i++)
	{
		long gx = g_pc[2 * i] + px, gy = g_pc[2 * i + 1] + py;
		if (gx < 0 || gx >= COLS || gy >= ROWS) return 1;
		if (gy >= 0 && g_field[gy * COLS + gx]) return 1;
	}
	return 0;
}

/* decalage -> 1 si la piece n'y entre pas */
static int blocked (long dx, long dy) { return collide (g_type, g_rot, g_px + dx, g_py + dy); }

/* ---------------------------------------------------------------------------------------------------------------
 *  La musique (15/11/2026) : un air original en la mineur, huit mesures en croches qui bouclent (la, fa, mi, ré, sol),
 *  sur un synthé FM à part des effets (trois voix : mélodie en carré, basse en triangle, charleston en bruit). Elle
 *  joue pendant la partie, s'arrête en pause et à la fin ; le tempo monte avec le niveau.
 * --------------------------------------------------------------------------------------------------------------- */

#define MU_STEPS	64			/* croches : 8 mesures de 8 */
#define MU_HOLD		-1			/* la note d'avant continue */

/* notes MIDI (69 = la 440 Hz), 0 : silence */
static const signed char mu_lead[MU_STEPS] = {
	69, MU_HOLD, 72, 76, 74, 72, 71, MU_HOLD,	72, MU_HOLD, 69, MU_HOLD, 64, MU_HOLD, 69, 71,
	72, MU_HOLD, 74, 76, 77, 76, 74, 72,		71, MU_HOLD, 67, MU_HOLD, 71, MU_HOLD, 76, MU_HOLD,
	77, MU_HOLD, 76, 74, 76, MU_HOLD, 72, MU_HOLD,	74, MU_HOLD, 72, 71, 72, MU_HOLD, 69, MU_HOLD,
	71, 72, 74, 71, 72, 74, 76, 79,			81, MU_HOLD, 76, MU_HOLD, 69, MU_HOLD, 0, MU_HOLD };
/* la basse : la fondamentale et son octave, une croche chacune (la, la, fa, mi ; ré, la, sol puis mi, la) */
static const signed char mu_root[8] = { 45, 45, 41, 40, 38, 45, 43, 45 };

static long mu_fm;					/* synthé de la musique (0 : pas de son) */
static int mu_on = 1, mu_play;				/* Musique cochée ; en train de jouer */
static long mu_step;
static unsigned long mu_next, mu_bass_off, mu_hat_off;

/* note MIDI -> millihertz */
static long mu_mhz (int n)
{
	static const short semis[12] = { 1000, 1059, 1122, 1189, 1260, 1335, 1414, 1498, 1587, 1682, 1782, 1888 };
	int d = n - 69, oct = d >= 0 ? d / 12 : -((11 - d) / 12);
	long f = 440000L * semis[d - oct * 12] / 1000;
	return oct >= 0 ? f << oct : f >> -oct;
}

static unsigned long mu_step_ms (void)
{
	long bpm = 120 + 4 * (g_level < 15 ? g_level : 15);	/* (deux croches par temps) */
	return (unsigned long) (30000 / bpm);
}

static void mu_silence (void)
{
	if (mu_fm)
		ak_fm_stop (mu_fm, -1);
	mu_bass_off = mu_hat_off = 0;
}

/* la partie commence ou reprend (rewind : depuis le début) */
static void mu_start (int rewind)
{
	if (rewind)
		mu_step = 0;
	if (!mu_fm || !mu_on || gs_mute || mu_play)
		return;
	mu_play = 1;
	mu_next = gs_time_ms ();
}

static void mu_stop (void)
{
	mu_play = 0;
	mu_silence ();
}

/* à chaque image : les croches dues, les fins de notes courtes */
static void mu_tick (void)
{
	if (!mu_play)
		return;
	if (gs_mute) {					/* (Son décoché : la musique se tait aussi) */
		mu_stop ();
		return;
	}
	unsigned long now = gs_time_ms ();
	if (mu_bass_off && now >= mu_bass_off) {
		ak_fm_stop (mu_fm, 1);
		mu_bass_off = 0;
	}
	if (mu_hat_off && now >= mu_hat_off) {
		ak_fm_stop (mu_fm, 2);
		mu_hat_off = 0;
	}
	if (now < mu_next)
		return;
	unsigned long len = mu_step_ms ();
	if (now - mu_next > 4 * len)			/* (une longue absence : on repart d'ici, sans rattraper) */
		mu_next = now;
	long i = mu_step;
	int n = mu_lead[i];
	if (n > 0)
		ak_fm_start (mu_fm, 0, mu_mhz (n), AKW_SQUARE, 34);
	else if (n == 0)
		ak_fm_stop (mu_fm, 0);
	int root = mu_root[i / 8];
	if (i / 8 == 6 && (i & 7) >= 4)			/* (la 7e mesure : sol, puis mi) */
		root = 40;
	ak_fm_start (mu_fm, 1, mu_mhz ((i & 1) ? root + 12 : root), AKW_TRIANGLE, 80);
	mu_bass_off = now + len * 6 / 10;
	if (i & 1) {
		ak_fm_start (mu_fm, 2, 6000000, AKW_NOISE, 14);
		mu_hat_off = now + 25;
	}
	mu_step = (i + 1) % MU_STEPS;
	mu_next += len;
}

static void set_state (long s)
{
	g_state = s;
	g_timer = 0;
	gf_still = s == ST_PAUSE;
	gf_redraw = 1;
	if (s == ST_PLAY || s == ST_CLEAR)
		mu_start (0);
	else
		mu_stop ();
}

static void keep_best (void)
{
	if (g_score <= g_best)
		return;
	g_best = g_score;
	g_newbest = 1;
	gf_save_best (P_DIR, P_BEST, g_best);
}

static void game_over (void)
{
	keep_best ();
	set_state (ST_DYING);
	g_grey = ROWS;
	gs_seq (gs_tab_lose);
}

/* la piece entre en haut */
static void put_piece (void)
{
	g_rot = 0;
	g_px = 3;
	g_py = -1;
	if (blocked (0, 0))
		game_over ();
}

/* la prochaine piece entre */
static void spawn (void)
{
	g_type = g_next[0];
	g_next[0] = g_next[1];
	g_next[1] = g_next[2];
	g_next[2] = (long) gf_below (7);
	g_hused = 0;
	put_piece ();
}

/* restart d'Onyx, avec la reserve et les pieces a venir */
static void new_game (void)
{
	memset (g_field, 0, sizeof g_field);
	memset (g_flash, 0, sizeof g_flash);
	memset (g_off, 0, sizeof g_off);
	memset (g_clr, 0, sizeof g_clr);
	g_score = g_show = 0;
	g_lines = g_level = 0;
	mu_step = 0;				/* (la musique repart du début) */
	g_frames = g_last = 0;
	g_newbest = 0;
	g_pop = 0;
	g_rep = 0;
	g_hold = -1;
	ga_parts_clear ();
	for (int i = 0; i < 3; i++)
		g_next[i] = (long) gf_below (7);
	set_state (ST_PLAY);
	g_ldirty = 1;
	spawn ();
}

/* la piece se pose, les lignes pleines partent (clear_lines d'Onyx : marquees ici, enlevees par collapse) */
static void lock_piece (void)
{
	cells_of (g_type, g_rot);
	for (int i = 0; i < 4; i++)
	{
		long gx = g_pc[2 * i] + g_px, gy = g_pc[2 * i + 1] + g_py;
		if (gy < 0)
			continue;
		g_field[gy * COLS + gx] = (unsigned char) (g_type + 1);
		g_flash[gy * COLS + gx] = 10;
	}
	gs_note (180, 40, AKW_TRIANGLE, 110);
	long n = 0;					/* les lignes pleines */
	for (int r = 0; r < ROWS; r++)
	{
		int full = 1;
		for (int c = 0; c < COLS; c++) if (!g_field[r * COLS + c]) full = 0;
		g_clr[r] = (unsigned char) full;
		n += full;
	}
	if (!n)
	{
		spawn ();
		return;
	}
	g_nclr = n;
	g_lines += n;
	g_score += (unsigned long) (100 * n);
	set_state (ST_CLEAR);
	const struct gs_ent *snd = snd_line;
	if (n >= 4)
	{
		snd = snd_tetris;
		g_popspr = g_tx[TX_TETRIS];
		g_pop = 70;
		g_shake = 6;
	}
	gs_seq (snd);
}

/* les lignes marquees s'en vont, le reste tombe */
static void collapse (void)
{
	long w = ROWS - 1;
	for (long r = ROWS - 1; r >= 0; r--)
	{
		if (g_clr[r])
			continue;
		if (w != r)
			memcpy (&g_field[w * COLS], &g_field[r * COLS], COLS);
		g_off[w] = (w - r) * CELL;		/* (positif : la ligne part d'en haut et tombe) */
		w--;
	}
	for (; w >= 0; w--)				/* le haut se vide */
		memset (&g_field[w * COLS], 0, COLS);
	memset (g_clr, 0, sizeof g_clr);
	long lv = g_lines / 10;				/* le niveau : tous les 10 lignes */
	if (lv == g_level)
		return;
	g_level = lv;
	g_ldirty = 1;
	g_popspr = g_tx[TX_LEVEL];
	g_pop = 70;
	gs_seq (gs_tab_win);
}

static void step_down (void)
{
	if (!blocked (0, 1))
		g_py++;
	else
		lock_piece ();
}

/* tourner : sur place, puis une case a cote, deux pour la barre */
static void try_rotate (void)
{
	long nr = (g_rot + 1) & 3;
	for (unsigned i = 0; i < sizeof kicks; i++)
	{
		long k = kicks[i];
		if (g_type && (k == 2 || k == -2))	/* (la barre seule a +-2) */
			continue;
		if (collide (g_type, nr, g_px + k, g_py))
			continue;
		g_px += k;
		g_rot = nr;
		gs_note (620, 25, AKW_TRIANGLE, 70);
		return;
	}
}

/* decalage : la piece glisse */
static void slide (long d)
{
	if (blocked (d, 0))
		return;
	g_px += d;
	gs_note (300, 12, AKW_SQUARE, 26);
}

static void hard_drop (void)
{
	long n = 0;
	while (!blocked (0, 1))
	{
		g_py++;
		n++;
	}
	if (n)
	{						/* la trainee, sous chaque case */
		cells_of (g_type, g_rot);
		for (int i = 0; i < 4; i++)
		{
			long x = (g_pc[2 * i] + g_px) * CELL + WX + 10, y = (g_pc[2 * i + 1] + g_py) * CELL + WY;
			for (int k = 0; k < 6; k++)
			{
				long r = (long) gf_below ((unsigned long) (n + 1) << 1);
				ga_part (x, y, 0, -((r * 50000) >> 3), colors[g_type], 14, 2, 0);
			}
		}
		g_shake = 3;
		gs_note (110, 70, AKW_NOISE, 90);
	}
	lock_piece ();
}

/* la reserve : la piece y va, celle qui y etait entre */
static void do_hold (void)
{
	if (g_hused)
		return;
	long old = g_hold;
	g_hold = g_type;
	if (old < 0)
		spawn ();
	else
	{
		g_type = old;
		put_piece ();
	}
	g_hused = 1;
	gs_note (440, 40, AKW_SINE, 90);
}

/* (gamepause) la partie s'arrete -> 1 si elle etait en cours */
static long game_pause (void)
{
	g_rep = 0;
	g_btn = 0;
	if (g_state != ST_PLAY)
		return 0;
	set_state (ST_PAUSE);
	return 1;
}

/* (gamepause) la partie repart */
static void game_resume (void)
{
	if (g_state == ST_PAUSE)
		set_state (ST_PLAY);
}

/* action AC_* */
static void act (long a)
{
	if (a == AC_PAUSE)
	{
		gp_open ();
		return;
	}
	if (g_state != ST_PLAY)
		return;
	if (a == AC_LEFT)
		slide (-1);
	else if (a == AC_RIGHT)
		slide (1);
	else if (a == AC_DOWN)
	{
		step_down ();
		g_last = g_frames;
	}
	else if (a == AC_ROT)
		try_rotate ();
	else if (a == AC_DROP)
	{
		hard_drop ();
		g_last = g_frames;
	}
	else if (a == AC_HOLD)
		do_hold ();
}

/* action qui se repete, sa touche (0 : bouton, -1 : manette) */
static void rep_start (long a, long key)
{
	if (g_rep == a)					/* (la repetition du clavier) */
		return;
	g_rep = a;
	g_repk = key;
	g_rept = 13;
	act (a);
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Une image
 * --------------------------------------------------------------------------------------------------------------- */
/* rang (0..4) : la case de ce rang de chaque cote du milieu part en eclats, sur chaque ligne marquee */
static void blow_cell (long c, long r)
{
	unsigned v = g_field[r * COLS + c];
	g_field[r * COLS + c] = 0;
	if (v)
		ga_burst (c * CELL + WX + 10, r * CELL + WY + 10, colors[v - 1], 9, 180000, 16, 3, 9000);
}

static void clear_step (long k)
{
	for (long r = 0; r < ROWS; r++)
		if (g_clr[r])
		{
			blow_cell (4 - k, r);
			blow_cell (5 + k, r);
		}
}

static void step (void)
{
	g_tick++;
	gs_tick ();
	mu_tick ();
	ga_parts_step ();
	for (int i = 0; i < NM; i++)			/* les poussieres montent */
	{
		int32_t y = g_mote[i].y - g_mote[i].v;
		if (y < 0)
			y = GF_H << 16;
		g_mote[i].y = y;
	}
	if (g_score > g_show)				/* le score affiche rattrape */
		g_show += ((g_score - g_show) >> 3) + 1;
	if (g_shake)
		g_shake--;
	if (g_pop)
		g_pop--;
	for (int i = 0; i < ROWS * COLS; i++)
		if (g_flash[i])
			g_flash[i]--;
	for (int i = 0; i < ROWS; i++)			/* les lignes finissent de tomber */
		if (g_off[i])
			g_off[i] = g_off[i] < 7 ? 0 : g_off[i] - 7;
	if (g_state == ST_TITLE)
	{
		for (int i = 0; i < NRAIN; i++)
		{
			g_rain[i].y += g_rain[i].v;
			if ((g_rain[i].y >> 16) >= GF_H)
				rain_new (i);
		}
		return;
	}
	if (g_state == ST_PLAY)
	{
		long a = g_rep;				/* la touche tenue */
		if (a)
		{
			long k = g_repk, held;
			if (!k)
				held = 1;		/* (le bouton : relache par la souris) */
			else if (k < 0)
				held = (gp_now >> pad_bits[a]) & 1;
			else
				held = gf_held[k];
			if (!held)
				g_rep = 0;
			else if (!--g_rept)
			{
				g_rept = a == AC_DOWN ? 2 : 3;
				act (a);
			}
		}
		if (g_state != ST_PLAY)			/* (elle a pu changer) */
			return;
		long speed = 30 - g_lines;		/* plus vite avec les lignes, comme Onyx */
		if (speed < 5)
			speed = 5;
		if (++g_frames - g_last < speed)
			return;
		g_last = g_frames;
		step_down ();
	}
	else if (g_state == ST_CLEAR)
	{						/* les cases s'en vont depuis le milieu */
		long t = ++g_timer;
		if ((t & 3) == 1)
			clear_step (t >> 2);
		if (t < 20)
			return;
		collapse ();
		set_state (ST_PLAY);
		spawn ();
	}
	else if (g_state == ST_DYING)
	{
		if (!(g_tick & 1))
			return;
		if (g_grey)
			g_grey--;
		else
			set_state (ST_OVER);
	}
	else
		g_timer++;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Evenements
 * --------------------------------------------------------------------------------------------------------------- */
static void do_menu (long id)
{
	if (id == ID_QUIT)
		gf_quit = 1;
	else if (id == ID_NEW)
		new_game ();
	else if (id == ID_PAUSE)
		gp_open ();
	else if (id == ID_SOUND)
	{
		ui_menu_state (ID_SOUND, gs_toggle () ? UMS_CHECKED : 0);
		if (!gs_mute && (g_state == ST_PLAY || g_state == ST_CLEAR))
			mu_start (0);
	}
	else if (id == ID_MUSIC)
	{
		mu_on ^= 1;
		ui_menu_state (ID_MUSIC, mu_on ? UMS_CHECKED : 0);
		if (!mu_on)
			mu_stop ();
		else if (g_state == ST_PLAY || g_state == ST_CLEAR)
			mu_start (0);
	}
}

/* Entree, clic, appui hors de la partie -> 1 si cela a servi */
static long start_act (void)
{
	if (g_state == ST_TITLE)
		new_game ();
	else if (g_state == ST_OVER)
	{
		if (g_timer >= 25)			/* (pas d'un coup de doigt egare) */
			new_game ();
	}
	else if (g_state == ST_PAUSE)
		gp_open ();
	else
		return 0;
	return 1;
}

static void on_key (void)
{
	uint32_t code = (uint32_t) GF_EV (EV_CODE);
	if (!GF_EV (EV_KIND))
	{
		if (code == KEY_UP_)			/* haut relache : on peut tourner encore */
			g_rotk = 0;
		return;
	}
	long id = ui_menu_key (0);
	if (id >= 0)
	{
		do_menu (id);
		return;
	}
	uint32_t ch = (uint32_t) GF_EV (EV_CHAR) | 0x20;
	if (ch == 'p')
		gp_open ();
	else if (ch == 'r')
	{
		if (g_state != ST_TITLE)
			new_game ();
	}
	else if (ch == 'c')
		act (AC_HOLD);
	else if (code == KEY_ENTER_)
		start_act ();
	else if (code == KEY_SPACE_)
	{
		if (!start_act ())
			act (AC_DROP);
	}
	else if (code == KEY_LEFT_)
		rep_start (AC_LEFT, code);
	else if (code == KEY_RIGHT_)
		rep_start (AC_RIGHT, code);
	else if (code == KEY_DOWN_)
		rep_start (AC_DOWN, code);
	else if (code == KEY_UP_ && !g_rotk)		/* (un tour par appui) */
	{
		g_rotk = 1;
		act (AC_ROT);
	}
}

static void on_mouse (void)
{
	long mx, my;
	gf_mouse (&mx, &my);
	long kind = GF_EV (EV_KIND);
	if (kind == 2)
	{
		g_btn = 0;
		if (g_rep && !g_repk)			/* le bouton d'ecran est relache */
			g_rep = 0;
		return;
	}
	if (kind != 1 || !(GF_EV (EV_CODE) & 1))
		return;
	if (gf_drag_top (16))
		return;
	if (start_act ())
		return;
	long id = (long) ga_zone (zones, (unsigned long) mx, (unsigned long) my, 0);
	if (id)
	{
		g_btn = id;
		if (id <= AC_DOWN)
			rep_start (id, 0);
		else
			act (id);
	}
	else if ((unsigned long) (mx - WX) < WW && (unsigned long) (my - WY) < WH)
		act (AC_ROT);				/* un appui dans le puits tourne */
}

/* WEV_PAD : les fronts ; manette 0 (le clavier mappe) deja arrivee en touches */
static void on_pad (void)
{
	if (!GF_EV (EV_KIND))
		return;
	unsigned long n = gp_new;			/* (Start : gamepause) */
	if (n & PAD_A)
	{
		if (start_act ())
			return;
		act (AC_ROT);
	}
	if (n & PAD_B)
		act (AC_DROP);
	if (n & PAD_X)
		act (AC_HOLD);
	if (n & PAD_LEFT)
		rep_start (AC_LEFT, -1);
	if (n & PAD_RIGHT)
		rep_start (AC_RIGHT, -1);
	if (n & PAD_DOWN)
		rep_start (AC_DOWN, -1);
}

static void on_event (long t)
{
	if (gp_event (t))
		return;
	if (t == WEV_MENU)
		do_menu ((uint32_t) GF_EV (EV_CODE));
	else if (t == WEV_KEY)
		on_key ();
	else if (t == WEV_MOUSE)
		on_mouse ();
	else if (t == WEV_PAD)
		on_pad ();
	else if (t == WEV_FOCUS)
	{
		g_btn = 0;
		g_rep = 0;
	}
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Le dessin
 * --------------------------------------------------------------------------------------------------------------- */
/* piece, rotation, coin, pas, table de sprites, y minimal */
static void draw_piece (long type, long rot, long x, long y, long step, gf_sprite_t *const *tab, long ymin)
{
	const gf_sprite_t *s = tab[type];
	cells_of (type, rot);
	for (int i = 0; i < 4; i++)
	{
		long cy = g_pc[2 * i + 1] * step + y;
		if (cy >= ymin)
			ga_ablit (g_pc[2 * i] * step + x, cy, s);
	}
}

/* piece, centre : la piece en petit, au milieu */
static void draw_mini (long type, long cx, long cy)
{
	cells_of (type, 0);
	unsigned long x0 = 9, x1 = 0, y0 = 9, y1 = 0;	/* sa boite */
	for (int i = 0; i < 4; i++)
	{
		unsigned long x = g_pc[2 * i], y = g_pc[2 * i + 1];
		if (x < x0) x0 = x;
		if (x > x1) x1 = x;
		if (y < y0) y0 = y;
		if (y > y1) y1 = y;
	}
	/* (x0 + x1 + 1 : deux fois le milieu de la boite, en cases ; le coin est donc a centre - w / 2) */
	unsigned long w = (x0 + x1 + 1) * MINI, h = (y0 + y1 + 1) * MINI;
	draw_piece (type, 0, cx - (long) (w >> 1), cy - (long) (h >> 1), MINI, g_sm, 0);
}

static void draw_field (void)
{
	long sh = g_shake;				/* le puits tremble */
	if (sh & 1)
		sh = -sh;
	sh += sh;
	for (long r = 0; r < ROWS; r++)
	{
		long y = r * CELL + WY - g_off[r] + sh;
		for (long c = 0; c < COLS; c++)
		{
			long i = r * COLS + c;
			unsigned v = g_field[i];
			if (!v)
				continue;
			long k = v - 1;
			if (g_state >= ST_DYING && r >= g_grey)
				k = 7;			/* le puits se fige, en gris */
			if (y < WY)
				continue;
			ga_ablit (c * CELL + WX, y, g_sb[k]);
			if (g_flash[i])			/* elle vient de se poser */
				gf_glow (c * CELL + WX - 3, y - 3, g_sglow);
		}
	}
	if (g_state != ST_CLEAR)
		return;
	for (long r = 0; r < ROWS; r++)			/* les lignes qui partent : un eclair */
		if (g_clr[r])
			ga_panel (WX, r * CELL + WY, WW, CELL - 1, 0, 0x00FFFFFF, 0x00FFFFFF,
				  (unsigned long) (20 - g_timer) << 3, 0);
}

/* texte TX_*, centre, y */
static void tx_c (int t, long cx, long y) { ga_show (g_tx[t], cx, y); }

/* un voile et deux lignes au milieu du puits : titre TX_*, suite TX_* */
static void well_box (int title, int sub)
{
	ga_panel (WX, WY, WW, WH, 0, 0, 0, 120, 0);
	ga_panel (WX + 12, 170, WW - 24, 120, 12, 0x00283448, 0x00141C2C, 240, 0x00809CC0);
	tx_c (title, WX + WW / 2, 184);
	tx_c (sub, WX + WW / 2, 252);
}

static void draw_title (void)
{
	for (int i = 0; i < NRAIN; i++)			/* la pluie de pieces */
		draw_piece (g_rain[i].type, g_rain[i].rot, g_rain[i].x >> 16, g_rain[i].y >> 16, MINI, g_sm, -100);
	for (long l = 0; l < 6; l++)			/* le nom, en blocs ; une vague le traverse */
		for (long r = 0; r < 7; r++)
			for (long c = 0; c < 5; c++)
			{
				if (!((logo[l][r] >> (4 - c)) & 1))
					continue;
				unsigned long ph = (unsigned long) (l * 6 + c) << 3;
				long y = r * MINI + LOGO_Y + (long) (ga_sin8 (ph + g_tick * 2) >> 4);
				long x = l * MINI * 6 + c * MINI + LOGO_X;
				const gf_sprite_t *s = g_sm[logo_col[l]];
				ga_shadow (x + 2, y + 4, s);
				ga_ablit (x, y, s);
			}
	tx_c (TX_SUB, GF_W / 2, 226);
	if (ga_sin8 ((g_tick >> 1) << 2) >= 40)	/* l'invite clignote doucement */
		tx_c (TX_PROMPT, GF_W / 2, 284);
	tx_c (TX_RECORD, GF_W / 2 - 40, 332);
	ga_num (g_best, GF_W / 2 + 8, 330, g_dbig, 4);
}

static void draw (void)
{
	if (g_ldirty)
		build_layer ();
	gf_copy (gf_frame, gf_layer, GF_PITCH * GF_H);
	ga_orbs (g_sorb[0], g_sorb[1], g_tick);		/* deux lumieres derivent derriere */
	for (int i = 0; i < NM; i++)
		gf_bglow (g_mote[i].x >> 16, g_mote[i].y >> 16, g_smote);
	if (g_state == ST_TITLE)
	{
		draw_title ();
		ga_parts_draw ();
		return;
	}
	draw_field ();
	if (g_state == ST_PLAY)
	{
		long d = 0;				/* le fantome : ou elle tomberait */
		while (!blocked (0, d + 1))
			d++;
		if (d)
			draw_piece (g_type, g_rot, g_px * CELL + WX, (g_py + d) * CELL + WY, CELL, g_sg, WY);
		draw_piece (g_type, g_rot, g_px * CELL + WX, g_py * CELL + WY, CELL, g_sb, WY);
	}
	if (g_hold >= 0)
		draw_mini (g_hold, 105, 80);
	for (int i = 0; i < 3; i++)			/* les pieces a venir */
		draw_mini (g_next[i], 535, i * 44 + 84);
	ga_num (g_show, 182, 158, g_dbig, 6);
	ga_num ((unsigned long) g_lines, 90, 228, g_dbig, 6);
	ga_num ((unsigned long) g_level + 1, 182, 228, g_dbig, 6);
	ga_num (g_best, 612, 222, g_dsmall, 6);
	if (g_btn)					/* le bouton tenu s'allume */
		for (const struct ga_zone_t *z = zones; z->id; z++)
			if (z->id == g_btn)
			{
				if (z->id != AC_HOLD)
					ga_zone_panel (z, 10, 0x0080B0F0, 70, 0);
				break;
			}
	ga_parts_draw ();
	if (g_pop)					/* le texte qui monte */
		ga_ablit (WX + WW / 2 - (long) (g_popspr[0] >> 1), g_pop + 120, g_popspr);
	if (g_state == ST_OVER)				/* (la pause : gamepause) */
	{
		well_box (TX_OVER, TX_AGAIN);
		ga_num (g_score, WX + WW / 2, 218, g_dbig, 5);
		if (g_newbest)
			tx_c (TX_BEST, WX + WW / 2, 300);
	}
}

int main (void)
{
	if (gf_open ("Tetris") < 0)
		return 0;
	ui_menu ((long) s_menus);
	if (!ga_init (1024 * 1024))
		return 0;
	gs_open ();
	if (gs_fm)					/* la musique : son propre synthé, trois voix */
	{
		mu_fm = ak_fm_new (3);
		if (mu_fm > 0 && ak_fm_live (mu_fm, 0) < 0)
		{
			ak_fm_free (mu_fm);
			mu_fm = 0;
		}
		if (mu_fm < 0)
			mu_fm = 0;
	}
	g_best = gf_load_best (P_BEST);
	gen_sprites ();
	gen_motes ();
	gen_rain ();
	gp_init (game_pause, game_resume, new_game, -1, 0, on_pad);
	set_state (ST_TITLE);
	g_ldirty = 1;
	gf_run (on_event, step, draw);
	keep_best ();
	mu_stop ();
	if (mu_fm)
		ak_fm_free (mu_fm);
	gs_close ();
	return 0;
}
