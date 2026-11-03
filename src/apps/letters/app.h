//
// app.h -- what Letters' parts share: UIKit's variables, the window's look, the clock.
//
#ifndef _writer_app_h
#define _writer_app_h

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <uikit.h>

namespace wr {

static struct ui_vars ui;

// ---- the theme: UIKit's tokens, never theme.txt ---------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

// The heights of UIKit's scales (uk_mt_tab): a control's, the interface's text.
static int ctl_h () { static const int H[3] = { 32, 32, 28 }; long e = ui_echelle (); return H[e > 2 ? 2 : e]; }
static int ui_px () { return ui_echelle () >= 2 ? 12 : 13; }
static long font () { return (long) ui.font; }

// (10 ms)
static unsigned ticks ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) t.tv_sec * 100u + (unsigned) (t.tv_nsec / 10000000);
}

} // namespace wr

#endif
