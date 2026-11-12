/* init2.h : entre le cœur commun d'init2 (init2.c) et la couche de chaque
 * machine (init2_aarch64.c, init2_x86_64.c). */
#ifndef KORIN_INIT2_H
#define KORIN_INIT2_H

#include "../svclib/svclib.h"

/* redémarrage matériel (INIT_OP_REBOOT), après la réponse et 500 ms : flags bit 0
 * tryboot, bit 1 arrêt ; revient si le redémarrage n'a pas eu lieu */
void plat_reboot(u64 flags);

#endif
