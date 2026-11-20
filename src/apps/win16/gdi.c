/* gdi.c -- win16 : GDI, dessiné directement dans les pixels de la fenêtre de Korin (be_pixels).
 *
 * winevdm passe GDI à gdi32 de Windows ; ici on trace nous-mêmes : contextes de dessin (DC) d'une fenêtre,
 * plumes, brosses, rectangles, ellipses, lignes, texte (police de Korin). La suite (bitmaps, BitBlt, polices,
 * régions, palettes) viendra avec les programmes qui la demandent (docs/etude/WIN16.md, étape 4). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w16.h"
#include "be.h"
#include "user.h"

enum { OBJ_NONE, OBJ_PEN, OBJ_BRUSH, OBJ_FONT, OBJ_BITMAP, OBJ_PALETTE, OBJ_REGION };
#define MAX_OBJ 1024
struct obj
{
	int type, stock;
	uint32_t color;		/* COLORREF 0x00BBGGRR */
	int style, width;	/* plume : PS_* ; brosse : BS_* (0 pleine, 1 creuse) */
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

static uint16_t obj_new (int type, uint32_t color, int style, int width)
{
	for (int i = 32; i < MAX_OBJ; i++)
		if (!objs[i].type)
		{
			objs[i].type = type;
			objs[i].stock = 0;
			objs[i].color = color;
			objs[i].style = style;
			objs[i].width = width;
			return OBJ_HANDLE (i);
		}
	return 0;
}

#define MAX_DC 64
struct dc
{
	int used;
	uint16_t hwnd;
	uint16_t pen, brush, font;
	uint32_t text_color, bk_color;
	int bk_mode;		/* 1 TRANSPARENT, 2 OPAQUE */
	int cur_x, cur_y;
	int vx, vy;		/* SetViewportOrg */
	int dx0, dy0, dx1, dy1, dirty;
};
static struct dc dcs[MAX_DC];
#define DC_HANDLE(i) ((uint16_t) (0x3000 + (i) * 4))
static struct dc *dc_get (uint16_t h)
{
	int i = (h - 0x3000) / 4;
	if (h < 0x3000 || (h & 3) || i >= MAX_DC || !dcs[i].used)
		return 0;
	return &dcs[i];
}

/* la surface d'un DC : pixels, origine et découpe dans la fenêtre de Korin */
struct surf { uint32_t *px; int stride, ox, oy, cx0, cy0, cx1, cy1; long be; };

static int dc_surface (struct dc *d, struct surf *s)
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
	s->ox = ox + d->vx;
	s->oy = oy + d->vy;
	s->cx0 = ox < 0 ? 0 : ox;
	s->cy0 = oy < 0 ? 0 : oy;
	s->cx1 = ox + cw > pw ? pw : ox + cw;
	s->cy1 = oy + ch > ph ? ph : oy + ch;
	return s->cx1 > s->cx0 && s->cy1 > s->cy0;
}

static uint32_t ref_to_rgb (uint32_t c)
{
	return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
}

