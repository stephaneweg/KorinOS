/* uk_tabs.c -- UIKit : conteneurs et barres. Port ligne à ligne de
 * uikit_tabs.inc (src/kits/uikit/PORT.md).
 *
 *	UT_PANEL	panneau (Onyx panel.h) : une zone qui contient des widgets
 *			(ui_w_set_parent) ; le cacher cache son contenu
 *	UT_GROUP	cadre de groupe (Onyx groupbox.h) : un panneau titré
 *	UT_SEGMENT	contrôle segmenté (Onyx segmented.cpp, DESIGN 11.4) :
 *			boutons radio « en pilules »
 *	UT_TABS		onglets (Onyx tabhost.cpp) : le contrôle segmenté en tête
 *			et un panneau par page (ui_tabs_page)
 *	UT_TABSTRIP	barre d'onglets de documents (Onyx tabstrip.cpp) : titres,
 *			croix de fermeture, « + »
 *	UT_TOOLBAR	barre d'outils (Onyx toolbar.cpp, DESIGN 11.7), et ses
 *			boutons UT_TOOL (glyphe UTF-8, libellé, à bascule)
 *
 * Les widgets d'un panneau gardent leurs coordonnées de fenêtre ; un panneau
 * caché (ou une page d'onglets) cache tout ce qu'il contient, et se dessine
 * sous son contenu. Éléments des contrôles segmentés et des barres d'onglets :
 * « a|b|c » à la création, ou ui_list_add / ui_list_get. */
#include "uk.h"

/* ---------------------------------------------------------------------------
 *  Panneau, cadre de groupe
 * ------------------------------------------------------------------------- */

/* x0..x3 = rectangle, x4 = drapeaux (UPN_SURFACE : fond SURFACE et contour)
 * -> poignée */
long ui_panel(long a0, long a1, long a2, long a3, long a4)
{
	long w = uk_w_new(UT_PANEL, a0, a1, a2, a3);
	if (w)
		STX(w, UW_STYLE, a4);
	return w;
}

/* x0..x3 = rectangle, x4 = titre -> poignée (un panneau titré) */
long ui_groupbox(long a0, long a1, long a2, long a3, long a4)
{
	long title = TR(a4); /* tr_x4 */
	long w = uk_w_new(UT_GROUP, a0, a1, a2, a3);
	if (!w)
		return 0;
	uk_set_text(w, title, -1);
	return w;
}

/* x0 = widget, x1 = panneau (0 : la fenêtre) : le widget passe dans ce
 * panneau (montré et caché avec lui) */
long ui_w_set_parent(long a0, long a1)
{
	if (a0)
		STX(a0, UW_PARENT, a1);
	return a0;
}

long uk_pn_draw(long w)
{
	long type = LDW(w, UW_TYPE);
	/* comme l'asm : ldp w0..w3 sans sxtw (coordonnées zéro-étendues) */
	long x = LDW(w, UW_X), y = LDW(w, UW_Y);
	long wd = LDW(w, UW_W), ht = LDW(w, UW_H);
	long style = LDX(w, UW_STYLE);
	long c, small, tw;

	if ((u32)type == UT_GROUP) {
		/* cadre : contour LIGNE sous le titre */
		ui_fill(x, y, wd, ht, JET(TJ_FACE));
		c = JET(TJ_LINE);
		ui_rbox(LDW(w, UW_X), LDW(w, UW_Y) + 8, LDW(w, UW_W), LDW(w, UW_H) - 8, 6, c, c);
		c = JET(TJ_FACE);
		ui_rbox(LDW(w, UW_X) + 1, LDW(w, UW_Y) + 9, LDW(w, UW_W) - 2, LDW(w, UW_H) - 10, 5, c, c);
		/* titre : gras petit, sur le trait */
		small = LDX(uk_met(), MT_SMALL);
		tw = ui_text_width(LDX(w, UW_TEXT), small, uk_bold());
		ui_fill(LDW(w, UW_X) + 10, LDW(w, UW_Y), tw + 8, 16, JET(TJ_FACE));
		return uk_text_box(LDX(w, UW_TEXT), SXTW(LDW(w, UW_X)) + 14, SXTW(LDW(w, UW_Y)), 16,
				   JET(TJ_TEXT2), uk_bold(), 0, small);
	}
	if (!(style & 1))
		return ui_fill(x, y, wd, ht, JET(TJ_FACE));
	/* SURFACE, contour LIGNE, rayon 6 */
	c = JET(TJ_LINE);
	ui_rbox(x, y, wd, ht, 6, c, c);
	c = JET(TJ_SURFACE);
	return ui_rbox(LDW(w, UW_X) + 1, LDW(w, UW_Y) + 1, LDW(w, UW_W) - 2, LDW(w, UW_H) - 2, 5, c, c);
}

