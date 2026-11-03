//
// tgcrypto.h -- the cryptography MTProto 2.0 needs, on OpenSSL (the OS's only TLS and crypto: docs/OPENSSL.md; Onyx's was on
// mbedTLS): SHA-1 / SHA-256, AES-256 in IGE mode, RSA_PAD (the auth key's exchange), the factoring of pq, the 2048-bit
// Diffie-Hellman, SRP (the two-step verification's password), and the random generator (OpenSSL's, seeded by the kernel's
// getrandom; a seed file and the keys' timing mixed in, as on Onyx). The parts that are not library calls are Onyx's.
//
// MIT licence.
//
#ifndef TG_CRYPTO_H
#define TG_CRYPTO_H

#include <stdint.h>
#include <string.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

// From the platform (tgplat.h): raw entropy (the OS's), a fine counter, the seed file.
int  tg_platform_entropy (unsigned char *buf, int n);
unsigned long long tg_platform_counter ();
bool tg_seed_load (unsigned char *buf, int n);
void tg_seed_save (const unsigned char *buf, int n);

namespace tgc {

// ---- hashes ------------------------------------------------------------------------------------------

struct Part { const void *p; int n; };

inline void digest (const EVP_MD *md, unsigned char *out, const Part *ps, int n)
{
	static EVP_MD_CTX *x;
	if (!x) x = EVP_MD_CTX_new ();
	EVP_DigestInit_ex (x, md, 0);
	for (int i = 0; i < n; i++) if (ps[i].n > 0) EVP_DigestUpdate (x, ps[i].p, (size_t) ps[i].n);
	unsigned got = 0;
	EVP_DigestFinal_ex (x, out, &got);
}

inline void sha1 (unsigned char out[20], Part a, Part b = Part { 0, 0 }, Part c = Part { 0, 0 }, Part d = Part { 0, 0 })
{
	Part ps[4] = { a, b, c, d };
	digest (EVP_sha1 (), out, ps, 4);
}

inline void sha256 (unsigned char out[32], Part a, Part b = Part { 0, 0 }, Part c = Part { 0, 0 }, Part d = Part { 0, 0 },
		    Part e = Part { 0, 0 }, Part f = Part { 0, 0 })
{
	Part ps[6] = { a, b, c, d, e, f };
	digest (EVP_sha256 (), out, ps, 6);
}

// ---- AES-256-IGE -------------------------------------------------------------------------------------
// iv: 32 bytes (the first 16 the previous ciphertext block's, the last 16 the previous plaintext's).
// n: a multiple of 16. in and out may be the same buffer. (bits: 128 for the tests' known vector)
// One ECB block at a time through EVP (the constant-time AES on the Pi 4, which has no AES instructions).

inline void aes_ige (bool enc, const unsigned char *key, const unsigned char iv[32], const unsigned char *in, unsigned char *out, int n, int bits = 256)
{
	static EVP_CIPHER_CTX *c;
	if (!c) c = EVP_CIPHER_CTX_new ();
	EVP_CipherInit_ex (c, bits == 128 ? EVP_aes_128_ecb () : EVP_aes_256_ecb (), 0, key, 0, enc ? 1 : 0);
	EVP_CIPHER_CTX_set_padding (c, 0);
	unsigned char x[16], y[16], t[16], blk[16];
	// encrypt: c = E(p ^ c_prev) ^ p_prev;  decrypt: p = D(c ^ p_prev) ^ c_prev
	if (enc) { memcpy (x, iv, 16); memcpy (y, iv + 16, 16); }	// x = c_prev, y = p_prev
	else { memcpy (x, iv + 16, 16); memcpy (y, iv, 16); }	// x = p_prev, y = c_prev
	for (int o = 0; o + 16 <= n; o += 16)
	{
		memcpy (blk, in + o, 16);
		for (int i = 0; i < 16; i++) t[i] = blk[i] ^ x[i];
		int l = 0;
		EVP_CipherUpdate (c, t, &l, t, 16);
		for (int i = 0; i < 16; i++) t[i] ^= y[i];
		memcpy (out + o, t, 16);
		memcpy (x, t, 16);		// (the output: the next block's "previous" on its side)
		memcpy (y, blk, 16);
	}
}

// ---- randomness --------------------------------------------------------------------------------------

inline bool &rng_ready () { static bool r; return r; }

// Something unpredictable (an event's time and value): mixed into OpenSSL's pool.
inline void entropy_add (const void *p, int n)
{
	if (!rng_ready ()) return;
	unsigned long long c = tg_platform_counter ();
	RAND_add (&c, sizeof c, 0.0);
	if (n > 0) RAND_add (p, n, 0.0);
}

// Once, at start: the kernel's bytes and the seed file in the pool, a new seed file written.
inline void rng_init ()
{
	if (rng_ready ()) return;
	rng_ready () = true;
	unsigned char b[64];
	int n = tg_platform_entropy (b, sizeof b);
	if (n > 0) RAND_seed (b, n);
	if (tg_seed_load (b, 64)) RAND_add (b, 64, 0.0);
	RAND_bytes (b, 64);
	tg_seed_save (b, 64);
}

// Before a secret is made (a DH exponent, SRP's a): fresh bytes of the kernel in the pool.
inline void reseed ()
{
	if (!rng_ready ()) rng_init ();
	unsigned char b[32];
	int n = tg_platform_entropy (b, sizeof b);
	if (n > 0) RAND_add (b, n, n);
}

inline void random (void *out, int n)
{
	if (!rng_ready ()) rng_init ();
	if (n > 0 && RAND_bytes ((unsigned char *) out, n) != 1) tg_platform_entropy ((unsigned char *) out, n);
}
inline long long random64 () { long long x; random (&x, 8); return x; }

// ---- big numbers -------------------------------------------------------------------------------------

inline BN_CTX *bn_ctx () { static BN_CTX *c; if (!c) c = BN_CTX_new (); return c; }

struct Mpi
{
	BIGNUM *m;
	Mpi () : m (BN_new ()) {}
	Mpi (const void *be, int n) : m (BN_new ()) { set (be, n); }
	~Mpi () { BN_clear_free (m); }
	bool hex (const char *h) { return BN_hex2bn (&m, h) != 0; }
	void set (const void *be, int n) { BN_bin2bn ((const unsigned char *) be, n, m); }
	void word (unsigned long w) { BN_set_word (m, w); }
	void out (unsigned char *be, int n) const { BN_bn2binpad (m, be, n); }	// left-padded
	int bytes () const { return BN_num_bytes (m); }
private:
	Mpi (const Mpi &);
	Mpi &operator= (const Mpi &);
};

// x = a^e mod n
inline bool powmod (Mpi &x, const Mpi &a, const Mpi &e, const Mpi &n)
{
	BN_set_flags (e.m, BN_FLG_CONSTTIME);			// (the exponents here are secrets, or the public 65537)
	return BN_mod_exp (x.m, a.m, e.m, n.m, bn_ctx ()) == 1;
}

// ---- the server's RSA keys ---------------------------------------------------------------------------
// tdesktop's mtproto_dc_options.cpp (the production key and the test servers' key), the modulus in hex,
// e = 65537. The fingerprint is computed (SHA-1 of the TL strings n and e, its last 8 bytes).
// config.ini's test_rsa_n (tools/nettest/tgsrv.py's key): the only one then.

struct RsaKey { const char *n_hex; long long fp; unsigned char n[256]; bool made; };

inline char *rsa_test_hex () { static char h[516]; return h; }

inline void rsa_make (RsaKey &k)
{
	for (int j = 0; j < 256; j++)
	{
		const char *h = k.n_hex + j * 2;
		int hi = h[0] <= '9' ? h[0] - '0' : (h[0] | 32) - 'a' + 10, lo = h[1] <= '9' ? h[1] - '0' : (h[1] | 32) - 'a' + 10;
		k.n[j] = (unsigned char) (hi * 16 + lo);
	}
	unsigned char ser[264 + 4 + 4], dig[20];
	ser[0] = 254; ser[1] = 0; ser[2] = 1; ser[3] = 0;	// (TL string of 256 bytes)
	memcpy (ser + 4, k.n, 256);
	ser[260] = 3; ser[261] = 1; ser[262] = 0; ser[263] = 1;	// (TL string of 3 bytes: 01 00 01)
	sha1 (dig, Part { ser, 264 });
	long long fp = 0;
	for (int j = 0; j < 8; j++) fp |= (long long) dig[12 + j] << (8 * j);
	k.fp = fp;
	k.made = true;
}

inline RsaKey *rsa_keys (int *count)
{
	static RsaKey test = { rsa_test_hex (), 0, { 0 }, false };
	if (rsa_test_hex ()[0])
	{
		if (!test.made) rsa_make (test);
		*count = 1;
		return &test;
	}
	static RsaKey k[2] = {
		{ "e8bb3305c0b52c6cf2afdf7637313489e63e05268e5badb601af417786472e5f93b85438968e20e6729a301c0afc121bf7151f834436f7fda680847a66bf64accec78ee21c0b316f0edafe2f41908da7bd1f4a5107638eeb67040ace472a14f90d9f7c2b7def99688ba3073adb5750bb02964902a359fe745d8170e36876d4fd8a5d41b2a76cbff9a13267eb9580b2d06d10357448d20d9da2191cb5d8c93982961cdfdeda629e37f1fb09a0722027696032fe61ed663db7a37f6f263d370f69db53a0dc0a1748bdaaff6209d5645485e6e001d1953255757e4b8e42813347b11da6ab500fd0ace7e6dfa3736199ccaf9397ed0745a427dcfa6cd67bcb1acff3", 0, { 0 }, false },
		{ "c8c11d635691fac091dd9489aedced2932aa8a0bcefef05fa800892d9b52ed03200865c9e97211cb2ee6c7ae96d3fb0e15aeffd66019b44a08a240cfdd2868a85e1f54d6fa5deaa041f6941ddf302690d61dc476385c2fa655142353cb4e4b59f6e5b6584db76fe8b1370263246c010c93d011014113ebdf987d093f9d37c2be48352d69a1683f8f6e6c2167983c761e3ab169fde5daaa12123fa1beab621e4da5935e9c198f82f35eae583a99386d8110ea6bd1abb0f568759f62694419ea5f69847c43462abef858b4cb5edc84e7b9226cd7bd7e183aa974a712c079dde85b9dc063b8a5c08e8f859c0ee5dcd824c7807f20153361a7f63cfd2a433a1be7f5", 0, { 0 }, false } };
	for (int i = 0; i < 2; i++) if (!k[i].made) rsa_make (k[i]);
	*count = 2;
	return k;
}

// RSA_PAD (MTProto 2.0): data (up to 144 bytes) -> 256 bytes encrypted with the key.
inline bool rsa_pad (const unsigned char *data, int n, const RsaKey &key, unsigned char out[256])
{
	if (n > 144) return false;
	unsigned char padded[192], rev[192], temp[32], dh[224], enc[224], kae[256], h[32];
	memcpy (padded, data, (size_t) n);
	random (padded + n, 192 - n);
	for (int i = 0; i < 192; i++) rev[i] = padded[191 - i];
	Mpi mod (key.n, 256), e, m, r;
	e.word (65537);
	for (int tries = 0; tries < 32; tries++)
	{
		random (temp, 32);
		memcpy (dh, rev, 192);
		sha256 (dh + 192, Part { temp, 32 }, Part { padded, 192 });
		static const unsigned char zero_iv[32] = { 0 };
		aes_ige (true, temp, zero_iv, dh, enc, 224);
		sha256 (h, Part { enc, 224 });
		for (int i = 0; i < 32; i++) kae[i] = temp[i] ^ h[i];
		memcpy (kae + 32, enc, 224);
		m.set (kae, 256);
		if (BN_cmp (m.m, mod.m) >= 0) continue;
		if (!powmod (r, m, e, mod)) return false;
		r.out (out, 256);
		return true;
	}
	return false;
}

// ---- pq --------------------------------------------------------------------------------------------

inline uint64_t mulmod (uint64_t a, uint64_t b, uint64_t m) { return (uint64_t) ((unsigned __int128) a * b % m); }
inline uint64_t gcd (uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }

// pq (a product of two primes, < 2^63) -> p < q. Pollard's rho, Brent's variant.
inline bool factor (uint64_t pq, uint64_t *p, uint64_t *q)
{
	if (pq < 4) return false;
	if (!(pq & 1)) { *p = 2; *q = pq / 2; return true; }
	for (uint64_t c = 1; c < 64; c++)
	{
		uint64_t y = 2 + c, x = y, g = 1, r = 1, qq = 1, ys = y;
		const uint64_t m = 128;
		while (g == 1)
		{
			x = y;
			for (uint64_t i = 0; i < r; i++) y = (mulmod (y, y, pq) + c) % pq;
			uint64_t k = 0;
			while (k < r && g == 1)
			{
				ys = y;
				for (uint64_t i = 0; i < m && i < r - k; i++)
				{
					y = (mulmod (y, y, pq) + c) % pq;
					qq = mulmod (qq, x > y ? x - y : y - x, pq);
				}
				g = gcd (qq, pq);
				k += m;
			}
			r *= 2;
			if (r > (1ull << 26)) break;
		}
		if (g == pq)
		{
			do { ys = (mulmod (ys, ys, pq) + c) % pq; g = gcd (x > ys ? x - ys : ys - x, pq); } while (g == 1);
		}
		if (g != 1 && g != pq)
		{
			uint64_t a = g, b = pq / g;
			*p = a < b ? a : b; *q = a < b ? b : a;
			return true;
		}
	}
	return false;
}

// ---- Diffie-Hellman checks ---------------------------------------------------------------------------

// The 2048-bit safe prime Telegram's servers send (checked by value; another one is tested for
// primality, slower, then kept for this run).
inline bool check_dh (const unsigned char *prime, int n, int g)
{
	static const char known[] =
		"c71caeb9c6b1c9048e6c522f70f13f73980d40238e3e21c14934d037563d930f48198a0aa7c14058229493d22530f4dbfa336f6e0ac925139543aed44cce7c3720fd51f69458705ac68cd4fe6b6b13abdc9746512969328454f18faf8c595f642477fe96bb2a941d5bcd1d4ac8cc49880708fa9b378e3c4f3a9060bee67cf9a4a4a695811051907e162753b56b0f6b410dba74d8a84b2a14b3144e0ef1284754fd17ed950d5965b4b9dd46582db1178d169c6bc465b0d6ff9ca3928fef5b9ae4e418fc15e83ebea0f87fa9ff5eed70050ded2849f47bf959d956850ce929851f0d8115f635b105ee2e4e15d04b2454bf6f4fadf034b10403119cd8e3b92fcc5b";
	static unsigned char checked[256];
	static bool have = false;
	if (n != 256 || g < 2 || g > 7) return false;
	if (have && !memcmp (checked, prime, 256)) return true;
	Mpi k, p (prime, n);
	k.hex (known);
	bool ok = BN_cmp (k.m, p.m) == 0;
	if (!ok)
	{
		// p prime, (p - 1) / 2 prime (both probable), 2^2047 < p < 2^2048
		if (BN_num_bits (p.m) != 2048) return false;
		if (BN_check_prime (p.m, bn_ctx (), 0) != 1) return false;
		Mpi h;
		if (!BN_rshift1 (h.m, p.m) || BN_check_prime (h.m, bn_ctx (), 0) != 1) return false;
		ok = true;
	}
	// g generates the subgroup of order (p - 1) / 2: p mod (4g)'s conditions (MTProto's list)
	switch (g)
	{
	case 2: ok = ok && BN_mod_word (p.m, 8) == 7; break;
	case 3: ok = ok && BN_mod_word (p.m, 3) == 2; break;
	case 4: break;
	case 5: { BN_ULONG r = BN_mod_word (p.m, 5); ok = ok && (r == 1 || r == 4); break; }
	case 6: { BN_ULONG r = BN_mod_word (p.m, 24); ok = ok && (r == 19 || r == 23); break; }
	case 7: { BN_ULONG r = BN_mod_word (p.m, 7); ok = ok && (r == 3 || r == 5 || r == 6); break; }
	}
	if (ok) { memcpy (checked, prime, 256); have = true; }
	return ok;
}

// 1 < x < p - 1, and 2^{2048-64} <= x <= p - 2^{2048-64} (g_a, g_b)
inline bool check_g (const Mpi &x, const Mpi &p)
{
	Mpi lo, hi;
	BN_set_word (lo.m, 1);
	BN_lshift (lo.m, lo.m, 2048 - 64);
	BN_sub (hi.m, p.m, lo.m);
	return BN_cmp (x.m, lo.m) >= 0 && BN_cmp (x.m, hi.m) <= 0;
}

// ---- SRP: the cloud password (account.password's current_algo: SHA256 SHA256 PBKDF2-HMAC-SHA512
// 100000 SHA256 ModPow) -> A and M1 for inputCheckPasswordSRP. False: the parameters are not safe.

inline void sh (unsigned char out[32], const void *d, int n, const void *salt, int sn)
{
	sha256 (out, Part { salt, sn }, Part { d, n }, Part { salt, sn });
}

// The password's x (the hash PH2), as both sides compute it.
inline void srp_ph2 (unsigned char x[32], const char *password, const unsigned char *salt1, int s1, const unsigned char *salt2, int s2)
{
	unsigned char a[32], b[32], pb[64];
	sh (a, password, (int) strlen (password), salt1, s1);
	sh (b, a, 32, salt2, s2);
	PKCS5_PBKDF2_HMAC ((const char *) b, 32, salt1, s1, 100000, EVP_sha512 (), 64, pb);
	sh (x, pb, 64, salt2, s2);
}

// a_be, when given (the tests' known vector): SRP's secret a, not drawn.
inline bool srp (const char *password, const unsigned char *salt1, int s1, const unsigned char *salt2, int s2,
		 int g, const unsigned char *p_be, int pn, const unsigned char *B_be, int bn,
		 unsigned char A_out[256], unsigned char M1[32], const unsigned char *a_be = 0)
{
	if (pn != 256 || !check_dh (p_be, pn, g)) return false;
	unsigned char xh[32];
	srp_ph2 (xh, password, salt1, s1, salt2, s2);
	Mpi p (p_be, 256), gg, x (xh, 32), B (B_be, bn), v, a, A, k, u, t, s, e, tmp;
	gg.word ((unsigned long) g);
	if (BN_is_zero (B.m) || BN_cmp (B.m, p.m) >= 0) return false;
	unsigned char gpad[256], Bpad[256], Apad[256], h[32];
	gg.out (gpad, 256);
	B.out (Bpad, 256);
	powmod (v, gg, x, p);
	// k = H(p | g)
	sha256 (h, Part { p_be, 256 }, Part { gpad, 256 });
	k.set (h, 32);
	reseed ();
	for (int tries = 0; tries < 8; tries++)
	{
		unsigned char ab[256];
		if (a_be) memcpy (ab, a_be, 256); else random (ab, 256);
		a.set (ab, 256);
		powmod (A, gg, a, p);
		if (a_be || check_g (A, p)) break;
	}
	A.out (Apad, 256);
	sha256 (h, Part { Apad, 256 }, Part { Bpad, 256 });		// u = H(A | B)
	u.set (h, 32);
	if (BN_is_zero (u.m)) return false;
	// t = (B - k v) mod p
	BN_mod_mul (tmp.m, k.m, v.m, p.m, bn_ctx ());
	BN_mod_sub (t.m, B.m, tmp.m, p.m, bn_ctx ());
	// s = t ^ (a + u x) mod p
	BN_mul (e.m, u.m, x.m, bn_ctx ());
	BN_add (e.m, e.m, a.m);
	powmod (s, t, e, p);
	unsigned char spad[256], K[32], hp[32], hg[32], hs1[32], hs2[32], hx[32];
	s.out (spad, 256);
	sha256 (K, Part { spad, 256 });
	sha256 (hp, Part { p_be, 256 });
	sha256 (hg, Part { gpad, 256 });
	for (int i = 0; i < 32; i++) hx[i] = hp[i] ^ hg[i];
	sha256 (hs1, Part { salt1, s1 });
	sha256 (hs2, Part { salt2, s2 });
	sha256 (M1, Part { hx, 32 }, Part { hs1, 32 }, Part { hs2, 32 }, Part { Apad, 256 }, Part { Bpad, 256 }, Part { K, 32 });
	memcpy (A_out, Apad, 256);
	return true;
}

// A new password's verifier v = g^x mod p (256 bytes): account.updatePasswordSettings (the tests).
inline bool srp_verifier (const char *password, const unsigned char *salt1, int s1, const unsigned char *salt2, int s2,
			  int g, const unsigned char *p_be, unsigned char v_out[256])
{
	unsigned char xh[32];
	srp_ph2 (xh, password, salt1, s1, salt2, s2);
	Mpi p (p_be, 256), gg, x (xh, 32), v;
	gg.word ((unsigned long) g);
	if (!powmod (v, gg, x, p)) return false;
	v.out (v_out, 256);
	return true;
}

} // namespace tgc

#endif
