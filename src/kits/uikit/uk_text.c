/* uk_text.c -- UIKit : le champ de texte (UT_TEXT), le texte des widgets
 * (ui_w_set_text / ui_w_get_text), le presse-papiers du système (clipd, sinon
 * Elegant). Port ligne à ligne de uikit_text.inc et uikit_clip.inc
 * (src/kits/uikit/PORT.md).
 *
 * Champ : une ligne UTF-8 ; curseur (VALUE, en octets), ancre de la sélection
 * (ANCHOR), défilement horizontal (POS, en pixels), indication (AUX2) ; mot de
 * passe (UTF_PASSWORD : des « • »), lecture seule (UTF_READONLY), police mono
 * (UTF_MONO). Entrée : UN_ACTIVATE ; un changement : UN_CHANGE. Longueur max :
 * MAX (0 : 4096 octets). */
#include "uk.h"

/* ======================================================================== */
/* Presse-papiers par Elegant (WS_OP_CLIP_SET / GET)                         */
/* ======================================================================== */

/* uk_ws_clip_set : x0 = texte, x1 = octets (-1 : jusqu'au NUL) -> 0 (< 0 :
 * erreur) */
long uk_ws_clip_set(long text, long len)
{
	long off, n, r;

	if (len == -1)
		len = uk_strlen(text);
	if (UGT(len, WS_CLIP_MAX))
		len = WS_CLIP_MAX;
	off = 0;
	do {
		n = len - off;
		if (UGT(n, WS_CLIP_PIECE))
			n = WS_CLIP_PIECE;
		STX(uk.msg, WM_A, off);
		STX(uk.msg, WM_B, len);
		STW(uk.msg, WM_LEN, n);
		uk_memcpy(UKA(msg) + WM_DATA, text + off, n);
		r = uk_call(WS_OP_CLIP_SET);
		if (r < 0)
			return r;
		off += n;
	} while (ULT(off, len));
	return 0;
}

/* uk_ws_clip_get : x0 = tampon, x1 = taille du tampon -> octets lus (texte
 * terminé par NUL ; < 0 : erreur) */
long uk_ws_clip_get(long buf, long size)
{
	long max = size - 1; /* (place du NUL) */
	long got = 0, r, n;

	if (max < 0)
		/* comme l'asm : x0 est encore le tampon (taille <= 0 : rend
		 * l'adresse du tampon, 0 s'il est nul) */
		return buf;
	for (;;) {
		if (UGE(got, max))
			break;
		STX(uk.msg, WM_A, got);
		r = uk_call(WS_OP_CLIP_GET);
		if (r < 0) {
			if (buf)
				STB(buf, 0, 0);
			return r;
		}
		if (!r)
			break;
		n = max - got;
		if (!UGT(r, n))
			n = r;
		uk_memcpy(buf + got, UKA(msg) + WM_DATA, n);
		got += n;
		if (!ULT(got, LDX(uk.msg, WM_B))) /* fin du contenu ? */
			break;
	}
	STB(buf, got, 0);
	return got;
}

/* ======================================================================== */
/* Presse-papiers des programmes : service clipd (uikit_clip.inc)            */
/* ======================================================================== */

/* uk_cs_svc .. uk_cs_files */
static const char cs_svc[] = "clipboard";
static const char cs_text[] = "text\0";   /* (une liste : « text », fin) */
static const char cs_image[] = "image\0";
/* uk_cs_flist, suivi de uk_cs_cut : deux formats, « files » puis « files-cut » */
static const char cs_flist[] = "files\0files-cut\0";
#define CS_CUT (S(cs_flist) + 6)
static const char cs_files[] = "files";

/* uk_cd_call : x0 = message : au service clipboard -> résultat (< 0 : pas de
 * service) */
static long cd_call(long msg)
{
	long h = SYS2(SYS_SVC_OPEN, S(cs_svc), 0);
	if (h < 0)
		return h;
	return SYS2(SYS_SVC_CALL, h, msg);
}

/* uk_cd_put : x0 = format, x1 = données, x2 = octets, x3 = copie à compléter
 * (0 : nouvelle), x4 = en-tête de 8 octets (0 : aucun) -> id de la copie (< 0 :
 * refus) ; *x3_out = x3 au retour (le dernier octet lu du format) */
