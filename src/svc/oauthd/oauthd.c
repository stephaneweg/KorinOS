/* oauthd - les comptes OAuth 2.0 du système (docs/OAUTH.md) : un programme demande un jeton d'accès
 * par les fonctions oauth_* de NetKit, qui envoient leurs requêtes à ce service (« oauth »). Il garde la
 * configuration des comptes, les jetons de rafraîchissement (chiffrés) et les jetons d'accès (en
 * mémoire seulement), et fait les requêtes HTTPS aux serveurs d'autorisation :
 *
 *   - code d'appareil (RFC 8628) : un code à taper sur un téléphone ou un PC ;
 *   - navigateur (code d'autorisation, RFC 6749 4.1, avec PKCE S256, RFC 7636) : retour sur
 *     http://127.0.0.1:<port>/, reçu ici, ou adresse de retour collée à la main ;
 *   - rafraîchissement (RFC 6749 6).
 *
 * Fichiers (dossier personnel, comme Mail) :
 *   HOME:/oauth/accounts.ini   une section par compte : provider, client_id, client_secret, scope...
 *   HOME:/oauth/secrets        les jetons de rafraîchissement, AES-256-GCM (EVP d'OpenSSL)
 *   HOME:/oauth/key            la clé (32 octets, getentropy), faite une fois
 *
 * Lancé par NetKit au premier appel (f_spawn de /sys/svc/oauthd), il reste ensuite. Un seul thread :
 * une requête à la fois (une requête HTTPS bloque les autres programmes jusqu'à 20 s au plus). */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <armpi/syscall.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

typedef int64_t i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

/* ---- Korin : appels système, message (src/inc/syscalls.inc) */
#define SYS_SVC_REGISTER 0x30
#define SYS_SVC_NEXT 0x33
#define SYS_SVC_REPLY 0x34
#define SYS_SVC_MAP_CALLER 0x36
#define SVF_ENDS 1
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_FORK 0xFFFC
#define MC_WRITE 1

/* ---- le protocole (le même dans src/kits/netkit/netkit.c) : nom du compte dans les données, c = adresse
 * d'un tampon de l'appelant, d = sa taille (entrée ou sortie selon l'opération) */
#define OA_SETUP 1	   /* c, d : la configuration (texte « clé = valeur ») -> 0 */
#define OA_STATUS 2	   /* -> 2 jeton d'accès valable, 1 connecté, 0 configuré sans jeton */
#define OA_DEVICE_BEGIN 3  /* c, d : sortie « code\nurl\nurl complète\nexpire\nintervalle\n » -> octets */
#define OA_DEVICE_POLL 4   /* -> 0 connecté, E_AGAIN pas encore */
#define OA_BROWSER_BEGIN 5 /* a = port (0 : choisi), c, d : sortie, l'adresse à ouvrir -> octets */
#define OA_BROWSER_POLL 6  /* -> 0 connecté, E_AGAIN pas encore */
#define OA_BROWSER_FINISH 7 /* c, d : l'adresse de retour (ou le code seul) -> 0 */
#define OA_TOKEN 8	   /* c, d : sortie, le jeton d'accès -> octets */
#define OA_FORGET 9	   /* -> 0 : jetons oubliés, configuration gardée */
#define OA_ERROR 10	   /* (pas de nom) c, d : sortie, la dernière erreur de l'appelant -> octets */
#define OA_LIST 11	   /* (pas de nom) c, d : sortie, « nom\tétat\n » par compte -> octets */
#define OA_REMOVE 12	   /* -> 0 : compte retiré (configuration et jetons) */
#define OA_INFO 13	   /* c, d : sortie, la configuration (sans le secret) -> octets */

#define DATA_MAX 464
#define USER_MAX 65536

/* erreurs de Korin : numéros de Linux */
#define E_NOENT -2
#define E_IO -5
#define E_AGAIN -11
#define E_NOMEM -12
#define E_ACCES -13
#define E_EXIST -17
#define E_INVAL -22
#define E_NOSPC -28
#define E_RANGE -34
#define E_NAMETOOLONG -36
#define E_TIMEDOUT -110
#define E_CONNREFUSED -111

struct msg {
	i64 op;
	i64 a, b, c, d;
	u32 len;
	u32 pid;
	u8 data[DATA_MAX];
};
_Static_assert(sizeof(struct msg) == 512, "message de 512 octets");

static long ksys(long n, long a, long b, long c, long d)
{
	return armpi_syscall(n, a, b, c, d, 0, 0);
}

#define DIR_PATH "HOME:/oauth"
#define ACCOUNTS_INI DIR_PATH "/accounts.ini"
#define SECRETS_FILE DIR_PATH "/secrets"
#define KEY_FILE DIR_PATH "/key"
#define SECRETS_MAGIC "KORINOA1"

#define NAME_MAX_ 64
#define TEXT 512
#define ACC_MAX 64
#define ERR_MAX 32
#define HTTP_TIMEOUT 20000
#define BODY_MAX 65536

/* ---- état */

struct account {
	int used;
	char name[NAME_MAX_];
	/* la configuration (accounts.ini) */
	char provider[32], client_id[TEXT], client_secret[TEXT], scope[TEXT];
	char auth_url[TEXT], token_url[TEXT], device_url[TEXT], auth_params[TEXT], tenant[64];
	/* les jetons */
	char *refresh;		/* (secrets) */
	char *access;		/* (en mémoire) */
	time_t access_until;
	/* code d'appareil en cours */
	char device_code[TEXT];
	time_t device_until, device_next;
	int device_interval;
	/* navigateur en cours */
	char verifier[96], state[48], redirect[64];
	int listen_fd;
	time_t browser_until;
};

