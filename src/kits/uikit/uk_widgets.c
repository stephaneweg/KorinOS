/* uk_widgets.c -- UIKit : uikit_widgets.inc (tas du kit, métriques, texte
 * coupé, découpe, table des types, création, dessin, fonctions génériques
 * ui_w_*, événements ui_w_event, barre de défilement). Port ligne à ligne de
 * l'assembleur (src/kits/uikit/PORT.md). */
#include "uk.h"

/* ---------------------------------------------------------------------------
 * Tas du kit : classes de puissances de deux (32 o à 1 Go), une liste libre
 * par classe ; en-tête de 16 octets (+0 lien quand libre, +8 classe).
 * ------------------------------------------------------------------------- */

/* uk_alloc : x0 = taille -> bloc mis à zéro (0 : échec) */
long uk_alloc(long size)
{
	long c = 5, b, n, p;
	size += 16;
	for (;;) {
		if (UGE(LSL(1, c), size))
			break;
		c++;
		if (!ULE(c, 30))
			return 0;
	}
	b = uk.free_heads[c];
	if (b) {
		uk.free_heads[c] = LDX(b, 0);
	} else {
		b = uk_malloc(LSL(1, c));
		if (!b)
			return 0;
	}
	STX(b, 8, c);
	p = b + 16;
	n = LSL(1, c) - 16;
	do { /* (stp xzr, xzr par 16 octets) */
		STX(p, 0, 0);
		STX(p, 8, 0);
		p += 16;
		n -= 16;
	} while (n > 0);
	return b + 16;
}

/* uk_free : x0 = bloc de uk_alloc (0 : rien) */
long uk_free(long p)
{
	long c;
	if (!p)
		return 0;
	p -= 16;
	c = LDX(p, 8);
	STX(p, 0, uk.free_heads[c]);
	uk.free_heads[c] = p;
	return p;
}

/* uk_alloc_size : octets utilisables */
long uk_alloc_size(long p)
{
	return LSL(1, LDX(p - 16, 8)) - 16;
}

/* uk_strlen : longueur (feuille) */
long uk_strlen(long s)
{
	long n = 0;
	while (LDB(s, n))
		n++;
	return n;
}

/* uk_memcpy : copie vers le bas, octet par octet -> fin de la destination */
long uk_memcpy(long d, long s, long n)
{
	while (n) {
		STB(d, 0, LDB(s, 0));
		d++;
		s++;
		n--;
	}
	return d;
}

/* uk_memcpy_up : copie vers le haut (dest > source, recouvrement) */
long uk_memcpy_up(long d, long s, long n)
{
	while (n) {
		n--;
		STB(d, n, LDB(s, n));
	}
	return d;
}

/* uk_strdup : x0 = chaîne (0 : vide) -> copie dans le tas du kit */
long uk_strdup(long s)
{
	long src = s ? s : S(uk_empty), n, d;
	n = uk_strlen(src) + 1;
	d = uk_alloc(n);
	if (d)
		uk_memcpy(d, src, n);
	return d;
}

/* ---------------------------------------------------------------------------
 * Métriques, polices
 * ------------------------------------------------------------------------- */

/* uk_mt_tab : px, petite, ligne, contrôle, barre, en-tête, menu (8 par échelle) */
static const u8 uk_mt_tab[4 * 8] = {
	14, 13, 30, 32, 8, 24, 28, 0,  /* grande */
	14, 13, 30, 32, 10, 24, 26, 0, /* moyenne */
	13, 12, 26, 28, 10, 22, 24, 0, /* petite */
	14, 13, 36, 36, 12, 28, 36, 0, /* tactile (density = touch) */
};

/* uk_met : -> métriques de l'échelle (MT_*), recalculées après un thème relu */
long uk_met(void)
{
	if (!uk.mt_ok) {
		long e, i;
		uk.mt_ok = 1;
		e = ui_scale();
		if (UGT(e, 2))
			e = 2;
		if (LDX(uk.tv, UK_TV_DENSITY) == 1)
			e = 3;
		for (i = 0; ULT(i, UK_MT_N); i++)
			uk.mt[i] = uk_mt_tab[e * 8 + i];
	}
	return UKA(mt);
}

/* ui_metric : x0 = UMT_* -> métrique (UMT_TOUCH : 1 tactile) ; -1 : inconnue */
long ui_metric(long i)
{
	long mt = uk_met();
	if (i == UMT_TOUCH)
		return LDX(uk.tv, UK_TV_DENSITY) == 1;
	if (UGE(i, UK_MT_N))
		return -1;
	return LDX(mt, i * 8);
}

long uk_font_r(void)
{
	return uk.vars + UV_FONT;
}

long uk_font_m(void)
{
	return uk.vars + UV_FONT_M;
}

/* ---------------------------------------------------------------------------
 * Texte : largeur d'un préfixe, point -> position, ellipse
 * ------------------------------------------------------------------------- */

/* ui_text_prefix : largeur des x1 premiers octets (-1 : tout) */
long ui_text_prefix(long s, long n, long px, long font)
{
	ttf_set_size(font, px);
	return ttf_text_pos(font, s, n, -1, 0).r0;
}

/* ui_text_hit : x3 = x -> r0 octets avant le point le plus proche, r1 sa position */
ukr ui_text_hit(long s, long px, long font, long x)
{
	ukr r = {0, 0, 0, 0};
	ukr p;
	ttf_set_size(font, px);
	if (x < 0)
		return r;
	p = ttf_text_pos(font, s, -1, x, 0);
	r.r0 = p.r1;
	r.r1 = p.r0;
	return r;
}

/* ui_text_fit : comme ui_text, x6 = largeur max : coupé avec « … » */
long ui_text_fit(long s, long x, long base, long color, long px, long font, long max)
{
	color = (u32)color;
	if (max > 0) {
		ttf_set_size(font, px);
		if (ttf_text_width(font, s) > max) {
			long avail = max - ttf_text_width(font, S(uk_s_ellipsis)), n, e;
			if (avail < 0)
				avail = 0;
			n = ttf_text_pos(font, s, -1, avail, 1).r1;
			if (UGT(n, 400))
				n = 400;
			e = uk_memcpy(UKA(fitbuf), s, n);
			STB(e, 0, 0xE2);
			STB(e, 1, 0x80);
			STB(e, 2, 0xA6);
			STB(e, 3, 0);
			s = UKA(fitbuf);
		}
	}
	return ui_text(s, x, base, color, px, font, max < 0 ? 0 : max);
}

