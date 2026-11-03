// Rasterizer.cs -- le repli de System.3D sans GPU : les memes sommets et lots que gk_render, dessines par le
// processeur. Seulement ce que Renderer3D produit : test LESS, faces, melange ALPHA, texel * couleur + couleur 2.
// Couleurs et Z lineaires a l'ecran, comme teapot d'Onyx ; seules les textures sont corrigees en perspective.
#nullable disable
using System;
using System.GpuKit;
using System.Runtime.CompilerServices;

namespace System.Windows.Media.Media3D;

internal static unsafe class Rasterizer
{
	[MethodImpl (MethodImplOptions.AggressiveOptimization)]
	internal static void Draw (GkVertex* v, GkBatch* batches, int count, ImageTexture[] textures, uint* px, int w, int h, int stride, uint clear, float* zb)
	{
		for (int y = 0; y < h; y++)
		{
			new Span<uint> (px + (long) y * stride, w).Fill (clear);
			new Span<float> (zb + (long) y * w, w).Fill (2);
		}
		for (int i = 0; i < count; i++)
		{
			GkBatch* b = batches + i;
			var tex = textures[i];
			if (tex != null)
			{
				fixed (uint* t = tex.Pixels)
					Batch (v, b, t, tex.Width, tex.Height, px, w, h, stride, zb);
			}
			else
			{
				Batch (v, b, null, 0, 0, px, w, h, stride, zb);
			}
		}
	}

