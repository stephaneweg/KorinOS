//
// render.cpp -- Morpho : l'éclairage par sommet, la lumière de l'heure, le ciel en 2D, la prairie et son décor.
//
#include "render.h"
#include <gpukit_abi.h>

namespace mo {

// ==== la lumière du moment ============================================================================================
struct Sky { float h; Col top, hor, sun, sky, ground; float amb; };
static const Sky SKIES[] = {
	{ 0.0f, Col (0.025f, 0.04f, 0.11f), Col (0.09f, 0.12f, 0.24f), Col (0.32f, 0.4f, 0.62f), Col (0.17f, 0.21f, 0.38f), Col (0.07f, 0.08f, 0.12f), 0.62f },
	{ 5.0f, Col (0.025f, 0.04f, 0.11f), Col (0.09f, 0.12f, 0.24f), Col (0.32f, 0.4f, 0.62f), Col (0.17f, 0.21f, 0.38f), Col (0.07f, 0.08f, 0.12f), 0.62f },
	{ 6.6f, Col (0.27f, 0.36f, 0.62f), Col (0.98f, 0.7f, 0.52f), Col (1.0f, 0.74f, 0.52f), Col (0.56f, 0.56f, 0.72f), Col (0.36f, 0.29f, 0.26f), 0.56f },
	{ 8.6f, Col (0.27f, 0.54f, 0.92f), Col (0.74f, 0.87f, 0.98f), Col (1.0f, 0.96f, 0.88f), Col (0.58f, 0.7f, 0.9f), Col (0.42f, 0.44f, 0.32f), 0.52f },
	{ 17.0f, Col (0.27f, 0.52f, 0.9f), Col (0.76f, 0.86f, 0.96f), Col (1.0f, 0.94f, 0.84f), Col (0.58f, 0.68f, 0.88f), Col (0.42f, 0.42f, 0.3f), 0.52f },
	{ 19.0f, Col (0.24f, 0.22f, 0.5f), Col (1.0f, 0.56f, 0.4f), Col (1.0f, 0.62f, 0.42f), Col (0.52f, 0.44f, 0.62f), Col (0.32f, 0.24f, 0.22f), 0.56f },
	{ 20.6f, Col (0.025f, 0.04f, 0.11f), Col (0.09f, 0.12f, 0.24f), Col (0.32f, 0.4f, 0.62f), Col (0.17f, 0.21f, 0.38f), Col (0.07f, 0.08f, 0.12f), 0.62f },
	{ 24.0f, Col (0.025f, 0.04f, 0.11f), Col (0.09f, 0.12f, 0.24f), Col (0.32f, 0.4f, 0.62f), Col (0.17f, 0.21f, 0.38f), Col (0.07f, 0.08f, 0.12f), 0.62f },
};

Light lightAt (float hour, bool lampOff, float overcast)
{
	Light L;
	hour = fmodf (hour, 24); if (hour < 0) hour += 24;
	L.hour = hour;
	int i = 0;
	while (i < 6 && SKIES[i + 1].h <= hour) i++;
	const Sky &a = SKIES[i], &b = SKIES[i + 1];
	float t = smooth (0, 1, (hour - a.h) / fmaxf (b.h - a.h, 0.01f));
	L.skyTop = mixc (a.top, b.top, t); L.skyHor = mixc (a.hor, b.hor, t);
	L.sunC = mixc (a.sun, b.sun, t); L.skyC = mixc (a.sky, b.sky, t); L.groundC = mixc (a.ground, b.ground, t);
	L.amb = lerpf (a.amb, b.amb, t);
	float day = sinf ((hour - 6) / 12 * PI);			// > 0 : le soleil est levé
	L.night = 1 - smooth (-0.15f, 0.2f, day);
	L.sunUp = smooth (-0.05f, 0.1f, day);
	float el = day > 0 ? 0.18f + day * 0.95f : 0.75f;		// (la nuit : la lune)
	float az = 0.55f + (hour - 12) * 0.12f;
	L.sun = norm (V3 (cosf (el) * sinf (az), sinf (el), cosf (el) * cosf (az)));
	L.sunDisc = day > 0 ? mixc (Col (1, 0.85f, 0.6f), Col (1, 0.98f, 0.92f), smooth (0, 0.5f, day)) : Col (0.92f, 0.94f, 1);
	if (overcast > 0)
	{
		Col grey = Col (0.6f, 0.64f, 0.7f) * (1 - L.night * 0.8f);
		L.skyTop = mixc (L.skyTop, grey, overcast * 0.7f); L.skyHor = mixc (L.skyHor, grey, overcast * 0.6f);
		L.sunC = L.sunC * (1 - overcast * 0.45f); L.amb += overcast * 0.12f;
	}
	if (lampOff)
	{
		L.skyTop = mixc (L.skyTop, Col (0.02f, 0.03f, 0.08f), 0.85f); L.skyHor = mixc (L.skyHor, Col (0.06f, 0.08f, 0.17f), 0.85f);
		L.sunC = Col (0.3f, 0.36f, 0.58f); L.skyC = Col (0.16f, 0.19f, 0.34f); L.groundC = Col (0.06f, 0.06f, 0.09f);
		L.amb = 0.6f; L.night = fmaxf (L.night, 0.85f);
		L.sun = norm (V3 (0.35f, 0.75f, 0.55f)); L.sunDisc = Col (0.92f, 0.94f, 1); L.sunUp = 0;
	}
	L.rimC = mixc (L.skyHor, Col (1, 1, 1), 0.35f) * (0.55f + 0.45f * (1 - L.night));
	L.skyLow = L.skyHor * 0.92f;
	L.fog = L.skyHor;
	return L;
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
			add = add + L.rimC * (rim * 0.32f * (0.4f + 0.6f * diff));
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
void makeGrassTex (uint32_t *px, int n)
{
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
		{
			float u = (float) x, v = (float) y;
			float k = 0.82f + 0.12f * vnoise (u * 0.09f, v * 0.09f, 1, n * 9 / 100) + 0.1f * vnoise (u * 0.35f, v * 0.35f, 2, n * 35 / 100);
			float bl = hashf (x, y / 3, 4);						// (des brins)
			k += (bl - 0.5f) * 0.12f;
			Col c = Col (k, k * 1.02f, k * 0.9f);
			px[y * n + x] = 0xFF000000u | packc (c);
		}
}

// ==== le monde ========================================================================================================
static float groundY (float x, float z)
{
	float r = sqrtf (x * x + z * z);
	if (r < 4) return 0;
	float th = atan2f (z, x);
	float hill = 0.25f + 1.6f * vnoise ((th + PI) / (2 * PI) * 7, r * 0.12f, 77, 7);
	return smooth (4, 15, r) * hill * smooth (4, 9, r) - smooth (12, 17, r) * 0.5f;
}

void buildWorld (Mesh &ground, Mesh &decor, Mesh &blades, uint32_t seed)
{
	Rng r (seed);
	ground.clear (); decor.clear (); blades.clear ();
	ground.tex = TEX_GRASS;
	// la prairie : un disque en couronnes, des collines au loin
	{
		Builder B (&ground);
		B.gloss = 0;
		const int RN = 26, SN = 72;
		int base = (int) ground.v.size ();
		for (int i = 0; i <= RN; i++)
		{
			float rr = 18 * powf ((float) i / RN, 1.7f);
			for (int j = 0; j <= SN; j++)
			{
				float th = 2 * PI * (float) j / SN;
				float x = cosf (th) * rr, z = -sinf (th) * rr;
				float y = groundY (x, z);
				const float e = 0.05f;
				V3 n = norm (V3 (groundY (x - e, z) - groundY (x + e, z), 2 * e, groundY (x, z - e) - groundY (x, z + e)));
				float nn = vnoise (x * 0.35f, z * 0.35f, 5) * 0.6f + vnoise (x * 1.3f, z * 1.3f, 6) * 0.4f;
				Col c = mixc (Col (0.36f, 0.62f, 0.25f), Col (0.5f, 0.72f, 0.28f), nn);
				c = mixc (c, Col (0.3f, 0.52f, 0.24f), smooth (5, 12, rr) * 0.6f);
				c = mixc (Col (0.56f, 0.74f, 0.33f), c, smooth (0.6f, 2.6f, rr));	// (la clairière, plus claire)
				B.add (V3 (x, y, z), n, c, x * 0.55f, z * 0.55f);
			}
		}
		// (de l'intérieur vers l'extérieur : faces vers le haut)
		for (int i = 0; i < RN; i++)
			for (int j = 0; j < SN; j++)
			{
				int a = base + i * (SN + 1) + j, b = a + 1, c = a + SN + 1, d = c + 1;
				B.tri (a, d, b); B.tri (a, c, d);
			}
	}
	Builder D (&decor);
	auto place = [&] (float rmin, float rmax, float &x, float &z) {
		for (int k = 0; k < 20; k++)
		{
			float a = r.f () * 2 * PI, d = sqrtf (r.range (rmin * rmin, rmax * rmax));
			x = cosf (a) * d; z = sinf (a) * d;
			if (!(z > 1.2f && fabsf (x) < 2.2f)) return;		// (pas devant la caméra)
		}
	};
	// des fleurs
	static const unsigned PET[6] = { 0xFFFFFF, 0xFFD84A, 0xFF8FB8, 0xB78CFF, 0xFF5A5A, 0x7FC8FF };
	for (int k = 0; k < 30; k++)
	{
		float x, z; place (1.9f, 6.0f, x, z);
		float y = groundY (x, z), h = r.range (0.1f, 0.22f);
		V3 b0 (x, y, z), top (x + r.range (-0.02f, 0.02f), y + h, z + r.range (-0.02f, 0.02f));
		V3 pts[2] = { b0, top };
		float rad[2] = { 0.008f, 0.006f };
		Col cols[2] = { Col (0.25f, 0.5f, 0.2f), Col (0.35f, 0.62f, 0.25f) };
		D.gloss = 0.1f; D.tube (pts, rad, cols, 2, 5, 1, 0, V3 (1, 0, 0), 2);
		Col pc = hexc (PET[r.irange (0, 5)]);
		float ps = r.range (0.022f, 0.034f);
		for (int p = 0; p < 5; p++)
		{
			float a = (float) p / 5 * 2 * PI + r.f ();
			Frame f; f.o = top + V3 (cosf (a), 0.1f, sinf (a)) * ps; f.z = norm (V3 (cosf (a), 0.25f, sinf (a)));
			f.y = norm (V3 (0, 1, 0) - f.z * dot (V3 (0, 1, 0), f.z)); f.x = cross (f.y, f.z);
			D.gloss = 0.2f; D.ellipsoid (f, V3 (ps * 0.6f, ps * 0.15f, ps), pc, pc * 0.85f, 6, 4);
		}
		D.sphere (top + V3 (0, 0.008f, 0), ps * 0.45f, Col (1, 0.8f, 0.25f), 6, 4);
	}
	// des pierres
	for (int k = 0; k < 12; k++)
	{
		float x, z; place (2.2f, 7.5f, x, z);
		float s = r.range (0.06f, 0.2f);
		Frame f; f.o = V3 (x, groundY (x, z) + s * 0.2f, z); f.x = rotY (f.x, r.f () * 3); f.z = rotY (f.z, 0); f.z = cross (f.x, f.y);
		float g = r.range (0.5f, 0.68f);
		D.gloss = 0.25f; D.ellipsoid (f, V3 (s, s * r.range (0.45f, 0.7f), s * r.range (0.7f, 1.0f)), Col (g, g, g * 1.02f), Col (g * 0.7f, g * 0.7f, g * 0.72f), 10, 6);
	}
	// des champignons
	for (int k = 0; k < 6; k++)
	{
		float x, z; place (2.4f, 5.5f, x, z);
		float y = groundY (x, z), h = r.range (0.07f, 0.13f), cr = h * r.range (0.7f, 1.0f);
		V3 pts[2] = { V3 (x, y, z), V3 (x, y + h, z) };
		float rad[2] = { cr * 0.32f, cr * 0.26f };
		Col cols[2] = { Col (0.92f, 0.9f, 0.84f), Col (0.96f, 0.95f, 0.9f) };
		D.gloss = 0.1f; D.tube (pts, rad, cols, 2, 8, 1, 0, V3 (1, 0, 0), 2);
		Frame f; f.o = V3 (x, y + h, z);
		Col cc = r.chance (0.6f) ? Col (0.85f, 0.18f, 0.15f) : Col (0.75f, 0.5f, 0.28f);
		D.gloss = 0.5f; D.ellipsoid (f, V3 (cr, cr * 0.6f, cr), cc, cc * 0.6f, 12, 7);
		if (cc.g < 0.3f)
			for (int q = 0; q < 5; q++)
			{
				float a = (float) q * 1.3f;
				D.sphere (f.o + V3 (cosf (a) * cr * 0.55f, cr * 0.42f, sinf (a) * cr * 0.55f), cr * 0.12f, Col (1, 1, 0.96f), 6, 4);
			}
	}
	// des arbres au loin : feuillus et sapins
	for (int k = 0; k < 14; k++)
	{
		float x, z; place (6.5f, 12, x, z);
		float y = groundY (x, z) - 0.05f, s = r.range (0.8f, 1.5f);
		bool pine = r.chance (0.4f);
		V3 pts[2] = { V3 (x, y, z), V3 (x, y + s * (pine ? 0.5f : 0.9f), z) };
		float rad[2] = { 0.09f * s, 0.07f * s };
		Col cols[2] = { Col (0.38f, 0.26f, 0.17f), Col (0.45f, 0.31f, 0.2f) };
		D.gloss = 0; D.tube (pts, rad, cols, 2, 7, 1, 0, V3 (1, 0, 0), 2);
		Col leaf = hsv (r.range (0.24f, 0.36f), r.range (0.45f, 0.65f), r.range (0.38f, 0.55f));
		if (pine)
			for (int q = 0; q < 3; q++)
			{
				float yy = y + s * (0.35f + 0.42f * (float) q), rr = s * (0.55f - 0.15f * (float) q);
				V3 cp[2] = { V3 (x, yy, z), V3 (x, yy + s * 0.65f, z) };
				float cr[2] = { rr, 0.01f };
				Col cc[2] = { leaf * 0.85f, leaf };
				D.gloss = 0.05f; D.tube (cp, cr, cc, 2, 10, 1, 0, V3 (1, 0, 0), 1);
			}
		else
			for (int q = 0; q < 4; q++)
			{
				V3 o (x + r.range (-0.3f, 0.3f) * s, y + s * r.range (1.0f, 1.45f), z + r.range (-0.3f, 0.3f) * s);
				Frame f; f.o = o;
				float rr = s * r.range (0.38f, 0.55f);
				D.gloss = 0.05f; D.ellipsoid (f, V3 (rr, rr * 0.85f, rr), leaf * 1.1f, leaf * 0.7f, 9, 6);
			}
	}
	// des touffes d'herbe (deux faces)
	Builder G (&blades);
	G.two = true; G.gloss = 0;
	for (int k = 0; k < 170; k++)
	{
		float x, z; place (0.9f, 7, x, z);
		float y = groundY (x, z);
		int nb = r.irange (3, 5);
		Col gc = hsv (r.range (0.24f, 0.31f), r.range (0.5f, 0.7f), r.range (0.45f, 0.65f));
		for (int q = 0; q < nb; q++)
		{
			float a = r.f () * 2 * PI, h = r.range (0.07f, 0.17f), lean = r.range (0.2f, 0.6f);
			V3 d (cosf (a), 0, sinf (a)), s (-d.z, 0, d.x);
			V3 b0 = V3 (x, y, z) + d * 0.015f;
			V3 tip = b0 + V3 (0, h, 0) + d * (h * lean);
			V3 nn = norm (cross (s, tip - b0));
			int i0 = G.add (b0 - s * 0.012f, nn, gc * 0.7f), i1 = G.add (b0 + s * 0.012f, nn, gc * 0.7f), i2 = G.add (tip, nn, gc * 1.25f);
			G.tri (i0, i1, i2);
		}
	}
	blades.flags = 0;
}

// ==== le ciel en 2D ===================================================================================================
static inline uint32_t mixp (uint32_t d, const Col &c, float a)
{
	float r = (float) ((d >> 16) & 255) / 255, g = (float) ((d >> 8) & 255) / 255, b = (float) (d & 255) / 255;
	return packc (Col (r + (c.r - r) * a, g + (c.g - g) * a, b + (c.b - b) * a));
}
static inline uint32_t addp (uint32_t d, const Col &c, float a)
{
	float r = (float) ((d >> 16) & 255) / 255, g = (float) ((d >> 8) & 255) / 255, b = (float) (d & 255) / 255;
	return packc (Col (r + c.r * a, g + c.g * a, b + c.b * a));
}
static void softDisc (uint32_t *px, int w, int h, int stride, float cx, float cy, float rx, float ry, const Col &c, float a, bool additive, float hard = 0.5f)
{
	int x0 = (int) floorf (cx - rx - 1), x1 = (int) ceilf (cx + rx + 1), y0 = (int) floorf (cy - ry - 1), y1 = (int) ceilf (cy + ry + 1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
		{
			float dx = ((float) x + 0.5f - cx) / rx, dy = ((float) y + 0.5f - cy) / ry;
			float d = sqrtf (dx * dx + dy * dy);
			if (d >= 1) continue;
			float k = 1 - smooth (hard, 1, d);
			uint32_t &p = px[(long) y * stride + x];
			p = additive ? addp (p, c, k * a) : mixp (p, c, k * a);
		}
}

void drawSky (uint32_t *px, int w, int h, int stride, const Camera &C, const Light &L, float t, float clouds)
{
	// le dégradé, ligne par ligne, d'après la hauteur du regard
	for (int y = 0; y < h; y++)
	{
		V3 d = C.ray ((float) w * 0.5f, (float) y + 0.5f);
		float el = asinf (clampf (d.y, -1, 1));
		Col c = el > 0 ? mixc (L.skyHor, L.skyTop, powf (smooth (0, 1.0f, el), 0.6f)) : mixc (L.skyHor, L.skyLow, smooth (0, 0.3f, -el));
		uint32_t v = packc (c);
		uint32_t *row = px + (long) y * stride;
		for (int x = 0; x < w; x++) row[x] = v;
	}
	// les étoiles
	if (L.night > 0.05f)
		for (int k = 0; k < 220; k++)
		{
			float az = hashf (k, 1, 3) * 2 * PI, el = asinf (hashf (k, 2, 3) * 0.95f + 0.04f);
			V3 d (cosf (el) * sinf (az), sinf (el), cosf (el) * cosf (az));
			float sx, sy;
			if (!C.project (C.eye + d * 40, sx, sy)) continue;
			if (sx < 0 || sy < 0 || sx >= (float) w || sy >= (float) h) continue;
			float tw = 0.6f + 0.4f * sinf (t * (1.5f + hashf (k, 3, 3) * 3) + (float) k);
			float a = L.night * tw * (0.4f + 0.6f * hashf (k, 4, 3)) * smooth (0.02f, 0.25f, el);
			float s = 0.7f + 1.3f * hashf (k, 5, 3);
			softDisc (px, w, h, stride, sx, sy, s, s, Col (1, 0.97f, 0.9f), a, true, 0.2f);
		}
	// le soleil ou la lune
	{
		V3 d = L.night > 0.5f ? norm (V3 (-0.5f, 0.55f, -0.65f)) : norm (V3 (L.sun.x * -1, L.sun.y, -L.sun.z));
		float sx, sy;
		if (C.project (C.eye + d * 40, sx, sy))
		{
			float R = (float) h * 0.045f;
			if (L.night > 0.5f)
			{
				softDisc (px, w, h, stride, sx, sy, R * 4, R * 4, Col (0.5f, 0.6f, 0.9f), 0.12f * L.night, true, 0);
				softDisc (px, w, h, stride, sx, sy, R, R, Col (0.93f, 0.94f, 0.98f), L.night, false, 0.88f);
				softDisc (px, w, h, stride, sx - R * 0.3f, sy - R * 0.2f, R * 0.25f, R * 0.22f, Col (0.78f, 0.8f, 0.86f), L.night * 0.8f, false, 0.6f);
				softDisc (px, w, h, stride, sx + R * 0.35f, sy + R * 0.25f, R * 0.18f, R * 0.16f, Col (0.8f, 0.82f, 0.88f), L.night * 0.8f, false, 0.6f);
			}
			else if (L.sunUp > 0)
			{
				softDisc (px, w, h, stride, sx, sy, R * 6, R * 6, L.sunDisc, 0.2f * L.sunUp, true, 0);
				softDisc (px, w, h, stride, sx, sy, R * 2.2f, R * 2.2f, L.sunDisc, 0.35f * L.sunUp, true, 0);
				softDisc (px, w, h, stride, sx, sy, R, R, L.sunDisc, L.sunUp, false, 0.85f);
			}
		}
	}
	// les nuages : des groupes de boules douces qui dérivent
	if (clouds > 0)
	{
		Col cc = mixc (mixc (Col (1, 1, 1), L.skyHor, 0.25f), L.skyTop * 1.4f, L.night * 0.8f);
		Col cs = cc * (0.82f - 0.2f * L.night);
		for (int k = 0; k < 9; k++)
		{
			float az = hashf (k, 7, 1) * 2 * PI + t * 0.004f * (0.5f + hashf (k, 8, 1));
			float el = 0.12f + hashf (k, 9, 1) * 0.35f;
			V3 d (cosf (el) * sinf (az), sinf (el), cosf (el) * cosf (az));
			float sx, sy;
			if (!C.project (C.eye + d * 40, sx, sy)) continue;
			float S = (float) h * (0.05f + 0.04f * hashf (k, 10, 1));
			if (sx < -S * 4 || sx > (float) w + S * 4) continue;
			for (int q = 0; q < 6; q++)
			{
				float ox = (hashf (k, q, 2) - 0.5f) * S * 4, oy = (hashf (k, q, 4) - 0.5f) * S * 0.7f;
				float rr = S * (0.6f + 0.7f * hashf (k, q, 5));
				softDisc (px, w, h, stride, sx + ox, sy + oy + rr * 0.2f, rr * 1.3f, rr * 0.7f, cs, 0.55f * clouds, false, 0.3f);
				softDisc (px, w, h, stride, sx + ox, sy + oy, rr * 1.2f, rr * 0.65f, cc, 0.75f * clouds, false, 0.35f);
			}
		}
	}
}

}	// namespace mo
