/* rngtest - essai du générateur aléatoire (appel getrandom, random.asm du
 * noyau), comme rngtest.asm : le bloc ChaCha20 contre le vecteur de la
 * RFC 8439 (2.3.2), puis deux tirages de 32 octets (différents), puis 1 Mo
 * tiré (débit) et le nombre de bits à 1 (environ la moitié). Seuls les 16
 * premiers octets du bloc sont comparés, comme l'asm. Une mémoire refusée
 * pour le Mo : fin sans rien dire, code 0 (comme l'asm). */
#include "korinrt.h"

static u8 key[32];
static u8 block[64] __attribute__((aligned(8)));
static const u8 nonce[12] = { 0, 0, 0, 9, 0, 0, 0, 0x4A, 0, 0, 0, 0 };
static const u8 expected[16] = { 0x10, 0xF1, 0xE7, 0xE4, 0xD1, 0x3B, 0x59, 0x15,
				 0x50, 0x0F, 0xDD, 0x1F, 0xA3, 0x20, 0x71, 0xC4 };

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 rotl(u32 v, int n)
{
	return v << n | v >> (32 - n);
}

#define QR(a, b, c, d)                       \
	do {                                 \
		a += b, d ^= a, d = rotl(d, 16); \
		c += d, b ^= c, b = rotl(b, 12); \
		a += b, d ^= a, d = rotl(d, 8);  \
		c += d, b ^= c, b = rotl(b, 7);  \
	} while (0)

/* le bloc ChaCha20 (RFC 8439, 2.3), comme src/inc/chacha20.inc : clé (32 o),
 * compteur, nonce (12 o) -> sortie (64 o) */
static void chacha20_block(const u8 *k, u32 counter, const u8 *n, u8 *out)
{
	u32 s[16], x[16];
	s[0] = 0x61707865; /* « expand 32-byte k » */
	s[1] = 0x3320646E;
	s[2] = 0x79622D32;
	s[3] = 0x6B206574;
	for (int i = 0; i < 8; i++)
		s[4 + i] = rd32(k + 4 * i);
	s[12] = counter;
	for (int i = 0; i < 3; i++)
		s[13 + i] = rd32(n + 4 * i);
	for (int i = 0; i < 16; i++)
		x[i] = s[i];
	for (int r = 0; r < 10; r++) { /* 10 doubles tours */
		QR(x[0], x[4], x[8], x[12]); /* colonnes */
		QR(x[1], x[5], x[9], x[13]);
		QR(x[2], x[6], x[10], x[14]);
		QR(x[3], x[7], x[11], x[15]);
		QR(x[0], x[5], x[10], x[15]); /* diagonales */
		QR(x[1], x[6], x[11], x[12]);
		QR(x[2], x[7], x[8], x[13]);
		QR(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++) {
		u32 v = x[i] + s[i];
		out[4 * i] = (u8)v;
		out[4 * i + 1] = (u8)(v >> 8);
		out[4 * i + 2] = (u8)(v >> 16);
		out[4 * i + 3] = (u8)(v >> 24);
	}
}

/* octets, nombre : en hexadécimal, puis fin de ligne */
static void hex(const u8 *p, u64 n)
{
	char s[144];
	char *o = s;
	for (u64 i = 0; i < n; i++) {
		u32 h = p[i] >> 4, l = p[i] & 15;
		*o++ = (char)(h < 10 ? '0' + h : 'a' + h - 10);
		*o++ = (char)(l < 10 ? '0' + l : 'a' + l - 10);
	}
	*o++ = '\n';
	*o = 0;
	ak_puts(s);
}

/* bits à 1 d'un mot (cnt + addv dans l'asm) */
static u64 popcount64(u64 v)
{
	v = v - ((v >> 1) & 0x5555555555555555UL);
	v = (v & 0x3333333333333333UL) + ((v >> 2) & 0x3333333333333333UL);
	v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0FUL;
	return (v * 0x0101010101010101UL) >> 56;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	for (int i = 0; i < 32; i++) /* vecteur : clé 00..1f, compteur 1, nonce 000000090000004a00000000 */
		key[i] = (u8)i;
	chacha20_block(key, 1, nonce, block);
	int ok = 1;
	for (int i = 0; i < 16; i++)
		if (block[i] != expected[i]) {
			ok = 0;
			break;
		}
	ak_puts(ok ? "rngtest : ChaCha20 conforme (RFC 8439, 2.3.2)\n" : "rngtest : ChaCha20 FAUX\n");
	for (int t = 0; t < 2; t++) {
		if (korin_sys2(SYS_GETRANDOM, block, 32) != 32) {
			ak_puts("rngtest : getrandom en erreur\n");
			korin_sys1(SYS_EXIT, 1);
		}
		ak_puts("rngtest : getrandom 32 o : ");
		hex(block, 32);
	}
	u8 *m = ak_malloc(1048576); /* débit, bits */
	if (!m)
		return 0;
	long t = korin_sys0(SYS_TIME_MS);
	for (u64 n = 0; n < 1048576; n += 65536)
		korin_sys2(SYS_GETRANDOM, m + n, 65536);
	t = korin_sys0(SYS_TIME_MS) - t;
	u64 bits = 0;
	for (u64 i = 0; i < 1048576; i += 8)
		bits += popcount64(*(u64 *)(m + i));
	ak_puts("rngtest : 1 Mo en ");
	print_dec(t);
	ak_puts(" ms, bits a 1 : ");
	print_dec((long)bits);
	ak_puts(" sur 8388608\n");
	return 0;
}
