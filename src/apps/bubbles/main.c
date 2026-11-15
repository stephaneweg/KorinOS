/*
 * bubbles -- un jeu de tir de bulles, original. Un lanceur en bas qu'on oriente ; la bulle tiree rebondit sur les
 * murs et se colle a la grille hexagonale du haut (rangees decalees) ; 3 bulles ou plus de la meme couleur reliees
 * eclatent ; celles qui ne tiennent plus au plafond tombent (points en plus). Le plafond descend d'une rangee tous
 * les N tirs ; une bulle sous la ligne du bas : perdu. Niveaux generes (motifs, plus de couleurs, descente plus
 * rapide).
 *
 * Clavier : gauche et droite visent, Espace, Entree ou haut tirent, bas echange la bulle et la suivante ; P : pause ;
 * R : recommencer. Souris ou doigt : viser en touchant ou glissant, tirer au relache ; toucher la suivante l'echange.
 * Manette : croix (tenue, elle tourne), A tire, X ou Y echangent. Plein ecran ; Start, Echap ou le bouton || : le
 * menu de pause (gamepause.h). 640 x 480.
 *
 * Port en C de bubbles.asm (memes regles, memes dessins, memes sons, meme record HOME:/bubbles/best.txt), un
 * seul source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#define GF_W		640
#define GF_H		480
#include "../../inc/gamepause.h"

#define COLS		10			/* bulles d'une rangee paire (9 sur une impaire) */
#define GROWS		13
#define NCELL		(COLS * GROWS)
#define LOSE_ROW	12			/* rangee (plafond compris) qui fait perdre */
#define BD		32			/* diametre */
#define BRAD		16
#define ROW_H		28
#define HIT2		(28 * 28)		/* contact */
#define FLD_X		160
#define FLD_W		(COLS * BD)
#define FLD_Y		44			/* bas du plafond, au depart */
#define CEIL_TOP	26
#define DANGER_Y	(FLD_Y + LOSE_ROW * ROW_H + 6)
#define LX		320			/* le lanceur */
#define LY		434
#define NXX		232			/* la suivante */
#define NXY		430
#define AMIN		16			/* angles en demi-degres (180 : vers le haut) */
#define AMAX		344
#define NSUB		4			/* sous-pas d'une image */
#define SUBPX		3			/* points par sous-pas */
#define NCOLOR		7
#define NSIZE		4
#define FXN		160			/* bulles qui eclatent ou tombent */
#define FX_POP		1
#define FX_FALL		2
#define POP_T		8
#define GRAV		0x5800
#define DECO_N		14

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_LOST, ST_OVER, ST_CLEAR };
enum { ID_NEW = 1, ID_PAUSE, ID_SOUND, ID_QUIT };

#define KEY_ENTER_	0x28
#define KEY_SPACE_	0x2C
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50
#define KEY_DOWN_	0x51
#define KEY_UP_		0x52

enum { TX_LOGO, TX_SUB, TX_PROMPT, TX_RECORD, TX_OVER, TX_AGAIN, TX_NEWBEST, TX_CLEAR, TX_BONUS, TX_CONT, TX_LEVEL,
       TX_HELP1, TX_HELP2, TX_HELP3, NTX };

#define P_DIR		"HOME:/bubbles"
#define P_BEST		"HOME:/bubbles/best.txt"

/* ---- l'etat du jeu ---- */
static long g_state, g_timer, g_prev;		/* g_prev : l'etat d'avant la pause */
static unsigned long g_tick;
static unsigned long g_score, g_show, g_best;
static long g_newbest, g_level, g_ncol;
static long g_every, g_shots;			/* tirs entre deux descentes */
static long g_drop;				/* rangees descendues */
static long g_danim;				/* la descente a l'ecran (points restants) */
static long g_ang, g_hold;
static long g_cur, g_next;			/* couleurs 1..NCOLOR */
static long g_fly;
static long g_bx, g_by, g_vx, g_vy;		/* la bulle tiree (16.16) */
static long g_down, g_ldirty;
static long g_flt, g_fltv, g_fltx, g_flty;	/* le gain qui monte : reste, valeur, x, y */
static unsigned long g_bonus;
static long g_banner, g_popk, g_shk;
static gf_sprite_t *g_sb[NCOLOR * NSIZE];	/* bulles, quatre tailles */
static gf_sprite_t *g_sghost[NCOLOR], *g_shalo[NCOLOR], *g_sring[3], *g_sorb[2];
static gf_sprite_t *g_sdot, *g_sdot2, *g_sarrow, *g_shead, *g_sbase, *g_snbase, *g_son, *g_soff, *g_swarn;
static gf_sprite_t *g_dbig[10], *g_dgain[10], *g_tx[NTX];
static long g_nb[6];
static struct { long x, y, v, size; } g_deco[DECO_N];	/* x, y (16.16), vitesse, taille */
static unsigned char g_grid[NCELL];		/* 0 vide, 1..NCOLOR */
static unsigned char g_mark[NCELL];
static unsigned short g_q[NCELL];
static struct fx { int32_t x, y, vx, vy; uint32_t type, col; int32_t timer, pad; } g_fx[FXN];

/* ---- tables ---- */
static const uint32_t colors[NCOLOR] = { 0x00F04860, 0x003C8CF0, 0x00FFC83C, 0x003CCC7C, 0x00A860F0, 0x00FF8838, 0x0040E0E0 };
static const unsigned char sizes[NSIZE] = { BD, 24, 16, 8 };
static const unsigned char ring_sz[3] = { 16, 26, 40 };
static const struct gs_ent snd_fall[] = {
	{ 0, 700, 70, AKW_SINE, 80 }, { 70, 560, 70, AKW_SINE, 80 }, { 140, 440, 90, AKW_SINE, 80 },
	{ 230, 330, 140, AKW_SINE, 70 }, { 0, 0, 0, 0, 0 } };
