/* gdi.c -- win16 : GDI, tracé par nous dans les pixels de la fenêtre de Korin (be_pixels) ou d'un bitmap.
 *
 * winevdm passe GDI à gdi32 de Windows ; ici on trace : contextes de dessin (fenêtre, mémoire), plumes, brosses,
 * polices (celles de Korin), bitmaps et DIB, BitBlt/StretchBlt avec les 256 opérations raster, formes, polygones,
 * régions rectangulaires ou polygonales, modes de projection, palettes, icônes. Les métafichiers et l'impression
 * ne sont pas encore là (docs/etude/WIN16.md, étape 4). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "w16.h"
#include "be.h"
#include "user.h"
#include "gdi.h"

enum { OBJ_NONE, OBJ_PEN, OBJ_BRUSH, OBJ_FONT, OBJ_BITMAP, OBJ_PALETTE, OBJ_REGION, OBJ_ICON, OBJ_METAFILE };
#define MAX_OBJ 4096
#define MAX_PTS 64
struct obj
{
	int type, stock;
	uint32_t color;			/* plume, brosse : COLORREF */
	int style, width;		/* plume : PS_* ; brosse : BS_* (0 pleine, 1 creuse, 2 hachures, 3 motif) */
	/* police */
	int px, fstyle, height, weight, italic, underline, pitch;
	char face[32];
	/* bitmap */
	int w, h, bpp;
	uint32_t *px32;
	uint16_t mask;			/* icône : le masque (bitmap) ; image = color */
	uint16_t image;
	int dim_x, dim_y;		/* bitmap : SetBitmapDimension (0,1 mm) */
	/* palette */
	int npal;
	uint32_t *pal;
	/* région : rectangle, ou polygone */
	int rl, rt, rr, rb, npts;
	int (*pts)[2];
};
static struct obj objs[MAX_OBJ];
#define OBJ_HANDLE(i) ((uint16_t) (0x2000 + (i) * 4))
static struct obj *obj_get (uint16_t h)
{
	int i = (h - 0x2000) / 4;
	if (h < 0x2000 || (h & 3) || i >= MAX_OBJ || !objs[i].type)
		return 0;
	return &objs[i];
}

static uint16_t obj_new (int type)
{
	for (int i = 32; i < MAX_OBJ; i++)
		if (!objs[i].type)
		{
			memset (&objs[i], 0, sizeof objs[i]);
			objs[i].type = type;
			return OBJ_HANDLE (i);
		}
	return 0;
}

static void obj_free (struct obj *o)
{
	free (o->px32);
	free (o->pal);
	free (o->pts);
	memset (o, 0, sizeof *o);
}

/* ======================================================================== DC */
#define MAX_DC 128
#define MAX_SAVE 8
#define MAX_EXCL 48
struct dcstate
{
	uint16_t pen, brush, font, bitmap, palette;
	uint32_t text_color, bk_color;
	int bk_mode, rop2, map_mode, align, poly_mode, stretch_mode;
	int cur_x, cur_y;
	int wox, woy, wex, wey, vox, voy, vex, vey;
	int clip_on, cl, ct, cr, cb;	/* découpe (coordonnées de l'appareil) */
	int brush_x, brush_y;		/* origine des motifs (SetBrushOrg ; les motifs pleins n'en dépendent pas) */
	int nex, ex[MAX_EXCL][4];	/* rectangles exclus (ExcludeClipRect, filles de WS_CLIPCHILDREN) */
};
struct dc
{
	int used;
	int kind;			/* 0 fenêtre, 1 mémoire, 2 information (rien n'est tracé) */
	uint16_t hwnd;
	struct dcstate s;
	struct dcstate save[MAX_SAVE];
	int nsave;
	int dx0, dy0, dx1, dy1, dirty;
};
static struct dc dcs[MAX_DC];
#define DC_HANDLE(i) ((uint16_t) (0x7000 + (i) * 4))
static struct dc *dc_get (uint16_t h)
{
	int i = (h - 0x7000) / 4;
	if (h < 0x7000 || (h & 3) || i >= MAX_DC || !dcs[i].used)
		return 0;
	return &dcs[i];
}

/* la surface d'un DC : pixels, origine (de la zone cliente dans la fenêtre de Korin) et découpe, en appareil */
struct surf { uint32_t *px; int stride, w, h, ox, oy, cx0, cy0, cx1, cy1; long be; struct dc *d; };

static int dc_surface (struct dc *d, struct surf *s)
{
	memset (s, 0, sizeof *s);
	s->d = d;
	if (d->kind == 2)
		return 0;
	if (d->kind == 1)
	{
		struct obj *b = obj_get (d->s.bitmap);
		if (!b || !b->px32)
			return 0;
		s->px = b->px32;
		s->stride = s->w = b->w;
		s->h = b->h;
		s->cx1 = b->w;
		s->cy1 = b->h;
	}
	else
	{
		int ox, oy, cw, ch, pw, ph, stride;
		long be;
		if (!wnd_client_geometry (d->hwnd, &ox, &oy, &cw, &ch, &be))
			return 0;
		s->px = be_pixels (be, &pw, &ph, &stride);
		if (!s->px)
			return 0;
		s->be = be;
		s->stride = stride;
		s->w = pw;
		s->h = ph;
		s->ox = ox;
		s->oy = oy;
		s->cx0 = ox < 0 ? 0 : ox;
		s->cy0 = oy < 0 ? 0 : oy;
		s->cx1 = ox + cw > pw ? pw : ox + cw;
		s->cy1 = oy + ch > ph ? ph : oy + ch;
	}
	if (d->s.clip_on)
	{
		if (s->cx0 < d->s.cl + s->ox) s->cx0 = d->s.cl + s->ox;
		if (s->cy0 < d->s.ct + s->oy) s->cy0 = d->s.ct + s->oy;
		if (s->cx1 > d->s.cr + s->ox) s->cx1 = d->s.cr + s->ox;
		if (s->cy1 > d->s.cb + s->oy) s->cy1 = d->s.cb + s->oy;
	}
	return s->cx1 > s->cx0 && s->cy1 > s->cy0;
}

/* logique -> appareil (relatif à la zone cliente) */
static int lx (struct dc *d, int x)
{
	if (d->s.map_mode == 1)
		return x - d->s.wox + d->s.vox;
	return (int) ((long) (x - d->s.wox) * d->s.vex / (d->s.wex ? d->s.wex : 1)) + d->s.vox;
}

static int ly (struct dc *d, int y)
{
	if (d->s.map_mode == 1)
		return y - d->s.woy + d->s.voy;
	return (int) ((long) (y - d->s.woy) * d->s.vey / (d->s.wey ? d->s.wey : 1)) + d->s.voy;
}

static int dlx (struct dc *d, int x)	/* appareil -> logique */
{
	if (d->s.map_mode == 1)
		return x - d->s.vox + d->s.wox;
	return (int) ((long) (x - d->s.vox) * d->s.wex / (d->s.vex ? d->s.vex : 1)) + d->s.wox;
}

static int dly (struct dc *d, int y)
{
	if (d->s.map_mode == 1)
		return y - d->s.voy + d->s.woy;
	return (int) ((long) (y - d->s.voy) * d->s.wey / (d->s.vey ? d->s.vey : 1)) + d->s.woy;
}

static int lw (struct dc *d, int w)	/* une largeur logique -> pixels */
{
	if (d->s.map_mode == 1)
		return w;
	int r = (int) labs ((long) w * d->s.vex / (d->s.wex ? d->s.wex : 1));
	return r;
}

uint32_t gdi_ref_to_rgb (uint16_t hdc, uint32_t c)
{
	if ((c >> 24) == 1)	/* PALETTEINDEX */
	{
		struct dc *d = dc_get (hdc);
		struct obj *p = d ? obj_get (d->s.palette) : 0;
		int i = (int) (c & 0xffff);
		if (p && i < p->npal)
			return p->pal[i];
		return 0;
	}
	return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
}

static uint32_t rgb_of (struct dc *d, uint32_t ref)
{
	return gdi_ref_to_rgb (DC_HANDLE (d - dcs), ref);
}

static void touch (struct surf *s, int x0, int y0, int x1, int y1)
{
	struct dc *d = s->d;
	if (d->kind != 0)
		return;
	if (x0 < s->cx0) x0 = s->cx0;
	if (y0 < s->cy0) y0 = s->cy0;
	if (x1 > s->cx1) x1 = s->cx1;
	if (y1 > s->cy1) y1 = s->cy1;
	if (x1 <= x0 || y1 <= y0)
		return;
	if (!d->dirty)
	{
		d->dx0 = x0; d->dy0 = y0; d->dx1 = x1; d->dy1 = y1;
		d->dirty = 1;
		return;
	}
	if (x0 < d->dx0) d->dx0 = x0;
	if (y0 < d->dy0) d->dy0 = y0;
	if (x1 > d->dx1) d->dx1 = x1;
	if (y1 > d->dy1) d->dy1 = y1;
}

static int excluded (struct surf *s, int x, int y)
{
	struct dcstate *t = &s->d->s;
	for (int i = 0; i < t->nex; i++)
		if (x >= t->ex[i][0] + s->ox && x < t->ex[i][2] + s->ox && y >= t->ex[i][1] + s->oy && y < t->ex[i][3] + s->oy)
			return 1;
	return 0;
}

/* les 16 opérations binaires (SetROP2) */
static uint32_t rop2 (int r, uint32_t pen, uint32_t dst)
{
	switch (r)
	{
	case 1: return 0;			/* R2_BLACK */
	case 2: return ~(pen | dst) & 0xffffff;	/* R2_NOTMERGEPEN */
	case 3: return dst & ~pen & 0xffffff;
	case 4: return ~pen & 0xffffff;
	case 5: return pen & ~dst & 0xffffff;
	case 6: return ~dst & 0xffffff;		/* R2_NOT */
	case 7: return pen ^ dst;		/* R2_XORPEN */
	case 8: return ~(pen & dst) & 0xffffff;
	case 9: return pen & dst;
	case 10: return ~(pen ^ dst) & 0xffffff;	/* R2_NOTXORPEN */
	case 11: return dst;			/* R2_NOP */
	case 12: return (dst | ~pen) & 0xffffff;
	case 14: return (pen | ~dst) & 0xffffff;
	case 15: return pen | dst;
	case 16: return 0xffffff;
	default: return pen;			/* R2_COPYPEN */
	}
}

/* un rectangle plein en coordonnées de l'appareil (zone cliente), [x0, x1) × [y0, y1) */
static void dfill (struct surf *s, int x0, int y0, int x1, int y1, uint32_t rgb, int rop)
{
	x0 += s->ox; x1 += s->ox; y0 += s->oy; y1 += s->oy;
	if (x0 < s->cx0) x0 = s->cx0;
	if (y0 < s->cy0) y0 = s->cy0;
	if (x1 > s->cx1) x1 = s->cx1;
	if (y1 > s->cy1) y1 = s->cy1;
	if (x1 <= x0 || y1 <= y0)
		return;
	/* (les rectangles exclus qui touchent ce rectangle ; aucun : le chemin rapide) */
	struct dcstate *st = &s->d->s;
	int hits[MAX_EXCL], nh = 0;
	for (int i = 0; i < st->nex; i++)
		if (st->ex[i][0] + s->ox < x1 && st->ex[i][2] + s->ox > x0 && st->ex[i][1] + s->oy < y1 && st->ex[i][3] + s->oy > y0)
			hits[nh++] = i;
	for (int y = y0; y < y1; y++)
	{
		uint32_t *p = s->px + (size_t) y * (size_t) s->stride;
		if (!nh && rop == 13)
			for (int x = x0; x < x1; x++)
				p[x] = rgb;
		else
			for (int x = x0; x < x1; x++)
			{
				int out = 0;
				for (int k = 0; k < nh && !out; k++)
				{
					int *e = st->ex[hits[k]];
					out = x >= e[0] + s->ox && x < e[2] + s->ox && y >= e[1] + s->oy && y < e[3] + s->oy;
				}
				if (!out)
					p[x] = rop2 (rop, rgb, p[x] & 0xffffff);
			}
	}
	touch (s, x0, y0, x1, y1);
}

static void dpixel (struct surf *s, int x, int y, uint32_t rgb, int rop)
{
	x += s->ox;
	y += s->oy;
	if (x < s->cx0 || y < s->cy0 || x >= s->cx1 || y >= s->cy1 || (s->d->s.nex && excluded (s, x, y)))
		return;
	uint32_t *p = s->px + (size_t) y * (size_t) s->stride + (size_t) x;
	*p = rop2 (rop, rgb, *p & 0xffffff);
	touch (s, x, y, x + 1, y + 1);
}

static int pen_of (struct dc *d, uint32_t *rgb, int *w)
{
	struct obj *p = obj_get (d->s.pen);
	if (!p || p->style == 5)	/* PS_NULL */
		return 0;
	*rgb = rgb_of (d, p->color);
	*w = p->width > 1 ? lw (d, p->width) : 1;
	if (*w < 1)
		*w = 1;
	return 1;
}

static int brush_of (struct dc *d, uint32_t *rgb)
{
	struct obj *b = obj_get (d->s.brush);
	if (!b || b->style == 1)	/* BS_HOLLOW */
		return 0;
	*rgb = rgb_of (d, b->color);
	return 1;
}

