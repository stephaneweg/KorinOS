// sketch.cpp -- Forge : l'esquisse (model.h) ; ses contraintes résolues par PlaneGCS, ses contours en faces
// d'OpenCASCADE pour l'extrusion et la révolution.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <set>
#include "model.h"

#include <planegcs/GCS.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

namespace forge {

static void axes (const Sketch &s, double x[3], double y[3])
{
	const double *n = s.plane.n;
	for (int i = 0; i < 3; i++) x[i] = s.plane.x[i];
	y[0] = n[1] * x[2] - n[2] * x[1], y[1] = n[2] * x[0] - n[0] * x[2], y[2] = n[0] * x[1] - n[1] * x[0];
}

void skWorld (const Sketch &s, double x, double y, double out[3])
{
	double X[3], Y[3];
	axes (s, X, Y);
	for (int i = 0; i < 3; i++) out[i] = s.plane.o[i] + x * X[i] + y * Y[i];
}

void skLocal (const Sketch &s, const double p[3], double &x, double &y)
{
	double X[3], Y[3], d[3] = { p[0] - s.plane.o[0], p[1] - s.plane.o[1], p[2] - s.plane.o[2] };
	axes (s, X, Y);
	x = d[0] * X[0] + d[1] * X[1] + d[2] * X[2];
	y = d[0] * Y[0] + d[1] * Y[1] + d[2] * Y[2];
}

const char *consName (SkConsType t)
{
	static const char *const N[] = { "Horizontale", "Verticale", "Coïncidence", "Parallèle", "Perpendiculaire", "Tangente",
		"Égal", "Distance", "Rayon", "Sur l'élément", "Concentrique", "Distance horizontale", "Distance verticale" };
	return N[t];
}

// ---- la résolution ----------------------------------------------------------------------------------------------

bool skSolve (Sketch &s)
{
	size_t np = s.pts.size (), ng = s.geos.size ();
	std::vector<double> P;
	P.reserve (np * 2 + ng * 3 + 1);	// (pas de réallocation : PlaneGCS garde des pointeurs)
	for (auto &p : s.pts) P.push_back (p.x), P.push_back (p.y);
	std::vector<int> ri (ng, -1), a0i (ng, -1), a1i (ng, -1);
	for (size_t g = 0; g < ng; g++) {
		const SkGeo &e = s.geos[g];
		if (e.t == SG_CIRCLE || e.t == SG_ARC) ri[g] = (int) P.size (), P.push_back (e.r);
		if (e.t == SG_ARC) a0i[g] = (int) P.size (), P.push_back (e.a0), a1i[g] = (int) P.size (), P.push_back (e.a1);
	}
	std::vector<GCS::Point> gp (np);
	for (size_t i = 0; i < np; i++) gp[i] = GCS::Point (&P[i * 2], &P[i * 2 + 1]);
	std::vector<GCS::Line> L (ng);
	std::vector<GCS::Circle> C (ng);
	std::vector<GCS::Arc> A (ng);
	GCS::System sys;
	for (size_t g = 0; g < ng; g++) {
		const SkGeo &e = s.geos[g];
		if (e.t == SG_LINE) L[g].p1 = gp[e.a], L[g].p2 = gp[e.b];
		else if (e.t == SG_CIRCLE) C[g].center = gp[e.c], C[g].rad = &P[ri[g]];
		else {
			A[g].center = gp[e.c], A[g].rad = &P[ri[g]];
			A[g].startAngle = &P[a0i[g]], A[g].endAngle = &P[a1i[g]];
			A[g].start = gp[e.a], A[g].end = gp[e.b];
			sys.addConstraintArcRules (A[g], 0);
		}
	}
	std::vector<double> V (s.cons.size () + 1);
	for (size_t k = 0; k < s.cons.size (); k++) {
		const SkCons &c = s.cons[k];
		int tag = (int) k + 1;
		V[k] = c.v;
		auto geo = [&] (int g) -> const SkGeo & { return s.geos[g]; };
		bool g1ok = c.g1 >= 0 && c.g1 < (int) ng, g2ok = c.g2 >= 0 && c.g2 < (int) ng;
		switch (c.t) {
		case SC_HORIZ:
			if (g1ok && geo (c.g1).t == SG_LINE) sys.addConstraintHorizontal (L[c.g1], tag);
			else if (c.p1 >= 0 && c.p2 >= 0) sys.addConstraintHorizontal (gp[c.p1], gp[c.p2], tag);
			break;
		case SC_VERT:
			if (g1ok && geo (c.g1).t == SG_LINE) sys.addConstraintVertical (L[c.g1], tag);
			else if (c.p1 >= 0 && c.p2 >= 0) sys.addConstraintVertical (gp[c.p1], gp[c.p2], tag);
			break;
		case SC_COINCIDENT: sys.addConstraintP2PCoincident (gp[c.p1], gp[c.p2], tag); break;
		case SC_PARALLEL: sys.addConstraintParallel (L[c.g1], L[c.g2], tag); break;
		case SC_PERP: sys.addConstraintPerpendicular (L[c.g1], L[c.g2], tag); break;
		case SC_TANGENT: {
			SkGeoType t1 = geo (c.g1).t, t2 = geo (c.g2).t;
			int a = c.g1, b = c.g2;
			if (t1 != SG_LINE && t2 == SG_LINE) std::swap (a, b), std::swap (t1, t2);
			if (t1 == SG_LINE && t2 == SG_CIRCLE) sys.addConstraintTangent (L[a], C[b], tag);
			else if (t1 == SG_LINE && t2 == SG_ARC) sys.addConstraintTangent (L[a], A[b], tag);
			else if (t1 == SG_CIRCLE && t2 == SG_CIRCLE) sys.addConstraintTangent (C[a], C[b], tag);
			else if (t1 == SG_ARC && t2 == SG_ARC) sys.addConstraintTangent (A[a], A[b], tag);
			else if (t1 == SG_CIRCLE && t2 == SG_ARC) sys.addConstraintTangent (C[a], A[b], tag);
			else if (t1 == SG_ARC && t2 == SG_CIRCLE) sys.addConstraintTangent (C[b], A[a], tag);
			break;
		}
		case SC_EQUAL: {
			SkGeoType t1 = geo (c.g1).t, t2 = geo (c.g2).t;
			if (t1 == SG_LINE && t2 == SG_LINE) sys.addConstraintEqualLength (L[c.g1], L[c.g2], tag);
			else if (t1 == SG_CIRCLE && t2 == SG_CIRCLE) sys.addConstraintEqualRadius (C[c.g1], C[c.g2], tag);
			else if (t1 == SG_CIRCLE && t2 == SG_ARC) sys.addConstraintEqualRadius (C[c.g1], A[c.g2], tag);
			else if (t1 == SG_ARC && t2 == SG_CIRCLE) sys.addConstraintEqualRadius (C[c.g2], A[c.g1], tag);
			else if (t1 == SG_ARC && t2 == SG_ARC) sys.addConstraintEqualRadius (A[c.g1], A[c.g2], tag);
			break;
		}
		case SC_DIST:
			if (c.p1 >= 0 && c.p2 >= 0) sys.addConstraintP2PDistance (gp[c.p1], gp[c.p2], &V[k], tag);
			else if (c.p1 >= 0 && g1ok && geo (c.g1).t == SG_LINE) sys.addConstraintP2LDistance (gp[c.p1], L[c.g1], &V[k], tag);
			break;
		case SC_DIST_X: sys.addConstraintDifference (gp[c.p1].x, gp[c.p2].x, &V[k], tag); break;
		case SC_DIST_Y: sys.addConstraintDifference (gp[c.p1].y, gp[c.p2].y, &V[k], tag); break;
		case SC_RADIUS:
			if (geo (c.g1).t == SG_CIRCLE) sys.addConstraintCircleRadius (C[c.g1], &V[k], tag);
			else sys.addConstraintArcRadius (A[c.g1], &V[k], tag);
			break;
		case SC_POINT_ON: {
			SkGeoType t1 = geo (c.g1).t;
			if (t1 == SG_LINE) sys.addConstraintPointOnLine (gp[c.p1], L[c.g1], tag);
			else if (t1 == SG_CIRCLE) sys.addConstraintPointOnCircle (gp[c.p1], C[c.g1], tag);
			else sys.addConstraintPointOnArc (gp[c.p1], A[c.g1], tag);
			break;
		}
		case SC_CONCENTRIC: sys.addConstraintP2PCoincident (gp[geo (c.g1).c], gp[geo (c.g2).c], tag); break;
		}
		(void) g2ok;
	}
	GCS::VEC_pD unknowns;
	for (size_t i = 2; i < P.size (); i++) unknowns.push_back (&P[i]);	// (le point 0, l'origine, est fixe)
	sys.declareUnknowns (unknowns);
	sys.initSolution ();
	int st = sys.solve (true, GCS::DogLeg);
	bool ok = st == GCS::Success || st == GCS::Converged;
	if (ok) sys.applySolution ();
	else sys.undoSolution ();
	sys.diagnose ();
	s.dof = sys.dofsNumber ();
	GCS::VEC_I conf, red;
	sys.getConflicting (conf);
	sys.getRedundant (red);
	s.conflict.clear (), s.redundant.clear ();
	for (int t : conf) if (t > 0) s.conflict.push_back (t - 1);
	for (int t : red) if (t > 0) s.redundant.push_back (t - 1);
	s.failed = !ok || !s.conflict.empty ();
	if (ok) {
		for (size_t i = 0; i < np; i++) s.pts[i].x = P[i * 2], s.pts[i].y = P[i * 2 + 1];
		for (size_t g = 0; g < ng; g++) {
			if (ri[g] >= 0) s.geos[g].r = P[ri[g]];
			if (a0i[g] >= 0) s.geos[g].a0 = P[a0i[g]], s.geos[g].a1 = P[a1i[g]];
		}
	}
	// ce qui peut encore bouger : un paramètre libre (dépendant, pour PlaneGCS) dans l'élément
	GCS::VEC_pD dep;
	sys.getDependentParams (dep);
	std::set<double *> depS (dep.begin (), dep.end ());
	s.free.assign (ng, 0);
	auto isFree = [&] (int idx) { return idx >= 0 && depS.count (&P[idx]); };
	for (size_t g = 0; g < ng; g++) {
		const SkGeo &e = s.geos[g];
		bool f = isFree (ri[g]) || isFree (a0i[g]) || isFree (a1i[g]);
		for (int pt : { e.a, e.b, e.c })
			if (pt >= 0) f = f || isFree (pt * 2) || isFree (pt * 2 + 1);
		s.free[g] = f;
	}
	return ok;
}

// ---- les éléments -----------------------------------------------------------------------------------------------

int skAddPoint (Sketch &s, double x, double y)
{
	s.pts.push_back ({ x, y });
	return (int) s.pts.size () - 1;
}

int skAddLine (Sketch &s, int a, int b)
{
	SkGeo g;
	g.t = SG_LINE, g.a = a, g.b = b;
	s.geos.push_back (g);
	return (int) s.geos.size () - 1;
}

int skAddCircle (Sketch &s, int c, double r)
{
	SkGeo g;
	g.t = SG_CIRCLE, g.c = c, g.r = r;
	s.geos.push_back (g);
	return (int) s.geos.size () - 1;
}

int skAddArc (Sketch &s, int c, int a, int b)
{
	SkGeo g;
	g.t = SG_ARC, g.c = c, g.a = a, g.b = b;
	const SkPoint &C = s.pts[c], &P = s.pts[a], &Q = s.pts[b];
	g.r = std::hypot (P.x - C.x, P.y - C.y);
	g.a0 = atan2 (P.y - C.y, P.x - C.x);
	g.a1 = atan2 (Q.y - C.y, Q.x - C.x);
	while (g.a1 <= g.a0) g.a1 += 2 * M_PI;
	s.pts[b].x = C.x + g.r * cos (g.a1), s.pts[b].y = C.y + g.r * sin (g.a1);
	s.geos.push_back (g);
	return (int) s.geos.size () - 1;
}

// les points sans élément ni contrainte s'en vont (sauf l'origine), et les rangs sont refaits
static void compact (Sketch &s)
{
	std::vector<char> used (s.pts.size (), 0);
	used[0] = 1;
	for (auto &g : s.geos)
		for (int p : { g.a, g.b, g.c })
			if (p >= 0) used[p] = 1;
	std::vector<int> map (s.pts.size (), -1);
	std::vector<SkPoint> np;
	for (size_t i = 0; i < s.pts.size (); i++)
		if (used[i]) map[i] = (int) np.size (), np.push_back (s.pts[i]);
	s.pts = np;
	for (auto &g : s.geos) {
		if (g.a >= 0) g.a = map[g.a];
		if (g.b >= 0) g.b = map[g.b];
		if (g.c >= 0) g.c = map[g.c];
	}
	std::vector<SkCons> nc;
	for (auto c : s.cons) {
		if ((c.p1 >= 0 && map[c.p1] < 0) || (c.p2 >= 0 && map[c.p2] < 0)) continue;
		if (c.p1 >= 0) c.p1 = map[c.p1];
		if (c.p2 >= 0) c.p2 = map[c.p2];
		nc.push_back (c);
	}
	s.cons = nc;
}

void skRemoveGeo (Sketch &s, int g)
{
	if (g < 0 || g >= (int) s.geos.size ()) return;
	std::vector<SkCons> nc;
	for (auto c : s.cons) {
		if (c.g1 == g || c.g2 == g) continue;
		if (c.g1 > g) c.g1--;
		if (c.g2 > g) c.g2--;
		nc.push_back (c);
	}
	s.cons = nc;
	s.geos.erase (s.geos.begin () + g);
	compact (s);
}

void skRemoveCons (Sketch &s, int k)
{
	if (k >= 0 && k < (int) s.cons.size ()) s.cons.erase (s.cons.begin () + k);
}

bool skAxis (const Sketch &s, double o[3], double d[3])
{
	for (auto &g : s.geos)
		if (g.t == SG_LINE && g.cons) {
			double p[3], q[3];
			skWorld (s, s.pts[g.a].x, s.pts[g.a].y, p);
			skWorld (s, s.pts[g.b].x, s.pts[g.b].y, q);
			double l = std::hypot (std::hypot (q[0] - p[0], q[1] - p[1]), q[2] - p[2]);
			if (l < 1e-9) continue;
			for (int i = 0; i < 3; i++) o[i] = p[i], d[i] = (q[i] - p[i]) / l;
			return true;
		}
	return false;
}

// ---- les contours en faces --------------------------------------------------------------------------------------

// les contours fermés de l'esquisse en faces planes, avec leur aire -> faux (err) si aucun
struct ClosedFace { TopoDS_Face f; double area; };
static bool closedFaces (const Sketch &s, std::vector<ClosedFace> &faces, std::string &err)
{
	gp_Dir n (s.plane.n[0], s.plane.n[1], s.plane.n[2]), x (s.plane.x[0], s.plane.x[1], s.plane.x[2]);
	Handle (TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape, wires;
	std::vector<TopoDS_Wire> closed;
	for (auto &g : s.geos) {
		if (g.cons) continue;
		double c[3], p[3], q[3];
		if (g.t == SG_LINE) {
			skWorld (s, s.pts[g.a].x, s.pts[g.a].y, p);
			skWorld (s, s.pts[g.b].x, s.pts[g.b].y, q);
			if (std::hypot (std::hypot (q[0] - p[0], q[1] - p[1]), q[2] - p[2]) < 1e-7) continue;
			edges->Append (BRepBuilderAPI_MakeEdge (gp_Pnt (p[0], p[1], p[2]), gp_Pnt (q[0], q[1], q[2])).Edge ());
		} else {
			skWorld (s, s.pts[g.c].x, s.pts[g.c].y, c);
			if (g.r < 1e-7) continue;
			gp_Circ circ (gp_Ax2 (gp_Pnt (c[0], c[1], c[2]), n, x), g.r);
			if (g.t == SG_CIRCLE) {
				TopoDS_Edge e = BRepBuilderAPI_MakeEdge (circ).Edge ();
				closed.push_back (BRepBuilderAPI_MakeWire (e).Wire ());
			} else
				edges->Append (BRepBuilderAPI_MakeEdge (circ, g.a0, g.a1).Edge ());
		}
	}
	if (edges->Length ()) {
		ShapeAnalysis_FreeBounds::ConnectEdgesToWires (edges, 1e-4, false, wires);
		for (int i = 1; i <= wires->Length (); i++) {
			TopoDS_Wire w = TopoDS::Wire (wires->Value (i));
			if (BRep_Tool::IsClosed (w)) closed.push_back (w);
		}
	}
	if (closed.empty ()) {
		err = "aucun contour fermé";
		return false;
	}
	for (auto &w : closed) {
		BRepBuilderAPI_MakeFace mf (gp_Pln (gp_Pnt (s.plane.o[0], s.plane.o[1], s.plane.o[2]), n), w, true);
		if (!mf.IsDone ()) continue;
		GProp_GProps g;
		BRepGProp::SurfaceProperties (mf.Face (), g);
		faces.push_back ({ mf.Face (), std::fabs (g.Mass ()) });
	}
	// (de la plus grande à la plus petite)
	std::stable_sort (faces.begin (), faces.end (), [] (const ClosedFace &a, const ClosedFace &b) { return a.area > b.area; });
	if (faces.empty ()) {
		err = "aucun contour fermé";
		return false;
	}
	return true;
}
TopoDS_Shape skFaces (const Sketch &s, std::string &err)
{
	std::vector<ClosedFace> faces;
	if (!closedFaces (s, faces, err)) return TopoDS_Shape ();
	// les faces, de la plus grande à la plus petite ; une face dans une plus grande en fait un trou
	std::vector<TopoDS_Shape> keep;
	std::vector<char> hole (faces.size (), 0);
	for (size_t i = 0; i < faces.size (); i++) {
		if (hole[i]) continue;
		TopoDS_Shape f = faces[i].f;
		for (size_t j = i + 1; j < faces.size (); j++) {
			if (hole[j]) continue;
			TopExp_Explorer v (faces[j].f, TopAbs_VERTEX);
			if (!v.More ()) continue;
			gp_Pnt p = BRep_Tool::Pnt (TopoDS::Vertex (v.Current ()));
			BRepClass_FaceClassifier cl (TopoDS::Face (faces[i].f), p, 1e-6);
			if (cl.State () == TopAbs_IN) f = BRepAlgoAPI_Cut (f, faces[j].f).Shape (), hole[j] = 1;
		}
		keep.push_back (f);
	}
	if (keep.size () == 1) return keep[0];
	TopoDS_Compound comp;
	BRep_Builder bb;
	bb.MakeCompound (comp);
	for (auto &f : keep) bb.Add (comp, f);
	return comp;
}

// ---- les régions : ce qu'on choisit d'extruder ------------------------------------------------------------------
//
// Chaque contour fermé a pour parent le plus petit contour qui le contient (un de ses sommets dedans) ; sa région est
// sa face moins les faces de ses enfants directs. Un rectangle percé d'un cercle : deux régions, le rectangle troué
// (profondeur 0) et le disque (1) ; un îlot dans le trou : une troisième (2). Le profil entier d'avant (skFaces) est
// la réunion des régions de profondeur 0.

static TopoDS_Shape compound (const std::vector<TopoDS_Shape> &v)
{
	if (v.size () == 1) return v[0];
	TopoDS_Compound comp;
	BRep_Builder bb;
	bb.MakeCompound (comp);
	for (auto &f : v) bb.Add (comp, f);
	return comp;
}

// un point bien dans la face (du plan de l'esquisse) : le centre du plus grand de ses triangles
static bool insidePoint (const Sketch &s, const TopoDS_Shape &face, double &x, double &y)
{
	BRepMesh_IncrementalMesh mesh (face, 0.05, false, 0.3);
	double best = -1, c[3] = { 0, 0, 0 };
	for (TopExp_Explorer e (face, TopAbs_FACE); e.More (); e.Next ()) {
		TopLoc_Location loc;
		Handle (Poly_Triangulation) tr = BRep_Tool::Triangulation (TopoDS::Face (e.Current ()), loc);
		if (tr.IsNull ()) continue;
		for (int i = 1; i <= tr->NbTriangles (); i++) {
			int a, b, d;
			tr->Triangle (i).Get (a, b, d);
			gp_Pnt A = tr->Node (a).Transformed (loc), B = tr->Node (b).Transformed (loc), D = tr->Node (d).Transformed (loc);
			double ar = gp_Vec (A, B).Crossed (gp_Vec (A, D)).Magnitude ();
			if (ar > best) best = ar, c[0] = (A.X () + B.X () + D.X ()) / 3, c[1] = (A.Y () + B.Y () + D.Y ()) / 3, c[2] = (A.Z () + B.Z () + D.Z ()) / 3;
		}
	}
	if (best <= 0) return false;
	skLocal (s, c, x, y);
	return true;
}

bool skRegions (const Sketch &s, std::vector<SkRegion> &out, std::string &err)
{
	out.clear ();
	std::vector<ClosedFace> faces;
	if (!closedFaces (s, faces, err)) return false;
	std::vector<int> parent (faces.size (), -1);
	for (size_t j = 1; j < faces.size (); j++) {
		TopExp_Explorer v (faces[j].f, TopAbs_VERTEX);
		if (!v.More ()) continue;
		gp_Pnt p = BRep_Tool::Pnt (TopoDS::Vertex (v.Current ()));
		for (size_t i = j; i-- > 0;) {	// (le plus petit qui le contient : le premier en remontant)
			BRepClass_FaceClassifier cl (faces[i].f, p, 1e-6);
			if (cl.State () == TopAbs_IN) {
				parent[j] = (int) i;
				break;
			}
		}
	}
	for (size_t i = 0; i < faces.size (); i++) {
		SkRegion r;
		r.face = faces[i].f;
		r.depth = parent[i] < 0 ? 0 : out[parent[i]].depth + 1;	// (le parent est avant : plus grand)
		r.area = faces[i].area;
		for (size_t j = i + 1; j < faces.size (); j++)
			if (parent[j] == (int) i) r.face = BRepAlgoAPI_Cut (r.face, faces[j].f).Shape (), r.area -= faces[j].area;
		if (!insidePoint (s, r.face, r.ix, r.iy)) {	// (rien dedans : un contour couvert par ses enfants)
			double c[3];
			GProp_GProps g;
			BRepGProp::SurfaceProperties (faces[i].f, g);
			c[0] = g.CentreOfMass ().X (), c[1] = g.CentreOfMass ().Y (), c[2] = g.CentreOfMass ().Z ();
			skLocal (s, c, r.ix, r.iy);
		}
		out.push_back (r);
	}
	return true;
}

int skRegionAt (const Sketch &s, const std::vector<SkRegion> &r, double x, double y)
{
	double w[3];
	skWorld (s, x, y, w);
	gp_Pnt p (w[0], w[1], w[2]);
	for (size_t i = r.size (); i-- > 0;)	// (les plus petites d'abord : un îlot avant le contour qui l'entoure)
		for (TopExp_Explorer f (r[i].face, TopAbs_FACE); f.More (); f.Next ()) {
			BRepClass_FaceClassifier cl (TopoDS::Face (f.Current ()), p, 1e-6);
			if (cl.State () == TopAbs_IN) return (int) i;
		}
	return -1;
}

bool skRegionDefault (const SkRegion &r) { return r.depth == 0; }

TopoDS_Shape skFacesPick (const Sketch &s, const std::vector<double> &picks, std::string &err)
{
	if (picks.size () < 2) return skFaces (s, err);
	std::vector<SkRegion> r;
	if (!skRegions (s, r, err)) return TopoDS_Shape ();
	std::vector<char> in (r.size (), 0);
	std::vector<TopoDS_Shape> keep;
	for (size_t k = 0; k + 1 < picks.size (); k += 2) {
		int i = skRegionAt (s, r, picks[k], picks[k + 1]);
		if (i < 0) {
			err = "plus la région choisie (l'esquisse a changé) : choisissez-la de nouveau";
			return TopoDS_Shape ();
		}
		if (!in[i]) in[i] = 1, keep.push_back (r[i].face);
	}
	if (keep.size () == 1) return keep[0];
	// plusieurs régions : réunies (un contour et son trou, choisis tous les deux, donnent la face pleine)
	TopoDS_Shape u = keep[0];
	for (size_t k = 1; k < keep.size (); k++) {
		BRepAlgoAPI_Fuse fu (u, keep[k]);
		if (!fu.IsDone ()) return compound (keep);
		u = fu.Shape ();
	}
	ShapeUpgrade_UnifySameDomain un (u, true, true, false);
	un.Build ();
	return un.Shape ();
}

} // namespace forge
