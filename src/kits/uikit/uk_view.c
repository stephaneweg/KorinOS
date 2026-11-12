/* uk_view.c -- UIKit : la vue dessinée à la main (uikit_view.inc), port ligne
 * à ligne de l'assembleur (src/kits/uikit/PORT.md).
 *
 * Un widget dont le programme fait lui-même le contenu. Rappels du programme
 * (mêmes registres que l'asm) :
 *	dessin (vue, largeur, hauteur)		x0..x2
 *	souris (vue, genre, x, y, infos)	x0..x4 ; infos : clics | boutons << 8
 *						| mods << 16 -> UN_*
 *	touche (vue, code HID, caractère, mods)	x0..x3 -> -1 : laissée au programme
 *	molette (vue, crans)			x0, x1 -> UN_*
 */
#include "uk.h"

/* x0..x3 = rectangle, x4 = UVW_* -> vue */
long ui_view(long a0, long a1, long a2, long a3, long a4)
{
	long type = UT_VIEW;
	if (a4 & 1)
		type = UT_VIEWF;
	return uk_w_new(type, a0, a1, a2, a3);
}

/* x0 = vue, x1 = dessin, x2 = souris, x3 = touche, x4 = molette (0 : aucune) */
long ui_view_on(long a0, long a1, long a2, long a3, long a4)
{
	if (!a0)
		return a0;
	STX(a0, UW_VWPAINT, a1);
	STX(a0, UW_VWMOUSE, a2);
	STX(a0, UW_VWKEY, a3);
	STX(a0, UW_VWWHEEL, a4);
	STX(a0, UW_VWDIRTY, 1);
	return a0;
}

/* x0 = vue : à redessiner */
long ui_view_dirty(long a0)
{
	if (!a0)
		return a0;
	STX(a0, UW_VWDIRTY, 1);
	return a0;
}

/* dessine et montre les vues marquées de la fenêtre courante */
long ui_view_flush(void)
{
	long win = uk_win();
	long r = win;   /* x0 rendu : la dernière valeur de x0, comme l'asm */
	long w = uk.wfirst;
	while (w) {
		r = LDX(w, UW_WIN);
		if (r == win) {
			r = LDW(w, UW_TYPE);
			if (!ULT(r, UT_VIEW)) {   /* comme l'asm : UT_VIEW et au-delà (lo) */
				r = LDX(w, UW_VWDIRTY);
				if (r)
					r = uk_w_draw(w);
			}
		}
		w = LDX(w, UW_NEXT);
	}
	return r;
}

/* x0 = vue : ses pixels à sa taille (VT_LAYOUT, et avant un dessin) */
long uk_vw_fit(long w)
{
	long ww = LDW(w, UW_W), hh = LDW(w, UW_H);
	long img = LDX(w, UW_AUX);
	if (img && LDX(img, IMG_W) == ww && LDX(img, IMG_H) == hh)
		return w;
	ui_image_free(img);
	STX(w, UW_AUX, 0);
	img = uk_img_new(LDW(w, UW_W), LDW(w, UW_H));
	STX(w, UW_AUX, img);
	STX(w, UW_VWDIRTY, 1);
	return 1;
}

long uk_vw_draw(long w)
{
	long r = uk_vw_fit(w);
	long img = LDX(w, UW_AUX);
	if (!img)
		return r;
	if (LDX(w, UW_VWDIRTY)) {
		STX(w, UW_VWDIRTY, 0);
		long paint = LDX(w, UW_VWPAINT);
		if (paint) {
			uk_target(img + IMG_PIX, LDX(img, IMG_W), LDX(img, IMG_H));
			/* rappel dessin : x0 = vue, x1, x2 = taille (ldp w : zéro-étendues) */
			UK_CB3(paint, w, LDW(w, UW_W), LDW(w, UW_H));
			uk_target_back();
		}
	}
	return uk_cv_draw(w);
}

/* x0 = vue, x1 = genre, x2, x3 = point, x4 = clics -> UN_* */
long uk_vw_mouse(long w, long kind, long x, long y, long clicks)
{
	long f = LDX(w, UW_VWMOUSE);
	if (!f)
		return 0;
	long ev = UV(UV_EV);
	clicks |= LDB(ev, 16) << 8;          /* boutons */
	clicks |= LDW(ev, EV_MODS) << 16;
	return UK_CB5(f, w, kind, x, y, clicks);
}

/* x0 = vue, x1 = code HID, x2 = caractère, x3 = mods -> UN_*, -1 : pas prise */
long uk_vw_key(long w, long code, long ch, long mods)
{
	long f = LDX(w, UW_VWKEY);
	if (!f)
		return -1;
	return UK_CB4(f, w, code, ch, mods);
}

/* x0 = vue, x1 = crans -> UN_* */
long uk_vw_wheel(long w, long steps)
{
	long f = LDX(w, UW_VWWHEEL);
	if (!f)
		return 0;
	return UK_CB2(f, w, steps);
}
