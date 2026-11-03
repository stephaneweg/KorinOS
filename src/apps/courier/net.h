//
// net.h -- Courier's HTTP engine: a request prepared (its variables resolved, its auth, its body made:
// raw, x-www-form-urlencoded, multipart form-data with files, a binary file; the cookies of the jar),
// sent on a thread of its own (the window keeps running, Cancel works) over POSIX sockets, or TLS by OpenSSL
// (the certificate checked against /etc/ca-bundle, unless the request's Settings say not to) for https://; the
// response read whole (any size, up to a limit), chunked and gzip / deflate decoded, the redirects followed
// (the cookies they set kept), timed (connect, first byte, total).
//
// Onyx's Apps/courier/net.h: the same logic; Transport, the thread and the inflating rewritten for ARMPI, the
// connection as in Mail's conn.h: a socket waited on by poll, so that a cancel and the time-out hold.
//
#ifndef _courier_net_h
#define _courier_net_h

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <netkit_abi.h>
#include <uikit.h>

// The words shown are in the system's language (ui_tr)
#define TR(s)	((const char *) ui_tr ((long) (s)))

#include "vars.h"

extern "C" char *stbi_zlib_decode_malloc_guesssize_headerflag (const char *buffer, int len, int initial_size, int *outlen, int parse_header);

