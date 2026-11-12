/* lz4.h : le codeur LZ4 (format bloc) de rdpd ; traduction de lz4_compress
 * (rdpd.asm), lz4.c. */
#ifndef RDPD_LZ4_H
#define RDPD_LZ4_H

#define LZ4_HT_BYTES (4096 * 4) /* table de hachage (12 bits) */

/* source, octets, destination (place : n + n / 255 + 16), table de
 * LZ4_HT_BYTES octets -> octets compressés */
u64 lz4_compress(const u8 *src, u64 n, u8 *dst, u32 *htab);

#endif
