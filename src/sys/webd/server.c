/*
 * server.c -- webd : les ports en écoute, une connexion par thread, HTTP/1.1 (GET, HEAD, OPTIONS) pour les fichiers
 * statiques : hôtes virtuels (Host), garder la connexion, ETag et Last-Modified (304), plages d'octets (206, vidéos),
 * page d'index, liste d'un dossier, 404.html du site. Instance de WebStudio (--dev) : pas de cache, un script de
 * rechargement ajouté aux pages HTML et /__webstudio/reload (événements serveur).
 *
 * Le réseau de Korin (docs/POSIX.md, « Réseau ») : IPv4 ; netd ne lie que des ports (une liaison à 127.0.0.1 écoute
 * aussi le réseau) et accept ne dit pas d'où vient la connexion. poll sur un socket : boucle de libposix (1 ms).
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <openssl/err.h>
#include "webd.h"

volatile int g_quit;
volatile unsigned g_reload_gen;
pthread_mutex_t g_reload_mu = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_reload_cv = PTHREAD_COND_INITIALIZER;
int g_dev;
int g_dev_lan;
static int g_nconns;

#define HEAD_MAX	16384
#define CHUNK		65536
#define IDLE_MS		15000		/* attente de la requête suivante d'une connexion gardée */
#define PART_MS		10000		/* attente de la suite d'une requête commencée */
#define BODY_MAX	(1 << 20)	/* corps de requête jeté au plus (au-delà : la connexion se ferme) */

static const char reload_script[] =
	"<script>(function(){var e=new EventSource(\"/__webstudio/reload\");"
	"e.onmessage=function(){location.reload()};})();</script>\n";

void conf_ref (struct conf *c)
{
	__atomic_add_fetch (&c->refs, 1, __ATOMIC_SEQ_CST);
}

void conf_unref (struct conf *c)
{
	if (__atomic_sub_fetch (&c->refs, 1, __ATOMIC_SEQ_CST) == 0) {
		webtls_free (c);
		free (c);
	}
}

void reload_pages (void)
{
	pthread_mutex_lock (&g_reload_mu);
	g_reload_gen++;
	pthread_cond_broadcast (&g_reload_cv);
	pthread_mutex_unlock (&g_reload_mu);
}

/* ---- une connexion ---- */

struct conn {
	int fd;
	SSL *ssl;
	struct listener *l;
	int port, scheme;		/* (copiés de l : le tableau des ports est refait au rechargement) */
	SSL_CTX *ctx;
	struct conf *conf;
	char buf[HEAD_MAX + 1];
	int len;
};

struct req {
	struct conn *c;
	struct site *site;
	char method[16];
	char target[2048];
	char path[2048];		/* décodé */
	char host[256];
	char range[128];
	char inm[256];			/* If-None-Match */
	char ims[64];			/* If-Modified-Since */
	long long clen;
	int chunked;
	int keep;
	int head;			/* HEAD : pas de corps */
	int status;
	long long sent;			/* octets du corps envoyés */
	const char *note;
};

/* v contient-il mot (sans casse) ? */
static int has_word (const char *v, const char *mot)
{
	size_t n = strlen (mot);
	for (; *v; v++)
		if (!strncasecmp (v, mot, n))
			return 1;
	return 0;
}

static int conn_read (struct conn *c, char *b, int n, int ms)
{
	if (!(c->ssl && SSL_pending (c->ssl))) {
		struct pollfd p = { c->fd, POLLIN, 0 };
		if (poll (&p, 1, ms) <= 0)
			return -1;
	}
	int r = c->ssl ? SSL_read (c->ssl, b, n) : (int) recv (c->fd, b, (size_t) n, 0);
	return r > 0 ? r : -1;
}

static int conn_write (struct conn *c, const void *b, size_t n)
{
	const char *p = b;
	while (n > 0) {
		int r = c->ssl ? SSL_write (c->ssl, p, (int) (n > CHUNK ? CHUNK : n)) : (int) send (c->fd, p, n, 0);
		if (r <= 0)
			return -1;
		p += r;
		n -= (size_t) r;
	}
	return 0;
}

