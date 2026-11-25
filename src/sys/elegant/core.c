/* core.c : le cœur du serveur graphique, commun à tous les modes (core.inc) : fenêtres,
 * recomposition, routage des entrées, requêtes des programmes. Ce qui change d'un mode à l'autre
 * (fond, session, réglages) passe par la table du mode actif (struct policy, comme struct
 * ws_policy d'Onyx).
 *
 * Recomposition : les zones sales (une liste de rectangles) sont redessinées dans un tampon
 * arrière (mémoire du serveur, cacheable), puis présentées au framebuffer (present.c : DMA 2D
 * asynchrone sur le Pi, ou copie par le processeur). Chaque fenêtre a ses images dans un tampon
 * partagé (buf_create) que le programme mappe chez lui (buf_map) : le contenu, puis deux copies
 * du cadre (active, inactive), dessinées par UIKit comme dans Onyx. Le serveur ne dessine aucun
 * texte : il compose, et route les entrées.
 *
 * Rôles (WF_* de armpi.inc) : trois couches (fond, normale, dessus), sans cadre, sans focus,
 * hors liste, à l'écoute de la liste, capture (menu ouvert), réserve d'un bord (barre de menus,
 * dock), transparence. */
#include "elegant.h"

/* pixels non alignés sur 8 (une zone commence où elle veut) */
typedef u64 u64u __attribute__((aligned(4), may_alias));

static u64 hist_buf[64][5]; /* (diagnostic) historique */

static void ev_clear(void)
{
	u64 *e = (u64 *)evbuf;
	e[0] = e[1] = e[2] = e[3] = 0;
}

static inline long win_index(const struct win *w)
{
	return w - windows;
}

/* ---------------------------------------------------------------------------
 *  Zones sales
 * ---------------------------------------------------------------------------
 * Une petite liste de rectangles (dirty_list ; dirty_n), pas une seule union : une horloge en
 * haut à droite et le curseur en bas à gauche ne font plus recomposer tout l'écran. Une zone
 * nouvelle est fusionnée avec une zone de la liste quand leur union coûte peu (aire de l'union <=
 * somme des aires + DIRTY_SLACK : zones qui se chevauchent, se touchent, ou proches) ; l'union
 * fusionnée est réessayée contre les autres. Liste pleine : repli sur l'union de tout (une
 * seule zone). */

/* x, y, largeur, hauteur (écran) : ajoute une zone sale */
void dirty(long x, long y, long w, long h)
{
	if (w <= 0 || h <= 0)
		return;
	long x1 = x + w, y1 = y + h; /* -> x0, y0, x1, y1 bornés à l'écran */
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x1 > scr_w)
		x1 = scr_w;
	if (y1 > scr_h)
		y1 = scr_h;
	if (x >= x1 || y >= y1)
		return;
again:
	for (long i = 0; i < dirty_n; i++) {
		struct rect *r = &dirty_list[i];
		long ux0 = x < r->x0 ? x : r->x0; /* union */
		long uy0 = y < r->y0 ? y : r->y0;
		long ux1 = x1 > r->x1 ? x1 : r->x1;
		long uy1 = y1 > r->y1 ? y1 : r->y1;
		u64 sum = (u64)(r->x1 - r->x0) * (u64)(r->y1 - r->y0) + (u64)(x1 - x) * (u64)(y1 - y) +
			  DIRTY_SLACK;
		u64 un = (u64)(ux1 - ux0) * (u64)(uy1 - uy0);
		if (un > sum)
			continue;
		x = ux0; /* fusion : la nouvelle devient l'union, la zone i sort (la dernière à sa place) */
		y = uy0;
		x1 = ux1;
		y1 = uy1;
		dirty_n--;
		*r = dirty_list[dirty_n];
		goto again; /* l'union, contre les autres */
	}
	if (dirty_n >= DIRTY_MAX) { /* liste pleine : l'union de tout */
		for (long i = 0; i < dirty_n; i++) {
			struct rect *r = &dirty_list[i];
			if (r->x0 < x)
				x = r->x0;
			if (r->y0 < y)
				y = r->y0;
			if (r->x1 > x1)
				x1 = r->x1;
			if (r->y1 > y1)
				y1 = r->y1;
		}
		dirty_list[0] = (struct rect){ x, y, x1, y1 };
		dirty_n = 1;
		dirty_overflow++; /* (diagnostic F11) */
		return;
	}
	dirty_list[dirty_n++] = (struct rect){ x, y, x1, y1 };
}

void dirty_all(void)
{
	dirty(0, 0, scr_w, scr_h);
}

/* fenêtre -> rectangle extérieur (cadre compris) */
void win_outer(const struct win *w, long *x, long *y, long *wd, long *ht)
{
	*x = w->x;
	*y = w->y;
	*wd = w->w;
	*ht = w->h;
	if (w->flags & WF_FRAMELESS)
		return;
	*x -= FRAME_BORDER;
	*y -= FRAME_TITLE_H;
	*wd += 2 * FRAME_BORDER;
	*ht += FRAME_TITLE_H + FRAME_BORDER;
}

static void glass_zone(const struct win *w, struct rect *z);

/* fenêtre : marque son rectangle extérieur, et l'ombre d'une fenêtre de verre (hors de son
 * rectangle : sans elle, l'ombre de la barre de menus et du panel restait après leur fermeture) */
void dirty_window(struct win *w)
{
	long x, y, wd, ht;
	win_outer(w, &x, &y, &wd, &ht);
	dirty(x, y, wd, ht);
	if (w->flags & WF_BLUR) {
		struct rect z;
		glass_zone(w, &z);
		if (z.x1 > z.x0 && z.y1 > z.y0)
			dirty(z.x0, z.y0, z.x1 - z.x0, z.y1 - z.y0);
	}
}

/* fenêtre -> couche : 0 fond, 1 normale, 2 dessus, 3 plein écran (WF_TOP sans cadre, en 0,0, de
 * la taille de l'écran : un jeu, un émulateur ; au-dessus du panel, de la barre de menus et de
 * leurs bulles), 4 les menus (ui_popup : WF_TOP | WF_GRAB | WF_NOFOCUS), au-dessus de tout, plein
 * écran compris : la liste d'une liste déroulante de l'installateur, qui couvre l'écran), 5 la couche
 * sûre : la carte d'élévation et l'écran de connexion (A5), au-dessus de tout ce qu'une app peut ouvrir */
long win_layer(const struct win *w)
{
	u64 f = w->flags;
	if (w->secure)
		return 5;
	if (f & WF_HOME) /* (l'accueil du mode poche : au fond) */
		return 0;
	if (f & WF_BACK)
		return 0;
	if (!(f & WF_TOP))
		return 1;
	if ((f & (WF_GRAB | WF_NOFOCUS)) == (WF_GRAB | WF_NOFOCUS))
		return 4;
	if ((f & WF_FRAMELESS) && !(w->x | w->y) && (u64)w->w >= (u64)scr_w && (u64)w->h >= (u64)scr_h)
		return 3;
	return 2;
}

/* la liste des fenêtres a changé : les fenêtres à l'écoute seront prévenues */
void wins_changed(void)
{
	wins_dirty = 1;
}

/* (diagnostic) genre, quatre valeurs : une entrée dans l'historique (F11) */
void hist(long kind, long a, long b, long c, long d)
{
	u64 *e = hist_buf[hist_n++ & 63];
	e[0] = (u64)kind;
	e[1] = (u64)a;
	e[2] = (u64)b;
	e[3] = (u64)c;
	e[4] = (u64)d;
}

/* ---------------------------------------------------------------------------
 *  Dessin dans le tampon arrière, découpé par le clip courant (clip_r)
 * --------------------------------------------------------------------------- */

/* x, y, largeur, hauteur -> intersection avec le clip : coin, taille (<= 0 si vide) */
void clip_rect(long *x, long *y, long *w, long *h)
{
	long x0 = *x, y0 = *y, x1 = x0 + *w, y1 = y0 + *h;
	if (x0 < clip_r.x0)
		x0 = clip_r.x0;
	if (y0 < clip_r.y0)
		y0 = clip_r.y0;
	if (x1 > clip_r.x1)
		x1 = clip_r.x1;
	if (y1 > clip_r.y1)
		y1 = clip_r.y1;
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
}

void bb_fill(long x, long y, long w, long h, u32 color)
{
	clip_rect(&x, &y, &w, &h);
	if (w <= 0 || h <= 0)
		return;
	u8 *row = (u8 *)bb + y * bb_pitch + x * 4;
	for (; h; h--, row += bb_pitch) {
		u32 *d = (u32 *)row;
		for (long i = 0; i < w; i++)
			d[i] = color;
	}
}

void bb_blit(const u32 *src, long pitch, long x, long y, long w, long h)
{
	bb_blit_any(src, pitch, x, y, w, h, 0);
}

/* (src * (255 - t) + dst * t) / 255, canal par canal (les trois du bas ; le quatrième à 0),
 * deux canaux à la fois sur 16 bits chacun : la même arrondi que l'asm, x + x / 256 + 1, / 256 */
static inline u32 blend(u32 s, u32 d, u32 t)
{
	u32 u = 255 - t;
	u32 lo = (s & 0x00FF00FF) * u + (d & 0x00FF00FF) * t;
	u32 hi = ((s >> 8) & 0x00FF00FF) * u + ((d >> 8) & 0x00FF00FF) * t;
	lo = ((lo + ((lo >> 8) & 0x00FF00FF) + 0x00010001) >> 8) & 0x00FF00FF;
	hi = ((hi + ((hi >> 8) & 0x00FF00FF) + 0x00010001) >> 8) & 0x00FF00FF;
	return (lo | hi << 8) & 0x00FFFFFF;
}

