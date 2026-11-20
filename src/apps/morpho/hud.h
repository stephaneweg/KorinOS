//
// hud.h -- Morpho : l'interface 2D par-dessus la scène, aux couleurs du thème (jetons de UIKit) : la carte de l'animal
// et ses jauges, la barre des soins (icônes dessinées), le choix du repas, la fiche de l'animal, les messages, la bulle
// au-dessus de sa tête, les cartes des grands moments (éclosion, métamorphose, départ), les particules (cœurs, Zzz…).
//
#ifndef MORPHO_HUD_H
#define MORPHO_HUD_H
#include "actor.h"
#include "render.h"
#include "../../inc/gameart.h"

namespace mo {

enum HudAct { H_NONE, H_FEED, H_PLAY, H_CLEAN, H_HEAL, H_LAMP, H_CARESS, H_INFO, H_MEAL, H_FRUIT, H_TREAT, H_CLOSE, H_RENAME, H_CARD, H_NEWEGG, H_WAKE, H_COUNT };
enum Card { C_NONE, C_WELCOME, C_HATCHED, C_EVOLVED, C_GONE, C_BACK };

struct Hud
{
	int w = 0, h = 0; float k = 1;
	int hover = H_NONE;
	bool feedOpen = false, infoOpen = false;
	struct Toast { char text[160]; float t, life; int icon; };
	Toast toasts[4]; int nt = 0;
	int card = C_NONE; char cardTitle[128] = "", cardText[512] = ""; float cardT = 0; int cardIcon = 0;
	struct Hit { int act; int x, y, w, h; bool on; };
	Hit hits[32]; int nh = 0;
	float t = 0;

	void toast (const char *s, int icon = -1);
	void showCard (int c, const char *title, const char *text);
	void update (float dt);
	void draw (ga::Img &im, const Pet &p, const Actor &a, const Camera &cam, const Light &L);
	int hit (int x, int y) const;		// -> HudAct
	bool over (int x, int y) const;		// sur un élément de l'interface (pas la scène)
};

void formatAge (double sec, char *out, int cap);
void formatDur (double sec, char *out, int cap);

}	// namespace mo
#endif
