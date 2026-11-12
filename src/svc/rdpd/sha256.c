/* sha256.c : SHA-256 (FIPS 180-4) et HMAC-SHA256 (RFC 2104), pour le défi de
 * rdpd (le mot de passe de l'accès à distance) ; traduction de sha256.inc.
 * Tout en scalaire (le Cortex-A72 du Pi 4 n'a pas les instructions SHA
 * d'ARMv8). Vérifié contre les vecteurs de la RFC 4231 (clés <= 64 octets) et
 * de hashlib. Ni tas ni données : tout sur la pile. */
#include "korinrt.h"
#include "sha256.h"

static const u32 sha_h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
static const u32 sha_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static u32 ror(u32 v, int n)
{
	return v >> n | v << (32 - n);
}

/* état (8 mots), un bloc de 64 octets (sha_block) */
static void sha_block(u32 *st, const u8 *p)
{
	u32 w[64];
	for (int t = 0; t < 16; t++) /* W[0..15] : le bloc, gros-boutiste */
		w[t] = (u32)p[4 * t] << 24 | (u32)p[4 * t + 1] << 16 | (u32)p[4 * t + 2] << 8 | p[4 * t + 3];
	for (int t = 16; t < 64; t++) { /* W[t] = s1(W[t-2]) + W[t-7] + s0(W[t-15]) + W[t-16] */
		u32 a = w[t - 2], b = w[t - 15];
		u32 s1 = ror(a, 17) ^ ror(a, 19) ^ (a >> 10);
		u32 s0 = ror(b, 7) ^ ror(b, 18) ^ (b >> 3);
		w[t] = s1 + s0 + w[t - 7] + w[t - 16];
	}
	u32 a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
	for (int t = 0; t < 64; t++) {
		/* T1 = h + S1(e) + Ch(e,f,g) + K[t] + W[t] ; T2 = S0(a) + Maj(a,b,c) */
		u32 t1 = (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) | (g & ~e)) + h + sha_k[t] + w[t];
		u32 t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	st[0] += a; /* état += a..h */
	st[1] += b;
	st[2] += c;
	st[3] += d;
	st[4] += e;
	st[5] += f;
	st[6] += g;
	st[7] += h;
}

void sha256(const u8 *data, u64 n, u8 *out)
{
	u32 st[8];
	u8 tail[128]; /* la fin (2 blocs) */
	for (int i = 0; i < 8; i++)
		st[i] = sha_h0[i];
	u64 rest = n;
	while (rest >= 64) {
		sha_block(st, data);
		data += 64;
		rest -= 64;
	}
	memset(tail, 0, 128);
	memcpy(tail, data, rest);
	tail[rest] = 0x80;
	u64 nb = rest < 56 ? 64 : 128; /* 1 bloc, ou 2 s'il reste 56 octets ou plus */
	u64 bits = n << 3; /* la longueur en bits, gros-boutiste */
	for (int i = 0; i < 8; i++)
		tail[nb - 8 + i] = (u8)(bits >> (56 - 8 * i));
	sha_block(st, tail);
	if (nb == 128)
		sha_block(st, tail + 64);
	for (int i = 0; i < 8; i++) {
		out[4 * i] = (u8)(st[i] >> 24);
		out[4 * i + 1] = (u8)(st[i] >> 16);
		out[4 * i + 2] = (u8)(st[i] >> 8);
		out[4 * i + 3] = (u8)st[i];
	}
}

/* [0..63] = clé ^ pad, complétée de zéros (.key de hmac_sha256) */
static void hmac_pad(u8 *b, const u8 *key, u64 klen, u8 pad)
{
	for (u64 i = 0; i < 64; i++)
		b[i] = (i < klen ? key[i] : 0) ^ pad;
}

void hmac_sha256(const u8 *key, u64 klen, const u8 *m, u64 mlen, u8 *out)
{
	u8 b[128], inner[32]; /* (clé ^ pad) || message, puis le haché interne */
	hmac_pad(b, key, klen, 0x36); /* interne : (clé ^ ipad) || message */
	memcpy(b + 64, m, mlen);
	sha256(b, 64 + mlen, inner);
	hmac_pad(b, key, klen, 0x5C); /* externe : (clé ^ opad) || interne */
	memcpy(b + 64, inner, 32);
	sha256(b, 96, out);
}