static long cd_put(long fmt, long data, long n, long id, long hdr, long *x3_out)
{
	u8 msg[WS_MSG] __attribute__((aligned(16)));
	long m = (long)(uintptr_t)msg;
	long i = 0, c;

	STX(m, WM_OP, CLIP_PUT);
	STX(m, WM_A, data);
	STX(m, WM_B, n);
	STX(m, WM_C, id);
	STX(m, WM_D, hdr);
	for (;;) {
		c = LDB(fmt, i);
		STB(m + WM_DATA, i, c);
		if (!c)
			break;
		i++;
		if (!ULT(i, CLIP_FMT - 1)) {
			STB(m + WM_DATA, i, 0);
			break;
		}
	}
	if (x3_out)
		*x3_out = c;
	return cd_call(m);
}

/* uk_cd_get : x0 = formats (« a\0b\0\0 »), x1 = destination (0 : la taille
 * seule), x2 = place, x3 = octets à sauter -> r0 = octets écrits (< 0 : pas de
 * service), r1 = octets de la représentation (0 : rien ne convient), r2 = rang
 * du format */
static ukr cd_get(long fmts, long dst, long room, long skip)
{
	u8 msg[WS_MSG] __attribute__((aligned(16)));
	long m = (long)(uintptr_t)msg;
	long i = 0, c;
	ukr r;

	STX(m, WM_OP, CLIP_GET);
	STX(m, WM_A, dst);
	STX(m, WM_B, room);
	STX(m, WM_C, skip);
	for (;;) {
		c = LDB(fmts, i);
		STB(m + WM_DATA, i, c);
		i++;
		if (UGE(i, 400))
			break;
		if (c)
			continue;
		if (!LDB(fmts, i)) /* (une chaîne finie : la suivante ?) */
			break;
	}
	STB(m + WM_DATA, i, 0);
	r.r0 = cd_call(m);
	r.r1 = LDX(m, WM_A);
	r.r2 = LDX(m, WM_B);
	r.r3 = 0;
	return r;
}

/* x0 = texte, x1 = octets (-1 : jusqu'au NUL) -> 0 (< 0 : erreur) */
long ui_clip_set(long text, long len)
{
	if (len == -1)
		len = uk_strlen(text);
	if (cd_put(S(cs_text), text, len, 0, 0, 0) >= 0)
		return 0;
	return uk_ws_clip_set(text, len);
}

/* x0 = tampon, x1 = taille du tampon -> octets lus (texte terminé par NUL ;
 * < 0 : erreur) */
long ui_clip_get(long buf, long size)
{
	if (size >= 1) {
		ukr r = cd_get(S(cs_text), buf, size - 1, 0);
		if (r.r0 >= 0) {
			STB(buf, r.r0, 0);
			return r.r0;
		}
	}
	return uk_ws_clip_get(buf, size);
}

/* x0 = format, x1 = données, x2 = octets, x3 = copie à compléter (0 : nouvelle)
 * -> x0 = id de la copie (0 : refusée ou pas de service), x3 */
ukr ui_clip_put(long fmt, long data, long n, long id)
{
	ukr r;
	long x3 = id;

	r.r0 = cd_put(fmt, data, n, id, 0, &x3);
	if (r.r0 < 0)
		r.r0 = 0;
	r.r1 = 0;
	r.r2 = 0;
	/* comme l'asm : x3 rendu = le dernier octet du format lu par uk_cd_put
	 * (0, sauf un nom de 23 octets ou plus) ; l'appel système garde x3 */
	r.r3 = x3;
	return r;
}

/* x0 = formats (« a\0b\0\0 »), x1 = tampon (0 : la taille seule), x2 = taille
 * -> r0 = octets de la représentation (0 : rien, ou pas de service), r1 = rang
 * du format */
ukr ui_clip_take(long fmts, long buf, long size)
{
	ukr g = cd_get(fmts, buf, size, 0), r;

	r.r2 = 0;
	r.r3 = 0;
	if (g.r0 < 0) {
		r.r0 = 0;
		r.r1 = 0;
		return r;
	}
	r.r0 = g.r1;
	r.r1 = g.r2;
	return r;
}

/* x0 = pixels 0x00RRGGBB, x1 = largeur, x2 = hauteur -> 1 (0 : refusée, ou
 * pas de service) */
