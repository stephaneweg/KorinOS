/* uk_extra.c -- UIKit : mise en page, canevas, dessin vectoriel anticrénelé,
 * indications de manette, navigation entre objets (uikit_extra.inc). Port
 * ligne à ligne de l'assembleur (src/kits/uikit/PORT.md).
 *
 *	ui_layout	piles et grille : place les widgets d'un panneau
 *	UT_CANVAS	canevas : un tampon de pixels à soi (ui_canvas_begin)
 *	ui_vp_*		formes anticrénelées : disque, trait épais, polygone
 *			(coordonnées en 1/16 de pixel)
 *	ui_pad_hints	indications de manette (« (A) choisir »)
 *	ui_objnav	l'objet voisin dans une direction
 *
 * ui_vp_disc et ui_vp_line calculaient en double (scvtf, fmul, fmadd, fdiv,
 * fmsub, fsqrt, fcvtzs) : ici sans flottant, par une arithmétique double
 * logicielle exacte (arrondi au plus proche pair à chaque opération, fmadd /
 * fmsub fusionnées, sqrt correctement arrondie) : mêmes bits que le FPU. */
#include "uk.h"

typedef unsigned __int128 u128;

/* ---------------------------------------------------------------------------
 *  Double logiciel : valeur = (-1)^s * m * 2^e, m dans [2^52, 2^53) (0 : zéro).
 *  Pas de dénormaux ni d'infinis : les valeurs viennent d'entiers de 64 bits
 *  (comme scvtf) et restent loin des bornes de l'exposant ; le signe d'un zéro
 *  n'est pas suivi (il ne change aucun résultat de ces formules).
 * ------------------------------------------------------------------------- */

typedef struct {
	u64 m;
	long e;
	int s;
} sdbl;

/* nombre de bits significatifs */
static int sd_bitlen(u128 x)
{
	u64 hi = (u64)(x >> 64), lo = (u64)x;
	if (hi)
		return 128 - __builtin_clzll(hi);
	if (lo)
		return 64 - __builtin_clzll(lo);
	return 0;
}

/* (-1)^s * M * 2^e arrondi à 53 bits, au plus proche pair. Un reste perdu
 * plus bas que M est « collé » au bit 0 de M par l'appelant (M a alors au
 * moins 55 bits : le collage ne crée ni ne défait de cas d'égalité). */
static sdbl sd_round(int s, u128 M, long e)
{
	sdbl r;
	int n = sd_bitlen(M);
	r.s = s;
	if (!n) {
		r.m = 0;
		r.e = 0;
		r.s = 0;
		return r;
	}
	if (n > 53) {
		int sh = n - 53;
		u128 keep = M >> sh;
		u128 rem = M & (((u128)1 << sh) - 1);
		u128 half = (u128)1 << (sh - 1);
		if (rem > half || (rem == half && (keep & 1)))
			keep++;
		if (keep >> 53) {
			keep >>= 1;
			sh++;
		}
		r.m = (u64)keep;
		r.e = e + sh;
	} else {
		r.m = (u64)M << (53 - n);
		r.e = e - (53 - n);
	}
	return r;
}

/* scvtf : entier signé de 64 bits */
static sdbl sd_from(long x)
{
	int s = x < 0;
	u64 u = s ? 0 - (u64)x : (u64)x;
	return sd_round(s, u, 0);
}

/* fmul */
static sdbl sd_mul(sdbl a, sdbl b)
{
	if (!a.m || !b.m)
		return sd_round(0, 0, 0);
	return sd_round(a.s ^ b.s, (u128)a.m * b.m, a.e + b.e);
}

