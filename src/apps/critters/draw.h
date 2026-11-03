//
// draw.h -- Critters' drawing: the creatures, the hatch, the exit, the roles' pictures (the drawings of Onyx, on
// gameart.h), each creature pre-rendered once into a sprite with its opacity; the terrain relit (a rim of light on
// the surfaces, darker with depth), a sky made for each level, moving water and lava.
//
#ifndef CRITTERS_DRAW_H
#define CRITTERS_DRAW_H

static int g_lang = 0;				// 0 English, 1 French (the levels have both)
static void mmss (int s, char *b, int cap) { if (s < 0) s = 0; snprintf (b, (size_t) cap, "%d:%02d", s / 60, s % 60); }
static const char *ROLE_NAME[critters::NROLES] = { "Grimpeur", "Planeur", "Bloqueur", "B\xC3\xA2tisseur", "Creuseur", "Artificier", "Perceur", "Mineur" };
enum { NBUILT = 6 };				// the roles built

static bool g_grey;				// the pictures of an empty slot: every colour greyed
static unsigned C (unsigned c)
{
	if (!g_grey) return c;
	int b = (int) (((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 5 + (c & 255) * 2) / 10;
	b = 110 + b / 3;
	return (unsigned) (b << 16 | b << 8 | b);
}
static void el (Img &cv, double x, double y, double rx, double ry, unsigned c, int a = 255) { cv.ellipse ((float) x, (float) y, (float) rx, (float) ry, C (c), a + 1); }
static void ln (Img &cv, double x0, double y0, double x1, double y1, double w, unsigned c, int a = 255) { cv.line ((float) x0, (float) y0, (float) x1, (float) y1, (float) w, C (c), a + 1, false); }
static void box (Img &cv, double x, double y, double w, double h, unsigned c, int a = 255)
{
	float q[8] = { (float) x, (float) y, (float) (x + w), (float) y, (float) (x + w), (float) (y + h), (float) x, (float) (y + h) };
	cv.poly (q, 4, C (c), a + 1);
}
static void arc (Img &cv, double cx, double cy, double r, int a0, int a1, double w, unsigned c)	// degrees, anticlockwise on screen
{
	float p[2 * 20]; int n = 0;
	for (int i = 0; i < 20; i++) { double a = (a0 + (a1 - a0) * i / 19.0) * M_PI / 180; p[n++] = (float) (cx + cos (a) * r); p[n++] = (float) (cy - sin (a) * r); }
	cv.stroke (p, 20, (float) w, C (c));
}
static void digit (int cx, int base, const char *s, int px, unsigned ink)
{
	int w = textW (s, px);
	for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (dx || dy) text (cx - w / 2 + dx, base + dy, s, 0x1A0E06, px);
	text (cx - w / 2, base, s, ink, px);
}

// ---- the creatures -----------------------------------------------------------------------------------------------------
enum { D_WALK, D_FALL, D_FLOAT, D_CLIMB, D_BLOCK, D_BUILD, D_DIG, D_SHRUG, D_SPLAT, ND };
struct Look { int state, frame, dir, flags; bool hot; int fuse; };
static const unsigned BODY = 0xFFC24A, BODY_DK = 0xE58E22, BELLY = 0xFFE7A6, LINE = 0x4A2A10, LEAF = 0x7ED957, LEAF_DK = 0x3E8A2E, FEET = 0x3A2412;
static unsigned g_brickCol = 0xC8A060;

static void critter (Img &cv, double sx, double sy, const Look &c, double k = 1.0)
{
	int d = c.dir; bool front = c.state == D_BLOCK;
	if (c.state == D_SPLAT)
	{
		el (cv, sx, sy - 2.5 * k, 10.5 * k, 3.6 * k, LINE); el (cv, sx, sy - 2.6 * k, 9.5 * k, 2.8 * k, BODY);
		for (int e = -1; e <= 1; e += 2) { ln (cv, sx + e * 3 * k - 1.3 * k, sy - 4 * k, sx + e * 3 * k + 1.3 * k, sy - 1.6 * k, 0.9 * k, LINE); ln (cv, sx + e * 3 * k + 1.3 * k, sy - 4 * k, sx + e * 3 * k - 1.3 * k, sy - 1.6 * k, 0.9 * k, LINE); }
		return;
	}
	double bob = (c.state == D_WALK && (c.frame & 1)) ? -0.6 * k : 0, cy = sy - 11 * k + bob;
	if (c.state == D_DIG) cy += 2 * k;
	unsigned body = c.hot ? mix (BODY, 0xFF4A30, 150) : BODY;
	if (c.state == D_FLOAT)
	{
		ln (cv, sx - 7 * k, cy - 4 * k, sx - 1 * k, cy - 18 * k, 0.8 * k, LEAF_DK); ln (cv, sx + 7 * k, cy - 4 * k, sx + 1 * k, cy - 18 * k, 0.8 * k, LEAF_DK);
		el (cv, sx, cy - 19 * k, 12 * k, 4.6 * k, LEAF_DK); el (cv, sx, cy - 19.6 * k, 11 * k, 3.6 * k, LEAF);
		ln (cv, sx - 10 * k, cy - 19 * k, sx + 10 * k, cy - 19 * k, 0.7 * k, LEAF_DK);
	}
	double f = c.state == D_WALK ? (c.frame % 4 == 0 ? 1.6 : c.frame % 4 == 2 ? -1.6 : 0) : 0;
	if (c.state == D_FALL || c.state == D_FLOAT) { el (cv, sx - 1.8 * k, sy - 1.6 * k, 2 * k, 1.5 * k, FEET); el (cv, sx + 1.8 * k, sy - 1.6 * k, 2 * k, 1.5 * k, FEET); }
	else if (c.state == D_CLIMB) { double s = (c.frame & 1) ? 1.2 : 0; el (cv, sx + d * 6 * k, sy - (2 + s) * k, 1.6 * k, 2.2 * k, FEET); el (cv, sx + d * 6 * k, sy - (6 - s) * k, 1.6 * k, 2.2 * k, FEET); }
	else { el (cv, sx - 3.2 * k + f * k, sy - 1.4 * k, 2.6 * k, 1.5 * k, FEET); el (cv, sx + 3.2 * k - f * k, sy - 1.4 * k, 2.6 * k, 1.5 * k, FEET); }
	auto arm = [&] (double ax, double ay) { el (cv, ax, ay, 2.6 * k, 2.2 * k, LINE); el (cv, ax, ay, 1.8 * k, 1.5 * k, BODY_DK); };
	if (c.state == D_BLOCK) { el (cv, sx - 10.5 * k, cy, 3.8 * k, 2.4 * k, LINE); el (cv, sx - 10.5 * k, cy, 3 * k, 1.6 * k, BODY_DK); el (cv, sx + 10.5 * k, cy, 3.8 * k, 2.4 * k, LINE); el (cv, sx + 10.5 * k, cy, 3 * k, 1.6 * k, BODY_DK); }
	if (c.state == D_SHRUG || c.state == D_FALL) { arm (sx - 8.5 * k, cy - 7 * k); arm (sx + 8.5 * k, cy - 7 * k); }
	el (cv, sx, cy, 8.8 * k, 9.8 * k, LINE);
	el (cv, sx, cy, 7.9 * k, 8.9 * k, body);
	el (cv, sx, cy + 4.2 * k, 6.4 * k, 4.4 * k, BODY_DK, 70);
	el (cv, sx + (front ? 0 : d * 1.2 * k), cy + 2.6 * k, 4.6 * k, 4.4 * k, BELLY, 210);
	el (cv, sx - 3.2 * k, cy - 5 * k, 2.2 * k, 1.5 * k, 0xFFFFFF, 140);
	if (c.flags & critters::F_CLIMBER) arc (cv, sx, cy, 7.6 * k, 35, 145, 2.2 * k, 0x2EC4E8);
	if (c.state != D_FLOAT)
	{
		double tx = sx + (front ? 0 : -d * 0.6 * k), ty = cy - 9.6 * k;
		ln (cv, tx, ty, tx + 0.8 * k, ty - 3.6 * k, 1.1 * k, LEAF_DK);
		el (cv, tx + 3 * k, ty - 3.8 * k, 3 * k, 1.5 * k, LEAF_DK); el (cv, tx + 3 * k, ty - 3.9 * k, 2.3 * k, 1 * k, LEAF);
		if (c.flags & critters::F_FLOATER) { el (cv, tx - 2.4 * k, ty - 3.4 * k, 2.8 * k, 1.6 * k, LEAF_DK); el (cv, tx - 2.4 * k, ty - 3.5 * k, 2.1 * k, 1.1 * k, 0xB8F07A); }
	}
	double ex = front ? 0 : d * 2.4 * k, ey = cy - 2.6 * k, er = (c.state == D_FALL ? 2.9 : 2.4) * k;
	double px = front ? 0 : d * 0.9 * k, py = c.state == D_CLIMB || c.state == D_FALL ? -0.9 * k : c.state == D_DIG || c.state == D_BUILD ? 0.8 * k : 0;
	for (int e = -1; e <= 1; e += 2)
	{
		el (cv, sx + ex + e * 3 * k, ey, er, er * 1.1, 0xFFFFFF);
		el (cv, sx + ex + e * 3 * k + px, ey + py, 1.3 * k, 1.5 * k, 0x1A0E06);
	}
	if (c.state == D_BLOCK) { ln (cv, sx - 5.4 * k, ey - 3.6 * k, sx - 1.6 * k, ey - 2.6 * k, 1 * k, LINE); ln (cv, sx + 5.4 * k, ey - 3.6 * k, sx + 1.6 * k, ey - 2.6 * k, 1 * k, LINE); }
	if (c.state == D_FALL) el (cv, sx + ex, cy + 3.2 * k, 1.5 * k, 1.8 * k, LINE);
	if (c.state == D_CLIMB) { arm (sx + d * 8 * k, cy - 6 * k); arm (sx + d * 8.5 * k, cy + 1 * k); }
	if (c.state == D_BUILD)
	{
		double bx = sx + d * 8.5 * k, by = cy + 2 * k + ((c.frame & 3) == 3 ? 1.5 * k : 0);
		box (cv, bx - 4 * k, by - 2.2 * k, 8 * k, 4.4 * k, LINE); box (cv, bx - 3.3 * k, by - 1.5 * k, 6.6 * k, 3 * k, g_brickCol); box (cv, bx - 3.3 * k, by - 1.5 * k, 6.6 * k, 1 * k, shade (g_brickCol, 330));
		arm (bx - d * 3.4 * k, by + 1 * k);
	}
	if (c.state == D_DIG)
	{
		arm (sx - 6.5 * k, sy - 2.5 * k); arm (sx + 6.5 * k, sy - 2.5 * k);
		static const double DUST[6][2] = { { -10, -3 }, { -12, -6 }, { -8, -8 }, { 10, -4 }, { 12, -7 }, { 9, -9 } };
		for (int i = 0; i < 6; i++) if ((i + c.frame) % 3) el (cv, sx + DUST[i][0] * k, sy + DUST[i][1] * k, 1.3 * k, 1.3 * k, 0xB08860, 220);
	}
}

static void brackets (Img &cv, double sx, double sy, unsigned c, bool focus)
{
	double x0 = sx - 11, x1 = sx + 11, y0 = sy - 27, y1 = sy + 2, L = 6, w = 1.6;
	ln (cv, x0, y0, x0 + L, y0, w, c); ln (cv, x0, y0, x0, y0 + L, w, c); ln (cv, x1, y0, x1 - L, y0, w, c); ln (cv, x1, y0, x1, y0 + L, w, c);
	ln (cv, x0, y1, x0 + L, y1, w, c); ln (cv, x0, y1, x0, y1 - L, w, c); ln (cv, x1, y1, x1 - L, y1, w, c); ln (cv, x1, y1, x1, y1 - L, w, c);
	if (focus) { float t[6] = { (float) sx - 5, (float) y0 - 9, (float) sx + 5, (float) y0 - 9, (float) sx, (float) y0 - 3 }; cv.poly (t, 3, 0xFFD060); }
}

// ---- the hatch and the exit ----------------------------------------------------------------------------------------
static void hatch (Img &cv, double hx, double hy, bool open, double k = 1)
{
	double cy = hy - 12 * k;
	el (cv, hx + 3, cy + 6, 25 * k, 11 * k, 0x000000, 70);
	cv.rbox ((int) (hx - 23 * k), (int) (cy - 11 * k), (int) (46 * k), (int) (19 * k), (int) (9 * k), 0x2A1E16, 0x2A1E16);
	cv.rbox ((int) (hx - 22 * k), (int) (cy - 10 * k), (int) (44 * k), (int) (17 * k), (int) (8 * k), 0x7A6654, 0x54443A);
	el (cv, hx - 6 * k, cy - 6 * k, 12 * k, 3 * k, 0x9A8470, 200);
	el (cv, hx - 15 * k, cy - 10 * k, 5 * k, 2.4 * k, 0x5E8A32); el (cv, hx - 9 * k, cy - 11 * k, 4 * k, 2 * k, 0x7EB04A); el (cv, hx + 14 * k, cy - 9.5 * k, 4.5 * k, 2.2 * k, 0x5E8A32);
	if (!open)
	{
		el (cv, hx, cy + 6 * k, 14 * k, 4.6 * k, 0x2A1A0C); el (cv, hx, cy + 5.6 * k, 13 * k, 3.8 * k, 0xA8783E);
		ln (cv, hx - 5 * k, cy + 2.4 * k, hx - 5 * k, cy + 9 * k, 0.8 * k, 0x6A4420); ln (cv, hx + 5 * k, cy + 2.4 * k, hx + 5 * k, cy + 9 * k, 0.8 * k, 0x6A4420);
		el (cv, hx + 8 * k, cy + 5.6 * k, 1.2 * k, 1.2 * k, 0xE8D090);
	}
	else
	{
		el (cv, hx, cy + 6 * k, 14 * k, 4.6 * k, 0x2A1A0C); el (cv, hx, cy + 6 * k, 12.4 * k, 3.6 * k, 0x0E0804);
		el (cv, hx - 15 * k, cy + 14 * k, 3.4 * k, 9.5 * k, 0x2A1A0C); el (cv, hx - 15 * k, cy + 14 * k, 2.6 * k, 8.6 * k, 0xA8783E);
		el (cv, hx, cy + 8 * k, 8 * k, 1.4 * k, 0xFFD890, 60);
	}
}
static void portal (Img &cv, double ex, double ey, int pulse, unsigned tick, double k = 1)
{
	double top = ey - 36 * k;
	cv.glow ((float) ex, (float) (ey - 18 * k), (float) (46 * k), shade (0x207868, 150 + pulse / 3));
	box (cv, ex - 10 * k, top + 10 * k, 20 * k, 26 * k, 0x1C8C84); el (cv, ex, top + 10 * k, 10 * k, 10 * k, 0x1C8C84);
	box (cv, ex - 7 * k, top + 12 * k, 14 * k, 24 * k, 0x3ED8C4); el (cv, ex, top + 12 * k, 7 * k, 7 * k, 0x3ED8C4);
	el (cv, ex, ey - 13 * k, 4.5 * k, 12 * k, 0xD8FFF6, 150 + pulse / 3);
	arc (cv, ex, top + 10 * k, 13 * k, 0, 180, 6 * k, 0x3A3028);
	arc (cv, ex, top + 10 * k, 13 * k, 0, 180, 4.4 * k, 0xA89A88);
	box (cv, ex - 16 * k, top + 10 * k, 6 * k, 26 * k, 0x3A3028); box (cv, ex + 10 * k, top + 10 * k, 6 * k, 26 * k, 0x3A3028);
	box (cv, ex - 15.2 * k, top + 10 * k, 4.4 * k, 26 * k, 0xA89A88); box (cv, ex + 10.8 * k, top + 10 * k, 4.4 * k, 26 * k, 0x8C7E6E);
	el (cv, ex, top - 3 * k, 2.4 * k, 2.4 * k, 0x7ED957);
	box (cv, ex - 18 * k, ey - 1 * k, 36 * k, 3 * k, 0x6E6254);
	for (int i = 0; i < 6; i++)					// des etincelles montent dans la porte
	{
		unsigned ph = (tick / 2 + (unsigned) i * 17) % 40;
		el (cv, ex + sin ((double) i * 2.4 + (double) ph * 0.2) * 6 * k, ey - ((double) ph * 0.75) * k - 4, 1.1 * k, 1.1 * k, 0xFFFFFF, 240 - (int) ph * 5);
	}
}

// ---- the roles' pictures ------------------------------------------------------------------------------------------------
static void role_icon (Img &cv, int role, int cx, int cy, int size, bool off)
{
	using namespace critters;
	g_grey = off;
	double k = size / 34.0, sx = cx, sy = cy + 13 * k;
	Look c = { D_WALK, 1, 1, 0, false, 0 };
	switch (role)
	{
	case R_CLIMBER: c.state = D_CLIMB; c.flags = F_CLIMBER; c.frame = 0; sx -= 4 * k; box (cv, cx + 9 * k, cy - 16 * k, 6 * k, 32 * k, 0x8890A0); box (cv, cx + 9 * k, cy - 16 * k, 1.5 * k, 32 * k, 0xB8C0CC); break;
	case R_FLOATER: c.state = D_FLOAT; c.flags = F_FLOATER; sy += 2 * k; break;
	case R_BLOCKER: c.state = D_BLOCK; break;
	case R_BUILDER: c.state = D_BUILD; c.frame = 0; sx -= 4 * k;
		for (int i = 0; i < 3; i++) { box (cv, cx + (3 + i * 5) * k, sy - (2 + i * 3.5) * k, 7 * k, 3 * k, 0x7A5A2A); box (cv, cx + (3.5 + i * 5) * k, sy - (1.6 + i * 3.5) * k, 6 * k, 2.2 * k, 0xC8A060); } break;
	case R_DIGGER: c.state = D_DIG; c.frame = 1; box (cv, cx - 15 * k, sy, 30 * k, 4 * k, 0x8A5A34); box (cv, cx - 5 * k, sy, 10 * k, 4 * k, 0x3A2412); break;
	case R_EXPLODER: sy += 4 * k; break;
	}
	unsigned keep = g_brickCol; g_brickCol = 0xC8A060;
	critter (cv, sx, sy, c, k);
	g_brickCol = keep;
	g_grey = false;
}
static void nuke_icon (Img &cv, int cx, int cy, int size, bool off)
{
	g_grey = off; double r = size / 2.0; float xy[48];
	for (int i = 0; i < 24; i++) { double a = i * M_PI / 12, rr = (i & 1) ? r * 0.55 : r; xy[2 * i] = (float) (cx + cos (a) * rr); xy[2 * i + 1] = (float) (cy + sin (a) * rr); }
	cv.poly (xy, 24, C (0xE84A2A));
	el (cv, cx, cy, r * 0.45, r * 0.45, 0xFFD24A); el (cv, cx, cy, r * 0.22, r * 0.22, 0xFFFFFF, 200);
	g_grey = false;
}

// ---- sprites with an opacity (a drawing on black, on white: the opacity is the difference) -------------------------------
template <int W, int H> struct Spr { uint32_t col[W * H]; uint8_t a[W * H]; };
template <int W, int H, class Fn> static void make_spr (Spr<W, H> *f, Fn draw)
{
	static uint32_t pb[W * H], pw[W * H];
	Img b, w; b.set (pb, W, H, W); w.set (pw, W, H, W);
	b.rect (0, 0, W, H, 0x000000); draw (b);
	w.rect (0, 0, W, H, 0xFFFFFF); draw (w);
	for (int i = 0; i < W * H; i++)
	{
		int a = 255 - ((int) ((pw[i] >> 8) & 255) - (int) ((pb[i] >> 8) & 255));
		f->col[i] = pb[i] & 0xFFFFFF; f->a[i] = (uint8_t) (a < 0 ? 0 : a > 255 ? 255 : a);
	}
}
template <int W, int H> static void blend_spr (Img &cv, const Spr<W, H> *f, int x0, int y0, int sink = 0)
{
	if (!f) return;
	y0 += sink;
	for (int y = 0; y < H - sink; y++)
	{
		int Y = y0 + y; if (Y < cv.cy0 || Y >= cv.cy1) continue;
		uint32_t *row = cv.px + (long) Y * cv.stride;
		const uint32_t *src = f->col + y * W; const uint8_t *al = f->a + y * W;
		for (int x = 0; x < W; x++)
		{
			int a = al[x]; if (!a) continue;
			int X = x0 + x; if (X < cv.cx0 || X >= cv.cx1) continue;
			if (a == 255) { row[X] = src[x]; continue; }
			uint32_t d = row[X], s = src[x]; int na = 255 - a;
			unsigned r = ((s >> 16) & 255) + ((d >> 16) & 255) * (unsigned) na / 255, g = ((s >> 8) & 255) + ((d >> 8) & 255) * (unsigned) na / 255, bl = (s & 255) + (d & 255) * (unsigned) na / 255;
			row[X] = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl);
		}
	}
}
enum { SPW = 36, SPH = 48, SPX = 18, SPY = 42, NSHRINK = 8, ICON = 44 };
typedef Spr<SPW, SPH> Frame;
static Frame *g_frames[ND * 4 * 2 * 4 * 2 * NSHRINK];
static unsigned g_framesBrick;
static const Frame *frame_of (const Look &lk, int shrink)
{
	if (g_framesBrick != g_brickCol) { for (Frame *&f : g_frames) { delete f; f = 0; } g_framesBrick = g_brickCol; }
	int key = ((((lk.state * 4 + (lk.frame & 3)) * 2 + (lk.dir > 0)) * 4 + (lk.flags & 3)) * 2 + (lk.hot ? 1 : 0)) * NSHRINK + shrink;
	if (g_frames[key]) return g_frames[key];
	Look l = lk; l.fuse = 0;
	double k = 1.0 - shrink / (double) NSHRINK;
	Frame *f = new Frame;
	make_spr (f, [&] (Img &c) { critter (c, SPX, SPY, l, k); });
	g_frames[key] = f;
	return f;
}
static Spr<ICON, ICON> *g_icon[NBUILT][2];
static const Spr<ICON, ICON> *icon_of (int role, bool off)
{
	Spr<ICON, ICON> *&s = g_icon[role][off ? 1 : 0];
	if (!s) { s = new Spr<ICON, ICON>; make_spr (s, [&] (Img &c) { role_icon (c, role, ICON / 2, ICON / 2, 36, off); }); }
	return s;
}

// ---- the terrain, relit ---------------------------------------------------------------------------------------------------
static uint32_t *g_lit, *g_sky; static int g_skyW, g_skyH;
// the level's sky, wider than the view (it moves at half the speed): a gradient, clouds, stars, two rows of hills
static void make_sky (const critters::Level &lv, int viewW)
{
	free (g_sky);
	g_skyW = lv.w / 2 + viewW + 4; g_skyH = lv.h;
	g_sky = (uint32_t *) malloc ((size_t) g_skyW * (size_t) g_skyH * 4);
	if (!g_sky) return;
	unsigned bg = lv.bg, earth = lv.nshape ? lv.shape[0].colour : 0x8A5A34;
	unsigned top = shade (bg, 150), bot = mix (shade (bg, 300), earth, 40), cloud = mix (shade (bg, 420), 0xFFFFFF, 40);
	unsigned far = mix (shade (bg, 200), earth, 50), nearc = mix (shade (bg, 130), earth, 90);
	int bright = (int) ((bg >> 16) & 255) + (int) ((bg >> 8) & 255) + (int) (bg & 255);
	for (int x = 0; x < g_skyW; x++)
	{
		float fx = (float) x;
		int h1 = g_skyH - (int) (fbm (fx * 0.012f, 3.1f, 5) * (float) g_skyH * 0.75f);
		int h2 = g_skyH - (int) (fbm (fx * 0.021f, 7.7f, 9) * (float) g_skyH * 0.5f);
		for (int y = 0; y < g_skyH; y++)
		{
			unsigned c = mix (top, bot, y * 256 / g_skyH);
			float n = fbm (fx * 0.02f, (float) y * 0.05f, 11);
			if (n > 0.5f) c = mix (c, cloud, (int) ((n - 0.5f) * 220));
			if (bright < 240 && y < g_skyH * 2 / 3 && hash2 (x, y, 2) > 0.9975f) c = mix (c, 0xFFFFFF, 150);
			if (y >= h1) c = mix (far, shade (far, 170), (y - h1) * 256 / (g_skyH - h1 + 1));
			if (y >= h2) c = mix (nearc, shade (nearc, 170), (y - h2) * 256 / (g_skyH - h2 + 1));
			g_sky[(long) y * g_skyW + x] = c;
		}
	}
}
static void relight (const critters::Terrain &t, int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= t.w) x1 = t.w - 1;
	y1 += 16; if (y1 >= t.h) y1 = t.h - 1;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			int m = t.m[y * t.w + x];
			unsigned c = t.col[y * t.w + x];
			if (m != critters::M_EMPTY)
			{
				int k = 256;
				if (m == critters::M_EARTH || m == critters::M_STEEL)
				{
					int d = 0;
					while (d < 14 && y - d - 1 >= 0 && t.m[(y - d - 1) * t.w + x] != critters::M_EMPTY) d++;
					k = d == 0 ? 370 : d == 1 ? 310 : d == 14 ? 180 : 280 - d * 7;
					if (x > 0 && t.m[y * t.w + x - 1] == critters::M_EMPTY) k += 40;
					if (x + 1 < t.w && t.m[y * t.w + x + 1] == critters::M_EMPTY) k -= 40;
					if (y + 1 < t.h && t.m[(y + 1) * t.w + x] == critters::M_EMPTY) k -= 70;
					k += (int) (hash2 (x, y, 4) * 30) - 15;
				}
				c = shade (c, k);
			}
			g_lit[y * t.w + x] = c;
		}
}
// the visible part, x2 from the view's left edge vx
static void blit_terrain (Img &cv, const critters::Terrain &t, int vx, int ox, int playH, unsigned tick)
{
	cv.rect (0, 0, cv.w, playH, shade (t.bg, 90));
	int cols = (cv.w - ox) / 2; if (cols > t.w - vx) cols = t.w - vx;
	if (cols <= 0 || !g_lit || !g_sky) return;
	static const signed char WAVE[32] = { 0, 3, 6, 9, 11, 12, 11, 9, 6, 3, 0, -3, -6, -9, -11, -12, -11, -9, -6, -3, 0, 2, 4, 2, 0, -2, -4, -2, 0, 1, -1, 0 };
	int skyOff = vx / 2;
	for (int ly = 0; ly < t.h && ly * 2 + 1 < playH; ly++)
	{
		uint32_t *d0 = cv.px + (long) (ly * 2) * cv.stride + ox;
		const uint32_t *s = g_lit + (long) ly * t.w + vx, *sk = g_sky + (long) ly * g_skyW + skyOff;
		const uint8_t *m = t.m + (long) ly * t.w + vx;
		for (int i = 0; i < cols; i++)
		{
			uint32_t c;
			int k = m[i];
			if (k == critters::M_EMPTY) c = sk[i < g_skyW - skyOff ? i : 0];
			else if (k >= critters::M_WATER)			// l'eau et la lave ondulent
			{
				int w = WAVE[(unsigned) (vx + i + ly * 3 + (int) (tick / 3)) & 31];
				bool surf = ly > 0 && t.m[(ly - 1) * t.w + vx + i] == critters::M_EMPTY;
				c = shade (s[i], 256 + w * 7 + (surf ? 110 : 0));
			}
			else c = s[i];
			d0[2 * i] = d0[2 * i + 1] = c;
		}
		memcpy (cv.px + (long) (ly * 2 + 1) * cv.stride + ox, d0, (size_t) cols * 8);
	}
}
// the level's words, painted into the terrain's colours (earth and steel only)
static void paint_labels (const critters::Level &lv, critters::Terrain &t)
{
	static Layer c;
	if (!lv.nlabel || !c.alloc (512, 16)) return;
	for (int k = 0; k < lv.nlabel; k++)
	{
		const critters::Label &l = lv.label[k];
		const char *s = l.text.get (g_lang);
		if (!*s) continue;
		int tw = textW (s, 9) + 2, th = 13; if (tw > 512) tw = 512;
		c.begin (); ui_fill (0, 0, 512, 16, 0); text (1, 10, s, 0xFFFFFF, 9); c.end ();
		for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++)
		{
			int a = (int) (c.img.px[y * c.img.stride + x] & 255), X = l.at.x - tw / 2 + x, Y = l.at.y - th / 2 + y;
			if (a < 90 || X < 0 || X >= t.w || Y < 0 || Y >= t.h || !t.solid (X, Y)) continue;
			t.col[Y * t.w + X] = mix (t.col[Y * t.w + X], l.colour, 150);
		}
	}
}

#endif
