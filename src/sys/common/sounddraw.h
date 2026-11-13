/* sounddraw.h : petits dessins du son (UIKit) : icône haut-parleur, curseur de volume, case à
 * cocher, vumètre ; pour la barre de menus, le volet du son (soundmenu) et le Mixer
 * (docs/SON.md 4.4). Repris de src/inc/sounddraw.inc. Le programme importe uikit ; les couleurs
 * viennent des jetons du thème (ui_tokens, TJ_*), sauf l'encre du haut-parleur, donnée par
 * l'appelant. ui : les variables de l'interface du programme (sa police grasse). */
#ifndef KORIN_SOUNDDRAW_H
#define KORIN_SOUNDDRAW_H

#include "korinrt.h"

#define SD_SLIDER_H 16 /* hauteur de la zone d'un curseur */

/* x, y : coin (16 x 14) ; muted != 0 : une croix à la place des ondes ; ink : encre ; dim :
 * encre atténuée (pas utilisée, comme l'asm) */
void sd_speaker(long x, long y, long muted, u32 ink, u32 dim, const struct krt_ui *ui);
/* x, y : coin ; w : largeur ; value : 0..100 ; grey != 0 : grisé (muet) : piste, part remplie,
 * bouton */
void sd_slider(long x, long y, long w, u64 value, long grey);
/* cx : abscisse du clic ; x : début du curseur ; w : largeur -> 0..100 */
long sd_slider_value(long cx, long x, long w);
/* x, y : coin (14 x 14) ; checked != 0 : cochée */
void sd_check(long x, long y, long checked, const struct krt_ui *ui);
/* x, y : coin ; w, h : taille ; peak : crête (0..32767) */
void sd_meter(long x, long y, long w, long h, u64 peak);

#endif
