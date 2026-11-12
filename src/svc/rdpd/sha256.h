/* sha256.h : SHA-256 (FIPS 180-4) et HMAC-SHA256 (RFC 2104) pour le défi de
 * rdpd ; traduction de sha256.inc (sha256.c). */
#ifndef RDPD_SHA256_H
#define RDPD_SHA256_H

/* données, octets -> 32 octets */
void sha256(const u8 *data, u64 n, u8 *out);
/* clé (64 octets au plus : le mot de passe, 63), message (64 octets au plus)
 * -> 32 octets */
void hmac_sha256(const u8 *key, u64 klen, const u8 *m, u64 mlen, u8 *out);

#endif
