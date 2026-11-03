//
// irc.h -- IRC's state and protocol, apart from its window: the conversations, the users of a channel, the rooms
// (LIST), the socket, the server's lines, what you type. The engine of Onyx's irc (user/Apps/irc/main.cpp), its calls
// to kapi_* replaced by the POSIX ones; the text stays UTF-8 from the wire to the screen (Onyx: Latin-1).
//
// The window (main.cpp) asks to be told by the g_*Dirty flags, and answers the few functions at the end of "the
// state". The connection is made by a thread (the name's lookup and connect () block); everything else stays on the
// window's thread: the socket is read without waiting, once a turn of the loop.
//
#ifndef _irc_h
#define _irc_h

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

namespace irc {

enum { MAXLINES = 400, MAXBUF = 40, MAXROOMS = 15000, TEXT_MAX = 640 };
#define HOME_DIR	"SD0:/home/irc"
#define HISTPATH	HOME_DIR "/servers.txt"
#define NICKPATH	HOME_DIR "/nick.txt"
#define CONFIGPATH	HOME_DIR "/config.ini"

// ---- tiny string helpers --------------------------------------------------------------------------------------
static int  slen (const char *s) { return (int) strlen (s); }
static void scat (char *dst, int cap, const char *src)
{
	int d = slen (dst), i = 0;
	while (src[i] && d + 1 < cap) dst[d++] = src[i++];
	// (a UTF-8 character cut at the end: off)
	if (src[i]) while (d > 0 && ((unsigned char) dst[d - 1] & 0xC0) == 0x80) d--;
	if (src[i] && d > 0 && (unsigned char) dst[d - 1] >= 0xC0) d--;
	dst[d] = '\0';
}
static void scpy (char *dst, int cap, const char *src) { dst[0] = '\0'; scat (dst, cap, src); }
static void scatn (char *dst, int cap, int v) { char b[16]; snprintf (b, sizeof b, "%d", v); scat (dst, cap, b); }
static char lc (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static bool ieq (const char *a, const char *b)		// ASCII case-insensitive (nicks, channels)
{
	while (*a && lc (*a) == lc (*b)) { a++; b++; }
	return *a == '\0' && *b == '\0';
}
static bool istarts (const char *s, const char *w) { while (*w) if (lc (*s++) != lc (*w++)) return false; return true; }
static bool icontains (const char *s, const char *needle)
{
	if (!needle[0]) return true;
	for (; *s; s++)
	{
		int k = 0;
		while (needle[k] && lc (s[k]) == lc (needle[k])) k++;
		if (!needle[k]) return true;
	}
	return false;
}
static bool is_chan (const char *s) { return s[0] == '#' || s[0] == '&' || s[0] == '+' || s[0] == '!'; }
static bool is_word_char (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '[' || c == ']' || c == '\\' || c == '`' || c == '^' || c == '{' || c == '}' || c == '|'; }
static bool mentions (const char *text, const char *nick)	// nick as a word in text
{
	int n = slen (nick);
	if (n == 0) return false;
	for (int i = 0; text[i]; i++)
	{
		int k = 0;
		while (k < n && text[i + k] && lc (text[i + k]) == lc (nick[k])) k++;
		if (k == n && (i == 0 || !is_word_char (text[i - 1])) && !is_word_char (text[i + n])) return true;
	}
	return false;
}

// The wire's bytes -> UTF-8: mIRC's formatting codes and the tabs dropped; a byte that is no UTF-8 is an old client's
// Latin-1, made UTF-8.
static void from_wire (const char *in, char *out, int cap)
{
	int o = 0;
	const unsigned char *s = (const unsigned char *) in;
	while (*s && o + 4 < cap)
	{
		unsigned c = *s;
		if (c == 3)					// ^C colour: digits[,digits]
		{
			s++;
			for (int k = 0; k < 2 && *s >= '0' && *s <= '9'; k++) s++;
			if (*s == ',' && s[1] >= '0' && s[1] <= '9') { s++; for (int k = 0; k < 2 && *s >= '0' && *s <= '9'; k++) s++; }
			continue;
		}
		if (c == 2 || c == 0x0F || c == 0x16 || c == 0x1D || c == 0x1F || c == 0x1E || c == 0x11) { s++; continue; }
		if (c == '\t') { out[o++] = ' '; s++; continue; }
		if (c < 0x80) { out[o++] = (char) c; s++; continue; }
		int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		bool ok = n > 0;
		for (int k = 1; ok && k <= n; k++) if ((s[k] & 0xC0) != 0x80) ok = false;
		if (!ok) { out[o++] = (char) (0xC0 | c >> 6); out[o++] = (char) (0x80 | (c & 63)); s++; continue; }
		for (int k = 0; k <= n; k++) out[o++] = (char) s[k];
		s += n + 1;
	}
	out[o] = '\0';
}

static int now_mins (void)
{
	time_t t = time (0);
	struct tm m;
	localtime_r (&t, &m);
	return m.tm_hour * 60 + m.tm_min;
}
static unsigned ticks (void)					// (milliseconds)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned) (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
static void fmt_time (int mins, char *o)			// "14:02"
{
	o[0] = (char) ('0' + mins / 600); o[1] = (char) ('0' + mins / 60 % 10); o[2] = ':';
	o[3] = (char) ('0' + mins % 60 / 10); o[4] = (char) ('0' + mins % 10); o[5] = '\0';
}

// ---- the state --------------------------------------------------------------------------------------------------
enum { LK_MSG, LK_ACTION, LK_NOTICE, LK_EVENT, LK_INFO, LK_ERROR };
// (an event's sub-kind: arrivals and departures are folded in one row by the channel view)
enum { JE_NONE, JE_JOIN, JE_PART, JE_QUIT, JE_NICK };
struct Line { unsigned char kind, self, mention, ev, open; short mins; short h, hw; char nick[32]; char text[TEXT_MAX]; };
struct Nick { char pfx; char name[31]; };
enum { BK_SERVER, BK_CHAN, BK_QUERY };

struct Buf
{
	char  name[64]; int kind; bool joined, namesOpen, peerGone, rejoin;
	Line *lines; int first, count;
	int   unread, mentions;
	char  topic[TEXT_MAX];
	Nick *nicks; int nnick, capn;
	int   scroll, lastTotal;	// px up from the newest; the view's height when it was last laid out
	int   seen, mark;		// the lines looked at when it was left; the line under the "new" mark, -1: none
	int   below;			// messages come in while scrolled up
	long  win;			// a private conversation's window (0: none)
};
static Buf *g_buf[MAXBUF];
static int  g_nbuf = 0, g_cur = 0;

// Connection + config.
static char g_server[80]  = "irc.libera.chat";
static char g_nick[32]    = "armpi";			// the nickname in use (the server's word after 001)
static char g_user[32]    = "armpi";
static char g_real[64]    = "ARMPI IRC user";
static char g_autojoin[128] = "";
static char g_password[64] = "";
static unsigned g_port    = 6667;
static int  g_sock = -1;
enum { ST_OFFLINE, ST_CONNECTING, ST_REGISTERING, ST_ONLINE };
static int  g_state = ST_OFFLINE;
static char g_rx[2048];
static char g_lost[160];				// why the connection is not there ("" : nothing to say)
static int  g_rxlen = 0;
static char g_hist[8][96];				// the servers used, the latest first
static int  g_histn = 0;

// Rooms (LIST).
struct Room { char name[48]; int users; char topic[160]; };
static Room *g_rooms = 0;
static int  *g_view = 0;
static int   g_nrooms = 0, g_nview = 0;
static bool  g_listing = false;

static bool g_sideDirty = true, g_usersDirty = true, g_viewDirty = true, g_headDirty = true, g_statusDirty = true, g_roomsDirty = false;

// (main.cpp and the views)
static void state_changed (void);			// connected, offline: the bar, the conversation's header, the windows
static void show_conv (int i);				// a conversation shown (a channel joined)
static void pm_line_added (Buf *b);			// a line in a private conversation: its window follows
static void pm_state (Buf *b);				// its header
static void set_nick_field (const char *nick);

// ---- conversations -----------------------------------------------------------------------------------------------
static int buf_find (const char *name)
{
	for (int i = 0; i < g_nbuf; i++) if (ieq (g_buf[i]->name, name)) return i;
	return -1;
}
static int buf_get (const char *name, int kind)
{
	int i = buf_find (name);
	if (i >= 0) return i;
	if (g_nbuf >= MAXBUF) return 0;				// (full: the server's)
	Buf *b = new Buf;
	scpy (b->name, sizeof b->name, name);
	b->kind = kind; b->joined = b->namesOpen = b->peerGone = b->rejoin = false;
	b->lines = new Line[MAXLINES]; b->first = b->count = 0;
	b->unread = b->mentions = 0; b->topic[0] = '\0';
	b->nicks = 0; b->nnick = b->capn = 0; b->scroll = b->lastTotal = 0; b->seen = 0; b->mark = -1; b->below = 0;
	b->win = 0;
	g_buf[g_nbuf++] = b;
	g_sideDirty = true;
	return g_nbuf - 1;
}
static void buf_remove (int i)
{
	if (i <= 0 || i >= g_nbuf) return;			// (never the server's)
	Buf *b = g_buf[i];
	delete [] b->lines; delete [] b->nicks; delete b;
	for (int k = i; k + 1 < g_nbuf; k++) g_buf[k] = g_buf[k + 1];
	g_nbuf--;
	if (g_cur >= i) g_cur = g_cur > 0 ? g_cur - 1 : 0;
	g_sideDirty = g_usersDirty = g_viewDirty = g_headDirty = true;
}
static Line &buf_line (Buf *b, int i) { return b->lines[(b->first + i) % MAXLINES]; }

// A line into a conversation (the text already UTF-8).
static void add_line (int bi, int kind, const char *nick, const char *text, bool self = false, int ev = JE_NONE)
{
	if (bi < 0 || bi >= g_nbuf) bi = 0;
	Buf *b = g_buf[bi];
	int idx;
	if (b->count < MAXLINES) idx = (b->first + b->count++) % MAXLINES;
	else { idx = b->first; b->first = (b->first + 1) % MAXLINES; if (b->seen > 0) b->seen--; if (b->mark >= 0) b->mark--; }
	Line &l = b->lines[idx];
	l.kind = (unsigned char) kind; l.self = self; l.ev = (unsigned char) ev; l.open = 0; l.mins = (short) now_mins (); l.h = 0; l.hw = 0;
	scpy (l.nick, sizeof l.nick, nick ? nick : "");
	scpy (l.text, sizeof l.text, text);
	l.mention = !self && (kind == LK_MSG || kind == LK_ACTION) && b->kind == BK_CHAN && mentions (text, g_nick);
	bool shown = b->kind == BK_QUERY ? b->win != 0 : bi == g_cur;
	if (!shown && (kind == LK_MSG || kind == LK_ACTION || (kind == LK_NOTICE && b->kind != BK_SERVER)))
	{
		b->unread++;
		if (l.mention || b->kind == BK_QUERY) b->mentions++;
		g_sideDirty = true;
	}
	if (bi == g_cur && b->scroll > 0 && (kind == LK_MSG || kind == LK_ACTION)) b->below++;
	if (b->kind == BK_QUERY) pm_line_added (b);
	if (bi == g_cur) g_viewDirty = true;
}
static void info (const char *text, int kind = LK_INFO) { add_line (0, kind, "", text); }
static void info_here (const char *text, int kind = LK_ERROR) { add_line (g_cur, kind, "", text); }

// ---- a channel's users -------------------------------------------------------------------------------------------
static int pfx_rank (char p) { return p == '~' ? 0 : p == '&' ? 1 : p == '@' ? 2 : p == '%' ? 3 : p == '+' ? 4 : 5; }
static int nick_index (Buf *b, const char *name)
{
	for (int i = 0; i < b->nnick; i++) if (ieq (b->nicks[i].name, name)) return i;
	return -1;
}
static void nick_add (Buf *b, const char *raw)
{
	char pfx = 0;
	while (*raw == '~' || *raw == '&' || *raw == '@' || *raw == '%' || *raw == '+')
	{ if (!pfx || pfx_rank (*raw) < pfx_rank (pfx)) pfx = *raw; raw++; }
	if (!*raw) return;
	int i = nick_index (b, raw);
	if (i >= 0) { if (pfx) b->nicks[i].pfx = pfx; return; }
	if (b->nnick == b->capn)
	{
		int nc = b->capn ? b->capn * 2 : 64;
		Nick *n = new Nick[nc];
		for (int k = 0; k < b->nnick; k++) n[k] = b->nicks[k];
		delete [] b->nicks; b->nicks = n; b->capn = nc;
	}
	Nick &n = b->nicks[b->nnick++];
	n.pfx = pfx; scpy (n.name, sizeof n.name, raw);
	if (g_buf[g_cur] == b) g_usersDirty = true;
}
static bool nick_del (Buf *b, const char *name)
{
	int i = nick_index (b, name);
	if (i < 0) return false;
	b->nicks[i] = b->nicks[--b->nnick];
	if (g_buf[g_cur] == b) g_usersDirty = true;
	return true;
}
static bool nick_less (const Nick &a, const Nick &b)
{
	int ra = pfx_rank (a.pfx), rb = pfx_rank (b.pfx);
	if (ra != rb) return ra < rb;
	const char *x = a.name, *y = b.name;
	while (*x && lc (*x) == lc (*y)) { x++; y++; }
	return lc (*x) < lc (*y);
}
static void nick_sort (Buf *b)				// (shell sort: a thousand users is quick)
{
	for (int gap = b->nnick / 2; gap > 0; gap /= 2)
		for (int i = gap; i < b->nnick; i++)
		{
			Nick t = b->nicks[i]; int j = i;
			while (j >= gap && nick_less (t, b->nicks[j - gap])) { b->nicks[j] = b->nicks[j - gap]; j -= gap; }
			b->nicks[j] = t;
		}
}

// ---- the socket ----------------------------------------------------------------------------------------------------
// The connection's thread: the name looked up and connect (), both blocking. It writes its answer in s_conn and goes.
static volatile int s_connDone, s_connWant;		// answered; the connection wanted (one given up: closed by its thread)
static int s_connFd;
static pthread_t s_connThread;
struct ConnJob { char host[80]; unsigned port; int serial; };

static void *conn_thread (void *a)
{
	ConnJob *j = (ConnJob *) a;
	int fd = -1;
	char port[12]; snprintf (port, sizeof port, "%u", j->port);
	struct addrinfo hints, *res = 0;
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo (j->host, port, &hints, &res) == 0 && res)
	{
		fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd >= 0 && connect (fd, res->ai_addr, res->ai_addrlen) != 0) { close (fd); fd = -1; }
		freeaddrinfo (res);
	}
	if (j->serial != s_connWant) { if (fd >= 0) close (fd); delete j; return 0; }	// (given up meanwhile)
	s_connFd = fd;
	delete j;
	__sync_synchronize ();
	s_connDone = 1;
	return 0;
}
static bool conn_start (const char *host, unsigned port)
{
	ConnJob *j = new ConnJob;
	scpy (j->host, sizeof j->host, host); j->port = port; j->serial = s_connWant + 1; s_connWant = j->serial;
	s_connDone = 0;
	pthread_attr_t at; pthread_attr_init (&at);
	pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
	bool ok = pthread_create (&s_connThread, &at, conn_thread, j) == 0;
	pthread_attr_destroy (&at);
	if (!ok) delete j;
	return ok;
}