/* uk_text_box : texte centré verticalement dans une boîte (x2 haut, x3 hauteur) */
long uk_text_box(long s, long x, long y, long h, long color, long font, long max, long px)
{
	long t = UDIV(px * 37 + 50, 100);
	return ui_text_fit(s, x, y + LSR(h, 1) + t, (u32)color, px, font, max);
}

/* ---------------------------------------------------------------------------
 * Découpe : ui_clip (programme) et pile interne (widgets)
 * ------------------------------------------------------------------------- */

long ui_clip(long x, long y, long w, long h)
{
	uk.clip_sp = 0; /* (la pile interne repart de zéro) */
	if (w <= 0 || h <= 0) {
		uk.clip_on = 0;
		return x;
	}
	uk.clip_on = 1;
	w += x;
	h += y;
	uk.clip[0] = x < 0 ? 0 : x;
	uk.clip[1] = y < 0 ? 0 : y;
	uk.clip[2] = w;
	uk.clip[3] = h;
	return uk.clip[0];
}

/* uk_clip_push : découpe à l'intersection avec la découpe courante (empilée) */
long uk_clip_push(long x, long y, long w, long h)
{
	long sp = uk.clip_sp, *e, on;
	if (UGE(sp, 8))
		return x; /* (pile pleine : on garde l'ancienne) */
	e = &uk.clip_stack[sp * 5];
	uk.clip_sp = sp + 1;
	e[0] = uk.clip[0];
	e[1] = uk.clip[1];
	e[2] = uk.clip[2];
	e[3] = uk.clip[3];
	on = uk.clip_on;
	e[4] = on;
	w += x;
	h += y;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (on) {
		if (x < uk.clip[0])
			x = uk.clip[0];
		if (y < uk.clip[1])
			y = uk.clip[1];
		if (w > uk.clip[2])
			w = uk.clip[2];
		if (h > uk.clip[3])
			h = uk.clip[3];
	}
	if (w < x) /* (vide : un rectangle nul) */
		w = x;
	if (h < y)
		h = y;
	uk.clip[0] = x;
	uk.clip[1] = y;
	uk.clip[2] = w;
	uk.clip[3] = h;
	uk.clip_on = 1;
	return x;
}

long uk_clip_pop(void)
{
	long sp = uk.clip_sp, *e;
	if (!sp)
		return 0;
	sp--;
	uk.clip_sp = sp;
	e = &uk.clip_stack[sp * 5];
	uk.clip[0] = e[0];
	uk.clip[1] = e[1];
	uk.clip[2] = e[2];
	uk.clip[3] = e[3];
	uk.clip_on = e[4];
	return sp;
}

/* ---------------------------------------------------------------------------
 * Table des types (uk_vt de l'asm) : entrées présentes (bit = VT_* / 4) et
 * drapeaux VTF_* par type ; les appels passent par des switch (pas de table
 * de pointeurs : le chargeur de kits ne fait pas de relocations).
 * ------------------------------------------------------------------------- */

#define D 1  /* VT_DRAW */
#define M 2  /* VT_MOUSE */
#define K 4  /* VT_KEY */
#define W 8  /* VT_WHEEL */
#define C 16 /* VT_CHILD */
#define L 32 /* VT_LAYOUT */
static const u8 uk_vt_has_tab[UT_VIEWF + 1] = {
	0,                   /* 0 : aucun */
	D | M | W,           /* UT_SCROLL */
	D | M | K | W | C | L, /* UT_LIST */
	D | M | K,           /* UT_TEXT */
	D | M | K,           /* UT_BUTTON */
	D,                   /* UT_LABEL */
	D | M | K,           /* UT_CHECK */
	D | M | K,           /* UT_RADIO */
	D | M | K,           /* UT_SWITCH */
	D | M | K,           /* UT_DROP */
	D | M | C | L,       /* UT_COMBO */
	D | M | K | W | C | L, /* UT_AREA */
	D | M | K | W,       /* UT_SLIDER */
	D,                   /* UT_PROGRESS */
	D | M | K | W,       /* UT_NUMERIC */
	D | M | K | W,       /* UT_KNOB */
	D,                   /* UT_VUMETER */
	D,                   /* UT_LCD */
	D | M,               /* UT_SPLIT */
	D,                   /* UT_PANEL */
	D,                   /* UT_GROUP */
	D | M | K,           /* UT_SEGMENT */
	D | M | K,           /* UT_TABS */
	D | M | W,           /* UT_TABSTRIP */
	D,                   /* UT_TOOLBAR */
	D | M,               /* UT_TOOL */
	D,                   /* UT_IMAGE */
	D | M | K,           /* UT_CAL */
	D | M | K,           /* UT_DATE */
	D | M,               /* UT_PALETTE */
	D | M | K,           /* UT_COLOR */
	D | M | K | W | C | L, /* UT_TREE */
	D | M,               /* UT_CANVAS */
	D | M | K | W | L,   /* UT_VIEW */
	D | M | K | W | L,   /* UT_VIEWF */
};
#undef D
#undef M
#undef K
#undef W
#undef C
#undef L

#define FRM (VTF_FOCUS | VTF_RING | VTF_MOVE)
#define FR (VTF_FOCUS | VTF_RING)
static const u8 uk_vt_flags_tab[UT_VIEWF + 1] = {
	0, 0, VTF_FOCUS, FR, FRM, 0, FRM, FRM, FRM, FR, 0, FR | VTF_TAB, FR, 0, FR, FR, 0, 0, 0, 0, 0,
	FRM, VTF_FOCUS | VTF_MOVE, 0, 0, VTF_MOVE, 0, FR, FR, 0, FR, VTF_FOCUS, 0, 0, VTF_FOCUS | VTF_TAB,
};
#undef FRM
#undef FR

/* uk_vt_fn : 1 si le type du widget a cette entrée VT_* */
long uk_vt_has(long w, long entry)
{
	long t = LDW(w, UW_TYPE);
	if (UGT(t, UT_VIEWF))
		return 0;
	return (uk_vt_has_tab[t] >> (entry / 4)) & 1;
}

/* uk_vt_flags : drapeaux VTF_* du type */
long uk_vt_flags(long w)
{
	long t = LDW(w, UW_TYPE);
	if (UGT(t, UT_VIEWF))
		return 0;
	return uk_vt_flags_tab[t];
}

