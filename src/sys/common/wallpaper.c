/* wallpaper.c : le fond d'écran du bureau (wallpaper.h) : le réglage et le peintre, communs à
 * /sys/voronoy et au volet Thème de Réglages. Les fonds d'Onyx (cellules, dégradé, bulles, uni, motif,
 * image et sa teinte), au style Givre calme : la palette du thème, plus clair en haut. */
#include "wallpaper.h"

#define WP_NMAX 64	/* cellules */

const char *const wp_mode_key[WP_NMODES] = { "voronoi", "gradient", "bubbles", "solid", "pattern", "image" };

/* Givre, Onyx Noir : les cinq couleurs calmes du fond (celles de voronoy.asm, docs/DESIGN-ELEGANT.md 5.1) */
static const uint32_t wp_pal_givre[5] = { 0x00D6DEE9, 0x00DCE3ED, 0x00D1DAE6, 0x00E1E7EF, 0x00CDD7E4 };
static const uint32_t wp_pal_noir[5] = { 0x001A1E26, 0x001D222B, 0x00181C23, 0x0020252F, 0x001B2029 };

static void wp_copy(char *d, const char *s, long size)
{
	long i = 0;
	for (; s[i] && i < size - 1; i++)
		d[i] = s[i];
	d[i] = 0;
}

