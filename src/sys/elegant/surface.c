/* surface.c : surface attachée à une fenêtre (docs/DESIGN-ELEGANT.md 17 ; surface.inc) : le
 * programme crée un tampon partagé (BUF_CREATE), dit à Elegant que le contenu de sa fenêtre (ou
 * une zone de ce contenu) est ce tampon (WS_OP_SURFACE_ATTACH), puis signale les rectangles
 * changés (WS_OP_SURFACE_DAMAGE). Elegant compose directement depuis la surface (XRGB8888 ou
 * RGB565), sans recopie par le programme dans le tampon de la fenêtre : lecteur vidéo,
 * émulateurs, programmes SDL portés, Jet.
 *
 * Une entrée par place de fenêtre (surfaces, indexée comme windows). Le tampon est mappé chez
 * Elegant (BUF_MAP : le programme l'y autorise par BUF_GRANT) et rendu (BUF_FREE) au
 * détachement ou à la fin de la fenêtre.
 *
 * Des zones du contenu peuvent passer par-dessus la surface (WS_OP_SURFACE_OVER, docs/DESIGN-CAO.md
 * §11) : les widgets d'UIKit d'une vue GL ou vidéo (panneaux flottants, cotes, boutons de la vue).
 * Elles appartiennent à la fenêtre et restent quand la surface est rattachée (le GL le fait à
 * chaque image) ; leurs pixels viennent du tampon de la fenêtre, avec leur transparence. */
#include "elegant.h"

#define SF_MAX_SIDE 4096

/* une surface (SF_* de l'asm, 64 octets) */
struct surf {
	u8 *addr;   /* premier pixel chez Elegant (0 : pas de surface) */
	u64 buf;    /* numéro du tampon partagé */
	u32 x, y;   /* zone dans le contenu */
	u32 w, h;
	u64 pitch;  /* octets par ligne */
	u64 fmt;    /* WS_SF_* */
	u64 pad[2];
};
_Static_assert(sizeof(struct surf) == 64, "SF_SIZE");

/* surfaces attachées, par place (segment de elegant.asm) */
static struct surf surfaces[MAX_WINDOWS];

/* zones du contenu par-dessus la surface, par place : x, y, largeur, hauteur */
struct over {
	u32 n;
	u16 r[WS_OVER_MAX][4];
};
static struct over overs[MAX_WINDOWS];

/* -> son entrée dans surfaces */
static struct surf *surf_entry(const struct win *w)
{
	return &surfaces[w - windows];
}

/* son entrée remise à zéro (fenêtre neuve, sans surface) */
void surf_clear(struct win *w)
{
	struct surf *s = surf_entry(w);
	u64 *p = (u64 *)s;
	for (int i = 0; i < 8; i++)
		p[i] = 0;
	overs[w - windows].n = 0; /* (ni zones par-dessus) */
}

/* détache sa surface ; le tampon est démappé chez Elegant s'il ne sert plus à aucune autre
 * fenêtre */
void surf_detach(struct win *w)
{
	struct surf *s = surf_entry(w);
	if (!s->addr)
		return;
	u64 b = s->buf;
	s->addr = 0;
	for (int i = 0; i < MAX_WINDOWS; i++) /* encore attaché ailleurs ? */
		if (surfaces[i].addr && surfaces[i].buf == b)
			return;
	korin_sys1(SYS_BUF_FREE, b);
}

/* -> entrée ; *vw, *vh = largeur, hauteur visibles de la surface (zone bornée au contenu ;
 * <= 0 : rien à montrer) */
static struct surf *surf_visible(const struct win *w, long *vw, long *vh)
{
	struct surf *s = surf_entry(w);
	long aw = w->w - (long)s->x, ah = w->h - (long)s->y;
	*vw = (long)s->w > aw ? aw : (long)s->w;
	*vh = (long)s->h > ah ? ah : (long)s->h;
	return s;
}