/* somme exacte de deux valeurs (M1 < 2^107, M2 < 2^107), arrondie une fois */
static sdbl sd_addx(int s1, u128 M1, long e1, int s2, u128 M2, long e2)
{
	if (!M1)
		return sd_round(s2, M2, e2);
	if (!M2)
		return sd_round(s1, M1, e1);
	int n = sd_bitlen(M1);              /* bit fort au rang 125 (bits bas à 0) */
	M1 <<= 126 - n;
	e1 -= 126 - n;
	n = sd_bitlen(M2);
	M2 <<= 126 - n;
	e2 -= 126 - n;
	if (e1 < e2) {
		u128 tm = M1;
		long te = e1;
		int ts = s1;
		M1 = M2;
		e1 = e2;
		s1 = s2;
		M2 = tm;
		e2 = te;
		s2 = ts;
	}
	long diff = e1 - e2;
	if (diff >= 126) {
		M2 = 1;                         /* tout sous le dernier bit : collé */
	} else if (diff) {
		u128 lost = M2 & (((u128)1 << diff) - 1);
		M2 >>= diff;
		if (lost)
			M2 |= 1;
	}
	if (s1 == s2)
		return sd_round(s1, M1 + M2, e1);
	if (M1 >= M2)
		return sd_round(s1, M1 - M2, e1);
	return sd_round(s2, M2 - M1, e1);
}

/* fmadd : a * b + c (une seule arrondi) */
static sdbl sd_fma(sdbl a, sdbl b, sdbl c)
{
	u128 p = (a.m && b.m) ? (u128)a.m * b.m : 0;
	return sd_addx(a.s ^ b.s, p, a.e + b.e, c.s, c.m, c.e);
}

/* fmsub : c - a * b (une seule arrondi) */
static sdbl sd_fms(sdbl a, sdbl b, sdbl c)
{
	u128 p = (a.m && b.m) ? (u128)a.m * b.m : 0;
	return sd_addx(!(a.s ^ b.s), p, a.e + b.e, c.s, c.m, c.e);
}

/* fsub : a - b */
static sdbl sd_sub(sdbl a, sdbl b)
{
	return sd_addx(a.s, a.m, a.e, !b.s, b.m, b.e);
}

/* fdiv (b non nul) : 67 bits de quotient par tranches de 11 bits, reste collé */
static sdbl sd_div(sdbl a, sdbl b)
{
	if (!a.m)
		return sd_round(0, 0, 0);
	u64 q = UDIV(a.m, b.m);
	u64 r = a.m - q * b.m;
	u128 Q = q;
	for (int i = 0; i < 6; i++) {
		r <<= 11;                       /* r < 2^53 : tient en 64 bits */
		u64 d = UDIV(r, b.m);
		r -= d * b.m;
		Q = (Q << 11) | d;
	}
	if (r)
		Q |= 1;
	return sd_round(a.s ^ b.s, Q, a.e - b.e - 66);
}

/* fsqrt (a >= 0) : racine entière de m * 2^k (exposant pair), reste collé */
static sdbl sd_sqrt(sdbl a)
{
	if (!a.m)
		return sd_round(0, 0, 0);
	int k = 64;
	if ((a.e - 64) & 1)
		k = 65;
	u128 op = (u128)a.m << k;           /* dans [2^116, 2^118) */
	long E = a.e - k;
	u128 res = 0, one = (u128)1 << 126;
	while (one > op)
		one >>= 2;
	while (one) {
		if (op >= res + one) {
			op -= res + one;
			res = (res >> 1) + one;
		} else {
			res >>= 1;
		}
		one >>= 2;
	}
	if (op)
		res |= 1;
	return sd_round(0, res, ASR(E, 1));
}

/* fcvtzs : vers zéro, saturé */
static long sd_trunc(sdbl a)
{
	if (!a.m)
		return 0;
	u64 v;
	if (a.e >= 0) {
		if (a.e > 10)
			return a.s ? (long)((u64)1 << 63) : (long)(((u64)1 << 63) - 1);
		v = a.m << a.e;
	} else {
		if (-a.e >= 64)
			v = 0;
		else
			v = a.m >> -a.e;
	}
	return a.s ? (long)(0 - v) : (long)v;
}

