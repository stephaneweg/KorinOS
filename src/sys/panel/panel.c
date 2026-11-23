/* panel : le dash du bureau Elegant (/sys/panel, lancé par la session), comme celui de GNOME
 * (23/11/2026 ; docs/DESIGN-ELEGANT.md 8.9 et 19), quand le bureau est en vue d'ensemble
 * (le défaut ; avec desktop=classic de /etc/system.ini, le bureau classique : la barre d'avant,
 * panel_classic.c). Il n'est montré que dans la vue d'ensemble
 * (rôle WF_OVERVIEW : Elegant le cache dans la vue normale) : un bandeau de verre arrondi,
 * centré en bas de l'écran, de la largeur de son contenu :
 *   - les RACCOURCIS (panelconf : /etc/panel.ini), puis les applications ouvertes qui n'en ont
 *     pas ; un point sous une application ouverte ; un clic lance ou ramène (et referme la vue
 *     d'ensemble : Elegant) ; un clic droit, leur menu (épingler, détacher, lancer un autre
 *     process, paramètres) ;
 *   - après un séparateur, le bouton « Applications » (grille de 3 x 3) : le lanceur
 *     (launcher.c).
 * Plus de bureaux (le carrousel de la vue d'ensemble), ni corbeille, presse-papiers, réglages
 * ou éteindre (les réglages rapides de la barre de menus). Il ne réserve plus le bas de
 * l'écran. Fenêtre sans cadre, au-dessus, sans focus ; son ombre est dans sa marge. */
#include "panel.h"
#include "buskit_import.h"

#define SL_MAX 24   /* raccourcis et applications ouvertes */
#define HIT_APPS -2 /* ce qu'il y a sous le pointeur (0.. : emplacement) */
#define SH_M 0      /* marge de la fenêtre autour du bandeau (flat : pas d'ombre) */
#define BOTTOM 14   /* du bas du bandeau au bas de l'écran */
#define PAD 8       /* dans le bandeau, autour des emplacements */
#define SEP_W 13    /* le séparateur avant « Applications » */
#define TRANSPARENT_PX 0xFF000000u

/* par échelle (grande, moyenne, petite) : hauteur du bandeau, emplacement, icône, rayon */
static const int ech[3][4] = {
	{68, 60, 46, 14},
	{62, 54, 42, 13},
	{56, 48, 36, 12},
};
#define FLAT_T 0xE8     /* transparence du bandeau : un voile clair à peine (≈ 9 %) sur le fond sombre de la vue */
#define FLAT_T_HOVER 0xC8 /* survol (≈ 22 %) */
static struct {
	long bh, cw, icon, rad;
} cur;

struct krt_ui ui;
long scr[4];
long screen_todo;
long la_bottom;

static long pan_win;
static u8 pan_msg[WS_MSG];
static long pan_x, pan_y, pan_w, pan_h; /* la fenêtre, à l'écran */
static long nslots;
static long npinned; /* dont raccourcis */
static long apps_x;  /* le bouton « Applications » (dans la fenêtre) */
static long tip_ticks; /* secondes sans mouvement */
static long hover = -1; /* survolé (hit) */
static long sl_x[SL_MAX];
static u8 sl_app[SL_MAX];
static long shown;   /* 1 : la vue d'ensemble est ouverte (le dash se voit) */
static long grid_open; /* 1 : la grille des applications est montrée (« Applications » allumé) */

static void draw(void);


/* ---------------------------------------------------------------------------
 *  Disposition
 * --------------------------------------------------------------------------- */

static void metrics(void)
{
	long s = ui_scale();
	if (dl_size >= 0)
		s = dl_size;
	const int *e = ech[s];
	cur.bh = e[0];
	cur.cw = e[1];
	cur.icon = e[2];
	cur.rad = e[3];
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

/* les emplacements (les raccourcis, puis les applications ouvertes sans raccourci), la taille
 * et la place du dash -> 1 si la fenêtre doit changer de taille */
static long slots_build(void)
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
	long room = (scr[SCR_W] - 2 * SH_M - 2 * PAD - SEP_W - 40) / cur.cw - 1; /* (tient dans l'écran) */
	if (n > room)
		n = room > 0 ? room : 0;
	nslots = n;
	long x = SH_M + PAD;
	for (long i = 0; i < n; i++, x += cur.cw)
		sl_x[i] = x;
	if (n)
		x += SEP_W;
	apps_x = x;
	x += cur.cw + PAD + SH_M;
	long w = x, h = cur.bh + 2 * SH_M;
	long changed = w != pan_w || h != pan_h;
	pan_w = w;
	pan_h = h;
	pan_x = (scr[SCR_W] - w) / 2;
	pan_y = scr[SCR_H] - BOTTOM - cur.bh - SH_M;
	return changed;
}

