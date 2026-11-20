// net.cpp -- des requêtes HTTP et HTTPS bloquantes (OpenSSL, HTTP/1.0), pour l'échange de licence des essais et
// la mise à jour du module (net.h).
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "net.h"

namespace {

struct Response
{
	int status = 0;
	std::string location;		// (une redirection)
	std::vector<uint8_t> body;
};

// une requête ; false : pas de réponse (message sur stderr)
bool request (const char *method, const std::string &url, const char *content_type, const std::vector<uint8_t> &body,
	      size_t max, Response &r)
{
	bool tls = !url.compare (0, 8, "https://");
	if (!tls && url.compare (0, 7, "http://"))
	{
		fprintf (stderr, "net : adresse http(s) seulement : %s\n", url.c_str ());
		return false;
	}
	std::string rest = url.substr (tls ? 8 : 7), host = rest, path = "/", port = tls ? "443" : "80";
	size_t sl = rest.find ('/');
	if (sl != std::string::npos)
	{
		host = rest.substr (0, sl);
		path = rest.substr (sl);
	}
	size_t colon = host.find (':');
	if (colon != std::string::npos)
	{
		port = host.substr (colon + 1);
		host = host.substr (0, colon);
	}
	struct addrinfo hints = {}, *res = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	// (le réseau peut ne pas être prêt : jusqu'à 90 s d'attente)
	for (int tries = 0; tries < 90 && (getaddrinfo (host.c_str (), port.c_str (), &hints, &res) || !res); tries++)
	{
		res = nullptr;
		sleep (1);
	}
	if (!res)
	{
		fprintf (stderr, "net : %s introuvable (réseau ?)\n", host.c_str ());
		return false;
	}
	int fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect (fd, res->ai_addr, res->ai_addrlen))
	{
		fprintf (stderr, "net : %s ne répond pas\n", host.c_str ());
		freeaddrinfo (res);
		if (fd >= 0)
			close (fd);
		return false;
	}
	freeaddrinfo (res);
	SSL_CTX *ctx = nullptr;
	SSL *ssl = nullptr;
	if (tls)
	{
		ctx = SSL_CTX_new (TLS_client_method ());
		SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION);
		SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, nullptr);
		SSL_CTX_set_default_verify_paths (ctx);
		ssl = SSL_new (ctx);
		SSL_set_fd (ssl, fd);
		SSL_set_tlsext_host_name (ssl, host.c_str ());
		SSL_set1_host (ssl, host.c_str ());
	}
	auto put = [&] (const void *p, int n) { return tls ? SSL_write (ssl, p, n) : (int) write (fd, p, (size_t) n); };
	auto get = [&] (void *p, int n) { return tls ? SSL_read (ssl, p, n) : (int) read (fd, p, (size_t) n); };
	bool ok = false;
	if (tls && SSL_connect (ssl) != 1)
		fprintf (stderr, "net : TLS avec %s impossible (vérification : %s)\n", host.c_str (),
			 X509_verify_cert_error_string (SSL_get_verify_result (ssl)));
	else
	{
		std::string head = std::string (method) + " " + path + " HTTP/1.0\r\nHost: " + host +
				   "\r\nUser-Agent: KorinOS-cdmhost/1\r\nConnection: close\r\n";
		if (content_type)
			head += std::string ("Content-Type: ") + content_type + "\r\nContent-Length: " + std::to_string (body.size ()) + "\r\n";
		head += "\r\n";
		std::vector<uint8_t> req (head.begin (), head.end ());
		req.insert (req.end (), body.begin (), body.end ());
		if (put (req.data (), (int) req.size ()) == (int) req.size ())
		{
			std::vector<uint8_t> raw;
			char b[16384];
			int k;
			bool big = false;
			while ((k = get (b, sizeof b)) > 0)
			{
				raw.insert (raw.end (), b, b + k);
				if (raw.size () > max + 65536)
				{
					big = true;
					break;
				}
			}
			std::string s (raw.begin (), raw.begin () + (long) std::min (raw.size (), (size_t) 65536));
			size_t he = s.find ("\r\n\r\n");
			if (big)
				fprintf (stderr, "net : réponse de %s trop grande\n", host.c_str ());
			else if (he != std::string::npos && s.compare (0, 5, "HTTP/") == 0)
			{
				r.status = atoi (s.c_str () + s.find (' ') + 1);
				// (l'en-tête Location d'une redirection)
				for (size_t at = s.find ("\r\n"); at < he; at = s.find ("\r\n", at + 2))
					if (!strncasecmp (s.c_str () + at + 2, "Location:", 9))
					{
						size_t v = at + 11, e = s.find ("\r\n", v);
						while (v < e && s[v] == ' ')
							v++;
						r.location = s.substr (v, e - v);
					}
				r.body.assign (raw.begin () + (long) he + 4, raw.end ());
				ok = true;
			}
		}
	}
	if (ssl)
		SSL_free (ssl);
	if (ctx)
		SSL_CTX_free (ctx);
	close (fd);
	return ok;
}

} // namespace

bool https_post (const std::string &url, const std::vector<uint8_t> &body, std::vector<uint8_t> &out, int *status,
		 const char *content_type)
{
	if (url.compare (0, 8, "https://"))
	{
		fprintf (stderr, "net : https seulement : %s\n", url.c_str ());
		return false;
	}
	Response r;
	if (!request ("POST", url, content_type, body, 16u << 20, r))
		return false;
	*status = r.status;
	out = std::move (r.body);
	return true;
}

bool http_get (const std::string &url, std::vector<uint8_t> &out, int *status, size_t max)
{
	std::string at = url;
	for (int hops = 0; hops <= 5; hops++)
	{
		Response r;
		if (!request ("GET", at, nullptr, {}, max, r))
			return false;
		if (r.status >= 300 && r.status < 400 && !r.location.empty ())
		{
			// (une adresse relative : sur le même hôte)
			if (r.location[0] == '/')
			{
				size_t h = at.find ("://");
				size_t p = at.find ('/', h + 3);
				r.location = at.substr (0, p) + r.location;
			}
			at = r.location;
			continue;
		}
		*status = r.status;
		out = std::move (r.body);
		return true;
	}
	fprintf (stderr, "net : trop de redirections : %s\n", url.c_str ());
	return false;
}
