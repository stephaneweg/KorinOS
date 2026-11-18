// gfx.c -- dessin logiciel lissé (voir gfx.h)
#include <math.h>
#include <string.h>
#include "gfx.h"

gfx_img G;

static inline float clampf (float v, float a, float b) { return v < a ? a : v > b ? b : v; }

void gfx_target (uint32_t *px, int w, int h, int stride)
{
	G.px = px; G.w = w; G.h = h; G.stride = stride;
	gfx_noclip ();
}

void gfx_clip (int x0, int y0, int x1, int y1)
{
	G.cx0 = x0 < 0 ? 0 : x0; G.cy0 = y0 < 0 ? 0 : y0;
	G.cx1 = x1 > G.w ? G.w : x1; G.cy1 = y1 > G.h ? G.h : y1;
}

void gfx_noclip (void) { G.cx0 = G.cy0 = 0; G.cx1 = G.w; G.cy1 = G.h; }

unsigned gfx_mix (unsigned a, unsigned b, int k)
{
	if (k <= 0) return a & 0xFFFFFF;
	if (k >= 256) return b & 0xFFFFFF;
	unsigned rb = (((a & 0xFF00FF) * (unsigned) (256 - k) + (b & 0xFF00FF) * (unsigned) k) >> 8) & 0xFF00FF;
	unsigned g = (((a & 0x00FF00) * (unsigned) (256 - k) + (b & 0x00FF00) * (unsigned) k) >> 8) & 0x00FF00;
	return rb | g;
}

unsigned gfx_shade (unsigned c, int k)
{
	if (k < 0) k = 0;
	int r = (int) ((c >> 16) & 255) * k >> 8, g = (int) ((c >> 8) & 255) * k >> 8, b = (int) (c & 255) * k >> 8;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (unsigned) r << 16 | (unsigned) g << 8 | (unsigned) b;
}

static inline void put (int x, int y, unsigned c, int a)
{
	if (a <= 0) return;
	uint32_t *d = G.px + (long) y * G.stride + x;
	*d = a >= 256 ? (c & 0xFFFFFF) : gfx_mix (*d, c, a);
}

static inline void add (uint32_t *d, int r, int g, int b)
{
	uint32_t s = *d;
	r += (int) ((s >> 16) & 255); g += (int) ((s >> 8) & 255); b += (int) (s & 255);
	*d = (unsigned) (r > 255 ? 255 : r) << 16 | (unsigned) (g > 255 ? 255 : g) << 8 | (unsigned) (b > 255 ? 255 : b);
}

void gfx_rect (int x, int y, int w, int h, unsigned c, int a)
{
	int x1 = x + w, y1 = y + h;
	if (x < G.cx0) x = G.cx0;
	if (y < G.cy0) y = G.cy0;
	if (x1 > G.cx1) x1 = G.cx1;
	if (y1 > G.cy1) y1 = G.cy1;
	for (int j = y; j < y1; j++)
	{
		uint32_t *p = G.px + (long) j * G.stride;
		if (a >= 256) for (int i = x; i < x1; i++) p[i] = c & 0xFFFFFF;
		else for (int i = x; i < x1; i++) p[i] = gfx_mix (p[i], c, a);
	}
}

// un polygone : quatre sous-lignes par rangée de pixels, couverture exacte en travers (pair-impair)
void gfx_poly (const float *xy, int n, unsigned c, int a)
{
	if (n < 3 || a <= 0) return;
	float ymin = xy[1], ymax = xy[1], xmin = xy[0], xmax = xy[0];
	for (int i = 1; i < n; i++)
	{
		float x = xy[2 * i], y = xy[2 * i + 1];
		if (y < ymin) ymin = y;
		if (y > ymax) ymax = y;
		if (x < xmin) xmin = x;
		if (x > xmax) xmax = x;
	}
	int y0 = (int) floorf (ymin), y1 = (int) ceilf (ymax), bx0 = (int) floorf (xmin), bx1 = (int) ceilf (xmax) + 1;
	if (y0 < G.cy0) y0 = G.cy0;
	if (y1 > G.cy1) y1 = G.cy1;
	if (bx0 < G.cx0) bx0 = G.cx0;
	if (bx1 > G.cx1) bx1 = G.cx1;
	if (bx1 <= bx0 || y1 <= y0) return;
	static int cover[4100];
	int W = bx1 - bx0;
	if (W > 4096) W = 4096;
	for (int y = y0; y < y1; y++)
	{
		memset (cover, 0, (size_t) (W + 2) * sizeof (int));
		int any = 0;
		for (int s = 0; s < 4; s++)
		{
			float sy = (float) y + (0.125f + 0.25f * (float) s);
			float xs[64];
			int m = 0;
			for (int i = 0, j = n - 1; i < n && m < 62; j = i++)
			{
				float ya = xy[2 * i + 1], yb = xy[2 * j + 1];
				if ((ya <= sy) == (yb <= sy)) continue;
				float t = (sy - ya) / (yb - ya);
				xs[m++] = xy[2 * i] + t * (xy[2 * j] - xy[2 * i]) - (float) bx0;
			}
			for (int i = 1; i < m; i++)
			{
				float v = xs[i];
				int k = i;
				for (; k > 0 && xs[k - 1] > v; k--) xs[k] = xs[k - 1];
				xs[k] = v;
			}
			for (int i = 0; i + 1 < m; i += 2)
			{
				float a0 = clampf (xs[i], 0, (float) W), a1 = clampf (xs[i + 1], 0, (float) W);
				if (a1 <= a0) continue;
				int ia = (int) a0, ib = (int) a1;
				any = 1;
				if (ia == ib) { cover[ia] += (int) ((a1 - a0) * 64); continue; }
				cover[ia] += (int) (((float) (ia + 1) - a0) * 64);
				for (int k = ia + 1; k < ib; k++) cover[k] += 64;
				if (ib < W) cover[ib] += (int) ((a1 - (float) ib) * 64);
			}
		}
		if (!any) continue;
		for (int i = 0; i < W; i++)
			if (cover[i]) put (bx0 + i, y, c, cover[i] >= 256 ? a : cover[i] * a / 256);
	}
}

