// render.h -- TerraCube : le rendu par OpenGL ES 3.0 (GlKit : Mesa iris sur le GPU Intel, softpipe sans lui).
//
// Les tronçons sont maillés sur le processeur (mesh.cpp) puis gardés dans des tampons du GPU ; chaque image dessine le
// ciel (un dégradé et le soleil, dans un shader), les tronçons visibles (opaques, puis l'eau mélangée), le contour du
// bloc visé, puis l'affichage tête haute (viseur, barre des blocs). Les positions sont relatives à la case de la
// caméra : pas de perte de précision loin de l'origine.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "entity.h"
#include "world.h"

namespace tc {

void buildAtlas (uint8_t *px);	// textures.cpp : 256 × 256 × 4 octets

struct Camera {
	double x, y, z;		// l'œil
	float yaw, pitch;	// degrés : yaw 0 vers -z (le nord), 90 vers +x ; pitch positif vers le haut
	float fov = 70.0f;
};

// le panneau de l'inventaire et de la fabrication (mode survie), en pixels de la vue (main.cpp, craftDraw)
struct CraftDraw {
	struct Cell {
		int x, y, size;
		int item, count;
		float wear;			// (-1 : pas un outil)
		bool hover, result, selected;
	};
	struct Arrow {
		int x, y, len;			// le départ (milieu de la hampe), la longueur
	};
	int x, y, w, h;			// le panneau
	bool book = false;		// (le livre des recettes : le panneau seul, les textes par UIKit)
	std::vector<Cell> cells;
	std::vector<Arrow> arrows;
	int cursorItem = 0, cursorCount = 0, mx = 0, my = 0;
	float cursorWear = -1;
};

struct HudState {
	int slots[9];			// des objets (items.h) : un bloc, ou une matière, un outil
	int counts[9];			// (-1 : pas de nombre : mode créatif)
	float wear[9];			// usure d'un outil, 0 neuf à 1 cassé (-1 : pas un outil)
	float mining = -1;		// le bloc en train d'être cassé : 0 à 1 (-1 : rien)
	int selected;
	bool underwater;
	bool inLava = false;		// (l'œil dans la lave : tout orange)
	// le mode survie : santé et faim (0 à 20 ; -1 : ne pas les montrer), l'air (0 à 10 bulles ; -1 : rien), un coup reçu (0 à 1)
	float health = -1, hunger = -1, air = -1, hurtFlash = 0;
	bool crosshair;
	bool hotbar = true;
	const CraftDraw *craft = nullptr;	// (le panneau ouvert : pas de barre ni de viseur)
	unsigned sunken = 0, line = 0, hoverCol = 0;
	unsigned panel, panelLine, accent, text, good, warn, bad;	// jetons du thème (0xRRGGBB)
};

struct Frame {
	int w, h;
	float daylight;		// 0 la nuit, 1 le jour
	float sunAngle;		// radians : 0 au lever, π au coucher
	int distance;		// en tronçons
	bool hasTarget;
	Hit target;
	// la météo
	int precip = 0;			// 0 rien, 1 pluie, 2 neige
	float precipLevel = 0;		// 0 à 1
	float gloom = 0;		// le ciel gris (0 à 1)
	float flash = 0;		// un éclair (0 à 1)
	const uint8_t *heights = nullptr;	// 64 × 64 : y + 1 du plus haut bloc opaque (ou feuilles) autour de l'œil
	// les créatures : leurs quads, relatifs à la case de l'œil (floor (cam.x), 0, floor (cam.z)), par créature
	const std::vector<EVertex> *ents = nullptr;
	const std::vector<EDraw> *entDraws = nullptr;
};

const int HMAP = 64;			// (la case de l'œil est la colonne 32, 32)

class Renderer {
public:
	bool init (std::string &err);
	// rend les maillages refaits ce tour (au plus maxMs millisecondes) -> nombre refait
	int updateMeshes (World &w, const Camera &cam, int distance, double maxMs);
	void draw (World &w, const Camera &cam, const Frame &f, const HudState &hud);
	void freeChunk (Chunk *c);
	const char *rendererName () const { return name.c_str (); }
	long drawnChunks = 0, drawnQuads = 0;
private:
	unsigned progWorld = 0, progSky = 0, progHud = 0, progLine = 0, progRain = 0, hmapTex = 0;
	int rU[8];
	unsigned tex = 0, vao = 0, ibo = 0, hudVbo = 0, lineVbo = 0, entVbo = 0;
	int iboQuads = 0;
	std::string name;
	// emplacements des uniformes
	int uVp, uOrigin, uCam, uFog0, uFog1, uDay, uFogCol, uAlpha, uTex, uWater, uTime, uCell, uTint;
	int sInvVp, sSun, sDay, sZen, sHor;
	int hProj, hTex;
	int lVp, lCol;
	void ensureIndices (int quads);
	void upload (Mesh &m, const std::vector<Vertex> &v);
	void drawHud (const Frame &f, const HudState &hud);
	void drawSelection (const Camera &cam, const Hit &h, const float *vp);
	void drawPrecip (const Camera &cam, const Frame &f, const float *vp);
};

// matrices (colonnes d'abord, comme GL)
void matPerspective (float *m, float fovDeg, float aspect, float zn, float zf);
void matMul (float *out, const float *a, const float *b);
bool matInvert (float *out, const float *m);
void viewDir (float yaw, float pitch, float d[3]);

} // namespace tc