/* ---------------------------------------------------------------------------
 *  Contrôle segmenté
 * ------------------------------------------------------------------------- */

static long seg_total(long w);

/* uk_new_items : x0..x3, x4 = éléments, x5 = type -> widget avec ses éléments
 * (largeur 0 : uk_seg_total ; hauteur 0 : un contrôle) */
static long new_items(long x, long y, long wd, long ht, long items, long type)
{
	long w;
	items = uk_tr_items(items);
	if (!ht)
		ht = LDX(uk_met(), MT_CTL);
	w = uk_w_new(type, x, y, wd, ht);
	if (!w)
		return 0;
	uk_items_split(w, items);
	if (!wd) /* largeur selon les segments */
		STW(w, UW_W, seg_total(w));
	return w;
}

/* x0..x3 = rectangle (largeur 0 : selon les textes ; hauteur 0 : un
 * contrôle), x4 = segments « a|b|c » -> poignée (valeur 0) */
long ui_segmented(long a0, long a1, long a2, long a3, long a4)
{
	return new_items(a0, a1, a2, a3, a4, UT_SEGMENT);
}

/* uk_seg_w : x0 = widget, x1 = élément -> x0 = largeur de son segment (texte
 * gras + 24) */
static long seg_w(long w, long i)
{
	long text = LDX(LDX(w, UW_ITEMS) + LSL(i, 4), 0);
	long px = LDX(uk_met(), MT_PX);
	return ui_text_width(text, px, uk_bold()) + UK_SEG_PAD;
}

/* uk_seg_total : x0 = widget -> x0 = largeur de tous ses segments (+ 4) */
static long seg_total(long w)
{
	long i, t = 4;
	for (i = 0; ULT(i, LDX(w, UW_NITEMS)); i++)
		t += seg_w(w, i);
	return t;
}

/* uk_seg_rail : x0 = widget -> r0 = x du rail (centré dans le widget), r1 =
 * sa largeur */
static ukr seg_rail(long w)
{
	ukr r;
	long tot = seg_total(w);
	long wd = LDW(w, UW_W);
	long x = SXTW(LDW(w, UW_X));
	if (ULT(tot, wd))
		x += LSR(wd - tot, 1);
	r.r0 = x;
	r.r1 = tot;
	r.r2 = r.r3 = 0;
	return r;
}

/* uk_seg_paint : x0 = widget, x1 = y, x2 = hauteur : dessine le rail et ses
 * segments (valeur = segment actif, HOT = survolé) */
