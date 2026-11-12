/* httpd - mini serveur HTTP sur le port 80 : répond une page à chaque requête
 * (QEMU : hostfwd=tcp::8080-:80, puis http://127.0.0.1:8080/ depuis l'hôte).
 * Comme httpd.asm : sans argument ni fin ; la page dit encore « Serveur HTTP
 * en assembleur AArch64 » (même texte, mêmes traductions) ; une requête est
 * lue jusqu'à la ligne vide, 1000 octets ou 3 s de silence, et le tampon n'est
 * pas vidé entre deux requêtes (une requête vide rejoue le début de la
 * précédente dans le journal) ; en cas d'erreur au départ, « httpd : erreur »
 * est suivi du résultat de l'écriture du message (0 dans un tube ou un
 * fichier), pas du code d'erreur, et code 1. */
#include "korinrt.h"
#include "netkit_import.h"

static u32 ni[8];
static char buf[1040];
static char page[1024];

/* -> 1 si buf contient « \r\n\r\n » */
static int has_blank_line(void)
{
	for (const char *p = buf; *p; p++)
		if (p[0] == 13 && p[1] == 10 && p[2] == 13 && p[3] == 10)
			return 1;
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	wait_dhcp((long)ni);
	long sd = net_socket(SOCK_TCP);
	if (sd < 0 || net_bind(sd, 80) < 0 || net_listen(sd, 4) < 0) {
		print_dec(lang_puts("httpd : erreur ")); /* (comme l'asm) */
		_exit(1);
	}
	lang_puts("httpd : en écoute sur le port 80\n");
	u64 served = 0;
	for (;;) {
		long c = net_accept(sd, 3600000);
		if (c < 0)
			continue;
		u64 n = 0; /* lire jusqu'à la ligne vide */
		for (;;) {
			long r = net_recv(c, (long)(buf + n), (long)(1000 - n), 3000);
			if (r <= 0)
				break;
			n += (u64)r;
			buf[n] = 0;
			if (has_blank_line() || n >= 1000)
				break;
		}
		served++;
		lang_puts("httpd : requête "); /* journal : première ligne de la requête */
		u64 i = 0;
		while (buf[i] && buf[i] != 13)
			i++;
		buf[i] = 0;
		ak_puts(buf);
		ak_puts("\n");
		char *p = str_cat(page, "HTTP/1.0 200 OK\r\n"
					"Content-Type: text/html; charset=utf-8\r\n"
					"Connection: close\r\n\r\n");
		p = str_cat(p, lang_tr("<html><body><h1>Bonjour depuis Korin OS</h1>"
				       "<p>Serveur HTTP en assembleur AArch64, pile TCP/IP maison.</p>"
				       "<p>Requête numéro "));
		p = fmt_dec(served, p);
		p = str_cat(p, "</p></body></html>\r\n");
		net_send(c, (long)page, p - page);
		net_close(c);
	}
}
