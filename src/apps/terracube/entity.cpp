// entity.cpp -- TerraCube : les créatures (entity.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "entity.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include "audio.h"
#include "game.h"
#include "items.h"

namespace tc {

namespace {

const EntityInfo INFO[E_TYPE_COUNT] = {
	{ "chicken", "Poule", 0.2f, 0.7f, 1.6f, 4, false },
	{ "pig", "Cochon", 0.45f, 0.9f, 1.8f, 10, false },
	{ "wolf", "Loup", 0.3f, 0.85f, 3.6f, 8, false },
	{ "rodeur", "Rôdeur", 0.3f, 1.9f, 2.6f, 20, true },
	{ "spider", "Araignée", 0.65f, 0.8f, 4.2f, 14, true },
};

inline double frand () { return (rand () % 100000) / 100000.0; }
inline double frange (double a, double b) { return a + (b - a) * frand (); }

// ---- le corps : les collisions (comme le joueur) ---------------------------------------------------------------

bool solidAt (const World &w, int x, int y, int z)
{
	if (y < 0) return true;
	if (y >= CH) return false;
	if (!w.find (floorDiv (x, CW), floorDiv (z, CW))) return true;
	return isSolid (w.get (x, y, z));
}

bool boxHits (const World &w, double x0, double y0, double z0, double x1, double y1, double z1)
{
	for (int y = (int) floor (y0); y <= (int) floor (y1 - 1e-9); y++)
		for (int z = (int) floor (z0); z <= (int) floor (z1 - 1e-9); z++)
			for (int x = (int) floor (x0); x <= (int) floor (x1 - 1e-9); x++)
				if (solidAt (w, x, y, z)) return true;
	return false;
}

bool hitsAt (const World &w, const Entity &e, double x, double y, double z)
{
	const EntityInfo &in = INFO[e.type];
	double h = in.half * (e.age < 0 ? 0.6 : 1.0), ht = in.height * (e.age < 0 ? 0.6 : 1.0);
	return boxHits (w, x - h, y, z - h, x + h, y + ht, z + h);
}

bool moveAxis (const World &w, Entity &e, int axis, double d)
{
	if (d == 0) return false;
	int steps = (int) ceil (fabs (d) / 0.2);
	double s = d / steps;
	for (int i = 0; i < steps; i++) {
		double nx = e.x + (axis == 0 ? s : 0), ny = e.y + (axis == 1 ? s : 0), nz = e.z + (axis == 2 ? s : 0);
		if (hitsAt (w, e, nx, ny, nz)) {
			if (axis == 1 && s < 0) e.y = floor (e.y + s) + 1;
			if (hitsAt (w, e, e.x, e.y, e.z)) e.y += 0.01;
			return true;
		}
		e.x = nx, e.y = ny, e.z = nz;
	}
	return false;
}

double dist2 (const Entity &e, double x, double z) { return (e.x - x) * (e.x - x) + (e.z - z) * (e.z - z); }
double distP (const Entity &e) { return sqrt (dist2 (e, g.pl.x, g.pl.z) + (e.y - g.pl.y) * (e.y - g.pl.y)); }

float panOf (double x, double z)
{
	double dx = x - g.pl.x, dz = z - g.pl.z, d = sqrt (dx * dx + dz * dz);
	if (d < 0.5) return 0;
	double yaw = g.pl.yaw * M_PI / 180.0;
	return (float) ((dx * cos (yaw) + dz * sin (yaw)) / d * 0.7);
}

// un cri, plus faible au loin
void cry (const Entity &e, int sfx, int arg = 0, float vol = 1.0f)
{
	double d = distP (e);
	float v = vol * (float) std::max (0.0, 1.0 - d / 20.0);
	if (v > 0.02f) audio::play ((audio::Sfx) sfx, arg, v, panOf (e.x, e.z));
}

int voiceOf (int type)
{
	switch (type) {
	case E_CHICKEN: return audio::S_CHICKEN;
	case E_PIG: return audio::S_PIG;
	case E_WOLF: return audio::S_WOLF;
	case E_RODEUR: return audio::S_RODEUR;
	default: return audio::S_SPIDER;
	}
}

bool loaded (const World &w, double x, double z) { return w.find (floorDiv ((int) floor (x), CW), floorDiv ((int) floor (z), CW)) != nullptr; }

} // namespace

const EntityInfo &entityInfo (int type) { return INFO[(unsigned) type < E_TYPE_COUNT ? type : 0]; }

int entityByName (const char *s)
{
	if (!s) return -1;
	for (int i = 0; i < E_TYPE_COUNT; i++)
		if (!strcasecmp (s, INFO[i].name) || !strcasecmp (s, INFO[i].label)) return i;
	if (!strcasecmp (s, "zombie") || !strcasecmp (s, "prowler")) return E_RODEUR;
	return -1;
}

Entity *Entities::spawn (int type, double x, double y, double z)
{
	if ((unsigned) type >= E_TYPE_COUNT) return nullptr;
	Entity e;
	e.id = nextId++;
	e.type = type;
	e.x = x, e.y = y, e.z = z;
	e.fallY = y;
	e.yaw = (float) frange (0, 360);
	e.headYaw = 0;
	e.health = INFO[type].health;
	e.sound = (float) frange (3, 15);
	list.push_back (e);
	return &list.back ();
}

Entity *Entities::get (int id)
{
	for (auto &e : list)
		if (e.id == id) return &e;
	return nullptr;
}

int Entities::count (int type, double radius) const
{
	int n = 0;
	for (auto &e : list)
		if ((type < 0 || e.type == type) && e.dead < 0 && dist2 (e, g.pl.x, g.pl.z) < radius * radius) n++;
	return n;
}

int Entities::pick (const float eye[3], const float dir[3], float maxDist, float &dist) const
{
	int best = 0;
	dist = maxDist;
	for (auto &e : list) {
		if (e.dead >= 0) continue;
		const EntityInfo &in = INFO[e.type];
		double s = e.age < 0 ? 0.6 : 1.0, h = in.half * s + 0.1, ht = in.height * s + 0.1;
		double lo[3] = { e.x - h, e.y - 0.05, e.z - h }, hi[3] = { e.x + h, e.y + ht, e.z + h };
		double t0 = 0, t1 = maxDist;
		bool ok = true;
		for (int a = 0; a < 3 && ok; a++) {
			if (fabs (dir[a]) < 1e-9) {
				if (eye[a] < lo[a] || eye[a] > hi[a]) ok = false;
				continue;
			}
			double ta = (lo[a] - eye[a]) / dir[a], tb = (hi[a] - eye[a]) / dir[a];
			if (ta > tb) std::swap (ta, tb);
			t0 = std::max (t0, ta), t1 = std::min (t1, tb);
			if (t0 > t1) ok = false;
		}
		if (ok && t0 < dist) dist = (float) t0, best = e.id;
	}
	return best;
}

void Entities::hurtEntity (Entity &e, float dmg, double fx, double fz, int attacker)
{
	if (e.dead >= 0 || dmg <= 0) return;
	if (e.tamed && attacker == -1) return;	// (le maître ne frappe pas son loup)
	e.health -= dmg;
	e.hurt = 0.35f;
	e.lastAttacker = attacker;
	if (attacker == -1 || (attacker > 0 && get (attacker) && get (attacker)->tamed)) e.byPlayer = true;
	double dx = e.x - fx, dz = e.z - fz, d = sqrt (dx * dx + dz * dz);
	if (d > 0.01) e.vx += dx / d * 6.0, e.vz += dz / d * 6.0;
	e.vy = std::max (e.vy, 4.5);
	if (!INFO[e.type].hostile && e.type != E_WOLF) e.panic = 5;
	if (e.type == E_WOLF && !e.tamed) e.angry = 30, e.target = attacker;
	if (INFO[e.type].hostile && attacker != 0) e.target = attacker;
	if (e.type == E_WOLF && e.tamed && attacker > 0) e.target = attacker;	// (assis, il garde : il se défend, puis revient)
	cry (e, audio::S_MOB_HURT, e.type);
	if (e.health <= 0) {
		e.dead = 0;
		e.health = 0;
	}
}

bool Entities::interact (Entity &e, int held, bool &used, std::string &msg)
{
	used = false;
	if (e.dead >= 0) return false;
	bool meat = held == I_RAW_CHICKEN || held == I_COOKED_CHICKEN || held == I_RAW_PORK || held == I_COOKED_PORK;
	// nourrir un animal adulte : il cherche un partenaire
	int food = e.type == E_CHICKEN ? I_SEEDS : e.type == E_PIG ? I_APPLE : e.type == E_WOLF && e.tamed ? -2 : 0;
	if ((food > 0 && held == food) || (food == -2 && meat && e.health >= INFO[e.type].health * 2.5f)) {
		if (e.age < 0 || e.love > 0 || e.breedCool > 0) return false;
		e.love = 30;
		used = true;
		msg = std::string (INFO[e.type].label) + " : prêt à avoir un petit";
		cry (e, voiceOf (e.type), 0);
		return true;
	}
	if (e.type != E_WOLF) return false;
	if (!e.tamed) {
		if (held != I_BONE) return false;
		used = true;
		if (rand () % 3 == 0) {
			e.tamed = true, e.sitting = true, e.angry = 0, e.target = 0;
			e.hx = e.x, e.hy = e.y, e.hz = e.z;
			e.health = INFO[E_WOLF].health * 2.5f;
			msg = "Le loup est apprivoisé : il garde cette place (clic droit : il te suit)";
			cry (e, audio::S_WOLF, 2);
		} else
			msg = "Le loup ronge l'os, méfiant", cry (e, audio::S_WOLF, 1, 0.6f);
		return true;
	}
	// un loup apprivoisé : la viande le soigne ; sinon il s'assoit (il garde) ou se lève (il suit)
	if (meat && e.health < INFO[E_WOLF].health * 2.5f) {
		e.health = std::min (INFO[E_WOLF].health * 2.5f, e.health + 6);
		used = true;
		msg = "Le loup est soigné";
		return true;
	}
	e.sitting = !e.sitting;
	if (e.sitting) e.hx = e.x, e.hy = e.y, e.hz = e.z;
	e.target = 0;
	msg = e.sitting ? "Le loup garde cette place" : "Le loup te suit";
	cry (e, audio::S_WOLF, 0, 0.5f);
	return true;
}

void Entities::blast (double x, double y, double z, float power)
{
	for (auto &e : list) {
		double dx = e.x - x, dy = e.y + 0.5 - y, dz = e.z - z, d = sqrt (dx * dx + dy * dy + dz * dz);
		if (d > power * 2) continue;
		float dmg = (float) ((1 - d / (power * 2)) * 18);
		hurtEntity (e, dmg, x, z, 0);
		e.vy += 4;
	}
}

// ---- l'esprit et le corps, à chaque image ---------------------------------------------------------------------

namespace {

// avancer vers (tx, tz) à cette vitesse (0 : s'arrêter) ; tourner le corps
void steer (Entity &e, double tx, double tz, double speed, double dt)
{
	double dx = tx - e.x, dz = tz - e.z, d = sqrt (dx * dx + dz * dz);
	double wx = 0, wz = 0;
	if (d > 0.15 && speed > 0) {
		wx = dx / d * speed, wz = dz / d * speed;
		float want = (float) (atan2 (dx, -dz) * 180.0 / M_PI);
		float diff = fmodf (want - e.yaw + 540.0f, 360.0f) - 180.0f;
		e.yaw += diff * (float) std::min (1.0, dt * 8.0);
	}
	double k = 1 - exp (-dt * (e.onGround ? 10.0 : 2.0));
	e.vx += (wx - e.vx) * k, e.vz += (wz - e.vz) * k;
}

// le sol devant (pas de chute de plus de « drop » blocs, pas de lave)
bool safeAhead (const World &w, const Entity &e, int drop)
{
	double sp = sqrt (e.vx * e.vx + e.vz * e.vz);
	if (sp < 0.1) return true;
	int fx = (int) floor (e.x + e.vx / sp * 0.8), fz = (int) floor (e.z + e.vz / sp * 0.8), fy = (int) floor (e.y + 0.1);
	for (int k = 0; k <= drop + 1; k++) {
		int b = w.get (fx, fy - k, fz);
		if (b == LAVA) return false;
		if (isSolid (b) || b == WATER) return true;
	}
	return false;
}

Entity *nearestOf (Entities &es, const Entity &me, bool (*want) (const Entity &, const Entity &), double r)
{
	Entity *best = nullptr;
	double bd = r * r;
	for (auto &o : es.list) {
		if (o.id == me.id || o.dead >= 0 || !want (me, o)) continue;
		double d = dist2 (me, o.x, o.z) + (me.y - o.y) * (me.y - o.y);
		if (d < bd) bd = d, best = &o;
	}
	return best;
}

void wander (Entity &e, const World &w, double dt, double speed)
{
	e.think -= (float) dt;
	if (e.think <= 0) {
		e.think = (float) frange (3, 9);
		if (frand () < 0.65) {
			double a = frange (0, 2 * M_PI), r = frange (2, 7);
			e.gx = e.x + cos (a) * r, e.gz = e.z + sin (a) * r;
			e.hasGoal = true;
		} else
			e.hasGoal = false;
	}
	if (e.hasGoal && dist2 (e, e.gx, e.gz) < 0.3) e.hasGoal = false;
	steer (e, e.gx, e.gz, e.hasGoal ? speed * 0.6 : 0, dt);
	if (!safeAhead (w, e, 2)) e.vx = e.vz = 0, e.hasGoal = false;
}

// attaquer ce qui est visé (le joueur, une créature) : s'approcher, frapper quand on le touche
void chase (Entities &es, Entity &e, const World &w, double dt, double tx, double ty, double tz, float dmg, int victim)
{
	const EntityInfo &in = INFO[e.type];
	double d = sqrt (dist2 (e, tx, tz));
	steer (e, tx, tz, d > 0.8 + in.half ? in.speed : 0, dt);
	if (e.type == E_SPIDER && d < 3 && d > 1.2 && e.onGround && e.cool <= 0.3f) e.vy = 5.5;	// (un bond)
	if (d < 1.0 + in.half && fabs (ty - e.y) < 1.6 && e.cool <= 0) {
		e.cool = 1.0f;
		if (victim == -1) {
			if (g.mode == MODE_SURVIVAL) gameHurtPlayer (dmg, in.label, e.x, e.z);
		} else if (Entity *v = es.get (victim))
			es.hurtEntity (*v, dmg, e.x, e.z, e.id);
		if (e.type == E_WOLF) cry (e, audio::S_WOLF, 0, 0.6f);
	}
	(void) w;
}

bool isMonster (const Entity &, const Entity &o) { return INFO[o.type].hostile; }
bool sameInLove (const Entity &me, const Entity &o) { return o.type == me.type && o.love > 0 && o.age >= 0 && (o.type != E_WOLF || o.tamed); }

// les créatures de la nuit et les animaux qui arrivent, ceux qui partent
void populate (Entities &es, World &w, double dt)
{
	static double t = 0;
	t += dt;
	if (t < 1.0) return;
	t = 0;
	float day = std::max (0.0f, std::min (1.0f, sinf ((g.hour - 6.0f) / 12.0f * (float) M_PI) * 2.5f + 0.35f));
	// partir : les créatures de la nuit loin du joueur, ou au jour dehors ; les animaux sauvages très loin
	for (auto &e : es.list) {
		if (e.dead >= 0) continue;
		double d = sqrt (dist2 (e, g.pl.x, g.pl.z));
		if (INFO[e.type].hostile) {
			if (d > 72 || (!es.monsters)) e.dead = 9, e.byPlayer = false;
			else if (e.type == E_SPIDER && day > 0.6f && w.skyLit ((int) floor (e.x), (int) floor (e.y) + 1, (int) floor (e.z)) && d > 20 && frand () < 0.05)
				e.dead = 9, e.byPlayer = false;
		} else if (!e.tamed && d > 128)
			e.dead = 9, e.byPlayer = false;
	}
	// arriver
	auto groundAt = [&] (int x, int z, bool needSky, int &y) {
		if (!loaded (w, x, z)) return false;
		y = w.height (x, z);
		int top = w.get (x, y, z);
		if (!isSolid (top) || top == LEAVES || top == PINE_LEAVES || isLiquid (top)) return false;
		y++;
		if (isSolid (w.get (x, y, z)) || isSolid (w.get (x, y + 1, z)) || isLiquid (w.get (x, y, z))) return false;
		return !needSky || w.skyLit (x, y, z);
	};
	if (es.monsters) {
		int hostile = 0;
		for (auto &e : es.list) hostile += INFO[e.type].hostile && e.dead < 0 && dist2 (e, g.pl.x, g.pl.z) < 64 * 64;
		for (int tries = 0; tries < 3 && hostile < 10; tries++) {
			double a = frange (0, 2 * M_PI), r = frange (18, 40);
			int x = (int) floor (g.pl.x + cos (a) * r), z = (int) floor (g.pl.z + sin (a) * r);
			int y;
			bool ok = false;
			if (day < 0.25f && groundAt (x, z, true, y)) ok = true;	// la nuit, dehors
			else if (loaded (w, x, z)) {				// sous terre, dans le noir
				int top = w.height (x, z);
				for (int k = 0; k < 6 && !ok; k++) {
					y = 5 + rand () % std::max (1, top - 10);
					if (w.get (x, y, z) == AIR && w.get (x, y + 1, z) == AIR && isSolid (w.get (x, y - 1, z)) && !isLiquid (w.get (x, y - 1, z)) &&
					    !w.skyLit (x, y, z) && fabs (y - g.pl.y) < 24)
						ok = true;
				}
			}
			if (!ok) continue;
			int type = frand () < 0.6 ? E_RODEUR : E_SPIDER;
			es.spawn (type, x + 0.5, y, z + 0.5);
			hostile++;
		}
	}
	static double at = 0;
	at += 1;
	if (at >= 6) {
		at = 0;
		int animals = 0;
		for (auto &e : es.list) animals += !INFO[e.type].hostile && e.dead < 0 && dist2 (e, g.pl.x, g.pl.z) < 96 * 96;
		if (animals < 14) {
			double a = frange (0, 2 * M_PI), r = frange (28, 60);
			int x = (int) floor (g.pl.x + cos (a) * r), z = (int) floor (g.pl.z + sin (a) * r), y;
			if (groundAt (x, z, true, y)) {
				int top = w.get (x, y - 1, z);
				int type = top == SNOW ? E_WOLF : top == GRASS ? (frand () < 0.12 ? E_WOLF : frand () < 0.55 ? E_CHICKEN : E_PIG) : -1;
				if (type >= 0) {
					int n = type == E_WOLF ? 2 + rand () % 2 : 2 + rand () % 3;
					for (int i = 0; i < n; i++) {
						int sx = x + rand () % 5 - 2, sz = z + rand () % 5 - 2, sy;
						if (groundAt (sx, sz, false, sy)) es.spawn (type, sx + 0.5, sy, sz + 0.5);
					}
				}
			}
		}
	}
}

} // namespace

void Entities::update (World &w, double dt)
{
	if (dt > 0.1) dt = 0.1;
	populate (*this, w, dt);
	float day = std::max (0.0f, std::min (1.0f, sinf ((g.hour - 6.0f) / 12.0f * (float) M_PI) * 2.5f + 0.35f));
	bool survival = g.mode == MODE_SURVIVAL;
	int held = 0;
	if (survival) held = g.inv.s[g.selected].empty () ? 0 : g.inv.s[g.selected].item;
	for (size_t i = 0; i < list.size (); i++) {
		Entity &e = list[i];
		const EntityInfo &in = INFO[e.type];
		if (!loaded (w, e.x, e.z)) continue;	// (au loin : figé)
		e.hurt = std::max (0.0f, e.hurt - (float) dt);
		e.cool = std::max (0.0f, e.cool - (float) dt);
		if (e.dead >= 0) {
			e.dead += (float) dt;
			e.vx *= 0.9, e.vz *= 0.9;
			continue;
		}
		// grandir, aimer
		if (e.age < 0) e.age = std::min (0.0f, e.age + (float) dt);
		if (e.love > 0) e.love -= (float) dt;
		if (e.breedCool > 0) e.breedCool -= (float) dt;
		e.panic = std::max (0.0f, e.panic - (float) dt);
		e.angry = std::max (0.0f, e.angry - (float) dt);
		// le feu : la lave, le soleil (le rôdeur)
		int feet = w.get ((int) floor (e.x), (int) floor (e.y + 0.1), (int) floor (e.z));
		int body = w.get ((int) floor (e.x), (int) floor (e.y + in.height * 0.6), (int) floor (e.z));
		e.inLava = feet == LAVA || body == LAVA;
		e.inWater = feet == WATER || body == WATER || e.inLava;
		if (e.inLava) e.burn = 4;
		else if (e.inWater) e.burn = 0;
		if (e.type == E_RODEUR && day > 0.5f && !e.inWater && w.skyLit ((int) floor (e.x), (int) floor (e.y + 1.8), (int) floor (e.z))) e.burn = std::max (e.burn, 1.0f);
		if (e.burn > 0) {
			e.burn -= (float) dt;
			e.health -= (float) dt * (e.inLava ? 4.0f : 1.5f);
			if (e.health <= 0) {
				e.dead = 0;
				continue;
			}
		}
		// l'esprit
		double px = g.pl.x, py = g.pl.y, pz = g.pl.z;
		double dp = sqrt (dist2 (e, px, pz));
		if (in.hostile) {
			Entity *t = e.target > 0 ? get (e.target) : nullptr;
			if (t && (t->dead >= 0 || dist2 (e, t->x, t->z) > 24 * 24)) t = nullptr, e.target = 0;
			bool seesPlayer = survival && dp < (e.type == E_SPIDER ? 16 : 20) && fabs (py - e.y) < 8 &&
					  (e.type != E_SPIDER || day < 0.4f || e.target == -1 || !w.skyLit ((int) floor (e.x), (int) floor (e.y) + 1, (int) floor (e.z)));
			if (t) chase (*this, e, w, dt, t->x, t->y, t->z, e.type == E_RODEUR ? 3 : 2, t->id);
			else if (seesPlayer) chase (*this, e, w, dt, px, py, pz, e.type == E_RODEUR ? 3 : 2, -1);
			else wander (e, w, dt, in.speed);
			if (e.type == E_RODEUR && e.burn > 0 && !t && !seesPlayer) wander (e, w, dt, in.speed * 1.5);
		} else if (e.type == E_WOLF && e.tamed) {
			// un loup apprivoisé : défendre (son poste s'il est assis, son maître sinon)
			double cx = e.sitting ? e.hx : px, cz = e.sitting ? e.hz : pz;
			Entity *t = e.target > 0 ? get (e.target) : nullptr;
			if (t && (t->dead >= 0 || dist2 (*t, cx, cz) > 16 * 16)) t = nullptr, e.target = 0;
			if (!t) {
				Entity tmp = e;
				tmp.x = cx, tmp.z = cz;
				t = nearestOf (*this, tmp, isMonster, 12);
				if (t) e.target = t->id;
			}
			if (t) chase (*this, e, w, dt, t->x, t->y, t->z, 4, t->id);
			else if (e.sitting) {
				steer (e, e.hx, e.hz, dist2 (e, e.hx, e.hz) > 1.0 ? in.speed : 0, dt);
				e.walkAmt *= 0.9f;
			} else {
				if (dp > 24 && loaded (w, px, pz)) e.x = px + frange (-1.5, 1.5), e.z = pz + frange (-1.5, 1.5), e.y = py + 0.5, e.vx = e.vz = e.vy = 0;
				steer (e, px, pz, dp > 3 ? in.speed * (dp > 8 ? 1.4 : 1.0) : 0, dt);
			}
		} else if (e.type == E_WOLF && e.angry > 0 && survival && dp < 24) {
			chase (*this, e, w, dt, px, py, pz, 3, -1);
		} else {
			// un animal : fuir s'il a été frappé ; suivre sa nourriture ; trouver un partenaire ; flâner
			int food = e.type == E_CHICKEN ? I_SEEDS : e.type == E_PIG ? I_APPLE : 0;
			Entity *mate = e.love > 0 ? nearestOf (*this, e, sameInLove, 8) : nullptr;
			if (e.panic > 0) {
				double ax = e.x - px, az = e.z - pz, d = sqrt (ax * ax + az * az) + 0.01;
				steer (e, e.x + ax / d * 4, e.z + az / d * 4, in.speed * 1.6, dt);
				if (!safeAhead (w, e, 2)) e.vx = e.vz = 0;
			} else if (mate) {
				steer (e, mate->x, mate->z, in.speed, dt);
				if (dist2 (e, mate->x, mate->z) < 1.3 && e.id < mate->id) {
					Entity *baby = spawn (e.type, (e.x + mate->x) / 2, e.y + 0.2, (e.z + mate->z) / 2);
					Entity &e2 = list[i];	// (spawn peut déplacer la liste)
					Entity *m2 = nullptr;
					for (auto &o : list)
						if (o.love > 0 && o.type == e2.type && o.id != e2.id && dist2 (o, e2.x, e2.z) < 1.3) m2 = &o;
					if (baby) {
						baby->age = -300;
						baby->tamed = e2.type == E_WOLF;
						if (baby->tamed) baby->health = INFO[E_WOLF].health * 2.5f, baby->sitting = false;
					}
					e2.love = 0, e2.breedCool = 300;
					if (m2) m2->love = 0, m2->breedCool = 300;
					cry (e2, voiceOf (e2.type), 0);
					continue;
				}
			} else if (food && held == food && dp < 8 && survival) {
				steer (e, px, pz, dp > 2 ? in.speed : 0, dt);
			} else
				wander (e, w, dt, in.speed);
		}
		// un petit suit un adulte de son espèce
		if (e.age < 0 && !e.tamed && e.panic <= 0) {
			Entity *p = nearestOf (*this, e, [] (const Entity &a, const Entity &b) { return b.type == a.type && b.age >= 0; }, 10);
			if (p && dist2 (e, p->x, p->z) > 9) steer (e, p->x, p->z, in.speed, dt);
		}
		// le corps : la pesanteur, l'eau, sauter les marches, grimper (l'araignée)
		if (e.inWater) {
			e.vy += (e.inLava ? 6.0 : 14.0) * dt;
			if (e.vy > 2.0) e.vy = 2.0;
			e.vx *= 0.92, e.vz *= 0.92;
		}
		e.vy -= 28.0 * dt;
		if (e.vy < -40) e.vy = -40;
		if (e.type == E_CHICKEN && e.vy < -3) e.vy = -3;	// (elle bat des ailes)
		if (e.blocked && e.onGround && (fabs (e.vx) > 0.3 || fabs (e.vz) > 0.3)) e.vy = 8.0;
		if (e.blocked && e.type == E_SPIDER) e.vy = 3.5;
		if (e.sitting && e.tamed) e.vx *= 0.5, e.vz *= 0.5;
		bool wasGround = e.onGround;
		e.onGround = false;
		if (moveAxis (w, e, 1, e.vy * dt)) {
			if (e.vy < 0) e.onGround = true;
			e.vy = 0;
		}
		bool bx = moveAxis (w, e, 0, e.vx * dt), bz = moveAxis (w, e, 2, e.vz * dt);
		if (bx) e.vx *= 0.2;
		if (bz) e.vz *= 0.2;
		e.blocked = bx || bz;
		// la chute (les chats retombent sur leurs pattes, pas les autres ; la poule plane)
		if (wasGround && !e.onGround) e.fallY = e.y;
		if (e.inWater) e.fallY = e.y;
		if (!wasGround && e.onGround) {
			double fall = e.fallY - e.y;
			if (fall > 3.5 && e.type != E_CHICKEN) hurtEntity (e, (float) (fall - 3), e.x, e.z, 0);
			e.fallY = e.y;
		}
		if (e.y > e.fallY) e.fallY = e.y;
		if (e.y < -20) e.dead = 9;
		// les pattes, la tête (vers le joueur proche)
		double sp = sqrt (e.vx * e.vx + e.vz * e.vz);
		e.walk += (float) (sp * dt * 6.0);
		e.walkAmt += ((float) std::min (1.0, sp / 2.0) - e.walkAmt) * (float) std::min (1.0, dt * 8);
		if (dp < 6) {
			float want = (float) (atan2 (px - e.x, -(pz - e.z)) * 180.0 / M_PI) - e.yaw;
			want = fmodf (want + 540.0f, 360.0f) - 180.0f;
			want = std::max (-60.0f, std::min (60.0f, want));
			e.headYaw += (want - e.headYaw) * (float) std::min (1.0, dt * 6);
		} else
			e.headYaw *= 0.95f;
		// les cris
		e.sound -= (float) dt;
		if (e.sound <= 0) {
			e.sound = (float) frange (in.hostile ? 4 : 6, in.hostile ? 10 : 18);
			int arg = 0;
			if (e.type == E_WOLF) arg = e.angry > 0 || e.target ? 1 : day < 0.2f && !e.tamed && frand () < 0.4 ? 3 : 0;
			cry (e, voiceOf (e.type), arg, e.type == E_WOLF && arg == 3 ? 1.0f : 0.7f);
		}
	}
	// se pousser les uns les autres (et le joueur)
	for (size_t i = 0; i < list.size (); i++)
		for (size_t j = i + 1; j < list.size (); j++) {
			Entity &a = list[i], &b = list[j];
			double r = INFO[a.type].half + INFO[b.type].half;
			double dx = b.x - a.x, dz = b.z - a.z, d2 = dx * dx + dz * dz;
			if (d2 < r * r && d2 > 1e-6 && fabs (a.y - b.y) < 1.5) {
				double d = sqrt (d2), push = (r - d) * 0.5 * std::min (1.0, dt * 10);
				a.vx -= dx / d * push * 4, a.vz -= dz / d * push * 4;
				b.vx += dx / d * push * 4, b.vz += dz / d * push * 4;
			}
		}
	// le joueur les pousse aussi
	for (auto &e : list) {
		double r = INFO[e.type].half + 0.3, dx = e.x - g.pl.x, dz = e.z - g.pl.z, d2 = dx * dx + dz * dz;
		if (e.dead < 0 && d2 < r * r && d2 > 1e-6 && e.y < g.pl.y + 1.8 && e.y + INFO[e.type].height > g.pl.y) {
			double d = sqrt (d2);
			e.vx += dx / d * (r - d) * 8, e.vz += dz / d * (r - d) * 8;
		}
	}
	// mourir : ce qui tombe (au joueur, s'il est l'auteur et près), puis plus rien
	for (size_t i = 0; i < list.size ();) {
		Entity &e = list[i];
		if (e.dead >= 0.6f) {
			if (e.byPlayer && e.dead < 5 && survival && distP (e) < 24) {
				bool cooked = e.burn > 0;
				auto give = [&] (int item, int n) {
					if (n > 0) g.inv.add (item, n);
				};
				switch (e.type) {
				case E_CHICKEN: give (cooked ? I_COOKED_CHICKEN : I_RAW_CHICKEN, e.age < 0 ? 0 : 1), give (I_FEATHER, rand () % 3); break;
				case E_PIG: give (cooked ? I_COOKED_PORK : I_RAW_PORK, e.age < 0 ? 0 : 1 + rand () % 3); break;
				case E_RODEUR: give (I_BONE, rand () % 3); break;
				case E_SPIDER: give (I_STRING, rand () % 3); break;
				}
				audio::play (audio::S_PICKUP, 0, 0.6f);
			}
			list.erase (list.begin () + (long) i);
			continue;
		}
		i++;
	}
}

// ---- sauver et relire ---------------------------------------------------------------------------------------------

std::string Entities::save () const
{
	std::string t;
	for (auto &e : list) {
		if (e.dead >= 0 || INFO[e.type].hostile) continue;
		char b[192];
		snprintf (b, sizeof b, "%s %.2f %.2f %.2f %.1f %.1f %d %d %.2f %.2f %.2f %.0f\n", INFO[e.type].name, e.x, e.y, e.z, e.yaw, e.health, e.tamed,
			  e.sitting, e.hx, e.hy, e.hz, e.age);
		t += b;
	}
	return t;
}

void Entities::load (const std::string &text)
{
	list.clear ();
	size_t p = 0;
	while (p < text.size ()) {
		size_t e = text.find ('\n', p);
		std::string l = text.substr (p, e == std::string::npos ? std::string::npos : e - p);
		p = e == std::string::npos ? text.size () : e + 1;
		char name[32];
		double x, y, z, hx, hy, hz;
		float yaw, health, age;
		int tamed, sitting;
		if (sscanf (l.c_str (), "%31s %lf %lf %lf %f %f %d %d %lf %lf %lf %f", name, &x, &y, &z, &yaw, &health, &tamed, &sitting, &hx, &hy, &hz, &age) < 12) continue;
		int type = entityByName (name);
		if (type < 0) continue;
		Entity *n = spawn (type, x, y, z);
		n->yaw = yaw, n->health = health, n->tamed = tamed, n->sitting = sitting, n->hx = hx, n->hy = hy, n->hz = hz, n->age = age;
	}
}

// ---- les corps, en boîtes -----------------------------------------------------------------------------------------

namespace {

enum Anim { A_NONE, A_HEAD, A_LEG_A, A_LEG_B, A_TAIL, A_ARM_A, A_ARM_B, A_SLEG_A, A_SLEG_B };

struct Part {
	float x0, y0, z0, x1, y1, z1;	// en 1/16 de bloc ; x à droite, y en haut, z vers l'arrière (l'avant : -z)
	float px, py, pz;		// le pivot
	int tile, front;		// la tuile (toutes les faces), celle de la face avant (-z ; -1 : la même)
	int anim;
	float roll;			// (degrés, autour de z : les pattes de l'araignée)
	int glow;			// (les yeux qui luisent)
	int show;			// 0 toujours ; 1 apprivoisé seulement (le collier)
};

const Part CHICKEN[] = {
	{ -3, 4, -4, 3, 10, 4, 0, 0, 0, T_CHICKEN_BODY, -1, A_NONE, 0, 0, 0 },
	{ -2, 9, -6, 2, 15, -3, 0, 10, -4, T_CHICKEN_BODY, T_CHICKEN_FACE, A_HEAD, 0, 0, 0 },
	{ -2, 10, -8, 2, 12, -6, 0, 10, -4, T_CHICKEN_LEG, -1, A_HEAD, 0, 0, 0 },
	{ -2, 0, -1, -1, 4, 1, -1.5f, 4, 0, T_CHICKEN_LEG, -1, A_LEG_A, 0, 0, 0 },
	{ 1, 0, -1, 2, 4, 1, 1.5f, 4, 0, T_CHICKEN_LEG, -1, A_LEG_B, 0, 0, 0 },
	{ -4, 6, -3, -3, 10, 3, -3.5f, 10, 0, T_CHICKEN_BODY, -1, A_NONE, 0, 0, 0 },
	{ 3, 6, -3, 4, 10, 3, 3.5f, 10, 0, T_CHICKEN_BODY, -1, A_NONE, 0, 0, 0 },
};
const Part PIG[] = {
	{ -5, 6, -8, 5, 14, 8, 0, 0, 0, T_PIG_SKIN, -1, A_NONE, 0, 0, 0 },
	{ -4, 8, -14, 4, 16, -8, 0, 12, -8, T_PIG_SKIN, T_PIG_FACE, A_HEAD, 0, 0, 0 },
	{ -5, 0, -7, -1, 6, -3, -3, 6, -5, T_PIG_SKIN, -1, A_LEG_A, 0, 0, 0 },
	{ 1, 0, -7, 5, 6, -3, 3, 6, -5, T_PIG_SKIN, -1, A_LEG_B, 0, 0, 0 },
	{ -5, 0, 3, -1, 6, 7, -3, 6, 5, T_PIG_SKIN, -1, A_LEG_B, 0, 0, 0 },
	{ 1, 0, 3, 5, 6, 7, 3, 6, 5, T_PIG_SKIN, -1, A_LEG_A, 0, 0, 0 },
};
const Part WOLF[] = {
	{ -3, 7, -5, 3, 13, 7, 0, 0, 0, T_WOLF_FUR, -1, A_NONE, 0, 0, 0 },
	{ -4, 7, -6, 4, 14, -1, 0, 0, 0, T_WOLF_FUR, -1, A_NONE, 0, 0, 0 },
	{ -3, 9, -10, 3, 15, -6, 0, 12, -6, T_WOLF_FUR, T_WOLF_FACE, A_HEAD, 0, 0, 0 },
	{ -1.5f, 9, -13, 1.5f, 12, -10, 0, 12, -6, T_WOLF_FUR, -1, A_HEAD, 0, 0, 0 },
	{ -3, 15, -8, -1, 17, -7, 0, 12, -6, T_WOLF_FUR, -1, A_HEAD, 0, 0, 0 },
	{ 1, 15, -8, 3, 17, -7, 0, 12, -6, T_WOLF_FUR, -1, A_HEAD, 0, 0, 0 },
	{ -2.5f, 0, -4, -0.5f, 7, -2, -1.5f, 7, -3, T_WOLF_FUR, -1, A_LEG_A, 0, 0, 0 },
	{ 0.5f, 0, -4, 2.5f, 7, -2, 1.5f, 7, -3, T_WOLF_FUR, -1, A_LEG_B, 0, 0, 0 },
	{ -2.5f, 0, 4, -0.5f, 7, 6, -1.5f, 7, 5, T_WOLF_FUR, -1, A_LEG_B, 0, 0, 0 },
	{ 0.5f, 0, 4, 2.5f, 7, 6, 1.5f, 7, 5, T_WOLF_FUR, -1, A_LEG_A, 0, 0, 0 },
	{ -1, 10, 7, 1, 12, 15, 0, 11, 7, T_WOLF_FUR, -1, A_TAIL, 0, 0, 0 },
	{ -4.2f, 8.5f, -6.6f, 4.2f, 13.5f, -5.4f, 0, 0, 0, T_WOLF_COLLAR, -1, A_NONE, 0, 0, 1 },
};
const Part RODEUR[] = {
	{ -4, 0, -2, 0, 12, 2, -2, 12, 0, T_RODEUR_CLOTH, -1, A_LEG_A, 0, 0, 0 },
	{ 0, 0, -2, 4, 12, 2, 2, 12, 0, T_RODEUR_CLOTH, -1, A_LEG_B, 0, 0, 0 },
	{ -4, 12, -2, 4, 24, 2, 0, 0, 0, T_RODEUR_CLOTH, -1, A_NONE, 0, 0, 0 },
	{ -8, 12, -2, -4, 24, 2, -6, 22, 0, T_RODEUR_SKIN, -1, A_ARM_A, 0, 0, 0 },
	{ 4, 12, -2, 8, 24, 2, 6, 22, 0, T_RODEUR_SKIN, -1, A_ARM_B, 0, 0, 0 },
	{ -4, 24, -4, 4, 32, 4, 0, 24, 0, T_RODEUR_SKIN, T_RODEUR_FACE, A_HEAD, 0, 0, 0 },
	{ -3, 28, -4.4f, -1, 29, -4, 0, 24, 0, T_LAMP_ON, -1, A_HEAD, 0, 255, 0 },
	{ 1, 28, -4.4f, 3, 29, -4, 0, 24, 0, T_LAMP_ON, -1, A_HEAD, 0, 255, 0 },
};
const Part SPIDER[] = {
	{ -5, 4, 2, 5, 12, 14, 0, 0, 0, T_SPIDER_BODY, -1, A_NONE, 0, 0, 0 },
	{ -3, 5, -4, 3, 11, 2, 0, 0, 0, T_SPIDER_LEG, -1, A_NONE, 0, 0, 0 },
	{ -4, 4, -10, 4, 12, -4, 0, 8, -4, T_SPIDER_BODY, T_SPIDER_FACE, A_HEAD, 0, 0, 0 },
	{ -3, 9, -10.4f, -1, 10, -10, 0, 8, -4, T_WOOL_RED, -1, A_HEAD, 0, 255, 0 },
	{ 1, 9, -10.4f, 3, 10, -10, 0, 8, -4, T_WOOL_RED, -1, A_HEAD, 0, 255, 0 },
	// les pattes : quatre de chaque côté, penchées vers le sol, qui balaient d'avant en arrière
	{ 3, 7, -3.5f, 17, 9, -1.5f, 3, 8, -2.5f, T_SPIDER_LEG, -1, A_SLEG_A, -38, 0, 0 },
	{ 3, 7, -1.5f, 17, 9, 0.5f, 3, 8, -0.5f, T_SPIDER_LEG, -1, A_SLEG_B, -38, 0, 0 },
	{ 3, 7, 0.5f, 17, 9, 2.5f, 3, 8, 1.5f, T_SPIDER_LEG, -1, A_SLEG_A, -38, 0, 0 },
	{ 3, 7, 2.5f, 17, 9, 4.5f, 3, 8, 3.5f, T_SPIDER_LEG, -1, A_SLEG_B, -38, 0, 0 },
	{ -17, 7, -3.5f, -3, 9, -1.5f, -3, 8, -2.5f, T_SPIDER_LEG, -1, A_SLEG_B, 38, 0, 0 },
	{ -17, 7, -1.5f, -3, 9, 0.5f, -3, 8, -0.5f, T_SPIDER_LEG, -1, A_SLEG_A, 38, 0, 0 },
	{ -17, 7, 0.5f, -3, 9, 2.5f, -3, 8, 1.5f, T_SPIDER_LEG, -1, A_SLEG_B, 38, 0, 0 },
	{ -17, 7, 2.5f, -3, 9, 4.5f, -3, 8, 3.5f, T_SPIDER_LEG, -1, A_SLEG_A, 38, 0, 0 },
};

struct Model {
	const Part *p;
	int n;
};
const Model MODELS[E_TYPE_COUNT] = {
	{ CHICKEN, (int) (sizeof CHICKEN / sizeof CHICKEN[0]) }, { PIG, (int) (sizeof PIG / sizeof PIG[0]) }, { WOLF, (int) (sizeof WOLF / sizeof WOLF[0]) },
	{ RODEUR, (int) (sizeof RODEUR / sizeof RODEUR[0]) }, { SPIDER, (int) (sizeof SPIDER / sizeof SPIDER[0]) },
};

// tourner un point autour d'un pivot : d'abord autour de z (roll), puis de x (pitch), puis de y (yaw) ; degrés
void rotate (float p[3], const float piv[3], float roll, float pitch, float yaw)
{
	float x = p[0] - piv[0], y = p[1] - piv[1], z = p[2] - piv[2];
	if (roll) {
		float c = cosf (roll * (float) M_PI / 180), s = sinf (roll * (float) M_PI / 180);
		float nx = x * c - y * s, ny = x * s + y * c;
		x = nx, y = ny;
	}
	if (pitch) {
		float c = cosf (pitch * (float) M_PI / 180), s = sinf (pitch * (float) M_PI / 180);
		float ny = y * c - z * s, nz = y * s + z * c;
		y = ny, z = nz;
	}
	if (yaw) {
		float c = cosf (yaw * (float) M_PI / 180), s = sinf (yaw * (float) M_PI / 180);
		float nx = x * c - z * s, nz = x * s + z * c;
		x = nx, z = nz;
	}
	p[0] = x + piv[0], p[1] = y + piv[1], p[2] = z + piv[2];
}

} // namespace

void buildEntityMeshes (const Entities &es, const World &w, double cx, double cz, std::vector<EVertex> &out, std::vector<EDraw> &draws)
{
	out.clear (), draws.clear ();
	static const float SHADE[6] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.68f, 0.68f };
	for (auto &e : es.list) {
		if (!loaded (w, e.x, e.z)) continue;
		double dx = e.x - g.pl.x, dz = e.z - g.pl.z;
		if (dx * dx + dz * dz > 96.0 * 96.0) continue;
		const Model &m = MODELS[e.type];
		float scale = e.age < 0 ? 0.6f : 1.0f;
		float sky = w.skyLit ((int) floor (e.x), (int) floor (e.y + 0.5), (int) floor (e.z)) ? 1.0f : 0.0f;
		float light = 0.22f + 0.78f * sky;
		EDraw d;
		d.first = (int) out.size ();
		d.tint[0] = 1, d.tint[1] = e.hurt > 0 || e.dead >= 0 ? 0.45f : 1, d.tint[2] = d.tint[1];
		if (e.burn > 0 && e.type == E_RODEUR) d.tint[1] = std::min (d.tint[1], 0.7f), d.tint[2] = std::min (d.tint[2], 0.5f);
		float swing = sinf (e.walk) * 40.0f * e.walkAmt;
		bool sit = e.tamed && e.sitting && e.target == 0;
		for (int i = 0; i < m.n; i++) {
			const Part &p = m.p[i];
			if (p.show == 1 && !e.tamed) continue;
			float pitch = 0, yaw = 0;
			switch (p.anim) {
			case A_HEAD: yaw = e.headYaw, pitch = e.headPitch; break;
			case A_LEG_A: case A_ARM_B: pitch = swing; break;
			case A_LEG_B: case A_ARM_A: pitch = -swing; break;
			case A_TAIL: pitch = e.tamed ? -40.0f : -15.0f, yaw = e.tamed ? sinf (e.walk * 3 + (float) e.id) * 25.0f : 0; break;
			case A_SLEG_A: yaw = swing * 0.5f; break;
			case A_SLEG_B: yaw = -swing * 0.5f; break;
			}
			if (sit && (p.anim == A_LEG_A || p.anim == A_LEG_B) && p.z0 > 0) pitch = -80;	// (assis : les pattes de derrière repliées)
			float lo[3] = { p.x0, p.y0, p.z0 }, hi[3] = { p.x1, p.y1, p.z1 }, piv[3] = { p.px, p.py, p.pz };
			for (int f = 0; f < 6; f++) {
				int a = f >> 1, s = (f & 1) ? -1 : 1;
				int u = a == 0 ? 1 : a == 1 ? 2 : 0, v = a == 0 ? 2 : a == 1 ? 0 : 1;
				static const int ORD_POS[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
				static const int ORD_NEG[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
				const int (*ord)[2] = s > 0 ? ORD_POS : ORD_NEG;
				int tile = f == 5 && p.front >= 0 ? p.front : p.tile;
				// la face, centrée dans sa tuile (à sa taille, en texels)
				float fw = hi[a == 0 ? 2 : 0] - lo[a == 0 ? 2 : 0], fh = a == 1 ? hi[2] - lo[2] : hi[1] - lo[1];
				fw = std::min (fw, 16.0f), fh = std::min (fh, 16.0f);
				float u0 = (16 - fw) / 2, v0 = (16 - fh) / 2;
				for (int k = 0; k < 4; k++) {
					float c[3];
					c[a] = s > 0 ? hi[a] : lo[a];
					c[u] = ord[k][0] ? hi[u] : lo[u];
					c[v] = ord[k][1] ? hi[v] : lo[v];
					float fu, fv;
					if (a == 0) fu = (s > 0) ? (c[2] > lo[2] ? 0.0f : 1.0f) : (c[2] > lo[2] ? 1.0f : 0.0f), fv = c[1] > lo[1] ? 0.0f : 1.0f;
					else if (a == 2) fu = (s > 0) ? (c[0] > lo[0] ? 1.0f : 0.0f) : (c[0] > lo[0] ? 0.0f : 1.0f), fv = c[1] > lo[1] ? 0.0f : 1.0f;
					else fu = c[0] > lo[0] ? 1.0f : 0.0f, fv = c[2] > lo[2] ? 1.0f : 0.0f;
					rotate (c, piv, p.roll, pitch, yaw);
					// le corps : assis, couché (mourant), petit, tourné, posé
					if (sit) c[1] -= 3;
					float zero[3] = { 0, 0, 0 };
					if (e.dead >= 0) rotate (c, zero, std::min (1.0f, e.dead / 0.4f) * 90.0f, 0, 0);
					for (float &q : c) q *= scale;
					rotate (c, zero, 0, 0, e.yaw);
					EVertex vx;
					vx.x = (float) ((e.x - cx) * 16.0 + c[0]);
					vx.y = (float) (e.y * 16.0 + c[1]);
					vx.z = (float) ((e.z - cz) * 16.0 + c[2]);
					vx.light = p.glow ? 1.0f : light * SHADE[f];
					vx.glow = p.glow / 255.0f;
					vx.u = (float) ((tile & 15) * 256) + (u0 + fu * fw) * 16.0f;
					vx.v = (float) ((tile >> 4) * 256) + (v0 + fv * fh) * 16.0f;
					out.push_back (vx);
				}
			}
		}
		d.quads = ((int) out.size () - d.first) / 4;
		if (d.quads) draws.push_back (d);
	}
}

} // namespace tc
