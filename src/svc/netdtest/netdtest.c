/* netdtest - essai de la boucle locale de netd (127.0.0.1), par NetKit, sans
 * interface réseau ni serveur extérieur : marche sur le Pi comme sur le PC (où
 * netd tourne sans pilote). Sortie sur la sortie standard.
 *
 *	netdtest [tours]	(20 par défaut)
 *
 *  * ping 127.0.0.1, nom « localhost » ;
 *  * TCP : un serveur (second thread) sur le port 7000 renvoie ce qu'il reçoit ;
 *    à chaque tour, le client se connecte, envoie un message (petit, puis
 *    100 Ko un tour sur quatre), relit l'écho octet pour octet, ferme ; le
 *    serveur lit la fin et ferme ;
 *  * UDP : un datagramme de 127.0.0.1:7002 vers 7001 ;
 *  * à la fin, les sockets de netd (aucun ne doit rester à ce process). */
#include "korinrt.h"
#include "netkit_import.h"

#define PORT 7000
#define BIG 100000
#define WAIT 10000

static u8 srv_buf[16384];
static u8 cli_tx[BIG], cli_rx[BIG];
static u8 stack[65536] __attribute__((aligned(16)));
static volatile long srv_rounds, srv_err, srv_ready;

/* (thread) le serveur d'écho : une connexion après l'autre */
static void server(long ls)
{
	for (;;) {
		long c = net_accept(ls, 25000);
		if (c == E_AGAIN)
			continue;
		if (c < 0) {
			srv_err = c;
			continue;
		}
		for (;;) {
			long n = net_recv(c, (long)srv_buf, sizeof srv_buf, WAIT);
			if (n <= 0) {
				if (n < 0)
					srv_err = n;
				break;
			}
			for (long k = 0; k < n;) {
				long s = net_send(c, (long)srv_buf + k, n - k);
				if (s <= 0) {
					srv_err = s;
					break;
				}
				k += s;
			}
		}
		net_close(c);
		srv_rounds++;
	}
}

/* un tour : -> 0 ou un code d'erreur (et l'étape dans *step) */
static long round_trip(u64 len, u64 r, const char **step)
{
	for (u64 i = 0; i < len; i++)
		cli_tx[i] = (u8)(i * 7 + r);
	*step = "socket";
	long sd = net_socket(SOCK_TCP);
	if (sd < 0)
		return sd;
	*step = "connect";
	long e = net_connect(sd, 0x7F000001, PORT);
	if (e < 0)
		goto out;
	*step = "send";
	for (u64 k = 0; k < len;) {
		e = net_send(sd, (long)cli_tx + (long)k, (long)(len - k));
		if (e <= 0) {
			e = e ? e : -1;
			goto out;
		}
		k += (u64)e;
	}
	*step = "recv";
	for (u64 k = 0; k < len;) {
		e = net_recv(sd, (long)cli_rx + (long)k, (long)(len - k), WAIT);
		if (e <= 0) {
			e = e ? e : -2; /* -2 : fermée trop tôt */
			goto out;
		}
		k += (u64)e;
	}
	*step = "compare";
	e = memcmp(cli_tx, cli_rx, len) ? -3 : 0;
out:
	net_close(sd);
	return e;
}

int main(int argc, char **argv)
{
	long rounds = argc > 1 ? atoi(argv[1]) : 20;
	long fails = 0;
	long rtt = net_ping(0x7F000001, 2000);
	printf("ping 127.0.0.1 : %s\n", rtt >= 0 ? "ok" : "erreur");
	long a = net_resolve((long)"localhost");
	printf("localhost : %s\n", a == 0x7F000001 ? "127.0.0.1" : "erreur");
	if (rtt < 0 || a != 0x7F000001)
		fails++;
	long ls = net_socket(SOCK_TCP);
	if (ls < 0 || net_bind(ls, PORT) < 0 || net_listen(ls, 4) < 0) {
		printf("serveur : erreur\n");
		return 1;
	}
	if (korin_sys3(SYS_THREAD_CREATE, server, stack + sizeof stack, ls) < 0) {
		printf("thread : erreur\n");
		return 1;
	}
	for (long r = 0; r < rounds; r++) {
		u64 len = r % 4 == 3 ? BIG : 16 + (u64)r;
		const char *step;
		u64 t0 = (u64)sys_time_ms();
		long e = round_trip(len, (u64)r, &step);
		u64 dt = (u64)sys_time_ms() - t0;
		if (e) {
			fails++;
			printf("tour %ld (%lu o) : erreur %ld à l'étape %s (%lu ms)\n", r, len, e, step, dt);
		} else {
			printf("tour %ld (%lu o) : ok%s\n", r, len, dt > 2000 ? " (lent)" : "");
		}
	}
	u64 end = (u64)sys_time_ms() + 5000;
	while (srv_rounds < rounds && (u64)sys_time_ms() < end)
		sys_sleep(10);
	printf("serveur : %ld connexions, dernière erreur %ld\n", srv_rounds, srv_err);
	/* UDP */
	long u1 = net_socket(SOCK_UDP), u2 = net_socket(SOCK_UDP);
	long ub = u1 >= 0 && u2 >= 0 ? net_bind(u1, 7001) : -1;
	if (ub >= 0)
		ub = net_bind(u2, 7002);
	long us = ub >= 0 ? net_sendto(u2, (long)"datagramme", 10, 0x7F000001, 7001) : -1;
	u32 from[2] = {0, 0};
	char ubuf[32];
	long ur = us == 10 ? net_recvfrom(u1, (long)ubuf, sizeof ubuf, 3000, (long)from) : -1;
	int uok = ur == 10 && !memcmp(ubuf, "datagramme", 10) && from[0] == 0x7F000001 && from[1] == 7002;
	printf("UDP : %s\n", uok ? "ok" : "erreur");
	if (!uok)
		fails++;
	net_close(u1);
	net_close(u2);
	net_close(ls);
	sys_sleep(300);
	printf("bilan : %ld tours, %ld erreurs\n", rounds, fails);
	return fails ? 1 : 0;
}
