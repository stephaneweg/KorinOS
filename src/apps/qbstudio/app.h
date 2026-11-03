//
// app.h -- ce que les vues de QBStudio partagent : l'interface (ui_vars), les couleurs (les jetons de UIKit, jamais
// theme.txt), le texte dans les vues dessinées à la main. Les vues (designer.h, props.h) sont des ui_view : tout passe
// par les ui_* de l'OS, sans classe autour des widgets.
//
#ifndef _qbstudio_app_h
#define _qbstudio_app_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <uikit.h>

#include "form.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))

namespace qs {

static struct ui_vars ui;

// ---- le thème ----------------------------------------------------------------------------------------------------
static const unsigned *s_jet;
static inline unsigned jet (int offset) { return s_jet[offset / 4] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned bg) { return (unsigned) ui_ink (bg) & 0xFFFFFF; }

// Les hauteurs de l'échelle de UIKit : un contrôle, le texte de l'interface
static int ctl_h () { static const int H[3] = { 32, 32, 28 }; long e = ui_echelle (); return H[e > 2 ? 2 : e]; }
static int ui_px () { return ui_echelle () >= 2 ? 12 : 13; }
static inline long font () { return (long) ui.font; }
static inline long font_bold () { return (long) ui.font_b; }

// ---- le texte dans une vue ---------------------------------------------------------------------------------------
static int tw (const char *s, bool bold = false) { return (int) ui_text_width ((long) s, ui_px (), bold ? font_bold () : font ()); }
// Une ligne dans une boîte, centrée sur sa hauteur (ui_text prend la ligne de base) ; maxw > 0 : coupée par « … »
static void text_box (const char *s, int x, int y, int h, unsigned ink, bool bold = false, int maxw = 0)
{
	int px = ui_px ();
	ui_text_fit ((long) s, x, y + h / 2 + px * 37 / 100, ink, px, bold ? font_bold () : font (), maxw > 0 ? maxw : 2000);
}
static void text_c (const char *s, int x, int y, int w, int h, unsigned ink, bool bold = false)
{
	text_box (s, x + (w - tw (s, bold)) / 2, y, h, ink, bold, w);
}
static void dashed_rect (int x, int y, int w, int h, unsigned c)
{
	for (int i = 0; i < w; i += 6) { int l = imin (3, w - i); ui_fill (x + i, y, l, 1, c); ui_fill (x + i, y + h - 1, l, 1, c); }
	for (int i = 0; i < h; i += 6) { int l = imin (3, h - i); ui_fill (x, y + i, 1, l, c); ui_fill (x + w - 1, y + i, 1, l, c); }
}
// Une case à cocher (dessin de la grille des propriétés et du concepteur)
static void check_mark (int x, int y, int s, bool on)
{
	ui_rbox (x, y, s, s, 3, on ? jet (TJ_ACCENT) : jet (TJ_LIGNE_FORTE), on ? jet (TJ_ACCENT) : jet (TJ_LIGNE_FORTE));
	if (!on) ui_rbox (x + 1, y + 1, s - 2, s - 2, 2, jet (TJ_SURFACE), jet (TJ_SURFACE));
	else text_c ("\xE2\x9C\x93", x, y, s, s, ink_on (jet (TJ_ACCENT)), true);
}

// Ce que la fenêtre dit aux vues
static void (*g_onSelect) (El *e);			// un élément choisi (0 : aucun)
static void (*g_onFormEdited) ();			// l'arbre a changé : l'application réécrit le texte
static void (*g_onElementOpen) (El *e);			// un double-clic : la SUB de son événement
static void (*g_onDesignHint) (const char *s);		// une ligne pour la barre d'état

} // namespace qs

#endif