namespace cr {

static const char *const USER_AGENT = "Courier/1.0 (ARMPI)";
enum { RESP_MAX = 16 << 20 };		// a response bigger than that is cut (truncated)
enum { HTTP_ERR_CONNECT = -2, HTTP_ERR_NET = -3, HTTP_ERR_TIMEOUT = -4, HTTP_ERR_SEND = -5, HTTP_ERR_EMPTY = -6, HTTP_ERR_TLS = -8 };

static inline unsigned now_ms ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

// ---- the cookie jar ------------------------------------------------------------------------------------------
struct Cookie
{
	Str domain, path, name, value;
	long long expires;			// unix seconds, 0: a session cookie
	bool secure, hostOnly, httpOnly;
	Cookie () : expires (0), secure (false), hostOnly (true), httpOnly (false) {}
};
struct CookieJar
{
	Vec<Cookie> list;
	bool dirty;
	CookieJar () : dirty (false) {}
	static bool domainMatch (const char *host, const Cookie &c)
	{
		if (c.hostOnly) return s_eqi (host, c.domain.c ());
		int hl = (int) strlen (host), dl = c.domain.len ();
		if (hl < dl) return false;
		if (!s_eqi (host + hl - dl, c.domain.c ())) return false;
		return hl == dl || host[hl - dl - 1] == '.';
	}
	static bool pathMatch (const char *path, const Cookie &c)
	{
		int pl = c.path.len ();
		if (pl == 0 || c.path.eq ("/")) return true;
		if (strncmp (path, c.path.c (), pl)) return false;
		return path[pl] == 0 || path[pl] == '/' || path[pl] == '?' || c.path.c ()[pl - 1] == '/';
	}
	// the Cookie header for a request (empty: none)
	void header (Str &out, const char *host, const char *path, bool https)
	{
		long long now = now_unix ();
		for (int i = 0; i < list.size (); i++)
		{
			const Cookie &c = list[i];
			if (c.expires && c.expires < now) continue;
			if (c.secure && !https) continue;
			if (!domainMatch (host, c) || !pathMatch (path, c)) continue;
			if (!out.empty ()) out.add ("; ");
			out.add (c.name.c ()); out.add ('='); out.add (c.value.c ());
		}
	}
	// a Set-Cookie header's value, from a response of host / path
	void set (const char *v, const char *host, const char *path, Cookie *parsed = 0)
	{
		Cookie c;
		const char *p = v;
		const char *e = p; while (*e && *e != ';') e++;
		const char *eq = p; while (eq < e && *eq != '=') eq++;
		if (eq >= e) return;
		s_trim (c.name, p, (int) (eq - p)); s_trim (c.value, eq + 1, (int) (e - eq - 1));
		c.domain = host;
		{ Str dp; const char *q = strchr (path, '?'); int n = q ? (int) (q - path) : (int) strlen (path);
		  dp.set (path, n); int sl = -1; for (int i = 0; i < dp.len (); i++) if (dp.c ()[i] == '/') sl = i;
		  if (sl <= 0) c.path = "/"; else c.path.set (dp.c (), sl); }
		long long maxAge = -1;
		while (*e == ';')
		{
			p = e + 1; while (*p == ' ') p++;
			e = p; while (*e && *e != ';') e++;
			const char *q = p; while (q < e && *q != '=') q++;
			Str k, val; s_trim (k, p, (int) (q - p)); if (q < e) s_trim (val, q + 1, (int) (e - q - 1));
			if (s_eqi (k.c (), "domain") && !val.empty ())
			{ const char *d = val.c (); if (*d == '.') d++; c.domain = d; c.hostOnly = false; }
			else if (s_eqi (k.c (), "path") && !val.empty ()) c.path = val;
			else if (s_eqi (k.c (), "secure")) c.secure = true;
			else if (s_eqi (k.c (), "httponly")) c.httpOnly = true;
			else if (s_eqi (k.c (), "max-age")) maxAge = atoll (val.c ());
			else if (s_eqi (k.c (), "expires")) c.expires = parse_http_date (val.c ());
		}
		if (maxAge >= 0) c.expires = maxAge == 0 ? 1 : now_unix () + maxAge;
		if (parsed) *parsed = c;
		for (int i = 0; i < list.size (); i++)
			if (list[i].name.eq (c.name.c ()) && s_eqi (list[i].domain.c (), c.domain.c ()) && list[i].path.eq (c.path.c ()))
			{
				if (c.expires && c.expires < now_unix ()) list.remove (i); else list[i] = c;
				dirty = true;
				return;
			}
		if (c.expires && c.expires < now_unix ()) return;
		list.push (c); dirty = true;
	}
	// "Wed, 21 Oct 2026 07:28:00 GMT" -> unix seconds (0: not understood)
	static long long parse_http_date (const char *s)
	{
		static const char *const MON[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
		int d = 0, y = 0, h = 0, mi = 0, se = 0, mo = -1;
		const char *p = s;
		while (*p && !(*p >= '0' && *p <= '9')) p++;
		d = atoi (p); while (*p >= '0' && *p <= '9') p++;
		while (*p == ' ' || *p == '-') p++;
		for (int i = 0; i < 12; i++) if (s_eqin (p, MON[i], 3)) mo = i + 1;
		while (*p && *p != ' ' && *p != '-') p++;
		while (*p == ' ' || *p == '-') p++;
		y = atoi (p); if (y < 100) y += y < 70 ? 2000 : 1900;
		while (*p >= '0' && *p <= '9') p++;
		while (*p == ' ') p++;
		sscanf (p, "%d:%d:%d", &h, &mi, &se);
		if (mo < 0 || !d || !y) return 0;
		return (long long) days_from_civil (y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
	}
};

// ---- a request ready to send, and what came back ---------------------------------------------------------------
struct Prepared
{
	Str method, url;
	KVList headers;			// what is sent (resolved; the defaults, the auth, the content type)
	char *body; int bodyLen;
	bool follow, insecure; int timeoutMs;
	Str error;			// could not be prepared (a file missing): not sent
	Prepared () : body (0), bodyLen (0), follow (true), insecure (false), timeoutMs (30000) {}
	~Prepared () { free (body); }
private:
	Prepared (const Prepared &); Prepared &operator= (const Prepared &);
};

struct Response
{
	int status;			// the HTTP status, or < 0: it failed (error says why)
	Str reason, error, httpVersion;
	KVList headers;
	char *body; int bodyLen;	// the body, decoded (NUL-terminated)
	int wireLen, headLen;		// bytes received (compressed), the headers' size
	int msTotal, msConnect, msFirst;
	Str finalUrl;
	Str console;			// the exchange as text: each hop's request and response headers
	Vec<Cookie> setCookies;		// the cookies this response set
	bool truncated, cancelled;
	int redirects;
	Response () : status (0), body (0), bodyLen (0), wireLen (0), headLen (0), msTotal (0), msConnect (0), msFirst (0),
		      truncated (false), cancelled (false), redirects (0) {}
	~Response () { free (body); }
	const char *header (const char *name) const
	{
		for (int i = 0; i < headers.size (); i++) if (s_eqi (headers[i].key.c (), name)) return headers[i].value.c ();
		return 0;
	}
private:
	Response (const Response &); Response &operator= (const Response &);
};

// ---- a growing byte buffer -----------------------------------------------------------------------------------------
struct Buf
{
	char *p; int n, cap;
	Buf () : p (0), n (0), cap (0) {}
	~Buf () { free (p); }
	bool reserve (int need)
	{
		if (need + 1 <= cap) return true;
		int c = cap ? cap : 16384;
		while (c < need + 1) c *= 2;
		char *q = (char *) realloc (p, c);
		if (!q) return false;
		p = q; cap = c;
		return true;
	}
	void add (const void *d, int len) { if (len <= 0 || !reserve (n + len)) return; memcpy (p + n, d, len); n += len; p[n] = 0; }
	void add (const char *s) { add (s, (int) strlen (s)); }
	char *take () { char *q = p; p = 0; n = cap = 0; return q; }
};

// ---- the URL -----------------------------------------------------------------------------------------------------
struct Url
{
	bool https; unsigned port; Str host, path;	// path: with its query, without the fragment
	bool parse (const char *u)
	{
		https = false; port = 80;
		const char *p = u;
		while (*p == ' ') p++;
		if (s_startsi (p, "https://")) { https = true; port = 443; p += 8; }
		else if (s_startsi (p, "http://")) p += 7;
		else if (strstr (p, "://")) return false;	// (another scheme)
		const char *h = p;
		while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') p++;
		host.set (h, (int) (p - h));
		if (*p == ':')
		{
			p++; unsigned v = 0; bool any = false;
			while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned) (*p - '0'); p++; any = true; }
			if (any) port = v;
		}
		path.clear ();
		if (*p != '/') path.add ('/');
		const char *e = p; while (*e && *e != '#') e++;
		Str rest; rest.set (p, (int) (e - p));
		url_encode_soft (path, rest.c ());
		return !host.empty ();
	}
};

// ---- the connection: TCP, TLS ----------------------------------------------------------------------------------------
// the one context, made by the first https:// (it reads the system's roots): the window does not wait for it
static SSL_CTX *tls_ctx ()
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	static SSL_CTX *c;
	pthread_mutex_lock (&lock);
	if (!c)
	{
		c = SSL_CTX_new (TLS_client_method ());
		if (c) { SSL_CTX_set_min_proto_version (c, TLS1_2_VERSION); SSL_CTX_set_default_verify_paths (c); }
	}
	pthread_mutex_unlock (&lock);
	return c;
}
struct Transport
{
	int sock; SSL *ssl; bool tls;
	volatile int *cancel;
	Transport () : sock (-1), ssl (0), tls (false), cancel (0) {}
	bool cancelled () const { return cancel && *cancel; }
};
static inline void tp_wait (Transport &t, short what)
{
	struct pollfd p; p.fd = t.sock; p.events = what; p.revents = 0;
	poll (&p, 1, 40);
}
static inline void tp_close (Transport &t)
{
	if (t.ssl) { if (t.tls) SSL_shutdown (t.ssl); SSL_free (t.ssl); t.ssl = 0; }
	if (t.sock >= 0) close (t.sock);
	t.sock = -1; t.tls = false;
}
static inline bool tls_start (Transport &t, const char *host, bool insecure, int timeoutMs, Str &err)
{
	SSL_CTX *ctx = tls_ctx ();
	t.ssl = ctx ? SSL_new (ctx) : 0;
	if (!t.ssl) { err.addf (TR ("The TLS handshake with %s failed."), host); return false; }
	SSL_set_fd (t.ssl, t.sock);
	struct in_addr ip;
	if (inet_pton (AF_INET, host, &ip) != 1) { SSL_set_tlsext_host_name (t.ssl, host); SSL_set1_host (t.ssl, host); }
	else X509_VERIFY_PARAM_set1_ip_asc (SSL_get0_param (t.ssl), host);
	SSL_set_verify (t.ssl, insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, 0);
	unsigned t0 = now_ms ();
	for (;;)
	{
		if (t.cancelled ()) return false;
		ERR_clear_error ();
		int r = SSL_connect (t.ssl);
		if (r == 1) break;
		int e = SSL_get_error (t.ssl, r);
		if (e == SSL_ERROR_WANT_READ) tp_wait (t, POLLIN);
		else if (e == SSL_ERROR_WANT_WRITE) tp_wait (t, POLLOUT);
		else
		{
			long v = insecure ? X509_V_OK : SSL_get_verify_result (t.ssl);
			if (v != X509_V_OK) err.addf (TR ("The server's certificate is not trusted (%s: %s). The request's Settings can turn off the check."), host, X509_verify_cert_error_string (v));
			else err.addf (TR ("The TLS handshake with %s failed."), host);
			return false;
		}
		if ((int) (now_ms () - t0) > timeoutMs) { err.addf (TR ("The TLS handshake with %s took too long."), host); return false; }
	}
	t.tls = true;
	return true;
}
// 0, or HTTP_ERR_*: err says why
static inline int tp_open (Transport &t, const char *host, unsigned port, bool https, bool insecure, int timeoutMs, Str &err)
{
	char ps[12]; snprintf (ps, sizeof ps, "%u", port);
	struct addrinfo hints, *res = 0;
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo (host, ps, &hints, &res) != 0 || !res) { err.addf (TR ("The host %s is unknown."), host); return HTTP_ERR_CONNECT; }
	t.sock = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (t.sock < 0 || connect (t.sock, res->ai_addr, res->ai_addrlen) != 0)
	{
		int e = errno;
		freeaddrinfo (res);
		tp_close (t);
		err.addf (TR ("Could not connect to %s:%u (error %d): it refused the connection or did not answer."), host, port, e);
		return HTTP_ERR_CONNECT;
	}
	freeaddrinfo (res);
	fcntl (t.sock, F_SETFL, fcntl (t.sock, F_GETFL, 0) | O_NONBLOCK);
	if (https && !tls_start (t, host, insecure, timeoutMs, err)) { tp_close (t); return HTTP_ERR_TLS; }
	return 0;
}
// everything sent: false when the connection went, or a cancel
static inline bool tp_send (Transport &t, const char *p, int left)
{
	unsigned t0 = now_ms ();
	while (left > 0)
	{
		if (t.cancelled ()) return false;
		int r;
		if (t.tls)
		{
			ERR_clear_error ();
			r = SSL_write (t.ssl, p, left);
			if (r <= 0)
			{
				int e = SSL_get_error (t.ssl, r);
				if (e == SSL_ERROR_WANT_READ) tp_wait (t, POLLIN);
				else if (e == SSL_ERROR_WANT_WRITE) tp_wait (t, POLLOUT);
				else return false;
			}
		}
		else
		{
			r = (int) send (t.sock, p, (size_t) left, 0);
			if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) tp_wait (t, POLLOUT);
			else if (r < 0) return false;
		}
		if (r > 0) { p += r; left -= r; t0 = now_ms (); }
		else if (now_ms () - t0 > 30000) return false;
	}
	return true;
}
// > 0: bytes; 0: nothing yet (a short wait); < 0: the server closed
static inline int tp_recv (Transport &t, char *b, int cap)
{
	short want = POLLIN;
	if (t.tls)
	{
		ERR_clear_error ();
		int r = SSL_read (t.ssl, b, cap);
		if (r > 0) return r;
		int e = SSL_get_error (t.ssl, r);
		if (e == SSL_ERROR_WANT_WRITE) want = POLLOUT;
		else if (e != SSL_ERROR_WANT_READ) return -1;
	}
	else
	{
		int r = (int) recv (t.sock, b, (size_t) cap, MSG_DONTWAIT);
		if (r > 0) return r;
		if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return -1;
	}
	tp_wait (t, want);
	return 0;
}

