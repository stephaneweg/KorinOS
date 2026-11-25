/* overview.c : la vue d'ensemble du bureau (mode desktop ; docs/DESIGN-ELEGANT.md 19), comme
 * l'aperçu des activités de GNOME. Deux vues :
 *   * la vue normale : la barre de menus et les fenêtres des applications ;
 *   * la vue d'ensemble : sur le fond d'écran flouté et assombri, les bureaux en carrousel
 *     horizontal (environ 80 % de l'écran chacun, une marge autour, les voisins à moitié
 *     visibles), leurs fenêtres en exposé (côte à côte, sans se couvrir), et en bas le dash
 *     (les fenêtres WF_OVERVIEW : le panel).
 * La molette fait défiler le carrousel d'un bureau à l'autre (animé) ; un clic sur le bureau du
 * milieu l'active (retour à la vue normale), sur un voisin le centre ; un clic sur une fenêtre
 * active son bureau et la ramène ; une fenêtre glissée sur un autre bureau y passe ; un clic sur
 * le fond, Échap, Entrée, le bouton « Korin OS » de la barre de menus (WS_OP_OVERVIEW) ou Super
 * reviennent à la vue normale sur le bureau du milieu. Une fenêtre normale créée, ou ramenée par
 * WS_OP_RAISE (le dash), referme la vue.
 *
 * Rendu (processeur) : le coût est payé une fois, à l'ouverture, pas à chaque image.
 *   * le fond (les fenêtres de la couche du fond : voronoy, les applets) est composé une fois en
 *     pleine taille (bg_full), d'où sont tirés le fond de la vue (bg_dim : réduit, flouté,
 *     assombri, agrandi) et la vignette d'un bureau (bg_thumb, réduite par moyenne de blocs) ;
 *     bg_full et bg_dim sont gardés d'une ouverture à l'autre tant que le fond ne change pas ;
 *   * chaque fenêtre est composée hors écran par le chemin ordinaire (draw_framed : contenu,
 *     surface attachée, widgets par-dessus, boutons du cadre ; le tampon arrière redirigé, comme
 *     surf_read_over), puis réduite par moyenne de blocs à la taille de l'exposé, coins arrondis
 *     lissés : sa vignette. Elle est refaite quand le contenu change (génération), au plus
 *     toutes les OV_REFRESH_MS ;
 *   * à l'arrêt et pendant le défilement du carrousel, une image n'est que des copies de ces
 *     vignettes ; pendant les animations d'ouverture et de fermeture (zoom), les bureaux et les
 *     fenêtres sont mis à l'échelle au plus proche depuis les images pleine taille (gardées pour
 *     le bureau animé seulement).
 * Le GPU n'est pas nécessaire à ce régime (des copies) ; la mise à l'échelle des animations est
 * la place d'un chemin GpuKit (textures et quads) à venir, sur le Pi. */
#include "elegant.h"

#define OV_ONE 65536
#define OV_OPEN_MS 280
#define OV_CLOSE_MS 240
#define OV_SCROLL_MS 260
#define OV_REFRESH_MS 120     /* une vignette refaite au plus tous les tant de ms */
#define OV_R_DESK 16          /* rayon des coins d'un bureau (à sa taille dans la vue) */
#define OV_SH_DESK 14         /* son ombre */
#define OV_SH_WIN 10          /* l'ombre d'une fenêtre */
#define OV_DIM 60             /* le fond de la vue : luminosité sur 256 (plus sombre : demande du 23/11) */
#define OV_SNAP_BUDGET (128L << 20) /* images pleine taille gardées (octets) */
#define OV_DRAG_PX 6          /* au-delà, un appui sur une fenêtre devient un glisser */
#define OV_COL_EMPTY 0x001E1F22 /* (sans mémoire pour le fond) */
#define OV_MAXW 4096

long ov_on; /* non nul : la vue d'ensemble a l'écran (ouverture, ouverte, fermeture) */
long ov_enabled; /* la vue existe (défaut) ; desktop=classic (system.ini) : le bureau classique */

static long ov_state; /* 1 ouverture, 2 ouverte, 3 fermeture */
static long ov_p = OV_ONE;     /* avancement du zoom (0 : vue normale, OV_ONE : vue d'ensemble) */
static long ov_pa, ov_pb;      /* d'où, vers où */
static u64 ov_t0;
static long ov_dur;
static long ov_sel;            /* le bureau du milieu (choisi) */
static long ov_pos;            /* position du carrousel (bureau, 16.16) */
static long ov_pos_a, ov_pos_b;
static u64 ov_st0;
static long ov_hover = -1, ov_hover_desk = -1;
static long ov_press_item = -1, ov_press_desk = -1, ov_pressed;
static long ov_press_x, ov_press_y;
static long ov_drag = -1;      /* fenêtre glissée (rang dans items) */
static long ov_drag_dx, ov_drag_dy, ov_drag_x0, ov_drag_y0;
static u64 ov_sig;             /* empreinte des fenêtres du carrousel (disposition à refaire) */
static long ov_geom_tw;
/* la grille des applications (une fenêtre WF_APPGRID du panel) : 0 le carrousel, OV_ONE les bureaux en
 * rangée en haut de l'écran et la grille montée du bas */
static long ov_q, ov_qa, ov_qb;
static u64 ov_qt0;
#define OV_APPS_MS 300

/* une fenêtre du carrousel : son bureau, sa place dans l'exposé (au coin du bureau, à la taille
 * de la vue) */
struct ov_item {
	struct win *w;
	long desk;
	long rx, ry, rw, rh;
};
static struct ov_item items[MAX_WINDOWS];
static long n_items;

/* par place de fenêtre : l'image pleine taille (snap, facultative) et la vignette */
struct ov_cache {
	u64 id;
	u32 *snap;
	long sw, sh;
	u32 *thumb;
	long tw, th;
	u32 gen, cgen;
	long act;
	u64 t;
};
static struct ov_cache cache[MAX_WINDOWS];
static long snap_bytes;
static u32 *scratch;
static long scratch_px;

static u32 *bg_full, *bg_dim, *bg_thumb;
static long bg_w, bg_h, bgt_w, bgt_h;
static u64 bg_sig;
static long bg_ok; /* bg_full et bg_dim à jour (pour bg_sig) */

struct box {
	long x, y, w, h;
};

/* la géométrie de la vue (refaite à chaque image : la place du dash peut changer) */
static struct {
	long W, H;
	long tw, th; /* un bureau, à l'arrêt */
	long cx, cy; /* centre du bureau du milieu */
	long gap;    /* entre deux bureaux */
	long s;      /* échelle d'un bureau (16.16) */
	long dash_y; /* haut du dash */
} g;

static const char ov_topic[] = "korin.shell.overview";

/* ---------------------------------------------------------------------------
 *  Petits outils
 * --------------------------------------------------------------------------- */

static inline u32 ov_blend(u32 s, u32 d, u32 t) /* (blend de core.c) */
{
	u32 u = 255 - t;
	u32 lo = (s & 0x00FF00FF) * u + (d & 0x00FF00FF) * t;
	u32 hi = ((s >> 8) & 0x00FF00FF) * u + ((d >> 8) & 0x00FF00FF) * t;
	lo = ((lo + ((lo >> 8) & 0x00FF00FF) + 0x00010001) >> 8) & 0x00FF00FF;
	hi = ((hi + ((hi >> 8) & 0x00FF00FF) + 0x00010001) >> 8) & 0x00FF00FF;
	return (lo | hi << 8) & 0x00FFFFFF;
}

