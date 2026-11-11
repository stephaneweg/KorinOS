/* ArchiveKit : compression et archives (docs/KITS.md), en C commun au Pi et
 * au PC ; remplace archivekit.asm et ses .inc, même table (archivekit.abi),
 * mêmes résultats, mêmes codes d'erreur, mêmes octets compressés.
 *
 *  * CRC-32, Adler-32 ;
 *  * deflate (ar_deflate.c), inflate, zlib, gzip (ar_inflate.c) ;
 *  * archives lues, sur la carte ou en mémoire (ar_read.c) : ZIP, ZIP64,
 *    tar, tar.gz, gz ; archives écrites (ar_write.c) : ZIP, tar, tar.gz, gz.
 *
 * Poignées opaques (des blocs du kit, reconnus à leur premier mot), erreurs
 * négatives. Aucune variable globale hors de la liaison à AppKit (relais
 * générés) : deux threads peuvent travailler en même temps sur deux poignées.
 *
 * Mémoire : pages de VM_MAP, à zéro, peuplées à la demande : les grands états
 * (deflate 0,6 Mo, inflate 0,35 Mo) ne coûtent que les pages touchées.
 * Pas de TLS, pas de chiffrement : une entrée chiffrée rend AR_E_CRYPT. */
#include "ar.h"

/* --- mémoire ------------------------------------------------------------- */

/* octets -> bloc à zéro (taille rangée 16 octets avant), 0 si échec */
void *ar_alloc(u64 n)
{
	u64 sz = (n + 16 + 4095) & ~(u64)4095;
	long p = korin_sys4(SYS_VM_MAP, 0, sz, PROT_READ | PROT_WRITE, 0);
	if (p < 0)
		return 0;
	*(u64 *)p = sz;
	return (u8 *)p + 16;
}

void ar_mfree(void *p)
{
	if (!p)
		return;
	u8 *b = (u8 *)p - 16;
	korin_sys2(SYS_VM_UNMAP, b, *(u64 *)b);
}

/* vers l'avant (recouvrement permis si destination < source) */
void ar_memcpy(void *d, const void *s, u64 n)
{
	u8 *a = d;
	const u8 *b = s;
	while (n >= 8) {
		u64 t;
		__builtin_memcpy(&t, b, 8);
		__builtin_memcpy(a, &t, 8);
		a += 8;
		b += 8;
		n -= 8;
	}
	while (n--)
		*a++ = *b++;
}

/* --- sommes ----------------------------------------------------------------- */

#if defined(__aarch64__)
/* instructions CRC d'ARMv8 (Cortex-A72 et A76), comme l'asm */
__attribute__((target("+crc"))) u32 crc32_upd(u32 crc, const u8 *p, u64 n)
{
	crc = ~crc;
	while (n >= 8) {
		u64 v;
		__builtin_memcpy(&v, p, 8);
		crc = __builtin_aarch64_crc32x(crc, v);
		p += 8;
		n -= 8;
	}
	while (n--)
		crc = __builtin_aarch64_crc32b(crc, *p++);
	return ~crc;
}
#else
static u32 crc_tab[256];
static int crc_ready;

u32 crc32_upd(u32 crc, const u8 *p, u64 n)
{
	if (!crc_ready) {
		for (u32 i = 0; i < 256; i++) {
			u32 c = i;
			for (int k = 0; k < 8; k++)
				c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
			crc_tab[i] = c;
		}
		crc_ready = 1;
	}
	crc = ~crc;
	while (n--)
		crc = crc_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return ~crc;
}
#endif

u32 adler32_upd(u32 adler, const u8 *p, u64 n)
{
	u32 a = adler & 0xFFFF, b = adler >> 16;
	while (n) {
		u64 k = n < 5552 ? n : 5552; /* (au-delà, b déborderait 32 bits) */
		n -= k;
		do {
			a += *p++;
			b += a;
		} while (--k);
		a %= 65521;
		b %= 65521;
	}
	return a | b << 16;
}

long ar_crc32(long crc, long data, long n)
{
	return crc32_upd((u32)crc, (const u8 *)data, (u64)n);
}

long ar_adler32(long adler, long data, long n)
{
	return adler32_upd((u32)adler, (const u8 *)data, (u64)n);
}

/* --- puits ------------------------------------------------------------------ */

