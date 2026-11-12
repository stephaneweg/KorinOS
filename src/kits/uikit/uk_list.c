/* uk_list.c -- UIKit : la liste (UT_LIST), une ou plusieurs colonnes (texte
 * d'un élément : colonnes séparées par une tabulation), en-têtes facultatifs,
 * tri par clic sur un en-tête (numérique si les deux champs commencent par
 * un chiffre), colonnes redimensionnables (glisser le bord d'un en-tête),
 * sélection, double-clic / Entrée (UN_ACTIVATE), molette, clavier (flèches,
 * Début, Fin, Page, première lettre), barre de défilement superposée (un
 * widget enfant). Port ligne à ligne de uikit_list.inc
 * (src/kits/uikit/PORT.md). docs/DESIGN-ELEGANT.md 11.3.
 *
 * Champs : VALUE = élément choisi (-1), POS = première ligne montrée,
 * HOT = ligne survolée, SORT = colonne triée (-1), ANCHOR = sens (1 :
 * décroissant), DRAG = colonne redimensionnée + 1, DRAG2 = x de départ,
 * AUX2 = sa largeur de départ, AUX = la barre de défilement. */
#include "uk.h"

static long ls_sync(long l);
static long ls_text(long l, long item);

/* x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = drapeaux (UL_*) -> x0 =
 * poignée (0 : échec) */
long ui_list(long a0, long a1, long a2, long a3, long a4)
{
	long flags = a4;
	long l = uk_w_new(UT_LIST, a0, a1, a2, a3);
	if (!l)
		return 0;
	STX(l, UW_STYLE, flags);
	STX(l, UW_VALUE, -1);
	STX(l, UW_ANCHOR, 0);
	long sb = ui_scrollbar(0, 0, LDX(uk_met(), MT_SB), 1, 0);
	if (!sb)
		return 0; /* comme l'asm : la liste reste allouée */
	STX(sb, UW_PARENT, l);
	STX(l, UW_AUX, sb);
	if (flags & UL_HSCROLL) {
		/* UL_HSCROLL : barre horizontale, cachée tant que les colonnes tiennent */
		long hb = ui_scrollbar(0, 0, 1, LDX(uk_met(), MT_SB), USB_HORIZONTAL);
		if (!hb)
			return 0;
		STX(hb, UW_PARENT, l);
		STX(l, UW_HBAR, hb);
		STW(hb, UW_FLAGS, UWF_HIDDEN);
	}
	uk_ls_layout(l);
	return l;
}

/* uk_ls_hdr : -> hauteur de l'en-tête (0 : sans).
 * comme l'asm : lit uk_mt sans passer par uk_met. */
static long ls_hdr(long l)
{
	if (!(LDX(l, UW_STYLE) & UL_HEADER))
		return 0;
	return LDX(UKA(mt), MT_HDR);
}

/* uk_ls_hbar_h : -> hauteur de sa barre horizontale montrée, 0 sinon */
static long ls_hbar_h(long l)
{
	long hb = LDX(l, UW_HBAR);
	if (!hb)
		return 0;
	if (LDW(hb, UW_FLAGS) & UWF_HIDDEN)
		return 0;
	return LDX(UKA(mt), MT_SB);
}

/* uk_ls_rows : -> lignes entières visibles (>= 1) */
long uk_ls_rows(long l)
{
	long row = LDX(uk_met(), MT_ROW);
	long hb = ls_hbar_h(l);
	long hd = ls_hdr(l);
	long r = UDIV(LDW(l, UW_H) - hd - hb, row);
	if (r < 1)
		r = 1;
	return r;
}

/* uk_ls_colx : colonne -> r0 = son x (depuis la liste, moins le décalage
 * UL_HSCROLL), r1 = sa largeur (sans colonne définie : une seule, toute la
 * largeur) */
static ukr ls_colx(long l, long col)
{
	ukr r = {0, 0, 0, 0};
	if (!LDX(l, UW_NCOLS)) {
		r.r1 = LDW(l, UW_W);
		return r;
	}
	long cols = LDX(l, UW_COLS);
	long x = 0;
	for (long i = 0; ULT(i, col); i++)
		x += LDW(cols + (i << 4), 0);
	r.r1 = LDW(cols + (col << 4), 0);
	r.r0 = x - LDX(l, UW_HPOS);
	return r;
}

/* uk_ls_ncols : -> nombre de colonnes montrées (>= 1) */
static long ls_ncols(long l)
{
	long n = LDX(l, UW_NCOLS);
	if (ULT(n, 1))
		n = 1;
	return n;
}

/* uk_ls_layout : place la barre de défilement, borne la position, recopie
 * la plage dans la barre */
