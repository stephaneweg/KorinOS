//
// look.h -- the Telegram app's look: the way of Windows Live Messenger (the sky, its soft light, the "glass" picture frames
// whose colour says the status, the little buddy figure, the round avatars with initials) made of the theme's tokens: Onyx's
// fixed sky blue would be a stranger under Givre or Onyx Noir. Every colour here comes from ui_jetons (never theme.txt),
// mixed until it reads on what is under it; only the avatars' seven hues and the emoticons are pictures of their own.
// The photos are read by stb_image (image.c), cut to a square, kept at the sizes drawn.
//
// MIT licence.
//
#ifndef TG_LOOK_H
#define TG_LOOK_H

#include <time.h>
#include "client.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))
#define TRN(s)	s

static struct ui_vars ui;
extern "C" unsigned *image_decode (const unsigned char *b, int n, int *w, int *h);	// image.c

static inline int imin (int a, int b) { return a < b ? a : b; }
static inline int imax (int a, int b) { return a > b ? a : b; }

// ---- the theme's tokens --------------------------------------------------------------------------------

enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned bg) { return (unsigned) ui_ink (bg) & 0xFFFFFF; }

static double lum (unsigned c)
{
	int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
	int y = (54 * r + 183 * g + 19 * b) >> 8;
	return y * y / 65025.0;
}
static double contrast (unsigned a, unsigned b)
{
	double x = lum (a), y = lum (b);
	return x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05);
}
// A colour mixed towards the theme's text until it reads at `ratio` on `bg`.
static unsigned readable (unsigned c, unsigned bg, double ratio)
{
	unsigned o = c;
	for (int t = 0; t <= 240 && contrast (o, bg) < ratio; t += 16) o = mix (c, jet (J_TEXTE1), t);
	return o;
}

// What is drawn, in Messenger's names; made once: the theme does not change under a running program.
struct Colours
{
	unsigned bg, skyTop, skyBot, line, ink, grey, light, title, link, selTop, selBot, selRim, hotTop, hotBot, hotRim;
	unsigned online, away, busy, offline, unread, unreadInk, inputBg, me, them, group, bot, ticks, noteBg, glow;
};
static Colours C;
#define TC_BG		C.bg
#define TC_SKY_TOP	C.skyTop
#define TC_SKY_BOT	C.skyBot
#define TC_LINE		C.line
#define TC_INK		C.ink
#define TC_GREY		C.grey
#define TC_LIGHT	C.light
#define TC_TITLE	C.title
#define TC_LINK		C.link
#define TC_SEL_TOP	C.selTop
#define TC_SEL_BOT	C.selBot
#define TC_SEL_RIM	C.selRim
#define TC_HOT_TOP	C.hotTop
#define TC_HOT_BOT	C.hotBot
#define TC_HOT_RIM	C.hotRim
#define TC_ONLINE	C.online
#define TC_AWAY		C.away
#define TC_BUSY		C.busy
#define TC_OFFLINE	C.offline
#define TC_UNREAD	C.unread
#define TC_INPUT_BG	C.inputBg
#define TC_ME		C.me
#define TC_THEM		C.them

