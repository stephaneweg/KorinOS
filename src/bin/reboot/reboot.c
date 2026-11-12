/* reboot - redémarre le Pi (docs/RESEAU.md 5.3), comme reboot.asm : demande à init2 (op
 * INIT_OP_REBOOT), qui laisse finir les écritures puis passe par le bloc PM.
 *   reboot            redémarrage
 *   reboot tryboot    une fois sur tryboot.txt (kernel=kernel8-essai.img...)
 * Comme l'asm : la ligne d'arguments entière est comparée à « tryboot » (tout autre texte :
 * redémarrage normal) ; pas d'aide ni d'usage ; le message de départ s'écrit avant la demande,
 * et « init2 ne redémarre pas le Pi » (code 1) si init2 manque, refuse, ou si le Pi tourne
 * encore 5 s après. */
#include "korinrt.h"

#define INIT_OP_REBOOT 3

static u64 msg[MSG_SIZE / 8];

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long flags = 0;
	if (krt_line && str_eq(krt_line, "tryboot"))
		flags = 1;
	lang_puts("reboot : redémarrage...\n");
	long ch = korin_sys2(SYS_SVC_OPEN, "init2", 1000);
	if (ch >= 0) {
		msg[0] = INIT_OP_REBOOT;
		msg[1] = (u64)flags;
		*(u32 *)((u8 *)msg + MSG_LEN) = 0;
		if (korin_sys2(SYS_SVC_CALL, ch, msg) >= 0)
			sys_sleep(5000); /* (le Pi redémarre pendant ce temps) */
	}
	lang_puts("reboot : init2 ne redémarre pas le Pi\n");
	_exit(1);
}
