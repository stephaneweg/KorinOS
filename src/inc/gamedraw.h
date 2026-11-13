/*
 * gamedraw.h -- dessin soigne des jeux en C, au-dessus de gamefb.h : la traduction de src/inc/gameart.inc (memes
 * noms ga_*, memes calculs entiers, donc les memes pixels) : sprites a couche alpha (premultipliee, octet haut),
 * blocs et spheres generes, panneaux de verre, texte pre-rendu (dessine sans UIKit a chaque image), chiffres,
 * particules, bruit, ciel.
 *
 * Apres gamefb.h ; ga_init (octets pour les sprites) apres gf_open. Les sprites de gamefb (gf_sprite, gf_glow)
 * lisent le meme format : un point nul est transparent. ga_ablit melange, ga_shadow assombrit selon l'alpha.
 * (src/inc/gameart.h est autre chose : l'atelier en C++ de pinball, critters, cosmozoom.)
 */
#ifndef KORIN_GAMEDRAW_H
#define KORIN_GAMEDRAW_H

#include <stdlib.h>
#include "gamefb.h"

#define GA_NP		320			/* particules */

struct ga_part_t
{
	int32_t x, y;				/* 16.16 */
	int32_t vx, vy;
	int32_t life;
	uint32_t col;
	int32_t size, grav;
};

static char *ga_bp;				/* pile des sprites */
static unsigned long ga_pnext;
static unsigned char ga_nz[4096];
static struct ga_part_t ga_parts[GA_NP];

static inline void ga_gen_noise (void)
{
	for (int i = 0; i < 4096; i++)
		ga_nz[i] = (unsigned char) (gf_rand () >> 24);
}

/* octets pour les sprites -> 0 si plus de memoire */
static inline long ga_init (unsigned long bytes)
{
	ga_bp = (char *) malloc (bytes);
	if (!ga_bp)
		return 0;
	ga_gen_noise ();
	return 1;
}

/* largeur, hauteur -> un sprite vide (a 0) */
static inline gf_sprite_t *ga_new (unsigned long w, unsigned long h)
{
	gf_sprite_t *s = (gf_sprite_t *) ga_bp;
	s[0] = (uint32_t) w;
	s[1] = (uint32_t) h;
	ga_bp += 8 + w * h * 4;
	memset (s + 2, 0, w * h * 4);
	return s;
}

/* deux couleurs, 0..256 vers la seconde */
static inline uint32_t ga_mix (uint32_t a, uint32_t b, uint32_t k)
{
	uint32_t ik = 256 - k;
	uint32_t rb = (((a & 0xFF00FF) * ik + (b & 0xFF00FF) * k) >> 8) & 0xFF00FF;
	uint32_t g = (((a & 0xFF00) * ik + (b & 0xFF00) * k) >> 8) & 0xFF00;
	return g | rb;
}

/* couleur, facteur (256 : telle quelle, plus : plus claire) */
static inline uint32_t ga_shade (uint32_t c, uint32_t f)
{
	uint32_t r = ((c >> 16) & 0xFF) * f >> 8, g = ((c >> 8) & 0xFF) * f >> 8, b = (c & 0xFF) * f >> 8;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return b | g << 8 | r << 16;
}

/* phase -> 0..255, une onde en paraboles */
static inline unsigned long ga_sin8 (unsigned long p)
{
	p &= 255;
	if (p < 128)
		return (p * (128 - p) >> 5) + 127;
	p -= 128;
	return 128 - (p * (128 - p) >> 5);
}

/* point (px, py) dans un rectangle w x h arrondi de rayon r -> couverture 0..256 */
static inline unsigned long ga_cover (unsigned long px, unsigned long py, unsigned long w, unsigned long h, unsigned long r)
{
	if (!r)
		return 256;
	unsigned long a = w - 1 - px, b = h - 1 - py;
	if (px < a) a = px;
	if (py < b) b = py;
	if (a >= r || b >= r)
		return 256;
	unsigned long t = r << 1;
	a = t - (a << 1) - 1;
	b = t - (b << 1) - 1;
	unsigned long d = a * a + b * b, e = (t + 1) * (t + 1);
	if (d >= e)
		return 0;
	unsigned long v = ((e - d) << 8) / (t << 2);
	return v > 256 ? 256 : v;
}

/* dans gf_dst : rectangle, rayon, degrade c1 -> c2, opacite (0..256), couleur du filet (0 : aucun). Decoupe a
 * l'image. */
