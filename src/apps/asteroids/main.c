// main.c -- Asteroids : le classique de l'arcade, en dessin lissé et ombré (Korin OS, UIKit, AudioKit)
//
// Le monde fait WORLD_H unités de haut, et la largeur de la fenêtre à la même échelle : tout grandit avec la
// fenêtre. Les bords se rejoignent (ce qui sort d'un côté rentre de l'autre).
// Clavier : Gauche / Droite tournent, Haut accélère, Espace tire, Bas saute dans l'hyperespace.
// Souris : le vaisseau se tourne vers le pointeur et le rejoint, clic gauche tire, clic droit : hyperespace.
// P ou Échap : le menu de pause (Reprendre, Recommencer, Quitter) ; S : le son ; F2 : nouvelle partie.
// Record : HOME:/asteroids/best.txt (le dossier personnel, comme les autres jeux).
// Écrit par Claude sur Korin (le 18/11/2026), intégré à l'arbre ensuite.
//
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <uikit.h>
#include "gfx.h"
#include "sound.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))	// texte traduit (la clé est le texte français)

#ifndef KEY_SPACE
#define KEY_SPACE 0x2C
#endif
#define KEY_F2		0x3B
#define KEY_F11		0x44

#define HOMEDIR		"HOME:/asteroids"
#define BESTFILE	HOMEDIR "/best.txt"

#define WORLD_H		720.0f
#define PI		3.14159265f
#define TAU		6.2831853f

#define MAX_ROCKS	72
#define MAX_SHOTS	32
#define MAX_PARTS	1800
#define MAX_RINGS	16
#define MAX_POPS	24
#define MAX_STARS	240
#define ROCK_MAXV	14

#define SHIP_S		13.0f		// échelle du dessin du vaisseau (unités du monde)
#define SHIP_R		14.0f		// rayon de collision du vaisseau
#define SHOT_SPEED	720.0f
#define SHOT_LIFE	0.85f
#define BG_DARK		0x070914	// le noir de l'espace (fondus de la fumée et des textes)

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

// ---- outils ------------------------------------------------------------------------------------------------------

static unsigned now_ms (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) ((unsigned long long) t.tv_sec * 1000u + (unsigned long long) t.tv_nsec / 1000000u);
}

static unsigned g_seed = 0x1234567u;
static float frand (void)
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5;
	return (float) (g_seed & 0xFFFFFF) / 16777216.0f;
}
static float frange (float a, float b) { return a + (b - a) * frand (); }
static float clampf (float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float angle_wrap (float a)
{
	while (a > PI) a -= TAU;
	while (a < -PI) a += TAU;
	return a;
}

// ---- l'écran et le monde -----------------------------------------------------------------------------------------

static int g_W, g_H;			// la fenêtre, en pixels
static float g_px = 1;			// pixels par unité du monde
static float g_ww, g_wh = WORLD_H;	// le monde
static float g_shx, g_shy, g_shake;	// tremblement : décalage en pixels, amplitude
static float g_time;			// secondes de jeu
static float g_txt = 1;			// échelle des textes

#define PX(x)	((x) * g_px + g_shx)
#define PY(y)	((y) * g_px + g_shy)

// écarts les plus courts dans un monde qui se referme sur lui-même
static float wdx (float d) { return d > g_ww * 0.5f ? d - g_ww : d < -g_ww * 0.5f ? d + g_ww : d; }
static float wdy (float d) { return d > g_wh * 0.5f ? d - g_wh : d < -g_wh * 0.5f ? d + g_wh : d; }
static void wrap_pos (float *x, float *y)
{
	if (*x < 0) *x += g_ww; else if (*x >= g_ww) *x -= g_ww;
	if (*y < 0) *y += g_wh; else if (*y >= g_wh) *y -= g_wh;
}

// les places où dessiner un objet de rayon r : la sienne, et ses reflets s'il touche un bord
static int wrap_spots (float x, float y, float r, float *ox, float *oy)
{
	float xs[2] = { x, x }, ys[2] = { y, y };
	int nx = 1, ny = 1, n = 0;
	if (x < r) xs[nx++] = x + g_ww; else if (x > g_ww - r) xs[nx++] = x - g_ww;
	if (y < r) ys[ny++] = y + g_wh; else if (y > g_wh - r) ys[ny++] = y - g_wh;
	for (int i = 0; i < nx; i++)
		for (int j = 0; j < ny; j++) { ox[n] = xs[i]; oy[n] = ys[j]; n++; }
	return n;
}

// ---- le décor : nébuleuses, poussière d'étoiles, une planète (calculé une fois par taille) -------------------------

static uint32_t *g_bg;
static int g_bg_w, g_bg_h;
static unsigned g_bg_seed = 7;

static unsigned hash2 (int x, int y)
{
	unsigned h = (unsigned) x * 374761393u + (unsigned) y * 668265263u + g_bg_seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}
static float lattice (int x, int y) { return (float) (hash2 (x, y) & 0xFFFF) * (1.0f / 65535.0f); }
static float vnoise (float x, float y)
{
	int xi = (int) floorf (x), yi = (int) floorf (y);
	float fx = x - (float) xi, fy = y - (float) yi;
	fx = fx * fx * (3 - 2 * fx);
	fy = fy * fy * (3 - 2 * fy);
	float a = lattice (xi, yi), b = lattice (xi + 1, yi), c = lattice (xi, yi + 1), d = lattice (xi + 1, yi + 1);
	return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm (float x, float y)
{
	float s = 0, amp = 0.5f;
	for (int i = 0; i < 5; i++)
	{
		s += amp * vnoise (x, y);
		x = x * 2.03f + 17.1f;
		y = y * 2.03f + 9.7f;
		amp *= 0.5f;
	}
	return s;
}
static float smooth (float a, float b, float v)
{
	float t = clampf ((v - a) / (b - a), 0, 1);
	return t * t * (3 - 2 * t);
}

static void build_bg (void)
{
	int W = g_W, H = g_H;
	free (g_bg);
	g_bg = (uint32_t *) malloc ((size_t) W * (size_t) H * 4);
	g_bg_w = W; g_bg_h = H;
	if (!g_bg) return;

	// les nuages, sur une grille quatre fois plus lâche, puis lissés
	int lw = W / 4 + 2, lh = H / 4 + 2;
	float *low = (float *) malloc ((size_t) lw * (size_t) lh * 3 * sizeof (float));
	if (!low) { memset (g_bg, 0, (size_t) W * (size_t) H * 4); return; }
	float aspect = (float) W / (float) H;
	for (int cy = 0; cy < lh; cy++)
		for (int cx = 0; cx < lw; cx++)
		{
			float u = (float) (cx * 4) / (float) H, v = (float) (cy * 4) / (float) H;
			float r = 5 + 5 * v, g = 7 + 3 * v, b = 18 + 8 * v;
			float n1 = fbm (u * 2.2f, v * 2.2f), n2 = fbm (u * 3.1f + 5.2f, v * 3.1f + 1.3f);
			float n3 = fbm (u * 7.0f + 3.0f, v * 7.0f + 8.0f);
			float d1x = u - aspect * 0.27f, d1y = v - 0.32f, d2x = u - aspect * 0.72f, d2y = v - 0.7f;
			float m1 = expf (-(d1x * d1x + d1y * d1y) / 0.30f), m2 = expf (-(d2x * d2x + d2y * d2y) / 0.22f);
			float c1 = smooth (0.38f, 0.78f, n1) * m1, c2 = smooth (0.42f, 0.80f, n2) * m2;
			float detail = 0.55f + 0.9f * n3;
			float lane = 1 - 0.55f * smooth (0.52f, 0.68f, n3) * (c1 + c2);	// traînées sombres
			r += (100 * c1 + 18 * c2) * detail * 0.62f * lane;
			g += (32 * c1 + 78 * c2) * detail * 0.62f * lane;
			b += (150 * c1 + 125 * c2) * detail * 0.62f * lane;
			float vx = (float) (cx * 4) / (float) W - 0.5f, vy = (float) (cy * 4) / (float) H - 0.5f;
			float vig = 1 - 0.55f * (vx * vx + vy * vy) * 2;
			float *o = low + ((size_t) cy * (size_t) lw + (size_t) cx) * 3;
			o[0] = r * vig; o[1] = g * vig; o[2] = b * vig;
		}
	for (int y = 0; y < H; y++)
	{
		float gy = (float) y / 4.0f;
		int iy = (int) gy;
		float fy = gy - (float) iy;
		uint32_t *row = g_bg + (size_t) y * (size_t) W;
		for (int x = 0; x < W; x++)
		{
			float gx = (float) x / 4.0f;
			int ix = (int) gx;
			float fx = gx - (float) ix;
			const float *p00 = low + ((size_t) iy * (size_t) lw + (size_t) ix) * 3;
			const float *p10 = p00 + 3, *p01 = p00 + (size_t) lw * 3, *p11 = p01 + 3;
			float dither = (float) (hash2 (x, y) & 255) / 255.0f;
			int c[3];
			for (int k = 0; k < 3; k++)
			{
				float a = p00[k] + (p10[k] - p00[k]) * fx, b = p01[k] + (p11[k] - p01[k]) * fx;
				int v = (int) (a + (b - a) * fy + dither);
				c[k] = v < 0 ? 0 : v > 255 ? 255 : v;
			}
			row[x] = (unsigned) c[0] << 16 | (unsigned) c[1] << 8 | (unsigned) c[2];
		}
	}
	free (low);

	// une planète géante, en bas à droite, dans la pénombre
	{
		float R = (float) H * 0.30f, pcx = (float) W * 0.88f, pcy = (float) H * 0.93f;
		float lx = -0.55f, ly = -0.62f, lz = 0.56f;
		int x0 = (int) (pcx - R * 1.15f), x1 = (int) (pcx + R * 1.15f), y0 = (int) (pcy - R * 1.15f), y1 = (int) (pcy + R * 1.15f);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > W) x1 = W;
		if (y1 > H) y1 = H;
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++)
			{
				float dx = ((float) x + 0.5f - pcx) / R, dy = ((float) y + 0.5f - pcy) / R, d2 = dx * dx + dy * dy;
				uint32_t *p = g_bg + (size_t) y * (size_t) W + (size_t) x;
				if (d2 < 1)
				{
					float nz = sqrtf (1 - d2);
					float lam = dx * lx + dy * ly + nz * lz;
					float light = clampf (lam, 0, 1) * 0.92f + 0.03f;
					float t = dy * 7.5f + 1.4f * vnoise (dx * 2.5f + 20, dy * 6.0f + 4);
					float band = 0.5f + 0.5f * sinf (t * 2.6f);
					float storm = smooth (0.62f, 0.8f, vnoise (dx * 5 + 40, dy * 9 + 11));
					float r = 150 + 70 * band - 40 * storm, g = 92 + 62 * band - 20 * storm, b = 70 + 40 * band + 10 * storm;
					float rim = powf (1 - nz, 3.0f) * clampf (lam + 0.35f, 0, 1);
					float k = light * 0.5f;
					r = r * k + 50 * rim; g = g * k + 100 * rim; b = b * k + 220 * rim;
					float cov = clampf ((1 - sqrtf (d2)) * R, 0, 1);	// bord lissé
					unsigned col = (unsigned) clampf (r, 0, 255) << 16 | (unsigned) clampf (g, 0, 255) << 8 | (unsigned) clampf (b, 0, 255);
					*p = gfx_mix (*p, col, (int) (cov * 256));
				}
				else
				{
					float d = sqrtf (d2);
					if (d > 1.15f) continue;
					float a = 1 - (d - 1) / 0.15f;
					a *= a;
					float side = clampf (-(dx * lx + dy * ly) / d * 0.9f + 0.35f, 0, 1);
					float k = a * side * 0.55f;
					uint32_t s = *p;
					int r = (int) ((s >> 16) & 255) + (int) (40 * k), g = (int) ((s >> 8) & 255) + (int) (95 * k), b = (int) (s & 255) + (int) (230 * k);
					*p = (unsigned) (r > 255 ? 255 : r) << 16 | (unsigned) (g > 255 ? 255 : g) << 8 | (unsigned) (b > 255 ? 255 : b);
				}
			}
	}

	// la poussière d'étoiles, fixe
	int n = W * H / 650;
	unsigned save = g_seed;
	g_seed = 0xC0FFEEu + (unsigned) W * 31u + (unsigned) H;
	for (int i = 0; i < n; i++)
	{
		int x = (int) (frand () * (float) W), y = (int) (frand () * (float) H);
		float br = 18 + powf (frand (), 3.0f) * 150;
		float t = frand ();
		float r = br, g = br, b = br;
		if (t < 0.3f) { r *= 0.75f; g *= 0.88f; }
		else if (t < 0.45f) { b *= 0.7f; g *= 0.9f; }
		uint32_t *p = g_bg + (size_t) y * (size_t) W + (size_t) x;
		uint32_t s = *p;
		int R = (int) ((s >> 16) & 255) + (int) r, Gc = (int) ((s >> 8) & 255) + (int) g, B = (int) (s & 255) + (int) b;
		*p = (unsigned) (R > 255 ? 255 : R) << 16 | (unsigned) (Gc > 255 ? 255 : Gc) << 8 | (unsigned) (B > 255 ? 255 : B);
	}
	g_seed = save;
}

