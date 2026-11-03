//
// window.cpp -- the window and its loop. The drawing lives on a surface that Elegant composes directly; a view of the same
// size under it takes the pointer. Nothing here waits for the tiles: each round shows what is ready.
//
#include "app.h"
#include "ui/editor.h"
#include "text/fonts.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace vd;
using namespace vr;

namespace ve {

struct ui_vars ui;
const unsigned *s_jet;

long chrome_canvas_view ();
bool chrome_field (long w, long code);
SkColor sk (unsigned rgb, float a = 1);

static long s_surf;
static uint32_t *s_px;
static int s_stride, s_cx, s_cy;
static std::vector<uint32_t> s_base, s_back;
static ViewState s_shown;
static Vec2 s_mouse;
static bool s_more, s_spaceHand;

static uint32_t *surface_pixels (long s, long *pitch)
{
	long r1 = 0, r2 = 0;
	long p = uikit_call3 ((long (*) ()) ui_surface_pixels, s, 0, 0, 0, &r1, &r2);
	*pitch = r1;
	return (uint32_t *) p;
}

void canvas_place (int x, int y, int w, int h)
{
	if (w < 1 || h < 1) return;
	bool same = w == E.cw && h == E.ch;
	if (!s_surf) s_surf = ui_surface (x, y, w, h, WS_SF_XRGB8888);
	else if (!ui_surface_move (s_surf, x, y, w, h))
	{
		ui_surface_free (s_surf);
		s_surf = ui_surface (x, y, w, h, WS_SF_XRGB8888);
	}
	long pitch = 0;
	s_px = s_surf ? surface_pixels (s_surf, &pitch) : nullptr;
	s_stride = (int) (pitch / 4);
	s_cx = x; s_cy = y;
	// the view keeps its centre
	if (!same && E.cw) { E.view.ox += (w - E.cw) / 2; E.view.oy += (h - E.ch) / 2; }
	E.cw = w; E.ch = h;
	s_base.assign ((size_t) w * h, 0xFF000000 | jet (J_CREUX));
	s_back.assign ((size_t) w * h, 0);
	s_shown = ViewState ();
	E.dirty = true;
}

static void present ()
{
	if (!s_px || !E.tiles) return;
	E.view.desk = jet (J_CREUX);
	E.view.outline = E.outline;
	int w = E.cw, h = E.ch;
	// the old picture, moved and scaled, stands in for the tiles not ready yet
	if (s_shown.zoom != E.view.zoom || s_shown.ox != E.view.ox || s_shown.oy != E.view.oy)
	{
		double k = E.view.zoom / s_shown.zoom;
		SkImageInfo ii = SkImageInfo::MakeN32Premul (w, h);
		sk_sp<SkSurface> b = SkSurfaces::WrapPixels (ii, s_back.data (), (size_t) w * 4);
		SkCanvas *c = b->getCanvas ();
		c->clear (sk (E.view.desk));
		c->translate ((float) (E.view.ox - s_shown.ox * k), (float) (E.view.oy - s_shown.oy * k));
		c->scale ((float) k, (float) k);
		SkPixmap pm (ii, s_base.data (), (size_t) w * 4);
		c->drawImage (SkImages::RasterFromPixmap (pm, nullptr, nullptr), 0, 0, SkSamplingOptions (SkFilterMode::kLinear));
		s_base.swap (s_back);
		s_shown = E.view;
	}
	bool done = E.tiles->compose (E.view, s_base.data (), w, h, w);
	for (int y = 0; y < h; y++) memcpy (s_px + (long) y * s_stride, s_base.data () + (long) y * w, (size_t) w * 4);
	sk_sp<SkSurface> o = SkSurfaces::WrapPixels (SkImageInfo::MakeN32Premul (w, h), s_px, (size_t) s_stride * 4);
	SkCanvas *c = o->getCanvas ();
	// the page's edge
	Vec2 a = E.to_screen ({ 0, 0 }), z = E.to_screen ({ E.scene->width, E.scene->height });
	SkPaint e;
	e.setStyle (SkPaint::kStroke_Style);
	e.setColor (sk (jet (J_LIGNE_FORTE)));
	c->drawRect (SkRect::MakeLTRB ((float) a.x - .5f, (float) a.y - .5f, (float) z.x + .5f, (float) z.y + .5f), e);
	paint_overlay (c);
	if (!E.message.empty () && E.msgUntil > 0) paint_badge (c, { 4, E.ch - 40.0 }, E.message.c_str ());
	ui_surface_damage (s_surf, 0, 0, w, h);
	s_more = !done;
}

static int mods_of (int m)
{
	int r = 0;
	if (m & UI_MOD_SHIFT) r |= MOD_SHIFT;
	if (m & UI_MOD_CTRL) r |= MOD_CTRL;
	if (m & 0x04) r |= MOD_ALT;			// (left Alt only: right Alt types)
	return r;
}
static Tool *active () { return E.tools[s_spaceHand ? (int) T_HAND : E.tool]; }

static void canvas_paint (long, long w, long h) { ui_fill (0, 0, w, h, jet (J_CREUX)); }
static long canvas_mouse (long, long kind, long x, long y, long info)
{
	Pointer q;
	q.s = { (double) x, (double) y };
	q.p = E.to_page (q.s);
	q.clicks = (int) (info & 0xFF);
	q.button = (int) ((info >> 8) & 0xFF);
	q.mods = mods_of ((int) ((info >> 16) & 0xFF));
	s_mouse = q.s;
	static bool down;
	switch (kind)
	{
	case 1:
		down = true;
		ui_w_focus (chrome_canvas_view ());
		if (q.button == 4 || q.button == 2) { /* (the middle button pans, below) */ }
		active ()->press (q);
		break;
	case 2: down = false; active ()->release (q); break;
	case 0: if (down) active ()->move (q); else active ()->hover (q); break;
	}
	return 0;
}
static long canvas_wheel (long, long n)
{
	int m = mods_of (ui_ev_field (&ui, EV_MODS));
	if (m & MOD_SHIFT) { E.view.ox += n * 32.0; E.dirty = true; }
	else if (m & MOD_CTRL) { E.view.oy += n * 32.0; E.dirty = true; }
	else zoom_at (n > 0 ? 1.25 : 0.8, s_mouse);
	return 0;
}
void canvas_handlers (long *p, long *m, long *w) { *p = (long) canvas_paint; *m = (long) canvas_mouse; *w = (long) canvas_wheel; }

}

