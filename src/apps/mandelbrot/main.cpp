/*
 * mandelbrot -- Fractales : un explorateur de fractales. Mandelbrot, Julia (choix du point a la souris), Burning Ship,
 * Tricorn, Multibrot 3 a 5, Newton, Phoenix, Lyapunov (suite AB a soi), attracteurs de Clifford et de De Jong,
 * Buddhabrot.
 *
 * Port en C++ depuis Onyx (user/Apps/mandelbrot/main.cpp, le « fractal browser » : Mandelbrot, Julia, Burning Ship,
 * Tricorn, centre et largeur de la vue, reset_view, le budget d'iterations qui monte avec le zoom) et de
 * mandelbrot.asm + kernels.inc de Korin, qui en ont fait bien plus et dont le comportement fait foi : memes types,
 * memes vues de depart, memes palettes, signets, widgets, raccourcis, textes et PNG. Un seul source pour le Pi et le PC ;
 * les calculs sont dans kernels.cpp (memes bits que l'asm). L'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 *
 * Les calculs tournent sur tous les coeurs (threads worker). Le rendu est progressif : un point sur 8, puis 2, puis
 * tous ; un changement de vue arrete les threads (g_gen) et repart. Chaque point garde sa valeur (g_mu) : changer de
 * palette ou la faire tourner ne recalcule rien. Les attracteurs et le Buddhabrot comptent les points par pixel dans
 * g_mu, par lots, et l'image se rehausse tant qu'ils tournent.
 *
 * Molette ou doigt vertical : zoom, sur le point vise. Glisser : deplacer. Double toucher : zoom x 2 ; bouton droit :
 * recule. Touches : fleches, + -, R reinitialiser, A animer, P palette suivante, [ ] etirer la palette.
 */
#pragma GCC optimize ("fp-contract=off")	/* (la vue se calcule comme dans l'asm : pas de fmadd) */
#include <armpi/syscall.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <string>
#include <uikit.h>
#include <archivekit_abi.h>
#include "fractal.h"

#define WIN_W		800
#define WIN_H		540
#define TOP		44			/* la barre du haut */
#define FOOT		38			/* la barre du bas */

#define KEY_R		0x15
#define KEY_A		0x04
#define KEY_P		0x13
#define KEY_MINUS	0x2D
#define KEY_PLUS	0x2E			/* (touche =) */
#define KEY_LB		0x2F
#define KEY_RB		0x30
#define KEY_KPMINUS	0x56
#define KEY_KPPLUS	0x57			/* (les fleches : KEY_LEFT... de uikit) */

#define SYS_TIME_MS_	0x19

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static unsigned long time_ms (void) { return (unsigned long) armpi_syscall (SYS_TIME_MS_, 0, 0, 0, 0, 0, 0); }

/* ---- les types : couleurs, centre, largeur, 1 / log2 (degre), iterations, palette de depart (-1 : la courante) ---- */
struct ftype { long mode; double cx, cy, span, ild; long iter, pal; };
static const struct ftype ftab[T_COUNT] = {
	{ C_SMOOTH, -0.6, 0.0, 3.4, 1.0, 256, -1 },			/* Mandelbrot */
	{ C_SMOOTH, 0.0, 0.0, 3.4, 1.0, 256, -1 },			/* Julia */
	{ C_SMOOTH, -0.45, -0.5, 3.6, 1.0, 256, 1 },			/* Burning Ship */
	{ C_SMOOTH, -0.3, 0.0, 3.6, 1.0, 256, -1 },			/* Tricorn */
	{ C_SMOOTH, 0.0, 0.0, 3.2, 0.6309297535714574, 256, -1 },	/* Multibrot 3 */
	{ C_SMOOTH, 0.0, 0.0, 3.0, 0.5, 256, -1 },			/* Multibrot 4 */
	{ C_SMOOTH, 0.0, 0.0, 3.0, 0.43067655807339306, 256, -1 },	/* Multibrot 5 */
	{ C_NEWTON, 0.0, 0.0, 4.0, 1.0, 64, 6 },			/* Newton */
	{ C_SMOOTH, 0.0, 0.0, 3.2, 1.0, 400, 4 },			/* Phoenix */
	{ C_LYAP, 3.0, 3.0, 2.0, 1.0, 240, 3 },				/* Lyapunov */
	{ C_HIST, 0.0, 0.0, 7.5, 1.0, 1000, 1 },			/* Clifford */
	{ C_HIST, 0.0, 0.0, 7.0, 1.0, 1000, 5 },			/* De Jong */
	{ C_HIST, 0.0, -0.4, 4.2, 1.0, 1000, 5 },			/* Buddhabrot */
};

