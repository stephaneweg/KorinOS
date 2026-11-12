/* uk_tree.c -- UIKit : l'arborescence (UT_TREE, Onyx treeview.h). Port ligne
 * à ligne de uikit_tree.inc (src/kits/uikit/PORT.md).
 *
 * Noeuds rangés en ordre préfixe : les éléments (texte, donnée) et, en
 * parallèle (UW_COLS), 16 octets par noeud : id (stable), id du parent (-1 :
 * racine), profondeur, drapeaux (bit 0 déplié, bit 1 a des enfants).
 * ui_tree_add rend l'id ; la valeur est l'id choisi (-1 : aucun). Clic sur
 * le triangle ou double-clic : déplier / replier ; double-clic sur une
 * feuille, Entrée : UN_ACTIVATE ; haut / bas ; droite déplie, gauche replie
 * ou remonte au parent ; molette ; barre de défilement (enfant).
 * Champs : MIN prochain id, ANCHOR capacité du tableau des noeuds, POS
 * première ligne montrée, HOT ligne survolée, AUX la barre. */
#include "uk.h"

#define TN_ID 0
#define TN_PARENT 4
#define TN_DEPTH 8
#define TN_FLAGS 12
#define UK_TREE_IND 18 /* retrait par niveau */

static const char uk_s_tri_r[] = "\xE2\x96\xB8"; /* « ▸ » */

static long tr_sync(long t);

/* x0..x3 = rectangle -> poignée */
long ui_tree(long a0, long a1, long a2, long a3)
{
	long t = uk_w_new(UT_TREE, a0, a1, a2, a3);
	if (!t)
		return 0;
	STX(t, UW_VALUE, -1);
	STX(t, UW_HOT, -1);
	long sb = ui_scrollbar(0, 0, LDX(uk_met(), MT_SB), 1, 0);
	if (sb) {
		STX(sb, UW_PARENT, t);
		STX(t, UW_AUX, sb);
	}
	uk_tr_layout(t);
	return t;
}

/* uk_tr_index : id -> rang (-1 : inconnu) */
long uk_tr_index(long t, long id)
{
	long nodes = LDX(t, UW_COLS);
	long n = LDX(t, UW_NITEMS);
	for (long i = 0; ULT(i, n); i++)
		if (LDSW(nodes + (i << 4), TN_ID) == id)
			return i;
	return -1;
}

/* uk_tr_node : rang -> noeud (adresse) */
static long tr_node(long t, long rank)
{
	return LDX(t, UW_COLS) + (rank << 4);
}

/* uk_tr_next : rang -> rang visible suivant (ses descendants sautés s'il est
 * replié ; nombre de noeuds à la fin) */
static long tr_next(long t, long rank)
{
	long nodes = LDX(t, UW_COLS);
	long n = LDX(t, UW_NITEMS);
	long nd = nodes + (rank << 4);
	u32 fl = (u32)LDW(nd, TN_FLAGS);
	u32 depth = (u32)LDW(nd, TN_DEPTH);
	long r = rank + 1;
	if (fl & 1) /* déplié : le suivant */
		return r;
	while (ULT(r, n)) {
		if ((u32)LDW(nodes + (r << 4), TN_DEPTH) <= depth)
			break;
		r++;
	}
	return r;
}

/* uk_tr_row : ligne visible -> rang (-1 : au-delà) ; ligne -1 -> nombre de
 * lignes visibles */
static long tr_row(long t, long line)
{
	long rank = 0, l = 0;
	while (ULT(rank, LDX(t, UW_NITEMS))) {
		if (l == line)
			return rank;
		rank = tr_next(t, rank);
		l++;
	}
	if (line < 0)
		return l; /* comme l'asm : toute ligne négative (pas seulement -1) */
	return -1;
}

/* uk_tr_line_of : rang -> sa ligne visible (-1 : caché sous un parent replié) */
static long tr_line_of(long t, long rank)
{
	long r = 0, l = 0;
	while (ULT(r, LDX(t, UW_NITEMS))) {
		if (r == rank)
			return l;
		if (UGT(r, rank))
			return -1;
		r = tr_next(t, r);
		l++;
	}
	return -1;
}

