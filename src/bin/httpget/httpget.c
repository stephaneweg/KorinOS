/* httpget - client HTTP/1.0 : GET http://<hôte>/ (example.com, sinon un
 * serveur local sur l'hôte QEMU 10.0.2.2:8000), affiche la ligne d'état et les
 * en-têtes. Comme httpget.asm : sans argument (ceux donnés sont ignorés) ;
 * sortie sans tampon ; seuls les en-têtes du premier morceau reçu sont
 * affichés (jusqu'à la ligne vide, sinon tout le morceau) ; le message
 * « essai du serveur local de l'hôte » n'est pas traduit (la clé de
 * lang/en.txt n'a pas l'apostrophe) ; code 0 si une réponse est reçue. */
#include "korinrt.h"
#include "netkit_import.h"

static u32 ni[8];
static char req[512];
static char buf[1040];

/* adresse IPv4 -> sortie standard (le print_ip de NetKit écrit sur la console
 * du noyau, donc dans le journal, pas dans le tube du terminal) */
static void put_ip(u32 ip)
{
	char b[32];
	fmt_ip(ip, (long)b);
	ak_puts(b);
}

/* IP, port, nom d'hôte -> 0 si une réponse est reçue */
static int http_get(u32 ip, long port, const char *host)
{
	lang_puts("httpget : connexion à ");
	put_ip(ip);
	ak_puts(":");
	print_dec(port);
	ak_puts("\n");
	long sd = net_socket(SOCK_TCP);
	long r = sd;
	if (sd < 0)
		goto err;
	r = net_connect(sd, ip, port);
	if (r < 0)
		goto err_close;
	lang_puts("httpget : connecté, envoi de la requête\n");
	char *p = str_cat(req, "GET / HTTP/1.0\r\nHost: ");
	p = str_cat(p, host);
	p = str_cat(p, "\r\nUser-Agent: Korin-httpget/0.1\r\n\r\n");
	r = net_send(sd, (long)req, p - req);
	if (r < 0)
		goto err_close;
	u64 total = 0;
	int shown = 0;
	for (;;) {
		r = net_recv(sd, (long)buf, 1024, 10000);
		if (r <= 0)
			break;
		total += (u64)r;
		if (shown)
			continue;
		shown = 1; /* la tête de la réponse (en-têtes) */
		long i = 0, end = r;
		for (; i < r - 3; i++)
			if (buf[i] == 13 && buf[i + 1] == 10 && buf[i + 2] == 13 && buf[i + 3] == 10) {
				end = i;
				break;
			}
		buf[end] = 0;
		lang_puts("httpget : en-têtes reçus :\n");
		ak_puts(buf);
		ak_puts("\n");
	}
	lang_puts("httpget : total ");
	print_dec((long)total);
	lang_puts(" octets, fin de lecture ");
	print_dec(r);
	ak_puts("\n");
	net_close(sd);
	return total ? 0 : 1;
err_close:
	net_close(sd);
err:
	lang_puts("httpget : erreur ");
	print_dec(r);
	ak_puts("\n");
	return 1;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!wait_dhcp((long)ni)) {
		lang_puts("httpget : pas de réseau\n");
		_exit(1);
	}
	long a = net_resolve((long)"example.com"); /* 1) example.com:80 */
	if (a >= 0 && !http_get((u32)a, 80, "example.com"))
		_exit(0);
	lang_puts("httpget : essai du serveur local de l'hôte\n"); /* 2) serveur local de l'hôte */
	_exit(http_get(0x0A000202, 8000, "10.0.2.2") ? 1 : 0);
}