/* fmax(t, +0) puis fmin(t, 1.0) */
static sdbl sd_clamp01(sdbl t)
{
	if (t.m && t.s)
		return sd_round(0, 0, 0);
	if (t.m && (t.e > -52 || (t.e == -52 && t.m > ((u64)1 << 52))))
		return sd_round(0, 1, 0);
	return t;
}

/* ---------------------------------------------------------------------------
 *  Mise en page
 * ------------------------------------------------------------------------- */

/* x0 = panneau, x1 = genre (ULY_VSTACK, ULY_HSTACK, ULY_GRID), x2 = écart (et
 * marge), x3 = colonnes : place les widgets du panneau (ordre de création) ;
 * ne redessine pas */
long ui_layout(long a0, long a1, long a2, long a3)
{
	long p = a0;
	if (!p)
		return 0;
	long kind = a1, gap = a2;
	long cols = a3 < 1 ? 1 : a3;        /* colonnes */
	long r = 1;                         /* x0 rendu : sa dernière valeur, comme l'asm */
	long cx = SXTW(LDW(p, UW_X)) + gap; /* curseur : x, y */
	long cy = SXTW(LDW(p, UW_Y)) + gap;
	long col = 0;                       /* colonne courante (grille) */
	long rowh = 0;                      /* hauteur de la rangée (grille) */
	for (long w = uk.wfirst; w; w = LDX(w, UW_NEXT)) {
		r = LDX(w, UW_PARENT);
		if (r != p)
			continue;
		long aw = LDW(p, UW_W) - gap * 2;   /* place utile du panneau */
		if (kind == ULY_HSTACK) {           /* ligne : pleine hauteur */
			long ww = LDW(w, UW_W);
			long hh = LDW(p, UW_H) - gap * 2;
			long x = cx;
			cx = cx + ww + gap;
			r = ui_w_move(w, x, cy, ww, hh);
		} else if (kind == ULY_GRID) {      /* grille : largeur d'une case */
			long cw = UDIV(aw - (cols - 1) * gap, cols);
			long x = col * (cw + gap) + SXTW(LDW(p, UW_X)) + gap;   /* x de la case */
			long hh = LDW(w, UW_H);
			if (UGT(hh, rowh))
				rowh = hh;
			r = ui_w_move(w, x, cy, cw, hh);
			col++;
			if (!ULT(col, cols)) {
				col = 0;                    /* rangée suivante */
				cy = cy + rowh + gap;
				rowh = 0;
			}
		} else {                            /* colonne : pleine largeur */
			long hh = LDW(w, UW_H);
			long y = cy;
			cy = cy + hh + gap;
			r = ui_w_move(w, cx, y, LDW(p, UW_W) - gap * 2, hh);
		}
	}
	return r;
}

/* ---------------------------------------------------------------------------
 *  Canevas
 * ------------------------------------------------------------------------- */

/* x0..x3 = rectangle -> canevas (blanc ; pixels : ui_canvas_pixels ; la
 * souris y rend UN_CHANGE, le point dans ui_ev) */
long ui_canvas(long a0, long a1, long a2, long a3)
{
	long w = uk_w_new(UT_CANVAS, a0, a1, a2, a3);
	if (!w)
		return 0;
	long img = uk_img_new(LDW(w, UW_W), LDW(w, UW_H));   /* (une image) */
	STX(w, UW_AUX, img);
	if (!img)
		return w;
	long n = LDX(img, IMG_W) * LDX(img, IMG_H);
	long p = img + IMG_PIX;
	while (n) {
		STW(p, 0, 0x00FFFFFF);
		p += 4;
		n--;
	}
	return w;
}

/* x0 = canevas -> x0 = pixels (0x00RRGGBB), x1 = largeur, x2 = hauteur */
ukr ui_canvas_pixels(long a0)
{
	ukr r;
	long img = LDX(a0, UW_AUX);
	r.r3 = img;
	if (!img) {
		r.r0 = 0;
		r.r1 = 0;
		r.r2 = 0;
		return r;
	}
	r.r1 = LDX(img, IMG_W);
	r.r2 = LDX(img, IMG_H);
	r.r0 = img + IMG_PIX;
	return r;
}

