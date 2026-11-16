/* mkexfat.c : formater un volume en exFAT (mkexfat.h ; la spécification exFAT de Microsoft, §3 région de
 * démarrage, §4 FAT, §6 répertoires, §7.1 bitmap, §7.2 majuscules, §7.3 étiquette). */
#include "mkexfat.h"

void *memcpy(void *d, const void *s, __SIZE_TYPE__ n);
void *memset(void *d, int c, __SIZE_TYPE__ n);

#define S EXFAT_SECTOR
#define ALIGN 2048 /* secteurs : 1 Mio */

static const uint16_t upcase[] = {
#include "upcase.inc"
};

static uint8_t buf[EXFAT_WR_MAX * S]; /* tampon de travail (64 Kio) */

static void w16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void w32(uint8_t *p, uint32_t v)
{
	w16(p, v);
	w16(p + 2, v >> 16);
}

static void w64(uint8_t *p, uint64_t v)
{
	w32(p, (uint32_t)v);
	w32(p + 4, (uint32_t)(v >> 32));
}

/* la somme de la spécification (§3.4, §7.2.2) : rotation à droite d'un bit, plus l'octet */
static uint32_t sum_add(uint32_t c, uint8_t b)
{
	return ((c & 1) ? 0x80000000u : 0) + (c >> 1) + b;
}

static uint64_t up(uint64_t v, uint64_t a)
{
	return (v + a - 1) / a * a;
}

/* zéro sur count secteurs à partir de sector */
static long zero(exfat_wr wr, void *ctx, uint64_t sector, uint64_t count)
{
	memset(buf, 0, sizeof buf);
	while (count) {
		uint64_t k = count > EXFAT_WR_MAX ? EXFAT_WR_MAX : count;
		long r = wr(ctx, sector, k, buf);
		if (r)
			return r;
		sector += k;
		count -= k;
	}
	return 0;
}

