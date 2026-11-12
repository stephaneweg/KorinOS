/* AudioKit : décodeur générique (ak_dec.inc). Un décodeur (poignée opaque)
 * lit une SOURCE d'octets : un tampon en mémoire, ou des fonctions de
 * lecture / positionnement (le fichier du kit, ouvert par AppKit). Le format
 * est reconnu aux premiers octets ; le module du format décode une unité
 * dans des tampons PLANS (un par canal, flottant 32 bits ou entier 32 bits
 * cadré à gauche), que ak_dec_read entrelace en s16. L'API par paquets
 * (ak_codec_*, pour MediaKit) prend les mêmes modules, sans source. */
#include "ak.h"

/* ---- mémoire du décodeur */

/* bloc à zéro rendu avec le décodeur (liste chaînée devant le bloc), 0 si échec */
void *akd_alloc(struct akd *d, u64 n)
{
	u8 *b = ak_alloc(n + 16);
	if (!b)
		return 0;
	*(void **)b = d->allocs;
	d->allocs = b;
	return b + 16;
}

/* rend tous les blocs du décodeur et lui-même */
void akd_free(struct akd *d)
{
	if (d->modfree)
		d->modfree(d);
	void *b = d->allocs;
	while (b) {
		void *n = *(void **)b;
		ak_free(b);
		b = n;
	}
	if (d->mode == 1) {
		ak_free(d->base);
		if (d->ioclose)
			d->ioclose(d->ioctx);
	}
	ak_free(d);
}

/* tampons plans (d->pcm) : trames + 4 (marge pour NEON) -> 0 / AKE_NOMEM */
long akd_planes(struct akd *d, u64 frames, u64 ch)
{
	u64 sz = (frames + 4) << 2;
	for (u64 c = 0; c < ch; c++) {
		void *p = akd_alloc(d, sz);
		if (!p)
			return AKE_NOMEM;
		d->pcm[c] = p;
	}
	return 0;
}

/* ---- source d'octets */

/* -> adresse de la position courante ; *avail = octets contigus disponibles
 * (>= voulus, sauf en fin de flux) */
u8 *akd_need(struct akd *d, u64 want, u64 *avail)
{
	if (!d->mode) {
		s64 r = d->len - d->off;
		*avail = r < 0 ? 0 : (u64)r;
		return d->base + d->off;
	}
	if (want >= AKD_IOBUF)
		want = AKD_IOBUF;
	s64 x5 = d->off - d->bufoff; /* place dans le tampon */
	if (x5 < 0 || x5 > d->buflen) {
		/* hors du tampon : repositionner */
		d->ioseek(d->ioctx, d->off);
		d->bufoff = d->off;
		d->iopos = d->off;
		d->buflen = 0;
	} else {
		u64 have = (u64)(d->buflen - x5);
		if (have >= want)
			goto out;
		memmove(d->base, d->base + x5, have); /* ramène le reste au début */
		d->bufoff = d->off;
		d->buflen = (s64)have;
	}
	/* lire jusqu'à avoir assez */
	while ((u64)d->buflen < want) {
		s64 at = d->bufoff + d->buflen;
		if (at != d->iopos) { /* le fichier n'est pas là où il faut */
			d->ioseek(d->ioctx, at);
			d->iopos = at;
		}
		long n = d->ioread(d->ioctx, d->base + d->buflen, AKD_IOBUF - d->buflen);
		if (n <= 0)
			break;
		d->buflen += n;
		d->iopos += n;
	}
out:
	x5 = d->off - d->bufoff;
	*avail = (u64)(d->buflen - x5);
	return d->base + x5;
}

/* ---- ouverture */

static long akd_start(struct akd *d);

static long akd_open_common(struct akd *d)
{
	d->dataend = d->len;
	d->total = -1;
	long r = akd_start(d);
	if (r < 0) {
		akd_free(d);
		return r;
	}
	return (long)d;
}

/* tampon, octets -> décodeur (< 0 : erreur AKE_*) */
long ak_dec_open_mem(const void *buf, u64 n)
{
	struct akd *d = ak_alloc(AKD_BYTES);
	if (!d)
		return AKE_NOMEM;
	d->base = (u8 *)buf;
	d->len = (s64)n;
	return akd_open_common(d);
}

