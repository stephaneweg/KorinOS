/* mediakit - la video en kit d'ARMPI (docs/KITS.md, docs/MEDIAKIT.md) : la
 * bibliotheque av d'Onyx (user/Libs/av : demultiplexeurs WebM / Matroska, MP4,
 * RIFF ; YUV -> RGB ; reechantillonnage), libvpx 1.15.2 (VP8, VP9), dav1d 1.5.1
 * (AV1) ; le son par l'API par paquets d'AudioKit (av/av_audiokit.c). Code C
 * compile par la chaine d'Onyx (WSL) en position independante ; binaire
 * precompile (prebuilt/mediakit.so), hors du selfhost.
 *
 * Pas de codec audio dans MediaKit (decision de l'utilisateur) : les paquets
 * audio sont pour AudioKit. Seul point ou l'on decode un paquet audio :
 * audio_decode_packet () plus bas : Opus, Vorbis, MP3, FLAC par ak_codec_*
 * d'AudioKit (av/av_audiokit.c), le PCM brut converti ; AAC... : piste muette
 * et un avertissement.
 *
 * Fonctions (table d'exportation, ajout seul : mediakit.S, mediakit.abi) :
 * AAPCS64, entiers, pointeurs, poignees opaques : appelables de l'assembleur
 * comme du C. Une poignee ne sert qu'a un thread a la fois.
 *
 * Systeme : pas de couche posix (libposix est une bibliotheque statique pour
 * un executable a adresse fixe, avec crt0 et errno par thread), mais une glue
 * propre : appels systeme directs (VM_MAP pour le tas et la
 * pile des decodeurs), fichiers par fichier_sys d'AppKit (deja charge dans le
 * process qui appelle), mk_libc.c pour la bibliotheque C. Le kit se reloge
 * lui-meme (R_AARCH64_RELATIVE) dans sa copie des variables, au premier appel
 * de chaque process.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "av/av.h"
#include "mk_sys.h"

#define MK_VERSION	0x00010000		/* 1.0 */
#define MK_MAGIC	0x4D4B4844		/* « MKHD » */
#define MK_IOBUF	(256 * 1024)
#define MK_STACK	(2 * 1024 * 1024)	/* pile des decodeurs (peuplee a la demande) */
#define MK_QMAX		4096			/* paquets en attente par piste, au plus */
#define MK_QBYTES	(48u << 20)		/* octets en attente par piste, au plus */

/* erreurs (numeros d'ARMPI / Linux, negatifs) */
#define MK_E_NOENT	(-2)
#define MK_E_IO		(-5)
#define MK_E_BADF	(-9)
#define MK_E_NOMEM	(-12)
#define MK_E_INVAL	(-22)
#define MK_E_SPIPE	(-29)			/* recherche impossible */
#define MK_E_NODATA	(-61)			/* pas de piste de ce genre */
#define MK_E_BADMSG	(-74)			/* donnees illisibles */
#define MK_E_UNSUP	(-95)			/* format ou codec non pris */
#define MK_E_NOKIT	(-200)			/* AppKit introuvable */

/* ---- relocations, AppKit ------------------------------------------------------------------- */

static int relocated;
extern char __kit_base[] __attribute__((visibility("hidden")));
typedef struct { uint64_t off, info; int64_t addend; } rela_t;
extern rela_t __rela_start[] __attribute__((visibility("hidden")));
extern rela_t __rela_end[] __attribute__((visibility("hidden")));

static void kit_relocate(void)
{
	if (relocated) return;
	uintptr_t base = (uintptr_t)__kit_base;
	for (rela_t *r = __rela_start; r < __rela_end; r++)
		if ((uint32_t)r->info == 1027)
			*(uint64_t *)(base + r->off) = base + r->addend;
	relocated = 1;
}

struct kit_table { uint32_t magic, count, off[]; };
static void *fsys;				/* fichier_sys d'AppKit (rang 13) */

static int appkit_open(void)
{
	if (fsys) return 0;
	long r = mk_svc(MK_SYS_LIB_OPEN, (long)"appkit", 14, 0, 0);
	if (r < 0) return MK_E_NOKIT;
	const struct kit_table *k = (const struct kit_table *)r;
	if (k->count <= 13) return MK_E_NOKIT;
	fsys = (char *)k + k->off[13];
	return 0;
}

/* fichier_sys : x8 = numero, x0..x2 ; garde tout sauf x0, x16 (et x30) */
long mk_fsys(long n, long a, long b, long c)
{
	if (!fsys && appkit_open() < 0) return MK_E_NOKIT;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x8 __asm__("x8") = n;
	register void *x9 __asm__("x9") = fsys;
	__asm__ volatile("blr x9" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8), "r"(x9)
			 : "x16", "x17", "x30", "memory", "cc");
	return x0;
}