static const struct gs_ent snd_drop[] = {
	{ 0, 98, 220, AKW_SQUARE, 90 }, { 40, 73, 260, AKW_TRIANGLE, 110 }, { 0, 0, 0, 0, 0 } };
static const struct ga_txe tx_table[NTX] = {
	{ "Bubbles", 76, 1, 0x00FFFFFF, 0x0060B4FF },
	{ "Visez, tirez, faites \xC3\xA9" "clater les bulles", 15, 0, 0x00D8E0F4, 0x00A8B4D0 },
	{ "Espace, A ou toucher pour jouer", 18, 1, 0x00FFFFFF, 0x00D0E0FF },
	{ "Record", 15, 1, 0x00C8D4F0, 0x00A0B0D0 },
	{ "Partie termin\xC3\xA9" "e", 26, 1, 0x00FFD878, 0x00F09040 },
	{ "Espace, A ou toucher : rejouer", 13, 0, 0x00D8E0F4, 0x00A8B4D0 },
	{ "Nouveau record !", 15, 1, 0x00FFE080, 0x00F0B040 },
	{ "Niveau r\xC3\xA9ussi !", 26, 1, 0x00C8FFD0, 0x0048D088 },
	{ "Bonus", 15, 1, 0x00FFE8A0, 0x00F0C060 },
	{ "Espace, A ou toucher : continuer", 13, 0, 0x00D8E0F4, 0x00A8B4D0 },
	{ "Niveau", 28, 1, 0x00FFFFFF, 0x0080C0FF },
	{ "Gauche, droite ou souris : viser", 13, 0, 0x00B8C4E0, 0x0098A4C4 },
	{ "Espace, haut, A ou rel\xC3\xA2" "cher : tirer", 13, 0, 0x00B8C4E0, 0x0098A4C4 },
	{ "Bas, X, Y ou toucher la suivante : \xC3\xA9" "changer", 13, 0, 0x00B8C4E0, 0x0098A4C4 },
};
static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tPause\tP\nI3c\tSon\n-\nI4\tQuitter\n";
/* sin(k / 2 degres) en 16.16, k = 0..180 */
static const uint32_t sin_tab[181] = {
	0,572,1144,1716,2287,2859,3430,4001,
	4572,5142,5712,6281,6850,7419,7987,8554,
	9121,9687,10252,10817,11380,11943,12505,13066,
	13626,14185,14742,15299,15855,16409,16962,17514,
	18064,18613,19161,19707,20252,20795,21336,21876,
	22415,22951,23486,24019,24550,25080,25607,26132,
	26656,27177,27697,28214,28729,29242,29753,30261,
	30767,31271,31772,32271,32768,33262,33754,34242,
	34729,35212,35693,36172,36647,37120,37590,38057,
	38521,38982,39441,39896,40348,40797,41243,41686,
	42126,42562,42995,43425,43852,44275,44695,45112,
	45525,45935,46341,46744,47143,47538,47930,48318,
	48703,49084,49461,49834,50203,50569,50931,51289,
	51643,51993,52339,52682,53020,53354,53684,54010,
	54332,54650,54963,55273,55578,55879,56175,56468,
	56756,57040,57319,57594,57865,58131,58393,58650,
	58903,59152,59396,59635,59870,60100,60326,60547,
	60764,60976,61183,61386,61584,61777,61966,62149,
	62328,62503,62672,62837,62997,63152,63303,63449,
	63589,63725,63856,63983,64104,64220,64332,64439,
	64540,64637,64729,64816,64898,64975,65048,65115,
	65177,65234,65287,65334,65376,65414,65446,65474,
	65496,65514,65526,65534,65536,
};

/* ---------------------------------------------------------------------------------------------------------------
 *  Sprites et couche
 * --------------------------------------------------------------------------------------------------------------- */
/* diametre, couleur -> une bulle de savon (pour gf_glow), claire au bord, un reflet en haut a gauche */
static gf_sprite_t *gen_ring (unsigned long d, uint32_t c)
{
	gf_sprite_t *s = ga_new (d, d);
	uint32_t *p = s + 2;
	unsigned long dd = d * d, hl = d * 7 >> 4;	/* le reflet : 0,44 du rayon */
	for (unsigned long y = 0; y < d; y++)
		for (unsigned long x = 0; x < d; x++, p++)
		{
			long dx = (long) (x << 1) + 1 - (long) d, dy = (long) (y << 1) + 1 - (long) d;	/* en demi-points */
			unsigned long q = (unsigned long) (dx * dx + dy * dy);
			if (q >= dd)
				continue;
			unsigned long t = (q << 8) / dd;	/* 0..255 du centre au bord */
			t = t * t >> 8;
			t = t * t >> 8;
			t += 20;
			long ex = dx + (long) hl, ey = dy + (long) hl;
			if ((unsigned long) (ex * ex + ey * ey) < dd >> 4)
				t += 150;
			if (t > 256)
				t = 256;
			*p = ga_shade (c, (uint32_t) t);
		}
	return s;
}