static void look_make ()
{
	s_jet = (const unsigned *) ui_jetons ();
	unsigned acc = jet (J_ACCENT);
	C.bg = jet (J_SURFACE); C.ink = jet (J_TEXTE1); C.grey = jet (J_TEXTE2); C.light = jet (J_TEXTE3);
	C.line = jet (J_LIGNE); C.inputBg = jet (J_FACE);
	// the sky: the accent in the surface, as much as lets the text read on it
	unsigned sky = mix (C.bg, acc, 16);
	for (int t = 96; t >= 16; t -= 8)
	{
		unsigned c = mix (C.bg, acc, t);
		if (contrast (C.ink, c) >= 7 && contrast (C.grey, c) >= 3.8) { sky = c; break; }
	}
	C.skyTop = sky; C.skyBot = mix (C.bg, sky, 72);
	C.glow = mix (sky, lum (C.bg) > 0.4 ? 0xFFFFFF : C.ink, 120);
	C.title = readable (mix (C.ink, acc, 96), sky, 6.5);
	C.link = readable (acc, C.bg, 4.5);
	C.selTop = mix (C.bg, acc, 44); C.selBot = mix (C.bg, acc, 76); C.selRim = mix (C.bg, acc, 136);
	C.hotTop = mix (C.bg, acc, 14); C.hotBot = mix (C.bg, acc, 30); C.hotRim = mix (C.bg, acc, 76);
	C.online = jet (J_SUCCES); C.away = jet (J_ALERTE); C.busy = readable (jet (J_DANGER), C.bg, 4.2); C.offline = jet (J_TEXTE3);
	C.unread = acc; C.unreadInk = ink_on (acc);
	C.me = C.link; C.them = readable (0x9A3EC0, C.bg, 4.5);
	C.group = readable (acc, sky, 2.2); C.bot = readable (0x8A6FDB, sky, 2.2);
	C.ticks = readable (jet (J_SUCCES), C.bg, 3.0);
	C.noteBg = mix (jet (J_FACE), jet (J_ALERTE), 48);
}

// Telegram's peer colours (the avatar without a photo).
static const unsigned tc_peer[7][2] = {
	{ 0xFF885E, 0xFF516A }, { 0xFFCD6A, 0xFFA85C }, { 0xA0DE7E, 0x54CB68 }, { 0x53EDD6, 0x28C9B7 },
	{ 0x72D5FD, 0x2A9EF1 }, { 0xE0A2F3, 0xD669ED }, { 0x82B1FF, 0x665FFF } };
static int peer_colour (long long id) { long long k = id < 0 ? -id : id; return (int) (k % 7); }

// ---- the text ------------------------------------------------------------------------------------------

static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)
static int ui_px () { return ui_echelle () >= 2 ? 12 : 13; }
#define PX_SMALL	11
#define PX_BIG		15
#define PX_TITLE	18
#define PX_HUGE		24
static inline int fh (int px) { return px * 117 / 100; }		// (a line of DejaVu)
static long f_sans () { return (long) ui.font; }
static long f_bold () { return (long) ui.font_b; }
static long f_ital () { static long f; if (!f) f = ui_font_get (UFN_ITALIC); return f; }
static inline long f_style (int s) { return s == 2 ? f_bold () : s == 1 ? f_ital () : f_sans (); }

// Text with its top at y, in one of the styles (0 regular, 1 italic, 2 bold), cut with an ellipsis to w px (0: no limit).
static void ftext (const char *s, int x, int y, unsigned c, int px, int style = 0, int w = 0)
{
	if (!s || !*s) return;
	long f = f_style (style);
	int base = y + px * 93 / 100;
	if (w > 0) ui_text_fit ((long) s, x, base, c, px, f, w);
	else ui_text ((long) s, x, base, c, px, f, 4000);
}
// ... centred on the height of a box
static void fbox (const char *s, int x, int y, int h, unsigned c, int px, int style = 0, int w = 0) { ftext (s, x, y + (h - fh (px)) / 2, c, px, style, w); }
static int ftw (const char *s, int px, int style = 0) { return s && *s ? (int) ui_text_width ((long) s, px, f_style (style)) : 0; }

// ---- the view being painted --------------------------------------------------------------------------------

static uint32_t *g_px;
static long g_pw, g_ph;
static inline void begin_paint (long view) { g_px = uikit_canvas_pixels (view, &g_pw, &g_ph); }
static inline void px_blend (int x, int y, unsigned c, int a)
{
	if (x < 0 || y < 0 || x >= g_pw || y >= g_ph) return;
	uint32_t &d = g_px[(long) y * g_pw + x];
	unsigned o = d, na = 255 - (unsigned) a, ua = (unsigned) a;
	d = ((c >> 16 & 255) * ua + (o >> 16 & 255) * na) / 255 << 16 | ((c >> 8 & 255) * ua + (o >> 8 & 255) * na) / 255 << 8 | ((c & 255) * ua + (o & 255) * na) / 255;
}