static const double k_wheel[2] = { 0.8, 1.25 };
static const double k_min = 1.0e-13, k_max = 40.0;
static const double k_phx[2] = { 0.5667, -0.5 };
static const double k_jul[2] = { -0.8, 0.156 };
static const double k_fit = 1.12;
static const double k_orb[2] = { 0.07, 0.11 };
static const float k_scale[4] = { 28.0f, 0.0f, 900.0f, 0.0f };	/* g_scale selon la facon de colorer */
static const double kp_cliff[4][4] = {
	{ 1.5, -1.8, 1.6, 0.9 }, { -1.4, 1.6, 1.0, 0.7 }, { 1.7, 1.7, 0.6, 1.2 }, { -1.8, -2.0, -0.5, -0.9 } };
static const double kp_dejong[4][4] = {
	{ 1.4, -2.3, 2.4, -2.1 }, { -2.0, -2.0, -1.2, 2.0 }, { 0.97, -1.899, 1.381, -1.506 }, { 2.01, -2.53, 1.61, -0.33 } };

/* les signets : type, palette, centre, largeur, c (Julia) ou a..d (attracteurs), suite (Lyapunov) */
struct mark { long type, pal; double cx, cy, span, p[4]; const char *seq; };
static const struct mark marks[] = {
	{ T_MANDEL, 0, -0.7436447860, 0.1318252536, 0.00035, { 0, 0, 0, 0 }, 0 },
	{ T_MANDEL, 1, 0.2925, 0.0149, 0.012, { 0, 0, 0, 0 }, 0 },
	{ T_MANDEL, 2, -0.1011, 0.9563, 0.02, { 0, 0, 0, 0 }, 0 },
	{ T_MANDEL, 3, -1.7497, 0.0, 0.03, { 0, 0, 0, 0 }, 0 },
	{ T_JULIA, 5, 0.0, 0.0, 3.2, { -0.8, 0.156, 0, 0 }, 0 },
	{ T_JULIA, 0, 0.0, 0.0, 3.2, { -0.123, 0.745, 0, 0 }, 0 },
	{ T_JULIA, 4, 0.0, 0.0, 3.2, { 0.285, 0.01, 0, 0 }, 0 },
	{ T_JULIA, 1, 0.0, 0.0, 3.2, { -0.4, 0.6, 0, 0 }, 0 },
	{ T_SHIP, 1, -1.7625, -0.0285, 0.08, { 0, 0, 0, 0 }, 0 },
	{ T_LYAP, 3, 3.0, 3.0, 2.0, { 0, 0, 0, 0 }, "AB" },
	{ T_LYAP, 2, 3.7, 2.95, 0.6, { 0, 0, 0, 0 }, "BBBBBBAAAAAA" },
	{ T_LYAP, 4, 3.4, 3.6, 0.8, { 0, 0, 0, 0 }, "AABAB" },
	{ T_CLIFFORD, 2, 0.0, 0.0, 7.5, { -1.4, 1.6, 1.0, 0.7 }, 0 },
	{ T_CLIFFORD, 1, 0.0, 0.0, 7.5, { 1.5, -1.8, 1.6, 0.9 }, 0 },
	{ T_DEJONG, 5, 0.0, 0.0, 7.0, { 1.4, -2.3, 2.4, -2.1 }, 0 },
	{ T_BUDDHA, 1, 0.0, -0.4, 4.2, { 0, 0, 0, 0 }, 0 },
};

#define NPAL 8
/* huit arrets par palette, la boucle se ferme */
static const uint32_t palettes[NPAL][8] = {
	{ 0x000A1450, 0x00206BCB, 0x00EDFFFF, 0x00FFAA00, 0x00803000, 0x00100800, 0x00300860, 0x000A1450 },	/* Aurore */
	{ 0x00000000, 0x00500000, 0x00C02000, 0x00FF8000, 0x00FFE060, 0x00FFFFFF, 0x00FFC030, 0x00802000 },	/* Feu */
	{ 0x00001020, 0x00003060, 0x000060A0, 0x0040B0D0, 0x00C0F0F0, 0x0040B0D0, 0x000060A0, 0x00003060 },	/* Ocean */
	{ 0x00100800, 0x00402000, 0x00906010, 0x00E0B030, 0x00FFF0A0, 0x00E0B030, 0x00906010, 0x00402000 },	/* Or */
	{ 0x00000020, 0x004000C0, 0x00E000E0, 0x0000E0E0, 0x00FFFFFF, 0x00E0E000, 0x00E00080, 0x00200040 },	/* Electrique */
	{ 0x00102040, 0x00305880, 0x0070A0C8, 0x00C0E0F0, 0x00FFFFFF, 0x00C0E0F0, 0x0070A0C8, 0x00305880 },	/* Givre */
	{ 0x00FF0000, 0x00FF9000, 0x00F0F000, 0x0000D000, 0x0000C0F0, 0x000040F0, 0x008000E0, 0x00F00090 },	/* Arc-en-ciel */
	{ 0x00000000, 0x00383838, 0x00808080, 0x00D0D0D0, 0x00FFFFFF, 0x00D0D0D0, 0x00808080, 0x00383838 },	/* Noir et blanc */
};

static const char s_types[] = "Mandelbrot|Julia|Burning Ship|Tricorn|Multibrot 3|Multibrot 4|Multibrot 5|Newton|Phoenix|"
	"Lyapunov|Clifford|De Jong|Buddhabrot";
