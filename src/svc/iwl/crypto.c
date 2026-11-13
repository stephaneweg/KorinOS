/* crypto.c : ce qu'il faut au supplicant WPA2-personnel (802.11-2020, 12.7) : SHA-1 (FIPS 180-4),
 * HMAC (RFC 2104), PBKDF2 (RFC 8018) pour la PMK, la PRF de 802.11i pour la PTK, le MIC des trames
 * EAPOL-Key (version 2 : HMAC-SHA1-128), AES-128 (FIPS 197) et son déballage de clé (RFC 3394) pour
 * la GTK. Écrit ici d'après ces normes ; essayé contre leurs vecteurs (tools/pc/iwltest). Le
 * chiffrement des trames (CCMP) est fait par la puce, avec la clé installée par commande. */
#include "iwlfw.h"

/* ---------------------------------------------------------------- SHA-1 */

struct sha1_ctx {
	u32 h[5];
	u64 n;
	u8 buf[64];
	u32 used;
};

static u32 rol(u32 x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(u32 h[5], const u8 *p)
{
	u32 w[80];
	for (int i = 0; i < 16; i++)
		w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 80; i++)
		w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
	for (int i = 0; i < 80; i++) {
		u32 f, k;
		if (i < 20)
			f = (b & c) | (~b & d), k = 0x5A827999;
		else if (i < 40)
			f = b ^ c ^ d, k = 0x6ED9EBA1;
		else if (i < 60)
			f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
		else
			f = b ^ c ^ d, k = 0xCA62C1D6;
		u32 t = rol(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rol(b, 30);
		b = a;
		a = t;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
}

static void sha1_init(struct sha1_ctx *c)
{
	c->h[0] = 0x67452301;
	c->h[1] = 0xEFCDAB89;
	c->h[2] = 0x98BADCFE;
	c->h[3] = 0x10325476;
	c->h[4] = 0xC3D2E1F0;
	c->n = 0;
	c->used = 0;
}

static void sha1_update(struct sha1_ctx *c, const u8 *d, u32 len)
{
	c->n += len;
	while (len) {
		u32 k = 64 - c->used;
		if (k > len)
			k = len;
		memcpy(c->buf + c->used, d, k);
		c->used += k;
		d += k;
		len -= k;
		if (c->used == 64) {
			sha1_block(c->h, c->buf);
			c->used = 0;
		}
	}
}

static void sha1_final(struct sha1_ctx *c, u8 out[20])
{
	u64 bits = c->n * 8;
	u8 pad = 0x80;
	sha1_update(c, &pad, 1);
	pad = 0;
	while (c->used != 56)
		sha1_update(c, &pad, 1);
	u8 l[8];
	for (int i = 0; i < 8; i++)
		l[i] = (u8)(bits >> (56 - 8 * i));
	sha1_update(c, l, 8);
	for (int i = 0; i < 5; i++) {
		out[4 * i] = (u8)(c->h[i] >> 24);
		out[4 * i + 1] = (u8)(c->h[i] >> 16);
		out[4 * i + 2] = (u8)(c->h[i] >> 8);
		out[4 * i + 3] = (u8)c->h[i];
	}
}

void sha1(const u8 *data, u32 len, u8 out[20])
{
	struct sha1_ctx c;
	sha1_init(&c);
	sha1_update(&c, data, len);
	sha1_final(&c, out);
}

/* ---------------------------------------------------------------- HMAC-SHA1 */

void hmac_sha1_v(const u8 *key, u32 klen, int n, const u8 *const *data, const u32 *lens, u8 out[20])
{
	u8 k[64], tk[20], pad[64];
	memset(k, 0, sizeof k);
	if (klen > 64) {
		sha1(key, klen, tk);
		memcpy(k, tk, 20);
	} else
		memcpy(k, key, klen);
	struct sha1_ctx c;
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x36;
	sha1_init(&c);
	sha1_update(&c, pad, 64);
	for (int i = 0; i < n; i++)
		sha1_update(&c, data[i], lens[i]);
	u8 inner[20];
	sha1_final(&c, inner);
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x5c;
	sha1_init(&c);
	sha1_update(&c, pad, 64);
	sha1_update(&c, inner, 20);
	sha1_final(&c, out);
}

void hmac_sha1(const u8 *key, u32 klen, const u8 *data, u32 len, u8 out[20])
{
	hmac_sha1_v(key, klen, 1, &data, &len, out);
}

/* ---------------------------------------------------------------- PBKDF2, PRF, PTK, MIC */

void pbkdf2_sha1(const u8 *pass, u32 plen, const u8 *salt, u32 slen, u32 iter, u8 *out, u32 olen)
{
	for (u32 blk = 1; olen; blk++) {
		u8 cnt[4] = {(u8)(blk >> 24), (u8)(blk >> 16), (u8)(blk >> 8), (u8)blk};
		const u8 *parts[2] = {salt, cnt};
		u32 lens[2] = {slen, 4};
		u8 u[20], t[20];
		hmac_sha1_v(pass, plen, 2, parts, lens, u);
		memcpy(t, u, 20);
		for (u32 i = 1; i < iter; i++) {
			hmac_sha1(pass, plen, u, 20, u);
			for (int j = 0; j < 20; j++)
				t[j] ^= u[j];
		}
		u32 k = olen < 20 ? olen : 20;
		memcpy(out, t, k);
		out += k;
		olen -= k;
	}
}

void wpa_pmk(const char *pass, const u8 *ssid, u32 ssid_len, u8 pmk[32])
{
	pbkdf2_sha1((const u8 *)pass, (u32)strlen(pass), ssid, ssid_len, 4096, pmk, 32);
}

void prf_sha1(const u8 *key, u32 klen, const char *label, const u8 *data, u32 dlen, u8 *out, u32 olen)
{
	u8 zero = 0;
	for (u8 i = 0; olen; i++) {
		const u8 *parts[4] = {(const u8 *)label, &zero, data, &i};
		u32 lens[4] = {(u32)strlen(label), 1, dlen, 1};
		u8 h[20];
		hmac_sha1_v(key, klen, 4, parts, lens, h);
		u32 k = olen < 20 ? olen : 20;
		memcpy(out, h, k);
		out += k;
		olen -= k;
	}
}

void wpa_ptk(const u8 pmk[32], const u8 aa[6], const u8 spa[6], const u8 anonce[32], const u8 snonce[32],
	     u8 ptk[48])
{
	u8 d[6 + 6 + 32 + 32];
	int a_first = memcmp(aa, spa, 6) < 0;
	memcpy(d, a_first ? aa : spa, 6);
	memcpy(d + 6, a_first ? spa : aa, 6);
	int n_first = memcmp(anonce, snonce, 32) < 0;
	memcpy(d + 12, n_first ? anonce : snonce, 32);
	memcpy(d + 44, n_first ? snonce : anonce, 32);
	prf_sha1(pmk, 32, "Pairwise key expansion", d, sizeof d, ptk, 48);
}

void eapol_mic(const u8 kck[16], const u8 *eapol, u32 len, u8 mic[16])
{
	u8 h[20];
	hmac_sha1(kck, 16, eapol, len, h);
	memcpy(mic, h, 16);
}

/* ---------------------------------------------------------------- AES-128 */

static u8 sbox[256], inv_sbox[256];
static int aes_ready;

static u8 xt(u8 x) { return (u8)(x << 1 ^ ((x >> 7) * 0x1b)); }

static u8 gmul(u8 a, u8 b)
{
	u8 r = 0;
	while (b) {
		if (b & 1)
			r ^= a;
		a = xt(a);
		b >>= 1;
	}
	return r;
}

static void aes_init(void)
{
	/* la boîte S de FIPS 197 5.1.1 : inverse dans GF(2^8), puis la transformation affine */
	u8 p = 1, q = 1;
	do {
		p = (u8)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0)); /* p × 3 */
		q ^= q << 1;                                     /* q ÷ 3 */
		q ^= q << 2;
		q ^= q << 4;
		if (q & 0x80)
			q ^= 0x09;
		u8 x = (u8)(q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^ (q << 3 | q >> 5) ^ (q << 4 | q >> 4));
		sbox[p] = x ^ 0x63;
	} while (p != 1);
	sbox[0] = 0x63;
	for (int i = 0; i < 256; i++)
		inv_sbox[sbox[i]] = (u8)i;
	aes_ready = 1;
}

