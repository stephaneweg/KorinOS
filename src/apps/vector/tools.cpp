//
// tools.cpp -- Vector without a window: everything the QEMU checks need (tools/vector) runs from here.
//
#include "app.h"
#include "ui/editor.h"
#include "render/tiles.h"
#include "include/docs/SkPDFDocument.h"
#include "include/core/SkDocument.h"
#include <unistd.h>
#include "include/core/SkData.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unistd.h>
#include <string>

using namespace vd;
using namespace vr;

namespace vapp {

static unsigned s_seed = 12345;
static unsigned rnd () { s_seed = s_seed * 1103515245 + 12345; return s_seed >> 8; }

static double now_ms ()
{
	using namespace std::chrono;
	return duration_cast<microseconds> (steady_clock::now ().time_since_epoch ()).count () / 1000.0;
}

bool read_file (const char *path, std::string &out)
{
	FILE *f = fopen (path, "rb");
	if (!f) return false;
	char b[65536];
	size_t n;
	out.clear ();
	while ((n = fread (b, 1, sizeof b, f)) > 0) out.append (b, n);
	fclose (f);
	return true;
}
bool write_file (const char *path, const std::string &s)
{
	FILE *f = fopen (path, "wb");
	if (!f) return false;
	size_t n = fwrite (s.data (), 1, s.size (), f);
	fclose (f);
	return n == s.size ();
}

static int tool_render (const char *in, const char *out, double zoom)
{
	std::string s, err;
	if (!read_file (in, s)) { printf ("render: cannot read %s\n", in); return 1; }
	Document d;
	if (!d.load (s.data (), s.size (), &err)) { printf ("render: %s: %s\n", in, err.c_str ()); return 1; }
	d.path = in;
	double t0 = now_ms ();
	SceneBuilder b (&d);
	auto sc = b.update ();
	double t1 = now_ms ();
	int w = (int) std::ceil (sc->width * zoom), h = (int) std::ceil (sc->height * zoom);
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (w > 8192) w = 8192;
	if (h > 8192) h = 8192;
	std::vector<uint32_t> px ((size_t) w * h);
	render_page (*sc, zoom, px.data (), w, h, 0xFFFFFF, false);
	double t2 = now_ms ();
	SkPixmap pm (SkImageInfo::MakeN32Premul (w, h), px.data (), (size_t) w * 4);
	SkFILEWStream st (out);
	bool ok = st.isValid () && SkPngEncoder::Encode (&st, pm, SkPngEncoder::Options ());
	printf ("render: %s %dx%d scene %.1f ms paint %.1f ms %s\n", in, w, h, t1 - t0, t2 - t1, ok ? "ok" : "PNG FAILED");
	return ok ? 0 : 1;
}

static int tool_roundtrip (const char *in, const char *out)
{
	std::string s, err;
	if (!read_file (in, s)) return 1;
	Document d;
	if (!d.load (s.data (), s.size (), &err)) { printf ("roundtrip: %s\n", err.c_str ()); return 1; }
	std::string a = d.text ();
	Document e;
	if (!e.load (a.data (), a.size (), &err)) { printf ("roundtrip: second read: %s\n", err.c_str ()); return 1; }
	bool same = a == e.text ();
	printf ("roundtrip: %s %s\n", in, same ? "PASS" : "FAIL");
	if (out) write_file (out, a);
	return same ? 0 : 1;
}


static int tool_bench (int n, const char *out)
{
	std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"600\" viewBox=\"0 0 800 600\">\n";
	char b[300];
	for (int i = 0; i < n; i++)
	{
		int x = rnd () % 780, y = rnd () % 580, w = 4 + rnd () % 40, h = 4 + rnd () % 40;
		unsigned c = rnd () & 0xFFFFFF;
		switch (i % 4)
		{
		case 0: snprintf (b, sizeof b, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#%06x\"/>\n", x, y, w, h, c); break;
		case 1: snprintf (b, sizeof b, "<circle cx=\"%d\" cy=\"%d\" r=\"%d\" fill=\"#%06x\" stroke=\"#000\"/>\n", x, y, w / 2, c); break;
		case 2: snprintf (b, sizeof b, "<path d=\"M%d %d c%d %d %d %d %d %d z\" fill=\"#%06x\" fill-opacity=\"0.6\"/>\n", x, y, w, 0, w, h, 0, h, c); break;
		default: snprintf (b, sizeof b, "<path d=\"M%d %d l%d %d l%d %d\" fill=\"none\" stroke=\"#%06x\" stroke-width=\"2\"/>\n", x, y, w, h, -w, h, c); break;
		}
		s += b;
	}
	s += "</svg>\n";
	std::string log;
	auto say = [&] (const char *fmt, double v) { char l[120]; snprintf (l, sizeof l, fmt, v); printf ("%s\n", l); log += l; log += '\n'; };
	Document d;
	double t0 = now_ms ();
	d.load (s.data (), s.size (), nullptr);
	double t1 = now_ms ();
	say ("parse: %.1f ms", t1 - t0);
	SceneBuilder sb (&d);
	auto sc = sb.update ();
	double t2 = now_ms ();
	say ("scene: %.1f ms", t2 - t1);
	std::vector<uint32_t> px (800 * 600);
	render_page (*sc, 1, px.data (), 800, 600, 0xFFFFFF, false);
	double t3 = now_ms ();
	say ("whole page, one thread: %.1f ms", t3 - t2);
	for (int workers : { 1, 2, 3, 4 })
	{
		TileCache tc (workers);
		tc.set_scene (sc, nullptr);
		ViewState v;
		double a = now_ms ();
		tc.compose (v, px.data (), 800, 600, 800);
		tc.wait_idle ();
		tc.compose (v, px.data (), 800, 600, 800);
		double e = now_ms ();
		char l[80];
		snprintf (l, sizeof l, "tiles, %d worker(s): %%.1f ms", workers);
		say (l, e - a);
	}
	{
		TileCache tc (3);
		tc.set_scene (sc, nullptr);
		ViewState v;
		tc.compose (v, px.data (), 800, 600, 800);
		tc.wait_idle ();
		double a = now_ms ();
		for (int i = 0; i < 100; i++) { tc.compose (v, px.data (), 800, 600, 800); }
		say ("pan on cached tiles: %.2f ms per frame", (now_ms () - a) / 100);
		// one object moved
		XNode *el = nullptr;
		for (XNode *k : d.root->kids) if (k->element ()) el = k;
		a = now_ms ();
		for (int i = 0; i < 20; i++)
		{
			d.begin ("move");
			char t[40];
			snprintf (t, sizeof t, "translate(%d,0)", i);
			d.set (el, "transform", t);
			d.commit ();
			sc = sb.update ();
			tc.set_scene (sc, sb.lastAll ? nullptr : &sb.lastDamage);
			tc.compose (v, px.data (), 800, 600, 800);
			tc.wait_idle ();
			tc.compose (v, px.data (), 800, 600, 800);
		}
		say ("one object moved, whole cycle: %.1f ms", (now_ms () - a) / 20);
		// the journal
		bool same;
		std::string before;
		while (d.undo ()) {}
		before = d.text ();
		same = before == x_write (d.top);
		say ("undo all: %.0f (1 ok)", same);
	}
	if (out) write_file (out, log);
	return 0;
}

// every line of the list: a.svg -> a.png in the same folder, one report line each; a broken file must not stop the rest
static int tool_batch (const char *list, const char *report)
{
	std::string s, log;
	if (!read_file (list, s)) return 1;
	size_t a = 0;
	int done = 0;
	double t0 = now_ms ();
	FILE *rep = fopen (report, "wb");
	while (a < s.size ())
	{
		size_t e = s.find ('\n', a);
		if (e == std::string::npos) e = s.size ();
		std::string f = s.substr (a, e - a);
		a = e + 1;
		if (!f.empty () && f.back () == '\r') f.pop_back ();
		if (f.empty ()) continue;
		int rw = 0, rh = 0;			// the size to fit, after a tab
		size_t tab = f.find ('\t');
		if (tab != std::string::npos) { sscanf (f.c_str () + tab + 1, "%d %d", &rw, &rh); f.resize (tab); }
		std::string in, err;
		const char *st = "ok";
		Document d;
		if (!read_file (f.c_str (), in)) st = "unreadable";
		else if (!d.load (in.data (), in.size (), &err)) st = "badxml";
		else
		{
			d.path = f;
			SceneBuilder b (&d);
			auto sc = b.update ();
			int w = rw ? rw : (int) std::ceil (sc->width), h = rh ? rh : (int) std::ceil (sc->height);
			if (w < 1 || h < 1 || w > 4096 || h > 4096) st = "size";
			else
			{
				std::vector<uint32_t> px ((size_t) w * h);
				render_page (*sc, sc->width > 0 ? w / sc->width : 1, px.data (), w, h, 0, true);
				std::string out = f.substr (0, f.size () - 4) + ".out";
				SkPixmap pm (SkImageInfo::MakeN32Premul (w, h), px.data (), (size_t) w * 4);
				SkFILEWStream os (out.c_str ());
				if (!SkPngEncoder::Encode (&os, pm, SkPngEncoder::Options ())) st = "png";
			}
		}
		if (rep) { fprintf (rep, "%s %s\n", st, f.c_str ()); fflush (rep); }
		done++;
	}
	if (rep) { fprintf (rep, "done %d %.0f ms\n", done, now_ms () - t0); fclose (rep); }
	printf ("batch: %d files %.0f ms\n", done, now_ms () - t0);
	return 0;
}

// random transactions: undoing them all must give the file back to the byte, redoing them the state they reached
static int tool_fuzz (const char *in, int rounds)
{
	std::string src;
	if (!read_file (in, src)) return 1;
	Document d;
	if (!d.load (src.data (), src.size (), nullptr)) return 1;
	std::string start = d.text ();
	std::vector<XNode *> all;
	std::function<void (XNode *)> walk = [&] (XNode *n) { if (n->element ()) { all.push_back (n); for (XNode *k : n->kids) walk (k); } };
	int ops = 0, txs = 0;
	for (int r = 0; r < rounds; r++)
	{
		all.clear ();
		walk (d.root);
		d.begin ("fuzz");
		int n = 1 + rnd () % 4;
		for (int i = 0; i < n; i++)
		{
			XNode *t = all[rnd () % all.size ()];
			switch (rnd () % 6)
			{
			case 0: d.set (t, "fill", "#123456"); break;
			case 1: d.set (t, "transform", "translate(1,2)"); break;
			case 2: if (!t->attrs.empty ()) d.set (t, t->attrs[rnd () % t->attrs.size ()].name.c_str (), nullptr); break;
			case 3: { XNode *c = x_new (X_ELEMENT, "rect"); c->attrs.push_back ({ "id", d.new_id ("fz") }); d.insert (t, rnd () % (t->kids.size () + 1), c); all.push_back (c); break; }
			case 4:
				if (t != d.root)
				{
					d.remove (t);
					all.clear ();
					walk (d.root);
				}
				break;
			default:
			{
				if (t == d.root) break;
				XNode *to = all[rnd () % all.size ()];
				bool inside = false;
				for (XNode *p = to; p; p = p->parent) if (p == t) inside = true;
				if (!inside) d.move (t, to, rnd () % (to->kids.size () + 1));
				break;
			}
			}
			ops++;
		}
		d.commit ();
		txs++;
	}
	std::string end = d.text ();
	while (d.undo ()) {}
	bool back = d.text () == start;
	while (d.redo ()) {}
	bool forth = d.text () == end;
	while (d.undo ()) {}
	bool again = d.text () == start;
	printf ("fuzz: %d transactions, %d operations: undo %s, redo %s, undo again %s\n", txs, ops, back ? "PASS" : "FAIL", forth ? "PASS" : "FAIL", again ? "PASS" : "FAIL");
	return back && forth && again ? 0 : 1;
}

// ---- two extensions of ours: they follow the protocol, the program is this one -------------------------------------------------
static std::string arg_of (int argc, char **argv, const char *name, const char *def)
{
	std::string k = std::string ("--") + name + "=";
	for (int i = 0; i < argc; i++) if (!strncmp (argv[i], k.c_str (), k.size ())) return argv[i] + k.size ();
	return def;
}
static int ext_out (Document &d) { fputs (d.text ().c_str (), stdout); return 0; }
static XNode *el_new (const char *n, const std::vector<std::pair<const char *, std::string>> &a)
{
	XNode *e = x_new (X_ELEMENT, n);
	for (auto &p : a) e->attrs.push_back ({ p.first, p.second });
	return e;
}
static std::string num (double v) { char b[32]; snprintf (b, sizeof b, "%.6g", v); return b; }
static int ext_gear (int argc, char **argv)
{
	std::string s;
	Document d;
	if (!read_file (argv[argc - 1], s) || !d.load (s.data (), s.size (), nullptr)) return 1;
	int teeth = atoi (arg_of (argc, argv, "teeth", "16").c_str ());
	double r = atof (arg_of (argc, argv, "radius", "60").c_str ()), depth = r * 0.15, cx = r + 20, cy = r + 20;
	if (teeth < 3) teeth = 3;
	vg::Path p;
	for (int i = 0; i < teeth; i++)
	{
		double a = 2 * M_PI * i / teeth, w = 2 * M_PI / teeth;
		double T[4] = { a, a + w * .25, a + w * .5, a + w * .75 }, R[4] = { r - depth, r + depth, r + depth, r - depth };
		for (int k = 0; k < 4; k++)
		{
			vg::Vec2 q (cx + R[k] * std::cos (T[k]), cy + R[k] * std::sin (T[k]));
			if (p.empty ()) p.move_to (q); else p.line_to (q);
		}
	}
	p.close ();
	d.begin ("gear");
	d.insert (d.root, -1, el_new ("path", { { "d", vg::write_d (p) }, { "style", "fill:#4992a7;stroke:#000000" } }));
	d.commit ();
	return ext_out (d);
}
static int ext_grid (int argc, char **argv)
{
	std::string s;
	Document d;
	if (!read_file (argv[argc - 1], s) || !d.load (s.data (), s.size (), nullptr)) return 1;
	int nx = atoi (arg_of (argc, argv, "columns", "10").c_str ()), ny = atoi (arg_of (argc, argv, "rows", "10").c_str ());
	double st = atof (arg_of (argc, argv, "step", "20").c_str ());
	vg::Path p;
	for (int i = 0; i <= nx; i++) { p.move_to ({ i * st, 0 }); p.line_to ({ i * st, ny * st }); }
	for (int i = 0; i <= ny; i++) { p.move_to ({ 0, i * st }); p.line_to ({ nx * st, i * st }); }
	d.begin ("grid");
	d.insert (d.root, -1, el_new ("path", { { "d", vg::write_d (p) }, { "style", "fill:none;stroke:#808080;stroke-width:" + num (0.5) } }));
	d.commit ();
	return ext_out (d);
}
static int tool_export (const char *in, const char *out, const char *fmt_)
{
	std::string s;
	Document d;
	if (!read_file (in, s) || !d.load (s.data (), s.size (), nullptr)) return 1;
	SceneBuilder b (&d);
	auto sc = b.update ();
	if (!strcmp (fmt_, "pdf"))
	{
		SkFILEWStream st (out);
		SkPDF::Metadata md;
		auto doc = SkPDF::MakeDocument (&st, md);
		float k = 72.f / 96;
		SkCanvas *c = doc->beginPage ((float) sc->width * k, (float) sc->height * k);
		c->scale (k, k);
		draw_scene (c, *sc);
		doc->endPage ();
		doc->close ();
		printf ("export: pdf ok\n");
		return 0;
	}
	bool ok = ve::export_format (d, b, *sc, out, fmt_);
	printf ("export: %s %s\n", fmt_, ok ? "ok" : "FAILED");
	return ok ? 0 : 1;
}

int run_tool (int argc, char **argv)
{
	if (!strcmp (argv[1], "--ext-gear")) return ext_gear (argc, argv);
	if (!strcmp (argv[1], "--ext-grid")) return ext_grid (argc, argv);
	if (!strcmp (argv[1], "--export") && argc >= 5) return tool_export (argv[2], argv[3], argv[4]);
	if (!strcmp (argv[1], "--ext-run") && argc >= 4)
	{
		std::string s;
		Document d;
		if (!read_file (argv[2], s) || !d.load (s.data (), s.size (), nullptr)) return 1;
		bool ok = ve::ext_run_on (d, argv[3]);
		if (ok && argc > 4) write_file (argv[4], d.text ());
		printf ("ext-run: %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 1;
	}
	if (!strcmp (argv[1], "--fuzz") && argc >= 4) return tool_fuzz (argv[2], atoi (argv[3]));
	if (!strcmp (argv[1], "--batch") && argc >= 4) return tool_batch (argv[2], argv[3]);
	if (!strcmp (argv[1], "--delay") && argc > 3) { sleep (atoi (argv[2])); return run_tool (argc - 2, argv + 2); }
	if (!strcmp (argv[1], "--render") && argc >= 4) return tool_render (argv[2], argv[3], argc > 4 ? atof (argv[4]) : 1);
	if (!strcmp (argv[1], "--roundtrip") && argc >= 3) return tool_roundtrip (argv[2], argc > 3 ? argv[3] : nullptr);
	if (!strcmp (argv[1], "--bench") && argc >= 3) return tool_bench (atoi (argv[2]), argc > 3 ? argv[3] : nullptr);
	printf ("usage: vector [file.svg] | --render a.svg b.png [zoom] | --bench N [out] | --roundtrip a.svg [b.svg]\n");
	return 2;
}

}