static void touch (struct dc *d, const struct surf *s, int x0, int y0, int x1, int y1)
{
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

/* un rectangle plein, coordonnées logiques du DC, [x0, x1) × [y0, y1) */
static void fill (struct dc *d, const struct surf *s, int x0, int y0, int x1, int y1, uint32_t rgb)
{
	x0 += s->ox; x1 += s->ox; y0 += s->oy; y1 += s->oy;
	if (x0 < s->cx0) x0 = s->cx0;
	if (y0 < s->cy0) y0 = s->cy0;
	if (x1 > s->cx1) x1 = s->cx1;
	if (y1 > s->cy1) y1 = s->cy1;
	if (x1 <= x0 || y1 <= y0)
		return;
	for (int y = y0; y < y1; y++)
	{
		uint32_t *p = s->px + (size_t) y * (size_t) s->stride;
		for (int x = x0; x < x1; x++)
			p[x] = rgb;
	}
	touch (d, s, x0, y0, x1, y1);
}

static void pixel (struct dc *d, const struct surf *s, int x, int y, uint32_t rgb)
{
	x += s->ox;
	y += s->oy;
	if (x < s->cx0 || y < s->cy0 || x >= s->cx1 || y >= s->cy1)
		return;
	s->px[(size_t) y * (size_t) s->stride + (size_t) x] = rgb;
	touch (d, s, x, y, x + 1, y + 1);
}

static int pen_of (struct dc *d, uint32_t *rgb, int *w)
{
	struct obj *p = obj_get (d->pen);
	if (!p || p->style == 5)	/* PS_NULL */
		return 0;
	*rgb = ref_to_rgb (p->color);
	*w = p->width > 1 ? p->width : 1;
	return 1;
}

static int brush_of (struct dc *d, uint32_t *rgb)
{
	struct obj *b = obj_get (d->brush);
	if (!b || b->style == 1)	/* BS_HOLLOW */
		return 0;
	*rgb = ref_to_rgb (b->color);
	return 1;
}

static void line (struct dc *d, const struct surf *s, int x0, int y0, int x1, int y1, uint32_t rgb, int w)
{
	/* Bresenham, sans le dernier point (comme Windows) */
	int dx = abs (x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
	while (x0 != x1 || y0 != y1)
	{
		if (w > 1)
			fill (d, s, x0 - w / 2, y0 - w / 2, x0 - w / 2 + w, y0 - w / 2 + w, rgb);
		else
			pixel (d, s, x0, y0, rgb);
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

/* ---- DC ---- */
uint16_t gdi_dc_for_window (uint16_t hwnd)
{
	for (int i = 0; i < MAX_DC; i++)
		if (!dcs[i].used)
		{
			struct dc *d = &dcs[i];
			memset (d, 0, sizeof *d);
			d->used = 1;
			d->hwnd = hwnd;
			d->pen = OBJ_HANDLE (7);	/* BLACK_PEN */
			d->brush = OBJ_HANDLE (0);	/* WHITE_BRUSH */
			d->font = OBJ_HANDLE (13);	/* SYSTEM_FONT */
			d->text_color = 0;
			d->bk_color = 0xffffff;
			d->bk_mode = 2;
			return DC_HANDLE (i);
		}
	return 0;
}

static void flush (struct dc *d)
{
	if (!d->dirty)
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

void gdi_fill_rect_brush (uint16_t hdc, int l, int t, int r, int b, uint16_t hbrush)
{
	struct dc *d = dc_get (hdc);
	struct surf s;
	if (!d || !dc_surface (d, &s))
		return;
	uint32_t ref;
	if (hbrush && hbrush <= 32)	/* COLOR_* + 1 */
		ref = user_sys_color (hbrush - 1);
	else
	{
		struct obj *o = obj_get (hbrush);
		if (!o || o->type != OBJ_BRUSH || o->style == 1)
			return;
		ref = o->color;
	}
	fill (d, &s, l, t, r, b, ref_to_rgb (ref));
}

/* ---- l'API ---- */
void api_GetStockObject16 (struct w16call *c)
{
	int i = (int) c->a[0];
	c->ret = (i >= 0 && i <= 16 && objs[i].type) ? OBJ_HANDLE (i) : 0;
}

void api_CreatePen16 (struct w16call *c)
{
	c->ret = obj_new (OBJ_PEN, c->a[2], (int16_t) c->a[0], (int16_t) c->a[1]);
}

void api_CreateSolidBrush16 (struct w16call *c)
{
	c->ret = obj_new (OBJ_BRUSH, c->a[0], 0, 0);
}

void api_DeleteObject16 (struct w16call *c)
{
	struct obj *o = obj_get ((uint16_t) c->a[0]);
	c->ret = 0;
	if (o && !o->stock)
	{
		o->type = OBJ_NONE;
		c->ret = 1;
	}
}

void api_SelectObject16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct obj *o = obj_get ((uint16_t) c->a[1]);
	c->ret = 0;
	if (!d || !o)
		return;
	uint16_t *slot = o->type == OBJ_PEN ? &d->pen : o->type == OBJ_BRUSH ? &d->brush : o->type == OBJ_FONT ? &d->font : 0;
	if (!slot)
		return;
	c->ret = *slot;
	*slot = (uint16_t) c->a[1];
}

void api_SetTextColor16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = d->text_color;
	d->text_color = c->a[1];
}

void api_SetBkColor16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = d->bk_color;
	d->bk_color = c->a[1];
}

void api_SetBkMode16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = (uint32_t) d->bk_mode;
	d->bk_mode = (int) c->a[1];
}

void api_Rectangle16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = 1;
	if (!d || !dc_surface (d, &s))
		return;
	int l = (int16_t) c->a[1], t = (int16_t) c->a[2], r = (int16_t) c->a[3], b = (int16_t) c->a[4];
	uint32_t rgb;
	int w;
	if (brush_of (d, &rgb))
		fill (d, &s, l + 1, t + 1, r - 1, b - 1, rgb);
	if (pen_of (d, &rgb, &w))
	{
		fill (d, &s, l, t, r, t + w, rgb);
		fill (d, &s, l, b - w, r, b, rgb);
		fill (d, &s, l, t, l + w, b, rgb);
		fill (d, &s, r - w, t, r, b, rgb);
	}
}

