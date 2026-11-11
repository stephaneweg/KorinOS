// main.c -- la plus petite app graphique de Korin OS (guide : SD0:/apps/help.app/api/app_fr.md).
// Construire sous Korin :  pibuild app <ce dossier> <nom>   ->  /apps/<nom>.app/main
#include <uikit.h>

static struct ui_vars ui;

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	const unsigned *jet = (const unsigned *) ui_jetons ();		// les couleurs du theme
	ui_window (360, 160, (long) "Bonjour");
	ui_fill (0, 0, ui.w, ui.h, jet[TJ_FACE / 4] & 0xFFFFFF);		// le fond des fenetres
	ui_text ((long) "Une app de Korin OS.", 20, 70, jet[TJ_TEXTE1 / 4] & 0xFFFFFF, 15, (long) ui.font, 0);
	long b = ui_pushbutton (20, 100, 140, 32, (long) "Bonjour", UB_DEFAULT);
	ui_w_draw (0);
	ui_update_all ();
	for (;;)
	{
		long t = ui_event (100);
		if (t == WEV_CLOSE)
			break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			if (uikit_w_event (&code, &taken) == b && code == UN_ACTIVATE)
				ui_message ((long) "Bonjour", (long) "Bonjour, le monde !", UM_OK);
		}
	}
	return 0;
}
