//
// draw.h -- Pinball's drawing of a table: the static layer (the playfield lit from above, the artwork, the words, the
// rails with their shadows, the posts), drawn once per table and scale, and the moving parts over it each image (the
// lamps, the bumpers, the flippers at their angle, the plunger, the balls and their trails). Table units -> pixels by View.
//
#ifndef PINBALL_DRAW_H
#define PINBALL_DRAW_H

static int g_lang = 0;				// 0 English, 1 French (the tables have both)

static void fmt (long v, char *o, int cap)	// numbers grouped by thousands
{
	char d[24]; snprintf (d, sizeof d, "%ld", v);
	int n = (int) strlen (d), k = 0;
	for (int i = 0; i < n && k < cap - 4; i++)
	{
		o[k++] = d[i];
		int left = n - 1 - i;
		if (left > 0 && left % 3 == 0 && d[i] != '-') o[k++] = ' ';
	}
	o[k] = 0;
}

struct View
{
	float s, ox, oy;
	float X (double u) const { return ox + (float) u * s; }
	float Y (double u) const { return oy + (float) u * s; }
	float L (double u) const { float r = (float) u * s; return r < 0.5f ? 0.5f : r; }
};
static View fit_view (const Table &t, int x, int y, int w, int h)
{
	float s = fminf ((float) w / (float) t.w, (float) h / (float) t.h);
	View v = { s, (float) x + ((float) w - (float) t.w * s) / 2, (float) y + ((float) h - (float) t.h * s) / 2 };
	return v;
}

enum { MAXDRAW = 128 };
static float g_xy[2 * MAXDRAW + 2];
static int to_xy (const View &v, const Vec *p, int n)
{
	if (n > MAXDRAW) n = MAXDRAW;
	for (int i = 0; i < n; i++) { g_xy[2 * i] = v.X (p[i].x); g_xy[2 * i + 1] = v.Y (p[i].y); }
	return n;
}
static void disc (Img &cv, const View &v, Vec c, double r, unsigned col, int a = 256) { cv.disc (v.X (c.x), v.Y (c.y), v.L (r), col, a); }
static void ring (Img &cv, const View &v, Vec c, double r, double w, unsigned col, int a = 256) { cv.ring (v.X (c.x), v.Y (c.y), v.L (r), v.L (w), col, a); }
static void glow (Img &cv, const View &v, Vec c, double r, unsigned col) { cv.glow (v.X (c.x), v.Y (c.y), v.L (r), col); }
static void quad (Img &cv, const View &v, Vec a, Vec b, double th, unsigned col, int al = 256) { cv.line (v.X (a.x), v.Y (a.y), v.X (b.x), v.Y (b.y), v.L (th), col, al, false); }
static void stroke (Img &cv, const View &v, const Vec *p, int n, double w, unsigned col, bool closed = false, int a = 256)
{ n = to_xy (v, p, n); cv.stroke (g_xy, n, v.L (w), col, closed, a); }
static void fillpoly (Img &cv, const View &v, const Vec *p, int n, unsigned col, int a = 256) { n = to_xy (v, p, n); cv.poly (g_xy, n, col, a); }

// Words on the table, centred on (x, y); angle 90: read from the bottom up (a scratch canvas, its coverage turned)
static Layer g_scratch;
static void words (Layer &to, int x, int y, const char *s, unsigned col, int px, int angle)
{
	int tw = textW (s, px), th = px + px / 3;
	if (angle != 90) { textS (x - tw / 2, y + px / 3, s, col, px); return; }
	if (!g_scratch.alloc (512, 64) || tw + 2 > 512 || th > 64) return;
	to.end ();
	g_scratch.begin ();
	ui_fill (0, 0, 512, 64, 0);
	text (1, px, s, 0xFFFFFF, px);
	g_scratch.end ();
	Img &im = to.img;
	for (int yy = 0; yy < th; yy++) for (int xx = 0; xx < tw + 2; xx++)
	{
		int a = g_scratch.img.px[yy * g_scratch.img.stride + xx] & 255;
		if (a) im.put (x - th / 2 + yy, y + (tw + 2) / 2 - xx, col, a + 1);
	}
	to.begin ();
}
static int label_px (int size, float s) { static const int PX[5] = { 0, 11, 15, 22, 30 }; return (int) lroundf ((float) PX[size < 1 ? 1 : size > 4 ? 4 : size] * s / 0.654f); }