/* source, pas source (octets), x, y (écran), largeur, hauteur, alpha = 1 : transparence (octet
 * haut : 0 opaque, 255 invisible). Copie découpée par le clip. */
void bb_blit_any(const u32 *src, long pitch, long x, long y, long w, long h, long alpha)
{
	long x0 = x, y0 = y;
	clip_rect(&x, &y, &w, &h);
	if (w <= 0 || h <= 0)
		return;
	const u8 *s = (const u8 *)src + (y - y0) * pitch + (x - x0) * 4; /* décalage dans la source */
	u8 *d = (u8 *)bb + y * bb_pitch + x * 4;
	if (!alpha) { /* --- opaque */
		for (; h; h--, s += pitch, d += bb_pitch) {
			const u32 *sp = (const u32 *)s;
			u32 *dp = (u32 *)d;
			for (long i = 0; i < w; i++)
				dp[i] = sp[i];
		}
		return;
	}
	for (; h; h--, s += pitch, d += bb_pitch) { /* --- avec transparence */
		const u32 *sp = (const u32 *)s;
		u32 *dp = (u32 *)d;
		long n = w;
		for (; n >= 4; n -= 4, sp += 4, dp += 4) { /* quatre pixels (le NEON de l'asm) */
			u32 s0 = sp[0], s1 = sp[1], s2 = sp[2], s3 = sp[3];
			u32 t0 = s0 >> 24, t1 = s1 >> 24, t2 = s2 >> 24, t3 = s3 >> 24;
			if (!(t0 | t1 | t2 | t3)) { /* tous opaques */
				dp[0] = s0;
				dp[1] = s1;
				dp[2] = s2;
				dp[3] = s3;
				continue;
			}
			if ((t0 & t1 & t2 & t3) == 255) /* tous invisibles */
				continue;
			/* mélangés : chacun par la formule (un invisible y rend dst sans son octet haut,
			   un opaque src : comme l'asm) */
			dp[0] = t0 ? blend(s0, dp[0], t0) : s0;
			dp[1] = t1 ? blend(s1, dp[1], t1) : s1;
			dp[2] = t2 ? blend(s2, dp[2], t2) : s2;
			dp[3] = t3 ? blend(s3, dp[3], t3) : s3;
		}
		for (; n; n--, sp++, dp++) { /* le reste, pixel par pixel */
			u32 v = *sp, t = v >> 24;
			if (!t)
				*dp = v;
			else if (t != 255)
				*dp = blend(v, *dp, t);
		}
	}
}

/* ---------------------------------------------------------------------------
 *  Verre : flou d'arrière-plan et ombre portée (WF_BLUR, le panel ; DESIGN-ELEGANT.md 8.9)
 * ---------------------------------------------------------------------------
 * Le flou se calcule dans le tampon arrière, sur ce qui est déjà composé sous la fenêtre, juste
 * avant de la poser. Il a besoin de toute la zone : une zone sale qui touche une fenêtre WF_BLUR
 * (ou son ombre) est agrandie à toute la fenêtre et son ombre (glass_extend), sinon un petit
 * morceau flouté seul (le curseur) ferait des marques. Deux passes de boîte (horizontale puis
 * verticale) font un flou proche d'un gaussien, bords pris sur eux-mêmes. */
#define GLASS_R 5      /* rayon de la boîte (pixels) */
#define GLASS_SHADOW 10 /* hauteur de l'ombre au-dessus de la fenêtre */
#define GLASS_MAXW 4096
#define GLASS_MAXH 256

static u32 glass_line[GLASS_MAXW > GLASS_MAXH ? GLASS_MAXW : GLASS_MAXH];

/* n pixels à pas (en u32) : flou de boîte de rayon GLASS_R, sur place (copie dans glass_line) */
static void glass_box(u32 *p, long n, long step)
{
	if (n < 2)
		return;
	for (long i = 0; i < n; i++)
		glass_line[i] = p[i * step];
	const long r = GLASS_R, k = 2 * r + 1;
	u32 sr = 0, sg = 0, sb = 0;
	for (long i = -r; i <= r; i++) { /* la fenêtre de départ, bords répétés */
		u32 v = glass_line[i < 0 ? 0 : i >= n ? n - 1 : i];
		sr += (v >> 16) & 0xFF;
		sg += (v >> 8) & 0xFF;
		sb += v & 0xFF;
	}
	for (long i = 0; i < n; i++) {
		p[i * step] = (sr / k) << 16 | (sg / k) << 8 | (sb / k);
		long a = i + r + 1, b = i - r;
		u32 va = glass_line[a >= n ? n - 1 : a], vb = glass_line[b < 0 ? 0 : b];
		sr += ((va >> 16) & 0xFF) - ((vb >> 16) & 0xFF);
		sg += ((va >> 8) & 0xFF) - ((vb >> 8) & 0xFF);
		sb += (va & 0xFF) - (vb & 0xFF);
	}
}

/* l'ombre d'une fenêtre de verre : sous elle si elle touche le haut de l'écran (la barre de
 * menus), au-dessus sinon (le panel) */
static int glass_shadow_below(const struct win *w)
{
	return w->y <= 0;
}

/* la zone d'une fenêtre de verre : elle et son ombre, dans l'écran */
static void glass_zone(const struct win *w, struct rect *z)
{
	long y0 = w->y, y1 = w->y + w->h;
	if (glass_shadow_below(w))
		y1 += GLASS_SHADOW;
	else
		y0 -= GLASS_SHADOW;
	z->x0 = w->x < 0 ? 0 : w->x;
	z->y0 = y0 < 0 ? 0 : y0;
	z->x1 = w->x + w->w > scr_w ? scr_w : w->x + w->w;
	z->y1 = y1 > scr_h ? scr_h : y1;
}

/* zone sale -> agrandie à la zone de chaque fenêtre de verre qu'elle touche */
static void glass_extend(struct rect *r)
{
	for (long i = 0; i < win_count; i++) {
		struct win *w = win_z[i];
		if (!w->used || w->mini || !(w->flags & WF_BLUR))
			continue;
		struct rect z;
		glass_zone(w, &z);
		if (r->x1 <= z.x0 || r->x0 >= z.x1 || r->y1 <= z.y0 || r->y0 >= z.y1)
			continue;
		if (z.x0 < r->x0)
			r->x0 = z.x0;
		if (z.y0 < r->y0)
			r->y0 = z.y0;
		if (z.x1 > r->x1)
			r->x1 = z.x1;
		if (z.y1 > r->y1)
			r->y1 = z.y1;
	}
}

/* fenêtre de verre : son ombre au-dessus d'elle, puis ce qu'il y a sous elle flouté (seulement
 * si le clip la contient toute : une lecture d'un morceau, rdpd, la pose sans flou) */
static void glass_under(const struct win *w)
{
	struct rect z;
	glass_zone(w, &z);
	if (clip_r.x0 > z.x0 || clip_r.y0 > z.y0 || clip_r.x1 < z.x1 || clip_r.y1 < z.y1)
		return;
	int below = glass_shadow_below(w);
	long sy0 = below ? w->y + w->h : z.y0, sy1 = below ? z.y1 : w->y;
	for (long y = sy0; y < sy1; y++) { /* l'ombre : du noir, plus dense près de la fenêtre */
		long d = below ? y - (w->y + w->h) + 1 : w->y - y; /* 1 .. GLASS_SHADOW */
		long e = GLASS_SHADOW + 1 - d;
		u32 t = 255 - (u32)(e * e * 46 / ((GLASS_SHADOW) * (GLASS_SHADOW))); /* ~18 % au bord */
		u32 *row = (u32 *)((u8 *)bb + y * bb_pitch);
		for (long x = z.x0; x < z.x1; x++)
			row[x] = blend(0, row[x], t);
	}
	long y0 = w->y < 0 ? 0 : w->y;
	long ye = w->y + w->h > scr_h ? scr_h : w->y + w->h;
	long h = ye - y0, wd = z.x1 - z.x0;
	if (h < 2 || wd < 2 || wd > GLASS_MAXW || h > GLASS_MAXH)
		return;
	u32 *base = (u32 *)((u8 *)bb + y0 * bb_pitch) + z.x0;
	long pitch = bb_pitch >> 2;
	for (int pass = 0; pass < 2; pass++) {
		for (long y = 0; y < h; y++)
			glass_box(base + y * pitch, wd, 1);
		for (long x = 0; x < wd; x++)
			glass_box(base + x, h, pitch);
	}
}

/* ---------------------------------------------------------------------------
 *  Recomposition
 * ---------------------------------------------------------------------------
 * Les zones sales sont prises (cmp_list ; d'autres peuvent se marquer pendant le dessin : elles
 * iront à la recomposition suivante), redessinées une à une dans le tampon arrière (clip = la
 * zone), puis présentées à l'écran ensemble (present.c). Avant de dessiner, le transfert
 * précédent doit être fini (le DMA lit le tampon arrière). */
void compose(void)
{
	if (!dirty_n)
		return;
	present_wait(); /* (peut tout salir : repli processeur) */
	long n = dirty_n;
	if (!n)
		return;
	dirty_n = 0;
	cmp_n = n;
	for (long i = 0; i < n; i++) {
		cmp_list[i] = dirty_list[i];
		glass_extend(&cmp_list[i]); /* (verre : toute sa zone, ou rien) */
	}
	for (long i = 0; i < n; i++) {
		clip_r = cmp_list[i]; /* clip = la zone */
		hist(100, clip_r.x0, clip_r.y0, clip_r.x1, clip_r.y1); /* (historique : recomposition, zone) */
		compose_clip();
	}
	present(); /* cmp_list -> écran */
	long x0 = cmp_list[0].x0, y0 = cmp_list[0].y0, x1 = cmp_list[0].x1, y1 = cmp_list[0].y1;
	for (long i = 1; i < n; i++) { /* union des zones (vncd, screenshot) */
		if (cmp_list[i].x0 < x0)
			x0 = cmp_list[i].x0;
		if (cmp_list[i].y0 < y0)
			y0 = cmp_list[i].y0;
		if (cmp_list[i].x1 > x1)
			x1 = cmp_list[i].x1;
		if (cmp_list[i].y1 > y1)
			y1 = cmp_list[i].y1;
	}
	snap_record(x0, y0, x1, y1); /* (snapshot.c) */
}

