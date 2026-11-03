//
// tools2.cpp -- the Text tool (the characters live in the tree, the caret here) and the Gradient tool.
//
#include "editor.h"
#include "../text/fonts.h"
#include "include/core/SkFont.h"
#include <cstdio>
#include <cstring>

using namespace vd;
using namespace vr;

namespace ve {

SkColor sk (unsigned rgb, float a = 1);

static Affine layer_page ()
{
	XNode *l = current_layer ();
	return l == E.doc.root ? E.scene->docToPx : node_to_page (l);
}

// ---- Text ---------------------------------------------------------------------------------------------------------------
class TextTool : public Tool
{
	XNode *target = nullptr;
	std::vector<std::string> lines;
	int line = 0, col = 0;
	bool typing = false;
	double x = 0, y = 0, size = 16;

	void load (XNode *n)
	{
		target = n;
		lines.clear ();
		bool tspans = false;
		for (XNode *k : n->kids)
		{
			if (k->is ("tspan") || k->is ("svg:tspan")) { lines.push_back (k->text_content ()); tspans = true; }
			else if (!tspans && (k->type == X_TEXT || k->type == X_CDATA) && k->text.find_first_not_of (" \n\t\r") != std::string::npos) lines.push_back (k->text);
		}
		if (lines.empty ()) lines.push_back ("");
		line = (int) lines.size () - 1;
		col = (int) lines[line].size ();
		x = parse_length (n->get ("x"), 100, 16);
		y = parse_length (n->get ("y"), 100, 16);
		Style st, base;
		compute_style (n, base, E.builder->sheet, st);
		size = st.fontSize;
		E.dirty = true;
	}
	void write ()
	{
		if (!target) return;
		E.doc.begin (TR ("Type text"), 9);
		while (!target->kids.empty ()) E.doc.remove (target->kids.back ());
		for (size_t i = 0; i < lines.size (); i++)
		{
			XNode *t = x_new (X_TEXT, "");
			t->text = lines[i];
			if (lines.size () == 1) { E.doc.insert (target, -1, t); break; }
			XNode *sp = x_new (X_ELEMENT, "tspan");
			sp->attrs.push_back ({ "sodipodi:role", "line" });
			sp->attrs.push_back ({ "x", fmt (x) });
			sp->attrs.push_back ({ "y", fmt (y + size * 1.25 * i) });
			sp->kids.push_back (t);
			t->parent = sp;
			E.doc.insert (target, -1, sp);
		}
		E.doc.commit ();
		E.sync ();
	}
	void finish ()
	{
		bool empty = true;
		for (auto &l : lines) if (!l.empty ()) empty = false;
		if (target && empty)
		{
			E.doc.begin (TR ("Delete"));
			E.doc.remove (target);
			E.doc.commit ();
			E.sync ();
		}
		else if (target) E.select (target);
		target = nullptr; typing = false;
		E.dirty = true;
	}
	double width (const std::string &s)
	{
		SkFont f (font_match ("DejaVu Sans", 400, 5, false), (float) size);
		if (target)
		{
			Style st, base;
			compute_style (target, base, E.builder->sheet, st);
			std::string fam = st.fontFamily;
			size_t c = fam.find (',');
			if (c != std::string::npos) fam.resize (c);
			while (!fam.empty () && (fam[0] == '\'' || fam[0] == '"')) fam.erase (0, 1);
			while (!fam.empty () && (fam.back () == '\'' || fam.back () == '"')) fam.pop_back ();
			if (auto tf = font_match (fam.c_str (), st.fontWeight, st.fontStretch, st.fontItalic != 0)) f = SkFont (tf, (float) size);
		}
		f.setLinearMetrics (true);
		return f.measureText (s.data (), s.size (), SkTextEncoding::kUTF8);
	}
public:
	const char *hint () override { return typing ? TR ("Type. Enter: new line. Esc: done.") : TR ("Click to write. Click a text to edit it."); }
	int cursor () override { return UC_TEXT; }
	void enter () override
	{
		if (E.sel.size () == 1 && !strcmp (el_name (E.sel[0]), "text")) { load (E.sel[0]); typing = true; }
	}
	void leave () override { if (typing) finish (); }
	void press (const Pointer &q) override
	{
		XNode *o = hit_object (q.p, 3 / E.view.zoom);
		if (o && !strcmp (el_name (o), "text"))
		{
			if (typing && o != target) finish ();
			E.select (o);
			load (o);
			typing = true;
			return;
		}
		if (typing) finish ();
		Vec2 a = layer_page ().inverse ().apply (q.p);
		double px = 24 / layer_page ().mean_scale ();
		E.doc.begin (TR ("Text"));
		char st[160];
		snprintf (st, sizeof st, "font-size:%.4gpx;font-family:'DejaVu Sans';fill:%s;stroke:none", px, E.fill.c_str ());
		XNode *n = add_shape ("text", { { "x", fmt (a.x) }, { "y", fmt (a.y) }, { "style", st }, { "xml:space", "preserve" } });
		E.doc.commit ();
		E.sync ();
		E.select (n);
		load (n);
		typing = true;
	}
	bool key (int hid, int ch, int mods) override
	{
		if (!typing) return false;
		if (mods & MOD_CTRL) { if (hid == KEY_ENTER) { finish (); return true; } return false; }
		std::string &l = lines[line];
		switch (hid)
		{
		case KEY_ESC: finish (); return true;
		case KEY_ENTER:
		{
			std::string tail = l.substr (col);
			l.resize (col);
			lines.insert (lines.begin () + line + 1, tail);
			line++; col = 0;
			write ();
			return true;
		}
		case KEY_BACKSPACE:
			if (col > 0)
			{
				int c = col - 1;
				while (c > 0 && ((unsigned char) l[c] & 0xC0) == 0x80) c--;
				l.erase (c, col - c);
				col = c;
			}
			else if (line > 0)
			{
				col = (int) lines[line - 1].size ();
				lines[line - 1] += l;
				lines.erase (lines.begin () + line);
				line--;
			}
			write ();
			return true;
		case KEY_LEFT:
			if (col > 0) { col--; while (col > 0 && ((unsigned char) l[col] & 0xC0) == 0x80) col--; } else if (line > 0) { line--; col = (int) lines[line].size (); }
			E.dirty = true; return true;
		case KEY_RIGHT:
			if (col < (int) l.size ()) { col++; while (col < (int) l.size () && ((unsigned char) l[col] & 0xC0) == 0x80) col++; } else if (line + 1 < (int) lines.size ()) { line++; col = 0; }
			E.dirty = true; return true;
		case KEY_UP: if (line > 0) { line--; col = std::min (col, (int) lines[line].size ()); } E.dirty = true; return true;
		case KEY_DOWN: if (line + 1 < (int) lines.size ()) { line++; col = std::min (col, (int) lines[line].size ()); } E.dirty = true; return true;
		}
		if (ch >= 32)
		{
			char b[5]; int n = 0;
			unsigned c = (unsigned) ch;
			if (c < 0x80) b[n++] = (char) c;
			else if (c < 0x800) { b[n++] = (char) (0xC0 | c >> 6); b[n++] = (char) (0x80 | (c & 63)); }
			else if (c < 0x10000) { b[n++] = (char) (0xE0 | c >> 12); b[n++] = (char) (0x80 | ((c >> 6) & 63)); b[n++] = (char) (0x80 | (c & 63)); }
			else { b[n++] = (char) (0xF0 | c >> 18); b[n++] = (char) (0x80 | ((c >> 12) & 63)); b[n++] = (char) (0x80 | ((c >> 6) & 63)); b[n++] = (char) (0x80 | (c & 63)); }
			l.insert (col, b, n);
			col += n;
			write ();
			return true;
		}
		return false;
	}
	void overlay (SkCanvas *c) override
	{
		if (!typing || !target) return;
		Affine m = node_to_page (target);
		double w = width (lines[line].substr (0, col));
		Vec2 a = E.to_screen (m.apply ({ x + w, y + size * 1.25 * line + size * 0.2 })), b = E.to_screen (m.apply ({ x + w, y + size * 1.25 * line - size * 0.85 }));
		SkPaint p;
		p.setColor (sk (jet (J_ACCENT)));
		p.setStrokeWidth (1.5f);
		c->drawLine ((float) a.x, (float) a.y, (float) b.x, (float) b.y, p);
	}
};

// ---- Gradient -----------------------------------------------------------------------------------------------------------
class GradientTool : public Tool
{
	XNode *target = nullptr;	// the object being given a gradient
	XNode *grad = nullptr;
	int dragging = -1;		// 0 start, 1 end
	bool open = false;
	Pointer start;

