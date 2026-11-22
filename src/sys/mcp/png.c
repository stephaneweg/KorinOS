/* png.c -- une image RGB en PNG pour korin-mcp (aucun kit de Korin n'écrit de PNG) : filtre Sub ou Up par ligne
 * (le plus petit en somme absolue), deflate à codes de Huffman fixes avec un LZ77 à table de hachage (une chaîne
 * courte) : un écran du bureau (aplats, texte) se réduit d'un facteur 10 à 40, en quelques dizaines de ms. */
#include "png.h"

#include <stdlib.h>
#include <string.h>

/* ---- sortie de bits (LSB d'abord, comme deflate) ---- */
struct bits {
	uint8_t *p;
	size_t n, cap;
	uint32_t acc;
	int nb;
	int oom;
};

static void put_byte(struct bits *o, uint8_t v)
{
	if (o->n >= o->cap) {
		size_t cap = o->cap ? o->cap * 2 : 1 << 16;
		uint8_t *p = realloc(o->p, cap);
		if (!p) {
			o->oom = 1;
			return;
		}
		o->p = p;
		o->cap = cap;
	}
	o->p[o->n++] = v;
}

static void put_bits(struct bits *o, uint32_t v, int n)
{
	o->acc |= v << o->nb;
	o->nb += n;
	while (o->nb >= 8) {
		put_byte(o, (uint8_t)o->acc);
		o->acc >>= 8;
		o->nb -= 8;
	}
}

/* un code de Huffman s'écrit bit de poids fort d'abord : retourné */
static uint32_t rev(uint32_t v, int n)
{
	uint32_t r = 0;
	for (int i = 0; i < n; i++, v >>= 1)
		r = r << 1 | (v & 1);
	return r;
}

/* littéral ou longueur (0..285) en codes fixes */
static void put_sym(struct bits *o, int s)
{
	if (s < 144)
		put_bits(o, rev((uint32_t)(0x30 + s), 8), 8);
	else if (s < 256)
		put_bits(o, rev((uint32_t)(0x190 + s - 144), 9), 9);
	else if (s < 280)
		put_bits(o, rev((uint32_t)(s - 256), 7), 7);
	else
		put_bits(o, rev((uint32_t)(0xC0 + s - 280), 8), 8);
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
				       67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
					1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void put_match(struct bits *o, int len, int dist)
{
	int i = 28;
	while (len_base[i] > len)
		i--;
	put_sym(o, 257 + i);
	if (len_extra[i])
		put_bits(o, (uint32_t)(len - len_base[i]), len_extra[i]);
	int d = 29;
	while (dist_base[d] > dist)
		d--;
	put_bits(o, rev((uint32_t)d, 5), 5);
	if (dist_extra[d])
		put_bits(o, (uint32_t)(dist - dist_base[d]), dist_extra[d]);
}

#define WIN 32768
#define HBITS 15
#define CHAIN 8

/* deflate (zlib) de d[0..n[ -> o */
static void zlib(struct bits *o, const uint8_t *d, size_t n)
{
	put_byte(o, 0x78);
	put_byte(o, 0x01);
	put_bits(o, 1, 1); /* dernier bloc */
	put_bits(o, 1, 2); /* codes fixes */
	int32_t *head = malloc(sizeof(int32_t) << HBITS);
	int32_t *prev = malloc(sizeof(int32_t) * WIN);
	if (!head || !prev) {
		free(head);
		free(prev);
		o->oom = 1;
		return;
	}
	for (size_t i = 0; i < (1u << HBITS); i++)
		head[i] = -1;
	size_t i = 0;
	while (i < n) {
		int best = 0, bdist = 0;
		if (i + 3 <= n) {
			uint32_t h = ((uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2]) * 2654435761u >> (32 - HBITS);
			int32_t c = head[h];
			for (int k = 0; k < CHAIN && c >= 0 && i - (size_t)c <= WIN; k++) {
				size_t max = n - i < 258 ? n - i : 258, l = 0;
				const uint8_t *a = d + c, *b = d + i;
				while (l < max && a[l] == b[l])
					l++;
				if ((int)l > best) {
					best = (int)l;
					bdist = (int)(i - (size_t)c);
					if (l == max)
						break;
				}
				int32_t nc = prev[c % WIN];
				if (nc >= c)
					break;
				c = nc;
			}
			prev[i % WIN] = head[h];
			head[h] = (int32_t)i;
		}
		if (best >= 3) {
			put_match(o, best, bdist);
			/* les positions sautées entrent dans la table (un nombre limité : le long d'un aplat, une seule
			 * suffit, la distance 1 ou 4 se retrouve à la position suivante) */
			size_t end = i + (size_t)best;
			size_t step = best > 32 ? (size_t)best / 8 : 1;
			for (size_t j = i + 1; j < end && j + 3 <= n; j += step) {
				uint32_t h = ((uint32_t)d[j] << 16 | (uint32_t)d[j + 1] << 8 | d[j + 2]) * 2654435761u >> (32 - HBITS);
				prev[j % WIN] = head[h];
				head[h] = (int32_t)j;
			}
			i = end;
		} else {
			put_sym(o, d[i]);
			i++;
		}
	}
	put_sym(o, 256);
	if (o->nb)
		put_bits(o, 0, 8 - o->nb);
	free(head);
	free(prev);
	uint32_t a = 1, b = 0; /* adler-32 */
	for (size_t k = 0; k < n;) {
		size_t m = n - k < 5552 ? n - k : 5552;
		for (size_t j = 0; j < m; j++) {
			a += d[k + j];
			b += a;
		}
		a %= 65521;
		b %= 65521;
		k += m;
	}
	uint32_t ad = b << 16 | a;
	for (int s = 24; s >= 0; s -= 8)
		put_byte(o, (uint8_t)(ad >> s));
}

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t v = i;
			for (int k = 0; k < 8; k++)
				v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
			crc_table[i] = v;
		}
	c = ~c;
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
	return ~c;
}