static long seg_paint(long w, long y, long ht)
{
	ukr r = seg_rail(w);
	long rx = r.r0;
	long c, sx, i, sw, ink, px;

	/* rail CREUX (Givre) / SURFACE (Noir) */
	c = JET(TJ_SUNKEN);
	if (JET(TJ_DARK))
		c = JET(TJ_SURFACE);
	ui_rbox(rx, y, r.r1, ht, 8, c, c);
	sx = rx + 2; /* x du segment */
	for (i = 0; ULT(i, LDX(w, UW_NITEMS)); i++) {
		sw = seg_w(w, i);
		if (LDX(w, UW_VALUE) == i) {
			/* actif : ombre LIGNE d'un pixel, SURFACE */
			c = JET(TJ_LINE);
			ui_rbox(sx, y + 2, sw, ht - 3, 6, c, c);
			c = JET(TJ_SURFACE);
			if (JET(TJ_DARK))
				c = JET(TJ_RAISED);
			ui_rbox(sx, y + 2, sw, ht - 4, 6, c, c);
		}
		/* texte : TEXTE1 actif ou survolé */
		ink = JET(TJ_TEXT2);
		if (LDX(w, UW_VALUE) == i || LDX(w, UW_HOT) == i)
			ink = JET(TJ_TEXT1);
		long text = LDX(LDX(w, UW_ITEMS) + LSL(i, 4), 0);
		px = LDX(uk_met(), MT_PX);
		uk_text_box(text, sx + UK_SEG_PAD / 2, y, ht, ink, uk_bold(), 0, px);
		sx += sw;
	}
	return LDX(w, UW_NITEMS); /* comme l'asm : x0 = le dernier NITEMS lu */
}

/* uk_seg_at : x0 = widget, x1 = x local -> x0 = segment sous ce point (-1 :
 * aucun) */
static long seg_at(long w, long lx)
{
	long x = lx + SXTW(LDW(w, UW_X)); /* x fenêtre */
	long sx = seg_rail(w).r0 + 2;
	long i, sw;
	for (i = 0; ULT(i, LDX(w, UW_NITEMS)); i++) {
		sw = seg_w(w, i);
		if (x < sx)
			return -1;
		sx += sw;
		if (x < sx)
			return i;
	}
	return -1;
}

long uk_sg_draw(long w)
{
	/* comme l'asm : ldp w0..w3 sans sxtw */
	ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), JET(TJ_FACE));
	return seg_paint(w, SXTW(LDW(w, UW_Y)), LDW(w, UW_H));
}

/* uk_tabs_show : x0 = onglets : seul le panneau de la page choisie est montré */
static void tabs_show(long w)
{
	long n = LDX(w, UW_NITEMS);
	long items = LDX(w, UW_ITEMS);
	long v = LDX(w, UW_VALUE);
	long i, p, f;
	for (i = 0; ULT(i, n); i++) {
		p = LDX(items + LSL(i, 4), 8);
		if (!p)
			continue;
		f = LDW(p, UW_FLAGS) | UWF_HIDDEN;
		if (i == v)
			f &= ~(long)UWF_HIDDEN;
		STW(p, UW_FLAGS, f);
	}
}

/* x0 = widget, x1 = segment : le choisit -> x0 = UN_CHANGE s'il change */
long uk_seg_choose(long w, long seg)
{
	if (seg < 0)
		return 0;
	if (UGE(seg, LDX(w, UW_NITEMS)))
		return 0;
	if (seg == LDX(w, UW_VALUE))
		return 0;
	STX(w, UW_VALUE, seg);
	if ((u32)LDW(w, UW_TYPE) == UT_TABS) /* onglets : la page */
		tabs_show(w);
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* souris d'un contrôle segmenté (et de l'en-tête des onglets) */
long uk_sg_mouse(long w, long kind, long x, long y, long clicks)
{
	long hot;
	(void)clicks;
	if ((u32)LDW(w, UW_TYPE) == UT_TABS) { /* onglets : l'en-tête seulement */
		if (UGE(y, LDX(uk_met(), MT_CTL)))
			return 0;
	}
	hot = -1;
	if (kind != 3) {
		hot = seg_at(w, x);
		if (kind == 1)
			return uk_seg_choose(w, hot);
	}
	if (LDX(w, UW_HOT) == hot)
		return 0;
	STX(w, UW_HOT, hot);
	uk_w_refresh(w);
	return 0;
}

/* clavier : gauche, droite */
long uk_sg_key(long w, long code, long ch, long mods)
{
	long v = LDX(w, UW_VALUE);
	(void)ch;
	(void)mods;
	if ((u32)code == KEY_LEFT)
		return uk_seg_choose(w, v - 1);
	if ((u32)code == KEY_RIGHT)
		return uk_seg_choose(w, v + 1);
	return -1;
}