long ui_clip_set_image(long px, long w, long h)
{
	long n = (long)((u64)w * (u64)h) << 2;
	long hdr = w | LSL(h, 32);
	return cd_put(S(cs_image), px, n, 0, hdr, 0) > 0;
}

/* x0 = tampon (0 : la taille seule), x1 = taille -> r0 = largeur, r1 = hauteur
 * (0 : pas d'image), r2 = octets des pixels */
ukr ui_clip_get_image(long buf, long size)
{
	u64 hdr = 0;
	long h = (long)(uintptr_t)&hdr;
	ukr r;

	r.r3 = 0;
	if (cd_get(S(cs_image), h, 8, 0).r0 != 8) {
		r.r0 = 0;
		r.r1 = 0;
		r.r2 = 0;
		return r;
	}
	if (buf)
		cd_get(S(cs_image), buf, size, 8);
	r.r0 = LDW(h, 0);
	r.r1 = LDW(h, 4);
	r.r2 = (long)((u64)(u32)r.r0 * (u64)(u32)r.r1) << 2; /* umull */
	return r;
}

/* x0 = chemins (séparés par \n), x1 = 1 : coupés -> 1 (0 : refusée, ou pas
 * de service) */
long ui_clip_set_files(long paths, long cut)
{
	long n = uk_strlen(paths);
	long fmt = cut ? CS_CUT : S(cs_files);
	return cd_put(fmt, paths, n, 0, 0, 0) > 0;
}

/* x0 = tampon, x1 = taille -> r0 = octets des chemins (texte terminé par
 * NUL ; 0 : aucun), r1 = 1 s'ils sont coupés */
ukr ui_clip_get_files(long buf, long size)
{
	ukr g = cd_get(S(cs_flist), buf, size - 1, 0), r;

	r.r2 = 0;
	r.r3 = 0;
	if (g.r0 < 0) {
		r.r0 = 0;
		r.r1 = 0;
		return r;
	}
	STB(buf, g.r0, 0);
	if (!g.r0) {
		r.r0 = 0;
		r.r1 = 0;
		return r;
	}
	r.r0 = g.r0;
	r.r1 = g.r2;
	return r;
}

/* des fichiers coupés ont été déplacés par le collage : la copie s'en va */
long ui_clip_done_cut(void)
{
	u8 msg[WS_MSG] __attribute__((aligned(16)));
	long m = (long)(uintptr_t)msg;

	STX(m, WM_OP, CLIP_DROPCUT);
	return cd_call(m);
}

/* ======================================================================== */
/* Texte d'un widget                                                         */
/* ======================================================================== */

/* uk_text_room : x0 = widget, x1 = octets nécessaires (NUL compris) : agrandit
 * son tampon -> 1 (0 : plus de mémoire) */
static long text_room(long w, long need)
{
	long cap, p, old;

	if (ULE(need, LDX(w, UW_TCAP)))
		return 1;
	cap = need << 1; /* (de la marge) */
	if (ULT(cap, 64))
		cap = 64;
	p = uk_alloc(cap);
	if (!p)
		return 0;
	old = LDX(w, UW_TEXT);
	if (old) {
		uk_memcpy(p, old, LDX(w, UW_TLEN) + 1);
		uk_free(LDX(w, UW_TEXT));
	}
	STX(w, UW_TEXT, p);
	STX(w, UW_TCAP, cap);
	return 1;
}

/* x0 = widget, x1 = texte (0 : vide), x2 = octets (-1 : jusqu'au NUL) : le
 * garde dans son tampon (UW_TEXT, terminé par NUL) -> 1 (0 : mémoire) */
long uk_set_text(long w, long text, long len)
{
	long src = S(uk_empty), e;

	if (text)
		src = text;
	if (len == -1)
		len = uk_strlen(src);
	if (!text_room(w, len + 1))
		return 0;
	e = uk_memcpy(LDX(w, UW_TEXT), src, len);
	STB(e, 0, 0);
	STX(w, UW_TLEN, len);
	return 1;
}

