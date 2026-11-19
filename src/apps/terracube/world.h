// world.h -- TerraCube : le monde en tronçons de 16 × 128 × 16 blocs, sa génération, ses sauvegardes, le lancer de rayon.
//
// Repère : x vers l'est, y vers le haut (0 : le socle, 127 : le ciel), z vers le sud. Un tronçon (cx, cz) couvre
// x de 16·cx à 16·cx + 15, et de même en z. Le monde est sans bord en x et z ; les tronçons sont faits autour du
// joueur (et partout où un script écrit), puis oubliés au loin ; ceux qui ont été changés sont sauvés à part.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "blocks.h"
#include "noise.h"

namespace tc {

const int CW = 16;		// largeur d'un tronçon (x et z)
const int CH = 128;		// hauteur du monde
const int SEA = 48;		// niveau de la mer

inline int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline int64_t chunkKey (int cx, int cz) { return (int64_t) (uint32_t) cx << 32 | (uint32_t) cz; }

// un sommet des maillages : position en 1/16 de bloc, lumière, texel de l'atlas (12 octets)
struct Vertex {
	uint16_t x, y, z;
	uint8_t light, glow;
	uint16_t u, v;
};
static_assert (sizeof (Vertex) == 12, "Vertex");

struct Mesh {
	unsigned vbo = 0;	// tampon GL (render.cpp)
	int quads = 0;
};

struct Chunk {
	int cx, cz;
	uint8_t b[CH * CW * CW];	// (y · 16 + z) · 16 + x
	int8_t top[CW * CW];		// le plus haut bloc opaque de chaque colonne (-1 : aucun)
	bool modified = false;		// changé depuis sa génération : à sauver
	bool dirty = true;		// maillage à refaire
	bool meshed = false;
	Mesh solid, water;
	uint8_t at (int x, int y, int z) const { return b[(y * CW + z) * CW + x]; }
	void put (int x, int y, int z, uint8_t v) { b[(y * CW + z) * CW + x] = v; }
	void computeTop ();
	void topColumn (int x, int z);
};

struct Hit {
	bool hit = false;
	int x, y, z;		// le bloc touché
	int nx, ny, nz;		// la face : où poser un bloc
	int block;
	float dist;
};

class World {
public:
	uint32_t seed = 1;
	std::string name = "monde", dir;	// dir : HOME:/terracube/worlds/<nom>
	std::unordered_map<int64_t, Chunk *> chunks;

	World ();
	~World ();
	void reset (const std::string &name, uint32_t seed);
	// le tronçon, fait ou relu s'il manque
	Chunk *ensure (int cx, int cz);
	Chunk *find (int cx, int cz) const
	{
		auto it = chunks.find (chunkKey (cx, cz));
		return it == chunks.end () ? nullptr : it->second;
	}
	int get (int x, int y, int z) const;	// AIR hors des tronçons faits ; BEDROCK sous 0
	int getLoaded (int x, int y, int z);	// fait le tronçon au besoin
	// pose un bloc (le tronçon est fait au besoin) -> faux hors du monde ou bloc inconnu
	bool set (int x, int y, int z, int b);
	int height (int x, int z);		// y du plus haut bloc non vide (le tronçon est fait au besoin)
	bool skyLit (int x, int y, int z) const;	// au-dessus de tout bloc opaque de sa colonne
	void unload (Chunk *c);			// sauvé s'il a changé, puis oublié
	void markDirty (int x, int z);		// le maillage du tronçon de (x, z), et des voisins au bord
	Hit raycast (const float eye[3], const float dir[3], float maxDist) const;
	// sauvegardes
	bool saveAll ();
	bool saveChunk (const Chunk *c);
	bool loadChunk (Chunk *c);
	std::vector<Chunk *> freed;		// (les maillages à rendre au GL : render.cpp les vide)
	long generated = 0;

private:
	Noise n1, n2, n3, n4, n5;
	void generate (Chunk *c);
	int terrainHeight (int x, int z, int &biome) const;
	void tree (Chunk *c, int x, int y, int z, bool pine);
};

// biomes
enum { B_PLAINS, B_FOREST, B_DESERT, B_SNOW, B_MOUNTAIN };

// le maillage d'un tronçon (ses quatre voisins faits) -> sommets opaques (et découpés), sommets de l'eau
void buildMesh (const World &w, const Chunk &c, std::vector<Vertex> &solid, std::vector<Vertex> &water);

} // namespace tc