/* une ligne en appareil, sans le dernier point (comme Windows) */
static void dline (struct surf *s, int x0, int y0, int x1, int y1, uint32_t rgb, int w, int rop)
{
	int dx = abs (x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
	while (x0 != x1 || y0 != y1)
	{
		if (w > 1)
			dfill (s, x0 - w / 2, y0 - w / 2, x0 - w / 2 + w, y0 - w / 2 + w, rgb, rop);
		else
			dpixel (s, x0, y0, rgb, rop);
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

/* un polygone plein (appareil), règle pair-impair (1) ou enroulement (2) */
static void dpoly_fill (struct surf *s, int (*p)[2], int n, uint32_t rgb, int mode)
{
	if (n < 3)
		return;
	int ymin = p[0][1], ymax = p[0][1];
	for (int i = 1; i < n; i++)
	{
		if (p[i][1] < ymin) ymin = p[i][1];
		if (p[i][1] > ymax) ymax = p[i][1];
	}
	int xs[256], dirs[256];
	for (int y = ymin; y < ymax; y++)
	{
		int k = 0;
		double fy = y + 0.5;
		for (int i = 0; i < n && k < 256; i++)
		{
			int *a = p[i], *b = p[(i + 1) % n];
			if ((a[1] <= fy) == (b[1] <= fy))
				continue;
			double t = (fy - a[1]) / (double) (b[1] - a[1]);
			xs[k] = (int) floor (a[0] + t * (b[0] - a[0]) + 0.5);
			dirs[k++] = b[1] > a[1] ? 1 : -1;
		}
		for (int i = 1; i < k; i++)	/* (tri par insertion) */
			for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--)
			{
				int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t;
				t = dirs[j]; dirs[j] = dirs[j - 1]; dirs[j - 1] = t;
			}
		if (mode == 2)
		{
			int wind = 0;
			for (int i = 0; i + 1 < k; i++)
			{
				wind += dirs[i];
				if (wind)
					dfill (s, xs[i], y, xs[i + 1], y + 1, rgb, s->d->s.rop2);
			}
		}
		else
			for (int i = 0; i + 1 < k; i += 2)
				dfill (s, xs[i], y, xs[i + 1], y + 1, rgb, s->d->s.rop2);
	}
}

/* ---- DC des fenêtres ---- */
static void dc_defaults (struct dc *d)
{
	memset (&d->s, 0, sizeof d->s);
	d->s.pen = OBJ_HANDLE (7);	/* BLACK_PEN */
	d->s.brush = OBJ_HANDLE (0);	/* WHITE_BRUSH */
	d->s.font = OBJ_HANDLE (13);	/* SYSTEM_FONT */
	d->s.palette = OBJ_HANDLE (15);
	d->s.text_color = 0;
	d->s.bk_color = 0xffffff;
	d->s.bk_mode = 2;
	d->s.rop2 = 13;
	d->s.map_mode = 1;
	d->s.wex = d->s.wey = d->s.vex = d->s.vey = 1;
	d->s.poly_mode = 1;
	d->s.stretch_mode = 1;
}

static uint16_t dc_new (int kind, uint16_t hwnd)
{
	for (int i = 0; i < MAX_DC; i++)
		if (!dcs[i].used)
		{
			struct dc *d = &dcs[i];
			memset (d, 0, sizeof *d);
			d->used = 1;
			d->kind = kind;
			d->hwnd = hwnd;
			dc_defaults (d);
			return DC_HANDLE (i);
		}
	w16_fatal ("plus de contextes de dessin libres");
}

int user_child_rects (uint16_t hwnd, int (*r)[4], int max);

uint16_t gdi_dc_for_window (uint16_t hwnd)
{
	uint16_t h = dc_new (0, hwnd);
	/* WS_CLIPCHILDREN : les fenêtres filles visibles sont exclues du dessin du parent */
	struct dc *d = dc_get (h);
	d->s.nex = user_child_rects (hwnd, d->s.ex, MAX_EXCL);
	return h;
}

void gdi_dc_clip (uint16_t hdc, int l, int t, int r, int b)
{
	struct dc *d = dc_get (hdc);
	if (!d)
		return;
	d->s.clip_on = 1;
	d->s.cl = l;
	d->s.ct = t;
	d->s.cr = r;
	d->s.cb = b;
}

static void flush (struct dc *d)
{
	if (!d->dirty || d->kind != 0)
		return;
	int ox, oy, cw, ch;
	long be;
	if (wnd_client_geometry (d->hwnd, &ox, &oy, &cw, &ch, &be))
		be_present (be, d->dx0, d->dy0, d->dx1 - d->dx0, d->dy1 - d->dy0);
	d->dirty = 0;
}

void gdi_release_dc (uint16_t hdc)
{
	struct dc *d = dc_get (hdc);
	if (!d)
		return;
	flush (d);
	d->used = 0;
}

uint32_t gdi_brush_color (uint16_t hbrush, int *hollow)
{
	*hollow = 0;
	if (hbrush && hbrush <= 32)	/* COLOR_* + 1 */
		return user_sys_color (hbrush - 1);
	struct obj *o = obj_get (hbrush);
	if (!o || o->type != OBJ_BRUSH || o->style == 1)
	{
		*hollow = 1;
		return 0;
	}
	return o->color;
}

void gdi_fill_rect_brush (uint16_t hdc, int l, int t, int r, int b, uint16_t hbrush)
{
	struct dc *d = dc_get (hdc);
	struct surf s;
	int hollow;
	uint32_t ref = gdi_brush_color (hbrush, &hollow);
	if (!d || hollow || !dc_surface (d, &s))
		return;
	dfill (&s, lx (d, l), ly (d, t), lx (d, r), ly (d, b), rgb_of (d, ref), 13);
}

/* ---- pour USER : tracés en couleurs directes ---- */
void gdi_rgb_fill (uint16_t hdc, int l, int t, int r, int b, uint32_t rgb)
{
	struct dc *d = dc_get (hdc);
	struct surf s;
	if (d && dc_surface (d, &s))
		dfill (&s, lx (d, l), ly (d, t), lx (d, r), ly (d, b), rgb, 13);
}

void gdi_rgb_frame (uint16_t hdc, int l, int t, int r, int b, uint32_t rgb)
{
	gdi_rgb_fill (hdc, l, t, r, t + 1, rgb);
	gdi_rgb_fill (hdc, l, b - 1, r, b, rgb);
	gdi_rgb_fill (hdc, l, t, l + 1, b, rgb);
	gdi_rgb_fill (hdc, r - 1, t, r, b, rgb);
}

void gdi_invert (uint16_t hdc, int l, int t, int r, int b)
{
	struct dc *d = dc_get (hdc);
	struct surf s;
	if (d && dc_surface (d, &s))
		dfill (&s, lx (d, l), ly (d, t), lx (d, r), ly (d, b), 0, 6);
}

void gdi_rgb_line (uint16_t hdc, int x0, int y0, int x1, int y1, uint32_t rgb)
{
	struct dc *d = dc_get (hdc);
	struct surf s;
	if (d && dc_surface (d, &s))
		dline (&s, lx (d, x0), ly (d, y0), lx (d, x1), ly (d, y1), rgb, 1, 13);
}

/* un rectangle à coins arrondis, plein et bordé (rayon en pixels) */
void gdi_rgb_rbox (uint16_t hdc, int l, int t, int r, int b, int rad, uint32_t fill, uint32_t border)
{
	struct dc *d = dc_get (hdc);
	struct surf s;
	if (!d || !dc_surface (d, &s))
		return;
	l = lx (d, l); r = lx (d, r); t = ly (d, t); b = ly (d, b);
	if (rad * 2 > r - l) rad = (r - l) / 2;
	if (rad * 2 > b - t) rad = (b - t) / 2;
	for (int y = t; y < b; y++)
	{
		int in = 0;
		int dy = y < t + rad ? t + rad - y : y >= b - rad ? y - (b - rad - 1) : 0;
		if (dy)
			in = rad - (int) floor (sqrt ((double) rad * rad - (double) (dy - 0.5) * (dy - 0.5)) + 0.5);
		if (in < 0) in = 0;
		dfill (&s, l + in, y, r - in, y + 1, border, 13);
		int edge = (y == t || y == b - 1);
		if (!edge)
		{
			int in2 = in;
			if (dy)
			{
				int dy2 = dy + 1;
				double v = (double) (rad - 1) * (rad - 1) - (double) (dy2 - 1.5) * (dy2 - 1.5);
				in2 = v > 0 ? rad - (int) floor (sqrt (v) + 0.5) : rad;
				if (in2 < in + 1) in2 = in + 1;
			}
			else
				in2 = 1;
			dfill (&s, l + in2, y, r - in2, y + 1, fill, 13);
		}
	}
}

void gdi_rgb_disc (uint16_t hdc, int cx, int cy, int rad, uint32_t rgb)
{
	struct dc *d = dc_get (hdc);
	struct surf s;
	if (!d || !dc_surface (d, &s))
		return;
	cx = lx (d, cx);
	cy = ly (d, cy);
	for (int y = -rad; y < rad; y++)
	{
		double v = (double) rad * rad - (y + 0.5) * (y + 0.5);
		if (v < 0)
			continue;
		int h = (int) floor (sqrt (v) + 0.5);
		dfill (&s, cx - h, cy + y, cx + h, cy + y + 1, rgb, 13);
	}
}

/* ======================================================================== polices */
static void font_resolve (struct obj *f)
{
	int h = f->height;
	int px = h < 0 ? -h : h > 0 ? h * 4 / 5 : 13;
	if (px < 6)
		px = 6;
	if (px > 96)
		px = 96;
	f->px = px;
	int st = 0;
	char face[32];
	snprintf (face, sizeof face, "%s", f->face);
	for (char *p = face; *p; p++)
		*p = (char) (*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
	int mono = (f->pitch & 3) == 1 || strstr (face, "courier") || strstr (face, "fixed") || strstr (face, "terminal")
		|| strstr (face, "mono") || strstr (face, "lucida console");
	int serif = strstr (face, "times") || strstr (face, "roman") || strstr (face, "serif") ? !strstr (face, "sans") : 0;
	if (mono)
		st = 2;
	else if (serif)
		st = f->weight >= 600 ? 4 : 3;
	else if (f->weight >= 600)
		st = 1;
	else if (f->italic)
		st = 5;
	f->fstyle = st;
}

static struct obj *font_of (struct dc *d, uint16_t font)
{
	struct obj *f = obj_get (font ? font : d->s.font);
	if (!f || f->type != OBJ_FONT)
		f = &objs[13];
	return f;
}

static int text_extent (struct obj *f, const char *utf8)
{
	return be_text_width (utf8, f->px, f->fstyle);
}

/* le texte d'un DC (CP1252) en (x, y) logiques : haut de la cellule ; rend la largeur en pixels */
static int dtext (struct dc *d, struct surf *s, int x, int y, const uint8_t *str, int n, uint32_t rgb, struct obj *f,
	int opaque, uint32_t bk)
{
	char buf[2048];
	w16_to_utf8 (str, n, buf, sizeof buf);
	int w = text_extent (f, buf), h = be_font_height (f->px);
	if (opaque)
		dfill (s, x, y, x + w, y + h, bk, 13);
	int cw = s->cx1 - s->cx0, ch = s->cy1 - s->cy0;
	if (d->kind == 1)
		be_text_buf (s->px, s->w, s->h, x + s->ox, y + s->oy, buf, rgb, s->cx0, s->cy0, cw, ch, f->px, f->fstyle);
	else
		be_text (s->be, x + s->ox, y + s->oy, buf, rgb, s->cx0, s->cy0, cw, ch, f->px, f->fstyle);
	if (f->underline)
		dfill (s, x, y + be_font_ascent (f->px) + 1, x + w, y + be_font_ascent (f->px) + 2, rgb, 13);
	touch (s, x + s->ox, y + s->oy, x + s->ox + w, y + s->oy + h);
	return w;
}

int gdi_text (uint16_t hdc, int x, int y, const char *s, int n, uint32_t rgb, uint16_t font)
{
	struct dc *d = dc_get (hdc);
	struct surf su;
	if (!d || !dc_surface (d, &su))
		return 0;
	return dtext (d, &su, lx (d, x), ly (d, y), (const uint8_t *) s, n, rgb, font_of (d, font), 0, 0);
}

int gdi_text_width (uint16_t hdc, const char *s, int n, uint16_t font)
{
	struct dc *d = dc_get (hdc);
	char buf[2048];
	w16_to_utf8 ((const uint8_t *) s, n, buf, sizeof buf);
	return text_extent (d ? font_of (d, font) : &objs[13], buf);
}

int gdi_text_height (uint16_t hdc, uint16_t font)
{
	struct dc *d = dc_get (hdc);
	return be_font_height ((d ? font_of (d, font) : &objs[13])->px);
}

uint16_t gdi_get_font (uint16_t hdc) { struct dc *d = dc_get (hdc); return d ? d->s.font : 0; }
void gdi_set_font (uint16_t hdc, uint16_t f) { struct dc *d = dc_get (hdc); if (d && obj_get (f)) d->s.font = f; }
uint32_t gdi_text_color (uint16_t hdc) { struct dc *d = dc_get (hdc); return d ? d->s.text_color : 0; }
uint32_t gdi_bk_color (uint16_t hdc) { struct dc *d = dc_get (hdc); return d ? d->s.bk_color : 0xffffff; }
void gdi_set_colors (uint16_t hdc, uint32_t t, uint32_t b) { struct dc *d = dc_get (hdc); if (d) { d->s.text_color = t; d->s.bk_color = b; } }
uint16_t gdi_stock (int i) { return OBJ_HANDLE (i); }

/* DrawText : rect (4 entiers, logiques) ; DT_* de Windows ; rend la hauteur */
int gdi_draw_text (uint16_t hdc, const char *str, int n, int *rc, unsigned fmt)
{
	struct dc *d = dc_get (hdc);
	if (!d)
		return 0;
	struct obj *f = font_of (d, 0);
	if (n < 0)
		n = (int) strlen (str);
	/* les lignes, découpées aux \n et (DT_WORDBREAK) aux mots ; « & » souligne la lettre suivante (DT_NOPREFIX) */
	char line[1024];
	int lh = be_font_height (f->px), y = rc[1], maxw = 0, lines = 0;
	int width = rc[2] - rc[0];
	struct surf s;
	int can = !(fmt & 0x400) && dc_surface (d, &s);	/* DT_CALCRECT */
	int i = 0;
	int total = 0;
	/* (première passe pour le centrage vertical d'une ligne seule) */
	if ((fmt & 0x20) && (fmt & 0x04))	/* DT_SINGLELINE | DT_VCENTER */
		y = rc[1] + (rc[3] - rc[1] - lh) / 2;
	else if ((fmt & 0x20) && (fmt & 0x08))	/* DT_BOTTOM */
		y = rc[3] - lh;
	while (i < n || (i == 0 && n == 0))
	{
		int k = 0, last_space = -1, start = i;
		while (i < n && str[i] != '\n' && k < (int) sizeof line - 1)
		{
			if (str[i] == '\r')
			{
				i++;
				continue;
			}
			if (str[i] == '&' && !(fmt & 0x800))
			{
				if (i + 1 < n && str[i + 1] == '&')
					i++;
				else
				{
					i++;
					continue;
				}
			}
			line[k++] = str[i++];
			line[k] = 0;
			if (line[k - 1] == ' ')
				last_space = k - 1;
			if ((fmt & 0x10) && !(fmt & 0x20) && width > 0 && gdi_text_width (hdc, line, k, 0) > width && last_space > 0)
			{
				/* (couper au dernier espace) */
				i -= k - last_space - 1;
				k = last_space;
				break;
			}
		}
		(void) start;
		if (i < n && str[i] == '\n')
			i++;
		line[k] = 0;
		int w = gdi_text_width (hdc, line, k, 0);
		if (w > maxw)
			maxw = w;
		int x = rc[0];
		if (fmt & 0x01)	/* DT_CENTER */
			x = rc[0] + (width - w) / 2;
		else if (fmt & 0x02)	/* DT_RIGHT */
			x = rc[2] - w;
		if (can)
			dtext (d, &s, lx (d, x), ly (d, y), (const uint8_t *) line, k, rgb_of (d, d->s.text_color), f,
				d->s.bk_mode == 2, rgb_of (d, d->s.bk_color));
		y += lh;
		total += lh;
		lines++;
		if ((fmt & 0x20) || n == 0)
			break;
	}
	if (fmt & 0x400)
	{
		rc[2] = rc[0] + maxw;
		rc[3] = rc[1] + total;
	}
	return total;
}

/* ======================================================================== bitmaps */
static uint16_t bitmap_new (int w, int h, int bpp)
{
	uint16_t hb = obj_new (OBJ_BITMAP);
	struct obj *b = obj_get (hb);
	if (!b)
		return 0;
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	b->w = w;
	b->h = h;
	b->bpp = bpp;
	b->px32 = calloc ((size_t) w * (size_t) h, 4);
	return hb;
}

/* un DIB (BITMAPINFOHEADER ou BITMAPCOREHEADER, puis la table des couleurs) -> pixels ; bits : 0 = à la suite */
static int dib_decode (const uint8_t *bi, const uint8_t *bits, uint32_t *out, int ow, int oh, int start, int lines,
	int topdown_out)
{
	uint32_t hsz = bi[0] | bi[1] << 8 | bi[2] << 16 | (uint32_t) bi[3] << 24;
	int w, h, bpp, comp = 0, ncol;
	const uint8_t *pal;
	int pal_step;
	if (hsz == 12)
	{
		w = bi[4] | bi[5] << 8;
		h = (int16_t) (bi[6] | bi[7] << 8);
		bpp = bi[10] | bi[11] << 8;
		ncol = bpp <= 8 ? 1 << bpp : 0;
		pal = bi + 12;
		pal_step = 3;
	}
	else
	{
		w = (int32_t) (bi[4] | bi[5] << 8 | bi[6] << 16 | (uint32_t) bi[7] << 24);
		h = (int32_t) (bi[8] | bi[9] << 8 | bi[10] << 16 | (uint32_t) bi[11] << 24);
		bpp = bi[14] | bi[15] << 8;
		comp = bi[16];
		uint32_t used = bi[32] | bi[33] << 8 | bi[34] << 16 | (uint32_t) bi[35] << 24;
		ncol = bpp <= 8 ? (used ? (int) used : 1 << bpp) : (comp == 3 ? 3 : (int) used);
		pal = bi + hsz;
		pal_step = 4;
	}
	if (!bits)
		bits = pal + ncol * pal_step;
	int bottom_up = h > 0;
	if (h < 0)
		h = -h;
	int stride = ((w * bpp + 31) / 32) * 4;
	uint32_t colors[256];
	for (int i = 0; i < 256; i++)
		colors[i] = i < ncol && bpp <= 8 ? (uint32_t) (pal[i * pal_step + 2] << 16 | pal[i * pal_step + 1] << 8 | pal[i * pal_step]) : 0;
	if (comp == 1 || comp == 2)	/* RLE8, RLE4 */
	{
		uint8_t *idx = calloc ((size_t) w * (size_t) h, 1);
		const uint8_t *p = bits;
		int x = 0, y = 0;
		for (int guard = 0; guard < w * h * 4 + 1000 && y < h; guard++)
		{
			int a = *p++, b = *p++;
			if (a)
			{
				for (int k = 0; k < a && x < w; k++, x++)
					idx[y * w + x] = (uint8_t) (comp == 1 ? b : (k & 1 ? b & 15 : b >> 4));
			}
			else if (b == 0) { x = 0; y++; }
			else if (b == 1) break;
			else if (b == 2) { x += *p++; y += *p++; }
			else
			{
				for (int k = 0; k < b; k++, x++)
				{
					int v = comp == 1 ? p[k] : (k & 1 ? p[k / 2] & 15 : p[k / 2] >> 4);
					if (x < w)
						idx[y * w + x] = (uint8_t) v;
				}
				int nb = comp == 1 ? b : (b + 1) / 2;
				p += (nb + 1) & ~1;
			}
		}
		for (int yy = 0; yy < h && yy < oh; yy++)
			for (int xx = 0; xx < w && xx < ow; xx++)
			{
				int sy = bottom_up ? h - 1 - yy : yy;
				out[(topdown_out ? yy : oh - 1 - yy) * ow + xx] = colors[idx[sy * w + xx]];
			}
		free (idx);
		return h;
	}
	for (int line = start; line < start + lines && line < h; line++)
	{
		const uint8_t *row = bits + (size_t) (line - start) * (size_t) stride;
		int yy = bottom_up ? h - 1 - line : line;	/* ligne d'image (0 = haut) */
		if (yy < 0 || yy >= oh)
			continue;
		uint32_t *o = out + (size_t) (topdown_out ? yy : oh - 1 - yy) * (size_t) ow;
		for (int x = 0; x < w && x < ow; x++)
		{
			uint32_t c;
			switch (bpp)
			{
			case 1: c = colors[(row[x >> 3] >> (7 - (x & 7))) & 1]; break;
			case 4: c = colors[(row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15]; break;
			case 8: c = colors[row[x]]; break;
			case 16: { int v = row[2 * x] | row[2 * x + 1] << 8; c = (uint32_t) (((v >> 10) & 31) * 255 / 31) << 16 | (uint32_t) (((v >> 5) & 31) * 255 / 31) << 8 | (uint32_t) ((v & 31) * 255 / 31); break; }
			case 24: c = (uint32_t) (row[3 * x + 2] << 16 | row[3 * x + 1] << 8 | row[3 * x]); break;
			case 32: c = (uint32_t) (row[4 * x + 2] << 16 | row[4 * x + 1] << 8 | row[4 * x]); break;
			default: c = 0; break;
			}
			o[x] = c;
		}
	}
	return lines;
}

static void dib_size (const uint8_t *bi, int *w, int *h, int *bpp)
{
	uint32_t hsz = bi[0] | bi[1] << 8;
	if (hsz == 12)
	{
		*w = bi[4] | bi[5] << 8;
		*h = (int16_t) (bi[6] | bi[7] << 8);
		*bpp = bi[10];
	}
	else
	{
		*w = (int32_t) (bi[4] | bi[5] << 8 | bi[6] << 16 | (uint32_t) bi[7] << 24);
		*h = (int32_t) (bi[8] | bi[9] << 8 | bi[10] << 16 | (uint32_t) bi[11] << 24);
		*bpp = bi[14];
	}
	if (*h < 0)
		*h = -*h;
}

uint16_t gdi_bitmap_from_dib (const uint8_t *dib, uint32_t size)
{
	(void) size;
	int w, h, bpp;
	dib_size (dib, &w, &h, &bpp);
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
		return 0;
	uint16_t hb = bitmap_new (w, h, bpp == 1 ? 1 : 24);
	struct obj *b = obj_get (hb);
	if (b)
		dib_decode (dib, 0, b->px32, w, h, 0, h, 1);
	return hb;
}

/* ======================================================================== icônes */
uint16_t gdi_icon_load (uint16_t hinst, uint32_t name)
{
	if (!hinst || !SELECTOROF (name) && OFFSETOF (name) >= 32512 && !hinst)
		return 0;
	uint32_t size;
	const uint8_t *grp = kernel_resource (hinst, 14, name, &size);	/* RT_GROUP_ICON */
	if (!grp || size < 6)
		return 0;
	int count = grp[4] | grp[5] << 8, best = -1, bestscore = -1;
	for (int i = 0; i < count; i++)
	{
		const uint8_t *e = grp + 6 + 14 * i;
		int w = e[0] ? e[0] : 256, bc = e[6] | e[7] << 8;
		int score = (w == 32 ? 1000 : 500 - abs (w - 32)) + bc;
		if (score > bestscore)
		{
			bestscore = score;
			best = i;
		}
	}
	if (best < 0)
		return 0;
	int id = grp[6 + 14 * best + 12] | grp[6 + 14 * best + 13] << 8;
	const uint8_t *ic = kernel_resource (hinst, 3, (uint32_t) id, &size);	/* RT_ICON */
	if (!ic)
		return 0;
	int w, h, bpp;
	dib_size (ic, &w, &h, &bpp);
	h /= 2;	/* (image puis masque) */
	uint16_t hi = obj_new (OBJ_ICON);
	struct obj *o = obj_get (hi);
	uint16_t img = bitmap_new (w, h, 24), msk = bitmap_new (w, h, 1);
	o->image = img;
	o->mask = msk;
	o->w = w;
	o->h = h;
	/* l'image : les h lignes du bas du DIB (ordre de bas en haut) ; le masque : 1 bit, juste après */
	uint8_t hdr[40];
	memcpy (hdr, ic, 40);
	hdr[8] = (uint8_t) h; hdr[9] = (uint8_t) (h >> 8); hdr[10] = hdr[11] = 0;
	uint32_t hsz = ic[0] | ic[1] << 8;
	int ncol = bpp <= 8 ? 1 << bpp : 0;
	const uint8_t *pal = ic + hsz;
	const uint8_t *xor_bits = pal + ncol * 4;
	int xstride = ((w * bpp + 31) / 32) * 4, mstride = ((w + 31) / 32) * 4;
	uint8_t *tmp = malloc (hsz + (size_t) ncol * 4);
	memcpy (tmp, ic, hsz + (size_t) ncol * 4);
	memcpy (tmp, hdr, 40);
	dib_decode (tmp, xor_bits, obj_get (img)->px32, w, h, 0, h, 1);
	free (tmp);
	const uint8_t *and_bits = xor_bits + (size_t) xstride * (size_t) h;
	struct obj *m = obj_get (msk);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			m->px32[(h - 1 - y) * w + x] = (and_bits[y * mstride + (x >> 3)] >> (7 - (x & 7))) & 1 ? 0xffffff : 0;
	return hi;
}

void gdi_icon_draw (uint16_t hdc, int x, int y, uint16_t hicon)
{
	struct dc *d = dc_get (hdc);
	struct obj *o = obj_get (hicon);
	struct surf s;
	if (!d || !o || o->type != OBJ_ICON || !dc_surface (d, &s))
		return;
	struct obj *img = obj_get (o->image), *m = obj_get (o->mask);
	if (!img || !m)
		return;
	int dx = lx (d, x), dy = ly (d, y);
	for (int yy = 0; yy < o->h; yy++)
		for (int xx = 0; xx < o->w; xx++)
			if (!m->px32[yy * o->w + xx])
				dpixel (&s, dx + xx, dy + yy, img->px32[yy * o->w + xx], 13);
}

/* ======================================================================== l'API */
void api_GetStockObject16 (struct w16call *c)
{
	int i = (int) c->a[0];
	c->ret = (i >= 0 && i <= 17 && objs[i].type) ? OBJ_HANDLE (i) : 0;
}

void api_CreatePen16 (struct w16call *c)
{
	uint16_t h = obj_new (OBJ_PEN);
	struct obj *o = obj_get (h);
	if (o)
	{
		o->style = (int16_t) c->a[0];
		o->width = (int16_t) c->a[1];
		o->color = c->a[2];
	}
	c->ret = h;
}

void api_CreatePenIndirect16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	uint16_t h = obj_new (OBJ_PEN);
	struct obj *o = obj_get (h);
	if (o)
	{
		o->style = (int16_t) rd16 (p);
		o->width = (int16_t) rd16 (p + 2);
		o->color = rd32 (p + 6);
	}
	c->ret = h;
}

static uint16_t brush_new (int style, uint32_t color)
{
	uint16_t h = obj_new (OBJ_BRUSH);
	struct obj *o = obj_get (h);
	if (o)
	{
		o->style = style;
		o->color = color;
	}
	return h;
}

void api_CreateSolidBrush16 (struct w16call *c) { c->ret = brush_new (0, c->a[0]); }
void api_CreateHatchBrush16 (struct w16call *c) { c->ret = brush_new (2, c->a[1]); }

void api_CreatePatternBrush16 (struct w16call *c)
{
	/* (un motif : sa couleur moyenne) */
	struct obj *b = obj_get ((uint16_t) c->a[0]);
	uint32_t r = 0, g = 0, bl = 0, n = 0;
	if (b && b->px32)
		for (int i = 0; i < b->w * b->h; i++, n++)
		{
			r += b->px32[i] >> 16 & 255;
			g += b->px32[i] >> 8 & 255;
			bl += b->px32[i] & 255;
		}
	c->ret = brush_new (0, n ? (bl / n) << 16 | (g / n) << 8 | r / n : 0x808080);
}

void api_CreateBrushIndirect16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	int style = rd16 (p);
	c->ret = brush_new (style == 1 ? 1 : style == 2 ? 2 : 0, rd32 (p + 2));
}

static uint16_t font_new (int h, int weight, int italic, int underline, int pitch, const char *face)
{
	uint16_t hf = obj_new (OBJ_FONT);
	struct obj *f = obj_get (hf);
	if (!f)
		return 0;
	f->height = h;
	f->weight = weight;
	f->italic = italic;
	f->underline = underline;
	f->pitch = pitch;
	snprintf (f->face, sizeof f->face, "%s", face ? face : "");
	font_resolve (f);
	return hf;
}

void api_CreateFont16 (struct w16call *c)
{
	c->ret = font_new ((int16_t) c->a[0], (int16_t) c->a[4], (int) c->a[5], (int) c->a[6], (int) c->a[12], w16_raw (c->a[13]));
}

void api_CreateFontIndirect16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	char face[33];
	memcpy (face, lin_ptr (p + 18), 32);
	face[32] = 0;
	c->ret = font_new ((int16_t) rd16 (p), (int16_t) rd16 (p + 8), lin_ptr (p + 10)[0], lin_ptr (p + 11)[0],
		lin_ptr (p + 17)[0], face);
}

void api_DeleteObject16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	c->ret = 0;
	if (o && !o->stock)
	{
		obj_free (o);
		c->ret = 1;
	}
}