static void key_expand(const u8 key[16], u8 rk[176])
{
	memcpy(rk, key, 16);
	u8 rcon = 1;
	for (int i = 16; i < 176; i += 4) {
		u8 t[4];
		memcpy(t, rk + i - 4, 4);
		if (i % 16 == 0) {
			u8 x = t[0];
			t[0] = sbox[t[1]] ^ rcon;
			t[1] = sbox[t[2]];
			t[2] = sbox[t[3]];
			t[3] = sbox[x];
			rcon = xt(rcon);
		}
		for (int j = 0; j < 4; j++)
			rk[i + j] = rk[i - 16 + j] ^ t[j];
	}
}

static void add_rk(u8 s[16], const u8 *rk)
{
	for (int i = 0; i < 16; i++)
		s[i] ^= rk[i];
}

void aes128_encrypt(const u8 key[16], const u8 in[16], u8 out[16])
{
	if (!aes_ready)
		aes_init();
	u8 rk[176], s[16];
	key_expand(key, rk);
	memcpy(s, in, 16);
	add_rk(s, rk);
	for (int r = 1; r <= 10; r++) {
		u8 t[16];
		for (int i = 0; i < 16; i++) /* SubBytes + ShiftRows (colonne c, ligne i : décalée de i) */
			t[i] = sbox[s[(i + 4 * (i % 4)) % 16]];
		if (r < 10)
			for (int c = 0; c < 4; c++) {
				u8 *m = t + 4 * c, a0 = m[0], a1 = m[1], a2 = m[2], a3 = m[3];
				m[0] = xt(a0) ^ (xt(a1) ^ a1) ^ a2 ^ a3;
				m[1] = a0 ^ xt(a1) ^ (xt(a2) ^ a2) ^ a3;
				m[2] = a0 ^ a1 ^ xt(a2) ^ (xt(a3) ^ a3);
				m[3] = (xt(a0) ^ a0) ^ a1 ^ a2 ^ xt(a3);
			}
		memcpy(s, t, 16);
		add_rk(s, rk + 16 * r);
	}
	memcpy(out, s, 16);
}

