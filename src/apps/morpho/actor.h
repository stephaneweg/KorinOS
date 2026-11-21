//
// actor.h -- Morpho : l'animal sur son socle. Ce qu'il fait (flâner, regarder, manger, jouer à la balle, dormir,
// bouder, se faire câliner, éclore, se métamorphoser) devient une pose (creature.h) à chaque image ; les objets
// (nourriture, balle, crottes, pilule, bulles, coquille) et les particules 2D (cœurs, étoiles, Zzz) qui vont avec.
//
#ifndef MORPHO_ACTOR_H
#define MORPHO_ACTOR_H
#include "pet.h"
#include "creature.h"

namespace mo {

enum Act { A_IDLE, A_EAT, A_REFUSE, A_PLAY, A_HAPPY, A_MEDICINE, A_BATH, A_CARESS, A_EVOLVE, A_HATCH, A_GONE };

// une particule de l'interface, accrochée à un point du monde
enum PKind { P_HEART, P_STAR, P_Z, P_NOTE, P_DROP, P_STINK, P_BUBBLE, P_CRUMB, P_SHELL, P_SPARK };
struct Particle { int kind; V3 p, v; float life, max, size, spin; unsigned col; };

struct Poop { V3 p; float age, scale; };
enum { SND_HAPPY, SND_SAD, SND_EAT, SND_BOUNCE, SND_CATCH, SND_POP, SND_EVOLVE, SND_HATCH, SND_YUCK, SND_SPLASH, SND_CLEAN, SND_PURR, SND_CRACK,
       SND_YUM, SND_REFUSE, SND_MISS };

struct Actor
{
	V3 pos; float yaw = 0;
	V3 target; bool walking = false;
	float speedNow = 0;
	float t = 0;
	int act = A_IDLE; float actT = 0;
	float nextThink = 1, blinkT = 0, nextBlink = 2;
	float lookYaw = 0, lookPitch = 0;	// tête (lissée)
	V3 lookAt;				// ce qu'il regarde
	float lookCamFor = 0;
	float moodS = 0.5f, lieS = 0, sickS = 0, wagS = 0, flapS = 0, blinkS = 0;
	// la métamorphose
	float morph = -1;			// 0..1 pendant, -1 sinon
	Genome from, to;
	bool skinDirty = true; float skinT = 0;
	// manger
	int food = 0; float foodLeft = 0; V3 foodAt;
	// la balle
	bool ballOn = false, ballHeld = false; V3 ball, ballV; float ballIdle = 0, ballSpin = 0; int catches = 0;
	int misses = 0, ballBounces = 0; float playT = 0, holdT = 0, jumpT = -1, jumpH = 0; bool jumpHit = false, jumpDone = false;
	// le reste
	std::vector<Poop> poops;
	std::vector<Particle> parts;
	bool stroking = false; float strokeT = 0;
	float hatchCrack = 0;
	Pose pose;
	CreatureOut out;
	Genome shown;				// le génome dessiné (le mélange pendant la métamorphose)
	PartScale ps;
	float flash = 0;			// éclair blanc (métamorphose, éclosion)
	int snd[8]; int nsnd = 0;		// les sons demandés pendant l'image (SND_*)
	void sound (int s) { if (nsnd < 8) snd[nsnd++] = s; }

	void reset (const Pet &p);
	void start (int a, Pet &p);
	void update (float dt, Pet &p, const V3 &camEye, const V3 *cursor);
	void build (CreatureMeshes &M, Mesh &props, Mesh &propsSheets, Mesh &propsAlpha);
	void emit (int kind, const V3 &at, int n, unsigned col, float spread = 0.2f);
	void throwBall (const V3 &from, const V3 &to);
	bool hitBall (const V3 &rayO, const V3 &rayD) const;
	void smackBall (const V3 &rayD);
	bool clickPoop (const V3 &rayO, const V3 &rayD);	// une crotte touchée : enlevée
	bool hitPet (const V3 &rayO, const V3 &rayD) const;
};

}	// namespace mo
#endif