static struct account accs[ACC_MAX];
static struct {
	u32 pid;
	char text[256];
} errs[ERR_MAX];
static struct msg msg __attribute__((aligned(16)));
static long canal, req_cour;
static u32 pid_cour;
static int loaded;
static SSL_CTX *ssl_ctx;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("oauthd: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* la dernière erreur de l'appelant (oauth_error) ; rend r */
static long fail(long r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static long fail(long r, const char *fmt, ...)
{
	int k = -1;
	for (int i = 0; i < ERR_MAX; i++)
		if (errs[i].pid == pid_cour) {
			k = i;
			break;
		}
	if (k < 0)
		for (int i = 0; i < ERR_MAX; i++)
			if (!errs[i].pid) {
				k = i;
				break;
			}
	if (k < 0)
		k = (int)(pid_cour % ERR_MAX);
	errs[k].pid = pid_cour;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errs[k].text, sizeof errs[k].text, fmt, ap);
	va_end(ap);
	return r;
}

static void scpy(char *d, const char *s, size_t n)
{
	if (!n)
		return;
	size_t i = 0;
	for (; s && s[i] && i + 1 < n; i++)
		d[i] = s[i];
	d[i] = 0;
}

static void wipe_free(char **p)
{
	if (*p) {
		OPENSSL_cleanse(*p, strlen(*p));
		free(*p);
		*p = 0;
	}
}

/* ---- tampon extensible */

struct buf {
	char *p;
	size_t n, cap;
};

static int b_add(struct buf *b, const void *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap * 2 : 1024;
		while (c < b->n + n + 1)
			c *= 2;
		char *q = realloc(b->p, c);
		if (!q)
			return -1;
		b->p = q;
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
	return 0;
}
static int b_str(struct buf *b, const char *s) { return b_add(b, s, strlen(s)); }
static int b_fmt(struct buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int b_fmt(struct buf *b, const char *fmt, ...)
{
	char t[2048];
	va_list ap;
	va_start(ap, fmt);
	int k = vsnprintf(t, sizeof t, fmt, ap);
	va_end(ap);
	if (k < 0 || k >= (int)sizeof t)
		return -1;
	return b_add(b, t, (size_t)k);
}
static void b_free(struct buf *b)
{
	if (b->p) {
		OPENSSL_cleanse(b->p, b->cap);
		free(b->p);
	}
	b->p = 0;
	b->n = b->cap = 0;
}

/* encodage d'URL (formulaires et paramètres) */
static int b_enc(struct buf *b, const char *s)
{
	static const char hex[] = "0123456789ABCDEF";
	for (const u8 *p = (const u8 *)s; *p; p++) {
		u8 c = *p;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
		    c == '_' || c == '~') {
			if (b_add(b, &c, 1))
				return -1;
		} else {
			char t[3] = {'%', hex[c >> 4], hex[c & 15]};
			if (b_add(b, t, 3))
				return -1;
		}
	}
	return 0;
}
static int b_param(struct buf *b, const char *k, const char *v)
{
	if (b->n && b->p[b->n - 1] != '?' && b_str(b, "&"))
		return -1;
	if (b_str(b, k) || b_str(b, "="))
		return -1;
	return b_enc(b, v);
}

static int hexv(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* décodage d'URL en place (« + » : espace) */
static void url_dec(char *s)
{
	char *d = s;
	for (; *s; s++) {
		if (*s == '%' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) {
			*d++ = (char)(hexv(s[1]) * 16 + hexv(s[2]));
			s += 2;
		} else
			*d++ = *s == '+' ? ' ' : *s;
	}
	*d = 0;
}

/* la valeur d'un paramètre de « a=1&b=2 » (après « ? » ou « # » s'il y en a) -> 1 trouvé */
static int query_get(const char *q, const char *key, char *out, size_t n)
{
	const char *s = strpbrk(q, "?#");
	s = s ? s + 1 : q;
	size_t kn = strlen(key);
	while (*s) {
		const char *e = s + strcspn(s, "&# \r\n");
		if ((size_t)(e - s) > kn && !strncmp(s, key, kn) && s[kn] == '=') {
			size_t l = (size_t)(e - s - kn - 1);
			if (l >= n)
				l = n - 1;
			memcpy(out, s + kn + 1, l);
			out[l] = 0;
			url_dec(out);
			return 1;
		}
		if (!*e || *e == ' ' || *e == '\r' || *e == '\n')
			break;
		s = e + 1;
	}
	return 0;
}

/* ---- JSON : les champs du premier niveau d'une réponse (assez pour les serveurs d'autorisation) */

static const char *json_skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	return p;
}

/* fin d'une valeur JSON (chaîne, nombre, objet, tableau, mot) */
static const char *json_skip(const char *p)
{
	p = json_skip_ws(p);
	if (*p == '"') {
		for (p++; *p && *p != '"'; p++)
			if (*p == '\\' && p[1])
				p++;
		return *p ? p + 1 : p;
	}
	if (*p == '{' || *p == '[') {
		int depth = 0;
		for (; *p; p++) {
			if (*p == '"') {
				p = json_skip(p) - 1;
				continue;
			}
			if (*p == '{' || *p == '[')
				depth++;
			else if ((*p == '}' || *p == ']') && --depth == 0)
				return p + 1;
		}
		return p;
	}
	while (*p && *p != ',' && *p != '}' && *p != ']')
		p++;
	return p;
}

static const char *json_find(const char *j, const char *key)
{
	const char *p = json_skip_ws(j);
	if (*p != '{')
		return 0;
	p++;
	size_t kn = strlen(key);
	for (;;) {
		p = json_skip_ws(p);
		if (*p != '"')
			return 0;
		const char *ks = p + 1;
		const char *ke = json_skip(p) - 1;
		p = json_skip_ws(ke + 1);
		if (*p != ':')
			return 0;
		p = json_skip_ws(p + 1);
		if ((size_t)(ke - ks) == kn && !memcmp(ks, key, kn))
			return p;
		p = json_skip_ws(json_skip(p));
		if (*p != ',')
			return 0;
		p++;
	}
}

static void u8put(struct buf *b, unsigned u)
{
	char t[4];
	int n;
	if (u < 0x80) {
		t[0] = (char)u;
		n = 1;
	} else if (u < 0x800) {
		t[0] = (char)(0xC0 | u >> 6);
		t[1] = (char)(0x80 | (u & 63));
		n = 2;
	} else if (u < 0x10000) {
		t[0] = (char)(0xE0 | u >> 12);
		t[1] = (char)(0x80 | ((u >> 6) & 63));
		t[2] = (char)(0x80 | (u & 63));
		n = 3;
	} else {
		t[0] = (char)(0xF0 | u >> 18);
		t[1] = (char)(0x80 | ((u >> 12) & 63));
		t[2] = (char)(0x80 | ((u >> 6) & 63));
		t[3] = (char)(0x80 | (u & 63));
		n = 4;
	}
	b_add(b, t, (size_t)n);
}

/* une chaîne (malloc, à libérer) ; 0 : absente */
static char *json_str(const char *j, const char *key)
{
	const char *v = json_find(j, key);
	if (!v || *v != '"')
		return 0;
	struct buf o = {0};
	b_add(&o, "", 0);
	for (v++; *v && *v != '"'; v++) {
		if (*v != '\\') {
			b_add(&o, v, 1);
			continue;
		}
		v++;
		switch (*v) {
		case 'n': b_add(&o, "\n", 1); break;
		case 't': b_add(&o, "\t", 1); break;
		case 'r': case 'b': case 'f': break;
		case 'u': {
			unsigned u = 0;
			for (int i = 1; i <= 4; i++)
				u = u * 16 + (unsigned)(hexv(v[i]) < 0 ? 0 : hexv(v[i]));
			for (int i = 0; i < 4 && v[1]; i++)
				v++;
			if (u >= 0xD800 && u < 0xDC00 && v[1] == '\\' && v[2] == 'u') {
				unsigned lo = 0;
				for (int i = 3; i <= 6; i++)
					lo = lo * 16 + (unsigned)(hexv(v[i]) < 0 ? 0 : hexv(v[i]));
				if (lo >= 0xDC00 && lo < 0xE000) {
					u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
					v += 6;
				}
			}
			u8put(&o, u);
			break;
		}
		case 0: v--; break;
		default: b_add(&o, v, 1);
		}
	}
	return o.p;
}

static long json_num(const char *j, const char *key, long def)
{
	const char *v = json_find(j, key);
	if (!v)
		return def;
	if (*v == '"')
		v++;
	return (*v >= '0' && *v <= '9') || *v == '-' ? atol(v) : def;
}

/* ---- HTTPS : une requête, la réponse entière (HTTP/1.0, la connexion fermée à la fin) */

struct url {
	int tls;
	char host[256];
	int port;
	char path[1024];
};

static int private_host(const char *h)
{
	if (!strcmp(h, "localhost"))
		return 1;
	unsigned b0, b1, b2, b3;
	char extra;
	if (sscanf(h, "%u.%u.%u.%u%c", &b0, &b1, &b2, &b3, &extra) != 4 || b0 > 255 || b1 > 255 || b2 > 255 || b3 > 255)
		return 0;
	u32 ip = b0 << 24 | b1 << 16 | b2 << 8 | b3;
	return (ip >> 24) == 127 || (ip >> 24) == 10 || (ip >> 16) == 0xC0A8 || (ip >> 20) == 0xAC1;
}

static int url_parse(const char *s, struct url *u)
{
	memset(u, 0, sizeof *u);
	if (!strncasecmp(s, "https://", 8)) {
		u->tls = 1;
		u->port = 443;
		s += 8;
	} else if (!strncasecmp(s, "http://", 7)) {
		u->port = 80;
		s += 7;
	} else
		return -1;
	size_t hl = strcspn(s, ":/?");
	if (!hl || hl >= sizeof u->host)
		return -1;
	memcpy(u->host, s, hl);
	u->host[hl] = 0;
	s += hl;
	if (*s == ':') {
		u->port = atoi(s + 1);
		s += 1 + strspn(s + 1, "0123456789");
	}
	if (u->port <= 0 || u->port > 65535)
		return -1;
	scpy(u->path, *s == '/' ? s : "/", sizeof u->path);
	if (*s == '?')
		snprintf(u->path, sizeof u->path, "/%s", s);
	/* en clair : pour les essais seulement (un faux serveur sur le réseau local) */
	if (!u->tls && !private_host(u->host))
		return -2;
	return 0;
}

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int wait_fd(int fd, short ev, long deadline)
{
	long left = deadline - now_ms();
	if (left <= 0)
		return 0;
	struct pollfd p = {fd, ev, 0};
	return poll(&p, 1, (int)(left > 200 ? 200 : left)) >= 0;
}

static SSL_CTX *tls_ctx(void)
{
	if (!ssl_ctx) {
		ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!ssl_ctx)
			return 0;
		SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
		SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, 0);
		SSL_CTX_set_default_verify_paths(ssl_ctx); /* (/etc/ca-bundle, docs/OPENSSL.md) */
	}
	return ssl_ctx;
}