static const char *reason (int s)
{
	switch (s) {
	case 200: return "OK";
	case 204: return "No Content";
	case 206: return "Partial Content";
	case 301: return "Moved Permanently";
	case 304: return "Not Modified";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 411: return "Length Required";
	case 413: return "Payload Too Large";
	case 416: return "Range Not Satisfiable";
	case 431: return "Request Header Fields Too Large";
	case 500: return "Internal Server Error";
	case 503: return "Service Unavailable";
	}
	return "Unknown";
}

/* l'en-tête de la réponse ; clen < 0 : pas de Content-Length */
static int send_head (struct req *rq, int status, const char *ctype, long long clen, const char *extra)
{
	char h[3072], date[64];
	rq->status = status;
	http_date (time (NULL), date, sizeof date);
	int n = snprintf (h, sizeof h, "HTTP/1.1 %d %s\r\nServer: " WEBD_SERVER "\r\nDate: %s\r\n", status, reason (status),
		date);
	if (ctype)
		n += snprintf (h + n, sizeof h - (size_t) n, "Content-Type: %s\r\n", ctype);
	if (clen >= 0)
		n += snprintf (h + n, sizeof h - (size_t) n, "Content-Length: %lld\r\n", clen);
	if (rq->site && rq->site->dev)
		n += snprintf (h + n, sizeof h - (size_t) n, "Cache-Control: no-store\r\n");
	if (extra)
		n += snprintf (h + n, sizeof h - (size_t) n, "%s", extra);
	n += snprintf (h + n, sizeof h - (size_t) n, "Connection: %s\r\n\r\n", rq->keep ? "keep-alive" : "close");
	return conn_write (rq->c, h, (size_t) n);
}

static int send_body (struct req *rq, const void *b, size_t n)
{
	if (rq->head)
		return 0;
	rq->sent += (long long) n;
	return conn_write (rq->c, b, n);
}

static int serve_file (struct req *rq, const char *fs, int status);

static int send_error (struct req *rq, int status, const char *detail)
{
	/* la page 404.html du site, s'il en a une */
	if (status == 404 && rq->site) {
		char p[700];
		snprintf (p, sizeof p, "%s/404.html", rq->site->root);
		struct stat st;
		if (stat (p, &st) == 0 && S_ISREG (st.st_mode))
			return serve_file (rq, p, 404);
	}
	char body[1024];
	int n = snprintf (body, sizeof body,
		"<!doctype html><html lang=\"fr\"><meta charset=\"utf-8\"><title>%d %s</title>"
		"<body style=\"font-family:sans-serif;margin:3em\"><h1>%d %s</h1><p>%s</p><hr><small>" WEBD_SERVER
		"</small></body></html>\n", status, reason (status), status, reason (status), detail ? detail : "");
	const char *extra = status == 405 ? "Allow: GET, HEAD, OPTIONS\r\n" : NULL;
	if (send_head (rq, status, "text/html; charset=utf-8", n, extra) < 0)
		return -1;
	return send_body (rq, body, (size_t) n);
}

/* ---- les fichiers ---- */

/* « bytes=a-b », « bytes=a- », « bytes=-n » -> 1 et [*from, *to], 0 : pas de plage utilisable (tout le fichier), -1 :
 * hors du fichier (416) */
static int parse_range (const char *r, long long size, long long *from, long long *to)
{
	if (strncasecmp (r, "bytes=", 6) || strchr (r, ','))
		return 0;
	r += 6;
	char *end;
	if (*r == '-') {
		long long n = strtoll (r + 1, &end, 10);
		if (n <= 0)
			return -1;
		*from = n >= size ? 0 : size - n;
		*to = size - 1;
	} else {
		*from = strtoll (r, &end, 10);
		if (*end != '-')
			return 0;
		*to = end[1] ? strtoll (end + 1, NULL, 10) : size - 1;
		if (*to >= size)
			*to = size - 1;
	}
	if (*from < 0 || *from >= size || *to < *from)
		return -1;
	return 1;
}

