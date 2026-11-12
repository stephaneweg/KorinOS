/* remotepw.h : le mot de passe de l'accès à distance (remotepw.c, comme
 * src/inc/remotepw.inc). */
#ifndef REMOTEPW_H
#define REMOTEPW_H

#define RP_MAX 63

extern char rp_pw[RP_MAX + 1]; /* en clair (VNC en a besoin pour son défi DES) */
long rp_read(void);                       /* -> longueur (0 : aucun, refuser) */
int rp_equal(const void *in, u64 len);    /* -> 1 si c'est le bon */

#endif
