/* uk_area.c -- UIKit : le texte multiligne (UT_AREA), d'après l'éditeur
 * (src/apps/editor) : tampon plat UTF-8 (« \n » entre les lignes), curseur et
 * ancre en octets, insertion / suppression par déplacement. Port ligne à ligne
 * de uikit_area.inc (src/kits/uikit/PORT.md).
 *
 * Champs : VALUE curseur, ANCHOR ancre, POS première ligne, DRAG défilement
 * horizontal (pixels), DRAG2 colonne voulue (pixels), AUX barre verticale,
 * HBAR barre horizontale, PX taille de la police (0 : l'interface), KWFN
 * fonction des mots-clés du programme, HOT ligne éclairée, TA_BG / TA_INK
 * papier et encre + 1, TA_LEX état de l'analyse (UTA_CS, UTA_XML). */
#include "uk.h"

static long ta_layout_impl(long w);

/* x0 = texte multiligne, x1 = papier 0x00RRGGBB (-1 : celui du thème), x2 = encre */
long ui_textarea_paper(long w, long bg, long ink)
{
	STX(w, UW_TA_BG, bg + 1);
	STX(w, UW_TA_INK, ink + 1);
	return w;
}

/* uk_ta_bars : x0 = texte multiligne : crée ou retire ses barres de
 * défilement selon son style (UTA_VSCROLL, UTA_HSCROLL) */
static void ta_bars(long w)
{
	long style = LDX(w, UW_STYLE), b;

	b = LDX(w, UW_AUX); /* verticale */
	if (!(style & UTA_VSCROLL)) {
		if (b) {
			ui_w_destroy(b);
			STX(w, UW_AUX, 0);
		}
	} else if (!b) {
		b = ui_scrollbar(0, 0, 8, 8, 0);
		if (b) {
			STX(b, UW_PARENT, w);
			STX(w, UW_AUX, b);
		}
	}
	b = LDX(w, UW_HBAR); /* horizontale */
	if (!(style & UTA_HSCROLL)) {
		if (b) {
			ui_w_destroy(b);
			STX(w, UW_HBAR, 0);
		}
	} else if (!b) {
		b = ui_scrollbar(0, 0, 8, 8, USB_HORIZONTAL);
		if (b) {
			STX(b, UW_PARENT, w);
			STX(w, UW_HBAR, b);
		}
	}
}

/* x0..x3 = rectangle, x4 = drapeaux (UTF_READONLY, UTF_MONO, UTA_LINENUM)
 * -> poignée */
long ui_textarea(long x, long y, long wd, long ht, long flags)
{
	long w = uk_w_new(UT_AREA, x, y, wd, ht);
	if (!w)
		return 0;
	STX(w, UW_STYLE, flags);
	uk_set_text(w, 0, 0);
	ta_bars(w);
	ta_layout_impl(w);
	return w;
}

/* uk_ta_font : x0 = texte multiligne -> r0 = police, r1 = taille, r2 =
 * hauteur de ligne (1,5 x la taille) */
static ukr ta_font(long w)
{
	ukr r = uk_tb_font(w);
	r.r2 = r.r1 + LSR(r.r1, 1);
	return r;
}

/* uk_ta_gutter : x0 = texte multiligne -> marge des numéros de ligne (0 : sans) */
static long ta_gutter(long w)
{
	return (LDX(w, UW_STYLE) & UTA_LINENUM) ? 44 : 0;
}

/* uk_ta_bottom : x0 = widget -> r0 = place de la barre horizontale en bas
 * (0 : sans), r1 = place de la verticale à droite */
static ukr ta_bottom(long w)
{
	long sb = LDX(uk_met(), MT_SB) + 2;
	ukr r;

	r.r0 = LDX(w, UW_HBAR) ? sb : 0;
	r.r1 = LDX(w, UW_AUX) ? sb : 0;
	r.r2 = 0;
	r.r3 = 0;
	return r;
}

/* uk_ta_rows : x0 = widget -> lignes entières visibles (>= 1) */
static long ta_rows(long w)
{
	long h = LDW(w, UW_H) - ta_bottom(w).r0 - 2 * UK_TA_PAD;
	/* comme l'asm : udiv (une hauteur négative donne beaucoup de lignes) */
	long n = UDIV(h, ta_font(w).r2);
	if (n < 1)
		n = 1;
	return n;
}

/* uk_ta_nlines : x0 = widget -> nombre de lignes */
static long ta_nlines(long w)
{
	long t = LDX(w, UW_TEXT), n = LDX(w, UW_TLEN), i, c = 1;

	for (i = 0; ULT(i, n); i++)
		if (LDB(t, i) == 10)
			c++;
	return c;
}

/* uk_ta_line_of : x0 = widget, x1 = décalage -> r0 = numéro de sa ligne, r1 =
 * début de la ligne */