/* x0 = canevas : les dessins de UIKit vont dans ses pixels jusqu'à ui_canvas_end */
long ui_canvas_begin(long a0)
{
	long img = LDX(a0, UW_AUX);
	if (!img)
		return a0;
	return uk_target(img + IMG_PIX, LDX(img, IMG_W), LDX(img, IMG_H));
}

/* fin des dessins dans un canevas (le montrer : ui_w_draw) */
long ui_canvas_end(void)
{
	return uk_target_back();
}

/* uk_blit_row : x1 = pixels source, x2 = y, x3 = x, x4 = largeur : une ligne
 * copiée dans la surface (découpe : surface, ui_clip) */
static void blit_row(long src, long y, long x, long width)
{
	long v = uk.vars;
	long right = LDX(v, UV_W), bottom = LDX(v, UV_H);  /* bornes droite, bas */
	long left = 0, top = 0;                            /* bornes gauche, haut */
	if (uk.clip_on) {
		left = uk.clip[0];
		top = uk.clip[1];
		if (uk.clip[2] < right)
			right = uk.clip[2];
		if (uk.clip[3] < bottom)
			bottom = uk.clip[3];
	}
	if (y < top || y >= bottom)
		return;
	long row = LDX(v, UV_SURF) + ((y * LDX(v, UV_W)) << 2);   /* ligne de destination */
	for (long i = 0; ULT(i, width); i++) {
		long xx = x + i;
		if (xx < left)
			continue;
		if (xx >= right)
			return;
		STW(row, xx << 2, LDW(src, i << 2) & 0x00FFFFFF);
	}
}

/* uk_blit : x0 = image, x1, x2 = place : copie opaque (taille réelle, découpée)
 * -> x0 = IMG_H (dernière valeur lue, comme l'asm) */
static long blit(long img, long x, long y)
{
	long row = 0;
	for (;;) {
		long h = LDX(img, IMG_H);
		if (!ULT(row, h))
			return h;
		long w = LDX(img, IMG_W);           /* une ligne, copie directe bornée */
		blit_row(img + IMG_PIX + ((row * w) << 2), y + row, x, w);
		row++;
	}
}

long uk_cv_draw(long w)
{
	long img = LDX(w, UW_AUX);
	if (!img)
		return 0;
	return blit(img, SXTW(LDW(w, UW_X)), SXTW(LDW(w, UW_Y)));
}

/* souris d'un canevas : UN_CHANGE (le point et le genre dans ui_ev) */
long uk_cv_mouse(long w, long kind, long x, long y, long clicks)
{
	(void)w;
	(void)x;
	(void)y;
	(void)clicks;
	if (kind == 3)
		return 0;
	return UN_CHANGE;
}

/* ---------------------------------------------------------------------------
 *  Formes anticrénelées (coordonnées en 1/16 de pixel)
 * ------------------------------------------------------------------------- */

/* uk_blend_px : x0 = x, x1 = y (pixel), w2 = couleur, x3 = couverture 0..256 :
 * mélange un pixel (découpe comme ui_pixel) */
static void blend_px(long x, long y, long color, long cov)
{
	if (!cov)
		return;
	long v = uk.vars;
	long W = LDX(v, UV_W), H = LDX(v, UV_H);
	if (UGE(x, W) || UGE(y, H))
		return;
	if (uk.clip_on) {
		if (x < uk.clip[0] || y < uk.clip[1])
			return;
		if (x >= uk.clip[2] || y >= uk.clip[3])
			return;
	}
	long p = LDX(v, UV_SURF) + ((y * W + x) << 2);
	u32 d = (u32)LDW(p, 0);
	u32 c = (u32)color, a = (u32)cov, na = 256 - a;
	u32 out = 0;
	for (u32 sh = 0; sh < 24; sh += 8) {
		u32 s = (c >> sh) & 0xFF;
		u32 b = (d >> sh) & 0xFF;
		u32 m = (s * a + b * na) >> 8;
		out |= m << sh;
	}
	STW(p, 0, out);
}

