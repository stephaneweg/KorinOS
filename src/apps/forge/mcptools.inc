// mcptools.inc -- Forge : les outils MCP (inclus dans main.cpp, dans son espace de noms : ils agissent sur le document,
// la vue et l'historique comme l'interface ; le transport est dans mcp.cpp).
//
// Le protocole : initialize, ping, tools/list, tools/call (JSON-RPC 2.0) ; les notifications sont acceptées sans
// réponse. Un outil qui change le document passe par le même chemin que l'interface : une opération (Feature) insérée
// à la barre de reconstruction, l'historique rejoué, annuler possible ; une opération refusée par le rejeu est retirée
// et l'erreur rendue. Les noms et descriptions des outils sont en anglais (ils s'adressent au modèle) ; les noms des
// opérations, des corps et les erreurs du rejeu restent ceux de Forge.
//
// Les numéros des faces et des arêtes sont ceux de get_body_topology (et de la vue) ; ils changent quand le corps
// change : relire la topologie après une opération. Les opérations gardent leurs faces et arêtes par leur géométrie.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.

// ---- petit JSON en sortie ---------------------------------------------------------------------------------------

std::string jq (const std::string &s)
{
	jbuf b;
	jb_init (&b);
	jb_strn (&b, s.data (), s.size ());
	std::string r (b.p, b.n);
	jb_free (&b);
	return r;
}
std::string jn (double v)
{
	if (!std::isfinite (v)) return "null";
	if (std::fabs (v) < 5e-10) v = 0;
	char t[40];
	snprintf (t, sizeof t, "%.10g", v);
	return t;
}
std::string jv (const double *v, int n = 3)
{
	std::string s = "[";
	for (int i = 0; i < n; i++) s += (i ? "," : "") + jn (v[i]);
	return s + "]";
}
struct JO {
	std::string s = "{";
	JO &raw (const char *k, const std::string &v)
	{
		if (s.size () > 1) s += ',';
		s += jq (k) + ':' + v;
		return *this;
	}
	JO &str (const char *k, const std::string &v) { return raw (k, jq (v)); }
	JO &num (const char *k, double v) { return raw (k, jn (v)); }
	JO &boo (const char *k, bool v) { return raw (k, v ? "true" : "false"); }
	std::string end () const { return s + "}"; }
};
struct JA {
	std::string s = "[";
	JA &add (const std::string &v)
	{
		if (s.size () > 1) s += ',';
		s += v;
		return *this;
	}
	std::string end () const { return s + "]"; }
};

// ---- les arguments ----------------------------------------------------------------------------------------------

struct ToolErr {
	std::string msg;
};
[[noreturn]] void fail (const std::string &m) { throw ToolErr { m }; }

