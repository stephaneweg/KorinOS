/*
 * av_threads_pc.c -- Korin PC : les decodeurs video de la bibliotheque av sur plusieurs fils (docs/WEBKIT.md,
 * « Le compositeur sur le GPU du PC »). av_vpx.c et av_dav1d.c d'Onyx ouvrent leur decodeur sur un seul fil (le Pi :
 * ses fils partagent un coeur) ; ici l'edition de liens les fait passer par ces deux fonctions
 * (-Wl,--wrap=vpx_codec_dec_init_ver -Wl,--wrap=dav1d_open, tools/webkit/build-jet.sh), qui demandent plus de fils :
 * libvpx decode les colonnes de tuiles d'une image VP9 en parallele (les videos de YouTube en ont 2 a 4 des 720p),
 * dav1d ses tuiles et ses filtres. Toujours une image a la fois (max_frame_delay 1 : av_dav1d.c compte dessus).
 * KORIN_VIDEO_THREADS : le nombre (1 : comme le Pi).
 */
#include <stdlib.h>
#include "vpx/vpx_decoder.h"
#include "dav1d/dav1d.h"

static int threads (void)
{
	const char *e = getenv ("KORIN_VIDEO_THREADS");
	int n = e ? atoi (e) : 4;
	return n < 1 ? 1 : n > 8 ? 8 : n;
}

vpx_codec_err_t __real_vpx_codec_dec_init_ver (vpx_codec_ctx_t *ctx, vpx_codec_iface_t *iface,
					      const vpx_codec_dec_cfg_t *cfg, vpx_codec_flags_t flags, int ver);

vpx_codec_err_t __wrap_vpx_codec_dec_init_ver (vpx_codec_ctx_t *ctx, vpx_codec_iface_t *iface,
					      const vpx_codec_dec_cfg_t *cfg, vpx_codec_flags_t flags, int ver)
{
	vpx_codec_dec_cfg_t c = { 0 };
	if (cfg)
		c = *cfg;
	c.threads = (unsigned) threads ();
	return __real_vpx_codec_dec_init_ver (ctx, iface, &c, flags, ver);
}

int __real_dav1d_open (Dav1dContext **c_out, const Dav1dSettings *s);

int __wrap_dav1d_open (Dav1dContext **c_out, const Dav1dSettings *s)
{
	Dav1dSettings t = *s;
	t.n_threads = threads ();
	t.max_frame_delay = 1;
	return __real_dav1d_open (c_out, &t);
}
