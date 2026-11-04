//
// image.h -- les images calculees d'Arabesque : le mandala et des fractales classiques (Mandelbrot, Julia, Burning Ship,
// Lyapunov, Newton). Chacune se refait a partir de sa sorte et de sa graine : la graine choisit le cadrage, les
// parametres et la palette, et c'est tout ce que garde un prereglage. Memes calculs que Fractals.cs et Mandala.cs du port
// Windows, dans le meme ordre (l'application se compile sans contraction en FMA : tools/office/build.sh).
//
// Ce qui compose bien : des couleurs qui varient en douceur (le chemin avance par degres conjoints) mais dont les trois
// composantes ne varient pas ensemble (hauteur, duree et nuances restent independantes). D'ou une palette en cosinus a
// phase differente par composante, et un interieur colore par l'orbite plutot que d'une seule teinte : une grande plage
// unie ne donnerait qu'une note repetee.
//
// Le mandala : les images symetriques composent le mieux, le chemin y retrouve les memes couleurs, donc les memes motifs.
//
#ifndef ARABESQUE_IMAGE_H
#define ARABESQUE_IMAGE_H

#include <math.h>
#include <pthread.h>
#include "compose.h"

static const char *const IMG_KINDS[] = { "mandala", "mandelbrot", "julia", "burningship", "lyapunov", "newton", "rays" };
static const char *const IMG_NAMES[] = { "Mandala", "Mandelbrot", "Julia", "Burning Ship", "Lyapunov", "Newton", "Rayons" };
static const int NKINDS = 7;
static int img_kind_index (const char *k) { for (int i = 0; i < NKINDS; i++) if (!strcmp (k, IMG_KINDS[i])) return i; return -1; }

// un reglage d'image : def est la valeur par defaut d'un reglage fixed (que la graine ne tire pas), et sinon la valeur
// moyenne de la graine ; low et high nomment les deux bouts quand le nombre seul ne parle pas ; hidden : regle a la
// souris sur l'image (le cadrage), pas de ligne dans la fenetre des reglages
struct ImgParam { const char *key, *label; double min, max, def; bool isInt, fixed; const char *low, *high; bool hidden; };
static const ImgParam RAYS_PARAMS[] = {
	{ "rays", "Rayons au centre", 6, 120, 0, true, false, 0, 0 },
	{ "rings", "Anneaux", 2, 16, 0, true, false, 0, 0 },
	{ "sat", "Saturation", 0, 1, 0.75, false, false, 0, 0 },
	{ "light", "Luminosité", 0.1, 1, 0.725, false, false, 0, 0 },
	{ "fade", "Dégradé vers l'extérieur", -1, 1, 0, false, true, "plus foncé", "plus clair" },
};
// le cadrage, de toutes les sortes : le centre (dans les coordonnees de la sorte) et le grossissement, a la souris
static const ImgParam VIEW_PARAMS[] = {
	{ "x", "Centre X", -1e6, 1e6, 0, false, false, 0, 0, true },
	{ "y", "Centre Y", -1e6, 1e6, 0, false, false, 0, 0, true },
	{ "zoom", "Zoom", 0.25, 1e12, 1, false, false, 0, 0, true },
};
static const ImgParam RAYS_ALL[] = {
	RAYS_PARAMS[0], RAYS_PARAMS[1], RAYS_PARAMS[2], RAYS_PARAMS[3], RAYS_PARAMS[4], VIEW_PARAMS[0], VIEW_PARAMS[1], VIEW_PARAMS[2],
};
// les reglages d'une sorte ; un reglage laisse de cote, la graine le choisit (sauf fixed : sa valeur par defaut)
static const ImgParam *img_params (const char *kind, int &n)
{
	if (!strcmp (kind, "rays")) { n = (int) (sizeof RAYS_ALL / sizeof RAYS_ALL[0]); return RAYS_ALL; }
	if (img_kind_index (kind) >= 0) { n = 3; return VIEW_PARAMS; }
	n = 0;
	return 0;
}
// ce que lit une image : le reglage donne, sinon la valeur tiree de la graine (le tirage a lieu dans les deux cas, pour
// que regler une chose ne change pas les autres)
struct Knobs
{
	const ImgParam *defs; int n; const ImgSet *set; ImgSet *used;
	double get (const char *key, double drawn) const
	{
		const ImgParam *d = defs;
		for (int i = 0; i < n; i++) if (!strcmp (defs[i].key, key)) d = &defs[i];
		double g, v = set && set->get (key, g) ? (g < d->min ? d->min : g > d->max ? d->max : g) : drawn;
		if (d->isInt) v = nearbyint (v);		// (au pair le plus proche, comme Math.Round)
		used->put (key, v);
		return v;
	}
	bool has (const char *key) const { double g; return set && set->get (key, g); }
	// le cadrage est-il regle ? (sinon l'image se calcule exactement comme avant)
	bool view () const { return has ("x") || has ("y") || has ("zoom"); }
};
// un cadrage en coordonnees d'image (le mandala, les rayons) : le centre de 0 a 1, le grossissement
static void image_view (Image &p, double vx, double vy, double zoom)
{
	p.canView = true;
	p.vox = vx; p.voy = vy; p.vax = 1 / zoom; p.vay = 0; p.vbx = 0; p.vby = 1 / zoom;
}
// un cadrage tourne (Mandelbrot, Julia, Burning Ship, Newton) : le pixel (x, y) est au point
// o + rot ((2x - w) / w * half, (2y - h) / w * half)
static void turned_view (Image &p, int w, int h, double ox, double oy, double half, double cs, double sn)
{
	double ax = 2 * half, ay = 2 * half * h / w;
	p.canView = true;
	p.vox = ox; p.voy = oy;
	p.vax = ax * cs; p.vay = ax * sn; p.vbx = -ay * sn; p.vby = ay * cs;
}