static ukr ta_line_of(long w, long off)
{
	long t = LDX(w, UW_TEXT), i = 0;
	ukr r;

	r.r0 = 0;
	r.r1 = 0; /* début de la ligne courante */
	while (ULT(i, off)) {
		long c = LDB(t, i);
		i++;
		if (c != 10)
			continue;
		r.r0++;
		r.r1 = i;
	}
	r.r2 = 0;
	r.r3 = 0;
	return r;
}

/* x0 = widget, x1 = numéro de ligne -> r0 = son début (au-delà : la fin du
 * texte), r1 = sa fin (le « \n » ou la fin du texte) */
ukr uk_ta_line(long w, long line)
{
	long t = LDX(w, UW_TEXT), n = LDX(w, UW_TLEN), i = 0;
	ukr r;

	while (line && ULT(i, n)) {
		long c = LDB(t, i);
		i++;
		if (c == 10)
			line--;
	}
	r.r0 = i;
	while (ULT(i, n) && LDB(t, i) != 10)
		i++;
	r.r1 = i;
	r.r2 = 0;
	r.r3 = 0;
	return r;
}

/* uk_ta_width : x0 = widget, x1 = début, x2 = octets -> largeur en pixels */
static long ta_width(long w, long start, long n)
{
	ukr f = ta_font(w);
	ttf_set_size(f.r0, f.r1);
	return ttf_text_pos(f.r0, LDX(w, UW_TEXT) + start, n, -1, 0).r0;
}

/* uk_ta_offset_at : x0 = widget, x1 = numéro de ligne, x2 = x en pixels
 * (depuis le début du texte de la ligne) -> décalage sous ce point */
static long ta_offset_at(long w, long line, long x)
{
	ukr l = uk_ta_line(w, line);
	long start = l.r0, len = l.r1 - l.r0;
	ukr f = ta_font(w);

	ttf_set_size(f.r0, f.r1);
	return start + ttf_text_pos(f.r0, LDX(w, UW_TEXT) + start, len, x < 0 ? 0 : x, 0).r1;
}

/* uk_ta_layout : x0 = widget : place les barres de défilement, recopie la plage */
static long ta_layout_impl(long w)
{
	long sb = LDX(uk_met(), MT_SB);
	long bot = ta_bottom(w).r0; /* place en bas */
	long x = LDW(w, UW_X), y = LDW(w, UW_Y), wd = LDW(w, UW_W), ht = LDW(w, UW_H);
	long b = LDX(w, UW_AUX), g, n;

	if (b) { /* verticale : à droite */
		STW(b, UW_X, x + wd - sb - 2);
		STW(b, UW_Y, y + 2);
		STW(b, UW_W, sb);
		STW(b, UW_H, ht - 4 - bot);
	}
	if (LDX(w, UW_HBAR)) { /* horizontale : en bas, après la marge */
		g = ta_gutter(w);
		x = LDW(w, UW_X);
		y = LDW(w, UW_Y);
		wd = LDW(w, UW_W);
		ht = LDW(w, UW_H);
		b = LDX(w, UW_HBAR);
		STW(b, UW_X, x + g + 2);
		STW(b, UW_Y, y + ht - sb - 2);
		n = wd - g - 4;
		if (LDX(w, UW_AUX))
			n = n - sb - 2;
		STW(b, UW_W, n);
		STW(b, UW_H, sb);
	}
	return uk_ta_sync(w);
}

long uk_ta_layout(long w)
{
	return ta_layout_impl(w);
}

/* uk_ta_maxw : x0 = widget -> largeur estimée de la ligne la plus longue
 * (colonnes, taquets de 4, x largeur d'une espace ; exacte en mono) + 40 */
static long ta_maxw(long w)
{
	long t = LDX(w, UW_TEXT), n = LDX(w, UW_TLEN);
	long col = 0, max = 0, i = 0, c;
	ukr f;

	while (ULT(i, n)) {
		c = LDB(t, i);
		i++;
		if (c == 10) {
			if (UGT(col, max))
				max = col;
			col = 0;
			continue;
		}
		if (c == 9) {
			col = (col + 4) & ~3L;
			continue;
		}
		if ((c & 0xC0) == 0x80)
			continue;
		/* comme l'asm : le test du retour chariot porte sur l'octet
		 * masqué par 0xC0 (jamais 13) : un \r compte une colonne ; le test,
		 * toujours faux, n'est pas écrit */
		col++;
	}
	if (UGT(col, max))
		max = col;
	f = ta_font(w); /* largeur d'une espace */
	return ui_text_width(S(uk_s_space), f.r1, f.r0) * max + 40;
}

/* uk_ta_textw : x0 = widget -> largeur utile du texte (sans marges, numéros,
 * barre) */
static long ta_textw(long w)
{
	long x = LDW(w, UW_W) - ta_gutter(w) - (2 * UK_TA_PAD + 4);
	return x - ta_bottom(w).r1;
}