// ---- les objets ----------------------------------------------------------------------------------------------------

typedef struct
{
	int alive, size, nv, ncr;
	float x, y, vx, vy, ang, spin;
	float ux[ROCK_MAXV], uy[ROCK_MAXV];		// le contour, rayon 1 environ
	float crx[4], cry[4], crr[4];			// les cratères (rapportés au rayon)
	unsigned col;
} rock;

typedef struct { int alive, enemy; float x, y, vx, vy, life; } shot;

enum { P_SPARK, P_FLAME, P_SMOKE, P_DEBRIS, P_SHARD, P_FLASH };
typedef struct
{
	int kind;
	float x, y, vx, vy, life, max, size, ang, spin;
	unsigned col;
} part;

typedef struct { int alive; float x, y, r, vr, w, life, max; unsigned col; } ringfx;
typedef struct { int alive; float x, y, life; char text[16]; unsigned col; } popup;

static const float ROCK_R[4] = { 0, 15, 27, 50 };
static const int ROCK_PTS[4] = { 0, 100, 50, 20 };
static const unsigned ROCK_PAL[] = { 0x8a7d70, 0x7a8394, 0x977459, 0x737c6d, 0x8f8a84, 0x6f6a78 };

static rock g_rocks[MAX_ROCKS];
static shot g_shots[MAX_SHOTS];
static part g_parts[MAX_PARTS];
static int g_pnext;
static ringfx g_rings[MAX_RINGS];
static popup g_pops[MAX_POPS];

static struct
{
	int alive;
	float x, y, vx, vy, ang;
	float inv;			// invulnérable encore tant de secondes (bouclier)
	float respawn;			// attente avant de revenir
	float fire_cd, hyper_cd;
	float thrust;			// poussée montrée (lissée), 0..1
} ship;

static struct
{
	int alive, small;
	float x, y, vx, vy, fire_t, dir_t, t;
} ufo;

typedef struct { float x, y, depth, tw, ph; unsigned col; } star;
static star g_stars[MAX_STARS];
static int g_nstars;

// ---- l'état du jeu --------------------------------------------------------------------------------------------------

enum { ST_TITLE, ST_PLAY, ST_OVER };
static int g_state = ST_TITLE;
static int g_level, g_lives, g_score, g_best, g_next_life, g_new_best;
static float g_banner;			// le bandeau « Niveau n » encore tant de secondes
static float g_clear_t;			// le niveau vidé : la suite dans tant de secondes
static float g_ufo_t;			// le prochain ovni
static float g_beat_t; static int g_beat_alt;
static float g_over_t;			// temps passé sur l'écran de fin
static float g_end_t;			// plus de vie : l'écran de fin dans tant de secondes
static float g_flash; static unsigned g_flash_col;
static int g_level_units;		// la masse d'astéroïdes au début du niveau (cadence du battement)
static int g_sound = 1, g_sound_ok;
static int g_quit;

// la pause : son menu
enum { PM_RESUME, PM_RESTART, PM_QUIT, PM_N };
static int g_paused, g_psel, g_redraw = 1;
static int g_pm_x, g_pm_y, g_pm_w, g_pm_h, g_pm_step;	// la place des boutons (pour la souris)
static int g_pb_x, g_pb_y, g_pb_s;			// le bouton ‖

// les commandes
static int k_left, k_right, k_up, k_down, k_fire, k_fire_edge, k_hyper;
static int g_mouse_mode, m_left, m_fire_edge, m_x, m_y, m_seen;
static float m_moved;

// ---- record ----------------------------------------------------------------------------------------------------------

static void load_best (void)
{
	FILE *f = fopen (BESTFILE, "r");
	if (!f) return;
	if (fscanf (f, "%d", &g_best) != 1) g_best = 0;
	fclose (f);
}

static void save_best (void)
{
	mkdir (HOMEDIR, 0777);
	FILE *f = fopen (BESTFILE, "w");
	if (!f) return;
	fprintf (f, "%d\n", g_best);
	fclose (f);
}

// ---- les étoiles qui bougent (trois plans) ----------------------------------------------------------------------------

static void init_stars (void)
{
	g_nstars = (int) clampf (g_ww * g_wh / 4200.0f, 60, MAX_STARS);
	for (int i = 0; i < g_nstars; i++)
	{
		star *s = &g_stars[i];
		s->x = frand () * g_ww; s->y = frand () * g_wh;
		float l = frand ();
		s->depth = l < 0.55f ? 0.15f : l < 0.85f ? 0.35f : 0.7f;
		s->tw = frange (1.0f, 4.0f); s->ph = frand () * TAU;
		float t = frand ();
		s->col = t < 0.25f ? 0xBFD4FF : t < 0.4f ? 0xFFE4C0 : 0xFFFFFF;
	}
}

// ---- les particules et les effets ------------------------------------------------------------------------------------

static part *new_part (int kind, float x, float y, float vx, float vy, float life, float size, unsigned col)
{
	part *p = &g_parts[g_pnext];
	g_pnext = (g_pnext + 1) % MAX_PARTS;	// la plus ancienne cède sa place
	p->kind = kind; p->x = x; p->y = y; p->vx = vx; p->vy = vy;
	p->life = p->max = life; p->size = size; p->col = col;
	p->ang = frand () * TAU; p->spin = frange (-6, 6);
	return p;
}

static void spray (int kind, int n, float x, float y, float bvx, float bvy, float smin, float smax,
	float lmin, float lmax, float zmin, float zmax, unsigned col)
{
	for (int i = 0; i < n; i++)
	{
		float a = frand () * TAU, v = frange (smin, smax);
		new_part (kind, x, y, bvx + cosf (a) * v, bvy + sinf (a) * v, frange (lmin, lmax), frange (zmin, zmax), col);
	}
}

static void add_ring (float x, float y, float r0, float vr, float w, float life, unsigned col)
{
	for (int i = 0; i < MAX_RINGS; i++)
		if (!g_rings[i].alive)
		{
			ringfx *r = &g_rings[i];
			r->alive = 1; r->x = x; r->y = y; r->r = r0; r->vr = vr; r->w = w; r->life = r->max = life; r->col = col;
			return;
		}
}

static void add_popup (float x, float y, const char *text, unsigned col)
{
	for (int i = 0; i < MAX_POPS; i++)
		if (!g_pops[i].alive)
		{
			popup *p = &g_pops[i];
			p->alive = 1; p->x = x; p->y = y; p->life = 1.1f; p->col = col;
			snprintf (p->text, sizeof p->text, "%s", text);
			return;
		}
}

static void shake (float amount) { if (amount > g_shake) g_shake = amount; }
static void flash (unsigned col, float k) { if (k > g_flash) { g_flash = k; g_flash_col = col; } }

static void add_score (int pts, float x, float y)
{
	if (g_state != ST_PLAY) return;
	g_score += pts;
	char t[16];
	snprintf (t, sizeof t, "+%d", pts);
	add_popup (x, y, t, pts >= 200 ? 0xFFD060 : 0xE8F0FF);
	if (g_score >= g_next_life)
	{
		g_next_life += 10000;
		if (g_lives < 9) g_lives++;
		snd_play (SFX_LIFE);
		add_popup (ship.x, ship.y - 30, TR ("Vie +1"), 0x70FFA0);
		if (ship.alive) add_ring (ship.x, ship.y, 10, 260, 6, 0.6f, 0x50FF90);
	}
}

// ---- les astéroïdes ------------------------------------------------------------------------------------------------

static rock *rock_new (int size, float x, float y, float vx, float vy, unsigned col)
{
	for (int i = 0; i < MAX_ROCKS; i++)
		if (!g_rocks[i].alive)
		{
			rock *r = &g_rocks[i];
			memset (r, 0, sizeof *r);
			r->alive = 1; r->size = size; r->x = x; r->y = y; r->vx = vx; r->vy = vy;
			r->ang = frand () * TAU;
			r->spin = frange (-1.2f, 1.2f) * (size == 1 ? 1.8f : size == 2 ? 1.2f : 0.7f);
			r->nv = size == 1 ? 9 + (int) (frand () * 3) : 11 + (int) (frand () * 4);
			if (r->nv > ROCK_MAXV) r->nv = ROCK_MAXV;
			for (int k = 0; k < r->nv; k++)
			{
				float a = (float) k * TAU / (float) r->nv + frange (-0.18f, 0.18f);
				float rr = frange (0.80f, 1.05f);
				if (frand () < 0.18f) rr *= 0.82f;	// une entaille
				r->ux[k] = cosf (a) * rr; r->uy[k] = sinf (a) * rr;
			}
			r->ncr = size == 3 ? 3 + (int) (frand () * 2) : size == 2 ? 2 + (int) (frand () * 2) : 1;
			for (int k = 0; k < r->ncr; k++)
			{
				float a = frand () * TAU, d = frange (0.05f, 0.45f);
				r->crx[k] = cosf (a) * d; r->cry[k] = sinf (a) * d;
				r->crr[k] = frange (0.11f, 0.22f) * (size == 1 ? 1.4f : 1.0f);
			}
			r->col = col ? col : ROCK_PAL[(int) (frand () * (sizeof ROCK_PAL / sizeof ROCK_PAL[0]))];
			r->col = gfx_shade (r->col, 230 + (int) (frand () * 50));
			return r;
		}
	return 0;
}

