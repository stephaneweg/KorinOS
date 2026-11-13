//
// rvz.cpp -- RVZ et WIA (images de disque compressées de Dolphin), GameCube seulement, lus à la demande.
// Écrit d'après la description publique du format (docs/WiaAndRvz.md de Dolphin), sans reprendre son code (GPL).
//
// Le fichier (grand-boutiste) :
//   * en-tête 1 (0x48 o) : magique « WIA\1 » ou « RVZ\1 », versions, taille de l'en-tête 2, taille de l'ISO ;
//   * en-tête 2 : type de disque (1 GameCube), compression (0 aucune, 2 bzip2, 3 LZMA, 4 LZMA2, 5 Zstandard),
//     taille d'un groupe (chunk), les 0x80 premiers octets du disque, tables des partitions (Wii), des zones
//     brutes et des groupes ;
//   * zones brutes (24 o chacune, table compressée) : décalage et taille sur le disque, premier groupe, groupes ;
//     la zone commence en fait au multiple de 0x8000 au-dessous de son décalage ; chaque groupe en couvre un
//     chunk (le dernier peut être plus court) ;
//   * groupes (WIA 8 o, RVZ 12 o, table compressée) : décalage dans le fichier / 4, taille (RVZ : bit 31 =
//     compressé, sinon stocké tel quel ; 0 : que des zéros), RVZ : taille « empaquetée » (0 : pas empaqueté) ;
//   * empaquetage de RVZ (après décompression) : des enregistrements u32 taille ; bit 31 : données de
//     remplissage du disque (taille & 0x7FFFFFFF octets), rendues par le générateur de Fibonacci retardé des
//     disques de Nintendo depuis une graine de 17 u32 qui suit, avancé de (position sur le disque % 0x8000) ;
//     sinon, la taille d'octets tels quels.
// Pris en charge : sans compression et Zstandard (le défaut de Dolphin pour RVZ) ; bzip2, LZMA, LZMA2 et la
// « purge » de WIA sont refusés avec un message. Pas de Wii (partitions chiffrées).
// Un petit cache de groupes décodés (lectures du DVD surtout à la suite).
//
#include "rvz.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include <zstd.h>

namespace {

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

inline u32 be32 (const u8 *p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }
inline u64 be64 (const u8 *p) { return (u64) be32 (p) << 32 | be32 (p + 4); }

// le générateur des données de remplissage (Fibonacci retardé, x[n] = x[n-521] ^ x[n-32]) : 521 mots, rendus
// en octets grand-boutistes, les bits 16..23 de chaque mot pris aux bits 18..25 (particularité de Nintendo)
struct Junk
{
	enum { K = 521, J = 32, BYTES = K * 4 };
	u32 w[K];
	u32 pos;				// octets déjà rendus du bloc courant
	void step ()
	{
		for (int i = 0; i < J; i++) w[i] ^= w[i + K - J];
		for (int i = J; i < K; i++) w[i] ^= w[i - J];
	}
	void seed (const u8 *s)		// 17 mots grand-boutistes
	{
		for (int i = 0; i < 17; i++) w[i] = be32 (s + 4 * i);
		for (int i = 17; i < K; i++) w[i] = (w[i - 17] << 23) ^ (w[i - 16] >> 9) ^ w[i - 1];
		for (int i = 0; i < K; i++)
		{
			u32 x = (w[i] & 0xFF00FFFFu) | ((w[i] >> 2) & 0x00FF0000u);
			w[i] = __builtin_bswap32 (x);	// (rangés pour une copie octet à octet sur une machine petit-boutiste)
		}
		for (int i = 0; i < 4; i++) step ();
		pos = 0;
	}
	void skip (u64 n)
	{
		n += pos;
		while (n >= BYTES) { step (); n -= BYTES; }
		pos = (u32) n;
	}
	void get (u8 *out, u64 n)
	{
		while (n)
		{
			u32 k = BYTES - pos;
			if (k > n) k = (u32) n;
			memcpy (out, (const u8 *) w + pos, k);
			out += k; n -= k; pos += k;
			if (pos == BYTES) { step (); pos = 0; }
		}
	}
};

struct Raw { u64 start, end; u32 group, ngroups; };	// (start : déjà ramené au multiple de 0x8000)
struct Group { u64 off; u32 size; bool packed_cmp; u32 packed; };

enum { CACHE = 4, ALIGN = 0x8000 };

} // namespace

struct Rvz
{
	FILE *f;
	bool rvz;
	u32 comp, chunk;
	u64 iso_size;
	u8 dhead[0x80];
	Raw *raws; u32 nraw;
	Group *groups; u32 ngroups;
	u8 *cbuf; u32 cbufCap;		// données lues du fichier
	u8 *pbuf; u32 pbufCap;		// données empaquetées (RVZ)
	struct { long group; u32 len; u8 *data; unsigned age; } cache[CACHE];
	unsigned tick;
	ZSTD_DCtx *z;
	Junk junk;
};

