/* panelconf.h : les réglages du panel (SD0:/etc/panel.ini), repris de src/inc/panelconf.inc ;
 * communs au panel (sys/panel), à l'accueil du mode poche et à l'applet du panneau de
 * configuration. Reprend de dockconf dl_ndesks, dl_desk, dl_trash, dl_size et la lecture de
 * ces clés. Une ligne par réglage :
 *
 *	pin       = terminal		un raccourci, dans l'ordre (12 au plus)
 *	desk      = Principal		un espace de travail, son nom (1 à 6)
 *	corbeille = oui | non
 *	taille    = auto | grande | moyenne | petite
 *
 * Sans panel.ini : dock.ini de l'ancien dock (ses lanceurs deviennent des raccourcis),
 * sinon terminal et fichiers.
 *
 *	applis_load(); pan_load();		pan_npin, pan_pin */
#ifndef KORIN_PANELCONF_H
#define KORIN_PANELCONF_H

#include "dockconf.h"

#define PANEL_MAXPINS 12
#define PAN_PATH "SD0:/etc/panel.ini"

extern long pan_npin;
extern char pan_pin[PANEL_MAXPINS][AP_NOM];

void pan_line(char *key, char *val); /* (ini_read) une ligne */
void pan_add(const char *name);      /* un raccourci de plus */
void pan_load(void);
/* application -> 1 si elle est une partie du bureau (catégorie Shell : ni dans le lanceur,
 * ni parmi les raccourcis) */
long pan_shell(long app);

#endif
