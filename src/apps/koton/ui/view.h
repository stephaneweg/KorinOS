//
// ui/view.h -- Koton's window on UIKit (src/kits/uikit, called directly). A hand-drawn part of the window is a View: it
// draws with the ui_* calls into a hidden ui_canvas, copied into the window under the controls, and gets the mouse and the
// keys. A control is a UIKit widget, a plain handle: the code that makes it calls ui_pushbutton, ui_dropdown..., hands it
// to its View (addCtl: its place, its tag) and reads it with ui_w_get_value; its changes arrive in View::onControl.
//
#ifndef _koton_view_h
#define _koton_view_h

#include <stdint.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <uikit.h>
#include "ui/sys.h"

namespace kui {

extern struct ui_vars g_ui;
extern unsigned g_mods;				// the modifiers held (MOD_*)
extern int g_cursor;				// the shape a view asks for (UC_*), from onMouse

enum { MOD_CTRL = 1, MOD_SHIFT = 2, MOD_ALT = 4 };
enum { K_BACKSPACE = 8, K_TAB = 9, K_ENTER = 13, K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_DEL,
	K_F1 = 0x110, K_F12 = 0x11B };
enum { SOUND_RATE = 44100 };
#define KEY_CTRL(c)	((long) ((c) & 0x1F))	// Ctrl + a letter (^A = 1 ... ^Z = 26)
#define TR(s)		((const char *) ui_tr ((long) (s)))

// ---- the theme's colours (0x00RRGGBB), loaded with the window ----------------------------------------------------------
extern unsigned C_BG, C_FACE, C_BORDER, C_TEXT, C_ACCENT, C_FIELD, C_FIELD_TEXT, C_BUTTON;
extern unsigned N_BG0, N_BG1, N_BG2, N_BG3, N_LINE, N_TEXT1, N_TEXT2, N_TEXT3, N_ACCENT, N_ACCENT_HI, N_DANGER, N_SUCCESS, N_WARNING;

// ---- text: the size of the text drawn, its family ------------------------------------------------------------------
struct Face { int px; bool mono; };
extern Face *g_face;				// 0: the interface's, 13 px
struct FaceScope
{
	Face *keep;
	FaceScope (Face *f) : keep (g_face) { if (f) g_face = f; }
	~FaceScope () { g_face = keep; }
};
int textPx ();
long textFont (int style);			// style: 1 italic, 2 bold
static inline int th () { return (textPx () * 5 + 1) / 4; }
static inline int tw (const char *s, int style = 0) { return s && *s ? (int) ui_text_width ((long) s, textPx (), textFont (style)) : 0; }

// the pixels being drawn (the view's canvas), for what UIKit has no call for
extern unsigned *g_px;
extern int g_pw, g_ph;

// ---- views -----------------------------------------------------------------------------------------------------------
enum { ANCHOR_LEFT = 1, ANCHOR_TOP = 2, ANCHOR_RIGHT = 4, ANCHOR_BOTTOM = 8, ANCHOR_FILL = 15 };

struct Ctl					// a control in a view
{
	long h;
	int tag, x, y, w, ht, anchor, want;
	bool hidden;
	int nx, ny, nw, nh; bool nshown, nenabled, enabled;	// (what UIKit was told)
	Ctl *next;
};

class View
{
public:
	unsigned *px;					// (its canvas)
	int	 left, top, width, height, scrollX, scrollY, tag, anchor, lytW, lytH;
	bool	 valid, shouldRedraw, hidden, hasFocus, canFocus, catchOutside;
	bool	 floating;				// over its younger brothers: their controls under it are hidden
	View	*parent, *firstChild, *lastChild, *prevSib, *nextSib, *prevHandled;
	Ctl	*ctls;
	long	 surface;
	int	 surfW, surfH;

	View (int l, int t, int w, int h);
	virtual ~View ();
	void invalidate (bool redraw);
	virtual void resizeTo (int w, int h);
	void addChild (View *c);
	void removeChild (View *c);
	void bringToFront ();
	void draw ();
	bool handleMouse (int mx, int my, int bl, int br, int bm, int wheel);
	bool handleKey (long k);
	void clearFocusTree ();
	void focusPathUp ();
	void setFocus ();
	virtual void onDraw () {}
	virtual bool onMouse (int, int, int, int, int, int) { return false; }
	virtual bool onKey (long) { return false; }
	virtual unsigned bgColor () { return parent ? parent->bgColor () : C_BG; }
	virtual void layout ();

