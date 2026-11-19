// net.cpp -- une requête HTTPS POST bloquante (OpenSSL), pour l'échange de licence des essais (net.h).
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "net.h"

// ---- HTTPS POST (OpenSSL, bloquant) : le corps de la réponse, statut HTTP dans *status ----
bool https_post (const std::string &url, const std::vector<uint8_t> &body, std::vector<uint8_t> &out, int *status)
{
	if (url.compare (0, 8, "https://"))
	{
		fprintf (stderr, "cdmtest : https seulement : %s\n", url.c_str ());
		return false;
	}
	std::string rest = url.substr (8), host = rest, path = "/";
	size_t sl = rest.find ('/');
	if (sl != std::string::npos)
	{
		host = rest.substr (0, sl);
		path = rest.substr (sl);
	}
	struct addrinfo hints = {}, *res = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	// (le réseau peut ne pas être prêt : jusqu'à 90 s d'attente)
	for (int tries = 0; tries < 90 && (getaddrinfo (host.c_str (), "443", &hints, &res) || !res); tries++)
	{
		res = nullptr;
		sleep (1);
	}
	if (!res)
	{
		fprintf (stderr, "cdmtest : %s introuvable (réseau ?)\n", host.c_str ());
		return false;
	}
	int fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect (fd, res->ai_addr, res->ai_addrlen))
	{
		fprintf (stderr, "cdmtest : %s ne répond pas\n", host.c_str ());
		freeaddrinfo (res);
		if (fd >= 0)
			close (fd);
		return false;
	}
	freeaddrinfo (res);
	SSL_CTX *ctx = SSL_CTX_new (TLS_client_method ());
	SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION);
	SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, nullptr);
	SSL_CTX_set_default_verify_paths (ctx);
	SSL *ssl = SSL_new (ctx);
	SSL_set_fd (ssl, fd);
	SSL_set_tlsext_host_name (ssl, host.c_str ());
	SSL_set1_host (ssl, host.c_str ());
	bool ok = false;
	if (SSL_connect (ssl) != 1)
		fprintf (stderr, "cdmtest : TLS avec %s impossible (vérification : %s)\n", host.c_str (),
		     X509_verify_cert_error_string (SSL_get_verify_result (ssl)));
	else
	{
		char head[512];
		int n = snprintf (head, sizeof head,
				  "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: KorinOS-cdmtest/1\r\n"
				  "Content-Type: application/octet-stream\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
				  path.c_str (), host.c_str (), body.size ());
		std::vector<uint8_t> req (head, head + n);
		req.insert (req.end (), body.begin (), body.end ());
		if (SSL_write (ssl, req.data (), (int) req.size ()) == (int) req.size ())
		{
			std::vector<uint8_t> raw;
			char b[8192];
			int k;
			while ((k = SSL_read (ssl, b, sizeof b)) > 0)
				raw.insert (raw.end (), b, b + k);
			std::string s (raw.begin (), raw.end ());
			size_t he = s.find ("\r\n\r\n");
			if (he != std::string::npos && s.compare (0, 5, "HTTP/") == 0)
			{
				*status = atoi (s.c_str () + s.find (' ') + 1);
				out.assign (raw.begin () + (long) he + 4, raw.end ());
				ok = true;
			}
		}
	}
	SSL_free (ssl);
	SSL_CTX_free (ctx);
	close (fd);
	return ok;
}
