/* panel : la barre du bureau Elegant (/sys/panel, lancé par la session), à la place du
 * dock (sys/dock, conservé, n'est plus lancé). Traduit de panel.asm.
 * Collée au bas de l'écran, toute sa largeur, aux couleurs du thème :
 *   - à gauche, le bouton « apps » (grille de 3 x 3) : le lanceur (launcher.c) ;
 *   - au centre, les RACCOURCIS (panelconf : /etc/panel.ini), puis les applications
 *     ouvertes qui n'en ont pas ; un clic lance ou ramène ;
 *   - à droite, les ESPACES DE TRAVAIL (un carré chacun, leurs fenêtres en petit), la
 *     corbeille, le presse-papiers (sys/clipboard), les réglages, l'alimentation.
 * Un trait sous une application ouverte. Fenêtre sans cadre, au-dessus des autres, sans
 * focus ; elle réserve le bas de l'écran. */
#include "panel.h"

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

struct krt_ui ui;
long scr[4];
long screen_todo;

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
	trash_x = -1; /* corbeille, */
	if (dl_trash) {
		x -= cur.btn + 4;
		trash_x = x;
	}
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
	u32 c; /* contour */
	if (current)
		c = M32(tj, TJ_ACCENT);
	else
		c = (u32)ui_tone(M32(tj, TJ_DOCK) & 0xFFFFFF, 70);
	ui_rbox(bx, by, sw, sh, 5, c, c);
	u32 wall; /* fond du carré */
	if (current) {
		if (M32(tj, TJ_DARK))
			wall = (u32)ui_mix(M32(tj, TJ_ACCENT_LIGHT), 0x00FFFFFF, 0);
		else
			wall = (u32)ui_mix(M32(tj, TJ_ACCENT), 0x00FFFFFF, 196);
	} else {
		wall = (u32)ui_tone(M32(tj, TJ_DOCK) & 0xFFFFFF, hover - HIT_DESK == d ? 150 : 128);
	}
	ui_rbox(bx + 1, by + 1, sw - 2, sh - 2, 4, wall, wall);
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
	if (!current)
		ink = (u32)ui_mix(wall, ink, 150);
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

/* -> couleur des séparateurs (avec transparence 0x66) */
static u32 sep_colour(void)
{
	u8 *tj = (u8 *)ui_tokens();
	u32 c;
	if (M32(tj, TJ_DARK))
		c = M32(tj, TJ_LINE);
	else
		c = (u32)ui_tone(M32(tj, TJ_DOCK) & 0xFFFFFF, 100);
	return c | 0x66000000;
}

/* x, largeur : le fond d'un élément survolé */
static void hover_box(long x, long w)
{
	u32 dock = tok(TJ_DOCK);
	u32 c = (u32)ui_tone(dock & 0xFFFFFF, 158) | (dock & 0xFF000000);
	ui_rbox(x, 4, w, cur.dh - 8, 10, c, c);
}

/* trait vertical à ce x */
static void draw_sep(long x)
{
	ui_fill(x, 12, 1, cur.dh - 24, sep_colour());
}

/* glyphe, x du bouton, couleur : au centre du bouton, côté moitié du bouton */
static void btn_glyph(const char *name, long x, u32 colour)
{
	long side = ((cur.btn >> 1) + 1) & ~1L;
	x += (long)((u64)(cur.btn - side) >> 1);
	long y = (long)((u64)(cur.dh - side) >> 1);
	ui_glyph((long)name, x, y, side, colour);
}

/* -> encre lisible sur la barre */
static u32 bar_ink(void)
{
	return (u32)ui_ink(tok(TJ_DOCK) & 0xFFFFFF);
}

/* le bouton « apps » : pastille neutre (accent clair au survol), grille de 3 x 3 */
static void draw_apps(void)
{
	u8 *tj = (u8 *)ui_tokens();
	long btn = cur.btn;
	long y = (long)((u64)(cur.dh - btn) >> 1);
	u32 face = M32(tj, TJ_SUNKEN);
	if (hover == HIT_APPS)
		face = M32(tj, TJ_ACCENT_LIGHT);
	ui_glyph((long)"glyph-pill", 8, y, btn, face);
	u32 ink = (u32)ui_ink(face);
	long side = ((btn >> 1) + 2) & ~1L; /* la grille, centrée */
	long off = (long)((u64)(btn - side) >> 1);
	ui_glyph((long)"glyph-apps", off + 8, y + off, side, ink);
}

/* icône, x, code HIT_* : un bouton à droite */
static void draw_btn(const char *name, long x, long code)
{
	if (hover == code)
		hover_box(x, cur.btn);
	btn_glyph(name, x, tok(TJ_TEXT2)); /* monochrome, comme ses voisins */
}

/* le bouton des réglages, à l'encre de la barre */
static void draw_gear(void)
{
	if (hover == HIT_GEAR)
		hover_box(gear_x, cur.btn);
	btn_glyph("glyph-gear", gear_x, bar_ink());
}

/* le bouton d'alimentation, couleur de danger */
static void draw_power(void)
{
	if (hover == HIT_POWER)
		hover_box(power_x, cur.btn);
	u32 c = hover == HIT_POWER ? tok(TJ_DANGER) : tok(TJ_TEXT2); /* (rouge au survol) */
	btn_glyph("glyph-power", power_x, c);
}

/* le bouton du presse-papiers, à l'encre de la barre */
static void draw_clip(void)
{
	if (hover == HIT_CLIP)
		hover_box(clip_x, cur.btn);
	btn_glyph("glyph-clipboard", clip_x, bar_ink());
}

static void draw(void)
{
	u8 *tj = (u8 *)ui_tokens();
	long dh = cur.dh, w = scr[SCR_W];
	ui_fill(0, 0, w, dh, M32(tj, TJ_DOCK)); /* toute la largeur, le dock du thème */
	ui_fill(0, 0, w, 1, sep_colour());      /* un trait en haut */
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
			ui_rbox(x + (long)((u64)sw >> 1) - 7, dh - 5, 14, 3, 1, v, v);
		}
	}
	draw_sep(desk_x - 7); /* --- à droite */
	for (long d = 0; d < dl_ndesks; d++)
		draw_square(d);
	if (trash_x >= 0)
		draw_btn("glyph-trash", trash_x, HIT_TRASH);
	draw_gear();
	draw_clip();
	draw_power();
	ui_update_all();
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

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
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
			       WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST | WF_LISTEN | WF_RESERVE | WF_ALPHA);
	if (pan_win < 0) /* (en bas, toute la largeur) */
		_exit(0);
	apps_running();
	slots_build();
	desks_refresh();
	draw();
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
		if (t != WEV_MOUSE)
			continue;
		tip_ticks = 0;
		long h = hit(*(i32 *)(ui.ev + EV_X), *(i32 *)(ui.ev + EV_Y));
		if (*(u32 *)(ui.ev + EV_KIND) != 1) {
			hover_set(h);
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