static u32 isqrt(u64 v)
{
	u64 r = 0, b = 1ULL << 62;
	while (b > v)
		b >>= 2;
	while (b) {
		if (v >= r + b) {
			v -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return (u32)r;
}

static long lerp(long a, long b, long p)
{
	return a + (b - a) * p / OV_ONE;
}

static u64 now_ms(void)
{
	return (u64)korin_sys0(SYS_TIME_MS);
}

/* t 0..OV_ONE -> 1 - (1 - t)^3 */
static long ease(long t)
{
	if (t >= OV_ONE)
		return OV_ONE;
	long u = OV_ONE - t;
	return OV_ONE - (u * u / OV_ONE) * u / OV_ONE;
}

static long box_hit(const struct box *b, long x, long y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

/* le rectangle touche-t-il le clip (marge comprise) ? */
static long box_in_clip(const struct box *b, long m)
{
	return b->x - m < clip_r.x1 && b->x + b->w + m > clip_r.x0 && b->y - m < clip_r.y1 &&
	       b->y + b->h + m > clip_r.y0;
}

static void dirty_box(const struct box *b, long m)
{
	dirty(b->x - m, b->y - m, b->w + 2 * m, b->h + 2 * m);
}

static void *ov_alloc(long bytes)
{
	return bytes > 0 ? malloc((size_t)bytes) : 0;
}

/* ---------------------------------------------------------------------------
 *  Images : rendu hors écran, réduction, coins, mise à l'échelle
 * --------------------------------------------------------------------------- */

/* fenêtre -> son contenu composé (cadre arrondi, surface, boutons) dans dst (pas : sa largeur) :
 * le tampon arrière redirigé vers dst, le clip = le contenu, puis le chemin ordinaire */
static void render_win(struct win *w, u32 *dst)
{
	u32 *keep_bb = bb;
	long keep_pitch = bb_pitch;
	struct rect keep_clip = clip_r;
	long cw = w->w, ch = w->h;
	for (long i = 0, n = cw * ch; i < n; i++)
		dst[i] = 0;
	bb_pitch = cw << 2;
	bb = (u32 *)((u64)dst - (u64)(w->y * bb_pitch) - ((u64)w->x << 2));
	clip_r.x0 = w->x;
	clip_r.y0 = w->y;
	clip_r.x1 = w->x + cw;
	clip_r.y1 = w->y + ch;
	if (w->flags & WF_FRAMELESS) {
		bb_blit_any(w->surf, cw * 4, w->x, w->y, cw, ch, (w->flags >> 8) & 1);
		draw_surface(w, w->x, w->y);
	} else {
		draw_framed(w);
	}
	bb = keep_bb;
	bb_pitch = keep_pitch;
	clip_r = keep_clip;
}

/* le fond de chaque bureau (la couche du fond : fond uni du mode, voronoy, applets) composé en
 * pleine taille dans dst (pas : la largeur de l'écran) */
static void render_back(u32 *dst)
{
	u32 *keep_bb = bb;
	long keep_pitch = bb_pitch;
	struct rect keep_clip = clip_r;
	bb = dst;
	bb_pitch = scr_w << 2;
	clip_r = (struct rect){ 0, 0, scr_w, scr_h };
	policy->back();
	for (long i = 0; i < win_count; i++) {
		struct win *w = win_z[i];
		if (w->mini || win_layer(w) != 0)
			continue;
		draw_window(w);
	}
	bb = keep_bb;
	bb_pitch = keep_pitch;
	clip_r = keep_clip;
}

/* réduction par moyenne de blocs, transparence comprise (octet haut : 0 opaque, 255 invisible ;
 * couleurs pondérées par l'opacité). dw <= sw, dh <= sh. */
static u64 acc_r[OV_MAXW], acc_g[OV_MAXW], acc_b[OV_MAXW], acc_o[OV_MAXW];
static u16 xmap[OV_MAXW];
static u32 xcnt[OV_MAXW];

static void downscale(const u32 *src, long sw, long sh, long sp, u32 *dst, long dw, long dh)
{
	if (sw > OV_MAXW || dw > OV_MAXW || dw < 1 || dh < 1)
		return;
	for (long c = 0; c < dw; c++)
		xcnt[c] = 0;
	for (long x = 0; x < sw; x++) {
		long c = x * dw / sw;
		xmap[x] = (u16)c;
		xcnt[c]++;
	}
	for (long ty = 0; ty < dh; ty++) {
		long sy0 = ty * sh / dh, sy1 = (ty + 1) * sh / dh;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (long c = 0; c < dw; c++)
			acc_r[c] = acc_g[c] = acc_b[c] = acc_o[c] = 0;
		for (long sy = sy0; sy < sy1; sy++) {
			const u32 *row = src + sy * sp;
			for (long x = 0; x < sw; x++) {
				u32 v = row[x];
				u32 o = 255 - (v >> 24);
				long c = xmap[x];
				acc_o[c] += o;
				acc_r[c] += ((v >> 16) & 0xFF) * o;
				acc_g[c] += ((v >> 8) & 0xFF) * o;
				acc_b[c] += (v & 0xFF) * o;
			}
		}
		u32 *out = dst + ty * dw;
		u64 n = (u64)(sy1 - sy0);
		for (long c = 0; c < dw; c++) {
			u64 o = acc_o[c];
			if (!o) {
				out[c] = TRANSPARENT;
				continue;
			}
			u64 tot = n * xcnt[c];
			u32 t = 255 - (u32)(o / tot);
			out[c] = t << 24 | (u32)(acc_r[c] / o) << 16 | (u32)(acc_g[c] / o) << 8 | (u32)(acc_b[c] / o);
		}
	}
}

/* coins arrondis lissés (couverture 4 x 4) : l'opacité des pixels des coins réduite */
static void round_mask(u32 *px, long w, long h, long r)
{
	if (2 * r > w)
		r = w / 2;
	if (2 * r > h)
		r = h / 2;
	for (long j = 0; j < r; j++) {
		for (long k = 0; k < r; k++) {
			long cov = 0;
			for (long sy = 0; sy < 4; sy++)
				for (long sx = 0; sx < 4; sx++) {
					long qx = 8 * k + 2 * sx + 1 - 8 * r, qy = 8 * j + 2 * sy + 1 - 8 * r;
					if (qx * qx + qy * qy <= 64 * r * r)
						cov++;
				}
			if (cov == 16)
				break;
			u32 *c4[4] = { px + j * w + k, px + j * w + (w - 1 - k), px + (h - 1 - j) * w + k,
				       px + (h - 1 - j) * w + (w - 1 - k) };
			for (int i = 0; i < 4; i++) {
				u32 v = *c4[i];
				u32 o = (255 - (v >> 24)) * (u32)cov / 16;
				*c4[i] = (v & 0xFFFFFF) | (255 - o) << 24;
			}
		}
	}
}

/* rangée j (0 : la plus au bord) d'un rectangle arrondi de rayon r -> pixels retirés */
static long corner_inset(long r, long j)
{
	if (r <= 0 || j >= r)
		return 0;
	long d = 2 * r - 2 * j - 1; /* (en demi-pixels) */
	return r - (long)isqrt((u64)(4 * r * r - d * d) / 4);
}

/* mise à l'échelle au plus proche, découpée par le clip ; coins arrondis de rayon r (sans
 * lissage : en mouvement) ; transparence de l'octet haut */
static void scale_blit(const u32 *src, long sw, long sh, long sp, long dx, long dy, long dw, long dh, long r)
{
	if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
		return;
	long x0 = dx > clip_r.x0 ? dx : clip_r.x0, x1 = dx + dw < clip_r.x1 ? dx + dw : clip_r.x1;
	long y0 = dy > clip_r.y0 ? dy : clip_r.y0, y1 = dy + dh < clip_r.y1 ? dy + dh : clip_r.y1;
	if (x0 >= x1 || y0 >= y1)
		return;
	if (2 * r > dw)
		r = dw / 2;
	if (2 * r > dh)
		r = dh / 2;
	u64 xs = ((u64)sw << 16) / (u64)dw;
	for (long y = y0; y < y1; y++) {
		long ry = y - dy;
		long sy = ry * sh / dh;
		long j = ry < dh - 1 - ry ? ry : dh - 1 - ry;
		long in = corner_inset(r, j);
		long lx0 = x0 > dx + in ? x0 : dx + in, lx1 = x1 < dx + dw - in ? x1 : dx + dw - in;
		if (lx0 >= lx1)
			continue;
		const u32 *s = src + sy * sp;
		u32 *d = (u32 *)((u8 *)bb + y * bb_pitch);
		u64 fx = (u64)(lx0 - dx) * xs + (xs >> 1);
		for (long x = lx0; x < lx1; x++, fx += xs) {
			u32 v = s[fx >> 16], t = v >> 24;
			if (!t)
				d[x] = v;
			else if (t != 255)
				d[x] = ov_blend(v, d[x], t);
		}
	}
}

/* autour d'un rectangle arrondi (rayon r), à la distance d (pixels) de son bord, pour d0 <= d <
 * d1 : la couleur avec l'opacité a (ombre : décroissante jusqu'à d1, plate : constante) */
static void halo(const struct box *b, long r, long d0, long d1, u32 color, long a, long shadow, long oy)
{
	long X0 = b->x - d1, Y0 = b->y + oy - d1, X1 = b->x + b->w + d1, Y1 = b->y + oy + b->h + d1;
	if (X0 < clip_r.x0)
		X0 = clip_r.x0;
	if (Y0 < clip_r.y0)
		Y0 = clip_r.y0;
	if (X1 > clip_r.x1)
		X1 = clip_r.x1;
	if (Y1 > clip_r.y1)
		Y1 = clip_r.y1;
	if (X0 >= X1 || Y0 >= Y1)
		return;
	long cx0 = b->x + r, cx1 = b->x + b->w - 1 - r;
	long cy0 = b->y + oy + r, cy1 = b->y + oy + b->h - 1 - r;
	long ix0 = b->x + r, ix1 = b->x + b->w - r; /* l'intérieur des rangées du milieu : sauté */
	for (long y = Y0; y < Y1; y++) {
		long qy = y < cy0 ? cy0 - y : y > cy1 ? y - cy1 : 0;
		u32 *row = (u32 *)((u8 *)bb + y * bb_pitch);
		for (long x = X0; x < X1; x++) {
			if (!qy && y >= b->y + oy && y < b->y + oy + b->h && x >= ix0 && x < ix1) {
				x = ix1 - 1;
				continue;
			}
			long qx = x < cx0 ? cx0 - x : x > cx1 ? x - cx1 : 0;
			long d = (long)isqrt((u64)(qx * qx + qy * qy)) - r;
			if (d < d0 || d >= d1)
				continue;
			long o = a;
			if (shadow) {
				long e = d1 - d;
				o = a * e * e / (d1 * d1);
			}
			if (o > 0)
				row[x] = ov_blend(color, row[x], 255 - (u32)o);
		}
	}
}

/* ---------------------------------------------------------------------------
 *  Géométrie et disposition
 * --------------------------------------------------------------------------- */

/* est-elle une fenêtre du carrousel ? (normale, ou « toujours au-dessus » à cadre) */
static long ov_member(const struct win *w)
{
	if (!w->used || (w->flags & (WF_BACK | WF_HOME | WF_OVERVIEW)))
		return 0;
	long l = win_layer(w);
	if (l == 1)
		return 1;
	return l == 2 && !(w->flags & WF_FRAMELESS);
}

/* -> 1 si cette fenêtre se montre et se touche normalement pendant la vue : les barres, le dash,
 * les menus, les volets (sans cadre, au-dessus, pas en plein écran) */
long ov_top(const struct win *w)
{
	if (w->secure)
		return 1; /* (la carte d'élévation : toujours montrée et touchée) */
	if (!(w->flags & WF_FRAMELESS))
		return 0;
	long l = win_layer(w);
	return l == 2 || l == 4;
}

static long win_desk(const struct win *w)
{
	return w->desk ? (long)w->desk - 1 : desk_cur;
}

static void ov_geom(void)
{
	long W = scr_w, H = scr_h;
	g.W = W;
	g.H = H;
	long dy = H - H / 11; /* le haut du dash (sans dash : une place pour rien) */
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (w->used && (w->flags & WF_OVERVIEW) && w->y > H / 2 && w->y < dy)
			dy = w->y;
	}
	g.dash_y = dy;
	long top = area_top;
	long m = H * 3 / 100 + 6; /* la marge autour du bureau */
	long ah = dy - top - 2 * m, aw = W * 82 / 100;
	if (ah < H / 4)
		ah = H / 4;
	long s1 = (aw << 16) / W, s2 = (ah << 16) / H;
	g.s = s1 < s2 ? s1 : s2;
	g.tw = W * g.s >> 16;
	g.th = H * g.s >> 16;
	g.cx = W / 2;
	g.cy = top + m + ah / 2;
	g.gap = W * 4 / 100 + 8;
}

/* le rectangle du bureau d, à l'avancement ov_p et à la position ov_pos */
static void desk_rect(long d, struct box *r)
{
	long p = ov_p;
	long bw = lerp(g.W, g.tw, p), bh = lerp(g.H, g.th, p);
	long bx = lerp(0, g.cx - g.tw / 2, p), by = lerp(0, g.cy - g.th / 2, p);
	long gp = lerp(g.W / 20, g.gap, p);
	long off = ((d << 16) - ov_pos) * (bw + gp) / OV_ONE;
	r->x = bx + off;
	r->y = by;
	r->w = bw;
	r->h = bh;
	if (!ov_q)
		return;
	/* la grille des applications : tous les bureaux en rangée, petits, en haut */
	long sh = g.H * 13 / 100, sw = g.W * sh / g.H, sg = 16;
	long n = desk_count, total = n * sw + (n - 1) * sg;
	if (total > g.W - 40) { /* (beaucoup de bureaux : plus petits) */
		sw = (g.W - 40 - (n - 1) * sg) / n;
		sh = g.H * sw / g.W;
		total = n * sw + (n - 1) * sg;
	}
	long sx = (g.W - total) / 2 + d * (sw + sg), sy = area_top + g.H * 2 / 100 + 6;
	r->x = lerp(r->x, sx, ov_q);
	r->y = lerp(r->y, sy, ov_q);
	r->w = lerp(r->w, sw, ov_q);
	r->h = lerp(r->h, sh, ov_q);
}

/* le rectangle d'une fenêtre, dans le rectangle D de son bureau : de sa place réelle (p = 0) à
 * sa place dans l'exposé (p = 1) */
static void item_rect(const struct ov_item *it, const struct box *D, struct box *o)
{
	struct win *w = it->w;
	long tw = g.tw, th = g.th;
	long hx = w->x * tw / g.W, hy = w->y * th / g.H, hw = w->w * tw / g.W, hh = w->h * th / g.H;
	if (w->mini & MINI_REDUCED) { /* réduite : elle vient du bas de l'écran (du dash), toute petite */
		hw = hh = 8;
		hx = tw / 2 - 4;
		hy = th - 8;
	}
	long p = ov_p;
	long rx = lerp(hx, it->rx, p), ry = lerp(hy, it->ry, p);
	long rw = lerp(hw, it->rw, p), rh = lerp(hh, it->rh, p);
	o->x = D->x + rx * D->w / tw;
	o->y = D->y + ry * D->h / th;
	o->w = rw * D->w / tw;
	o->h = rh * D->h / th;
}

/* l'exposé d'un bureau : les fenêtres en rangées (triées par leur centre, de haut en bas puis de
 * gauche à droite), à la plus grande échelle qui tient, sans dépasser celle du bureau */
static void layout_desk(long d)
{
	long idx[MAX_WINDOWS], n = 0;
	for (long i = 0; i < n_items; i++)
		if (items[i].desk == d)
			idx[n++] = i;
	if (!n)
		return;
	for (long i = 1; i < n; i++) { /* par centre vertical */
		long v = idx[i], k = i;
		struct win *a = items[v].w;
		while (k > 0) {
			struct win *b = items[idx[k - 1]].w;
			if (b->y + b->h / 2 <= a->y + a->h / 2)
				break;
			idx[k] = idx[k - 1];
			k--;
		}
		idx[k] = v;
	}
	long pad = g.tw * 5 / 100 + 4, G = g.tw / 40 + 6;
	long Aw = g.tw - 2 * pad, Ah = g.th - 2 * pad;
	long best_k = 0, best_r = 1;
	for (long r = 1; r <= n; r++) {
		long sum_h = 0, k = g.s;
		for (long row = 0; row < r; row++) {
			long a = row * n / r, b = (row + 1) * n / r;
			long sw = 0, mh = 0;
			for (long i = a; i < b; i++) {
				struct win *w = items[idx[i]].w;
				sw += w->w;
				if (w->h > mh)
					mh = w->h;
			}
			sum_h += mh;
			long kr = ((Aw - (b - a - 1) * G) << 16) / (sw ? sw : 1);
			if (kr < k)
				k = kr;
		}
		long kv = ((Ah - (r - 1) * G) << 16) / (sum_h ? sum_h : 1);
		if (kv < k)
			k = kv;
		if (k > best_k) {
			best_k = k;
			best_r = r;
		}
	}
	long k = best_k > 256 ? best_k : 256;
	long r = best_r;
	long total_h = (r - 1) * G;
	for (long row = 0; row < r; row++) {
		long a = row * n / r, b = (row + 1) * n / r, mh = 0;
		for (long i = a; i < b; i++)
			if (items[idx[i]].w->h > mh)
				mh = items[idx[i]].w->h;
		total_h += mh * k >> 16;
	}
	long y = pad + (Ah - total_h) / 2;
	for (long row = 0; row < r; row++) {
		long a = row * n / r, b = (row + 1) * n / r;
		for (long i = a + 1; i < b; i++) { /* la rangée, par centre horizontal */
			long v = idx[i], q = i;
			struct win *wa = items[v].w;
			while (q > a) {
				struct win *wb = items[idx[q - 1]].w;
				if (wb->x + wb->w / 2 <= wa->x + wa->w / 2)
					break;
				idx[q] = idx[q - 1];
				q--;
			}
			idx[q] = v;
		}
		long rw = (b - a - 1) * G, mh = 0;
		for (long i = a; i < b; i++) {
			struct win *w = items[idx[i]].w;
			rw += w->w * k >> 16;
			if (w->h > mh)
				mh = w->h;
		}
		long rh = mh * k >> 16;
		long x = pad + (Aw - rw) / 2;
		for (long i = a; i < b; i++) {
			struct ov_item *it = &items[idx[i]];
			long ww = it->w->w * k >> 16, wh = it->w->h * k >> 16;
			if (ww < 8)
				ww = 8;
			if (wh < 8)
				wh = 8;
			it->rx = x;
			it->ry = y + (rh - wh) / 2;
			it->rw = ww;
			it->rh = wh;
			x += ww + G;
		}
		y += rh + G;
	}
}

/* -> empreinte des fenêtres du carrousel (place, taille, bureau) et de la géométrie */
static u64 member_sig(void)
{
	u64 sig = (u64)g.tw << 32 ^ (u64)g.th ^ (u64)desk_count << 56;
	for (long i = 0; i < win_count; i++) {
		struct win *w = win_z[i];
		if (!ov_member(w))
			continue;
		sig = sig * 1099511628211ULL ^ w->id;
		sig = sig * 1099511628211ULL ^ ((u64)w->w << 32 | (u64)w->h);
		sig = sig * 1099511628211ULL ^ ((u64)(w->x & 0xFFFF) << 16 | (u64)(w->y & 0xFFFF));
		sig = sig * 1099511628211ULL ^ (u64)win_desk(w);
	}
	return sig;
}

/* la liste du carrousel (dans l'ordre d'empilement : la plus en avant à la fin), l'exposé de
 * chaque bureau */
static void layout_all(void)
{
	n_items = 0;
	for (long i = 0; i < win_count; i++) {
		struct win *w = win_z[i];
		if (!ov_member(w))
			continue;
		struct ov_item *it = &items[n_items++];
		it->w = w;
		it->desk = win_desk(w);
		if (it->desk >= desk_count)
			it->desk = desk_count - 1;
	}
	for (long d = 0; d < desk_count; d++)
		layout_desk(d);
	ov_sig = member_sig();
	ov_geom_tw = g.tw;
}

/* ---------------------------------------------------------------------------
 *  Caches
 * --------------------------------------------------------------------------- */

static struct ov_cache *cache_of(struct win *w)
{
	struct ov_cache *c = &cache[w - windows];
	if (c->id != w->id) { /* (une autre fenêtre a repris la place) */
		if (c->snap) {
			free(c->snap);
			snap_bytes -= c->sw * c->sh * 4;
		}
		if (c->thumb)
			free(c->thumb);
		*c = (struct ov_cache){ 0 };
		c->id = w->id;
	}
	return c;
}

static void cache_free(struct ov_cache *c)
{
	if (c->snap) {
		free(c->snap);
		snap_bytes -= c->sw * c->sh * 4;
	}
	if (c->thumb)
		free(c->thumb);
	*c = (struct ov_cache){ 0 };
}

/* fenêtre, garder l'image pleine taille ? -> son image composée (la sienne, ou le brouillon) */
static u32 *snap_of(struct win *w, long keep)
{
	struct ov_cache *c = cache_of(w);
	long px = w->w * w->h;
	if (c->snap && (c->sw != w->w || c->sh != w->h)) {
		free(c->snap);
		snap_bytes -= c->sw * c->sh * 4;
		c->snap = 0;
	}
	if (!c->snap && keep && snap_bytes + px * 4 <= OV_SNAP_BUDGET) {
		c->snap = ov_alloc(px * 4);
		if (c->snap) {
			c->sw = w->w;
			c->sh = w->h;
			snap_bytes += px * 4;
		}
	}
	u32 *dst = c->snap;
	if (!dst) {
		if (scratch_px < px) {
			if (scratch)
				free(scratch);
			scratch = ov_alloc(px * 4);
			scratch_px = scratch ? px : 0;
		}
		dst = scratch;
	}
	if (dst) {
		render_win(w, dst);
		round_mask(dst, w->w, w->h, FRAME_RADIUS);
	}
	return dst;
}

/* la vignette d'une fenêtre, à la taille de son exposé (refaite si force, ou si la taille a
 * changé) */
static void thumb_build(struct ov_item *it, long force, long keep)
{
	struct win *w = it->w;
	struct ov_cache *c = cache_of(w);
	long tw = it->rw, th = it->rh;
	if (tw > w->w)
		tw = w->w;
	if (th > w->h)
		th = w->h;
	if (!force && c->thumb && c->tw == tw && c->th == th)
		return;
	u32 *src = snap_of(w, keep);
	if (!src)
		return;
	if (!c->thumb || c->tw != tw || c->th != th) {
		if (c->thumb)
			free(c->thumb);
		c->thumb = ov_alloc(tw * th * 4);
		c->tw = c->thumb ? tw : 0;
		c->th = c->thumb ? th : 0;
		if (!c->thumb)
			return;
	}
	downscale(src, w->w, w->h, w->w, c->thumb, tw, th);
	long r = FRAME_RADIUS * tw / w->w + 1;
	round_mask(c->thumb, tw, th, r < 3 ? 3 : r);
	c->gen = w->gen;
	c->cgen = w->cgen;
	c->act = win_focus == w;
	c->t = now_ms();
}

/* l'image pleine taille des fenêtres du bureau d (pour l'animer) */
static void snaps_for(long d)
{
	for (long i = 0; i < n_items; i++) {
		struct ov_item *it = &items[i];
		if (it->desk != d)
			continue;
		struct ov_cache *c = cache_of(it->w);
		if (!c->snap || c->gen != it->w->gen || c->cgen != it->w->cgen)
			thumb_build(it, 1, 1);
	}
}

static void thumbs_all(void)
{
	for (long i = 0; i < n_items; i++)
		thumb_build(&items[i], 0, items[i].desk == ov_sel);
}

/* -> empreinte du fond (ses fenêtres, leur contenu, la taille de l'écran) */
static u64 back_sig(void)
{
	u64 s = (u64)scr_w << 32 | (u64)scr_h;
	for (long i = 0; i < win_count; i++) {
		struct win *w = win_z[i];
		if (w->mini || win_layer(w) != 0)
			continue;
		s = s * 1099511628211ULL ^ (w->id << 40 ^ (u64)w->gen << 20 ^ w->cgen);
		s = s * 1099511628211ULL ^ ((u64)(w->x & 0xFFFF) << 16 | (u64)(w->y & 0xFFFF));
	}
	return s;
}

/* flou de boîte (rayon 2) sur une petite image, deux passes */
static void small_blur(u32 *px, long w, long h)
{
	static u32 line[OV_MAXW];
	for (int pass = 0; pass < 2; pass++) {
		for (int dir = 0; dir < 2; dir++) {
			long n = dir ? h : w, m = dir ? w : h, step = dir ? w : 1, lstep = dir ? 1 : w;
			for (long l = 0; l < m; l++) {
				u32 *p = px + l * lstep;
				for (long i = 0; i < n; i++)
					line[i] = p[i * step];
				for (long i = 0; i < n; i++) {
					u32 r = 0, gg = 0, b = 0;
					for (long k = -2; k <= 2; k++) {
						long q = i + k < 0 ? 0 : i + k >= n ? n - 1 : i + k;
						u32 v = line[q];
						r += (v >> 16) & 0xFF;
						gg += (v >> 8) & 0xFF;
						b += v & 0xFF;
					}
					p[i * step] = (r / 5) << 16 | (gg / 5) << 8 | (b / 5);
				}
			}
		}
	}
}

/* le fond : pleine taille, et le fond de la vue (réduit au seizième, flouté, assombri, agrandi
 * en bilinéaire) ; gardés tant que le fond ne change pas */
static void bg_build(void)
{
	long W = scr_w, H = scr_h;
	u64 sig = back_sig();
	if (bg_ok && sig == bg_sig && bg_w == W && bg_h == H)
		return;
	if (bg_w != W || bg_h != H) {
		if (bg_full)
			free(bg_full);
		if (bg_dim)
			free(bg_dim);
		bg_full = ov_alloc(W * H * 4);
		bg_dim = ov_alloc(W * H * 4);
		bg_w = W;
		bg_h = H;
	}
	bg_ok = 0;
	if (!bg_full)
		return;
	render_back(bg_full);
	bg_sig = sig;
	if (!bg_dim)
		return;
	long sw = W / 16 + 1, sh = H / 16 + 1;
	u32 *small = ov_alloc(sw * sh * 4);
	if (!small)
		return;
	downscale(bg_full, W, H, W, small, sw, sh);
	small_blur(small, sw, sh);
	for (long i = 0; i < sw * sh; i++) { /* assombri */
		u32 v = small[i];
		small[i] = (((v >> 16) & 0xFF) * OV_DIM >> 8) << 16 | (((v >> 8) & 0xFF) * OV_DIM >> 8) << 8 |
			   ((v & 0xFF) * OV_DIM >> 8);
	}
	for (long y = 0; y < H; y++) { /* agrandi (bilinéaire, 8 bits de fraction) */
		long fy = (y * (sh - 1) << 8) / (H > 1 ? H - 1 : 1);
		long y0 = fy >> 8, wy = fy & 0xFF;
		long y1 = y0 + 1 < sh ? y0 + 1 : y0;
		const u32 *r0 = small + y0 * sw, *r1 = small + y1 * sw;
		u32 *out = bg_dim + y * W;
		for (long x = 0; x < W; x++) {
			long fx = (x * (sw - 1) << 8) / (W > 1 ? W - 1 : 1);
			long x0 = fx >> 8, wx = fx & 0xFF;
			long x1 = x0 + 1 < sw ? x0 + 1 : x0;
			u32 a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1], o = 0;
			for (int sh8 = 0; sh8 < 24; sh8 += 8) {
				long ta = (a >> sh8) & 0xFF, tb = (b >> sh8) & 0xFF;
				long tc = (c >> sh8) & 0xFF, td = (d >> sh8) & 0xFF;
				long top = ta * (256 - wx) + tb * wx, bot = tc * (256 - wx) + td * wx;
				o |= (u32)(((top * (256 - wy) + bot * wy) >> 16) & 0xFF) << sh8;
			}
			out[x] = o;
		}
	}
	free(small);
	bg_ok = 1;
}

/* la vignette du fond d'un bureau, à la taille de la vue */
static void bg_thumb_build(void)
{
	if (!bg_full)
		return;
	if (bg_thumb && bgt_w == g.tw && bgt_h == g.th)
		return;
	if (bg_thumb)
		free(bg_thumb);
	bg_thumb = ov_alloc(g.tw * g.th * 4);
	bgt_w = bg_thumb ? g.tw : 0;
	bgt_h = bg_thumb ? g.th : 0;
	if (!bg_thumb)
		return;
	downscale(bg_full, g.W, g.H, g.W, bg_thumb, g.tw, g.th);
	round_mask(bg_thumb, g.tw, g.th, OV_R_DESK);
}

/* tout ce qui n'est gardé que pendant la vue */
static void caches_free(void)
{
	for (long i = 0; i < MAX_WINDOWS; i++)
		cache_free(&cache[i]);
	snap_bytes = 0;
	if (scratch)
		free(scratch);
	scratch = 0;
	scratch_px = 0;
	if (bg_thumb)
		free(bg_thumb);
	bg_thumb = 0;
	bgt_w = bgt_h = 0;
}

/* ---------------------------------------------------------------------------
 *  Composition
 * --------------------------------------------------------------------------- */

/* -> la grille des applications montrée (0 : aucune) */
static struct win *grid_win(void)
{
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (w->used && !w->mini && (w->flags & WF_APPGRID))
			return w;
	}
	return 0;
}

