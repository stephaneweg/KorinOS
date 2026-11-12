/* ultest - envoie 300 000 octets (motif (i*7)&255) à 10.0.2.2:9000 puis ferme
 * (comme ultest.asm : sans argument ; sortie sans tampon ; un échec affiche
 * le dernier résultat, 0 si le DHCP n'a rien donné, et code 1) */
#include "korinrt.h"
#include "netkit_import.h"

#define TOTAL 300000

static u32 ni[8];
static u8 buf[4000];

static NORETURN void fail(long r)
{
	print_dec(r);
	ak_puts(" : ultest echec\n");
	_exit(1);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long r = wait_dhcp((long)ni);
	if (!r)
		fail(r);
	long sd = net_socket(SOCK_TCP);
	if (sd < 0)
		fail(sd);
	r = net_connect(sd, 0x0A000202, 9000);
	if (r < 0)
		fail(r);
	long t0 = sys_time_ms();
	u64 sent = 0;
	while (sent < TOTAL) {
		for (u64 i = 0; i < 4000; i++) /* bloc de 4000 octets */
			buf[i] = (u8)((sent + i) * 7);
		u64 n = TOTAL - sent;
		r = net_send(sd, (long)buf, n > 4000 ? 4000 : (long)n);
		if (r <= 0)
			fail(r);
		sent += (u64)r;
	}
	net_close(sd);
	long ms = sys_time_ms() - t0;
	ak_puts("ultest : envoyé ");
	print_dec((long)sent);
	ak_puts(" octets en ");
	print_dec(ms);
	ak_puts(" ms (mise en file)\n");
	_exit(0);
}
