// docfile.cpp -- Forge : le document dans un fichier .forge (agent.h, docSave / docLoad). Du JSON : l'historique tel
// quel (les opérations, leurs valeurs et leurs formules, les esquisses avec leurs contraintes, la barre, les paramètres) ;
// les corps n'y sont pas, ils sont refaits par le rejeu à l'ouverture.
//
//   { "format" : "forge", "version" : 1, "name" : "...", "bar" : n, "params" : [[nom, formule], ...],
//     "features" : [ { "kind" : "box", ... }, ... ] }
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "agent.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "json.h"

namespace forge {

namespace {

const char *const KIND[] = { "box", "cylinder", "sketch", "extrude", "fillet", "chamfer", "combine", "cut", "hole", "push_pull", "slot",
			     "revolve", "shell", "pattern", "mirror" };
const int NKIND = (int) (sizeof KIND / sizeof *KIND);

// ---- l'écriture -----------------------------------------------------------------------------------------------

struct W {
	jbuf b;
	W () { jb_init (&b); }
	~W () { jb_free (&b); }
	void num (double v)
	{
		char s[40];
		if (std::fabs (v - std::round (v)) < 1e-12 && std::fabs (v) < 1e15) snprintf (s, sizeof s, "%.0f", v);
		else snprintf (s, sizeof s, "%.17g", v);
		jb_puts (&b, s);
	}
	void key (const char *k) { jb_str (&b, k), jb_putc (&b, ':'); }
	void nums (const double *v, int n)
	{
		jb_putc (&b, '[');
		for (int i = 0; i < n; i++) {
			if (i) jb_putc (&b, ',');
			num (v[i]);
		}
		jb_putc (&b, ']');
	}
};

void frameOut (W &w, const char *k, const Frame &f)
{
	w.key (k);
	jb_puts (&w.b, "{");
	w.key ("o"), w.nums (f.o, 3), jb_putc (&w.b, ',');
	w.key ("n"), w.nums (f.n, 3), jb_putc (&w.b, ',');
	w.key ("x"), w.nums (f.x, 3);
	jb_puts (&w.b, "}");
}

void sketchOut (W &w, const Sketch &s)
{
	w.key ("sketch");
	jb_putc (&w.b, '{');
	frameOut (w, "plane", s.plane);
	jb_putc (&w.b, ','), w.key ("body"), w.num (s.body);
	jb_putc (&w.b, ','), w.key ("points"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < s.pts.size (); i++) {
		double p[2] = { s.pts[i].x, s.pts[i].y };
		if (i) jb_putc (&w.b, ',');
		w.nums (p, 2);
	}
	jb_puts (&w.b, "],");
	w.key ("geos"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < s.geos.size (); i++) {
		const SkGeo &g = s.geos[i];
		double v[8] = { (double) g.t, (double) g.a, (double) g.b, (double) g.c, g.r, g.a0, g.a1, g.cons ? 1.0 : 0.0 };
		if (i) jb_putc (&w.b, ',');
		w.nums (v, 8);
	}
	jb_puts (&w.b, "],");
	w.key ("cons"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < s.cons.size (); i++) {
		const SkCons &c = s.cons[i];
		double v[6] = { (double) c.t, (double) c.g1, (double) c.g2, (double) c.p1, (double) c.p2, c.v };
		if (i) jb_putc (&w.b, ',');
		w.nums (v, 6);
	}
	jb_puts (&w.b, "]}");
}

void featureOut (W &w, const Feature &f)
{
	jb_putc (&w.b, '{');
	w.key ("kind"), jb_str (&w.b, f.kind >= 0 && f.kind < NKIND ? KIND[f.kind] : "?");
	jb_putc (&w.b, ','), w.key ("name"), jb_str (&w.b, f.name.c_str ());
	jb_putc (&w.b, ','), w.key ("op"), w.num (f.op);
	jb_putc (&w.b, ','), w.key ("body"), w.num (f.body);
	jb_putc (&w.b, ','), w.key ("tools"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < f.tools.size (); i++) {
		if (i) jb_putc (&w.b, ',');
		w.num (f.tools[i]);
	}
	jb_putc (&w.b, ']');
	jb_putc (&w.b, ','), w.key ("keep"), jb_puts (&w.b, f.keep ? "true" : "false");
	jb_putc (&w.b, ','), frameOut (w, "frame", f.frame);
	double v[6] = { f.a, f.b, f.c, f.d1, f.d2, f.dep2 };
	jb_putc (&w.b, ','), w.key ("values"), w.nums (v, 6);
	jb_putc (&w.b, ','), w.key ("pos"), w.nums (f.pos.data (), (int) f.pos.size ());
	jb_putc (&w.b, ','), w.key ("edges"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < f.edges.size (); i++) {
		double e[6] = { f.edges[i].mid[0], f.edges[i].mid[1], f.edges[i].mid[2], f.edges[i].tan[0], f.edges[i].tan[1], f.edges[i].tan[2] };
		if (i) jb_putc (&w.b, ',');
		w.nums (e, 6);
	}
	jb_putc (&w.b, ']');
	double fc[6] = { f.face.pt[0], f.face.pt[1], f.face.pt[2], f.face.n[0], f.face.n[1], f.face.n[2] };
	jb_putc (&w.b, ','), w.key ("face"), w.nums (fc, 6);
	jb_putc (&w.b, ','), w.key ("faces"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < f.faces.size (); i++) {
		double r[6] = { f.faces[i].pt[0], f.faces[i].pt[1], f.faces[i].pt[2], f.faces[i].n[0], f.faces[i].n[1], f.faces[i].n[2] };
		if (i) jb_putc (&w.b, ',');
		w.nums (r, 6);
	}
	jb_putc (&w.b, ']');
	jb_putc (&w.b, ','), w.key ("ref"), w.num (f.ref);
	jb_putc (&w.b, ','), w.key ("count"), w.num (f.count);
	jb_putc (&w.b, ','), w.key ("mode"), w.num (f.mode);
	jb_putc (&w.b, ','), w.key ("expr"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < f.expr.size (); i++) {
		if (i) jb_putc (&w.b, ',');
		jb_putc (&w.b, '['), w.num (f.expr[i].first), jb_putc (&w.b, ','), jb_str (&w.b, f.expr[i].second.c_str ()), jb_putc (&w.b, ']');
	}
	jb_putc (&w.b, ']');
	jb_putc (&w.b, ','), w.key ("off"), jb_puts (&w.b, f.off ? "true" : "false");
	if (f.kind == ST_SKETCH) jb_putc (&w.b, ','), sketchOut (w, f.sk);
	jb_putc (&w.b, '}');
}

// ---- la lecture ------------------------------------------------------------------------------------------------

double num (const json_t *v, double def = 0)
{
	if (!v || v->kind != JSON_NUM) return def;
	return strtod (v->s, nullptr);
}
void nums (const json_t *v, double *out, int n)
{
	for (int i = 0; i < n; i++) out[i] = num (json_at (v, i), out[i]);
}
bool boolean (const json_t *v) { return v && v->kind == JSON_BOOL && v->b; }

void frameIn (const json_t *v, Frame &f)
{
	if (!v) return;
	nums (json_get (v, "o"), f.o, 3);
	nums (json_get (v, "n"), f.n, 3);
	nums (json_get (v, "x"), f.x, 3);
}

void sketchIn (const json_t *v, Sketch &s)
{
	if (!v) return;
	frameIn (json_get (v, "plane"), s.plane);
	s.body = (int) num (json_get (v, "body"), -1);
	s.pts.clear ();
	const json_t *p = json_get (v, "points");
	for (int i = 0; i < json_len (p); i++) {
		double q[2] = { 0, 0 };
		nums (json_at (p, i), q, 2);
		s.pts.push_back (SkPoint { q[0], q[1] });
	}
	if (s.pts.empty ()) s.pts.push_back (SkPoint ());
	const json_t *g = json_get (v, "geos");
	for (int i = 0; i < json_len (g); i++) {
		double q[8] = { 0, -1, -1, -1, 0, 0, 0, 0 };
		nums (json_at (g, i), q, 8);
		SkGeo e;
		e.t = (SkGeoType) (int) q[0], e.a = (int) q[1], e.b = (int) q[2], e.c = (int) q[3], e.r = q[4], e.a0 = q[5], e.a1 = q[6], e.cons = q[7] != 0;
		s.geos.push_back (e);
	}
	const json_t *c = json_get (v, "cons");
	for (int i = 0; i < json_len (c); i++) {
		double q[6] = { 0, -1, -1, -1, -1, 0 };
		nums (json_at (c, i), q, 6);
		SkCons k;
		k.t = (SkConsType) (int) q[0], k.g1 = (int) q[1], k.g2 = (int) q[2], k.p1 = (int) q[3], k.p2 = (int) q[4], k.v = q[5];
		s.cons.push_back (k);
	}
}

bool featureIn (const json_t *v, Feature &f)
{
	std::string k = json_gets (v, "kind");
	int kind = -1;
	for (int i = 0; i < NKIND; i++) if (k == KIND[i]) kind = i;
	if (kind < 0) return false;
	f.kind = (StepKind) kind;
	f.name = json_gets (v, "name");
	f.op = (Op) (int) num (json_get (v, "op"), 0);
	f.body = (int) num (json_get (v, "body"), -1);
	const json_t *t = json_get (v, "tools");
	for (int i = 0; i < json_len (t); i++) f.tools.push_back ((int) num (json_at (t, i), -1));
	f.keep = boolean (json_get (v, "keep"));
	frameIn (json_get (v, "frame"), f.frame);
	double vals[6] = { 0, 0, 0, 0, 0, 0 };
	nums (json_get (v, "values"), vals, 6);
	f.a = vals[0], f.b = vals[1], f.c = vals[2], f.d1 = vals[3], f.d2 = vals[4], f.dep2 = vals[5];
	const json_t *p = json_get (v, "pos");
	for (int i = 0; i < json_len (p); i++) f.pos.push_back (num (json_at (p, i)));
	const json_t *e = json_get (v, "edges");
	for (int i = 0; i < json_len (e); i++) {
		double q[6] = { 0, 0, 0, 0, 0, 0 };
		nums (json_at (e, i), q, 6);
		f.edges.push_back (EdgeRef { { q[0], q[1], q[2] }, { q[3], q[4], q[5] } });
	}
	double fc[6] = { 0, 0, 0, 0, 0, 1 };
	nums (json_get (v, "face"), fc, 6);
	f.face = FaceRef { { fc[0], fc[1], fc[2] }, { fc[3], fc[4], fc[5] } };
	const json_t *fs = json_get (v, "faces");
	for (int i = 0; i < json_len (fs); i++) {
		double q[6] = { 0, 0, 0, 0, 0, 1 };
		nums (json_at (fs, i), q, 6);
		f.faces.push_back (FaceRef { { q[0], q[1], q[2] }, { q[3], q[4], q[5] } });
	}
	f.ref = (int) num (json_get (v, "ref"), -1);
	f.count = (int) num (json_get (v, "count"), 0);
	f.mode = (int) num (json_get (v, "mode"), 0);
	const json_t *x = json_get (v, "expr");
	for (int i = 0; i < json_len (x); i++) {
		const json_t *pr = json_at (x, i);
		f.expr.push_back ({ (int) num (json_at (pr, 0), 0), json_text (json_at (pr, 1)) });
	}
	f.off = boolean (json_get (v, "off"));
	if (f.kind == ST_SKETCH) sketchIn (json_get (v, "sketch"), f.sk);
	return true;
}

} // namespace

std::string docSave (const Doc &d, const std::string &path)
{
	W w;
	jb_puts (&w.b, "{\n");
	w.key ("format"), jb_str (&w.b, "forge"), jb_puts (&w.b, ",\n");
	w.key ("version"), w.num (1), jb_puts (&w.b, ",\n");
	w.key ("name"), jb_str (&w.b, d.name.c_str ()), jb_puts (&w.b, ",\n");
	w.key ("bar"), w.num (d.bar), jb_puts (&w.b, ",\n");
	w.key ("params"), jb_putc (&w.b, '[');
	for (size_t i = 0; i < d.params.size (); i++) {
		if (i) jb_putc (&w.b, ',');
		jb_putc (&w.b, '['), jb_str (&w.b, d.params[i].first.c_str ()), jb_putc (&w.b, ','), jb_str (&w.b, d.params[i].second.c_str ()), jb_putc (&w.b, ']');
	}
	jb_puts (&w.b, "],\n");
	w.key ("features"), jb_puts (&w.b, "[\n");
	for (size_t i = 0; i < d.feats.size (); i++) {
		featureOut (w, d.feats[i]);
		jb_puts (&w.b, i + 1 < d.feats.size () ? ",\n" : "\n");
	}
	jb_puts (&w.b, "]\n}\n");
	FILE *fp = fopen (path.c_str (), "wb");
	if (!fp) return "impossible d'écrire le fichier";
	size_t n = fwrite (w.b.p, 1, w.b.n, fp);
	bool ok = n == w.b.n;
	ok = fclose (fp) == 0 && ok;
	return ok ? "" : "le fichier n'a pas été écrit en entier";
}

std::string docLoad (Doc &d, const std::string &path)
{
	FILE *fp = fopen (path.c_str (), "rb");
	if (!fp) return "impossible d'ouvrir le fichier";
	std::string text;
	char buf[65536];
	size_t n;
	while ((n = fread (buf, 1, sizeof buf, fp)) > 0) text.append (buf, n);
	fclose (fp);
	json_t *v = json_parse (text.data (), text.size ());
	if (!v) return "ce fichier n'est pas un document Forge (JSON illisible)";
	std::string err;
	if (std::string (json_gets (v, "format")) != "forge") err = "ce fichier n'est pas un document Forge";
	else if (num (json_get (v, "version"), 0) > 1) err = "ce document vient d'une version plus récente de Forge";
	Doc r;
	if (err.empty ()) {
		r.name = json_gets (v, "name");
		const json_t *ps = json_get (v, "params");
		for (int i = 0; i < json_len (ps); i++) r.params.push_back ({ json_text (json_at (json_at (ps, i), 0)), json_text (json_at (json_at (ps, i), 1)) });
		const json_t *fs = json_get (v, "features");
		for (int i = 0; i < json_len (fs) && err.empty (); i++) {
			Feature f;
			if (!featureIn (json_at (fs, i), f)) err = std::string ("une opération inconnue : ") + json_gets (json_at (fs, i), "kind");
			else r.feats.push_back (f);
		}
		r.bar = (int) num (json_get (v, "bar"), (double) r.feats.size ());
		if (r.bar < 0 || r.bar > (int) r.feats.size ()) r.bar = (int) r.feats.size ();
	}
	json_free (v);
	if (!err.empty ()) return err;
	for (auto &f : r.feats)
		if (f.kind == ST_SKETCH) skSolve (f.sk);	// (le diagnostic de l'esquisse : DDL, conflits)
	d = r;
	return "";
}

} // namespace forge
