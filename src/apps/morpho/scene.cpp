//
// scene.cpp -- Morpho : la scène 3D et son dessin (scene.h).
//
#include "scene.h"
#include <gpukit_abi.h>
#include <time.h>
#include <stdlib.h>

namespace mo {

static double nowMs ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (double) t.tv_sec * 1000.0 + (double) t.tv_nsec / 1e6;
}

void Scene::init (uint32_t seed)
{
	buildWorld (ground, decor, blades, seed);
	decor.flags = MF_CULL;
	skin.assign ((size_t) SKIN_W * SKIN_H, 0xFFFFFFFFu);
	shadowTex.assign (64 * 64, 0); makeShadowTex (shadowTex.data (), 64);
	grassTex.assign (128 * 128, 0); makeGrassTex (grassTex.data (), 128);
	tex[TEX_SKIN] = { skin.data (), SKIN_W, SKIN_H, -1, 0 };
	tex[TEX_SHADOW] = { shadowTex.data (), 64, 64, -1, 0 };
	tex[TEX_GRASS] = { grassTex.data (), 128, 128, -1, 0 };
	gpu = gk_info (0) == 1;
	if (gpu)
		for (int i = 0; i < TEX_N; i++)
		{
			long id = gk_texture (-1, (long) tex[i].px, tex[i].w, tex[i].h, tex[i].w);
			if (id < 0) { gpu = false; break; }
			tex[i].id = (int) id;
		}
	if (!gpu) for (int i = 0; i < TEX_N; i++) tex[i].id = -1;
	// (au processeur, la texture de l'herbe coûte plus que tout le reste : les couleurs des sommets suffisent)
	ground.tex = gpu ? TEX_GRASS : -1;
	cm.body.tex = TEX_SKIN; cm.body.flags = MF_CULL;
	cm.parts.flags = MF_CULL; cm.sheets.flags = 0; cm.alpha.flags = MF_ALPHA;
	props.flags = MF_CULL; propsSheets.flags = 0; propsAlpha.flags = MF_ALPHA;
	shadow.tex = TEX_SHADOW; shadow.flags = MF_ALPHA | MF_UNLIT | MF_NOFOG;
}

void Scene::setSkin (const uint32_t *px)
{
	memcpy (skin.data (), px, sizeof (uint32_t) * SKIN_W * SKIN_H);
	if (gpu && tex[TEX_SKIN].id >= 0)
		if (gk_texture_rect (tex[TEX_SKIN].id, 0, 0, SKIN_W, SKIN_H, (long) skin.data (), SKIN_W) < 0) gpu = false;
}

void Scene::draw (uint32_t *px, int w, int h, int stride, float t)
{
	if (w < 8 || h < 8) return;
	// au processeur, une image trop lente passe à demi-taille (et revient quand c'est possible)
	int dw = w, dh = h, ds = stride;
	uint32_t *dst = px;
	if (!gpu && half)
	{
		dw = (w + 1) / 2; dh = (h + 1) / 2; ds = dw;
		low.resize ((size_t) dw * (size_t) dh);
		dst = low.data ();
	}
	cam.setup (dw, dh);
	drawSky (dst, dw, dh, ds, cam, light, t, clouds);
	// l'ombre sous la créature
	shadow.clear ();
	{
		Builder B (&shadow);
		float R = shadowR;
		V3 o = shadowAt; o.y = 0.006f;
		Col c (0, 0, 0);
		B.alpha = clampf (shadowK * (0.55f + 0.45f * (1 - light.night)), 0, 1);
		int a = B.add (o + V3 (-R, 0, -R), V3 (0, 1, 0), c, 0, 0), b = B.add (o + V3 (R, 0, -R), V3 (0, 1, 0), c, 1, 0);
		int cc = B.add (o + V3 (R, 0, R), V3 (0, 1, 0), c, 1, 1), d = B.add (o + V3 (-R, 0, R), V3 (0, 1, 0), c, 0, 1);
		B.tri (a, cc, b); B.tri (a, d, cc);
	}
	dl.clear ();
#ifdef MORPHO_HOST
	const char *skip = getenv ("MORPHO_SKIP") ? getenv ("MORPHO_SKIP") : "";
	if (!strstr (skip, "ground")) dl.add (ground, light, cam);
	dl.add (shadow, light, cam);
	if (!strstr (skip, "decor")) dl.add (decor, light, cam);
	if (!strstr (skip, "blades")) dl.add (blades, light, cam);
#else
	dl.add (ground, light, cam);
	dl.add (shadow, light, cam);
	dl.add (decor, light, cam);
	dl.add (blades, light, cam);
#endif
	dl.add (cm.body, light, cam);
	dl.add (cm.parts, light, cam);
	dl.add (cm.sheets, light, cam);
	dl.add (props, light, cam);
	dl.add (propsSheets, light, cam);
	dl.add (cm.alpha, light, cam);
	dl.add (propsAlpha, light, cam);
	GkFrame f = { dst, dw, dh, ds, 0, (unsigned) GK_F_KEEP, 0 };
	double t0 = nowMs ();
	long r = gk_draw ((long) &f, (long) dl.v.data (), (long) dl.v.size (), (long) dl.b.data (), (long) dl.b.size (), (long) tex);
	lastMs = (float) (nowMs () - t0);
	if (r == 0 && gpu) gpu = false;			// (le GPU s'est arrêté : le processeur désormais)
	if (!gpu && !fixedSize)
	{
		// (pleine taille au-dessus de 45 ms : demi-taille ; sous 9 ms à demi-taille : on revient)
		if (!half && lastMs > 45) half = 1;
		else if (half && lastMs < 9) half = 0;
	}
	if (dst != px)
	{
		// l'agrandissement : bilinéaire
		for (int y = 0; y < h; y++)
		{
			float fy = ((float) y + 0.5f) * 0.5f - 0.5f;
			int y0 = (int) floorf (fy); float ty = fy - (float) y0;
			int ya = clampi (y0, 0, dh - 1), yb = clampi (y0 + 1, 0, dh - 1);
			const uint32_t *ra = dst + (long) ya * ds, *rb = dst + (long) yb * ds;
			uint32_t *o = px + (long) y * stride;
			int wy = (int) (ty * 256);
			for (int x = 0; x < w; x++)
			{
				float fx = ((float) x + 0.5f) * 0.5f - 0.5f;
				int x0 = (int) floorf (fx); int wx = (int) ((fx - (float) x0) * 256);
				int xa = clampi (x0, 0, dw - 1), xb = clampi (x0 + 1, 0, dw - 1);
				uint32_t p00 = ra[xa], p01 = ra[xb], p10 = rb[xa], p11 = rb[xb];
				uint32_t res = 0;
				for (int sh = 0; sh <= 16; sh += 8)
				{
					int c00 = (int) ((p00 >> sh) & 255), c01 = (int) ((p01 >> sh) & 255), c10 = (int) ((p10 >> sh) & 255), c11 = (int) ((p11 >> sh) & 255);
					int top = c00 * (256 - wx) + c01 * wx, bot = c10 * (256 - wx) + c11 * wx;
					int v = (top * (256 - wy) + bot * wy) >> 16;
					res |= (uint32_t) v << sh;
				}
				o[x] = res;
			}
		}
	}
	cam.setup (w, h);		// (pour l'interface : les projections en pleine taille)
}

}	// namespace mo
