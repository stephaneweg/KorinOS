//
// pkgui.h -- la liste de Logiciels (docs/PAQUETS.md) : trois onglets (Mises à jour, Installés, Disponibles), la
// recherche, les lignes des paquets, les boutons, la progression. Une seule source pour deux hôtes :
//
//   * la fenêtre Logiciels, apps/pkgman.app (main.cpp) ;
//   * le volet Logiciels de Réglages, sys/applets/pkg.so (applet/applet.cpp ; src/apps/control/control.h, « les
//     applets installées »).
//
// Elle ne touche pas elle-même aux paquets (ni pkglib.h ni OpenSSL : un module de Réglages n'apporte pas de libc) :
// l'état vient de « pkg ui-list », chaque travail est un process « pkg ui-job ... » (src/bin/pkg/main.cpp) dont elle
// lit la progression, la fenêtre vivante. Un travail à la fois ; Logiciels fermé, il va au bout.
//
// Les widgets sont ceux d'UIKit, posés dans le rectangle donné (x, y, l, h) de la fenêtre courante ; leurs
// notifications passent par ui_w_on (l'hôte n'a qu'à appeler uikit_w_event, puis pkgui_poll souvent).
//
#ifndef PKGUI_H
#define PKGUI_H

#define PKG_CMD		"/bin/pkg"
#define PKGMAN_APP	"/apps/pkgman.app/main"

// capw : la place laissée en haut à droite (les boutons du cadre de la fenêtre) ; updates : ouvrir sur Mises à jour
void pkgui_open (long x, long y, long w, long h, long capw, bool updates);
void pkgui_layout (long x, long y, long w, long h);	// le rectangle a changé
void pkgui_close (void);				// les widgets retirés (un travail en cours continue)
void pkgui_redraw (void);				// les widgets et les vues redessinés (le fond l'a été)
void pkgui_poll (void);				// à appeler souvent : la progression, les clics qui ouvrent un dialogue
void pkgui_status (const char *text);			// le texte du pied (traduit)
// le paquet pkgman (la commande pkg, Logiciels, ce volet) vient d'être mis à jour : à l'hôte de se relancer
extern void (*pkgui_self_updated) (void);

#endif
