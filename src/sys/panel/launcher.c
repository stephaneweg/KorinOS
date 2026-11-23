/* launcher.c : le lanceur du panel, traduit de launcher.inc. Le bouton « apps » cache les
 * fenêtres (rideau d'Elegant, WF_CURTAIN) et montre une feuille opaque : les catégories dans
 * un rail à gauche, une recherche, les applications de la catégorie choisie. La recherche
 * porte sur la catégorie choisie, sans la casse ni les accents. Comme un menu, la fenêtre
 * capture souris et clavier : un clic hors de la feuille (le bouton « apps » compris), ou
 * Échap, la ferme. Partagé avec l'accueil du mode poche (sys/home) : voir panel.h ; un
 * onglet « Récentes » en tête du rail (recent). */
#include "panel.h"

#define LA_SHEET_R 14
#define LA_RAIL_M 8 /* rail : marge dans la feuille, */
#define LA_RAIL_W 160
#define LA_RAIL_R 10
#define LA_RAIL_IN 8 /* marge en haut et en bas */
#define LA_ROW_H 34  /* ligne : pas (moins s'il y en a trop), */
#define LA_ROW_X 6   /* retrait, */
#define LA_ROW_R 8   /* rayon de sa boîte */
#define LA_GAP 12    /* entre le rail et la recherche */
#define LA_BOX_Y 12  /* recherche : haut dans la feuille, */
#define LA_BOX_H 38
#define LA_BOX_R 10
#define LA_BOX_TX 30 /* le champ commence après la loupe */
#define LA_GRID_Y 11 /* sous la recherche */
#define LA_GRID_B 12 /* sous la grille */
#define LA_ICON_Y 11 /* icône : haut dans la cellule */
#define LA_HI_X 6    /* boîte de survol : retrait, */
#define LA_HI_Y 3
#define LA_HI_R 10
#define LA_TAB_PX 14
#define LA_LBL_PX 13
#define LA_PAD 16
#define LA_MAXTABS (CA_MAX + 1)
#define LA_ALL 0xFF    /* onglet « Toutes » */
#define LA_RECENT 0xFE /* onglet « Récentes » */
#define LA_NORM 96     /* texte normalisé */

/* cellule (largeur mini, hauteur, icône) par échelle */
static const u8 la_ech[3][3] = {{112, 94, 48}, {112, 94, 48}, {96, 84, 40}};
/* le second octet d'un caractère Latin-1 en UTF-8 (0xC3 xx), & 0x3F -> sa lettre sans accent */
static const char la_fold[] = "aaaaaaaceeeeiiiidnooooo*ouuuuyts"
			      "aaaaaaaceeeeiiiidnooooo/ouuuuyty";

static long la_win;
static long la_prev;         /* la fenêtre du programme, rendue à la fin */
static long la_fixed = 1;    /* onglets fixes en tête (Récentes, Toutes) */
static long la_w_box;
static long la_geom[3];      /* y, largeur, hauteur de la fenêtre */
static long la_cx, la_cy, la_cw, la_ch; /* la feuille */
static long la_gx, la_gw;    /* recherche et grille : x, largeur, */
static long la_grid_x;       /* (la_grid) x de la grille (la recherche a le sien) */
static long la_by;           /* haut de la recherche */
static long la_row = LA_ROW_H; /* pas du rail */
static long la_cols = 1, la_rows = 1; /* grille : colonnes, rangées, */
static long la_cellw = 1, la_cellh = 1; /* cellule, */
static long la_gy;           /* haut */
static long la_icon = 48;
static long la_ntab;
static long la_cur;          /* onglet choisi */
static long la_n;            /* applications montrées */
static long la_scroll;       /* rangée du haut */
static long la_sel = -1;     /* Entrée, flèches */
static long la_zone;         /* le clavier : 0 recherche, 1 grille, 2 rail */
static long la_hover = -1;
static long la_tab_hover = -1;
static long la_press = -1;   /* case de l'appui */
static long la_bar_drag;
static u8 la_tab[LA_MAXTABS];
/* style « grille de la vue d'ensemble » (la_grid, 23/11/2026) : la fenêtre WF_APPGRID est transparente
 * sur le fond gris de la vue d'Elegant ; la recherche centrée en haut, les catégories en pilules sur une
 * ligne, une grande grille d'icônes ; un clic ailleurs, Échap : retour aux bureaux */
long la_grid;
static long la_tx[LA_MAXTABS], la_tw[LA_MAXTABS]; /* (la_grid) les pilules : x, largeur */
static long la_ty;           /* (la_grid) haut des pilules */
#define LA_PILL_H 32
#define LA_DOTS_H 34 /* (la_grid) sous la grille : les points des pages */
#define LA_ARROW_W 44 /* (la_grid) les flèches des pages, sur les côtés */
static u64 la_wheel_t;      /* (la_grid) dernière page tournée à la molette (ms) */
#define LA_GRID_MAXC 8
static u8 la_res[AP_MAX];
static char la_q[LA_NORM];
static char la_t[LA_NORM];

static void la_draw(void);
static void la_tab_draw(long t, long show);

static u32 tok(long off)
{
	return M32(ui_tokens(), off);
}

/* application -> 1 si le lanceur ne la montre pas : une partie du bureau (Shell), un émulateur
 * (Émulateurs : la Game Library les lance, comme le lanceur d'Onyx sans « Emulators ») */
