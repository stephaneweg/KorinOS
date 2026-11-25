/* pwhash.h : SHA-256, HMAC-SHA-256, PBKDF2-HMAC-SHA-256 (FIPS 180-4, RFC 8018), autonome (memcpy, memset,
 * str_len) : authd (les empreintes de /etc/shadow.ini) et osinstall (le compte du système installé). Inclus une fois
 * par programme (fonctions static). */
#ifndef KORIN_PWHASH_H
#define KORIN_PWHASH_H

/* ---------------------------------------------------------------- SHA-256, HMAC, PBKDF2 (FIPS 180-4, RFC 8018) */

struct sha {
	u32 h[8];
	u8 buf[64];
	u64 len;
	u32 n;
};

static const u32 K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static u32 ror(u32 x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static void sha_block(struct sha *s, const u8 *p)
{
	u32 w[64], a, b, c, d, e, f, g, h;
	for (int i = 0; i < 16; i++)
		w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 64; i++) {
		u32 s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
		u32 s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
	for (int i = 0; i < 64; i++) {
		u32 t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		u32 t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
	}
	s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

static void sha_init(struct sha *s)
{
	static const u32 iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
				  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	for (int i = 0; i < 8; i++)
		s->h[i] = iv[i];
	s->len = 0;
	s->n = 0;
}

static void sha_add(struct sha *s, const void *data, u64 len)
{
	const u8 *p = (const u8 *)data; /* (explicite : le fichier sert aussi au C++ de l'installateur) */
	s->len += len;
	while (len--) {
		s->buf[s->n++] = *p++;
		if (s->n == 64) {
			sha_block(s, s->buf);
			s->n = 0;
		}
	}
}

static void sha_end(struct sha *s, u8 out[32])
{
	u64 bits = s->len * 8;
	u8 one = 0x80, zero = 0;
	sha_add(s, &one, 1);
	while (s->n != 56)
		sha_add(s, &zero, 1);
	u8 l[8];
	for (int i = 0; i < 8; i++)
		l[i] = (u8)(bits >> (56 - 8 * i));
	sha_add(s, l, 8);
	for (int i = 0; i < 8; i++) {
		out[4 * i] = (u8)(s->h[i] >> 24);
		out[4 * i + 1] = (u8)(s->h[i] >> 16);
		out[4 * i + 2] = (u8)(s->h[i] >> 8);
		out[4 * i + 3] = (u8)s->h[i];
	}
}

/* HMAC-SHA-256 d'un message en deux morceaux (le sel et le numéro de bloc, pour PBKDF2) */
__attribute__((unused)) static void hmac(const u8 *key, u64 klen, const u8 *m1, u64 n1, const u8 *m2, u64 n2, u8 out[32])
{
	u8 k[64], pad[64], t[32];
	struct sha s;
	memset(k, 0, 64);
	if (klen > 64) {
		sha_init(&s);
		sha_add(&s, key, klen);
		sha_end(&s, k);
	} else
		memcpy(k, key, klen);
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x36;
	sha_init(&s);
	sha_add(&s, pad, 64);
	sha_add(&s, m1, n1);
	sha_add(&s, m2, n2);
	sha_end(&s, t);
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x5c;
	sha_init(&s);
	sha_add(&s, pad, 64);
	sha_add(&s, t, 32);
	sha_end(&s, out);
}

/* PBKDF2-HMAC-SHA-256, un bloc de 32 octets */
__attribute__((unused)) static void pbkdf2(const char *pw, const u8 *salt, u64 slen, u32 iter, u8 out[32])
{
	u8 u[32], blk[4] = {0, 0, 0, 1};
	u64 plen = (u64)str_len(pw);
	hmac((const u8 *)pw, plen, salt, slen, blk, 4, u);
	memcpy(out, u, 32);
	for (u32 i = 1; i < iter; i++) {
		hmac((const u8 *)pw, plen, u, 32, 0, 0, u);
		for (int j = 0; j < 32; j++)
			out[j] ^= u[j];
	}
}

#endif