static float rock_speed (int size)
{
	float lv = 1 + 0.07f * (float) (g_level > 0 ? g_level - 1 : 0);
	if (lv > 1.7f) lv = 1.7f;
	return (size == 3 ? frange (35, 70) : size == 2 ? frange (60, 110) : frange (90, 160)) * lv;
}

static void rock_destroy (int i, float ivx, float ivy, int by_player)
{
	rock *r = &g_rocks[i];
	if (!r->alive) return;
	r->alive = 0;
	float x = r->x, y = r->y, R = ROCK_R[r->size];
	int size = r->size;
	if (by_player) add_score (ROCK_PTS[size], x, y);
	snd_play (size == 3 ? SFX_BANG_L : size == 2 ? SFX_BANG_M : SFX_BANG_S);
	shake (size == 3 ? 7 : size == 2 ? 4 : 2);

	// des éclats de roche, de la poussière brûlante, de la fumée, une onde
	int nd = size == 3 ? 16 : size == 2 ? 10 : 6;
	for (int k = 0; k < nd; k++)
	{
		float a = frand () * TAU, v = frange (30, 150);
		part *p = new_part (P_DEBRIS, x + cosf (a) * R * 0.5f, y + sinf (a) * R * 0.5f,
			r->vx * 0.6f + cosf (a) * v, r->vy * 0.6f + sinf (a) * v, frange (0.8f, 1.8f),
			frange (2.0f, 4.5f) * (size == 3 ? 1.4f : 1.0f), gfx_shade (r->col, (int) frange (180, 300)));
		p->spin = frange (-9, 9);
	}
	spray (P_SPARK, size == 3 ? 34 : size == 2 ? 22 : 12, x, y, r->vx * 0.5f, r->vy * 0.5f, 40, 260, 0.25f, 0.8f, 2.0f, 4.0f, 0);
	spray (P_SMOKE, size == 3 ? 10 : size == 2 ? 6 : 3, x, y, r->vx * 0.4f, r->vy * 0.4f, 8, 45, 0.9f, 1.8f, R * 0.35f, R * 0.6f, 0x6A6070);
	new_part (P_FLASH, x, y, r->vx * 0.5f, r->vy * 0.5f, 0.22f, R * 2.2f, 0xFFC890);
	if (size >= 2) add_ring (x, y, R * 0.4f, size == 3 ? 260 : 190, size == 3 ? 9 : 6, 0.55f, 0xFFB070);

	if (size > 1)
	{
		for (int k = 0; k < 2; k++)
		{
			float a = atan2f (ivy, ivx) + (k ? 1 : -1) * frange (0.5f, 1.3f);
			float v = rock_speed (size - 1);
			float nx = cosf (a), ny = sinf (a);
			rock_new (size - 1, x + nx * R * 0.35f, y + ny * R * 0.35f,
				r->vx * 0.5f + nx * v + ivx * 0.04f, r->vy * 0.5f + ny * v + ivy * 0.04f, r->col);
		}
	}
}

static int rocks_alive (int *units)
{
	int n = 0, u = 0;
	for (int i = 0; i < MAX_ROCKS; i++)
		if (g_rocks[i].alive) { n++; u += g_rocks[i].size == 3 ? 7 : g_rocks[i].size == 2 ? 3 : 1; }
	if (units) *units = u;
	return n;
}

// des gros astéroïdes, loin du vaisseau
static void spawn_wave (int n)
{
	for (int i = 0; i < n; i++)
	{
		float x = 0, y = 0;
		for (int tries = 0; tries < 30; tries++)
		{
			if (frand () < 0.5f) { x = frand () * g_ww; y = frand () < 0.5f ? frange (0, 60) : g_wh - frange (0, 60); }
			else { y = frand () * g_wh; x = frand () < 0.5f ? frange (0, 60) : g_ww - frange (0, 60); }
			float dx = wdx (x - ship.x), dy = wdy (y - ship.y);
			if (g_state != ST_PLAY || dx * dx + dy * dy > 240.0f * 240.0f) break;
		}
		float a = frand () * TAU, v = rock_speed (3);
		rock_new (3, x, y, cosf (a) * v, sinf (a) * v, 0);
	}
}

// ---- le vaisseau -----------------------------------------------------------------------------------------------------

static const float HULL[] = {
	1.75f, 0.0f, 0.70f, -0.30f, -0.10f, -0.42f, -0.85f, -1.20f, -1.20f, -1.18f, -0.95f, -0.42f, -1.10f, -0.28f,
	-1.10f, 0.28f, -0.95f, 0.42f, -1.20f, 1.18f, -0.85f, 1.20f, -0.10f, 0.42f, 0.70f, 0.30f
};
#define HULL_N 13

static void spawn_ship (void)
{
	ship.alive = 1;
	ship.x = g_ww * 0.5f; ship.y = g_wh * 0.5f;
	ship.vx = ship.vy = 0; ship.ang = -PI / 2;
	ship.inv = 2.5f; ship.fire_cd = 0.2f; ship.hyper_cd = 0.5f; ship.thrust = 0;
	add_ring (ship.x, ship.y, 60, -90, 5, 0.5f, 0x60D0FF);
}

static int center_clear (void)
{
	float cx = g_ww * 0.5f, cy = g_wh * 0.5f;
	for (int i = 0; i < MAX_ROCKS; i++)
		if (g_rocks[i].alive)
		{
			float dx = wdx (g_rocks[i].x - cx), dy = wdy (g_rocks[i].y - cy), R = ROCK_R[g_rocks[i].size] + 110;
			if (dx * dx + dy * dy < R * R) return 0;
		}
	if (ufo.alive)
	{
		float dx = wdx (ufo.x - cx), dy = wdy (ufo.y - cy);
		if (dx * dx + dy * dy < 160 * 160) return 0;
	}
	return 1;
}

static void kill_ship (void)
{
	if (!ship.alive) return;
	ship.alive = 0;
	ship.respawn = 2.2f;
	g_lives--;
	snd_play (SFX_DIE);
	snd_thrust (0);
	shake (16);
	flash (0xFFE0C0, 0.55f);
	float c = cosf (ship.ang), s = sinf (ship.ang);
	// la coque part en morceaux : un éclat par côté
	for (int i = 0; i < HULL_N; i++)
	{
		int j = (i + 1) % HULL_N;
		float ax = HULL[2 * i], ay = HULL[2 * i + 1], bx = HULL[2 * j], by = HULL[2 * j + 1];
		float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;
		float wx = ship.x + (mx * c - my * s) * SHIP_S, wy = ship.y + (mx * s + my * c) * SHIP_S;
		float ex = (bx - ax), ey = (by - ay);
		float len = sqrtf (ex * ex + ey * ey) * SHIP_S * 0.5f;
		float out = atan2f (wy - ship.y, wx - ship.x), v = frange (25, 90);
		part *p = new_part (P_SHARD, wx, wy, ship.vx * 0.4f + cosf (out) * v, ship.vy * 0.4f + sinf (out) * v,
			frange (1.4f, 2.6f), len, 0xC8D4E8);
		p->ang = atan2f (ex * s + ey * c, ex * c - ey * s);
		p->spin = frange (-4, 4);
	}
	spray (P_SPARK, 60, ship.x, ship.y, ship.vx * 0.3f, ship.vy * 0.3f, 50, 340, 0.4f, 1.3f, 2.5f, 5.0f, 0);
	spray (P_SPARK, 25, ship.x, ship.y, ship.vx * 0.3f, ship.vy * 0.3f, 20, 160, 0.4f, 1.0f, 2.0f, 3.5f, 0x60C8FF);
	spray (P_SMOKE, 12, ship.x, ship.y, ship.vx * 0.3f, ship.vy * 0.3f, 10, 50, 1.2f, 2.4f, 12, 22, 0x605870);
	new_part (P_FLASH, ship.x, ship.y, 0, 0, 0.35f, 110, 0xFFD0A0);
	add_ring (ship.x, ship.y, 8, 330, 12, 0.7f, 0xFFC080);
	add_ring (ship.x, ship.y, 4, 200, 7, 0.9f, 0x80C0FF);
	if (g_lives <= 0) g_end_t = 2.8f;
}

static void fire_shot (void)
{
	int n = 0;
	for (int i = 0; i < MAX_SHOTS; i++) if (g_shots[i].alive && !g_shots[i].enemy) n++;
	if (n >= 10) return;
	for (int i = 0; i < MAX_SHOTS; i++)
		if (!g_shots[i].alive)
		{
			shot *b = &g_shots[i];
			float c = cosf (ship.ang), s = sinf (ship.ang);
			b->alive = 1; b->enemy = 0; b->life = SHOT_LIFE;
			b->x = ship.x + c * SHIP_S * 1.8f; b->y = ship.y + s * SHIP_S * 1.8f;
			b->vx = ship.vx + c * SHOT_SPEED; b->vy = ship.vy + s * SHOT_SPEED;
			ship.vx -= c * 5; ship.vy -= s * 5;		// un léger recul
			new_part (P_FLASH, b->x, b->y, ship.vx, ship.vy, 0.08f, 22, 0x60E0FF);
			snd_play (SFX_FIRE);
			return;
		}
}

static void hyperspace (void)
{
	if (!ship.alive || ship.hyper_cd > 0) return;
	spray (P_SPARK, 30, ship.x, ship.y, 0, 0, 60, 220, 0.3f, 0.7f, 2, 3.5f, 0x80B0FF);
	add_ring (ship.x, ship.y, 4, 220, 6, 0.4f, 0x8090FF);
	ship.x = frange (40, g_ww - 40); ship.y = frange (40, g_wh - 40);
	ship.vx = ship.vy = 0;
	ship.hyper_cd = 1.0f;
	if (ship.inv < 0.4f) ship.inv = 0.4f;
	add_ring (ship.x, ship.y, 70, -120, 6, 0.45f, 0x8090FF);
	spray (P_SPARK, 20, ship.x, ship.y, 0, 0, 20, 90, 0.2f, 0.5f, 2, 3, 0xA0C0FF);
	snd_play (SFX_HYPER);
}