static const char s_pals[] = "Aurore|Feu|Océan|Or|Électrique|Givre|Arc-en-ciel|Noir et blanc";
static const char s_marks[] = "Signets...|Hippocampes|Éléphants|Spirales|Mini-ensemble|Dragon|Lapin de Douady|Cercles|"
	"Éclair|Armada|Lyapunov AB|Zircon|Spirales AABAB|Clifford 1|Clifford 2|De Jong|Bouddha";

/* ---- l'etat (les noms de l'asm) ---- */
static const unsigned *g_tok;
static long w_type, w_pal, w_mark, w_reset, w_anim, w_png, w_status, w_iter, w_pick, w_seq, w_other;
static long g_sf, g_pitch;
static uint32_t *g_px;
static long g_base = 256, g_palnum, g_anim, g_picking, g_drag, g_moved, g_lx, g_ly, g_mx, g_my, g_mark_ok, g_pnum;
static unsigned long g_tap, g_t0, g_lasth, g_lasts, g_lastf;
static double g_cx, g_cy, g_span = 3.0, g_span0 = 3.0, g_zoom;	/* la vue */
static char g_seqtext[64] = "AB";
static char g_path[256];
static int g_ark;					/* ArchiveKit ouvert (le PNG) */

/* ce que les calculs lisent (fractal.h) */
long g_vw, g_vh, g_type, g_mode, g_maxit = 256, g_seqn = 2;
unsigned long g_offset;
double g_x0, g_y0, g_dx, g_cr = -0.8, g_ci = 0.156, g_ild = 1.0, g_pa[4] = { -1.4, 1.6, 1.0, 0.7 };
float g_scale = 28.0f;
uint32_t g_lut[LUT_N];
unsigned char g_seq[64] = { 0, 1 };
void *g_mu;
struct orbit_state g_orb[NTH];

/* le travail : les threads prennent les lignes (ou les lots) une a une */
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cnd = PTHREAD_COND_INITIALIZER;
static std::atomic<long> g_active, g_finished, g_row;
static std::atomic<unsigned long> g_tend;
static long g_gen, g_nrows, g_step = 8, g_inflight;
static float *g_rowv[NTH];				/* les valeurs d'une ligne, puis ses couleurs */
static uint32_t *g_rowc[NTH];

static unsigned tok (int off) { return g_tok[off / 4]; }

static void status (void);
static void restart (void);

/* ---- les threads ---- */

/* (mutex pris) g_nrows selon g_step */
static void pass_rows (void) { g_nrows = (g_vh + g_step - 1) / g_step; }

/* row = ligne de l'image, step = pas ; me = numero du thread */
static void do_row (long me, long row, long step)
{
	float *vals = g_rowv[me];
	uint32_t *cols = g_rowc[me];
	const long vw = g_vw, vh = g_vh;
	long pts = (vw + step - 1) / step;
	double y = (double) row * g_dx;
	y = y + g_y0;
	row_kernel (g_type, vals, pts, g_maxit, g_x0, y, (double) step * g_dx);
	color_line (vals, pts, cols);
	float *mu = (float *) g_mu + row * vw;
	uint32_t *px = (uint32_t *) ((char *) g_px + row * g_pitch);
	for (long r = 0; r < step && row + r < vh; r++, mu += vw, px = (uint32_t *) ((char *) px + g_pitch))
		for (long x = 0; x < vw; x++)
		{
			mu[x] = vals[x / step];
			px[x] = cols[x / step];
		}
}

static void *worker (void *arg)
{
	long me = (long) arg;
	pthread_mutex_lock (&g_mtx);
	for (;;)
	{
		if (!g_active || g_row >= g_nrows)
		{
			pthread_cond_wait (&g_cnd, &g_mtx);
			continue;
		}
		long row = g_row++;
		g_inflight++;
		long step = g_step, gen = g_gen;
		pthread_mutex_unlock (&g_mtx);
		if (g_mode == C_HIST)
			hist_kernel (g_type, me);
		else
			do_row (me, row * step, step);
		pthread_mutex_lock (&g_mtx);
		long left = --g_inflight;
		if (g_gen != gen)
		{
			pthread_cond_broadcast (&g_cnd);	/* (stop_job attend) */
			continue;
		}
		if (left || g_row < g_nrows)
			continue;
		if (g_step == 1)			/* passe finie : 8, 2, 1 */
		{
			g_active = 0;
			g_finished = 1;
			g_tend = time_ms ();
		}
		else
		{
			g_step = g_step == 2 ? 1 : 2;
			g_row = 0;
			pass_rows ();
		}
		pthread_cond_broadcast (&g_cnd);
	}
	return 0;
}