static inline void ga_panel (long x, long y, unsigned long w, unsigned long h, unsigned long r, uint32_t c1, uint32_t c2,
		      unsigned long op, uint32_t line)
{
	for (unsigned long yy = 0; yy < h; yy++)
	{
		unsigned long ry = (unsigned long) y + yy;
		if (ry >= GF_H)
			continue;			/* (hors de l'image, negatif compris) */
		uint32_t *row = gf_dst + ry * GF_W;
		uint32_t lc = ga_mix (c1, c2, (uint32_t) ((yy << 8) / h));
		for (unsigned long xx = 0; xx < w; xx++)
		{
			unsigned long cx = (unsigned long) x + xx;
			if (cx >= GF_W)
				continue;
			unsigned long cov = ga_cover (xx, yy, w, h, r);
			if (!cov)
				continue;
			uint32_t v = ga_mix (row[cx], lc, (uint32_t) (cov * op >> 8));
			if (line)
			{				/* le filet : couverture moins celle de dedans */
				unsigned long in = 0;
				if (xx != 0 && yy != 0 && xx < w - 1 && yy < h - 1)
					in = ga_cover (xx - 1, yy - 1, w - 2, h - 2, r ? r - 1 : 0);
				if (cov > in)
					v = ga_mix (v, line, (uint32_t) (cov - in));
			}
			row[cx] = v;
		}
	}
}

/* rectangle : octet haut 0xFF dans gf_dst (premier plan : gf_bglow s'arrete) */
static inline void ga_mark (long x, long y, long w, long h)
{
	uint32_t *d = gf_dst + y * GF_W + x;
	do
	{
		long i = 0;
		do
			d[i] |= 0xFF000000;
		while (++i < w);
		d += GF_W;
	}
	while (--h);
}

/* sprite a alpha premultiplie : shadow 0 le pose (ga_ablit), 1 son ombre (ga_shadow : trois quarts de l'alpha) */
static inline void ga_blit_m (long x, long y, const gf_sprite_t *s, int shadow)
{
	long w = s[0], h = s[1];
	const uint32_t *px = s + 2;
	long c0 = x < 0 ? -x : 0, c1 = w;
	if (c1 > GF_W - x) c1 = GF_W - x;
	long r0 = y < 0 ? -y : 0, r1 = h;
	if (r1 > GF_H - y) r1 = GF_H - y;
	if (r0 >= r1 || c0 >= c1)
		return;
	uint32_t *d = gf_dst + (y + r0) * GF_W + x;
	px += r0 * w;
	for (long r = r0; r < r1; r++, px += w, d += GF_W)
		for (long c = c0; c < c1; c++)
		{
			uint32_t v = px[c], a = v >> 24;
			if (!a)
				continue;
			if (shadow)
			{
				a = (a >> 1) + (a >> 2);
				v = 0;
			}
			else if (a == 255)
			{
				d[c] = v;
				continue;
			}
			uint32_t o = d[c], ia = 256 - a;
			uint32_t m = ((((o & 0xFF00FF) * ia) >> 8) & 0xFF00FF) | ((((o & 0xFF00) * ia) >> 8) & 0xFF00);
			d[c] = (v & 0xFFFFFF) + m;
		}
}
static inline void ga_ablit (long x, long y, const gf_sprite_t *s) { ga_blit_m (x, y, s, 0); }
static inline void ga_shadow (long x, long y, const gf_sprite_t *s) { ga_blit_m (x, y, s, 1); }

/* x du centre, y, sprite : centre en largeur */
static inline void ga_ablit_c (long cx, long y, const gf_sprite_t *s) { ga_ablit (cx - (long) (s[0] >> 1), y, s); }

/* sprite, facteur 0..256 : tout le sprite s'estompe */
static inline void ga_fade (gf_sprite_t *s, uint32_t f)
{
	unsigned long n = (unsigned long) s[0] * s[1];
	uint32_t *p = s + 2;
	do
	{
		uint32_t v = *p;
		uint32_t a = (v >> 24) * f >> 8, r = ((v >> 16) & 0xFF) * f >> 8, g = ((v >> 8) & 0xFF) * f >> 8,
			 b = (v & 0xFF) * f >> 8;
		*p++ = b | g << 8 | r << 16 | a << 24;
	}
	while (--n);
}

/* un point premultiplie : adresse, couleur, alpha 0..256 */
static inline void ga_put (uint32_t *p, uint32_t c, unsigned long a)
{
	if (a >= 256)
	{
		*p = c | 0xFF000000;
		return;
	}
	uint32_t r = ((c >> 16) & 0xFF) * (uint32_t) a >> 8, g = ((c >> 8) & 0xFF) * (uint32_t) a >> 8,
		 b = (c & 0xFF) * (uint32_t) a >> 8;
	*p = b | g << 8 | r << 16 | (uint32_t) a << 24;
}

