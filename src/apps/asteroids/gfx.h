// gfx.h -- dessin logiciel lissé dans un tampon XRGB8888 (la fenêtre) : polygones, disques, anneaux avec
// transparence, lumières additives. Même principe que src/inc/gameart.h des jeux du système, en C.
#ifndef ASTEROIDS_GFX_H
#define ASTEROIDS_GFX_H

#include <stdint.h>

typedef struct
{
	uint32_t *px;			// les pixels
	int w, h, stride;		// taille, pixels par ligne
	int cx0, cy0, cx1, cy1;		// la découpe
} gfx_img;

extern gfx_img G;			// la cible courante

void gfx_target (uint32_t *px, int w, int h, int stride);
void gfx_clip (int x0, int y0, int x1, int y1);
void gfx_noclip (void);

unsigned gfx_mix (unsigned a, unsigned b, int k);	// k 0..256 : vers b
unsigned gfx_shade (unsigned c, int k);		// k 256 : la même ; plus : plus claire

// a : opacité 0..256 ; coordonnées en pixels (flottants)
void gfx_rect (int x, int y, int w, int h, unsigned c, int a);
void gfx_poly (const float *xy, int n, unsigned c, int a);
void gfx_disc (float x, float y, float r, unsigned c, int a);
void gfx_ring (float x, float y, float r, float w, unsigned c, int a);
void gfx_line (float x0, float y0, float x1, float y1, float w, unsigned c, int a);
void gfx_rbox (int x, int y, int w, int h, int r, unsigned top, unsigned bot, int a, unsigned line);

// lumières ajoutées : c au centre (multipliée par k / 256), rien au bord
void gfx_glow (float x, float y, float r, unsigned c, int k);
void gfx_glow_ring (float x, float y, float r, float w, unsigned c, int k);
void gfx_add_all (unsigned c);			// un éclair sur toute l'image
void gfx_dim (int k);				// toute l'image plus sombre (k 0..256 gardé)

#endif