/* -> 0, erreur, AR_STOP */
long sink_write(struct sink *k, const void *data, u64 n)
{
	const u8 *p = data;
	u64 pos, room;
	if (k->kind == SKK_FD) {
		while (n) {
			long r = f_ecrire((long)k->ptr, (long)p, (long)n);
			if (r <= 0)
				return r < 0 ? r : AR_E_IO;
			p += r;
			n -= (u64)r;
			k->pos += (u64)r;
		}
		return 0;
	}
	if (k->kind == SKK_GROW) {
		u64 need = k->pos + n + 1; /* (la place d'un 0 final) */
		if (need > k->cap) {
			u64 nc = k->cap << 1;
			if (nc < 65536)
				nc = 65536;
			if (nc < need)
				nc = need; /* nouvelle capacité */
			u8 *b = ar_alloc(nc);
			if (!b)
				return AR_E_NOMEM;
			if (k->ptr) {
				ar_memcpy(b, (u8 *)k->ptr, k->pos);
				ar_mfree((u8 *)k->ptr);
			}
			k->ptr = (u64)b;
			k->cap = nc;
		}
		goto copy;
	}
	pos = k->pos;
	if (k->kind == SKK_NULL) {
		k->pos = pos + n;
		return 0;
	}
	room = k->cap - pos;
	if (k->kind == SKK_PROBE) {
		if (n > room)
			n = room;
		ar_memcpy((u8 *)k->ptr + pos, p, n);
		k->pos += n;
		return k->pos < k->cap ? 0 : AR_STOP;
	}
	if (n > room)
		return AR_E_SPACE;
copy:
	ar_memcpy((u8 *)k->ptr + k->pos, p, n);
	k->pos += n;
	return 0;
}

/* puits en mémoire : un 0 après les données -> 0, erreur */
long sink_zero(struct sink *k)
{
	u8 z = 0;
	long r = sink_write(k, &z, 1);
	if (r)
		return r;
	k->pos--;
	return 0;
}

/* --- tampons du kit ------------------------------------------------------------ */

/* -> tampon vide, 0 si plus de mémoire */
struct arbuf *buf_new(void)
{
	struct arbuf *b = ar_alloc(4096 - 16);
	if (!b)
		return 0;
	b->magic = AR_MAGIC_BUF;
	b->sink.kind = SKK_GROW;
	return b;
}

/* ses octets terminés (un 0 après), data et size mis -> tampon, erreur */
long buf_done(struct arbuf *b)
{
	long r = sink_zero(&b->sink);
	if (r < 0) {
		ar_free((long)b);
		return r;
	}
	b->data = b->sink.ptr;
	b->size = b->sink.pos;
	return (long)b;
}

/* poignée : tampon ou écrivain, sinon 0 */
static int h_ok(long h)
{
	if (h <= 0)
		return 0;
	u64 m = *(u64 *)h;
	return m == AR_MAGIC_BUF || m == AR_MAGIC_WR;
}

long ar_data(long h)
{
	if (!h_ok(h))
		return AR_E_INVAL;
	return (long)((struct arbuf *)h)->data;
}

long ar_bytes(long h)
{
	if (!h_ok(h))
		return AR_E_INVAL;
	return (long)((struct arbuf *)h)->size;
}

/* tampon, écrivain ou archive -> 0 */
long ar_free(long h)
{
	if (!h)
		return 0;
	if (h < 0)
		return AR_E_INVAL;
	u64 m = *(u64 *)h;
	if (m == AR_MAGIC_BUF) {
		struct arbuf *b = (struct arbuf *)h;
		b->magic = 0;
		ar_mfree((u8 *)b->sink.ptr);
		ar_mfree(b);
		return 0;
	}
	if (m == AR_MAGIC_RD)
		return ar_close(h);
	if (m == AR_MAGIC_WR)
		return wr_free((void *)h);
	return AR_E_INVAL;
}

/* --- compression d'un bloc de mémoire -------------------------------------------- */

long ar_deflate_bound(long n, long wrap)
{
	(void)wrap;
	return (long)((u64)n + ((u64)n >> 12) + 128);
}

/* niveau -> 0..9, négatif si invalide */
long ar_level_of(long level)
{
	if (level == -1)
		return AR_DEFAULT;
	if ((u64)level <= 9)
		return level;
	return AR_E_INVAL;
}

