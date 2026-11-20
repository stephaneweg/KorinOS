//
// creature.h -- Morpho : la créature en 3D, construite à chaque image depuis son génome et sa pose (l'animation) ;
// l'œuf ; la texture de la peau.
//
#ifndef MORPHO_CREATURE_H
#define MORPHO_CREATURE_H
#include "genome.h"
#include "mesh.h"

namespace mo {

// la taille des pièces pendant une métamorphose (0 : disparue, 1 : entière)
struct PartScale { float horn = 1, ear = 1, back = 1, wing = 1, tip = 1, legs = 1, arms = 1, eyes = 1, mouth = 1, brows = 1, heads = 1, shell = 1; };

struct Pose
{
	float t = 0;				// secondes (les cycles)
	float breathe = 1;			// amplitude du souffle
	float headYaw = 0, headPitch = 0, headRoll = 0;
	V3 gaze = V3 (0, 0.6f, 4);		// le point regardé (monde)
	float blink = 0;			// paupières : 0 ouvertes, 1 fermées
	float lidHappy = 0;			// yeux fermés en arc (rire)
	float mouthOpen = 0;
	float mood = 0.5f;			// -1 triste, 1 joyeux
	float anger = 0;			// sourcils froncés
	float wag = 0;				// la queue frétille
	float walkPhase = 0, walkAmt = 0;
	float hop = 0;				// saut (hauteur, monde)
	float squash = 1;			// 1 : rien ; < 1 écrasé ; > 1 étiré
	float wave = 0;				// un bras salue
	float reach = 0;			// les bras (ou la tête) vers la nourriture
	float flap = 0;				// les ailes : 0 repliées, 1 battent
	float shiver = 0;			// malade : tremble
	float lie = 0;				// couché (dort)
	float sick = 0;				// teint verdâtre
	float pupil = 1;			// taille des pupilles
	float dirt = 0;				// sale : plus terne
	V3 pos; float yaw = 0;			// place dans le monde
	PartScale ps;
};

// ce que la scène veut savoir après la construction (monde)
struct CreatureOut
{
	V3 head, top, mouth, center;		// la tête, au-dessus de la tête, la bouche, le centre du corps
	float radius = 0.5f;			// pour l'ombre et la caméra
	float footprint = 0.5f;
};

// les maillages d'une créature : corps texturé, pièces, feuilles (deux faces), transparences
struct CreatureMeshes { Mesh body, parts, sheets, alpha; };

enum { SKIN_W = 256, SKIN_H = 128 };
void makeSkin (const Genome &g, uint32_t *px);		// 0xAARRGGBB, SKIN_W × SKIN_H
void makeShell (const Genome &g, uint32_t *px);
void buildCreature (const Genome &g, const Pose &P, CreatureMeshes &M, CreatureOut &out);
void buildEgg (const Genome &g, float wobble, float crack, float t, const V3 &pos, CreatureMeshes &M, CreatureOut &out);

}	// namespace mo
#endif
