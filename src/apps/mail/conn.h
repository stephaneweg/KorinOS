//
// conn.h -- a mail server's connection: TCP over POSIX sockets, TLS from the start (993, 995, 465) or after STARTTLS
// (143, 110, 587) through OpenSSL (the certificate checked against the system's store, /etc/ca-bundle, unless the
// account says not to), read line by line (CRLF) or by an exact count (IMAP's literals), with a time-out and a cancel
// flag. Blocking: used from Mail's worker thread. Onyx's user/Libs/mail/conn.h, its sockets and TLS rewritten: the
// socket is non-blocking and waited on by poll, so that a cancel and the time-out hold at every step (connect ()
// itself always blocks on ARMPI, docs/POSIX.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef ONYX_MAIL_CONN_H
#define ONYX_MAIL_CONN_H

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <netkit_abi.h>
#include <uikit.h>
#include "util.h"

// The words shown are in the system's language (ui_tr); TRN marks a text translated where it is shown
#define TR(s)	((const char *) ui_tr ((long) (s)))
#define TRN(s)	(s)

namespace mail {

enum Security { SEC_TLS = 0, SEC_STARTTLS = 1, SEC_NONE = 2 };

static inline unsigned clock_ms ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

// the one context (only the worker thread makes connections): the system's roots, TLS 1.2 at least
static SSL_CTX *tls_ctx ()
{
	static SSL_CTX *c;
	if (c) return c;
	c = SSL_CTX_new (TLS_client_method ());
	if (!c) return 0;
	SSL_CTX_set_min_proto_version (c, TLS1_2_VERSION);
	SSL_CTX_set_default_verify_paths (c);
	return c;
}

struct Conn
{
	int sock; bool tls, open_; SSL *ssl;
	char host[128];
	char in[16384]; int rp, wp;
	volatile int *cancel;				// set by the window: give up at once
	unsigned timeoutMs;
	bool verify, insecureOk;			// the certificate checked; refused -> go on anyway (the user said so)
	bool softTimeout, timedOut;			// (IDLE: the time-out only says so, the connection kept)
	bool certBad;					// (the last failure: the certificate refused -- Mail offers to go on)
	char err[240];
	void (*trace) (void *, bool out, const char *line, int n); void *traceCtx;	// (the account's log: what went, what came)

	Conn () : sock (-1), tls (false), open_ (false), ssl (0), rp (0), wp (0), cancel (0), timeoutMs (30000), verify (true), insecureOk (false), softTimeout (false), timedOut (false), certBad (false), trace (0), traceCtx (0)
	{ host[0] = err[0] = 0; }
	~Conn () { close (); }

	bool fail (const char *fmt, ...)
	{
		va_list a; va_start (a, fmt); vsnprintf (err, sizeof err, TR (fmt), a); va_end (a);
		return false;
	}
	bool cancelled () const { return cancel && *cancel; }

	// the socket ready for `what` (POLLIN, POLLOUT), a short wait: the callers loop and watch the clock
	void wait_io (short what)
	{
		struct pollfd p; p.fd = sock; p.events = what; p.revents = 0;
		poll (&p, 1, 40);
	}
	bool expired (unsigned t0) const { return clock_ms () - t0 > timeoutMs; }