static long la_hidden(long a)
{
	return pan_shell(a) || cat_hidden(ap_cat[a]);
}

/* catégorie (LA_ALL : toutes, LA_RECENT : récentes) -> son nom, traduit (ca_nom : celui du
 * manifeste, qui sert de clé) */
static const char *la_cat_name(long c)
{
	if (c == LA_ALL)
		return lang_tr("Toutes");
	if (c == LA_RECENT)
		return lang_tr("Récentes");
	return lang_tr(ca_nom[c]);
}

/* les onglets : « Récentes » s'il y en a, « Toutes », puis les catégories qui ont des
 * applications, par nom */
static void la_cats(void)
{
	rc_load(); /* (une autre coquille a pu l'écrire) */
	long n = 0;
	for (long i = 0; i < rc_n; i++)
		if (!la_hidden(rc_app[i])) {
			la_tab[0] = LA_RECENT;
			n = 1;
			break;
		}
	la_tab[n++] = LA_ALL;
	la_fixed = n; /* (les onglets fixes, en tête) */
	for (long c = 0; c < ca_n; c++) {
		long a;
		for (a = 0; a < ap_n; a++) /* une application montrée ? */
			if (ap_cat[a] == c && !la_hidden(a))
				break;
		if (a == ap_n)
			continue;
		la_tab[n] = (u8)c; /* insertion, par nom */
		for (long j = n++; (u64)j > (u64)la_fixed; j--) {
			if (str_ci_cmp(la_cat_name(la_tab[j - 1]), la_cat_name(la_tab[j])) <= 0)
				break;
			u8 t = la_tab[j - 1];
			la_tab[j - 1] = la_tab[j];
			la_tab[j] = t;
		}
	}
	la_ntab = n;
}

static const char *la_cat_name(long c);

/* (la_grid) la disposition : la fenêtre entre la rangée des bureaux d'Elegant (13 % de l'écran, en haut,
 * overview.c) et le dash ; la recherche, les pilules, la grille, centrées */
static void la_grid_metrics(void)
{
	long W = scr[SCR_W], H = scr[SCR_H];
	long y0 = scr[SCR_TOP] + H * 2 / 100 + 6 + H * 13 / 100 + 14;
	la_geom[0] = y0;
	la_geom[1] = W;
	la_geom[2] = H - la_bottom - y0;
	la_cx = 0;
	la_cy = 0;
	la_cw = W;
	la_ch = la_geom[2];
	long bw = W - 80 < 460 ? W - 80 : 460; /* la recherche */
	la_gx = (W - bw) / 2;
	la_gw = bw;
	la_by = 4;
	la_ty = la_by + LA_BOX_H + 14; /* les pilules, centrées */
	long total = 0;
	for (long t = 0; t < la_ntab; t++) {
		la_tw[t] = ui_text_width((long)la_cat_name(la_tab[t]), LA_TAB_PX, (long)ui.font_b) + 28;
		total += la_tw[t] + 8;
	}
	total -= 8;
	long x = (W - total) / 2;
	if (x < 8)
		x = 8;
	for (long t = 0; t < la_ntab; t++) {
		la_tx[t] = x;
		x += la_tw[t] + 8;
	}
	long s = ui_scale(); /* la grille : grandes cases, 8 colonnes au plus */
	long cw = s == 2 ? 118 : 136, ch = s == 2 ? 104 : 120;
	la_icon = s == 2 ? 48 : 60;
	long cols = (W - 2 * (LA_ARROW_W + 20)) / cw; /* (la place des flèches des pages) */
	if (cols > LA_GRID_MAXC)
		cols = LA_GRID_MAXC;
	if (cols < 1)
		cols = 1;
	la_cols = cols;
	la_cellw = cw;
	la_cellh = ch;
	long gx = (W - cols * cw) / 2;
	la_gy = la_ty + LA_PILL_H + 22;
	long rows = (la_ch - la_gy - LA_DOTS_H) / ch; /* (sous la grille : les points des pages) */
	la_rows = rows < 1 ? 1 : rows;
	/* (la_gx et la_gw servent à la recherche : la grille garde son x dans la_grid_x) */
	la_grid_x = gx;
}

/* la disposition : fenêtre, feuille, rail, recherche, grille (calculs non signés, comme
 * l'asm) */
static void la_metrics(void)
{
	ui_screen((long)scr); /* (la barre du haut a pu venir après nous) */
	if (la_grid) {
		la_grid_metrics();
		return;
	}
	u64 w = (u64)scr[SCR_W];
	u64 h = (u64)(scr[SCR_H] - scr[SCR_TOP] - scr[SCR_BOTTOM] - la_bottom);
	la_geom[0] = scr[SCR_TOP]; /* y, largeur, hauteur de la fenêtre */
	la_geom[1] = (long)w;
	la_geom[2] = (long)h;
	u64 m = w < 900 ? 8 : 20; /* la feuille : marges, 1000 x 640 au plus */
	u64 cw = w - 2 * m;
	if (cw > 1000)
		cw = 1000;
	u64 ch = h - 2 * m;
	if (ch > 640)
		ch = 640;
	la_cx = (long)((w - cw) >> 1);
	la_cy = (long)((h - ch) >> 1);
	la_cw = (long)cw;
	la_ch = (long)ch;
	la_gx = la_cx + LA_RAIL_M + LA_RAIL_W + LA_GAP; /* recherche et grille : même colonne */
	la_gw = la_cx + la_cw - LA_PAD - la_gx;
	la_by = la_cy + LA_BOX_Y;
	u64 row = (ch - 2 * (LA_RAIL_M + LA_RAIL_IN)) / (u64)la_ntab; /* les lignes du rail tiennent toutes */
	if (row > LA_ROW_H)
		row = LA_ROW_H;
	la_row = (long)row;
	const u8 *e = la_ech[ui_scale()]; /* cellule selon l'échelle, à la largeur */
	la_icon = e[2];
	u64 cols = (u64)la_gw / e[0];
	if (cols < 1)
		cols = 1;
	la_cellw = (long)((u64)la_gw / cols);
	la_gy = la_cy + LA_BOX_Y + LA_BOX_H + LA_GRID_Y; /* haut de la grille */
	u64 rows = (u64)(la_cy + la_ch - LA_GRID_B - la_gy) / e[1];
	if (rows < 1)
		rows = 1;
	la_cols = (long)cols;
	la_rows = (long)rows;
	la_cellh = e[1];
}