static void hsv (double h, double s, double v, int *c)
{
	h = (h - floor (h)) * 6;
	int i = (int) h;
	double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f)), r, g, b;
	switch (i)
	{
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	c[0] = (int) (r * 255); c[1] = (int) (g * 255); c[2] = (int) (b * 255);
}

static void image_alloc (Image &im, int w, int h)
{
	free (im.px);
	im.px = (uint32_t *) malloc (sizeof (uint32_t) * (size_t) w * (size_t) h);
	im.w = w; im.h = h;
}

// view : le cadrage regle (le centre vx, vy de l'image, de 0 a 1, et le grossissement zoom) ; sans lui, l'image entiere
static void mandala (Image &im, int w, int h, uint32_t seed, bool view = false, double vx = 0.5, double vy = 0.5, double zoom = 1)
{
	Rng r (seed * 2654435761u + 1);
	image_alloc (im, w, h);
	int fold = 5 + r.below (8);			// branches
	int rings = 5 + r.below (6);
	double wob = 0.05 + r.unit () * 0.12, base = r.unit ();
	enum { NCOL = 7 };
	int pal[NCOL][3];
	double hue = base, stepH = 0.09 + r.unit () * 0.3;
	for (int i = 0; i < NCOL; i++)
	{
		// (deux tirages dans l'ordre : dans les arguments d'une fonction, le C++ ne le garantit pas)
		double sat = 0.55 + r.unit () * 0.45, val = 0.45 + r.unit () * 0.55;
		hsv (hue, sat, val, pal[i]);
		hue += stepH;
	}
	double petal[16];
	for (int i = 0; i < 16; i++) petal[i] = r.unit ();
	const double PI2 = 6.283185307179586;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			double dx = (2.0 * x - w) / w, dy = (2.0 * y - h) / h;
			if (view) { dx = 2 * (vx + ((double) x / w - 0.5) / zoom) - 1; dy = 2 * (vy + ((double) y / h - 0.5) / zoom) - 1; }
			double d = sqrt (dx * dx + dy * dy), a = atan2 (dy, dx);
			a = fmod (a + PI2, PI2 / fold) * fold / PI2;	// un secteur, 0..1
			a = a > 0.5 ? 1 - a : a;			// replie : la symetrie miroir
			double rr = d * rings + wob * rings * sin (a * 6.283 * 2 + d * 9);
			int ring = (int) rr;
			double fr = rr - ring;
			int cell = (int) (a * 2 * (2 + ring));
			double pet = petal[(ring * 3 + cell) & 15];
			int k = ((ring * 2 + cell + (pet > 0.5 ? 1 : 0)) % NCOL + NCOL) % NCOL;	// (pres du centre, la couronne peut etre negative)
			const int *c = pal[k], *c2 = pal[(k + 3) % NCOL];
			double t = fr < 0.12 || fr > 0.88 ? 0.0 : pet;		// un filet sombre entre les couronnes
			double dark = fr < 0.06 || fr > 0.94 ? 0.25 : 1.0;
			if (d > 1.0) dark = 0.45;
			int R = (int) ((c[0] * (1 - t) + c2[0] * t) * dark), G = (int) ((c[1] * (1 - t) + c2[1] * t) * dark), B = (int) ((c[2] * (1 - t) + c2[2] * t) * dark);
			im.px[y * w + x] = (uint32_t) R << 16 | (uint32_t) G << 8 | (uint32_t) B;
		}
}

