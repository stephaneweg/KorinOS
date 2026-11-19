// entity.h -- TerraCube : les créatures (animaux et créatures de la nuit) : leur corps, leur vie, leur esprit.
//
// Les animaux (poules, cochons, loups) vivent le jour comme la nuit ; on les nourrit pour qu'ils se reproduisent
// (graines pour les poules, pommes pour les cochons), on apprivoise un loup avec des os : il suit son maître, ou,
// assis, garde sa place et attaque les créatures qui s'en approchent. Les créatures de la nuit (le rôdeur, l'araignée)
// apparaissent dans le noir, la nuit dehors ou dans les grottes, et s'en prennent au joueur en mode survie ; le rôdeur
// brûle au soleil. Leurs corps sont des boîtes (des « os » qui tournent : pattes, tête, queue) dessinées à chaque image.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "world.h"

namespace tc {

enum EntityType { E_CHICKEN, E_PIG, E_WOLF, E_RODEUR, E_SPIDER, E_TYPE_COUNT };

struct EntityInfo {
	const char *name;	// nom anglais (scripts, MCP, console)
	const char *label;	// nom montré
	float half, height;	// la boîte (demi-largeur, hauteur), en blocs
	float speed;		// blocs par seconde, en marchant
	float health;		// santé (2 = un cœur)
	bool hostile;		// une créature de la nuit
};

const EntityInfo &entityInfo (int type);
int entityByName (const char *s);	// -> -1

struct Entity {
	int id = 0, type = 0;
	double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
	float yaw = 0, headYaw = 0, headPitch = 0;	// degrés (le corps : 0 vers le nord, comme le joueur)
	float health = 1;
	bool onGround = false, inWater = false, inLava = false, blocked = false;
	float walk = 0, walkAmt = 0;	// les pattes : la phase, l'amplitude
	float hurt = 0;			// (s : rouge)
	float cool = 0;			// (s avant le prochain coup)
	float burn = 0;			// (s de feu)
	float panic = 0;		// (s : un animal frappé s'enfuit)
	float angry = 0;		// (s : un loup sauvage frappé attaque)
	int target = 0;			// une créature visée (son id) ; -1 : le joueur
	int lastAttacker = 0;		// (qui l'a frappé : id, -1 le joueur)
	double gx = 0, gz = 0;		// où il va en flânant
	bool hasGoal = false;
	float think = 0;		// (s avant de choisir un autre but)
	bool tamed = false, sitting = false;
	double hx = 0, hy = 0, hz = 0;	// le poste d'un loup assis
	float age = 0;			// < 0 : petit (les secondes avant d'être grand)
	float love = 0, breedCool = 0;	// (s)
	float sound = 0;		// (s avant son prochain cri)
	float dead = -1;		// >= 0 : mourant depuis tant de secondes
	bool byPlayer = false;		// (tué par le joueur ou son loup : ce qui tombe va au joueur)
	double fallY = 0;		// (le haut de la chute en cours)
};

struct Entities {
	std::vector<Entity> list;
	int nextId = 1;
	bool monsters = true;		// les créatures de la nuit apparaissent (console : mobs on|off)

	void clear () { list.clear (); }
	Entity *spawn (int type, double x, double y, double z);
	Entity *get (int id);
	// avance de dt secondes : l'esprit, le corps, les apparitions et disparitions
	void update (World &w, double dt);
	// le premier corps sur le rayon (au plus maxDist) -> son id (0 : aucun) ; dist : la distance
	int pick (const float eye[3], const float dir[3], float maxDist, float &dist) const;
	// frapper : dmg, d'où vient le coup (le recul), qui frappe (-1 le joueur, sinon une créature)
	void hurtEntity (Entity &e, float dmg, double fx, double fz, int attacker);
	// un clic droit sur une créature avec cet objet en main -> vrai s'il a servi (nourrir, apprivoiser, asseoir…) ;
	// used : l'objet est consommé (un)
	bool interact (Entity &e, int held, bool &used, std::string &msg);
	// une explosion : les créatures autour sont frappées et repoussées
	void blast (double x, double y, double z, float power);
	// sauver et relire (les animaux et les loups apprivoisés ; pas les créatures de la nuit)
	std::string save () const;
	void load (const std::string &text);
	int count (int type, double radius) const;	// (type -1 : toutes ; autour du joueur)
};

// le corps des créatures, pour le rendu : des quads (4 sommets), en 1/16 de bloc, relatifs à la case (cx, 0, cz)
struct EVertex {
	float x, y, z, light, glow, u, v;
};
struct EDraw {
	int first, quads;
	float tint[3];
};
void buildEntityMeshes (const Entities &es, const World &w, double cx, double cz, std::vector<EVertex> &v, std::vector<EDraw> &d);

} // namespace tc
