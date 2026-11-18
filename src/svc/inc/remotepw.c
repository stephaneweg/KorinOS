/* remotepw.c : le mot de passe de l'accès à distance (telnetd, ftpd, vncd,
 * rdpd ; docs/RESEAU.md), traduction de src/inc/remotepw.inc. Un seul pour les
 * quatre (UTF-8, 63 octets au plus). Relu à chaque connexion. Sans mot de passe :
 * toute connexion est refusée. Un service en C l'ajoute par son svc.conf
 * (« sources = ../inc/remotepw.c »). rp_read et rp_equal se partagent rp_pw et
 * leur tampon : un thread à la fois (verrou de l'appelant, comme l'asm).
 *
 * Avec le coffre à secrets (le PC : service keyvault, docs/etude/COFFRE.md) : le
 * mot de passe est le secret « remote-password » de l'espace @system (les
 * services sont d'uid 0 ; Réglages, élevé, l'écrit) ; /etc/remote.txt ne garde
 * que ses autres lignes et « password_vault = 1 » (Réglages sait ainsi qu'un mot
 * de passe est défini, sans le lire). Une ligne « password = ... » encore dans
 * remote.txt (ancien système, écrivain sans coffre) est la plus récente : elle
 * passe dans le coffre, puis le fichier est réécrit sans elle. Sans coffre (le
 * Pi, ou keyvault absent) : « password = ... » dans /etc/remote.txt, comme
 * avant. */
#include "korinrt.h"
#include "remotepw.h"
#define KV_SYSCALL(n, a, b) korin_sys2((n), (a), (b))
#include "../../lib/keyvault/keyvault.h"

#define RP_BUF 1024
#define RP_FILE "/etc/remote.txt"
#define RP_VAULT "remote-password"	/* le secret, dans l'espace @system */
#define RP_MARK "password_vault"	/* la ligne de remote.txt : « dans le coffre » */

char rp_pw[RP_MAX + 1];
static char rp_buf[RP_BUF];

/* -> 1 si le coffre répond (son programme d'abord : sans lui, pas d'attente de
 * 3 s à chaque connexion pour un service qui n'existe pas, le Pi) */
static int rp_vault(void)
{
	long st[2];
	if (sys_stat("/sys/svc/keyvault", st) < 0)
		return 0;
	return kv_request(KV_STATUS, KVF_SYSTEM, 0, 0, 0) >= 0;
}

/* p = début de ligne, key -> la valeur (après « clé », espaces, « = »,
 * espaces) si la ligne est celle de key, 0 sinon */
static const char *rp_key(const char *p, const char *key)
{
	while (*key && *key == *p) {
		key++;
		p++;
	}
	if (*key)
		return 0;
	while (*p == ' ' || *p == 9) /* espaces, « = », espaces */
		p++;
	if (*p != '=')
		return 0;
	p++;
	while (*p == ' ')
		p++;
	return p;
}

/* p -> la ligne suivante (0 à la fin) */
static const char *rp_next(const char *p)
{
	for (;;) {
		char c = *p++;
		if (!c)
			return 0;
		if (c == 10)
			return p;
	}
}

/* remote.txt (dans rp_buf, n octets) réécrit sans sa ligne « password = »
 * (line, jusqu'à end : 0 à la fin du fichier), avec RP_MARK -> 0 ou erreur */
static long rp_strip(long n, const char *line, const char *end)
{
	int mark = 0;
	for (const char *p = rp_buf; p && *p; p = rp_next(p))
		if (rp_key(p, RP_MARK))
			mark = 1;
	long fd = sys_open(RP_FILE, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long r = 0;
	if (line > rp_buf && sys_write(fd, rp_buf, (u64)(line - rp_buf)) < 0)
		r = -1;
	if (end && *end && sys_write(fd, end, (u64)(rp_buf + n - end)) < 0)
		r = -1;
	if (!mark) {
		const char *m = RP_MARK " = 1\n";
		if (end && *end && rp_buf[n - 1] != 10 && sys_write(fd, "\n", 1) < 0)
			r = -1;
		if (sys_write(fd, m, strlen(m)) < 0)
			r = -1;
	}
	sys_close(fd);
	return r;
}

/* -> longueur du mot de passe (0 : aucun), lui dans rp_pw (terminé par 0) (rp_lire) */
long rp_read(void)
{
	rp_pw[0] = 0;
	long n = 0;
	long fd = sys_open(RP_FILE, O_READ);
	if (fd >= 0) {
		n = sys_read(fd, rp_buf, RP_BUF - 1);
		sys_close(fd);
		if (n < 0)
			n = 0;
	}
	rp_buf[n] = 0;
	long len = 0;
	const char *line = 0, *q = 0;
	for (const char *p = rp_buf; p && *p; p = rp_next(p)) /* une ligne « password = ... » */
		if ((q = rp_key(p, "password"))) {
			line = p;
			break;
		}
	if (line) {
		while (q[len] && q[len] != 10 && q[len] != 13 && len < RP_MAX) { /* la valeur, jusqu'à la fin de ligne */
			rp_pw[len] = q[len];
			len++;
		}
		rp_pw[len] = 0;
		while (len && rp_pw[len - 1] == ' ') /* (espaces de fin retirées) */
			rp_pw[--len] = 0;
	}
	if (rp_vault()) {
		if (len) { /* encore en clair : dans le coffre, puis retiré du fichier */
			if (kv_sys_set(RP_VAULT, rp_pw, len) >= 0 && n < RP_BUF - 1) /* (fichier lu en entier) */
				rp_strip(n, line, rp_next(line));
		} else {
			long r = kv_sys_get(RP_VAULT, rp_pw, RP_MAX);
			len = r > 0 ? r : 0; /* (absent ou refusé : aucun, connexions refusées) */
			rp_pw[len] = 0;
		}
	}
	memset(rp_buf, 0, sizeof rp_buf);
	return len;
}

/* saisie, sa longueur -> 1 si c'est le mot de passe de rp_pw (rp_read avant) ;
 * même durée quel que soit le premier octet faux (rp_egal) */
int rp_equal(const void *in, u64 len)
{
	const u8 *s = in;
	u64 l = 0;
	while (rp_pw[l])
		l++;
	if (!l)
		return 0; /* aucun mot de passe : refusé */
	u64 diff = l ^ len; /* longueurs différentes -> non nul */
	for (u64 i = 0; i < l; i++)
		diff |= (u8)rp_pw[i] ^ (i < len ? s[i] : 0);
	return diff == 0;
}