/* point dans la fenêtre -> ce qu'il y a dessous (-1 : rien, HIT_APPS, ou un emplacement) */
static long hit(long x, long y)
{
	if (y < SH_M || y >= SH_M + cur.bh)
		return -1;
	if (x >= apps_x && x < apps_x + cur.cw)
		return HIT_APPS;
	for (long i = 0; i < nslots; i++)
		if (x >= sl_x[i] && x < sl_x[i] + cur.cw)
			return i;
	return -1;
}

/* la fenêtre à la taille et à la place du dash */
static void place(void)
{
	ui_min_size(1, 1); /* (le dash est plus bas que le minimum) */
	if (ui.w != pan_w || ui.h != pan_h)
		ui_resize(pan_w, pan_h);
	M64(pan_msg, WM_OP) = WS_OP_WIN_PLACE;
	M64(pan_msg, WM_A) = (u64)pan_win;
	M64(pan_msg, WM_B) = (u64)pan_x;
	M64(pan_msg, WM_C) = (u64)pan_y;
	M32(pan_msg, WM_LEN) = 0;
	ksys(SYS_WS_CALL, (long)pan_msg, 0, 0, 0, 0, 0);
}

static void relayout(void)
{
	slots_build();
	place();
	draw();
}

/* ce qui est survolé : son nom au-dessus du dash, redessiné s'il change */
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
	if (h >= 0) {
		text = app_label(sl_app[h]);
		x = sl_x[h];
	} else {
		x = apps_x - 20;
		text = lang_tr("Applications");
	}
	ui_tip_at(x, SH_M - 30, (long)text);
	draw();
}

/* ---------------------------------------------------------------------------
 *  Dessin
 * --------------------------------------------------------------------------- */

/* couverture (sur 16) du pixel (k, j) du coin haut-gauche d'un arrondi de rayon r */
static long corner_cov(long k, long j, long r)
{
	long cov = 0;
	for (long sy = 0; sy < 4; sy++)
		for (long sx = 0; sx < 4; sx++) {
			long px = 8 * k + 2 * sx + 1 - 8 * r, py = 8 * j + 2 * sy + 1 - 8 * r;
			if (px * px + py * py <= 64 * r * r)
				cov++;
		}
	return cov;
}

/* a, b (0xTTRRGGBB), k 0..256 : les quatre octets mêlés */
static u32 mix4(u32 a, u32 b, u32 k)
{
	u32 o = 0;
	for (u32 sh = 0; sh < 32; sh += 8) {
		u32 x = (a >> sh) & 0xFF, y = (b >> sh) & 0xFF;
		o |= ((x * (256 - k) + y * k) >> 8) << sh;
	}
	return o;
}

/* -> l'encre sur le fond de la vue d'ensemble (toujours sombre : le fond d'écran assombri par Elegant) */
static u32 dash_ink(void)
{
	return (u32)ui_ink(0x00202124) & 0xFFFFFF;
}

/* -> la teinte du bandeau : un voile de l'encre claire, à peine plus clair que le fond de la vue (flat, comme le
 * dash de GNOME ; 23/11/2026) */
static u32 flat_colour(void)
{
	return dash_ink() | (u32)FLAT_T << 24;
}

/* boîte arrondie unie, écrite dans les pixels : coins lissés sur le fond bg (0xTTRRGGBB) */
static void flat_rbox(long x, long y, long w, long h, long r, u32 c, u32 bg)
{
	u32 *px = ui.surf;
	long W = ui.w;
	if (2 * r > w)
		r = w / 2;
	if (2 * r > h)
		r = h / 2;
	for (long j = 0; j < h; j++) {
		long cr = j < r ? j : h - 1 - j;
		u32 *row = px + (y + j) * W + x;
		long inner = 0;
		if (cr < r) {
			for (long k = 0; k < r; k++) {
				long cov = corner_cov(k, cr, r);
				if (cov == 16)
					break;
				u32 m = mix4(bg, c, (u32)(cov * 16));
				row[k] = m;
				row[w - 1 - k] = m;
				inner = k + 1;
			}
		}
		for (long i = inner; i < w - inner; i++)
			row[i] = c;
	}
}