/* méthode, adresse, corps de formulaire (0 : aucun) -> statut HTTP (< 0 : erreur), corps dans out */
static int http(const char *method, const char *url_s, const char *form, const char *bearer, struct buf *out)
{
	struct url u;
	int pr = url_parse(url_s, &u);
	if (pr == -2)
		return (int)fail(E_ACCES, "%s : adresse en clair refusée (https seulement, sauf réseau local)", url_s);
	if (pr)
		return (int)fail(E_INVAL, "adresse invalide : %s", url_s);
	long deadline = now_ms() + HTTP_TIMEOUT;

	struct addrinfo hints, *res = 0;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	char ps[8];
	snprintf(ps, sizeof ps, "%d", u.port);
	if (getaddrinfo(u.host, ps, &hints, &res) != 0 || !res)
		return (int)fail(E_IO, "serveur %s introuvable (réseau ?)", u.host);
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		freeaddrinfo(res);
		if (fd >= 0)
			close(fd);
		return (int)fail(E_CONNREFUSED, "le serveur %s ne répond pas (port %d)", u.host, u.port);
	}
	freeaddrinfo(res);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	SSL *ssl = 0;
	int r = E_IO;
	if (u.tls) {
		SSL_CTX *c = tls_ctx();
		if (!c || !(ssl = SSL_new(c))) {
			r = (int)fail(E_NOMEM, "TLS indisponible");
			goto done;
		}
		SSL_set_fd(ssl, fd);
		SSL_set_tlsext_host_name(ssl, u.host);
		SSL_set1_host(ssl, u.host);
		for (;;) {
			ERR_clear_error();
			int k = SSL_connect(ssl);
			if (k == 1)
				break;
			int e = SSL_get_error(ssl, k);
			if ((e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) && now_ms() < deadline) {
				wait_fd(fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline);
				continue;
			}
			long v = SSL_get_verify_result(ssl);
			if (v != X509_V_OK)
				r = (int)fail(E_ACCES, "certificat de %s refusé : %s", u.host, X509_verify_cert_error_string(v));
			else
				r = (int)fail(now_ms() >= deadline ? E_TIMEDOUT : E_IO, "poignée de main TLS avec %s impossible", u.host);
			goto done;
		}
	}

	{
		struct buf req = {0};
		b_fmt(&req, "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: KorinOS-oauthd/1\r\nAccept: application/json\r\n"
			    "Connection: close\r\n",
		      method, u.path, u.host);
		if (bearer)
			b_fmt(&req, "Authorization: Bearer %s\r\n", bearer);
		if (form)
			b_fmt(&req, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\n\r\n", strlen(form));
		else
			b_str(&req, "\r\n");
		if (form)
			b_str(&req, form);
		size_t off = 0;
		while (req.p && off < req.n) {
			int k;
			if (ssl) {
				ERR_clear_error();
				k = SSL_write(ssl, req.p + off, (int)(req.n - off));
				if (k <= 0) {
					int e = SSL_get_error(ssl, k);
					if ((e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) && now_ms() < deadline) {
						wait_fd(fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline);
						continue;
					}
					break;
				}
			} else {
				k = (int)send(fd, req.p + off, req.n - off, 0);
				if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now_ms() < deadline) {
					wait_fd(fd, POLLOUT, deadline);
					continue;
				}
				if (k <= 0)
					break;
			}
			off += (size_t)k;
		}
		int sent = req.p && off == req.n;
		b_free(&req);
		if (!sent) {
			r = (int)fail(E_IO, "envoi à %s interrompu", u.host);
			goto done;
		}
	}

	{
		struct buf raw = {0};
		char t[4096];
		for (;;) {
			int k;
			if (ssl) {
				ERR_clear_error();
				k = SSL_read(ssl, t, sizeof t);
				if (k <= 0) {
					int e = SSL_get_error(ssl, k);
					if ((e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) && now_ms() < deadline) {
						wait_fd(fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline);
						continue;
					}
					break; /* (fin : ZERO_RETURN, ou fermeture sans close_notify) */
				}
			} else {
				k = (int)recv(fd, t, sizeof t, 0);
				if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now_ms() < deadline) {
					wait_fd(fd, POLLIN, deadline);
					continue;
				}
				if (k <= 0)
					break;
			}
			if (raw.n + (size_t)k > BODY_MAX + 16384 || b_add(&raw, t, (size_t)k))
				break;
		}
		OPENSSL_cleanse(t, sizeof t);
		char *he = raw.p ? strstr(raw.p, "\r\n\r\n") : 0;
		int status = raw.p && !strncmp(raw.p, "HTTP/", 5) ? atoi(raw.p + strcspn(raw.p, " ")) : 0;
		if (!he || status < 100) {
			r = (int)fail(now_ms() >= deadline ? E_TIMEDOUT : E_IO, "réponse illisible de %s", u.host);
			b_free(&raw);
			goto done;
		}
		*he = 0;
		const char *body = he + 4;
		size_t blen = raw.n - (size_t)(body - raw.p);
		int chunked = 0;
		for (char *h = strstr(raw.p, "\r\n"); h; h = strstr(h + 2, "\r\n"))
			if (!strncasecmp(h + 2, "Transfer-Encoding:", 18)) {
				const char *v = h + 2 + 18;
				v += strspn(v, " \t");
				chunked = !strncasecmp(v, "chunked", 7);
			}
		b_add(out, "", 0);
		if (chunked) {
			const char *p = body, *end = body + blen;
			while (p < end) {
				unsigned long n = strtoul(p, 0, 16);
				const char *nl = strstr(p, "\r\n");
				if (!nl || !n)
					break;
				p = nl + 2;
				if (p + n > end)
					n = (unsigned long)(end - p);
				b_add(out, p, n);
				p += n + 2;
			}
		} else
			b_add(out, body, blen);
		b_free(&raw);
		r = status;
	}
