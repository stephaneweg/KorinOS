//
// image.cpp -- the decoders, in a unit of their own (Onyx's imgload.hpp): stb_image (public domain) for BMP, GIF, PNG,
// JPEG, simplewebp (BSD-3) for WebP, PCX here. A GIF gives its first frame.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "imgload.h"

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb_image.h"

#define NDEBUG
#define SIMPLEWEBP_DISABLE_STDIO
#define SIMPLEWEBP_IMPLEMENTATION
#include "simplewebp.h"
#pragma GCC diagnostic pop

// RGBA bytes -> a new unsigned[] of 0xAARRGGBB.
static unsigned *frame_from_rgba (const unsigned char *p, long count)
{
	unsigned *o = new unsigned[count];
	for (long i = 0; i < count; i++)
		o[i] = ((unsigned) p[i * 4 + 3] << 24) | ((unsigned) p[i * 4] << 16) | ((unsigned) p[i * 4 + 1] << 8) | p[i * 4 + 2];
	return o;
}

// ---- PCX (ZSoft): RLE scanlines of `planes` planes x `bpl` bytes ------------------------
static unsigned *img_pcx (const unsigned char *d, unsigned len, int *pw, int *ph)
{
	if (len < 128 || d[0] != 0x0A || d[2] != 1) return 0;
	int bpp = d[3], planes = d[65], bpl = d[66] | (d[67] << 8);
	int w = (d[8] | (d[9] << 8)) - (d[4] | (d[5] << 8)) + 1;
	int h = (d[10] | (d[11] << 8)) - (d[6] | (d[7] << 8)) + 1;
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || bpl <= 0 || planes < 1 || planes > 4) return 0;
	unsigned pal[256];
	for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u;
	for (int i = 0; i < 16; i++) pal[i] = 0xFF000000u | (d[16 + i * 3] << 16) | (d[17 + i * 3] << 8) | d[18 + i * 3];
	if (bpp == 8 && planes == 1 && len >= 769 && d[len - 769] == 0x0C)	// VGA palette at the end
		for (int i = 0; i < 256; i++)
			pal[i] = 0xFF000000u | (d[len - 768 + i * 3] << 16) | (d[len - 767 + i * 3] << 8) | d[len - 766 + i * 3];
	if (bpp == 1 && planes == 1) { pal[0] = 0xFF000000u; pal[1] = 0xFFFFFFFFu; }
	unsigned *out = new unsigned[(unsigned long) w * h];
	unsigned char *line = (unsigned char *) malloc ((size_t) bpl * planes);
	if (!line) { delete[] out; return 0; }
	unsigned pos = 128;
	for (int y = 0; y < h; y++)
	{
		int total = bpl * planes;
		for (int i = 0; i < total; )		// RLE: 0b11xxxxxx = run length, then a byte
		{
			unsigned char b = pos < len ? d[pos++] : 0, v = b;
			int run = 1;
			if ((b & 0xC0) == 0xC0) { run = b & 0x3F; v = pos < len ? d[pos++] : 0; }
			while (run-- > 0 && i < total) line[i++] = v;
		}
		unsigned *row = out + (unsigned long) y * w;
		for (int x = 0; x < w; x++)
		{
			if (bpp == 8 && planes >= 3)				// 24-bit: R, G, B planes
				row[x] = 0xFF000000u | (line[x] << 16) | (line[bpl + x] << 8) | line[2 * bpl + x];
			else if (bpp == 8)
				row[x] = pal[line[x]];
			else							// 1/2/4-bit, 1..4 planes
			{
				int idx = 0;
				for (int p = 0; p < planes; p++)
				{
					int bit = x * bpp, byte = p * bpl + bit / 8, sh = 8 - bpp - bit % 8;
					idx |= ((line[byte] >> sh) & ((1 << bpp) - 1)) << (p * bpp);
				}
				row[x] = pal[idx & 255];
			}
		}
	}
	free (line);
	*pw = w; *ph = h;
	return out;
}

// ---- WebP (simplewebp) -------------------------------------------------------------------
static void *wa (void *, size_t n) { return malloc (n); }
static void wf (void *, void *p) { free (p); }
static unsigned *img_webp (const unsigned char *d, unsigned len, int *pw, int *ph)
{
	simplewebp_allocator al = { wa, wf, 0 };
	simplewebp *wp = 0;
	if (simplewebp_load_from_memory ((void *) d, len, &al, &wp) != SIMPLEWEBP_NO_ERROR) return 0;
	size_t w = 0, h = 0;
	simplewebp_get_dimensions (wp, &w, &h);
	unsigned char *rgba = (unsigned char *) malloc (w * h * 4);
	if (!rgba || simplewebp_decode (wp, rgba, 0) != SIMPLEWEBP_NO_ERROR) { free (rgba); simplewebp_unload (wp); return 0; }
	simplewebp_unload (wp);
	unsigned *px = frame_from_rgba (rgba, (long) (w * h));
	free (rgba);
	*pw = (int) w; *ph = (int) h;
	return px;
}

unsigned *img_decode (const unsigned char *d, unsigned len, int *w, int *h)
{
	if (len >= 12 && !memcmp (d, "RIFF", 4) && !memcmp (d + 8, "WEBP", 4)) return img_webp (d, len, w, h);
	if (len >= 128 && d[0] == 0x0A && d[2] == 1) return img_pcx (d, len, w, h);
	int comp = 0;
	stbi_uc *rgba = stbi_load_from_memory (d, (int) len, w, h, &comp, 4);	// (a GIF: its first frame)
	if (!rgba) return 0;
	unsigned *px = frame_from_rgba (rgba, (long) *w * *h);
	stbi_image_free (rgba);
	return px;
}

unsigned *img_load (const char *path, int *w, int *h)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	struct stat st;
	if (fstat (f, &st) != 0 || st.st_size <= 0 || (unsigned long) st.st_size > IMG_MAX_BYTES) { close (f); return 0; }
	unsigned n = (unsigned) st.st_size, got = 0;
	unsigned char *b = (unsigned char *) malloc (n);
	while (b && got < n) { long r = read (f, b + got, n - got); if (r <= 0) break; got += (unsigned) r; }
	close (f);
	unsigned *px = b && got == n ? img_decode (b, n, w, h) : 0;
	free (b);
	return px;
}

unsigned char *img_inflate (const void *data, unsigned len, bool zlib, unsigned *outLen)
{
	int n = 0;
	char *r = zlib ? stbi_zlib_decode_malloc ((const char *) data, (int) len, &n)
		       : stbi_zlib_decode_noheader_malloc ((const char *) data, (int) len, &n);
	if (!r || n < 0) { free (r); return 0; }
	unsigned char *o = new unsigned char[n > 0 ? n : 1];
	memcpy (o, r, (size_t) n);
	free (r);
	*outLen = (unsigned) n;
	return o;
}

bool img_is_image_name (const char *n)
{
	static const char *const ext[] = { "bmp", "gif", "png", "jpg", "jpeg", "jpe", "pcx", "webp", 0 };
	const char *d = strrchr (n, '.');
	if (!d) return false;
	for (int e = 0; ext[e]; e++) if (!strcasecmp (d + 1, ext[e])) return true;
	return false;
}