/* redessine la zone clip_r dans le tampon arrière : fond, fenêtres couche par couche (du fond
 * vers l'avant), barres et menus, curseur */
void compose_clip(void)
{
	if (ov_on) { /* la vue d'ensemble (overview.c) : la sienne, puis le curseur */
		ov_compose_clip();
		goto cursor;
	}
	if (sw_on) { /* un bureau qui glisse vers un autre (overview.c) */
		sw_compose_clip();
		goto cursor;
	}
	struct win *cover = surf_cover(); /* une surface couvre la zone : rien dessous */
	if (!cover)
		policy->back();
	long n = win_count;
	for (long layer = 0; layer < WIN_LAYERS; layer++) { /* fenêtres, couche par couche */
		for (long i = 0; i < n; i++) {
			struct win *w = win_z[i];
			if (w->mini)
				continue;
			if (win_layer(w) != layer)
				continue;
			if (cover) {
				if (w != cover) /* celles d'avant sont dessous */
					continue;
				cover = 0;
				if (!(w->flags & WF_FRAMELESS)) {
					cover_only = 1;
					draw_framed(w);
					cover_only = 0;
				} else {
					draw_surface(w, w->x, w->y);
				}
				continue;
			}
			if (pk_veil == w) /* (mode poche : le voile sous le dialogue) */
				veil_fill();
			if (w == drag_win && drag_zone) /* l'aperçu de l'ancrage, sous la fenêtre glissée */
				drag_preview();
			draw_window(w);
		}
	}
	policy->front();
cursor:;
	long cx, cy, cw, ch; /* le curseur (et l'image d'un glisser), s'il touche la zone */
	cursor_box(&cx, &cy, &cw, &ch);
	if (cx >= clip_r.x1 || cy >= clip_r.y1 || cx + cw <= clip_r.x0 || cy + ch <= clip_r.y0)
		return;
	draw_cursor();
}

/* (flush_rect, la copie du tampon arrière vers le framebuffer par le processeur : dans
 * present_aarch64.c et present_x86_64.c) */

/* fenêtre : cadre (copie active ou inactive), puis contenu */
void draw_window(struct win *w)
{
	u64 f = w->flags;
	if (f & WF_FRAMELESS) {
		long a = ((f >> 8) & 1) & ~measure_opaque; /* (WF_ALPHA ; mesure : sans transparence) */
		if ((f & WF_BLUR) && a && !ov_on)
			glass_under(w); /* (le verre : flou dessous, ombre ; pas dans la vue d'ensemble : la barre y est
					   claire, sur le fond de la vue) */
		bb_blit_any(w->surf, w->w * 4, w->x, w->y, w->w, w->h, a);
		draw_surface(w, w->x, w->y); /* la surface attachée par-dessus (surface.c) */
		return;
	}
	const u32 *img = win_focus == w ? w->chr_a : w->chr_i;
	if (win_is_max(w)) { /* agrandie : le contenu seul */
		draw_framed(w);
		return;
	}
	/* le cadre, sans ce que le contenu couvre : barre de titre, bords gauche, droit, bas (coins
	   arrondis, ombre : transparence) */
	long ox, oy, ow, oh;
	win_outer(w, &ox, &oy, &ow, &oh);
	long pitch = ow * 4; /* pas de l'image */
	long a = measure_opaque ^ 1;
	bb_blit_any(img, pitch, ox, oy, ow, FRAME_TITLE_H, a); /* barre de titre */
	bb_blit_any(img + FRAME_TITLE_H * ow, pitch, ox, oy + FRAME_TITLE_H, FRAME_BORDER, /* gauche */
		    oh - FRAME_TITLE_H, a);
	bb_blit_any(img + FRAME_TITLE_H * ow + (ow - FRAME_BORDER), pitch, ox + ow - FRAME_BORDER, /* droit */
		    oy + FRAME_TITLE_H, FRAME_BORDER, oh - FRAME_TITLE_H, a);
	bb_blit_any(img + (oh - FRAME_BORDER) * ow + FRAME_BORDER, pitch, ox + FRAME_BORDER, /* bas */
		    oy + oh - FRAME_BORDER, ow - 2 * FRAME_BORDER, FRAME_BORDER, a);
	draw_framed(w);
}

/* (draw_framed) la fenêtre en cours : son image du cadre, son contenu, son pas */
struct nt_ctx {
	struct win *w;
	const u32 *img;
	long cw, ch, cx, cy; /* contenu : largeur, hauteur, coin à l'écran */
	long iw;             /* largeur de l'image du cadre (pixels) */
};

/* coin dans le contenu, taille, décalage à l'écran (vers la droite) : ce morceau de l'image du
 * cadre, avec sa transparence (nt_part, nt_part_to) */
static void nt_part_to(const struct nt_ctx *c, long x, long y, long w, long h, long off)
{
	bb_blit_any(c->img + (y + FRAME_BORDER) * c->iw + (x + FRAME_BORDER), c->iw * 4, c->cx + x + off,
		    c->cy + y, w, h, 1);
}

/* première ligne, retrait à chaque bout, lignes : ces lignes du contenu */
static void nt_rows(const struct nt_ctx *c, long r0, long inset, long n)
{
	bb_blit_any(c->w->surf + r0 * c->cw + inset, c->cw * 4, c->cx + inset, c->cy + r0,
		    c->cw - 2 * inset, n, (c->w->flags >> 8) & 1);
}

static const u8 nt_inset[FRAME_CORNER] = { 7, 5, 4, 3, 2, 1, 1, 0 }; /* retrait des lignes du bord */

/* fenêtre (son cadre dessiné) : les coins du cadre (rayon FRAME_RADIUS), le contenu sans ses
 * coins, sa surface, les boutons par-dessus */
void draw_framed(struct win *w)
{
	struct nt_ctx c;
	c.w = w;
	c.img = win_focus == w ? w->chr_a : w->chr_i;
	c.cw = w->w;
	c.ch = w->h;
	c.cx = w->x;
	c.cy = w->y;
	c.iw = c.cw + 2 * FRAME_BORDER;
	if (!cover_only) { /* (surf_cover : rien sous la surface) */
		if (win_is_max(w)) { /* agrandie : carrée */
			nt_rows(&c, 0, 0, c.ch);
		} else {
			nt_part_to(&c, 0, 0, FRAME_RADIUS, FRAME_RADIUS, 0); /* les coins */
			nt_part_to(&c, c.cw - FRAME_RADIUS, 0, FRAME_RADIUS, FRAME_RADIUS, 0);
			nt_part_to(&c, 0, c.ch - FRAME_RADIUS, FRAME_RADIUS, FRAME_RADIUS, 0);
			nt_part_to(&c, c.cw - FRAME_RADIUS, c.ch - FRAME_RADIUS, FRAME_RADIUS, FRAME_RADIUS, 0);
			for (long r = 0; r < FRAME_CORNER; r++) { /* contenu : lignes coupées, en haut et en bas */
				nt_rows(&c, r, nt_inset[r], 1);
				nt_rows(&c, c.ch - 1 - r, nt_inset[r], 1);
			}
			nt_rows(&c, FRAME_CORNER, 0, c.ch - 2 * FRAME_CORNER); /* le reste */
		}
	}
	draw_surface(w, c.cx, c.cy);
	/* mode poche : fermer, et le trait de réduire (l'accueil) à la place d'agrandir (sauf un
	   dialogue) */
	if ((policy->flags & POLF_POCKET) && !(w->flags & WF_MODAL)) {
		nt_part_to(&c, c.cw - FRAME_CAP_MARGIN - FRAME_CAP_BTN, FRAME_CAP_MARGIN, FRAME_CAP_BTN,
			   FRAME_CAP_H, 0);
		nt_part_to(&c, c.cw - FRAME_CAP_MARGIN - 3 * FRAME_CAP_BTN, FRAME_CAP_MARGIN, FRAME_CAP_BTN,
			   FRAME_CAP_H, FRAME_CAP_BTN);
		return;
	}
	nt_part_to(&c, c.cw - FRAME_CAP_MARGIN - FRAME_CAP_W, FRAME_CAP_MARGIN, FRAME_CAP_W, /* les boutons */
		   FRAME_CAP_H, 0);
}

/* le curseur (12 x 19, forme de cursor_pick) dessiné par-dessus tout ; « X » noir, « . » blanc,
 * espace : rien (l'asm : un bb_fill d'un pixel pour chacun ; ici le même pixel, découpé de même) */
void draw_cursor(void)
{
	if (lock_win) /* (souris relative : pas de curseur) */
		return;
	draw_ghost(); /* (un glisser-déposer en cours : l'élément sous le curseur) */
	long hx, hy;
	const u8 *shape = cursor_pick(&hx, &hy); /* forme (uikit_ops.c), point chaud */
	long px = cur_x - hx, py = cur_y - hy;
	for (long r = 0; r < 19; r++) {
		long y = py + r;
		for (long col = 0; col < 12; col++) {
			u8 ch = *shape++;
			if (ch == ' ')
				continue;
			long x = px + col;
			if (x < clip_r.x0 || x >= clip_r.x1 || y < clip_r.y0 || y >= clip_r.y1)
				continue;
			*(u32 *)((u8 *)bb + y * bb_pitch + x * 4) = ch == 'X' ? 0 : 0x00FFFFFF;
		}
	}
}

