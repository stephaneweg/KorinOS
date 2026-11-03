//
// jpeg.c -- JPEG pictures: UIKit reads BMP and PNG; a presentation's photos are JPEG. stb_image (public domain, as in
// Onyx's imagekit), the JPEG part only, in a unit of its own.
//
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"

// The picture as 0xFFRRGGBB (malloc), *w x *h; 0: not one it can read.
unsigned *jpeg_decode (const unsigned char *b, int n, int *w, int *h)
{
	int comp;
	unsigned char *rgb = stbi_load_from_memory (b, n, w, h, &comp, 3);
	if (!rgb) return 0;
	long np = (long) *w * *h;
	unsigned *px = (unsigned *) malloc ((size_t) np * 4);
	if (px)
		for (long i = 0; i < np; i++)
			px[i] = 0xFF000000u | (unsigned) rgb[3 * i] << 16 | (unsigned) rgb[3 * i + 1] << 8 | rgb[3 * i + 2];
	stbi_image_free (rgb);
	return px;
}