/* largeur, hauteur, rayon, couleur -> un bloc arrondi, bombe, reflet en haut, biseau clair en haut et a
 * gauche, sombre en bas */
static inline gf_sprite_t *ga_block (unsigned long w, unsigned long h, unsigned long r, uint32_t c)
{
	gf_sprite_t *s = ga_new (w, h);
	uint32_t *p = s + 2;
	for (unsigned long y = 0; y < h; y++)
		for (unsigned long x = 0; x < w; x++, p++)
		{
			unsigned long cov = ga_cover (x, y, w, h, r);
			if (!cov)
				continue;
			unsigned long f = 296 - y * 120 / h;		/* degrade */
			if ((y << 1) < h && x >= 2 && x < w - 3)	/* reflet : le tiers haut, sans les bords */
				f += ((h >> 1) - y) << 2;
			if (y <= 1) f += 120;				/* biseau */
			if (x <= 1) f += 70;
			if (y >= h - 2) f -= 90;
			if (x >= w - 2) f -= 60;
			ga_put (p, ga_shade (c, (uint32_t) f), cov);
		}
	return s;
}

/* diametre, couleur -> une sphere eclairee d'en haut a gauche */
static inline gf_sprite_t *ga_ball (unsigned long d, uint32_t c)
{
	gf_sprite_t *s = ga_new (d, d);
	uint32_t *p = s + 2;
	for (unsigned long y = 0; y < d; y++)
		for (unsigned long x = 0; x < d; x++, p++)
		{
			unsigned long cov = ga_cover (x, y, d, d, d >> 1);
			if (!cov)
				continue;
			long dx = (long) (x << 1) + 1 - (long) d + (long) (d >> 1);	/* la lumiere decalee */
			long dy = (long) (y << 1) + 1 - (long) d + (long) (d >> 1);
			unsigned long q = (unsigned long) (dx * dx + dy * dy) * 270 / (d * d);	/* 0 au reflet */
			unsigned long f = q > 420 ? 70 : 420 - q;
			if (f < 70)
				f = 70;
			uint32_t v = ga_shade (c, (uint32_t) f);
			if (q < 12)				/* le point blanc */
				v = ga_mix (v, 0xFFFFFF, (uint32_t) (((12 - q) << 7) / 12));
			ga_put (p, v, cov);
		}
	return s;
}

/* diametre, couleur -> une lumiere ronde (pour gf_glow) */
static inline gf_sprite_t *ga_disc (unsigned long d, uint32_t c)
{
	gf_sprite_t *s = ga_new (d, d);
	uint32_t *p = s + 2;
	unsigned long dd = d * d;
	for (unsigned long y = 0; y < d; y++)
		for (unsigned long x = 0; x < d; x++, p++)
		{
			long dx = (long) (x << 1) + 1 - (long) d, dy = (long) (y << 1) + 1 - (long) d;
			unsigned long q = (unsigned long) (dx * dx + dy * dy);
			if (q >= dd)
				continue;
			unsigned long k = 256 - q * 256 / dd;
			*p = ga_shade (c, (uint32_t) (k * k >> 8));
		}
	return s;
}

/* --- texte pre-rendu ---------------------------------------------------------------------------------------------
 * Entre ui_canvas_begin et ui_canvas_end, avant de dessiner la couche : le coin (0,0) du canevas sert de
 * brouillon. texte, corps, police, couleur du haut, du bas -> sprite */
static inline gf_sprite_t *ga_text (const char *t, unsigned long px, long font, uint32_t c1, uint32_t c2)
{
	t = gf_tr (t);
	unsigned long w = (unsigned long) ui_text_width ((long) t, (long) px, font) + 4;
	if (w > GF_W)
		w = GF_W;
	unsigned long h = px + (px >> 2) + 4;
	ui_fill (0, 0, (long) w, (long) h, 0);
	ui_text ((long) t, 1, (long) px + 1, 0x00FFFFFF, (long) px, font, 0);
	gf_sprite_t *s = ga_new (w, h);
	uint32_t *p = s + 2;
	const uint32_t *l = gf_layer;
	for (unsigned long y = 0; y < h; y++, p += w, l += GF_W)
	{
		uint32_t lc = ga_mix (c1, c2, (uint32_t) ((y << 8) / h));
		for (unsigned long x = 0; x < w; x++)
		{
			unsigned long a = l[x] & 0xFF;
			if (!a)
				continue;
			ga_put (p + x, lc, a >= 250 ? 256 : a);
		}
	}
	return s;
}