/* ---------------------------------------------------------------------------
 *  Onglets (pages)
 * ------------------------------------------------------------------------- */

/* x0..x3 = rectangle, x4 = onglets « a|b|c » -> poignée ; chaque onglet a son
 * panneau (ui_tabs_page), sous l'en-tête */
long ui_tabs(long a0, long a1, long a2, long a3, long a4)
{
	long w = new_items(a0, a1, a2, a3, a4, UT_TABS);
	long ctl, i, p;
	if (!w)
		return 0;
	ctl = LDX(uk_met(), MT_CTL);
	for (i = 0; ULT(i, LDX(w, UW_NITEMS)); i++) { /* un panneau par onglet */
		/* comme l'asm : ldp w0..w3 zéro-étendus */
		p = ui_panel(LDW(w, UW_X), LDW(w, UW_Y) + ctl + 8, LDW(w, UW_W), LDW(w, UW_H) - ctl - 8, 0);
		if (!p)
			continue;
		STX(p, UW_PARENT, w);
		/* donnée de l'onglet : son panneau */
		STX(LDX(w, UW_ITEMS) + LSL(i, 4), 8, p);
	}
	tabs_show(w);
	return w;
}

/* x0 = onglets, x1 = numéro -> x0 = panneau de cette page (0 : aucun) */
long ui_tabs_page(long a0, long a1)
{
	if (!a0)
		return 0;
	if (UGE(a1, LDX(a0, UW_NITEMS)))
		return 0;
	return LDX(LDX(a0, UW_ITEMS) + LSL(a1, 4), 8);
}

long uk_tabs_draw(long w)
{
	/* fond, en-tête (centré) ; comme l'asm : ldp w0..w3 sans sxtw */
	ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), JET(TJ_FACE));
	long ctl = LDX(uk_met(), MT_CTL);
	return seg_paint(w, SXTW(LDW(w, UW_Y)), ctl);
}

/* ---------------------------------------------------------------------------
 *  Barre d'onglets de documents
 * ------------------------------------------------------------------------- */

/* x0..x3 = rectangle, x4 = drapeaux (UTS_CLOSE : croix, UTS_PLUS : « + »)
 * -> poignée (onglets : ui_tabstrip_add) */
long ui_tabstrip(long a0, long a1, long a2, long a3, long a4)
{
	long w = new_items(a0, a1, a2, a3, 0, UT_TABSTRIP);
	if (!w)
		return 0;
	STX(w, UW_STYLE, a4);
	STX(w, UW_VALUE, -1);
	STX(w, UW_AUX2, -1);
	return w;
}

/* x0 = barre, x1 = titre, x2 = donnée -> x0 = numéro de l'onglet (choisi) */
long ui_tabstrip_add(long a0, long a1, long a2)
{
	long i = ui_list_add(a0, a1, a2);
	if (i < 0)
		return i;
	STX(a0, UW_VALUE, i);
	uk_w_refresh(a0);
	return i;
}

/* x0 = barre -> x0 = onglet dont la fermeture est demandée (UN_CLOSE) */
long ui_tabstrip_closing(long a0)
{
	return LDX(a0, UW_AUX2);
}

/* uk_ts_tabw : x0 = barre -> x0 = largeur d'un onglet (tous la même, bornée) */
static long ts_tabw(long w)
{
	long n = LDX(w, UW_NITEMS);
	long wd = LDW(w, UW_W);
	long t;
	if (LDX(w, UW_STYLE) & 2)
		wd -= 32; /* place du « + » */
	if (!n)
		return UK_TS_MAX;
	t = UDIV(wd, n);
	if (UGT(t, UK_TS_MAX))
		t = UK_TS_MAX;
	if (ULT(t, UK_TS_MIN))
		t = UK_TS_MIN;
	return t;
}

