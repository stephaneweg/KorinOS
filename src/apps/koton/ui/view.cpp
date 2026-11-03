//
// view.cpp -- see view.h: the views' tree, their painting, the events, the dialogs, the menus.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "ui/view.h"

namespace kui {

struct ui_vars g_ui;
unsigned g_mods;
int g_cursor;
static int s_ready, s_quit, s_screenW, s_screenH, s_top, s_bottom;
static AppWindow *s_root;
static Dialog *s_dialog;			// the dialog open
static View *s_target;				// the view drawing

static void screen ()
{
	if (s_screenW) return;
	if (!s_ready && uikit_open (&g_ui)) s_ready = 1;
	long scr[4] = { 1024, 768, 0, 0 };
	if (s_ready) ui_screen ((long) scr);
	s_screenW = (int) scr[0]; s_screenH = (int) scr[1]; s_bottom = (int) scr[2]; s_top = (int) scr[3];
}

void screenInfo (ScreenInfo *s)
{
	screen ();
	s->w = s_screenW; s->h = s_screenH;
	s->workW = s_screenW - 8;				// (the frame: its border on both sides)
	s->workH = s_screenH - s_top - s_bottom - 32;		// (its title and border)
}

void quitApp () { s_quit = 1; }

// ---- the theme -----------------------------------------------------------------------------------------------------
unsigned C_BG, C_FACE, C_BORDER, C_TEXT, C_ACCENT, C_FIELD, C_FIELD_TEXT, C_BUTTON;
unsigned N_BG0, N_BG1, N_BG2, N_BG3, N_LINE, N_TEXT1, N_TEXT2, N_TEXT3, N_ACCENT, N_ACCENT_HI, N_DANGER, N_SUCCESS, N_WARNING;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE, J_BARRE, J_DOCK,
       J_TITRE_ACTIF, J_TITRE_INACTIF, J_CONTOUR, J_SOMBRE, J_MENU };

static void loadTheme ()
{
	const unsigned *k = (const unsigned *) ui_jetons ();
	#define T(i) (k[i] & 0x00FFFFFFu)
	bool dark = k[J_SOMBRE] != 0;
	N_BG0 = T (J_SURFACE); N_BG1 = T (J_FACE); N_BG2 = T (dark ? J_CREUX : J_ELEVE); N_BG3 = T (dark ? J_ELEVE : J_CREUX);
	N_LINE = T (J_LIGNE); N_TEXT1 = T (J_TEXTE1); N_TEXT2 = T (J_TEXTE2); N_TEXT3 = T (J_TEXTE3);
	N_ACCENT = T (J_ACCENT); N_ACCENT_HI = T (J_ACCENT_VIF); N_DANGER = T (J_DANGER); N_SUCCESS = T (J_SUCCES); N_WARNING = T (J_ALERTE);
	C_BG = C_FACE = T (J_FACE); C_FIELD = T (J_SURFACE); C_BUTTON = T (J_ELEVE);
	C_ACCENT = T (J_ACCENT); C_BORDER = N_LINE; C_TEXT = C_FIELD_TEXT = N_TEXT1;
	#undef T
}

// ---- text ------------------------------------------------------------------------------------------------------------
Face *g_face;
unsigned *g_px;
int g_pw, g_ph;

int textPx () { return g_face ? g_face->px : 13; }
long textFont (int style)
{
	if (g_face && g_face->mono) return style & 2 ? ui_font_get (UFN_MONOBOLD) : (long) g_ui.font_m;
	switch (style & 3)
	{
	case 1: return ui_font_get (UFN_ITALIC);
	case 2: return (long) g_ui.font_b;
	case 3: return ui_font_get (UFN_BOLDITALIC);
	}
	return (long) g_ui.font;
}

// ---- the tree --------------------------------------------------------------------------------------------------------
View::View (int l, int t, int w, int h)
  : px (0), left (l), top (t), width (w), height (h), scrollX (0), scrollY (0), tag (0), anchor (ANCHOR_LEFT | ANCHOR_TOP), lytW (w), lytH (h),
    valid (false), shouldRedraw (true), hidden (false), hasFocus (false), canFocus (false), catchOutside (false), floating (false),
    parent (0), firstChild (0), lastChild (0), prevSib (0), nextSib (0), prevHandled (0), ctls (0), surface (0), surfW (0), surfH (0)
{}