	static XNode *defs ()
	{
		for (XNode *k : E.doc.root->kids) if (k->is ("defs") || k->is ("svg:defs")) return k;
		XNode *d = x_new (X_ELEMENT, "defs");
		d->attrs.push_back ({ "id", E.doc.new_id ("defs") });
		E.doc.insert (E.doc.root, 0, d);
		return d;
	}
	XNode *grad_of (XNode *n)
	{
		std::string f = url_id (get_prop (n, "fill").c_str ());
		XNode *g = f.empty () ? nullptr : E.doc.by_id (f);
		if (g && !strcmp (el_name (g), "linearGradient")) return g;
		return nullptr;
	}
	Vec2 end_point (int i)
	{
		Affine m = node_to_page (target);
		Vec2 p (parse_length (grad->get (i ? "x2" : "x1"), 100, 16), parse_length (grad->get (i ? "y2" : "y1"), 100, 16));
		return E.to_screen (m.apply (p));
	}
public:
	const char *hint () override { return TR ("Drag over an object to give it a gradient."); }
	void enter () override { target = E.sel.size () == 1 ? E.sel[0] : nullptr; grad = target ? grad_of (target) : nullptr; }
	void press (const Pointer &q) override
	{
		start = q;
		dragging = -1;
		if (grad)
			for (int i = 0; i < 2; i++)
				if (vg::dist (q.s, end_point (i)) < 10) { dragging = i; return; }
		XNode *o = hit_object (q.p, 3 / E.view.zoom);
		if (!o) { target = grad = nullptr; E.select_none (); return; }
		E.select (o);
		target = o;
		grad = grad_of (o);
		E.doc.begin (TR ("Gradient"));
		open = true;
		if (!grad)
		{
			std::string col = get_prop (o, "fill");
			if (col.empty () || col == "none" || col.compare (0, 4, "url(") == 0) col = E.fill;
			XNode *d = defs ();
			XNode *g = x_new (X_ELEMENT, "linearGradient");
			std::string id = E.doc.new_id ("linearGradient");
			g->attrs.push_back ({ "id", id });
			g->attrs.push_back ({ "gradientUnits", "userSpaceOnUse" });
			for (int i = 0; i < 2; i++)
			{
				XNode *s = x_new (X_ELEMENT, "stop");
				s->attrs.push_back ({ "offset", i ? "1" : "0" });
				s->attrs.push_back ({ "style", "stop-color:" + col + ";stop-opacity:" + (i ? "0" : "1") });
				s->parent = g;
				g->kids.push_back (s);
			}
			E.doc.insert (d, -1, g);
			set_style (o, "fill", ("url(#" + id + ")").c_str ());
			grad = g;
		}
		Vec2 a = node_to_page (o).inverse ().apply (q.p);
		E.doc.set (grad, "x1", fmt (a.x)); E.doc.set (grad, "y1", fmt (a.y));
		E.doc.set (grad, "x2", fmt (a.x)); E.doc.set (grad, "y2", fmt (a.y));
		dragging = 1;
		E.sync ();
	}
	void move (const Pointer &q) override
	{
		if (dragging < 0 || !grad || !target) return;
		if (!open) { E.doc.begin (TR ("Gradient")); open = true; }
		Vec2 a = node_to_page (target).inverse ().apply (q.p);
		if (q.mods & MOD_CTRL)
		{
			Vec2 o (parse_length (grad->get (dragging ? "x1" : "x2"), 100, 16), parse_length (grad->get (dragging ? "y1" : "y2"), 100, 16));
			double an = std::round (std::atan2 (a.y - o.y, a.x - o.x) / (M_PI / 12)) * (M_PI / 12), l = vg::dist (a, o);
			a = o + Vec2 (std::cos (an), std::sin (an)) * l;
		}
		E.doc.set (grad, dragging ? "x2" : "x1", fmt (a.x));
		E.doc.set (grad, dragging ? "y2" : "y1", fmt (a.y));
		E.sync ();
	}
	void release (const Pointer &) override
	{
		if (open) { E.doc.commit (); open = false; E.sync (); }
		dragging = -1;
	}
	void overlay (SkCanvas *c) override
	{
		if (!grad || !target) return;
		Vec2 a = end_point (0), b = end_point (1);
		SkPaint h, l;
		h.setAntiAlias (true); l.setAntiAlias (true);
		h.setStrokeWidth (3); h.setColor (sk (jet (J_SURFACE), 0.7f));
		l.setStrokeWidth (1.5f); l.setColor (sk (jet (J_ACCENT)));
		c->drawLine ((float) a.x, (float) a.y, (float) b.x, (float) b.y, h);
		c->drawLine ((float) a.x, (float) a.y, (float) b.x, (float) b.y, l);
		paint_handle (c, a, 1, dragging == 0);
		paint_handle (c, b, 1, dragging == 1);
	}
};

Tool *make_text_tool () { return new TextTool; }
Tool *make_gradient_tool () { return new GradientTool; }

}
