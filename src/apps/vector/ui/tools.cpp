//
// tools.cpp -- the tools of the rail (docs/UX-VECTOR.md 3). A gesture is one transaction: begin on the first real movement,
// commit on release, cancel on Escape. The tools speak page px; each converts to the coordinates of what it edits.
//
#include "editor.h"
#include "../text/fonts.h"
#include "include/core/SkFont.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include <cstdio>
#include <cstring>
#include <set>

using namespace vd;
using namespace vr;

namespace ve {

SkColor sk (unsigned rgb, float a = 1);

static Affine layer_to_page ()
{
	XNode *l = current_layer ();
	return l == E.doc.root ? E.scene->docToPx : node_to_page (l);
}
static std::string new_style ()
{
	std::string s = "fill:" + E.fill + ";stroke:" + E.stroke;
	if (E.stroke != "none") s += ";stroke-width:" + fmt (E.strokeWidth);
	return s;
}
static void dashed_line (SkCanvas *c, Vec2 a, Vec2 b, unsigned col)
{
	SkPaint p;
	p.setAntiAlias (true);
	p.setColor (sk (col));
	p.setStrokeWidth (1.5f);
	c->drawLine ((float) a.x, (float) a.y, (float) b.x, (float) b.y, p);
}

// ---- Selector ---------------------------------------------------------------------------------------------------------
class SelectTool : public Tool
{
	enum { M_NONE, M_MAYBE, M_MOVE, M_SCALE, M_ROTATE, M_RUBBER } mode = M_NONE;
	int handle = H_NONE;
	Pointer start;
	Box box0;
	Vec2 cur;
	std::vector<Affine> orig;
	bool open = false, moved = false;
	char badge[60] = "";
	XNode *pressed = nullptr;