void api_UnrealizeObject16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_SelectObject16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	uint16_t h = (uint16_t) c->a[1];
	struct obj *o = obj_get (h);
	c->ret = 0;
	if (!d || !o)
		return;
	if (o->type == OBJ_REGION)
	{
		d->s.clip_on = 1;
		d->s.cl = lx (d, o->rl); d->s.ct = ly (d, o->rt); d->s.cr = lx (d, o->rr); d->s.cb = ly (d, o->rb);
		c->ret = 2;	/* SIMPLEREGION */
		return;
	}
	uint16_t *slot = o->type == OBJ_PEN ? &d->s.pen : o->type == OBJ_BRUSH ? &d->s.brush : o->type == OBJ_FONT ? &d->s.font
		: o->type == OBJ_BITMAP ? &d->s.bitmap : 0;
	if (!slot || (o->type == OBJ_BITMAP && d->kind != 1))
		return;
	c->ret = *slot ? *slot : h;
	*slot = h;
}

void api_GetObject16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	int n = (int16_t) c->a[1];
	uint32_t p = c->a[2] ? far_lin (c->a[2]) : 0;
	uint8_t buf[64];
	int len = 0;
	memset (buf, 0, sizeof buf);
	if (!o)
	{
		c->ret = 0;
		return;
	}
	switch (o->type)
	{
	case OBJ_PEN:
		buf[0] = (uint8_t) o->style; buf[2] = (uint8_t) o->width; buf[3] = (uint8_t) (o->width >> 8);
		memcpy (buf + 6, &o->color, 4);
		len = 10;
		break;
	case OBJ_BRUSH:
		buf[0] = (uint8_t) (o->style == 1 ? 1 : o->style == 2 ? 2 : 0);
		memcpy (buf + 2, &o->color, 4);
		len = 8;
		break;
	case OBJ_FONT:
		buf[0] = (uint8_t) o->height; buf[1] = (uint8_t) (o->height >> 8);
		buf[8] = (uint8_t) o->weight; buf[9] = (uint8_t) (o->weight >> 8);
		buf[10] = (uint8_t) o->italic; buf[11] = (uint8_t) o->underline;
		buf[17] = (uint8_t) o->pitch;
		memcpy (buf + 18, o->face, 32);
		len = 50;
		break;
	case OBJ_BITMAP:
	{
		int wb = o->bpp == 1 ? ((o->w + 15) / 16) * 2 : ((o->w * 3 + 1) / 2) * 2;
		buf[2] = (uint8_t) o->w; buf[3] = (uint8_t) (o->w >> 8);
		buf[4] = (uint8_t) o->h; buf[5] = (uint8_t) (o->h >> 8);
		buf[6] = (uint8_t) wb; buf[7] = (uint8_t) (wb >> 8);
		buf[8] = 1;
		buf[9] = (uint8_t) (o->bpp == 1 ? 1 : 24);
		len = 14;
		break;
	}
	case OBJ_PALETTE:
		buf[0] = (uint8_t) o->npal; buf[1] = (uint8_t) (o->npal >> 8);
		len = 2;
		break;
	}
	if (p && n > 0)
	{
		if (len > n)
			len = n;
		memcpy (lin_ptr (p), buf, (size_t) len);
	}
	c->ret = (uint32_t) (p ? len : (o->type == OBJ_FONT ? 50 : len));
}

