/*
 * cards.h -- les cartes des jeux de patience de Korin (FreeCell, Solitaire), en C (se compile aussi en C++).
 *
 * Le modele vient de games/cards.h d'Onyx : une carte est 0..51, enseigne = c / 13 (0 trefle, 1 carreau,
 * 2 coeur, 3 pique), rang = c % 13 (0 as .. 12 roi) ; l'animation de fin (les cartes rebondissent en
 * laissant leurs traces) aussi. Le dessin et la table sont la traduction de src/inc/cards.inc, ce que Korin
 * a ajoute a Onyx : memes noms, memes calculs entiers, donc memes pixels que les jeux en assembleur.
 *
 * Dessin : les enseignes sont des formes anticrenelees (ui_vp_*) rendues une fois par taille dans des
 * masques ; chaque carte est composee dans une planche (canevas cache) au premier besoin, puis copiee avec
 * ses coins arrondis : poser 52 cartes ne coute que des copies. Les cartes suivent la taille de la fenetre
 * (cards_layout : la plus grande carte qui tient).
 *
 * Table : deux couches de la taille de la fenetre (canevas caches). T tient la table sans les cartes
 * glissees ; K compose T + cartes glissees ; ce qui change seulement va dans la surface de la fenetre, d'un
 * coup, sans passer par un etat ou la carte manque : le glisser ne scintille pas. En haut, un bandeau de
 * boutons et de texte (band_*), qui sert aussi a deplacer la fenetre.
 *
 * Le jeu inclut ce fichier une fois (tout est static) ; les variables de l'interface (ui) sont ici.
 */
#ifndef KORIN_CARDS_H
#define KORIN_CARDS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <armpi/syscall.h>
#include <uikit.h>

/* ---- le modele (games/cards.h d'Onyx) ---- */
static inline int card_suit (int c) { return c / 13; }
static inline int card_rank (int c) { return c % 13; }
static inline int card_red (int c) { int s = c / 13; return s == 1 || s == 2; }
static inline int card_make (int rank, int suit) { return suit * 13 + rank; }

/* ---- les tailles et les couleurs (cards.inc) ---- */
#define CARD_BACK	52			/* le dos, dans card_blit */
#define CARD_MIN_W	32
#define CARD_MAX_W	200			/* (grand as : 2/5, doit tenir dans CK_CELL) */
#define CK_CELL		80			/* case d'un masque (3 tailles x 4 enseignes) */
#define CK_CANW		(3 * CK_CELL)
#define CK_CANH		(4 * CK_CELL + 40)
#define CK_CORNER	32			/* pas de la table des coins */
#define CK_EDGE		0x00505860
#define CK_FACE		0x00FFFFFF
#define CK_RED		0x00C81828
#define CK_BLACK	0x00141820
#define CK_TINT_R	0x00F8E2DC
#define CK_TINT_B	0x00DEE4F4
#define CK_BACK		0x002050B0
#define CK_BACK_DOT	0x004078D8
#define CK_BACK_LINE	0x0090B8F0

#define BAND_H		36			/* bandeau du haut : boutons, etat, cadre */
#define BAND_BTN_H	26
#define BAND_MAX	4			/* boutons */
#define TAPIS_BAND	0x00124E26
#define TAPIS_HAUT	0x001F7A3A
#define TAPIS_BAS	0x0017622E
#define TAPIS_SLOT	0x0019692F
#define TAPIS_SLOT_L	0x003E9A5C
#define TAPIS_HINT	0x002E8A4C
#define TAPIS_BTN	0x001F7A3A
#define TAPIS_TEXT	0x00E4F4E8
#define TAPIS_NOTE	0x00FFE08A

#define DRAG_MIN	6			/* pixels avant qu'un appui devienne un glisser */
#define CK_TAP_MS	350			/* deuxieme appui d'un double-clic : ignore */

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;

