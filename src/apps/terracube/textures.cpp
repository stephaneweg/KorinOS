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


// ---- 1.3 : la lave, l'obsidienne, le cuivre et les circuits ----------------------------------------------------------

static void fillTile (int t, unsigned c, int a = 255)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) put (t, x, y, c, a);
}

static void rect (int t, int x0, int y0, int x1, int y1, unsigned c, int a = 255)
{
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
			if (x >= 0 && x < 16 && y >= 0 && y < 16) put (t, x, y, c, a);
}

// de la pierre lisse (le dessous des répéteurs, les boîtiers)
static void smoothStone (int t, int salt)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++)
			put (t, x, y, shade (0xa2a2a2, (int) (rnd (t, x, y, salt) * 12) - 6 + (x == 0 || y == 0 || x == 15 || y == 15 ? -18 : 0)));
}

static void planksInto (int t)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			int board = y / 4;
			bool line = y % 4 == 3 || (x == ((board * 7 + 3) & 15) && y % 4 != 3);
			put (t, x, y, line ? 0x7a5a32 : shade (0xb08752, (int) (sinf (x * 0.9f + board * 2.0f) * 6) + (int) (rnd (t, x, y) * 10) - 5));
		}
}

static void cobbleInto (int t, int salt)
{
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float r = rnd (t, x / 3, y / 3, salt);
			put (t, x, y, shade (r < 0.5f ? 0x7c7c7c : 0x8e8e8e, (int) (rnd (t, x, y, salt + 1) * 18) - 9 + ((x % 5 == 0 || y % 4 == 0) ? -14 : 0)));
		}
}