long uk_ts_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long tw, i, tx, c, ink, px, max;

	/* fond CREUX, trait LIGNE en bas */
	ui_fill(x, y, wd, ht, JET(TJ_SUNKEN));
	ui_fill(x, y + ht - 1, wd, 1, JET(TJ_LINE));
	tw = ts_tabw(w);
	for (i = 0; ULT(i, LDX(w, UW_NITEMS)); i++) {
		tx = x + i * tw; /* x de l'onglet */
		if (LDX(w, UW_VALUE) == i) {
			/* actif : SURFACE arrondi en haut, sur le trait du bas */
			c = JET(TJ_LINE);
			ui_rbox(tx, y + 4, tw, ht - 3, 6, c, c);
			c = JET(TJ_SURFACE);
			ui_rbox(tx + 1, y + 5, tw - 2, ht - 4, 5, c, c);
			ink = JET(TJ_TEXT1);
		} else {
			/* inactif : un séparateur à droite */
			ui_fill(tx + tw - 1, y + 10, 1, ht - 20, JET(TJ_LINE));
			ink = JET(TJ_TEXT2);
			if (LDX(w, UW_HOT) == i)
				ink = JET(TJ_TEXT1);
		}
		px = LDX(uk_met(), MT_PX);
		max = tw - 24;
		if (LDX(w, UW_STYLE) & 1)
			max -= 18;
		uk_text_box(LDX(LDX(w, UW_ITEMS) + LSL(i, 4), 0), tx + 12, y + 2, ht, ink, uk_font_r(), max, px);
		if (LDX(w, UW_STYLE) & 1) /* croix de fermeture */
			uk_text_box(S(uk_s_cross), tx + tw - 22, y + 2, ht, JET(TJ_TEXT3), uk_font_r(), 0, UK_PX);
	}
	if (!(LDX(w, UW_STYLE) & 2)) /* « + » */
		return 0;
	return uk_text_box(S(uk_s_plus), x + LDX(w, UW_NITEMS) * tw + 10, y + 2, ht, JET(TJ_TEXT2), uk_font_r(), 0, 17);
}

/* souris : choisir, fermer (croix), nouveau (« + »), survol ; molette :
 * l'onglet voisin */
long uk_ts_mouse(long w, long kind, long x, long y, long clicks)
{
	long tw = ts_tabw(w);
	long hot = -1, n;
	(void)y;
	(void)clicks;
	if (kind == 3)
		goto hot;
	hot = UDIV(x, tw); /* onglet sous le point */
	n = LDX(w, UW_NITEMS);
	if (!ULT(hot, n)) {
		/* après les onglets : « + » ? */
		if (kind == 1 && (LDX(w, UW_STYLE) & 2) && ULT(x, n * tw + 32))
			return UN_NEW;
		hot = -1;
		goto hot;
	}
	if (kind != 1)
		goto hot;
	if (LDX(w, UW_STYLE) & 1) { /* la croix ? */
		if (!(x < hot * tw + tw - 26)) {
			STX(w, UW_AUX2, hot);
			return UN_CLOSE;
		}
	}
	if (LDX(w, UW_VALUE) == hot)
		return 0;
	STX(w, UW_VALUE, hot);
	uk_w_refresh(w);
	return UN_CHANGE;
hot:
	if (LDX(w, UW_HOT) == hot)
		return 0;
	STX(w, UW_HOT, hot);
	uk_w_refresh(w);
	return 0;
}

