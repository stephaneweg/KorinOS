//
// gameart.h -- what the games in C++ share (pinball, critters): anti-aliased drawing into 0x00RRGGBB pixels, a hidden
// UIKit canvas to draw text and to keep a static layer in, the effects on AudioKit's FM voices, sparks. The twin of
// gameart.inc and gamesnd.inc for the games in assembly. The window's pixels are ui.surf, as in Cosmo Zoom!.
//
#ifndef GAMEART_H
#define GAMEART_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <uikit.h>
#include <audiokit_abi.h>

namespace ga {

extern struct ui_vars ui;

static inline unsigned now_ms ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) ((unsigned long long) t.tv_sec * 1000u + (unsigned long long) t.tv_nsec / 1000000u);
}
static inline unsigned mix (unsigned a, unsigned b, int k)		// k 0..256: towards b
{
	unsigned rb = (((a & 0xFF00FF) * (unsigned) (256 - k) + (b & 0xFF00FF) * (unsigned) k) >> 8) & 0xFF00FF;
	unsigned g = (((a & 0x00FF00) * (unsigned) (256 - k) + (b & 0x00FF00) * (unsigned) k) >> 8) & 0x00FF00;
	return rb | g;
}
static inline unsigned shade (unsigned c, int k)			// k 256: unchanged, more: lighter
{
	int r = (int) ((c >> 16) & 255) * k >> 8, g = (int) ((c >> 8) & 255) * k >> 8, b = (int) (c & 255) * k >> 8;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (unsigned) r << 16 | (unsigned) g << 8 | (unsigned) b;
}
static inline float clampf (float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// ---- pixels ---------------------------------------------------------------------------------------------------------
struct Img
{
	uint32_t *px = 0; int w = 0, h = 0, stride = 0;
	int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;			// the clip
	void set (uint32_t *p, int W, int H, int s) { px = p; w = W; h = H; stride = s; noClip (); }
	void clip (int x0, int y0, int x1, int y1) { cx0 = x0 < 0 ? 0 : x0; cy0 = y0 < 0 ? 0 : y0; cx1 = x1 > w ? w : x1; cy1 = y1 > h ? h : y1; }
	void noClip () { cx0 = cy0 = 0; cx1 = w; cy1 = h; }

	inline void put (int x, int y, unsigned c, int a)		// a 0..256
	{
		if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || a <= 0) return;
		uint32_t &d = px[(long) y * stride + x];
		d = a >= 256 ? (c & 0xFFFFFF) : mix (d, c, a);
	}
	inline void add (int x, int y, unsigned c)			// light added
	{
		if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
		uint32_t &d = px[(long) y * stride + x];
		int r = (int) ((d >> 16) & 255) + (int) ((c >> 16) & 255), g = (int) ((d >> 8) & 255) + (int) ((c >> 8) & 255), b = (int) (d & 255) + (int) (c & 255);
		d = (unsigned) (r > 255 ? 255 : r) << 16 | (unsigned) (g > 255 ? 255 : g) << 8 | (unsigned) (b > 255 ? 255 : b);
	}
	void rect (int x, int y, int W, int H, unsigned c, int a = 256)
	{
		int x1 = x + W, y1 = y + H;
		if (x < cx0) x = cx0;
		if (y < cy0) y = cy0;
		if (x1 > cx1) x1 = cx1;
		if (y1 > cy1) y1 = cy1;
		for (int j = y; j < y1; j++)
		{
			uint32_t *p = px + (long) j * stride;
			if (a >= 256) for (int i = x; i < x1; i++) p[i] = c & 0xFFFFFF;
			else for (int i = x; i < x1; i++) p[i] = mix (p[i], c, a);
		}
	}
	void vgrad (int x, int y, int W, int H, unsigned top, unsigned bot, int a = 256)
	{
		for (int j = 0; j < H; j++) rect (x, y + j, W, 1, mix (top, bot, H > 1 ? j * 256 / (H - 1) : 0), a);
	}
	// a rounded box: a gradient, its opacity, and a thin line round it (0: none)
	void rbox (int x, int y, int W, int H, int r, unsigned top, unsigned bot, int a = 256, unsigned line = 0)
	{
		if (r * 2 > H) r = H / 2;
		if (r * 2 > W) r = W / 2;
		float R = (float) r;
		for (int j = 0; j < H; j++)
		{
			unsigned c = mix (top, bot, H > 1 ? j * 256 / (H - 1) : 0);
			float dy = j < r ? R - (float) j - 0.5f : j >= H - r ? (float) (j - (H - r)) + 0.5f : 0;
			for (int i = 0; i < W; i++)
			{
				float dx = i < r ? R - (float) i - 0.5f : i >= W - r ? (float) (i - (W - r)) + 0.5f : 0;
				float cov = 1, inner = 1;
				if (dx > 0 && dy > 0)
				{
					float d = sqrtf (dx * dx + dy * dy);
					cov = clampf (R - d + 0.5f, 0, 1);
					inner = clampf (R - 1 - d + 0.5f, 0, 1);
				}
				else if (i == 0 || j == 0 || i == W - 1 || j == H - 1) inner = 0;
				if (cov <= 0) continue;
				unsigned col = c;
				int al = (int) (cov * (float) a);
				if (line && inner < 1) { col = mix (line, c, (int) (inner * 256)); al = (int) (cov * 256); if (a < 256 && inner > 0) al = (int) ((float) a + (1 - inner) * (float) (256 - a)); }
				put (x + i, y + j, col, al);
			}
		}
	}
	// a disc, anti-aliased
	void disc (float x, float y, float r, unsigned c, int a = 256)
	{
		int x0 = (int) floorf (x - r - 1), x1 = (int) ceilf (x + r + 1), y0 = (int) floorf (y - r - 1), y1 = (int) ceilf (y + r + 1);
		if (x0 < cx0) x0 = cx0;
		if (y0 < cy0) y0 = cy0;
		if (x1 > cx1) x1 = cx1;
		if (y1 > cy1) y1 = cy1;
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
	void ellipse (float x, float y, float rx, float ry, unsigned c, int a = 256)
	{
		if (rx <= 0 || ry <= 0) return;
		int x0 = (int) floorf (x - rx - 1), x1 = (int) ceilf (x + rx + 1), y0 = (int) floorf (y - ry - 1), y1 = (int) ceilf (y + ry + 1);
		if (x0 < cx0) x0 = cx0;
		if (y0 < cy0) y0 = cy0;
		if (x1 > cx1) x1 = cx1;
		if (y1 > cy1) y1 = cy1;
		float m = rx < ry ? rx : ry;
		for (int j = y0; j < y1; j++)
		{
			float dy = ((float) j + 0.5f - y) / ry;
			for (int i = x0; i < x1; i++)
			{
				float dx = ((float) i + 0.5f - x) / rx, q = sqrtf (dx * dx + dy * dy);
				float cov = clampf ((1 - q) * m + 0.5f, 0, 1);
				if (cov > 0) put (i, j, c, (int) (cov * (float) a));
			}
		}
	}
	// a ring
	void ring (float x, float y, float r, float w, unsigned c, int a = 256)
	{
		float o = r + w / 2, n = r - w / 2;
		int x0 = (int) floorf (x - o - 1), x1 = (int) ceilf (x + o + 1), y0 = (int) floorf (y - o - 1), y1 = (int) ceilf (y + o + 1);
		if (x0 < cx0) x0 = cx0;
		if (y0 < cy0) y0 = cy0;
		if (x1 > cx1) x1 = cx1;
		if (y1 > cy1) y1 = cy1;
		for (int j = y0; j < y1; j++)
		{
			float dy = (float) j + 0.5f - y;
			for (int i = x0; i < x1; i++)
			{
				float dx = (float) i + 0.5f - x, d = sqrtf (dx * dx + dy * dy);
				float cov = clampf (o - d + 0.5f, 0, 1) * clampf (d - n + 0.5f, 0, 1);
				if (cov > 0) put (i, j, c, (int) (cov * (float) a));
			}
		}
	}
	// a soft light, added: c at the centre, nothing at r
	void glow (float x, float y, float r, unsigned c)
	{
		int x0 = (int) floorf (x - r), x1 = (int) ceilf (x + r), y0 = (int) floorf (y - r), y1 = (int) ceilf (y + r);
		if (x0 < cx0) x0 = cx0;
		if (y0 < cy0) y0 = cy0;
		if (x1 > cx1) x1 = cx1;
		if (y1 > cy1) y1 = cy1;
		float k = 1 / (r * r);
		for (int j = y0; j < y1; j++)
		{
			float dy = (float) j + 0.5f - y;
			for (int i = x0; i < x1; i++)
			{
				float dx = (float) i + 0.5f - x, q = 1 - (dx * dx + dy * dy) * k;
				if (q <= 0) continue;
				add (i, j, shade (c, (int) (q * q * 256)));
			}
		}
	}
	// a polygon (points as x, y floats), 4 sub-lines a pixel row, exact across
	void poly (const float *xy, int n, unsigned c, int a = 256)
	{
		if (n < 3) return;
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
		if (y0 < cy0) y0 = cy0;
		if (y1 > cy1) y1 = cy1;
		if (bx0 < cx0) bx0 = cx0;
		if (bx1 > cx1) bx1 = cx1;
		if (bx1 <= bx0) return;
		static int cover[4100];
		int W = bx1 - bx0; if (W > 4096) W = 4096;
		for (int y = y0; y < y1; y++)
		{
			memset (cover, 0, (size_t) (W + 2) * sizeof (int));
			for (int s = 0; s < 4; s++)
			{
				float sy = (float) y + (0.125f + 0.25f * (float) s);
				float xs[128]; int m = 0;
				for (int i = 0, j = n - 1; i < n && m < 126; j = i++)
				{
					float ya = xy[2 * i + 1], yb = xy[2 * j + 1];
					if ((ya <= sy) == (yb <= sy)) continue;
					float t = (sy - ya) / (yb - ya);
					xs[m++] = xy[2 * i] + t * (xy[2 * j] - xy[2 * i]) - (float) bx0;
				}
				for (int i = 1; i < m; i++) { float v = xs[i]; int k = i; for (; k > 0 && xs[k - 1] > v; k--) xs[k] = xs[k - 1]; xs[k] = v; }
				for (int i = 0; i + 1 < m; i += 2)
				{
					float a0 = clampf (xs[i], 0, (float) W), a1 = clampf (xs[i + 1], 0, (float) W);
					if (a1 <= a0) continue;
					int ia = (int) a0, ib = (int) a1;
					if (ia == ib) { cover[ia] += (int) ((a1 - a0) * 64); continue; }
					cover[ia] += (int) (((float) (ia + 1) - a0) * 64);
					for (int k = ia + 1; k < ib; k++) cover[k] += 64;
					if (ib < W) cover[ib] += (int) ((a1 - (float) ib) * 64);
				}
			}
			for (int i = 0; i < W; i++) if (cover[i]) put (bx0 + i, y, c, cover[i] >= 256 ? a : cover[i] * a / 256);
		}
	}
	// a band a -> b, w thick, its ends round
	void line (float x0, float y0, float x1, float y1, float w, unsigned c, int a = 256, bool round = true)
	{
		float dx = x1 - x0, dy = y1 - y0, l = sqrtf (dx * dx + dy * dy);
		if (l > 0)
		{
			float nx = -dy / l * w / 2, ny = dx / l * w / 2;
			float q[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
			poly (q, 4, c, a);
		}
		if (round) { disc (x0, y0, w / 2, c, a); disc (x1, y1, w / 2, c, a); }
	}
	void stroke (const float *xy, int n, float w, unsigned c, bool closed = false, int a = 256)
	{
		for (int i = 0; i + 1 < n; i++) line (xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], w, c, a);
		if (closed && n > 2) line (xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], w, c, a);
	}
	// all the image darker: k 0..256 of it kept
	void dim (int k)
	{
		for (int y = cy0; y < cy1; y++)
		{
			uint32_t *p = px + (long) y * stride;
			for (int x = cx0; x < cx1; x++) p[x] = ((p[x] & 0xFF00FF) * (unsigned) k >> 8 & 0xFF00FF) | ((p[x] & 0xFF00) * (unsigned) k >> 8 & 0xFF00);
		}
	}
};