// ---- the sky -------------------------------------------------------------------------------------------------

// A soft light: an ellipse that fades to nothing at its rim (the swooshes of Messenger's header).
static void glow (int cx, int cy, int rx, int ry, unsigned c, int alpha)
{
	if (rx < 1 || ry < 1) return;
	int x0 = imax (cx - rx, 0), x1 = imin (cx + rx, (int) g_pw), y0 = imax (cy - ry, 0), y1 = imin (cy + ry, (int) g_ph);
	long rx2 = (long) rx * rx, ry2 = (long) ry * ry;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
		{
			long dx = x - cx, dy = y - cy;
			long d = (dx * dx * 256) / rx2 + (dy * dy * 256) / ry2;
			if (d < 256) px_blend (x, y, c, (int) (alpha * (256 - d) / 256));
		}
}
static void sky (int x, int y, int w, int h, bool line = true)
{
	ui_rbox (x, y, w, h, 0, TC_SKY_TOP, TC_SKY_BOT);
	glow (x + w - w / 5, y - h / 4, w / 2, h, C.glow, 80);
	glow (x + w / 3, y + h + h / 3, w / 2, h, C.glow, 50);
	if (line) ui_fill (x, y + h - 1, w, 1, TC_LINE);
}

// ---- shapes ------------------------------------------------------------------------------------------------------
// In px, floats: UIKit's anti-aliased shapes take 1/16 of a pixel.

static inline long V16 (float px) { return (long) (px * 16.0f); }
static void vp_disc (float cx, float cy, float r, unsigned c) { ui_vp_disc (V16 (cx), V16 (cy), V16 (r), c); }
static void vp_line (float x0, float y0, float x1, float y1, float w, unsigned c) { ui_vp_line (V16 (x0), V16 (y0), V16 (x1), V16 (y1), V16 (w), c); }
static float s_sin[64];
static void vp_tables ()
{
	if (s_sin[1] != 0) return;
	// (sine by Bhaskara's formula would do; the table from a rotation, no libm in the paint path)
	float c = 1, s = 0, dc = 0.995184727f, ds = 0.09801714f;
	for (int i = 0; i < 64; i++) { s_sin[i] = s; float t = c * dc - s * ds; s = s * dc + c * ds; c = t; }
}
static inline float fsin (int k) { return s_sin[k & 63]; }		// (64 steps a turn)
static inline float fcos (int k) { return s_sin[(k + 16) & 63]; }
static void vp_ellipse (float cx, float cy, float rx, float ry, unsigned c)
{
	int pts[48];
	for (int i = 0; i < 24; i++)
	{
		int k = i * 64 / 24;
		pts[i * 2] = (int) V16 (cx + rx * fcos (k));
		pts[i * 2 + 1] = (int) V16 (cy + ry * fsin (k));
	}
	ui_vp_poly ((long) pts, 24, c);
}
// An arc of the circle, a0..a1 in degrees, counter-clockwise on the screen (0: east, 90: north), w its thickness.
static void vp_arc (float cx, float cy, float r, int a0, int a1, float w, unsigned c)
{
	int n = imax (2, (a1 - a0) / 20);
	float px = 0, py = 0;
	for (int i = 0; i <= n; i++)
	{
		int a = a0 + (a1 - a0) * i / n;
		int k = ((a % 360 + 360) % 360) * 64 / 360;
		float x = cx + r * fcos (k), y = cy - r * fsin (k);
		if (i) vp_line (px, py, x, y, w, c);
		px = x; py = y;
	}
}
static void vp_poly (const float *xy, int n, unsigned c)
{
	int pts[48];
	if (n > 24) n = 24;
	for (int i = 0; i < n * 2; i++) pts[i] = (int) V16 (xy[i]);
	ui_vp_poly ((long) pts, n, c);
}
static void vp_rrect (float x, float y, float w, float h, float r, unsigned c)
{
	float e = r < w / 2 ? r : w / 2; if (e > h / 2) e = h / 2;
	float q[24] = { x + e, y, x + w - e, y, x + w, y + e, x + w, y + h - e, x + w - e, y + h, x + e, y + h, x, y + h - e, x, y + e };
	vp_poly (q, 8, c);
}

