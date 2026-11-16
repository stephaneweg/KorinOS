/*
 * same -- le SameGame d'Onyx (user/Apps/same/main.cpp) : 18 x 13 billes de 4 couleurs ; un groupe de 2 billes ou
 * plus s'en va, les colonnes tombent, les colonnes vides se referment a gauche ; (n-1)^2 points. Memes regles,
 * habillees : billes eclairees dans des alveoles, groupe survole qui luit, chute et glissement animes, eclats,
 * son, record.
 *
 * Souris ou doigt ; fleches et Espace ; R : nouvelle partie ; manette : la croix deplace le curseur, A retire le
 * groupe. Plein ecran ; Start, Echap ou le bouton || : le menu de pause (gamepause.h). Taille logique 640 x 480.
 *
 * Port en C++ depuis le source d'Onyx : les regles (flood, collapse, moves_left, play, le score) viennent
 * d'Onyx ; la presentation vient de same.asm de Korin, par gamefb.h, gamedraw.h, gamepause.h, gamesound.h (memes
 * calculs entiers, donc les memes pixels), avec son generateur (celui de gamefb, pas le congruentiel d'Onyx), son
 * record HOME:/same/best.txt, ses sons, son curseur et ses menus. Un seul source pour le Pi et le PC ; l'asm
 * reste dans le dossier jusqu'a la validation sur le Pi.
 */
#define GF_W		640
#define GF_H		480
#include "../../inc/gamepause.h"

#define GW		18
#define GH		13
#define NCOL		4
#define NCELLS		(GW * GH)
#define CELL		33
#define BALL		31
#define OX		23
#define OY		42
#define HUD_H		36

enum { ST_TITLE, ST_PLAY, ST_OVER };
enum { ID_NEW = 1, ID_SOUND = 3, ID_QUIT = 4 };
enum { TX_LOGO, TX_SUB, TX_PROMPT, TX_RECORD, TX_NOMOVE, TX_EMPTY, TX_AGAIN, TX_NEWBEST, NTX };

#define KEY_ENTER_	0x28
#define KEY_SPACE_	0x2C
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50
#define KEY_DOWN_	0x51
#define KEY_UP_		0x52

#define P_DIR		"HOME:/same"
#define P_BEST		"HOME:/same/best.txt"

/* ---- l'etat ---- */
static long g_state, g_timer;
static unsigned long g_tick;
static unsigned long g_score, g_show, g_best;
static long g_newbest;
static long g_left;				/* billes restantes */
static long g_busy;				/* des billes bougent */
static long g_vy;
static long g_hc = -1, g_hr;			/* case visee (-1 : aucune) */
static long g_hn;				/* taille du groupe vise (0 : pas de groupe) */
static long g_cursor;				/* 1 : le curseur clavier est montre */
static long g_flt, g_fltv, g_fltx, g_flty;	/* le gain qui monte : reste, valeur, x, y */
static long g_ldirty;
static gf_sprite_t *g_sb[NCOL], *g_sh[NCOL];	/* billes, lueurs */
static gf_sprite_t *g_scur, *g_sorb[2];
static gf_sprite_t *g_dbig[10], *g_dsmall[10], *g_dgain[10];
static gf_sprite_t *g_tx[NTX];
static unsigned char g_grid[NCELLS];		/* 0 vide, 1..NCOL */
static unsigned char g_hover[NCELLS];		/* le groupe vise */
static unsigned char g_mark[NCELLS];
static unsigned short g_oy[NCELLS];		/* retard de chaque bille, en points */
static unsigned short g_ox[NCELLS];
static unsigned short g_stk[NCELLS];

/* ---- tables ---- */
static const uint32_t colors[NCOL] = { 0x00E85050, 0x0048C068, 0x004888F0, 0x00F0C840 };
static const struct ga_txe tx_table[NTX] = {
	{ "Same", 78, 1, 0x00FFF0C0, 0x00F07858 },
	{ "Des groupes de m\xC3\xAAme couleur, et plus rien", 16, 0, 0x00E0D8F0, 0x00B0A8C8 },
	{ "Espace, A ou toucher pour jouer", 18, 1, 0x00FFFFFF, 0x00D8D0F0 },
	{ "Record", 15, 1, 0x00C8BCD8, 0x00A098B8 },
	{ "Plus de coup possible", 26, 1, 0x00FFD878, 0x00F09040 },
	{ "Plateau vide !", 26, 1, 0x00C8FFD0, 0x0060D890 },
	{ "Espace, A ou toucher : nouvelle partie", 14, 0, 0x00E0D8F0, 0x00B0A8C8 },
	{ "Nouveau record !", 16, 1, 0x00FFE080, 0x00F0B040 },
};
static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI3c\tSon\n-\nI4\tQuitter\n";

