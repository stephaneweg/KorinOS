/* uk_controls.c -- UIKit : bouton (UT_BUTTON), étiquette (UT_LABEL), texte
 * coupé en lignes (ui_text_wrap). Port ligne à ligne de uikit_controls.inc
 * (src/kits/uikit/PORT.md).
 *
 * Bouton (DESIGN-ELEGANT.md 11.1) : standard, principal (UB_DEFAULT : accent,
 * Entrée d'un dialogue), fantôme (UB_GHOST : barres d'outils) ; survol,
 * appui, grisé. Clic, Espace ou Entrée : UN_ACTIVATE. Largeur 0 : texte + 28
 * (au moins 80).
 * Étiquette : texte (UL_* de ui_label), coupé en lignes si ULB_WRAP. */
#include "uk.h"

/* ---------------------------------------------------------------------------
 *  Texte coupé en lignes
 * ------------------------------------------------------------------------- */

/* x0 = texte (« \n » : nouvelle ligne), x1 = x, x2 = y (haut), x3 = largeur,
 * w4 = couleur, x5 = police, x6 = taille px, x7 = drapeaux (bit 0 : mesurer
 * seulement) -> x0 = hauteur occupée (lignes x 1,4 px). Les lignes se coupent
 * aux espaces (un mot trop long : là où il déborde). */
long ui_text_wrap(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7)
{
	long y0 = a2;           /* [sp,16] : y de départ */
	long s = a0;            /* x19 : position */
	long x = a1;            /* x20 */
	long y = a2;            /* x21 : y de la ligne */
	long width = a3;        /* x22 */
	long color = UXTW(a4);  /* [sp] : str w4 */
	long font = a5;         /* x23 */
	long px = a6;           /* x24 */
	long flags = a7;        /* [sp,8] */
	long len, shown, used, i;

	ttf_set_size(font, px);
	while (LDB(s, 0)) {
		len = 0; /* longueur jusqu'à la fin de ligne */
		for (;;) {
			long c = LDB(s, len);
			if (!c || c == 10)
				break;
			len++;
		}
		/* ce qui tient */
		ukr r = ttf_text_pos(font, s, len, width, 1);
		shown = r.r1; /* octets montrés */
		used = r.r1;  /* octets consommés */
		if (UGE(shown, len)) {
			/* toute la ligne tient (et le saut de ligne) */
			used = len;
			if (LDB(s, len))
				used++;
			goto draw;
		}
		/* sinon : dernier espace avant */
		i = shown;
		for (;;) {
			if (!i)
				goto cut;
			i--;
			if (LDB(s, i) == ' ')
				break;
		}
		if (!i)
			goto cut;
		shown = i; /* jusqu'à l'espace (exclu) */
		used = i + 1;
		goto draw;
	cut:
		if (shown)
			goto draw; /* un mot trop long : coupé là ; au moins un point de code */
		shown = uk_u8_next(s, 0, len);
		used = shown;
	draw:
		if (!(flags & 1)) {
			long n = shown;
			if (UGT(n, 400))
				n = 400;
			long end = uk_memcpy(UKA(fitbuf), s, n);
			STB(end, 0, 0);
			/* ligne de base : y + px */
			ui_text(UKA(fitbuf), x, y + px, color, px, font, 0);
		}
		s += used;
		y += UDIV(7 * px, 5); /* interligne 1,4 */
	}
	return y - y0;
}

/* ---------------------------------------------------------------------------
 *  Bouton
 * ------------------------------------------------------------------------- */

/* x0 = x, x1 = y, x2 = largeur (0 : selon le texte), x3 = hauteur (0 : celle
 * d'un contrôle), x4 = texte, x5 = style (UB_*) -> x0 = poignée */
long ui_pushbutton(long a0, long a1, long a2, long a3, long a4, long a5)
{
	long w = a2, h = a3;
	long text = ui_tr(a4);
	long style = a5;
	long mt = uk_met();
	if (!h)
		h = LDX(mt, MT_CTL);
	if (!w) {
		w = ui_text_width(text, LDX(mt, MT_PX), uk_font_r()) + 28;
		if (ULT(w, 80))
			w = 80;
	}
	long b = uk_w_new(UT_BUTTON, a0, a1, w, h);
	if (!b)
		return 0;
	STX(b, UW_STYLE, style);
	uk_set_text(b, text, -1);
	return b;
}