/* en minuscules, sans accents (Latin-1 en UTF-8), borné à LA_NORM */
static void la_norm(char *d, const char *s)
{
	for (long n = LA_NORM - 1; n; n--) {
		u8 c = (u8)*s++;
		if (!c)
			break;
		if (c == 0xC3) {
			c = (u8)*s;
			if (!c)
				break;
			s++;
			c = (u8)la_fold[c & 0x3F];
		} else if ((u8)(c - 'A') <= 25) {
			c += 32;
		}
		*d++ = (char)c;
	}
	*d = 0;
}

/* -> 1 si text contient q */
static long la_has(const char *text, const char *q)
{
	for (;; text++) {
		long i = 0;
		while (q[i] && q[i] == text[i])
			i++;
		if (!q[i])
			return 1;
		if (!*text)
			return 0;
	}
}

/* la_res <- les applications de l'onglet choisi que la recherche garde */
static void la_filter(void)
{
	la_norm(la_q, (const char *)ui_w_get_text(la_w_box));
	long cat = la_tab[la_cur];
	const u8 *src = ap_order; /* par nom, toutes */
	long count = ap_n;
	if (cat == LA_RECENT) { /* (les récentes, dans leur ordre) */
		src = rc_app;
		count = rc_n;
	}
	long n = 0;
	for (long i = 0; i < count; i++) {
		long a = src[i];
		if (cat < LA_RECENT && ap_cat[a] != cat) /* (Toutes, Récentes : sans catégorie) */
			continue;
		if (la_hidden(a))
			continue;
		if (la_q[0]) { /* le libellé, puis le nom du paquet */
			la_norm(la_t, ap_lib[a]);
			if (!la_has(la_t, la_q)) {
				la_norm(la_t, ap_nom[a]);
				if (!la_has(la_t, la_q))
					continue;
			}
		}
		la_res[n++] = (u8)a;
	}
	la_n = n;
	la_scroll = 0;
	la_hover = -1;
	la_sel = la_q[0] && n ? 0 : -1; /* une recherche : Entrée prend la première */
}

/* onglet : le choisit */
static void la_tab_set(long t)
{
	la_cur = t;
	la_filter();
	la_draw();
}

/* -> rangées de la_res */
static long la_total(void)
{
	return (long)((u64)(la_n + la_cols - 1) / (u64)la_cols);
}

/* rangée du haut voulue : bornée, redessine si elle change */
static void la_cell_draw(long i, long show);

/* (la_grid) d'une page à l'autre (rangées du haut) : l'ancienne sort d'un côté, la nouvelle entre de
 * l'autre (LA_SLIDE_MS, « ease out ») ; seule la zone de la grille est redessinée à chaque image */
#define LA_SLIDE_MS 240
static long la_xoff; /* décalage des cases (le glissement) */
static void la_page_slide(long from, long to)
{
	long dir = to > from ? 1 : -1, W = la_geom[1], gy = la_gy, gh = la_rows * la_cellh;
	long per = la_rows * la_cols;
	u64 t0 = (u64)korin_sys0(SYS_TIME_MS);
	for (;;) {
		long t = (long)(((u64)korin_sys0(SYS_TIME_MS) - t0) * 65536 / LA_SLIDE_MS);
		if (t >= 65536)
			break;
		long u = 65536 - t; /* 1 - (1 - t)^3 */
		long e = 65536 - (u * u / 65536) * u / 65536;
		long dx = W * e / 65536;
		ui_clip(0, gy, W, gh);
		ui_fill(0, gy, W, gh, 0xFF000000);
		la_xoff = -dir * dx;
		la_scroll = from;
		for (long i = from * la_cols; i < from * la_cols + per && i < la_n; i++)
			la_cell_draw(i, 0);
		la_xoff = dir * (W - dx);
		la_scroll = to;
		for (long i = to * la_cols; i < to * la_cols + per && i < la_n; i++)
			la_cell_draw(i, 0);
		ui_clip(0, 0, 0, 0);
		ui_update(0, gy, W, gh);
		korin_sys1(SYS_SLEEP, 10);
	}
	la_xoff = 0;
}

