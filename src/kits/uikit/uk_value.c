/* uk_value.c -- UIKit : les widgets d'une valeur bornée. Port ligne à ligne
 * de uikit_value.inc (src/kits/uikit/PORT.md).
 *
 *	UT_SLIDER	glissière (DESIGN 11.6 : rail 4 px, partie remplie ACCENT,
 *			bouton disque 18 blanc ; verticale si plus haute que large)
 *	UT_PROGRESS	barre de progression (rail CREUX, remplie ACCENT)
 *	UT_NUMERIC	nombre +/- (Onyx numeric.h) : champ et deux boutons
 *	UT_KNOB		molette (Onyx knob.cpp) : disque, repère sur 270 degrés
 *	UT_VUMETER	vu-mètre (Onyx vumeter.cpp) : segments SUCCES, ALERTE,
 *			DANGER, crête gardée
 *	UT_LCD		afficheur à sept segments (Onyx lcd.cpp) : chiffres, - : .
 *
 * Plage : ui_w_set_range(widget, min, max, 0, pas) ; valeur : ui_w_set_value
 * / ui_w_get_value. Glissière, nombre, molette : souris, molette, flèches,
 * Page (10 pas), Début, Fin -> UN_CHANGE. */
#include "uk.h"

/* uk_new_ranged : x0..x3 = rectangle, x4 = min, x5 = max, x6 = valeur,
 * x7 = type -> widget */
static long new_ranged(long x, long y, long w, long h, long min, long max, long val, long type)
{
	long ht = LDX(uk_met(), MT_CTL); /* hauteur 0 : celle d'un contrôle */
	if (h)
		ht = h;
	long r = uk_w_new(type, x, y, w, ht);
	if (!r)
		return 0;
	STX(r, UW_MIN, min);
	STX(r, UW_MAX, max);
	STX(r, UW_VALUE, val);
	uk_w_clamp(r);
	return r;
}

/* x0..x3 = rectangle, x4 = min, x5 = max, x6 = valeur -> poignée */
long ui_slider(long a0, long a1, long a2, long a3, long a4, long a5, long a6)
{
	return new_ranged(a0, a1, a2, a3, a4, a5, a6, UT_SLIDER);
}

/* x0..x3, x4 = max, x5 = valeur (min 0) */
long ui_progress(long a0, long a1, long a2, long a3, long a4, long a5)
{
	return new_ranged(a0, a1, a2, a3, 0, a4, a5, UT_PROGRESS);
}

/* x0..x3, x4 = min, x5 = max, x6 = valeur */
long ui_numeric(long a0, long a1, long a2, long a3, long a4, long a5, long a6)
{
	return new_ranged(a0, a1, a2, a3, a4, a5, a6, UT_NUMERIC);
}

/* x0..x3, x4 = min, x5 = max, x6 = valeur */
long ui_knob(long a0, long a1, long a2, long a3, long a4, long a5, long a6)
{
	return new_ranged(a0, a1, a2, a3, a4, a5, a6, UT_KNOB);
}

/* x0..x3, x4 = max (valeur 0) */
long ui_vumeter(long a0, long a1, long a2, long a3, long a4)
{
	return new_ranged(a0, a1, a2, a3, 0, a4, 0, UT_VUMETER);
}

/* x0..x3, x4 = texte (chiffres, « - », « : », « . », espace) */
long ui_lcd(long a0, long a1, long a2, long a3, long a4)
{
	long w = uk_w_new(UT_LCD, a0, a1, a2, a3);
	if (!w)
		return 0;
	uk_set_text(w, a4, -1);
	return w;
}

/* x0 = widget -> x0 = 1 s'il est vertical (plus haut que large) */
long uk_vertical(long w)
{
	return (s32)LDW(w, UW_H) > (s32)LDW(w, UW_W);
}

/* uk_val_set : x0 = widget, x1 = nouvelle valeur : bornée, redessine -> x0 =
 * UN_CHANGE si elle a changé (0 sinon) */