// ---- the head of a response, unparsed -----------------------------------------------------------------------------
// the value of a header in h[0..n) (its length in *vl), 0: none
static inline const char *head_find (const char *h, int n, const char *name, int *vl)
{
	int nl = (int) strlen (name);
	const char *e = h + n;
	for (const char *p = h; p < e; )
	{
		const char *le = p; while (le < e && *le != '\r' && *le != '\n') le++;
		if (le - p > nl && p[nl] == ':' && s_eqin (p, name, nl))
		{
			const char *v = p + nl + 1; while (v < le && *v == ' ') v++;
			*vl = (int) (le - v);
			return v;
		}
		p = le; while (p < e && (*p == '\r' || *p == '\n')) p++;
	}
	return 0;
}
// Location against the URL it came from -> the next URL. false: it does not fit.
static inline bool resolve_redirect (const char *base, const char *loc, char *out, int cap)
{
	Str r;
	while (*loc == ' ') loc++;
	const char *sep = strstr (base, "://");
	if (!sep) return false;
	const char *hostEnd = sep + 3; while (*hostEnd && *hostEnd != '/' && *hostEnd != '?' && *hostEnd != '#') hostEnd++;
	if (s_startsi (loc, "http://") || s_startsi (loc, "https://")) r = loc;
	else if (loc[0] == '/' && loc[1] == '/') { r.set (base, (int) (sep - base) + 1); r.add (loc); }
	else if (loc[0] == '/') { r.set (base, (int) (hostEnd - base)); r.add (loc); }
	else
	{
		const char *end = hostEnd; while (*end && *end != '?' && *end != '#') end++;
		const char *slash = 0; for (const char *p = hostEnd; p < end; p++) if (*p == '/') slash = p;
		r.set (base, (int) ((slash ? slash + 1 : hostEnd) - base));
		if (!slash) r.add ('/');
		r.add (loc);
	}
	if (r.len () >= cap) return false;
	s_copy (out, r.c (), cap);
	return true;
}

