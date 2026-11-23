/* wifilist.h : la liste des réseaux Wi-Fi, partagée par le menu Wi-Fi (wifimenu) et les réglages rapides
 * (quickmenu) : wifilist.c. Le programme importe uikit, netkit, buskit ; il pose wl_ui (son interface : polices et
 * événement), wl_x et wl_w (la place de la liste dans sa fenêtre), puis :
 *	wl_start (1)		l'état, un balayage, le thread des travaux (une fois)
 *	wl_bus (sub)		un WEV_BUS : korin.net.wifi -> 1 (à relire au prochain wl_tick)
 *	wl_tick ()		toutes les 200 ms -> 0 rien, 1 redessiner, 2 connecté
 *	wl_nhits_reset (), wl_draw (y) -> y sous la liste ; wl_height () ; wl_state_text ()
 *	wl_click (x, y, &arg)	-> 0 rien, 1 redessiner, >= 3 une zone du programme (wl_hit_add)
 *	wl_key ()		une touche (le champ de la clé) -> 1 si prise */
#ifndef KORIN_WIFILIST_H
#define KORIN_WIFILIST_H

#include "korinrt.h"

extern long wl_w, wl_x;
extern const struct krt_ui *wl_ui;
extern u8 wl_wstat[64];     /* WIFI_OP_STATUS : état, RSSI, MAC, SSID (+16) */
extern long wl_selrow;      /* la ligne dépliée (la clé), -1 : aucune */
extern const char *wl_msgp; /* le message de la dernière action (0 : aucun) */

void wl_status(void);
void wl_launch(long job);  /* 1 balayer, 2 se connecter */
long wl_start(long scan);
long wl_bus(u64 sub);
long wl_tick(void);
void wl_nhits_reset(void);
void wl_hit_add(long x, long y, long w, long h, long action, long arg);
long wl_height(void);
const char *wl_state_text(void);
long wl_draw(long y);
long wl_click(long x, long y, long *arg);
long wl_key(void);

#endif
