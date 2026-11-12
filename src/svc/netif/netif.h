/* netif/netif.h - côté pilote d'une interface réseau, en C (repris pas à pas de
 * src/svc/inc/netif.inc ; docs/RESEAU.md 2.2-2.3) : inscription auprès de netd
 * (service « net »), anneau de trames partagé (un producteur, un consommateur par
 * sens, sans verrou ni appel système), état du lien.
 *
 * Ce dossier n'est pas un service (pas de main) : un pilote le compile par son
 * svc.conf (sources = ../netif/netif.c). */
#ifndef NETIF_H
#define NETIF_H

#include "korinrt.h"

#define NIF_ETHERNET 1 /* types d'interface (src/inc/netd.inc) */
#define NIF_WIFI 2
#define NIF_USB 3
#define NIF_PRIO_USB 10
#define NR_FRAME_MAX 1544

extern long nif_wake; /* canal + 1 d'un service du pilote (avant netif_register ; 0 : il sonde) */

/* type, MAC (octets dans l'ordre, petit-boutiste), priorité, nom -> 0, < 0 si netd ne répond pas */
long netif_register(u64 type, u64 mac, u64 prio, const char *name);
/* trame, longueur -> 1 si mise dans l'anneau de réception */
int netif_rx_put(const u8 *frame, u64 len);
/* tampon de 1544 o -> longueur d'une trame à émettre (0 : rien) */
u64 netif_tx_get(u8 *out);
/* trames en attente d'émission */
u64 netif_tx_pending(void);
/* lien 0/1, débit Mb/s, SSID (0 : aucun) : dit à netd */
void netif_link(u64 link, u64 speed, const char *ssid);
/* dans la boucle du pilote : toutes les 2 s, l'état à netd ; relancé : réinscription */
void netif_tick(void);

#endif
