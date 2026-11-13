/* apprun.h : les applications qui tournent, pour la coquille (le panel, l'accueil du mode
 * poche), repris de src/inc/apprun.inc : un pid par application du catalogue (le noyau
 * nomme le process d'un paquet d'après lui), et l'ouverture d'une application (sa fenêtre
 * ramenée, sinon lancée). Une application qui démarre passe devant les récentes (recent.h),
 * sauf au premier tour (ce qui tournait déjà). Le programme importe uikit (ui_winlist,
 * ui_raise) et a fait ui_init.
 *
 *	apps_running();		pids : != 0 si quelque chose a changé
 *	app_open(app); */
#ifndef KORIN_APPRUN_H
#define KORIN_APPRUN_H

#include "recent.h"

#define APPRUN_PROCS 64 /* process lus par morceau (SYS_PROCLIST_FROM ; MAXPROCS de l'asm) */
#define PE_SIZE 64      /* entrée de proclist : nom en +24 */

extern long pids[AP_MAX]; /* pid de chaque application (0 : ne tourne pas) */

/* pids des applications qui tournent (tous les process, par morceaux) -> nombre de
 * changements (0 : rien) */
long apps_running(void);
/* ramène la fenêtre de l'application (réduite aussi), ou la lance */
void app_open(long app);

#endif