static long val_set(long w, long v)
{
	long old = LDX(w, UW_VALUE);
	STX(w, UW_VALUE, v);
	uk_w_clamp(w);
	if (LDX(w, UW_VALUE) == old)
		return 0;
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* clavier commun : flèches (gauche, bas : -pas ; droite, haut : +pas), Page
 * (10 pas), Début, Fin */
long uk_val_key(long w, long code, long ch, long mods)
{
	long step = LDX(w, UW_STEP);
	long v = LDX(w, UW_VALUE);
	long page, nv;
	u32 k = (u32)code;
	(void)ch;
	(void)mods;
	if (k == KEY_RIGHT || k == KEY_UP)
		nv = v + step;
	else if (k == KEY_LEFT || k == KEY_DOWN)
		nv = v - step;
	else {
		page = step * 10;
		if (k == KEY_PGUP)
			nv = v + page;
		else if (k == KEY_PGDN)
			nv = v - page;
		else if (k == KEY_HOME)
			nv = LDX(w, UW_MIN);
		else if (k == KEY_END)
			nv = LDX(w, UW_MAX);
		else
			return -1;
	}
	return val_set(w, nv);
}

/* molette commune : un pas par cran */
long uk_val_wheel(long w, long steps)
{
	return val_set(w, LDX(w, UW_VALUE) + steps * LDX(w, UW_STEP));
}

/* ---------------------------------------------------------------------------
 *  Glissière
 * ------------------------------------------------------------------------- */

/* uk_sl_pos : x0 = glissière -> r0 = position du bouton le long du rail
 * (0..course), r1 = course (longueur - 18) */
static ukr sl_pos(long w)
{
	ukr r;
	long len = LDW(w, UW_W);
	if (uk_vertical(w))
		len = LDW(w, UW_H);
	len -= UK_BOX;
	if (len < 1)
		len = 1;
	long min = LDX(w, UW_MIN);
	long span = LDX(w, UW_MAX) - min;
	long off = LDX(w, UW_VALUE) - min;
	r.r0 = 0;
	if (span > 0)
		r.r0 = UDIV(off * len, span);
	r.r1 = len;
	r.r2 = r.r3 = 0;
	return r;
}

/* uk_sl_accent : -> w5 = ACCENT, ou sa version grisée si le widget x19 est
 * inactif */
static long sl_accent(long w)
{
	long c = JET(TJ_ACCENT);
	if (!uk_w_enabled(w))
		c = UXTW(ui_mix(JET(TJ_FACE), JET(TJ_ACCENT), 90));
	return c;
}

long uk_sl_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long vert, pos, course, kx, ky, c;

	ui_fill(x, y, wd, ht, JET(TJ_FACE));
	vert = uk_vertical(w);
	ukr p = sl_pos(w);
	pos = p.r0;    /* position */
	course = p.r1; /* course */
	if (!vert) {
		/* --- horizontale : rail au milieu */
		c = JET(TJ_LINE);
		ui_rbox(x + UK_BOX / 2, y + LSR(ht, 1) - 2, course, 4, 2, c, c);
		c = sl_accent(w);
		ui_rbox(x + UK_BOX / 2, y + LSR(ht, 1) - 2, pos, 4, 2, c, c);
		kx = x + pos; /* bouton */
		ky = y + LSR(ht, 1) - UK_BOX / 2;
	} else {
		/* --- verticale : le minimum en bas */
		c = JET(TJ_LINE);
		ui_rbox(x + LSR(wd, 1) - 2, y + UK_BOX / 2, 4, course, 2, c, c);
		c = sl_accent(w);
		ui_rbox(x + LSR(wd, 1) - 2, y + UK_BOX / 2 + course - pos, 4, pos, 2, c, c);
		kx = x + LSR(wd, 1) - UK_BOX / 2;
		ky = y + course - pos;
	}
	/* disque blanc, contour LIGNE_FORTE */
	c = JET(TJ_LINE_STRONG);
	if (LDW(w, UW_FLAGS) & 8)
		c = JET(TJ_ACCENT);
	ui_rbox(kx, ky, UK_BOX, UK_BOX, UK_BOX / 2, c, c);
	return ui_rbox(kx + 1, ky + 1, UK_BOX - 2, UK_BOX - 2, UK_BOX / 2 - 1, 0x00FFFFFF, 0x00FFFFFF);
}