// ---- la palette : a + b cos (2 pi (c t + d)), une phase par composante ----
struct Palette
{
	double a[3], b[3], c[3], d[3];
	explicit Palette (Rng &r)
	{
		double freq = 0.6 + r.unit () * 1.4;
		for (int k = 0; k < 3; k++)
		{
			a[k] = 0.45 + r.unit () * 0.1;
			b[k] = 0.35 + r.unit () * 0.15;
			c[k] = freq * (0.6 + r.unit () * 0.8);
			d[k] = r.unit ();
		}
	}
	uint32_t at (double t, double light = 1) const
	{
		uint32_t o = 0;
		for (int k = 0; k < 3; k++)
		{
			double v = (a[k] + b[k] * cos (6.283185307179586 * (c[k] * t + d[k]))) * light;
			int q = (int) (v * 255);
			o = o << 8 | (uint32_t) (q < 0 ? 0 : q > 255 ? 255 : q);
		}
		return o;
	}
};

// les lignes d'une image, sur quatre fils : chaque pixel ne depend que de lui-meme
struct Rows { void (*fn) (void *, int); void *ctx; int y0, y1; };
static void *rows_run (void *p) { Rows *r = (Rows *) p; for (int y = r->y0; y < r->y1; y++) r->fn (r->ctx, y); return 0; }
static void rows_parallel (int h, void (*fn) (void *, int), void *ctx)
{
	enum { N = 4 };
	Rows part[N]; pthread_t th[N]; bool on[N];
	for (int i = 0; i < N; i++)
	{
		part[i] = Rows { fn, ctx, h * i / N, h * (i + 1) / N };
		on[i] = i > 0 && pthread_create (&th[i], 0, rows_run, &part[i]) == 0;
	}
	rows_run (&part[0]);
	for (int i = 1; i < N; i++) if (on[i]) pthread_join (th[i], 0); else rows_run (&part[i]);
}