View::~View ()
{
	View *c = firstChild; while (c) { View *n = c->nextSib; removeChild (c); delete c; c = n; }
	while (ctls) { Ctl *n = ctls->next; ui_w_destroy (ctls->h); free (ctls); ctls = n; }
	if (surface) ui_w_destroy (surface);
}

void View::forget ()
{
	while (ctls) { Ctl *n = ctls->next; free (ctls); ctls = n; }
	surface = 0; px = 0;
	for (View *c = firstChild; c; c = c->nextSib) c->forget ();
}

static void nop_callback (long, long) {}

long View::addCtl (long h, int tg, int x, int y, int w, int ht, int want)
{
	if (!h) return 0;
	Ctl *c = (Ctl *) malloc (sizeof (Ctl));
	c->h = h; c->tag = tg; c->x = x; c->y = y; c->w = w; c->ht = ht; c->anchor = ANCHOR_LEFT | ANCHOR_TOP; c->want = want;
	c->hidden = false; c->nx = c->ny = -1; c->nw = c->nh = 0; c->nshown = true; c->nenabled = c->enabled = true;
	c->next = 0;
	Ctl **p = &ctls; while (*p) p = &(*p)->next;		// (in order: the draws, the Tab key)
	*p = c;
	ui_w_on (h, (long) nop_callback, (long) this);
	invalidate (true);
	return h;
}

Ctl *View::ctl (long h)
{
	for (Ctl *c = ctls; c; c = c->next) if (c->h == h) return c;
	return 0;
}

void View::ctlClear ()
{
	while (ctls) { Ctl *n = ctls->next; ui_w_destroy (ctls->h); free (ctls); ctls = n; }
	invalidate (true);
}

void View::ctlRemove (long h)
{
	for (Ctl **p = &ctls; *p; p = &(*p)->next)
		if ((*p)->h == h) { Ctl *c = *p; *p = c->next; ui_w_destroy (h); free (c); invalidate (true); return; }
}

const char *joinLabels (const char *const *names, int n)
{
	static char b[4096];
	int o = 0;
	b[0] = 0;
	for (int i = 0; i < n; i++) o += snprintf (b + o, sizeof b - (size_t) o, "%s%s", i ? "|" : "", TR (names[i]));
	return b;
}

void View::invalidate (bool redraw)
{
	valid = false;
	if (redraw) shouldRedraw = true;
	if (parent) parent->invalidate (false);
}

void View::resizeTo (int w, int h)
{
	if (w == width && h == height) return;
	width = w; height = h; layout (); invalidate (true);
}

static inline void anchored (int a, int dW, int dH, int *x, int *y, int *w, int *h)
{
	bool L = (a & ANCHOR_LEFT) != 0, R = (a & ANCHOR_RIGHT) != 0, T = (a & ANCHOR_TOP) != 0, B = (a & ANCHOR_BOTTOM) != 0;
	if (R && !L) *x += dW;
	if (B && !T) *y += dH;
	if (L && R) *w += dW;
	if (T && B) *h += dH;
}

void View::layout ()
{
	int dW = width - lytW, dH = height - lytH;
	if (dW == 0 && dH == 0) return;
	for (View *c = firstChild; c; c = c->nextSib)
	{
		int nw_ = c->width, nh_ = c->height;
		anchored (c->anchor, dW, dH, &c->left, &c->top, &nw_, &nh_);
		if (nw_ != c->width || nh_ != c->height) c->resizeTo (nw_, nh_);
	}
	for (Ctl *c = ctls; c; c = c->next) anchored (c->anchor, dW, dH, &c->x, &c->y, &c->w, &c->ht);
	lytW = width; lytH = height;
}

void View::addChild (View *c)
{
	if (c == 0 || c->parent) return;
	c->parent = this; c->prevSib = lastChild; c->nextSib = 0;
	if (lastChild) lastChild->nextSib = c; else firstChild = c;
	lastChild = c;
	layout ();
	invalidate (true);
}