	bool tls_start ()
	{
		certBad = false;
		SSL_CTX *ctx = tls_ctx ();
		if (!ctx) return fail ("The secure connection to %s failed.", host);
		ssl = SSL_new (ctx);
		if (!ssl) return fail ("The secure connection to %s failed.", host);
		SSL_set_fd (ssl, sock);
		struct in_addr ip;
		bool isIp = inet_pton (AF_INET, host, &ip) == 1;
		if (!isIp) { SSL_set_tlsext_host_name (ssl, host); SSL_set1_host (ssl, host); }
		else X509_VERIFY_PARAM_set1_ip_asc (SSL_get0_param (ssl), host);
		bool check = verify && !insecureOk;
		SSL_set_verify (ssl, check ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, 0);
		unsigned t0 = clock_ms ();
		for (;;)
		{
			if (cancelled ()) return fail ("Cancelled.");
			ERR_clear_error ();
			int r = SSL_connect (ssl);
			if (r == 1) break;
			int e = SSL_get_error (ssl, r);
			if (e == SSL_ERROR_WANT_READ) wait_io (POLLIN);
			else if (e == SSL_ERROR_WANT_WRITE) wait_io (POLLOUT);
			else
			{
				long v = check ? SSL_get_verify_result (ssl) : X509_V_OK;
				if (v != X509_V_OK)
				{
					certBad = true;
					return fail ("The server's certificate is not trusted (%s: %s): it may not be the real one.", host, X509_verify_cert_error_string (v));
				}
				return fail ("The secure connection to %s failed.", host);
			}
			if (expired (t0)) return fail ("The secure connection to %s took too long.", host);
		}
		tls = true;
		return true;
	}
	// open: TLS at once (sec SEC_TLS) or plain (STARTTLS comes later: starttls ())
	bool open (const char *h, int port, int sec)
	{
		close ();
		scpy (host, h, sizeof host); rp = wp = 0; err[0] = 0; certBad = false;
		unsigned char ni[64]; memset (ni, 0, sizeof ni);
		net_info ((long) ni);
		if (!(ni[0] | ni[1] | ni[2] | ni[3])) return fail ("The network is not up (Wi-Fi?).");
		char ps[12]; snprintf (ps, sizeof ps, "%d", port);
		struct addrinfo hints, *res = 0;
		memset (&hints, 0, sizeof hints);
		hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo (h, ps, &hints, &res) != 0 || !res) return fail ("The server %s was not found.", h);
		sock = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sock < 0 || connect (sock, res->ai_addr, res->ai_addrlen) != 0)
		{
			freeaddrinfo (res);
			if (sock >= 0) ::close (sock);
			sock = -1;
			return fail ("The server %s did not answer (port %d).", h, port);
		}
		freeaddrinfo (res);
		fcntl (sock, F_SETFL, fcntl (sock, F_GETFL, 0) | O_NONBLOCK);
		open_ = true;
		if (sec == SEC_TLS && !tls_start ()) { char keep[240]; bool c = certBad; scpy (keep, err, sizeof keep); close (); scpy (err, keep, sizeof err); certBad = c; return false; }
		return true;
	}
	// after the server said yes to STARTTLS: what was read before is dropped (RFC 3207)
	bool starttls () { rp = wp = 0; return tls_start (); }
	void close ()
	{
		if (ssl) { if (tls) SSL_shutdown (ssl); SSL_free (ssl); ssl = 0; }
		if (sock >= 0) ::close (sock);
		tls = false; open_ = false; sock = -1; rp = wp = 0;
	}

	bool send (const void *b, int n)
	{
		if (!open_) return fail ("Not connected.");
		if (trace) trace (traceCtx, true, (const char *) b, n);
		const char *p = (const char *) b; int left = n;
		unsigned t0 = clock_ms ();
		while (left > 0)
		{
			if (cancelled ()) { close (); return fail ("Cancelled."); }
			int r;
			if (tls)
			{
				ERR_clear_error ();
				r = SSL_write (ssl, p, left);
				if (r <= 0)
				{
					int e = SSL_get_error (ssl, r);
					if (e == SSL_ERROR_WANT_READ) wait_io (POLLIN);
					else if (e == SSL_ERROR_WANT_WRITE) wait_io (POLLOUT);
					else { close (); return fail ("The connection to %s was lost.", host); }
				}
			}
			else
			{
				r = (int) ::send (sock, p, (size_t) left, 0);
				if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) wait_io (POLLOUT);
				else if (r < 0) { close (); return fail ("The connection to %s was lost.", host); }
			}
			if (r > 0) { p += r; left -= r; t0 = clock_ms (); }
			else if (expired (t0)) { close (); return fail ("The connection to %s was lost.", host); }
		}
		return true;
	}
	bool send (const char *str) { return send (str, (int) strlen (str)); }
	bool sendf (const char *fmt, ...)
	{
		char t[2048]; va_list a; va_start (a, fmt); int k = vsnprintf (t, sizeof t, fmt, a); va_end (a);
		if (k >= (int) sizeof t) k = sizeof t - 1;
		return send (t, k);
	}
	// more bytes into in[]: false on the connection's end, the time-out or a cancel
	bool fill ()
	{
		if (rp > 0 && rp == wp) rp = wp = 0;
		if (wp == (int) sizeof in) { memmove (in, in + rp, wp - rp); wp -= rp; rp = 0; }
		unsigned t0 = clock_ms (); timedOut = false;
		for (;;)
		{
			if (cancelled ()) { close (); return fail ("Cancelled."); }
			int r; short want = POLLIN;
			if (tls)
			{
				ERR_clear_error ();
				r = SSL_read (ssl, in + wp, (int) sizeof in - wp);
				if (r > 0) { wp += r; return true; }
				int e = SSL_get_error (ssl, r);
				if (e == SSL_ERROR_WANT_WRITE) want = POLLOUT;
				else if (e != SSL_ERROR_WANT_READ) { close (); return fail ("%s closed the connection.", host); }
			}
			else
			{
				r = (int) recv (sock, in + wp, sizeof in - wp, MSG_DONTWAIT);
				if (r > 0) { wp += r; return true; }
				if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { close (); return fail ("%s closed the connection.", host); }
			}
			if (expired (t0))
			{
				timedOut = true;
				if (softTimeout) return false;
				close (); return fail ("%s does not answer any more.", host);
			}
			wait_io (want);
		}
	}
	// a line without its CRLF into o (cleared first): false when the connection ends (a time-out loses nothing: what
	// came of the line stays in in[] until its end comes)
	bool line (Buf &o)
	{
		o.clear ();
		int scan = rp;
		for (;;)
		{
			for (int i = scan; i < wp; i++)
				if (in[i] == '\n')
				{
					o.add (in + rp, i - rp); rp = i + 1;
					if (o.n && o.p[o.n - 1] == '\r') o.p[--o.n] = 0;
					if (trace) trace (traceCtx, false, o.c (), o.n);
					return true;
				}
			if (rp == 0 && wp == (int) sizeof in) { o.add (in, wp); rp = wp = 0; }	// (a long line: kept aside)
			scan = wp;
			if (o.n > (8 << 20)) { close (); return fail ("A line too long from %s.", host); }
			if (!open_) return false;
			int had = rp; if (!fill ()) return false;
			scan -= had - rp;					// (fill () may have moved in[])
		}
	}
	// exactly n bytes appended to o
	bool bytes (Buf &o, long n)
	{
		if (!o.reserve ((int) n)) { close (); return fail ("Not enough memory (%ld bytes).", n); }
		while (n > 0)
		{
			if (rp == wp && (!open_ || !fill ())) return false;
			int k = wp - rp < n ? wp - rp : (int) n;
			o.add (in + rp, k); rp += k; n -= k;
		}
		return true;
	}
};

} // namespace mail

#endif
