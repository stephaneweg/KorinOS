//
// vpath.cpp -- see VPath in palette.h: outlines joined (geometry), filled by ui_vp_poly of UIKit.
//
#include "ui/palette.h"

namespace kui {

static const short SIN90[91] = {
	0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406, 3686, 3964, 4240, 4516, 4790,
	5063, 5334, 5604, 5872, 6138, 6402, 6664, 6924, 7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162,
	9397, 9630, 9860, 10087, 10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365,
	12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044, 14189, 14330, 14466, 14598,
	14726, 14849, 14968, 15082, 15191, 15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964,
	16026, 16083, 16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374, 16382, 16384 };

int vsin (int d)
{
	d %= 360; if (d < 0) d += 360;
	if (d <= 90) return SIN90[d];
	if (d <= 180) return SIN90[180 - d];
	if (d <= 270) return -SIN90[d - 180];
	return -SIN90[360 - d];
}

VPath::VPath () : m_x (0), m_y (0), m_n (0), m_cap (0), m_cs (0), m_nc (0), m_ccap (0) {}
VPath::~VPath () { delete[] m_x; delete[] m_y; delete[] m_cs; }
void VPath::clear () { m_n = 0; m_nc = 0; }

void VPath::add (int x, int y)
{
	if (m_n == m_cap)
	{
		int c = m_cap ? m_cap * 2 : 64;
		int *nx = new int[c], *ny = new int[c];
		for (int i = 0; i < m_n; i++) { nx[i] = m_x[i]; ny[i] = m_y[i]; }
		delete[] m_x; delete[] m_y; m_x = nx; m_y = ny; m_cap = c;
	}
	m_x[m_n] = x; m_y[m_n] = y; m_n++;
}
void VPath::begin ()
{
	if (m_nc == m_ccap)
	{
		int c = m_ccap ? m_ccap * 2 : 16;
		int *n = new int[c];
		for (int i = 0; i < m_nc; i++) n[i] = m_cs[i];
		delete[] m_cs; m_cs = n; m_ccap = c;
	}
	m_cs[m_nc++] = m_n;
}
// Close the outline begun last: turned so that its signed area is positive (a hole: negative).
void VPath::end (bool asHole)
{
	int s = m_cs[m_nc - 1], n = m_n - s;
	if (n < 3) { m_n = s; m_nc--; return; }
	long long a = 0;
	for (int i = 0; i < n; i++)
	{
		int j = (i + 1) % n;
		a += (long long) m_x[s + i] * m_y[s + j] - (long long) m_x[s + j] * m_y[s + i];
	}
	if ((a < 0) != asHole)
		for (int i = 0, j = n - 1; i < j; i++, j--)
		{
			int t = m_x[s + i]; m_x[s + i] = m_x[s + j]; m_x[s + j] = t;
			t = m_y[s + i]; m_y[s + i] = m_y[s + j]; m_y[s + j] = t;
		}
}

void VPath::poly (const int *xy, int n) { begin (); for (int i = 0; i < n; i++) add (xy[2 * i], xy[2 * i + 1]); end (); }
void VPath::rect (int x, int y, int w, int h)
{
	begin (); add (x, y); add (x + w, y); add (x + w, y + h); add (x, y + h); end ();
}
static int segs (int r) { int n = r / 8; return n < 12 ? 12 : n > 72 ? 72 : n; }
void VPath::rrect (int x, int y, int w, int h, int r)
{
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	if (r <= 0) { rect (x, y, w, h); return; }
	int k = segs (r) / 4 + 2;
	begin ();
	int cx[4] = { x + w - r, x + r, x + r, x + w - r }, cy[4] = { y + r, y + r, y + h - r, y + h - r };
	for (int q = 0; q < 4; q++)			// (the corners: top right, top left, bottom left, bottom right)
		for (int i = 0; i <= k; i++)
		{
			int d = q * 90 + 90 * i / k;
			add (cx[q] + r * vcos (d) / 16384, cy[q] - r * vsin (d) / 16384);
		}
	end ();
}
void VPath::ellipse (int cx, int cy, int rx, int ry)
{
	int n = segs (rx > ry ? rx : ry);
	begin ();
	for (int i = 0; i < n; i++) { int d = 360 * i / n; add (cx + rx * vcos (d) / 16384, cy - ry * vsin (d) / 16384); }
	end ();
}
void VPath::circle (int cx, int cy, int r) { ellipse (cx, cy, r, r); }
void VPath::hole (int cx, int cy, int r)
{
	int n = segs (r);
	begin ();
	for (int i = 0; i < n; i++) { int d = 360 * i / n; add (cx + r * vcos (d) / 16384, cy - r * vsin (d) / 16384); }
	end (true);
}

// (an integer square root)
static int isqrt (long long v) { if (v <= 0) return 0; long long r = 1; while (r * r <= v) r <<= 1; long long lo = r >> 1, hi = r; while (lo < hi) { long long m = (lo + hi + 1) >> 1; if (m * m <= v) lo = m; else hi = m - 1; } return (int) lo; }

void VPath::line (int x0, int y0, int x1, int y1, int w)
{
	int dx = x1 - x0, dy = y1 - y0, len = isqrt ((long long) dx * dx + (long long) dy * dy);
	int h = w / 2;
	if (len > 0)
	{
		int nx = -dy * h / len, ny = dx * h / len;	// (the normal, half the width long)
		begin (); add (x0 + nx, y0 + ny); add (x1 + nx, y1 + ny); add (x1 - nx, y1 - ny); add (x0 - nx, y0 - ny); end ();
	}
	if (h >= 4) { circle (x0, y0, h); circle (x1, y1, h); }	// (round ends; a hair's: square)
}
void VPath::polyline (const int *xy, int n, int w, bool closed)
{
	for (int i = 0; i + 1 < n; i++) line (xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], w);
	if (closed && n > 2) line (xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], w);
}
void VPath::arc (int cx, int cy, int r, int a0, int a1, int w)
{
	int span = a1 - a0; if (span < 0) span = -span;
	int n = span * segs (r) / 360 + 2;
	int px = 0, py = 0;
	for (int i = 0; i <= n; i++)
	{
		int d = a0 + (a1 - a0) * i / n;
		int x = cx + r * vcos (d) / 16384, y = cy - r * vsin (d) / 16384;
		if (i) line (px, py, x, y, w);
		px = x; py = y;
	}
}
void VPath::arrowHead (int x, int y, int deg, int len, int half)
{
	int bx = x - len * vcos (deg) / 16384, by = y + len * vsin (deg) / 16384;	// (the base's middle)
	int nx = half * vcos (deg + 90) / 16384, ny = -half * vsin (deg + 90) / 16384;
	int pts[6] = { x, y, bx + nx, by + ny, bx - nx, by - ny };
	poly (pts, 3);
}

// ---- filling --------------------------------------------------------------------------------------------
// (ARMPI) chaque contour : un polygone de UIKit, coordonnees en 1/16 de pixel comme ici
static void vp_fill (const int *x, const int *y, int n, unsigned c, int dx, int dy)
{
	static int pts[512];
	if (n > 256) n = 256;
	for (int i = 0; i < n; i++) { pts[2 * i] = x[i] + V (dx); pts[2 * i + 1] = y[i] + V (dy); }
	ui_vp_poly ((long) pts, n, c);
}
void VPath::fill (unsigned c, int alpha, int dx, int dy)
{
	if (alpha < 255) c = mixc (C_BG, c, alpha + 1);
	for (int k = 0; k < m_nc; k++)
	{
		int s = m_cs[k], e = k + 1 < m_nc ? m_cs[k + 1] : m_n;
		vp_fill (m_x + s, m_y + s, e - s, c, dx, dy);
	}
}

} // namespace kui
