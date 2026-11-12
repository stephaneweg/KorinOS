/* uk_choice.c -- UIKit : case à cocher (UT_CHECK), bouton radio (UT_RADIO),
 * interrupteur (UT_SWITCH), liste déroulante (UT_DROP), liste déroulante
 * modifiable (UT_COMBO). Port ligne à ligne de uikit_choice.inc
 * (src/kits/uikit/PORT.md). docs/DESIGN-ELEGANT.md 11.6.
 *
 * Valeur : 0 / 1 (case, radio, interrupteur), élément choisi (-1 : aucun)
 * pour les listes déroulantes. Un clic (relâché sur le widget) ou Espace
 * change la valeur : UN_CHANGE. Radio : ceux du même groupe (UW_AUX) dans la
 * fenêtre se décochent. Éléments des listes déroulantes : ui_list_add,
 * ui_list_clear, ui_list_count, ui_list_get (comme une liste), ou le texte
 * « un|deux|trois » de la création ; le menu s'ouvre sous le widget
 * (ui_popup ; place de la fenêtre : WS_OP_WIN_POS). */
#include "uk.h"

/* uk_win_pos : -> r0, r1 = coin du contenu de la fenêtre courante à l'écran
 * (0, 0 si inconnu) */
ukr uk_win_pos(void)
{
	ukr r;
	STX(uk.msg, WM_A, uk_win());
	long x2 = uk_call(WS_OP_WIN_POS);
	r.r0 = 0;
	r.r1 = 0;
	r.r2 = x2;
	r.r3 = 0;
	if (x2 >= 0) {
		r.r0 = LDX(uk.msg, WM_A);
		r.r1 = LDX(uk.msg, WM_A + 8);
	}
	return r;
}

/* uk_new_sized : type, rectangle (largeur 0 : texte + extra, hauteur 0 : un
 * contrôle), texte -> widget avec son texte */
long uk_new_sized(long type, long x, long y, long w, long h, long text, long extra)
{
	long mt = uk_met();
	if (!h)
		h = LDX(mt, MT_CTL);
	if (!w) {
		w = extra;
		if (text) {
			long font = uk_font_r();
			w = ui_text_width(text, LDX(mt, MT_PX), font) + extra;
		}
	}
	long wd = uk_w_new(type, x, y, w, h);
	if (!wd)
		return 0;
	uk_set_text(wd, text, -1);
	return wd;
}

/* uk_new_toggle : rectangle, libellé, valeur (radio : groupe), type */
static long new_toggle(long x, long y, long w, long h, long label, long value, long type)
{
	label = TR(label); /* tr_x4 */
	long extra = UK_BOX + 10;
	if (type == UT_SWITCH)
		extra = 46;
	long wd = uk_new_sized(type, x, y, w, h, label, extra);
	if (!wd)
		return 0;
	if (type == UT_RADIO)
		STX(wd, UW_AUX, value);
	else
		STX(wd, UW_VALUE, value != 0);
	return wd;
}

/* x0..x3 = rectangle (0 : auto), x4 = libellé, x5 = 1 cochée -> poignée */
long ui_checkbox(long a0, long a1, long a2, long a3, long a4, long a5)
{
	return new_toggle(a0, a1, a2, a3, a4, a5, UT_CHECK);
}

/* x0..x3, x4 = libellé, x5 = groupe (les radios d'un groupe s'excluent) */
long ui_radio(long a0, long a1, long a2, long a3, long a4, long a5)
{
	return new_toggle(a0, a1, a2, a3, a4, a5, UT_RADIO);
}

/* x0..x3, x4 = libellé, x5 = 1 allumé */
long ui_switch(long a0, long a1, long a2, long a3, long a4, long a5)
{
	return new_toggle(a0, a1, a2, a3, a4, a5, UT_SWITCH);
}

/* uk_tg_text : texte (à x + décalage) centré verticalement, TEXTE1 / TEXTE3 */
static long tg_text(long w, long off)
{
	long en = uk_w_enabled(w);
	long color = JET(TJ_TEXT1);
	if (!en)
		color = JET(TJ_TEXT3);
	long px = LDX(uk_met(), MT_PX);
	long font = uk_font_r();
	long x = SXTW(LDW(w, UW_X)) + off;
	long y = SXTW(LDW(w, UW_Y));
	long max = LDW(w, UW_W) - off;
	return uk_text_box(LDX(w, UW_TEXT), x, y, LDW(w, UW_H), color, font, max, px);
}

