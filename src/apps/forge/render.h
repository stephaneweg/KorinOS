// render.h -- Forge : la vue 3D rendue par le processeur dans des pixels 0x00RRGGBB (une surface d'UIKit) :
// projection orthographique, tampon de profondeur, faces ombrées, arêtes, grille, trièdre, cube de vue ; choix
// de l'élément sous le pointeur. En attendant le chemin GPU (GlKit, tools/mesa/armgl/glview.h), qui gardera la
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
	bool dark;
};

// un élément de la pièce : corps (-1 : rien), face ou arête (-1 : le corps entier)
struct Pick {
	int body = -1, face = -1, edge = -1;
	bool operator== (const Pick &o) const { return body == o.body && face == o.face && edge == o.edge; }
	bool operator!= (const Pick &o) const { return !(*this == o); }
	bool none () const { return body < 0; }
};

// la caméra : point visé, cap (degrés autour de Z), hauteur (degrés au-dessus du plan XY), échelle (pixels par mm)
struct Camera {
	double tx = 45, ty = 30, tz = 20;
	double yaw = -50, pitch = 28, scale = 6;
	void basis (double r[3], double u[3], double f[3]) const;
};

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
	void pan (int dx, int dy);
	void zoomAt (int x, int y, double factor);
	void standard (int face);	// la face du cube vers soi (0..5), ou 6 : l'isométrique de départ
	void fit (const Doc &d);
	// le rayon sous le point (x, y) de la vue : origine devant la scène, direction de la vue
	void ray (int x, int y, double o[3], double dir[3]) const;
	// le point (x, y) de la vue sur le plan (point p, normale n) -> faux si le plan est vu par la tranche
	bool onPlane (int x, int y, const double p[3], const double n[3], double out[3]) const;
	// un point de l'espace à l'écran (dans la vue)
	void toScreen (const double p[3], double &x, double &y) const;
	// le fantôme d'une opération en cours (null : aucun), dessiné en teinte d'accent, ou de danger pour enlever
	const Mesh *ghost = nullptr;
	bool ghostCut = false;
	Camera cam;
	bool edges = true, grid = true;
	int hotCube = -1;		// la face du cube survolée
	double lastMs = 0;		// durée du dernier rendu
private:
	int W = 0, H = 0;
	std::vector<float> zb;		// profondeur (plus petit : plus près)
	std::vector<int> fid;		// corps << 20 | face + 1 par pixel (0 : rien)
	struct Seg { float x0, y0, z0, x1, y1, z1; int body, edge; };
	std::vector<Seg> segs;		// les arêtes projetées du dernier rendu
	void project (const float *p, double &sx, double &sy, double &sz) const;
	void tri (unsigned *pix, int pitch, const double v[3][3], const double li[3], unsigned col, int id);
	void line (unsigned *pix, int pitch, double x0, double y0, double z0, double x1, double y1, double z1, unsigned col, int thick, bool depth);
};

} // namespace forge