static void emit_flame (float tl, float dt)
{
	float c = cosf (ship.ang), s = sinf (ship.ang);
	float bx = ship.x - c * SHIP_S * 1.2f, by = ship.y - s * SHIP_S * 1.2f;
	static float acc;
	acc += dt * 110 * tl;
	while (acc >= 1)
	{
		acc -= 1;
		float a = ship.ang + PI + frange (-0.25f, 0.25f), v = frange (120, 220);
		new_part (P_FLAME, bx + frange (-2, 2), by + frange (-2, 2), ship.vx + cosf (a) * v, ship.vy + sinf (a) * v,
			frange (0.18f, 0.4f), frange (3, 5.5f), 0);
	}
}

// ---- l'ovni ------------------------------------------------------------------------------------------------------------

static float ufo_r (void) { return ufo.small ? 13 : 22; }

static void spawn_ufo (void)
{
	float p_small = g_score > 12000 ? 0.75f : g_score > 5000 ? 0.45f : g_level >= 3 ? 0.25f : 0.1f;
	ufo.alive = 1;
	ufo.small = frand () < p_small;
	int left = frand () < 0.5f;
	ufo.x = left ? -30 : g_ww + 30;
	ufo.y = frange (g_wh * 0.15f, g_wh * 0.85f);
	ufo.vx = (left ? 1 : -1) * (ufo.small ? 170 : 125);
	ufo.vy = 0; ufo.fire_t = 1.0f; ufo.dir_t = 1.2f; ufo.t = 0;
}

static void ufo_fire (void)
{
	float a;
	if (ufo.small && ship.alive)
	{
		float err = clampf (0.35f - (float) g_score / 60000.0f, 0.06f, 0.35f);
		a = atan2f (wdy (ship.y - ufo.y), wdx (ship.x - ufo.x)) + frange (-err, err);
	}
	else if (ship.alive && frand () < 0.35f)
		a = atan2f (wdy (ship.y - ufo.y), wdx (ship.x - ufo.x)) + frange (-0.6f, 0.6f);
	else a = frand () * TAU;
	for (int i = 0; i < MAX_SHOTS; i++)
		if (!g_shots[i].alive)
		{
			shot *b = &g_shots[i];
			b->alive = 1; b->enemy = 1; b->life = 1.3f;
			b->x = ufo.x + cosf (a) * ufo_r (); b->y = ufo.y + sinf (a) * ufo_r ();
			b->vx = cosf (a) * 380; b->vy = sinf (a) * 380;
			snd_play (SFX_UFO_FIRE);
			return;
		}
}

static void ufo_destroy (int by_player)
{
	if (!ufo.alive) return;
	ufo.alive = 0;
	snd_ufo (0);
	snd_play (SFX_UFO_BANG);
	if (by_player) add_score (ufo.small ? 1000 : 200, ufo.x, ufo.y);
	shake (9);
	spray (P_SPARK, 40, ufo.x, ufo.y, ufo.vx * 0.3f, 0, 50, 280, 0.3f, 1.0f, 2, 4.5f, 0);
	spray (P_SPARK, 20, ufo.x, ufo.y, ufo.vx * 0.3f, 0, 30, 200, 0.3f, 0.9f, 2, 3.5f, 0x70FF90);
	spray (P_DEBRIS, 10, ufo.x, ufo.y, ufo.vx * 0.3f, 0, 40, 160, 0.8f, 1.6f, 2, 4, 0x9AA4B8);
	spray (P_SMOKE, 6, ufo.x, ufo.y, 0, 0, 10, 40, 1.0f, 1.8f, 10, 16, 0x605870);
	new_part (P_FLASH, ufo.x, ufo.y, 0, 0, 0.3f, 90, 0xC0FFD0);
	add_ring (ufo.x, ufo.y, 6, 280, 9, 0.6f, 0x80FFB0);
}

// ---- les parties -------------------------------------------------------------------------------------------------------

static void next_level (void)
{
	g_level++;
	int n = 3 + g_level;
	if (n > 11) n = 11;
	spawn_wave (n);
	rocks_alive (&g_level_units);
	g_banner = 2.6f;
	g_clear_t = 0;
	g_beat_t = 0;
}

static void clear_world (void)
{
	memset (g_rocks, 0, sizeof g_rocks);
	memset (g_shots, 0, sizeof g_shots);
	memset (g_rings, 0, sizeof g_rings);
	memset (g_pops, 0, sizeof g_pops);
	ufo.alive = 0;
	snd_ufo (0);
}

static void new_game (void)
{
	clear_world ();
	g_state = ST_PLAY;
	g_paused = 0;
	g_score = 0; g_lives = 3; g_level = 0; g_next_life = 10000; g_new_best = 0; g_end_t = 0;
	g_ufo_t = frange (16, 26);
	spawn_ship ();
	next_level ();
	snd_play (SFX_START);
}

static void attract_mode (void)
{
	clear_world ();
	g_level = 1;
	for (int i = 0; i < 7; i++)
	{
		float a = frand () * TAU, v = rock_speed (3) * 0.7f;
		int size = i < 4 ? 3 : i < 6 ? 2 : 1;
		rock_new (size, frand () * g_ww, frand () * g_wh, cosf (a) * v, sinf (a) * v, 0);
	}
}

static void game_over (void)
{
	g_state = ST_OVER;
	g_over_t = 0;
	snd_ufo (0);
	snd_thrust (0);
	if (g_score > g_best) { g_best = g_score; g_new_best = 1; save_best (); }
}

// ---- la logique, à chaque image ----------------------------------------------------------------------------------------

static void update_ship (float dt)
{
	if (!ship.alive)
	{
		snd_thrust (0);
		if (g_lives > 0)
		{
			ship.respawn -= dt;
			if (ship.respawn <= 0 && (center_clear () || ship.respawn < -4)) spawn_ship ();
		}
		return;
	}
	float tl = 0;
	if (g_mouse_mode)
	{
		// la souris : le vaisseau se tourne vers le pointeur et le rejoint, puis freine près de lui
		float tx = (float) m_x / g_px, ty = (float) m_y / g_px;
		float dx = tx - ship.x, dy = ty - ship.y, d = sqrtf (dx * dx + dy * dy);
		if (d > 3)
		{
			float diff = angle_wrap (atan2f (dy, dx) - ship.ang), maxr = 7.5f * dt;
			ship.ang += clampf (diff, -maxr, maxr);
			const float dead = 48;
			if (d > dead) tl = clampf ((d - dead) / 200.0f, 0, 1) * clampf (cosf (diff) * 1.4f - 0.4f, 0, 1);
		}
		if (d < 70)
		{
			float k = expf (-dt * 3.5f * (1 - d / 70));
			ship.vx *= k; ship.vy *= k;
		}
	}
	else
	{
		if (k_left) ship.ang -= 4.7f * dt;
		if (k_right) ship.ang += 4.7f * dt;
		if (k_up) tl = 1;
	}
	ship.ang = angle_wrap (ship.ang);
	if (tl > 0.03f)
	{
		ship.vx += cosf (ship.ang) * 430 * tl * dt;
		ship.vy += sinf (ship.ang) * 430 * tl * dt;
		emit_flame (tl, dt);
	}
	float k = expf (-dt * 0.45f);
	ship.vx *= k; ship.vy *= k;
	float sp = sqrtf (ship.vx * ship.vx + ship.vy * ship.vy);
	if (sp > 560) { ship.vx *= 560 / sp; ship.vy *= 560 / sp; }
	ship.x += ship.vx * dt; ship.y += ship.vy * dt;
	wrap_pos (&ship.x, &ship.y);
	ship.thrust += (tl - ship.thrust) * clampf (dt * 12, 0, 1);
	snd_thrust (tl);
	if (ship.inv > 0) ship.inv -= dt;
	if (ship.hyper_cd > 0) ship.hyper_cd -= dt;

	// le tir : un appui tire tout de suite, une touche tenue tire en rafale
	ship.fire_cd -= dt;
	int edge = k_fire_edge || m_fire_edge, held = k_fire || m_left;
	if ((edge && ship.fire_cd <= 0.09f) || (held && ship.fire_cd <= 0))
	{
		fire_shot ();
		ship.fire_cd = 0.17f;
	}
	if (k_hyper) hyperspace ();
}

static void update_ufo (float dt)
{
	if (!ufo.alive)
	{
		if (g_state == ST_PLAY && g_level >= 1 && rocks_alive (0) > 0)
		{
			g_ufo_t -= dt;
			if (g_ufo_t <= 0)
			{
				spawn_ufo ();
				g_ufo_t = frange (18, 30) - clampf ((float) g_level * 1.5f, 0, 10);
			}
		}
		snd_ufo (0);
		return;
	}
	ufo.t += dt;
	ufo.x += ufo.vx * dt; ufo.y += ufo.vy * dt;
	if (ufo.y < 0) ufo.y += g_wh; else if (ufo.y >= g_wh) ufo.y -= g_wh;
	ufo.dir_t -= dt;
	if (ufo.dir_t <= 0)
	{
		float r = frand ();
		ufo.vy = r < 0.33f ? -90 : r < 0.66f ? 0 : 90;
		ufo.dir_t = frange (0.8f, 1.8f);
	}
	ufo.fire_t -= dt;
	if (ufo.fire_t <= 0 && g_state == ST_PLAY)
	{
		ufo_fire ();
		ufo.fire_t = ufo.small ? frange (0.7f, 1.1f) : frange (1.0f, 1.6f);
	}
	if ((ufo.vx > 0 && ufo.x > g_ww + 40) || (ufo.vx < 0 && ufo.x < -40)) { ufo.alive = 0; snd_ufo (0); return; }
	snd_ufo (ufo.small ? 2 : 1);
}

static int hit (float ax, float ay, float bx, float by, float r)
{
	float dx = wdx (ax - bx), dy = wdy (ay - by);
	return dx * dx + dy * dy < r * r;
}

