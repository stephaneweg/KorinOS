/* remotepw.h : le mot de passe de l'accès à distance (remotepw.c, comme
 * src/inc/remotepw.inc). */
#ifndef REMOTEPW_H
#define REMOTEPW_H

#define RP_MAX 63

extern char rp_pw[RP_MAX + 1]; /* en clair (VNC en a besoin pour son défi DES) */
long rp_read(void);                       /* -> longueur (0 : aucun, refuser) */
int rp_equal(const void *in, u64 len);    /* -> 1 si c'est le bon */
/* les comptes (E70) : 1 si l'accès à distance passe par les comptes (un compte d'uid >= 1000 a un mot de passe,
 * et remote.txt ne dit pas « shared = 1 ») ; 0 : le secret commun (le Pi, les images d'essai sans mot de passe) */
int rp_accounts(void);
/* compte, mot de passe -> son uid (> 0), ou < 0 : refusé (authd, AUTH_VERIFY ; jamais l'uid 0) ; *home : son dossier */
long rp_account(const char *name, const char *pw, char *home, u64 cap);
/* trop d'échecs récents (secret commun et comptes, par service) : refuser sans essayer (S3) */
int rp_locked(void);

#endif