static void circuitTiles ()
{
	// la lave : des courants orange sous une croûte sombre (période 16 : la tuile se raccorde, le jeu la fait glisser)
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			const float K = 2.0f * (float) M_PI / 16.0f;
			float w = sinf ((x + 2 * y) * K) * 0.4f + sinf ((3 * x - y) * K + 2.1f) * 0.35f + sinf ((x - 3 * y) * K * 2 + 0.7f) * 0.2f;
			float h = 0.5f + 0.5f * w + (rnd (T_LAVA, x, y) - 0.5f) * 0.15f;
			unsigned c = h > 0.72f ? mixc (0xffb43a, 0xfff07a, (h - 0.72f) * 3) : h > 0.38f ? mixc (0xd8400e, 0xffb43a, (h - 0.38f) / 0.34f)
							    : mixc (0x5a1a06, 0xd8400e, h / 0.38f);
			put (T_LAVA, x, y, c);
		}
	// l'obsidienne
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float r = rnd (T_OBSIDIAN, x, y);
			unsigned c = shade (0x1a1024, (int) (r * 16) - 8);
			if (rnd (T_OBSIDIAN, x / 2, y / 2, 3) < 0.18f) c = shade (0x3a2456, (int) (r * 20) - 10);
			if (rnd (T_OBSIDIAN, x, y, 5) < 0.04f) c = 0x6a4a9a;
			put (T_OBSIDIAN, x, y, c);
		}
	// le minerai de cuivre : des amas cuivrés, un peu de vert-de-gris
	ore (T_COPPER_ORE, 0xd8743a);
	for (int k = 0; k < 5; k++) put (T_COPPER_ORE, (int) (rnd (T_COPPER_ORE, k, 7) * 16), (int) (rnd (T_COPPER_ORE, k, 8) * 16), 0x4aa58a);
	// la poudre de cuivre (l'objet) : un petit tas
	clearTile (T_DUST);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float dx = (x - 7.5f) / 6.5f, dy = (y - 10.0f) / 4.0f;
			if (dx * dx + dy * dy < 1.0f && rnd (T_DUST, x, y) < 0.85f) put (T_DUST, x, y, shade (0xe0703a, (int) (rnd (T_DUST, x, y, 2) * 50) - 25 - (y - 8) * 4));
			else if (rnd (T_DUST, x, y, 4) < 0.05f && y > 4) put (T_DUST, x, y, 0xc8602a);
		}
	// le fil : éteint (cuivre sombre), allumé (cuivre chaud)
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float r = rnd (T_WIRE_OFF, x, y);
			put (T_WIRE_OFF, x, y, shade (0x7a3418, (int) (r * 30) - 15));
			put (T_WIRE_ON, x, y, shade (r < 0.2f ? 0xfff0a0 : 0xff8a2a, (int) (rnd (T_WIRE_ON, x, y) * 30) - 15));
		}
	// le bloc de cuivre : des plaques rivetées
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			bool seam = x == 0 || y == 0 || x == 8 || y == 8;
			bool rivet = (x % 8 == 2 || x % 8 == 6) && (y % 8 == 2 || y % 8 == 6);
			unsigned c = seam ? 0x8a4a22 : rivet ? 0xf0a878 : shade (0xc8713c, (int) (rnd (T_COPPER_BLOCK, x, y) * 14) - 7 + ((x + y) % 8 == 3 ? 18 : 0));
			put (T_COPPER_BLOCK, x, y, c);
		}
	// la torche : un manche (colonnes 7 et 8, lignes 6 à 15), une tête de cuivre (lignes 4 à 8)
	for (int k = 0; k < 2; k++) {
		int t = k ? T_TORCH_OFF : T_TORCH_ON;
		clearTile (t);
		for (int y = 6; y < 16; y++) put (t, 7, y, 0x8f6a3e), put (t, 8, y, 0x6a4a28);
		for (int y = 4; y <= 8; y++) put (t, 7, y, k ? 0x7a3418 : 0xffd060), put (t, 8, y, k ? 0x5a2410 : 0xff9a30);
		if (!k)	// (une lueur autour, pour l'icône)
			for (int y = 3; y <= 9; y++) put (t, 6, y, 0xffc050, y == 3 || y == 9 ? 0 : 255), put (t, 9, y, 0xff9a30, y == 3 || y == 9 ? 0 : 255);
	}
	// le levier : un socle de pierre, un manche
	clearTile (T_LEVER);
	rect (T_LEVER, 4, 12, 11, 15, 0x7c7c7c);
	rect (T_LEVER, 4, 12, 11, 12, 0x9a9a9a);
	for (int y = 2; y < 15; y++) put (T_LEVER, 7, y, 0x8f6a3e), put (T_LEVER, 8, y, 0x6a4a28);
	rect (T_LEVER, 7, 2, 8, 3, 0xb0b0b0);
	// les icônes du bouton et de la plaque
	clearTile (T_BUTTON_ICON);
	rect (T_BUTTON_ICON, 4, 6, 11, 10, 0x8a8a8a);
	rect (T_BUTTON_ICON, 4, 6, 11, 6, 0xb0b0b0);
	rect (T_BUTTON_ICON, 4, 10, 11, 10, 0x5a5a5a);
	clearTile (T_PLATE_ICON);
	rect (T_PLATE_ICON, 1, 9, 14, 12, 0x8a8a8a);
	rect (T_PLATE_ICON, 1, 9, 14, 9, 0xb0b0b0);
	rect (T_PLATE_ICON, 1, 12, 14, 12, 0x5a5a5a);
	// répéteur et comparateur : de la pierre lisse, un chemin de cuivre vers le haut (la sortie)
	for (int k = 0; k < 4; k++) {
		int t = k == 0 ? T_REPEATER_OFF : k == 1 ? T_REPEATER_ON : k == 2 ? T_COMPARATOR_OFF : T_COMPARATOR_ON;
		bool on = k & 1;
		smoothStone (t, k);
		unsigned w = on ? 0xff8a2a : 0x7a3418;
		if (k < 2) {
			rect (t, 7, 2, 8, 14, w);
			rect (t, 6, 2, 9, 2, w), rect (t, 5, 3, 10, 3, w);	// (la flèche)
		} else {
			rect (t, 7, 2, 8, 6, w);
			rect (t, 3, 12, 12, 12, w), rect (t, 3, 9, 4, 12, w), rect (t, 11, 9, 12, 12, w);
			rect (t, 6, 6, 9, 7, w);
			for (int i = 0; i < 4; i++) put (t, 4 + i, 8 - i, w), put (t, 11 - i, 8 - i, w);
		}
	}
	for (int k = 0; k < 2; k++) {	// les icônes : la dalle, deux petites torches
		int t = k ? T_COMPARATOR_ICON : T_REPEATER_ICON;
		clearTile (t);
		rect (t, 1, 10, 14, 13, 0xa2a2a2);
		rect (t, 1, 10, 14, 10, 0xc0c0c0);
		int xs[3] = { 3, 11, 7 };
		for (int i = 0; i < (k ? 3 : 2); i++) {
			int x = k ? xs[i] : (i ? 11 : 3);
			rect (t, x, 5, x + 1, 9, 0x8f6a3e);
			rect (t, x, 4, x + 1, 5, 0xff9a30);
		}
		rect (t, 6, 11, 9, 11, 0xd8743a);
	}
	// l'observateur : un visage (deux yeux, une bouche), un dos (son voyant), des côtés fléchés
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			unsigned base = shade (0x5a5a5a, (int) (rnd (T_OBSERVER_FACE, x, y) * 12) - 6 + (x == 0 || y == 0 || x == 15 || y == 15 ? -18 : 0));
			put (T_OBSERVER_FACE, x, y, base);
			put (T_OBSERVER_BACK, x, y, base);
			put (T_OBSERVER_BACK_ON, x, y, base);
			put (T_OBSERVER_SIDE, x, y, shade (base, 10));
		}
	rect (T_OBSERVER_FACE, 3, 4, 6, 6, 0x1a1a1a), rect (T_OBSERVER_FACE, 9, 4, 12, 6, 0x1a1a1a);
	rect (T_OBSERVER_FACE, 4, 5, 5, 5, 0xd8d8d8), rect (T_OBSERVER_FACE, 10, 5, 11, 5, 0xd8d8d8);
	rect (T_OBSERVER_FACE, 3, 10, 12, 11, 0x1a1a1a);
	rect (T_OBSERVER_BACK, 6, 6, 9, 9, 0x3a1208);
	rect (T_OBSERVER_BACK_ON, 6, 6, 9, 9, 0xff5a2a), rect (T_OBSERVER_BACK_ON, 7, 7, 8, 8, 0xffe0a0);
	rect (T_OBSERVER_SIDE, 7, 3, 8, 12, 0x2a2a2a);
	for (int i = 0; i < 3; i++) put (T_OBSERVER_SIDE, 6 - i, 4 + i, 0x2a2a2a), put (T_OBSERVER_SIDE, 9 + i, 4 + i, 0x2a2a2a);
	// le piston : une face de planches (collante : de la colle au milieu), des côtés de pavés cerclés de fer
	planksInto (T_PISTON_FACE);
	rect (T_PISTON_FACE, 6, 6, 9, 9, 0xa8a8b0);
	planksInto (T_STICKY_FACE);
	for (int y = 2; y < 14; y++)
		for (int x = 2; x < 14; x++)
			if ((x - 7.5f) * (x - 7.5f) + (y - 7.5f) * (y - 7.5f) < 30) put (T_STICKY_FACE, x, y, shade (0x7ac04a, (int) (rnd (T_STICKY_FACE, x, y) * 24) - 12));
	cobbleInto (T_PISTON_SIDE, 1);
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 16; x++) put (T_PISTON_SIDE, x, y, y == 3 ? 0x7a5a32 : shade (0xb08752, (int) (rnd (T_PISTON_SIDE, x, y, 9) * 10) - 5));
	rect (T_PISTON_SIDE, 0, 9, 15, 10, 0xa8a8b0);
	cobbleInto (T_PISTON_BACK, 2);
	rect (T_PISTON_BACK, 5, 5, 10, 10, 0xa8a8b0), rect (T_PISTON_BACK, 6, 6, 9, 9, 0x6a6a72);
	cobbleInto (T_PISTON_INNER, 3);
	rect (T_PISTON_INNER, 5, 5, 10, 10, 0x2a2a2a), rect (T_PISTON_INNER, 6, 6, 9, 9, 0xb08752);
	// la lampe à signal : un cadre, des vitres sombres ou chaudes
	for (int k = 0; k < 2; k++) {
		int t = k ? T_LAMP_ON : T_LAMP_OFF;
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				bool frame = x == 0 || y == 0 || x == 15 || y == 15 || ((x == 5 || x == 10 || y == 5 || y == 10) && rnd (t, x, y) < 0.8f);
				float d = fabsf (x - 7.5f) + fabsf (y - 7.5f);
				unsigned glass = k ? mixc (0xfff2b0, 0xf09030, d / 12.0f + rnd (t, x, y, 2) * 0.12f) : mixc (0x6a4a30, 0x3a281c, d / 12.0f + rnd (t, x, y, 2) * 0.15f);
				put (t, x, y, frame ? (k ? 0x8a5a2a : 0x4a3424) : glass);
			}
	}
	// le bloc musical : des planches sombres, une note
	planksInto (T_NOTE);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			const uint8_t *q = g_px + ((((T_NOTE >> 4) * 16 + y) * 256 + (T_NOTE & 15) * 16 + x) * 4);
			put (T_NOTE, x, y, shade ((unsigned) (q[0] << 16 | q[1] << 8 | q[2]), -45));
		}
	rect (T_NOTE, 9, 3, 9, 10, 0x1a1008), rect (T_NOTE, 10, 3, 11, 4, 0x1a1008);
	rect (T_NOTE, 6, 9, 8, 11, 0x1a1008);
	// le capteur de jour : des vitres bleues entre des barres de cuivre
	for (int k = 0; k < 2; k++) {
		int t = k ? T_DAYLIGHT_INV : T_DAYLIGHT;
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				bool bar = x == 0 || y == 0 || x == 15 || y == 15 || x == 5 || x == 10;
				put (t, x, y, bar ? 0xb06a3a : shade (k ? 0x2a3a6a : 0x9ac8ea, (int) (rnd (t, x, y) * 16) - 8 + (y < 4 ? 12 : 0)));
			}
	}
	// le bloc de colle : du vert, un cœur plus sombre
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			bool edge = x == 0 || y == 0 || x == 15 || y == 15;
			bool core = x >= 4 && x <= 11 && y >= 4 && y <= 11;
			put (T_GLUE, x, y, edge ? 0x5aa83a : core ? shade (0x4a9a2a, (int) (rnd (T_GLUE, x, y) * 12) - 6) : shade (0x82d058, (int) (rnd (T_GLUE, x, y, 1) * 10) - 5),
			     edge || core ? 255 : 200);
		}
	// la dynamite : des bâtons rouges liés, leurs bouts dessus et dessous ; allumée : blanchie
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			bool band = y >= 6 && y <= 9;
			unsigned stick = (x % 4 == 0) ? 0x7a1a12 : shade (0xc42a1e, (int) (rnd (T_TNT_SIDE, x, y) * 14) - 7 + (x % 4 == 1 ? 14 : 0));
			put (T_TNT_SIDE, x, y, band ? shade (0xd8c08a, (int) (rnd (T_TNT_SIDE, x, y, 2) * 10) - 5 + (y == 6 || y == 9 ? -20 : 0)) : stick);
			float cx = (x % 5) - 2.0f, cy = (y % 5) - 2.0f;
			bool end = cx * cx + cy * cy < 4.5f;
			put (T_TNT_TOP, x, y, end ? (cx * cx + cy * cy < 1.2f ? 0x3a2a1a : 0xc42a1e) : 0x5a1410);
			put (T_TNT_BOTTOM, x, y, end ? 0xa4241a : 0x4a1410);
			put (T_TNT_LIT, x, y, band ? 0xfff6e8 : shade (0xffe8e0, (int) (rnd (T_TNT_LIT, x, y) * 10) - 5));
		}
	rect (T_TNT_TOP, 7, 7, 8, 8, 0x2a2a2a);
	// le seau : vide, d'eau, de lave ; la résine
	for (int k = 0; k < 3; k++) {
		int t = k == 0 ? T_BUCKET : k == 1 ? T_BUCKET_WATER : T_BUCKET_LAVA;
		clearTile (t);
		for (int y = 4; y < 15; y++) {
			int w = 6 - (y - 4) / 4;
			for (int x = 8 - w; x < 8 + w; x++) put (t, x, y, x == 8 - w || x == 7 + w || y == 14 ? 0x6a6a72 : shade (0xc8c8d0, (x - 8) * -5));
		}
		for (int x = 2; x < 14; x++) put (t, x, 4, 0x8a8a92);
		if (k) rect (t, 3, 5, 12, 6, k == 1 ? 0x3a72d8 : 0xff8a1a), rect (t, 4, 5, 6, 5, k == 1 ? 0x7aa8f0 : 0xffe060);
		for (int i = 0; i < 6; i++) put (t, 3 + i * 2, 2 - (i > 0 && i < 5 ? 1 : 0), 0x6a6a72);	// (l'anse)
	}
	clearTile (T_RESIN);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			float dx = (x - 7.5f) / 4.5f, dy = (y - 9.5f) / 5.0f;
			bool in = dx * dx + dy * dy < 1.0f || (fabsf (x - 7.5f) < (y - 2) * 0.5f && y > 2 && y < 10);
			if (in) put (T_RESIN, x, y, x < 7 && y < 9 ? 0xffe090 : shade (0xe0901a, (int) (rnd (T_RESIN, x, y) * 16) - 8 + (y > 12 ? -20 : 0)));
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
	circuitTiles ();
}

} // namespace tc