void mk_log(const char *s)
{
	size_t n = strlen(s);
	if (!n) return;
	mk_fsys(MK_SYS_PRINT, (long)s, (long)n, 0);
	if (s[n - 1] != '\n') mk_fsys(MK_SYS_PRINT, (long)"\n", 1, 0);
}

static int64_t now_us(void)
{
	uint64_t c, f;
	__asm__ volatile("isb\n\tmrs %0, cntpct_el0\n\tmrs %1, cntfrq_el0" : "=r"(c), "=r"(f));
	return (int64_t)(c / f) * 1000000 + (int64_t)((c % f) * 1000000 / f);
}

/* (bibliotheque av) l'horloge */
av_us av_now(void) { return now_us(); }
/* (bibliotheque av) les decodeurs d'essai d'Onyx (av_stub.c) : aucun ici */
const struct av_codec_impl *av__stub_of(int codec);
const struct av_codec_impl *av__stub_of(int codec) { (void)codec; return 0; }
/* (dav1d) nombre de coeurs : un seul thread de decodage */
long onyx_dav1d_sysconf(int name);

extern long mk_on_stack(long (*fn)(void *), void *arg, void *top);

/* ---- structures rendues au programme (champs de 64 bits : docs/MEDIAKIT.md) ------------- */

struct mk_info {
	int64_t duration;		/* +0 us, 0 inconnue */
	int64_t ntracks;		/* +8 */
	int64_t vtrack, atrack;		/* +16, +24 pistes choisies (-1 : aucune) */
	int64_t width, height;		/* +32, +40 image (taille d'affichage) */
	int64_t fps1000;		/* +48 images/s x 1000 (0 inconnu) */
	int64_t rate, channels;		/* +56, +64 son */
	int64_t vcodec, acodec;		/* +72, +80 MK_C_* (= AV_C_* d'av.h) */
	int64_t vok, aok;		/* +88, +96 1 : decode par MediaKit */
	char container[16];		/* +104 « webm », « mp4 »... */
	char vname[16];			/* +120 « vp9 »... */
	char aname[16];			/* +136 « opus »... */
};					/* 152 octets */

struct mk_track {
	int64_t kind;			/* +0 1 video, 2 audio, 0 autre */
	int64_t codec;			/* +8 */
	int64_t width, height;		/* +16, +24 */
	int64_t rate, channels;		/* +32, +40 */
	int64_t duration;		/* +48 us */
	const uint8_t *extra;		/* +56 donnees d'initialisation du codec */
	int64_t extra_len;		/* +64 */
	int64_t supported;		/* +72 1 : MediaKit le decode */
	char name[16];			/* +80 « vp9 », « opus »... */
	char codec_id[32];		/* +96 nom du conteneur (« V_VP9 ») */
	int64_t delay;			/* +128 us a retirer au debut (pre-skip d'Opus) */
};					/* 136 octets */

struct mk_frame {
	int64_t pts, dur;		/* +0, +8 us */
	int64_t width, height;		/* +16, +24 taille decodee */
	int64_t decode_us;		/* +32 temps de decodage de cette image */
};

struct mk_packet {
	const uint8_t *data;		/* +0 (au kit, jusqu'a l'appel suivant) */
	int64_t size;			/* +8 */
	int64_t pts, dur;		/* +16, +24 us */
	int64_t key;			/* +32 */
	int64_t codec;			/* +40 */
};

struct mk_stats {
	int64_t vframes;		/* +0 images decodees */
	int64_t vdecode_us;		/* +8 temps total de decodage video */
	int64_t aframes;		/* +16 trames audio rendues */
	int64_t adecode_us;		/* +24 */
	int64_t dropped;		/* +32 paquets jetes (files pleines) */
	int64_t heap_big;		/* +40 octets des gros blocs du tas */
};

/* ---- la poignee --------------------------------------------------------------------------- */

struct pq_item { struct pq_item *next; struct av_packet p; };
struct pq { struct pq_item *head, *tail; int n; size_t bytes; };

struct mk {
	uint32_t magic;
	long fd;
	int64_t size;
	struct av_demux *dm;
	uint8_t *io;
	int eof;
	int vt, at;				/* pistes choisies (-1) */
	struct pq pre, vq, aq;
	struct av_decoder *vdec;
	int vdrained;
	struct av_frame vf;
	int have_vf;
	int64_t vf_decode_us;
	/* son */
	struct av_decoder *adec;
	int amuted, awarned;
	int out_rate;
	struct av_resampler *rs;
	int16_t *apend;				/* s16 stereo deja reechantillonne */
	int apend_cap, apend_n, apend_pos;
	int64_t apend_pts;			/* pts de apend[apend_pos] */
	int adrained;
	struct av_packet apkt;			/* mk_audio_packet : le dernier rendu */
	/* conversion a l'echelle */
	uint32_t *rgb;
	size_t rgb_cap;
	int32_t *xmap;
	int xmap_cap;
	/* pile des decodeurs */
	void *stack;
	int on_stack;
	struct mk_stats st;
};

