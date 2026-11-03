//
// filter.cpp -- an SVG <filter> as a graph of Skia image filters (Filter Effects Module). Each primitive's result has a
// name; "in" reads one of them, or SourceGraphic, or SourceAlpha. Colour work happens in linearRGB unless the primitive
// says sRGB: the colour filters of Skia convert at both ends.
//
#include "scene.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkShader.h"
#include "include/effects/SkBlenders.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkPerlinNoiseShader.h"
#include <cstring>
#include <map>
#include <string>

using namespace vd;

namespace vr {

using F = sk_sp<SkImageFilter>;

static sk_sp<SkColorFilter> to_linear () { return SkColorFilters::SRGBToLinearGamma (); }
static sk_sp<SkColorFilter> to_srgb () { return SkColorFilters::LinearToSRGBGamma (); }

static double frac_or_abs (const char *s, double base, double off, double def, bool box)
{
	if (!s) return def;
	char *e;
	double v = strtod (s, &e);
	if (e == s) return def;
	if (!box) return parse_length (s, base, 16);
	if (*e == '%') v /= 100;
	return off + v * base;
}

static SkBlendMode mode_of (const char *m)
{
	if (!m) return SkBlendMode::kSrcOver;
	static const struct { const char *n; SkBlendMode b; } T[] = {
		{"multiply",SkBlendMode::kMultiply},{"screen",SkBlendMode::kScreen},{"overlay",SkBlendMode::kOverlay},{"darken",SkBlendMode::kDarken},
		{"lighten",SkBlendMode::kLighten},{"color-dodge",SkBlendMode::kColorDodge},{"color-burn",SkBlendMode::kColorBurn},{"hard-light",SkBlendMode::kHardLight},
		{"soft-light",SkBlendMode::kSoftLight},{"difference",SkBlendMode::kDifference},{"exclusion",SkBlendMode::kExclusion},{"hue",SkBlendMode::kHue},
		{"saturation",SkBlendMode::kSaturation},{"color",SkBlendMode::kColor},{"luminosity",SkBlendMode::kLuminosity} };
	for (auto &t : T) if (!strcmp (m, t.n)) return t.b;
	return SkBlendMode::kSrcOver;
}

// a component transfer's table
static void transfer (const XNode *fn, uint8_t t[256])
{
	for (int i = 0; i < 256; i++) t[i] = (uint8_t) i;
	if (!fn) return;
	const char *type = fn->get2 ("type", "identity");
	auto clampb = [] (double v) { return (uint8_t) (v < 0 ? 0 : v > 1 ? 255 : std::lround (v * 255)); };
	if (!strcmp (type, "table") || !strcmp (type, "discrete"))
	{
		auto v = parse_numbers (fn->get ("tableValues"));
		if (v.empty ()) return;
		int n = (int) v.size ();
		bool disc = type[0] == 'd';
		for (int i = 0; i < 256; i++)
		{
			double c = i / 255.0;
			if (disc) { int k = std::min ((int) (c * n), n - 1); t[i] = clampb (v[k]); }
			else
			{
				if (n == 1) { t[i] = clampb (v[0]); continue; }
				int k = std::min ((int) (c * (n - 1)), n - 2);
				double f = c * (n - 1) - k;
				t[i] = clampb (v[k] + f * (v[k + 1] - v[k]));
			}
		}
	}
	else if (!strcmp (type, "linear"))
	{
		double sl = atof (fn->get2 ("slope", "1")), in = atof (fn->get2 ("intercept", "0"));
		for (int i = 0; i < 256; i++) t[i] = clampb (sl * i / 255.0 + in);
	}
	else if (!strcmp (type, "gamma"))
	{
		double am = atof (fn->get2 ("amplitude", "1")), ex = atof (fn->get2 ("exponent", "1")), of = atof (fn->get2 ("offset", "0"));
		for (int i = 0; i < 256; i++) t[i] = clampb (am * std::pow (i / 255.0, ex) + of);
	}
}

// the CSS list: blur (2px) drop-shadow (...) url (#f) ... each one on the result of the one before (Filter Effects, 11)
sk_sp<SkImageFilter> filter_functions (SceneBuilder &b, const Style &st, const SkRect &bbox, SkRect *region)
{
	const char *p = st.filterFunc.c_str ();
	F cur;
	SkRect r = bbox;
	bool any = false;
	while (*p)
	{
		while (*p == ' ' || *p == ',') p++;
		if (!*p) break;
		std::string name;
		while (*p && *p != '(' && *p != ' ') name += *p++;
		while (*p == ' ') p++;
		if (*p != '(') return nullptr;
		p++;
		std::string arg;
		int depth = 1;
		while (*p && depth) { if (*p == '(') depth++; else if (*p == ')') { if (--depth == 0) break; } arg += *p++; }
		if (*p != ')') return nullptr;
		p++;
		const char *a = arg.c_str ();
		bool empty = arg.find_first_not_of (' ') == std::string::npos;
		double v = 1;
		bool pct = false;
		if (!empty) { char *e; v = strtod (a, &e); while (*e == ' ') e++; if (*e == '%') { pct = true; v /= 100; } else if (name != "blur" && name != "hue-rotate" && name != "drop-shadow" && name != "url" && *e) return nullptr; }
		(void) pct;
		auto clamp1 = [] (double x) { return x < 0 ? 0.0 : x > 1 ? 1.0 : x; };
		auto matrix = [&] (const float M[20]) { cur = SkImageFilters::ColorFilter (SkColorFilters::Matrix (M), cur); };
		any = true;
		if (name == "blur")
		{
			double s = empty ? 0 : parse_length (a, 100, st.fontSize);
			if (!empty && v < 0) return nullptr;
			cur = SkImageFilters::Blur ((float) s, (float) s, SkTileMode::kDecal, cur);
			r.outset ((float) s * 3, (float) s * 3);
		}
		else if (name == "grayscale" || name == "sepia")
		{
			if (v < 0) return nullptr;
			double k = 1 - clamp1 (v);
			if (name[0] == 'g')
			{
				float M[20] = { (float) (.2126 + .7874 * k), (float) (.7152 - .7152 * k), (float) (.0722 - .0722 * k), 0, 0,
					(float) (.2126 - .2126 * k), (float) (.7152 + .2848 * k), (float) (.0722 - .0722 * k), 0, 0,
					(float) (.2126 - .2126 * k), (float) (.7152 - .7152 * k), (float) (.0722 + .9278 * k), 0, 0, 0, 0, 0, 1, 0 };
				matrix (M);
			}
			else
			{
				float M[20] = { (float) (.393 + .607 * k), (float) (.769 - .769 * k), (float) (.189 - .189 * k), 0, 0,
					(float) (.349 - .349 * k), (float) (.686 + .314 * k), (float) (.168 - .168 * k), 0, 0,
					(float) (.272 - .272 * k), (float) (.534 - .534 * k), (float) (.131 + .869 * k), 0, 0, 0, 0, 0, 1, 0 };
				matrix (M);
			}
		}
		else if (name == "saturate")
		{
			if (v < 0) return nullptr;
			double s = v;
			float M[20] = { (float) (.213 + .787 * s), (float) (.715 - .715 * s), (float) (.072 - .072 * s), 0, 0, (float) (.213 - .213 * s), (float) (.715 + .285 * s), (float) (.072 - .072 * s), 0, 0,
				(float) (.213 - .213 * s), (float) (.715 - .715 * s), (float) (.072 + .928 * s), 0, 0, 0, 0, 0, 1, 0 };
			matrix (M);
		}
		else if (name == "hue-rotate")
		{
			double an = empty ? 0 : v * M_PI / 180, c = std::cos (an), s = std::sin (an);
			if (!empty) { char *e; strtod (a, &e); if (!strncmp (e, "turn", 4)) an = v * 2 * M_PI; else if (!strncmp (e, "rad", 3)) an = v; else if (!strncmp (e, "grad", 4)) an = v * M_PI / 200; else if (*e && strncmp (e, "deg", 3)) return nullptr; c = std::cos (an); s = std::sin (an); }
			double m[9] = { .213+c*.787-s*.213, .715-c*.715-s*.715, .072-c*.072+s*.928, .213-c*.213+s*.143, .715+c*.285+s*.140, .072-c*.072-s*.283, .213-c*.213-s*.787, .715-c*.715+s*.715, .072+c*.928+s*.072 };
			float M[20] = { 0 };
			for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) M[i * 5 + j] = (float) m[i * 3 + j];
			M[18] = 1;
			matrix (M);
		}
		else if (name == "invert")
		{
			if (v < 0) return nullptr;
			float k = (float) clamp1 (v), g = 1 - 2 * k;
			float M[20] = { g, 0, 0, 0, k, 0, g, 0, 0, k, 0, 0, g, 0, k, 0, 0, 0, 1, 0 };
			matrix (M);
		}
		else if (name == "opacity")
		{
			if (v < 0) return nullptr;
			float k = (float) clamp1 (v);
			float M[20] = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, k, 0 };
			matrix (M);
		}
		else if (name == "brightness")
		{
			if (v < 0) return nullptr;
			float k = (float) v;
			float M[20] = { k, 0, 0, 0, 0, 0, k, 0, 0, 0, 0, 0, k, 0, 0, 0, 0, 0, 1, 0 };
			matrix (M);
		}
		else if (name == "contrast")
		{
			if (v < 0) return nullptr;
			float k = (float) v, o = (1 - k) / 2;
			float M[20] = { k, 0, 0, 0, o, 0, k, 0, 0, o, 0, 0, k, 0, o, 0, 0, 0, 1, 0 };
			matrix (M);
		}
		else if (name == "drop-shadow")
		{
			// colour and three lengths, in any order around the colour
			uint32_t col = st.color; float al = 1;
			double ln[3] = { 0, 0, 0 };
			int n = 0;
			const char *q = a;
			while (*q)
			{
				while (*q == ' ') q++;
				if (!*q) break;
				const char *e = q;
				int dp = 0;
				while (*e && (*e != ' ' || dp)) { if (*e == '(') dp++; else if (*e == ')') dp--; e++; }
				std::string w (q, e - q);
				bool ok;
				double d = parse_length (w.c_str (), 100, st.fontSize, &ok);
				if (ok && n < 3) ln[n++] = d;
				else if (!parse_color (w.c_str (), col, &al)) return nullptr;
				q = e;
			}
			if (n != 0 && n < 2) return nullptr;
			SkColor4f c { ((col >> 16) & 255) / 255.f, ((col >> 8) & 255) / 255.f, (col & 255) / 255.f, al };
			cur = SkImageFilters::DropShadow ((float) ln[0], (float) ln[1], (float) ln[2], (float) ln[2], c, nullptr, cur);
			SkRect s = r;
			s.offset ((float) ln[0], (float) ln[1]);
			s.outset ((float) ln[2] * 3, (float) ln[2] * 3);
			r.join (s);
		}
		else if (name == "url")
		{
			std::string id = url_id ((std::string ("url(") + arg + ")").c_str ());
			XNode *f = b.doc->by_id (id);
			if (!f || strcmp (el_name (f), "filter")) return nullptr;
			SkRect sub = r;
			F g = build_filter (b, f, st, bbox, &sub);
			if (!g) return nullptr;
			cur = cur ? SkImageFilters::Compose (g, cur) : g;
			r.join (sub);
		}
		else return nullptr;
	}
	if (!any) return nullptr;
	*region = r;
	return cur ? cur : SkImageFilters::Merge (nullptr, 0);
}