void api_SetTextColor16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = d->s.text_color; d->s.text_color = c->a[1]; }
void api_SetBkColor16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = d->s.bk_color; d->s.bk_color = c->a[1]; }
void api_GetTextColor16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); c->ret = d ? d->s.text_color : 0; }
void api_GetBkColor16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); c->ret = d ? d->s.bk_color : 0; }
void api_SetBkMode16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = (uint32_t) d->s.bk_mode; d->s.bk_mode = (int) c->a[1]; }
void api_GetBkMode16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); c->ret = d ? (uint32_t) d->s.bk_mode : 0; }
void api_SetROP216 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = (uint32_t) d->s.rop2; d->s.rop2 = (int) c->a[1]; }
void api_GetROP216 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); c->ret = d ? (uint32_t) d->s.rop2 : 0; }
void api_SetPolyFillMode16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = (uint32_t) d->s.poly_mode; d->s.poly_mode = (int) c->a[1]; }
void api_SetStretchBltMode16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = (uint32_t) d->s.stretch_mode; d->s.stretch_mode = (int) c->a[1]; }
void api_SetTextAlign16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = (uint32_t) d->s.align; d->s.align = (int) c->a[1]; }
void api_GetTextAlign16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); c->ret = d ? (uint32_t) d->s.align : 0; }
void api_SetMapperFlags16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_MulDiv16 (struct w16call *c)
{
	int16_t b = (int16_t) c->a[2];
	if (!b)
	{
		c->ret = 0xffff;
		return;
	}
	long r = ((long) (int16_t) c->a[0] * (int16_t) c->a[1] + (b / 2)) / b;
	c->ret = (uint16_t) (r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}

/* ---- projection ---- */
static void set_map_mode (struct dc *d, int mode)
{
	d->s.map_mode = mode;
	/* (96 points par pouce ; y vers le haut pour les modes métriques) */
	switch (mode)
	{
	case 2: d->s.wex = 254; d->s.wey = 254; d->s.vex = 96; d->s.vey = -96; break;	/* MM_LOMETRIC */
	case 3: d->s.wex = 2540; d->s.wey = 2540; d->s.vex = 96; d->s.vey = -96; break;	/* MM_HIMETRIC */
	case 4: d->s.wex = 100; d->s.wey = 100; d->s.vex = 96; d->s.vey = -96; break;	/* MM_LOENGLISH */
	case 5: d->s.wex = 1000; d->s.wey = 1000; d->s.vex = 96; d->s.vey = -96; break;	/* MM_HIENGLISH */
	case 6: d->s.wex = 1440; d->s.wey = 1440; d->s.vex = 96; d->s.vey = -96; break;	/* MM_TWIPS */
	case 7: case 8: break;	/* MM_ISOTROPIC, MM_ANISOTROPIC : étendues gardées */
	default: d->s.map_mode = 1; d->s.wex = d->s.wey = d->s.vex = d->s.vey = 1; break;
	}
}

void api_SetMapMode16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) return; c->ret = (uint32_t) d->s.map_mode; set_map_mode (d, (int) c->a[1]); }
void api_GetMapMode16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); c->ret = d ? (uint32_t) d->s.map_mode : 1; }

#define PAIR(a, b) ((uint32_t) (uint16_t) (a) | (uint32_t) (uint16_t) (b) << 16)
static void put_pt (uint32_t p, int x, int y) { if (p) { wr16 (far_lin (p), (uint16_t) x); wr16 (far_lin (p) + 2, (uint16_t) y); } }

static void set_pair (struct w16call *c, int *a, int *b, int ex)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = PAIR (*a, *b);
	if (ex && c->a[3])
		put_pt (c->a[3], *a, *b);
	*a = (int16_t) c->a[1];
	*b = (int16_t) c->a[2];
	if (d->s.map_mode == 7 && d->s.wex && d->s.wey)	/* MM_ISOTROPIC : même échelle */
	{
		long sx = labs ((long) d->s.vex * 1000 / d->s.wex), sy = labs ((long) d->s.vey * 1000 / d->s.wey);
		if (sx < sy)
			d->s.vey = (int) ((long) d->s.vey * sx / (sy ? sy : 1));
		else if (sy < sx)
			d->s.vex = (int) ((long) d->s.vex * sy / (sx ? sx : 1));
	}
	if (ex)
		c->ret = 1;
}

#define DCF(name, fa, fb, ex) void name (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (!d) { c->ret = 0; return; } set_pair (c, &d->s.fa, &d->s.fb, ex); }
DCF (api_SetViewportOrg16, vox, voy, 0)
DCF (api_SetWindowOrg16, wox, woy, 0)
DCF (api_SetViewportExt16, vex, vey, 0)
DCF (api_SetWindowExt16, wex, wey, 0)
DCF (api_SetViewportOrgEx16, vox, voy, 1)
DCF (api_SetWindowOrgEx16, wox, woy, 1)
DCF (api_SetViewportExtEx16, vex, vey, 1)
DCF (api_SetWindowExtEx16, wex, wey, 1)

#define GETF(name, fa, fb) void name (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); c->ret = d ? PAIR (d->s.fa, d->s.fb) : 0; }
#define GETEX(name, fa, fb) void name (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (d) put_pt (c->a[1], d->s.fa, d->s.fb); c->ret = d != 0; }
GETF (api_GetViewportOrg16, vox, voy)
GETF (api_GetWindowOrg16, wox, woy)
GETF (api_GetViewportExt16, vex, vey)
GETF (api_GetWindowExt16, wex, wey)
GETF (api_GetCurrentPosition16, cur_x, cur_y)
GETEX (api_GetViewportOrgEx16, vox, voy)
GETEX (api_GetWindowOrgEx16, wox, woy)
GETEX (api_GetViewportExtEx16, vex, vey)
GETEX (api_GetWindowExtEx16, wex, wey)
GETEX (api_GetCurrentPositionEx16, cur_x, cur_y)

void api_OffsetViewportOrg16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = PAIR (d->s.vox, d->s.voy);
	d->s.vox += (int16_t) c->a[1];
	d->s.voy += (int16_t) c->a[2];
}

void api_OffsetWindowOrg16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = PAIR (d->s.wox, d->s.woy);
	d->s.wox += (int16_t) c->a[1];
	d->s.woy += (int16_t) c->a[2];
}

void api_LPtoDP16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	uint32_t p = far_lin (c->a[1]);
	for (int i = 0; d && i < (int16_t) c->a[2]; i++, p += 4)
	{
		wr16 (p, (uint16_t) lx (d, (int16_t) rd16 (p)));
		wr16 (p + 2, (uint16_t) ly (d, (int16_t) rd16 (p + 2)));
	}
	c->ret = d != 0;
}

void api_DPtoLP16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	uint32_t p = far_lin (c->a[1]);
	for (int i = 0; d && i < (int16_t) c->a[2]; i++, p += 4)
	{
		wr16 (p, (uint16_t) dlx (d, (int16_t) rd16 (p)));
		wr16 (p + 2, (uint16_t) dly (d, (int16_t) rd16 (p + 2)));
	}
	c->ret = d != 0;
}

/* ---- SaveDC, RestoreDC ---- */
void api_SaveDC16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d || d->nsave >= MAX_SAVE)
	{
		c->ret = 0;
		return;
	}
	d->save[d->nsave++] = d->s;
	c->ret = (uint32_t) d->nsave;
}

void api_RestoreDC16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	int n = (int16_t) c->a[1];
	if (!d || !d->nsave)
	{
		c->ret = 0;
		return;
	}
	int to = n < 0 ? d->nsave + n : n - 1;
	if (to < 0 || to >= d->nsave)
	{
		c->ret = 0;
		return;
	}
	d->s = d->save[to];
	d->nsave = to;
	c->ret = 1;
}

/* ---- découpe ---- */
void api_IntersectClipRect16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	int l = lx (d, (int16_t) c->a[1]), t = ly (d, (int16_t) c->a[2]), r = lx (d, (int16_t) c->a[3]), b = ly (d, (int16_t) c->a[4]);
	if (!d->s.clip_on)
	{
		d->s.clip_on = 1;
		d->s.cl = l; d->s.ct = t; d->s.cr = r; d->s.cb = b;
	}
	else
	{
		if (l > d->s.cl) d->s.cl = l;
		if (t > d->s.ct) d->s.ct = t;
		if (r < d->s.cr) d->s.cr = r;
		if (b < d->s.cb) d->s.cb = b;
	}
	c->ret = d->s.cr > d->s.cl && d->s.cb > d->s.ct ? 2 : 1;
}

void api_ExcludeClipRect16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	if (d->s.nex < MAX_EXCL)
	{
		int *e = d->s.ex[d->s.nex++];
		e[0] = lx (d, (int16_t) c->a[1]); e[1] = ly (d, (int16_t) c->a[2]); e[2] = lx (d, (int16_t) c->a[3]); e[3] = ly (d, (int16_t) c->a[4]);
	}
	c->ret = 3;	/* COMPLEXREGION */
}

void api_SelectClipRgn16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct obj *o = obj_get ((uint16_t) c->a[1]);
	if (!d)
		return;
	d->s.nex = 0;
	if (!o)
	{
		d->s.clip_on = 0;
		c->ret = 2;
		return;
	}
	d->s.clip_on = 1;
	d->s.cl = o->rl; d->s.ct = o->rt; d->s.cr = o->rr; d->s.cb = o->rb;
	c->ret = 2;
}