static struct mk *handle_of(long h)
{
	struct mk *m = (struct mk *)h;
	if (h <= 0 || ((uintptr_t)m & 7) || m->magic != MK_MAGIC) return 0;
	return m;
}

/* fn (arg) sur la pile du kit */
static long run(struct mk *m, long (*fn)(void *), void *arg)
{
	if (m->on_stack || !m->stack) return fn(arg);
	m->on_stack = 1;
	long r = mk_on_stack(fn, arg, (char *)m->stack + MK_STACK);
	m->on_stack = 0;
	return r;
}

/* ---- files de paquets ---------------------------------------------------------------------- */

static void pq_push(struct pq *q, struct av_packet *p)
{
	struct pq_item *it = malloc(sizeof *it);
	if (!it) { av_pkt_free(p); return; }
	it->p = *p;
	it->next = 0;
	if (q->tail) q->tail->next = it; else q->head = it;
	q->tail = it;
	q->n++;
	q->bytes += p->size;
}

static int pq_pop(struct pq *q, struct av_packet *p)
{
	struct pq_item *it = q->head;
	if (!it) return 0;
	q->head = it->next;
	if (!q->head) q->tail = 0;
	q->n--;
	q->bytes -= it->p.size;
	*p = it->p;
	free(it);
	return 1;
}

static void pq_clear(struct pq *q)
{
	struct av_packet p;
	while (pq_pop(q, &p)) av_pkt_free(&p);
}

/* ---- lecture du fichier, demultiplexage ---------------------------------------------------- */

static long read_at(struct mk *m, int64_t pos, void *buf, long n)
{
	if (mk_fsys(MK_SYS_SEEK, m->fd, (long)pos, 0) < 0) return -1;
	return mk_fsys(MK_SYS_READ, m->fd, (long)buf, n);
}

/* un paquet du conteneur, range dans la file de sa piste -> 1, 0 fin, < 0 erreur */
static int pull_one(struct mk *m)
{
	struct av_packet p;
	int again = 0;
	if (m->eof) return 0;
	for (;;) {
		int r = av_demux_read(m->dm, &p);
		if (r == AV_OK) {
			struct pq *q = 0;
			if (m->vt < -1 || m->at < -1) q = &m->pre;	/* (ouverture : pistes pas encore choisies) */
			else if (p.track == m->vt) q = &m->vq;
			else if (p.track == m->at) q = &m->aq;
			if (!q) { av_pkt_free(&p); return 1; }
			if (q->n >= MK_QMAX || q->bytes >= MK_QBYTES) {	/* personne ne lit cette piste */
				struct av_packet o;
				if (pq_pop(q, &o)) { av_pkt_free(&o); m->st.dropped++; }
			}
			pq_push(q, &p);
			return 1;
		}
		if (r == AV_AGAIN) {
			int64_t pos = av_demux_want(m->dm);
			long n = -1;
			if (pos >= 0 && pos < m->size)
				n = read_at(m, pos, m->io, MK_IOBUF);
			if (n <= 0) {
				if (again++ > 2) { m->eof = 1; return 0; }
				av_demux_end(m->dm);
				continue;
			}
			again = 0;
			av_demux_feed(m->dm, pos, m->io, (size_t)n);
			continue;
		}
		if (r == AV_EOF) { m->eof = 1; return 0; }
		m->eof = 1;
		return r == AV_EUNSUP ? MK_E_UNSUP : r == AV_ENOMEM ? MK_E_NOMEM : MK_E_BADMSG;
	}
}

static int track_supported(const struct av_track *t)
{
	if (t->kind == AV_VIDEO) return av_decoder_supported_track(t);
	if (t->kind == AV_AUDIO)
		return !t->encrypted && (t->codec == AV_C_OPUS || t->codec == AV_C_VORBIS ||
			t->codec == AV_C_MP3 || t->codec == AV_C_FLAC ||
			(t->codec >= AV_C_PCM_S16LE && t->codec <= AV_C_PCM_U8));
	return 0;
}

static void copy_name(char *d, const char *s, size_t n)
{
	size_t i = 0;
	if (s) for (; i + 1 < n && s[i]; i++) d[i] = s[i];
	d[i] = 0;
}

static const char *codec_name(int c)
{
	switch (c) {
	case AV_C_VP8: return "vp8";
	case AV_C_VP9: return "vp9";
	case AV_C_AV1: return "av1";
	case AV_C_H264: return "h264";
	case AV_C_HEVC: return "hevc";
	case AV_C_MJPEG: return "mjpeg";
	case AV_C_I420: return "i420";
	case AV_C_OPUS: return "opus";
	case AV_C_VORBIS: return "vorbis";
	case AV_C_AAC: return "aac";
	case AV_C_MP3: return "mp3";
	case AV_C_FLAC: return "flac";
	case AV_C_ALAW: return "alaw";
	case AV_C_ULAW: return "ulaw";
	case AV_C_NONE: return "?";
	}
	return (c >= AV_C_PCM_S16LE && c <= AV_C_PCM_U8) ? "pcm" : "?";
}

