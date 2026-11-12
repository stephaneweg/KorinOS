/* uk_window.c -- UIKit : uikit_window.inc (les fenêtres du programme, fenêtre
 * redimensionnable, contrôles adaptatifs, séparateur de volets). Port ligne à
 * ligne de l'assembleur (src/kits/uikit/PORT.md). */
#include "uk.h"

/* uk_wb_xfer (uk_wb_tab de l'asm) : x0 = bloc, x1 = 0 : l'état de la fenêtre
 * courante y va ; 1 : il en vient (feuille) */
static void xf(long *v, long blk, long off, long dir)
{
	if (dir)
		*v = LDX(blk, off);
	else
		STX(blk, off, *v);
}

static void uk_wb_xfer(long blk, long dir)
{
	long i;
	if (dir) {
		STX(uk.vars, UV_WIN, LDX(blk, WB_VARS));
		STX(uk.vars, UV_SURF, LDX(blk, WB_VARS + 8));
		STX(uk.vars, UV_W, LDX(blk, WB_VARS + 16));
		STX(uk.vars, UV_H, LDX(blk, WB_VARS + 24));
	} else {
		STX(blk, WB_VARS, LDX(uk.vars, UV_WIN));
		STX(blk, WB_VARS + 8, LDX(uk.vars, UV_SURF));
		STX(blk, WB_VARS + 16, LDX(uk.vars, UV_W));
		STX(blk, WB_VARS + 24, LDX(uk.vars, UV_H));
	}
	/* uk_dmg .. uk_flags (56 octets) */
	xf(&uk.dmg, blk, WB_DMG, dir);
	xf(&uk.buf, blk, WB_DMG + 8, dir);
	xf(&uk.chr_a, blk, WB_DMG + 16, dir);
	xf(&uk.chr_i, blk, WB_DMG + 24, dir);
	xf(&uk.ow, blk, WB_DMG + 32, dir);
	xf(&uk.oh, blk, WB_DMG + 40, dir);
	xf(&uk.flags, blk, WB_DMG + 48, dir);
	/* uk_focus, uk_capture, uk_hover (24) */
	xf(&uk.focus, blk, WB_FOCUS, dir);
	xf(&uk.capture, blk, WB_FOCUS + 8, dir);
	xf(&uk.hover, blk, WB_FOCUS + 16, dir);
	/* uk_winact, uk_minw, uk_minh, uk_resize_fn (32) */
	xf(&uk.winact, blk, WB_ACT, dir);
	xf(&uk.minw, blk, WB_ACT + 8, dir);
	xf(&uk.minh, blk, WB_ACT + 16, dir);
	xf(&uk.resize_fn, blk, WB_ACT + 24, dir);
	xf(&uk.cur_shape, blk, WB_SHAPE, dir);
	for (i = 0; i < 4; i++) /* uk_clip (32) */
		xf(&uk.clip[i], blk, WB_CLIP + i * 8, dir);
	xf(&uk.clip_on, blk, WB_CLIP_ON, dir);
	xf(&uk.menu_src, blk, WB_MENU, dir);
	xf(&uk.menu_p, blk, WB_MENU_P, dir);
	xf(&uk.win_dlg, blk, WB_DLG, dir);
	for (i = 0; i < 128; i += 8) { /* uk_title (128) */
		if (dir)
			STX(uk.title, i, LDX(blk, WB_TITLE + i));
		else
			STX(blk, WB_TITLE + i, LDX(uk.title, i));
	}
	xf(&uk.dsg_w, blk, WB_DSG, dir); /* uk_dsg_w, uk_dsg_h (16) */
	xf(&uk.dsg_h, blk, WB_DSG + 8, dir);
}

/* uk_wb_find : x0 = poignée -> son bloc (0 : aucun) (feuille) */
long uk_wb_find(long id)
{
	long b;
	for (b = uk.wins; b; b = LDX(b, WB_NEXT))
		if (LDX(b, WB_ID) == id)
			return b;
	return 0;
}

/* uk_wb_new : bloc d'une fenêtre de plus, devenue la courante (0 : plus de
 * mémoire). La première garde les variables telles qu'elles sont ; une autre
 * part de zéro. */
