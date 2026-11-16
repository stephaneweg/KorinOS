/*
 * snake -- le Snake d'Onyx (user/Apps/snake/main.cpp) : 24 x 18 cases, 4 anneaux au depart, 10 points par pomme,
 * une case toutes les 8 - score/60 images (3 au mieux), le mur ou le corps mettent fin a la partie. Memes regles,
 * habillees : serpent qui ondule d'une case a l'autre, herbe generee, pomme qui bat, eclats, son, record.
 *
 * Fleches ; un appui a cote de la tete, ou un glissement, tourne ; P : pause ; R : recommencer ; manette : croix,
 * A joue. Plein ecran ; Start, Echap ou le bouton || : le menu de pause (gamepause.h). Taille logique 640 x 480.
 *
 * Port en C++ depuis le source d'Onyx : les regles (restart, step, place_food, les virages sans demi-tour, la
 * vitesse) viennent d'Onyx ; la presentation vient de snake.asm de Korin, par gamefb.h, gamedraw.h, gamepause.h,
 * gamesound.h (memes calculs entiers, donc les memes pixels), avec son generateur (celui de gamefb, pas le
 * congruentiel d'Onyx), son record HOME:/snake/best.txt, ses sons et ses menus. Un seul source pour le Pi et
 * le PC ; l'asm reste dans le dossier jusqu'a la validation sur le Pi.
 */
#define GF_W		640
#define GF_H		480
#include "../../inc/gamepause.h"

#define GW		24
#define GH		18
#define NCELLS		(GW * GH)
#define CELL		24
#define OX		32
#define OY		38
#define SEG		23			/* diametre du corps */
#define NSZ		3			/* tailles du corps (vers la queue) */
#define SWIPE		18			/* glissement qui compte */

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_DYING, ST_OVER };
enum { ID_NEW = 1, ID_PAUSE, ID_SOUND, ID_QUIT };
enum { TX_LOGO, TX_SUB, TX_PROMPT, TX_RECORD, TX_PAUSE, TX_RESUME, TX_OVER, TX_AGAIN, TX_NEWBEST, NTX };

#define KEY_ENTER_	0x28
#define KEY_SPACE_	0x2C
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50
#define KEY_DOWN_	0x51
#define KEY_UP_		0x52

#define P_DIR		"HOME:/snake"
#define P_BEST		"HOME:/snake/best.txt"

/* ---- l'etat ---- */
static long g_state, g_timer;
static unsigned long g_tick;
static long g_len;
static long g_dx, g_dy, g_ndx, g_ndy;		/* direction, et la suivante (des touches) */
static long g_fx, g_fy;				/* la pomme */
static long g_tailx, g_taily;			/* d'ou vient le dernier anneau */
static unsigned long g_score, g_show, g_best;
static long g_newbest;
static long g_frames, g_last, g_speed;
static long g_dead;				/* anneaux gris (depuis la tete) */
static long g_shake;
static long g_gulp;				/* la bouchee descend le long du corps : anneau + 1 */
static long g_flt, g_fltx, g_flty;
static long g_ax, g_ay, g_down;			/* ancre d'un glissement */
static long g_ldirty;
static gf_sprite_t *g_sbody[NSZ * 3];		/* tailles x clair, sombre, gris */
static gf_sprite_t *g_shead[2], *g_seye, *g_spupil, *g_sapple, *g_sleaf, *g_shalo, *g_sshad, *g_sorb[2], *g_sbulge;
static gf_sprite_t *g_dbig[10], *g_dgain[10];
static gf_sprite_t *g_tx[NTX];
static unsigned char g_sx[NCELLS + 1], g_sy[NCELLS + 1];	/* le corps ; [0] = la tete */
static int32_t g_ptx[NCELLS + 2], g_pty[NCELLS + 2];		/* centres a l'ecran, l'anneau i */

