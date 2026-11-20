//
// scene.h -- Morpho : la scène 3D (le studio : fond sombre, socle, créature, objets, ombre) et son dessin par gk_draw : par le GPU quand il
// est là (V3D du Pi, Intel du PC), sinon au processeur, alors à demi-taille quand l'image coûte trop, puis agrandie.
//
#ifndef MORPHO_SCENE_H
#define MORPHO_SCENE_H
#include "creature.h"
#include "render.h"

namespace mo {

struct GkTex { uint32_t *px; int w, h, id, pad; };
struct GkFrame { uint32_t *px; int w, h, stride; unsigned clear, flags, pad; };

struct Scene
{
	Mesh shadow, props, propsSheets, propsAlpha;
	CreatureMeshes cm;
	std::vector<uint32_t> skin, shadowTex, low;
	GkTex tex[TEX_N];
	DrawList dl;
	Camera cam;
	Light light;
	bool gpu = false;		// gk_draw rend par le GPU
	int half = 0;			// au processeur : 1 = demi-taille
	bool fixedSize = false;		// (essais : pas de passage automatique)
	float lastMs = 0;		// le temps du dernier rendu 3D
	V3 shadowAt; float shadowR = 0.5f, shadowK = 1;
	V3 focus, ringC; float ringR = 0.8f;	// le halo (derrière l'animal), le socle

	void init ();
	void setSkin (const uint32_t *px);	// SKIN_W × SKIN_H (envoyée au GPU)
	void begin () { cm.body.clear (); cm.parts.clear (); cm.sheets.clear (); cm.alpha.clear (); props.clear (); propsSheets.clear (); propsAlpha.clear (); }
	// dessine le fond du studio puis la 3D dans px (w × h, pas en pixels)
	void draw (uint32_t *px, int w, int h, int stride, float t);
};

}	// namespace mo
#endif
