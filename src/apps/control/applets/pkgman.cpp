/*
 * applets/pkgman.cpp -- le volet Logiciels du panneau de configuration : les paquets ont leur fenêtre, pkgman
 * (docs/PAQUETS.md) ; choisir le volet la lance, le bouton « Ouvrir » la rouvre.
 *
 * Port en C++ de applets/pkgman.inc (Korin). Le pkgman d'Onyx était une app à part (user/Apps/pkgman) : Korin ne
 * garde dans Réglages qu'un renvoi vers la sienne.
 */
#include "../control.h"

static void sw_apply (void) { spawn ("/apps/pkgman.app/main", 0); }

static void sw_open (void)
{
	applet_row ("Mises \xC3\xA0 jour, paquets install\xC3\xA9s, paquets disponibles : dans la fen\xC3\xAAtre Logiciels.",
		R_NOTE);
	ap.button = "Ouvrir";
	sw_apply ();
}

const Applet pkgman_applet = { "pkgman", "Logiciels", "Logiciels", "pkgman", sw_open, sw_apply };