/* (r - d), tronqué vers zéro, puis couverture (t + 8) * 16 */
static long cover(sdbl r, sdbl d)
{
	long t = sd_trunc(sd_sub(r, d));
	return LSL(t + 8, 4);
}

/* x0 = cx, x1 = cy, x2 = rayon (1/16 px), w3 = couleur : disque anticrénelé */
long ui_vp_disc(long a0, long a1, long a2, long a3)
{
	long cx = a0, cy = a1, r = a2;
	u32 color = (u32)a3;
	sdbl rd = sd_from(r);
	long yb;
	for (long py = ASR(cy - r, 4) - 1;; py++) {    /* lignes */
		yb = ASR(cy + r, 4) + 1;
		if (py > yb)
			break;
		for (long px = ASR(cx - r, 4) - 1; px <= ASR(cx + r, 4) + 1; px++) {
			/* distance du centre du pixel (1/16) */
			sdbl dx = sd_from(LSL(px, 4) + 8 - cx);
			sdbl dy = sd_from(LSL(py, 4) + 8 - cy);
			sdbl d = sd_sqrt(sd_fma(dy, dy, sd_mul(dx, dx)));
			long cov = cover(rd, d);           /* (r + 8 - d) * 16, bornée */
			if (cov <= 0)
				continue;
			if (cov > 256)
				cov = 256;
			blend_px(px, py, color, cov);
		}
	}
	return yb;                                  /* x0 de l'asm à la sortie */
}

/* x0, x1 = début, x2, x3 = fin, x4 = épaisseur (1/16 px), w5 = couleur :
 * trait anticrénelé à bouts ronds */
long ui_vp_line(long a0, long a1, long a2, long a3, long a4, long a5)
{
	long x0 = a0, y0 = a1, x1 = a2, y1 = a3;
	long half = LSR(a4, 1);                     /* demi-épaisseur (lsr) */
	u32 color = (u32)a5;
	long minx = x0 < x1 ? x0 : x1, maxx = x0 < x1 ? x1 : x0;
	long miny = y0 < y1 ? y0 : y1, maxy = y0 < y1 ? y1 : y0;
	sdbl dx = sd_from(x1 - x0);                 /* d = fin - début, |d|^2 */
	sdbl dy = sd_from(y1 - y0);
	sdbl len2 = sd_fma(dy, dy, sd_mul(dx, dx));
	sdbl hd = sd_from(half);
	long yb;
	for (long py = ASR(miny - half, 4) - 1;; py++) {
		yb = ASR(maxy + half, 4) + 1;
		if (py > yb)
			break;
		for (long px = ASR(minx - half, 4) - 1; px <= ASR(maxx + half, 4) + 1; px++) {
			sdbl p0 = sd_from(LSL(px, 4) + 8 - x0);    /* p - début */
			sdbl p1 = sd_from(LSL(py, 4) + 8 - y0);
			/* t = (p.d) / |d|^2, borné à [0, 1] */
			sdbl t = sd_fma(p1, dy, sd_mul(p0, dx));
			if (!len2.m)
				t = sd_round(0, 0, 0);
			else
				t = sd_clamp01(sd_div(t, len2));
			p0 = sd_fms(t, dx, p0);                    /* p - t d */
			p1 = sd_fms(t, dy, p1);
			sdbl d = sd_sqrt(sd_fma(p1, p1, sd_mul(p0, p0)));
			long cov = cover(hd, d);
			if (cov <= 0)
				continue;
			if (cov > 256)
				cov = 256;
			blend_px(px, py, color, cov);
		}
	}
	return yb;                                  /* x0 de l'asm à la sortie */
}