/* UTF-8 -> UTF-16 (plan de base), 11 caractères au plus -> nombre de caractères */
static int label16(const char *s, uint16_t *o)
{
	int n = 0;
	const uint8_t *p = (const uint8_t *)s;
	while (*p && n < 11) {
		uint32_t c = *p++;
		if (c >= 0xE0 && p[0] && p[1]) {
			c = ((c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
			p += 2;
		} else if (c >= 0xC0 && p[0]) {
			c = ((c & 0x1F) << 6) | (p[0] & 0x3F);
			p++;
		} else if (c >= 0x80) {
			c = '_';
		}
		o[n++] = (uint16_t)c;
	}
	return n;
}

long mkexfat(const struct mkexfat_opts *o, exfat_wr wr, void *ctx, struct mkexfat_info *info)
{
	uint64_t sectors = o->bytes / S;
	if (sectors < 16384) /* 8 Mio */
		return -22;
	uint64_t cb = o->cluster;
	if (!cb)
		cb = o->bytes <= (256ull << 20) ? 4096 : o->bytes <= (32ull << 30) ? 32768 : 131072;
	if (cb < S || cb > (32u << 20) || (cb & (cb - 1)))
		return -22;
	uint64_t spc = cb / S;
	uint32_t shift = 0;
	while ((1ull << shift) < spc)
		shift++;
	/* FAT au secteur 2048, tas aligné sur 1 Mio (et sur un cluster) */
	uint64_t fat_off = ALIGN;
	uint64_t align = spc > ALIGN ? spc : ALIGN;
	uint64_t clusters = (sectors - fat_off) / spc, fat_len = 0, heap = 0;
	for (int i = 0; i < 4; i++) { /* (la FAT réduit le tas : deux passes suffisent) */
		fat_len = up((clusters + 2) * 4, S) / S;
		heap = up(fat_off + fat_len, align);
		if (heap >= sectors)
			return -22;
		clusters = (sectors - heap) / spc;
	}
	if (clusters > 0xFFFFFFF5u || clusters < 16)
		return -22;
	/* le tas : bitmap (cluster 2), majuscules, racine */
	uint64_t bm_bytes = (clusters + 7) / 8;
	uint64_t bm_cl = (bm_bytes + cb - 1) / cb;
	uint64_t up_bytes = sizeof upcase;
	uint64_t up_cl = (up_bytes + cb - 1) / cb;
	uint32_t c_bm = 2, c_up = (uint32_t)(2 + bm_cl), c_root = (uint32_t)(2 + bm_cl + up_cl);
	uint64_t used = bm_cl + up_cl + 1;
	if (used >= clusters)
		return -22;
	long r;

	/* la FAT : 0 et 1 réservés, puis les trois chaînes (le reste à zéro : libre) */
	uint64_t fat_bytes = (used + 2) * 4;
	for (uint64_t done = 0; done < fat_len * S; done += sizeof buf) {
		memset(buf, 0, sizeof buf);
		uint64_t n = fat_len * S - done < sizeof buf ? fat_len * S - done : sizeof buf;
		for (uint64_t e = done / 4; e < (done + n) / 4 && e * 4 < fat_bytes; e++) {
			uint32_t v;
			if (e == 0)
				v = 0xFFFFFFF8u;
			else if (e == 1)
				v = 0xFFFFFFFFu;
			else if (e + 1 == c_up || e + 1 == c_root || e + 1 == c_root + 1)
				v = 0xFFFFFFFFu; /* fin d'une chaîne */
			else
				v = (uint32_t)(e + 1);
			w32(buf + (e * 4 - done), v);
		}
		r = wr(ctx, fat_off + done / S, (n + S - 1) / S, buf);
		if (r)
			return r;
		if (done + n >= fat_bytes && done + n < fat_len * S) { /* le reste de la FAT : des zéros */
			uint64_t s0 = fat_off + (done + n) / S;
			r = zero(wr, ctx, s0, fat_off + fat_len - s0);
			if (r)
				return r;
			break;
		}
	}

	/* le bitmap : les clusters pris, puis des zéros */
	uint64_t bm_sec = heap + (c_bm - 2) * spc;
	for (uint64_t done = 0; done < bm_cl * cb; done += sizeof buf) {
		memset(buf, 0, sizeof buf);
		uint64_t n = bm_cl * cb - done < sizeof buf ? bm_cl * cb - done : sizeof buf;
		for (uint64_t b = done * 8; b < used && b < (done + n) * 8; b++)
			buf[b / 8 - done] |= (uint8_t)(1u << (b % 8));
		r = wr(ctx, bm_sec + done / S, n / S, buf);
		if (r)
			return r;
		if (done + n >= (used + 7) / 8 && done + n < bm_cl * cb) {
			uint64_t s0 = bm_sec + (done + n) / S;
			r = zero(wr, ctx, s0, bm_sec + bm_cl * spc - s0);
			if (r)
				return r;
			break;
		}
	}

	/* la table des majuscules (et sa somme) */
	uint32_t up_sum = 0;
	uint64_t up_sec = heap + (uint64_t)(c_up - 2) * spc;
	for (uint64_t done = 0; done < up_cl * cb; done += sizeof buf) {
		memset(buf, 0, sizeof buf);
		uint64_t n = up_cl * cb - done < sizeof buf ? up_cl * cb - done : sizeof buf;
		for (uint64_t i = done; i < done + n && i < up_bytes; i++) {
			uint8_t b = (uint8_t)(upcase[i / 2] >> ((i & 1) * 8));
			buf[i - done] = b;
			up_sum = sum_add(up_sum, b);
		}
		r = wr(ctx, up_sec + done / S, n / S, buf);
		if (r)
			return r;
	}

	/* la racine : étiquette (0x83, ou 0x03 sans étiquette), bitmap (0x81), majuscules (0x82) */
	uint64_t root_sec = heap + (uint64_t)(c_root - 2) * spc;
	{
		uint64_t n = cb < sizeof buf ? cb : sizeof buf;
		memset(buf, 0, sizeof buf);
		uint8_t *e = buf;
		uint16_t l16[11];
		int ln = o->label ? label16(o->label, l16) : 0;
		e[0] = ln ? 0x83 : 0x03;
		e[1] = (uint8_t)ln;
		for (int i = 0; i < ln; i++)
			w16(e + 2 + 2 * i, l16[i]);
		e += 32;
		e[0] = 0x81;
		w32(e + 20, c_bm);
		w64(e + 24, bm_bytes);
		e += 32;
		e[0] = 0x82;
		w32(e + 4, up_sum);
		w32(e + 20, c_up);
		w64(e + 24, up_bytes);
		r = wr(ctx, root_sec, n / S, buf);
		if (r)
			return r;
		if (cb > n) {
			r = zero(wr, ctx, root_sec + n / S, (cb - n) / S);
			if (r)
				return r;
		}
	}

	/* la région de démarrage : secteurs 0 à 11, puis la copie 12 à 23 (la somme en dernier) */
	r = zero(wr, ctx, 24, fat_off - 24); /* (entre la copie et la FAT : rien d'ancien) */
	if (r)
		return r;
	memset(buf, 0, 12 * S);
	uint8_t *b = buf;
	b[0] = 0xEB;
	b[1] = 0x76;
	b[2] = 0x90;
	memcpy(b + 3, "EXFAT   ", 8);
	w64(b + 64, o->part_offset);
	w64(b + 72, sectors);
	w32(b + 80, (uint32_t)fat_off);
	w32(b + 84, (uint32_t)fat_len);
	w32(b + 88, (uint32_t)heap);
	w32(b + 92, (uint32_t)clusters);
	w32(b + 96, c_root);
	w32(b + 100, o->serial);
	w16(b + 104, 0x0100); /* révision 1.00 */
	w16(b + 106, 0);      /* VolumeFlags */
	b[108] = 9;           /* 512 octets par secteur */
	b[109] = (uint8_t)shift;
	b[110] = 1; /* une FAT */
	b[111] = 0x80;
	b[112] = (uint8_t)(used * 100 / clusters); /* PercentInUse */
	b[510] = 0x55;
	b[511] = 0xAA;
	for (int i = 1; i <= 8; i++) { /* secteurs de démarrage étendus : leur signature */
		b[i * S + 510] = 0x55;
		b[i * S + 511] = 0xAA;
	}
	uint32_t sum = 0;
	for (uint64_t i = 0; i < 11 * S; i++)
		if (i != 106 && i != 107 && i != 112)
			sum = sum_add(sum, b[i]);
	for (int i = 0; i < S / 4; i++)
		w32(b + 11 * S + 4 * i, sum);
	memcpy(buf + 12 * S, buf, 12 * S);
	r = wr(ctx, 0, 24, buf);
	if (r)
		return r;
	if (info) {
		info->sectors = sectors;
		info->fat_offset = fat_off;
		info->fat_length = fat_len;
		info->heap_offset = heap;
		info->clusters = clusters;
		info->cluster_bytes = cb;
	}
	return 0;
}