void View::removeChild (View *c)
{
	if (c == 0 || c->parent != this) return;
	if (prevHandled == c) prevHandled = 0;
	if (c->prevSib) c->prevSib->nextSib = c->nextSib; else firstChild = c->nextSib;
	if (c->nextSib) c->nextSib->prevSib = c->prevSib; else lastChild = c->prevSib;
	c->parent = 0; c->prevSib = c->nextSib = 0;
	layout ();
	invalidate (true);
}

void View::bringToFront ()
{ if (parent && nextSib) { View *p = parent; p->removeChild (this); p->addChild (this); } }

bool View::handleMouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	bool handled = false; View *who = 0;
	for (View *n = lastChild; n && !handled; n = n->prevSib)
	{
		if (n->hidden) continue;
		int cmx = mx + scrollX, cmy = my + scrollY;
		bool over = cmx >= n->left && cmy >= n->top && cmx < n->left + n->width && cmy < n->top + n->height;
		if (over || n->catchOutside)
			if (n->handleMouse (cmx - n->left, cmy - n->top, bl, br, bm, wheel)) { handled = true; who = n; }
		if (over) break;
	}
	if (!handled) handled = onMouse (mx, my, bl, br, bm, wheel);
	if (prevHandled && prevHandled != who) prevHandled->handleMouse (-1, -1, 0, 0, 0, 0);
	prevHandled = who;
	return handled;
}

bool View::handleKey (long k)
{
	if (!hasFocus) return false;
	for (View *c = firstChild; c; c = c->nextSib)
		if (!c->hidden && c->hasFocus && c->handleKey (k)) return true;
	return onKey (k);
}

void View::clearFocusTree ()
{
	if (hasFocus) { hasFocus = false; if (canFocus) invalidate (true); }
	for (View *c = firstChild; c; c = c->nextSib) c->clearFocusTree ();
}
void View::focusPathUp ()
{
	if (!hasFocus) { hasFocus = true; if (canFocus) invalidate (true); }
	if (parent) parent->focusPathUp ();
}
void View::setFocus ()
{
	View *r = this; while (r->parent) r = r->parent;
	r->clearFocusTree ();
	focusPathUp ();
	if (s_ready) ui_w_focus (0);			// (the keys: no longer a field's)
}

// ---- painting ----------------------------------------------------------------------------------------------------------
static int s_dx0, s_dy0, s_dx1, s_dy1;			// what changed in the window
static bool s_repaint;
static View *s_float;					// the floating view of the root, its rectangle
static int s_fx0, s_fy0, s_fx1, s_fy1;

// the controls at their place. UIKit does not clip them by their view: too wide for it, they are shortened; outside, hidden
static void sync_ctls (View *v, int ax, int ay, bool shown, int cx0, int cy0, int cx1, int cy1)
{
	if (ax > cx0) cx0 = ax;
	if (ay > cy0) cy0 = ay;
	if (ax + v->width < cx1) cx1 = ax + v->width;
	if (ay + v->height < cy1) cy1 = ay + v->height;
	for (Ctl *c = v->ctls; c; c = c->next)
	{
		int x = ax + c->x - v->scrollX, y = ay + c->y - v->scrollY;
		int vw = x + c->w <= cx1 ? c->w : cx1 - x;
		bool in = shown && !c->hidden && x >= cx0 && y >= cy0 && y + c->ht <= cy1 && vw >= 20;
		if (s_float && x < s_fx1 && x + vw > s_fx0 && y < s_fy1 && y + c->ht > s_fy0) in = false;	// (UIKit would draw it, and take its clicks)
		if (in && (x != c->nx || y != c->ny || vw != c->nw || c->ht != c->nh))
		{
			c->nx = x; c->ny = y; c->nw = vw; c->nh = c->ht;
			ui_w_move (c->h, x, y, vw, c->ht);
		}
		if (in != c->nshown)
		{
			c->nshown = in;
			ui_w_show (c->h, in);
			if (!in) s_repaint = true;		// (what it covered)
		}
		if (c->nenabled != c->enabled) { c->nenabled = c->enabled; ui_w_enable (c->h, c->enabled); }
	}
	for (View *k = v->firstChild; k; k = k->nextSib)
	{
		if (k == s_float) s_float = 0;			// (its own controls; it is the last child)
		sync_ctls (k, ax + k->left - v->scrollX, ay + k->top - v->scrollY, shown && !k->hidden, cx0, cy0, cx1, cy1);
	}
}