/* l'en-tête de l'enveloppe -> 0, erreur */
long wrap_head(struct sink *k, u64 wrap, u64 level)
{
	u8 h[16];
	if (wrap == AR_ZLIB) {
		h[0] = 0x78;
		h[1] = level <= 1 ? 0x01 : level <= 5 ? 0x5E : level <= 6 ? 0x9C : 0xDA;
		return sink_write(k, h, 2);
	}
	if (wrap == AR_GZIP) {
		h[0] = 0x1F;
		h[1] = 0x8B;
		h[2] = 8;
		h[3] = 0;
		h[4] = h[5] = h[6] = h[7] = 0;
		u8 x = 0;
		if (level == 9)
			x = 2;
		if (level == 1)
			x = 4;
		h[8] = x;
		h[9] = 3; /* Unix */
		return sink_write(k, h, 10);
	}
	return 0;
}

/* la fin de l'enveloppe d'un état deflate terminé -> 0, erreur */
long wrap_tail(struct sink *k, struct defl *s)
{
	u8 t[8];
	if (s->wrap == AR_ZLIB) {
		u32 a = (u32)s->adler;
		t[0] = (u8)(a >> 24);
		t[1] = (u8)(a >> 16);
		t[2] = (u8)(a >> 8);
		t[3] = (u8)a;
		return sink_write(k, t, 4);
	}
	if (s->wrap == AR_GZIP) {
		u32 c = (u32)s->crc, n = (u32)s->total;
		for (int i = 0; i < 4; i++) {
			t[i] = (u8)(c >> (8 * i));
			t[4 + i] = (u8)(n >> (8 * i));
		}
		return sink_write(k, t, 8);
	}
	return 0;
}

/* puits, source, octets, enveloppe, niveau -> 0, erreur */
static long defl_all(struct sink *k, const u8 *src, u64 n, u64 wrap, u64 level)
{
	struct defl *s = df_new(k);
	if (!s)
		return AR_E_NOMEM;
	long r = wrap_head(k, wrap, level);
	if (!r) {
		df_begin(s, level, wrap);
		r = df_feed(s, src, n);
		if (!r) {
			r = df_finish(s);
			if (!r)
				r = wrap_tail(k, s);
		}
	}
	ar_mfree(s);
	return r;
}

long ar_deflate(long src, long n, long out, long cap, long wrap, long level)
{
	if ((u64)wrap > AR_GZIP)
		return AR_E_INVAL;
	long lv = ar_level_of(level);
	if (lv < 0)
		return AR_E_INVAL;
	struct sink k = {SKK_FIXED, (u64)out, 0, (u64)cap};
	long r = defl_all(&k, (const u8 *)src, (u64)n, (u64)wrap, (u64)lv);
	if (r)
		return r;
	return (long)k.pos;
}

long ar_deflate_new(long src, long n, long wrap, long level)
{
	long lv = ar_level_of(level);
	if (lv < 0)
		return lv;
	if ((u64)wrap > AR_GZIP)
		return AR_E_INVAL;
	struct arbuf *b = buf_new();
	if (!b)
		return AR_E_NOMEM;
	long r = defl_all(&b->sink, (const u8 *)src, (u64)n, (u64)wrap, (u64)lv);
	if (r) {
		ar_free((long)b);
		return r;
	}
	return buf_done(b);
}

/* source, octets, sortie, capacité, puits, enveloppe -> octets */
static long infl_all(const u8 *src, u64 n, u8 *out, u64 cap, struct sink *k, u64 wrap)
{
	if (wrap > AR_AUTO)
		return AR_E_INVAL;
	struct infl *s = ar_alloc(sizeof(struct infl));
	if (!s)
		return AR_E_NOMEM;
	if_begin(s, src, n, out, cap, k, wrap);
	long r = if_run(s);
	ar_mfree(s);
	return r;
}

long ar_inflate(long src, long n, long out, long cap, long wrap)
{
	return infl_all((const u8 *)src, (u64)n, (u8 *)out, (u64)cap, 0, (u64)wrap);
}

long ar_inflate_size(long src, long n, long wrap)
{
	struct sink k = {0, 0, 0, 0};
	return infl_all((const u8 *)src, (u64)n, 0, 0, &k, (u64)wrap);
}

long ar_inflate_new(long src, long n, long wrap, long expect)
{
	struct arbuf *b = buf_new();
	if (!b)
		return AR_E_NOMEM;
	if (expect) {
		b->sink.cap = (u64)expect + 1; /* (taille annoncée par l'appelant) */
		b->sink.ptr = (u64)ar_alloc((u64)expect + 1);
		if (!b->sink.ptr)
			b->sink.cap = 0;
	}
	long r = infl_all((const u8 *)src, (u64)n, 0, 0, &b->sink, (u64)wrap);
	if (r < 0) {
		ar_free((long)b);
		return r;
	}
	return buf_done(b);
}