long uk_ls_layout(long l)
{
	long sb = LDX(l, UW_AUX);
	if (!sb)
		return 0;
	long w3 = LDX(uk_met(), MT_SB);
	long hd = ls_hdr(l);
	STW(sb, UW_X, LDW(l, UW_X) + LDW(l, UW_W) - w3 - 2);
	STW(sb, UW_Y, LDW(l, UW_Y) + hd + 1);
	STW(sb, UW_W, w3);
	STW(sb, UW_H, LDW(l, UW_H) - hd - 2);
	long hb = LDX(l, UW_HBAR); /* barre horizontale : au bas de la liste */
	if (hb) {
		STW(hb, UW_X, LDW(l, UW_X) + 1);
		STW(hb, UW_Y, LDW(l, UW_Y) + LDW(l, UW_H) - w3 - 1);
		STW(hb, UW_W, LDW(l, UW_W) - 2);
		STW(hb, UW_H, w3);
	}
	return ls_sync(l);
}

/* uk_ls_hsync : liste UL_HSCROLL : la barre horizontale d'après la largeur
 * des colonnes (cachée si elles tiennent), le décalage borné, la barre
 * verticale au-dessus */
static long ls_hsync(long l)
{
	long hb = LDX(l, UW_HBAR);
	if (!hb)
		return 0;
	long n = LDX(l, UW_NCOLS); /* largeur des colonnes */
	long cols = LDX(l, UW_COLS);
	long total = 0;
	while (n) {
		n--;
		total += LDW(cols + (n << 4), 0);
	}
	long w3 = LDX(uk_met(), MT_SB);
	long vis = LDW(l, UW_W) - w3 - 4; /* partie visible (la barre verticale en moins) */
	if (vis < 1)
		vis = 1;
	long hpos = LDX(l, UW_HPOS); /* décalage borné */
	long max = total - vis;
	if (hpos > max)
		hpos = max;
	if (hpos < 0)
		hpos = 0;
	STX(l, UW_HPOS, hpos);
	STX(hb, UW_MIN, 0);
	STX(hb, UW_MAX, total);
	STX(hb, UW_PAGE, vis);
	STX(hb, UW_VALUE, hpos);
	u32 f = (u32)LDW(hb, UW_FLAGS) | UWF_HIDDEN;
	if (total > vis)
		f &= ~(u32)UWF_HIDDEN;
	STW(hb, UW_FLAGS, f);
	long sb = LDX(l, UW_AUX); /* barre verticale */
	long h = LDW(l, UW_H) - ls_hdr(l) - 2;
	h -= ls_hbar_h(l);
	STW(sb, UW_H, h);
	return 0;
}

/* uk_ls_sync : position bornée, plage de la barre = (0, éléments, lignes) */
static long ls_sync(long l)
{
	ls_hsync(l);
	long rows = uk_ls_rows(l);
	long n = LDX(l, UW_NITEMS);
	long max = n - rows; /* position max */
	if (max < 0)
		max = 0;
	long pos = LDX(l, UW_POS);
	if (pos > max)
		pos = max;
	if (pos < 0)
		pos = 0;
	STX(l, UW_POS, pos);
	long sb = LDX(l, UW_AUX);
	if (!sb)
		return rows;
	STX(sb, UW_MIN, 0);
	STX(sb, UW_MAX, n);
	STX(sb, UW_PAGE, rows);
	STX(sb, UW_VALUE, pos);
	return rows;
}

/* uk_ls_select : élément (-1 : aucun) : le choisit et le rend visible */
long uk_ls_select(long l, long item)
{
	long n = LDX(l, UW_NITEMS);
	if (item >= n)
		item = n - 1;
	if (item < -1)
		item = -1;
	STX(l, UW_VALUE, item);
	if (item >= 0) {
		long rows = uk_ls_rows(l);
		long pos = LDX(l, UW_POS);
		if (item < pos)
			STX(l, UW_POS, item);
		else if (item >= pos + rows)
			STX(l, UW_POS, item - rows + 1);
	}
	return ls_sync(l);
}

/* uk_field_ptr : texte, numéro de colonne -> début du champ (fin : tabulation
 * ou NUL ; champ absent : la fin du texte) */
static long field_ptr(long s, long col)
{
	while (col) {
		long c = LDB(s, 0);
		s++;
		if (!c)
			return s - 1;
		if (c == 9)
			col--;
	}
	return s;
}

/* uk_field : texte, colonne -> le champ, copie terminée par NUL dans uk_fbuf
 * (255 octets au plus) */
long uk_field(long text, long col)
{
	long p = field_ptr(text, col);
	long d = UKA(fbuf);
	long i = 0;
	for (;;) {
		long c = LDB(p, i);
		if (!c || c == 9)
			break;
		STB(d, i, c);
		i++;
		if (!ULT(i, 255))
			break;
	}
	STB(d, i, 0);
	return d;
}

/* ---------------------------------------------------------------------------
 *  Éléments
 * ------------------------------------------------------------------------- */

