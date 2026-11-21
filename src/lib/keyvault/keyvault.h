/* keyvault.h : le client du coffre à secrets (service « keyvault », docs/KEYVAULT.md), en en-tête seul, pour
 * tout programme en C ou C++ : sur libposix (par défaut), sur korinrt ou dans un kit (définir KV_SYSCALL avant
 * l'inclusion). AppKit en fait ses fonctions vault_* (pour l'asm, le BASIC…).
 *
 * Le programme ne donne jamais son identité : le coffre la prend lui-même (le chemin vérifié de son exécutable,
 * ou de son script pour un interprète). Un secret rangé par un programme n'est lu que par lui.
 *
 *   long kv_set (const char *name, const void *value, long len)   -> 0
 *   long kv_get (const char *name, void *out, long max)           -> octets (max = 0 : la taille seule)
 *   long kv_delete (const char *name)                              -> 0
 *   long kv_list (char *out, long max)                             -> octets : un nom par ligne
 *   long kv_status (void)                                          -> KVS_*
 *   long kv_error (char *out, long max)                            -> octets : le texte de la dernière erreur
 *
 * Erreurs : E_NOENT (-2) pas de tel secret, E_ACCES (-13) coffre fermé ou refusé, E_PERM (-1) programme non
 * reconnu, E_CANCELED (-125) refusé à la carte, autres. Coffre fermé : le coffre ouvre la carte du mot de passe et
 * la fonction attend la réponse (au plus 5 minutes). */
#ifndef KORIN_KEYVAULT_H
#define KORIN_KEYVAULT_H

#ifndef KV_SYSCALL
#include <armpi/syscall.h>
#define KV_SYSCALL(n, a, b) armpi_syscall ((n), (long) (a), (long) (b), 0, 0, 0, 0)
#endif

#define KV_SERVICE "keyvault"
#define KV_NAME_MAX 128		/* nom d'un secret : lettres, chiffres, . _ - / @ : */
#define KV_VALUE_MAX 65536

/* opérations (a, b, c, d du message ; nom dans les données) */
#define KV_SET 1		/* c, d : la valeur -> 0 */
#define KV_GET 2		/* c, d : sortie -> octets */
#define KV_DELETE 3
#define KV_LIST 4		/* c, d : sortie -> octets */
#define KV_STATUS 5		/* -> KVS_* (le coffre du compte de l'appelant) */
#define KV_UNLOCK 6		/* données : nom du compte, 0, mot de passe (uid 0 : authd) ; ou le mot de passe seul (son compte) */
#define KV_LOCK 7		/* données : nom du compte (uid 0) ; vide : son compte */
#define KV_PASSWD 8		/* (uid 0 : authd) données : compte, 0, ancien, 0, nouveau -> 0 : réenveloppé */
#define KV_SETPW 9		/* (uid 0 : authd) données : compte, 0, nouveau -> 0 réenveloppé, 1 coffre vidé */
#define KV_CARD 10		/* (la carte) a = jeton, b = 1 annuler ; données : mot de passe */
#define KV_ERROR 11		/* c, d : sortie -> octets */
#define KV_WHOAMI 12		/* c, d : sortie, l'identité vue par le coffre -> octets */
/* le gestionnaire du coffre (Réglages › Coffre à secrets, et lui seul : /apps/control.app) : les secrets de tous les
 * programmes du compte. b & KVF_SYSTEM : le coffre du compte system (appelant d'uid 0, c'est-à-dire élevé ; ensuite
 * 5 min pour ce process, même revenu à son uid). Valeurs et effacements : le mot de passe du compte, vérifié par le
 * coffre, puis 5 min pour ce process (sans mot de passe : E_ACCES, « mot de passe demandé »). */
#define KV_MGR_LIST 13		/* c, d : sortie, « identité\tnom\ttaille\n » par secret -> octets */
#define KV_MGR_GET 14		/* données : identité, 0, nom, 0, mot de passe (ou rien) ; c, d : sortie -> octets */
#define KV_MGR_DELETE 15	/* données : identité, 0, nom, 0, mot de passe (ou rien) -> 0 */
#define KVF_PARENT 1		/* b : pour le process père (/bin/secret, lancé par un script) */
#define KVF_SYSTEM 2		/* b : l'espace « @system » du compte system, partagé par ses programmes (uid 0 seulement) :
				   un secret écrit par Réglages (élevé) et lu par un service */

#define KVS_LOCKED 0
#define KVS_OPEN 1
#define KVS_NONE 2		/* pas encore de coffre (il naîtra au premier secret) */

#define KV_SVC_OPEN 0x31
#define KV_SVC_CALL 0x32
#define KV_E_AGAIN -11
#define KV_E_PIPE -32
#define KV_E_INVAL -22

static long kv_chan_;		/* canal + 1 */
static long kv_parent_;		/* /bin/secret : le pid de son père (KVF_PARENT) ; 0 : soi */

