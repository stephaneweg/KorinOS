/*
 * av_video_armpi.c -- LE point de branchement de la video du moteur media de WebKit sur ARMPI
 * (docs/WEBKIT.md, « Video : le point de bascule vers MediaKit »).
 *
 * La bibliotheque av d'Onyx (src/extra/onyx-libs/av) cherche un decodeur dans sa table (av_codec.c :
 * ici FLAC, MP3, Opus, PCM -- le son ; libvpx et dav1d ne sont pas lies) puis, pour un codec qu'elle
 * n'a pas, appelle av__stub_of (codec) (dans Onyx : les decodeurs factices des essais, av_stub.c,
 * qui n'est pas compile ici). C'est cette fonction, et elle seule, qui donnera les decodeurs video
 * de MediaKit (VP8, VP9, AV1 : un kit C precompile, autre chantier) : une struct av_codec_impl par
 * codec (open / send / receive / flush / close, av_int.h) qui appelle MediaKit. Tant qu'elle rend
 * NULL, une piste video n'a pas de decodeur : canPlayType / isTypeSupported repondent non pour la
 * video (av_type_supported), une page qui en joue quand meme a le son et une image vide.
 */
#include "av_int.h"

const struct av_codec_impl *av__stub_of (int codec)
{
	(void) codec;
	return 0;			/* (MediaKit : ses decodeurs VP8 / VP9 / AV1 ici) */
}

/* (av_stub.c d'Onyx en fournit aussi l'interrupteur des essais) */
void av_codec_enable_stubs (void)
{
}