/* le bandeau : une teinte unie, sans dégradé, reflet ni ombre (flat) */
static void draw_body(void)
{
	u32 *px = ui.surf;
	for (long i = 0, n = ui.w * ui.h; i < n; i++)
		px[i] = TRANSPARENT_PX;
	flat_rbox(SH_M, SH_M, ui.w - 2 * SH_M, cur.bh, cur.rad, flat_colour(), TRANSPARENT_PX);
}

/* x, largeur : le fond d'un élément survolé, la teinte du bandeau un peu plus marquée */
static void hover_box(long x, long w)
{
	u32 bg = flat_colour();
	u32 c = dash_ink() | (u32)FLAT_T_HOVER << 24;
	flat_rbox(x + 3, SH_M + 5, w - 6, cur.bh - 10, cur.rad - 4 > 6 ? cur.rad - 4 : 6, c, bg);
}

static void draw_apps(void)
{
	u8 *tj = (u8 *)ui_tokens();
	long cw = cur.cw;
	int dark = M32(tj, TJ_DARK) != 0;
	u32 ink = dash_ink();
	if (hover == HIT_APPS || grid_open) {
		hover_box(apps_x, cw);
		ink = M32(tj, dark ? TJ_ACCENT_VIVID : TJ_ACCENT);
	}
	long side = (cur.icon * 2 / 3) & ~1L; /* la grille, centrée */
	ui_glyph((long)"glyph-apps", apps_x + (cw - side) / 2, SH_M + (cur.bh - side) / 2, side, ink);
}

static void draw(void)
{
	if (!ui.surf)
		return;
	draw_body();
	long top = SH_M, bh = cur.bh, cw = cur.cw, ic = cur.icon;
	for (long i = 0; i < nslots; i++) { /* --- emplacements */
		long x = sl_x[i];
		if (hover == i)
			hover_box(x, cw);
		long app = sl_app[i];
		ui_icon((long)ap_nom[app], x + (cw - ic) / 2, top + (bh - ic) / 2 - 3, ic);
		if (pids[app]) { /* un point sous une application ouverte */
			u32 v = dash_ink();
			ui_rbox(x + cw / 2 - 2, top + bh - 8, 5, 5, 2, v, v);
		}
	}
	if (nslots) { /* --- séparateur */
		ui_fill(apps_x - SEP_W / 2 - 1, top + 14, 1, bh - 28, dash_ink() | 0xB0000000);
	}
	draw_apps();
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
		x = pan_x + sl_x[h];
	}
	o = str_cat(o, lang_tr("Paramètres du panneau")) + 1;
	act[n++] = PM_SETTINGS;
	*o = 0;
	long row = ui_metric(UMT_MENU);
	if (row <= 0)
		row = 28;
	long mh = 2 * POP_PAD + (n - seps) * row + seps * POP_SEP; /* au-dessus du panel */
	long y = pan_y + SH_M - mh - POP_SHADOW - 6;
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
	relayout();
}

/* nouvelle résolution, autre taille : refait, centré en bas de l'écran */
static void screen_follow(void)
{
	if (ui_screen((long)scr) < 0)
		return;
	metrics();
	relayout();
}

/* la vue d'ensemble ouverte ? (WS_OP_OVERVIEW, lire) */
static long overview_open(void)
{
	M64(pan_msg, WM_OP) = WS_OP_OVERVIEW;
	M64(pan_msg, WM_A) = 2;
	M32(pan_msg, WM_LEN) = 0;
	return ksys(SYS_WS_CALL, (long)pan_msg, 0, 0, 0, 0, 0) == 1;
}

/* (le lanceur en grille, launcher.c) un événement du dash pendant la grille -> 1 : la fermer. Un clic sur
 * « Applications » la referme ; sur une application, la lance ou la ramène (Elegant referme la vue) ; la
 * vue fermée (un clic sur un bureau en haut, Échap...), la grille aussi */