static void send_all (const char *buf, int len)
{
	if (g_sock < 0) return;
	int off = 0;
	while (off < len)
	{
		int n = (int) send (g_sock, buf + off, (size_t) (len - off), 0);
		if (n <= 0) break;
		off += n;
	}
}
// One IRC line + CRLF in a SINGLE send (a "\r\n" sent apart got delayed: the server dropped the line).
static void send_line (const char *s)
{
	char buf[1024]; int n = 0;
	for (int i = 0; s[i] != '\0' && n < 510; i++) buf[n++] = s[i];
	buf[n++] = '\r'; buf[n++] = '\n';
	send_all (buf, n);
}

// ---- the parser --------------------------------------------------------------------------------------------------
struct Msg { char nick[64]; char *cmd; char *p[16]; int np; };

static bool parse (char *line, Msg &m)
{
	m.nick[0] = '\0'; m.np = 0; m.cmd = 0;
	char *s = line;
	if (*s == '@') { while (*s && *s != ' ') s++; while (*s == ' ') s++; }	// (IRCv3 tags: skipped)
	if (*s == ':')
	{
		s++;
		int i = 0; while (*s && *s != ' ' && *s != '!' && i < 63) m.nick[i++] = *s++;
		m.nick[i] = '\0';
		while (*s && *s != ' ') s++;
		while (*s == ' ') s++;
	}
	if (!*s) return false;
	m.cmd = s;
	while (*s && *s != ' ') s++;
	if (*s) *s++ = '\0';
	while (*s && m.np < 16)
	{
		while (*s == ' ') s++;
		if (!*s) break;
		if (*s == ':') { m.p[m.np++] = s + 1; break; }
		m.p[m.np++] = s;
		while (*s && *s != ' ') s++;
		if (*s) *s++ = '\0';
	}
	return true;
}
static const char *arg (Msg &m, int i) { return i >= 0 && i < m.np ? m.p[i] : ""; }
static bool from_server (Msg &m) { for (int i = 0; m.nick[i]; i++) if (m.nick[i] == '.') return true; return m.nick[0] == '\0'; }

