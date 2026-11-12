/* net.c : ce que partagent les commandes réseau de /bin (docs/BIN-C.md),
 * comme src/inc/nettool.inc : le réseau prêt, une connexion TCP à un nom,
 * l'envoi d'une chaîne, une réponse lue par lignes, une adresse et une
 * erreur en texte. Au-dessus de NetKit : seules les commandes qui importent
 * netkit (bin.conf « import = netkit ») s'en servent (l'objet n'est lié que
 * si l'une de ces fonctions sert). */
#include "korinrt.h"

/* NetKit (src/kits/netkit/netkit.abi) : mêmes prototypes que netkit_import.h */
long fmt_ip(long a0, long a1);
long wait_dhcp(long a0);
long net_socket(long a0);
long net_connect(long a0, long a1, long a2);
long net_send(long a0, long a1, long a2);
long net_recv(long a0, long a1, long a2, long a3);
long net_close(long a0);
long net_resolve(long a0);
long net_info(long a0);

struct nt_reader nt_in;
char nt_line_buf[NT_LINE];

/* -> 1 si le réseau a une adresse (sinon attend le DHCP, 20 s au plus) (nt_reseau) */
long nt_network(void)
{
	u32 ni[12];
	net_info((long)ni);
	if (ni[NI_IP / 4])
		return 1;
	return wait_dhcp((long)ni);
}

/* nom ou « a.b.c.d », port -> socket TCP connecté, < 0 si erreur ; *ip = l'adresse (nt_tcp) */
long nt_tcp(const char *host, long port, u32 *ip)
{
	long a = net_resolve((long)host);
	if (a < 0)
		return a;
	long sd = nt_tcp_ip((u32)a, port);
	if (sd >= 0 && ip)
		*ip = (u32)a;
	return sd;
}

/* adresse IPv4, port -> socket TCP connecté, < 0 si erreur (nt_tcp_ip) */
long nt_tcp_ip(u32 ip, long port)
{
	long sd = net_socket(SOCK_TCP);
	if (sd < 0)
		return sd;
	long r = net_connect(sd, (long)ip, port);
	if (r < 0) {
		net_close(sd);
		return r;
	}
	return sd;
}

/* socket, chaîne -> octets envoyés, < 0 si erreur (nt_envoyer) */
long nt_send_str(long sd, const char *s)
{
	return net_send(sd, (long)s, str_len(s));
}

/* une nouvelle lecture par lignes sur ce socket (nt_lire_debut) */
void nt_read_start(long sd)
{
	nt_in.sd = sd;
	nt_in.n = 0;
	nt_in.i = 0;
}

/* -> octet suivant, -1 à la fin (fermée, erreur ou 30 s sans rien) (nt_getc) */
int nt_getc(void)
{
	if (nt_in.i >= nt_in.n) {
		long n = net_recv(nt_in.sd, (long)nt_in.rx, NT_RX, 30000);
		if (n <= 0)
			return -1;
		nt_in.n = (u64)n;
		nt_in.i = 0;
	}
	return nt_in.rx[nt_in.i++];
}

/* -> la ligne (sans CR ni LF, dans nt_line_buf), 0 à la fin de la connexion ;
 * au-delà de NT_LINE - 2 octets le reste est perdu ; une dernière ligne sans
 * fin compte (nt_ligne) */
char *nt_line(void)
{
	u64 n = 0;
	for (;;) {
		int c = nt_getc();
		if (c < 0) {
			if (!n)
				return 0;
			break;
		}
		if (c == 10)
			break;
		if (c == 13)
			continue;
		if (n < NT_LINE - 2)
			nt_line_buf[n++] = (char)c;
	}
	nt_line_buf[n] = 0;
	return nt_line_buf;
}

/* texte (brut), largeur : le texte, puis des espaces jusqu'à la largeur, une au moins (t_col) */
void out_col(const char *s, u64 w)
{
	out_puts(s);
	u64 n = (u64)str_len(s);
	do {
		out_putc(' ');
		n++;
	} while (n < w);
}

/* adresse IPv4 sur la sortie tamponnée (nt_put_ip) */
void nt_put_ip(u32 ip)
{
	char b[32];
	fmt_ip((long)ip, (long)b);
	out_puts(b);
}

/* code d'erreur (< 0) : son texte, traduit, sur l'erreur standard, sans fin de ligne (nt_err_texte) */
void nt_err_text(long e)
{
	const char *s;
	switch (e) {
	case E_NOENT: /* le DNS ne connaît pas ce nom */
		s = "nom inconnu";
		break;
	case E_NT_CONNREFUSED:
		s = "connexion refusée";
		break;
	case E_NT_HOSTUNREACH:
		s = "hôte injoignable";
		break;
	case E_NT_TIMEDOUT:
		s = "pas de réponse (délai dépassé)";
		break;
	case E_NT_CONNRESET:
		s = "connexion coupée";
		break;
	default: {
		char b[32];
		err_say("erreur ");
		fmt_dec((u64)-e, b);
		err_puts("-"); /* (le code, négatif) */
		err_puts(b);
		return;
	}
	}
	err_say(s);
}