// ---- preparing -------------------------------------------------------------------------------------------------------
static inline void set_header (KVList &h, const char *k, const char *v, bool replace = true)
{
	for (int i = 0; i < h.size (); i++)
		if (s_eqi (h[i].key.c (), k)) { if (replace) h[i].value = v; return; }
	h.push (KV (k, v));
}
static inline bool has_header (const KVList &h, const char *k)
{
	for (int i = 0; i < h.size (); i++) if (s_eqi (h[i].key.c (), k)) return true;
	return false;
}
// the auth that applies to a request: its own, or its folders', or its collection's
static inline const Auth &effective_auth (const Request &r, const Item *it, const Collection *c)
{
	if (r.auth.type != AUTH_INHERIT) return r.auth;
	for (const Item *p = it ? it->parent : 0; p; p = p->parent)
		if (p->parent && p->auth.type != AUTH_INHERIT) return p->auth;		// (a folder's)
	static Auth none; none.type = AUTH_NONE;
	return c ? c->auth : none;
}
static inline const char *mime_of (const char *path)
{
	const char *e = strrchr (path, '.');
	if (!e) return "application/octet-stream";
	e++;
	if (s_eqi (e, "json")) return "application/json";
	if (s_eqi (e, "txt")) return "text/plain";
	if (s_eqi (e, "png")) return "image/png";
	if (s_eqi (e, "jpg") || s_eqi (e, "jpeg")) return "image/jpeg";
	if (s_eqi (e, "gif")) return "image/gif";
	if (s_eqi (e, "xml")) return "application/xml";
	if (s_eqi (e, "html") || s_eqi (e, "htm")) return "text/html";
	if (s_eqi (e, "csv")) return "text/csv";
	if (s_eqi (e, "pdf")) return "application/pdf";
	return "application/octet-stream";
}
// The request, its variables resolved, made ready to send (the cookies of `jar`).
static inline void prepare (Prepared &p, const Request &r, const Scope &sc, const Auth &auth, CookieJar &jar)
{
	Str v;
	p.method = r.method.empty () ? "GET" : r.method.c ();
	for (int i = 0; i < p.method.len (); i++) if (p.method.c ()[i] >= 'a' && p.method.c ()[i] <= 'z') p.method.data ()[i] -= 32;
	resolve (v, sc, r.url.c ());
	Str url; s_trim (url, v.c ());
	if (!url.empty () && !strstr (url.c (), "://")) { Str t ("http://"); t.add (url.c ()); url = t; }
	p.follow = r.followRedirects;
	p.insecure = r.insecure;
	p.timeoutMs = r.timeoutMs > 0 ? r.timeoutMs : 30000;
	// the headers: the request's, enabled, resolved
	for (int i = 0; i < r.headers.size (); i++)
	{
		const KV &h = r.headers[i];
		if (!h.on || h.key.empty ()) continue;
		KV o; resolve (o.key, sc, h.key.c ()); resolve (o.value, sc, h.value.c ());
		Str k; s_trim (k, o.key.c ()); o.key = k;
		p.headers.push (o);
	}
	// the auth
	switch (auth.type)
	{
	case AUTH_BEARER:
		resolve (v, sc, auth.token.c ());
		{ Str h ("Bearer "); h.add (v.c ()); set_header (p.headers, "Authorization", h.c (), false); }
		break;
	case AUTH_BASIC:
	{
		Str u, pw, both, h ("Basic ");
		resolve (u, sc, auth.user.c ()); resolve (pw, sc, auth.pass.c ());
		both.add (u.c ()); both.add (':'); both.add (pw.c ());
		base64 (h, (const unsigned char *) both.c (), both.len ());
		set_header (p.headers, "Authorization", h.c (), false);
		break;
	}
	case AUTH_APIKEY:
	{
		Str k, val; resolve (k, sc, auth.key.c ()); resolve (val, sc, auth.value.c ());
		if (k.empty ()) break;
		if (auth.inQuery)
		{
			Str q; url_encode (q, k.c ()); q.add ('='); url_encode (q, val.c ());
			url.add (strchr (url.c (), '?') ? '&' : '?'); url.add (q.c ());
		}
		else set_header (p.headers, k.c (), val.c (), false);
		break;
	}
	}
	p.url = url;
	// the body
	Buf b; const char *ctype = 0; Str ctBuf;
	if (r.bodyMode == BODY_RAW)
	{
		resolve (v, sc, r.raw.c ());
		b.add (v.c (), v.len ());
		ctype = RAW_CTYPE[r.rawLang];
	}
	else if (r.bodyMode == BODY_URLENC)
	{
		Str s;
		for (int i = 0; i < r.urlenc.size (); i++)
		{
			const KV &kv = r.urlenc[i];
			if (!kv.on || kv.key.empty ()) continue;
			Str k, val; resolve (k, sc, kv.key.c ()); resolve (val, sc, kv.value.c ());
			if (!s.empty ()) s.add ('&');
			url_encode (s, k.c (), true); s.add ('='); url_encode (s, val.c (), true);
		}
		b.add (s.c (), s.len ());
		ctype = "application/x-www-form-urlencoded";
	}
	else if (r.bodyMode == BODY_FORM)
	{
		Str boundary ("----CourierFormBoundary");
		for (int i = 0; i < 16; i++) boundary.add ("0123456789abcdefghijklmnopqrstuvwxyz"[rnd32 () % 36]);
		for (int i = 0; i < r.form.size (); i++)
		{
			const KV &kv = r.form[i];
			if (!kv.on || kv.key.empty ()) continue;
			Str k, val; resolve (k, sc, kv.key.c ()); resolve (val, sc, kv.value.c ());
			b.add ("--"); b.add (boundary.c ()); b.add ("\r\n");
			if (kv.file)
			{
				int n = 0; char *f = read_file (val.c (), &n);
				if (!f) { p.error.addf (TR ("The file of the form's field \"%s\" cannot be read: %s"), k.c (), val.c ()); return; }
				Str d; d.addf ("Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n", k.c (), base_name (val.c ()), mime_of (val.c ()));
				b.add (d.c ()); b.add (f, n); b.add ("\r\n");
				free (f);
			}
			else
			{
				Str d; d.addf ("Content-Disposition: form-data; name=\"%s\"\r\n\r\n", k.c ());
				b.add (d.c ()); b.add (val.c (), val.len ()); b.add ("\r\n");
			}
		}
		b.add ("--"); b.add (boundary.c ()); b.add ("--\r\n");
		ctBuf.add ("multipart/form-data; boundary="); ctBuf.add (boundary.c ());
		ctype = ctBuf.c ();
	}
	else if (r.bodyMode == BODY_BINARY && !r.binary.empty ())
	{
		resolve (v, sc, r.binary.c ());
		int n = 0; char *f = read_file (v.c (), &n);
		if (!f) { p.error.addf (TR ("The body's file cannot be read: %s"), v.c ()); return; }
		b.add (f, n); free (f);
		ctype = mime_of (v.c ());
	}
	if (ctype && !has_header (p.headers, "Content-Type")) p.headers.push (KV ("Content-Type", ctype));
	p.bodyLen = b.n; p.body = b.take ();
	// the defaults (as Postman sends), unless the request has its own
	set_header (p.headers, "User-Agent", USER_AGENT, false);
	set_header (p.headers, "Accept", "*/*", false);
	set_header (p.headers, "Accept-Encoding", "gzip, deflate", false);
	// the jar's cookies (unless the request sets its own Cookie header)
	if (!has_header (p.headers, "Cookie"))
	{
		Url u;
		if (u.parse (url.c ()))
		{
			Str c; jar.header (c, u.host.c (), u.path.c (), u.https);
			if (!c.empty ()) p.headers.push (KV ("Cookie", c.c ()));
		}
	}
}

