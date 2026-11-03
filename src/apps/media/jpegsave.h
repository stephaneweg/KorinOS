//
// jpegsave.h -- a picture as a baseline JPEG (Onyx's imagekit/img/pngsave.hpp, the JPEG part): the videos' frames kept on the card.
//
#ifndef _media_jpegsave_h
#define _media_jpegsave_h

#include <stdlib.h>
#include <string.h>

namespace media {

// a growing buffer
struct Buf
{
	unsigned char *b; unsigned n, cap;
	Buf () : b (0), n (0), cap (0) {}
	void grow (unsigned k)
	{
		if (n + k <= cap) return;
		unsigned c = cap ? cap * 2 : 65536; while (c < n + k) c *= 2;
		unsigned char *t = new unsigned char[c];
		for (unsigned i = 0; i < n; i++) t[i] = b[i];
		delete[] b; b = t; cap = c;
	}
	void put (unsigned char c) { grow (1); b[n++] = c; }
	void put (const void *p, unsigned k) { grow (k); memcpy (b + n, p, k); n += k; }
	unsigned char *take (unsigned *len) { unsigned char *r = b; *len = n; b = 0; n = cap = 0; return r; }
};

// ---- JPEG -----------------------------------------------------------------------------------------------
static const unsigned char ZIGZAG[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const unsigned char QLUM[64] = {
	16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
	18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
static const unsigned char QCHR[64] = {
	17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };
static const unsigned char DC_LUM_BITS[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned char DC_CHR_BITS[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const unsigned char DC_VALS[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const unsigned char AC_LUM_BITS[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D };
static const unsigned char AC_LUM_VALS[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
	0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
	0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
	0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94,
	0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
	0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
	0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
	0xF9, 0xFA };
static const unsigned char AC_CHR_BITS[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const unsigned char AC_CHR_VALS[162] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81,
	0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
	0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92,
	0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
	0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
	0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
	0xF9, 0xFA };
// The DCT's basis, x 4096: C(u) / 2 cos ((2x + 1) u pi / 16).
static const short DCT_T[64] = {
	1448, 1448, 1448, 1448, 1448, 1448, 1448, 1448, 2009, 1703, 1138, 400, -400, -1138, -1703, -2009,
	1892, 784, -784, -1892, -1892, -784, 784, 1892, 1703, -400, -2009, -1138, 1138, 2009, 400, -1703,
	1448, -1448, -1448, 1448, 1448, -1448, -1448, 1448, 1138, -2009, 400, 1703, -1703, -400, 2009, -1138,
	784, -1892, 1892, -784, -784, 1892, -1892, 784, 400, -1138, 1703, -2009, 2009, -1703, 1138, -400 };

struct Huff { unsigned short code[256]; unsigned char size[256]; };
static void huff_make (Huff &h, const unsigned char *bits, const unsigned char *vals)
{
	unsigned code = 0; int k = 0;
	for (int i = 0; i < 256; i++) h.size[i] = 0;
	for (int len = 1; len <= 16; len++)
	{
		for (int i = 0; i < bits[len - 1]; i++) { h.code[vals[k]] = (unsigned short) code; h.size[vals[k]] = (unsigned char) len; k++; code++; }
		code <<= 1;
	}
}

struct JpegOut
{
	Buf o; unsigned acc; int n;			// (the bits, MSB first)
	JpegOut () : acc (0), n (0) {}
	void bits (unsigned v, int k)
	{
		for (int i = k - 1; i >= 0; i--)
		{
			acc = acc << 1 | ((v >> i) & 1);
			if (++n == 8) { o.put ((unsigned char) acc); if ((acc & 255) == 255) o.put (0); acc = 0; n = 0; }
		}
	}
	void flush () { while (n) bits (1, 1); }
	void w16 (unsigned v) { o.put ((unsigned char) (v >> 8)); o.put ((unsigned char) v); }
};

static void jpeg_block (JpegOut &j, const int *f, const unsigned char *q, int &prevDC, const Huff &dc, const Huff &ac)
{
	long long tmp[64];
	for (int y = 0; y < 8; y++)					// (rows, then columns)
		for (int u = 0; u < 8; u++)
		{
			long long s = 0;
			for (int x = 0; x < 8; x++) s += (long long) f[y * 8 + x] * DCT_T[u * 8 + x];
			tmp[y * 8 + u] = s;
		}
	int coef[64], zz[64];
	for (int v = 0; v < 8; v++)
		for (int u = 0; u < 8; u++)
		{
			long long sum = 0;
			for (int y = 0; y < 8; y++) sum += tmp[y * 8 + u] * DCT_T[v * 8 + y];
			long long d = (long long) q[v * 8 + u] << 24;		// (F / 4096^2, then / q, rounded)
			coef[v * 8 + u] = (int) (sum >= 0 ? (sum + d / 2) / d : -((-sum + d / 2) / d));
		}
	for (int k = 0; k < 64; k++) zz[k] = coef[ZIGZAG[k]];
	auto category = [] (int v) { int a = v < 0 ? -v : v, s = 0; while (a) { s++; a >>= 1; } return s; };
	int diff = zz[0] - prevDC; prevDC = zz[0];
	int s = category (diff);
	j.bits (dc.code[s], dc.size[s]);
	if (s) j.bits ((unsigned) (diff < 0 ? diff + (1 << s) - 1 : diff) & ((1u << s) - 1), s);
	int run = 0;
	for (int k = 1; k < 64; k++)
	{
		int v = zz[k];
		if (v == 0) { run++; continue; }
		while (run > 15) { j.bits (ac.code[0xF0], ac.size[0xF0]); run -= 16; }
		s = category (v);
		int sym = run << 4 | s;
		j.bits (ac.code[sym], ac.size[sym]);
		j.bits ((unsigned) (v < 0 ? v + (1 << s) - 1 : v) & ((1u << s) - 1), s);
		run = 0;
	}
	if (run) j.bits (ac.code[0], ac.size[0]);
}

// w x h pixels -> a JPEG file (quality 1..100; the clear pixels on white).
static unsigned char *jpeg_encode (const unsigned *px, int w, int h, int quality, unsigned *outLen)
{
	if (quality < 1) quality = 1;
	if (quality > 100) quality = 100;
	int sc = quality < 50 ? 5000 / quality : 200 - quality * 2;
	unsigned char ql[64], qc[64];
	for (int i = 0; i < 64; i++)
	{
		int a = (QLUM[i] * sc + 50) / 100, b = (QCHR[i] * sc + 50) / 100;
		ql[i] = (unsigned char) (a < 1 ? 1 : a > 255 ? 255 : a); qc[i] = (unsigned char) (b < 1 ? 1 : b > 255 ? 255 : b);
	}
	Huff dcl, dcc, acl, acc;
	huff_make (dcl, DC_LUM_BITS, DC_VALS); huff_make (dcc, DC_CHR_BITS, DC_VALS);
	huff_make (acl, AC_LUM_BITS, AC_LUM_VALS); huff_make (acc, AC_CHR_BITS, AC_CHR_VALS);
	JpegOut j;
	Buf &o = j.o;
	o.put (0xFF); o.put (0xD8);						// SOI
	o.put (0xFF); o.put (0xE0); j.w16 (16); o.put ("JFIF", 5); o.put (1); o.put (1); o.put (0); j.w16 (1); j.w16 (1); o.put (0); o.put (0);
	for (int t = 0; t < 2; t++)						// DQT (zigzag order)
	{
		o.put (0xFF); o.put (0xDB); j.w16 (67); o.put ((unsigned char) t);
		for (int k = 0; k < 64; k++) o.put ((t ? qc : ql)[ZIGZAG[k]]);
	}
	o.put (0xFF); o.put (0xC0); j.w16 (17); o.put (8); j.w16 ((unsigned) h); j.w16 ((unsigned) w); o.put (3);	// SOF0
	o.put (1); o.put (0x22); o.put (0); o.put (2); o.put (0x11); o.put (1); o.put (3); o.put (0x11); o.put (1);
	struct T { int cls, id; const unsigned char *bits, *vals; int n; } tabs[4] = {
		{ 0, 0, DC_LUM_BITS, DC_VALS, 12 }, { 1, 0, AC_LUM_BITS, AC_LUM_VALS, 162 },
		{ 0, 1, DC_CHR_BITS, DC_VALS, 12 }, { 1, 1, AC_CHR_BITS, AC_CHR_VALS, 162 } };
	for (int t = 0; t < 4; t++)						// DHT
	{
		o.put (0xFF); o.put (0xC4); j.w16 ((unsigned) (19 + tabs[t].n)); o.put ((unsigned char) (tabs[t].cls << 4 | tabs[t].id));
		o.put (tabs[t].bits, 16); o.put (tabs[t].vals, (unsigned) tabs[t].n);
	}
	o.put (0xFF); o.put (0xDA); j.w16 (12); o.put (3);			// SOS
	o.put (1); o.put (0x00); o.put (2); o.put (0x11); o.put (3); o.put (0x11); o.put (0); o.put (63); o.put (0);
	int pdY = 0, pdCb = 0, pdCr = 0;
	int Y[4][64], Cb[64], Cr[64];
	for (int my = 0; my < h; my += 16)
		for (int mx = 0; mx < w; mx += 16)
		{
			int sb[64], sr[64];
			for (int i = 0; i < 64; i++) sb[i] = sr[i] = 0;
			for (int yy = 0; yy < 16; yy++)
				for (int xx = 0; xx < 16; xx++)
				{
					int X = mx + xx < w ? mx + xx : w - 1, Yy = my + yy < h ? my + yy : h - 1;
					unsigned c = px[(unsigned) Yy * w + X];
					int r = (int) (c >> 16 & 255), g = (int) (c >> 8 & 255), b = (int) (c & 255);
					int lum = (77 * r + 150 * g + 29 * b + 128) >> 8;
					Y[(yy >> 3) * 2 + (xx >> 3)][(yy & 7) * 8 + (xx & 7)] = lum - 128;
					sb[(yy >> 1) * 8 + (xx >> 1)] += ((-43 * r - 85 * g + 128 * b) >> 8);
					sr[(yy >> 1) * 8 + (xx >> 1)] += ((128 * r - 107 * g - 21 * b) >> 8);
				}
			for (int i = 0; i < 64; i++) { Cb[i] = (sb[i] + 2) >> 2; Cr[i] = (sr[i] + 2) >> 2; }
			for (int k = 0; k < 4; k++) jpeg_block (j, Y[k], ql, pdY, dcl, acl);
			jpeg_block (j, Cb, qc, pdCb, dcc, acc);
			jpeg_block (j, Cr, qc, pdCr, dcc, acc);
		}
	j.flush ();
	o.put (0xFF); o.put (0xD9);						// EOI
	return o.take (outLen);
}

} // namespace media

#endif
