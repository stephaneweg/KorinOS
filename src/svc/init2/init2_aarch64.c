/* init2_aarch64.c : le redémarrage du Pi 4 (INIT_OP_REBOOT), repris de
 * reboot_request d'init2.asm : le bloc PM (0xFE100000), chien de garde à 10
 * tics et réinitialisation complète (docs/RESEAU.md 5.3). tryboot : le
 * firmware lira tryboot.txt une fois (SET_REBOOT_FLAGS). Arrêt (bit 1,
 * INIT_REBOOT_HALT) : la même réinitialisation, la partition 63 dans PM_RSTS
 * dit au firmware de s'arrêter là au lieu de relancer le système. */
#include "init2.h"

static u32 fw_tryboot[8] __attribute__((aligned(16))) = {28, 0, 0x00038064, 4, 4, 1, 0}; /* SET_REBOOT_FLAGS(1) */

void plat_reboot(u64 flags)
{
	if (flags & 1)
		korin_sys1(SYS_FW_CALL, fw_tryboot);
	long v = korin_sys2(SYS_DEV_MAP, 0xFE100000, 0x1000); /* bloc PM */
	if (v < 0)
		return;
	volatile u32 *pm = (volatile u32 *)v;
	if (flags & 2)
		pm[0x20 / 4] = pm[0x20 / 4] | 0x5A000555; /* PM_RSTS : partition 63 (bits pairs) */
	pm[0x24 / 4] = 0x5A00000A; /* PM_WDOG = mot de passe | 10 */
	pm[0x1C / 4] = (pm[0x1C / 4] & 0xFFFFFFCF) | 0x5A000020; /* PM_RSTC : réinitialisation complète */
	sys_sleep(3000);
}
