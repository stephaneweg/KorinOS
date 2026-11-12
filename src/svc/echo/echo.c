/* echo - service d'essai (SD0:/sys/svc/echo), repris de echo.asm : il
 * s'enregistre sous « echo » et répond à chaque requête op + a, avec les
 * mêmes données (svctest le mesure). Commun au Pi et au PC. */
#include "korinrt.h"

static u64 msg[64]; /* 512 o */

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long chan = korin_sys2(SYS_SVC_REGISTER, "echo", 0);
	if (chan < 0)
		return 0; /* (code 0, comme l'asm) */
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, 60000);
		if (req < 0)
			return 0;
		if (!req)
			continue; /* rien (ou avis du noyau) */
		msg[0] += msg[1]; /* résultat : op + a */
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
