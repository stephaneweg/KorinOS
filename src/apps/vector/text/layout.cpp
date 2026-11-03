//
// layout.cpp -- SVG text: the characters of <text> and its <tspan> and <textPath> gathered with what applies to each,
// chunks by absolute position, shaping by HarfBuzz (no ICU: its own Unicode tables), glyphs handed to Skia by id.
//
#include "../render/scene.h"
#include "fonts.h"
#include "include/core/SkContourMeasure.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRSXform.h"
#include "include/core/SkStream.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include <hb-ot.h>
#include <hb.h>
#include <cstring>
#include <map>

using namespace vd;

namespace vr {

// ---- fonts ----------------------------------------------------------------------------------------------------------
struct HbFace { hb_face_t *face; hb_font_t *font; int upem; };

static HbFace *hb_for (const sk_sp<SkTypeface> &tf)
{
	static std::map<uint32_t, HbFace *> faces;
	auto f = faces.find (tf->uniqueID ());
	if (f != faces.end ()) return f->second;
	int ttc = 0;
	std::unique_ptr<SkStreamAsset> s (tf->openStream (&ttc));
	HbFace *h = new HbFace ();
	if (s)
	{
		size_t n = s->getLength ();
		char *mem = (char *) malloc (n);
		s->read (mem, n);
		hb_blob_t *blob = hb_blob_create (mem, (unsigned) n, HB_MEMORY_MODE_READONLY, mem, free);
		h->face = hb_face_create (blob, ttc);
		hb_blob_destroy (blob);
	}
	else h->face = hb_face_get_empty ();
	h->font = hb_font_create (h->face);
	hb_ot_font_set_funcs (h->font);
	h->upem = hb_face_get_upem (h->face);
	if (h->upem <= 0) h->upem = 1000;
	hb_font_set_scale (h->font, h->upem, h->upem);
	faces[tf->uniqueID ()] = h;
	return h;
}

static sk_sp<SkTypeface> find_face (const Style &st)
{
	static std::map<std::string, sk_sp<SkTypeface>> cache;
	char key[32];
	snprintf (key, sizeof key, "|%d|%d|%d", st.fontWeight, st.fontStretch, st.fontItalic);
	std::string k = st.fontFamily + key;
	auto f = cache.find (k);
	if (f != cache.end ()) return f->second;
	sk_sp<SkTypeface> tf;
	const char *p = st.fontFamily.c_str ();
	bool it = st.fontItalic != 0;
	while (*p && !tf)
	{
		while (*p == ' ' || *p == ',' || *p == '"' || *p == '\'') p++;
		std::string name;
		while (*p && *p != ',') name += *p++;
		while (!name.empty () && (name.back () == ' ' || name.back () == '"' || name.back () == '\'')) name.pop_back ();
		if (!name.empty ()) tf = font_match (name.c_str (), st.fontWeight, st.fontStretch, it);
	}
	if (!tf) tf = font_match ("DejaVu Sans", st.fontWeight, st.fontStretch, it);
	cache[k] = tf;
	return tf;
}
static sk_sp<SkTypeface> fallback_for (const Style &st, uint32_t cp) { return font_for_char (cp, st.fontWeight, st.fontItalic != 0); }

static void font_of (const sk_sp<SkTypeface> &tf, double size, SkFont &f)
{
	f = SkFont (tf, (float) size);
	f.setSubpixel (true);
	f.setLinearMetrics (true);
	f.setHinting (SkFontHinting::kNone);
	f.setEdging (SkFont::Edging::kAntiAlias);
}

// ---- gathering ------------------------------------------------------------------------------------------------------
namespace {
struct Ch
{
	uint32_t cp;
	int span;
	bool abs = false;		// starts a chunk
	double x = NAN, y = NAN, dx = 0, dy = 0, rot = NAN;
	bool sp = false;
};
struct Span { Style st; XNode *path = nullptr; double startOffset = 0; };

struct Lists { std::vector<double> x, y, dx, dy, rot; size_t pos = 0; };

struct Gather
{
	SceneBuilder &b;
	std::vector<Ch> ch;
	std::vector<Span> spans;
	bool lastSpace = true;

