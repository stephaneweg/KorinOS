// agent.cpp -- Forge : la topologie décrite, les mesures, l'export, l'insertion d'une étape (agent.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "agent.h"

#include <algorithm>
#include <cmath>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <NCollection_IndexedMap.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

namespace forge {

typedef NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> ShapeMap;

static void put (double out[3], const gp_XYZ &p) { out[0] = p.X (), out[1] = p.Y (), out[2] = p.Z (); }

void describeFaces (const Body &b, std::vector<FaceDesc> &out)
{
	out.clear ();
	if (b.shape.IsNull ()) return;
	ShapeMap m;
	TopExp::MapShapes (b.shape, TopAbs_FACE, m);
	for (int i = 1; i <= m.Extent (); i++) {
		FaceDesc d;
		d.index = i - 1;
		TopoDS_Face f = TopoDS::Face (m (i));
		try {
			GProp_GProps g;
			BRepGProp::SurfaceProperties (f, g);
			d.area = g.Mass ();
			put (d.center, g.CentreOfMass ().XYZ ());
			BRepAdaptor_Surface a (f);
			switch (a.GetType ()) {
			case GeomAbs_Plane: {
				d.type = "plane", d.planar = true;
				gp_Dir n = a.Plane ().Axis ().Direction ();
				if (f.Orientation () == TopAbs_REVERSED) n.Reverse ();
				put (d.normal, n.XYZ ());
				break;
			}
			case GeomAbs_Cylinder:
				d.type = "cylinder", d.radius = a.Cylinder ().Radius ();
				put (d.axis, a.Cylinder ().Axis ().Direction ().XYZ ());
				break;
			case GeomAbs_Cone: d.type = "cone", put (d.axis, a.Cone ().Axis ().Direction ().XYZ ()); break;
			case GeomAbs_Sphere: d.type = "sphere", d.radius = a.Sphere ().Radius (); break;
			case GeomAbs_Torus: d.type = "torus", d.radius = a.Torus ().MajorRadius (); break;
			case GeomAbs_BSplineSurface: case GeomAbs_BezierSurface: d.type = "bspline"; break;
			default: d.type = "other"; break;
			}
		} catch (const Standard_Failure &) {
			d.type = "other";
		}
		out.push_back (d);
	}
}

void describeEdges (const Body &b, std::vector<EdgeDesc> &out)
{
	out.clear ();
	if (b.shape.IsNull ()) return;
	ShapeMap m;
	TopExp::MapShapes (b.shape, TopAbs_EDGE, m);
	for (int i = 1; i <= m.Extent (); i++) {
		EdgeDesc d;
		d.index = i - 1;
		TopoDS_Edge e = TopoDS::Edge (m (i));
		try {
			GProp_GProps g;
			BRepGProp::LinearProperties (e, g);
			d.length = g.Mass ();
			BRepAdaptor_Curve c (e);
			double t0 = c.FirstParameter (), t1 = c.LastParameter ();
			gp_Pnt p;
			gp_Vec v;
			c.D1 ((t0 + t1) / 2, p, v);
			put (d.mid, p.XYZ ());
			if (v.Magnitude () > 1e-12) put (d.tangent, v.Normalized ().XYZ ());
			put (d.start, c.Value (t0).XYZ ());
			put (d.end, c.Value (t1).XYZ ());
			switch (c.GetType ()) {
			case GeomAbs_Line: d.type = "line"; break;
			case GeomAbs_Circle:
				d.type = "circle", d.radius = c.Circle ().Radius ();
				put (d.center, c.Circle ().Location ().XYZ ());
				break;
			case GeomAbs_Ellipse: d.type = "ellipse"; break;
			case GeomAbs_BSplineCurve: case GeomAbs_BezierCurve: d.type = "bspline"; break;
			default: d.type = "other"; break;
			}
		} catch (const Standard_Failure &) {
			d.type = "other";
		}
		out.push_back (d);
	}
}

void measureBody (const Body &b, Measure &out)
{
	out = Measure ();
	if (b.shape.IsNull ()) return;
	try {
		GProp_GProps g;
		BRepGProp::VolumeProperties (b.shape, g);
		out.volume = g.Mass ();
		put (out.com, g.CentreOfMass ().XYZ ());
		GProp_GProps s;
		BRepGProp::SurfaceProperties (b.shape, s);
		out.area = s.Mass ();
		Bnd_Box bb;
		BRepBndLib::Add (b.shape, bb);
		if (!bb.IsVoid ()) bb.Get (out.lo[0], out.lo[1], out.lo[2], out.hi[0], out.hi[1], out.hi[2]);
		ShapeMap m;
		TopExp::MapShapes (b.shape, TopAbs_SOLID, m);
		out.solids = m.Extent ();
		m.Clear ();
		TopExp::MapShapes (b.shape, TopAbs_FACE, m);
		out.faces = m.Extent ();
		m.Clear ();
		TopExp::MapShapes (b.shape, TopAbs_EDGE, m);
		out.edges = m.Extent ();
		out.valid = out.solids > 0 && BRepCheck_Analyzer (b.shape).IsValid ();
	} catch (const Standard_Failure &) {
	}
}

std::string exportBodies (const std::vector<const Body *> &bodies, const std::string &path, const std::string &format)
{
	BRep_Builder bb;
	TopoDS_Compound c;
	bb.MakeCompound (c);
	int n = 0;
	for (const Body *b : bodies)
		if (b && !b->shape.IsNull ()) bb.Add (c, b->shape), n++;
	if (!n) return "aucun corps à exporter";
	TopoDS_Shape s = c;
	if (n == 1)
		for (const Body *b : bodies)
			if (b && !b->shape.IsNull ()) s = b->shape;
	try {
		if (format == "step") {
			STEPControl_Writer w;
			Interface_Static::SetCVal ("write.step.schema", "AP214IS");
			if (w.Transfer (s, STEPControl_AsIs) != IFSelect_RetDone) return "STEP : le transfert a échoué";
			if (w.Write (path.c_str ()) != IFSelect_RetDone) return "STEP : le fichier n'a pas pu être écrit";
		} else if (format == "iges") {
			IGESControl_Controller::Init ();
			IGESControl_Writer w ("MM", 1);
			if (!w.AddShape (s)) return "IGES : le transfert a échoué";
			if (!w.Write (path.c_str ())) return "IGES : le fichier n'a pas pu être écrit";
		} else if (format == "stl") {
			BRepMesh_IncrementalMesh mesh (s, 0.02, false, 0.2);
			StlAPI_Writer w;
			w.ASCIIMode () = false;
			if (!w.Write (s, path.c_str ())) return "STL : le fichier n'a pas pu être écrit";
		} else if (format == "brep") {
			if (!BRepTools::Write (s, path.c_str ())) return "BREP : le fichier n'a pas pu être écrit";
		} else
			return "format inconnu : " + format + " (step, iges, stl, brep)";
	} catch (const Standard_Failure &x) {
		return std::string ("OpenCASCADE : ") + x.what ();
	}
	return "";
}

void insertFeature (Doc &d, int at, const Feature &f)
{
	if (at < 0) at = 0;
	if (at > (int) d.feats.size ()) at = (int) d.feats.size ();
	for (auto &g : d.feats)
		if (g.ref >= at) g.ref++;
	d.feats.insert (d.feats.begin () + at, f);
	if (d.bar >= at) d.bar++;
}

void eraseFeature (Doc &d, int at)
{
	if (at < 0 || at >= (int) d.feats.size ()) return;
	d.feats.erase (d.feats.begin () + at);
	for (auto &g : d.feats)
		if (g.ref == at) g.ref = -1;		// (son esquisse ou son étape répétée n'est plus : elle passera en erreur)
		else if (g.ref > at) g.ref--;
	if (d.bar > at) d.bar--;
}

// les corps créés par chaque étape, toutes jouées (même désactivées ou après la barre) : rang de création -> étape
static std::vector<int> creatorsOf (const Doc &d)
{
	Doc p;
	p.feats = d.feats;
	p.params = d.params;
	for (auto &f : p.feats) f.off = false;
	p.bar = (int) p.feats.size ();
	replay (p, 2.0);
	return p.bodyOf;
}

Removal planRemoval (const Doc &d, int at)
{
	Removal r;
	if (at < 0 || at >= (int) d.feats.size ()) return r;
	std::vector<int> made = creatorsOf (d);
	std::vector<char> dead (made.size (), 0), gone (d.feats.size (), 0);
	auto kill = [&] (int j) {
		gone[j] = 1;
		r.erased.push_back (j);
		for (size_t b = 0; b < made.size (); b++)
			if (made[b] == j) dead[b] = 1;
	};
	auto isDead = [&] (int b) { return b >= 0 && b < (int) dead.size () && dead[b]; };
	kill (at);
	for (int j = at + 1; j < (int) d.feats.size (); j++) {
		const Feature &f = d.feats[j];
		bool refGone = f.ref >= 0 && f.ref < (int) gone.size () && gone[f.ref];
		bool bodyDead = isDead (f.body);
		switch (f.kind) {
		case ST_SKETCH: break;		// (son plan reste ; seule l'opération choisie pour l'extrusion changera)
		case ST_EXTRUDE: case ST_REVOLVE:
			if (refGone) kill (j);
			else if (bodyDead && f.op != OP_NEW) r.renewed.push_back (j);
			break;
		case ST_BOX: case ST_CYLINDER:
			if (bodyDead && f.op != OP_NEW) r.renewed.push_back (j);
			break;
		case ST_PATTERN:
			if (refGone) kill (j);
			break;
		case ST_COMBINE: case ST_CUT: {
			int left = 0, lost = 0;
			for (int t : f.tools) (isDead (t) ? lost : left)++;
			if (bodyDead || left == 0) kill (j);
			else if (lost) r.trimmed.push_back (j);
			break;
		}
		default:			// (congé, chanfrein, pousser/tirer, perçage, oblong, coque, symétrie)
			if (bodyDead) kill (j);
			break;
		}
	}
	return r;
}

void applyRemoval (Doc &d, const Removal &plan)
{
	if (plan.erased.empty ()) return;
	std::vector<int> made = creatorsOf (d);
	auto has = [] (const std::vector<int> &v, int j) { return std::find (v.begin (), v.end (), j) != v.end (); };
	std::vector<char> dead (made.size (), 0);
	for (size_t b = 0; b < made.size (); b++) dead[b] = has (plan.erased, made[b]);
	std::vector<int> newIdx (d.feats.size (), -1);
	Doc n;
	n.name = d.name;
	n.params = d.params;
	for (int j = 0; j < (int) d.feats.size (); j++) {
		if (has (plan.erased, j)) continue;
		Feature f = d.feats[j];
		if (has (plan.renewed, j)) f.op = OP_NEW, f.body = -1;
		if (f.kind == ST_COMBINE || f.kind == ST_CUT) {
			std::vector<int> keep;
			for (int t : f.tools) if (t < 0 || t >= (int) dead.size () || !dead[t]) keep.push_back (t);
			f.tools = keep;
		}
		if (f.kind == ST_SKETCH && f.sk.body >= 0 && f.sk.body < (int) dead.size () && dead[f.sk.body]) f.sk.body = -1;
		newIdx[j] = (int) n.feats.size ();
		if (j < d.bar) n.bar++;
		n.feats.push_back (f);
	}
	for (auto &f : n.feats)
		if (f.ref >= 0) f.ref = f.ref < (int) newIdx.size () ? newIdx[f.ref] : -1;
	// les corps : le k-ième corps d'une étape gardée garde son rang parmi les corps qu'elle crée maintenant
	std::vector<int> made2 = creatorsOf (n);
	std::vector<int> rank (made.size (), -1);
	for (size_t b = 0; b < made.size (); b++) {
		int c = made[b];
		if (c < 0 || c >= (int) newIdx.size () || newIdx[c] < 0) continue;
		int k = 0;
		for (size_t e = 0; e < b; e++) k += made[e] == c;
		for (size_t e = 0; e < made2.size (); e++)
			if (made2[e] == newIdx[c] && k-- == 0) { rank[b] = (int) e; break; }
	}
	auto remap = [&] (int b) { return b >= 0 && b < (int) rank.size () ? rank[b] : -1; };
	for (auto &f : n.feats) {
		if (f.body >= 0) f.body = remap (f.body);
		for (auto &t : f.tools) t = remap (t);
		if (f.sk.body >= 0) f.sk.body = remap (f.sk.body);
	}
	d.feats = n.feats;
	d.bar = n.bar;
}

} // namespace forge
