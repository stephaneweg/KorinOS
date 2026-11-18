/* shares.h : les dossiers partagés (/etc/shares.ini) et les comptes de smbd (/etc/smbd.ini), lus et
 * écrits par smbd et par le volet « Partage de fichiers » de Réglages (applet/applet.cpp ; docs/SMB.md).
 *
 * shares.ini, une section par partage, commun à tous les types (smb aujourd'hui, ftp plus tard) :
 *
 *   [Korin]                    le nom du partage (SMB : \\korin\Korin)
 *   path = HOME:/           le dossier partagé
 *   type = smb                 smb | ftp
 *   readonly = no              yes : lecture seule
 */
#ifndef KORIN_SHARES_H
#define KORIN_SHARES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHARES_FILE "/etc/shares.ini"
#define SMBD_FILE "/etc/smbd.ini"
#define SHARES_MAX 32
#define SHARE_NAME 80
#define SHARE_PATH 1024

enum { SHARE_SMB = 0, SHARE_FTP = 1 };

struct share_ent {
	char name[SHARE_NAME];
	char path[SHARE_PATH];
	int type;     /* SHARE_* */
	int readonly;
};

/* -> nombre de partages lus (0 : fichier absent ou vide) */
int shares_load(const char *file, struct share_ent *out, int max);
/* -> 0, -1 si le fichier ne peut pas être écrit */
int shares_save(const char *file, const struct share_ent *s, int n);
const char *share_type_name(int type);
/* un nom de partage SMB acceptable (pas de \ / : * ? " < > |, ni vide, ni trop long) -> 1 */
int share_name_ok(const char *name);

/* smbd.ini, [users] : le premier compte (nom et mot de passe) -> 1 s'il y en a un */
int smbd_account_get(const char *file, char *user, int usz, char *pw, int psz);
/* le compte remplace ceux de [users] (le reste du fichier gardé) -> 0 */
int smbd_account_set(const char *file, const char *user, const char *pw);

/* le coffre à secrets (le PC, service keyvault, docs/etude/COFFRE.md) : le mot de passe d'un compte est le secret
 * « smbd/<compte> » de l'espace @system (uid 0 : smbd lancé par init2, Réglages élevé) ; smbd.ini garde
 * « compte = » (valeur vide). smbd y range au chargement un mot de passe encore en clair. Sans coffre : en clair dans
 * smbd.ini, comme avant. */
int smbd_vault_present(void); /* son programme est là (le PC) : un compte sans mot de passe est alors refusé */
int smbd_vault_ok(void);      /* il répond (appelant d'uid 0) */
void smbd_vault_name(char *out, size_t size, const char *user);
/* (uid 0) le compte : son mot de passe dans le coffre s'il répond (smbd.ini sans lui), sinon dans smbd.ini ; pw vide :
 * celui de old_user (dans le coffre) gardé -> 0, -1 fichier, -3 pas de mot de passe */
int smbd_account_store(const char *file, const char *user, const char *pw, const char *old_user);

#ifdef __cplusplus
}
#endif

#endif