void api_GetClipBox16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	uint32_t p = far_lin (c->a[1]);
	if (!d)
		return;
	int l = 0, t = 0, r = 0, b = 0;
	if (dc_surface (d, &s))
	{
		l = dlx (d, s.cx0 - s.ox); t = dly (d, s.cy0 - s.oy); r = dlx (d, s.cx1 - s.ox); b = dly (d, s.cy1 - s.oy);
	}
	else if (d->kind == 2)
	{
		r = 640;
		b = 480;
	}
	wr16 (p, (uint16_t) l); wr16 (p + 2, (uint16_t) t); wr16 (p + 4, (uint16_t) r); wr16 (p + 6, (uint16_t) b);
	c->ret = r > l && b > t ? 2 : 1;
}

void api_RectVisible16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_PtVisible16 (struct w16call *c) { (void) c; c->ret = 1; }

/* ---- formes ---- */
static void shape_rect (struct dc *d, struct surf *s, int l, int t, int r, int b)
{
	uint32_t rgb;
	int w;
	if (brush_of (d, &rgb))
		dfill (s, l + 1, t + 1, r - 1, b - 1, rgb, d->s.rop2);
	if (pen_of (d, &rgb, &w))
	{
		dfill (s, l, t, r, t + w, rgb, d->s.rop2);
		dfill (s, l, b - w, r, b, rgb, d->s.rop2);
		dfill (s, l, t + w, l + w, b - w, rgb, d->s.rop2);
		dfill (s, r - w, t + w, r, b - w, rgb, d->s.rop2);
	}
	else if (brush_of (d, &rgb))
		dfill (s, l, t, r, b, rgb, d->s.rop2);
}

void api_Rectangle16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = 1;
	if (!d || !dc_surface (d, &s))
		return;
	shape_rect (d, &s, lx (d, (int16_t) c->a[1]), ly (d, (int16_t) c->a[2]), lx (d, (int16_t) c->a[3]), ly (d, (int16_t) c->a[4]));
}

/* une ellipse (et ses morceaux : angle de départ et d'arrivée par deux points, sens trigonométrique) */
static void ellipse (struct dc *d, struct surf *s, int l, int t, int r, int b, int kind, int x3, int y3, int x4, int y4)
{
	if (l > r) { int k = l; l = r; r = k; }
	if (t > b) { int k = t; t = b; b = k; }
	double cx = (l + r) / 2.0, cy = (t + b) / 2.0, rx = (r - l) / 2.0, ry = (b - t) / 2.0;
	if (rx <= 0 || ry <= 0)
		return;
	uint32_t fillc, penc;
	int w;
	int hasb = brush_of (d, &fillc), hasp = pen_of (d, &penc, &w);
	if (kind == 0)
	{
		if (hasb)
			for (int y = t; y < b; y++)
			{
				double dy = (y + 0.5 - cy) / ry;
				if (dy * dy > 1)
					continue;
				double half = rx * sqrt (1 - dy * dy);
				dfill (s, (int) floor (cx - half + 0.5), y, (int) floor (cx + half + 0.5), y + 1, fillc, d->s.rop2);
			}
		if (hasp)
		{
			int n = (int) (2 * M_PI * (rx > ry ? rx : ry)) + 8, px = 0, py = 0;
			for (int i = 0; i <= n; i++)
			{
				double a = 2 * M_PI * i / n;
				int x = (int) floor (cx + (rx - 0.5) * cos (a) + 0.5), y = (int) floor (cy - (ry - 0.5) * sin (a) + 0.5);
				if (i)
					dline (s, px, py, x, y, penc, w, d->s.rop2);
				px = x;
				py = y;
			}
		}
		return;
	}
	double a0 = atan2 (-(y3 - cy) / ry, (x3 - cx) / rx), a1 = atan2 (-(y4 - cy) / ry, (x4 - cx) / rx);
	if (a1 <= a0)
		a1 += 2 * M_PI;
	int n = (int) ((a1 - a0) * (rx > ry ? rx : ry)) + 4;
	int (*pts)[2] = malloc (sizeof (int[2]) * (size_t) (n + 3));
	int k = 0;
	if (kind == 2)	/* Pie */
	{
		pts[k][0] = (int) cx;
		pts[k++][1] = (int) cy;
	}
	for (int i = 0; i <= n; i++)
	{
		double a = a0 + (a1 - a0) * i / n;
		pts[k][0] = (int) floor (cx + rx * cos (a) + 0.5);
		pts[k++][1] = (int) floor (cy - ry * sin (a) + 0.5);
	}
	if (kind >= 2 && hasb)
		dpoly_fill (s, pts, k, fillc, 1);
	if (hasp)
	{
		int start = kind == 2 ? 0 : 0;
		for (int i = start; i + 1 < k; i++)
			dline (s, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], penc, w, d->s.rop2);
		if (kind >= 2)
			dline (s, pts[k - 1][0], pts[k - 1][1], pts[0][0], pts[0][1], penc, w, d->s.rop2);
	}
	free (pts);
}

#define ELL(name, kind) void name (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); struct surf s; c->ret = 1; \
	if (!d || !dc_surface (d, &s)) return; ellipse (d, &s, lx (d, (int16_t) c->a[1]), ly (d, (int16_t) c->a[2]), lx (d, (int16_t) c->a[3]), \
	ly (d, (int16_t) c->a[4]), kind, lx (d, (int16_t) c->a[5]), ly (d, (int16_t) c->a[6]), lx (d, (int16_t) c->a[7]), ly (d, (int16_t) c->a[8])); }
void api_Ellipse16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = 1;
	if (d && dc_surface (d, &s))
		ellipse (d, &s, lx (d, (int16_t) c->a[1]), ly (d, (int16_t) c->a[2]), lx (d, (int16_t) c->a[3]), ly (d, (int16_t) c->a[4]), 0, 0, 0, 0, 0);
}
ELL (api_Arc16, 1)
ELL (api_Pie16, 2)
ELL (api_Chord16, 3)

void api_RoundRect16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = 1;
	if (!d || !dc_surface (d, &s))
		return;
	int l = lx (d, (int16_t) c->a[1]), t = ly (d, (int16_t) c->a[2]), r = lx (d, (int16_t) c->a[3]), b = ly (d, (int16_t) c->a[4]);
	int ew = lw (d, (int16_t) c->a[5]) / 2, eh = lw (d, (int16_t) c->a[6]) / 2;
	if (ew <= 0 || eh <= 0)
	{
		shape_rect (d, &s, l, t, r, b);
		return;
	}
	int n = 0;
	int (*pts)[2] = malloc (sizeof (int[2]) * 4 * 34);
	const double q = M_PI / 2;
	double cxs[4] = { r - ew, l + ew, l + ew, r - ew }, cys[4] = { t + eh, t + eh, b - eh, b - eh };
	for (int k = 0; k < 4; k++)
		for (int i = 0; i <= 32; i++)
		{
			double a = q * k + q * i / 32;
			pts[n][0] = (int) floor (cxs[k] + ew * cos (a) + 0.5);
			pts[n++][1] = (int) floor (cys[k] - eh * sin (a) + 0.5);
		}
	uint32_t rgb;
	int w;
	if (brush_of (d, &rgb))
		dpoly_fill (&s, pts, n, rgb, 1);
	if (pen_of (d, &rgb, &w))
		for (int i = 0; i < n; i++)
			dline (&s, pts[i][0], pts[i][1], pts[(i + 1) % n][0], pts[(i + 1) % n][1], rgb, w, d->s.rop2);
	free (pts);
}

static int read_points (struct dc *d, uint32_t p, int n, int (**out)[2])
{
	if (n <= 0 || n > 16384)
		return 0;
	int (*pts)[2] = malloc (sizeof (int[2]) * (size_t) n);
	uint32_t a = far_lin (p);
	for (int i = 0; i < n; i++, a += 4)
	{
		pts[i][0] = lx (d, (int16_t) rd16 (a));
		pts[i][1] = ly (d, (int16_t) rd16 (a + 2));
	}
	*out = pts;
	return n;
}

void api_Polygon16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	int (*pts)[2];
	c->ret = 1;
	if (!d || !dc_surface (d, &s))
		return;
	int n = read_points (d, c->a[1], (int16_t) c->a[2], &pts);
	if (!n)
		return;
	uint32_t rgb;
	int w;
	if (brush_of (d, &rgb))
		dpoly_fill (&s, pts, n, rgb, d->s.poly_mode);
	if (pen_of (d, &rgb, &w))
		for (int i = 0; i < n; i++)
			dline (&s, pts[i][0], pts[i][1], pts[(i + 1) % n][0], pts[(i + 1) % n][1], rgb, w, d->s.rop2);
	free (pts);
}

void api_Polyline16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	int (*pts)[2];
	c->ret = 1;
	if (!d || !dc_surface (d, &s))
		return;
	int n = read_points (d, c->a[1], (int16_t) c->a[2], &pts);
	uint32_t rgb;
	int w;
	if (n && pen_of (d, &rgb, &w))
		for (int i = 0; i + 1 < n; i++)
			dline (&s, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], rgb, w, d->s.rop2);
	if (n)
		free (pts);
}

void api_MoveTo16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = PAIR (d->s.cur_x, d->s.cur_y);
	d->s.cur_x = (int16_t) c->a[1];
	d->s.cur_y = (int16_t) c->a[2];
}

void api_MoveToEx16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	put_pt (c->a[3], d->s.cur_x, d->s.cur_y);
	d->s.cur_x = (int16_t) c->a[1];
	d->s.cur_y = (int16_t) c->a[2];
	c->ret = 1;
}

void api_LineTo16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = 1;
	if (!d)
		return;
	int x = (int16_t) c->a[1], y = (int16_t) c->a[2];
	uint32_t rgb;
	int w;
	if (dc_surface (d, &s) && pen_of (d, &rgb, &w))
		dline (&s, lx (d, d->s.cur_x), ly (d, d->s.cur_y), lx (d, x), ly (d, y), rgb, w, d->s.rop2);
	d->s.cur_x = x;
	d->s.cur_y = y;
}

void api_SetPixel16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = c->a[3];
	if (d && dc_surface (d, &s))
		dpixel (&s, lx (d, (int16_t) c->a[1]), ly (d, (int16_t) c->a[2]), rgb_of (d, c->a[3]), 13);
}

void api_GetPixel16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = 0xffffffff;
	if (!d || !dc_surface (d, &s))
		return;
	int x = lx (d, (int16_t) c->a[1]) + s.ox, y = ly (d, (int16_t) c->a[2]) + s.oy;
	if (x < 0 || y < 0 || x >= s.w || y >= s.h)
		return;
	uint32_t p = s.px[(size_t) y * (size_t) s.stride + (size_t) x];
	c->ret = ((p >> 16) & 0xff) | (p & 0xff00) | (p & 0xff) << 16;
}

void api_FloodFill16 (struct w16call *c) { (void) c; c->ret = 0; }

/* ---- texte ---- */
static void text_at (struct dc *d, int x, int y, const uint8_t *str, int n, int opaque_rect, int *rect)
{
	struct surf s;
	if (!dc_surface (d, &s))
		return;
	struct obj *f = font_of (d, 0);
	char buf[2048];
	w16_to_utf8 (str, n, buf, sizeof buf);
	int w = text_extent (f, buf), h = be_font_height (f->px);
	int dx = lx (d, x), dy = ly (d, y);
	if ((d->s.align & 6) == 6)	/* TA_CENTER */
		dx -= w / 2;
	else if (d->s.align & 2)	/* TA_RIGHT */
		dx -= w;
	if ((d->s.align & 24) == 24)	/* TA_BASELINE */
		dy -= be_font_ascent (f->px);
	else if (d->s.align & 8)	/* TA_BOTTOM */
		dy -= h;
	if (opaque_rect && rect)
		dfill (&s, lx (d, rect[0]), ly (d, rect[1]), lx (d, rect[2]), ly (d, rect[3]), rgb_of (d, d->s.bk_color), 13);
	dtext (d, &s, dx, dy, str, n, rgb_of (d, d->s.text_color), f, d->s.bk_mode == 2 && !opaque_rect,
		rgb_of (d, d->s.bk_color));
	if (d->s.align & 1)	/* TA_UPDATECP */
		d->s.cur_x += w;
}

void api_TextOut16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 1;
	if (!d)
		return;
	int x = (int16_t) c->a[1], y = (int16_t) c->a[2];
	if (d->s.align & 1)
	{
		x = d->s.cur_x;
		y = d->s.cur_y;
	}
	text_at (d, x, y, far_ptr (c->a[3]), (int) (int16_t) c->a[4], 0, 0);
}

void api_ExtTextOut16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 1;
	if (!d)
		return;
	int rect[4] = { 0, 0, 0, 0 }, has = 0;
	if (c->a[4])
	{
		uint32_t r = far_lin (c->a[4]);
		for (int i = 0; i < 4; i++)
			rect[i] = (int16_t) rd16 (r + 2 * (uint32_t) i);
		has = 1;
	}
	int clip = has && (c->a[3] & 4);	/* ETO_CLIPPED */
	struct dcstate save = d->s;
	if (clip)
	{
		d->s.clip_on = 1;
		d->s.cl = lx (d, rect[0]); d->s.ct = ly (d, rect[1]); d->s.cr = lx (d, rect[2]); d->s.cb = ly (d, rect[3]);
	}
	text_at (d, (int16_t) c->a[1], (int16_t) c->a[2], c->a[5] ? far_ptr (c->a[5]) : (const uint8_t *) "", (int16_t) c->a[6],
		has && (c->a[3] & 2), rect);	/* ETO_OPAQUE */
	int cx = d->s.cur_x;
	d->s = save;
	d->s.cur_x = cx;
}

/* TabbedTextOut : les tabulations vont au taquet suivant (aucun : tous les 8 caractères moyens ; un seul : un pas
 * régulier ; plusieurs : les positions, depuis nTabOrigin) ; rend la largeur et la hauteur du texte */
static uint32_t tabbed_text (struct dc *d, int x0, int y, const uint8_t *str, int n, int ntabs, uint32_t tabs, int origin);