/* (fenêtres WF_OVERVIEW, WF_APPGRID) cachées hors de la vue */
static void dash_show(long on)
{
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || !(w->flags & (WF_OVERVIEW | WF_APPGRID)))
			continue;
		if (on)
			w->mini &= ~(u64)MINI_OVERVIEW;
		else
			w->mini |= MINI_OVERVIEW;
	}
}

static void draw_item(const struct ov_item *it, const struct box *B, long steady)
{
	struct win *w = it->w;
	struct ov_cache *c = &cache[w - windows];
	if (c->id != w->id)
		c = 0;
	if (!box_in_clip(B, OV_SH_WIN + 6))
		return;
	halo(B, 8, 0, OV_SH_WIN, 0, 80, 1, 3); /* l'ombre, un peu plus bas */
	if (c && steady && c->thumb && c->tw == B->w && c->th == B->h)
		bb_blit_any(c->thumb, c->tw * 4, B->x, B->y, B->w, B->h, 1);
	else if (c && c->snap)
		scale_blit(c->snap, c->sw, c->sh, c->sw, B->x, B->y, B->w, B->h, FRAME_RADIUS * B->w / (w->w ? w->w : 1) + 1);
	else if (c && c->thumb)
		scale_blit(c->thumb, c->tw, c->th, c->tw, B->x, B->y, B->w, B->h, 4);
	else
		bb_fill(B->x, B->y, B->w, B->h, 0x00808080);
}