struct Args {
	const json_t *o;
	const json_t *get (const char *k) const { return o ? json_get (o, k) : nullptr; }
	bool has (const char *k) const
	{
		const json_t *v = get (k);
		return v && v->kind != JSON_NULL;
	}
	// un nombre, ou une chaîne (formule, unités, paramètres nommés) ; f et field : la formule est gardée dans l'opération
	double num (const char *k, double def, bool req = false, Feature *f = nullptr, int field = -1) const
	{
		const json_t *v = get (k);
		if (!v || v->kind == JSON_NULL) {
			if (req) fail (std::string ("missing argument: ") + k);
			return def;
		}
		if (v->kind == JSON_NUM) return atof (v->s);
		if (v->kind == JSON_STR) {
			double r;
			if (!evalExpr (g_doc, v->s, r)) fail (std::string ("unreadable value or unknown parameter for ") + k + ": " + v->s);
			double plain;
			bool isPlain = evalExpr (v->s, plain) && !strpbrk (v->s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_");
			if (f && field >= 0 && !isPlain) f->expr.push_back ({ field, v->s });
			return r;
		}
		fail (std::string ("argument ") + k + " must be a number or an expression string");
	}
	int integer (const char *k, int def, bool req = false) const { return (int) std::lround (num (k, def, req)); }
	bool boolean (const char *k, bool def) const
	{
		const json_t *v = get (k);
		return v && v->kind == JSON_BOOL ? v->b != 0 : def;
	}
	std::string str (const char *k, const std::string &def = "", bool req = false) const
	{
		const json_t *v = get (k);
		if (!v || v->kind == JSON_NULL) {
			if (req) fail (std::string ("missing argument: ") + k);
			return def;
		}
		if (v->kind != JSON_STR && v->kind != JSON_NUM) fail (std::string ("argument ") + k + " must be a string");
		return v->s;
	}
	// un tableau de n nombres (n = 2 ou 3 : nmin..nmax) -> le nombre lu (0 : absent)
	int vec (const char *k, double *out, int nmin, int nmax, bool req = false) const { return vecOf (get (k), k, out, nmin, nmax, req); }
	static int vecOf (const json_t *v, const char *k, double *out, int nmin, int nmax, bool req = false)
	{
		if (!v || v->kind == JSON_NULL) {
			if (req) fail (std::string ("missing argument: ") + k);
			return 0;
		}
		if (v->kind != JSON_ARR || v->n < nmin || v->n > nmax) fail (std::string ("argument ") + k + " must be an array of " + std::to_string (nmin)
									  + (nmax > nmin ? "-" + std::to_string (nmax) : "") + " numbers");
		for (int i = 0; i < v->n; i++) {
			const json_t *x = json_at (v, i);
			if (x->kind == JSON_NUM) out[i] = atof (x->s);
			else if (x->kind == JSON_STR && evalExpr (g_doc, x->s, out[i])) {
			} else
				fail (std::string ("argument ") + k + ": not a number");
		}
		return v->n;
	}
	std::vector<int> ints (const char *k, bool req = false) const
	{
		const json_t *v = get (k);
		std::vector<int> r;
		if (!v || v->kind == JSON_NULL) {
			if (req) fail (std::string ("missing argument: ") + k);
			return r;
		}
		if (v->kind == JSON_NUM) return { atoi (v->s) };
		if (v->kind != JSON_ARR) fail (std::string ("argument ") + k + " must be an array of integers");
		for (int i = 0; i < v->n; i++) r.push_back ((int) json_int (json_at (v, i), -1));
		return r;
	}
};

// ---- le document ------------------------------------------------------------------------------------------------

const char *kindName (StepKind k)
{
	static const char *const N[] = { "box", "cylinder", "sketch", "extrude", "fillet", "chamfer", "combine", "cut", "hole", "push_pull", "slot",
					 "revolve", "shell", "pattern", "mirror" };
	return k >= 0 && k < (int) (sizeof N / sizeof *N) ? N[k] : "?";
}
const char *opKey (Op o) { return o == OP_NEW ? "new" : o == OP_JOIN ? "join" : o == OP_CUT ? "cut" : "intersect"; }
Op opOf (const std::string &s)
{
	if (s.empty () || s == "new") return OP_NEW;
	if (s == "join" || s == "union" || s == "add") return OP_JOIN;
	if (s == "cut" || s == "subtract" || s == "remove") return OP_CUT;
	if (s == "intersect" || s == "common") return OP_INTERSECT;
	fail ("operation must be new, join, cut or intersect");
}

int liveBody (int b)
{
	if (b < 0 || b >= (int) g_doc.bodies.size () || g_doc.bodies[b].shape.IsNull ())
		fail ("no body " + std::to_string (b) + " (get_document lists the bodies)");
	return b;
}
int faceOf (int b, int f)
{
	if (f < 0 || f >= g_doc.bodies[b].mesh.nfaces) fail ("body " + std::to_string (b) + " has no face " + std::to_string (f) + " (see get_body_topology)");
	return f;
}
int edgeOf (int b, int e)
{
	if (e < 0 || e >= g_doc.bodies[b].mesh.nedges) fail ("body " + std::to_string (b) + " has no edge " + std::to_string (e) + " (see get_body_topology)");
	return e;
}
int featureOf (int i)
{
	if (i < 0 || i >= (int) g_doc.feats.size ()) fail ("no feature " + std::to_string (i) + " (get_document lists the history)");
	return i;
}

std::string bodiesJson ()
{
	JA a;
	for (size_t i = 0; i < g_doc.bodies.size (); i++) {
		const Body &b = g_doc.bodies[i];
		if (b.shape.IsNull ()) continue;
		Measure m;
		measureBody (b, m);
		a.add (JO ().num ("index", (double) i).str ("name", b.name).boo ("visible", b.visible).num ("volume_mm3", m.volume).num ("area_mm2", m.area)
		       .raw ("bbox_min", jv (m.lo)).raw ("bbox_max", jv (m.hi)).raw ("center_of_mass", jv (m.com)).num ("faces", m.faces)
		       .num ("edges", m.edges).num ("solids", m.solids).boo ("valid_solid", m.valid)
		       .num ("created_by_feature", i < g_doc.bodyOf.size () ? g_doc.bodyOf[i] : -1).end ());
	}
	return a.end ();
}

std::string sketchJson (const Sketch &s)
{
	JA ge, co, pts;
	for (size_t i = 0; i < s.pts.size (); i++) {
		double p[2] = { s.pts[i].x, s.pts[i].y };
		pts.add (jv (p, 2));
	}
	for (size_t i = 0; i < s.geos.size (); i++) {
		const SkGeo &g = s.geos[i];
		JO o;
		o.num ("index", (double) i).boo ("construction", g.cons).boo ("can_move", i < s.free.size () && s.free[i]);
		if (g.t == SG_LINE) {
			double a[2] = { s.pts[g.a].x, s.pts[g.a].y }, b[2] = { s.pts[g.b].x, s.pts[g.b].y };
			o.str ("type", "line").raw ("from", jv (a, 2)).raw ("to", jv (b, 2)).num ("from_point", g.a).num ("to_point", g.b);
		} else {
			double c[2] = { s.pts[g.c].x, s.pts[g.c].y };
			o.str ("type", g.t == SG_CIRCLE ? "circle" : "arc").raw ("center", jv (c, 2)).num ("radius", g.r).num ("center_point", g.c);
			if (g.t == SG_ARC) {
				double a[2] = { s.pts[g.a].x, s.pts[g.a].y }, b[2] = { s.pts[g.b].x, s.pts[g.b].y };
				o.raw ("start", jv (a, 2)).raw ("end", jv (b, 2));
			}
		}
		ge.add (o.end ());
	}
	for (size_t k = 0; k < s.cons.size (); k++) {
		const SkCons &c = s.cons[k];
		JO o;
		o.num ("index", (double) k).str ("type", consName (c.t));
		JA el, pp;
		if (c.g1 >= 0) el.add (std::to_string (c.g1));
		if (c.g2 >= 0) el.add (std::to_string (c.g2));
		if (c.p1 >= 0) pp.add (std::to_string (c.p1));
		if (c.p2 >= 0) pp.add (std::to_string (c.p2));
		o.raw ("elements", el.end ()).raw ("points", pp.end ());
		if (c.t == SC_DIST || c.t == SC_DIST_X || c.t == SC_DIST_Y || c.t == SC_RADIUS) o.num ("value", c.v);
		bool bad = std::find (s.conflict.begin (), s.conflict.end (), (int) k) != s.conflict.end ();
		if (bad) o.boo ("conflict", true);
		co.add (o.end ());
	}
	double y[3] = { s.plane.n[1] * s.plane.x[2] - s.plane.n[2] * s.plane.x[1], s.plane.n[2] * s.plane.x[0] - s.plane.n[0] * s.plane.x[2],
			s.plane.n[0] * s.plane.x[1] - s.plane.n[1] * s.plane.x[0] };
	std::string err;
	TopoDS_Shape faces = skFaces (s, err);
	return JO ().raw ("plane_origin", jv (s.plane.o)).raw ("plane_normal", jv (s.plane.n)).raw ("plane_x", jv (s.plane.x)).raw ("plane_y", jv (y))
		.num ("on_body", s.body).num ("degrees_of_freedom", s.dof).boo ("fully_constrained", s.dof == 0)
		.boo ("has_closed_profile", !faces.IsNull ()).raw ("points", pts.end ()).raw ("elements", ge.end ()).raw ("constraints", co.end ()).end ();
}

std::string featureJson (int i, bool full)
{
	const Feature &f = g_doc.feats[i];
	JO o;
	o.num ("index", i).str ("name", f.name).str ("kind", kindName (f.kind)).str ("detail", featureDetail (f));
	if (f.kind != ST_SKETCH && f.kind != ST_FILLET && f.kind != ST_CHAMFER && f.kind != ST_SHELL && f.kind != ST_PUSH && f.kind != ST_COMBINE && f.kind != ST_CUT)
		o.str ("operation", opKey (f.op));
	if (f.kind == ST_COMBINE || f.kind == ST_CUT) {
		Op co = f.kind == ST_CUT ? OP_CUT : f.op == OP_NEW ? OP_JOIN : f.op;
		o.str ("operation", co == OP_CUT ? "cut" : co == OP_INTERSECT ? "intersect" : "union").boo ("keep_tools", f.keep);
	}
	if (f.body >= 0) o.num ("target_body", f.body);
	if (!f.tools.empty ()) {
		JA tb;
		for (int b : f.tools) tb.add (std::to_string (b));
		o.raw ("tool_bodies", tb.end ());
	}
	if (f.ref >= 0) o.num ("uses_feature", f.ref);
	o.boo ("rolled_back", i >= g_doc.bar).boo ("suppressed", f.off);
	if (!f.error.empty ()) o.str ("error", f.error);
	const char *names[3];
	double vals[3];
	int n = featureFields (f, names, vals);
	JA fl;
	for (int k = 0; k < n; k++) {
		JO x;
		x.num ("index", k).str ("name", names[k]).num ("value", vals[k]);
		for (auto &e : f.expr)
			if (e.first == k) x.str ("formula", e.second);
		fl.add (x.end ());
	}
	o.raw ("fields", fl.end ());
	if (full) {
		if (f.kind == ST_BOX || f.kind == ST_CYLINDER || f.kind == ST_HOLE || f.kind == ST_PATTERN || f.kind == ST_MIRROR)
			o.raw ("frame", JO ().raw ("origin", jv (f.frame.o)).raw ("normal", jv (f.frame.n)).raw ("x", jv (f.frame.x)).end ());
		if (f.kind == ST_SKETCH) o.raw ("sketch", sketchJson (f.sk));
		if (f.kind == ST_HOLE) {
			JA c;
			for (size_t k = 0; k + 1 < f.pos.size (); k += 2) {
				double p[2] = { f.pos[k], f.pos[k + 1] };
				c.add (jv (p, 2));
			}
			o.raw ("hole_positions_in_frame", c.end ());
		}
		if (f.kind == ST_PATTERN) o.str ("pattern_mode", f.mode ? "circular" : "linear").num ("count", f.count);
	}
	return o.end ();
}

std::string paramsJson ()
{
	JA a;
	for (auto &p : g_doc.params) {
		double v = 0;
		bool ok = evalExpr (g_doc, p.second.c_str (), v);
		JO o;
		o.str ("name", p.first).str ("expression", p.second);
		if (ok) o.num ("value", v);
		else o.str ("error", "unreadable or unknown parameter");
		a.add (o.end ());
	}
	return a.end ();
}

std::string documentJson (bool full)
{
	JA fs;
	for (size_t i = 0; i < g_doc.feats.size (); i++) fs.add (featureJson ((int) i, full));
	return JO ().str ("name", g_doc.name).str ("units", "mm").num ("rollback_bar", g_doc.bar).num ("feature_count", (double) g_doc.feats.size ())
		.raw ("features", fs.end ()).raw ("bodies", bodiesJson ()).raw ("parameters", paramsJson ()).num ("snap_mm", g_snap)
		.boo ("can_undo", !g_undo.empty ()).boo ("can_redo", !g_redo.empty ()).end ();
}

std::string errorsJson ()
{
	JA a;
	for (size_t i = 0; i < g_doc.feats.size (); i++)
		if (!g_doc.feats[i].error.empty ())
			a.add (JO ().num ("feature", (double) i).str ("name", g_doc.feats[i].name).str ("error", g_doc.feats[i].error).end ());
	return a.end ();
}

// ---- les changements --------------------------------------------------------------------------------------------

// l'interface laisse la main : une commande en cours est annulée ; une esquisse ouverte bloque (c'est l'utilisateur)
void agentTakes ()
{
	if (g_sm.on) fail ("the user is editing a sketch in Forge: ask them to finish or cancel it first");
	if (g_cmd.tool != T_NONE) cmdEnd ();
	g_selStep = -1;
}

// revient à l'état d'avant le dernier pushUndo (une opération refusée), sans rien laisser à rétablir
void dropLastUndo ()
{
	if (g_undo.empty ()) return;
	g_doc.feats = g_undo.back ().feats;
	g_doc.bar = g_undo.back ().bar;
	g_doc.params = g_undo.back ().params;
	g_undo.pop_back ();
	rebuild ();
}

std::string changed (int feature)
{
	JO o;
	if (feature >= 0) o.num ("feature", feature).str ("name", g_doc.feats[feature].name).str ("detail", featureDetail (g_doc.feats[feature]));
	return o.raw ("bodies", bodiesJson ()).raw ("features_in_error", errorsJson ()).end ();
}

// une opération ajoutée à la barre ; refusée par le rejeu : retirée, l'erreur rendue
std::string commitFeature (Feature f, const char *what)
{
	if (f.name.empty ()) f.name = featureName (g_doc, f.kind);
	int at = g_doc.bar;
	pushUndo ();
	insertFeature (g_doc, at, f);
	rebuild ();
	if (!g_doc.feats[at].error.empty ()) {
		std::string e = g_doc.feats[at].error;
		dropLastUndo ();
		refreshAll ();
		fail (std::string ("Forge refused the ") + what + ": " + e);
	}
	refreshAll ();
	status ((std::string (TR ("Agent : ")) + g_doc.feats[at].name + " · " + featureDetail (g_doc.feats[at])).c_str ());
	return changed (at);
}

// un repère : origin (le centre de la base), normal (+Z par défaut), x (facultatif) ; ou on_face {body, face}
Frame frameArg (const Args &a, bool needOrigin = true)
{
	Frame fr;
	const json_t *of = a.get ("on_face");
	if (of && of->kind == JSON_OBJ) {
		Args o { of };
		int b = liveBody (o.integer ("body", -1, true));
		int fc = faceOf (b, o.integer ("face", -1, true));
		if (!facePlane (g_doc.bodies[b], fc, fr)) fail ("on_face: the face is not planar");
	}
	double n[3];
	if (a.vec ("normal", n, 3, 3)) {
		double l = std::sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (l < 1e-9) fail ("normal is zero");
		for (int i = 0; i < 3; i++) fr.n[i] = n[i] / l;
		// x : horizontal pour un plan vertical, le long de X sinon (comme facePlane)
		if (std::fabs (fr.n[2]) < 0.9) {
			double x[3] = { -fr.n[1], fr.n[0], 0 }, lx = std::hypot (x[0], x[1]);
			for (int i = 0; i < 3; i++) fr.x[i] = x[i] / lx;
		} else {
			double x[3] = { 1 - fr.n[0] * fr.n[0], -fr.n[0] * fr.n[1], -fr.n[0] * fr.n[2] }, lx = std::sqrt (x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
			for (int i = 0; i < 3; i++) fr.x[i] = x[i] / lx;
		}
	}
	double x[3];
	if (a.vec ("x_direction", x, 3, 3)) {
		double d = x[0] * fr.n[0] + x[1] * fr.n[1] + x[2] * fr.n[2];
		for (int i = 0; i < 3; i++) x[i] -= d * fr.n[i];
		double l = std::sqrt (x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
		if (l < 1e-9) fail ("x_direction is parallel to the normal");
		for (int i = 0; i < 3; i++) fr.x[i] = x[i] / l;
	}
	double o[3];
	if (a.vec ("origin", o, 3, 3, needOrigin && !(of && of->kind == JSON_OBJ))) {
		if (of && of->kind == JSON_OBJ) {	// (projeté sur le plan de la face)
			double d = (o[0] - fr.o[0]) * fr.n[0] + (o[1] - fr.o[1]) * fr.n[1] + (o[2] - fr.o[2]) * fr.n[2];
			for (int i = 0; i < 3; i++) o[i] -= d * fr.n[i];
		}
		for (int i = 0; i < 3; i++) fr.o[i] = o[i];
	}
	return fr;
}

void opArgs (const Args &a, Feature &f)
{
	f.op = opOf (a.str ("operation", "new"));
	if (f.op != OP_NEW) f.body = liveBody (a.integer ("body", -1, true));
}

// ---- les outils -------------------------------------------------------------------------------------------------

typedef std::string (*ToolFn) (const Args &);
struct ToolDef {
	const char *name, *desc, *schema;
	ToolFn fn;
	bool readOnly;
};

std::string t_get_document (const Args &a) { return documentJson (a.boolean ("full", false)); }

std::string t_get_feature (const Args &a) { return featureJson (featureOf (a.integer ("feature", -1, true)), true); }

std::string t_get_body_topology (const Args &a)
{
	int b = liveBody (a.integer ("body", -1, true));
	std::vector<FaceDesc> fs;
	std::vector<EdgeDesc> es;
	describeFaces (g_doc.bodies[b], fs);
	describeEdges (g_doc.bodies[b], es);
	JA fa, ea;
	for (auto &f : fs) {
		JO o;
		o.num ("index", f.index).str ("type", f.type).num ("area_mm2", f.area).raw ("center", jv (f.center));
		if (f.planar) o.raw ("normal", jv (f.normal));
		if (f.radius > 0) o.num ("radius", f.radius);
		if (f.type == "cylinder" || f.type == "cone") o.raw ("axis", jv (f.axis));
		fa.add (o.end ());
	}
	for (auto &e : es) {
		JO o;
		o.num ("index", e.index).str ("type", e.type).num ("length", e.length).raw ("mid", jv (e.mid)).raw ("tangent", jv (e.tangent)).raw ("start", jv (e.start))
			.raw ("end", jv (e.end));
		if (e.type == "circle") o.num ("radius", e.radius).raw ("center", jv (e.center));
		ea.add (o.end ());
	}
	return JO ().num ("body", b).str ("name", g_doc.bodies[b].name).raw ("faces", fa.end ()).raw ("edges", ea.end ()).end ();
}

std::string t_add_box (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_BOX;
	f.frame = frameArg (a);
	f.a = a.num ("width", 0, true, &f, 0);
	f.b = a.num ("depth", 0, true, &f, 1);
	f.c = a.num ("height", 0, true, &f, 2);
	opArgs (a, f);
	f.name = a.str ("name");
	return commitFeature (f, "box");
}

std::string t_add_cylinder (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_CYLINDER;
	f.frame = frameArg (a);
	if (a.has ("radius")) f.a = a.num ("radius", 0);
	else f.a = a.num ("diameter", 0, true, &f, 0) / 2;
	f.c = a.num ("height", 0, true, &f, 1);
	opArgs (a, f);
	f.name = a.str ("name");
	return commitFeature (f, "cylinder");
}

std::string t_push_pull (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_PUSH;
	f.body = liveBody (a.integer ("body", -1, true));
	int fc = faceOf (f.body, a.integer ("face", -1, true));
	if (!faceRef (g_doc.bodies[f.body], fc, f.face)) fail ("push_pull needs a planar face");
	f.a = a.num ("distance", 0, true, &f, 0);
	f.name = a.str ("name");
	return commitFeature (f, "push/pull");
}

std::string edgesFeature (const Args &a, StepKind k, const char *valueKey, const char *what)
{
	agentTakes ();
	Feature f;
	f.kind = k;
	f.body = liveBody (a.integer ("body", -1, true));
	std::vector<int> es = a.ints ("edges", true);
	if (es.empty ()) fail ("edges is empty");
	std::vector<int> all;
	for (int e : es) {
		edgeOf (f.body, e);
		std::vector<int> ch = a.boolean ("tangent_chain", false) ? tangentChain (g_doc.bodies[f.body], e) : std::vector<int> { e };
		for (int c : ch)
			if (std::find (all.begin (), all.end (), c) == all.end ()) all.push_back (c);
	}
	for (int e : all) f.edges.push_back (edgeRef (g_doc.bodies[f.body], e));
	f.a = a.num (valueKey, 0, true, &f, 0);
	f.name = a.str ("name");
	try {
		return commitFeature (f, what);
	} catch (const ToolErr &x) {
		if (k != ST_FILLET) throw;
		double mx = filletMax (g_doc.bodies[f.body].shape, f.edges, f.a);
		fail (x.msg + (mx > 0 ? " (the largest radius that works on these edges is about " + fmtNum (mx) + " mm)" : ""));
	}
}
std::string t_fillet (const Args &a) { return edgesFeature (a, ST_FILLET, "radius", "fillet"); }
std::string t_chamfer (const Args &a) { return edgesFeature (a, ST_CHAMFER, "distance", "chamfer"); }

std::string t_add_hole (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_HOLE, f.op = OP_CUT;
	f.body = liveBody (a.integer ("body", -1, true));
	int fc = faceOf (f.body, a.integer ("face", -1, true));
	if (!facePlane (g_doc.bodies[f.body], fc, f.frame)) fail ("add_hole needs a planar face");
	FaceRef fr;
	if (faceRef (g_doc.bodies[f.body], fc, fr)) f.faces = { fr };	// (la face : le perçage la suit si elle bouge)
	const json_t *cs = a.get ("centers");
	if (!cs || cs->kind != JSON_ARR || !cs->n) fail ("centers must be a non-empty array of [x, y, z] points");
	const double *n = f.frame.n, *xa = f.frame.x;
	double ya[3] = { n[1] * xa[2] - n[2] * xa[1], n[2] * xa[0] - n[0] * xa[2], n[0] * xa[1] - n[1] * xa[0] };
	for (int i = 0; i < cs->n; i++) {
		double p[3];
		Args::vecOf (json_at (cs, i), "centers[]", p, 3, 3, true);
		double d0 = (p[0] - f.frame.o[0]) * n[0] + (p[1] - f.frame.o[1]) * n[1] + (p[2] - f.frame.o[2]) * n[2];
		for (int k = 0; k < 3; k++) p[k] -= d0 * n[k];	// (sur le plan de la face)
		if (i == 0)
			for (int k = 0; k < 3; k++) f.frame.o[k] = p[k];
		double d[3] = { p[0] - f.frame.o[0], p[1] - f.frame.o[1], p[2] - f.frame.o[2] };
		f.pos.push_back (d[0] * xa[0] + d[1] * xa[1] + d[2] * xa[2]);
		f.pos.push_back (d[0] * ya[0] + d[1] * ya[1] + d[2] * ya[2]);
	}
	f.d1 = a.num ("diameter", 0, true, &f, 0);
	f.c = a.num ("depth", 0, false, &f, 2);
	const json_t *cb = a.get ("counterbore");
	if (cb && cb->kind == JSON_OBJ) {
		Args c { cb };
		f.d2 = c.num ("diameter", 0, true);
		f.dep2 = c.num ("depth", 0, true);
		if (f.d2 <= f.d1) fail ("counterbore diameter must be larger than the hole diameter");
	}
	f.name = a.str ("name");
	return commitFeature (f, "hole");
}

std::string t_shell (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_SHELL;
	f.body = liveBody (a.integer ("body", -1, true));
	for (int fc : a.ints ("open_faces", true)) {
		FaceRef r;
		if (!faceRef (g_doc.bodies[f.body], faceOf (f.body, fc), r)) fail ("shell: open face " + std::to_string (fc) + " is not planar");
		f.faces.push_back (r);
	}
	if (f.faces.empty ()) fail ("open_faces is empty");
	f.a = a.num ("thickness", 0, true, &f, 0);
	f.name = a.str ("name");
	return commitFeature (f, "shell");
}

void axisArg (const Args &a, const char *k, double out[3], const double def[3])
{
	const json_t *v = a.get (k);
	for (int i = 0; i < 3; i++) out[i] = def[i];
	if (!v || v->kind == JSON_NULL) return;
	if (v->kind == JSON_STR) {
		std::string s = v->s;
		for (auto &c : s) c = (char) tolower (c);
		int ax = s == "x" ? 0 : s == "y" ? 1 : s == "z" ? 2 : -1;
		if (ax < 0) fail (std::string (k) + " must be x, y, z or [dx, dy, dz]");
		for (int i = 0; i < 3; i++) out[i] = i == ax;
		return;
	}
	Args::vecOf (v, k, out, 3, 3, true);
	double l = std::sqrt (out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (l < 1e-9) fail (std::string (k) + " is zero");
	for (int i = 0; i < 3; i++) out[i] /= l;
}

std::string t_pattern (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_PATTERN;
	f.ref = featureOf (a.integer ("feature", -1, true));
	if (f.ref >= g_doc.bar) fail ("the repeated feature is after the rollback bar");
	if (!repeatable (g_doc.feats[f.ref].kind)) fail ("only boxes, cylinders, holes, slots, extrusions and revolutions can be patterned");
	std::string mode = a.str ("mode", "linear");
	f.mode = mode == "circular" ? 1 : mode == "linear" ? 0 : -1;
	if (f.mode < 0) fail ("mode must be linear or circular");
	f.count = a.integer ("count", 0, true);
	static const double X[3] = { 1, 0, 0 }, Z[3] = { 0, 0, 1 };
	if (f.mode == 0) {
		axisArg (a, "direction", f.frame.n, X);
		f.a = a.num ("spacing", 0, true, &f, 1);
	} else {
		axisArg (a, "axis", f.frame.n, Z);
		a.vec ("center", f.frame.o, 3, 3, true);
		f.a = a.num ("angle", 360, false, &f, 1);
	}
	if (std::fabs (f.frame.n[2]) < 0.9) f.frame.x[0] = -f.frame.n[1], f.frame.x[1] = f.frame.n[0], f.frame.x[2] = 0;
	f.name = a.str ("name");
	return commitFeature (f, "pattern");
}

std::string t_mirror (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_MIRROR;
	f.body = liveBody (a.integer ("body", -1, true));
	if (a.has ("face")) {
		if (!facePlane (g_doc.bodies[f.body], faceOf (f.body, a.integer ("face", -1)), f.frame)) fail ("mirror: the face is not planar");
	} else {
		std::string p = a.str ("plane", "yz");
		for (auto &c : p) c = (char) tolower (c);
		static const double Z[3] = { 0, 0, 1 };
		if (p == "yz") f.frame.n[0] = 1, f.frame.n[2] = 0;
		else if (p == "xz") f.frame.n[1] = 1, f.frame.n[2] = 0;
		else if (p == "xy") {
		} else if (p == "custom") axisArg (a, "normal", f.frame.n, Z);
		else fail ("plane must be yz, xz, xy or custom (with normal), or give face");
		a.vec ("origin", f.frame.o, 3, 3);
	}
	f.op = a.boolean ("join", true) ? OP_JOIN : OP_NEW;
	f.name = a.str ("name");
	return commitFeature (f, "mirror");
}

std::string t_boolean (const Args &a)
{
	agentTakes ();
	Feature f;
	std::string op = a.str ("operation", "", true);
	f.kind = ST_COMBINE;
	if (op == "union" || op == "join") f.op = OP_JOIN;
	else if (op == "cut" || op == "subtract") f.op = OP_CUT;
	else if (op == "intersect" || op == "intersection" || op == "common") f.op = OP_INTERSECT;
	else fail ("operation must be union, cut or intersect");
	f.body = liveBody (a.integer ("target_body", -1, true));
	if (a.has ("tool_bodies"))
		for (int b : a.ints ("tool_bodies")) f.tools.push_back (liveBody (b));
	if (a.has ("tool_body")) f.tools.push_back (liveBody (a.integer ("tool_body", -1, true)));
	if (f.tools.empty ()) fail ("give tool_body or tool_bodies");
	for (int b : f.tools)
		if (b == f.body) fail ("target_body is also a tool body");
	f.keep = a.boolean ("keep_tools", false);
	f.name = a.str ("name");
	return commitFeature (f, op.c_str ());
}

// -- l'esquisse
int skPointArg (Sketch &s, const json_t *v, const char *k)
{
	double p[3];
	int n = Args::vecOf (v, k, p, 2, 3, true);
	double x = p[0], y = p[1];
	if (n == 3) skLocal (s, p, x, y);	// (un point de l'espace : projeté sur le plan)
	for (size_t i = 0; i < s.pts.size (); i++)
		if (std::fabs (s.pts[i].x - x) < 1e-6 && std::fabs (s.pts[i].y - y) < 1e-6) return (int) i;	// (le même point : partagé)
	return skAddPoint (s, x, y);
}

void skLineAuto (Sketch &s, int g, bool autoC)
{
	if (!autoC) return;
	const SkGeo &e = s.geos[g];
	double dx = s.pts[e.b].x - s.pts[e.a].x, dy = s.pts[e.b].y - s.pts[e.a].y;
	SkCons c;
	c.g1 = g;
	if (std::fabs (dy) < 1e-9 && std::fabs (dx) > 1e-9) c.t = SC_HORIZ, s.cons.push_back (c);
	else if (std::fabs (dx) < 1e-9 && std::fabs (dy) > 1e-9) c.t = SC_VERT, s.cons.push_back (c);
}

std::string t_create_sketch (const Args &a)
{
	agentTakes ();
	Feature f;
	f.kind = ST_SKETCH;
	Sketch &s = f.sk;
	const json_t *of = a.get ("on_face");
	if (of && of->kind == JSON_OBJ) {
		Args o { of };
		int b = liveBody (o.integer ("body", -1, true));
		if (!facePlane (g_doc.bodies[b], faceOf (b, o.integer ("face", -1, true)), s.plane)) fail ("on_face: the face is not planar");
		s.body = b;
		double org[3];
		if (a.vec ("origin", org, 3, 3)) {
			double d = (org[0] - s.plane.o[0]) * s.plane.n[0] + (org[1] - s.plane.o[1]) * s.plane.n[1] + (org[2] - s.plane.o[2]) * s.plane.n[2];
			for (int i = 0; i < 3; i++) s.plane.o[i] = org[i] - d * s.plane.n[i];
		}
	} else {
		std::string p = a.str ("plane", "xy");
		for (auto &c : p) c = (char) tolower (c);
		if (p == "xy") {
		} else if (p == "xz") s.plane.n[0] = 0, s.plane.n[1] = -1, s.plane.n[2] = 0;	// (x -> X, y -> Z)
		else if (p == "yz") s.plane.n[0] = 1, s.plane.n[2] = 0, s.plane.x[0] = 0, s.plane.x[1] = 1;	// (x -> Y, y -> Z)
		else fail ("plane must be xy, xz or yz (or give on_face)");
		double off = a.num ("offset", 0);
		for (int i = 0; i < 3; i++) s.plane.o[i] = off * s.plane.n[i];
		a.vec ("origin", s.plane.o, 3, 3);
	}
	bool autoC = a.boolean ("auto_constraints", true);
	const json_t *els = a.get ("elements");
	if (!els || els->kind != JSON_ARR || !els->n) fail ("elements must be a non-empty array");
	JA made;		// pour chaque élément donné : les éléments de l'esquisse qu'il a faits
	std::vector<std::vector<int>> geoOf;
	for (int i = 0; i < els->n; i++) {
		const json_t *e = json_at (els, i);
		Args ea { e };
		std::string t = ea.str ("type", "", true);
		std::vector<int> gs;
		if (t == "line") {
			int p = skPointArg (s, ea.get ("from"), "from"), q = skPointArg (s, ea.get ("to"), "to");
			if (p == q) fail ("element " + std::to_string (i) + ": the line has no length");
			gs.push_back (skAddLine (s, p, q));
			skLineAuto (s, gs.back (), autoC);
		} else if (t == "polyline") {
			const json_t *ps = ea.get ("points");
			if (!ps || ps->kind != JSON_ARR || ps->n < 2) fail ("element " + std::to_string (i) + ": points needs at least 2 points");
			std::vector<int> ids;
			for (int k = 0; k < ps->n; k++) ids.push_back (skPointArg (s, json_at (ps, k), "points[]"));
			if (ea.boolean ("closed", false) && ids.front () != ids.back ()) ids.push_back (ids.front ());
			for (size_t k = 0; k + 1 < ids.size (); k++) {
				if (ids[k] == ids[k + 1]) continue;
				gs.push_back (skAddLine (s, ids[k], ids[k + 1]));
				skLineAuto (s, gs.back (), autoC);
			}
		} else if (t == "rectangle") {
			double c1[3], c2[3];
			int n1 = Args::vecOf (ea.get ("corner1"), "corner1", c1, 2, 3, true), n2 = Args::vecOf (ea.get ("corner2"), "corner2", c2, 2, 3, true);
			double x1 = c1[0], y1 = c1[1], x2 = c2[0], y2 = c2[1];
			if (n1 == 3) skLocal (s, c1, x1, y1);
			if (n2 == 3) skLocal (s, c2, x2, y2);
			if (std::fabs (x2 - x1) < 1e-9 || std::fabs (y2 - y1) < 1e-9) fail ("element " + std::to_string (i) + ": the rectangle is flat");
			double P[4][2] = { { x1, y1 }, { x2, y1 }, { x2, y2 }, { x1, y2 } };
			int id[4];
			for (int k = 0; k < 4; k++) {
				char tmp[96];
				snprintf (tmp, sizeof tmp, "[%.12g,%.12g]", P[k][0], P[k][1]);
				json_t *pj = json_parse (tmp, strlen (tmp));
				id[k] = skPointArg (s, pj, "corner");
				json_free (pj);
			}
			for (int k = 0; k < 4; k++) gs.push_back (skAddLine (s, id[k], id[(k + 1) % 4]));
			for (int k = 0; k < 4; k++) skLineAuto (s, gs[k], true);
		} else if (t == "circle") {
			int c = skPointArg (s, ea.get ("center"), "center");
			double r = ea.has ("radius") ? ea.num ("radius", 0) : ea.num ("diameter", 0, true) / 2;
			if (r <= 0) fail ("element " + std::to_string (i) + ": the radius must be positive");
			gs.push_back (skAddCircle (s, c, r));
		} else if (t == "arc") {
			int c = skPointArg (s, ea.get ("center"), "center"), p = skPointArg (s, ea.get ("start"), "start"), q = skPointArg (s, ea.get ("end"), "end");
			if (p == q || p == c) fail ("element " + std::to_string (i) + ": the arc is degenerate");
			gs.push_back (skAddArc (s, c, p, q));
		} else
			fail ("element " + std::to_string (i) + ": type must be line, polyline, rectangle, circle or arc");
		if (ea.boolean ("construction", false))
			for (int g : gs) s.geos[g].cons = true;
		JA m;
		for (int g : gs) m.add (std::to_string (g));
		made.add (m.end ());
		geoOf.push_back (gs);
	}
	// les contraintes : sur les éléments de l'esquisse (les numéros rendus dans « elements_created »)
	const json_t *cs = a.get ("constraints");
	if (cs && cs->kind == JSON_ARR)
		for (int i = 0; i < cs->n; i++) {
			Args ca { json_at (cs, i) };
			std::string t = ca.str ("type", "", true);
			std::vector<int> g = ca.ints ("elements", true);
			for (int x : g)
				if (x < 0 || x >= (int) s.geos.size ()) fail ("constraint " + std::to_string (i) + ": no sketch element " + std::to_string (x));
			auto need = [&] (size_t n) {
				if (g.size () < n) fail ("constraint " + std::to_string (i) + " (" + t + ") needs " + std::to_string (n) + " element(s)");
			};
			SkCons c;
			need (1);
			c.g1 = g[0];
			const SkGeo &e = s.geos[g[0]];
			if (t == "horizontal") c.t = SC_HORIZ;
			else if (t == "vertical") c.t = SC_VERT;
			else if (t == "parallel") need (2), c.t = SC_PARALLEL, c.g2 = g[1];
			else if (t == "perpendicular") need (2), c.t = SC_PERP, c.g2 = g[1];
			else if (t == "equal") need (2), c.t = SC_EQUAL, c.g2 = g[1];
			else if (t == "tangent") need (2), c.t = SC_TANGENT, c.g2 = g[1];
			else if (t == "concentric") need (2), c.t = SC_CONCENTRIC, c.g2 = g[1];
			else if (t == "length") {
				if (e.t != SG_LINE) fail ("constraint " + std::to_string (i) + ": length applies to a line");
				c = SkCons (), c.t = SC_DIST, c.p1 = e.a, c.p2 = e.b, c.v = ca.num ("value", 0, true);
			} else if (t == "radius" || t == "diameter") {
				if (e.t == SG_LINE) fail ("constraint " + std::to_string (i) + ": " + t + " applies to a circle or an arc");
				c.t = SC_RADIUS, c.v = ca.num ("value", 0, true) / (t == "diameter" ? 2 : 1);
			} else
				fail ("constraint " + std::to_string (i) + ": type must be horizontal, vertical, parallel, perpendicular, equal, tangent, concentric, length, radius or diameter");
			s.cons.push_back (c);
		}
	skSolve (s);
	if (s.failed || !s.conflict.empty ()) {
		std::string m = "the constraints contradict each other";
		for (int k : s.conflict) m += std::string (" · ") + consName (s.cons[k].t) + " #" + std::to_string (k);
		fail (m);
	}
	f.name = a.str ("name");
	std::string r = commitFeature (f, "sketch");
	int at = g_doc.bar - 1;
	return JO ().num ("feature", at).str ("name", g_doc.feats[at].name).raw ("elements_created", made.end ()).raw ("sketch", sketchJson (g_doc.feats[at].sk)).end ();
}

std::string t_set_sketch_dimension (const Args &a)
{
	agentTakes ();
	int fi = featureOf (a.integer ("feature", -1, true));
	if (g_doc.feats[fi].kind != ST_SKETCH) fail ("feature " + std::to_string (fi) + " is not a sketch");
	Sketch s = g_doc.feats[fi].sk;
	int k = a.integer ("constraint", -1, true);
	if (k < 0 || k >= (int) s.cons.size ()) fail ("no constraint " + std::to_string (k));
	SkCons &c = s.cons[k];
	if (c.t != SC_DIST && c.t != SC_DIST_X && c.t != SC_DIST_Y && c.t != SC_RADIUS) fail ("constraint " + std::to_string (k) + " has no value");
	double v = a.num ("value", 0, true);
	if (c.t == SC_RADIUS && a.boolean ("is_diameter", false)) v /= 2;
	c.v = v;
	skSolve (s);
	if (s.failed || !s.conflict.empty ()) fail ("the sketch cannot take this value (conflict)");
	pushUndo ();
	g_doc.feats[fi].sk = s;
	rebuild ();
	refreshAll ();
	return changed (fi);
}

std::string sweep (const Args &a, StepKind k)
{
	agentTakes ();
	Feature f;
	f.kind = k;
	f.ref = featureOf (a.integer ("sketch", -1, true));
	if (g_doc.feats[f.ref].kind != ST_SKETCH) fail ("feature " + std::to_string (f.ref) + " is not a sketch");
	if (f.ref >= g_doc.bar) fail ("the sketch is after the rollback bar");
	if (k == ST_EXTRUDE) f.a = a.num ("distance", 0, true, &f, 0);
	else f.a = a.num ("angle", 360, false, &f, 0);
	opArgs (a, f);
	f.name = a.str ("name");
	return commitFeature (f, k == ST_EXTRUDE ? "extrusion" : "revolution");
}
std::string t_extrude (const Args &a) { return sweep (a, ST_EXTRUDE); }
std::string t_revolve (const Args &a) { return sweep (a, ST_REVOLVE); }

// -- l'historique
std::string t_edit_feature (const Args &a)
{
	agentTakes ();
	int fi = featureOf (a.integer ("feature", -1, true));
	const json_t *vs = a.get ("values");
	if (!vs || vs->kind != JSON_OBJ || !vs->n) fail ("values must be an object: field name or index -> number or expression");
	Feature f = g_doc.feats[fi];
	const char *names[3];
	double vals[3];
	int n = featureFields (f, names, vals);
	if (!n) fail ("this feature has no editable values (a sketch: use set_sketch_dimension)");
	for (int i = 0; i < vs->n; i++) {
		std::string key = vs->keys[i];
		int idx = -1;
		for (int k = 0; k < n; k++) {
			std::string nm = names[k];
			if (key == nm || key == std::to_string (k)) idx = k;
			else {	// (sans la casse, en ASCII)
				std::string a1 = key, b1 = nm;
				for (auto &c : a1) c = (char) tolower (c);
				for (auto &c : b1) c = (char) tolower (c);
				if (a1 == b1) idx = k;
			}
		}
		if (idx < 0) {
			std::string m = "unknown field " + key + " (fields:";
			for (int k = 0; k < n; k++) m += std::string (" ") + std::to_string (k) + "=" + names[k];
			fail (m + ")");
		}
		for (size_t k = 0; k < f.expr.size (); k++)
			if (f.expr[k].first == idx) f.expr.erase (f.expr.begin () + k--);
		Args one { vs };
		featureSet (f, idx, one.num (vs->keys[i], 0, true, &f, idx));
	}
	if (a.has ("name")) f.name = a.str ("name");
	pushUndo ();
	g_doc.feats[fi] = f;
	rebuild ();
	if (!g_doc.feats[fi].error.empty () && a.boolean ("revert_on_error", true)) {
		std::string e = g_doc.feats[fi].error;
		dropLastUndo ();
		refreshAll ();
		fail ("Forge refused the new values: " + e + " (nothing changed)");
	}
	refreshAll ();
	return changed (fi);
}

std::string t_delete_feature (const Args &a)
{
	agentTakes ();
	int fi = featureOf (a.integer ("feature", -1, true));
	pushUndo ();
	eraseFeature (g_doc, fi);
	rebuild ();
	refreshAll ();
	return changed (-1);
}

std::string t_suppress_feature (const Args &a)
{
	agentTakes ();
	int fi = featureOf (a.integer ("feature", -1, true));
	pushUndo ();
	g_doc.feats[fi].off = a.boolean ("suppressed", true);
	rebuild ();
	refreshAll ();
	return changed (fi);
}

std::string t_set_rollback (const Args &a)
{
	agentTakes ();
	int p = a.integer ("position", (int) g_doc.feats.size ());
	if (p < 0 || p > (int) g_doc.feats.size ()) fail ("position must be between 0 and " + std::to_string (g_doc.feats.size ()));
	pushUndo ();
	g_doc.bar = p;
	rebuild ();
	refreshAll ();
	return changed (-1);
}

std::string t_undo (const Args &a)
{
	agentTakes ();
	bool redo = a.boolean ("redo", false);
	int n = std::max (1, a.integer ("steps", 1));
	int done = 0;
	for (int i = 0; i < n && !(redo ? g_redo : g_undo).empty (); i++, done++) undo (redo);
	return JO ().num ("steps_done", done).raw ("bodies", bodiesJson ()).num ("feature_count", (double) g_doc.feats.size ()).end ();
}

std::string t_new_document (const Args &a)
{
	agentTakes ();
	pushUndo ();
	g_doc.feats.clear ();
	g_doc.params.clear ();
	g_doc.bar = 0;
	g_doc.name = a.str ("name", TR ("Sans titre"));
	ui_w_set_text (w_doc, (long) g_doc.name.c_str ());
	rebuild ();
	refreshAll ();
	return documentJson (false);
}

// -- les paramètres nommés
std::string t_set_parameter (const Args &a)
{
	agentTakes ();
	std::string name = a.str ("name", "", true), ex;
	const json_t *v = a.get ("expression");
	if (!v || v->kind == JSON_NULL) fail ("missing argument: expression");
	ex = v->kind == JSON_NUM ? std::string (v->s) : v->kind == JSON_STR ? std::string (v->s) : "";
	if (name.empty () || isdigit ((unsigned char) name[0]) || name.find_first_of (" +-*/(),.=") != std::string::npos)
		fail ("a parameter name is letters, digits and _ (no spaces, not starting with a digit)");
	Doc probe = g_doc;
	size_t at = probe.params.size ();
	for (size_t i = 0; i < probe.params.size (); i++)
		if (probe.params[i].first == name) at = i;
	if (at == probe.params.size ()) probe.params.push_back ({ name, ex });
	else probe.params[at].second = ex;
	double val;
	if (!evalExpr (probe, name.c_str (), val)) fail ("unreadable expression or unknown parameter: " + ex);
	pushUndo ();
	g_doc.params = probe.params;
	rebuild ();
	refreshAll ();
	return JO ().str ("name", name).num ("value", val).raw ("parameters", paramsJson ()).raw ("bodies", bodiesJson ()).raw ("features_in_error", errorsJson ()).end ();
}

std::string t_delete_parameter (const Args &a)
{
	agentTakes ();
	std::string name = a.str ("name", "", true);
	size_t at = g_doc.params.size ();
	for (size_t i = 0; i < g_doc.params.size (); i++)
		if (g_doc.params[i].first == name) at = i;
	if (at == g_doc.params.size ()) fail ("no parameter " + name);
	pushUndo ();
	g_doc.params.erase (g_doc.params.begin () + at);
	rebuild ();
	refreshAll ();
	return JO ().raw ("parameters", paramsJson ()).raw ("features_in_error", errorsJson ()).end ();
}

// -- la vue
std::string t_set_view (const Args &a)
{
	std::string v = a.str ("view", "iso");
	static const char *const V[7] = { "right", "left", "back", "front", "top", "bottom", "iso" };
	int k = -1;
	for (int i = 0; i < 7; i++)
		if (v == V[i]) k = i;
	if (k < 0) fail ("view must be front, back, left, right, top, bottom or iso");
	if (g_sm.on) fail ("the user is editing a sketch in Forge");
	g_view.standard (k);
	if (a.boolean ("fit", true)) g_view.fit (g_doc);
	cameraChanged ();
	return JO ().str ("view", v).end ();
}

std::string t_highlight (const Args &a)
{
	std::vector<Pick> p;
	if (a.has ("body")) {
		int b = liveBody (a.integer ("body", -1));
		for (int f : a.ints ("faces")) p.push_back (Pick { b, faceOf (b, f), -1 });
		for (int e : a.ints ("edges")) p.push_back (Pick { b, -1, edgeOf (b, e) });
		if (p.empty ()) p.push_back (Pick { b, -1, -1 });
	}
	if (g_sm.on) fail ("the user is editing a sketch in Forge");
	select (p);
	return JO ().num ("selected", (double) p.size ()).end ();
}

// l'image de la vue en PNG (ArchiveKit pour la compression), réduite à max_width
bool g_ark;
std::string pngOf (const std::vector<uint32_t> &px, int w, int h)
{
	auto put32 = [] (std::string &s, uint32_t v) { s += (char) (v >> 24), s += (char) (v >> 16), s += (char) (v >> 8), s += (char) v; };
	std::string raw;
	raw.reserve ((size_t) (w * 3 + 1) * h);
	for (int y = 0; y < h; y++) {
		raw += '\0';
		for (int x = 0; x < w; x++) {
			uint32_t c = px[(size_t) y * w + x];
			raw += (char) (c >> 16), raw += (char) (c >> 8), raw += (char) c;
		}
	}
	long z = g_ark ? ar_deflate_new ((long) raw.data (), (long) raw.size (), AR_ZLIB, 6) : 0;
	if (z <= 0) fail ("PNG: compression unavailable (ArchiveKit)");
	std::string out ("\x89PNG\r\n\x1a\n", 8);
	auto chunk = [&] (const char *type, const std::string &data) {
		put32 (out, (uint32_t) data.size ());
		std::string td = std::string (type, 4) + data;
		out += td;
		put32 (out, (uint32_t) ar_crc32 (0, (long) td.data (), (long) td.size ()));
	};
	std::string ih;
	put32 (ih, (uint32_t) w), put32 (ih, (uint32_t) h);
	ih += (char) 8, ih += (char) 2, ih += '\0', ih += '\0', ih += '\0';
	chunk ("IHDR", ih);
	chunk ("IDAT", std::string ((const char *) ar_data (z), (size_t) ar_bytes (z)));
	ar_free (z);
	chunk ("IEND", "");
	return out;
}

std::string g_image;		// (la capture : rendue à part, dans le contenu de la réponse)
std::string t_screenshot (const Args &a)
{
	if (a.has ("view")) t_set_view (a);
	renderView ();
	int mw = std::max (64, std::min (2048, a.integer ("max_width", 1024)));
	int w = VW, h = VH;
	if ((int) g_back.size () < w * h) fail ("the view is not rendered yet");
	std::vector<uint32_t> px;
	if (w > mw) {	// (réduite par moyenne de blocs)
		int nw = mw, nh = (int) ((long) h * mw / w);
		px.resize ((size_t) nw * nh);
		for (int y = 0; y < nh; y++)
			for (int x = 0; x < nw; x++) {
				int x0 = x * w / nw, x1 = std::max (x0 + 1, (x + 1) * w / nw), y0 = y * h / nh, y1 = std::max (y0 + 1, (y + 1) * h / nh);
				unsigned r = 0, g = 0, b = 0, n = 0;
				for (int yy = y0; yy < y1; yy++)
					for (int xx = x0; xx < x1; xx++) {
						uint32_t c = g_back[(size_t) yy * w + xx];
						r += c >> 16 & 255, g += c >> 8 & 255, b += c & 255, n++;
					}
				px[(size_t) y * nw + x] = (r / n) << 16 | (g / n) << 8 | (b / n);
			}
		w = nw, h = nh;
	} else
		px.assign (g_back.begin (), g_back.begin () + (size_t) w * h);
	std::string png = pngOf (px, w, h);
	g_image = mcp::base64 ((const unsigned char *) png.data (), png.size ());
	return JO ().num ("width", w).num ("height", h).str ("note", "the image shows the 3D view only (no panels)").end ();
}

// -- l'export
std::string t_export (const Args &a)
{
	std::string path = a.str ("path", "", true), fmt = a.str ("format");
	if (fmt.empty ()) {
		std::string e = path.substr (path.find_last_of ('.') == std::string::npos ? path.size () : path.find_last_of ('.') + 1);
		for (auto &c : e) c = (char) tolower (c);
		fmt = e == "stp" || e == "step" ? "step" : e == "igs" || e == "iges" ? "iges" : e == "stl" ? "stl" : e == "brep" || e == "brp" ? "brep" : "";
		if (fmt.empty ()) fail ("give format (step, iges, stl, brep) or a path ending in .step, .iges, .stl or .brep");
	}
	if (path.find (':') == std::string::npos && path[0] != '/') {	// (relatif : le dossier de Forge)
		mkdir ("HOME:/forge", 0755);
		path = "HOME:/forge/" + path;
	}
	std::vector<const Body *> bs;
	std::vector<int> want = a.ints ("bodies");
	if (want.empty ())
		for (auto &b : g_doc.bodies) {
			if (!b.shape.IsNull () && b.visible) bs.push_back (&b);
		}
	else
		for (int b : want) bs.push_back (&g_doc.bodies[liveBody (b)]);
	std::string err = exportBodies (bs, path, fmt);
	if (!err.empty ()) fail (err);
	struct stat st;
	long size = stat (path.c_str (), &st) == 0 ? (long) st.st_size : -1;
	status ((std::string (TR ("Agent : exporté ")) + path).c_str ());
	return JO ().str ("path", path).str ("format", fmt).num ("bodies", (double) bs.size ()).num ("bytes", size).end ();
}

// ---- le catalogue -----------------------------------------------------------------------------------------------

#define S(...) #__VA_ARGS__
const ToolDef TOOLS[] = {
	{ "get_document",
	  "Read the Forge document: the parametric history (features with their editable fields and formulas, errors, rollback bar), the bodies "
	  "(volume, bounding box, face/edge counts) and the named parameters. Units are millimetres and degrees. Call this first.",
	  S ({ "type" : "object", "properties" : { "full" : { "type" : "boolean", "description" : "include frames, sketches and hole positions" } } }),
	  t_get_document, true },
	{ "get_feature", "Read one feature of the history in full (frame, sketch elements and constraints, fields and formulas).",
	  S ({ "type" : "object", "properties" : { "feature" : { "type" : "integer" } }, "required" : ["feature"] }), t_get_feature, true },
	{ "get_body_topology",
	  "List the faces (type, area, center, outward normal for planes, radius and axis for cylinders) and edges (type, length, mid point, "
	  "tangent, ends, circle center and radius) of a body, with the indices the other tools use. Indices change when the body changes: "
	  "read the topology again after each modelling operation.",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" } }, "required" : ["body"] }), t_get_body_topology, true },
	{ "add_box",
	  "Add a box. origin is the center of its base; it extends width along x_direction, depth along normal x x_direction, and height along "
	  "normal (negative: below). Default normal +Z, x_direction +X. on_face {body, face} takes the plane of a planar face (origin then "
	  "projected on it). operation: new (default, a new body), join, cut or intersect with body. Values may be numbers or expressions using "
	  "named parameters (\"wall*2\"), which stay parametric.",
	  S ({ "type" : "object", "properties" : {
		  "origin" : { "type" : "array", "items" : { "type" : "number" }, "minItems" : 3, "maxItems" : 3 },
		  "width" : { "type" : ["number", "string"] }, "depth" : { "type" : ["number", "string"] }, "height" : { "type" : ["number", "string"] },
		  "normal" : { "type" : "array", "items" : { "type" : "number" } }, "x_direction" : { "type" : "array", "items" : { "type" : "number" } },
		  "on_face" : { "type" : "object", "properties" : { "body" : { "type" : "integer" }, "face" : { "type" : "integer" } } },
		  "operation" : { "type" : "string", "enum" : ["new", "join", "cut", "intersect"] }, "body" : { "type" : "integer" },
		  "name" : { "type" : "string" } }, "required" : ["width", "depth", "height"] }),
	  t_add_box, false },
	{ "add_cylinder",
	  "Add a cylinder: origin is the center of its base, it extends height along normal (default +Z; negative: below). diameter or radius. "
	  "on_face, operation and body as for add_box.",
	  S ({ "type" : "object", "properties" : {
		  "origin" : { "type" : "array", "items" : { "type" : "number" } }, "diameter" : { "type" : ["number", "string"] },
		  "radius" : { "type" : ["number", "string"] }, "height" : { "type" : ["number", "string"] },
		  "normal" : { "type" : "array", "items" : { "type" : "number" } },
		  "on_face" : { "type" : "object", "properties" : { "body" : { "type" : "integer" }, "face" : { "type" : "integer" } } },
		  "operation" : { "type" : "string", "enum" : ["new", "join", "cut", "intersect"] }, "body" : { "type" : "integer" },
		  "name" : { "type" : "string" } }, "required" : ["height"] }),
	  t_add_cylinder, false },
	{ "push_pull", "Push or pull a planar face of a body along its outward normal: positive distance adds material, negative removes it.",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" }, "face" : { "type" : "integer" },
		  "distance" : { "type" : ["number", "string"] }, "name" : { "type" : "string" } }, "required" : ["body", "face", "distance"] }),
	  t_push_pull, false },
	{ "fillet",
	  "Round edges of a body with a radius. tangent_chain also takes the edges tangent to them. If the radius is too large, the error gives "
	  "the largest radius that works.",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" }, "edges" : { "type" : "array", "items" : { "type" : "integer" } },
		  "radius" : { "type" : ["number", "string"] }, "tangent_chain" : { "type" : "boolean" }, "name" : { "type" : "string" } },
		  "required" : ["body", "edges", "radius"] }),
	  t_fillet, false },
	{ "chamfer", "Bevel edges of a body by a distance (equal on both faces). tangent_chain as for fillet.",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" }, "edges" : { "type" : "array", "items" : { "type" : "integer" } },
		  "distance" : { "type" : ["number", "string"] }, "tangent_chain" : { "type" : "boolean" }, "name" : { "type" : "string" } },
		  "required" : ["body", "edges", "distance"] }),
	  t_chamfer, false },
	{ "add_hole",
	  "Drill holes into a planar face of a body, from the face inwards. centers are 3D points (projected on the face). depth 0 or absent: "
	  "through all. counterbore {diameter, depth} is optional.",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" }, "face" : { "type" : "integer" },
		  "centers" : { "type" : "array", "items" : { "type" : "array", "items" : { "type" : "number" } } },
		  "diameter" : { "type" : ["number", "string"] }, "depth" : { "type" : ["number", "string"] },
		  "counterbore" : { "type" : "object", "properties" : { "diameter" : { "type" : "number" }, "depth" : { "type" : "number" } } },
		  "name" : { "type" : "string" } }, "required" : ["body", "face", "centers", "diameter"] }),
	  t_add_hole, false },
	{ "shell", "Hollow a body with a wall thickness, removing the given planar faces (the openings).",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" }, "open_faces" : { "type" : "array", "items" : { "type" : "integer" } },
		  "thickness" : { "type" : ["number", "string"] }, "name" : { "type" : "string" } }, "required" : ["body", "open_faces", "thickness"] }),
	  t_shell, false },
	{ "pattern",
	  "Repeat a feature (box, cylinder, hole, slot, extrusion, revolution). count includes the original. linear: direction (x, y, z or a "
	  "vector) and spacing. circular: axis (x, y, z or a vector) through center, angle = total angle (360: evenly around).",
	  S ({ "type" : "object", "properties" : { "feature" : { "type" : "integer" }, "mode" : { "type" : "string", "enum" : ["linear", "circular"] },
		  "count" : { "type" : "integer" }, "direction" : {}, "spacing" : { "type" : ["number", "string"] }, "axis" : {},
		  "center" : { "type" : "array", "items" : { "type" : "number" } }, "angle" : { "type" : ["number", "string"] }, "name" : { "type" : "string" } },
		  "required" : ["feature", "count"] }),
	  t_pattern, false },
	{ "mirror",
	  "Mirror a body across a plane: a planar face of the body, or plane yz / xz / xy (through origin, default [0,0,0]), or custom with normal. "
	  "join true (default) fuses the mirror image with the body; false makes a new body.",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" }, "face" : { "type" : "integer" },
		  "plane" : { "type" : "string", "enum" : ["yz", "xz", "xy", "custom"] }, "origin" : { "type" : "array", "items" : { "type" : "number" } },
		  "normal" : { "type" : "array", "items" : { "type" : "number" } }, "join" : { "type" : "boolean" }, "name" : { "type" : "string" } },
		  "required" : ["body"] }),
	  t_mirror, false },
	{ "boolean", "Combine bodies (one Combine step): union fuses the tool bodies into target_body, cut removes them from it, intersect keeps only "
	  "what target_body shares with them. Give tool_body or tool_bodies. The tool bodies are consumed unless keep_tools is true.",
	  S ({ "type" : "object", "properties" : { "operation" : { "type" : "string", "enum" : ["union", "cut", "intersect"] }, "target_body" : { "type" : "integer" },
		  "tool_body" : { "type" : "integer" }, "tool_bodies" : { "type" : "array", "items" : { "type" : "integer" } },
		  "keep_tools" : { "type" : "boolean" }, "name" : { "type" : "string" } }, "required" : ["operation", "target_body"] }),
	  t_boolean, false },
	{ "create_sketch",
	  "Create a 2D sketch solved by PlaneGCS, on plane xy / xz / yz (offset along its normal, or origin) or on_face {body, face}. Sketch "
	  "coordinates are 2D in the plane (xz: x->X, y->Z; yz: x->Y, y->Z); a 3-number point is a 3D point projected on the plane. Elements: "
	  "line {from, to}, polyline {points, closed}, rectangle {corner1, corner2}, circle {center, radius|diameter}, arc {center, start, end} "
	  "(counter-clockwise), each with construction:true for an axis (revolve uses the first construction line). Points at the same "
	  "coordinates are shared. Horizontal/vertical lines get H/V constraints (auto_constraints). constraints refer to sketch element "
	  "indices (see elements_created): horizontal, vertical, parallel, perpendicular, equal, tangent, concentric, length, radius, diameter "
	  "(with value). Closed profiles (small inside big = holes) can then be extruded or revolved.",
	  S ({ "type" : "object", "properties" : {
		  "plane" : { "type" : "string", "enum" : ["xy", "xz", "yz"] }, "offset" : { "type" : "number" },
		  "origin" : { "type" : "array", "items" : { "type" : "number" } },
		  "on_face" : { "type" : "object", "properties" : { "body" : { "type" : "integer" }, "face" : { "type" : "integer" } } },
		  "elements" : { "type" : "array", "items" : { "type" : "object", "properties" : {
			  "type" : { "type" : "string", "enum" : ["line", "polyline", "rectangle", "circle", "arc"] },
			  "from" : { "type" : "array" }, "to" : { "type" : "array" }, "points" : { "type" : "array" }, "closed" : { "type" : "boolean" },
			  "corner1" : { "type" : "array" }, "corner2" : { "type" : "array" }, "center" : { "type" : "array" },
			  "radius" : { "type" : "number" }, "diameter" : { "type" : "number" }, "start" : { "type" : "array" }, "end" : { "type" : "array" },
			  "construction" : { "type" : "boolean" } }, "required" : ["type"] } },
		  "constraints" : { "type" : "array", "items" : { "type" : "object", "properties" : { "type" : { "type" : "string" },
			  "elements" : { "type" : "array", "items" : { "type" : "integer" } }, "value" : { "type" : "number" } } } },
		  "auto_constraints" : { "type" : "boolean" }, "name" : { "type" : "string" } }, "required" : ["elements"] }),
	  t_create_sketch, false },
	{ "set_sketch_dimension", "Change the value of a dimensional constraint (length, distance, radius) of a sketch; the sketch is solved again and what uses it follows.",
	  S ({ "type" : "object", "properties" : { "feature" : { "type" : "integer" }, "constraint" : { "type" : "integer" },
		  "value" : { "type" : "number" }, "is_diameter" : { "type" : "boolean" } }, "required" : ["feature", "constraint", "value"] }),
	  t_set_sketch_dimension, false },
	{ "extrude",
	  "Extrude the closed profiles of a sketch along the sketch normal by distance (negative: the other way). operation new (default), "
	  "join, cut or intersect with body.",
	  S ({ "type" : "object", "properties" : { "sketch" : { "type" : "integer" }, "distance" : { "type" : ["number", "string"] },
		  "operation" : { "type" : "string", "enum" : ["new", "join", "cut", "intersect"] }, "body" : { "type" : "integer" },
		  "name" : { "type" : "string" } }, "required" : ["sketch", "distance"] }),
	  t_extrude, false },
	{ "revolve", "Revolve the closed profiles of a sketch around its first construction line by angle (degrees, default 360). operation and body as for extrude.",
	  S ({ "type" : "object", "properties" : { "sketch" : { "type" : "integer" }, "angle" : { "type" : ["number", "string"] },
		  "operation" : { "type" : "string", "enum" : ["new", "join", "cut", "intersect"] }, "body" : { "type" : "integer" },
		  "name" : { "type" : "string" } }, "required" : ["sketch"] }),
	  t_revolve, false },
	{ "edit_feature",
	  "Change the editable values of a feature (see fields in get_document: by name or index), as numbers or expressions with named "
	  "parameters. The history is replayed; by default a value that makes the feature fail is refused and nothing changes.",
	  S ({ "type" : "object", "properties" : { "feature" : { "type" : "integer" }, "values" : { "type" : "object" }, "name" : { "type" : "string" },
		  "revert_on_error" : { "type" : "boolean" } }, "required" : ["feature", "values"] }),
	  t_edit_feature, false },
	{ "delete_feature", "Delete a feature from the history (features that used it go into error).",
	  S ({ "type" : "object", "properties" : { "feature" : { "type" : "integer" } }, "required" : ["feature"] }), t_delete_feature, false },
	{ "suppress_feature", "Suppress (skip) or restore a feature without deleting it.",
	  S ({ "type" : "object", "properties" : { "feature" : { "type" : "integer" }, "suppressed" : { "type" : "boolean" } }, "required" : ["feature"] }),
	  t_suppress_feature, false },
	{ "set_rollback", "Move the rollback bar: only features before position are played; new features are inserted there.",
	  S ({ "type" : "object", "properties" : { "position" : { "type" : "integer" } }, "required" : ["position"] }), t_set_rollback, false },
	{ "undo", "Undo (or redo with redo:true) the last changes, steps times.",
	  S ({ "type" : "object", "properties" : { "redo" : { "type" : "boolean" }, "steps" : { "type" : "integer" } } }), t_undo, false },
	{ "new_document", "Start an empty document (undoable).",
	  S ({ "type" : "object", "properties" : { "name" : { "type" : "string" } } }), t_new_document, false },
	{ "set_parameter",
	  "Create or change a named parameter (expression: a number, units, other parameters: \"wall*2+1\"). Values written with it follow when it changes.",
	  S ({ "type" : "object", "properties" : { "name" : { "type" : "string" }, "expression" : { "type" : ["string", "number"] } },
		  "required" : ["name", "expression"] }),
	  t_set_parameter, false },
	{ "delete_parameter", "Delete a named parameter (values that use it go into error).",
	  S ({ "type" : "object", "properties" : { "name" : { "type" : "string" } }, "required" : ["name"] }), t_delete_parameter, false },
	{ "set_view", "Turn the 3D view to a standard direction (front, back, left, right, top, bottom, iso) and fit the part.",
	  S ({ "type" : "object", "properties" : { "view" : { "type" : "string", "enum" : ["front", "back", "left", "right", "top", "bottom", "iso"] },
		  "fit" : { "type" : "boolean" } } }),
	  t_set_view, true },
	{ "highlight", "Select a body, or faces/edges of it, in Forge so that the user sees them (no arguments: clear the selection).",
	  S ({ "type" : "object", "properties" : { "body" : { "type" : "integer" }, "faces" : { "type" : "array", "items" : { "type" : "integer" } },
		  "edges" : { "type" : "array", "items" : { "type" : "integer" } } } }),
	  t_highlight, true },
	{ "screenshot", "Return a PNG image of the 3D view (optionally after set_view). Use it to check the result visually.",
	  S ({ "type" : "object", "properties" : { "view" : { "type" : "string", "enum" : ["front", "back", "left", "right", "top", "bottom", "iso"] },
		  "fit" : { "type" : "boolean" }, "max_width" : { "type" : "integer" } } }),
	  t_screenshot, true },
	{ "export",
	  "Export bodies (default: all visible) to STEP, IGES, STL (binary) or BREP. A relative path goes to HOME:/forge/; Korin paths "
	  "look like HOME:/documents/part.step (the account's folder, /home/<user>).",
	  S ({ "type" : "object", "properties" : { "path" : { "type" : "string" }, "format" : { "type" : "string", "enum" : ["step", "iges", "stl", "brep"] },
		  "bodies" : { "type" : "array", "items" : { "type" : "integer" } } }, "required" : ["path"] }),
	  t_export, true },
};
#undef S

