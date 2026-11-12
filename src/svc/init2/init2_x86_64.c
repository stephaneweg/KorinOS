/* init2_x86_64.c : le redémarrage du PC (INIT_OP_REBOOT). Pas encore d'appel
 * pour redémarrer depuis un service (le moniteur du noyau a « reboot ») : rien,
 * init2 écrit que le redémarrage n'a pas eu lieu. */
#include "init2.h"

void plat_reboot(u64 flags)
{
	(void)flags;
}