/* lecture (contexte, tampon, octets) -> octets, positionnement (contexte,
 * position), contexte, taille du flux -> décodeur */
long ak_dec_open_io(ak_io_read_fn rd, ak_io_seek_fn sk, long ctx, u64 size)
{
	struct akd *d = ak_alloc(AKD_BYTES);
	if (!d)
		return AKE_NOMEM;
	u8 *b = ak_alloc(AKD_IOBUF + 64);
	if (!b) {
		ak_free(d);
		return AKE_NOMEM;
	}
	d->base = b;
	d->ioread = rd;
	d->ioseek = sk;
	d->ioctx = ctx;
	d->len = (s64)size;
	d->mode = 1;
	return akd_open_common(d);
}

/* reconnaît le format et ouvre son module -> 0 / < 0 */
static long akd_start(struct akd *d)
{
	int id3 = 0; /* ID3v2 vu */
	for (;;) {
		u64 avail;
		u8 *p = akd_need(d, 64, &avail);
		if (avail < 12)
			return AKE_FORMAT;
		u32 w = ak_rd32le(p);
		if ((w & 0xFFFFFF) == 0x00334449) { /* 'ID3' : étiquettes ID3v2, puis l'audio */
			u32 sz = ((u32)p[6] << 21) | ((u32)p[7] << 14) | ((u32)p[8] << 7) | p[9];
			u64 len = (u64)sz + 10;
			if (p[5] & 0x10) /* pied de page */
				len += 10;
			u64 n;
			u8 *t = akd_need(d, len < 65536 ? len : 65536, &n); /* les 64 premiers Ko */
			akd_id3v2(d, n < len ? n : len, t);
			akd_skip(d, len);
			id3 = 1;
			continue;
		}
		if (w == 0x46464952) { /* 'RIFF' ... 'WAVE' */
			if (ak_rd32le(p + 8) != 0x45564157)
				return AKE_FORMAT;
			return wav_open(d);
		}
		if (w == 0x43614C66) /* 'fLaC' */
			return flac_open(d);
		if (w == 0x5367674F) /* 'OggS' */
			return ogg_open(d);
		if (w == 0x6468544D) /* 'MThd' */
			return midi_open(d);
		if (w == 0x732D6D66) /* 'fm-s' : 'fm-song-project' */
			return fms_open(d);
		if (p[0] == 0xFF && (p[1] & 0xE0) == 0xE0) /* synchro MP3 (11 bits à 1) */
			return mp3_open(d);
		if (id3) /* après ID3v2 : MP3 sans doute (synchro cherchée) */
			return mp3_open(d);
		return AKE_FORMAT;
	}
}

/* ---- lecture, informations, recherche, fermeture */

/* décodeur, tampon, trames -> trames rendues (s16 entrelacé, canaux de la
 * source) ; 0 : fin ; < 0 : erreur */
long ak_dec_read(struct akd *d, void *buf, u64 frames)
{
	u64 done = 0;
	while (done < frames) {
		u64 av = (u64)(d->pcmn - d->pcmi);
		if (!av) {
			if (d->eof)
				break;
			long n = d->next(d);
			if (n <= 0) {
				d->eof = 1;
				if (n == 0 || done)
					break;
				return n; /* erreur */
			}
			d->pcmn = n;
			u64 sk = (u64)d->skip; /* trames à sauter */
			u64 k = sk < (u64)n ? sk : (u64)n;
			d->pcmi = (s64)k;
			d->skip = (s64)(sk - k);
			continue;
		}
		u64 want = frames - done;
		if (av > want)
			av = want;
		if (d->limit) { /* fin donnée par le conteneur */
			s64 r = d->total - d->pos;
			u64 ru = r < 0 ? 0 : (u64)r;
			if (av > ru)
				av = ru;
			if (!av) {
				d->eof = 1;
				d->pcmi = d->pcmn;
				break;
			}
		}
		akd_emit(d, (u64)d->pcmi, av, (u8 *)buf + d->ch * 2 * done, AKC_S16);
		d->pcmi += av;
		d->pos += av;
		done += av;
	}
	return (long)done;
}

