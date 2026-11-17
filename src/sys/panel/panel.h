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

/* ui_screen : largeur, hauteur, réservé en bas (le panel), barre du haut */
enum { SCR_W, SCR_H, SCR_BOTTOM, SCR_TOP };

extern struct krt_ui ui;
extern long scr[4];
extern long screen_todo; /* la résolution a changé pendant le lanceur */

/* le lanceur, jusqu'à ce qu'il se ferme ; une application choisie est lancée (ou sa fenêtre
 * ramenée) */
void launcher(void);

#endif