void api_TabbedTextOut16 (struct w16call *c)
{
	c->ret = tabbed_text (dc_get ((uint16_t) c->a[0]), (int16_t) c->a[1], (int16_t) c->a[2],
		c->a[3] ? far_ptr (c->a[3]) : (const uint8_t *) "", (int16_t) c->a[4], (int16_t) c->a[5],
		c->a[6] ? far_lin (c->a[6]) : 0, (int16_t) c->a[7]);
}

/* GetTabbedTextExtent (hdc, texte, longueur, nombre de taquets, taquets) : la mesure de TabbedTextOut sans tracer */
void api_GetTabbedTextExtent16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct dc probe;
	if (d)
	{
		probe = *d;
		probe.kind = 2;	/* (un DC d'information : rien n'est tracé) */
	}
	c->ret = tabbed_text (d ? &probe : 0, 0, 0, c->a[1] ? far_ptr (c->a[1]) : (const uint8_t *) "", (int16_t) c->a[2],
		(int16_t) c->a[3], c->a[4] ? far_lin (c->a[4]) : 0, 0);
}

static uint32_t tabbed_text (struct dc *d, int x0, int y, const uint8_t *str, int n, int ntabs, uint32_t tabs, int origin)
{
	struct obj *f = d ? font_of (d, 0) : &objs[13];
	int h = be_font_height (f->px);
	int step = 8 * (text_extent (f, "x") > 0 ? text_extent (f, "x") : 8);
	if (ntabs == 1 && tabs && (int16_t) rd16 (tabs) > 0)
		step = (int16_t) rd16 (tabs);
	int x = x0;
	struct dcstate save = d ? d->s : (struct dcstate) { 0 };
	if (d)
		d->s.align &= ~1;	/* (TA_UPDATECP : la position courante se règle à la fin) */
	for (int i = 0; i < n;)
	{
		int j = i;
		while (j < n && str[j] != '\t')
			j++;
		char buf[2048];
		w16_to_utf8 (str + i, j - i, buf, sizeof buf);
		if (d && j > i)
			text_at (d, x, y, str + i, j - i, 0, 0);
		x += text_extent (f, buf);
		if (j >= n)
			break;
		/* le taquet suivant */
		int rel = x - origin, next = -1;
		if (ntabs > 1 && tabs)
			for (int k = 0; k < ntabs; k++)
			{
				int t = (int16_t) rd16 (tabs + 2 * (uint32_t) k);
				if (t > rel)
				{
					next = t;
					break;
				}
			}
		if (next < 0)
			next = (rel / step + 1) * step;
		x = origin + next;
		i = j + 1;
	}
	if (d)
	{
		int cx = save.cur_x + (x - x0);
		d->s.align = save.align;
		if (d->s.align & 1)
			d->s.cur_x = cx;
	}
	return PAIR (x - x0, h);
}

void api_SetBrushOrg16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 0;
	if (!d)
		return;
	c->ret = PAIR (d->s.brush_x, d->s.brush_y);
	d->s.brush_x = (int16_t) c->a[1];
	d->s.brush_y = (int16_t) c->a[2];
}

void api_GetTextExtent16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct obj *f = d ? font_of (d, 0) : &objs[13];
	char buf[2048];
	w16_to_utf8 (far_ptr (c->a[1]), (int16_t) c->a[2], buf, sizeof buf);
	c->ret = PAIR (text_extent (f, buf), be_font_height (f->px));
}

void api_GetTextExtentPoint16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct obj *f = d ? font_of (d, 0) : &objs[13];
	char buf[2048];
	w16_to_utf8 (far_ptr (c->a[1]), (int16_t) c->a[2], buf, sizeof buf);
	put_pt (c->a[3], text_extent (f, buf), be_font_height (f->px));
	c->ret = 1;
}

static void text_metrics (struct obj *f, uint8_t *t)
{
	memset (t, 0, 31);
	int h = be_font_height (f->px), a = be_font_ascent (f->px);
	int ave = be_text_width ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", f->px, f->fstyle) / 52;
	int mx = be_text_width ("W", f->px, f->fstyle);
	if (ave < 1)
		ave = 1;
#define W16(o, v) do { t[o] = (uint8_t) (v); t[(o) + 1] = (uint8_t) ((v) >> 8); } while (0)
	W16 (0, h);
	W16 (2, a);
	W16 (4, h - a);
	W16 (6, h - f->px > 0 ? h - f->px : 0);
	W16 (8, 0);
	W16 (10, ave);
	W16 (12, mx);
	W16 (14, f->weight ? f->weight : (f->fstyle == 1 || f->fstyle == 4 ? 700 : 400));
	t[16] = (uint8_t) f->italic;
	t[17] = (uint8_t) f->underline;
	t[19] = 0x20;
	t[20] = 0xff;
	t[21] = 0x80;
	t[22] = 0x20;
	t[23] = (uint8_t) (f->fstyle == 2 ? 0x30 : (f->fstyle >= 3 && f->fstyle <= 4) ? 0x17 : 0x27);	/* (bit 0 : chasse variable) */
	t[24] = 0;
	W16 (27, 96);
	W16 (29, 96);
#undef W16
}

void api_GetTextMetrics16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	text_metrics (d ? font_of (d, 0) : &objs[13], lin_ptr (far_lin (c->a[1])));
	c->ret = 1;
}

void api_GetTextFace16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct obj *f = d ? font_of (d, 0) : &objs[13];
	const char *face = f->face[0] ? f->face : "System";
	int n = (int16_t) c->a[1];
	int l = (int) strlen (face);
	if (l > n - 1)
		l = n - 1;
	if (l > 0)
	{
		memcpy (far_ptr (c->a[2]), face, (size_t) l);
		((char *) far_ptr (c->a[2]))[l] = 0;
	}
	c->ret = (uint32_t) (l > 0 ? l : 0);
}

void api_GetTextCharacterExtra16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_SetTextCharacterExtra16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_SetTextJustification16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_GetCharWidth16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct obj *f = d ? font_of (d, 0) : &objs[13];
	uint32_t p = far_lin (c->a[3]);
	for (int ch = (int) c->a[1]; ch <= (int) c->a[2] && ch < 256; ch++, p += 2)
	{
		uint8_t s1[2] = { (uint8_t) ch, 0 };
		char b[8];
		w16_to_utf8 (s1, 1, b, sizeof b);
		wr16 (p, (uint16_t) text_extent (f, b));
	}
	c->ret = 1;
}

/* EnumFonts : quelques polices de Windows 3.1, vues par le programme (LOGFONT, TEXTMETRIC, type) */
void api_EnumFonts16 (struct w16call *c)
{
	static const char *faces[] = { "Arial", "Courier New", "Times New Roman", "MS Sans Serif", "System", "Courier" };
	uint32_t proc = c->a[2], data = c->a[3];
	const char *want = c->a[1] ? w16_raw (c->a[1]) : 0;
	uint16_t sel = global_alloc (128, 0);
	uint32_t lf = sel_base (sel), tm = lf + 64;
	c->ret = 1;
	for (int i = 0; i < 6; i++)
	{
		if (want && *want && strcasecmp (want, faces[i]))
			continue;
		memset (lin_ptr (lf), 0, 128);
		struct obj f;
		memset (&f, 0, sizeof f);
		f.height = -13;
		snprintf (f.face, sizeof f.face, "%s", faces[i]);
		font_resolve (&f);
		wr16 (lf, (uint16_t) be_font_height (f.px));
		wr16 (lf + 8, 400);
		lin_ptr (lf + 17)[0] = (uint8_t) (f.fstyle == 2 ? 0x31 : f.fstyle >= 3 ? 0x12 : 0x22);
		snprintf ((char *) lin_ptr (lf + 18), 32, "%s", faces[i]);
		text_metrics (&f, lin_ptr (tm));
		uint16_t args[8] = { sel, 0, sel, 64, (uint16_t) (i < 3 ? 4 : 1), (uint16_t) (data >> 16), (uint16_t) data };
		/* (FONTENUMPROC : LOGFONT far *, TEXTMETRIC far *, int type, LPARAM) */
		uint16_t a[7] = { sel, 0, sel, 64, args[4], (uint16_t) (data >> 16), (uint16_t) data };
		c->ret = call16 (proc, a, 7, ne_ds_for (proc)) & 0xffff;
		if (!c->ret)
			break;
	}
	global_free (sel);
}

/* ---- bitmaps et DC mémoire ---- */
void api_CreateCompatibleDC16 (struct w16call *c)
{
	uint16_t h = dc_new (1, 0);
	struct dc *d = dc_get (h);
	d->s.bitmap = bitmap_new (1, 1, 1);
	c->ret = h;
}

void api_CreateDC16 (struct w16call *c)
{
	const char *drv = w16_raw (c->a[0]);
	uint16_t h = dc_new (strcasecmp (drv, "DISPLAY") ? 2 : 1, 0);
	struct dc *d = dc_get (h);
	if (d->kind == 1)
		d->s.bitmap = bitmap_new (1, 1, 24);
	c->ret = h;
}

void api_CreateIC16 (struct w16call *c) { (void) c; c->ret = dc_new (2, 0); }

void api_DeleteDC16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (d)
		d->used = 0;
	c->ret = d != 0;
}

void api_CreateBitmap16 (struct w16call *c)
{
	int w = (int16_t) c->a[0], h = (int16_t) c->a[1], bpp = (int) c->a[3] * (int) c->a[2];
	uint16_t hb = bitmap_new (w, h, bpp <= 1 ? 1 : 24);
	struct obj *b = obj_get (hb);
	if (b && c->a[4])
	{
		/* (bits de l'appareil : 1 bit, ou 8 bits par composante) */
		uint8_t *src = far_ptr (c->a[4]);
		int wb = bpp <= 1 ? ((w + 15) / 16) * 2 : 0;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				b->px32[y * w + x] = bpp <= 1 ? ((src[y * wb + (x >> 3)] >> (7 - (x & 7))) & 1 ? 0xffffff : 0) : 0;
	}
	c->ret = hb;
}

void api_CreateBitmapIndirect16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	struct w16call d = { .a = { rd16 (p + 2), rd16 (p + 4), lin_ptr (p + 8)[0], lin_ptr (p + 9)[0], rd32 (p + 10) }, .n = 5 };
	api_CreateBitmap16 (&d);
	c->ret = d.ret;
}

void api_CreateCompatibleBitmap16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	int mono = d && d->kind == 1 && obj_get (d->s.bitmap) && obj_get (d->s.bitmap)->bpp == 1;
	c->ret = bitmap_new ((int16_t) c->a[1], (int16_t) c->a[2], mono ? 1 : 24);
}

void api_CreateDiscardableBitmap16 (struct w16call *c) { api_CreateCompatibleBitmap16 (c); }

void api_CreateDIBitmap16 (struct w16call *c)
{
	const uint8_t *bih = far_ptr (c->a[1]);
	int w, h, bpp;
	dib_size (bih, &w, &h, &bpp);
	uint16_t hb = bitmap_new (w, h, 24);
	struct obj *b = obj_get (hb);
	if (b && (c->a[2] & 4) && c->a[3] && c->a[4])	/* CBM_INIT */
		dib_decode (far_ptr (c->a[4]), lin_ptr (far_lin (c->a[3])), b->px32, w, h, 0, h, 1);
	c->ret = hb;
}

void api_SetDIBits16 (struct w16call *c)
{
	struct obj *b = obj_get ((uint16_t) c->a[1]);
	if (!b || !b->px32)
	{
		c->ret = 0;
		return;
	}
	c->ret = (uint32_t) dib_decode (far_ptr (c->a[5]), lin_ptr (far_lin (c->a[4])), b->px32, b->w, b->h, (int) c->a[2], (int) c->a[3], 1);
}

void api_GetDIBits16 (struct w16call *c)
{
	struct obj *b = obj_get ((uint16_t) c->a[1]);
	uint8_t *bi = far_ptr (c->a[5]);
	if (!b || !b->px32 || !bi)
	{
		c->ret = 0;
		return;
	}
	int start = (int) c->a[2], lines = (int) c->a[3];
	uint32_t hsz = bi[0] | bi[1] << 8;
	if (hsz < 40)
	{
		c->ret = 0;
		return;
	}
	/* (on rend du 24 bits, de bas en haut) */
	memset (bi + 4, 0, 36);
	memcpy (bi + 4, &b->w, 4);
	memcpy (bi + 8, &b->h, 4);
	bi[12] = 1;
	bi[14] = 24;
	int stride = ((b->w * 24 + 31) / 32) * 4;
	uint32_t img = (uint32_t) stride * (uint32_t) b->h;
	memcpy (bi + 20, &img, 4);
	if (!c->a[4])
	{
		c->ret = (uint32_t) b->h;
		return;
	}
	uint8_t *out = lin_ptr (far_lin (c->a[4]));
	for (int l = start; l < start + lines && l < b->h; l++)
	{
		uint8_t *row = out + (size_t) (l - start) * (size_t) stride;
		uint32_t *src = b->px32 + (size_t) (b->h - 1 - l) * (size_t) b->w;
		for (int x = 0; x < b->w; x++)
		{
			row[3 * x] = (uint8_t) src[x];
			row[3 * x + 1] = (uint8_t) (src[x] >> 8);
			row[3 * x + 2] = (uint8_t) (src[x] >> 16);
		}
	}
	c->ret = (uint32_t) lines;
}

void api_GetBitmapBits16 (struct w16call *c)
{
	struct obj *b = obj_get ((uint16_t) c->a[0]);
	int32_t n = (int32_t) c->a[1];
	if (!b || !b->px32)
	{
		c->ret = 0;
		return;
	}
	uint8_t *out = lin_ptr (far_lin (c->a[2]));
	int wb = b->bpp == 1 ? ((b->w + 15) / 16) * 2 : ((b->w * 3 + 1) / 2) * 2;
	int32_t total = wb * b->h;
	if (n > total)
		n = total;
	memset (out, 0, (size_t) n);
	for (int y = 0; y < b->h; y++)
		for (int x = 0; x < b->w; x++)
		{
			uint32_t p = b->px32[y * b->w + x];
			if (b->bpp == 1)
			{
				int o = y * wb + (x >> 3);
				if (o < n && (p & 0xffffff))
					out[o] |= (uint8_t) (0x80 >> (x & 7));
			}
			else
			{
				int o = y * wb + 3 * x;
				if (o + 2 < n)
				{
					out[o] = (uint8_t) p;
					out[o + 1] = (uint8_t) (p >> 8);
					out[o + 2] = (uint8_t) (p >> 16);
				}
			}
		}
	c->ret = (uint32_t) n;
}