/* uk_items_grow : place pour un élément de plus -> 1 (0 : plus de mémoire) */
long uk_items_grow(long l)
{
	long cap = LDX(l, UW_ICAP);
	if (ULT(LDX(l, UW_NITEMS), cap))
		return 1;
	long nc = cap << 1;
	if (ULT(nc, 16))
		nc = 16;
	long p = uk_alloc(nc << 4);
	if (!p)
		return 0;
	uk_memcpy(p, LDX(l, UW_ITEMS), LDX(l, UW_NITEMS) << 4);
	uk_free(LDX(l, UW_ITEMS));
	STX(l, UW_ITEMS, p);
	STX(l, UW_ICAP, nc);
	return 1;
}

/* uk_items_clear : rend les textes de ses éléments, plus d'élément */
long uk_items_clear(long w)
{
	long n = LDX(w, UW_NITEMS);
	if (!LDX(w, UW_KWFN)) { /* (liste virtuelle : pas d'éléments à rendre) */
		while (n) {
			n--;
			uk_free(LDX(LDX(w, UW_ITEMS) + (n << 4), 0));
		}
	}
	STX(w, UW_NITEMS, 0);
	return 0;
}

/* x0 = liste, x1 = texte (colonnes séparées par des tabulations), x2 =
 * donnée du programme -> x0 = numéro de l'élément (-1 : échec). Ajoute à la
 * fin (ui_list_sort pour trier) ; rien n'est redessiné (ui_w_draw). */
long ui_list_add(long a0, long a1, long a2)
{
	long l = a0;
	if (!l)
		return -1;
	if (!uk_items_grow(l))
		return -1;
	long s = uk_strdup(a1);
	if (!s)
		return -1;
	long i = LDX(l, UW_NITEMS);
	long it = LDX(l, UW_ITEMS) + (i << 4);
	STX(it, 0, s);
	STX(it, 8, a2);
	STX(l, UW_NITEMS, i + 1);
	ls_sync(l);
	return i;
}

/* x0 = liste : plus d'élément (rien n'est redessiné) */
long ui_list_clear(long a0)
{
	long l = a0;
	if (!l)
		return 0;
	uk_items_clear(l);
	STX(l, UW_VALUE, -1);
	STX(l, UW_HOT, -1);
	STX(l, UW_POS, 0);
	return ls_sync(l);
}

/* x0 = liste -> x0 = nombre d'éléments */
long ui_list_count(long a0)
{
	if (!a0)
		return 0;
	return LDX(a0, UW_NITEMS);
}

/* x0 = liste, x1 = numéro -> r0 = son texte (0 : pas d'élément), r1 = sa
 * donnée. Liste virtuelle : la fonction du programme (x0 = liste, x1 =
 * numéro), r1 = 0. */
ukr ui_list_get(long a0, long a1)
{
	ukr r = {0, 0, 0, 0};
	if (!a0)
		return r;
	long fn = LDX(a0, UW_KWFN);
	if (fn) {
		if (UGE(a1, LDX(a0, UW_NITEMS)))
			return r;
		r.r0 = UK_CB2(fn, a0, a1);
		return r;
	}
	if (UGE(a1, LDX(a0, UW_NITEMS)))
		return r;
	long it = LDX(a0, UW_ITEMS) + (a1 << 4);
	r.r0 = LDX(it, 0);
	r.r1 = LDX(it, 8);
	return r;
}

/* x0 = liste, x1 = numéro, x2 = nouveau texte : le remplace (et redessine) */
long ui_list_set(long a0, long a1, long a2)
{
	long l = a0;
	if (!l)
		return 0;
	if (UGE(a1, LDX(l, UW_NITEMS)))
		return 0;
	long s = uk_strdup(a2);
	if (!s)
		return 0;
	long it = LDX(l, UW_ITEMS) + (a1 << 4);
	long old = LDX(it, 0);
	STX(it, 0, s);
	uk_free(old);
	return uk_w_refresh(l);
}

/* x0 = liste, x1 = numéro : retire l'élément (et redessine) */
long ui_list_remove(long a0, long a1)
{
	long l = a0, i = a1;
	if (!l)
		return 0;
	if (UGE(i, LDX(l, UW_NITEMS)))
		return 0;
	uk_free(LDX(LDX(l, UW_ITEMS) + (i << 4), 0));
	long d = LDX(l, UW_ITEMS) + (i << 4); /* les suivants descendent */
	uk_memcpy(d, d + 16, (LDX(l, UW_NITEMS) - i - 1) << 4);
	STX(l, UW_NITEMS, LDX(l, UW_NITEMS) - 1);
	long v = LDX(l, UW_VALUE); /* sélection : suit son élément */
	if (v >= i) {
		if (v > i)
			v = v - 1;
		else
			v = -1;
		STX(l, UW_VALUE, v);
	}
	STX(l, UW_HOT, -1);
	ls_sync(l);
	return uk_w_refresh(l);
}

/* x0 = liste, x1 = titre, x2 = largeur, x3 = drapeaux (ULC_RIGHT) -> x0 =
 * numéro de la colonne (-1 : échec). Les colonnes se placent de gauche à
 * droite ; sans colonne, une seule de toute la largeur. */