/* ---- le point de branchement du son -------------------------------------------------------- */

/* Decoder UN paquet audio : le seul endroit de MediaKit qui decode du son.
 * Opus, Vorbis, MP3, FLAC : l'API par paquets d'AudioKit (av/av_audiokit.c : le paquet,
 * les donnees d'initialisation de la piste -> PCM) ; le PCM brut : conversion ; tout autre
 * codec (AAC...) : piste muette, un avertissement.
 * -> AV_OK (*pcm, *frames, *rate, *ch remplis ; *frames peut valoir 0), AV_AGAIN
 * (rien encore), < 0 erreur (paquet saute) */
static int audio_decode_packet(struct mk *m, const struct av_packet *p, const float **pcm,
			       int *frames, int *rate, int *ch, av_us *pts)
{
	struct av_frame f;
	int r;
	if (p) {
		r = av_decoder_send(m->adec, p);
		if (r < 0) return r;
	} else if (!m->adrained) {
		av_decoder_send(m->adec, 0);
		m->adrained = 1;
	}
	r = av_decoder_receive(m->adec, &f);
	if (r != AV_OK) return AV_AGAIN;
	*pcm = f.pcm;
	*frames = f.samples;
	*rate = f.rate;
	*ch = f.channels;
	*pts = f.pts;
	return AV_OK;
}

/* ---- ouverture ----------------------------------------------------------------------------- */

static void mk_free(struct mk *m)
{
	if (!m) return;
	m->magic = 0;
	pq_clear(&m->pre); pq_clear(&m->vq); pq_clear(&m->aq);
	if (m->vdec) av_decoder_free(m->vdec);
	if (m->adec) av_decoder_free(m->adec);
	if (m->rs) av_resampler_free(m->rs);
	if (m->dm) av_demux_free(m->dm);
	if (m->fd > 0) mk_fsys(MK_SYS_CLOSE, m->fd, 0, 0);
	free(m->apkt.data);
	free(m->apend);
	free(m->rgb);
	free(m->xmap);
	free(m->io);
	if (m->stack) mk_vm_unmap(m->stack, MK_STACK);
	free(m);
}

/* les pistes choisies : les paquets mis de cote a l'ouverture vont a leur file */
static void select_tracks(struct mk *m, int v, int a)
{
	struct av_packet p;
	struct pq keep = { 0, 0, 0, 0 };
	m->vt = v;
	m->at = a;
	while (pq_pop(&m->pre, &p)) {
		if (p.track == v) pq_push(&m->vq, &p);
		else if (p.track == a) pq_push(&m->aq, &p);
		else pq_push(&keep, &p);
	}
	m->pre = keep;			/* (gardes pour un autre choix) */
}

static int open_decoders(struct mk *m)
{
	if (m->vdec) { av_decoder_free(m->vdec); m->vdec = 0; }
	if (m->adec) { av_decoder_free(m->adec); m->adec = 0; }
	m->have_vf = 0;
	m->vdrained = m->adrained = 0;
	if (m->vt >= 0) {
		m->vdec = av_decoder_new(av_demux_track(m->dm, m->vt));
		if (!m->vdec) m->vt = -1;
	}
	m->amuted = 0;
	if (m->at >= 0) {
		const struct av_track *t = av_demux_track(m->dm, m->at);
		if (track_supported(t)) m->adec = av_decoder_new(t);
		if (!m->adec) m->amuted = 1;
	}
	return 0;
}

struct open_args { struct mk *m; int r; };

static long open_body(void *arg)
{
	struct open_args *o = arg;
	struct mk *m = o->m;
	int i, v = -1, a = -1, n;
	m->dm = av_demux_new(AV_FMT_UNKNOWN);
	if (!m->dm) return MK_E_NOMEM;
	av_demux_set_size(m->dm, m->size);
	m->vt = m->at = -2;		/* tout garder jusqu'au choix */
	/* les pistes : lire jusqu'a les connaitre (et un paquet au moins) */
	for (i = 0; i < 4096; i++) {
		int r = pull_one(m);
		if (r < 0) return r;
		if (av_demux_ntracks(m->dm) > 0 && m->pre.n > 0) break;
		if (r == 0) break;
	}
	n = av_demux_ntracks(m->dm);
	if (n <= 0) return MK_E_UNSUP;
	for (i = 0; i < n; i++) {
		const struct av_track *t = av_demux_track(m->dm, i);
		if (t->kind == AV_VIDEO && v < 0 && track_supported(t)) v = i;
		if (t->kind == AV_AUDIO && a < 0) a = i;
	}
	for (i = 0; i < n && v < 0; i++)	/* (une video non prise : montree dans mk_info) */
		if (av_demux_track(m->dm, i)->kind == AV_VIDEO) v = i;
	select_tracks(m, v, a);
	open_decoders(m);
	if (m->vt < 0 && m->at < 0) return MK_E_UNSUP;
	return 0;
}