// ---- JSON-RPC ---------------------------------------------------------------------------------------------------

std::string rpcError (const std::string &id, int code, const std::string &msg)
{
	return JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("error", JO ().num ("code", code).str ("message", msg).end ()).end ();
}

std::string toolsList ()
{
	JA a;
	for (auto &t : TOOLS) {
		json_t *sc = json_parse (t.schema, strlen (t.schema));
		jbuf b;
		jb_init (&b);
		if (sc) jb_json (&b, sc);
		std::string schema = sc ? std::string (b.p, b.n) : "{\"type\":\"object\"}";
		jb_free (&b);
		json_free (sc);
		a.add (JO ().str ("name", t.name).str ("description", t.desc).raw ("inputSchema", schema)
		       .raw ("annotations", JO ().boo ("readOnlyHint", t.readOnly).boo ("destructiveHint", false).end ()).end ());
	}
	return JO ().raw ("tools", a.end ()).end ();
}

std::string toolCall (const json_t *params)
{
	std::string name = json_gets (params, "name");
	const json_t *args = json_get (params, "arguments");
	const ToolDef *t = nullptr;
	for (auto &d : TOOLS)
		if (name == d.name) t = &d;
	g_image.clear ();
	std::string text;
	bool err = false;
	if (!t) text = "unknown tool: " + name, err = true;
	else
		try {
			text = t->fn (Args { args && args->kind == JSON_OBJ ? args : nullptr });
		} catch (const ToolErr &x) {
			text = x.msg, err = true;
		} catch (const Standard_Failure &x) {
			text = std::string ("OpenCASCADE: ") + x.what (), err = true;
		} catch (const std::exception &x) {
			text = std::string ("error: ") + x.what (), err = true;
		}
	if (err) status ((std::string (TR ("Agent : ")) + name + " — " + text).c_str ());
	JA content;
	if (!g_image.empty () && !err) content.add (JO ().str ("type", "image").str ("data", g_image).str ("mimeType", "image/png").end ());
	content.add (JO ().str ("type", "text").str ("text", text).end ());
	g_image.clear ();
	return JO ().raw ("content", content.end ()).boo ("isError", err).end ();
}