/* ---- l'etat (cards_data de cards.inc) ---- */
static long c_mask, c_serif, c_sheet;
static uint32_t *c_maskpx, *c_sheet_px;
static long c_sheet_pitch, c_sheet_w, c_sheet_h;
static long c_w, c_h, c_r, c_idx, c_ih, c_dy_max, c_dy_min;	/* carte : taille, rayon, valeur, pas d'une colonne */
static long c_pip[3];						/* cotes des enseignes : coin, milieu, as */
static long l_gap, l_ox, l_top, l_tab;				/* la disposition : ecart, bord, rangees */
static long g_w, g_h;						/* la taille de la table */
static long t_cv, k_cv, t_pitch, t_cap_h;
static uint32_t *t_px, *k_px;
static uint32_t *cd_px;						/* ou vont les cartes (card_blit) */
static long cd_pitch, cd_w, cd_h, cd_lay;
static uint32_t *st_px;						/* ou vont les enseignes (ck_stamp) */
static long st_pitch;
static long dr_on, dr_n, dr_dy, dr_x, dr_y;			/* les cartes glissees */
static unsigned char dr_cards[56];
static long bb_n, bb_end, bb_x[BAND_MAX], bb_w[BAND_MAX];	/* les boutons du bandeau */
static const char *bb_s;
static unsigned char c_ready[56];				/* cartes deja dans la planche */
static unsigned char c_corner[CK_CORNER * CK_CORNER];
static uint32_t ck_seed = 1;

/* l'animation de fin (WinAnim d'Onyx) : fondations, cartes lancees, la carte en vol (x16) */
static long win_on;
static struct { long fx[4], fy, next, on, card, x, y, vx, vy; } wa;

static inline unsigned long ck_time_ms (void) { return (unsigned long) armpi_syscall (0x19, 0, 0, 0, 0, 0, 0); }

/* rd = pixel, rs = couleur, ra = 0..255 -> le melange (px_mix de cards.inc) */
static inline uint32_t px_mix (uint32_t rd, uint32_t rs, uint32_t ra)
{
	uint32_t a = ra + (ra >> 7), na = 256 - a;
	uint32_t rb = (((rd & 0x00FF00FF) * na + (rs & 0x00FF00FF) * a) >> 8) & 0x00FF00FF;
	uint32_t g = (((rd & 0x0000FF00) * na + (rs & 0x0000FF00) * a) >> 8) & 0x0000FF00;
	return g | rb;
}

/* -> pseudo-aleatoire sur 15 bits (le generateur de cards.inc : la graine donne la meme donne que l'asm) */
static inline unsigned long ck_rnd (void)
{
	ck_seed = ck_seed * 1103515245u + 12345u;
	return (ck_seed >> 16) & 0x7FFF;
}

/* -> 0 .. n - 1 */
static inline long ck_rnd_n (long n)
{
	return (long) (ck_rnd () % (unsigned long) n);
}

/* largeur, hauteur -> canevas cache */
static inline long ck_canvas (long w, long h)
{
	long c = ui_canvas (0, 0, w, h);
	if (c == 0 || ui_canvas_pixels (c) == 0)
		exit (1);			/* (plus de memoire) */
	ui_w_show (c, 0);
	return c;
}

/* taille voulue -> ce qui tient entre les barres de l'ecran (avant la fenetre : le PocketTerm n'a que
 * 800 x 600) */
static inline void cards_fit (long *w, long *h)
{
	long s[4] = { 0, 0, 0, 0 };
	ui_screen ((long) s);
	unsigned long bot = (unsigned long) s[2] < 64 ? 64 : (unsigned long) s[2];	/* (le panel flotte : hors des reserves) */
	unsigned long top = (unsigned long) s[3] < 44 ? 44 : (unsigned long) s[3];	/* (ou Elegant pose une nouvelle fenetre) */
	unsigned long sw = (unsigned long) s[0] - 16, sh = (unsigned long) s[1] - bot - top;
	if (sw > (unsigned long) *w)
		sw = (unsigned long) *w;
	if (sh > (unsigned long) *h)
		sh = (unsigned long) *h;
	*w = (long) sw;
	*h = (long) sh;
}