/* x0 = widget, x1 = texte : son texte (champ, étiquette, bouton...), redessine */
long ui_w_set_text(long w, long text)
{
	long t;

	if (!w)
		return 0;
	t = w;
	if (LDW(w, UW_TYPE) == UT_COMBO) { /* liste modifiable : son champ */
		t = LDX(w, UW_AUX2);
		if (!t)
			return w; /* comme l'asm : x0 est encore la liste */
	}
	uk_set_text(t, text, -1);
	STX(t, UW_VALUE, LDX(t, UW_TLEN)); /* champ : curseur à la fin, sans sélection */
	STX(t, UW_ANCHOR, LDX(t, UW_TLEN));
	STX(t, UW_POS, 0);
	if (LDW(t, UW_TYPE) == UT_AREA) { /* texte multiligne : au début */
		STX(t, UW_VALUE, 0);
		STX(t, UW_ANCHOR, 0);
		STX(t, UW_DRAG, 0);
		uk_undo_clear(t);
		uk_ta_sync(t);
	}
	return uk_w_refresh(t);
}

/* x0 = widget -> longueur de son texte en octets */
long ui_w_text_len(long w)
{
	if (!w)
		return 0;
	if (LDW(w, UW_TYPE) == UT_COMBO) {
		w = LDX(w, UW_AUX2);
		if (!w)
			return 0;
	}
	return LDX(w, UW_TLEN);
}

/* x0 = widget -> son texte (tampon du kit ; jamais 0) */
long ui_w_get_text(long w)
{
	long t;

	if (!w)
		return S(uk_empty);
	if (LDW(w, UW_TYPE) == UT_COMBO) { /* liste modifiable : son champ */
		w = LDX(w, UW_AUX2);
		if (!w)
			return S(uk_empty);
	}
	t = LDX(w, UW_TEXT);
	if (!t)
		return S(uk_empty);
	return t;
}

/* ======================================================================== */
/* Champ de texte                                                            */
/* ======================================================================== */

/* x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = drapeaux (UTF_*) -> poignée */
long ui_textbox(long x, long y, long wd, long ht, long flags)
{
	long w = uk_w_new(UT_TEXT, x, y, wd, ht);
	if (!w)
		return 0;
	STX(w, UW_STYLE, flags);
	uk_set_text(w, 0, 0);
	return w;
}

/* x0 = champ, x1 = indication (montrée quand il est vide) */
long ui_textbox_hint(long w, long hint)
{
	long p;

	if (!w)
		return 0;
	p = uk_strdup(hint);
	uk_free(LDX(w, UW_AUX2));
	STX(w, UW_AUX2, p);
	return uk_w_refresh(w);
}

/* x0 = champ, x1 = début, x2 = fin (octets ; -1 : la fin du texte) : choisit
 * ce texte (curseur à la fin) */
long ui_textbox_select(long w, long a, long b)
{
	long n;

	if (!w)
		return 0;
	n = LDX(w, UW_TLEN);
	if (UGT(a, n))
		a = n;
	if (UGT(b, n))
		b = n;
	STX(w, UW_ANCHOR, a);
	STX(w, UW_VALUE, b);
	return uk_w_refresh(w);
}

/* x0 = champ -> r0 = début, r1 = fin de la sélection (octets) */
ukr ui_textbox_selection(long w)
{
	long v = LDX(w, UW_VALUE), a = LDX(w, UW_ANCHOR);
	ukr r;

	r.r0 = ULT(v, a) ? v : a;
	r.r1 = ULT(v, a) ? a : v;
	r.r2 = a;
	r.r3 = 0;
	return r;
}

/* x0 = champ -> r0 = police, r1 = taille px */
ukr uk_tb_font(long w)
{
	long m = uk_met();
	long px = LDX(m, MT_PX);
	long p = LDX(w, UW_PX); /* taille du programme (texte multiligne) */
	ukr r;

	if (p)
		px = p;
	if (LDX(w, UW_STYLE) & UTF_MONO)
		r.r0 = uk_font_m();
	else
		r.r0 = uk_font_r();
	r.r1 = px;
	r.r2 = 0;
	r.r3 = 0;
	return r;
}

/* uk_tb_disp : x0 = champ -> texte montré (le texte, ou des « • » pour un mot
 * de passe) */