// ---- decoding ---------------------------------------------------------------------------------------------------
// the chunked body b[0..n) decoded in place -> its length
static inline int dechunk (char *b, int n)
{
	int r = 0, w = 0;
	while (r < n)
	{
		int sz = 0, any = 0;
		while (r < n && hexval (b[r]) >= 0) { sz = sz * 16 + hexval (b[r]); r++; any = 1; }
		while (r < n && b[r] != '\n') r++;
		if (r < n) r++;
		if (!any || sz == 0) break;
		if (sz > n - r) sz = n - r;
		memmove (b + w, b + r, sz); w += sz; r += sz;
		if (r < n && b[r] == '\r') r++;
		if (r < n && b[r] == '\n') r++;
	}
	return w;
}
// gzip / deflate (zlib's, or bare: some servers send that) -> a malloc'd buffer, NUL-terminated (0: could not)
static inline char *inflate_body (const char *b, int n, bool gzip, int *outLen)
{
	const unsigned char *d = (const unsigned char *) b;
	int off = 0; bool zlib = false;
	if (gzip)
	{
		if (n < 18 || d[0] != 0x1F || d[1] != 0x8B) return 0;
		int flg = d[3]; off = 10;
		if (flg & 4) { if (off + 2 > n) return 0; off += 2 + (d[off] | d[off + 1] << 8); }
		if (flg & 8) { while (off < n && d[off]) off++; off++; }
		if (flg & 16) { while (off < n && d[off]) off++; off++; }
		if (flg & 2) off += 2;
		if (off >= n) return 0;
	}
	else zlib = n >= 2 && (d[0] & 0x0F) == 8 && ((d[0] << 8) | d[1]) % 31 == 0;
	int len = 0;
	char *o = stbi_zlib_decode_malloc_guesssize_headerflag (b + off, n - off, imax (16384, (n - off) * 4), &len, zlib);
	if (!o) return 0;
	char *r = (char *) malloc ((size_t) len + 1);
	if (r) { memcpy (r, o, (size_t) len); r[len] = 0; *outLen = len; }
	free (o);
	return r;
}

