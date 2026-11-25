/* panel_classic.c : le panel du bureau classique (desktop=classic de /etc/system.ini ;
 * panel.c le lance quand Elegant n'a pas de vue d'ensemble), tel qu'il était avant le dash du
 * 23/11/2026. La barre du bureau Elegant (/sys/panel, lancé par la session), à la place du
 * dock (sys/dock, conservé, n'est plus lancé). Traduit de panel.asm.
 * Collée au bas de l'écran, toute sa largeur, aux couleurs du thème :
 *   - à gauche, le bouton « apps » (grille de 3 x 3) : le lanceur (launcher.c) ;
 *   - au centre, les RACCOURCIS (panelconf : /etc/panel.ini), puis les applications
 *     ouvertes qui n'en ont pas ; un clic lance ou ramène ; un clic droit, leur menu
 *     (épingler, détacher, lancer un autre process) ;
 *   - à droite, les ESPACES DE TRAVAIL (un carré chacun, leurs fenêtres en petit), la
 *     corbeille (masquée pour l'instant), le presse-papiers (sys/clipboard), les réglages,
 *     l'alimentation.
 * Un trait sous une application ouverte. Fenêtre sans cadre, au-dessus des autres, sans
 * focus ; elle réserve le bas de l'écran. */
#include "panel.h"
#include "buskit_import.h"

#define SL_MAX 24   /* raccourcis et applications ouvertes */
#define HIT_APPS -2 /* ce qu'il y a sous le pointeur (0.. : emplacement) */
#define HIT_TRASH -3
#define HIT_GEAR -4
#define HIT_POWER -5
#define HIT_CLIP -6
#define HIT_DESK 64 /* + l'espace */
#define SQGAP 5     /* entre deux carrés d'espaces de travail */
#define MINI_MAX 32 /* fenêtres dessinées dans les carrés */

/* constantes des échelles (L, M, P) : DH, CW, ICON, BTN, carré d'espace (l x h) */
#define ECH_N 6
static const int ech[3][ECH_N] = {
	{48, 40, 32, 34, 34, 25},
	{48, 40, 32, 34, 30, 22},
	{48, 44, 32, 40, 26, 19},
};
static struct {
	long dh, cw, icon, btn, sqw, sqh;
} cur; /* constantes courantes */

/* une fenêtre en petit : espace + 1 | clavier << 8, x, y, l, h */
struct mini {
	u16 desk;
	int16_t x, y;
	u16 w, h;
};

/* (ui, scr, screen_todo : panel.c) */

static long pan_win;
static u8 pan_msg[WS_MSG];
static long nslots;
static long npinned; /* dont raccourcis */
static long slot_w;  /* largeur d'un emplacement */
static long icon_px;
static long power_x, gear_x, clip_x, desk_x;
static long trash_x = -1;
static long desk_cur;  /* espace de travail montré */
static long tip_ticks; /* secondes sans mouvement */
static long hover = -1; /* survolé (hit) */
static long sl_x[SL_MAX];
static u8 sl_app[SL_MAX];
static long nminis;
static u64 minis_sig;
static struct mini minis[MINI_MAX];
static u8 wdet[WE_PER_MSG * WE_SIZE];
static char tip_text[8 + 8 + DL_DESK];
static char tip_num[8];

static void draw(void);

static u32 tok(long off)
{
	return M32(ui_tokens(), off);
}

/* ---------------------------------------------------------------------------
 *  Disposition
 * --------------------------------------------------------------------------- */

/* constantes de l'échelle (ou de « taille = »), ce qui est fixe à droite */
static void metrics(void)
{
	long s = ui_scale();
	if (dl_size >= 0)
		s = dl_size;
	const int *e = ech[s];
	cur.dh = e[0];
	cur.cw = e[1];
	cur.icon = e[2];
	cur.btn = e[3];
	cur.sqw = e[4];
	cur.sqh = e[5];
	long x = scr[SCR_W] - 8 - cur.btn; /* de la droite vers la gauche : alimentation, */
	power_x = x;
	x -= cur.btn + 4; /* réglages, */
	gear_x = x;
	x -= cur.btn + 4; /* presse-papiers, */
	clip_x = x;
	/* la corbeille : masquée tant qu'il n'y en a pas (22/11/2026 ; « corbeille = » de panel.ini
	 * sans effet). Pour la remettre : si dl_trash, x -= cur.btn + 4 et trash_x = x */
	trash_x = -1;
	x -= 10; /* espaces de travail */
	desk_x = x - (dl_ndesks * (cur.sqw + SQGAP) - SQGAP);
}

