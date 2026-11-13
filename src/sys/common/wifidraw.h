/* wifidraw.h : petits dessins du Wi-Fi (UIKit) : barres de signal, cadenas, coche ; pour la barre
 * de menus, wifimenu et l'applet Wi-Fi du panneau de configuration. Repris de src/inc/wifidraw.inc.
 * Le programme importe uikit (sys.conf « import = uikit ») ; les couleurs sont données par
 * l'appelant (jetons du thème, ui_tokens). */
#ifndef KORIN_WIFIDRAW_H
#define KORIN_WIFIDRAW_H

#include "korinrt.h"

/* RSSI (dBm, signé) -> barres allumées (1..4) */
long wd_level(long rssi);
/* x, top : coin de l'icône (16 x 14) ; lit : barres allumées (0..4) ; on, off : couleurs */
void wd_bars(long x, long top, long lit, u32 on, u32 off);
/* x, top : coin du cadenas (8 x 10) */
void wd_lock(long x, long top, u32 color);
/* x, top : coin de la coche (10 x 8) */
void wd_check(long x, long top, u32 color);

#endif
