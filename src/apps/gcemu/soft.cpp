//
// soft.cpp -- le repli sans GPU (QEMU, GPU absent) : les images de gc_gxdraw.cpp (triangles en clip, TEV par sommet :
// texel x couleur + couleur 2) sur le processeur. Pour voir que le jeu tourne, pas pour jouer : ni copies d'EFB, ni
// TEV par pixel, ni fidelite. Coupe au plan proche, point par point, tampon de profondeur de 16 bits.
//
#include "soft.h"
#include <string.h>

using gc::u8; using gc::u32; using gc::s32;

enum { MAXW = 640, MAXH = 576 };
static float g_z[MAXW * MAXH];

struct V { float x, y, z, w, s, t, c[8]; };

static inline V lerpV (const V &a, const V &b, float k)
{
	V r;
	const float *pa = &a.x, *pb = &b.x;
	float *o = &r.x;
	for (unsigned i = 0; i < sizeof (V) / 4; i++) o[i] = pa[i] + (pb[i] - pa[i]) * k;
	return r;
}
static inline V fromG (const gc::GVertex &g)
{
	V v;
	v.x = g.x; v.y = g.y; v.z = g.z; v.w = g.w; v.s = g.s; v.t = g.t;
	v.c[0] = g.r; v.c[1] = g.g; v.c[2] = g.b; v.c[3] = g.a; v.c[4] = g.r2; v.c[5] = g.g2; v.c[6] = g.b2; v.c[7] = g.a2;
	return v;
}
static inline int wrapc (int v, int n, int mode)
{
	if (mode == 1) return v < 0 ? 0 : v >= n ? n - 1 : v;
	if (mode == 2) { int p = v & (2 * n - 1); if (n & (n - 1)) { p = v % (2 * n); if (p < 0) p += 2 * n; } return p < n ? p : 2 * n - 1 - p; }
	if (n & (n - 1)) { int p = v % n; return p < 0 ? p + n : p; }
	return v & (n - 1);
}

