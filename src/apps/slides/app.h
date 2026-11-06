//
// app.h -- what the window's parts share: UIKit's variables, the theme (its tokens, never theme.txt), the sizes, the
// clock, the keys, the buffer a popup list is written into. The deck's text is UTF-8, as UIKit's: no crossing.
//
#ifndef _slides_app_h
#define _slides_app_h

#include <time.h>
#include "editor.h"

namespace sl {

static struct ui_vars ui;

enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

// The heights of UIKit's scales (uk_mt_tab): a control's, the interface's text.
static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)
static int ui_px () { return ui_echelle () >= 2 ? 12 : 13; }
static long font () { return (long) ui.font; }
static long font_bold () { return (long) ui.font_b; }

// A line of text in a box, centred on its height (ui_text takes the baseline).
static void text_box (const char *u8, int x, int y, int h, unsigned ink, long fnt = 0, int maxw = 0)
{
	int px = ui_px ();
	ui_text_fit ((long) u8, x, y + h / 2 + px * 37 / 100, ink, px, fnt ? fnt : font (), maxw > 0 ? maxw : 2000);
}
static int text_w (const char *u8, long fnt = 0) { return (int) ui_text_width ((long) u8, ui_px (), fnt ? fnt : font ()); }

// (10 ms)
static unsigned ticks ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) t.tv_sec * 100u + (unsigned) (t.tv_nsec / 10000000);
}

// The keys as one number: a character, or SK (a HID code) for what is not one.
#define SK(hid) (0x10000 | (hid))
enum { K_ENTER = SK (KEY_ENTER), K_ESC = SK (KEY_ESC), K_BACK = SK (KEY_BACKSPACE), K_TAB = SK (KEY_TAB), K_DEL = SK (KEY_DELETE),
       K_LEFT = SK (KEY_LEFT), K_RIGHT = SK (KEY_RIGHT), K_UP = SK (KEY_UP), K_DOWN = SK (KEY_DOWN), K_HOME = SK (KEY_HOME), K_END = SK (KEY_END),
       K_PGUP = SK (KEY_PGUP), K_PGDN = SK (KEY_PGDN), K_F2 = SK (0x3B) };
static long key_code (int hid, int ch)
{
	switch (hid)
	{
	case KEY_ENTER: case KEY_ESC: case KEY_BACKSPACE: case KEY_TAB: case KEY_DELETE: case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN:
	case KEY_HOME: case KEY_END: case KEY_PGUP: case KEY_PGDN: case 0x3B: return SK (hid);
	}
	return ch >= 32 ? ch : 0;
}

// A list at a point of the window (labels: the kit's form, "-": a line) -> the choice, -1.
static int popup_list (int x, int y, int w, const char *const *items, int n)
{
	Buf b;
	for (int i = 0; i < n; i++) { b.puts (TR (items[i])); b.put ('\0'); }
	b.put ('\0');
	return (int) ui_popup_local (x, y, (long) b.b, w);
}

// What a part of the window needs (acted on in the main loop, out of UIKit's event code).
static bool g_ui;			// the state changed: the bars, the sidebar, the status follow
static bool g_svDirty, g_listDirty;	// the slide, the thumbnails to compose again
static inline void after () { g_ui = true; g_svDirty = true; g_listDirty = true; }

} // namespace sl

#endif