long uk_ts_wheel(long w, long steps)
{
	long v = LDX(w, UW_VALUE) - steps;
	if (UGE(v, LDX(w, UW_NITEMS)))
		return 0;
	STX(w, UW_VALUE, v);
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* ---------------------------------------------------------------------------
 *  Barre d'outils
 * ------------------------------------------------------------------------- */

/* x0..x3 = rectangle (hauteur 0 : contrôle + 16) -> poignée (boutons :
 * ui_toolbar_add) */
long ui_toolbar(long a0, long a1, long a2, long a3)
{
	long h = a3;
	if (!h)
		h = LDX(uk_met(), MT_CTL) + 16;
	long w = uk_w_new(UT_TOOLBAR, a0, a1, a2, h);
	if (w)
		STX(w, UW_POS, 8); /* x du prochain bouton (depuis le bord) */
	return w;
}

/* x0 = barre, x1 = glyphe UTF-8 (0 : aucun), x2 = libellé (0 : aucun), x3 =
 * drapeaux (UTB_TOGGLE, UTB_SEP : un espace avant) -> poignée du bouton (clic :
 * UN_ACTIVATE ; à bascule : sa valeur 0 / 1 change avant) */
long ui_toolbar_add(long a0, long a1, long a2, long a3)
{
	long bar = a0, glyph = a1, label = a2, fl = a3;
	long mt, ht, wd, bx, by, pos, b;

	if (fl & 2) /* séparateur : un trait, de l'espace */
		STX(bar, UW_POS, LDX(bar, UW_POS) + 12);
	mt = uk_met();
	ht = LDX(mt, MT_CTL); /* hauteur */
	wd = ht;              /* largeur : carré, ou glyphe + libellé */
	if (label) {
		wd = ui_text_width(label, LDX(mt, MT_PX), uk_font_r()) + 20;
		if (glyph)
			wd += 20;
	}
	/* comme l'asm : ldp w1,w2 zéro-étendus */
	pos = LDX(bar, UW_POS);
	bx = LDW(bar, UW_X) + pos;
	by = LDW(bar, UW_Y) + LSR(LDW(bar, UW_H) - ht, 1);
	STX(bar, UW_POS, pos + wd + 2);
	b = uk_w_new(UT_TOOL, bx, by, wd, ht);
	if (!b)
		return 0;
	STX(b, UW_PARENT, bar);
	STX(b, UW_STYLE, fl);
	if (glyph) /* glyphe : AUX2 ; libellé : le texte */
		STX(b, UW_AUX2, uk_strdup(glyph));
	uk_set_text(b, label, -1);
	return b;
}

/* uk_tool_map : les caractères des barres d'outils des programmes -> glyphes
 * de tools/icons (art.py) : point de code, nom (24 octets par entrée) */
struct tool_ent {
	u16 cp;
	char name[22];
};
static const struct tool_ent tool_map[] = {
	{0x2610, "glyph-tb-new"},
	{0x21E7, "glyph-tb-open"},
	{0x21E9, "glyph-tb-save"},
	{0x21B6, "glyph-tb-undo"},
	{0x21B7, "glyph-tb-redo"},
	{0x229E, "glyph-tb-copy"},
	{0x2750, "glyph-tb-copy"},
	{0x2702, "glyph-tb-cut"},
	{0x25A3, "glyph-tb-paste"},
	{0x25A4, "glyph-tb-paste"},
	{0x2315, "glyph-tb-find"},
	{0x25A6, "glyph-tb-table"},
	{0x25A8, "glyph-tb-image"},
	{0x2212, "glyph-tb-minus"},
	{0x002B, "glyph-tb-plus"},
	{0x2715, "glyph-tb-cross"},
	{0x2717, "glyph-tb-cross"},
	{0x2713, "glyph-tb-check"},
	{0x21E4, "glyph-tb-alignl"},
	{0x2194, "glyph-tb-alignc"},
	{0x21E5, "glyph-tb-alignr"},
	{0x2261, "glyph-tb-alignj"},
	{0x2022, "glyph-tb-bullets"},
	{0x21A1, "glyph-tb-pagebreak"},
	{0x2191, "glyph-tb-up"},
	{0x2193, "glyph-tb-down"},
	{0x2190, "glyph-tb-left"},
	{0x2192, "glyph-tb-right"},
	{0x2197, "glyph-tb-arrow"},
	{0x25BD, "glyph-tb-filter"},
	{0x25D5, "glyph-tb-chart"},
	{0x25D4, "glyph-tb-chart"},
	{0x21A9, "glyph-tb-wrap"},
	{0x21D4, "glyph-tb-merge"},
	{0x256C, "glyph-tb-freeze"},
	{0x25A5, "glyph-tb-layout"},
	{0x25C6, "glyph-tb-shape"},
	{0x2195, "glyph-tb-spacing"},
	{0x21C8, "glyph-tb-front"},
	{0x21CA, "glyph-tb-back"},
	{0x25B6, "glyph-tb-play"},
	{0, ""},
};

/* uk_tool_glyph : x0 = glyphe UTF-8 d'un outil -> x0 = nom de son image, 0 :
 * du texte */
static long tool_glyph(long s)
{
	ukr r = ttf_utf8_next(s);
	const struct tool_ent *e;
	if (!r.r0)
		return 0;
	if (LDB(r.r1, 0)) /* un seul caractère */
		return 0;
	for (e = tool_map; e->cp; e++)
		if ((long)e->cp == r.r0)
			return (long)(uintptr_t)e->name;
	return 0;
}

long uk_tbar_draw(long w)
{
	/* FACE, trait LIGNE dessous ; comme l'asm : ldp w0..w3 sans sxtw */
	long r = ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), JET(TJ_FACE));
	if (LDX(w, UW_STYLE) & 1) /* (UTBS_NOLINE : sans le trait) */
		return r;
	return ui_fill(LDW(w, UW_X), LDW(w, UW_Y) + LDW(w, UW_H) - 1, LDW(w, UW_W), 1, JET(TJ_LINE));
}