/* la zone clip_r, pendant la vue : le fond, les bureaux et leurs fenêtres, puis les barres, le
 * dash et les menus (le curseur : compose_clip) */
void ov_compose_clip(void)
{
	ov_geom();
	if (bg_dim && bg_ok && bg_w == scr_w && bg_h == scr_h)
		bb_blit(bg_dim, scr_w * 4, 0, 0, scr_w, scr_h);
	else
		bb_fill(clip_r.x0, clip_r.y0, clip_r.x1 - clip_r.x0, clip_r.y1 - clip_r.y0, OV_COL_EMPTY);
	long steady = ov_p == OV_ONE && !ov_q && ov_geom_tw == g.tw;
	long rad = OV_R_DESK * ov_p / OV_ONE;
	if (ov_q) /* (en rangée : des coins plus petits) */
		rad = lerp(rad, 8, ov_q);
	for (long d = 0; d < desk_count; d++) {
		struct box D;
		desk_rect(d, &D);
		if (!box_in_clip(&D, OV_SH_DESK + 4))
			continue;
		if (ov_p > OV_ONE / 8)
			halo(&D, rad, 0, OV_SH_DESK, 0, 110 * ov_p / OV_ONE, 1, 4);
		if (steady && bg_thumb && bgt_w == D.w && bgt_h == D.h)
			bb_blit_any(bg_thumb, bgt_w * 4, D.x, D.y, D.w, D.h, 1);
		else if (bg_full && bg_ok)
			scale_blit(bg_full, g.W, g.H, g.W, D.x, D.y, D.w, D.h, rad);
		else
			bb_fill(D.x, D.y, D.w, D.h, COL_DESK);
		if ((steady || ov_q == OV_ONE) && ov_hover_desk == d && ov_hover < 0 && ov_drag < 0)
			halo(&D, rad, 2, 5, 0x00FFFFFF, 140, 0, 0);
		else if (ov_q == OV_ONE && d == ov_sel) /* (en rangée : le bureau courant) */
			halo(&D, rad, 2, 4, 0x00FFFFFF, 90, 0, 0);
		for (long i = 0; i < n_items; i++) {
			const struct ov_item *it = &items[i];
			if (it->desk != d || i == ov_drag)
				continue;
			struct box B;
			item_rect(it, &D, &B);
			draw_item(it, &B, steady);
			if (steady && ov_hover == i && ov_drag < 0)
				halo(&B, 8, 3, 7, 0x00FFFFFF, 170, 0, 0);
		}
		if (ov_drag >= 0 && items[ov_drag].desk != d && ov_hover_desk == d)
			halo(&D, rad, 2, 6, 0x00FFFFFF, 170, 0, 0); /* (là où la fenêtre irait) */
	}
	/* les barres, le dash (qui monte pendant l'ouverture), les menus, les volets */
	long slide = (OV_ONE - ov_p) * (g.H - g.dash_y + 24) / OV_ONE;
	for (long layer = 2; layer < WIN_LAYERS; layer++) {
		for (long i = 0; i < win_count; i++) {
			struct win *w = win_z[i];
			if (w->mini || win_layer(w) != layer || !ov_top(w))
				continue;
			long gs = 0; /* (la grille monte du bas avec ov_q) */
			if (w->flags & WF_APPGRID) {
				if (!ov_q)
					continue;
				gs = (OV_ONE - ov_q) * (g.H - w->y) / OV_ONE;
			}
			if (((w->flags & WF_OVERVIEW) && slide) || gs) {
				long dy = (w->flags & WF_APPGRID) ? gs : slide;
				w->y += dy;
				draw_window(w);
				w->y -= dy;
			} else {
				draw_window(w);
			}
		}
	}
	if (ov_drag >= 0) { /* la fenêtre glissée, sous le curseur */
		const struct ov_item *it = &items[ov_drag];
		struct box B = { cur_x - ov_drag_dx, cur_y - ov_drag_dy, it->rw, it->rh };
		draw_item(it, &B, 1);
	}
}