static void start_threads (void)
{
	long n = sysconf (_SC_NPROCESSORS_ONLN);
	if (n < 1)
		n = 1;
	if (n > NTH)
		n = NTH;
	for (long i = 0; i < n; i++)
	{
		struct orbit_state *o = &g_orb[i];	/* l'etat : deux orbites de depart, le hasard */
		o->rng = (time_ms () + ((unsigned long) i << 32) + 1) * 0x9E3779B97F4A7C15ul;
		double k = (double) (i + 1);
		o->x[0] = k * k_orb[0];
		o->x[1] = -o->x[0];
		o->y[0] = k * k_orb[1];
		o->y[1] = -o->y[0];
		o->orb = (double *) malloc (ORB_MAX * 2 * sizeof (double));
		g_rowv[i] = (float *) malloc ((ROW_MAX + ROW_PAD) * sizeof (float));
		g_rowc[i] = (uint32_t *) malloc ((ROW_MAX + ROW_PAD) * sizeof (uint32_t));
		pthread_t t;
		if (!o->orb || !g_rowv[i] || !g_rowc[i] || pthread_create (&t, 0, worker, (void *) i))
			break;
		pthread_detach (t);
	}
}

/* arrete le travail en cours : apres, aucun thread ne touche a l'image */
static void stop_job (void)
{
	pthread_mutex_lock (&g_mtx);
	g_active = 0;
	g_gen++;
	while (g_inflight)
		pthread_cond_wait (&g_cnd, &g_mtx);
	pthread_mutex_unlock (&g_mtx);
}

/* repart : la vue g_cx, g_cy, g_span */
static void restart (void)
{
	stop_job ();
	double vw = (double) g_vw, vh = (double) g_vh;	/* la vue en pixels */
	double dx = g_span / vw;
	vw = vw * 0.5;
	vw = vw * dx;
	vh = vh * 0.5;
	vh = vh * dx;
	g_x0 = g_cx - vw;
	g_y0 = g_cy - vh;
	g_dx = dx;
	double z = g_span0 / g_span;			/* les iterations suivent le zoom */
	g_zoom = z <= 1.0 ? 0.0 : log2d (z);
	unsigned long it = (unsigned long) g_base;
	if (g_mode == C_SMOOTH)				/* base + 1/5 par doublement */
	{
		double b = (double) it;
		double v = g_zoom * b;
		v = v / 5.0;
		v = v + b;
		it = v >= 18446744073709551615.0 ? ~0ul : (unsigned long) v;
	}
	if (it > ORB_MAX - 2)
		it = ORB_MAX - 2;
	g_maxit = (long) it;
	if (g_mode == C_HIST)
	{
		memset (g_mu, 0, (size_t) (g_vw * g_vh) * 4);	/* on recompte */
		g_step = 1;
		g_nrows = g_type == T_BUDDHA ? 60000 : 4000;
	}
	else
	{
		g_step = 8;
		pass_rows ();
	}
	g_row = 0;
	g_finished = 0;
	g_t0 = time_ms ();
	pthread_mutex_lock (&g_mtx);
	g_active = 1;
	pthread_cond_broadcast (&g_cnd);
	pthread_mutex_unlock (&g_mtx);
	status ();
}

/* ---- les couleurs ---- */

static void set_palette (long p)
{
	g_palnum = p;
	set_lut (palettes[p]);
}

static void repaint_now (void)
{
	if (g_mode == C_HIST)
	{
		color_hist (g_px, g_pitch);
		return;
	}
	const float *mu = (const float *) g_mu;
	uint32_t *px = g_px;
	for (long y = g_vh; y; y--, mu += g_vw, px = (uint32_t *) ((char *) px + g_pitch))
		color_line (mu, g_vw, px);
}

/* toutes les couleurs, puis a l'ecran */
static void repaint (void)
{
	repaint_now ();
	ui_surface_damage (g_sf, 0, 0, 0, 0);
}

/* ---- la ligne d'etat ---- */

/* un morceau de texte traduit ; les espaces autour restent ici (un dictionnaire ne garde pas ceux d'une cle) */
static void tr_cat (std::string &s, const char *t)
{
	while (*t == ' ')
		s += *t++;
	size_t n = strlen (t), k = n;
	while (k && t[k - 1] == ' ')
		k--;
	std::string key (t, k);
	s += TR (key.c_str ());
	s += t + k;
}

static void status (void)
{
	std::string s;
	if (g_picking)
		tr_cat (s, "Touchez un point : la fractale de Julia de ce point.");
	else
	{
		tr_cat (s, "Zoom ");
		unsigned long z = 1;			/* 2^zoom */
		if (g_zoom > 0.0)
		{
			unsigned long e = (unsigned long) g_zoom;
			z = 1ul << (e > 50 ? 50 : e);
		}
		s += std::to_string (z);
		if (g_mode != C_HIST)
		{
			tr_cat (s, "   Itérations ");
			s += std::to_string (g_maxit);
		}
		else
		{
			tr_cat (s, "   Points ");
			s += std::to_string ((unsigned long) g_row * (g_type == T_BUDDHA ? BUD_N * 2 : ATTR_N * 2) / 1000);
			s += " 000";
		}
		if (g_active)
			tr_cat (s, "   (calcul...)");
		else
		{
			unsigned long t = g_tend, now = time_ms ();
			if (!(t > g_t0))
				t = now;
			s += "   ";
			s += std::to_string (t - g_t0);
			s += " ms";
		}
	}
	ui_w_set_text (w_status, (long) s.c_str ());
}