/* première trame des tampons plans, trames, sortie entrelacée, AKC_S16 / AKC_F32 */
void akd_emit(struct akd *d, u64 first, u64 frames, void *out, long flags)
{
	u64 ch = (u64)d->ch;
	for (u64 c = 0; c < ch; c++) {
		const u8 *src = (const u8 *)d->pcm[c] + (first << 2);
		if (!flags) {
			s16 *o = (s16 *)out + c;
			if (!d->pcmfmt) {
				for (u64 i = 0; i < frames; i++) {
					float f;
					__builtin_memcpy(&f, src + 4 * i, 4);
					s32 v = ak_fcvtns_w(f * 32768.0f);
					if (v > 32767)
						v = 32767;
					if (v < -32768)
						v = -32768;
					o[i * ch] = (s16)v;
				}
			} else {
				for (u64 i = 0; i < frames; i++)
					o[i * ch] = (s16)((s32)ak_rd32le(src + 4 * i) >> 16);
			}
		} else {
			float *o = (float *)out + c;
			for (u64 i = 0; i < frames; i++) {
				float f;
				__builtin_memcpy(&f, src + 4 * i, 4);
				if (d->pcmfmt) {
					s32 v = (s32)ak_rd32le(src + 4 * i);
					f = (float)v * 4.656612873077393e-10f; /* 2^-31 */
				}
				o[i * ch] = f;
			}
		}
	}
}

/* décodeur, tampon AKI_SIZE -> 0 */
long ak_dec_info(struct akd *d, void *info)
{
	s64 *o = info;
	o[AKI_FORMAT / 8] = d->fmt;
	o[AKI_RATE / 8] = d->rate;
	o[AKI_CHANNELS / 8] = d->ch;
	o[AKI_FRAMES / 8] = d->total;
	s64 dur = -1;
	if (d->total >= 0 && d->rate)
		dur = (s64)(((u64)d->total * 1000) / (u64)d->rate);
	o[AKI_DURATION / 8] = dur;
	o[AKI_BITRATE / 8] = d->bitrate;
	o[AKI_POSITION / 8] = d->pos;
	o[AKI_BITS / 8] = d->bits;
	return 0;
}

long ak_dec_tell(struct akd *d)
{
	return d->pos;
}

/* décodeur, position (ms) -> 0 / < 0 */
long ak_dec_seek(struct akd *d, long ms)
{
	akd_seek_fn f = d->seekfn;
	if (!f)
		return AKE_INVAL;
	if (ms < 0)
		ms = 0;
	u64 fr = ((u64)ms * (u64)d->rate) / 1000; /* trame voulue */
	if (d->total >= 0 && fr > (u64)d->total)
		fr = (u64)d->total;
	d->pcmn = 0;
	d->pcmi = 0;
	d->eof = 0;
	d->skip = 0;
	return f(d, fr);
}

/* décodeur, AKT_*, tampon, taille -> longueur (octets, sans le 0 final ; 0 : absente) */
long ak_dec_tag(struct akd *d, u64 tag, char *buf, u64 size)
{
	if (tag > 2 || !size)
		return 0;
	const u8 *t = d->tags[tag];
	u64 i = 0;
	while (i < size - 1 && t[i]) {
		buf[i] = (char)t[i];
		i++;
	}
	buf[i] = 0;
	return (long)i;
}

long ak_dec_close(struct akd *d)
{
	if (d)
		akd_free(d);
	return 0;
}

/* ---- étiquettes */

/* point de code (< 0x10000) -> UTF-8 en t + *n */
static void tag_utf8(u8 *t, u64 *n, u32 c)
{
	if (c < 0x80) {
		t[(*n)++] = (u8)c;
		return;
	}
	if (c < 0x800) {
		t[(*n)++] = (u8)((c >> 6) | 0xC0);
	} else {
		t[(*n)++] = (u8)((c >> 12) | 0xE0);
		t[(*n)++] = (u8)(((c >> 6) & 0x3F) | 0x80);
	}
	t[(*n)++] = (u8)((c & 0x3F) | 0x80);
}

/* décodeur, AKT_*, texte, octets, codage (0 latin-1, 1 UTF-16 avec BOM, 2
 * UTF-16BE, 3 UTF-8) : pose l'étiquette si elle est encore vide (UTF-8, 255
 * octets au plus, s'arrête au NUL) */