// ---- rooms -----------------------------------------------------------------------------------------------------
static void rooms_request (void)
{
	if (g_sock < 0 || g_state != ST_ONLINE) return;
	g_nrooms = 0; g_listing = true;
	send_line ("LIST");
	g_roomsDirty = true;
}
static void join_chan (const char *ch)
{
	if (g_sock < 0 || g_state != ST_ONLINE) { info_here (TR ("Not connected.")); return; }
	char j[160] = "JOIN "; scat (j, sizeof j, ch); send_line (j);
}

// ---- the server's lines ----------------------------------------------------------------------------------------
static void chan_event (int bi, const char *text) { add_line (bi, LK_EVENT, "", text); }

static void handle_line (char *raw)
{
	if (!raw[0]) return;
	Msg m;
	if (!parse (raw, m)) return;
	const char *cmd = m.cmd;

	if (!strcmp (cmd, "PING")) { char b[600] = "PONG :"; scat (b, sizeof b, arg (m, 0)); send_line (b); return; }

	char text[TEXT_MAX]; from_wire (arg (m, m.np - 1), text, sizeof text);
	char t[TEXT_MAX + 80];

	if (!strcmp (cmd, "PRIVMSG") || !strcmp (cmd, "NOTICE"))
	{
		bool notice = cmd[0] == 'N';
		const char *target = arg (m, 0);
		int bi;
		if (notice && (from_server (m) || ieq (target, "*"))) bi = 0;
		else if (is_chan (target)) bi = buf_get (target, BK_CHAN);
		else if (notice) bi = (ieq (m.nick, "NickServ") || ieq (m.nick, "ChanServ")) ? (g_buf[g_cur]->kind == BK_CHAN ? g_cur : 0) : buf_find (m.nick) >= 0 ? buf_find (m.nick) : g_cur;
		else bi = buf_get (m.nick, BK_QUERY);
		if (text[0] == '\x01')					// CTCP
		{
			char *c = text + 1; int k = 0; while (c[k] && c[k] != '\x01') k++; c[k] = '\0';
			if (!strncmp (c, "ACTION", 6))
			{
				add_line (bi, LK_ACTION, m.nick, c[6] == ' ' ? c + 7 : c + 6);
			}
			else if (!notice && !strncmp (c, "VER", 3))
			{ char r[160] = "NOTICE "; scat (r, sizeof r, m.nick); scat (r, sizeof r, " :\x01VERSION ARMPI IRC\x01"); send_line (r); }
			return;
		}
		add_line (bi, notice ? LK_NOTICE : LK_MSG, m.nick[0] ? m.nick : "server", text);
		return;
	}
	if (!strcmp (cmd, "JOIN"))
	{
		const char *ch = arg (m, 0);
		int bi = buf_get (ch, BK_CHAN);
		Buf *b = g_buf[bi];
		if (ieq (m.nick, g_nick))
		{
			b->joined = true; b->nnick = 0;
			b->rejoin = false;
			snprintf (t, sizeof t, TR ("You joined %s"), ch); chan_event (bi, t);
			show_conv (bi);
		}
		else { nick_add (b, m.nick); add_line (bi, LK_EVENT, m.nick, "", false, JE_JOIN); }
		g_sideDirty = true;
		return;
	}
	if (!strcmp (cmd, "PART") || !strcmp (cmd, "KICK"))
	{
		bool kick = cmd[0] == 'K';
		int bi = buf_find (arg (m, 0));
		if (bi < 0) return;
		const char *who = kick ? arg (m, 1) : m.nick;
		if (ieq (who, g_nick)) { g_buf[bi]->joined = false; g_buf[bi]->nnick = 0; g_usersDirty = g_sideDirty = true; }
		else nick_del (g_buf[bi], who);
		const char *why = m.np > (kick ? 2 : 1) ? text : "";
		if (kick)
		{
			snprintf (t, sizeof t, TR ("%s was kicked by %s"), who, m.nick);
			if (why[0]) { scat (t, sizeof t, " ("); scat (t, sizeof t, why); scat (t, sizeof t, ")"); }
			chan_event (bi, t);
		}
		else add_line (bi, LK_EVENT, who, why, false, JE_PART);
		if (bi == g_cur) g_headDirty = true;
		return;
	}
	if (!strcmp (cmd, "QUIT"))
	{
		for (int i = 0; i < g_nbuf; i++)
			if ((g_buf[i]->kind == BK_CHAN && nick_del (g_buf[i], m.nick)) || (g_buf[i]->kind == BK_QUERY && ieq (g_buf[i]->name, m.nick)))
			{
				add_line (i, LK_EVENT, m.nick, text, false, JE_QUIT);
				if (g_buf[i]->kind == BK_QUERY) { g_buf[i]->peerGone = true; g_sideDirty = true; pm_state (g_buf[i]); }
			}
		return;
	}
	if (!strcmp (cmd, "NICK"))
	{
		const char *nn = arg (m, 0);
		bool me = ieq (m.nick, g_nick);
		if (me) snprintf (t, sizeof t, TR ("You are now known as %s"), nn);
		for (int i = 0; i < g_nbuf; i++)
		{
			Buf *b = g_buf[i];
			int k = b->kind == BK_CHAN ? nick_index (b, m.nick) : -1;
			if (k >= 0) { scpy (b->nicks[k].name, sizeof b->nicks[k].name, nn); add_line (i, LK_EVENT, m.nick, nn, false, JE_NICK); if (i == g_cur) g_usersDirty = true; }
			else if (b->kind == BK_QUERY && ieq (b->name, m.nick))
			{ scpy (b->name, sizeof b->name, nn); add_line (i, LK_EVENT, m.nick, nn, false, JE_NICK); pm_state (b); g_sideDirty = true; }
		}
		if (me) { scpy (g_nick, sizeof g_nick, nn); set_nick_field (nn); info (t, LK_EVENT); state_changed (); }
		return;
	}
	if (!strcmp (cmd, "TOPIC"))
	{
		int bi = buf_find (arg (m, 0));
		if (bi < 0) return;
		scpy (g_buf[bi]->topic, sizeof g_buf[bi]->topic, text);
		snprintf (t, sizeof t, TR ("%s set the topic: %s"), m.nick, text);
		chan_event (bi, t);
		if (bi == g_cur) g_headDirty = true;
		return;
	}
	if (!strcmp (cmd, "MODE"))
	{
		const char *ch = arg (m, 0);
		int bi = buf_find (ch);
		if (bi < 0 || !is_chan (ch)) return;
		Buf *b = g_buf[bi];
		snprintf (t, sizeof t, TR ("%s sets mode"), m.nick);
		for (int i = 1; i < m.np; i++) { scat (t, sizeof t, " "); scat (t, sizeof t, m.p[i]); }
		chan_event (bi, t);
		const char *md = arg (m, 1); bool plus = true; int pi = 2;	// (+o / +v: the users' marks)
		for (; *md; md++)
		{
			if (*md == '+' || *md == '-') { plus = *md == '+'; continue; }
			if (*md == 'o' || *md == 'v' || *md == 'h')
			{
				int k = nick_index (b, arg (m, pi++));
				char pf = *md == 'o' ? '@' : *md == 'h' ? '%' : '+';
				if (k >= 0) { if (plus) { if (pfx_rank (pf) < pfx_rank (b->nicks[k].pfx)) b->nicks[k].pfx = pf; } else if (b->nicks[k].pfx == pf) b->nicks[k].pfx = 0; }
				if (bi == g_cur) g_usersDirty = true;
			}
			else if (*md == 'b' || *md == 'k' || *md == 'e' || *md == 'I' || (*md == 'l' && plus)) pi++;
		}
		return;
	}
	if (!strcmp (cmd, "ERROR")) { info (text, LK_ERROR); return; }

	// Numerics.
	if (cmd[0] >= '0' && cmd[0] <= '9')
	{
		int num = (cmd[0] - '0') * 100 + (cmd[1] - '0') * 10 + (cmd[2] - '0');
		switch (num)
		{
		case 1:							// welcome: registered
		{
			g_state = ST_ONLINE;
			scpy (g_nick, sizeof g_nick, arg (m, 0)); set_nick_field (g_nick);
			info (text);
			if (g_password[0])
			{
				char l[160] = "PRIVMSG NickServ :IDENTIFY "; scat (l, sizeof l, g_nick); scat (l, sizeof l, " "); scat (l, sizeof l, g_password);
				send_line (l); info (TR ("Identifying with NickServ..."), LK_EVENT);
			}
			if (g_autojoin[0]) join_chan (g_autojoin);
			for (int i = 1; i < g_nbuf; i++) if (g_buf[i]->kind == BK_CHAN && g_buf[i]->rejoin) join_chan (g_buf[i]->name);	// (a reconnection)
			state_changed ();
			return;
		}
		case 433:						// the nickname is taken
		case 432: case 436:
		{
			scpy (t, sizeof t, arg (m, 1)); scat (t, sizeof t, ": "); scat (t, sizeof t, text);
			info (t, LK_ERROR);
			if (g_state == ST_REGISTERING && num == 433)	// (registering: try another one)
			{
				char nn[32]; scpy (nn, sizeof nn, arg (m, 1));
				int n = slen (nn);
				if (n < 15) { nn[n] = '_'; nn[n + 1] = '\0'; }
				else nn[n - 1] = (char) ('0' + (ticks () % 10));
				char l[64] = "NICK "; scat (l, sizeof l, nn); send_line (l);
				snprintf (t, sizeof t, TR ("Trying the nickname %s"), nn); info (t, LK_EVENT);
			}
			else if (g_buf[g_cur]->kind != BK_SERVER) info_here (t);
			return;
		}
		case 331: case 332:					// the topic
		{
			int bi = buf_find (arg (m, 1));
			if (bi < 0) return;
			scpy (g_buf[bi]->topic, sizeof g_buf[bi]->topic, num == 332 ? text : "");
			if (bi == g_cur) g_headDirty = true;
			return;
		}
		case 333: case 366: case 329: case 315:			// (the topic's author, the end of the names...)
			if (num == 366) { int bi = buf_find (arg (m, 1)); if (bi >= 0) g_buf[bi]->namesOpen = false; }
			return;
		case 353:						// the names: "= #chan :@bob alice"
		{
			int bi = buf_find (arg (m, 2));
			if (bi < 0) return;
			Buf *b = g_buf[bi];
			if (!b->namesOpen) { b->namesOpen = true; b->nnick = 0; }
			char *s = m.p[m.np - 1];
			while (*s)
			{
				while (*s == ' ') s++;
				char *w = s; while (*s && *s != ' ') s++;
				char sv = *s; *s = '\0';
				if (*w) nick_add (b, w);
				*s = sv;
			}
			if (bi == g_cur) g_usersDirty = true;
			return;
		}
		case 321: g_nrooms = 0; g_listing = true; return;	// the LIST
		case 322:
		{
			if (!g_rooms || g_nrooms >= MAXROOMS) return;
			Room &r = g_rooms[g_nrooms++];
			scpy (r.name, sizeof r.name, arg (m, 1));
			r.users = 0; for (const char *u = arg (m, 2); *u >= '0' && *u <= '9'; u++) r.users = r.users * 10 + (*u - '0');
			const char *tp = text;
			if (tp[0] == '[' && tp[1] == '+') { while (*tp && *tp != ']') tp++; if (*tp) tp++; while (*tp == ' ') tp++; }	// ("[+nt] ": the modes)
			scpy (r.topic, sizeof r.topic, tp);
			return;
		}
		case 323: g_listing = false; g_roomsDirty = true; return;
		case 375: case 372: case 376: case 2: case 3: case 4: case 5: case 250: case 251: case 252: case 253:
		case 254: case 255: case 265: case 266: case 396: case 900:
		{
			if (num == 4 || num == 5)			// (the server's version, its features: the words)
			{
				t[0] = '\0';
				for (int i = 1; i < m.np; i++) { if (i > 1) scat (t, sizeof t, " "); char w[TEXT_MAX]; from_wire (m.p[i], w, sizeof w); scat (t, sizeof t, w); }
				info (t);
			}
			else info (text);
			return;
		}
		default:
			if (num >= 400)					// an error: where you are, and the server's
			{
				t[0] = '\0';
				if (m.np > 2) { scat (t, sizeof t, arg (m, 1)); scat (t, sizeof t, ": "); }
				scat (t, sizeof t, text);
				info (t, LK_ERROR);
				if (g_buf[g_cur]->kind != BK_SERVER) info_here (t);
			}
			else
			{
				t[0] = '\0';
				for (int i = 1; i < m.np; i++) { if (i > 1) scat (t, sizeof t, " "); char w[TEXT_MAX]; from_wire (m.p[i], w, sizeof w); scat (t, sizeof t, w); }
				info (t);
			}
			return;
		}
	}
	// Anything else: the server's buffer.
	scpy (t, sizeof t, cmd); scat (t, sizeof t, " "); scat (t, sizeof t, text);
	info (t);
}

