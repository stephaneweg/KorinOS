/* uk_frame.c -- UIKit : fenêtres servies par Elegant, leur cadre, les menus,
 * la liste des fenêtres (dock, barre de menus). Port ligne à ligne de
 * uikit_frame.inc (src/kits/uikit/PORT.md).
 *
 * Comme dans Onyx (uikit/skin.cpp), le cadre est dessiné par le programme :
 * deux copies (active, inactive) dans le tampon partagé de la fenêtre, à la
 * suite du contenu ; le serveur montre l'une ou l'autre selon le focus. Coins
 * arrondis : l'octet haut des pixels est la transparence (0 opaque).
 * Géométrie (FRAME_*) : pas de barre de titre, bords de 12 px (ombre), rayon
 * 10 ; réduire, agrandir, fermer sur le contenu, en haut à droite. */
#include "uk.h"

const char uk_menu_title[] = "menu";
static const char uk_sous[] = "\xE2\x80\xBA"; /* U+203A */

/* anneaux des ombres (transparence dans l'octet haut) */
static const u32 uk_ombre_a[3] = {0xF4000000, 0xEC000000, 0xE0000000}; /* menu, actif */
static const u32 uk_ombre_fa[11] = {0xFE000000, 0xFD000000, 0xFA000000, 0xF5000000, 0xF0000000, 0xEA000000,
				    0xE2000000, 0xD9000000, 0xCF000000, 0xC3000000, 0xB7000000}; /* fenêtre active */
static const u32 uk_ombre_fi[11] = {0xFF000000, 0xFE000000, 0xFC000000, 0xFA000000, 0xF8000000, 0xF4000000,
				    0xF0000000, 0xEC000000, 0xE7000000, 0xE1000000, 0xDB000000}; /* inactive */
/* (uk_ombre_i, anneaux d'un cadre inactif, n'est lu par aucun code de l'asm) */

/* ---- surface de dessin ---------------------------------------------------- */

/* uk_target : image, largeur, hauteur : les dessins de UIKit y vont (jusqu'à
 * uk_target_back) ; sans la découpe de ui_clip (la zone et la pile sont
 * gardées à part). -> image (x0 gardé, comme l'asm) */
long uk_target(long img, long w, long h)
{
	long v = uk.vars;
	uk.save[0] = LDX(v, UV_SURF);
	uk.save[1] = LDX(v, UV_W);
	uk.save[2] = LDX(v, UV_H);
	STX(v, UV_SURF, img);
	STX(v, UV_W, w);
	STX(v, UV_H, h);
	uk.save_clip = uk.clip_on;
	uk.clip_on = 0;
	uk.save_rect[0] = uk.clip[0];
	uk.save_rect[1] = uk.clip[1];
	uk.save_rect[2] = uk.clip[2];
	uk.save_rect[3] = uk.clip[3];
	uk.save_rect[4] = uk.clip_sp;
	return img;
}

/* uk_target_back : la surface, la découpe et sa pile d'avant uk_target */
long uk_target_back(void)
{
	long v = uk.vars;
	STX(v, UV_SURF, uk.save[0]);
	STX(v, UV_W, uk.save[1]);
	STX(v, UV_H, uk.save[2]);
	uk.clip_on = uk.save_clip;
	uk.clip[0] = uk.save_rect[0];
	uk.clip[1] = uk.save_rect[1];
	uk.clip[2] = uk.save_rect[2];
	uk.clip[3] = uk.save_rect[3];
	uk.clip_sp = uk.save_rect[4];
	return 0;
}

/* uk_bold : -> police grasse du programme */
long uk_bold(void)
{
	return uk.vars + UV_FONT_B;
}

/* ---- fenêtres ------------------------------------------------------------- */

/* uk_create : x, y, largeur, hauteur, titre, rôle (WF_*) -> id (< 0 : erreur),
 * réponse du serveur dans uk_msg. Si le serveur graphique n'est pas encore
 * prêt (démarrage), on l'attend jusqu'à 10 s. */
long uk_create(long x, long y, long w, long h, long title, long role)
{
	long tries = 0, r;
	for (;;) {
		STX(uk.msg, WM_A, x);
		STX(uk.msg, WM_B, y);
		STX(uk.msg, WM_C, w);
		STX(uk.msg, WM_D, h);
		STX(uk.msg, WM_DATA, role);
		uk_title_copy(UKA(msg) + WM_DATA + 8, title);
		r = uk_call(WS_OP_CREATE);
		if (r != E_NODEV) /* E_NODEV : pas (encore) de serveur */
			return r;
		tries++;
		if (UGE(tries, 100))
			return r;
		SYS1(SYS_SLEEP, 100);
	}
}