/* application, n = emplacements : ajoutée si elle n'y est pas */
static void slot_add(long app, long *n)
{
	for (long i = 0; i < *n; i++)
		if (sl_app[i] == app)
			return;
	if (*n >= SL_MAX)
		return;
	sl_app[(*n)++] = (u8)app;
}

/* les emplacements : les raccourcis, puis les applications ouvertes sans raccourci ; leur
 * place, centrés sur l'écran autant que possible */
static void slots_build(void)
{
	long n = 0;
	for (long i = 0; i < pan_npin; i++) {
		long a = app_find(pan_pin[i]);
		if (a >= 0)
			slot_add(a, &n);
	}
	npinned = n;
	for (long i = 0; i < ap_n; i++) { /* par nom */
		long a = ap_order[i];
		if (pids[a] && !pan_shell(a))
			slot_add(a, &n);
	}
	nslots = n;
	long start = cur.btn + 16; /* la place entre « apps » et les carrés */
	long end = desk_x - 10;
	long room = end - start;
	if (room < 0)
		room = 0;
	u64 w = (u64)cur.cw;
	long shown = n; /* emplacements montrés */
	if (n) {
		u64 q = (u64)room / (u64)n; /* trop nombreux : plus étroits, */
		if (q < w)
			w = q;
		if (w < 30) { /* puis moins nombreux */
			w = 30;
			shown = (long)((u64)room / 30);
		}
	}
	slot_w = (long)w;
	nslots = shown;
	u64 ic = (u64)cur.icon; /* l'icône tient dans l'emplacement */
	if (ic > w - 4)
		ic = w - 4;
	icon_px = (long)ic;
	long total = shown * (long)w; /* largeur des emplacements */
	long x = (long)((u64)(scr[SCR_W] - total) >> 1); /* centrés (décalage non signé, comme l'asm) */
	if (x > end - total)
		x = end - total;
	if (x < start)
		x = start;
	for (long i = 0; i < shown; i++, x += (long)w)
		sl_x[i] = x;
}

/* espace -> le coin de son carré */
static void desk_box(long d, long *x, long *y)
{
	*x = d * (cur.sqw + SQGAP) + desk_x;
	*y = (long)((u64)(cur.dh - cur.sqh) >> 1);
}

/* point dans la fenêtre -> ce qu'il y a dessous (-1 : rien, ou HIT_*, ou un emplacement) ;
 * comparaisons non signées, comme l'asm */
static long hit(long x, long y)
{
	(void)y;
	u64 btn = (u64)cur.btn;
	if ((u64)(x - 8) < btn)
		return HIT_APPS;
	if ((u64)(x - power_x) < btn)
		return HIT_POWER;
	if ((u64)(x - gear_x) < btn)
		return HIT_GEAR;
	if ((u64)(x - clip_x) < btn)
		return HIT_CLIP;
	if (trash_x >= 0 && (u64)(x - trash_x) < btn)
		return HIT_TRASH;
	long dx = desk_x - 2; /* (2 px autour) */
	u64 step = (u64)(cur.sqw + SQGAP);
	for (long d = 0; d < dl_ndesks; d++, dx += (long)step)
		if ((u64)(x - dx) < step)
			return HIT_DESK + d;
	for (long i = 0; i < nslots; i++)
		if ((u64)(x - sl_x[i]) < (u64)slot_w)
			return i;
	return -1;
}

