//
// pet.h -- Morpho : la vie de l'animal. Cinq besoins (satiété, bonheur, énergie, propreté, santé) qui baissent avec
// le temps, même app fermée ; les soins (manger, jouer, laver, soigner, dormir, câliner) ; la maladie, les crottes, le
// poids ; six stades (œuf, bébé, enfant, adolescent, adulte, ancien), chacun tiré au hasard à son arrivée d'après le
// précédent et les soins reçus ; à la fin, un œuf pour la génération suivante. La sauvegarde : HOME:/morpho/pet.ini.
//
#ifndef MORPHO_PET_H
#define MORPHO_PET_H
#include "genome.h"

namespace mo {

enum Need { N_FOOD, N_FUN, N_ENERGY, N_CLEAN, N_HEALTH, N_COUNT };
enum Food { FOOD_MEAL, FOOD_FRUIT, FOOD_TREAT, FOOD_COUNT };
// ce que fait un soin (pour l'animation et les messages)
enum Result { RS_OK, RS_REFUSED, RS_ASLEEP, RS_NOTHING, RS_TOOTIRED, RS_FULL, RS_CURED };
// les évènements que l'interface montre
enum Event { EV_NONE, EV_HATCH, EV_EVOLVE, EV_SICK, EV_POOP, EV_SLEEP, EV_WAKE, EV_GONE, EV_HUNGRY, EV_BORED, EV_TIRED };

struct Pet
{
	char name[32];
	Genome g, prev;				// le génome, celui d'avant (la métamorphose)
	int generation;
	uint32_t lineage;			// la graine de la lignée
	double age, stageAge;			// secondes simulées depuis la ponte, dans le stade
	int64_t lastReal;			// la dernière mise à jour (secondes, horloge du système)
	int speed;				// 1, 10, 60 : le rythme
	float st[N_COUNT];
	float fat;				// 0..1 (0.3 : normal)
	int poops; float poopIn;		// crottes au sol ; la prochaine dans (s, 0 : rien de prévu)
	bool sick; int meds;			// malade, doses qu'il faut encore
	bool asleep, lampOff;
	double careSum; float careN;		// la moyenne des soins pendant le stade
	int mistakes;				// besoins laissés au plus bas
	float lowFor[N_COUNT];
	float zeroHealthFor;			// santé à zéro depuis (s)
	float treats;				// friandises récentes
	bool gone;				// parti : il reste un œuf à faire éclore
	int hist[ST_COUNT]; uint32_t histSeed[ST_COUNT];
	float alarmIn[N_COUNT];			// (pour ne pas répéter les messages)

	void reset (uint32_t lineageSeed, int gen, const Genome *parent);
	// le temps qui passe (secondes simulées) ; offline : rattrapage app fermée (plus doux) ; events : file à remplir
	void step (float dt, float hour, bool offline, int *events, int &nev);
	// rattrape le temps réel depuis lastReal (au lancement) -> secondes simulées rattrapées
	double catchUp (int64_t now, float hour);
	float stageLen () const;
	float stageProgress () const { return (float) (stageAge / stageLen ()); }
	float care () const { return careN > 0 ? (float) (careSum / careN) : 0.7f; }
	float mood () const;			// -1..1
	int worst () const;			// le besoin le plus pressant (-1 : aucun)

	Result feed (int food);
	Result play ();
	Result playCatch ();			// une balle attrapée
	Result clean ();
	Result heal ();
	Result caress (float dt);
	void lamp ();
	Result wake ();
	bool canAct () const { return !gone && g.stage != ST_EGG; }

	bool save (const char *path) const;
	bool load (const char *path);
};

void randomName (uint32_t seed, char *out, int cap);
void speciesName (const Genome &g, char *out, int cap);
// ce qui a changé d'un stade au suivant (lignes séparées par '\n', traduites)
void evolutionText (const Genome &a, const Genome &b, char *out, int cap);
const char *stageName (int stage);
const char *traitName (int trait);

}	// namespace mo
#endif
