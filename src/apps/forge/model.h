// model.h -- Forge : le document (corps OpenCASCADE, étapes de l'historique), le maillage pour la vue et les mesures
// d'une face, d'une arête ou d'un corps (docs/DESIGN-CAO.md). Sans UIKit : se compile aussi pour Linux (essais).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <string>
#include <vector>
#include <TopoDS_Shape.hxx>

namespace forge {

// le maillage d'un corps : des triangles par face (indices dans pts), des polylignes par arête ; les numéros de
// face et d'arête sont ceux de TopExp::MapShapes (1..n), rangés ici à partir de 0
struct Mesh {
	std::vector<float> pts;		// x, y, z
	std::vector<float> nrm;		// une normale par point
	std::vector<int> tris;		// trois indices par triangle
	std::vector<int> triFace;	// la face de chaque triangle
	std::vector<int> faceFirst;	// premier triangle de chaque face (+ un de plus à la fin)
	std::vector<float> edgePts;	// x, y, z des polylignes des arêtes, à la suite
	std::vector<int> edgeFirst;	// premier point de chaque arête (+ un de plus à la fin)
	float lo[3], hi[3];		// boîte englobante
	int nfaces = 0, nedges = 0;
};

struct Body {
	std::string name;
	TopoDS_Shape shape;
	int color = 0;			// rang dans la palette du document (forge::bodyColor)
	bool visible = true;
	Mesh mesh;
};

// une étape de l'historique (la frise) ; kind : le genre, pour son dessin
enum StepKind { ST_BOX, ST_CYLINDER, ST_SKETCH, ST_EXTRUDE, ST_FILLET, ST_CHAMFER, ST_UNION, ST_CUT, ST_HOLE };
struct Step {
	StepKind kind;
	std::string name, detail;
};

struct Doc {
	std::string name = "Sans titre";
	std::vector<Body> bodies;
	std::vector<Step> steps;
};

// l'équerre de 3DForge (plaque 90 × 60 × 8 aux coins R 8, paroi de 37 mm, raccord R 6, coins de paroi R 12, deux
// lamages, un trou Ø 22, un oblong), faite par OpenCASCADE avec son historique -> faux si une opération échoue
bool buildSample (Doc &d, std::string &err);

// le maillage d'un corps (flèche en mm) -> nombre de triangles
int meshBody (Body &b, double deflection);

// ce qu'on sait d'un élément choisi : un titre, des lignes « nom \t valeur »
struct Info {
	std::string title, sub;
	std::vector<std::pair<std::string, std::string>> rows;
	bool solidOk = false;		// (corps) un solide fermé et valide
};
void faceInfo (const Body &b, int face, Info &out);
void edgeInfo (const Body &b, int edge, Info &out);
void bodyInfo (const Body &b, Info &out);

// la palette des corps (comme 3DForge : cinq teintes douces) ; dark : la variante d'Onyx Noir
unsigned bodyColor (int i, bool dark);

} // namespace forge