/* l'indice de la dernière « </body> » (sans casse) -> son décalage, ou len */
static size_t body_end (const char *b, size_t len)
{
	for (size_t i = len; i >= 7; i--)
		if (b[i - 7] == '<' && !strncasecmp (b + i - 7, "</body>", 7))
			return i - 7;
	return len;
}

static int serve_file (struct req *rq, const char *fs, int status)
{
	struct stat st;
	if (stat (fs, &st) != 0)
		return send_error (rq, 404, "Le fichier n'existe pas.");
	long long size = (long long) st.st_size;
	char etag[64], lastmod[64], extra[512];
	snprintf (etag, sizeof etag, "\"%llx-%llx\"", size, (long long) st.st_mtime);
	http_date (st.st_mtime, lastmod, sizeof lastmod);
	const char *ctype = mime_type (fs);
	int html = !strncmp (ctype, "text/html", 9);

	if (status == 200 && !(rq->site && rq->site->dev) &&
		((*rq->inm && strstr (rq->inm, etag)) || (!*rq->inm && *rq->ims && !strcmp (rq->ims, lastmod)))) {
		snprintf (extra, sizeof extra, "ETag: %s\r\n", etag);
		return send_head (rq, 304, NULL, -1, extra);
	}

	int fd = open (fs, O_RDONLY);
	if (fd < 0)
		return send_error (rq, 403, "Le fichier ne peut pas être lu.");

	/* WebStudio : le script de rechargement avant </body> (la page entière en mémoire) */
	if (rq->site && rq->site->dev && html && size < 8 * 1024 * 1024) {
		size_t sl = sizeof reload_script - 1;
		char *b = malloc ((size_t) size + sl + 1);
		long long got = 0;
		while (b && got < size) {
			ssize_t r = read (fd, b + got, (size_t) (size - got));
			if (r <= 0)
				break;
			got += r;
		}
		close (fd);
		if (!b)
			return send_error (rq, 500, "Mémoire insuffisante.");
		size_t at = body_end (b, (size_t) got);
		memmove (b + at + sl, b + at, (size_t) got - at);
		memcpy (b + at, reload_script, sl);
		int r = send_head (rq, status, ctype, got + (long long) sl, NULL);
		if (r == 0)
			r = send_body (rq, b, (size_t) got + sl);
		free (b);
		return r;
	}

	long long from = 0, to = size - 1;
	int partial = 0;
	if (status == 200 && *rq->range && (!*rq->inm || strstr (rq->inm, etag))) {
		partial = parse_range (rq->range, size, &from, &to);
		if (partial < 0) {
			close (fd);
			snprintf (extra, sizeof extra, "Content-Range: bytes */%lld\r\n", size);
			return send_head (rq, 416, NULL, 0, extra);
		}
	}
	int n = snprintf (extra, sizeof extra, "ETag: %s\r\nLast-Modified: %s\r\nAccept-Ranges: bytes\r\n", etag, lastmod);
	if (partial) {
		snprintf (extra + n, sizeof extra - (size_t) n, "Content-Range: bytes %lld-%lld/%lld\r\n", from, to, size);
		status = 206;
	}
	if (send_head (rq, status, ctype, size ? to - from + 1 : 0, extra) < 0 || rq->head || size == 0) {
		close (fd);
		return 0;
	}
	if (from && lseek (fd, (off_t) from, SEEK_SET) != (off_t) from) {
		close (fd);
		rq->keep = 0;
		return -1;
	}
	char *chunk = malloc (CHUNK);
	long long left = to - from + 1;
	int r = chunk ? 0 : -1;
	while (r == 0 && left > 0) {
		ssize_t got = read (fd, chunk, (size_t) (left > CHUNK ? CHUNK : left));
		if (got <= 0) {
			r = -1;
			break;
		}
		r = send_body (rq, chunk, (size_t) got);
		left -= got;
	}
	free (chunk);
	close (fd);
	if (r < 0)
		rq->keep = 0;
	return r;
}