	void remember ()
	{
		orig.clear ();
		for (XNode *n : E.sel) orig.push_back (node_transform (n));
		box0 = selection_box ();
	}
	void set_all (const Affine &m)
	{
		for (size_t i = 0; i < E.sel.size (); i++)
		{
			XNode *n = E.sel[i];
			Affine P = parent_to_page (n);
			Affine d = P.inverse () * m * P;
			E.doc.set (n, "transform", write_transform (d * orig[i]).c_str ());
		}
		E.sync ();
	}
public:
	const char *hint () override { return TR ("Click to select. Drag to move. Handles scale and turn."); }
	int cursor () override { return UC_ARROW; }
	void hover (const Pointer &q) override
	{
		int h = handle_at (q.s);
		static const int C[] = { UC_NWSE, UC_VRESIZE, UC_NESW, UC_HRESIZE, UC_NWSE, UC_VRESIZE, UC_NESW, UC_HRESIZE, UC_HAND };
		ui_cursor (h >= 0 ? C[h] : hit_object (q.p, 3 / E.view.zoom) ? UC_MOVE : UC_ARROW);
	}
	void press (const Pointer &q) override
	{
		start = q; cur = q.s; moved = false; pressed = nullptr;
		int h = handle_at (q.s);
		if (h != H_NONE)
		{
			remember ();
			handle = h;
			mode = h == H_ROT ? M_ROTATE : M_SCALE;
			return;
		}
		XNode *o = hit_object (q.p, 4 / E.view.zoom, nullptr, (q.mods & MOD_CTRL) != 0);
		if (o)
		{
			if (q.clicks >= 2)
			{
				const char *n = el_name (o);
				if (!strcmp (n, "text")) { E.select (o); E.set_tool (T_TEXT); return; }
				if (!strcmp (n, "g")) { E.select (o); return; }
				E.select (o);
				E.set_tool (T_NODE);
				return;
			}
			if (q.mods & MOD_SHIFT) { E.select (o, true); mode = M_NONE; return; }
			if (!E.selected (o)) E.select (o);
			pressed = o;
			remember ();
			mode = M_MAYBE;
			return;
		}
		if (!(q.mods & MOD_SHIFT)) E.select_none ();
		mode = M_RUBBER;
	}
	void move (const Pointer &q) override
	{
		cur = q.s;
		switch (mode)
		{
		case M_MAYBE:
			if (vg::dist (q.s, start.s) < 4) return;
			mode = M_MOVE;
			// fallthrough
		case M_MOVE:
		{
			if (!open) { E.doc.begin (TR ("Move")); open = true; snap_begin (true); }
			Vec2 d = q.p - start.p;
			if (q.mods & MOD_CTRL) { if (std::fabs (d.x) > std::fabs (d.y)) d.y = 0; else d.x = 0; }
			{
				Box mv (box0.x0 + d.x, box0.y0 + d.y, box0.x1 + d.x, box0.y1 + d.y);
				d += snap_box (mv, q.mods);
			}
			set_all (Affine::translate (d.x, d.y));
			snprintf (badge, sizeof badge, "%+.1f, %+.1f", d.x, d.y);
			break;
		}
		case M_SCALE:
		{
			if (!open) { E.doc.begin (TR ("Scale")); open = true; }
			Box b = box0;
			double ax = handle == H_NE || handle == H_E || handle == H_SE ? b.x0 : b.x1;
			double ay = handle == H_SW || handle == H_S || handle == H_SE ? b.y0 : b.y1;
			bool xs = handle != H_N && handle != H_S, ys = handle != H_E && handle != H_W;
			double w0 = b.x1 - ax + b.x0 - ax, h0 = b.y1 - ay + b.y0 - ay;
			Vec2 c { ax, ay };
			if (q.mods & MOD_ALT) { c = b.center (); w0 /= 2; h0 /= 2; }
			double sx = xs && w0 != 0 ? (q.p.x - c.x) / (w0) : 1, sy = ys && h0 != 0 ? (q.p.y - c.y) / (h0) : 1;
			if ((q.mods & MOD_CTRL) && xs && ys) { double k = std::max (std::fabs (sx), std::fabs (sy)); sx = sx < 0 ? -k : k; sy = sy < 0 ? -k : k; }
			if (std::fabs (sx) < 1e-4) sx = 1e-4;
			if (std::fabs (sy) < 1e-4) sy = 1e-4;
			set_all (Affine::scale_at (sx, sy, c));
			snprintf (badge, sizeof badge, "%.1f x %.1f", std::fabs (b.w () * sx), std::fabs (b.h () * sy));
			break;
		}
		case M_ROTATE:
		{
			if (!open) { E.doc.begin (TR ("Rotate")); open = true; }
			Vec2 c = box0.center ();
			double a0 = std::atan2 (start.p.y - c.y, start.p.x - c.x), a1 = std::atan2 (q.p.y - c.y, q.p.x - c.x), a = a1 - a0;
			if (q.mods & MOD_CTRL) a = std::round (a / (M_PI / 12)) * (M_PI / 12);
			set_all (Affine::rotate_at (a, c));
			snprintf (badge, sizeof badge, "%.1f\xC2\xB0", a * 180 / M_PI);
			break;
		}
		case M_RUBBER:
			E.dirty = true;
			break;
		default:
			break;
		}
		moved = true;
	}
	void release (const Pointer &q) override
	{
		if (mode == M_RUBBER && moved)
		{
			Vec2 a = start.p, b = q.p;
			Box r (std::min (a.x, b.x), std::min (a.y, b.y), std::max (a.x, b.x), std::max (a.y, b.y));
			bool touch = b.x < a.x;
			std::function<void (XNode *)> walk;
			std::vector<XNode *> hits;
			for (XNode *k : current_layer ()->kids)
			{
				if (!k->element ()) continue;
				Box o = object_box (k);
				if (o.empty ()) continue;
				if (touch ? o.overlaps (r) : o.inside (r)) hits.push_back (k);
			}
			for (XNode *n : hits) if (!E.selected (n)) E.sel.push_back (n);
			E.dirty = E.chromeDirty = E.panelDirty = true;
		}
		if (open) { update_connectors (); E.doc.commit (); open = false; E.sync (); }
		snap_end ();
		mode = M_NONE; badge[0] = 0;
		E.dirty = true;
	}
	bool key (int hid, int, int mods) override
	{
		if (hid == KEY_ESC && open) { E.doc.cancel (); open = false; mode = M_NONE; E.sync (); return true; }
		if (hid == KEY_LEFT || hid == KEY_RIGHT || hid == KEY_UP || hid == KEY_DOWN)
		{
			if (E.sel.empty ()) return false;
			double k = (mods & MOD_ALT) ? 1 / E.view.zoom : (mods & MOD_SHIFT) ? 10 : 1;
			double dx = hid == KEY_LEFT ? -k : hid == KEY_RIGHT ? k : 0, dy = hid == KEY_UP ? -k : hid == KEY_DOWN ? k : 0;
			Vec2 c = selection_box ().center ();
			Affine m = Affine::translate (dx, dy);
			if (mods & MOD_CTRL)
			{
				if (dx != 0) m = Affine::rotate_at ((dx > 0 ? 1 : -1) * M_PI / ((mods & MOD_SHIFT) ? 180 : 12), c);
				else { double s = dy < 0 ? 1.1 : 1 / 1.1; m = Affine::scale_at (s, s, c); }
			}
			E.doc.begin (TR ("Move"), 7);
			transform_selection (m);
			E.doc.commit ();
			E.sync ();
			return true;
		}
		return false;
	}
	void overlay (SkCanvas *c) override
	{
		if (mode == M_RUBBER && moved)
		{
			SkRect r = SkRect::MakeLTRB ((float) std::min (start.s.x, cur.x), (float) std::min (start.s.y, cur.y), (float) std::max (start.s.x, cur.x), (float) std::max (start.s.y, cur.y));
			SkPaint f, l;
			f.setColor (sk (jet (J_ACCENT), 0.12f));
			l.setColor (sk (jet (J_ACCENT)));
			l.setStyle (SkPaint::kStroke_Style);
			c->drawRect (r, f);
			c->drawRect (r, l);
		}
		if (badge[0]) paint_badge (c, cur, badge);
	}
};

// ---- Nodes ------------------------------------------------------------------------------------------------------------
class NodeTool : public Tool
{
	struct Ref { int s, n; bool operator< (const Ref &o) const { return s != o.s ? s < o.s : n < o.n; } };
	XNode *target = nullptr;
	vg::Path path;
	Affine toPage, toLocal;
	std::set<Ref> chosen;
	enum { M_NONE, M_NODE, M_HANDLE, M_RUBBER } mode = M_NONE;
	Ref dragged { 0, 0 };
	int handleSide = 0;
	Pointer start;
	Vec2 cur;
	bool open = false, moved = false;
	vg::Path before;