// ---- Mandelbrot, Julia, Burning Ship : le temps de fuite ----
// trap : le plus pres de 0 qu'a ete le chemin (pour l'interieur)
static int esc_iter (double zx, double zy, double cx, double cy, bool ship, int max, double &nx, double &ny, double &trap)
{
	int n = 0;
	trap = 1e9;
	while (n < max)
	{
		if (ship) { zx = fabs (zx); zy = fabs (zy); }
		double x2 = zx * zx, y2 = zy * zy;
		if (x2 + y2 > 65536) break;
		if (n > 0 && x2 + y2 < trap) trap = x2 + y2;
		zy = 2 * zx * zy + cy; zx = x2 - y2 + cx;
		n++;
	}
	nx = zx; ny = zy;
	return n;
}
// un point au bord de l'ensemble : c'est la que l'image est riche
static void esc_border (Rng &r, bool ship, int lo, int max, double &ox, double &oy)
{
	for (int tries = 0; tries < 4000; tries++)
	{
		double cx = ship ? -2.2 + r.unit () * 3.2 : -2.1 + r.unit () * 2.7;
		double cy = -1.4 + r.unit () * 2.8;
		double a, b, t;
		int n = esc_iter (0, 0, cx, cy, ship, max, a, b, t);
		if (n >= lo && n < max) { ox = cx; oy = cy; return; }
	}
	ox = ship ? -1.76 : -0.75; oy = ship ? -0.03 : 0.1;
}
static const double SHIP_SPOTS[][3] = {
	{ -0.45, -0.5, 1.9 }, { -1.762, -0.028, 0.06 }, { -1.62, -0.01, 0.15 }, { -1.861, -0.004, 0.02 }, { -1.941, -0.003, 0.012 }, { -1.25, -0.4, 0.35 },
};
struct EscCtx { Image *im; const Palette *pal; bool julia, ship; double cx, cy, ox, oy, half, cs, sn, bands; int max; };
static void esc_row (void *p, int y)
{
	EscCtx &e = *(EscCtx *) p;
	int w = e.im->w, h = e.im->h;
	for (int x = 0; x < w; x++)
	{
		double u = (2.0 * x - w) / w * e.half, v = (2.0 * y - h) / w * e.half;
		double px = e.ox + u * e.cs - v * e.sn, py = e.oy + u * e.sn + v * e.cs;
		double zx, zy, trap;
		int n = e.julia ? esc_iter (px, py, e.cx, e.cy, false, e.max, zx, zy, trap) : esc_iter (0, 0, px, py, e.ship, e.max, zx, zy, trap);
		uint32_t c;
		if (n >= e.max) c = e.pal->at (0.35 + sqrt (sqrt (trap)) * 0.9, 0.6);	// des volutes plutot qu'une plage unie
		else
		{
			double nu = n + 1 - log (log (zx * zx + zy * zy) * 0.5 / log (2.0)) / log (2.0);
			c = e.pal->at (nu / e.bands);
		}
		e.im->px[y * w + x] = c;
	}
}
static void escape (Image &im, int w, int h, uint32_t seed, bool julia, bool ship, const Knobs &k)
{
	Rng r (seed * 2246822519u + (julia ? 7u : ship ? 11u : 3u));
	Palette pal (r);
	EscCtx e;
	e.im = &im; e.pal = &pal; e.julia = julia; e.ship = ship;
	if (julia)
	{
		esc_border (r, false, 25, 300, e.cx, e.cy);
		e.ox = e.oy = 0;
		e.half = 1.6 / pow (10, r.unit () * 0.8);
		e.max = 300;
	}
	else
	{
		double depth = r.unit () * 3.2;				// 0 : l'ensemble entier ; 3 : mille fois plus pres
		if (ship)
		{
			// (le bord du navire est du bruit vu de pres : ses coins connus, a peine decales)
			const double *s = SHIP_SPOTS[r.below ((int) (sizeof SHIP_SPOTS / sizeof SHIP_SPOTS[0]))];
			double z = 0.6 + r.unit () * 0.8;
			double jx = (r.unit () - 0.5) * s[2] * 0.4, jy = (r.unit () - 0.5) * s[2] * 0.4;
			e.ox = s[0] + jx; e.oy = s[1] + jy; e.half = s[2] * z;
			depth = log10 (1.9 / e.half);
		}
		else
		{
			if (depth < 0.35) { e.ox = -0.6; e.oy = 0; }
			else esc_border (r, false, 40, 400, e.ox, e.oy);
			e.half = 1.6 / pow (10, depth);
		}
		e.max = 250 + (int) (depth * 150);
		e.cx = e.cy = 0;
	}
	double rot = r.unit () * 6.283185307179586;
	e.cs = cos (rot); e.sn = sin (rot);
	if (ship) { e.cs = 1; e.sn = 0; }				// le navire a l'endroit (y vers le bas de l'image)
	e.bands = 12 + r.unit () * 30;				// combien d'iterations par tour de palette
	// le cadrage regle : plus pres, plus d'iterations (comme la profondeur tiree)
	e.ox = k.get ("x", e.ox); e.oy = k.get ("y", e.oy);
	double zoom = k.get ("zoom", 1);
	if (k.has ("zoom")) { e.half /= zoom; int m = e.max + (int) (log10 (zoom) * 150); e.max = m > 100 ? m : 100; }
	image_alloc (im, w, h);
	turned_view (im, w, h, e.ox, e.oy, e.half, e.cs, e.sn);
	rows_parallel (h, esc_row, &e);
}