/* ---- les types, les vues ---- */

static void default_scale (void) { g_scale = k_scale[g_mode]; }

/* (Clifford, De Jong) la vue qui montre tout l'attracteur */
static void attr_fit (void)
{
	if (g_type != T_CLIFFORD && g_type != T_DEJONG)
		return;
	double b[4];
	attr_bounds (g_type, b);
	double cx = b[0] + b[1], cy = b[2] + b[3];	/* centre */
	cx = cx * 0.5;
	cy = cy * 0.5;
	double w = b[1] - b[0], h = b[3] - b[2];	/* largeurs */
	double r = (double) g_vw / (double) g_vh;
	h = h * r;					/* la hauteur, en largeur */
	if (h > w)
		w = h;
	w = w * k_fit;
	g_cx = cx;
	g_cy = cy;
	g_span = w;
	g_span0 = w;
}

/* retour a la vue de depart du type */
static void reset_view (void)
{
	const struct ftype *f = &ftab[g_type];
	g_cx = f->cx;
	g_cy = f->cy;
	g_span = f->span;
	g_span0 = f->span;
	default_scale ();
	g_mark_ok = 1;
	attr_fit ();
	restart ();
}

/* g_pnum (modulo le nombre de jeux du type) -> g_pa */
static void load_params (void)
{
	const double *p = (g_type == T_CLIFFORD ? kp_cliff : kp_dejong)[g_pnum & 3];
	memcpy (g_pa, p, sizeof g_pa);
}

/* g_seqtext -> g_seq (0 : a, 1 : b), g_seqn ; sans lettre : AB */
static void parse_seq (void)
{
	long n = 0;
	for (const char *p = g_seqtext; *p; p++)
	{
		unsigned c = (unsigned) ((*p | 0x20) - 'a') & 0xFF;
		if (c <= 1)
			g_seq[n++] = (unsigned char) c;
	}
	if (!n)
	{
		g_seq[0] = 0;
		g_seq[1] = 1;
		n = 2;
	}
	g_seqn = n;
}

/* les reglages du type g_type (couleurs, iterations) ; les widgets du bas */
static void apply_type (void)
{
	long t = g_type;
	const struct ftype *f = &ftab[t];
	g_mode = f->mode;
	g_ild = f->ild;
	g_base = f->iter;
	ui_w_set_value (w_iter, g_base);
	if (t == T_PHOENIX)				/* les constantes de Phoenix */
	{
		g_cr = k_phx[0];
		g_ci = k_phx[1];
	}
	ui_w_show (w_pick, t == T_JULIA || t == T_MANDEL);
	ui_w_show (w_seq, t == T_LYAP);
	ui_w_show (w_other, t == T_CLIFFORD || t == T_DEJONG);
	if (t == T_LYAP)
		ui_w_set_text (w_seq, (long) g_seqtext);
	if (t == T_CLIFFORD || t == T_DEJONG)
		load_params ();				/* les constantes de l'attracteur */
	ui_fill (0, ui.h - FOOT, ui.w, FOOT, tok (TJ_FACE));
	ui_w_draw (0);
	ui_update_all ();
}

static void set_pal_widget (long p)
{
	ui_w_set_value (w_pal, p);
	ui_w_draw (w_pal);
}

static void set_type_widget (long t)
{
	ui_w_set_value (w_type, t);
	ui_w_draw (w_type);
}

/* t = type : sa vue de depart */
static void set_type (long t)
{
	stop_job ();
	g_type = t;
	g_picking = 0;
	if (t == T_JULIA)				/* Julia : son c de depart */
	{
		g_cr = k_jul[0];
		g_ci = k_jul[1];
	}
	long p = ftab[t].pal;				/* sa palette */
	if (p >= 0)
	{
		set_palette (p);
		set_pal_widget (p);
	}
	apply_type ();
	reset_view ();
}

/* i = signet (1..) : sa vue */
static void go_mark (long i)
{
	const struct mark *m = &marks[i - 1];
	stop_job ();
	g_type = m->type;
	g_picking = 0;
	apply_type ();
	set_type_widget (g_type);
	g_cx = m->cx;
	g_cy = m->cy;
	g_span = m->span;
	g_span0 = ftab[g_type].span;
	if (g_type == T_JULIA)
	{
		g_cr = m->p[0];
		g_ci = m->p[1];
	}
	if (g_type == T_CLIFFORD || g_type == T_DEJONG)
		memcpy (g_pa, m->p, sizeof g_pa);
	if (g_type == T_LYAP)
	{
		snprintf (g_seqtext, sizeof g_seqtext, "%s", m->seq);
		ui_w_set_text (w_seq, (long) g_seqtext);
		parse_seq ();
	}
	set_palette (m->pal);
	set_pal_widget (g_palnum);
	default_scale ();
	attr_fit ();
	ui_w_set_value (w_mark, 0);			/* le menu revient a « Signets » */
	ui_w_draw (w_mark);
	restart ();
}