long uk_wb_new(void)
{
	long b = UKA(win0);
	if (LDX(b, WB_ID)) {
		b = uk_alloc(WB_SIZE);
		if (!b)
			return 0;
	}
	if (uk.cur) {
		uk_wb_xfer(uk.cur, 0); /* la fenêtre d'avant va dans son bloc */
		uk.focus = 0;
		uk.capture = 0;
		uk.hover = 0;
		uk.winact = 1;
		uk.minw = 120;
		uk.minh = 80;
		uk.resize_fn = 0;
		uk.cur_shape = -1;
		uk.clip_on = 0;
		uk.clip_sp = 0;
		uk.menu_src = 0;
		uk.menu_p = -1;
		uk.win_dlg = 0;
	}
	STX(b, WB_NEXT, uk.wins);
	uk.wins = b;
	uk.cur = b;
	return b;
}

/* uk_wb_drop : (fenêtre courante fermée) retire son bloc ; la dernière ouverte
 * des autres devient la courante */
long uk_wb_drop(void)
{
	long cur = uk.cur, *pp;
	if (!cur)
		return 0;
	for (pp = &uk.wins; *pp; pp = (long *)(uintptr_t)(*pp + WB_NEXT)) /* hors de la liste */
		if (*pp == cur) {
			*pp = LDX(cur, WB_NEXT);
			break;
		}
	if (UGT(uk.menu_p + 1, 1)) /* (mémoire des menus : ni 0, ni -1) */
		uk_free(uk.menu_p);
	STX(cur, WB_ID, 0);
	if (cur != UKA(win0))
		uk_free(cur);
	uk.cur = uk.wins;
	if (uk.cur) {
		uk_wb_xfer(uk.cur, 1);
		return 0;
	}
	uk.dmg = 0; /* plus de fenêtre */
	uk.menu_p = 0;
	return 0;
}

/* ui_window_drag : bouton tenu depuis un appui dans le contenu : la fenêtre suit */
long ui_window_drag(void)
{
	STX(uk.msg, WM_A, uk_win());
	return uk_call(WS_OP_WIN_DRAG);
}

/* ui_window_use : x0 = fenêtre -> la courante d'avant (0 : aucune, ou poignée
 * inconnue : rien n'a changé) */
long ui_window_use(long id)
{
	long b = uk_wb_find(id), prev = 0, c;
	if (!b)
		return 0;
	c = uk.cur;
	if (c) {
		prev = LDX(c, WB_ID);
		if (c == b)
			return prev;
		uk_wb_xfer(c, 0);
	}
	uk_wb_xfer(b, 1);
	uk.cur = b;
	return prev;
}

/* uk_route : x0 = poignée -> fenêtre à rendre après (0 : déjà la courante, ou
 * fenêtre sans bloc) */
long uk_route(long win)
{
	if (uk_win() == win)
		return 0;
	return ui_window_use(win);
}

/* ui_window_close : x0 = fenêtre (0 : la courante) -> 0 (E_BADF : inconnue) */
long ui_window_close(long id)
{
	long prev;
	if (!id)
		id = uk_win();
	if (!uk_wb_find(id))
		return E_BADF;
	prev = uk_route(id);
	uk_tip_hide();
	uk_w_destroy_win(id);
	ui_close();
	if (prev)
		ui_window_use(prev);
	return 0;
}

/* ui_window_count : fenêtres ouvertes du programme (feuille) */
long ui_window_count(void)
{
	long n = 0, b;
	for (b = uk.wins; b; b = LDX(b, WB_NEXT))
		n++;
	return n;
}

long ui_w_window(long w)
{
	return w ? LDX(w, UW_WIN) : 0;
}

/* uk_w_destroy_win : détruit les widgets de la fenêtre (avec leurs enfants) */
long uk_w_destroy_win(long win)
{
	long w;
again:
	for (w = uk.wfirst; w; w = LDX(w, UW_NEXT))
		if (LDX(w, UW_WIN) == win && !LDX(w, UW_PARENT)) { /* (un enfant part avec son parent) */
			ui_w_destroy(w);
			goto again;
		}
	return 0;
}