static void la_scroll_to(long r)
{
	if (la_grid) /* (pages entières) */
		r = r < 0 ? 0 : r / la_rows * la_rows;
	long low = la_total() - la_rows; /* le plus bas */
	if (low < 0)
		low = 0;
	if (la_grid) /* (la dernière page commence à un multiple de la_rows) */
		low = la_total() ? (la_total() - 1) / la_rows * la_rows : 0;
	if (r > low)
		r = low;
	if (r < 0)
		r = 0;
	if (la_scroll == r)
		return;
	la_hover = -1; /* (la case survolée a changé) */
	if (la_grid && la_win > 0) /* (la grille : la page glisse vers la suivante) */
		la_page_slide(la_scroll, r);
	la_scroll = r;
	la_draw();
}

static u32 la_grid_ink(void);
static void la_soft_box(long x, long y, long w, long h, long r, u32 t);

/* (la_grid) -> pages de la_res, page montrée */
static long la_pages(void)
{
	long r = la_total();
	return r ? (r + la_rows - 1) / la_rows : 1;
}

/* (la_grid) x du premier point des pages, y de leur centre */
static void la_dots_at(long *x, long *y)
{
	long n = la_pages();
	*x = (la_geom[1] - (n * 16 - 8)) / 2;
	*y = la_gy + la_rows * la_cellh + LA_DOTS_H / 2;
}

/* (la_grid) point -> page de ce point (-1 : aucun), flèches : -2 précédente, -3 suivante */
static long la_page_at(long x, long y)
{
	long n = la_pages();
	if (n < 2)
		return -1;
	long cur = la_scroll / la_rows;
	long gy = la_gy, gh = la_rows * la_cellh;
	if (y >= gy && y < gy + gh) {
		if (cur > 0 && x < LA_ARROW_W + 8)
			return -2;
		if (cur < n - 1 && x >= la_geom[1] - LA_ARROW_W - 8)
			return -3;
	}
	long dx, dy;
	la_dots_at(&dx, &dy);
	if (y < dy - 10 || y > dy + 10 || x < dx - 4)
		return -1;
	long p = (x - dx + 4) / 16;
	return p < n ? p : -1;
}

/* (la_grid) les points des pages (la page montrée pleine) et les flèches, s'il y a plusieurs pages */
static void la_pages_draw(void)
{
	long n = la_pages();
	if (n < 2)
		return;
	long cur = la_scroll / la_rows, x, y;
	la_dots_at(&x, &y);
	for (long p = 0; p < n; p++, x += 16)
		la_soft_box(x, y - 4, 8, 8, 4, p == cur ? 0x10 : 0xA0);
	long gy = la_gy + la_rows * la_cellh / 2;
	u32 ink = la_grid_ink();
	if (cur > 0) { /* ‹ */
		la_soft_box(8, gy - 22, LA_ARROW_W, 44, 22, 0xD8);
		ui_text((long)"\xE2\x80\xB9", 8 + LA_ARROW_W / 2 - 5, gy + 9, ink, 26, (long)ui.font_b, 0);
	}
	if (cur < n - 1) { /* › */
		long ax = la_geom[1] - 8 - LA_ARROW_W;
		la_soft_box(ax, gy - 22, LA_ARROW_W, 44, 22, 0xD8);
		ui_text((long)"\xE2\x80\xBA", ax + LA_ARROW_W / 2 - 5, gy + 9, ink, 26, (long)ui.font_b, 0);
	}
}

/* (la_grid) l'encre sur le fond de la vue d'ensemble (sombre : le fond d'écran assombri par Elegant) */
static u32 la_grid_ink(void)
{
	return (u32)ui_ink(0x00303236);
}

/* (la_grid) boîte arrondie d'un voile clair (l'encre, transparence t) sur le fond transparent */
static void la_soft_box(long x, long y, long w, long h, long r, u32 t)
{
	ui_rbox(x, y, w, h, r, (la_grid_ink() & 0xFFFFFF) | t << 24, (la_grid_ink() & 0xFFFFFF) | t << 24);
}

/* x0, x1 = numéro dans la_res, 1 : montrer tout de suite */
static void la_cell_draw(long i, long show)
{
	if (i < 0 || i >= la_n)
		return;
	long r = (long)((u64)i / (u64)la_cols);
	long c = i - r * la_cols;
	r -= la_scroll;
	if (r < 0 || (u64)r >= (u64)la_rows)
		return;
	long cw = la_cellw, ch = la_cellh;
	long y = r * ch + la_gy;
	long x = c * cw + (la_grid ? la_grid_x : la_gx) + la_xoff;
	long app = la_res[i];
	u32 ink = la_grid ? la_grid_ink() : tok(TJ_TEXT1);
	if (la_grid) { /* sur le fond de la vue : transparent, survol d'un voile clair */
		ui_fill(x, y, cw, ch, 0xFF000000);
		if (la_hover == i || la_sel == i)
			la_soft_box(x + LA_HI_X, y + LA_HI_Y, cw - 2 * LA_HI_X, ch - 2 * LA_HI_Y, 14, 0xD8);
	} else {
		ui_fill(x, y, cw, ch, tok(TJ_FACE));
		if (la_hover == i || la_sel == i) { /* survol, sélection */
			u32 hi = (u32)ui_mix(tok(TJ_FACE), tok(TJ_LINE), 128);
			ui_rbox(x + LA_HI_X, y + LA_HI_Y, cw - 2 * LA_HI_X, ch - 2 * LA_HI_Y, LA_HI_R, hi, hi);
		}
	}
	long ic = la_icon;
	ui_icon((long)ap_nom[app], x + (long)((u64)(cw - ic) >> 1), y + LA_ICON_Y, ic);
	const char *label = app_label(app); /* libellé, centré */
	u64 tw = (u64)ui_text_width((long)label, LA_LBL_PX, (long)ui.font);
	u64 maxw = (u64)(cw - (2 * LA_HI_X + 4));
	if (tw > maxw)
		tw = maxw;
	ui_text_fit((long)label, x + (long)((u64)(cw - (long)tw) >> 1), y + ic + LA_ICON_Y + 16, ink,
		    LA_LBL_PX, (long)ui.font, (long)maxw);
	if (show)
		ui_update(x, y, cw, ch);
}