// ---- Lyapunov : la suite logistique, son taux r passant de a a b selon un motif ----
static const char *const LYA_SEQS[] = { "AB", "AABAB", "ABBAB", "AAABB", "BBBBBBAAAAAA", "ABAAB", "AABB", "ABBBAA" };
struct LyaCtx { Image *im; const Palette *pal; const char *seq; int len; double a0, a1, b0, b1; };
static void lya_row (void *p, int y)
{
	LyaCtx &e = *(LyaCtx *) p;
	int w = e.im->w, h = e.im->h;
	for (int x = 0; x < w; x++)
	{
		double ra = e.a0 + (e.a1 - e.a0) * x / (w - 1), rb = e.b1 - (e.b1 - e.b0) * y / (h - 1);
		double v = 0.5, sum = 0;
		for (int i = 0; i < 40; i++) { double k = e.seq[i % e.len] == 'A' ? ra : rb; v = k * v * (1 - v); }
		const int N = 120;
		for (int i = 0; i < N; i++)
		{
			double k = e.seq[(i + 40) % e.len] == 'A' ? ra : rb;
			v = k * v * (1 - v);
			double dd = fabs (k * (1 - 2 * v));
			sum += log (dd < 1e-12 ? 1e-12 : dd);
		}
		double l = sum / N;
		// l'ordre (l < 0) sur une moitie de la palette, le chaos (l > 0) sur l'autre, plus sombre
		double t = l < 0 ? 0.5 * (1 - exp (l)) : 0.5 + 0.5 * (1 - exp (-2 * l));
		e.im->px[y * w + x] = e.pal->at (t * 1.7, l < 0 ? 1 : 0.6);
	}
}
static void lyapunov (Image &im, int w, int h, uint32_t seed, const Knobs &kn)
{
	Rng r (seed * 2246822519u + 13);
	Palette pal (r);
	LyaCtx e;
	e.im = &im; e.pal = &pal;
	e.seq = LYA_SEQS[r.below ((int) (sizeof LYA_SEQS / sizeof LYA_SEQS[0]))];
	e.len = (int) strlen (e.seq);
	e.a0 = 2.0 + r.unit () * 1.6; e.b0 = 2.0 + r.unit () * 1.6;
	double size = 0.25 + r.unit () * 1.2;
	e.a1 = e.a0 + size < 4.0 ? e.a0 + size : 4.0; e.b1 = e.b0 + size < 4.0 ? e.b0 + size : 4.0;
	// le cadrage : le centre (taux a, taux b) et le grossissement ; regle, la fenetre se refait autour
	double cx = kn.get ("x", (e.a0 + e.a1) / 2), cy = kn.get ("y", (e.b0 + e.b1) / 2), zoom = kn.get ("zoom", 1);
	if (kn.view ())
	{
		double hw = (e.a1 - e.a0) / 2 / zoom, hh = (e.b1 - e.b0) / 2 / zoom;
		e.a0 = cx - hw; e.a1 = cx + hw; e.b0 = cy - hh; e.b1 = cy + hh;
	}
	image_alloc (im, w, h);
	im.canView = true;
	im.vox = (e.a0 + e.a1) / 2; im.voy = (e.b0 + e.b1) / 2; im.vax = e.a1 - e.a0; im.vay = 0; im.vbx = 0; im.vby = -(e.b1 - e.b0);
	rows_parallel (h, lya_row, &e);
}

