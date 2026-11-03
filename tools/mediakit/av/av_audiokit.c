/*
 * av_audiokit.c -- ARMPI (MediaKit) : les codecs audio de la bibliotheque av passent par
 * l'API par paquets d'AudioKit (ak_codec_*, docs/KITS.md) : Opus, Vorbis, MP3, FLAC decodes
 * en assembleur dans le kit du systeme, pas dans MediaKit (decision de l'utilisateur).
 * Remplace av_opus.c (libopus, le secours d'avant).
 *
 * AudioKit est trouve par lib_open (deja charge par init2) ; ses fonctions suivent la
 * convention ordinaire du systeme (x0-x7, x19-x28 gardes) mais pas d8-d15 : l'appel passe par
 * ak_call, qui les donne pour perdus au compilateur.
 *
 * Donnees d'initialisation : Matroska les donne telles qu'AudioKit les veut (OpusHead, Vorbis
 * en lacage Xiph, fLaC + blocs) ; MP4 : dOps (Opus sans « OpusHead », gros-boutien) refait
 * en OpusHead, dfLa (version et drapeaux puis les blocs) sans ses 4 premiers octets.
 * Sortie : flottant entrelace ; Opus et Vorbis de 3 a 8 canaux remis dans l'ordre WAVE
 * (celui d'av_frame) ; Opus : l'avance (pre-skip) retiree au debut, DiscardPadding a la fin.
 */
#ifdef MK_ARMPI
#include "av_int.h"
#include "../mk_sys.h"

#define AKF_FLAC	2
#define AKF_MP3		3
#define AKF_VORBIS	4
#define AKF_OPUS	5
#define AKC_F32		1
#define AKI_RATE	1			/* (en mots de 64 bits) */
#define AKI_CHANNELS	2

/* rangs dans la table d'AudioKit (src/kits/audiokit/audiokit.abi) */
enum { AK_OPEN = 34, AK_DECODE, AK_RESET, AK_INFO, AK_CLOSE, AK_COUNT };

struct kit_table { uint32_t magic, count, off[]; };
static void *akfn[AK_COUNT];

static int audiokit(void)
{
	if (akfn[AK_OPEN]) return 1;
	long r = mk_svc(MK_SYS_LIB_OPEN, (long)"audiokit", 0, 0, 0);
	if (r <= 0) return 0;
	const struct kit_table *k = (const struct kit_table *)r;
	if (k->count < AK_COUNT) return 0;
	for (int i = AK_OPEN; i < AK_COUNT; i++) akfn[i] = (char *)k + k->off[i];
	return 1;
}

/* une fonction d'AudioKit, 6 arguments au plus */
static long ak_call(int fn, long a, long b, long c, long d, long e, long f)
{
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	register void *x9 __asm__("x9") = akfn[fn];
	__asm__ volatile("blr x9"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x9)
		:
		: "x6", "x7", "x8", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",
		  "x30", "memory", "cc",
		  "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12",
		  "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24",
		  "v25", "v26", "v27", "v28", "v29", "v30", "v31");
	return x0;
}

struct akc {
	long h;				/* poignee d'AudioKit */
	int codec, channels, rate, max, skip;
	float *tmp, *pcm;
	struct av_frame f;
	int have;
};

/* Vorbis -> WAVE (L R C LFE BL BR SL SR), comme av_opus.c */
static const unsigned char reorder[9][8] = {
	{ 0 }, { 0 }, { 0, 1 }, { 0, 2, 1 }, { 0, 1, 2, 3 }, { 0, 2, 1, 3, 4 },
	{ 0, 2, 1, 5, 3, 4 }, { 0, 2, 1, 6, 5, 3, 4 }, { 0, 2, 1, 7, 5, 6, 3, 4 },
};

static void *akc_open(const struct av_track *t)
{
	const uint8_t *e = t->extra;
	size_t n = t->extra_len;
	uint8_t head[19 + 2 + 8];
	int fmt, ch = t->channels > 0 ? t->channels : 2, preskip = 0;
	struct akc *c;
	long info[8];

	if (!audiokit())
		return NULL;
	switch (t->codec) {
	case AV_C_OPUS:
		fmt = AKF_OPUS;
		if (e != NULL && n >= 11 && memcmp(e, "OpusHead", 8) != 0) {
			/* dOps -> OpusHead */
			size_t m = 19;
			memcpy(head, "OpusHead", 8);
			head[8] = 1;
			head[9] = e[1];
			head[10] = e[3]; head[11] = e[2];			/* pre-skip */
			head[12] = e[7]; head[13] = e[6]; head[14] = e[5]; head[15] = e[4];
			head[16] = e[9]; head[17] = e[8];			/* gain */
			head[18] = e[10];					/* famille */
			if (e[10] != 0 && n >= 13 + (size_t)e[1] && e[1] <= 8) {
				memcpy(head + 19, e + 11, 2 + (size_t)e[1]);
				m += 2 + e[1];
			}
			e = head;
			n = m;
		}
		if (e != NULL && n >= 19) {
			ch = e[9];
			preskip = (int)av_rl16(e + 10);
		}
		break;
	case AV_C_VORBIS:
		fmt = AKF_VORBIS;
		break;
	case AV_C_MP3:
		fmt = AKF_MP3;
		e = NULL;
		n = 0;
		break;
	case AV_C_FLAC:
		fmt = AKF_FLAC;
		if (e != NULL && n >= 8 && e[0] == 0 && e[1] == 0 && e[2] == 0 && e[3] == 0) {
			e += 4;			/* dfLa : version et drapeaux */
			n -= 4;
		}
		break;
	default:
		return NULL;
	}
	if (ch < 1 || ch > 8)
		return NULL;
	c = (struct akc *)calloc(1, sizeof *c);
	if (c == NULL)
		return NULL;
	c->codec = t->codec;
	c->max = fmt == AKF_FLAC ? 65535 : fmt == AKF_OPUS ? 5760 : 8192;
	c->h = ak_call(AK_OPEN, fmt, t->rate, ch, (long)e, (long)n, 0);
	if (c->h <= 0)
		goto fail;
	c->channels = ch;
	c->rate = fmt == AKF_OPUS ? 48000 : t->rate;
	if (ak_call(AK_INFO, c->h, (long)info, 0, 0, 0, 0) == 0) {
		if (info[AKI_CHANNELS] > 0) c->channels = (int)info[AKI_CHANNELS];
		if (info[AKI_RATE] > 0) c->rate = (int)info[AKI_RATE];
	}
	if (c->channels > 8)
		goto fail;
	c->skip = preskip;
	c->tmp = (float *)malloc((size_t)c->max * 8 * sizeof(float));
	c->pcm = (float *)malloc((size_t)c->max * 8 * sizeof(float));
	if (c->tmp == NULL || c->pcm == NULL)
		goto fail;
	return c;
fail:
	if (c->h > 0) ak_call(AK_CLOSE, c->h, 0, 0, 0, 0, 0);
	free(c->tmp);
	free(c->pcm);
	free(c);
	return NULL;
}