static void paint_view (View *v)
{
	if (v->width <= 0 || v->height <= 0) return;
	if (v->surface && (v->surfW != v->width || v->surfH != v->height)) { ui_w_destroy (v->surface); v->surface = 0; }
	if (!v->surface)
	{
		v->surface = ui_canvas (0, 0, v->width, v->height);
		if (!v->surface) return;
		ui_w_show (v->surface, 0);		// (a drawing buffer: the window does not see it)
		v->px = (unsigned *) ui_canvas_pixels (v->surface);
		v->surfW = v->width; v->surfH = v->height;
	}
	View *was = s_target; unsigned *wpx = g_px; int ww = g_pw, wh = g_ph;
	s_target = v; g_px = v->px; g_pw = v->width; g_ph = v->height;
	ui_canvas_begin (v->surface);
	v->onDraw ();
	ui_canvas_end ();
	s_target = was; g_px = wpx; g_pw = ww; g_ph = wh;
}

static void compose (View *v, int ax, int ay, int cx0, int cy0, int cx1, int cy1, bool force)
{
	if (v->valid && !force) return;
	int x0 = ax > cx0 ? ax : cx0, y0 = ay > cy0 ? ay : cy0;
	int x1 = ax + v->width < cx1 ? ax + v->width : cx1, y1 = ay + v->height < cy1 ? ay + v->height : cy1;
	bool mine = v->shouldRedraw || force;
	if (v->shouldRedraw) { paint_view (v); v->shouldRedraw = false; }
	if (mine && v->px && x1 > x0 && y1 > y0)
	{
		for (int y = y0; y < y1; y++)
			memcpy ((unsigned *) g_ui.surf + (long) y * g_ui.w + x0, v->px + (long) (y - ay) * v->width + (x0 - ax), (size_t) (x1 - x0) * 4);
		if (x0 < s_dx0) s_dx0 = x0;
		if (y0 < s_dy0) s_dy0 = y0;
		if (x1 > s_dx1) s_dx1 = x1;
		if (y1 > s_dy1) s_dy1 = y1;
	}
	bool under = false;				// (something under a floating view was painted: it comes back on top)
	for (View *c = v->firstChild; c; c = c->nextSib)
	{
		if (c->hidden) continue;
		if (!c->valid) under = true;
		compose (c, ax + c->left - v->scrollX, ay + c->top - v->scrollY, x0, y0, x1, y1, mine || (c->floating && under));
	}
	if (mine) for (Ctl *c = v->ctls; c; c = c->next) if (c->nshown) ui_w_draw (c->h);
	v->valid = true;
}

static void show_tree (View *v)
{
	s_float = 0;
	for (View *c = v->firstChild; c; c = c->nextSib)
		if (c->floating && !c->hidden) { s_float = c; s_fx0 = c->left; s_fy0 = c->top; s_fx1 = c->left + c->width; s_fy1 = c->top + c->height; }
	sync_ctls (v, 0, 0, true, 0, 0, (int) g_ui.w, (int) g_ui.h);
	s_dx0 = s_dy0 = 1 << 30; s_dx1 = s_dy1 = 0;
	compose (v, 0, 0, 0, 0, (int) g_ui.w, (int) g_ui.h, s_repaint);
	s_repaint = false;
	if (s_dx1 > s_dx0) ui_update (s_dx0, s_dy0, s_dx1 - s_dx0, s_dy1 - s_dy0);
}

void View::draw () { if (!valid && !s_dialog) show_tree (this); }

