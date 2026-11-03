//
// overlay.cpp -- what the editor paints over the drawing, in the view's pixels, sizes on the screen whatever the zoom
// (docs/UX-VECTOR.md 5). Every line is doubled: the colour on a pale halo, so it reads on any drawing in both themes.
//
#include "editor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPathBuilder.h"
#include "include/effects/SkDashPathEffect.h"
#include "../text/fonts.h"

namespace ve {

SkColor sk (unsigned rgb, float a = 1)
{
	return SkColorSetARGB ((int) (a * 255), (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}
static SkFont &ui_font ()
{
	static SkFont f;
	static bool made;
	if (!made)
	{
		made = true;
		f = SkFont (vr::font_ui (), 12);
		f.setSubpixel (true);
		f.setEdging (SkFont::Edging::kAntiAlias);
	}
	return f;
}

void paint_handle (SkCanvas *c, Vec2 s, int shape, bool hot)
{
	const float r = 5.5f;
	SkPaint fill, line;
	fill.setAntiAlias (true);
	line.setAntiAlias (true);
	line.setStyle (SkPaint::kStroke_Style);
	line.setStrokeWidth (1.5f);
	fill.setColor (sk (jet (hot ? J_ACCENT : J_SURFACE)));
	line.setColor (sk (jet (hot ? J_SURFACE : J_ACCENT)));
	float x = (float) std::floor (s.x) + 0.5f, y = (float) std::floor (s.y) + 0.5f;
	if (shape == 0)
	{
		SkRect q = SkRect::MakeLTRB (x - r, y - r, x + r, y + r);
		c->drawRect (q, fill); c->drawRect (q, line);
	}
	else if (shape == 2)
	{
		c->drawCircle (x, y, r + 1, fill); c->drawCircle (x, y, r + 1, line);
	}
	else
	{
		SkPathBuilder b;
		b.moveTo (x, y - r - 1.5f); b.lineTo (x + r + 1.5f, y); b.lineTo (x, y + r + 1.5f); b.lineTo (x - r - 1.5f, y); b.close ();
		SkPath p = b.detach ();
		c->drawPath (p, fill); c->drawPath (p, line);
	}
}

// a node: smaller than the selection's handles, dark outline, its kind in its shape (docs/UX-VECTOR.md 5.1, 5.2)
void paint_node (SkCanvas *c, Vec2 s, int kind, bool chosen)
{
	const float r = 4.5f;
	SkPaint fill, line;
	fill.setAntiAlias (true);
	line.setAntiAlias (true);
	line.setStyle (SkPaint::kStroke_Style);
	line.setStrokeWidth (1.5f);
	fill.setColor (sk (jet (chosen ? J_ACCENT : J_SURFACE)));
	line.setColor (sk (jet (J_TEXTE1)));
	float x = (float) std::floor (s.x) + 0.5f, y = (float) std::floor (s.y) + 0.5f;
	if (kind == 0) { SkRect q = SkRect::MakeLTRB (x - r, y - r, x + r, y + r); c->drawRect (q, fill); c->drawRect (q, line); }
	else if (kind == 2) { c->drawCircle (x, y, r, fill); c->drawCircle (x, y, r, line); }
	else
	{
		SkPathBuilder b;
		b.moveTo (x, y - r - 1); b.lineTo (x + r + 1, y); b.lineTo (x, y + r + 1); b.lineTo (x - r - 1, y); b.close ();
		SkPath p = b.detach ();
		c->drawPath (p, fill); c->drawPath (p, line);
	}
}

void paint_badge (SkCanvas *c, Vec2 s, const char *text)
{
	SkFont &f = ui_font ();
	float w = f.measureText (text, strlen (text), SkTextEncoding::kUTF8);
	float x = (float) s.x + 16, y = (float) s.y + 16;
	if (x + w + 12 > E.cw) x = E.cw - w - 12;
	if (y + 20 > E.ch) y = E.ch - 20;
	SkPaint bg;
	bg.setAntiAlias (true);
	bg.setColor (sk (jet (J_TEXTE1), 0.92f));
	c->drawRoundRect (SkRect::MakeXYWH (x, y, w + 12, 20), 5, 5, bg);
	SkPaint t;
	t.setAntiAlias (true);
	t.setColor (sk ((unsigned) ui_ink (jet (J_TEXTE1))));
	c->drawSimpleText (text, strlen (text), SkTextEncoding::kUTF8, x + 6, y + 14, f, t);
}

static Box screen_box (const Box &b)
{
	Vec2 a = E.to_screen ({ b.x0, b.y0 }), z = E.to_screen ({ b.x1, b.y1 });
	return { a.x, a.y, z.x, z.y };
}

Vec2 handle_pos (int h, const Box &b)
{
	double mx = (b.x0 + b.x1) / 2, my = (b.y0 + b.y1) / 2;
	// (small objects: the handles stand outside, docs/UX-VECTOR.md 5.1)
	double side = std::min (b.w (), b.h ()), off = side < 24 ? 14 : side < 48 ? 8 : 0;
	switch (h)
	{
	case H_NW: return { b.x0 - off, b.y0 - off };
	case H_N: return { mx, b.y0 - off };
	case H_NE: return { b.x1 + off, b.y0 - off };
	case H_E: return { b.x1 + off, my };
	case H_SE: return { b.x1 + off, b.y1 + off };
	case H_S: return { mx, b.y1 + off };
	case H_SW: return { b.x0 - off, b.y1 + off };
	case H_W: return { b.x0 - off, my };
	default: return { mx, b.y0 - off - 24 };
	}
}
static bool side_shown (const Box &b) { return std::min (b.w (), b.h ()) >= 96; }

int handle_at (Vec2 s)
{
	if (E.sel.empty ()) return H_NONE;
	Box b = screen_box (selection_box ());
	if (b.empty ()) return H_NONE;
	int best = H_NONE;
	double bd = 11;
	for (int h = 0; h < H_COUNT; h++)
	{
		if ((h == H_N || h == H_E || h == H_S || h == H_W) && !side_shown (b)) continue;
		double d = vg::dist (s, handle_pos (h, b));
		if (d < bd) { bd = d; best = h; }
	}
	return best;
}

void paint_selection (SkCanvas *c)
{
	if (E.sel.empty () || E.tool == T_NODE) return;
	SkPaint halo, line;
	halo.setStyle (SkPaint::kStroke_Style);
	halo.setStrokeWidth (3);
	halo.setColor (sk (jet (J_SURFACE), 0.5f));
	line.setStyle (SkPaint::kStroke_Style);
	line.setStrokeWidth (1);
	line.setColor (sk (jet (J_ACCENT)));
	float iv[2] = { 4, 4 };
	line.setPathEffect (SkDashPathEffect::Make (SkSpan<const float> (iv, 2), 0));
	if (E.sel.size () > 1)
	{
		SkPaint th;
		th.setStyle (SkPaint::kStroke_Style);
		th.setColor (sk (jet (J_ACCENT), 0.6f));
		for (XNode *n : E.sel)
		{
			Box b = screen_box (object_box (n));
			if (!b.empty ()) c->drawRect (SkRect::MakeLTRB ((float) b.x0, (float) b.y0, (float) b.x1, (float) b.y1), th);
		}
	}
	Box b = screen_box (selection_box ());
	if (b.empty ()) return;
	SkRect r = SkRect::MakeLTRB ((float) std::floor (b.x0) + .5f, (float) std::floor (b.y0) + .5f, (float) std::floor (b.x1) + .5f, (float) std::floor (b.y1) + .5f);
	c->drawRect (r, halo);
	c->drawRect (r, line);
	if (E.tool != T_SELECT) return;
	bool sides = side_shown (b);
	Vec2 top = handle_pos (H_N, b), rot = handle_pos (H_ROT, b);
	SkPaint tige;
	tige.setColor (sk (jet (J_ACCENT)));
	c->drawLine ((float) top.x, (float) top.y, (float) rot.x, (float) rot.y, tige);
	for (int h = 0; h < H_COUNT; h++)
	{
		bool isSide = h == H_N || h == H_E || h == H_S || h == H_W;
		if (isSide && !sides) continue;
		paint_handle (c, handle_pos (h, b), h == H_ROT ? 2 : isSide ? 1 : 0, false);
	}
}

void paint_overlay (SkCanvas *c)
{
	// an empty page says what to do (docs/UX-VECTOR.md 9.1)
	bool empty = true;
	for (XNode *k : E.doc.root->kids) if (k->element () && !object_box (k).empty ()) { empty = false; break; }
	if (empty && E.doc.path.empty ())
	{
		const char *t = TR ("Choose a tool and draw on the page.");
		SkFont &f = ui_font ();
		float w = f.measureText (t, strlen (t), SkTextEncoding::kUTF8);
		Vec2 m = E.to_screen ({ E.scene->width / 2, E.scene->height / 2 });
		SkPaint p;
		p.setAntiAlias (true);
		p.setColor (sk (jet (J_TEXTE3)));
		c->drawSimpleText (t, strlen (t), SkTextEncoding::kUTF8, (float) m.x - w / 2, (float) m.y, f, p);
	}
	paint_grid (c);
	paint_selection (c);
	E.tools[E.tool]->overlay (c);
	if (E.snapShown)
	{
		Vec2 s = E.to_screen (E.snapAt);
		SkPaint h, l;
		h.setStrokeWidth (3); h.setColor (sk (jet (J_SURFACE), 0.7f));
		l.setStrokeWidth (1.5f); l.setColor (sk (jet (J_SUCCES)));
		float x = (float) s.x, y = (float) s.y;
		c->drawLine (x - 6, y - 6, x + 6, y + 6, h); c->drawLine (x - 6, y + 6, x + 6, y - 6, h);
		c->drawLine (x - 6, y - 6, x + 6, y + 6, l); c->drawLine (x - 6, y + 6, x + 6, y - 6, l);
		SkPaint t;
		t.setAntiAlias (true);
		t.setColor (sk (jet (J_SUCCES)));
		c->drawSimpleText (E.snapWhat, strlen (E.snapWhat), SkTextEncoding::kUTF8, x + 9, y - 7, ui_font (), t);
	}
}

}