/* -> la fenêtre la plus en avant dont la surface couvre toute la zone clip_r (0 : aucune) : le
 * fond et ce qui est dessous n'ont pas à être dessinés, ni son cadre ni son contenu
 * (draw_framed ne fait que la surface et les boutons) */
struct win *surf_cover(void)
{
	struct win *best = 0;
	long n = win_count;
	for (long layer = 0; layer < WIN_LAYERS; layer++)
		for (long i = 0; i < n; i++) {
			struct win *w = win_z[i];
			if (w->mini || win_layer(w) != layer)
				continue;
			long vw, vh;
			struct surf *s = surf_visible(w, &vw, &vh);
			if (!s->addr || vw <= 0 || vh <= 0)
				continue;
			long x0 = w->x + (long)s->x, y0 = w->y + (long)s->y; /* la surface à l'écran */
			if (clip_r.x0 < x0 || clip_r.y0 < y0 || clip_r.x1 > x0 + vw || clip_r.y1 > y0 + vh)
				continue;
			best = w;
		}
	return best;
}

/* ox, oy = origine de son contenu (coordonnées du tampon arrière) : dessine sa surface,
 * découpée par le clip */
void draw_surface(struct win *w, long ox, long oy)
{
	long vw, vh;
	struct surf *s = surf_visible(w, &vw, &vh);
	if (!s->addr || vw <= 0 || vh <= 0)
		return;
	long x = (long)s->x + ox, y = (long)s->y + oy;
	if (!s->fmt)
		bb_blit((const u32 *)s->addr, (long)s->pitch, x, y, vw, vh); /* XRGB8888 : opaque */
	else
		bb_blit565((const u16 *)s->addr, (long)s->pitch, x, y, vw, vh);
	const struct over *o = &overs[w - windows]; /* les zones du contenu par-dessus */
	for (u32 i = 0; i < o->n; i++) {
		long rx = o->r[i][0], ry = o->r[i][1], rw = o->r[i][2], rh = o->r[i][3];
		long sx0 = (long)s->x, sy0 = (long)s->y; /* bornées à la surface visible */
		if (rx < sx0) {
			rw -= sx0 - rx;
			rx = sx0;
		}
		if (ry < sy0) {
			rh -= sy0 - ry;
			ry = sy0;
		}
		if (rx + rw > sx0 + vw)
			rw = sx0 + vw - rx;
		if (ry + rh > sy0 + vh)
			rh = sy0 + vh - ry;
		if (rw <= 0 || rh <= 0)
			continue;
		bb_blit_any(w->surf + ry * w->w + rx, w->w * 4, ox + rx, oy + ry, rw, rh, 1);
	}
}

/* WS_OP_SURFACE_OVER : a id, b nombre (0 : aucune) ; données : x, y, largeur, hauteur (16 bits
 * chacun) par zone, dans le contenu (WS_OVER_MAX au plus) -> 0 : ces zones du tampon de la
 * fenêtre passent par-dessus sa surface ; les anciennes et les nouvelles sont recomposées */
long req_surface_over(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	u64 n = M64(msg, WM_B);
	if (n > WS_OVER_MAX)
		return E_INVAL;
	struct over *o = &overs[w - windows];
	for (int pass = 0; pass < 2; pass++) { /* les anciennes, puis les nouvelles */
		if (pass) {
			const u16 *r = (const u16 *)(msg + WM_DATA);
			o->n = 0;
			for (u64 i = 0; i < n; i++, r += 4) { /* bornées au contenu */
				u32 rx = r[0], ry = r[1], rw = r[2], rh = r[3];
				if (rx >= (u32)w->w || ry >= (u32)w->h || !rw || !rh)
					continue;
				if (rw > (u32)w->w - rx)
					rw = (u32)w->w - rx;
				if (rh > (u32)w->h - ry)
					rh = (u32)w->h - ry;
				o->r[o->n][0] = (u16)rx;
				o->r[o->n][1] = (u16)ry;
				o->r[o->n][2] = (u16)rw;
				o->r[o->n][3] = (u16)rh;
				o->n++;
			}
		}
		if (!w->mini)
			for (u32 i = 0; i < o->n; i++)
				dirty(w->x + o->r[i][0], w->y + o->r[i][1], o->r[i][2], o->r[i][3]);
	}
	w->gen++; /* (rdpd : le contenu a changé) */
	return 0;
}

