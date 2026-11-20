// items.h -- TerraCube : les objets (les blocs, puis les matières et les outils), l'inventaire du mode survie, le
// minage (dureté, outils, ce qui tombe) et les recettes de fabrication.
//
// Un objet a un numéro : 0 à BLOCK_COUNT - 1 pour les blocs (blocks.h), 100 et plus pour le reste. Comme pour les
// blocs, on ajoute à la fin, on ne retire ni ne déplace jamais (un monde sauvé garde ses numéros).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <string>
#include <vector>
#include "blocks.h"

namespace tc {

enum Item {
	I_STICK = 100,
	I_COAL,
	I_IRON_INGOT,
	I_GOLD_INGOT,
	I_CRYSTAL,
	I_WOOD_PICKAXE,
	I_STONE_PICKAXE,
	I_IRON_PICKAXE,
	I_CRYSTAL_PICKAXE,
	I_WOOD_AXE,
	I_STONE_AXE,
	I_IRON_AXE,
	I_CRYSTAL_AXE,
	I_WOOD_SHOVEL,
	I_STONE_SHOVEL,
	I_IRON_SHOVEL,
	I_CRYSTAL_SHOVEL,
	// 1.3
	I_BUCKET,
	I_WATER_BUCKET,
	I_LAVA_BUCKET,
	I_RESIN,
	// 1.4 : la nourriture, ce que donnent les créatures, les épées
	I_SEEDS,
	I_APPLE,
	I_RAW_CHICKEN,
	I_COOKED_CHICKEN,
	I_RAW_PORK,
	I_COOKED_PORK,
	I_FEATHER,
	I_BONE,
	I_STRING,
	I_WOOD_SWORD,
	I_STONE_SWORD,
	I_IRON_SWORD,
	I_CRYSTAL_SWORD,
	I_END
};

enum Tool { TOOL_NONE, TOOL_PICKAXE, TOOL_AXE, TOOL_SHOVEL, TOOL_SWORD };

struct ItemInfo {
	const char *name;	// nom anglais (scripts, MCP, console)
	const char *label;	// nom montré
	int tile;		// tuile de l'icône (un bloc : son cube)
	int maxStack;		// 64, ou 1 pour un outil
	int tool;		// TOOL_*
	int tier;		// 1 bois, 2 pierre, 3 fer, 4 cristal
	int durability;		// coups avant de casser (0 : pas un outil)
};

bool itemValid (int id);
ItemInfo itemInfo (int id);
bool isBlockItem (int id);
int itemByName (const char *s);			// nom anglais, nom montré ou numéro -> -1
std::vector<int> allItems ();

// ---- l'inventaire : 36 cases, les 9 premières sont la barre --------------------------------------------------------

struct Stack {
	int item = 0, count = 0, dur = 0;	// dur : usure d'un outil (coups donnés)
	bool empty () const { return count <= 0 || item == 0; }
};

struct Inventory {
	static const int N = 36;
	Stack s[N];
	int add (int item, int n);		// -> ce qui n'a pas trouvé de place
	int count (int item) const;
	bool take (int item, int n);		// tout ou rien
	void clear ();
	std::string save () const;		// « item:nombre:usure,… »
	void load (const std::string &t);
};

// ---- le minage ----------------------------------------------------------------------------------------------------

struct Mining {
	float hardness;		// secondes à la main (< 0 : incassable)
	int tool;		// l'outil qui va le plus vite
	int tier;		// outil (pioche) qu'il faut pour que le bloc tombe (0 : rien)
	int drop, dropN;	// ce qui tombe (0 : rien)
	float chance;		// probabilité que cela tombe
};

Mining mining (int block);
// le temps pour casser ce bloc avec cet objet en main (secondes ; < 0 : impossible) ; harvest : il tombera
float mineTime (int block, int held, bool *harvest);

// ---- la fabrication -----------------------------------------------------------------------------------------------

enum Station { ST_HAND, ST_WORKBENCH, ST_FURNACE };

struct Ingredient {
	int item, n;
};

struct Key {
	char c;
	int item;
};

// Une recette a une forme : des lignes séparées par « / », une lettre par case, « . » pour une case vide
// (« MMM/.S./.S. » : trois matières en haut, deux bâtons au milieu en dessous). La forme peut être posée n'importe
// où dans la grille, et retournée de gauche à droite. Une recette sans forme (shapeless) donne seulement ses
// lettres, une par objet, posées n'importe comment. Une forme qui tient en 2 × 2 se fait à la main ; les autres
// demandent l'établi (3 × 3) ; les recettes du four ont une seule lettre.
struct Recipe {
	const char *name;		// identifiant anglais (« wood_pickaxe »)
	int out, outN;
	const char *shape;
	Key keys[4];
	int station;			// ST_HAND, ST_WORKBENCH, ST_FURNACE (calculé pour les deux premiers)
	bool shapeless;
	Ingredient in[4];		// (calculé : ce qu'elle prend en tout)
	int w, h;			// (calculé : la taille de la forme)
};

const std::vector<Recipe> &recipes ();
const Recipe *recipeByName (const char *s);	// identifiant, nom de l'objet fait, ou numéro dans la liste
const char *stationLabel (int st);
// combien de fois cette recette peut être faite avec cet inventaire (sans compter le combustible)
int craftable (const Recipe &r, const Inventory &inv);
// la recette que forment les objets d'une grille (n × n, ligne par ligne, 0 : vide) -> nullptr si aucune
const Recipe *matchGrid (const int *items, int n);
// la forme d'une recette en texte, pour le livre des recettes (une ligne par rangée, noms des objets en légende)
std::string recipeText (const Recipe &r);
// fait la recette « times » fois au plus -> fois faites ; err : pourquoi pas plus. fuel : les points de chauffe du four
int craft (const Recipe &r, int times, Inventory &inv, float &fuel, bool hasWorkbench, bool hasFurnace, std::string &err);
// les points de chauffe d'un combustible (0 : ne brûle pas)
float fuelValue (int item);

// ---- manger, se battre --------------------------------------------------------------------------------------------

struct Food {
	int hunger;		// points de faim rendus (0 : ça ne se mange pas)
	float saturation;
};
Food foodOf (int item);
float attackDamage (int item);		// les dégâts d'un coup avec cet objet en main (la main : 1)

} // namespace tc