static int wp_eq(const char *a, const char *b)
{
	for (;; a++, b++) {
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

void wp_defaults(struct wp_conf *c)
{
	c->mode = WP_VORONOI;
	c->tile = 0;
	c->tint[0] = 0;
	wp_copy(c->pattern, WP_PATTERN_DEF, sizeof c->pattern);
	c->image[0] = 0;
}

void wp_key(struct wp_conf *c, const char *k, const char *v)
{
	if (wp_eq(k, "mode")) {
		for (int i = 0; i < WP_NMODES; i++)
			if (wp_eq(v, wp_mode_key[i]))
				c->mode = i;
	} else if (wp_eq(k, "tint")) {
		wp_copy(c->tint, wp_eq(v, "none") || wp_eq(v, "no") ? "" : v, sizeof c->tint);
	} else if (wp_eq(k, "pattern")) {
		if (v[0])
			wp_copy(c->pattern, v, sizeof c->pattern);
	} else if (wp_eq(k, "image")) {
		wp_copy(c->image, v, sizeof c->image);
	} else if (wp_eq(k, "style")) {
		c->tile = wp_eq(v, "tile");
	}
}

/* une ligne : blancs autour de la clé et de la valeur retirés, « # » ou « ; » en tête : commentaire */
void wp_parse(struct wp_conf *c, char *p)
{
	while (*p) {
		char *l = p;
		while (*p && *p != '\n')
			p++;
		if (*p)
			*p++ = 0;
		while (*l == ' ' || *l == '\t')
			l++;
		if (*l == '#' || *l == ';')
			continue;
		char *eq = l;
		while (*eq && *eq != '=')
			eq++;
		if (*eq != '=')
			continue;
		char *ke = eq;
		while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t'))
			ke--;
		*ke = 0;
		char *v = eq + 1;
		while (*v == ' ' || *v == '\t')
			v++;
		char *ve = v;
		while (*ve)
			ve++;
		while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r'))
			*--ve = 0;
		wp_key(c, l, v);
	}
}

static long wp_put(char *o, long p, long cap, const char *s)
{
	while (s && *s && p < cap - 1)
		o[p++] = *s++;
	o[p] = 0;
	return p;
}

long wp_format(const struct wp_conf *c, char *o, long cap)
{
	long p = 0;
	p = wp_put(o, p, cap, "# Le fond d'ecran du bureau (src/sys/common/wallpaper.h), ecrit par Reglages (volet Theme).\r\n"
			      "# mode : voronoi, gradient, bubbles, solid, pattern ou image ; tint : vide, accent ou un accent.\r\n"
			      "mode    = ");
	p = wp_put(o, p, cap, wp_mode_key[c->mode >= 0 && c->mode < WP_NMODES ? c->mode : 0]);
	p = wp_put(o, p, cap, "\r\ntint    = ");
	p = wp_put(o, p, cap, c->tint);
	p = wp_put(o, p, cap, "\r\npattern = ");
	p = wp_put(o, p, cap, c->pattern);
	p = wp_put(o, p, cap, "\r\nimage   = ");
	p = wp_put(o, p, cap, c->image);
	p = wp_put(o, p, cap, "\r\nstyle   = ");
	p = wp_put(o, p, cap, c->tile ? "tile" : "cover");
	p = wp_put(o, p, cap, "\r\n");
	return p;
}

void wp_pattern_path(const struct wp_conf *c, char *out, long size)
{
	int path = 0;
	for (const char *s = c->pattern; *s; s++)
		if (*s == '/' || *s == ':')
			path = 1;
	long p = 0;
	out[0] = 0;
	if (!path) {
		p = wp_put(out, p, size, WP_DIR);
		p = wp_put(out, p, size, "/");
	}
	wp_put(out, p, size, c->pattern);
}

/* ---- couleurs ---- */

uint32_t wp_tone(uint32_t col, long n)
{
	uint32_t res = 0;
	if (n < 0)
		n = 0;
	if (n > 255)
		n = 255;
	for (uint32_t sh = 0; sh < 24; sh += 8) {
		uint32_t v = (col >> sh) & 0xFF;
		if (n < 128)
			v = (v * (uint32_t)n) >> 7;
		else
			v = v + (255 - v) * ((uint32_t)n - 128) / 127;
		res |= v << sh;
	}
	return res;
}

uint32_t wp_mix(uint32_t a, uint32_t b, long t)
{
	if (t <= 0)
		return a & 0xFFFFFF;
	if (t >= 256)
		return b & 0xFFFFFF;
	uint32_t res = 0;
	for (uint32_t sh = 0; sh < 24; sh += 8) {
		uint32_t x = (a >> sh) & 0xFF, y = (b >> sh) & 0xFF;
		res |= ((x * (256 - (uint32_t)t) + y * (uint32_t)t) >> 8) << sh;
	}
	return res;
}

static uint32_t wp_lum(uint32_t c)
{
	return (((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8;
}

/* la part de la teinte dans la palette calme : assez pour la voir, pas assez pour crier */
static long wp_tint_part(int dark)
{
	return dark ? 36 : 52;
}

static uint32_t wp_pal(int dark, int tinted, uint32_t tint, int i)
{
	uint32_t c = (dark ? wp_pal_noir : wp_pal_givre)[i];
	return tinted ? wp_mix(c, tint, wp_tint_part(dark)) : c;
}

uint32_t wp_base(int dark, int tinted, uint32_t tint)
{
	return wp_pal(dark, tinted, tint, 0);
}

/* le ton d'une ligne y (0..h) : plus clair en haut (comme les cellules de voronoy) */
static long wp_level(const struct wp_target *t, long y, long k)
{
	long base = t->dark ? 128 : 136;
	if (t->dark)
		k = k * 4 / 5;
	return base + k / 3 - k * y / (t->h > 1 ? t->h : 1);
}

/* ---- le hasard (xorshift64, comme voronoy) ---- */
static uint64_t wp_rng;

static uint64_t wp_rnd_n(uint64_t n)
{
	uint64_t x = wp_rng;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	wp_rng = x;
	return (x >> 1) % (n ? n : 1);
}

/* ---- cellules : Voronoï « facettes douces » (voronoy.asm) ---- */
static void wp_voronoi(const struct wp_target *t)
{
	static int64_t sx[WP_NMAX], sy[WP_NMAX];
	static uint32_t sc[WP_NMAX], se[WP_NMAX], rc[WP_NMAX];
	uint64_t w = (uint64_t)t->w, h = (uint64_t)t->h;
	/* une cellule pour 28 000 pixels de l'écran (l'aperçu : autant de cellules que l'écran) */
	uint64_t sw = (uint64_t)(t->scale_w > t->w ? t->scale_w : t->w), shh = h * sw / w;
	uint64_t n = (sw * shh + 14000) / 28000;
	if (n < 8)
		n = 8;
	if (n > WP_NMAX)
		n = WP_NMAX;
	uint64_t dmin = w * h * 3844 / 10000 / n; /* distance minimale² = 0,62² x l x h / N */
	int64_t off = (int64_t)(20 * w / sw);	  /* germes un peu hors de l'écran */
	for (uint64_t k = 0; k < n; k++) {
		long tries = 200;
		int64_t x, y;
	again:
		x = (int64_t)wp_rnd_n(w + 2 * (uint64_t)off) - off;
		y = (int64_t)wp_rnd_n(h + 2 * (uint64_t)off) - off;
		if (--tries) {
			for (uint64_t j = 0; j < k; j++) {
				uint64_t dx = (uint64_t)(x - sx[j]), dy = (uint64_t)(y - sy[j]);
				if (dx * dx + dy * dy < dmin)
					goto again;
			}
		}
		sx[k] = x;
		sy[k] = y;
		sc[k] = wp_pal(t->dark, t->tinted, t->tint, (int)wp_rnd_n(5));
		se[k] = wp_tone(sc[k], 134); /* liseré : à peine plus clair */
	}
	for (uint64_t y = 0; y < h; y++) {
		long lv = wp_level(t, (long)y, 30) - 10 + (t->dark ? 2 : 0);
		for (uint64_t i = 0; i < n; i++)
			rc[i] = wp_tone(sc[i], lv);
		uint32_t *px = t->px + (long)y * t->stride;
		for (uint64_t x = 0; x < w; x++) {
			uint64_t d1 = (uint64_t)-1, d2 = (uint64_t)-1, k1 = 0, k2 = 0;
			for (uint64_t i = 0; i < n; i++) {
				uint64_t dx = x - (uint64_t)sx[i], dy = y - (uint64_t)sy[i];
				uint64_t d = dx * dx + dy * dy;
				if (d < d1) {
					d2 = d1;
					k2 = k1;
					d1 = d;
					k1 = i;
				} else if (d < d2) {
					d2 = d;
					k2 = i;
				}
			}
			uint64_t dd = d2 - d1;
			uint64_t gx = (uint64_t)sx[k1] - (uint64_t)sx[k2], gy = (uint64_t)sy[k1] - (uint64_t)sy[k2];
			uint64_t g = (gx * gx + gy * gy) << 2;
			px[x] = dd * dd < g ? se[k1] : rc[k1];
		}
	}
}

/* ---- dégradé : la couleur calme, plus claire en haut ---- */
static void wp_gradient(const struct wp_target *t, uint32_t base)
{
	for (long y = 0; y < t->h; y++) {
		uint32_t c = wp_tone(base, wp_level(t, y, 42));
		uint32_t *d = t->px + y * t->stride;
		for (long x = 0; x < t->w; x++)
			d[x] = c;
	}
}

static unsigned wp_isqrt(unsigned n)
{
	if (n == 0)
		return 0;
	unsigned x = n, y = (x + 1) / 2;
	while (y < x) {
		x = y;
		y = (x + n / x) / 2;
	}
	return x;
}

/* ---- bulles (Onyx) : des disques doux, plus ou moins transparents, de toutes les tailles ---- */
static void wp_bubbles(const struct wp_target *t, uint32_t base)
{
	long w = t->w, h = t->h;
	uint32_t ink = t->dark ? wp_tone(base, 168) : wp_mix(base, 0x00FFFFFF, 170);
	for (int b = 0; b < 26; b++) {
		long cx = (long)wp_rnd_n((uint64_t)w), cy = (long)wp_rnd_n((uint64_t)h);
		long r = h / 40 + (long)wp_rnd_n((uint64_t)(h / 7 + 1));
		long op = (t->dark ? 18 : 26) + (long)wp_rnd_n(t->dark ? 40 : 60);
		if (r < 2)
			r = 2;
		for (long y = cy - r; y <= cy + r; y++) {
			if (y < 0 || y >= h)
				continue;
			for (long x = cx - r; x <= cx + r; x++) {
				if (x < 0 || x >= w)
					continue;
				long dx = x - cx, dy = y - cy;
				unsigned dd = (unsigned)(dx * dx + dy * dy);
				if (dd > (unsigned)(r * r))
					continue;
				long k = (long)wp_isqrt((unsigned)((uint64_t)dd * 65536u / (uint64_t)(r * r))); /* 0..256 */
				long a = op; /* le corps, puis un bord plus clair qui s'efface */
				if (k >= 236)
					a = 2 * op * (256 - k) / 20;
				else if (k >= 200)
					a = op + (k - 200) * op / 36;
				if (a < 0)
					a = 0;
				uint32_t *p = t->px + y * t->stride + x;
				*p = wp_mix(*p, ink, a);
			}
		}
	}
}

/* ---- une image posée sur t (cover : remplit, centrée, le reste coupé ; tile : en mosaïque, à
 * l'échelle de l'écran) : la moyenne des pixels sous chacun quand elle rétrécit, bilinéaire quand elle
 * grandit (wp_grey_cover d'Onyx, en couleur) ---- */
static void wp_cover(const struct wp_target *t)
{
	const uint32_t *img = t->img;
	long iw = t->iw, ih = t->ih, w = t->w, h = t->h;
	int64_t kx = (int64_t)iw * 65536 / w, ky = (int64_t)ih * 65536 / h, k = kx < ky ? kx : ky; /* 16.16 */
	int64_t ox = ((int64_t)iw * 65536 - k * w) / 2, oy = ((int64_t)ih * 65536 - k * h) / 2;
	for (long y = 0; y < h; y++) {
		uint32_t *o = t->px + y * t->stride;
		if (k >= 65536) {
			long sy0 = (long)((oy + k * y) >> 16), sy1 = (long)((oy + k * (y + 1)) >> 16);
			if (sy1 <= sy0)
				sy1 = sy0 + 1;
			if (sy1 > ih)
				sy1 = ih;
			for (long x = 0; x < w; x++) {
				long sx0 = (long)((ox + k * x) >> 16), sx1 = (long)((ox + k * (x + 1)) >> 16);
				if (sx1 <= sx0)
					sx1 = sx0 + 1;
				if (sx1 > iw)
					sx1 = iw;
				uint32_t r = 0, g = 0, b = 0, n = 0;
				long step = (sx1 - sx0) * (sy1 - sy0) > 64 ? 2 : 1; /* (une grande réduction : un sur deux) */
				for (long sy = sy0; sy < sy1; sy += step)
					for (long sx = sx0; sx < sx1; sx += step) {
						uint32_t c = img[sy * iw + sx];
						r += (c >> 16) & 255;
						g += (c >> 8) & 255;
						b += c & 255;
						n++;
					}
				o[x] = n ? (r / n) << 16 | (g / n) << 8 | b / n : 0;
			}
		} else {
			int64_t fy = oy + k * y + k / 2 - 32768;
			if (fy < 0)
				fy = 0;
			long sy = (long)(fy >> 16), ty = (long)((fy >> 8) & 255);
			if (sy >= ih)
				sy = ih - 1;
			long sy2 = sy + 1 < ih ? sy + 1 : ih - 1;
			for (long x = 0; x < w; x++) {
				int64_t fx = ox + k * x + k / 2 - 32768;
				if (fx < 0)
					fx = 0;
				long sx = (long)(fx >> 16), tx = (long)((fx >> 8) & 255);
				if (sx >= iw)
					sx = iw - 1;
				long sx2 = sx + 1 < iw ? sx + 1 : iw - 1;
				uint32_t top = wp_mix(img[sy * iw + sx], img[sy * iw + sx2], tx);
				uint32_t bot = wp_mix(img[sy2 * iw + sx], img[sy2 * iw + sx2], tx);
				o[x] = wp_mix(top, bot, ty);
			}
		}
	}
}

static void wp_tile(const struct wp_target *t)
{
	long sw = t->scale_w > t->w ? t->scale_w : t->w;
	for (long y = 0; y < t->h; y++) {
		long sy = (y * sw / t->w) % t->ih;
		uint32_t *o = t->px + y * t->stride;
		for (long x = 0; x < t->w; x++)
			o[x] = t->img[sy * t->iw + (x * sw / t->w) % t->iw] & 0xFFFFFF;
	}
}

/* ---- un motif : le gris de l'image colore le dégradé (clair : ses gris l'assombrissent ; sombre :
 * ils l'éclairent, des traits de lumière sur le fond) ---- */
static void wp_pattern(const struct wp_target *t, uint32_t base)
{
	wp_cover(t);
	for (long y = 0; y < t->h; y++) {
		uint32_t c = wp_tone(base, wp_level(t, y, 36));
		uint32_t lo = t->dark ? c : wp_tone(c, 78), hi = t->dark ? wp_tone(c, 164) : c;
		uint32_t *d = t->px + y * t->stride;
		for (long x = 0; x < t->w; x++) {
			long g = (long)wp_lum(d[x]);
			d[x] = t->dark ? wp_mix(lo, hi, (255 - g) * 256 / 255) : wp_mix(lo, hi, g * 256 / 255);
		}
	}
}

/* une image teintée : un duotone de la teinte, de son ombre à sa lumière */
static void wp_duotone(const struct wp_target *t)
{
	uint32_t lo = wp_tone(t->tint, t->dark ? 40 : 56), hi = wp_mix(t->tint, 0x00FFFFFF, t->dark ? 120 : 170);
	for (long y = 0; y < t->h; y++) {
		uint32_t *d = t->px + y * t->stride;
		for (long x = 0; x < t->w; x++)
			d[x] = wp_mix(lo, hi, (long)wp_lum(d[x]) * 256 / 255);
	}
}

void wp_paint(const struct wp_conf *c, const struct wp_target *t)
{
	if (t->w <= 0 || t->h <= 0)
		return;
	wp_rng = (t->seed ^ 0x9E3779B97F4A7C15ull) | 1;
	uint32_t base = wp_base(t->dark, t->tinted, t->tint);
	int pic = t->img && t->iw > 0 && t->ih > 0;
	switch (c->mode) {
	case WP_GRADIENT:
		wp_gradient(t, base);
		break;
	case WP_BUBBLES:
		wp_gradient(t, base);
		wp_bubbles(t, base);
		break;
	case WP_SOLID:
		for (long y = 0; y < t->h; y++)
			for (long x = 0; x < t->w; x++)
				t->px[y * t->stride + x] = base;
		break;
	case WP_PATTERN:
		if (pic)
			wp_pattern(t, base);
		else
			wp_gradient(t, base);
		break;
	case WP_IMAGE:
		if (!pic) {
			wp_gradient(t, base);
			break;
		}
		if (c->tile)
			wp_tile(t);
		else
			wp_cover(t);
		if (t->tinted)
			wp_duotone(t);
		break;
	default:
		wp_voronoi(t);
		break;
	}
}
