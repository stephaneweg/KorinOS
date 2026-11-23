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

enum StepKind { ST_BOX, ST_CYLINDER, ST_SKETCH, ST_EXTRUDE, ST_FILLET, ST_CHAMFER, ST_COMBINE, ST_CUT, ST_HOLE, ST_PUSH, ST_SLOT, ST_REVOLVE,
	ST_SHELL, ST_PATTERN, ST_MIRROR };	// (ST_CUT : l'ancienne soustraction, rejouée comme Combiner)
enum Op { OP_NEW, OP_JOIN, OP_CUT, OP_INTERSECT };

// un repère : origine, normale (Z), direction X
struct Frame {
	double o[3] = { 0, 0, 0 }, n[3] = { 0, 0, 1 }, x[3] = { 1, 0, 0 };
};

// une arête retrouvée par son milieu et sa tangente au milieu ; une face plane par un de ses points et sa normale
struct EdgeRef { double mid[3], tan[3]; };
struct FaceRef { double pt[3], n[3]; };

// ---- l'esquisse (sketch.cpp) : des points, des éléments qui les partagent (deux lignes qui se suivent ont le même
// point : la coïncidence va de soi), des contraintes résolues par PlaneGCS --------------------------------------------

enum SkGeoType { SG_LINE, SG_CIRCLE, SG_ARC };
struct SkPoint { double x = 0, y = 0; };
struct SkGeo {
	SkGeoType t = SG_LINE;
	int a = -1, b = -1;		// ligne : ses deux points ; arc : son début et sa fin
	int c = -1;			// cercle, arc : le centre
	double r = 0, a0 = 0, a1 = 0;	// rayon ; arc : angles de début et de fin (radians, dans le sens direct)
	bool cons = false;		// de construction : ne fait pas le profil (un axe de révolution)
};
enum SkConsType { SC_HORIZ, SC_VERT, SC_COINCIDENT, SC_PARALLEL, SC_PERP, SC_TANGENT, SC_EQUAL, SC_DIST, SC_RADIUS,
	SC_POINT_ON, SC_CONCENTRIC, SC_DIST_X, SC_DIST_Y };
struct SkCons {
	SkConsType t = SC_HORIZ;
	int g1 = -1, g2 = -1;		// éléments
	int p1 = -1, p2 = -1;		// points
	double v = 0;			// cote (mm) ; SC_RADIUS : le rayon
};
struct Sketch {
	Frame plane;			// le plan ; le point 0 est son origine, fixe
	std::vector<SkPoint> pts { SkPoint () };
	std::vector<SkGeo> geos;
	std::vector<SkCons> cons;
	// le dernier calcul : degrés de liberté, contraintes en conflit ou redondantes (rangs), éléments encore libres
	int dof = -1;
	std::vector<int> conflict, redundant;
	std::vector<char> free;		// par élément : il peut encore bouger
	bool failed = false;
	int body = -1;			// le corps dont une face porte l'esquisse (-1 : un plan de base)
};

struct Feature {
	StepKind kind = ST_BOX;
	std::string name;
	Op op = OP_NEW;			// ce que la forme fait au corps visé
	int body = -1;			// le corps visé (rang de création ; -1 : un nouveau corps)
	std::vector<int> tools;		// (combiner) les corps outils
	bool keep = false;		// (combiner) les outils restent
	Frame frame;			// boîte, cylindre, perçage, oblong : le plan de base et son centre
	double a = 0, b = 0, c = 0;	// boîte : largeur, profondeur, hauteur (signée) ; cylindre : rayon, -, hauteur ;
					// congé, chanfrein : rayon ; pousser/tirer : distance (signée) ; oblong : longueur, largeur
	double d1 = 0, d2 = 0, dep2 = 0;// perçage : diamètre, diamètre et profondeur du lamage (c : profondeur, 0 : traversant)
	std::vector<double> pos;	// perçage : centres x, y dans le repère (deux par trou)
	std::vector<EdgeRef> edges;	// congé, chanfrein
	FaceRef face;			// pousser/tirer
	Sketch sk;			// esquisse
	int ref = -1;			// extrusion, révolution : l'étape de l'esquisse ; réseau : l'étape répétée
	std::vector<FaceRef> faces;	// coque : les faces retirées ; perçage : sa face (le repère la suit au rejeu)
	int count = 0, mode = 0;	// réseau : nombre (l'original compris), 0 linéaire (frame.n : direction, a : pas)
					// ou 1 circulaire (frame : axe, a : angle total) ; symétrie : frame = le plan
	std::vector<std::pair<int, std::string>> expr;	// valeurs données par une formule : rang du champ, formule
	bool off = false;		// désactivée
	std::string error;		// l'erreur du dernier rejeu ("" : bonne)
};