/* 0 mk_version () -> 0x00010000 */
long mk_version(void)
{
	kit_relocate();
	return MK_VERSION;
}

/* 1 mk_open (chemin) -> poignee (> 0) ou erreur (< 0) */
long mk_open(const char *path)
{
	kit_relocate();
	int r = appkit_open();
	if (r < 0) return r;
	struct mk *m = calloc(1, sizeof *m);
	if (!m) return MK_E_NOMEM;
	m->magic = MK_MAGIC;
	m->vt = m->at = -1;
	long s = mk_vm_map(MK_STACK);
	if (s > 0) m->stack = (void *)s;
	m->io = malloc(MK_IOBUF);
	if (!m->io) { mk_free(m); return MK_E_NOMEM; }
	m->fd = mk_fsys(MK_SYS_OPEN, (long)path, MK_O_READ, 0);
	if (m->fd < 0) { r = (int)m->fd; m->fd = 0; mk_free(m); return r; }
	m->size = mk_fsys(MK_SYS_SEEK, m->fd, 0, 2);
	if (m->size <= 0) { mk_free(m); return MK_E_IO; }
	struct open_args o = { m, 0 };
	r = (int)run(m, open_body, &o);
	if (r < 0) { mk_free(m); return r; }
	return (long)m;
}

/* 2 mk_close (poignee) */
long mk_close(long h)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m) return MK_E_BADF;
	mk_free(m);
	return 0;
}

/* 3 mk_info (poignee, struct mk_info *) -> 0 */
long mk_info(long h, struct mk_info *o)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m || !o) return MK_E_BADF;
	memset(o, 0, sizeof *o);
	o->duration = av_demux_duration(m->dm);
	o->ntracks = av_demux_ntracks(m->dm);
	o->vtrack = m->vt;
	o->atrack = m->at;
	if (m->vt >= 0) {
		const struct av_track *t = av_demux_track(m->dm, m->vt);
		o->width = t->dwidth ? t->dwidth : t->width;
		o->height = t->dheight ? t->dheight : t->height;
		o->fps1000 = (int64_t)(t->fps * 1000);
		o->vcodec = t->codec;
		o->vok = m->vdec != 0;
		copy_name(o->vname, codec_name(t->codec), sizeof o->vname);
		if (!o->duration) o->duration = t->duration;
	}
	if (m->at >= 0) {
		const struct av_track *t = av_demux_track(m->dm, m->at);
		o->rate = t->rate;
		o->channels = t->channels;
		o->acodec = t->codec;
		o->aok = m->adec != 0;
		copy_name(o->aname, codec_name(t->codec), sizeof o->aname);
		if (!o->duration) o->duration = t->duration;
	}
	copy_name(o->container, av_demux_name(m->dm), sizeof o->container);
	return 0;
}

/* 4 mk_track (poignee, n, struct mk_track *) -> 0 */
long mk_track(long h, long i, struct mk_track *o)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m || !o) return MK_E_BADF;
	const struct av_track *t = av_demux_track(m->dm, (int)i);
	if (!t) return MK_E_INVAL;
	memset(o, 0, sizeof *o);
	o->kind = t->kind;
	o->codec = t->codec;
	o->width = t->dwidth ? t->dwidth : t->width;
	o->height = t->dheight ? t->dheight : t->height;
	o->rate = t->rate;
	o->channels = t->channels;
	o->duration = t->duration;
	o->extra = t->extra;
	o->extra_len = (int64_t)t->extra_len;
	o->supported = track_supported(t);
	o->delay = t->delay;
	copy_name(o->name, codec_name(t->codec), sizeof o->name);
	copy_name(o->codec_id, t->codec_id, sizeof o->codec_id);
	return 0;
}

/* 5 mk_select (poignee, piste video, piste audio ; -1 : aucune) -> 0 */
long mk_select(long h, long v, long a)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m) return MK_E_BADF;
	int n = av_demux_ntracks(m->dm);
	if (v >= n || a >= n) return MK_E_INVAL;
	if (v >= 0 && av_demux_track(m->dm, (int)v)->kind != AV_VIDEO) return MK_E_INVAL;
	if (a >= 0 && av_demux_track(m->dm, (int)a)->kind != AV_AUDIO) return MK_E_INVAL;
	struct av_packet p;
	while (pq_pop(&m->vq, &p)) pq_push(&m->pre, &p);	/* (rendus a la reserve) */
	while (pq_pop(&m->aq, &p)) pq_push(&m->pre, &p);
	select_tracks(m, v < 0 ? -1 : (int)v, a < 0 ? -1 : (int)a);
	open_decoders(m);
	if (m->rs) { av_resampler_free(m->rs); m->rs = 0; }
	m->apend_n = m->apend_pos = 0;
	return 0;
}

/* ---- video ------------------------------------------------------------------------------- */

struct vnext_args { struct mk *m; struct mk_frame *f; };

