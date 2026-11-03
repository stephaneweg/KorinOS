/*
 * uitest.c -- essai de UIKit depuis le C (src/kits/uikit/include/uikit.h, interface generee par
 * tools/kits/abi2c.py ; docs/WEBKIT.md, etape 5) : une fenetre, un fond, un degrade dessine
 * directement dans ses pixels, du texte TrueType, un bouton, la mise a jour, puis les evenements
 * pendant 3 s (ou jusqu'a la fermeture).
 *   uitest [secondes]   -> « uitest: ok ... »
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <uikit.h>

static struct ui_vars ui;

int main (int argc, char **argv)
{
	int seconds = argc > 1 ? atoi (argv[1]) : 3;
	if (!uikit_open (&ui))
	{
		printf ("uitest: FAIL UIKit ou ses polices absents\n");
		return 1;
	}
	long win = ui_window (420, 260, (long) "uitest : UIKit depuis le C");
	if (win <= 0 || ui.surf == 0)
	{
		printf ("uitest: FAIL pas de fenetre (%ld)\n", win);
		return 1;
	}
	ui_fill (0, 0, ui.w, ui.h, UI_BG);
	for (long y = 60; y < 120; y++)			/* les pixels a la main */
		for (long x = 20; x < ui.w - 20; x++)
			ui.surf[y * ui.w + x] = (uint32_t) ((x * 255 / ui.w) << 16 | (y * 2) << 8 | 0x80);
	ui_text ((long) "Bonjour depuis le C (UIKit, AAPCS64)", 20, 40, UI_FG, 18, (long) ui.font, ui.w - 40);
	ui_button (20, 150, 160, 32, (long) "Un bouton", 1);
	long width = ui_text_width ((long) "Bonjour", 18, (long) ui.font);
	ui_update_all ();
	printf ("uitest: fenetre %ld, %ld x %ld, texte « Bonjour » = %ld px\n", win, ui.w, ui.h, width);
	fflush (stdout);
	int events = 0;
	time_t end = time (0) + seconds;
	while (time (0) < end)
	{
		long t = ui_event (100);
		if (t > 0)
			events++;
		if (t == WEV_CLOSE)
			break;
	}
	ui_close ();
	printf ("uitest: ok (%d evenements)\n", events);
	return 0;
}
