//
// io.cpp -- other formats, and extensions. An exporter walks the drawing as flat paths in page px with their colours; what a
// format cannot say (gradients, filters) is reduced to a flat colour or left out, the README lists it. The extension
// protocol is the plain one: a .inx file describes the parameters and a command; the command gets --name=value and a
// temporary SVG, and writes the changed SVG on its standard output.
//
#include "editor.h"
#include "include/core/SkPathUtils.h"
#include "../render/scene.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>

using namespace vd;
using namespace vr;

namespace vapp { bool read_file (const char *path, std::string &out); bool write_file (const char *path, const std::string &s); }

namespace ve {

struct FlatPath { vg::Path p; uint32_t fill, stroke; bool hasFill, hasStroke; double width; bool evenodd; };

static void flat_walk (Document &d, SceneBuilder &b, const Scene &sc, XNode *n, const Style &parent, const Affine &m, std::vector<FlatPath> &out)
{
	if (!n->element ()) return;
	const char *name = el_name (n);
	if (!strcmp (name, "defs") || !strcmp (name, "sodipodi:namedview") || !strcmp (name, "metadata") || !strcmp (name, "style") || !strcmp (name, "title") || !strcmp (name, "desc") || !strcmp (name, "symbol") || !strcmp (name, "clipPath") || !strcmp (name, "mask") || !strcmp (name, "marker") || !strcmp (name, "pattern")) return;
	Style st;
	compute_style (n, parent, b.sheet, st);
	if (!st.display) return;
	Affine t = m * parse_transform (n->get ("transform"));
	if (!strcmp (name, "g") || !strcmp (name, "a") || !strcmp (name, "svg") || !strcmp (name, "switch"))
	{
		for (XNode *k : n->kids) flat_walk (d, b, sc, k, st, t, out);
		return;
	}
	if (!strcmp (name, "use"))
	{
		const char *h = n->get ("xlink:href");
		if (!h) h = n->get ("href");
		XNode *q = h && h[0] == '#' ? d.by_id (h + 1) : nullptr;
		if (q) flat_walk (d, b, sc, q, st, t * Affine::translate (parse_length (n->get ("x"), 100, 16), parse_length (n->get ("y"), 100, 16)), out);
		return;
	}
	vg::Path p;
	if (!strcmp (name, "text"))
	{
		auto f = sc.objs.find (n->uid);
		if (f == sc.objs.end ()) return;
		p = from_skpath (text_outline (f->second.node->pieces));
	}
	else p = shape_to_path (n);
	if (p.empty () || !st.visible) return;
	p.transform (t);
	FlatPath o;
	o.p = std::move (p);
	auto col = [&] (const PaintSpec &s, bool &has) -> uint32_t {
		has = s.kind != P_NONE;
		if (s.kind == P_COLOR) return s.rgb;
		if (s.kind == P_CURRENT) return st.color;
		return 0x808080;			// (a gradient: its middle would be better)
	};
	o.fill = col (st.fill, o.hasFill);
	o.stroke = col (st.stroke, o.hasStroke);
	o.width = st.strokeWidth * t.mean_scale ();
	o.evenodd = st.fillRule == FR_EVENODD;
	out.push_back (std::move (o));
}
static std::vector<FlatPath> flat_all (Document &d, SceneBuilder &b, const Scene &sc)
{
	std::vector<FlatPath> o;
	Style base;
	Style st;
	compute_style (d.root, base, b.sheet, st);
	for (XNode *k : d.root->kids) flat_walk (d, b, sc, k, st, sc.docToPx, o);
	return o;
}

static void pf (std::string &o, const char *fmt_, ...) __attribute__ ((format (printf, 2, 3)));
static void pf (std::string &o, const char *fmt_, ...)
{
	char b[256];
	va_list a;
	va_start (a, fmt_);
	vsnprintf (b, sizeof b, fmt_, a);
	va_end (a);
	o += b;
}

static std::string to_eps (const std::vector<FlatPath> &v, double w, double h)
{
	double k = 72.0 / 96;
	std::string o;
	pf (o, "%%!PS-Adobe-3.0 EPSF-3.0\n%%%%BoundingBox: 0 0 %d %d\n%%%%Creator: Vector\n%%%%EndComments\n", (int) std::ceil (w * k), (int) std::ceil (h * k));
	pf (o, "%.5f %.5f scale\n0 %.3f translate\n1 -1 scale\n", k, k, h);
	auto path = [&] (const vg::Path &p) {
		o += "newpath\n";
		for (auto &s : p.sub)
		{
			if (s.n.empty ()) continue;
			pf (o, "%.3f %.3f moveto\n", s.n[0].p.x, s.n[0].p.y);
			for (int i = 0; i < s.segments (); i++)
			{
				vg::Bezier b = vg::seg_of (s, i);
				if (b.line ()) pf (o, "%.3f %.3f lineto\n", b.p3.x, b.p3.y);
				else pf (o, "%.3f %.3f %.3f %.3f %.3f %.3f curveto\n", b.p1.x, b.p1.y, b.p2.x, b.p2.y, b.p3.x, b.p3.y);
			}
			if (s.closed) o += "closepath\n";
		}
	};
	for (auto &f : v)
	{
		if (f.hasFill)
		{
			pf (o, "%.3f %.3f %.3f setrgbcolor\n", ((f.fill >> 16) & 255) / 255.0, ((f.fill >> 8) & 255) / 255.0, (f.fill & 255) / 255.0);
			path (f.p);
			o += f.evenodd ? "eofill\n" : "fill\n";
		}
		if (f.hasStroke)
		{
			pf (o, "%.3f %.3f %.3f setrgbcolor %.3f setlinewidth\n", ((f.stroke >> 16) & 255) / 255.0, ((f.stroke >> 8) & 255) / 255.0, (f.stroke & 255) / 255.0, f.width);
			path (f.p);
			o += "stroke\n";
		}
	}
	o += "showpage\n%%EOF\n";
	return o;
}

// R12: POLYLINE and VERTEX are read by everything; millimetres, y up
static std::string to_dxf (const std::vector<FlatPath> &v, double h)
{
	double k = 25.4 / 96;
	std::string o = "0\nSECTION\n2\nENTITIES\n";
	for (auto &f : v)
		for (auto &s : f.p.sub)
		{
			if (s.n.size () < 2) continue;
			std::vector<Vec2> pts { s.n[0].p };
			for (int i = 0; i < s.segments (); i++) vg::flatten (vg::seg_of (s, i), 0.1, pts);
			pf (o, "0\nPOLYLINE\n8\n0\n66\n1\n70\n%d\n", s.closed ? 1 : 0);
			for (auto &p : pts) pf (o, "0\nVERTEX\n8\n0\n10\n%.4f\n20\n%.4f\n", p.x * k, (h - p.y) * k);
			o += "0\nSEQEND\n";
		}
	o += "0\nENDSEC\n0\nEOF\n";
	return o;
}

// HP-GL: 40 units to the millimetre, a pen per colour is not kept
static std::string to_hpgl (const std::vector<FlatPath> &v, double h)
{
	double k = 25.4 / 96 * 40;
	std::string o = "IN;SP1;";
	for (auto &f : v)
		for (auto &s : f.p.sub)
		{
			if (s.n.size () < 2) continue;
			std::vector<Vec2> pts { s.n[0].p };
			for (int i = 0; i < s.segments (); i++) vg::flatten (vg::seg_of (s, i), 0.1, pts);
			if (s.closed) pts.push_back (s.n[0].p);
			pf (o, "PU%d,%d;", (int) (pts[0].x * k), (int) ((h - pts[0].y) * k));
			for (size_t i = 1; i < pts.size (); i++) pf (o, "PD%d,%d;", (int) (pts[i].x * k), (int) ((h - pts[i].y) * k));
			o += '\n';
		}
	o += "PU;SP0;\n";
	return o;
}

// plain SVG: the editors' namespaces go
static void strip (XNode *n)
{
	for (int i = (int) n->attrs.size () - 1; i >= 0; i--)
	{
		const std::string &a = n->attrs[i].name;
		if (!a.compare (0, 9, "inkscape:") || !a.compare (0, 9, "sodipodi:") || a == "xmlns:inkscape" || a == "xmlns:sodipodi")
			n->attrs.erase (n->attrs.begin () + i);
	}
	for (int i = (int) n->kids.size () - 1; i >= 0; i--)
	{
		XNode *k = n->kids[i];
		if (k->element () && (!k->name.compare (0, 9, "sodipodi:") || !k->name.compare (0, 9, "inkscape:"))) { delete k; n->kids.erase (n->kids.begin () + i); continue; }
		strip (k);
	}
}

bool export_format (Document &d, SceneBuilder &b, const Scene &sc, const char *path, const char *fmt_)
{
	std::string o;
	if (!strcmp (fmt_, "svg"))
	{
		XNode *t = d.top->clone ();
		strip (t);
		o = x_write (t);
		delete t;
	}
	else
	{
		auto v = flat_all (d, b, sc);
		if (!strcmp (fmt_, "eps")) o = to_eps (v, sc.width, sc.height);
		else if (!strcmp (fmt_, "dxf")) o = to_dxf (v, sc.height);
		else if (!strcmp (fmt_, "plt")) o = to_hpgl (v, sc.height);
		else return false;
	}
	return vapp::write_file (path, o);
}
void export_other ()
{
	char n[16] = "eps";
	if (!ui_ask ((long) TR ("Export"), (long) TR ("Format: svg (plain), eps, dxf or plt"), (long) n, sizeof n)) return;
	char name[40], f[256];
	snprintf (name, sizeof name, "drawing.%s", n);
	if (!ui_file_save ((long) "SD0:/home/documents", (long) name, (long) f, sizeof f)) return;
	E.say (export_format (E.doc, *E.builder, *E.scene, f, n) ? TR ("Exported.") : TR ("The file could not be written."));
}

// ---- extensions -----------------------------------------------------------------------------------------------------------
struct ExtParam { std::string name, label, value; };
struct Ext { std::string name, command, dir; std::vector<ExtParam> params; };
static std::vector<Ext> s_ext;
static int s_lastExt = -1;

static void ext_load (const char *dir)
{
	DIR *d = opendir (dir);
	if (!d) return;
	while (dirent *e = readdir (d))
	{
		size_t l = strlen (e->d_name);
		if (l < 5 || strcasecmp (e->d_name + l - 4, ".inx")) continue;
		std::string s, p = std::string (dir) + e->d_name;
		if (!vapp::read_file (p.c_str (), s)) continue;
		XDoc x = x_parse (s.data (), s.size (), nullptr);
		if (!x.root) continue;
		Ext ex;
		ex.dir = dir;
		std::function<void (XNode *)> walk = [&] (XNode *n) {
			if (!n->element ()) return;
			if (n->is ("name") && ex.name.empty ()) ex.name = n->text_content ();
			else if (n->is ("command")) ex.command = n->text_content ();
			else if (n->is ("param"))
			{
				ExtParam q;
				q.name = n->get2 ("name", "");
				q.label = n->get2 ("gui-text", q.name.c_str ());
				q.value = n->text_content ();
				ex.params.push_back (q);
				return;
			}
			for (XNode *k : n->kids) walk (k);
		};
		walk (x.root);
		delete x.top;
		if (!ex.command.empty ()) s_ext.push_back (std::move (ex));
	}
	closedir (d);
}
static void ext_scan ()
{
	static bool done;
	if (done) return;
	done = true;
	ext_load ("SD0:/apps/vector.app/ext/");
	ext_load ("SD0:/home/vector/ext/");
}
bool ext_run_on (Document &d, const std::string &cmd)
{
	std::string in = "RAM0:/vector-ext.svg", out;
	if (!vapp::write_file (in.c_str (), d.text ())) return false;
	std::string full = cmd + " " + in;
	FILE *p = popen (full.c_str (), "r");
	if (!p) return false;
	char b[4096];
	size_t n;
	while ((n = fread (b, 1, sizeof b, p)) > 0) out.append (b, n);
	int rc = pclose (p);
	remove (in.c_str ());
	if (rc != 0 || out.empty ()) return false;
	Document r;
	if (!r.load (out.data (), out.size (), nullptr)) return false;
	// the result replaces the root's content, in one undoable step
	d.begin ("Extension");
	std::vector<std::string> old;
	for (auto &a : d.root->attrs) old.push_back (a.name);
	for (auto &o : old) if (!r.root->get (o.c_str ())) d.set (d.root, o.c_str (), nullptr);
	for (auto &a : r.root->attrs) d.set (d.root, a.name.c_str (), a.value);
	std::vector<XNode *> kids = d.root->kids;
	for (XNode *k : kids) d.remove (k);
	for (XNode *k : r.root->kids) d.insert (d.root, -1, k->clone ());
	d.commit ();
	return true;
}
static long s_fields[12];
static Ext *s_cur;
static long ext_cb (long, long, long) { return 0; }
void extensions_menu ()
{
	ext_scan ();
	if (s_ext.empty ()) { E.say (TR ("No extension installed.")); return; }
	std::string l;
	std::vector<char> t;
	for (auto &e : s_ext) { for (char c : e.name) t.push_back (c); t.push_back (0); }
	t.push_back (0);
	long c = ui_popup_local (60, 60, (long) t.data (), 240);
	if (c < 0) return;
	Ext &x = s_ext[c];
	s_cur = &x;
	s_lastExt = (int) c;
	int n = std::min ((int) x.params.size (), 12);
	if (n)
	{
		long d = ui_dialog_begin (420, 80 + n * 40, (long) x.name.c_str ());
		for (int i = 0; i < n; i++)
		{
			ui_label (12, 40 + i * 40, 200, 24, (long) x.params[i].label.c_str (), 0);
			s_fields[i] = ui_textbox (220, 36 + i * 40, 188, 28, 0);
			ui_w_set_text (s_fields[i], (long) x.params[i].value.c_str ());
		}
		ui_dialog_button (d, 0, (long) TR ("Apply"), 1, UB_DEFAULT, 0);
		ui_dialog_button (d, 1, (long) TR ("Cancel"), 2, 0, 0);
		ui_dialog_cancel (d, 2);
		long r = ui_dialog_run (d, (long) ext_cb, 0);
		if (r == 1) for (int i = 0; i < n; i++) x.params[i].value = (const char *) ui_w_get_text (s_fields[i]);
		ui_dialog_end (d);
		if (r != 1) return;
	}
	std::string cmd = x.command;
	if (cmd.find ('/') == std::string::npos) cmd = x.dir + cmd;
	for (auto &p : x.params) cmd += " --" + p.name + "=" + p.value;
	E.sel.clear ();
	if (ext_run_on (E.doc, cmd)) { E.layer = nullptr; E.sync (); }
	else E.say (TR ("The extension failed."));
}

}