/* souris : appui ou glisser -> la valeur sous le point */
long uk_sl_mouse(long w, long kind, long x, long y, long clicks)
{
	long fl = LDW(w, UW_FLAGS);
	long vert, course, p, min, span, v, step, r;
	(void)clicks;
	if (kind == 1) {
		STW(w, UW_FLAGS, fl | UWF_PRESSED);
	} else if (kind == 2) {
		STW(w, UW_FLAGS, fl & ~(long)UWF_PRESSED);
		uk_w_refresh(w);
		return 0;
	} else {
		if (kind)
			return 0;
		if (uk.capture != w)
			return 0;
	}
	vert = uk_vertical(w);
	course = sl_pos(w).r1;
	p = x - UK_BOX / 2;
	if (vert)
		p = LDW(w, UW_H) - y - UK_BOX / 2; /* vertical : depuis le bas */
	if (p < 0)
		p = 0;
	if (p > course)
		p = course;
	min = LDX(w, UW_MIN);
	span = LDX(w, UW_MAX) - min;
	p = p * span + LSR(course, 1); /* (arrondi) */
	p = UDIV(p, course);
	v = p + min;
	step = LDX(w, UW_STEP); /* multiple du pas */
	if (step > 1) {
		v = v - min + LSR(step, 1);
		v = UDIV(v, step) * step + min;
	}
	r = val_set(w, v);
	if (r)
		return r;
	uk_w_refresh(w); /* (appui sans changement : le bouton) */
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Barre de progression
 * ------------------------------------------------------------------------- */

long uk_pg_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long c, min, span, done;

	ui_fill(x, y, wd, ht, JET(TJ_FACE));
	c = JET(TJ_LINE); /* rail : CREUX, contour LIGNE */
	ui_rbox(x, y, wd, ht, LSR(ht, 1), c, c);
	c = JET(TJ_SUNKEN);
	ui_rbox(x + 1, y + 1, wd - 2, ht - 2, LSR(ht - 2, 1), c, c);
	min = LDX(w, UW_MIN); /* partie faite */
	span = LDX(w, UW_MAX) - min;
	done = LDX(w, UW_VALUE) - min;
	if (span <= 0)
		return min; /* (x0 : le min lu par ldp, comme l'asm) */
	done = UDIV(done * wd, span);
	if (!done)
		return min;
	if (ULT(done, ht)) /* (au moins un disque) */
		done = ht;
	c = JET(TJ_ACCENT);
	return ui_rbox(x, y, done, ht, LSR(ht, 1), c, c);
}

/* ---------------------------------------------------------------------------
 *  Nombre +/-
 * ------------------------------------------------------------------------- */

/* uk_nm_glyph : (uk_nm_draw) x0 = glyphe, x1 = x du bouton carré (côté x23),
 * x2 = 0 / 1 (appuyé si DRAG2 = x2 + 1) ; le widget (x19), son y (x21) et sa
 * hauteur (x23) de l'appelant */
static long nm_glyph(long g, long bx, long idx, long w, long y, long ht)
{
	if (LDX(w, UW_DRAG2) - 1 == idx) /* appuyé : CREUX */
		ui_fill(bx + 1, y + 1, ht - 2, ht - 2, JET(TJ_SUNKEN));
	bx = bx + 7 + LSR(ht, 2);
	return uk_text_box(g, bx, y, ht, JET(TJ_TEXT2), uk_bold(), 0, 15);
}

long uk_nm_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long c, v, p, end, px;

	c = JET(TJ_LINE_STRONG); /* cadre d'un champ */
	if (uk_w_focused(w))
		c = JET(TJ_ACCENT);
	ui_rbox(x, y, wd, ht, 6, c, c);
	c = JET(TJ_SURFACE);
	ui_rbox(x + 1, y + 1, wd - 2, ht - 2, 5, c, c);
	/* deux boutons à droite : « − » « + » */
	ui_fill(x + wd - LSL(ht, 1), y, 1, ht, JET(TJ_LINE));
	ui_fill(x + wd - ht, y, 1, ht, JET(TJ_LINE));
	nm_glyph(S(uk_s_minus), x + wd - LSL(ht, 1), 0, w, y, ht);
	nm_glyph(S(uk_s_plus), x + wd - ht, 1, w, y, ht);
	v = LDX(w, UW_VALUE); /* la valeur */
	p = UKA(numbuf);
	if (v < 0) {
		STB(p, 0, '-');
		p++;
		v = (long)(0 - (u64)v);
	}
	end = fmt_dec(v, p);
	STB(end, 0, 0);
	px = LDX(uk_met(), MT_PX);
	return uk_text_box(UKA(numbuf), x + 10, y, ht, JET(TJ_TEXT1), uk_font_r(), wd - LSL(ht, 1) - 12, px);
}

