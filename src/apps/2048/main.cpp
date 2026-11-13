/*
 * 2048 -- le jeu d'Onyx (user/Apps/2048/main.cpp) : on fait glisser les tuiles, deux tuiles egales fusionnent.
 * Fleches ou balayage (ecran tactile, souris) ; R ou Partie > Nouvelle partie recommence. La fenetre est
 * redimensionnable, le plateau suit (le plus grand possible, centre).
 *
 * Port en C++ depuis le source d'Onyx : le moteur (add_tile, restart, slide, move, la fin de partie, le
 * generateur pseudo-aleatoire, les couleurs des tuiles) vient d'Onyx ; la presentation vient de 2048.asm de
 * Korin (fenetre UIKit redimensionnable, menu Partie, puits du score, plateau et tuiles a la taille de la
 * fenetre, balayage, appui dans le bandeau qui deplace la fenetre, message de fin) : memes calculs, donc les
 * memes pixels. Appels directs a UIKit (ui_*), un seul source pour le Pi et le PC ; l'asm reste dans le
 * dossier jusqu'a la validation sur le Pi.
 */
#include <armpi/syscall.h>
#include <uikit.h>

#define N		4
#define WIN_W		400
#define WIN_H		452
#define HEAD		64			/* bandeau du score */
#define PAD		12
#define GAP		8
#define SWIPE		24			/* balayage : au moins ce trajet */
#define ID_NEW		1
#define ID_QUIT		2
#define KEY_R_		0x15			/* (codes HID) */
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50
#define KEY_DOWN_	0x51
#define KEY_UP_		0x52
#define SYS_TIME_MS_	0x19

static struct ui_vars ui;
static const unsigned *g_tok;			/* les jetons du theme (ui_tokens) */
static long g_win_w = WIN_W, g_win_h = WIN_H;	/* taille de la fenetre */
static long g_cell, g_side, g_ox;		/* tuile, plateau, son bord gauche */
static long g_press_x, g_press_y, g_press;	/* appui : x, y, en cours */

/* ---- le moteur d'Onyx ---- */
static int g_grid[N][N];
static long g_score, g_over;
static unsigned g_rng;

static const unsigned tile_rgb[11] = { 0x00EEE4DA, 0x00EDE0C8, 0x00F2B179, 0x00F59563, 0x00F67C5F, 0x00F65E3B,
	0x00EDCF72, 0x00EDCC61, 0x00EDC850, 0x00EDC53F, 0x00EDC22E };

static const char s_title[] = "2048";
static const char s_score[] = "Score";
static const char s_over[] = "Partie termin\xC3\xA9" "e";
static const char s_again[] = "Toucher ou R : nouvelle partie";
static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tR\n-\nI2\tQuitter\n";

static unsigned tok (int off) { return g_tok[off / 4]; }
static const char *tr (const char *s) { return (const char *) ui_tr ((long) s); }

/* le generateur d'Onyx (15 bits) */
static unsigned rnd (void) { g_rng = g_rng * 1103515245u + 12345u; return g_rng >> 16; }

/* une tuile 2 (9 fois sur 10) ou 4 dans une case vide au hasard */
static void add_tile (void)
{
	int empty[N * N], ne = 0;
	for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) if (!g_grid[r][c]) empty[ne++] = r * N + c;
	if (ne == 0) return;
	int e = empty[rnd () % (unsigned) ne];
	g_grid[e / N][e % N] = (rnd () % 10 == 0) ? 4 : 2;
}

static void restart (void)
{
	for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) g_grid[r][c] = 0;
	g_score = 0; g_over = 0;
	add_tile (); add_tile ();
}

/* Fait glisser une ligne de 4 cases vers sa premiere (avec fusion) -> 1 si elle a change. */
static int slide (int *v)
{
	int tmp[N], n = 0, changed = 0;
	for (int i = 0; i < N; i++) if (v[i]) tmp[n++] = v[i];
	for (int i = n; i < N; i++) tmp[i] = 0;
	for (int i = 0; i < N - 1; i++)
		if (tmp[i] && tmp[i] == tmp[i + 1])
		{
			tmp[i] *= 2; g_score += tmp[i];
			for (int j = i + 1; j < N - 1; j++) tmp[j] = tmp[j + 1];
			tmp[N - 1] = 0;
		}
	for (int i = 0; i < N; i++) { if (v[i] != tmp[i]) changed = 1; v[i] = tmp[i]; }
	return changed;
}