/* ce qui est survolé : son nom au-dessus du panel, redessiné s'il change */
static void hover_set(long h)
{
	if (hover == h)
		return;
	hover = h;
	if (h == -1) {
		ui_tip_at(0, 0, 0);
		draw();
		return;
	}
	long x;
	const char *text;
	if (h >= HIT_DESK) { /* « Bureau 2 : son nom » */
		long d = h - HIT_DESK;
		tip_text[0] = 0;
		char *e = str_cat(tip_text, lang_tr("Bureau "));
		*e++ = (char)('1' + d);
		*e = 0;
		const char *name = dl_desk[d];
		/* (le nom d'origine, son numéro : rien de plus) */
		if (name[0] && (name[0] != '1' + d || name[1])) {
			e = str_cat(e, " : ");
			str_cat(e, name);
		}
		long y;
		desk_box(d, &x, &y);
		x -= 60;
		text = tip_text;
	} else if (h >= 0) {
		text = app_label(sl_app[h]);
		x = sl_x[h];
	} else if (h == HIT_APPS) {
		x = 8;
		text = "Applications";
	} else if (h == HIT_GEAR) {
		x = gear_x - 50;
		text = "Réglages";
	} else if (h == HIT_POWER) {
		x = power_x - 40;
		text = "Éteindre";
	} else if (h == HIT_CLIP) {
		x = clip_x - 50;
		text = "Presse-papiers";
	} else {
		x = trash_x - 30;
		text = "Corbeille";
	}
	/* (tout passe par lang_tr, le texte de l'espace et le libellé compris : comme l'asm) */
	ui_tip_at(x, -28, (long)lang_tr(text));
	draw();
}

/* ---------------------------------------------------------------------------
 *  Espaces de travail (les applications qui tournent : apprun)
 * --------------------------------------------------------------------------- */

/* les fenêtres, en petit dans les carrés -> 1 si quelque chose a changé */
static long desks_refresh(void)
{
	long d = ui_desk(-1, 0);
	u64 sig = (u64)d & 0xFFFF; /* (empreinte : l'espace, les fenêtres) */
	desk_cur = d & 0xFF;
	long kept = 0;
	for (long first = 0;; first += WE_PER_MSG) {
		long n = ui_win_details(first, (long)wdet);
		if (n <= first)
			break;
		n -= first;
		if (n > WE_PER_MSG)
			n = WE_PER_MSG;
		for (long k = 0; k < n; k++) {
			u8 *e = wdet + k * WE_SIZE;
			if (M32(e, WE_FLAGS) & (WF_FRAMELESS | WF_TOP | WF_BACK | WF_NOLIST))
				continue;
			u32 st = M32(e, WE_STATE);
			if (st & WES_REDUITE)
				continue;
			if (kept >= MINI_MAX)
				continue;
			struct mini *m = &minis[kept++];
			u64 dk = ((st >> WES_DESK) & 0xFF) | ((u64)(st & WES_KEYS) << 8); /* espace + 1 */
			long mx = *(int16_t *)(e + WE_X), my = *(int16_t *)(e + WE_Y);
			u64 mw = *(u16 *)(e + WE_W), mh = *(u16 *)(e + WE_H);
			m->desk = (u16)dk;
			m->x = (int16_t)mx;
			m->y = (int16_t)my;
			m->w = (u16)mw;
			m->h = (u16)mh;
			sig = sig * 33 + dk;
			sig ^= (u64)mx << 8;
			sig ^= (u64)my << 20;
			sig ^= mw << 32;
			sig ^= mh << 44;
		}
	}
	nminis = kept;
	u64 old = minis_sig;
	minis_sig = sig;
	return old != sig;
}