/* x0 = widget : première ligne bornée, plage des barres */
long uk_ta_sync(long w)
{
	long rows = ta_rows(w);
	long n = ta_nlines(w);
	long last = n - rows, pos, b, mw, tw, lim, d;
	long r = n; /* (x0 rendu par l'asm) */

	if (last < 0)
		last = 0;
	pos = LDX(w, UW_POS);
	if (pos > last)
		pos = last;
	if (pos < 0)
		pos = 0;
	STX(w, UW_POS, pos);
	b = LDX(w, UW_AUX);
	if (b) {
		STX(b, UW_MIN, 0);
		STX(b, UW_MAX, n);
		STX(b, UW_PAGE, rows);
		STX(b, UW_VALUE, pos);
	}
	b = LDX(w, UW_HBAR); /* horizontale : la ligne la plus longue */
	if (!b)
		return r;
	mw = ta_maxw(w);
	tw = ta_textw(w);
	STX(b, UW_MIN, 0);
	STX(b, UW_MAX, mw);
	STX(b, UW_PAGE, tw);
	STX(b, UW_STEP, 10);
	lim = mw - tw; /* défilement borné */
	if (lim < 0)
		lim = 0;
	d = LDX(w, UW_DRAG);
	if (d > lim)
		d = lim;
	STX(w, UW_DRAG, d);
	STX(b, UW_VALUE, d);
	return tw;
}

/* uk_ta_show_caret : x0 = widget : rend le curseur visible (lignes et pixels) */
static void ta_show_caret(long w)
{
	ukr l = ta_line_of(w, LDX(w, UW_VALUE));
	long line = l.r0, start = l.r1;
	long rows = ta_rows(w);
	long pos = LDX(w, UW_POS), cx, tw, d;

	if (line < pos)
		STX(w, UW_POS, line);
	else if (!(line < pos + rows))
		STX(w, UW_POS, line - rows + 1);
	cx = ta_width(w, start, LDX(w, UW_VALUE) - start); /* horizontal */
	tw = ta_textw(w);
	d = LDX(w, UW_DRAG);
	if (cx - d < 0) {
		d = cx - 40; /* (un peu de marge à gauche) */
		if (d < 0)
			d = 0;
	} else if (cx - d > tw) {
		d = cx - tw + 40;
	}
	STX(w, UW_DRAG, d);
	uk_ta_sync(w);
}

/* ---- une ligne : texte uni, ou couleurs du code ------------------------- */

/* uk_ta_color : x0 = genre UKC_* -> sa couleur (jetons du thème) */
static long ta_color(long kind)
{
	long t = 110, other; /* (mélange avec TEXTE1 : lisible sur les deux thèmes) */

	if (kind == UKC_COMMENT)
		return JET(TJ_TEXT3);
	if (kind == UKC_STRING) {
		other = JET(TJ_SUCCESS);
	} else if (kind == UKC_NUMBER) {
		other = JET(TJ_WARNING);
		t = 140;
	} else if (kind == UKC_REGISTER) {
		return JET(TJ_ACCENT);
	} else if (kind == UKC_KEYWORD) {
		other = JET(TJ_DANGER);
		t = 90;
	} else if (kind == UKC_LABEL) {
		return JET(TJ_ACCENT_DARK);
	} else if (kind == UKC_PUNCT) {
		return JET(TJ_TEXT2);
	} else {
		return JET(TJ_TEXT1);
	}
	return ui_mix(JET(TJ_TEXT1), other, 256 - t);
}

/* uk_identch : w0 = octet -> 1 s'il fait partie d'un nom (lettre, chiffre,
 * _ . @ $ ?) */
static long identch(long c)
{
	u32 b = (u32)c;
	if (b >= 0x80 || b - '0' < 10 || (b | 0x20) - 'a' < 26)
		return 1;
	if (b == '_' || b == '.' || b == '@' || b == '$' || b == '?')
		return 1;
	return 0;
}

/* uk_reg_prefix, uk_fasm_words : mots à la suite, un vide à la fin */
static const char reg_prefix[] = "xwsdqvbh";
static const char fasm_words[] =
	"include\0macro\0segment\0db\0dh\0dw\0dd\0rb\0"
	"rh\0rw\0rd\0align\0if\0else\0end\0equ\0"
	"format\0entry\0virtual\0repeat\0while\0local\0"
	"struc\0common\0forward\0reverse\0times\0label\0"
	"display\0err\0assert\0readable\0writeable\0"
	"executable\0at\0defined\0used\0file\0match\0"
	"irp\0irps\0rept\0purge\0restore\0fix\0"; /* (+ le NUL final) */

