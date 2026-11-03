//
// imgload.h -- pictures read into 0xAARRGGBB pixels (Onyx's imgload.hpp): BMP, GIF, PNG, JPEG by stb_image, WebP by
// simplewebp, PCX by our own decoder (image.cpp).
//
#ifndef _paint_imgload_h
#define _paint_imgload_h

#define IMG_MAX_BYTES	(64u * 1024 * 1024)	// refuse bigger files

// The first frame (new unsigned[], the caller's): *w x *h; 0: not a picture it can read.
unsigned *img_decode (const unsigned char *d, unsigned len, int *w, int *h);
unsigned *img_load (const char *path, int *w, int *h);
// A deflate stream inflated (zlib: with its zlib header; else raw, as a ZIP entry's): a new unsigned char[] of *outLen
// bytes (delete []), or 0.
unsigned char *img_inflate (const void *data, unsigned len, bool zlib, unsigned *outLen);
bool img_is_image_name (const char *name);	// by extension (bmp gif png jpg jpeg jpe pcx webp)

#endif
