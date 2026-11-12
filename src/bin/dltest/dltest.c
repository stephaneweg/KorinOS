/* dltest - télécharge http://10.0.2.2:8000/big.bin (serveur de l'hôte QEMU),
 * compte les octets du corps et leur somme, mesure le débit (comme
 * dltest.asm : sans argument ; sortie sans tampon ; le résultat de l'envoi de
 * la requête n'est pas regardé ; un échec affiche le dernier résultat, 0 si le
 * DHCP n'a rien donné, et code 1) */
#include "korinrt.h"
#include "netkit_import.h"

static u32 ni[8];
static u8 buf[4100];
static const char req[] = "GET /big.bin HTTP/1.0\r\nHost: 10.0.2.2\r\n\r\n";

static NORETURN void fail(long r)
{
	print_dec(r);
	ak_puts(" : dltest echec\n");
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
	r = net_connect(sd, 0x0A000202, 8000);
	if (r < 0)
		fail(r);
	long t0 = sys_time_ms();
	net_send(sd, (long)req, sizeof req - 1);
	u64 bytes = 0, sum = 0; /* octets du corps, somme */
	int body = 0;           /* 1 : en-têtes passés */
	u32 last4 = 0;          /* 4 derniers octets (détection de \r\n\r\n) */
	for (;;) {
		long n = net_recv(sd, (long)buf, 4096, 10000);
		if (n <= 0)
			break;
		for (long i = 0; i < n; i++) {
			if (body) {
				bytes++;
				sum += buf[i];
			} else {
				last4 = last4 << 8 | buf[i];
				body = last4 == 0x0D0A0D0A;
			}
		}
	}
	long ms = sys_time_ms() - t0;
	net_close(sd);
	ak_puts("dltest : corps ");
	print_dec((long)bytes);
	ak_puts(" octets, somme ");
	print_dec((long)sum);
	ak_puts(", ");
	print_dec(ms);
	ak_puts(" ms\n");
	_exit(0);
}