static void collisions (void)
{
	for (int i = 0; i < MAX_SHOTS; i++)
	{
		shot *b = &g_shots[i];
		if (!b->alive) continue;
		for (int k = 0; k < MAX_ROCKS && b->alive; k++)
		{
			rock *r = &g_rocks[k];
			if (r->alive && hit (b->x, b->y, r->x, r->y, ROCK_R[r->size] * 0.95f))
			{
				b->alive = 0;
				spray (P_SPARK, 6, b->x, b->y, 0, 0, 40, 160, 0.15f, 0.35f, 1.5f, 2.5f, b->enemy ? 0xFF6080 : 0x80E8FF);
				rock_destroy (k, b->vx, b->vy, !b->enemy);
			}
		}
		if (!b->alive) continue;
		if (!b->enemy && ufo.alive && hit (b->x, b->y, ufo.x, ufo.y, ufo_r () * 1.1f))
		{
			b->alive = 0;
			ufo_destroy (1);
			continue;
		}
		if (b->enemy && ship.alive && ship.inv <= 0 && hit (b->x, b->y, ship.x, ship.y, SHIP_R))
		{
			b->alive = 0;
			kill_ship ();
		}
	}
	if (ship.alive && ship.inv <= 0)
	{
		for (int k = 0; k < MAX_ROCKS; k++)
		{
			rock *r = &g_rocks[k];
			if (r->alive && hit (ship.x, ship.y, r->x, r->y, ROCK_R[r->size] * 0.85f + SHIP_R * 0.8f))
			{
				rock_destroy (k, ship.vx, ship.vy, 1);
				kill_ship ();
				break;
			}
		}
		if (ship.alive && ufo.alive && hit (ship.x, ship.y, ufo.x, ufo.y, ufo_r () + SHIP_R * 0.8f))
		{
			ufo_destroy (1);
			kill_ship ();
		}
	}
	if (ufo.alive)
		for (int k = 0; k < MAX_ROCKS; k++)
		{
			rock *r = &g_rocks[k];
			if (r->alive && hit (ufo.x, ufo.y, r->x, r->y, ROCK_R[r->size] * 0.85f + ufo_r ()))
			{
				rock_destroy (k, ufo.vx, ufo.vy, 0);
				ufo_destroy (0);
				break;
			}
		}
}

static void step (float dt)
{
	g_time += dt;

	// les étoiles glissent à l'opposé du vaisseau, plus vite au premier plan
	float svx = ship.alive && g_state == ST_PLAY ? ship.vx : 0, svy = ship.alive && g_state == ST_PLAY ? ship.vy : 0;
	for (int i = 0; i < g_nstars; i++)
	{
		star *s = &g_stars[i];
		s->x -= (svx * 0.08f + 6) * s->depth * dt;
		s->y -= (svy * 0.08f + 2) * s->depth * dt;
		wrap_pos (&s->x, &s->y);
	}

	g_shake *= expf (-dt * 7);
	if (g_shake < 0.2f) g_shake = 0;
	g_shx = (frand () * 2 - 1) * g_shake * g_px;
	g_shy = (frand () * 2 - 1) * g_shake * g_px;
	g_flash -= dt * 2.2f;
	if (g_flash < 0) g_flash = 0;
	if (g_banner > 0) g_banner -= dt;

	if (g_state == ST_PLAY)
	{
		update_ship (dt);
		if (g_end_t > 0) { g_end_t -= dt; if (g_end_t <= 0) game_over (); }
	}
	else
	{
		snd_thrust (0);
		g_over_t += dt;
	}
	update_ufo (dt);
	k_fire_edge = m_fire_edge = k_hyper = 0;

	for (int i = 0; i < MAX_ROCKS; i++)
	{
		rock *r = &g_rocks[i];
		if (!r->alive) continue;
		r->x += r->vx * dt; r->y += r->vy * dt;
		r->ang += r->spin * dt;
		wrap_pos (&r->x, &r->y);
	}
	for (int i = 0; i < MAX_SHOTS; i++)
	{
		shot *b = &g_shots[i];
		if (!b->alive) continue;
		b->x += b->vx * dt; b->y += b->vy * dt;
		wrap_pos (&b->x, &b->y);
		b->life -= dt;
		if (b->life <= 0) b->alive = 0;
	}
	for (int i = 0; i < MAX_PARTS; i++)
	{
		part *p = &g_parts[i];
		if (p->life <= 0) continue;
		p->life -= dt;
		float drag = p->kind == P_SPARK ? 2.2f : p->kind == P_FLAME ? 3.5f : p->kind == P_SMOKE ? 1.4f : 0.25f;
		float k = expf (-dt * drag);
		p->vx *= k; p->vy *= k;
		p->x += p->vx * dt; p->y += p->vy * dt;
		p->ang += p->spin * dt;
		wrap_pos (&p->x, &p->y);
	}
	for (int i = 0; i < MAX_RINGS; i++)
	{
		ringfx *r = &g_rings[i];
		if (!r->alive) continue;
		r->r += r->vr * dt;
		r->life -= dt;
		if (r->life <= 0 || r->r <= 0) r->alive = 0;
	}
	for (int i = 0; i < MAX_POPS; i++)
	{
		popup *p = &g_pops[i];
		if (!p->alive) continue;
		p->y -= 40 * dt;
		p->life -= dt;
		if (p->life <= 0) p->alive = 0;
	}

	if (g_state != ST_PLAY) return;
	collisions ();

	// le niveau vidé : le suivant, après un souffle
	int units = 0, n = rocks_alive (&units);
	if (n == 0 && !ufo.alive && g_lives > 0)
	{
		if (g_clear_t <= 0) g_clear_t = 2.0f;
		else
		{
			g_clear_t -= dt;
			if (g_clear_t <= 0.0001f) { g_clear_t = 0; next_level (); }
		}
	}
	// le battement : de plus en plus rapide à mesure que le ciel se vide
	if (n > 0 && g_lives > 0)
	{
		float ratio = g_level_units > 0 ? clampf ((float) units / (float) g_level_units, 0, 1) : 1;
		float interval = 0.26f + 0.74f * ratio;
		g_beat_t -= dt;
		if (g_beat_t <= 0)
		{
			snd_play (g_beat_alt ? SFX_BEAT2 : SFX_BEAT1);
			g_beat_alt ^= 1;
			g_beat_t = interval;
		}
	}
}

// ---- le dessin -----------------------------------------------------------------------------------------------------

static void draw_bg (void)
{
	if (!g_bg || g_bg_w != g_W || g_bg_h != g_H) build_bg ();
	if (!g_bg) { gfx_rect (0, 0, g_W, g_H, BG_DARK, 256); return; }
	int sx = (int) g_shx, sy = (int) g_shy;
	for (int y = 0; y < g_H; y++)
	{
		int srcy = y - sy;
		if (srcy < 0) srcy = 0;
		if (srcy >= g_H) srcy = g_H - 1;
		const uint32_t *src = g_bg + (size_t) srcy * (size_t) g_W;
		uint32_t *dst = ui.surf + (size_t) y * (size_t) ui.w;
		if (sx == 0) memcpy (dst, src, (size_t) g_W * 4);
		else if (sx > 0)
		{
			memcpy (dst + sx, src, (size_t) (g_W - sx) * 4);
			for (int x = 0; x < sx; x++) dst[x] = src[0];
		}
		else
		{
			memcpy (dst, src - sx, (size_t) (g_W + sx) * 4);
			for (int x = g_W + sx; x < g_W; x++) dst[x] = src[g_W - 1];
		}
	}
}

static void draw_stars (void)
{
	for (int i = 0; i < g_nstars; i++)
	{
		const star *s = &g_stars[i];
		float b = 0.65f + 0.35f * sinf (g_time * s->tw + s->ph);
		float X = PX (s->x), Y = PY (s->y);
		if (s->depth < 0.3f) gfx_glow (X, Y, 1.6f * g_px, s->col, (int) (150 * b));
		else if (s->depth < 0.5f) gfx_glow (X, Y, 2.2f * g_px, s->col, (int) (200 * b));
		else
		{
			gfx_glow (X, Y, 4.0f * g_px, s->col, (int) (120 * b));
			gfx_glow (X, Y, 1.6f * g_px, 0xFFFFFF, (int) (230 * b));
		}
	}
}

static void draw_rock (const rock *r, float wx, float wy)
{
	float R = ROCK_R[r->size] * g_px, c = cosf (r->ang), s = sinf (r->ang);
	float X = PX (wx), Y = PY (wy);
	unsigned mid = r->col;
	unsigned dark = gfx_shade (mid, 92), lite = gfx_shade (mid, 330), hi = gfx_shade (mid, 430);
	// des couches de plus en plus petites, décalées vers la lumière (en haut à gauche) : un relief
	static const float L[4][3] = { { 1.0f, 0, 0 }, { 0.87f, -0.06f, -0.07f }, { 0.6f, -0.15f, -0.17f }, { 0.3f, -0.26f, -0.28f } };
	unsigned cols[4] = { dark, mid, gfx_mix (mid, lite, 170), gfx_mix (lite, hi, 120) };
	static const int alpha[4] = { 256, 256, 190, 120 };
	float pts[2 * ROCK_MAXV];
	for (int l = 0; l < 4; l++)
	{
		float k = L[l][0] * R, ox = X + L[l][1] * R, oy = Y + L[l][2] * R;
		for (int i = 0; i < r->nv; i++)
		{
			float lx = r->ux[i] * k, ly = r->uy[i] * k;
			pts[2 * i] = ox + lx * c - ly * s;
			pts[2 * i + 1] = oy + lx * s + ly * c;
		}
		gfx_poly (pts, r->nv, cols[l], alpha[l]);
	}
	// les cratères : creux dans l'ombre en haut à gauche, rebord éclairé en bas à droite
	for (int k = 0; k < r->ncr; k++)
	{
		float u = r->crx[k] * R, v = r->cry[k] * R;
		float cx = X + u * c - v * s, cy = Y + u * s + v * c, cr = r->crr[k] * R;
		if (cr < 1.2f) continue;
		int lf = 256 - (int) (((cx - X) + (cy - Y)) / (R * 1.4f) * 80);
		gfx_disc (cx + cr * 0.14f, cy + cr * 0.14f, cr, gfx_shade (mid, lf * 125 / 100), 210);
		gfx_disc (cx - cr * 0.08f, cy - cr * 0.08f, cr * 0.8f, gfx_shade (mid, lf * 55 / 100), 235);
	}
	// un liseré de lumière sur le bord éclairé
	for (int i = 0; i < r->nv; i++)
	{
		int j = (i + 1) % r->nv;
		float ax = r->ux[i] * R, ay = r->uy[i] * R, bx = r->ux[j] * R, by = r->uy[j] * R;
		float x0 = X + ax * c - ay * s, y0 = Y + ax * s + ay * c, x1 = X + bx * c - by * s, y1 = Y + bx * s + by * c;
		float nx = (x0 + x1) * 0.5f - X, ny = (y0 + y1) * 0.5f - Y;
		float lit = -(nx + ny) / (R * 1.2f);
		if (lit > 0.15f) gfx_line (x0, y0, x1, y1, 1.3f * g_px, lite, (int) (clampf (lit, 0, 1) * 150));
	}
}

static void xform (const float *src, int n, float X, float Y, float c, float s, float k, float *dst)
{
	for (int i = 0; i < n; i++)
	{
		float lx = src[2 * i] * k, ly = src[2 * i + 1] * k;
		dst[2 * i] = X + lx * c - ly * s;
		dst[2 * i + 1] = Y + lx * s + ly * c;
	}
}