// a lamp (an insert): unlit a dim tint in the playfield, lit its colour, a highlight, a halo
static void lamp (Img &cv, const View &v, Vec c, double r, unsigned col, unsigned bg, bool lit, int pulse = 200)
{
	if (!lit)
	{
		disc (cv, v, c, r, mix (bg, col, 72)); ring (cv, v, c, r - 0.6, 1.2, mix (bg, col, 130), 200);
		Vec h = { c.x - r * 0.3, c.y - r * 0.3 }; disc (cv, v, h, r * 0.3, mix (bg, col, 110));
		return;
	}
	glow (cv, v, c, r * 3.2, shade (col, pulse / 3 + 40));
	disc (cv, v, c, r, col); Vec h = { c.x - r * 0.3, c.y - r * 0.3 }; disc (cv, v, h, r * 0.42, 0xFFFFFF, 180);
}
// a pill-shaped insert with its word (the multiplier row, SHOOT AGAIN)
static void pill (Img &cv, const View &v, Vec c, double w, double h, unsigned col, unsigned bg, bool lit, const char *txt)
{
	int x = (int) lroundf (v.X (c.x - w / 2)), y = (int) lroundf (v.Y (c.y - h / 2)), pw = (int) lroundf ((float) w * v.s), ph = (int) lroundf ((float) h * v.s);
	if (pw < 4 || ph < 3) return;
	if (lit) { cv.glow ((float) (x + pw / 2), (float) (y + ph / 2), (float) pw * 0.8f, shade (col, 110)); cv.rbox (x, y, pw, ph, ph / 2, mix (col, 0xFFFFFF, 90), col); }
	else cv.rbox (x, y, pw, ph, ph / 2, mix (bg, col, 70), mix (bg, col, 60), 256, mix (bg, col, 130));
	if (txt && ph >= 8)
	{
		int px = (int) ((float) ph * 0.72f);
		while (px > 6 && textW (txt, px) > pw - 4) px--;
		textC (x + pw / 2, y + ph / 2 + px * 35 / 100, txt, lit ? 0x201408 : mix (bg, col, 170), px);
	}
}

struct Looks
{
	unsigned slingCol, lampCol;		// the flippers' rubber, the inserts' colour
	int lowL, lowR;				// the lower left and right flippers
	int slingBody[pinball::MAXSLING];	// the closed [wall] behind each sling (-1: none)
};
static void looks_of (const Table &t, Looks &k)
{
	k.slingCol = t.nsling ? t.sling[0].colour : 0xE0559A;
	k.lampCol = 0xFFD34D;
	for (int i = 0; i < t.nlane; i++) if (t.rotate >= 0 && t.lane[i].group == t.rotate) { k.lampCol = t.lane[i].colour; break; }
	k.lowL = k.lowR = -1;
	for (int f = 0; f < t.nflip; f++)
	{
		int &low = t.flip[f].side == pinball::SIDE_LEFT ? k.lowL : k.lowR;
		if (low < 0 || t.flip[f].pivot.y > t.flip[low].pivot.y) low = f;
	}
	for (int s = 0; s < t.nsling; s++)
	{
		k.slingBody[s] = -1;
		Vec a = t.seg[t.sling[s].seg].a;
		for (int w = 0; w < t.nwall && k.slingBody[s] < 0; w++)
		{
			const pinball::Wall &W = t.wall[w];
			if (!W.closed || W.outline) continue;
			for (int i = 0; i < W.n; i++)
			{
				Vec p = t.seg[W.first + i].a;
				if (fabs (p.x - a.x) < 0.01 && fabs (p.y - a.y) < 0.01) { k.slingBody[s] = w; break; }
			}
		}
	}
}
static int wall_points (const Table &t, const pinball::Wall &W, Vec *out, int cap)
{
	int n = 0;
	for (int i = 0; i < W.n && n < cap; i++) out[n++] = t.seg[W.first + i].a;
	if (!W.closed && n < cap && W.n) out[n++] = t.seg[W.first + W.n - 1].b;
	return n;
}