static bool fread_at (FILE *f, u64 off, void *dst, size_t n)
{
	return !fseeko (f, (off_t) off, SEEK_SET) && fread (dst, 1, n, f) == n;
}

// src (n octets) décompressé selon la compression de l'image -> exactement want octets dans dst
static bool unpack_data (Rvz *r, const u8 *src, u32 n, u8 *dst, u32 want, bool compressed)
{
	if (!compressed || r->comp == 0)
	{
		if (n < want) return false;
		memcpy (dst, src, want);
		return true;
	}
	if (r->comp == 5)
	{
		size_t got = ZSTD_decompressDCtx (r->z, dst, want, src, n);
		return !ZSTD_isError (got) && got == want;
	}
	return false;
}

// une table compressée du fichier (off, taille stockée) -> want octets (malloc), 0 si illisible
static u8 *read_table (Rvz *r, u64 off, u32 stored, u32 want)
{
	u8 *in = (u8 *) malloc (stored ? stored : 1), *out = (u8 *) malloc (want ? want : 1);
	bool ok = in && out && fread_at (r->f, off, in, stored) && unpack_data (r, in, stored, out, want, true);
	free (in);
	if (!ok) { free (out); return 0; }
	return out;
}

bool rvz_is (const unsigned char *h)
{
	return (h[0] == 'R' && h[1] == 'V' && h[2] == 'Z' && h[3] == 1) || (h[0] == 'W' && h[1] == 'I' && h[2] == 'A' && h[3] == 1);
}

void rvz_close (Rvz *r)
{
	if (!r) return;
	if (r->f) fclose (r->f);
	free (r->raws); free (r->groups); free (r->cbuf); free (r->pbuf);
	for (int i = 0; i < CACHE; i++) free (r->cache[i].data);
	if (r->z) ZSTD_freeDCtx (r->z);
	delete r;
}

Rvz *rvz_open (FILE *f, const char **err)
{
	static const char *e;
	e = "Not a RVZ / WIA disc image.";
	if (err) *err = e;
	u8 h1[0x48];
	if (!fread_at (f, 0, h1, sizeof h1) || !rvz_is (h1)) return 0;
	Rvz *r = new (std::nothrow) Rvz ();
	if (!r) return 0;
	r->f = f;
	r->rvz = h1[0] == 'R';
	u32 h2size = be32 (h1 + 0x0C);
	r->iso_size = be64 (h1 + 0x24);
	u8 h2[0x100];
	memset (h2, 0, sizeof h2);
	if (h2size < 0xDC || !fread_at (f, 0x48, h2, h2size < sizeof h2 ? h2size : sizeof h2)) { r->f = 0; rvz_close (r); return 0; }
	u32 disc_type = be32 (h2);
	r->comp = be32 (h2 + 4);
	r->chunk = be32 (h2 + 0x0C);
	memcpy (r->dhead, h2 + 0x10, 0x80);
	u32 npart = be32 (h2 + 0x90);
	r->nraw = be32 (h2 + 0xB4);
	u64 raw_off = be64 (h2 + 0xB8);
	u32 raw_size = be32 (h2 + 0xC0);
	r->ngroups = be32 (h2 + 0xC4);
	u64 group_off = be64 (h2 + 0xC8);
	u32 group_size = be32 (h2 + 0xD0);
	if (disc_type != 1 || npart) e = "Wii disc images are not supported (GameCube only).";
	else if (r->comp != 0 && r->comp != 5)
		e = r->comp == 1 ? "WIA \"purge\" compression is not supported (convert to RVZ with zstd)." :
			"Only uncompressed or Zstandard RVZ / WIA images are supported (convert with zstd).";
	else if (r->chunk < ALIGN || r->chunk > (32u << 20) || r->nraw == 0 || r->nraw > 4096 || r->ngroups > (1u << 24) ||
		 r->iso_size < 0x2460 || r->iso_size > (8ull << 30))
		e = "Damaged RVZ / WIA header.";
	else
		e = 0;
	if (err) *err = e ? e : "Damaged RVZ / WIA image.";
	if (e || !(r->z = ZSTD_createDCtx ())) { r->f = 0; rvz_close (r); return 0; }
	u8 *t = read_table (r, raw_off, raw_size, r->nraw * 24);
	r->raws = (Raw *) malloc (r->nraw * sizeof (Raw));
	bool ok = t && r->raws;
	for (u32 i = 0; ok && i < r->nraw; i++)
	{
		u64 off = be64 (t + i * 24), size = be64 (t + i * 24 + 8);
		r->raws[i].start = off - off % ALIGN;
		r->raws[i].end = off + size;
		r->raws[i].group = be32 (t + i * 24 + 16);
		r->raws[i].ngroups = be32 (t + i * 24 + 20);
		if ((u64) r->raws[i].group + r->raws[i].ngroups > r->ngroups) ok = false;
	}
	free (t);
	u32 gs = r->rvz ? 12 : 8;
	t = ok ? read_table (r, group_off, group_size, r->ngroups * gs) : 0;
	r->groups = (Group *) malloc ((r->ngroups ? r->ngroups : 1) * sizeof (Group));
	ok = t && r->groups;
	for (u32 i = 0; ok && i < r->ngroups; i++)
	{
		const u8 *g = t + i * gs;
		u32 sz = be32 (g + 4);
		r->groups[i].off = (u64) be32 (g) << 2;
		r->groups[i].size = r->rvz ? sz & 0x7FFFFFFFu : sz;
		r->groups[i].packed_cmp = r->rvz ? (sz & 0x80000000u) != 0 : true;
		r->groups[i].packed = r->rvz ? be32 (g + 8) : 0;
	}
	free (t);
	for (int i = 0; ok && i < CACHE; i++)
	{
		r->cache[i].group = -1;
		ok = (r->cache[i].data = (u8 *) malloc (r->chunk)) != 0;
	}
	if (!ok) { r->f = 0; rvz_close (r); return 0; }
	if (err) *err = 0;
	return r;
}

