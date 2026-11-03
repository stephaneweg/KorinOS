//
// tools3.cpp -- the rest of the rail (docs/UX-VECTOR.md 3.4), and tracing. No pressure on this system: Calligraphy thins with
// speed. Tracing follows the outlines of a bitmap on the pixels' edges, finds the corners, fits curves between them, from
// the ideas of P. Selinger's published paper "Potrace: a polygon-based tracing algorithm"; it is not that program.
//
#include "editor.h"
#include "../text/fonts.h"
#include "include/codec/SkCodec.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkSurface.h"
#include "include/pathops/SkPathOps.h"
#include <cstdio>
#include <cstring>
#include <set>

using namespace vd;
using namespace vr;

namespace ve {

SkColor sk (unsigned rgb, float a = 1);

static Affine layer_page ()
{
	XNode *l = current_layer ();
	return l == E.doc.root ? E.scene->docToPx : node_to_page (l);
}
static XNode *add_path (const vg::Path &pagePath, const std::string &style, const char *label)
{
	vg::Path p = pagePath;
	p.transform (layer_page ().inverse ());
	E.doc.begin (label);
	XNode *n = add_shape ("path", { { "style", style }, { "d", vg::write_d (p) } });
	E.doc.commit ();
	E.sync ();
	E.select (n);
	return n;
}
static void cursor_ring (SkCanvas *c, Vec2 s, double r)
{
	SkPaint p;
	p.setAntiAlias (true);
	p.setStyle (SkPaint::kStroke_Style);
	p.setColor (sk (jet (J_ACCENT)));
	c->drawCircle ((float) s.x, (float) s.y, (float) r, p);
}

// ---- Calligraphy --------------------------------------------------------------------------------------------------------
class CalligraphyTool : public Tool
{
	std::vector<Vec2> left, right;
	Vec2 last;
	double speed = 0;
	bool on = false;
	int field = 0;
public:
	double width = 12, angle = 30, thin = 0.5;
	const char *hint () override { return TR ("Drag to draw. A fast stroke is thinner."); }
	void press (const Pointer &q) override { left.clear (); right.clear (); last = q.p; speed = 0; on = true; }
	void move (const Pointer &q) override
	{
		if (!on) return;
		double d = vg::dist (q.p, last) * E.view.zoom;
		if (d < 1.5) return;
		speed = speed * 0.7 + d * 0.3;
		double k = 1 - thin * std::min (1.0, speed / 40), w = width / E.view.zoom * k / 2;
		double an = angle * M_PI / 180;
		Vec2 dir = vg::unit (q.p - last), nib (std::cos (an), std::sin (an));
		// a flat nib: what shows is its width across the direction of travel
		double across = std::max (0.15, std::fabs (vg::cross (dir, nib)));
		Vec2 off = nib * (w * across);
		left.push_back (q.p + off);
		right.push_back (q.p - off);
		last = q.p;
		E.dirty = true;
	}
	void release (const Pointer &) override
	{
		on = false;
		if (left.size () < 3) { left.clear (); right.clear (); return; }
		std::vector<Vec2> ring = left;
		ring.insert (ring.end (), right.rbegin (), right.rend ());
		vg::Path p = fit_curve (ring, 0.7 / E.view.zoom);
		p.close ();
		add_path (p, "fill:" + (E.fill == "none" ? std::string ("#000000") : E.fill) + ";stroke:none", TR ("Calligraphy"));
		left.clear (); right.clear ();
	}
	void overlay (SkCanvas *c) override
	{
		if (left.size () < 2) return;
		SkPaint l;
		l.setAntiAlias (true);
		l.setStrokeWidth (1.5f);
		l.setColor (sk (jet (J_ACCENT_VIF)));
		for (size_t i = 1; i < left.size (); i++)
		{
			Vec2 a = E.to_screen (left[i - 1]), b = E.to_screen (left[i]), e = E.to_screen (right[i - 1]), f = E.to_screen (right[i]);
			c->drawLine ((float) a.x, (float) a.y, (float) b.x, (float) b.y, l);
			c->drawLine ((float) e.x, (float) e.y, (float) f.x, (float) f.y, l);
		}
	}
	int bar_count () override { return 4; }
	const char *bar_label (int i) override
	{
		static char b[32];
		if (i < 2) snprintf (b, sizeof b, "%s %.0f", i ? "+" : "\xE2\x88\x92", width);
		else snprintf (b, sizeof b, "%s %.0f\xC2\xB0", i == 3 ? "+" : "\xE2\x88\x92", angle);
		return b;
	}
	void bar_click (int i) override
	{
		if (i == 0 && width > 1) width -= 2;
		else if (i == 1) width += 2;
		else if (i == 2) angle -= 15;
		else if (i == 3) angle += 15;
	}
};

// ---- Eraser -------------------------------------------------------------------------------------------------------------
class EraserTool : public Tool
{
	std::vector<Vec2> pts;
	Vec2 cur;
	bool on = false;
public:
	double width = 16;
	bool cut = true;
	const char *hint () override { return cut ? TR ("Drag to cut the shapes under the eraser.") : TR ("Drag: the objects touched go."); }
	void press (const Pointer &q) override { pts.clear (); pts.push_back (q.p); cur = q.s; on = true; }
	void hover (const Pointer &q) override { cur = q.s; E.dirty = true; }
	void move (const Pointer &q) override { cur = q.s; if (on) pts.push_back (q.p); E.dirty = true; }
	void release (const Pointer &) override
	{
		on = false;
		if (pts.empty ()) return;
		if (pts.size () == 1) pts.push_back (pts[0] + Vec2 (0.01, 0));
		SkPathBuilder b;
		b.moveTo ((float) pts[0].x, (float) pts[0].y);
		for (auto &p : pts) b.lineTo ((float) p.x, (float) p.y);
		SkPaint s;
		s.setStyle (SkPaint::kStroke_Style);
		s.setStrokeWidth ((float) (width / E.view.zoom));
		s.setStrokeCap (SkPaint::kRound_Cap);
		s.setStrokeJoin (SkPaint::kRound_Join);
		SkPath knife = skpathutils::FillPathWithPaint (b.detach (), s);
		SkRect kb = knife.getBounds ();
		Box kbox (kb.left (), kb.top (), kb.right (), kb.bottom ());
		std::vector<XNode *> hit;
		for (XNode *k : current_layer ()->kids) if (k->element () && object_box (k).overlaps (kbox)) hit.push_back (k);
		E.doc.begin (TR ("Erase"));
		for (XNode *n : hit)
		{
			vg::Path p = shape_to_path (n);
			if (p.empty ()) continue;
			p.transform (node_to_page (n));
			SkPath sp = to_skpath (p, false);
			auto inter = ::Op (sp, knife, kIntersect_SkPathOp);
			if (!inter || inter->isEmpty ()) continue;
			if (!cut) { E.doc.remove (n); continue; }
			auto r = ::Op (sp, knife, kDifference_SkPathOp);
			if (!r) continue;
			if (r->isEmpty ()) { E.doc.remove (n); continue; }
			vg::Path out = from_skpath (*r);
			out.transform (node_to_page (n).inverse ());
			XNode *q = convert_to_path (n);
			E.doc.set (q, "d", vg::write_d (out));
			E.doc.set (q, "sodipodi:nodetypes", nullptr);
		}
		E.doc.commit ();
		E.select_none ();
		E.sync ();
		pts.clear ();
	}
	void overlay (SkCanvas *c) override { cursor_ring (c, cur, width / 2); }
	int bar_count () override { return 3; }
	const char *bar_label (int i) override
	{
		static char b[32];
		if (i == 0) return cut ? TR ("Cut") : TR ("Delete");
		snprintf (b, sizeof b, "%s %.0f", i == 1 ? "\xE2\x88\x92" : "+", width);
		return b;
	}
	void bar_click (int i) override
	{
		if (i == 0) cut = !cut;
		else if (i == 1 && width > 2) width -= 4;
		else if (i == 2) width += 4;
	}
};

// ---- tracing, shared by Bucket and the Trace command -------------------------------------------------------------------------
// the outlines of mask (1 inside), on the pixels' edges, as closed polygons of the bitmap's coordinates
static void outlines (const std::vector<uint8_t> &m, int w, int h, std::vector<std::vector<Vec2>> &out)
{
	// an edge is identified by its start corner and direction: right 0, down 1, left 2, up 3
	std::vector<uint8_t> seen ((size_t) (w + 1) * (h + 1), 0);
	auto at = [&] (int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && m[(size_t) y * w + x]; };
	auto start = [&] (int x, int y, int d) {
		// the edge has the inside on its right
		switch (d) { case 0: return at (x, y) && !at (x, y - 1); case 1: return at (x - 1, y) && !at (x, y); case 2: return at (x - 1, y - 1) && !at (x - 1, y); default: return at (x, y - 1) && !at (x - 1, y - 1); }
	};
	for (int y = 0; y <= h; y++)
		for (int x = 0; x <= w; x++)
			for (int d0 = 0; d0 < 4; d0++)
			{
				if (seen[(size_t) y * (w + 1) + x] & (1 << d0) || !start (x, y, d0)) continue;
				std::vector<Vec2> poly;
				int cx = x, cy = y, d = d0;
				for (;;)
				{
					uint8_t &s = seen[(size_t) cy * (w + 1) + cx];
					if (s & (1 << d)) break;
					s |= 1 << d;
					poly.emplace_back (cx, cy);
					static const int DX[] = { 1, 0, -1, 0 }, DY[] = { 0, 1, 0, -1 };
					cx += DX[d]; cy += DY[d];
					// turn right first, keeping the inside on the right; then straight, then left
					int r = (d + 1) & 3, l = (d + 3) & 3;
					if (start (cx, cy, r)) d = r;
					else if (start (cx, cy, d)) {}
					else d = l;
				}
				if (poly.size () >= 4) out.push_back (std::move (poly));
			}
}
// straight stretches stay lines, the rest becomes curves: corners are where the direction turns sharply over a few steps
static vg::Path smooth_polygon (const std::vector<Vec2> &raw, double alpha)
{
	// the midpoints take the staircase away
	int n = (int) raw.size ();
	std::vector<Vec2> q;
	for (int i = 0; i < n; i++) q.push_back ((raw[i] + raw[(i + 1) % n]) / 2);
	const int R = 3;
	std::vector<int> corners;
	for (int i = 0; i < n; i++)
	{
		Vec2 a = vg::unit (q[i] - q[(i + n - R) % n]), b = vg::unit (q[(i + R) % n] - q[i]);
		if (vg::dot (a, b) < alpha)
		{
			// the sharpest of its neighbours
			bool best = true;
			double me = vg::dot (a, b);
			for (int k = -2; k <= 2; k++)
			{
				int j = (i + k + n) % n;
				if (dot (vg::unit (q[j] - q[(j + n - R) % n]), vg::unit (q[(j + R) % n] - q[j])) < me) best = false;
			}
			if (best && (corners.empty () || i - corners.back () > 2)) corners.push_back (i);
		}
	}
	vg::Path p;
	if (corners.empty ())
	{
		std::vector<Vec2> c = q;
		c.push_back (q[0]);
		p = fit_curve (c, 0.6);
		p.close ();
		return p;
	}
	p.move_to (q[corners[0]]);
	for (size_t i = 0; i < corners.size (); i++)
	{
		int a = corners[i], b = corners[(i + 1) % corners.size ()];
		std::vector<Vec2> seg;
		for (int k = a; ; k = (k + 1) % n) { seg.push_back (q[k]); if (k == b && seg.size () > 1) break; if (seg.size () > (size_t) n + 1) break; }
		vg::Path f = fit_curve (seg, 0.6);
		if (!f.empty ())
			for (int k = 1; k < (int) f.sub[0].n.size (); k++)
			{
				const vg::SubPath &s = f.sub[0];
				p.curve_to (s.n[k - 1].out, s.n[k].in, s.n[k].p);
			}
	}
	p.close ();
	return p;
}
vg::Path trace_mask (const std::vector<uint8_t> &m, int w, int h, double alpha, double minArea)
{
	std::vector<std::vector<Vec2>> polys;
	outlines (m, w, h, polys);
	vg::Path all;
	for (auto &poly : polys)
	{
		double a = 0;
		for (size_t i = 0; i < poly.size (); i++) a += vg::cross (poly[i], poly[(i + 1) % poly.size ()]);
		if (std::fabs (a) / 2 < minArea) continue;
		all.append (smooth_polygon (poly, alpha));
	}
	return all;
}

// ---- Bucket -------------------------------------------------------------------------------------------------------------
class BucketTool : public Tool
{
public:
	int threshold = 16;
	const char *hint () override { return TR ("Click a closed area to fill it."); }
	void press (const Pointer &q) override
	{
		// what the page looks like at the view's zoom, limited in size
		double z = std::min (E.view.zoom, 2048 / std::max (E.scene->width, E.scene->height));
		if (z <= 0) return;
		int w = (int) std::ceil (E.scene->width * z), h = (int) std::ceil (E.scene->height * z);
		if (w < 1 || h < 1) return;
		std::vector<uint32_t> px ((size_t) w * h);
		render_page (*E.scene, z, px.data (), w, h, E.scene->page, false);
		int sx = (int) (q.p.x * z), sy = (int) (q.p.y * z);
		if (sx < 0 || sy < 0 || sx >= w || sy >= h) { E.say (TR ("Click inside the page.")); return; }
		uint32_t ref = px[(size_t) sy * w + sx];
		auto near_ = [&] (uint32_t c) {
			int d = 0;
			for (int s = 0; s < 24; s += 8) d = std::max (d, std::abs ((int) ((c >> s) & 255) - (int) ((ref >> s) & 255)));
			return d <= threshold;
		};
		std::vector<uint8_t> m ((size_t) w * h, 0);
		std::vector<int> stack { sy * w + sx };
		m[(size_t) sy * w + sx] = 1;
		bool leaks = false;
		while (!stack.empty ())
		{
			int i = stack.back ();
			stack.pop_back ();
			int x = i % w, y = i / w;
			if (x == 0 || y == 0 || x == w - 1 || y == h - 1) leaks = true;
			for (int k = 0; k < 4; k++)
			{
				int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
				size_t j = (size_t) ny * w + nx;
				if (!m[j] && near_ (px[j])) { m[j] = 1; stack.push_back ((int) j); }
			}
		}
		(void) leaks;
		vg::Path p = trace_mask (m, w, h, 0.5, 4);
		if (p.empty ()) return;
		p.transform (Affine::scale (1 / z, 1 / z));
		add_path (p, "fill:" + (E.fill == "none" ? std::string ("#808080") : E.fill) + ";fill-rule:evenodd;stroke:none", TR ("Fill area"));
	}
	int bar_count () override { return 2; }
	const char *bar_label (int i) override { static char b[32]; snprintf (b, sizeof b, "%s %d", i ? "+" : "\xE2\x88\x92", threshold); return b; }
	void bar_click (int i) override { threshold = std::max (0, std::min (255, threshold + (i ? 8 : -8))); }
};

// the Trace command: an image selected, N levels of brightness become N paths, darkest last
void trace_selection (int levels)
{
	if (E.sel.size () != 1 || strcmp (el_name (E.sel[0]), "image")) { E.say (TR ("Select one image.")); return; }
	XNode *n = E.sel[0];
	auto f = E.scene->objs.find (n->uid);
	if (f == E.scene->objs.end ()) return;
	const DNode *d = f->second.node.get ();
	sk_sp<SkImage> img = d->image;
	if (!img) return;
	int w = std::min (img->width (), 1024), h = img->height () * w / img->width ();
	if (h < 1) return;
	std::vector<uint32_t> px ((size_t) w * h);
	sk_sp<SkSurface> s = SkSurfaces::WrapPixels (SkImageInfo::MakeN32Premul (w, h), px.data (), (size_t) w * 4);
	s->getCanvas ()->clear (SK_ColorWHITE);
	s->getCanvas ()->drawImageRect (img, SkRect::MakeWH ((float) w, (float) h), SkSamplingOptions (SkFilterMode::kLinear));
	std::vector<uint8_t> lum ((size_t) w * h);
	for (size_t i = 0; i < px.size (); i++) { uint32_t c = px[i]; lum[i] = (uint8_t) ((((c >> 16) & 255) * 54 + ((c >> 8) & 255) * 183 + (c & 255) * 19) >> 8); }
	Affine m = node_to_page (n) * Affine::translate (d->dst.left (), d->dst.top ()) * Affine::scale (d->dst.width () / w, d->dst.height () / h);
	E.doc.begin (TR ("Trace image"));
	XNode *g = x_new (X_ELEMENT, "g");
	g->attrs.push_back ({ "id", E.doc.new_id ("g") });
	E.doc.insert (n->parent, n->index () + 1, g);
	Affine toG = parent_to_page (g).inverse ();
	for (int k = 0; k < levels; k++)
	{
		int cut = 255 - (k + 1) * 256 / (levels + 1);
		int shade = levels == 1 ? 0 : 230 - k * 230 / (levels - 1);
		std::vector<uint8_t> mask ((size_t) w * h);
		for (size_t i = 0; i < lum.size (); i++) mask[i] = lum[i] <= cut;
		vg::Path p = trace_mask (mask, w, h, 0.5, 6);
		if (p.empty ()) continue;
		p.transform (toG * m);
		char st[80];
		snprintf (st, sizeof st, "fill:#%02x%02x%02x;fill-rule:evenodd;stroke:none", shade, shade, shade);
		XNode *q = x_new (X_ELEMENT, "path");
		q->attrs.push_back ({ "id", E.doc.new_id ("path") });
		q->attrs.push_back ({ "style", st });
		q->attrs.push_back ({ "d", vg::write_d (p) });
		E.doc.insert (g, -1, q);
	}
	E.doc.commit ();
	E.sync ();
	E.select (g);
}

// ---- Tweak --------------------------------------------------------------------------------------------------------------
class TweakTool : public Tool
{
	Vec2 last, cur;
	bool on = false, open = false;
public:
	double radius = 40, force = 0.5;
	const char *hint () override { return TR ("Drag over selected paths to push them."); }
	void press (const Pointer &q) override { last = q.p; cur = q.s; on = true; if (E.sel.empty ()) E.say (TR ("Select something first.")); }
	void hover (const Pointer &q) override { cur = q.s; E.dirty = true; }
	void move (const Pointer &q) override
	{
		cur = q.s;
		if (!on || E.sel.empty ()) { E.dirty = true; return; }
		Vec2 d = q.p - last;
		if (vg::length (d) * E.view.zoom < 2) return;
		if (!open) { E.doc.begin (TR ("Tweak")); open = true; }
		double r = radius / E.view.zoom;
		for (XNode *&n : E.sel)
		{
			if (strcmp (el_name (n), "path")) n = convert_to_path (n);
			Affine m = node_to_page (n), inv = m.inverse ();
			vg::Path p = shape_to_path (n);
			Vec2 ld = inv.linear (d);
			bool touched = false;
			for (auto &s : p.sub)
				for (auto &nd : s.n)
				{
					double e = vg::dist (m.apply (nd.p), q.p);
					if (e >= r) continue;
					double k = force * (1 - e / r);
					k = k * k * (3 - 2 * k);
					Vec2 v = ld * (k * 2);
					nd.p += v; nd.in += v; nd.out += v;
					touched = true;
				}
			if (touched) E.doc.set (n, "d", vg::write_d (p));
		}
		last = q.p;
		E.sync ();
	}
	void release (const Pointer &) override { on = false; if (open) { E.doc.commit (); open = false; E.sync (); } }
	void overlay (SkCanvas *c) override { cursor_ring (c, cur, radius); }
	int bar_count () override { return 2; }
	const char *bar_label (int i) override { static char b[32]; snprintf (b, sizeof b, "%s %.0f", i ? "+" : "\xE2\x88\x92", radius); return b; }
	void bar_click (int i) override { radius = std::max (6.0, radius + (i ? 8 : -8)); }
};

// ---- Spray --------------------------------------------------------------------------------------------------------------
class SprayTool : public Tool
{
	Vec2 last, cur;
	bool on = false, open = false;
	unsigned seed = 7;
	double rnd () { seed = seed * 1103515245 + 12345; return ((seed >> 8) & 0xFFFF) / 65535.0; }
	std::vector<XNode *> source;
public:
	double radius = 40;
	int amount = 3;
	bool clones = false;
	const char *hint () override { return TR ("Select an object, then drag to spray copies."); }
	void press (const Pointer &q) override
	{
		last = q.p; cur = q.s; on = true;
		source = E.sel;
		if (source.empty ()) { E.say (TR ("Select something first.")); on = false; }
	}
	void hover (const Pointer &q) override { cur = q.s; E.dirty = true; }
	void move (const Pointer &q) override
	{
		cur = q.s;
		E.dirty = true;
		if (!on || vg::dist (q.p, last) * E.view.zoom < radius / 2) return;
		last = q.p;
		if (!open) { E.doc.begin (TR ("Spray")); open = true; }
		double r = radius / E.view.zoom;
		for (int i = 0; i < amount; i++)
		{
			XNode *src = source[(size_t) (rnd () * source.size ()) % source.size ()];
			Box b = object_box (src);
			double a = rnd () * 2 * M_PI, l = std::sqrt (rnd ()) * r;
			Vec2 at = q.p + Vec2 (std::cos (a), std::sin (a)) * l;
			XNode *c;
			if (clones)
			{
				if (!src->get ("id")) E.doc.set (src, "id", E.doc.new_id ("o"));
				c = x_new (X_ELEMENT, "use");
				c->attrs.push_back ({ "xlink:href", std::string ("#") + src->get ("id") });
			}
			else c = src->clone ();
			bool hasId = false;
			for (auto &t : c->attrs) if (t.name == "id") { t.value = E.doc.new_id ("o"); hasId = true; }
			if (!hasId) c->attrs.push_back ({ "id", E.doc.new_id ("o") });
			E.doc.insert (src->parent, -1, c);
			Affine P = parent_to_page (c);
			Affine d = P.inverse () * Affine::rotate_at (rnd () * 0.6 - 0.3, b.center ()) * Affine::translate (at.x - b.center ().x, at.y - b.center ().y) ;
			(void) d;
			Affine mv = P.inverse () * Affine::translate (at.x - b.center ().x, at.y - b.center ().y) * P;
			E.doc.set (c, "transform", write_transform (mv * (clones ? Affine () : node_transform (c))).c_str ());
		}
		E.sync ();
	}
	void release (const Pointer &) override { on = false; if (open) { E.doc.commit (); open = false; E.sync (); } E.sel = source; }
	void overlay (SkCanvas *c) override { cursor_ring (c, cur, radius); }
	int bar_count () override { return 3; }
	const char *bar_label (int i) override { static char b[32]; if (i == 0) return clones ? TR ("Clones") : TR ("Copies"); snprintf (b, sizeof b, "%s %.0f", i == 1 ? "\xE2\x88\x92" : "+", radius); return b; }
	void bar_click (int i) override { if (i == 0) clones = !clones; else radius = std::max (6.0, radius + (i == 2 ? 8 : -8)); }
};

// ---- Connector ----------------------------------------------------------------------------------------------------------
void update_connectors ()
{
	std::vector<XNode *> all;
	std::function<void (XNode *)> walk = [&] (XNode *n) { if (n->element ()) { if (n->get ("inkscape:connector-type")) all.push_back (n); for (XNode *k : n->kids) walk (k); } };
	walk (E.doc.root);
	for (XNode *c : all)
	{
		const char *a = c->get ("inkscape:connection-start"), *b = c->get ("inkscape:connection-end");
		XNode *x = a && a[0] == '#' ? E.doc.by_id (a + 1) : nullptr, *y = b && b[0] == '#' ? E.doc.by_id (b + 1) : nullptr;
		if (!x || !y) continue;
		Affine inv = node_to_page (c).inverse ();
		Vec2 p = inv.apply (object_box (x).center ()), q = inv.apply (object_box (y).center ());
		vg::Path d;
		d.move_to (p);
		if (!strcmp (c->get ("inkscape:connector-type"), "orthogonal")) { d.line_to ({ (p.x + q.x) / 2, p.y }); d.line_to ({ (p.x + q.x) / 2, q.y }); }
		d.line_to (q);
		std::string s = vg::write_d (d);
		if (s != c->get2 ("d", "")) E.doc.set (c, "d", s);
	}
}
class ConnectorTool : public Tool
{
	XNode *from = nullptr;
	Vec2 a, cur;
public:
	bool ortho = false;
	const char *hint () override { return TR ("Drag from one object to another."); }
	void press (const Pointer &q) override
	{
		from = hit_object (q.p, 3 / E.view.zoom);
		if (from) { a = object_box (from).center (); cur = q.p; }
	}
	void move (const Pointer &q) override { cur = q.p; if (from) E.dirty = true; }
	void release (const Pointer &q) override
	{
		XNode *to = from ? hit_object (q.p, 3 / E.view.zoom) : nullptr;
		if (from && to && to != from)
		{
			for (XNode *n : { from, to }) if (!n->get ("id")) { E.doc.begin ("id"); E.doc.set (n, "id", E.doc.new_id ("o")); E.doc.commit (); }
			E.doc.begin (TR ("Connector"));
			if (!E.doc.root->get ("xmlns:inkscape")) E.doc.set (E.doc.root, "xmlns:inkscape", "http://www.inkscape.org/namespaces/inkscape");
			add_shape ("path", { { "style", "fill:none;stroke:#000000;stroke-width:" + fmt (E.strokeWidth) }, { "d", "M0 0" }, { "inkscape:connector-type", ortho ? "orthogonal" : "polyline" },
				{ "inkscape:connection-start", std::string ("#") + from->get ("id") }, { "inkscape:connection-end", std::string ("#") + to->get ("id") } });
			E.sync ();
			update_connectors ();
			E.doc.commit ();
			E.sync ();
		}
		from = nullptr;
		E.dirty = true;
	}
	void overlay (SkCanvas *c) override
	{
		if (!from) return;
		Vec2 p = E.to_screen (a), q = E.to_screen (cur);
		SkPaint l;
		l.setAntiAlias (true); l.setColor (sk (jet (J_ACCENT))); l.setStrokeWidth (1.5f);
		c->drawLine ((float) p.x, (float) p.y, (float) q.x, (float) q.y, l);
	}
	int bar_count () override { return 1; }
	const char *bar_label (int) override { return ortho ? TR ("Elbow") : TR ("Straight"); }
	void bar_click (int) override { ortho = !ortho; }
};

// ---- Measure ------------------------------------------------------------------------------------------------------------
class MeasureTool : public Tool
{
	Vec2 a, b;
	bool has = false;
public:
	const char *hint () override { return TR ("Drag to measure."); }
	void press (const Pointer &q) override { a = b = q.p; has = true; snap_begin (false); a = b = snap_point (q.p, q.mods); }
	void move (const Pointer &q) override { b = snap_point (q.p, q.mods); E.dirty = true; }
	void release (const Pointer &) override { snap_end (); }
	void overlay (SkCanvas *c) override
	{
		if (!has) return;
		Vec2 p = E.to_screen (a), q = E.to_screen (b);
		SkPaint h, l;
		h.setAntiAlias (true); l.setAntiAlias (true);
		h.setStrokeWidth (3); h.setColor (sk (jet (J_SURFACE), 0.7f));
		l.setStrokeWidth (1.5f); l.setColor (sk (jet (J_ACCENT)));
		c->drawLine ((float) p.x, (float) p.y, (float) q.x, (float) q.y, h);
		c->drawLine ((float) p.x, (float) p.y, (float) q.x, (float) q.y, l);
		paint_handle (c, p, 2, false);
		paint_handle (c, q, 2, false);
		char m[80];
		Vec2 d = b - a;
		snprintf (m, sizeof m, "%.2f px  %.1f\xC2\xB0  %+.1f, %+.1f", vg::length (d), std::atan2 (-d.y, d.x) * 180 / M_PI, d.x, d.y);
		paint_badge (c, (p + q) / 2, m);
	}
};

// ---- Shape builder --------------------------------------------------------------------------------------------------------
class BuilderTool : public Tool
{
	bool keepRest = true;
	std::vector<SkPath> regions;
	std::vector<bool> on;
	std::vector<XNode *> sources;
	int hot = -1;
	void build ()
	{
		regions.clear (); on.clear (); sources.clear ();
		for (XNode *n : E.sel)
		{
			vg::Path p = shape_to_path (n);
			if (p.empty ()) continue;
			p.transform (node_to_page (n));
			SkPath s = to_skpath (p, false);
			sources.push_back (n);
			std::vector<SkPath> next;
			SkPath left = s;
			for (auto &r : regions)
			{
				auto i = ::Op (r, s, kIntersect_SkPathOp), o = ::Op (r, s, kDifference_SkPathOp), l = ::Op (left, r, kDifference_SkPathOp);
				if (i && !i->isEmpty ()) next.push_back (*i);
				if (o && !o->isEmpty ()) next.push_back (*o);
				if (l) left = *l;
			}
			if (!left.isEmpty ()) next.push_back (left);
			regions.swap (next);
		}
		on.assign (regions.size (), false);
	}
	int at (Vec2 p)
	{
		for (int i = (int) regions.size () - 1; i >= 0; i--) if (regions[i].contains ((float) p.x, (float) p.y)) return i;
		return -1;
	}
	void apply ()
	{
		bool any = false;
		for (bool b : on) if (b) any = true;
		if (!any) return;
		SkPath u;
		bool first = true;
		for (size_t i = 0; i < regions.size (); i++)
			if (on[i]) { if (first) { u = regions[i]; first = false; } else if (auto r = ::Op (u, regions[i], kUnion_SkPathOp)) u = *r; }
		std::string style = sources[0]->get2 ("style", "");
		E.doc.begin (TR ("Build shape"));
		vg::Path p = from_skpath (u);
		p.transform (parent_to_page (sources[0]).inverse ());
		XNode *n = x_new (X_ELEMENT, "path");
		n->attrs.push_back ({ "id", E.doc.new_id ("path") });
		n->attrs.push_back ({ "style", style });
		n->attrs.push_back ({ "d", vg::write_d (p) });
		int at = sources[0]->index () + 1;
		XNode *par = sources[0]->parent;
		E.doc.insert (par, at++, n);
		// what was not touched stays, as pieces
		if (keepRest)
			for (size_t i = 0; i < regions.size (); i++)
			{
				if (on[i]) continue;
				vg::Path q = from_skpath (regions[i]);
				q.transform (parent_to_page (sources[0]).inverse ());
				XNode *k = x_new (X_ELEMENT, "path");
				k->attrs.push_back ({ "id", E.doc.new_id ("path") });
				k->attrs.push_back ({ "style", style });
				k->attrs.push_back ({ "d", vg::write_d (q) });
				E.doc.insert (par, at++, k);
			}
		for (XNode *s : sources) E.doc.remove (s);
		E.doc.commit ();
		E.sel = { n };
		E.sync ();
		regions.clear (); on.clear ();
	}
public:
	const char *hint () override { return TR ("Drag over the regions to join them. Enter applies."); }
	void enter () override { build (); }
	void leave () override { regions.clear (); on.clear (); }
	void press (const Pointer &q) override { if (regions.empty ()) build (); int i = at (q.p); if (i >= 0) on[i] = !(q.mods & MOD_ALT); E.dirty = true; }
	void move (const Pointer &q) override { int i = at (q.p); if (i >= 0) on[i] = !(q.mods & MOD_ALT); hot = i; E.dirty = true; }
	void hover (const Pointer &q) override { int i = at (q.p); if (i != hot) { hot = i; E.dirty = true; } }
	bool key (int hid, int, int) override { if (hid == KEY_ENTER) { apply (); return true; } return false; }
	void overlay (SkCanvas *c) override
	{
		SkMatrix m = SkMatrix::MakeAll ((float) E.view.zoom, 0, (float) E.view.ox, 0, (float) E.view.zoom, (float) E.view.oy, 0, 0, 1);
		for (size_t i = 0; i < regions.size (); i++)
		{
			if (!on[i] && (int) i != hot) continue;
			SkPaint f;
			f.setAntiAlias (true);
			f.setColor (sk (jet (on[i] ? J_ACCENT : J_ACCENT_CLAIR), 0.5f));
			c->drawPath (regions[i].makeTransform (m), f);
		}
	}

	int bar_count () override { return 3; }
	const char *bar_label (int i) override { return i == 0 ? TR ("Apply") : i == 1 ? TR ("Cancel") : keepRest ? TR ("Keep the rest") : TR ("Drop the rest"); }
	void bar_click (int i) override
	{
		if (i == 2) keepRest = !keepRest;
		else if (i == 1) { on.assign (on.size (), false); E.dirty = true; }
		else apply ();
	}
};

void make_tools3 ()
{
	E.tools[T_CALLI] = new CalligraphyTool;
	E.tools[T_ERASER] = new EraserTool;
	E.tools[T_BUCKET] = new BucketTool;
	E.tools[T_TWEAK] = new TweakTool;
	E.tools[T_SPRAY] = new SprayTool;
	E.tools[T_CONNECT] = new ConnectorTool;
	E.tools[T_MEASURE] = new MeasureTool;
	E.tools[T_BUILDER] = new BuilderTool;
}

}
