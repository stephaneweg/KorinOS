/* lz4.c : LZ4 (format bloc : suites de littéraux + une copie d'au moins 4 octets
 * à 64 Ko au plus en arrière), le codeur simple de rdpd.c d'Onyx ; traduction
 * de lz4_compress (rdpd.asm), mêmes choix, donc mêmes octets : hachage de
 * Knuth sur 12 bits (0x9E3779B1), table remise à -1 à chaque appel, pas de
 * 1 + (échecs >> 5), dernière correspondance à n - 12, extension jusqu'à n - 5
 * (8 octets à la fois, puis octet par octet). */
#include "korinrt.h"
#include "lz4.h"

typedef struct __attribute__((packed)) {
	u32 v;
} un32;
typedef struct __attribute__((packed)) {
	u64 v;
} un64;

static u32 ld32(const u8 *p)
{
	return ((const un32 *)p)->v;
}

static u64 ld64(const u8 *p)
{
	return ((const un64 *)p)->v;
}

/* une longueur de plus de 15 : octets de 255, puis le reste */
static u8 *put_len(u8 *op, u64 k)
{
	while (k >= 255) {
		*op++ = 255;
		k -= 255;
	}
	*op++ = (u8)k;
	return op;
}

u64 lz4_compress(const u8 *src, u64 n, u8 *dst, u32 *htab)
{
	u8 *op = dst;
	memset(htab, 0xFF, LZ4_HT_BYTES); /* table : -1 partout */
	long ip = 0, anchor = 0;
	u64 miss = 0;
	long limit = (long)n - 12; /* ip < n - 12 (comparaison signée) */
	while (ip < limit) {
		u32 seq = ld32(src + ip);
		u32 h = (seq * 0x9E3779B1u) >> 20;
		u32 ref = htab[h];
		htab[h] = (u32)ip;
		/* distance <= 65535, mêmes 4 octets */
		if (ref == 0xFFFFFFFF || ((u64)ip - ref) >> 16 || ld32(src + ref) != seq) {
			ip += (long)(miss >> 5) + 1; /* ip += 1 + (miss++ >> 5) */
			miss++;
			continue;
		}
		miss = 0;
		long ml = 4, lim = (long)n - 5;
		const u8 *r = src + ref, *p = src + ip;
		for (;;) {
			if (ip + ml + 8 > lim) { /* la fin : octet par octet */
				while (ip + ml < lim && r[ml] == p[ml])
					ml++;
				break;
			}
			u64 x = ld64(r + ml) ^ ld64(p + ml);
			if (x) {
				ml += __builtin_ctzll(x) >> 3;
				break;
			}
			ml += 8;
		}
		u64 lit = (u64)(ip - anchor), mlc = (u64)ml - 4; /* littéraux, ml - 4 */
		*op++ = (u8)((lit < 15 ? lit : 15) << 4 | (mlc < 15 ? mlc : 15)); /* le jeton */
		if (lit >= 15)
			op = put_len(op, lit - 15);
		memcpy(op, src + anchor, lit);
		op += lit;
		u64 d = (u64)ip - ref; /* distance */
		*op++ = (u8)d;
		*op++ = (u8)(d >> 8);
		if (mlc >= 15)
			op = put_len(op, mlc - 15);
		ip += ml;
		anchor = ip;
	}
	u64 lit = n - (u64)anchor; /* les derniers littéraux */
	*op++ = (u8)((lit < 15 ? lit : 15) << 4);
	if (lit >= 15)
		op = put_len(op, lit - 15);
	memcpy(op, src + anchor, lit);
	op += lit;
	return (u64)(op - dst);
}
