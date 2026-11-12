/* presenttest - essai du present asynchrone (ws_ext.inc, docs/ABI.md), comme
 * presenttest.asm : temps d'un WS_OP_UPDATE (requête, attend Elegant) et d'un
 * ui_update (sans l'attendre), images par WEV_FRAME, attente de ui_event
 * bornée. Sortie : les temps, puis « presenttest : PASS / FAIL » ; code =
 * nombre d'échecs (pas de fenêtre : code 1). Comme l'asm : arguments ignorés,
 * sortie sans tampon (puts, print_dec) ; le message des WS_OP_UPDATE est le
 * même tampon d'un appel à l'autre (la réponse d'Elegant y reste, seuls op,
 * a, b, c, d et les 8 premiers octets des données sont réécrits). */
#include "korinrt.h"
#include "uikit_import.h"

#define PT_W 400
#define PT_H 300
#define PT_CALLS 2000   /* mises à jour par phase */
#define PT_FRAME_MS 2000 /* durée de la phase WEV_FRAME */
#define PT_WAIT_MS 100  /* attente de ui_event */

static struct krt_ui ui;
static u8 msg[WS_MSG];

/* ms pour PT_CALLS appels, libellé : les affiche, en us par appel */
static void report(u64 ms, const char *label)
{
	ak_puts(label);
	print_dec((long)(ms * 1000 / PT_CALLS));
	ak_puts(" us par appel\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ui_init((long)&ui);
	if (ui_window(PT_W, PT_H, (long)"presenttest") < 0) {
		ak_puts("presenttest : pas de fenêtre (Elegant ?)\n");
		_exit(1);
	}
	long fails = 0;
	ui_update_all();

	/* --- ancien chemin : une requête par mise à jour */
	u64 t0 = (u64)sys_time_ms();
	for (int i = PT_CALLS; i; i--) {
		u64 *m = (u64 *)msg;
		m[WM_OP / 8] = WS_OP_UPDATE;
		m[WM_A / 8] = (u64)ui.win;
		m[WM_B / 8] = 0;
		m[WM_C / 8] = 0;
		m[WM_D / 8] = PT_W;
		m[WM_DATA / 8] = PT_H;
		ksys(SYS_WS_CALL, (long)msg, 0, 0, 0, 0, 0);
	}
	report((u64)sys_time_ms() - t0, "presenttest : WS_OP_UPDATE ");

	/* --- ui_update : sans requête */
	t0 = (u64)sys_time_ms();
	for (int i = PT_CALLS; i; i--)
		ui_update(0, 0, PT_W, PT_H);
	report((u64)sys_time_ms() - t0, "presenttest : ui_update ");

	/* --- images cadencées par WEV_FRAME */
	t0 = (u64)sys_time_ms();
	u64 frames = 0;
	for (;;) {
		if (!ui_frame_request())
			goto lost;
		ui_fill(0, 0, PT_W, PT_H, (long)(u32)((u32)frames << 8)); /* une image différente */
		ui_update(0, 0, PT_W, PT_H);
		for (;;) {
			long t = ui_event(500);
			if (!t)
				goto lost; /* aucun événement : perdu */
			if ((u32)t == WEV_FRAME)
				break;
		}
		frames++;
		if ((u64)sys_time_ms() - t0 >= PT_FRAME_MS)
			break;
	}
	ak_puts("presenttest : WEV_FRAME ");
	print_dec((long)frames);
	ak_puts(" images en ");
	print_dec((long)((u64)sys_time_ms() - t0));
	ak_puts(" ms\n");
	if (frames < 10)
		fails++;
	goto bound;
lost:
	ak_puts("presenttest : WEV_FRAME perdu\n");
	fails++;

	/* --- attente bornée : ni trop courte, ni par sondage raté */
bound:
	for (int n = 0; n < 256; n++) /* (les événements en file) */
		if (!ui_event(0))
			break;
	t0 = (u64)sys_time_ms();
	long got = ui_event(PT_WAIT_MS);
	u64 dt = (u64)sys_time_ms() - t0;
	ak_puts("presenttest : ui_event(100) rendu en ");
	print_dec((long)dt);
	ak_puts(" ms\n");
	if (!got && dt < PT_WAIT_MS - 1) /* (un événement arrivé entre-temps : bon) */
		fails++;
	ak_puts(fails ? "presenttest : FAIL\n" : "presenttest : PASS\n");
	ui_close();
	_exit((int)fails);
}