// le vaisseau, en pixels : centre, angle, échelle (pixels par unité du dessin), poussée 0..1
static void draw_ship_px (float X, float Y, float ang, float k, float thrust, int full)
{
	static float inset[2 * HULL_N];
	static int ready;
	if (!ready)
	{
		for (int i = 0; i < HULL_N; i++) { inset[2 * i] = (HULL[2 * i] + 0.1f) * 0.82f - 0.1f; inset[2 * i + 1] = HULL[2 * i + 1] * 0.78f; }
		ready = 1;
	}
	float c = cosf (ang), s = sinf (ang), pts[2 * 16];
	if (thrust > 0.02f && full)
	{
		// la flamme : trois langues qui vacillent, et sa lumière
		float L = (0.9f + 0.6f * thrust + frange (-0.15f, 0.25f)) * thrust;
		float f1[] = { -1.10f, -0.26f, -1.10f - 1.7f * L, 0, -1.10f, 0.26f };
		float f2[] = { -1.10f, -0.17f, -1.10f - 1.15f * L, 0, -1.10f, 0.17f };
		float f3[] = { -1.10f, -0.09f, -1.10f - 0.6f * L, 0, -1.10f, 0.09f };
		gfx_glow (X - c * k * (1.4f + L), Y - s * k * (1.4f + L), k * (1.6f + L), 0xFF7020, (int) (190 * thrust));
		xform (f1, 3, X, Y, c, s, k, pts); gfx_poly (pts, 3, 0xFF6A1A, 230);
		xform (f2, 3, X, Y, c, s, k, pts); gfx_poly (pts, 3, 0xFFC040, 240);
		xform (f3, 3, X, Y, c, s, k, pts); gfx_poly (pts, 3, 0xFFF4D0, 256);
	}
	xform (HULL, HULL_N, X, Y, c, s, k, pts);
	gfx_poly (pts, HULL_N, 0x1A2232, 256);
	xform (inset, HULL_N, X, Y, c, s, k, pts);
	gfx_poly (pts, HULL_N, 0x93A2B8, 256);
	// le dos éclairé
	static const float spine[] = { 1.5f, 0, 0.6f, -0.15f, -0.95f, -0.17f, -0.95f, 0.17f, 0.6f, 0.15f };
	xform (spine, 5, X, Y, c, s, k, pts);
	gfx_poly (pts, 5, 0xDCE6F4, 256);
	// les bandes rouges des ailes
	static const float wl[] = { -0.42f, -0.60f, -0.72f, -1.00f, -0.92f, -1.00f, -0.62f, -0.60f };
	static const float wr[] = { -0.42f, 0.60f, -0.72f, 1.00f, -0.92f, 1.00f, -0.62f, 0.60f };
	xform (wl, 4, X, Y, c, s, k, pts); gfx_poly (pts, 4, 0xE0463C, 256);
	xform (wr, 4, X, Y, c, s, k, pts); gfx_poly (pts, 4, 0xE0463C, 256);
	// la tuyère
	static const float noz[] = { -1.0f, -0.22f, -1.2f, -0.2f, -1.2f, 0.2f, -1.0f, 0.22f };
	xform (noz, 4, X, Y, c, s, k, pts); gfx_poly (pts, 4, 0x3A4456, 256);
	// le cockpit
	float cx = X + c * k * 0.6f, cy = Y + s * k * 0.6f;
	gfx_disc (cx, cy, k * 0.24f, 0x0E2448, 256);
	gfx_disc (cx, cy, k * 0.19f, 0x1E4E8C, 256);
	gfx_disc (cx + (c * 0.06f + s * 0.07f) * k, cy + (s * 0.06f - c * 0.07f) * k, k * 0.07f, 0xA8E0FF, 220);
	if (!full) return;
	// les feux de position, qui clignotent
	float blink = fmodf (g_time, 1.2f) < 0.15f ? 1.0f : 0.35f;
	float lx = -1.02f, ly = 1.16f;
	gfx_glow (X + (lx * c + ly * s) * k, Y + (lx * s - ly * c) * k, k * 0.55f, 0xFF3030, (int) (255 * blink));
	gfx_glow (X + (lx * c - ly * s) * k, Y + (lx * s + ly * c) * k, k * 0.55f, 0x30FF60, (int) (255 * blink));
}

static void draw_ship (void)
{
	if (!ship.alive) return;
	float ox[4], oy[4];
	int n = wrap_spots (ship.x, ship.y, SHIP_S * 2, ox, oy);
	for (int i = 0; i < n; i++)
	{
		float X = PX (ox[i]), Y = PY (oy[i]);
		draw_ship_px (X, Y, ship.ang, SHIP_S * g_px, ship.thrust, 1);
		if (ship.inv > 0)
		{
			// le bouclier du retour : une bulle qui palpite, et s'efface en clignotant à la fin
			float a = ship.inv > 0.8f ? 1.0f : (fmodf (ship.inv, 0.2f) < 0.1f ? 0.3f : 0.9f);
			float pulse = 0.85f + 0.15f * sinf (g_time * 9);
			gfx_disc (X, Y, SHIP_S * 2.1f * g_px, 0x2080FF, (int) (28 * a));
			gfx_glow_ring (X, Y, SHIP_S * 2.1f * g_px, 3.5f * g_px, 0x50C8FF, (int) (200 * a * pulse));
		}
	}
}

static void draw_ufo_px (float X, float Y, float U, int small)
{
	unsigned hull = small ? 0xB07060 : 0x8E99AC, top = small ? 0xE0A090 : 0xC8D2E0;
	gfx_glow (X, Y + U * 0.35f, U * 2.2f, small ? 0x502010 : 0x103050, 200);
	// le dôme
	gfx_disc (X, Y - U * 0.22f, U * 0.56f, 0x14294A, 256);
	gfx_disc (X, Y - U * 0.25f, U * 0.48f, small ? 0x6A2A3A : 0x255C94, 256);
	gfx_disc (X - U * 0.17f, Y - U * 0.43f, U * 0.15f, 0xC8EEFF, 200);
	// la coque
	float body[] = { -1.55f, 0.05f, -1.0f, 0.45f, 1.0f, 0.45f, 1.55f, 0.05f, 0.95f, -0.22f, -0.95f, -0.22f };
	float in[] = { -1.42f, 0.05f, -0.95f, 0.38f, 0.95f, 0.38f, 1.42f, 0.05f, 0.9f, -0.16f, -0.9f, -0.16f };
	float band[] = { -1.35f, 0.0f, 1.35f, 0.0f, 0.88f, -0.15f, -0.88f, -0.15f };
	float pts[12];
	for (int i = 0; i < 6; i++) { pts[2 * i] = X + body[2 * i] * U; pts[2 * i + 1] = Y + body[2 * i + 1] * U; }
	gfx_poly (pts, 6, 0x1A1E2A, 256);
	for (int i = 0; i < 6; i++) { pts[2 * i] = X + in[2 * i] * U; pts[2 * i + 1] = Y + in[2 * i + 1] * U; }
	gfx_poly (pts, 6, hull, 256);
	for (int i = 0; i < 4; i++) { pts[2 * i] = X + band[2 * i] * U; pts[2 * i + 1] = Y + band[2 * i + 1] * U; }
	gfx_poly (pts, 4, top, 256);
	gfx_line (X - U * 1.4f, Y + U * 0.06f, X + U * 1.4f, Y + U * 0.06f, U * 0.08f, 0x2A3040, 256);
	// les lumières qui tournent
	static const unsigned lc[3] = { 0xFF4040, 0xFFD040, 0x40FF80 };
	for (int i = 0; i < 5; i++)
	{
		float lx = X + (-1.0f + 0.5f * (float) i) * U, ly = Y + U * 0.24f;
		unsigned col = lc[(i + (int) (g_time * 8)) % 3];
		gfx_glow (lx, ly, U * 0.36f, col, 230);
		gfx_disc (lx, ly, U * 0.07f, 0xFFFFFF, 230);
	}
}

static void draw_ufo (void)
{
	if (!ufo.alive) return;
	float ox[4], oy[4];
	int n = wrap_spots (ufo.x, ufo.y, ufo_r () * 2, ox, oy);
	for (int i = 0; i < n; i++) draw_ufo_px (PX (ox[i]), PY (oy[i]), ufo_r () * g_px, ufo.small);
}

static void draw_shots (void)
{
	for (int i = 0; i < MAX_SHOTS; i++)
	{
		const shot *b = &g_shots[i];
		if (!b->alive) continue;
		unsigned col = b->enemy ? 0xFF3060 : 0x40E0FF;
		float X = PX (b->x), Y = PY (b->y), fade = clampf (b->life / 0.15f, 0, 1);
		for (int t = 3; t >= 1; t--)		// la traînée
		{
			float tx = X - b->vx * 0.009f * (float) t * g_px, ty = Y - b->vy * 0.009f * (float) t * g_px;
			gfx_glow (tx, ty, (6.5f - (float) t) * g_px, col, (int) ((150 - 35 * t) * fade));
		}
		gfx_glow (X, Y, 9 * g_px, col, (int) (210 * fade));
		gfx_glow (X, Y, 3.2f * g_px, 0xFFFFFF, (int) (255 * fade));
	}
}

// la couleur d'une étincelle selon sa chaleur (1 : blanche, 0 : rouge sombre)
static unsigned heat (float f)
{
	if (f > 0.66f) return gfx_mix (0xFFB040, 0xFFF4D8, (int) ((f - 0.66f) / 0.34f * 256));
	if (f > 0.33f) return gfx_mix (0xD03010, 0xFFB040, (int) ((f - 0.33f) / 0.33f * 256));
	return gfx_mix (0x401008, 0xD03010, (int) (f / 0.33f * 256));
}

static void draw_parts_under (void)
{
	for (int i = 0; i < MAX_PARTS; i++)
	{
		const part *p = &g_parts[i];
		if (p->life <= 0 || p->kind != P_SMOKE) continue;
		float f = p->life / p->max;
		float r = p->size * (1.0f + 1.6f * (1 - f)) * g_px;
		gfx_disc (PX (p->x), PY (p->y), r, p->col, (int) (f * f * 90));
	}
	for (int i = 0; i < MAX_RINGS; i++)
	{
		const ringfx *r = &g_rings[i];
		if (!r->alive) continue;
		float f = r->life / r->max;
		gfx_glow_ring (PX (r->x), PY (r->y), r->r * g_px, r->w * g_px * (0.5f + 0.5f * f), r->col, (int) (f * 200));
	}
}