/* ui_resize : x0, x1 = taille (bornée à la taille minimale) -> 0 / erreur */
long ui_resize(long w, long h)
{
	long r, surf, b, c;
	if (w < uk.minw)
		w = uk.minw;
	if (h < uk.minh)
		h = uk.minh;
	STX(uk.msg, WM_A, uk_win());
	STX(uk.msg, WM_B, w);
	STX(uk.msg, WM_C, h);
	r = uk_call(WS_OP_RESIZE);
	if (r < 0)
		return r;
	SYS1(SYS_BUF_FREE, uk.buf); /* l'ancien tampon rendu, le nouveau mappé */
	uk.buf = LDX(uk.msg, WM_A);
	surf = SYS1(SYS_BUF_MAP, uk.buf);
	if (surf < 0)
		return surf;
	STX(uk.vars, UV_SURF, surf);
	STX(uk.vars, UV_W, w);
	STX(uk.vars, UV_H, h);
	uk.dmg = surf + LDX(uk.msg, WM_D); /* en-tête WD_* */
	b = LDX(uk.msg, WM_B); /* cadres */
	c = LDX(uk.msg, WM_C);
	if (b) {
		b += surf;
		c += surf;
	} /* comme l'asm : sans cadre actif, uk_chr_i garde le décalage brut */
	uk.chr_a = b;
	uk.chr_i = c;
	uk.ow = w + 2 * FRAME_BORDER;
	uk.oh = h + FRAME_TITLE_H + FRAME_BORDER;
	uk.clip_on = 0; /* (plus de découpe) */
	ui_fill(0, 0, w, h, JET(TJ_FACE));
	uk_draw_chrome();
	return 0;
}

/* ui_maximize : comme le bouton agrandir ; WEV_RESIZE suit */
long ui_maximize(void)
{
	STX(uk.msg, WM_A, uk_win());
	return uk_call(WS_OP_MAXIMIZE);
}

long ui_min_size(long w, long h)
{
	uk.minw = w;
	uk.minh = h;
	return w;
}

long ui_on_resize(long fn)
{
	uk.resize_fn = fn;
	return fn;
}

/* uk_on_resize_ev : (ui_w_event) événement WEV_RESIZE de la fenêtre courante */
long uk_on_resize_ev(long ev)
{
	long fn;
	if (ui_resize(LDW(ev, EV_X), LDW(ev, EV_Y)) < 0)
		return 0;
	uk_adapt(LDX(uk.vars, UV_W), LDX(uk.vars, UV_H)); /* widgets ancrés, fenêtre fixe */
	fn = uk.resize_fn;
	if (fn)
		return UK_CB2(fn, LDX(uk.vars, UV_W), LDX(uk.vars, UV_H));
	ui_w_draw(0);
	return ui_update_all();
}

/* ui_anchor : x1 = UA_* (0 : placement automatique) -> 0 */
long ui_anchor(long w, long a)
{
	if (w)
		STW(w, UW_LAY, (LDW(w, UW_LAY) & ~31L) | (a & 31));
	return 0;
}

/* ui_design_size : taille de conception de la fenêtre courante ; les places
 * de ses widgets deviennent leurs rectangles de conception -> 0 */
long ui_design_size(long w, long h)
{
	long win, k;
	uk.dsg_w = w;
	uk.dsg_h = h;
	win = uk_win();
	for (k = uk.wfirst; k; k = LDX(k, UW_NEXT))
		if (LDX(k, UW_WIN) == win)
			uk_des_note(k, 1);
	return 0;
}

/* uk_des_note : x1 = 1 : toujours ; 0 : si sa fenêtre (la courante) a sa
 * taille de conception -> sa place devient son rectangle de conception */
long uk_des_note(long w, long always)
{
	if (!always) {
		if (uk_win() != LDX(w, UW_WIN))
			return 0;
		if (uk.dsg_w) {
			if (LDX(uk.vars, UV_W) != uk.dsg_w)
				return 0;
			if (LDX(uk.vars, UV_H) != uk.dsg_h)
				return 0;
		}
	}
	STH(w, UW_DES, LDW(w, UW_X));
	STH(w, UW_DES + 2, LDW(w, UW_Y));
	STH(w, UW_DES + 4, LDW(w, UW_W));
	STH(w, UW_DES + 6, LDW(w, UW_H));
	STW(w, UW_LAY, LDW(w, UW_LAY) | UL_DES);
	return 0;
}

