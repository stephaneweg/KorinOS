/* shutdown - éteint la machine (ou la redémarre), comme Éteindre du dialogue de fin de session
 * (SD0:/sys/shutdown) : demande à init2 (op INIT_OP_REBOOT, bit 1 : arrêt), qui prévient les
 * programmes, vide et démonte les volumes, arrête les disques, puis éteint (Pi : le bloc PM,
 * le firmware ne relance pas ; PC : ACPI S5).
 *   shutdown                 extinction
 *   shutdown -h | -P | now   extinction (les options habituelles, acceptées)
 *   shutdown -r              redémarrage (comme reboot)
 * Une autre option : l'usage, code 2. « init2 n'éteint pas la machine » (code 1) si init2 manque,
 * refuse, ou si la machine tourne encore 10 s après. */
#include "korinrt.h"

#define INIT_OP_REBOOT 3
#define INIT_REBOOT_HALT 2

static u64 msg[MSG_SIZE / 8];

int main(int argc, char **argv)
{
	long flags = INIT_REBOOT_HALT;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (str_eq(a, "-r"))
			flags = 0;
		else if (!str_eq(a, "-h") && !str_eq(a, "-P") && !str_eq(a, "now")) {
			lang_puts("usage : shutdown [-h | -r] [now]\n");
			_exit(2);
		}
	}
	lang_puts(flags ? "shutdown : extinction...\n" : "shutdown : redémarrage...\n");
	long ch = korin_sys2(SYS_SVC_OPEN, "init2", 1000);
	if (ch >= 0) {
		msg[0] = INIT_OP_REBOOT;
		msg[1] = (u64)flags;
		*(u32 *)((u8 *)msg + MSG_LEN) = 0;
		if (korin_sys2(SYS_SVC_CALL, ch, msg) >= 0)
			sys_sleep(10000); /* (init2 arrête les programmes, celui-ci compris) */
	}
	lang_puts("shutdown : init2 n'éteint pas la machine\n");
	_exit(1);
}
