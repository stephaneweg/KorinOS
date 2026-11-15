// model.h -- Forge : le document (un historique d'opérations rejoué en corps OpenCASCADE), le maillage pour la vue et
// les mesures (docs/DESIGN-CAO.md). Sans UIKit : se compile aussi pour Linux (essais).
//
// L'historique est la seule vérité : les corps sont le résultat du rejeu des opérations jusqu'à la barre de
// reconstruction. Une opération qui désigne des arêtes ou une face les garde par leur signature géométrique (point,
// direction) et les retrouve après le rejeu ; si rien ne correspond assez, elle passe en erreur et le rejeu continue
// sans elle (docs/DESIGN-CAO.md §12, le nommage topologique).
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

// ---- l'historique ------------------------------------------------------------------------------------------------

enum StepKind { ST_BOX, ST_CYLINDER, ST_SKETCH, ST_EXTRUDE, ST_FILLET, ST_CHAMFER, ST_UNION, ST_CUT, ST_HOLE, ST_PUSH, ST_SLOT };
enum Op { OP_NEW, OP_JOIN, OP_CUT, OP_INTERSECT };

// un repère : origine, normale (Z), direction X
struct Frame {
	double o[3] = { 0, 0, 0 }, n[3] = { 0, 0, 1 }, x[3] = { 1, 0, 0 };
};

// une arête retrouvée par son milieu et sa tangente au milieu ; une face plane par un de ses points et sa normale
struct EdgeRef { double mid[3], tan[3]; };
struct FaceRef { double pt[3], n[3]; };

struct Feature {
	StepKind kind = ST_BOX;
	std::string name;
	Op op = OP_NEW;			// ce que la forme fait au corps visé
	int body = -1;			// le corps visé (rang de création ; -1 : un nouveau corps)
	int tool = -1;			// (union, soustraction) le corps outil
	Frame frame;			// boîte, cylindre, perçage, oblong : le plan de base et son centre
	double a = 0, b = 0, c = 0;	// boîte : largeur, profondeur, hauteur (signée) ; cylindre : rayon, -, hauteur ;
					// congé, chanfrein : rayon ; pousser/tirer : distance (signée) ; oblong : longueur, largeur
	double d1 = 0, d2 = 0, dep2 = 0;// perçage : diamètre, diamètre et profondeur du lamage
	std::vector<double> pos;	// perçage : centres x, y dans le repère (deux par trou)
	std::vector<EdgeRef> edges;	// congé, chanfrein
	FaceRef face;			// pousser/tirer
	bool off = false;		// désactivée
	std::string error;		// l'erreur du dernier rejeu ("" : bonne)
};

struct Doc {
	std::string name = "Sans titre";
	std::vector<Feature> feats;
	int bar = 0;			// la barre de reconstruction : les opérations jouées (0..feats.size ())
	std::vector<Body> bodies;	// le résultat du rejeu (un corps par opération « nouveau corps »)
	std::vector<int> bodyOf;	// pour chaque corps, l'opération qui l'a créé
};

// rejoue l'historique jusqu'à la barre (les corps et leurs maillages) -> nombre d'opérations en erreur
int replay (Doc &d, double deflection = 0.05);
// la forme qu'ajouterait ou retirerait une opération (boîte, cylindre, pousser/tirer...), sans l'appliquer (fantôme)
TopoDS_Shape featureTool (const Doc &d, const Feature &f);
// une étiquette courte des valeurs d'une opération (« 90 × 60 × 8 », « R 6 · 1 arête »)
std::string featureDetail (const Feature &f);
// le nom par défaut d'une nouvelle opération de ce genre (« Boîte 3 »)
std::string featureName (const Doc &d, StepKind k);
const char *opName (Op o);

// l'équerre de 3DForge en huit opérations (plaque, congé R 8, paroi jointe, congé R 12, raccord R 6, trou Ø 22,
// deux lamages, oblong)
void sampleDoc (Doc &d);

// les références d'une arête et d'une face plane d'un corps (numéros du maillage) ; faux : pas plane
EdgeRef edgeRef (const Body &b, int edge);
bool faceRef (const Body &b, int face, FaceRef &out);
// le plan d'une face plane, normale vers l'extérieur ; faux : la face n'est pas plane
bool facePlane (const Body &b, int face, Frame &out);

// ---- la vue et les mesures -----------------------------------------------------------------------------------------

// le maillage d'un corps (flèche en mm) -> nombre de triangles
int meshBody (Body &b, double deflection);
// le maillage d'une forme quelconque (le fantôme d'une opération)
int meshShape (const TopoDS_Shape &s, Mesh &m, double deflection);

struct Info {
	std::string title, sub;
	std::vector<std::pair<std::string, std::string>> rows;
	bool solidOk = false;		// (corps) un solide fermé et valide
};
void faceInfo (const Body &b, int face, Info &out);
void edgeInfo (const Body &b, int edge, Info &out);
void bodyInfo (const Body &b, Info &out);
double bodyVolume (const Body &b);

// la palette des corps (comme 3DForge : cinq teintes douces) ; dark : la variante d'Onyx Noir
unsigned bodyColor (int i, bool dark);

// une valeur tapée : nombres, + - * / ( ), unités mm cm m in ° ; virgule ou point -> faux si illisible
bool evalExpr (const char *s, double &out);
std::string fmtNum (double v);	// 12,5 ; 90

} // namespace forge