/* a appeler une fois la fenetre creee */
static inline void cards_init (void)
{
	c_mask = ck_canvas (CK_CANW, CK_CANH);
	c_maskpx = uikit_canvas_pixels (c_mask, 0, 0);
	c_serif = ui_font_get (UFN_SERIFBOLD);
	ck_seed = (uint32_t) ck_time_ms () | 1;
}

/* formes des enseignes (1 disque : x, y, r ; 2 polygone : n, points ; 0 fin), sur 256 x 256 ; trefle,
 * carreau, coeur, pique */
static const unsigned char suit_data[] = {
	1,128,70,60, 1,66,150,60, 1,190,150,60,
	2,4, 128,100, 180,150, 128,190, 76,150,
	2,5, 128,130, 114,212, 84,250, 172,250, 142,212,
	0,
	2,8, 128,4, 172,74, 236,128, 172,182, 128,252, 84,182, 20,128, 84,74,
	0,
	1,74,78,70, 1,182,78,70,
	2,8, 8,92, 22,140, 72,196, 128,250, 184,196, 234,140, 248,92, 128,60,
	0,
	1,72,156,64, 1,184,156,64,
	2,7, 128,4, 176,62, 232,112, 248,160, 8,160, 24,112, 80,62,
	2,5, 128,150, 114,214, 84,250, 172,250, 142,214,
	0,
};
static const unsigned char suit_ofs[4] = { 0, 35, 54, 81 };

/* valeurs (jeu francais : valet, dame, roi) */
static const char *const s_ranks[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "V", "D", "R" };

/* enseignes d'une carte de 2 a 10 : colonne 0..2, ligne 0..6 */
static const unsigned char pip_lay[9][20] = {
	{ 1,0, 1,6 },
	{ 1,0, 1,3, 1,6 },
	{ 0,0, 2,0, 0,6, 2,6 },
	{ 0,0, 2,0, 1,3, 0,6, 2,6 },
	{ 0,0, 2,0, 0,3, 2,3, 0,6, 2,6 },
	{ 0,0, 2,0, 1,1, 0,3, 2,3, 0,6, 2,6 },
	{ 0,0, 2,0, 1,1, 0,3, 2,3, 1,5, 0,6, 2,6 },
	{ 0,0, 2,0, 0,2, 2,2, 1,3, 0,4, 2,4, 0,6, 2,6 },
	{ 0,0, 2,0, 1,1, 0,2, 2,2, 0,4, 2,4, 1,5, 0,6, 2,6 },
};

/* enseigne, coin, cote : la forme, en blanc */
static inline void ck_suit (int suit, long x, long y, long side)
{
	const unsigned char *p = suit_data + suit_ofs[suit];
	long x16 = x << 4, y16 = y << 4;
	for (;;)
	{
		int op = *p++;
		if (op == 0)
			break;
		if (op == 1)
		{
			long cx = x16 + ((long) p[0] * side >> 4), cy = y16 + ((long) p[1] * side >> 4);
			long r = (long) p[2] * side >> 4;
			p += 3;
			ui_vp_disc (cx, cy, r, 0x00FFFFFF);
			continue;
		}
		int n = *p++;
		int32_t pt[16];
		for (int i = 0; i < n; i++, p += 2)
		{
			pt[2 * i] = (int32_t) (x16 + ((long) p[0] * side >> 4));
			pt[2 * i + 1] = (int32_t) (y16 + ((long) p[1] * side >> 4));
		}
		ui_vp_poly ((long) pt, n, 0x00FFFFFF);
	}
}

/* les masques de la taille courante : enseignes, coin */
static inline void ck_masks (void)
{
	ui_canvas_begin (c_mask);
	ui_fill (0, 0, CK_CANW, CK_CANH, 0);
	for (int t = 0; t < 3; t++)
		for (int s = 0; s < 4; s++)
			ck_suit (s, CK_CELL * t, CK_CELL * s, c_pip[t]);
	ui_vp_disc (c_r << 4, (4 * CK_CELL + c_r) << 4, c_r << 4, 0x00FFFFFF);	/* le coin : disque de rayon r */
	ui_canvas_end ();
	const uint32_t *m = c_maskpx + 4 * CK_CELL * CK_CANW;	/* ... recopie dans sa table */
	for (long r = 0; r < c_r; r++)
		for (long c = 0; c < c_r; c++)
			c_corner[r * CK_CORNER + c] = (unsigned char) (m[r * CK_CANW + c] & 0xFF);
}

