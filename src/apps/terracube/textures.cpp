// textures.cpp -- TerraCube : l'atlas des blocs, dessiné ici au départ (aucun fichier d'image) : 16 × 16 tuiles de
// 16 px, octets R, G, B, A (glTexImage2D en GL_RGBA). Les couleurs sont celles des matières (données du jeu, pas de
// l'interface : l'interface prend les jetons du thème).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "render.h"

#include <math.h>
#include <string.h>
#include "noise.h"

namespace tc {

namespace {

uint8_t *g_px;		// 256 × 256 × 4

inline int clamp255 (int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

void put (int tile, int x, int y, unsigned rgb, int a = 255)
{
	int X = (tile & 15) * 16 + x, Y = (tile >> 4) * 16 + y;
	uint8_t *p = g_px + (Y * 256 + X) * 4;
	p[0] = (uint8_t) (rgb >> 16), p[1] = (uint8_t) (rgb >> 8), p[2] = (uint8_t) rgb, p[3] = (uint8_t) a;
}

unsigned shade (unsigned rgb, int d)
{
	int r = clamp255 ((int) (rgb >> 16 & 255) + d), g = clamp255 ((int) (rgb >> 8 & 255) + d), b = clamp255 ((int) (rgb & 255) + d);
	return (unsigned) (r << 16 | g << 8 | b);
}

unsigned mixc (unsigned a, unsigned b, float t)
{
	int r = (int) ((a >> 16 & 255) * (1 - t) + (b >> 16 & 255) * t), g = (int) ((a >> 8 & 255) * (1 - t) + (b >> 8 & 255) * t),
	    bl = (int) ((a & 255) * (1 - t) + (b & 255) * t);
	return (unsigned) (r << 16 | g << 8 | bl);
}

inline float rnd (int tile, int x, int y, int salt = 0) { return rand01 (x, y, tile * 7 + salt, 0x7a11e5u); }

// du bruit autour d'une couleur
void noisy (int tile, unsigned base, int amp, int salt = 0)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) put (tile, x, y, shade (base, (int) ((rnd (tile, x, y, salt) - 0.5f) * 2 * amp)));
}

void specks (int tile, unsigned col, float p, int salt)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++)
			if (rnd (tile, x, y, salt) < p) put (tile, x, y, shade (col, (int) (rnd (tile, x, y, salt + 1) * 30) - 15));
}

void ore (int tile, unsigned col)
{
	noisy (tile, 0x7f7f7f, 14);
	// trois petits amas
	for (int k = 0; k < 4; k++) {
		int cx = 2 + (int) (rnd (tile, k, 0, 9) * 12), cy = 2 + (int) (rnd (tile, k, 1, 9) * 12);
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
				if (rnd (tile, cx + dx, cy + dy, 10) < 0.7f)
					put (tile, (cx + dx) & 15, (cy + dy) & 15, shade (col, (int) (rnd (tile, dx, dy, k) * 40) - 20));
	}
}

void wool (int tile, unsigned col)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			int d = (int) ((rnd (tile, x, y) - 0.5f) * 18) + (((x + y) & 3) == 0 ? -8 : 0);
			put (tile, x, y, shade (col, d));
		}
}

void bricks (int tile, unsigned brick, unsigned mortar, int bw, int bh)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			int row = y / bh, off = (row & 1) ? bw / 2 : 0;
			bool m = y % bh == bh - 1 || (x + off) % bw == bw - 1;
			put (tile, x, y, m ? shade (mortar, (int) (rnd (tile, x, y) * 16) - 8) : shade (brick, (int) ((rnd (tile, x, y, 1) - 0.5f) * 28)));
		}
}

void plant (int tile, unsigned stem, unsigned petal, bool flower)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) put (tile, x, y, 0, 0);
	if (!flower) {
		// des brins
		for (int k = 0; k < 7; k++) {
			int x0 = 1 + (int) (rnd (tile, k, 0) * 14), h = 6 + (int) (rnd (tile, k, 1) * 9);
			float lean = (rnd (tile, k, 2) - 0.5f) * 0.6f;
			for (int i = 0; i < h; i++) {
				int x = x0 + (int) (lean * i), y = 15 - i;
				if (x >= 0 && x < 16) put (tile, x, y, shade (stem, i * 3 - 10 + (int) (rnd (tile, x, y) * 16)));
			}
		}
		return;
	}
	for (int y = 7; y < 16; y++) put (tile, 7 + (y > 11), y, shade (stem, -10 + y));
	put (tile, 6, 11, stem), put (tile, 5, 10, stem), put (tile, 9, 12, stem), put (tile, 10, 11, stem);
	static const int P[][2] = { { 7, 3 }, { 6, 4 }, { 8, 4 }, { 7, 5 }, { 6, 2 }, { 8, 2 }, { 5, 3 }, { 9, 3 }, { 6, 6 }, { 8, 6 }, { 5, 5 }, { 9, 5 } };
	for (auto &p : P) put (tile, p[0], p[1], shade (petal, (int) (rnd (tile, p[0], p[1]) * 30) - 15));
	put (tile, 7, 4, 0xf4e04a);
}