/* uk_tr_layout : barre de défilement placée, plage = lignes visibles */
long uk_tr_layout(long t)
{
	long sb = LDX(t, UW_AUX);
	if (sb) {
		long w3 = LDX(uk_met(), MT_SB);
		long x = LDW(t, UW_X) + LDW(t, UW_W) - w3 - 2;
		long y = LDW(t, UW_Y) + 1;
		long h = LDW(t, UW_H) - 2;
		STW(sb, UW_X, x);
		STW(sb, UW_Y, y);
		STW(sb, UW_W, w3);
		STW(sb, UW_H, h);
	}
	return tr_sync(t);
}

/* uk_tr_sync */
static long tr_sync(long t)
{
	long vis = tr_row(t, -1); /* lignes visibles */
	long rows = uk_ls_rows(t);
	long max = vis - rows;
	if (max < 0)
		max = 0;
	long pos = LDX(t, UW_POS);
	if (pos > max)
		pos = max;
	if (pos < 0)
		pos = 0;
	STX(t, UW_POS, pos);
	long sb = LDX(t, UW_AUX);
	if (!sb)
		return rows;
	STX(sb, UW_MIN, 0);
	STX(sb, UW_MAX, vis);
	STX(sb, UW_PAGE, rows);
	STX(sb, UW_VALUE, pos);
	return rows;
}

/* x0 = arbre, x1 = id du parent (-1 : racine), x2 = libellé, x3 = donnée
 * -> x0 = id du noeud (-1 : échec) */
long ui_tree_add(long a0, long a1, long a2, long a3)
{
	long t = a0, parent = a1, label = a2, data = a3;
	if (!t)
		return -1;
	/* place : après les descendants du parent */
	if (!uk_items_grow(t))
		return -1;
	if (ULT(LDX(t, UW_ANCHOR), LDX(t, UW_ICAP))) {
		/* tableau des noeuds à la même capacité */
		long p = uk_alloc(LDX(t, UW_ICAP) << 4);
		if (!p)
			return -1;
		uk_memcpy(p, LDX(t, UW_COLS), LDX(t, UW_NITEMS) << 4);
		uk_free(LDX(t, UW_COLS));
		STX(t, UW_COLS, p);
		STX(t, UW_ANCHOR, LDX(t, UW_ICAP));
	}
	long ins = LDX(t, UW_NITEMS); /* rang d'insertion (à la fin : racine) */
	long depth = 0;
	if (parent >= 0) {
		long pr = uk_tr_index(t, parent);
		if (pr < 0)
			return -1;
		long nodes = LDX(t, UW_COLS);
		long nd = nodes + (pr << 4);
		STW(nd, TN_FLAGS, (u32)LDW(nd, TN_FLAGS) | 2); /* le parent a des enfants */
		depth = LDW(nd, TN_DEPTH);
		ins = pr + 1;
		while (ULT(ins, LDX(t, UW_NITEMS))) {
			if ((u32)LDW(nodes + (ins << 4), TN_DEPTH) <= (u32)depth)
				break;
			ins++;
		}
		depth += 1;
	}
	long s = uk_strdup(label);
	if (!s)
		return -1;
	/* les suivants montent d'un rang */
	long n = (LDX(t, UW_NITEMS) - ins) << 4;
	long src = LDX(t, UW_ITEMS) + (ins << 4);
	uk_memcpy_up(src + 16, src, n);
	src = LDX(t, UW_COLS) + (ins << 4);
	uk_memcpy_up(src + 16, src, n);
	long it = LDX(t, UW_ITEMS) + (ins << 4);
	STX(it, 0, s);
	STX(it, 8, data);
	long nd = LDX(t, UW_COLS) + (ins << 4);
	long id = LDX(t, UW_MIN); /* id neuf */
	STX(t, UW_MIN, id + 1);
	STW(nd, TN_ID, id);
	STW(nd, TN_PARENT, parent);
	STW(nd, TN_DEPTH, depth);
	STW(nd, TN_FLAGS, 0);
	STX(t, UW_NITEMS, LDX(t, UW_NITEMS) + 1);
	tr_sync(t);
	return id;
}