void api_SetBitmapBits16 (struct w16call *c)
{
	struct obj *b = obj_get ((uint16_t) c->a[0]);
	int32_t n = (int32_t) c->a[1];
	if (!b || !b->px32)
	{
		c->ret = 0;
		return;
	}
	const uint8_t *in = lin_ptr (far_lin (c->a[2]));
	int wb = b->bpp == 1 ? ((b->w + 15) / 16) * 2 : ((b->w * 3 + 1) / 2) * 2;
	for (int y = 0; y < b->h; y++)
		for (int x = 0; x < b->w; x++)
		{
			if (b->bpp == 1)
			{
				int o = y * wb + (x >> 3);
				if (o < n)
					b->px32[y * b->w + x] = (in[o] >> (7 - (x & 7))) & 1 ? 0xffffff : 0;
			}
			else
			{
				int o = y * wb + 3 * x;
				if (o + 2 < n)
					b->px32[y * b->w + x] = (uint32_t) (in[o] | in[o + 1] << 8 | in[o + 2] << 16);
			}
		}
	c->ret = (uint32_t) n;
}

/* l'opération raster rop3 (octet du haut du code de BitBlt) sur des pixels de 24 bits */
static inline uint32_t rop3 (int r, uint32_t P, uint32_t S, uint32_t D)
{
	switch (r)
	{
	case 0xcc: return S;
	case 0xf0: return P;
	case 0x00: return 0;
	case 0xff: return 0xffffff;
	case 0x66: return S ^ D;
	case 0x88: return S & D;
	case 0xee: return S | D;
	case 0x33: return ~S & 0xffffff;
	case 0x55: return ~D & 0xffffff;
	case 0x5a: return P ^ D;
	case 0xbb: return (~S | D) & 0xffffff;
	case 0x44: return S & ~D & 0xffffff;
	case 0x11: return ~(S | D) & 0xffffff;
	case 0xc0: return P & S;
	case 0xb8: return ((P ^ D) & S) ^ P;	/* (motif sous masque) */
	case 0xe2: return ((D ^ P) & S) ^ D;
	}
	uint32_t out = 0;
	for (int i = 0; i < 8; i++)
		if ((r >> i) & 1)
			out |= ((i & 4) ? P : ~P) & ((i & 2) ? S : ~S) & ((i & 1) ? D : ~D);
	return out & 0xffffff;
}

/* une copie de bloc, étirée si les tailles diffèrent ; src = 0 : sans source (PatBlt) */
static void blit (struct dc *d, int dx, int dy, int dw, int dh, struct dc *sd, int sx, int sy, int sw, int sh, uint32_t rop)
{
	struct surf s, ss;
	if (!dc_surface (d, &s))
		return;
	int r = (int) ((rop >> 16) & 0xff);
	uint32_t pat = 0;
	brush_of (d, &pat);
	int usesrc = ((r >> 2) & 0x33) != (r & 0x33);
	int has_src = usesrc && sd && dc_surface (sd, &ss);
	if (usesrc && !has_src)
		return;
	/* le rectangle de destination en appareil, retourné si la largeur est négative */
	int X0 = lx (d, dx), Y0 = ly (d, dy), X1 = lx (d, dx + dw), Y1 = ly (d, dy + dh);
	int SX0 = has_src ? lx (sd, sx) : 0, SY0 = has_src ? ly (sd, sy) : 0;
	int SX1 = has_src ? lx (sd, sx + sw) : 0, SY1 = has_src ? ly (sd, sy + sh) : 0;
	int flipx = (X1 < X0) != (SX1 < SX0), flipy = (Y1 < Y0) != (SY1 < SY0);
	if (X1 < X0) { int k = X0; X0 = X1; X1 = k; }
	if (Y1 < Y0) { int k = Y0; Y0 = Y1; Y1 = k; }
	if (SX1 < SX0) { int k = SX0; SX0 = SX1; SX1 = k; }
	if (SY1 < SY0) { int k = SY0; SY0 = SY1; SY1 = k; }
	int W = X1 - X0, H = Y1 - Y0, SW = SX1 - SX0, SH = SY1 - SY0;
	if (W <= 0 || H <= 0)
		return;
	struct obj *sbm = has_src && sd->kind == 1 ? obj_get (sd->s.bitmap) : 0;
	struct obj *dbm = d->kind == 1 ? obj_get (d->s.bitmap) : 0;
	int src_mono = sbm && sbm->bpp == 1, dst_mono = dbm && dbm->bpp == 1;
	uint32_t tc = rgb_of (d, d->s.text_color), bc = rgb_of (d, d->s.bk_color);
	uint32_t sbk = has_src ? rgb_of (sd, sd->s.bk_color) : 0;
	int rop2i = 13;
	(void) rop2i;
	for (int y = 0; y < H; y++)
	{
		int py = Y0 + y + s.oy;
		if (py < s.cy0 || py >= s.cy1)
			continue;
		uint32_t *drow = s.px + (size_t) py * (size_t) s.stride;
		int syy = 0;
		if (has_src)
		{
			syy = SH == H ? y : (int) ((long) y * SH / H);
			if (flipy)
				syy = SH - 1 - syy;
			syy += SY0 + ss.oy;
			if (syy < 0 || syy >= ss.h)
				continue;
		}
		for (int x = 0; x < W; x++)
		{
			int px = X0 + x + s.ox;
			if (px < s.cx0 || px >= s.cx1)
				continue;
			if (d->s.nex && excluded (&s, px, py))
				continue;
			uint32_t S = 0;
			if (has_src)
			{
				int sxx = SW == W ? x : (int) ((long) x * SW / W);
				if (flipx)
					sxx = SW - 1 - sxx;
				sxx += SX0 + ss.ox;
				if (sxx < 0 || sxx >= ss.w)
					continue;
				S = ss.px[(size_t) syy * (size_t) ss.stride + (size_t) sxx] & 0xffffff;
				if (src_mono && !dst_mono)
					S = S ? bc : tc;	/* (monochrome -> couleur : 1 = fond, 0 = texte) */
				else if (dst_mono && !src_mono)
					S = S == sbk ? 0xffffff : 0;
			}
			drow[px] = rop3 (r, pat, S, drow[px] & 0xffffff);
		}
	}
	touch (&s, X0 + s.ox, Y0 + s.oy, X1 + s.ox, Y1 + s.oy);
}

void api_BitBlt16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 1;
	if (d)
		blit (d, (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3], (int16_t) c->a[4], dc_get ((uint16_t) c->a[5]),
			(int16_t) c->a[6], (int16_t) c->a[7], (int16_t) c->a[3], (int16_t) c->a[4], c->a[8]);
}

void api_StretchBlt16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 1;
	if (d)
		blit (d, (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3], (int16_t) c->a[4], dc_get ((uint16_t) c->a[5]),
			(int16_t) c->a[6], (int16_t) c->a[7], (int16_t) c->a[8], (int16_t) c->a[9], c->a[10]);
}

void api_PatBlt16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 1;
	if (d)
		blit (d, (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3], (int16_t) c->a[4], 0, 0, 0, 0, 0, c->a[5]);
}

/* un DIB vers l'appareil (SetDIBitsToDevice, StretchDIBits) : décodé dans un bitmap temporaire, puis copié */
static void dib_to_device (struct dc *d, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh,
	const uint8_t *bi, const uint8_t *bits, uint32_t rop)
{
	int w, h, bpp;
	dib_size (bi, &w, &h, &bpp);
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
		return;
	uint16_t hb = bitmap_new (w, h, 24);
	dib_decode (bi, bits, obj_get (hb)->px32, w, h, 0, h, 1);
	uint16_t hm = dc_new (1, 0);
	struct dc *m = dc_get (hm);
	m->s.bitmap = hb;
	/* (sy compte depuis le bas dans un DIB de bas en haut) */
	int top = h - sy - sh;
	blit (d, dx, dy, dw, dh, m, sx, top < 0 ? 0 : top, sw, sh, rop);
	m->used = 0;
	obj_free (obj_get (hb));
}

void api_SetDIBitsToDevice16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 0;
	if (!d)
		return;
	const uint8_t *bi = far_ptr (c->a[10]);
	int w, h, bpp;
	dib_size (bi, &w, &h, &bpp);
	dib_to_device (d, (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3], (int16_t) c->a[4], (int16_t) c->a[5],
		(int16_t) c->a[6], (int16_t) c->a[3], (int16_t) c->a[4], bi, lin_ptr (far_lin (c->a[9])), 0xcc0020);
	c->ret = c->a[8];
}

void api_StretchDIBits16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 0;
	if (!d)
		return;
	dib_to_device (d, (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3], (int16_t) c->a[4], (int16_t) c->a[5],
		(int16_t) c->a[6], (int16_t) c->a[7], (int16_t) c->a[8], far_ptr (c->a[10]), lin_ptr (far_lin (c->a[9])), c->a[12]);
	c->ret = c->a[8];
}

/* ---- palettes : l'écran est en 24 bits, une palette ne fait que traduire PALETTEINDEX ---- */
void api_CreatePalette16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	uint16_t h = obj_new (OBJ_PALETTE);
	struct obj *o = obj_get (h);
	int n = rd16 (p + 2);
	if (o)
	{
		o->npal = n;
		o->pal = calloc ((size_t) (n ? n : 1), 4);
		for (int i = 0; i < n; i++)
		{
			uint8_t *e = lin_ptr (p + 4 + 4 * (uint32_t) i);
			o->pal[i] = (uint32_t) (e[0] << 16 | e[1] << 8 | e[2]);
		}
	}
	c->ret = h;
}

void api_SelectPalette16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct obj *o = obj_get ((uint16_t) c->a[1]);
	c->ret = 0;
	if (!d || !o || o->type != OBJ_PALETTE)
		return;
	c->ret = d->s.palette;
	d->s.palette = (uint16_t) c->a[1];
}

void api_RealizePalette16 (struct w16call *c) { (void) c; c->ret = 0; }

static const uint32_t sys20[20] = { 0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
	0xc0dcc0, 0xa6caf0, 0xfffbf0, 0xa0a0a4, 0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff,
	0xffffff };

static void put_entries (uint32_t p, const uint32_t *cols, int from, int n, int count)
{
	for (int i = 0; i < n && from + i < count; i++)
	{
		uint8_t *e = lin_ptr (p + 4 * (uint32_t) i);
		e[0] = (uint8_t) (cols[from + i] >> 16);
		e[1] = (uint8_t) (cols[from + i] >> 8);
		e[2] = (uint8_t) cols[from + i];
		e[3] = 0;
	}
}

void api_GetPaletteEntries16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	if (!o || !o->pal)
	{
		c->ret = 0;
		return;
	}
	put_entries (c->a[3], o->pal, (int) c->a[1], (int) c->a[2], o->npal);
	c->ret = c->a[2];
}

void api_SetPaletteEntries16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	if (!o || !o->pal)
	{
		c->ret = 0;
		return;
	}
	for (int i = 0; i < (int) c->a[2] && (int) c->a[1] + i < o->npal; i++)
	{
		uint8_t *e = lin_ptr (far_lin (c->a[3]) + 4 * (uint32_t) i);
		o->pal[c->a[1] + (uint32_t) i] = (uint32_t) (e[0] << 16 | e[1] << 8 | e[2]);
	}
	c->ret = c->a[2];
}

void api_GetSystemPaletteEntries16 (struct w16call *c)
{
	uint32_t pal[256];
	for (int i = 0; i < 256; i++)
		pal[i] = i < 10 ? sys20[i] : i >= 246 ? sys20[i - 236] : (uint32_t) ((i * 37 & 255) << 16 | (i * 91 & 255) << 8 | (i * 13 & 255));
	put_entries (c->a[3], pal, (int) c->a[1], (int) c->a[2], 256);
	c->ret = c->a[2];
}

void api_GetNearestColor16 (struct w16call *c) { c->ret = c->a[1] & 0xffffff; }
void api_GetNearestPaletteIndex16 (struct w16call *c) { (void) c; c->ret = 0; }

/* ---- régions : un rectangle (englobant), et les points pour PtInRegion d'un polygone ---- */
static uint16_t rgn_new (int l, int t, int r, int b)
{
	uint16_t h = obj_new (OBJ_REGION);
	struct obj *o = obj_get (h);
	if (o)
	{
		o->rl = l; o->rt = t; o->rr = r; o->rb = b;
	}
	return h;
}

void api_CreateRectRgn16 (struct w16call *c) { c->ret = rgn_new ((int16_t) c->a[0], (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3]); }
void api_CreateEllipticRgn16 (struct w16call *c) { api_CreateRectRgn16 (c); }

void api_CreateRectRgnIndirect16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	c->ret = rgn_new ((int16_t) rd16 (p), (int16_t) rd16 (p + 2), (int16_t) rd16 (p + 4), (int16_t) rd16 (p + 6));
}

void api_CreatePolygonRgn16 (struct w16call *c)
{
	int n = (int16_t) c->a[1];
	if (n <= 0 || n > 4096)
	{
		c->ret = 0;
		return;
	}
	uint32_t p = far_lin (c->a[0]);
	int l = 32767, t = 32767, r = -32768, b = -32768;
	int (*pts)[2] = malloc (sizeof (int[2]) * (size_t) n);
	for (int i = 0; i < n; i++)
	{
		int x = (int16_t) rd16 (p + 4 * (uint32_t) i), y = (int16_t) rd16 (p + 4 * (uint32_t) i + 2);
		pts[i][0] = x;
		pts[i][1] = y;
		if (x < l) l = x;
		if (y < t) t = y;
		if (x > r) r = x;
		if (y > b) b = y;
	}
	uint16_t h = rgn_new (l, t, r, b);
	struct obj *o = obj_get (h);
	if (o)
	{
		o->pts = pts;
		o->npts = n;
	}
	else
		free (pts);
	c->ret = h;
}