/* case choisie (-1 : aucune) : visible ; seules l'ancienne et la nouvelle sont redessinées,
 * sauf si la grille défile */
static void la_sel_set(long s)
{
	long old = la_sel;
	if (old == s)
		return;
	la_sel = s;
	if (s >= 0) {
		long r = (long)((u64)s / (u64)la_cols); /* sa rangée */
		if (la_grid && (r < la_scroll || r >= la_scroll + la_rows)) { /* (la page de la case) */
			la_scroll = r / la_rows * la_rows;
			la_draw();
			return;
		}
		if (r < la_scroll) {
			la_scroll = r;
			la_draw();
			return;
		}
		if ((u64)r >= (u64)(la_scroll + la_rows)) {
			la_scroll = r - la_rows + 1;
			la_draw();
			return;
		}
	}
	la_cell_draw(old, 1);
	la_cell_draw(s, 1);
}

/* déplacement : la sélection, gardée visible */
static void la_sel_move(long d)
{
	if (!la_n)
		return;
	long s = la_sel < 0 ? 0 : la_sel + d; /* (rien de choisi : la première) */
	if (s < 0)
		s = 0;
	if (s > la_n - 1)
		s = la_n - 1;
	la_sel_set(s);
}

/* la case choisie par défaut : la première s'il y a une recherche (Entrée la prend), sinon
 * aucune */
static void la_sel_default(void)
{
	la_sel_set(la_q[0] && la_n ? 0 : -1);
}

/* x0, x1, x2 = la boîte (la_box_at) : découpe à l'intérieur de son champ, sans son contour
 * ni son anneau de focus */
static void la_clip_at(long x, long y, long w)
{
	ui_clip(x + LA_BOX_TX + 7, y + 1, w - (LA_BOX_TX + 16), LA_BOX_H - 2);
}

static void la_clip_field(void)
{
	la_clip_at(la_gx, la_by, la_gw);
}

static void la_clip_off(void)
{
	ui_clip(0, 0, 0, 0);
}

/* zone du clavier : 0 la recherche (le champ a le focus), 1 la grille, 2 le rail (l'onglet
 * choisi a un anneau) */
static void la_zone_set(long z)
{
	long old = la_zone;
	if (old == z)
		return;
	la_zone = z;
	la_clip_field(); /* le champ : focus seulement en 0 */
	ui_w_focus(z == 0 ? la_w_box : 0);
	la_clip_off();
	if (z == 2 || old == 2) /* l'anneau du rail */
		la_tab_draw(la_cur, 1);
}

/* y dans la barre de défilement : la rangée du haut suit */
static void la_bar_set(long y)
{
	long d = la_total() - la_rows;
	if (d <= 0)
		return;
	long yy = y - la_gy;
	if (yy < 0)
		yy = 0;
	u64 bar = (u64)(la_rows * la_cellh); /* hauteur de la barre */
	la_scroll_to((long)(((u64)d * (u64)yy + (bar >> 1)) / bar));
}

/* point -> onglet (-1 : aucun) */
static long la_tab_at(long x, long y)
{
	if (la_grid) {
		if (y < la_ty || y >= la_ty + LA_PILL_H)
			return -1;
		for (long t = 0; t < la_ntab; t++)
			if (x >= la_tx[t] && x < la_tx[t] + la_tw[t])
				return t;
		return -1;
	}
	if ((u64)(x - (la_cx + LA_RAIL_M)) >= LA_RAIL_W)
		return -1;
	u64 t = (u64)(y - (la_cy + LA_RAIL_M + LA_RAIL_IN)) / (u64)la_row;
	if (t >= (u64)la_ntab)
		return -1;
	return (long)t;
}

/* point -> application dessous, numéro dans la_res (-1 : aucune) */
static long la_cell_at(long x, long y)
{
	long dx = x - (la_grid ? la_grid_x : la_gx);
	if (dx < 0)
		return -1;
	long dy = y - la_gy;
	if (dy < 0)
		return -1;
	u64 c = (u64)dx / (u64)la_cellw;
	if (c >= (u64)la_cols)
		return -1;
	u64 r = (u64)dy / (u64)la_cellh;
	if (r >= (u64)la_rows)
		return -1;
	u64 i = (r + (u64)la_scroll) * (u64)la_cols + c;
	if (i >= (u64)la_n)
		return -1;
	return (long)i;
}

