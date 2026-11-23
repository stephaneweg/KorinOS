/* panel.h : ce que partagent panel.c et le lanceur (launcher.c). Le lanceur servira aussi à
 * l'accueil du mode poche (sys/home) : le programme fournit LA_WFLAGS (le rôle de la
 * fenêtre), ui (ui_init), scr (ui_screen), screen_todo, les catalogues (applist, dockconf,
 * panelconf), les récentes (recent) et app_open (apprun). */
#ifndef KORIN_PANEL_H
#define KORIN_PANEL_H

#include "sys_consts.h"
#include "uikit_import.h"
#include "applist.h"
#include "dockconf.h"
#include "panelconf.h"
#include "recent.h"
#include "apprun.h"
#include "glass.h"

/* le lanceur : un rideau sur les fenêtres */
#define LA_WFLAGS (WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST | WF_GRAB | WF_ALPHA | WF_CURTAIN)

/* la grille des applications de la vue d'ensemble (la_grid) : sur le fond de la vue, ni rideau ni capture
 * (les bureaux en haut et le dash restent à Elegant et au panel) ; Elegant la fait monter du bas */
#define LA_GRID_WFLAGS (WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_ALPHA | WF_APPGRID)

/* ui_screen : largeur, hauteur, réservé en bas (le panel), barre du haut */
enum { SCR_W, SCR_H, SCR_BOTTOM, SCR_TOP };

extern struct krt_ui ui;
extern long scr[4];
extern long screen_todo; /* la résolution a changé pendant le lanceur */
extern long la_bottom;
extern long la_grid;     /* 1 : le lanceur en grille de la vue d'ensemble */
/* (la_grid) un événement pour une autre fenêtre du programme (le dash), la sienne courante -> 1 : fermer la
 * grille ; fourni par le programme */
long la_other(void);   /* place laissée en bas de l'écran sous le lanceur (le dash), en plus du réservé */

/* le lanceur, jusqu'à ce qu'il se ferme ; une application choisie est lancée (ou sa fenêtre
 * ramenée) */
void launcher(void);

#endif