done:
	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
	close(fd);
	return r;
}

/* ---- fichiers */

static char *file_read(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	struct buf b = {0};
	char t[4096];
	size_t k;
	while ((k = fread(t, 1, sizeof t, f)) > 0)
		if (b_add(&b, t, k)) {
			fclose(f);
			b_free(&b);
			return 0;
		}
	fclose(f);
	OPENSSL_cleanse(t, sizeof t);
	if (!b.p)
		b_add(&b, "", 0);
	if (len)
		*len = b.n;
	return b.p;
}

/* écrit à côté puis renomme : un fichier jamais à moitié écrit */
static int file_save(const char *path, const void *p, size_t n)
{
	mkdir(DIR_PATH, 0700);
	char tmp[128];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "wb");
	if (!f)
		return -1;
	int ok = fwrite(p, 1, n, f) == n;
	ok = fclose(f) == 0 && ok;
	if (!ok) {
		unlink(tmp);
		return -1;
	}
	unlink(path);
	return rename(tmp, path);
}

/* ---- les secrets (AES-256-GCM) : « KORINOA1 », 12 octets d'IV, 16 de marque, le texte chiffré ;
 * texte : « nom\tjeton\n » par compte. La clé est sur la même carte (comme Mail) : un fichier copié seul
 * ne se lit pas, la carte entière, si. */

static int gcm(int enc, const u8 *k, const u8 *iv, const u8 *in, int n, u8 *out, u8 *tag)
{
	EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
	if (!c)
		return 0;
	int l = 0;
	int ok = EVP_CipherInit_ex(c, EVP_aes_256_gcm(), 0, 0, 0, enc) == 1 &&
		 EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, 0) == 1 && EVP_CipherInit_ex(c, 0, 0, k, iv, enc) == 1 &&
		 (n == 0 || EVP_CipherUpdate(c, out, &l, in, n) == 1);
	if (ok && !enc)
		ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1;
	ok = ok && EVP_CipherFinal_ex(c, out + l, &l) == 1;
	if (ok && enc)
		ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
	EVP_CIPHER_CTX_free(c);
	return ok;
}

static int get_key(u8 k[32], int create)
{
	size_t len = 0;
	char *b = file_read(KEY_FILE, &len);
	if (b && len == 32) {
		memcpy(k, b, 32);
		OPENSSL_cleanse(b, 32);
		free(b);
		return 1;
	}
	free(b);
	if (!create || RAND_bytes(k, 32) != 1)
		return 0;
	return file_save(KEY_FILE, k, 32) == 0;
}

static struct account *find(const char *name);
static struct account *add(const char *name);

static void load_secrets(void)
{
	size_t len = 0;
	char *b = file_read(SECRETS_FILE, &len);
	u8 k[32];
	if (b && len > 8 + 12 + 16 && !memcmp(b, SECRETS_MAGIC, 8) && get_key(k, 0)) {
		int n = (int)(len - 36);
		u8 *plain = malloc((size_t)n + 1);
		if (plain && gcm(0, k, (u8 *)b + 8, (u8 *)b + 36, n, plain, (u8 *)b + 20)) {
			plain[n] = 0;
			for (char *line = (char *)plain; *line;) {
				char *e = line + strcspn(line, "\n");
				char *tab = memchr(line, '\t', (size_t)(e - line));
				if (tab) {
					*tab = 0;
					struct account *a = find(line);
					if (a) {
						wipe_free(&a->refresh);
						a->refresh = strndup(tab + 1, (size_t)(e - tab - 1));
					}
				}
				line = *e ? e + 1 : e;
			}
		} else if (plain)
			say("secrets illisibles (clé changée ?) : comptes à reconnecter");
		if (plain) {
			OPENSSL_cleanse(plain, (size_t)n);
			free(plain);
		}
		OPENSSL_cleanse(k, sizeof k);
	}
	free(b);
}

static int save_secrets(void)
{
	struct buf t = {0};
	b_add(&t, "", 0);
	for (int i = 0; i < ACC_MAX; i++)
		if (accs[i].used && accs[i].refresh)
			b_fmt(&t, "%s\t%s\n", accs[i].name, accs[i].refresh);
	u8 k[32];
	int ok = 0;
	if (get_key(k, 1)) {
		size_t n = t.n;
		u8 *out = malloc(36 + n + 1);
		if (out) {
			memcpy(out, SECRETS_MAGIC, 8);
			if (RAND_bytes(out + 8, 12) == 1 && gcm(1, k, out + 8, (u8 *)t.p, (int)n, out + 36, out + 20))
				ok = file_save(SECRETS_FILE, out, 36 + n) == 0;
			free(out);
		}
		OPENSSL_cleanse(k, sizeof k);
	}
	b_free(&t);
	if (!ok)
		say("écriture de %s impossible", SECRETS_FILE);
	return ok ? 0 : -1;
}

