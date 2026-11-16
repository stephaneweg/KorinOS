// caotest : l'essai du moteur de la CAO de Korin (docs/DESIGN-CAO.md, « Construction et résultats ») :
// OpenCASCADE et PlaneGCS ensemble, chaque étape chronométrée.
//
//   caotest [dossier]       (défaut /tmp) : écrit caotest.step, caotest.igs et caotest.stl dans le dossier
//
// 1. Esquisse PlaneGCS : rectangle 60 × 40 (coin fixé à l'origine, horizontales, verticales, coïncidences, cotes)
//    et deux trous de rayon 5 (cotes aux bords, rayons égaux, centres alignés), à partir d'un dessin faux ;
//    diagnostic (degrés de liberté = 0), puis une cote contradictoire que le diagnostic doit désigner.
// 2. Boîte 60 × 40 × 30, congé sur une arête droite.
// 3. Booléen : union avec un cylindre penché de 20° ; congé sur l'arête d'intersection (une ellipse : l'arête
//    « quelconque » que 3DForge ne savait pas arrondir).
// 4. Booléen : les deux trous de l'esquisse, extrudés, retirés de la pièce.
// 5. Contrôle (BRepCheck), volume ; export STEP, relu (même volume) ; IGES ; maillage et STL.
// Code de sortie 0 si tout passe.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_CurveType.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <planegcs/GCS.h>

static int failures = 0;
static double t_last;

static double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void step(const char* what, bool ok, const std::string& detail = "")
{
    double t = now();
    std::printf("%-4s %-46s %8.1f ms  %s\n", ok ? "ok" : "ECHEC", what, (t - t_last) * 1000.0, detail.c_str());
    std::fflush(stdout);
    if (!ok) {
        failures++;
    }
    t_last = now();
}

static double volume(const TopoDS_Shape& s)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return p.Mass();
}

static bool valid(const TopoDS_Shape& s)
{
    BRepCheck_Analyzer a(s);
    return a.IsValid();
}