/* source RGB565, pas source (octets), x, y (tampon arrière), largeur, hauteur : copie convertie
 * en XRGB8888 (5 -> 8 bits : v << 3 | v >> 2 ; 6 -> 8 : v << 2 | v >> 4), découpée par le clip
 * (chemin chaud de la composition : boucles simples que GCC vectorise) */
void bb_blit565(const u16 *src, long pitch, long x, long y, long width, long height)
{
	long cx = x, cy = y, cw = width, ch = height;
	clip_rect(&cx, &cy, &cw, &ch);
	if (cw <= 0 || ch <= 0)
		return;
	const u8 *s = (const u8 *)src + (cy - y) * pitch + ((cx - x) << 1); /* décalage dans la source */
	u8 *d = (u8 *)bb + cy * bb_pitch + (cx << 2);
	for (; ch > 0; ch--) {
		const u16 *sp = (const u16 *)s;
		u32 *dp = (u32 *)d;
		for (long i = 0; i < cw; i++) {
			u32 p = sp[i];
			u32 r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
			r = r << 3 | r >> 2;
			g = g << 2 | g >> 4;
			b = b << 3 | b >> 2;
			dp[i] = r << 16 | g << 8 | b;
		}
		d += bb_pitch;
		s += pitch;
	}
}

/* (rdpd, WS_OP_WIN_READ) buf = tampon de l'appelant (le rectangle lu, pas = largeur * 4),
 * x, y (dans le contenu), largeur, hauteur : la surface par-dessus le contenu recopié */
void surf_read_over(struct win *w, u32 *buf, long x, long y, long width, long height)
{
	u32 *keep_bb = bb; /* garder bb, bb_pitch, le clip */
	long keep_pitch = bb_pitch;
	struct rect keep_clip = clip_r;
	clip_r.x0 = x; /* clip = le rectangle (coordonnées du contenu) */
	clip_r.y0 = y;
	clip_r.x1 = x + width;
	clip_r.y1 = y + height;
	bb_pitch = width << 2; /* bb = tampon - (y * pas + x * 4) */
	bb = (u32 *)((u64)buf - (u64)(y * bb_pitch) - ((u64)x << 2));
	draw_surface(w, 0, 0);
	bb = keep_bb;
	bb_pitch = keep_pitch;
	clip_r = keep_clip;
}

/* WS_OP_SURFACE_ATTACH : a id, b tampon (-1 : détacher), c x | y << 32 (zone dans le contenu),
 * d largeur | hauteur << 32 ; données : +0 format (WS_SF_*, 32 bits), +4 pas (32 bits, 0 :
 * largeur * octets du pixel), +8 décalage du premier pixel dans le tampon (64 bits) -> 0 ;
 * réponse a = pid d'Elegant (E_PERM : le programme l'autorise par BUF_GRANT et recommence).
 * Rattacher le même tampon change seulement la zone, le format, le pas, le décalage (double
 * tampon dans un seul tampon partagé). */