/* onglet, 1 : montrer tout de suite */
static void la_tab_draw(long t, long show)
{
	if (la_grid) { /* une pilule : la choisie pleine du voile clair, les autres au survol */
		long x = la_tx[t], y = la_ty, w = la_tw[t], h = LA_PILL_H;
		ui_fill(x, y, w, h, 0xFF000000);
		if (la_cur == t)
			la_soft_box(x, y, w, h, h / 2, la_zone == 2 ? 0x70 : 0xA0);
		else if (la_tab_hover == t)
			la_soft_box(x, y, w, h, h / 2, 0xD0);
		long sel = la_cur == t;
		ui_text_fit((long)la_cat_name(la_tab[t]), x + 14, y + h / 2 + 5, la_grid_ink(), LA_TAB_PX,
			    (long)(sel ? ui.font_b : ui.font), w - 20);
		if (show)
			ui_update(x, y, w, h);
		return;
	}
	long x = la_cx + LA_RAIL_M + LA_ROW_X;
	long y = la_cy + LA_RAIL_M + LA_RAIL_IN + t * la_row;
	long h = la_row - 4; /* hauteur de la boîte */
	const long w = LA_RAIL_W - 2 * LA_ROW_X;
	u8 *tj = (u8 *)ui_tokens();
	ui_fill(x, y, w, h, M32(tj, TJ_SUNKEN));
	int box = 0;
	u32 c = 0;
	if (la_cur == t) {
		if (la_zone == 2) { /* choisi : boîte relevée ; le rail a le clavier : un anneau ACCENT */
			c = M32(tj, TJ_ACCENT);
			ui_rbox(x, y, w, h, LA_ROW_R, c, c);
			c = M32(tj, TJ_RAISED);
			ui_rbox(x + 2, y + 2, w - 4, h - 4, LA_ROW_R - 2, c, c);
		} else {
			c = M32(tj, TJ_RAISED);
			box = 1;
		}
	} else if (la_tab_hover == t) {
		c = (u32)ui_mix(M32(tj, TJ_SUNKEN), tok(TJ_LINE), 128);
		box = 1;
	}
	if (box)
		ui_rbox(x, y, w, h, LA_ROW_R, c, c);
	const char *name = la_cat_name(la_tab[t]);
	u32 tc = M32(tj, TJ_TEXT2);
	long font = (long)ui.font;
	if (la_cur == t) {
		tc = M32(tj, TJ_TEXT1);
		font = (long)ui.font_b;
	}
	ui_text_fit((long)name, x + 12, y + (long)((u64)h >> 1) + 5, tc, LA_TAB_PX, font, w - 24);
	if (show)
		ui_update(x, y, w, h);
}

/* x, y = coin, w = largeur : une boîte de recherche (LA_BOX_H de haut ; le champ ui_textbox à
 * x + LA_BOX_TX, largeur - LA_BOX_TX - 2, découpé par la_clip_at) : contour, fond, loupe. Le
 * champ dessine son propre contour de rayon 6 : il est plus grand que ce qu'on en montre. */
static void la_box_at(long x, long y, long w)
{
	u8 *tj = (u8 *)ui_tokens();
	u32 c = M32(tj, TJ_LINE_STRONG);
	ui_rbox(x, y, w, LA_BOX_H, LA_BOX_R, c, c);
	c = M32(tj, TJ_SURFACE);
	ui_rbox(x + 1, y + 1, w - 2, LA_BOX_H - 2, LA_BOX_R - 1, c, c);
	c = M32(tj, TJ_TEXT3); /* loupe : anneau, manche */
	ui_rbox(x + 15, y + 12, 12, 12, 6, c, c);
	c = M32(tj, TJ_SURFACE);
	ui_rbox(x + 17, y + 14, 8, 8, 4, c, c);
	for (long i = 0; i < 4; i++)
		ui_fill(x + i + 25, y + i + 22, 2, 2, M32(tj, TJ_TEXT3));
}

/* ui_w_event, le champ découpé -> code UN_* */
static long la_w_event(void)
{
	la_clip_field();
	struct krt_r4 r = krt_call_r4(ui_w_event, 0, 0, 0, 0);
	la_clip_off();
	return r.r1;
}

/* tout le lanceur */
static void la_draw(void)
{
	ui_fill(0, 0, la_geom[1], la_geom[2], 0xFF000000); /* hors de la feuille : transparent */
	if (la_grid) { /* (la grille de la vue d'ensemble : sans feuille, sur le fond de la vue) */
		la_box_at(la_gx, la_by, la_gw);
		for (long t = 0; t < la_ntab; t++)
			la_tab_draw(t, 0);
		for (long i = 0; i < la_n; i++)
			la_cell_draw(i, 0);
		if (!la_n) {
			const char *s = lang_tr("Aucune application");
			long tw = ui_text_width((long)s, 14, (long)ui.font);
			ui_text((long)s, (la_geom[1] - tw) / 2, la_gy + 40, la_grid_ink(), 14, (long)ui.font, 0);
		}
		la_pages_draw();
		la_clip_field();
		ui_w_draw(0);
		la_clip_off();
		ui_update_all();
		return;
	}
	long x = la_cx, y = la_cy, w = la_cw, h = la_ch;
	u32 c = tok(TJ_LINE_STRONG); /* la feuille : contour, face */
	ui_rbox(x, y, w, h, LA_SHEET_R, c, c);
	c = tok(TJ_FACE);
	ui_rbox(x + 1, y + 1, w - 2, h - 2, LA_SHEET_R - 1, c, c);
	c = tok(TJ_SUNKEN); /* le rail */
	ui_rbox(x + LA_RAIL_M, y + LA_RAIL_M, LA_RAIL_W, h - 2 * LA_RAIL_M, LA_RAIL_R, c, c);
	la_box_at(la_gx, la_by, la_gw);
	for (long t = 0; t < la_ntab; t++) /* onglets */
		la_tab_draw(t, 0);
	for (long i = 0; i < la_n; i++) /* applications */
		la_cell_draw(i, 0);
	if (!la_n) { /* (rien ne correspond) */
		const char *s = lang_tr("Aucune application");
		long tw = ui_text_width((long)s, 14, (long)ui.font);
		ui_text((long)s, la_gx + (long)((u64)(la_gw - tw) >> 1), la_gy + 40, tok(TJ_TEXT1), 14,
			(long)ui.font, 0);
	} else {
		u64 total = (u64)la_total(); /* barre de défilement */
		u64 rows = (u64)la_rows;
		if (total > rows) {
			u64 bar = (u64)la_cellh * rows; /* hauteur de la barre */
			u64 th = bar * rows / total;    /* du curseur */
			if (th < 24)
				th = 24;
			u64 off = (bar - th) * (u64)la_scroll / (total - rows);
			c = tok(TJ_LINE_STRONG);
			ui_rbox(la_cx + la_cw - 12, la_gy + (long)off, 5, (long)th, 2, c, c);
		}
	}
	la_clip_field();
	ui_w_draw(0);
	la_clip_off();
	ui_update_all();
}