std::string mcpHandle (const std::string &in)
{
	json_t *m = json_parse (in.data (), in.size ());
	if (!m) return rpcError ("null", -32700, "parse error");
	std::string out;
	const json_t *idv = json_get (m, "id");
	std::string id;
	if (idv) {
		jbuf b;
		jb_init (&b);
		jb_json (&b, idv);
		id.assign (b.p, b.n);
		jb_free (&b);
	}
	std::string method = json_gets (m, "method");
	const json_t *params = json_get (m, "params");
	if (!idv || idv->kind == JSON_NULL) {	// (notification, ou réponse : rien à rendre)
		json_free (m);
		return "";
	}
	if (method == "initialize") {
		std::string want = params ? json_gets (params, "protocolVersion") : "";
		std::string ver = want == "2025-06-18" || want == "2025-03-26" || want == "2024-11-05" ? want : "2025-06-18";
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", JO ().str ("protocolVersion", ver)
			.raw ("capabilities", JO ().raw ("tools", JO ().boo ("listChanged", false).end ()).end ())
			.raw ("serverInfo", JO ().str ("name", "forge").str ("title", "Forge (Korin OS CAD)").str ("version", "1.3.0").end ())
			.str ("instructions", "Forge is the parametric CAD of Korin OS (OpenCASCADE solids, PlaneGCS sketches), in millimetres. "
			      "Start with get_document; use get_body_topology to find face and edge indices (they change after each operation); "
			      "check results with screenshot. Every change is a feature in the history, which the user sees live and can undo.")
			.end ()).end ();
	} else if (method == "ping")
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", "{}").end ();
	else if (method == "tools/list")
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", toolsList ()).end ();
	else if (method == "tools/call") {
		if (!params) out = rpcError (id, -32602, "missing params");
		else out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", toolCall (params)).end ();
	} else if (method == "resources/list" || method == "prompts/list")
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", method == "resources/list" ? "{\"resources\":[]}" : "{\"prompts\":[]}").end ();
	else
		out = rpcError (id, -32601, "method not found: " + method);
	json_free (m);
	return out;
}