/* ui_window : largeur, hauteur, titre -> poignée (< 0 : erreur). Fenêtre
 * ordinaire, placée automatiquement, remplie de FACE. */
long ui_window(long w, long h, long title)
{
	return ui_window_ex(-1, -1, w, h, title, 0);
}

/* ui_window_ex : x, y (-1, -1 : automatique), largeur, hauteur, titre, rôle
 * (WF_*) -> poignée (< 0 : erreur). Remplie de FACE (transparente : WF_ALPHA). */
long ui_window_ex(long x, long y, long w, long h, long title, long role)
{
	long id, b, surf, c1, c2;
	title = TR(title);
	id = uk_create(x, y, w, h, title, role);
	if (id < 0)
		return id;
	b = uk_wb_new(); /* son bloc : elle devient la courante */
	if (!b) {
		STX(uk.msg, WM_A, id);
		uk_call(WS_OP_CLOSE);
		return E_NOMEM;
	}
	STX(b, WB_ID, id);
	uk.dsg_w = w; /* taille de conception (uk_adapt) */
	uk.dsg_h = h;
	STX(uk.vars, UV_WIN, id);
	uk.buf = LDX(uk.msg, WM_A); /* contenu et cadres : tampon partagé */
	surf = SYS1(SYS_BUF_MAP, uk.buf);
	if (surf < 0)
		return surf;
	STX(uk.vars, UV_SURF, surf);
	STX(uk.vars, UV_W, w);
	STX(uk.vars, UV_H, h);
	uk.dmg = surf + LDX(uk.msg, WM_D); /* en-tête WD_* (présent asynchrone) */
	c1 = LDX(uk.msg, WM_B); /* décalages des deux cadres (0 : aucun) */
	c2 = LDX(uk.msg, WM_C);
	if (c1) {
		c1 += surf;
		c2 += surf;
	}
	/* comme l'asm : sans cadre actif, uk_chr_i garde le décalage brut */
	uk.chr_a = c1;
	uk.chr_i = c2;
	uk.ow = w + 2 * FRAME_BORDER;
	uk.oh = h + FRAME_TITLE_H + FRAME_BORDER;
	uk.flags = role;
	uk_title_copy(UKA(title), title);
	if (!(role & WF_ALPHA)) /* WF_ALPHA : reste transparente */
		ui_fill(0, 0, w, h, JET(TJ_FACE));
	uk_draw_chrome();
	return id;
}

/* ui_title : titre : change le titre de la fenêtre (et de son cadre) ->
 * (comme l'asm) le retour de uk_draw_chrome */
long ui_title(long t)
{
	t = TR(t);
	uk_title_copy(UKA(title), t);
	uk_title_copy(UKA(msg) + WM_DATA, t);
	STX(uk.msg, WM_A, uk_win());
	uk_call(WS_OP_TITLE);
	return uk_draw_chrome();
}

/* (uk_draw_frame) .cross : x, y = centre : croix de 9 x 9, encre ink */
static void frame_cross(long cx, long cy, long ink)
{
	long d;
	for (d = -4; d <= 4; d++) {
		ui_fill(cx + d, cy + d, 2, 1, ink);
		ui_fill(cx + d, cy - d, 2, 1, ink);
	}
}

/* uk_frame_buttons : jetons, actif, largeur de l'image : les trois boutons,
 * sans fond ni contour : réduire, agrandir, fermer (mesures de win_hit,
 * core.inc d'Elegant) */
