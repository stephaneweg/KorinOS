/* init2.h : entre le cœur commun d'init2 (init2.c) et la couche de chaque
 * machine (init2_aarch64.c, init2_x86_64.c). */
#ifndef KORIN_INIT2_H
#define KORIN_INIT2_H

#include "../svclib/svclib.h"

/* du cœur : « chemin [arguments] », watch = 1 : surveillé (relancé sur une
 * erreur) -> pid ou erreur ; au journal « init2 : ligne -> pid » */
long lancer_ligne(const char *line, int watch);

/* redémarrage matériel (INIT_OP_REBOOT), après la réponse et 500 ms : flags bit 0
 * tryboot, bit 1 arrêt ; revient si le redémarrage n'a pas eu lieu */
void plat_reboot(u64 flags);

/* les bus de la machine, après hwmanager et hidmanager : Pi, usbd (le DWC2) ;
 * PC, pcid annonce de nouveau tout le PCIe à hwmanager (usbd par xHCI...) */
void plat_buses(void);

/* 1 : pas d'interface graphique, init2 lance la console texte au lieu
 * d'Elegant et de startup.txt ; Pi : toujours 0 (Elegant lit shell= lui-même) */
int plat_text_console(void);

/* Elegant ne répond pas (« ws » muet après le lancement) ou est abandonné (trop de relances) :
 * Pi, rien (comme l'asm) ; PC, la console texte en secours */
void plat_shell_lost(void);

/* fermer la session (INIT_OP_LOGOUT, après la réponse) : les process de l'uid uid (> 0) prévenus (SIGTERM),
 * puis arrêtés de force au bout de 3 s -> 0 ; Pi : E_NOSYS (pas d'écran de connexion) */
long plat_logout(long uid);

#endif