/* ---------------------------------------------------------------------------
 *  Ouvrir, fermer, animer
 * --------------------------------------------------------------------------- */

static void publish(long on)
{
	if (bp_canal)
		bp_publish(ov_topic, on ? "1" : "0", BUS_RETAIN);
}

static void anim_to(long pb, long ms)
{
	ov_pa = ov_p;
	ov_pb = pb;
	ov_t0 = now_ms();
	ov_dur = ms;
}

static void scroll_to(long d)
{
	if (d < 0)
		d = 0;
	if (d >= desk_count)
		d = desk_count - 1;
	ov_sel = d;
	ov_pos_a = ov_pos;
	ov_pos_b = d << 16;
	ov_st0 = now_ms();
}

void ov_open(void)
{
	if ((policy->flags & POLF_POCKET) || !ov_enabled)
		return;
	if (ov_on) {
		if (ov_state == 3) { /* (en train de se fermer : on revient) */
			ov_state = 1;
			anim_to(OV_ONE, OV_OPEN_MS);
			publish(1);
		}
		return;
	}
	drag_win = 0; /* (les gestes en cours de la vue normale) */
	resize_win = 0;
	capture_win = 0;
	ov_on = 1;
	ov_state = 1;
	ov_p = 0;
	anim_to(OV_ONE, OV_OPEN_MS);
	ov_sel = desk_cur;
	ov_pos = ov_pos_a = ov_pos_b = desk_cur << 16;
	ov_hover = ov_hover_desk = ov_drag = -1;
	ov_pressed = 0;
	dash_show(1);
	ov_geom();
	bg_build();
	bg_thumb_build();
	layout_all();
	thumbs_all();
	publish(1);
	wins_changed();
	dirty_all();
}