/* le bouton du bas sous Mandelbrot, Julia : choisir c */
static void pick_toggle (void)
{
	if (g_type != T_MANDEL)				/* Julia : on repasse par Mandelbrot */
	{
		g_type = T_MANDEL;
		set_type_widget (T_MANDEL);
		stop_job ();
		apply_type ();
		reset_view ();
	}
	g_picking = 1;
	status ();
}

/* (x, y : le pixel) c = ce point : on passe en Julia */
static void pick_at (long x, long y)
{
	double cr = (double) x * g_dx, ci = (double) y * g_dx;
	g_cr = cr + g_x0;
	g_ci = ci + g_y0;
	g_picking = 0;
	stop_job ();
	g_type = T_JULIA;
	apply_type ();
	set_type_widget (T_JULIA);
	reset_view ();
}

/* le bouton « Autres valeurs » : le jeu de constantes suivant */
static void other_params (void)
{
	g_pnum++;
	load_params ();
	reset_view ();
}

/* le champ de la suite AB a change */
static void read_seq (void)
{
	const char *t = (const char *) ui_w_get_text (w_seq);
	size_t i = 0;
	if (t)
		for (; i < 62 && t[i]; i++)
			g_seqtext[i] = t[i];
	g_seqtext[i] = 0;
	parse_seq ();
	restart ();
}

/* x, y = pixel, f = facteur : ce point reste sous le doigt */
static void zoom_at (long x, long y, double f)
{
	double s = g_span * f;
	if (s < k_min || s > k_max)
		return;
	double ox = (double) (x - (g_vw >> 1)), oy = (double) (y - (g_vh >> 1));
	ox = ox * g_dx;					/* ecart au centre */
	oy = oy * g_dx;
	double k = 1.0 - f;
	ox = ox * k;
	oy = oy * k;
	g_cx = g_cx + ox;
	g_cy = g_cy + oy;
	g_span = s;
	restart ();
}

/* ---- PNG : ArchiveKit fait le deflate et les CRC ---- */

static void put32 (unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char) (v >> 24);
	p[1] = (unsigned char) (v >> 16);
	p[2] = (unsigned char) (v >> 8);
	p[3] = (unsigned char) v;
}

/* fd = fichier, name = nom (4 o), data, n = octets */
static void png_chunk (int fd, const char *name, const void *data, long n)
{
	unsigned char b[4];
	put32 (b, (uint32_t) n);
	write (fd, b, 4);
	write (fd, name, 4);
	long crc = ar_crc32 (0, (long) name, 4);
	if (n)
	{
		write (fd, data, (size_t) n);
		crc = ar_crc32 (crc, (long) data, n);
	}
	put32 (b, (uint32_t) crc);
	write (fd, b, 4);
}