static int serve_listing (struct req *rq, const char *fs)
{
	DIR *d = opendir (fs);
	if (!d)
		return send_error (rq, 403, "Le dossier ne peut pas être lu.");
	size_t cap = 16384, len = 0;
	char *b = malloc (cap);
	char esc[1200], line[3000];
	html_escape (rq->path, esc, sizeof esc);
	len = (size_t) snprintf (b, cap, "<!doctype html><html lang=\"fr\"><meta charset=\"utf-8\"><title>%s</title>"
		"<body style=\"font-family:sans-serif;margin:2em\"><h1>%s</h1><table cellpadding=\"4\">\n"
		"<tr><td><a href=\"../\">../</a></td><td></td></tr>\n", esc, esc);
	struct dirent *e;
	while (b && (e = readdir (d))) {
		if (e->d_name[0] == '.')
			continue;
		char full[3000];
		struct stat st;
		snprintf (full, sizeof full, "%s/%s", fs, e->d_name);
		int dir = stat (full, &st) == 0 && S_ISDIR (st.st_mode);
		html_escape (e->d_name, esc, sizeof esc);
		int n = dir ? snprintf (line, sizeof line, "<tr><td><a href=\"%s/\">%s/</a></td><td></td></tr>\n", esc, esc)
			: snprintf (line, sizeof line, "<tr><td><a href=\"%s\">%s</a></td><td align=\"right\">%lld</td></tr>\n",
				esc, esc, (long long) st.st_size);
		if (len + (size_t) n + 64 > cap) {
			char *nb = realloc (b, cap *= 2);
			if (!nb) {
				free (b);
				b = NULL;
				break;
			}
			b = nb;
		}
		memcpy (b + len, line, (size_t) n);
		len += (size_t) n;
	}
	closedir (d);
	if (!b)
		return send_error (rq, 500, "Mémoire insuffisante.");
	len += (size_t) snprintf (b + len, cap - len, "</table></body></html>\n");
	int r = send_head (rq, 200, "text/html; charset=utf-8", (long long) len, NULL);
	if (r == 0)
		r = send_body (rq, b, len);
	free (b);
	return r;
}

/* /__webstudio/reload : événements serveur ; un « reload » à l'enregistrement suivant, puis la connexion se ferme */
static int serve_events (struct req *rq)
{
	rq->keep = 0;
	unsigned gen0 = g_reload_gen;
	if (send_head (rq, 200, "text/event-stream", -1, NULL) < 0 || send_body (rq, "retry: 1000\n\n", 13) < 0)
		return -1;
	for (;;) {
		pthread_mutex_lock (&g_reload_mu);
		struct timespec ts;
		clock_gettime (CLOCK_REALTIME, &ts);
		ts.tv_sec += 15;
		while (g_reload_gen == gen0 && !g_quit && !rq->c->l->stop)
			if (pthread_cond_timedwait (&g_reload_cv, &g_reload_mu, &ts) == ETIMEDOUT)
				break;
		unsigned gen = g_reload_gen;
		pthread_mutex_unlock (&g_reload_mu);
		if (g_quit || rq->c->l->stop)
			return 0;
		if (gen != gen0) {
			rq->note = "rechargement";
			return send_body (rq, "data: reload\n\n", 14);
		}
		if (send_body (rq, ": ping\n\n", 8) < 0)
			return -1;
	}
}

/* ---- une requête ---- */

static struct site *pick_site (struct req *rq)
{
	struct conf *c = rq->c->conf;
	struct conn *l = rq->c;
	struct site *any = NULL, *first = NULL;
	for (int i = 0; i < c->nsites; i++) {
		struct site *s = &c->sites[i];
		if (!s->enabled)
			continue;
		for (int j = 0; j < s->nbinds; j++) {
			struct bind *b = &s->binds[j];
			if (b->port != l->port || b->scheme != l->scheme)
				continue;
			if (!strcmp (b->host, rq->host))
				return s;
			if (!any && !strcmp (b->host, "*"))
				any = s;
			if (!first)
				first = s;
		}
	}
	return any ? any : first;
}