static void gen_sprites (void)
{
	for (int c = 0; c < NCOLOR; c++)
	{
		uint32_t col = colors[c];
		for (int z = 0; z < NSIZE; z++)
			g_sb[c * 4 + z] = ga_ball (sizes[z], col);
		g_sghost[c] = ga_ball (BD, col);	/* le fantome : ou elle se posera */
		ga_fade (g_sghost[c], 80);
		g_shalo[c] = ga_disc (58, (col >> 2) & 0x3F3F3F);	/* la lueur, du quart de la couleur */
	}
	for (int i = 0; i < 3; i++)			/* bulles de savon du decor */
		g_sring[i] = gen_ring (ring_sz[i], 0x00385074);
	g_sorb[0] = ga_disc (170, 0x00141038);
	g_sorb[1] = ga_disc (140, 0x00082C3C);
	g_sdot = ga_ball (7, 0x00E8F0FF);
	g_sdot2 = ga_ball (5, 0x0090A0C8);
	g_sarrow = ga_ball (9, 0x00FFC850);
	g_shead = ga_ball (6, 0x00FFE090);
	g_sbase = ga_ball (50, 0x00485A80);
	g_snbase = ga_ball (34, 0x00303C5C);
	g_son = ga_ball (11, 0x00FFB840);
	g_soff = ga_ball (11, 0x00384058);
	g_swarn = ga_ball (11, 0x00FF4058);
	ui_canvas_begin (gf_cv);
	ga_digits (g_dbig, 20, (long) ui.font_b, 0x00FFFFFF, 0x00C0D4FF);
	ga_digits (g_dgain, 22, (long) ui.font_b, 0x00FFF8C0, 0x00FFA830);
	ga_texts (tx_table, g_tx, NTX);
	ui_canvas_end ();
}

