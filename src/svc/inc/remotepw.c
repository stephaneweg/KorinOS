/* remotepw.c : le mot de passe de l'accès à distance (telnetd, ftpd, vncd,
 * rdpd ; docs/RESEAU.md), traduction de src/inc/remotepw.inc. Un seul pour les
 * quatre, dans SD0:/etc/remote.txt : « password = ... » (UTF-8, 63 octets au
 * plus). Relu à chaque connexion. Sans mot de passe (pas de fichier, ou vide) :
 * toute connexion est refusée. Un service en C l'ajoute par son svc.conf
 * (« sources = ../inc/remotepw.c »). rp_read et rp_equal se partagent rp_pw et
 * leur tampon : un thread à la fois (verrou de l'appelant, comme l'asm). */
#include "korinrt.h"
#include "remotepw.h"

#define RP_BUF 1024

char rp_pw[RP_MAX + 1];
static char rp_buf[RP_BUF];

/* -> longueur du mot de passe (0 : aucun), lui dans rp_pw (terminé par 0) (rp_lire) */
long rp_read(void)
{
	rp_pw[0] = 0;
	long fd = sys_open("SD0:/etc/remote.txt", O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, rp_buf, RP_BUF - 1);
	sys_close(fd);
	if (n <= 0)
		return 0;
	rp_buf[n] = 0;
	const char *p = rp_buf;
	for (;;) { /* une ligne « password = ... » */
		if (!*p)
			return 0;
		const char *k = "password", *q = p;
		while (*k && *k == *q) {
			k++;
			q++;
		}
		if (!*k) {
			while (*q == ' ' || *q == 9) /* espaces, « = », espaces */
				q++;
			if (*q == '=') {
				q++;
				while (*q == ' ')
					q++;
				long len = 0; /* la valeur, jusqu'à la fin de ligne */
				while (q[len] && q[len] != 10 && q[len] != 13 && len < RP_MAX) {
					rp_pw[len] = q[len];
					len++;
				}
				rp_pw[len] = 0;
				while (len && rp_pw[len - 1] == ' ') /* (espaces de fin retirées) */
					rp_pw[--len] = 0;
				return len;
			}
		}
		for (;;) { /* ligne suivante */
			char c = *p++;
			if (!c)
				return 0;
			if (c == 10)
				break;
		}
	}
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