namespace vapp {

using namespace ve;

int run_window (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{
		char lang[80];
		snprintf (lang, sizeof lang, "SD0:/apps/vector.app/lang/%s.txt", (const char *) ui_lang ());
		ui_tr_load ((long) lang);
	}
	long scr[4] = { 800, 600, 0, 0 };
	ui_screen ((long) scr);
	// (a dock that reserves nothing floats over the foot of the desktop: the window stays above it)
	int ww = (int) scr[0] - 8, wh = (int) (scr[1] - scr[2] - scr[3]) - 4 - (scr[2] ? 0 : 56);
	if (ui_window_ex (0, scr[3], ww, wh, (long) "Vector", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (480, 360);

	E.doc.create_blank (210, 297);
	SceneBuilder sb (&E.doc);
	E.builder = &sb;
	E.scene = sb.update ();
	TileCache tc (3);
	E.tiles = &tc;
	tc.set_scene (E.scene, nullptr);
	vr::fonts_scan_async ();
	make_tools ();
	chrome_build ();
	build_menu ();
	ui_on_resize ((long) (void (*) (long, long)) [] (long, long) { chrome_layout (); }, 0);
	chrome_layout ();
	if (argc > 1) load_file (argv[1]);
	fit_page ();
	ui_w_focus (chrome_canvas_view ());
	E.tools[E.tool]->enter ();

	for (;;)
	{
		long t = ui_event (s_more ? 8 : 40);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool isKey = t == WEV_KEY;
			bool press = isKey && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			if (isKey && hid == 0x2C && !press) s_spaceHand = false;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) chrome_field (w, code);
			if (press && !taken)
			{
				if (hid == 0x2C && E.tool != T_TEXT) s_spaceHand = true;
				else handle_key (hid, ch, mods_of (mods));
			}
			if (t == WEV_FOCUS) { s_jet = (const unsigned *) ui_jetons (); chrome_paint_all (); }
		}
		else
		{
			ui_w_idle ();
			if (E.msgUntil > 0 && --E.msgUntil == 0) E.dirty = true;
		}
		E.sync ();
		if (tc.pending ()) E.dirty = true;
		if (s_more) E.dirty = true;
		if (E.dirty) { E.dirty = false; present (); }
		chrome_refresh ();
		ui_view_flush ();
	}
	return 0;
}

}
