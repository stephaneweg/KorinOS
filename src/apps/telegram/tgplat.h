//
// tgplat.h -- what the Telegram app's core (tl.h, tgcrypto.h, mtproto.h, client.h) asks of the OS, here in POSIX: the clock
// (UTC), the log (a ring kept in memory, written to SD0:/home/telegram/log.txt), the entropy (getentropy), the seed file,
// gzip's inflate (ArchiveKit), the files (the session, the cache), and the TCP transport: MTProto's "intermediate" framing
// (0xeeeeeeee, then every packet its length on 4 bytes) over a socket to a data centre's port.
//
// connect () blocks: the transport opens in a thread of its own, as IRC does, and what is sent before it is there waits.
// While config.ini names a test server (test_host), no connection goes anywhere else: open () refuses.
//
// MIT licence.
//
#ifndef TG_PLAT_H
#define TG_PLAT_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <archivekit_abi.h>
#include "mtproto.h"

#define TG_DIR		"SD0:/home/telegram/"

// ---- the clock -------------------------------------------------------------------------------------

long long tg_unix_ms ()
{
	struct timespec ts;
	clock_gettime (CLOCK_REALTIME, &ts);
	return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

unsigned long long tg_platform_counter ()
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned long long) ts.tv_sec * 1000000000ull + (unsigned long long) ts.tv_nsec;
}

int tg_platform_entropy (unsigned char *buf, int n)
{
	return getentropy (buf, (size_t) n) == 0 ? n : 0;
}

// ---- files -----------------------------------------------------------------------------------------

// A whole file -> malloc'd bytes (NUL-terminated), *n its size; 0 if there is none.
static unsigned char *tg_load (const char *path, int *n)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0) return 0;
	struct stat st;
	if (fstat (fd, &st) != 0 || st.st_size > (64 << 20)) { close (fd); return 0; }
	int sz = (int) st.st_size, got = 0;
	unsigned char *b = (unsigned char *) malloc ((size_t) sz + 1);
	if (!b) { close (fd); return 0; }
	while (got < sz)
	{
		int r = (int) read (fd, b + got, (size_t) (sz - got));
		if (r <= 0) break;
		got += r;
	}
	close (fd);
	b[got] = 0;
	*n = got;
	return b;
}
static bool tg_write (const char *path, const void *d, int n)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return false;
	const char *p = (const char *) d;
	int left = n;
	while (left > 0)
	{
		int r = (int) write (fd, p, (size_t) left);
		if (r <= 0) break;
		p += r; left -= r;
	}
	close (fd);
	return left == 0;
}

// The card can be busy for a long time (Jet's preload holds vfs): the window never waits for it. What goes to the card or
// comes from it goes through a thread of its own, in order: tg_save (a copy of the bytes) and the pictures read (look.h).
struct IoJob
{
	IoJob *next;
	char path[160];
	unsigned char *data; int n;			// to write; 0: a picture to read
	int w, h; bool square;				// the picture, at that size
	unsigned *px;
};
static pthread_mutex_t s_ioLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_ioCond = PTHREAD_COND_INITIALIZER;
static IoJob *s_ioHead, *s_ioTail;
static volatile int s_ioBusy;
static bool s_ioStarted;
static void img_job (IoJob *j);				// look.h
static IoJob *volatile s_ioDone;			// the pictures read, for the window (img_poll)

static void *io_thread (void *)
{
	for (;;)
	{
		pthread_mutex_lock (&s_ioLock);
		while (!s_ioHead) pthread_cond_wait (&s_ioCond, &s_ioLock);
		IoJob *j = s_ioHead;
		s_ioHead = j->next;
		if (!s_ioHead) s_ioTail = 0;
		s_ioBusy = 1;
		pthread_mutex_unlock (&s_ioLock);
		if (j->data) { tg_write (j->path, j->data, j->n); free (j->data); delete j; }
		else
		{
			img_job (j);
			pthread_mutex_lock (&s_ioLock);
			j->next = s_ioDone; s_ioDone = j;
			pthread_mutex_unlock (&s_ioLock);
		}
		s_ioBusy = 0;
	}
	return 0;
}
static void io_push (IoJob *j)
{
	pthread_mutex_lock (&s_ioLock);
	if (!s_ioStarted)
	{
		s_ioStarted = true;
		pthread_attr_t at; pthread_attr_init (&at);
		pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
		pthread_t th;
		pthread_create (&th, &at, io_thread, 0);
		pthread_attr_destroy (&at);
	}
	j->next = 0;
	if (s_ioTail) s_ioTail->next = j; else s_ioHead = j;
	s_ioTail = j;
	pthread_cond_signal (&s_ioCond);
	pthread_mutex_unlock (&s_ioLock);
}
static bool tg_save (const char *path, const void *d, int n)
{
	IoJob *j = new IoJob;
	memset (j, 0, sizeof *j);
	snprintf (j->path, sizeof j->path, "%s", path);
	j->data = (unsigned char *) malloc ((size_t) n + 1);
	if (!j->data) { delete j; return false; }
	memcpy (j->data, d, (size_t) n);
	j->n = n;
	io_push (j);
	return true;
}
// (the end of the program: what waits is written, for a few seconds at most)
static void tg_save_drain ()
{
	for (int i = 0; i < 600 && (s_ioHead || s_ioBusy); i++) usleep (5000);
}