static long vt_draw(long w)
{
	switch (LDW(w, UW_TYPE)) {
	case UT_SCROLL: return uk_sb_draw(w);
	case UT_LIST: return uk_ls_draw(w);
	case UT_TEXT: return uk_tb_draw(w);
	case UT_BUTTON: return uk_bt_draw(w);
	case UT_LABEL: return uk_lb_draw(w);
	case UT_CHECK: case UT_RADIO: case UT_SWITCH: return uk_tg_draw(w);
	case UT_DROP: case UT_COMBO: return uk_dd_draw(w);
	case UT_AREA: return uk_ta_draw(w);
	case UT_SLIDER: return uk_sl_draw(w);
	case UT_PROGRESS: return uk_pg_draw(w);
	case UT_NUMERIC: return uk_nm_draw(w);
	case UT_KNOB: return uk_kn_draw(w);
	case UT_VUMETER: return uk_vu_draw(w);
	case UT_LCD: return uk_lcd_draw(w);
	case UT_SPLIT: return uk_sp_draw(w);
	case UT_PANEL: case UT_GROUP: return uk_pn_draw(w);
	case UT_SEGMENT: return uk_sg_draw(w);
	case UT_TABS: return uk_tabs_draw(w);
	case UT_TABSTRIP: return uk_ts_draw(w);
	case UT_TOOLBAR: return uk_tbar_draw(w);
	case UT_TOOL: return uk_tool_draw(w);
	case UT_IMAGE: return uk_im_draw(w);
	case UT_CAL: return uk_cal_draw(w);
	case UT_DATE: return uk_dp_draw(w);
	case UT_PALETTE: return uk_pal_draw(w);
	case UT_COLOR: return uk_cp_draw(w);
	case UT_TREE: return uk_tr_draw(w);
	case UT_CANVAS: return uk_cv_draw(w);
	case UT_VIEW: case UT_VIEWF: return uk_vw_draw(w);
	}
	return 0;
}

static long vt_mouse(long w, long kind, long x, long y, long clicks)
{
	switch (LDW(w, UW_TYPE)) {
	case UT_SCROLL: return uk_sb_mouse(w, kind, x, y, clicks);
	case UT_LIST: return uk_ls_mouse(w, kind, x, y, clicks);
	case UT_TEXT: return uk_tb_mouse(w, kind, x, y, clicks);
	case UT_BUTTON: return uk_bt_mouse(w, kind, x, y, clicks);
	case UT_CHECK: case UT_RADIO: case UT_SWITCH: return uk_tg_mouse(w, kind, x, y, clicks);
	case UT_DROP: case UT_COMBO: return uk_dd_mouse(w, kind, x, y, clicks);
	case UT_AREA: return uk_ta_mouse(w, kind, x, y, clicks);
	case UT_SLIDER: return uk_sl_mouse(w, kind, x, y, clicks);
	case UT_NUMERIC: return uk_nm_mouse(w, kind, x, y, clicks);
	case UT_KNOB: return uk_kn_mouse(w, kind, x, y, clicks);
	case UT_SPLIT: return uk_sp_mouse(w, kind, x, y, clicks);
	case UT_SEGMENT: case UT_TABS: return uk_sg_mouse(w, kind, x, y, clicks);
	case UT_TABSTRIP: return uk_ts_mouse(w, kind, x, y, clicks);
	case UT_TOOL: return uk_tool_mouse(w, kind, x, y, clicks);
	case UT_CAL: return uk_cal_mouse(w, kind, x, y, clicks);
	case UT_DATE: return uk_dp_mouse(w, kind, x, y, clicks);
	case UT_PALETTE: return uk_pal_mouse(w, kind, x, y, clicks);
	case UT_COLOR: return uk_cp_mouse(w, kind, x, y, clicks);
	case UT_TREE: return uk_tr_mouse(w, kind, x, y, clicks);
	case UT_CANVAS: return uk_cv_mouse(w, kind, x, y, clicks);
	case UT_VIEW: case UT_VIEWF: return uk_vw_mouse(w, kind, x, y, clicks);
	}
	return 0;
}

static long vt_key(long w, long code, long ch, long mods)
{
	switch (LDW(w, UW_TYPE)) {
	case UT_LIST: return uk_ls_key(w, code, ch, mods);
	case UT_TEXT: return uk_tb_key(w, code, ch, mods);
	case UT_BUTTON: return uk_bt_key(w, code, ch, mods);
	case UT_CHECK: case UT_RADIO: case UT_SWITCH: return uk_tg_key(w, code, ch, mods);
	case UT_DROP: return uk_dd_key(w, code, ch, mods);
	case UT_AREA: return uk_ta_key(w, code, ch, mods);
	case UT_SLIDER: case UT_KNOB: return uk_val_key(w, code, ch, mods);
	case UT_NUMERIC: return uk_nm_key(w, code, ch, mods);
	case UT_SEGMENT: case UT_TABS: return uk_sg_key(w, code, ch, mods);
	case UT_CAL: return uk_cal_key(w, code, ch, mods);
	case UT_DATE: return uk_dp_key(w, code, ch, mods);
	case UT_COLOR: return uk_cp_key(w, code, ch, mods);
	case UT_TREE: return uk_tr_key(w, code, ch, mods);
	case UT_VIEW: case UT_VIEWF: return uk_vw_key(w, code, ch, mods);
	}
	return -1;
}

static long vt_wheel(long w, long steps)
{
	switch (LDW(w, UW_TYPE)) {
	case UT_SCROLL: return uk_sb_wheel(w, steps);
	case UT_LIST: return uk_ls_wheel(w, steps);
	case UT_AREA: return uk_ta_wheel(w, steps);
	case UT_SLIDER: case UT_NUMERIC: case UT_KNOB: return uk_val_wheel(w, steps);
	case UT_TABSTRIP: return uk_ts_wheel(w, steps);
	case UT_TREE: return uk_tr_wheel(w, steps);
	case UT_VIEW: case UT_VIEWF: return uk_vw_wheel(w, steps);
	}
	return 0;
}

static long vt_child(long w, long child, long code)
{
	switch (LDW(w, UW_TYPE)) {
	case UT_LIST: return uk_ls_child(w, child, code);
	case UT_COMBO: return uk_cb_child(w, child, code);
	case UT_AREA: return uk_ta_child(w, child, code);
	case UT_TREE: return uk_tr_child(w, child, code);
	}
	return 0;
}