/* largeur d'une carte : les tailles de tout ce qui en depend */
static inline void cards_size (long w)
{
	if (w == c_w)
		return;
	c_w = w;
	c_h = w * 11 >> 3;			/* (64 x 88 chez Onyx) */
	c_r = w / 10 < 3 ? 3 : w / 10;
	c_idx = w * 15 >> 6 < 11 ? 11 : w * 15 >> 6;	/* corps de la valeur */
	c_ih = c_idx + 3;			/* bande de la valeur */
	c_pip[0] = c_idx * 3 >> 2 < 7 ? 7 : c_idx * 3 >> 2;	/* enseigne de la valeur */
	c_pip[1] = w * 3 >> 4;			/* enseigne du milieu */
	c_pip[2] = (w << 1) / 5;		/* celle de l'as */
	c_dy_max = c_h >> 2;			/* pas d'une colonne */
	c_dy_min = c_h >> 3;
	long sw = 13 * w, sh = 5 * c_h;		/* la planche */
	if (sw > c_sheet_w || sh > c_sheet_h)
	{
		if (c_sheet)
			ui_w_destroy (c_sheet);
		c_sheet_w = sw;
		c_sheet_h = sh;
		c_sheet = ck_canvas (sw, sh);
		c_sheet_px = uikit_canvas_pixels (c_sheet, &c_sheet_pitch, 0);
	}
	memset (c_ready, 0, 53);
	ck_masks ();
}

/* colonnes : la carte la plus grande pour la fenetre (ecarts, marges) ; hauteur : rangee du haut + colonne
 * de trois cartes ou presque */
static inline void cards_layout (long ncol)
{
	unsigned long cw = ((unsigned long) g_w - (unsigned long) ((ncol + 1) << 2)) / (unsigned long) ncol;	/* ecarts d'au moins 4 */
	unsigned long ch = (((unsigned long) g_h - (BAND_H + 16)) << 3) / 44;	/* selon la hauteur : 4 hauteurs de carte */
	if (ch < cw)
		cw = ch;
	if (cw < CARD_MIN_W)
		cw = CARD_MIN_W;
	if (cw > CARD_MAX_W)
		cw = CARD_MAX_W;
	long w = (long) cw;
	cards_size (w);
	long gap = (g_w - ncol * w) / (ncol + 1);	/* ecart */
	if (gap > w >> 2)
		gap = w >> 2;			/* (pas trop : le jeu reste groupe) */
	l_gap = gap;
	long width = ncol * w + (ncol - 1) * gap;	/* largeur du jeu */
	l_ox = (g_w - width) >> 1;
	l_top = BAND_H + 6;
	l_tab = l_top + c_h + (c_h >> 6) + 8;
}

/* colonne -> x */
static inline long ck_col_x (long col) { return col * (c_w + l_gap) + l_ox; }

/* x (centre), colonnes -> colonne la plus proche, bornee */
static inline long ck_col_at (long x, long ncol)
{
	long c = (x - l_ox + (l_gap >> 1)) / (l_gap + c_w);
	if (c < 0)
		c = 0;
	if (c >= ncol)
		c = ncol - 1;
	return c;
}

/* rectangle, rayon, couleur : boite aux coins anticrenels, dans la cible courante */
static inline void ck_rbox (long x, long y, long w, long h, long r, uint32_t col)
{
	ui_fill (x + r, y, w - 2 * r, h, col);
	ui_fill (x, y + r, w, h - 2 * r, col);
	ui_vp_disc ((x + r) << 4, (y + r) << 4, r << 4, col);
	ui_vp_disc ((x + w - r) << 4, (y + r) << 4, r << 4, col);
	ui_vp_disc ((x + r) << 4, (y + h - r) << 4, r << 4, col);
	ui_vp_disc ((x + w - r) << 4, (y + h - r) << 4, r << 4, col);
}