static void save_png (void)
{
	long ok = ui_file_save ((long) "SD0:/home/pictures", (long) "fractale.png", (long) g_path, sizeof g_path);
	ui_w_draw (0);
	if (ok != 1)
		return;
	long w = g_vw, h = g_vh, line = 3 * w + 1;	/* octets d'une ligne */
	unsigned char *raw = (unsigned char *) malloc ((size_t) (line * h));
	if (!raw)
		return;
	unsigned char *q = raw;				/* filtre 0, RVB */
	const uint32_t *src = g_px;
	for (long y = 0; y < h; y++, src = (const uint32_t *) ((const char *) src + g_pitch))
	{
		*q++ = 0;
		for (long x = 0; x < w; x++)
		{
			uint32_t c = src[x];
			*q++ = (unsigned char) (c >> 16);
			*q++ = (unsigned char) (c >> 8);
			*q++ = (unsigned char) c;
		}
	}
	long z = g_ark ? ar_deflate_new ((long) raw, line * h, AR_ZLIB, 6) : 0;
	free (raw);
	int fd = z > 0 ? open (g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
	if (fd < 0)
	{
		if (z > 0)
			ar_free (z);
		ui_w_set_text (w_status, (long) TR ("Image non enregistrée."));
		return;
	}
	static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
	write (fd, sig, 8);
	unsigned char ihdr[13] = { 0 };			/* 8 bits, RVB */
	put32 (ihdr, (uint32_t) w);
	put32 (ihdr + 4, (uint32_t) h);
	ihdr[8] = 8;
	ihdr[9] = 2;
	png_chunk (fd, "IHDR", ihdr, 13);
	png_chunk (fd, "IDAT", (const void *) ar_data (z), ar_bytes (z));
	png_chunk (fd, "IEND", 0, 0);
	close (fd);
	ar_free (z);
	ui_w_set_text (w_status, (long) TR ("Image enregistrée."));
}

/* ---- la fenetre ---- */

static void make_widgets (void)
{
	w_type = ui_dropdown (10, 8, 158, 28, (long) s_types);
	w_pal = ui_dropdown (174, 8, 116, 28, (long) TR (s_pals));
	w_mark = ui_dropdown (296, 8, 120, 28, (long) TR (s_marks));
	w_reset = ui_pushbutton (422, 8, 62, 28, (long) "Départ", UB_GHOST);
	w_anim = ui_pushbutton (486, 8, 68, 28, (long) "Animer", UB_GHOST);
	w_png = ui_pushbutton (556, 8, 52, 28, (long) "PNG", UB_GHOST);
	w_status = ui_label (0, 0, 100, 22, 0, ULB_DIM);
	w_iter = ui_numeric (0, 0, 120, 28, 16, 20000, 200);
	ui_w_tip (w_iter, (long) "Itérations de départ (elles montent avec le zoom)");
	w_pick = ui_pushbutton (0, 0, 150, 28, (long) "Choisir c...", 0);
	w_seq = ui_textbox (0, 0, 150, 28, 0);
	ui_w_tip (w_seq, (long) "Suite de A et de B");
	w_other = ui_pushbutton (0, 0, 150, 28, (long) "Autres valeurs", 0);
}

/* w, h = taille de la fenetre : tout a sa place, l'image a la nouvelle taille */
static long on_resize (long w, long h)
{
	stop_job ();
	long vh = h - (TOP + FOOT);
	long vw = (unsigned long) w > ROW_MAX ? ROW_MAX : w;	/* (les tampons des lignes) */
	g_vw = vw;
	g_vh = vh;
	ui_fill (0, 0, w, h, tok (TJ_FACE));
	if (g_sf)
		ui_surface_move (g_sf, 0, TOP, vw, vh);
	else
		g_sf = ui_surface (0, TOP, vw, vh, WS_SF_XRGB8888);
	g_px = (uint32_t *) uikit_call3 (reinterpret_cast<long (*) ()> (reinterpret_cast<void *> (ui_surface_pixels)), g_sf,
		0, 0, 0, &g_pitch, 0);
	free (g_mu);					/* les valeurs */
	g_mu = calloc ((size_t) (vw * vh), 4);
	if (!g_mu)
		exit (0);
	ui_w_move (w_status, 12, h - (FOOT - 8), w - (12 + 120 + 8 + 150 + 16), 22);
	ui_w_move (w_iter, w - 128, h - (FOOT - 5), 120, 28);
	ui_w_move (w_pick, w - (128 + 158), h - (FOOT - 5), 150, 28);
	ui_w_move (w_seq, w - (128 + 158), h - (FOOT - 5), 150, 28);
	ui_w_move (w_other, w - (128 + 158), h - (FOOT - 5), 150, 28);
	ui_w_draw (0);
	ui_update_all ();
	if (g_mark_ok)
		restart ();
	return 0;
}

static void on_widget (long w, long code)
{
	if (!w || !code)
		return;
	if (w == w_type)
	{
		long v;
		if (code == UN_CHANGE && (v = ui_w_get_value (w)) >= 0)
			set_type (v);
	}
	else if (w == w_pal)
	{
		long v;
		if (code == UN_CHANGE && (v = ui_w_get_value (w)) >= 0)
		{
			set_palette (v);
			repaint ();
		}
	}
	else if (w == w_mark)
	{
		long v;
		if (code == UN_CHANGE && (v = ui_w_get_value (w)) > 0)
			go_mark (v);
	}
	else if (w == w_iter)
	{
		if (code == UN_CHANGE)
		{
			g_base = ui_w_get_value (w);
			restart ();
		}
	}
	else if (w == w_seq)
	{
		if (code == UN_CHANGE)
			read_seq ();
	}
	else if (code != UN_ACTIVATE)
		return;
	else if (w == w_reset)
		reset_view ();
	else if (w == w_anim)
	{
		g_anim ^= 1;
		ui_w_style (w_anim, g_anim ? UB_DEFAULT : UB_GHOST);
		ui_w_draw (w_anim);
	}
	else if (w == w_png)
		save_png ();
	else if (w == w_pick)
		pick_toggle ();
	else if (w == w_other)
		other_params ();
}

static void on_key (void)
{
	if (!ui_ev_field (&ui, EV_KIND))
		return;
	long k = ui_ev_field (&ui, EV_CODE);
	switch (k)
	{
	case KEY_R:
		reset_view ();
		return;
	case KEY_A:
		on_widget (w_anim, UN_ACTIVATE);
		return;
	case KEY_P:
	{
		long p = g_palnum + 1;
		if ((unsigned long) p >= NPAL)
			p = 0;
		set_palette (p);
		set_pal_widget (p);
		repaint ();
		return;
	}
	case KEY_LB:
		g_scale = g_scale / 1.5f;
		repaint ();
		return;
	case KEY_RB:
		g_scale = g_scale * 1.5f;
		repaint ();
		return;
	case KEY_PLUS:
	case KEY_KPPLUS:
		zoom_at (g_vw >> 1, g_vh >> 1, 0.5);
		return;
	case KEY_MINUS:
	case KEY_KPMINUS:
		zoom_at (g_vw >> 1, g_vh >> 1, 2.0);
		return;
	}
	long dx = k == KEY_LEFT ? -1 : k == KEY_RIGHT ? 1 : 0;	/* en huitiemes de la largeur */
	long dy = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : 0;
	if (!dx && !dy)
		return;
	double s = 0.125 * g_span;
	g_cx = g_cx + (double) dx * s;
	g_cy = g_cy + (double) dy * s;
	restart ();
}

/* un evenement souris : glisser, double toucher, bouton droit, choix de c */
static void on_mouse (void)
{
	long kind = ui_ev_field (&ui, EV_KIND);
	long x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y) - TOP;
	g_mx = x;
	g_my = y;
	if (kind == 1)
	{
		if (y < 0 || y >= g_vh)
			return;
		if (ui_ev_field (&ui, EV_CODE) & 2)	/* bouton droit : recule */
		{
			zoom_at (x, y, 2.0);
			return;
		}
		g_drag = 1;
		g_moved = 0;
		g_lx = x;
		g_ly = y;
		return;
	}
	if (kind == 2)
	{
		if (!g_drag)
			return;
		g_drag = 0;
		if (g_moved)
			return;
		if (g_picking)				/* choix de c */
		{
			pick_at (x, y);
			return;
		}
		unsigned long now = time_ms (), last = g_tap;	/* deux touchers : zoom x 2 */
		g_tap = now;
		if (now - last >= 400)
			return;
		g_tap = 0;
		zoom_at (x, y, 0.5);
		return;
	}
	if (!g_drag)					/* deplacement */
		return;
	long ddx = x - g_lx, ddy = y - g_ly;
	if (!ddx && !ddy)
		return;
	if (!g_moved)
	{
		if ((unsigned long) (ddx + 3) <= 6 && (unsigned long) (ddy + 3) <= 6)
			return;				/* (un doigt bouge toujours un peu) */
		g_moved = 1;
	}
	g_lx = x;
	g_ly = y;
	double mx = (double) ddx * g_dx, my = (double) ddy * g_dx;
	g_cx = g_cx - mx;
	g_cy = g_cy - my;
	restart ();
}