long uk_bt_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long style = LDX(w, UW_STYLE);
	long fl = LDW(w, UW_FLAGS);
	long ink, c, px;

	if (!uk_w_enabled(w))
		fl |= UWF_DISABLED;
	ui_fill(x, y, wd, ht, JET(TJ_FACE)); /* fond (FACE : sous les coins) */
	ink = JET(TJ_TEXT1);                /* couleur du texte */
	if (style & 2) {
		/* --- fantôme : rien au repos ; survol CREUX, appui ton(CREUX, 116) */
		if (fl & 2) {
			ink = JET(TJ_TEXT3);
			goto text;
		}
		if (fl & 8)
			c = UXTW(ui_tone(JET(TJ_SUNKEN), 116));
		else if (fl & 4)
			c = JET(TJ_SUNKEN);
		else
			goto text;
		ui_rbox(x, y, wd, ht, 8, c, c);
		goto text;
	}
	if (style & 1) {
		/* --- principal : ACCENT uni, texte blanc ; survol et appui ACCENT_FONCE */
		ink = JET(TJ_ACCENT_INK);
		if (fl & 2) {
			c = JET(TJ_SUNKEN);
			ink = JET(TJ_TEXT3);
		} else {
			c = JET(TJ_ACCENT);
			if (fl & 12) /* survol (bit 2), appui (bit 3) */
				c = JET(TJ_ACCENT_DARK);
		}
		ui_rbox(x, y, wd, ht, 8, c, c);
		goto text;
	}
	/* --- standard : contour LIGNE_FORTE, intérieur ELEVE uni */
	c = JET(TJ_LINE_STRONG);
	if (fl & 2)
		c = JET(TJ_LINE);
	ui_rbox(x, y, wd, ht, 8, c, c);
	if (fl & 2) {
		c = JET(TJ_FACE);
		ink = JET(TJ_TEXT3);
	} else if (fl & 8) {
		c = JET(TJ_SUNKEN); /* appui : CREUX */
	} else {
		c = JET(TJ_RAISED);
		if (fl & 4) /* survol : CREUX mêlé à ELEVE */
			c = UXTW(ui_mix(JET(TJ_RAISED), JET(TJ_SUNKEN), 160));
	}
	ui_rbox(x + 1, y + 1, wd - 2, ht - 2, 7, c, c);
text:
	/* texte centré */
	px = LDX(uk_met(), MT_PX);
	{
		long tw = ui_text_width(LDX(w, UW_TEXT), px, uk_font_r());
		long m = wd - 16;
		if (tw > m)
			tw = m;
		long tx = x + ASR(wd - tw, 1);
		return uk_text_box(LDX(w, UW_TEXT), tx, y, ht, ink, uk_font_r(), wd - 16, px);
	}
}

/* souris d'un bouton : survol, appui, relâché dedans -> UN_ACTIVATE */
long uk_bt_mouse(long w, long kind, long x, long y, long clicks)
{
	long fl = LDW(w, UW_FLAGS);
	long nf;
	(void)clicks;
	if (kind == 3) {
		nf = (u32)(fl & ~(long)(UWF_HOVER | UWF_PRESSED));
	} else if (kind == 1) {
		nf = (u32)(fl | UWF_PRESSED | UWF_HOVER);
	} else if (kind == 2) {
		STW(w, UW_FLAGS, fl & ~(long)UWF_PRESSED);
		uk_w_refresh(w);
		if (!(fl & 8)) /* relâché sur le bouton appuyé */
			return 0;
		return UN_ACTIVATE;
	} else {
		/* déplacement : dedans ? */
		long wd = LDW(w, UW_W), ht = LDW(w, UW_H);
		long in = 0;
		if (!(x < 0 || x >= wd || y < 0 || y >= ht))
			in = 1;
		long on = fl | UWF_HOVER;
		long off = (u32)(fl & ~(long)(UWF_HOVER | UWF_PRESSED));
		if (uk.capture == w) /* (bouton tenu : appuyé s'il est dedans) */
			on |= UWF_PRESSED;
		nf = in ? on : off;
	}
	STW(w, UW_FLAGS, nf);
	if ((u32)nf == (u32)fl)
		return 0;
	uk_w_refresh(w);
	return 0;
}

/* clavier : Espace, Entrée -> UN_ACTIVATE */
long uk_bt_key(long w, long code, long ch, long mods)
{
	(void)w;
	(void)mods;
	if ((u32)code == KEY_ENTER)
		return UN_ACTIVATE;
	if ((u32)ch == ' ')
		return UN_ACTIVATE;
	return -1;
}

/* ---------------------------------------------------------------------------
 *  Étiquette
 * ------------------------------------------------------------------------- */

/* x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = texte, x5 = drapeaux
 * (ULB_*) -> x0 = poignée */
long ui_label(long a0, long a1, long a2, long a3, long a4, long a5)
{
	long text = ui_tr(a4);
	long w = uk_w_new(UT_LABEL, a0, a1, a2, a3);
	if (!w)
		return 0;
	STX(w, UW_STYLE, a5);
	uk_set_text(w, text, -1);
	return w;
}

long uk_lb_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long style = LDX(w, UW_STYLE);
	long px, ink, font, tx;

	ui_fill(x, y, wd, ht, JET(TJ_FACE));
	px = LDX(uk_met(), MT_PX); /* taille, police, couleur */
	if (style & 32)            /* ULB_TITLE : 15 px gras */
		px = 15;
	ink = JET(TJ_TEXT1);
	if (style & 2) /* ULB_DIM */
		ink = JET(TJ_TEXT2);
	if (!uk_w_enabled(w))
		ink = JET(TJ_TEXT3);
	font = uk_font_r();
	if (style & (1 | 32)) /* ULB_BOLD, ULB_TITLE */
		font = uk_bold();
	if (style & 4) /* ULB_WRAP : lignes */
		return ui_text_wrap(LDX(w, UW_TEXT), x, y, wd, ink, font, px, 0);
	/* une ligne : à gauche, à droite, au centre */
	tx = x;
	if (style & (8 | 16)) {
		long tw = ui_text_width(LDX(w, UW_TEXT), px, font);
		if (tw > wd)
			tw = wd;
		long d = wd - tw;
		if (!(style & 8))
			d = ASR(d, 1);
		tx = x + d;
	}
	return uk_text_box(LDX(w, UW_TEXT), tx, y, ht, ink, font, wd, px);
}