/* bureau (-1 : celui du milieu), fenêtre à ramener (0 : aucune) : retour à la vue normale */
void ov_close(long d, struct win *w)
{
	if (!ov_on)
		return;
	if (d < 0)
		d = ov_sel;
	if (d >= desk_count)
		d = desk_count - 1;
	if (ov_sel != d || ov_pos != (d << 16))
		scroll_to(d);
	ov_drag = -1;
	ov_hover = ov_hover_desk = -1;
	for (long i = 0; i < MAX_WINDOWS; i++) /* (la grille des applications part tout de suite) */
		if (windows[i].used && (windows[i].flags & WF_APPGRID))
			windows[i].mini |= MINI_OVERVIEW;
	ov_qa = ov_q;
	ov_qb = 0;
	ov_qt0 = now_ms();
	desk_show(d);
	desk_apply(); /* (les fenêtres glissées sur un autre bureau pendant la vue) */
	if (w) {
		w->mini &= ~(u64)MINI_REDUCED;
		struct win *b = win_blocker(w);
		win_raise(b ? b : w);
	}
	if (ov_state != 3) {
		ov_state = 3;
		anim_to(0, OV_CLOSE_MS);
		publish(0);
	}
	snaps_for(d);
	dirty_all();
}

static void ov_finish(void)
{
	ov_on = 0;
	ov_state = 0;
	ov_p = OV_ONE;
	ov_q = ov_qa = ov_qb = 0;
	ov_drag = ov_hover = ov_hover_desk = -1;
	dash_show(0);
	caches_free();
	desk_apply();
	wins_changed();
	dirty_all();
}

/* (screen.c) la taille de l'écran change : la vue se ferme sans animation, tout est rendu */
void ov_reset(void)
{
	if (ov_on) {
		publish(0);
		ov_finish();
	}
	if (bg_full)
		free(bg_full);
	if (bg_dim)
		free(bg_dim);
	bg_full = bg_dim = 0;
	bg_w = bg_h = 0;
	bg_ok = 0;
}

