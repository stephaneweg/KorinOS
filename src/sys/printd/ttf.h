//
// ttf.h -- une police TrueType lue directement (printd n'a pas FreeType) : caractere -> glyphe (cmap 4 et 12), avances
// (hmtx), metriques (hhea), contours (glyf, composites compris) pour remplir les glyphes a la resolution de l'imprimante
// sans hinting, comme FT_LOAD_NO_HINTING chez Onyx. Pas de crenage (Onyx lisait celui de la table kern).
//
#ifndef PRINTD_TTF_H
#define PRINTD_TTF_H

struct Ttf
{
	const unsigned char *d; unsigned n;
	unsigned cmap, hmtx, glyf, loca, nhm, nglyph; int upem, asc, desc, gap; bool longloca; unsigned cmap4, cmap12;
	Ttf () : d (0), n (0), cmap (0), hmtx (0), glyf (0), loca (0), nhm (0), nglyph (0), upem (1000), asc (0), desc (0), gap (0), longloca (false), cmap4 (0), cmap12 (0) {}

	unsigned u16 (unsigned o) const { return o + 2 <= n ? (unsigned) d[o] << 8 | d[o + 1] : 0; }
	int s16 (unsigned o) const { return (short) u16 (o); }
	unsigned u32 (unsigned o) const { return o + 4 <= n ? (unsigned) d[o] << 24 | (unsigned) d[o + 1] << 16 | (unsigned) d[o + 2] << 8 | d[o + 3] : 0; }