/* taille 0..2, enseigne, coin, couleur, renversee : pose le masque dans st_px */
static inline void ck_stamp (int size, int suit, long x, long y, uint32_t col, int flip)
{
	long s = c_pip[size];
	const uint32_t *m = c_maskpx + suit * CK_CELL * CK_CANW + size * CK_CELL;
	uint32_t *d = st_px + y * st_pitch + x;
	for (long r = 0; r < s; r++, d += st_pitch)
	{
		const uint32_t *row = m + (flip ? s - 1 - r : r) * CK_CANW;
		for (long c = 0; c < s; c++)
		{
			uint32_t a = row[c] & 0xFF;
			if (a)
				d[c] = px_mix (d[c], col, a);
		}
	}
}

/* (ck_render) recopie le coin de la valeur, retourne, au coin oppose */
static inline void ck_turn (long x, long y)
{
	long p = c_sheet_pitch;
	const uint32_t *s = c_sheet_px + (y + 2) * p + x + 2;
	uint32_t *d = c_sheet_px + (y + c_h - 3) * p + x + c_w - 3;
	long w = c_w * 3 / 5;
	for (long n = c_ih; n; n--, s += p, d -= p)
		for (long i = 0; i < w; i++)
			d[-i] = s[i];
}

/* (ck_render) les enseignes d'une carte de 2 a 10 */
static inline void ck_pips (long x, long y, int suit, int rank, uint32_t ink)
{
	long side = c_pip[1];
	const unsigned char *lay = pip_lay[rank - 1];
	for (int k = 0; k <= rank; k++, lay += 2)
	{
		long px = x + (c_w >> 1) + ((long) lay[0] - 1) * (c_w * 27 / 100) - (side >> 1);
		long first = c_ih + 3;		/* premiere ligne */
		long py = y + first + (c_h - (2 * first + side)) * lay[1] / 6;
		ck_stamp (1, suit, px, py, ink, lay[1] > 3);	/* renversee en bas */
	}
}

/* (ck_render) valet, dame, roi : un cadre, la lettre, deux enseignes */
static inline void ck_figure (long x, long y, int suit, int rank, uint32_t ink)
{
	long m = c_w >> 3, t = c_ih + 3;	/* marge */
	ui_fill (x + m, y + t, c_w - 2 * m, c_h - 2 * t, ink == CK_RED ? CK_TINT_R : CK_TINT_B);
	ui_frame (x + m, y + t, c_w - 2 * m, c_h - 2 * t, ink);
	long p = c_pip[1];
	ck_stamp (1, suit, x + m + 2, y + t + 2, ink, 0);	/* enseigne en haut a gauche du cadre... */
	ck_stamp (1, suit, x + c_w - m - 2 - p, y + c_h - t - 2 - p, ink, 1);	/* ... et renversee en bas a droite */
	long size = ((c_h - 2 * t) << 1) / 5;	/* la lettre */
	long tw = ui_text_width ((long) s_ranks[rank], size, c_serif);
	ui_text ((long) s_ranks[rank], x + ((c_w - tw) >> 1), y + (c_h >> 1) + (size * 3 >> 3), ink, size, c_serif, 0);
}