// ---- the status --------------------------------------------------------------------------------------------

enum { TS_ONLINE, TS_AWAY, TS_OFFLINE, TS_GROUP, TS_BOT };

static int user_ts (tg::User *u, int now)
{
	if (!u) return TS_OFFLINE;
	if (u->bot) return TS_BOT;
	if (u->status == tg::ST_ONLINE && (u->expires == 0 || u->expires > now)) return TS_ONLINE;
	if (u->status == tg::ST_RECENTLY) return TS_AWAY;
	if (u->status == tg::ST_OFFLINE && now - u->wasOnline < 15 * 60) return TS_AWAY;
	return TS_OFFLINE;
}
static unsigned ts_colour (int ts) { return ts == TS_ONLINE ? TC_ONLINE : ts == TS_AWAY ? TC_AWAY : ts == TS_GROUP ? C.group : ts == TS_BOT ? C.bot : TC_OFFLINE; }

// The buddy: a little person (two for a group) in the status colour, glossy.
static void buddy (int x, int y, int s, unsigned c, bool group = false)
{
	float k = s / 16.0f;
	unsigned dark = mix (c, 0x000000, 90), light = mix (c, 0xFFFFFF, 110);
	for (int who = group ? 1 : 0; who >= 0; who--)
	{
		float ox = group ? (who ? 4.0f * k : -2.0f * k) : 0, oy = group && who ? -1.5f * k : 0;
		float bx = x + (8 + ox) * k;
		unsigned body = who ? mix (c, 0xFFFFFF, 70) : c;
		vp_ellipse (bx, y + (13.2f + oy) * k, 6.2f * k, 4.6f * k, dark);
		vp_disc (bx, y + (4.6f + oy) * k, 3.6f * k, dark);
		vp_ellipse (bx, y + (13.2f + oy) * k, 5.4f * k, 3.9f * k, body);
		vp_disc (bx, y + (4.6f + oy) * k, 2.9f * k, body);
		vp_ellipse (x + (7 + ox) * k, y + (3.4f + oy) * k, 1.5f * k, 1.0f * k, light);
	}
}

// ---- the avatars ---------------------------------------------------------------------------------------------

// A picture file (JPEG, PNG...) -> its pixels (0xAARRGGBB, malloc), *w, *h; 0: unreadable.
static unsigned *img_load (const char *path, int *w, int *h)
{
	int n = 0;
	unsigned char *b = tg_load (path, &n);
	if (!b) return 0;
	unsigned *px = image_decode (b, n, w, h);
	free (b);
	return px;
}

// src (sw x sh) brought to dw x dh: the average of what each pixel covers (smaller), the nearest (larger).
static unsigned *scale_box (const unsigned *src, int sw, int sh, int dw, int dh)
{
	unsigned *out = (unsigned *) malloc (sizeof (unsigned) * (size_t) dw * (size_t) dh);
	if (!out) return 0;
	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++)
		{
			int x0 = (int) ((long) x * sw / dw), x1 = (int) ((long) (x + 1) * sw / dw), y0 = (int) ((long) y * sh / dh), y1 = (int) ((long) (y + 1) * sh / dh);
			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			if (x1 > sw) x1 = sw;
			if (y1 > sh) y1 = sh;
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++)
			{ unsigned c = src[(long) yy * sw + xx]; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
			out[(long) y * dw + x] = n ? ((r / n) << 16) | ((g / n) << 8) | (b / n) : 0xFFFFFF;
		}
	return out;
}

