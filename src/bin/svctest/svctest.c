/* svctest - essai des services nommés (docs/MICRO-NOYAU.md), comme
 * svctest.asm : un programme hors de /sys ne peut ni enregistrer de
 * service ni toucher au matériel (codes affichés) ; lance le service d'essai
 * du matériel (/sys/svc/hwtest) et l'attend ; lance le service d'essai
 * /sys/svc/echo, l'ouvre, lui fait N allers-retours (op 7, a = i : la
 * réponse doit valoir i + 7) et affiche leur durée ; arrête le service : son
 * appel suivant échoue (code affiché). Sorties par puts et print_dec d'AppKit
 * (sans tampon) ; codes 0, 1 (réponse fausse ou erreur). */
#include "korinrt.h"

#define N 10000

static u64 msg[64];

static void show(const char *label, long v)
{
	ak_puts(label);
	print_dec(v);
	ak_puts("\n");
}

static NORETURN void fail(long e)
{
	show("svctest : erreur ", e);
	_exit(1);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	show("svctest : enregistrement hors de /sys -> ", korin_sys2(SYS_SVC_REGISTER, "essai", 0));
	show("svctest : acces au materiel hors de /sys -> ", korin_sys2(SYS_DEV_MAP, 0xFE003000L, 0x20));
	/* SYS_SPAWN par file_sys d'AppKit (macro sys), SYS_WAIT au noyau */
	long pid = ksys(SYS_SPAWN, (long)"/sys/svc/hwtest", 0, 0, 0, 0, 0);
	if (pid < 0)
		fail(pid);
	korin_sys1(SYS_WAIT, pid); /* (x0 = le pid rendu, comme l'asm) */
	long svc = ksys(SYS_SPAWN, (long)"/sys/svc/echo", 0, 0, 0, 0, 0);
	if (svc < 0)
		fail(svc);
	long ch = korin_sys2(SYS_SVC_OPEN, "echo", 2000);
	if (ch < 0)
		fail(ch);
	long t0 = sys_time_ms();
	for (u64 i = 0; i < N; i++) {
		msg[0] = 7;
		msg[1] = i;
		long r = korin_sys2(SYS_SVC_CALL, ch, msg);
		if ((u64)r != i + 7) {
			ak_puts("svctest : reponse fausse\n");
			_exit(1);
		}
	}
	long dt = sys_time_ms() - t0;
	ak_puts("svctest : ");
	print_dec(N);
	ak_puts(" allers-retours en ");
	print_dec(dt);
	ak_puts(" ms, soit ");
	print_dec((long)((u64)(dt * 1000) / N));
	ak_puts(" us chacun\n");
	korin_sys1(SYS_KILL, svc); /* fin du service : ses appels échouent */
	korin_sys1(SYS_WAIT, svc);
	msg[0] = 7;
	msg[1] = 0;
	show("svctest : service arrete -> ", korin_sys2(SYS_SVC_CALL, ch, msg));
	_exit(0);
}
