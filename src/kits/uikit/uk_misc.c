/* uk_misc.c -- UIKit : curseur, infobulles, glisser-déposer, localisation
 * (TR), menus (raccourcis traités par le kit, entrées grisées ou cochées),
 * polices du kit (uikit_misc.inc). Port ligne à ligne de l'assembleur
 * (src/kits/uikit/PORT.md). */
#include "uk.h"

/* ---------------------------------------------------------------------------
 *  Forme du curseur
 * ------------------------------------------------------------------------- */

/* x0 = forme (UC_*) : pour le contenu de la fenêtre courante */
long ui_cursor(long a0)
{
	if (uk.cur_shape == a0)
		return a0;
	uk.cur_shape = a0;
	STX(uk.msg, WM_A, uk_win());
	STX(uk.msg, WM_B, a0);
	return uk_call(WS_OP_CURSOR);
}

/* x0 = 1 : la fenêtre prend le pointeur (souris relative, WEV_M_REL), 0 : elle le rend */
long ui_pointer_lock(long a0)
{
	STX(uk.msg, WM_A, uk_win());
	STX(uk.msg, WM_B, a0);
	return uk_call(WS_OP_POINTER_LOCK);
}

/* x0 = widget, x1 = forme (UC_* ; -1 : celle de son type) */
long ui_w_cursor(long a0, long a1)
{
	if (a0)
		STX(a0, UW_CURSOR, a1 + 1);
	return a0;
}

/* x0 = widget (0 : aucun) -> forme du curseur sur lui */
long uk_w_shape(long w)
{
	if (!w)
		return UC_ARROW;
	long c = LDX(w, UW_CURSOR);
	if (c)
		return c - 1;
	u32 t = (u32)LDW(w, UW_TYPE);
	if (t == UT_TEXT || t == UT_AREA) {
		if (LDX(w, UW_STYLE) & 2)    /* (texte en lecture seule : flèche) */
			return UC_ARROW;
		return UC_TEXT;
	}
	if (t != UT_SPLIT)
		return UC_ARROW;
	/* séparateur : selon son sens (cmp w, signé) */
	if ((s32)LDW(w, UW_H) > (s32)LDW(w, UW_W))
		return UC_HRESIZE;
	return UC_VRESIZE;
}

/* ---------------------------------------------------------------------------
 *  Infobulles
 * ------------------------------------------------------------------------- */

/* x0 = widget, x1 = texte (0 : aucune) : son infobulle */
long ui_w_tip(long a0, long a1)
{
	if (!a0)
		return 0;
	long t = 0;
	if (a1)
		t = uk_strdup(ui_tr(a1));
	long r = uk_free(LDX(a0, UW_TIP));
	STX(a0, UW_TIP, t);
	return r;
}

/* uk_tip_show : x0 = texte : l'infobulle, sous le pointeur (fenêtre sans
 * cadre, au-dessus), à uk_mx + (12, 20) dans le contenu */
static long tip_show(long text)
{
	long small = LDX(uk_met(), MT_SMALL);   /* taille : texte + 16, ligne de 24 */
	long font = uk_font_r();
	long w = ui_text_width(text, small, font) + 16;
	if (UGT(w, 400))
		w = 400;
	long h = 24;
	ukr p = uk_win_pos();                   /* place : pointeur + (12, 20) */
	long x = p.r0 + uk.mx[0] + 12;
	long y = p.r1 + uk.mx[1] + 20;
	long id = uk_create(x, y, w, h, S(uk_menu_title), WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST);
	if (id < 0)
		return id;
	long buf = LDX(uk.msg, WM_A);           /* son tampon */
	long px = SYS1(SYS_BUF_MAP, buf);
	if (px < 0) {
		STX(uk.msg, WM_A, id);
		return uk_call(WS_OP_CLOSE);
	}
	uk_target(px, w, h);
	ui_fill(0, 0, w, h, JET(TJ_TEXT1));    /* fond TEXTE1, texte SURFACE (inverse) */
	small = LDX(uk_met(), MT_SMALL);
	font = uk_font_r();
	uk_text_box(text, 8, 0, h, JET(TJ_SURFACE), font, w - 16, small);
	uk_target_back();
	u8 *m = uk.msg;                         /* montre */
	STX(m, WM_A, id);
	STX(m, WM_B, 0);
	STX(m, WM_C, 0);
	STX(m, WM_D, w);
	STX(m, WM_DATA, h);
	uk_call(WS_OP_UPDATE);
	uk.tip_win[0] = id;
	uk.tip_win[1] = buf;
	return UKA(tip_win);
}