static long tb_disp(long w)
{
	long n, i, d, t, c;

	if (!(LDX(w, UW_STYLE) & UTF_PASSWORD))
		return LDX(w, UW_TEXT);
	n = LDX(w, UW_TLEN); /* 3 octets par point de code */
	i = 0;
	d = UKA(pwbuf);
	t = LDX(w, UW_TEXT);
	while (ULT(i, n)) {
		c = LDB(t, i);
		i++;
		if ((c & 0xC0) == 0x80)
			continue;
		if (UGE(d, UKA(pwbuf) + UK_PW_MAX - 4))
			break;
		STB(d, 0, 0xE2); /* « • » U+2022 */
		STB(d, 1, 0x80);
		STB(d, 2, 0xA2);
		d += 3;
	}
	STB(d, 0, 0);
	return UKA(pwbuf);
}

/* uk_tb_to_disp : x0 = champ, x1 = décalage dans le texte -> décalage dans le
 * texte montré */
static long tb_to_disp(long w, long off)
{
	long t, i, r, c;

	if (!(LDX(w, UW_STYLE) & UTF_PASSWORD))
		return off;
	t = LDX(w, UW_TEXT);
	i = 0;
	r = 0;
	while (ULT(i, off)) {
		c = LDB(t, i);
		i++;
		if ((c & 0xC0) == 0x80)
			continue;
		r += 3;
	}
	return r;
}

/* uk_tb_from_disp : x0 = champ, x1 = décalage dans le texte montré ->
 * décalage dans le texte */
static long tb_from_disp(long w, long off)
{
	long t, n, i;

	if (!(LDX(w, UW_STYLE) & UTF_PASSWORD))
		return off;
	off = UDIV(off, 3); /* points de code */
	t = LDX(w, UW_TEXT);
	n = LDX(w, UW_TLEN);
	for (i = 0; ULT(i, n); i++) {
		if ((LDB(t, i) & 0xC0) == 0x80)
			continue;
		if (!off)
			return i;
		off--;
	}
	return i;
}

/* uk_tb_xof : x0 = champ, x1 = décalage dans le texte -> x en pixels depuis
 * le début du texte */
static long tb_xof(long w, long off)
{
	long d = tb_to_disp(w, off);
	long s = tb_disp(w);
	ukr f = uk_tb_font(w);
	return ui_text_prefix(s, d, f.r1, f.r0);
}

/* uk_tb_scroll : x0 = champ : défilement pour que le curseur soit visible */
static void tb_scroll(long w)
{
	long x = tb_xof(w, LDX(w, UW_VALUE));
	long room = LDW(w, UW_W) - 22; /* largeur utile */
	long pos = LDX(w, UW_POS);
	long d = x - pos;

	if (d < 0)
		pos = x;
	else if (d > room)
		pos = x - room;
	STX(w, UW_POS, pos);
}

/* uk_tb_x0 : x0 = champ -> x du début de son texte (10 ; UTF_RIGHT : collé à
 * droite tant qu'il tient) */
static long tb_x0(long w)
{
	long x;

	if (!(LDX(w, UW_STYLE) & UTF_RIGHT))
		return 10;
	x = tb_xof(w, LDX(w, UW_TLEN));
	x = (LDW(w, UW_W) - 10) - x;
	if (x < 10)
		x = 10;
	return x;
}

long uk_tb_draw(long w)
{
	long x = LDSW(w, UW_X), y = LDSW(w, UW_Y);
	long wd = LDW(w, UW_W), ht = LDW(w, UW_H);
	long focus = uk_w_focused(w);
	long enabled = uk_w_enabled(w);
	long c, tx, s;
	ukr f;

	c = JET(TJ_LIGNE_FORTE); /* contour : LIGNE_FORTE, ACCENT (focus) */
	if (focus)
		c = JET(TJ_ACCENT);
	if (LDX(w, UW_STYLE) & UTF_ERROR) /* UTF_ERROR : DANGER, focus ou non */
		c = JET(TJ_DANGER);
	ui_rbox(x, y, wd, ht, 8, c, c);
	c = JET(TJ_SURFACE); /* intérieur : SURFACE, FACE (grisé) */
	if (!enabled)
		c = JET(TJ_FACE);
	ui_rbox(x + 1, y + 1, wd - 2, ht - 2, 7, c, c);
	uk_clip_push(x + 8, y + 2, wd - 16, ht - 4); /* zone du texte */
	if (focus) /* (sans focus : le défilement reste) */
		tb_scroll(w);
	tx = x + tb_x0(w);
	tx -= LDX(w, UW_POS); /* x du début du texte */
	if (LDX(w, UW_TLEN)) {
		ukr sel = ui_textbox_selection(w); /* sélection */
		if (sel.r0 != sel.r1) {
			long xa = tb_xof(w, sel.r0);
			long xb = tb_xof(w, sel.r1);
			c = JET(TJ_ACCENT_CLAIR);
			if (!focus)
				c = JET(TJ_CREUX);
			ui_fill(tx + xa, y + 4, xb - xa, ht - 8, c);
		}
		s = tb_disp(w);
		f = uk_tb_font(w);
		c = JET(TJ_TEXTE1);
		if (!enabled)
			c = JET(TJ_TEXTE3);
		uk_text_box(s, tx, y, ht, c, f.r0, 0, f.r1);
	} else {
		s = LDX(w, UW_AUX2); /* vide : l'indication */
		if (s) {
			f = uk_tb_font(w);
			uk_text_box(s, x + 10, y, ht, JET(TJ_TEXTE3), f.r0, 0, f.r1);
		}
	}
	/* curseur : 1 x 16, ACCENT (lecture seule : sans curseur) */
	if (focus && !(LDX(w, UW_STYLE) & UTF_READONLY)) {
		long cx = tx + tb_xof(w, LDX(w, UW_VALUE));
		ui_fill(cx, y + ASR(ht - 16, 1), 1, 16, JET(TJ_ACCENT));
	}
	return uk_clip_pop();
}

