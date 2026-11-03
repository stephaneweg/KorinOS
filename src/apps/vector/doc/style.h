//
// style.h -- CSS as SVG uses it: a style sheet, the cascade (presentation attributes, then the sheet's rules by
// specificity, then the style attribute, !important last), inheritance, and the computed style of an element.
//
#ifndef _vector_style_h
#define _vector_style_h

#include "xml.h"
#include "../geom/geom.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vd {

enum PaintKind : uint8_t { P_NONE, P_COLOR, P_URL, P_CURRENT, P_CTX_FILL, P_CTX_STROKE };

struct PaintSpec
{
	PaintKind kind = P_NONE;
	uint32_t rgb = 0;			// 0xRRGGBB
	float alpha = 1;			// (rgba ())
	std::string url;			// the id, and a fallback in rgb when hasFallback
	bool hasFallback = false;
};

enum { LC_BUTT, LC_ROUND, LC_SQUARE };
enum { LJ_MITER, LJ_ROUND, LJ_BEVEL };
enum { TA_START, TA_MIDDLE, TA_END };
enum { FR_NONZERO, FR_EVENODD };
enum { DB_AUTO, DB_MIDDLE, DB_CENTRAL, DB_HANGING, DB_MATHEMATICAL, DB_TEXT_BEFORE, DB_TEXT_AFTER, DB_IDEOGRAPHIC };

struct Style
{
	Style () { fill.kind = P_COLOR; }
	// ---- inherited
	PaintSpec fill, stroke;
	uint32_t color = 0;
	float fillOpacity = 1, strokeOpacity = 1;
	uint8_t fillRule = FR_NONZERO, clipRule = FR_NONZERO;
	double strokeWidth = 1, strokeMiter = 4, dashOffset = 0;
	bool dashPct = false;
	uint8_t lineCap = LC_BUTT, lineJoin = LJ_MITER;
	std::vector<double> dash;
	std::string markerStart, markerMid, markerEnd;
	bool visible = true;
	double fontSize = 16;			// px
	int fontWeight = 400;
	uint8_t fontItalic = 0;			// 0 normal, 1 italic, 2 oblique
	int fontStretch = 5;			// 1..9
	std::string fontFamily = "sans-serif";
	std::string fontFeatures, fontVariation;
	double letterSpacing = 0, wordSpacing = 0;
	uint8_t textAnchor = TA_START;
	uint8_t direction = 0;			// 0 ltr, 1 rtl
	uint8_t writingMode = 0;		// 0 horizontal, 1 vertical
	uint8_t textDecoInherit = 0;
	bool noKerning = false, smallCaps = false;
	uint8_t paintOrder[3] = { 0, 1, 2 };	// 0 fill, 1 stroke, 2 markers
	uint8_t colorInterpFilters = 1;		// 0 sRGB, 1 linearRGB
	uint8_t shapeRendering = 0;		// 1: no antialiasing
	// ---- not inherited
	float opacity = 1, stopOpacity = 1, floodOpacity = 1;
	uint32_t stopColor = 0, floodColor = 0, lightingColor = 0xFFFFFF;
	bool display = true, isolate = false, overflowVisible = false;
	uint8_t blend = 0;			// 0 normal, then BLEND_NAMES
	uint8_t textDeco = 0;			// 1 underline, 2 overline, 4 line-through
	uint8_t baseline = DB_AUTO;
	std::string clipPath, mask, filter;	// ids
	double baselineShift = 0;
	double inlineSize = 0;			// SVG 2 auto-wrapped text
	double lineHeight = 1.25;
	std::string filterFunc;			// a CSS function list (blur (...))
};

struct Selector
{
	struct Simple { char comb = ' '; std::string tag, id; std::vector<std::string> cls; std::vector<std::pair<std::string, std::string>> attr; bool first = false; };
	std::vector<Simple> parts;
	int specificity = 0;
};
struct Decl { std::string prop, value; bool important = false; };
struct Rule { Selector sel; std::vector<Decl> decls; int order = 0; };

class StyleSheet
{
public:
	std::vector<Rule> rules;
	void clear () { rules.clear (); }
	void parse (const std::string &css);
	void collect (XNode *root);		// every <style> of the tree
	void declarations (const XNode *el, std::vector<std::pair<int, const Decl *>> &out) const;
};

void parse_inline (const char *style, std::vector<Decl> &out);
// the computed style of el under its parent's
void compute_style (const XNode *el, const Style &parent, const StyleSheet &sheet, Style &out);

// values
bool parse_color (const char *s, uint32_t &rgb, float *alpha = nullptr);
double parse_length (const char *s, double percentOf, double fontSize, bool *ok = nullptr);
void set_viewport_units (double w, double h);	// what vw, vh, vmin and vmax count
vg::Affine parse_transform (const char *s);
std::string write_transform (const vg::Affine &m);
std::string url_id (const char *s);		// the id in url(#x), or empty
std::vector<double> parse_numbers (const char *s);
// the value of a property in an element's own style attribute (or its presentation attribute)
std::string get_prop (const XNode *el, const char *prop);
// the style attribute with one property replaced (value nullptr: removed)
std::string style_with (const char *style, const char *prop, const char *value);

}

#endif