/* uk_poly_in : x0, x1 = point (1/16) -> 1 dedans (règle pair-impair) */
static long poly_in(long x, long y, long pts, long n)
{
	long in = 0;
	long j = n - 1;                             /* j = dernier */
	for (long i = 0; ULT(i, n); i++) {
		long xi = LDSW(pts, i << 3), yi = LDSW(pts, (i << 3) + 4);
		long xj = LDSW(pts, j << 3), yj = LDSW(pts, (j << 3) + 4);
		if ((yi > y) != (yj > y)) {             /* (yi > y) != (yj > y) */
			long v = SDIV((xj - xi) * (y - yi), yj - yi) + xi;
			if (x < v)
				in ^= 1;
		}
		j = i;
	}
	return in;
}

/* x0 = points (x, y en 1/16 de pixel, 32 bits chacun), x1 = nombre, w2 =
 * couleur : polygone plein anticrénelé (pair-impair, 4 x 4 échantillons) */
long ui_vp_poly(long a0, long a1, long a2)
{
	long pts = a0, n = a1;
	u32 color = (u32)a2;
	if (ULT(n, 3))
		return a0;
	long minx = LDSW(pts, 0), miny = LDSW(pts, 4);   /* boîte */
	long maxx = minx, maxy = miny;
	long i;
	for (i = 1; ULT(i, n); i++) {
		long x = LDSW(pts, i << 3), y = LDSW(pts, (i << 3) + 4);
		if (x < minx)
			minx = x;
		if (x > maxx)
			maxx = x;
		if (y < miny)
			miny = y;
		if (y > maxy)
			maxy = y;
	}
	long r = i;                                 /* x0 rendu, comme l'asm */
	minx = ASR(minx, 4);
	miny = ASR(miny, 4);
	maxx = ASR(maxx, 4);
	maxy = ASR(maxy, 4);
	for (long py = miny; py <= maxy; py++) {
		for (long px = minx; px <= maxx; px++) {
			long in = 0;                        /* échantillons dedans */
			for (long sy = 0; sy < 4; sy++)     /* (2, 6, 10, 14) / 16 */
				for (long sx = 0; sx < 4; sx++) {
					r = poly_in(LSL(px, 4) + sx * 4 + 2, LSL(py, 4) + sy * 4 + 2, pts, n);
					in += r;
				}
			if (!in)
				continue;
			blend_px(px, py, color, LSL(in, 4));   /* 16 échantillons -> 0..256 */
			r = px;
		}
	}
	return r;
}

/* ---------------------------------------------------------------------------
 *  Indications de manette
 * ------------------------------------------------------------------------- */

/* uk_bold_w : x0 = texte, x1 = taille -> largeur en gras */
static long bold_w(long text, long size)
{
	long f = uk_bold();
	return ui_text_width(text, size, f);
}

/* x0 = indications (« A<tab>choisir<lf>B<tab>retour... »), x1 = x, x2 = y,
 * x3 = hauteur, x4 = 1 mesurer seulement -> largeur. Bouton : pastille ronde
 * (A, B, X, Y), arrondie (L, R, Start...) ou croix (« + ») ; texte TEXTE2. */