static void header_copy (char *dst, size_t n, const char *v)
{
	while (*v == ' ' || *v == '\t')
		v++;
	snprintf (dst, n, "%s", v);
}

/* -> la taille de l'en-tête lu, 0 : la connexion est finie, -1 : trop long */
static int read_head (struct conn *c)
{
	int first = 1;
	for (;;) {
		c->buf[c->len] = 0;
		char *e = strstr (c->buf, "\r\n\r\n");
		if (e)
			return (int) (e - c->buf) + 4;
		if (c->len >= HEAD_MAX)
			return -1;
		int r = conn_read (c, c->buf + c->len, HEAD_MAX - c->len, first && c->len == 0 ? IDLE_MS : PART_MS);
		if (r <= 0)
			return 0;
		c->len += r;
		first = 0;
	}
}

/* -> 1 : garder la connexion */
static int handle (struct conn *c)
{
	struct req rq;
	memset (&rq, 0, sizeof rq);
	rq.c = c;
	int hl = read_head (c);
	if (hl == 0)
		return 0;
	long t0 = now_ms ();
	if (hl < 0) {
		send_error (&rq, 431, "L'en-tête de la requête est trop long.");
		return 0;
	}
	/* la ligne de requête */
	char *line = c->buf, *eol = strstr (line, "\r\n");
	*eol = 0;
	char version[16] = "";
	if (sscanf (line, "%15s %2047s %15s", rq.method, rq.target, version) != 3 || strncmp (version, "HTTP/1.", 7)) {
		send_error (&rq, 400, "Requête mal formée.");
		return 0;
	}
	rq.keep = !strcmp (version, "HTTP/1.1");
	/* les en-têtes */
	for (char *h = eol + 2; h < c->buf + hl - 2;) {
		char *he = strstr (h, "\r\n");
		*he = 0;
		char *colon = strchr (h, ':');
		if (colon) {
			*colon = 0;
			const char *v = colon + 1;
			if (!strcasecmp (h, "Host"))
				header_copy (rq.host, sizeof rq.host, v);
			else if (!strcasecmp (h, "Connection")) {
				if (has_word (v, "close"))
					rq.keep = 0;
				else if (has_word (v, "keep-alive"))
					rq.keep = 1;
			} else if (!strcasecmp (h, "Range"))
				header_copy (rq.range, sizeof rq.range, v);
			else if (!strcasecmp (h, "If-None-Match"))
				header_copy (rq.inm, sizeof rq.inm, v);
			else if (!strcasecmp (h, "If-Modified-Since"))
				header_copy (rq.ims, sizeof rq.ims, v);
			else if (!strcasecmp (h, "Content-Length"))
				rq.clen = strtoll (v, NULL, 10);
			else if (!strcasecmp (h, "Transfer-Encoding"))
				rq.chunked = has_word (v, "chunked");
		}
		h = he + 2;
	}
	/* l'hôte : minuscules, sans port ni point final */
	for (char *p = rq.host; *p; p++) {
		if (*p == ':') {
			*p = 0;
			break;
		}
		if (*p >= 'A' && *p <= 'Z')
			*p += 32;
	}
	size_t hn = strlen (rq.host);
	if (hn && rq.host[hn - 1] == '.')
		rq.host[hn - 1] = 0;

	/* ce qui suit l'en-tête : le corps (jeté), puis peut-être la requête suivante */
	c->len -= hl;
	memmove (c->buf, c->buf + hl, (size_t) c->len);
	if (rq.chunked) {
		rq.keep = 0;
		send_error (&rq, 411, "Corps de requête découpé : non pris en charge.");
		goto done;
	}
	if (rq.clen > 0) {
		if (rq.clen > BODY_MAX)
			rq.keep = 0;
		long long left = rq.clen;
		long long take = left < c->len ? left : c->len;
		c->len -= (int) take;
		memmove (c->buf, c->buf + take, (size_t) c->len);
		left -= take;
		char sink[4096];
		while (rq.keep && left > 0) {
			int r = conn_read (c, sink, left > (long long) sizeof sink ? (int) sizeof sink : (int) left, PART_MS);
			if (r <= 0) {
				rq.keep = 0;
				break;
			}
			left -= r;
		}
	}

	rq.site = pick_site (&rq);
	rq.head = !strcmp (rq.method, "HEAD");
	if (!rq.site) {
		send_error (&rq, 404, "Aucun site sur cette adresse.");
		goto done;
	}
	if (!strcmp (rq.method, "OPTIONS")) {
		send_head (&rq, 204, NULL, -1, "Allow: GET, HEAD, OPTIONS\r\n");
		goto done;
	}
	if (strcmp (rq.method, "GET") && !rq.head) {
		send_error (&rq, 405, "Seuls GET et HEAD sont servis (fichiers statiques).");
		goto done;
	}
	if (rq.site->dev && !strcmp (rq.target, "/__webstudio/reload")) {
		snprintf (rq.path, sizeof rq.path, "%s", rq.target);
		serve_events (&rq);
		goto done;
	}
	if (url_decode_path (rq.target, rq.path, sizeof rq.path) < 0) {
		snprintf (rq.path, sizeof rq.path, "%.200s", rq.target);
		send_error (&rq, 404, "Ce chemin n'est pas servi.");
		goto done;
	}
	char fs[2600];
	snprintf (fs, sizeof fs, "%s%s", rq.site->root, rq.path);
	struct stat st;
	if (stat (fs, &st) != 0) {
		rq.note = "fichier absent";
		send_error (&rq, 404, "Cette page n'existe pas.");
		goto done;
	}
	if (S_ISDIR (st.st_mode)) {
		size_t pl = strlen (rq.path);
		if (rq.path[pl - 1] != '/') {
			char loc[2200];
			snprintf (loc, sizeof loc, "Location: %s/\r\n", rq.path);
			send_head (&rq, 301, NULL, 0, loc);
			goto done;
		}
		size_t fl = strlen (fs);
		for (int i = 0; i < rq.site->nindex; i++) {
			snprintf (fs + fl, sizeof fs - fl, "%s", rq.site->index[i]);
			if (stat (fs, &st) == 0 && S_ISREG (st.st_mode)) {
				serve_file (&rq, fs, 200);
				goto done;
			}
		}
		fs[fl] = 0;
		if (rq.site->listing || rq.site->dev)
			serve_listing (&rq, fs);
		else
			send_error (&rq, 403, "Ce dossier n'a pas de page d'index.");
		goto done;
	}
	serve_file (&rq, fs, 200);
done:
	log_request (rq.site ? rq.site->name : "-", rq.method, *rq.path ? rq.path : rq.target, rq.status, rq.sent,
		now_ms () - t0, rq.note);
	return rq.keep;
}