static void be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* un bloc (chunk) : longueur, type, données, CRC */
static int chunk(uint8_t **out, size_t *len, size_t *cap, const char *type, const uint8_t *d, size_t n)
{
	if (*len + n + 12 > *cap) {
		size_t c = (*len + n + 12) * 2;
		uint8_t *p = realloc(*out, c);
		if (!p)
			return -1;
		*out = p;
		*cap = c;
	}
	uint8_t *p = *out + *len;
	be32(p, (uint32_t)n);
	memcpy(p + 4, type, 4);
	if (n)
		memcpy(p + 8, d, n);
	be32(p + 8 + n, crc(0, p + 4, n + 4));
	*len += n + 12;
	return 0;
}

uint8_t *png_encode(const uint8_t *rgb, int w, int h, size_t *out_len)
{
	size_t row = (size_t)w * 3;
	uint8_t *raw = malloc((row + 1) * (size_t)h);
	if (!raw)
		return 0;
	for (int y = 0; y < h; y++) { /* le filtre de chaque ligne : Sub (1) ou Up (2), le plus petit */
		const uint8_t *cur = rgb + (size_t)y * row, *up = y ? cur - row : 0;
		uint8_t *o = raw + (size_t)y * (row + 1);
		unsigned long sub = 0, upf = 0;
		for (size_t x = 0; x < row; x++) {
			int a = x >= 3 ? cur[x - 3] : 0;
			int s = (int8_t)(uint8_t)(cur[x] - a);
			sub += (unsigned long)(s < 0 ? -s : s);
			int u = (int8_t)(uint8_t)(cur[x] - (up ? up[x] : 0));
			upf += (unsigned long)(u < 0 ? -u : u);
		}
		int f = up && upf < sub ? 2 : 1;
		o[0] = (uint8_t)f;
		for (size_t x = 0; x < row; x++)
			o[1 + x] = (uint8_t)(cur[x] - (f == 1 ? (x >= 3 ? cur[x - 3] : 0) : up[x]));
	}
	struct bits z = { 0 };
	zlib(&z, raw, (row + 1) * (size_t)h);
	free(raw);
	if (z.oom) {
		free(z.p);
		return 0;
	}
	size_t cap = z.n + 128, len = 8;
	uint8_t *out = malloc(cap);
	if (!out) {
		free(z.p);
		return 0;
	}
	memcpy(out, "\x89PNG\r\n\x1a\n", 8);
	uint8_t ihdr[13];
	be32(ihdr, (uint32_t)w);
	be32(ihdr + 4, (uint32_t)h);
	ihdr[8] = 8; /* 8 bits, RGB */
	ihdr[9] = 2;
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	if (chunk(&out, &len, &cap, "IHDR", ihdr, 13) || chunk(&out, &len, &cap, "IDAT", z.p, z.n) ||
	    chunk(&out, &len, &cap, "IEND", 0, 0)) {
		free(out);
		free(z.p);
		return 0;
	}
	free(z.p);
	*out_len = len;
	return out;
}