/* uk_tg_box : -> x, y du carré de UK_BOX (centré verticalement) */
static ukr tg_box(long w)
{
	ukr r;
	r.r0 = SXTW(LDW(w, UW_X));
	r.r1 = SXTW(LDW(w, UW_Y)) + ASR(LDW(w, UW_H) - UK_BOX, 1);
	r.r2 = 0;
	r.r3 = 0;
	return r;
}

/* uk_tg_draw : dessin d'une case, d'un radio, d'un interrupteur */
long uk_tg_draw(long w)
{
	ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), JET(TJ_FACE)); /* fond */
	long en = uk_w_enabled(w);
	long value = LDX(w, UW_VALUE);
	ukr b = tg_box(w);
	long x = b.r0, y = b.r1;
	long type = LDW(w, UW_TYPE);
	if ((u32)type != UT_SWITCH) {
		long rad = 4; /* case : rayon 4 ; radio : un disque */
		if ((u32)type == UT_RADIO)
			rad = UK_BOX / 2;
		if (value) {
			long c = JET(TJ_ACCENT); /* choisi : ACCENT */
			if (!en)
				c = (u32)ui_mix(JET(TJ_FACE), JET(TJ_ACCENT), 90);
			ui_rbox(x, y, UK_BOX, UK_BOX, rad, c, c);
			if ((u32)type == UT_RADIO) {
				/* point blanc de 6 */
				long ink = JET(TJ_ACCENT_INK);
				ui_rbox(x + 6, y + 6, 6, 6, 3, ink, ink);
			} else {
				/* « ✓ » Sans Bold 13 blanc centré */
				long tw = ui_text_width(S(uk_s_check), 13, uk_bold());
				long cx = x + ASR(UK_BOX - tw, 1);
				uk_text_box(S(uk_s_check), cx, y, UK_BOX, JET(TJ_ACCENT_INK), uk_bold(), 0, 13);
			}
		} else {
			/* contour LIGNE_FORTE, intérieur SURFACE */
			long c = JET(TJ_LINE_STRONG);
			ui_rbox(x, y, UK_BOX, UK_BOX, rad, c, c);
			c = JET(TJ_SURFACE);
			if (!en)
				c = JET(TJ_FACE);
			if (LDW(w, UW_FLAGS) & 4) /* survol : intérieur CREUX */
				c = JET(TJ_SUNKEN);
			ui_rbox(x + 1, y + 1, UK_BOX - 2, UK_BOX - 2, rad - 1, c, c);
		}
		return tg_text(w, UK_BOX + 8);
	}
	/* rail 34 x 20, bouton 16 */
	y -= 1;
	long c = JET(TJ_LINE_STRONG);
	if (value)
		c = JET(TJ_ACCENT);
	ui_rbox(x, y, 34, 20, 10, c, c);
	if (!value) { /* éteint : intérieur CREUX */
		c = JET(TJ_SUNKEN);
		ui_rbox(x + 1, y + 1, 32, 18, 9, c, c);
	}
	long bx = x + 2; /* le bouton */
	if (value)
		bx = x + 16;
	long by = y + 2;
	if (value) {
		ui_rbox(bx, by, 16, 16, 8, 0x00FFFFFF, 0x00FFFFFF);
	} else {
		c = JET(TJ_LINE_STRONG);
		ui_rbox(bx, by, 16, 16, 8, c, c);
		ui_rbox(bx + 1, by + 1, 14, 14, 7, 0x00FFFFFF, 0x00FFFFFF);
	}
	return tg_text(w, 46);
}

/* uk_tg_toggle : la valeur change (radio : choisi, les autres du groupe
 * décochés) -> UN_CHANGE (0 : radio déjà choisi) */