// les blocs du mode survie, les objets, les outils, les chiffres de la barre
static void clearTile (int t)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) put (t, x, y, 0, 0);
}

static void dot (int t, float fx, float fy, unsigned c)
{
	int x = (int) floorf (fx + 0.5f), y = (int) floorf (fy + 0.5f);
	if (x >= 0 && x < 16 && y >= 0 && y < 16) put (t, x, y, c);
}

// un manche en diagonale, du bas à gauche vers le haut à droite
static void handle (int t, int x0, int x1)
{
	for (int x = x0; x <= x1; x++) {
		put (t, x, 15 - x - 1, x & 1 ? 0x7a5530 : 0x8f6a3e);
		put (t, x + 1, 15 - x - 1, 0x4a321a);
	}
}

static void toolTile (int t, int kind, unsigned mat)
{
	clearTile (t);
	handle (t, 1, kind == 2 ? 10 : 9);
	unsigned dk = shade (mat, -55), lt = shade (mat, 40);
	if (kind == 1) {	// pioche : un fer courbe en travers du manche
		for (float s = -6.5f; s <= 6.5f; s += 0.25f) {
			float bx = 10.5f + s * 0.707f, by = 3.5f + s * 0.707f;
			float c = s * s / 9.0f;
			bx -= c * 0.707f, by += c * 0.707f;
			dot (t, bx, by, mat);
			dot (t, bx + 0.8f, by - 0.6f, s < 0 ? lt : dk);
		}
	} else if (kind == 2) {	// hache : une lame d'un côté
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				float dx = x - 12.0f, dy = y - 5.0f;
				if (dx * dx + dy * dy < 13.0f && x - y > 4) put (t, x, y, dx + dy < -1 ? lt : dx > 1.5f ? dk : mat);
			}
	} else {		// pelle : une lame ronde au bout
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				float dx = x - 12.0f, dy = y - 3.5f;
				if (dx * dx + dy * dy < 9.5f) put (t, x, y, dx + dy < -1.5f ? lt : dx + dy > 1.5f ? dk : mat);
			}
	}
}