/* ---------------------------------------------------------------------------------------------------------------
 *  Sprites et couche
 * --------------------------------------------------------------------------------------------------------------- */
static void gen_sprites (void)
{
	for (int i = 0; i < NCOL; i++)
	{
		g_sb[i] = ga_ball (BALL, colors[i]);
		g_sh[i] = ga_disc (CELL + 10, colors[i]);
	}
	g_sorb[0] = ga_disc (140, 0x00281838);
	g_sorb[1] = ga_disc (180, 0x00102838);
	g_scur = ga_new (CELL, CELL);			/* le curseur du clavier : un cadre de 3 points */
	for (int y = 0; y < CELL; y++)
		for (int x = 0; x < CELL; x++)
			if (x < 3 || x >= CELL - 3 || y < 3 || y >= CELL - 3)
				g_scur[2 + y * CELL + x] = 0xFFFFFFFF;
	ui_canvas_begin (gf_cv);
	ga_digits (g_dbig, 20, (long) ui.font_b, 0x00FFFFFF, 0x00E8C8FF);
	ga_digits (g_dsmall, 14, (long) ui.font_b, 0x00E0E4F0, 0x00B0B8D0);
	ga_digits (g_dgain, 26, (long) ui.font_b, 0x00FFF8C0, 0x00FFA830);
	ga_texts (tx_table, g_tx, NTX);
	ui_canvas_end ();
}