/* carte (ou CARD_BACK) : la compose dans la planche */
static inline void ck_render (int card)
{
	if (cd_lay)
		ui_canvas_end ();		/* (la cible n'a qu'un niveau) */
	int suit = card / 13, rank = card % 13;	/* ligne, colonne */
	long x = rank * c_w, y = suit * c_h;
	ui_canvas_begin (c_sheet);
	st_px = c_sheet_px;
	st_pitch = c_sheet_pitch;
	ui_fill (x, y, c_w, c_h, CK_EDGE);	/* le bord, puis le corps */
	ck_rbox (x + 1, y + 1, c_w - 2, c_h - 2, c_r - 1, CK_FACE);
	if (card == CARD_BACK)
	{
		ck_rbox (x + 3, y + 3, c_w - 6, c_h - 6, 3, CK_BACK);	/* le dos */
		long step = c_w >> 4 < 4 ? 4 : c_w >> 4;	/* pas des points, au moins 4 */
		long dot = step >> 1;			/* cote d'un point */
		long row = 0;
		for (long yy = y + 6; yy <= y + c_h - 6 - dot; yy += step, row++)
			for (long xx = x + 6 + (row & 1 ? dot : 0); xx <= x + c_w - 6 - dot; xx += step)
				ui_fill (xx, yy, dot, dot, CK_BACK_DOT);
		ui_frame (x + 5, y + 5, c_w - 10, c_h - 10, CK_BACK_LINE);
	}
	else
	{
		uint32_t ink = (unsigned) (suit - 1) <= 1 ? CK_RED : CK_BLACK;
		const char *s = s_ranks[rank];	/* valeur et enseigne, cote a cote : c'est ce qu'on voit d'une */
		long vx = x + (c_w >> 5) + 2;	/* carte recouverte */
		long base = y + c_idx * 4 / 5 + 3;
		ui_text ((long) s, vx, base, ink, c_idx, (long) ui.font_b, 0);
		long tw = ui_text_width ((long) s, c_idx, (long) ui.font_b);
		ck_stamp (0, suit, vx + tw + 1, base - c_pip[0], ink, 0);	/* (assise sur la ligne de base) */
		ck_turn (x, y);			/* le coin oppose */
		if (rank == 0)
		{
			long s2 = c_pip[2];
			ck_stamp (2, suit, x + ((c_w - s2) >> 1), y + ((c_h - s2) >> 1), ink, 0);
		}
		else if (rank >= 10)
			ck_figure (x, y, suit, rank, ink);
		else
			ck_pips (x, y, suit, rank, ink);
	}
	ui_canvas_end ();
	c_ready[card] = 1;
	st_px = t_px;
	st_pitch = t_pitch;
	if (cd_lay)
		ui_canvas_begin (t_cv);
}

/* carte, coin : la pose dans la couche courante (cd_*), coins arrondis, decoupee */
static inline void card_blit (int card, long x, long y)
{
	if (!c_ready[card])
		ck_render (card);
	const uint32_t *src = c_sheet_px + (card / 13) * c_h * c_sheet_pitch + (card % 13) * c_w;
	long a = -x > 0 ? -x : 0;		/* colonnes a, b de la carte */
	long b = cd_w - x < c_w ? cd_w - x : c_w;
	if (a >= b)
		return;
	for (long r = 0; r < c_h; r++, src += c_sheet_pitch)
	{
		long yy = y + r;
		if (yy < 0)
			continue;
		if (yy >= cd_h)
			return;
		uint32_t *d = cd_px + yy * cd_pitch + x;	/* (colonne 0 de la carte) */
		long e = r < c_h - 1 - r ? r : c_h - 1 - r;	/* distance au bord */
		if (e >= c_r)
		{
			memcpy (d + a, src + a, (size_t) (b - a) * 4);
			continue;
		}
		const unsigned char *cr = c_corner + e * CK_CORNER;
		for (long c = a; c < b; c++)
		{
			long f = c < c_w - 1 - c ? c : c_w - 1 - c;
			uint32_t al = f < c_r ? cr[f] : 255;
			if (al == 0)
				continue;
			uint32_t p = src[c];
			if (al < 255)
				p = px_mix (d[c], p, al);
			d[c] = p;
		}
	}
}

/* coin, enseigne indiquee (-1 : aucune) : une place vide, sur la couche T, cible active */
static inline void card_slot (long x, long y, int hint)
{
	ck_rbox (x, y, c_w, c_h, c_r, TAPIS_SLOT_L);
	ck_rbox (x + 1, y + 1, c_w - 2, c_h - 2, c_r - 1, TAPIS_SLOT);
	if (hint < 0)
		return;
	long s = c_pip[2];
	ck_stamp (2, hint, x + ((c_w - s) >> 1), y + ((c_h - s) >> 1), TAPIS_HINT, 0);
}

/* les cartes vont dans la couche T */
static inline void ck_use_t (void)
{
	cd_px = st_px = t_px;
	cd_pitch = st_pitch = t_pitch;
	cd_w = g_w;
	cd_h = g_h;
}