static void extraTiles (uint8_t *)
{
	// établi : des planches, une grille d'outils dessus
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			bool frame = x == 0 || y == 0 || x == 15 || y == 15;
			bool grid = (x == 5 || x == 10 || y == 5 || y == 10) && !frame;
			put (T_WORKBENCH_TOP, x, y, frame ? 0x6a4a28 : grid ? 0x7a5a32 : shade (0xb48a54, (int) (rnd (T_WORKBENCH_TOP, x, y) * 14) - 7));
			unsigned side;
			if (y < 4) side = y == 3 ? 0x6a4a28 : shade (0xb48a54, (int) (rnd (T_WORKBENCH_SIDE, x, y) * 12) - 6);
			else if (x < 3 || x > 12) side = shade (0x8a6238, (int) (rnd (T_WORKBENCH_SIDE, x, y) * 12) - 6);	// les pieds
			else side = 0x3a2a18;
			if (y >= 6 && y <= 10 && x >= 5 && x <= 10 && (x == 7 || y == 8)) side = 0xa9a9a9;	// une scie accrochée
			put (T_WORKBENCH_SIDE, x, y, side);
		}
	// four : de la pierre, une bouche qui rougeoie
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			unsigned st = shade (0x7c7c7c, (int) (rnd (T_FURNACE_SIDE, x, y) * 22) - 11 + ((x % 5 == 0 || y % 4 == 0) ? -16 : 0));
			if (x >= 4 && x <= 11 && y >= 8 && y <= 13) {
				float hot = (y - 8) / 5.0f;
				st = y == 8 || x == 4 || x == 11 ? 0x2a2a2a : mixc (0x3a1a08, 0xff9a2a, hot * (0.6f + rnd (T_FURNACE_SIDE, x, y) * 0.4f));
			}
			put (T_FURNACE_SIDE, x, y, st);
			put (T_FURNACE_TOP, x, y, shade (0x8a8a8a, (int) (rnd (T_FURNACE_TOP, x, y) * 16) - 8 + (x == 0 || y == 0 || x == 15 || y == 15 ? -24 : 0)));
		}
	// bâton, charbon, lingots, cristal
	clearTile (T_STICK);
	handle (T_STICK, 2, 12);
	clearTile (T_COAL);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float dx = x - 7.5f, dy = y - 8.0f;
			if (dx * dx * 0.8f + dy * dy < 26) put (T_COAL, x, y, dx + dy < -4 ? 0x5a5a5a : shade (0x262626, (int) (rnd (T_COAL, x, y) * 20) - 10));
		}
	for (int k = 0; k < 2; k++) {
		int t = k ? T_GOLD_INGOT : T_IRON_INGOT;
		unsigned c = k ? 0xf2c832 : 0xd2d2d8;
		clearTile (t);
		for (int y = 5; y <= 11; y++)
			for (int x = 1 + (11 - y) / 2; x <= 14 - (11 - y) / 2; x++)
				put (t, x, y, y == 5 ? shade (c, 40) : y == 11 ? shade (c, -60) : x < 4 + (11 - y) / 2 ? shade (c, 20) : c);
	}
	clearTile (T_CRYSTAL);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float d = fabsf (x - 7.5f) + fabsf (y - 7.5f) * 0.8f;
			if (d < 6.5f) put (T_CRYSTAL, x, y, x < 8 && y < 8 ? 0xd8fffe : x >= 8 && y >= 8 ? 0x2a9ea0 : 0x5fe6e0);
		}
	static const unsigned MAT[4] = { 0xa07a48, 0x8a8a8a, 0xdcdce2, 0x5fe6e0 };
	for (int m = 0; m < 4; m++) {
		toolTile (T_PICK0 + m, 1, MAT[m]);
		toolTile (T_AXE0 + m, 2, MAT[m]);
		toolTile (T_SHOVEL0 + m, 3, MAT[m]);
	}
	// chiffres 3 × 5, grossis trois fois, bordés de noir (les nombres de la barre)
	static const char *DIG[10] = { "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
				       "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111" };
	for (int d = 0; d < 10; d++) {
		int t = T_DIGIT0 + d;
		clearTile (t);
		for (int pass = 0; pass < 2; pass++)
			for (int r = 0; r < 5; r++)
				for (int c = 0; c < 3; c++) {
					if (DIG[d][r * 3 + c] != '1') continue;
					for (int yy = 0; yy < 3; yy++)
						for (int xx = 0; xx < 3; xx++) {
							int x = 2 + c * 3 + xx, y = r * 3 + yy;
							if (pass == 0) {
								for (int oy = -1; oy <= 1; oy++)
									for (int ox = -1; ox <= 1; ox++)
										if (x + ox >= 0 && x + ox < 16 && y + oy >= 0 && y + oy < 16) put (t, x + ox, y + oy, 0x101010);
							} else put (t, x, y, 0xffffff);
						}
				}
	}
}

} // namespace