static void build_layer (void)
{
	g_ldirty = 0;
	ga_sky (0x001A2C54, 0x00080A1C, 0x00304878, 90);
	gf_dst = gf_layer;
	if (g_state != ST_TITLE)
	{
		ga_panel (FLD_X - 10, 18, FLD_W + 20, 460, 16, 0x003A4A70, 0x00182238, 220, 0x007088B8);	/* le cadre et la vitre */
		ga_panel (FLD_X - 2, CEIL_TOP, FLD_W + 4, 444, 10, 0x000A1022, 0x00141C36, 235, 0);
		ga_panel (LX - 118, 404, 236, 64, 20, 0x00242E4A, 0x00101828, 220, 0x00405478);	/* le pupitre */
		for (long x = FLD_X + 2; x < FLD_X + FLD_W - 4; x += 13)	/* la ligne a ne pas passer, en tirets */
			gf_rect (x, DANGER_Y, 7, 2, 0x00B83850);
		ga_panel (14, 44, 128, 186, 14, 0x00243256, 0x00121A34, 200, 0x00506494);	/* les panneaux des scores */
		ga_panel (GF_W - 142, 44, 128, 70, 14, 0x00243256, 0x00121A34, 200, 0x00506494);
		ga_mark (FLD_X - 10, 18, FLD_W + 20, 460);	/* (les bulles du decor passent derriere) */
		ga_mark (14, 44, 128, 186);
		ga_mark (GF_W - 142, 44, 128, 70);
		ui_canvas_begin (gf_cv);
		gf_text ("Score", 30, 68, 0x00B8C4E0, 13, (long) ui.font_b);
		gf_text ("Niveau", 30, 128, 0x00B8C4E0, 13, (long) ui.font_b);
		gf_text ("Record", 30, 188, 0x00B8C4E0, 13, (long) ui.font_b);
		gf_text ("Descente", GF_W - 126, 68, 0x00B8C4E0, 13, (long) ui.font_b);
		gf_text_c ("Suivante", NXX, 462, 0x008C9CC0, 11, (long) ui.font);
		ui_canvas_end ();
	}
	gf_dst = gf_frame;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  La grille
 * --------------------------------------------------------------------------------------------------------------- */
/* case -> son centre (sans l'animation de descente) */
static void cell_pos (unsigned long c, long *x, long *y)
{
	unsigned long r = c / COLS, col = c % COLS;
	*x = (long) (col << 5) + FLD_X + BRAD + (long) ((r & 1) << 4);
	*y = (long) (r + (unsigned long) g_drop) * ROW_H + FLD_Y + BRAD;
}

/* case -> nombre de voisines, leurs numeros dans g_nb (rangees impaires decalees a droite d'un rayon) */
static long nbrs (unsigned long c)
{
	unsigned long r = c / COLS, col = c % COLS, odd = r & 1, len = COLS - odd;
	long n = 0;
	g_nb[n] = (long) c - 1;
	if (col - 1 < len) n++;
	g_nb[n] = (long) c + 1;
	if (col + 1 < len) n++;
	unsigned long a = col - 1 + odd, b = a + 1, len2 = COLS + odd - 1;	/* dans les rangees voisines */
	if (r)
	{
		g_nb[n] = (long) ((r - 1) * COLS + a);
		if (a < len2) n++;
		g_nb[n] = (long) ((r - 1) * COLS + b);
		if (b < len2) n++;
	}
	if (r + 1 < GROWS)
	{
		g_nb[n] = (long) ((r + 1) * COLS + a);
		if (a < len2) n++;
		g_nb[n] = (long) ((r + 1) * COLS + b);
		if (b < len2) n++;
	}
	return n;
}

/* case de depart (-1 : toutes les bulles de la rangee 0), couleur a suivre (0 : toutes) -> nombre de cases
 * marquees (g_mark), g_q : la liste, des plus proches aux plus loin */
static long bfs (long start, long color)
{
	long n = 0;
	memset (g_mark, 0, sizeof g_mark);
	if (start >= 0)
	{
		g_mark[start] = 1;
		g_q[n++] = (unsigned short) start;
	}
	else
		for (int i = 0; i < COLS; i++)
			if (g_grid[i])
			{
				g_mark[i] = 1;
				g_q[n++] = (unsigned short) i;
			}
	for (long h = 0; h < n;)
	{
		long k = nbrs (g_q[h++]);
		for (long j = 0; j < k; j++)
		{
			long c = g_nb[j];
			if (g_mark[c] || !g_grid[c])
				continue;
			if (color && g_grid[c] != color)
				continue;
			g_mark[c] = 1;
			g_q[n++] = (unsigned short) c;
		}
	}
	return n;
}

/* x, y (points) -> 1 si une bulle a cet endroit touche le plafond ou une bulle de la grille */
static long hit_at (long x, long y)
{
	long base = g_drop * ROW_H + FLD_Y + BRAD;	/* centre de la rangee 0 */
	if (y <= base)
		return 1;
	long r = (long) ((unsigned long) (y - base) / ROW_H), r0 = r - 1, r1 = r + 1;
	if (r0 < 0) r0 = 0;
	if (r1 > GROWS - 1) r1 = GROWS - 1;
	for (; r0 <= r1; r0++)
	{
		long dy = y - (base + r0 * ROW_H), dy2 = dy * dy, odd = r0 & 1, ox = FLD_X + BRAD + (odd << 4);
		const unsigned char *g = g_grid + r0 * COLS;
		for (long c = 0; c < COLS - odd; c++)
		{
			if (!g[c])
				continue;
			long dx = x - ((c << 5) + ox);
			if ((unsigned long) (dx * dx + dy2) < HIT2)
				return 1;
		}
	}
	return 0;
}

/* x, y -> la case libre la plus proche qui tient (plafond ou voisine), -1 : aucune */
static long find_cell (long x, long y)
{
	long best = -1;
	unsigned long bd = ~0ul;
	for (int relax = 0; relax < 2; relax++)		/* 1 : sans la condition de tenue */
	{
		for (unsigned long c = 0; c < NCELL; c++)
		{
			unsigned long r = c / COLS;
			if ((r & 1) && c % COLS == COLS - 1)
				continue;
			if (g_grid[c])
				continue;
			if (!relax && r)
			{
				long k = nbrs (c), j;
				for (j = 0; j < k; j++)
					if (g_grid[g_nb[j]])
						break;
				if (j == k)
					continue;
			}
			long cx, cy;
			cell_pos (c, &cx, &cy);
			cx -= x;
			cy -= y;
			unsigned long d = (unsigned long) (cx * cx + cy * cy);
			if (d < bd)
			{
				bd = d;
				best = (long) c;
			}
		}
		if (best >= 0)
			break;
	}
	return best;
}

/* -> les couleurs presentes (bit c) */
static unsigned long color_mask (void)
{
	unsigned long m = 0;
	for (int i = 0; i < NCELL; i++)
		m |= 1ul << g_grid[i];
	return m & ~1ul;
}

/* -> une couleur 1..ncol, parmi celles qui restent sur le plateau */
static long pick_color (void)
{
	unsigned long m = color_mask ();
	for (int t = 40; t; t--)
	{
		long c = (long) gf_below ((unsigned long) g_ncol) + 1;
		if (!m || ((m >> (c & 63)) & 1))
			return c;
	}
	return __builtin_ctzl (m);
}

/* la bulle et la suivante : des couleurs encore presentes */
static void fix_colors (void)
{
	unsigned long m = color_mask ();
	if (!m)
		return;
	if (!((m >> (g_cur & 63)) & 1))
		g_cur = pick_color ();
	if (!((m >> (g_next & 63)) & 1))
		g_next = pick_color ();
}

/* -> 1 si une bulle est sous la ligne */
static long check_lose (void)
{
	long s = LOSE_ROW - g_drop;
	if (s < 0)
		s = 0;
	for (long i = s * COLS; i < NCELL; i++)
		if (g_grid[i])
			return 1;
	return 0;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Bulles qui eclatent, bulles qui tombent
 * --------------------------------------------------------------------------------------------------------------- */
/* type, x, y (points), couleur, minuterie -> l'entree (0 : plus de place) */
static struct fx *fx_add (uint32_t type, long x, long y, uint32_t col, long timer)
{
	for (int i = 0; i < FXN; i++)
	{
		struct fx *f = &g_fx[i];
		if (f->type)
			continue;
		f->x = (int32_t) (x << 16);
		f->y = (int32_t) (y << 16);
		f->vx = f->vy = 0;
		f->type = type;
		f->col = col;
		f->timer = (int32_t) timer;
		return f;
	}
	return 0;
}

static void fx_clear (void)
{
	for (int i = 0; i < FXN; i++)
		g_fx[i].type = 0;
}

static void fx_step (void)
{
	for (int i = 0; i < FXN; i++)
	{
		struct fx *f = &g_fx[i];
		if (!f->type)
			continue;
		if (f->type == FX_POP)
		{
			uint32_t t = (uint32_t) f->timer - 1;
			f->timer = (int32_t) t;
			if (!t)
			{
				f->type = 0;
				continue;
			}
			if (t != POP_T)
				continue;
			/* elle eclate : gerbe, note qui monte */
			ga_burst (f->x >> 16, f->y >> 16, colors[f->col - 1], 14, 170000, 18, 3, 5000);
			long k = g_popk++;
			if (k > 20)
				k = 20;
			gs_note (k * 45 + 520, 60, AKW_SINE, 90);
			continue;
		}
		if (f->timer)				/* (une attente d'abord) */
		{
			f->timer--;
			continue;
		}
		f->x = (int32_t) ((uint32_t) f->x + (uint32_t) f->vx);
		f->y = (int32_t) ((uint32_t) f->y + (uint32_t) f->vy);
		f->vy = (int32_t) ((uint32_t) f->vy + GRAV);
		if ((f->y >> 16) >= GF_H + 24)
			f->type = 0;
	}
}

/* sprite, centre */
static void centered (const gf_sprite_t *s, long x, long y) { ga_ablit (x - (long) (s[0] >> 1), y - (long) (s[1] >> 1), s); }
static void centered_glow (const gf_sprite_t *s, long x, long y) { gf_glow (x - (long) (s[0] >> 1), y - (long) (s[1] >> 1), s); }

static void fx_draw (void)
{
	for (int i = 0; i < FXN; i++)
	{
		struct fx *f = &g_fx[i];
		if (!f->type)
			continue;
		long c = (long) f->col - 1, x = f->x >> 16, y = f->y >> 16, size = 0;
		if (f->type == FX_POP && (uint32_t) f->timer <= POP_T)
		{					/* elle eclate : lueur, elle retrecit */
			size = (long) ((uint32_t) (POP_T - f->timer) >> 1);
			centered_glow (g_shalo[c], x, y);
		}
		centered (g_sb[(c << 2) + size], x, y);
	}
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Les regles
 * --------------------------------------------------------------------------------------------------------------- */
static void set_state (long s)
{
	g_state = s;
	g_timer = 0;
	gf_still = s == ST_PAUSE;
	gf_redraw = 1;
}

/* le niveau g_level : couleurs, rangees, descente, motif */
static void start_level (void)
{
	long L = g_level;
	g_ncol = 3 + (L >> 1);				/* couleurs : 3 + niveau / 2, 7 au plus */
	if (g_ncol > NCOLOR)
		g_ncol = NCOLOR;
	unsigned long rows = 4 + ((unsigned long) L >> 1);	/* rangees : 4 + niveau / 2, 8 au plus */
	if (rows > 8)
		rows = 8;
	g_every = 10 - L;				/* tirs par descente : 10 - niveau, 4 au moins */
	if (g_every < 4)
		g_every = 4;
	long pat = (L - 1) & 3;				/* motif */
	g_drop = g_danim = g_shots = g_fly = 0;
	memset (g_grid, 0, sizeof g_grid);
	for (unsigned long c = 0; c < NCELL; c++)
	{
		unsigned long r = c / COLS, col = c % COLS;
		if (r >= rows)
			break;
		if ((r & 1) && col == COLS - 1)
			continue;
		unsigned long pos = (col << 1) + (r & 1);	/* position en rayons */
		if (pat == 1)
		{					/* un entonnoir */
			long d = (long) pos - 9;
			if (d < 0)
				d = -d;
			if ((unsigned long) d > ((rows - 1 - r) << 1) + 3)
				continue;
		}
		else if (pat == 2)
		{					/* des colonnes qui pendent */
			if (r >= 2 && ((pos >> 2) & 1))
				continue;
		}
		else if (pat == 3)
		{					/* un treillis a trous */
			if (r && (r * 3 + pos) % 7 >= 5)
				continue;
		}
		unsigned long k = gf_below (100);	/* des grappes : souvent la couleur voisine */
		unsigned char v = 0;
		if (k < 45 && col)
			v = g_grid[c - 1];
		if (!v && k < 70 && r)
			v = g_grid[c - COLS];
		if (!v)
			v = (unsigned char) (gf_below ((unsigned long) g_ncol) + 1);
		g_grid[c] = v;
	}
	bfs (-1, 0);					/* rien ne flotte */
	for (int i = 0; i < NCELL; i++)
		if (!g_mark[i])
			g_grid[i] = 0;
#ifdef BUB_TEST						/* (essai : deux bulles d'une couleur) */
	memset (g_grid, 0, sizeof g_grid);
	g_grid[4] = g_grid[5] = 1;
	g_ncol = 1;
#endif
#ifdef BUB_LOSE						/* (essai : le plafond descend a chaque tir) */
	g_every = 1;
#endif
	if (!color_mask ())				/* (jamais vide) */
		for (int i = 0; i < COLS; i++)
			g_grid[i] = (unsigned char) (gf_below ((unsigned long) g_ncol) + 1);
	g_cur = pick_color ();
	g_next = pick_color ();
	g_banner = 110;
	set_state (ST_PLAY);
	g_ldirty = 1;
}

static void new_game (void)
{
	g_score = g_show = 0;
	g_newbest = g_flt = g_down = 0;
	g_level = 1;
	g_ang = 180;
	fx_clear ();
	ga_parts_clear ();
	start_level ();
}

static void next_level (void)
{
	g_level++;
	start_level ();
}

/* angle -> cos, sin (16.16 ; sin > 0 : vers le haut) */
static void aim_dir (unsigned long a, long *c, long *s)
{
	if (a <= 180)
	{
		*s = sin_tab[a];
		*c = sin_tab[180 - a];
	}
	else
	{
		*s = sin_tab[360 - a];
		*c = -(long) sin_tab[a - 180];
	}
}

/* point : le lanceur le vise (rien s'il est sous le lanceur) */
static void aim_at (long px, long py)
{
	long dx = px - LX, dy = LY - py;
	if (dy < 10)
		return;
	long best = AMIN, bv = (long) (1ul << 63);
	for (long a = AMIN; a <= AMAX; a++)
	{
		long c, s;
		aim_dir ((unsigned long) a, &c, &s);
		long v = c * dx + s * dy;
		if (v > bv)
		{
			bv = v;
			best = a;
		}
	}
	g_ang = best;
}

/* gauche / droite tenues (clavier, croix) : le lanceur tourne, de plus en plus vite */
static void aim_step (void)
{
	long l = gf_held[KEY_LEFT_] | (gp_now & PAD_LEFT), r = gf_held[KEY_RIGHT_] | (gp_now & PAD_RIGHT);
	long d = (l != 0) - (r != 0);			/* +1 : vers la gauche */
	if (!d)
	{
		g_hold = 0;
		return;
	}
	long h = ++g_hold, sp = h < 10 ? 1 : h < 30 ? 2 : 3;
	long a = g_ang + sp * d;
	if (a < AMIN) a = AMIN;
	if (a > AMAX) a = AMAX;
	g_ang = a;
}

static void shoot (void)
{
	if (g_state != ST_PLAY || g_fly)
		return;
	long c, s;
	aim_dir ((unsigned long) g_ang, &c, &s);
	g_vx = c * SUBPX;
	g_vy = -(s * SUBPX);
	g_bx = (long) LX << 16;
	g_by = (long) LY << 16;
	g_fly = 1;
	gs_note (620, 45, AKW_TRIANGLE, 90);
	gs_later (35, 930, 45, AKW_TRIANGLE, 70);
}

static void do_swap (void)
{
	if (g_state != ST_PLAY || g_fly)
		return;
	long t = g_cur;
	g_cur = g_next;
	g_next = t;
	gs_note (880, 35, AKW_SINE, 70);
}

/* perdu : toutes les bulles tombent, du bas vers le haut */
static void lose (void)
{
	set_state (ST_LOST);
	g_fly = 0;
	for (unsigned long i = 0; i < NCELL; i++)
	{
		unsigned char col = g_grid[i];
		if (!col)
			continue;
		g_grid[i] = 0;
		long x, y;
		cell_pos (i, &x, &y);
		struct fx *f = fx_add (FX_FALL, x, y - g_danim, col, (long) ((GROWS - i / COLS) << 2));
		if (f)
			f->vx = (int32_t) (gf_below (0x20000) - 0x10000);
	}
	if (g_score > g_best)
	{
		g_best = g_score;
		g_newbest = 1;
		gf_save_best (P_DIR, P_BEST, g_best);
	}
	gs_seq (gs_tab_lose);
}

static void level_clear (void)
{
	set_state (ST_CLEAR);
	g_bonus = (unsigned long) g_level * 250;
	g_score += g_bonus;
	gs_seq (gs_tab_win);
}

/* ou la bulle s'arrete : elle se pose, les groupes eclatent, ce qui ne tient plus tombe, le plafond descend,
 * perdu ou gagne */
static void land (long x, long y)
{
	g_fly = 0;
	g_popk = 0;
	long c = find_cell (x, y);
	if (c >= 0)
	{
		g_grid[c] = (unsigned char) g_cur;
		long n = bfs (c, g_cur);
		if (n < 3)
			gs_note (300, 60, AKW_TRIANGLE, 80);	/* elle colle */
		else
		{
			for (long i = 0; i < n; i++)
			{
				unsigned long k = g_q[i];
				unsigned char col = g_grid[k];
				g_grid[k] = 0;
				long px, py;
				cell_pos (k, &px, &py);
				fx_add (FX_POP, px, py - g_danim, col, i * 3 + POP_T + 1);	/* en cascade depuis l'impact */
			}
			unsigned long gain = (unsigned long) n * 10;
			bfs (-1, 0);			/* ce qui ne tient plus */
			unsigned long falls = 0;
			for (unsigned long i = 0; i < NCELL; i++)
			{
				unsigned char col = g_grid[i];
				if (!col || g_mark[i])
					continue;
				g_grid[i] = 0;
				falls++;
				long px, py;
				cell_pos (i, &px, &py);
				struct fx *f = fx_add (FX_FALL, px, py - g_danim, col, 0);
				if (!f)
					continue;
				f->vx = (int32_t) (gf_below (0x18000) - 0xC000);
				f->vy = (int32_t) -(long) (gf_below (0x14000) + 0x6000);
			}
			if (falls)
			{				/* 20 par bulle, plus pour une grosse grappe */
				gain += ((falls + 3) >> 2) * falls * 20;
				gs_seq (snd_fall);
			}
			g_score += gain;
			g_fltv = (long) gain;
			g_flt = 60;
			cell_pos ((unsigned long) c, &g_fltx, &g_flty);
		}
	}
	if (!color_mask ())
	{
		level_clear ();
		return;
	}
	long s = g_shots + 1;
	if (s >= g_every)
	{
		g_drop++;
		g_danim = ROW_H;
		g_shots = 0;
		gs_seq (snd_drop);
	}
	else
		g_shots = s;
	if (check_lose ())
	{
		lose ();
		return;
	}
	g_cur = g_next;
	g_next = pick_color ();
	fix_colors ();
}

/* la bulle tiree avance (quatre sous-pas), rebondit, se pose */
static void fly_step (void)
{
	long bounce = 0;
	for (int n = NSUB; n; n--)
	{
		long x = g_bx + g_vx, lim;
		g_by += g_vy;
		if (x < (lim = (long) (FLD_X + BRAD) << 16) || x > (lim = (long) (FLD_X + FLD_W - BRAD) << 16))
		{
			x = (lim << 1) - x;
			g_vx = -g_vx;
			bounce = 1;
		}
		g_bx = x;
		if (hit_at (x >> 16, g_by >> 16))
		{
			land (g_bx >> 16, g_by >> 16);
			return;
		}
	}
	if (bounce)
		gs_note (1400, 18, AKW_SINE, 50);
}

/* (gamepause) la partie s'arrete -> 1 si elle etait en cours */
static long game_pause (void)
{
	long s = g_state;
	if (s != ST_PLAY && s != ST_LOST && s != ST_CLEAR)
		return 0;
	g_prev = s;
	g_state = ST_PAUSE;
	g_down = 0;
	gf_still = 1;
	gf_redraw = 1;
	return 1;
}

/* (gamepause) la partie repart */
static void game_resume (void)
{
	if (g_state != ST_PAUSE)
		return;
	g_state = g_prev;
	gf_still = 0;
	gf_redraw = 1;
}

static void deco_init (void)
{
	for (int i = 0; i < DECO_N; i++)
	{
		g_deco[i].x = (long) gf_below (GF_W);
		g_deco[i].y = (long) gf_below (GF_H) << 16;
		g_deco[i].v = (long) gf_below (0xA000) + 0x3000;
		g_deco[i].size = (long) gf_below (3);
	}
}

static void deco_step (void)
{
	for (int i = 0; i < DECO_N; i++)
	{
		g_deco[i].y -= g_deco[i].v;
		if ((g_deco[i].y >> 16) + 40 < 0)
		{
			g_deco[i].y = (long) (GF_H + 30) << 16;
			g_deco[i].x = (long) gf_below (GF_W);
		}
	}
}

static void step (void)
{
	g_tick++;
	gs_tick ();
	ga_parts_step ();
	deco_step ();
	fx_step ();
	g_timer++;
	if (g_score > g_show)
	{
		unsigned long d = (g_score - g_show) >> 3;
		g_show += d < 1 ? 1 : d;
	}
	if (g_flt)
		g_flt--;
	if (g_banner)
		g_banner--;
	g_danim = g_danim - 2 < 0 ? 0 : g_danim - 2;
	if (g_state == ST_PLAY)
	{
		aim_step ();
		if (g_fly)
			fly_step ();
	}
	else if (g_state == ST_LOST)
	{
		if (g_timer >= 120)
			set_state (ST_OVER);
	}
	else if (g_state == ST_CLEAR)
	{
		if (g_timer >= 240)
			next_level ();
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

/* tirer, ou commencer / continuer / rejouer */
static void act_fire (void)
{
	long s = g_state;
	if (s == ST_TITLE)
		new_game ();
	else if (s == ST_OVER)
	{
		if (g_timer >= 25)
			new_game ();
	}
	else if (s == ST_CLEAR)
	{
		if (g_timer >= 40)
			next_level ();
	}
	else if (s == ST_PLAY)
		shoot ();
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
	unsigned ch = (uint32_t) GF_EV (EV_CHAR) | 0x20;
	if (ch == 'p')
	{
		gp_open ();
		return;
	}
	if (ch == 'r')
	{
		if (g_state)
			new_game ();
		return;
	}
	uint32_t code = (uint32_t) GF_EV (EV_CODE);
	if (code == KEY_SPACE_ || code == KEY_ENTER_ || code == KEY_UP_)
		act_fire ();
	else if (code == KEY_DOWN_)
		do_swap ();
}

static void on_mouse (void)
{
	long mx, my;
	gf_mouse (&mx, &my);
	long kind = GF_EV (EV_KIND);
	if (kind == 0)
	{						/* survol ou glissement : viser */
		if (g_state == ST_PLAY)
			aim_at (mx, my);
	}
	else if (kind == 1)
	{
		if (!(GF_EV (EV_CODE) & 1))
			return;
		if (g_state != ST_PLAY)
		{
			act_fire ();
			return;
		}
		long ax = mx - NXX, ay = my - NXY;	/* la suivante : echange */
		if (ax < 0) ax = -ax;
		if (ay < 0) ay = -ay;
		if (ax < 22 && ay < 22)
		{
			do_swap ();
			return;
		}
		g_down = 1;
		aim_at (mx, my);
	}
	else if (kind == 2)
	{
		if (!g_down)
			return;
		g_down = 0;
		if (g_state != ST_PLAY)
			return;
		aim_at (mx, my);
		shoot ();
	}
}

static void on_pad (void)
{
	if (!GF_EV (EV_KIND))
		return;
	unsigned long n = gp_new;			/* (Start : gamepause ; la croix tenue : aim_step) */
	if (n & (PAD_A | PAD_UP))
		act_fire ();
	if (n & (PAD_X | PAD_Y))
		do_swap ();
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
static void deco_draw (void)
{
	for (long i = 0; i < DECO_N; i++)
	{
		long w = ((long) ga_sin8 (g_tick + (unsigned long) i * 45) - 127) >> 4;
		const gf_sprite_t *s = g_sring[g_deco[i].size];
		long h = (long) (s[0] >> 1);
		gf_bglow (g_deco[i].x + w - h, (g_deco[i].y >> 16) - h, s);
	}
}

static void draw_hud (void)
{
	ga_num (g_show, 30, 74, g_dbig, 4);
	ga_num ((unsigned long) g_level, 30, 134, g_dbig, 4);
	ga_num (g_best, 30, 194, g_dbig, 4);
	unsigned long every = (unsigned long) g_every, left = every - (unsigned long) g_shots;	/* tirs avant la descente */
	for (unsigned long i = 0; i < every; i++)
	{
		const gf_sprite_t *s = g_soff;
		if (i < left)
			s = left == 1 && (g_tick & 8) ? g_swarn : g_son;
		centered (s, (long) i * 12 + GF_W - 124, 92);
	}
}

/* le plafond, qui descend */
static void draw_ceiling (void)
{
	long bot = g_drop * ROW_H + FLD_Y - g_danim, span = bot - CEIL_TOP, shk = g_shk;	/* son bas */
	for (long y = CEIL_TOP; (unsigned long) y < (unsigned long) bot; y++)
		gf_rect (shk + FLD_X - 2, y, FLD_W + 4, 1,
			 ga_mix (0x005C6C94, 0x002C3858, (uint32_t) (((unsigned long) (y - CEIL_TOP) << 8) / (unsigned long) span)));
	gf_rect (shk + FLD_X - 2, bot - 2, FLD_W + 4, 2, 0x00A8C0E8);
	gf_rect (shk + FLD_X - 2, bot, FLD_W + 4, 3, 0x00060A14);
	for (long i = 0; i < 8; i++)			/* les rivets */
		centered (g_sdot2, i * 40 + FLD_X + 20 + shk, bot - 9);
}

static void draw_grid (void)
{
	for (unsigned long c = 0; c < NCELL; c++)
	{
		unsigned char col = g_grid[c];
		if (!col)
			continue;
		long x, y;
		cell_pos (c, &x, &y);
		centered (g_sb[(col - 1) << 2], x + g_shk, y - g_danim);
	}
}

/* les pointilles de la trajectoire (un rebond) et le fantome a l'arrivee */
static void draw_aim (void)
{
	long c, s;
	aim_dir ((unsigned long) g_ang, &c, &s);
	long dx = c << 2, dy = -s << 2;			/* 4 points par pas */
	long x = (long) LX << 16, y = (long) LY << 16, b = 0;
	const long L = (long) (FLD_X + BRAD) << 16, R = (long) (FLD_X + FLD_W - BRAD) << 16;
	for (long n = 1; n < 220; n++)
	{
		x += dx;
		y += dy;
		if (x < L || x > R)
		{
			x = ((x < L ? L : R) << 1) - x;
			dx = -dx;
			if (++b >= 2)
				return;
		}
		if (hit_at (x >> 16, y >> 16))
		{
			long k = find_cell (x >> 16, y >> 16);
			if (k < 0)
				return;
			long px, py;
			cell_pos ((unsigned long) k, &px, &py);
			centered (g_sghost[g_cur - 1], px, py - g_danim);
			return;
		}
		if ((n & 3) || n < 8)
			continue;
		centered (b ? g_sdot2 : g_sdot, x >> 16, y >> 16);
	}
}

/* le lanceur, sa fleche, la bulle prete et la suivante */
static void draw_launcher (void)
{
	centered (g_sbase, LX, LY);
	centered (g_snbase, NXX, NXY);
	if (g_next)
		centered (g_sb[((g_next - 1) << 2) + 1], NXX, NXY);
	long c, s;
	aim_dir ((unsigned long) g_ang, &c, &s);
	for (long d = 20; d < 54; d += 2)
		centered (g_sarrow, (c * d >> 16) + LX, LY - (s * d >> 16));
	for (long d = 50; d < 68; d++)			/* la pointe */
	{
		long hw = (long) ((unsigned long) ((68 - d) * 5) / 12);	/* demi-largeur */
		for (long k = -hw; k <= hw; k += 2)	/* centre + dir * d + perp * k */
			centered (g_shead, ((c * d + s * k) >> 16) + LX, ((c * k - s * d) >> 16) + LY);
	}
	if (g_fly || !g_cur)
		return;
	centered_glow (g_shalo[g_cur - 1], LX, LY);
	centered (g_sb[(g_cur - 1) << 2], LX, LY);
}

static void draw_flying (void)
{
	if (g_fly)
		centered (g_sb[(g_cur - 1) << 2], g_bx >> 16, g_by >> 16);
}

static void draw_title (void)
{
	for (long i = 0; i < NCOLOR; i++)		/* sept bulles qui dansent */
		centered (g_sb[i << 2], i * 44 + GF_W / 2 - 132, (long) (ga_sin8 ((g_tick << 2) + (unsigned long) i * 36) >> 4) + 52);
	ga_show (g_tx[TX_LOGO], GF_W / 2, 92);
	ga_show (g_tx[TX_SUB], GF_W / 2, 194);
	if (ga_sin8 (g_tick << 1) >= 40)
		ga_show (g_tx[TX_PROMPT], GF_W / 2, 234);
	ga_show (g_tx[TX_RECORD], GF_W / 2 - 40, 276);
	ga_num (g_best, GF_W / 2 + 8, 274, g_dbig, 4);
	ga_show (g_tx[TX_HELP1], GF_W / 2, 332);
	ga_show (g_tx[TX_HELP2], GF_W / 2, 354);
	ga_show (g_tx[TX_HELP3], GF_W / 2, 376);
}

/* titre TX_*, suite TX_*, 1 : le score (et le record), 2 : le bonus */
static void box (int title, int sub, int kind)
{
	ga_panel (150, 140, 340, 190, 18, 0x00283C64, 0x00121A30, 252, 0x0090A8D8);
	ga_show (g_tx[title], GF_W / 2, 154);
	if (kind == 1)
	{
		ga_num (g_score, GF_W / 2, 200, g_dbig, 5);
		if (g_newbest)
			ga_show (g_tx[TX_NEWBEST], GF_W / 2, 236);
	}
	else
	{
		ga_show (g_tx[TX_BONUS], GF_W / 2, 198);
		ga_num (g_bonus, GF_W / 2, 224, g_dgain, 5);
	}
	ga_show (g_tx[sub], GF_W / 2, 288);
}

static void draw (void)
{
	if (g_ldirty)
		build_layer ();
	gf_copy (gf_frame, gf_layer, GF_PITCH * GF_H);
	ga_orbs (g_sorb[0], g_sorb[1], g_tick);
	deco_draw ();
	if (g_state == ST_TITLE)
	{
		draw_title ();
		return;
	}
	draw_hud ();
	long shk = 0;					/* un tir avant la descente : le plafond tremble */
	if (g_state == ST_PLAY && g_every - g_shots == 1)
		shk = (long) (((g_tick >> 2) & 1) << 1) - 1;
	g_shk = shk;
	draw_ceiling ();
	draw_grid ();
	if (g_state == ST_PLAY && !g_fly)
		draw_aim ();
	draw_launcher ();
	draw_flying ();
	fx_draw ();
	ga_parts_draw ();
	if (g_flt && g_fltv)				/* le gain qui monte */
		ga_num ((unsigned long) g_fltv, g_fltx, g_flty - 40 + (long) ((unsigned long) g_flt >> 1), g_dgain, 5);
	if (g_state == ST_OVER)				/* (la pause : gamepause) */
		box (TX_OVER, TX_AGAIN, 1);
	else if (g_state == ST_CLEAR)
	{
		if (g_timer >= 30)
			box (TX_CLEAR, TX_CONT, 2);
	}
	else if (g_state == ST_PLAY && g_banner >= 12)
	{						/* le numero du niveau, au debut */
		const gf_sprite_t *s = g_tx[TX_LEVEL];
		ga_show (s, GF_W / 2 - 16, 206);
		ga_num ((unsigned long) g_level, GF_W / 2 - 10 + (long) (s[0] >> 1), 211, g_dgain, 4);
	}
}

int main (void)
{
	if (gf_open ("Bubbles") < 0)
		return 0;
	ui_menu ((long) s_menus);
	if (!ga_init (2 * 1024 * 1024))
		return 0;
	gs_open ();
	g_best = gf_load_best (P_BEST);
	gen_sprites ();
	deco_init ();
	gp_init (game_pause, game_resume, new_game, GF_W - 34, 6, on_pad);
	g_ang = 180;
	set_state (ST_TITLE);
	g_ldirty = 1;
	gf_run (on_event, step, draw);
	gs_close ();
	return 0;
}