/* ---- tables ---- */
static const uint32_t body_col[3] = { 0x0040B848, 0x002C9038, 0x00686C68 };
/* le muret : x, y, l, h */
static const unsigned short walls[4][4] = {
	{ OX - 9, OY - 9, GW * CELL + 18, 9 }, { OX - 9, OY + GH * CELL, GW * CELL + 18, 9 },
	{ OX - 9, OY - 9, 9, GH * CELL + 18 }, { OX + GW * CELL, OY - 9, 9, GH * CELL + 18 } };
static const struct ga_txe tx_table[NTX] = {
	{ "Snake", 72, 1, 0x00D8FFC0, 0x0048C050 },
	{ "Une pomme, un anneau de plus", 15, 0, 0x00D8E8D0, 0x00A8C0A0 },
	{ "Espace, A ou toucher pour jouer", 18, 1, 0x00FFFFFF, 0x00D8F0D0 },
	{ "Record", 15, 1, 0x00C8D8C0, 0x00A0B898 },
	{ "Pause", 26, 1, 0x00FFFFFF, 0x00B8F0A8 },
	{ "P ou toucher pour reprendre", 13, 0, 0x00D8E8D0, 0x00A8C0A0 },
	{ "Partie termin\xC3\xA9" "e", 26, 1, 0x00FFD878, 0x00F09040 },
	{ "Espace, A ou toucher : rejouer", 13, 0, 0x00D8E8D0, 0x00A8C0A0 },
	{ "Nouveau record !", 15, 1, 0x00FFE080, 0x00F0B040 },
};
static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tPause\tP\nI3c\tSon\n-\nI4\tQuitter\n";

/* ---------------------------------------------------------------------------------------------------------------
 *  Sprites et couche
 * --------------------------------------------------------------------------------------------------------------- */
static void gen_sprites (void)
{
	for (int t = 0; t < 3; t++)			/* teinte */
		for (int s = 0; s < NSZ; s++)		/* taille */
			g_sbody[t * NSZ + s] = ga_ball (SEG - 3 * s, body_col[t]);
	g_shead[0] = ga_ball (SEG + 3, 0x0060D860);
	g_shead[1] = ga_ball (SEG + 3, 0x00787C78);
	g_sbulge = ga_ball (SEG + 5, 0x0078E068);
	g_seye = ga_ball (9, 0x00F0F4E8);
	g_spupil = ga_ball (5, 0x00181810);
	g_sapple = ga_ball (CELL - 3, 0x00E83830);
	g_sleaf = ga_block (11, 6, 3, 0x0048B840);	/* la feuille */
	g_sshad = ga_ball (SEG, 0x00404040);
	ga_fade (g_sshad, 120);
	g_shalo = ga_disc (44, 0x00502018);
	g_sorb[0] = ga_disc (170, 0x000C1C0C);
	g_sorb[1] = ga_disc (130, 0x00081C10);
	ui_canvas_begin (gf_cv);
	ga_digits (g_dbig, 19, (long) ui.font_b, 0x00FFFFFF, 0x00C8F0B8);
	ga_digits (g_dgain, 18, (long) ui.font_b, 0x00FFF4B0, 0x00FFB040);
	ga_texts (tx_table, g_tx, NTX);
	ui_canvas_end ();
}

/* l'herbe, les cases, l'ombre des bords */
static void gen_grass (void)
{
	uint32_t *p = gf_layer;
	for (unsigned long y = 0; y < GF_H; y++)
		for (unsigned long x = 0; x < GF_W; x++)
		{
			long a = ga_noise2 (x, y, 5);	/* de grandes taches */
			unsigned long i = ((x * 7 + (y >> 2) * 31) ^ ((y + (x << 2)) << 3)) & 4095;
			long b = ((long) ga_nz[i] - 128) >> 2;	/* des brins : du bruit etire en hauteur */
			unsigned long cx = (x < OX ? 0 : x - OX) / CELL, cy = (y < OY ? 0 : y - OY) / CELL;
			uint32_t c = ((cx + cy) & 1) ? 0x00387C3C : 0x00306C34;	/* deux verts, en damier */
			c = ga_shade (c, (uint32_t) (((a - 128) >> 2) + b + 240));
			long dx = (long) x - GF_W / 2, dy = (long) y - GF_H / 2;	/* plus sombre vers les bords */
			unsigned long d = (unsigned long) (dx * dx + dy * dy) >> 11;
			unsigned long f = d > 280 ? 120 : 280 - d;
			if (f < 120)
				f = 120;
			*p++ = ga_shade (c, (uint32_t) f);
		}
}

