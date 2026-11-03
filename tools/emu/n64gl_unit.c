/*
 * n64gl_unit -- le combineur et le blender generes par n64gl.c, contre un calcul fait ici sans GLSL, sur llvmpipe
 * (PC) : des reglages du RDP au hasard, un pixel a chaque fois ; puis le filtre a trois points et les replis
 * sur une texture au hasard.
 *     n64gl_unit [essais]
 */
#define main replay_main
#include "n64gl_replay.c"
#undef main
#include <math.h>

#define W 32
#define H 32

static uint32_t rng = 12345;
static uint32_t rnd (void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

typedef struct { float r, g, b, a; } C;

static C rgba_of (uint32_t c)
{
	C x = { (c >> 24) / 255.0f, (c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f };
	return x;
}

static float sat (float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

struct in { C comb, t0, t1, prim, shade, env; float lod; };

static C pick (const struct in *i, int v)
{
	static const C one = { 1, 1, 1, 1 }, zero = { 0, 0, 0, 0 };
	switch (v) {
	case 0: return i->comb;
	case 1: return i->t0;
	case 2: return i->t1;
	case 3: return i->prim;
	case 4: return i->shade;
	case 5: return i->env;
	case 6: return one;
	}
	return zero;
}

static C splat (float a)
{
	C x = { a, a, a, a };
	return x;
}

/* une lettre -> une entree, les quatre tableaux du manuel */
static C letter_a (const struct in *i, int v) { return v < 7 ? pick (i, v) : splat (0); }
static C letter_b (const struct in *i, int v) { return v < 6 ? pick (i, v) : splat (0); }
static C letter_d (const struct in *i, int v) { return v < 7 ? pick (i, v) : splat (0); }
static C letter_c (const struct in *i, int v)
{
	if (v < 6)
		return pick (i, v);
	if (v >= 7 && v <= 12)
		return splat (pick (i, v - 7).a);
	if (v == 14)
		return splat (i->lod);
	return splat (0);
}
static float alpha_c (const struct in *i, int v)
{
	if (v >= 1 && v <= 5)
		return pick (i, v).a;
	if (v == 6)
		return i->lod;
	return 0;
}
static float alpha_x (const struct in *i, int v) { return v < 7 ? pick (i, v).a : 0; }

static C one_cycle (const struct in *i, int a, int b, int c, int d, int aa, int ab, int ac, int ad)
{
	C A = letter_a (i, a), B = letter_b (i, b), K = letter_c (i, c), D = letter_d (i, d), o;
	o.r = sat ((A.r - B.r) * K.r + D.r);
	o.g = sat ((A.g - B.g) * K.g + D.g);
	o.b = sat ((A.b - B.b) * K.b + D.b);
	o.a = sat ((alpha_x (i, aa) - alpha_x (i, ab)) * alpha_c (i, ac) + alpha_x (i, ad));
	return o;
}

static C lerp3 (C x, float a, C y, float b)
{
	C o = { x.r * a + y.r * b, x.g * a + y.g * b, x.b * a + y.b * b, x.a };
	return o;
}

static int near_edge;

/* -> 0 : le pixel est refuse */
static int reference (const struct n64gl_batch *b, const C *t0, const C *t1, C shade, C dst, C *out)
{
	struct in in;
	uint32_t h = b->combH, l = b->combL, om = b->omL;
	int two = (b->omH >> 20 & 3) == 1;
	C c;
	memset (&in, 0, sizeof in);
	in.t0 = *t0;
	in.t1 = *t1;
	in.prim = rgba_of (b->prim);
	in.env = rgba_of (b->env);
	in.shade = shade;
	in.lod = b->primLod / 255.0f;
	c = one_cycle (&in, h >> 20 & 15, l >> 28, h >> 15 & 31, l >> 15 & 7, h >> 12 & 7, l >> 12 & 7, h >> 9 & 7, l >> 9 & 7);
	if (two) {
		C s = in.t0;
		in.comb = c;
		in.t0 = in.t1;			/* le RDP a avance d'une tuile */
		in.t1 = s;
		c = one_cycle (&in, h >> 5 & 15, l >> 24 & 15, h & 31, l >> 6 & 7, l >> 21 & 7, l >> 3 & 7, l >> 18 & 7, l & 7);
	}
	C fog = rgba_of (b->fog), bc = rgba_of (b->blend);
	float th = bc.a < 1 / 255.0f ? 1 / 255.0f : bc.a;
	near_edge = (om & 1) && fabsf (c.a - th) < 1.5f / 255;	/* (le seuil tombe entre deux arrondis) */
	if ((om & 1) && c.a < th)
		return 0;
	int sel = om >> 13 & 1, x = om >> 12 & 1;
	if (sel && x && c.a < 0.5f)
		return 0;
	if (sel && !x)
		c.a = 1;
	C p = c;
	const float aval[4] = { c.a, fog.a, shade.a, 0 };
	if (two) {
		int P = om >> 30 & 3, A = om >> 26 & 3, M = om >> 22 & 3, B = om >> 18 & 3;
		const C cols[4] = { p, p, bc, fog };
		float a = aval[A], bb = B == 0 ? 1 - a : B == 3 ? 0 : 1;
		p = lerp3 (cols[P], a, cols[M], bb);
	}
	int sh = two ? 2 : 0;
	int P = om >> (28 + 2 - sh) & 3, A = om >> (24 + 2 - sh) & 3, M = om >> (20 + 2 - sh) & 3, B = om >> (16 + 2 - sh) & 3;
	C cols[4] = { p, dst, bc, fog };
	float a = aval[A], bb = B == 0 ? 1 - a : B == 3 ? 0 : 1;
	int on = (om >> 14 & 1) || (om >> 10 & 3) == 2;
	C o = on ? lerp3 (cols[P], a, cols[M], bb) : cols[P];
	o.r = sat (o.r);
	o.g = sat (o.g);
	o.b = sat (o.b);
	*out = o;
	return 1;
}

static struct n64gl_vert quad[6];

static void make_quad (float s0, float t0, float s1, float t1, uint32_t rgba)
{
	static const float xy[6][2] = { { -1, 1 }, { -1, -1 }, { 1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } };
	for (int i = 0; i < 6; i++) {
		struct n64gl_vert *v = &quad[i];
		v->x = xy[i][0];
		v->y = xy[i][1];
		v->z = 0;
		v->w = 1;
		v->s = xy[i][0] < 0 ? s0 : s1;
		v->t = xy[i][1] > 0 ? t0 : t1;
		v->r = rgba >> 24;
		v->g = rgba >> 16;
		v->b = rgba >> 8;
		v->a = rgba;
	}
}

/* un reglage au hasard, sans bruit ni lod par pixel, que le calcul ne peut pas predire */
static void random_batch (struct n64gl_batch *b)
{
	memset (b, 0, sizeof *b);
	for (;;) {
		uint32_t h = rnd () & 0xFFFFFF, l = rnd ();
		if ((h >> 20 & 15) == 7 || (h >> 5 & 15) == 7 || (h >> 15 & 31) == 13 || (h & 31) == 13)
			continue;
		if ((h >> 9 & 7) == 0 || (l >> 18 & 7) == 0)
			continue;
		b->combH = h;
		b->combL = l;
		break;
	}
	b->omH = (rnd () & 1) << 20;
	b->omL = (rnd () & 0xFFFF0000) | (rnd () & 1) | (rnd () & 1) << 12 | (rnd () & 1) << 13 | (rnd () & 1) << 14 | (rnd () & 3) << 10;
	if ((b->omL >> 10 & 3) == 3)
		b->omL &= ~0xC00;
	b->prim = rnd ();
	b->env = rnd ();
	b->fog = rnd ();
	b->blend = rnd ();
	b->primLod = rnd () & 255;
	b->count = 6;
	b->tex0 = 0;
	b->tex1 = 1;
	b->tile0 = b->tile1 = 0;
	b->sc0 = 0;
	b->sc1 = 320 | 240u << 16;
	b->flags = 7 | 8;
}

static int tests, fails;

static void compare (const char *what, const unsigned char *p, C want, const struct n64gl_batch *b)
{
	float e = fabsf (p[0] / 255.0f - want.r);
	float g = fabsf (p[1] / 255.0f - want.g), d = fabsf (p[2] / 255.0f - want.b);
	if (g > e) e = g;
	if (d > e) e = d;
	tests++;
	if (e > 3.5f / 255) {
		if (fails++ < 12)
			printf ("  %s : %d %d %d, attendu %.0f %.0f %.0f ; comb %06X %08X om %08X %08X\n", what, p[0], p[1], p[2],
				want.r * 255, want.g * 255, want.b * 255, b->combH, b->combL, b->omH, b->omL);
	}
}

static uint32_t px0[16], px1[16];
static struct n64gl_tex texs[N64GL_MAX_TEX];

static void set_tex (uint32_t *px, struct n64gl_tex *t, int w, int h)
{
	t->px = px;
	t->w = w;
	t->h = h;
	t->dirty = 1;
}

static void draw (struct n64gl_batch *b, uint32_t clear)
{
	struct n64gl_frame f;
	memset (&f, 0, sizeof f);
	f.w = W;
	f.h = H;
	f.clear = clear;
	f.gw = 320;
	f.gh = 240;
	f.v = quad;
	f.nv = 6;
	f.nb = 1;
	f.b = b;
	f.tex = texs;
	n64gl_render (&f);
	plat_finish ();
}

static void combiner_tests (int n)
{
	static unsigned char rgba[W * H * 4];
	rng = 12345;
	for (int i = 0; i < n; i++) {
		struct n64gl_batch b;
		random_batch (&b);
		uint32_t c0 = rnd (), c1 = rnd (), sh = rnd (), cl = rnd () & 0xFFFFFF;
		for (int k = 0; k < 16; k++) {
			px0[k] = (c0 << 24 | c0 >> 8);		/* R G B A -> 0xAARRGGBB */
			px1[k] = (c1 << 24 | c1 >> 8);
		}
		set_tex (px0, &texs[0], 4, 4);
		set_tex (px1, &texs[1], 4, 4);
		make_quad (0, 0, 4, 4, sh);
		draw (&b, cl);
		plat_read (rgba, W, H);
		C t0 = rgba_of (c0), t1 = rgba_of (c1), o, dst = rgba_of (cl << 8 | 255);
		if (!reference (&b, &t0, &t1, rgba_of (sh), dst, &o))
			o = dst;
		if (!near_edge)
			compare ("combineur", rgba + (H / 2 * W + W / 2) * 4, o, &b);
	}
}

/* le filtre : texture 4 x 4 au hasard, trois points contre le calcul, chacun des trois replis */
static uint32_t wrap_px (const uint32_t *px, int x, int y, int wx, int wy)
{
	if (wx == 1)
		x = x < 0 ? 0 : x > 3 ? 3 : x;
	else if (wx == 2) {
		x &= 7;
		if (x > 3)
			x = 7 - x;
	} else
		x &= 3;
	if (wy == 1)
		y = y < 0 ? 0 : y > 3 ? 3 : y;
	else if (wy == 2) {
		y &= 7;
		if (y > 3)
			y = 7 - y;
	} else
		y &= 3;
	return px[y * 4 + x];
}

static float ch (uint32_t c, int k)
{
	return (c >> (k * 8) & 255) / 255.0f;
}

static void filter_tests (void)
{
	static unsigned char rgba[W * H * 4];
	for (int wx = 0; wx < 3; wx++)
		for (int wy = 0; wy < 3; wy++) {
			struct n64gl_batch b;
			memset (&b, 0, sizeof b);
			b.combH = 1 << 20 | 31 << 15 | 7 << 12 | 7 << 9;	/* (TEXEL0 - 0) * 0 + ... : d = TEXEL0 */
			b.combL = 8u << 28 | 1 << 15 | 7 << 12 | 1 << 9;
			b.omL = 0;
			b.count = 6;
			b.tex0 = 0;
			b.tex1 = -1;
			b.sc1 = 320 | 240u << 16;
			b.flags = 7 | 8 | 1 << 12;
			b.wrap0 = wx << 13 | wy << 15;
			for (int k = 0; k < 16; k++)
				px0[k] = rnd ();
			set_tex (px0, &texs[0], 4, 4);
			make_quad (-3, -3, 9, 9, 0xFFFFFFFF);
			draw (&b, 0);
			plat_read (rgba, W, H);
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++) {
					float s = -3 + 12 * (x + 0.5f) / W, t = -3 + 12 * (y + 0.5f) / H;
					int ix = floorf (s), iy = floorf (t);
					float fx = s - ix, fy = t - iy;
					int up = fx + fy >= 1;
					uint32_t c0 = wrap_px (px0, ix + up, iy + up, wx, wy);
					uint32_t cx = wrap_px (px0, up ? ix : ix + 1, up ? iy + 1 : iy, wx, wy);
					uint32_t cy = wrap_px (px0, up ? ix + 1 : ix, up ? iy : iy + 1, wx, wy);
					float gx = up ? 1 - fx : fx, gy = up ? 1 - fy : fy;
					C o;
					o.r = ch (c0, 2) + gx * (ch (cx, 2) - ch (c0, 2)) + gy * (ch (cy, 2) - ch (c0, 2));
					o.g = ch (c0, 1) + gx * (ch (cx, 1) - ch (c0, 1)) + gy * (ch (cy, 1) - ch (c0, 1));
					o.b = ch (c0, 0) + gx * (ch (cx, 0) - ch (c0, 0)) + gy * (ch (cy, 0) - ch (c0, 0));
					o.a = 1;
					compare ("filtre", rgba + (y * W + x) * 4, o, &b);
				}
		}
}

int main (int argc, char **argv)
{
	void *proc = NULL;
	int n = argc > 1 ? atoi (argv[1]) : 2000;
	if (!plat_init (W, H, &proc) || !n64gl_start ((void *(*) (const char *)) proc, W, H)) {
		printf ("n64gl_unit : FAIL au depart\n");
		return 1;
	}
	filter_tests ();
	printf ("filtre : %d pixels, %d ecarts\n", tests, fails);
	combiner_tests (n);
	printf ("n64gl_unit : %d pixels compares, %d ecarts : %s\n", tests, fails, fails ? "FAIL" : "PASS");
	return fails != 0;
}
