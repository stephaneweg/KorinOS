/* AudioKit : le son (docs/SON.md 3, docs/KITS.md), en C commun au Pi et au
 * PC ; remplace audiokit.asm et ses .inc, même table (audiokit.abi), mêmes
 * résultats au bit près, mêmes codes d'erreur.
 *
 * Le seul chemin des programmes vers le son :
 *  * flux de lecture : ak_open(fréquence, canaux, bits, sortie) ; ak_write
 *    convertit dans le process de l'application (mono -> stéréo, 8/24/32 bits
 *    -> 16, autre fréquence -> 44,1 kHz par le rééchantillonneur linéaire) et
 *    écrit dans l'anneau partagé du flux, que le service « sound » mélange ;
 *  * capture : ak_rec_open(source), ak_rec_read : s16 stéréo 44,1 kHz ;
 *  * outils : rééchantillonnage, mélange, gain, courbe de volume, en-tête WAV ;
 *  * formats (docs/SON.md 9) : les codecs (ak_*.c, sans appel système) ;
 *    ici, la mémoire, les fichiers (AppKit), le jeu en direct de la FM, la
 *    SoundFont du système ; le lecteur en arrière-plan : ak_play.c.
 * Variables par process (segment inscriptible du kit) : canal du service,
 * table des flux (AK_MAX). */
#include "ak.h"
#include "ak_enc.h"
#include "ak_fm.h"
#include "ak_synth.h"
#include "appkit_import.h"

/* src/inc/armpi.inc, sound.inc */
#define E_AGAIN (-11)
#define E_NODEV (-19)
#define E_PIPE (-32)
#define AKE_BADF (-9)
#define PROT_READ 1
#define PROT_WRITE 2
#define O_READ 1
#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define SND_OP_OPEN 1
#define SND_OP_CLOSE 2
#define SND_OP_REC_OPEN 3
#define SND_OP_STATE 4
#define SND_OP_OUT_VOLUME 5
#define SND_OP_CLIENT_VOLUME 6
#define SND_OP_ROUTE 7
#define SND_OP_MAIN 8
#define SR_HEAD 0
#define SR_TAIL 8
#define SR_VOLUME 16
#define SR_SOURCE 16
#define SR_PLAYED 40
#define SR_LOST 48
#define SR_OUT_TYPE 56
#define SR_DATA 4096
#define SR_STREAM_BYTES 65536

#define AK_MAX 8	   /* flux par process */
#define AK_LAT_DEF 16384   /* ~93 ms d'avance par défaut */
#define AK_PIECE 512	   /* trames converties par morceau */
#define MODE_SVC 1

/* AKS_* : une entrée de la table des flux (128 octets) */
struct aks {
	u64 used;    /* 0 0 libre, 1 lecture, 2 capture, 3 réservé */
	s64 id;	     /* 8 numéro chez sound */
	u8 *ring;    /* 16 anneau mappé */
	s64 buf;     /* 24 son numéro de tampon */
	u64 rate;    /* 32 */
	u64 ch;	     /* 40 */
	u64 bits;    /* 48 */
	u64 fbytes;  /* 56 octets par trame d'entrée */
	u64 rs[3];   /* 64 rééchantillonneur : précédente, position, pas */
	u64 lat;     /* 88 avance maximale (octets) */
	u64 written; /* 96 trames écrites (s16 stéréo 44,1 kHz) */
	u64 mode;    /* 104 1 service */
	u64 pad[2];
};
_Static_assert(sizeof(struct aks) == 128, "aks");

static long ak_chan;			  /* canal du service sound + 1 */
static long ak_mode;			  /* 0 à choisir, 1 service */
static long ak_stnum[2] = {-1, 0};	  /* tampon d'état : numéro, adresse */
static struct aks ak_tab[AK_MAX] __attribute__((aligned(16)));
static long ak_sf_def;			  /* SoundFont du système (1 : aucune) */

/* ---- mémoire */

void *ak_alloc(u64 n)
{
	u64 sz = (n + 16 + 4095) & ~(u64)4095;
	long p = korin_sys4(SYS_VM_MAP, 0, sz, PROT_READ | PROT_WRITE, 0);
	if (p < 0)
		return 0;
	*(u64 *)p = sz; /* taille, pour ak_free */
	return (u8 *)p + 16;
}

