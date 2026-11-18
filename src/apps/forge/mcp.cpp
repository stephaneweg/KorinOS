// mcp.cpp -- Forge : le serveur MCP, transport HTTP (mcp.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "mcp.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace forge {
namespace mcp {

namespace {

struct Job {
	std::string in, out;
	bool done = false;
};

std::mutex g_mu;
std::condition_variable g_cv;
Job *g_job;			// le message en attente de la boucle de l'interface
pthread_t g_thread;
std::atomic<bool> g_run { false }, g_stop { false };
int g_listen = -1, g_port;
std::string g_token;
std::atomic<long> g_served { 0 };

bool sendAll (int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t k = send (fd, p, n, 0);
		if (k <= 0) {
			if (k < 0 && errno == EINTR) continue;
			return false;
		}
		p += k, n -= (size_t) k;
	}
	return true;
}

void reply (int fd, int code, const char *reason, const std::string &body, const char *type = "application/json", const char *extra = "")
{
	char h[512];
	int n = snprintf (h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n%s%s%sCache-Control: no-store\r\nConnection: close\r\n\r\n", code, reason,
			  body.size (), body.empty () ? "" : "Content-Type: ", body.empty () ? "" : type, body.empty () ? "" : "\r\n");
	std::string hd (h, (size_t) n);
	if (*extra) hd.insert (hd.size () - 2, extra);
	sendAll (fd, hd.data (), hd.size ());
	if (!body.empty ()) sendAll (fd, body.data (), body.size ());
}

// deux secrets comparés en un temps qui ne dit pas où ils diffèrent (le jeton)
static bool sameSecret (const std::string &a, const std::string &b)
{
	unsigned char d = a.size () != b.size ();
	for (size_t i = 0, n = a.size () < b.size () ? a.size () : b.size (); i < n; i++) d |= (unsigned char) (a[i] ^ b[i]);
	return !d;
}

// la valeur d'un en-tête (sans la casse du nom) dans le bloc des en-têtes -> "" si absent
std::string header (const std::string &hd, const char *name)
{
	size_t nl = strlen (name), at = hd.find ("\r\n");
	while (at != std::string::npos && at + 2 < hd.size ()) {
		size_t s = at + 2, e = hd.find ("\r\n", s);
		if (e == std::string::npos) e = hd.size ();
		if (e - s > nl && hd[s + nl] == ':' && !strncasecmp (hd.c_str () + s, name, nl)) {
			size_t v = s + nl + 1;
			while (v < e && (hd[v] == ' ' || hd[v] == '\t')) v++;
			return hd.substr (v, e - v);
		}
		at = e == hd.size () ? std::string::npos : e;
	}
	return "";
}

// une connexion : une requête, une réponse
void serve (int fd)
{
	std::string buf;
	char tmp[8192];
	size_t hend = std::string::npos;
	while (hend == std::string::npos) {
		struct pollfd p = { fd, POLLIN, 0 };
		if (poll (&p, 1, 10000) <= 0) return;
		ssize_t k = recv (fd, tmp, sizeof tmp, 0);
		if (k <= 0) return;
		buf.append (tmp, (size_t) k);
		hend = buf.find ("\r\n\r\n");
		if (buf.size () > 65536 && hend == std::string::npos) return reply (fd, 431, "Request Header Fields Too Large", "");
	}
	std::string hd = buf.substr (0, hend), body = buf.substr (hend + 4);
	std::string method = hd.substr (0, hd.find (' '));
	size_t p0 = hd.find (' ') + 1, p1 = hd.find (' ', p0);
	std::string path = p1 == std::string::npos ? "" : hd.substr (p0, p1 - p0);
	if (path.find ('?') != std::string::npos) path.erase (path.find ('?'));
	if (path != "/mcp" && path != "/mcp/" && path != "/") return reply (fd, 404, "Not Found", "");
	if (!header (hd, "Origin").empty ())	// (un navigateur : refusé, contre le rebond DNS)
		return reply (fd, 403, "Forbidden", "{\"error\":\"Origin refused\"}");
	if (!g_token.empty () && !sameSecret (header (hd, "Authorization"), "Bearer " + g_token))
		return reply (fd, 401, "Unauthorized", "{\"error\":\"bad or missing token (Authorization: Bearer ...)\"}");
	if (method == "GET" || method == "DELETE")	// (pas de flux SSE, pas de session à fermer)
		return reply (fd, 405, "Method Not Allowed", "", "application/json", "Allow: POST\r\n");
	if (method != "POST") return reply (fd, 405, "Method Not Allowed", "", "application/json", "Allow: POST\r\n");
	long want = atol (header (hd, "Content-Length").c_str ());
	if (want < 0 || want > 16 * 1024 * 1024) return reply (fd, 413, "Payload Too Large", "");
	while ((long) body.size () < want) {
		struct pollfd p = { fd, POLLIN, 0 };
		if (poll (&p, 1, 10000) <= 0) return;
		ssize_t k = recv (fd, tmp, sizeof tmp, 0);
		if (k <= 0) return;
		body.append (tmp, (size_t) k);
	}
	body.resize ((size_t) want);
	// à la boucle de l'interface
	Job job;
	job.in = body;
	{
		std::unique_lock<std::mutex> l (g_mu);
		g_cv.wait (l, [] { return !g_job || g_stop; });
		if (g_stop) return reply (fd, 503, "Service Unavailable", "");
		g_job = &job;
		if (!g_cv.wait_for (l, std::chrono::seconds (600), [&] { return job.done || g_stop; })) {
			if (g_job == &job) g_job = nullptr;
			g_cv.notify_all ();
			return reply (fd, 504, "Gateway Timeout", "");
		}
		if (!job.done) {
			if (g_job == &job) g_job = nullptr;
			return reply (fd, 503, "Service Unavailable", "");
		}
	}
	g_served++;
	if (job.out.empty ()) reply (fd, 202, "Accepted", "");
	else reply (fd, 200, "OK", job.out);
}

void *loop (void *)
{
	while (!g_stop) {
		struct pollfd p = { g_listen, POLLIN, 0 };
		if (poll (&p, 1, 250) <= 0) continue;
		struct sockaddr_in a;
		socklen_t al = sizeof a;
		int fd = accept (g_listen, (struct sockaddr *) &a, &al);
		if (fd < 0) continue;
		serve (fd);
		close (fd);
	}
	return nullptr;
}

} // namespace