/* espace : son carré, ses fenêtres en petit (son numéro s'il n'en a pas) */
static void draw_square(long d)
{
	long bx, by;
	desk_box(d, &bx, &by);
	long sw = cur.sqw, sh = cur.sqh;
	u8 *tj = (u8 *)ui_tokens();
	int current = desk_cur == d;
	int dark = M32(tj, TJ_DARK) != 0;
	u32 wall; /* fond du carré : le courant, l'accent (contour et fond clair) ; les autres, un
		     verre plus dense, sans contour (17/11/2026) */
	if (current) {
		u32 c = M32(tj, TJ_ACCENT);
		ui_rbox(bx, by, sw, sh, 6, c, c);
		if (dark)
			wall = M32(tj, TJ_ACCENT_LIGHT);
		else
			wall = (u32)ui_mix(M32(tj, TJ_ACCENT), 0x00FFFFFF, 210);
		ui_rbox(bx + 1, by + 1, sw - 2, sh - 2, 5, wall, wall);
	} else {
		int hov = hover - HIT_DESK == d;
		u32 g = dark ? (u32)ui_mix(M32(tj, TJ_DOCK) & 0xFFFFFF, 0x00FFFFFF, hov ? 52 : 30)
			     : M32(tj, TJ_SURFACE);
		u32 t = dark ? (hov ? 0x28 : 0x40) : (hov ? 0x08 : 0x38);
		glass_rbox(bx, by, sw, sh, 6, (g & 0xFFFFFF) | (t << 24));
		wall = g & 0xFFFFFF; /* (l'encre du numéro se choisit sur sa teinte) */
	}
	int any = 0; /* ses fenêtres (en a-t-il ?) */
	for (long i = 0; i < nminis; i++) {
		struct mini *m = &minis[i];
		long md = m->desk & 0xFF; /* espace + 1 (0 : tous) */
		if (md) {
			if (md - 1 != d)
				continue;
		} else if (!current) { /* (sur tous : dans le courant) */
			continue;
		}
		long top = scr[SCR_TOP]; /* écran, sans la barre du haut */
		long scw = scr[SCR_W], sch = scr[SCR_H] - top;
		long aw = sw - 4, ah = sh - 4; /* place dans le carré */
		long x0 = m->x;
		long x1 = x0 * aw / scw;
		long x2 = (x0 + m->w) * aw / scw;
		long y0 = m->y - top;
		long y1 = y0 * ah / sch;
		long y2 = (y0 + m->h) * ah / sch;
		if (x1 < 0) /* dans le carré */
			x1 = 0;
		if (y1 < 0)
			y1 = 0;
		if (x2 > aw)
			x2 = aw;
		if (y2 > ah)
			y2 = ah;
		long w = x2 - x1, h = y2 - y1;
		if (w < 3 || h < 3)
			continue;
		any = 1;
		long x = x1 + bx + 2, y = y1 + by + 2;
		u32 t = M32(tj, TJ_TITLE_INACTIVE); /* le clavier : le cadre actif */
		if ((m->desk & 0x100) && current)
			t = M32(tj, TJ_TITLE_ACTIVE);
		t &= 0xFFFFFF;
		ui_fill(x, y, w, h, (u32)ui_tone(t, 176));
		ui_frame(x, y, w, h, (u32)ui_tone(t, 70));
		ui_fill(x + 1, y + 1, w - 2, 2, t);
	}
	if (any)
		return;
	u32 ink = (u32)ui_ink(wall); /* vide : son numéro */
	if (!current) /* (sur le verre : l'encre secondaire, comme les boutons) */
		ink = M32(tj, TJ_TEXT2);
	tip_num[0] = (char)('1' + d);
	tip_num[1] = 0;
	long font = current ? (long)ui.font_b : (long)ui.font;
	long tw = ui_text_width((long)tip_num, 12, font);
	long x = bx + (long)((u64)(sw - tw) >> 1);
	long y = by + (long)((u64)sh >> 1) + 4;
	ui_text((long)tip_num, x, y, ink, 12, font, 0);
}

/* ---------------------------------------------------------------------------
 *  Dessin
 * --------------------------------------------------------------------------- */

/* --- Le verre (glass.c, commun avec la barre de menus ; docs/DESIGN-ELEGANT.md 8.9) */

/* -> couleur des séparateurs (translucide) */
static u32 sep_colour(void)
{
	u8 *tj = (u8 *)ui_tokens();
	u32 c = M32(tj, TJ_DARK) ? M32(tj, TJ_LINE_STRONG) : M32(tj, TJ_TEXT3);
	return (c & 0xFFFFFF) | 0xA0000000;
}

/* x, largeur : le fond d'un élément survolé, un verre plus dense et plus clair */
static void hover_box(long x, long w)
{
	glass_rbox(x, 5, w, cur.dh - 10, 9, glass_hover());
}

/* trait vertical à ce x */
static void draw_sep(long x)
{
	ui_fill(x, 14, 1, cur.dh - 28, sep_colour());
}

/* glyphe, x du bouton, couleur : au centre du bouton (20 px pour un bouton de 34) */
static void btn_glyph(const char *name, long x, u32 colour)
{
	long side = ((cur.btn >> 1) + 3) & ~1L;
	x += (long)((u64)(cur.btn - side) >> 1);
	long y = (long)((u64)(cur.dh - side) >> 1);
	ui_glyph((long)name, x, y, side, colour);
}

/* le bouton « apps » : une pastille de verre plus dense (teintée de l'accent au survol), la
 * grille de 3 x 3 à l'encre du texte (de l'accent au survol) */
