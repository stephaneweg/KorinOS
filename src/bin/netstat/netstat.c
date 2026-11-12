/* netstat - les interfaces, la configuration IP et les sockets ouverts de tout
 * le système (docs/OUTILS.md ; comme netstat.asm) :
 *   netstat [-t] [-u] [-l] [-i]
 *   -t : TCP seulement, -u : UDP seulement, -l : les sockets en écoute
 *   seulement, -i : les interfaces et la configuration seulement
 * Comme l'asm : les lettres s'assemblent (« -tl ») ; -n et -a sont acceptées
 * sans effet ; « - » seul est accepté ; un mot sans « - » ou une autre lettre :
 * usage, code 2 ; -l l'emporte sur -i (pas de configuration). */
#include "korinrt.h"
#include "netkit_import.h"

#define SK_MAX 128 /* sockets lus au plus */
#define IF_MAXE 8

/* src/inc/netd.inc */
#define NIF_ETHERNET 1
#define NIF_WIFI 2
#define NIF_USB 3
#define NIF_E_NAME 0
#define NIF_E_TYPE 8
#define NIF_E_LINK 12
#define NIF_E_SPEED 24
#define NIF_E_ACTIVE 28
#define NIF_E_SSID 32
#define NIF_ENT 64
#define NSK_TYPE 0
#define NSK_STATE 4
#define NSK_LPORT 8
#define NSK_RPORT 12
#define NSK_RIP 16
#define NSK_OWNER 20
#define NSK_RXQ 24
#define NSK_TXQ 28
#define NSK_ENT 32

static u8 ifs[IF_MAXE * NIF_ENT];
static u8 socks[SK_MAX * NSK_ENT];
static int opts; /* bit 0 -t, 1 -u, 2 -l, 3 -i */

static const char *const states[] = {
	"CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD", "ESTABLISHED", "FIN_WAIT1",
	"FIN_WAIT2", "CLOSE_WAIT", "CLOSING", "LAST_ACK", "TIME_WAIT",
};

static u32 rd32(const u8 *p)
{
	return *(const u32 *)p;
}

/* libellé : traduit, colonne de 12 */
static void say12(const char *s)
{
	out_col(lang_tr(s), 12);
}

/* libellé, adresse IPv4 (0 : aucune) : une ligne */
static void ip_line(const char *s, u32 ip)
{
	say12(s);
	if (ip)
		nt_put_ip(ip);
	else
		out_say("(aucune)");
	out_nl();
}

/* octet : deux chiffres hexadécimaux */
static void hex2(u8 v)
{
	static const char h[] = "0123456789abcdef";
	out_putc(h[v >> 4]);
	out_putc(h[v & 15]);
}

/* ---- interfaces et configuration */
static void config(void)
{
	long n = net_ifaces((long)ifs, IF_MAXE);
	if (n <= 0)
		out_say("(aucune interface : netd ne répond pas)\n");
	else
		for (const u8 *e = ifs; n; n--, e += NIF_ENT) {
			char name[9]; /* nom (8 octets, pas toujours terminé) */
			memcpy(name, e + NIF_E_NAME, 8);
			name[8] = 0;
			out_col(name, 8);
			u32 t = rd32(e + NIF_E_TYPE);
			out_col(t == NIF_ETHERNET ? "ethernet" : t == NIF_WIFI ? "wifi" : t == NIF_USB ? "usb" : "?", 10);
			out_say(rd32(e + NIF_E_LINK) ? "lien" : "pas de lien");
			if (rd32(e + NIF_E_LINK) && rd32(e + NIF_E_SPEED)) {
				out_puts(", ");
				out_putn(rd32(e + NIF_E_SPEED));
				out_puts(" Mb/s");
			}
			if (rd32(e + NIF_E_ACTIVE))
				out_say(", active");
			if (e[NIF_E_SSID]) {
				out_puts(", « ");
				ifs[e - ifs + NIF_E_SSID + 31] = 0;
				out_puts((const char *)e + NIF_E_SSID);
				out_puts(" »");
			}
			out_nl();
		}
	u32 ni[12]; /* netinfo */
	net_info((long)ni);
	out_nl();
	ip_line("Adresse", ni[NI_IP / 4]);
	ip_line("Masque", ni[NI_MASK / 4]);
	ip_line("Passerelle", ni[NI_GW / 4]);
	ip_line("DNS", ni[NI_DNS / 4]);
	say12("MAC");
	const u8 *mac = (const u8 *)ni + NI_MAC;
	for (int i = 0;;) {
		hex2(mac[i]);
		if (++i >= 6)
			break;
		out_putc(':');
	}
	out_nl();
	say12("DHCP");
	out_say(ni[NI_DHCP / 4] == 4 ? "bail obtenu" : ni[NI_UP / 4] ? "en attente" : "pas de lien");
	out_nl();
}

