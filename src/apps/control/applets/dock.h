/*
 * applets/dock.h -- ce que les volets Dock, Panel et Imprimantes partagent (applets/dock.cpp, dockconf.cpp) :
 *   * les réglages du dock (src/inc/dockconf.inc : SD0:/etc/dock.ini, la structure DockLayout du dockconf.h
 *     d'Onyx), dont le panel reprend les bureaux, la taille et la corbeille ;
 *   * les aides des volets en widgets de dock.inc : les widgets à retirer en quittant le volet (dk_top,
 *     dk_close), leurs évènements (dk_on), infobulles (dk_tip), le message du bas, les bureaux.
 * (src/sys/common/dockconf.c est la même chose pour korinrt : repris ici sur le catalogue d'applist.cpp.)
 */
#ifndef CONTROL_DOCK_H
#define CONTROL_DOCK_H

#include "../control.h"

/* ---- dockconf.cpp (src/inc/dockconf.inc) ---- */
enum
{
	DOCK_MAXLAUNCHERS = 6,
	DOCK_MAXDESKS = 6,
	DL_DESK = 24,				/* nom d'un espace (NUL compris) */
};

extern long dl_ncats, dl_nlaunchers, dl_ndesks;
extern long dl_trash;				/* corbeille : 1 oui (dl_corbeille) */
extern long dl_size;				/* taille : -1 auto, 0 grande, 1 moyenne, 2 petite (dl_taille) */
extern unsigned char dl_cat[CAT_MAX];		/* par ligne (l'ordre du dock) : catégorie (catalogue), */
extern unsigned char dl_hidden[CAT_MAX];	/* 1 : sans tiroir, */
extern unsigned char dl_napps[CAT_MAX];		/* ses applications sur la carte, */
extern char dl_app[CAT_MAX][APP_NAME];		/* son application principale (vide : aucune) */
extern char dl_launcher[DOCK_MAXLAUNCHERS][APP_NAME];
extern char dl_desk[DOCK_MAXDESKS][DL_DESK];
extern unsigned char ap_order[APP_MAX];		/* les applications par libellé (apps_sort) */

#define DL_PATH		"SD0:/etc/dock.ini"
#define DL_DEFAULT_TERMINAL "terminal"
#define DL_DEFAULT_FILES "files"

long dk_ci_cmp (const char *a, const char *b);	/* comparaison sans la casse (ASCII) */
void dk_copy_n (char *d, const char *s, long n);	/* copie bornée (n : NUL compris) */
long dock_category_docked (const char *cat);
void dl_line (char *key, char *val, void *data);	/* (ini_read de dock.ini) */
void dl_launcher_add (const char *name);
void dl_desks_default (void);			/* aucun espace nommé : 4, de 1 à 4 */
long dock_layout_load (void);			/* dock.ini fusionné au catalogue -> 0 sans fichier */
const char *dl_cat_name (long row);
void dl_swap (long a, long b);
const char *app_label (long app);
void apps_sort (void);
long dl_main (long row);			/* l'application principale (-1 : aucune) */

/* ---- dock.cpp : les aides des volets en widgets (applets/dock.inc) ---- */
enum
{
	DK_W = 700,				/* le volet d'Onyx */
	DK_H = 470,
	DK_CT = 24,				/* haut du contenu d'un groupe */
	DK_BTN = 28,				/* hauteur d'un bouton */
	DK_TOPS = 16,
};

typedef void (*dk_fn) (long w, long code);	/* (ui_w_on) x0 = widget, x1 = code UN_* */

extern long dk_ntop;
extern long dk_w_launchers, dk_w_all, dk_w_count, dk_w_message, dk_w_size, dk_w_trash;
extern long dk_w_number[DOCK_MAXDESKS], dk_w_name[DOCK_MAXDESKS];
extern long dk_nall;				/* toutes les applications, sauf les parties du bureau */
extern unsigned char dk_all[APP_MAX];
extern char dk_file[4096];			/* le fichier écrit */

long dk_top (long w);				/* widget à retirer en quittant le volet -> w */
long dk_on (long w, dk_fn fn);			/* -> w */
long dk_tip (long w, const char *tip);		/* -> w */
void dk_close (void);				/* (ap.close) les widgets s'en vont */
void dk_draw (void);				/* (ap.draw) ui_w_draw (0) */
void dk_widgets (void);				/* ap.draw, ap.close, ap.widgets = 1, dk_ntop = 0 */
const char *dk_label (const char *name);	/* le libellé d'une application (son nom si elle n'y est pas) */
void dk_show_desks (void);			/* les bureaux montrés, leurs noms */
void dk_message (const char *text);
void dk_read_name (long desk, int fill);	/* son nom <- son champ (fill : un nom vide devient son numéro) */
char *dk_put (char *o, const char *text);	/* ajoute au fichier -> fin */
void dk_on_count (long w, long code);		/* (le nombre de bureaux) */
void dk_on_size (long w, long code);
void dk_on_trash (long w, long code);
long dk_desks_group (long x, long y, long h, const char *tip);	/* le groupe Bureaux -> sa poignée */
void dk_bottom (long pw, long ph, dk_fn apply, dk_fn restore);	/* le message, Appliquer, Rétablir */
void dk_header (long right);			/* (dans l'en-tête) la taille, la corbeille */
char *dk_put_tail (char *o);			/* desk, corbeille, taille : la fin du fichier */

/* les textes partagés de dock.inc */
extern const char dk_s_up[], dk_s_down[], dk_t_left[], dk_t_right[], dk_s_add[], dk_s_remove[], dk_t_all[],
	dk_s_choose[], dk_s_empty[];

#endif
