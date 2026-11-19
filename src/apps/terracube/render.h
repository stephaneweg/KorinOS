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
#include "world.h"

namespace tc {

void buildAtlas (uint8_t *px);	// textures.cpp : 256 × 256 × 4 octets

struct Camera {
	double x, y, z;		// l'œil
	float yaw, pitch;	// degrés : yaw 0 vers -z (le nord), 90 vers +x ; pitch positif vers le haut
	float fov = 70.0f;
};

struct HudState {
	int slots[9];
	int selected;
	bool underwater;
	bool crosshair;
	bool hotbar = true;
	unsigned panel, panelLine, accent, text;	// jetons du thème (0xRRGGBB)
};

struct Frame {
	int w, h;
	float daylight;		// 0 la nuit, 1 le jour
	float sunAngle;		// radians : 0 au lever, π au coucher
	int distance;		// en tronçons
	bool hasTarget;
	Hit target;
};

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
	unsigned progWorld = 0, progSky = 0, progHud = 0, progLine = 0;
	unsigned tex = 0, vao = 0, ibo = 0, hudVbo = 0, lineVbo = 0;
	int iboQuads = 0;
	std::string name;
	// emplacements des uniformes
	int uVp, uOrigin, uCam, uFog0, uFog1, uDay, uFogCol, uAlpha, uTex, uWater, uTime, uCell;
	int sInvVp, sSun, sDay, sZen, sHor;
	int hProj, hTex;
	int lVp, lCol;
	void ensureIndices (int quads);
	void upload (Mesh &m, const std::vector<Vertex> &v);
	void drawHud (const Frame &f, const HudState &hud);
	void drawSelection (const Camera &cam, const Hit &h, const float *vp);
};

// matrices (colonnes d'abord, comme GL)
void matPerspective (float *m, float fovDeg, float aspect, float zn, float zf);
void matMul (float *out, const float *a, const float *b);
bool matInvert (float *out, const float *m);
void viewDir (float yaw, float pitch, float d[3]);

} // namespace tc