	void load ()
	{
		target = nullptr;
		chosen.clear ();
		if (E.sel.size () != 1) return;
		XNode *n = E.sel[0];
		const char *name = el_name (n);
		if (!strcmp (name, "g") || !strcmp (name, "text") || !strcmp (name, "image") || !strcmp (name, "use")) return;
		target = n;
		path = shape_to_path (n);
		toPage = node_to_page (n);
		toLocal = toPage.inverse ();
	}
	void store (const char *label, int merge = 0)
	{
		if (!target) return;
		E.doc.begin (label, merge);
		commit_path ();
		E.doc.commit ();
		E.sync ();
		toPage = node_to_page (target);
		toLocal = toPage.inverse ();
	}
	void commit_path ()
	{
		if (strcmp (el_name (target), "path"))
		{
			XNode *q = convert_to_path (target);
			target = q;
		}
		E.doc.set (target, "d", vg::write_d (path));
		E.doc.set (target, "sodipodi:nodetypes", vg::write_nodetypes (path));
	}
	Vec2 scr (Vec2 local) { return E.to_screen (toPage.apply (local)); }
	bool find_node (Vec2 s, Ref &r)
	{
		double best = 9;
		bool any = false;
		for (int i = 0; i < (int) path.sub.size (); i++)
			for (int j = 0; j < (int) path.sub[i].n.size (); j++)
			{
				double d = vg::dist (s, scr (path.sub[i].n[j].p));
				if (d < best) { best = d; r = { i, j }; any = true; }
			}
		return any;
	}
	bool find_handle (Vec2 s, Ref &r, int &side)
	{
		double best = 8;
		bool any = false;
		for (auto &c : chosen)
		{
			for (int i = -1; i <= 1; i++)
			{
				auto &sp = path.sub[c.s];
				int cnt = (int) sp.n.size();
				int j = c.n + i;
				if (j < 0 || j >= cnt) { if (!sp.closed) continue; j = (j + cnt) % cnt; }
				const vg::Node &n = sp.n[j];
				if (n.in != n.p) { double d = vg::dist (s, scr (n.in)); if (d < best) { best = d; r = { c.s, j }; side = 0; any = true; } }
				if (n.out != n.p) { double d = vg::dist (s, scr (n.out)); if (d < best) { best = d; r = { c.s, j }; side = 1; any = true; } }
			}
		}
		return any;
	}
	bool find_segment (Vec2 s, int &si, int &seg, double &tOut)
	{
		double best = 7;
		bool any = false;
		for (int i = 0; i < (int) path.sub.size (); i++)
		{
			auto &sp = path.sub[i];
			for (int k = 0; k < sp.segments (); k++)
			{
				vg::Bezier b = vg::seg_of (sp, k);
				for (int q = 0; q <= 32; q++)
				{
					double t = q / 32.0, d = vg::dist (s, scr (b.at (t)));
					if (d < best) { best = d; si = i; seg = k; tOut = t; any = true; }
				}
			}
		}
		return any;
	}
	void insert_at (int si, int seg, double t)
	{
		auto &sp = path.sub[si];
		int cnt = (int) sp.n.size (), next = (seg + 1) % cnt;
		vg::Bezier b = vg::seg_of (sp, seg), l, r;
		b.split (t, l, r);
		vg::Node q;
		q.p = l.p3; q.in = b.line () ? q.p : l.p2; q.out = b.line () ? q.p : r.p1;
		q.type = vg::N_SMOOTH;
		if (!b.line ()) { sp.n[seg].out = l.p1; sp.n[next].in = r.p2; }
		sp.n.insert (sp.n.begin () + seg + 1, q);
		chosen.clear ();
		chosen.insert ({ si, seg + 1 });
	}
	void delete_chosen ()
	{
		std::vector<Ref> v (chosen.begin (), chosen.end ());
		for (int i = (int) v.size () - 1; i >= 0; i--)
		{
			auto &sp = path.sub[v[i].s];
			sp.n.erase (sp.n.begin () + v[i].n);
		}
		for (int i = (int) path.sub.size () - 1; i >= 0; i--)
			if (path.sub[i].n.size () < 2) path.sub.erase (path.sub.begin () + i);
		chosen.clear ();
		if (path.sub.empty ())
		{
			E.doc.begin (TR ("Delete"));
			E.doc.remove (target);
			E.doc.commit ();
			target = nullptr;
			E.sync ();
			return;
		}
		store (TR ("Delete nodes"));
	}
	void set_type (vg::NodeType t)
	{
		for (auto &c : chosen)
		{
			auto &sp = path.sub[c.s];
			vg::Node &n = sp.n[c.n];
			n.type = t;
			int cnt = (int) sp.n.size ();
			Vec2 prev = sp.n[(c.n + cnt - 1) % cnt].p, next = sp.n[(c.n + 1) % cnt].p;
			if (t == vg::N_CUSP) continue;
			Vec2 dir = vg::unit (next - prev);
			double li = vg::dist (n.p, prev) / 3, lo = vg::dist (n.p, next) / 3;
			if (t == vg::N_SYMMETRIC) li = lo = (li + lo) / 2;
			if (n.in != n.p && t == vg::N_SMOOTH) { dir = vg::unit (n.p - n.in); }
			n.in = n.p - dir * li;
			n.out = n.p + dir * lo;
		}
		store (TR ("Node type"));
	}
public:
	const char *hint () override { return TR ("Click a node. Drag to move. Double-click a segment to add a node."); }
	int cursor () override { return UC_ARROW; }
	void enter () override { load (); }
	void leave () override { if (open) { E.doc.commit (); open = false; } target = nullptr; chosen.clear (); }
	void press (const Pointer &q) override
	{
		start = q; cur = q.s; moved = false;
		if (!target || (!E.sel.empty () && E.sel[0] != target)) load ();
		Ref r;
		int side;
		if (target && find_handle (q.s, r, side)) { mode = M_HANDLE; dragged = r; handleSide = side; before = path; return; }
		if (target && find_node (q.s, r))
		{
			if (q.clicks >= 2)
			{
				auto &n = path.sub[r.s].n[r.n];
				chosen.clear (); chosen.insert (r);
				set_type (n.type == vg::N_CUSP ? vg::N_SMOOTH : vg::N_CUSP);
				return;
			}
			if (q.mods & MOD_SHIFT) { if (chosen.count (r)) chosen.erase (r); else chosen.insert (r); }
			else if (!chosen.count (r)) { chosen.clear (); chosen.insert (r); }
			mode = M_NODE; before = path; E.dirty = true;
			return;
		}
		int si, seg; double t;
		if (target && find_segment (q.s, si, seg, t))
		{
			if (q.clicks >= 2) { insert_at (si, seg, t); store (TR ("Add node")); return; }
			chosen.clear ();
			chosen.insert ({ si, seg }); chosen.insert ({ si, (seg + 1) % (int) path.sub[si].n.size () });
			E.dirty = true;
			return;
		}
		XNode *o = hit_object (q.p, 4 / E.view.zoom, nullptr, true);
		if (o) { E.select (o); load (); return; }
		chosen.clear ();
		mode = M_RUBBER;
		E.dirty = true;
	}
	void move (const Pointer &q) override
	{
		cur = q.s;
		moved = true;
		Vec2 a = toLocal.apply (start.p), b = toLocal.apply (q.p), d = b - a;
		if (mode == M_NODE)
		{
			if (!open) { E.doc.begin (TR ("Move nodes")); open = true; }
			path = before;
			if (q.mods & MOD_CTRL) { if (std::fabs (d.x) > std::fabs (d.y)) d.y = 0; else d.x = 0; }
			for (auto &c : chosen) { vg::Node &n = path.sub[c.s].n[c.n]; n.p += d; n.in += d; n.out += d; }
			commit_path ();
			E.sync ();
		}
		else if (mode == M_HANDLE)
		{
			if (!open) { E.doc.begin (TR ("Move handle")); open = true; }
			path = before;
			vg::Node &n = path.sub[dragged.s].n[dragged.n];
			Vec2 &h = handleSide ? n.out : n.in, &o = handleSide ? n.in : n.out;
			Vec2 target2 = b;
			if (q.mods & MOD_CTRL)
			{
				double ang = std::atan2 (b.y - n.p.y, b.x - n.p.x), l = vg::dist (b, n.p);
				ang = std::round (ang / (M_PI / 12)) * (M_PI / 12);
				target2 = n.p + Vec2 (std::cos (ang), std::sin (ang)) * l;
			}
			h = target2;
			if (!(q.mods & MOD_SHIFT) && n.type != vg::N_CUSP)
			{
				Vec2 dir = vg::unit (n.p - h);
				double l = n.type == vg::N_SYMMETRIC ? vg::dist (h, n.p) : vg::dist (o, n.p);
				o = n.p + dir * l;
			}
			commit_path ();
			E.sync ();
		}
		else if (mode == M_RUBBER) E.dirty = true;
	}
	void release (const Pointer &q) override
	{
		if (mode == M_RUBBER && moved && target)
		{
			Box r (std::min (start.s.x, q.s.x), std::min (start.s.y, q.s.y), std::max (start.s.x, q.s.x), std::max (start.s.y, q.s.y));
			for (int i = 0; i < (int) path.sub.size (); i++)
				for (int j = 0; j < (int) path.sub[i].n.size (); j++)
					if (r.contains (scr (path.sub[i].n[j].p))) chosen.insert ({ i, j });
		}
		if (open) { E.doc.commit (); open = false; E.sync (); }
		mode = M_NONE;
		E.dirty = true;
	}
	bool key (int hid, int ch, int mods) override
	{
		if (!target) return false;
		if ((hid == KEY_BACKSPACE || hid == KEY_DELETE) && !chosen.empty ()) { delete_chosen (); return true; }
		if (hid == KEY_LEFT || hid == KEY_RIGHT || hid == KEY_UP || hid == KEY_DOWN)
		{
			if (chosen.empty ()) return false;
			double k = (mods & MOD_ALT) ? 1 / E.view.zoom : (mods & MOD_SHIFT) ? 10 : 1;
			Vec2 d = toLocal.linear ({ hid == KEY_LEFT ? -k : hid == KEY_RIGHT ? k : 0, hid == KEY_UP ? -k : hid == KEY_DOWN ? k : 0 });
			for (auto &c : chosen) { vg::Node &n = path.sub[c.s].n[c.n]; n.p += d; n.in += d; n.out += d; }
			store (TR ("Move nodes"), 8);
			return true;
		}
		(void) ch;
		return false;
	}
	void overlay (SkCanvas *c) override
	{
		if (!target || E.sel.empty () || E.sel[0] != target) { if (E.sel.size () == 1 && E.sel[0] != target) load (); if (!target) return; }
		// the outline
		SkPaint halo, line;
		halo.setAntiAlias (true); line.setAntiAlias (true);
		halo.setStyle (SkPaint::kStroke_Style); line.setStyle (SkPaint::kStroke_Style);
		halo.setStrokeWidth (3); halo.setColor (sk (jet (J_SURFACE), 0.7f));
		line.setStrokeWidth (1); line.setColor (sk (jet (J_ACCENT_VIF)));
		vg::Path sp = path;
		Affine m = Affine (E.view.zoom, 0, 0, E.view.zoom, E.view.ox, E.view.oy) * toPage;
		sp.transform (m);
		SkPath o = to_skpath (sp, false);
		c->drawPath (o, halo);
		c->drawPath (o, line);
		// the handles of what is chosen and its neighbours
		SkPaint th;
		th.setColor (sk (jet (J_ACCENT)));
		th.setAntiAlias (true);
		std::set<Ref> shown;
		for (auto &cc : chosen)
		{
			auto &s = path.sub[cc.s];
			int cnt = (int) s.n.size ();
			for (int i = -1; i <= 1; i++)
			{
				int j = cc.n + i;
				if (j < 0 || j >= cnt) { if (!s.closed) continue; j = (j + cnt) % cnt; }
				shown.insert ({ cc.s, j });
			}
		}
		for (auto &r : shown)
		{
			const vg::Node &n = path.sub[r.s].n[r.n];
			Vec2 a = scr (n.p);
			for (Vec2 h : { n.in, n.out })
				if (h != n.p)
				{
					Vec2 b = scr (h);
					c->drawLine ((float) a.x, (float) a.y, (float) b.x, (float) b.y, th);
					c->drawCircle ((float) b.x, (float) b.y, 3.5f, th);
				}
		}
		for (int i = 0; i < (int) path.sub.size (); i++)
			for (int j = 0; j < (int) path.sub[i].n.size (); j++)
			{
				const vg::Node &n = path.sub[i].n[j];
				paint_node (c, scr (n.p), n.type == vg::N_CUSP ? 0 : n.type == vg::N_SYMMETRIC ? 1 : 2, chosen.count ({ i, j }) != 0);
			}
		if (mode == M_RUBBER && moved)
		{
			SkPaint l;
			l.setColor (sk (jet (J_ACCENT)));
			l.setStyle (SkPaint::kStroke_Style);
			c->drawRect (SkRect::MakeLTRB ((float) std::min (start.s.x, cur.x), (float) std::min (start.s.y, cur.y), (float) std::max (start.s.x, cur.x), (float) std::max (start.s.y, cur.y)), l);
		}
	}
	int bar_count () override { return 8; }
	const char *bar_label (int i) override
	{
		static const char *const L[] = { "Insert", "Delete", "Join", "Break", "Corner", "Smooth", "Symmetric", "Auto" };
		return TR (L[i]);
	}
	void bar_click (int i) override
	{
		if (!target) return;
		switch (i)
		{
		case 0: insert_between (); break;
		case 1: if (!chosen.empty ()) delete_chosen (); break;
		case 2: join_ends (); break;
		case 3: break_at (); break;
		default: set_type ((vg::NodeType) (i == 4 ? vg::N_CUSP : i == 5 ? vg::N_SMOOTH : i == 6 ? vg::N_SYMMETRIC : vg::N_AUTO));
		}
	}
private:
	// a node in the middle of each segment whose two ends are chosen
	void insert_between ()
	{
		std::vector<Ref> v (chosen.begin (), chosen.end ());
		bool any = false;
		for (int i = (int) v.size () - 1; i >= 0; i--)
		{
			auto &sp = path.sub[v[i].s];
			int cnt = (int) sp.n.size (), nx = (v[i].n + 1) % cnt;
			if (v[i].n >= sp.segments () || !chosen.count ({ v[i].s, nx })) continue;
			insert_at (v[i].s, v[i].n, 0.5);
			any = true;
		}
		if (any) store (TR ("Add node"));
	}
	// two ends of open subpaths become one node
	void join_ends ()
	{
		if (chosen.size () != 2) { E.say (TR ("Choose two ends.")); return; }
		Ref a = *chosen.begin (), b = *chosen.rbegin ();
		auto end_of = [&] (Ref r) { auto &s = path.sub[r.s]; return !s.closed && (r.n == 0 || r.n == (int) s.n.size () - 1); };
		if (!end_of (a) || !end_of (b)) { E.say (TR ("Choose two ends.")); return; }
		if (a.s == b.s)
		{
			auto &s = path.sub[a.s];
			s.n[0].in = s.n.back ().in;
			s.n[0].p = (s.n[0].p + s.n.back ().p) / 2;
			s.n.pop_back ();
			s.closed = true;
		}
		else
		{
			vg::SubPath x = path.sub[a.s], y = path.sub[b.s];
			auto rev = [] (vg::SubPath &q) { std::reverse (q.n.begin (), q.n.end ()); for (auto &m : q.n) std::swap (m.in, m.out); };
			if (a.n == 0) rev (x);
			if (b.n != 0) rev (y);
			vg::Node &l = x.n.back (); const vg::Node &f = y.n[0];
			l.out = f.out; l.p = (l.p + f.p) / 2;
			x.n.insert (x.n.end (), y.n.begin () + 1, y.n.end ());
			path.sub[a.s] = x;
			path.sub.erase (path.sub.begin () + b.s);
		}
		chosen.clear ();
		store (TR ("Join nodes"));
	}
	// the path opens at the chosen node
	void break_at ()
	{
		if (chosen.size () != 1) { E.say (TR ("Choose one node.")); return; }
		Ref r = *chosen.begin ();
		vg::SubPath s = path.sub[r.s];
		int cnt = (int) s.n.size ();
		if (s.closed)
		{
			vg::SubPath o;
			for (int i = 0; i <= cnt; i++) o.n.push_back (s.n[(r.n + i) % cnt]);
			path.sub[r.s] = o;
		}
		else
		{
			if (r.n == 0 || r.n == cnt - 1) return;
			vg::SubPath a, b;
			a.n.assign (s.n.begin (), s.n.begin () + r.n + 1);
			b.n.assign (s.n.begin () + r.n, s.n.end ());
			path.sub[r.s] = a;
			path.sub.insert (path.sub.begin () + r.s + 1, b);
		}
		chosen.clear ();
		store (TR ("Break path"));
	}
};

// ---- Shapes -----------------------------------------------------------------------------------------------------------
class ShapeTool : public Tool
{
protected:
	XNode *made = nullptr;
	Pointer start;
	Vec2 cur;
	char badge[40] = "";
	virtual XNode *create (Vec2 a, Vec2 b, const Pointer &q) = 0;
	virtual void update (XNode *n, Vec2 a, Vec2 b, const Pointer &q) = 0;
	virtual const char *label () = 0;
public:
	void press (const Pointer &q) override
	{
		start = q; made = nullptr;
		snap_begin (false);
		start.p = snap_point (q.p, q.mods);
	}
	void move (const Pointer &q) override
	{
		if (!made && vg::dist (q.s, start.s) < 3) return;
		cur = q.s;
		Affine inv = layer_to_page ().inverse ();
		Vec2 a = inv.apply (start.p), b = inv.apply (snap_point (q.p, q.mods));
		if (q.mods & MOD_ALT) a = a - (b - a);
		if (!made) { E.doc.begin (label ()); made = create (a, b, q); }
		update (made, a, b, q);
		E.sync ();
		snprintf (badge, sizeof badge, "%.1f x %.1f", std::fabs (b.x - a.x), std::fabs (b.y - a.y));
	}
	void release (const Pointer &) override
	{
		badge[0] = 0;
		snap_end ();
		if (!made) return;
		E.doc.commit ();
		E.sync ();
		E.select (made);
		made = nullptr;
	}
	bool key (int hid, int, int) override
	{
		if (hid == KEY_ESC && made) { E.doc.cancel (); made = nullptr; badge[0] = 0; E.sync (); return true; }
		return false;
	}
	void overlay (SkCanvas *c) override { if (badge[0]) paint_badge (c, cur, badge); }
};

class RectTool : public ShapeTool
{
	const char *label () override { return TR ("Rectangle"); }
	XNode *create (Vec2, Vec2, const Pointer &) override { return add_shape ("rect", { { "style", new_style () } }); }
	void update (XNode *n, Vec2 a, Vec2 b, const Pointer &q) override
	{
		double w = std::fabs (b.x - a.x), h = std::fabs (b.y - a.y);
		if (q.mods & MOD_CTRL) w = h = std::max (w, h);
		double x = b.x < a.x ? a.x - w : a.x, y = b.y < a.y ? a.y - h : a.y;
		E.doc.set (n, "x", fmt (x)); E.doc.set (n, "y", fmt (y)); E.doc.set (n, "width", fmt (w)); E.doc.set (n, "height", fmt (h));
	}
public:
	const char *hint () override { return TR ("Drag to create. Ctrl: square. Alt: from the centre."); }
};
class EllipseTool : public ShapeTool
{
	const char *label () override { return TR ("Ellipse"); }
	XNode *create (Vec2, Vec2, const Pointer &) override { return add_shape ("ellipse", { { "style", new_style () } }); }
	void update (XNode *n, Vec2 a, Vec2 b, const Pointer &q) override
	{
		double rx = std::fabs (b.x - a.x) / 2, ry = std::fabs (b.y - a.y) / 2;
		if (q.mods & MOD_CTRL) rx = ry = std::max (rx, ry);
		double cx = a.x + (b.x < a.x ? -rx : rx), cy = a.y + (b.y < a.y ? -ry : ry);
		E.doc.set (n, "cx", fmt (cx)); E.doc.set (n, "cy", fmt (cy)); E.doc.set (n, "rx", fmt (rx)); E.doc.set (n, "ry", fmt (ry));
	}
public:
	const char *hint () override { return TR ("Drag to create. Ctrl: circle. Alt: from the centre."); }
};
class StarTool : public ShapeTool
{
public:
	int sides = 5; double ratio = 0.5; bool polygon = false;
private:
	const char *label () override { return TR ("Star"); }
	XNode *create (Vec2, Vec2, const Pointer &) override { return add_shape ("path", { { "style", new_style () }, { "sodipodi:type", "star" } }); }
	void update (XNode *n, Vec2 a, Vec2 b, const Pointer &q) override
	{
		double r = vg::dist (a, b), ang = std::atan2 (b.y - a.y, b.x - a.x);
		if (q.mods & MOD_CTRL) ang = std::round (ang / (M_PI / 12)) * (M_PI / 12);
		double r2 = polygon ? r * std::cos (M_PI / sides) : r * ratio;
		double step = M_PI / sides;
		vg::Path p;
		for (int i = 0; i < sides * 2; i++)
		{
			if (polygon && (i & 1)) continue;
			double rr = i & 1 ? r2 : r, t = ang + i * step;
			Vec2 v = a + Vec2 (std::cos (t), std::sin (t)) * rr;
			if (p.empty ()) p.move_to (v); else p.line_to (v);
		}
		p.close ();
		E.doc.set (n, "d", vg::write_d (p));
		E.doc.set (n, "sodipodi:sides", fmt (sides));
		E.doc.set (n, "sodipodi:cx", fmt (a.x)); E.doc.set (n, "sodipodi:cy", fmt (a.y));
		E.doc.set (n, "sodipodi:r1", fmt (r)); E.doc.set (n, "sodipodi:r2", fmt (r2));
		E.doc.set (n, "sodipodi:arg1", fmt (ang)); E.doc.set (n, "sodipodi:arg2", fmt (ang + step));
		E.doc.set (n, "inkscape:flatsided", polygon ? "true" : "false");
	}
public:
	const char *hint () override { return TR ("Drag to create. Ctrl: angle by 15 degrees."); }
	int bar_count () override { return 3; }
	const char *bar_label (int i) override
	{
		static char b[32];
		if (i == 0) return polygon ? TR ("Polygon") : TR ("Star");
		snprintf (b, sizeof b, "%s %d", i == 1 ? "\xE2\x88\x92" : "+", sides);
		return b;
	}
	void bar_click (int i) override
	{
		if (i == 0) polygon = !polygon;
		else if (i == 1 && sides > 3) sides--;
		else if (i == 2 && sides < 100) sides++;
		E.chromeDirty = true;
	}
};
class SpiralTool : public ShapeTool
{
public:
	double turns = 3;
private:
	const char *label () override { return TR ("Spiral"); }
	XNode *create (Vec2, Vec2, const Pointer &) override { return add_shape ("path", { { "style", "fill:none;stroke:" + (E.stroke == "none" ? E.fill : E.stroke) + ";stroke-width:" + fmt (E.strokeWidth) }, { "sodipodi:type", "spiral" } }); }
	void update (XNode *n, Vec2 a, Vec2 b, const Pointer &) override
	{
		double r = vg::dist (a, b), a0 = std::atan2 (b.y - a.y, b.x - a.x);
		int segs = (int) (turns * 8);
		// a cubic per eighth of a turn, tangents from the derivative of r(t) = R t
		double dt = 1.0 / segs, da = 2 * M_PI * turns * dt;
		vg::Path p;
		auto P = [&] (double t) { double an = a0 + 2 * M_PI * turns * (t - 1); return a + Vec2 (std::cos (an), std::sin (an)) * (r * t); };
		auto T = [&] (double t) { double an = a0 + 2 * M_PI * turns * (t - 1), w = 2 * M_PI * turns; Vec2 u (std::cos (an), std::sin (an)), v (-u.y, u.x); return (u + v * (w * t)) * r; };
		p.move_to (P (1e-3));
		for (int i = 0; i < segs; i++)
		{
			double t0 = std::max (i * dt, 1e-3), t1 = (i + 1) * dt;
			(void) da;
			double h = (t1 - t0) / 3;
			p.curve_to (P (t0) + T (t0) * h, P (t1) - T (t1) * h, P (t1));
		}
		E.doc.set (n, "d", vg::write_d (p));
		E.doc.set (n, "sodipodi:cx", fmt (a.x)); E.doc.set (n, "sodipodi:cy", fmt (a.y));
		E.doc.set (n, "sodipodi:expansion", "1"); E.doc.set (n, "sodipodi:revolution", fmt (turns));
		E.doc.set (n, "sodipodi:radius", fmt (r)); E.doc.set (n, "sodipodi:argument", fmt (a0)); E.doc.set (n, "sodipodi:t0", "0");
	}
public:
	const char *hint () override { return TR ("Drag to create. The drag sets the size and the end."); }
	int bar_count () override { return 2; }
	const char *bar_label (int i) override
	{
		static char b[32];
		snprintf (b, sizeof b, "%s %.0f", i == 0 ? "\xE2\x88\x92" : "+", turns);
		return b;
	}
	void bar_click (int i) override
	{
		if (i == 0 && turns > 1) turns--;
		else if (i == 1 && turns < 20) turns++;
		E.chromeDirty = true;
	}
};

// ---- Pen --------------------------------------------------------------------------------------------------------------
class PenTool : public Tool
{
	vg::SubPath sp;
	bool drawing = false, dragging = false;
	Vec2 cur;
	Pointer start;