static int akc_send(void *cv, const struct av_packet *p)
{
	struct akc *c = (struct akc *)cv;
	long info[8];
	int n, k, i, skip, ch;
	float *src;

	if (p == NULL)
		return AV_OK;
	n = (int)ak_call(AK_DECODE, c->h, (long)p->data, (long)p->size, (long)c->tmp, c->max, AKC_F32);
	if (n < 0)
		return AV_ERR;
	if (n == 0)
		return AV_OK;		/* paquet sans son (premier paquet Vorbis...) */
	/* MP3, FLAC : frequence et canaux de la trame */
	if (c->codec == AV_C_MP3 || c->codec == AV_C_FLAC)
		if (ak_call(AK_INFO, c->h, (long)info, 0, 0, 0, 0) == 0 && info[AKI_CHANNELS] > 0 &&
		    info[AKI_CHANNELS] <= 8 && info[AKI_RATE] > 0) {
			c->channels = (int)info[AKI_CHANNELS];
			c->rate = (int)info[AKI_RATE];
		}
	ch = c->channels;
	skip = c->skip < n ? c->skip : n;
	c->skip -= skip;
	if (p->discard_end > 0) {
		int drop = (int)(p->discard_end * c->rate / AV_US);
		n = drop < n ? n - drop : 0;
	}
	if (n <= skip)
		return AV_OK;
	src = c->tmp + (size_t)skip * ch;
	if (ch >= 3 && (c->codec == AV_C_OPUS || c->codec == AV_C_VORBIS)) {
		for (i = 0; i < n - skip; i++)
			for (k = 0; k < ch; k++)
				c->pcm[i * ch + k] = src[i * ch + reorder[ch][k]];
		src = c->pcm;
	}
	memset(&c->f, 0, sizeof c->f);
	c->f.kind = AV_AUDIO;
	c->f.pts = p->pts + (av_us)skip * AV_US / c->rate;
	c->f.rate = c->rate;
	c->f.channels = ch;
	c->f.samples = n - skip;
	c->f.dur = (av_us)(n - skip) * AV_US / c->rate;
	c->f.pcm = src;
	c->have = 1;
	return AV_OK;
}

static int akc_receive(void *cv, struct av_frame *f)
{
	struct akc *c = (struct akc *)cv;

	if (!c->have)
		return AV_AGAIN;
	c->have = 0;
	*f = c->f;
	return AV_OK;
}

static void akc_flush(void *cv)
{
	struct akc *c = (struct akc *)cv;
	ak_call(AK_RESET, c->h, 0, 0, 0, 0, 0);
	c->have = 0;
}

static void akc_close(void *cv)
{
	struct akc *c = (struct akc *)cv;
	ak_call(AK_CLOSE, c->h, 0, 0, 0, 0, 0);
	free(c->tmp);
	free(c->pcm);
	free(c);
}

const struct av_codec_impl av_ak_opus_codec = {
	AV_C_OPUS, AV_AUDIO, "opus", "AudioKit", 0, 0, 0,
	akc_open, akc_send, akc_receive, akc_flush, akc_close, NULL
};
const struct av_codec_impl av_ak_vorbis_codec = {
	AV_C_VORBIS, AV_AUDIO, "vorbis", "AudioKit", 0, 0, 0,
	akc_open, akc_send, akc_receive, akc_flush, akc_close, NULL
};
const struct av_codec_impl av_ak_mp3_codec = {
	AV_C_MP3, AV_AUDIO, "mp3", "AudioKit", 0, 0, 0,
	akc_open, akc_send, akc_receive, akc_flush, akc_close, NULL
};
const struct av_codec_impl av_ak_flac_codec = {
	AV_C_FLAC, AV_AUDIO, "flac", "AudioKit", 0, 0, 0,
	akc_open, akc_send, akc_receive, akc_flush, akc_close, NULL
};
#endif