/* ---- sockets */
static void sockets(void)
{
	char t[32];
	u8 info[64]; /* SYS_PROC_INFO : nom du programme à +24 */
	u64 shown = 0;
	long n = net_sockets((long)socks, SK_MAX);
	for (const u8 *e = socks; n > 0; n--, e += NSK_ENT) {
		u32 type = rd32(e + NSK_TYPE);
		if (type == SOCK_TCP ? !(opts & 1) : !(opts & 2)) /* filtres */
			continue;
		if (opts & 4) { /* -l : en écoute (TCP), UDP sans pair */
			if (type == SOCK_TCP ? rd32(e + NSK_STATE) != 1 : rd32(e + NSK_RIP) != 0)
				continue;
		}
		if (!shown)
			out_say("Proto État        Local   Distant               Reçus   Émis    Programme\n");
		shown++;
		out_col(type == SOCK_TCP ? "tcp" : "udp", 6);
		const char *st = "-"; /* état */
		if (type == SOCK_TCP && rd32(e + NSK_STATE) <= 10)
			st = states[rd32(e + NSK_STATE)];
		out_col(st, 12);
		fmt_dec(rd32(e + NSK_LPORT), t); /* port local */
		out_col(t, 8);
		if (rd32(e + NSK_RIP)) { /* adresse distante */
			char *p = (char *)fmt_ip(rd32(e + NSK_RIP), (long)t);
			*p++ = ':';
			fmt_dec(rd32(e + NSK_RPORT), p);
			out_col(t, 22);
		} else
			out_col("*", 22);
		fmt_dec(rd32(e + NSK_RXQ), t); /* files */
		out_col(t, 8);
		fmt_dec(rd32(e + NSK_TXQ), t);
		out_col(t, 8);
		u32 owner = rd32(e + NSK_OWNER); /* programme */
		if (owner) {
			fmt_dec(owner, t);
			out_puts(t);
			if (ksys(SYS_PROC_INFO, owner, (long)info, 0, 0, 0, 0) >= 0) {
				out_putc(' ');
				info[24 + 31] = 0;
				out_puts((const char *)info + 24);
			}
		} else
			out_puts("- (netd)");
		out_nl();
	}
	if (!shown)
		out_say("(aucun socket ouvert)\n");
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] != '-')
			goto usage;
		for (a++; *a; a++) {
			switch (*a) {
			case 't': opts |= 1; break;
			case 'u': opts |= 2; break;
			case 'l': opts |= 4; break;
			case 'i': opts |= 8; break;
			case 'n': /* (-n, -a : sans effet, tout est numérique) */
			case 'a': break;
			default: goto usage;
			}
		}
	}
	if (!(opts & 3)) /* ni -t ni -u : les deux */
		opts |= 3;
	if (!(opts & 4)) { /* -l : pas la configuration */
		config();
		if (opts & 8)
			return 0;
		out_nl();
	}
	sockets();
	return 0;
usage:
	err_say("usage : netstat [-t] [-u] [-l] [-i]\n");
	return 2;
}