void api_PtInRegion16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	int x = (int16_t) c->a[1], y = (int16_t) c->a[2];
	c->ret = 0;
	if (!o || x < o->rl || x >= o->rr || y < o->rt || y >= o->rb)
		return;
	if (!o->pts)
	{
		c->ret = 1;
		return;
	}
	int in = 0;
	for (int i = 0, j = o->npts - 1; i < o->npts; j = i++)
	{
		int *a = o->pts[i], *b = o->pts[j];
		if ((a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (double) (y - a[1]) / (b[1] - a[1]) + a[0])
			in = !in;
	}
	c->ret = (uint32_t) in;
}

void api_GetRgnBox16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	uint32_t p = far_lin (c->a[1]);
	if (!o)
	{
		c->ret = 0;
		return;
	}
	wr16 (p, (uint16_t) o->rl); wr16 (p + 2, (uint16_t) o->rt); wr16 (p + 4, (uint16_t) o->rr); wr16 (p + 6, (uint16_t) o->rb);
	c->ret = 2;
}

void api_OffsetRgn16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	int dx = (int16_t) c->a[1], dy = (int16_t) c->a[2];
	if (!o)
	{
		c->ret = 0;
		return;
	}
	o->rl += dx; o->rr += dx; o->rt += dy; o->rb += dy;
	for (int i = 0; i < o->npts; i++)
	{
		o->pts[i][0] += dx;
		o->pts[i][1] += dy;
	}
	c->ret = 2;
}

void api_CombineRgn16 (struct w16call *c)
{
	struct obj *d = obj_get ((uint16_t) c->a[0]), *a = obj_get ((uint16_t) c->a[1]), *b = obj_get ((uint16_t) c->a[2]);
	if (!d || !a)
	{
		c->ret = 0;
		return;
	}
	int l = a->rl, t = a->rt, r = a->rr, bt = a->rb;
	if (b && (c->a[3] == 2 || c->a[3] == 3))	/* RGN_OR, RGN_XOR : l'union englobante */
	{
		if (b->rl < l) l = b->rl;
		if (b->rt < t) t = b->rt;
		if (b->rr > r) r = b->rr;
		if (b->rb > bt) bt = b->rb;
	}
	else if (b && c->a[3] == 1)	/* RGN_AND */
	{
		if (b->rl > l) l = b->rl;
		if (b->rt > t) t = b->rt;
		if (b->rr < r) r = b->rr;
		if (b->rb < bt) bt = b->rb;
	}
	d->rl = l; d->rt = t; d->rr = r; d->rb = bt;
	c->ret = r > l && bt > t ? 2 : 1;
}

void gdi_set_rect_rgn (uint16_t hrgn, int l, int t, int r, int b)
{
	struct obj *o = obj_get (hrgn);
	if (o)
	{
		o->rl = l; o->rt = t; o->rr = r; o->rb = b;
		free (o->pts);
		o->pts = 0;
		o->npts = 0;
	}
}

void api_SetRectRgn16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	if (o)
	{
		o->rl = (int16_t) c->a[1]; o->rt = (int16_t) c->a[2]; o->rr = (int16_t) c->a[3]; o->rb = (int16_t) c->a[4];
		free (o->pts);
		o->pts = 0;
		o->npts = 0;
	}
}

static void rgn_paint (uint16_t hdc, uint16_t hrgn, uint16_t brush, int invert)
{
	struct obj *o = obj_get (hrgn);
	struct dc *d = dc_get (hdc);
	struct surf s;
	if (!o || !d || !dc_surface (d, &s))
		return;
	uint32_t rgb;
	if (invert)
		rgb = 0;
	else
	{
		int hollow;
		uint32_t ref = gdi_brush_color (brush, &hollow);
		if (hollow)
			return;
		rgb = rgb_of (d, ref);
	}
	if (o->pts)
	{
		int (*pts)[2] = malloc (sizeof (int[2]) * (size_t) o->npts);
		for (int i = 0; i < o->npts; i++)
		{
			pts[i][0] = lx (d, o->pts[i][0]);
			pts[i][1] = ly (d, o->pts[i][1]);
		}
		dpoly_fill (&s, pts, o->npts, rgb, 1);
		free (pts);
	}
	else
		dfill (&s, lx (d, o->rl), ly (d, o->rt), lx (d, o->rr), ly (d, o->rb), rgb, invert ? 6 : 13);
}

void api_FillRgn16 (struct w16call *c) { rgn_paint ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], 0); c->ret = 1; }
void api_PaintRgn16 (struct w16call *c) { struct dc *d = dc_get ((uint16_t) c->a[0]); if (d) rgn_paint ((uint16_t) c->a[0], (uint16_t) c->a[1], d->s.brush, 0); c->ret = 1; }
void api_InvertRgn16 (struct w16call *c) { rgn_paint ((uint16_t) c->a[0], (uint16_t) c->a[1], 0, 1); c->ret = 1; }

/* ---- divers ---- */
void api_GetDeviceCaps16 (struct w16call *c)
{
	switch ((int) c->a[1])
	{
	case 0: c->ret = 0x300; break;		/* DRIVERVERSION */
	case 2: c->ret = 1; break;		/* TECHNOLOGY : DT_RASDISPLAY */
	case 4: c->ret = 169; break;		/* HORZSIZE (mm) */
	case 6: c->ret = 127; break;		/* VERTSIZE */
	case 8: c->ret = 640; break;		/* HORZRES */
	case 10: c->ret = 480; break;		/* VERTRES */
	case 12: c->ret = 24; break;		/* BITSPIXEL */
	case 14: c->ret = 1; break;		/* PLANES */
	case 16: c->ret = 0xffff; break;	/* NUMBRUSHES */
	case 18: c->ret = 0xffff; break;	/* NUMPENS */
	case 22: c->ret = 0; break;		/* NUMFONTS */
	case 24: c->ret = 0xffff; break;	/* NUMCOLORS : « plus de 256 » */
	case 26: c->ret = 0; break;
	case 28: c->ret = 0x7fff; break;	/* CURVECAPS */
	case 30: c->ret = 0x7fff; break;	/* LINECAPS */
	case 32: c->ret = 0x7fff; break;	/* POLYGONALCAPS */
	case 34: c->ret = 0x7fff; break;	/* TEXTCAPS */
	case 36: c->ret = 1; break;		/* CLIPCAPS */
	case 38: c->ret = 0x0001 | 0x0008 | 0x0080 | 0x0200 | 0x0800; break;	/* RASTERCAPS : BITBLT, BITMAP64, DI_BITMAP, DIBTODEV, STRETCHBLT */
	case 40: case 42: c->ret = 36; break;	/* ASPECTX, ASPECTY */
	case 44: c->ret = 51; break;		/* ASPECTXY */
	case 88: case 90: c->ret = 96; break;	/* LOGPIXELSX/Y */
	case 104: c->ret = 0; break;		/* SIZEPALETTE */
	case 106: c->ret = 20; break;		/* NUMRESERVED */
	case 108: c->ret = 24; break;		/* COLORRES */
	default: c->ret = 0; break;
	}
}

/* métafichiers et impression : pas encore (rend l'échec, comme sans imprimante) */
void api_PlayMetaFile16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_DeleteMetaFile16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_CopyMetaFile16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_GetMetaFileBits16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_SetMetaFileBitsBetter16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_StartDoc16 (struct w16call *c) { (void) c; c->ret = 0xffff; }
void api_EndDoc16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_StartPage16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_EndPage16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_Escape16 (struct w16call *c) { (void) c; c->ret = 0; }

void gdi_init (void)
{
	memset (objs, 0, sizeof objs);
	memset (dcs, 0, sizeof dcs);
	static const uint32_t brushes[5] = { 0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0x000000 };
	for (int i = 0; i < 5; i++)
	{
		objs[i].type = OBJ_BRUSH;
		objs[i].stock = 1;
		objs[i].color = brushes[i];
	}
	objs[5] = (struct obj) { .type = OBJ_BRUSH, .stock = 1, .style = 1 };	/* NULL_BRUSH */
	objs[6] = (struct obj) { .type = OBJ_PEN, .stock = 1, .color = 0xffffff, .width = 1 };
	objs[7] = (struct obj) { .type = OBJ_PEN, .stock = 1, .color = 0, .width = 1 };
	objs[8] = (struct obj) { .type = OBJ_PEN, .stock = 1, .style = 5, .width = 1 };	/* NULL_PEN */
	static const char *faces[] = { "Courier", "Courier", "MS Sans Serif", "System", "System", "", "", "Fixedsys" };
	for (int i = 10; i <= 17; i++)
	{
		if (i == 15)
			continue;
		objs[i] = (struct obj) { .type = OBJ_FONT, .stock = 1, .height = i == 12 ? -12 : -13, .pitch = (i == 10 || i == 11 || i == 16 || i == 17) ? 1 : 2 };
		snprintf (objs[i].face, sizeof objs[i].face, "%s", faces[i - 10]);
		font_resolve (&objs[i]);
	}
	objs[15] = (struct obj) { .type = OBJ_PALETTE, .stock = 1, .npal = 20 };	/* DEFAULT_PALETTE */
	objs[15].pal = malloc (20 * 4);
	memcpy (objs[15].pal, sys20, sizeof sys20);
}

/* ======================================================================== fonctions de Windows 3.11 (21/11) */
/* GetDCOrg : l'origine du DC à l'écran (la zone cliente de sa fenêtre ; 0,0 pour un DC de mémoire) */
void api_GetDCOrg16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	int x = 0, y = 0;
	if (d && d->kind == 0 && d->hwnd)
		user_client_origin (d->hwnd, &x, &y);
	c->ret = PAIR (x, y);
}

void api_GetBrushOrg16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = d ? PAIR (d->s.brush_x, d->s.brush_y) : 0;
}

/* les dimensions d'un bitmap en 0,1 mm : seulement gardées pour le programme */
void api_GetBitmapDimension16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	c->ret = o ? PAIR (o->dim_x, o->dim_y) : 0;
}
void api_SetBitmapDimension16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	c->ret = 0;
	if (!o)
		return;
	c->ret = PAIR (o->dim_x, o->dim_y);
	o->dim_x = (int16_t) c->a[1];
	o->dim_y = (int16_t) c->a[2];
}

/* LineDDA (x1, y1, x2, y2, procédure, donnée) : la procédure pour chaque point de la ligne, le dernier exclu */
void api_LineDDA16 (struct w16call *c)
{
	int x = (int16_t) c->a[0], y = (int16_t) c->a[1], x2 = (int16_t) c->a[2], y2 = (int16_t) c->a[3];
	uint32_t proc = c->a[4], data = c->a[5];
	int dx = abs (x2 - x), sx = x < x2 ? 1 : -1, dy = -abs (y2 - y), sy = y < y2 ? 1 : -1, err = dx + dy;
	for (int guard = 0; (x != x2 || y != y2) && guard < 65536; guard++)
	{
		uint16_t args[4] = { (uint16_t) x, (uint16_t) y, (uint16_t) (data >> 16), (uint16_t) data };
		call16 (proc, args, 4, ne_ds_for (proc));
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x += sx; }
		if (e2 <= dx) { err += dx; y += sy; }
	}
	c->ret = 1;
}

/* GetCharABCWidths : les polices TrueType seulement ; ici aucune : échec, le programme prend GetCharWidth */
void api_GetCharABCWidths16 (struct w16call *c) { c->ret = 0; }

/* EnumFontFamilies : les mêmes polices qu'EnumFonts (les structures du début sont les mêmes) */
void api_EnumFontFamilies16 (struct w16call *c) { api_EnumFonts16 (c); }

void api_UpdateColors16 (struct w16call *c) { c->ret = 0; }
void api_ExtFloodFill16 (struct w16call *c) { api_FloodFill16 (c); }
void api_EnumObjects16 (struct w16call *c) { c->ret = 0; }

/* les métafichiers : pas encore (CreateMetaFile échoue, le programme s'en passe : OLE, le presse-papiers) */
void api_CreateMetaFile16 (struct w16call *c) { c->ret = 0; }
void api_CloseMetaFile16 (struct w16call *c) { c->ret = 0; }
void api_SetMetaFileBits16 (struct w16call *c) { c->ret = 0; }
void api_EnumMetaFile16 (struct w16call *c) { c->ret = 0; }
void api_PlayMetaFileRecord16 (struct w16call *c) { c->ret = 0; }

/* ScrollDC (hdc, dx, dy, rect à faire défiler, découpe, région, rect à repeindre) : comme ScrollWindow, le plus
 * simple et toujours juste : tout le rectangle est à repeindre, et la fenêtre du DC est invalidée */
void api_ScrollDC16 (struct w16call *c)
{
	extern void api_InvalidateRect16 (struct w16call *);
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	int r[4] = { 0, 0, 0, 0 };
	uint32_t src = c->a[4] ? c->a[4] : c->a[3];
	if (src)
	{
		uint32_t p = far_lin (src);
		for (int i = 0; i < 4; i++)
			r[i] = (int16_t) rd16 (p + 2 * (uint32_t) i);
	}
	if (c->a[5])
		gdi_set_rect_rgn ((uint16_t) c->a[5], r[0], r[1], r[2], r[3]);
	if (c->a[6])
	{
		uint32_t p = far_lin (c->a[6]);
		for (int i = 0; i < 4; i++)
			wr16 (p + 2 * (uint32_t) i, (uint16_t) r[i]);
	}
	if (d && d->kind == 0 && d->hwnd)
	{
		struct w16call iv = { .a = { d->hwnd, 0, 1 }, .n = 3 };
		api_InvalidateRect16 (&iv);
	}
	c->ret = 1;
}
