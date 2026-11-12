/* whois - la base WHOIS (TCP, port 43) (docs/OUTILS.md ; comme whois.asm) :
 *   whois [-h SERVEUR] [-p PORT] DOMAINE [SERVEUR]
 * Sans serveur : whois.iana.org d'abord, puis le serveur de son « refer: » (ou
 * « whois: »). -p : un autre port que 43 (essais).
 * Comme l'asm : une option est « -x » suivi d'une valeur (sinon usage, code 2 ;
 * « - » seul regarde l'octet qui suit sa fin, comme l'asm) ; -p 0 ou illisible :
 * usage ; la réponse (64 Ko au plus) s'arrête après 10 s de silence ; elle est
 * affichée sans les CR, suivie d'une fin de ligne ; un serveur injoignable :
 * code 1. */
#include "korinrt.h"
#include "netkit_import.h"

#define REP_MAX 65536

static char server[96];
static char rep[REP_MAX];
static const char *domain;
static long port = 43;

/* « [serveur] » */
static void title(const char *s)
{
	out_putc('[');
	out_puts(s);
	out_putc(']');
	out_nl();
	out_flush();
}

/* serveur : « domaine CR LF » au port, toute la réponse dans rep -> octets, < 0 */
static long query(const char *s)
{
	long sd = nt_tcp(s, port, 0);
	if (sd < 0)
		return sd;
	nt_send_str(sd, domain);
	nt_send_str(sd, "\r\n");
	u64 n = 0;
	while (n < REP_MAX - 1) {
		long r = net_recv(sd, (long)(rep + n), (long)(REP_MAX - 1 - n), 10000); /* 10 s de silence : la fin */
		if (r <= 0)
			break;
		n += (u64)r;
	}
	net_close(sd);
	rep[n] = 0;
	return (long)n;
}

/* ligne, préfixe -> après lui, 0 s'il n'y est pas */
static const char *prefix(const char *l, const char *p)
{
	for (; *p; p++, l++)
		if (*l != *p)
			return 0;
	return l;
}

/* la ligne « refer: » ou « whois: » de la réponse -> server, 1 si trouvée */
static int find_refer(void)
{
	const char *l = rep;
	while (*l) {
		const char *v = prefix(l, "refer:");
		if (!v)
			v = prefix(l, "whois:");
		if (v) {
			while (*v == ' ' || *v == 9)
				v++;
			u64 n = 0;
			while ((u8)v[n] > ' ') { /* (fin de ligne, CR, blanc, 0) */
				server[n] = v[n];
				if (++n >= 95)
					break;
			}
			server[n] = 0;
			if (n)
				return 1;
		}
		for (;;) { /* ligne suivante */
			char c = *l++;
			if (!c)
				return 0;
			if (c == 10)
				break;
		}
	}
	return 0;
}

/* la réponse sur la sortie, sans les CR */
static void show(void)
{
	for (const char *p = rep; *p; p++)
		if (*p != 13)
			out_putc((u8)*p);
	out_nl();
}

static NORETURN void unreachable(const char *s, long e)
{
	err_say("whois : injoignable : ");
	err_puts(s);
	err_puts(" : ");
	nt_err_text(e);
	err_puts("\n");
	exit(1);
}

int main(int argc, char **argv)
{
	const char *srv = 0;
	for (int i = 1; i < argc;) {
		const char *a = argv[i++];
		if (a[0] != '-') {
			if (!domain)
				domain = a;
			else if (!srv)
				srv = a;
			else
				goto usage;
			continue;
		}
		if (a[2]) /* (« - » seul : l'octet après sa fin, comme l'asm) */
			goto usage;
		if (i >= argc) /* une valeur après l'option */
			goto usage;
		const char *v = argv[i++];
		if (a[1] == 'h')
			srv = v;
		else if (a[1] == 'p') {
			port = (long)parse_dec(v);
			if (!port)
				goto usage;
		} else
			goto usage;
	}
	if (!domain)
		goto usage;
	if (!nt_network()) {
		err_say("whois : pas de réseau\n");
		return 1;
	}
	if (!srv) { /* 1) l'IANA */
		srv = "whois.iana.org";
		title(srv);
		long r = query(srv);
		if (r < 0)
			unreachable(srv, r);
		if (!find_refer()) { /* l'IANA tient la réponse */
			show();
			return 0;
		}
		srv = server; /* 2) le registre */
	}
	title(srv);
	long r = query(srv);
	if (r < 0)
		unreachable(srv, r);
	show();
	return 0;
usage:
	err_say("usage : whois [-h serveur] [-p port] domaine [serveur]\n");
	return 2;
}
