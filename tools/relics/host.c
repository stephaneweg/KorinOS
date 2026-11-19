/*
 * host.c -- Relics sur l'hote (Linux), sans Korin : le meme main.c, UIKit et AudioKit en bouchons (mkstubs.py), le
 * texte en pavés. Sert a deux choses (tools/relics/run.sh) :
 *
 *   host check [n]              chaque niveau (ou le n-ieme) parcouru par la vraie physique de l'explorateur, en
 *                               largeur, depuis le depart : la sortie est-elle atteinte, quels objets ne le sont pas ;
 *                               les portes et les murs fissures sont comptes ouverts (les cles et la dynamite a part)
 *   host shot n out.ppm [script]  une image du niveau n apres un script d'entrees (R30 = droite 30 images ; L U D J F
 *                               X = gauche, haut, bas, saut, tir, dynamite ; W = rien ; lettres collees : ensemble,
 *                               « RJ12 ») ; n = -1 : l'ecran titre
 */
#define RELICS_NO_MAIN
#include "../../src/apps/relics/main.c"
#include <time.h>

/* ---- les bouchons qui comptent -------------------------------------------------------------------------------- */
static uint32_t *h_canvas, *h_surf, *h_target;
static long h_cw, h_ch;

int uikit_kit_open (void) { return 1; }
int audiokit_kit_open (void) { return 0; }
long ui_init (long v) { memset ((void *) v, 0, sizeof (struct ui_vars)); return 1; }
long ui_screen (long p) { long *s = (long *) p; s[0] = 1280; s[1] = 768; return 0; }
long ui_window_ex (long x, long y, long w, long h, long t, long f)
{
	(void) x; (void) y; (void) t; (void) f;
	ui.w = w; ui.h = h; ui.win = 1;
	ui.surf = (uint32_t *) calloc ((size_t) (w * h), 4);
	return 1;
}
long ui_canvas (long x, long y, long w, long h) { (void) x; (void) y; h_cw = w; h_ch = h; h_canvas = (uint32_t *) calloc ((size_t) (w * h), 4); return 7; }
long ui_canvas_pixels (long cv) { (void) cv; return (long) h_canvas; }
long ui_canvas_begin (long cv) { (void) cv; h_target = h_canvas; return 0; }
long ui_canvas_end (void) { h_target = 0; return 0; }
long ui_surface (long x, long y, long w, long h, long f) { (void) x; (void) y; (void) f; h_surf = (uint32_t *) calloc ((size_t) (w * h), 4); return 9; }
long ui_surface_pixels (long sf) { (void) sf; uikit__ret[1] = ui.w * 4; uikit__ret[2] = ui.w; uikit__ret[3] = ui.h; return (long) h_surf; }
long ui_tokens (void) { static unsigned t[64]; t[TJ_ACCENT / 4] = 0x3C8CE4; return (long) t; }
long ui_tr (long s) { return s; }
/* le texte : FreeType et les polices de Korin (Inter, comme UIKit), pour des images fideles */
#include <ft2build.h>
#include FT_FREETYPE_H
static FT_Library h_ft;
static FT_Face h_face[2];
static FT_Face face_of (long f)
{
	if (!h_ft)
	{
		const char *dir = getenv ("RELICS_FONTS") ? getenv ("RELICS_FONTS") : "sdcard/fonts";
		char p[512];
		FT_Init_FreeType (&h_ft);
		snprintf (p, sizeof p, "%s/Inter-Regular.ttf", dir);
		if (FT_New_Face (h_ft, p, 0, &h_face[0])) fprintf (stderr, "police absente : %s\n", p);
		snprintf (p, sizeof p, "%s/Inter-SemiBold.ttf", dir);
		if (FT_New_Face (h_ft, p, 0, &h_face[1])) h_face[1] = h_face[0];
	}
	return h_face[f == (long) ui.font_b ? 1 : 0];
}
static unsigned utf8_next (const unsigned char **ps)
{
	const unsigned char *p = *ps;
	unsigned c = *p++;
	if (c >= 0xF0) { c = (c & 7) << 18 | (p[0] & 63u) << 12 | (p[1] & 63u) << 6 | (p[2] & 63u); p += 3; }
	else if (c >= 0xE0) { c = (c & 15) << 12 | (p[0] & 63u) << 6 | (p[1] & 63u); p += 2; }
	else if (c >= 0xC0) { c = (c & 31) << 6 | (p[0] & 63u); p += 1; }
	*ps = p;
	return c;
}
/* texte -> sa largeur ; draw : dessine dans la cible, la base a (x, base) */
static long h_text (const char *s, long x, long base, long c, long px, long f, int draw)
{
	FT_Face face = face_of (f);
	if (!face) return 0;
	FT_Set_Pixel_Sizes (face, 0, (FT_UInt) px);
	long pen = 0;
	for (const unsigned char *p = (const unsigned char *) s; *p; )
	{
		unsigned ch = utf8_next (&p);
		if (FT_Load_Char (face, ch, draw ? FT_LOAD_RENDER : FT_LOAD_DEFAULT)) continue;
		FT_GlyphSlot g = face->glyph;
		if (draw && h_target)
		{
			FT_Bitmap *bm = &g->bitmap;
			long gx = x + pen + g->bitmap_left, gy = base - g->bitmap_top;
			for (unsigned r = 0; r < bm->rows; r++)
				for (unsigned q = 0; q < bm->width; q++)
				{
					long X = gx + q, Y = gy + r;
					unsigned a = bm->buffer[r * bm->pitch + q];
					if (!a || X < 0 || Y < 0 || X >= h_cw || Y >= h_ch) continue;
					uint32_t *d = h_target + Y * h_cw + X;
					*d = ga_mix (*d, (uint32_t) c, a + (a >> 7));
				}
		}
		pen += g->advance.x >> 6;
	}
	return pen;
}
long ui_text_width (long s, long px, long f) { return h_text ((const char *) s, 0, 0, 0, px, f, 0); }
long ui_fill (long x, long y, long w, long h, long c)
{
	if (!h_target) return 0;
	for (long j = y; j < y + h; j++)
		for (long i = x; i < x + w; i++)
			if (i >= 0 && j >= 0 && i < h_cw && j < h_ch) h_target[j * h_cw + i] = (uint32_t) c;
	return 0;
}
long ui_text (long s, long x, long base, long c, long px, long f, long fl)
{
	(void) fl;
	h_text ((const char *) s, x, base, c, px, f, 1);
	return 0;
}