static long vt_layout(long w)
{
	switch (LDW(w, UW_TYPE)) {
	case UT_LIST: return uk_ls_layout(w);
	case UT_COMBO: return uk_cb_layout(w);
	case UT_AREA: return uk_ta_layout(w);
	case UT_TREE: return uk_tr_layout(w);
	case UT_VIEW: case UT_VIEWF: return uk_vw_fit(w);
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * Création, liste des widgets
 * ------------------------------------------------------------------------- */

/* uk_w_new : x0 = type, x1..x4 = rectangle -> widget (0 : échec), à la fin de
 * la liste, dans la fenêtre courante */
long uk_w_new(long type, long x, long y, long w, long h)
{
	long b = uk_alloc(UW_SIZE), last;
	if (!b)
		return 0;
	STW(b, UW_TYPE, type);
	STW(b, UW_X, x);
	STW(b, UW_Y, y);
	STW(b, UW_W, w);
	STW(b, UW_H, h);
	STX(b, UW_WIN, uk_win());
	STX(b, UW_HOT, -1);
	STX(b, UW_SORT, -1);
	STX(b, UW_STEP, 1);
	uk_des_note(b, 1); /* rectangle de conception : celui-ci */
	last = uk.wlast;
	uk.wlast = b;
	if (last)
		STX(last, UW_NEXT, b);
	else
		uk.wfirst = b;
	return b;
}

/* uk_w_shown : 1 s'il est montré (lui et ce qui le contient) dans la fenêtre courante */
long uk_w_shown(long w)
{
	if (LDX(w, UW_WIN) != uk_win())
		return 0;
	do {
		if (LDW(w, UW_FLAGS) & UWF_HIDDEN)
			return 0;
		w = LDX(w, UW_PARENT);
	} while (w);
	return 1;
}

/* uk_w_enabled : 1 s'il est actif (lui et ce qui le contient) */
long uk_w_enabled(long w)
{
	do {
		if (LDW(w, UW_FLAGS) & UWF_DISABLED)
			return 0;
		w = LDX(w, UW_PARENT);
	} while (w);
	return 1;
}

/* uk_w_focused : 1 s'il a le focus (et la fenêtre aussi) */
long uk_w_focused(long w)
{
	if (w != uk.focus)
		return 0;
	return uk.winact;
}

/* uk_w_inview : 0 si une vue qui le contient coupe ce point (feuille) */
static long uk_w_inview(long w, long x, long y)
{
	for (;;) {
		long px, py;
		w = LDX(w, UW_PARENT);
		if (!w)
			return 1;
		if (ULT(LDW(w, UW_TYPE), UT_VIEW))
			continue;
		px = LDSW(w, UW_X);
		py = LDSW(w, UW_Y);
		if (x < px || y < py)
			return 0;
		if (x >= px + LDW(w, UW_W))
			return 0;
		if (y >= py + LDW(w, UW_H))
			return 0;
	}
}

/* uk_w_at : x0, x1 = point (fenêtre), x2 = fenêtre -> widget montré le plus
 * en avant sous ce point (0 : aucun) */
long uk_w_at(long x, long y, long win)
{
	long w, found = 0;
	for (w = uk.wfirst; w; w = LDX(w, UW_NEXT)) {
		long wx, wy;
		if (LDX(w, UW_WIN) != win)
			continue;
		wx = LDSW(w, UW_X);
		wy = LDSW(w, UW_Y);
		if (x < wx || y < wy)
			continue;
		if (x >= wx + LDW(w, UW_W) || y >= wy + LDW(w, UW_H))
			continue;
		if (!uk_w_shown(w))
			continue;
		if (!uk_w_inview(w, x, y)) /* (un enfant coupé par sa vue : pas là) */
			continue;
		found = w;
	}
	return found;
}

/* ---------------------------------------------------------------------------
 * Dessin
 * ------------------------------------------------------------------------- */

/* uk_ring : anneau de focus (11.8) ou son effacement (FACE) autour */
static long uk_ring(long w)
{
	long x = LDSW(w, UW_X), y = LDSW(w, UW_Y), c;
	ui_fill(x - 3, y - 3, LDW(w, UW_W) + 6, LDW(w, UW_H) + 6, JET(TJ_FACE));
	if (UGE(LDW(w, UW_H), 80)) /* (pas autour d'une grande zone) */
		return 0;
	if (!uk_w_focused(w))
		return 0;
	c = ui_mix(JET(TJ_FACE), JET(TJ_ACCENT), 110);
	ui_rbox(x - 3, y - 3, LDW(w, UW_W) + 6, LDW(w, UW_H) + 6, 11, (u32)c, (u32)c);
	return ui_rbox(x - 1, y - 1, LDW(w, UW_W) + 2, LDW(w, UW_H) + 2, 9, JET(TJ_FACE), JET(TJ_FACE));
}

/* uk_w_paint : le dessine (lui puis ses enfants), sans ui_update */
long uk_w_paint(long w)
{
	long k;
	if (!uk_w_shown(w))
		return 0;
	if (uk_vt_flags(w) & VTF_RING)
		uk_ring(w);
	uk_clip_push(LDSW(w, UW_X), LDSW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H));
	if (uk_vt_has(w, VT_DRAW))
		vt_draw(w);
	/* une vue coupe ses enfants (formulaire qui défile) ; sinon ils peuvent déborder */
	if (ULT(LDW(w, UW_TYPE), UT_VIEW))
		uk_clip_pop();
	for (k = uk.wfirst; k; k = LDX(k, UW_NEXT))
		if (LDX(k, UW_PARENT) == w)
			uk_w_paint(k);
	if (UGE(LDW(w, UW_TYPE), UT_VIEW))
		uk_clip_pop();
	return 0;
}

/* uk_update_clip : ui_update borné à la surface */
long uk_update_clip(long x, long y, long w, long h)
{
	long bw = LDX(uk.vars, UV_W), bh = LDX(uk.vars, UV_H);
	w += x;
	h += y;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (w > bw)
		w = bw;
	if (h > bh)
		h = bh;
	w -= x;
	if (w <= 0)
		return w;
	h -= y;
	if (h <= 0)
		return h;
	return ui_update(x, y, w, h);
}

/* uk_w_draw : dessine et montre (ui_update de son rectangle, anneau compris) */
long uk_w_draw(long w)
{
	long prev = uk_route(LDX(w, UW_WIN)); /* (widget d'une autre fenêtre : on y va) */
	if (uk_w_shown(w)) {
		uk_w_paint(w);
		uk_update_clip(LDSW(w, UW_X) - 3, LDSW(w, UW_Y) - 3, LDW(w, UW_W) + 6, LDW(w, UW_H) + 6);
	}
	if (prev)
		ui_window_use(prev);
	return 0;
}

/* uk_w_refresh : à redessiner après un changement (son parent s'il en a un) */
long uk_w_refresh(long w)
{
	long p;
	while ((p = LDX(w, UW_PARENT)))
		w = p;
	return uk_w_draw(w);
}

/* ---------------------------------------------------------------------------
 * Fonctions génériques (table)
 * ------------------------------------------------------------------------- */

/* ui_w_draw : x0 = widget (0 : tous ceux sans parent) : dessine et montre */
long ui_w_draw(long w)
{
	if (w)
		return uk_w_draw(w);
	for (w = uk.wfirst; w; w = LDX(w, UW_NEXT))
		if (!LDX(w, UW_PARENT))
			uk_w_draw(w);
	return 0;
}

/* ui_w_move : nouvelle place (rien n'est redessiné) */
long ui_w_move(long w, long x, long y, long ww, long h)
{
	if (!w)
		return 0;
	if (LDW(w, UW_TYPE) == UT_TOOLBAR) /* ses boutons la suivent */
		uk_tbar_shift(w, x - LDW(w, UW_X), y - LDW(w, UW_Y));
	STW(w, UW_X, x);
	STW(w, UW_Y, y);
	STW(w, UW_W, ww);
	STW(w, UW_H, h);
	uk_des_note(w, 0);
	if (uk_vt_has(w, VT_LAYOUT))
		return vt_layout(w);
	return w;
}

/* ui_w_show : 1 montre (redessiné) / 0 caché (perd le focus) */
long ui_w_show(long w, long show)
{
	long f;
	if (!w)
		return 0;
	f = LDW(w, UW_FLAGS) | UWF_HIDDEN;
	if (show)
		f &= ~(long)UWF_HIDDEN;
	STW(w, UW_FLAGS, f);
	if (show)
		return uk_w_refresh(w);
	if (uk.focus == w)
		uk.focus = 0;
	return w;
}

/* ui_w_enable : 1 actif / 0 grisé */
long ui_w_enable(long w, long on)
{
	long f;
	if (!w)
		return 0;
	f = LDW(w, UW_FLAGS) | UWF_DISABLED;
	if (on)
		f &= ~(long)UWF_DISABLED;
	STW(w, UW_FLAGS, f);
	return uk_w_refresh(w);
}

/* ui_w_focus : x0 = widget (0 : aucun) : prend le focus clavier */
long ui_w_focus(long w)
{
	long back = 0, old;
	if (w) /* (widget d'une autre fenêtre : son focus) */
		back = uk_route(LDX(w, UW_WIN));
	old = uk.focus;
	if (old != w) {
		uk.focus = w;
		if (old)
			uk_w_refresh(old);
		if (w)
			uk_w_refresh(w);
	}
	if (back)
		ui_window_use(back);
	return 0;
}

long ui_w_get_focus(void)
{
	return uk.focus;
}

/* ui_w_on : x1 = fonction (x0 widget, x1 code), x2 = donnée */
long ui_w_on(long w, long fn, long user)
{
	if (w) {
		STX(w, UW_FN, fn);
		STX(w, UW_USER, user);
	}
	return w;
}

long ui_w_user(long w)
{
	return w ? LDX(w, UW_USER) : 0;
}

long ui_w_get_value(long w)
{
	return w ? LDX(w, UW_VALUE) : 0;
}

/* uk_w_clamp : valeur bornée à [min, max - page] (feuille) */
long uk_w_clamp(long w)
{
	long lo = LDX(w, UW_MIN), hi = LDX(w, UW_MAX) - LDX(w, UW_PAGE), v = LDX(w, UW_VALUE);
	if (hi < lo)
		hi = lo;
	if (v < lo)
		v = lo;
	if (v > hi)
		v = hi;
	STX(w, UW_VALUE, v);
	return w;
}

/* ui_w_set_value : la change (bornée) et redessine, sans UN_CHANGE */
long ui_w_set_value(long w, long v)
{
	long t;
	if (!w)
		return 0;
	t = LDW(w, UW_TYPE);
	if (t == UT_LIST) { /* liste : choisit et montre l'élément */
		uk_ls_select(w, v);
		goto draw;
	}
	if (t == UT_CHECK || t == UT_TOOL || t == UT_SWITCH)
		goto boolv;
	if (t == UT_RADIO) { /* radio : choisi (les autres décochés) */
		if (!v)
			goto boolv;
		if (LDX(w, UW_VALUE))
			goto draw;
		uk_tg_toggle(w);
		return 0;
	}
	if (t == UT_DROP || t == UT_COMBO) { /* listes déroulantes : -1..n-1 */
		if (UGE(v, LDX(w, UW_NITEMS)))
			v = -1;
		STX(w, UW_VALUE, v);
		if (t != UT_COMBO || v < 0)
			goto draw;
		ui_w_set_text(LDX(w, UW_AUX2), uk_dd_current(w));
		goto draw;
	}
	if (t == UT_TREE) { /* arbre : l'id du noeud choisi */
		long r = uk_tr_index(w, v);
		if (r >= 0)
			uk_tr_select(w, r);
		return 0;
	}
	if (t == UT_SEGMENT || t == UT_TABS) { /* le choisi (page) */
		uk_seg_choose(w, v);
		return 0;
	}
	if (t == UT_TABSTRIP) {
		if (UGE(v, LDX(w, UW_NITEMS)))
			return 0;
		STX(w, UW_VALUE, v);
		goto draw;
	}
	if (t == UT_TEXT) { /* champ : le curseur (sans sélection) */
		long n = LDX(w, UW_TLEN);
		if (UGT(v, n))
			v = n;
		STX(w, UW_VALUE, v);
		STX(w, UW_ANCHOR, v);
		goto draw;
	}
	STX(w, UW_VALUE, v);
	if (t != UT_PALETTE && t != UT_COLOR) /* palette, pastille : sans bornes */
		uk_w_clamp(w);
	goto draw;
boolv:
	STX(w, UW_VALUE, v != 0);
draw:
	uk_w_refresh(w);
	return 0;
}

/* ui_w_set_range : x1 = min, x2 = max, x3 = page, x4 = pas (0 : inchangé) */
long ui_w_set_range(long w, long min, long max, long page, long step)
{
	if (!w)
		return 0;
	STX(w, UW_MIN, min);
	STX(w, UW_MAX, max);
	STX(w, UW_PAGE, page);
	if (step)
		STX(w, UW_STEP, step);
	uk_w_clamp(w);
	return uk_w_refresh(w);
}

/* uk_forget : la variable est mise à 0 si elle désigne le widget */
static void uk_forget(long *v, long w)
{
	if (*v == w)
		*v = 0;
}

/* ui_w_destroy : le retire de la fenêtre et rend sa mémoire (ses enfants aussi) */
long ui_w_destroy(long w)
{
	long k, prev, cols, n;
	if (!w)
		return 0;
again: /* enfants d'abord */
	for (k = uk.wfirst; k; k = LDX(k, UW_NEXT))
		if (LDX(k, UW_PARENT) == w) {
			ui_w_destroy(k);
			goto again;
		}
	uk_forget(&uk.focus, w); /* plus de références */
	uk_forget(&uk.capture, w);
	uk_forget(&uk.hover, w);
	uk_forget(&uk.click[0], w);
	uk_forget(&uk.held[0], w);
	for (k = uk.wins; k; k = LDX(k, WB_NEXT)) { /* (les autres fenêtres : leur bloc) */
		uk_forget((long *)(uintptr_t)(k + WB_FOCUS), w);
		uk_forget((long *)(uintptr_t)(k + WB_FOCUS + 8), w);
		uk_forget((long *)(uintptr_t)(k + WB_FOCUS + 16), w);
	}
	prev = 0; /* retire de la liste */
	for (k = uk.wfirst; k; prev = k, k = LDX(k, UW_NEXT))
		if (k == w) {
			if (prev)
				STX(prev, UW_NEXT, LDX(w, UW_NEXT));
			else
				uk.wfirst = LDX(w, UW_NEXT);
			if (uk.wlast == w)
				uk.wlast = prev;
			break;
		}
	if (UGE(LDW(w, UW_TYPE), UT_VIEW)) /* (une vue : ses pixels) */
		ui_image_free(LDX(w, UW_AUX));
	uk_free(LDX(w, UW_TEXT));
	cols = LDX(w, UW_COLS); /* titres des colonnes */
	n = LDX(w, UW_NCOLS);
	while (n) {
		n--;
		uk_free(LDX(cols + n * 16, 8));
	}
	uk_free(cols);
	uk_items_clear(w); /* éléments */
	uk_free(LDX(w, UW_ITEMS));
	uk_free(LDX(w, UW_TIP));
	uk_undo_clear(w);
	uk_free(LDX(w, UW_UNDO));
	uk_free(w);
	return 0;
}

/* ---------------------------------------------------------------------------
 * Événements
 * ------------------------------------------------------------------------- */

/* uk_w_mouse : appelle la fonction souris du widget -> code */
long uk_w_mouse(long w, long kind, long x, long y, long clicks)
{
	if (!uk_vt_has(w, VT_MOUSE))
		return 0;
	return vt_mouse(w, kind, x, y, clicks);
}

/* uk_focus_next : x0 = fenêtre, x1 = 1 vers l'arrière -> widget qui prend le
 * focus (suivant dans l'ordre de création, montré, actif, VTF_FOCUS) */
static long uk_focus_next(long win, long back)
{
	long f = uk.focus, before = 0, first = 0, after = 0, last = 0, passed = 0, w, r;
	for (w = uk.wfirst; w; w = LDX(w, UW_NEXT)) {
		if (w == f) {
			passed = 1;
			continue;
		}
		if (LDX(w, UW_WIN) != win)
			continue;
		if (!(uk_vt_flags(w) & VTF_FOCUS))
			continue;
		if (!uk_w_shown(w) || !uk_w_enabled(w))
			continue;
		if (!first)
			first = w;
		last = w;
		if (passed) {
			if (!after)
				after = w;
		} else
			before = w;
	}
	if (!back)
		r = after ? after : first; /* en avant : après, sinon le premier */
	else
		r = before ? before : last; /* en arrière : avant, sinon le dernier */
	if (r)
		ui_w_focus(r);
	return r;
}

static long uabs(long v)
{
	return v < 0 ? -v : v;
}

/* uk_w_event_in : x0 = événement de la fenêtre courante -> r0 widget,
 * r1 code UN_*, r2 1 si pris */
static ukr uk_w_event_in(long ev)
{
	ukr ret = {0, 0, 0, 0};
	long type = LDW(ev, EV_TYPE), kind, x, y, win, w = 0, code = 0, clicks, hv;

	if (type == WEV_MOUSE)
		goto mouse;
	if (type == WEV_KEY)
		goto key;
	if (type == WEV_WHEEL)
		goto wheel;
	if (type == WEV_FOCUS) {
		uk.winact = LDW(ev, EV_KIND); /* la fenêtre gagne / perd le focus */
		if (uk.focus)
			uk_w_refresh(uk.focus);
		goto none;
	}
	if (type == WEV_DROP) { /* glisser-déposer (uk_misc.c) */
		ukr d = uk_on_drop(ev);
		w = d.r0;
		if (!w)
			goto none;
		code = d.r1;
		goto notify2;
	}
	if (type != WEV_RESIZE)
		goto none;
	if (uk_win() != LDW(ev, EV_WIN)) /* nouvelle taille (uk_window.c) */
		goto none;
	uk_on_resize_ev(ev);
	w = 0;
	goto taken;

mouse:
	kind = LDW(ev, EV_KIND);
	x = LDSW(ev, EV_X);
	y = LDSW(ev, EV_Y);
	win = LDW(ev, EV_WIN);
	uk.mx[0] = x; /* (infobulle : le point, le temps ; une infobulle montrée s'en va) */
	uk.mx[1] = y;
	uk.mwin = win;
	uk.hover_t = SYS0(SYS_TIME_MS);
	uk_tip_hide();
	w = uk.held[0]; /* un appui attend : déplacement ou clic ? */
	if (w)
		goto held;
nothold:
	w = uk.capture;
	if (w)
		goto send;
	w = uk_w_at(x, y, win);
	hv = uk.hover; /* survol : sortie de l'ancien */
	if (hv != w) {
		uk.hover = w;
		ui_cursor(uk_w_shape(w)); /* la forme du curseur suit le widget */
		if (hv)
			uk_w_mouse(hv, 3, 0, 0, 0);
	}
	if (!w) {
		/* appui hors des widgets, en haut de la fenêtre : elle se déplace */
		if (kind != 1 || UGE(y, UK_DRAG_H))
			goto none;
		goto drag;
	}
	/* un widget sans souris (panneau, texte, barre d'outils) laisse faire comme le vide */
	if (kind == 1 && !UGE(y, UK_DRAG_H) && !uk_vt_has(w, VT_MOUSE))
		goto drag;
	goto widget;
drag:
	ui_window_drag();
	goto none;
widget:
	if (!uk_w_enabled(w))
		goto taken;
	if (kind != 1)
		goto send;
	/* appui en haut de la fenêtre, sur un bouton : il attend de voir si la
	 * souris bouge (la fenêtre) ou non (le clic) */
	if (UGE(y, UK_INTENT_H) || uk.replay || (LDW(ev, EV_CODE) & 2) || !(uk_vt_flags(w) & VTF_MOVE))
		goto press;
	uk.held[0] = w;
	uk.held[1] = x;
	uk.held[2] = y;
	uk.capture = w;
	goto none;
held:
	if (kind == 2)
		goto hrel;
	if (kind)
		goto none;
	/* bouge : la fenêtre part, le widget ne saura rien de cet appui */
	if (UGE(uabs(uk.held[1] - x), UK_INTENT_PX) || UGE(uabs(uk.held[2] - y), UK_INTENT_PX)) {
		uk.held[0] = 0;
		uk.capture = 0;
		uk.hover = 0;
		ui_window_drag();
	}
	goto none;
hrel: { /* relâché sans bouger : c'est un clic, l'appui passe, puis ce relâché */
		u8 e[32] __attribute__((aligned(8)));
		long hx = uk.held[1], hy = uk.held[2];
		ukr r;
		uk.held[0] = 0;
		uk.capture = 0;
		memcpy(e, (void *)(uintptr_t)ev, 32);
		STW(e, EV_KIND, 1);
		STW(e, EV_X, hx);
		STW(e, EV_Y, hy);
		uk.replay = 1;
		r = uk_w_event_in((long)(uintptr_t)e);
		uk.replay = 0;
		if (!r.r1)
			goto nothold;
		/* (onglets : choisis à l'appui, le relâché n'a plus rien à dire) */
		uk.capture = 0;
		uk.hover = 0;
		return r;
	}
press: /* appui : capture, focus, double-clic */
	uk.capture = w;
	if (uk_vt_flags(w) & VTF_FOCUS)
		ui_w_focus(w);
	{
		long t = SYS0(SYS_TIME_MS);
		clicks = 1;
		if (uk.click[0] == w && ULT(t - uk.click[1], UK_DBL_MS) && ULT(uabs(uk.click[2] - x), UK_DBL_PX) &&
		    ULT(uabs(uk.click[3] - y), UK_DBL_PX)) {
			clicks = 2;
			uk.click[0] = 0; /* (un troisième clic : simple) */
		} else {
			uk.click[0] = w;
			uk.click[1] = t;
			uk.click[2] = x;
			uk.click[3] = y;
		}
	}
	goto send2;
send:
	clicks = 1;
send2:
	if (kind == 2) { /* relâché : fin de la capture (le survol sera recalculé) */
		uk.capture = 0;
		uk.hover = 0;
	}
	uk.mbtn = LDW(ev, 16); /* boutons (uk_mbtn : bit 1 droit) */
	code = uk_w_mouse(w, kind, x - LDSW(w, UW_X), y - LDSW(w, UW_Y), clicks);
	goto notify2;

key: {
		long kc, ch, mods;
		uk_tip_hide();
		if (!LDW(ev, EV_KIND)) /* relâché */
			goto none;
		kc = LDW(ev, EV_CODE);
		ch = LDW(ev, EV_CHAR);
		mods = LDW(ev, EV_MODS);
		if (kc == KEY_TAB) {
			long f;
			if (mods & MOD_CTRL) /* (un widget qui garde Tab : sans Ctrl) */
				goto tabf;
			f = uk.focus;
			if (!f)
				goto tabf;
			if (LDX(f, UW_STYLE) & (1L << 10)) /* (UTA_FORMTAB : Tab passe au suivant) */
				goto tabf;
			if (uk_vt_flags(f) & VTF_TAB)
				goto kf;
tabf:
			w = uk_focus_next(LDW(ev, EV_WIN), (mods & 0x22) != 0); /* Maj : vers l'arrière */
			if (!w)
				goto none;
			goto taken;
		}
kf:
		w = uk.focus;
		if (!w)
			goto none;
		if (LDX(w, UW_WIN) != LDW(ev, EV_WIN))
			goto none;
		if (!uk_w_shown(w) || !uk_w_enabled(w))
			goto none;
		if (!uk_vt_has(w, VT_KEY))
			goto none;
		code = vt_key(w, kc, ch, mods);
		if (code < 0) /* touche laissée au programme */
			goto none;
		goto notify2;
	}

wheel: {
		long steps;
		uk_tip_hide();
		steps = LDSW(ev, EV_Y); /* crans */
		w = uk.hover;
		if (!w)
			w = uk.focus;
		for (;;) { /* le widget, ou celui qui le contient */
			if (!w)
				goto none;
			if (uk_vt_has(w, VT_WHEEL))
				break;
			w = LDX(w, UW_PARENT);
		}
		if (!uk_w_shown(w) || !uk_w_enabled(w))
			goto none;
		code = vt_wheel(w, steps);
		goto notify2;
	}

notify2:
	if (!code)
		goto taken;
	for (;;) { /* un enfant prévient son parent */
		long p = LDX(w, UW_PARENT);
		if (!p || !uk_vt_has(p, VT_CHILD))
			break;
		code = vt_child(p, w, code);
		w = p;
		if (!code)
			goto taken;
	}
	{
		long fn = LDX(w, UW_FN);
		if (fn)
			UK_CB2(fn, w, code);
	}
	ret.r0 = w;
	ret.r1 = code;
	ret.r2 = 1;
	return ret;
taken:
	ret.r0 = w;
	ret.r1 = 0;
	ret.r2 = 1;
	return ret;
none:
	return ret;
}

/* ui_w_event : x0 = événement (0 : ui_ev) -> r0 widget, r1 code UN_*, r2 1 si pris */
ukr ui_w_event(long ev)
{
	long prev;
	ukr r;
	if (!ev)
		ev = uk.vars + UV_EV;
	prev = uk_route(LDW(ev, EV_WIN)); /* la fenêtre de l'événement, le temps de le traiter */
	r = uk_w_event_in(ev);
	if (prev)
		ui_window_use(prev);
	return r;
}

/* ---------------------------------------------------------------------------
 * Barre de défilement (UT_SCROLL) : MIN..MAX = le contenu, PAGE = la partie
 * visible, VALUE = la position. Sans flèches, pouce arrondi (11.5).
 * ------------------------------------------------------------------------- */

long ui_scrollbar(long x, long y, long w, long h, long style)
{
	long b = uk_w_new(UT_SCROLL, x, y, w, h);
	if (!b)
		return 0;
	STX(b, UW_STYLE, style);
	STX(b, UW_PAGE, 1);
	STX(b, UW_MAX, 1);
	return b;
}

/* uk_sb_geom : -> r0 rail, r1 pouce, r2 son décalage, r3 1 si le contenu dépasse */
ukr uk_sb_geom(long w)
{
	ukr r;
	long rail = (LDX(w, UW_STYLE) & 1) ? LDW(w, UW_W) : LDW(w, UW_H);
	long mn = LDX(w, UW_MIN), span = LDX(w, UW_MAX) - mn, page = LDX(w, UW_PAGE);
	r.r1 = rail;
	r.r2 = 0;
	r.r3 = 0;
	if (span > page && page > 0) {
		long t = UDIV(rail * page, span); /* pouce = rail * page / étendue */
		if (t < 24)
			t = 24;
		if (t > rail)
			t = rail;
		r.r1 = t;
		r.r2 = UDIV((LDX(w, UW_VALUE) - mn) * (rail - t), span - page);
		r.r3 = 1;
	}
	r.r0 = rail;
	return r;
}

long uk_sb_draw(long w)
{
	ukr g;
	long x, y, level, col, bx, by, bw, bh, rad;
	if (!LDX(w, UW_PARENT)) /* seule : son fond (FACE) ; dans un widget : par-dessus */
		ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), JET(TJ_FACE));
	g = uk_sb_geom(w);
	if (!g.r3)
		return 0;
	x = LDW(w, UW_X); /* comme l'asm : ldp w (zéro-étendus) */
	y = LDW(w, UW_Y);
	if (LDW(w, UW_FLAGS) & UWF_HOVER) /* survol : le rail en CREUX */
		ui_rbox(x, y, LDW(w, UW_W), LDW(w, UW_H), 4, JET(TJ_SUNKEN), JET(TJ_SUNKEN));
	level = 128; /* pouce : LIGNE_FORTE, survol 100, appui 84 */
	if (LDW(w, UW_FLAGS) & UWF_HOVER)
		level = 100;
	if (LDW(w, UW_FLAGS) & UWF_PRESSED)
		level = 84;
	col = ui_tone(JET(TJ_LINE_STRONG), level);
	if (LDX(w, UW_STYLE) & 1) { /* horizontal */
		bx = x + g.r2;
		by = y + 1;
		bw = g.r1;
		bh = LDW(w, UW_H) - 2;
	} else { /* vertical */
		bx = x + 1;
		by = y + g.r2;
		bw = LDW(w, UW_W) - 2;
		bh = g.r1;
	}
	rad = LSR(ULT(bw, bh) ? bw : bh, 1);
	return ui_rbox(bx, by, bw, bh, rad, (u32)col, (u32)col);
}