long req_surface_attach(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	u64 b = M64(msg, WM_B);
	u64 c = M64(msg, WM_C), d = M64(msg, WM_D);
	u32 x = (u32)c, y = (u32)(c >> 32), sw = (u32)d, sh = (u32)(d >> 32);
	u32 raw = M32(msg, WM_DATA);
	u64 fmt = raw & 0xFF; /* (sans WS_SF_NOREDRAW) */
	u64 pitch = M32(msg, WM_DATA + 4);
	u64 off = M64(msg, WM_DATA + 8);
	M64(msg, WM_A) = (u64)korin_sys0(SYS_GETPID); /* (réponse : le pid d'Elegant) */
	long noredraw = 0;
	if (b == (u64)-1) {
		surf_detach(w);
	} else {
		if (fmt > WS_SF_RGB565)
			return E_INVAL;
		u64 bpp = fmt ? 2 : 4; /* octets du pixel */
		if (sw < 1 || sh < 1 || sw > SF_MAX_SIDE || sh > SF_MAX_SIDE || x > SF_MAX_SIDE ||
		    y > SF_MAX_SIDE)
			return E_INVAL;
		u64 row = sw * bpp; /* pas : au moins une ligne */
		if (!pitch)
			pitch = row;
		if (pitch < row || pitch > BUF_SLOT || off >= BUF_SLOT)
			return E_INVAL;
		u64 last = (sh - 1) * pitch + off + row; /* dernier octet lu : décalage + pas * (h - 1)
							     + largeur * octets */
		if (last > BUF_SLOT)
			return E_INVAL;
		struct surf *s = surf_entry(w);
		if (!s->addr || s->buf != b) { /* déjà attaché à ce tampon ? */
			if (s->addr)
				surf_detach(w); /* un autre : détaché d'abord */
			long r = korin_sys1(SYS_BUF_MAP, b);
			if (r < 0)
				return r; /* (E_PERM : pas autorisé) */
		}
		u8 *base = (u8 *)(BUF_VA_BASE + b * BUF_SLOT); /* (adresse du tampon chez Elegant) */
		long pi = korin_sys1(SYS_VM_PAGEINFO, base + last - 1); /* le tampon couvre la zone ? */
		if (!(pi & 1)) { /* (PI_PRESENT) tampon trop petit : refusé */
			s->buf = b;
			s->addr = base;
			surf_detach(w);
			return E_INVAL;
		}
		/* seul le décalage change, et WS_SF_NOREDRAW ? */
		if ((raw & WS_SF_NOREDRAW) && s->addr && s->x == x && s->y == y && s->w == sw &&
		    s->h == sh && s->pitch == pitch)
			noredraw = 1;
		s->addr = base + off;
		s->buf = b;
		s->x = x;
		s->y = y;
		s->w = sw;
		s->h = sh;
		s->pitch = pitch;
		s->fmt = fmt;
	}
	w->gen++; /* (rdpd : le contenu a changé) */
	if (!noredraw && !w->mini) /* (WS_SF_NOREDRAW : DAMAGE suit) */
		dirty(w->x, w->y, w->w, w->h); /* tout le contenu à recomposer */
	return 0;
}

/* WS_OP_SURFACE_DAMAGE : a id, b nombre de rectangles (0 : toute la surface) ; données : x, y,
 * largeur, hauteur (16 bits chacun) par rectangle, dans la surface (WS_DAMAGE_MAX au plus) -> 0 :
 * ce sont les zones sales du compositeur */
long req_surface_damage(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	long vw, vh;
	struct surf *s = surf_visible(w, &vw, &vh);
	if (!s->addr)
		return E_INVAL;
	w->gen++; /* (rdpd : le contenu a changé) */
	if (w->mini || vw <= 0 || vh <= 0)
		return 0;
	long ox = w->x + (long)s->x, oy = w->y + (long)s->y; /* origine de la surface à l'écran */
	u64 n = M64(msg, WM_B);
	if (!n) { /* toute la surface */
		dirty(ox, oy, vw, vh);
		return 0;
	}
	if (n > WS_DAMAGE_MAX)
		n = WS_DAMAGE_MAX;
	const u16 *r = (const u16 *)(msg + WM_DATA);
	for (; n; n--, r += 4) { /* x, y, l, h bornés à la surface visible */
		u64 rx = r[0], ry = r[1], rw = r[2], rh = r[3];
		if (rx >= (u64)vw || ry >= (u64)vh)
			continue;
		if (rw > (u64)vw - rx)
			rw = (u64)vw - rx;
		if (rh > (u64)vh - ry)
			rh = (u64)vh - ry;
		dirty((long)rx + ox, (long)ry + oy, (long)rw, (long)rh);
	}
	return 0;
}