/* x0 = champ, x1 = x local -> décalage dans le texte sous ce point */
long uk_tb_hit(long w, long x)
{
	long x0 = tb_x0(w), s;
	ukr f;

	x = x + LDX(w, UW_POS) - x0;
	if (x < 0)
		x = 0;
	s = tb_disp(w);
	f = uk_tb_font(w);
	return tb_from_disp(w, ui_text_hit(s, f.r1, f.r0, x).r0);
}

/* x0 = texte, x1 = décalage, x2 = longueur -> début du point de code suivant */
long uk_u8_next(long text, long off, long len)
{
	if (UGE(off, len))
		return off;
	off++;
	while (ULT(off, len) && (LDB(text, off) & 0xC0) == 0x80)
		off++;
	return off;
}

/* uk_u8_prev : x0 = texte, x1 = décalage -> début du point de code précédent */
static long u8_prev(long text, long off)
{
	if (!off)
		return 0;
	off--;
	while (off && (LDB(text, off) & 0xC0) == 0x80)
		off--;
	return off;
}

/* uk_wordch : w0 = octet -> 1 s'il fait partie d'un mot (lettre, chiffre, _,
 * UTF-8) */
static long wordch(long c)
{
	u32 b = (u32)c;
	if (b >= 0x80 || b == '_' || b - '0' < 10 || (b | 0x20) - 'a' < 26)
		return 1;
	return 0;
}

/* x0 = texte, x1 = décalage, x2 = longueur, x3 = 1 vers la droite -> bord de
 * mot (Ctrl + flèches, double-clic) */
long uk_word_edge(long text, long off, long len, long right)
{
	if (right) { /* droite : les séparateurs, puis le mot */
		while (ULT(off, len) && !wordch(LDB(text, off)))
			off++;
		while (ULT(off, len) && wordch(LDB(text, off)))
			off++;
		return off;
	}
	/* gauche : les séparateurs, puis le mot */
	while (off && !wordch(LDB(text, off - 1)))
		off--;
	while (off && wordch(LDB(text, off - 1)))
		off--;
	return off;
}

/* x1 = genre, x2, x3 = point local, x4 = clics */
long uk_tb_mouse(long w, long kind, long x, long y, long clicks)
{
	long v;

	(void)y;
	if (kind == 1) {
		v = uk_tb_hit(w, x);
		STX(w, UW_VALUE, v);
		STX(w, UW_ANCHOR, v);
		if (clicks == 2) { /* double-clic : le mot */
			STX(w, UW_ANCHOR, uk_word_edge(LDX(w, UW_TEXT), v, LDX(w, UW_TLEN), 0));
			STX(w, UW_VALUE, uk_word_edge(LDX(w, UW_TEXT), v, LDX(w, UW_TLEN), 1));
		}
	} else {
		if (kind) /* relâché, sortie */
			return 0;
		if (uk.capture != w) /* déplacement : bouton tenu ? */
			return 0;
		STX(w, UW_VALUE, uk_tb_hit(w, x));
	}
	uk_w_refresh(w);
	return 0;
}