/* ---------------------------------------------------------------------------
 *  Entrées
 * --------------------------------------------------------------------------- */

static const char m_no_hid[] = "elegant : pas de HIDManager (service hid), aucune entree\n";

/* abonnement aux entrées : HIDManager (service « hid ») rend un tampon partagé, l'anneau des
 * événements, lu ici sans appel système */
void hid_subscribe(void)
{
	long c = korin_sys2(SYS_SVC_OPEN, "hid", 10000);
	if (c >= 0) {
		M64(msg, 0) = HID_OP_SUBSCRIBE;
		long b = korin_sys2(SYS_SVC_CALL, c, msg);
		if (b >= 0) {
			long a = korin_sys1(SYS_BUF_MAP, b);
			if (a >= 0) {
				hid_ring = (u8 *)a;
				return;
			}
		}
	}
	ak_puts(m_no_hid);
}

/* vide l'anneau des entrées (HIDManager) */
void do_input(void)
{
	u8 *ring = hid_ring;
	if (!ring)
		return;
	u64 *hd = (u64 *)ring, *tl = (u64 *)(ring + 8);
	for (;;) {
		u64 head = __atomic_load_n(hd, __ATOMIC_RELAXED); /* tête, queue */
		u64 tail = __atomic_load_n(tl, __ATOMIC_RELAXED);
		if (head == tail)
			break;
		__atomic_thread_fence(__ATOMIC_ACQUIRE); /* (l'événement après la tête) */
		in_event(ring + HID_RING_EV + (tail & (HID_RING_N - 1)) * 32);
		__atomic_thread_fence(__ATOMIC_RELEASE); /* lu avant de rendre la place */
		__atomic_store_n(tl, __atomic_load_n(tl, __ATOMIC_RELAXED) + 1, __ATOMIC_RELAXED);
	}
}

/* -> fenêtre qui reçoit le clavier : celle qui capture, sinon l'active */
struct win *secure_top(void)
{
	for (long i = win_count; i;) {
		struct win *w = win_z[--i];
		if (!w->mini && w->secure)
			return w;
	}
	return 0;
}

/* tant qu'une fenêtre sûre est ouverte, le clavier ne va qu'aux fenêtres sûres : une app qui se mettait au
 * premier plan, ou ouvrait un menu (WF_GRAB), sous la carte recevait le mot de passe tapé dans la carte (A5) */
struct win *key_target(void)
{
	struct win *w = find_grab();
	if (w)
		return w;
	struct win *s = secure_top();
	return s && !(win_focus && win_focus->secure) ? s : win_focus;
}

/* -> fenêtre visible qui capture (WF_GRAB, la plus en avant), 0 sinon ; une fenêtre sûre ouverte : seulement
 * une fenêtre sûre (la liste déroulante de l'écran de connexion). Sinon, la capture d'une app ne vaut que si
 * elle est au premier plan (son menu ouvert) : une fenêtre de 1 point WF_GRAB ouverte par une app en arrière-plan
 * recevait toutes les frappes destinées aux autres (G3) ; celles du système (panel, menus rapides) valent
 * toujours */
struct win *find_grab(void)
{
	int sec = secure_top() != 0;
	for (long i = win_count; i;) {
		struct win *w = win_z[--i];
		if (w->mini || !(w->flags & WF_GRAB))
			continue;
		if (sec ? w->secure : (w->sysui || !win_focus || win_focus->pid == w->pid))
			return w;
	}
	return 0;
}

#define IN32(in, off) (*(const u32 *)((in) + (off)))
#define IN64(in, off) (*(const u64 *)((in) + (off)))

/* entrée brute */
void in_event(const u8 *in)
{
	u32 type = IN32(in, 0);
	if (type == IN_KEY) {
		cur_mods = (long)IN64(in, 24); /* (modificateurs : aussi pour la souris) */
		if (policy->key(in)) /* touches du mode (poche : Super, Alt+Tab, F11...), avant l'app */
			return;
		if (ov_on && ov_key(in)) /* la vue d'ensemble : Échap, flèches (le reste avalé) */
			return;
		u64 code = IN64(in, 8); /* F10 : tout recomposer ; F11 : état d'Elegant (diagnostic) */
		if (code == 0x43) {
			if (IN64(in, 16))
				dirty_all();
			return;
		}
		if (code == 0x44) {
			if (IN64(in, 16))
				diag_windows();
			return;
		}
		if (desk_key(in)) /* Ctrl+Alt+Gauche, Droite : espace de travail voisin */
			return;
		struct win *t = key_target();
		if (!t)
			return;
		ev_clear();
		M32(evbuf, EV_TYPE) = WEV_KEY;
		M32(evbuf, 4) = (u32)IN64(in, 16); /* appui / relâché */
		M32(evbuf, EV_CODE) = (u32)code;
		M32(evbuf, EV_CHAR) = IN32(in, 4);
		M32(evbuf, EV_MODS) = (u32)IN64(in, 24);
		win_push(t, evbuf); /* (HIDManager répète la touche tenue) */
		return;
	}
	if (type == IN_WHEEL) { /* molette : comme le clavier */
		if (ov_on && !find_grab() && ov_wheel((long)IN64(in, 8))) /* (la vue d'ensemble : le carrousel) */
			return;
		struct win *t = key_target();
		if (!t)
			return;
		ev_clear();
		M32(evbuf, EV_TYPE) = WEV_WHEEL;
		M32(evbuf, 12) = (u32)IN64(in, 8);
		win_push(t, evbuf);
		return;
	}
	if (type == IN_HWHEEL) { /* molette horizontale : seulement le carrousel de la vue d'ensemble */
		if (ov_on && !find_grab())
			ov_hwheel((long)IN64(in, 8));
		return;
	}
	if (type == 17) { /* EV_PAD (inc/pad.inc) : manette, comme le clavier */
		struct win *t = key_target();
		if (!t)
			return;
		ev_clear();
		M32(evbuf, EV_TYPE) = WEV_PAD;
		M32(evbuf, 4) = IN32(in, 4);   /* manette */
		M64(evbuf, 8) = IN64(in, 8);   /* boutons, boutons bruts */
		M64(evbuf, 16) = IN64(in, 16); /* axes, modèle */
		M64(evbuf, 24) = IN64(in, 24);
		win_push(t, evbuf);
		return;
	}
	if (lock_win) { /* souris relative : la fenêtre, si elle a le clavier */
		if (key_target() != lock_win) {
			pointer_unlock();
			return;
		}
		long dx, dy;
		if (type == IN_MOUSE_ABS) {
			long nx = (long)((IN64(in, 8) * (u64)scr_w) >> 15);
			long ny = (long)((IN64(in, 16) * (u64)scr_h) >> 15);
			dx = nx - cur_x;
			dy = ny - cur_y;
			cur_x = nx;
			cur_y = ny;
		} else if (type == IN_MOUSE_REL) {
			dx = (long)IN64(in, 8);
			dy = (long)IN64(in, 16);
		} else {
			return;
		}
		ev_clear();
		M32(evbuf, EV_TYPE) = WEV_MOUSE;
		M32(evbuf, 4) = WEV_M_REL;
		M32(evbuf, EV_X) = (u32)dx;
		M32(evbuf, EV_Y) = (u32)dy;
		u64 b = IN64(in, 24);
		M32(evbuf, 16) = (u32)b;
		cur_buttons = (long)b;
		win_push(lock_win, evbuf);
		return;
	}
	long x, y;
	if (type == IN_MOUSE_ABS) {
		x = (long)((IN64(in, 8) * (u64)scr_w) >> 15);
		y = (long)((IN64(in, 16) * (u64)scr_h) >> 15);
	} else if (type == IN_MOUSE_REL) {
		x = cur_x + (long)IN64(in, 8);
		y = cur_y + (long)IN64(in, 16);
	} else {
		return;
	}
	if (x < 0) /* borner à l'écran */
		x = 0;
	if (y < 0)
		y = 0;
	if (x > scr_w - 1)
		x = scr_w - 1;
	if (y > scr_h - 1)
		y = scr_h - 1;
	mouse(x, y, (long)IN64(in, 24));
}

/* zones salies signalées par les programmes (en-têtes WD_*, ws_ext.inc) : au compositeur ;
 * WEV_FRAME voulu -> framep */
void damage_collect(void)
{
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used)
			continue;
		volatile u64 *d = w->dmg;
		if (d[WD_DAMAGE / 8]) {
			u64 v = __atomic_exchange_n((u64 *)&d[WD_DAMAGE / 8], 0, __ATOMIC_ACQ_REL);
			long x0 = (long)(v & 0xFFFF), y0 = (long)((v >> 16) & 0xFFFF);
			long x1 = (long)((v >> 32) & 0xFFFF), y1 = (long)(v >> 48);
			win_damage(w, x0, y0, x1 - x0, y1 - y0);
			d = w->dmg;
		}
		if (d[WD_FRAME / 8]) {
			d[WD_FRAME / 8] = 0;
			w->framep = 1;
		}
	}
}

/* après compose : WEV_FRAME aux fenêtres qui l'attendent, au plus un tour tous les
 * WS_FRAME_MS -> attente suivante (ms) */
