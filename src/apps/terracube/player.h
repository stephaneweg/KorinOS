// player.h -- TerraCube : le déplacement du joueur (player.cpp).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include "game.h"

namespace tc {

struct MoveInput {
	double forward = 0, strafe = 0;	// -1 à 1
	bool jump = false, sneak = false, sprint = false, noclip = false;
};

void playerStep (const World &w, Player &p, const MoveInput &in, double dt);
// le bloc (x, y, z) est-il dans la boîte du joueur ? (on n'y pose rien)
bool playerInside (const Player &p, int x, int y, int z);

} // namespace tc
