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
static long h_chars (const char *s) { long n = 0; for (; *s; s++) if (((unsigned char) *s & 0xC0) != 0x80) n++; return n; }
long ui_text_width (long s, long px, long f) { (void) f; return h_chars ((const char *) s) * px * 56 / 100; }
long ui_fill (long x, long y, long w, long h, long c)
{
	if (!h_target) return 0;
	for (long j = y; j < y + h; j++)
		for (long i = x; i < x + w; i++)
			if (i >= 0 && j >= 0 && i < h_cw && j < h_ch) h_target[j * h_cw + i] = (uint32_t) c;
	return 0;
}
/* le texte : un pave par lettre, a la hauteur des capitales */
long ui_text (long s, long x, long base, long c, long px, long f, long fl)
{
	(void) f; (void) fl;
	if (!h_target) return 0;
	const char *p = (const char *) s;
	long cw = px * 56 / 100;
	for (; *p; p++)
	{
		if (((unsigned char) *p & 0xC0) == 0x80) continue;
		if (*p != ' ')
			ui_fill (x + 1, base - px * 7 / 10, cw - 2 > 1 ? cw - 2 : 1, px * 7 / 10, c);
		x += cw;
	}
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
	gf_open ("Relics");
	gs_open ();
	ga_init (14u << 20);
	gen_sprites ();
	gp_init (game_pause, game_resume, game_new, GF_W - 32, 0, on_pad);
	ui_canvas_begin (gf_cv);
	gen_texts ();
	ui_canvas_end ();
	g_unlocked = NLEVELS - 1;
	goto_title ();
}

/* les entrees du script, pas a pas */
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
		for (int i = 0; i < n; i++) step ();
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
	fprintf (stderr, "host check [n] | host shot n out.ppm [script]\n");
	return 2;
}
