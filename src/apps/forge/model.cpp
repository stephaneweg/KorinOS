// model.cpp -- Forge : la pièce d'exemple, le maillage et les mesures (model.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "model.h"

#include <cmath>
#include <cstdio>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

namespace forge {

typedef NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> ShapeMap;

static std::string fmt (const char *f, double a, double b = 0, double c = 0)
{
	char buf[128];
	snprintf (buf, sizeof buf, f, a, b, c);
	std::string s = buf;
	for (auto &ch : s) if (ch == '.') ch = ',';	// (les nombres à la française)
	return s;
}

// ---- la pièce d'exemple ---------------------------------------------------------------------------------------------

// les arêtes droites de s parallèles à l'axe ax (0 x, 1 y, 2 z) dont le milieu est près de (p[i] pour i != ax)
static std::vector<TopoDS_Edge> linesAt (const TopoDS_Shape &s, int ax, double a, double b)
{
	std::vector<TopoDS_Edge> out;
	for (TopExp_Explorer x (s, TopAbs_EDGE); x.More (); x.Next ()) {
		TopoDS_Edge e = TopoDS::Edge (x.Current ());
		BRepAdaptor_Curve c (e);
		if (c.GetType () != GeomAbs_Line) continue;
		gp_Pnt p0 = c.Value (c.FirstParameter ()), p1 = c.Value (c.LastParameter ());
		double d[3] = { p1.X () - p0.X (), p1.Y () - p0.Y (), p1.Z () - p0.Z () };
		double m[3] = { (p0.X () + p1.X ()) / 2, (p0.Y () + p1.Y ()) / 2, (p0.Z () + p1.Z ()) / 2 };
		int o1 = (ax + 1) % 3, o2 = (ax + 2) % 3;
		if (std::fabs (d[o1]) > 1e-6 || std::fabs (d[o2]) > 1e-6) continue;
		double w1 = ax == 1 ? b : a, w2 = ax == 1 ? a : b;	// (a, b : les deux autres coordonnées, dans l'ordre x, y, z)
		if (std::fabs (m[o1] - w1) < 1e-6 && std::fabs (m[o2] - w2) < 1e-6) {
			bool dup = false;
			for (auto &k : out) dup = dup || k.IsSame (e);
			if (!dup) out.push_back (e);
		}
	}
	return out;
}

static TopoDS_Shape fillet (const TopoDS_Shape &s, const std::vector<TopoDS_Edge> &edges, double r)
{
	BRepFilletAPI_MakeFillet f (s);
	for (auto &e : edges) f.Add (r, e);
	f.Build ();
	if (!f.IsDone ()) throw Standard_Failure ("congé");
	return f.Shape ();
}

static TopoDS_Shape cyl (double x, double y, double z, gp_Dir dir, double r, double h)
{
	return BRepPrimAPI_MakeCylinder (gp_Ax2 (gp_Pnt (x, y, z), dir), r, h).Shape ();
}

static TopoDS_Shape unify (const TopoDS_Shape &s)
{
	ShapeUpgrade_UnifySameDomain u (s, true, true, false);
	u.Build ();
	return u.Shape ();
}

bool buildSample (Doc &d, std::string &err)
{
	try {
		d.name = "Équerre";
		d.steps.clear ();
		d.bodies.clear ();
		// la plaque, ses deux coins avant arrondis
		TopoDS_Shape p = BRepPrimAPI_MakeBox (90.0, 60.0, 8.0).Shape ();
		d.steps.push_back ({ ST_BOX, "Boîte 1", "90 × 60 × 8" });
		std::vector<TopoDS_Edge> e = linesAt (p, 2, 0, 0), e2 = linesAt (p, 2, 90, 0);
		e.insert (e.end (), e2.begin (), e2.end ());
		p = fillet (p, e, 8.0);
		d.steps.push_back ({ ST_FILLET, "Congé 1", "R 8 · 2 arêtes" });
		// la paroi, jointe
		TopoDS_Shape w = BRepPrimAPI_MakeBox (gp_Pnt (0, 52, 8), gp_Pnt (90, 60, 45)).Shape ();
		p = unify (BRepAlgoAPI_Fuse (p, w).Shape ());
		d.steps.push_back ({ ST_BOX, "Boîte 2", "90 × 8 × 37 · joindre" });
		// ses coins du haut, puis le raccord avec la plaque
		e = linesAt (p, 1, 0, 45);
		e2 = linesAt (p, 1, 90, 45);
		e.insert (e.end (), e2.begin (), e2.end ());
		p = fillet (p, e, 12.0);
		d.steps.push_back ({ ST_FILLET, "Congé 2", "R 12 · 2 arêtes" });
		p = fillet (p, linesAt (p, 0, 52, 8), 6.0);
		d.steps.push_back ({ ST_FILLET, "Congé 3", "R 6 · 1 arête" });
		// le trou de la paroi
		p = BRepAlgoAPI_Cut (p, cyl (45, 50, 30, gp::DY (), 11.0, 12.0)).Shape ();
		d.steps.push_back ({ ST_CYLINDER, "Cylindre 1", "Ø 22 · enlever" });
		// deux lamages : Ø 9 de part en part, Ø 14 sur 3 mm
		for (double x : { 15.0, 75.0 }) {
			p = BRepAlgoAPI_Cut (p, cyl (x, 22, -1, gp::DZ (), 4.5, 10.0)).Shape ();
			p = BRepAlgoAPI_Cut (p, cyl (x, 22, 5, gp::DZ (), 7.0, 4.0)).Shape ();
		}
		d.steps.push_back ({ ST_HOLE, "Perçage 1", "Ø 9 / Ø 14 · 2 lamages" });
		// l'oblong 20 × 9
		TopoDS_Shape slot = BRepPrimAPI_MakeBox (gp_Pnt (40, 21.5, -1), gp_Pnt (50, 30.5, 9)).Shape ();
		slot = BRepAlgoAPI_Fuse (slot, cyl (40, 26, -1, gp::DZ (), 4.5, 10.0)).Shape ();
		slot = BRepAlgoAPI_Fuse (slot, cyl (50, 26, -1, gp::DZ (), 4.5, 10.0)).Shape ();
		p = unify (BRepAlgoAPI_Cut (p, slot).Shape ());
		d.steps.push_back ({ ST_EXTRUDE, "Extrusion 1", "oblong 20 × 9 · à travers" });
		Body b;
		b.name = "Équerre";
		b.shape = p;
		b.color = 0;
		d.bodies.push_back (b);
		return true;
	} catch (const Standard_Failure &x) {
#if OCC_VERSION_HEX >= 0x080000
		err = x.what ();
#else
		err = x.GetMessageString ();	// (l'OCCT de Linux, pour les essais)
#endif
	} catch (const std::exception &x) {
		err = x.what ();
	}
	return false;
}

// ---- le maillage --------------------------------------------------------------------------------------------------

static void grow (Mesh &m, float x, float y, float z)
{
	if (x < m.lo[0]) m.lo[0] = x;
	if (y < m.lo[1]) m.lo[1] = y;
	if (z < m.lo[2]) m.lo[2] = z;
	if (x > m.hi[0]) m.hi[0] = x;
	if (y > m.hi[1]) m.hi[1] = y;
	if (z > m.hi[2]) m.hi[2] = z;
}

int meshBody (Body &b, double deflection)
{
	Mesh &m = b.mesh;
	m = Mesh ();
	m.lo[0] = m.lo[1] = m.lo[2] = 1e30f;
	m.hi[0] = m.hi[1] = m.hi[2] = -1e30f;
	BRepMesh_IncrementalMesh mesher (b.shape, deflection, false, 0.35, true);
	ShapeMap faces, edges;
	TopExp::MapShapes (b.shape, TopAbs_FACE, faces);
	TopExp::MapShapes (b.shape, TopAbs_EDGE, edges);
	m.nfaces = faces.Extent ();
	m.nedges = edges.Extent ();
	for (int fi = 1; fi <= faces.Extent (); fi++) {
		m.faceFirst.push_back ((int) m.triFace.size ());
		TopoDS_Face f = TopoDS::Face (faces (fi));
		TopLoc_Location loc;
		Handle (Poly_Triangulation) t = BRep_Tool::Triangulation (f, loc);
		if (t.IsNull ()) continue;
		if (!t->HasNormals ()) BRepLib_ToolTriangulatedShape::ComputeNormals (f, t);
		gp_Trsf tr = loc.Transformation ();
		bool rev = f.Orientation () == TopAbs_REVERSED;
		int base = (int) m.pts.size () / 3;
		for (int i = 1; i <= t->NbNodes (); i++) {
			gp_Pnt p = t->Node (i).Transformed (tr);
			gp_Dir n = t->Normal (i).Transformed (tr);
			if (rev) n.Reverse ();
			m.pts.insert (m.pts.end (), { (float) p.X (), (float) p.Y (), (float) p.Z () });
			m.nrm.insert (m.nrm.end (), { (float) n.X (), (float) n.Y (), (float) n.Z () });
			grow (m, (float) p.X (), (float) p.Y (), (float) p.Z ());
		}
		for (int i = 1; i <= t->NbTriangles (); i++) {
			int a, c, e;
			t->Triangle (i).Get (a, c, e);
			if (rev) std::swap (c, e);
			m.tris.insert (m.tris.end (), { base + a - 1, base + c - 1, base + e - 1 });
			m.triFace.push_back (fi - 1);
		}
	}
	m.faceFirst.push_back ((int) m.triFace.size ());
	for (int ei = 1; ei <= edges.Extent (); ei++) {
		m.edgeFirst.push_back ((int) m.edgePts.size () / 3);
		TopoDS_Edge e = TopoDS::Edge (edges (ei));
		if (BRep_Tool::Degenerated (e)) continue;
		bool seam = false;		// (la couture d'un cylindre : pas une arête qu'on montre)
		for (int fi = 1; fi <= faces.Extent () && !seam; fi++) {
			for (TopExp_Explorer x (faces (fi), TopAbs_EDGE); x.More (); x.Next ())
				if (x.Current ().IsSame (e)) { seam = BRep_Tool::IsClosed (e, TopoDS::Face (faces (fi))); break; }
		}
		if (seam) continue;
		BRepAdaptor_Curve c (e);
		GCPnts_TangentialDeflection d (c, 0.2, deflection);
		for (int i = 1; i <= d.NbPoints (); i++) {
			gp_Pnt p = d.Value (i);
			m.edgePts.insert (m.edgePts.end (), { (float) p.X (), (float) p.Y (), (float) p.Z () });
		}
	}
	m.edgeFirst.push_back ((int) m.edgePts.size () / 3);
	return (int) m.triFace.size ();
}

// ---- les mesures --------------------------------------------------------------------------------------------------

static TopoDS_Shape nth (const TopoDS_Shape &s, TopAbs_ShapeEnum k, int i)
{
	ShapeMap m;
	TopExp::MapShapes (s, k, m);
	return i >= 0 && i < m.Extent () ? m (i + 1) : TopoDS_Shape ();
}

void faceInfo (const Body &b, int face, Info &out)
{
	out = Info ();
	TopoDS_Shape s = nth (b.shape, TopAbs_FACE, face);
	if (s.IsNull ()) return;
	TopoDS_Face f = TopoDS::Face (s);
	BRepAdaptor_Surface a (f);
	GProp_GProps g;
	BRepGProp::SurfaceProperties (f, g);
	Bnd_Box bb;
	BRepBndLib::Add (f, bb);
	double x0, y0, z0, x1, y1, z1;
	bb.Get (x0, y0, z0, x1, y1, z1);
	out.sub = b.name + " · face " + std::to_string (face + 1);
	out.rows.push_back ({ "Aire", fmt ("%.1f mm²", g.Mass ()) });
	switch (a.GetType ()) {
	case GeomAbs_Plane: {
		out.title = "Face plane";
		gp_Dir n = a.Plane ().Axis ().Direction ();
		if (f.Orientation () == TopAbs_REVERSED) n.Reverse ();
		const char *axn[3] = { "X", "Y", "Z" };
		double c[3] = { n.X (), n.Y (), n.Z () };
		std::string ns;
		for (int i = 0; i < 3; i++)
			if (std::fabs (std::fabs (c[i]) - 1) < 1e-9) ns = std::string (c[i] < 0 ? "-" : "+") + axn[i];
		if (ns.empty ()) ns = fmt ("%.2f, %.2f, %.2f", c[0], c[1], c[2]);
		out.rows.push_back ({ "Normale", ns });
		gp_Pnt o = a.Plane ().Location ();
		for (int i = 0; i < 3; i++)
			if (ns == std::string ("+") + axn[i] || ns == std::string ("-") + axn[i])
				out.rows.push_back ({ "Plan", std::string (axn[i]) + fmt (" = %.2f mm", i == 0 ? o.X () : i == 1 ? o.Y () : o.Z ()) });
		break;
	}
	case GeomAbs_Cylinder:
		out.title = "Face cylindrique";
		out.rows.push_back ({ "Diamètre", fmt ("%.2f mm", 2 * a.Cylinder ().Radius ()) });
		break;
	case GeomAbs_Cone: out.title = "Face conique"; break;
	case GeomAbs_Sphere: out.title = "Face sphérique"; break;
	case GeomAbs_Torus: out.title = "Face torique"; break;
	default: out.title = "Face"; break;
	}
	out.rows.push_back ({ "Taille", fmt ("%.1f × %.1f × %.1f mm", x1 - x0, y1 - y0, z1 - z0) });
}

void edgeInfo (const Body &b, int edge, Info &out)
{
	out = Info ();
	TopoDS_Shape s = nth (b.shape, TopAbs_EDGE, edge);
	if (s.IsNull ()) return;
	TopoDS_Edge e = TopoDS::Edge (s);
	BRepAdaptor_Curve c (e);
	GProp_GProps g;
	BRepGProp::LinearProperties (e, g);
	out.sub = b.name + " · arête " + std::to_string (edge + 1);
	switch (c.GetType ()) {
	case GeomAbs_Line: out.title = "Arête droite"; break;
	case GeomAbs_Circle:
		out.title = "Arête circulaire";
		out.rows.push_back ({ "Diamètre", fmt ("%.2f mm", 2 * c.Circle ().Radius ()) });
		break;
	case GeomAbs_Ellipse: out.title = "Arête elliptique"; break;
	case GeomAbs_BSplineCurve: out.title = "Arête B-spline"; break;
	default: out.title = "Arête"; break;
	}
	out.rows.insert (out.rows.begin (), { "Longueur", fmt ("%.2f mm", g.Mass ()) });
}

void bodyInfo (const Body &b, Info &out)
{
	out = Info ();
	out.title = b.name;
	GProp_GProps v, s;
	BRepGProp::VolumeProperties (b.shape, v);
	BRepGProp::SurfaceProperties (b.shape, s);
	Bnd_Box bb;
	BRepBndLib::Add (b.shape, bb, false);
	double x0, y0, z0, x1, y1, z1;
	bb.Get (x0, y0, z0, x1, y1, z1);
	int solids = 0;
	for (TopExp_Explorer x (b.shape, TopAbs_SOLID); x.More (); x.Next ()) solids++;
	out.solidOk = solids == 1 && BRepCheck_Analyzer (b.shape).IsValid ();
	out.sub = "Corps · " + std::to_string (b.mesh.nfaces) + " faces, " + std::to_string (b.mesh.nedges) + " arêtes";
	out.rows.push_back ({ "Taille", fmt ("%.1f × %.1f × %.1f mm", x1 - x0, y1 - y0, z1 - z0) });
	out.rows.push_back ({ "Volume", fmt ("%.2f cm³", v.Mass () / 1000) });
	out.rows.push_back ({ "Surface", fmt ("%.1f cm²", s.Mass () / 100) });
	out.rows.push_back ({ "Triangles", fmt ("%.0f", (double) b.mesh.triFace.size ()) });
}

unsigned bodyColor (int i, bool dark)
{
	static const unsigned light[5] = { 0xA8B6C8, 0xC2C6CC, 0xD6B48A, 0x9DC0A0, 0xD9958C };
	static const unsigned noir[5] = { 0x8794A6, 0x9CA1A8, 0xB08F68, 0x7E9D82, 0xB4766E };
	return (dark ? noir : light)[((i % 5) + 5) % 5];
}

} // namespace forge