// ---- events --------------------------------------------------------------------------------------------------------------
static int s_buttons, s_mx, s_my;
static bool s_ownDrag;					// a press taken by a view: the drag stays its own
static int s_curShown = -1;

static void cursor_show ()
{
	if (g_cursor == s_curShown) return;
	s_curShown = g_cursor;
	ui_cursor (s_curShown);
}

static unsigned hid_mods (int m)
{
	unsigned r = 0;
	if (m & 0x11) r |= MOD_CTRL;
	if (m & 0x22) r |= MOD_SHIFT;
	if (m & 0x44) r |= MOD_ALT;
	return r;
}

static long key_code (int hid, int ch, unsigned mods)	// HID code, character -> K_*, a character, 0: none
{
	if ((mods & MOD_CTRL) && hid >= 0x04 && hid <= 0x1D) return hid - 0x04 + 1;
	switch (hid)
	{
	case 0x28: case 0x58: return K_ENTER;
	case 0x29: return 27;
	case 0x2A: return K_BACKSPACE;
	case 0x2B: return K_TAB;
	case 0x4C: return K_DEL;
	case 0x4F: return K_RIGHT;
	case 0x50: return K_LEFT;
	case 0x51: return K_DOWN;
	case 0x52: return K_UP;
	case 0x4A: return K_HOME;
	case 0x4D: return K_END;
	case 0x4B: return K_PGUP;
	case 0x4E: return K_PGDN;
	}
	if (hid >= 0x3A && hid <= 0x45) return K_F1 + (hid - 0x3A);
	return ch >= 32 ? ch : 0;
}

// ui_w_event gives three values: widget, UN_*, taken (the generated stubs give one)
static long w_event (long *code, long *taken)
{
	register long x0 __asm__ ("x0") = 0;
	register long x1 __asm__ ("x1");
	register long x2 __asm__ ("x2");
	__asm__ volatile ("bl ui_w_event" : "+r" (x0), "=r" (x1), "=r" (x2) : : "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory", "cc");
	*code = x1; *taken = x2;
	return x0;
}