static long vnext_body(void *arg)
{
	struct vnext_args *a = arg;
	struct mk *m = a->m;
	struct av_packet p;
	int64_t t0 = now_us();
	m->have_vf = 0;
	for (;;) {
		int r = av_decoder_receive(m->vdec, &m->vf);
		if (r == AV_OK && m->vf.kind == AV_VIDEO && m->vf.width > 0) break;
		if (pq_pop(&m->vq, &p)) {
			av_decoder_send(m->vdec, &p);	/* (un paquet illisible : saute) */
			av_pkt_free(&p);
			continue;
		}
		if (!m->eof) {
			int k = pull_one(m);
			if (k < 0) return k;
			continue;
		}
		if (!m->vdrained) {
			av_decoder_send(m->vdec, 0);
			m->vdrained = 1;
			continue;
		}
		return 0;			/* fin */
	}
	m->have_vf = 1;
	m->vf_decode_us = now_us() - t0;
	m->st.vframes++;
	m->st.vdecode_us += m->vf_decode_us;
	if (a->f) {
		a->f->pts = m->vf.pts;
		a->f->dur = m->vf.dur;
		a->f->width = m->vf.width;
		a->f->height = m->vf.height;
		a->f->decode_us = m->vf_decode_us;
	}
	return 1;
}

/* 6 mk_video_next (poignee, struct mk_frame * (0 : rien)) -> 1 image decodee (gardee pour
 * mk_video_convert), 0 fin, < 0 erreur */
long mk_video_next(long h, struct mk_frame *f)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m) return MK_E_BADF;
	if (!m->vdec) return MK_E_NODATA;
	struct vnext_args a = { m, f };
	return run(m, vnext_body, &a);
}

/* XRGB8888 w x h (bilineaire, 16.16) depuis l'image RGB de la source */
static void scale_xrgb(struct mk *m, const uint32_t *src, int sw, int sh, uint8_t *dst, long stride,
		       int dw, int dh)
{
	int x, y;
	if (m->xmap_cap < dw) {
		free(m->xmap);
		m->xmap = malloc(sizeof(int32_t) * 2 * (size_t)dw);
		if (!m->xmap) { m->xmap_cap = 0; return; }
		m->xmap_cap = dw;
	}
	int64_t xs = ((int64_t)sw << 16) / dw, ys = ((int64_t)sh << 16) / dh;
	for (x = 0; x < dw; x++) {
		int64_t sx = x * xs + xs / 2 - 0x8000;
		if (sx < 0) sx = 0;
		int ix = (int)(sx >> 16);
		int fx = (int)((sx >> 8) & 0xFF);
		if (ix >= sw - 1) { ix = sw - 1; fx = 0; }
		m->xmap[2 * x] = ix;
		m->xmap[2 * x + 1] = fx;
	}
	for (y = 0; y < dh; y++) {
		int64_t sy = y * ys + ys / 2 - 0x8000;
		if (sy < 0) sy = 0;
		int iy = (int)(sy >> 16);
		int fy = (int)((sy >> 8) & 0xFF);
		if (iy >= sh - 1) { iy = sh - 1; fy = 0; }
		const uint32_t *r0 = src + (size_t)iy * sw;
		const uint32_t *r1 = fy ? r0 + sw : r0;
		uint32_t *d = (uint32_t *)(dst + (size_t)y * stride);
		for (x = 0; x < dw; x++) {
			int ix = m->xmap[2 * x], fx = m->xmap[2 * x + 1];
			uint32_t a = r0[ix], b = fx ? r0[ix + 1] : a;
			uint32_t c = r1[ix], e = fx ? r1[ix + 1] : c;
			/* deux canaux a la fois (rouge et bleu, puis vert) */
			uint32_t rb0 = (a & 0xFF00FF) * (256 - fx) + (b & 0xFF00FF) * fx;
			uint32_t g0 = (a & 0x00FF00) * (256 - fx) + (b & 0x00FF00) * fx;
			uint32_t rb1 = (c & 0xFF00FF) * (256 - fx) + (e & 0xFF00FF) * fx;
			uint32_t g1 = (c & 0x00FF00) * (256 - fx) + (e & 0x00FF00) * fx;
			rb0 = (rb0 >> 8) & 0xFF00FF; g0 = (g0 >> 8) & 0x00FF00;
			rb1 = (rb1 >> 8) & 0xFF00FF; g1 = (g1 >> 8) & 0x00FF00;
			uint32_t rb = ((rb0 * (256 - fy) + rb1 * fy) >> 8) & 0xFF00FF;
			uint32_t g = ((g0 * (256 - fy) + g1 * fy) >> 8) & 0x00FF00;
			d[x] = rb | g;
		}
	}
}

/* 7 mk_video_convert (poignee, destination, pas en octets, largeur, hauteur) -> 0 :
 * l'image gardee par mk_video_next, en XRGB8888 (0x00RRGGBB) a la taille voulue */
