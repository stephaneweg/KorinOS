/* applist.h : le catalogue des applications de la session du bureau (panel, menubar),
 * repris de src/inc/applist.inc : les paquets /apps/<nom>.app (main : l'exécutable ;
 * app.txt : le manifeste, clés d'Onyx « name » et « category »). ini_read, ini_trim et
 * str_copy_n viennent de svclib (src/svc/svclib, toujours lié par tools/sys/build_c.sh). */
#ifndef KORIN_APPLIST_H
#define KORIN_APPLIST_H

#include "svclib.h"

#define AP_MAX 128
#define AP_NOM 32 /* nom (paquet sans .app) */
#define AP_LIB 40 /* libellé UTF-8 */
#define CA_MAX 24
#define CA_NOM 24

extern long ap_n, ca_n;            /* applications, catégories */
extern char ap_nom[AP_MAX][AP_NOM]; /* noms */
extern char ap_lib[AP_MAX][AP_LIB]; /* libellés (name= traduit par lang_tr) */
extern u8 ap_cat[AP_MAX];           /* catégorie de chaque application */
extern char ca_nom[CA_MAX][CA_NOM]; /* noms des catégories (clés : lang_tr à l'affichage) */

/* « a, b, c » -> premier champ (sans blancs) ; *rest = la suite (0 : aucune) ; coupe sur place */
char *ini_field(char *s, char **rest);
long app_find(const char *name);   /* -> application, -1 : inconnue */
long cat_find(const char *name);   /* -> catégorie (ajoutée si nouvelle ; -1 : plus de place) */
void applis_load(void);            /* le catalogue (sans manifeste ni catégorie : « Autres ») */
long app_launch(long app);         /* /apps/<nom>.app/main -> pid ou erreur (SYS_SPAWN) */
/* catégorie -> 1 si le lanceur et le menu Korin ne la montrent pas : « Émulateurs » (comme
 * « Emulators » d'Onyx) ; les émulateurs se lancent par la Game Library ou par leurs fichiers */
long cat_hidden(long c);

#endif