/* -> 1 pendant une animation (la boucle n'attend presque pas) */
long ov_busy(void)
{
	return sw_on || (ov_on && (ov_state != 2 || ov_pos != ov_pos_b || ov_q != ov_qb));
}

/* à chaque tour (dk_tick) */
static void sw_tick(void);

void ov_tick(void)
{
	sw_tick();
	if (!ov_on)
		return;
	u64 now = now_ms();
	ov_geom();
	if (ov_state != 3 && (member_sig() != ov_sig || ov_geom_tw != g.tw)) {
		/* fenêtre ouverte, fermée, déplacée, bureau ou dash changés : l'exposé refait */
		if (ov_geom_tw != g.tw)
			bg_thumb_build();
		layout_all();
		thumbs_all();
		if (ov_drag >= n_items)
			ov_drag = -1;
		if (ov_hover >= n_items)
			ov_hover = -1;
		dirty_all();
	}
	if (ov_state == 1 || ov_state == 3) {
		long t = (long)((now - ov_t0) * OV_ONE / (u64)(ov_dur ? ov_dur : 1));
		ov_p = ov_pa + (ov_pb - ov_pa) * ease(t) / OV_ONE;
		dirty_all();
		if (t >= OV_ONE) {
			ov_p = ov_pb;
			if (ov_state == 3) {
				ov_pos = ov_pos_b;
				ov_finish();
				return;
			}
			ov_state = 2;
		}
	}
	long qt = ov_state != 3 && grid_win() ? OV_ONE : 0; /* la grille des applications : montée ? */
	if (ov_state != 3 && qt != ov_qb) {
		ov_qa = ov_q;
		ov_qb = qt;
		ov_qt0 = now;
		ov_hover = ov_hover_desk = -1;
	}
	if (ov_q != ov_qb) {
		long t = (long)((now - ov_qt0) * OV_ONE / OV_APPS_MS);
		ov_q = ov_qa + (ov_qb - ov_qa) * ease(t) / OV_ONE;
		if (t >= OV_ONE)
			ov_q = ov_qb;
		dirty_all();
	}
	if (ov_pos != ov_pos_b) {
		long t = (long)((now - ov_st0) * OV_ONE / OV_SCROLL_MS);
		ov_pos = ov_pos_a + (ov_pos_b - ov_pos_a) * ease(t) / OV_ONE;
		if (t >= OV_ONE)
			ov_pos = ov_pos_b;
		dirty_all();
	}
	if (ov_state != 2)
		return;
	/* une vignette dont le contenu a changé (au plus une par tour) */
	for (long i = 0; i < n_items; i++) {
		struct ov_item *it = &items[i];
		struct win *w = it->w;
		struct ov_cache *c = &cache[w - windows];
		if (c->id != w->id || !c->thumb)
			continue;
		if (c->gen == w->gen && c->cgen == w->cgen && c->act == (win_focus == w))
			continue;
		if (now - c->t < OV_REFRESH_MS)
			continue;
		thumb_build(it, 1, c->snap != 0);
		struct box D, B;
		desk_rect(it->desk, &D);
		item_rect(it, &D, &B);
		dirty_box(&B, OV_SH_WIN + 8);
		break;
	}
}

/* une fenêtre va disparaître : son cache est rendu */
void ov_gone(struct win *w)
{
	cache_free(&cache[w - windows]);
	for (long i = 0; i < n_items; i++)
		if (items[i].w == w) {
			if (ov_drag == i)
				ov_drag = -1;
			ov_hover = -1;
			ov_sig = 0; /* (refait au prochain tour) */
		}
}

/* WS_OP_OVERVIEW : a -1 basculer, 0 fermer, 1 ouvrir, 2 lire -> 1 ouverte / 0 fermée ; 3 le bureau passe en vue
 * d'ensemble, 4 au bureau classique (les Réglages, sans redémarrer ; system.ini est écrit par eux) -> 0 */
long req_overview(void)
{
	long m = MS64(msg, WM_A);
	if ((m == 3 || m == 4) && !(policy->flags & POLF_POCKET)) {
		if (m == 4 && ov_on) /* (la vue ouverte : fermée tout de suite) */
			ov_reset();
		ov_enabled = m == 3;
		wins_changed(); /* (la barre de menus relit le mode) */
		dirty_all();
		return 0;
	}
	if ((policy->flags & POLF_POCKET) || !ov_enabled) /* (le bureau classique : pas de vue d'ensemble) */
		return E_INVAL;
	long a = MS64(msg, WM_A);
	if (a == -1)
		a = ov_on && ov_state != 3 ? 0 : 1;
	if (a == 1)
		ov_open();
	else if (a == 0)
		ov_close(-1, 0);
	return ov_on && ov_state != 3;
}

/* (desktop.c) Super seul */
void ov_toggle(void)
{
	if (ov_on && ov_state != 3)
		ov_close(-1, 0);
	else
		ov_open();
}

/* ---------------------------------------------------------------------------
 *  Entrées
 * --------------------------------------------------------------------------- */

/* x, y -> fenêtre du carrousel sous le point (rang, -1), son bureau (-1 : aucun) */
static long item_at(long x, long y, long *desk)
{
	*desk = -1;
	for (long d = 0; d < desk_count; d++) {
		struct box D;
		desk_rect(d, &D);
		if (!box_hit(&D, x, y))
			continue;
		*desk = d;
		for (long i = n_items; i-- > 0;) {
			if (items[i].desk != d || i == ov_drag)
				continue;
			struct box B;
			item_rect(&items[i], &D, &B);
			if (box_hit(&B, x, y))
				return i;
		}
		return -1;
	}
	return -1;
}

static void dirty_item(long i)
{
	if (i < 0 || i >= n_items)
		return;
	struct box D, B;
	desk_rect(items[i].desk, &D);
	item_rect(&items[i], &D, &B);
	dirty_box(&B, OV_SH_WIN + 8);
}

static void dirty_desk(long d)
{
	if (d < 0)
		return;
	struct box D;
	desk_rect(d, &D);
	dirty_box(&D, 8);
}

static void hover_set(long i, long d)
{
	if (i == ov_hover && d == ov_hover_desk)
		return;
	dirty_item(ov_hover);
	dirty_item(i);
	if (d != ov_hover_desk || (i < 0) != (ov_hover < 0)) {
		dirty_desk(ov_hover_desk);
		dirty_desk(d);
	}
	ov_hover = i;
	ov_hover_desk = d;
}