	// the controls: a handle of UIKit given its place in this view; tag: what onControl gets
	long addCtl (long h, int tag, int x, int y, int w, int ht, int want = 0);	// want: the UN_* that reaches onControl, 0: all, -1: none
	Ctl *ctl (long h);
	void ctlAnchor (long h, int a) { Ctl *c = ctl (h); if (c) c->anchor = a; }
	void ctlShow (long h, bool on) { Ctl *c = ctl (h); if (c) c->hidden = !on; invalidate (true); }
	void ctlEnable (long h, bool on) { Ctl *c = ctl (h); if (c) c->enabled = on; invalidate (true); }
	void ctlRemove (long h);
	void ctlClear ();
	virtual void onControl (long w, int tag, int code) { (void) w; (void) tag; (void) code; }
	void forget ();					// (ui_dialog_end destroyed its controls)
};

// the labels of a drop-down or a segmented control: « a|b|c » (a shared buffer)
const char *joinLabels (const char *const *names, int n);

class Panel : public View
{
public:
	unsigned bg;
	Panel (int l, int t, int w, int h, unsigned bg_ = 0) : View (l, t, w, h), bg (bg_) {}
	unsigned bgColor () override { return bg; }
	void onDraw () override { ui_fill (0, 0, width, height, bg); }
};

class AppWindow : public View
{
public:
	unsigned bg;
	AppWindow (int w, int h, const char *title);
	void setBg (unsigned c) { bg = c; invalidate (true); }
	unsigned bgColor () override { return bg; }
	void onDraw () override { ui_fill (0, 0, width, height, bg); }
	void run ();
	virtual void onTick () {}
	virtual void onResized () {}
	virtual void onDrop (int x, int y, bool files, const char *data, int len) { (void) x; (void) y; (void) files; (void) data; (void) len; }
	static AppWindow *current ();
};

// a dialog: its window (ui_dialog_begin) opened at once, so that its controls can be made in the constructors
class Dialog : public View
{
public:
	bool done; int result;
	Dialog (int w, int h, const char *title);
	~Dialog ();
	int run ();
	void close (int r) { result = r; done = true; }
	virtual void onButton (int tag) { (void) tag; }
	unsigned bgColor () override { return C_FACE; }
	void onDraw () override { ui_fill (0, 0, width, height, C_FACE); }
	void onControl (long w, int tag, int code) override { (void) w; if (code == UN_ACTIVATE) onButton (tag); }
private:
	long m_dlg;
};

// a menu at a point of the window -> the id chosen, -1: none
class Popup
{
	enum { MAXI = 16 };
	const char *m_label[MAXI]; int m_id[MAXI], m_n, m_x, m_y;
public:
	Popup (int x, int y) : m_n (0), m_x (x), m_y (y) {}
	void add (const char *label, int id, bool enabled = true) { if (m_n < MAXI && enabled) { m_label[m_n] = TR (label); m_id[m_n] = id; m_n++; } }
	void separator () {}
	int run ();
};

// the menu bar: ui_menu's text, and what each item does
typedef void (*MenuAction) ();
class MenuBar
{
public:
	MenuBar () : m_len (0), m_count (0) { m_spec[0] = 0; }
	void menu (const char *title);
	void item (const char *label, const char *shortcutText, long key, MenuAction cb);
	void separator ();
	void publish ();
	bool shortcut (long key);
	void command (int id);
	static MenuBar *current ();
private:
	enum { MAXITEMS = 64 };
	char m_spec[2048]; int m_len, m_count;
	MenuAction m_cb[MAXITEMS]; long m_key[MAXITEMS];
	void put (const char *s);
};

bool fileOpen (char *out, unsigned cap, const char *startDir, const char *filters = 0);
bool fileSave (char *out, unsigned cap, const char *startDir, const char *defName, const char *filters = 0);
void quitApp ();
static inline int message (const char *title, const char *text, int um) { return (int) ui_message ((long) TR (title), (long) TR (text), um); }

// the screen: its size and the area left to windows
struct ScreenInfo { int w, h, workW, workH; };
void screenInfo (ScreenInfo *s);

// the game pad's hints (PocketTerm): one line of ui_pad_hints at the bottom of the window
int hintLine (View *root, const char *text);		// « glyphs\tword\n... » -> its height
int hintLineHeight ();
int padGlyph (int x, int y, int h, int kind, bool measure = false);

} // namespace kui

#endif