static void build_layer (void)
{
	g_ldirty = 0;
	gen_grass ();
	gf_dst = gf_layer;
	ga_panel (0, 0, GF_W, OY - 9, 0, 0x001C2E1C, 0x00142014, 256, 0);	/* le bandeau du haut et le muret */
	for (int i = 0; i < 4; i++)
		ga_panel (walls[i][0], walls[i][1], walls[i][2], walls[i][3], 3, 0x00787058, 0x00484030, 256, 0x00A8A080);
	if (g_state == ST_TITLE)
		ga_panel (110, 92, 420, 230, 20, 0x00142814, 0x000C160C, 175, 0x0080A070);
	else
	{
		ui_canvas_begin (gf_cv);
		gf_text ("Score", OX - 4, 20, 0x00B8C8B0, 13, (long) ui.font_b);
		gf_text ("Longueur", 190, 20, 0x00B8C8B0, 13, (long) ui.font_b);
		gf_text ("Record", 350, 20, 0x00B8C8B0, 13, (long) ui.font_b);
		ui_canvas_end ();
	}
	gf_dst = gf_frame;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Les regles d'Onyx
 * --------------------------------------------------------------------------------------------------------------- */
static void set_state (long s)
{
	g_state = s;
	g_timer = 0;
	gf_still = s == ST_PAUSE;
	gf_redraw = 1;
}

static void place_food (void)
{
	for (;;)
	{
		long x = (long) gf_below (GW), y = (long) gf_below (GH);
		int on = 0;
		for (long i = 0; i < g_len; i++) if (g_sx[i] == x && g_sy[i] == y) { on = 1; break; }
		if (!on) { g_fx = x; g_fy = y; return; }
	}
}

/* restart d'Onyx */
static void new_game (void)
{
	g_len = 4;
	for (int i = 0; i < g_len; i++) { g_sx[i] = (unsigned char) (GW / 2 - i); g_sy[i] = GH / 2; }
	g_tailx = GW / 2 - 4;
	g_taily = GH / 2;
	g_dx = 1; g_dy = 0; g_ndx = 1; g_ndy = 0;
	g_score = g_show = 0;
	g_newbest = 0;
	g_frames = g_last = 0;
	g_dead = g_gulp = g_flt = 0;
	g_speed = 8;
	ga_parts_clear ();
	place_food ();
	set_state (ST_PLAY);
	g_ldirty = 1;
}

static void die (void)
{
	set_state (ST_DYING);
	g_shake = 6;
	if (g_score > g_best)
	{
		g_best = g_score;
		g_newbest = 1;
		gf_save_best (P_DIR, P_BEST, g_best);
	}
	gs_seq (gs_tab_lose);
}

/* la pomme est mangee */
static void eat (void)
{
	g_flt = 60;
	g_fltx = g_fx * CELL + OX + CELL / 2;
	g_flty = g_fy * CELL + OY + CELL / 2;
	ga_burst (g_fltx, g_flty, 0x00F05040, 26, 200000, 20, 3, 6000);
	ga_burst (g_fltx, g_flty, 0x0080E070, 10, 150000, 20, 2, 4000);
	g_gulp = 1;
	gs_later (0, 660, 50, AKW_TRIANGLE, 110);
	gs_later (50, 990, 70, AKW_TRIANGLE, 110);
}

/* une case d'avance (step d'Onyx) */
static void advance (void)
{
	g_dx = g_ndx; g_dy = g_ndy;
	long nx = g_sx[0] + g_dx, ny = g_sy[0] + g_dy;
	if ((unsigned long) nx >= GW || (unsigned long) ny >= GH) { die (); return; }
	int grow = (nx == g_fx && ny == g_fy);
	for (long i = 0; i < g_len; i++)
	{
		if (!grow && i == g_len - 1) continue;	/* (la queue s'en va) */
		if (g_sx[i] == nx && g_sy[i] == ny) { die (); return; }
	}
	g_tailx = g_sx[g_len - 1];			/* d'ou repart la queue */
	g_taily = g_sy[g_len - 1];
	if (grow && g_len < NCELLS) g_len++;
	for (long i = g_len - 1; i > 0; i--) { g_sx[i] = g_sx[i - 1]; g_sy[i] = g_sy[i - 1]; }
	g_sx[0] = (unsigned char) nx; g_sy[0] = (unsigned char) ny;
	if (grow) { g_score += 10; eat (); place_food (); }
	unsigned long k = g_score / 60;			/* une case toutes les 8 - score/60 images, 3 au mieux */
	g_speed = 8 - (long) (k > 5 ? 5 : k);
}

/* direction voulue : pas de demi-tour */
static void turn (long x, long y)
{
	if (x ? g_dx != 0 : g_dy != 0)
		return;
	g_ndx = x;
	g_ndy = y;
}

/* (gamepause) la partie s'arrete -> 1 si elle etait en cours */
static long game_pause (void)
{
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

static void step (void)
{
	g_tick++;
	gs_tick ();
	ga_parts_step ();
	g_timer++;
	if (g_score > g_show)
		g_show++;
	if (g_flt)
		g_flt--;
	if (g_shake)
		g_shake--;
	if (g_state == ST_PLAY)
	{
		if (++g_frames - g_last < g_speed)
			return;
		g_last = g_frames;
		if (g_gulp)				/* la bouchee avance d'un anneau */
		{
			g_gulp++;
			if (g_gulp >= g_len)
				g_gulp = 0;
		}
		advance ();
	}
	else if (g_state == ST_DYING && (g_tick & 1))
	{
		g_dead += 2;
		if (g_dead >= g_len)
			set_state (ST_OVER);
	}
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
		ui_menu_state (ID_SOUND, gs_toggle () ? UMS_CHECKED : 0);
}

/* -> 1 si l'appui a servi (titre, fin, pause) */
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
	if (!GF_EV (EV_KIND))
		return;
	long id = ui_menu_key (0);
	if (id >= 0)
	{
		do_menu (id);
		return;
	}
	uint32_t ch = (uint32_t) GF_EV (EV_CHAR) | 0x20;
	if (ch == 'p')
	{
		gp_open ();
		return;
	}
	if (ch == 'r')
	{
		if (g_state != ST_TITLE)
			new_game ();
		return;
	}
	uint32_t code = (uint32_t) GF_EV (EV_CODE);
	if (code == KEY_SPACE_ || code == KEY_ENTER_)
		start_act ();
	else if (code == KEY_LEFT_)
		turn (-1, 0);
	else if (code == KEY_RIGHT_)
		turn (1, 0);
	else if (code == KEY_UP_)
		turn (0, -1);
	else if (code == KEY_DOWN_)
		turn (0, 1);
}

/* un appui a cote de la tete : tourner de ce cote */
static void tap_turn (long x, long y)
{
	long hx = g_sx[0] * CELL + OX + CELL / 2, hy = g_sy[0] * CELL + OY + CELL / 2;
	if (g_dx)					/* on va de cote : monter ou descendre */
		turn (0, y >= hy ? 1 : -1);
	else
		turn (x >= hx ? 1 : -1, 0);
}

static void on_mouse (void)
{
	long mx, my;
	gf_mouse (&mx, &my);
	long kind = GF_EV (EV_KIND);
	if (kind == 1)
	{
		if (!(GF_EV (EV_CODE) & 1))
			return;
		if (gf_drag_top (OY - 8))
			return;
		if (start_act ())
			return;
		g_down = 1;
		g_ax = mx;
		g_ay = my;
		tap_turn (mx, my);
		return;
	}
	if (kind == 2)
	{
		g_down = 0;
		return;
	}
	if (!g_down)					/* un glissement */
		return;
	long dx = mx - g_ax, dy = my - g_ay;
	long ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	if ((ax > ay ? ax : ay) < SWIPE)
		return;
	g_ax = mx;
	g_ay = my;
	if (ax > ay)
		turn (dx < 0 ? -1 : 1, 0);
	else
		turn (0, dy < 0 ? -1 : 1);
}

/* la manette : A joue, la croix tourne (Start : gamepause) */
static void on_pad (void)
{
	if (!GF_EV (EV_KIND))
		return;
	unsigned long n = gp_new;
	if (n & PAD_A)
		start_act ();
	if (n & PAD_LEFT)
		turn (-1, 0);
	if (n & PAD_RIGHT)
		turn (1, 0);
	if (n & PAD_UP)
		turn (0, -1);
	if (n & PAD_DOWN)
		turn (0, 1);
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
		g_down = 0;				/* (la pause : gamepause) */
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Le dessin
 * --------------------------------------------------------------------------------------------------------------- */
/* sprite, centre */
static void centered (const gf_sprite_t *s, long cx, long cy)
{
	ga_ablit (cx - (long) (s[0] >> 1), cy - (long) (s[1] >> 1), s);
}

/* centre : l'ombre d'un anneau, decalee (comme l'asm, toujours celle du corps) */
static void centered_sh (long cx, long cy) { centered (g_sshad, cx + 3, cy + 4); }

/* anneau -> sprite (taille selon la distance a la queue, teinte en bandes) */
static const gf_sprite_t *seg_sprite (long i)
{
	long k = g_len - i;				/* 1 : la queue */
	long size = k > 3 ? 0 : k > 1 ? 1 : 2;
	long tint = (i >> 1) & 1;			/* bandes */
	if (g_state >= ST_DYING && i < g_dead)
		tint = 2;
	return g_sbody[tint * NSZ + size];
}

/* les centres de tous les anneaux, a l'image courante */
static void snake_points (void)
{
	long frac = g_speed;				/* fraction du pas */
	if (g_state == ST_PLAY || g_state == ST_PAUSE)
		frac = g_frames - g_last;
	for (long i = 0; i < g_len; i++)
	{
		long tx = g_sx[i] * CELL, ty = g_sy[i] * CELL, fx, fy;	/* ou il va, d'ou il vient */
		if (i + 1 < g_len)
		{
			fx = g_sx[i + 1] * CELL;
			fy = g_sy[i + 1] * CELL;
		}
		else
		{
			fx = g_tailx * CELL;
			fy = g_taily * CELL;
		}
		g_ptx[i] = (int32_t) ((tx - fx) * frac / g_speed + fx + OX + CELL / 2);
		g_pty[i] = (int32_t) ((ty - fy) * frac / g_speed + fy + OY + CELL / 2);
	}
}

static void draw_snake (void)
{
	snake_points ();
	long last = g_len - 1;
	for (long i = last; i >= 0; i--)		/* d'abord les ombres */
		centered_sh (g_ptx[i], g_pty[i]);
	for (long i = last; i > 0; i--)			/* de la queue vers la tete */
	{
		const gf_sprite_t *s = seg_sprite (i);
		centered (s, g_ptx[i], g_pty[i]);
		if (g_gulp == i)			/* la bouchee fait une bosse */
			centered (g_sbulge, g_ptx[i], g_pty[i]);
		centered (s, (g_ptx[i] + g_ptx[i - 1]) >> 1, (g_pty[i] + g_pty[i - 1]) >> 1);	/* un anneau entre deux */
	}
	long hx = g_ptx[0], hy = g_pty[0];		/* la tete */
	if ((g_tick & 63) < 12 && g_state < ST_DYING)	/* la langue */
		gf_rect (g_dx * 14 + hx - 1, g_dy * 14 + hy - 1, 3, 3, 0x00E83040);
	centered (g_shead[g_state >= ST_DYING ? 1 : 0], hx, hy);
	for (long s = -1; s <= 1; s += 2)		/* les yeux, de chaque cote du sens de marche */
	{
		long ex = g_dx * 5 + g_dy * s * 5 + hx, ey = g_dy * 5 + g_dx * s * 5 + hy;
		centered (g_seye, ex, ey);
		centered (g_spupil, g_dx * 2 + ex, g_dy * 2 + ey);
	}
}

static void draw_apple (void)
{
	long x = g_fx * CELL + OX + CELL / 2;
	long y = g_fy * CELL + OY + CELL / 2 - (long) (ga_sin8 (g_tick << 2) >> 5);	/* 0..7 : elle saute */
	long h = (long) (g_shalo[0] >> 1);
	gf_glow (x - h, y + 3 - h, g_shalo);
	centered_sh (x, y + 4);
	centered (g_sapple, x, y);
	gf_rect (x + 1, y - 14, 2, 5, 0x00503820);	/* la queue et la feuille */
	ga_ablit (x + 3, y - 15, g_sleaf);
}

static void draw_title (void)
{
	unsigned long t = g_tick;
	for (unsigned long i = 26; i; i--)		/* un serpent ondule en bas de l'ecran */
	{
		long y = (long) (ga_sin8 ((i << 4) + (t << 1)) >> 3) + 350;
		long x = (long) ((i * 20 + t) % (560 + 640)) - 280;
		centered (g_sbody[((i >> 1) & 1) * NSZ], x, y);
	}
	ga_show (g_tx[TX_LOGO], GF_W / 2, 112);
	ga_show (g_tx[TX_SUB], GF_W / 2, 206);
	if (ga_sin8 (g_tick << 1) >= 40)
		ga_show (g_tx[TX_PROMPT], GF_W / 2, 246);
	ga_show (g_tx[TX_RECORD], GF_W / 2 - 40, 284);
	ga_num (g_best, GF_W / 2 + 8, 282, g_dbig, 4);
}

/* titre, suite, 1 : montrer le score */
static void box (int title, int sub, int score)
{
	ga_panel (150, 160, 340, 170, 16, 0x00203820, 0x00101C10, 235, 0x0090B080);
	ga_show (g_tx[title], GF_W / 2, 176);
	if (score)
	{
		ga_num (g_score, GF_W / 2, 216, g_dbig, 5);
		if (g_newbest)
			ga_show (g_tx[TX_NEWBEST], GF_W / 2, 250);
	}
	ga_show (g_tx[sub], GF_W / 2, 290);
}

static void draw (void)
{
	if (g_ldirty)
		build_layer ();
	gf_copy (gf_frame, gf_layer, GF_PITCH * GF_H);
	ga_orbs (g_sorb[0], g_sorb[1], g_tick);
	if (g_state == ST_TITLE)
	{
		draw_title ();
		return;
	}
	ga_num (g_show, 74, 2, g_dbig, 4);
	ga_num ((unsigned long) g_len, 270, 2, g_dbig, 4);
	ga_num (g_best, 406, 2, g_dbig, 4);
	draw_apple ();
	draw_snake ();
	ga_parts_draw ();
	if (g_flt)
		ga_num (10, g_fltx, g_flty - 80 + g_flt, g_dgain, 5);
	if (g_state == ST_OVER)				/* (la pause : gamepause) */
		box (TX_OVER, TX_AGAIN, 1);
}

int main (void)
{
	if (gf_open ("Snake") < 0)
		return 0;
	ui_menu ((long) s_menus);
	if (!ga_init (1024 * 1024))
		return 0;
	gs_open ();
	g_best = gf_load_best (P_BEST);
	gen_sprites ();
	gp_init (game_pause, game_resume, new_game, GF_W - 32, 1, on_pad);
	set_state (ST_TITLE);
	g_ldirty = 1;
	gf_run (on_event, step, draw);
	gs_close ();
	return 0;
}