long uk_tg_toggle(long w)
{
	if ((u32)LDW(w, UW_TYPE) != UT_RADIO) {
		STX(w, UW_VALUE, LDX(w, UW_VALUE) ^ 1);
	} else {
		if (LDX(w, UW_VALUE))
			return 0;
		STX(w, UW_VALUE, 1);
		for (long o = uk.wfirst; o; o = LDX(o, UW_NEXT)) { /* les autres du groupe */
			if (o == w)
				continue;
			if ((u32)LDW(o, UW_TYPE) != UT_RADIO)
				continue;
			if (LDX(o, UW_WIN) != LDX(w, UW_WIN))
				continue;
			if (LDX(o, UW_AUX) != LDX(w, UW_AUX))
				continue;
			if (!LDX(o, UW_VALUE))
				continue;
			STX(o, UW_VALUE, 0);
			uk_w_refresh(o);
		}
	}
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* uk_tg_mouse : comme un bouton ; un clic change la valeur */
long uk_tg_mouse(long w, long kind, long x, long y, long clicks)
{
	if (uk_bt_mouse(w, kind, x, y, clicks) != UN_ACTIVATE)
		return 0;
	return uk_tg_toggle(w);
}

/* uk_tg_key : Espace */
long uk_tg_key(long w, long code, long ch, long mods)
{
	(void)code;
	(void)mods;
	if ((u32)ch != ' ')
		return -1;
	return uk_tg_toggle(w);
}

/* ---------------------------------------------------------------------------
 *  Listes déroulantes
 * ------------------------------------------------------------------------- */

/* uk_items_split : éléments « un|deux|trois » (0 : aucun) : les ajoute */
long uk_items_split(long w, long items)
{
	long p = items;
	if (!p)
		return 0;
	while (LDB(p, 0)) {
		long d = UKA(fbuf); /* un élément */
		long n = 0;
		for (;;) {
			long c = LDB(p, 0);
			p++;
			if (!c) {
				p--;
				break;
			}
			if (c == '|')
				break;
			if (UGE(n, 254))
				continue;
			STB(d, n, c);
			n++;
		}
		STB(d, n, 0);
		ui_list_add(w, UKA(fbuf), 0);
	}
	return 0;
}

/* x0..x3 = rectangle (hauteur 0 : un contrôle), x4 = éléments « a|b|c »
 * -> poignée (valeur 0 : le premier) */
long ui_dropdown(long a0, long a1, long a2, long a3, long a4)
{
	long items = uk_tr_items(a4);
	long w = uk_new_sized(UT_DROP, a0, a1, a2, a3, 0, 0);
	if (!w)
		return 0;
	uk_items_split(w, items);
	STX(w, UW_VALUE, 0);
	if (!LDX(w, UW_NITEMS))
		STX(w, UW_VALUE, -1);
	return w;
}

/* x0..x3, x4 = éléments : liste déroulante modifiable (un champ de texte et
 * le bouton de son menu) -> poignée ; texte : ui_w_get_text.
 * comme l'asm : les éléments ne passent pas par uk_tr_items (non traduits). */
long ui_combobox(long a0, long a1, long a2, long a3, long a4)
{
	long w = uk_new_sized(UT_COMBO, a0, a1, a2, a3, 0, 0);
	if (!w)
		return 0;
	uk_items_split(w, a4);
	STX(w, UW_VALUE, -1);
	/* le champ, à gauche du bouton */
	long h = LDW(w, UW_H);
	long tb = ui_textbox(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W) - h - 4, h, 0);
	if (tb) {
		STX(tb, UW_PARENT, w);
		STX(w, UW_AUX2, tb);
	}
	return w;
}

/* uk_cb_layout : place du champ d'une liste modifiable (après ui_w_move) */
long uk_cb_layout(long w)
{
	long f = LDX(w, UW_AUX2);
	if (!f)
		return w;
	STW(f, UW_X, LDW(w, UW_X));
	STW(f, UW_Y, LDW(w, UW_Y));
	long h = LDW(w, UW_H);
	STW(f, UW_W, (u32)LDW(w, UW_W) - (u32)h - 4);
	STW(f, UW_H, h);
	return w;
}

/* uk_dd_current : -> texte de l'élément choisi (0 : aucun) */
long uk_dd_current(long w)
{
	if ((u32)LDW(w, UW_TYPE) == UT_DATE) /* (sélecteur de date : son texte) */
		return LDX(w, UW_TEXT);
	long v = LDX(w, UW_VALUE);
	if (v < 0)
		return 0;
	if (UGE(v, LDX(w, UW_NITEMS)))
		return 0;
	return LDX(LDX(w, UW_ITEMS) + (v << 4), 0);
}

