//
// style.cpp -- see style.h. Written from the CSS 2.1, CSS Color and SVG specifications.
//
#include "style.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vd {

// ---- small helpers ---------------------------------------------------------------------------------------------------
static bool sp (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static std::string trim (const std::string &s)
{
	size_t a = 0, b = s.size ();
	while (a < b && sp (s[a])) a++;
	while (b > a && sp (s[b - 1])) b--;
	return s.substr (a, b - a);
}
static std::string lower (std::string s)
{
	for (auto &c : s) if (c >= 'A' && c <= 'Z') c += 32;
	return s;
}

std::vector<double> parse_numbers (const char *s)
{
	std::vector<double> v;
	if (!s) return v;
	while (*s)
	{
		while (*s && (sp (*s) || *s == ',')) s++;
		if (!*s) break;
		char *e;
		double d = strtod (s, &e);
		if (e == s) break;
		v.push_back (d);
		s = e;
	}
	return v;
}

std::string url_id (const char *s)
{
	if (!s) return "";
	while (sp (*s)) s++;
	if (strncmp (s, "url(", 4)) return "";
	s += 4;
	while (sp (*s) || *s == '"' || *s == '\'') s++;
	if (*s != '#') return "";
	s++;
	std::string o;
	while (*s && *s != ')' && *s != '"' && *s != '\'' && !sp (*s)) o += *s++;
	return o;
}

// ---- colours ---------------------------------------------------------------------------------------------------------
static const struct { const char *n; uint32_t c; } NAMES[] = {
	{"aliceblue",0xF0F8FF},{"antiquewhite",0xFAEBD7},{"aqua",0x00FFFF},{"aquamarine",0x7FFFD4},{"azure",0xF0FFFF},
	{"beige",0xF5F5DC},{"bisque",0xFFE4C4},{"black",0x000000},{"blanchedalmond",0xFFEBCD},{"blue",0x0000FF},
	{"blueviolet",0x8A2BE2},{"brown",0xA52A2A},{"burlywood",0xDEB887},{"cadetblue",0x5F9EA0},{"chartreuse",0x7FFF00},
	{"chocolate",0xD2691E},{"coral",0xFF7F50},{"cornflowerblue",0x6495ED},{"cornsilk",0xFFF8DC},{"crimson",0xDC143C},
	{"cyan",0x00FFFF},{"darkblue",0x00008B},{"darkcyan",0x008B8B},{"darkgoldenrod",0xB8860B},{"darkgray",0xA9A9A9},
	{"darkgreen",0x006400},{"darkgrey",0xA9A9A9},{"darkkhaki",0xBDB76B},{"darkmagenta",0x8B008B},{"darkolivegreen",0x556B2F},
	{"darkorange",0xFF8C00},{"darkorchid",0x9932CC},{"darkred",0x8B0000},{"darksalmon",0xE9967A},{"darkseagreen",0x8FBC8F},
	{"darkslateblue",0x483D8B},{"darkslategray",0x2F4F4F},{"darkslategrey",0x2F4F4F},{"darkturquoise",0x00CED1},{"darkviolet",0x9400D3},
	{"deeppink",0xFF1493},{"deepskyblue",0x00BFFF},{"dimgray",0x696969},{"dimgrey",0x696969},{"dodgerblue",0x1E90FF},
	{"firebrick",0xB22222},{"floralwhite",0xFFFAF0},{"forestgreen",0x228B22},{"fuchsia",0xFF00FF},{"gainsboro",0xDCDCDC},
	{"ghostwhite",0xF8F8FF},{"gold",0xFFD700},{"goldenrod",0xDAA520},{"gray",0x808080},{"green",0x008000},
	{"greenyellow",0xADFF2F},{"grey",0x808080},{"honeydew",0xF0FFF0},{"hotpink",0xFF69B4},{"indianred",0xCD5C5C},
	{"indigo",0x4B0082},{"ivory",0xFFFFF0},{"khaki",0xF0E68C},{"lavender",0xE6E6FA},{"lavenderblush",0xFFF0F5},
	{"lawngreen",0x7CFC00},{"lemonchiffon",0xFFFACD},{"lightblue",0xADD8E6},{"lightcoral",0xF08080},{"lightcyan",0xE0FFFF},
	{"lightgoldenrodyellow",0xFAFAD2},{"lightgray",0xD3D3D3},{"lightgreen",0x90EE90},{"lightgrey",0xD3D3D3},{"lightpink",0xFFB6C1},
	{"lightsalmon",0xFFA07A},{"lightseagreen",0x20B2AA},{"lightskyblue",0x87CEFA},{"lightslategray",0x778899},{"lightslategrey",0x778899},
	{"lightsteelblue",0xB0C4DE},{"lightyellow",0xFFFFE0},{"lime",0x00FF00},{"limegreen",0x32CD32},{"linen",0xFAF0E6},
	{"magenta",0xFF00FF},{"maroon",0x800000},{"mediumaquamarine",0x66CDAA},{"mediumblue",0x0000CD},{"mediumorchid",0xBA55D3},
	{"mediumpurple",0x9370DB},{"mediumseagreen",0x3CB371},{"mediumslateblue",0x7B68EE},{"mediumspringgreen",0x00FA9A},{"mediumturquoise",0x48D1CC},
	{"mediumvioletred",0xC71585},{"midnightblue",0x191970},{"mintcream",0xF5FFFA},{"mistyrose",0xFFE4E1},{"moccasin",0xFFE4B5},
	{"navajowhite",0xFFDEAD},{"navy",0x000080},{"oldlace",0xFDF5E6},{"olive",0x808000},{"olivedrab",0x6B8E23},
	{"orange",0xFFA500},{"orangered",0xFF4500},{"orchid",0xDA70D6},{"palegoldenrod",0xEEE8AA},{"palegreen",0x98FB98},
	{"paleturquoise",0xAFEEEE},{"palevioletred",0xDB7093},{"papayawhip",0xFFEFD5},{"peachpuff",0xFFDAB9},{"peru",0xCD853F},
	{"pink",0xFFC0CB},{"plum",0xDDA0DD},{"powderblue",0xB0E0E6},{"purple",0x800080},{"rebeccapurple",0x663399},{"red",0xFF0000},
	{"rosybrown",0xBC8F8F},{"royalblue",0x4169E1},{"saddlebrown",0x8B4513},{"salmon",0xFA8072},{"sandybrown",0xF4A460},
	{"seagreen",0x2E8B57},{"seashell",0xFFF5EE},{"sienna",0xA0522D},{"silver",0xC0C0C0},{"skyblue",0x87CEEB},
	{"slateblue",0x6A5ACD},{"slategray",0x708090},{"slategrey",0x708090},{"snow",0xFFFAFA},{"springgreen",0x00FF7F},
	{"steelblue",0x4682B4},{"tan",0xD2B48C},{"teal",0x008080},{"thistle",0xD8BFD8},{"tomato",0xFF6347},
	{"turquoise",0x40E0D0},{"violet",0xEE82EE},{"wheat",0xF5DEB3},{"white",0xFFFFFF},{"whitesmoke",0xF5F5F5},
	{"yellow",0xFFFF00},{"yellowgreen",0x9ACD32},
};

static int hexv (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	c |= 32;
	return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static int clamp8 (double v) { return v < 0 ? 0 : v > 255 ? 255 : (int) std::lround (v); }
static double huef (double p, double q, double t)
{
	if (t < 0) t += 1;
	if (t > 1) t -= 1;
	if (t < 1.0 / 6) return p + (q - p) * 6 * t;
	if (t < 0.5) return q;
	if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
	return p;
}
bool parse_color (const char *s, uint32_t &rgb, float *alpha)
{
	if (alpha) *alpha = 1;
	while (sp (*s)) s++;
	if (*s == '#')
	{
		s++;
		int n = 0;
		while (hexv (s[n]) >= 0) n++;
		if (n == 3 || n == 4)
		{
			rgb = hexv (s[0]) * 17 << 16 | hexv (s[1]) * 17 << 8 | hexv (s[2]) * 17;
			if (n == 4 && alpha) *alpha = hexv (s[3]) * 17 / 255.f;
			return true;
		}
		if (n == 6 || n == 8)
		{
			rgb = 0;
			for (int i = 0; i < 6; i++) rgb = rgb << 4 | hexv (s[i]);
			if (n == 8 && alpha) *alpha = (hexv (s[6]) * 16 + hexv (s[7])) / 255.f;
			return true;
		}
		return false;
	}
	std::string l;
	const char *p = s;
	while (*p && *p != '(' && !sp (*p)) l += (char) (*p >= 'A' && *p <= 'Z' ? *p + 32 : *p), p++;
	while (sp (*p)) p++;
	if (*p == '(' && (l == "rgb" || l == "rgba" || l == "hsl" || l == "hsla"))
	{
		p++;
		double v[4] = { 0, 0, 0, 1 }; bool pct[4] = {};
		int n = 0;
		while (n < 4)
		{
			while (*p && (sp (*p) || *p == ',' || *p == '/')) p++;
			char *e;
			double d = strtod (p, &e);
			if (e == p) break;
			v[n] = d; p = e;
			if (*p == '%') { pct[n] = true; p++; }
			else if (!strncmp (p, "deg", 3)) p += 3;
			n++;
		}
		if (n < 3) return false;
		if (l[0] == 'r')
		{
			int c[3];
			for (int i = 0; i < 3; i++) c[i] = clamp8 (pct[i] ? v[i] * 2.55 : v[i]);
			rgb = c[0] << 16 | c[1] << 8 | c[2];
		}
		else
		{
			double h = std::fmod (std::fmod (v[0], 360) + 360, 360) / 360, sa = v[1] / 100, li = v[2] / 100;
			sa = std::min (1.0, std::max (0.0, sa)); li = std::min (1.0, std::max (0.0, li));
			double q = li < 0.5 ? li * (1 + sa) : li + sa - li * sa, pp = 2 * li - q;
			rgb = clamp8 (huef (pp, q, h + 1.0 / 3) * 255) << 16 | clamp8 (huef (pp, q, h) * 255) << 8 | clamp8 (huef (pp, q, h - 1.0 / 3) * 255);
		}
		if (alpha) { double a = pct[3] ? v[3] / 100 : v[3]; *alpha = (float) std::min (1.0, std::max (0.0, a)); }
		return true;
	}
	if (l == "transparent") { rgb = 0; if (alpha) *alpha = 0; return true; }
	for (auto &q : NAMES) if (l == q.n) { rgb = q.c; return true; }
	return false;
}

static double s_vw = 100, s_vh = 100;
void set_viewport_units (double w, double h) { s_vw = w; s_vh = h; }

double parse_length (const char *s, double percentOf, double fontSize, bool *ok)
{
	if (ok) *ok = false;
	if (!s) return 0;
	char *e;
	double v = strtod (s, &e);
	if (e == s) return 0;
	if (ok) *ok = true;
	// (a unit touches its number: what follows a space is the next value of a list)
	if (!*e || sp (*e) || *e == ',') return v;
	if (e[0] == '%') return v * percentOf / 100;
	char a = e[0] | 32, b = e[1] | 32;
	std::string unit;
	for (const char *q = e; *q && *q != ' ' && *q != ',' && *q != ')' ; q++) unit += (char) (*q | 32);
	if (unit == "vw" || unit == "vi") return v * s_vw / 100;
	if (unit == "vh" || unit == "vb") return v * s_vh / 100;
	if (unit == "vmin") return v * std::min (s_vw, s_vh) / 100;
	if (unit == "vmax") return v * std::max (s_vw, s_vh) / 100;
	if (unit == "rem") return v * 16;
	if (unit == "ch") return v * fontSize / 2;
	if (unit == "ic") return v * fontSize;
	if (unit == "cap") return v * fontSize * 0.7;
	if (unit == "lh") return v * fontSize * 1.2;
	if (unit == "rlh") return v * 16 * 1.2;
	if (unit != "px" && unit != "pt" && unit != "pc" && unit != "mm" && unit != "cm" && unit != "in" && unit != "em" && unit != "ex" && unit != "q")
	{
		if (ok) *ok = false;
		return 0;
	}
	if (a == 'p' && b == 'x') return v;
	if (a == 'p' && b == 't') return v * 96 / 72;
	if (a == 'p' && b == 'c') return v * 16;
	if (a == 'm' && b == 'm') return v * 96 / 25.4;
	if (a == 'c' && b == 'm') return v * 96 / 2.54;
	if (a == 'i' && b == 'n') return v * 96;
	if (a == 'e' && b == 'm') return v * fontSize;
	if (a == 'e' && b == 'x') return v * fontSize / 2;
	if (a == 'q') return v * 96 / 101.6;
	if (a == 'r' && b == 'e') return v * 16;
	return v;
}

vg::Affine parse_transform (const char *s)
{
	vg::Affine m;
	if (!s) return m;
	for (;;)
	{
		while (*s && (sp (*s) || *s == ',')) s++;
		if (!*s) break;
		std::string name;
		while (*s && *s != '(' && !sp (*s)) name += *s++;
		while (sp (*s)) s++;
		if (*s != '(') break;
		s++;
		double v[6] = {}; int n = 0;
		while (n < 6)
		{
			while (*s && (sp (*s) || *s == ',')) s++;
			if (*s == ')') break;
			char *e;
			double d = strtod (s, &e);
			if (e == s) return m;
			v[n++] = d; s = e;
		}
		while (*s && *s != ')') s++;
		if (*s == ')') s++;
		vg::Affine t;
		if (name == "matrix" && n == 6) t = { v[0], v[1], v[2], v[3], v[4], v[5] };
		else if (name == "translate") t = vg::Affine::translate (v[0], n > 1 ? v[1] : 0);
		else if (name == "scale") t = vg::Affine::scale (v[0], n > 1 ? v[1] : v[0]);
		else if (name == "rotate")
		{
			double r = v[0] * M_PI / 180;
			t = n >= 3 ? vg::Affine::rotate_at (r, { v[1], v[2] }) : vg::Affine::rotate (r);
		}
		else if (name == "skewX") t = { 1, 0, std::tan (v[0] * M_PI / 180), 1, 0, 0 };
		else if (name == "skewY") t = { 1, std::tan (v[0] * M_PI / 180), 0, 1, 0, 0 };
		m = m * t;
	}
	return m;
}
std::string write_transform (const vg::Affine &m)
{
	char b[160];
	if (m.identity ()) return "";
	if (m.a == 1 && m.b == 0 && m.c == 0 && m.d == 1) snprintf (b, sizeof b, "translate(%.8g,%.8g)", m.e, m.f);
	else if (m.b == 0 && m.c == 0 && m.e == 0 && m.f == 0) snprintf (b, sizeof b, "scale(%.8g,%.8g)", m.a, m.d);
	else snprintf (b, sizeof b, "matrix(%.8g,%.8g,%.8g,%.8g,%.8g,%.8g)", m.a, m.b, m.c, m.d, m.e, m.f);
	return b;
}

// ---- the style sheet --------------------------------------------------------------------------------------------------
void parse_inline (const char *s, std::vector<Decl> &out)
{
	if (!s) return;
	while (*s)
	{
		while (*s && (sp (*s) || *s == ';')) s++;
		if (!*s) break;
		std::string prop;
		while (*s && *s != ':' && *s != ';') prop += *s++;
		if (*s != ':') continue;
		s++;
		std::string val;
		int paren = 0; char q = 0;
		while (*s && (*s != ';' || paren > 0 || q))
		{
			if (q) { if (*s == q) q = 0; }
			else if (*s == '"' || *s == '\'') q = *s;
			else if (*s == '(') paren++;
			else if (*s == ')') paren--;
			val += *s++;
		}
		Decl d;
		d.prop = lower (trim (prop));
		d.value = trim (val);
		size_t bang = d.value.rfind ('!');
		if (bang != std::string::npos && lower (trim (d.value.substr (bang + 1))) == "important")
		{
			d.important = true;
			d.value = trim (d.value.substr (0, bang));
		}
		if (!d.prop.empty ()) out.push_back (std::move (d));
	}
}

static bool name_char (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || (unsigned char) c >= 128; }

static bool parse_selector (const std::string &text, Selector &sel)
{
	const char *s = text.c_str ();
	char comb = ' ';
	while (*s)
	{
		while (sp (*s)) s++;
		if (!*s) break;
		if (*s == '>' || *s == '+' || *s == '~')
		{
			if (*s != '>') return false;		// (sibling combinators: not used in drawings)
			comb = '>'; s++;
			continue;
		}
		Selector::Simple p;
		p.comb = comb; comb = ' ';
		bool any = false;
		while (*s && !sp (*s) && *s != '>')
		{
			any = true;
			if (*s == '*') { s++; continue; }
			if (*s == '#') { s++; while (name_char (*s)) p.id += *s++; sel.specificity += 10000; continue; }
			if (*s == '.') { s++; std::string c; while (name_char (*s)) c += *s++; p.cls.push_back (c); sel.specificity += 100; continue; }
			if (*s == ':')
			{
				s++;
				std::string c;
				while (name_char (*s)) c += *s++;
				if (c == "first-child") p.first = true;
				else return false;
				sel.specificity += 100;
				continue;
			}
			if (*s == '[')
			{
				s++;
				std::string k, v;
				while (*s && *s != ']' && *s != '=') k += *s++;
				if (*s == '=')
				{
					s++;
					while (*s && *s != ']') { if (*s != '"' && *s != '\'') v += *s; s++; }
				}
				if (*s == ']') s++;
				p.attr.push_back ({ trim (k), v });
				sel.specificity += 100;
				continue;
			}
			if (name_char (*s)) { while (name_char (*s) || *s == '|') p.tag += *s++; sel.specificity += 1; continue; }
			return false;
		}
		if (any) sel.parts.push_back (std::move (p));
	}
	return !sel.parts.empty ();
}

void StyleSheet::parse (const std::string &css)
{
	size_t i = 0, n = css.size ();
	while (i < n)
	{
		// comments and CDATA markers
		if (css.compare (i, 2, "/*") == 0) { size_t e = css.find ("*/", i + 2); if (e == std::string::npos) return; i = e + 2; continue; }
		if (sp (css[i])) { i++; continue; }
		size_t br = i;
		std::string head;
		while (br < n && css[br] != '{' && css[br] != ';')
		{
			if (css.compare (br, 2, "/*") == 0) { size_t e = css.find ("*/", br + 2); if (e == std::string::npos) return; br = e + 2; continue; }
			head += css[br++];
		}
		if (br >= n) return;
		if (css[br] == ';') { i = br + 1; continue; }	// (@import, @charset)
		size_t e = br + 1;
		int depth = 1;
		while (e < n && depth) { if (css[e] == '{') depth++; else if (css[e] == '}') depth--; e++; }
		std::string body = css.substr (br + 1, e - br - 2);
		i = e;
		head = trim (head);
		if (head.empty () || head[0] == '@')
		{
			if (head.compare (0, 6, "@media") == 0 && head.find ("print") == std::string::npos) parse (body);
			continue;
		}
		std::vector<Decl> decls;
		parse_inline (body.c_str (), decls);
		size_t a = 0;
		for (;;)
		{
			size_t c = head.find (',', a);
			Rule r;
			if (parse_selector (head.substr (a, c == std::string::npos ? c : c - a), r.sel))
			{
				r.decls = decls;
				r.order = (int) rules.size ();
				rules.push_back (std::move (r));
			}
			if (c == std::string::npos) break;
			a = c + 1;
		}
	}
}

void StyleSheet::collect (XNode *n)
{
	if (!n->element ()) return;
	if (n->name == "style" || n->name == "svg:style")
	{
		const char *t = n->get ("type");
		if (!t || !*t || !strcmp (t, "text/css")) parse (n->text_content ());
		return;
	}
	for (XNode *k : n->kids) collect (k);
}

static bool has_class (const char *list, const std::string &c)
{
	if (!list) return false;
	const char *p = list;
	while (*p)
	{
		while (sp (*p)) p++;
		const char *e = p;
		while (*e && !sp (*e)) e++;
		if ((size_t) (e - p) == c.size () && !strncmp (p, c.c_str (), c.size ())) return true;
		p = e;
	}
	return false;
}
static const char *local_name (const std::string &n)
{
	size_t c = n.rfind (':');
	return c == std::string::npos ? n.c_str () : n.c_str () + c + 1;
}
static bool match_simple (const Selector::Simple &p, const XNode *el)
{
	if (!p.tag.empty () && p.tag != el->name && p.tag != local_name (el->name)) return false;
	if (!p.id.empty ()) { const char *id = el->get ("id"); if (!id || p.id != id) return false; }
	if (!p.cls.empty ()) { const char *c = el->get ("class"); for (auto &q : p.cls) if (!has_class (c, q)) return false; }
	for (auto &a : p.attr)
	{
		const char *v = el->get (a.first.c_str ());
		if (!v || (!a.second.empty () && a.second != v)) return false;
	}
	if (p.first && el->parent)
	{
		for (XNode *k : el->parent->kids)
			if (k->element ()) { if (k != el) return false; break; }
	}
	return true;
}
static bool match (const Selector &s, const XNode *el, int part)
{
	if (!match_simple (s.parts[part], el)) return false;
	if (part == 0) return true;
	char comb = s.parts[part].comb;
	const XNode *p = el->parent;
	if (comb == '>') return p && p->element () && p->name != "#top" && match (s, p, part - 1);
	for (; p && p->name != "#top"; p = p->parent)
		if (match (s, p, part - 1)) return true;
	return false;
}
void StyleSheet::declarations (const XNode *el, std::vector<std::pair<int, const Decl *>> &out) const
{
	if (rules.empty ()) return;
	std::vector<const Rule *> hit;
	for (auto &r : rules)
		if (match (r.sel, el, (int) r.sel.parts.size () - 1)) hit.push_back (&r);
	std::stable_sort (hit.begin (), hit.end (), [] (const Rule *a, const Rule *b) {
		return a->sel.specificity != b->sel.specificity ? a->sel.specificity < b->sel.specificity : a->order < b->order;
	});
	for (const Rule *r : hit)
		for (auto &d : r->decls) out.push_back ({ r->sel.specificity, &d });
}

// ---- the computed style -----------------------------------------------------------------------------------------------
static void parse_paint (const std::string &v, const Style &st, PaintSpec &p, bool isInherit, const PaintSpec &parent)
{
	if (isInherit) { p = parent; return; }
	const char *s = v.c_str ();
	if (v == "none") { p = PaintSpec (); return; }
	if (v == "currentColor" || v == "currentcolor") { p = PaintSpec (); p.kind = P_CURRENT; return; }
	if (v == "context-fill") { p = PaintSpec (); p.kind = P_CTX_FILL; return; }
	if (v == "context-stroke") { p = PaintSpec (); p.kind = P_CTX_STROKE; return; }
	if (!strncmp (s, "url(", 4))
	{
		p = PaintSpec ();
		p.kind = P_URL;
		p.url = url_id (s);
		const char *e = strchr (s, ')');
		if (e)
		{
			std::string fb = trim (e + 1);
			if (fb == "none") { p.hasFallback = true; p.rgb = 0; p.alpha = 0; }
			else if (!fb.empty () && parse_color (fb.c_str (), p.rgb, &p.alpha)) p.hasFallback = true;
		}
		return;
	}
	uint32_t c; float a;
	if (parse_color (s, c, &a)) { p = PaintSpec (); p.kind = P_COLOR; p.rgb = c; p.alpha = a; }
	(void) st;
}

static const char *const BLEND_NAMES[] = { "normal", "multiply", "screen", "overlay", "darken", "lighten", "color-dodge", "color-burn",
	"hard-light", "soft-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity" };

static float num01 (const std::string &v)
{
	char *e;
	double d = strtod (v.c_str (), &e);
	if (*e == '%') d /= 100;
	return (float) std::min (1.0, std::max (0.0, d));
}

static void apply_decl (const std::string &prop, const std::string &v, const Style &parent, Style &o)
{
	bool inh = v == "inherit";
	const char *s = v.c_str ();
#define IS(x) (prop == x)
	switch (prop[0])
	{
	case 'b':
		if (IS ("baseline-shift"))
		{
			if (inh) o.baselineShift = parent.baselineShift;
			else if (v == "sub") o.baselineShift = -parent.fontSize * 0.3;
			else if (v == "super") o.baselineShift = parent.fontSize * 0.4;
			else if (v != "baseline") o.baselineShift = parse_length (s, parent.fontSize, parent.fontSize);
			else o.baselineShift = 0;
		}
		return;
	case 'c':
		if (IS ("color")) { if (inh) o.color = parent.color; else { uint32_t c; if (parse_color (s, c)) o.color = c; } }
		else if (IS ("clip-path")) o.clipPath = inh ? parent.clipPath : url_id (s);
		else if (IS ("clip-rule")) o.clipRule = inh ? parent.clipRule : v == "evenodd" ? FR_EVENODD : FR_NONZERO;
		else if (IS ("color-interpolation-filters")) o.colorInterpFilters = inh ? parent.colorInterpFilters : v == "sRGB" ? 0 : 1;
		return;
	case 'd':
		if (IS ("display")) o.display = inh ? parent.display : v != "none";
		else if (IS ("direction")) o.direction = inh ? parent.direction : v == "rtl";
		else if (IS ("dominant-baseline"))
		{
			static const char *const N[] = { "auto", "middle", "central", "hanging", "mathematical", "text-before-edge", "text-after-edge", "ideographic" };
			if (inh) o.baseline = parent.baseline;
			else { o.baseline = 0; for (int i = 0; i < 8; i++) if (v == N[i]) o.baseline = i; }
		}
		return;
	case 'f':
		if (IS ("fill")) parse_paint (v, o, o.fill, inh, parent.fill);
		else if (IS ("fill-opacity")) o.fillOpacity = inh ? parent.fillOpacity : num01 (v);
		else if (IS ("fill-rule")) o.fillRule = inh ? parent.fillRule : v == "evenodd" ? FR_EVENODD : FR_NONZERO;
		else if (IS ("font-family")) o.fontFamily = inh ? parent.fontFamily : v;
		else if (IS ("font-size"))
		{
			static const struct { const char *n; double k; } ABS[] = { {"xx-small",9},{"x-small",10},{"small",13},{"medium",16},{"large",18},{"x-large",24},{"xx-large",32} };
			if (inh) { o.fontSize = parent.fontSize; return; }
			if (v == "larger") { o.fontSize = parent.fontSize * 1.2; return; }
			if (v == "smaller") { o.fontSize = parent.fontSize / 1.2; return; }
			for (auto &a : ABS) if (v == a.n) { o.fontSize = a.k; return; }
			bool ok;
			double d = parse_length (s, parent.fontSize, parent.fontSize, &ok);
			if (ok) o.fontSize = d;
		}
		else if (IS ("font-weight"))
		{
			if (inh) o.fontWeight = parent.fontWeight;
			else if (v == "normal") o.fontWeight = 400;
			else if (v == "bold") o.fontWeight = 700;
			else if (v == "bolder") o.fontWeight = std::min (900, parent.fontWeight + 300);
			else if (v == "lighter") o.fontWeight = std::max (100, parent.fontWeight - 300);
			else { int w = atoi (s); if (w) o.fontWeight = w; }
		}
		else if (IS ("font-style")) o.fontItalic = inh ? parent.fontItalic : v == "italic" ? 1 : v == "oblique" ? 2 : 0;
		else if (IS ("font-stretch"))
		{
			static const char *const N[] = { "ultra-condensed", "extra-condensed", "condensed", "semi-condensed", "normal", "semi-expanded", "expanded", "extra-expanded", "ultra-expanded" };
			if (inh) o.fontStretch = parent.fontStretch;
			else for (int i = 0; i < 9; i++) if (v == N[i]) o.fontStretch = i + 1;
		}
		else if (IS ("font-feature-settings")) o.fontFeatures = inh ? parent.fontFeatures : v;
		else if (IS ("font-kerning")) o.noKerning = inh ? parent.noKerning : v == "none";
		else if (IS ("font-variant") || IS ("font-variant-caps")) o.smallCaps = inh ? parent.smallCaps : v.find ("small-caps") != std::string::npos;
		else if (IS ("font-variation-settings")) o.fontVariation = inh ? parent.fontVariation : v;
		else if (IS ("filter"))
		{
			if (inh) { o.filter = parent.filter; o.filterFunc = parent.filterFunc; }
			else if (!strncmp (s, "url(", 4) && v.find (')') + 1 == v.size ()) o.filter = url_id (s);
			else if (v != "none") o.filterFunc = v;
		}
		else if (IS ("flood-color")) { if (inh) o.floodColor = parent.floodColor; else { uint32_t c; if (v == "currentColor") o.floodColor = o.color; else if (parse_color (s, c)) o.floodColor = c; } }
		else if (IS ("flood-opacity")) o.floodOpacity = inh ? parent.floodOpacity : num01 (v);
		else if (IS ("font"))
		{
			// the short form: only what comes before the family matters here
			size_t sl = v.find_first_of ("0123456789");
			if (sl != std::string::npos)
			{
				bool ok;
				size_t end = v.find (' ', sl);
				if (end != std::string::npos)
				{
					double d = parse_length (v.substr (sl, end - sl).c_str (), parent.fontSize, parent.fontSize, &ok);
					if (ok) o.fontSize = d;
					o.fontFamily = trim (v.substr (end));
				}
			}
			if (v.find ("bold") != std::string::npos) o.fontWeight = 700;
			if (v.find ("italic") != std::string::npos) o.fontItalic = 1;
		}
		return;
	case 'i':
		if (IS ("isolation")) o.isolate = v == "isolate";
		else if (IS ("inline-size")) o.inlineSize = inh ? parent.inlineSize : parse_length (s, 100, o.fontSize);
		return;
	case 'l':
		if (IS ("line-height"))
		{
			if (inh) o.lineHeight = parent.lineHeight;
			else if (v != "normal") { char *e; double d = strtod (s, &e); if (e != s) o.lineHeight = *e == '%' ? d / 100 : d; }
		}
		else if (IS ("kerning")) o.noKerning = inh ? parent.noKerning : v == "0";
		else if (IS ("letter-spacing")) o.letterSpacing = inh ? parent.letterSpacing : v == "normal" ? 0 : parse_length (s, o.fontSize, o.fontSize);
		else if (IS ("lighting-color")) { if (inh) o.lightingColor = parent.lightingColor; else { uint32_t c; if (v == "currentColor") o.lightingColor = o.color; else if (parse_color (s, c)) o.lightingColor = c; } }
		return;
	case 'm':
		if (IS ("mask")) o.mask = inh ? parent.mask : url_id (s);
		else if (IS ("marker-start")) o.markerStart = inh ? parent.markerStart : url_id (s);
		else if (IS ("marker-mid")) o.markerMid = inh ? parent.markerMid : url_id (s);
		else if (IS ("marker-end")) o.markerEnd = inh ? parent.markerEnd : url_id (s);
		else if (IS ("marker")) { o.markerStart = o.markerMid = o.markerEnd = url_id (s); }
		else if (IS ("mix-blend-mode"))
		{
			o.blend = 0;
			for (int i = 0; i < 16; i++) if (v == BLEND_NAMES[i]) o.blend = i;
		}
		return;
	case 'o':
		if (IS ("opacity")) o.opacity = inh ? parent.opacity : num01 (v);
		else if (IS ("overflow")) o.overflowVisible = inh ? parent.overflowVisible : v == "visible" || v == "auto";
		return;
	case 'p':
		if (IS ("paint-order"))
		{
			if (inh) { memcpy (o.paintOrder, parent.paintOrder, 3); return; }
			if (v == "normal") { o.paintOrder[0] = 0; o.paintOrder[1] = 1; o.paintOrder[2] = 2; return; }
			int n = 0; bool seen[3] = {};
			const char *p = s;
			while (*p && n < 3)
			{
				while (sp (*p)) p++;
				int k = !strncmp (p, "fill", 4) ? 0 : !strncmp (p, "stroke", 6) ? 1 : !strncmp (p, "markers", 7) ? 2 : -1;
				if (k < 0) break;
				if (!seen[k]) { o.paintOrder[n++] = k; seen[k] = true; }
				while (*p && !sp (*p)) p++;
			}
			for (int k = 0; k < 3 && n < 3; k++) if (!seen[k]) o.paintOrder[n++] = k;
		}
		return;
	case 's':
		if (IS ("stroke")) parse_paint (v, o, o.stroke, inh, parent.stroke);
		else if (IS ("stroke-width")) { if (inh) o.strokeWidth = parent.strokeWidth; else { bool ok; double d = parse_length (s, 100, o.fontSize, &ok); if (ok && d >= 0) o.strokeWidth = d; } }
		else if (IS ("stroke-opacity")) o.strokeOpacity = inh ? parent.strokeOpacity : num01 (v);
		else if (IS ("stroke-linecap")) o.lineCap = inh ? parent.lineCap : v == "round" ? LC_ROUND : v == "square" ? LC_SQUARE : LC_BUTT;
		else if (IS ("stroke-linejoin")) o.lineJoin = inh ? parent.lineJoin : v == "round" ? LJ_ROUND : v == "bevel" ? LJ_BEVEL : LJ_MITER;
		else if (IS ("stroke-miterlimit")) { if (inh) o.strokeMiter = parent.strokeMiter; else { double d = atof (s); if (d >= 1) o.strokeMiter = d; } }
		else if (IS ("stroke-dasharray"))
		{
			if (inh) { o.dash = parent.dash; return; }
			o.dash.clear ();
			if (v == "none") return;
			const char *p = s;
			while (*p)
			{
				while (*p && (sp (*p) || *p == ',')) p++;
				if (!*p) break;
				bool ok;
				double d = parse_length (p, 100, o.fontSize, &ok);
				if (!ok || d < 0) { o.dash.clear (); return; }
				o.dash.push_back (d);
				while (*p && !sp (*p) && *p != ',') p++;
			}
		}
		else if (IS ("stroke-dashoffset")) o.dashOffset = inh ? parent.dashOffset : parse_length (s, 100, o.fontSize);
		else if (IS ("stop-color")) { if (inh) o.stopColor = parent.stopColor; else { uint32_t c; float a; if (v == "currentColor") o.stopColor = o.color; else if (parse_color (s, c, &a)) { o.stopColor = c; o.stopOpacity *= a; } } }
		else if (IS ("stop-opacity")) o.stopOpacity *= inh ? parent.stopOpacity : num01 (v);
		else if (IS ("shape-rendering")) o.shapeRendering = inh ? parent.shapeRendering : v == "crispEdges" || v == "optimizeSpeed";
		return;
	case 't':
		if (IS ("text-anchor")) o.textAnchor = inh ? parent.textAnchor : v == "middle" ? TA_MIDDLE : v == "end" ? TA_END : TA_START;
		else if (IS ("text-decoration") || IS ("text-decoration-line"))
		{
			o.textDeco = 0;
			if (v.find ("underline") != std::string::npos) o.textDeco |= 1;
			if (v.find ("overline") != std::string::npos) o.textDeco |= 2;
			if (v.find ("line-through") != std::string::npos) o.textDeco |= 4;
		}
		return;
	case 'v':
		if (IS ("visibility")) o.visible = inh ? parent.visible : v == "visible";
		return;
	case 'w':
		if (IS ("word-spacing")) o.wordSpacing = inh ? parent.wordSpacing : v == "normal" ? 0 : parse_length (s, o.fontSize, o.fontSize);
		else if (IS ("writing-mode")) o.writingMode = inh ? parent.writingMode : (v == "tb" || v == "tb-rl" || v == "vertical-rl" || v == "vertical-lr");
		return;
	}
#undef IS
}

// (the font-size first: lengths in em lean on it)
static const char *const PRESENTATION[] = {
	"font-size", "color", "alignment-baseline", "baseline-shift", "clip-path", "clip-rule", "color-interpolation-filters", "direction", "display",
	"dominant-baseline", "fill", "fill-opacity", "fill-rule", "filter", "flood-color", "flood-opacity", "font-family", "font-stretch",
	"font-style", "font-variant", "font-kerning", "inline-size", "line-height", "kerning", "font-weight", "letter-spacing", "lighting-color", "marker-end", "marker-mid", "marker-start", "mask", "mix-blend-mode", "opacity",
	"overflow", "paint-order", "shape-rendering", "stop-color", "stop-opacity", "stroke", "stroke-dasharray", "stroke-dashoffset", "stroke-linecap",
	"stroke-linejoin", "stroke-miterlimit", "stroke-opacity", "stroke-width", "text-anchor", "text-decoration", "visibility", "word-spacing", "writing-mode",
};

void compute_style (const XNode *el, const Style &parent, const StyleSheet &sheet, Style &o)
{
	o = parent;
	// the properties that do not inherit
	o.opacity = 1; o.stopColor = 0; o.stopOpacity = 1; o.floodColor = 0; o.floodOpacity = 1; o.lightingColor = 0xFFFFFF;
	o.display = true; o.isolate = false; o.overflowVisible = false; o.blend = 0; o.baseline = DB_AUTO; o.baselineShift = 0;
	o.clipPath.clear (); o.mask.clear (); o.filter.clear (); o.filterFunc.clear ();
	o.inlineSize = 0;
	o.textDecoInherit |= parent.textDeco; o.textDeco = 0;

	for (const char *p : PRESENTATION)
		if (const char *v = el->get (p)) apply_decl (p, trim (v), parent, o);

	std::vector<std::pair<int, const Decl *>> sh;
	sheet.declarations (el, sh);
	std::vector<Decl> in;
	parse_inline (el->get ("style"), in);
	// (font-size first in each layer)
	for (auto &d : sh) if (!d.second->important && d.second->prop == "font-size") apply_decl (d.second->prop, d.second->value, parent, o);
	for (auto &d : in) if (!d.important && d.prop == "font-size") apply_decl (d.prop, d.value, parent, o);
	for (auto &d : sh) if (!d.second->important) apply_decl (d.second->prop, d.second->value, parent, o);
	for (auto &d : in) if (!d.important) apply_decl (d.prop, d.value, parent, o);
	for (auto &d : sh) if (d.second->important) apply_decl (d.second->prop, d.second->value, parent, o);
	for (auto &d : in) if (d.important) apply_decl (d.prop, d.value, parent, o);
}

std::string get_prop (const XNode *el, const char *prop)
{
	std::vector<Decl> in;
	parse_inline (el->get ("style"), in);
	for (int i = (int) in.size () - 1; i >= 0; i--) if (in[i].prop == prop) return in[i].value;
	const char *v = el->get (prop);
	return v ? v : "";
}

std::string style_with (const char *style, const char *prop, const char *value)
{
	std::vector<Decl> in;
	parse_inline (style, in);
	std::string o;
	bool done = false;
	for (auto &d : in)
	{
		if (d.prop == prop) { if (done || !value) continue; d.value = value; done = true; }
		if (!o.empty ()) o += ';';
		o += d.prop; o += ':'; o += d.value;
		if (d.important) o += " !important";
	}
	if (!done && value) { if (!o.empty ()) o += ';'; o += prop; o += ':'; o += value; }
	return o;
}

}
