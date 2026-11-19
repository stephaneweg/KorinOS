// world.cpp -- TerraCube : génération du terrain, tronçons, sauvegardes, lancer de rayon (world.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

namespace tc {

int blockByName (const char *s)
{
	if (!s || !*s) return -1;
	char *end;
	long v = strtol (s, &end, 10);
	if (*end == 0) return v >= 0 && v < BLOCK_COUNT ? (int) v : -1;
	for (int i = 0; i < BLOCK_COUNT; i++)
		if (!strcasecmp (s, BLOCKS[i].name)) return i;
	// (quelques autres noms)
	static const struct { const char *a; int b; } AL[] = {
		{ "wood", PLANKS }, { "cobble", COBBLE }, { "wool", WOOL_WHITE }, { "light", LAMP }, { "glowstone", LAMP },
		{ "tree", LOG }, { "rock", STONE }, { "flower", FLOWER_RED }, { "diamond_ore", CRYSTAL_ORE } };
	for (auto &a : AL)
		if (!strcasecmp (s, a.a)) return a.b;
	return -1;
}

void Chunk::topColumn (int x, int z)
{
	int y = CH - 1;
	while (y >= 0 && !isOpaque (at (x, y, z)) && at (x, y, z) != LEAVES && at (x, y, z) != PINE_LEAVES) y--;
	top[z * CW + x] = (int8_t) y;
}

void Chunk::computeTop ()
{
	for (int z = 0; z < CW; z++)
		for (int x = 0; x < CW; x++) topColumn (x, z);
}

World::World () { reset ("monde", 1); }

World::~World ()
{
	for (auto &kv : chunks) delete kv.second;
}

void World::reset (const std::string &nm, uint32_t sd)
{
	for (auto &kv : chunks) freed.push_back (kv.second);
	chunks.clear ();
	name = nm;
	seed = sd;
	dir = "HOME:/terracube/worlds/" + name;
	n1.reseed (sd);
	n2.reseed (sd * 31 + 7);
	n3.reseed (sd * 131 + 17);
	n4.reseed (sd * 977 + 3);
	n5.reseed (sd * 7919 + 11);
	generated = 0;
}

// ---- génération ---------------------------------------------------------------------------------------------------

int World::terrainHeight (int x, int z, int &biome) const
{
	float fx = (float) x, fz = (float) z;
	float cont = n1.fbm2 (fx / 600.0f, fz / 600.0f, 3);			// continents
	float hills = n2.fbm2 (fx / 140.0f, fz / 140.0f, 4);			// collines
	float rid = 1.0f - fabsf (n3.fbm2 (fx / 300.0f, fz / 300.0f, 4));	// crêtes
	float mask = n4.fbm2 (fx / 900.0f + 50.0f, fz / 900.0f - 30.0f, 2);	// où sont les montagnes
	float m = mask > 0.05f ? (mask - 0.05f) * 2.2f : 0.0f;
	if (m > 1) m = 1;
	float h = SEA + 4 + cont * 16.0f + hills * 9.0f + m * rid * rid * 52.0f;
	int hi = (int) h;
	if (hi < 4) hi = 4;
	if (hi > CH - 12) hi = CH - 12;
	float temp = n5.fbm2 (fx / 800.0f, fz / 800.0f, 2);
	float wet = n4.fbm2 (fx / 420.0f + 300.0f, fz / 420.0f, 2);
	if (hi > SEA + 38 || (m > 0.6f && hi > SEA + 26)) biome = B_MOUNTAIN;
	else if (temp < -0.28f) biome = B_SNOW;
	else if (temp > 0.30f && wet < 0.1f) biome = B_DESERT;
	else if (wet > 0.05f) biome = B_FOREST;
	else biome = B_PLAINS;
	return hi;
}

void World::tree (Chunk *c, int x, int y, int z, bool pine)
{
	uint32_t r = hash3 (c->cx * CW + x, y, c->cz * CW + z, seed + 99);
	if (pine) {
		int h = 6 + (int) (r % 4);
		if (y + h + 2 >= CH) return;
		for (int i = 0; i < h; i++) c->put (x, y + i, z, PINE_LOG);
		for (int i = 2; i <= h + 1; i++) {
			int rad = (h + 1 - i) / 2 + (i < h ? 1 : 0);
			if (rad > 2) rad = 2;
			int ly = y + i;
			for (int dz = -rad; dz <= rad; dz++)
				for (int dx = -rad; dx <= rad; dx++) {
					if (abs (dx) + abs (dz) > rad + (rad > 1 ? 0 : 1)) continue;
					int ax = x + dx, az = z + dz;
					if (ax < 0 || ax >= CW || az < 0 || az >= CW) continue;
					if (c->at (ax, ly, az) == AIR) c->put (ax, ly, az, PINE_LEAVES);
				}
		}
		if (c->at (x, y + h + 1, z) == AIR) c->put (x, y + h + 1, z, PINE_LEAVES);
		return;
	}
	int h = 4 + (int) (r % 3);
	if (y + h + 2 >= CH) return;
	for (int i = 0; i < h; i++) c->put (x, y + i, z, LOG);
	for (int ly = y + h - 2; ly <= y + h + 1; ly++) {
		int rad = ly >= y + h ? 1 : 2;
		for (int dz = -rad; dz <= rad; dz++)
			for (int dx = -rad; dx <= rad; dx++) {
				if (rad == 2 && abs (dx) == 2 && abs (dz) == 2 && (hash3 (dx, ly, dz, r) & 1)) continue;
				if (rad == 1 && ly == y + h + 1 && abs (dx) + abs (dz) > 1) continue;
				int ax = x + dx, az = z + dz;
				if (ax < 0 || ax >= CW || az < 0 || az >= CW) continue;
				if (c->at (ax, ly, az) == AIR) c->put (ax, ly, az, LEAVES);
			}
	}
}

void World::generate (Chunk *c)
{
	memset (c->b, AIR, sizeof c->b);
	memset (c->m, 0, sizeof c->m);
	int hs[CW * CW], bio[CW * CW];
	for (int z = 0; z < CW; z++)
		for (int x = 0; x < CW; x++) {
			int wx = c->cx * CW + x, wz = c->cz * CW + z;
			int biome, h = terrainHeight (wx, wz, biome);
			hs[z * CW + x] = h;
			bio[z * CW + x] = biome;
			int beach = h <= SEA + 1 && h >= SEA - 3;
			for (int y = 0; y <= h; y++) {
				int b;
				if (y == 0 || (y < 4 && rand01 (wx, y, wz, seed) < 0.5f - y * 0.12f)) b = BEDROCK;
				else if (y < h - 3) b = STONE;
				else if (biome == B_DESERT) b = y < h - 2 ? SANDSTONE : SAND;
				else if (beach) b = SAND;
				else if (h < SEA - 3) b = y == h ? (rand01 (wx, 7, wz, seed) < 0.3f ? CLAY : GRAVEL) : DIRT;
				else if (biome == B_MOUNTAIN && h > SEA + 30) b = y == h && h > SEA + 46 ? SNOW : STONE;
				else if (y == h) b = biome == B_SNOW ? SNOW : GRASS;
				else b = DIRT;
				c->put (x, y, z, (uint8_t) b);
			}
			for (int y = h + 1; y <= SEA; y++) c->put (x, y, z, y == SEA && biome == B_SNOW ? ICE : WATER);
			// grottes : là où deux bruits 3D passent ensemble près de zéro
			for (int y = 5; y < h + 1 && y < CH - 1; y++) {
				if (h <= SEA + 1 && y > h - 4) break;	// (pas d'ouverture sous l'eau)
				float a = n3.n3 (wx / 28.0f, y / 18.0f, wz / 28.0f);
				float b2 = n5.n3 (wx / 28.0f + 40.0f, y / 18.0f, wz / 28.0f);
				float big = n4.n3 (wx / 60.0f, y / 30.0f, wz / 60.0f);
				if (a * a + b2 * b2 < 0.012f || (big > 0.55f && y < 40)) {
					int was = c->at (x, y, z);
					if (was != BEDROCK && was != WATER) c->put (x, y, z, y < 10 ? LAVA : AIR);	// (des lacs de lave au fond)
				}
			}
			// minerais
			for (int y = 1; y < h - 3; y++) {
				if (c->at (x, y, z) != STONE) continue;
				float r = rand01 (wx, y, wz, seed + 5);
				if (r < 0.010f && y < 110) c->put (x, y, z, COAL_ORE);
				else if (r < 0.016f && y < 64) c->put (x, y, z, IRON_ORE);
				else if (r < 0.0185f && y < 32) c->put (x, y, z, GOLD_ORE);
				else if (r < 0.0197f && y < 16) c->put (x, y, z, CRYSTAL_ORE);
				else if (r < 0.0247f && y < 20) c->put (x, y, z, COPPER_ORE);
			}
		}
	// arbres, plantes, cactus : dans le tronçon seulement (les couronnes ne passent pas les bords)
	for (int z = 0; z < CW; z++)
		for (int x = 0; x < CW; x++) {
			int h = hs[z * CW + x], biome = bio[z * CW + x];
			int wx = c->cx * CW + x, wz = c->cz * CW + z;
			int g = c->at (x, h, z);
			if (h + 1 >= CH - 10) continue;
			if (c->at (x, h + 1, z) != AIR) continue;
			float r = rand01 (wx, 1, wz, seed + 77);
			bool inside = x >= 2 && x < CW - 2 && z >= 2 && z < CW - 2;
			float dens = biome == B_FOREST ? 0.045f : biome == B_SNOW ? 0.02f : biome == B_PLAINS ? 0.006f : 0.0f;
			if ((g == GRASS || g == SNOW) && inside && r < dens) {
				tree (c, x, h + 1, z, biome == B_SNOW || (biome == B_FOREST && rand01 (wx, 2, wz, seed) < 0.25f));
				c->put (x, h, z, DIRT);
			} else if (g == SAND && biome == B_DESERT && r < 0.006f) {
				int ch = 1 + (int) (hash3 (wx, 3, wz, seed) % 3);
				for (int i = 1; i <= ch; i++) c->put (x, h + i, z, CACTUS);
			} else if (g == GRASS) {
				float p = rand01 (wx, 4, wz, seed);
				if (p < 0.12f) c->put (x, h + 1, z, TALL_GRASS);
				else if (p < 0.135f) c->put (x, h + 1, z, FLOWER_RED);
				else if (p < 0.15f) c->put (x, h + 1, z, FLOWER_YELLOW);
			}
		}
	c->computeTop ();
	generated++;
}

Chunk *World::ensure (int cx, int cz)
{
	Chunk *c = find (cx, cz);
	if (c) return c;
	c = new Chunk ();
	c->cx = cx, c->cz = cz;
	if (!loadChunk (c)) generate (c);
	chunks[chunkKey (cx, cz)] = c;
	if (onLoad) onLoad (c);
	// les voisins faits refont leur maillage (leurs bords voient ce tronçon)
	static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	for (auto &d : D)
		if (Chunk *n = find (cx + d[0], cz + d[1])) n->dirty = true;
	return c;
}

int World::get (int x, int y, int z) const
{
	if (y < 0) return BEDROCK;
	if (y >= CH) return AIR;
	const Chunk *c = find (floorDiv (x, CW), floorDiv (z, CW));
	if (!c) return AIR;
	return c->at (x - c->cx * CW, y, z - c->cz * CW);
}

int World::getLoaded (int x, int y, int z)
{
	if (y < 0) return BEDROCK;
	if (y >= CH) return AIR;
	Chunk *c = ensure (floorDiv (x, CW), floorDiv (z, CW));
	return c->at (x - c->cx * CW, y, z - c->cz * CW);
}

void World::markDirty (int x, int z)
{
	int cx = floorDiv (x, CW), cz = floorDiv (z, CW);
	int lx = x - cx * CW, lz = z - cz * CW;
	if (Chunk *c = find (cx, cz)) c->dirty = true;
	if (lx == 0) if (Chunk *c = find (cx - 1, cz)) c->dirty = true;
	if (lx == CW - 1) if (Chunk *c = find (cx + 1, cz)) c->dirty = true;
	if (lz == 0) if (Chunk *c = find (cx, cz - 1)) c->dirty = true;
	if (lz == CW - 1) if (Chunk *c = find (cx, cz + 1)) c->dirty = true;
}

int World::meta (int x, int y, int z) const
{
	if (y < 0 || y >= CH) return 0;
	const Chunk *c = find (floorDiv (x, CW), floorDiv (z, CW));
	if (!c) return 0;
	return c->meta (x - c->cx * CW, y, z - c->cz * CW);
}

bool World::setMeta (int x, int y, int z, int mt)
{
	if (y < 0 || y >= CH) return false;
	Chunk *c = find (floorDiv (x, CW), floorDiv (z, CW));
	if (!c) return false;
	int lx = x - c->cx * CW, lz = z - c->cz * CW;
	if (c->meta (lx, y, lz) == (uint8_t) mt) return true;
	c->putMeta (lx, y, lz, (uint8_t) mt);
	c->modified = true;
	markDirty (x, z);
	int b = c->at (lx, y, lz);
	if (onChange) onChange (x, y, z, b, b);
	return true;
}

bool World::set (int x, int y, int z, int b, int mt)
{
	if (y < 0 || y >= CH || b < 0 || b >= BLOCK_COUNT) return false;
	Chunk *c = ensure (floorDiv (x, CW), floorDiv (z, CW));
	int lx = x - c->cx * CW, lz = z - c->cz * CW;
	int old = c->at (lx, y, lz);
	if (old == b) {
		if (c->meta (lx, y, lz) != (uint8_t) mt) setMeta (x, y, z, mt);
		return true;
	}
	c->put (lx, y, lz, (uint8_t) b);
	c->putMeta (lx, y, lz, (uint8_t) mt);
	c->modified = true;
	int t = c->top[lz * CW + lx];
	if (y >= t) c->topColumn (lx, lz);
	markDirty (x, z);
	// (la lumière du ciel change sous ce bloc : les voisins aussi, pour le lissage)
	if (y >= t || y >= c->top[lz * CW + lx]) {
		markDirty (x - 1, z), markDirty (x + 1, z), markDirty (x, z - 1), markDirty (x, z + 1);
	}
	if (onChange) onChange (x, y, z, old, b);
	return true;
}

int World::height (int x, int z)
{
	Chunk *c = ensure (floorDiv (x, CW), floorDiv (z, CW));
	int lx = x - c->cx * CW, lz = z - c->cz * CW;
	int y = CH - 1;
	while (y > 0 && c->at (lx, y, lz) == AIR) y--;
	return y;
}

bool World::skyLit (int x, int y, int z) const
{
	const Chunk *c = find (floorDiv (x, CW), floorDiv (z, CW));
	if (!c) return true;
	return y > c->top[(z - c->cz * CW) * CW + (x - c->cx * CW)];
}

// ---- sauvegardes : un fichier par tronçon changé, en plages (nombre sur deux octets, bloc, état) ---------------
// (TCC1, avant la 1.3 : sans l'état)

static void mkdirs (const std::string &path)
{
	// HOME:/terracube/worlds/<nom> : chaque niveau
	for (size_t i = 0; i < path.size (); i++)
		if (path[i] == '/' && i > 0 && path[i - 1] != ':') mkdir (path.substr (0, i).c_str (), 0755);
	mkdir (path.c_str (), 0755);
}

bool World::saveChunk (const Chunk *c)
{
	mkdirs (dir);
	char fn[64];
	snprintf (fn, sizeof fn, "/c.%d.%d.chk", c->cx, c->cz);
	FILE *f = fopen ((dir + fn).c_str (), "wb");
	if (!f) return false;
	fwrite ("TCC2", 1, 4, f);
	const int N = CH * CW * CW;
	for (int i = 0; i < N;) {
		int j = i + 1;
		while (j < N && j - i < 65535 && c->b[j] == c->b[i] && c->m[j] == c->m[i]) j++;
		unsigned char rec[4] = { (unsigned char) ((j - i) & 255), (unsigned char) ((j - i) >> 8), c->b[i], c->m[i] };
		fwrite (rec, 1, 4, f);
		i = j;
	}
	bool ok = !ferror (f);
	fclose (f);
	return ok;
}

bool World::loadChunk (Chunk *c)
{
	char fn[64];
	snprintf (fn, sizeof fn, "/c.%d.%d.chk", c->cx, c->cz);
	FILE *f = fopen ((dir + fn).c_str (), "rb");
	if (!f) return false;
	char mg[4];
	bool ok = fread (mg, 1, 4, f) == 4 && (!memcmp (mg, "TCC1", 4) || !memcmp (mg, "TCC2", 4));
	size_t rl = ok && mg[3] == '2' ? 4 : 3;
	const int N = CH * CW * CW;
	int i = 0;
	unsigned char rec[4] = { 0, 0, 0, 0 };
	memset (c->m, 0, sizeof c->m);
	while (ok && i < N && fread (rec, 1, rl, f) == rl) {
		int n = rec[0] | rec[1] << 8;
		if (n <= 0 || i + n > N || rec[2] >= BLOCK_COUNT) {
			ok = false;
			break;
		}
		memset (c->b + i, rec[2], (size_t) n);
		memset (c->m + i, rec[3], (size_t) n);
		i += n;
	}
	fclose (f);
	if (!ok || i != N) return false;
	c->modified = true;	// (il diffère de sa génération : on le garde)
	c->computeTop ();
	return true;
}

bool World::saveAll ()
{
	bool ok = true;
	for (auto &kv : chunks)
		if (kv.second->modified) ok &= saveChunk (kv.second);
	return ok;
}

void World::unload (Chunk *c)
{
	if (c->modified) saveChunk (c);
	chunks.erase (chunkKey (c->cx, c->cz));
	freed.push_back (c);
}

// ---- lancer de rayon (DDA, de case en case) -----------------------------------------------------------------------

Hit World::raycast (const float eye[3], const float d[3], float maxDist, bool liquids) const
{
	Hit h;
	int x = (int) floorf (eye[0]), y = (int) floorf (eye[1]), z = (int) floorf (eye[2]);
	int sx = d[0] > 0 ? 1 : -1, sy = d[1] > 0 ? 1 : -1, sz = d[2] > 0 ? 1 : -1;
	float tdx = d[0] != 0 ? fabsf (1.0f / d[0]) : 1e30f, tdy = d[1] != 0 ? fabsf (1.0f / d[1]) : 1e30f,
	      tdz = d[2] != 0 ? fabsf (1.0f / d[2]) : 1e30f;
	float tx = d[0] != 0 ? ((sx > 0 ? x + 1 - eye[0] : eye[0] - x) * tdx) : 1e30f;
	float ty = d[1] != 0 ? ((sy > 0 ? y + 1 - eye[1] : eye[1] - y) * tdy) : 1e30f;
	float tz = d[2] != 0 ? ((sz > 0 ? z + 1 - eye[2] : eye[2] - z) * tdz) : 1e30f;
	int nx = 0, ny = 0, nz = 0;
	float t = 0;
	while (t <= maxDist) {
		int b = get (x, y, z);
		if (b != AIR && (liquids || !isLiquid (b)) && y >= 0) {
			h.hit = true;
			h.x = x, h.y = y, h.z = z;
			h.nx = x + nx, h.ny = y + ny, h.nz = z + nz;
			h.block = b;
			h.dist = t;
			return h;
		}
		if (tx < ty && tx < tz) {
			x += sx, t = tx, tx += tdx, nx = -sx, ny = nz = 0;
		} else if (ty < tz) {
			y += sy, t = ty, ty += tdy, ny = -sy, nx = nz = 0;
		} else {
			z += sz, t = tz, tz += tdz, nz = -sz, nx = ny = 0;
		}
		if (y < -1 || y > CH) break;
	}
	return h;
}

} // namespace tc