bool tg_seed_load (unsigned char *buf, int n)
{
	int m = 0;
	unsigned char *b = tg_load (TG_DIR "seed.bin", &m);
	if (!b) return false;
	bool ok = m >= n;
	if (ok) memcpy (buf, b, (size_t) n);
	free (b);
	return ok;
}
void tg_seed_save (const unsigned char *buf, int n) { tg_save (TG_DIR "seed.bin", buf, n); }

// ---- inflate ---------------------------------------------------------------------------------------

bool tg_gunzip (const unsigned char *in, int n, unsigned char **out, int *outn)
{
	long b = ar_inflate_new ((long) in, n, AR_GZIP, 0);
	if (b <= 0) return false;
	int on = (int) ar_bytes (b);
	*out = (unsigned char *) malloc ((size_t) on + 1);		// (the caller frees with free: the kit's own is ar_free)
	if (*out) { memcpy (*out, (const void *) ar_data (b), (size_t) on); *outn = on; }
	ar_free (b);
	return *out != 0;
}

// ---- the log ---------------------------------------------------------------------------------------

struct TgLog { char buf[32768]; int n; bool dirty; long long saved; };
static TgLog &tglog () { static TgLog l; return l; }

void tg_log (const char *fmt, ...)
{
	char line[300];
	time_t t = time (0);
	struct tm m;
	localtime_r (&t, &m);
	int o = snprintf (line, sizeof line, "%02d:%02d:%02d ", m.tm_hour, m.tm_min, m.tm_sec);
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (line + o, sizeof line - (size_t) o - 1, fmt, ap);
	va_end (ap);
	int n = (int) strlen (line);
	line[n++] = '\n';
	TgLog &L = tglog ();
	if (L.n + n > (int) sizeof L.buf)		// (the older half dropped)
	{
		int keep = (int) sizeof L.buf / 2;
		memmove (L.buf, L.buf + L.n - keep, (size_t) keep);
		L.n = keep;
	}
	memcpy (L.buf + L.n, line, (size_t) n);
	L.n += n;
	L.dirty = true;
}

// (from the app's tick: the log written once in a while)
static void tg_log_flush (bool now)
{
	TgLog &L = tglog ();
	long long t = tg_unix_ms ();
	if (!L.dirty || (!now && t - L.saved < 5000)) return;
	tg_save (TG_DIR "log.txt", L.buf, L.n);
	L.dirty = false;
	L.saved = t;
}

// ---- TCP: the intermediate transport ---------------------------------------------------------------

// config.ini's test_host: the only host connections may go to ("" : no restriction).
static char g_tgOnlyHost[64];

// The connection's thread: it writes its answer here and goes. The transport gone before that: `abandoned`, the
// thread closes the socket and frees the job.
struct TcpJob { char host[64]; int port; int fd; volatile int done, abandoned; };

static void *tcp_thread (void *a)
{
	TcpJob *j = (TcpJob *) a;
	int fd = -1;
	char port[12]; snprintf (port, sizeof port, "%d", j->port);
	struct addrinfo hints, *res = 0;
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo (j->host, port, &hints, &res) == 0 && res)
	{
		fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd >= 0 && connect (fd, res->ai_addr, res->ai_addrlen) != 0) { close (fd); fd = -1; }
		freeaddrinfo (res);
	}
	j->fd = fd;
	__sync_synchronize ();
	j->done = 1;
	__sync_synchronize ();
	if (j->abandoned) { if (fd >= 0) close (fd); delete j; }	// (the transport went first; see close ())
	return 0;
}