/* ---- l'image -------------------------------------------------------------------------------------------- */
static void write_ppm (const char *path)
{
	FILE *f = fopen (path, "wb");
	if (!f) { perror (path); return; }
	fprintf (f, "P6\n%d %d\n255\n", GF_W, GF_H);
	for (int i = 0; i < GF_W * GF_H; i++)
	{
		uint32_t v = gf_frame[i];
		unsigned char b[3] = { (unsigned char) (v >> 16), (unsigned char) (v >> 8), (unsigned char) v };
		fwrite (b, 1, 3, f);
	}
	fclose (f);
}

static void init_all (void)
{
	if (getenv ("RELICS_SPRITES")) g_sprdir = getenv ("RELICS_SPRITES");
	if (getenv ("RELICS_TILES")) g_tildir = getenv ("RELICS_TILES");
	gf_open ("Relics");
	gs_open ();
	ga_init (BANK);
	g_bank0 = ga_bp;
	gen_sprites ();
	use_tiles ();
	if (getenv ("RELICS_MODE")) g_mode = atoi (getenv ("RELICS_MODE"));	/* 0 normal, 1 classique, 2 hardcore */
	if (getenv ("RELICS_BANK")) fprintf (stderr, "reserve des images : %lu Mo pris, %lu Mo libres\n", (unsigned long) (ga_bp - g_bank0) >> 20, bank_left () >> 20);
	gp_init (game_pause, game_resume, game_new, GF_W - 32, 0, on_pad);
	ui_canvas_begin (gf_cv);
	gen_texts ();
	ui_canvas_end ();
	g_unlocked = NLEVELS - 1;
	goto_title ();
}