long ui_list_column(long a0, long a1, long a2, long a3)
{
	long title = TR(a1); /* tr_x1 */
	long l = a0;
	if (!l)
		return -1;
	long n = LDX(l, UW_NCOLS);
	long p = uk_alloc((n + 1) << 4);
	if (!p)
		return -1;
	uk_memcpy(p, LDX(l, UW_COLS), n << 4);
	uk_free(LDX(l, UW_COLS));
	STX(l, UW_COLS, p);
	long e = p + (n << 4);
	STW(e, 0, a2);
	STW(e, 4, a3);
	STX(e, 8, uk_strdup(title));
	STX(l, UW_NCOLS, n + 1);
	ls_sync(l);
	return n;
}

/* ---------------------------------------------------------------------------
 *  Tri
 * ------------------------------------------------------------------------- */

/* uk_field_isnum : champ -> 1 s'il commence par un chiffre (après des espaces) */
static long field_isnum(long s)
{
	long c;
	do {
		c = LDB(s, 0);
		s++;
	} while (c == ' ');
	return (u32)(c - '0') < 10;
}

/* uk_field_num : champ -> son nombre entier (chiffres du début ; les
 * séparateurs d'espace ou de point entre deux chiffres sont sautés : « 1 024 ») */
static long field_num(long s)
{
	u64 v = 0;
	u32 c;
	do {
		c = (u32)LDB(s, 0);
		s++;
	} while (c == ' ');
	for (;;) {
		c -= '0';
		if (c < 10) {
			v = v * 10 + c;
			c = (u32)LDB(s, 0);
			s++;
			continue;
		}
		c += '0'; /* séparateur suivi d'un chiffre */
		if (c != ' ' && c != '.')
			break;
		c = (u32)LDB(s, 0);
		if ((u32)(c - '0') >= 10)
			break;
		s++;
	}
	return (long)v;
}

/* uk_cmp_field : textes a, b, colonne -> -1, 0, 1 (numérique si les deux
 * champs commencent par un chiffre ; sinon ASCII sans casse) */
static long cmp_field(long a, long b, long col)
{
	long pa = field_ptr(a, col);
	long pb = field_ptr(b, col);
	if (field_isnum(pa) && field_isnum(pb)) {
		u64 na = (u64)field_num(pa);
		u64 nb = (u64)field_num(pb);
		if (na < nb)
			return -1;
		if (na > nb)
			return 1;
		/* comme l'asm : nombres égaux -> comparaison des textes */
	}
	for (;;) {
		u32 c0 = (u32)LDB(pa, 0);
		u32 c1 = (u32)LDB(pb, 0);
		pa++;
		pb++;
		if (c0 == 9)
			c0 = 0;
		if (c1 == 9)
			c1 = 0;
		if (c0 - 'A' < 26) /* minuscules */
			c0 += 32;
		if (c1 - 'A' < 26)
			c1 += 32;
		if (c0 < c1)
			return -1;
		if (c0 > c1)
			return 1;
		if (!c0)
			return 0;
	}
}

/* uk_ls_sort_go : trie les éléments à partir de « first » (tri par
 * insertion, stable ; l'élément choisi le reste) et redessine */
static long ls_sort_go(long l, long col, long desc, long first)
{
	if (!l)
		return 0;
	STX(l, UW_SORT, col);
	STX(l, UW_ANCHOR, desc);
	if (!LDX(l, UW_KWFN)) { /* virtuelle : le programme trie */
		long sel = 0; /* texte de l'élément choisi */
		long v = LDX(l, UW_VALUE);
		if (v >= 0)
			sel = LDX(LDX(l, UW_ITEMS) + (v << 4), 0);
		long items = LDX(l, UW_ITEMS);
		long n = LDX(l, UW_NITEMS);
		for (long i = first + 1; ULT(i, n); i++) {
			long kt = LDX(items + (i << 4), 0); /* clé */
			long kd = LDX(items + (i << 4), 8);
			long j = i - 1;
			while (j >= first) {
				long r = cmp_field(LDX(items + (j << 4), 0), kt, col);
				if (desc)
					r = -r;
				if (r <= 0)
					break;
				STX(items + (j << 4), 16, LDX(items + (j << 4), 0));
				STX(items + (j << 4), 24, LDX(items + (j << 4), 8));
				j--;
			}
			STX(items + (j << 4), 16, kt);
			STX(items + (j << 4), 24, kd);
		}
		if (sel) { /* l'élément choisi : sa nouvelle place */
			for (long k = 0; ULT(k, n); k++) {
				if (LDX(items + (k << 4), 0) == sel) {
					uk_ls_select(l, k);
					break;
				}
			}
		}
	}
	return uk_w_refresh(l);
}

/* x0 = liste, x1 = colonne, x2 = 1 décroissant : trie les éléments (tri par
 * insertion, stable ; l'élément choisi le reste) et redessine */