// ---- the pictures, read by the card's thread (tgplat.h) ------------------------------------------------------------

// (that thread) A picture at its size; square: its square middle.
static void img_job (IoJob *j)
{
	int w, h;
	unsigned *src = img_load (j->path, &w, &h);
	if (!src) return;
	if (!j->square) j->px = scale_box (src, w, h, j->w, j->h);
	else
	{
		int sq = imin (w, h), ox = (w - sq) / 2, oy = (h - sq) / 2;
		unsigned *crop = (unsigned *) malloc (sizeof (unsigned) * (size_t) sq * (size_t) sq);
		if (crop)
		{
			for (int y = 0; y < sq; y++) memcpy (crop + (long) y * sq, src + (long) (y + oy) * w + ox, sizeof (unsigned) * (size_t) sq);
			j->px = scale_box (crop, sq, sq, j->w, j->h);
			free (crop);
		}
	}
	free (src);
}
enum { IMGS = 128 };
struct Img { char path[160]; int w, h; bool square; unsigned *px; };
static Img g_img[IMGS];
static int g_nimg;
// The picture at that size, when it is there; else it is asked for (once) and comes later (img_poll).
static unsigned *img_get (const char *path, int w, int h, bool square)
{
	for (int i = 0; i < g_nimg; i++)
		if (g_img[i].w == w && g_img[i].h == h && g_img[i].square == square && !strcmp (g_img[i].path, path)) return g_img[i].px;
	if (g_nimg == IMGS)				// (the oldest goes)
	{
		free (g_img[0].px);
		memmove (g_img, g_img + 1, sizeof g_img[0] * (IMGS - 1));
		g_nimg--;
	}
	Img &m = g_img[g_nimg++];
	memset (&m, 0, sizeof m);
	snprintf (m.path, sizeof m.path, "%s", path); m.w = w; m.h = h; m.square = square;
	IoJob *j = new IoJob;
	memset (j, 0, sizeof *j);
	snprintf (j->path, sizeof j->path, "%s", path); j->w = w; j->h = h; j->square = square;
	io_push (j);
	return 0;
}
// What the thread has read -> true: there are new pictures to draw.
static bool img_poll ()
{
	if (!s_ioDone) return false;
	pthread_mutex_lock (&s_ioLock);
	IoJob *d = s_ioDone; s_ioDone = 0;
	pthread_mutex_unlock (&s_ioLock);
	bool any = false;
	while (d)
	{
		IoJob *nx = d->next;
		bool kept = false;
		for (int i = 0; i < g_nimg && !kept; i++)
			if (g_img[i].w == d->w && g_img[i].h == d->h && g_img[i].square == d->square && !g_img[i].px && !strcmp (g_img[i].path, d->path))
			{ g_img[i].px = d->px; kept = true; any = any || d->px; }
		if (!kept) free (d->px);
		delete d;
		d = nx;
	}
	return any;
}

// A rectangle of pixels (w x h) laid at (x, y) in the view, its corners rounded (r).
static void blit_rect_round (const unsigned *px, int x, int y, int w, int h, int r)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			int dx = i < r ? r - i : i >= w - r ? i - (w - r - 1) : 0, dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
			int a = 255;
			if (dx && dy)
			{
				int d2 = (dx - 1) * (dx - 1) + (dy - 1) * (dy - 1), r2 = r * r;
				if (d2 >= r2) continue;
				if (d2 > (r - 1) * (r - 1)) a = 255 * (r2 - d2) / (r2 - (r - 1) * (r - 1) + 1);
			}
			px_blend (x + i, y + j, px[(long) j * w + i] & 0xFFFFFF, a);
		}
}

