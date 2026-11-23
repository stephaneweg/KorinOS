// render.h -- Forge : la vue 3D rendue par le processeur dans des pixels 0x00RRGGBB (une surface d'UIKit) :
// projection en perspective (par défaut) ou orthographique, tampon de profondeur, faces ombrées, arêtes, grille,
// trièdre, cube de vue (toujours orthographique) ; choix de l'élément sous le pointeur. En attendant le chemin GPU (GlKit, tools/mesa/armgl/glview.h), qui gardera la
// même interface. Sans UIKit : se compile aussi pour Linux (essais).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <vector>
#include "model.h"

namespace forge {

// les couleurs, tirées des jetons du thème par l'app (docs/DESIGN-CAO.md §10)
struct Palette {
	unsigned bg, grid, grid50, accent, hover, edge, axis[3], cubeFace, cubeLine, cubeHot, danger;
	unsigned inter = 0;		// (le fantôme d'une intersection)
	bool dark;
};

// un élément de la pièce : corps (-1 : rien), face ou arête (-1 : le corps entier)
struct Pick {
	int body = -1, face = -1, edge = -1;
	bool operator== (const Pick &o) const { return body == o.body && face == o.face && edge == o.edge; }
	bool operator!= (const Pick &o) const { return !(*this == o); }
	bool none () const { return body < 0; }
};

// la caméra : point visé, cap (degrés autour de Z), hauteur (degrés au-dessus du plan XY), échelle (pixels par mm au
// point visé ; en perspective, elle donne la distance de l'œil au point visé : View::dist)
struct Camera {
	double tx = 45, ty = 30, tz = 20;
	double yaw = -50, pitch = 28, scale = 6;
	void basis (double r[3], double u[3], double f[3]) const;
	bool operator== (const Camera &o) const { return tx == o.tx && ty == o.ty && tz == o.tz && yaw == o.yaw && pitch == o.pitch && scale == o.scale; }
};

struct Proj;

class View {
public:
	// la taille des pixels (la zone de la vue) ; les tampons suivent
	void resize (int w, int h);
	int width () const { return W; }
	int height () const { return H; }
	// rend le document dans pix (pas en pixels) ; sel : ce qui est choisi (plusieurs arêtes ou faces), hot : le survol
	void render (unsigned *pix, int pitch, const Doc &d, const Palette &p, const std::vector<Pick> &sel, const Pick &hot);
	// l'élément sous le point (x, y) de la vue, d'après le dernier rendu
	Pick pick (const Doc &d, int x, int y) const;
	// le cube de vue : sa zone (en haut à droite), la face sous le point (-1 : aucune ; 0..5 : +X -X +Y -Y +Z -Z)
	void cubeRect (int &x, int &y, int &w, int &h) const;
	int cubeFaceAt (int x, int y) const;
	// les étiquettes du cube : pour chaque face vue, son centre (dans la vue) ; visible[i] faux sinon
	void cubeLabels (int cx[6], int cy[6], bool visible[6]) const;
	// tourner, déplacer, zoomer vers un point, vues types, cadrer
	void orbit (double dyaw, double dpitch);
	void panFrom (int x, int y);	// (au début d'un déplacement : la profondeur du point pris sous le pointeur)
	void pan (int dx, int dy);
	void zoomAt (int x, int y, double factor);
	void standard (int face);	// la face du cube vers soi (0..5), ou 6 : l'isométrique de départ
	void fit (const Doc &d);
	// le rayon sous le point (x, y) de la vue : origine (l'œil en perspective, devant la scène sinon), direction unitaire
	void ray (int x, int y, double o[3], double dir[3]) const;
	// le point (x, y) de la vue sur le plan (point p, normale n) -> faux si le plan est vu par la tranche
	bool onPlane (int x, int y, const double p[3], const double n[3], double out[3]) const;
	// un point de l'espace à l'écran (dans la vue)
	void toScreen (const double p[3], double &x, double &y) const;
	// le fantôme d'une opération en cours (null : aucun), dessiné en teinte d'accent, ou de danger pour enlever
	const Mesh *ghost = nullptr;
	bool ghostCut = false, ghostInter = false;	// (intersection : en teinte inter, cachée par ce qui est devant)
	// des traits par-dessus tout (l'esquisse) : polylignes dans l'espace, couleur, épaisseur, pointillés ; depth : cachés
	// par la pièce qui est devant (une esquisse terminée, hors de son édition) ; des points
	struct Stroke { std::vector<double> pts; unsigned col; int thick; bool dashed; bool depth = false; };
	// (round : un disque de rayon size, évidé de hole pixels en holeCol ; sinon un carré de côté 2 size + 1)
	struct Dot { double p[3]; unsigned col; int size; bool round = false; int hole = 0; unsigned holeCol = 0; };
	std::vector<Stroke> strokes;
	std::vector<Dot> dots;
	// des surfaces teintées sous les traits, sans profondeur (les régions d'une esquisse pendant l'extrusion : choisies,
	// survolées) ; amount : 0..255, la part de la couleur mêlée à la scène
	struct Fill { const Mesh *m; unsigned col; int amount; };
	std::vector<Fill> fills;
	bool dimBodies = false;		// (en esquisse : la pièce pâlie)
	// regarder le long de -n (la face vers soi), x vers la droite autant que possible
	void lookAlong (const double n[3]);
	Camera cam;
	// la projection : perspective (angle de champ vertical FOV, en degrés) ou orthographique ; la même échelle au
	// point visé dans les deux, la pièce garde sa taille quand on passe de l'une à l'autre
	bool persp = true;
	static constexpr double FOV = 35;
	double focal () const;		// (la focale en pixels : à z mm de l'œil, x mm du centre donnent x * focal / z pixels)
	double dist () const;		// (l'œil au point visé, en mm)
	void eye (double e[3]) const;
	bool edges = true, grid = true;
	int hotCube = -1;		// la face du cube survolée
	double lastMs = 0;		// durée du dernier rendu
private:
	int W = 0, H = 0;
	std::vector<float> zb;		// profondeur (plus petit : plus près)
	std::vector<int> fid;		// corps << 20 | face + 1 par pixel (0 : rien)
	struct Seg { float x0, y0, z0, x1, y1, z1; int body, edge; };
	std::vector<Seg> segs;		// les arêtes projetées du dernier rendu (profondeur : le long de la vue, en mm)
	Camera rcam;			// la caméra du dernier rendu (zb et fid valent pour elle)
	bool rpersp = false, rdone = false;
	double panZ = 0;		// la profondeur du point pris par panFrom (0 : celle du point visé)
	Proj proj () const;
	bool sameAsRendered () const { return rdone && rpersp == persp && rcam == cam; }
	void seg (unsigned *pix, int pitch, const Proj &P, const double a[3], const double b[3], unsigned col, int thick, bool depth, int body = -1, int edge = -1);
	float zAt (float z0, float z1, double t) const;
	float bias (float z) const;
	void line (unsigned *pix, int pitch, double x0, double y0, double z0, double x1, double y1, double z1, unsigned col, int thick, bool depth);
};

} // namespace forge