static void on_wheel (void)
{
	long n = ui_ev_field (&ui, EV_Y);
	if (!n)
		return;
	double k = k_wheel[0];				/* 0.8 par cran */
	if (n < 0)
	{
		k = k_wheel[1];
		n = -n;
	}
	double f = 1.0;
	while (n--)
		f = f * k;
	zoom_at (g_mx, g_my, f);
}

/* une image de la boucle (au plus une toutes les 30 ms) : les couleurs qui tournent, les comptes, l'avancement */
static void frame (void)
{
	unsigned long now = time_ms ();
	if (now - g_lastf >= 30)
	{
		g_lastf = now;
		if (g_anim)
		{
			g_offset += 12;
			repaint_now ();
		}
		else if (g_mode == C_HIST && (g_active || g_finished) && now - g_lasth >= 250)
		{
			g_lasth = now;			/* les comptes : l'image se rehausse */
			repaint_now ();
		}
		if (g_active || g_finished || g_anim)
		{
			ui_surface_damage (g_sf, 0, 0, 0, 0);
			if (g_finished)
			{
				g_finished = 0;
				if (g_mode == C_HIST)	/* (la derniere image des comptes) */
					repaint ();
				status ();
			}
			else
			{
				unsigned long t = time_ms ();	/* (le temps qui passe) */
				if (t - g_lasts >= 500)
				{
					g_lasts = t;
					if (g_active)
						status ();
				}
			}
		}
	}
	ui_view_flush ();
}

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	g_ark = archivekit_kit_open ();
	long scr[4];					/* la taille : ce que l'ecran permet */
	unsigned long w = WIN_W, h = WIN_H;
	if (ui_screen ((long) scr) >= 0)
	{
		unsigned long sw = (unsigned long) (scr[0] - 16), sh = (unsigned long) (scr[1] - scr[2] - scr[3] - 32);
		if (w > sw)
			w = sw;
		if (h > sh)
			h = sh;
	}
	if (ui_window_ex (-1, -1, (long) w, (long) h, (long) TR ("Fractales"), WF_RESIZABLE) < 0)
		return 0;
	g_tok = (const unsigned *) ui_tokens ();
	ui_min_size (640, 400);
	ui_on_resize ((long) on_resize, 0);
	make_widgets ();
	start_threads ();
	set_palette (0);
	on_resize ((long) w, (long) h);
	set_type (T_MANDEL);
	ui_update_all ();
	for (;;)
	{
		long t = ui_event (g_active || g_anim ? 40 : 1000);
		if (t)
		{
			if (t == WEV_CLOSE)
				break;
			long code = 0, taken = 0;
			long wd = uikit_w_event (&code, &taken);
			on_widget (wd, code);
			if (!taken)
			{
				if (t == WEV_MOUSE)
					on_mouse ();
				else if (t == WEV_WHEEL)
					on_wheel ();
				else if (t == WEV_KEY)
					on_key ();
			}
		}
		frame ();
	}
	exit (0);
}
