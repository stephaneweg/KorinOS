/*
 * av_video_armpi.c -- le recours des codecs absents de la bibliotheque av (docs/WEBKIT.md, « Son et video »).
 *
 * La bibliotheque av d'Onyx (src/extra/onyx-libs/av) cherche un decodeur dans sa table (av_codec.c :
 * VP8 et VP9 par libvpx, AV1 par dav1d, Opus, FLAC, MP3, PCM -- tools/webkit/av.mk les lie dans
 * libonyxav.a) puis, pour un codec qu'elle n'a pas (H.264, AAC, Vorbis...), appelle av__stub_of (codec)
 * (dans Onyx : les decodeurs factices des essais, av_stub.c, qui n'est pas compile ici). Elle rend NULL :
 * une piste de ce codec n'a pas de decodeur et canPlayType / isTypeSupported repondent non pour elle
 * (av_type_supported). Un decodeur d'ailleurs (MediaKit, un materiel) se brancherait ici : une
 * struct av_codec_impl par codec (open / send / receive / flush / close, av_int.h).
 */
#include "av_int.h"

const struct av_codec_impl *av__stub_of (int codec)
{
	(void) codec;
	return 0;
}

/* (av_stub.c d'Onyx en fournit aussi l'interrupteur des essais) */
void av_codec_enable_stubs (void)
{
}