void ak_free(void *p)
{
	if (!p)
		return;
	u8 *b = (u8 *)p - 16;
	korin_sys2(SYS_VM_UNMAP, b, *(u64 *)b);
}

/* ---- liaison */

/* -> 1 si le service sound répond (canal ouvert), 0 : aucun son */
static long ak_mode_get(void)
{
	if (ak_mode)
		return ak_mode;
	long c = korin_sys2(SYS_SVC_OPEN, "sound", 3000); /* (sound peut démarrer après nous) */
	if (c < 0)
		return 0;
	ak_chan = c + 1;
	ak_mode = MODE_SVC;
	return MODE_SVC;
}

/* -> 1 service sound, 0 aucun (2 : l'ancien son du noyau, parti) */
long ak_backend(void)
{
	return ak_mode_get();
}

/* message -> résultat (E_NODEV : pas de service) */
static long ak_call(u8 *msg)
{
	if (!ak_chan)
		return E_NODEV;
	long r = korin_sys2(SYS_SVC_CALL, ak_chan - 1, msg);
	if (r == E_PIPE) { /* sound s'est arrêté */
		ak_chan = 0;
		ak_mode = 0;
	}
	return r;
}

static long ak_req(long op, long a, long b, long c, u8 *msg)
{
	*(long *)(msg + MSG_OP) = op;
	*(long *)(msg + MSG_A) = a;
	*(long *)(msg + MSG_B) = b;
	*(long *)(msg + MSG_C) = c;
	*(u32 *)(msg + MSG_LEN) = 0;
	return ak_call(msg);
}

/* poignée, genre (1 lecture, 2 capture) -> entrée, 0 si invalide */
static struct aks *ak_entry(long h, u64 kind)
{
	if ((u64)h >= AK_MAX)
		return 0;
	struct aks *e = &ak_tab[h];
	if (e->used != kind)
		return 0;
	return e;
}