void akd_tag_put(struct akd *d, u64 tag, const u8 *txt, u64 n, u64 enc)
{
	u8 *t = d->tags[tag];
	if (t[0])
		return;
	u64 k = 0;
	uintptr_t end = (uintptr_t)txt + n;
	int be = 1; /* UTF-16 : gros-boutiste */
	if (enc < 1) {
		while ((uintptr_t)txt < end) {
			u8 c = *txt++;
			if (!c || k >= 252)
				break;
			tag_utf8(t, &k, c);
		}
	} else if (enc <= 2) {
		if (enc == 1) { /* BOM : FE FF gros-boutiste, sinon petit */
			if ((uintptr_t)txt + 2 > end)
				goto fin;
			u8 c = txt[0];
			txt += 2;
			be = c == 0xFE;
		}
		for (;;) {
			if ((uintptr_t)txt + 2 > end)
				break;
			u32 c = be ? (u32)txt[1] | ((u32)txt[0] << 8) : (u32)txt[0] | ((u32)txt[1] << 8);
			txt += 2;
			if (!c || k >= 250)
				break;
			if ((c >> 11) == 0x1B) /* demi-codes : remplacés par '?' */
				c = '?';
			tag_utf8(t, &k, c);
		}
	} else {
		while ((uintptr_t)txt < end) {
			u8 c = *txt++;
			if (!c || k >= 254)
				break;
			t[k++] = c;
		}
	}
fin:
	t[k] = 0;
	while (k && t[k - 1] == ' ') { /* retire les espaces de fin (ID3v1) */
		t[k - 1] = 0;
		k--;
	}
}

/* "CLE=valeur", octets, clé (majuscules) -> place de la valeur (0 : autre clé) */
long akd_key(const u8 *s, u64 n, const char *key)
{
	u64 i = 0;
	for (;;) {
		u8 k = (u8)key[i];
		if (!k)
			break;
		if (i >= n)
			return 0;
		u32 c = s[i];
		if (c - 'a' <= 25)
			c -= 32;
		if (c != k)
			return 0;
		i++;
	}
	if (i >= n || s[i] != '=')
		return 0;
	return (long)(i + 1);
}

/* commentaires Vorbis (sans l'en-tête du paquet), octets : TITLE, ARTIST, ALBUM */
void akd_vcomment(struct akd *d, const u8 *p, u64 n)
{
	uintptr_t end = (uintptr_t)p + n;
	if ((uintptr_t)p + 4 > end)
		return;
	u32 v = ak_rd32le(p); /* fournisseur */
	p += 4 + (u64)v;
	if ((uintptr_t)p + 4 > end)
		return;
	u32 cnt = ak_rd32le(p);
	p += 4;
	while (cnt) {
		cnt--;
		if ((uintptr_t)p + 4 > end)
			return;
		u32 len = ak_rd32le(p);
		p += 4;
		const u8 *nx = p + len;
		if ((uintptr_t)nx > end)
			return;
		u64 tag = AKT_TITLE;
		long o = akd_key(p, len, "TITLE");
		if (!o) {
			tag = AKT_ARTIST;
			o = akd_key(p, len, "ARTIST");
		}
		if (!o) {
			tag = AKT_ALBUM;
			o = akd_key(p, len, "ALBUM");
		}
		if (o)
			akd_tag_put(d, tag, p + o, len - (u64)o, 3);
		p = nx;
	}
}

/* données de la trame (codage, texte), octets */
static void id3_put(struct akd *d, u64 tag, const u8 *p, u32 n)
{
	if (!n)
		return;
	u8 enc = p[0];
	if (enc <= 3)
		akd_tag_put(d, tag, p + 1, (u64)n - 1, enc);
}

