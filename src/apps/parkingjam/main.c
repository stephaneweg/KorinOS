/*
 * parkingjam -- Parking Jam, un jeu fait avec Godot 4 sur Korin par Claude (le 18/11/2026) ; le projet est dans
 * project/ (l'ouvrir dans Godot, /apps/godot.app ; « Korin PC » l'exporte), son PCK dans data/main.pck.
 *
 * Le greffon Korin Export copie le modèle d'export de Godot (66 Mo) dans chaque jeu. Ce main le remplace : il
 * lance le modèle du paquet godot-templates sur le PCK du jeu (--main-pack), dans le même process (execv) ; un jeu
 * Godot ne pèse alors que son PCK. Sans le modèle, il le dit.
 */
#include <unistd.h>
#include <uikit.h>

#define TEMPLATE	"/apps/godot.app/templates/template_release.x86_64"
#define PCK		"/apps/parkingjam.app/main.pck"

static struct ui_vars ui;

int main (int argc, char **argv)
{
	char *av[40];
	int n = 0;
	av[n++] = (char *) TEMPLATE;
	av[n++] = (char *) "--main-pack";
	av[n++] = (char *) PCK;
	for (int i = 1; i < argc && n < 39; i++)
		av[n++] = argv[i];
	av[n] = 0;
	execv (TEMPLATE, av);
	if (uikit_open (&ui))			/* (le modèle n'est pas installé) */
		ui_message ((long) "Parking Jam",
			    ui_tr ((long) "Ce jeu a besoin du paquet godot-templates : installez-le par Logiciels."), UM_OK);
	return 1;
}