	bool open (const unsigned char *data, unsigned len)
	{
		d = data; n = len;
		if (n < 12) return false;
		unsigned nt = u16 (4), head = 0, hhea = 0, maxp = 0;
		for (unsigned i = 0; i < nt; i++)
		{
			unsigned e = 12 + i * 16, o = u32 (e + 8);
			if (e + 16 > n) return false;
			const unsigned char *t = d + e;
			if (!memcmp (t, "cmap", 4)) cmap = o;
			else if (!memcmp (t, "hmtx", 4)) hmtx = o;
			else if (!memcmp (t, "glyf", 4)) glyf = o;
			else if (!memcmp (t, "loca", 4)) loca = o;
			else if (!memcmp (t, "head", 4)) head = o;
			else if (!memcmp (t, "hhea", 4)) hhea = o;
			else if (!memcmp (t, "maxp", 4)) maxp = o;
		}
		if (!head || !hhea || !maxp || !glyf || !loca) return false;
		upem = (int) u16 (head + 18); if (upem <= 0) upem = 1000;
		longloca = s16 (head + 50) != 0;
		asc = s16 (hhea + 4); desc = s16 (hhea + 6); gap = s16 (hhea + 8); nhm = u16 (hhea + 34);
		nglyph = u16 (maxp + 4);
		if (cmap)
		{
			unsigned k = u16 (cmap + 2);
			for (unsigned i = 0; i < k; i++)
			{
				unsigned p = u16 (cmap + 4 + i * 8), e = u16 (cmap + 6 + i * 8), o = cmap + u32 (cmap + 8 + i * 8), f = u16 (o);
				if (f == 4 && (p == 3 || p == 0) && !cmap4) cmap4 = o;
				if (f == 12 && (p == 3 || p == 0) && !cmap12) cmap12 = o;
				(void) e;
			}
		}
		return true;
	}
	unsigned gid (unsigned cp) const
	{
		if (cmap12)
		{
			unsigned ng = u32 (cmap12 + 12);
			for (unsigned lo = 0, hi = ng; lo < hi; )
			{
				unsigned m = (lo + hi) / 2, o = cmap12 + 16 + m * 12, a = u32 (o), b = u32 (o + 4);
				if (cp < a) hi = m; else if (cp > b) lo = m + 1; else return u32 (o + 8) + cp - a;
			}
		}
		if (cmap4 && cp < 0x10000)
		{
			unsigned sc = u16 (cmap4 + 6) / 2, endc = cmap4 + 14, startc = endc + sc * 2 + 2, delta = startc + sc * 2, rng = delta + sc * 2;
			for (unsigned i = 0; i < sc; i++)
			{
				if (cp > u16 (endc + i * 2)) continue;
				unsigned s = u16 (startc + i * 2);
				if (cp < s) return 0;
				unsigned ro = u16 (rng + i * 2), dl = u16 (delta + i * 2);
				if (!ro) return (cp + dl) & 0xFFFF;
				unsigned g = u16 (rng + i * 2 + ro + (cp - s) * 2);
				return g ? (g + dl) & 0xFFFF : 0;
			}
		}
		return 0;
	}
	// l'avance d'un glyphe, en unites de la police
	int advance (unsigned g) const
	{
		if (!hmtx || !nhm) return upem / 2;
		return (int) u16 (hmtx + (g < nhm ? g : nhm - 1) * 4);
	}
	// les contours d'un glyphe transformes : x' = sx * x + skew * y + ox, y' = sy * y + oy ; em.seg (x0, y0, x1, y1)
	template <class E> void contours (unsigned g, float sx, float sy, float skew, float ox, float oy, int steps, E &em) const
	{
		M m = { 1, 0, 0, 1, 0, 0 };
		walk (g, m, sx, sy, skew, ox, oy, steps, em, 0);
	}

private:
	struct M { float a, b, c, d, e, f; };
	struct P { float x, y; bool on; };
	void range (unsigned g, unsigned &o, unsigned &l) const
	{
		o = l = 0;
		if (g >= nglyph) return;
		unsigned a = longloca ? u32 (loca + g * 4) : u16 (loca + g * 2) * 2, b = longloca ? u32 (loca + (g + 1) * 4) : u16 (loca + (g + 1) * 2) * 2;
		if (b > a) { o = glyf + a; l = b - a; }
	}
	template <class E> void walk (unsigned g, const M &m, float sx, float sy, float skew, float ox, float oy, int steps, E &em, int depth) const
	{
		unsigned o, l;
		range (g, o, l);
		if (!l || depth > 5) return;
		int nc = s16 (o);
		if (nc < 0)
		{
			unsigned p = o + 10, fl;
			do
			{
				fl = u16 (p); unsigned sub = u16 (p + 2); p += 4;
				int a1, a2;
				if (fl & 1) { a1 = s16 (p); a2 = s16 (p + 2); p += 4; }
				else { a1 = (signed char) d[p]; a2 = (signed char) d[p + 1]; p += 2; }
				if (!(fl & 2)) a1 = a2 = 0;
				float a = 1, b = 0, c = 0, e = 1;
				if (fl & 8) { a = e = s16 (p) / 16384.0f; p += 2; }
				else if (fl & 0x40) { a = s16 (p) / 16384.0f; e = s16 (p + 2) / 16384.0f; p += 4; }
				else if (fl & 0x80) { a = s16 (p) / 16384.0f; b = s16 (p + 2) / 16384.0f; c = s16 (p + 4) / 16384.0f; e = s16 (p + 6) / 16384.0f; p += 8; }
				M k = { m.a * a + m.c * b, m.b * a + m.d * b, m.a * c + m.c * e, m.b * c + m.d * e,
					m.a * a1 + m.c * a2 + m.e, m.b * a1 + m.d * a2 + m.f };
				walk (sub, k, sx, sy, skew, ox, oy, steps, em, depth + 1);
			} while (fl & 0x20);
			return;
		}
		if (nc == 0) return;
		unsigned p = o + 10;
		unsigned np = u16 (p + (nc - 1) * 2) + 1;
		unsigned ends = p; p += nc * 2;
		p += 2 + u16 (p);
		if (np > 20000) return;
		P *pt = new P[np]; unsigned char *fl = new unsigned char[np];
		for (unsigned i = 0; i < np && p < n; )
		{
			unsigned char f = d[p++]; fl[i++] = f;
			if (f & 8) { unsigned r = d[p++]; while (r-- && i < np) fl[i++] = f; }
		}
		int v = 0;
		for (unsigned i = 0; i < np; i++)
		{
			if (fl[i] & 2) { int k = d[p++]; v += fl[i] & 16 ? k : -k; }
			else if (!(fl[i] & 16)) { v += s16 (p); p += 2; }
			pt[i].x = (float) v; pt[i].on = (fl[i] & 1) != 0;
		}
		v = 0;
		for (unsigned i = 0; i < np; i++)
		{
			if (fl[i] & 4) { int k = d[p++]; v += fl[i] & 32 ? k : -k; }
			else if (!(fl[i] & 32)) { v += s16 (p); p += 2; }
			pt[i].y = (float) v;
		}
		for (unsigned i = 0; i < np; i++)
		{
			float x = pt[i].x, y = pt[i].y, gx = m.a * x + m.c * y + m.e, gy = m.b * x + m.d * y + m.f;
			pt[i].x = sx * gx + skew * gy + ox; pt[i].y = sy * gy + oy;
		}
		unsigned st = 0;
		for (int c = 0; c < nc; c++)
		{
			unsigned e = u16 (ends + c * 2);
			if (e >= np || e < st) break;
			curve (pt + st, (int) (e - st + 1), steps, em);
			st = e + 1;
		}
		delete[] pt; delete[] fl;
	}
	// un contour : points sur la courbe et points de controle (quadratiques), les milieux sous-entendus
	template <class E> static void curve (const P *c, int k, int steps, E &em)
	{
		if (k < 2) return;
		float cx, cy; int first = 0;
		if (c[0].on) { cx = c[0].x; cy = c[0].y; first = 1; }
		else if (c[k - 1].on) { cx = c[k - 1].x; cy = c[k - 1].y; }
		else { cx = (c[0].x + c[k - 1].x) / 2; cy = (c[0].y + c[k - 1].y) / 2; }
		float sx = cx, sy = cy, qx = 0, qy = 0; bool q = false;
		for (int i = 0; i <= k - first; i++)
		{
			P a;
			if (i == k - first) { a.x = sx; a.y = sy; a.on = true; }
			else a = c[(first + i) % k];
			if (a.on)
			{
				if (q) quad (cx, cy, qx, qy, a.x, a.y, steps, em);
				else em.seg (cx, cy, a.x, a.y);
				cx = a.x; cy = a.y; q = false;
			}
			else
			{
				if (q) { float mx = (qx + a.x) / 2, my = (qy + a.y) / 2; quad (cx, cy, qx, qy, mx, my, steps, em); cx = mx; cy = my; }
				qx = a.x; qy = a.y; q = true;
			}
		}
	}
	template <class E> static void quad (float x0, float y0, float x1, float y1, float x2, float y2, int steps, E &em)
	{
		float px = x0, py = y0;
		for (int i = 1; i <= steps; i++)
		{
			float t = (float) i / steps, u = 1 - t;
			float x = u * u * x0 + 2 * u * t * x1 + t * t * x2, y = u * u * y0 + 2 * u * t * y1 + t * t * y2;
			em.seg (px, py, x, y); px = x; py = y;
		}
	}
};

#endif
