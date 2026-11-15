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
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
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

TopoDS_Shape skFaces (const Sketch &s, std::string &err)
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
		return TopoDS_Shape ();
	}
	// les faces, de la plus grande à la plus petite ; une face dans une plus grande en fait un trou
	struct F { TopoDS_Face f; double area; };
	std::vector<F> faces;
	for (auto &w : closed) {
		BRepBuilderAPI_MakeFace mf (gp_Pln (gp_Pnt (s.plane.o[0], s.plane.o[1], s.plane.o[2]), n), w, true);
		if (!mf.IsDone ()) continue;
		GProp_GProps g;
		BRepGProp::SurfaceProperties (mf.Face (), g);
		faces.push_back ({ mf.Face (), std::fabs (g.Mass ()) });
	}
	std::sort (faces.begin (), faces.end (), [] (const F &a, const F &b) { return a.area > b.area; });
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

} // namespace forge