/* x, y, appuyés, relâchés -> 1 si la vue l'a pris (sinon : une barre, le dash, un menu) */
long ov_mouse(long x, long y, long down, long up)
{
	if (ov_drag < 0 && !ov_pressed) {
		long zone;
		if (win_hit(x, y, &zone)) { /* (win_hit ne voit que ov_top pendant la vue) */
			hover_set(-1, -1);
			return 0;
		}
	}
	if (ov_state != 2 || ov_q != ov_qb) /* (pendant une animation : rien) */
		return 1;
	if (ov_drag >= 0) { /* une fenêtre glissée */
		struct ov_item *it = &items[ov_drag];
		struct box B = { ov_drag_x0, ov_drag_y0, it->rw, it->rh };
		dirty_box(&B, OV_SH_WIN + 4);
		ov_drag_x0 = x - ov_drag_dx;
		ov_drag_y0 = y - ov_drag_dy;
		B.x = ov_drag_x0;
		B.y = ov_drag_y0;
		dirty_box(&B, OV_SH_WIN + 4);
		long d;
		item_at(x, y, &d);
		hover_set(-1, d);
		if (up & 1) { /* posée : sur un autre bureau, elle y passe */
			long i = ov_drag;
			ov_drag = -1;
			ov_pressed = 0;
			if (d >= 0 && d != items[i].desk) {
				items[i].desk = d;
				items[i].w->desk = (u64)d + 1;
				ov_sig = 0; /* (l'exposé refait au prochain tour) */
			}
			dirty_all();
		}
		return 1;
	}
	long d, i = item_at(x, y, &d);
	if (ov_pressed) {
		if (up & 1) {
			ov_pressed = 0;
			long pi = ov_press_item, pd = ov_press_desk;
			if (pi >= 0 && pi == i) { /* une fenêtre : son bureau, elle devant */
				ov_close(items[pi].desk, items[pi].w);
			} else if (pd >= 0 && pd == d && pi < 0 && i < 0) {
				if (d == ov_sel || ov_qb) /* le bureau du milieu (ou de la rangée) : activé */
					ov_close(d, 0);
				else /* un voisin : au milieu */
					scroll_to(d);
			} else if (pd < 0 && d < 0 && !ov_qb) { /* le fond : retour */
				ov_close(-1, 0);
			}
			return 1;
		}
		long mx = x - ov_press_x, my = y - ov_press_y;
		if (ov_press_item >= 0 && (mx * mx + my * my > OV_DRAG_PX * OV_DRAG_PX)) { /* un glisser */
			struct ov_item *it = &items[ov_press_item];
			struct box D, B;
			desk_rect(it->desk, &D);
			item_rect(it, &D, &B);
			ov_drag = ov_press_item;
			ov_drag_dx = ov_press_x - B.x;
			ov_drag_dy = ov_press_y - B.y;
			ov_drag_x0 = x - ov_drag_dx;
			ov_drag_y0 = y - ov_drag_dy;
			dirty_all();
		}
		return 1;
	}
	hover_set(i, d);
	if (down & 1) {
		ov_pressed = 1;
		ov_press_item = i;
		ov_press_desk = d;
		ov_press_x = x;
		ov_press_y = y;
	}
	return 1;
}

/* molette (crans : positif vers le haut) : le bureau d'avant ou d'après */
long ov_wheel(long n)
{
	if (ov_qb) /* (la grille des applications : à elle) */
		return 0;
	if (!n || ov_state == 3)
		return 1;
	scroll_to(ov_sel + (n > 0 ? -1 : 1));
	hover_set(-1, -1);
	return 1;
}

/* molette horizontale (deux doigts sur le pavé tactile ; crans vers la gauche > 0) : le
 * carrousel suit les doigts. Un bureau après OV_SWIPE_FIRST crans d'un même geste, puis un de plus
 * tous les OV_SWIPE_NEXT ; un geste finit après OV_SWIPE_GAP ms sans cran. */
#define OV_SWIPE_FIRST 4
#define OV_SWIPE_NEXT 10
#define OV_SWIPE_GAP 250
static long sw_acc, sw_need = OV_SWIPE_FIRST;
static u64 sw_t;

void ov_hwheel(long n)
{
	if (!n || ov_state == 3 || ov_qb)
		return;
	u64 now = now_ms();
	if (now - sw_t > OV_SWIPE_GAP || (sw_acc > 0) != (n > 0)) { /* un nouveau geste, ou l'autre sens */
		sw_acc = 0;
		sw_need = OV_SWIPE_FIRST;
	}
	sw_t = now;
	sw_acc += n;
	if (sw_acc >= sw_need || -sw_acc >= sw_need) {
		scroll_to(ov_sel + (sw_acc > 0 ? -1 : 1));
		hover_set(-1, -1);
		sw_acc = 0;
		sw_need = OV_SWIPE_NEXT;
	}
}

/* entrée clavier -> 1 si prise (pendant la vue, tout ce qui ne va pas à un menu) */
long ov_key(const u8 *in)
{
	if (find_grab())
		return 0;
	struct win *f = key_target(); /* (un volet ouvert pendant la vue : les réglages rapides, le Wi-Fi) */
	if (f && ov_top(f))
		return 0;
	u64 code = M64(in, 8);
	if (!M64(in, 16))
		return 1;
	if (code == KEY_ESC || code == KEY_ENTER)
		ov_close(-1, 0);
	else if (code == KEY_LEFT)
		scroll_to(ov_sel - 1);
	else if (code == KEY_RIGHT)
		scroll_to(ov_sel + 1);
	return 1;
}

/* ---------------------------------------------------------------------------
 *  Le glissement d'un bureau à l'autre, dans la vue normale (23/11/2026)
 * ---------------------------------------------------------------------------
 * Ctrl+Alt+Gauche / Droite, ou une fenêtre ramenée sur un autre bureau : comme GNOME, le contenu du
 * bureau d'avant (fond et fenêtres normales) sort d'un côté pendant que celui du nouveau entre de l'autre
 * (OV_SWITCH_MS, « ease out ») ; les barres, les volets et le curseur restent en place. Les deux images
 * sont composées une fois (hors écran, le chemin ordinaire) : chaque image n'est que deux copies. */
#define OV_SWITCH_MS 260

long sw_dir, sw_on;
static u32 *sw_old, *sw_new;
static long sw_w, sw_h, sw_sign;
static u64 sw_t0;
static long sw_x; /* décalage du bureau d'avant (0 .. -sign * largeur) */

/* la couche du fond et la couche normale (le bureau courant) composées en pleine taille dans dst */
static void render_desk(u32 *dst)
{
	u32 *keep_bb = bb;
	long keep_pitch = bb_pitch;
	struct rect keep_clip = clip_r;
	bb = dst;
	bb_pitch = scr_w << 2;
	clip_r = (struct rect){ 0, 0, scr_w, scr_h };
	policy->back();
	for (long layer = 0; layer < 2; layer++)
		for (long i = 0; i < win_count; i++) {
			struct win *w = win_z[i];
			if (w->mini || win_layer(w) != layer)
				continue;
			draw_window(w);
		}
	bb = keep_bb;
	bb_pitch = keep_pitch;
	clip_r = keep_clip;
}

static void sw_free(void)
{
	if (sw_old)
		free(sw_old);
	if (sw_new)
		free(sw_new);
	sw_old = sw_new = 0;
	sw_on = 0;
}

long sw_prepare(void)
{
	if (ov_on || (policy->flags & POLF_POCKET) || curtain_on)
		return 0;
	if (sw_on) /* (un glissement pas fini : le suivant part d'ici) */
		sw_free();
	long px = scr_w * scr_h;
	sw_old = ov_alloc(px * 4);
	sw_new = ov_alloc(px * 4);
	if (!sw_old || !sw_new) {
		sw_free();
		return 0;
	}
	sw_w = scr_w;
	sw_h = scr_h;
	render_desk(sw_old);
	return 1;
}

void sw_start(long dir)
{
	if (!sw_old || !sw_new)
		return;
	render_desk(sw_new);
	sw_sign = dir > 0 ? 1 : -1;
	sw_t0 = now_ms();
	sw_x = 0;
	sw_on = 1;
	dirty_all();
}

static void sw_tick(void)
{
	if (!sw_on)
		return;
	if (sw_w != scr_w || sw_h != scr_h) { /* (l'écran a changé de taille) */
		sw_free();
		dirty_all();
		return;
	}
	long t = (long)((now_ms() - sw_t0) * OV_ONE / OV_SWITCH_MS);
	if (t >= OV_ONE) {
		sw_free();
		dirty_all();
		return;
	}
	sw_x = -sw_sign * (sw_w * ease(t) / OV_ONE);
	dirty_all();
}

void sw_compose_clip(void)
{
	long W = sw_w;
	bb_blit(sw_old, W * 4, sw_x, 0, W, sw_h);              /* l'ancien sort, */
	bb_blit(sw_new, W * 4, sw_x + sw_sign * W, 0, W, sw_h); /* le nouveau entre */
	for (long layer = 2; layer < WIN_LAYERS; layer++) /* les barres, les volets, les menus : en place */
		for (long i = 0; i < win_count; i++) {
			struct win *w = win_z[i];
			if (w->mini || win_layer(w) != layer)
				continue;
			draw_window(w);
		}
	policy->front();
}