static void frame_buttons(long active, long ow)
{
	long j = UKA(tokens), bx, by, f;
	u32 ink, grey;
	bx = ow - (FRAME_BORDER + FRAME_CAP_MARGIN + FRAME_CAP_W);
	by = FRAME_BORDER + FRAME_CAP_MARGIN;
	ui_fill(bx, by, FRAME_CAP_W, FRAME_CAP_H, 0xFF000000);
	ink = (u32)LDW(j, TJ_TEXT2); /* symboles (inactif : plus discrets) */
	if (!active)
		ink = (u32)LDW(j, TJ_TEXT3);
	grey = (u32)LDW(j, TJ_TEXT3);
	if (uk.flags & WF_GLYPH_LIGHT) { /* (WF_GLYPH_LIGHT : en clair) */
		ink = 0x00FFFFFF;
		if (!active)
			ink = 0x00B0B4BA;
		grey = 0x00808488;
	}
	/* fermer : une croix */
	frame_cross(bx + FRAME_CAP_W - FRAME_CAP_BTN / 2, by + FRAME_CAP_H / 2, ink);
	f = uk.flags; /* agrandir : carré (grisé sans WF_RESIZABLE, rien sur un dialogue) */
	if (f & WF_MODAL)
		return;
	ui_frame(bx + FRAME_CAP_W - FRAME_CAP_BTN - FRAME_CAP_BTN / 2 - 5, by + FRAME_CAP_H / 2 - 5, 10, 10,
		 (f & WF_RESIZABLE) ? ink : grey);
	f = uk.flags; /* réduire : trait (pas sur un dialogue) */
	if (f & WF_MODAL)
		return;
	ui_fill(bx + FRAME_CAP_W - 2 * FRAME_CAP_BTN - FRAME_CAP_BTN / 2 - 5, by + FRAME_CAP_H / 2 + 3, 10, 2, ink);
}

/* uk_draw_frame : image (uk_ow x uk_oh), 1 actif : le cadre. Le bord de 12 px
 * est une ombre (onze anneaux noirs transparents, décalés vers le bas, puis
 * le contour) ; le corps, arrondi, n'apparaît qu'aux coins du contenu ;
 * boutons par-dessus. */
static void draw_frame(long img, long active)
{
	long ow = uk.ow, oh = uk.oh, i, dy;
	const u32 *rings;
	u32 c, a;
	uk_target(img, ow, oh);
	rings = active ? uk_ombre_fa : uk_ombre_fi; /* transparences des anneaux */
	for (i = 0; ULT(i, 11); i++) { /* anneau 0..10 (rayons 22 à 12) */
		c = rings[i];
		dy = UDIV((10 - i) * 3, 10); /* décalage : 3 px pour le plus large */
		if (UGT(dy, i))
			dy = i;
		ui_rbox(i, dy + i, ow - (i << 1), oh - (i << 1), 22 - i, c, c);
	}
	c = (u32)JET(TJ_OUTLINE); /* contour (inactif : plus léger) */
	if (!active) {
		a = (c >> 24) + 0x10;
		if (a > 255)
			a = 255;
		c = (c & 0xFFFFFF) | (a << 24);
	}
	ui_rbox(11, 11, ow - 22, oh - 22, 11, c, c);
	c = (u32)JET(TJ_FACE); /* le corps */
	if (uk.flags & WF_BODY_DARK)
		c = 0;
	ui_rbox(12, 12, ow - 24, oh - 24, 10, c, c);
	frame_buttons(active, ow);
	uk_target_back();
}

/* uk_draw_chrome : dessine les deux copies du cadre (thème courant), puis
 * prévient le serveur -> 0 (sans cadre) ou la réponse du serveur */
long uk_draw_chrome(void)
{
	if (!uk.chr_a)
		return 0;
	draw_frame(uk.chr_a, 1);
	draw_frame(uk.chr_i, 0);
	STX(uk.msg, WM_A, uk_win());
	return uk_call(WS_OP_FRAME);
}

/* ---- menus ---------------------------------------------------------------- */

/* état d'un menu (registres x19..x27 de ui_popup_ex) */
struct pop {
	long x, y;      /* x19, x20 : coin du corps (écran) */
	long labels;    /* x21 */
	long width;     /* x22 */
	long height;    /* x23 : hauteur du corps */
	long count;     /* x24 : libellés */
	long win;       /* x25 : fenêtre du menu */
	long surf;      /* x26 : tampon partagé, mappé */
	long hover;     /* x27 : libellé sous le pointeur */
};

/* un libellé « - » est un séparateur */
static int pop_is_sep(long s)
{
	return LDB(s, 0) == '-' && LDB(s, 1) == 0;
}

/* chaîne suivante (après le NUL) */
static long pop_skip(long s)
{
	while (LDB(s, 0))
		s++;
	return s + 1;
}

/* uk_pop_item : numéro -> son libellé */
static long pop_item(struct pop *p, long n)
{
	long s = p->labels;
	while (n) {
		s = pop_skip(s);
		n--;
	}
	return s;
}