/* nouvelle case survolée */
static void la_hover_set(long i)
{
	long old = la_hover;
	if (old == i)
		return;
	la_hover = i;
	la_cell_draw(i, 1);
	la_cell_draw(old, 1);
}

/* nouvel onglet survolé */
static void la_tab_hover_set(long t)
{
	long old = la_tab_hover;
	if (old == t)
		return;
	la_tab_hover = t;
	if (t >= 0)
		la_tab_draw(t, 1);
	if (old >= 0)
		la_tab_draw(old, 1);
}

/* rail -> grille (sans application : la recherche) ; la première case montrée */
static void la_rail_to_grid(void)
{
	if (!la_n) {
		la_sel_default();
		la_zone_set(0);
		return;
	}
	la_zone_set(1);
	la_sel_set(la_scroll * la_cols);
}

/* vers le rail : la grille n'a plus de choix */
static void la_to_rail(void)
{
	la_sel_set(-1);
	la_zone_set(2);
}

/* onglet du rail (haut, bas) : choisi, la grille sans le clavier */
static void la_rail_set(long t)
{
	la_tab_set(t);
	la_sel_set(-1);
}

/* (une touche ou la souris, que le champ reçoit aussi) l'événement au champ ; un clic dans
 * le champ : la recherche ; un changement du texte : la liste refaite */
static void la_pass(void)
{
	long code = la_w_event();
	if (ui_w_get_focus() == la_w_box && la_zone) {
		la_sel_default();
		long old = la_zone; /* (le champ a déjà le focus) */
		la_zone = 0;
		if (old == 2)
			la_tab_draw(la_cur, 1);
	}
	if (code == UN_CHANGE) {
		la_filter();
		la_draw();
	}
}

/* une touche (enfoncée) -> 1 : fermer (*pick : l'application choisie, ou -1). Les flèches :
 * recherche (0), grille (1), rail (2). Bas : de la recherche à la grille ; haut depuis la
 * première rangée : la recherche ; gauche en début de recherche ou depuis la première
 * colonne : le rail ; droite (ou Entrée) depuis le rail : la grille. Dans le rail, haut et bas
 * changent d'onglet. */
static int la_key(long code, long *pick)
{
	switch (code) {
	case KEY_ESC:
		return 1;
	case KEY_ENTER:
		if (la_zone == 2) /* le rail : vers les icônes */
			la_rail_to_grid();
		else if (la_sel >= 0) {
			*pick = la_res[la_sel];
			return 1;
		}
		return 0;
	case KEY_TAB: { /* onglet suivant */
		long t = la_cur + 1;
		if ((u64)t >= (u64)la_ntab)
			t = 0;
		la_tab_set(t);
		return 0;
	}
	case KEY_DOWN:
		if (la_zone == 2) {
			if (la_cur + 1 < la_ntab)
				la_rail_set(la_cur + 1);
		} else if (la_zone) {
			la_sel_move(la_cols);
		} else if (la_n) { /* recherche -> grille */
			la_zone_set(1);
			if (la_sel < 0)
				la_sel_set(0);
		}
		return 0;
	case KEY_UP:
		if (la_zone == 2) {
			if (la_cur)
				la_rail_set(la_cur - 1);
		} else if (la_zone) {
			/* première rangée : la recherche (comparaison non signée, comme l'asm) */
			if ((u64)la_sel < (u64)la_cols) {
				la_sel_default();
				la_zone_set(0);
			} else {
				la_sel_move(-la_cols);
			}
		}
		return 0;
	case KEY_LEFT:
		if (la_zone == 2)
			return 0;
		if (la_zone) { /* première colonne : le rail */
			if ((u64)la_sel % (u64)la_cols == 0)
				la_to_rail();
			else
				la_sel_move(-1);
		} else if (ui_w_get_value(la_w_box)) { /* recherche : le curseur va à gauche */
			la_pass();
		} else { /* (au début) */
			la_to_rail();
		}
		return 0;
	case KEY_RIGHT:
		if (!la_zone)
			la_pass();
		else if (la_zone == 2)
			la_rail_to_grid();
		else
			la_sel_move(1);
		return 0;
	case KEY_PGDN:
		la_scroll_to(la_scroll + la_rows);
		return 0;
	case KEY_PGUP:
		la_scroll_to(la_scroll - la_rows);
		return 0;
	}
	if (la_zone) { /* une autre touche hors de la recherche : elle y revient (on tape) */
		la_sel_default();
		la_zone_set(0);
	}
	la_pass();
	return 0;
}