/* uk_tb_del_sel : x0 = champ : efface la sélection -> 1 s'il y en avait une */
static long tb_del_sel(long w)
{
	ukr s = ui_textbox_selection(w);
	if (s.r0 == s.r1)
		return 0;
	uk_tb_delete(w, s.r0, s.r1);
	return 1;
}

/* x0 = champ, x1 = début, x2 = fin : retire ces octets ; curseur au début */
long uk_tb_delete(long w, long start, long end)
{
	long t, n, r;

	/* (annuler : les octets retirés) */
	uk_undo_rec(w, 2, LDX(w, UW_TEXT) + start, end - start);
	t = LDX(w, UW_TEXT);
	n = end - start; /* octets retirés */
	r = uk_memcpy(t + start, t + end, LDX(w, UW_TLEN) - end + 1); /* (NUL compris) */
	STX(w, UW_TLEN, LDX(w, UW_TLEN) - n);
	STX(w, UW_VALUE, start);
	STX(w, UW_ANCHOR, start);
	return r; /* comme l'asm : x0 de uk_memcpy */
}

/* x0 = champ, x1 = texte, x2 = octets : insère au curseur (la sélection
 * remplacée) -> 1 (0 : trop long) */
long uk_tb_insert(long w, long text, long len)
{
	long max, n, t, v;

	tb_del_sel(w); /* comme l'asm : la sélection part même si c'est trop long */
	max = LDX(w, UW_MAX); /* longueur max (0 : selon le type) */
	if (!max) {
		max = UK_TEXT_MAX;
		if (LDW(w, UW_TYPE) == UT_AREA)
			max = UK_AREA_MAX;
	}
	n = LDX(w, UW_TLEN) + len;
	if (UGT(n, max))
		return 0;
	if (!text_room(w, n + 1))
		return 0;
	t = LDX(w, UW_TEXT); /* la fin monte */
	v = LDX(w, UW_VALUE);
	uk_memcpy_up(t + v + len, t + v, LDX(w, UW_TLEN) - v + 1);
	uk_memcpy(LDX(w, UW_TEXT) + v, text, len);
	STX(w, UW_TLEN, LDX(w, UW_TLEN) + len);
	STX(w, UW_VALUE, v + len);
	STX(w, UW_ANCHOR, v + len);
	/* (annuler : les octets insérés) */
	uk_undo_rec_at(w, 1, LDX(w, UW_TEXT) + v, len, v);
	return 1;
}

/* uk_tb_copy : x0 = champ : copie la sélection dans le presse-papiers (mot de
 * passe : non) */
static void tb_copy(long w)
{
	ukr s;

	if (LDX(w, UW_STYLE) & UTF_PASSWORD)
		return;
	s = ui_textbox_selection(w);
	if (s.r1 - s.r0 == 0)
		return;
	ui_clip_set(LDX(w, UW_TEXT) + s.r0, s.r1 - s.r0);
}

/* uk_utf8_put : w0 = point de code, x1 = destination -> octets écrits (1 à 4) */
static long tb_utf8_put(long cp, long d)
{
	u32 c = (u32)cp;

	if (c < 0x80) {
		STB(d, 0, c);
		return 1;
	}
	if (c < 0x800) {
		STB(d, 0, (c >> 6) | 0xC0);
		STB(d, 1, (c & 0x3F) | 0x80);
		return 2;
	}
	if (c < 0x10000) {
		STB(d, 0, (c >> 12) | 0xE0);
		STB(d, 1, ((c >> 6) & 0x3F) | 0x80);
		STB(d, 2, (c & 0x3F) | 0x80);
		return 3;
	}
	/* comme l'asm : au-delà de U+10FFFF, l'octet de tête garde les bits hauts */
	STB(d, 0, (c >> 18) | 0xF0);
	STB(d, 1, ((c >> 12) & 0x3F) | 0x80);
	STB(d, 2, ((c >> 6) & 0x3F) | 0x80);
	STB(d, 3, (c & 0x3F) | 0x80);
	return 4;
}