/* uk_pop_sub : numéro -> 1 si son texte commence par « > » */
static long pop_sub(struct pop *p, long n)
{
	long s = pop_item(p, n), q;
	u8 c;
	if (uk.pop_fl & 1) { /* avec icônes : après la tabulation */
		q = s;
		for (;;) {
			c = (u8)LDB(q, 0);
			q++;
			if (!c)
				break; /* (sans tabulation : le libellé entier) */
			if (c == 9) {
				s = q;
				break;
			}
		}
	}
	return LDB(s, 0) == '>';
}

/* uk_pop_row_y : numéro -> r0 = y (écran) du haut de sa ligne, r1 = son
 * libellé (x2 de l'asm, rendu par ui_popup_ex) */
static ukr pop_row_y(struct pop *p, long n)
{
	ukr r;
	long y = p->y + UK_POP_PAD, s = p->labels, i, h;
	for (i = 0; ULT(i, n); i++) {
		h = uk.pop_rh;
		if (pop_is_sep(s))
			h = UK_POP_SEP;
		y += h;
		s = pop_skip(s);
	}
	r.r0 = y;
	r.r1 = s;
	r.r2 = r.r3 = 0;
	return r;
}

/* uk_pop_hit : point dans la fenêtre -> libellé dessous (-1 : séparateur ou
 * marge, -2 : hors du menu) */
static long pop_hit(struct pop *p, long x, long y)
{
	long s, i, h;
	x -= UK_OMBRE;
	y -= UK_OMBRE;
	if (x < 0 || x >= p->width || y < 0 || y >= p->height)
		return -2;
	y -= UK_POP_PAD;
	s = p->labels;
	for (i = 0; ULT(i, p->count); i++) {
		h = uk.pop_rh;
		if (pop_is_sep(s))
			h = UK_POP_SEP;
		if (y < 0)
			return -1;
		if (ULT(y, h)) {
			/* comme l'asm : une ligne haute de UK_POP_SEP compte comme séparateur */
			if (h == UK_POP_SEP)
				return -1;
			if (LDB(s, 0) == 1) /* (entrée grisée : rien) */
				return -1;
			return i;
		}
		y -= h;
		s = pop_skip(s);
	}
	return -1;
}

/* ui_shadow (uk_ombre) : largeur, hauteur : ombre (trois anneaux transparents)
 * et contour autour d'un corps en retrait de UK_OMBRE, rayon 8 (dans la
 * surface courante) -> retour du dernier ui_rbox */
long ui_shadow(long w, long h)
{
	long i, r = 0;
	u32 c;
	for (i = 0; ULT(i, 4); i++) {
		c = i < 3 ? uk_ombre_a[i] : (u32)JET(TJ_OUTLINE);
		r = ui_rbox(i, i, w - (i << 1), h - (i << 1), 12 - i, c, c);
	}
	return r;
}