/* x0 = arbre, x1 = id, x2 = 1 déplie / 0 replie */
long ui_tree_expand(long a0, long a1, long a2)
{
	long t = a0;
	if (!t)
		return 0;
	long r = uk_tr_index(t, a1);
	if (r < 0)
		return r;
	long nd = tr_node(t, r);
	u32 f = (u32)LDW(nd, TN_FLAGS) & ~1u;
	f |= (a2 != 0);
	STW(nd, TN_FLAGS, f);
	tr_sync(t);
	return uk_w_refresh(t);
}

/* x0 = arbre, x1 = id -> x0 = libellé (0 : inconnu), x1 = donnée */
ukr ui_tree_get(long a0, long a1)
{
	long r = uk_tr_index(a0, a1);
	if (r < 0) {
		ukr z = {0, 0, 0, 0};
		return z;
	}
	return ui_list_get(a0, r);
}

/* x0 = arbre : plus de noeud.
 * comme l'asm : un arbre nul (0) est lu par uk_tr_sync (faute). */
long ui_tree_clear(long a0)
{
	ui_list_clear(a0);
	return tr_sync(a0);
}

/* uk_tr_toggle : rang : déplie / replie -> 0 */
static long tr_toggle(long t, long rank)
{
	long nd = tr_node(t, rank);
	STW(nd, TN_FLAGS, (u32)LDW(nd, TN_FLAGS) ^ 1);
	tr_sync(t);
	uk_w_refresh(t);
	return 0;
}

/* uk_tr_select : rang : choisi et rendu visible -> UN_CHANGE (0 : déjà) */
long uk_tr_select(long t, long rank)
{
	long id = LDSW(tr_node(t, rank), TN_ID);
	long old = LDX(t, UW_VALUE);
	STX(t, UW_VALUE, id);
	long changed = id != old;
	/* visible : ligne entre POS et POS + lignes */
	long line = tr_line_of(t, uk_tr_index(t, LDX(t, UW_VALUE)));
	if (line >= 0) {
		long rows = uk_ls_rows(t);
		long pos = LDX(t, UW_POS);
		if (line < pos) {
			STX(t, UW_POS, line);
		} else if (line >= pos + rows) {
			STX(t, UW_POS, line - rows + 1);
		}
		tr_sync(t);
	}
	uk_w_refresh(t);
	return changed ? UN_CHANGE : 0;
}

/* uk_tr_draw */
long uk_tr_draw(long t)
{
	long x = SXTW(LDW(t, UW_X));
	long y0 = SXTW(LDW(t, UW_Y));
	long wd = LDW(t, UW_W);
	long ht = LDW(t, UW_H);
	ui_fill(x, y0, wd, ht, JET(TJ_SURFACE));
	long row = LDX(uk_met(), MT_ROW);
	long foc = uk_w_focused(t);
	long line = LDX(t, UW_POS); /* première ligne */
	long rank = tr_row(t, line);
	long y = y0;
	for (;;) {
		if (rank < 0)
			break;
		if (UGE(rank, LDX(t, UW_NITEMS)))
			break;
		if (y + row > y0 + ht)
			break;
		/* fond : choisi, survolé */
		long id = LDSW(tr_node(t, rank), TN_ID);
		long ink = JET(TJ_TEXT1);
		long c = 0;
		int box = 0;
		if (id == LDX(t, UW_VALUE)) {
			c = JET(TJ_ACCENT_LIGHT);
			if (foc) {
				c = JET(TJ_ACCENT);
				ink = JET(TJ_ACCENT_INK);
			}
			box = 1;
		} else if (line == LDX(t, UW_HOT)) {
			c = JET(TJ_SUNKEN);
			box = 1;
		}
		if (box)
			ui_rbox(x + 6, y, wd - 24, row, 5, c, c);
		/* triangle, libellé */
		long nd = tr_node(t, rank);
		long tx = LDW(nd, TN_DEPTH) * UK_TREE_IND + x + 10; /* x du triangle */
		u32 fl = (u32)LDW(nd, TN_FLAGS);
		if (fl & 2) {
			long s = S(uk_s_tri_r); /* « ▸ » replié, « ▾ » déplié */
			if (fl & 1)
				s = S(uk_s_chev);
			uk_text_box(s, tx, y, row, ink, uk_font_r(), 0, UK_PX);
		}
		long label = LDX(LDX(t, UW_ITEMS) + (rank << 4), 0);
		long lx = tx + 16;
		long px = LDX(uk_met(), MT_PX);
		long font = uk_font_r();
		uk_text_box(label, lx, y, row, ink, font, x + wd - lx - 24, px);
		rank = tr_next(t, rank);
		y += row;
		line++;
	}
	return 0;
}

