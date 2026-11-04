//
// http.h -- PocketQI : une requête POST ou GET en HTTP ou en HTTPS (OpenSSL, le seul TLS de l'OS ; le certificat vérifié
// contre le magasin du système, /etc/ca-bundle), bloquante, faite par le fil du cerveau (brain.h) : la fenêtre ne
// l'attend jamais. Le transport est celui de Courier (src/apps/courier/net.h) : une socket non bloquante attendue par
// poll, pour que le délai et l'abandon tiennent à chaque étape. La réponse est lue jusqu'à la fermeture
// (« Connection: close »), puis décodée si elle est en morceaux (chunked).
//
// Les en-têtes envoyés (la clé d'API en fait partie) ne sont jamais écrits nulle part : ni journal, ni sortie.
//
#ifndef POCKETQI_HTTP_H
#define POCKETQI_HTTP_H

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <algorithm>
#include <string>

namespace pq {

static inline unsigned now_ms ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

// une adresse découpée : http(s)://hôte[:port]/chemin
struct Url
{
	bool https = false;
	std::string host, path = "/";
	unsigned port = 80;
	bool parse (const std::string &u)
	{
		size_t p;
		if (u.compare (0, 8, "https://") == 0) { https = true; port = 443; p = 8; }
		else if (u.compare (0, 7, "http://") == 0) { https = false; port = 80; p = 7; }
		else return false;
		size_t slash = u.find ('/', p);
		std::string hp = u.substr (p, slash == std::string::npos ? std::string::npos : slash - p);
		path = slash == std::string::npos ? "/" : u.substr (slash);
		size_t colon = hp.rfind (':');
		if (colon != std::string::npos) { port = (unsigned) atoi (hp.c_str () + colon + 1); hp.resize (colon); }
		host = hp;
		return !host.empty () && port > 0 && port < 65536;
	}
};

struct Conn
{
	int sock = -1;
	SSL *ssl = 0;
	volatile int *cancel = 0;
	bool cancelled () const { return cancel && *cancel; }
	void wait (short what) { struct pollfd p; p.fd = sock; p.events = what; p.revents = 0; poll (&p, 1, 40); }
	void close_ ()
	{
		if (ssl) { SSL_shutdown (ssl); SSL_free (ssl); ssl = 0; }
		if (sock >= 0) close (sock);
		sock = -1;
	}
	~Conn () { close_ (); }
};

// le seul contexte TLS, fait à la première requête (il lit les racines du système)
static SSL_CTX *tls_ctx ()
{
	static SSL_CTX *c;
	if (!c)
	{
		c = SSL_CTX_new (TLS_client_method ());
		if (c) { SSL_CTX_set_min_proto_version (c, TLS1_2_VERSION); SSL_CTX_set_default_verify_paths (c); }
	}
	return c;
}

static bool conn_open (Conn &c, const Url &u, int timeoutMs, std::string &err)
{
	char ps[12]; snprintf (ps, sizeof ps, "%u", u.port);
	struct addrinfo hints, *res = 0;
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo (u.host.c_str (), ps, &hints, &res) != 0 || !res) { err = "hôte inconnu : " + u.host; return false; }
	c.sock = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (c.sock < 0 || connect (c.sock, res->ai_addr, res->ai_addrlen) != 0)
	{
		freeaddrinfo (res);
		c.close_ ();
		err = "pas de connexion à " + u.host;
		return false;
	}
	freeaddrinfo (res);
	fcntl (c.sock, F_SETFL, fcntl (c.sock, F_GETFL, 0) | O_NONBLOCK);
	if (!u.https) return true;
	SSL_CTX *ctx = tls_ctx ();
	c.ssl = ctx ? SSL_new (ctx) : 0;
	if (!c.ssl) { err = "TLS indisponible"; return false; }
	SSL_set_fd (c.ssl, c.sock);
	struct in_addr ip;
	if (inet_pton (AF_INET, u.host.c_str (), &ip) != 1) { SSL_set_tlsext_host_name (c.ssl, u.host.c_str ()); SSL_set1_host (c.ssl, u.host.c_str ()); }
	else X509_VERIFY_PARAM_set1_ip_asc (SSL_get0_param (c.ssl), u.host.c_str ());
	SSL_set_verify (c.ssl, SSL_VERIFY_PEER, 0);
	unsigned t0 = now_ms ();
	for (;;)
	{
		if (c.cancelled ()) return false;
		ERR_clear_error ();
		int r = SSL_connect (c.ssl);
		if (r == 1) return true;
		int e = SSL_get_error (c.ssl, r);
		if (e == SSL_ERROR_WANT_READ) c.wait (POLLIN);
		else if (e == SSL_ERROR_WANT_WRITE) c.wait (POLLOUT);
		else
		{
			long v = SSL_get_verify_result (c.ssl);
			err = v != X509_V_OK ? std::string ("certificat refusé (") + X509_verify_cert_error_string (v) + ")" : std::string ("échec de la poignée de main TLS");
			return false;
		}
		if ((int) (now_ms () - t0) > timeoutMs) { err = "TLS trop long"; return false; }
	}
}