/* uk_idle_in */
static long idle_in(void)
{
	if (uk.tip_win[0])                     /* déjà montrée */
		return uk.tip_win[0];
	if (uk.capture)                        /* (bouton tenu : non) */
		return uk.capture;
	long w = uk.hover;
	/* comme l'asm : sans widget survolé ou sans infobulle, x0 reste
	 * l'adresse de uk_hover */
	if (!w)
		return UKA(hover);
	long tip = LDX(w, UW_TIP);
	if (!tip)
		return UKA(hover);
	long r = uk_w_shown(w);
	if (!r)
		return 0;
	r = SYS0(SYS_TIME_MS) - uk.hover_t;
	if (ULT(r, UK_TIP_MS))
		return r;
	return tip_show(tip);
}

/* à appeler quand ui_event n'a rien rendu (attente écoulée) : montre
 * l'infobulle du widget survolé depuis UK_TIP_MS */
long ui_w_idle(void)
{
	long win = uk_route(uk.mwin);           /* la fenêtre sous la souris */
	long r = idle_in();
	if (win)
		r = ui_window_use(win);
	return r;
}

/* x0, x1 = point de la fenêtre, x2 = texte (0 : retire) : une infobulle (coin
 * haut gauche à ce point), sans attendre ; une nouvelle remplace l'ancienne */
long ui_tip_at(long a0, long a1, long a2)
{
	long r = uk_tip_hide();
	if (!a2)
		return r;
	uk.mx[0] = a0 - 12;                     /* (uk_tip_show : pointeur + 12, 20) */
	uk.mx[1] = a1 - 20;
	return tip_show(a2);
}

/* ferme l'infobulle montrée (s'il y en a une) */
long uk_tip_hide(void)
{
	long id = uk.tip_win[0], buf = uk.tip_win[1];
	if (!id)
		return UKA(tip_win);
	uk.tip_win[0] = 0;
	uk.tip_win[1] = 0;
	STX(uk.msg, WM_A, id);
	uk_call(WS_OP_CLOSE);
	return SYS1(SYS_BUF_FREE, buf);
}

/* ---------------------------------------------------------------------------
 *  Glisser-déposer
 * ------------------------------------------------------------------------- */

/* x0 = données, x1 = octets (-1 : jusqu'au NUL ; au plus WS_DRAG_MAX), x2 =
 * genre (UDT_TEXT, UDT_FILES) : commence un glisser -> x0 = 0 */
long ui_drag_start(long a0, long a1, long a2)
{
	long n = a1;
	if (n == -1)
		n = uk_strlen(a0);
	if (UGT(n, WS_DRAG_MAX))
		n = WS_DRAG_MAX;
	STX(uk.msg, WM_A, a2);
	STW(uk.msg, WM_LEN, n);
	uk_memcpy((long)(uintptr_t)(uk.msg + WM_DATA), a0, n);
	return uk_call(WS_OP_DRAG);
}

/* x0, x1, x2, x3 = rectangle du contenu de la fenêtre courante (l'élément glissé), x4, x5 = son
 * point sous le curseur : après ui_drag_start, Elegant le dessine en transparence sous le curseur
 * jusqu'au relâché (WS_OP_DRAG_IMAGE) -> 0 */
long ui_drag_image(long a0, long a1, long a2, long a3, long a4, long a5)
{
	STX(uk.msg, WM_A, uk_win());
	STX(uk.msg, WM_B, (a0 & 0xFFFF) | (a1 & 0xFFFF) << 16 | (a2 & 0xFFFF) << 32 | (a3 & 0xFFFF) << 48);
	STX(uk.msg, WM_C, (a4 & 0xFFFFFFFF) | (a5 & 0xFFFFFFFF) << 32);
	STW(uk.msg, WM_LEN, 0);
	return uk_call(WS_OP_DRAG_IMAGE);
}

/* x0 = tampon, x1 = taille -> x0 = octets du dernier dépôt (texte terminé
 * par NUL), x1 = son genre */