static void draw_parts_mid (void)
{
	for (int i = 0; i < MAX_PARTS; i++)
	{
		const part *p = &g_parts[i];
		if (p->life <= 0) continue;
		float f = p->life / p->max, X = PX (p->x), Y = PY (p->y);
		if (p->kind == P_DEBRIS)
		{
			float z = p->size * g_px * (f < 0.3f ? f / 0.3f : 1);
			float pts[8];
			static const float ra[4] = { 1.0f, 0.75f, 0.95f, 0.7f }, aa[4] = { 0, 1.7f, 3.3f, 4.6f };
			for (int k = 0; k < 4; k++)
			{
				pts[2 * k] = X + cosf (p->ang + aa[k]) * z * ra[k];
				pts[2 * k + 1] = Y + sinf (p->ang + aa[k]) * z * ra[k];
			}
			gfx_poly (pts, 4, p->col, 256);
			if (f > 0.6f) gfx_glow (X, Y, z * 2.2f, 0xFF8030, (int) ((f - 0.6f) * 300));	// encore brûlant
		}
		else if (p->kind == P_SHARD)
		{
			float l = p->size * g_px, cx = cosf (p->ang) * l, cy = sinf (p->ang) * l;
			gfx_line (X - cx, Y - cy, X + cx, Y + cy, 2.2f * g_px, gfx_mix (0x404858, p->col, (int) (f * 256)), (int) (clampf (f * 1.5f, 0, 1) * 256));
			if (f > 0.5f) gfx_glow (X, Y, l * 1.2f, 0xFF9040, (int) ((f - 0.5f) * 260));
		}
	}
}

static void draw_parts_light (void)
{
	for (int i = 0; i < MAX_PARTS; i++)
	{
		const part *p = &g_parts[i];
		if (p->life <= 0) continue;
		float f = p->life / p->max, X = PX (p->x), Y = PY (p->y);
		switch (p->kind)
		{
		case P_SPARK:
		{
			unsigned col = p->col ? p->col : heat (f);
			gfx_glow (X, Y, p->size * (0.5f + 0.8f * f) * g_px * 1.6f, col, (int) (255 * f));
			if (f > 0.5f) gfx_glow (X, Y, p->size * 0.6f * g_px, 0xFFFFFF, (int) (255 * (f - 0.5f) * 2));
			break;
		}
		case P_FLAME:
			gfx_glow (X, Y, p->size * (0.6f + 1.2f * (1 - f)) * g_px, heat (f * 0.9f + 0.1f), (int) (220 * f));
			break;
		case P_FLASH:
			gfx_glow (X, Y, p->size * (1.0f + 0.4f * (1 - f)) * g_px, p->col, (int) (255 * f));
			break;
		}
	}
}

static void text_center (const char *s, float cx, float base, unsigned col, int size, long font)
{
	long w = ui_text_width ((long) s, size, font);
	ui_text ((long) s, (long) (cx - (float) w / 2), (long) base, col, size, font, 0);
}

static void text_shadow_center (const char *s, float cx, float base, unsigned col, int size, long font)
{
	long w = ui_text_width ((long) s, size, font);
	long x = (long) (cx - (float) w / 2);
	int off = size > 30 ? 3 : 1;
	ui_text ((long) s, x + off, (long) base + off, 0x000000, size, font, 0);
	ui_text ((long) s, x, (long) base, col, size, font, 0);
}

static void draw_popups (void)
{
	int size = (int) (15 * g_txt);
	for (int i = 0; i < MAX_POPS; i++)
	{
		const popup *p = &g_pops[i];
		if (!p->alive) continue;
		float f = clampf (p->life / 0.5f, 0, 1);
		text_center (p->text, PX (p->x), PY (p->y), gfx_mix (BG_DARK, p->col, (int) (f * 256)), size, (long) ui.font_b);
	}
}

// un grand titre lumineux : une lueur derrière, une ombre, le texte
static void title_text (const char *s, float cx, float base, int size, unsigned col, unsigned glow)
{
	long w = ui_text_width ((long) s, size, (long) ui.font_b);
	float gy = base - (float) size * 0.36f;
	for (int k = -2; k <= 2; k++)
		gfx_glow (cx + (float) k * (float) w * 0.2f, gy, (float) size * 1.1f, glow, 70);
	text_shadow_center (s, cx, base, col, size, (long) ui.font_b);
}

static void draw_hud (void)
{
	int sz = (int) (26 * g_txt), small = (int) (14 * g_txt);
	char t[64];
	if (g_state == ST_PLAY)
	{
		snprintf (t, sizeof t, "%d", g_score);
		ui_text ((long) t, 22 + 2, 14 + sz + 2, 0x000000, sz, (long) ui.font_b, 0);
		ui_text ((long) t, 22, 14 + sz, 0xF2F6FF, sz, (long) ui.font_b, 0);
		// les vies : de petits vaisseaux
		int n = g_lives - (ship.alive ? 1 : 0);
		if (n < 0) n = 0;
		for (int i = 0; i < n && i < 8; i++)
			draw_ship_px (32 + (float) i * 24 * g_txt, 14 + (float) sz + 22 * g_txt, -PI / 2, 7.5f * g_txt, 0, 0);
		snprintf (t, sizeof t, TR ("Niveau %d"), g_level);
		text_shadow_center (t, (float) g_W * 0.5f, 14 + (float) sz * 0.6f + (float) small, 0x9AA6C8, small, (long) ui.font_b);
	}
	if (g_best > 0)
	{
		snprintf (t, sizeof t, TR ("Record %d"), g_best);
		text_shadow_center (t, (float) g_W * 0.5f, 14 + (float) small, 0xC8D0E8, small, (long) ui.font_b);
	}
	if (!g_sound_ok || !g_sound)
		ui_text ((long) TR ("Son coupé"), 22, g_H - 14, 0x8090B0, small, (long) ui.font, 0);

	// le bouton ‖ : sous la bande des boutons de la fenêtre, à droite
	if (g_state == ST_PLAY)
	{
		g_pb_s = (int) (30 * g_txt);
		g_pb_x = g_W - g_pb_s - 14;
		g_pb_y = 40;
		gfx_rbox (g_pb_x, g_pb_y, g_pb_s, g_pb_s, g_pb_s / 4, 0x2A3348, 0x1A2030, 170, 0x56607A);
		int bw = g_pb_s / 7, bh = g_pb_s / 2;
		gfx_rect (g_pb_x + g_pb_s / 2 - bw * 3 / 2, g_pb_y + g_pb_s / 4, bw, bh, 0xE0E6F2, 256);
		gfx_rect (g_pb_x + g_pb_s / 2 + bw / 2, g_pb_y + g_pb_s / 4, bw, bh, 0xE0E6F2, 256);
	}
	else g_pb_s = 0;

	// le réticule de la souris
	if (g_state == ST_PLAY && g_mouse_mode && m_seen && !g_paused)
	{
		gfx_ring ((float) m_x, (float) m_y, 11 * g_txt, 1.5f, 0x80D8FF, 150);
		gfx_disc ((float) m_x, (float) m_y, 1.8f, 0x80D8FF, 200);
	}
}

static void draw_banners (void)
{
	float cx = (float) g_W * 0.5f, H = (float) g_H;
	int big = (int) clampf ((float) g_W / 7.5f, 40, 120);
	int mid = (int) (20 * g_txt), small = (int) (15 * g_txt);
	unsigned accent = jet (TJ_ACCENT);
	if (g_state == ST_TITLE)
	{
		title_text ("ASTEROIDS", cx, H * 0.36f, big, 0xF4F8FF, accent);
		text_shadow_center (TR ("Clavier : flèches pour tourner et accélérer, Espace pour tirer, Bas pour l'hyperespace"),
			cx, H * 0.52f, 0xC0C8E0, small, (long) ui.font);
		text_shadow_center (TR ("Souris : le vaisseau suit le pointeur, clic gauche pour tirer, clic droit pour l'hyperespace"),
			cx, H * 0.52f + (float) small * 1.7f, 0xC0C8E0, small, (long) ui.font);
		text_shadow_center (TR ("P ou Échap : pause     S : son     F2 : nouvelle partie"),
			cx, H * 0.52f + (float) small * 3.4f, 0x8E98B8, small, (long) ui.font);
		if (fmodf (g_time, 1.4f) < 1.0f)
			text_shadow_center (TR ("Appuyez sur Espace ou cliquez pour jouer"), cx, H * 0.75f, 0xFFFFFF, mid, (long) ui.font_b);
	}
	else if (g_state == ST_OVER)
	{
		title_text (TR ("FIN DE PARTIE"), cx, H * 0.40f, (int) (big * 0.7f), 0xFFFFFF, jet (TJ_DANGER));
		char t[64];
		snprintf (t, sizeof t, TR ("Score : %d"), g_score);
		text_shadow_center (t, cx, H * 0.40f + (float) mid * 2.4f, 0xE8EEFF, (int) (mid * 1.3f), (long) ui.font_b);
		if (g_new_best && fmodf (g_time, 0.8f) < 0.55f)
			text_shadow_center (TR ("Nouveau record !"), cx, H * 0.40f + (float) mid * 4.2f, 0xFFD060, mid, (long) ui.font_b);
		if (g_over_t > 1.2f && fmodf (g_time, 1.4f) < 1.0f)
			text_shadow_center (TR ("Appuyez sur Espace ou cliquez pour rejouer"), cx, H * 0.75f, 0xFFFFFF, mid, (long) ui.font_b);
	}
	else if (g_banner > 0)
	{
		float a = g_banner > 2.2f ? (2.6f - g_banner) / 0.4f : g_banner < 0.6f ? g_banner / 0.6f : 1;
		char t[48];
		snprintf (t, sizeof t, TR ("Niveau %d"), g_level);
		int sz = (int) (big * 0.55f);
		text_center (t, cx + 2, H * 0.42f + 2, gfx_mix (BG_DARK, 0x000000, (int) (a * 256)), sz, (long) ui.font_b);
		text_center (t, cx, H * 0.42f, gfx_mix (BG_DARK, 0xF0F4FF, (int) (a * 256)), sz, (long) ui.font_b);
	}
}

// le menu de pause, aux couleurs du thème (c'est de l'interface)
static void draw_pause (void)
{
	static const char *items[PM_N] = { "Reprendre", "Recommencer", "Quitter" };
	gfx_dim (110);
	float k = g_txt;
	int w = (int) (280 * k), bh = (int) (44 * k), gap = (int) (10 * k), title = (int) (60 * k);
	int h = title + PM_N * (bh + gap) + gap;
	int x = (g_W - w) / 2, y = (g_H - h) / 2;
	gfx_rbox (x, y, w, h, (int) (14 * k), jet (TJ_SURFACE), jet (TJ_SUNKEN), 240, jet (TJ_LINE_STRONG));
	text_center (TR ("Pause"), (float) g_W * 0.5f, (float) y + 40 * k, jet (TJ_TEXT1), (int) (24 * k), (long) ui.font_b);
	g_pm_x = x + gap * 2; g_pm_w = w - gap * 4; g_pm_y = y + title; g_pm_h = bh; g_pm_step = bh + gap;
	for (int i = 0; i < PM_N; i++)
	{
		int by = g_pm_y + i * g_pm_step;
		int sel = i == g_psel;
		unsigned top = sel ? jet (TJ_ACCENT) : jet (TJ_RAISED), bot = sel ? jet (TJ_ACCENT_DARK) : jet (TJ_RAISED_LOW);
		gfx_rbox (g_pm_x, by, g_pm_w, bh, (int) (9 * k), top, bot, 256, sel ? jet (TJ_ACCENT_LIGHT) : jet (TJ_LINE));
		text_center (TR (items[i]), (float) (g_pm_x + g_pm_w / 2), (float) by + (float) bh * 0.5f + 6 * k,
			sel ? jet (TJ_ACCENT_INK) : jet (TJ_TEXT1), (int) (16 * k), (long) ui.font_b);
	}
}