/* ---- la configuration (accounts.ini) */

static const char *const KEYS[] = {"provider", "client_id", "client_secret", "scope", "auth_url",
				   "token_url", "device_url", "auth_params", "tenant", 0};

static char *field(struct account *a, const char *k, size_t *n)
{
#define F(name)                                                                                                        \
	if (!strcmp(k, #name)) {                                                                                       \
		*n = sizeof a->name;                                                                                   \
		return a->name;                                                                                        \
	}
	F(provider) F(client_id) F(client_secret) F(scope) F(auth_url) F(token_url) F(device_url) F(auth_params) F(tenant)
#undef F
	return 0;
}

static int name_ok(const char *s)
{
	size_t n = strlen(s);
	if (!n || n >= NAME_MAX_)
		return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '.' ||
		      *s == '_' || *s == '-' || *s == '/' || *s == '@'))
			return 0;
	return 1;
}

static struct account *find(const char *name)
{
	for (int i = 0; i < ACC_MAX; i++)
		if (accs[i].used && !strcmp(accs[i].name, name))
			return &accs[i];
	return 0;
}

static struct account *add(const char *name)
{
	struct account *a = find(name);
	if (a)
		return a;
	for (int i = 0; i < ACC_MAX; i++)
		if (!accs[i].used) {
			a = &accs[i];
			memset(a, 0, sizeof *a);
			a->used = 1;
			a->listen_fd = -1;
			scpy(a->name, name, sizeof a->name);
			return a;
		}
	return 0;
}

static void trim(char **s, char **e)
{
	while (*s < *e && (**s == ' ' || **s == '\t'))
		(*s)++;
	while (*e > *s && ((*e)[-1] == ' ' || (*e)[-1] == '\t' || (*e)[-1] == '\r'))
		(*e)--;
}

/* « clé = valeur » par ligne dans a ; « [nom] » change de compte si sections != 0 -> champs lus */
static int parse_conf(char *text, struct account *a, int sections)
{
	int n = 0;
	for (char *line = text; *line;) {
		char *e = line + strcspn(line, "\n");
		char *next = *e ? e + 1 : e;
		char *s = line;
		trim(&s, &e);
		if (s < e && (*s == '#' || *s == ';')) {
			line = next;
			continue;
		}
		if (sections && s < e && *s == '[' && e[-1] == ']') {
			*(e - 1) = 0;
			a = name_ok(s + 1) ? add(s + 1) : 0;
		} else if (a && s < e) {
			char *eq = memchr(s, '=', (size_t)(e - s));
			if (eq) {
				char *ks = s, *ke = eq, *vs = eq + 1, *ve = e;
				trim(&ks, &ke);
				trim(&vs, &ve);
				*ke = 0;
				size_t cap;
				char *f = field(a, ks, &cap);
				if (f && (size_t)(ve - vs) < cap) {
					memcpy(f, vs, (size_t)(ve - vs));
					f[ve - vs] = 0;
					n++;
				}
			}
		}
		line = next;
	}
	return n;
}

static void load(void)
{
	if (loaded)
		return;
	loaded = 1;
	char *t = file_read(ACCOUNTS_INI, 0);
	if (t) {
		parse_conf(t, 0, 1);
		free(t);
	}
	load_secrets();
}

static int save_accounts(void)
{
	struct buf t = {0};
	b_str(&t, "# Comptes OAuth (oauthd, docs/OAUTH.md) : écrit par oauthd ; les jetons sont dans secrets, chiffrés.\n");
	for (int i = 0; i < ACC_MAX; i++) {
		struct account *a = &accs[i];
		if (!a->used)
			continue;
		b_fmt(&t, "\n[%s]\n", a->name);
		for (int k = 0; KEYS[k]; k++) {
			size_t cap;
			const char *v = field(a, KEYS[k], &cap);
			if (v && *v)
				b_fmt(&t, "%s = %s\n", KEYS[k], v);
		}
	}
	int r = t.p ? file_save(ACCOUNTS_INI, t.p, t.n) : -1;
	b_free(&t);
	return r;
}

/* ---- fournisseurs connus */

struct endpoints {
	char auth[TEXT], token[TEXT], device[TEXT], params[TEXT];
};

static void endpoints_of(struct account *a, struct endpoints *e)
{
	memset(e, 0, sizeof *e);
	const char *tenant = a->tenant[0] ? a->tenant : "common";
	if (!strcasecmp(a->provider, "google")) {
		scpy(e->auth, "https://accounts.google.com/o/oauth2/v2/auth", TEXT);
		scpy(e->token, "https://oauth2.googleapis.com/token", TEXT);
		scpy(e->device, "https://oauth2.googleapis.com/device/code", TEXT);
		/* (sans eux, Google ne rend pas de jeton de rafraîchissement) */
		scpy(e->params, "access_type=offline&prompt=consent", TEXT);
	} else if (!strcasecmp(a->provider, "microsoft")) {
		snprintf(e->auth, TEXT, "https://login.microsoftonline.com/%s/oauth2/v2.0/authorize", tenant);
		snprintf(e->token, TEXT, "https://login.microsoftonline.com/%s/oauth2/v2.0/token", tenant);
		snprintf(e->device, TEXT, "https://login.microsoftonline.com/%s/oauth2/v2.0/devicecode", tenant);
	}
	if (a->auth_url[0])
		scpy(e->auth, a->auth_url, TEXT);
	if (a->token_url[0])
		scpy(e->token, a->token_url, TEXT);
	if (a->device_url[0])
		scpy(e->device, a->device_url, TEXT);
	if (a->auth_params[0])
		scpy(e->params, a->auth_params, TEXT);
}

/* le client (client_id, client_secret) dans un formulaire */
static void form_client(struct buf *f, struct account *a)
{
	b_param(f, "client_id", a->client_id);
	if (a->client_secret[0])
		b_param(f, "client_secret", a->client_secret);
}

/* le message d'erreur d'une réponse du serveur */
static long server_error(long r, struct account *a, int status, const char *body)
{
	char *e = json_str(body, "error");
	char *d = json_str(body, "error_description");
	long rr = fail(r, "%s : %s%s%s (HTTP %d)", a->name, e ? e : "erreur", d ? " : " : "", d ? d : "", status);
	free(e);
	free(d);
	return rr;
}