/* souris : un bouton -> -/+ un pas ; molette, clavier : uk_val_* */
long uk_nm_mouse(long w, long kind, long x, long y, long clicks)
{
	long wd, ht, b, step, r;
	(void)y;
	(void)clicks;
	if (kind == 2) {
		STX(w, UW_DRAG2, 0);
		uk_w_refresh(w);
		return 0;
	}
	if (kind != 1)
		return 0;
	wd = LDW(w, UW_W); /* quel bouton ? */
	ht = LDW(w, UW_H);
	if (x < wd - LSL(ht, 1))
		return 0;
	b = 1; /* - */
	if (!(x < wd - ht))
		b = 2; /* + */
	STX(w, UW_DRAG2, b);
	step = LDX(w, UW_STEP);
	if (b == 1)
		step = (long)(0 - (u64)step);
	r = val_set(w, LDX(w, UW_VALUE) + step);
	uk_w_refresh(w);
	return r;
}

/* clavier : flèches, Page... ; chiffres : tapés à la suite ; Retour arrière */
long uk_nm_key(long w, long code, long ch, long mods)
{
	if ((u32)ch < '0') {
		if ((u32)code != KEY_BACKSPACE)
			return uk_val_key(w, code, ch, mods);
		return val_set(w, SDIV(LDX(w, UW_VALUE), 10));
	}
	if ((u32)ch > '9')
		return uk_val_key(w, code, ch, mods);
	/* valeur * 10 + chiffre (au-delà : le chiffre) */
	long d = (u32)((u32)ch - '0');
	long v = LDX(w, UW_VALUE) * 10 + d;
	if (v > LDX(w, UW_MAX))
		v = d;
	return val_set(w, v);
}

/* ---------------------------------------------------------------------------
 *  Molette (bouton rond)
 * ------------------------------------------------------------------------- */

/* uk_knob_tab : cos et sin (x 1000) de -225 à +45 degrés par pas de 10 (le
 * repère va de 7 h 30 à 4 h 30 dans le sens des aiguilles d'une montre) */
static const s16 knob_tab[UK_KNOB_N * 2] = {
	-707, -707, -819, -574, -906, -423, -966, -259, -996, -87,
	-996, 87, -966, 259, -906, 423, -819, 574, -707, 707,
	-574, 819, -423, 906, -259, 966, -87, 996, 87, 996,
	259, 966, 423, 906, 574, 819, 707, 707, 819, 574,
	906, 423, 966, 259, 996, 87, 996, -87, 966, -259,
	906, -423, 819, -574, 707, -707,
};

/* uk_kn_point : (uk_kn_draw) x0 = cran 0..27, x1 = rayon -> r0, r1 = point
 * (centre : x20 + x25, x21 + x25 de l'appelant) */
static ukr kn_point(long step, long radius, long cx, long cy, long rad0)
{
	ukr r;
	long co = knob_tab[step * 2];     /* cos */
	long si = knob_tab[step * 2 + 1]; /* sin (vers le bas : y croît) */
	co = SDIV(co * radius, 1000);
	si = SDIV(si * radius, 1000);
	r.r0 = cx + rad0 + co;
	r.r1 = cy + rad0 - si;
	r.r2 = r.r3 = 0;
	return r;
}