	static std::vector<double> lengths (const char *s, const Style &st, double pct)
	{
		std::vector<double> o;
		if (!s) return o;
		while (*s)
		{
			while (*s == ' ' || *s == ',' || *s == '\n' || *s == '\t' || *s == '\r') s++;
			if (!*s) break;
			bool ok;
			double v = parse_length (s, pct, st.fontSize, &ok);
			if (!ok) break;
			o.push_back (v);
			while (*s && *s != ' ' && *s != ',' && *s != '\n' && *s != '\t') s++;
		}
		return o;
	}
	void walk (XNode *el, const Style &st, std::vector<Lists *> lists, bool preserve, XNode *tp, double off)
	{
		Lists own;
		own.x = lengths (el->get ("x"), st, b.vpW);
		own.y = lengths (el->get ("y"), st, b.vpH);
		own.dx = lengths (el->get ("dx"), st, b.vpW);
		own.dy = lengths (el->get ("dy"), st, b.vpH);
		own.rot = parse_numbers (el->get ("rotate"));
		lists.push_back (&own);
		if (const char *sp = el->get ("xml:space")) preserve = !strcmp (sp, "preserve");
		spans.push_back ({ st, tp, off });
		int spanIdx = (int) spans.size () - 1;
		for (XNode *k : el->kids)
		{
			if (k->type == X_TEXT || k->type == X_CDATA)
			{
				const unsigned char *s = (const unsigned char *) k->text.c_str ();
				while (*s)
				{
					uint32_t c = *s++;
					if (c >= 0xF0) { c = (c & 7) << 18 | (s[0] & 63) << 12 | (s[1] & 63) << 6 | (s[2] & 63); s += 3; }
					else if (c >= 0xE0) { c = (c & 15) << 12 | (s[0] & 63) << 6 | (s[1] & 63); s += 2; }
					else if (c >= 0xC0) { c = (c & 31) << 6 | (s[0] & 63); s += 1; }
					bool sp = c == ' ' || c == '\n' || c == '\t' || c == '\r';
					if (!preserve)
					{
						if (c == '\n' || c == '\r') continue;
						if (sp) { if (lastSpace) continue; c = ' '; }
					}
					else if (sp) c = ' ';
					lastSpace = sp;
					Ch q;
					q.cp = c; q.span = spanIdx; q.sp = sp;
					// the value at this character: the innermost list that has one
					for (int l = (int) lists.size () - 1; l >= 0; l--)
					{
						Lists *t = lists[l];
						if (t->pos < t->x.size () && std::isnan (q.x)) q.x = t->x[t->pos];
						if (t->pos < t->y.size () && std::isnan (q.y)) q.y = t->y[t->pos];
						if (t->pos < t->dx.size () && q.dx == 0) q.dx = t->dx[t->pos];
						if (t->pos < t->dy.size () && q.dy == 0) q.dy = t->dy[t->pos];
						if (t->rot.size () && std::isnan (q.rot)) q.rot = t->rot[std::min (t->pos, t->rot.size () - 1)];
					}
					for (Lists *t : lists) t->pos++;
					q.abs = !std::isnan (q.x) || !std::isnan (q.y);
					ch.push_back (q);
				}
			}
			else if (k->element ())
			{
				const char *n = el_name (k);
				Style ks;
				compute_style (k, st, b.sheet, ks);
				if (!ks.display) continue;
				if (!strcmp (n, "tref"))
				{
					const char *h = k->get ("href");
					if (!h) h = k->get ("xlink:href");
					XNode *t = h && h[0] == '#' ? b.doc->by_id (h + 1) : nullptr;
					if (t)
					{
						XNode tmp;
						tmp.type = X_ELEMENT; tmp.name = "tspan";
						XNode *tx = x_new (X_TEXT, "");
						tx->text = t->text_content ();
						tmp.kids.push_back (tx);
						tx->parent = &tmp;
						walk (&tmp, ks, lists, preserve, tp, off);
						tmp.kids.clear ();
						delete tx;
						spans.push_back ({ st, tp, off });
						spanIdx = (int) spans.size () - 1;
					}
				}
				else if (!strcmp (n, "tspan") || !strcmp (n, "a"))
				{
					walk (k, ks, lists, preserve, tp, off);
					spans.push_back ({ st, tp, off });
					spanIdx = (int) spans.size () - 1;
				}
				else if (!strcmp (n, "textPath"))
				{
					const char *h = k->get ("href");
					if (!h) h = k->get ("xlink:href");
					XNode *pathEl = h && h[0] == '#' ? b.doc->by_id (h + 1) : nullptr;
					double so = 0;
					if (const char *o = k->get ("startOffset"))
					{
						double total = 0;
						if (pathEl && strchr (o, '%')) total = -1;
						so = total < 0 ? -atof (o) / 100 - 1e-9 : parse_length (o, 100, ks.fontSize);
					}
					walk (k, ks, lists, preserve, pathEl, so);
					spans.push_back ({ st, tp, off });
					spanIdx = (int) spans.size () - 1;
				}
			}
		}
		lists.pop_back ();
		
	}
};

struct Placed { uint16_t glyph; float x, y, rot; bool hasRot; int ci; float stretch = 1; };
struct Run { sk_sp<SkTypeface> tf; int span; std::vector<Placed> g; };

static void utf8 (std::string &o, uint32_t c)
{
	if (c < 0x80) o += (char) c;
	else if (c < 0x800) { o += (char) (0xC0 | c >> 6); o += (char) (0x80 | (c & 63)); }
	else if (c < 0x10000) { o += (char) (0xE0 | c >> 12); o += (char) (0x80 | ((c >> 6) & 63)); o += (char) (0x80 | (c & 63)); }
	else { o += (char) (0xF0 | c >> 18); o += (char) (0x80 | ((c >> 12) & 63)); o += (char) (0x80 | ((c >> 6) & 63)); o += (char) (0x80 | (c & 63)); }
}
}

static void parse_features (const std::string &s, std::vector<hb_feature_t> &o)
{
	const char *p = s.c_str ();
	while (*p)
	{
		while (*p == ' ' || *p == ',') p++;
		std::string one;
		while (*p && *p != ',') { if (*p != '"' && *p != '\'') one += *p; p++; }
		hb_feature_t f;
		// CSS: "liga" 0 -> HarfBuzz: liga=0
		size_t sp = one.find (' ');
		if (sp != std::string::npos) one[sp] = '=';
		if (hb_feature_from_string (one.c_str (), -1, &f)) o.push_back (f);
	}
}

// shapes ch[a, b) of one span with one typeface; pen advances
static void shape_run (const std::vector<Ch> &ch, int a, int b, const Style &st, const sk_sp<SkTypeface> &tf, std::vector<double> &adv, Run &r, double &pen)
{
	HbFace *h = hb_for (tf);
	hb_buffer_t *buf = hb_buffer_create ();
	for (int i = a; i < b; i++) hb_buffer_add (buf, ch[i].cp, i);
	hb_buffer_set_content_type (buf, HB_BUFFER_CONTENT_TYPE_UNICODE);
	hb_buffer_guess_segment_properties (buf);
	if (st.direction == 1) hb_buffer_set_direction (buf, HB_DIRECTION_RTL);
	std::vector<hb_feature_t> fe;
	if (st.letterSpacing != 0) { hb_feature_t f; hb_feature_from_string ("liga=0", -1, &f); fe.push_back (f); hb_feature_from_string ("clig=0", -1, &f); fe.push_back (f); }
	parse_features (st.fontFeatures, fe);
	if (st.noKerning) { hb_feature_t f; hb_feature_from_string ("kern=0", -1, &f); fe.push_back (f); }
	if (st.smallCaps) { hb_feature_t f; hb_feature_from_string ("smcp", -1, &f); fe.push_back (f); }
	hb_shape (h->font, buf, fe.data (), (unsigned) fe.size ());
	unsigned n;
	hb_glyph_info_t *info = hb_buffer_get_glyph_infos (buf, &n);
	hb_glyph_position_t *pos = hb_buffer_get_glyph_positions (buf, &n);
	double k = st.fontSize / h->upem;
	for (unsigned i = 0; i < n; i++)
	{
		Placed p;
		p.glyph = (uint16_t) info[i].codepoint;
		p.ci = (int) info[i].cluster;
		p.x = (float) (pen + pos[i].x_offset * k);
		p.y = (float) (-pos[i].y_offset * k);
		p.rot = 0; p.hasRot = false;
		double a2 = pos[i].x_advance * k;
		// the first glyph of a cluster takes the spacing
		if (i + 1 == n || info[i + 1].cluster != info[i].cluster)
		{
			a2 += st.letterSpacing;
			if (ch[p.ci].cp == ' ') a2 += st.wordSpacing;
		}
		pen += a2;
		adv[p.ci] += a2;
		r.g.push_back (p);
	}
	hb_buffer_destroy (buf);
}

SkPath text_outline (const std::vector<TextPiece> &pieces)
{
	SkPathBuilder b;
	for (auto &t : pieces)
	{
		SkFont f;
		font_of (t.face, t.size, f);
		for (size_t i = 0; i < t.glyphs.size (); i++)
		{
			std::optional<SkPath> g = f.getPath (t.glyphs[i]);
			if (!g) continue;
			const SkRSXform &x = t.xf[i];
			SkMatrix m = SkMatrix::MakeAll (x.fSCos, -x.fSSin, x.fTx, x.fSSin, x.fSCos, x.fTy, 0, 0, 1);
			b.addPath (g->makeTransform (m));
		}
	}
	return b.detach ();
}

void layout_text (SceneBuilder &b, XNode *el, const Style &st, DNode &n)
{
	Gather g { b };
	g.walk (el, st, {}, false, nullptr, 0);
	// (a trailing space goes, when collapsing)
	if (!g.ch.empty () && g.ch.back ().sp && !el->get ("xml:space")) g.ch.pop_back ();
	if (g.ch.empty ()) return;
	int cn = (int) g.ch.size ();
	std::vector<double> adv (cn, 0);

	// chunks
	struct Chunk { int a, b; };
	std::vector<Chunk> chunks;
	int start = 0;
	for (int i = 1; i <= cn; i++)
		if (i == cn || g.ch[i].abs) { chunks.push_back ({ start, i }); start = i; }

	double cx = 0, cy = 0;
	SkRect bbox = SkRect::MakeEmpty ();
	std::vector<Run> done;
	std::vector<std::pair<int, SkRect>> decos;
	for (auto &c : chunks)
	{
		if (!std::isnan (g.ch[c.a].x)) cx = g.ch[c.a].x;
		if (!std::isnan (g.ch[c.a].y)) cy = g.ch[c.a].y;
		// runs: one span, one typeface
		std::vector<Run> runs;
		double pen = 0;
		int i = c.a;
		const Style *first = &g.spans[g.ch[c.a].span].st;
		while (i < c.b)
		{
			int sp = g.ch[i].span, j = i;
			const Style &ss = g.spans[sp].st;
			sk_sp<SkTypeface> tf = find_face (ss);
			SkFont test;
			font_of (tf, ss.fontSize, test);
			uint16_t gid = 0;
			SkUnichar uc = (SkUnichar) g.ch[i].cp;
			tf->unicharsToGlyphs (SkSpan<const SkUnichar> (&uc, 1), SkSpan<uint16_t> (&gid, 1));
			if (gid == 0 && !g.ch[i].sp)
				if (auto fb = fallback_for (ss, g.ch[i].cp)) tf = fb;
			while (j < c.b && g.ch[j].span == sp)
			{
				if (j > i)
				{
					SkUnichar u2 = (SkUnichar) g.ch[j].cp;
					uint16_t g2 = 0;
					tf->unicharsToGlyphs (SkSpan<const SkUnichar> (&u2, 1), SkSpan<uint16_t> (&g2, 1));
					if (g2 == 0 && !g.ch[j].sp && u2 >= 0x300) break;
				}
				j++;
			}
			Run r;
			r.tf = tf; r.span = sp;
			shape_run (g.ch, i, j, ss, tf, adv, r, pen);
			runs.push_back (std::move (r));
			i = j;
		}
		// dx, dy, rotate: per character, cumulative
		std::vector<double> shiftX (cn + 1, 0), shiftY (cn + 1, 0);
		double sx = 0, sy = 0;
		for (int k = c.a; k < c.b; k++)
		{
			sx += g.ch[k].dx; sy += g.ch[k].dy;
			shiftX[k] = sx; shiftY[k] = sy;
		}
		// inline-size: lines broken at spaces; the next line starts under the first (it comes as dy on the glyphs)
		if (st.inlineSize > 0)
		{
			std::vector<double> at (cn + 1, -1);
			for (auto &r : runs) for (auto &p : r.g) if (at[p.ci] < 0 || p.x < at[p.ci]) at[p.ci] = p.x;
			double lineStart = 0, lh = st.fontSize * st.lineHeight, widest = 0, lineEnd = 0;
			int line = 0, lastSpace = -1;
			std::vector<double> shift (cn + 1, 0);
			std::vector<int> lineOf (cn + 1, 0);
			for (int k = c.a; k < c.b; k++)
			{
				if (at[k] < 0) at[k] = lineEnd;
				if (g.ch[k].cp == ' ') lastSpace = k;
				double end = at[k] + adv[k] - lineStart;
				if (end > st.inlineSize && lastSpace > 0 && at[lastSpace + 1] > lineStart)
				{
					widest = std::max (widest, at[lastSpace] - lineStart);
					lineStart = at[lastSpace + 1];
					line++;
					for (int q = lastSpace + 1; q <= k; q++) lineOf[q] = line;
					lastSpace = -1;
				}
				lineOf[k] = line;
				shift[k] = lineStart;
				lineEnd = at[k] + adv[k];
			}
			for (int k = c.a; k < c.b; k++) { int lp = lineOf[k]; double ls = 0; for (int q = c.a; q < c.b; q++) if (lineOf[q] == lp) { ls = at[q]; break; } shift[k] = ls; }
			for (auto &r : runs) for (auto &p : r.g) { p.x -= (float) shift[p.ci]; p.y += (float) (lineOf[p.ci] * lh); }
			if (line) pen = std::max (widest, lineEnd - lineStart);
		}
		// textLength: spacing (between characters) or spacingAndGlyphs (the run stretched)
		if (const char *tl = el->get ("textLength"))
		{
			bool ok;
			double want = parse_length (tl, b.vpW, st.fontSize, &ok);
			const char *la = el->get ("lengthAdjust");
			int cnt = c.b - c.a;
			if (ok && want > 0 && pen > 0 && cnt > 1 && chunks.size () == 1)
			{
				if (la && !strcmp (la, "spacingAndGlyphs"))
				{
					double k = want / pen;
					for (auto &r : runs) for (auto &p : r.g) { p.x = (float) (p.x * k); p.stretch = (float) k; }
					pen = want;
				}
				else
				{
					double extra = (want - pen) / (cnt - 1);
					for (auto &r : runs) for (auto &p : r.g) p.x += (float) (extra * (p.ci - c.a));
					pen = want;
				}
			}
		}
		double anchor = 0, total = pen + sx;
		if (first->textAnchor == TA_MIDDLE) anchor = -total / 2;
		else if (first->textAnchor == TA_END) anchor = -total;
		for (auto &r : runs)
		{
			const Span &sp = g.spans[r.span];
			const Style &ss = sp.st;
			SkFont f;
			font_of (r.tf, ss.fontSize, f);
			SkFontMetrics fm;
			f.getMetrics (&fm);
			double base = -ss.baselineShift;
			switch (ss.baseline)
			{
			case DB_MIDDLE: base += -fm.fXHeight / 2; break;
			case DB_CENTRAL: base += (-fm.fAscent - fm.fDescent) / 2; break;
			case DB_HANGING: base += -fm.fAscent * 0.8; break;
			case DB_MATHEMATICAL: base += -fm.fAscent * 0.5; break;
			case DB_TEXT_BEFORE: base += -fm.fAscent; break;
			case DB_TEXT_AFTER: base += -fm.fDescent; break;
			case DB_IDEOGRAPHIC: base += -fm.fDescent; break;
			default: break;
			}
			for (auto &p : r.g)
			{
				p.x = (float) (p.x + shiftX[p.ci] + anchor + cx);
				p.y = (float) (p.y + shiftY[p.ci] + base + cy);
				if (!std::isnan (g.ch[p.ci].rot)) { p.rot = (float) g.ch[p.ci].rot; p.hasRot = p.rot != 0; }
			}
			double x0 = cx + anchor, y0 = cy + base;
			(void) x0; (void) y0;
		}
		// on a path
		for (auto &r : runs)
		{
			const Span &sp = g.spans[r.span];
			if (!sp.path) continue;
			vg::Path vp = vg::parse_d (sp.path->get ("d"));
			if (vp.empty ()) continue;
			SkPath skp = to_skpath (vp, false);
			if (const char *t = sp.path->get ("transform")) skp = skp.makeTransform (to_sk (parse_transform (t)));
			SkContourMeasureIter it (skp, false);
			sk_sp<SkContourMeasure> cm = it.next ();
			if (!cm) continue;
			double off = sp.startOffset < 0 ? (-sp.startOffset) * cm->length () : sp.startOffset;
			for (auto &p : r.g)
			{
				SkPoint q, t;
				float at = (float) (p.x + off);
				if (at < 0 || at > cm->length () || !cm->getPosTan (at, &q, &t)) { p.glyph = 0xFFFF; continue; }
				p.x = q.fX; p.y = q.fY;
				p.rot = (float) (std::atan2 (t.fY, t.fX) * 180 / M_PI); p.hasRot = true;
			}
		}
		// a decoration: a band along each run (it goes under the glyphs; line-through over: same pass here)
		for (auto &r : runs)
		{
			const Style &ss = g.spans[r.span].st;
			int deco = ss.textDeco | ss.textDecoInherit;
			if (!deco || r.g.empty ()) continue;
			SkFont f;
			font_of (r.tf, ss.fontSize, f);
			SkFontMetrics fm;
			f.getMetrics (&fm);
			float x0 = 1e30f, x1 = -1e30f;
			for (auto &p : r.g) { x0 = std::min (x0, p.x); x1 = std::max (x1, p.x); }
			// the end of the last glyph
			SkRect gb;
			uint16_t last = r.g.back ().glyph;
			float adv = 0;
			f.getWidth (last);
			adv = f.getWidth (last);
			(void) gb;
			x1 += adv;
			float th = fm.fUnderlineThickness > 0 ? fm.fUnderlineThickness : (float) ss.fontSize / 16;
			float yb = r.g[0].y;
			if (deco & 1) decos.push_back ({ r.span, SkRect::MakeLTRB (x0, yb + fm.fUnderlinePosition, x1, yb + fm.fUnderlinePosition + th) });
			if (deco & 2) decos.push_back ({ r.span, SkRect::MakeLTRB (x0, yb + fm.fAscent, x1, yb + fm.fAscent + th) });
			if (deco & 4) decos.push_back ({ r.span, SkRect::MakeLTRB (x0, yb - fm.fXHeight / 2 - th / 2, x1, yb - fm.fXHeight / 2 + th / 2) });
		}
		cx += total + anchor * 0; cy += sy;
		for (auto &r : runs) done.push_back (std::move (r));
	}

	// the blobs: one per run
	struct Out { sk_sp<SkTextBlob> blob; int span; sk_sp<SkTypeface> face; std::vector<uint16_t> glyphs; std::vector<SkRSXform> xf; };
	std::vector<Out> outs;
	for (auto &r : done)
	{
		const Style &ss = g.spans[r.span].st;
		SkFont f;
		font_of (r.tf, ss.fontSize, f);
		bool anyRot = false;
		int cnt = 0;
		for (auto &p : r.g) { if (p.hasRot) anyRot = true; if (p.glyph != 0xFFFF) cnt++; }
		if (!r.g.empty () && r.g[0].stretch != 1) f.setScaleX (r.g[0].stretch);
		if (!cnt) continue;
		SkTextBlobBuilder tb;
		if (anyRot)
		{
			auto &rb = tb.allocRunRSXform (f, cnt);
			int w = 0;
			for (auto &p : r.g)
			{
				if (p.glyph == 0xFFFF) continue;
				rb.glyphs[w] = p.glyph;
				double a = p.rot * M_PI / 180;
				rb.xforms ()[w] = SkRSXform::Make ((float) std::cos (a), (float) std::sin (a), p.x, p.y);
				w++;
			}
		}
		else
		{
			auto &rb = tb.allocRunPos (f, cnt);
			int w = 0;
			for (auto &p : r.g)
			{
				if (p.glyph == 0xFFFF) continue;
				rb.glyphs[w] = p.glyph;
				rb.points ()[w] = { p.x, p.y };
				w++;
			}
		}
		sk_sp<SkTextBlob> blob = tb.make ();
		if (!blob) continue;
		Out ou { blob, r.span, r.tf, {}, {} };
		for (auto &p : r.g)
		{
			if (p.glyph == 0xFFFF) continue;
			double a = p.rot * M_PI / 180;
			ou.glyphs.push_back (p.glyph);
			ou.xf.push_back (SkRSXform::Make ((float) std::cos (a), (float) std::sin (a), p.x, p.y));
		}
		outs.push_back (std::move (ou));
		bbox.join (blob->bounds ());
	}
	// the paints, now that the box is known (a gradient spans the whole text)
	for (auto &o : outs)
	{
		const Style &ss = g.spans[o.span].st;
		TextPiece p;
		p.blob = o.blob;
		p.face = o.face; p.size = (float) ss.fontSize; p.glyphs = o.glyphs; p.xf = o.xf;
		memcpy (p.order, ss.paintOrder, 3);
		if (ss.visible)
		{
			p.hasFill = b.resolve_paint (ss.fill, ss.fillOpacity, ss, bbox, p.fill);
			p.hasStroke = ss.strokeWidth > 0 && b.resolve_paint (ss.stroke, ss.strokeOpacity, ss, bbox, p.stroke);
			if (p.hasStroke) b.setup_stroke (ss, p.stroke);
		}
		SkRect pb = o.blob->bounds ();
		if (p.hasStroke) { float k = (float) ss.strokeWidth; pb.outset (k, k); }
		n.bounds.join (pb);
		n.pieces.push_back (std::move (p));
	}
	for (auto &d : decos)
	{
		const Style &ss = g.spans[d.first].st;
		auto q = std::make_shared<DNode> ();
		q->kind = D_SHAPE;
		q->path = SkPath::Rect (d.second);
		q->bounds = d.second;
		if (ss.visible)
		{
			q->hasFill = b.resolve_paint (ss.fill, ss.fillOpacity, ss, bbox, q->fill);
			q->hasStroke = ss.strokeWidth > 0 && b.resolve_paint (ss.stroke, ss.strokeOpacity, ss, bbox, q->stroke);
			if (q->hasStroke) b.setup_stroke (ss, q->stroke);
		}
		n.bounds.join (d.second);
		n.kids.push_back (q);
	}
	// vertical writing: the lines turn around the start point (glyphs lying on their side)
	if (st.writingMode == 1 && !n.pieces.empty ())
	{
		float sx = 0, sy = 0;
		if (!g.ch.empty ()) { sx = (float) (std::isnan (g.ch[0].x) ? 0 : g.ch[0].x); sy = (float) (std::isnan (g.ch[0].y) ? 0 : g.ch[0].y); }
		n.vertical = true;
		n.verticalAt = { sx, sy };
		SkMatrix m = SkMatrix::RotateDeg (90, { sx, sy });
		n.bounds = m.mapRect (n.bounds);
	}
}

}
