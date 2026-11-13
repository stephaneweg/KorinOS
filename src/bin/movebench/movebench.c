/* movebench - coût d'un déplacement de fenêtre dans Elegant, pour comparer deux serveurs
 * graphiques (asm et C, docs/etude/PC-PILOTES.md, « Bureau Elegant en C ») :
 *   movebench [secondes]   attend (20 s par défaut : bureau ouvert), ouvre une fenêtre de
 *                          800 x 520, puis la déplace 120 fois de 2 points en largeur et 1 en hauteur (WS_OP_WIN_MOVE,
 *                          comme rdpd) ; après chaque pas, une image demandée (ui_frame_request) :
 *                          le WEV_FRAME arrive quand Elegant a recomposé l'écran avec la fenêtre
 *                          à sa nouvelle place. Affiche le temps moyen d'un pas.
 * Elegant n'envoie pas plus d'un WEV_FRAME toutes les 16 ms (WS_FRAME_MS) : un temps de 16 à
 * 17 ms veut dire « une image par rafraîchissement », pas plus lent. */
#include "korinrt.h"
#include "uikit_import.h"

#define STEPS 120
#define WS_OP_WIN_MOVE 16

static struct krt_ui ui;
static long msg[WS_MSG / 8];

/* attend le WEV_FRAME de la fenêtre (2 s au plus) -> 1 s'il est venu */
static int wait_frame(void)
{
	long t0 = sys_time_ms();
	while (sys_time_ms() - t0 < 2000) {
		long t = ui_event(100);
		if (t == WEV_FRAME)
			return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long wait = 20000;
	if (krt_line && krt_line[0])
		wait = (long)(parse_dec(krt_line) * 1000);
	sys_sleep(wait);
	ui_init((long)&ui);
	long id = ui_window_ex(60, 80, 800, 520, (long)"movebench", 0);
	if (id < 0) {
		ak_puts("movebench : pas de fenetre, erreur ");
		print_dec(id);
		ak_puts("\n");
		return 1;
	}
	ui_update_all();
	ui_frame_request();
	wait_frame();
	sys_sleep(500);
	long lost = 0;
	long t0 = sys_time_ms();
	for (int i = 1; i <= STEPS; i++) {
		msg[WM_OP / 8] = WS_OP_WIN_MOVE;
		msg[WM_A / 8] = id;
		msg[WM_B / 8] = 60 + 2 * i;
		msg[WM_C / 8] = 80 + i;
		korin_sys1(SYS_WS_CALL, (long)msg);
		ui_frame_request();
		if (!wait_frame())
			lost++;
	}
	long ms = sys_time_ms() - t0;
	ak_puts("movebench : ");
	print_dec(STEPS);
	ak_puts(" pas de 800x520 en ");
	print_dec(ms);
	ak_puts(" ms : ");
	print_dec(ms * 10 / STEPS / 10);
	ak_puts(",");
	print_dec(ms * 10 / STEPS % 10);
	ak_puts(" ms par pas");
	if (lost) {
		ak_puts(" (images perdues : ");
		print_dec(lost);
		ak_puts(")");
	}
	ak_puts("\n");
	ui_window_close(id);
	return 0;
}
