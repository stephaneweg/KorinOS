/* recent.h : les applications récentes, la plus récente d'abord, repris de
 * src/inc/recent.inc : /etc/recent.txt, un nom de paquet par ligne (« terminal »).
 * Tenues par la coquille qui voit les applications démarrer (apprun : le panel sur le
 * bureau, l'accueil en poche), lues par le lanceur (onglet « Récentes ») et l'accueil
 * (rangée « Récentes »).
 *
 *	applis_load(); rc_load();		rc_n, rc_app (applications)
 *	rc_touch(app);				devant, et le fichier réécrit */
#ifndef KORIN_RECENT_H
#define KORIN_RECENT_H

#include "applist.h"

#define RC_MAX 12
#define RC_BUF (RC_MAX * AP_NOM + 64)
#define RC_PATH "/etc/recent.txt"

extern long rc_n;
extern u8 rc_app[RC_MAX];

void rc_load(void);       /* rc_app <- les applications du fichier connues du catalogue */
void rc_touch(long app);  /* devant la liste, le fichier réécrit */
void rc_save(void);       /* /etc/recent.txt <- rc_app */

#endif