/* souris : x1 genre, x2, x3 point local */
long uk_sb_mouse(long w, long kind, long x, long y, long clicks)
{
	long pos = (LDX(w, UW_STYLE) & 1) ? x : y; /* position le long du rail */
	long old = LDX(w, UW_VALUE), v;
	ukr g;
	(void)clicks;
	if (kind == 3) {
		STW(w, UW_FLAGS, LDW(w, UW_FLAGS) & ~(long)UWF_HOVER);
		goto redraw0;
	}
	if (kind == 2) {
		STX(w, UW_DRAG, 0);
		STW(w, UW_FLAGS, LDW(w, UW_FLAGS) & ~(long)UWF_PRESSED);
		goto redraw0;
	}
	if (kind == 1) {
		g = uk_sb_geom(w);
		if (!g.r3)
			return 0;
		if (pos < g.r2) {
			v = old - LDX(w, UW_PAGE);
			goto setv;
		}
		if (pos >= g.r2 + g.r1) {
			v = old + LDX(w, UW_PAGE);
			goto setv;
		}
		STX(w, UW_DRAG, pos - g.r2 + 1); /* pouce saisi */
		STW(w, UW_FLAGS, LDW(w, UW_FLAGS) | UWF_PRESSED);
		goto redraw0;
	}
	if (LDX(w, UW_DRAG)) { /* glisser */
		long top = pos - LDX(w, UW_DRAG) + 1, course, vr, mn;
		g = uk_sb_geom(w);
		course = g.r0 - g.r1;
		if (course <= 0)
			return 0;
		if (top < 0)
			top = 0;
		if (top > course)
			top = course;
		mn = LDX(w, UW_MIN);
		vr = LDX(w, UW_MAX) - mn - LDX(w, UW_PAGE);
		v = UDIV(top * vr + LSR(course, 1), course) + mn;
		goto setv;
	}
	if (LDW(w, UW_FLAGS) & UWF_HOVER)
		return 0;
	STW(w, UW_FLAGS, LDW(w, UW_FLAGS) | UWF_HOVER);
	goto redraw0;
setv:
	STX(w, UW_VALUE, v);
	uk_w_clamp(w);
	if (LDX(w, UW_VALUE) == old)
		goto redraw0;
	uk_w_refresh(w);
	return UN_CHANGE;
redraw0:
	uk_w_refresh(w);
	return 0;
}

/* x1 = crans (> 0 : vers le haut) : 3 pas par cran */
long uk_sb_wheel(long w, long steps)
{
	long old = LDX(w, UW_VALUE);
	STX(w, UW_VALUE, old - steps * (LDX(w, UW_STEP) * 3));
	uk_w_clamp(w);
	if (LDX(w, UW_VALUE) == old)
		return 0;
	uk_w_refresh(w);
	return UN_CHANGE;
}
