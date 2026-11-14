/*
 * av_yuv_x86.c -- Korin PC : la conversion YUV 4:2:0 -> RGB 32 bits de la bibliotheque av (av_yuv.c d'Onyx) en SSE2,
 * 16 pixels a la fois : le meme calcul que le C et que le NEON du Pi, bit pour bit (virgule fixe sur 16 bits, 6 bits
 * de fraction, sommes saturees ; la chrominance au plus proche). av_yuv.c est compile sous le nom
 * av__yuv_to_rgb_portable (tools/webkit/av.mk, AV_ARCH generic) : il fait le reste (images deja en 32 bits).
 * Mesure (i7-11800H, une image 1080p) : docs/WEBKIT.md, « Le compositeur sur le GPU du PC ».
 */
#include <emmintrin.h>
#include "av_int.h"

void av__yuv_to_rgb_portable (const struct av_frame *f, uint8_t *dst, int dst_stride, int pix);

struct coefs { int ymul, yoff, vr, ug, vg, ub; };
/* (les memes que av_yuv.c) */
static const struct coefs C601L = { 149, 1192, 102, 25, 52, 129 };
static const struct coefs C709L = { 149, 1192, 115, 14, 34, 135 };
static const struct coefs C601F = { 128, 0, 90, 22, 46, 113 };
static const struct coefs C709F = { 128, 0, 101, 12, 30, 119 };

static inline int sat16 (int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline uint8_t out8 (int v) { v = (v + 32) >> 6; return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v); }

static void row (const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *d, int w, const struct coefs *k, int bgr)
{
	const __m128i zero = _mm_setzero_si128 ();
	const __m128i c128 = _mm_set1_epi16 (128);
	const __m128i ymul = _mm_set1_epi16 ((short) k->ymul);
	const __m128i yoff = _mm_set1_epi16 ((short) k->yoff);
	const __m128i vr = _mm_set1_epi16 ((short) k->vr), ug = _mm_set1_epi16 ((short) k->ug);
	const __m128i vg = _mm_set1_epi16 ((short) k->vg), ub = _mm_set1_epi16 ((short) k->ub);
	const __m128i r32 = _mm_set1_epi16 (32);
	const __m128i alpha = _mm_set1_epi8 ((char) 255);
	int x;

	for (x = 0; x + 16 <= w; x += 16) {
		__m128i yv = _mm_loadu_si128 ((const __m128i *) (y + x));
		__m128i uu = _mm_sub_epi16 (_mm_unpacklo_epi8 (_mm_loadl_epi64 ((const __m128i *) (u + (x >> 1))), zero), c128);
		__m128i vv = _mm_sub_epi16 (_mm_unpacklo_epi8 (_mm_loadl_epi64 ((const __m128i *) (v + (x >> 1))), zero), c128);
		__m128i rv = _mm_mullo_epi16 (vv, vr);
		__m128i guv = _mm_add_epi16 (_mm_mullo_epi16 (uu, ug), _mm_mullo_epi16 (vv, vg));
		__m128i bu = _mm_mullo_epi16 (uu, ub);
		/* y * ymul >> 1 (non signe : y * 149 < 65536), - yoff */
		__m128i ylo = _mm_sub_epi16 (_mm_srli_epi16 (_mm_mullo_epi16 (_mm_unpacklo_epi8 (yv, zero), ymul), 1), yoff);
		__m128i yhi = _mm_sub_epi16 (_mm_srli_epi16 (_mm_mullo_epi16 (_mm_unpackhi_epi8 (yv, zero), ymul), 1), yoff);
		/* chaque echantillon de chrominance pour deux pixels */
		__m128i rlo = _mm_unpacklo_epi16 (rv, rv), rhi = _mm_unpackhi_epi16 (rv, rv);
		__m128i glo = _mm_unpacklo_epi16 (guv, guv), ghi = _mm_unpackhi_epi16 (guv, guv);
		__m128i blo = _mm_unpacklo_epi16 (bu, bu), bhi = _mm_unpackhi_epi16 (bu, bu);
		/* (v + 32) >> 6, sature en 0..255 ; l'addition saturee de 32 ne change rien a ce qui sature deja */
#define OUT(a) _mm_srai_epi16 (_mm_adds_epi16 ((a), r32), 6)
		__m128i R = _mm_packus_epi16 (OUT (_mm_adds_epi16 (ylo, rlo)), OUT (_mm_adds_epi16 (yhi, rhi)));
		__m128i G = _mm_packus_epi16 (OUT (_mm_subs_epi16 (ylo, glo)), OUT (_mm_subs_epi16 (yhi, ghi)));
		__m128i B = _mm_packus_epi16 (OUT (_mm_adds_epi16 (ylo, blo)), OUT (_mm_adds_epi16 (yhi, bhi)));
#undef OUT
		__m128i c0 = bgr ? B : R, c2 = bgr ? R : B;
		__m128i lo01 = _mm_unpacklo_epi8 (c0, G), hi01 = _mm_unpackhi_epi8 (c0, G);
		__m128i lo23 = _mm_unpacklo_epi8 (c2, alpha), hi23 = _mm_unpackhi_epi8 (c2, alpha);
		_mm_storeu_si128 ((__m128i *) (d + 4 * x), _mm_unpacklo_epi16 (lo01, lo23));
		_mm_storeu_si128 ((__m128i *) (d + 4 * x + 16), _mm_unpackhi_epi16 (lo01, lo23));
		_mm_storeu_si128 ((__m128i *) (d + 4 * x + 32), _mm_unpacklo_epi16 (hi01, hi23));
		_mm_storeu_si128 ((__m128i *) (d + 4 * x + 48), _mm_unpackhi_epi16 (hi01, hi23));
	}
	for (; x < w; x++) {
		int yy = ((y[x] * k->ymul) >> 1) - k->yoff;
		int uu = u[x >> 1] - 128, vv = v[x >> 1] - 128;
		int r = sat16 (yy + k->vr * vv), g = sat16 (yy - (k->ug * uu + k->vg * vv)), b = sat16 (yy + k->ub * uu);
		uint8_t *o = d + 4 * x;
		o[bgr ? 2 : 0] = out8 (r);
		o[1] = out8 (g);
		o[bgr ? 0 : 2] = out8 (b);
		o[3] = 255;
	}
}

void av_yuv_to_rgb (const struct av_frame *f, uint8_t *dst, int dst_stride, int pix)
{
	if (f->pix != AV_PIX_I420) {
		av__yuv_to_rgb_portable (f, dst, dst_stride, pix);
		return;
	}
	const struct coefs *k = f->colorspace == AV_CS_BT709 ? (f->full_range ? &C709F : &C709L) : (f->full_range ? &C601F : &C601L);
	for (int r = 0; r < f->height; r++)
		row (f->plane[0] + (size_t) r * f->stride[0], f->plane[1] + (size_t) (r >> 1) * f->stride[1],
		     f->plane[2] + (size_t) (r >> 1) * f->stride[2], dst + (size_t) r * dst_stride, f->width, k, pix == AV_PIX_BGRA);
}