// The peer's photo at size, 0 if it has none (yet).
static unsigned *av_photo (tg::Client &c, long long peer, int size)
{
	char path[160];
	return c.photo (peer, path, sizeof path) ? img_get (path, size, size, true) : 0;
}

static unsigned u8_get (const char *s, int *len)
{
	const unsigned char *p = (const unsigned char *) s;
	unsigned c = p[0];
	int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
	unsigned cp = k == 1 ? c : k == 2 ? c & 0x1F : k == 3 ? c & 0x0F : c & 0x07;
	for (int i = 1; i < k; i++) { if ((p[i] & 0xC0) != 0x80) { *len = 1; return c; } cp = (cp << 6) | (p[i] & 0x3F); }
	*len = k;
	return cp;
}
static int u8_put (char *o, unsigned cp)
{
	if (cp < 0x80) { o[0] = (char) cp; return 1; }
	if (cp < 0x800) { o[0] = (char) (0xC0 | cp >> 6); o[1] = (char) (0x80 | (cp & 63)); return 2; }
	if (cp < 0x10000) { o[0] = (char) (0xE0 | cp >> 12); o[1] = (char) (0x80 | (cp >> 6 & 63)); o[2] = (char) (0x80 | (cp & 63)); return 3; }
	o[0] = (char) (0xF0 | cp >> 18); o[1] = (char) (0x80 | (cp >> 12 & 63)); o[2] = (char) (0x80 | (cp >> 6 & 63)); o[3] = (char) (0x80 | (cp & 63));
	return 4;
}

// The initials of a name (two letters at most, UTF-8; an emoji or the like in a name is skipped: the font has none).
static void initials (const char *name, char *out)
{
	int o = 0, words = 0;
	const char *p = name;
	while (*p && words < 2)
	{
		while (*p == ' ') p++;
		if (!*p) break;
		int k;
		unsigned cp = u8_get (p, &k);
		if (cp >= 'a' && cp <= 'z') cp -= 32;
		else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 32;
		if (cp < 0x2000) o += u8_put (out + o, cp);
		words++;
		while (*p && *p != ' ') p++;
	}
	out[o] = 0;
}

// A round avatar (Telegram's way, in the lists): the photo, else the initials on the peer's colour.
static void avatar_round (tg::Client &c, long long peer, int x, int y, int s)
{
	unsigned *px = av_photo (c, peer, s);
	if (px) { blit_rect_round (px, x, y, s, s, s / 2); return; }
	int col = peer_colour (tg::pid (peer));
	vp_disc (x + s / 2.0f, y + s / 2.0f, s / 2.0f, tc_peer[col][1]);
	vp_disc (x + s / 2.0f, y + s * 0.42f, s * 0.38f, mix (tc_peer[col][1], tc_peer[col][0], 128));
	char name[128], ini[16];
	c.peerName (peer, name, sizeof name);
	initials (name, ini);
	int f = s >= 56 ? PX_HUGE : s >= 36 ? PX_BIG : s >= 24 ? 13 : 10;
	int tw = ftw (ini, f, 2);
	fbox (ini, x + (s - tw) / 2, y, s, ink_on (tc_peer[col][1]), f, 2);
}

// The glass frame of Messenger around a picture: s the picture, the frame b px around it, its colour the status.
static void frame (int x, int y, int s, int b, unsigned c)
{
	int o = s + 2 * b;
	ui_rbox (x - b - 1, y - b - 1, o + 2, o + 2, b + 4, mix (c, 0x000000, 80), mix (c, 0x000000, 110));
	ui_rbox (x - b, y - b, o, o, b + 3, mix (c, 0xFFFFFF, 150), c);
	ui_rbox (x - b + 1, y - b + 1, o - 2, o / 2 - 1, b + 2, mix (c, 0xFFFFFF, 190), mix (c, 0xFFFFFF, 110));
	ui_rbox (x - 1, y - 1, s + 2, s + 2, 4, jet (J_SURFACE), jet (J_SURFACE));
}