long ui_list_sort(long a0, long a1, long a2)
{
	return ls_sort_go(a0, a1, a2, 0);
}

/* uk_ls_sort_range : x3 = premier élément trié (ceux d'avant ne bougent
 * pas : dossiers puis fichiers) */
long uk_ls_sort_range(long l, long col, long desc, long first)
{
	return ls_sort_go(l, col, desc, first);
}

/* ---------------------------------------------------------------------------
 *  Dessin
 * ------------------------------------------------------------------------- */

static long ls_header(long l);
static long ls_row(long l, long item, long y);

/* uk_ls_draw */
long uk_ls_draw(long l)
{
	long mt = uk_met();
	long x = SXTW(LDW(l, UW_X));
	long y = SXTW(LDW(l, UW_Y));
	long wd = LDW(l, UW_W);
	long ht = LDW(l, UW_H);
	/* fond SURFACE, traits LIGNE dessus et dessous */
	long bg = JET(TJ_SURFACE);
	if (LDX(l, UW_STYLE) & UL_SIDEBAR) /* (UL_SIDEBAR : CREUX, comme Fichiers) */
		bg = JET(TJ_SUNKEN);
	ui_fill(x, y, wd, ht, bg);
	if (!(LDX(l, UW_STYLE) & UL_SIDEBAR)) { /* (panneau latéral : sans traits) */
		ui_fill(x, y, wd, 1, JET(TJ_LINE));
		ui_fill(x, y + ht - 1, wd, 1, JET(TJ_LINE));
	}
	long hd = ls_hdr(l); /* hauteur de l'en-tête */
	if (hd)
		ls_header(l);
	long item = LDX(l, UW_POS);
	long ry = y + hd; /* y de la ligne */
	long bot = ht - ls_hbar_h(l); /* (bas des lignes : la barre horizontale) */
	for (;;) {
		if (UGE(item, LDX(l, UW_NITEMS)))
			break;
		if (ry + LDX(mt, MT_ROW) > y + bot)
			break;
		ls_row(l, item, ry);
		ry += LDX(mt, MT_ROW);
		item++;
	}
	return 0;
}

/* uk_ls_header : les en-têtes de colonnes (Sans Bold petite TEXTE3, flèche
 * du tri, séparateurs, trait dessous) */
static long ls_header(long l)
{
	long hd = ls_hdr(l);
	long x = SXTW(LDW(l, UW_X));
	long y = SXTW(LDW(l, UW_Y));
	ui_fill(x, y + hd - 1, LDW(l, UW_W), 1, JET(TJ_LINE)); /* trait sous l'en-tête */
	long n = LDX(l, UW_NCOLS);
	for (long c = 0; ULT(c, n); c++) {
		ukr cx = ls_colx(l, c);
		long colx = x + cx.r0; /* x de la colonne */
		long colw = cx.r1;     /* largeur */
		ui_fill(colx + colw - 1, y + 6, 1, hd - 12, JET(TJ_LINE)); /* séparateur à droite */
		long title = LDX(LDX(l, UW_COLS) + (c << 4), 8);
		long font = uk_bold();
		long end = uk_text_box(title, colx + 10, y, hd, JET(TJ_TEXT3), font, colw - 30,
				       LDX(UKA(mt), MT_SMALL) - 1);
		if (LDX(l, UW_SORT) == c) { /* colonne triée : flèche */
			long s = S(uk_s_up);
			if (LDX(l, UW_ANCHOR))
				s = S(uk_s_down);
			uk_text_box(s, end + 4, y, hd, JET(TJ_TEXT2), uk_font_r(), 0, 9);
		}
	}
	return 0;
}

/* uk_ls_rmargin : -> marge droite des lignes (place de la barre si utile) */
static long ls_rmargin(long l)
{
	long sb = LDX(l, UW_AUX);
	if (!sb)
		return 6;
	ukr g = uk_sb_geom(sb);
	if (!g.r3)
		return 6;
	return LDW(sb, UW_W) + 8;
}

/* uk_ls_icon : genre (0 fichier, 1 dossier, 2 volume), x, y : icône 16 x 15 */
static void ls_icon(long kind, long x, long y)
{
#define UK_IC(dx, dy, w, h, t) ui_fill(x + (dx), y + (dy), w, h, JET(t))
	if (kind == 1) { /* dossier */
		UK_IC(0, 1, 7, 3, TJ_WARNING);
		UK_IC(0, 3, 16, 11, TJ_WARNING);
		long c = (u32)ui_mix(JET(TJ_WARNING), JET(TJ_SURFACE), 110);
		ui_fill(x + 1, y + 5, 14, 8, c);
	} else if (UGT(kind, 1)) { /* volume */
		UK_IC(0, 3, 16, 10, TJ_TEXT2);
		UK_IC(1, 4, 14, 8, TJ_SUNKEN);
		UK_IC(1, 4, 14, 1, TJ_LINE);
		UK_IC(11, 8, 3, 2, TJ_SUCCESS);
	} else { /* fichier */
		UK_IC(2, 0, 12, 15, TJ_TEXT3);
		UK_IC(3, 1, 10, 13, TJ_SURFACE);
		UK_IC(10, 1, 3, 3, TJ_SUNKEN);
		UK_IC(5, 6, 6, 1, TJ_LINE_STRONG);
		UK_IC(5, 8, 6, 1, TJ_LINE_STRONG);
		UK_IC(5, 10, 4, 1, TJ_LINE_STRONG);
	}
#undef UK_IC
}