/* les entrees du script, pas a pas */
static int h_trace;
static void run_script (const char *s)
{
	while (*s)
	{
		while (*s == ' ') s++;
		unsigned keys = 0;
		memset (gf_held, 0, sizeof gf_held);
		while (*s && (*s < '0' || *s > '9') && *s != ' ')
		{
			switch (*s)
			{
			case 'L': gf_held[0x50] = 1; break;
			case 'R': gf_held[0x4F] = 1; break;
			case 'U': gf_held[0x52] = 1; break;
			case 'D': gf_held[0x51] = 1; break;
			case 'J': gf_held[0x2C] = 1; break;
			case 'F': gf_held[0x1B] = 1; break;
			case 'X': gf_held[0x07] = 1; break;
			case 'W': break;
			}
			keys++;
			s++;
		}
		int n = atoi (s);
		while (*s >= '0' && *s <= '9') s++;
		if (!n) n = 1;
		for (int i = 0; i < n; i++)
		{
			step ();
			if (h_trace && g_tick % 8 == 0)
				printf ("t %4u etat %d x %7.1f y %6.1f vx %5.2f vy %5.2f sol %d mort %d\n", (unsigned) g_tick, g_state, g_h.x, g_h.y, g_h.vx, g_h.vy, g_h.ground, g_h.dead);
		}
		(void) keys;
	}
	memset (gf_held, 0, sizeof gf_held);
}

/* ---- le parcours en largeur ------------------------------------------------------------------------------ */
struct node { struct hero h; };
#define HSIZE		(1 << 20)
static uint64_t *h_keys;
static struct node *h_q;
static int h_qn, h_qh;
static unsigned char h_got[NE];
static int h_exit;

static uint64_t key_of (const struct hero *h)
{
	return ((uint64_t) (uint32_t) (int) floorf (h->x / 2 + 0.5f) << 32) | ((uint64_t) (uint32_t) ((int) floorf (h->y + 0.5f) & 0x7FFFFFFF) << 1) | (uint64_t) (h->climb != 0);
}
static int visit (const struct hero *h)
{
	uint64_t k = key_of (h) + 1;
	uint64_t i = (k * 0x9E3779B97F4A7C15ull) >> 44;
	while (h_keys[i])
	{
		if (h_keys[i] == k) return 0;
		i = (i + 1) & (HSIZE - 1);
	}
	h_keys[i] = k;
	return 1;
}

/* le corps touche-t-il la sortie, des objets ? */
static void touch (void)
{
	for (int i = 0; i < g_ne; i++)
	{
		struct ent *e = &g_e[i];
		if (e->type == E_EXIT && ov (g_h.x + 4, g_h.y, 4, H_H, e->x + 10, e->y + 14, 16, 30)) h_exit = 1;
		if ((e->type == E_ITEM || e->type == E_CHECK) && ov (g_h.x + 2, g_h.y + 3, H_W - 4, H_H - 4, e->x, e->y, e->w, e->h)) h_got[i] = 1;
	}
}

struct seg { unsigned in; int n; };
/* une action : des segments d'entree, puis rien jusqu'a l'arret -> 1 si un etat stable et vivant */
static int act (const struct hero *from, const struct seg *sg, int ns, struct hero *to)
{
	g_h = *from;
	g_in = 0;
	g_simdead = 0;
	int frames = 0;
	for (int k = 0; k <= ns; k++)
	{
		unsigned in = k < ns ? sg[k].in : 0;
		int n = k < ns ? sg[k].n : 200;
		for (int i = 0; i < n; i++)
		{
			g_inprev = g_in;
			g_in = in;
			g_edge = in & ~g_inprev;
			hero_step ();
			touch ();
			frames++;
			if (g_simdead || g_h.y > (float) g_ih) return 0;
			if (k == ns && ((g_h.ground && fabsf (g_h.vx) < 0.01f) || (g_h.climb && g_h.vy == 0))) { *to = g_h; return 1; }
		}
	}
	return 0;
}