void api_Ellipse16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = 1;
	if (!d || !dc_surface (d, &s))
		return;
	int l = (int16_t) c->a[1], t = (int16_t) c->a[2], r = (int16_t) c->a[3], b = (int16_t) c->a[4];
	double cx = (l + r - 1) / 2.0, cy = (t + b - 1) / 2.0, rx = (r - l) / 2.0, ry = (b - t) / 2.0;
	if (rx <= 0 || ry <= 0)
		return;
	uint32_t fillc, penc;
	int w;
	int hasb = brush_of (d, &fillc), hasp = pen_of (d, &penc, &w);
	for (int y = t; y < b; y++)
	{
		double dy = (y - cy) / ry;
		if (dy * dy > 1)
			continue;
		double half = rx * __builtin_sqrt (1 - dy * dy);
		int x0 = (int) (cx - half + 0.5), x1 = (int) (cx + half + 0.5) + 1;
		if (hasb)
			fill (d, &s, x0, y, x1, y + 1, fillc);
		if (hasp)
		{
			fill (d, &s, x0, y, x0 + w, y + 1, penc);
			fill (d, &s, x1 - w, y, x1, y + 1, penc);
		}
	}
	if (hasp)
		for (int x = l; x < r; x++)
		{
			double dx = (x - cx) / rx;
			if (dx * dx > 1)
				continue;
			double half = ry * __builtin_sqrt (1 - dx * dx);
			int y0 = (int) (cy - half + 0.5), y1 = (int) (cy + half + 0.5);
			fill (d, &s, x, y0, x + 1, y0 + w, penc);
			fill (d, &s, x, y1 - w + 1, x + 1, y1 + 1, penc);
		}
}

void api_MoveTo16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = (uint32_t) (uint16_t) d->cur_x | (uint32_t) (uint16_t) d->cur_y << 16;
	d->cur_x = (int16_t) c->a[1];
	d->cur_y = (int16_t) c->a[2];
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
		line (d, &s, d->cur_x, d->cur_y, x, y, rgb, w);
	d->cur_x = x;
	d->cur_y = y;
}

void api_SetPixel16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	struct surf s;
	c->ret = c->a[3];
	if (d && dc_surface (d, &s))
		pixel (d, &s, (int16_t) c->a[1], (int16_t) c->a[2], ref_to_rgb (c->a[3]));
}

void api_FillRect16 (struct w16call *c)
{
	uint32_t r = far_lin (c->a[1]);
	gdi_fill_rect_brush ((uint16_t) c->a[0], (int16_t) rd16 (r), (int16_t) rd16 (r + 2), (int16_t) rd16 (r + 4),
		(int16_t) rd16 (r + 6), (uint16_t) c->a[2]);
	c->ret = 1;
}

static void text_out (struct dc *d, int x, int y, const uint8_t *str, int n)
{
	struct surf s;
	if (!dc_surface (d, &s))
		return;
	char buf[1024];
	extern int w16_to_utf8 (const uint8_t *, int, char *, int);
	w16_to_utf8 (str, n, buf, sizeof buf);
	int w = be_text_width (buf), h = be_font_height ();
	if (d->bk_mode == 2)
		fill (d, &s, x, y, x + w, y + h, ref_to_rgb (d->bk_color));
	be_text (s.be, x + s.ox, y + s.oy, buf, ref_to_rgb (d->text_color), s.cx0, s.cy0, s.cx1 - s.cx0, s.cy1 - s.cy0);
	touch (d, &s, x + s.ox, y + s.oy, x + s.ox + w, y + s.oy + h);
	w16_log ("   texte (%d, %d) : %s\n", x, y, buf);
}

