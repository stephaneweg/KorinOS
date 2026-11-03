/*
 * skgltest -- Skia Ganesh sur OpenGL ES (docs/MESA.md) : la meme scene dessinee par le processeur et par
 * le GPU, pixels compares, temps des deux.
 *     skgltest            le GPU
 *     skgltest --dry      sans GPU : le contexte, et tout le GLSL de Skia compile par Mesa ; pas d'image
 *     skgltest --win [--dry]  dans une fenetre : le temps d'une image AFFICHEE, recopie du processeur contre
 *                         rendu direct du GPU dans la surface d'Elegant
 *     skgltest --big [L H] [--dry]  de meme avec une scene chargee a la maniere d'une interface Avalonia,
 *                         760 x 520 par defaut
 * Sans GPU et sans --dry : le dit et rend 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradientShader.h"
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/gl/GrGLAssembleInterface.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "armgl.h"
extern "C" {
#include <uikit.h>
}

#define W 400
#define H 300
#define GR_GL_RGBA8 0x8058

static struct ui_vars ui;

/* ui_surface_pixels rend quatre valeurs : les relais generes n'en rendent qu'une */
static uint32_t *surface_pixels (long s, long *pitch)
{
	register long x0 __asm__ ("x0") = s;
	register long x1 __asm__ ("x1");
	__asm__ volatile ("bl ui_surface_pixels" : "+r" (x0), "=r" (x1) : : "x2", "x3", "x16", "x17", "x30", "memory", "cc");
	*pitch = x1;
	return (uint32_t *) x0;
}

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static GrGLFuncPtr get_proc (void *, const char *name)
{
	return (GrGLFuncPtr) armgl_proc (name);
}

/* ce qu'une interface d'Avalonia demande : aplats, arrondis, degrades, chemins, images, texte, opacite */
static void scene (SkCanvas *c, const sk_sp<SkImage> &image, const SkFont *font, float t)
{
	SkPaint p;
	c->clear (SkColorSetRGB (0xF2, 0xF4, 0xF7));

	SkPoint pts[2] = { { 0, 0 }, { W, 0 } };
	SkColor cols[3] = { SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE };
	p.setShader (SkGradientShader::MakeLinear (pts, cols, nullptr, 3, SkTileMode::kClamp));
	c->drawRect (SkRect::MakeXYWH (0, 0, W, 40), p);
	p.setShader (nullptr);

	p.setAntiAlias (true);
	p.setColor (SkColorSetRGB (0x2F, 0x6F, 0xED));
	c->drawRRect (SkRRect::MakeRectXY (SkRect::MakeXYWH (20 + t, 60, 160, 70), 12, 12), p);

	p.setColor (SkColorSetARGB (0x80, 0xE0, 0x30, 0x30));
	c->drawCircle (170 + t, 110, 50, p);

	SkPath path;
	path.moveTo (250, 60);
	path.cubicTo (300, 40, 380, 120, 300, 140);
	path.lineTo (240, 130);
	path.close ();
	p.setColor (SkColorSetRGB (0x20, 0x90, 0x60));
	c->drawPath (path, p);

	p.setStyle (SkPaint::kStroke_Style);
	p.setStrokeWidth (3);
	p.setColor (SK_ColorBLACK);
	c->drawPath (path, p);
	p.setStyle (SkPaint::kFill_Style);

	c->drawImageRect (image, SkRect::MakeXYWH (20, 150, 96, 96), SkSamplingOptions (SkFilterMode::kLinear), nullptr);

	if (font) {
		p.setColor (SK_ColorBLACK);
		c->drawString ("Skia Ganesh sur le V3D", 130, 180, *font, p);
		c->drawString ("0123456789 abcdefghij", 130, 210, *font, p);
	}
}

/* une interface chargee : barre laterale, cartes avec ombre floue et degrade, images, texte, panneau
 * transparent, barre animee. Tout est redessine a chaque image. */
