/* tcpbench - banc du réseau, sans disque dans la boucle : serveur TCP sur le
 * port 5001, le client est tools/tcpbench.py. Le premier octet d'une
 * connexion choisit (comme tcpbench.asm) :
 *	S  (+ un octet : la durée en secondes) le Pi envoie, puis ferme
 *	R  le Pi reçoit et jette jusqu'à la fermeture
 *	E  le Pi renvoie chaque octet (aller-retour) ; toute autre lettre aussi
 *	I  le Pi envoie les compteurs de netd (NET_OP_STATS, NST_BYTES octets)
 * Sans argument ni fin ; messages sans accents ni traduction (comme l'asm). */
#include "korinrt.h"
#include "netkit_import.h"

#define TB_PORT 5001
#define TB_BUF 65536
#define TB_WAIT 30000 /* ms */
#define NET_OP_STATS 23
#define NST_BYTES 136
#define WM_DATA 48

static u32 ni[8];
static u64 msg[64];
static u8 buf[TB_BUF];

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	wait_dhcp((long)ni);
	long sd = net_socket(SOCK_TCP);
	if (sd < 0 || net_bind(sd, TB_PORT) < 0 || net_listen(sd, 4) < 0) {
		ak_puts("tcpbench : reseau indisponible\n");
		_exit(1);
	}
	long canal = korin_sys2(SYS_SVC_OPEN, "net", 5000);
	ak_puts("tcpbench : en ecoute sur le port 5001\n");
	for (;;) {
		long c = net_accept(sd, 3600000);
		if (c < 0)
			continue;
		if (net_recv(c, (long)buf, 1, TB_WAIT) != 1) /* le choix */
			goto close;
		switch (buf[0]) {
		case 'S': { /* (fin décidée ici : la fermeture du client se voit par un RST,
			     * qui peut se perdre, et la connexion suivante attendrait) */
			if (net_recv(c, (long)buf, 1, TB_WAIT) != 1)
				break;
			u64 end = (u64)buf[0] * 1000 + (u64)sys_time_ms();
			while (net_send(c, (long)buf, TB_BUF) > 0 && (u64)sys_time_ms() < end)
				;
			break;
		}
		case 'R':
			while (net_recv(c, (long)buf, TB_BUF, TB_WAIT) > 0)
				;
			break;
		case 'I':
			msg[0] = NET_OP_STATS;
			korin_sys2(SYS_SVC_CALL, canal, msg);
			net_send(c, (long)msg + WM_DATA, NST_BYTES);
			break;
		default: /* E : l'écho */
			for (;;) {
				long n = net_recv(c, (long)buf, 1024, TB_WAIT);
				if (n <= 0 || net_send(c, (long)buf, n) <= 0)
					break;
			}
		}
	close:
		net_close(c);
	}
}