long uk_tool_draw(long w)
{
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long fl, c, ink, name, gx, gy, lx, px;

	ui_fill(x, y, wd, ht, JET(TJ_FACE));
	fl = LDW(w, UW_FLAGS);
	if (!uk_w_enabled(w))
		fl |= UWF_DISABLED;
	/* fantôme : survol CREUX, appui plus foncé ; à bascule allumé : ACCENT_CLAIR */
	c = JET(TJ_SUNKEN);
	if (fl & 8) {
		c = UXTW(ui_tone(JET(TJ_SUNKEN), 116));
		goto box;
	}
	if (LDX(w, UW_VALUE)) {
		c = JET(TJ_ACCENT_LIGHT);
		goto box;
	}
	if (!(fl & 4))
		goto ink;
box:
	ui_rbox(x, y, wd, ht, 8, c, c);
ink:
	ink = JET(TJ_TEXT1);
	if (!uk_w_enabled(w))
		ink = JET(TJ_TEXT3);
	/* glyphe : image, sinon texte de 17 px */
	if (LDX(w, UW_AUX2)) {
		name = tool_glyph(LDX(w, UW_AUX2));
		if (name) {
			/* (avec un libellé : à gauche, seul : centré) */
			gx = x + 10;
			if (!LDX(w, UW_TLEN))
				gx = x + LSR(wd - 18, 1);
			gy = y + LSR(ht - 18, 1);
			ui_glyph(name, gx, gy, 18, ink);
		} else {
			gx = x;
			if (!LDX(w, UW_TLEN)) { /* seul : centré */
				long tw = ui_text_width(LDX(w, UW_AUX2), 17, uk_font_r());
				gx = x + LSR(wd - tw, 1);
			} else {
				gx = x + 10;
			}
			uk_text_box(LDX(w, UW_AUX2), gx, y, ht, ink, uk_font_r(), 0, 17);
		}
	}
	/* libellé */
	if (!LDX(w, UW_TLEN))
		return 0;
	lx = x + 10;
	if (LDX(w, UW_AUX2))
		lx = x + 30;
	px = LDX(uk_met(), MT_PX);
	return uk_text_box(LDX(w, UW_TEXT), lx, y, ht, ink, uk_font_r(), 0, px);
}

/* souris : comme un bouton ; à bascule : la valeur change au clic */
long uk_tool_mouse(long w, long kind, long x, long y, long clicks)
{
	long r = uk_bt_mouse(w, kind, x, y, clicks);
	if (r != UN_ACTIVATE)
		return r;
	if (!(LDX(w, UW_STYLE) & 1))
		return r;
	STX(w, UW_VALUE, LDX(w, UW_VALUE) ^ 1);
	uk_w_refresh(w);
	return UN_ACTIVATE;
}
