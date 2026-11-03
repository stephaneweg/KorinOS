//
// scene.cpp -- from the tree to drawing nodes (see scene.h). Rendering rules from the SVG 1.1 and SVG 2 specifications.
//
#include "scene.h"
#include "include/codec/SkCodec.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/codec/SkWebpDecoder.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkShader.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkLumaColorFilter.h"
#include "include/pathops/SkPathOps.h"
#include <cstdio>
#include <cstring>

using namespace vd;
using vg::Affine;
using vg::Box;
using vg::Vec2;

namespace vr {

void init_codecs ()
{
	static bool done;
	if (done) return;
	done = true;
	SkCodecs::Register (SkPngDecoder::Decoder ());
	SkCodecs::Register (SkJpegDecoder::Decoder ());
	SkCodecs::Register (SkWebpDecoder::Decoder ());
}

const char *el_name (const XNode *n)
{
	const char *s = n->name.c_str ();
	return !strncmp (s, "svg:", 4) ? s + 4 : s;
}
static bool named (const XNode *n, const char *s) { return n->element () && !strcmp (el_name (n), s); }
static const char *href_of (const XNode *n)
{
	const char *h = n->get ("href");
	if (!h) h = n->get ("xlink:href");
	return h;
}

bool parse_viewbox (const char *s, double vb[4])
{
	auto v = parse_numbers (s);
	if (v.size () != 4 || v[2] <= 0 || v[3] <= 0) return false;
	for (int i = 0; i < 4; i++) vb[i] = v[i];
	return true;
}
Affine viewbox_transform (const double vb[4], const char *par, double x, double y, double w, double h)
{
	if (!par) par = "xMidYMid meet";
	while (*par == ' ') par++;
	if (!strncmp (par, "defer", 5)) { par += 5; while (*par == ' ') par++; }
	double sx = w / vb[2], sy = h / vb[3];
	bool none = !strncmp (par, "none", 4), slice = strstr (par, "slice") != nullptr;
	if (!none) { double k = slice ? std::max (sx, sy) : std::min (sx, sy); sx = sy = k; }
	double tx = x - vb[0] * sx, ty = y - vb[1] * sy;
	if (!none)
	{
		double ax = strstr (par, "xMin") ? 0 : strstr (par, "xMax") ? 1 : 0.5;
		double ay = strstr (par, "YMin") ? 0 : strstr (par, "YMax") ? 1 : 0.5;
		tx += (w - vb[2] * sx) * ax;
		ty += (h - vb[3] * sy) * ay;
	}
	return { sx, 0, 0, sy, tx, ty };
}

SkPath to_skpath (const vg::Path &p, bool evenodd)
{
	SkPathBuilder b (evenodd ? SkPathFillType::kEvenOdd : SkPathFillType::kWinding);
	for (auto &s : p.sub)
	{
		if (s.n.empty ()) continue;
		b.moveTo ((float) s.n[0].p.x, (float) s.n[0].p.y);
		for (int i = 0; i < s.segments (); i++)
		{
			vg::Bezier q = vg::seg_of (s, i);
			if (q.line ()) b.lineTo ((float) q.p3.x, (float) q.p3.y);
			else b.cubicTo ((float) q.p1.x, (float) q.p1.y, (float) q.p2.x, (float) q.p2.y, (float) q.p3.x, (float) q.p3.y);
		}
		if (s.closed) b.close ();
	}
	return b.detach ();
}

static double len_attr (const XNode *n, const char *a, double pct, double fs, double def = 0)
{
	bool ok;
	double v = parse_length (n->get (a), pct, fs, &ok);
	return ok ? v : def;
}

// ---- paints ----------------------------------------------------------------------------------------------------------
static SkColor4f color4 (uint32_t rgb, float a)
{
	return { ((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a };
}

// an attribute through the href chain of gradients and patterns
static bool is_gradient (const XNode *n) { return named (n, "linearGradient") || named (n, "radialGradient"); }
// a gradient reads from gradients, a pattern from patterns; x1.. only from linear ones, cx.. only from radial ones
static const char *chain_attr (Document *doc, XNode *g, const char *name)
{
	bool grad = is_gradient (g);
	static const char *const LIN[] = { "x1", "y1", "x2", "y2" }, *const RAD[] = { "cx", "cy", "r", "fx", "fy", "fr" };
	const char *only = nullptr;
	if (grad) { for (auto s : LIN) if (!strcmp (name, s)) only = "linearGradient"; for (auto s : RAD) if (!strcmp (name, s)) only = "radialGradient"; }
	for (int i = 0; g && i < 16; i++)
	{
		if (!only || named (g, only)) if (const char *v = g->get (name)) return v;
		const char *h = href_of (g);
		XNode *n = h && h[0] == '#' ? doc->by_id (h + 1) : nullptr;
		if (n && (grad ? !is_gradient (n) : !named (n, "pattern"))) n = nullptr;
		g = n;
	}
	return nullptr;
}
static XNode *chain_stops (Document *doc, XNode *g)
{
	for (int i = 0; g && i < 16; i++)
	{
		for (XNode *k : g->kids) if (named (k, "stop")) return g;
		const char *h = href_of (g);
		XNode *n = h && h[0] == '#' ? doc->by_id (h + 1) : nullptr;
		g = n && is_gradient (n) ? n : nullptr;
	}
	return nullptr;
}
static double frac (const char *s, double def)
{
	if (!s) return def;
	char *e;
	double v = strtod (s, &e);
	if (e == s) return def;
	return *e == '%' ? v / 100 : v;
}

sk_sp<SkShader> SceneBuilder::gradient (XNode *g, float opacity, const SkRect &bbox)
{
	std::vector<SkColor4f> cols;
	std::vector<float> pos;
	if (XNode *sg = chain_stops (doc, g))
	{
		Style base = inherited_style (sg);
		float last = 0;
		for (XNode *k : sg->kids)
			if (named (k, "stop"))
			{
				Style st;
				compute_style (k, base, sheet, st);
				float p = (float) std::min (1.0, std::max (0.0, frac (k->get ("offset"), 0)));
				if (p < last) p = last;
				last = p;
				pos.push_back (p);
				cols.push_back (color4 (st.stopColor, st.stopOpacity * opacity));
			}
	}
	if (cols.empty ()) return nullptr;
	bool userSpace = false;
	if (const char *u = chain_attr (doc, g, "gradientUnits")) userSpace = !strcmp (u, "userSpaceOnUse");
	if (!userSpace && (bbox.width () <= 0 || bbox.height () <= 0)) return nullptr;
	Affine t = parse_transform (chain_attr (doc, g, "gradientTransform"));
	if (!t.invertible ()) return nullptr;
	SkMatrix lm = to_sk (t);
	if (!userSpace) lm.postConcat (SkMatrix::MakeAll (bbox.width (), 0, bbox.left (), 0, bbox.height (), bbox.top (), 0, 0, 1));
	SkTileMode tile = SkTileMode::kClamp;
	if (const char *s = chain_attr (doc, g, "spreadMethod")) tile = !strcmp (s, "reflect") ? SkTileMode::kMirror : !strcmp (s, "repeat") ? SkTileMode::kRepeat : SkTileMode::kClamp;
	if (cols.size () == 1) { cols.push_back (cols[0]); pos.push_back (1); }
	auto L = [&] (const char *a, double def) {
		const char *v = chain_attr (doc, g, a);
		if (!v) return def;
		if (userSpace) { bool isY = a[1] == 'y' || a[0] == 'y'; return parse_length (v, isY ? vpH : vpW, 16); }
		return frac (v, def);
	};
	SkGradient grad (SkGradient::Colors (SkSpan<const SkColor4f> (cols.data (), cols.size ()), SkSpan<const float> (pos.data (), pos.size ()), tile), SkGradient::Interpolation ());
	if (named (g, "linearGradient"))
	{
		SkPoint pts[2] = { { (float) L ("x1", userSpace ? 0 : 0), (float) L ("y1", 0) }, { (float) L ("x2", userSpace ? vpW : 1), (float) L ("y2", 0) } };
		return SkShaders::LinearGradient (pts, grad, &lm);
	}
	double cx = L ("cx", userSpace ? vpW / 2 : 0.5), cy = L ("cy", userSpace ? vpH / 2 : 0.5);
	double r = L ("r", userSpace ? std::sqrt ((vpW * vpW + vpH * vpH) / 2) / 2 : 0.5);
	double fx = L ("fx", cx), fy = L ("fy", cy), fr = L ("fr", 0);
	if (r < 0) r = 0;
	if (fr < 0) fr = 0;
	if (r == 0) return SkShaders::Color (cols.back (), nullptr);
	if (fx == cx && fy == cy && fr == 0) return SkShaders::RadialGradient ({ (float) cx, (float) cy }, (float) r, grad, &lm);
	// (a focal point outside the circle is pulled in, as the specification asks)
	double dx = fx - cx, dy = fy - cy, d = std::hypot (dx, dy);
	if (d >= r * 0.9999) { double k = r * 0.9999 / d; fx = cx + dx * k; fy = cy + dy * k; }
	return SkShaders::TwoPointConicalGradient ({ (float) fx, (float) fy }, (float) fr, { (float) cx, (float) cy }, (float) r, grad, &lm);
}

sk_sp<SkShader> SceneBuilder::pattern (XNode *g, float opacity, const SkRect &bbox)
{
	if (stack.count (g)) return nullptr;
	const char *u = chain_attr (doc, g, "patternUnits");
	bool user = u && !strcmp (u, "userSpaceOnUse");
	const char *cu = chain_attr (doc, g, "patternContentUnits");
	bool cuser = !cu || strcmp (cu, "objectBoundingBox");
	auto L = [&] (const char *a) {
		const char *v = chain_attr (doc, g, a);
		if (!v) return 0.0;
		if (user) return parse_length (v, a[0] == 'x' || a[0] == 'w' ? vpW : vpH, 16);
		return frac (v, 0);
	};
	double x = L ("x"), y = L ("y"), w = L ("width"), h = L ("height");
	if (!user) { x = bbox.left () + x * bbox.width (); y = bbox.top () + y * bbox.height (); w *= bbox.width (); h *= bbox.height (); }
	if (w <= 0 || h <= 0) return nullptr;
	XNode *content = g;
	for (int i = 0; i < 16 && content; i++)
	{
		bool any = false;
		for (XNode *k : content->kids) if (k->element ()) any = true;
		if (any) break;
		const char *hr = href_of (content);
		content = hr && hr[0] == '#' ? doc->by_id (hr + 1) : nullptr;
	}
	if (!content) return nullptr;
	Affine inner;
	double vb[4];
	if (parse_viewbox (chain_attr (doc, g, "viewBox"), vb)) inner = viewbox_transform (vb, chain_attr (doc, g, "preserveAspectRatio"), 0, 0, w, h);
	else if (!cuser) inner = Affine::scale (bbox.width (), bbox.height ());
	stack.insert (g);
	inDefs++;
	double svW = vpW, svH = vpH;
	Style base;
	compute_style (content, Style (), sheet, base);
	SkPictureRecorder rec;
	SkCanvas *c = rec.beginRecording (SkRect::MakeWH ((float) w, (float) h));
	c->concat (to_sk (inner));
	if (opacity < 1) c->saveLayerAlphaf (nullptr, opacity);
	for (XNode *k : content->kids)
		if (k->element ())
			if (DRef d = build (k, base, Affine (), true)) draw_node (c, *d);
	if (opacity < 1) c->restore ();
	sk_sp<SkPicture> pic = rec.finishRecordingAsPicture ();
	vpW = svW; vpH = svH;
	inDefs--;
	stack.erase (g);
	SkMatrix lm = to_sk (parse_transform (chain_attr (doc, g, "patternTransform")) * Affine::translate (x, y));
	SkRect tile = SkRect::MakeWH ((float) w, (float) h);
	return pic->makeShader (SkTileMode::kRepeat, SkTileMode::kRepeat, SkFilterMode::kLinear, &lm, &tile);
}

// meshgradient (SVG 2 draft): rows of Coons patches. Each stop's path is the edge leaving a corner, its colour that corner's;
// the first patch has its four edges, the rest of a row three, the next rows three then two: what they share is not repeated.
sk_sp<SkShader> SceneBuilder::mesh (XNode *g, float opacity, const SkRect &bbox)
{
	bool user = true;
	if (const char *u = g->get ("gradientUnits")) user = strcmp (u, "objectBoundingBox") != 0;
	Vec2 org (parse_length (g->get ("x"), vpW, 16), parse_length (g->get ("y"), vpH, 16));
	struct Patch { SkPoint c[12]; SkColor4f col[4]; };
	std::vector<std::vector<Patch>> rows;
	for (XNode *r : g->kids)
	{
		if (!named (r, "meshrow")) continue;
		std::vector<Patch> row;
		int ri = (int) rows.size ();
		for (XNode *m : r->kids)
		{
			if (!named (m, "meshpatch")) continue;
			int ci = (int) row.size ();
			Patch q;
			for (auto &c : q.col) c = { 0, 0, 0, 1 };
			// the edges this patch shares with its neighbours
			bool hasTop = ri > 0, hasLeft = ci > 0;
			Vec2 cur = org;
			if (hasTop)
			{
				const Patch &up = rows[ri - 1][std::min (ci, (int) rows[ri - 1].size () - 1)];
				// the bottom edge of the patch above, reversed
				q.c[0] = up.c[9]; q.c[1] = up.c[8]; q.c[2] = up.c[7]; q.c[3] = up.c[6];
				q.col[0] = up.col[3]; q.col[1] = up.col[2];
				cur = { q.c[3].fX, q.c[3].fY };
			}
			if (hasLeft)
			{
				const Patch &lf = row[ci - 1];
				q.c[0] = lf.c[3]; q.c[11] = lf.c[4]; q.c[10] = lf.c[5]; q.c[9] = lf.c[6];
				q.col[0] = lf.col[1]; q.col[3] = lf.col[2];
				if (!hasTop) cur = { q.c[0].fX, q.c[0].fY };
			}
			int edge = hasTop ? 1 : 0;
			for (XNode *k : m->kids)
			{
				if (!named (k, "stop") || edge > 3) continue;
				if (hasLeft && edge == 3) break;
				Style st;
				compute_style (k, inherited_style (k), sheet, st);
				SkColor4f col = color4 (st.stopColor, st.stopOpacity * opacity);
				vg::Path e = vg::parse_d ((std::string ("M0 0 ") + k->get2 ("path", "l0 0")).c_str ());
				Vec2 a1, a2, a3;
				if (!e.empty () && e.sub[0].n.size () >= 2) { a1 = e.sub[0].n[0].out; a2 = e.sub[0].n[1].in; a3 = e.sub[0].n[1].p; if (e.sub[0].n[0].out == Vec2 ()) a1 = a3 / 3; if (a2 == a3) a2 = a3 * (2.0 / 3); }
				int base = edge * 3;
				if (edge == 0) q.c[0] = { (float) cur.x, (float) cur.y };
				q.col[edge] = col;
				if (edge < 3 || true)
				{
					q.c[(base + 1) % 12] = { (float) (cur.x + a1.x), (float) (cur.y + a1.y) };
					q.c[(base + 2) % 12] = { (float) (cur.x + a2.x), (float) (cur.y + a2.y) };
					if (edge < 3) q.c[base + 3] = { (float) (cur.x + a3.x), (float) (cur.y + a3.y) };
				}
				cur += a3;
				edge++;
			}
			row.push_back (q);
		}
		rows.push_back (std::move (row));
	}
	if (rows.empty ()) return nullptr;
	SkRect b = SkRect::MakeEmpty ();
	for (auto &r : rows) for (auto &q : r) for (auto &c : q.c) b.join (SkRect::MakeXYWH (c.fX, c.fY, 0.001f, 0.001f));
	SkPictureRecorder rec;
	SkCanvas *c = rec.beginRecording (b);
	for (auto &r : rows)
		for (auto &q : r)
		{
			SkColor cols[4];
			for (int i = 0; i < 4; i++) cols[i] = q.col[i].toSkColor ();
			c->drawPatch (q.c, cols, nullptr, SkBlendMode::kDst, SkPaint ());
		}
	sk_sp<SkPicture> pic = rec.finishRecordingAsPicture ();
	SkMatrix lm = to_sk (parse_transform (g->get ("gradientTransform")));
	if (!user) lm.postConcat (SkMatrix::MakeAll (bbox.width (), 0, bbox.left (), 0, bbox.height (), bbox.top (), 0, 0, 1));
	return pic->makeShader (SkTileMode::kDecal, SkTileMode::kDecal, SkFilterMode::kLinear, &lm, &b);
}

bool SceneBuilder::resolve_paint (const PaintSpec &spec, float opacity, const Style &st, const SkRect &bbox, SkPaint &p)
{
	p.setAntiAlias (st.shapeRendering == 0);
	switch (spec.kind)
	{
	case P_NONE:
		return false;
	case P_COLOR:
		p.setColor4f (color4 (spec.rgb, opacity * spec.alpha));
		return true;
	case P_CURRENT:
		p.setColor4f (color4 (st.color, opacity));
		return true;
	case P_CTX_FILL:
		return ctxFill ? resolve_paint (*ctxFill, opacity, st, bbox, p) : false;
	case P_CTX_STROKE:
		return ctxStroke ? resolve_paint (*ctxStroke, opacity, st, bbox, p) : false;
	case P_URL:
	{
		note_refs (nullptr);
		usedIds.insert (spec.url);
		XNode *g = doc->by_id (spec.url);
		sk_sp<SkShader> sh;
		if (g && (named (g, "linearGradient") || named (g, "radialGradient"))) sh = gradient (g, opacity, bbox);
		else if (g && named (g, "pattern")) sh = pattern (g, opacity, bbox);
		else if (g && named (g, "meshgradient")) sh = mesh (g, opacity, bbox);
		if (sh) { p.setShader (std::move (sh)); return true; }
		if (spec.hasFallback && spec.alpha > 0) { p.setColor4f (color4 (spec.rgb, opacity * spec.alpha)); return true; }
		return false;
	}
	}
	return false;
}

void SceneBuilder::setup_stroke (const Style &st, SkPaint &p)
{
	p.setStyle (SkPaint::kStroke_Style);
	p.setStrokeWidth ((float) st.strokeWidth);
	p.setStrokeCap (st.lineCap == LC_ROUND ? SkPaint::kRound_Cap : st.lineCap == LC_SQUARE ? SkPaint::kSquare_Cap : SkPaint::kButt_Cap);
	p.setStrokeJoin (st.lineJoin == LJ_ROUND ? SkPaint::kRound_Join : st.lineJoin == LJ_BEVEL ? SkPaint::kBevel_Join : SkPaint::kMiter_Join);
	p.setStrokeMiter ((float) st.strokeMiter);
	double total = 0;
	for (double d : st.dash) total += d;
	if (!st.dash.empty () && total > 0)
	{
		std::vector<float> iv;
		for (double d : st.dash) iv.push_back ((float) d);
		if (iv.size () & 1) { size_t n = iv.size (); for (size_t i = 0; i < n; i++) iv.push_back (iv[i]); }
		p.setPathEffect (SkDashPathEffect::Make (SkSpan<const float> (iv.data (), iv.size ()), (float) st.dashOffset));
	}
}

static SkBlendMode blend_of (uint8_t b)
{
	static const SkBlendMode M[] = { SkBlendMode::kSrcOver, SkBlendMode::kMultiply, SkBlendMode::kScreen, SkBlendMode::kOverlay, SkBlendMode::kDarken,
		SkBlendMode::kLighten, SkBlendMode::kColorDodge, SkBlendMode::kColorBurn, SkBlendMode::kHardLight, SkBlendMode::kSoftLight, SkBlendMode::kDifference,
		SkBlendMode::kExclusion, SkBlendMode::kHue, SkBlendMode::kSaturation, SkBlendMode::kColor, SkBlendMode::kLuminosity };
	return M[b & 15];
}

// ---- clips and masks -------------------------------------------------------------------------------------------------
void SceneBuilder::clip_collect (XNode *el, const Style &parent, const SkMatrix &m, SkPath &out, bool &any)
{
	if (!el->element ()) return;
	Style st;
	compute_style (el, parent, sheet, st);
	if (!st.display || !st.visible) return;
	const char *name = el_name (el);
	SkMatrix t = m;
	t.preConcat (to_sk (element_transform (el, st)));
	SkPath p;
	if (!strcmp (name, "use"))
	{
		const char *h = href_of (el);
		XNode *target = h && h[0] == '#' ? doc->by_id (h + 1) : nullptr;
		if (!target || stack.count (target)) return;
		stack.insert (target);
		t.preTranslate ((float) len_attr (el, "x", vpW, st.fontSize), (float) len_attr (el, "y", vpH, st.fontSize));
		clip_collect (target, st, t, out, any);
		stack.erase (target);
		return;
	}
	if (!strcmp (name, "text"))
	{
		DNode tn;
		layout_text (*this, el, st, tn);
		p = text_outline (tn.pieces).makeTransform (t);
		if (!any) { out = p; any = true; }
		else if (auto u = ::Op (out, p, kUnion_SkPathOp)) out = *u;
		return;
	}
	// a shape, by building it without its paint
	DRef d = build_shape (el, st, SkMatrix::I (), Affine ());
	if (!d) return;
	p = d->path.makeFillType (st.clipRule == FR_EVENODD ? SkPathFillType::kEvenOdd : SkPathFillType::kWinding).makeTransform (t);
	// a child can be clipped too, in its own coordinates
	if (!st.clipPath.empty ())
	{
		SkPath c2;
		if (clip_path (st.clipPath, d->path.computeTightBounds (), c2))
			if (auto r = ::Op (p, c2.makeTransform (t), kIntersect_SkPathOp)) p = *r;
	}
	if (!any) { out = p; any = true; }
	else if (auto u = ::Op (out, p, kUnion_SkPathOp)) out = *u;
}

bool SceneBuilder::clip_path (const std::string &id, const SkRect &bbox, SkPath &out)
{
	XNode *cp = doc->by_id (id);
	usedIds.insert (id);
	if (!cp || !named (cp, "clipPath") || stack.count (cp)) return false;
	stack.insert (cp);
	Style base;
	compute_style (cp, inherited_style (cp), sheet, base);
	SkMatrix m = to_sk (element_transform (cp, base));
	if (const char *u = cp->get ("clipPathUnits"))
		if (!strcmp (u, "objectBoundingBox"))
			m.preConcat (SkMatrix::MakeAll (bbox.width (), 0, bbox.left (), 0, bbox.height (), bbox.top (), 0, 0, 1));
	bool any = false;
	for (XNode *k : cp->kids) clip_collect (k, base, m, out, any);
	if (!any) out = SkPath ();			// (an empty clip hides everything)
	if (!base.clipPath.empty ())
	{
		SkPath o2;
		if (clip_path (base.clipPath, bbox, o2)) { if (auto r = ::Op (out, o2, kIntersect_SkPathOp)) out = *r; }
	}
	stack.erase (cp);
	return true;
}

void SceneBuilder::effects (DNode &n, XNode *el, const Style &st, const SkRect &own)
{
	n.opacity = st.opacity;
	n.blend = blend_of (st.blend);
	(void) el;
	if (!st.clipPath.empty ())
	{
		SkPath c;
		XNode *cp = doc->by_id (st.clipPath);
		if (cp && stack.count (cp)) { n.bounds = SkRect::MakeEmpty (); return; }	// (it refers to itself: nothing is drawn)
		if (clip_path (st.clipPath, own, c)) { n.hasClip = true; n.clip = c; n.bounds.intersect (c.getBounds ().makeOffset (0, 0)) || (n.bounds = SkRect::MakeEmpty (), true); }
	}
	if (!st.filter.empty ())
	{
		usedIds.insert (st.filter);
		XNode *f = doc->by_id (st.filter);
		SkRect region = own;
		sk_sp<SkImageFilter> fl;
		if (f && named (f, "filter")) fl = build_filter (*this, f, st, own, &region);
		// (a filter that cannot be found, or an empty region: the element is not drawn)
		if (!fl) { n.bounds = SkRect::MakeEmpty (); return; }
		n.filter = fl; n.bounds = region;
	}
	else if (!st.filterFunc.empty ())
	{
		SkRect region = own;
		sk_sp<SkImageFilter> fl = filter_functions (*this, st, own, &region);
		if (!fl) { n.bounds = SkRect::MakeEmpty (); return; }
		n.filter = fl; n.bounds = region;
	}
	if (!st.mask.empty ())
	{
		usedIds.insert (st.mask);
		XNode *mk = doc->by_id (st.mask);
		if (mk && named (mk, "mask") && stack.count (mk)) { n.bounds = SkRect::MakeEmpty (); return; }
		if (mk && named (mk, "mask"))
		{
			stack.insert (mk);
			inDefs++;
			bool userMask = false;
			if (const char *u = mk->get ("maskUnits")) userMask = !strcmp (u, "userSpaceOnUse");
			const char *cu = mk->get ("maskContentUnits");
			bool cbox = cu && !strcmp (cu, "objectBoundingBox");
			auto L = [&] (const char *a, double def) {
				const char *v = mk->get (a);
				if (!v) return userMask ? (a[0] == 'x' || a[0] == 'w' ? vpW : vpH) * def : def;
				if (userMask) return parse_length (v, a[0] == 'x' || a[0] == 'w' ? vpW : vpH, 16);
				return frac (v, def);
			};
			double x = L ("x", -0.1), y = L ("y", -0.1), w = L ("width", 1.2), h = L ("height", 1.2);
			if (!userMask) { x = own.left () + x * own.width (); y = own.top () + y * own.height (); w *= own.width (); h *= own.height (); }
			auto g = std::make_shared<DNode> ();
			Style base;
			compute_style (mk, inherited_style (mk), sheet, base);
			g->opacity = 1;
			g->pixelated = get_prop (mk, "mask-type") == "alpha";		// (a field no group uses: the mask's kind)
			if (cbox) g->m = SkMatrix::MakeAll (own.width (), 0, own.left (), 0, own.height (), own.top (), 0, 0, 1);
			for (XNode *k : mk->kids)
				if (k->element ())
					if (DRef d = build (k, base, Affine (), true)) g->kids.push_back (d);
			g->hasClipRect = false;
			g->dst = SkRect::MakeXYWH ((float) x, (float) y, (float) w, (float) h);
			g->bounds = g->dst;
			// a mask on the mask
			if (!base.mask.empty ())
			{
				Style ms = base;
				DNode holder;
				holder.bounds = g->dst;
				SkRect keep = g->dst;
				effects (holder, mk, ms, keep);
				if (holder.bounds.isEmpty ()) n.bounds = SkRect::MakeEmpty ();
				g->mask = holder.mask;
			}
			n.mask = g;
			inDefs--;
			stack.erase (mk);
		}
	}
}

// ---- markers ---------------------------------------------------------------------------------------------------------
void SceneBuilder::markers (DNode &n, const vg::Path &p, const Style &st)
{
	if (st.markerStart.empty () && st.markerMid.empty () && st.markerEnd.empty ()) return;
	struct V { Vec2 at; double angle; int which; };
	std::vector<V> vs;
	auto dir = [] (const vg::Bezier &b, bool end) {
		Vec2 d = end ? (b.p3 == b.p2 ? (b.p3 == b.p1 ? b.p3 - b.p0 : b.p3 - b.p1) : b.p3 - b.p2)
			: (b.p1 == b.p0 ? (b.p2 == b.p0 ? b.p3 - b.p0 : b.p2 - b.p0) : b.p1 - b.p0);
		return d;
	};
	int subIdx = 0, subs = (int) p.sub.size ();
	for (auto &s : p.sub)
	{
		int cnt = (int) s.n.size ();
		for (int i = 0; i < cnt; i++)
		{
			Vec2 in, out;
			bool hasIn = false, hasOut = false;
			if (i > 0 || s.closed) { in = dir (vg::seg_of (s, (i + cnt - 1) % cnt), true); hasIn = true; }
			if (i < cnt - 1 || s.closed) { out = dir (vg::seg_of (s, i), false); hasOut = true; }
			Vec2 d = hasIn && hasOut ? vg::unit (in) + vg::unit (out) : hasIn ? in : out;
			double ang = std::atan2 (d.y, d.x);
			int which = 1;
			if (subIdx == 0 && i == 0) which = 0;
			if (subIdx == subs - 1 && i == cnt - 1 && !s.closed) which = 2;
			vs.push_back ({ s.n[i].p, ang, which });
		}
		if (s.closed && subIdx == subs - 1 && cnt)
		{
			Vec2 in = dir (vg::seg_of (s, cnt - 1), true), out = dir (vg::seg_of (s, 0), false);
			Vec2 d = vg::unit (in) + vg::unit (out);
			vs.push_back ({ s.n[0].p, std::atan2 (d.y, d.x), 2 });
		}
		subIdx++;
	}
	const PaintSpec *sf = ctxFill, *ss = ctxStroke;
	ctxFill = &st.fill; ctxStroke = &st.stroke;
	for (auto &v : vs)
	{
		const std::string &id = v.which == 0 ? st.markerStart : v.which == 1 ? st.markerMid : st.markerEnd;
		if (id.empty ()) continue;
		usedIds.insert (id);
		XNode *mk = doc->by_id (id);
		if (!mk || !named (mk, "marker") || stack.count (mk)) continue;
		Style base;
		compute_style (mk, inherited_style (mk), sheet, base);
		double mw = len_attr (mk, "markerWidth", vpW, 16, 3), mh = len_attr (mk, "markerHeight", vpH, 16, 3);
		double rx = 0, ry = 0;
		const char *ori = mk->get2 ("orient", "0");
		double ang = 0;
		if (!strcmp (ori, "auto")) ang = v.angle;
		else if (!strcmp (ori, "auto-start-reverse")) ang = v.which == 0 ? v.angle + M_PI : v.angle;
		else ang = atof (ori) * M_PI / 180;
		const char *mu = mk->get ("markerUnits");
		double k = !mu || strcmp (mu, "userSpaceOnUse") ? st.strokeWidth : 1;
		Affine vbm;
		double vb[4];
		bool hasVb = parse_viewbox (mk->get ("viewBox"), vb);
		if (hasVb) vbm = viewbox_transform (vb, mk->get ("preserveAspectRatio"), 0, 0, mw, mh);
		bool ok;
		double refx = parse_length (mk->get ("refX"), mw, 16, &ok), refy = parse_length (mk->get ("refY"), mh, 16, &ok);
		Vec2 ref = vbm.apply ({ refx, refy });
		rx = ref.x; ry = ref.y;
		Affine t = Affine::translate (v.at.x, v.at.y) * Affine::rotate (ang) * Affine::scale (k, k) * Affine::translate (-rx, -ry);
		auto g = std::make_shared<DNode> ();
		g->m = to_sk (t);
		const char *ov = mk->get ("overflow");
		bool clipped = !ov || strcmp (ov, "visible");
		if (base.overflowVisible) clipped = false;
		if (clipped) { g->hasClipRect = true; g->clipRect = SkRect::MakeWH ((float) mw, (float) mh); }
		auto c = std::make_shared<DNode> ();
		c->m = to_sk (vbm);
		stack.insert (mk);
		inDefs++;
		SkRect u = SkRect::MakeEmpty ();
		for (XNode *kid : mk->kids)
			if (kid->element ())
				if (DRef d = build (kid, base, Affine (), true)) { c->kids.push_back (d); u.join (d->bounds); }
		inDefs--;
		stack.erase (mk);
		c->bounds = c->m.mapRect (u);
		if (clipped) c->bounds.intersect (g->clipRect);
		g->kids.push_back (c);
		g->bounds = g->m.mapRect (c->bounds);
		n.bounds.join (g->bounds);
		n.kids.push_back (g);
	}
	ctxFill = sf; ctxStroke = ss;
}

// ---- elements --------------------------------------------------------------------------------------------------------
static bool shape_path (XNode *el, const char *name, const Style &st, double vw, double vh, vg::Path &p)
{
	double fs = st.fontSize;
	if (!strcmp (name, "path"))
	{
		p = vg::parse_d (el->get ("d"));
		if (const char *t = el->get ("sodipodi:nodetypes")) vg::apply_nodetypes (p, t);
	}
	else if (!strcmp (name, "rect"))
	{
		double w = len_attr (el, "width", vw, fs), h = len_attr (el, "height", vh, fs);
		if (w <= 0 || h <= 0) return false;
		p = vg::path_rect (len_attr (el, "x", vw, fs), len_attr (el, "y", vh, fs), w, h, len_attr (el, "rx", vw, fs, -1), len_attr (el, "ry", vh, fs, -1));
	}
	else if (!strcmp (name, "circle"))
	{
		double dg = std::sqrt ((vw * vw + vh * vh) / 2), r = len_attr (el, "r", dg, fs);
		if (r <= 0) return false;
		p = vg::path_ellipse (len_attr (el, "cx", vw, fs), len_attr (el, "cy", vh, fs), r, r);
	}
	else if (!strcmp (name, "ellipse"))
	{
		double rx = len_attr (el, "rx", vw, fs, -1), ry = len_attr (el, "ry", vh, fs, -1);
		if (rx < 0) rx = ry;
		if (ry < 0) ry = rx;
		if (rx <= 0 || ry <= 0) return false;
		p = vg::path_ellipse (len_attr (el, "cx", vw, fs), len_attr (el, "cy", vh, fs), rx, ry);
	}
	else if (!strcmp (name, "line"))
	{
		p.move_to ({ len_attr (el, "x1", vw, fs), len_attr (el, "y1", vh, fs) });
		p.line_to ({ len_attr (el, "x2", vw, fs), len_attr (el, "y2", vh, fs) });
	}
	else if (!strcmp (name, "polyline") || !strcmp (name, "polygon"))
	{
		auto v = parse_numbers (el->get ("points"));
		if (v.size () < 2) return false;
		for (size_t i = 0; i + 1 < v.size (); i += 2)
			if (i == 0) p.move_to ({ v[0], v[1] }); else p.line_to ({ v[i], v[i + 1] });
		if (name[4] == 'g') p.close ();
	}
	else return false;
	return true;
}

DRef SceneBuilder::build_shape (XNode *el, const Style &st, const SkMatrix &m, const Affine &ctm)
{
	(void) ctm;
	vg::Path p;
	if (!shape_path (el, el_name (el), st, vpW, vpH, p) || p.empty ()) return nullptr;
	auto n = std::make_shared<DNode> ();
	n->kind = D_SHAPE;
	n->uid = el->uid;
	n->m = m;
	n->path = to_skpath (p, st.fillRule == FR_EVENODD);
	SkRect box = n->path.computeTightBounds ();
	memcpy (n->order, st.paintOrder, 3);
	if (st.visible)
	{
		n->hasFill = resolve_paint (st.fill, st.fillOpacity, st, box, n->fill);
		n->hasStroke = st.strokeWidth > 0 && resolve_paint (st.stroke, st.strokeOpacity, st, box, n->stroke);
	}
	SkRect b = n->hasFill || !n->hasStroke ? box : box;
	if (n->hasStroke)
	{
		setup_stroke (st, n->stroke);
		float k = (float) (st.strokeWidth / 2 * (st.lineJoin == LJ_MITER ? std::max (st.strokeMiter, 1.5) : 1.5));
		b.outset (k, k);
	}
	n->bounds = b;
	if (st.visible) markers (*n, p, st);
	// (the node's own coordinates so far; effects work there, the parent wants them mapped)
	effects (*n, el, st, box);
	n->bounds = m.mapRect (n->bounds);
	return n;
}

static sk_sp<SkData> base64 (const char *s)
{
	std::vector<uint8_t> o;
	unsigned acc = 0; int bits = 0;
	for (; *s; s++)
	{
		char c = *s;
		int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
		if (v < 0) continue;
		acc = acc << 6 | v; bits += 6;
		if (bits >= 8) { bits -= 8; o.push_back ((acc >> bits) & 255); }
	}
	return SkData::MakeWithCopy (o.data (), o.size ());
}

DRef SceneBuilder::build_image (XNode *el, const Style &st, const SkMatrix &m)
{
	const char *h = href_of (el);
	if (!h) return nullptr;
	sk_sp<SkData> data = image_data (h);
	if (!data) return nullptr;
	sk_sp<SkImage> img;
	sk_sp<SkPicture> pic;
	double picW = 0, picH = 0;
	const char *bytes = (const char *) data->data ();
	size_t blen = data->size ();
	size_t skip = 0;
	while (skip < blen && (bytes[skip] == ' ' || bytes[skip] == '\n' || bytes[skip] == '\r' || bytes[skip] == '\t' || (unsigned char) bytes[skip] >= 0xEF)) skip++;
	if (skip < blen && bytes[skip] == '<')
	{
		// an SVG in an image: drawn from its own, separate document
		if (stack.size () > 8) return nullptr;
		Document sub;
		if (!sub.load (bytes, blen, nullptr)) return nullptr;
		sub.path = doc->path;
		SceneBuilder sb (&sub);
		auto sc = sb.update ();
		SkPictureRecorder rec;
		SkCanvas *c = rec.beginRecording (SkRect::MakeWH ((float) sc->width, (float) sc->height));
		draw_scene (c, *sc);
		pic = rec.finishRecordingAsPicture ();
		picW = sc->width; picH = sc->height;
	}
	else
	{
		img = SkImages::DeferredFromEncodedData (data);
		if (!img) return nullptr;
		picW = img->width (); picH = img->height ();
	}
	double x = len_attr (el, "x", vpW, 16), y = len_attr (el, "y", vpH, 16);
	double w = len_attr (el, "width", vpW, 16, -1), h2 = len_attr (el, "height", vpH, 16, -1);
	double iw = picW, ih = picH;
	if (w < 0 && h2 < 0) { w = iw; h2 = ih; }
	else if (w < 0) w = iw * h2 / ih;
	else if (h2 < 0) h2 = ih * w / iw;
	if (w <= 0 || h2 <= 0) return nullptr;
	double vb[4] = { 0, 0, iw, ih };
	Affine t = viewbox_transform (vb, el->get ("preserveAspectRatio"), x, y, w, h2);
	auto n = std::make_shared<DNode> ();
	n->kind = D_IMAGE;
	n->uid = el->uid;
	n->image = img;
	n->picture = pic;
	n->m = m;
	n->dst = SkRect::MakeXYWH ((float) t.e, (float) t.f, (float) (iw * t.a), (float) (ih * t.d));
	n->hasClipRect = true;
	n->clipRect = SkRect::MakeXYWH ((float) x, (float) y, (float) w, (float) h2);
	n->bounds = n->clipRect;
	if (n->bounds.intersect (n->dst)) {}
	std::string ir = get_prop (el, "image-rendering");
	n->pixelated = ir == "pixelated" || ir == "optimizeSpeed" || ir == "crisp-edges";
	SkRect own = n->bounds;
	effects (*n, el, st, own);
	n->bounds = m.mapRect (n->bounds);
	return n;
}

// a reference to an image: inline, or a file next to the document
sk_sp<SkData> SceneBuilder::image_data (const char *h)
{
	if (!strncmp (h, "data:", 5))
	{
		const char *c = strchr (h, ',');
		if (!c) return nullptr;
		if (strstr (h, ";base64") && strstr (h, ";base64") < c) return base64 (c + 1);
		// (percent-encoded text)
		std::string o;
		for (const char *s = c + 1; *s; s++)
		{
			if (*s == '%' && s[1] && s[2]) { char b[3] = { s[1], s[2], 0 }; o += (char) strtol (b, nullptr, 16); s += 2; }
			else o += *s;
		}
		return SkData::MakeWithCopy (o.data (), o.size ());
	}
	std::string f = h;
	if (!strncmp (h, "file://", 7)) f = h + 7;
	if (f[0] != '/' && !strstr (f.c_str (), ":/") && !doc->path.empty ())
	{
		size_t sl = doc->path.rfind ('/');
		if (sl != std::string::npos) f = doc->path.substr (0, sl + 1) + f;
	}
	// ".." is taken out here: the volume's root is not above "SD0:"
	size_t colon = f.find (":/");
	std::string head = colon == std::string::npos ? "" : f.substr (0, colon + 2), rest = colon == std::string::npos ? f : f.substr (colon + 2);
	std::vector<std::string> parts;
	size_t a = 0;
	while (a <= rest.size ())
	{
		size_t e = rest.find ('/', a);
		if (e == std::string::npos) e = rest.size ();
		std::string w = rest.substr (a, e - a);
		if (w == "..") { if (!parts.empty ()) parts.pop_back (); }
		else if (!w.empty () && w != ".") parts.push_back (w);
		a = e + 1;
	}
	for (size_t i = 0; i < parts.size (); i++) { if (i) head += '/'; head += parts[i]; }
	return SkData::MakeFromFileName (head.c_str ());
}
sk_sp<SkImage> SceneBuilder::load_image (const char *href)
{
	sk_sp<SkData> d = image_data (href);
	return d ? SkImages::DeferredFromEncodedData (d) : nullptr;
}
sk_sp<SkPicture> SceneBuilder::picture_of (XNode *el, const SkRect &area)
{
	SkPictureRecorder rec;
	SkCanvas *c = rec.beginRecording (area);
	if (!stack.count (el))
	{
		stack.insert (el);
		inDefs++;
		if (DRef d = build (el, inherited_style (el), Affine (), true)) draw_node (c, *d);
		inDefs--;
		stack.erase (el);
	}
	return rec.finishRecordingAsPicture ();
}

static bool conditions_ok (const XNode *el)
{
	if (const char *r = el->get ("requiredExtensions")) if (*r) return false;
	if (const char *l = el->get ("systemLanguage"))
	{
		// "fr", "en", a comma separated list; empty matches nothing
		if (!*l) return false;
		const char *p = l;
		bool hit = false;
		while (*p)
		{
			while (*p == ' ' || *p == ',') p++;
			if ((p[0] == 'e' && p[1] == 'n') || (p[0] == 'f' && p[1] == 'r')) hit = true;
			while (*p && *p != ',') p++;
		}
		if (!hit) return false;
	}
	return true;
}

DRef SceneBuilder::build_group (XNode *el, const Style &st, const Affine &ctm, const SkMatrix &m, bool force)
{
	auto n = std::make_shared<DNode> ();
	n->uid = el->uid;
	n->m = m;
	Affine ct = ctm * Affine (m.getScaleX (), m.getSkewY (), m.getSkewX (), m.getScaleY (), m.getTranslateX (), m.getTranslateY ());
	bool isSwitch = named (el, "switch");
	SkRect u = SkRect::MakeEmpty ();
	for (XNode *k : el->kids)
	{
		if (!k->element ()) continue;
		if (isSwitch && !conditions_ok (k)) continue;
		if (DRef d = build (k, st, ct, force))
		{
			n->kids.push_back (d);
			u.join (d->bounds);
			if (isSwitch) break;
		}
		else if (isSwitch) break;
	}
	n->bounds = u;
	effects (*n, el, st, u);
	n->bounds = m.mapRect (n->bounds);
	return n;
}

DRef SceneBuilder::build_svg (XNode *el, const Style &st, const Affine &ctm, const SkMatrix &m, bool outer, bool force, double useW, double useH)
{
	double svW = vpW, svH = vpH;
	double x = 0, y = 0, w, h;
	bool isSymbol = named (el, "symbol");
	if (outer) { x = 0; y = 0; w = vpW; h = vpH; }
	else
	{
		if (!isSymbol) { x = len_attr (el, "x", vpW, st.fontSize); y = len_attr (el, "y", vpH, st.fontSize); }
		w = useW >= 0 ? useW : len_attr (el, "width", vpW, st.fontSize, vpW);
		h = useH >= 0 ? useH : len_attr (el, "height", vpH, st.fontSize, vpH);
	}
	auto n = std::make_shared<DNode> ();
	n->uid = el->uid;
	n->m = m;
	Affine vbm = Affine::translate (x, y);
	double vb[4];
	bool hasVb = parse_viewbox (el->get ("viewBox"), vb);
	if (hasVb) { vbm = viewbox_transform (vb, el->get ("preserveAspectRatio"), x, y, w, h); vpW = vb[2]; vpH = vb[3]; }
	else { vpW = w; vpH = h; }
	auto inner = std::make_shared<DNode> ();
	inner->uid = el->uid;
	inner->m = to_sk (vbm);
	Affine ct = ctm * Affine (m.getScaleX (), m.getSkewY (), m.getSkewX (), m.getScaleY (), m.getTranslateX (), m.getTranslateY ()) * vbm;
	SkRect u = SkRect::MakeEmpty ();
	for (XNode *k : el->kids)
		if (k->element ())
			if (DRef d = build (k, st, ct, force)) { inner->kids.push_back (d); u.join (d->bounds); }
	vpW = svW; vpH = svH;
	inner->bounds = inner->m.mapRect (u);
	if (!outer && !st.overflowVisible)
	{
		n->hasClipRect = true;
		n->clipRect = SkRect::MakeXYWH ((float) x, (float) y, (float) w, (float) h);
		if (!inner->bounds.intersect (n->clipRect)) inner->bounds = SkRect::MakeEmpty ();
	}
	n->kids.push_back (inner);
	n->bounds = inner->bounds;
	effects (*n, el, st, n->bounds);
	n->bounds = m.mapRect (n->bounds);
	return n;
}

DRef SceneBuilder::build_use (XNode *el, const Style &st, const Affine &ctm, const SkMatrix &m)
{
	const char *h = href_of (el);
	if (!h || h[0] != '#') return nullptr;
	usedIds.insert (h + 1);
	XNode *target = doc->by_id (h + 1);
	if (!target || stack.count (target)) return nullptr;
	stack.insert (target);
	inDefs++;
	const PaintSpec *sf = ctxFill, *ss = ctxStroke;
	ctxFill = &st.fill; ctxStroke = &st.stroke;
	double x = len_attr (el, "x", vpW, st.fontSize), y = len_attr (el, "y", vpH, st.fontSize);
	SkMatrix t = SkMatrix::Translate ((float) x, (float) y);
	auto n = std::make_shared<DNode> ();
	n->uid = el->uid;
	n->m = m;
	DRef d;
	Affine ct = ctm;
	if (named (target, "symbol") || named (target, "svg"))
	{
		Style ts;
		compute_style (target, st, sheet, ts);
		double w = len_attr (el, "width", vpW, st.fontSize, -1), h2 = len_attr (el, "height", vpH, st.fontSize, -1);
		d = build_svg (target, ts, ct, t, false, true, w, h2);
	}
	else
	{
		auto w = std::make_shared<DNode> ();
		w->m = t;
		if (DRef k = build (target, st, ct, true)) { w->kids.push_back (k); w->bounds = t.mapRect (k->bounds); }
		d = w;
	}
	ctxFill = sf; ctxStroke = ss;
	inDefs--;
	stack.erase (target);
	if (!d) return nullptr;
	n->kids.push_back (d);
	n->bounds = d->bounds;
	effects (*n, el, st, n->bounds);
	n->bounds = m.mapRect (n->bounds);
	return n;
}

void SceneBuilder::note_refs (XNode *) {}

void SceneBuilder::record (DRef n, XNode *el, const Affine &ctm)
{
	if (inDefs || !n || !cur) return;
	ObjInfo o;
	o.node = n;
	Box b (n->bounds.left (), n->bounds.top (), n->bounds.right (), n->bounds.bottom ());
	o.visual = ctm.apply (b);
	o.geom = o.visual;
	cur->objs[el->uid] = std::move (o);
}

DRef SceneBuilder::build (XNode *el, const Style &parent, const Affine &ctm, bool force)
{
	if (!el->element ()) return nullptr;
	if (!inDefs && !force && !dirty.count (el->uid))
	{
		auto f = memo.find (el->uid);
		if (f != memo.end ()) return f->second.node;	// (its line in objs came along with the table)
	}
	if (forced.count (el->uid)) force = true;
	const char *name = el_name (el);
	if (!conditions_ok (el) && !named (el->parent, "switch")) { if (!inDefs) memo[el->uid] = { nullptr }; return nullptr; }
	Style st;
	compute_style (el, parent, sheet, st);
	DRef n;
	if (st.display)
	{
		SkMatrix m = to_sk (element_transform (el, st));
		if (!strcmp (name, "g") || !strcmp (name, "a") || !strcmp (name, "switch")) n = build_group (el, st, ctm, m, force);
		else if (!strcmp (name, "svg")) n = build_svg (el, st, ctm, m, false, force, -1, -1);
		else if (!strcmp (name, "use")) n = build_use (el, st, ctm, m);
		else if (!strcmp (name, "image")) n = build_image (el, st, m);
		else if (!strcmp (name, "text"))
		{
			auto t = std::make_shared<DNode> ();
			t->kind = D_TEXT;
			t->uid = el->uid;
			t->m = m;
			layout_text (*this, el, st, *t);
			SkRect own = t->bounds;
			effects (*t, el, st, own);
			t->bounds = m.mapRect (t->bounds);
			n = t;
		}
		else n = build_shape (el, st, m, ctm);
	}
	if (!inDefs)
	{
		memo[el->uid] = { n };
		record (n, el, ctm);
	}
	return n;
}

// the transform attribute or property, around transform-origin
Affine SceneBuilder::element_transform (const XNode *el, const Style &st)
{
	std::string t = get_prop (el, "transform"), o = get_prop (el, "transform-origin");
	Affine m = parse_transform (t.c_str ());
	if (o.empty ()) return m;
	double v[2] = { 0, 0 };
	int n = 0;
	const char *p = o.c_str ();
	bool swapped = false;
	while (*p && n < 2)
	{
		while (*p == ' ') p++;
		if (!*p) break;
		const char *e = p;
		while (*e && *e != ' ') e++;
		std::string w (p, e - p);
		double pct = n == 0 ? vpW : vpH;
		if (w == "left") v[0] = 0, n = n == 0 ? 1 : n + 1;
		else if (w == "right") v[0] = vpW, n = n == 0 ? 1 : n + 1;
		else if (w == "top") { v[n == 0 ? 1 : 1] = 0; swapped = n == 0; n++; }
		else if (w == "bottom") { v[1] = vpH; swapped = n == 0; n++; }
		else if (w == "center") { double c = (swapped ? vpW : (n == 0 ? vpW : vpH)) / 2; v[swapped ? 0 : n] = c; n++; }
		else { bool ok; double d = parse_length (w.c_str (), pct, st.fontSize, &ok); if (ok) v[swapped ? 0 : n] = d; n++; }
		p = e;
	}
	if (n == 1 && !swapped) v[1] = vpH / 2;
	return Affine::translate (v[0], v[1]) * m * Affine::translate (-v[0], -v[1]);
}

// what an element in defs inherits from its own ancestors
Style SceneBuilder::inherited_style (const XNode *el)
{
	std::vector<const XNode *> chain;
	for (const XNode *p = el->parent; p && p->element () && p->name != "#top"; p = p->parent) chain.push_back (p);
	Style s;
	for (int i = (int) chain.size () - 1; i >= 0; i--) { Style o; compute_style (chain[i], s, sheet, o); s = o; }
	return s;
}

std::shared_ptr<const Scene> SceneBuilder::update ()
{
	init_codecs ();
	XNode *root = doc->root;
	auto next = std::make_shared<Scene> ();
	lastAll = false;
	// what changed: a node outside the scene (a gradient, a symbol), or one others point to, means everything
	bool all = !cur || doc->structure;
	dirty.clear (); forced.clear ();
	for (XNode *t : doc->touched)
	{
		if (!t) continue;
		const char *id = t->get ("id");
		if (!memo.count (t->uid) || (id && usedIds.count (id))) { all = true; break; }
		if (named (t, "style")) { all = true; break; }
		forced.insert (t->uid);
		for (XNode *p = t; p; p = p->parent) dirty.insert (p->uid);
	}
	if (all) { memo.clear (); usedIds.clear (); lastAll = true; }
	doc->touched.clear ();
	doc->structure = false;

	std::shared_ptr<Scene> prev = cur;
	cur = next;
	// the table of objects moves on: only what is rebuilt writes to it (the threads read nodes, never this table)
	Box dmg0;
	if (!all && prev)
		for (uint32_t u : forced) { auto f = prev->objs.find (u); if (f != prev->objs.end ()) dmg0.add (f->second.visual); }
	if (!all && prev) next->objs.swap (prev->objs);
	sheet.clear ();
	sheet.collect (root);

	Style base;
	Style st;
	compute_style (root, base, sheet, st);
	bool okW, okH;
	double w = parse_length (root->get ("width"), 100, 16, &okW), h = parse_length (root->get ("height"), 100, 16, &okH);
	double vb[4];
	bool hasVb = parse_viewbox (root->get ("viewBox"), vb);
	const char *ws = root->get ("width"), *hs = root->get ("height");
	bool pctW = ws && strchr (ws, '%'), pctH = hs && strchr (hs, '%');
	if (!okW || pctW) w = hasVb ? vb[2] : 100 * 96 / 25.4 * 2;
	if (!okH || pctH) h = hasVb ? vb[3] : 100 * 96 / 25.4 * 2;
	if (!okW && okH && hasVb) w = h * vb[2] / vb[3];
	if (okW && !okH && hasVb) h = w * vb[3] / vb[2];
	next->width = w; next->height = h;
	vpW = w; vpH = h;
	set_viewport_units (hasVb ? vb[2] : w, hasVb ? vb[3] : h);
	for (XNode *k : root->kids)
		if (named (k, "sodipodi:namedview"))
		{
			if (const char *c = k->get ("pagecolor")) parse_color (c, next->page);
			if (const char *c = k->get ("inkscape:pageopacity")) next->pageOpacity = (float) atof (c);
		}
	double zero[4] = { 0, 0, w, h };
	next->docToPx = hasVb ? viewbox_transform (vb, root->get ("preserveAspectRatio"), 0, 0, w, h) : viewbox_transform (zero, nullptr, 0, 0, w, h);
	bool force = all;
	// the kept objects' old boxes, for the damage
	Box dmg = dmg0;
	next->root = build_svg (root, st, Affine (), SkMatrix::I (), true, force, -1, -1);
	vpW = w; vpH = h;
	if (!all)
	{
		for (uint32_t u : forced) { auto f = next->objs.find (u); if (f != next->objs.end ()) dmg.add (f->second.visual); }
		lastDamage = dmg;
	}
	return next;
}

// ---- drawing ---------------------------------------------------------------------------------------------------------
static void draw_shape (SkCanvas *c, const DNode &n)
{
	for (int i = 0; i < 3; i++)
		switch (n.order[i])
		{
		case 0: if (n.hasFill) c->drawPath (n.path, n.fill); break;
		case 1: if (n.hasStroke) c->drawPath (n.path, n.stroke); break;
		default: for (auto &k : n.kids) draw_node (c, *k); break;
		}
}
static void draw_text (SkCanvas *c, const DNode &n)
{
	SkAutoCanvasRestore keep (c, n.vertical);
	if (n.vertical) c->rotate (90, n.verticalAt.fX, n.verticalAt.fY);
	for (auto &p : n.pieces)
		for (int i = 0; i < 2; i++)
		{
			int w = p.order[i] == 2 ? p.order[2] : p.order[i];
			if (w == 0 && p.hasFill) c->drawTextBlob (p.blob, p.at.fX, p.at.fY, p.fill);
			if (w == 1 && p.hasStroke) c->drawTextBlob (p.blob, p.at.fX, p.at.fY, p.stroke);
		}
	for (auto &k : n.kids) draw_node (c, *k);
}

static void draw_inner (SkCanvas *c, const DNode &n)
{
	switch (n.kind)
	{
	case D_SHAPE: draw_shape (c, n); break;
	case D_TEXT: draw_text (c, n); break;
	case D_IMAGE:
		if (n.picture)
		{
			SkAutoCanvasRestore keep (c, true);
			c->translate (n.dst.left (), n.dst.top ());
			SkRect cr = n.picture->cullRect ();
			c->scale (n.dst.width () / cr.width (), n.dst.height () / cr.height ());
			c->drawPicture (n.picture);
			break;
		}
		c->drawImageRect (n.image, n.dst, n.pixelated ? SkSamplingOptions (SkFilterMode::kNearest) : SkSamplingOptions (SkFilterMode::kLinear, SkMipmapMode::kLinear), nullptr);
		break;
	default:
		for (auto &k : n.kids) draw_node (c, *k);
	}
}

void draw_node (SkCanvas *c, const DNode &n)
{
	if (c->quickReject (n.bounds)) return;
	SkAutoCanvasRestore keep (c, true);
	if (!n.m.isIdentity ()) c->concat (n.m);
	if (n.hasClipRect) c->clipRect (n.clipRect, true);
	if (n.hasClip) c->clipPath (n.clip, true);
	bool layer = n.opacity < 1 || n.blend != SkBlendMode::kSrcOver || n.filter || n.mask;
	if (!layer) { draw_inner (c, n); return; }
	SkPaint lp;
	lp.setAlphaf (n.opacity);
	lp.setBlendMode (n.blend);
	lp.setImageFilter (n.filter);
	c->saveLayer (nullptr, &lp);
	draw_inner (c, n);
	if (n.mask)
	{
		SkPaint mp;
		mp.setBlendMode (SkBlendMode::kDstIn);
		mp.setColorFilter (SkLumaColorFilter::Make ());
		if (n.mask->pixelated) mp.setColorFilter (nullptr);	// (mask-type: alpha)
		c->saveLayer (nullptr, &mp);
		c->clipRect (n.mask->dst, true);
		if (n.mask->mask)
		{
			// the mask's own mask: one layer more, kept in the inner one
			SkPaint q;
			c->saveLayer (nullptr, &q);
			c->save ();
			c->concat (n.mask->m);
			for (auto &k : n.mask->kids) draw_node (c, *k);
			c->restore ();
			SkPaint mq;
			mq.setBlendMode (SkBlendMode::kDstIn);
			if (!n.mask->mask->pixelated) mq.setColorFilter (SkLumaColorFilter::Make ());
			c->saveLayer (nullptr, &mq);
			c->concat (n.mask->mask->m);
			for (auto &k : n.mask->mask->kids) draw_node (c, *k);
			c->restore ();
			c->restore ();
		}
		else
		{
			c->concat (n.mask->m);
			for (auto &k : n.mask->kids) draw_node (c, *k);
		}
		c->restore ();
	}
	c->restore ();
}

void draw_scene (SkCanvas *c, const Scene &s)
{
	if (s.root) draw_node (c, *s.root);
}

// the view's Outlines mode: every path a hairline, no paint, no effect
static void outline_node (SkCanvas *c, const DNode &n, const SkPaint &p)
{
	if (c->quickReject (n.bounds)) return;
	SkAutoCanvasRestore keep (c, true);
	if (!n.m.isIdentity ()) c->concat (n.m);
	if (n.kind == D_SHAPE) c->drawPath (n.path, p);
	else if (n.kind == D_IMAGE) c->drawRect (n.dst, p);
	else if (n.kind == D_TEXT) for (auto &t : n.pieces) c->drawTextBlob (t.blob, t.at.fX, t.at.fY, p);
	if (n.kind != D_SHAPE) for (auto &k : n.kids) outline_node (c, *k, p);
}
void draw_outlines (SkCanvas *c, const Scene &s)
{
	SkPaint p;
	p.setAntiAlias (true);
	p.setStyle (SkPaint::kStroke_Style);
	p.setStrokeWidth (0);
	p.setColor (SK_ColorBLACK);
	if (s.root) outline_node (c, *s.root, p);
}

}