/* uk_popup_draw : dessine le menu, puis le montre */
static void popup_draw(struct pop *p)
{
	long j = UKA(tokens), s, i, ly, t, k, n, tw, px;
	long font = uk.vars + UV_FONT;
	u32 menu, ink, col;
	u8 c;
	menu = (u32)LDW(j, TJ_MENU);
	ui_shadow(p->width + 2 * UK_OMBRE, p->height + 2 * UK_OMBRE); /* ombre, contour, corps */
	ui_rbox(UK_OMBRE, UK_OMBRE, p->width, p->height, 8, menu, menu);
	s = p->labels;
	ly = UK_OMBRE + UK_POP_PAD; /* y de la ligne */
	for (i = 0; ULT(i, p->count); i++, s = pop_skip(s)) {
		if (pop_is_sep(s)) { /* séparateur */
			ui_fill(UK_OMBRE + 10, ly + UK_POP_SEP / 2, p->width - 20, 1, JET(TJ_LINE));
			ly += UK_POP_SEP;
			continue;
		}
		ink = (u32)LDW(j, TJ_TEXT1); /* encre (sur l'accent : son encre) */
		if (LDB(s, 0) == 1) { /* grisée (0x01) : TEXTE3, jamais surlignée */
			ink = (u32)LDW(j, TJ_TEXT3);
		} else if (i == p->hover) {
			col = (u32)LDW(j, TJ_ACCENT);
			ui_rbox(UK_OMBRE + 5, ly, p->width - 10, uk.pop_rh, 5, col, col);
			ink = (u32)JET(TJ_ACCENT_INK);
		}
		t = s; /* texte (après sa marque) */
		c = (u8)LDB(t, 0);
		if (c <= 2) {
			t++;
			if (c == 2) /* cochée : « ✓ » dans la marge */
				ui_text(S(uk_s_check), UK_OMBRE + 1, ly + LSR(uk.pop_rh + 9, 1), ink, 12, font, 0);
		}
		uk.pop_x = UK_OMBRE + 14; /* texte à gauche, sauf s'il y a une icône */
		if (uk.pop_fl & 1) {
			n = 0; /* icône : nom jusqu'à la tabulation */
			for (;;) {
				c = (u8)LDB(t, 0);
				t++;
				if (!c || c == 9)
					break;
				if (UGE(n, 30))
					continue;
				STB(UKA(pop_name), n, c);
				n++;
			}
			STB(UKA(pop_name), n, 0);
			if (!c) {
				/* comme l'asm : pas de tabulation, tout est texte, marque
				 * (0x01, 0x02) comprise */
				t = s;
			} else {
				ui_icon(UKA(pop_name), UK_OMBRE + 11, ly + LSR(uk.pop_rh - 16, 1), 16);
				uk.pop_x = UK_OMBRE + 36;
			}
		}
		if (LDB(t, 0) == '>') { /* « > » : ouvre un sous-menu, « › » à droite */
			t++;
			tw = ui_text_width(S(uk_sous), 14, uk.vars + UV_FONT);
			px = p->width + UK_OMBRE - 14 - tw;
			col = (u32)LDW(j, TJ_TEXT3);
			if (i == p->hover)
				col = (u32)JET(TJ_ACCENT_INK);
			ui_text(S(uk_sous), px, ly + LSR(uk.pop_rh + 9, 1), col, 14, uk.vars + UV_FONT, 0);
		}
		if (uk.pop_fl & 16) { /* « texte<tab>raccourci », raccourci à droite en TEXTE3 */
			n = 0; /* source */
			k = 0; /* copie (bornée) */
			for (;;) {
				c = (u8)LDB(t, n);
				if (!c || c == 9)
					break;
				n++;
				if (UGE(k, 126))
					continue;
				STB(UKA(pop_label), k, c);
				k++;
			}
			STB(UKA(pop_label), k, 0);
			if (c) { /* le raccourci, après la tabulation */
				uk.pop_key = t + n + 1;
				tw = ui_text_width(uk.pop_key, UK_PX, uk.vars + UV_FONT);
				px = p->width + UK_OMBRE - 14 - tw;
				col = (u32)LDW(j, TJ_TEXT3);
				if (i == p->hover)
					col = (u32)JET(TJ_ACCENT_INK);
				ui_text(uk.pop_key, px, ly + LSR(uk.pop_rh + 9, 1), col, UK_PX, uk.vars + UV_FONT, 0);
			}
			t = UKA(pop_label);
		}
		ui_text(t, uk.pop_x, ly + LSR(uk.pop_rh + 9, 1), ink, UK_PX, uk.vars + UV_FONT,
			p->width + UK_OMBRE - 30 - uk.pop_x);
		ly += uk.pop_rh;
	}
	STX(uk.msg, WM_A, p->win);
	STX(uk.msg, WM_B, 0);
	STX(uk.msg, WM_C, 0);
	STX(uk.msg, WM_D, p->width + 2 * UK_OMBRE);
	STX(uk.msg, WM_DATA, p->height + 2 * UK_OMBRE);
	uk_call(WS_OP_UPDATE);
}

/* ui_popup : x, y (écran), libellés (chaînes à la suite, une vide à la fin),
 * largeur -> numéro du libellé choisi, -1 si abandon (-2 : clic hors du menu ;
 * r1, r2 : comme ui_popup_ex) */
ukr ui_popup(long x, long y, long labels, long w)
{
	/* comme l'asm (repli dans ui_popup_ex) : x5, x6 ne comptent pas, x4 = 0 */
	return ui_popup_ex(x, y, labels, w, 0, 0, 0);
}