bool start (int port, const std::string &token, std::string &err)
{
	if (g_run) return true;
	int s = socket (AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		err = "socket : " + std::string (strerror (errno));
		return false;
	}
	int one = 1;
	setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons ((unsigned short) port);
	a.sin_addr.s_addr = htonl (INADDR_ANY);
	if (bind (s, (struct sockaddr *) &a, sizeof a) < 0 || listen (s, 8) < 0) {
		err = "port " + std::to_string (port) + " : " + strerror (errno);
		close (s);
		return false;
	}
	g_listen = s, g_port = port, g_token = token;
	g_stop = false;
	if (pthread_create (&g_thread, nullptr, loop, nullptr) != 0) {
		err = "thread";
		close (s);
		g_listen = -1;
		return false;
	}
	g_run = true;
	return true;
}

void stop ()
{
	if (!g_run) return;
	{
		std::lock_guard<std::mutex> l (g_mu);
		g_stop = true;
	}
	g_cv.notify_all ();
	pthread_join (g_thread, nullptr);
	close (g_listen);
	g_listen = -1;
	g_run = false;
}

bool running () { return g_run; }
int port () { return g_port; }
long served () { return g_served; }

bool poll (const std::function<std::string (const std::string &)> &handler)
{
	Job *j;
	{
		std::lock_guard<std::mutex> l (g_mu);
		j = g_job;
		if (!j || j->done) return false;
	}
	std::string out = handler (j->in);	// (hors du verrou : le thread attend)
	{
		std::lock_guard<std::mutex> l (g_mu);
		j->out = out;
		j->done = true;
		g_job = nullptr;
	}
	g_cv.notify_all ();
	return true;
}

std::string base64 (const unsigned char *p, size_t n)
{
	static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string o;
	o.reserve ((n + 2) / 3 * 4);
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
		o += T[v >> 18 & 63];
		o += T[v >> 12 & 63];
		o += i + 1 < n ? T[v >> 6 & 63] : '=';
		o += i + 2 < n ? T[v & 63] : '=';
	}
	return o;
}

} // namespace mcp
} // namespace forge