void buildAtlas (uint8_t *px)
{
	g_px = px;
	memset (px, 0, 256 * 256 * 4);
	noisy (T_STONE, 0x7f7f7f, 14);
	specks (T_STONE, 0x6a6a6a, 0.12f, 3);
	// herbe
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float r = rnd (T_GRASS_TOP, x, y);
			put (T_GRASS_TOP, x, y, shade (mixc (0x5fa83c, 0x4c9a33, r), (int) (r * 20) - 10));
		}
	noisy (T_DIRT, 0x8b5d36, 16);
	specks (T_DIRT, 0x6e4728, 0.15f, 4);
	specks (T_DIRT, 0xa4764a, 0.08f, 5);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			int edge = 3 + (int) (rnd (T_GRASS_SIDE, x, 0, 2) * 3);
			if (y < edge) put (T_GRASS_SIDE, x, y, shade (0x5ba53a, (int) (rnd (T_GRASS_SIDE, x, y) * 22) - 11));
			else {
				const uint8_t *d = px + (((T_DIRT >> 4) * 16 + y) * 256 + (T_DIRT & 15) * 16 + x) * 4;
				put (T_GRASS_SIDE, x, y, (unsigned) (d[0] << 16 | d[1] << 8 | d[2]));
			}
		}
	// pavés : des cellules arrondies
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float best = 99, second = 99;
			int id = 0;
			for (int k = 0; k < 9; k++) {
				float cx = (k % 3) * 5.5f + 2 + rnd (T_COBBLE, k, 0) * 3, cy = (k / 3) * 5.5f + 2 + rnd (T_COBBLE, k, 1) * 3;
				float dx = fminf (fabsf (x - cx), 16 - fabsf (x - cx)), dy = fminf (fabsf (y - cy), 16 - fabsf (y - cy));
				float d = sqrtf (dx * dx + dy * dy);
				if (d < best) second = best, best = d, id = k;
				else if (d < second) second = d;
			}
			bool line = second - best < 1.1f;
			unsigned c = line ? 0x555555 : shade (0x8c8c8c, (int) (rnd (T_COBBLE, id, 5) * 30) - 15 - (int) (best * 2));
			put (T_COBBLE, x, y, shade (c, (int) (rnd (T_COBBLE, x, y) * 10) - 5));
		}
	// planches
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			int board = y / 4;
			bool line = y % 4 == 3 || (x == ((board * 7 + 3) & 15) && y % 4 != 3);
			unsigned c = shade (0xb08752, (int) (sinf (x * 0.9f + board * 2.0f) * 6) + (int) (rnd (T_PLANKS, x, y) * 10) - 5);
			put (T_PLANKS, x, y, line ? 0x7a5a32 : c);
		}
	noisy (T_BEDROCK, 0x555555, 40);
	specks (T_BEDROCK, 0x222222, 0.2f, 6);
	// eau (transparente)
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			// (des ondes de période 16 : la tuile se raccorde à elle-même, l'eau la fait glisser)
			const float K = 2.0f * (float) M_PI / 16.0f;
			float w = sinf ((2 * x + y) * K) * 0.35f + sinf ((x - 2 * y) * K + 1.3f) * 0.25f + 0.5f;
			put (T_WATER, x, y, mixc (0x2b5fbf, 0x5a92e6, w * 0.7f + rnd (T_WATER, x, y) * 0.12f), 168);
		}
	noisy (T_SAND, 0xdbcf92, 10);
	specks (T_SAND, 0xc8b87a, 0.12f, 7);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float r = rnd (T_GRAVEL, x / 2, y / 2);
			put (T_GRAVEL, x, y, shade (r < 0.33f ? 0x8a8580 : r < 0.66f ? 0x6f6a66 : 0x9a8f86, (int) (rnd (T_GRAVEL, x, y) * 16) - 8));
		}
	// tronc
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float d = sqrtf ((x - 7.5f) * (x - 7.5f) + (y - 7.5f) * (y - 7.5f));
			bool bark = x == 0 || y == 0 || x == 15 || y == 15;
			put (T_LOG_TOP, x, y, bark ? 0x5e4325 : ((int) d & 1) ? 0xb89262 : 0xa27d50);
			put (T_LOG_SIDE, x, y, shade (0x6a4c2c, ((x * 5 + (int) (rnd (T_LOG_SIDE, x, 0) * 4)) % 4 == 0 ? -20 : 0) + (int) (rnd (T_LOG_SIDE, x, y) * 14) - 7));
			put (T_PINE_LOG_SIDE, x, y, shade (0x4a3520, ((x * 3) % 5 == 0 ? -14 : 0) + (int) (rnd (T_PINE_LOG_SIDE, x, y) * 16) - 8));
		}
	// feuilles (découpées)
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float r = rnd (T_LEAVES, x, y);
			if (r < 0.18f) put (T_LEAVES, x, y, 0, 0);
			else put (T_LEAVES, x, y, shade (0x3f8f2c, (int) (r * 40) - 22));
			float q = rnd (T_PINE_LEAVES, x, y);
			if (q < 0.15f) put (T_PINE_LEAVES, x, y, 0, 0);
			else put (T_PINE_LEAVES, x, y, shade (0x2f5f3a, (int) (q * 34) - 18));
		}
	// verre
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			bool edge = x == 0 || y == 0 || x == 15 || y == 15;
			bool streak = (x - y == 3 || x - y == 4 || x - y == -6) && x > 2 && x < 13;
			put (T_GLASS, x, y, edge ? 0xd8eef5 : 0xffffff, edge ? 255 : streak ? 140 : 0);
		}
	bricks (T_BRICK, 0xa2473a, 0xc9c0b4, 8, 4);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			put (T_SANDSTONE_TOP, x, y, shade (0xd8c98c, (int) (rnd (T_SANDSTONE_TOP, x, y) * 12) - 6));
			int band = y < 3 ? 8 : y > 12 ? -12 : (y % 4 == 0 ? -6 : 0);
			put (T_SANDSTONE_SIDE, x, y, shade (0xd2c283, band + (int) (rnd (T_SANDSTONE_SIDE, x, y) * 10) - 5));
		}
	noisy (T_SNOW, 0xf2f6fa, 6);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			int edge = 3 + (int) (rnd (T_SNOW_SIDE, x, 0, 2) * 3);
			if (y < edge) put (T_SNOW_SIDE, x, y, shade (0xf2f6fa, (int) (rnd (T_SNOW_SIDE, x, y) * 8) - 4));
			else put (T_SNOW_SIDE, x, y, shade (0x8b5d36, (int) (rnd (T_SNOW_SIDE, x, y) * 28) - 14));
			put (T_ICE, x, y, shade (0x9cc3ec, (int) (rnd (T_ICE, x, y) * 10) - 5 + ((x + y) % 7 == 0 ? 18 : 0)));
		}
	ore (T_COAL_ORE, 0x2a2a2a);
	ore (T_IRON_ORE, 0xc9a083);
	ore (T_GOLD_ORE, 0xf2cf3a);
	ore (T_CRYSTAL_ORE, 0x5fe6e0);
	// lampe
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			bool frame = x == 0 || y == 0 || x == 15 || y == 15 || x == 7 || y == 7;
			float d = fabsf (x - 7.5f) + fabsf (y - 7.5f);
			put (T_LAMP, x, y, frame ? 0x8a6a3a : mixc (0xfff2b0, 0xf0b040, d / 12.0f + rnd (T_LAMP, x, y) * 0.15f));
		}
	plant (T_TALL_GRASS, 0x4f9c34, 0, false);
	plant (T_FLOWER_RED, 0x3f8a2c, 0xd23a2e, true);
	plant (T_FLOWER_YELLOW, 0x3f8a2c, 0xf0d23a, true);
	wool (T_WOOL_WHITE, 0xe9ecec);
	wool (T_WOOL_RED, 0xb02e26);
	wool (T_WOOL_ORANGE, 0xf07613);
	wool (T_WOOL_YELLOW, 0xf8c627);
	wool (T_WOOL_GREEN, 0x5e9c18);
	wool (T_WOOL_BLUE, 0x3c44aa);
	wool (T_WOOL_PURPLE, 0x8932b8);
	wool (T_WOOL_BLACK, 0x1d1d21);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			bool edge = x == 0 || y == 0 || x == 15 || y == 15;
			bool shine = (x + y == 6 || x + y == 7) && x > 1 && y > 1;
			put (T_GOLD_BLOCK, x, y, edge ? 0xc99a1e : shine ? 0xfff6b8 : shade (0xf6cf3c, (int) (rnd (T_GOLD_BLOCK, x, y) * 14) - 7));
		}
	bricks (T_STONE_BRICK, 0x7c7c7c, 0x4f4f4f, 8, 8);
	noisy (T_CLAY, 0x9fa4b0, 6);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float d = fabsf (x - 7.5f) + fabsf (y - 7.5f);
			put (T_CACTUS_TOP, x, y, x == 0 || y == 0 || x == 15 || y == 15 ? 0x2f6a1e : d < 3 ? 0x7fb84a : 0x5a9a32);
			bool spine = (x == 3 || x == 12) && (y % 4 == 1);
			put (T_CACTUS_SIDE, x, y, spine ? 0xe8e2b8 : (x % 4 == 0 ? 0x3e7a26 : shade (0x58983a, (int) (rnd (T_CACTUS_SIDE, x, y) * 12) - 6)));
		}
	// bibliothèque : des planches en haut et en bas, des livres entre
	static const unsigned BOOK[] = { 0x8a2a24, 0x2a4a8a, 0x2a7a3a, 0x9a7a2a, 0x5a2a6a, 0x7a4a2a };
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			if (y < 2 || y > 13 || y == 7 || y == 8) put (T_BOOKSHELF, x, y, y == 7 || y == 1 || y == 14 ? 0x7a5a32 : 0xb08752);
			else {
				int bk = (x / 2 + (y > 8 ? 3 : 0)) % 6;
				bool gap = x % 2 == 1 && rnd (T_BOOKSHELF, x, y > 8) < 0.3f;
				put (T_BOOKSHELF, x, y, gap ? 0x3a2a1a : shade (BOOK[bk], (y == 3 || y == 10) ? 30 : 0));
			}
		}
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) put (T_SELECT, x, y, 0xffffff);
	extraTiles (px);
}

} // namespace tc