// ---- the static layer ---------------------------------------------------------------------------------------------
// to: the layer (begun, for the words); small: a list's picture (no words, thin rails)
static void draw_static (Layer &to, const Table &t, const View &v, bool small)
{
	Img &cv = to.img;
	unsigned bg = t.background;
	int x0 = (int) lroundf (v.ox), y0 = (int) lroundf (v.oy), w = (int) lroundf ((float) t.w * v.s), h = (int) lroundf ((float) t.h * v.s);
	// the playfield: its colour, lit from the middle, a fine grain of wood or paint
	for (int y = 0; y < h; y++)
	{
		uint32_t *p = cv.px + (long) (y0 + y) * cv.stride + x0;
		float fy = (float) y / (float) h - 0.42f;
		for (int x = 0; x < w; x++)
		{
			float fx = (float) x / (float) w - 0.5f;
			float d = fx * fx * 2.2f + fy * fy * 1.6f;
			float l = 1.28f - d * 1.5f;
			l += (noise ((float) x * 0.012f / v.s * 0.65f, (float) y * 0.05f / v.s * 0.65f, 3) - 0.5f) * 0.10f;
			p[x] = shade (bg, (int) (clampf (l, 0.45f, 1.5f) * 256));
		}
	}
	for (int i = 0; i < t.nshape; i++)
	{
		int n = to_xy (v, t.shape[i].p, t.shape[i].n);
		cv.poly (g_xy, n, shade (t.shape[i].colour, 270));
		if (!small) cv.stroke (g_xy, n, 0.7f, shade (t.shape[i].colour, 340), true, 70);
	}
	if (!small) for (int i = 0; i < t.nlabel; i++)
	{
		const pinball::Label &l = t.label[i]; int px = label_px (l.size, v.s);
		if (px >= 7) words (to, (int) v.X (l.at.x), (int) v.Y (l.at.y), l.text.get (g_lang), l.colour, px, l.angle);
	}
	for (int i = 0; i < t.nramp; i++)				// a ramp: a see-through track, two chrome rails
	{
		const pinball::Ramp &r = t.ramp[i]; stroke (cv, v, r.path, r.npath, r.width, r.colour, false, 60);
		for (int side = -1; side <= 1; side += 2)
		{
			Vec o[pinball::MAXPTS];
			for (int k = 0; k < r.npath; k++)
			{
				Vec a = r.path[k > 0 ? k - 1 : k], b = r.path[k < r.npath - 1 ? k + 1 : k];
				double dx = b.x - a.x, dy = b.y - a.y, l = sqrt (dx * dx + dy * dy); if (l <= 0) l = 1;
				o[k].x = r.path[k].x - dy / l * side * r.width / 2; o[k].y = r.path[k].y + dx / l * side * r.width / 2;
			}
			Vec sh[pinball::MAXPTS]; for (int k = 0; k < r.npath; k++) { sh[k].x = o[k].x + 3; sh[k].y = o[k].y + 4; }
			stroke (cv, v, sh, r.npath, small ? 2.5 : 2, 0x000000, false, 90);
			stroke (cv, v, o, r.npath, small ? 2.5 : 2, mix (r.colour, 0xFFFFFF, 100), false, 240);
			stroke (cv, v, o, r.npath, 0.8, 0xFFFFFF, false, 160);
		}
	}
	static Vec pts[MAXDRAW], sh[MAXDRAW];
	for (int pass = 0; pass < 2; pass++)				// the walls' shadows, then the walls
		for (int i = 0; i < t.nwall; i++)
		{
			const pinball::Wall &W = t.wall[i];
			int n = wall_points (t, W, pts, MAXDRAW);
			if (pass == 0)
			{
				if (W.outline) continue;
				for (int k = 0; k < n; k++) { sh[k].x = pts[k].x + 4; sh[k].y = pts[k].y + 6; }
				if (W.closed) fillpoly (cv, v, sh, n, 0x000000, 110);
				else stroke (cv, v, sh, n, W.width + 1, 0x000000, false, 100);
				continue;
			}
			if (W.outline)					// the side rails, chrome
			{
				stroke (cv, v, pts, n, 8, shade (W.colour, 150), true);
				stroke (cv, v, pts, n, 4, W.colour, true);
				if (!small) stroke (cv, v, pts, n, 1.2, mix (W.colour, 0xFFFFFF, 170), true, 200);
			}
			else if (W.closed)
			{
				fillpoly (cv, v, pts, n, mix (bg, W.colour, 96));
				stroke (cv, v, pts, n, 2.5, W.colour, true);
				if (!small) stroke (cv, v, pts, n, 0.9, mix (W.colour, 0xFFFFFF, 150), true, 180);
			}
			else
			{
				stroke (cv, v, pts, n, W.width, W.colour);
				if (!small) stroke (cv, v, pts, n, W.width * 0.3, mix (W.colour, 0xFFFFFF, 150), false, 170);
			}
		}
	for (int i = 0; i < t.narc; i++)
	{
		const pinball::Arc &a = t.arc[i];
		enum { NA = 40 }; Vec p[NA + 1];
		double f = a.from * PB_RAD, sp = (a.to - a.from) * PB_RAD;
		for (int k = 0; k <= NA; k++) { double q = f + sp * k / NA; p[k].x = a.c.x + cos (q) * a.r; p[k].y = a.c.y + sin (q) * a.r; }
		stroke (cv, v, p, NA + 1, a.width, a.colour);
		if (!small) stroke (cv, v, p, NA + 1, a.width * 0.3, mix (a.colour, 0xFFFFFF, 150), false, 170);
	}
	for (int i = 0; i < t.nsling; i++) { const pinball::Seg &s = t.seg[t.sling[i].seg]; Vec p[2] = { s.a, s.b }; stroke (cv, v, p, 2, 5, 0xF4F4F4); }	// the rubber
	for (int i = 0; i < t.ncirc; i++)
		if (t.circ[i].kind == pinball::C_POST)
		{
			Vec c = t.circ[i].c, s = { c.x + 3, c.y + 4 };
			disc (cv, v, s, t.circ[i].r + 2.5, 0x000000, 90);
			disc (cv, v, c, t.circ[i].r + 2.5, 0xF4F4F4); disc (cv, v, c, t.circ[i].r - 1, mix (t.circ[i].colour, 0x000000, 60));
			Vec h = { c.x - 1.5, c.y - 1.5 }; disc (cv, v, h, t.circ[i].r * 0.4, 0xFFFFFF, 150);
		}
	for (int i = 0; i < t.ngate; i++) { Vec p[2] = { t.gate[i].a, t.gate[i].b }; stroke (cv, v, p, 2, 2, 0xC8CCD4); disc (cv, v, t.gate[i].a, 3, 0xC8CCD4); }
	for (int i = 0; i < t.nsaucer; i++)
	{
		disc (cv, v, t.saucer[i].c, t.saucer[i].r + 4, mix (bg, t.saucer[i].colour, 110));
		disc (cv, v, t.saucer[i].c, t.saucer[i].r, mix (bg, 0x000000, 190));
		Vec s = { t.saucer[i].c.x + 2, t.saucer[i].c.y + 3 }; disc (cv, v, s, t.saucer[i].r * 0.7, 0x000000, 90);
	}
	if (!small)							// the plunger's housing
	{
		Vec a = { t.plunger.x - 14, t.h - 8 }, b = { t.plunger.x + 14, t.h - 8 };
		quad (cv, v, a, b, 12, mix (bg, 0x000000, 120));
	}
}

