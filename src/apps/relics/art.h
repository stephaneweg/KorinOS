/*
 * art.h -- les images de Relics, toutes generees au demarrage (rien n'est lu) : un petit atelier de formes
 * lissees (disques, capsules, boites arrondies, triangles) dans des sprites a alpha premultiplie (ceux de
 * gamedraw.h), l'explorateur et ses ennemis, objets et pieges, les quatre decors du fond (1024 x 480, sans couture
 * a l'horizontale), et l'image d'un niveau entier, faite point par point depuis la carte (pierres sans couture,
 * chapeau de mousse, de sable, de neige ou de braise, ombres sur le mur du fond). Inclus par main.c.
 */

/* ---- l'atelier ------------------------------------------------------------------------------------------------ */
static gf_sprite_t *sp_cur;
static int sp_w, sp_h;
static float sp_k = 1;				/* l'echelle : les formes sont donnees en points de dessin, posees a sp_k */

static inline float clampf (float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float mixf (float a, float b, float t) { return a + (b - a) * t; }

static void sp_use (gf_sprite_t *s) { sp_cur = s; sp_w = (int) s[0]; sp_h = (int) s[1]; }
static gf_sprite_t *sp_new (int w, int h)
{
	gf_sprite_t *s = ga_new ((unsigned long) ceilf ((float) w * sp_k), (unsigned long) ceilf ((float) h * sp_k));
	sp_use (s);
	return s;
}

/* x, y, couleur 0xRRGGBB, couverture 0..1 : pose par-dessus (alpha premultiplie) */
static inline void sp_over (int x, int y, uint32_t c, float a)
{
	if ((unsigned) x >= (unsigned) sp_w || (unsigned) y >= (unsigned) sp_h || a <= 0.004f)
		return;
	uint32_t A = a >= 1 ? 256 : (uint32_t) (a * 256), ia = 256 - A;
	uint32_t *p = sp_cur + 2 + y * sp_w + x, d = *p;
	uint32_t r = (((c >> 16) & 255) * A + ((d >> 16) & 255) * ia) >> 8;
	uint32_t g = (((c >> 8) & 255) * A + ((d >> 8) & 255) * ia) >> 8;
	uint32_t b = ((c & 255) * A + (d & 255) * ia) >> 8;
	uint32_t al = (255 * A + (d >> 24) * ia) >> 8;
	*p = al << 24 | r << 16 | g << 8 | b;
}

/* la forme : une distance signee (negative dedans) -> sa couverture lissee, couleur du haut et du bas */
typedef float (*sp_sdf_t) (float x, float y, const float *k);
static void sp_shape (sp_sdf_t f, const float *kk, float x0, float y0, float x1, float y1, uint32_t ct, uint32_t cb, float op)
{
	float k = sp_k, ik = 1 / sp_k;
	int ix0 = (int) floorf (x0 * k) - 1, iy0 = (int) floorf (y0 * k) - 1, ix1 = (int) ceilf (x1 * k) + 1, iy1 = (int) ceilf (y1 * k) + 1;
	if (ix0 < 0) ix0 = 0;
	if (iy0 < 0) iy0 = 0;
	if (ix1 > sp_w) ix1 = sp_w;
	if (iy1 > sp_h) iy1 = sp_h;
	float hh = y1 - y0 > 0.5f ? y1 - y0 : 1;
	for (int y = iy0; y < iy1; y++)
	{
		float py = ((float) y + 0.5f) * ik;
		uint32_t c = ct == cb ? ct : ga_mix (ct, cb, (uint32_t) (clampf ((py - y0) / hh, 0, 1) * 256));
		for (int x = ix0; x < ix1; x++)
		{
			float d = f (((float) x + 0.5f) * ik, py, kk) * k;
			if (d < 0.5f)
				sp_over (x, y, c, clampf (0.5f - d, 0, 1) * op);
		}
	}
}

static float sdf_disc (float x, float y, const float *k) { float dx = x - k[0], dy = y - k[1]; return sqrtf (dx * dx + dy * dy) - k[2]; }
static float sdf_ell (float x, float y, const float *k)
{
	float dx = (x - k[0]) / k[2], dy = (y - k[1]) / k[3], q = sqrtf (dx * dx + dy * dy);
	return (q - 1) * (k[2] < k[3] ? k[2] : k[3]);
}
static float sdf_cap (float x, float y, const float *k)
{
	float ax = x - k[0], ay = y - k[1], bx = k[2] - k[0], by = k[3] - k[1];
	float l = bx * bx + by * by, t = l > 0 ? clampf ((ax * bx + ay * by) / l, 0, 1) : 0;
	float dx = ax - bx * t, dy = ay - by * t;
	return sqrtf (dx * dx + dy * dy) - k[4];
}
static float sdf_box (float x, float y, const float *k)
{
	float cx = (k[0] + k[2]) * 0.5f, cy = (k[1] + k[3]) * 0.5f, hx = (k[2] - k[0]) * 0.5f, hy = (k[3] - k[1]) * 0.5f, r = k[4];
	float qx = fabsf (x - cx) - hx + r, qy = fabsf (y - cy) - hy + r;
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
	return sqrtf (ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}
static float sdf_tri (float x, float y, const float *k)
{
	float d = -1e9f;
	float s = (k[2] - k[0]) * (k[5] - k[1]) - (k[3] - k[1]) * (k[4] - k[0]);	/* sens */
	for (int i = 0; i < 3; i++)
	{
		float ax = k[i * 2], ay = k[i * 2 + 1], bx = k[(i * 2 + 2) % 6], by = k[(i * 2 + 3) % 6];
		float ex = bx - ax, ey = by - ay, l = sqrtf (ex * ex + ey * ey);
		if (l <= 0) continue;
		float e = ((x - ax) * ey - (y - ay) * ex) / l;
		if (s < 0) e = -e;
		if (e > d) d = e;
	}
	return d;
}

static void sp_disc (float x, float y, float r, uint32_t c) { float k[3] = { x, y, r }; sp_shape (sdf_disc, k, x - r, y - r, x + r, y + r, c, c, 1); }
static void sp_disc_g (float x, float y, float r, uint32_t ct, uint32_t cb) { float k[3] = { x, y, r }; sp_shape (sdf_disc, k, x - r, y - r, x + r, y + r, ct, cb, 1); }
static void sp_ell (float x, float y, float rx, float ry, uint32_t c) { float k[4] = { x, y, rx, ry }; sp_shape (sdf_ell, k, x - rx, y - ry, x + rx, y + ry, c, c, 1); }
static void sp_cap (float x0, float y0, float x1, float y1, float r, uint32_t c)
{
	float k[5] = { x0, y0, x1, y1, r };
	sp_shape (sdf_cap, k, fminf (x0, x1) - r, fminf (y0, y1) - r, fmaxf (x0, x1) + r, fmaxf (y0, y1) + r, c, c, 1);
}
static void sp_box_g (float x0, float y0, float x1, float y1, float r, uint32_t ct, uint32_t cb)
{
	float k[5] = { x0, y0, x1, y1, r };
	sp_shape (sdf_box, k, x0, y0, x1, y1, ct, cb, 1);
}
static void sp_box (float x0, float y0, float x1, float y1, float r, uint32_t c) { sp_box_g (x0, y0, x1, y1, r, c, c); }
static void sp_tri (float ax, float ay, float bx, float by, float cx, float cy, uint32_t c)
{
	float k[6] = { ax, ay, bx, by, cx, cy };
	sp_shape (sdf_tri, k, fminf (ax, fminf (bx, cx)), fminf (ay, fminf (by, cy)), fmaxf (ax, fmaxf (bx, cx)), fmaxf (ay, fmaxf (by, cy)), c, c, 1);
}
static void sp_tri_g (float ax, float ay, float bx, float by, float cx, float cy, uint32_t ct, uint32_t cb)
{
	float k[6] = { ax, ay, bx, by, cx, cy };
	sp_shape (sdf_tri, k, fminf (ax, fminf (bx, cx)), fminf (ay, fminf (by, cy)), fmaxf (ax, fmaxf (bx, cx)), fmaxf (ay, fmaxf (by, cy)), ct, cb, 1);
}

/* un filet sombre autour de ce qui est opaque (la lisibilite des sprites de jeu) */
static void sp_outline (uint32_t c, float op)
{
	int n = sp_w * sp_h;
	unsigned char *a = (unsigned char *) malloc ((size_t) n);
	if (!a) return;
	for (int i = 0; i < n; i++) a[i] = (unsigned char) (sp_cur[2 + i] >> 24);
	for (int y = 0; y < sp_h; y++)
		for (int x = 0; x < sp_w; x++)
		{
			int self = a[y * sp_w + x];
			if (self > 200) continue;
			int m = 0;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					int X = x + dx, Y = y + dy;
					if ((dx || dy) && X >= 0 && Y >= 0 && X < sp_w && Y < sp_h && a[Y * sp_w + X] > m)
						m = a[Y * sp_w + X];
				}
			if (m > self + 40)
			{
				/* sous le point : le filet ; le point garde ce qu'il avait par-dessus */
				uint32_t old = sp_cur[2 + y * sp_w + x];
				sp_cur[2 + y * sp_w + x] = 0;
				sp_over (x, y, c, op * (float) (m - self) / 255.0f);
				if (old)
				{
					uint32_t oa = old >> 24, ia = 256 - oa, d = sp_cur[2 + y * sp_w + x];
					uint32_t r = ((old >> 16) & 255) + ((((d >> 16) & 255) * ia) >> 8), g = ((old >> 8) & 255) + ((((d >> 8) & 255) * ia) >> 8);
					uint32_t b = (old & 255) + (((d & 255) * ia) >> 8), al = oa + (((d >> 24) * ia) >> 8);
					sp_cur[2 + y * sp_w + x] = (al > 255 ? 255 : al) << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
				}
			}
		}
	free (a);
}

/* sprite -> sa copie en miroir (horizontal, ou vertical) */
static gf_sprite_t *sp_mirror (const gf_sprite_t *s, int vertical)
{
	int w = (int) s[0], h = (int) s[1];
	gf_sprite_t *d = ga_new ((unsigned long) w, (unsigned long) h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			d[2 + y * w + x] = vertical ? s[2 + (h - 1 - y) * w + x] : s[2 + y * w + (w - 1 - x)];
	return d;
}

/* une lumiere ronde a ajouter (gf_glow), diametre, couleur au centre */
static gf_sprite_t *sp_light (int d, uint32_t c) { return ga_disc ((unsigned long) d, c); }

/* ---- l'explorateur -------------------------------------------------------------------------------------------- */
/* Sprite 24 x 32, tourne a droite ; la boite du corps (12 x 26) y commence en (6, 6). */
#define SPW		24
#define SPH		32
#define SPOX		6
#define SPOY		6

struct pose { float fk[2], ff[2], bk[2], bf[2];	/* genou et pied, devant et derriere */
	      float fe[2], fh[2], be[2], bh[2];	/* coude et main */
	      float lean; int gun; };

enum { HF_STAND, HF_WALK0, HF_WALK1, HF_WALK2, HF_WALK3, HF_JUMP, HF_FALL, HF_CLIMB0, HF_CLIMB1, HF_SHOOT, HF_DROP, HF_N };

static const struct pose hero_pose[HF_N] = {
	/* debout */ { { 13.2f, 25.8f }, { 13.8f, 30.4f }, { 11.0f, 25.8f }, { 10.6f, 30.4f }, { 14.0f, 17.0f }, { 14.4f, 20.6f }, { 11.0f, 17.0f }, { 10.4f, 20.4f }, 0, 0 },
	/* marche */ { { 14.8f, 25.0f }, { 17.4f, 30.0f }, { 10.0f, 25.6f }, { 7.4f, 30.0f }, { 10.8f, 17.2f }, { 9.0f, 20.2f }, { 14.6f, 16.8f }, { 16.8f, 19.6f }, 0.4f, 0 },
	{ { 13.6f, 26.0f }, { 13.2f, 30.4f }, { 12.4f, 24.4f }, { 10.4f, 28.4f }, { 12.4f, 17.4f }, { 12.4f, 21.0f }, { 12.6f, 17.4f }, { 13.0f, 21.0f }, 0.2f, 0 },
	{ { 10.0f, 25.6f }, { 7.4f, 30.0f }, { 14.8f, 25.0f }, { 17.4f, 30.0f }, { 14.6f, 16.8f }, { 16.8f, 19.6f }, { 10.8f, 17.2f }, { 9.0f, 20.2f }, 0.4f, 0 },
	{ { 12.4f, 24.4f }, { 10.4f, 28.4f }, { 13.6f, 26.0f }, { 13.2f, 30.4f }, { 12.6f, 17.4f }, { 13.0f, 21.0f }, { 12.4f, 17.4f }, { 12.4f, 21.0f }, 0.2f, 0 },
	/* saut */ { { 15.6f, 23.0f }, { 14.6f, 27.8f }, { 11.2f, 25.0f }, { 8.2f, 28.6f }, { 15.6f, 12.4f }, { 17.4f, 9.0f }, { 9.6f, 15.2f }, { 6.4f, 13.4f }, 0.6f, 0 },
	/* chute */ { { 13.8f, 26.0f }, { 15.4f, 30.4f }, { 10.4f, 25.4f }, { 8.6f, 29.8f }, { 16.0f, 12.0f }, { 17.6f, 8.4f }, { 9.0f, 12.2f }, { 7.4f, 8.6f }, 0, 0 },
	/* echelle */ { { 13.0f, 22.8f }, { 13.0f, 27.4f }, { 11.0f, 26.0f }, { 11.0f, 30.6f }, { 14.8f, 10.8f }, { 13.6f, 6.8f }, { 10.0f, 15.6f }, { 11.0f, 12.0f }, 0, 0 },
	{ { 11.0f, 22.8f }, { 11.0f, 27.4f }, { 13.0f, 26.0f }, { 13.0f, 30.6f }, { 10.0f, 10.8f }, { 11.0f, 6.8f }, { 14.8f, 15.6f }, { 13.6f, 12.0f }, 0, 0 },
	/* tir */ { { 13.6f, 25.8f }, { 15.4f, 30.4f }, { 10.6f, 25.8f }, { 8.6f, 30.4f }, { 16.6f, 14.6f }, { 20.4f, 14.6f }, { 10.8f, 17.0f }, { 10.0f, 20.6f }, 0, 1 },
	/* pose de la dynamite (accroupi) */ { { 16.0f, 26.6f }, { 15.6f, 30.6f }, { 9.6f, 27.2f }, { 9.0f, 30.6f }, { 15.4f, 22.0f }, { 17.4f, 27.6f }, { 11.0f, 21.0f }, { 12.4f, 26.0f }, 0, 0 },
};

/* un personnage : palette (chapeau, peau, chemise, foulard, pantalon, bottes, sac), pose, decalage vertical */
struct look { uint32_t hat, skin, shirt, scarf, pants, boots, pack, gun; int kind; };

enum { LK_HERO, LK_NATIVE, LK_MUMMY, LK_SOLDIER, LK_ROBOT };

static void draw_figure (const struct look *L, const struct pose *P, int shooter)
{
	float dy = P == &hero_pose[HF_DROP] ? 4.5f : 0;
	uint32_t pantsD = ga_shade (L->pants, 190), shirtD = ga_shade (L->shirt, 200), skinD = ga_shade (L->skin, 210);
	int k = L->kind;
	/* le bras de derriere */
	uint32_t armB = k == LK_MUMMY ? ga_shade (L->shirt, 180) : k == LK_ROBOT ? ga_shade (L->shirt, 170) : shirtD;
	sp_cap (12.6f, 13.2f + dy, P->be[0], P->be[1] + dy, 1.55f, armB);
	sp_cap (P->be[0], P->be[1] + dy, P->bh[0], P->bh[1] + dy, 1.4f, k == LK_HERO || k == LK_NATIVE ? skinD : armB);
	/* la jambe de derriere */
	sp_cap (11.6f, 21.0f + dy * 0.6f, P->bk[0], P->bk[1] + dy * 0.3f, 1.9f, pantsD);
	sp_cap (P->bk[0], P->bk[1] + dy * 0.3f, P->bf[0], P->bf[1] - 1.0f, 1.7f, pantsD);
	sp_box (P->bf[0] - 1.8f, P->bf[1] - 2.2f, P->bf[0] + 2.6f, P->bf[1] + 0.9f, 1.0f, ga_shade (L->boots, 200));
	/* le sac (l'explorateur), le carquois (l'indigene) */
	if (k == LK_HERO)
		sp_box_g (5.4f, 12.2f + dy, 9.6f, 20.2f + dy, 1.6f, ga_shade (L->pack, 300), ga_shade (L->pack, 210));
	if (k == LK_SOLDIER)
		sp_box_g (5.6f, 12.4f + dy, 9.2f, 19.6f + dy, 1.2f, 0x00586048, 0x00384030);
	/* la jambe de devant */
	sp_cap (12.6f, 21.0f + dy * 0.6f, P->fk[0], P->fk[1] + dy * 0.3f, 1.95f, L->pants);
	sp_cap (P->fk[0], P->fk[1] + dy * 0.3f, P->ff[0], P->ff[1] - 1.0f, 1.75f, L->pants);
	sp_box_g (P->ff[0] - 1.8f, P->ff[1] - 2.2f, P->ff[0] + 2.8f, P->ff[1] + 0.9f, 1.0f, ga_shade (L->boots, 320), L->boots);
	/* le buste */
	if (k == LK_NATIVE)
	{
		sp_box_g (9.0f, 11.6f + dy, 15.6f, 20.0f + dy, 2.4f, ga_shade (L->skin, 290), L->skin);
		sp_box_g (8.4f, 18.6f + dy, 16.4f, 23.6f + dy, 1.4f, ga_shade (L->shirt, 300), L->shirt);	/* le pagne d'herbes */
		for (int i = 0; i < 5; i++)
			sp_cap (9.2f + i * 1.6f, 21.0f + dy, 9.0f + i * 1.7f, 24.6f + dy, 0.55f, ga_shade (L->shirt, 170));
		sp_cap (9.6f, 15.0f + dy, 15.0f, 15.0f + dy, 0.6f, L->scarf);		/* une peinture */
	}
	else if (k == LK_ROBOT)
	{
		sp_box_g (8.2f, 11.4f + dy, 16.4f, 21.6f + dy, 2.0f, ga_shade (L->shirt, 330), ga_shade (L->shirt, 190));
		sp_box (10.0f, 14.0f + dy, 14.6f, 17.0f + dy, 1.0f, 0x00202830);
		sp_disc (12.3f, 15.5f + dy, 1.0f, L->scarf);
		sp_box (9.0f, 20.4f + dy, 15.8f, 22.6f + dy, 0.8f, 0x00404850);
	}
	else
	{
		sp_box_g (8.4f, 11.4f + dy, 16.2f, 21.8f + dy, 2.6f, ga_shade (L->shirt, 300), ga_shade (L->shirt, 220));
		if (k == LK_MUMMY)
			for (int i = 0; i < 4; i++)
				sp_cap (8.8f, 13.0f + i * 2.4f + dy, 15.8f, 12.0f + i * 2.4f + dy, 0.45f, ga_shade (L->shirt, 170));
		else
		{
			sp_box (8.4f, 19.6f + dy, 16.2f, 21.2f + dy, 0.6f, ga_shade (L->pants, 150));	/* la ceinture */
			sp_disc (14.0f, 20.4f + dy, 0.8f, 0x00E8C060);
		}
		if (k == LK_HERO)
			sp_cap (12.2f, 12.4f + dy, 7.8f, 17.6f + dy, 0.6f, ga_shade (L->pack, 170));	/* la sangle */
		if (k == LK_SOLDIER)
			sp_cap (9.0f, 12.6f + dy, 15.6f, 19.0f + dy, 0.6f, 0x00303830);
	}
	/* la tete */
	float hx = 13.0f + P->lean, hy = 7.6f + dy;
	if (k == LK_ROBOT)
	{
		sp_cap (13.0f + P->lean, 1.0f + dy, 13.0f + P->lean, 4.0f + dy, 0.5f, 0x00A0A8B0);
		sp_disc (13.0f + P->lean, 1.0f + dy, 1.1f, L->scarf);
		sp_box_g (9.0f + P->lean, 3.4f + dy, 17.0f + P->lean, 11.4f + dy, 2.0f, ga_shade (L->shirt, 340), ga_shade (L->shirt, 200));
		sp_box (12.4f + P->lean, 5.6f + dy, 17.4f + P->lean, 8.0f + dy, 1.0f, 0x00200808);
		sp_box (13.4f + P->lean, 6.2f + dy, 16.8f + P->lean, 7.4f + dy, 0.5f, L->scarf);
	}
	else if (k == LK_MUMMY)
	{
		sp_disc_g (hx, hy, 3.9f, ga_shade (L->shirt, 300), ga_shade (L->shirt, 230));
		for (int i = 0; i < 3; i++)
			sp_cap (hx - 3.4f, hy - 2.4f + i * 2.2f, hx + 3.6f, hy - 3.0f + i * 2.4f, 0.4f, ga_shade (L->shirt, 170));
		sp_disc (hx + 2.0f, hy - 0.4f, 0.9f, L->scarf);
	}
	else
	{
		if (k == LK_SOLDIER)
			sp_disc_g (hx - 0.4f, hy - 0.2f, 5.0f, 0x00F0F0EC, 0x00C8C8C0);	/* la capuche fourree */
		sp_disc_g (hx, hy, 3.8f, ga_shade (L->skin, 290), L->skin);
		sp_disc (hx - 1.6f, hy + 0.3f, 0.9f, skinD);				/* l'oreille */
		sp_disc (hx + 3.6f, hy + 0.5f, 0.9f, L->skin);				/* le nez */
		sp_disc (hx + 1.9f, hy - 0.7f, 0.62f, 0x00201410);			/* l'oeil */
		if (k == LK_HERO)
		{
			sp_box (hx - 2.4f, hy + 2.6f, hx + 2.2f, hy + 4.6f, 0.8f, L->scarf);	/* le foulard */
			sp_box_g (hx - 3.8f, hy - 6.6f, hx + 3.4f, hy - 2.2f, 1.7f, ga_shade (L->hat, 320), L->hat);
			sp_box (hx - 3.8f, hy - 3.6f, hx + 3.4f, hy - 2.4f, 0.4f, ga_shade (L->hat, 120));
			sp_ell (hx - 0.1f, hy - 2.2f, 7.0f, 1.35f, ga_shade (L->hat, 190));
		}
		else if (k == LK_NATIVE)
		{
			sp_box_g (hx - 1.2f, hy - 4.6f, hx + 4.4f, hy + 3.6f, 2.4f, 0x00D49A50, 0x00986228);	/* le masque */
			sp_cap (hx + 0.2f, hy - 2.6f, hx + 3.6f, hy - 2.6f, 0.45f, 0x00F4ECD8);
			sp_cap (hx + 0.2f, hy + 1.8f, hx + 3.6f, hy + 1.8f, 0.45f, 0x00F4ECD8);
			sp_disc (hx + 2.4f, hy - 0.6f, 0.75f, 0x00180C04);
			sp_cap (hx - 1.0f, hy - 3.4f, hx - 3.0f, hy - 8.4f, 0.8f, 0x00E04030);	/* les plumes */
			sp_cap (hx + 0.4f, hy - 4.0f, hx + 0.2f, hy - 9.4f, 0.8f, 0x00F0C030);
			sp_cap (hx + 1.8f, hy - 4.0f, hx + 3.6f, hy - 8.6f, 0.8f, 0x0040B060);
		}
		else if (k == LK_SOLDIER)
		{
			sp_box (hx - 0.4f, hy - 1.8f, hx + 4.4f, hy + 0.2f, 0.8f, 0x00303A48);	/* les lunettes */
			sp_disc (hx + 2.8f, hy - 0.8f, 0.7f, 0x0090D8FF);
		}
	}
	/* l'arme (pose de tir, le tireur) */
	int gun = P->gun || shooter;
	if (gun)
	{
		float gx = P->fh[0], gy = P->fh[1] + dy;
		if (k == LK_NATIVE)
			sp_cap (gx - 2.0f, gy - 0.6f, gx + 6.0f, gy - 1.4f, 0.75f, 0x00A07040);	/* la sarbacane */
		else if (k == LK_ROBOT)
		{
			sp_box_g (gx - 1.0f, gy - 2.2f, gx + 4.4f, gy + 0.6f, 0.8f, 0x00A0B0C0, 0x00606C78);
			sp_disc (gx + 4.2f, gy - 0.8f, 1.0f, 0x0060E8FF);
		}
		else if (k == LK_MUMMY)
			sp_disc (gx + 1.2f, gy - 0.6f, 1.6f, 0x00E8C040);		/* un scarabee d'or */
		else
		{
			sp_box (gx - 0.6f, gy - 2.0f, gx + 4.6f, gy - 0.2f, 0.5f, L->gun);
			sp_box (gx - 0.4f, gy - 0.8f, gx + 1.2f, gy + 1.8f, 0.4f, ga_shade (L->gun, 170));
		}
	}
	/* le bras de devant */
	uint32_t armF = k == LK_NATIVE ? L->skin : L->shirt;
	sp_cap (13.0f, 13.2f + dy, P->fe[0], P->fe[1] + dy, 1.6f, armF);
	sp_cap (P->fe[0], P->fe[1] + dy, P->fh[0], P->fh[1] + dy, 1.45f, k == LK_HERO ? L->skin : armF);
	if (k != LK_ROBOT)
		sp_disc (P->fh[0], P->fh[1] + dy, 1.45f, k == LK_HERO || k == LK_NATIVE || k == LK_SOLDIER ? L->skin : armF);
	/* la lance de l'indigene qui marche */
	if (k == LK_NATIVE && !gun)
	{
		sp_cap (P->fh[0] - 3.0f, P->fh[1] + 6.0f, P->fh[0] + 3.0f, P->fh[1] - 14.0f, 0.55f, 0x00805830);
		sp_tri (P->fh[0] + 1.8f, P->fh[1] - 13.0f, P->fh[0] + 4.6f, P->fh[1] - 13.6f, P->fh[0] + 3.8f, P->fh[1] - 18.0f, 0x00C8D0D8);
	}
	sp_outline (0x00140C08, 1.0f);
	sp_outline (0x00140C08, 0.55f);			/* (deux passes : le trait epais des tuiles) */
}

/* ---- les ennemis volants, les araignees ------------------------------------------------------------------------ */
static gf_sprite_t *gen_bat (uint32_t c, int up)
{
	gf_sprite_t *s = sp_new (26, 16);
	uint32_t d = ga_shade (c, 180);
	if (up)
	{
		sp_tri (12.0f, 8.0f, 1.0f, 1.5f, 6.0f, 9.5f, d);
		sp_tri (14.0f, 8.0f, 25.0f, 1.5f, 20.0f, 9.5f, d);
	}
	else
	{
		sp_tri (12.0f, 7.0f, 1.0f, 13.5f, 7.0f, 6.0f, d);
		sp_tri (14.0f, 7.0f, 25.0f, 13.5f, 19.0f, 6.0f, d);
	}
	sp_ell (13.0f, 8.4f, 4.2f, 3.6f, c);
	sp_tri (10.4f, 6.0f, 11.6f, 2.4f, 12.4f, 5.6f, c);
	sp_tri (13.6f, 5.6f, 14.4f, 2.4f, 15.6f, 6.0f, c);
	sp_disc (11.6f, 7.8f, 0.8f, 0x00FF5030);
	sp_disc (14.4f, 7.8f, 0.8f, 0x00FF5030);
	sp_outline (0x00100808, 0.9f);
	return s;
}

static gf_sprite_t *gen_spider (uint32_t c, int f)
{
	gf_sprite_t *s = sp_new (22, 16);
	uint32_t d = ga_shade (c, 160);
	for (int i = 0; i < 4; i++)
	{
		float a = (float) i * 0.55f + 0.35f + (f ? 0.12f : -0.12f) * (i & 1 ? 1 : -1);
		float lx = 9.0f * cosf (a), ly = 6.0f * sinf (a);
		sp_cap (11.0f, 8.0f, 11.0f - lx * 0.6f, 8.0f - 3.5f + ly * 0.3f, 0.7f, d);
		sp_cap (11.0f - lx * 0.6f, 4.5f + ly * 0.3f, 11.0f - lx, 9.0f + ly, 0.6f, d);
		sp_cap (11.0f, 8.0f, 11.0f + lx * 0.6f, 8.0f - 3.5f + ly * 0.3f, 0.7f, d);
		sp_cap (11.0f + lx * 0.6f, 4.5f + ly * 0.3f, 11.0f + lx, 9.0f + ly, 0.6f, d);
	}
	sp_ell (11.0f, 7.0f, 4.6f, 4.0f, c);
	sp_disc (11.0f, 10.6f, 2.6f, ga_shade (c, 130));
	sp_disc (10.0f, 11.2f, 0.65f, 0x00FF4040);
	sp_disc (12.0f, 11.2f, 0.65f, 0x00FF4040);
	sp_disc (11.0f, 5.6f, 1.2f, 0x00E04040);
	sp_outline (0x00080808, 0.8f);
	return s;
}

/* ---- objets et pieges --------------------------------------------------------------------------------------- */
static gf_sprite_t *gen_gem (uint32_t c)
{
	gf_sprite_t *s = sp_new (16, 16);
	sp_tri (8.0f, 14.5f, 1.5f, 6.0f, 14.5f, 6.0f, ga_shade (c, 200));
	sp_box_g (2.5f, 3.0f, 13.5f, 6.6f, 1.0f, ga_shade (c, 360), ga_shade (c, 280));
	sp_tri (5.0f, 6.2f, 11.0f, 6.2f, 8.0f, 13.0f, ga_shade (c, 260));
	sp_tri (4.0f, 3.6f, 7.0f, 3.6f, 5.0f, 5.8f, 0x00FFFFFF);
	sp_outline (0x00100810, 0.8f);
	return s;
}

static gf_sprite_t *gen_coin (void)
{
	gf_sprite_t *s = sp_new (16, 16);
	sp_disc_g (8.0f, 8.0f, 6.4f, 0x00FFE890, 0x00C88A18);
	sp_disc_g (8.0f, 8.0f, 4.6f, 0x00D89A28, 0x00FFE070);
	sp_box (6.6f, 5.0f, 9.4f, 11.0f, 0.8f, 0x00FFF0B0);
	sp_outline (0x00281800, 0.8f);
	return s;
}

static gf_sprite_t *gen_vase (uint32_t c)
{
	gf_sprite_t *s = sp_new (16, 18);
	sp_ell (8.0f, 11.0f, 5.4f, 5.4f, c);
	sp_box (5.6f, 2.0f, 10.4f, 7.0f, 1.2f, c);
	sp_ell (8.0f, 2.4f, 4.0f, 1.2f, ga_shade (c, 330));
	sp_cap (3.6f, 9.6f, 12.4f, 9.6f, 0.7f, 0x00E8C048);
	sp_cap (4.0f, 13.0f, 12.0f, 13.0f, 0.5f, ga_shade (c, 160));
	sp_ell (6.0f, 9.0f, 1.2f, 2.0f, ga_shade (c, 340));
	sp_outline (0x00180C04, 0.8f);
	return s;
}

static gf_sprite_t *gen_relic (void)
{
	gf_sprite_t *s = sp_new (20, 24);
	uint32_t g1 = 0x00FFE070, g2 = 0x00C07810;
	sp_box_g (3.0f, 19.0f, 17.0f, 23.0f, 1.2f, 0x00A88A60, 0x00685038);	/* le socle */
	sp_box_g (6.0f, 9.0f, 14.0f, 19.4f, 2.4f, g1, g2);			/* le corps */
	sp_disc_g (10.0f, 6.4f, 4.4f, g1, g2);				/* la tete */
	sp_tri (5.6f, 4.0f, 10.0f, -0.6f, 14.4f, 4.0f, 0x00F0C040);	/* la coiffe */
	sp_cap (6.0f, 11.4f, 3.4f, 15.6f, 1.2f, g2);
	sp_cap (14.0f, 11.4f, 16.6f, 15.6f, 1.2f, g2);
	sp_disc (8.6f, 6.0f, 0.9f, 0x0040E0FF);
	sp_disc (11.4f, 6.0f, 0.9f, 0x0040E0FF);
	sp_disc (10.0f, 13.0f, 1.6f, 0x00E83040);
	sp_outline (0x00281400, 0.9f);
	return s;
}

static gf_sprite_t *gen_ammo (void)
{
	gf_sprite_t *s = sp_new (16, 14);
	sp_box_g (1.0f, 4.0f, 15.0f, 13.0f, 1.2f, 0x00788A50, 0x00485830);
	sp_box (1.0f, 4.0f, 15.0f, 6.0f, 0.6f, 0x00A0B070);
	for (int i = 0; i < 3; i++)
	{
		sp_box_g (3.4f + i * 3.6f, 0.6f, 5.6f + i * 3.6f, 5.0f, 1.0f, 0x00FFE090, 0x00C89030);
	}
	sp_box (5.0f, 8.0f, 11.0f, 10.6f, 0.5f, 0x00E8E0C0);
	sp_outline (0x00101008, 0.85f);
	return s;
}

static gf_sprite_t *gen_dynamite (int lit)
{
	gf_sprite_t *s = sp_new (16, 16);
	for (int i = 0; i < 3; i++)
		sp_box_g (2.6f + i * 3.8f, 5.0f, 5.8f + i * 3.8f, 15.0f, 1.4f, 0x00F05040, 0x00A01810);
	sp_box (2.2f, 8.6f, 13.8f, 10.4f, 0.4f, 0x00302020);
	sp_cap (8.0f, 5.0f, 10.0f, 1.0f, 0.5f, 0x00B0A090);
	if (lit)
	{
		sp_disc (10.4f, 1.4f, 1.8f, 0x00FFF0A0);
		sp_disc (10.4f, 1.4f, 1.0f, 0x00FFFFFF);
	}
	sp_outline (0x00180808, 0.85f);
	return s;
}

static gf_sprite_t *gen_key (void)
{
	gf_sprite_t *s = sp_new (16, 12);
	sp_disc_g (4.6f, 6.0f, 3.8f, 0x00FFE890, 0x00C88A18);
	sp_disc (4.6f, 6.0f, 1.6f, 0);
	sp_cap (7.6f, 6.0f, 14.6f, 6.0f, 1.1f, 0x00E8B840);
	sp_box (11.4f, 6.0f, 13.0f, 10.0f, 0.4f, 0x00D8A830);
	sp_box (13.4f, 6.0f, 15.0f, 9.2f, 0.4f, 0x00D8A830);
	/* le trou de l'anneau : vide (alpha a 0) */
	for (int y = 0; y < sp_h; y++)
		for (int x = 0; x < sp_w; x++)
		{
			float dx = ((float) x + 0.5f) / sp_k - 4.6f, dy = ((float) y + 0.5f) / sp_k - 6.0f;
			if (dx * dx + dy * dy < 2.0f) sp_cur[2 + y * sp_w + x] = 0;
		}
	sp_outline (0x00281800, 0.85f);
	return s;
}

static gf_sprite_t *gen_heart (void)
{
	gf_sprite_t *s = sp_new (16, 15);
	sp_disc_g (5.0f, 5.4f, 3.8f, 0x00FF8090, 0x00D02840);
	sp_disc_g (11.0f, 5.4f, 3.8f, 0x00FF8090, 0x00D02840);
	sp_tri (1.4f, 6.6f, 14.6f, 6.6f, 8.0f, 14.0f, 0x00D83048);
	sp_disc (4.6f, 4.2f, 1.2f, 0x00FFD0D8);
	sp_outline (0x00200810, 0.85f);
	return s;
}

static gf_sprite_t *gen_bullet_icon (void)
{
	gf_sprite_t *s = sp_new (8, 14);
	sp_box_g (1.4f, 5.0f, 6.6f, 13.4f, 0.6f, 0x00FFE090, 0x00B87820);
	sp_ell (4.0f, 4.6f, 2.6f, 4.0f, 0x00C8C0B0);
	sp_outline (0x00181008, 0.8f);
	return s;
}

/* le point de passage : une torche sur un piquet, eteinte ou allumee (la flamme est a part) */
static gf_sprite_t *gen_check (int lit)
{
	gf_sprite_t *s = sp_new (12, 32);
	sp_box_g (4.6f, 8.0f, 7.4f, 32.0f, 0.8f, 0x00A07848, 0x00604028);
	sp_box_g (2.4f, 4.0f, 9.6f, 9.0f, 1.6f, lit ? 0x00E0B060 : 0x00807060, lit ? 0x00A06020 : 0x00504030);
	sp_box (3.0f, 26.0f, 9.0f, 27.6f, 0.4f, 0x00403020);
	sp_outline (0x00140C04, 0.8f);
	return s;
}

/* la sortie : une arche de pierre, son ouverture sombre et sa lueur */
static gf_sprite_t *gen_exit (uint32_t stone)
{
	gf_sprite_t *s = sp_new (36, 44);
	uint32_t st1 = ga_shade (stone, 330), st2 = ga_shade (stone, 190);
	sp_box_g (1.0f, 8.0f, 35.0f, 44.0f, 2.0f, st1, st2);
	sp_disc_g (18.0f, 14.0f, 14.0f, st1, st2);
	sp_box_g (7.0f, 16.0f, 29.0f, 44.0f, 0.5f, 0x00302A40, 0x00080610);
	sp_disc_g (18.0f, 16.0f, 11.0f, 0x00484060, 0x00302A40);
	for (int i = 0; i < 6; i++)
	{
		float a = 3.14159f * (0.15f + 0.14f * (float) i);
		sp_cap (18.0f - cosf (a) * 11.5f, 16.0f - sinf (a) * 11.5f, 18.0f - cosf (a) * 15.0f, 16.0f - sinf (a) * 15.0f, 0.45f, st2);
	}
	sp_disc (18.0f, 4.0f, 2.4f, 0x0060E0FF);
	sp_outline (0x00100808, 0.85f);
	return s;
}

static gf_sprite_t *gen_boulder (uint32_t c, float rot)
{
	gf_sprite_t *s = sp_new (20, 20);
	sp_disc_g (10.0f, 10.0f, 9.2f, ga_shade (c, 330), ga_shade (c, 150));
	for (int i = 0; i < 3; i++)
	{
		float a = rot + (float) i * 2.1f;
		sp_cap (10.0f + cosf (a) * 2.0f, 10.0f + sinf (a) * 2.0f, 10.0f + cosf (a + 0.5f) * 7.4f, 10.0f + sinf (a + 0.5f) * 7.4f, 0.55f, ga_shade (c, 120));
		sp_disc (10.0f + cosf (a + 1.2f) * 5.0f, 10.0f + sinf (a + 1.2f) * 5.0f, 1.3f, ga_shade (c, 200));
	}
	sp_disc (7.0f, 6.6f, 2.0f, ga_shade (c, 380));
	sp_outline (0x00100808, 0.85f);
	return s;
}

static gf_sprite_t *gen_stalactite (uint32_t c)
{
	gf_sprite_t *s = sp_new (14, 18);
	sp_tri_g (0.6f, 0.0f, 13.4f, 0.0f, 7.0f, 17.6f, ga_shade (c, 320), ga_shade (c, 170));
	sp_tri (3.0f, 0.0f, 5.6f, 0.0f, 6.4f, 11.0f, ga_shade (c, 400));
	sp_outline (0x00100808, 0.7f);
	return s;
}

/* le pilon : un bloc de metal hérissé par-dessous */
static gf_sprite_t *gen_crusher (uint32_t c)
{
	gf_sprite_t *s = sp_new (24, 26);
	sp_box_g (1.0f, 0.0f, 23.0f, 18.0f, 2.0f, ga_shade (c, 330), ga_shade (c, 170));
	sp_box (1.0f, 7.0f, 23.0f, 9.0f, 0.4f, ga_shade (c, 120));
	for (int i = 0; i < 4; i++)
		sp_tri_g (2.0f + i * 5.0f, 17.0f, 7.0f + i * 5.0f, 17.0f, 4.5f + i * 5.0f, 25.6f, 0x00E8ECF0, 0x00808890);
	sp_disc (5.0f, 4.0f, 1.2f, ga_shade (c, 400));
	sp_disc (19.0f, 4.0f, 1.2f, ga_shade (c, 400));
	sp_outline (0x00080808, 0.85f);
	return s;
}

/* les pointes d'un piege, sorties de h points (14 au plus) */
static gf_sprite_t *gen_trapspikes (void)
{
	gf_sprite_t *s = sp_new (20, 15);
	for (int i = 0; i < 4; i++)
		sp_tri_g (0.5f + i * 5.0f, 15.0f, 5.0f + i * 5.0f, 15.0f, 2.75f + i * 5.0f, 0.5f, 0x00F4F8FF, 0x00707880);
	return s;
}

static gf_sprite_t *gen_dart (int right)
{
	gf_sprite_t *s = sp_new (16, 6);
	sp_cap (2.0f, 3.0f, 12.0f, 3.0f, 0.9f, 0x00D8B070);
	sp_tri (11.0f, 1.0f, 11.0f, 5.0f, 16.0f, 3.0f, 0x00E8F0F8);
	sp_tri (0.0f, 0.4f, 4.0f, 3.0f, 0.0f, 5.6f, 0x00F04030);
	sp_outline (0x00180808, 0.9f);
	return right ? s : sp_mirror (s, 0);
}

/* une flamme (torche, jet de feu), d'une hauteur ; f : image 0..3 */
static gf_sprite_t *gen_flame (int w, int h, int f, uint32_t outer, uint32_t inner)
{
	gf_sprite_t *s = sp_new (w, h);
	float cx = (float) w * 0.5f, sw = sinf ((float) f * 1.57f) * (float) w * 0.08f;
	sp_tri ((float) w * 0.1f, (float) h * 0.78f, (float) w * 0.9f, (float) h * 0.78f, cx + sw, 0.0f, outer);
	sp_ell (cx, (float) h * 0.74f, (float) w * 0.42f, (float) h * 0.24f, outer);
	sp_tri ((float) w * 0.28f, (float) h * 0.8f, (float) w * 0.72f, (float) h * 0.8f, cx - sw, (float) h * 0.3f, inner);
	sp_ell (cx, (float) h * 0.8f, (float) w * 0.24f, (float) h * 0.16f, 0x00FFFFE0);
	return s;
}

/* ---- les mondes ------------------------------------------------------------------------------------------------ */
struct world
{
	const char *name;
	uint32_t rock, cap, back, brick, plat, ladder, liquid, liquid2, metal, deco;
	int lava;
	struct look foe;
	uint32_t bat, spider, gem, shot;
};

static const struct world worlds[NWORLD] = {
	{ "La jungle d'Ixtala", 0x0072745E, 0x0058A838, 0x002C3628, 0x009A9070, 0x008A5A30, 0x00906838, 0x002E86B8, 0x0060C8F0, 0x00788078, 0x0040802C, 0,
	  { 0, 0x008C5A36, 0x0090A040, 0x00E8D040, 0x00604028, 0x00402818, 0, 0x00807060, LK_NATIVE }, 0x005A4438, 0x00402848, 0x0040E070, 0x00B08850 },
	{ "La pyramide de Sekhet", 0x00C49C64, 0x00E8CC8A, 0x00604A30, 0x00D8B478, 0x00987048, 0x00A07848, 0x00B08448, 0x00D8B070, 0x00908070, 0x00884A20, 0,
	  { 0, 0x00C8B890, 0x00D8CCA8, 0x0060FF80, 0x00B8A888, 0x00988868, 0, 0x00807060, LK_MUMMY }, 0x00806040, 0x00804020, 0x0040A0FF, 0x00E8C040 },
	{ "La forteresse de glace", 0x008AA2BA, 0x00F4FAFF, 0x00283850, 0x00A4B0BE, 0x007890A8, 0x008898A8, 0x002878B8, 0x0090D8F8, 0x00A0B0C0, 0x00B8E4FF, 0,
	  { 0, 0x00E8C4A0, 0x00566C88, 0x00C04040, 0x00404C5C, 0x00283038, 0, 0x00404448, LK_SOLDIER }, 0x00C8D8E8, 0x00305070, 0x00FF6090, 0x00C0C8D0 },
	{ "Le volcan du docteur Krell", 0x00584846, 0x00C05028, 0x00261E22, 0x00707C88, 0x00606A74, 0x00808890, 0x00FF6A1A, 0x00FFD050, 0x00808A94, 0x00C04020, 1,
	  { 0, 0x00A0A8B0, 0x0098A2AC, 0x00FF3020, 0x00606870, 0x00404850, 0, 0x00606870, LK_ROBOT }, 0x00803020, 0x00505860, 0x00FFB020, 0x0060E8FF },
};

/* ---- les decors du fond : 1024 x 480, sans couture --------------------------------------------------------- */
#define BG_W		1024
#define BG_H		480

/* bruit 1D sans couture sur 1024 (pas de 2^s, s <= 4) */
static inline long nz1 (long x, unsigned s, long row) { return ga_noise2 ((unsigned long) (x & 1023), (unsigned long) row, s); }

static void gen_bg (uint32_t *p, int w)
{
	for (int y = 0; y < BG_H; y++)
		for (int x = 0; x < BG_W; x++)
		{
			float fy = (float) y / BG_H;
			uint32_t c;
			if (w == 0)
			{					/* la jungle : ciel clair, montagnes, canopee */
				c = ga_mix (0x0078B8C8, 0x00D8E8C8, (uint32_t) (fy * 256));
				long m = 150 + (nz1 (x, 4, 3) - 128) / 2 + (nz1 (x, 3, 9) - 128) / 4;
				if (y > m) c = ga_mix (0x00709888, 0x00506E5C, (uint32_t) clampf ((float) (y - m) * 3, 0, 256));
				long t = 250 + (nz1 (x, 3, 17) - 128) / 2 + (nz1 (x, 2, 21) - 128) / 3;
				if (y > t)
				{
					unsigned long n = ga_noise2 ((unsigned long) x & 1023, (unsigned long) y, 3);
					c = ga_mix (0x00305A30, 0x00183018, (uint32_t) clampf ((float) (y - t) * 2, 0, 200));
					c = ga_shade (c, 200 + (uint32_t) (n >> 1));
				}
				long t2 = 360 + (nz1 (x, 2, 31) - 128) / 2;
				if (y > t2) c = ga_shade (0x00142814, 180 + (uint32_t) (ga_noise2 ((unsigned long) x & 1023, (unsigned long) y, 2) >> 2));
				if (y > 200 && y < 300)		/* la brume */
				{
					float k = 1 - fabsf ((float) (y - 250) / 50);
					c = ga_mix (c, 0x00D0E4D8, (uint32_t) (k * k * 70));
				}
				if ((x * 7 + 13) % 89 == 0 && y < t && y > 60)	/* les lianes */
				{
					long len = 40 + (x % 60);
					if (y > t - len) c = 0x00284A20;
				}
			}
			else if (w == 1)
			{					/* le desert : couchant, soleil, pyramides, dunes */
				c = fy < 0.5f ? ga_mix (0x00402858, 0x00F08850, (uint32_t) (fy * 2 * 256)) : ga_mix (0x00F08850, 0x00FFD090, (uint32_t) ((fy - 0.5f) * 2 * 256));
				float sx = (float) x - 700, sy = (float) y - 210, sd = sqrtf (sx * sx + sy * sy);
				if (sd < 46) c = ga_mix (c, 0x00FFF4C0, (uint32_t) clampf ((46 - sd) * 64, 0, 256));
				else if (sd < 160) c = ga_mix (c, 0x00FFE0A0, (uint32_t) ((160 - sd) * (160 - sd) / 160 / 2));
				for (int k = 0; k < 3; k++)		/* trois pyramides */
				{
					float px = k == 0 ? 220.0f : k == 1 ? 330.0f : 860.0f, ph = k == 1 ? 150.0f : 110.0f, base = 300;
					float dx = fabsf ((float) x - px), top = base - ph + dx;
					if ((float) y > top && (float) y < base)
						c = (float) x < px ? ga_shade (0x00A05838, 150 + (uint32_t) (dx / ph * 60)) : ga_shade (0x00C87848, 200);
				}
				long d1 = 290 + (nz1 (x, 4, 5) - 128) / 3;
				if (y > d1) c = ga_mix (0x00D89058, 0x00B87040, (uint32_t) clampf ((float) (y - d1) * 2, 0, 256));
				long d2 = 360 + (nz1 (x + 300, 4, 11) - 128) / 2;
				if (y > d2) c = ga_mix (0x00E8A868, 0x00A86438, (uint32_t) clampf ((float) (y - d2) * 1.5f, 0, 256));
			}
			else if (w == 2)
			{					/* la glace : nuit, aurore, montagnes, sapins */
				c = ga_mix (0x00080C24, 0x00304A78, (uint32_t) (fy * 256));
				long ab = 110 + (nz1 (x, 4, 7) - 128) / 2;		/* l'aurore */
				float ad = fabsf ((float) (y - ab));
				if (ad < 40)
				{
					float k = 1 - ad / 40;
					uint32_t ac = ga_mix (0x0040F0A0, 0x0060A0F0, (uint32_t) (nz1 (x, 3, 13)));
					float rays = 0.55f + 0.45f * sinf ((float) x * 0.0491f + (float) nz1 (x, 4, 41) * 0.02f);	/* (period 128 : sans couture) */
					c = ga_mix (c, ac, (uint32_t) (k * k * 120 * rays));
				}
				/* deux chaines de montagnes, des sinus sans couture sur 1024 */
				float fx = (float) x * 6.2832f / 1024;
				float m1 = 230 - 50 * fabsf (sinf (fx * 3)) - 25 * sinf (fx * 7 + 1) - (float) (nz1 (x, 4, 23) - 128) / 6;
				float m2 = 300 - 40 * fabsf (sinf (fx * 2 + 0.7f)) - 20 * sinf (fx * 5 + 2) - (float) (nz1 (x, 3, 29) - 128) / 8;
				if ((float) y > m1)
				{
					float k = clampf (((float) y - m1) / 80, 0, 1), side = cosf (fx * 3) * sinf (fx * 3) > 0 ? 1.0f : 0.82f;
					c = ga_shade (ga_mix (0x00C8D8F0, 0x00506C98, (uint32_t) (k * 256)), (uint32_t) (256 * side));
				}
				if ((float) y > m2)
				{
					float k = clampf (((float) y - m2) / 60, 0, 1), side = cosf (fx * 2 + 0.7f) * sinf (fx * 2 + 0.7f) > 0 ? 1.0f : 0.8f;
					c = ga_shade (ga_mix (0x00F4F8FF, 0x008098C0, (uint32_t) (k * 256)), (uint32_t) (256 * side));
				}
				long t = 380 + (nz1 (x, 2, 29) - 128) / 4;
				long tx = x % 46, th = 70 + (x / 46 % 5) * 12;
				if (y > t - th + (long) labs (tx - 23) * th / 26 && y > t - th) c = ga_mix (0x00203048, 0x00101828, (uint32_t) clampf ((float) (y - t + th) * 2, 0, 256));
				if (y > t) c = ga_mix (0x00C8D8F0, 0x009CB0D0, (uint32_t) clampf ((float) (y - t) * 3, 0, 256));
			}
			else
			{					/* le volcan : ciel de cendres, cone, coulees */
				unsigned long n = ga_noise2 ((unsigned long) x & 1023, (unsigned long) y, 4);
				c = ga_mix (0x00200C10, 0x00702418, (uint32_t) (fy * 256));
				c = ga_mix (c, 0x00502830, (uint32_t) (n > 140 ? (n - 140) : 0));
				float vx = (float) x - 512, vt = 170 + fabsf (vx) * 0.55f;
				if ((float) y > vt && fabsf (vx) < 420)
				{
					c = ga_shade (0x00302020, 180 + (uint32_t) (ga_noise2 ((unsigned long) x & 1023, (unsigned long) y, 3) >> 2));
					float lv = fabsf (vx + sinf ((float) y * 0.05f) * 12 - 20);
					if (lv < 4 && y < 360) c = ga_mix (c, 0x00FF8030, (uint32_t) ((4 - lv) * 64));
				}
				float gx = (float) x - 512, gy = (float) y - 170, gd = sqrtf (gx * gx + gy * gy);
				if (gd < 130) c = ga_mix (c, 0x00FF7030, (uint32_t) ((130 - gd) * (130 - gd) / 130 / 1.4f));
				long r = 400 + (nz1 (x, 3, 37) - 128) / 3;
				if (y > r) c = ga_shade (0x00281818, 200 + (uint32_t) (ga_noise2 ((unsigned long) x & 1023, (unsigned long) y, 2) >> 2));
				if (gf_below (900) == 0) c = 0x00FFA050;
			}
			*p++ = c;
		}
}

/* ---- les tuiles ---------------------------------------------------------------------------------------------------
 * Une case de la carte est une tuile de TP x TP points, dessinee au trait sombre comme des tuiles de jeu : paves
 * arrondis, blocs de temple sculptes, mur du fond en grosses pierres, rondins, echelle, pointes, eau. Chaque tuile
 * pleine connait ses bords a l'air libre (masque : haut 1, bas 2, gauche 4, droite 8) : trait epais, coins arrondis,
 * chapeau d'herbe, de sable, de neige ou de braise sur le dessus ; une tuile au coeur d'un mur est plus sombre. Faites a
 * la demande, une fois (g_tc).
 */
#define TP		32
#define M_U		1
#define M_D		2
#define M_L		4
#define M_R		8

enum { TK_BACK, TK_ROCK, TK_BRICK, TK_TRAPR, TK_TRAPB, TK_BREAK, TK_PLAT, TK_LADDER, TK_SPIKE, TK_SPIKEC, TK_LIQ, TK_LIQS,
       TK_CRUMB, TK_DOOR, TK_TORCH, TK_DECO, TK_JET, TK_DARTL, TK_DARTR, TK_N };
#define TC_N		96
static gf_sprite_t *g_tc[NWORLD][TK_N][TC_N];

/* les tuiles et decors du tileset de l'utilisateur, par monde (tools/relics/mktiles.py, main.c load_til) : s'ils sont
 * la, ils remplacent les tuiles dessinees du terrain (roche, mur du fond, planches, echelles, pointes, eau) */
enum { ET_GRASS, ET_COBBLE, ET_BACKA, ET_BACKB, ET_PLANK, ET_LOGS, ET_LADDER, ET_SPIKES, ET_WSURF, ET_WMID, ET_DECO,
       ET_BIG = ET_DECO + 8, ET_DOOR, ET_COIN, ET_POT, ET_KEY, ET_BG, ET_N };	/* (ET_DECO..+7 : les decors semes) */
static gf_sprite_t *g_til[NWORLD][ET_N];

/* une image du tileset dans la tuile en cours (TP x TP), en miroir si mirror (1 : de cote, 2 : de haut en bas) ;
 * dx : decalage horizontal circulaire (les vagues) */
static void til_copy (const gf_sprite_t *t, int mirror, int dx)
{
	int w = (int) t[0], h = (int) t[1];
	for (int y = 0; y < TP && y < h; y++)
		for (int x = 0; x < TP && x < w; x++)
		{
			int sx = mirror == 1 ? w - 1 - x : x, sy = mirror == 2 ? h - 1 - y : y;
			sx = (sx + dx) % w;
			sp_cur[2 + y * TP + x] = t[2 + sy * w + sx];
		}
}

static inline uint32_t hashu (int x, int y, int k)
{
	unsigned h = (unsigned) x * 374761393u + (unsigned) y * 668265263u + (unsigned) k * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

static uint32_t ink_of (int w) { static const uint32_t k[NWORLD] = { 0x00241A10, 0x00342010, 0x00182436, 0x00140A0A }; return k[w]; }

/* un pave arrondi, son trait, sa face eclairee d'en haut */
static void stone (float x0, float y0, float x1, float y1, float r, uint32_t c, uint32_t ink)
{
	sp_box (x0, y0, x1, y1, r, ink);
	sp_box_g (x0 + 1.2f, y0 + 1.2f, x1 - 1.2f, y1 - 1.2f, r - 1, ga_shade (c, 300), ga_shade (c, 205));
	sp_cap (x0 + r * 0.8f + 1.4f, y0 + 2.4f, x1 - r - 1.6f, y0 + 2.4f, 0.7f, ga_shade (c, 360));
}

/* les bords a l'air libre : coupe arrondie aux coins, trait epais, lumiere en haut ; puis l'assombrissement du coeur */
static void tile_edges (int mask, uint32_t ink, int deep)
{
	const float R = 7;
	for (int y = 0; y < TP; y++)
		for (int x = 0; x < TP; x++)
		{
			float px = (float) x + 0.5f, py = (float) y + 0.5f, e = 99;
			if (mask & M_L) e = fminf (e, px);
			if (mask & M_R) e = fminf (e, TP - px);
			if (mask & M_U) e = fminf (e, py);
			if (mask & M_D) e = fminf (e, TP - py);
			int cx = (mask & M_L) && px < R ? 1 : (mask & M_R) && px > TP - R ? 2 : 0;
			int cy = (mask & M_U) && py < R ? 1 : (mask & M_D) && py > TP - R ? 2 : 0;
			if (cx && cy)
			{
				float ox = cx == 1 ? R : TP - R, oy = cy == 1 ? R : TP - R;
				e = R - sqrtf ((px - ox) * (px - ox) + (py - oy) * (py - oy));
			}
			uint32_t *p = sp_cur + 2 + y * TP + x;
			if (e < 0) { *p = 0; continue; }
			if (e < 2.4f)
			{
				uint32_t a = *p >> 24;
				uint32_t v = ink | 0xFF000000;
				if (e < 1) { uint32_t k = (uint32_t) (e * 256); v = (((ink >> 16 & 255) * k >> 8) << 16) | (((ink >> 8 & 255) * k >> 8) << 8) | ((ink & 255) * k >> 8) | (k > 255 ? 255 : k) << 24; }
				(void) a;
				*p = v;
			}
		}
	if (deep)
	{
		uint32_t f = deep == 1 ? 175 : 120;
		for (int i = 0; i < TP * TP; i++)
		{
			uint32_t v = sp_cur[2 + i];
			sp_cur[2 + i] = (v & 0xFF000000) | ga_shade (v & 0xFFFFFF, f);
		}
	}
}

/* le chapeau du dessus : herbe et feuilles (jungle), sable, neige, croute de braise */
static void tile_cap (int w, uint32_t cap, uint32_t ink, int x0)
{
	for (int i = 0; i < 6; i++)
	{
		float cx = (float) i * 6.4f + 1, d = 6.0f + (float) (hashu (x0, i, 31) % 4);
		sp_disc (cx, d - 3.0f, 4.0f, ink);
	}
	sp_box (-2, -2, TP + 2, 6.5f, 0, ink);
	sp_box_g (-2, -2, TP + 2, 5.0f, 0, ga_shade (cap, 330), cap);
	for (int i = 0; i < 6; i++)
	{
		float cx = (float) i * 6.4f + 1, d = 6.0f + (float) (hashu (x0, i, 31) % 4);
		sp_disc (cx, d - 3.0f, 2.8f, cap);
	}
	if (w == 0)
	{						/* des feuilles qui debordent */
		for (int i = 0; i < 3; i++)
		{
			float lx = 4.0f + (float) i * 11.0f + (float) (hashu (x0, i, 33) % 4), ly = 7.0f + (float) (hashu (x0, i, 34) % 4);
			sp_ell (lx, ly, 3.4f, 5.0f, ink);
			sp_ell (lx, ly, 2.3f, 3.9f, ga_shade (cap, 260));
			sp_cap (lx, ly - 3, lx, ly + 3, 0.4f, ga_shade (cap, 150));
		}
	}
	else if (w == 2)
	{						/* des gouttes de neige */
		sp_ell (9, 7.5f, 2.2f, 3.2f, ink);
		sp_ell (9, 7.0f, 1.3f, 2.4f, cap);
		sp_ell (24, 6.5f, 1.8f, 2.6f, ink);
		sp_ell (24, 6.0f, 1.0f, 1.8f, cap);
	}
	else if (w == 3)
	{						/* des braises */
		sp_disc (7, 2.0f, 1.0f, 0x00FFB040);
		sp_disc (20, 3.0f, 0.8f, 0x00FFD060);
	}
	sp_cap (2, 1.2f, 10, 1.2f, 0.6f, ga_shade (cap, 420));
}

static void back_shade (int sh);

/* le mur du fond : de grosses pierres sombres ; sh : ombre du solide au-dessus (1), a gauche (2) */
static void gen_back (const struct world *W, int v, int sh)
{
	uint32_t ink = ga_shade (W->back, 55), c = W->back;
	sp_box (0, 0, TP, TP, 0, ga_shade (W->back, 70));
	static const float L[3][5][4] = {
		{ { 0, 0, 20, 14 }, { 20, 0, 32, 14 }, { 0, 14, 12, 32 }, { 12, 14, 32, 32 }, { 0, 0, 0, 0 } },
		{ { 0, 0, 14, 18 }, { 14, 0, 32, 10 }, { 14, 10, 32, 24 }, { 0, 18, 14, 32 }, { 14, 24, 32, 32 } },
		{ { 0, 0, 32, 12 }, { 0, 12, 18, 32 }, { 18, 12, 32, 22 }, { 18, 22, 32, 32 }, { 0, 0, 0, 0 } } };
	for (int i = 0; i < 5; i++)
	{
		const float *b = L[v][i];
		if (b[2] <= b[0]) continue;
		uint32_t sc = ga_shade (c, 220 + (uint32_t) (hashu (v, i, 5) % 50));
		sp_box (b[0] + 0.6f, b[1] + 0.6f, b[2] - 0.6f, b[3] - 0.6f, 4, ink);
		sp_box_g (b[0] + 1.8f, b[1] + 1.8f, b[2] - 1.8f, b[3] - 1.8f, 3, ga_shade (sc, 120), ga_shade (sc, 92));
		sp_cap (b[0] + 4, b[1] + 3, b[2] - 5, b[1] + 3, 0.6f, ga_shade (sc, 150));
	}
	back_shade (sh);
}

/* l'ombre du solide sur le mur du fond : au-dessus (1), a gauche (2) */
static void back_shade (int sh)
{
	for (int y = 0; y < TP; y++)
		for (int x = 0; x < TP; x++)
		{
			float k = 1;
			if ((sh & 1) && y < 12) k *= 0.45f + 0.55f * (float) y / 12;
			if ((sh & 2) && x < 10) k *= 0.55f + 0.45f * (float) x / 10;
			if (k < 1)
			{
				uint32_t *p = sp_cur + 2 + y * TP + x;
				*p = (*p & 0xFF000000) | ga_shade (*p & 0xFFFFFF, (uint32_t) (k * 256));
			}
		}
}

/* la roche : des paves */
static void gen_rock (int w, const struct world *W, int mask, int v, int deep, int trap)
{
	uint32_t ink = ink_of (w);
	sp_box (0, 0, TP, TP, 0, ga_shade (W->rock, 110));
	static const float L[2][5][4] = {
		{ { 0, 0, 17, 13 }, { 17, 0, 32, 11 }, { 0, 13, 12, 32 }, { 12, 13, 32, 23 }, { 12, 23, 32, 32 } },
		{ { 0, 0, 12, 16 }, { 12, 0, 32, 14 }, { 0, 16, 20, 32 }, { 20, 14, 32, 25 }, { 20, 25, 32, 32 } } };
	for (int i = 0; i < 5; i++)
	{
		const float *b = L[v][i];
		stone (b[0] + 0.5f, b[1] + 0.5f, b[2] - 0.5f, b[3] - 0.5f, 4.5f, ga_shade (W->rock, 225 + (uint32_t) (hashu (v, i, 7) % 45)), ink);
	}
	if (trap)
		for (int i = 0; i < 4; i++) sp_box (3.0f + (float) i * 7.5f, 3.2f, 5.6f + (float) i * 7.5f, 5.2f, 0.6f, 0x00100808);
	if ((mask & M_U) && !trap) tile_cap (w, W->cap, ink, v * 7 + mask);
	tile_edges (mask, ink, deep);
}

/* un bloc de temple : trait, biseau, et parfois un motif sculpte (spirale, soleil, oeil, frise) */
static void gen_brick (int w, const struct world *W, int mask, int motif, int deep, int kind)
{
	uint32_t ink = ink_of (w), c = W->brick;
	sp_box (0, 0, TP, TP, 0, ink);
	sp_box_g (1.2f, 1.2f, TP - 1.2f, TP - 1.2f, 2.5f, ga_shade (c, 290), ga_shade (c, 215));
	sp_box (1.2f, TP - 4.0f, TP - 1.2f, TP - 1.2f, 1.5f, ga_shade (c, 170));
	sp_box (TP - 4.0f, 1.2f, TP - 1.2f, TP - 1.2f, 1.5f, ga_shade (c, 180));
	sp_cap (3.5f, 2.8f, TP - 5, 2.8f, 0.8f, ga_shade (c, 360));
	sp_cap (2.8f, 3.5f, 2.8f, TP - 5, 0.8f, ga_shade (c, 330));
	uint32_t cut = ga_shade (c, 140), lit = ga_shade (c, 330);
	float m = TP / 2;
	if (kind == 1)
	{						/* fissure */
		float pts[7][2] = { { 6, 2 }, { 11, 9 }, { 8, 15 }, { 15, 20 }, { 12, 26 }, { 18, 30 }, { 18, 30 } };
		for (int i = 0; i < 5; i++) sp_cap (pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], 1.0f, ink);
		sp_cap (15, 20, 25, 17, 0.9f, ink);
		sp_cap (25, 17, 29, 22, 0.8f, ink);
		sp_cap (11, 9, 19, 6, 0.8f, ink);
		sp_disc (22, 25, 1.6f, ink);
	}
	else if (motif == 1)
	{						/* spirale */
		for (int i = 0; i < 26; i++)
		{
			float a0 = (float) i * 0.42f, a1 = a0 + 0.42f, r0 = 2 + a0 * 1.25f, r1 = 2 + a1 * 1.25f;
			sp_cap (m + 1 + cosf (a0) * r0, m + 1 + sinf (a0) * r0, m + 1 + cosf (a1) * r1, m + 1 + sinf (a1) * r1, 0.9f, lit);
			sp_cap (m + cosf (a0) * r0, m + sinf (a0) * r0, m + cosf (a1) * r1, m + sinf (a1) * r1, 0.9f, cut);
		}
	}
	else if (motif == 2)
	{						/* soleil */
		for (int i = 0; i < 8; i++)
		{
			float a = (float) i * 0.785f;
			sp_cap (m + cosf (a) * 6, m + sinf (a) * 6, m + cosf (a) * 11, m + sinf (a) * 11, 1.0f, cut);
		}
		sp_disc (m + 0.7f, m + 0.7f, 5.0f, lit);
		sp_disc (m, m, 5.0f, cut);
		sp_disc (m, m, 3.2f, ga_shade (c, 250));
	}
	else if (motif == 3)
	{						/* oeil */
		sp_ell (m, m, 10, 6, cut);
		sp_ell (m, m, 8.4f, 4.6f, ga_shade (c, 330));
		sp_disc (m, m, 3.6f, ink);
		sp_disc (m, m, 2.6f, w == 3 ? 0x00FF4020 : 0x0030A0E8);
		sp_disc (m - 1, m - 1, 0.9f, 0x00FFFFFF);
	}
	else if (motif == 4)
	{						/* frise */
		sp_box (4, 10, TP - 4, 12, 0.5f, cut);
		sp_box (4, 20, TP - 4, 22, 0.5f, cut);
		for (int i = 0; i < 4; i++) sp_box (6.0f + (float) i * 6, 13.5f, 9.0f + (float) i * 6, 18.5f, 0.8f, cut);
	}
	if (kind == 2 || kind == 3)
	{						/* une tete de pierre : la bouche du cote du tir */
		float mx = kind == 3 ? TP - 6.0f : 6.0f;
		sp_ell (m, 11, 9, 4.5f, cut);
		sp_disc (m - 4, 11, 2, ink);
		sp_disc (m + 4, 11, 2, ink);
		sp_disc (m - 4, 11, 0.9f, 0x00FF3020);
		sp_disc (m + 4, 11, 0.9f, 0x00FF3020);
		sp_ell (mx, 21, 4, 3.4f, ink);
		sp_ell (mx, 21, 2.6f, 2.2f, 0x00080404);
	}
	if (kind == 4)
		for (int i = 0; i < 4; i++) sp_box (3.0f + (float) i * 7.5f, 3.2f, 5.6f + (float) i * 7.5f, 5.2f, 0.6f, 0x00100808);
	if ((mask & M_U) && kind != 4 && w != 1) tile_cap (w, W->cap, ink, motif * 5 + mask);
	tile_edges (mask & ~(M_D | M_L | M_R) ? mask : mask, ink, deep);
}

/* une planche de rondins ; e : 1 bout a gauche, 2 bout a droite */
static void gen_plat (int w, const struct world *W, int e)
{
	uint32_t ink = ink_of (w), c = W->plat;
	float x0 = (e & 1) ? 1 : -4, x1 = (e & 2) ? TP - 1 : TP + 4;
	sp_box (x0, 1, x1, 14, 6, ink);
	sp_box_g (x0 + 1.4f, 2.4f, x1 - 1.4f, 12.6f, 5, ga_shade (c, 310), ga_shade (c, 200));
	sp_cap (x0 + 5, 4.6f, x1 - 5, 4.6f, 0.8f, ga_shade (c, 380));
	sp_cap (16, 2.5f, 16, 12.5f, 0.8f, ink);
	sp_cap (13, 9, 19, 9, 0.6f, ga_shade (c, 150));
	if (e & 1) { sp_disc (6, 7.5f, 4.2f, ga_shade (c, 330)); sp_disc (6, 7.5f, 2.4f, ga_shade (c, 220)); sp_disc (6, 7.5f, 0.9f, ga_shade (c, 150)); }
	if (e & 2) { sp_disc (TP - 6, 7.5f, 4.2f, ga_shade (c, 330)); sp_disc (TP - 6, 7.5f, 2.4f, ga_shade (c, 220)); sp_disc (TP - 6, 7.5f, 0.9f, ga_shade (c, 150)); }
	/* la corde qui la tient */
	sp_box (7, 13, 10, 18, 1, ink);
	sp_box (7.8f, 13, 9.2f, 17.2f, 0.5f, 0x00C8A060);
	sp_box (22, 13, 25, 18, 1, ink);
	sp_box (22.8f, 13, 24.2f, 17.2f, 0.5f, 0x00C8A060);
}

static void gen_ladder (int w, const struct world *W)
{
	uint32_t ink = ink_of (w), c = W->ladder;
	for (int i = 0; i < 3; i++)
	{
		float y = 4.0f + (float) i * 10.7f;
		sp_box (8, y - 2.2f, 24, y + 2.2f, 1.2f, ink);
		sp_box_g (8, y - 1.0f, 24, y + 1.0f, 0.6f, ga_shade (c, 330), ga_shade (c, 220));
	}
	for (int j = 0; j < 2; j++)
	{
		float x = j ? 24.5f : 7.5f;
		sp_box (x - 3.2f, -1, x + 3.2f, TP + 1, 1.5f, ink);
		sp_box_g (x - 2.0f, -1, x + 2.0f, TP + 1, 1, ga_shade (c, 340), ga_shade (c, 240));
		sp_cap (x - 0.8f, 0, x - 0.8f, TP, 0.5f, ga_shade (c, 420));
	}
}

static void gen_spikes (int w, const struct world *W, int ceil)
{
	uint32_t ink = ink_of (w);
	for (int i = 0; i < 3; i++)
	{
		float x0 = (float) i * 10.67f, x1 = x0 + 10.67f, cx = (x0 + x1) / 2;
		float b = ceil ? 0 : TP, t = ceil ? 18 : TP - 18;
		sp_tri (x0 - 0.5f, b, x1 + 0.5f, b, cx, t + (ceil ? 1.5f : -1.5f), ink);
		sp_tri_g (x0 + 1.3f, b + (ceil ? 0.5f : -1.0f), x1 - 1.3f, b + (ceil ? 0.5f : -1.0f), cx, t, ga_shade (W->metal, 380), ga_shade (W->metal, 220));
		sp_tri (cx, t, x0 + 1.3f, b + (ceil ? 0.5f : -1.0f), cx, b + (ceil ? 0.5f : -1.0f), ga_shade (W->metal, 420));
	}
}

/* l'eau, les sables, la lave : le fond, ou la surface (image f de 4 : la vague) */
static void gen_liquid (const struct world *W, int surf, int f)
{
	uint32_t c = W->liquid, c2 = W->liquid2;
	sp_box_g (0, 0, TP, TP, 0, c, ga_shade (c, 170));
	for (int i = 0; i < 4; i++)
	{
		float y = 6.0f + (float) i * 7, x = (float) ((hashu (i, f, 9) % 20));
		sp_cap (x, y, x + 8, y, 0.8f, ga_shade (c, W->lava ? 330 : 260));
	}
	if (W->lava)
		for (int i = 0; i < 3; i++) sp_disc ((float) (hashu (i, f, 3) % 28) + 2, (float) (hashu (i, f, 4) % 24) + 6, 1.4f, 0x00FFE070);
	if (surf)
	{
		for (int x = 0; x < TP; x++)
		{
			float wv = sinf ((float) x * 0.196f + (float) f * 1.5708f) * 1.6f;
			for (int y = 0; y < 9; y++)
			{
				uint32_t *p = sp_cur + 2 + y * TP + x;
				float top = 3 + wv;
				if ((float) y < top) *p = 0;
				else if ((float) y < top + 4) *p = c2 | 0xFF000000;
			}
		}
	}
}

/* sol qui s'effondre : une dalle fendue */
static void gen_crumb (int w, const struct world *W)
{
	uint32_t ink = ink_of (w), c = ga_mix (W->rock, 0x00B09070, 110);
	sp_box (0, 0, TP, 15, 3, ink);
	sp_box_g (1.4f, 1.4f, TP - 1.4f, 13.6f, 2, ga_shade (c, 320), ga_shade (c, 220));
	sp_cap (6, 1.5f, 10, 8, 0.8f, ink);
	sp_cap (10, 8, 8, 13.5f, 0.8f, ink);
	sp_cap (21, 1.5f, 19, 7, 0.8f, ink);
	sp_cap (19, 7, 24, 13.5f, 0.8f, ink);
	sp_cap (3, 3.2f, 8, 3.2f, 0.6f, ga_shade (c, 380));
}

static void gen_door (int w, const struct world *W, int lock)
{
	uint32_t ink = ink_of (w), c = 0x00905A30;
	sp_box (0, 0, TP, TP, 0, ink);
	for (int i = 0; i < 4; i++)
		sp_box_g (1.2f + (float) i * 7.5f, 0, 7.6f + (float) i * 7.5f, TP, 0.8f, ga_shade (c, 300), ga_shade (c, 220));
	for (int j = 0; j < 2; j++)
	{
		float y = j ? 23 : 5;
		sp_box (0, y, TP, y + 4.5f, 0.5f, ink);
		sp_box_g (0, y + 0.8f, TP, y + 3.7f, 0.5f, ga_shade (W->metal, 330), ga_shade (W->metal, 210));
		for (int i = 0; i < 4; i++) sp_disc (4.0f + (float) i * 8, y + 2.2f, 0.9f, ga_shade (W->metal, 420));
	}
	if (lock)
	{
		sp_box (10, 11, 22, 21, 2, ink);
		sp_box_g (11.2f, 12.2f, 20.8f, 19.8f, 1.5f, 0x00FFE070, 0x00C08820);
		sp_disc (16, 15, 1.6f, ink);
		sp_box (15.3f, 15, 16.7f, 18.4f, 0.4f, ink);
	}
}

static void gen_torch (int w, const struct world *W)
{
	uint32_t ink = ink_of (w);
	sp_box (13, 14, 19, 30, 1.5f, ink);
	sp_box_g (14.2f, 14, 17.8f, 29, 1, 0x00A07040, 0x00684020);
	sp_box (9, 10, 23, 16, 2, ink);
	sp_box_g (10.2f, 11.2f, 21.8f, 14.8f, 1.2f, ga_shade (W->metal, 330), ga_shade (W->metal, 200));
}

/* l'ornement du monde, sur le mur : une liane, un hieroglyphe, des stalactites de glace, un tuyau */
static void gen_deco (int w, const struct world *W, int v)
{
	uint32_t ink = ink_of (w), c = W->deco;
	if (w == 0)
	{
		float x = 10.0f + (float) v * 4;
		for (int i = 0; i < 8; i++)
		{
			float y0 = (float) i * 4, y1 = y0 + 4, a = sinf ((float) i * 0.8f + (float) v) * 2.5f, b = sinf ((float) (i + 1) * 0.8f + (float) v) * 2.5f;
			sp_cap (x + a, y0, x + b, y1, 2.0f, ink);
		}
		for (int i = 0; i < 8; i++)
		{
			float y0 = (float) i * 4, y1 = y0 + 4, a = sinf ((float) i * 0.8f + (float) v) * 2.5f, b = sinf ((float) (i + 1) * 0.8f + (float) v) * 2.5f;
			sp_cap (x + a, y0, x + b, y1, 1.0f, ga_shade (c, 260));
		}
		for (int i = 0; i < 3; i++)
		{
			float y = 5.0f + (float) i * 10, s = (i + v) & 1 ? 1.0f : -1.0f, xx = x + sinf (y * 0.2f + (float) v) * 2.5f;
			sp_ell (xx + s * 4.5f, y, 4.2f, 2.6f, ink);
			sp_ell (xx + s * 4.5f, y, 3.0f, 1.6f, ga_shade (c, 300));
		}
	}
	else if (w == 1)
	{
		uint32_t cut = ga_shade (W->back, 60), lit = ga_shade (W->back, 160);
		float m = TP / 2;
		for (int k = 0; k < 2; k++)
		{
			uint32_t col = k ? cut : lit;
			float o = k ? 0 : 0.8f;
			if (v == 0) { sp_ell (m + o, m + o, 9, 5, col); sp_ell (m, m, 7.5f, 3.6f, W->back); sp_disc (m + o, m + o, 2.4f, col); sp_cap (m - 4 + o, m + 5 + o, m - 7 + o, m + 10 + o, 1, col); }
			else if (v == 1) { sp_ell (m + o, 8 + o, 4, 5, col); sp_ell (m, 8, 2.4f, 3.4f, W->back); sp_cap (m + o, 12 + o, m + o, 27 + o, 1.2f, col); sp_cap (m - 7 + o, 15 + o, m + 7 + o, 15 + o, 1.2f, col); }
			else if (v == 2) { sp_cap (8 + o, 8 + o, 24 + o, 8 + o, 1.2f, col); sp_cap (8 + o, 8 + o, 8 + o, 24 + o, 1.2f, col); sp_cap (14 + o, 14 + o, 24 + o, 24 + o, 1.2f, col); }
			else { sp_cap (m + o, 6 + o, m + o, 26 + o, 1.2f, col); sp_cap (m + o, 8 + o, m + 7 + o, 15 + o, 1.0f, col); sp_cap (m + o, 8 + o, m - 7 + o, 15 + o, 1.0f, col); }
		}
	}
	else if (w == 2)
	{
		for (int i = 0; i < 4; i++)
		{
			float x = 4.0f + (float) i * 8, h = 10.0f + (float) (hashu (v, i, 3) % 14);
			sp_tri (x - 3.6f, -1, x + 3.6f, -1, x, h + 1.5f, ink);
			sp_tri_g (x - 2.4f, -1, x + 2.4f, -1, x, h, 0x00F0FAFF, ga_shade (c, 200));
		}
	}
	else
	{
		sp_box (-1, 9, TP + 1, 23, 0, ink);
		sp_box_g (-1, 10.4f, TP + 1, 21.6f, 0, ga_shade (W->metal, 330), ga_shade (W->metal, 170));
		sp_box (13, 6, 19, 26, 1, ink);
		sp_box_g (14.2f, 7.2f, 17.8f, 24.8f, 0.6f, ga_shade (W->metal, 360), ga_shade (W->metal, 200));
		if (v & 1) sp_disc (16, 16, 1.4f, 0x00FF4020);
	}
}

static void gen_jet (int w, const struct world *W)
{
	uint32_t ink = ink_of (w);
	sp_box (0, 0, TP, TP, 2, ink);
	sp_box_g (1.4f, 1.4f, TP - 1.4f, TP - 1.4f, 1.5f, ga_shade (W->metal, 300), ga_shade (W->metal, 180));
	for (int i = 0; i < 4; i++) sp_tri (2.0f + (float) i * 8, TP - 2, 6.0f + (float) i * 8, TP - 2, 6.0f + (float) i * 8, TP - 7, 0x00E8C030);
	sp_box (9, -1, 23, 7, 1.5f, ink);
	sp_box (11, 0, 21, 5, 1, 0x00201410);
	sp_disc (5, 10, 1.3f, ga_shade (W->metal, 420));
	sp_disc (TP - 5, 10, 1.3f, ga_shade (W->metal, 420));
}

/* une tuile : la faire si elle n'existe pas */
static gf_sprite_t *tile_get (int w, int kind, int sub)
{
	if (sub < 0 || sub >= TC_N) sub = 0;
	gf_sprite_t *t = g_tc[w][kind][sub];
	if (t) return t;
	float k = sp_k;
	sp_k = 1;
	t = sp_new (TP, TP);
	const struct world *W = &worlds[w];
	gf_sprite_t *const *E = g_til[w];
	if (E[0])
		switch (kind)
		{					/* le tileset de l'utilisateur */
		case TK_BACK:
			til_copy (E[sub % 3 == 1 ? ET_BACKB : ET_BACKA], sub % 3 == 2, 0);
			back_shade (sub / 3);
			goto done;
		case TK_ROCK: case TK_TRAPR:
		{
			int mask = sub & 15;
			til_copy (E[(mask & M_U) && kind == TK_ROCK ? ET_GRASS : ET_COBBLE], (sub >> 4) & 1, 0);
			if (kind == TK_TRAPR)
				for (int i = 0; i < 4; i++) sp_box (3.0f + (float) i * 7.5f, 3.2f, 5.6f + (float) i * 7.5f, 5.2f, 0.6f, 0x00100808);
			tile_edges (mask, ink_of (w), kind == TK_ROCK ? sub >> 5 : 0);
			goto done;
		}
		case TK_PLAT: til_copy (E[ET_PLANK], 0, 0); goto done;
		case TK_LADDER: til_copy (E[ET_LADDER], 0, 0); goto done;
		case TK_SPIKE: til_copy (E[ET_SPIKES], 0, 0); goto done;
		case TK_SPIKEC: til_copy (E[ET_SPIKES], 2, 0); goto done;
		case TK_LIQ: til_copy (E[ET_WMID], 0, 0); goto done;
		case TK_LIQS: til_copy (E[ET_WSURF], 0, (sub & 3) * 8); goto done;
		}
	switch (kind)
	{
	case TK_BACK: gen_back (W, sub % 3, sub / 3); break;
	case TK_ROCK: gen_rock (w, W, sub & 15, (sub >> 4) & 1, sub >> 5, 0); break;
	case TK_TRAPR: gen_rock (w, W, sub & 15, 0, 0, 1); break;
	case TK_BRICK: if (sub >= 80) gen_brick (w, W, 0, 0, sub - 80, 0); else gen_brick (w, W, sub & 15, sub >> 4, 0, 0); break;
	case TK_TRAPB: gen_brick (w, W, sub & 15, 0, 0, 4); break;
	case TK_BREAK: gen_brick (w, W, sub & 15, 0, 0, 1); break;
	case TK_DARTL: gen_brick (w, W, sub & 15, 0, 0, 2); break;
	case TK_DARTR: gen_brick (w, W, sub & 15, 0, 0, 3); break;
	case TK_PLAT: gen_plat (w, W, sub & 3); break;
	case TK_LADDER: gen_ladder (w, W); break;
	case TK_SPIKE: gen_spikes (w, W, 0); break;
	case TK_SPIKEC: gen_spikes (w, W, 1); break;
	case TK_LIQ: gen_liquid (W, 0, 0); break;
	case TK_LIQS: gen_liquid (W, 1, sub & 3); break;
	case TK_CRUMB: gen_crumb (w, W); break;
	case TK_DOOR: gen_door (w, W, sub & 1); break;
	case TK_TORCH: gen_torch (w, W); break;
	case TK_DECO: gen_deco (w, W, sub & 3); break;
	case TK_JET: gen_jet (w, W); break;
	}
done:
	sp_k = k;
	g_tc[w][kind][sub] = t;
	return t;
}

/* ---- la fougere du premier plan : 3 x 2 tuiles, devant tout -------------------------------------------- */
static gf_sprite_t *gen_fern (int w)
{
	float k = sp_k;
	sp_k = 1;
	gf_sprite_t *s = sp_new (112, 72);
	uint32_t ink = ink_of (w);
	static const uint32_t col[NWORLD][2] = { { 0x0050B040, 0x002E7A2C }, { 0x0098A048, 0x00607030 }, { 0x00D8ECF8, 0x007C9CC0 }, { 0x00503028, 0x00301818 } };
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < 9; i++)
		{
			float a = 3.1416f * (0.08f + 0.105f * (float) i), len = 46 + (float) ((i * 7) % 4) * 5;
			float bx = 56, by = 70, tx = bx - cosf (a) * len, ty = by - sinf (a) * len * 0.95f;
			float mx = (bx + tx) / 2 - sinf (a) * 6, my = (by + ty) / 2 - 8;
			for (int j = 0; j < 10; j++)
			{
				float t0 = (float) j / 10, t1 = (float) (j + 1) / 10;
				float x0 = (1 - t0) * (1 - t0) * bx + 2 * (1 - t0) * t0 * mx + t0 * t0 * tx, y0 = (1 - t0) * (1 - t0) * by + 2 * (1 - t0) * t0 * my + t0 * t0 * ty;
				float x1 = (1 - t1) * (1 - t1) * bx + 2 * (1 - t1) * t1 * mx + t1 * t1 * tx, y1 = (1 - t1) * (1 - t1) * by + 2 * (1 - t1) * t1 * my + t1 * t1 * ty;
				float r = 6.5f * sinf (3.1416f * (0.15f + 0.85f * t0)) + 1.0f;
				if (pass == 0) sp_cap (x0, y0, x1, y1, r + 1.6f, ink);
				else
				{
					sp_cap (x0, y0, x1, y1, r, ga_mix (col[w][0], col[w][1], (uint32_t) (t0 * 140)));
					sp_cap (x0, y0, x1, y1, 0.6f, col[w][1]);
				}
			}
			if (pass == 1 && w == 3) sp_disc (tx, ty, 2.0f, 0x00FF8030);
		}
	sp_k = k;
	return s;
}
