/* ping - attend l'adresse DHCP, puis (comme ping.asm) :
 *   ping HOTE [N]	N pings (4 par défaut) vers un nom ou une adresse, et le bilan
 *   ping -t HOTE	jusqu'à Ctrl+C
 *   ping		l'essai du réseau : la passerelle, puis un nom résolu par DNS
 * Comme l'asm : seul « -t » est une option (tout autre mot est l'hôte, puis le
 * nombre) ; un nombre nul ou illisible est ignoré ; sortie sans tampon (puts,
 * print_dec) ; code 0 si une réponse au moins, 1 sinon (et sans réseau), 2 si
 * l'hôte est inconnu. */
#include "korinrt.h"
#include "netkit_import.h"

static u32 ni[8];

/* adresse IPv4 -> sortie standard (le print_ip de NetKit écrit sur la console
 * du noyau, donc dans le journal, pas dans le tube du terminal) */
static void put_ip(u32 ip)
{
	char b[32];
	fmt_ip(ip, (long)b);
	ak_puts(b);
}

/* adresse, nombre de pings (0 : sans fin) -> réponses reçues */
static u64 ping_n(u32 ip, u64 n)
{
	u64 got = 0;
	for (;;) {
		lang_puts("ping : ");
		put_ip(ip);
		lang_puts(" : ");
		long r = net_ping(ip, 1000);
		if (r >= 0) {
			got++;
			print_dec(r);
			ak_puts(" ms\n");
		} else {
			print_dec(r);
			lang_puts(" (erreur)\n");
		}
		sys_sleep(300);
		if (n && !--n)
			break;
	}
	return got;
}

int main(int argc, char **argv)
{
	(void)argc;
	const char *host = 0;
	u64 count = 4;
	int forever = 0;
	for (int i = 1; argv[i]; i++) {
		const char *a = argv[i];
		if (a[0] == '-' && a[1] == 't' && !a[2]) {
			forever = 1;
			continue;
		}
		if (!host) {
			host = a;
			continue;
		}
		u64 v = parse_dec(a);
		if (v)
			count = v;
	}
	if (forever)
		count = 0;
	if (!wait_dhcp((long)ni)) {
		lang_puts("ping : pas de réseau\n");
		_exit(1);
	}
	if (!host) {
		lang_puts("ping : adresse ");
		put_ip(ni[NI_IP / 4]);
		lang_puts("  passerelle ");
		put_ip(ni[NI_GW / 4]);
		ak_puts("\n");
		ping_n(ni[NI_GW / 4], 4); /* la passerelle */
		lang_puts("ping : résolution DNS "); /* un nom */
		ak_puts("example.com");
		ak_puts(" -> ");
		long a = net_resolve((long)"example.com");
		if (a < 0) {
			print_dec(a);
			ak_puts("\n");
			_exit(2);
		}
		put_ip((u32)a);
		ak_puts("\n");
		ping_n((u32)a, 3);
		_exit(0);
	}
	long a = net_resolve((long)host);
	if (a < 0) {
		lang_puts("ping : hôte inconnu : ");
		ak_puts(host);
		ak_puts("\n");
		_exit(2);
	}
	u64 got = ping_n((u32)a, count);
	lang_puts("ping : ");
	print_dec((long)got);
	lang_puts(" sur ");
	print_dec((long)count);
	lang_puts(" reçus\n");
	_exit(got == 0);
}
