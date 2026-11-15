// agent.h -- Forge : ce qu'un agent (MCP, mcp.cpp) lit et écrit du modèle sans passer par la vue : la topologie d'un
// corps décrite face par face et arête par arête (numéros du maillage, ceux que la vue et les opérations utilisent),
// les mesures, l'export (STEP, IGES, STL, BREP), et l'insertion ou la suppression d'une étape qui garde les renvois des
// autres (esquisse d'une extrusion, étape répétée par un réseau). Sans UIKit : se compile aussi pour Linux (essais).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <string>
#include <vector>
#include "model.h"

namespace forge {

struct FaceDesc {
	int index = 0;
	std::string type;		// plane, cylinder, cone, sphere, torus, bspline, other
	double area = 0;
	double center[3] = { 0, 0, 0 };	// centre de gravité de la face
	bool planar = false;
	double normal[3] = { 0, 0, 0 };	// (plane) vers l'extérieur
	double radius = 0;		// (cylindre, sphère, tore : le grand rayon)
	double axis[3] = { 0, 0, 0 };	// (cylindre, cône) la direction de l'axe
};

struct EdgeDesc {
	int index = 0;
	std::string type;		// line, circle, ellipse, bspline, other
	double length = 0;
	double mid[3] = { 0, 0, 0 }, tangent[3] = { 0, 0, 0 };
	double start[3] = { 0, 0, 0 }, end[3] = { 0, 0, 0 };
	double radius = 0;		// (cercle)
	double center[3] = { 0, 0, 0 };	// (cercle)
};

void describeFaces (const Body &b, std::vector<FaceDesc> &out);
void describeEdges (const Body &b, std::vector<EdgeDesc> &out);

struct Measure {
	double volume = 0, area = 0;
	double com[3] = { 0, 0, 0 };	// centre de gravité
	double lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
	int solids = 0, faces = 0, edges = 0;
	bool valid = false;		// un solide fermé et valide
};
void measureBody (const Body &b, Measure &out);

// format : step, iges, stl, brep -> "" si écrit, sinon l'erreur
std::string exportBodies (const std::vector<const Body *> &bodies, const std::string &path, const std::string &format);

// une étape insérée à « at » ou retirée : les renvois des autres étapes (ref) suivent ; la barre aussi
void insertFeature (Doc &d, int at, const Feature &f);
void eraseFeature (Doc &d, int at);

} // namespace forge