class TcpTransport : public mt::Transport
{
public:
	TcpTransport () : m_s (-1), m_job (0), m_in (0), m_n (0), m_cap (0), m_out (0), m_outn (0), m_outcap (0), m_failed (false) {}
	~TcpTransport () { close (); free (m_in); free (m_out); }
	bool open (const char *host, int port) override
	{
		if (g_tgOnlyHost[0] && strcmp (host, g_tgOnlyHost)) { tg_log ("tcp: %s:%d refused (config.ini: test_host)", host, port); return false; }
		tg_log ("tcp: connecting to %s:%d", host, port);
		TcpJob *j = new TcpJob;
		memset (j, 0, sizeof *j);
		snprintf (j->host, sizeof j->host, "%s", host); j->port = port; j->fd = -1;
		pthread_attr_t at; pthread_attr_init (&at);
		pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
		pthread_t th;
		bool ok = pthread_create (&th, &at, tcp_thread, j) == 0;
		pthread_attr_destroy (&at);
		if (!ok) { delete j; return false; }
		m_job = j;
		static const unsigned char tag[4] = { 0xee, 0xee, 0xee, 0xee };
		return queue (tag, 4);
	}
	bool send (const unsigned char *p, int n) override
	{
		if (m_failed) return false;
		unsigned char h[4] = { (unsigned char) n, (unsigned char) (n >> 8), (unsigned char) (n >> 16), (unsigned char) (n >> 24) };
		if (m_s < 0) return queue (h, 4) && queue (p, n);	// (still connecting)
		return all (h, 4) && all (p, n);
	}
	int recv (tl::Buf &out) override
	{
		if (m_failed) return -1;
		if (m_s < 0 && !connected ()) return m_failed ? -1 : 0;
		for (;;)
		{
			if (m_n >= 4)
			{
				int len = (int) ((unsigned) m_in[0] | ((unsigned) m_in[1] << 8) | ((unsigned) m_in[2] << 16) | ((unsigned) m_in[3] << 24));
				if (len < 0 || len > (16 << 20)) return -1;
				if (m_n >= 4 + len)
				{
					out.put (m_in + 4, len);
					memmove (m_in, m_in + 4 + len, (size_t) (m_n - 4 - len));
					m_n -= 4 + len;
					return 1;
				}
				if (m_cap < 4 + len && !grow (4 + len)) return -1;
			}
			if (m_cap - m_n < 4096 && !grow (m_n + 65536)) return -1;
			int r = (int) ::recv (m_s, m_in + m_n, (size_t) (m_cap - m_n), MSG_DONTWAIT);
			if (r == 0) return -1;
			if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
			m_n += r;
		}
	}
	void close () override
	{
		if (m_job)
		{
			m_job->abandoned = 1;
			__sync_synchronize ();
			if (m_job->done) { if (m_job->fd >= 0) ::close (m_job->fd); delete m_job; }	// (else the thread does it)
			m_job = 0;
		}
		if (m_s >= 0) ::close (m_s);
		m_s = -1; m_n = 0; m_outn = 0;
	}
private:
	int m_s;
	TcpJob *m_job;
	unsigned char *m_in;
	int m_n, m_cap;
	unsigned char *m_out;				// what was sent before the connection was there
	int m_outn, m_outcap;
	bool m_failed;

	// The thread has answered? -> true: the socket is there, what waited is sent.
	bool connected ()
	{
		if (!m_job || !m_job->done) return false;
		__sync_synchronize ();
		m_s = m_job->fd;
		delete m_job; m_job = 0;
		if (m_s < 0) { m_failed = true; tg_log ("tcp: the connection failed"); return false; }
		tg_log ("tcp: connected");
		if (!all (m_out, m_outn)) { m_failed = true; return false; }
		m_outn = 0;
		return true;
	}
	bool queue (const unsigned char *p, int n)
	{
		if (m_outn + n > m_outcap)
		{
			int c = (m_outn + n) * 2 + 256;
			unsigned char *x = (unsigned char *) realloc (m_out, (size_t) c);
			if (!x) return false;
			m_out = x; m_outcap = c;
		}
		memcpy (m_out + m_outn, p, (size_t) n);
		m_outn += n;
		return true;
	}
	bool grow (int c)
	{
		if (c <= m_cap) return true;
		unsigned char *x = (unsigned char *) realloc (m_in, (size_t) c);
		if (!x) return false;
		m_in = x; m_cap = c;
		return true;
	}
	bool all (const unsigned char *p, int n)
	{
		while (n > 0)
		{
			int r = (int) ::send (m_s, p, (size_t) n, 0);
			if (r <= 0) return false;
			p += r; n -= r;
		}
		return true;
	}
};

static mt::Transport *tg_new_transport () { return new TcpTransport; }

#endif