static bool conn_send (Conn &c, const char *p, size_t left, int timeoutMs)
{
	unsigned t0 = now_ms ();
	while (left > 0)
	{
		if (c.cancelled ()) return false;
		int r;
		if (c.ssl)
		{
			ERR_clear_error ();
			r = SSL_write (c.ssl, p, (int) left);
			if (r <= 0)
			{
				int e = SSL_get_error (c.ssl, r);
				if (e == SSL_ERROR_WANT_READ) c.wait (POLLIN);
				else if (e == SSL_ERROR_WANT_WRITE) c.wait (POLLOUT);
				else return false;
			}
		}
		else
		{
			r = (int) send (c.sock, p, left, 0);
			if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) c.wait (POLLOUT);
			else if (r < 0) return false;
		}
		if (r > 0) { p += r; left -= (size_t) r; t0 = now_ms (); }
		else if ((int) (now_ms () - t0) > timeoutMs) return false;
	}
	return true;
}

// > 0 : octets ; 0 : rien encore ; < 0 : fermée
static int conn_recv (Conn &c, char *b, int cap)
{
	short want = POLLIN;
	if (c.ssl)
	{
		ERR_clear_error ();
		int r = SSL_read (c.ssl, b, cap);
		if (r > 0) return r;
		int e = SSL_get_error (c.ssl, r);
		if (e == SSL_ERROR_WANT_WRITE) want = POLLOUT;
		else if (e != SSL_ERROR_WANT_READ) return -1;
	}
	else
	{
		int r = (int) recv (c.sock, b, (size_t) cap, MSG_DONTWAIT);
		if (r > 0) return r;
		if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return -1;
	}
	c.wait (want);
	return 0;
}

// le corps en morceaux (chunked) remis bout à bout
static std::string dechunk (const std::string &b)
{
	std::string o;
	size_t p = 0;
	while (p < b.size ())
	{
		size_t eol = b.find ("\r\n", p);
		if (eol == std::string::npos) break;
		unsigned long n = strtoul (b.c_str () + p, 0, 16);
		if (n == 0) break;
		p = eol + 2;
		if (p + n > b.size ()) n = b.size () - p;
		o.append (b, p, n);
		p += n + 2;
	}
	return o;
}

// ce qui est arrivé jusqu'ici (en-têtes et morceaux compris, brut), à chaque réception ; false : assez, la requête s'arrête
typedef bool (*HttpProgress) (void *ctx, const std::string &all);

// POST url avec ce JSON (GET : get vrai, sans corps) : -> le code HTTP (0 : pas de réponse, err dit pourquoi) ; body :
// le corps de la réponse ; progress : la réponse suivie à mesure (la voix la joue avant la fin)
static int http_request (bool get, const std::string &url, const std::string &headers, const std::string &json, std::string &body,
			 std::string &err, volatile int *cancel, int timeoutMs, HttpProgress progress = 0, void *ctx = 0)
{
	Url u;
	body.clear ();
	if (!u.parse (url)) { err = "adresse invalide"; return 0; }
	Conn c;
	c.cancel = cancel;
	if (!conn_open (c, u, timeoutMs, err)) return 0;
	std::string req = (get ? "GET " : "POST ") + u.path + " HTTP/1.1\r\nHost: " + u.host;
	if ((u.https && u.port != 443) || (!u.https && u.port != 80)) req += ":" + std::to_string (u.port);
	req += "\r\nUser-Agent: PocketQI/1.0 (Korin OS)\r\nAccept: application/json\r\n";
	if (!get) req += "Content-Type: application/json\r\nContent-Length: " + std::to_string (json.size ()) + "\r\n";
	req += "Connection: close\r\n" + headers + "\r\n";
	if (!get) req += json;
	bool sent = conn_send (c, req.data (), req.size (), timeoutMs);
	std::fill (req.begin (), req.end (), 0);		// (la clé d'API ne traîne pas en mémoire)
	if (!sent) { err = "envoi interrompu"; return 0; }
	std::string all;
	char buf[8192];
	unsigned t0 = now_ms ();
	for (;;)
	{
		if (c.cancelled ()) { err = "abandon"; return 0; }
		int r = conn_recv (c, buf, sizeof buf);
		if (r < 0) break;
		if (r > 0)
		{
			all.append (buf, (size_t) r);
			t0 = now_ms ();
			if (all.size () > (16u << 20)) break;
			if (progress && !progress (ctx, all)) break;
		}
		else if ((int) (now_ms () - t0) > timeoutMs) { err = "le service ne répond pas"; return 0; }
		// fin connue par Content-Length : inutile d'attendre la fermeture
		size_t he = all.find ("\r\n\r\n");
		if (he != std::string::npos)
		{
			std::string head = all.substr (0, he);
			for (auto &ch : head) ch = (char) tolower ((unsigned char) ch);
			size_t cl = head.find ("content-length:");
			if (cl != std::string::npos && all.size () - he - 4 >= strtoul (head.c_str () + cl + 15, 0, 10)) break;
		}
	}
	size_t he = all.find ("\r\n\r\n");
	if (all.compare (0, 5, "HTTP/") != 0 || he == std::string::npos) { err = "réponse illisible"; return 0; }
	int code = atoi (all.c_str () + all.find (' ') + 1);
	std::string head = all.substr (0, he);
	for (auto &ch : head) ch = (char) tolower ((unsigned char) ch);
	body = all.substr (he + 4);
	if (head.find ("transfer-encoding: chunked") != std::string::npos) body = dechunk (body);
	return code;
}
static int http_post (const std::string &url, const std::string &headers, const std::string &json, std::string &body,
		      std::string &err, volatile int *cancel, int timeoutMs = 60000)
{
	return http_request (false, url, headers, json, body, err, cancel, timeoutMs);
}
static int http_get (const std::string &url, std::string &body, std::string &err, volatile int *cancel, int timeoutMs = 20000)
{
	return http_request (true, url, "", "", body, err, cancel, timeoutMs);
}

} // namespace pq

#endif