// ---- Newton : z^n = 1, chaque racine sa teinte, le nombre de pas la lumiere ----
struct NewCtx { Image *im; const Palette *pal; int deg; double relax, half, ox, oy, cs, sn, rx[8], ry[8]; };
static void newton_row (void *p, int y)
{
	NewCtx &e = *(NewCtx *) p;
	int w = e.im->w, h = e.im->h, deg = e.deg;
	const int MAX = 60;
	for (int x = 0; x < w; x++)
	{
		double u = (2.0 * x - w) / w * e.half, v = (2.0 * y - h) / w * e.half;
		double zx = e.ox + u * e.cs - v * e.sn, zy = e.oy + u * e.sn + v * e.cs;
		int n = 0, root = -1;
		for (; n < MAX && root < 0; n++)
		{
			// z^(deg-1) puis f = z^deg - 1, f' = deg z^(deg-1)
			double px = 1, py = 0;
			for (int k = 1; k < deg; k++) { double t = px * zx - py * zy; py = px * zy + py * zx; px = t; }
			double fx = px * zx - py * zy - 1, fy = px * zy + py * zx;
			double dx = deg * px, dy = deg * py, den = dx * dx + dy * dy;
			if (den < 1e-300) break;
			zx -= e.relax * (fx * dx + fy * dy) / den; zy -= e.relax * (fy * dx - fx * dy) / den;
			for (int k = 0; k < deg; k++) { double ex = zx - e.rx[k], ey = zy - e.ry[k]; if (ex * ex + ey * ey < 1e-6) { root = k; break; } }
		}
		double light = 1.15 - (n * 0.025 < 0.75 ? n * 0.025 : 0.75);
		e.im->px[y * w + x] = root < 0 ? e.pal->at (0.95, 0.3) : e.pal->at ((double) root / deg + n * 0.013, light);
	}
}
static void newton (Image &im, int w, int h, uint32_t seed, const Knobs &kn)
{
	Rng r (seed * 2246822519u + 17);
	Palette pal (r);
	NewCtx e;
	e.im = &im; e.pal = &pal;
	e.deg = 3 + r.below (4);
	e.relax = 1.0 + r.unit () * 0.7;				// > 1 : des bassins plus enchevetres
	e.half = 1.6 / pow (10, r.unit () * 1.2);
	e.ox = (r.unit () - 0.5) * 0.8; e.oy = (r.unit () - 0.5) * 0.8;
	double rot = r.unit () * 6.283185307179586;
	e.cs = cos (rot); e.sn = sin (rot);
	for (int k = 0; k < e.deg; k++) { e.rx[k] = cos (6.283185307179586 * k / e.deg); e.ry[k] = sin (6.283185307179586 * k / e.deg); }
	e.ox = kn.get ("x", e.ox); e.oy = kn.get ("y", e.oy);
	double zoom = kn.get ("zoom", 1);
	if (kn.has ("zoom")) e.half /= zoom;
	image_alloc (im, w, h);
	turned_view (im, w, h, e.ox, e.oy, e.half, e.cs, e.sn);
	rows_parallel (h, newton_row, &e);
}