void api_TextOut16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	c->ret = 1;
	if (!d)
		return;
	text_out (d, (int16_t) c->a[1], (int16_t) c->a[2], far_ptr (c->a[3]), (int) (int16_t) c->a[4]);
}

void api_GetTextExtent16 (struct w16call *c)
{
	char buf[1024];
	extern int w16_to_utf8 (const uint8_t *, int, char *, int);
	w16_to_utf8 (far_ptr (c->a[1]), (int16_t) c->a[2], buf, sizeof buf);
	c->ret = (uint32_t) be_text_width (buf) | (uint32_t) be_font_height () << 16;
}

void api_GetTextMetrics16 (struct w16call *c)
{
	/* TEXTMETRIC 16 bits : la police « System » vue par le programme */
	uint32_t t = far_lin (c->a[1]);
	memset (lin_ptr (t), 0, 31);
	wr16 (t, (uint16_t) be_font_height ());		/* tmHeight */
	wr16 (t + 2, (uint16_t) be_font_ascent ());	/* tmAscent */
	wr16 (t + 4, (uint16_t) (be_font_height () - be_font_ascent ()));
	wr16 (t + 10, 7);				/* tmAveCharWidth */
	wr16 (t + 12, 14);				/* tmMaxCharWidth */
	wr16 (t + 14, 400);				/* tmWeight */
	lin_ptr (t + 20)[0] = 0x20;			/* tmFirstChar */
	lin_ptr (t + 21)[0] = 0xff;
	lin_ptr (t + 22)[0] = 0x2e;
	lin_ptr (t + 23)[0] = 0x20;
	lin_ptr (t + 24)[0] = 0x22;			/* tmPitchAndFamily : variable, suisse */
	c->ret = 1;
}

void api_SetViewportOrg16 (struct w16call *c)
{
	struct dc *d = dc_get ((uint16_t) c->a[0]);
	if (!d)
		return;
	c->ret = (uint32_t) (uint16_t) d->vx | (uint32_t) (uint16_t) d->vy << 16;
	d->vx = (int16_t) c->a[1];
	d->vy = (int16_t) c->a[2];
}

void api_GetDeviceCaps16 (struct w16call *c)
{
	switch ((int) c->a[1])
	{
	case 8: c->ret = 640; break;	/* HORZRES */
	case 10: c->ret = 480; break;	/* VERTRES */
	case 12: c->ret = 24; break;	/* BITSPIXEL */
	case 14: c->ret = 1; break;	/* PLANES */
	case 24: c->ret = 0xffff; break;	/* NUMCOLORS : « plus de 256 » */
	case 88: case 90: c->ret = 96; break;	/* LOGPIXELSX/Y */
	default: c->ret = 0; break;
	}
}

void gdi_init (void)
{
	memset (objs, 0, sizeof objs);
	memset (dcs, 0, sizeof dcs);
	static const uint32_t brushes[5] = { 0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0x000000 };
	for (int i = 0; i < 5; i++)
		objs[i] = (struct obj) { OBJ_BRUSH, 1, brushes[i], 0, 0 };
	objs[5] = (struct obj) { OBJ_BRUSH, 1, 0, 1, 0 };	/* NULL_BRUSH */
	objs[6] = (struct obj) { OBJ_PEN, 1, 0xffffff, 0, 1 };
	objs[7] = (struct obj) { OBJ_PEN, 1, 0x000000, 0, 1 };
	objs[8] = (struct obj) { OBJ_PEN, 1, 0, 5, 1 };		/* NULL_PEN */
	for (int i = 10; i <= 14; i++)
		objs[i] = (struct obj) { OBJ_FONT, 1, 0, i, 0 };
	objs[15] = (struct obj) { OBJ_PALETTE, 1, 0, 0, 0 };
	objs[16] = (struct obj) { OBJ_FONT, 1, 0, 16, 0 };
}