ukr ui_drop_get(long a0, long a1)
{
	ukr r;
	long n = uk_call(WS_OP_DROP_GET);
	if (n < 0) {
		STB(a0, 0, 0);
		r.r0 = 0;
		r.r1 = 0;
		r.r2 = 0;
		r.r3 = 0;
		return r;
	}
	long max = a1 - 1;   /* comme l'asm : taille 0 -> -1, aucune borne (lo) */
	if (ULT(n, max))
		max = n;
	long end = uk_memcpy(a0, (long)(uintptr_t)(uk.msg + WM_DATA), max);
	STB(end, 0, 0);
	r.r0 = max;
	r.r1 = LDX(uk.msg, WM_A);
	r.r2 = max;
	r.r3 = 0;
	return r;
}

/* (ui_w_event) événement WEV_DROP (x19 de l'asm) : un texte déposé sur un
 * champ ou un texte multiligne y est inséré au point de dépôt -> r0 = widget
 * dessous (0 : aucun), r1 = code (UN_CHANGE : inséré ; UN_DROP : au programme) */
ukr uk_on_drop(long ev)
{
	ukr r;
	r.r2 = 0;
	r.r3 = 0;
	long x = LDSW(ev, EV_X);
	long y = LDSW(ev, EV_Y);
	long w = uk_w_at(x, y, LDW(ev, EV_WIN));
	if (!w) {
		r.r0 = 0;
		r.r1 = 0;
		return r;
	}
	r.r0 = w;
	r.r1 = UN_DROP;
	if ((u32)LDW(ev, 16) != UDT_TEXT)       /* un texte ? */
		return r;
	u32 t = (u32)LDW(w, UW_TYPE);
	if (t != UT_TEXT && t != UT_AREA)
		return r;
	if (LDX(w, UW_STYLE) & 2)               /* (lecture seule) */
		return r;
	/* comme l'asm : le bit 0 (mot de passe) est testé sans effet */
	long px = x - SXTW(LDW(w, UW_X));       /* curseur au point de dépôt */
	long py = y - SXTW(LDW(w, UW_Y));
	long pos;
	if ((u32)LDW(w, UW_TYPE) == UT_AREA)
		pos = uk_ta_hit(w, px, py);
	else
		pos = uk_tb_hit(w, px);
	STX(w, UW_VALUE, pos);
	STX(w, UW_ANCHOR, pos);
	long n = ui_drop_get(UKA(pastebuf), UK_PASTE_MAX).r0;
	if (!n)
		return r;
	uk_tb_insert(w, UKA(pastebuf), n);
	ui_w_focus(w);
	uk_w_refresh(w);
	r.r1 = UN_CHANGE;
	return r;
}

/* ---------------------------------------------------------------------------
 *  Localisation : TR (AppKit, lang.inc : la langue vaut pour tout programme)
 * ------------------------------------------------------------------------- */

/* x0 = texte -> x0 = sa traduction (ou le texte) */
long ui_tr(long a0)
{
	return lang_tr(a0);
}

/* x0 = chemin d'un dictionnaire du programme -> x0 = 1 si lu */
long ui_tr_load(long a0)
{
	return lang_load(a0);
}

/* -> x0 = la langue */
long ui_lang(void)
{
	return lang_get();
}

/* x4 = éléments "a|b|c" -> les mêmes traduits, dans uk.tr_buf (un seul texte
 * en cours : à recopier avant le suivant). En C : élément en argument, rendu. */
long uk_tr_items(long items)
{
	if (!items)
		return items;
	u8 *s = (u8 *)(uintptr_t)items;
	u8 *d = uk.tr_buf;
	u8 *lim = uk.tr_buf + UK_TR_ITEMS - 2;
	for (;;) {
		u8 word[256];
		long n = 0;
		for (;;) {
			u8 c = *s;
			if (!c || c == '|')
				break;
			s++;
			if (n >= 255)
				continue;
			word[n++] = c;
		}
		word[n] = 0;
		u8 *t = (u8 *)(uintptr_t)lang_tr((long)(uintptr_t)word);
		for (;;) {
			u8 c = *t++;
			if (!c)
				break;
			if (d >= lim)
				continue;
			*d++ = c;
		}
		if (!*s)
			break;
		s++;
		/* comme l'asm : le séparateur est écrit sans borne (au-delà de
		 * uk.tr_buf si les éléments traduits le remplissent) */
		*d++ = '|';
	}
	*d = 0;
	return UKA(tr_buf);
}