/* une réponse de jeton (code, appareil, rafraîchissement) -> 0, ou erreur */
static long take_tokens(struct account *a, int status, const char *body)
{
	char *acc = json_str(body, "access_token");
	if (status != 200 || !acc) {
		free(acc);
		return server_error(E_ACCES, a, status, body);
	}
	wipe_free(&a->access);
	a->access = acc;
	long exp = json_num(body, "expires_in", 3600);
	a->access_until = time(0) + (exp > 120 ? exp - 60 : exp);
	char *ref = json_str(body, "refresh_token");
	if (ref) { /* (Microsoft en rend un nouveau à chaque fois) */
		int changed = !a->refresh || strcmp(a->refresh, ref);
		wipe_free(&a->refresh);
		a->refresh = ref;
		if (changed)
			save_secrets();
	} else if (!a->refresh)
		say("%s : pas de jeton de rafraîchissement (portée offline_access, ou access_type=offline ?)", a->name);
	return 0;
}

static int configured(struct account *a)
{
	struct endpoints e;
	endpoints_of(a, &e);
	return a->client_id[0] && e.token[0];
}

/* ---- les opérations */

static long op_setup(struct account *a, const char *conf)
{
	char old_client[TEXT], old_token[TEXT];
	scpy(old_client, a->client_id, sizeof old_client);
	struct endpoints e;
	endpoints_of(a, &e);
	scpy(old_token, e.token, sizeof old_token);
	for (int k = 0; KEYS[k]; k++) {
		size_t cap;
		char *f = field(a, KEYS[k], &cap);
		f[0] = 0;
	}
	char *t = strdup(conf);
	if (!t)
		return E_NOMEM;
	parse_conf(t, a, 0);
	OPENSSL_cleanse(t, strlen(t));
	free(t);
	if (!a->provider[0])
		scpy(a->provider, "custom", sizeof a->provider);
	if (!configured(a))
		return fail(E_INVAL, "%s : il faut client_id et un fournisseur connu (google, microsoft) ou token_url", a->name);
	endpoints_of(a, &e);
	/* un autre client ou un autre serveur : les jetons d'avant ne servent plus */
	if (strcmp(old_client, a->client_id) || strcmp(old_token, e.token)) {
		wipe_free(&a->access);
		if (a->refresh) {
			wipe_free(&a->refresh);
			save_secrets();
		}
	}
	return save_accounts() ? fail(E_IO, "écriture de %s impossible", ACCOUNTS_INI) : 0;
}

static long op_status(struct account *a)
{
	if (a->access && time(0) < a->access_until)
		return 2;
	return a->refresh ? 1 : 0;
}

static long op_device_begin(struct account *a, struct buf *out)
{
	struct endpoints e;
	endpoints_of(a, &e);
	if (!e.device[0])
		return fail(E_INVAL, "%s : ce fournisseur n'a pas de code d'appareil (device_url)", a->name);
	struct buf f = {0}, body = {0};
	form_client(&f, a);
	if (a->scope[0])
		b_param(&f, "scope", a->scope);
	int st = http("POST", e.device, f.p, 0, &body);
	b_free(&f);
	if (st < 0) {
		b_free(&body);
		return st;
	}
	char *dc = json_str(body.p, "device_code");
	char *uc = json_str(body.p, "user_code");
	char *uri = json_str(body.p, "verification_uri");
	if (!uri)
		uri = json_str(body.p, "verification_url"); /* (Google) */
	char *full = json_str(body.p, "verification_uri_complete");
	long r;
	if (st != 200 || !dc || !uc || !uri)
		r = server_error(E_IO, a, st, body.p);
	else {
		scpy(a->device_code, dc, sizeof a->device_code);
		long exp = json_num(body.p, "expires_in", 900);
		a->device_interval = (int)json_num(body.p, "interval", 5);
		if (a->device_interval < 1)
			a->device_interval = 5;
		a->device_until = time(0) + exp;
		a->device_next = time(0) + a->device_interval;
		b_fmt(out, "%s\n%s\n%s\n%ld\n%d\n", uc, uri, full ? full : "", exp, a->device_interval);
		r = 0;
	}
	free(dc);
	free(uc);
	free(uri);
	free(full);
	b_free(&body);
	return r;
}

static long op_device_poll(struct account *a)
{
	if (!a->device_code[0])
		return fail(E_INVAL, "%s : aucune connexion par code en cours (oauth_device_begin)", a->name);
	time_t t = time(0);
	if (t >= a->device_until) {
		a->device_code[0] = 0;
		return fail(E_TIMEDOUT, "%s : le code a expiré, recommencer", a->name);
	}
	if (t < a->device_next)
		return E_AGAIN; /* (pas plus vite que le serveur ne le permet) */
	struct endpoints e;
	endpoints_of(a, &e);
	struct buf f = {0}, body = {0};
	b_param(&f, "grant_type", "urn:ietf:params:oauth:grant-type:device_code");
	b_param(&f, "device_code", a->device_code);
	form_client(&f, a);
	int st = http("POST", e.token, f.p, 0, &body);
	b_free(&f);
	a->device_next = time(0) + a->device_interval;
	if (st < 0) {
		b_free(&body);
		return E_AGAIN; /* (le réseau : on redemandera) */
	}
	long r;
	char *err = st == 200 ? 0 : json_str(body.p, "error");
	if (err && !strcmp(err, "authorization_pending"))
		r = E_AGAIN;
	else if (err && !strcmp(err, "slow_down")) {
		a->device_interval += 5;
		a->device_next = time(0) + a->device_interval;
		r = E_AGAIN;
	} else {
		r = take_tokens(a, st, body.p);
		a->device_code[0] = 0;
	}
	free(err);
	b_free(&body);
	return r;
}