uint64_t rvz_disc_size (const Rvz *r) { return r->iso_size; }

static bool grow (u8 **b, u32 *cap, u32 n)
{
	if (n <= *cap) return true;
	u8 *p = (u8 *) realloc (*b, n);
	if (!p) return false;
	*b = p; *cap = n;
	return true;
}

// le groupe gi (chunk de len octets, à la position disque pos) -> dst
static bool decode_group (Rvz *r, u32 gi, u64 pos, u32 len, u8 *dst)
{
	const Group &g = r->groups[gi];
	if (g.size == 0) { memset (dst, 0, len); return true; }
	if (!grow (&r->cbuf, &r->cbufCap, g.size) || !fread_at (r->f, g.off, r->cbuf, g.size)) return false;
	if (!g.packed) return unpack_data (r, r->cbuf, g.size, dst, len, g.packed_cmp);
	if (!grow (&r->pbuf, &r->pbufCap, g.packed) || !unpack_data (r, r->cbuf, g.size, r->pbuf, g.packed, g.packed_cmp))
		return false;
	u32 in = 0, out = 0;
	while (out < len)
	{
		if (in + 4 > g.packed) return false;
		u32 sz = be32 (r->pbuf + in), n = sz & 0x7FFFFFFFu;
		in += 4;
		if (n > len - out) return false;
		if (sz & 0x80000000u)
		{
			if (in + 68 > g.packed) return false;
			r->junk.seed (r->pbuf + in);
			in += 68;
			r->junk.skip ((pos + out) % ALIGN);
			r->junk.get (dst + out, n);
		}
		else
		{
			if (in + n > g.packed) return false;
			memcpy (dst + out, r->pbuf + in, n);
			in += n;
		}
		out += n;
	}
	return true;
}

// position disque -> le groupe qui la couvre (*start : son début, *len : sa taille) ; -1 hors des zones
static long find_group (const Rvz *r, u64 off, u64 *start, u32 *len)
{
	for (u32 i = 0; i < r->nraw; i++)
	{
		const Raw &w = r->raws[i];
		if (off < w.start || off >= w.end) continue;
		u64 k = (off - w.start) / r->chunk;
		if (k >= w.ngroups) return -1;
		*start = w.start + k * r->chunk;
		u64 e = *start + r->chunk;
		*len = (u32) ((e < w.end ? e : w.end) - *start);
		return (long) (w.group + k);
	}
	return -1;
}

bool rvz_read (Rvz *r, uint64_t off, uint32_t len, unsigned char *dst)
{
	if (off + len > r->iso_size) return false;
	while (len)
	{
		if (off < 0x80)		// (les 0x80 premiers octets : dans l'en-tête)
		{
			u32 n = (u32) (0x80 - off);
			if (n > len) n = len;
			memcpy (dst, r->dhead + off, n);
			off += n; dst += n; len -= n;
			continue;
		}
		u64 start; u32 glen;
		long gi = find_group (r, off, &start, &glen);
		u32 n;
		if (gi < 0)		// (hors des zones : des zéros, jusqu'à la zone suivante)
		{
			u64 next = r->iso_size;
			for (u32 i = 0; i < r->nraw; i++) if (r->raws[i].start > off && r->raws[i].start < next) next = r->raws[i].start;
			n = next - off < len ? (u32) (next - off) : len;
			memset (dst, 0, n);
		}
		else
		{
			int c = -1, old = 0;
			for (int i = 0; i < CACHE; i++)
			{
				if (r->cache[i].group == gi) c = i;
				if (r->cache[i].age < r->cache[old].age) old = i;
			}
			if (c < 0)
			{
				c = old;
				r->cache[c].group = -1;
				if (!decode_group (r, (u32) gi, start, glen, r->cache[c].data)) return false;
				r->cache[c].group = gi;
				r->cache[c].len = glen;
			}
			r->cache[c].age = ++r->tick;
			u32 in = (u32) (off - start);
			n = glen - in < len ? glen - in : len;
			memcpy (dst, r->cache[c].data + in, n);
		}
		off += n; dst += n; len -= n;
	}
	return true;
}