/* octets disponibles, début de l'étiquette ("ID3") */
void akd_id3v2(struct akd *d, u64 avail, const u8 *tag)
{
	uintptr_t end = (uintptr_t)tag + avail;
	u8 ver = tag[3]; /* 2, 3, 4 */
	const u8 *p = tag + 10;
	if (tag[5] & 0x40) { /* en-tête étendu */
		u32 b1 = p[0], b2 = p[1], b3 = p[2], b4 = p[3];
		u64 sz;
		if (ver == 4)
			sz = (b1 << 21) | (b2 << 14) | (b3 << 7) | b4;
		else /* 2.3 : taille sans elle-même */
			sz = (u64)((b1 << 24) | (b2 << 16) | (b3 << 8) | b4) + 4;
		p += sz;
	}
	if (ver == 2) {
		for (;;) { /* 2.2 : id 3, taille 3 */
			if ((uintptr_t)p + 6 > end || !p[0])
				return;
			u32 sz = ((u32)p[3] << 16) | ((u32)p[4] << 8) | p[5];
			const u8 *nx = p + 6 + sz;
			if ((uintptr_t)nx > end)
				return;
			u32 id = ak_rd32le(p) & 0xFFFFFF;
			if (id == 0x325454) /* TT2 */
				id3_put(d, AKT_TITLE, p + 6, sz);
			else if (id == 0x315054) /* TP1 */
				id3_put(d, AKT_ARTIST, p + 6, sz);
			else if (id == 0x4C4154) /* TAL */
				id3_put(d, AKT_ALBUM, p + 6, sz);
			p = nx;
		}
	}
	for (;;) { /* 2.3 / 2.4 : id 4, taille 4, drapeaux 2 */
		if ((uintptr_t)p + 10 > end || !p[0])
			return;
		u32 b1 = p[4], b2 = p[5], b3 = p[6], b4 = p[7];
		u32 sz;
		if (ver == 4)
			sz = (b1 << 21) | (b2 << 14) | (b3 << 7) | b4;
		else
			sz = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
		const u8 *nx = p + 10 + sz;
		if ((uintptr_t)nx > end)
			return;
		u32 id = ak_rd32le(p);
		if (id == 0x32544954) /* TIT2 */
			id3_put(d, AKT_TITLE, p + 10, sz);
		else if (id == 0x31455054) /* TPE1 */
			id3_put(d, AKT_ARTIST, p + 10, sz);
		else if (id == 0x424C4154) /* TALB */
			id3_put(d, AKT_ALBUM, p + 10, sz);
		p = nx;
	}
}

/* les 128 octets de la fin du fichier : ID3v1 ("TAG") -> 1 s'il y en a un */
long akd_id3v1(struct akd *d, const u8 *t)
{
	if (t[0] != 'T' || t[1] != 'A' || t[2] != 'G')
		return 0;
	akd_tag_put(d, AKT_TITLE, t + 3, 30, 0);
	akd_tag_put(d, AKT_ARTIST, t + 33, 30, 0);
	akd_tag_put(d, AKT_ALBUM, t + 63, 30, 0);
	return 1;
}

/* ---- API par paquets (MediaKit) : docs/KITS.md */

/* format AKF_*, fréquence, canaux, données du codec (extradata), octets -> poignée */
long ak_codec_open(long fmt, long rate, long ch, const void *extra, u64 n)
{
	struct akd *d = ak_alloc(AKD_BYTES);
	if (!d)
		return AKE_NOMEM;
	d->mode = 2;
	d->fmt = fmt;
	d->rate = rate;
	d->ch = ch;
	d->total = -1;
	long r;
	if (fmt == AKF_FLAC)
		r = flac_init_pkt(d, extra, n);
	else if (fmt == AKF_MP3)
		r = mp3_init_pkt(d, extra, n);
	else if (fmt == AKF_VORBIS)
		r = vb_init_pkt(d, extra, n);
	else if (fmt == AKF_OPUS)
		r = ops_init_pkt(d, extra, n);
	else
		r = AKE_FORMAT;
	if (r < 0) {
		akd_free(d);
		return r;
	}
	return (long)d;
}

/* poignée, paquet, octets, sortie entrelacée, trames au plus, AKC_* -> trames */
long ak_codec_decode(struct akd *d, const void *pkt, u64 n, void *out, u64 max, long flags)
{
	if (!d->pktfn)
		return AKE_INVAL;
	long r = d->pktfn(d, pkt, n);
	if (r <= 0)
		return r;
	if ((u64)r > max)
		r = (long)max;
	akd_emit(d, 0, (u64)r, out, flags);
	return r;
}

long ak_codec_reset(struct akd *d)
{
	if (d->resetfn)
		d->resetfn(d);
	return 0;
}

long ak_codec_info(struct akd *d, void *info)
{
	return ak_dec_info(d, info);
}

long ak_codec_close(struct akd *d)
{
	return ak_dec_close(d);
}