/* -> poignée libre, réservée (genre 3) ; -1 si plein */
static long ak_slot(void)
{
	for (long i = 0; i < AK_MAX; i++) {
		u64 z = 0;
		if (__atomic_compare_exchange_n(&ak_tab[i].used, &z, 3, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return i;
	}
	return -1;
}

static inline u64 ring_get(const struct aks *e, u64 off)
{
	return *(volatile u64 *)(e->ring + off);
}
static inline u64 ring_acq(const struct aks *e, u64 off)
{
	return __atomic_load_n((u64 *)(e->ring + off), __ATOMIC_ACQUIRE);
}
static inline void ring_rel(struct aks *e, u64 off, u64 v)
{
	__atomic_store_n((u64 *)(e->ring + off), v, __ATOMIC_RELEASE);
}

/* ---- flux de lecture */

long ak_open(u64 rate, u64 ch, u64 bits, long out)
{
	u8 msg[SVC_MSG];
	if (rate < 4000 || rate > 192000 || ch - 1 > 7) /* formats acceptés */
		return AKE_INVAL;
	if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
		return AKE_INVAL;
	long mode = ak_mode_get();
	if (!mode)
		return E_NODEV;
	long h = ak_slot();
	if (h < 0)
		return E_AGAIN;
	struct aks *e = &ak_tab[h];
	long r = ak_req(SND_OP_OPEN, out, 0, 0, msg);
	if (r < 0) {
		e->used = 0;
		return r;
	}
	e->id = r;
	e->buf = *(long *)(msg + MSG_B); /* l'anneau du flux */
	long m = korin_sys1(SYS_BUF_MAP, e->buf);
	if (m < 0) { /* flux ouvert chez sound : le fermer */
		ak_req(SND_OP_CLOSE, e->id, 0, 0, msg);
		e->used = 0;
		return m;
	}
	e->ring = (u8 *)m;
	e->mode = (u64)mode;
	e->rate = rate;
	e->ch = ch;
	e->bits = bits;
	e->fbytes = (bits >> 3) * ch;
	e->rs[0] = 0; /* rééchantillonneur : 0, 0, pas */
	e->rs[1] = 0;
	e->rs[2] = (rate << 16) / SND_RATE_STD;
	e->lat = AK_LAT_DEF;
	e->written = 0;
	__atomic_store_n(&e->used, 1, __ATOMIC_RELEASE);
	return h;
}

/* -> octets en attente, *free = place libre (s16 stéréo) */
static u64 ak_fill(const struct aks *e, u64 *free)
{
	u64 head = ring_get(e, SR_HEAD);
	u64 tail = ring_acq(e, SR_TAIL);
	u64 p = head - tail;
	*free = SR_STREAM_BYTES - p;
	return p;
}

/* -> place utilisable (octets s16 stéréo, avance comprise) */
static u64 ak_room(const struct aks *e)
{
	u64 fr;
	u64 p = ak_fill(e, &fr);
	u64 adv = e->lat < p ? 0 : e->lat - p; /* avance restante */
	u64 r = fr < adv ? fr : adv;
	return r & ~(u64)3;
}

/* trames s16 stéréo, octets (qui tiennent) */
static void ak_put(struct aks *e, const void *src, u64 n)
{
	e->written += n >> 2;
	u64 h = ring_get(e, SR_HEAD);
	u8 *data = e->ring + SR_DATA;
	const u8 *s = src;
	for (u64 i = 0; i < n; i += 4) {
		__builtin_memcpy(data + (h & (SR_STREAM_BYTES - 1)), s + i, 4);
		h += 4;
	}
	ring_rel(e, SR_HEAD, h);
}

/* échantillon -> s16 (8 bits non signé ; sinon les deux octets de poids fort) */
static inline u16 ak_sample(const u8 *p, u64 bits, u64 bps)
{
	if (bits == 8)
		return (u16)(((u32)p[0] - 128) << 8);
	const u8 *q = p + bps - 2;
	return (u16)((u32)q[0] | ((u32)(s32)(s8)q[1] << 8));
}

/* source, trames, destination s16 stéréo */
static void ak_convert(const struct aks *e, const u8 *src, u64 frames, u16 *dst)
{
	u64 bps = e->bits >> 3;
	while (frames) {
		u16 l = ak_sample(src, e->bits, bps), r = l;
		if (e->ch != 1) /* (mono : droite = gauche) */
			r = ak_sample(src + bps, e->bits, bps);
		*dst++ = l;
		*dst++ = r;
		src += e->fbytes;
		frames--;
	}
}

long ak_resample(const u32 *in, u64 frames, u32 *out, u64 *st);

/* flux, tampon, octets -> octets pris (format du flux) */
long ak_write(long h, const void *buf, u64 n)
{
	u32 tmp[AK_PIECE];	/* morceau converti */
	u32 tmp2[AK_PIECE + 16]; /* puis rééchantillonné */
	struct aks *e = ak_entry(h, 1);
	if (!e)
		return AKE_BADF;
	const u8 *src = buf;
	u64 taken = 0;
	u64 fb = e->fbytes, rate = e->rate;
	if (rate == SND_RATE_STD && e->ch == 2 && e->bits == 16) { /* format natif : copie directe */
		u64 room = ak_room(e);
		u64 k = n & ~(u64)3;
		if (k > room)
			k = room;
		if (k)
			ak_put(e, src, k);
		return (long)k;
	}
	for (;;) {
		u64 fin = n / fb; /* trames d'entrée disponibles */
		if (!fin)
			break;
		u64 fo = ak_room(e) >> 2; /* trames de sortie possibles */
		if (rate == SND_RATE_STD) {
			if (fo > AK_PIECE) /* même fréquence : trame pour trame */
				fo = AK_PIECE;
		} else {
			if (fo < 2) /* sortie <= m + 1 trames */
				break;
			fo--;
			if (fo > AK_PIECE - 1)
				fo = AK_PIECE - 1;
			fo = (fo * e->rs[2]) >> 16;
			if (fo > AK_PIECE)
				fo = AK_PIECE;
		}
		if (fin > fo)
			fin = fo;
		if (!fin)
			break;
		ak_convert(e, src, fin, (u16 *)tmp);
		u64 fr = fin;
		const u32 *p = tmp;
		if (rate != SND_RATE_STD) {
			fr = (u64)ak_resample(tmp, fin, tmp2, e->rs);
			p = tmp2;
		}
		if (fr << 2)
			ak_put(e, p, fr << 2);
		u64 used = fin * fb;
		src += used;
		n -= used;
		taken += used;
	}
	return (long)taken;
}

/* flux, tampon, octets : tout écrire (attend la place) */
long ak_write_all(long h, const void *buf, u64 n)
{
	const u8 *p = buf;
	u64 done = 0;
	while (n) {
		long r = ak_write(h, p, n);
		if (r < 0)
			return r;
		p += r;
		n -= (u64)r;
		done += (u64)r;
		if (r)
			continue;
		struct aks *e = ak_entry(h, 1); /* plus rien ne passe : un reste plus petit qu'une trame ? */
		if (!e || n < e->fbytes)
			break;
		korin_sys1(SYS_SLEEP, 5);
	}
	return (long)done;
}

/* flux -> octets acceptés maintenant (format du flux) */
long ak_space(long h)
{
	struct aks *e = ak_entry(h, 1);
	if (!e)
		return AKE_BADF;
	u64 f = ak_room(e) >> 2; /* trames 44,1 kHz */
	return (long)((f * e->rate / SND_RATE_STD) * e->fbytes);
}

/* flux -> octets en attente */
long ak_pending(long h)
{
	struct aks *e = ak_entry(h, 1);
	if (!e)
		return AKE_BADF;
	u64 fr;
	return (long)ak_fill(e, &fr);
}

/* flux -> trames jouées */
long ak_position(long h)
{
	struct aks *e = ak_entry(h, 1);
	if (!e)
		return AKE_BADF;
	return (long)ring_get(e, SR_PLAYED);
}

/* flux, volume 0..256 */
long ak_volume(long h, u64 v)
{
	struct aks *e = ak_entry(h, 1);
	if (!e)
		return AKE_BADF;
	if (v > 256)
		v = 256;
	if (e->ring)
		*(volatile u64 *)(e->ring + SR_VOLUME) = v;
	return 0;
}

/* flux, octets d'avance (4096..65536) */
long ak_latency(long h, u64 b)
{
	struct aks *e = ak_entry(h, 1);
	if (!e)
		return AKE_BADF;
	if (b < 4096)
		b = 4096;
	if (b > SR_STREAM_BYTES)
		b = SR_STREAM_BYTES;
	e->lat = b & ~(u64)3;
	return 0;
}

/* flux, tampon {en attente, libre, matériel} */
long ak_status(long h, u64 *buf)
{
	struct aks *e = ak_entry(h, 1);
	if (!e)
		return AKE_BADF;
	u64 fr;
	buf[0] = ak_fill(e, &fr);
	buf[1] = fr;
	buf[2] = ring_get(e, SR_OUT_TYPE);
	return 0;
}

/* flux : attend que tout soit joué (10 s au plus) */
long ak_drain(long h)
{
	for (int i = 0; i < 1000; i++) {
		if (ak_pending(h) <= 0)
			break;
		korin_sys1(SYS_SLEEP, 10);
	}
	return 0;
}

/* flux (lecture ou capture) : fermeture */
long ak_close(long h)
{
	u8 msg[SVC_MSG];
	struct aks *e = ak_entry(h, 1);
	if (!e)
		e = ak_entry(h, 2);
	if (!e)
		return AKE_BADF;
	ak_req(SND_OP_CLOSE, e->id, 0, 0, msg);
	korin_sys1(SYS_BUF_FREE, e->buf);
	__atomic_store_n(&e->used, 0, __ATOMIC_RELEASE);
	return 0;
}

/* ---- capture */

/* source -> poignée de capture */
long ak_rec_open(long src)
{
	u8 msg[SVC_MSG];
	long mode = ak_mode_get();
	if (!mode)
		return E_NODEV;
	long h = ak_slot();
	if (h < 0)
		return E_AGAIN;
	struct aks *e = &ak_tab[h];
	e->mode = (u64)mode;
	long r = ak_req(SND_OP_REC_OPEN, src, 0, 0, msg);
	if (r < 0) {
		e->used = 0;
		return r;
	}
	e->id = r;
	e->buf = *(long *)(msg + MSG_B);
	long m = korin_sys1(SYS_BUF_MAP, e->buf);
	if (m < 0) {
		ak_req(SND_OP_CLOSE, e->id, 0, 0, msg);
		e->used = 0;
		return m;
	}
	e->ring = (u8 *)m;
	__atomic_store_n(&e->used, 2, __ATOMIC_RELEASE);
	return h;
}

/* capture, tampon, octets -> octets lus (non bloquant) */
long ak_rec_read(long h, void *buf, u64 n)
{
	struct aks *e = ak_entry(h, 2);
	if (!e)
		return AKE_BADF;
	u64 head = ring_acq(e, SR_HEAD);
	u64 t = ring_get(e, SR_TAIL);
	u64 av = head - t; /* disponibles */
	n &= ~(u64)3;
	if (n > av)
		n = av;
	const u8 *data = e->ring + SR_DATA;
	u8 *o = buf;
	for (u64 i = 0; i < n; i += 4) {
		__builtin_memcpy(o + i, data + (t & (SR_STREAM_BYTES - 1)), 4);
		t += 4;
	}
	ring_rel(e, SR_TAIL, t);
	return (long)n;
}

/* capture, tampon {disponibles, source, pertes} */
long ak_rec_status(long h, u64 *buf)
{
	struct aks *e = ak_entry(h, 2);
	if (!e)
		return AKE_BADF;
	u64 head = ring_acq(e, SR_HEAD);
	buf[0] = head - ring_get(e, SR_TAIL);
	buf[1] = ring_get(e, SR_SOURCE);
	buf[2] = ring_get(e, SR_LOST);
	return 0;
}

long ak_rec_close(long h)
{
	return ak_close(h);
}

/* ---- outils */

/* Rééchantillonnage linéaire (s16 stéréo), état {précédente (L | R << 16),
 * position 16.16 dans le bloc, pas 16.16} : la trame de sortie à la position
 * p interpole entre in[i-1] et in[i] (i = p >> 16, in[-1] = précédente).
 * entrée, trames (> 0), sortie, état -> trames produites */
long ak_resample(const u32 *in, u64 frames, u32 *out, u64 *st)
{
	u64 k = 0;
	if (!frames)
		return 0;
	u64 pos = st[1], step = st[2];
	u64 end = frames << 16; /* fin du bloc (16.16) */
	while (pos < end) {
		u64 i = pos >> 16;
		s32 fr = (s32)(pos & 0xFFFF);
		u32 a = i ? in[i - 1] : (u32)st[0];
		u32 b = in[i];
		s32 al = (s16)a, bl = (s16)b;
		s32 l = al + (s32)(((s64)(s32)((u32)bl - (u32)al) * fr) >> 16);
		s32 ar = (s32)a >> 16, br = (s32)b >> 16;
		s32 r = ar + (s32)(((s64)(s32)((u32)br - (u32)ar) * fr) >> 16);
		out[k++] = ((u32)l & 0xFFFF) | ((u32)r << 16);
		pos += step;
	}
	st[1] = pos - end;
	*(u32 *)st = in[frames - 1]; /* (32 bits bas seulement, comme l'asm) */
	return (long)k;
}

/* destination += source * gain (0..65536), avec saturation */
long ak_mix_s16(s16 *dst, const s16 *src, u64 frames, long gain)
{
	for (u64 n = frames << 1; n; n--) {
		s32 v = (s32)((u32)(s32)*src++ * (u32)gain) >> 16;
		v += *dst;
		if (v < -32768)
			v = -32768;
		if (v > 32767)
			v = 32767;
		*dst++ = (s16)v;
	}
	return 0;
}

/* tampon, trames, gain (0..65536) */
long ak_gain_s16(s16 *buf, u64 frames, long gain)
{
	for (u64 n = frames << 1; n; n--) {
		s32 v = (s32)((u32)(s32)*buf * (u32)gain) >> 16;
		if (v < -32768)
			v = -32768;
		if (v > 32767)
			v = 32767;
		*buf++ = (s16)v;
	}
	return 0;
}

/* volume 0..100 -> gain 0..65536 (carré : à l'oreille) */
long ak_volume_curve(u64 v)
{
	if (v > 100)
		v = 100;
	return (long)(((v * v) << 16) / 10000);
}

/* ---- mixer (service sound seulement) */

/* -> tampon d'état (SS_*), 0 sans service ; redemandé à chaque appel (sound
 * relancé : nouveau tampon) */
long ak_state(void)
{
	u8 msg[SVC_MSG];
	if (ak_mode_get() != MODE_SVC)
		return 0;
	if (ak_req(SND_OP_STATE, 0, 0, 0, msg) < 0)
		return 0;
	long num = *(long *)(msg + MSG_B); /* numéro du tampon */
	if (ak_stnum[0] == num && ak_stnum[1]) /* déjà mappé */
		return ak_stnum[1];
	long m = korin_sys1(SYS_BUF_MAP, num);
	if (m < 0)
		return 0;
	ak_stnum[0] = num;
	ak_stnum[1] = m;
	return m;
}

static long ak_mixer_op(long op, long a, long b, long c)
{
	u8 msg[SVC_MSG];
	if (ak_mode_get() != MODE_SVC)
		return E_NODEV;
	return ak_req(op, a, b, c, msg);
}

long ak_out_volume(long out, long vol, long mute)
{
	return ak_mixer_op(SND_OP_OUT_VOLUME, out, vol, mute);
}

long ak_client_volume(long pid, long vol, long mute)
{
	return ak_mixer_op(SND_OP_CLIENT_VOLUME, pid, vol, mute);
}

long ak_route(long pid, long out)
{
	return ak_mixer_op(SND_OP_ROUTE, pid, out, 0);
}

long ak_main(long out)
{
	return ak_mixer_op(SND_OP_MAIN, out, 0, 0);
}

/* ---- formats : fichiers par AppKit */

static long akf_read(long fd, void *buf, long n)
{
	return f_read(fd, (long)buf, n);
}
static long akf_write(long fd, const void *buf, long n)
{
	return f_write(fd, (long)buf, n);
}
static long akf_seek(long fd, long pos)
{
	return f_seek(fd, pos, 0);
}
static long akf_close(long fd)
{
	return f_close(fd);
}

/* chemin -> décodeur (< 0 : erreur) */
long ak_dec_open(const char *path)
{
	u64 st[8];
	long r = f_stat((long)path, (long)st);
	if (r < 0)
		return r;
	long fd = f_open((long)path, O_READ);
	if (fd < 0)
		return fd;
	long d = ak_dec_open_io(akf_read, akf_seek, fd, st[0]);
	if (d < 0) {
		f_close(fd);
		return d;
	}
	((struct akd *)d)->ioclose = akf_close;
	return d;
}

/* chemin, AKF_* (WAV, MP3, Opus), fréquence, canaux, débit (0 : par défaut)
 * -> encodeur (< 0 : erreur) ; le fichier est créé (ou vidé), retiré en cas
 * d'échec */
long ak_enc_open(const char *path, long fmt, u64 rate, u64 ch, long quality)
{
	long fd = f_open((long)path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long e = ak_enc_open_io(akf_write, akf_seek, fd, fmt, rate, ch, quality);
	if (e < 0) {
		f_close(fd);
		f_unlink((long)path);
		return e;
	}
	((struct akn *)e)->closefn = akf_close;
	return e;
}

/* ---- jeu en direct de la synthèse FM : un thread rend les voix dans un flux */

#define FL_FRAMES 256
#define FL_STACK_SIZE 32768
#define FL_LATENCY 8192 /* ~46 ms d'avance : on joue au clavier */

struct fl { /* FL_* */
	struct akfm *fm;       /* 0 */
	long stream;	       /* 8 */
	volatile long stop;    /* 16 */
	long tid;	       /* 24 */
	void *stack;	       /* 32 */
	u64 pad[3];
	s16 buf[FL_FRAMES * 2]; /* 64 */
};
_Static_assert(sizeof(struct fl) == 64 + FL_FRAMES * 4, "fl");

/* bloc : rend ce qui sonne, attend la place du flux */
static void __attribute__((noreturn)) ak_fm_thread(struct fl *f)
{
	while (!f->stop) {
		if (!ak_fm_active(f->fm)) {
			korin_sys1(SYS_SLEEP, 2);
			continue;
		}
		if (ak_space(f->stream) < FL_FRAMES * 4) {
			korin_sys1(SYS_SLEEP, 2);
			continue;
		}
		ak_fm_render(f->fm, f->buf, FL_FRAMES);
		ak_write(f->stream, f->buf, FL_FRAMES * 4);
	}
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

/* synthé, sortie (0 : celle du process, -1 : arrêt) -> 0 / < 0 */
long ak_fm_live(long fmh, long out)
{
	struct akfm *fm = (struct akfm *)fmh;
	struct fl *f = (struct fl *)fm->live;
	if (out < 0) { /* arrêt */
		if (f) {
			f->stop = 1;
			korin_sys1(SYS_THREAD_JOIN, f->tid);
			ak_close(f->stream);
			ak_free(f->stack);
			ak_free(f);
			fm->live = 0;
		}
		return 0;
	}
	if (f) /* déjà là */
		return 0;
	long s = ak_open(SND_RATE_STD, 2, 16, out);
	if (s < 0)
		return s;
	ak_latency(s, FL_LATENCY);
	long err = AKE_NOMEM;
	f = ak_alloc(sizeof(struct fl));
	if (!f)
		goto f3;
	f->fm = fm;
	f->stream = s;
	f->stack = ak_alloc(FL_STACK_SIZE);
	if (!f->stack)
		goto f2;
	long t = korin_sys3(SYS_THREAD_CREATE, ak_fm_thread, (u8 *)f->stack + FL_STACK_SIZE, f);
	if (t < 0) {
		err = t;
		ak_free(f->stack);
		goto f2;
	}
	f->tid = t;
	fm->live = (u64)f;
	return 0;
f2:
	ak_free(f);
f3:
	ak_close(s);
	return err;
}

/* ---- SoundFont sur la carte */

/* chemin -> SoundFont (le fichier est lu en mémoire), < 0 : erreur */
long ak_sf_load(const char *path)
{
	u64 st[8];
	long r = f_stat((long)path, (long)st);
	if (r < 0)
		return r;
	u64 size = st[0];
	if (size < 12)
		return AKE_CORRUPT;
	long fd = f_open((long)path, O_READ);
	if (fd < 0)
		return fd;
	u8 *mem = ak_alloc(size);
	if (!mem) {
		f_close(fd);
		return AKE_NOMEM;
	}
	u8 *p = mem;
	for (u64 rem = size; rem;) {
		long k = f_read(fd, (long)p, (long)rem); /* d'un bloc (la boucle : lecture courte) */
		if (k <= 0) {
			ak_free(mem);
			f_close(fd);
			return AKE_IO;
		}
		p += k;
		rem -= (u64)k;
	}
	f_close(fd);
	long sf = sf_parse(mem, size);
	if (sf < 0) {
		ak_free(mem);
		return sf;
	}
	((struct aksf *)sf)->mem = mem;
	((struct aksf *)sf)->size = size;
	return sf;
}

/* SoundFont -> 0 */
long ak_sf_free(struct aksf *sf)
{
	if (sf) {
		ak_free(sf->mem);
		ak_free(sf);
	}
	return 0;
}

/* -> SoundFont du système, chargée au premier appel ; 0 : aucune */
long ak_sf_default(void)
{
	if (ak_sf_def == 1) /* 1 : déjà cherchée, aucune */
		return 0;
	if (ak_sf_def)
		return ak_sf_def;
	long sf = ak_sf_load("/res/soundfonts/GeneralUser-GS.sf2");
	if (sf < 0)
		sf = ak_sf_load("/res/soundfonts/default.sf2");
	if (sf < 0) {
		ak_sf_def = 1;
		return 0;
	}
	ak_sf_def = sf;
	return sf;
}