static void draw_apps(void)
{
	u8 *tj = (u8 *)ui_tokens();
	long btn = cur.btn;
	long y = (long)((u64)(cur.dh - btn) >> 1);
	int dark = M32(tj, TJ_DARK) != 0;
	u32 face, ink;
	if (hover == HIT_APPS) {
		face = (M32(tj, TJ_ACCENT_LIGHT) & 0xFFFFFF) | (dark ? 0x18000000 : 0x10000000);
		ink = M32(tj, dark ? TJ_ACCENT_VIVID : TJ_ACCENT);
	} else {
		face = dark ? (((u32)ui_mix(M32(tj, TJ_DOCK) & 0xFFFFFF, 0x00FFFFFF, 30) & 0xFFFFFF) | 0x38000000)
			    : ((M32(tj, TJ_SURFACE) & 0xFFFFFF) | 0x30000000);
		ink = M32(tj, TJ_TEXT1);
	}
	glass_rbox(8, y, btn, btn, 10, face);
	long side = ((btn >> 1) + 0) & ~1L; /* la grille, centrée */
	long off = (long)((u64)(btn - side) >> 1);
	ui_glyph((long)"glyph-apps", off + 8, y + off, side, ink);
}

/* glyphe, x, code HIT_* : un bouton à droite. Les quatre (corbeille, presse-papiers,
 * réglages, éteindre) : le même jeu au trait, la même encre secondaire, l'encre du texte au
 * survol (éteindre : le rouge du thème) */
static void draw_btn(const char *name, long x, long code)
{
	u32 c = tok(TJ_TEXT2);
	if (hover == code) {
		hover_box(x, cur.btn);
		c = tok(code == HIT_POWER ? TJ_DANGER : TJ_TEXT1);
	}
	btn_glyph(name, x, c);
}

static void draw(void)
{
	u8 *tj = (u8 *)ui_tokens();
	long dh = cur.dh;
	glass_set(cur.dh, TJ_DOCK, GLASS_EDGE_TOP);
	glass_draw(scr[SCR_W]); /* toute la largeur, le verre */
	draw_apps();
	for (long i = 0; i < nslots; i++) { /* --- emplacements */
		long x = sl_x[i], sw = slot_w;
		if (hover == i)
			hover_box(x + 1, sw - 2);
		if (npinned == i && i) /* raccourcis | applications ouvertes */
			draw_sep(x);
		long px = icon_px;
		long app = sl_app[i];
		ui_icon((long)ap_nom[app], x + (long)((u64)(sw - px) >> 1), (long)((u64)(dh - px) >> 1) - 1, px);
		if (pids[app]) { /* un trait sous une application ouverte */
			u32 v = M32(tj, TJ_ACCENT_VIVID);
			ui_rbox(x + (long)((u64)sw >> 1) - 5, dh - 5, 10, 3, 1, v, v);
		}
	}
	draw_sep(desk_x - 7); /* --- à droite */
	for (long d = 0; d < dl_ndesks; d++)
		draw_square(d);
	if (trash_x >= 0)
		draw_btn("glyph-trash", trash_x, HIT_TRASH);
	draw_btn("glyph-clipboard", clip_x, HIT_CLIP);
	draw_btn("glyph-gear", gear_x, HIT_GEAR);
	draw_btn("glyph-power", power_x, HIT_POWER);
	ui_update_all();
}

/* ---------------------------------------------------------------------------
 *  Menu contextuel d'un emplacement (clic droit)
 * --------------------------------------------------------------------------- */

#ifndef UMT_MENU
#define UMT_MENU 6 /* ui_metric : ligne d'un menu */
#endif
#define POP_PAD 6   /* marge verticale d'un menu (UK_POP_PAD d'UIKit) */
#define POP_SEP 11  /* hauteur d'un séparateur (UK_POP_SEP) */
#define POP_SHADOW 4 /* son ombre (UK_OMBRE) */
#define POP_W 220

enum { PM_NEW, PM_PIN, PM_UNPIN, PM_SETTINGS }; /* les entrées */

/* application -> son rang parmi les raccourcis (-1 : pas épinglée) */
static long pin_index(long app)
{
	for (long i = 0; i < pan_npin; i++)
		if (str_eq(pan_pin[i], ap_nom[app]))
			return i;
	return -1;
}