/* uk_axis : un axe (feuille) : position, taille de conception, taille de
 * conception de la fenêtre, sa taille, mode (0 étire, 1 garde la taille et
 * centre, 2 ancre début, 3 ancre fin, 4 les deux), taille minimale (mode 1)
 * -> r0 position, r1 taille */
static ukr uk_axis(long pos, long size, long dw, long ww, long mode, long minsz)
{
	ukr r = {pos, size, 0, 0};
	if (mode == 1) { /* le centre suit */
		long c = SDIV((LSL(pos, 1) + size) * ww, LSL(dw, 1));
		if (size < minsz)
			size = minsz;
		r.r0 = c - LSR(size, 1);
		r.r1 = size;
		return r;
	}
	if (UGT(mode, 1)) {
		long d = ww - dw;
		if (ULT(mode, 3)) /* début : rien ne change */
			return r;
		if (mode == 3) { /* fin : distance au bord gardée */
			r.r0 = pos + d;
			return r;
		}
		size += d; /* les deux : étire */
		if (size < 1)
			size = 1;
		r.r1 = size;
		return r;
	}
	{ /* étire : les deux bords suivent */
		long a = SDIV(pos * ww, dw), b = SDIV((pos + size) * ww, dw);
		r.r0 = a;
		r.r1 = b - a;
	}
	return r;
}

/* uk_amode : ancre de début, de fin (0 / 1) -> mode de uk_axis */
static long uk_amode(long start, long end)
{
	static const u8 t[4] = {1, 2, 3, 4};
	return t[start + end * 2];
}

/* uk_adapt : x0, x1 = nouvelle taille : les widgets de la fenêtre courante
 * replacés depuis leurs rectangles de conception */
long uk_adapt(long ww, long wh)
{
	long dw = uk.dsg_w, dh = uk.dsg_h, autom = 0, touch = 0, win, w;
	if (!dw || !dh)
		return 0;
	if (!(uk.flags & (WF_RESIZABLE | WF_MODAL)) && !uk.resize_fn)
		autom = 1;
	/* densité tactile, et la place s'agrandit assez (9/8) : hauteur d'un contrôle */
	if (LDX(uk.tv, UK_TV_DENSITY) == 1 && !ULT(LSL(wh, 3), dh + LSL(dh, 3)))
		touch = LDX(uk_met(), MT_CTL);
	win = uk_win();
	for (w = uk.wfirst; w; w = LDX(w, UW_NEXT)) {
		long lay, p, keeph, mode, minsz, x1, x2;
		ukr rx, ry;
		if (LDX(w, UW_WIN) != win)
			continue;
		lay = LDW(w, UW_LAY);
		if (!(lay & UL_DES))
			continue;
		if (lay & UA_FIXED)
			continue;
		p = LDX(w, UW_PARENT); /* un parent qui place ses enfants */
		if (p && uk_vt_has(p, VT_LAYOUT))
			continue;
		if (lay & 15) { /* ancres */
			mode = uk_amode(lay & 1, (lay >> 2) & 1);
			rx = uk_axis(LDSH(w, UW_DES), LDSH(w, UW_DES + 4), dw, ww, mode, 0);
			mode = uk_amode((lay >> 1) & 1, (lay >> 3) & 1);
			ry = uk_axis(LDSH(w, UW_DES + 2), LDSH(w, UW_DES + 6), dh, wh, mode, 0);
			goto move;
		}
		if (!autom)
			continue;
		keeph = 0; /* automatique : hauteur fixe ? */
		if (ULT(LDW(w, UW_TYPE), 32))
			keeph = LSRW(UK_KEEPH, LDW(w, UW_TYPE)) & 1;
		/* x : garde la largeur si <= 24, ou si carré à hauteur fixe */
		x1 = LDSH(w, UW_DES + 4);
		x2 = LDSH(w, UW_DES + 6);
		mode = 1;
		if (x1 > 24) {
			mode = 0;
			if (keeph)
				mode = x1 <= x2 + LSR(x2, 2);
		}
		rx = uk_axis(LDSH(w, UW_DES), x1, dw, ww, mode, 0);
		/* y : garde la hauteur si <= 24, ou si hauteur fixe <= 64 (tactile : 36) */
		x1 = LDSH(w, UW_DES + 6);
		mode = 1;
		minsz = 0;
		if (x1 > 24) {
			mode = 0;
			if (keeph && x1 <= 64) {
				mode = 1;
				minsz = touch;
			}
		}
		ry = uk_axis(LDSH(w, UW_DES + 2), x1, dh, wh, mode, minsz);
move:
		ui_w_move(w, rx.r0, ry.r0, rx.r1, ry.r1);
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * Séparateur de volets (UT_SPLIT) : valeur = sa position (x ou y)
 * ------------------------------------------------------------------------- */

/* ui_splitter : x0..x3 = poignée, x4 = min, x5 = max -> poignée */
long ui_splitter(long x, long y, long w, long h, long min, long max)
{
	long b = uk_w_new(UT_SPLIT, x, y, w, h);
	if (!b)
		return 0;
	STX(b, UW_MIN, min);
	STX(b, UW_MAX, max);
	STX(b, UW_VALUE, x);
	if (!((s32)LDW(b, UW_H) > (s32)LDW(b, UW_W))) /* horizontale : y */
		STX(b, UW_VALUE, y);
	return b;
}

long uk_sp_draw(long w)
{
	long x = LDSW(w, UW_X), y = LDSW(w, UW_Y), vert, ww, hh, px, py, i;
	/* fond ; survol, glisser : CREUX (comme l'asm : ldp w, zéro-étendus) */
	ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H),
		(LDW(w, UW_FLAGS) & (UWF_HOVER | UWF_PRESSED)) ? JET(TJ_SUNKEN) : JET(TJ_FACE));
	vert = uk_vertical(w);
	ww = LDW(w, UW_W);
	hh = LDW(w, UW_H);
	if (vert) { /* trait LIGNE au milieu, trois points */
		ui_fill(x + LSR(ww, 1), y, 1, hh, JET(TJ_LINE));
		px = x + LSR(LDW(w, UW_W), 1) - 1;
		py = y + LSR(LDW(w, UW_H), 1) - 9;
	} else {
		ui_fill(x, y + LSR(hh, 1), ww, 1, JET(TJ_LINE));
		px = x + LSR(LDW(w, UW_W), 1) - 9;
		py = y + LSR(LDW(w, UW_H), 1) - 1;
	}
	for (i = 0; i < 3; i++) {
		ui_fill(px, py, 3, 3, JET(TJ_TEXT3));
		if (vert)
			py += 7;
		else
			px += 7;
	}
	return 0;
}