/* plus aucun coup : ni case vide, ni voisines egales */
static void check_over (void)
{
	int movable = 0;
	for (int r = 0; r < N; r++) for (int c = 0; c < N; c++)
	{
		if (!g_grid[r][c]) movable = 1;
		if (c + 1 < N && g_grid[r][c] == g_grid[r][c + 1]) movable = 1;
		if (r + 1 < N && g_grid[r][c] == g_grid[r + 1][c]) movable = 1;
	}
	g_over = !movable;
}

/* dir : 0 gauche, 1 droite, 2 haut, 3 bas -> 1 si le plateau a change */
static int move (int dir)
{
	int changed = 0;
	for (int k = 0; k < N; k++)
	{
		int line[N];
		for (int i = 0; i < N; i++)
		{
			int r, c;
			if (dir == 0) { r = k; c = i; }
			else if (dir == 1) { r = k; c = N - 1 - i; }
			else if (dir == 2) { r = i; c = k; }
			else { r = N - 1 - i; c = k; }
			line[i] = g_grid[r][c];
		}
		if (slide (line)) changed = 1;
		for (int i = 0; i < N; i++)
		{
			int r, c;
			if (dir == 0) { r = k; c = i; }
			else if (dir == 1) { r = k; c = N - 1 - i; }
			else if (dir == 2) { r = i; c = k; }
			else { r = N - 1 - i; c = k; }
			g_grid[r][c] = line[i];
		}
	}
	return changed;
}

/* ---- le dessin (celui de 2048.asm) ---- */
/* valeur -> decimal dans b ; -> nombre de chiffres */
static int dec (unsigned long v, char *b)
{
	char t[24];
	int n = 0, k = 0;
	do
		t[n++] = (char) ('0' + v % 10);
	while ((v /= 10) && n < 22);
	while (n)
		b[k++] = t[--n];
	b[k] = 0;
	return k;
}

/* valeur d'une tuile -> fond, encre */
static unsigned tile_color (unsigned v, unsigned *ink)
{
	if (v > 2048)
		v = 2048;				/* (au-dela : la couleur de 2048) */
	unsigned i = (unsigned) __builtin_ctz (v) - 1;	/* log2 - 1 */
	*ink = i < 2 ? 0x00776E65 : 0x00F9F6F2;
	return tile_rgb[i];
}

/* le score, a droite du puits */
static void draw_score (void)
{
	char b[32];
	dec ((unsigned long) g_score, b);
	long w = ui_text_width ((long) b, 22, (long) ui.font_b);
	ui_text ((long) b, g_win_w - (FRAME_CAP_W + PAD * 2) - w, 40, tok (TJ_TEXT1), 22, (long) ui.font_b, 0);
}

/* valeur, coin de la tuile */
static void draw_tile (unsigned v, long x, long y)
{
	unsigned ink, bg = tile_color (v, &ink);
	ui_rbox (x, y, g_cell, g_cell, 6, bg, bg);
	char b[16];
	unsigned long n = (unsigned long) dec (v, b);	/* le corps diminue avec les chiffres */
	unsigned long k = 5, a = (unsigned long) g_cell;
	if (n >= 3)
	{
		a *= 3;
		k = n < 4 ? 10 : n < 5 ? 12 : 15;
	}
	else
		a <<= 1;
	long px = (long) (a / k);
	long w = ui_text_width ((long) b, px, (long) ui.font_b);
	long tx = ((g_cell - w) >> 1) + x;
	long base = ((g_cell + (long) ((unsigned long) px >> 1) - (long) ((unsigned long) px >> 3)) >> 1)
		+ (long) ((unsigned long) px >> 2) + y;	/* ligne de base */
	ui_text ((long) b, tx, base, ink, px, (long) ui.font_b, 0);
}

/* texte, corps, police, y : centre sur la fenetre */
static void text_c (const char *s, long px, long font, long y, unsigned col)
{
	s = tr (s);
	long w = ui_text_width ((long) s, px, font);
	ui_text ((long) s, (g_win_w - w) >> 1, y, col, px, font, 0);
}

/* le message de fin, sur le plateau */
static void draw_over (void)
{
	long bw = g_side < 240 ? g_side : 240;
	long x = g_ox + ((g_side - bw) >> 1);
	long y = (g_side - 100 + HEAD * 2) >> 1;
	ui_rbox (x, y, bw, 100, 12, tok (TJ_SURFACE), tok (TJ_SURFACE));
	text_c (s_over, 22, (long) ui.font_b, y + 44, tok (TJ_TEXT1));
	text_c (s_again, 15, (long) ui.font, y + 76, tok (TJ_TEXT2));
}