/* uk_is_register : x0 = mot, x1 = longueur -> 1 si c'est un registre AArch64 */
static long is_register(long word, long len)
{
	long a, b, p, i, v;

	if (ULT(len, 2) || UGT(len, 3))
		return 0;
	a = LDB(word, 0) | 0x20;
	b = LDB(word, 1) | 0x20;
	if (len == 2) { /* sp, lr, fp */
		if (a == 's' && b == 'p')
			return 1;
		if (a == 'l' && b == 'r')
			return 1;
		if (a == 'f' && b == 'p')
			return 1;
	} else if (b == 'z') { /* xzr, wzr */
		if ((LDB(word, 2) | 0x20) != 'r')
			return 0;
		return a == 'x' || a == 'w';
	}
	for (p = S(reg_prefix);; p++) { /* x0..x30, w, s, d, q, v, b, h */
		long c = LDB(p, 0);
		if (!c)
			return 0;
		if (c == a)
			break;
	}
	v = 0; /* chiffres, 0..31 */
	for (i = 1; ULT(i, len); i++) {
		u32 d = (u32)LDB(word, i) - '0';
		if (d >= 10)
			return 0;
		v = v * 10 + (long)d;
	}
	return !UGT(v, 31);
}

/* uk_ta_wordkind : x0 = mot, x1 = longueur -> UKC_REGISTER, UKC_KEYWORD ou
 * UKC_TEXT (le widget, x19 dans l'asm : sa fonction de mots-clés,
 * ui_textarea_keywords, sinon les directives de FASMARM) */
static long ta_wordkind(long w, long word, long len)
{
	long fn, r, p, i, c;

	if (!(LDX(w, UW_STYLE) & (UTA_BASIC | UTA_CS))) /* (pas de registres) */
		if (is_register(word, len))
			return UKC_REGISTER;
	fn = LDX(w, UW_KWFN);
	if (fn) {
		/* rappel du programme : x0 = mot, x1 = longueur -> x0 (0 : non) */
		r = UK_CB2(fn, word, len);
	} else if (LDX(w, UW_STYLE) & UTA_CS) {
		r = uk_cs_word(word, len);
	} else {
		r = 0;
		p = S(fasm_words);
		while (LDB(p, 0)) {
			for (i = 0;; i++) {
				c = LDB(p, i);
				if (!c) {
					if (i == len)
						r = 1;
					break;
				}
				if (UGE(i, len))
					break;
				if ((LDB(word, i) | 0x20) != c) /* (sans casse) */
					break;
			}
			if (r)
				break;
			while (LDB(p, 0))
				p++;
			p++;
		}
	}
	return r == 0 ? UKC_TEXT : UKC_KEYWORD;
}

/* uk_ta_piece : x0 = widget, x1 = début, x2 = fin, w3 = couleur : dessine ces
 * octets à leur place, morceau par morceau entre les tabulations ; ls, sx,
 * sy : x20 (début de la ligne), x22 (x du début du texte de la ligne), x23
 * (y) laissés par uk_ta_paint_line */
static void ta_piece(long w, long a, long b, long color, long ls, long sx, long sy)
{
	long t, e, x, n, end;
	ukr f;

	while (ULT(a, b)) {
		t = LDX(w, UW_TEXT); /* un morceau sans tabulation */
		if (LDB(t, a) == 9) {
			a++;
			continue;
		}
		e = a;
		while (ULT(e, b) && LDB(t, e) != 9)
			e++;
		x = sx + ta_width(w, ls, a - ls); /* x : largeur depuis le début de la ligne */
		n = e - a; /* le morceau, terminé par NUL */
		if (UGT(n, UK_LINE_MAX - 1))
			n = UK_LINE_MAX - 1;
		end = uk_memcpy(UKA(linebuf), LDX(w, UW_TEXT) + a, n);
		STB(end, 0, 0);
		f = ta_font(w);
		uk_text_box(UKA(linebuf), x, sy, f.r2, (u32)color, f.r0, 0, f.r1);
		a = e;
	}
}

/* uk_ta_paint_line : x0 = widget, x1 = début, x2 = fin de la ligne, x3 = x du
 * début du texte, x4 = y de la ligne */
