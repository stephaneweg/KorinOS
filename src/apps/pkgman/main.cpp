//
// pkgman -- Logiciels, le gestionnaire de paquets de Korin (docs/PAQUETS.md) : la fenêtre autour de la liste de
// pkgui.cpp, que le volet Logiciels de Réglages montre aussi (applet/applet.cpp, sys/applets/pkg.so). Les paquets
// eux-mêmes passent par la commande pkg (pkg ui-list, pkg ui-job : src/bin/pkg/main.cpp).
//
//   pkgman              la fenêtre (une seule à la fois)
//   pkgman --updates    ouverte sur l'onglet Mises à jour
//   pkgman --restart    (après sa propre mise à jour : la nouvelle version prend la place de l'ancienne)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include "uikit.h"
#include "pkgui.h"

struct ui_vars ui;

enum { CAP = FRAME_CAP_W + 8 };
enum { SYS_PROCLIST_ = 0x15, SYS_SPAWN_ = 0x8B };
extern "C" long __armpi_fsys (long n, long a, long b, long c);

// un autre pkgman (la liste des process, comme proc_running de pkglib.h)
static bool other_running (void)
{
	static unsigned char t[128 * 64];
	long n = armpi_syscall (SYS_PROCLIST_, (long) t, 128, 0, 0, 0, 0);
	long self = getpid ();
	for (long i = 0; i < n; i++)
		if (*(long *) (t + i * 64) != self && !strcmp ((const char *) t + i * 64 + 24, "pkgman")) return true;
	return false;
}

// le gestionnaire s'est mis à jour : celui qui tourne est l'ancien ; le nouveau prend sa place
static void self_updated (void)
{
	pkgui_status ("Le gestionnaire s'est mis à jour : il redémarre...");
	ui_view_flush ();
	if (__armpi_fsys (SYS_SPAWN_, (long) PKGMAN_APP, (long) "--restart", 0) > 0) exit (0);
}

static long face (void) { return ((const unsigned *) ui_tokens ())[0] & 0xFFFFFF; }
// (une surface neuve : le panel qui repart change aussi la place de la fenetre)
static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, face ());
	pkgui_layout (0, 0, w, h);
	ui_w_draw (0); ui_view_flush (); ui_update_all ();
	return 0;
}

int main (int argc, char **argv)
{
	bool restart = argc > 1 && !strcmp (argv[1], "--restart");
	if (!restart && other_running ()) return 0;		// (un seul : deux fenêtres se gêneraient)
	if (!uikit_open (&ui)) return 1;
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = (int) scr[0] - 8, wh = (int) (scr[1] - scr[3] - scr[2]) - 32;
	if (ww > 780) ww = 780;
	if (wh > 540) wh = 540;
	long win = ui_window_ex ((scr[0] - ww) / 2, scr[3] + 8, ww, wh, (long) ui_tr ((long) "Logiciels"), WF_RESIZABLE);
	if (win <= 0) return 1;
	ui_min_size (640, 360);
	ui_on_resize ((long) on_resize, 0);
	pkgui_self_updated = self_updated;
	pkgui_open (0, 0, ui.w, ui.h, CAP, argc > 1 && !strcmp (argv[1], "--updates"));
	for (;;)
	{
		long t = ui_event (30);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			uikit_w_event (&code, &taken);
		}
		else ui_w_idle ();
		pkgui_poll ();
	}
	pkgui_close ();					// (un travail en cours va au bout)
	return 0;
}