// ---- sending --------------------------------------------------------------------------------------------------------
// One exchange (no redirect). The response's head parsed into resp; its body in body (raw bytes).
// Returns false when it failed (resp.status < 0, resp.error set).
static inline bool exchange (const char *method, const char *url, const KVList &headers, const char *body, int bodyLen, bool insecure,
			     int timeoutMs, volatile int *cancel, Response &resp, Buf &raw, int *hdrEnd, bool first)
{
	Url u;
	if (!u.parse (url)) { resp.status = -1; resp.error.addf (TR ("The URL is not valid: %s"), url); return false; }
	unsigned char ni[64]; memset (ni, 0, sizeof ni);
	net_info ((long) ni);
	if (!(ni[0] | ni[1] | ni[2] | ni[3])) { resp.status = HTTP_ERR_NET; resp.error = TR ("The network is down (no link, no address)."); return false; }

	// the request's head
	Str head;
	head.addf ("%s %s HTTP/1.1\r\n", method, u.path.c ());
	head.add ("Host: "); head.add (u.host.c ());
	if ((u.https && u.port != 443) || (!u.https && u.port != 80)) head.addf (":%u", u.port);
	head.add ("\r\n");
	for (int i = 0; i < headers.size (); i++)
	{
		if (s_eqi (headers[i].key.c (), "Host") || s_eqi (headers[i].key.c (), "Content-Length") || s_eqi (headers[i].key.c (), "Connection")) continue;
		head.add (headers[i].key.c ()); head.add (": "); head.add (headers[i].value.c ()); head.add ("\r\n");
	}
	if (bodyLen > 0 || s_eq (method, "POST") || s_eq (method, "PUT") || s_eq (method, "PATCH")) head.addf ("Content-Length: %d\r\n", bodyLen);
	head.add ("Connection: close\r\n\r\n");

	resp.console.addf ("%s %s\r\n", method, url);
	resp.console.add (head.c ());
	if (bodyLen > 0)
	{
		bool text = true;
		for (int i = 0; i < bodyLen && i < 4096; i++) { unsigned char c = (unsigned char) body[i]; if (c < 9 || (c > 13 && c < 32)) text = false; }
		if (text) { resp.console.add (body, imin (bodyLen, 8192)); if (bodyLen > 8192) { resp.console.add ('\n'); resp.console.addf (TR ("... (%d bytes in all)"), bodyLen); }; }
		else resp.console.addf (TR ("(%d bytes of binary data)"), bodyLen);
		resp.console.add ("\r\n\r\n");
	}

	unsigned t0 = now_ms ();
	Transport tp; tp.cancel = cancel;
	Str why;
	int orc = tp_open (tp, u.host.c (), u.port, u.https, insecure, timeoutMs, why);
	if (orc != 0)
	{
		resp.status = orc; resp.error = why;
		if (cancel && *cancel) { resp.status = -9; resp.cancelled = true; resp.error = TR ("Cancelled."); }
		return false;
	}
	if (first) resp.msConnect = (int) (now_ms () - t0);
	if (!tp_send (tp, head.c (), head.len ()) || (bodyLen > 0 && !tp_send (tp, body, bodyLen)))
	{
		tp_close (tp); resp.status = HTTP_ERR_SEND; resp.error = TR ("The request could not be sent (the connection closed)."); return false;
	}
	// read: until the server closes, the body is complete (Content-Length, the last chunk), or the timeout
	raw.n = 0;
	unsigned last = now_ms ();
	bool gotFirst = false;
	int he = -1; long long want = -1; bool chunked = false, noBody = s_eq (method, "HEAD");
	char tmp[8192];
	for (;;)
	{
		if (cancel && *cancel) { tp_close (tp); resp.status = -9; resp.cancelled = true; resp.error = TR ("Cancelled."); return false; }
		if (raw.n >= RESP_MAX) { resp.truncated = true; break; }
		int n = tp_recv (tp, tmp, imin ((int) sizeof tmp, RESP_MAX - raw.n));
		if (n > 0)
		{
			if (!gotFirst) { gotFirst = true; if (first) resp.msFirst = (int) (now_ms () - t0); }
			raw.add (tmp, n); last = now_ms ();
			if (he < 0)
			{
				for (int k = imax (0, raw.n - n - 3); k + 3 < raw.n; k++)
					if (raw.p[k] == '\r' && raw.p[k + 1] == '\n' && raw.p[k + 2] == '\r' && raw.p[k + 3] == '\n') { he = k; break; }
				if (he >= 0)
				{
					// the status, then what says where the body ends
					int code = 0; const char *sp = strchr (raw.p, ' ');
					if (sp) code = atoi (sp + 1);
					if (code == 204 || code == 304 || (code >= 100 && code < 200)) noBody = true;
					int vl = 0;
					const char *te = head_find (raw.p, he, "Transfer-Encoding", &vl);
					if (te && s_findi (te, vl, "chunked") >= 0) chunked = true;
					const char *cl = head_find (raw.p, he, "Content-Length", &vl);
					if (cl && !chunked) want = atoll (cl);
					if (code >= 100 && code < 200 && code != 101)	// (an interim response: skip it)
					{
						int rest = raw.n - (he + 4);
						memmove (raw.p, raw.p + he + 4, rest); raw.n = rest; raw.p[raw.n] = 0;
						he = -1; noBody = s_eq (method, "HEAD"); chunked = false; want = -1;
						continue;
					}
				}
			}
			if (he >= 0)
			{
				int got = raw.n - (he + 4);
				if (noBody) break;
				if (want >= 0 && got >= want) break;
				if (chunked && got >= 5 && !memcmp (raw.p + raw.n - 5, "0\r\n\r\n", 5)) break;
			}
		}
		else if (n == 0)
		{
			if ((int) (now_ms () - last) > timeoutMs)
			{
				if (raw.n == 0 || he < 0)
				{
					tp_close (tp); resp.status = HTTP_ERR_TIMEOUT;
					resp.error.addf (TR ("No response within %d ms (the timeout: the request's Settings)."), timeoutMs);
					return false;
				}
				break;
			}
		}
		else break;			// the server closed: the response is complete
	}
	tp_close (tp);
	if (raw.n == 0) { resp.status = HTTP_ERR_EMPTY; resp.error = TR ("The server closed the connection without an answer."); return false; }
	if (he < 0) he = raw.n;
	*hdrEnd = he;
	resp.wireLen += raw.n;
	// the status line and the headers
	resp.headers.clear ();
	const char *s = raw.p;
	const char *eol = strchr (s, '\r'); if (!eol || eol - s > he) eol = s + he;
	{
		const char *sp = s; while (sp < eol && *sp != ' ') sp++;
		resp.httpVersion.set (s, (int) (sp - s));
		resp.status = atoi (sp);
		const char *rs = sp; while (rs < eol && *rs == ' ') rs++;
		while (rs < eol && *rs >= '0' && *rs <= '9') rs++;
		while (rs < eol && *rs == ' ') rs++;
		resp.reason.set (rs, (int) (eol - rs));
		if (resp.status <= 0) { resp.status = -7; resp.error = TR ("The answer is not HTTP."); return false; }
	}
	const char *p = eol;
	while (p < s + he)
	{
		while (p < s + he && (*p == '\r' || *p == '\n')) p++;
		const char *e = p; while (e < s + he && *e != '\r' && *e != '\n') e++;
		const char *c = p; while (c < e && *c != ':') c++;
		if (c < e) { KV kv; s_trim (kv.key, p, (int) (c - p)); s_trim (kv.value, c + 1, (int) (e - c - 1)); resp.headers.push (kv); }
		p = e;
	}
	resp.headLen = he + 4;
	resp.console.add (raw.p, imin (he + 4, raw.n));
	resp.console.add ("\r\n");
	return true;
}