static void go_offline (const char *why)
{
	if (g_sock >= 0) { close (g_sock); g_sock = -1; }
	s_connWant = s_connWant + 1;				// (a connection under way: not wanted any more)
	g_state = ST_OFFLINE;
	info (why, LK_ERROR);
	scpy (g_lost, sizeof g_lost, why);
	for (int i = 1; i < g_nbuf; i++)
		if (g_buf[i]->kind == BK_CHAN && g_buf[i]->joined) { g_buf[i]->joined = false; g_buf[i]->rejoin = true; g_buf[i]->nnick = 0; chan_event (i, why); }
	g_usersDirty = g_sideDirty = true; g_listing = false; g_rxlen = 0;
	state_changed ();
}

static void drain_socket (void)
{
	if (g_sock < 0) return;
	char b[1600]; int n = 0, guard = 0;
	while (guard++ < 16 && (n = (int) recv (g_sock, b, sizeof b, MSG_DONTWAIT)) > 0)
		for (int k = 0; k < n; k++)
		{
			char c = b[k];
			if (c == '\r') continue;
			if (c == '\n') { g_rx[g_rxlen] = '\0'; handle_line (g_rx); g_rxlen = 0; }
			else if (g_rxlen < (int) sizeof g_rx - 1) g_rx[g_rxlen++] = c;
			if (g_sock < 0) return;
		}
	if (guard <= 16 && (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))) go_offline (TR ("Disconnected from the server."));
}