/* ui_popup_ex : ui_popup + x4 = drapeaux : bit 1, garder le menu ouvert quand
 * on choisit une entrée à sous-menu (« > ») : rend aussi r1 = y (écran) de sa
 * ligne ; puis bit 2 : reprendre ce menu (avec le bit 3, x5, x6 = le clic qui
 * a fermé le sous-menu : sur ce menu il y choisit, ailleurs tout se ferme), ou
 * ui_popup_close. Rend -1 (Échap), -2 (clic hors du menu : r1, r2 = sa
 * position sur l'écran). Bit 0 : libellés « icône<tab>texte » ; bit 4 (sans
 * le bit 0) : « texte<tab>raccourci ». Un libellé « - » est un séparateur ;
 * 0x01 en tête : grisé ; 0x02 : coché. (x, y) : coin du corps. */
ukr ui_popup_ex(long x, long y, long labels, long w, long fl, long cx, long cy)
{
	struct pop p;
	long bufid = 0, result, s, n, h, r, hit, kind, code, d, steps, item, ev;
	ukr out;
	u8 c;
	uk.capture = 0; /* (ouvert sur un appui : le relâché ne reviendra pas au widget) */
	if (fl & 4)
		goto resume;
	p.x = x;
	p.y = y;
	p.labels = labels;
	p.width = w;
	uk.pop_fl = fl;
	uk.pop_rh = LDX(uk_met(), MT_MENU); /* hauteur d'une ligne : 28, 26, 24 ; 36 tactile */
	p.count = 0; /* libellés, hauteur du corps */
	p.height = 2 * UK_POP_PAD;
	s = labels;
	while ((c = (u8)LDB(s, 0)) != 0) {
		p.count++;
		h = UK_POP_SEP;
		if (!(c == '-' && LDB(s, 1) == 0))
			h = uk.pop_rh;
		p.height += h;
		s = pop_skip(s);
	}
	result = -1;
	if (!p.count)
		goto ret;
	if (ui_screen(UKA(scr)) >= 0) { /* dans l'écran (avec l'ombre) */
		n = LDX(uk.scr, 0) - p.width - UK_OMBRE;
		if (p.x > n)
			p.x = n;
		n = LDX(uk.scr, 8) - p.height - UK_OMBRE;
		if (p.y > n)
			p.y = n;
	}
	if (p.x < UK_OMBRE)
		p.x = UK_OMBRE;
	if (p.y < UK_OMBRE)
		p.y = UK_OMBRE;
	/* fenêtre : le corps et son ombre */
	p.win = uk_create(p.x - UK_OMBRE, p.y - UK_OMBRE, p.width + 2 * UK_OMBRE, p.height + 2 * UK_OMBRE,
			  S(uk_menu_title), WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST | WF_GRAB | WF_ALPHA);
	if (p.win < 0)
		goto ret;
	bufid = LDX(uk.msg, WM_A);
	r = SYS1(SYS_BUF_MAP, bufid);
	if (r < 0)
		goto close;
	p.surf = r;
	uk_target(p.surf, p.width + 2 * UK_OMBRE, p.height + 2 * UK_OMBRE);
	p.hover = -1;
	popup_draw(&p);
wait:
	ev = SYS2(SYS_EV_WAIT, UKA(pev), 30000);
	if (!ev)
		goto done; /* (30 s sans rien : abandon) */
	if (LDW(uk.pev, EV_WIN) != p.win)
		goto wait; /* (événements de l'autre fenêtre : perdus) */
	if ((u32)ev == WEV_KEY)
		goto key;
	if ((u32)ev != WEV_MOUSE)
		goto wait;
	hit = pop_hit(&p, LDSW(uk.pev, EV_X), LDSW(uk.pev, EV_Y)); /* libellé sous le pointeur */
	kind = LDW(uk.pev, 4); /* 0 déplacement, 1 appui, 2 relâché */
	if (kind == 2)
		goto release;
	if (kind == 1 && hit == -2) { /* appui hors du menu : abandon (-2), position gardée */
		uk.pop_click[0] = LDSW(uk.pev, EV_X) + p.x - UK_OMBRE;
		uk.pop_click[1] = LDSW(uk.pev, EV_Y) + p.y - UK_OMBRE;
		result = -2;
		goto done;
	}
	if (hit == -2) /* (dehors : rien sous le pointeur) */
		hit = -1;
	if (hit == p.hover)
		goto wait;
	p.hover = hit;
	popup_draw(&p);
	goto wait;
release:
	if (hit < 0)
		goto wait; /* (le relâché du clic qui a ouvert le menu) */
	result = hit;
	goto chosen;
resume: /* --- reprise : état gardé dans uk_pp */
	p.x = uk.pp[0];
	p.y = uk.pp[1];
	p.labels = uk.pp[2];
	p.width = uk.pp[3];
	p.height = uk.pp[4];
	p.count = uk.pp[5];
	p.win = uk.pp[6];
	p.surf = uk.pp[7];
	p.hover = uk.pp[8];
	bufid = uk.pp[9];
	uk.pop_fl = uk.pp[10] & ~4L;
	uk.pop_rh = uk.pp[11];
	result = -1;
	if (!(fl & 8))
		goto redraw;
	/* bit 3 : le clic (x5, x6) qui a fermé le sous-menu : sur ce menu, il y
	 * choisit ; ailleurs, tout se ferme */
	hit = pop_hit(&p, cx - p.x + UK_OMBRE, cy - p.y + UK_OMBRE);
	if (hit == -2) {
		uk.pop_click[0] = cx;
		uk.pop_click[1] = cy;
		result = -2;
		/* comme l'asm : uk_target_back sans uk_target dans cet appel (rend la
		 * surface gardée par le dernier uk_target) */
		goto done;
	}
	if (hit < 0)
		goto redraw; /* (séparateur, marge) */
	result = hit; /* la ligne cliquée devient la ligne surlignée */
	p.hover = hit;
	uk_target(p.surf, p.width + 2 * UK_OMBRE, p.height + 2 * UK_OMBRE);
	popup_draw(&p);
	goto chosen;
redraw:
	uk_target(p.surf, p.width + 2 * UK_OMBRE, p.height + 2 * UK_OMBRE);
	popup_draw(&p);
	goto wait;
chosen: /* un choix : sous-menu (« > ») et menu à garder ouvert ? */
	if (!(uk.pop_fl & 2))
		goto done;
	if (!pop_sub(&p, result))
		goto done;
	/* gardé : état dans uk_pp, le menu reste affiché (ui_popup_ex : reprise ;
	 * ui_popup_close : fermeture) */
	uk.pp[0] = p.x;
	uk.pp[1] = p.y;
	uk.pp[2] = p.labels;
	uk.pp[3] = p.width;
	uk.pp[4] = p.height;
	uk.pp[5] = p.count;
	uk.pp[6] = p.win;
	uk.pp[7] = p.surf;
	uk.pp[8] = p.hover;
	uk.pp[9] = bufid;
	uk.pp[10] = uk.pop_fl;
	uk.pp[11] = uk.pop_rh;
	uk_target_back();
	out = pop_row_y(&p, result); /* y (écran) de la ligne choisie */
	/* r1 = y ; r2 = son libellé (x2 laissé par uk_pop_row_y, comme l'asm) */
	out.r2 = out.r1;
	out.r1 = out.r0;
	out.r0 = result;
	out.r3 = 0;
	return out;
key:
	if (!LDW(uk.pev, 4))
		goto wait; /* relâché */
	code = LDW(uk.pev, EV_CODE);
	if (code == KEY_ESC)
		goto done;
	if (code == KEY_ENTER) {
		if (p.hover < 0)
			goto wait;
		item = pop_item(&p, p.hover); /* (entrée grisée : rien) */
		if (LDB(item, 0) == 1)
			goto wait;
		result = p.hover;
		goto chosen;
	}
	if (code == 0x4F) { /* droite : comme Entrée (sous-menu) */
		if (p.hover < 0)
			goto wait;
		result = p.hover;
		goto chosen;
	}
	if (code == 0x51) /* bas : +1, haut : -1 (séparateurs sautés) */
		d = 1;
	else if (code == 0x52)
		d = -1;
	else
		goto wait;
	steps = p.count; /* (au plus un tour) */
	if (p.hover < 0) /* rien de choisi : depuis le haut, ou le bas */
		p.hover = d < 0 ? p.count : -1;
	for (;;) {
		p.hover += d;
		if (p.hover < 0)
			p.hover = 0;
		if (p.hover > p.count - 1)
			p.hover = p.count - 1;
		item = pop_item(&p, p.hover);
		if (!pop_is_sep(item))
			break;
		if (--steps == 0)
			break;
	}
	popup_draw(&p);
	goto wait;
done:
	uk_target_back();
close: /* fermeture du menu */
	STX(uk.msg, WM_A, p.win);
	uk_call(WS_OP_CLOSE);
	SYS1(SYS_BUF_FREE, bufid);
ret:
	out.r0 = result;
	out.r1 = uk.pop_click[0]; /* (-2 : r1, r2 = le clic) */
	out.r2 = uk.pop_click[1];
	out.r3 = 0;
	return out;
}