// ---- the moving parts --------------------------------------------------------------------------------------------
static void ball (Img &cv, const View &v, Vec c, double r)		// a chrome ball
{
	Vec sh = { c.x + 3, c.y + 4.5 }; disc (cv, v, sh, r * 1.05, 0x000000, 96);
	disc (cv, v, c, r, 0x5C6470);
	Vec m = { c.x - r * 0.12, c.y - r * 0.14 }; disc (cv, v, m, r * 0.86, 0xB0B8C4);
	Vec m2 = { c.x - r * 0.24, c.y - r * 0.26 }; disc (cv, v, m2, r * 0.56, 0xE4E8EE);
	Vec low = { c.x + r * 0.26, c.y + r * 0.4 }; disc (cv, v, low, r * 0.34, 0x8894A8, 170);
	Vec h = { c.x - r * 0.4, c.y - r * 0.42 }; disc (cv, v, h, r * 0.24, 0xFFFFFF);
}
// a flipper at its world angle: a shadow, a capsule of the rubber's colour, its ivory body, the pivot's screw
static void flipper (Img &cv, const View &v, const pinball::Flip &f, double ang, unsigned rubber, bool dead, unsigned bg)
{
	double dx = cos (ang), dy = sin (ang), nx = -dy, ny = dx;
	Vec tip = { f.pivot.x + dx * f.len, f.pivot.y + dy * f.len };
	for (int pass = 0; pass < 3; pass++)
	{
		double k = pass == 2 ? 2.6 : 0, o = pass == 0 ? 5 : 0; Vec pp[40]; int n = 0;
		for (int i = 0; i <= 16; i++) { double a = M_PI / 2 + M_PI * i / 16; double cx = cos (a), cy = sin (a);
			pp[n].x = f.pivot.x + o * 0.7 + (dx * cx + nx * cy) * (f.r0 - k); pp[n].y = f.pivot.y + o + (dy * cx + ny * cy) * (f.r0 - k); n++; }
		for (int i = 0; i <= 16; i++) { double a = -M_PI / 2 + M_PI * i / 16; double cx = cos (a), cy = sin (a);
			pp[n].x = tip.x + o * 0.7 + (dx * cx + nx * cy) * (f.r1 - k); pp[n].y = tip.y + o + (dy * cx + ny * cy) * (f.r1 - k); n++; }
		if (pass == 0) { fillpoly (cv, v, pp, n, 0x000000, 100); continue; }
		unsigned c = pass == 2 ? 0xF2F4F8 : rubber;
		if (dead) c = mix (c, bg, 150);
		fillpoly (cv, v, pp, n, c);
	}
	Vec a = { f.pivot.x - ny * 1.5, f.pivot.y + nx * -1.5 }, b = { tip.x - ny * 1.5, tip.y + nx * -1.5 };
	if (!dead) quad (cv, v, a, b, 1.4, 0xFFFFFF, 200);
	disc (cv, v, f.pivot, 3.2, 0x8A9099); Vec h = { f.pivot.x - 0.8, f.pivot.y - 0.8 }; disc (cv, v, h, 1.4, 0xD8DCE2);
}