/* souris : survol ; appui, glisser -> nouvelle position */
long uk_sp_mouse(long w, long kind, long x, long y, long clicks)
{
	long f = LDW(w, UW_FLAGS), p;
	(void)clicks;
	if (kind == 3) {
		STW(w, UW_FLAGS, f & ~(long)(UWF_HOVER | UWF_PRESSED));
		goto redraw;
	}
	if (kind == 2) {
		STW(w, UW_FLAGS, f & ~(long)UWF_PRESSED);
		goto redraw;
	}
	if (kind == 1) { /* prise : décalage dans la poignée */
		STW(w, UW_FLAGS, f | UWF_PRESSED);
		STX(w, UW_DRAG, x);
		STX(w, UW_DRAG2, y);
		goto redraw;
	}
	if (!(f & UWF_PRESSED)) {
		long nf = f | UWF_HOVER;
		if (nf == f)
			return 0;
		STW(w, UW_FLAGS, nf);
		goto redraw;
	}
	if (uk_vertical(w))
		p = LDSW(w, UW_X) + x - LDX(w, UW_DRAG); /* nouvelle position en x */
	else
		p = LDSW(w, UW_Y) + y - LDX(w, UW_DRAG2);
	if (p < LDX(w, UW_MIN))
		p = LDX(w, UW_MIN);
	if (p > LDX(w, UW_MAX))
		p = LDX(w, UW_MAX);
	if (p == LDX(w, UW_VALUE))
		return 0;
	STX(w, UW_VALUE, p);
	if (uk_vertical(w))
		STW(w, UW_X, LDX(w, UW_VALUE));
	else
		STW(w, UW_Y, LDX(w, UW_VALUE));
	return UN_CHANGE; /* (le programme redessine) */
redraw:
	uk_w_refresh(w);
	return 0;
}