// A framed picture (the headers', the conversation's display pictures): the photo, else the initials.
static void avatar_framed (tg::Client &c, long long peer, int x, int y, int s, unsigned status)
{
	int b = s >= 80 ? 6 : s >= 48 ? 5 : 3;
	frame (x, y, s, b, status);
	unsigned *px = av_photo (c, peer, s);
	if (px) { blit_rect_round (px, x, y, s, s, 3); return; }
	int col = peer_colour (tg::pid (peer));
	ui_rbox (x, y, s, s, 3, tc_peer[col][0], tc_peer[col][1]);
	char name[128], ini[16];
	c.peerName (peer, name, sizeof name);
	initials (name, ini);
	int f = s >= 56 ? PX_HUGE : s >= 36 ? PX_BIG : 13;
	int tw = ftw (ini, f, 2);
	fbox (ini, x + (s - tw) / 2, y, s, ink_on (tc_peer[col][1]), f, 2);
}

// The default picture (the sign-in's): Messenger's two buddies on the sky.
static void avatar_default (int x, int y, int s, unsigned status)
{
	int b = s >= 80 ? 7 : 5;
	frame (x, y, s, b, status);
	ui_rbox (x, y, s, s, 3, mix (jet (J_SURFACE), TC_SKY_TOP, 96), TC_SKY_TOP);
	buddy (x + s * 36 / 100, y + s * 10 / 100, s * 54 / 100, C.group);
	buddy (x + s * 8 / 100, y + s * 30 / 100, s * 62 / 100, TC_ONLINE);
}

// "last seen ..." in the user's words.
static void status_text (tg::Client &c, tg::User *u, char *out, int cap)
{
	out[0] = 0;
	if (!u) return;
	int now = c.serverTime ();
	if (u->bot) { snprintf (out, (size_t) cap, "%s", TR ("bot")); return; }
	if (u->id == c.selfId) { snprintf (out, (size_t) cap, "%s", c.wantOnline () ? TR ("Online") : TR ("Appear offline")); return; }
	switch (u->status)
	{
	case tg::ST_ONLINE: if (!u->expires || u->expires > now) { snprintf (out, (size_t) cap, "%s", TR ("Online")); return; } // fall through
	case tg::ST_OFFLINE:
	{
		int w = u->status == tg::ST_ONLINE ? u->expires : u->wasOnline;
		int ago = now - w;
		if (ago < 60) snprintf (out, (size_t) cap, "%s", TR ("last seen just now"));
		else if (ago < 3600) snprintf (out, (size_t) cap, TR ("last seen %d min ago"), ago / 60);
		else if (ago < 86400) snprintf (out, (size_t) cap, TR ("last seen %d h ago"), ago / 3600);
		else snprintf (out, (size_t) cap, TR ("last seen %d days ago"), ago / 86400);
		return;
	}
	case tg::ST_RECENTLY: snprintf (out, (size_t) cap, "%s", TR ("last seen recently")); return;
	case tg::ST_WEEK: snprintf (out, (size_t) cap, "%s", TR ("last seen within a week")); return;
	case tg::ST_MONTH: snprintf (out, (size_t) cap, "%s", TR ("last seen within a month")); return;
	default: snprintf (out, (size_t) cap, "%s", TR ("last seen a long time ago")); return;
	}
}

// ---- the days ------------------------------------------------------------------------------------------------

static void local_tm (int t, struct tm *m) { time_t x = t; localtime_r (&x, m); }
// A local day's number (days since 1970).
static long local_day (int t)
{
	struct tm m; local_tm (t, &m);
	long y = m.tm_year + 1900, mo = m.tm_mon + 1, d = m.tm_mday;
	y -= mo <= 2;
	long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
	long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}
static void hhmm (int t, char *out, int cap)
{
	struct tm m; local_tm (t, &m);
	snprintf (out, (size_t) cap, "%02d:%02d", m.tm_hour, m.tm_min);
}

#endif