// g 0: the table at rest (every lamp off, no ball: the picker's preview)
static void draw_dynamic (Img &cv, const Table &t, const Looks &k, const View &v, const Game *g, unsigned tick)
{
	unsigned bg = t.background; bool dead = g && g->tilted;
	const World *w = g ? &g->w : 0;
	int pulse = (int) ((sinf ((float) tick * 0.12f) + 1) * 100) + 56;
	if (w) for (int s = 0; s < t.nsling; s++)
		if (w->slingFlash[s] > 0 && k.slingBody[s] >= 0)
		{
			static Vec pts[MAXDRAW]; const pinball::Wall &W = t.wall[k.slingBody[s]];
			int n = wall_points (t, W, pts, MAXDRAW);
			fillpoly (cv, v, pts, n, mix (mix (bg, W.colour, 90), 0xFFFFFF, 120)); stroke (cv, v, pts, n, 2.5, mix (W.colour, 0xFFFFFF, 120), true);
			const pinball::Seg &sg = t.seg[t.sling[s].seg]; Vec pp[2] = { sg.a, sg.b }; stroke (cv, v, pp, 2, 5, 0xFFFFFF);
		}
	for (int i = 0; i < t.nlane; i++)
	{
		const pinball::Lane &l = t.lane[i]; Vec c = { l.x + l.w / 2, l.y + l.h / 2 };
		lamp (cv, v, c, (l.w < l.h ? l.w : l.h) * 0.3, l.colour, bg, w && !dead && w->laneLit[i], pulse);
		Vec ab[2] = { { l.x + 3, l.y + 4 }, { l.x + l.w - 3, l.y + 4 } }; stroke (cv, v, ab, 2, 1.4, 0xB8BEC8, false, 200);
	}
	for (int i = 0; i < t.ntgt; i++)
	{
		const pinball::Target &tg = t.tgt[i]; const pinball::Seg &s = t.seg[tg.seg];
		double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y, l = sqrt (dx * dx + dy * dy); if (l <= 0) continue;
		double nx = -dy / l, ny = dx / l;
		Vec m = { (s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2 };
		if ((m.x - t.w / 2) * nx + (m.y - t.h / 2) * ny < 0) { nx = -nx; ny = -ny; }
		if (tg.drop)
		{
			if (w && w->tgtDown[i]) { quad (cv, v, s.a, s.b, 3, mix (bg, 0x000000, 170)); continue; }
			Vec a = { s.a.x + nx * 4, s.a.y + ny * 4 }, b = { s.b.x + nx * 4, s.b.y + ny * 4 };
			quad (cv, v, a, b, 9, dead ? mix (tg.colour, bg, 140) : tg.colour);
			quad (cv, v, s.a, s.b, 2, 0xFFFFFF, 150);
		}
		else
		{
			Vec lc = { m.x + nx * 16, m.y + ny * 16 };
			lamp (cv, v, lc, 6, tg.colour, bg, w && !dead && w->tgtLit[i], pulse);
			Vec a = { s.a.x + nx * 3, s.a.y + ny * 3 }, b = { s.b.x + nx * 3, s.b.y + ny * 3 };
			quad (cv, v, a, b, 7, 0xF0F0F0); quad (cv, v, s.a, s.b, 2.5, tg.colour);
		}
	}
	for (int i = 0; i < t.ncirc; i++)				// pop bumpers: a skirt, a cap, a lit top
	{
		const pinball::Circle &b = t.circ[i]; if (b.kind != pinball::C_BUMPER) continue;
		bool fl = w && !dead && w->bumperFlash[i] > 0;
		Vec sh = { b.c.x + 4, b.c.y + 6 }; disc (cv, v, sh, b.r, 0x000000, 100);
		if (fl) { glow (cv, v, b.c, b.r * 2.6, shade (b.colour, 160)); ring (cv, v, b.c, b.r * (1.5 - w->bumperFlash[i] * 0.05), 3, 0xFFFFFF, 120); }
		disc (cv, v, b.c, b.r, mix (b.colour, 0x000000, 150));
		Vec c1 = { b.c.x - 0.8, b.c.y - 1.2 };
		disc (cv, v, c1, b.r * 0.86, fl ? mix (b.colour, 0xFFFFFF, 120) : dead ? mix (b.colour, bg, 120) : b.colour);
		ring (cv, v, c1, b.r * 0.86, 1.2, mix (b.colour, 0x000000, 90), 160);
		disc (cv, v, c1, b.r * 0.52, fl ? 0xFFFFFF : mix (b.colour, 0xFFFFFF, 150));
		Vec h = { b.c.x - b.r * 0.2, b.c.y - b.r * 0.24 }; disc (cv, v, h, b.r * 0.16, 0xFFFFFF, 210);
	}
	for (int i = 0; i < t.nsaucer; i++)
	{
		const pinball::Saucer &s = t.saucer[i]; bool lit = false;
		if (w && !dead) for (int b = 0; b < pinball::MAXBALL; b++) if (w->ball[b].state == pinball::B_SAUCER && w->ball[b].saucer == i) lit = true;
		if (lit) { glow (cv, v, s.c, s.r + 16, shade (s.colour, 110)); ring (cv, v, s.c, s.r + 2, 3, s.colour); }
		else ring (cv, v, s.c, s.r + 2, 2, mix (bg, s.colour, 120), 220);
	}
	for (int i = 0; i < t.nramp; i++)
	{
		const pinball::Ramp &r = t.ramp[i]; Vec m = { (r.a.x + r.b.x) / 2 - r.pass.x * 34, (r.a.y + r.b.y) / 2 - r.pass.y * 34 };
		double px = r.pass.x, py = r.pass.y, qx = -py, qy = px;
		Vec tri[3] = { { m.x + px * 11, m.y + py * 11 }, { m.x - px * 8 + qx * 9, m.y - py * 8 + qy * 9 }, { m.x - px * 8 - qx * 9, m.y - py * 8 - qy * 9 } };
		bool lit = w && !dead && w->rampFlash[i] > 0;
		if (lit) { glow (cv, v, m, 26, shade (r.colour, 130)); fillpoly (cv, v, tri, 3, r.colour); }
		else fillpoly (cv, v, tri, 3, mix (bg, r.colour, 80));
	}
	if (k.lowL >= 0 && k.lowR >= 0)					// the bonus multiplier, SHOOT AGAIN
	{
		double fx = (t.flip[k.lowL].pivot.x + t.flip[k.lowR].pivot.x) / 2, fy = t.flip[k.lowL].pivot.y;
		for (int i = 0; i < 4; i++)
		{
			char s[8]; snprintf (s, sizeof s, "%d\xC3\x97", i + 2); Vec c = { fx + (i - 1.5) * 38, fy - 115 };
			pill (cv, v, c, 32, 18, k.lampCol, bg, g && !dead && g->mult >= i + 2, s);
		}
		bool sa = g && (g->extraBalls > 0 || (g->ballSaveActive () && (g->frameNo / 15) % 2 == 0));
		Vec c = { fx, fy + 74 }; pill (cv, v, c, 112, 17, 0xF04848, bg, sa, g_lang ? "REJOUEZ" : "SHOOT AGAIN");
	}
	for (int i = 0; i < t.nflip; i++) flipper (cv, v, t.flip[i], w ? w->flipAng[i] : t.flip[i].rest, k.slingCol, dead, bg);
	double pull = w ? w->pull : 0;
	{								// the plunger: a knob on a spring
		double y = t.plunger.y + 15 + pull * 36, bottom = t.h - 10; Vec a = { t.plunger.x - 12, y }, b = { t.plunger.x + 12, y };
		Vec z[12]; int n = 0;
		for (double yy = y + 6; yy < bottom && n < 12; yy += (bottom - y - 6) / 9, n++) { z[n].x = t.plunger.x + (n & 1 ? 7 : -7); z[n].y = yy; }
		stroke (cv, v, z, n, 1.8, 0x9AA0A8);
		quad (cv, v, a, b, 8, 0xC8CCD4); quad (cv, v, a, b, 2, 0xFFFFFF, 120);
	}
}
static void draw_balls (Img &cv, const Table &t, const View &v, const World *w)
{
	double pull = w->pull;
	for (int i = 0; i < pinball::MAXBALL; i++)
	{
		const pinball::Ball &B = w->ball[i];
		if (B.state == pinball::B_PLAY || B.state == pinball::B_SAUCER) ball (cv, v, B.p, t.ball);
		else if (B.state == pinball::B_PLUNGER) { Vec p = { B.p.x, B.p.y + pull * 36 }; ball (cv, v, p, t.ball); }
	}
	for (int i = 0; i < pinball::MAXBALL; i++)			// a ball on a ramp: above the table
		if (w->ball[i].state == pinball::B_RAMP) ball (cv, v, w->rampPos (i), t.ball + 1);
}

#endif
