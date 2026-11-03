//
// image.c -- a message's pictures: JPEG, PNG, GIF (its first frame) and BMP by stb_image (public domain, as in Onyx's
// imagekit), in a unit of its own. One copy of stb_image in the tree: ../media/stb_image.h.
//
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 16384
#include "../media/stb_image.h"

// The picture as 0xAARRGGBB (malloc), *w x *h; 0: not one it can read.
unsigned *image_decode (const unsigned char *b, int n, int *w, int *h)
{
	int comp;
	unsigned char *rgba = stbi_load_from_memory (b, n, w, h, &comp, 4);
	if (!rgba) return 0;
	long np = (long) *w * *h;
	unsigned *px = (unsigned *) malloc ((size_t) np * 4);
	if (px)
		for (long i = 0; i < np; i++)
			px[i] = (unsigned) rgba[4 * i + 3] << 24 | (unsigned) rgba[4 * i] << 16 | (unsigned) rgba[4 * i + 1] << 8 | rgba[4 * i + 2];
	stbi_image_free (rgba);
	return px;
}