sk_sp<SkImageFilter> build_filter (SceneBuilder &b, XNode *f, const Style &st, const SkRect &bbox, SkRect *region)
{
	// (the primitives inherit from the filter element, not from the object)
	Style fs;
	compute_style (f, b.inherited_style (f), b.sheet, fs);
	bool userUnits = false;
	if (const char *u = f->get ("filterUnits")) userUnits = !strcmp (u, "userSpaceOnUse");
	bool primBox = false;
	if (const char *u = f->get ("primitiveUnits")) primBox = !strcmp (u, "objectBoundingBox");
	double bx = bbox.left (), by = bbox.top (), bw = bbox.width (), bh = bbox.height ();
	double fx, fy, fw, fh;
	if (userUnits)
	{
		fx = parse_length (f->get2 ("x", "-10%"), b.vpW, 16);
		fy = parse_length (f->get2 ("y", "-10%"), b.vpH, 16);
		fw = parse_length (f->get2 ("width", "120%"), b.vpW, 16);
		fh = parse_length (f->get2 ("height", "120%"), b.vpH, 16);
	}
	else
	{
		fx = frac_or_abs (f->get ("x"), bw, bx, bx - 0.1 * bw, true);
		fy = frac_or_abs (f->get ("y"), bh, by, by - 0.1 * bh, true);
		fw = frac_or_abs (f->get ("width"), bw, 0, 1.2 * bw, true);
		fh = frac_or_abs (f->get ("height"), bh, 0, 1.2 * bh, true);
	}
	if (fw <= 0 || fh <= 0) return nullptr;
	SkRect freg = SkRect::MakeXYWH ((float) fx, (float) fy, (float) fw, (float) fh);
	std::map<std::string, std::pair<F, bool>> results;	// name -> filter, is linear
	F last;
	bool lastLinear = false, any = false;

	auto convert = [] (F in, bool from, bool to) -> F {
		if (from == to) return in;
		return SkImageFilters::ColorFilter (to ? to_linear () : to_srgb (), in);
	};
	auto input = [&] (const char *name, bool wantLinear) -> F {
		F fsrc; bool lin = false;
		if (!name || !*name) { if (any) { fsrc = last; lin = lastLinear; } }
		else if (!strcmp (name, "SourceGraphic")) {}
		else if (!strcmp (name, "BackgroundImage") || !strcmp (name, "BackgroundAlpha") || !strcmp (name, "FillPaint") || !strcmp (name, "StrokePaint"))
			return SkImageFilters::Empty ();		// (no longer in the language: an empty input)
		else if (!strcmp (name, "SourceAlpha"))
		{
			float M[20] = { 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,1,0 };
			return SkImageFilters::ColorFilter (SkColorFilters::Matrix (M), nullptr);
		}
		else
		{
			auto q = results.find (name);
			if (q != results.end ()) { fsrc = q->second.first; lin = q->second.second; }
			else if (any) { fsrc = last; lin = lastLinear; }
		}
		return convert (fsrc, lin, wantLinear);
	};

	for (XNode *p : f->kids)
	{
		if (!p->element ()) continue;
		const char *n = el_name (p);
		if (strncmp (n, "fe", 2)) continue;
		Style ps;
		compute_style (p, fs, b.sheet, ps);
		bool lin = ps.colorInterpFilters == 1;
		const char *in1 = p->get ("in"), *in2 = p->get ("in2");
		// the primitive's subregion
		SkRect sub = freg;
		if (p->get ("x") || p->get ("y") || p->get ("width") || p->get ("height"))
		{
			double x = frac_or_abs (p->get ("x"), primBox ? bw : b.vpW, primBox ? bx : 0, fx, primBox);
			double y = frac_or_abs (p->get ("y"), primBox ? bh : b.vpH, primBox ? by : 0, fy, primBox);
			double w = frac_or_abs (p->get ("width"), primBox ? bw : b.vpW, 0, fw, primBox);
			double h = frac_or_abs (p->get ("height"), primBox ? bh : b.vpH, 0, fh, primBox);
			sub = SkRect::MakeXYWH ((float) x, (float) y, (float) w, (float) h);
		}
		SkImageFilters::CropRect crop (sub);
		F r;
		if (!strcmp (n, "feGaussianBlur"))
		{
			auto v = parse_numbers (p->get ("stdDeviation"));
			double sx = v.empty () ? 0 : v[0], sy = v.size () > 1 ? v[1] : sx;
			if (primBox) { sx *= bw; sy *= bh; }
			r = SkImageFilters::Blur ((float) sx, (float) sy, SkTileMode::kDecal, input (in1, lin), crop);
		}
		else if (!strcmp (n, "feOffset"))
		{
			double dx = atof (p->get2 ("dx", "0")), dy = atof (p->get2 ("dy", "0"));
			if (primBox) { dx *= bw; dy *= bh; }
			r = SkImageFilters::Offset ((float) dx, (float) dy, input (in1, lin), crop);
		}
		else if (!strcmp (n, "feFlood"))
		{
			SkColor4f c { ((ps.floodColor >> 16) & 255) / 255.f, ((ps.floodColor >> 8) & 255) / 255.f, (ps.floodColor & 255) / 255.f, ps.floodOpacity };
			r = SkImageFilters::Shader (SkShaders::Color (c, nullptr), crop);
			lin = false;			// (the colour is given in sRGB)
		}
		else if (!strcmp (n, "feColorMatrix"))
		{
			const char *t = p->get2 ("type", "matrix");
			auto v = parse_numbers (p->get ("values"));
			float M[20] = { 1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,1,0 };
			if (!strcmp (t, "matrix") && v.size () == 20)
			{
				for (int i = 0; i < 20; i++) M[i] = (float) v[i];
			}
			else if (!strcmp (t, "saturate"))
			{
				double s = v.empty () ? 1 : v[0];
				double m[9] = { .213+.787*s, .715-.715*s, .072-.072*s, .213-.213*s, .715+.285*s, .072-.072*s, .213-.213*s, .715-.715*s, .072+.928*s };
				for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) M[i * 5 + j] = (float) m[i * 3 + j];
			}
			else if (!strcmp (t, "hueRotate"))
			{
				double a = (v.empty () ? 0 : v[0]) * M_PI / 180, c = std::cos (a), s = std::sin (a);
				double m[9] = { .213+c*.787-s*.213, .715-c*.715-s*.715, .072-c*.072+s*.928,
					.213-c*.213+s*.143, .715+c*.285+s*.140, .072-c*.072-s*.283,
					.213-c*.213-s*.787, .715-c*.715+s*.715, .072+c*.928+s*.072 };
				for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) M[i * 5 + j] = (float) m[i * 3 + j];
			}
			else if (!strcmp (t, "luminanceToAlpha"))
			{
				float L[20] = { 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, .2125f,.7154f,.0721f,0,0 };
				memcpy (M, L, sizeof M);
			}
			// (the matrix's translation column is in 0..1 for Skia too)
			r = SkImageFilters::ColorFilter (SkColorFilters::Matrix (M), input (in1, lin), crop);
		}
		else if (!strcmp (n, "feComponentTransfer"))
		{
			const XNode *fr = nullptr, *fg = nullptr, *fb = nullptr, *fa = nullptr;
			for (XNode *k : p->kids)
				if (k->element ())
				{
					const char *q = el_name (k);
					if (!strcmp (q, "feFuncR")) fr = k;
					else if (!strcmp (q, "feFuncG")) fg = k;
					else if (!strcmp (q, "feFuncB")) fb = k;
					else if (!strcmp (q, "feFuncA")) fa = k;
				}
			uint8_t tr[256], tg[256], tb[256], ta[256];
			transfer (fr, tr); transfer (fg, tg); transfer (fb, tb); transfer (fa, ta);
			r = SkImageFilters::ColorFilter (SkColorFilters::TableARGB (ta, tr, tg, tb), input (in1, lin), crop);
		}
		else if (!strcmp (n, "feBlend"))
		{
			r = SkImageFilters::Blend (mode_of (p->get ("mode")), input (in2, lin), input (in1, lin), crop);
		}
		else if (!strcmp (n, "feComposite"))
		{
			const char *op = p->get2 ("operator", "over");
			if (!strcmp (op, "arithmetic"))
				r = SkImageFilters::Arithmetic ((float) atof (p->get2 ("k1", "0")), (float) atof (p->get2 ("k2", "0")), (float) atof (p->get2 ("k3", "0")), (float) atof (p->get2 ("k4", "0")),
					true, input (in2, lin), input (in1, lin), crop);
			else
			{
				SkBlendMode m = !strcmp (op, "in") ? SkBlendMode::kSrcIn : !strcmp (op, "out") ? SkBlendMode::kSrcOut : !strcmp (op, "atop") ? SkBlendMode::kSrcATop
					: !strcmp (op, "xor") ? SkBlendMode::kXor : !strcmp (op, "lighter") ? SkBlendMode::kPlus : SkBlendMode::kSrcOver;
				r = SkImageFilters::Blend (m, input (in2, lin), input (in1, lin), crop);
			}
		}
		else if (!strcmp (n, "feMerge"))
		{
			std::vector<F> kids;
			for (XNode *k : p->kids) if (k->element () && !strcmp (el_name (k), "feMergeNode")) kids.push_back (input (k->get ("in"), lin));
			if (!kids.empty ()) r = SkImageFilters::Merge (kids.data (), (int) kids.size (), crop);
		}
		else if (!strcmp (n, "feMorphology"))
		{
			auto v = parse_numbers (p->get ("radius"));
			double rx = v.empty () ? 0 : v[0], ry = v.size () > 1 ? v[1] : rx;
			if (primBox) { rx *= bw; ry *= bh; }
			if (!strcmp (p->get2 ("operator", "erode"), "dilate")) r = SkImageFilters::Dilate ((float) rx, (float) ry, input (in1, lin), crop);
			else r = SkImageFilters::Erode ((float) rx, (float) ry, input (in1, lin), crop);
		}
		else if (!strcmp (n, "feDisplacementMap"))
		{
			auto chan = [] (const char *s) { return !s ? SkColorChannel::kA : s[0] == 'R' ? SkColorChannel::kR : s[0] == 'G' ? SkColorChannel::kG : s[0] == 'B' ? SkColorChannel::kB : SkColorChannel::kA; };
			double sc = atof (p->get2 ("scale", "0"));
			if (primBox) sc *= bw;
			r = SkImageFilters::DisplacementMap (chan (p->get ("xChannelSelector")), chan (p->get ("yChannelSelector")), (float) sc, input (in2, lin), input (in1, lin), crop);
		}
		else if (!strcmp (n, "feTurbulence"))
		{
			auto v = parse_numbers (p->get ("baseFrequency"));
			float bfx = v.empty () ? 0 : (float) v[0], bfy = v.size () > 1 ? (float) v[1] : bfx;
			int oct = atoi (p->get2 ("numOctaves", "1"));
			float seed = (float) atof (p->get2 ("seed", "0"));
			bool frac2 = !strcmp (p->get2 ("type", "turbulence"), "fractalNoise");
			sk_sp<SkShader> sh = frac2 ? SkShaders::MakeFractalNoise (bfx, bfy, oct, seed) : SkShaders::MakeTurbulence (bfx, bfy, oct, seed);
			r = SkImageFilters::Shader (sh, crop);
		}
		else if (!strcmp (n, "feConvolveMatrix"))
		{
			auto ord = parse_numbers (p->get2 ("order", "3"));
			int ox = ord.empty () ? 3 : (int) ord[0], oy = ord.size () > 1 ? (int) ord[1] : ox;
			auto k = parse_numbers (p->get ("kernelMatrix"));
			if ((int) k.size () == ox * oy && ox > 0 && oy > 0)
			{
				std::vector<float> kf (k.size ());
				double sum = 0;
				for (size_t i = 0; i < k.size (); i++) { kf[i] = (float) k[i]; sum += k[i]; }
				double div = atof (p->get2 ("divisor", "0"));
				if (div == 0) div = sum != 0 ? sum : 1;
				const char *em = p->get2 ("edgeMode", "duplicate");
				SkTileMode tm = !strcmp (em, "wrap") ? SkTileMode::kRepeat : !strcmp (em, "none") ? SkTileMode::kDecal : SkTileMode::kClamp;
				r = SkImageFilters::MatrixConvolution ({ ox, oy }, kf.data (), (float) (1 / div), (float) atof (p->get2 ("bias", "0")),
					{ atoi (p->get2 ("targetX", std::to_string (ox / 2).c_str ())), atoi (p->get2 ("targetY", std::to_string (oy / 2).c_str ())) }, tm,
					!strcmp (p->get2 ("preserveAlpha", "false"), "false"), input (in1, lin), crop);
			}
		}
		else if (!strcmp (n, "feDiffuseLighting") || !strcmp (n, "feSpecularLighting"))
		{
			bool spec = n[2] == 'S';
			SkColor col = 0xFF000000 | ps.lightingColor;
			float scale = (float) atof (p->get2 ("surfaceScale", "1"));
			float kd = (float) atof (p->get2 (spec ? "specularConstant" : "diffuseConstant", "1"));
			float shin = (float) atof (p->get2 ("specularExponent", "1"));
			F src = input (in1, lin);
			for (XNode *k : p->kids)
			{
				if (!k->element ()) continue;
				const char *q = el_name (k);
				if (!strcmp (q, "feDistantLight"))
				{
					double az = atof (k->get2 ("azimuth", "0")) * M_PI / 180, el = atof (k->get2 ("elevation", "0")) * M_PI / 180;
					SkPoint3 d = SkPoint3::Make ((float) (std::cos (az) * std::cos (el)), (float) (std::sin (az) * std::cos (el)), (float) std::sin (el));
					r = spec ? SkImageFilters::DistantLitSpecular (d, col, scale, kd, shin, src, crop) : SkImageFilters::DistantLitDiffuse (d, col, scale, kd, src, crop);
				}
				else if (!strcmp (q, "fePointLight"))
				{
					SkPoint3 at = SkPoint3::Make ((float) atof (k->get2 ("x", "0")), (float) atof (k->get2 ("y", "0")), (float) atof (k->get2 ("z", "0")));
					r = spec ? SkImageFilters::PointLitSpecular (at, col, scale, kd, shin, src, crop) : SkImageFilters::PointLitDiffuse (at, col, scale, kd, src, crop);
				}
				else if (!strcmp (q, "feSpotLight"))
				{
					SkPoint3 at = SkPoint3::Make ((float) atof (k->get2 ("x", "0")), (float) atof (k->get2 ("y", "0")), (float) atof (k->get2 ("z", "0")));
					SkPoint3 to = SkPoint3::Make ((float) atof (k->get2 ("pointsAtX", "0")), (float) atof (k->get2 ("pointsAtY", "0")), (float) atof (k->get2 ("pointsAtZ", "0")));
					float ex = (float) atof (k->get2 ("specularExponent", "1")), cone = (float) atof (k->get2 ("limitingConeAngle", "90"));
					r = spec ? SkImageFilters::SpotLitSpecular (at, to, ex, cone, col, scale, kd, shin, src, crop) : SkImageFilters::SpotLitDiffuse (at, to, ex, cone, col, scale, kd, src, crop);
				}
			}
		}
		else if (!strcmp (n, "feDropShadow"))
		{
			auto v = parse_numbers (p->get ("stdDeviation"));
			double sx = v.empty () ? 2 : v[0], sy = v.size () > 1 ? v[1] : sx;
			SkColor4f c { ((ps.floodColor >> 16) & 255) / 255.f, ((ps.floodColor >> 8) & 255) / 255.f, (ps.floodColor & 255) / 255.f, ps.floodOpacity };
			r = SkImageFilters::DropShadow ((float) atof (p->get2 ("dx", "2")), (float) atof (p->get2 ("dy", "2")), (float) sx, (float) sy, c, nullptr, input (in1, false), crop);
			lin = false;
		}
		else if (!strcmp (n, "feTile"))
		{
			r = SkImageFilters::Tile (sub, freg, input (in1, lin));
		}
		else if (!strcmp (n, "feImage"))
		{
			const char *h = p->get ("href");
			if (!h) h = p->get ("xlink:href");
			XNode *el = h && h[0] == '#' ? b.doc->by_id (h + 1) : nullptr;
			if (el)
			{
				// an element, drawn as it stands in its own coordinates
				r = SkImageFilters::Picture (b.picture_of (el, sub));
			}
			else if (h && strncmp (h, "data:", 5))
			{
				if (auto img = b.load_image (h))
				{
					double vb[4] = { 0, 0, (double) img->width (), (double) img->height () };
					vg::Affine t = viewbox_transform (vb, p->get ("preserveAspectRatio"), sub.left (), sub.top (), sub.width (), sub.height ());
					r = SkImageFilters::Image (img, SkRect::Make (img->bounds ()), SkRect::MakeXYWH ((float) t.e, (float) t.f, (float) (vb[2] * t.a), (float) (vb[3] * t.d)), SkSamplingOptions (SkFilterMode::kLinear));
				}
			}
			else if (h && !strncmp (h, "data:", 5))
			{
				if (const char *c = strchr (h, ','))
				{
					// (the same decoding as <image>, kept short here)
					std::vector<uint8_t> o;
					unsigned acc = 0; int bits = 0;
					for (const char *s = c + 1; *s; s++)
					{
						char q = *s;
						int w = q >= 'A' && q <= 'Z' ? q - 'A' : q >= 'a' && q <= 'z' ? q - 'a' + 26 : q >= '0' && q <= '9' ? q - '0' + 52 : q == '+' ? 62 : q == '/' ? 63 : -1;
						if (w < 0) continue;
						acc = acc << 6 | w; bits += 6;
						if (bits >= 8) { bits -= 8; o.push_back ((acc >> bits) & 255); }
					}
					if (auto img = SkImages::DeferredFromEncodedData (SkData::MakeWithCopy (o.data (), o.size ())))
						r = SkImageFilters::Image (img, SkRect::Make (img->bounds ()), sub, SkSamplingOptions (SkFilterMode::kLinear));
				}
			}
			lin = false;
		}
		if (!r) continue;
		last = r; lastLinear = lin; any = true;
		if (const char *name = p->get ("result")) results[name] = { r, lin };
	}
	*region = freg;
	if (!any) return SkImageFilters::Empty ();
	return SkImageFilters::Crop (freg, convert (last, lastLinear, false));
}

}