long ui_pad_hints(long a0, long a1, long a2, long a3, long a4)
{
	u8 *s = (u8 *)(uintptr_t)a0;
	long x = a1, y = a2, h = a3, measure = a4;
	long start = a1;                            /* départ */
	u8 *buf = uk.fitbuf;
	while (*s) {
		long n = 0;                             /* le bouton */
		for (;;) {
			/* comme l'asm : l'octet est passé avant le test, un NUL compris
			 * (texte fini dans un bouton : le rôle est lu après le NUL) */
			u8 c = *s++;
			if (!c || c == 9)
				break;
			buf[n++] = c;
			if (!ULT(n, 7))
				break;
		}
		buf[n] = 0;
		long pw = h - 6;                        /* pastille : largeur = hauteur, ou texte */
		if (UGT(n, 1)) {
			long tw = bold_w(UKA(fitbuf), 11) + 10;
			if (ULT(tw, h - 6))
				tw = h - 6;
			pw = tw;
		}
		if (!measure) {
			if (buf[0] == '+' && !buf[1]) {     /* « + » : croix directionnelle */
				ui_rbox(x + (LSR(h, 1) - 4), y + 3, 8, h - 6, 2, JET(TJ_TEXT2), JET(TJ_TEXT2));
				ui_rbox(x + 3, y + (LSR(h, 1) - 4), h - 6, 8, 2, JET(TJ_TEXT2), JET(TJ_TEXT2));
			} else {                            /* pastille TEXTE2, lettre FACE */
				ui_rbox(x, y + 3, pw, h - 6, LSR(h - 6, 1), JET(TJ_TEXT2), JET(TJ_TEXT2));
				long tw = bold_w(UKA(fitbuf), 11);
				long tx = x + LSR(pw - tw, 1);
				long f = uk_bold();
				uk_text_box(UKA(fitbuf), tx, y, h, JET(TJ_FACE), f, 0, 11);
			}
		}
		x = x + pw + 5;                         /* le rôle */
		n = 0;
		for (;;) {
			u8 c = *s;
			if (!c)
				break;
			s++;
			if (c == 10)
				break;
			buf[n++] = c;
			if (!ULT(n, 200))
				break;
		}
		buf[n] = 0;
		long small = LDX(uk_met(), MT_SMALL);
		long f = uk_font_r();
		long tw = ui_text_width(UKA(fitbuf), small, f);
		if (!measure) {
			f = uk_font_r();
			uk_text_box(UKA(fitbuf), x, y, h, JET(TJ_TEXT2), f, 0, small);
		}
		x = x + tw + 14;                        /* entre deux indications */
	}
	long v = x - start;
	if (v < 14)                                 /* subs + lt : comparaison signée */
		return 0;
	return v - 14;
}

/* ---------------------------------------------------------------------------
 *  Navigation entre objets (croix directionnelle, sans pointeur)
 * ------------------------------------------------------------------------- */

/* x0 = rectangles (x, y, l, h : 32 bits chacun), x1 = nombre, x2 = objet
 * courant, x3 = direction (0 gauche, 1 droite, 2 haut, 3 bas) -> l'objet le
 * plus proche dans cette direction (-1 : aucun) */
long ui_objnav(long a0, long a1, long a2, long a3)
{
	long rects = a0, n = a1, cur = a2, dir = a3;
	if (!ULT(cur, n))
		return -1;
	/* centre du courant ; comme l'asm : ldp w (zéro-étendus), moitiés par lsr */
	long r0 = rects + (cur << 4);
	long cx = LDW(r0, 0) + LSR(LDW(r0, 8), 1);
	long cy = LDW(r0, 4) + LSR(LDW(r0, 12), 1);
	long best = -1;                             /* meilleur coût (non signé) */
	long found = -1;
	for (long i = 0; ULT(i, n); i++) {
		if (i == cur)
			continue;
		long ri = rects + (i << 4);
		long dx = LDW(ri, 0) + LSR(LDW(ri, 8), 1) - cx;   /* son centre : dx, dy */
		long dy = LDW(ri, 4) + LSR(LDW(ri, 12), 1) - cy;
		long ahead, side;
		if (!UGT(dir, 1)) {                     /* gauche : dx < 0 ; droite : dx > 0 */
			ahead = dir ? dx : -dx;
			if (ahead <= 0)
				continue;
			side = dy < 0 ? -dy : dy;
		} else {                                /* haut : dy < 0 ; bas (et au-delà) : dy > 0 */
			ahead = dir == 2 ? -dy : dy;
			if (ahead <= 0)
				continue;
			side = dx < 0 ? -dx : dx;
		}
		long cost = ahead + side * 2;           /* l'écart de côté compte double */
		if (UGE(cost, best))
			continue;
		best = cost;
		found = i;
	}
	return found;
}

/* x0 = x, x1 = y (dans la fenêtre), x2 = libellés, x3 = largeur -> comme
 * ui_popup (menu contextuel au point d'un clic) */
ukr ui_popup_local(long a0, long a1, long a2, long a3)
{
	ukr p = uk_win_pos();
	return ui_popup(p.r0 + a0, p.r1 + a1, a2, a3);
}