long frames_post(void)
{
	long wait = 8;
	long i;
	for (i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->framep)
			continue;
		if (w->used)
			break;
		w->framep = 0;
	}
	if (i == MAX_WINDOWS)
		return wait;
	u64 now = (u64)korin_sys0(SYS_TIME_MS);
	if (now < (u64)frame_next) { /* trop tôt : on revient à l'heure */
		u64 d = (u64)frame_next - now;
		if (d < (u64)wait)
			wait = (long)d;
		return wait;
	}
	frame_next = (long)(now + WS_FRAME_MS);
	for (i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->framep)
			continue;
		if (w->used) {
			ev_clear();
			M32(evbuf, EV_TYPE) = WEV_FRAME;
			if (win_push(w, evbuf) < 0) /* file pleine : au prochain tour */
				continue;
		}
		w->framep = 0;
	}
	return wait;
}

/* fenêtre, événement (32 o) : déposé dans la file du programme */
long win_push(struct win *w, u8 *ev)
{
	M32(ev, EV_WIN) = (u32)w->id;
	return korin_sys2(SYS_WS_POST, w->pid, ev);
}

static void push_close(struct win *w)
{
	ev_clear();
	M32(evbuf, EV_TYPE) = WEV_CLOSE;
	win_push(w, evbuf);
}

/* la fenêtre qui a reçu le dernier survol : quand le pointeur la quitte (par une autre fenêtre, un cadre, le fond),
 * un dernier déplacement en (-1, -1) lui dit qu'il n'est plus sur elle (sinon un titre de la barre de menus, une
 * icône du dash restaient en surbrillance) */
static struct win *hov_win;

static void hover_to(struct win *w)
{
	struct win *o = hov_win;
	hov_win = w;
	if (!o || o == w)
		return;
	ev_clear();
	M32(evbuf, EV_TYPE) = WEV_MOUSE;
	M32(evbuf, 4) = 0;
	M32(evbuf, EV_X) = (u32)-1;
	M32(evbuf, EV_Y) = (u32)-1;
	M32(evbuf, 16) = (u32)cur_buttons;
	M32(evbuf, EV_MODS) = (u32)cur_mods;
	win_push(o, evbuf);
}

/* -> le nombre de fenêtres qui prennent le clavier (une application, l'installateur de la clé...), sauf w */
static long ov_start_pending; /* 1 : le dash est arrivé devant une fenêtre ; la vue s'ouvrira quand elle se fermera */
static long apps_open(const struct win *w)
{
	long n = 0;
	for (long i = 0; i < win_count; i++)
		if (win_z[i] != w && !win_z[i]->mini &&
		    !(win_z[i]->flags & (WF_OVERVIEW | WF_APPGRID | WF_BACK | WF_HOME | WF_NOFOCUS)))
			n++;
	return n;
}

/* ---- l'ancrage au glisser (25/11/2026, comme GNOME) : le pointeur contre le haut (la barre de menus comprise) :
 * agrandie ; contre le bord gauche ou droit : la moitié de ce côté. Un aperçu translucide montre la place pendant
 * le geste ; au relâché, win_snap (uikit_ops.c). Une fenêtre agrandie ou ancrée qu'on glisse de plus de DRAG_UNSNAP
 * pixels reprend sa taille d'avant, le pointeur à la même place en proportion. Seulement les fenêtres à cadre
 * redimensionnables (ni barres, ni menus, ni dialogues de taille fixe). */
#define DRAG_EDGE 4      /* pixels du bord de l'écran qui ancrent */
#define DRAG_UNSNAP 10
long drag_zone;            /* WR_MAX, WR_LEFT, WR_RIGHT sous le pointeur (0 : aucune) */
static long drag_ox, drag_oy; /* la place de la fenêtre avant le geste */
static long drag_px, drag_py; /* le pointeur à l'appui */
static long drag_unsnap;      /* 1 : agrandie ou ancrée, pas encore rendue */

void drag_begin(struct win *w, long x, long y)
{
	drag_win = w;
	drag_dx[0] = x - w->x;
	drag_dx[1] = y - w->y;
	drag_ox = w->x;
	drag_oy = w->y;
	drag_px = x;
	drag_py = y;
	drag_unsnap = win_restore[w - windows][WR_ON] != 0;
	drag_zone = 0;
}

/* -> la zone d'ancrage sous le pointeur (0 : aucune) */
static long drag_zone_at(struct win *w, long x, long y)
{
	if ((w->flags & (WF_FRAMELESS | WF_RESIZABLE)) != WF_RESIZABLE || (w->flags & WF_MODAL))
		return 0;
	if (x < DRAG_EDGE)
		return WR_LEFT;
	if (x >= scr_w - DRAG_EDGE)
		return WR_RIGHT;
	if (y < area_top + DRAG_EDGE)
		return WR_MAX;
	return 0;
}

/* la zone change : l'ancien aperçu et le nouveau redessinés */
static void drag_zone_set(long z)
{
	if (z == drag_zone)
		return;
	long x, y, wd, ht;
	if (drag_zone) {
		win_zone(drag_zone, &x, &y, &wd, &ht);
		dirty(x, y, wd, ht);
	}
	drag_zone = z;
	if (z) {
		win_zone(z, &x, &y, &wd, &ht);
		dirty(x, y, wd, ht);
	}
}

/* (compose_clip) l'aperçu : un voile clair sur la place, un liseré plus clair, en retrait de 8 px */
void drag_preview(void)
{
	long x, y, wd, ht;
	win_zone(drag_zone, &x, &y, &wd, &ht);
	x += 8;
	y += 8;
	wd -= 16;
	ht -= 16;
	for (long j = 0; j < ht; j++) {
		long yy = y + j;
		if (yy < clip_r.y0 || yy >= clip_r.y1)
			continue;
		u32 *row = (u32 *)((u8 *)bb + yy * bb_pitch);
		long edge_y = j < 2 || j >= ht - 2;
		for (long i = 0; i < wd; i++) {
			long xx = x + i;
			if (xx < clip_r.x0 || xx >= clip_r.x1)
				continue;
			u32 t = edge_y || i < 2 || i >= wd - 2 ? 70 : 165; /* (transparence du blanc : bord, dedans) */
			row[xx] = blend(0x00FFFFFF, row[xx], t);
		}
	}
}

/* x, y, boutons */
void mouse(long x, long y, long buttons)
{
	/* ancien curseur sale (formes : le point chaud jusqu'à 8, 10 dans la forme) */
	pointer_dirty();
	cur_x = x;
	cur_y = y;
	pointer_dirty();
	long old = cur_buttons;
	cur_buttons = buttons;
	long down = buttons & ~old; /* appuyés */
	long up = old & ~buttons;   /* relâchés */

	/* --- un menu ouvert capture toute la souris */
	struct win *g = find_grab();
	if (down) /* (G3) un menu dont la capture ne vaut plus (son app a perdu le premier plan) reçoit quand même le
		   * clic fait hors de lui : il se ferme, comme avant */
		for (long i = win_count; i;) {
			struct win *o = win_z[--i];
			if (o != g && !o->mini && (o->flags & WF_GRAB) &&
			    !(cur_x >= o->x && cur_x < o->x + o->w && cur_y >= o->y && cur_y < o->y + o->h))
				send_mouse(o, 1);
		}
	if (g) {
		send_mouse(g, down ? 1 : up ? 2 : 0);
		return;
	}
	/* --- redimensionnement en cours (bord droit / bas) : au relâché */
	if (resize_win) {
		if (!(buttons & 1))
			resize_end(x, y);
		return;
	}
	/* --- déplacement d'une fenêtre en cours (et l'ancrage aux bords : drag_zone) */
	if (drag_win) {
		struct win *w = drag_win;
		if (!(buttons & 1)) {
			long z = drag_zone;
			drag_zone_set(0);
			drag_win = 0;
			if (z)
				win_snap(w, z, drag_ox, drag_oy);
			return;
		}
		if (drag_unsnap) { /* agrandie ou ancrée : rendue à sa taille d'avant dès que le pointeur s'éloigne */
			if (x - drag_px < DRAG_UNSNAP && drag_px - x < DRAG_UNSNAP && y - drag_py < DRAG_UNSNAP && drag_py - y < DRAG_UNSNAP)
				return;
			drag_unsnap = 0;
			long *r = win_restore[w - windows], ow = w->w > 0 ? w->w : 1, rw = r[WR_W];
			long dx = drag_dx[0] * rw / ow; /* (le pointeur garde sa place, en proportion, dans la largeur) */
			if (dx > rw - 40)
				dx = rw - 40;
			if (dx < 40)
				dx = rw < 80 ? rw / 2 : 40;
			drag_dx[0] = dx;
			drag_ox = x - dx; /* (sa place d'avant, si on l'ancre de nouveau) */
			drag_oy = y - drag_dx[1];
			win_unsnap(w);
		}
		dirty_window(w);
		long nx = x - drag_dx[0], ny = y - drag_dx[1];
		if (ny < area_top) /* le haut du contenu : sous la barre de menus (l'ombre passe dessous) */
			ny = area_top;
		w->x = nx;
		w->y = ny;
		dirty_window(w);
		drag_zone_set(drag_zone_at(w, x, y));
		return;
	}
	/* --- capture : boutons tenus après un clic dans un contenu */
	if (capture_win) {
		send_mouse(capture_win, up ? 2 : 0); /* déplacement, relâché */
		if (buttons)
			return;
		capture_win = 0;
		drag_drop(x, y); /* glisser-déposer : la fenêtre dessous */
		return;
	}
	/* --- la vue d'ensemble : ce qui n'est ni une barre, ni le dash, ni un menu */
	if (ov_on && ov_mouse(x, y, down, up)) {
		if (!down)
			hover_to(0);
		return;
	}
	if (!down) { /* survol : le mode, puis le contenu sous le pointeur (fenêtre active, ou sans
			focus : dock, barre de menus) */
		long zone;
		struct win *w = policy->hover(x, y) ? 0 : win_hit(x, y, &zone);
		if (w && (zone != Z_CLIENT || (!(w->flags & WF_NOFOCUS) && w != win_focus)))
			w = 0;
		hover_to(w);
		if (w)
			send_mouse(w, 0);
		return;
	}
	if ((down & 1) && policy->click(x, y)) /* bouton gauche : le mode, puis les fenêtres */
		return;
	long zone;
	struct win *w = win_hit(x, y, &zone);
	if (!w) {
		win_set_focus(0);
		return;
	}
	if (w->flags & WF_NOFOCUS) { /* sans focus (barre de menus, dock) : le clic va au contenu,
					sans changer d'ordre */
		if (zone != Z_CLIENT)
			return;
		goto client;
	}
	/* bloquée par un dialogue de son programme : elle passe devant, le dialogue par-dessus, et
	   le clic s'arrête là */
	struct win *blk = win_blocker(w);
	if (blk) {
		win_stack_top(w);
		dirty_window(w);
		win_raise(blk);
		return;
	}
	win_raise(w);
	if (zone == Z_CLIENT)
		goto client;
	if (!(down & 1)) /* cadre : bouton gauche seulement */
		return;
	if (policy->flags & POLF_POCKET) { /* mode poche : ni déplacée, ni redimensionnée ;
					      réduire montre l'accueil */
		if (zone == Z_CLOSE)
			push_close(w);
		else if (zone == Z_MINI)
			pk_home_cmd();
		return;
	}
	switch (zone) {
	case Z_TITLE:
		drag_begin(w, x, y);
		return;
	case Z_MAXI:
		win_maximize(w);
		return;
	case Z_BORDER: /* bord d'une fenêtre redimensionnable */
		if (w->flags & WF_RESIZABLE)
			resize_begin(w, x, y);
		return;
	case Z_CLOSE:
		push_close(w);
		return;
	case Z_MINI:
		win_minimize(w);
		return;
	}
	return;
client:
	capture_win = w;
	send_mouse(w, 1);
}