static void scene_big (SkCanvas *c, int w, int h, const sk_sp<SkImage> &image, const SkFont *font, float t)
{
	SkPaint p;
	p.setAntiAlias (true);
	c->clear (SkColorSetRGB (0xF2, 0xF4, 0xF7));

	/* barre laterale : 14 lignes, une surlignee */
	p.setColor (SkColorSetRGB (0xE3, 0xE8, 0xEF));
	c->drawRect (SkRect::MakeXYWH (0, 0, 150, h), p);
	SkFont small;
	if (font) {
		small = *font;
		small.setSize (13);
		small.setSubpixel (true);
	}
	for (int i = 0; i < 14; i++) {
		float y = 12 + i * 34;
		if (i == ((int) t / 8) % 14) {
			p.setColor (SkColorSetRGB (0x2F, 0x6F, 0xED));
			c->drawRRect (SkRRect::MakeRectXY (SkRect::MakeXYWH (8, y - 4, 134, 30), 6, 6), p);
		}
		if (font) {
			p.setColor (SK_ColorBLACK);
			c->drawString ("Element de la liste", 18, y + 16, small, p);
		}
	}

	/* les cartes */
	SkPaint shadow;
	shadow.setAntiAlias (true);
	shadow.setColor (SkColorSetARGB (0x50, 0, 0, 0));
	shadow.setMaskFilter (SkMaskFilter::MakeBlur (kNormal_SkBlurStyle, 6));
	int cols = (w - 170) / 200, rows = (h - 50) / 150;
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < cols; x++) {
			float cx = 166 + x * 200, cy = 16 + y * 150;
			SkRect r = SkRect::MakeXYWH (cx, cy, 188, 136);
			SkRRect rr = SkRRect::MakeRectXY (r, 10, 10);
			c->drawRRect (rr, shadow);
			p.setColor (SK_ColorWHITE);
			c->drawRRect (rr, p);

			SkPoint pts[2] = { { cx, cy }, { cx + 188, cy } };
			SkColor cols2[2] = { SkColorSetRGB (0x2F, 0x6F, 0xED), SkColorSetRGB (0x20, 0xB0, 0x90) };
			p.setShader (SkGradientShader::MakeLinear (pts, cols2, nullptr, 2, SkTileMode::kClamp));
			c->save ();
			c->clipRRect (rr, true);
			c->drawRect (SkRect::MakeXYWH (cx, cy, 188, 30), p);
			c->restore ();
			p.setShader (nullptr);

			c->drawImageRect (image, SkRect::MakeXYWH (cx + 10, cy + 40, 60, 60), SkSamplingOptions (SkFilterMode::kLinear), nullptr);
			if (font) {
				p.setColor (SK_ColorWHITE);
				c->drawString ("Titre de la carte", cx + 10, cy + 20, small, p);
				p.setColor (SkColorSetRGB (0x30, 0x34, 0x40));
				c->drawString ("Une ligne", cx + 80, cy + 56, small, p);
				c->drawString ("et une autre", cx + 80, cy + 76, small, p);
				c->drawString ("et encore", cx + 80, cy + 96, small, p);
			}
			p.setColor (SkColorSetRGB (0xD8, 0xDE, 0xE6));
			c->drawRRect (SkRRect::MakeRectXY (SkRect::MakeXYWH (cx + 10, cy + 114, 168, 8), 4, 4), p);
			p.setColor (SkColorSetRGB (0x2F, 0x6F, 0xED));
			float w2 = 20 + (float) ((int) (t * 3 + x * 30 + y * 50) % 148);
			c->drawRRect (SkRRect::MakeRectXY (SkRect::MakeXYWH (cx + 10, cy + 114, w2, 8), 4, 4), p);
		}

	/* un panneau transparent par-dessus, avec sa couche */
	SkPaint layer;
	layer.setAlphaf (0.85f);
	SkRect pr = SkRect::MakeXYWH (w - 260 - t, h - 90, 240, 70);
	c->saveLayer (&pr, &layer);
	p.setColor (SkColorSetRGB (0x20, 0x24, 0x30));
	c->drawRRect (SkRRect::MakeRectXY (pr, 12, 12), p);
	if (font) {
		p.setColor (SK_ColorWHITE);
		c->drawString ("Panneau transparent", pr.x () + 14, pr.y () + 28, small, p);
		c->drawString ("sur toute la scene", pr.x () + 14, pr.y () + 50, small, p);
	}
	c->restore ();
}

/* le chiffre qui compte : une image jusque dans la surface d'Elegant. Le processeur comme Avalonia aujourd'hui
 * (tampon prive, recopie, damage), puis le GPU directement dans la surface. */