long la_other(void)
{
	long t = *(u32 *)(ui.ev + EV_TYPE);
	if (t == WEV_WINLIST)
		return !overview_open();
	if (t != WEV_MOUSE)
		return 0;
	long mx = *(i32 *)(ui.ev + EV_X), my = *(i32 *)(ui.ev + EV_Y);
	long h = hit(mx, my);
	if (*(u32 *)(ui.ev + EV_KIND) != 1) {
		hover_set(h);
		return 0;
	}
	if (*(u32 *)(ui.ev + EV_CODE) & 2)
		return 0;
	if (h == HIT_APPS)
		return 1;
	if (h >= 0) {
		hover_set(-1);
		app_open(sl_app[h]);
		return 1;
	}
	return 0;
}

/* une application lancée ou finie, la vue ouverte ou fermée : refait */
static void tick(void)
{
	long changed = apps_running();
	long s = overview_open();
	if (s != shown) {
		shown = s;
		if (!s && hover != -1) /* (la bulle ne reste pas dans la vue normale) */
			hover_set(-1);
	}
	if (changed) {
		if (slots_build())
			place();
		draw();
	}
}

int classic_main(void); /* panel_classic.c */

/* le bureau a changé de genre (korin.shell.desktop, les Réglages : Elegant le sait déjà) : un panel neuf, qui
 * prend l'autre forme, puis celui-ci s'arrête (ses fenêtres partent avec lui) */
void panel_respawn(void)
{
	ksys(SYS_SPAWN, (long)"/sys/panel", 0, 0, 0, 0, 0);
	_exit(0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	/* le bureau classique (desktop=classic : Elegant n'a pas de vue d'ensemble) : la barre d'avant */
	M64(pan_msg, WM_OP) = WS_OP_OVERVIEW;
	M64(pan_msg, WM_A) = 2;
	M32(pan_msg, WM_LEN) = 0;
	if (ksys(SYS_WS_CALL, (long)pan_msg, 0, 0, 0, 0, 0) < 0)
		return classic_main();
	ui_init((long)&ui);
	if (ui_screen((long)scr) < 0)
		_exit(0);
	applis_load();
	apps_sort();
	rc_load();
	pan_load();
	ui_desk(-1, dl_ndesks); /* autant d'espaces de travail que nommés (le carrousel) */
	metrics();
	apps_running();
	slots_build();
	pan_win = ui_window_ex(pan_x, pan_y, pan_w, pan_h, (long)"Panel",
			       WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST | WF_LISTEN | WF_ALPHA | WF_OVERVIEW);
	if (pan_win < 0)
		_exit(0);
	draw();
	/* le bouton d'alimentation du PC (acpid, docs/etude/BATTERIE-PC.md) : le dialogue d'extinction */
	long power_sub = bus_subscribe((long)"korin.power.button", 0);
	long mode_sub = bus_subscribe((long)"korin.shell.desktop", 0); /* (les Réglages changent le bureau) */
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
				panel_respawn();
			continue;
		}
		if (t != WEV_MOUSE)
			continue;
		tip_ticks = 0;
		long mx = *(i32 *)(ui.ev + EV_X), my = *(i32 *)(ui.ev + EV_Y);
		long h = hit(mx, my);
		if (*(u32 *)(ui.ev + EV_KIND) != 1) {
			hover_set(h);
			continue;
		}
		if (*(u32 *)(ui.ev + EV_CODE) & 2) { /* clic droit : le menu d'un emplacement, ou du dash */
			if (h >= 0)
				slot_menu(h, pan_x + mx);
			else if (h == -1 && my >= SH_M && my < SH_M + cur.bh)
				slot_menu(-1, pan_x + mx);
			continue;
		}
		if (h >= 0) {
			hover_set(-1);
			app_open(sl_app[h]); /* (Elegant referme la vue d'ensemble) */
		} else if (h == HIT_APPS) {
			hover_set(-1);
			/* la grille des applications de la vue d'ensemble (Elegant fait monter les bureaux en
			   rangée en haut, et la grille du bas), au-dessus du dash ; un clic sur « Applications »
			   la referme */
			la_bottom = scr[SCR_H] - pan_y - SH_M + 10;
			la_grid = 1;
			grid_open = 1;
			draw();
			launcher();
			grid_open = 0;
			la_grid = 0;
			la_bottom = 0;
			hover = -2; /* (redessiné sans survol : le pointeur a quitté le dash pendant la grille) */
			hover_set(-1);
			if (screen_todo) { /* (la résolution a changé pendant) */
				screen_todo = 0;
				screen_follow();
				continue;
			}
		} else {
			continue;
		}
		tick();
	}
}