static void ta_paint_line(long w, long ls, long le, long sx, long sy)
{
	long style = LDX(w, UW_STYLE), t, p, q, c, c1, kind, i0, i1;

	if (!(style & UTA_CODE)) { /* uni : toute la ligne, TEXTE1 */
		long col = JET(TJ_TEXT1), ink = LDX(w, UW_TA_INK);
		if (ink)
			col = (u32)(ink - 1);
		ta_piece(w, ls, le, col, ls, sx, sy);
		return;
	}
	t = LDX(w, UW_TEXT);
	p = ls; /* position */
	if (style & (UTA_CS | UTA_XML)) {
		/* C#, XML : l'état suit les lignes */
		while (ULT(p, le)) {
			ukr r = uk_lex(t, p, le, LDX(w, UW_TA_LEX), LDX(w, UW_STYLE));
			STX(w, UW_TA_LEX, r.r2);
			q = r.r0;
			if (r.r1 >= 0) {
				kind = r.r1;
				if (kind == UKC_WORD)
					kind = ta_wordkind(w, t + p, q - p);
				ta_piece(w, p, q, ta_color(kind), ls, sx, sy);
			}
			p = q;
		}
		return;
	}
	while (ULT(p, le)) {
		c = LDB(t, p);
		if (c == ' ' || c == 9 || c == 13) {
			p++;
			continue;
		}
		if (LDX(w, UW_STYLE) & UTA_BASIC) {
			/* BASIC : ' ou REM, le reste de la ligne */
			if (c == 0x27)
				goto cmnt;
			if ((c | 0x20) == 'r' && !UGT(p + 3, le) && (LDB(t, p + 1) | 0x20) == 'e' &&
			    (LDB(t, p + 2) | 0x20) == 'm') {
				/* (un mot : début de ligne ou blanc avant, blanc après) */
				c1 = LDB(t, p + 3); /* comme l'asm : lu même à la fin de la ligne */
				if (p != ls) {
					long bf = LDB(t, p - 1);
					if (bf != ' ' && bf != 9 && bf != ':')
						goto bstr;
				}
				if (p + 3 == le)
					goto cmnt;
				if (c1 == ' ' || c1 == 9)
					goto cmnt;
			}
bstr:
			c = LDB(t, p);
			if (c == '"')
				goto quote;
			goto num;
		}
		if (c == ';') /* commentaire : le reste de la ligne */
			goto cmnt;
		if (c == 0x27 || c == '"') /* chaîne : jusqu'au même guillemet */
			goto quote;
num:
		if ((u32)(c - '0') < 10 || c == '#') { /* nombre : chiffres, lettres */
			q = p + 1;
			while (ULT(q, le) && identch(LDB(t, q)))
				q++;
			i0 = p;
			i1 = q;
			p = q;
			kind = UKC_NUMBER;
			goto draw;
		}
		if (identch(c)) { /* mot : registre, mot-clé, étiquette */
			q = p;
			while (ULT(q, le) && identch(LDB(t, q)))
				q++;
			kind = ta_wordkind(w, t + p, q - p);
			if (ULT(q, le) && LDB(t, q) == ':') /* suivi de « : » : une étiquette */
				kind = UKC_LABEL;
			i0 = p;
			i1 = q;
			p = q;
			goto draw;
		}
		i0 = p; /* ponctuation : un caractère */
		p++;
		i1 = p;
		kind = UKC_PUNCT;
		goto draw;
cmnt:
		i0 = p;
		i1 = le;
		p = le;
		kind = UKC_COMMENT;
		goto draw;
quote:
		q = p + 1;
		while (ULT(q, le)) {
			long b = LDB(t, q);
			q++;
			if (b == c)
				break;
		}
		i0 = p;
		i1 = q;
		p = q;
		kind = UKC_STRING;
draw:
		ta_piece(w, i0, i1, ta_color(kind), ls, sx, sy);
	}
}