long uk_kn_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long side, rad, i, c, min, span, mark, n;
	ukr p;

	ui_fill(x, y, wd, ht, JET(TJ_FACE));
	side = ULT(wd, ht) ? wd : ht; /* disque : le plus petit côté */
	x += LSR(wd - side, 1);
	y += LSR(ht - side, 1);
	rad = LSR(side, 1); /* rayon */
	/* graduations : points du cercle */
	for (i = 0; ULT(i, UK_KNOB_N); i += 3) {
		p = kn_point(i, rad - 2, x, y, rad);
		ui_fill(p.r0 - 1, p.r1 - 1, 2, 2, JET(TJ_LINE_STRONG));
	}
	/* disque ELEVE, contour LIGNE_FORTE */
	c = JET(TJ_LINE_STRONG);
	ui_rbox(x + 6, y + 6, side - 12, side - 12, rad - 6, c, c);
	c = JET(TJ_RAISED);
	ui_rbox(x + 7, y + 7, side - 14, side - 14, rad - 7, c, c);
	min = LDX(w, UW_MIN); /* repère : ACCENT */
	span = LDX(w, UW_MAX) - min;
	mark = 0;
	if (span > 0)
		mark = UDIV((LDX(w, UW_VALUE) - min) * (UK_KNOB_N - 1), span);
	/* trait de 4 points vers le centre */
	for (n = 4; n; n--) {
		p = kn_point(mark, (rad - 12) - n * 3, x, y, rad);
		c = JET(TJ_ACCENT);
		ui_rbox(p.r0 - 2, p.r1 - 2, 4, 4, 2, c, c);
	}
	return 0;
}

/* souris : glisser vers le haut / la droite augmente (une course de 150 px) */
long uk_kn_mouse(long w, long kind, long x, long y, long clicks)
{
	long d, min, span;
	(void)clicks;
	if (kind == 1) {
		STX(w, UW_DRAG, x - y); /* départ : x - y, valeur */
		STX(w, UW_DRAG2, LDX(w, UW_VALUE));
		return 0;
	}
	if (kind)
		return 0;
	if (uk.capture != w)
		return 0;
	d = x - y - LDX(w, UW_DRAG); /* déplacement */
	min = LDX(w, UW_MIN);
	span = LDX(w, UW_MAX) - min;
	d = SDIV(d * span, 150);
	return val_set(w, LDX(w, UW_DRAG2) + d);
}

/* ---------------------------------------------------------------------------
 *  Vu-mètre
 * ------------------------------------------------------------------------- */

/* x0 = vu-mètre, x1 = niveau : la valeur et la crête (gardée jusqu'à ce
 * qu'un niveau plus bas la fasse descendre d'un segment) */
long ui_vumeter_level(long a0, long a1)
{
	long v, peak;
	if (!a0)
		return 0;
	STX(a0, UW_VALUE, a1);
	uk_w_clamp(a0);
	v = LDX(a0, UW_VALUE);
	peak = LDX(a0, UW_AUX); /* crête */
	if (!(v >= peak)) {
		/* descend d'un trentième */
		peak -= UDIV(LDX(a0, UW_MAX), 30);
		/* comme l'asm : csel x1,x1,x2,gt garde le plus petit des deux (la
		 * crête tombe au niveau dès qu'elle le dépasse encore) */
		if (peak > v)
			peak = v;
		v = peak;
	}
	STX(a0, UW_AUX, v);
	return uk_w_refresh(a0);
}

long uk_vu_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long vert, len, nseg, min, span, lit, peak, i, c, o;

	ui_fill(x, y, wd, ht, JET(TJ_SUNKEN));
	vert = uk_vertical(w);
	len = wd; /* longueur */
	if (vert)
		len = ht;
	nseg = UDIV(len, UK_VU_SEG); /* segments */
	min = LDX(w, UW_MIN);
	span = LDX(w, UW_MAX) - min;
	if (span < 1)
		span = 1;
	lit = UDIV((LDX(w, UW_VALUE) - min) * nseg, span);  /* segments allumés */
	peak = UDIV((LDX(w, UW_AUX) - min) * nseg, span);   /* segment de crête */
	ht = LDW(w, UW_H);
	for (i = 0; ULT(i, nseg); i++) {
		/* vert, jaune aux 3/4, rouge au dernier huitième */
		c = JET(TJ_SUCCESS);
		if (!ULT(LSL(i, 2), nseg * 3)) {
			c = JET(TJ_WARNING);
			if (!ULT(LSL(i, 3), nseg * 7))
				c = JET(TJ_DANGER);
		}
		/* éteint : la couleur à peine visible ; (crête) allumée */
		if (!ULT(i, lit) && i != peak - 1)
			c = UXTW(ui_mix(c, JET(TJ_SUNKEN), 200));
		o = i * UK_VU_SEG;
		if (!vert)
			ui_fill(x + o, y + 2, UK_VU_SEG - 2, ht - 4, c); /* horizontal */
		else
			ui_fill(x + 2, (y + ht) - (o + UK_VU_SEG) + 2, wd - 4, UK_VU_SEG - 2, c); /* depuis le bas */
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Afficheur à sept segments
 * ------------------------------------------------------------------------- */

/* uk_seg7 : bit 0 haut, 1 haut droite, 2 bas droite, 3 bas, 4 bas gauche,
 * 5 haut gauche, 6 milieu ; chiffres 0..9 puis « - » */
static const u8 seg7[11] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40};