/* uk_tr_mouse : triangle (déplier), clic (choisir), double-clic, survol */
long uk_tr_mouse(long t, long kind, long x, long y, long clicks)
{
	long line = -1; /* ligne sous le point */
	if (kind != 3) {
		long row = LDX(uk_met(), MT_ROW);
		line = UDIV(y, row) + LDX(t, UW_POS);
		long rank = tr_row(t, line);
		if (rank < 0)
			line = -1;
		if (kind == 1) {
			if (rank < 0)
				return 0;
			long nd = tr_node(t, rank);
			u32 fl = (u32)LDW(nd, TN_FLAGS);
			if (fl & 2) { /* a des enfants : triangle ou double-clic */
				long tx = LDW(nd, TN_DEPTH) * UK_TREE_IND + 10;
				if (ULT(x - tx, 16) || clicks == 2) {
					tr_toggle(t, rank);
					return 0;
				}
			}
			long r = uk_tr_select(t, rank);
			if (clicks != 2)
				return r;
			return UN_ACTIVATE;
		}
	}
	if (LDX(t, UW_HOT) == line)
		return 0;
	STX(t, UW_HOT, line);
	uk_w_refresh(t);
	return 0;
}

/* uk_tr_key : haut, bas, droite (déplie), gauche (replie, parent), Entrée */
long uk_tr_key(long t, long code, long ch, long mods)
{
	(void)ch;
	(void)mods;
	u32 k = (u32)code;
	long rank = uk_tr_index(t, LDX(t, UW_VALUE)); /* rang du choisi */
	long r;
	if (k == KEY_DOWN) {
		/* le suivant visible (ou le premier) */
		r = 0;
		if (rank >= 0) {
			r = tr_next(t, rank);
			if (UGE(r, LDX(t, UW_NITEMS)))
				return 0;
		}
	} else if (k == KEY_UP) {
		/* le précédent visible */
		if (rank < 0)
			return 0;
		long l = tr_line_of(t, rank);
		if (!l)
			return 0;
		/* comme l'asm : ligne -1 (caché) -> tr_row(-2) = nombre de lignes */
		r = tr_row(t, l - 1);
	} else {
		if (rank < 0)
			return -1;
		long nd = tr_node(t, rank);
		u32 fl = (u32)LDW(nd, TN_FLAGS);
		if (k == KEY_RIGHT) {
			if (!(fl & 2) || (fl & 1))
				return 0;
			return tr_toggle(t, rank);
		}
		if (k == KEY_LEFT) {
			if ((fl & 1) && (fl & 2))
				return tr_toggle(t, rank);
			/* replie, ou va au parent */
			long p = LDSW(nd, TN_PARENT);
			if (p < 0)
				return 0;
			return uk_tr_select(t, uk_tr_index(t, p));
		}
		if (k != KEY_ENTER)
			return -1;
		if (fl & 2)
			return tr_toggle(t, rank);
		return UN_ACTIVATE;
	}
	if (UGE(r, LDX(t, UW_NITEMS)))
		return 0;
	return uk_tr_select(t, r);
}

/* uk_tr_wheel */
long uk_tr_wheel(long t, long steps)
{
	STX(t, UW_POS, LDX(t, UW_POS) - steps * 3);
	tr_sync(t);
	uk_w_refresh(t);
	return 0;
}

/* uk_tr_child */
long uk_tr_child(long t, long child, long code)
{
	(void)code;
	STX(t, UW_POS, LDX(child, UW_VALUE));
	tr_sync(t);
	uk_w_refresh(t);
	return 0;
}
