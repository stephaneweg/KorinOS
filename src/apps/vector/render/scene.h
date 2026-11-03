//
// scene.h -- what the tree looks like: an immutable tree of drawing nodes built from the document on the main thread and
// read by the tile threads. A node never changes once built; a change builds new nodes for what it touched and shares
// the rest, so a thread drawing the old scene is never disturbed.
//
#ifndef _vector_scene_h
#define _vector_scene_h

#include "../doc/document.h"
#include "../doc/style.h"
#include "../geom/path.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkData.h"
#include "include/core/SkImageFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPicture.h"
#include "include/core/SkRSXform.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkTextBlob.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vr {

struct DNode;
using DRef = std::shared_ptr<const DNode>;

enum DKind : uint8_t { D_GROUP, D_SHAPE, D_TEXT, D_IMAGE };

struct TextPiece
{
	sk_sp<SkTextBlob> blob;
	SkPoint at { 0, 0 };
	SkPaint fill, stroke;
	bool hasFill = false, hasStroke = false;
	uint8_t order[3] = { 0, 1, 2 };
	// (for text to path and text in a clip)
	sk_sp<SkTypeface> face;
	float size = 16;
	std::vector<uint16_t> glyphs;
	std::vector<SkRSXform> xf;
};
SkPath text_outline (const std::vector<TextPiece> &pieces);

struct DNode
{
	DKind kind = D_GROUP;
	uint32_t uid = 0;
	SkMatrix m = SkMatrix::I ();
	SkRect bounds = SkRect::MakeEmpty ();	// what it paints, in its parent's coordinates
	float opacity = 1;
	SkBlendMode blend = SkBlendMode::kSrcOver;
	sk_sp<SkImageFilter> filter;
	bool hasClip = false, hasClipRect = false;
	SkPath clip;				// in the node's coordinates
	SkRect clipRect = SkRect::MakeEmpty ();
	DRef mask;				// luminance
	// shape
	SkPath path;
	SkPaint fill, stroke;
	bool hasFill = false, hasStroke = false;
	uint8_t order[3] = { 0, 1, 2 };
	std::vector<DRef> kids;			// a group's; a shape's markers
	// text
	std::vector<TextPiece> pieces;
	bool vertical = false;
	SkPoint verticalAt { 0, 0 };
	// image
	sk_sp<SkImage> image;
	sk_sp<SkPicture> picture;		// an SVG in an <image>
	SkRect dst = SkRect::MakeEmpty ();
	bool pixelated = false;
};

// the tools' view of an element of the scene
struct ObjInfo
{
	DRef node;
	vg::Box visual;				// in the document's user units (root element's coordinates, viewBox applied)
	vg::Box geom;
};

struct Scene
{
	DRef root;
	double width = 0, height = 0;		// the page, in px
	vg::Affine docToPx;			// user units -> px (the root's viewBox)
	uint32_t page = 0xFFFFFF;
	float pageOpacity = 1;
	std::unordered_map<uint32_t, ObjInfo> objs;
};

class SceneBuilder
{
public:
	explicit SceneBuilder (vd::Document *d) : doc (d) {}
	// the scene as of now; what did not change since the last call is shared
	std::shared_ptr<const Scene> update ();
	void invalidate () { memo.clear (); }
	// the area that the last update changed, in px (empty: nothing; infinite when everything may have)
	vg::Box lastDamage;
	bool lastAll = true;

	vd::Document *doc;
	vd::StyleSheet sheet;
	double vpW = 100, vpH = 100;		// the current viewport, for percentages
	double dpi = 96;

	// ---- used by text/layout.cpp and render/filter.cpp too
	bool resolve_paint (const vd::PaintSpec &spec, float opacity, const vd::Style &st, const SkRect &bbox, SkPaint &out);
	void setup_stroke (const vd::Style &st, SkPaint &p);
	void set_context (const vd::PaintSpec *f, const vd::PaintSpec *s) { ctxFill = f; ctxStroke = s; }
	vd::Style inherited_style (const vd::XNode *el);
	sk_sp<SkImage> load_image (const char *href);
	sk_sp<SkData> image_data (const char *href);
	sk_sp<SkPicture> picture_of (vd::XNode *el, const SkRect &area);
	vg::Affine element_transform (const vd::XNode *el, const vd::Style &st);

private:
	struct Memo { DRef node; };
	std::unordered_map<uint32_t, Memo> memo;
	std::unordered_set<uint32_t> dirty, forced;
	std::unordered_set<std::string> usedIds;
	std::shared_ptr<Scene> cur;
	std::unordered_set<const vd::XNode *> stack;
	int inDefs = 0;
	const vd::PaintSpec *ctxFill = nullptr, *ctxStroke = nullptr;
	std::unordered_map<uint32_t, vg::Box> oldBoxes;

	DRef build (vd::XNode *el, const vd::Style &parent, const vg::Affine &ctm, bool force);
	DRef build_shape (vd::XNode *el, const vd::Style &st, const SkMatrix &m, const vg::Affine &ctm);
	DRef build_group (vd::XNode *el, const vd::Style &st, const vg::Affine &ctm, const SkMatrix &m, bool force);
	DRef build_use (vd::XNode *el, const vd::Style &st, const vg::Affine &ctm, const SkMatrix &m);
	DRef build_image (vd::XNode *el, const vd::Style &st, const SkMatrix &m);
	DRef build_svg (vd::XNode *el, const vd::Style &st, const vg::Affine &ctm, const SkMatrix &m, bool outer, bool force, double useW, double useH);
	void effects (DNode &n, vd::XNode *el, const vd::Style &st, const SkRect &own);
	void markers (DNode &n, const vg::Path &p, const vd::Style &st);
	bool clip_path (const std::string &id, const SkRect &bbox, SkPath &out);
	void clip_collect (vd::XNode *el, const vd::Style &st, const SkMatrix &m, SkPath &out, bool &any);
	sk_sp<SkShader> gradient (vd::XNode *g, float opacity, const SkRect &bbox);
	sk_sp<SkShader> pattern (vd::XNode *g, float opacity, const SkRect &bbox);
	sk_sp<SkShader> mesh (vd::XNode *g, float opacity, const SkRect &bbox);
	void record (DRef n, vd::XNode *el, const vg::Affine &ctm);
	void note_refs (vd::XNode *el);
};

// the drawing, on any thread
void draw_node (SkCanvas *c, const DNode &n);
void draw_scene (SkCanvas *c, const Scene &s);
void draw_outlines (SkCanvas *c, const Scene &s);

// the local element name ("svg:path" -> "path")
const char *el_name (const vd::XNode *n);
// the viewBox transform of SVG (preserveAspectRatio)
vg::Affine viewbox_transform (const double vb[4], const char *par, double x, double y, double w, double h);
bool parse_viewbox (const char *s, double vb[4]);
inline SkMatrix to_sk (const vg::Affine &m) { return SkMatrix::MakeAll ((float) m.a, (float) m.c, (float) m.e, (float) m.b, (float) m.d, (float) m.f, 0, 0, 1); }
SkPath to_skpath (const vg::Path &p, bool evenodd);
void init_codecs ();

// text/layout.cpp
void layout_text (SceneBuilder &b, vd::XNode *el, const vd::Style &st, DNode &n);
// render/filter.cpp
sk_sp<SkImageFilter> build_filter (SceneBuilder &b, vd::XNode *f, const vd::Style &st, const SkRect &bbox, SkRect *region);
sk_sp<SkImageFilter> filter_functions (SceneBuilder &b, const vd::Style &st, const SkRect &bbox, SkRect *region);

}

#endif