static int drop_get (int *kind, void *buf, unsigned cap)
{
	register long x0 __asm__ ("x0") = (long) buf;
	register long x1 __asm__ ("x1") = cap;
	__asm__ volatile ("bl ui_drop_get" : "+r" (x0), "+r" (x1) : : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory", "cc");
	if (kind) *kind = (int) x1;
	return (int) x0;
}

// an event of UIKit to the controls, then to the tree top
static void dispatch (long t, View *top)
{
	int kind = ui_ev_field (&g_ui, EV_KIND), x = ui_ev_field (&g_ui, EV_X), y = ui_ev_field (&g_ui, EV_Y), code = ui_ev_field (&g_ui, EV_CODE);
	if (t == WEV_KEY) g_mods = hid_mods (ui_ev_field (&g_ui, EV_MODS));
	long taken = 0, un = 0, w = 0;
	if (!(s_ownDrag && t == WEV_MOUSE)) w = w_event (&un, &taken);
	if (w && un)
	{
		View *me = (View *) ui_w_user (w);
		Ctl *c = me ? me->ctl (w) : 0;
		if (c && (!c->want || c->want == un)) me->onControl (w, c->tag, (int) un);
	}
	if (taken)
	{
		if (t == WEV_MOUSE) { top->handleMouse (-1, -1, 0, 0, 0, 0); s_curShown = -1; }
		return;
	}
	switch (t)
	{
	case WEV_MOUSE:
	{
		s_mx = x; s_my = y;
		int held = code & 7;
		if (kind == 1 && !(held ^ s_buttons)) held |= 1;
		else if (kind == 2 && !(held ^ s_buttons)) held = 0;
		s_buttons = held;
		s_ownDrag = held != 0;
		g_cursor = UC_ARROW;
		top->handleMouse (x, y, held & 1, (held >> 1) & 1, (held >> 2) & 1, 0);
		cursor_show ();
		break;
	}
	case WEV_WHEEL:
		top->handleMouse (s_mx, s_my, s_buttons & 1, (s_buttons >> 1) & 1, (s_buttons >> 2) & 1, y > 0 ? 1 : y < 0 ? -1 : 0);
		break;
	case WEV_KEY:
		if (kind != 0)
		{
			long k = key_code (code, ui_ev_field (&g_ui, EV_CHAR), g_mods);
			if (!k) break;
			if (top == s_root && MenuBar::current () && MenuBar::current ()->shortcut (k)) break;
			top->handleKey (k);
		}
		break;
	case WEV_MENU:
		if (top == s_root && MenuBar::current ()) MenuBar::current ()->command (code);
		break;
	case WEV_DROP:
		if (top == s_root)
		{
			static char data[4097];
			int type = 0, n = drop_get (&type, data, sizeof data - 1);
			if (n < 0) n = 0;
			data[n] = 0;
			s_root->onDrop (x, y, type == UDT_FILES, data, n);
		}
		break;
	}
}

// ---- the window ------------------------------------------------------------------------------------------------------
AppWindow *AppWindow::current () { return s_root; }

static void on_resize (long w, long h)
{
	if (!s_root) return;
	s_root->resizeTo ((int) w, (int) h);
	s_root->onResized ();
	s_root->invalidate (true);
	for (View *c = s_root->firstChild; c; c = c->nextSib) c->invalidate (true);
}

AppWindow::AppWindow (int w, int h, const char *title) : View (0, 0, w, h), bg (0)
{
	screen ();
	loadTheme ();
	bg = C_BG;
	s_root = this;
	hasFocus = true;
	if (s_ready && ui_window_ex (0, s_top, w, h, (long) title, WF_RESIZABLE) > 0) ui_on_resize ((long) on_resize, 0);
	else s_quit = 1;
}

void AppWindow::run ()
{
	while (!s_quit)
	{
		long t = ui_event (16);
		if (t == WEV_CLOSE) { s_quit = 1; break; }
		if (t > 0) dispatch (t, this);
		else ui_w_idle ();
		for (int budget = 64; budget > 0 && t > 0 && !s_quit; budget--)	// (what is already waiting)
		{
			t = ui_event (0);
			if (t == WEV_CLOSE) s_quit = 1;
			else if (t > 0) dispatch (t, this);
		}
		onTick ();
		draw ();
	}
}

// ---- dialogs ---------------------------------------------------------------------------------------------------------------
Dialog::Dialog (int w, int h, const char *title) : View (0, 0, w, h), done (false), result (0), m_dlg (0)
{
	hasFocus = true;
	m_dlg = ui_dialog_begin (w, h, (long) title);
}

// (the controls are read after run: the window goes with the object)
Dialog::~Dialog ()
{
	ui_dialog_end (m_dlg);
	forget ();					// (ui_dialog_end destroyed its controls)
}

int Dialog::run ()
{
	Dialog *was = s_dialog;
	View *keep = s_target;
	s_dialog = this;
	done = false;
	shouldRedraw = true; valid = false;
	for (View *c = firstChild; c; c = c->nextSib) c->invalidate (true);
	show_tree (this);
	ui_update_all ();
	long mine = g_ui.win;
	while (!done && !s_quit)
	{
		long t = ui_event (16);
		if (t > 0)
		{
			if (ui_ev_field (&g_ui, EV_WIN) != (int) mine)
			{
				if (t == WEV_MOUSE && ui_ev_field (&g_ui, EV_KIND) == 1) ui_raise (mine);
			}
			else if (t == WEV_CLOSE) close (0);
			else dispatch (t, this);
		}
		else ui_w_idle ();
		if (!valid) show_tree (this);
	}
	s_dialog = was;
	s_target = keep;
	if (s_root) s_root->invalidate (true);
	return result;
}

int Popup::run ()
{
	static char b[1024];
	int o = 0, w = 120;
	for (int i = 0; i < m_n; i++)
	{
		int n = (int) strlen (m_label[i]);
		if (o + n + 2 >= (int) sizeof b) { m_n = i; break; }
		memcpy (b + o, m_label[i], (size_t) n + 1); o += n + 1;
		int t = tw (m_label[i]) + 56; if (t > w) w = t;
	}
	b[o] = 0;
	if (!m_n) return -1;
	long r = ui_popup_local (m_x, m_y, (long) b, w);
	return r >= 0 && r < m_n ? m_id[r] : -1;
}

// « Songs|*.kson;*.sq|All|* » : the patterns of the first kind (ui_file_filter)
static void file_filter (const char *filters)
{
	static char b[128];
	int n = 0;
	const char *p = filters ? strchr (filters, '|') : 0;
	if (p) for (p++; *p && *p != '|' && n < (int) sizeof b - 1; p++) b[n++] = *p;
	b[n] = 0;
	ui_file_filter (n && strcmp (b, "*") ? (long) b : 0);
}
bool fileOpen (char *out, unsigned cap, const char *startDir, const char *filters)
{
	out[0] = 0;
	file_filter (filters);
	bool r = ui_file_open ((long) startDir, (long) out, cap) == 1;
	ui_file_filter (0);
	return r;
}
bool fileSave (char *out, unsigned cap, const char *startDir, const char *defName, const char *filters)
{
	out[0] = 0;
	file_filter (filters);
	bool r = ui_file_save ((long) startDir, (long) defName, (long) out, cap) == 1;
	ui_file_filter (0);
	return r;
}

// ---- the menu bar: ui_menu's text ----------------------------------------------------------------------------------------
static MenuBar *s_menu;
MenuBar *MenuBar::current () { return s_menu; }
void MenuBar::put (const char *s)
{
	for (int i = 0; s[i] && m_len < (int) sizeof m_spec - 1; i++) m_spec[m_len++] = s[i];
	m_spec[m_len] = 0;
}
void MenuBar::menu (const char *title) { put ("M"); put (title); put ("\n"); }
void MenuBar::separator () { put ("-\n"); }
void MenuBar::item (const char *label, const char *shortcutText, long key, MenuAction cb)
{
	if (m_count >= MAXITEMS) return;
	char id[16];
	snprintf (id, sizeof id, "%d", m_count);
	put ("I"); put (id); put ("\t"); put (label); put ("\t"); put (shortcutText ? TR (shortcutText) : ""); put ("\n");
	m_cb[m_count] = cb; m_key[m_count] = key; m_count++;
}
void MenuBar::publish () { s_menu = this; ui_menu ((long) m_spec); }
bool MenuBar::shortcut (long key)
{
	if (key == KEY_CTRL ('Q')) { s_quit = 1; return true; }
	if (key <= 0) return false;
	if ((key == K_BACKSPACE || key == K_TAB || key == K_ENTER) && !(g_mods & MOD_CTRL)) return false;
	for (int i = 0; i < m_count; i++)
		if (m_key[i] == key && m_cb[i]) { m_cb[i] (); if (s_root) s_root->invalidate (true); return true; }
	return false;
}
void MenuBar::command (int id)
{
	if (id < 0 || id >= m_count || !m_cb[id]) return;
	m_cb[id] ();
	if (s_root) s_root->invalidate (true);
}

// ---- the game pad's hints --------------------------------------------------------------------------------------------------
class HintBar : public View
{
public:
	char text[256];
	HintBar () : View (0, 0, 0, 24) { text[0] = 0; }
	void onDraw () override
	{
		ui_fill (0, 0, width, height, N_BG0);
		ui_pad_hints ((long) text, 8, 2, 20, 0);
	}
};
static HintBar *s_hints;
int hintLine (View *root, const char *text)
{
	if (!s_hints) { s_hints = new HintBar; root->addChild (s_hints); }
	if (strcmp (text, s_hints->text)) { snprintf (s_hints->text, sizeof s_hints->text, "%s", text); s_hints->invalidate (true); }
	s_hints->left = 0; s_hints->top = root->height - 24; s_hints->resizeTo (root->width, 24);
	return 24;
}
int hintLineHeight () { return s_hints ? 24 : 0; }
int padGlyph (int x, int y, int h, int kind, bool measure)
{
	char b[8];
	snprintf (b, sizeof b, "%c\t", kind);
	return (int) ui_pad_hints ((long) b, x, y, h, measure);
}

} // namespace kui