long uk_ta_draw(long w)
{
	long x = LDSW(w, UW_X), y = LDSW(w, UW_Y);
	long wd = LDW(w, UW_W), ht = LDW(w, UW_H);
	long focus = uk_w_focused(w);
	long c, gut, sela, selb, font, px, lh, bottom, nl, line, sy;
	ukr r;

	c = JET(TJ_LINE_STRONG); /* cadre comme un champ */
	/* (éditeur de code, UTA_LINENUM : sans le bleu du focus) */
	if (focus && !(LDX(w, UW_STYLE) & UTA_LINENUM))
		c = JET(TJ_ACCENT);
	ui_rbox(x, y, wd, ht, 6, c, c);
	c = JET(TJ_SURFACE);
	if (LDX(w, UW_TA_BG)) /* (ui_textarea_paper) */
		c = (u32)(LDX(w, UW_TA_BG) - 1);
	ui_rbox(x + 1, y + 1, wd - 2, ht - 2, 5, c, c);
	gut = ta_gutter(w);
	if (gut) /* marge des numéros : CREUX */
		ui_fill(x + 1, y + 1, gut, ht - 2, JET(TJ_SUNKEN));
	r = ui_textbox_selection(w);
	sela = r.r0;
	selb = r.r1;
	r = ta_font(w);
	font = r.r0;
	px = r.r1;
	lh = r.r2; /* hauteur de ligne */
	bottom = y + ht - UK_TA_PAD - ta_bottom(w).r0; /* bas des lignes */
	nl = ta_nlines(w);
	line = LDX(w, UW_POS);
	sy = y + UK_TA_PAD;
	uk_lex_start(w);
	for (;;) {
		long ls, le, zx, sx;
		if (UGE(line, nl))
			break;
		if (sy + lh > bottom) /* encore la place ? */
			break;
		r = uk_ta_line(w, line);
		ls = r.r0;
		le = r.r1;
		if (gut) { /* numéro de ligne */
			long e = fmt_dec(line + 1, UKA(fitbuf)), tw;
			STB(e, 0, 0);
			tw = ui_text_width(UKA(fitbuf), px, font);
			uk_text_box(UKA(fitbuf), x + (gut - tw - 8), sy, lh, JET(TJ_TEXT3), font, 0, px);
		}
		zx = x + gut + UK_TA_PAD; /* zone du texte : découpe */
		uk_clip_push(zx, sy, ta_textw(w) + 8, lh);
		if (LDX(w, UW_HOT) == line) { /* ligne éclairée (ui_textarea_highlight) */
			long tw = ta_textw(w) + 8;
			c = ui_mix(JET(TJ_SURFACE), JET(TJ_WARNING), 80);
			ui_fill(zx, sy, tw, lh, (u32)c);
		}
		sx = zx - LDX(w, UW_DRAG); /* x du début du texte */
		/* sélection dans cette ligne ? */
		if (sela != selb && !ULT(selb, ls) && !UGT(sela, le)) {
			long a = sela, b = selb, extra = 0, xa, wsel;
			if (ULT(a, ls))
				a = ls;
			if (!ULE(b, le)) { /* (la fin de ligne choisie : 6 px) */
				b = le;
				extra = 6;
			}
			xa = ta_width(w, ls, a - ls);
			wsel = ta_width(w, ls, b - ls) + extra - xa;
			c = JET(TJ_ACCENT_LIGHT);
			if (!focus)
				c = JET(TJ_SUNKEN);
			ui_fill(sx + xa, sy, wsel, lh, c);
		}
		ta_paint_line(w, ls, le, sx, sy); /* le texte de la ligne */
		uk_clip_pop();
		sy += lh;
		line++;
	}
	/* curseur */
	if (!focus || (LDX(w, UW_STYLE) & UTF_READONLY))
		return 0;
	r = ta_line_of(w, LDX(w, UW_VALUE));
	{
		long d = r.r0 - LDX(w, UW_POS), cy, cx;
		if (d < 0)
			return 0;
		cy = d * lh + y + UK_TA_PAD; /* y relatif */
		if (cy + lh > bottom)
			return 0;
		cx = ta_width(w, r.r1, LDX(w, UW_VALUE) - r.r1) - LDX(w, UW_DRAG);
		if (cx < 0)
			return 0;
		if (cx > ta_textw(w))
			return 0;
		ui_fill(x + gut + UK_TA_PAD + cx, cy, 1, lh, JET(TJ_ACCENT));
	}
	return 0;
}

/* x0 = widget, x2, x3 = point local (asm) -> décalage sous ce point */
long uk_ta_hit(long w, long x, long y)
{
	long lh = ta_font(w).r2, d, line, last;

	d = y - UK_TA_PAD;
	if (d < 0)
		d = 0;
	line = UDIV(d, lh) + LDX(w, UW_POS); /* ligne */
	last = ta_nlines(w) - 1;
	if (line > last)
		line = last;
	d = x - ta_gutter(w) - UK_TA_PAD + LDX(w, UW_DRAG);
	STX(w, UW_DRAG2, d); /* (colonne voulue) */
	return ta_offset_at(w, line, d);
}

/* souris : appui (double : un mot), glisser */
long uk_ta_mouse(long w, long kind, long x, long y, long clicks)
{
	long v;

	if (kind == 1) {
		v = uk_ta_hit(w, x, y);
		STX(w, UW_VALUE, v);
		STX(w, UW_ANCHOR, v);
		if (clicks == 2) {
			STX(w, UW_ANCHOR, uk_word_edge(LDX(w, UW_TEXT), v, LDX(w, UW_TLEN), 0));
			STX(w, UW_VALUE, uk_word_edge(LDX(w, UW_TEXT), v, LDX(w, UW_TLEN), 1));
		}
	} else {
		if (kind)
			return 0;
		if (uk.capture != w)
			return 0;
		STX(w, UW_VALUE, uk_ta_hit(w, x, y));
	}
	ta_show_caret(w);
	uk_w_refresh(w);
	return 0;
}