/* x1 = code HID, x2 = caractère, x3 = mods -> code (-1 : non pris) */
long uk_tb_key(long w, long code, long ch, long mods)
{
	long style = LDX(w, UW_STYLE); /* bit 1 : lecture seule */
	long res = 0;                  /* code */
	long text, v, n, x;
	u32 k = (u32)code, c = (u32)ch;
	ukr s;

	if (mods & MOD_CTRL) {
		if (k == HID_A) { /* Ctrl+A : tout */
			STX(w, UW_ANCHOR, 0);
			STX(w, UW_VALUE, LDX(w, UW_TLEN));
			goto draw;
		}
		if (k == HID_C) {
			tb_copy(w);
			goto zero;
		}
		if (k == HID_X) {
			if (style & UTF_READONLY)
				goto zero;
			tb_copy(w);
			if (LDX(w, UW_STYLE) & UTF_PASSWORD) /* (mot de passe : ni copie, ni coupe) */
				goto zero;
			if (!tb_del_sel(w))
				goto zero;
			goto changed;
		}
		if (k == HID_V) {
			long i;
			if (style & UTF_READONLY)
				goto zero;
			/* une ligne : arrêt à la fin de ligne */
			n = ui_clip_get(UKA(pastebuf), UK_PASTE_MAX);
			if (n <= 0)
				goto zero;
			for (i = 0; ULT(i, n); i++) {
				long b = LDB(UKA(pastebuf), i);
				if (b == 10 || b == 13)
					break;
			}
			uk_tb_insert(w, UKA(pastebuf), i);
			goto changed;
		}
	}
	/* nav */
	v = LDX(w, UW_VALUE);
	text = LDX(w, UW_TEXT);
	n = LDX(w, UW_TLEN);
	if (k == KEY_LEFT) {
		if (mods & MOD_CTRL) {
			x = uk_word_edge(text, v, n, 0);
			goto move;
		}
		if (!(mods & MOD_SHIFT)) { /* sans Maj, une sélection : son début */
			s = ui_textbox_selection(w);
			if (s.r0 != s.r1) {
				x = s.r0;
				goto move;
			}
			v = LDX(w, UW_VALUE);
		}
		x = u8_prev(text, v);
		goto move;
	}
	if (k == KEY_RIGHT) {
		if (mods & MOD_CTRL) {
			x = uk_word_edge(text, v, n, 1);
			goto move;
		}
		if (!(mods & MOD_SHIFT)) {
			s = ui_textbox_selection(w);
			if (s.r0 != s.r1) {
				x = s.r1;
				goto move;
			}
			v = LDX(w, UW_VALUE);
		}
		x = uk_u8_next(text, v, n);
		goto move;
	}
	if (k == KEY_HOME) {
		x = 0;
		goto move;
	}
	if (k == KEY_END) {
		x = n;
		goto move;
	}
	if (k == KEY_ENTER)
		return UN_ACTIVATE; /* (sans redessin) */
	if (style & UTF_READONLY) { /* lecture seule : rien à modifier */
		if (c >= ' ')
			goto zero;
		return -1;
	}
	if (k == KEY_BACKSPACE) {
		if (tb_del_sel(w))
			goto changed;
		v = LDX(w, UW_VALUE);
		if (!v)
			goto zero;
		x = u8_prev(LDX(w, UW_TEXT), v);
		uk_tb_delete(w, x, LDX(w, UW_VALUE));
		goto changed;
	}
	if (k == KEY_DELETE) {
		if (tb_del_sel(w))
			goto changed;
		v = LDX(w, UW_VALUE);
		n = LDX(w, UW_TLEN);
		if (UGE(v, n))
			goto zero;
		x = uk_u8_next(LDX(w, UW_TEXT), v, n);
		uk_tb_delete(w, LDX(w, UW_VALUE), x);
		goto changed;
	}
	/* un caractère : inséré (UTF-8) */
	if (c < ' ' || c == 0x7F || (mods & MOD_CTRL))
		return -1;
	n = tb_utf8_put(c, UKA(chbuf));
	if (!uk_tb_insert(w, UKA(chbuf), n))
		goto zero;
	goto changed;

move:
	STX(w, UW_VALUE, x); /* Maj : la sélection s'étend */
	if (!(mods & MOD_SHIFT))
		STX(w, UW_ANCHOR, x);
	goto draw;
changed:
	res = UN_CHANGE;
draw:
zero: /* comme l'asm : .zero redessine aussi */
	uk_w_refresh(w);
	return res;
}
