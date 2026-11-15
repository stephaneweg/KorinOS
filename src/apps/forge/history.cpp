// history.cpp -- Forge : l'historique d'opérations et son rejeu en corps OpenCASCADE (model.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "model.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Trsf.hxx>
#include <map>
#include <gp_Ax1.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>

namespace forge {

typedef NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> ShapeMap;

struct Fail {
	std::string msg;
};

static gp_Pnt P (const double *v) { return gp_Pnt (v[0], v[1], v[2]); }
static gp_Dir D (const double *v) { return gp_Dir (v[0], v[1], v[2]); }

static TopoDS_Shape unify (const TopoDS_Shape &s)
{
	ShapeUpgrade_UnifySameDomain u (s, true, true, false);
	u.Build ();
	return u.Shape ();
}

static double diag (const TopoDS_Shape &s)
{
	Bnd_Box b;
	BRepBndLib::Add (s, b);
	if (b.IsVoid ()) return 100;
	double x0, y0, z0, x1, y1, z1;
	b.Get (x0, y0, z0, x1, y1, z1);
	return sqrt ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
}

// ---- les formes des opérations ----------------------------------------------------------------------------------

static TopoDS_Shape makeBox (const Frame &f, double w, double d, double h)
{
	if (w < 0.01 || d < 0.01 || std::fabs (h) < 0.01) throw Fail { "une cote est nulle" };
	gp_Dir n = D (f.n), x = D (f.x);
	gp_Dir y = n.Crossed (x);
	gp_Pnt c = P (f.o).Translated (gp_Vec (x) * (-w / 2) + gp_Vec (y) * (-d / 2));
	if (h < 0) c.Translate (gp_Vec (n) * h);
	return BRepPrimAPI_MakeBox (gp_Ax2 (c, n, x), w, d, std::fabs (h)).Shape ();
}

static TopoDS_Shape makeCyl (const Frame &f, double r, double h)
{
	if (r < 0.01 || std::fabs (h) < 0.01) throw Fail { "une cote est nulle" };
	gp_Pnt c = P (f.o);
	if (h < 0) c.Translate (gp_Vec (D (f.n)) * h);
	return BRepPrimAPI_MakeCylinder (gp_Ax2 (c, D (f.n), D (f.x)), r, std::fabs (h)).Shape ();
}

// les trous d'un perçage (du plan du repère vers l'intérieur, -n), de part en part sur « depth »
static TopoDS_Shape makeHoles (const Feature &f, double depth)
{
	if (f.d1 < 0.01 || f.pos.size () < 2) throw Fail { "pas de trou" };
	gp_Dir n = D (f.frame.n), x = D (f.frame.x);
	gp_Dir y = n.Crossed (x);
	TopoDS_Shape out;
	for (size_t i = 0; i + 1 < f.pos.size (); i += 2) {
		gp_Pnt c = P (f.frame.o).Translated (gp_Vec (x) * f.pos[i] + gp_Vec (y) * f.pos[i + 1] + gp_Vec (n) * 1.0);
		double dep = f.c > 0 ? f.c : depth;
		TopoDS_Shape t = BRepPrimAPI_MakeCylinder (gp_Ax2 (c, n.Reversed (), x), f.d1 / 2, dep + 1).Shape ();
		if (f.d2 > f.d1 && f.dep2 > 0)
			t = BRepAlgoAPI_Fuse (t, BRepPrimAPI_MakeCylinder (gp_Ax2 (c, n.Reversed (), x), f.d2 / 2, f.dep2 + 1).Shape ()).Shape ();
		out = out.IsNull () ? t : BRepAlgoAPI_Fuse (out, t).Shape ();
	}
	return out;
}

// un oblong de part en part : longueur hors tout a, largeur b, centré sur le repère, le long de x
static TopoDS_Shape makeSlot (const Feature &f, double depth)
{
	double a = f.a, b = f.b;
	if (b < 0.01 || a < b) throw Fail { "l'oblong est plus large que long" };
	Frame g = f.frame;
	gp_Dir n = D (g.n), x = D (g.x);
	gp_Pnt top = P (g.o).Translated (gp_Vec (n) * 1.0);
	for (int i = 0; i < 3; i++) g.o[i] = top.Coord (i + 1);
	TopoDS_Shape s = makeBox (g, std::max (a - b, 0.02), b, -(depth + 1));
	for (double k : { -1.0, 1.0 }) {
		gp_Pnt c = top.Translated (gp_Vec (x) * (k * (a - b) / 2));
		s = BRepAlgoAPI_Fuse (s, BRepPrimAPI_MakeCylinder (gp_Ax2 (c, n.Reversed (), x), b / 2, depth + 1).Shape ()).Shape ();
	}
	return s;
}

// ---- retrouver une arête, une face ------------------------------------------------------------------------------

static void edgeSig (const TopoDS_Edge &e, double mid[3], double tan[3])
{
	BRepAdaptor_Curve c (e);
	double t = (c.FirstParameter () + c.LastParameter ()) / 2;
	gp_Pnt p;
	gp_Vec v;
	c.D1 (t, p, v);
	mid[0] = p.X (), mid[1] = p.Y (), mid[2] = p.Z ();
	double l = v.Magnitude ();
	if (l < 1e-12) l = 1;
	tan[0] = v.X () / l, tan[1] = v.Y () / l, tan[2] = v.Z () / l;
}

static TopoDS_Edge findEdge (const TopoDS_Shape &s, const EdgeRef &r)
{
	TopoDS_Edge best;
	double bd = 3.0;	// (au plus 3 mm du milieu gardé, tangente parallèle)
	for (TopExp_Explorer x (s, TopAbs_EDGE); x.More (); x.Next ()) {
		TopoDS_Edge e = TopoDS::Edge (x.Current ());
		if (BRep_Tool::Degenerated (e)) continue;
		double m[3], t[3];
		edgeSig (e, m, t);
		double dot = std::fabs (t[0] * r.tan[0] + t[1] * r.tan[1] + t[2] * r.tan[2]);
		if (dot < 0.995) continue;
		double d = sqrt ((m[0] - r.mid[0]) * (m[0] - r.mid[0]) + (m[1] - r.mid[1]) * (m[1] - r.mid[1]) + (m[2] - r.mid[2]) * (m[2] - r.mid[2]));
		if (d < bd) bd = d, best = e;
	}
	return best;
}

static bool planeOf (const TopoDS_Face &f, gp_Pln &pl, gp_Dir &n)
{
	BRepAdaptor_Surface a (f);
	if (a.GetType () != GeomAbs_Plane) return false;
	pl = a.Plane ();
	n = pl.Axis ().Direction ();
	if (f.Orientation () == TopAbs_REVERSED) n.Reverse ();
	return true;
}

static TopoDS_Face findFace (const TopoDS_Shape &s, const FaceRef &r)
{
	TopoDS_Face best;
	double bd = 3.0;
	for (TopExp_Explorer x (s, TopAbs_FACE); x.More (); x.Next ()) {
		TopoDS_Face f = TopoDS::Face (x.Current ());
		gp_Pln pl;
		gp_Dir n;
		if (!planeOf (f, pl, n)) continue;
		if (n.X () * r.n[0] + n.Y () * r.n[1] + n.Z () * r.n[2] < 0.999) continue;
		double d = pl.Distance (P (r.pt));
		if (d > bd) continue;
		gp_Pnt q = P (r.pt).Translated (gp_Vec (n) * -(gp_Vec (pl.Location (), P (r.pt)).Dot (gp_Vec (n))));
		BRepClass_FaceClassifier cl (f, q, 1e-6);
		if (cl.State () != TopAbs_IN && cl.State () != TopAbs_ON) continue;
		bd = d, best = f;
	}
	return best;
}

EdgeRef edgeRef (const Body &b, int edge)
{
	EdgeRef r {};
	ShapeMap m;
	TopExp::MapShapes (b.shape, TopAbs_EDGE, m);
	if (edge >= 0 && edge < m.Extent ()) edgeSig (TopoDS::Edge (m (edge + 1)), r.mid, r.tan);
	return r;
}

bool faceRef (const Body &b, int face, FaceRef &out)
{
	ShapeMap m;
	TopExp::MapShapes (b.shape, TopAbs_FACE, m);
	if (face < 0 || face >= m.Extent ()) return false;
	TopoDS_Face f = TopoDS::Face (m (face + 1));
	gp_Pln pl;
	gp_Dir n;
	if (!planeOf (f, pl, n)) return false;
	GProp_GProps g;
	BRepGProp::SurfaceProperties (f, g);
	gp_Pnt c = g.CentreOfMass ();
	BRepClass_FaceClassifier cl (f, c, 1e-6);
	if (cl.State () != TopAbs_IN) {	// (le centre hors de la face : un point d'un de ses triangles)
		const Mesh &me = b.mesh;
		for (int t = me.faceFirst[face]; t < me.faceFirst[face + 1]; t++) {
			gp_XYZ s (0, 0, 0);
			for (int k = 0; k < 3; k++) {
				int i = me.tris[t * 3 + k];
				s += gp_XYZ (me.pts[i * 3], me.pts[i * 3 + 1], me.pts[i * 3 + 2]);
			}
			c = gp_Pnt (s / 3);
			break;
		}
	}
	out.pt[0] = c.X (), out.pt[1] = c.Y (), out.pt[2] = c.Z ();
	out.n[0] = n.X (), out.n[1] = n.Y (), out.n[2] = n.Z ();
	return true;
}

bool facePlane (const Body &b, int face, Frame &out)
{
	FaceRef r;
	if (!faceRef (b, face, r)) return false;
	for (int i = 0; i < 3; i++) out.o[i] = r.pt[i], out.n[i] = r.n[i];
	gp_Dir n = D (r.n);	// x : horizontal pour une face verticale, le long de X sinon
	gp_Dir x = std::fabs (n.Z ()) < 0.9 ? gp_Dir (0, 0, 1).Crossed (n) : gp_Dir (gp_Vec (1, 0, 0) - gp_Vec (n) * n.X ());
	out.x[0] = x.X (), out.x[1] = x.Y (), out.x[2] = x.Z ();
	return true;
}

// ---- le rejeu ---------------------------------------------------------------------------------------------------

static Body *target (Doc &d, std::vector<int> &rank, int body)
{
	if (body < 0 || body >= (int) rank.size () || rank[body] < 0) throw Fail { "le corps visé n'existe plus" };
	Body &b = d.bodies[rank[body]];
	if (b.shape.IsNull ()) throw Fail { "le corps visé a été retiré" };
	return &b;
}

static void applyOp (Doc &d, std::vector<int> &rank, const Feature &f, int fi, const TopoDS_Shape &tool)
{
	if (f.op == OP_NEW) {
		Body b;
		b.name = "Corps " + std::to_string (rank.size () + 1);
		if (rank.empty ()) b.name = d.name;
		b.shape = tool;
		b.color = (int) rank.size ();
		rank.push_back ((int) d.bodies.size ());
		d.bodies.push_back (b);
		d.bodyOf.push_back (fi);
		return;
	}
	Body *b = target (d, rank, f.body);
	TopoDS_Shape r;
	if (f.op == OP_JOIN) r = BRepAlgoAPI_Fuse (b->shape, tool).Shape ();
	else if (f.op == OP_CUT) r = BRepAlgoAPI_Cut (b->shape, tool).Shape ();
	else r = BRepAlgoAPI_Common (b->shape, tool).Shape ();
	TopExp_Explorer x (r, TopAbs_SOLID);
	if (!x.More ()) throw Fail { f.op == OP_CUT ? "il ne resterait rien du corps" : "le résultat est vide" };
	b->shape = unify (r);
}

// la forme d'une extrusion (distance signée le long de la normale du plan) ou d'une révolution (angle en degrés)
static TopoDS_Shape sweepTool (const Doc &d, const Feature &f)
{
	if (f.ref < 0 || f.ref >= (int) d.feats.size () || d.feats[f.ref].kind != ST_SKETCH) throw Fail { "l'esquisse n'existe plus" };
	const Sketch &sk = d.feats[f.ref].sk;
	std::string err;
	TopoDS_Shape faces = skFaces (sk, err);
	if (faces.IsNull ()) throw Fail { "l'esquisse n'a " + err };
	if (f.kind == ST_EXTRUDE) {
		if (std::fabs (f.a) < 0.01) throw Fail { "la distance est nulle" };
		return BRepPrimAPI_MakePrism (faces, gp_Vec (D (sk.plane.n)) * f.a).Shape ();
	}
	double o[3], dir[3];
	if (!skAxis (sk, o, dir)) throw Fail { "l'esquisse n'a pas d'axe (une ligne de construction)" };
	if (std::fabs (f.a) < 0.1) throw Fail { "l'angle est nul" };
	return BRepPrimAPI_MakeRevol (faces, gp_Ax1 (P (o), D (dir)), f.a * M_PI / 180).Shape ();
}

// un perçage dont la face est retrouvée sur la forme : son repère recalé sur le plan de cette face (une épaisseur
// changée est suivie) ; sinon tel quel
static Feature holeOnFace (const Feature &f, const TopoDS_Shape &s)
{
	Feature g = f;
	if (f.faces.empty () || s.IsNull ()) return g;
	TopoDS_Face fc = findFace (s, f.faces[0]);
	gp_Pln pl;
	gp_Dir n;
	if (fc.IsNull () || !planeOf (fc, pl, n)) return g;
	double off = gp_Vec (pl.Location (), P (f.frame.o)).Dot (gp_Vec (n));
	g.frame.o[0] -= off * n.X (), g.frame.o[1] -= off * n.Y (), g.frame.o[2] -= off * n.Z ();
	return g;
}

TopoDS_Shape featureTool (const Doc &d, const Feature &f)
{
	try {
		switch (f.kind) {
		case ST_BOX: return makeBox (f.frame, f.a, f.b, f.c);
		case ST_CYLINDER: return makeCyl (f.frame, f.a, f.c);
		case ST_HOLE: return makeHoles (f.body >= 0 && f.body < (int) d.bodies.size () ? holeOnFace (f, d.bodies[f.body].shape) : f, 500);
		case ST_SLOT: return makeSlot (f, 500);
		case ST_EXTRUDE: case ST_REVOLVE: return sweepTool (d, f);
		case ST_PUSH: {
			for (auto &b : d.bodies) {
				if (b.shape.IsNull ()) continue;
				TopoDS_Face fc = findFace (b.shape, f.face);
				if (fc.IsNull ()) continue;
				return BRepPrimAPI_MakePrism (fc, gp_Vec (D (f.face.n)) * f.a).Shape ();
			}
			break;
		}
		default: break;
		}
	} catch (...) {
	}
	return TopoDS_Shape ();
}



int replay (Doc &d, double deflection)
{
	d.bodies.clear ();
	d.bodyOf.clear ();
	std::vector<int> rank;		// rang de création -> place dans bodies
	int errors = 0;
	if (d.bar < 0) d.bar = 0;
	if (d.bar > (int) d.feats.size ()) d.bar = (int) d.feats.size ();
	for (int i = 0; i < (int) d.feats.size (); i++) {
		Feature &f = d.feats[i];
		f.error.clear ();
		if (i >= d.bar || f.off) continue;
		try {
			for (auto &e : f.expr) {	// (les valeurs données par une formule : les paramètres du moment)
				double v;
				if (!evalExpr (d, e.second.c_str (), v)) throw Fail { "formule illisible ou paramètre inconnu : " + e.second };
				featureSet (f, e.first, v);
			}
			switch (f.kind) {
			case ST_BOX: applyOp (d, rank, f, i, makeBox (f.frame, f.a, f.b, f.c)); break;
			case ST_CYLINDER: applyOp (d, rank, f, i, makeCyl (f.frame, f.a, f.c)); break;
			case ST_HOLE: {
				Body *b = target (d, rank, f.body);
				Feature g = holeOnFace (f, b->shape);
				applyOp (d, rank, g, i, makeHoles (g, diag (b->shape) + 2));
				break;
			}
			case ST_SLOT: {
				Body *b = target (d, rank, f.body);
				applyOp (d, rank, f, i, makeSlot (f, diag (b->shape) + 2));
				break;
			}
			case ST_PUSH: {
				Body *b = target (d, rank, f.body);
				TopoDS_Face fc = findFace (b->shape, f.face);
				if (fc.IsNull ()) throw Fail { "la face n'a pas été retrouvée" };
				if (std::fabs (f.a) < 0.01) throw Fail { "la distance est nulle" };
				TopoDS_Shape pr = BRepPrimAPI_MakePrism (fc, gp_Vec (D (f.face.n)) * f.a).Shape ();
				Feature g = f;
				g.op = f.a > 0 ? OP_JOIN : OP_CUT;	// (tirer joint, pousser enlève)
				applyOp (d, rank, g, i, pr);
				break;
			}
			case ST_SKETCH: break;	// (une esquisse ne change aucun corps ; ses points sont déjà résolus)
			case ST_EXTRUDE:
			case ST_REVOLVE: {
				TopoDS_Shape t = sweepTool (d, f);
				Feature g = f;
				if (g.op != OP_NEW) target (d, rank, f.body);
				applyOp (d, rank, g, i, t);
				break;
			}
			case ST_FILLET:
			case ST_CHAMFER: {
				Body *b = target (d, rank, f.body);
				if (f.a <= 0) throw Fail { "le rayon est nul" };
				if (f.edges.empty ()) throw Fail { "aucune arête" };
				TopoDS_Shape r;
				if (f.kind == ST_FILLET) {
					BRepFilletAPI_MakeFillet mk (b->shape);
					for (auto &er : f.edges) {
						TopoDS_Edge e = findEdge (b->shape, er);
						if (e.IsNull ()) throw Fail { "une arête n'a pas été retrouvée" };
						mk.Add (f.a, e);
					}
					mk.Build ();
					if (!mk.IsDone ()) throw Fail { "le congé ne passe pas (rayon trop grand ?)" };
					r = mk.Shape ();
				} else {
					BRepFilletAPI_MakeChamfer mk (b->shape);
					for (auto &er : f.edges) {
						TopoDS_Edge e = findEdge (b->shape, er);
						if (e.IsNull ()) throw Fail { "une arête n'a pas été retrouvée" };
						mk.Add (f.a, e);
					}
					mk.Build ();
					if (!mk.IsDone ()) throw Fail { "le chanfrein ne passe pas (distance trop grande ?)" };
					r = mk.Shape ();
				}
				b->shape = r;
				break;
			}
			case ST_UNION:
			case ST_CUT: {
				Body *b = target (d, rank, f.body);
				Body *t = target (d, rank, f.tool);
				if (b == t) throw Fail { "le même corps deux fois" };
				Feature g = f;
				g.op = f.kind == ST_UNION ? OP_JOIN : OP_CUT;
				applyOp (d, rank, g, i, t->shape);
				t->shape = TopoDS_Shape ();	// (l'outil est consommé)
				break;
			}
			case ST_SHELL: {
				Body *b = target (d, rank, f.body);
				if (f.a <= 0) throw Fail { "l'épaisseur est nulle" };
				TopTools_ListOfShape rm;
				for (auto &fr : f.faces) {
					TopoDS_Face fc = findFace (b->shape, fr);
					if (fc.IsNull ()) throw Fail { "une face retirée n'a pas été retrouvée" };
					rm.Append (fc);
				}
				if (rm.IsEmpty ()) throw Fail { "aucune face retirée" };
				BRepOffsetAPI_MakeThickSolid mk;
				mk.MakeThickSolidByJoin (b->shape, rm, -f.a, 1e-3);
				mk.Build ();
				if (!mk.IsDone ()) throw Fail { "la coque ne passe pas (épaisseur trop grande ?)" };
				b->shape = mk.Shape ();
				break;
			}
			case ST_PATTERN: {
				if (f.ref < 0 || f.ref >= i) throw Fail { "l'étape répétée n'existe pas" };
				const Feature &r = d.feats[f.ref];
				if (r.kind != ST_BOX && r.kind != ST_CYLINDER && r.kind != ST_HOLE && r.kind != ST_SLOT && r.kind != ST_EXTRUDE && r.kind != ST_REVOLVE)
					throw Fail { "seules les formes, les perçages et les extrusions se répètent" };
				if (!r.error.empty () || r.off) throw Fail { "l'étape répétée est en erreur ou désactivée" };
				if (f.count < 2) throw Fail { "il faut au moins deux exemplaires" };
				TopoDS_Shape tool = featureTool (d, r);
				if (tool.IsNull ()) throw Fail { "la forme de l'étape répétée n'a pas pu être faite" };
				gp_Ax1 axis (P (f.frame.o), D (f.frame.n));
				double step = f.mode == 0 ? f.a : std::fabs (std::fabs (f.a) - 360) < 1e-9 ? 360.0 / f.count : f.a / (f.count - 1);
				for (int k = 1; k < f.count; k++) {
					gp_Trsf t;
					if (f.mode == 0) t.SetTranslation (gp_Vec (D (f.frame.n)) * (step * k));
					else t.SetRotation (axis, step * k * M_PI / 180);
					applyOp (d, rank, r, i, BRepBuilderAPI_Transform (tool, t, true).Shape ());
				}
				break;
			}
			case ST_MIRROR: {
				Body *b = target (d, rank, f.body);
				gp_Trsf t;
				t.SetMirror (gp_Ax2 (P (f.frame.o), D (f.frame.n)));
				TopoDS_Shape m = BRepBuilderAPI_Transform (b->shape, t, true).Shape ();
				if (f.op == OP_JOIN) {
					TopoDS_Shape r = BRepAlgoAPI_Fuse (b->shape, m).Shape ();
					b->shape = unify (r);
				} else {
					Feature g = f;
					g.op = OP_NEW;
					applyOp (d, rank, g, i, m);
				}
				break;
			}
			default: throw Fail { "opération inconnue" };
			}
		} catch (const Fail &x) {
			f.error = x.msg, errors++;
		} catch (const Standard_Failure &x) {
#if OCC_VERSION_HEX >= 0x080000
			f.error = std::string ("OpenCASCADE : ") + x.what ();
#else
			f.error = std::string ("OpenCASCADE : ") + x.GetMessageString ();
#endif
			errors++;
		} catch (const std::exception &x) {
			f.error = x.what (), errors++;
		}
	}
	for (auto &b : d.bodies)
		if (!b.shape.IsNull ()) meshBody (b, deflection);
		else b.visible = false, b.mesh = Mesh ();
	return errors;
}

// ---- les noms et les étiquettes ---------------------------------------------------------------------------------

std::string fmtNum (double v)
{
	char b[48];
	if (std::fabs (v - std::round (v)) < 1e-6) snprintf (b, sizeof b, "%.0f", v);
	else snprintf (b, sizeof b, "%.2f", v);
	std::string s = b;
	if (s.find ('.') != std::string::npos) {
		while (s.back () == '0') s.pop_back ();
		if (s.back () == '.') s.pop_back ();
	}
	for (auto &c : s) if (c == '.') c = ',';
	return s;
}

const char *opName (Op o)
{
	switch (o) {
	case OP_NEW: return "nouveau corps";
	case OP_JOIN: return "joindre";
	case OP_CUT: return "enlever";
	case OP_INTERSECT: return "intersection";
	}
	return "";
}

std::string featureDetail (const Feature &f)
{
	std::string n = std::to_string (f.edges.size ()) + (f.edges.size () > 1 ? " arêtes" : " arête");
	switch (f.kind) {
	case ST_BOX: return fmtNum (f.a) + " × " + fmtNum (f.b) + " × " + fmtNum (std::fabs (f.c)) + " · " + opName (f.op);
	case ST_CYLINDER: return "Ø " + fmtNum (2 * f.a) + " × " + fmtNum (std::fabs (f.c)) + " · " + opName (f.op);
	case ST_FILLET: return "R " + fmtNum (f.a) + " · " + n;
	case ST_CHAMFER: return fmtNum (f.a) + " · " + n;
	case ST_PUSH: return (f.a > 0 ? "tirer " : "pousser ") + fmtNum (std::fabs (f.a)) + " mm";
	case ST_HOLE: return "Ø " + fmtNum (f.d1) + (f.d2 > f.d1 ? " / Ø " + fmtNum (f.d2) : "") + (f.c > 0 ? " · prof. " + fmtNum (f.c) : " · traversant") + " · "
		+ std::to_string (f.pos.size () / 2) + (f.pos.size () > 2 ? " trous" : " trou");
	case ST_SLOT: return "oblong " + fmtNum (f.a) + " × " + fmtNum (f.b) + " · à travers";
	case ST_SKETCH: return std::to_string (f.sk.geos.size ()) + " éléments, " + std::to_string (f.sk.cons.size ()) + " contraintes"
		+ (f.sk.dof == 0 ? " · contrainte" : f.sk.dof > 0 ? " · " + std::to_string (f.sk.dof) + " DDL" : "");
	case ST_EXTRUDE: return fmtNum (std::fabs (f.a)) + " mm" + (f.a < 0 ? " vers le bas" : "") + " · " + opName (f.op);
	case ST_REVOLVE: return fmtNum (f.a) + "° · " + opName (f.op);
	case ST_SHELL: return "épaisseur " + fmtNum (f.a) + " · " + std::to_string (f.faces.size ()) + (f.faces.size () > 1 ? " faces ouvertes" : " face ouverte");
	case ST_PATTERN: return std::to_string (f.count) + " exemplaires · " + (f.mode == 0 ? "pas " + fmtNum (f.a) + " mm" : fmtNum (f.a) + "°");
	case ST_MIRROR: return std::string ("symétrie · ") + (f.op == OP_JOIN ? "jointe" : "nouveau corps");
	case ST_UNION: return "deux corps joints";
	case ST_CUT: return "un corps retiré d'un autre";
	default: return "";
	}
}

std::string featureName (const Doc &d, StepKind k)
{
	static const char *const N[] = { "Boîte", "Cylindre", "Esquisse", "Extrusion", "Congé", "Chanfrein", "Union", "Soustraction", "Perçage", "Pousser/tirer", "Oblong", "Révolution",
		"Coque", "Réseau", "Symétrie" };
	int n = 1;
	for (auto &f : d.feats) n += f.kind == k;
	return std::string (N[k]) + " " + std::to_string (n);
}

// ---- l'exemple --------------------------------------------------------------------------------------------------

static Frame frame (double ox, double oy, double oz, double nx = 0, double ny = 0, double nz = 1)
{
	Frame f;
	f.o[0] = ox, f.o[1] = oy, f.o[2] = oz;
	f.n[0] = nx, f.n[1] = ny, f.n[2] = nz;
	return f;
}

static EdgeRef er (double x, double y, double z, double tx, double ty, double tz)
{
	return EdgeRef { { x, y, z }, { tx, ty, tz } };
}

void sampleDoc (Doc &d)
{
	d = Doc ();
	d.name = "Équerre";
	auto add = [&] (Feature f) { d.feats.push_back (f); };
	Feature f;
	f.kind = ST_BOX, f.name = "Boîte 1", f.op = OP_NEW, f.frame = frame (45, 30, 0), f.a = 90, f.b = 60, f.c = 8;
	add (f);
	f = Feature ();
	f.kind = ST_FILLET, f.name = "Congé 1", f.body = 0, f.a = 8, f.edges = { er (0, 0, 4, 0, 0, 1), er (90, 0, 4, 0, 0, 1) };
	add (f);
	f = Feature ();
	f.kind = ST_BOX, f.name = "Boîte 2", f.op = OP_JOIN, f.body = 0, f.frame = frame (45, 56, 8), f.a = 90, f.b = 8, f.c = 37;
	add (f);
	f = Feature ();
	f.kind = ST_FILLET, f.name = "Congé 2", f.body = 0, f.a = 12, f.edges = { er (0, 56, 45, 0, 1, 0), er (90, 56, 45, 0, 1, 0) };
	add (f);
	f = Feature ();
	f.kind = ST_FILLET, f.name = "Congé 3", f.body = 0, f.a = 6, f.edges = { er (45, 52, 8, 1, 0, 0) };
	add (f);
	f = Feature ();
	f.kind = ST_CYLINDER, f.name = "Cylindre 1", f.op = OP_CUT, f.body = 0, f.frame = frame (45, 50, 30, 0, 1, 0), f.a = 11, f.c = 12;
	add (f);
	f = Feature ();
	f.kind = ST_HOLE, f.name = "Perçage 1", f.op = OP_CUT, f.body = 0, f.frame = frame (0, 0, 8), f.d1 = 9, f.d2 = 14, f.dep2 = 3, f.pos = { 15, 22, 75, 22 };
	add (f);
	f = Feature ();
	f.kind = ST_SLOT, f.name = "Oblong 1", f.op = OP_CUT, f.body = 0, f.frame = frame (45, 26, 8), f.a = 19, f.b = 9;
	add (f);
	d.bar = (int) d.feats.size ();
}

// ---- les valeurs tapées -----------------------------------------------------------------------------------------

namespace {
struct Parser {
	const char *p;
	bool ok = true;
	const Doc *doc = nullptr;
	int depth = 0;
	static bool idStart (unsigned char c) { return isalpha (c) || c == '_' || c >= 0x80; }
	void ws () { while (*p == ' ') p++; }
	double unit (double v)
	{
		ws ();
		if (!strncmp (p, "mm", 2)) p += 2;
		else if (!strncmp (p, "cm", 2)) p += 2, v *= 10;
		else if (!strncmp (p, "in", 2)) p += 2, v *= 25.4;
		else if (*p == 'm' && !isalpha ((unsigned char) p[1])) p++, v *= 1000;
		else if (!strncmp (p, "\xC2\xB0", 2)) p += 2;	// ° (degrés : tels quels)
		return v;
	}
	double atom ()
	{
		ws ();
		if (*p == '(') {
			p++;
			double v = sum ();
			ws ();
			if (*p == ')') p++;
			else ok = false;
			return unit (v);
		}
		if (*p == '-') { p++; return -atom (); }
		if (doc && idStart ((unsigned char) *p) && !(p[0] == 'i' && p[1] == 'n' && !idStart ((unsigned char) p[2]))) {	// un paramètre nommé
			std::string name;
			while (idStart ((unsigned char) *p) || isdigit ((unsigned char) *p)) name += *p++;
			for (auto &pr : doc->params)
				if (pr.first == name) {
					if (depth > 16) { ok = false; return 0; }	// (un paramètre qui dépend de lui-même)
					Parser q { pr.second.c_str () };
					q.doc = doc, q.depth = depth + 1;
					double v = q.sum ();
					q.ws ();
					if (!q.ok || *q.p) ok = false;
					return unit (v);
				}
			ok = false;
			return 0;
		}
		if (*p == '+') { p++; return atom (); }
		char buf[64];
		int n = 0;
		while ((isdigit ((unsigned char) *p) || *p == '.' || *p == ',') && n < 63) buf[n++] = *p == ',' ? '.' : *p, p++;
		buf[n] = 0;
		if (!n) { ok = false; return 0; }
		return unit (atof (buf));
	}
	double prod ()
	{
		double v = atom ();
		for (;;) {
			ws ();
			if (*p == '*') p++, v *= atom ();
			else if (*p == '/') {
				p++;
				double w = atom ();
				if (w == 0) ok = false;
				else v /= w;
			} else return v;
		}
	}
	double sum ()
	{
		double v = prod ();
		for (;;) {
			ws ();
			if (*p == '+') p++, v += prod ();
			else if (*p == '-') p++, v -= prod ();
			else return v;
		}
	}
};
}

bool evalExpr (const Doc &d, const char *s, double &out)
{
	Parser ps { s };
	ps.doc = &d;
	double v = ps.sum ();
	ps.ws ();
	if (!ps.ok || *ps.p) return false;
	out = v;
	return true;
}

// ---- les champs d'une opération ---------------------------------------------------------------------------------

int featureFields (const Feature &f, const char *names[3], double vals[3])
{
	switch (f.kind) {
	case ST_BOX: names[0] = "Largeur", names[1] = "Profondeur", names[2] = "Hauteur", vals[0] = f.a, vals[1] = f.b, vals[2] = f.c; return 3;
	case ST_CYLINDER: names[0] = "Diamètre", names[1] = "Hauteur", vals[0] = 2 * f.a, vals[1] = f.c; return 2;
	case ST_FILLET: names[0] = "Rayon", vals[0] = f.a; return 1;
	case ST_CHAMFER: names[0] = "Distance", vals[0] = f.a; return 1;
	case ST_PUSH: names[0] = "Distance", vals[0] = f.a; return 1;
	case ST_HOLE: names[0] = "Diamètre", names[1] = "Ø du lamage", names[2] = "Profondeur", vals[0] = f.d1, vals[1] = f.d2, vals[2] = f.c; return 3;
	case ST_SLOT: names[0] = "Longueur", names[1] = "Largeur", vals[0] = f.a, vals[1] = f.b; return 2;
	case ST_EXTRUDE: names[0] = "Distance", vals[0] = f.a; return 1;
	case ST_REVOLVE: names[0] = "Angle", vals[0] = f.a; return 1;
	case ST_SHELL: names[0] = "Épaisseur", vals[0] = f.a; return 1;
	case ST_PATTERN: names[0] = "Nombre", names[1] = f.mode == 0 ? "Pas" : "Angle", vals[0] = f.count, vals[1] = f.a; return 2;
	default: return 0;
	}
}

void featureSet (Feature &f, int i, double v)
{
	switch (f.kind) {
	case ST_BOX: (i == 0 ? f.a : i == 1 ? f.b : f.c) = v; break;
	case ST_CYLINDER: if (i == 0) f.a = v / 2; else f.c = v; break;
	case ST_FILLET: case ST_CHAMFER: case ST_PUSH: case ST_EXTRUDE: case ST_REVOLVE: case ST_SHELL: f.a = v; break;
	case ST_HOLE: (i == 0 ? f.d1 : i == 1 ? f.d2 : f.c) = v; break;
	case ST_SLOT: (i == 0 ? f.a : f.b) = v; break;
	case ST_PATTERN: if (i == 0) f.count = (int) std::lround (v); else f.a = v; break;
	default: break;
	}
}

// ---- la chaîne tangente, le rayon maximal -----------------------------------------------------------------------

static gp_Vec tanAt (const TopoDS_Edge &e, const TopoDS_Vertex &v)
{
	BRepAdaptor_Curve c (e);
	double t = BRep_Tool::Parameter (v, e);
	gp_Pnt p;
	gp_Vec d;
	c.D1 (t, p, d);
	return d.Magnitude () > 1e-12 ? d.Normalized () : d;
}

std::vector<int> tangentChain (const Body &b, int edge)
{
	ShapeMap edges;
	TopExp::MapShapes (b.shape, TopAbs_EDGE, edges);
	TopTools_IndexedDataMapOfShapeListOfShape v2e;
	TopExp::MapShapesAndAncestors (b.shape, TopAbs_VERTEX, TopAbs_EDGE, v2e);
	std::vector<int> out;
	if (edge < 0 || edge >= edges.Extent ()) return out;
	std::vector<char> seen (edges.Extent (), 0);
	std::vector<int> todo { edge };
	seen[edge] = 1;
	while (!todo.empty ()) {
		int e = todo.back ();
		todo.pop_back ();
		out.push_back (e);
		TopoDS_Edge E = TopoDS::Edge (edges (e + 1));
		for (TopExp_Explorer x (E, TopAbs_VERTEX); x.More (); x.Next ()) {
			TopoDS_Vertex V = TopoDS::Vertex (x.Current ());
			gp_Vec t0 = tanAt (E, V);
			const TopTools_ListOfShape &l = v2e.FindFromKey (V);
			for (TopTools_ListOfShape::Iterator it (l); it.More (); it.Next ()) {
				int k = edges.FindIndex (it.Value ()) - 1;
				if (k < 0 || seen[k] || BRep_Tool::Degenerated (TopoDS::Edge (it.Value ()))) continue;
				gp_Vec t1 = tanAt (TopoDS::Edge (it.Value ()), V);
				if (std::fabs (t0.Dot (t1)) > 0.9994) seen[k] = 1, todo.push_back (k);	// (moins de 2° : tangentes)
			}
		}
	}
	return out;
}

double filletMax (const TopoDS_Shape &s, const std::vector<EdgeRef> &edges, double hi)
{
	auto passes = [&] (double r) {
		try {
			BRepFilletAPI_MakeFillet mk (s);
			for (auto &er : edges) {
				TopoDS_Edge e = findEdge (s, er);
				if (e.IsNull ()) return false;
				mk.Add (r, e);
			}
			mk.Build ();
			return mk.IsDone () && BRepCheck_Analyzer (mk.Shape ()).IsValid ();
		} catch (...) {
			return false;
		}
	};
	double lo = 0;
	for (int k = 0; k < 14 && hi - lo > 0.01; k++) {
		double m = (lo + hi) / 2;
		if (passes (m)) lo = m;
		else hi = m;
	}
	return std::floor (lo * 10) / 10;	// (au dixième, en dessous)
}

bool evalExpr (const char *s, double &out)
{
	Parser ps { s };
	double v = ps.sum ();
	ps.ws ();
	if (!ps.ok || *ps.p) return false;
	out = v;
	return true;
}

} // namespace forge