static void build_layer (void)
{
	g_ldirty = 0;
	ga_sky (0x00241438, 0x000C0A1C, 0x005C2C70, 70);
	gf_dst = gf_layer;
	if (g_state != ST_TITLE)
	{						/* le plateau et ses alveoles */
		ga_panel (OX - 7, OY - 6, GW * CELL + 14, GH * CELL + 11, 12, 0x00584868, 0x00281C34, 256, 0x00B098C8);
		ga_panel (OX - 3, OY - 2, GW * CELL + 6, GH * CELL + 3, 8, 0x00140E20, 0x001C1428, 256, 0);
		for (long r = 0; r < GH; r++)
			for (long c = 0; c < GW; c++)
				ga_panel (c * CELL + OX + 2, r * CELL + OY + 2, CELL - 4, CELL - 4, (CELL - 4) / 2, 0x000A0612,
					  0x00221A30, 256, 0);
		ga_mark (OX - 7, OY - 6, GW * CELL + 14, GH * CELL + 11);
		ui_canvas_begin (gf_cv);
		gf_text ("Score", OX, 24, 0x00C8BCD8, 13, (long) ui.font_b);
		gf_text ("Reste", 212, 24, 0x00C8BCD8, 13, (long) ui.font_b);
		gf_text ("Record", 362, 24, 0x00C8BCD8, 13, (long) ui.font_b);
		ui_canvas_end ();
	}
	gf_dst = gf_frame;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Les regles d'Onyx
 * --------------------------------------------------------------------------------------------------------------- */
/* restart d'Onyx : un plateau au hasard ; les billes entrent par le haut, un peu decalees */
static void new_game (void)
{
	for (int i = 0; i < NCELLS; i++)
		g_grid[i] = (unsigned char) (gf_below (NCOL) + 1);
	for (int i = 0; i < NCELLS; i++)
		g_ox[i] = 0;
	for (int i = 0; i < NCELLS; i++)
		g_oy[i] = (unsigned short) (gf_below (60) + (GH + 1) * CELL);
	g_busy = 1;
	g_vy = 4;
	g_score = g_show = 0;
	g_newbest = 0;
	g_hn = 0;
	g_flt = 0;
	g_left = NCELLS;
	g_ldirty = 1;
	ga_parts_clear ();
	g_state = ST_PLAY;
	g_timer = 0;
	g_hc = -1;
}

/* colonne, ligne, marques -> taille du groupe de meme couleur. Une case est marquee quand elle est empilee : elle
 * l'est une seule fois (pile bornee par la taille du plateau). */
static long flood (long c0, long r0, unsigned char *mark)
{
	long start = r0 * GW + c0;
	unsigned col = g_grid[start];
	for (int i = 0; i < NCELLS; i++)
		mark[i] = 0;
	if (!col)
		return 0;
	long sp = 0, n = 0;
	mark[start] = 1;
	g_stk[sp++] = (unsigned short) start;
	while (sp)
	{
		long cell = g_stk[--sp];
		n++;
		long r = cell / GW, c = cell % GW;
		long nb[4];
		int k = 0;
		if (c)
			nb[k++] = cell - 1;
		if (c + 1 < GW)
			nb[k++] = cell + 1;
		if (r)
			nb[k++] = cell - GW;
		if (r + 1 < GH)
			nb[k++] = cell + GW;
		for (int i = 0; i < k; i++)
		{
			long q = nb[i];
			if (mark[q] || g_grid[q] != col)
				continue;
			mark[q] = 1;
			g_stk[sp++] = (unsigned short) q;
		}
	}
	return n;
}

/* le groupe sous la case visee */
static void hover_update (void)
{
	g_hn = 0;
	if (g_hc < 0 || g_busy || g_state != ST_PLAY)
		return;
	long n = flood (g_hc, g_hr, g_hover);
	g_hn = n < 2 ? 0 : n;
}

/* la bille (c, r) est visee (plus rien ne bouge ? le groupe est recalcule) */
static void hover_at (long c, long r)
{
	g_hc = c;
	g_hr = r;
	hover_update ();
}

/* les colonnes tombent, les vides se referment a gauche ; chaque bille garde son retard (animation) */
static void collapse (void)
{
	for (long c = 0; c < GW; c++)
	{
		long w = GH - 1;
		for (long r = GH - 1; r >= 0; r--)
		{
			long i = r * GW + c;
			if (!g_grid[i])
				continue;
			g_grid[w * GW + c] = g_grid[i];
			g_oy[w * GW + c] = (unsigned short) ((w - r) * CELL);
			w--;
		}
		for (; w >= 0; w--)
		{
			g_grid[w * GW + c] = 0;
			g_oy[w * GW + c] = 0;
		}
	}
	long w = 0;
	for (long c = 0; c < GW; c++)
	{
		if (!g_grid[(GH - 1) * GW + c])		/* colonne vide (apres la chute) */
			continue;
		if (c != w)
		{
			unsigned short d = (unsigned short) ((c - w) * CELL);
			for (long r = 0; r < GH; r++)
			{
				g_grid[r * GW + w] = g_grid[r * GW + c];
				g_oy[r * GW + w] = g_oy[r * GW + c];
				g_ox[r * GW + w] = d;
			}
		}
		w++;
	}
	for (; w < GW; w++)
		for (long r = 0; r < GH; r++)
		{
			g_grid[r * GW + w] = 0;
			g_oy[r * GW + w] = 0;
			g_ox[r * GW + w] = 0;
		}
	g_busy = 1;
	g_vy = 4;
}

/* -> 1 si un coup reste */
static int moves_left (void)
{
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
		{
			unsigned v = g_grid[r * GW + c];
			if (!v) continue;
			if (c + 1 < GW && g_grid[r * GW + c + 1] == v) return 1;
			if (r + 1 < GH && g_grid[(r + 1) * GW + c] == v) return 1;
		}
	return 0;
}

/* colonne, ligne : un coup */
static void play (long c, long r)
{
	if (g_busy)
		return;
	long n = flood (c, r, g_mark);
	if (n < 2)
	{
		if (n)					/* pas de groupe */
			gs_note (140, 60, AKW_SQUARE, 40);
		return;
	}
	long pts = (n - 1) * (n - 1);			/* les grands groupes rapportent bien plus */
	g_score += (unsigned long) pts;
	g_left -= n;
	g_flt = 70;					/* le gain, la ou l'on a touche */
	g_fltv = pts;
	g_fltx = c * CELL + OX + CELL / 2;
	g_flty = r * CELL + OY;
	for (long i = 0; i < NCELLS; i++)		/* les billes partent en eclats */
	{
		if (!g_mark[i])
			continue;
		unsigned v = g_grid[i];
		g_grid[i] = 0;
		ga_burst ((i % GW) * CELL + OX + CELL / 2, (i / GW) * CELL + OY + CELL / 2, colors[v - 1], 8, 150000, 20, 3,
			  7000);
	}
	gs_note (((n > 14 ? 14 : n) << 5) + 300, 90, AKW_TRIANGLE, 110);	/* le son monte avec le groupe */
	gs_note (120, 50, AKW_NOISE, 40);
	collapse ();
	g_hn = 0;
}

/* plus rien ne bouge : fin de partie, ou le groupe vise */
static void settled (void)
{
	g_busy = 0;
	if (g_state != ST_PLAY)
		return;
	if (moves_left ())
	{
		hover_update ();
		return;
	}
	g_state = ST_OVER;
	g_timer = 0;
	if (g_score > g_best)
	{
		g_best = g_score;
		g_newbest = 1;
		gf_save_best (P_DIR, P_BEST, g_best);
	}
	gs_seq (g_left ? gs_tab_lose : gs_tab_win);
}

static void step (void)
{
	g_tick++;
	gs_tick ();
	ga_parts_step ();
	g_timer++;
	if (g_score > g_show)
		g_show += ((g_score - g_show) >> 3) + 1;
	if (g_flt)
		g_flt--;
	if (!g_busy)
		return;
	long v = g_vy;					/* d'abord tout tombe */
	if (v + 1 <= 22)
		g_vy = v + 1;
	int moving = 0;
	for (int i = 0; i < NCELLS; i++)
		if (g_oy[i])
		{
			moving = 1;
			g_oy[i] = (unsigned short) (g_oy[i] < v ? 0 : g_oy[i] - v);
		}
	if (moving)
		return;
	for (int i = 0; i < NCELLS; i++)		/* puis les colonnes se referment */
		if (g_ox[i])
		{
			moving = 1;
			g_ox[i] = (unsigned short) (g_ox[i] < 12 ? 0 : g_ox[i] - 12);
		}
	if (!moving)
		settled ();
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
	else if (id == ID_SOUND)
		ui_menu_state (ID_SOUND, gs_toggle () ? UMS_CHECKED : 0);
}

/* Entree, clic hors de la partie -> 1 si cela a servi */
static long start_act (void)
{
	if (g_state == ST_PLAY)
		return 0;
	if (g_state != ST_OVER || g_timer >= 25)	/* (pas d'un coup de doigt egare) */
		new_game ();
	return 1;
}

/* decalage du curseur (pas encore montre : au milieu) */
static void cursor_move (long dx, long dy)
{
	long c = GW / 2, r = GH / 2;
	if (g_hc >= 0)
	{
		c = g_hc + dx;
		r = g_hr + dy;
		if (c < 0) c = 0;
		if (c > GW - 1) c = GW - 1;
		if (r < 0) r = 0;
		if (r > GH - 1) r = GH - 1;
	}
	g_cursor = 1;
	hover_at (c, r);
}

static void cursor_play (void)
{
	if (g_hc >= 0)
		play (g_hc, g_hr);
}

/* (gamepause) pas de temps qui court : rien a arreter */
static long game_pause (void) { return 0; }
static void game_resume (void) {}

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
	if (((uint32_t) GF_EV (EV_CHAR) | 0x20) == 'r')
	{
		new_game ();
		return;
	}
	uint32_t code = (uint32_t) GF_EV (EV_CODE);
	if (code == KEY_SPACE_ || code == KEY_ENTER_)
	{
		if (!start_act ())
			cursor_play ();
	}
	else if (code == KEY_LEFT_)
		cursor_move (-1, 0);
	else if (code == KEY_RIGHT_)
		cursor_move (1, 0);
	else if (code == KEY_UP_)
		cursor_move (0, -1);
	else if (code == KEY_DOWN_)
		cursor_move (0, 1);
}

