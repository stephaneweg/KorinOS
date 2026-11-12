/* svcfault - essai, comme svcfault.asm : un appel de service (blk.sd0,
 * BLK_INFO) dont le message est en lecture seule. La réponse ne peut pas y
 * être écrite : ce programme doit mourir seul, sans bloquer les services (le
 * noyau ne copie plus sous svc_lock) ; le shell qui l'a lancé doit répondre
 * ensuite. Sinon : « ECHEC, réponse écrite », code 1 ; blk.sd0 absent :
 * code 2. Le message est dans .rodata, que bin.ld met dans le premier segment
 * (lecture, exécution), comme msg_ro dans le segment de code de l'asm. */
#include "korinrt.h"

static const u64 msg_ro[64] __attribute__((aligned(8))) = {1}; /* BLK_INFO */

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long ch = korin_sys2(SYS_SVC_OPEN, "blk.sd0", 1000);
	if (ch < 0) {
		ak_puts("svcfault : blk.sd0 absent\n");
		_exit(2);
	}
	korin_sys2(SYS_SVC_CALL, ch, msg_ro);
	ak_puts("svcfault : ECHEC, reponse ecrite en lecture seule\n"); /* pas attendu */
	_exit(1);
}