// ---- files --------------------------------------------------------------------------------------------------------
static void save_small (const char *path, const char *b, int n)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	if (write (fd, b, (size_t) n) < 0) {}
	close (fd);
}
static int read_small (const char *path, char *buf, int cap)
{
	buf[0] = '\0';
	int fd = open (path, O_RDONLY);
	if (fd < 0) return 0;
	int got = (int) read (fd, buf, (size_t) cap - 1);
	close (fd);
	if (got < 0) got = 0;
	buf[got] = '\0';
	return got;
}
static void hist_save (void)
{
	char buf[8 * 98]; int n = 0;
	for (int i = 0; i < g_histn; i++)
	{
		for (int k = 0; g_hist[i][k] && n < (int) sizeof buf - 2; k++) buf[n++] = g_hist[i][k];
		buf[n++] = '\n';
	}
	save_small (HISTPATH, buf, n);
}
static void hist_load (void)
{
	static char buf[8 * 98];
	read_small (HISTPATH, buf, sizeof buf);
	int i = 0;
	while (buf[i] && g_histn < 8)
	{
		char line[96]; int j = 0;
		while (buf[i] && buf[i] != '\n' && buf[i] != '\r' && j < 95) line[j++] = buf[i++];
		line[j] = '\0';
		while (buf[i] == '\n' || buf[i] == '\r') i++;
		if (j > 0) { scpy (g_hist[g_histn], sizeof g_hist[0], line); g_histn++; }
	}
}
static void hist_add (const char *host, unsigned port)
{
	char entry[96]; scpy (entry, sizeof entry, host);
	if (port != 6667) { scat (entry, sizeof entry, ":"); scatn (entry, sizeof entry, (int) port); }
	int found = -1;
	for (int i = 0; i < g_histn; i++) if (ieq (g_hist[i], entry)) { found = i; break; }
	int start = found >= 0 ? found : (g_histn < 8 ? g_histn : 7);
	for (int i = start; i > 0; i--) scpy (g_hist[i], sizeof g_hist[0], g_hist[i - 1]);
	scpy (g_hist[0], sizeof g_hist[0], entry);
	if (found < 0 && g_histn < 8) g_histn++;
	hist_save ();
}
// SD0:/home/irc/config.ini, Onyx's: "server = x" lines (a [irc] heading, comments: passed over).
static void config_load (void)
{
	static char b[2048];
	if (read_small (CONFIGPATH, b, sizeof b) <= 0) return;
	for (char *l = b; l && *l; )
	{
		char *e = strchr (l, '\n'); if (e) *e = 0;
		char *q = strchr (l, '=');
		if (q && *l != '#' && *l != ';' && *l != '[')
		{
			char *ke = q; while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
			*ke = 0;
			char *v = q + 1; while (*v == ' ' || *v == '\t') v++;
			int n = slen (v); while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\r')) v[--n] = 0;
			if (!strcmp (l, "server")) scpy (g_server, sizeof g_server, v);
			else if (!strcmp (l, "nick")) scpy (g_nick, sizeof g_nick, v);
			else if (!strcmp (l, "user")) scpy (g_user, sizeof g_user, v);
			else if (!strcmp (l, "realname")) scpy (g_real, sizeof g_real, v);
			else if (!strcmp (l, "channel")) scpy (g_autojoin, sizeof g_autojoin, v);
			else if (!strcmp (l, "password")) scpy (g_password, sizeof g_password, v);
			else if (!strcmp (l, "port")) g_port = (unsigned) atoi (v);
		}
		l = e ? e + 1 : 0;
	}
}

} // namespace irc

#endif
