/*
 * present.h -- la fenetre de Jet sur ARMPI et LE point ou l'image de WebKit est montree
 * (docs/WEBKIT.md, etape 4).
 *
 * La page est une SURFACE ATTACHEE d'Elegant (ui_surface_* de UIKit, DESIGN-ELEGANT.md 17) : un
 * tampon partage qu'Elegant compose directement dans la zone de la page ; WebKit peint dedans
 * (jet_page_area donne ses pixels) et jet_present ne fait que signaler les rectangles changes
 * (ui_surface_damage) : pas de copie, pas de ui_update. Sans surface (Elegant ancien) ou sans zone
 * de page (wctest --view) : l'image est copiee dans le tampon de la fenetre, puis ui_update.
 *
 * C, sans en-tete d'ARMPI : se lie aussi dans les fichiers compiles comme WebCore.
 */
#ifndef JET_PRESENT_H
#define JET_PRESENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { JET_EV_NONE = 0, JET_EV_KEY = 1, JET_EV_MOUSE = 2, JET_EV_CLOSE = 3, JET_EV_WHEEL = 4, JET_EV_FOCUS = 5,
	JET_EV_MENU = 7 };

struct jet_event
{
	int type;			/* JET_EV_* */
	int kind;			/* touche : 1 appui / 0 relache ; souris : 0 deplacement, 1 appui, 2 relache */
	int x, y;			/* souris (dans la fenetre) ; molette : y = crans (> 0 vers le haut) */
	int code;			/* code HID de la touche, boutons de la souris, entree de menu */
	int ch;				/* caractere (Unicode) */
	int mods;
};

/* la fenetre (une par programme) -> 1 si ouverte ; *w, *h : sa taille utile */
int jet_window_open (int width, int height, const char *title, int *w, int *h);
void jet_window_title (const char *title);
/* la zone de la page dans la fenetre (x, y, w, h du contenu) : la surface attachee creee ou deplacee
 * -> ses pixels (32 bits 0x00RRGGBB, *stride en pixels, le pixel (0, 0) de la page en (x, y) de la
 * fenetre) ; 0 : pas de surface (l'image sera copiee dans la fenetre, a partir de (x, y)) */
uint32_t *jet_page_area (int x, int y, int w, int h, int *stride);
/* LE point de presentation : le rectangle (x, y, w, h) de la page (coordonnees de la page) montre.
 * pixels = ceux de jet_page_area : rien a copier ; sinon une image a soi (stride en pixels), copiee */
void jet_present (const uint32_t *pixels, int stride, int x, int y, int w, int h);
/* la page est dans un tampon partage ecrit par le GPU (pitch en octets, a offset), Elegant l'y lit : le
 * rectangle (x, y, w, h) a change. buffer -1 : retour aux pixels de jet_page_area. -> 1 (0 : pas de surface) */
int jet_page_show (int buffer, int pitch, long offset, int x, int y, int w, int h);
/* un evenement (attente en ms) -> son type (JET_EV_NONE : aucun) */
int jet_window_event (int wait_ms, struct jet_event *ev);
void jet_window_close (void);
/* les variables de UIKit de la fenetre (struct ui_vars de uikit.h) : l'habillage de Jet (main.cpp) y lit
 * ses evenements et y met ses widgets */
void *jet_ui_vars (void);

#ifdef __cplusplus
}
#endif

#endif