/* fenêtre, genre (0 déplacement, 1 appui, 2 relâché) ; position et boutons : cur_x, cur_y,
 * cur_buttons */
void send_mouse(struct win *w, long kind)
{
	ev_clear();
	M32(evbuf, EV_TYPE) = WEV_MOUSE;
	M32(evbuf, 4) = (u32)kind;
	M32(evbuf, EV_X) = (u32)(cur_x - w->x);
	M32(evbuf, EV_Y) = (u32)(cur_y - w->y);
	M32(evbuf, 16) = (u32)cur_buttons;
	M32(evbuf, EV_MODS) = (u32)cur_mods; /* Maj, Ctrl tenus */
	win_push(w, evbuf);
}

/* x, y -> fenêtre la plus en avant sous le point (0 : aucune), *zone (Z_*) */
struct win *win_hit(long x, long y, long *zone)
{
	for (long layer = WIN_LAYERS - 1; layer >= 0; layer--) { /* couches, du dessus vers le fond */
		for (long i = win_count; i;) {
			struct win *w = win_z[--i];
			if (w->mini || win_layer(w) != layer)
				continue;
			if (ov_on && !ov_top(w)) /* (la vue d'ensemble : les bureaux sont à elle) */
				continue;
			long ox, oy, ow, oh;
			win_outer(w, &ox, &oy, &ow, &oh);
			if (x < ox || x >= ox + ow || y < oy || y >= oy + oh)
				continue;
			u64 f = w->flags; /* dedans : quelle zone ? */
			if (f & WF_FRAMELESS) {
				*zone = Z_CLIENT;
				return w;
			}
			long X = w->x, Y = w->y, W = w->w, H = w->h;
			if (x < X - FRAME_HIT || x >= X + W + FRAME_HIT || y < Y - FRAME_HIT ||
			    y >= Y + H + FRAME_HIT) /* (l'ombre laisse passer) */
				continue;
			if (y < Y) {
				*zone = Z_TITLE;
				return w;
			}
			if (x < X || x >= X + W || y >= Y + H) {
				*zone = Z_BORDER;
				return w;
			}
			*zone = Z_CLIENT; /* boutons du cadre (depuis la droite) */
			if ((u64)(y - Y - FRAME_CAP_MARGIN) >= FRAME_CAP_H)
				return w;
			long d = (W - FRAME_CAP_MARGIN) - (x - X); /* 1.. depuis leur bord droit */
			if (d <= 0 || d > FRAME_CAP_W)
				return w;
			u64 b = (u64)(d - 1) / FRAME_CAP_BTN;
			if (!b) {
				*zone = Z_CLOSE;
			} else if (policy->flags & POLF_POCKET) { /* mode poche : le deuxième est réduire
								    (l'accueil), le reste poignée */
				*zone = b != 1 || (f & WF_MODAL) ? Z_TITLE : Z_MINI;
			} else if (b > 2) {
				*zone = Z_TITLE; /* (une poignée invisible à leur gauche) */
			} else if (b == 2) {
				*zone = f & WF_MODAL ? Z_TITLE : Z_MINI;
			} else {
				*zone = f & WF_RESIZABLE ? Z_MAXI : Z_TITLE;
			}
			return w;
		}
	}
	*zone = 0;
	return 0;
}

/* fenêtre -> dialogue modal qui la bloque (0 : aucun) : le plus en avant de son programme, sauf
 * si c'est elle */
struct win *win_blocker(struct win *w)
{
	for (long i = win_count; i;) {
		struct win *o = win_z[--i];
		if (o->pid != w->pid || o->mini || !(o->flags & WF_MODAL))
			continue;
		return o == w ? 0 : o;
	}
	return 0;
}

/* fenêtre : au sommet de la pile (de sa couche à l'affichage), focus si elle le prend */
void win_raise(struct win *w)
{
	win_stack_top(w);
	win_set_focus(w);
	dirty_window(w);
	wins_changed();
}

/* fenêtre : au sommet de la pile, sans toucher au focus */
void win_stack_top(struct win *w)
{
	long n = win_count, i;
	for (i = 0; i < n; i++)
		if (win_z[i] == w)
			break;
	if (i == n)
		return;
	for (; i + 1 < n; i++) /* décaler les suivantes */
		win_z[i] = win_z[i + 1];
	win_z[i] = w;
}

/* fenêtre (0 : aucune) : focus clavier, événements WEV_FOCUS (une fenêtre sans focus est
 * ignorée) */
void win_set_focus(struct win *w)
{
	if (w && (w->flags & WF_NOFOCUS))
		return;
	struct win *old = win_focus;
	if (old == w)
		return;
	win_focus = w;
	if (old) {
		ev_clear();
		M32(evbuf, EV_TYPE) = WEV_FOCUS;
		win_push(old, evbuf);
		dirty_window(old);
	}
	if (w) {
		ev_clear();
		M32(evbuf, EV_TYPE) = WEV_FOCUS;
		M32(evbuf, 4) = 1;
		win_push(w, evbuf);
		dirty_window(w);
	}
	wins_changed();
}

/* le focus va à la fenêtre normale visible la plus en avant (ou à aucune) */
void focus_top(void)
{
	for (long i = win_count; i;) {
		struct win *w = win_z[--i];
		if (w->mini || (w->flags & (WF_NOFOCUS | WF_BACK | WF_TOP)))
			continue;
		win_set_focus(w);
		return;
	}
	win_set_focus(0);
}

/* fenêtre : réduite (cachée ; le dock la fait revenir) */
void win_minimize(struct win *w)
{
	w->mini |= MINI_REDUCED;
	dirty_window(w);
	if (win_focus == w)
		focus_top();
	wins_changed();
}

/* fenêtre : retirée de la pile d'affichage (la place reste à libérer) */
void win_unlink(struct win *w)
{
	dirty_window(w);
	long n = win_count;
	for (long i = 0; i < n; i++) {
		if (win_z[i] != w)
			continue;
		for (; i + 1 < n; i++)
			win_z[i] = win_z[i + 1];
		win_count = n - 1;
		break;
	}
	if (win_focus == w) { /* focus à la fenêtre du dessus */
		win_focus = 0;
		focus_top();
	}
	if (capture_win == w)
		capture_win = 0;
	if (drag_win == w)
		drag_win = 0;
	if (hov_win == w)
		hov_win = 0;
	if (ov_start_pending && !ov_on && !apps_open(w)) { /* (la dernière fenêtre d'avant le dash : la vue d'ensemble) */
		ov_start_pending = 0;
		ov_open();
	}
	wins_changed();
}

/* fenêtre : retirée, tampon rendu, place libérée */
void win_destroy(struct win *w)
{
	policy->gone(w); /* (le mode : avant qu'elle parte) */
	ov_gone(w); /* (sa vignette dans la vue d'ensemble) */
	if (lock_win == w)
		pointer_unlock();
	win_unlink(w);
	surf_detach(w); /* (sa surface attachée) */
	korin_sys1(SYS_BUF_FREE, w->buf);
	/* (plus agrandie : la fenêtre qui reprendra cette place n'est pas à « rendre ») */
	win_restore[win_index(w)][4] = 0;
	w->used = 0;
	restore_clear(w); /* (la place ne sera plus agrandie) */
	recompute_reserve();
	if (w->flags & WF_CURTAIN) /* (programme mort compris : le rideau tombe) */
		curtain_apply();
}

/* bords réservés (area_top, area_bot) : fenêtres WF_RESERVE collées en haut ou en bas de
 * l'écran */
