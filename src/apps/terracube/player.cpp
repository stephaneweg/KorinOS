// player.cpp -- TerraCube : le déplacement du joueur (marche, saut, nage, vol) et ses collisions avec les blocs.
//
// Le joueur est une boîte de 0,6 × 1,8 × 0,6 bloc ; chaque axe est déplacé à son tour, puis ramené contre le premier
// bloc solide touché (y d'abord : on se pose, puis x et z : on glisse le long des murs). Une marche d'un demi-bloc
// au plus n'est pas montée seule : il faut sauter, comme dans la plupart des jeux du genre.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "player.h"

#include <math.h>

namespace tc {

namespace {

bool solidAt (const World &w, int x, int y, int z)
{
	if (y < 0) return true;
	if (y >= CH) return false;
	// (un tronçon pas encore fait : solide, qu'on ne tombe pas dans le vide en l'attendant)
	if (!w.find (floorDiv (x, CW), floorDiv (z, CW))) return true;
	return isSolid (w.get (x, y, z));
}

// la boîte [x0, x1] × [y0, y1] × [z0, z1] touche-t-elle un bloc solide ?
bool boxHits (const World &w, double x0, double y0, double z0, double x1, double y1, double z1)
{
	for (int y = (int) floor (y0); y <= (int) floor (y1 - 1e-9); y++)
		for (int z = (int) floor (z0); z <= (int) floor (z1 - 1e-9); z++)
			for (int x = (int) floor (x0); x <= (int) floor (x1 - 1e-9); x++)
				if (solidAt (w, x, y, z)) return true;
	return false;
}

bool hitsAt (const World &w, double x, double y, double z)
{
	return boxHits (w, x - Player::HALF, y, z - Player::HALF, x + Player::HALF, y + Player::HEIGHT, z + Player::HALF);
}

// un axe : avance par petits pas, s'arrête contre un bloc -> vrai si bloqué
bool moveAxis (const World &w, Player &p, int axis, double d)
{
	if (d == 0) return false;
	int steps = (int) ceil (fabs (d) / 0.2);
	double s = d / steps;
	for (int i = 0; i < steps; i++) {
		double nx = p.x + (axis == 0 ? s : 0), ny = p.y + (axis == 1 ? s : 0), nz = p.z + (axis == 2 ? s : 0);
		if (hitsAt (w, nx, ny, nz)) {
			// contre la face du bloc
			if (axis == 1) {
				if (s < 0) p.y = floor (p.y + s) + 1;
				else p.y = floor (p.y + Player::HEIGHT + s) - Player::HEIGHT - 1e-4;
				if (hitsAt (w, p.x, p.y, p.z)) p.y -= s;	// (on garde l'ancienne place)
			}
			return true;
		}
		p.x = nx, p.y = ny, p.z = nz;
	}
	return false;
}

} // namespace

bool playerInside (const Player &p, int x, int y, int z)
{
	return x + 1 > p.x - Player::HALF && x < p.x + Player::HALF && z + 1 > p.z - Player::HALF && z < p.z + Player::HALF &&
	       y + 1 > p.y && y < p.y + Player::HEIGHT;
}

void playerStep (const World &w, Player &p, const MoveInput &in, double dt)
{
	if (dt > 0.1) dt = 0.1;	// (une image lente : pas de bond à travers les murs)
	int bx = (int) floor (p.x), bz = (int) floor (p.z);
	if (!w.find (floorDiv (bx, CW), floorDiv (bz, CW))) return;	// (le sol pas encore fait)
	int feet = w.get (bx, (int) floor (p.y + 0.1), bz), body = w.get (bx, (int) floor (p.y + 1.0), bz);
	p.inLava = feet == LAVA || body == LAVA;
	p.inWater = feet == WATER || body == WATER || p.inLava;	// (la lave : on y nage, lentement)
	double yaw = p.yaw * M_PI / 180.0;
	double fx = sin (yaw), fz = -cos (yaw);		// devant
	double rx = cos (yaw), rz = sin (yaw);		// à droite
	double wx = fx * in.forward + rx * in.strafe, wz = fz * in.forward + rz * in.strafe;
	double len = sqrt (wx * wx + wz * wz);
	if (len > 1) wx /= len, wz /= len;
	if (p.fly) {
		double sp = in.sprint ? 22.0 : 11.0;
		double tx = wx * sp, tz = wz * sp, ty = (in.jump ? sp * 0.8 : 0) - (in.sneak ? sp * 0.8 : 0);
		double k = 1 - exp (-dt * 10);
		p.vx += (tx - p.vx) * k, p.vz += (tz - p.vz) * k, p.vy += (ty - p.vy) * k;
		if (in.noclip) {
			p.x += p.vx * dt, p.y += p.vy * dt, p.z += p.vz * dt;
			if (p.y < 1) p.y = 1;
			if (p.y > CH + 40) p.y = CH + 40;
			return;
		}
	} else if (p.inWater) {
		double sp = p.inLava ? 1.2 : 2.6;
		double k = 1 - exp (-dt * 6);
		p.vx += (wx * sp - p.vx) * k, p.vz += (wz * sp - p.vz) * k;
		p.vy -= (p.inLava ? 6.0 : 9.0) * dt;
		if (in.jump) p.vy += (p.inLava ? 14.0 : 22.0) * dt;
		if (p.vy < -3.5) p.vy = p.inLava ? -2.0 : -3.5;
		if (p.vy > 3.2) p.vy = p.inLava ? 2.0 : 3.2;
	} else {
		double sp = in.sprint ? 5.6 : in.sneak ? 1.5 : 4.3;
		double k = 1 - exp (-dt * (p.onGround ? 14.0 : 2.5));
		p.vx += (wx * sp - p.vx) * k, p.vz += (wz * sp - p.vz) * k;
		p.vy -= 28.0 * dt;
		if (p.vy < -55) p.vy = -55;
		if (in.jump && p.onGround) p.vy = 8.7;
	}
	double ox = p.x, oz = p.z;
	bool wasGround = p.onGround;
	p.onGround = false;
	if (moveAxis (w, p, 1, p.vy * dt)) {
		if (p.vy < 0) p.onGround = true;
		p.vy = 0;
	}
	if (moveAxis (w, p, 0, p.vx * dt)) p.vx = 0;
	if (moveAxis (w, p, 2, p.vz * dt)) p.vz = 0;
	// accroupi au sol : on ne tombe pas du bord
	if (in.sneak && !p.fly && wasGround && !p.inWater) {
		auto ground = [&] (double x, double z) { return boxHits (w, x - Player::HALF, p.y - 0.6, z - Player::HALF, x + Player::HALF, p.y, z + Player::HALF); };
		if (!ground (p.x, p.z)) {
			if (ground (ox, p.z)) p.x = ox, p.vx = 0;
			else if (ground (p.x, oz)) p.z = oz, p.vz = 0;
			else p.x = ox, p.z = oz, p.vx = p.vz = 0;
			p.onGround = true;
			p.vy = 0;
		}
	}
	// en vol, se poser arrête le vol au sol ? non : on vole jusqu'à ce qu'on le coupe (F)
	if (p.y < -10) p.y = CH + 5, p.vy = 0;	// (tombé hors du monde : on revient par le haut)
}

} // namespace tc
