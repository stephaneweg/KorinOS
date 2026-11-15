/*
 * applet.cpp -- le volet Logiciels de Réglages, installé avec le gestionnaire (paquet pkgman) : /sys/applets/pkg.so,
 * chargé par Réglages (src/apps/control/control.h, « les applets installées »). Il porte le nom du volet intégré
 * (pkgman, applets/pkgman.cpp, qui ne fait que lancer la fenêtre) et prend sa place : « control pkgman » l'ouvre.
 *
 * La liste est celle de la fenêtre Logiciels (../pkgui.cpp : onglets, recherche, lignes, progression) ; les paquets
 * passent par la commande pkg (pkg ui-list, pkg ui-job), OpenSSL reste dans pkg. Changer de volet pendant un
 * travail ne l'arrête pas : il va au bout, le volet rouvert le retrouve.
 */
#include "control.h"
#include "../pkgui.h"

static void pk_draw (void) { pkgui_redraw (); }
static int pk_tick (void) { pkgui_poll (); return 0; }
static void pk_close (void) { pkgui_close (); }

/* (le module chargé reste l'ancien jusqu'au prochain lancement de Réglages ; la commande pkg est déjà la nouvelle) */
static void pk_self (void)
{
	pkgui_status ("Logiciels s'est mis à jour : sa nouvelle version au prochain lancement de Réglages.");
}

static void pk_open (void)
{
	ap.draw = pk_draw;
	ap.close = pk_close;
	ap.tick = pk_tick;
	ap.widgets = 1;
	pkgui_self_updated = pk_self;
	pkgui_open (SIDE_W, AP_HEAD, ui.w - SIDE_W, ui.h - AP_HEAD, 12, false);	/* (12 : la marge à droite) */
}

static void pk_apply (void) {}

extern "C" {
extern const int korin_applet_abi;
extern const Applet korin_applet;
const int korin_applet_abi = APPLET_ABI;
const Applet korin_applet = { "pkgman", "Logiciels", "Logiciels", "pkgman", pk_open, pk_apply, PKG_CMD };
}
