/* SHA-256 (core/sha256.c) */
#ifndef KORIN_SHA256_H
#define KORIN_SHA256_H

#include <stdint.h>

struct sha256 {
	uint32_t h[8];
	uint64_t len;
	uint8_t buf[64];
	uint32_t n;
};

void sha256_init(struct sha256 *s);
void sha256_update(struct sha256 *s, const void *data, uint64_t len);
void sha256_final(struct sha256 *s, uint8_t out[32]);

#endif