static int bench_window (bool dry, const sk_sp<SkImage> &image, const SkFont *fp, sk_sp<GrDirectContext> gr, void *ctx,
			 int w, int h, bool big)
{
	const int N = dry ? 10 : 200, WARM = dry ? 2 : 20;
	SkImageInfo info = SkImageInfo::MakeN32Premul (w, h);
	long us = ui_surface (0, 0, w, h, WS_SF_XRGB8888), pitch = 0;
	if (!us) {
		printf ("skgltest : pas de surface. FAIL\n");
		return 1;
	}
	uint32_t *px = surface_pixels (us, &pitch);
	sk_sp<SkSurface> cs = SkSurfaces::Raster (info);
	double tdraw = 0, tall = 0;
	for (int i = -WARM; i < N; i++) {
		double a = now ();
		if (big)
			scene_big (cs->getCanvas (), w, h, image, fp, (float) (i & 255));
		else
			scene (cs->getCanvas (), image, fp, (float) (i & 31));
		double b = now ();
		cs->readPixels (info, px, pitch, 0, 0);
		ui_surface_damage (us, 0, 0, 0, 0);
		ui_event (0);
		if (i >= 0) {
			tdraw += b - a;
			tall += now () - a;
		}
	}
	tdraw /= N;
	tall /= N;

	void *ws = armgl_window (ui.win, 0, 0, w, h);
	if (!ws || !armgl_make_current (ctx, ws)) {
		printf ("skgltest : pas de surface GL dans la fenetre. FAIL\n");
		return 1;
	}
	GrGLFramebufferInfo fb;
	fb.fFBOID = 0;
	fb.fFormat = GR_GL_RGBA8;
	GrBackendRenderTarget rt = GrBackendRenderTargets::MakeGL (w, h, 0, 8, fb);
	sk_sp<SkSurface> gs = SkSurfaces::WrapBackendRenderTarget (gr.get (), rt, kBottomLeft_GrSurfaceOrigin,
								   kRGBA_8888_SkColorType, nullptr, nullptr);
	if (!gs) {
		printf ("skgltest : WrapBackendRenderTarget a echoue. FAIL\n");
		return 1;
	}
	double t0 = 0, first = 0;
	for (int i = -WARM; i < N; i++) {
		if (i == 0) {
			armgl_times_reset (ws);
			t0 = now ();
		}
		double a = now ();
		if (big)
			scene_big (gs->getCanvas (), w, h, image, fp, (float) (i & 255));
		else
			scene (gs->getCanvas (), image, fp, (float) (i & 31));
		gr->flushAndSubmit (gs.get (), GrSyncCpu::kNo);
		if (armgl_swap (ws)) {
			printf ("skgltest : image refusee. FAIL\n");
			return 1;
		}
		ui_event (0);
		if (i == -WARM)
			first = now () - a;
	}
	double tg = (now () - t0) / N, f, c, at;
	armgl_times (ws, &f, &c, &at);
	printf ("skgltest : %d x %d, premiere image GPU %.0f ms (shaders compris)\n", w, h, first);
	printf ("skgltest : image affichee : GPU %.2f ms, processeur %.2f ms\n", tg, tall);
	printf ("skgltest : GPU : programme %.2f ms, attente GPU %.2f, cache %.2f, rattachement %.2f\n", tg - f - c - at, f, c, at);
	printf ("skgltest : processeur : dessin %.2f ms, recopie et attente d'Elegant %.2f\n", tdraw, tall - tdraw);
	printf ("skgltest : fenetre : PASS\n");
	return 0;
}

