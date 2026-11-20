// items.cpp -- TerraCube : objets, inventaire, minage et recettes (items.h).
//
// Les recettes sont celles de TerraCube : une liste d'ingrédients (sans grille), faite à la main, à l'établi ou au
// four (proches du joueur : 4 blocs). Le four brûle un combustible : chaque objet cuit coûte un point de chauffe,
// pris au charbon (8), au tronc (4), aux planches (2) ou au bâton (1).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "items.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>

namespace tc {

namespace {

const ItemInfo ITEMS[I_END - 100] = {
	{ "stick", "Bâton", T_STICK, 64, TOOL_NONE, 0, 0 },
	{ "coal", "Charbon", T_COAL, 64, TOOL_NONE, 0, 0 },
	{ "iron_ingot", "Lingot de fer", T_IRON_INGOT, 64, TOOL_NONE, 0, 0 },
	{ "gold_ingot", "Lingot d'or", T_GOLD_INGOT, 64, TOOL_NONE, 0, 0 },
	{ "crystal", "Cristal", T_CRYSTAL, 64, TOOL_NONE, 0, 0 },
	{ "wood_pickaxe", "Pioche en bois", T_PICK0, 1, TOOL_PICKAXE, 1, 60 },
	{ "stone_pickaxe", "Pioche en pierre", T_PICK1, 1, TOOL_PICKAXE, 2, 132 },
	{ "iron_pickaxe", "Pioche en fer", T_PICK2, 1, TOOL_PICKAXE, 3, 251 },
	{ "crystal_pickaxe", "Pioche en cristal", T_PICK3, 1, TOOL_PICKAXE, 4, 1562 },
	{ "wood_axe", "Hache en bois", T_AXE0, 1, TOOL_AXE, 1, 60 },
	{ "stone_axe", "Hache en pierre", T_AXE1, 1, TOOL_AXE, 2, 132 },
	{ "iron_axe", "Hache en fer", T_AXE2, 1, TOOL_AXE, 3, 251 },
	{ "crystal_axe", "Hache en cristal", T_AXE3, 1, TOOL_AXE, 4, 1562 },
	{ "wood_shovel", "Pelle en bois", T_SHOVEL0, 1, TOOL_SHOVEL, 1, 60 },
	{ "stone_shovel", "Pelle en pierre", T_SHOVEL1, 1, TOOL_SHOVEL, 2, 132 },
	{ "iron_shovel", "Pelle en fer", T_SHOVEL2, 1, TOOL_SHOVEL, 3, 251 },
	{ "crystal_shovel", "Pelle en cristal", T_SHOVEL3, 1, TOOL_SHOVEL, 4, 1562 },
	{ "bucket", "Seau", T_BUCKET, 16, TOOL_NONE, 0, 0 },
	{ "water_bucket", "Seau d'eau", T_BUCKET_WATER, 1, TOOL_NONE, 0, 0 },
	{ "lava_bucket", "Seau de lave", T_BUCKET_LAVA, 1, TOOL_NONE, 0, 0 },
	{ "resin", "Résine", T_RESIN, 64, TOOL_NONE, 0, 0 },
	{ "seeds", "Graines", T_SEEDS, 64, TOOL_NONE, 0, 0 },
	{ "apple", "Pomme", T_APPLE, 64, TOOL_NONE, 0, 0 },
	{ "raw_chicken", "Poulet cru", T_RAW_CHICKEN, 64, TOOL_NONE, 0, 0 },
	{ "cooked_chicken", "Poulet rôti", T_COOKED_CHICKEN, 64, TOOL_NONE, 0, 0 },
	{ "raw_pork", "Porc cru", T_RAW_PORK, 64, TOOL_NONE, 0, 0 },
	{ "cooked_pork", "Côtelette grillée", T_COOKED_PORK, 64, TOOL_NONE, 0, 0 },
	{ "feather", "Plume", T_FEATHER, 64, TOOL_NONE, 0, 0 },
	{ "bone", "Os", T_BONE, 64, TOOL_NONE, 0, 0 },
	{ "string", "Fil d'araignée", T_STRING, 64, TOOL_NONE, 0, 0 },
	{ "wood_sword", "Épée en bois", T_SWORD0, 1, TOOL_SWORD, 1, 60 },
	{ "stone_sword", "Épée en pierre", T_SWORD1, 1, TOOL_SWORD, 2, 132 },
	{ "iron_sword", "Épée en fer", T_SWORD2, 1, TOOL_SWORD, 3, 251 },
	{ "crystal_sword", "Épée en cristal", T_SWORD3, 1, TOOL_SWORD, 4, 1562 },
};

const float TIER_SPEED[5] = { 1, 2, 4, 6, 9 };

} // namespace

bool itemValid (int id) { return (id > 0 && id < BLOCK_COUNT) || (id >= 100 && id < I_END); }
bool isBlockItem (int id) { return id > 0 && id < BLOCK_COUNT; }

ItemInfo itemInfo (int id)
{
	if (id >= 100 && id < I_END) return ITEMS[id - 100];
	const BlockInfo &b = info (id);
	return { b.name, b.label, b.side, 64, TOOL_NONE, 0, 0 };
}

int itemByName (const char *s)
{
	if (!s || !*s) return -1;
	char *end;
	long v = strtol (s, &end, 10);
	if (*end == 0) return itemValid ((int) v) ? (int) v : -1;
	for (int i = 100; i < I_END; i++)
		if (!strcasecmp (s, ITEMS[i - 100].name) || !strcasecmp (s, ITEMS[i - 100].label)) return i;
	int b = blockByName (s);
	if (b > 0) return b;
	for (int i = 1; i < BLOCK_COUNT; i++)
		if (!strcasecmp (s, BLOCKS[i].label)) return i;
	return -1;
}

std::vector<int> allItems ()
{
	std::vector<int> v;
	for (int i = 1; i < BLOCK_COUNT; i++)
		if (i != BEDROCK && i != WATER && i != LAVA && i != PISTON_HEAD) v.push_back (i);
	for (int i = 100; i < I_END; i++) v.push_back (i);
	return v;
}

// ---- l'inventaire -------------------------------------------------------------------------------------------------

int Inventory::add (int item, int n)
{
	if (!itemValid (item) || n <= 0) return n;
	int mx = itemInfo (item).maxStack;
	for (auto &st : s)	// (d'abord sur les piles qui en ont déjà)
		if (n > 0 && !st.empty () && st.item == item && st.count < mx) {
			int k = std::min (n, mx - st.count);
			st.count += k, n -= k;
		}
	for (auto &st : s)
		if (n > 0 && st.empty ()) {
			int k = std::min (n, mx);
			st.item = item, st.count = k, st.dur = 0, n -= k;
		}
	return n;
}

int Inventory::count (int item) const
{
	int n = 0;
	for (auto &st : s)
		if (!st.empty () && st.item == item) n += st.count;
	return n;
}

bool Inventory::take (int item, int n)
{
	if (count (item) < n) return false;
	for (int i = N - 1; i >= 0 && n > 0; i--) {	// (le sac d'abord, la barre ensuite)
		Stack &st = s[i];
		if (st.empty () || st.item != item) continue;
		int k = std::min (n, st.count);
		st.count -= k, n -= k;
		if (!st.count) st = Stack ();
	}
	return true;
}

void Inventory::clear ()
{
	for (auto &st : s) st = Stack ();
}

std::string Inventory::save () const
{
	std::string t;
	for (int i = 0; i < N; i++) {
		if (i) t += ',';
		if (!s[i].empty ()) t += std::to_string (s[i].item) + ":" + std::to_string (s[i].count) + ":" + std::to_string (s[i].dur);
	}
	return t;
}

void Inventory::load (const std::string &t)
{
	clear ();
	size_t p = 0;
	for (int i = 0; i < N && p <= t.size (); i++) {
		size_t e = t.find (',', p);
		std::string f = t.substr (p, e == std::string::npos ? std::string::npos : e - p);
		int it = 0, n = 0, d = 0;
		if (sscanf (f.c_str (), "%d:%d:%d", &it, &n, &d) >= 2 && itemValid (it) && n > 0)
			s[i].item = it, s[i].count = std::min (n, itemInfo (it).maxStack), s[i].dur = d;
		if (e == std::string::npos) break;
		p = e + 1;
	}
}

// ---- le minage ----------------------------------------------------------------------------------------------------

Mining mining (int b)
{
	switch (b) {
	case AIR: case WATER: case LAVA: case BEDROCK: return { -1, TOOL_NONE, 0, 0, 0, 0 };
	case OBSIDIAN: return { 80.0f, TOOL_PICKAXE, 4, OBSIDIAN, 1, 1 };
	case COPPER_ORE: return { 3.0f, TOOL_PICKAXE, 3, WIRE, 4, 1 };
	case COPPER_BLOCK: return { 5.0f, TOOL_PICKAXE, 1, COPPER_BLOCK, 1, 1 };
	case WIRE: case COPPER_TORCH: case REPEATER: case COMPARATOR: case TNT: case GLUE_BLOCK: return { 0.05f, TOOL_NONE, 0, b, 1, 1 };
	case LEVER: case STONE_BUTTON: case WOOD_BUTTON: case STONE_PLATE: case WOOD_PLATE: return { 0.5f, TOOL_NONE, 0, b, 1, 1 };
	case OBSERVER: return { 3.0f, TOOL_PICKAXE, 1, OBSERVER, 1, 1 };
	case PISTON: case STICKY_PISTON: return { 0.5f, TOOL_NONE, 0, b, 1, 1 };
	case PISTON_HEAD: return { 0.5f, TOOL_NONE, 0, 0, 0, 0 };	// (main.cpp : le piston tombe)
	case SIGNAL_LAMP: return { 0.3f, TOOL_NONE, 0, SIGNAL_LAMP, 1, 1 };
	case NOTE_BLOCK: return { 0.8f, TOOL_AXE, 0, NOTE_BLOCK, 1, 1 };
	case DAYLIGHT_SENSOR: return { 0.2f, TOOL_AXE, 0, DAYLIGHT_SENSOR, 1, 1 };
	case STONE: return { 1.5f, TOOL_PICKAXE, 1, COBBLE, 1, 1 };
	case GRASS: return { 0.6f, TOOL_SHOVEL, 0, DIRT, 1, 1 };
	case DIRT: case CLAY: return { 0.5f, TOOL_SHOVEL, 0, b, 1, 1 };
	case SAND: return { 0.5f, TOOL_SHOVEL, 0, SAND, 1, 1 };
	case GRAVEL: return { 0.6f, TOOL_SHOVEL, 0, GRAVEL, 1, 1 };
	case SNOW: return { 0.2f, TOOL_SHOVEL, 0, SNOW, 1, 1 };
	case COBBLE: return { 2.0f, TOOL_PICKAXE, 1, COBBLE, 1, 1 };
	case PLANKS: case WORKBENCH: return { 2.0f, TOOL_AXE, 0, b, 1, 1 };
	case LOG: case PINE_LOG: return { 2.0f, TOOL_AXE, 0, b, 1, 1 };
	case BOOKSHELF: return { 1.5f, TOOL_AXE, 0, PLANKS, 3, 1 };
	case LEAVES: case PINE_LEAVES: return { 0.2f, TOOL_NONE, 0, I_STICK, 1, 0.12f };
	case GLASS: return { 0.3f, TOOL_NONE, 0, 0, 0, 0 };
	case ICE: return { 0.5f, TOOL_PICKAXE, 0, 0, 0, 0 };
	case BRICK: case STONE_BRICK: return { 2.0f, TOOL_PICKAXE, 1, b, 1, 1 };
	case SANDSTONE: return { 0.8f, TOOL_PICKAXE, 1, SANDSTONE, 1, 1 };
	case COAL_ORE: return { 3.0f, TOOL_PICKAXE, 1, I_COAL, 1, 1 };
	case IRON_ORE: return { 3.0f, TOOL_PICKAXE, 2, IRON_ORE, 1, 1 };
	case GOLD_ORE: return { 3.0f, TOOL_PICKAXE, 3, GOLD_ORE, 1, 1 };
	case CRYSTAL_ORE: return { 3.0f, TOOL_PICKAXE, 3, I_CRYSTAL, 1, 1 };
	case GOLD_BLOCK: return { 3.0f, TOOL_PICKAXE, 3, GOLD_BLOCK, 1, 1 };
	case FURNACE: return { 3.5f, TOOL_PICKAXE, 1, FURNACE, 1, 1 };
	case LAMP: return { 0.3f, TOOL_NONE, 0, LAMP, 1, 1 };
	case TALL_GRASS: return { 0.05f, TOOL_NONE, 0, TALL_GRASS, 1, 0.5f };
	case FLOWER_RED: case FLOWER_YELLOW: return { 0.05f, TOOL_NONE, 0, b, 1, 1 };
	case CACTUS: return { 0.4f, TOOL_NONE, 0, CACTUS, 1, 1 };
	default:	// (les laines)
		return { 0.8f, TOOL_NONE, 0, b, 1, 1 };
	}
}

float mineTime (int block, int held, bool *harvest)
{
	Mining m = mining (block);
	if (m.hardness < 0) {
		if (harvest) *harvest = false;
		return -1;
	}
	ItemInfo t = itemInfo (held);
	bool can = m.tier == 0 || (t.tool == TOOL_PICKAXE && t.tier >= m.tier);
	if (harvest) *harvest = can;
	float speed = (t.tool != TOOL_NONE && t.tool == m.tool) ? TIER_SPEED[std::min (std::max (t.tier, 0), 4)] : 1.0f;
	return m.hardness * (can ? 1.0f : 3.3f) / speed;
}

// ---- la fabrication -----------------------------------------------------------------------------------------------

namespace {

std::vector<Recipe> buildRecipes ()
{
	const int M_WOOD = PLANKS, M_STONE = COBBLE, M_IRON = I_IRON_INGOT, M_CRYSTAL = I_CRYSTAL;
	std::vector<Recipe> R = {
		{ "planks", PLANKS, 4, "L", { { 'L', LOG } }, 0, true },
		{ "planks_from_pine", PLANKS, 4, "L", { { 'L', PINE_LOG } }, 0, true },
		{ "stick", I_STICK, 4, "P/P", { { 'P', PLANKS } }, 0, false },
		{ "workbench", WORKBENCH, 1, "PP/PP", { { 'P', PLANKS } }, 0, false },
		{ "wood_pickaxe", I_WOOD_PICKAXE, 1, "MMM/.S./.S.", { { 'M', M_WOOD }, { 'S', I_STICK } }, 0, false },
		{ "wood_axe", I_WOOD_AXE, 1, "MM/MS/.S", { { 'M', M_WOOD }, { 'S', I_STICK } }, 0, false },
		{ "wood_shovel", I_WOOD_SHOVEL, 1, "M/S/S", { { 'M', M_WOOD }, { 'S', I_STICK } }, 0, false },
		{ "stone_pickaxe", I_STONE_PICKAXE, 1, "MMM/.S./.S.", { { 'M', M_STONE }, { 'S', I_STICK } }, 0, false },
		{ "stone_axe", I_STONE_AXE, 1, "MM/MS/.S", { { 'M', M_STONE }, { 'S', I_STICK } }, 0, false },
		{ "stone_shovel", I_STONE_SHOVEL, 1, "M/S/S", { { 'M', M_STONE }, { 'S', I_STICK } }, 0, false },
		{ "iron_pickaxe", I_IRON_PICKAXE, 1, "MMM/.S./.S.", { { 'M', M_IRON }, { 'S', I_STICK } }, 0, false },
		{ "iron_axe", I_IRON_AXE, 1, "MM/MS/.S", { { 'M', M_IRON }, { 'S', I_STICK } }, 0, false },
		{ "iron_shovel", I_IRON_SHOVEL, 1, "M/S/S", { { 'M', M_IRON }, { 'S', I_STICK } }, 0, false },
		{ "crystal_pickaxe", I_CRYSTAL_PICKAXE, 1, "MMM/.S./.S.", { { 'M', M_CRYSTAL }, { 'S', I_STICK } }, 0, false },
		{ "crystal_axe", I_CRYSTAL_AXE, 1, "MM/MS/.S", { { 'M', M_CRYSTAL }, { 'S', I_STICK } }, 0, false },
		{ "crystal_shovel", I_CRYSTAL_SHOVEL, 1, "M/S/S", { { 'M', M_CRYSTAL }, { 'S', I_STICK } }, 0, false },
		{ "furnace", FURNACE, 1, "CCC/C.C/CCC", { { 'C', COBBLE } }, 0, false },
		{ "lamp", LAMP, 2, ".G./GKG/.I.", { { 'G', GLASS }, { 'K', I_COAL }, { 'I', I_IRON_INGOT } }, 0, false },
		{ "stone_brick", STONE_BRICK, 4, "SS/SS", { { 'S', STONE } }, 0, false },
		{ "sandstone", SANDSTONE, 2, "AA/AA", { { 'A', SAND } }, 0, false },
		{ "bookshelf", BOOKSHELF, 1, "PPP/KKK/PPP", { { 'P', PLANKS }, { 'K', I_COAL } }, 0, false },
		{ "gold_block", GOLD_BLOCK, 1, "GGG/GGG/GGG", { { 'G', I_GOLD_INGOT } }, 0, false },
		{ "wool_white", WOOL_WHITE, 1, "TT/TT", { { 'T', TALL_GRASS } }, 0, false },
		{ "wool_red", WOOL_RED, 1, "WF", { { 'W', WOOL_WHITE }, { 'F', FLOWER_RED } }, 0, true },
		{ "wool_yellow", WOOL_YELLOW, 1, "WF", { { 'W', WOOL_WHITE }, { 'F', FLOWER_YELLOW } }, 0, true },
		{ "wool_orange", WOOL_ORANGE, 2, "RY", { { 'R', WOOL_RED }, { 'Y', WOOL_YELLOW } }, 0, true },
		{ "wool_green", WOOL_GREEN, 1, "WLL", { { 'W', WOOL_WHITE }, { 'L', LEAVES } }, 0, true },
		{ "wool_blue", WOOL_BLUE, 4, "WWWWC", { { 'W', WOOL_WHITE }, { 'C', I_CRYSTAL } }, 0, true },
		{ "wool_purple", WOOL_PURPLE, 2, "RB", { { 'R', WOOL_RED }, { 'B', WOOL_BLUE } }, 0, true },
		{ "wool_black", WOOL_BLACK, 1, "WK", { { 'W', WOOL_WHITE }, { 'K', I_COAL } }, 0, true },
		{ "iron_ingot", I_IRON_INGOT, 1, "X", { { 'X', IRON_ORE } }, ST_FURNACE, true },
		{ "gold_ingot", I_GOLD_INGOT, 1, "X", { { 'X', GOLD_ORE } }, ST_FURNACE, true },
		{ "glass", GLASS, 1, "X", { { 'X', SAND } }, ST_FURNACE, true },
		{ "stone", STONE, 1, "X", { { 'X', COBBLE } }, ST_FURNACE, true },
		{ "brick", BRICK, 1, "X", { { 'X', CLAY } }, ST_FURNACE, true },
		{ "charcoal", I_COAL, 1, "X", { { 'X', LOG } }, ST_FURNACE, true },
		{ "charcoal_from_pine", I_COAL, 1, "X", { { 'X', PINE_LOG } }, ST_FURNACE, true },
		// 1.3 : les seaux, la résine, les circuits
		{ "bucket", I_BUCKET, 1, "I.I/.I.", { { 'I', I_IRON_INGOT } }, 0, false },
		{ "resin", I_RESIN, 1, "X", { { 'X', CACTUS } }, ST_FURNACE, true },
		{ "resin_from_needles", I_RESIN, 1, "LLLL", { { 'L', PINE_LEAVES } }, 0, true },
		{ "copper_block", COPPER_BLOCK, 1, "DDD/DDD/DDD", { { 'D', WIRE } }, 0, false },
		{ "copper_dust", WIRE, 9, "B", { { 'B', COPPER_BLOCK } }, 0, true },
		{ "copper_torch", COPPER_TORCH, 1, "D/S", { { 'D', WIRE }, { 'S', I_STICK } }, 0, false },
		{ "lever", LEVER, 1, "S/C", { { 'S', I_STICK }, { 'C', COBBLE } }, 0, false },
		{ "stone_button", STONE_BUTTON, 1, "S", { { 'S', STONE } }, 0, true },
		{ "wood_button", WOOD_BUTTON, 1, "P", { { 'P', PLANKS } }, 0, true },
		{ "stone_plate", STONE_PLATE, 1, "SS", { { 'S', STONE } }, 0, false },
		{ "wood_plate", WOOD_PLATE, 1, "PP", { { 'P', PLANKS } }, 0, false },
		{ "repeater", REPEATER, 1, "TDT/SSS", { { 'T', COPPER_TORCH }, { 'D', WIRE }, { 'S', STONE } }, 0, false },
		{ "comparator", COMPARATOR, 1, ".T./TKT/SSS", { { 'T', COPPER_TORCH }, { 'K', I_CRYSTAL }, { 'S', STONE } }, 0, false },
		{ "observer", OBSERVER, 1, "CCC/DDK/CCC", { { 'C', COBBLE }, { 'D', WIRE }, { 'K', I_CRYSTAL } }, 0, false },
		{ "piston", PISTON, 1, "PPP/CIC/CDC", { { 'P', PLANKS }, { 'C', COBBLE }, { 'I', I_IRON_INGOT }, { 'D', WIRE } }, 0, false },
		{ "sticky_piston", STICKY_PISTON, 1, "R/P", { { 'R', I_RESIN }, { 'P', PISTON } }, 0, false },
		{ "signal_lamp", SIGNAL_LAMP, 1, ".D./DLD/.D.", { { 'D', WIRE }, { 'L', LAMP } }, 0, false },
		{ "note_block", NOTE_BLOCK, 1, "PPP/PDP/PPP", { { 'P', PLANKS }, { 'D', WIRE } }, 0, false },
		{ "daylight_sensor", DAYLIGHT_SENSOR, 1, "GGG/KKK/PPP", { { 'G', GLASS }, { 'K', I_CRYSTAL }, { 'P', PLANKS } }, 0, false },
		{ "glue_block", GLUE_BLOCK, 1, "RRR/RRR/RRR", { { 'R', I_RESIN } }, 0, false },
		{ "resin_from_glue", I_RESIN, 9, "G", { { 'G', GLUE_BLOCK } }, 0, true },
		{ "tnt", TNT, 1, "CSC/SCS/CSC", { { 'C', I_COAL }, { 'S', SAND } }, 0, false },
		// 1.4 : les épées, la cuisine, la laine des araignées
		{ "wood_sword", I_WOOD_SWORD, 1, "M/M/S", { { 'M', M_WOOD }, { 'S', I_STICK } }, 0, false },
		{ "stone_sword", I_STONE_SWORD, 1, "M/M/S", { { 'M', M_STONE }, { 'S', I_STICK } }, 0, false },
		{ "iron_sword", I_IRON_SWORD, 1, "M/M/S", { { 'M', M_IRON }, { 'S', I_STICK } }, 0, false },
		{ "crystal_sword", I_CRYSTAL_SWORD, 1, "M/M/S", { { 'M', M_CRYSTAL }, { 'S', I_STICK } }, 0, false },
		{ "cooked_chicken", I_COOKED_CHICKEN, 1, "X", { { 'X', I_RAW_CHICKEN } }, ST_FURNACE, true },
		{ "cooked_pork", I_COOKED_PORK, 1, "X", { { 'X', I_RAW_PORK } }, ST_FURNACE, true },
		{ "wool_from_string", WOOL_WHITE, 1, "SS/SS", { { 'S', I_STRING } }, 0, false },
	};
	for (auto &r : R) {
		// la taille de la forme, ce qu'elle prend en tout, où elle se fait
		int w = 0, h = 1, cw = 0;
		for (const char *p = r.shape; *p; p++)
			if (*p == '/') h++, cw = 0;
			else w = std::max (w, ++cw);
		r.w = r.shapeless ? (int) strlen (r.shape) : w;
		r.h = r.shapeless ? 1 : h;
		for (auto &in : r.in) in = { 0, 0 };
		for (const char *p = r.shape; *p; p++) {
			int it = 0;
			for (auto &k : r.keys)
				if (k.c == *p) it = k.item;
			if (!it) continue;
			for (auto &in : r.in)
				if (in.item == it || !in.item) {
					in.item = it, in.n++;
					break;
				}
		}
		if (r.station != ST_FURNACE) {
			int cells = 0;
			for (auto &in : r.in) cells += in.n;
			r.station = r.shapeless ? (cells <= 4 ? ST_HAND : ST_WORKBENCH) : (r.w <= 2 && r.h <= 2 ? ST_HAND : ST_WORKBENCH);
		}
	}
	return R;
}

} // namespace

const std::vector<Recipe> &recipes ()
{
	static const std::vector<Recipe> R = buildRecipes ();
	return R;
}

const Recipe *matchGrid (const int *g, int n)
{
	// la boîte des cases pleines
	int x0 = n, y0 = n, x1 = -1, y1 = -1, cnt = 0;
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
			if (g[y * n + x]) x0 = std::min (x0, x), y0 = std::min (y0, y), x1 = std::max (x1, x), y1 = std::max (y1, y), cnt++;
	if (!cnt) return nullptr;
	int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
	for (auto &r : recipes ()) {
		if (r.station == ST_FURNACE || (r.station == ST_WORKBENCH && n < 3)) continue;
		if (r.shapeless) {
			// les mêmes objets, en même nombre, n'importe où
			int need = 0;
			bool ok = true;
			for (auto &in : r.in) {
				if (!in.n) continue;
				need += in.n;
				int have = 0;
				for (int i = 0; i < n * n; i++) have += g[i] == in.item;
				ok &= have == in.n;
			}
			if (ok && need == cnt) return &r;
			continue;
		}
		if (r.w != bw || r.h != bh) continue;
		for (int mirror = 0; mirror < 2; mirror++) {
			bool ok = true;
			int row = 0, col = 0;
			for (const char *p = r.shape; ok; p++) {
				if (*p == '/' || !*p) {
					for (; col < r.w; col++) ok &= g[(y0 + row) * n + x0 + (mirror ? r.w - 1 - col : col)] == 0;	// (une ligne plus courte)
					if (!*p) break;
					row++, col = 0;
					continue;
				}
				int want = 0;
				for (auto &k : r.keys)
					if (k.c == *p) want = k.item;
				int have = g[(y0 + row) * n + x0 + (mirror ? r.w - 1 - col : col)];
				ok &= have == want;
				col++;
			}
			if (ok) return &r;
		}
	}
	return nullptr;
}

std::string recipeText (const Recipe &r)
{
	std::string t;
	if (r.shapeless) {
		bool first = true;
		for (auto &in : r.in)
			if (in.n) t += (first ? "" : " + ") + std::to_string (in.n) + " " + itemInfo (in.item).label, first = false;
		return t + " (n'importe où dans la grille)";
	}
	t = "[";
	for (const char *p = r.shape; *p; p++) t += *p == '/' ? std::string ("] [") : *p == '.' ? std::string ("·") : std::string (1, *p);
	t += "]  ";
	bool first = true;
	for (auto &k : r.keys)
		if (k.c) t += (first ? "" : ", ") + std::string (1, k.c) + " : " + itemInfo (k.item).label, first = false;
	return t;
}

const Recipe *recipeByName (const char *s)
{
	if (!s || !*s) return nullptr;
	auto &R = recipes ();
	char *end;
	long v = strtol (s, &end, 10);
	if (*end == 0) return v >= 1 && v <= (long) R.size () ? &R[(size_t) v - 1] : nullptr;
	for (auto &r : R)
		if (!strcasecmp (s, r.name)) return &r;
	int it = itemByName (s);	// (le nom de ce qui est fait : la première recette qui le fait)
	for (auto &r : R)
		if (r.out == it) return &r;
	return nullptr;
}

const char *stationLabel (int st)
{
	return st == ST_WORKBENCH ? "établi" : st == ST_FURNACE ? "four" : "à la main";
}

int craftable (const Recipe &r, const Inventory &inv)
{
	int n = 1 << 20;
	for (auto &in : r.in)
		if (in.n > 0) n = std::min (n, inv.count (in.item) / in.n);
	return n == (1 << 20) ? 0 : n;
}

Food foodOf (int item)
{
	switch (item) {
	case I_APPLE: return { 4, 2.4f };
	case I_RAW_CHICKEN: return { 2, 1.2f };
	case I_COOKED_CHICKEN: return { 6, 7.2f };
	case I_RAW_PORK: return { 3, 1.8f };
	case I_COOKED_PORK: return { 8, 12.8f };
	default: return { 0, 0 };
	}
}

float attackDamage (int item)
{
	ItemInfo t = itemInfo (item);
	switch (t.tool) {
	case TOOL_SWORD: return 3.0f + t.tier;		// 4 à 7
	case TOOL_AXE: return 2.0f + t.tier;		// 3 à 6
	case TOOL_PICKAXE: return 1.0f + t.tier;	// 2 à 5
	case TOOL_SHOVEL: return 1.5f + t.tier * 0.5f;
	default: return 1.0f;
	}
}

float fuelValue (int item)
{
	switch (item) {
	case I_COAL: return 8;
	case LOG: case PINE_LOG: return 4;
	case PLANKS: return 2;
	case I_STICK: return 1;
	default: return 0;
	}
}

int craft (const Recipe &r, int times, Inventory &inv, float &fuel, bool hasWorkbench, bool hasFurnace, std::string &err)
{
	if (r.station == ST_WORKBENCH && !hasWorkbench) {
		err = "il faut un établi à moins de 4 blocs";
		return 0;
	}
	if (r.station == ST_FURNACE && !hasFurnace) {
		err = "il faut un four à moins de 4 blocs";
		return 0;
	}
	int done = 0;
	for (; done < times; done++) {
		if (craftable (r, inv) < 1) {
			err = "il manque des ingrédients";
			break;
		}
		if (r.station == ST_FURNACE && fuel < 1) {
			// du combustible : le premier qui brûle, sans prendre un ingrédient de la recette
			int best = 0;
			for (int f : { (int) I_COAL, (int) LOG, (int) PINE_LOG, (int) PLANKS, (int) I_STICK }) {
				int need = 0;
				for (auto &in : r.in)
					if (in.item == f) need = in.n;
				if (inv.count (f) > need) {
					best = f;
					break;
				}
			}
			if (!best) {
				err = "le four n'a plus de combustible (charbon, bois, planches, bâtons)";
				break;
			}
			inv.take (best, 1);
			fuel += fuelValue (best);
		}
		// la place pour le résultat (sinon rien n'est pris)
		Inventory test = inv;
		for (auto &in : r.in)
			if (in.n > 0) test.take (in.item, in.n);
		if (test.add (r.out, r.outN) > 0) {
			err = "l'inventaire est plein";
			break;
		}
		inv = test;
		if (r.station == ST_FURNACE) fuel -= 1;
	}
	return done;
}

} // namespace tc