/* x1 = code, x2 = caractère, x3 = mods -> code (-1 : non pris) */
long uk_ta_key(long w, long code, long ch, long mods)
{
	long style = LDX(w, UW_STYLE), res = 0, x, n, p;
	u32 k = (u32)code;
	ukr l;

	if (mods & MOD_CTRL) {
		long redo;
		if (k == 0x1D || k == 0x1C) { /* Ctrl+Z : annuler ; Ctrl+Y : rétablir */
			redo = k == 0x1C;
			if (style & UTF_READONLY)
				return 0;
			if (!uk_undo_step(w, redo))
				return 0;
			res = UN_CHANGE;
			goto show;
		}
		if (k == HID_V) { /* Ctrl+V : plusieurs lignes */
			if (style & UTF_READONLY)
				return 0;
			p = uk_alloc(UK_CLIP_BIG);
			if (!p)
				return 0;
			n = ui_clip_get(p, UK_CLIP_BIG);
			if (n > 0)
				uk_tb_insert(w, p, n);
			uk_free(p);
			res = UN_CHANGE; /* comme l'asm : même sans rien collé */
			goto show;
		}
		if (k == KEY_HOME) { /* Ctrl+Début, Ctrl+Fin : le texte */
			x = 0;
			goto move;
		}
		if (k == KEY_END) {
			x = LDX(w, UW_TLEN);
			goto move;
		}
		/* le reste comme un champ (Ctrl+A, C, X, flèches de mot) */
		res = uk_tb_key(w, code, ch, mods);
		if (res < 0)
			return -1;
		goto show;
	}
	if (k == KEY_UP || k == KEY_DOWN || k == KEY_PGUP || k == KEY_PGDN) {
		/* haut, bas, Page : même colonne */
		long line = ta_line_of(w, LDX(w, UW_VALUE)).r0;
		long rows = ta_rows(w), step = 1;
		if (k == KEY_PGUP || k == KEY_PGDN)
			step = rows;
		if (k == KEY_UP || k == KEY_PGUP) {
			line -= step;
			if (line < 0) { /* avant la première : le début */
				x = 0;
				goto move;
			}
		} else {
			line += step;
			if (!ULT(line, ta_nlines(w))) { /* après la dernière : la fin */
				x = LDX(w, UW_TLEN);
				goto move;
			}
		}
		x = ta_offset_at(w, line, LDX(w, UW_DRAG2));
		goto move2;
	}
	if (k == KEY_HOME) {
		x = ta_line_of(w, LDX(w, UW_VALUE)).r1;
		goto move;
	}
	if (k == KEY_END) {
		l = ta_line_of(w, LDX(w, UW_VALUE));
		x = uk_ta_line(w, l.r0).r1;
		goto move;
	}
	if (k == KEY_ENTER) {
		long src, len;
		if (style & UTF_READONLY)
			return 0;
		if (style & UTA_CODE) { /* code : garde le retrait de la ligne */
			long i = ta_line_of(w, LDX(w, UW_VALUE)).r1;
			long t = LDX(w, UW_TEXT), v = LDX(w, UW_VALUE), j = 1;
			STB(UKA(linebuf), 0, 10);
			while (ULT(i, v)) {
				long b = LDB(t, i);
				if (b != ' ' && b != 9)
					break;
				STB(UKA(linebuf), j, b);
				j++;
				i++;
				if (!ULT(j, 200))
					break;
			}
			src = UKA(linebuf);
			len = j;
		} else {
			STB(UKA(chbuf), 0, 10);
			src = UKA(chbuf);
			len = 1;
		}
		uk_tb_insert(w, src, len);
		res = UN_CHANGE;
		goto show;
	}
	if (k == KEY_TAB) {
		if (style & UTF_READONLY)
			return -1;
		if (style & UTA_TABS) { /* UTA_TABS : une vraie tabulation */
			STB(UKA(chbuf), 0, 9);
			uk_tb_insert(w, UKA(chbuf), 1);
		} else {
			uk_tb_insert(w, S(uk_s_tab), 4);
		}
		res = UN_CHANGE;
		goto show;
	}
	/* flèches gauche / droite, édition : comme un champ */
	res = uk_tb_key(w, code, ch, mods);
	if (res < 0)
		return -1;
	l = ta_line_of(w, LDX(w, UW_VALUE)); /* colonne voulue : la nouvelle */
	STX(w, UW_DRAG2, ta_width(w, l.r1, LDX(w, UW_VALUE) - l.r1));
	goto show;

move: /* colonne voulue : la nouvelle */
	l = ta_line_of(w, x);
	STX(w, UW_DRAG2, ta_width(w, l.r1, x - l.r1));
move2:
	STX(w, UW_VALUE, x);
	if (!(mods & MOD_SHIFT))
		STX(w, UW_ANCHOR, x);
show:
	ta_show_caret(w);
	uk_w_refresh(w);
	return res;
}

/* x1 = crans : 3 lignes */
long uk_ta_wheel(long w, long steps)
{
	STX(w, UW_POS, LDX(w, UW_POS) - steps * 3);
	uk_ta_sync(w);
	uk_w_refresh(w);
	return 0;
}

/* la barre a bougé */
long uk_ta_child(long w, long child, long code)
{
	long v = LDX(child, UW_VALUE);

	(void)code;
	if (child == LDX(w, UW_HBAR))
		STX(w, UW_DRAG, v);
	else
		STX(w, UW_POS, v);
	uk_ta_sync(w);
	uk_w_refresh(w);
	return 0;
}