// The whole request (the redirects followed if asked), into resp. The jar gets the cookies set.
static inline void perform (const Prepared &p, CookieJar &jar, volatile int *cancel, Response &resp)
{
	if (!p.error.empty ()) { resp.status = -10; resp.error = p.error; return; }
	if (p.url.empty ()) { resp.status = -1; resp.error = TR ("Enter a URL to send the request to."); return; }
	unsigned t0 = now_ms ();
	Str url = p.url, method = p.method;
	KVList headers = p.headers;
	const char *body = p.body; int bodyLen = p.bodyLen;
	Buf raw; int he = 0;
	for (int hop = 0; ; hop++)
	{
		if (!exchange (method.c (), url.c (), headers, body, bodyLen, p.insecure, p.timeoutMs, cancel, resp, raw, &he, hop == 0)) break;
		// the cookies set
		Url u; u.parse (url.c ());
		for (int i = 0; i < resp.headers.size (); i++)
			if (s_eqi (resp.headers[i].key.c (), "Set-Cookie"))
			{
				Cookie c; jar.set (resp.headers[i].value.c (), u.host.c (), u.path.c (), &c);
				resp.setCookies.push (c);
			}
		const char *loc = resp.header ("Location");
		bool redirect = resp.status >= 300 && resp.status < 400 && resp.status != 304 && loc && loc[0];
		if (redirect && p.follow && hop < 10)
		{
			char next[2048];
			if (!resolve_redirect (url.c (), loc, next, (int) sizeof next)) redirect = false;
			else
			{
				resp.console.addf (TR ("-> redirected (%d) to %s"), resp.status, next); resp.console.add ("\r\n\r\n");
				if (resp.status != 307 && resp.status != 308) { if (!method.eq ("HEAD")) method = "GET"; body = 0; bodyLen = 0; }
				url = next;
				resp.redirects++;
				// the new host's cookies (not the old ones), no body headers after a switch to GET
				for (int i = headers.size () - 1; i >= 0; i--)
					if (s_eqi (headers[i].key.c (), "Cookie") || (!body && s_eqi (headers[i].key.c (), "Content-Type"))) headers.remove (i);
				Url nu;
				if (nu.parse (url.c ())) { Str c; jar.header (c, nu.host.c (), nu.path.c (), nu.https); if (!c.empty ()) headers.push (KV ("Cookie", c.c ())); }
				continue;
			}
		}
		// the body: the chunks joined, then inflated
		int bstart = he + 4;
		int blen = raw.n > bstart ? raw.n - bstart : 0;
		char *b = raw.p + (raw.n > bstart ? bstart : raw.n);
		const char *te = resp.header ("Transfer-Encoding");
		if (te && s_findi (te, (int) strlen (te), "chunked") >= 0) blen = dechunk (b, blen);
		else { const char *cl = resp.header ("Content-Length"); if (cl) { int n = atoi (cl); if (n >= 0 && n < blen) blen = n; } }
		const char *ce = resp.header ("Content-Encoding");
		char *dec = 0; int dlen = 0;
		if (ce && blen > 0 && (s_eqi (ce, "gzip") || s_eqi (ce, "x-gzip") || s_eqi (ce, "deflate")))
			dec = inflate_body (b, blen, !s_eqi (ce, "deflate"), &dlen);
		if (dec) { resp.body = dec; resp.bodyLen = dlen; }
		else
		{
			resp.body = (char *) malloc (blen + 1);
			if (resp.body) { memcpy (resp.body, b, blen); resp.body[blen] = 0; resp.bodyLen = blen; }
		}
		break;
	}
	resp.finalUrl = url;
	resp.msTotal = (int) (now_ms () - t0);
}

