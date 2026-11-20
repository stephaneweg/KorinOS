/* avcenc_stubs.c -- ce que les conteneurs d'av prennent à av_codec.c (qui amènerait tous les décodeurs),
   pour l'essai avcenc_test (libavcodec y décode) : sans effet */
#include "av_int.h"

void av__ff_identify(struct av_track *t, int fmt, uint32_t fourcc, int esds_oti)
{
	(void) t;
	(void) fmt;
	(void) fourcc;
	(void) esds_oti;
}

void av__codec_string(struct av_track *t)
{
	(void) t;
}