/* uk_lcd_digit : (uk_lcd_draw) w2 = segments allumés : un chiffre à x27, y x21
 * (hauteur x24, largeur x26, épaisseur x25) */
static long lcd_digit(long segs, long dx, long y, long dh, long dw, long th)
{
	long i, c, half, x0, y0, w0, h0;
	for (i = 0; ULT(i, 7); i++) {
		c = 0x0018301E; /* éteint */
		if (LSRW(segs, i) & 1)
			c = 0x0050FF7A; /* allumé */
		half = LSR(dh, 1); /* moitié de hauteur */
		if (i == 0) { /* haut */
			x0 = dx + th;
			y0 = y;
			w0 = dw - LSL(th, 1);
			h0 = th;
		} else if (i == 1) { /* haut droite */
			x0 = dx + dw - th;
			y0 = y + th;
			w0 = th;
			h0 = half - th;
		} else if (i == 2) { /* bas droite */
			x0 = dx + dw - th;
			y0 = y + half;
			w0 = th;
			h0 = half - th;
		} else if (i == 3) { /* bas */
			x0 = dx + th;
			y0 = y + dh - th;
			w0 = dw - LSL(th, 1);
			h0 = th;
		} else if (i == 4) { /* bas gauche */
			x0 = dx;
			y0 = y + half;
			w0 = th;
			h0 = half - th;
		} else if (i == 5) { /* haut gauche */
			x0 = dx;
			y0 = y + th;
			w0 = th;
			h0 = half - th;
		} else { /* milieu */
			x0 = dx + th;
			y0 = y + half - LSR(th, 1);
			w0 = dw - LSL(th, 1);
			h0 = th;
		}
		ui_fill(x0, y0, w0, h0, c);
	}
	return 0;
}

long uk_lcd_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long dh, th, dw, dx, s, c, d;

	/* fond sombre (vert très foncé), arrondi */
	ui_rbox(x, y, wd, ht, 6, 0x00101A14, 0x0008100C);
	dh = ht - 12;         /* hauteur d'un chiffre */
	th = UDIV(dh, 10);    /* épaisseur d'un segment */
	if (ULT(th, 2))
		th = 2;
	dw = LSR(dh, 1) + th; /* largeur d'un chiffre */
	dx = x + 8;           /* x du chiffre */
	y += 6;
	s = LDX(w, UW_TEXT);
	for (;;) {
		if (!s)
			return 0;
		c = LDB(s, 0);
		s++;
		if (!c)
			return 0;
		if (c == ':') {
			/* deux points */
			ui_fill(dx + th, y + LSR(dh, 2), th, th, 0x0050FF7A);
			ui_fill(dx + th, y + LSR(dh, 2) * 3, th, th, 0x0050FF7A);
			dx += LSL(th, 2);
			continue;
		}
		if (c == '.') {
			ui_fill(dx - th, y + dh - th, th, th, 0x0050FF7A);
			continue;
		}
		/* segments éteints à peine visibles, allumés en vert vif */
		if (c == '-')
			d = 10; /* « - » */
		else {
			d = (u32)(c - '0');
			if (!ULT(d, 10))
				goto adv; /* espace : rien */
		}
		lcd_digit(seg7[d], dx, y, dh, dw, th);
	adv:
		dx += dw + LSL(th, 1);
	}
}