/* emplacement (-1 : la zone des icônes, hors d'un emplacement), x du clic : le menu au-dessus
 * du panel. Sur un emplacement : l'application, si elle est ouverte (un clic en lance un autre
 * process) ; « Détacher du panneau » (raccourci) ou « Épingler au panneau » (application
 * ouverte). Partout : « Paramètres du panneau » (Réglages, son volet) */
static void slot_menu(long h, long x)
{
	long app = h >= 0 ? sl_app[h] : -1;
	static char labels[AP_LIB + 192];
	long act[6], n = 0, seps = 0;
	char *o = labels;
	if (app >= 0) {
		if (pids[app]) {
			o = str_cat(o, app_label(app)) + 1;
			act[n++] = PM_NEW;
			o = str_cat(o, "-") + 1;
			act[n++] = -1;
			seps++;
		}
		if (pin_index(app) >= 0) {
			o = str_cat(o, lang_tr("Détacher du panneau")) + 1;
			act[n++] = PM_UNPIN;
		} else {
			if (pan_npin >= PANEL_MAXPINS) /* (plus de place : grisé) */
				*o++ = 1;
			o = str_cat(o, lang_tr("Épingler au panneau")) + 1;
			act[n++] = PM_PIN;
		}
		o = str_cat(o, "-") + 1;
		act[n++] = -1;
		seps++;
		x = sl_x[h];
	}
	o = str_cat(o, lang_tr("Paramètres du panneau")) + 1;
	act[n++] = PM_SETTINGS;
	*o = 0;
	long row = ui_metric(UMT_MENU);
	if (row <= 0)
		row = 28;
	long mh = 2 * POP_PAD + (n - seps) * row + seps * POP_SEP; /* au-dessus du panel */
	long y = scr[SCR_H] - cur.dh - mh - POP_SHADOW;
	hover_set(-1);
	/* (ui_popup rend plusieurs registres : sur le PC, une structure, par krt_call_r4 comme la
	 * barre de menus ; un appel direct décale les arguments) */
	long c = krt_call_r4(ui_popup, x, y, (long)labels, POP_W).r0;
	if (c < 0 || c >= n)
		return;
	if (act[c] == PM_SETTINGS) { /* Réglages, ouvert sur le volet du panel */
		ksys(SYS_SPAWN, (long)"/apps/control.app/main", (long)"panelconf", 0, 0, 0, 0);
		return;
	}
	if (act[c] == PM_NEW) {
		app_launch(app);
		return;
	}
	if (act[c] == PM_PIN) {
		if (pan_npin >= PANEL_MAXPINS)
			return;
		pan_add(ap_nom[app]);
	} else if (act[c] == PM_UNPIN) {
		long i = pin_index(app);
		if (i < 0)
			return;
		pan_npin--; /* les suivants remontent */
		mem_move(pan_pin[i], pan_pin[i + 1], (u64)(pan_npin - i) * AP_NOM);
	} else {
		return;
	}
	if (pan_save() < 0)
		say("panel : panel.ini non écrit\n");
	slots_build();
	draw();
}

/* nouvelle résolution : refait à sa taille, en bas de l'écran */
static void screen_follow(void)
{
	if (ui_screen((long)scr) < 0)
		return;
	metrics();
	ui_min_size(1, 1); /* (le panel est plus bas que le minimum) */
	ui_resize(scr[SCR_W], cur.dh);
	M64(pan_msg, WM_OP) = WS_OP_WIN_PLACE;
	M64(pan_msg, WM_A) = (u64)pan_win;
	M64(pan_msg, WM_B) = 0;
	M64(pan_msg, WM_C) = (u64)(scr[SCR_H] - cur.dh);
	M32(pan_msg, WM_LEN) = 0;
	ksys(SYS_WS_CALL, (long)pan_msg, 0, 0, 0, 0, 0);
	slots_build();
	draw();
}

/* une application lancée ou finie, un espace de travail, une fenêtre : redessine */
static void tick(void)
{
	long changed = apps_running();
	if (changed)
		slots_build();
	if (desks_refresh() | changed)
		draw();
}

