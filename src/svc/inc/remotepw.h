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
/* un essai d'un service qui vérifie lui-même (le défi de VNC, le HMAC de RDP) : ok 1 ou 0, pour le compteur (S3) */
void rp_note(int ok);
/* VNC (S4) : son propre mot de passe (le secret « remote-vnc » du coffre, 8 octets au plus comptent) ; sans lui, le
 * secret commun hors du mode comptes ; en mode comptes, 0 : refusé (le défi DES de VNC ne peut pas servir un
 * compte). -> longueur, lui dans rp_pw */
long rp_read_vnc(void);

#endif