/* x0 = texte multiligne -> r0 = ligne du curseur, r1 = colonne (octets) */
ukr ui_textarea_caret(long w)
{
	ukr l = ta_line_of(w, LDX(w, UW_VALUE)), r;

	r.r0 = l.r0;
	r.r1 = LDX(w, UW_VALUE) - l.r1;
	r.r2 = LDX(w, UW_VALUE);
	r.r3 = 0;
	return r;
}

/* x0 = texte multiligne, x1 = décalage : y place le curseur (sans sélection)
 * et le rend visible */
long ui_textarea_goto(long w, long off)
{
	long n = LDX(w, UW_TLEN);

	if (UGT(off, n))
		off = n;
	STX(w, UW_VALUE, off);
	STX(w, UW_ANCHOR, off);
	ta_show_caret(w);
	return uk_w_refresh(w);
}

/* ---- éditeur de code : fonctions du programme --------------------------- */

/* x0 = texte multiligne, x1 = ligne éclairée (-1 : aucune) */
long ui_textarea_highlight(long w, long line)
{
	if (!w)
		return 0;
	STX(w, UW_HOT, line);
	return uk_w_refresh(w);
}

/* x0 = texte multiligne, x1 = fonction (x0 = mot, x1 = longueur -> x0 = 1
 * mot-clé ; 0 : les directives de FASMARM) : mots-clés de UTA_CODE */
long ui_textarea_keywords(long w, long fn)
{
	if (!w)
		return 0;
	STX(w, UW_KWFN, fn);
	return w;
}

/* x0 = texte multiligne, x1 = texte : inséré au curseur, à la place de la
 * sélection (une opération pour Annuler) -> UN_CHANGE */
long ui_textarea_insert(long w, long text)
{
	if (!w)
		return 0;
	uk_tb_insert(w, text, uk_strlen(text));
	uk_ta_sync(w);
	ta_show_caret(w);
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* x0 = texte multiligne, x1 = taille de la police en pixels (0 : celle de
 * l'interface) */
long ui_textarea_font(long w, long px)
{
	if (!w)
		return 0;
	STX(w, UW_PX, px);
	uk_ta_sync(w);
	return uk_w_refresh(w);
}

/* uk_ute_keys : Z, Y, X, C, V, A */
static const u8 ute_keys[6] = { 0x1D, 0x1C, HID_X, HID_C, HID_V, HID_A };

/* x0 = texte multiligne, x1 = opération (UTE_UNDO, UTE_REDO, UTE_CUT,
 * UTE_COPY, UTE_PASTE, UTE_ALL) -> UN_CHANGE si le texte a changé */
long ui_textarea_edit(long w, long op)
{
	long r;

	if (!w)
		return 0;
	op--;
	if (UGE(op, 6))
		return 0;
	r = uk_ta_key(w, ute_keys[op], 0, MOD_CTRL);
	return r < 0 ? 0 : r;
}

/* x0 = texte multiligne, x1 = mot (sans casse ASCII) -> 1 trouvé (choisi,
 * montré ; après la sélection, puis depuis le début), 0 sinon */
long ui_textarea_find(long w, long word)
{
	long n = uk_strlen(word), last, at, wrapped = 0, v, a, t, i;

	if (!n)
		return 0;
	last = LDX(w, UW_TLEN) - n; /* dernier départ possible */
	if (last < 0)
		return 0;
	v = LDX(w, UW_VALUE); /* départ : après la sélection */
	a = LDX(w, UW_ANCHOR);
	at = UGT(v, a) ? v : a;
	for (;;) {
		if (UGT(at, last)) {
			if (wrapped)
				return 0;
			wrapped = 1;
			at = 0;
		}
		t = LDX(w, UW_TEXT) + at;
		for (i = 0; ULT(i, n); i++) {
			u32 c1 = (u32)LDB(t, i), c2 = (u32)LDB(word, i);
			if (c1 - 'A' < 26)
				c1 += 32;
			if (c2 - 'A' < 26)
				c2 += 32;
			if (c1 != c2)
				break;
		}
		if (!ULT(i, n))
			break;
		at++;
	}
	STX(w, UW_ANCHOR, at);
	STX(w, UW_VALUE, at + n);
	ta_show_caret(w);
	uk_w_refresh(w);
	return 1;
}

/* x0 = widget, x1 = drapeaux de création : nouveau style ; replace et redessine */
long ui_w_style(long w, long flags)
{
	if (!w)
		return 0;
	STX(w, UW_STYLE, flags);
	if (LDW(w, UW_TYPE) == UT_AREA) {
		ta_bars(w);
		ta_layout_impl(w);
	}
	return uk_w_refresh(w);
}