/* uk_ls_row : élément, y : une ligne */
static long ls_row(long l, long item, long y)
{
	long row = LDX(uk_met(), MT_ROW);
	long x = SXTW(LDW(l, UW_X));
	long foc = uk_w_focused(l); /* focus */
	long white = 0;             /* 1 : texte blanc */
	if (LDX(l, UW_STYLE) & UL_SIDEBAR) {
		/* UL_SIDEBAR : « #titre » = une section */
		long t = ls_text(l, item);
		if (LDB(t, 0) == '#') {
			long px = LDX(uk_met(), MT_SMALL);
			long font = uk_bold();
			return uk_text_box(t + 1, x + 10, y, row, JET(TJ_TEXT3), font, LDW(l, UW_W) - 20, px);
		}
	}
	if ((LDX(l, UW_STYLE) & UL_STRIPES) && (item & 1)) {
		/* UL_STRIPES : une ligne sur deux CREUX */
		long w = LDW(l, UW_W);
		long c = (u32)ui_mix(JET(TJ_SURFACE), JET(TJ_SUNKEN), 128);
		ui_fill(x, y, w, row, c);
	}
	long c5 = 0;
	int box = 0;
	if (LDX(l, UW_VALUE) == item) {
		/* choisi : ACCENT (focus), ACCENT_CLAIR */
		c5 = JET(TJ_ACCENT_LIGHT);
		if (foc) {
			c5 = JET(TJ_ACCENT);
			white = 1;
		}
		box = 1;
	} else if (LDX(l, UW_HOT) == item) {
		box = 1;
		if (LDX(l, UW_STYLE) & UL_SIDEBAR) { /* (sur CREUX : SURFACE) */
			c5 = JET(TJ_SURFACE);
		} else {
			/* survol : CREUX (Givre), FACE (Noir) */
			c5 = JET(TJ_SUNKEN);
			if ((u32)JET(TJ_DARK))
				c5 = JET(TJ_FACE);
		}
	}
	if (box) {
		long m = ls_rmargin(l);
		long w = LDW(l, UW_W) - 6 - m;
		ui_rbox(x + 6, y, w, row, 5, c5, c5);
	}
	long ncols = ls_ncols(l);
	for (long c = 0; ULT(c, ncols); c++) {
		ukr cx = ls_colx(l, c);
		long colx = x + cx.r0; /* x de la colonne */
		long colw = cx.r1;
		if (LDX(l, UW_STYLE) & UL_GRID) /* UL_GRID : trait LIGNE à droite */
			ui_fill(colx + colw - 1, y, 1, row, JET(TJ_LINE));
		long fn = LDX(l, UW_CELLFN); /* cellule dessinée par le programme ? */
		if (fn) {
			if (uk_cb8(fn, l, item, c, colx, y, colw, row, white))
				continue;
		}
		long t = ls_text(l, item);
		uk_field(t, c);
		/* colonne 0 : police d'interface, TEXTE1 ; les autres : petite, TEXTE3 */
		long px = LDX(UKA(mt), MT_PX);
		long color = JET(TJ_TEXT1);
		if (c) {
			color = JET(TJ_TEXT3);
			px = LDX(UKA(mt), MT_SMALL);
		}
		if (white)
			color = JET(TJ_ACCENT_INK);
		long max, tx;
		if (!c && (LDX(l, UW_STYLE) & UL_ICONS)) {
			/* UL_ICONS : icône en colonne 0 (comme l'asm : lue dans UW_ITEMS,
			 * même pour une liste virtuelle) */
			long kind = LDX(LDX(l, UW_ITEMS) + (item << 4), 8) & 3;
			ls_icon(kind, colx + 8, y + LSR(row - 15, 1));
			max = colw - (16 + 22);
			tx = colx + 10 + 22;
		} else {
			max = colw - 16; /* largeur utile */
			tx = colx + 10;
		}
		if (LDX(l, UW_NCOLS) && (LDW(LDX(l, UW_COLS) + (c << 4), 4) & ULC_RIGHT)) {
			/* aligné à droite (ULC_RIGHT) : largeur du texte */
			long font = uk_font_r();
			long tw = ui_text_width(UKA(fbuf), px, font);
			if (tw > max)
				tw = max;
			tx = colx + colw - 10 - tw;
		}
		long font = uk_font_r();
		uk_text_box(UKA(fbuf), tx, y, row, color, font, max, px);
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Souris, clavier, molette
 * ------------------------------------------------------------------------- */

/* uk_ls_mouse : genre, point local, clics */
long uk_ls_mouse(long l, long kind, long x, long y, long clicks)
{
	long hot;
	if (kind == 3) {
		hot = -1; /* sortie : plus de survol */
		goto sethot;
	}
	long drag = LDX(l, UW_DRAG); /* largeur de colonne en cours */
	if (drag) {
		if (kind == 2) {
			STX(l, UW_DRAG, 0);
			return 0;
		}
		long col = drag - 1;
		long nw = LDX(l, UW_AUX2) + (x - LDX(l, UW_DRAG2));
		if (nw < 24)
			nw = 24;
		STW(LDX(l, UW_COLS) + (col << 4), 0, nw);
		ls_sync(l);
		uk_w_refresh(l);
		return 0;
	}
	long hd = ls_hdr(l);
	if (y < hd) {
		if (kind != 1) /* en-tête : appui seulement */
			return 0;
		long n = LDX(l, UW_NCOLS);
		if (!n)
			return 0;
		for (long c = 0; ULT(c, n); c++) { /* bord d'une colonne (+/- 4) ? */
			ukr cx = ls_colx(l, c);
			long d = x - (cx.r0 + cx.r1);
			if (d < 0)
				d = -d;
			if (d <= 4) { /* redimensionne la colonne c */
				STX(l, UW_DRAG, c + 1);
				STX(l, UW_DRAG2, x);
				STX(l, UW_AUX2, cx.r1);
				return 0;
			}
		}
		if (LDX(l, UW_STYLE) & UL_NOSORT) /* tri par cette colonne */
			return 0;
		for (long c = 0; ULT(c, n); c++) {
			ukr cx = ls_colx(l, c);
			if (x < cx.r0 + cx.r1) {
				long desc = 0; /* même colonne : sens inverse */
				if (LDX(l, UW_SORT) == c)
					desc = LDX(l, UW_ANCHOR) ^ 1;
				if (LDX(l, UW_STYLE) & UL_USERSORT) { /* UL_USERSORT : au programme */
					STX(l, UW_SORT, c);
					STX(l, UW_ANCHOR, desc);
					return UN_SORT;
				}
				ui_list_sort(l, c, desc);
				return UN_SORT;
			}
		}
		return 0;
	}
	/* ligne sous le point */
	hot = UDIV(y - hd, LDX(uk_met(), MT_ROW)) + LDX(l, UW_POS);
	if (!ULT(hot, LDX(l, UW_NITEMS)))
		hot = -1;
	if (kind == 1) {
		if (hot < 0)
			return 0;
		if (LDX(l, UW_STYLE) & UL_SIDEBAR) { /* UL_SIDEBAR : une section ne se choisit pas */
			if (LDB(ls_text(l, hot), 0) == '#')
				return 0;
		}
		STX(l, UW_PX, x); /* point d'appui (UL_DRAG) */
		STX(l, UW_AUX2, y);
		STW(l, UW_FLAGS, (u32)LDW(l, UW_FLAGS) & ~16u);
		long old = LDX(l, UW_VALUE);
		uk_ls_select(l, hot);
		uk_w_refresh(l);
		if (uk.mbtn & 2) /* bouton droit : menu contextuel */
			return UN_CONTEXT;
		if (clicks == 2)
			return UN_ACTIVATE;
		if (old != hot)
			return UN_CHANGE;
		return 0;
	}
	if (kind) /* relâché */
		return 0;
	if (LDX(l, UW_STYLE) & UL_DRAG) {
		/* UL_DRAG : bouton tenu, assez loin du point d'appui -> glisser l'élément */
		if (uk.capture == l) {
			if (LDW(l, UW_FLAGS) & 16)
				return 0;
			long v = LDX(l, UW_VALUE);
			if (v < 0)
				return 0;
			long dx = x - LDX(l, UW_PX);
			if (dx < 0)
				dx = -dx;
			long dy = y - LDX(l, UW_AUX2);
			if (dy < 0)
				dy = -dy;
			if (ULT(dx + dy, UK_DRAG_PX))
				return 0;
			STW(l, UW_FLAGS, (u32)LDW(l, UW_FLAGS) | 16);
			/* le texte de sa première colonne */
			long f = uk_field(ls_text(l, v), 0);
			ui_drag_start(f, -1, UDT_TEXT);
			return 0;
		}
	}
sethot:
	/* déplacement : survol */
	if (LDX(l, UW_HOT) == hot)
		return 0;
	STX(l, UW_HOT, hot);
	uk_w_refresh(l);
	return 0;
}

/* uk_ls_key : code HID, caractère, mods -> code, -1 si non pris */
long uk_ls_key(long l, long code, long ch, long mods)
{
	(void)mods;
	u32 k = (u32)code;
	long v = LDX(l, UW_VALUE);
	if (k == KEY_ENTER) {
		if (v < 0)
			return -1;
		return UN_ACTIVATE;
	}
	if (!LDX(l, UW_NITEMS))
		return -1;
	long rows = uk_ls_rows(l); /* lignes visibles */
	long nv;
	if (k == KEY_UP) {
		nv = v - 1;
		if (v < 0)
			nv = 0;
	} else if (k == KEY_DOWN) {
		nv = v + 1;
	} else if (k == KEY_HOME) {
		nv = 0;
	} else if (k == KEY_END) {
		nv = LDX(l, UW_NITEMS) - 1;
	} else if (k == KEY_PGUP) {
		nv = v - rows;
		if (nv < 0)
			nv = 0;
	} else if (k == KEY_PGDN) {
		nv = v + rows;
	} else if (k == KEY_LEFT || k == KEY_RIGHT) {
		/* gauche, droite : les colonnes (UL_HSCROLL) */
		if (!LDX(l, UW_HBAR))
			return -1;
		long d = 48;
		if (k == KEY_LEFT)
			d = -48;
		STX(l, UW_HPOS, LDX(l, UW_HPOS) + d);
		ls_sync(l);
		uk_w_refresh(l);
		return 0;
	} else {
		/* première lettre : élément suivant qui commence par elle */
		if ((s32)ch <= ' ')
			return -1;
		if ((u32)ch >= 0x7F)
			return -1;
		u32 c = (u32)ch | 0x20; /* (minuscule ASCII) */
		long n = LDX(l, UW_NITEMS);
		long i = 1;
		for (;;) {
			if (UGT(i, n))
				return 0;
			nv = UREM(v + i, n); /* modulo le nombre d'éléments */
			long t = ls_text(l, nv);
			if (((u32)LDB(t, 0) | 0x20) == c)
				break;
			i++;
		}
	}
	uk_ls_select(l, nv);
	uk_w_refresh(l);
	if (LDX(l, UW_VALUE) == v)
		return 0;
	return UN_CHANGE;
}

/* uk_ls_wheel : crans : 3 lignes par cran */
long uk_ls_wheel(long l, long steps)
{
	STX(l, UW_POS, LDX(l, UW_POS) - steps * 3);
	ls_sync(l);
	uk_w_refresh(l);
	return 0;
}

/* uk_ls_child : enfant (une barre), code : la barre a bougé */
long uk_ls_child(long l, long child, long code)
{
	(void)code;
	long v = LDX(child, UW_VALUE);
	if (child == LDX(l, UW_HBAR))
		STX(l, UW_HPOS, v);
	else
		STX(l, UW_POS, v);
	ls_sync(l);
	uk_w_refresh(l);
	return 0;
}

/* uk_ls_text : élément -> son texte (virtuelle : la fonction du programme,
 * x0 = liste, x1 = élément ; « » s'il n'y en a pas) */
static long ls_text(long l, long item)
{
	long fn = LDX(l, UW_KWFN);
	if (fn) {
		long t = UK_CB2(fn, l, item);
		if (!t)
			t = S(uk_empty);
		return t;
	}
	return LDX(LDX(l, UW_ITEMS) + (item << 4), 0);
}

/* x0 = liste, x1 = nombre de lignes, x2 = fonction (x0 = liste, x1 = ligne
 * -> x0 = texte de la ligne, colonnes séparées par des tabulations ; valable
 * jusqu'au prochain appel) : liste virtuelle (grille de données : le
 * programme garde ses lignes ; un clic sur un en-tête rend UN_SORT, le
 * programme trie et redessine) */
long ui_list_virtual(long a0, long a1, long a2)
{
	long l = a0;
	if (!l)
		return 0;
	uk_items_clear(l);
	STX(l, UW_KWFN, a2);
	STX(l, UW_NITEMS, a1);
	if (UGE(LDX(l, UW_VALUE), a1))
		STX(l, UW_VALUE, -1);
	ls_sync(l);
	return uk_w_refresh(l);
}

/* x0 = liste, x1 = fonction (x0 = liste, x1 = ligne, x2 = colonne, x3, x4 =
 * coin de la cellule dans la fenêtre, x5 = largeur, x6 = hauteur, x7 = 1 si
 * le fond est celui d'une ligne choisie avec le focus -> x0 = 1 : cellule
 * dessinée, 0 : le texte ordinaire ; 0 : aucune) */
long ui_list_cell(long a0, long a1)
{
	if (a0)
		STX(a0, UW_CELLFN, a1);
	return a0;
}

/* x0 = liste -> r0 = colonne triée (-1 : aucune), r1 = 1 décroissant */
ukr ui_list_sorted(long a0)
{
	ukr r = {0, 0, 0, 0};
	r.r1 = LDX(a0, UW_ANCHOR);
	r.r0 = LDX(a0, UW_SORT);
	return r;
}