static int check_level (int n, int verbose)
{
	start_level (n);
	set_state (GS_PLAY);
	g_sim = 1;
	/* les portes et les murs fissures : comptes ouverts */
	int doors = 0, keys = 0, breaks = 0;
	for (int ty = 0; ty < g_mh; ty++)
		for (int tx = 0; tx < g_mw; tx++)
			if (g_map[ty][tx] == T_DOOR && tile_at (tx, ty - 1) != T_DOOR) doors++;
	for (int ty = 0; ty < g_mh; ty++)
		for (int tx = 0; tx < g_mw; tx++)
		{
			int t = g_map[ty][tx];
			if (t == T_DOOR) g_map[ty][tx] = T_BACK;
			if (t == T_BREAK) { breaks++; g_map[ty][tx] = T_BACK; }
		}
	for (int i = 0; i < g_ne; i++) if (g_e[i].type == E_ITEM && g_e[i].kind == IT_KEY) keys++;
	if (!h_keys) { h_keys = (uint64_t *) calloc (HSIZE, 8); h_q = (struct node *) malloc (sizeof (struct node) * 400000); }
	memset (h_keys, 0, HSIZE * 8);
	memset (h_got, 0, sizeof h_got);
	h_exit = 0;
	h_qn = h_qh = 0;
	/* le depart : laisse tomber au sol */
	struct hero s;
	struct seg none = { 0, 1 };
	if (!act (&g_h, &none, 1, &s)) { printf ("niveau %d : le depart n'est pas stable\n", n + 1); return 0; }
	visit (&s);
	h_q[h_qn++].h = s;
	enum { A_ = 0 };
	static const unsigned DIRS[3] = { 0, IN_L, IN_R };
	while (h_qh < h_qn && h_qn < 399000)
	{
		struct hero cur = h_q[h_qh++].h, nx;
		struct seg sg[3];
		int na = 0;
		struct { struct seg s[3]; int n; } A[40];
#define ADD1(a, an) do { A[na].s[0].in = (a); A[na].s[0].n = (an); A[na].n = 1; na++; } while (0)
#define ADD2(a, an, b, bn) do { A[na].s[0].in = (a); A[na].s[0].n = (an); A[na].s[1].in = (b); A[na].s[1].n = (bn); A[na].n = 2; na++; } while (0)
		ADD1 (IN_L, 6);
		ADD1 (IN_R, 6);
		ADD1 (IN_L, 2);
		ADD1 (IN_R, 2);
		ADD1 (IN_U, 12);
		ADD1 (IN_D, 12);
		ADD1 (IN_U, 4);
		ADD1 (IN_D, 4);
		ADD1 (IN_D | IN_JUMP, 2);
		for (int d = 0; d < 3; d++)
		{
			ADD1 (IN_JUMP | DIRS[d], 40);
			ADD1 (IN_JUMP | IN_U | DIRS[d], 40);
			if (d) ADD1 (IN_JUMP | DIRS[d], 4);
			if (d) ADD2 (IN_JUMP, 10, IN_JUMP | DIRS[d], 40);
			if (d) ADD2 (IN_JUMP, 18, IN_JUMP | DIRS[d], 40);
			if (d) ADD2 (IN_JUMP | DIRS[d], 10, IN_JUMP, 40);
			if (d) ADD2 (IN_JUMP | DIRS[d], 20, IN_JUMP, 40);
			if (d) ADD2 (DIRS[d], 5, IN_JUMP | DIRS[d], 40);		/* elan, puis saut */
			if (d) ADD2 (DIRS[d], 5, IN_JUMP | IN_U | DIRS[d], 40);
		}
		(void) sg;
		for (int a = 0; a < na; a++)
			if (act (&cur, A[a].s, A[a].n, &nx) && visit (&nx))
				h_q[h_qn++].h = nx;
	}
	g_sim = 0;
	if (getenv ("RELICS_DEBUG"))
	{
		float mx = 0, my = 0;
		for (int i = 0; i < h_qn; i++) if (h_q[i].h.x > mx && h_q[i].h.y < (getenv ("RELICS_Y") ? atof (getenv ("RELICS_Y")) : 1e9f)) { mx = h_q[i].h.x; my = h_q[i].h.y; }
		printf ("    le plus a droite : %.1f, %.1f (case %d, %d)\n", mx, my, (int) (mx / TS), (int) ((my + H_H - 1) / TS));
	}
	int miss = 0, items = 0, treas = 0, ammo = 0, dyn = 0;
	for (int i = 0; i < g_ne; i++)
	{
		struct ent *e = &g_e[i];
		if (e->type == E_ITEM)
		{
			items++;
			if (e->kind <= IT_RELIC) treas++;
			if (e->kind == IT_AMMO) ammo += 6;
			if (e->kind == IT_DYN) dyn += 3;
		}
		if ((e->type == E_ITEM || e->type == E_CHECK) && !h_got[i])
		{
			miss++;
			if (verbose) printf ("    %s en %d,%d hors d'atteinte\n", e->type == E_CHECK ? "point de passage" : "objet", e->tx, e->ty);
		}
	}
	printf ("niveau %2d %-32s %3dx%-2d  sortie %s  etats %6d  objets %d (tresors %d) manques %d  portes %d cles %d  murs %d  +balles %d +dyn %d\n",
		n + 1, levels[n].name, g_mw, g_mh, h_exit ? "OUI" : "NON", h_qn, items, treas, miss, doors, keys, breaks, ammo, dyn);
	return h_exit && keys >= doors;
}

