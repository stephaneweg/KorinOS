/* wget - un fichier par HTTP (docs/OUTILS.md ; comme wget.asm) :
 *   wget [-O FICHIER] [-q] [-S] URL
 * URL : http://hote[:port][/chemin] (sans « http:// » : HTTP aussi). Sans -O (ou
 * -O -) : le corps sur la sortie standard ; le bilan va sur la sortie d'erreur.
 * -q : sans bilan, -S : les en-têtes de la réponse sur la sortie d'erreur. Les
 * redirections (301, 302, 303, 307, 308) sont suivies, 5 au plus. Requête
 * HTTP/1.0. HTTPS : refusé (même message que l'asm).
 * Comme l'asm : une option est une seule lettre (« -qS », « - » : usage) ; le
 * nom d'hôte s'arrête à « : », « / » ou « ? » (127 octets au plus) ; un port
 * suivi d'autre chose que « / » donne un chemin « /reste » ; une erreur HTTP
 * met le corps sur la sortie standard seulement sans -O ; « transfert
 * incomplet » est dit même avec -q ; usage : code 2, erreurs : 1. */
#include "korinrt.h"
#include "netkit_import.h"

#define REDIR_MAX 5
#define HOST_MAX 128
#define PATH_MAX 1024
#define BODY 16384

static u64 port;
static i64 length; /* Content-Length, -1 : inconnue */
static char host[HOST_MAX];
static char path[PATH_MAX];
static char location[PATH_MAX];
static char req[PATH_MAX + HOST_MAX + 256];
static u8 body_buf[BODY];
static int flags; /* bit 0 : -q, bit 1 : -S */
static long sd;   /* la connexion de la requête */
static long out_fd;

/* valeur en décimal sur la sortie d'erreur */
static void err_dec(u64 v)
{
	char b[32];
	fmt_dec(v, b);
	err_puts(b);
}

/* destination, source, taille de la destination (0 compris) : au plus n - 1 octets */
static void copy_bounded(char *d, const char *s, u64 n)
{
	if (n > 1)
		for (n--; *s && n; n--)
			*d++ = *s++;
	*d = 0;
}

/* texte, préfixe en minuscules -> après le préfixe, 0 s'il n'y est pas (casse ASCII) */
static const char *prefix_i(const char *s, const char *p)
{
	for (; *p; p++, s++) {
		u8 c = (u8)*s;
		if ((u32)(c - 'A') <= 25u)
			c |= 0x20;
		if ((u8)*p != c)
			return 0;
	}
	return s;
}

/* URL -> hote, port, chemin ; 0, -1 si mauvaise, -2 si https:// */
static int url_parse(const char *u)
{
	if (prefix_i(u, "https://"))
		return -2;
	const char *s = prefix_i(u, "http://");
	if (!s)
		s = u;
	u64 n = 0; /* jusqu'à « : », « / », « ? » ou la fin */
	for (; *s && *s != ':' && *s != '/' && *s != '?'; s++) {
		if (n >= HOST_MAX - 1)
			return -1;
		host[n++] = *s;
	}
	host[n] = 0;
	if (!n)
		return -1;
	u64 p = 80;
	if (*s == ':') {
		s++;
		p = 0;
		for (; (u8)(*s - '0') <= 9; s++)
			p = p * 10 + (u64)(*s - '0');
		if (!p || p > 65535)
			return -1;
	}
	port = p;
	char *d = path; /* le chemin (« / » s'il manque) */
	if (*s != '/')
		*d++ = '/';
	copy_bounded(d, s, PATH_MAX - 1);
	return 0;
}

static int is_redirect(u64 code)
{
	return (code >= 301 && code <= 303) || code == 307 || code == 308;
}

/* texte -> après ses blancs (espaces, tabulations) */
static const char *skip_blanks(const char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	return s;
}

/* la requête à hote:port, chemin ; ligne d'état et en-têtes lus -> code HTTP,
 * sd (ouvert), location, length ; < 0 : erreur réseau */
static long request(void)
{
	location[0] = 0;
	length = -1;
	long r = nt_tcp(host, (long)port, 0);
	if (r < 0)
		return r;
	sd = r;
	char *p = str_cat(req, "GET "); /* GET chemin HTTP/1.0 */
	p = str_cat(p, path);
	p = str_cat(p, " HTTP/1.0\r\nHost: ");
	p = str_cat(p, host);
	if (port != 80) {
		*p++ = ':';
		p = fmt_dec(port, p);
	}
	str_cat(p, "\r\nUser-Agent: Korin-wget/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n");
	r = nt_send_str(sd, req);
	if (r < 0)
		goto close;
	nt_read_start(sd);
	char *l = nt_line(); /* « HTTP/1.x 200 OK » */
	if (!l)
		goto empty;
	if (flags & 2) {
		err_puts(l);
		err_puts("\n");
	}
	for (;;) { /* le code : après la première espace */
		char c = *l++;
		if (!c)
			goto empty;
		if (c == ' ')
			break;
	}
	u64 code = parse_dec(l);
	while ((l = nt_line()) && *l) { /* les en-têtes, jusqu'à la ligne vide */
		if (flags & 2) {
			err_puts(l);
			err_puts("\n");
		}
		const char *v = prefix_i(l, "location:");
		if (v) {
			copy_bounded(location, skip_blanks(v), PATH_MAX);
			continue;
		}
		v = prefix_i(l, "content-length:");
		if (v)
			length = (i64)parse_dec(skip_blanks(v));
	}
	return (long)code;
empty:
	r = E_NT_CONNRESET;
close:
	net_close(sd);
	return r;
}