/* table de 10 sprites, corps, police, couleurs : les chiffres */
static inline void ga_digits (gf_sprite_t **tab, unsigned long px, long font, uint32_t c1, uint32_t c2)
{
	char b[2] = { 0, 0 };
	for (int d = 0; d < 10; d++)
	{
		b[0] = (char) ('0' + d);
		tab[d] = ga_text (b, px, font, c1, c2);
	}
}

/* valeur, x, y, table des chiffres, 0 gauche, 1 centre, 2 droite (x est ce bord), 4 en plus : ombre */
static inline void ga_num (unsigned long v, long x, long y, gf_sprite_t *const *tab, unsigned long flags)
{
	char t[24];
	int n = 0;
	do
		t[n++] = (char) ('0' + v % 10);
	while ((v /= 10) && n < 22);
	long cw = (long) tab[0][0] - 3;			/* (les sprites ont 4 de marge, 3 a droite) */
	unsigned long tot = (unsigned long) (cw * n);
	if ((flags & 3) == 1)
		x -= (long) (tot >> 1);
	if ((flags & 3) == 2)
		x -= (long) tot;
	while (n)
	{
		const gf_sprite_t *s = tab[t[--n] - '0'];
		if (flags & 4)
			ga_shadow (x + 1, y + 2, s);
		ga_ablit (x, y, s);
		x += cw;
	}
}

/* --- bruit --------------------------------------------------------------------------------------------------- */
/* x, y, log2 du pas -> bruit lisse 0..255 */
static inline long ga_noise2 (unsigned long x, unsigned long y, unsigned s)
{
	unsigned long m = (1ul << s) - 1, fx = x & m, fy = y & m, t = 3 * (m + 1);
	fx = ((t - (fx << 1)) * fx * fx >> s) >> s;	/* fractions adoucies : f*f*(3-2f) */
	fy = ((t - (fy << 1)) * fy * fy >> s) >> s;
	x = (x >> s) & 63;
	y = (y >> s) & 63;
	long a = ga_nz[x + (y << 6)], b = ga_nz[((x + 1) & 63) + (y << 6)];
	long top = a + (((b - a) * (long) fx) >> s);
	y = (y + 1) & 63;
	a = ga_nz[x + (y << 6)];
	b = ga_nz[((x + 1) & 63) + (y << 6)];
	long bot = a + (((b - a) * (long) fx) >> s);
	return top + (((bot - top) * (long) fy) >> s);
}

/* x, y -> 0..255, trois octaves */
static inline unsigned long ga_fbm (unsigned long x, unsigned long y)
{
	long a = ga_noise2 (x, y, 7), b = ga_noise2 (x, y, 6), c = ga_noise2 (x, y, 5);
	return (unsigned long) (c + b + (a << 1)) >> 2;
}

/* --- particules ---------------------------------------------------------------------------------------------- */
/* x, y (points), vx, vy (16.16 par image), couleur, vie (images), taille, gravite (16.16) */
static inline void ga_part (long x, long y, long vx, long vy, uint32_t c, long life, long size, long grav)
{
	unsigned long i = ga_pnext;
	ga_pnext = i + 1 >= GA_NP ? 0 : i + 1;
	struct ga_part_t *p = &ga_parts[i];
	p->x = (int32_t) (x << 16);
	p->y = (int32_t) (y << 16);
	p->vx = (int32_t) vx;
	p->vy = (int32_t) vy;
	p->life = (int32_t) life;
	p->col = c;
	p->size = (int32_t) size;
	p->grav = (int32_t) grav;
}

/* x, y, couleur, nombre, vitesse (16.16), vie, taille, gravite : une gerbe dans toutes les directions */
static inline void ga_burst (long x, long y, uint32_t c, long n, long speed, long life, long size, long grav)
{
	while (n)
	{
		n--;
		unsigned long a = gf_below (256);
		long cx = (long) ga_sin8 (a) - 127, cy = (long) ga_sin8 (a + 64) - 127;
		long v = ((long) gf_below (256) + 64) * speed >> 8;	/* vitesse au hasard */
		long vx = cx * v >> 7, vy = cy * v >> 7;
		long l = life + (long) gf_below ((unsigned long) life);
		ga_part (x, y, vx, vy, c, l, size, grav);
	}
}

