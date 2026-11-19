// blocks.h -- TerraCube : les sortes de blocs, leurs propriétés et leurs tuiles dans l'atlas.
//
// Les numéros sont ceux du protocole de script (api.cpp), de Python (terracube.py), du BASIC et du MCP : on en
// ajoute à la fin, on n'en retire ni n'en déplace jamais (un monde sauvé garde ses numéros).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <stdint.h>

namespace tc {

enum Block : uint8_t {
	AIR = 0,
	STONE,
	GRASS,
	DIRT,
	COBBLE,
	PLANKS,
	BEDROCK,
	WATER,
	SAND,
	GRAVEL,
	LOG,
	LEAVES,
	GLASS,
	BRICK,
	SANDSTONE,
	SNOW,
	ICE,
	COAL_ORE,
	IRON_ORE,
	GOLD_ORE,
	CRYSTAL_ORE,
	LAMP,
	TALL_GRASS,
	FLOWER_RED,
	FLOWER_YELLOW,
	WOOL_WHITE,
	WOOL_RED,
	WOOL_ORANGE,
	WOOL_YELLOW,
	WOOL_GREEN,
	WOOL_BLUE,
	WOOL_PURPLE,
	WOOL_BLACK,
	GOLD_BLOCK,
	STONE_BRICK,
	CLAY,
	PINE_LOG,
	PINE_LEAVES,
	CACTUS,
	BOOKSHELF,
	WORKBENCH,
	FURNACE,
	BLOCK_COUNT
};

// la forme : un cube, une plante (deux quads en croix), un liquide
enum Shape : uint8_t { SH_NONE, SH_CUBE, SH_CROSS, SH_LIQUID };

struct BlockInfo {
	const char *name;	// nom anglais (scripts, MCP)
	const char *label;	// nom montré (français)
	Shape shape;
	bool solid;		// on bute dedans
	bool opaque;		// cache les faces voisines et la lumière du ciel
	bool cutout;		// pixels transparents (feuilles, verre) : test d'alpha
	bool glow;		// rend sa propre lumière
	uint8_t top, side, bottom;	// tuiles de l'atlas (16 × 16 tuiles de 16 px)
};

// tuiles de l'atlas (textures.cpp les dessine)
enum Tile : uint8_t {
	T_STONE, T_GRASS_TOP, T_GRASS_SIDE, T_DIRT, T_COBBLE, T_PLANKS, T_BEDROCK, T_WATER,
	T_SAND, T_GRAVEL, T_LOG_TOP, T_LOG_SIDE, T_LEAVES, T_GLASS, T_BRICK, T_SANDSTONE_TOP,
	T_SANDSTONE_SIDE, T_SNOW, T_SNOW_SIDE, T_ICE, T_COAL_ORE, T_IRON_ORE, T_GOLD_ORE, T_CRYSTAL_ORE,
	T_LAMP, T_TALL_GRASS, T_FLOWER_RED, T_FLOWER_YELLOW, T_WOOL_WHITE, T_WOOL_RED, T_WOOL_ORANGE, T_WOOL_YELLOW,
	T_WOOL_GREEN, T_WOOL_BLUE, T_WOOL_PURPLE, T_WOOL_BLACK, T_GOLD_BLOCK, T_STONE_BRICK, T_CLAY, T_PINE_LOG_SIDE,
	T_PINE_LEAVES, T_CACTUS_TOP, T_CACTUS_SIDE, T_BOOKSHELF, T_SELECT,
	T_WORKBENCH_TOP, T_WORKBENCH_SIDE, T_FURNACE_TOP, T_FURNACE_SIDE,
	T_STICK, T_COAL, T_IRON_INGOT, T_GOLD_INGOT, T_CRYSTAL,
	T_PICK0, T_PICK1, T_PICK2, T_PICK3, T_AXE0, T_AXE1, T_AXE2, T_AXE3, T_SHOVEL0, T_SHOVEL1, T_SHOVEL2, T_SHOVEL3,
	T_DIGIT0, T_TILE_COUNT = T_DIGIT0 + 10
};

static const BlockInfo BLOCKS[BLOCK_COUNT] = {
	{ "air", "Air", SH_NONE, false, false, false, false, 0, 0, 0 },
	{ "stone", "Pierre", SH_CUBE, true, true, false, false, T_STONE, T_STONE, T_STONE },
	{ "grass", "Herbe", SH_CUBE, true, true, false, false, T_GRASS_TOP, T_GRASS_SIDE, T_DIRT },
	{ "dirt", "Terre", SH_CUBE, true, true, false, false, T_DIRT, T_DIRT, T_DIRT },
	{ "cobblestone", "Pavés", SH_CUBE, true, true, false, false, T_COBBLE, T_COBBLE, T_COBBLE },
	{ "planks", "Planches", SH_CUBE, true, true, false, false, T_PLANKS, T_PLANKS, T_PLANKS },
	{ "bedrock", "Socle", SH_CUBE, true, true, false, false, T_BEDROCK, T_BEDROCK, T_BEDROCK },
	{ "water", "Eau", SH_LIQUID, false, false, false, false, T_WATER, T_WATER, T_WATER },
	{ "sand", "Sable", SH_CUBE, true, true, false, false, T_SAND, T_SAND, T_SAND },
	{ "gravel", "Gravier", SH_CUBE, true, true, false, false, T_GRAVEL, T_GRAVEL, T_GRAVEL },
	{ "log", "Tronc", SH_CUBE, true, true, false, false, T_LOG_TOP, T_LOG_SIDE, T_LOG_TOP },
	{ "leaves", "Feuilles", SH_CUBE, true, false, true, false, T_LEAVES, T_LEAVES, T_LEAVES },
	{ "glass", "Verre", SH_CUBE, true, false, true, false, T_GLASS, T_GLASS, T_GLASS },
	{ "brick", "Briques", SH_CUBE, true, true, false, false, T_BRICK, T_BRICK, T_BRICK },
	{ "sandstone", "Grès", SH_CUBE, true, true, false, false, T_SANDSTONE_TOP, T_SANDSTONE_SIDE, T_SANDSTONE_TOP },
	{ "snow", "Neige", SH_CUBE, true, true, false, false, T_SNOW, T_SNOW_SIDE, T_DIRT },
	{ "ice", "Glace", SH_CUBE, true, true, false, false, T_ICE, T_ICE, T_ICE },
	{ "coal_ore", "Charbon", SH_CUBE, true, true, false, false, T_COAL_ORE, T_COAL_ORE, T_COAL_ORE },
	{ "iron_ore", "Fer", SH_CUBE, true, true, false, false, T_IRON_ORE, T_IRON_ORE, T_IRON_ORE },
	{ "gold_ore", "Or", SH_CUBE, true, true, false, false, T_GOLD_ORE, T_GOLD_ORE, T_GOLD_ORE },
	{ "crystal_ore", "Cristal", SH_CUBE, true, true, false, true, T_CRYSTAL_ORE, T_CRYSTAL_ORE, T_CRYSTAL_ORE },
	{ "lamp", "Lampe", SH_CUBE, true, true, false, true, T_LAMP, T_LAMP, T_LAMP },
	{ "tall_grass", "Hautes herbes", SH_CROSS, false, false, true, false, T_TALL_GRASS, T_TALL_GRASS, T_TALL_GRASS },
	{ "flower_red", "Fleur rouge", SH_CROSS, false, false, true, false, T_FLOWER_RED, T_FLOWER_RED, T_FLOWER_RED },
	{ "flower_yellow", "Fleur jaune", SH_CROSS, false, false, true, false, T_FLOWER_YELLOW, T_FLOWER_YELLOW, T_FLOWER_YELLOW },
	{ "wool_white", "Laine blanche", SH_CUBE, true, true, false, false, T_WOOL_WHITE, T_WOOL_WHITE, T_WOOL_WHITE },
	{ "wool_red", "Laine rouge", SH_CUBE, true, true, false, false, T_WOOL_RED, T_WOOL_RED, T_WOOL_RED },
	{ "wool_orange", "Laine orange", SH_CUBE, true, true, false, false, T_WOOL_ORANGE, T_WOOL_ORANGE, T_WOOL_ORANGE },
	{ "wool_yellow", "Laine jaune", SH_CUBE, true, true, false, false, T_WOOL_YELLOW, T_WOOL_YELLOW, T_WOOL_YELLOW },
	{ "wool_green", "Laine verte", SH_CUBE, true, true, false, false, T_WOOL_GREEN, T_WOOL_GREEN, T_WOOL_GREEN },
	{ "wool_blue", "Laine bleue", SH_CUBE, true, true, false, false, T_WOOL_BLUE, T_WOOL_BLUE, T_WOOL_BLUE },
	{ "wool_purple", "Laine violette", SH_CUBE, true, true, false, false, T_WOOL_PURPLE, T_WOOL_PURPLE, T_WOOL_PURPLE },
	{ "wool_black", "Laine noire", SH_CUBE, true, true, false, false, T_WOOL_BLACK, T_WOOL_BLACK, T_WOOL_BLACK },
	{ "gold_block", "Bloc d'or", SH_CUBE, true, true, false, false, T_GOLD_BLOCK, T_GOLD_BLOCK, T_GOLD_BLOCK },
	{ "stone_brick", "Pierre taillée", SH_CUBE, true, true, false, false, T_STONE_BRICK, T_STONE_BRICK, T_STONE_BRICK },
	{ "clay", "Argile", SH_CUBE, true, true, false, false, T_CLAY, T_CLAY, T_CLAY },
	{ "pine_log", "Tronc de pin", SH_CUBE, true, true, false, false, T_LOG_TOP, T_PINE_LOG_SIDE, T_LOG_TOP },
	{ "pine_leaves", "Aiguilles", SH_CUBE, true, false, true, false, T_PINE_LEAVES, T_PINE_LEAVES, T_PINE_LEAVES },
	{ "cactus", "Cactus", SH_CUBE, true, true, false, false, T_CACTUS_TOP, T_CACTUS_SIDE, T_CACTUS_TOP },
	{ "bookshelf", "Bibliothèque", SH_CUBE, true, true, false, false, T_PLANKS, T_BOOKSHELF, T_PLANKS },
	{ "workbench", "Établi", SH_CUBE, true, true, false, false, T_WORKBENCH_TOP, T_WORKBENCH_SIDE, T_PLANKS },
	{ "furnace", "Four", SH_CUBE, true, true, false, false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP },
};

inline const BlockInfo &info (int b) { return BLOCKS[(unsigned) b < BLOCK_COUNT ? b : 0]; }
inline bool isOpaque (int b) { return info (b).opaque; }
inline bool isSolid (int b) { return info (b).solid; }

// le numéro d'un nom anglais ("stone"), d'un numéro écrit ("4") -> -1 si inconnu
int blockByName (const char *s);

} // namespace tc