// ---- Rayons : des batonnets de couleur qui partent d'un point, en anneaux (dans l'esprit de l'image de depart d'Art Song) ----
// Au centre, n rayons de meme largeur, chacun sa couleur. Tous les deux anneaux, chaque rayon se divise en deux dans
// l'angle : une moitie garde sa couleur, l'autre en prend une nouvelle, le cote alternant d'un rayon a l'autre. Dans un
// meme anneau, les rayons ont donc tous la meme largeur. Le degrade assombrit (< 0) ou eclaircit (> 0) vers le bord.
static void hsv_clamped (double h, double s, double v, int *c)
{
	s = s < 0 ? 0 : s > 1 ? 1 : s; v = v < 0.08 ? 0.08 : v > 1 ? 1 : v;
	hsv (h, s, v, c);
}
struct RaysCtx { Image *im; uint32_t *color[8]; int n, rings, levels; double ring, cx, cy, turn, fade, vx, vy, zoom; bool view; };
static void rays_row (void *p, int y)
{
	RaysCtx &e = *(RaysCtx *) p;
	int w = e.im->w, h = e.im->h;
	for (int x = 0; x < w; x++)
	{
		double dx = (double) x / w - e.cx, dy = (double) y / h - e.cy;
		if (e.view) { dx = e.vx + ((double) x / w - 0.5) / e.zoom - e.cx; dy = e.vy + ((double) y / h - 0.5) / e.zoom - e.cy; }
		double a = atan2 (dy, dx) / 6.283185307179586 + 1 + e.turn;
		a -= floor (a);
		double dist = sqrt (dx * dx + dy * dy) * 2;
		int ri = (int) (dist / e.ring); if (ri > e.rings - 1) ri = e.rings - 1;
		int s = ri / 2; if (s > e.levels - 1) s = e.levels - 1;
		int j = (int) (a * (e.n << s)); if (j > (e.n << s) - 1) j = (e.n << s) - 1;
		uint32_t px = e.color[s][j];
		if (e.fade != 0)
		{
			double f = dist / (e.ring * e.rings), t = e.fade * (f < 1.0 ? f : 1.0);
			int R = (int) (px >> 16) & 255, G = (int) (px >> 8) & 255, B = (int) px & 255;
			if (t < 0) { R = (int) (R * (1 + t)); G = (int) (G * (1 + t)); B = (int) (B * (1 + t)); }
			else { R += (int) ((255 - R) * t); G += (int) ((255 - G) * t); B += (int) ((255 - B) * t); }
			px = (uint32_t) R << 16 | (uint32_t) G << 8 | (uint32_t) B;
		}
		e.im->px[y * w + x] = px;
	}
}
static void rays (Image &im, int w, int h, uint32_t seed, const Knobs &k)
{
	Rng r (seed * 2246822519u + 19);
	RaysCtx e;
	e.im = &im;
	e.n = (int) k.get ("rays", 24 + r.below (40));
	e.rings = (int) k.get ("rings", 5 + r.below (4));
	double sat = k.get ("sat", 0.75), light = k.get ("light", 0.725);
	e.fade = k.get ("fade", 0);
	e.levels = (e.rings + 1) / 2;					// combien de fois les rayons se divisent jusqu'au bord
	while (e.levels > 1 && (long) e.n << (e.levels - 1) > 1 << 16) e.levels--;	// (pas plus de rayons que de pixels sur le tour)
	int c[3];
	for (int s = 0; s < e.levels; s++)
	{
		int cnt = e.n << s;
		e.color[s] = (uint32_t *) malloc (sizeof (uint32_t) * (size_t) cnt);
		for (int j = 0; j < cnt; j++)
		{
			if (s > 0 && (j & 1) == ((j >> 1) & 1)) { e.color[s][j] = e.color[s - 1][j >> 1]; continue; }	// la moitie qui garde la couleur
			// (trois tirages dans l'ordre : dans les arguments d'une fonction, le C++ ne le garantit pas)
			double hh = r.unit (), ss = sat + (r.unit () - 0.5) * 0.5, vv = light + (r.unit () - 0.5) * 0.55;
			hsv_clamped (hh, ss, vv, c);
			e.color[s][j] = (uint32_t) c[0] << 16 | (uint32_t) c[1] << 8 | (uint32_t) c[2];
		}
	}
	e.ring = (1.45 + r.unit () * 0.1) / e.rings;			// la largeur d'un anneau : le dernier atteint les coins
	e.cx = 0.5 + (r.unit () - 0.5) * 0.1;
	e.cy = 0.5 + (r.unit () - 0.5) * 0.1;
	e.turn = r.unit ();
	// le cadrage, en coordonnees d'image
	e.vx = k.get ("x", 0.5); e.vy = k.get ("y", 0.5); e.zoom = k.get ("zoom", 1);
	e.view = k.view ();
	image_alloc (im, w, h);
	image_view (im, e.vx, e.vy, e.zoom);
	rows_parallel (h, rays_row, &e);
	for (int s = 0; s < e.levels; s++) free (e.color[s]);
}

// une image calculee : sa sorte (IMG_KINDS, inconnue : le mandala), sa graine et ses reglages (0 : la graine choisit
// tout) ; l'image dit d'ou elle vient et quelles valeurs elle a prises
static void image_make (Image &im, const char *kind, int w, int h, uint32_t seed, const ImgSet *set = 0)
{
	if (set && !set->n) set = 0;
	ImgSet used; used.n = 0;
	int k = img_kind_index (kind), np;
	if (k < 0) k = 0;
	const ImgParam *defs = img_params (IMG_KINDS[k], np);
	Knobs kn = { defs, np, set, &used };
	switch (k)
	{
	case 1: escape (im, w, h, seed, false, false, kn); break;
	case 2: escape (im, w, h, seed, true, false, kn); break;
	case 3: escape (im, w, h, seed, false, true, kn); break;
	case 4: lyapunov (im, w, h, seed, kn); break;
	case 5: newton (im, w, h, seed, kn); break;
	case 6: rays (im, w, h, seed, kn); break;
	default:
	{
		double vx = kn.get ("x", 0.5), vy = kn.get ("y", 0.5), zoom = kn.get ("zoom", 1);
		mandala (im, w, h, seed, kn.view (), vx, vy, zoom);
		image_view (im, vx, vy, zoom);
		break;
	}
	}
	image_source_gen (im, IMG_KINDS[k], seed, set);
	im.used = used;
}

#endif