// ---- a smooth noise, for the decors -----------------------------------------------------------------------------------
static inline float hash2 (int x, int y, int k)
{
	unsigned h = (unsigned) x * 374761393u + (unsigned) y * 668265263u + (unsigned) k * 2147483647u;
	h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
	return (float) (h & 0xFFFF) / 65535.0f;
}
static inline float noise (float x, float y, int k = 0)
{
	int ix = (int) floorf (x), iy = (int) floorf (y);
	float fx = x - (float) ix, fy = y - (float) iy;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	float a = hash2 (ix, iy, k), b = hash2 (ix + 1, iy, k), c = hash2 (ix, iy + 1, k), d = hash2 (ix + 1, iy + 1, k);
	return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static inline float fbm (float x, float y, int k = 0) { return noise (x, y, k) * 0.55f + noise (x * 2.1f, y * 2.1f, k + 1) * 0.3f + noise (x * 4.3f, y * 4.3f, k + 2) * 0.15f; }

// ---- text ---------------------------------------------------------------------------------------------------------------
// into the current target: the window, or a Layer between begin () and end ()
static inline long F (bool bold = true) { return (long) (bold ? (void *) ui.font_b : (void *) ui.font); }
static inline int textW (const char *s, int px, bool bold = true) { return (int) ui_text_width ((long) s, px, F (bold)); }
static inline void text (int x, int base, const char *s, unsigned c, int px, bool bold = true) { ui_text ((long) s, x, base, c, px, F (bold), 0); }
static inline void textC (int cx, int base, const char *s, unsigned c, int px, bool bold = true) { text (cx - textW (s, px, bold) / 2, base, s, c, px, bold); }
static inline void textR (int rx, int base, const char *s, unsigned c, int px, bool bold = true) { text (rx - textW (s, px, bold), base, s, c, px, bold); }
static inline void textS (int x, int base, const char *s, unsigned c, int px, bool bold = true) { text (x + 1, base + 1, s, 0x000000, px, bold); text (x, base, s, c, px, bold); }
static inline void textCS (int cx, int base, const char *s, unsigned c, int px, bool bold = true) { textS (cx - textW (s, px, bold) / 2, base, s, c, px, bold); }
// wrapped to w -> the lines' height (measure: nothing drawn)
static inline int wrap (int x, int top, int w, const char *s, unsigned c, int px, bool bold = false, bool measure = false)
{
	return (int) ui_text_wrap ((long) s, x, top, w, c, F (bold), px, measure ? 1 : 0);
}

// a hidden UIKit canvas: pixels of one's own, text and the like drawn into it
struct Layer
{
	long cv = 0; Img img;
	bool alloc (int w, int h)
	{
		if (cv && img.w == w && img.h == h) return true;
		if (cv) { ui_w_destroy (cv); cv = 0; }
		cv = ui_canvas (0, 0, w, h);
		if (!cv) return false;
		ui_w_show (cv, 0);
		long W, H; uint32_t *p = uikit_canvas_pixels (cv, &W, &H);
		img.set (p, (int) W, (int) H, (int) W);
		return p != 0;
	}
	void begin () { ui_canvas_begin (cv); }
	void end () { ui_canvas_end (); }
	void toWindow (int dx = 0, int dy = 0)
	{
		int W = (int) ui.w, H = (int) ui.h;
		for (int y = 0; y < H && y - dy < img.h; y++)
		{
			if (y - dy < 0) continue;
			uint32_t *d = ui.surf + (long) y * W; const uint32_t *s = img.px + (long) (y - dy) * img.stride;
			int n = W < img.w ? W : img.w;
			if (!dx) memcpy (d, s, (size_t) n * 4);
			else for (int x = 0; x < n; x++) { int sx = x - dx; d[x] = sx >= 0 && sx < img.w ? s[sx] : 0; }
		}
	}
};

// the window as an image
static inline Img window () { Img i; i.set (ui.surf, (int) ui.w, (int) ui.h, (int) ui.w); return i; }

// ---- the effects, on AudioKit's FM voices (as gamesnd.inc) ------------------------------------------------------------
enum { SOUND_SQUARE = AKW_SQUARE, SOUND_TRIANGLE = AKW_TRIANGLE, SOUND_SAW = AKW_SAW, SOUND_NOISE = AKW_NOISE };
struct Snd
{
	enum { NV = 8, NQ = 24 };
	long fm = 0; bool mute = false; int next = 0;
	unsigned off[NV];
	struct Q { unsigned at; int hz, ms, wave, vol; } q[NQ]; int nq = 0;
	void open ()
	{
		memset (off, 0, sizeof off);
		fm = ak_fm_new (NV);
		if (fm <= 0) { fm = 0; return; }
		if (ak_fm_live (fm, 0) < 0) { ak_fm_free (fm); fm = 0; }
	}
	void close () { if (fm) { ak_fm_free (fm); fm = 0; } }
	void note (int hz, int ms, int wave = SOUND_SQUARE, int vol = 90)
	{
		if (!fm || mute) return;
		int v = next; next = (next + 1) & (NV - 1);
		ak_fm_start (fm, v, hz * 1000, wave, vol);
		off[v] = (now_ms () + (unsigned) ms) | 1;
	}
	void later (int delay, int hz, int ms, int wave = SOUND_SQUARE, int vol = 90)
	{
		if (!fm || mute || nq >= NQ) return;
		Q &k = q[nq++]; k.at = now_ms () + (unsigned) delay; k.hz = hz; k.ms = ms; k.wave = wave; k.vol = vol;
	}
	void tick ()
	{
		if (!fm) return;
		unsigned t = now_ms ();
		for (int i = 0; i < nq; )
			if ((int) (t - q[i].at) >= 0) { note (q[i].hz, q[i].ms, q[i].wave, q[i].vol); q[i] = q[--nq]; }
			else i++;
		for (int v = 0; v < NV; v++) if (off[v] && (int) (t - off[v]) >= 0) { off[v] = 0; ak_fm_stop (fm, v); }
	}
	void toggle () { mute = !mute; nq = 0; if (mute && fm) ak_fm_stop (fm, -1); }
	void win () { later (0, 523, 110); later (120, 659, 110); later (240, 784, 110); later (360, 1047, 360); }
	void lose () { later (0, 392, 150, SOUND_TRIANGLE, 120); later (160, 330, 150, SOUND_TRIANGLE, 120); later (320, 262, 150, SOUND_TRIANGLE, 120); later (480, 196, 150, SOUND_TRIANGLE, 120); }
};

// ---- sparks -----------------------------------------------------------------------------------------------------------
struct Spark { float x, y, vx, vy, g; int life, max; unsigned col; float size; };
struct Sparks
{
	enum { MAX = 256 };
	Spark s[MAX]; int n = 0; unsigned seed = 0x9E3779B9u;
	float rnd () { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float) (seed & 0xFFFF) / 65535.0f; }
	void clear () { n = 0; }
	void burst (float x, float y, unsigned col, int count, float speed, int life, float g = 0.06f, float size = 1.6f)
	{
		for (int i = 0; i < count; i++)
		{
			Spark &p = s[n < MAX ? n++ : (int) (rnd () * (MAX - 1))];
			float a = rnd () * 6.2832f, v = speed * (0.3f + rnd ());
			p.x = x; p.y = y; p.vx = cosf (a) * v; p.vy = sinf (a) * v; p.g = g;
			p.max = p.life = life + (int) (rnd () * (float) life); p.col = col; p.size = size * (0.6f + rnd () * 0.8f);
		}
	}
	void step ()
	{
		for (int i = 0; i < n; )
		{
			Spark &p = s[i];
			p.x += p.vx; p.y += p.vy; p.vy += p.g;
			if (--p.life <= 0) s[i] = s[--n]; else i++;
		}
	}
	void draw (Img &im)
	{
		for (int i = 0; i < n; i++)
		{
			const Spark &p = s[i];
			im.disc (p.x, p.y, p.size, p.col, p.life * 256 / p.max);
		}
	}
};

}

#endif