/* largeur, hauteur de la fenetre : les couches suivent (elles ne font que grandir) */
static inline void layers_size (long w, long h)
{
	g_w = w;
	g_h = h;
	if (w > t_pitch || h > t_cap_h)
	{
		if (w < t_pitch)
			w = t_pitch;
		if (h < t_cap_h)
			h = t_cap_h;
		if (t_cv)
		{
			ui_w_destroy (t_cv);
			ui_w_destroy (k_cv);
		}
		t_cv = ck_canvas (w, h);
		t_px = uikit_canvas_pixels (t_cv, &t_pitch, &t_cap_h);
		k_cv = ck_canvas (w, h);
		k_px = uikit_canvas_pixels (k_cv, 0, 0);
	}
	ck_use_t ();
}

/* les dessins de UIKit vont dans la couche T */
static inline void lay_begin (void)
{
	ui_canvas_begin (t_cv);
	ck_use_t ();
	cd_lay = 1;
}

static inline void lay_end (void)
{
	cd_lay = 0;
	ui_canvas_end ();
}

/* destination, son pas, source, son pas (pixels), coin, taille */
static inline void ck_copy (uint32_t *d, long dp, const uint32_t *s, long sp, long x, long y, long w, long h)
{
	d += y * dp + x;
	s += y * sp + x;
	for (; h > 0; h--, d += dp, s += sp)
		memcpy (d, s, (size_t) w * 4);
}

/* rectangle : T, et les cartes glissees par-dessus, montres d'un coup */
static inline void compose (long x, long y, long w, long h)
{
	long x1 = x + w, y1 = y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x1 > g_w)
		x1 = g_w;
	if (y1 > g_h)
		y1 = g_h;
	w = x1 - x;
	h = y1 - y;
	if (w <= 0 || h <= 0)
		return;
	const uint32_t *src = t_px;
	if (dr_on)
	{
		ck_copy (k_px, t_pitch, t_px, t_pitch, x, y, w, h);
		cd_px = k_px;
		for (long i = 0; i < dr_n; i++)
			card_blit (dr_cards[i], dr_x, dr_y + i * dr_dy);
		cd_px = t_px;
		src = k_px;
	}
	ck_copy (ui.surf, ui.w, src, t_pitch, x, y, w, h);
	ui_update (x, y, w, h);
}

/* toute la fenetre */
static inline void compose_all (void) { compose (0, 0, g_w, g_h); }

/* nouveau coin des cartes glissees : seul ce qui change est refait */
static inline void drag_move (long x, long y)
{
	long bh = c_h + dr_dy * (dr_n - 1);	/* hauteur du paquet */
	long ox = dr_x, oy = dr_y;		/* union avec l'ancienne place */
	dr_x = x;
	dr_y = y;
	long l = x > ox ? ox : x, t = y > oy ? oy : y;
	long r = (x > ox ? x : ox) + c_w, b = (y > oy ? y : oy) + bh;
	compose (l, t, r - l, b - t);
}

/* texte, sa couleur : le bandeau (boutons de band_set) */
static inline void band_draw (const char *text, uint32_t col)
{
	ui_fill (0, 0, g_w, BAND_H, TAPIS_BAND);
	const char *s = bb_s;
	for (long i = 0; i < bb_n; i++)
	{
		ui_rbox (bb_x[i], (BAND_H - BAND_BTN_H) / 2, bb_w[i], BAND_BTN_H, 6, TAPIS_BTN, TAPIS_BTN);
		ui_text (ui_tr ((long) s), bb_x[i] + 10, BAND_H / 2 + 5, TAPIS_TEXT, 14, (long) ui.font, 0);
		s += strlen (s) + 1;
	}
	long x = bb_end + 10;
	long max = g_w - x - (FRAME_CAP_W + 8);
	if (max >= 8)
		ui_text_fit ((long) text, x, BAND_H / 2 + 5, col, 14, (long) ui.font, max);
}