int main (int argc, char **argv)
{
	init_all ();
	if (argc >= 2 && !strcmp (argv[1], "check"))
	{
		int ok = 1;
		for (int n = 0; n < NLEVELS; n++)
			if (argc < 3 || atoi (argv[2]) == n + 1) ok &= check_level (n, 1);
		return ok ? 0 : 1;
	}
	if (argc >= 4 && !strcmp (argv[1], "shot"))
	{
		int n = atoi (argv[2]);
		if (n >= 0)
		{
			new_game (n);
			set_state (GS_PLAY);
			if (getenv ("RELICS_AT"))
			{				/* RELICS_AT=tx,ty : l'explorateur pose la */
				int tx = 0, ty = 0;
				sscanf (getenv ("RELICS_AT"), "%d,%d", &tx, &ty);
				g_h.x = (float) (tx * TS + 4);
				g_h.y = (float) ((ty + 1) * TS - H_H);
				camera_snap ();
			}
		}
		if (argc >= 5) run_script (argv[4]);
		else for (int i = 0; i < 30; i++) step ();
		const char *st = getenv ("RELICS_STATE");	/* intro, clear, over, win, toast, pause : l'ecran a voir */
		if (st)
		{
			g_treas = 5; g_bonus = 3550; g_score = 12340; g_timer = 64;
			if (!strcmp (st, "intro")) { g_state = GS_INTRO; g_timer = 10; }
			else if (!strcmp (st, "clear")) g_state = GS_CLEAR;
			else if (!strcmp (st, "over")) g_state = GS_OVER;
			else if (!strcmp (st, "win")) g_state = GS_WIN;
			else if (!strncmp (st, "end", 3)) { g_state = GS_END; g_timer = atoi (st + 3); g_tick = (unsigned long) g_timer; }
			else if (!strcmp (st, "toast")) { toast (TX_CHECK); g_toast_t = 60; }
			else if (!strcmp (st, "pause")) { gp_open (); }
		}
		draw ();
		gp_draw ();
		write_ppm (argv[3]);
		return 0;
	}
	if (argc >= 6 && !strcmp (argv[1], "traj"))
	{						/* traj n x y script : la trajectoire, image par image */
		start_level (atoi (argv[2]));
		set_state (GS_PLAY);
		g_sim = 1;
		g_h.x = (float) atof (argv[3]);
		g_h.y = (float) atof (argv[4]);
		g_h.ground = 1;
		const char *sc = argv[5];
		g_in = 0;
		while (*sc)
		{
			unsigned in = 0;
			for (; *sc && (*sc < '0' || *sc > '9'); sc++)
				in |= *sc == 'L' ? IN_L : *sc == 'R' ? IN_R : *sc == 'U' ? IN_U : *sc == 'D' ? IN_D : *sc == 'J' ? IN_JUMP : 0;
			int n = atoi (sc);
			while (*sc >= '0' && *sc <= '9') sc++;
			for (int i = 0; i < n; i++)
			{
				g_inprev = g_in; g_in = in; g_edge = in & ~g_inprev;
				g_simdead = 0;
				hero_step ();
				printf ("x %6.1f y %6.1f vx %5.2f vy %5.2f g %d c %d %s\n", g_h.x, g_h.y, g_h.vx, g_h.vy, g_h.ground, g_h.climb, g_simdead ? "MORT" : "");
			}
		}
		return 0;
	}
	if (argc >= 6 && !strcmp (argv[1], "chase"))
	{						/* chase n tx ty script : une fuite devant un rocher, avec tout le jeu */
		new_game (atoi (argv[2]));
		set_state (GS_PLAY);
		g_h.x = (float) (atoi (argv[3]) * TS + 4);
		g_h.y = (float) ((atoi (argv[4]) + 1) * TS - H_H);
		camera_snap ();
		int lives = g_lives;
		h_trace = getenv ("RELICS_TRACE") != 0;
		run_script (argv[5]);
		for (int i = 0; i < g_ne; i++)
			if (g_e[i].type == E_BOULDER)
				printf ("rocher %d : %s x %.0f (depart %.0f)\n", i, g_e[i].on ? (g_e[i].st ? "roule" : "au repos") : "ecrase", g_e[i].x, g_e[i].x0);
		for (int i = 0; i < g_ne; i++)
			if ((g_e[i].type == E_WALKER || g_e[i].type == E_SHOOTER) && fabsf (g_e[i].x - g_h.x) < 12 * TS)
				printf ("ennemi %d : %s, %d balle(s) encore\n", i, g_e[i].on ? "debout" : "abattu", g_e[i].hp);
		printf ("explorateur x %.0f (case %d) : %s\n", g_h.x, (int) (g_h.x / TS), g_lives < lives || g_h.dead ? "MORT" : "vivant");
		return 0;
	}
	if (argc >= 5 && !strcmp (argv[1], "deep"))
	{
		start_level (atoi (argv[2]));
		int tx = atoi (argv[3]), ty = atoi (argv[4]);
		printf ("tile %d mask %d deep %d fg %p sub82 %p\n", g_map[ty][tx], edge_mask (tx, ty), deep_of (tx, ty), (void *) g_tfg[ty][tx], (void *) g_tc[g_world][TK_BRICK][82]);
		return 0;
	}
	if (argc >= 2 && !strcmp (argv[1], "fuzz"))
	{						/* chaque niveau joue au hasard : rien ne doit planter */
		unsigned r = 12345;
		for (int n = 0; n < NLEVELS; n++)
		{
			new_game (n);
			g_lives = 99;
			for (int i = 0; i < 5000; i++)
			{
				if (i % 20 == 0)
				{
					r = r * 1103515245u + 12345u;
					memset (gf_held, 0, sizeof gf_held);
					gf_held[0x4F] = (r >> 8) % 3 != 0;
					gf_held[0x50] = (r >> 10) % 5 == 0;
					gf_held[0x52] = (r >> 12) % 4 == 0;
					gf_held[0x51] = (r >> 14) % 6 == 0;
					gf_held[0x2C] = (r >> 16) % 3 == 0;
					gf_held[0x1B] = (r >> 18) % 4 == 0;
					gf_held[0x07] = (r >> 20) % 15 == 0;
				}
				step ();
				if (i % 7 == 0) draw ();
				if (g_state == GS_CLEAR || g_state == GS_OVER || g_state == GS_WIN) break;
			}
			printf ("niveau %d : etat %d, x %.0f y %.0f, score %lu, vies %d\n", n + 1, g_state, g_h.x, g_h.y, g_score, g_lives);
		}
		return 0;
	}
	if (argc >= 2 && !strcmp (argv[1], "camtest"))
	{						/* camtest : la marche a droite ; le point de l'explorateur a l'ecran et le pas du decor */
		new_game (0);
		set_state (GS_PLAY);
		memset (gf_held, 0, sizeof gf_held);
		gf_held[0x4F] = 1;
		int last = 0;
		for (int i = 0; i < 160; i++)
		{
			step ();
			int cx = (int) floorf (g_camx * ZK + 0.001f), hx = (int) floorf ((g_h.x + H_W / 2) * ZK);
			if (i > 100) printf ("%d:%d/%d ", i, hx - cx, cx - last);
			last = cx;
		}
		printf ("\n");
		return 0;
	}
	fprintf (stderr, "host check [n] | host shot n out.ppm [script]\n");
	return 2;
}