/* base64url sans « = » */
static void b64url(const u8 *in, int n, char *out)
{
	static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	int o = 0;
	for (int i = 0; i < n; i += 3) {
		u32 v = (u32)in[i] << 16 | (i + 1 < n ? (u32)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
		out[o++] = T[v >> 18 & 63];
		out[o++] = T[v >> 12 & 63];
		if (i + 1 < n)
			out[o++] = T[v >> 6 & 63];
		if (i + 2 < n)
			out[o++] = T[v & 63];
	}
	out[o] = 0;
}

static void browser_stop(struct account *a)
{
	if (a->listen_fd >= 0)
		close(a->listen_fd);
	a->listen_fd = -1;
	a->state[0] = 0;
}

static long op_browser_begin(struct account *a, long port, struct buf *out)
{
	struct endpoints e;
	endpoints_of(a, &e);
	if (!e.auth[0])
		return fail(E_INVAL, "%s : ce fournisseur n'a pas d'adresse d'accord (auth_url)", a->name);
	browser_stop(a);
	/* le retour : http://127.0.0.1:<port>/ (RFC 8252 7.3), écouté ici */
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return fail(E_IO, "pas de socket");
	int p = 0;
	for (int i = 0; i < 20 && !p; i++) {
		int try_port = port ? (int)port : 47800 + i;
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof sa);
		sa.sin_family = AF_INET;
		sa.sin_port = htons((uint16_t)try_port);
		sa.sin_addr.s_addr = htonl(INADDR_ANY); /* (netd n'écoute pas sur une seule adresse) */
		if (bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0 && listen(fd, 4) == 0)
			p = try_port;
		else if (port)
			break;
	}
	if (!p) {
		close(fd);
		return fail(E_EXIST, "%s : port %ld déjà pris", a->name, port);
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	a->listen_fd = fd;
	a->browser_until = time(0) + 600;
	snprintf(a->redirect, sizeof a->redirect, "http://127.0.0.1:%d/", p);
	u8 rnd[48];
	RAND_bytes(rnd, sizeof rnd);
	b64url(rnd, 48, a->verifier); /* (64 caractères) */
	b64url(rnd + 32, 16, a->state);
	u8 h[32];
	unsigned hl = 0;
	EVP_Digest(a->verifier, strlen(a->verifier), h, &hl, EVP_sha256(), 0);
	char challenge[48];
	b64url(h, 32, challenge);
	OPENSSL_cleanse(rnd, sizeof rnd);
	b_str(out, e.auth);
	b_str(out, strchr(e.auth, '?') ? "&" : "?");
	b_param(out, "response_type", "code");
	b_param(out, "client_id", a->client_id);
	b_param(out, "redirect_uri", a->redirect);
	if (a->scope[0])
		b_param(out, "scope", a->scope);
	b_param(out, "state", a->state);
	b_param(out, "code_challenge", challenge);
	b_param(out, "code_challenge_method", "S256");
	if (e.params[0]) {
		b_str(out, "&");
		b_str(out, e.params);
	}
	return 0;
}

static long exchange_code(struct account *a, const char *code)
{
	struct endpoints e;
	endpoints_of(a, &e);
	struct buf f = {0}, body = {0};
	b_param(&f, "grant_type", "authorization_code");
	b_param(&f, "code", code);
	b_param(&f, "redirect_uri", a->redirect);
	b_param(&f, "code_verifier", a->verifier);
	form_client(&f, a);
	int st = http("POST", e.token, f.p, 0, &body);
	b_free(&f);
	long r = st < 0 ? st : take_tokens(a, st, body.p);
	b_free(&body);
	if (r == 0) {
		browser_stop(a);
		OPENSSL_cleanse(a->verifier, sizeof a->verifier);
		a->verifier[0] = 0;
	}
	return r;
}

/* une adresse de retour (ou le code seul) -> 0 connecté */
static long finish_with(struct account *a, const char *reply)
{
	char code[1024], st[64], err[128];
	if (!a->verifier[0])
		return fail(E_INVAL, "%s : aucune connexion par navigateur en cours (oauth_browser_begin)", a->name);
	if (query_get(reply, "error", err, sizeof err))
		return fail(E_ACCES, "%s : accord refusé (%s)", a->name, err);
	if (query_get(reply, "code", code, sizeof code)) {
		if (!query_get(reply, "state", st, sizeof st) || strcmp(st, a->state))
			return fail(E_ACCES, "%s : réponse d'une autre demande (state), recommencer", a->name);
	} else if (!strpbrk(reply, "?=&/ ") && strlen(reply) < sizeof code)
		scpy(code, reply, sizeof code); /* (le code seul, collé) */
	else
		return fail(E_INVAL, "%s : pas de code dans cette adresse", a->name);
	long r = exchange_code(a, code);
	OPENSSL_cleanse(code, sizeof code);
	return r;
}

static void http_answer(int fd, int status, const char *text)
{
	char t[1024];
	int n = snprintf(t, sizeof t,
			 "HTTP/1.0 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
			 "<!doctype html><meta charset=utf-8><title>Korin OS</title>"
			 "<body style=\"font-family:sans-serif;margin:3em\"><p>%s</p></body>",
			 status, status == 200 ? "OK" : "Not Found", text);
	long deadline = now_ms() + 2000;
	for (int off = 0; off < n;) {
		int k = (int)send(fd, t + off, (size_t)(n - off), 0);
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now_ms() < deadline) {
			wait_fd(fd, POLLOUT, deadline);
			continue;
		}
		if (k <= 0)
			break;
		off += k;
	}
}

static long op_browser_poll(struct account *a)
{
	if (a->listen_fd < 0)
		return a->verifier[0] ? E_AGAIN : fail(E_INVAL, "%s : aucune connexion par navigateur en cours", a->name);
	if (time(0) > a->browser_until) {
		browser_stop(a);
		return fail(E_TIMEDOUT, "%s : pas de réponse du navigateur, recommencer", a->name);
	}
	int c = accept(a->listen_fd, 0, 0);
	if (c < 0)
		return E_AGAIN;
	fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK);
	char req[4096];
	int n = 0;
	long deadline = now_ms() + 3000;
	while (n < (int)sizeof req - 1 && now_ms() < deadline) {
		int k = (int)recv(c, req + n, sizeof req - 1 - (size_t)n, 0);
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			wait_fd(c, POLLIN, deadline);
			continue;
		}
		if (k <= 0)
			break;
		n += k;
		req[n] = 0;
		if (strstr(req, "\r\n\r\n"))
			break;
	}
	req[n] = 0;
	long r;
	char *line_end = strstr(req, "\r\n");
	if (line_end)
		*line_end = 0;
	if (strncmp(req, "GET ", 4) || (!strstr(req, "code=") && !strstr(req, "error="))) {
		http_answer(c, 404, "");
		r = E_AGAIN; /* (favicon.ico...) */
	} else {
		r = finish_with(a, req + 4);
		const char *e = 0;
		for (int i = 0; i < ERR_MAX; i++)
			if (errs[i].pid == pid_cour)
				e = errs[i].text;
		http_answer(c, 200, r == 0 ? "Connexion réussie. Vous pouvez fermer cette page et revenir à Korin."
					   : e ? e : "La connexion a échoué.");
	}
	OPENSSL_cleanse(req, sizeof req);
	close(c);
	return r;
}

static long op_token(struct account *a, struct buf *out)
{
	if (!(a->access && time(0) < a->access_until)) {
		if (!a->refresh)
			return fail(E_ACCES, "%s : pas connecté (oauth_device_begin ou oauth_browser_begin)", a->name);
		struct endpoints e;
		endpoints_of(a, &e);
		struct buf f = {0}, body = {0};
		b_param(&f, "grant_type", "refresh_token");
		b_param(&f, "refresh_token", a->refresh);
		form_client(&f, a);
		int st = http("POST", e.token, f.p, 0, &body);
		b_free(&f);
		if (st < 0) {
			b_free(&body);
			return st;
		}
		char *err = st == 200 ? 0 : json_str(body.p, "error");
		long r;
		if (err && !strcmp(err, "invalid_grant")) { /* (révoqué, expiré, mot de passe changé) */
			wipe_free(&a->refresh);
			wipe_free(&a->access);
			save_secrets();
			r = server_error(E_ACCES, a, st, body.p);
		} else
			r = take_tokens(a, st, body.p);
		free(err);
		b_free(&body);
		if (r)
			return r;
	}
	b_str(out, a->access);
	return 0;
}