/* texte, sa couleur : la table et le bandeau, sur la couche T (cible active) */
static inline void table_bg (const char *text, uint32_t col)
{
	ui_rbox (0, BAND_H, g_w, g_h - BAND_H, 0, TAPIS_HAUT, TAPIS_BAS);
	band_draw (text, col);
}

/* libelles (chaines a la suite, un 0 de plus a la fin) : boutons du bandeau */
static inline void band_set (const char *labels)
{
	bb_s = labels;
	long n = 0, x = 8;
	for (const char *s = labels; *s && n < BAND_MAX; s += strlen (s) + 1, n++)
	{
		long w = ui_text_width (ui_tr ((long) s), 14, (long) ui.font) + 20;
		bb_x[n] = x;
		bb_w[n] = w;
		x += w + 6;
	}
	bb_n = n;
	bb_end = x;
}

/* x -> bouton du bandeau sous x (-1 : aucun ; *drag = 1 si la place est libre : un appui y deplace la
 * fenetre) */
static inline long band_hit (long x, long *drag)
{
	*drag = 0;
	for (long i = 0; i < bb_n; i++)
		if (x >= bb_x[i] && x < bb_x[i] + bb_w[i])
			return i;
	*drag = x < g_w - FRAME_CAP_W;
	return -1;
}

/* titre, texte : le message de fin, sur T (cible active), couleurs du theme */
static inline void win_box (const char *title, const char *text)
{
	title = TR (title);
	text = TR (text);
	const uint32_t *tok = (const uint32_t *) ui_tokens ();
	long w = ui_text_width ((long) text, 15, (long) ui.font) + 56;	/* largeur */
	long wt = ui_text_width ((long) title, 22, (long) ui.font_b) + 56;
	if (wt > w)
		w = wt;
	long x = (g_w - w) >> 1, y = (g_h - 104) >> 1;
	ck_rbox (x, y, w, 104, 12, tok[TJ_SURFACE / 4]);
	long tw = ui_text_width ((long) title, 22, (long) ui.font_b);
	ui_text ((long) title, x + ((w - tw) >> 1), y + 44, tok[TJ_TEXT1 / 4], 22, (long) ui.font_b, 0);
	tw = ui_text_width ((long) text, 15, (long) ui.font);
	ui_text ((long) text, x + ((w - tw) >> 1), y + 76, tok[TJ_TEXT2 / 4], 15, (long) ui.font, 0);
}

/* les quatre fondations (wa.fx rempli, y en fy) : les cartes vont rebondir */
static inline void win_start (long fy)
{
	wa.fy = fy;
	wa.next = 0;
	wa.on = 0;
	win_on = 1;
}

/* un pas de l'animation, sur la couche T (les traces restent) -> 0 : finie */
static inline int win_step (void)
{
	ck_use_t ();
	for (int k = 0; k < 3; k++)
	{
		if (!wa.on)
		{
			if (wa.next >= 52)
			{
				win_on = 0;
				return 0;
			}
			long f = wa.next & 3, rank = 12 - (wa.next >> 2);	/* enseigne, rang */
			wa.card = f * 13 + rank;
			wa.x = wa.fx[f] << 4;
			wa.y = wa.fy << 4;
			long vx = ck_rnd_n (60) + 40;
			if (ck_rnd () & 1)
				vx = -vx;
			wa.vx = vx;
			wa.vy = -ck_rnd_n (80);
			wa.on = 1;
			wa.next++;
		}
		wa.x += (wa.vx * c_w) >> 6;	/* le pas */
		long y = wa.y + wa.vy, vy = wa.vy + 12;
		long floor = g_h - c_h;
		if (y > floor << 4)
		{
			y = floor << 4;		/* rebond */
			vy = (-vy * 3) >> 2;
			if (vy >= -60)
				vy = -60 - ck_rnd_n (40);
		}
		wa.y = y;
		wa.vy = vy;
		card_blit ((int) wa.card, wa.x >> 4, y >> 4);
		compose (wa.x >> 4, y >> 4, c_w, c_h);
		long cx = wa.x >> 4;		/* sortie de la table ? */
		if (cx + c_w < 0 || cx > g_w)
			wa.on = 0;
	}
	return 1;
}

#endif