/* toute la fenetre */
static void paint (void)
{
	ui_fill (0, 0, g_win_w, g_win_h, tok (TJ_FACE));
	ui_rbox (PAD, 12, g_win_w - (PAD * 2 + FRAME_CAP_W), 40, 8, tok (TJ_SUNKEN), tok (TJ_SUNKEN));	/* le puits du score */
	ui_text ((long) tr (s_score), PAD + 12, 38, tok (TJ_TEXT2), 15, (long) ui.font, 0);
	draw_score ();
	ui_rbox (g_ox, HEAD, g_side, g_side, 10, 0x00BBADA0, 0x00BBADA0);	/* le plateau (ses couleurs a lui) */
	for (int i = 0; i < N * N; i++)
	{
		long x = (i & (N - 1)) * (g_cell + GAP) + g_ox + GAP;
		long y = (i >> 2) * (g_cell + GAP) + HEAD + GAP;
		int v = g_grid[i >> 2][i & (N - 1)];
		if (v)
			draw_tile ((unsigned) v, x, y);
		else
			ui_rbox (x, y, g_cell, g_cell, 6, 0x00CDC1B4, 0x00CDC1B4);
	}
	if (g_over)
		draw_over ();
	ui_update_all ();
}

/* 0 gauche, 1 droite, 2 haut, 3 bas : joue le coup et redessine */
static void play (int dir)
{
	if (g_over)
		return;
	if (!move (dir))
		return;
	add_tile ();
	check_over ();
	paint ();
}

/* fonction de ui_on_resize : le plateau, centre, le plus grand possible */
static void on_resize (long w, long h)
{
	g_win_w = w;
	g_win_h = h;
	long side = w - PAD * 2, hh = h - (HEAD + PAD);
	if (side > hh)
		side = hh;
	g_cell = (long) ((unsigned long) (side - GAP * (N + 1)) / N);	/* cote d'une tuile */
	g_side = g_cell * N + GAP * (N + 1);
	g_ox = (long) ((unsigned long) (w - g_side) >> 1);
	paint ();
}

/* la souris : un appui dans le bandeau deplace la fenetre ; un balayage joue */
static void on_mouse (void)
{
	long kind = ui_ev_field (&ui, EV_KIND), x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
	if (kind == 1)
	{
		if (!(ui_ev_field (&ui, EV_CODE) & 1))
			return;
		if (y >= HEAD)
		{
			g_press_x = x;
			g_press_y = y;
			g_press = 1;
		}
		else if (x < g_win_w - FRAME_CAP_W)
			ui_window_drag ();
		return;
	}
	if (kind != 2 || !g_press)
		return;
	g_press = 0;
	long dx = x - g_press_x, dy = y - g_press_y;	/* trajet */
	long ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	if (ax >= ay && ax >= SWIPE)
		play (dx > 0 ? 1 : 0);
	else if (ax < ay && ay >= SWIPE)
		play (dy > 0 ? 3 : 2);
	else if (g_over)
	{					/* partie finie : un toucher recommence */
		restart ();
		paint ();
	}
}

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) s_title, WF_RESIZABLE) < 0)
		return 0;
	ui_menu ((long) s_menus);
	g_tok = (const unsigned *) ui_tokens ();
	ui_min_size (300, 380);
	ui_on_resize ((long) on_resize, 0);		/* (le .abi lit deux arguments dans son commentaire) */
	g_rng = (unsigned) armpi_syscall (SYS_TIME_MS_, 0, 0, 0, 0, 0, 0) | 1u;
	restart ();
	on_resize (WIN_W, WIN_H);
	for (;;)
	{
		long t = ui_event (1000);
		if (!t)
			continue;
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MENU)
		{
			long id = (uint32_t) ui_ev_field (&ui, EV_CODE);
			if (id == ID_QUIT)
				break;
			if (id == ID_NEW)
			{
				restart ();
				paint ();
			}
		}
		else if (t == WEV_KEY)
		{
			if (!ui_ev_field (&ui, EV_KIND))
				continue;
			long code = (uint32_t) ui_ev_field (&ui, EV_CODE);
			if (code == KEY_R_)
			{
				restart ();
				paint ();
			}
			else if (code == KEY_LEFT_)
				play (0);
			else if (code == KEY_RIGHT_)
				play (1);
			else if (code == KEY_UP_)
				play (2);
			else if (code == KEY_DOWN_)
				play (3);
		}
		else if (t == WEV_MOUSE)
			on_mouse ();
		else
		{
			ui_w_event (0);			/* (WEV_RESIZE : ui_on_resize) */
			if (t == WEV_THEME)		/* un autre theme : tout dans ses couleurs */
				paint ();
		}
	}
	return 0;
}
