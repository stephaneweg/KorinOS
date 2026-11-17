/* glass.h : le verre des barres du bureau (panel, barre de menus ; docs/DESIGN-ELEGANT.md 8.9).
 * La fenêtre de la barre est WF_ALPHA | WF_BLUR : Elegant floute ce qu'il y a dessous et porte
 * son ombre ; ici, la teinte translucide du thème, son volume (plus claire en haut), son bord et
 * son reflet, et les boîtes translucides qui se posent dessus (survol). Le programme importe
 * uikit ; couleurs par ui_tokens. */
#ifndef KORIN_GLASS_H
#define KORIN_GLASS_H

#include "korinrt.h"

/* le bord de la barre qui touche le bureau : en haut (le panel, en bas de l'écran), en bas (la
 * barre de menus) */
#define GLASS_EDGE_TOP 0
#define GLASS_EDGE_BOTTOM 1

/* hauteur de la barre, jeton de sa teinte (TJ_DOCK, TJ_BAR), bord : à appeler avant le dessin
 * (après un changement de taille ou de thème aussi) */
void glass_set(long h, long tok, long edge);
/* ligne y -> le verre de cette ligne, 0xTTRRGGBB (TT : transparence) */
u32 glass_at(long y);
/* toute la barre, largeur w : le verre, son bord, son reflet */
void glass_draw(long w);
/* boîte arrondie translucide sur le verre (coins lissés à la main) : x, y, l, h, rayon,
 * couleur 0xTTRRGGBB */
void glass_rbox(long x, long y, long w, long h, long r, u32 c);
/* -> la couleur d'un élément survolé (un verre plus dense et plus clair) */
u32 glass_hover(void);

#endif