static std::string fmt(const char* f, double a, double b = 0, double c = 0)
{
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

// ---- 1. l'esquisse ----------------------------------------------------------------------------------------------
struct Sketch
{
    double hx = 10, hy = 20, r = 5;  // le premier trou (résolu)
    double h2x = 50;                 // le second trou
    double w = 60, h = 40;
};

static bool sketch(Sketch& out)
{
    // les paramètres : un dessin à main levée, faux exprès
    std::vector<double> v = {
        0,  0,             // p0 : le coin fixé (pas une inconnue)
        55, 3,  57, -2,    // L0 (p0 → a) ; b
        58, 35, 61, 44,    // c ; d
        -3, 38, 2,  41,    // e ; f
        1,  -1,            // g
        12, 17, 7,         // C1 : centre, rayon
        47, 25, 4,         // C2
    };
    double* P = v.data();
    double cw = 60, ch = 40, cr = 5, d10 = 10, d20 = 20, wrong = 6;
    GCS::Point p0(P + 0, P + 1), a(P + 2, P + 3), b(P + 4, P + 5), c(P + 6, P + 7), d(P + 8, P + 9),
        e(P + 10, P + 11), f(P + 12, P + 13), g(P + 14, P + 15);
    GCS::Line L0, L1, L2, L3;
    L0.p1 = p0, L0.p2 = a;
    L1.p1 = b, L1.p2 = c;
    L2.p1 = d, L2.p2 = e;
    L3.p1 = f, L3.p2 = g;
    GCS::Circle C1, C2;
    C1.center = GCS::Point(P + 16, P + 17), C1.rad = P + 18;
    C2.center = GCS::Point(P + 19, P + 20), C2.rad = P + 21;

    GCS::System sys;
    int tag = 1;
    sys.addConstraintP2PCoincident(a, b, tag++);
    sys.addConstraintP2PCoincident(c, d, tag++);
    sys.addConstraintP2PCoincident(e, f, tag++);
    sys.addConstraintP2PCoincident(g, p0, tag++);
    sys.addConstraintHorizontal(L0, tag++);
    sys.addConstraintVertical(L1, tag++);
    sys.addConstraintHorizontal(L2, tag++);
    sys.addConstraintVertical(L3, tag++);
    sys.addConstraintP2PDistance(p0, a, &cw, tag++);
    sys.addConstraintP2PDistance(b, c, &ch, tag++);
    sys.addConstraintCircleRadius(C1, &cr, tag++);
    sys.addConstraintP2LDistance(C1.center, L3, &d10, tag++);
    sys.addConstraintP2LDistance(C1.center, L0, &d20, tag++);
    sys.addConstraintEqualRadius(C1, C2, tag++);
    sys.addConstraintP2LDistance(C2.center, L1, &d10, tag++);
    sys.addConstraintHorizontal(C1.center, C2.center, tag++);
    int ncons = tag - 1;
    GCS::VEC_pD unknowns;
    for (size_t i = 2; i < v.size(); i++) {
        unknowns.push_back(&v[i]);
    }
    sys.declareUnknowns(unknowns);
    sys.initSolution();
    int st = sys.solve(true, GCS::DogLeg);
    sys.applySolution();
    sys.diagnose();
    int dofs = sys.dofsNumber();
    bool ok = (st == GCS::Success) && std::fabs(v[2] - 60) < 1e-6 && std::fabs(v[3]) < 1e-6
           && std::fabs(v[7] - 40) < 1e-6 && std::fabs(v[16] - 10) < 1e-6 && std::fabs(v[17] - 20) < 1e-6
           && std::fabs(v[18] - 5) < 1e-6 && std::fabs(v[19] - 50) < 1e-6 && std::fabs(v[20] - 20) < 1e-6
           && std::fabs(v[21] - 5) < 1e-6;
    step("esquisse : résolution (DogLeg)", ok,
         fmt("%.0f contraintes, coin opposé (%.3f, %.3f)", ncons, v[6], v[7]) + fmt(", trou 2 (%.3f, %.3f)", v[19], v[20]));
    step("esquisse : diagnostic", dofs == 0 && !sys.hasConflicting() && !sys.hasRedundant(),
         fmt("degrés de liberté %.0f", dofs));
    out.hx = v[16], out.hy = v[17], out.r = v[18], out.h2x = v[19], out.w = v[2], out.h = v[7];

    // une cote contradictoire (rayon du trou 2 = 6, alors que les rayons sont égaux et C1 = 5)
    int bad = ncons + 1;
    sys.addConstraintCircleRadius(C2, &wrong, bad);
    sys.declareUnknowns(unknowns);
    sys.initSolution();
    sys.solve(true, GCS::DogLeg);
    sys.diagnose();
    GCS::VEC_I conf;
    sys.getConflicting(conf);
    bool named = false;
    std::string list;
    for (int t : conf) {
        named = named || t == bad;
        list += (list.empty() ? "" : ",") + std::to_string(t);
    }
    step("esquisse : cote contradictoire désignée", sys.hasConflicting() && named, "conflit : " + list);
    return ok;
}

// ---- 2 à 5. le solide ----------------------------------------------------------------------------------------------
static TopoDS_Edge edgeOfType(const TopoDS_Shape& s, GeomAbs_CurveType want, double zmin)
{
    for (TopExp_Explorer x(s, TopAbs_EDGE); x.More(); x.Next()) {
        TopoDS_Edge e = TopoDS::Edge(x.Current());
        BRepAdaptor_Curve c(e);
        if (c.GetType() != want) {
            continue;
        }
        gp_Pnt m = c.Value((c.FirstParameter() + c.LastParameter()) / 2);
        if (m.Z() >= zmin) {
            return e;
        }
    }
    return TopoDS_Edge();
}

static const char* curveName(GeomAbs_CurveType t)
{
    switch (t) {
        case GeomAbs_Line: return "droite";
        case GeomAbs_Circle: return "cercle";
        case GeomAbs_Ellipse: return "ellipse";
        case GeomAbs_BSplineCurve: return "B-spline";
        case GeomAbs_BezierCurve: return "Bézier";
        default: return "autre";
    }
}

static void solid(const Sketch& sk, const std::string& dir)
{
    // 2. la boîte, congé sur une arête droite (l'arête verticale en x = 60, y = 40)
    TopoDS_Shape box = BRepPrimAPI_MakeBox(sk.w, sk.h, 30.0).Shape();
    step("boîte 60 × 40 × 30", !box.IsNull() && valid(box), fmt("volume %.1f", volume(box)));
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(box, TopAbs_EDGE, edges);
    TopoDS_Edge vert;
    for (int i = 1; i <= edges.Extent(); i++) {
        BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
        gp_Pnt p0 = c.Value(c.FirstParameter()), p1 = c.Value(c.LastParameter());
        if (std::fabs(p0.X() - sk.w) < 1e-9 && std::fabs(p0.Y() - sk.h) < 1e-9 && std::fabs(p1.X() - sk.w) < 1e-9) {
            vert = TopoDS::Edge(edges(i));
        }
    }
    BRepFilletAPI_MakeFillet f1(box);
    f1.Add(6.0, vert);
    f1.Build();
    TopoDS_Shape part = f1.IsDone() ? f1.Shape() : box;
    step("congé r = 6 sur une arête droite", f1.IsDone() && valid(part), fmt("volume %.2f", volume(part)));

    // 3. union avec un cylindre penché, puis congé sur l'arête d'intersection
    gp_Dir axis(0, std::sin(20 * M_PI / 180), std::cos(20 * M_PI / 180));
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, 20, 10), axis), 7.0, 35.0).Shape();
    BRepAlgoAPI_Fuse fuse(part, cyl);
    part = fuse.Shape();
    step("booléen : union avec un cylindre penché de 20°", fuse.IsDone() && valid(part), fmt("volume %.2f", volume(part)));
    TopoDS_Edge inter = edgeOfType(part, GeomAbs_Ellipse, 29.0);
    if (inter.IsNull()) {
        inter = edgeOfType(part, GeomAbs_BSplineCurve, 29.0);
    }
    std::string kind = inter.IsNull() ? "introuvable" : curveName(BRepAdaptor_Curve(inter).GetType());
    BRepFilletAPI_MakeFillet f2(part);
    bool ok2 = false;
    if (!inter.IsNull()) {
        f2.Add(2.5, inter);
        f2.Build();
        ok2 = f2.IsDone();
    }
    if (ok2) {
        part = f2.Shape();
    }
    step("congé r = 2,5 sur l'arête d'intersection", ok2 && valid(part), "arête : " + kind + fmt(", volume %.2f", volume(part)));

    // 4. les deux trous de l'esquisse, extrudés sur toute la hauteur, retirés
    TopoDS_Shape holes;
    for (double x : {sk.hx, sk.h2x}) {
        gp_Circ circ(gp_Ax2(gp_Pnt(x, sk.hy, -1), gp::DZ()), sk.r);
        TopoDS_Wire w = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(circ).Edge()).Wire();
        TopoDS_Face face = BRepBuilderAPI_MakeFace(w).Face();
        TopoDS_Shape pr = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 32)).Shape();
        holes = holes.IsNull() ? pr : BRepAlgoAPI_Fuse(holes, pr).Shape();
    }
    BRepAlgoAPI_Cut cut(part, holes);
    part = cut.Shape();
    double vol = volume(part);
    step("booléen : trous de l'esquisse retirés", cut.IsDone() && valid(part), fmt("volume %.2f", vol));

    // 5. STEP (écrit, relu), IGES, STL
    std::string stp = dir + "/caotest.step", igs = dir + "/caotest.igs", stl = dir + "/caotest.stl";
    STEPControl_Writer sw;
    Interface_Static::SetCVal("write.step.schema", "AP214IS");
    bool okw = sw.Transfer(part, STEPControl_AsIs) == IFSelect_RetDone && sw.Write(stp.c_str()) == IFSelect_RetDone;
    step("export STEP (AP214)", okw, stp);
    STEPControl_Reader sr;
    bool okr = sr.ReadFile(stp.c_str()) == IFSelect_RetDone && sr.TransferRoots() > 0;
    double v2 = okr ? volume(sr.OneShape()) : 0;
    step("STEP relu, même volume", okr && std::fabs(v2 - vol) < 1e-6 * vol, fmt("volume relu %.2f", v2));
    IGESControl_Controller::Init();
    IGESControl_Writer iw("MM", 1);
    bool oki = iw.AddShape(part) && iw.Write(igs.c_str());
    step("export IGES", oki, igs);
    BRepMesh_IncrementalMesh mesh(part, 0.05, false, 0.2);
    int tris = 0;
    for (TopExp_Explorer x(part, TopAbs_FACE); x.More(); x.Next()) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(TopoDS::Face(x.Current()), loc);
        if (!t.IsNull()) {
            tris += t->NbTriangles();
        }
    }
    step("maillage (flèche 0,05 mm)", mesh.IsDone() && tris > 0, fmt("%.0f triangles", tris));
    StlAPI_Writer stlw;
    stlw.ASCIIMode() = false;
    step("export STL binaire", stlw.Write(part, stl.c_str()), stl);
}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "/tmp";
    std::printf("caotest : OCCT %s, PlaneGCS de FreeCAD 1.1.4, Eigen 5.0.1\n", OCC_VERSION_COMPLETE);
    // (les statistiques des transferts STEP et IGES, en couleur sur la sortie : retirées)
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
    double t0 = now();
    t_last = t0;
    Sketch sk;
    try {
        sketch(sk);
        solid(sk, dir);
    }
    catch (const Standard_Failure& e) {
        step("exception OCCT", false, e.what());
    }
    catch (const std::exception& e) {
        step("exception C++", false, e.what());
    }
    std::printf("caotest : %s, %d échec(s), %.0f ms en tout\n", failures ? "ECHEC" : "PASSE", failures, (now() - t0) * 1000);
    return failures ? 1 : 0;
}
