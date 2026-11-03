/*
 * mediakit.h - MediaKit pour les programmes C / C++ d'ARMPI (docs/MEDIAKIT.md).
 * La meme table que mediakit.abi (assembleur) et tools/mediakit/mediakit.S (le kit) ;
 * ajout seul. Le kit se charge par lib_open (SYS_LIB_OPEN 0x64) / f_kit_load d'AppKit :
 * mk_kit_open () ci-dessous remplit les pointeurs de fonctions.
 *
 *	struct mk_api mk;
 *	if (mk_kit_open (&mk, kit_open_fn) != 0) ... MediaKit absent ...
 *	long h = mk.open ("SD0:/home/video/x.webm");
 *	struct mk_info info; mk.info (h, &info);
 *	struct mk_frame f;
 *	while (mk.video_next (h, &f) == 1) mk.video_convert (h, pixels, pitch, w, hh);
 *
 * Une poignee ne sert qu'a un thread a la fois (plusieurs poignees sur plusieurs threads :
 * permis). Les chaines et les donnees rendues (mk_track.extra, mk_packet.data,
 * mk_error_text) appartiennent au kit.
 */
#ifndef ARMPI_MEDIAKIT_H
#define ARMPI_MEDIAKIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MK_MIN_VERSION	15		/* fonctions de cette table */

/* erreurs (negatives) */
#define MK_E_NOENT	(-2)
#define MK_E_IO		(-5)
#define MK_E_BADF	(-9)
#define MK_E_NOMEM	(-12)
#define MK_E_INVAL	(-22)
#define MK_E_SPIPE	(-29)		/* recherche impossible (pas d'index) */
#define MK_E_NODATA	(-61)		/* pas de piste de ce genre */
#define MK_E_BADMSG	(-74)		/* donnees illisibles */
#define MK_E_UNSUP	(-95)		/* format ou codec non pris (son : pour AudioKit) */

/* codecs (AV_C_* de la bibliotheque av d'Onyx) */
enum { MK_C_NONE = 0, MK_C_VP8, MK_C_VP9, MK_C_AV1, MK_C_H264, MK_C_HEVC, MK_C_MJPEG, MK_C_I420,
	MK_C_OPUS = 64, MK_C_VORBIS, MK_C_AAC, MK_C_MP3, MK_C_FLAC, MK_C_PCM_S16LE };
enum { MK_VIDEO = 1, MK_AUDIO = 2 };

struct mk_info {
	int64_t duration;		/* us, 0 inconnue */
	int64_t ntracks;
	int64_t vtrack, atrack;		/* pistes choisies (-1 : aucune) */
	int64_t width, height;		/* taille d'affichage */
	int64_t fps1000;		/* images/s x 1000 */
	int64_t rate, channels;
	int64_t vcodec, acodec;
	int64_t vok, aok;		/* 1 : decode par MediaKit */
	char container[16], vname[16], aname[16];
};

struct mk_track {
	int64_t kind, codec, width, height, rate, channels, duration;
	const uint8_t *extra;		/* donnees d'initialisation du codec */
	int64_t extra_len;
	int64_t supported;
	char name[16];
	char codec_id[32];		/* nom du conteneur (« V_VP9 », « avc1 ») */
	int64_t delay;			/* us (pre-saut d'Opus) */
};

struct mk_frame { int64_t pts, dur, width, height, decode_us; };
struct mk_packet { const uint8_t *data; int64_t size, pts, dur, key, codec; };
struct mk_stats { int64_t vframes, vdecode_us, aframes, adecode_us, dropped, heap_big; };

/* la table, dans l'ordre de mediakit.abi */
struct mk_api {
	long (*version) (void);
	long (*open) (const char *path);
	long (*close) (long h);
	long (*info) (long h, struct mk_info *o);
	long (*track) (long h, long n, struct mk_track *o);
	long (*select) (long h, long vtrack, long atrack);
	long (*video_next) (long h, struct mk_frame *f);
	long (*video_convert) (long h, void *xrgb, long pitch, long w, long hh);
	long (*video_read) (long h, void *xrgb, long pitch, long w, long hh, struct mk_frame *f);
	long (*audio_open) (long h, long rate);
	long (*audio_read) (long h, int16_t *s16_stereo, long frames, int64_t *pts);
	long (*audio_packet) (long h, struct mk_packet *p);
	long (*seek) (long h, int64_t us);
	long (*stats) (long h, struct mk_stats *o);
	const char *(*error_text) (long err);
};

/* table d'un kit d'ARMPI : 'AKIT', nombre, decalages depuis la table */
struct mk_kit_table { uint32_t magic, count, off[]; };

/* kit_open (nom, version minimale) -> table du kit ou 0 : celle du programme (libposix :
 * __armpi_kit_open, qui demande a AppKit de lire SD0:/lib/<nom>.so) ; -> 0 ou MK_E_NOENT */
static inline int mk_kit_open (struct mk_api *api, const void *(*kit_open) (const char *, unsigned))
{
	const struct mk_kit_table *t = (const struct mk_kit_table *) kit_open ("mediakit", MK_MIN_VERSION);
	void **fn = (void **) api;
	unsigned i;
	if (!t || t->count < MK_MIN_VERSION)
		return MK_E_NOENT;
	for (i = 0; i < MK_MIN_VERSION; i++)
		fn[i] = (char *) t + t->off[i];
	return 0;
}

#ifdef __cplusplus
}
#endif
#endif