static long op_forget(struct account *a)
{
	wipe_free(&a->access);
	browser_stop(a);
	a->device_code[0] = 0;
	if (a->refresh) {
		wipe_free(&a->refresh);
		if (save_secrets())
			return fail(E_IO, "écriture de %s impossible", SECRETS_FILE);
	}
	return 0;
}

static long op_remove(struct account *a)
{
	long r = op_forget(a);
	memset(a, 0, sizeof *a);
	a->listen_fd = -1;
	if (save_accounts())
		return fail(E_IO, "écriture de %s impossible", ACCOUNTS_INI);
	return r;
}

static long op_info(struct account *a, struct buf *out)
{
	struct endpoints e;
	endpoints_of(a, &e);
	b_fmt(out, "provider = %s\nclient_id = %s\nclient_secret = %s\nscope = %s\nauth_url = %s\ntoken_url = %s\n"
		   "device_url = %s\nauth_params = %s\n",
	      a->provider, a->client_id, a->client_secret[0] ? "(défini)" : "", a->scope, e.auth, e.token, e.device, e.params);
	if (a->tenant[0])
		b_fmt(out, "tenant = %s\n", a->tenant);
	return 0;
}

static long op_list(struct buf *out)
{
	b_add(out, "", 0);
	for (int i = 0; i < ACC_MAX; i++)
		if (accs[i].used)
			b_fmt(out, "%s\t%ld\t%s\n", accs[i].name, op_status(&accs[i]), accs[i].provider);
	return 0;
}

/* ---- la boucle */

/* copie la sortie dans le tampon de l'appelant (c, d) -> octets (0 final compris si la place y est) */
static long reply_out(struct msg *m, struct buf *out)
{
	size_t n = out->n;
	if (m->d <= 0 || !m->c)
		return (long)n; /* (la taille seule) */
	size_t cap = (size_t)(m->d > USER_MAX ? USER_MAX : m->d);
	if (n + 1 > cap)
		return fail(E_RANGE, "tampon trop petit (%zu octets demandés)", n + 1);
	long va = ksys(SYS_SVC_MAP_CALLER, req_cour, m->c, (long)cap, MC_WRITE);
	if (va < 0)
		return va;
	memcpy((void *)va, out->p ? out->p : "", n);
	((char *)va)[n] = 0;
	return (long)n;
}

/* le texte du tampon de l'appelant (c, d) -> malloc */
static char *user_text(struct msg *m)
{
	if (!m->c || m->d <= 0 || m->d > USER_MAX)
		return 0;
	long va = ksys(SYS_SVC_MAP_CALLER, req_cour, m->c, m->d, 0);
	if (va < 0)
		return 0;
	return strndup((const char *)va, (size_t)m->d);
}

static long handle(struct msg *m)
{
	long op = m->op;
	struct buf out = {0};
	long r;
	load();
	if (op == OA_ERROR) {
		for (int i = 0; i < ERR_MAX; i++)
			if (errs[i].pid == pid_cour)
				b_str(&out, errs[i].text);
		b_add(&out, "", 0);
		r = reply_out(m, &out);
		b_free(&out);
		return r;
	}
	if (op == OA_LIST) {
		op_list(&out);
		r = reply_out(m, &out);
		b_free(&out);
		return r;
	}
	char name[NAME_MAX_];
	size_t nl = strnlen((const char *)m->data, sizeof m->data);
	if (nl >= sizeof name)
		return fail(E_NAMETOOLONG, "nom de compte trop long");
	memcpy(name, m->data, nl);
	name[nl] = 0;
	if (!name_ok(name))
		return fail(E_INVAL, "nom de compte invalide : « %s » (lettres, chiffres, . _ - / @)", name);
	struct account *a = find(name);
	if (op == OA_SETUP) {
		char *conf = user_text(m);
		if (!conf)
			return fail(E_INVAL, "%s : configuration absente", name);
		int is_new = !a;
		if (!a && !(a = add(name))) {
			free(conf);
			return fail(E_NOSPC, "trop de comptes (%d)", ACC_MAX);
		}
		r = op_setup(a, conf);
		OPENSSL_cleanse(conf, strlen(conf));
		free(conf);
		if (r && is_new) {
			memset(a, 0, sizeof *a);
			a->listen_fd = -1;
		}
		return r;
	}
	if (!a)
		return fail(E_NOENT, "compte inconnu : %s (oauth_setup)", name);
	switch (op) {
	case OA_STATUS: return op_status(a);
	case OA_DEVICE_POLL: return op_device_poll(a);
	case OA_BROWSER_POLL: return op_browser_poll(a);
	case OA_FORGET: return op_forget(a);
	case OA_REMOVE: return op_remove(a);
	case OA_BROWSER_FINISH: {
		char *reply = user_text(m);
		if (!reply)
			return fail(E_INVAL, "%s : adresse de retour absente", name);
		r = finish_with(a, reply);
		OPENSSL_cleanse(reply, strlen(reply));
		free(reply);
		return r;
	}
	case OA_DEVICE_BEGIN: r = op_device_begin(a, &out); break;
	case OA_BROWSER_BEGIN: r = op_browser_begin(a, m->a, &out); break;
	case OA_TOKEN: r = op_token(a, &out); break;
	case OA_INFO: r = op_info(a, &out); break;
	default: return E_INVAL;
	}
	if (r == 0)
		r = reply_out(m, &out);
	b_free(&out);
	return r;
}

int main(void)
{
	canal = ksys(SYS_SVC_REGISTER, (long)"oauth", SVF_ENDS, 0, 0);
	if (canal < 0) /* (déjà lancé : NetKit l'a lancé deux fois en même temps) */
		return 0;
	for (;;) {
		long req = ksys(SYS_SVC_NEXT, canal, (long)&msg, 60000, 0);
		if (req < 0)
			return 1;
		if (!req)
			continue;
		req_cour = req;
		u64 op = (u64)msg.op;
		if (op == SVC_OP_END) {
			for (int i = 0; i < ERR_MAX; i++)
				if (errs[i].pid == (u32)msg.a)
					errs[i].pid = 0;
			continue;
		}
		if (op == SVC_OP_FORK || op == SVC_OP_BIRTH)
			continue;
		pid_cour = msg.pid;
		msg.data[DATA_MAX - 1] = 0;
		long r = handle(&msg);
		memset(msg.data, 0, sizeof msg.data); /* (rien d'un compte ne repart dans la réponse) */
		msg.op = r;
		ksys(SYS_SVC_REPLY, req, (long)&msg, 0, 0);
	}
}