/* uk_dd_draw */
long uk_dd_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	ui_fill(x, y, wd, ht, JET(TJ_FACE));
	long en = uk_w_enabled(w);
	if ((u32)LDW(w, UW_TYPE) != UT_COMBO) {
		/* contour, intérieur ELEVE (survol : CREUX) */
		long c = JET(TJ_LINE_STRONG);
		ui_rbox(x, y, wd, ht, 8, c, c);
		c = JET(TJ_RAISED);
		if (!en)
			c = JET(TJ_FACE);
		if (LDW(w, UW_FLAGS) & 4)
			c = JET(TJ_SUNKEN);
		ui_rbox(x + 1, y + 1, wd - 2, ht - 2, 7, c, c);
		long cur = uk_dd_current(w); /* l'élément choisi */
		if (cur) {
			long px = LDX(uk_met(), MT_PX);
			long font = uk_font_r();
			long col = JET(TJ_TEXT1);
			if (!en)
				col = JET(TJ_TEXT3);
			uk_text_box(cur, x + 10, y, ht, col, font, wd - 40, px);
		}
	} else {
		/* le bouton du menu à droite (le champ est l'enfant) */
		long c = JET(TJ_LINE_STRONG);
		if (LDW(w, UW_FLAGS) & 4)
			c = JET(TJ_ACCENT);
		ui_rbox(x + wd - ht, y, ht, ht, 8, c, c);
		c = JET(TJ_RAISED);
		ui_rbox(x + wd - ht + 1, y + 1, ht - 2, ht - 2, 7, c, c);
	}
	/* « ▾ » à droite */
	long font = uk_font_r();
	long cx = x + wd - 22;
	if ((u32)LDW(w, UW_TYPE) == UT_COMBO)
		cx = x + wd - LSR(ht, 1) - 5; /* (au milieu du bouton carré) */
	uk_text_box(S(uk_s_chev), cx, y, ht, JET(TJ_TEXT2), font, 0, 13);
	return 0;
}

/* uk_dd_open : ouvre son menu sous lui -> UN_CHANGE si un autre élément est
 * choisi (liste modifiable : son texte dans le champ) */
static long dd_open(long w)
{
	long n = LDX(w, UW_NITEMS);
	if (!n)
		return 0;
	long d = UKA(popbuf); /* libellés à la suite, un vide à la fin */
	long lim = UKA(popbuf) + UK_POP_MAX - 2;
	long width = LDW(w, UW_W); /* largeur : le widget, ou son plus long */
	for (long i = 0; ULT(i, n); i++) {
		long font = uk_font_r();
		long tw = ui_text_width(LDX(LDX(w, UW_ITEMS) + (i << 4), 0), UK_PX, font) + 48;
		if (UGT(tw, width))
			width = tw;
		long s = LDX(LDX(w, UW_ITEMS) + (i << 4), 0);
		long c;
		do {
			if (UGE(d, lim))
				goto end; /* comme l'asm : place pleine au milieu d'un libellé */
			c = LDB(s, 0);
			s++;
			STB(d, 0, c);
			d++;
		} while (c);
	}
end:
	STB(d, 0, 0);
	ukr p = uk_win_pos();
	long px = p.r0 + SXTW(LDW(w, UW_X));
	long py = p.r1 + SXTW(LDW(w, UW_Y)) + LDW(w, UW_H) + 2;
	long r = ui_popup(px, py, UKA(popbuf), width).r0;
	if (r < 0)
		return 0;
	if (r == LDX(w, UW_VALUE))
		return 0;
	STX(w, UW_VALUE, r);
	if ((u32)LDW(w, UW_TYPE) == UT_COMBO)
		ui_w_set_text(LDX(w, UW_AUX2), uk_dd_current(w));
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* uk_dd_mouse : survol ; appui -> le menu */
long uk_dd_mouse(long w, long kind, long x, long y, long clicks)
{
	(void)x;
	(void)y;
	(void)clicks;
	if (kind == 1) {
		uk.capture = 0; /* (le menu prend la souris) */
		STW(w, UW_FLAGS, (u32)LDW(w, UW_FLAGS) & ~(u32)UWF_HOVER);
		return dd_open(w);
	}
	u32 f = (u32)LDW(w, UW_FLAGS);
	u32 g = f | UWF_HOVER;
	if (kind == 3)
		g = f & ~(u32)UWF_HOVER;
	if (f != g) {
		STW(w, UW_FLAGS, g);
		uk_w_refresh(w);
	}
	return 0;
}

/* uk_dd_key : Espace, Entrée, Alt+bas -> menu ; haut, bas -> élément voisin */
long uk_dd_key(long w, long code, long ch, long mods)
{
	if ((u32)ch == ' ')
		return dd_open(w);
	if ((u32)code == KEY_ENTER)
		return dd_open(w);
	long v = LDX(w, UW_VALUE);
	long nv;
	if ((u32)code == KEY_UP) {
		nv = v - 1;
	} else {
		if ((u32)code != KEY_DOWN)
			return -1;
		if ((u32)mods & 0x44) /* Alt */
			return dd_open(w);
		nv = v + 1;
	}
	if (UGE(nv, LDX(w, UW_NITEMS)))
		return 0; /* (hors bornes : rien) */
	STX(w, UW_VALUE, nv);
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* uk_cb_child : liste modifiable : son champ a changé -> UN_CHANGE de la liste */
long uk_cb_child(long w, long child, long code)
{
	(void)w;
	(void)child;
	return code;
}