static inline void ga_parts_step (void)
{
	for (int i = 0; i < GA_NP; i++)
	{
		struct ga_part_t *p = &ga_parts[i];
		if (!p->life)
			continue;
		p->life--;
		p->x = (int32_t) ((uint32_t) p->x + (uint32_t) p->vx);
		p->y = (int32_t) ((uint32_t) p->y + (uint32_t) p->vy);
		p->vy = (int32_t) ((uint32_t) p->vy + (uint32_t) p->grav);
	}
}

static inline void ga_parts_draw (void)
{
	for (int i = 0; i < GA_NP; i++)
	{
		struct ga_part_t *p = &ga_parts[i];
		if (!p->life)
			continue;
		uint32_t f = (uint32_t) p->life << 4;
		if (f > 256)
			f = 256;
		gf_rect (p->x >> 16, p->y >> 16, (uint32_t) p->size, (uint32_t) p->size, ga_shade (p->col, f));
	}
}

/* plus de particules */
static inline void ga_parts_clear (void)
{
	for (int i = 0; i < GA_NP; i++)
		ga_parts[i].life = 0;
}

/* --- zones d'appui -------------------------------------------------------------------------------------------- */
struct ga_zone_t { unsigned short x, y, w, h, id; };	/* fin : id 0 */
/* table, point -> id (0 : aucun), *e = l'entree */
static inline unsigned long ga_zone (const struct ga_zone_t *z, unsigned long x, unsigned long y, const struct ga_zone_t **e)
{
	for (; z->id; z++)
		if (x - z->x < z->w && y - z->y < z->h)
		{
			if (e) *e = z;
			return z->id;
		}
	return 0;
}

/* entree de zone, rayon, couleur, opacite, filet : son panneau */
static inline void ga_zone_panel (const struct ga_zone_t *z, unsigned long r, uint32_t c, unsigned long op, uint32_t line)
{
	ga_panel (z->x, z->y, z->w, z->h, r, c, c, op, line);
}

/* --- ce qui revient dans chaque jeu -------------------------------------------------------------------------- */
/* haut, bas, nuages, etoiles : un ciel dans la couche */
static inline void ga_sky (uint32_t top, uint32_t bot, uint32_t cloud, unsigned long stars)
{
	uint32_t *p = gf_layer;
	for (unsigned long y = 0; y < GF_H; y++)
	{
		uint32_t lc = ga_mix (top, bot, (uint32_t) ((y << 8) / GF_H));
		for (unsigned long x = 0; x < GF_W; x++)
		{
			unsigned long n = ga_fbm (x, y);	/* les nuages seulement au-dessus du seuil */
			n = n < 96 ? 0 : n - 96;
			unsigned long k = n << 1;
			if (k > 160)
				k = 160;
			*p++ = ga_mix (lc, cloud, (uint32_t) k);
		}
	}
	p = gf_layer;
	while (stars)
	{
		unsigned long i = gf_below ((unsigned long) GF_W * GF_H);
		uint32_t v = (uint32_t) gf_below (120) + 100;
		p[i] = v | v << 8 | v << 16;
		stars--;
	}
}

/* une table de textes : texte, corps, gras, deux couleurs */
struct ga_txe { const char *s; unsigned char px, bold; uint32_t c1, c2; };
/* table, sprites, nombre (entre ui_canvas_begin et _end) */
static inline void ga_texts (const struct ga_txe *t, gf_sprite_t **out, int n)
{
	for (int i = 0; i < n; i++)
		out[i] = ga_text (t[i].s, t[i].px, t[i].bold ? (long) ui.font_b : (long) ui.font, t[i].c1, t[i].c2);
}

/* sprite, x du centre, y : ombre et texte */
static inline void ga_show (const gf_sprite_t *s, long cx, long y)
{
	long x = cx - (long) (s[0] >> 1);
	ga_shadow (x + 2, y + 3, s);
	ga_ablit (x, y, s);
}

/* lueur 0, lueur 1, compteur : deux lumieres derivent sur le decor */
static inline void ga_orbs (const gf_sprite_t *o0, const gf_sprite_t *o1, unsigned long t)
{
	long a = (long) ga_sin8 (t >> 2) << 1, b = (long) ga_sin8 ((t >> 3) + 40);
	gf_bglow (a, b, o0);
	a = (long) ga_sin8 ((t >> 3) + 128) << 1;
	b = (long) ga_sin8 ((t >> 2) + 200);
	gf_bglow (a + 20, b + 120, o1);
}

#endif