/* le corps de la réponse (sd) vers out_fd -> octets ; *complete = 1 si la
 * longueur est atteinte, ou inconnue et la connexion fermée */
static u64 body(int *complete)
{
	u64 got = 0;
	*complete = 0;
	if (nt_in.n > nt_in.i) { /* déjà reçus avec les en-têtes */
		u64 k = nt_in.n - nt_in.i;
		got += k;
		if (sys_write(out_fd, nt_in.rx + nt_in.i, k) < 0)
			return got;
	}
	for (;;) {
		if (length >= 0 && got >= (u64)length) {
			*complete = 1;
			return got;
		}
		long n = net_recv(sd, (long)body_buf, BODY, 30000);
		if (n <= 0) /* fermée : complète si la longueur était inconnue */
			break;
		got += (u64)n;
		if (sys_write(out_fd, body_buf, (u64)n) < 0)
			return got;
	}
	*complete = length < 0 || got >= (u64)length;
	return got;
}

static NORETURN void usage(void)
{
	err_say("usage : wget [-O fichier] [-q] [-S] http://hôte[:port]/chemin\n");
	exit(2);
}

static NORETURN void https(void)
{
	err_say("wget : HTTPS non pris en charge (pas de TLS pour les outils en assembleur ; OpenSSL ne sert que les programmes C)\n");
	exit(1);
}

static NORETURN void bad_url(void)
{
	err_say("wget : URL invalide\n");
	exit(1);
}

int main(int argc, char **argv)
{
	const char *file = 0; /* fichier de sortie (0 : la sortie standard) */
	const char *url = 0;
	for (int i = 1; i < argc;) {
		const char *a = argv[i++];
		if (a[0] != '-') {
			if (url)
				usage();
			url = a;
			continue;
		}
		if (!a[1] || a[2])
			usage();
		if (a[1] == 'q')
			flags |= 1;
		else if (a[1] == 'S')
			flags |= 2;
		else if (a[1] == 'O') {
			if (i >= argc)
				usage();
			file = argv[i++];
			if (file[0] == '-' && !file[1]) /* « -O - » : la sortie standard */
				file = 0;
		} else
			usage();
	}
	if (!url)
		usage();
	int r = url_parse(url);
	if (r == -2)
		https();
	if (r < 0)
		bad_url();
	if (!nt_network()) {
		err_say("wget : pas de réseau\n");
		exit(1);
	}
	long code;
	for (int redirs = 0;;) {
		code = request();
		if (code < 0) {
			err_say("wget : connexion impossible à ");
			err_puts(host);
			err_puts(" : ");
			nt_err_text(code);
			err_puts("\n");
			exit(1);
		}
		if (!is_redirect((u64)code) || !location[0])
			break;
		if (++redirs > REDIR_MAX) {
			err_say("wget : trop de redirections\n");
			exit(1);
		}
		net_close(sd);
		if (!(flags & 1)) { /* « -> nouvelle adresse » */
			err_say("wget : redirigé vers ");
			err_puts(location);
			err_puts("\n");
		}
		if (location[0] == '/') { /* même hôte, autre chemin */
			copy_bounded(path, location, PATH_MAX);
			continue;
		}
		r = url_parse(location);
		if (r == -2)
			https();
		if (r < 0)
			bad_url();
	}
	int complete;
	if ((u64)(code - 200) >= 100) { /* une erreur HTTP : le corps sur la sortie standard seulement */
		if (!file) {
			out_fd = 1;
			out_flush();
			body(&complete);
		}
		net_close(sd);
		err_say("wget : erreur HTTP ");
		err_dec((u64)code);
		err_puts("\n");
		exit(1);
	}
	out_fd = 1;
	if (file) {
		long fd = sys_open(file, O_WRITE | O_CREATE | O_TRUNC);
		if (fd < 0) {
			err_file(file);
			net_close(sd);
			exit(1);
		}
		out_fd = fd;
	}
	out_flush();
	u64 got = body(&complete);
	net_close(sd);
	if (out_fd != 1)
		sys_close(out_fd);
	if (!(flags & 1)) {
		err_say("wget : ");
		err_dec(got);
		err_say(" octets reçus");
		if (file) {
			err_puts(" -> ");
			err_puts(file);
		}
		err_puts("\n");
	}
	if (!complete) {
		err_say("wget : transfert incomplet\n");
		exit(1);
	}
	return 0;
}