static void aes128_decrypt(const u8 key[16], const u8 in[16], u8 out[16])
{
	if (!aes_ready)
		aes_init();
	u8 rk[176], s[16];
	key_expand(key, rk);
	memcpy(s, in, 16);
	add_rk(s, rk + 160);
	for (int r = 9; r >= 0; r--) {
		u8 t[16];
		for (int i = 0; i < 16; i++) /* InvShiftRows + InvSubBytes */
			t[(i + 4 * (i % 4)) % 16] = inv_sbox[s[i]];
		add_rk(t, rk + 16 * r);
		if (r > 0)
			for (int c = 0; c < 4; c++) {
				u8 *m = t + 4 * c, a0 = m[0], a1 = m[1], a2 = m[2], a3 = m[3];
				m[0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
				m[1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
				m[2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
				m[3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
			}
		memcpy(s, t, 16);
	}
	memcpy(out, s, 16);
}

int aes_unwrap(const u8 kek[16], const u8 *in, u32 inlen, u8 *out)
{
	if (inlen < 24 || inlen % 8)
		return -1;
	u32 n = inlen / 8 - 1;
	u8 a[8], b[16];
	memcpy(a, in, 8);
	memcpy(out, in + 8, 8 * n);
	for (int j = 5; j >= 0; j--)
		for (u32 i = n; i >= 1; i--) {
			u64 t = (u64)n * (u64)j + i;
			memcpy(b, a, 8);
			for (int k = 0; k < 8; k++)
				b[7 - k] ^= (u8)(t >> (8 * k));
			memcpy(b + 8, out + 8 * (i - 1), 8);
			aes128_decrypt(kek, b, b);
			memcpy(a, b, 8);
			memcpy(out + 8 * (i - 1), b + 8, 8);
		}
	for (int k = 0; k < 8; k++)
		if (a[k] != 0xA6)
			return -2;
	return 0;
}

void aes_wrap(const u8 kek[16], const u8 *in, u32 inlen, u8 *out)
{
	u32 n = inlen / 8;
	u8 a[8], b[16];
	memset(a, 0xA6, 8);
	memcpy(out + 8, in, inlen);
	for (u32 j = 0; j <= 5; j++)
		for (u32 i = 1; i <= n; i++) {
			memcpy(b, a, 8);
			memcpy(b + 8, out + 8 * i, 8);
			aes128_encrypt(kek, b, b);
			u64 t = (u64)n * j + i;
			memcpy(a, b, 8);
			for (int k = 0; k < 8; k++)
				a[7 - k] ^= (u8)(t >> (8 * k));
			memcpy(out + 8 * i, b + 8, 8);
		}
	memcpy(out, a, 8);
}