	[MethodImpl (MethodImplOptions.AggressiveOptimization)]
	static void Batch (GkVertex* v, GkBatch* b, uint* tex, int tw, int th, uint* px, int w, int h, int stride, float* zb)
	{
		uint flags = b->Flags;
		bool mat = (flags & GK.GK_B_NOMATRIX) == 0, zwrite = (flags & GK.GK_B_NOZWRITE) == 0;
		bool blend = ((flags >> (int) GK.GK_B_BLEND_SHIFT) & 15) != 0, linear = (flags & GK.GK_B_LINEAR) != 0;
		bool cullBack = (flags & GK.GK_B_CULL_BACK) != 0, cullFront = (flags & GK.GK_B_CULL_FRONT) != 0;
		float* m = b->Matrix;
		float hw = w * 0.5f, hh = h * 0.5f;
		float* X = stackalloc float[3], Y = stackalloc float[3], Z = stackalloc float[3], IW = stackalloc float[3];
		for (uint i = b->First; i + 2 < b->First + b->Count; i += 3)
		{
			GkVertex* a = v + i;
			for (int k = 0; k < 3; k++)
			{
				GkVertex* q = a + k;
				float x = q->X, y = q->Y, z = q->Z, cw = q->W;
				if (mat)
				{
					float x0 = x, y0 = y, z0 = z, w0 = cw;
					x = m[0] * x0 + m[1] * y0 + m[2] * z0 + m[3] * w0;
					y = m[4] * x0 + m[5] * y0 + m[6] * z0 + m[7] * w0;
					z = m[8] * x0 + m[9] * y0 + m[10] * z0 + m[11] * w0;
					cw = m[12] * x0 + m[13] * y0 + m[14] * z0 + m[15] * w0;
				}
				float iw = 1 / cw;
				IW[k] = iw;
				X[k] = (x * iw + 1) * hw;
				Y[k] = (1 - y * iw) * hh;
				Z[k] = z * iw;
			}
			float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);	// (y en bas : une face avant est negative)
			if (area == 0 || (cullBack && area > 0) || (cullFront && area < 0))
				continue;
			int minX = (int) MathF.Floor (MathF.Min (X[0], MathF.Min (X[1], X[2])));
			int maxX = (int) MathF.Ceiling (MathF.Max (X[0], MathF.Max (X[1], X[2])));
			int minY = (int) MathF.Floor (MathF.Min (Y[0], MathF.Min (Y[1], Y[2])));
			int maxY = (int) MathF.Ceiling (MathF.Max (Y[0], MathF.Max (Y[1], Y[2])));
			minX = Math.Max (minX, 0);
			minY = Math.Max (minY, 0);
			maxX = Math.Min (maxX, w - 1);
			maxY = Math.Min (maxY, h - 1);
			if (minX > maxX || minY > maxY)
				continue;
			float inv = 1 / area;
			// poids barycentriques : leur pas par pixel, leur valeur au premier pixel
			float dw0x = (Y[1] - Y[2]) * inv, dw0y = (X[2] - X[1]) * inv;
			float dw1x = (Y[2] - Y[0]) * inv, dw1y = (X[0] - X[2]) * inv;
			float sx = minX + 0.5f, sy = minY + 0.5f;
			float rw0 = ((X[1] - sx) * (Y[2] - sy) - (X[2] - sx) * (Y[1] - sy)) * inv;
			float rw1 = ((X[2] - sx) * (Y[0] - sy) - (X[0] - sx) * (Y[2] - sy)) * inv;
			uint c0 = a[0].Color, c1 = a[1].Color, c2 = a[2].Color, e0 = a[0].Color2, e1 = a[1].Color2, e2 = a[2].Color2;
			float r0 = c0 & 255, g0 = (c0 >> 8) & 255, b0 = (c0 >> 16) & 255, a0 = c0 >> 24;
			float dr1 = (float) (c1 & 255) - r0, dg1 = (float) ((c1 >> 8) & 255) - g0, db1 = (float) ((c1 >> 16) & 255) - b0, da1 = (float) (c1 >> 24) - a0;
			float dr2 = (float) (c2 & 255) - r0, dg2 = (float) ((c2 >> 8) & 255) - g0, db2 = (float) ((c2 >> 16) & 255) - b0, da2 = (float) (c2 >> 24) - a0;
			float er0 = e0 & 255, eg0 = (e0 >> 8) & 255, eb0 = (e0 >> 16) & 255;
			float der1 = (float) (e1 & 255) - er0, deg1 = (float) ((e1 >> 8) & 255) - eg0, deb1 = (float) ((e1 >> 16) & 255) - eb0;
			float der2 = (float) (e2 & 255) - er0, deg2 = (float) ((e2 >> 8) & 255) - eg0, deb2 = (float) ((e2 >> 16) & 255) - eb0;
			// w0 + w1 + w2 = 1 : v = v2 + w0 (v0 - v2) + w1 (v1 - v2), ecrit ici depuis le sommet 0
			float z0v = Z[0], dz1 = Z[1] - z0v, dz2 = Z[2] - z0v;
			float iw0 = IW[0], diw1 = IW[1] - iw0, diw2 = IW[2] - iw0;
			float s0 = a[0].S * iw0, ds1 = a[1].S * IW[1] - s0, ds2 = a[2].S * IW[2] - s0;
			float t0 = a[0].T * iw0, dt1 = a[1].T * IW[1] - t0, dt2 = a[2].T * IW[2] - t0;
			for (int y = minY; y <= maxY; y++, rw0 += dw0y, rw1 += dw1y)
			{
				float l0 = rw0, l1 = rw1;
				uint* row = px + (long) y * stride;
				float* zrow = zb + (long) y * w;
				bool seen = false;
				for (int x = minX; x <= maxX; x++, l0 += dw0x, l1 += dw1x)
				{
					float l2 = 1 - l0 - l1;
					if (l0 < 0 || l1 < 0 || l2 < 0)
					{
						if (seen)
							break;		// (un triangle est convexe : la ligne est finie)
						continue;
					}
					seen = true;
					float z = z0v + l1 * dz1 + l2 * dz2;
					if (!(z < zrow[x]))
						continue;
					if (zwrite)
						zrow[x] = z;
					float cr = r0 + l1 * dr1 + l2 * dr2, cg = g0 + l1 * dg1 + l2 * dg2, cb = b0 + l1 * db1 + l2 * db2, ca = a0 + l1 * da1 + l2 * da2;
					if (tex != null)
					{
						float iw = 1 / (iw0 + l1 * diw1 + l2 * diw2);
						float u = (s0 + l1 * ds1 + l2 * ds2) * iw, t = (t0 + l1 * dt1 + l2 * dt2) * iw;
						uint texel = linear ? Bilinear (tex, tw, th, u, t) : Nearest (tex, tw, th, u, t);
						cr = cr * ((texel >> 16) & 255) / 255;
						cg = cg * ((texel >> 8) & 255) / 255;
						cb = cb * (texel & 255) / 255;
						ca = ca * (texel >> 24) / 255;
					}
					cr += er0 + l1 * der1 + l2 * der2;
					cg += eg0 + l1 * deg1 + l2 * deg2;
					cb += eb0 + l1 * deb1 + l2 * deb2;
					int ir = cr >= 255 ? 255 : (int) cr, ig = cg >= 255 ? 255 : (int) cg, ib = cb >= 255 ? 255 : (int) cb;
					if (blend)
					{
						int ia = (int) ca, na = 255 - ia;
						uint d = row[x];
						ir = (ir * ia + (int) ((d >> 16) & 255) * na) / 255;
						ig = (ig * ia + (int) ((d >> 8) & 255) * na) / 255;
						ib = (ib * ia + (int) (d & 255) * na) / 255;
					}
					row[x] = (uint) (ir << 16 | ig << 8 | ib);
				}
			}
		}
	}

	[MethodImpl (MethodImplOptions.AggressiveInlining)]
	static int Wrap (int i, int n) { i %= n; return i < 0 ? i + n : i; }

	[MethodImpl (MethodImplOptions.AggressiveInlining)]
	static uint Nearest (uint* tex, int w, int h, float u, float v) =>
		tex[Wrap ((int) MathF.Floor (v * h), h) * w + Wrap ((int) MathF.Floor (u * w), w)];

	static uint Bilinear (uint* tex, int w, int h, float u, float v)
	{
		float fx = u * w - 0.5f, fy = v * h - 0.5f;
		int x = (int) MathF.Floor (fx), y = (int) MathF.Floor (fy);
		int ax = (int) ((fx - x) * 256), ay = (int) ((fy - y) * 256);
		int x0 = Wrap (x, w), x1 = Wrap (x + 1, w), y0 = Wrap (y, h) * w, y1 = Wrap (y + 1, h) * w;
		uint top = Mix (tex[y0 + x0], tex[y0 + x1], ax), bottom = Mix (tex[y1 + x0], tex[y1 + x1], ax);
		return Mix (top, bottom, ay);
	}

	[MethodImpl (MethodImplOptions.AggressiveInlining)]
	static uint Mix (uint a, uint b, int k)
	{
		uint n = (uint) (256 - k), m = (uint) k;
		uint rb = ((a & 0xFF00FF) * n + (b & 0xFF00FF) * m) >> 8 & 0xFF00FF;
		uint ag = ((a >> 8 & 0xFF00FF) * n + (b >> 8 & 0xFF00FF) * m) & 0xFF00FF00;
		return rb | ag;
	}
}