static void draw (void)
{
	gfx_target (ui.surf, (int) ui.w, (int) ui.h, (int) ui.w);
	draw_bg ();
	draw_stars ();
	draw_parts_under ();
	for (int i = 0; i < MAX_ROCKS; i++)
	{
		const rock *r = &g_rocks[i];
		if (!r->alive) continue;
		float ox[4], oy[4];
		int n = wrap_spots (r->x, r->y, ROCK_R[r->size] * 1.1f, ox, oy);
		for (int k = 0; k < n; k++) draw_rock (r, ox[k], oy[k]);
	}
	draw_parts_mid ();
	draw_ufo ();
	draw_ship ();
	draw_shots ();
	draw_parts_light ();
	draw_popups ();
	if (g_flash > 0) gfx_add_all (gfx_shade (g_flash_col, (int) (g_flash * 120)));
	draw_hud ();
	draw_banners ();
	if (g_paused) draw_pause ();
}

// ---- la taille ---------------------------------------------------------------------------------------------------------

static void apply_size (void)
{
	if (ui.w < 16 || ui.h < 16) return;
	float old = g_ww;
	g_W = (int) ui.w; g_H = (int) ui.h;
	g_px = (float) g_H / WORLD_H;
	g_ww = (float) g_W / g_px;
	g_txt = clampf (g_px, 0.8f, 1.7f);
	if (old > 0 && old != g_ww)
	{
		// tout garde sa place relative
		float k = g_ww / old;
		for (int i = 0; i < MAX_ROCKS; i++) g_rocks[i].x *= k;
		for (int i = 0; i < MAX_SHOTS; i++) g_shots[i].x *= k;
		for (int i = 0; i < MAX_PARTS; i++) g_parts[i].x *= k;
		for (int i = 0; i < MAX_RINGS; i++) g_rings[i].x *= k;
		for (int i = 0; i < MAX_POPS; i++) g_pops[i].x *= k;
		for (int i = 0; i < g_nstars; i++) g_stars[i].x *= k;
		ship.x *= k; ufo.x *= k;
	}
	if (!g_nstars) init_stars ();
	g_redraw = 1;
}

static long on_resize (long w, long h)
{
	(void) w; (void) h;
	apply_size ();
	return 0;
}

// ---- les commandes -------------------------------------------------------------------------------------------------

static void release_all (void)
{
	k_left = k_right = k_up = k_down = k_fire = k_fire_edge = k_hyper = 0;
	m_left = m_fire_edge = 0;
}

static void set_pause (int on)
{
	if (on && g_state != ST_PLAY) return;
	g_paused = on;
	g_psel = 0;
	release_all ();
	if (on) { snd_thrust (0); snd_ufo (0); }
	g_redraw = 1;
}

static void toggle_sound (void)
{
	g_sound = !g_sound;
	snd_enable (g_sound);
	ui_menu_state (3, g_sound ? UMS_CHECKED : 0);
	g_redraw = 1;
}

static void start_or_restart (void) { new_game (); }

static void pause_act (int i)
{
	if (i == PM_RESUME) set_pause (0);
	else if (i == PM_RESTART) { set_pause (0); new_game (); }
	else if (i == PM_QUIT) g_quit = 1;
}

static void command (int id)
{
	switch (id)
	{
	case 1: start_or_restart (); break;
	case 2: if (g_state == ST_PLAY) set_pause (!g_paused); break;
	case 3: toggle_sound (); break;
	case 4: ui_maximize (); break;
	}
}

static int can_start (void) { return g_state == ST_TITLE || (g_state == ST_OVER && g_over_t > 1.2f); }

static void on_key (int down, int code, int ch)
{
	int c = ch | 0x20;
	if (g_paused)
	{
		if (!down) return;
		if (code == KEY_UP) g_psel = (g_psel + PM_N - 1) % PM_N;
		else if (code == KEY_DOWN) g_psel = (g_psel + 1) % PM_N;
		else if (code == KEY_ENTER || code == KEY_SPACE) pause_act (g_psel);
		else if (code == KEY_ESC || c == 'p') set_pause (0);
		g_redraw = 1;
		return;
	}
	switch (code)
	{
	case KEY_LEFT: k_left = down; if (down) g_mouse_mode = 0; return;
	case KEY_RIGHT: k_right = down; if (down) g_mouse_mode = 0; return;
	case KEY_UP: k_up = down; if (down) g_mouse_mode = 0; return;
	case KEY_DOWN: if (down && !k_down) k_hyper = 1; k_down = down; return;
	case KEY_SPACE:
		if (down && !k_fire) k_fire_edge = 1;
		k_fire = down;
		if (down && can_start ()) new_game ();
		return;
	case KEY_ENTER: if (down && can_start ()) new_game (); return;
	case KEY_ESC: if (down && g_state == ST_PLAY) set_pause (1); return;
	case KEY_F2: if (down) new_game (); return;
	}
	if (!down) return;
	if (c == 'p') set_pause (g_state == ST_PLAY);
	else if (c == 's') toggle_sound ();
	else if (c == 'h') k_hyper = 1;
}

static void on_mouse (int kind, int x, int y, int buttons)
{
	if (kind == 2) { m_left = 0; return; }		// relâché (comme les jeux du système)
	if (kind == 0)
	{
		if (m_seen) m_moved += (float) (abs (x - m_x) + abs (y - m_y));
		if (m_moved > 8) { g_mouse_mode = 1; m_moved = 0; }
		m_x = x; m_y = y; m_seen = 1;
		return;
	}
	if (kind != 1) return;
	m_x = x; m_y = y; m_seen = 1;
	if (g_paused)
	{
		if (!(buttons & 1)) return;
		if (x >= g_pm_x && x < g_pm_x + g_pm_w && y >= g_pm_y)
		{
			int i = (y - g_pm_y) / g_pm_step;
			if (i < PM_N && y - g_pm_y - i * g_pm_step < g_pm_h) { g_psel = i; pause_act (i); }
		}
		g_redraw = 1;
		return;
	}
	if (g_pb_s && (buttons & 1) && x >= g_pb_x && y >= g_pb_y && x < g_pb_x + g_pb_s && y < g_pb_y + g_pb_s)
	{
		set_pause (1);
		return;
	}
	if (can_start ())
	{
		if ((buttons & 1) && y > 36) { new_game (); g_mouse_mode = 1; }
		return;
	}
	g_mouse_mode = 1;
	if (buttons & 1) { if (!m_left) m_fire_edge = 1; m_left = 1; }
	else if (buttons & 2) k_hyper = 1;
}

static void one_event (long t)
{
	if (t == WEV_CLOSE) { g_quit = 1; return; }
	if (t == WEV_MENU) { command (ui_ev_field (&ui, EV_CODE)); return; }
	if (t == WEV_FOCUS)
	{
		if (ui_ev_field (&ui, EV_KIND) == 0) { release_all (); if (g_state == ST_PLAY && !g_paused) set_pause (1); }
		return;
	}
	if (t == WEV_KEY)
	{
		int down = ui_ev_field (&ui, EV_KIND) != 0;
		if (down)
		{
			long id = ui_menu_key (0);
			if (id >= 0) { command ((int) id); return; }
		}
		on_key (down, ui_ev_field (&ui, EV_CODE), ui_ev_field (&ui, EV_CHAR));
		return;
	}
	if (t == WEV_MOUSE)
		on_mouse (ui_ev_field (&ui, EV_KIND), ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), ui_ev_field (&ui, EV_CODE));
	long code = 0, taken = 0;
	uikit_w_event (&code, &taken);		// la taille de la fenêtre, son déplacement par le haut
	if (t == WEV_RESIZE || t == WEV_SCREEN) apply_size ();
}

// ---- le programme ----------------------------------------------------------------------------------------------------

static const char MENU[] =
	"MPartie\n"
	"I1\tNouvelle partie\tF2\n"
	"I2\tPause\tP\n"
	"-\n"
	"I3c\tSon\tS\n"
	"I4\tAgrandir la fenêtre\tF11\n";

int main (int argc, char **argv)
{
	(void) argc; (void) argv;
	if (!uikit_open (&ui)) return 1;
	/* (les textes : lang/en.txt, dans le dictionnaire du systeme) */
	s_jet = (const unsigned *) ui_tokens ();
	g_seed ^= now_ms () * 2654435761u;
	if (!g_seed) g_seed = 1;

	long scr[4] = { 1280, 800, 0, 0 };
	ui_screen ((long) scr);
	long w = scr[0] - 80, h = scr[1] - scr[2] - scr[3] - 60;
	if (w > 1180) w = 1180;
	if (h > 800) h = 800;
	if (w < 640) w = 640;
	if (h < 480) h = 480;
	if (ui_window_ex (-1, -1, w, h, (long) "Asteroids", WF_RESIZABLE | WF_BODY_DARK | WF_GLYPH_LIGHT) <= 0) return 1;
	ui_min_size (640, 480);
	ui_on_resize ((long) on_resize, 0);
	ui_menu ((long) MENU);
	ui_menu_state (3, UMS_CHECKED);
	ui_cursor (UC_CROSS);

	g_sound_ok = snd_init ();
	load_best ();
	apply_size ();
	attract_mode ();

	unsigned last = now_ms ();
	while (!g_quit)
	{
		long t;
		while ((t = ui_event (0)) > 0) { one_event (t); if (g_quit) break; }
		if (g_quit) break;
		if ((int) ui.w != g_W || (int) ui.h != g_H) apply_size ();
		unsigned now = now_ms ();
		float dt = (float) (now - last) / 1000.0f;
		last = now;
		if (dt > 0.05f) dt = 0.05f;		// une pause (menu, fenêtre cachée) : pas de saut

		if (g_paused)
		{
			// en pause : on ne redessine que si quelque chose change
			if (g_redraw) { draw (); ui_update_all (); g_redraw = 0; }
			long e = ui_event (100);
			if (e > 0) one_event (e);
			last = now_ms ();
			continue;
		}
		step (dt);
		draw ();
		ui_update_all ();
		g_redraw = 0;
		if (ui_frame_request ())
		{
			for (unsigned t0 = now_ms (); !g_quit && now_ms () - t0 < 200; )
			{
				long e = ui_event (10);
				if (e == WEV_FRAME) break;
				if (e > 0) one_event (e);
			}
		}
		else
		{
			long e = ui_event (12);
			if (e > 0) one_event (e);
		}
	}
	snd_quit ();
	return 0;
}