int main (int argc, char **argv)
{
	bool dry = false, win = false, big = false;
	int bw = 760, bh = 520;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--dry")) dry = true;
		if (!strcmp (argv[i], "--win")) win = true;
		if (!strcmp (argv[i], "--big")) {
			win = big = true;
			if (i + 2 < argc && argv[i + 1][0] != '-') {
				bw = atoi (argv[i + 1]);
				bh = atoi (argv[i + 2]);
				i += 2;
			}
		}
	}
	double t0 = now ();
	int mode = armgl_init (dry ? ARMGL_F_DRY : 0);
	printf ("skgltest : armgl_init %d\n", mode);
	if (mode == ARMGL_NONE) {
		printf ("skgltest : pas de GPU : repli sur le processeur. PASS\n");
		return 0;
	}
	void *ctx = armgl_context (3, 0, NULL);
	void *surf = armgl_surface (W, H);
	if (!ctx || !surf || !armgl_make_current (ctx, surf)) {
		printf ("skgltest : pas de contexte OpenGL ES\n");
		return 1;
	}
	sk_sp<const GrGLInterface> gl = GrGLMakeAssembledInterface (nullptr, get_proc);
	if (!gl) {
		printf ("skgltest : GrGLMakeAssembledInterface a echoue. FAIL\n");
		return 1;
	}
	sk_sp<GrDirectContext> gr = GrDirectContexts::MakeGL (gl);
	if (!gr) {
		printf ("skgltest : GrDirectContexts::MakeGL a echoue. FAIL\n");
		return 1;
	}
	printf ("skgltest : GrDirectContext pret en %.0f ms, cible maxi %d, texture maxi %d\n", now () - t0,
		gr->maxRenderTargetSize (), gr->maxTextureSize ());

	SkImageInfo info = SkImageInfo::MakeN32Premul (W, H);
	sk_sp<SkSurface> gs = SkSurfaces::RenderTarget (gr.get (), skgpu::Budgeted::kNo, info);
	sk_sp<SkSurface> cs = SkSurfaces::Raster (info);
	if (!gs || !cs) {
		printf ("skgltest : pas de surface. FAIL\n");
		return 1;
	}

	/* une image a mettre en texture : un damier */
	SkBitmap bm;
	bm.allocN32Pixels (32, 32);
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 32; x++)
			*bm.getAddr32 (x, y) = ((x ^ y) & 8) ? 0xFFFFFFFF : 0xFF3050A0;
	bm.setImmutable ();
	sk_sp<SkImage> image = bm.asImage ();

	sk_sp<SkFontMgr> fm = SkFontMgr::RefDefault ();
	sk_sp<SkTypeface> tf = fm ? fm->legacyMakeTypeface (nullptr, SkFontStyle ()) : nullptr;
	if (!tf)
		tf = SkTypeface::MakeFromFile ("SD0:/fonts/DejaVuSans.ttf");
	SkFont font (tf, 18);
	printf ("skgltest : police %s\n", tf ? "chargee" : "absente : pas de texte");
	const SkFont *fp = tf ? &font : nullptr;

	if (win) {
		int ww = big ? bw : W, wh = big ? bh : H;
		if (!uikit_open (&ui) || ui_window (ww, wh, (long) "skgltest") <= 0) {
			printf ("skgltest : pas de fenetre\n");
			return 1;
		}
		return bench_window (dry, image, fp, gr, ctx, ww, wh, big);
	}

	t0 = now ();
	scene (gs->getCanvas (), image, fp, 0);
	gr->flushAndSubmit (gs.get (), GrSyncCpu::kYes);
	armgl_finish ();
	printf ("skgltest : premiere image par Ganesh %.0f ms (shaders compris), %ld jobs\n", now () - t0, armgl_jobs ());

	if (mode == ARMGL_DRY) {
		if (armgl_jobs () < 1) {
			printf ("skgltest : aucun job. FAIL\n");
			return 1;
		}
		printf ("skgltest : a blanc : Skia et Mesa vont au bout. PASS\n");
		return 0;
	}

	scene (cs->getCanvas (), image, fp, 0);
	std::vector<uint32_t> a (W * H), b (W * H);
	if (!gs->readPixels (info, a.data (), W * 4, 0, 0) || !cs->readPixels (info, b.data (), W * 4, 0, 0)) {
		printf ("skgltest : lecture impossible. FAIL\n");
		return 1;
	}
	long over8 = 0, over32 = 0;
	for (int i = 0; i < W * H; i++) {
		int m = 0;
		for (int k = 0; k < 32; k += 8) {
			int d = abs ((int) (a[i] >> k & 255) - (int) (b[i] >> k & 255));
			m = d > m ? d : m;
		}
		over8 += m > 8;
		over32 += m > 32;
	}
	printf ("skgltest : %ld pixels sur %d a plus de 8 niveaux du processeur, %ld a plus de 32\n", over8, W * H, over32);

	FILE *f = fopen ("SD0:/skgltest.ppm", "wb");
	if (f) {
		fprintf (f, "P6\n%d %d\n255\n", W, H);
		for (int i = 0; i < W * H; i++) {
			unsigned char c[3] = { (unsigned char) (a[i] >> 16), (unsigned char) (a[i] >> 8), (unsigned char) a[i] };
			fwrite (c, 3, 1, f);
		}
		fclose (f);
		printf ("skgltest : SD0:/skgltest.ppm\n");
	}

	/* les temps : 200 images ou seules deux formes bougent */
	t0 = now ();
	for (int i = 0; i < 200; i++) {
		scene (gs->getCanvas (), image, fp, (float) (i % 40));
		gr->flushAndSubmit (gs.get (), GrSyncCpu::kYes);
	}
	armgl_finish ();
	double tg = (now () - t0) / 200;
	t0 = now ();
	for (int i = 0; i < 200; i++)
		scene (cs->getCanvas (), image, fp, (float) (i % 40));
	double tc = (now () - t0) / 200;
	printf ("skgltest : par image : GPU %.2f ms, processeur %.2f ms\n", tg, tc);

	/* tolere les bords antialiases et le texte, pas une image fausse */
	bool ok = over32 * 100 < W * H;
	printf ("skgltest : %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