struct Doc {
	std::string name = "Sans titre";
	std::vector<Feature> feats;
	int bar = 0;			// la barre de reconstruction : les opérations jouées (0..feats.size ())
	std::vector<Body> bodies;	// le résultat du rejeu (un corps par opération « nouveau corps »)
	std::vector<int> bodyOf;	// pour chaque corps, l'opération qui l'a créé
	std::vector<std::pair<std::string, std::string>> params;	// paramètres nommés : nom, formule (« épaisseur », « 4 »)
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
// un texte de l'interface dans la langue du système (main.cpp : ui_tr ; la clé est le texte français)
const char *trUi (const char *s);

// l'équerre de 3DForge en huit opérations (plaque, congé R 8, paroi jointe, congé R 12, raccord R 6, trou Ø 22,
// deux lamages, oblong)
void sampleDoc (Doc &d);

// les références d'une arête et d'une face plane d'un corps (numéros du maillage) ; faux : pas plane
EdgeRef edgeRef (const Body &b, int edge);
bool faceRef (const Body &b, int face, FaceRef &out);
// le plan d'une face plane, normale vers l'extérieur ; faux : la face n'est pas plane
bool facePlane (const Body &b, int face, Frame &out);

// ---- l'esquisse ----

// résout l'esquisse (PlaneGCS, DogLeg) et la diagnostique -> vrai si résolue ; les points et rayons suivent
bool skSolve (Sketch &s);
// les faces de ses contours fermés (les petits dans les grands font des trous) -> vide si aucun contour fermé
TopoDS_Shape skFaces (const Sketch &s, std::string &err);
// un point de l'esquisse dans l'espace, et l'inverse (projeté sur le plan)
void skWorld (const Sketch &s, double x, double y, double out[3]);
void skLocal (const Sketch &s, const double p[3], double &x, double &y);
// l'axe de révolution : la première ligne de construction (faux : aucune)
bool skAxis (const Sketch &s, double o[3], double d[3]);
const char *consName (SkConsType t);
// un élément nouveau, avec ses points (rendus : leurs rangs)
int skAddPoint (Sketch &s, double x, double y);
int skAddLine (Sketch &s, int a, int b);
int skAddCircle (Sketch &s, int c, double r);
int skAddArc (Sketch &s, int c, int a, int b);	// (le rayon : la distance du centre au début ; la fin est ramenée)
// retire un élément (et ses contraintes, ses points qui ne servent plus) ; une contrainte
void skRemoveGeo (Sketch &s, int g);
void skRemoveCons (Sketch &s, int k);

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
// la même, avec les paramètres nommés du document (et les paramètres qui en dépendent)
bool evalExpr (const Doc &d, const char *s, double &out);
// les champs modifiables d'une opération (noms, valeurs) et leur écriture ; les formules s'appliquent au rejeu
int featureFields (const Feature &f, const char *names[3], double vals[3]);
void featureSet (Feature &f, int i, double v);
// les arêtes tangentes à la suite d'une arête (la chaîne qu'un congé suit) -> leurs numéros, l'arête comprise
std::vector<int> tangentChain (const Body &b, int edge);
// le plus grand rayon de congé qui passe sur ces arêtes du corps (0 : aucun), cherché par dichotomie
double filletMax (const TopoDS_Shape &s, const std::vector<EdgeRef> &edges, double hi);
// l'axe de la poignée d'un congé ou d'un chanfrein sur une arête du corps : at, le milieu de l'arête ; dir (unitaire),
// la bissectrice des deux faces, tournée là où le congé s'enfonce (dans la matière pour une arête saillante, dans le
// vide pour une arête rentrante) -> faux : arête pas retrouvée, ou pas entre deux faces distinctes
bool edgeHandle (const TopoDS_Shape &s, const EdgeRef &r, double at[3], double dir[3]);
std::string fmtNum (double v);	// 12,5 ; 90

} // namespace forge
