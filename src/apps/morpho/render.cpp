//
// render.cpp -- Morpho : l'éclairage par sommet, la lumière du studio, le fond en 2D.
//
#include "render.h"
#include <gpukit_abi.h>

namespace mo {

// ==== la lumière du studio ============================================================================================
// L'heure ne change que des teintes : le fond plus bleu la nuit, plus chaud à l'aube et au crépuscule ; la lampe
// éteinte assombrit tout, une lumière de lune reste sur l'animal.
Light studioLight (float hour, bool lampOff)
{
	Light L;
	hour = fmodf (hour, 24); if (hour < 0) hour += 24;
	L.hour = hour;
	float day = sinf ((hour - 6) / 12 * PI);			// > 0 : le soleil est levé
	float night = 1 - smooth (-0.12f, 0.25f, day);
	float warm = (1 - smooth (0.0f, 0.45f, fabsf (day))) * (1 - night * 0.6f);	// (l'aube, le crépuscule)
	L.night = night;
	Col keyDay (1.0f, 0.96f, 0.9f), keyWarm (1.0f, 0.8f, 0.62f), keyNight (0.62f, 0.7f, 0.95f);
	L.sunC = mixc (mixc (keyDay, keyWarm, warm), keyNight, night) * (1 - 0.18f * night);
	L.skyC = mixc (Col (0.44f, 0.5f, 0.64f), Col (0.3f, 0.36f, 0.58f), night);
	L.groundC = Col (0.1f, 0.1f, 0.13f);
	L.amb = 0.62f;
	L.rimC = mixc (Col (0.55f, 0.72f, 1.0f), Col (1.0f, 0.72f, 0.55f), warm * 0.7f);
	L.rimK = 0.5f;
	Col topDay (0.090f, 0.102f, 0.137f), botDay (0.055f, 0.063f, 0.082f);
	Col topNight (0.055f, 0.067f, 0.118f), botNight (0.031f, 0.035f, 0.055f);
	Col topWarm (0.135f, 0.098f, 0.118f);
	L.bgTop = mixc (mixc (topDay, topWarm, warm), topNight, night);
	L.bgBot = mixc (botDay, botNight, night);
	L.glow = mixc (mixc (Col (0.18f, 0.45f, 0.86f), Col (0.95f, 0.55f, 0.35f), warm), Col (0.3f, 0.35f, 0.85f), night);
	L.glowK = 1;
	if (lampOff)
	{
		L.sunC = Col (0.36f, 0.43f, 0.68f); L.skyC = Col (0.14f, 0.17f, 0.3f); L.groundC = Col (0.04f, 0.04f, 0.06f);
		L.amb = 0.55f; L.rimC = Col (0.45f, 0.55f, 0.95f); L.rimK = 0.4f;
		L.bgTop = Col (0.035f, 0.04f, 0.07f); L.bgBot = Col (0.016f, 0.018f, 0.03f);
		L.glow = Col (0.25f, 0.3f, 0.75f); L.glowK = 0.5f; L.night = 1;
	}
	L.fog = L.bgBot;
	return L;
}

// la lumière principale suit la caméra : en haut à gauche, un peu devant (une lumière de studio)
void keyFrom (Light &L, const Camera &C)
{
	V3 f = norm (C.at - C.eye), s = norm (cross (f, V3 (0, 1, 0))), u = cross (s, f);
	L.sun = norm (s * -0.55f + u * 0.75f - f * 0.45f);
}

// ==== l'éclairage =====================================================================================================
static unsigned char b8 (float v) { return (unsigned char) (v <= 0 ? 0 : v >= 1 ? 255 : (int) (v * 255 + 0.5f)); }

void DrawList::add (const Mesh &m, const Light &L, const Camera &C)
{
	if (m.i.empty ()) return;
	GkB bt;
	memset (&bt, 0, sizeof bt);
	bt.first = (unsigned) v.size ();
	bt.count = (unsigned) m.i.size ();
	bt.tex = m.tex;
	unsigned f = GK_Z_LEQUAL;
	if (m.flags & MF_CULL) f |= GK_B_CULL_BACK;
	if (m.flags & (MF_ALPHA | MF_NOZW)) f |= GK_B_NOZWRITE;
	if (m.flags & MF_ALPHA) f |= GK_BLEND_ALPHA << GK_B_BLEND_SHIFT;
	if (m.flags & MF_ADD) f |= GK_BLEND_ADD << GK_B_BLEND_SHIFT;
	if (m.tex >= 0) f |= GK_B_LINEAR | (m.tex == TEX_SKIN ? (GK_WRAP_CLAMP << GK_B_WRAP_S_SHIFT) : 0);
	bt.flags = f;
	memcpy (bt.m, C.vp.m, sizeof bt.m);
	b.push_back (bt);
	// chaque sommet une fois, puis les triangles
	static std::vector<GkV> sh;
	sh.resize (m.v.size ());
	bool unlit = (m.flags & MF_UNLIT) != 0, nofog = (m.flags & MF_NOFOG) != 0;
	for (size_t k = 0; k < m.v.size (); k++)
	{
		const Vtx &x = m.v[k];
		GkV &o = sh[k];
		o.x = x.p.x; o.y = x.p.y; o.z = x.p.z; o.w = 1; o.s = x.u; o.t = x.v;
		V3 e = C.eye - x.p;
		float dist = len (e);
		V3 V = e * (1.0f / fmaxf (dist, 1e-4f));
		Col col, add (0, 0, 0);
		if (unlit) col = x.c;
		else
		{
			V3 n = x.n;
			if (x.two && dot (n, V) < 0) n = -n;
			float ndl = dot (n, L.sun);
			const float wrap = 0.4f;
			float diff = fmaxf (0, (ndl + wrap) / (1 + wrap));
			Col hemi = mixc (L.groundC, L.skyC, n.y * 0.5f + 0.5f);
			Col lit = L.sunC * (diff * 0.82f) + hemi * L.amb;
			col = x.c * lit;
			if (x.emit > 0) col = mixc (col, x.c, x.emit);
			float nv = fmaxf (0, dot (n, V));
			float rim = (1 - nv); rim = rim * rim * rim;
			if (x.gloss > 0 && ndl > 0)
			{
				V3 H = norm (L.sun + V);
				float sp = powf (fmaxf (0, dot (n, H)), 14 + x.gloss * 70) * x.gloss * 0.85f;
				add = add + L.sunC * sp;
			}
			add = add + L.rimC * (rim * L.rimK);
		}
		float fg = nofog ? 0 : smooth (L.fogNear, L.fogFar, dist) * 0.92f;
		col = col * (1 - fg);
		add = add * (1 - fg) + L.fog * fg;
		o.rgba[0] = b8 (col.r); o.rgba[1] = b8 (col.g); o.rgba[2] = b8 (col.b); o.rgba[3] = b8 (x.alpha);
		o.rgba2[0] = b8 (add.r); o.rgba2[1] = b8 (add.g); o.rgba2[2] = b8 (add.b); o.rgba2[3] = 0;
	}
	size_t base = v.size ();
	v.resize (base + m.i.size ());
	for (size_t k = 0; k < m.i.size (); k++) v[base + k] = sh[(size_t) m.i[k]];
}

// ==== les textures ====================================================================================================
void makeShadowTex (uint32_t *px, int n)
{
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
		{
			float dx = ((float) x + 0.5f) / (float) n * 2 - 1, dy = ((float) y + 0.5f) / (float) n * 2 - 1;
			float d = sqrtf (dx * dx + dy * dy);
			float a = 1 - smooth (0.15f, 1.0f, d);
			a = a * a * (3 - 2 * a);
			px[y * n + x] = (uint32_t) b8 (a * 0.62f) << 24;
		}
}
// ==== le fond du studio ===============================================================================================
static inline uint32_t mixp (uint32_t d, const Col &c, float a)
{
	float r = (float) ((d >> 16) & 255) / 255, g = (float) ((d >> 8) & 255) / 255, b = (float) (d & 255) / 255;
	return packc (Col (r + (c.r - r) * a, g + (c.g - g) * a, b + (c.b - b) * a));
}

void drawStudio (uint32_t *px, int w, int h, int stride, const Camera &C, const Light &L, const V3 &at, const V3 &ringC, float ringR, float t)
{
	(void) t;
	// le dégradé, ligne par ligne, avec un tramage léger (pas de bandes sur un fond si sombre)
	for (int y = 0; y < h; y++)
	{
		float f = (float) y / (float) (h > 1 ? h - 1 : 1);
		Col c = mixc (L.bgTop, L.bgBot, smooth (0, 1, f));
		uint32_t *row = px + (long) y * stride;
		for (int x = 0; x < w; x++)
		{
			float d = (hashf (x, y, 3) - 0.5f) * (1.0f / 255);
			row[x] = packc (Col (c.r + d, c.g + d, c.b + d));
		}
	}
	// le halo derrière l'animal : une lueur douce, quelques pour cent
	float sx, sy;
	if (C.project (at, sx, sy))
	{
		float R = (float) h * 0.62f;
		int x0 = (int) (sx - R), x1 = (int) (sx + R), y0 = (int) (sy - R), y1 = (int) (sy + R);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > w) x1 = w;
		if (y1 > h) y1 = h;
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++)
			{
				float dx = ((float) x - sx) / R, dy = ((float) y - sy) / (R * 0.9f);
				float d2 = dx * dx + dy * dy;
				if (d2 >= 1) continue;
				float k = (1 - d2); k = k * k * 0.085f * L.glowK;
				uint32_t &p = px[(long) y * stride + x];
				p = mixp (p, L.glow, k);
			}
	}
	// le socle : un disque à peine plus clair et un anneau fin, au sol
	{
		const int N = 96;
		float pts[(N + 1) * 2]; bool ok = true;
		for (int i = 0; i <= N && ok; i++)
		{
			float a = (float) i / N * 2 * PI;
			ok = C.project (ringC + V3 (cosf (a) * ringR, 0, sinf (a) * ringR), pts[i * 2], pts[i * 2 + 1]);
		}
		if (ok)
		{
			// le disque : par lignes, entre les bords de l'ellipse projetée
			float ymin = 1e9f, ymax = -1e9f;
			for (int i = 0; i < N; i++) { ymin = fminf (ymin, pts[i * 2 + 1]); ymax = fmaxf (ymax, pts[i * 2 + 1]); }
			for (int y = (int) fmaxf (0, ymin); y < (int) fminf ((float) h, ymax + 1); y++)
			{
				float yy = (float) y + 0.5f, xa = 1e9f, xb = -1e9f;
				for (int i = 0; i < N; i++)
				{
					float ay = pts[i * 2 + 1], by = pts[i * 2 + 3];
					if ((ay <= yy && by > yy) || (by <= yy && ay > yy))
					{
						float xx = pts[i * 2] + (yy - ay) / (by - ay) * (pts[i * 2 + 2] - pts[i * 2]);
						xa = fminf (xa, xx); xb = fmaxf (xb, xx);
					}
				}
				if (xa > xb) continue;
				float cy = (ymin + ymax) * 0.5f, ry = fmaxf ((ymax - ymin) * 0.5f, 1);
				float v = 1 - fabsf (yy - cy) / ry;
				for (int x = (int) fmaxf (0, xa); x < (int) fminf ((float) w, xb + 1); x++)
				{
					float hx = 1 - fabsf (((float) x + 0.5f) - (xa + xb) * 0.5f) / fmaxf ((xb - xa) * 0.5f, 1);
					float k = sqrtf (fmaxf (0, v * hx)) * 0.05f * (1 - L.night * 0.4f);
					uint32_t &p = px[(long) y * stride + x];
					p = mixp (p, Col (0.55f, 0.62f, 0.8f), k);
				}
			}
			// l'anneau : des segments anticrénelés (couverture par distance)
			for (int i = 0; i < N; i++)
			{
				float x0 = pts[i * 2], y0 = pts[i * 2 + 1], x1 = pts[i * 2 + 2], y1 = pts[i * 2 + 3];
				int bx0 = (int) floorf (fminf (x0, x1) - 2), bx1 = (int) ceilf (fmaxf (x0, x1) + 2);
				int by0 = (int) floorf (fminf (y0, y1) - 2), by1 = (int) ceilf (fmaxf (y0, y1) + 2);
				float dx = x1 - x0, dy = y1 - y0, l2 = fmaxf (dx * dx + dy * dy, 1e-4f);
				// (l'arrière de l'anneau, plus loin, plus pâle)
				float back = 0.55f + 0.45f * smooth (C.h * 0.0f, 1, (y0 - ymin) / fmaxf (ymax - ymin, 1));
				for (int y = by0; y <= by1; y++)
					for (int x = bx0; x <= bx1; x++)
					{
						if (x < 0 || y < 0 || x >= w || y >= h) continue;
						float px_ = (float) x + 0.5f, py_ = (float) y + 0.5f;
						float u = clampf (((px_ - x0) * dx + (py_ - y0) * dy) / l2, 0, 1);
						float ex = x0 + dx * u - px_, ey = y0 + dy * u - py_;
						float d = sqrtf (ex * ex + ey * ey);
						float cov = clampf (1.1f - d, 0, 1);
						if (cov <= 0) continue;
						uint32_t &p = px[(long) y * stride + x];
						p = mixp (p, Col (0.75f, 0.8f, 0.92f), cov * 0.16f * back);
					}
			}
		}
	}
}

}	// namespace mo