	Vec2 scr (Vec2 p) { return E.to_screen (p); }
	void finish (bool closed)
	{
		if (sp.n.size () >= 2)
		{
			Affine inv = layer_to_page ().inverse ();
			vg::Path p;
			sp.closed = closed;
			p.sub.push_back (sp);
			p.transform (inv);
			E.doc.begin (TR ("Pen"));
			XNode *n = add_shape ("path", { { "style", new_style () }, { "d", vg::write_d (p) }, { "sodipodi:nodetypes", vg::write_nodetypes (p) } });
			E.doc.commit ();
			E.sync ();
			E.select (n);
		}
		sp = vg::SubPath ();
		drawing = dragging = false;
		E.dirty = true;
	}
public:
	const char *hint () override { return drawing ? TR ("Click: corner. Drag: curve. Enter: finish. Backspace: last point.") : TR ("Click to start a path."); }
	void leave () override { if (drawing) finish (false); }
	void press (const Pointer &q) override
	{
		start = q; cur = q.s;
		if (drawing && sp.n.size () >= 2 && vg::dist (q.s, scr (sp.n[0].p)) < 10) { finish (true); return; }
		if (drawing && q.clicks >= 2) { finish (false); return; }
		snap_begin (false);
		Vec2 p = snap_point (q.p, q.mods);
		snap_end ();
		if ((q.mods & MOD_CTRL) && !sp.n.empty ())
		{
			Vec2 o = sp.n.back ().p, d = p - o;
			double a = std::round (std::atan2 (d.y, d.x) / (M_PI / 12)) * (M_PI / 12), l = vg::length (d);
			p = o + Vec2 (std::cos (a), std::sin (a)) * l;
		}
		sp.n.emplace_back (p);
		drawing = true; dragging = true;
		E.dirty = true;
	}
	void hover (const Pointer &q) override { cur = q.s; if (drawing) E.dirty = true; }
	void move (const Pointer &q) override
	{
		cur = q.s;
		if (dragging && !sp.n.empty () && vg::dist (q.s, start.s) > 3)
		{
			vg::Node &n = sp.n.back ();
			n.out = q.p;
			n.in = n.p * 2 - q.p;
			n.type = vg::N_SYMMETRIC;
		}
		E.dirty = true;
	}
	void release (const Pointer &) override { dragging = false; }
	bool key (int hid, int, int) override
	{
		if (!drawing) return false;
		if (hid == KEY_ENTER) { finish (false); return true; }
		if (hid == KEY_ESC) { sp = vg::SubPath (); drawing = false; E.dirty = true; return true; }
		if (hid == KEY_BACKSPACE) { sp.n.pop_back (); if (sp.n.empty ()) drawing = false; E.dirty = true; return true; }
		return false;
	}
	void overlay (SkCanvas *c) override
	{
		if (!drawing || sp.n.empty ()) return;
		vg::Path p;
		p.sub.push_back (sp);
		Affine m (E.view.zoom, 0, 0, E.view.zoom, E.view.ox, E.view.oy);
		p.transform (m);
		SkPaint l;
		l.setAntiAlias (true);
		l.setStyle (SkPaint::kStroke_Style);
		l.setStrokeWidth (1.5f);
		l.setColor (sk (jet (J_ACCENT_VIF)));
		c->drawPath (to_skpath (p, false), l);
		Vec2 last = scr (sp.n.back ().p);
		if (!dragging) dashed_line (c, last, cur, jet (J_ACCENT));
		for (auto &n : sp.n) paint_node (c, scr (n.p), n.type == vg::N_CUSP ? 0 : 2, false);
		if (sp.n.size () >= 2 && vg::dist (cur, scr (sp.n[0].p)) < 10)
		{
			SkPaint ring;
			ring.setAntiAlias (true);
			ring.setStyle (SkPaint::kStroke_Style);
			ring.setStrokeWidth (2);
			ring.setColor (sk (jet (J_SUCCES)));
			Vec2 f = scr (sp.n[0].p);
			c->drawCircle ((float) f.x, (float) f.y, 8.5f, ring);
		}
	}
};

// ---- Pencil -----------------------------------------------------------------------------------------------------------
class PencilTool : public Tool
{
	std::vector<Vec2> pts;
	bool on = false;
public:
	double smooth = 1.5;
	const char *hint () override { return TR ("Drag to draw freehand."); }
	void press (const Pointer &q) override { pts.clear (); pts.push_back (q.p); on = true; }
	void move (const Pointer &q) override
	{
		if (!on) return;
		if (vg::dist (q.p * E.view.zoom, pts.back () * E.view.zoom) >= 1) pts.push_back (q.p);
		E.dirty = true;
	}
	void release (const Pointer &) override
	{
		on = false;
		if (pts.size () < 2) { pts.clear (); return; }
		vg::Path p = fit_curve (pts, smooth / E.view.zoom);
		p.transform (layer_to_page ().inverse ());
		E.doc.begin (TR ("Pencil"));
		std::string st = "fill:none;stroke:" + (E.stroke == "none" ? E.fill : E.stroke) + ";stroke-width:" + fmt (E.strokeWidth) + ";stroke-linecap:round";
		XNode *n = add_shape ("path", { { "style", st }, { "d", vg::write_d (p) }, { "sodipodi:nodetypes", vg::write_nodetypes (p) } });
		E.doc.commit ();
		E.sync ();
		E.select (n);
		pts.clear ();
	}
	void overlay (SkCanvas *c) override
	{
		if (pts.size () < 2) return;
		SkPaint l;
		l.setAntiAlias (true);
		l.setColor (sk (jet (J_ACCENT_VIF)));
		l.setStrokeWidth (1.5f);
		for (size_t i = 1; i < pts.size (); i++)
		{
			Vec2 a = E.to_screen (pts[i - 1]), b = E.to_screen (pts[i]);
			c->drawLine ((float) a.x, (float) a.y, (float) b.x, (float) b.y, l);
		}
	}
};

// ---- Dropper, Hand, Zoom ----------------------------------------------------------------------------------------------
class DropperTool : public Tool
{
public:
	const char *hint () override { return TR ("Click an object: its fill. Shift: as stroke."); }
	void press (const Pointer &q) override
	{
		XNode *o = hit_object (q.p, 3 / E.view.zoom, nullptr, true);
		if (!o) return;
		std::string c = get_prop (o, "fill");
		if (c.empty ()) c = "#000000";
		if (E.sel.empty ()) { if (q.mods & MOD_SHIFT) E.stroke = c; else E.fill = c; E.chromeDirty = true; return; }
		style_selection (q.mods & MOD_SHIFT ? "stroke" : "fill", c.c_str (), TR ("Pick colour"));
	}
};
class HandTool : public Tool
{
	Vec2 last;
public:
	const char *hint () override { return TR ("Drag to move the view. Double-click: the whole page."); }
	int cursor () override { return UC_HAND; }
	void press (const Pointer &q) override { last = q.s; if (q.clicks >= 2) fit_page (); }
	void move (const Pointer &q) override { E.view.ox += std::round (q.s.x - last.x); E.view.oy += std::round (q.s.y - last.y); last = q.s; E.dirty = true; }
};
class ZoomTool : public Tool
{
public:
	const char *hint () override { return TR ("Click: zoom in. Shift: zoom out."); }
	void press (const Pointer &q) override { zoom_at (q.mods & MOD_SHIFT ? 0.8 : 1.25, q.s); }
};

Tool *make_text_tool ();
Tool *make_gradient_tool ();

void make_tools ()
{
	E.tools[T_SELECT] = new SelectTool;
	E.tools[T_NODE] = new NodeTool;
	E.tools[T_RECT] = new RectTool;
	E.tools[T_ELLIPSE] = new EllipseTool;
	E.tools[T_STAR] = new StarTool;
	E.tools[T_SPIRAL] = new SpiralTool;
	E.tools[T_PEN] = new PenTool;
	E.tools[T_PENCIL] = new PencilTool;
	E.tools[T_TEXT] = make_text_tool ();
	E.tools[T_GRADIENT] = make_gradient_tool ();
	E.tools[T_DROPPER] = new DropperTool;
	E.tools[T_HAND] = new HandTool;
	E.tools[T_ZOOM] = new ZoomTool;
	make_tools3 ();
}

}