static void *conn_thread (void *arg)
{
	struct conn *c = arg;
	int ok = 1;
	if (c->scheme == SCHEME_HTTPS) {
		c->ssl = SSL_new (c->ctx);
		if (!c->ssl || SSL_set_fd (c->ssl, c->fd) != 1 || SSL_accept (c->ssl) != 1) {
			ok = 0;
			ERR_clear_error ();
		}
	}
	for (int n = 0; ok && n < 1000 && !g_quit && !c->l->stop; n++)
		if (!handle (c))
			break;
	if (c->ssl) {
		if (ok)
			SSL_shutdown (c->ssl);
		SSL_free (c->ssl);
	}
	close (c->fd);
	conf_unref (c->conf);
	free (c);
	__atomic_sub_fetch (&g_nconns, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static void *accept_thread (void *arg)
{
	struct listener *l = arg;
	while (!l->stop && !g_quit) {
		struct pollfd p = { l->fd, POLLIN, 0 };
		if (poll (&p, 1, 500) <= 0)
			continue;
		int fd = accept (l->fd, NULL, NULL);
		if (fd < 0)
			continue;
		if (__atomic_add_fetch (&g_nconns, 1, __ATOMIC_SEQ_CST) > MAX_CONNS) {
			__atomic_sub_fetch (&g_nconns, 1, __ATOMIC_SEQ_CST);
			close (fd);
			continue;
		}
		struct conn *c = calloc (1, sizeof *c);
		if (!c) {
			__atomic_sub_fetch (&g_nconns, 1, __ATOMIC_SEQ_CST);
			close (fd);
			continue;
		}
		c->fd = fd;
		c->l = l;
		c->port = l->port;
		c->scheme = l->scheme;
		c->ctx = l->ctx;
		c->conf = l->conf;
		conf_ref (c->conf);
		pthread_t th;
		pthread_attr_t at;
		pthread_attr_init (&at);
		pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize (&at, 256 * 1024);
		if (pthread_create (&th, &at, conn_thread, c) != 0) {
			conf_unref (c->conf);
			close (fd);
			free (c);
			__atomic_sub_fetch (&g_nconns, 1, __ATOMIC_SEQ_CST);
		}
		pthread_attr_destroy (&at);
	}
	return NULL;
}

static int open_port (int port, int loopback)
{
	int fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	int one = 1;
	setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons ((uint16_t) port);
	a.sin_addr.s_addr = htonl (loopback ? INADDR_LOOPBACK : INADDR_ANY);
	if (bind (fd, (struct sockaddr *) &a, sizeof a) != 0 || listen (fd, 64) != 0) {
		close (fd);
		return -1;
	}
	return fd;
}

/* les ports des sites actifs, ouverts, puis TLS, puis un thread d'écoute chacun. dev_auto : le premier port libre à
 * partir de celui de la liaison (--dev), écrit dans la liaison. -> le nombre de ports ouverts */
int listeners_start (struct conf *c, struct listener *ls, int *nls, int dev_auto)
{
	*nls = 0;
	for (int i = 0; i < c->nsites; i++) {
		struct site *s = &c->sites[i];
		if (!s->enabled)
			continue;
		for (int j = 0; j < s->nbinds; j++) {
			struct bind *b = &s->binds[j];
			struct listener *l = NULL;
			for (int k = 0; k < *nls; k++)
				if (ls[k].port == b->port)
					l = &ls[k];
			if (l) {
				if (l->scheme != b->scheme)
					log_msg ("le port %d ne peut pas servir http et https : liaison de « %s » ignorée", b->port, s->name);
				continue;
			}
			if (*nls >= MAX_LISTEN)
				continue;
			int fd = -1, port = b->port;
			for (int tries = dev_auto ? 100 : 1; tries > 0 && fd < 0; tries--, port++)
				fd = open_port (port, s->dev && !g_dev_lan);
			if (fd < 0) {
				log_msg ("port %d indisponible (déjà pris ?) : liaison de « %s » ignorée", b->port, s->name);
				continue;
			}
			b->port = port - 1;
			l = &ls[(*nls)++];
			memset (l, 0, sizeof *l);
			l->port = b->port;
			l->scheme = b->scheme;
			l->fd = fd;
			l->conf = c;
		}
	}
	if (webtls_conf (c, ls, *nls) < 0)
		log_msg ("TLS : des liaisons https sont sans certificat utilisable");
	for (int k = 0; k < *nls; k++) {
		if (ls[k].scheme == SCHEME_HTTPS && !ls[k].ctx) {
			close (ls[k].fd);
			ls[k].fd = -1;
			continue;
		}
		pthread_create (&ls[k].th, NULL, accept_thread, &ls[k]);
	}
	return *nls;
}

void listeners_stop (struct listener *ls, int nls)
{
	for (int k = 0; k < nls; k++)
		ls[k].stop = 1;
	pthread_mutex_lock (&g_reload_mu);
	pthread_cond_broadcast (&g_reload_cv);
	pthread_mutex_unlock (&g_reload_mu);
	for (int k = 0; k < nls; k++)
		if (ls[k].fd >= 0) {
			pthread_join (ls[k].th, NULL);
			close (ls[k].fd);
			ls[k].fd = -1;
		}
}