/* la souris -> 1 : fermer (*pick : l'application choisie, ou -1) */
static int la_mouse(long *pick)
{
	long x = *(i32 *)(ui.ev + EV_X), y = *(i32 *)(ui.ev + EV_Y);
	u32 kind = *(u32 *)(ui.ev + EV_KIND);
	if (kind == 1) {
		u64 dx = (u64)(x - la_cx), dy = (u64)(y - la_cy);
		if (dx >= (u64)la_cw || dy >= (u64)la_ch) /* hors de la feuille : ferme */
			return 1;
		if (la_grid) { /* une flèche, un point : la page */
			long pg = la_page_at(x, y);
			if (pg == -2 || pg == -3) {
				la_scroll_to(la_scroll + (pg == -2 ? -la_rows : la_rows));
				return 0;
			}
			if (pg >= 0) {
				la_scroll_to(pg * la_rows);
				return 0;
			}
		}
		if (la_grid) { /* ni la recherche, ni une pilule, ni une icône : retour aux bureaux */
			long in_box = x >= la_gx && x < la_gx + la_gw && y >= la_by && y < la_by + LA_BOX_H;
			if (!in_box && la_tab_at(x, y) < 0 && la_cell_at(x, y) < 0)
				return 1;
		}
		if (!la_grid && dx >= (u64)(la_cw - LA_PAD) && y >= la_gy) { /* barre de défilement */
			la_bar_drag = 1;
			la_bar_set(y);
		} else {
			long t = la_tab_at(x, y);
			if (t >= 0)
				la_tab_set(t);
			else
				la_press = la_cell_at(x, y);
		}
	} else if (kind == 2) { /* relâché */
		la_bar_drag = 0;
		long p = la_press;
		la_press = -1;
		/* (p < 0 : le relâché du clic qui a ouvert) */
		if (p >= 0 && la_cell_at(x, y) == p) {
			*pick = la_res[p];
			return 1;
		}
	} else if (la_bar_drag) { /* déplacement */
		la_bar_set(y);
	} else {
		la_hover_set(la_cell_at(x, y));
		la_tab_hover_set(la_tab_at(x, y));
	}
	la_pass();
	return 0;
}

void launcher(void)
{
	la_cats();
	la_metrics();
	la_prev = ui.win; /* (la fenêtre d'avant, rendue à la fin) */
	la_win = ui_window_ex(0, la_geom[0], la_geom[1], la_geom[2], (long)lang_tr("Applications"),
			      la_grid ? LA_GRID_WFLAGS : LA_WFLAGS);
	if (la_win < 0)
		return;
	/* la recherche, le clavier dessus */
	la_w_box = ui_textbox(la_gx + LA_BOX_TX, la_by, la_gw - (LA_BOX_TX + 2), LA_BOX_H, 0);
	ui_w_focus(la_w_box);
	ui_textbox_hint(la_w_box, (long)lang_tr("Rechercher une application"));
	la_hover = -1;
	la_tab_hover = -1;
	la_press = -1;
	la_bar_drag = 0;
	la_zone = 0;
	la_tab_set(la_fixed - 1); /* « Toutes » (après « Récentes ») */
	long pick = -1; /* l'application choisie */
	for (;;) {
		long t = ui_event(1000);
		if (!t)
			continue;
		if (t == WEV_SCREEN) { /* nouvelle résolution : le panel la suivra */
			screen_todo = 1;
			break;
		}
		if ((long)*(u32 *)(ui.ev + EV_WIN) != la_win) {
			if (!la_grid)
				continue; /* (le panel attend) */
			/* (la grille de la vue d'ensemble n'a pas la souris pour elle seule : le dash répond ;
			   la vue fermée par Elegant, la grille aussi) */
			ui_window_use(la_prev);
			long close = la_other();
			ui_window_use(la_win);
			if (close)
				break;
			continue;
		}
		if (la_grid && t == WEV_FOCUS && !*(u32 *)(ui.ev + EV_KIND))
			break; /* (une application a pris le clavier) */
		if (t == WEV_MOUSE) {
			if (la_mouse(&pick))
				break;
		} else if (t == WEV_WHEEL) { /* (un cran vers le haut : positif) */
			long n = *(i32 *)(ui.ev + EV_Y);
			if (la_grid) { /* une page par geste (les crans rapprochés d'un pavé tactile n'en tournent qu'une) */
				u64 now = (u64)korin_sys0(SYS_TIME_MS);
				if (n && now - la_wheel_t > 350) {
					la_wheel_t = now;
					la_scroll_to(la_scroll + (n > 0 ? -la_rows : la_rows));
				} else if (n) {
					la_wheel_t = now;
				}
			} else {
				la_scroll_to(la_scroll - n);
			}
		} else if (t == WEV_KEY && *(u32 *)(ui.ev + EV_KIND)) {
			if (la_key(*(u32 *)(ui.ev + EV_CODE), &pick))
				break;
		} else { /* (une touche relâchée, le reste : au champ) */
			la_pass();
		}
	}
	ui_window_close(la_win);
	ui_window_use(la_prev);
	if (pick >= 0)
		app_open(pick);
}