static inline long kv_call_ (long op, long flags, const char *data, long dlen, const void *buf, long len)
{
	unsigned long m[64];
	for (int tries = 0; tries < 16; tries++) {
		if (!kv_chan_) {
			long c = KV_SYSCALL (KV_SVC_OPEN, KV_SERVICE, 3000);
			if (c < 0)
				return c;
			kv_chan_ = c + 1;
		}
		for (int i = 0; i < 64; i++)
			m[i] = 0;
		m[0] = (unsigned long) op;
		m[1] = (unsigned long) kv_parent_;
		m[2] = (unsigned long) (flags | (kv_parent_ ? KVF_PARENT : 0));
		m[3] = (unsigned long) buf;
		m[4] = (unsigned long) len;
		if (dlen < 0 || dlen > 460)
			return KV_E_INVAL;
		for (long i = 0; i < dlen; i++)
			((char *) m)[48 + i] = data[i];
		*(unsigned *) ((char *) m + 40) = (unsigned) dlen;
		long r = KV_SYSCALL (KV_SVC_CALL, kv_chan_ - 1, m);
		for (int i = 6; i < 64; i++)
			m[i] = 0;	/* (ni mot de passe ni nom ne restent ici) */
		if (r == KV_E_PIPE) {	/* (relancé par init2) */
			kv_chan_ = 0;
			continue;
		}
		if (r >= 0)
			r = (long) m[0];
		/* (E_AGAIN : la carte du coffre est ouverte depuis 20 s, on attend encore ; le noyau rend aussi la
		   réponse du service comme résultat de l'appel) */
		if (r != KV_E_AGAIN)
			return r;
	}
	return KV_E_AGAIN;
}

static inline long kv_name_len_ (const char *s)
{
	long n = 0;
	while (s && s[n] && n <= KV_NAME_MAX)
		n++;
	return n;
}

static inline long kv_request (long op, long flags, const char *name, const void *buf, long len)
{
	long n = kv_name_len_ (name);
	if (n > KV_NAME_MAX)
		return KV_E_INVAL;
	return kv_call_ (op, flags, name, name ? n + 1 : 0, buf, len);
}

static inline long kv_set (const char *name, const void *value, long len)
{
	return kv_request (KV_SET, 0, name, value, len);
}

static inline long kv_get (const char *name, void *out, long max)
{
	return kv_request (KV_GET, 0, name, out, max);
}

static inline long kv_delete (const char *name)
{
	return kv_request (KV_DELETE, 0, name, 0, 0);
}

static inline long kv_list (char *out, long max)
{
	return kv_request (KV_LIST, 0, 0, out, max);
}

static inline long kv_status (void)
{
	return kv_request (KV_STATUS, 0, 0, 0, 0);
}

/* l'espace « @system » (programmes d'uid 0 : services, Réglages élevé) */
static inline long kv_sys_set (const char *name, const void *value, long len)
{
	return kv_request (KV_SET, KVF_SYSTEM, name, value, len);
}

static inline long kv_sys_get (const char *name, void *out, long max)
{
	return kv_request (KV_GET, KVF_SYSTEM, name, out, max);
}

static inline long kv_sys_delete (const char *name)
{
	return kv_request (KV_DELETE, KVF_SYSTEM, name, 0, 0);
}

/* le gestionnaire (Réglages seul) : sys = 1 pour le coffre du compte system ; pw : 0 ou le mot de passe du compte */
static inline long kv_mgr_list (int sys, char *out, long max)
{
	return kv_call_ (KV_MGR_LIST, sys ? KVF_SYSTEM : 0, 0, 0, out, max);
}

static inline long kv_mgr_req_ (long op, int sys, const char *id, const char *name, const char *pw, void *out, long max)
{
	char d[460];
	long n = 0;
	const char *p[3] = { id, name, pw ? pw : "" };
	for (int k = 0; k < 3; k++) {
		for (long i = 0; p[k][i]; i++) {
			if (n >= (long) sizeof d - 1)
				return KV_E_INVAL;
			d[n++] = p[k][i];
		}
		d[n++] = 0;
	}
	long r = kv_call_ (op, sys ? KVF_SYSTEM : 0, d, n, out, max);
	for (long i = 0; i < n; i++)
		((volatile char *) d)[i] = 0;
	return r;
}

static inline long kv_mgr_get (int sys, const char *id, const char *name, const char *pw, void *out, long max)
{
	return kv_mgr_req_ (KV_MGR_GET, sys, id, name, pw, out, max);
}

static inline long kv_mgr_delete (int sys, const char *id, const char *name, const char *pw)
{
	return kv_mgr_req_ (KV_MGR_DELETE, sys, id, name, pw, 0, 0);
}

static inline long kv_error (char *out, long max)
{
	return kv_request (KV_ERROR, 0, 0, out, max);
}

#endif
