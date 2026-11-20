// sim.h -- TerraCube : ce qui vit dans le monde, pas à pas (20 pas par seconde) : l'eau et la lave qui coulent, les
// circuits de cuivre (fil, torches, leviers, boutons, plaques, répéteurs, comparateurs, observateurs, pistons,
// lampes, blocs musicaux, capteurs de jour, dynamite).
//
// Le monde prévient la simulation de chaque changement (World::onChange) : les blocs voisins sont mis à jour tout de
// suite (le fil, la lampe qui s'allume) ou plus tard (un pas programmé : le répéteur, la torche, le liquide qui
// coule). Les pas programmés ne sont pas sauvés : un tronçon relu relance ce qui doit l'être (World::onLoad).
//
// Les règles sont celles des jeux du genre : la puissance va de 15 à 0 et baisse de 1 par bloc de fil ; un bloc
// plein est alimenté fort (par une torche dessous, un répéteur, un levier posé dessus…) ou faible (par du fil qui
// pointe dedans) ; un bloc alimenté fort alimente le fil voisin, un bloc alimenté faible seulement les composants.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include "world.h"

namespace tc {
namespace sim {

const int DX[6] = { 1, -1, 0, 0, 0, 0 }, DY[6] = { 0, 0, 1, -1, 0, 0 }, DZ[6] = { 0, 0, 0, 0, 1, -1 };
enum { D_EAST, D_WEST, D_UP, D_DOWN, D_SOUTH, D_NORTH };	// +x, -x, +y, -y, +z, -z (les faces de mesh.cpp)
inline int opposite (int d) { return d ^ 1; }

// les sons de la simulation (main.cpp les place par rapport au joueur) : sfx, matière, volume, case, et un
// argument (la note d'un bloc musical)
enum SimSound { SS_CLICK, SS_PISTON_OUT, SS_PISTON_IN, SS_FIZZ, SS_EXPLODE, SS_NOTE, SS_FUSE };
extern void (*soundHook) (int sound, int arg, int x, int y, int z);
// une explosion (main.cpp : pousse le joueur) ; ce qui tombe d'un bloc cassé par la simulation (survie : au joueur
// proche) ; un bloc poussé par un piston dans la case (x, y, z) vers dir (le joueur qui s'y trouve est poussé)
extern void (*blastHook) (double x, double y, double z, float power);
extern void (*dropHook) (int item, int x, int y, int z);
extern void (*pushHook) (int x, int y, int z, int dir);
// quelqu'un (le joueur, une créature) a-t-il les pieds dans cette case ? (les plaques ; sinon : le joueur seul)
extern bool (*occupiedHook) (int x, int y, int z);

void attach (World *w);			// branche onChange et onLoad, oublie tout ce qui était en attente
// avance de dt secondes (au plus 5 pas par appel) ; daylight 0 à 1 (le capteur) ; les pieds du joueur (plaques)
void step (double dt, float daylight, double px, double py, double pz);
void runTicks (int n);			// n pas tout de suite (le test)
void press (int x, int y, int z);	// des pieds sur cette case : une plaque s'enfonce
long now ();				// les pas depuis le début

// un clic droit sur un bloc -> vrai s'il a servi (levier, bouton, répéteur…) ; held : l'objet en main
bool use (int x, int y, int z, int held);
// l'état d'un bloc posé : face, la face du bloc visé où il est posé (0 à 5, vers le nouveau bloc) ; le regard ->
// -1 si on ne peut pas le poser là (pas de support)
int placeMeta (int block, int x, int y, int z, int face, float yaw, float pitch);
// un bloc cassé par le joueur (block, meta : ce qu'il était) : ce qu'il entraîne (la tête d'un piston sorti, le
// piston d'une tête) -> le piston enlevé avec la tête (pour ce qui tombe), sinon 0
int broken (int x, int y, int z, int block, int meta);

// la puissance d'un bloc (le fil, la sortie d'un composant, un bloc plein alimenté) : pour les scripts
int powerAt (int x, int y, int z);
int pending ();				// les pas programmés en attente

} // namespace sim
} // namespace tc