long mk_video_convert(long h, uint8_t *dst, long stride, long w, long hh)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m) return MK_E_BADF;
	if (!m->have_vf || !dst || w <= 0 || hh <= 0 || stride < w * 4) return MK_E_INVAL;
	if (w == m->vf.width && hh == m->vf.height) {
		av_yuv_to_rgb(&m->vf, dst, (int)stride, AV_PIX_BGRA);
		return 0;
	}
	size_t need = (size_t)m->vf.width * m->vf.height * 4;
	if (m->rgb_cap < need) {
		free(m->rgb);
		m->rgb = malloc(need);
		if (!m->rgb) { m->rgb_cap = 0; return MK_E_NOMEM; }
		m->rgb_cap = need;
	}
	av_yuv_to_rgb(&m->vf, (uint8_t *)m->rgb, m->vf.width * 4, AV_PIX_BGRA);
	scale_xrgb(m, m->rgb, m->vf.width, m->vf.height, dst, stride, (int)w, (int)hh);
	return 0;
}

/* 8 mk_video_read (poignee, destination, pas, largeur, hauteur, struct mk_frame *) -> 1, 0
 * fin, < 0 : mk_video_next puis mk_video_convert */
long mk_video_read(long h, uint8_t *dst, long stride, long w, long hh, struct mk_frame *f)
{
	long r = mk_video_next(h, f);
	if (r != 1) return r;
	long c = mk_video_convert(h, dst, stride, w, hh);
	return c < 0 ? c : 1;
}

/* ---- son ---------------------------------------------------------------------------------- */

/* 9 mk_audio_open (poignee, frequence de sortie) -> 0 : mk_audio_read rendra du s16 stereo a
 * cette frequence ; MK_E_UNSUP : codec pour AudioKit (piste muette, avertissement au
 * journal ; mk_audio_packet reste possible) ; MK_E_NODATA : pas de son */
long mk_audio_open(long h, long rate)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m) return MK_E_BADF;
	if (m->at < 0) return MK_E_NODATA;
	if (rate < 8000 || rate > 192000) return MK_E_INVAL;
	m->out_rate = (int)rate;
	if (m->rs) { av_resampler_free(m->rs); m->rs = 0; }
	m->apend_n = m->apend_pos = 0;
	if (!m->adec) {
		if (!m->awarned) {
			const struct av_track *t = av_demux_track(m->dm, m->at);
			char b[160];
			snprintf(b, sizeof b, "mediakit : son %s pour AudioKit (pas encore de decodeur par "
				 "paquets) : piste muette", codec_name(t->codec));
			mk_log(b);
			m->awarned = 1;
		}
		return MK_E_UNSUP;
	}
	return 0;
}

struct aread_args { struct mk *m; int16_t *dst; long max; int64_t *pts; };

static long aread_body(void *arg)
{
	struct aread_args *a = arg;
	struct mk *m = a->m;
	long done = 0;
	int64_t t0 = now_us();
	int first = 1;
	while (done < a->max) {
		if (m->apend_pos < m->apend_n) {
			long k = m->apend_n - m->apend_pos;
			if (k > a->max - done) k = a->max - done;
			if (first && a->pts) *a->pts = m->apend_pts;
			first = 0;
			memcpy(a->dst + 2 * done, m->apend + 2 * m->apend_pos, (size_t)k * 4);
			m->apend_pos += (int)k;
			m->apend_pts += k * 1000000 / m->out_rate;
			done += k;
			continue;
		}
		/* un paquet de plus, decode, reechantillonne */
		struct av_packet p;
		int have = pq_pop(&m->aq, &p);
		if (!have && !m->eof) {
			int k = pull_one(m);
			if (k < 0) break;
			continue;
		}
		const float *pcm; int frames = 0, rate = 0, ch = 0; av_us pts = AV_NOTIME;
		int r = audio_decode_packet(m, have ? &p : 0, &pcm, &frames, &rate, &ch, &pts);
		if (have) av_pkt_free(&p);
		if (r != AV_OK) {
			if (!have) break;	/* fin : plus rien dans le decodeur */
			continue;
		}
		if (frames <= 0) continue;
		if (!m->rs) {
			m->rs = av_resampler_new(rate, ch, m->out_rate);
			if (!m->rs) break;
		}
		int cap = (int)(frames * av_resampler_factor(m->rs, 1.0)) + 16;
		if (m->apend_cap < cap) {
			free(m->apend);
			m->apend = malloc((size_t)cap * 4);
			if (!m->apend) { m->apend_cap = 0; break; }
			m->apend_cap = cap;
		}
		m->apend_n = av_resample(m->rs, pcm, frames, m->apend, cap, 1.0, 1.0f);
		m->apend_pos = 0;
		if (pts != AV_NOTIME) m->apend_pts = pts;
	}
	m->st.aframes += done;
	m->st.adecode_us += now_us() - t0;
	return done;
}

