/* dockconf.h : les réglages du dock (SD0:/etc/dock.ini), repris de src/inc/dockconf.inc ;
 * communs au dock (sys/dock), au panel (par panelconf), à l'accueil du mode poche et à
 * l'applet Dock du panneau de configuration (dockconf.h d'Onyx : DockLayout,
 * dock_layout_load). Une ligne par réglage :
 *
 *	drawer   = Accessoires, editor	un tiroir : sa catégorie, son application
 *					principale (vide : la première par nom), dans
 *					l'ordre du dock
 *	hidden   = Jeux			une catégorie sans tiroir
 *	launcher = terminal		un lanceur après les tiroirs (6 au plus)
 *	desk     = Principal		un espace de travail, son nom (1 à 6)
 *	corbeille = oui | non		(ARMPI)
 *	taille    = auto | grande | moyenne | petite (ARMPI)
 *
 * Les clés tiroir et lanceur sont lues comme drawer et launcher. Le dock a un tiroir pour
 * CHAQUE catégorie des applications de la carte, sauf les cachées et Shell, Settings,
 * Emulators (et leurs noms français) : celles que le fichier ne nomme pas viennent à la
 * fin, par nom.
 *
 *	applis_load(); dock_layout_load();	dl_* (ligne : l'ordre du dock)
 *	apps_sort();				ap_order : les applications par libellé */
#ifndef KORIN_DOCKCONF_H
#define KORIN_DOCKCONF_H

#include "applist.h"

#define DOCK_MAXLAUNCHERS 6
#define DOCK_MAXDESKS 6
#define DL_DESK 24 /* nom d'un espace (NUL compris) */

#define DL_PATH "SD0:/etc/dock.ini"
#define DL_KEY_LAUNCHER "launcher" /* (reprises par panelconf) */
#define DL_KEY_LANCEUR "lanceur"
#define DL_DEFAULT_TERMINAL "terminal" /* sans fichier : terminal, fichiers */
#define DL_DEFAULT_FILES "files"

extern long dl_ncats;     /* lignes (catégories du dock) */
extern long dl_nlaunchers;
extern long dl_ndesks;
extern long dl_trash;     /* corbeille = oui (1) | non (0) */
extern long dl_size;      /* -1 auto, 0 grande, 1 moyenne, 2 petite */
extern u8 dl_cat[CA_MAX];    /* par ligne : catégorie (catalogue), */
extern u8 dl_hidden[CA_MAX]; /* 1 : sans tiroir, */
extern u8 dl_napps[CA_MAX];  /* ses applications sur la carte, */
extern char dl_app[CA_MAX][AP_NOM]; /* son application principale (vide : aucune) */
extern char dl_launcher[DOCK_MAXLAUNCHERS][AP_NOM];
extern char dl_desk[DOCK_MAXDESKS][DL_DESK];
extern u8 ap_order[AP_MAX]; /* les applications par libellé (apps_sort) */

/* < 0, 0, > 0 : a et b comparées sans la casse (ASCII), comme str_ci_cmp de l'asm */
long str_ci_cmp(const char *a, const char *b);
/* catégorie (texte) -> 1 si elle peut avoir un tiroir */
long dock_category_docked(const char *cat);
long dl_find(long cat);  /* catégorie du catalogue -> sa ligne (-1 : aucune) */
long dl_add(long cat);   /* -> sa ligne (nouvelle s'il le faut ; -1 : plus de place) */
void dl_line(char *key, char *val); /* (ini_read de dock.ini) une ligne */
void dl_launcher_add(const char *name); /* un lanceur de plus (6 au plus) */
long dock_layout_load(void); /* dock.ini fusionné avec le catalogue -> 0 sans fichier */
void dl_desks_default(void); /* aucun espace nommé : 4, de « 1 » à « 4 » */
const char *dl_cat_name(long row); /* ligne -> le nom de sa catégorie */
void dl_swap(long a, long b);
void apps_sort(void);                 /* ap_order <- les applications par libellé */
char *app_label(long app);            /* application -> son libellé (ap_lib) */
long dl_main(long row); /* -> l'application principale de la ligne (-1 : aucune) ; apps_sort fait */

#endif