static void tri (const V &a, const V &b, const V &c, u32 flags, const gc::GTexture *T, u32 *px, int W, int H, long pitch)
{
	if (a.w <= 0 || b.w <= 0 || c.w <= 0) return;
	float ia = 1 / a.w, ib = 1 / b.w, ic = 1 / c.w;
	float ax = (a.x * ia + 1) * .5f * W, ay = (1 - a.y * ia) * .5f * H;
	float bx = (b.x * ib + 1) * .5f * W, by = (1 - b.y * ib) * .5f * H;
	float cx = (c.x * ic + 1) * .5f * W, cy = (1 - c.y * ic) * .5f * H;
	float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
	if (area == 0) return;
	if ((flags & gc::GF_CULL_BACK) && area > 0) return;
	if ((flags & gc::GF_CULL_FRONT) && area < 0) return;
	float mnx = ax < bx ? ax : bx, mxx = ax > bx ? ax : bx, mny = ay < by ? ay : by, mxy = ay > by ? ay : by;
	if (cx < mnx) mnx = cx;
	if (cx > mxx) mxx = cx;
	if (cy < mny) mny = cy;
	if (cy > mxy) mxy = cy;
	int x0 = mnx < 0 ? 0 : (int) mnx, x1 = mxx >= W ? W - 1 : (int) mxx, y0 = mny < 0 ? 0 : (int) mny, y1 = mxy >= H ? H - 1 : (int) mxy;
	float inv = 1 / area;
	int zf = flags & 7, blend = (flags >> gc::GF_BLEND_SHIFT) & 7, wrapS = (flags >> gc::GF_WRAP_S_SHIFT) & 3, wrapT = (flags >> gc::GF_WRAP_T_SHIFT) & 3;
	u32 aref = flags >> 19;
	bool at = flags & gc::GF_ALPHATEST, nozw = flags & gc::GF_NOZWRITE;
	for (int y = y0; y <= y1; y++)
	{
		u32 *d = (u32 *) ((char *) px + y * pitch);
		float *zb = g_z + y * MAXW;
		float py = (float) y + .5f;
		for (int x = x0; x <= x1; x++)
		{
			float qx = (float) x + .5f;
			float l0 = ((bx - qx) * (cy - py) - (by - py) * (cx - qx)) * inv;
			float l1 = ((cx - qx) * (ay - py) - (cy - py) * (ax - qx)) * inv;
			float l2 = 1 - l0 - l1;
			if (l0 < 0 || l1 < 0 || l2 < 0) continue;
			float p0 = l0 * ia, p1 = l1 * ib, p2 = l2 * ic, iw = 1 / (p0 + p1 + p2);
			p0 *= iw; p1 *= iw; p2 *= iw;
			float z = (a.z * ia * l0 + b.z * ib * l1 + c.z * ic * l2);
			bool pass = zf == 7 ? true : zf == 0 ? z < zb[x] : zf == 2 ? z == zb[x] : zf == 3 ? z <= zb[x] : zf == 4 ? z > zb[x] : zf == 5 ? z != zb[x] : z >= zb[x];
			if (!pass) continue;
			int col[4];
			for (int k = 0; k < 4; k++)
			{
				float v = a.c[k] * p0 + b.c[k] * p1 + c.c[k] * p2;
				float v2 = a.c[k + 4] * p0 + b.c[k + 4] * p1 + c.c[k + 4] * p2;
				col[k] = (int) v; col[k + 0] = col[k];
				if (T)
				{
					static const int sh[4] = { 16, 8, 0, 24 };
					float s = a.s * p0 + b.s * p1 + c.s * p2, t = a.t * p0 + b.t * p1 + c.t * p2;
					int tx = wrapc ((int) (s * (float) T->w), T->w, wrapS), ty = wrapc ((int) (t * (float) T->h), T->h, wrapT);
					u32 texel = T->px[ty * T->w + tx];
					col[k] = (((texel >> sh[k]) & 255) * col[k]) / 255 + (int) v2;
				}
				if (col[k] > 255) col[k] = 255;
			}
			if (at && (u32) col[3] < aref) continue;
			u32 o = d[x];
			if (blend)
			{
				int sa = col[3], dc[3] = { (int) ((o >> 16) & 255), (int) ((o >> 8) & 255), (int) (o & 255) };
				for (int k = 0; k < 3; k++)
				{
					int r = blend == 1 ? (col[k] * sa + dc[k] * (255 - sa)) / 255 : blend == 2 ? col[k] * sa / 255 + dc[k]
						: blend == 3 ? col[k] * dc[k] / 255 : blend == 4 ? col[k] + dc[k] * (255 - sa) / 255 : col[k];
					col[k] = r > 255 ? 255 : r;
				}
			}
			d[x] = (u32) col[0] << 16 | (u32) col[1] << 8 | (u32) col[2];
			if (!nozw) zb[x] = z;
		}
	}
}

void softFrame (gc::Machine &m, const gc::GFrame &F, u32 *px, int W, int H, long pitch)
{
	if (W > MAXW) W = MAXW;
	if (H > MAXH) H = MAXH;
	for (int y = 0; y < H; y++)
	{
		u32 *d = (u32 *) ((char *) px + y * pitch);
		float *z = g_z + y * MAXW;
		for (int x = 0; x < W; x++) { d[x] = F.clear; z[x] = 1e30f; }
	}
	for (int i = 0; i < F.nb; i++)
	{
		const gc::GBatch &B = F.b[i];
		const gc::GTexture *T = B.tex >= 0 && m.tex[B.tex].w > 0 && m.tex[B.tex].px ? &m.tex[B.tex] : 0;
		for (u32 k = 0; k + 2 < B.count; k += 3)
		{
			V v[3] = { fromG (F.v[B.first + k]), fromG (F.v[B.first + k + 1]), fromG (F.v[B.first + k + 2]) };
			// le plan proche : w petit ; un sommet derriere donne un ou deux triangles
			const float near = 1e-3f;
			int in = 0;
			for (int j = 0; j < 3; j++) if (v[j].w >= near) in++;
			if (in == 3) tri (v[0], v[1], v[2], B.flags, T, px, W, H, pitch);
			else if (in)
			{
				V o[4]; int n = 0;
				for (int j = 0; j < 3; j++)
				{
					const V &a = v[j], &b = v[(j + 1) % 3];
					if (a.w >= near) o[n++] = a;
					if ((a.w >= near) != (b.w >= near)) o[n++] = lerpV (a, b, (near - a.w) / (b.w - a.w));
				}
				tri (o[0], o[1], o[2], B.flags, T, px, W, H, pitch);
				if (n == 4) tri (o[0], o[2], o[3], B.flags, T, px, W, H, pitch);
			}
		}
	}
}
