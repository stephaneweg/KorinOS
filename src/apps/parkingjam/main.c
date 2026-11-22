/*
 * parkingjam -- Parking Jam, un jeu fait avec Godot 4 sur Korin par Claude (le 18/11/2026) ; le projet est dans
 * project/ (l'ouvrir dans Godot, /apps/godot.app ; « Korin PC » l'exporte), son PCK dans data/main.pck.
 *
 * Le greffon Korin Export copie le modèle d'export de Godot (66 Mo) dans chaque jeu. Ce main le remplace : il
 * lance le modèle du paquet godot-templates dans le même process (execv) ; un jeu Godot ne pèse alors que son PCK.
 *
 * Le modèle trouve le PCK à côté de son exécutable (« main.pck » pour « main ») ; les modèles d'export sont
 * construits sans remplacement de chemin (--main-pack les fait s'arrêter). Sous Korin, Godot prend le chemin de
 * son exécutable dans argv[0] (tools/godot/platform/korin/os_korin.cpp) : on lui passe donc celui de ce main, et
 * il charge /apps/parkingjam.app/main.pck.
 *
 * Le paquet ne marque pas le modèle exécutable (/apps/godot.app/templates n'est pas dans /etc/exec.txt) : si
 * execv refuse (EACCES), on le marque (chmod +x, l'index /.exec) et on réessaie. Sans le modèle, on le dit.
 */
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uikit.h>

#define TEMPLATE	"/apps/godot.app/templates/template_release.x86_64"
#define SELF		"/apps/parkingjam.app/main"

static struct ui_vars ui;

int main (int argc, char **argv)
{
	char *av[40];
	int n = 0;
	av[n++] = (char *) SELF;		/* le chemin où Godot cherche main.pck */
	for (int i = 1; i < argc && n < 39; i++)
		av[n++] = argv[i];
	av[n] = 0;
	execv (TEMPLATE, av);
	if (errno == EACCES && chmod (TEMPLATE, 0755) == 0)
		execv (TEMPLATE, av);
	if (uikit_open (&ui))			/* (le modèle n'est pas installé) */
		ui_message ((long) "Parking Jam",
			    ui_tr ((long) "Ce jeu a besoin du paquet godot-templates : installez-le par Logiciels."), UM_OK);
	return 1;
}
