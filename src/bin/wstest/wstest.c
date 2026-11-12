/* wstest - essai des mécanismes du serveur graphique (ws.asm du noyau, appels
 * 0x50-0x56), comme wstest.asm :
 *   wstest serveur : s'enregistre, prend l'écran (mire), crée un tampon
 *                    partagé, répond aux requêtes, voit la fin du client
 *   wstest client  : requête (2 + 3), tampon partagé, événement du serveur
 * Comme l'asm : seule la première lettre de la ligne d'arguments du noyau
 * compte (« s… » serveur, « c… » client ; sinon l'usage, code 2) ; sortie sans
 * tampon (puts, print_dec) ; « wstest : termine » code 0, « wstest : ECHEC »
 * code 1. Avec Elegant en marche, le
 * serveur échoue (ws_register refusé) et le client parle à Elegant (op 1 :
 * WS_OP_CREATE). */
#include "korinrt.h"

#define MAGIC 0x41524D50 /* « ARMP » */
#define WT_BUF_VA_BASE 0x6800000000ull /* tampon n : WT_BUF_VA_BASE + n * WT_BUF_SLOT (armpi.inc) */
#define WT_BUF_SLOT 0x4000000ull

static u64 info[5]; /* SYS_WS_DISPLAY : adresse, largeur, hauteur, pas, adresse physique */
static u32 ev[8];
static u8 msg[512];

#define MX(o) (*(u64 *)(msg + (o)))
#define MW(o) (*(u32 *)(msg + (o)))

/* « wstest : <libellé> <résultat> » -> le résultat */
static long report(long r, const char *label)
{
	ak_puts("wstest : ");
	ak_puts(label);
	print_dec(r);
	ak_puts("\n");
	return r;
}

static NORETURN void ok(void)
{
	ak_puts("wstest : termine\n");
	_exit(0);
}

static NORETURN void fail(void)
{
	ak_puts("wstest : ECHEC\n");
	_exit(1);
}

static NORETURN void server(void)
{
	if (report(ksys(SYS_WS_REGISTER, 0, 0, 0, 0, 0, 0), "ws_register -> ") < 0)
		fail();
	if (report(ksys(SYS_WS_DISPLAY, (long)info, 0, 0, 0, 0, 0), "ws_display -> ") < 0)
		fail();
	ak_puts("wstest : ecran "); /* adresse, taille de l'écran */
	print_dec((long)info[1]);
	ak_puts(" x ");
	print_dec((long)info[2]);
	ak_puts("\n");
	for (u64 y = 20; y < 120; y++) { /* mire : rectangle vert 200 x 100 en (20, 20) */
		u32 *p = (u32 *)(info[0] + y * info[3] + 20 * 4);
		for (int x = 0; x < 200; x++)
			*p++ = 0x0020C040;
	}
	long buf = report(ksys(SYS_BUF_CREATE, 4096, 0, 0, 0, 0, 0), "buf_create -> "); /* tampon partagé */
	if (buf < 0)
		fail();
	*(u32 *)(WT_BUF_VA_BASE + (u64)buf * WT_BUF_SLOT) = MAGIC;
	long ticks = 30; /* 30 s au plus */
	for (;;) {
		long req = ksys(SYS_WS_NEXT, (long)msg, 1000, 0, 0, 0, 0);
		if (!req) {
			if (!--ticks) {
				ak_puts("wstest : pas de client\n");
				fail();
			}
			continue;
		}
		if (req < 0)
			fail();
		u64 op = MX(WM_OP);
		if (op == 1) {
			ksys(SYS_BUF_GRANT, buf, MW(WM_PID), 0, 0, 0, 0);
			MX(WM_OP) = MX(WM_A) + MX(WM_B); /* a + b */
			MX(WM_C) = (u64)buf;             /* c = numéro du tampon */
			str_cat((char *)msg + WM_DATA, "bonjour du serveur");
			ksys(SYS_WS_REPLY, req, (long)msg, 0, 0, 0, 0);
		} else if (op == 2) {
			ev[0] = 42;
			report(ksys(SYS_WS_POST, MW(WM_PID), (long)ev, 0, 0, 0, 0), "ws_post -> ");
			MX(WM_OP) = 0;
			ksys(SYS_WS_REPLY, req, (long)msg, 0, 0, 0, 0);
		} else if (op == WS_OP_EXIT) {
			ak_puts("wstest : le noyau signale la fin du client, pid ");
			print_dec((long)MX(WM_A));
			ak_puts("\n");
			sys_sleep(4000); /* l'écran reste au serveur 4 s (capture) */
			ok();
		}
	}
}

static NORETURN void client(void)
{
	sys_sleep(500);
	MX(WM_OP) = 1; /* requête 1 : 2 + 3 */
	MX(WM_A) = 2;
	MX(WM_B) = 3;
	if (report(ksys(SYS_WS_CALL, (long)msg, 0, 0, 0, 0, 0), "ws_call(2 + 3) -> ") < 0)
		fail();
	ak_puts("wstest : donnees de la reponse : ");
	ak_puts((char *)msg + WM_DATA);
	ak_puts("\n");
	long b = (long)MX(WM_C); /* tampon partagé */
	long p = report(ksys(SYS_BUF_MAP, b, 0, 0, 0, 0, 0), "buf_map -> ");
	if (p < 0)
		fail();
	if (*(u32 *)p == MAGIC)
		ak_puts("wstest : tampon partage lu : ARMP correct\n");
	else
		ak_puts("wstest : tampon partage : VALEUR FAUSSE\n");
	MX(WM_OP) = 2; /* requête 2 : un événement */
	ksys(SYS_WS_CALL, (long)msg, 0, 0, 0, 0, 0);
	report(ksys(SYS_EV_WAIT, (long)ev, 2000, 0, 0, 0, 0), "ev_wait -> type ");
	ok();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *l = krt_line; /* (la ligne du noyau, comme x0 de l'asm) */
	if (l && l[0] == 's')
		server();
	if (l && l[0] == 'c')
		client();
	ak_puts("usage : wstest serveur | client\n");
	_exit(2);
}