// ---- the job: a request on a thread ------------------------------------------------------------------------------
struct Job
{
	Prepared prep;
	Response resp;
	CookieJar jar;				// a copy of the app's, updated by the job
	volatile int cancel;
	volatile int done;
	pthread_t tid; bool threaded;
	void (*onDone) (Job *);			// run by the window's loop (jobs_poll) once the job is done
	void *user;
	Job () : cancel (0), done (0), threaded (false), onDone (0), user (0) {}
};
static Vec<Job *> g_jobs;			// the jobs under way
static void *job_thread (void *arg)
{
	Job *j = (Job *) arg;
	perform (j->prep, j->jar, &j->cancel, j->resp);
	__atomic_store_n (&j->done, 1, __ATOMIC_RELEASE);
	return 0;
}
// start it: on a thread (else done here and now; jobs_poll says so all the same)
static inline void job_start (Job *j)
{
	g_jobs.push (j);
	pthread_attr_t a; pthread_attr_init (&a); pthread_attr_setstacksize (&a, 512 * 1024);
	j->threaded = pthread_create (&j->tid, &a, job_thread, j) == 0;
	pthread_attr_destroy (&a);
	if (!j->threaded) job_thread (j);
}
// the window's loop: the jobs that ended
static inline void jobs_poll ()
{
	for (int i = g_jobs.size () - 1; i >= 0; i--)
	{
		Job *j = g_jobs[i];
		if (!__atomic_load_n (&j->done, __ATOMIC_ACQUIRE)) continue;
		g_jobs.remove (i);
		if (j->threaded) pthread_join (j->tid, 0);
		if (j->onDone) j->onDone (j);
	}
}

} // namespace cr

#endif