void recompute_reserve(void)
{
	long top = 0, bot = 0;
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || w->mini || !(w->flags & WF_RESERVE)) /* (cachée : la barre masquée par F11) */
			continue;
		if (!w->y) {
			if (w->h > top)
				top = w->h;
		} else if (w->y + w->h >= scr_h) {
			if (w->h > bot)
				bot = w->h;
		}
	}
	area_top = top;
	area_bot = bot;
	/* les fenêtres normales déjà placées sous la barre du haut descendent (programmes ouverts
	   avant la barre de menus) */
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || (w->flags & (WF_BACK | WF_TOP | WF_FRAMELESS)))
			continue;
		if (w->y >= top + FRAME_TITLE_H)
			continue;
		if (win_restore[i][4]) /* (une fenêtre agrandie garde sa place : elle commence juste
					  sous la barre) */
			continue;
		w->y = top + FRAME_TITLE_H;
		need_all = 1; /* tout est à recomposer */
	}
}

/* id, pid (-1 : quelconque) -> fenêtre (0 : aucune) */
struct win *win_by_id(u64 id, long pid)
{
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || w->id != id)
			continue;
		if (pid == -1 || (long)w->pid == pid)
			return w;
		return 0; /* (comme l'asm : la première du même id décide) */
	}
	return 0;
}

/* destination, titre, taille max (NUL compris) : copie bornée */
void copy_title(char *d, const char *s, long max)
{
	long n = max - 1;
	for (long i = 0; i < n; i++) {
		d[i] = s[i];
		if (!s[i])
			return;
	}
	d[n] = 0;
}

/* prévient les fenêtres à l'écoute (WF_LISTEN) si la liste a changé */
void notify_listeners(void)
{
	if (!wins_dirty)
		return;
	wins_dirty = 0;
	policy->wins();
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || !(w->flags & WF_LISTEN))
			continue;
		ev_clear();
		M32(evbuf, EV_TYPE) = WEV_WINLIST;
		win_push(w, evbuf);
	}
}

/* ---------------------------------------------------------------------------
 *  Requêtes des programmes (message dans msg)
 * --------------------------------------------------------------------------- */

/* numéro de requête : traite, puis répond (sauf messages du noyau) */
void req_handle(long n)
{
	hist((long)M64(msg, WM_OP) + 1000, (long)M64(msg, WM_A), (long)M64(msg, WM_B), /* (historique) */
	     (long)M64(msg, WM_C), (long)M32(msg, WM_PID));
	long r;
	switch (M64(msg, WM_OP)) {
	case WS_OP_CREATE: r = req_create(); break;
	case WS_OP_UPDATE: r = req_update(); break;
	case WS_OP_CLOSE: r = req_close(); break;
	case WS_OP_TITLE: r = req_title(); break;
	case WS_OP_SCREEN:
		M64(msg, WM_A) = (u64)scr_w; /* largeur, hauteur */
		M64(msg, WM_B) = (u64)scr_h;
		M64(msg, WM_C) = (u64)area_bot; /* réservé en bas */
		M64(msg, WM_D) = (u64)area_top; /* en haut */
		r = 0;
		break;
	case WS_OP_FRAME: r = req_frame(); break;
	case WS_OP_LIST: r = req_list(); break;
	case WS_OP_RAISE: r = req_raise(); break;
	case WS_OP_MESURE: r = req_measure(); break;
	case WS_OP_MENU_SET: r = req_menu_set(); break; /* menus (menu.c) */
	case WS_OP_MENU_GET: r = req_menu_get(); break;
	case WS_OP_MENU_COMMAND: r = req_menu_command(); break;
	case WS_OP_SNAPSHOT: r = req_snapshot(); break;
	case WS_OP_WIN_LIST: r = req_win_list(); break; /* rdpd (remote.c) */
	case WS_OP_WIN_READ: r = req_win_read(); break;
	case WS_OP_WIN_MOVE: r = req_win_move(); break;
	case WS_OP_WIN_CLOSE: r = req_win_close(); break;
	case WS_OP_CLIP_SET: r = req_clip_set(); break; /* presse-papiers (uikit_ops.c) */
	case WS_OP_CLIP_GET: r = req_clip_get(); break;
	case WS_OP_WIN_POS: r = req_win_pos(); break;
	case WS_OP_RESIZE: r = req_resize(); break;
	case WS_OP_CURSOR: r = req_cursor(); break;
	case WS_OP_CURSOR_GET: r = req_cursor_get(); break;
	case WS_OP_POINTER_LOCK: r = req_pointer_lock(); break;
	case WS_OP_DRAG: r = req_drag(); break;
	case WS_OP_DRAG_IMAGE: r = req_drag_image(); break;
	case WS_OP_DROP_GET: r = req_drop_get(); break;
	case WS_OP_SURFACE_ATTACH: r = req_surface_attach(); break; /* (surface.c) */
	case WS_OP_SURFACE_DAMAGE: r = req_surface_damage(); break;
	case WS_OP_SURFACE_OVER: r = req_surface_over(); break;
	case WS_OP_MAXIMIZE: r = req_maximize(); break;
	case WS_OP_SCREEN_SET: r = req_screen_set(); break; /* (screen.c) */
	case WS_OP_SCREEN_KEEP: r = req_screen_keep(); break;
	case WS_OP_SCREEN_MODES: r = req_screen_modes(); break;
	case WS_OP_WIN_PLACE: r = req_win_place(); break;
	case WS_OP_WIN_TOP: r = req_win_top(); break;
	case WS_OP_SESSION_END: r = req_session_end(); break; /* (desktop.c) */
	case WS_OP_DESK: r = req_desk(); break;
	case WS_OP_WIN_DRAG: r = req_win_drag(); break;
	case WS_OP_MINIMIZE: r = req_minimize(); break;
	case WS_OP_WIN_THUMB: r = req_win_thumb(); break; /* vignette (snapshot.c) */
	case WS_OP_OVERVIEW: r = req_overview(); break; /* vue d'ensemble (overview.c) */
	case WS_OP_EXIT: /* fin d'un process : ses fenêtres (le noyau n'attend pas de réponse) */
		req_exit((long)M64(msg, WM_A));
		return;
	default: r = E_INVAL; break;
	}
	M64(msg, WM_OP) = (u64)r;
	korin_sys2(SYS_WS_REPLY, n, msg);
}

/* adresse, octets (multiple de 8 ; sinon arrondi au-dessus, comme l'asm), motif de 64 bits */
void fill64(void *a, long bytes, u64 pattern)
{
	u64u *p = a;
	if (!bytes)
		return;
	do
		*p++ = pattern;
	while ((bytes -= 8) > 0);
}

/* WS_OP_CREATE : a x, b y (-1 : auto), c largeur, d hauteur ; données : rôle, titre (+8) -> id
 * (< 0 : erreur) ; msg a = tampon, b / c = décalages des deux copies du cadre dans le tampon (0 :
 * sans cadre), d = l'en-tête WD_* */
long req_create(void)
{
	long x = (long)M64(msg, WM_A), y = (long)M64(msg, WM_B);
	long cw = (long)M64(msg, WM_C), ch = (long)M64(msg, WM_D);
	u64 role = M64(msg, WM_DATA);
	/* une fenêtre plus grande que l'écran est acceptée (découpée à l'affichage) */
	if (cw < 1 || ch < 1 || cw > 2048 || ch > 2048)
		return E_INVAL;
	struct win *w = 0; /* place libre */
	for (long i = 0; i < MAX_WINDOWS; i++)
		if (!windows[i].used) {
			w = &windows[i];
			break;
		}
	if (!w)
		return E_NOMEM;
	u64 content = (u64)(cw * ch) * 4;
	u64 frame = 0; /* un cadre (deux copies) */
	if (!(role & WF_FRAMELESS))
		frame = (u64)((cw + 2 * FRAME_BORDER) * (ch + FRAME_TITLE_H + FRAME_BORDER)) * 4;
	if (content + 2 * frame + WD_SIZE + 7 > BUF_SLOT) /* en-tête WD_* en fin de tampon */
		return E_INVAL;
	long buf = korin_sys1(SYS_BUF_CREATE, content + 2 * frame + WD_SIZE + 7);
	if (buf < 0)
		return buf;
	u8 *va = (u8 *)(BUF_VA_BASE + (u64)buf * BUF_SLOT);
	const u64 transp = (u64)TRANSPARENT | (u64)TRANSPARENT << 32;
	fill64(va, (long)content, role & WF_ALPHA ? transp : ~0ULL); /* contenu blanc (transparent :
									  WF_ALPHA) */
	fill64(va + content, (long)(2 * frame), transp); /* cadres invisibles jusqu'au dessin */
	u64 pid = M32(msg, WM_PID);
	korin_sys2(SYS_BUF_GRANT, buf, pid); /* le programme pourra mapper le tampon */
	w->pid = pid;
	w->w = cw;
	w->h = ch;
	surf_clear(w); /* pas de surface attachée */
	w->surf = (u32 *)va;
	w->buf = (u64)buf;
	w->dmg = (u64 *)(va + ((content + 2 * frame + 7) & ~7ULL));
	w->framep = 0;
	w->flags = role;
	w->secure = (u64)ws_secure_caller();
	w->sysui = w->secure || ((role & WF_GRAB) && ws_sysui_caller()); /* (seulement utile avec WF_GRAB) */
	w->mini = ((role & (WF_OVERVIEW | WF_APPGRID)) && !ov_on) ? MINI_OVERVIEW : 0; /* (le dash, la grille :
										   seulement dans la vue) */
	w->desk = role & (WF_BACK | WF_TOP) ? 0 : (u64)desk_cur + 1; /* sur l'espace courant ; fond et
									  dessus : sur tous */
	w->mlen = 0; /* pas de menu */
	w->mgen = 0;
	w->gen = 0; /* générations (contenu, cadre : un seul mot de 64 bits dans l'asm) */
	w->cgen = 0;
	w->chr_a = (u32 *)(va + content);
	w->chr_i = (u32 *)(va + content + frame);
	copy_title(w->title, (const char *)msg + WM_DATA + 8, 120);
	if (role & WF_FRAMELESS) { /* sans cadre : à la place demandée */
		if (x == -1)
			x = 0;
		if (y == -1)
			y = 0;
	} else {
		if (x == -1) { /* position : demandée, sinon en cascade */
			long c = win_cascade + 30;
			if (c > 240)
				c = 30;
			win_cascade = c;
			x = c + 40;
			y = c - 20 + area_top + FRAME_GAP;
		}
		long mx = scr_w - FRAME_GAP - cw; /* dans l'écran autant que possible */
		if (x > mx)
			x = mx;
		if (x < FRAME_GAP)
			x = FRAME_GAP;
		long my = scr_h - area_bot - FRAME_GAP - ch; /* entre les bords réservés, barre de titre
								visible */
		if (y > my)
			y = my;
		if (y < area_top + FRAME_GAP)
			y = area_top + FRAME_GAP;
	}
	w->x = x;
	w->y = y;
	w->id = (u64)++next_win_id;
	w->used = 1;
	if (role & WF_RESERVE) /* (les fenêtres agrandies suivent) */
		reserve_update();
	win_z[win_count++] = w; /* au sommet de la pile */
	win_raise(w);
	if ((role & WF_BACK) && !(role & WF_ALPHA)) { /* le fond d'écran (WF_BACK opaque) : sous les
							 applets, quel que soit l'ordre de démarrage */
		for (long i = win_count - 1; i > 0; i--)
			win_z[i] = win_z[i - 1];
		win_z[0] = w;
	}
	if ((role & WF_CURTAIN) || curtain_on) /* rideau ouvert : la nouvelle dessous */
		curtain_apply();
	policy->place(w); /* le mode la place (poche : plein écran) */
	if (ov_on && win_layer(w) == 1) /* une application s'ouvre : retour à la vue normale */
		ov_close(desk_cur, w);
	if ((role & WF_APPGRID) && !ov_on) /* la grille des applications : dans la vue d'ensemble */
		ov_open();
	if ((role & WF_OVERVIEW) && ov_enabled && !ov_on) { /* le dash arrive (une session commence) : sans application
							     ouverte, la vue d'ensemble d'abord, pour lancer la première
							     (demande de l'utilisateur, 23/11/2026) ; sinon (l'installateur
							     de la clé), quand la dernière se ferme */
		if (!apps_open(w))
			ov_open();
		else
			ov_start_pending = 1;
	} else if (!(role & (WF_OVERVIEW | WF_APPGRID | WF_BACK | WF_HOME | WF_NOFOCUS)))
		ov_start_pending = 0; /* (une application s'ouvre : on y travaille déjà) */
	M64(msg, WM_A) = (u64)buf;
	M64(msg, WM_B) = frame ? content : 0;
	M64(msg, WM_C) = frame ? content + frame : 0;
	M64(msg, WM_D) = (u64)((u8 *)w->dmg - va); /* l'en-tête WD_* */
	return (long)w->id;
}