void gfx_disc (float x, float y, float r, unsigned c, int a)
{
	if (r <= 0 || a <= 0) return;
	int x0 = (int) floorf (x - r - 1), x1 = (int) ceilf (x + r + 1), y0 = (int) floorf (y - r - 1), y1 = (int) ceilf (y + r + 1);
	if (x0 < G.cx0) x0 = G.cx0;
	if (y0 < G.cy0) y0 = G.cy0;
	if (x1 > G.cx1) x1 = G.cx1;
	if (y1 > G.cy1) y1 = G.cy1;
	float r2o = (r + 0.7f) * (r + 0.7f), r2i = r > 0.7f ? (r - 0.7f) * (r - 0.7f) : 0;
	for (int j = y0; j < y1; j++)
	{
		float dy = (float) j + 0.5f - y;
		for (int i = x0; i < x1; i++)
		{
			float dx = (float) i + 0.5f - x, d2 = dx * dx + dy * dy;
			if (d2 >= r2o) continue;
			if (d2 <= r2i) { put (i, j, c, a); continue; }
			put (i, j, c, (int) (clampf (r - sqrtf (d2) + 0.5f, 0, 1) * (float) a));
		}
	}
}

void gfx_ring (float x, float y, float r, float w, unsigned c, int a)
{
	float o = r + w / 2, in = r - w / 2;
	if (o <= 0 || a <= 0) return;
	int x0 = (int) floorf (x - o - 1), x1 = (int) ceilf (x + o + 1), y0 = (int) floorf (y - o - 1), y1 = (int) ceilf (y + o + 1);
	if (x0 < G.cx0) x0 = G.cx0;
	if (y0 < G.cy0) y0 = G.cy0;
	if (x1 > G.cx1) x1 = G.cx1;
	if (y1 > G.cy1) y1 = G.cy1;
	float o2 = (o + 1) * (o + 1), i2 = in > 1 ? (in - 1) * (in - 1) : 0;
	for (int j = y0; j < y1; j++)
	{
		float dy = (float) j + 0.5f - y;
		for (int i = x0; i < x1; i++)
		{
			float dx = (float) i + 0.5f - x, d2 = dx * dx + dy * dy;
			if (d2 >= o2 || d2 < i2) continue;
			float d = sqrtf (d2);
			float cov = clampf (o - d + 0.5f, 0, 1) * clampf (d - in + 0.5f, 0, 1);
			if (cov > 0) put (i, j, c, (int) (cov * (float) a));
		}
	}
}

void gfx_line (float x0, float y0, float x1, float y1, float w, unsigned c, int a)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf (dx * dx + dy * dy);
	if (l > 0.01f)
	{
		float nx = -dy / l * w / 2, ny = dx / l * w / 2;
		float q[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
		gfx_poly (q, 4, c, a);
	}
}