/* 10 mk_audio_read (poignee, tampon s16 stereo, trames au plus, int64_t *pts (0 : rien)) ->
 * trames ecrites (0 : fin) ; *pts = temps de la premiere (us) */
long mk_audio_read(long h, int16_t *dst, long max, int64_t *pts)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m) return MK_E_BADF;
	if (!m->adec || !m->out_rate) return m->at < 0 ? MK_E_NODATA : MK_E_UNSUP;
	if (!dst || max <= 0) return MK_E_INVAL;
	struct aread_args a = { m, dst, max, pts };
	return run(m, aread_body, &a);
}

static long apkt_body(void *arg)
{
	struct mk *m = arg;
	for (;;) {
		if (pq_pop(&m->aq, &m->apkt)) return 1;
		if (m->eof) return 0;
		int k = pull_one(m);
		if (k < 0) return k;
	}
}

/* 11 mk_audio_packet (poignee, struct mk_packet *) -> 1, 0 fin : le paquet suivant de la piste
 * audio, tel quel (pour un decodeur par paquets : AudioKit) ; a ne pas meler a
 * mk_audio_read */
long mk_audio_packet(long h, struct mk_packet *o)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m || !o) return MK_E_BADF;
	if (m->at < 0) return MK_E_NODATA;
	av_pkt_free(&m->apkt);
	memset(&m->apkt, 0, sizeof m->apkt);
	long r = run(m, apkt_body, m);
	if (r != 1) return r;
	o->data = m->apkt.data;
	o->size = (int64_t)m->apkt.size;
	o->pts = m->apkt.pts;
	o->dur = m->apkt.dur;
	o->key = m->apkt.key;
	o->codec = av_demux_track(m->dm, m->at)->codec;
	return 1;
}

/* ---- recherche ----------------------------------------------------------------------------- */

struct seek_args { struct mk *m; int64_t t; };

static long seek_body(void *arg)
{
	struct seek_args *a = arg;
	struct mk *m = a->m;
	av_us at = 0;
	int64_t pos = av_demux_seek(m->dm, a->t, &at);
	if (pos < 0) {
		if (a->t > 0) return MK_E_SPIPE;
		/* au debut : relire depuis 0 */
		av_demux_free(m->dm);
		m->dm = av_demux_new(AV_FMT_UNKNOWN);
		if (!m->dm) return MK_E_NOMEM;
		av_demux_set_size(m->dm, m->size);
		at = 0;
	}
	pq_clear(&m->vq); pq_clear(&m->aq); pq_clear(&m->pre);
	m->eof = 0;
	if (pos >= 0) {
		/* les octets du point d'acces tout de suite (le demultiplexeur garde sinon la fin
		 * du fichier vue avant : av_demux_end) */
		int64_t w = av_demux_want(m->dm);
		long n = w >= 0 && w < m->size ? read_at(m, w, m->io, MK_IOBUF) : -1;
		if (n > 0) av_demux_feed(m->dm, w, m->io, (size_t)n);
	}
	if (m->vdec) av_decoder_flush(m->vdec);
	if (m->adec) av_decoder_flush(m->adec);
	m->have_vf = 0;
	m->vdrained = m->adrained = 0;
	if (m->rs) { av_resampler_free(m->rs); m->rs = 0; }
	m->apend_n = m->apend_pos = 0;
	return at;
}

/* 12 mk_seek (poignee, temps us) -> temps du point d'acces atteint (us, <= demande), ou
 * MK_E_SPIPE (pas d'index : seul 0 est possible) */
long mk_seek(long h, int64_t t)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m) return MK_E_BADF;
	if (t < 0) t = 0;
	struct seek_args a = { m, t };
	return run(m, seek_body, &a);
}

/* 13 mk_stats (poignee, struct mk_stats *) -> 0 */
long mk_stats(long h, struct mk_stats *o)
{
	kit_relocate();
	struct mk *m = handle_of(h);
	if (!m || !o) return MK_E_BADF;
	*o = m->st;
	extern size_t mk_heap_big(void);
	o->heap_big = (int64_t)mk_heap_big();
	return 0;
}

/* 14 mk_error_text (code) -> texte (UTF-8, au kit) */
const char *mk_error_text(long e)
{
	kit_relocate();
	switch (e) {
	case 0: return "pas d'erreur";
	case MK_E_NOENT: return "fichier introuvable";
	case MK_E_IO: return "lecture impossible";
	case MK_E_BADF: return "poign\xc3\xa9" "e invalide";
	case MK_E_NOMEM: return "m\xc3\xa9moire insuffisante";
	case MK_E_INVAL: return "argument invalide";
	case MK_E_SPIPE: return "recherche impossible (pas d'index)";
	case MK_E_NODATA: return "pas de piste de ce genre";
	case MK_E_BADMSG: return "donn\xc3\xa9" "es illisibles";
	case MK_E_UNSUP: return "format ou codec non pris en charge";
	case MK_E_NOKIT: return "AppKit introuvable";
	}
	return "erreur";
}