/* ui_popup_close : ferme le menu gardé ouvert par ui_popup_ex (drapeau 2) */
long ui_popup_close(void)
{
	STX(uk.msg, WM_A, uk.pp[6]); /* fenêtre */
	uk_call(WS_OP_CLOSE);
	return SYS1(SYS_BUF_FREE, uk.pp[9]); /* tampon partagé */
}

/* ---- liste des fenêtres (dock, barre de menus) ---------------------------- */

/* ui_winlist : première, tampon (WL_PER_MSG x WL_SIZE octets) -> nombre de
 * fenêtres listées */
long ui_winlist(long first, long buf)
{
	long r, i;
	STX(uk.msg, WM_A, first);
	r = uk_call(WS_OP_LIST);
	if (r < 0)
		return r;
	for (i = 0; i < WL_PER_MSG * WL_SIZE; i += 8)
		STX(buf, i, LDX(uk.msg, WM_DATA + i));
	return r;
}

/* ui_win_details : première, tampon de WE_PER_MSG entrées WE_* -> nombre de
 * fenêtres (du fond vers l'avant, toutes les couches) */
long ui_win_details(long first, long buf)
{
	long r, i;
	STX(uk.msg, WM_A, first);
	r = uk_call(WS_OP_WIN_LIST);
	if (r < 0)
		return r;
	for (i = 0; i < WE_PER_MSG * WE_SIZE; i += 8)
		STX(buf, i, LDX(uk.msg, WM_DATA + i));
	return r;
}