// une boîte arrondie : dégradé, opacité, liseré (0 : aucun)
void gfx_rbox (int x, int y, int w, int h, int r, unsigned top, unsigned bot, int a, unsigned line)
{
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	float R = (float) r;
	for (int j = 0; j < h; j++)
	{
		int py = y + j;
		if (py < G.cy0 || py >= G.cy1) continue;
		unsigned c = gfx_mix (top, bot, h > 1 ? j * 256 / (h - 1) : 0);
		float dy = j < r ? R - (float) j - 0.5f : j >= h - r ? (float) (j - (h - r)) + 0.5f : 0;
		for (int i = 0; i < w; i++)
		{
			int px = x + i;
			if (px < G.cx0 || px >= G.cx1) continue;
			float dx = i < r ? R - (float) i - 0.5f : i >= w - r ? (float) (i - (w - r)) + 0.5f : 0;
			float cov = 1, inner = 1;
			if (dx > 0 && dy > 0)
			{
				float d = sqrtf (dx * dx + dy * dy);
				cov = clampf (R - d + 0.5f, 0, 1);
				inner = clampf (R - 1 - d + 0.5f, 0, 1);
			}
			else if (i == 0 || j == 0 || i == w - 1 || j == h - 1) inner = 0;
			if (cov <= 0) continue;
			unsigned col = c;
			int al = (int) (cov * (float) a);
			if (line && inner < 1) { col = gfx_mix (line, c, (int) (inner * 256)); al = (int) (cov * 256); }
			put (px, py, col, al);
		}
	}
}

void gfx_glow (float x, float y, float r, unsigned c, int k)
{
	if (r < 0.5f || k <= 0) return;
	int x0 = (int) floorf (x - r), x1 = (int) ceilf (x + r), y0 = (int) floorf (y - r), y1 = (int) ceilf (y + r);
	if (x0 < G.cx0) x0 = G.cx0;
	if (y0 < G.cy0) y0 = G.cy0;
	if (x1 > G.cx1) x1 = G.cx1;
	if (y1 > G.cy1) y1 = G.cy1;
	float ir2 = 1.0f / (r * r);
	float cr = (float) ((c >> 16) & 255) * (float) k / 256.0f;
	float cg = (float) ((c >> 8) & 255) * (float) k / 256.0f;
	float cb = (float) (c & 255) * (float) k / 256.0f;
	for (int j = y0; j < y1; j++)
	{
		float dy = (float) j + 0.5f - y, dy2 = dy * dy * ir2;
		if (dy2 >= 1) continue;
		uint32_t *row = G.px + (long) j * G.stride;
		for (int i = x0; i < x1; i++)
		{
			float dx = (float) i + 0.5f - x, q = 1 - dx * dx * ir2 - dy2;
			if (q <= 0) continue;
			q *= q;
			add (row + i, (int) (cr * q), (int) (cg * q), (int) (cb * q));
		}
	}
}

void gfx_glow_ring (float x, float y, float r, float w, unsigned c, int k)
{
	if (w < 0.5f || k <= 0) return;
	float o = r + w, in = r - w;
	int x0 = (int) floorf (x - o), x1 = (int) ceilf (x + o), y0 = (int) floorf (y - o), y1 = (int) ceilf (y + o);
	if (x0 < G.cx0) x0 = G.cx0;
	if (y0 < G.cy0) y0 = G.cy0;
	if (x1 > G.cx1) x1 = G.cx1;
	if (y1 > G.cy1) y1 = G.cy1;
	float o2 = o * o, i2 = in > 0 ? in * in : 0, iw = 1.0f / w;
	float cr = (float) ((c >> 16) & 255) * (float) k / 256.0f;
	float cg = (float) ((c >> 8) & 255) * (float) k / 256.0f;
	float cb = (float) (c & 255) * (float) k / 256.0f;
	for (int j = y0; j < y1; j++)
	{
		float dy = (float) j + 0.5f - y, dy2 = dy * dy;
		if (dy2 >= o2) continue;
		uint32_t *row = G.px + (long) j * G.stride;
		// seulement la bande de la rangée qui touche l'anneau
		float span = sqrtf (o2 - dy2);
		int a0 = (int) floorf (x - span), a1 = (int) ceilf (x + span);
		if (a0 < x0) a0 = x0;
		if (a1 > x1) a1 = x1;
		for (int i = a0; i < a1; i++)
		{
			float dx = (float) i + 0.5f - x, d2 = dx * dx + dy2;
			if (d2 >= o2 || d2 <= i2) continue;
			float q = 1 - fabsf (sqrtf (d2) - r) * iw;
			if (q <= 0) continue;
			q *= q;
			add (row + i, (int) (cr * q), (int) (cg * q), (int) (cb * q));
		}
	}
}

void gfx_add_all (unsigned c)
{
	int r = (int) ((c >> 16) & 255), g = (int) ((c >> 8) & 255), b = (int) (c & 255);
	if (!r && !g && !b) return;
	for (int j = G.cy0; j < G.cy1; j++)
	{
		uint32_t *p = G.px + (long) j * G.stride;
		for (int i = G.cx0; i < G.cx1; i++) add (p + i, r, g, b);
	}
}

void gfx_dim (int k)
{
	for (int j = G.cy0; j < G.cy1; j++)
	{
		uint32_t *p = G.px + (long) j * G.stride;
		for (int i = G.cx0; i < G.cx1; i++)
			p[i] = ((p[i] & 0xFF00FF) * (unsigned) k >> 8 & 0xFF00FF) | ((p[i] & 0xFF00) * (unsigned) k >> 8 & 0xFF00);
	}
}