static void on_mouse (void)
{
	long mx, my;
	gf_mouse (&mx, &my);
	long x = mx - OX, y = my - OY, c = x / CELL, r = y / CELL;
	if (x < 0 || y < 0 || (unsigned long) c >= GW || (unsigned long) r >= GH)
	{
		c = -1;
		r = 0;
	}
	long kind = GF_EV (EV_KIND);
	if (kind == 1)
	{
		if (!(GF_EV (EV_CODE) & 1))
			return;
		if (gf_drag_top (HUD_H - 6))
			return;
		if (start_act () || c < 0)
			return;
		g_cursor = 0;
		hover_at (c, r);
		play (c, r);
	}
	else if (!kind && (g_hc != c || g_hr != r))	/* le survol */
	{
		g_cursor = 0;
		hover_at (c, r);
	}
}

/* la manette : A retire le groupe, la croix deplace le curseur (Start : gamepause) */
static void on_pad (void)
{
	if (!GF_EV (EV_KIND))
		return;
	unsigned long n = gp_new;
	if (n & PAD_A)
	{
		if (start_act ())
			return;
		cursor_play ();
	}
	if (n & PAD_LEFT)
		cursor_move (-1, 0);
	if (n & PAD_RIGHT)
		cursor_move (1, 0);
	if (n & PAD_UP)
		cursor_move (0, -1);
	if (n & PAD_DOWN)
		cursor_move (0, 1);
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
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Le dessin
 * --------------------------------------------------------------------------------------------------------------- */
static void draw_board (void)
{
	unsigned long pulse = ga_sin8 (g_tick << 3);	/* la lueur du groupe bat */
	for (long i = 0; i < NCELLS; i++)
	{
		unsigned v = g_grid[i];
		if (!v)
			continue;
		long x = (i % GW) * CELL + OX + 1 + g_ox[i];
		long y = (i / GW) * CELL + OY + 1 - g_oy[i];
		if (y < OY - 1)				/* (au-dessus du plateau : cachees) */
			continue;
		if (g_hn && g_hover[i] && pulse >= 100)
			gf_glow (x - 6, y - 6, g_sh[v - 1]);
		ga_ablit (x, y, g_sb[v - 1]);
	}
}

/* titre, suite, troisieme (-1 : aucun) : la boite de fin */
static void end_box (int title, int sub, int third)
{
	ga_panel (120, 150, 400, 180, 16, 0x00382848, 0x001C1428, 235, 0x00B8A0D0);
	ga_show (g_tx[title], GF_W / 2, 166);
	ga_num (g_score, GF_W / 2, 208, g_dgain, 5);
	if (third >= 0)
		ga_show (g_tx[third], GF_W / 2, 246);
	ga_show (g_tx[sub], GF_W / 2, 288);
}

static void draw_title (void)
{
	unsigned long t = g_tick;
	for (unsigned long i = 0; i < 17; i++)		/* une ronde de billes */
	{
		unsigned long a = ga_sin8 (i * 12 + t);
		ga_ablit ((long) i * 36 + 14, (long) (a >> 3) + 300, g_sb[i & (NCOL - 1)]);
		a = ga_sin8 ((i << 3) + t + 128);
		ga_ablit ((long) i * 36 + 14, (long) (a >> 3) + 80, g_sb[(i + 2) & (NCOL - 1)]);
	}
	ga_show (g_tx[TX_LOGO], GF_W / 2, 120);
	ga_show (g_tx[TX_SUB], GF_W / 2, 214);
	if (ga_sin8 (g_tick << 1) >= 40)
		ga_show (g_tx[TX_PROMPT], GF_W / 2, 256);
	ga_show (g_tx[TX_RECORD], GF_W / 2 - 40, 380);
	ga_num (g_best, GF_W / 2 + 8, 379, g_dbig, 4);
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
	ga_num (g_show, 70, 8, g_dbig, 4);
	ga_num ((unsigned long) g_left, 262, 8, g_dbig, 4);
	ga_num (g_best, 418, 8, g_dbig, 4);
	draw_board ();
	if (g_cursor && g_hc >= 0)			/* le curseur du clavier */
		ga_ablit (g_hc * CELL + OX, g_hr * CELL + OY, g_scur);
	ga_parts_draw ();
	if (g_flt)					/* le gain qui monte */
		ga_num ((unsigned long) g_fltv, g_fltx, g_flty - 70 + g_flt, g_dgain, 5);
	if (g_state == ST_OVER)
		end_box (g_left ? TX_NOMOVE : TX_EMPTY, TX_AGAIN, g_newbest ? TX_NEWBEST : -1);
}

int main (void)
{
	if (gf_open ("Same") < 0)
		return 0;
	ui_menu ((long) s_menus);
	if (!ga_init (1024 * 1024))
		return 0;
	gs_open ();
	g_best = gf_load_best (P_BEST);
	gen_sprites ();
	gp_init (game_pause, game_resume, new_game, GF_W - 32, 4, on_pad);
	g_ldirty = 1;
	g_hc = -1;
	gf_run (on_event, step, draw);
	gs_close ();
	return 0;
}