/* -> fenêtre de l'appelant désignée par msg a (0 : aucune) */
struct win *req_win(void)
{
	return win_by_id(M64(msg, WM_A), (long)M32(msg, WM_PID));
}

/* WS_OP_UPDATE : a id, b x, c y, d largeur, données : hauteur */
long req_update(void)
{
	struct win *w = req_win();
	if (!w) {
		diag_bad++; /* (diagnostic) */
		return E_BADF;
	}
	win_damage(w, (long)M64(msg, WM_B), (long)M64(msg, WM_C), (long)M64(msg, WM_D),
		   (long)M64(msg, WM_DATA));
	return 0;
}

/* fenêtre, x, y, largeur, hauteur dans le contenu : zone salie */
void win_damage(struct win *w, long x, long y, long wd, long ht)
{
	w->nupd++;
	w->gen++; /* (rdpd : le contenu a changé) */
	if (x < 0) /* borner au contenu */
		x = 0;
	if (y < 0)
		y = 0;
	long x1 = x + wd, y1 = y + ht;
	if (x1 > w->w)
		x1 = w->w;
	if (y1 > w->h)
		y1 = w->h;
	wd = x1 - x;
	ht = y1 - y;
	long sx = w->x + x, sy = w->y + y;
	hist(200, sx, sy, wd, ht); /* (historique : zone salie) */
	dirty(sx, sy, wd, ht);
}

/* WS_OP_CLOSE : a id */
long req_close(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	win_destroy(w);
	return 0;
}

/* WS_OP_TITLE : a id, titre */
long req_title(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	copy_title(w->title, (const char *)msg + WM_DATA, 120);
	menu_title_changed(w); /* (le nom de l'appli dans la barre) */
	wins_changed();
	return 0;
}

/* WS_OP_FRAME : a id (le programme a redessiné son cadre) */
long req_frame(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	w->cgen++; /* (rdpd : le cadre a changé) */
	dirty_window(w);
	return 0;
}

/* WS_OP_RAISE : a id (fenêtre de n'importe quel programme : le dock) */
long req_raise(void)
{
	struct win *w = win_by_id(M64(msg, WM_A), -1);
	if (!w)
		return E_BADF;
	if (ov_on) { /* (la vue d'ensemble : on revient, sur son bureau, elle devant) */
		ov_close(w->desk ? (long)w->desk - 1 : -1, w);
		return 0;
	}
	if (w->mini & MINI_OFFDESK) /* sur un autre espace : on y va */
		desk_show((long)w->desk - 1);
	w->mini &= (u64)MINI_OVERVIEW; /* (le dash reste caché hors de la vue) */
	struct win *b = win_blocker(w); /* (bloquée : son dialogue vient devant) */
	win_raise(b ? b : w);
	policy->raise(w); /* (le mode : poche, l'accueil s'efface) */
	return 0;
}

/* WS_OP_LIST : a première -> nombre de fenêtres listées ; données : WL_PER_MSG entrées à partir
 * de la première (fenêtres normales, hors WF_NOLIST, dans l'ordre des places) */
long req_list(void)
{
	u64 first = M64(msg, WM_A);
	u8 *e = msg + WM_DATA; /* entrée suivante */
	long n = 0;
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || (w->flags & (WF_NOLIST | WF_BACK | WF_TOP | WF_HOME)))
			continue;
		u64 k = (u64)n - first; /* dans la page demandée ? */
		n++;
		if (k >= WL_PER_MSG)
			continue;
		M64(e, WL_ID) = w->id;
		M32(e, WL_PID) = (u32)w->pid;
		M32(e, WL_STATE) = ((u32)w->mini << 1) + (win_focus == w);
		copy_title((char *)e + WL_TITLE, w->title, WL_SIZE - WL_TITLE);
		e += WL_SIZE;
	}
	return n;
}

/* WS_OP_MESURE : a images, b = 1 sans transparence -> ms pour les a recompositions de tout
 * l'écran (coût des ombres sur le vrai matériel) */
long req_measure(void)
{
	u64 n = M64(msg, WM_A);
	measure_opaque = M64(msg, WM_B) != 0;
	long t0 = korin_sys0(SYS_TIME_MS);
	for (; n; n--) {
		dirty_all();
		compose();
	}
	long dt = korin_sys0(SYS_TIME_MS) - t0;
	measure_opaque = 0;
	dirty_all();
	return dt;
}

/* pid terminé : ses fenêtres disparaissent */
void req_exit(long pid)
{
	snap_exit(pid); /* (son tampon de l'écran) */
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (w->used && (long)w->pid == pid)
			win_destroy(w);
	}
}

/* (F11) diagnostic : chaque fenêtre, la pile, la zone sale */
void diag_windows(void)
{
	static const char dg_t[] = "\n=== Elegant (F11) ===\n";
	static const char dg_sp[] = " ", dg_ti[] = " : ", dg_nl[] = "\n";
	ak_puts(dg_t);
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used)
			continue;
		ak_puts("fenetre ");
		print_dec((long)w->id);
		ak_puts(" pid ");
		print_dec((long)w->pid);
		ak_puts(" role ");
		print_dec((long)w->flags);
		ak_puts(" x,y,l,h ");
		print_dec(w->x);
		ak_puts(dg_sp);
		print_dec(w->y);
		ak_puts(dg_sp);
		print_dec(w->w);
		ak_puts(dg_sp);
		print_dec(w->h);
		ak_puts(" reduite ");
		print_dec((long)w->mini);
		ak_puts(" maj ");
		print_dec((long)w->nupd);
		ak_puts(" pixels "); /* un pixel du contenu (milieu de la première ligne, puis +10 au
					milieu) */
		print_dec((long)w->surf[(u64)w->w >> 1]);
		ak_puts(dg_sp);
		print_dec((long)w->surf[((u64)w->h >> 1) * (u64)w->w + 10]);
		ak_puts(" tampon ");
		print_dec((long)w->buf);
		ak_puts(dg_ti);
		ak_puts(w->title);
		ak_puts(dg_nl);
	}
	ak_puts("pile : "); /* pile, mises à jour refusées */
	print_dec(win_count);
	ak_puts(" ; mises a jour refusees : ");
	print_dec(diag_bad);
	ak_puts(dg_nl);
	present_diag(); /* présentation, zones sales */
	u64 n = (u64)hist_n; /* historique : les 64 dernières entrées */
	for (u64 i = n < 64 ? 0 : n - 64; i < n; i++) {
		print_dec((long)i);
		ak_puts(dg_ti);
		for (long k = 0; k < 5; k++) {
			print_dec((long)hist_buf[i & 63][k]);
			ak_puts(dg_sp);
		}
		ak_puts(dg_nl);
	}
}