/* uk_read_file : x0 = chemin -> son contenu terminé par NUL (bloc du kit ;
 * 0 : absent). (Appelée nulle part dans l'asm : gardée.) */
static __attribute__((unused)) long read_file(long path)
{
	long st[2];
	if (SYS2(SYS_STAT, path, (long)(uintptr_t)st))
		return 0;
	long size = st[0];
	long buf = uk_alloc(size + 1);
	if (!buf)
		return 0;
	long fd = SYS2(SYS_OPEN, path, O_READ);
	if (fd >= 0) {
		long n = SYS3(SYS_READ, fd, buf, size);
		SYS1(SYS_CLOSE, fd);
		if (n >= 0) {
			STB(buf, n, 0);
			return buf;
		}
	}
	uk_free(buf);
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Menus : états des entrées, raccourcis traités par le kit
 * ------------------------------------------------------------------------- */

/* uk_menu_base : -> r0 = états des entrées, r1 = texte envoyé, de la fenêtre
 * courante (uk_menu_need d'abord pour une fenêtre qui n'a encore rien) */
static ukr menu_base(void)
{
	ukr r;
	long p = uk.menu_p;
	r.r2 = 0;
	r.r3 = 0;
	if (ULE(p + 1, 1)) {                   /* 0 : statique ; -1 : pas encore */
		r.r0 = UKA(menu_st);
		r.r1 = UKA(menubuf);
		return r;
	}
	r.r0 = p;
	r.r1 = p + UK_MENU_IDS * 16;
	return r;
}

/* uk_menu_need : les états et le texte des menus d'une fenêtre de plus, au
 * premier besoin */
static void menu_need(void)
{
	if (uk.menu_p != -1)
		return;
	uk.menu_p = uk_alloc(UK_MENU_IDS * 16 + WS_MENU_MAX + 8);
}

/* uk_menu_tr : x0 = texte des menus -> le même, ses libellés traduits (TR).
 * Le dernier texte traduit est gardé : un programme le renvoie souvent. */
static long menu_tr(long text)
{
	if (!text)
		return text;
	u8 *s = (u8 *)(uintptr_t)text;
	if (!*s)
		return text;
	/* comme l'asm : reconnu à son adresse seule (texte changé sur place :
	 * l'ancienne traduction) ; l'ancien tampon n'est jamais rendu */
	if (uk.menu_tr_d[0] == text)
		return uk.menu_tr_d[1];
	long nb = uk_alloc(WS_MENU_MAX);
	if (!nb)
		return text;
	u8 *d = (u8 *)(uintptr_t)nb;
	u8 *lim = d + WS_MENU_MAX - 480;       /* (une ligne de plus tient toujours) */
	uk.menu_tr_d[0] = text;
	uk.menu_tr_d[1] = nb;
	for (;;) {                              /* .line */
		u8 c = *s;
		if (!c)
			break;
		if (d >= lim)
			break;
		int label = 0;
		if (c == 'M') {
			*d++ = c;
			s++;
			label = 1;
		} else if (c == 'I') {
			/* I<id>, tabulation ; comme l'asm : sans arrêt au NUL */
			do {
				c = *s++;
				*d++ = c;
			} while (c != 9);
			label = 1;
		}
		if (label) {                        /* le libellé */
			u8 *f = uk.fitbuf;
			long n = 0;
			for (;;) {
				c = *s;
				if (!c || c == 9 || c == 10)
					break;
				if (n >= 400)
					break;
				f[n++] = c;
				s++;
			}
			f[n] = 0;
			u8 *t = (u8 *)(uintptr_t)ui_tr(UKA(fitbuf));
			while ((c = *t++))
				*d++ = c;
		}
		int end = 0;
		for (;;) {                          /* le reste de la ligne */
			c = *s;
			if (!c) {
				end = 1;
				break;
			}
			*d++ = c;
			s++;
			if (c == 10)
				break;
		}
		if (end)
			break;
	}
	*d = 0;
	return nb;
}

/* uk_menu_flags : x0 = id -> son état (UMS_*) */
static long menu_flags(long id)
{
	long base = menu_base().r0;
	for (long i = 0; ULT(i, UK_MENU_IDS); i++) {
		long e = base + (i << 4);
		long st = LDX(e, 8);
		if (st && LDX(e, 0) == id)
			return st & 0xFF;
	}
	return 0;
}

/* uk_menu_send : le texte du programme, avec les marques d'état, à la barre
 * de menus */
static long menu_send(void)
{
	long src = uk.menu_src;
	if (!src)
		return uk_menu_set(0);
	u8 *s = (u8 *)(uintptr_t)src;
	u8 *base = (u8 *)(uintptr_t)menu_base().r1;
	u8 *d = base;
	u8 *lim = base + WS_MENU_MAX - 4;
	while (d < lim) {                       /* .c */
		u8 c = *s++;
		*d++ = c;
		if (!c)
			break;
		if (c != 'I')                       /* « I<id> » en début de ligne */
			continue;
		u8 *at = d - 1;
		if (at != base && at[-1] != 10)
			continue;
		long id = 0;                        /* l'id, recopié */
		int full = 0;
		for (;;) {
			u8 k = *s;
			u32 dg = (u32)k - '0';
			if (dg >= 10)
				break;
			*d++ = k;
			s++;
			id = id * 10 + dg;
			if (d >= lim) {
				full = 1;
				break;
			}
		}
		if (full)
			break;
		long fl = menu_flags(id);           /* ses marques */
		if (fl & 1)
			*d++ = 'd';
		if (fl & 2)
			*d++ = 'c';
	}
	*d = 0;
	return uk_menu_set((long)(uintptr_t)base);
}

/* x0 = texte des menus (0 ou vide : plus de menu) de la fenêtre courante
 * -> x0 = 0 (< 0 : erreur) */
long ui_menu(long a0)
{
	uk.menu_src = menu_tr(a0);
	menu_need();
	return menu_send();
}

/* x0 = id, x1 = état (UMS_DISABLED, UMS_CHECKED ; 0 : normal) : renvoie les menus */
long ui_menu_state(long a0, long a1)
{
	menu_need();
	long base = menu_base().r0;             /* une place libre ou la sienne */
	long fr = -1;
	long e = 0;
	long i;
	for (i = 0; ULT(i, UK_MENU_IDS); i++) {
		e = base + (i << 4);
		if (!LDX(e, 8)) {
			if (fr == -1)
				fr = i;
			continue;
		}
		if (LDX(e, 0) == a0)
			break;
	}
	if (ULT(i, UK_MENU_IDS) || fr >= 0) {
		if (!ULT(i, UK_MENU_IDS))
			e = base + (fr << 4);
		STX(e, 0, a0);
		STX(e, 8, a1 | 0x100);              /* (place prise) */
	}
	return menu_send();
}

/* uk_shortcut_match : x0 = raccourci (« Ctrl+Maj+S », « F3 »... jusqu'à la fin
 * de ligne), x1 = événement de touche -> 1 s'il y correspond */
static long shortcut_match(long sp, long ev)
{
	u8 *s = (u8 *)(uintptr_t)sp;
	long want = 0;                          /* demandés : 1 Ctrl, 2 Maj, 4 Alt */
	u32 c3, c4, c5;
	for (;;) {
		c3 = s[0];
		c4 = s[1];
		c5 = s[2];
		if (c3 == 'C' && c4 == 't') {       /* « Ctrl+ » */
			want |= 1;
			s += 5;
			continue;
		}
		if (c3 == 'M' && c4 == 'a' && c5 == 'j') {   /* « Maj+ » */
			want |= 2;
			s += 4;
			continue;
		}
		if (c3 == 'A' && c4 == 'l') {       /* « Alt+ » */
			want |= 4;
			s += 4;
			continue;
		}
		break;
	}
	u32 mods = (u32)LDW(ev, EV_MODS);      /* modificateurs de l'événement */
	long have = 0;
	if (mods & 0x11)
		have |= 1;
	if (mods & 0x22)
		have |= 2;
	if (mods & 0x44)
		have |= 4;
	if (have != want)
		return 0;
	u32 code = (u32)LDW(ev, EV_CODE);      /* touche (code HID) */
	u32 k;
	if (c3 == 'F') {                        /* « F1 » .. « F12 » */
		c4 -= '0';
		if (c4 < 10) {
			c5 -= '0';
			if (c5 < 10)
				c4 = c4 * 10 + c5;
			c4 += 0x3A - 1;
			return code == c4;
		}
	}
	c3 |= 0x20;                             /* une lettre ou un chiffre (code HID US) */
	k = c3 - 'a';
	if (k < 26) {
		k += 4;
	} else {
		k = c3 - '1';
		if (k < 9)
			k += 0x1E;
		else if (c3 == '0')
			k = 0x27;
		else
			return 0;
	}
	return code == k;
}

/* uk_menu_key_in : x0 = événement (touche) de la fenêtre courante -> id, -1 : aucun */
static long menu_key_in(long ev)
{
	if (!LDW(ev, EV_KIND))
		return -1;
	u8 *l = (u8 *)(uintptr_t)menu_base().r1; /* lignes « I<id>[d][c]\t...\t<raccourci> » */
	/* comme l'asm : sans menu, x0 vaut uk_menu_src (0), pas -1 */
	if (!uk.menu_src)
		return 0;
	for (;;) {                              /* .line */
		u8 c = *l;
		if (!c)
			return -1;
		if (c == 'I') {
			u8 *p = l + 1;                  /* id */
			long id = 0;
			for (;;) {
				c = *p;
				u32 dg = (u32)c - '0';
				if (dg >= 10)
					break;
				id = id * 10 + dg;
				p++;
			}
			if (c != 'd') {                 /* grisée : sautée */
				long tabs = 0;              /* le raccourci : après la 2e tabulation */
				int next = 0;
				for (;;) {
					c = *p++;
					if (!c) {               /* .skipb */
						p--;
						l = p;
						next = 1;
						break;
					}
					if (c == 10) {          /* .line2 */
						l = p;
						next = 1;
						break;
					}
					if (c != 9)
						continue;
					tabs++;
					if (ULT(tabs, 2))
						continue;
					if (shortcut_match((long)(uintptr_t)p, ev))
						return id;
					break;
				}
				if (next)
					continue;
			}
		}
		for (;;) {                          /* .skip : ligne suivante */
			c = *l++;
			if (!c)
				return -1;
			if (c == 10)
				break;
		}
	}
}

/* x0 = événement (0 : ui_ev) -> id de l'entrée dont c'est le raccourci
 * (-1 : aucune ; entrée grisée : -1) */
long ui_menu_key(long a0)
{
	long ev = a0;
	if (!ev)
		ev = UV(UV_EV);
	if ((u32)LDW(ev, EV_TYPE) != WEV_KEY)
		return -1;
	long win = uk_route(LDW(ev, EV_WIN));  /* les menus de la fenêtre de la touche */
	long r = menu_key_in(ev);
	if (win)
		ui_window_use(win);
	return r;
}

/* ---------------------------------------------------------------------------
 *  Polices : les trois du programme et les autres DejaVu de /fonts,
 *  chargées au premier besoin dans le kit
 * ------------------------------------------------------------------------- */

/* uk_font_paths : chaînes à la suite (la (x - 3)e) */
static const char font_paths[] =
	"/fonts/DejaVuSans-Oblique.ttf\0"
	"/fonts/DejaVuSans-BoldOblique.ttf\0"
	"/fonts/DejaVuSansMono-Bold.ttf\0"
	"/fonts/DejaVuSerif.ttf\0"
	"/fonts/DejaVuSerif-Bold.ttf\0"
	"/fonts/DejaVuSerif-Italic.ttf\0"
	"/fonts/DejaVuSerif-BoldItalic.ttf";

/* x0 = police (UFN_*) -> police pour ui_text (absente : la régulière) */
long ui_font_get(long a0)
{
	long f = a0;
	if (!ULT(f, UK_NFONTS))
		return uk_font_r();
	if (ULT(f, 3))                          /* celles du programme */
		return UV(UV_FONT) + f * UI_FONT_SIZE;
	u8 *st = uk.font_st;                    /* 0 inconnue, 1 chargée, 2 absente */
	if (!st[f]) {
		st[f] = 2;
		const char *p = font_paths;         /* son chemin (le f - 3e) */
		long k = f - 3;
		while (k) {
			while (*p++)
				;
			k--;
		}
		if (ui_load_font(S(p), UKA(fonts) + (f - 3) * UI_FONT_SIZE))
			st[f] = 1;
	}
	if (st[f] == 1)
		return UKA(fonts) + (f - 3) * UI_FONT_SIZE;
	return uk_font_r();
}

/* -> police italique (ui_font_get(UFN_ITALIC)) */
long ui_font_italic(void)
{
	return ui_font_get(3);
}