int classic_main(void)
{
	ui_init((long)&ui);
	if (ui_screen((long)scr) < 0)
		_exit(0);
	applis_load();
	apps_sort();
	rc_load();
	pan_load();
	ui_desk(-1, dl_ndesks); /* autant d'espaces de travail que nommés */
	metrics();
	pan_win = ui_window_ex(0, scr[SCR_H] - cur.dh, scr[SCR_W], cur.dh, (long)"Panel",
			       WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST | WF_LISTEN | WF_RESERVE | WF_ALPHA | WF_BLUR);
	if (pan_win < 0) /* (en bas, toute la largeur) */
		_exit(0);
	apps_running();
	slots_build();
	desks_refresh();
	draw();
	/* le bouton d'alimentation du PC (acpid, docs/etude/BATTERIE-PC.md) : le même dialogue que le nôtre */
	long power_sub = bus_subscribe((long)"korin.power.button", 0);
	long mode_sub = bus_subscribe((long)"korin.shell.desktop", 0); /* (les Réglages changent le bureau) */
	long apps_sub = bus_subscribe((long)"korin.apps.changed", 0); /* (pkg : une app installée ou retirée) */
	say("panel : bureau prêt\n"); /* au journal, avec son heure : le repère du démarrage (docs/BOOT.md) */
	for (;;) {
		long t = ui_event(1000);
		if (!t) {
			/* (pas de sortie signalée : l'infobulle s'en va après quelques secondes) */
			if (hover != -1 && ++tip_ticks >= 4)
				hover_set(-1);
			tick();
			continue;
		}
		if (t == WEV_WINLIST) {
			tick();
			continue;
		}
		if (t == WEV_SCREEN) {
			screen_follow();
			tick();
			continue;
		}
		if (t == WEV_THEME) { /* autre thème : redessine */
			draw();
			continue;
		}
		if (t == WEV_BUS) {
			if (power_sub > 0 && *(u32 *)(ui.ev + BEV_SUB) == (u32)power_sub)
				ksys(SYS_SPAWN, (long)"/sys/shutdown", 0, 0, 0, 0, 0);
			if (mode_sub > 0 && *(u32 *)(ui.ev + BEV_SUB) == (u32)mode_sub)
				panel_respawn(); /* (panel.c) */
			if (apps_sub > 0 && *(u32 *)(ui.ev + BEV_SUB) == (u32)apps_sub) { /* le catalogue relu, la barre refaite */
				hover_set(-1);
				apps_reload();
				apps_sort();
				slots_build();
				draw();
				say("panel : catalogue relu (korin.apps.changed)\n");
			}
			continue;
		}
		if (t != WEV_MOUSE)
			continue;
		tip_ticks = 0;
		long h = hit(*(i32 *)(ui.ev + EV_X), *(i32 *)(ui.ev + EV_Y));
		if (*(u32 *)(ui.ev + EV_KIND) != 1) {
			hover_set(h);
			continue;
		}
		if (*(u32 *)(ui.ev + EV_CODE) & 2) { /* clic droit : le menu d'un emplacement, ou de la
							  zone des icônes (entre « apps » et les bureaux) */
			long mx = *(i32 *)(ui.ev + EV_X);
			if (h >= 0 && h < HIT_DESK)
				slot_menu(h, mx);
			else if (h == -1 && mx >= cur.btn + 16 && mx < desk_x - 10)
				slot_menu(-1, mx);
			continue;
		}
		if (h == -1)
			continue;
		if (h >= HIT_DESK) {
			ui_desk(h - HIT_DESK, 0);
		} else if (h >= 0) {
			app_open(sl_app[h]);
		} else if (h == HIT_APPS) {
			hover_set(-1);
			launcher();
			if (screen_todo) { /* (la résolution a changé pendant) */
				/* comme l'asm : seulement redessiné, sans screen_follow ni tour */
				screen_todo = 0;
				draw();
				continue;
			}
		} else if (h == HIT_TRASH) { /* la corbeille : dans Fichiers (lancé, pas ramené) */
			long a = app_find("files");
			if (a >= 0)
				app_launch(a);
			continue;
		} else {
			/* le presse-papiers ; « ⚙ » : les réglages ; alimentation : le dialogue de
			 * fin de session */
			const char *p = h == HIT_CLIP ? "/sys/clipboard"
				      : h == HIT_POWER ? "/sys/shutdown"
						       : "/apps/control.app/main";
			ksys(SYS_SPAWN, (long)p, 0, 0, 0, 0, 0);
			continue;
		}
		tick();
	}
}