/* ui_desk : espace de travail à montrer (-1 : garder), nombre (0 : garder) ->
 * courant | nombre << 8 | compteur << 16 */
long ui_desk(long desk, long n)
{
	STX(uk.msg, WM_A, desk);
	STX(uk.msg, WM_B, n);
	return uk_call(WS_OP_DESK);
}

/* ui_raise : id : fenêtre au premier plan (restaurée si réduite) */
long ui_raise(long id)
{
	STX(uk.msg, WM_A, id);
	return uk_call(WS_OP_RAISE);
}

/* ui_minimize : la fenêtre courante réduite, comme le bouton de son cadre ->
 * (comme l'asm) la réponse du serveur */
long ui_minimize(void)
{
	STX(uk.msg, WM_A, uk_win());
	return uk_call(WS_OP_MINIMIZE);
}

/* ui_screen_grab : pixels 0x00RRGGBB, largeur, hauteur : l'écran composé, tel
 * qu'il est montré (WS_OP_SNAPSHOT), recopié depuis son coin en haut à gauche,
 * une ligne tous les « largeur » pixels -> 1 (0 : Elegant ne répond pas) */
long ui_screen_grab(long dst, long w, long h)
{
	long id, m, sw, sh, rows, cols, k, src;
	STX(uk.msg, WM_B, 0); /* génération 0 : tout l'écran */
	STW(uk.msg, WM_LEN, 0);
	if (uk_call(WS_OP_SNAPSHOT) < 0)
		return 0;
	id = LDX(uk.msg, WM_D); /* le tampon (le même à chaque appel) */
	src = uk.snap[1]; /* numéro + 1 déjà vu, son adresse */
	if (uk.snap[0] != id + 1) {
		uk.snap[0] = id + 1;
		m = SYS1(SYS_BUF_MAP, id);
		if (m < 0) {
			uk.snap[0] = 0;
			return 0;
		}
		src = m;
		uk.snap[1] = m;
	}
	sw = LDX(uk.msg, WM_A); /* largeur, hauteur de l'écran */
	sh = LDX(uk.msg, WM_B);
	rows = ULT(sh, h) ? sh : h;
	cols = ULT(sw, w) ? sw : w;
	if (!cols)
		return 1;
	for (; rows; rows--) {
		for (k = 0; ULT(k, cols); k++)
			STW(dst, k << 2, LDW(src, k << 2));
		src += sw << 2;
		dst += w << 2;
	}
	return 1;
}
