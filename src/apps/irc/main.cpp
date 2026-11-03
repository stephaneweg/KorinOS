//
// irc -- Onyx's IRC client on ARMPI's UIKit, its interface redone (README.md): the conversations at the left under a
// card that says who you are and where, the channel in the middle (or one panel in its place: the rooms, the
// connection, Go to), its users on demand at the right; a private conversation is a window of its own.
//
// Plain-text IRC (port 6667). SD0:/home/irc keeps the servers used, the nickname and config.ini: server, port, nick,
// user, realname, channel, password (NickServ). Commands: /join, /part, /nick, /msg, /query, /me, /topic, /list,
// /server host[:port], /raw, /clear, /quit; anything else goes to the server as it is.
//
#include <initializer_list>
#include <netkit_abi.h>
#include "irc.h"
#include "look.h"
#include "chat.h"
#include "pm.h"

using namespace irc;

namespace irc {

#define W0		880
#define H0		580
#define IN_H		44
#define BANNER_H	28
#define FOOT_H		44
#define CARD_H		40
#define PANEL_H		40
#define HEAD_MAX	40

enum { P_CHAT, P_ROOMS, P_CONNECT, P_GOTO };
static int g_panel = P_CHAT;
static bool g_quit, g_auto, g_netUp;
static int  g_prev = 0;					// the conversation seen before (Alt+left)

static long g_card, g_side, g_bAdd, g_split;
static long g_tb, g_btSide, g_vTitle, g_tbR, g_btUsers, g_btMore;
static long g_banner, g_bannerBtn;
static long g_inRow, g_in, g_send;
static long g_uTitle, g_users, g_pmBtn;
static long g_rp, g_rTitle, g_rClose, g_rFilter, g_rMinL, g_rMin, g_rRefresh, g_rJoin, g_rList;
static long g_cp, g_cTitle, g_cSub, g_cL[4], g_cServer, g_cNick, g_cChans, g_cPass, g_cGo, g_cOff, g_cNote, g_cErr;
static long g_gp, g_gIn, g_gList;
static int  g_sideW = 168, g_w, g_h;
static int  s_band = -1, s_sideWant = -1, s_usersWant = -1;	// the window's width class; a choice by hand (-1: none)
static bool s_sideOn, s_usersOn, s_bannerOn;
static int  s_headH = HEAD_MAX;

static void layout ();
static void submit (char *s);
static void connect_now ();
static void show_panel (int p);
static void rooms_refresh ();

// ---- what a window's parts say ------------------------------------------------------------------------------------
static int total_unread () { int n = 0; for (int i = 0; i < g_nbuf; i++) n += g_buf[i]->unread; return n; }
static void title_sync ()
{
	static int last = -1;
	int n = total_unread ();
	if (n == last) return;
	last = n;
	char t[32] = "IRC";
	if (n) snprintf (t, sizeof t, "(%d) IRC", n);
	ui_title ((long) t);
}
static void state_changed ()
{
	g_headDirty = g_statusDirty = g_sideDirty = g_viewDirty = true;
	for (int i = 0; i < MAXPM; i++) if (g_pm[i].win) g_pm[i].dirty = true;
}
static void set_nick_field (const char *nick) { ui_w_set_text (g_cNick, (long) nick); }

// The conversation shown. A private one is its own window; the list's choice stays on the channel.
static void show_conv (int i)
{
	if (i < 0 || i >= g_nbuf) return;
	if (g_buf[i]->kind == BK_QUERY) { pm_show (g_buf[i]); g_sideDirty = true; return; }
	Buf *o = g_buf[g_cur], *b = g_buf[i];
	if (i != g_cur) { g_prev = g_cur; o->mark = -1; o->seen = o->count; }
	g_cur = i;
	b->mark = b->unread && b->seen < b->count ? b->seen : -1;
	b->unread = b->mentions = 0;
	b->scroll = 0; b->below = 0; b->lastTotal = 0;
	g_sideDirty = g_usersDirty = g_viewDirty = g_headDirty = g_statusDirty = true;
	if (g_panel != P_CHAT) show_panel (P_CHAT);
	else layout ();
	if (s_band == 0 && s_sideOn) { s_sideWant = 0; layout (); }
	ui_w_focus (g_in);
}
static const char *say_fail;
void say (int bi, const char *s, bool action)
{
	Buf *b = g_buf[bi];
	say_fail = 0;
	if (b->kind == BK_SERVER) { info_here (TR ("This is the server's page: pick a channel (or /msg nick text).")); return; }
	if (g_sock < 0 || g_state != ST_ONLINE) { add_line (bi, LK_ERROR, "", TR ("Not connected: the message was not sent.")); return; }
	if (b->kind == BK_CHAN && !b->joined) { info_here (TR ("You are not in this channel: /join it again.")); return; }
	char l[640] = "PRIVMSG "; scat (l, sizeof l, b->name); scat (l, sizeof l, " :");
	if (action) { scat (l, sizeof l, "\x01" "ACTION "); scat (l, sizeof l, s); scat (l, sizeof l, "\x01"); }
	else scat (l, sizeof l, s);
	send_line (l);
	add_line (bi, action ? LK_ACTION : LK_MSG, g_nick, s, true);
	b->scroll = 0;
}

// ---- the card: who you are, where --------------------------------------------------------------------------------
static void card_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (0, h - 1, w, 1, jet (J_LIGNE));
	unsigned dot = g_state == ST_ONLINE ? jet (J_SUCCES) : g_state == ST_OFFLINE ? jet (J_TEXTE3) : jet (J_ALERTE);
	ui_vp_disc (15 * 16, 13 * 16, 4 * 16, dot);
	const char *sv = g_server; if (!strncmp (sv, "irc.", 4)) sv += 4;
	text_box (g_state == ST_OFFLINE ? TR ("Offline") : sv, 25, 3, 18, jet (J_TEXTE1), ui_px (), f_bold (), (int) w - 44);
	text_box (g_nick, 25, 19, 18, jet (J_TEXTE2), PX_SMALL, f_sans (), (int) w - 44);
	text_box ("\xE2\x96\xBE", (int) w - 18, 0, (int) h, jet (J_TEXTE3), PX_SMALL, f_sans (), 0);
}
static int s_cardMenu;
static long card_mouse (long, long kind, long, long, long) { if (kind == 1) s_cardMenu = 1; return 0; }
static void ask_nick ()
{
	char n[32]; scpy (n, sizeof n, g_nick);
	if (!ui_ask ((long) TR ("Nickname"), (long) TR ("Your new nickname:"), (long) n, sizeof n) || !n[0]) return;
	if (g_state == ST_ONLINE) { char l[64] = "NICK "; scat (l, sizeof l, n); send_line (l); }
	else { scpy (g_nick, sizeof g_nick, n); set_nick_field (n); state_changed (); }
}
static void disconnect ()
{
	if (g_sock >= 0) send_line ("QUIT :ARMPI IRC");
	go_offline (TR ("You disconnected."));
	g_lost[0] = 0;
}
static void card_menu ()
{
	const char *conn = TR ("Connection..."), *chg = TR ("Change the nickname..."), *cut = g_state == ST_OFFLINE ? TR ("Reconnect") : TR ("Disconnect");
	char t[200]; int n = 0;
	for (const char *s : { conn, chg, cut }) { strcpy (t + n, s); n += slen (s) + 1; }
	t[n] = 0;
	switch (ui_popup_local (10, CARD_H, (long) t, 200))
	{
	case 0: show_panel (P_CONNECT); break;
	case 1: ask_nick (); break;
	case 2: if (g_state == ST_OFFLINE) connect_now (); else disconnect (); break;
	}
}

// ---- the conversations -----------------------------------------------------------------------------------------------
static int s_rowBuf[MAXBUF + 4], s_nrow;		// a row's conversation (-1: a heading)
static void side_add (int bi, const char *label) { s_rowBuf[s_nrow++] = bi; ui_list_add (g_side, (long) label, bi); }
static void side_rebuild ()
{
	ui_list_clear (g_side); s_nrow = 0;
	side_add (0, TR ("Server"));
	bool any = false;
	for (int pass = 0; pass < 2; pass++)
	{
		any = false;
		for (int i = 1; i < g_nbuf; i++)
		{
			Buf *b = g_buf[i];
			if ((pass == 0) != (b->kind == BK_CHAN)) continue;
			if (!any)
			{
				any = true;
				char h[40]; snprintf (h, sizeof h, "#%s", pass ? TR ("PRIVATE") : TR ("ROOMS"));
				side_add (-1, h);
			}
			side_add (i, bare_name (b->name));
		}
	}
	for (int r = 0; r < s_nrow; r++) if (s_rowBuf[r] == g_cur) ui_w_set_value (g_side, r);
	title_sync ();
}
static long side_cell (long, long row, long, long x0, long y0, long w0, long h0, long sel)
{
	if (row < 0 || row >= s_nrow || s_rowBuf[row] < 0) return 0;
	Buf *b = g_buf[s_rowBuf[row]];
	int x = (int) x0, y = (int) y0, w = (int) w0, h = (int) h0;
	int px = ui_px ();
	unsigned fg = sel ? ink_on (jet (J_ACCENT)) : jet (J_TEXTE2);
	bool off = (b->kind == BK_CHAN && !b->joined) || g_state != ST_ONLINE;
	bool gone = b->kind == BK_QUERY && (b->peerGone || g_state != ST_ONLINE);
	const char *g = b->kind == BK_SERVER ? "\xE2\x8C\x82" : b->kind == BK_CHAN ? "#" : gone ? "\xE2\x97\x8B" : "\xE2\x97\x8F";
	unsigned gi = sel ? fg : b->kind == BK_QUERY && !gone ? jet (J_SUCCES) : jet (J_TEXTE3);
	text_box (g, x + 8, y, h, gi, px, f_sans (), 0);
	int right = x + w - 8;
	if (b->unread)						// the badge
	{
		char t[16];
		if (b->mentions) snprintf (t, sizeof t, "@%d", imin (b->mentions, 99)); else if (b->unread > 99) strcpy (t, "99+"); else snprintf (t, sizeof t, "%d", b->unread);
		int bw = imax (18, text_w (t, PX_SMALL, f_bold ()) + 10);
		unsigned bg = b->mentions ? jet (J_DANGER) : jet (J_LIGNE_FORTE), ink = b->mentions ? ink_on (bg) : jet (J_TEXTE1);
		ui_rbox (right - bw, y + (h - 18) / 2, bw, 18, 9, bg, bg);
		text_center (t, right - bw, y + (h - 18) / 2, bw, 18, ink, PX_SMALL, f_bold ());
		right -= bw + 4;
	}
	long d; const char *name = (const char *) uikit_list_get (g_side, row, &d);
	long f = b->unread ? f_bold () : off && b->kind != BK_SERVER ? f_ital () : f_sans ();
	unsigned ink = sel ? fg : b->unread ? jet (J_TEXTE1) : off && b->kind != BK_SERVER ? jet (J_TEXTE3) : fg;
	text_box (name, x + 26, y, h, ink, px, f, right - x - 26);
	return 1;
}
static void leave_conv (int bi)
{
	if (bi <= 0 || bi >= g_nbuf) return;
	Buf *b = g_buf[bi];
	if (b->kind == BK_CHAN && b->joined && g_sock >= 0) { char j[96] = "PART "; scat (j, sizeof j, b->name); send_line (j); }
	if (b->win) { Pm *m = pm_of (b); if (m) pm_close (m); }
	buf_remove (bi);
	show_conv (g_cur);
}
static void side_menu (int bi)
{
	Buf *b = g_buf[bi];
	if (bi == 0) return;
	const char *one = b->kind == BK_CHAN ? TR ("Leave the room") : TR ("Close the conversation");
	char t[80]; int n = slen (one); strcpy (t, one); t[n + 1] = 0;
	if (ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), (long) t, 200) == 0) leave_conv (bi);
}

// ---- the channel's title, the banner --------------------------------------------------------------------------------
static void title_paint (long, long w, long h)
{
	Buf *b = g_buf[g_cur];
	ui_fill (0, 0, w, h, jet (J_FACE));
	int px = ui_px (), x = 8;
	if (!s_sideOn && total_unread ())
	{
		char t[12]; snprintf (t, sizeof t, "%d", imin (total_unread (), 99));
		int bw = imax (18, text_w (t, PX_SMALL, f_bold ()) + 10);
		ui_rbox (x, ((int) h - 18) / 2, bw, 18, 9, jet (J_DANGER), jet (J_DANGER));
		text_center (t, x, ((int) h - 18) / 2, bw, 18, ink_on (jet (J_DANGER)), PX_SMALL, f_bold ());
		x += bw + 8;
	}
	char t[120], sub[TEXT_MAX + 40];
	if (b->kind == BK_SERVER)
	{
		scpy (t, sizeof t, g_server);
		snprintf (sub, sizeof sub, "%s  \xC2\xB7  %s", TR ("Server messages"), g_nick);
	}
	else
	{
		scpy (t, sizeof t, b->name);
		if (b->nnick) { char n[20]; snprintf (n, sizeof n, " \xC2\xB7 %d", b->nnick); scat (t, sizeof t, n); }
		scpy (sub, sizeof sub, b->topic[0] ? b->topic : b->joined ? TR ("No topic") : TR ("You are not in this room"));
	}
	if (h < HEAD_MAX) { text_box (t, x, 0, (int) h, jet (J_TEXTE1), px, f_bold (), (int) w - x - 4); return; }
	text_box (t, x, 2, 18, jet (J_TEXTE1), px, f_bold (), (int) w - x - 4);
	text_box (sub, x, 19, 18, jet (J_TEXTE2), PX_SMALL, f_sans (), (int) w - x - 4);
}
// What the banner has to say; 0: nothing. *btn: its button's word (0: none).
static const char *banner_text (const char **btn, char *t, int cap)
{
	Buf *b = g_buf[g_cur];
	*btn = 0;
	if (g_state == ST_CONNECTING || g_state == ST_REGISTERING) { snprintf (t, cap, TR ("Connecting to %s..."), g_server); *btn = TR ("Cancel"); return t; }
	if (g_state == ST_OFFLINE)
	{
		if (!g_netUp && (g_auto || g_lost[0])) return TR ("Waiting for the network...");
		if (g_lost[0]) { snprintf (t, cap, TR ("Disconnected from %s"), g_server); *btn = TR ("Reconnect"); return t; }
		return 0;
	}
	if (b->kind == BK_CHAN && !b->joined) { *btn = TR ("Join"); return TR ("You are not in this room any more"); }
	return 0;
}
static void banner_paint (long, long w, long h)
{
	char t[160]; const char *btn;
	const char *s = banner_text (&btn, t, sizeof t);
	unsigned bg = g_state == ST_OFFLINE && g_lost[0] ? mix (jet (J_FACE), jet (J_DANGER), 32) : mix (jet (J_FACE), jet (J_ALERTE), 48);
	ui_fill (0, 0, w, h, bg);
	if (s) text_box (s, 12, 0, (int) h, jet (J_TEXTE1), PX_SMALL, f_sans (), (int) w - 130);
}
static void banner_click ()
{
	Buf *b = g_buf[g_cur];
	if (g_state == ST_CONNECTING || g_state == ST_REGISTERING) { disconnect (); g_auto = false; }
	else if (g_state == ST_OFFLINE) connect_now ();
	else if (b->kind == BK_CHAN) join_chan (b->name);
}
static void input_sync ()
{
	static int last = -2;
	bool ok = g_state == ST_ONLINE;
	int key = ok ? g_cur : -1;
	if (key == last) return;
	last = key;
	ui_w_enable (g_in, ok);
	char hint[100];
	if (ok && g_buf[g_cur]->kind != BK_SERVER) snprintf (hint, sizeof hint, TR ("Message to %s"), g_buf[g_cur]->name);
	else scpy (hint, sizeof hint, ok ? TR ("A command: /join #room") : TR ("Offline"));
	ui_textbox_hint (g_in, (long) hint);
	ui_w_draw (g_in);
}

// ---- a channel's users -----------------------------------------------------------------------------------------------
static long users_text (long, long row)
{
	Buf *b = g_buf[g_cur];
	return row >= 0 && row < b->nnick ? (long) b->nicks[row].name : (long) "";
}
static long users_cell (long, long row, long, long x, long y, long w, long h, long sel)
{
	Buf *b = g_buf[g_cur];
	if (row < 0 || row >= b->nnick) return 0;
	const Nick &n = b->nicks[row];
	char p[2] = { n.pfx, 0 };
	unsigned fg = sel ? ink_on (jet (J_ACCENT)) : jet (J_TEXTE1);
	if (n.pfx) text_box (p, (int) x + 6, (int) y, (int) h, sel ? fg : jet (J_ACCENT), ui_px (), f_bold (), 0);
	text_box (n.name, (int) x + 20, (int) y, (int) h, sel ? fg : nick_color (n.name), ui_px (), f_sans (), (int) w - 24);
	return 1;
}
static void utitle_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	char t[60]; snprintf (t, sizeof t, g_buf[g_cur]->nnick == 1 ? TR ("%d user") : TR ("%d users"), g_buf[g_cur]->nnick);
	text_box (t, 10, 0, (int) h, jet (J_TEXTE2), PX_SMALL, f_bold (), (int) w - 14);
}
static void users_rebuild ()
{
	Buf *b = g_buf[g_cur];
	if (b->kind == BK_CHAN) nick_sort (b);
	ui_list_virtual (g_users, b->kind == BK_CHAN ? b->nnick : 0, (long) users_text);
	ui_view_dirty (g_uTitle);
	ui_view_dirty (g_vTitle);
	ui_w_draw (g_users);
}
static const char *user_selected ()
{
	Buf *b = g_buf[g_cur];
	long i = ui_w_get_value (g_users);
	return i >= 0 && i < b->nnick ? b->nicks[i].name : 0;
}
static void open_query (const char *nick)
{
	if (!*nick || ieq (nick, g_nick)) return;
	pm_show (g_buf[buf_get (nick, BK_QUERY)]);
}

// ---- rooms -------------------------------------------------------------------------------------------------------
static char s_rowText[320]; static int s_rowLast = -1;
static long rooms_text (long, long row)
{
	if (row < 0 || row >= g_nview) return (long) "";
	if (row != s_rowLast)
	{
		const Room &r = g_rooms[g_view[row]];
		int bi = buf_find (r.name);
		snprintf (s_rowText, sizeof s_rowText, "%s%s\t%d\t%s", bi >= 0 && g_buf[bi]->joined ? "\xE2\x9C\x93 " : "", r.name, r.users, r.topic);
		s_rowLast = (int) row;
	}
	return (long) s_rowText;
}
static int s_sortCol = 1; static bool s_sortDesc = true;
static bool room_less (int a, int b)
{
	const Room &x = g_rooms[a], &y = g_rooms[b];
	int c = 0;
	if (s_sortCol == 1) c = x.users < y.users ? -1 : x.users > y.users ? 1 : 0;
	else
	{
		const char *p = s_sortCol == 2 ? x.topic : x.name, *q = s_sortCol == 2 ? y.topic : y.name;
		while (*p && lc (*p) == lc (*q)) { p++; q++; }
		c = lc (*p) < lc (*q) ? -1 : lc (*p) > lc (*q) ? 1 : 0;
	}
	return s_sortDesc ? c > 0 : c < 0;
}
static void rtitle_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (0, h - 1, w, 1, jet (J_LIGNE));
	char t[160], s[120];
	snprintf (t, sizeof t, TR ("Rooms on %s"), g_server);
	if (g_state != ST_ONLINE) scpy (s, sizeof s, TR ("Connect to a server to see its rooms."));
	else if (g_listing) snprintf (s, sizeof s, TR ("Receiving... %d rooms"), g_nrooms);
	else snprintf (s, sizeof s, TR ("%d of %d rooms"), g_nview, g_nrooms);
	text_box (t, 12, 2, 18, jet (J_TEXTE1), ui_px (), f_bold (), (int) w - 60);
	text_box (s, 12, 19, 18, jet (J_TEXTE2), PX_SMALL, f_sans (), (int) w - 60);
}
static void rooms_refresh ()
{
	const char *f = (const char *) ui_w_get_text (g_rFilter);
	int mn = (int) ui_w_get_value (g_rMin);
	g_nview = 0;
	for (int i = 0; i < g_nrooms; i++)
		if (g_rooms[i].users >= mn && (icontains (g_rooms[i].name, f) || icontains (g_rooms[i].topic, f)))
			g_view[g_nview++] = i;
	for (int gap = g_nview / 2; gap > 0; gap /= 2)
		for (int i = gap; i < g_nview; i++)
		{
			int t = g_view[i], j = i;
			while (j >= gap && room_less (t, g_view[j - gap])) { g_view[j] = g_view[j - gap]; j -= gap; }
			g_view[j] = t;
		}
	s_rowLast = -1;
	ui_list_virtual (g_rList, g_nview, (long) rooms_text);
	ui_list_sort (g_rList, s_sortCol, s_sortDesc);
	ui_view_dirty (g_rTitle);
	ui_w_draw (g_rList);
	ui_w_enable (g_rJoin, ui_w_get_value (g_rList) >= 0);
}
static void room_join ()
{
	long i = ui_w_get_value (g_rList);
	if (i < 0 || i >= g_nview) return;
	join_chan (g_rooms[g_view[i]].name);
	show_panel (P_CHAT);
}

// ---- the connection ------------------------------------------------------------------------------------------------
static void hist_options ()
{
	ui_list_clear (g_cServer);
	for (int i = 0; i < g_histn; i++) ui_list_add (g_cServer, (long) g_hist[i], 0);
}
static void config_save ()
{
	char b[1024];
	int n = snprintf (b, sizeof b, "# IRC\nuser = %s\nrealname = %s\nchannel = %s\npassword = %s\n", g_user, g_real, g_autojoin, g_password);
	save_small (CONFIGPATH, b, n);
}
static void server_spec (const char *spec)		// "host[:port]"
{
	while (*spec == ' ') spec++;
	if (*spec)
	{
		char host[80]; int n = 0; unsigned port = 6667;
		while (*spec && *spec != ':' && *spec != ' ' && n < 79) host[n++] = *spec++;
		host[n] = '\0';
		if (*spec == ':') { spec++; port = 0; while (*spec >= '0' && *spec <= '9') port = port * 10 + (unsigned) (*spec++ - '0'); if (!port) port = 6667; }
		scpy (g_server, sizeof g_server, host); g_port = port;
	}
}
// What the connection panel says becomes the connection's.
static void panel_read ()
{
	server_spec ((const char *) ui_w_get_text (g_cServer));
	const char *nk = (const char *) ui_w_get_text (g_cNick);
	if (nk[0])
	{
		char nn[32]; int n = 0;
		for (const char *p = nk; *p && n < 30; p++) if (*p != ' ') nn[n++] = *p;
		nn[n] = '\0';
		if (n) scpy (g_nick, sizeof g_nick, nn);
	}
	scpy (g_autojoin, sizeof g_autojoin, (const char *) ui_w_get_text (g_cChans));
	for (char *p = g_autojoin, *q = g_autojoin; ; p++) { if (*p != ' ') *q++ = *p; if (!*p) break; }	// (JOIN takes "#a,#b")
	scpy (g_password, sizeof g_password, (const char *) ui_w_get_text (g_cPass));
}
static void connect_now ()
{
	if (g_sock >= 0) { send_line ("QUIT :reconnecting"); close (g_sock); g_sock = -1; }
	for (int i = 1; i < g_nbuf; i++) if (g_buf[i]->kind == BK_CHAN) { if (g_buf[i]->joined) g_buf[i]->rejoin = true; g_buf[i]->joined = false; g_buf[i]->nnick = 0; }
	g_nrooms = 0; g_listing = false; g_rxlen = 0; g_lost[0] = 0; g_auto = false;
	char t[160]; snprintf (t, sizeof t, TR ("Connecting to %s:%u..."), g_server, g_port);
	info (t, LK_EVENT);
	g_state = ST_CONNECTING;
	ui_w_set_text (g_cErr, (long) "");
	state_changed ();
	if (g_panel != P_CHAT) show_panel (P_CHAT);
	if (!conn_start (g_server, g_port)) { g_state = ST_OFFLINE; go_offline (TR ("The connection failed.")); }
}
// The connection's thread has answered.
static void net_poll ()
{
	if (!s_connDone) return;
	s_connDone = 0;
	if (g_state != ST_CONNECTING) { if (s_connFd >= 0) close (s_connFd); return; }
	g_sock = s_connFd;
	if (g_sock < 0)
	{
		g_state = ST_OFFLINE;
		char t[160]; snprintf (t, sizeof t, TR ("The server %s did not answer."), g_server);
		info (t, LK_ERROR); scpy (g_lost, sizeof g_lost, t);
		ui_w_set_text (g_cErr, (long) t);
		state_changed ();
		return;
	}
	g_state = ST_REGISTERING;
	char l[160], t[160];
	scpy (l, sizeof l, "NICK "); scat (l, sizeof l, g_nick); send_line (l);
	scpy (l, sizeof l, "USER "); scat (l, sizeof l, g_user); scat (l, sizeof l, " 0 * :"); scat (l, sizeof l, g_real); send_line (l);
	snprintf (t, sizeof t, TR ("Signing in as %s..."), g_nick); info (t, LK_EVENT);
	hist_add (g_server, g_port); hist_options ();		// (the card may be busy: not before the connection is there)
	save_small (NICKPATH, g_nick, slen (g_nick));
	config_save ();
	state_changed ();
}
static void net_watch ()				// (once a second)
{
	unsigned char ni[64];
	memset (ni, 0, sizeof ni);
	net_info ((long) ni);
	bool up = (ni[0] | ni[1] | ni[2] | ni[3]) != 0;
	if (up != g_netUp) { g_netUp = up; state_changed (); ui_w_enable (g_cGo, up); }
	if (up && g_auto) connect_now ();
}

// ---- what you type ---------------------------------------------------------------------------------------------------
static void submit (char *s)
{
	if (s[0] == '\0') return;
	if (s[0] != '/' || s[1] == '/') { say (g_cur, s[0] == '/' ? s + 1 : s, false); return; }
	char *cmd = s + 1;
	char *a = cmd; while (*a && *a != ' ') a++;
	if (*a) *a++ = '\0';
	while (*a == ' ') a++;
	for (char *c = cmd; *c; c++) *c = lc (*c);

	if (!strcmp (cmd, "join") && a[0]) join_chan (a);
	else if (!strcmp (cmd, "part") || !strcmp (cmd, "close") || !strcmp (cmd, "leave"))
	{
		int bi = a[0] ? buf_find (a) : g_cur;
		if (bi > 0) leave_conv (bi);
	}
	else if (!strcmp (cmd, "nick") && a[0])
	{
		char j[64] = "NICK "; scat (j, sizeof j, a); send_line (j);
		if (g_state != ST_ONLINE) { scpy (g_nick, sizeof g_nick, a); set_nick_field (a); state_changed (); }
	}
	else if ((!strcmp (cmd, "msg") || !strcmp (cmd, "query")) && a[0])
	{
		char *target = a; while (*a && *a != ' ') a++;
		if (*a) *a++ = '\0';
		if (is_chan (target))
		{
			int bi = buf_find (target);
			if (bi >= 0 && a[0]) say (bi, a, false);
			else if (a[0]) { char l[640] = "PRIVMSG "; scat (l, sizeof l, target); scat (l, sizeof l, " :"); scat (l, sizeof l, a); send_line (l); }
		}
		else
		{
			int bi = buf_get (target, BK_QUERY);
			if (a[0]) say (bi, a, false);
			pm_show (g_buf[bi]);
		}
	}
	else if (!strcmp (cmd, "me") && a[0]) say (g_cur, a, true);
	else if (!strcmp (cmd, "topic"))
	{
		Buf *b = g_buf[g_cur];
		if (b->kind != BK_CHAN) return;
		char l[600] = "TOPIC "; scat (l, sizeof l, b->name);
		if (a[0]) { scat (l, sizeof l, " :"); scat (l, sizeof l, a); }
		send_line (l);
	}
	else if (!strcmp (cmd, "list")) show_panel (P_ROOMS);
	else if (!strcmp (cmd, "quit"))
	{
		char j[160] = "QUIT :"; scat (j, sizeof j, a[0] ? a : "ARMPI IRC"); send_line (j);
		g_quit = true;
	}
	else if ((!strcmp (cmd, "server") || !strcmp (cmd, "connect")) && a[0]) { server_spec (a); ui_w_set_text (g_cServer, (long) a); connect_now (); }
	else if (!strcmp (cmd, "clear")) { g_buf[g_cur]->count = 0; g_buf[g_cur]->mark = -1; g_viewDirty = true; }
	else if (!strcmp (cmd, "raw") && a[0]) send_line (a);
	else
	{
		char j[640]; scpy (j, sizeof j, cmd);
		for (char *c = j; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char) (*c - 32);
		if (a[0]) { scat (j, sizeof j, " "); scat (j, sizeof j, a); }
		send_line (j);
	}
}
// The lines sent, Up and Down in the field bring them back.
enum { HIST = 20 };
static char s_sent[HIST][420]; static int s_nsent, s_sentAt = -1;
static char s_draft[420];
static void on_send ()
{
	char s[512]; scpy (s, sizeof s, (const char *) ui_w_get_text (g_in));
	if (!s[0]) return;
	for (int i = imin (s_nsent, HIST - 1); i > 0; i--) scpy (s_sent[i], sizeof s_sent[0], s_sent[i - 1]);
	scpy (s_sent[0], sizeof s_sent[0], s);
	s_nsent = imin (s_nsent + 1, HIST); s_sentAt = -1;
	ui_w_set_text (g_in, (long) "");
	submit (s);
	g_buf[g_cur]->scroll = 0;
	g_viewDirty = true;
	ui_w_focus (g_in);
}
static void sent_step (int d)
{
	int at = s_sentAt + d;
	if (at < -1 || at >= s_nsent) return;
	if (s_sentAt == -1) scpy (s_draft, sizeof s_draft, (const char *) ui_w_get_text (g_in));
	s_sentAt = at;
	const char *t = at < 0 ? s_draft : s_sent[at];
	ui_w_set_text (g_in, (long) t);
	ui_textbox_select (g_in, slen (t), slen (t));
}

// Tab in the line: the word under the caret completed: a nickname (who spoke last first), "/": a command, "#": a room.
static const char *const CMDS[] = { "/join ", "/part", "/nick ", "/msg ", "/query ", "/me ", "/topic ", "/list", "/server ", "/raw ", "/clear", "/quit", 0 };
static char s_compWord[64]; static int s_compStart = -1, s_compAt = 0, s_compEnd;
static bool comp_seen (const char **v, int n, const char *s) { for (int i = 0; i < n; i++) if (ieq (v[i], s)) return true; return false; }
static int comp_list (const char **v, int cap)
{
	Buf *b = g_buf[g_cur];
	const char *w = s_compWord;
	int n = 0;
	if (w[0] == '/') { for (int i = 0; CMDS[i] && n < cap; i++) if (istarts (CMDS[i], w)) v[n++] = CMDS[i]; return n; }
	if (w[0] == '#') { for (int i = 1; i < g_nbuf && n < cap; i++) if (g_buf[i]->kind == BK_CHAN && istarts (g_buf[i]->name, w)) v[n++] = g_buf[i]->name; return n; }
	for (int i = b->count - 1; i >= 0 && n < cap; i--)
	{
		const Line &l = buf_line (b, i);
		if ((l.kind == LK_MSG || l.kind == LK_ACTION) && !l.self && istarts (l.nick, w) && !comp_seen (v, n, l.nick)) v[n++] = l.nick;
	}
	if (b->kind == BK_CHAN)
	{
		nick_sort (b);
		for (int i = 0; i < b->nnick && n < cap; i++)
			if (istarts (b->nicks[i].name, w) && !comp_seen (v, n, b->nicks[i].name)) v[n++] = b->nicks[i].name;
	}
	return n;
}
static void complete (int dir)
{
	char t[512]; scpy (t, sizeof t, (const char *) ui_w_get_text (g_in));
	long end; long st = uikit_textbox_selection (g_in, &end);
	int caret = (int) end > 0 ? (int) end : (int) st;
	if (s_compStart < 0 || caret != s_compEnd)		// a new word
	{
		int a = caret; while (a > 0 && t[a - 1] != ' ') a--;
		int n = imin (caret - a, 63);
		memcpy (s_compWord, t + a, (size_t) n); s_compWord[n] = 0;
		if (!n) return;
		s_compStart = a; s_compAt = -1;
	}
	static const char *v[64];
	int n = comp_list (v, 64);
	if (!n) { s_compStart = -1; return; }
	s_compAt = s_compAt < 0 && dir < 0 ? n - 1 : (s_compAt + dir + n) % n;
	const char *w = v[s_compAt];
	char o[600]; memcpy (o, t, (size_t) s_compStart); o[s_compStart] = 0;
	scat (o, sizeof o, w);
	if (s_compWord[0] != '/') scat (o, sizeof o, s_compStart == 0 ? ": " : " ");
	int pos = slen (o);
	scat (o, sizeof o, t + caret);
	ui_w_set_text (g_in, (long) o);
	ui_textbox_select (g_in, pos, pos);
	s_compEnd = pos;
}

// ---- Go to ---------------------------------------------------------------------------------------------------------
static void goto_fill ()
{
	const char *f = (const char *) ui_w_get_text (g_gIn);
	ui_list_clear (g_gList);
	for (int i = 0; i < g_nbuf; i++)
	{
		const char *n = i ? g_buf[i]->name : TR ("Server");
		if (icontains (n, f)) ui_list_add (g_gList, (long) n, i);
	}
	if (ui_list_count (g_gList)) ui_w_set_value (g_gList, 0);
	ui_w_draw (g_gList);
}
static void goto_go ()
{
	long i = ui_w_get_value (g_gList), d = -1;
	if (i >= 0 && i < ui_list_count (g_gList)) { uikit_list_get (g_gList, i, &d); show_panel (P_CHAT); show_conv ((int) d); return; }
	char t[80]; scpy (t, sizeof t, (const char *) ui_w_get_text (g_gIn));
	if (!t[0]) return;
	show_panel (P_CHAT);
	if (is_chan (t)) join_chan (t); else open_query (t);
}

// ---- the window -----------------------------------------------------------------------------------------------------
static void show_panel (int p)
{
	g_panel = p;
	ui_w_show (g_rp, p == P_ROOMS); ui_w_show (g_cp, p == P_CONNECT); ui_w_show (g_gp, p == P_GOTO);
	layout ();
	if (p == P_ROOMS)
	{
		if (g_nrooms == 0 && !g_listing) rooms_request ();
		rooms_refresh ();
		ui_w_focus (g_rFilter);
	}
	else if (p == P_CONNECT) { ui_w_show (g_cOff, g_state != ST_OFFLINE); ui_w_focus (g_cNick); }
	else if (p == P_GOTO) { ui_w_set_text (g_gIn, (long) ""); goto_fill (); ui_w_focus (g_gIn); }
	else ui_w_focus (g_in);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_view_dirty (g_card); ui_view_dirty (g_vTitle); ui_view_dirty (g_banner); ui_view_dirty (g_chat); ui_view_dirty (g_uTitle); ui_view_dirty (g_rTitle);
	ui_w_draw (0);
	ui_update_all ();
}
static void move_rooms (int x, int w, int h)
{
	int c = ctl_h (), hh = 40, ry = hh + 8, m = 12;
	ui_w_move (g_rp, x, 0, w, h);
	ui_w_move (g_rTitle, x, 0, w, hh);
	ui_w_move (g_rClose, x + w - 144, 5, 32, c);
	int join = 88, nb = 84, lab = text_w (TR ("Min."), PX_SMALL, f_sans ()) + 6;
	int fw = imax (100, w - 2 * m - join - nb - lab - 32 - 3 * 8);
	int rx = x + m;
	ui_w_move (g_rFilter, rx, ry, fw, c); rx += fw + 8;
	ui_w_move (g_rMinL, rx, ry, lab, c); rx += lab;
	ui_w_move (g_rMin, rx, ry, nb, c); rx += nb + 8;
	ui_w_move (g_rRefresh, rx, ry, 32, c); rx += 40;
	ui_w_move (g_rJoin, rx, ry, join, c);
	ui_w_move (g_rList, x + m, ry + c + 8, w - 2 * m, h - ry - c - 8 - m);
}
static void move_connect (int x, int w, int h)
{
	int bw = 320, cx = x + imax (12, (w - bw) / 2), y = imax (12, (h - 250) / 2), c = ctl_h ();
	int lw = 72, fw = imin (bw, w - 24) - lw;
	ui_w_move (g_cp, x, 0, w, h);
	ui_w_move (g_cTitle, cx, y, bw, 22); y += 22;
	ui_w_move (g_cSub, cx, y, bw, 20); y += 30;
	long *f[4] = { &g_cServer, &g_cNick, &g_cChans, &g_cPass };
	for (int i = 0; i < 4; i++, y += 34)
	{
		ui_w_move (g_cL[i], cx, y, lw, c);
		ui_w_move (*f[i], cx + lw, y, fw, c);
	}
	y += 6;
	ui_w_move (g_cGo, cx + lw + fw - 110, y, 110, c);
	ui_w_move (g_cOff, cx + lw + fw - 110 - 130, y, 122, c);
	ui_w_move (g_cErr, cx, y + c + 6, bw, 20);
	ui_w_move (g_cNote, cx, y + c + 26, bw, 20);
}
static void move_goto (int x, int w, int h)
{
	int c = ctl_h (), bw = imin (420, w - 24), cx = x + (w - bw) / 2;
	ui_w_move (g_gp, x, 0, w, h);
	ui_w_move (g_gIn, cx, 24, bw, c);
	ui_w_move (g_gList, cx, 24 + c + 8, bw, imin (280, h - 24 - c - 20));
}
static void layout ()
{
	int w = g_w, h = g_h;
	if (!w) return;
	int band = w >= 900 ? 2 : w >= 640 ? 1 : 0;
	if (band != s_band) { s_band = band; s_sideWant = s_usersWant = -1; }
	bool side = s_sideWant >= 0 ? s_sideWant != 0 : band >= 1;
	if (g_panel != P_CHAT && band == 0) side = false;
	bool isChan = g_buf[g_cur]->kind == BK_CHAN && g_panel == P_CHAT;
	bool users = isChan && (s_usersWant >= 0 ? s_usersWant != 0 : band == 2);
	if (band == 0 && side) users = false;
	s_sideOn = side; s_usersOn = users;
	int sw = imax (120, imin (240, g_sideW));

	ui_w_show (g_card, side); ui_w_show (g_side, side); ui_w_show (g_bAdd, side); ui_w_show (g_split, side);
	if (side)
	{
		ui_w_move (g_card, 0, 0, sw, CARD_H);
		ui_w_move (g_side, 0, CARD_H, sw, h - CARD_H - FOOT_H);
		ui_w_move (g_bAdd, 10, h - FOOT_H + (FOOT_H - ctl_h ()) / 2, sw - 20, ctl_h ());
		ui_w_move (g_split, sw, 0, 6, h);
	}
	int x = side ? sw + 6 : 0, cw = w - x;
	s_headH = h < 400 ? 30 : HEAD_MAX;
	int c = ctl_h (), by = (s_headH - c) / 2;
	bool chat = g_panel == P_CHAT;
	ui_w_show (g_tb, chat); ui_w_show (g_chat, chat); ui_w_show (g_inRow, chat);
	if (chat)
	{
		ui_w_move (g_tb, x, 0, cw, s_headH);
		ui_w_show (g_btSide, !side);
		ui_w_move (g_btSide, x + 4, by, 32, c);
		int tx = x + (side ? 0 : 38), rw = 72;
		ui_w_move (g_vTitle, tx, 0, imax (20, cw - (tx - x) - rw - 104), s_headH);
		ui_w_move (g_tbR, x + cw - rw - 104, 0, rw, s_headH);	// (left of the window's buttons)
		ui_w_show (g_btUsers, isChan);
		ui_w_move (g_btUsers, x + cw - rw - 102, by, 32, c);
		ui_w_move (g_btMore, x + cw - rw - 68, by, 32, c);
		ui_w_set_value (g_btUsers, users);
		char t[160]; const char *btn;
		const char *s = banner_text (&btn, t, sizeof t);
		s_bannerOn = s != 0;
		int y = s_headH;
		ui_w_show (g_banner, s_bannerOn);
		ui_w_show (g_bannerBtn, s_bannerOn && btn);
		if (s_bannerOn)
		{
			ui_w_move (g_banner, x, y, cw, BANNER_H);
			if (btn) { ui_w_set_text (g_bannerBtn, (long) btn); ui_w_move (g_bannerBtn, x + cw - 112, y + 2, 104, BANNER_H - 4); }
			y += BANNER_H;
		}
		int uw = users ? (w >= 1000 ? 150 : 132) : 0, mh = h - y - IN_H;
		ui_w_show (g_uTitle, users); ui_w_show (g_users, users); ui_w_show (g_pmBtn, users);
		if (users)
		{
			ui_w_move (g_uTitle, x + cw - uw, y, uw, 26);
			ui_w_move (g_users, x + cw - uw, y + 26, uw, h - y - 26 - c - 12);
			ui_w_move (g_pmBtn, x + cw - uw + 6, h - c - 8, uw - 12, c);
		}
		chat_move (x, y, cw - uw - (users ? 1 : 0), mh);
		ui_w_move (g_inRow, x, h - IN_H, cw - uw, IN_H);
		ui_w_move (g_in, x + 10, h - IN_H + (IN_H - c) / 2, cw - uw - 20 - 40, c);
		ui_w_move (g_send, x + cw - uw - 44, h - IN_H + (IN_H - c) / 2, 34, c);
	}
	else
	{
		ui_w_show (g_banner, 0); ui_w_show (g_bannerBtn, 0); ui_w_show (g_uTitle, 0); ui_w_show (g_users, 0); ui_w_show (g_pmBtn, 0);
		s_bannerOn = false;
	}
	move_rooms (x, cw, h); move_connect (x, cw, h); move_goto (x, cw, h);
	g_viewDirty = g_headDirty = true;
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_view_dirty (g_card); ui_view_dirty (g_vTitle); ui_view_dirty (g_banner); ui_view_dirty (g_uTitle); ui_view_dirty (g_rTitle);
	ui_w_draw (0);
	ui_update_all ();
}
static void on_resize (long w, long h) { g_w = (int) w; g_h = (int) h; layout (); }

enum { M_CONNECT = 1, M_OFF, M_ROOMS, M_NICK, M_GOTO = 10, M_NEXT, M_TOPIC, M_LEAVE, M_CLEAR, M_SIDE = 20, M_USERS, M_EVENTS };
static char s_menus[900];
static void menus_make ()
{
	snprintf (s_menus, sizeof s_menus,
		"MIRC\nI1\t%s\nI2\t%s\n-\nI3\t%s\tCtrl+L\nI4\t%s\n"
		"M%s\nI10\t%s\tCtrl+K\nI11\t%s\n-\nI12\t%s\nI13\t%s\tCtrl+W\nI14\t%s\n"
		"M%s\nI20c\t%s\tCtrl+B\nI21c\t%s\tCtrl+U\nI22c\t%s\n",
		TR ("Connection..."), TR ("Disconnect"), TR ("Rooms..."), TR ("Change the nickname..."),
		TR ("Conversation"), TR ("Go to..."), TR ("Next unread"), TR ("Change the topic..."), TR ("Leave the room"), TR ("Clear"),
		TR ("View"), TR ("Conversations"), TR ("Users"), TR ("Arrivals and departures"));
}
static void next_unread ()
{
	int best = -1;
	for (int pass = 0; pass < 2 && best < 0; pass++)
		for (int k = 1; k <= g_nbuf; k++)
		{
			int i = (g_cur + k) % g_nbuf; Buf *b = g_buf[i];
			if (pass == 0 ? b->mentions > 0 : b->unread > 0) { best = i; break; }
		}
	if (best >= 0) show_conv (best);
}
static void command (int id)
{
	switch (id)
	{
	case M_CONNECT: show_panel (P_CONNECT); break;
	case M_OFF: disconnect (); break;
	case M_ROOMS: show_panel (g_panel == P_ROOMS ? P_CHAT : P_ROOMS); break;
	case M_NICK: ask_nick (); break;
	case M_GOTO: show_panel (g_panel == P_GOTO ? P_CHAT : P_GOTO); break;
	case M_NEXT: next_unread (); break;
	case M_TOPIC:
		if (g_buf[g_cur]->kind == BK_CHAN)
		{
			char t[400]; scpy (t, sizeof t, g_buf[g_cur]->topic);
			if (ui_ask ((long) TR ("Topic"), (long) g_buf[g_cur]->name, (long) t, sizeof t))
			{ char s[420] = "/topic "; scat (s, sizeof s, t); submit (s); }
		}
		break;
	case M_LEAVE: if (g_cur > 0) leave_conv (g_cur); break;
	case M_CLEAR: { char s[8] = "/clear"; submit (s); break; }
	case M_SIDE: s_sideWant = s_sideOn ? 0 : 1; ui_menu_state (M_SIDE, s_sideOn ? 0 : UMS_CHECKED); layout (); break;
	case M_USERS: s_usersWant = s_usersOn ? 0 : 1; ui_menu_state (M_USERS, s_usersOn ? 0 : UMS_CHECKED); layout (); break;
	case M_EVENTS: g_showEvents = !g_showEvents; ui_menu_state (M_EVENTS, g_showEvents ? UMS_CHECKED : 0); for (int i = 0; i < g_nbuf; i++) g_buf[i]->lastTotal = 0; g_viewDirty = true; break;
	}
}
static void more_menu ()
{
	Buf *b = g_buf[g_cur];
	const char *a = TR ("Change the topic..."), *l = TR ("Leave the room"), *c = TR ("Clear"), *e = g_showEvents ? TR ("Hide arrivals and departures") : TR ("Show arrivals and departures");
	char t[300]; int n = 0;
	for (const char *s : { a, l, c, e }) { const char *x = s; if (s == a && b->kind != BK_CHAN) { t[n++] = 1; } if (s == l && b->kind != BK_CHAN) { t[n++] = 1; } strcpy (t + n, x); n += slen (x) + 1; }
	t[n] = 0;
	long r = ui_popup_local (g_w - 210, s_headH, (long) t, 200);
	static const int ID[4] = { M_TOPIC, M_LEAVE, M_CLEAR, M_EVENTS };
	if (r >= 0 && r < 4) command (ID[r]);
}
static void msg_menu ()
{
	Buf *b = g_buf[g_cur];
	if (s_ctxLine < 0 || s_ctxLine >= b->count) { s_ctxLine = -1; return; }
	Line &l = buf_line (b, s_ctxLine);
	s_ctxLine = -1;
	char reply[80], pm[80], t[220]; int n = 0;
	snprintf (reply, sizeof reply, TR ("Reply to %s"), l.nick); snprintf (pm, sizeof pm, TR ("Message %s"), l.nick);
	const char *copy = TR ("Copy the message");
	strcpy (t, copy); n = slen (copy) + 1;
	if (l.nick[0]) { strcpy (t + n, reply); n += slen (reply) + 1; strcpy (t + n, pm); n += slen (pm) + 1; }
	t[n] = 0;
	switch (ui_popup_local (s_ctxX, s_ctxY, (long) t, 220))
	{
	case 0: ui_clip_set ((long) l.text, -1); break;
	case 1:
	{
		char o[80]; snprintf (o, sizeof o, "%s: ", l.nick);
		ui_w_set_text (g_in, (long) o); ui_textbox_select (g_in, slen (o), slen (o)); ui_w_focus (g_in);
		break;
	}
	case 2: open_query (l.nick); break;
	}
}

// What a widget said (out of ui_w_event: the dialogues and the popups may open here).
static void control (long w, long code)
{
	if (pm_control (w, code)) return;
	if (w == g_chatBar) { if (code == UN_CHANGE) chat_bar_event (); }
	else if (w == g_in) { if (code == UN_ACTIVATE) on_send (); }
	else if (w == g_send) { if (code == UN_ACTIVATE) on_send (); }
	else if (w == g_split) { if (code == UN_CHANGE) { g_sideW = (int) ui_w_get_value (w); layout (); } }
	else if (w == g_side)
	{
		long i = ui_w_get_value (w);
		if (i < 0 || i >= s_nrow || s_rowBuf[i] < 0) return;
		if (code == UN_CHANGE && s_rowBuf[i] != g_cur) show_conv (s_rowBuf[i]);
		else if (code == UN_CONTEXT) { side_menu (s_rowBuf[i]); g_sideDirty = true; }
	}
	else if (w == g_bAdd) { if (code == UN_ACTIVATE) show_panel (P_ROOMS); }
	else if (w == g_btSide) { s_sideWant = 1; if (s_band == 0) s_usersWant = 0; layout (); }
	else if (w == g_btUsers) { s_usersWant = s_usersOn ? 0 : 1; if (s_band == 0 && !s_usersOn) s_sideWant = 0; layout (); }
	else if (w == g_btMore) more_menu ();
	else if (w == g_bannerBtn) { if (code == UN_ACTIVATE) banner_click (); }
	else if (w == g_users)
	{
		ui_w_enable (g_pmBtn, user_selected () != 0);
		if (code == UN_ACTIVATE && user_selected ()) open_query (user_selected ());
	}
	else if (w == g_pmBtn) { if (code == UN_ACTIVATE && user_selected ()) open_query (user_selected ()); }
	else if (w == g_rFilter || w == g_rMin) { if (code == UN_CHANGE) rooms_refresh (); }
	else if (w == g_rClose) { if (code == UN_ACTIVATE) show_panel (P_CHAT); }
	else if (w == g_rRefresh) { if (code == UN_ACTIVATE) { rooms_request (); rooms_refresh (); } }
	else if (w == g_rJoin) { if (code == UN_ACTIVATE) room_join (); }
	else if (w == g_rList)
	{
		if (code == UN_SORT)
		{
			long desc = 0, col = uikit_list_sorted (w, &desc);
			if (col >= 0) { s_sortCol = (int) col; s_sortDesc = desc != 0; rooms_refresh (); }
		}
		else if (code == UN_ACTIVATE) room_join ();
		else ui_w_enable (g_rJoin, ui_w_get_value (w) >= 0);
	}
	else if (w == g_cGo) { if (code == UN_ACTIVATE) { panel_read (); connect_now (); } }
	else if (w == g_cOff) { if (code == UN_ACTIVATE) { disconnect (); show_panel (P_CHAT); } }
	else if (g_panel == P_CONNECT && code == UN_ACTIVATE) { panel_read (); connect_now (); }	// (Enter in a field; the server's is the combo box's child)
	else if (w == g_gIn)
	{
		if (code == UN_CHANGE) goto_fill ();
		else if (code == UN_ACTIVATE) goto_go ();
	}
	else if (w == g_gList) { if (code == UN_ACTIVATE) goto_go (); }
}
// A key before the widgets: Tab, the arrows of the line are ours. true: taken.
static bool key_first (int hid, int ch, int mods)
{
	bool alt = mods & UI_MOD_ALT, shift = mods & UI_MOD_SHIFT, ctrl = mods & UI_MOD_CTRL;
	if (hid != KEY_TAB) s_compStart = -1;
	if (pm_of_win (ui_ev_field (&ui, EV_WIN)))		// (no menu in that window: its short cuts by hand)
	{
		if (ctrl && hid == 0x1A) { pm_close (pm_of_win (ui_ev_field (&ui, EV_WIN))); return true; }
		if (ctrl && hid == 0x0E) { ui_raise (g_mainWin); show_panel (P_GOTO); return true; }
		return false;
	}
	if (hid == KEY_ESC)
	{
		if (g_panel != P_CHAT) { show_panel (P_CHAT); return true; }
		Buf *b = g_buf[g_cur];
		if (b->scroll) { chat_to (b, 0); return true; }
		return false;
	}
	if (g_panel == P_GOTO && (hid == KEY_UP || hid == KEY_DOWN) && ui_w_get_focus () == g_gIn)
	{
		long i = ui_w_get_value (g_gList) + (hid == KEY_DOWN ? 1 : -1);
		if (i >= 0 && i < ui_list_count (g_gList)) ui_w_set_value (g_gList, i);
		return true;
	}
	if (g_panel == P_ROOMS && hid == KEY_DOWN && ui_w_get_focus () == g_rFilter) { ui_w_focus (g_rList); return true; }
	if (g_panel != P_CHAT) return false;
	if (alt && (hid == KEY_UP || hid == KEY_DOWN))
	{
		if (shift) { next_unread (); return true; }
		int i = g_cur;
		for (int k = 0; k < g_nbuf; k++)
		{
			i = (i + (hid == KEY_DOWN ? 1 : g_nbuf - 1)) % g_nbuf;
			if (g_buf[i]->kind != BK_QUERY) { show_conv (i); break; }
		}
		return true;
	}
	if (alt && (hid == KEY_LEFT || hid == KEY_RIGHT)) { if (g_prev < g_nbuf) show_conv (g_prev); return true; }
	if (alt && hid >= 0x1E && hid <= 0x26)			// Alt+1..9: the rows, headings not counted
	{
		int want = hid - 0x1E;
		for (int r = 0; r < s_nrow; r++) if (s_rowBuf[r] >= 0 && want-- == 0) { show_conv (s_rowBuf[r]); break; }
		return true;
	}
	if (hid == KEY_PGUP || hid == KEY_PGDN)
	{
		Buf *b = g_buf[g_cur];
		chat_to (b, b->scroll + (hid == KEY_PGUP ? 1 : -1) * (s_vh - 40));
		return true;
	}
	if (ctrl && (hid == KEY_HOME || hid == KEY_END)) { chat_to (g_buf[g_cur], hid == KEY_HOME ? 1 << 30 : 0); return true; }
	if (ui_w_get_focus () == g_in)
	{
		if (hid == KEY_TAB && ((const char *) ui_w_get_text (g_in))[0]) { complete (shift ? -1 : 1); return true; }
		if (hid == KEY_UP) { sent_step (1); return true; }
		if (hid == KEY_DOWN) { sent_step (-1); return true; }
	}
	else if (ch >= 32 && !ctrl && !alt && g_state == ST_ONLINE)	// a letter typed in the messages: it goes to the line
	{
		long f = ui_w_get_focus ();
		if (f == 0 || f == g_chat) ui_w_focus (g_in);
	}
	return false;
}

static long label_at (const char *s, long parent, int flags)
{
	long l = ui_label (0, 0, 10, 10, (long) s, flags);
	ui_w_set_parent (l, parent);
	return l;
}
static void build_window ()
{
	s_jet = (const unsigned *) ui_jetons ();
	nick_colors_make ();
	g_w = (int) ui.w; g_h = (int) ui.h;
	ui_fill (0, 0, g_w, g_h, jet (J_FACE));

	g_card = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_card, (long) card_paint, (long) card_mouse, 0, 0);
	ui_w_tip (g_card, (long) TR ("Your connection"));
	g_side = ui_list (0, 0, 10, 10, UL_SIDEBAR | UL_NOSORT);
	ui_list_cell (g_side, (long) side_cell);
	g_bAdd = ui_pushbutton (0, 0, 10, 0, (long) TR ("+ Rooms"), UB_GHOST);
	g_split = ui_splitter (g_sideW, 0, 6, 10, 120, 240);
	ui_w_set_value (g_split, g_sideW);

	g_tb = ui_toolbar (0, 0, 10, HEAD_MAX);
	g_btSide = ui_toolbar_add (g_tb, (long) "\xE2\x89\xA1", 0, 0);
	ui_w_tip (g_btSide, (long) TR ("Conversations (Ctrl+B)"));
	g_vTitle = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vTitle, (long) title_paint, 0, 0, 0);
	ui_w_set_parent (g_vTitle, g_tb);
	g_tbR = ui_toolbar (0, 0, 72, HEAD_MAX);
	ui_w_set_parent (g_tbR, g_tb);
	g_btUsers = ui_toolbar_add (g_tbR, (long) "\xE2\x98\xBA", 0, UTB_TOGGLE);
	ui_w_tip (g_btUsers, (long) TR ("Users (Ctrl+U)"));
	g_btMore = ui_toolbar_add (g_tbR, (long) "\xE2\x8B\xAF", 0, 0);
	ui_w_tip (g_btMore, (long) TR ("More"));

	g_banner = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_banner, (long) banner_paint, 0, 0, 0);
	g_bannerBtn = ui_pushbutton (0, 0, 10, 0, (long) "", UB_GHOST);
	ui_w_set_parent (g_bannerBtn, g_banner);

	chat_create ();
	g_inRow = ui_panel (0, 0, 10, 10, 0);
	g_in = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_w_set_parent (g_in, g_inRow);
	ui_w_set_range (g_in, 0, 400, 0, 0);
	g_send = ui_pushbutton (0, 0, 34, 0, (long) "\xE2\x9E\xA4", UB_GHOST);
	ui_w_set_parent (g_send, g_inRow);
	ui_w_tip (g_send, (long) TR ("Send"));

	g_uTitle = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_uTitle, (long) utitle_paint, 0, 0, 0);
	g_users = ui_list (0, 0, 10, 10, UL_SIDEBAR | UL_NOSORT);
	ui_list_virtual (g_users, 0, (long) users_text);
	ui_list_cell (g_users, (long) users_cell);
	ui_w_tip (g_users, (long) TR ("Double-click someone to talk to them privately"));
	g_pmBtn = ui_pushbutton (0, 0, 10, 0, (long) TR ("Message"), 0);
	ui_w_enable (g_pmBtn, 0);

	// the rooms
	g_rp = ui_panel (0, 0, 10, 10, 0);
	g_rTitle = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_rTitle, (long) rtitle_paint, 0, 0, 0);
	ui_w_set_parent (g_rTitle, g_rp);
	g_rClose = ui_pushbutton (0, 0, 32, 0, (long) "\xE2\x9C\x95", UB_GHOST);
	ui_w_set_parent (g_rClose, g_rp);
	g_rFilter = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_textbox_hint (g_rFilter, (long) TR ("Search"));
	ui_w_set_parent (g_rFilter, g_rp);
	g_rMinL = label_at (TR ("Min."), g_rp, ULB_DIM);
	g_rMin = ui_numeric (0, 0, 84, ctl_h (), 0, 100000, 0);
	ui_w_set_range (g_rMin, 0, 100000, 0, 5);
	ui_w_set_parent (g_rMin, g_rp);
	g_rRefresh = ui_pushbutton (0, 0, 32, 0, (long) "\xE2\x86\xBB", UB_GHOST);
	ui_w_set_parent (g_rRefresh, g_rp);
	ui_w_tip (g_rRefresh, (long) TR ("Ask the server for the list again"));
	g_rJoin = ui_pushbutton (0, 0, 88, 0, (long) TR ("Join"), UB_DEFAULT);
	ui_w_set_parent (g_rJoin, g_rp);
	g_rList = ui_list (0, 0, 10, 10, UL_HEADER | UL_USERSORT | UL_STRIPES | UL_HSCROLL);
	ui_list_column (g_rList, (long) TR ("Room"), 170, 0);
	ui_list_column (g_rList, (long) TR ("Users"), 76, ULC_RIGHT);
	ui_list_column (g_rList, (long) TR ("Topic"), 420, 0);
	ui_w_set_parent (g_rList, g_rp);
	ui_list_virtual (g_rList, 0, (long) rooms_text);
	ui_list_sort (g_rList, s_sortCol, s_sortDesc);

	// the connection
	g_cp = ui_panel (0, 0, 10, 10, 0);
	g_cTitle = label_at (TR ("Connect to IRC"), g_cp, ULB_TITLE);
	g_cSub = label_at (TR ("Choose a server and a nickname."), g_cp, ULB_DIM);
	static const char *const L[4] = { "Server", "Nickname", "Rooms", "NickServ" };
	for (int i = 0; i < 4; i++) g_cL[i] = label_at (TR (L[i]), g_cp, ULB_DIM);
	char addr[96]; scpy (addr, sizeof addr, g_histn ? g_hist[0] : g_server);
	if (!g_histn && g_port != 6667) { scat (addr, sizeof addr, ":"); scatn (addr, sizeof addr, (int) g_port); }
	g_cServer = ui_combobox (0, 0, 10, ctl_h (), (long) "");
	ui_w_set_parent (g_cServer, g_cp);
	hist_options ();
	ui_w_set_text (g_cServer, (long) addr);
	ui_w_tip (g_cServer, (long) TR ("The server (host or host:port); the arrow: the ones used before"));
	g_cNick = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_w_set_range (g_cNick, 0, 30, 0, 0);
	ui_w_set_text (g_cNick, (long) g_nick);
	ui_w_set_parent (g_cNick, g_cp);
	g_cChans = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_textbox_hint (g_cChans, (long) TR ("to join: #armpi, #onyx"));
	ui_w_set_text (g_cChans, (long) g_autojoin);
	ui_w_set_parent (g_cChans, g_cp);
	g_cPass = ui_textbox (0, 0, 10, ctl_h (), UTF_PASSWORD);
	ui_textbox_hint (g_cPass, (long) TR ("optional"));
	ui_w_set_text (g_cPass, (long) g_password);
	ui_w_set_parent (g_cPass, g_cp);
	g_cGo = ui_pushbutton (0, 0, 110, 0, (long) TR ("Connect"), UB_DEFAULT);
	ui_w_set_parent (g_cGo, g_cp);
	g_cOff = ui_pushbutton (0, 0, 122, 0, (long) TR ("Disconnect"), UB_GHOST);
	ui_w_set_parent (g_cOff, g_cp);
	g_cErr = label_at ("", g_cp, 0);
	g_cNote = label_at (TR ("A plain-text connection, port 6667."), g_cp, ULB_DIM);

	// Go to
	g_gp = ui_panel (0, 0, 10, 10, 0);
	g_gIn = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_textbox_hint (g_gIn, (long) TR ("A conversation, or #room to join, or a nickname"));
	ui_w_set_parent (g_gIn, g_gp);
	g_gList = ui_list (0, 0, 10, 10, UL_NOSORT);
	ui_w_set_parent (g_gList, g_gp);

	ui_w_show (g_rp, 0); ui_w_show (g_cp, 0); ui_w_show (g_gp, 0);
	menus_make ();
	ui_menu ((long) s_menus);
	ui_menu_state (M_EVENTS, UMS_CHECKED);
}

} // namespace irc

using namespace irc;

int main (int, char **)
{
	if (!uikit_open (&ui)) return 1;
	netkit_kit_open ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (W0, (int) scr[0] - 8), wh = imin (H0, (int) (scr[1] - scr[3] - scr[2]) - 32 - 96);
	g_mainWin = ui_window_ex (0, scr[3], ww, wh, (long) "IRC", WF_RESIZABLE);
	if (g_mainWin <= 0) return 1;
	ui_min_size (480, 320);
	ui_on_resize ((long) on_resize, 0);
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/irc.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	mkdir ("SD0:/home", 0755);
	mkdir (HOME_DIR, 0755);

	config_load ();
	bool first = access (CONFIGPATH, F_OK) != 0 && access (NICKPATH, F_OK) != 0;
	char nk[40];
	if (read_small (NICKPATH, nk, sizeof nk) > 0)
	{
		int n = 0; while (nk[n] && nk[n] != '\n' && nk[n] != '\r' && nk[n] != ' ') n++;
		nk[n] = '\0';
		if (n) scpy (g_nick, sizeof g_nick, nk);
	}
	hist_load ();
	buf_get ("*server*", BK_SERVER);
	g_rooms = new Room[MAXROOMS]; g_view = new int[MAXROOMS];
	build_window ();
	if (g_histn) server_spec (g_hist[0]);			// (the last server used: where the next start goes)
	g_auto = !first;
	net_watch ();
	layout ();
	ui_w_set_text (g_cErr, (long) "");
	if (first) show_panel (P_CONNECT); else ui_w_focus (g_in);
	chat_sync ();
	ui_view_flush ();
	f_ital (); f_boldital ();				// (read once the window is up: the card may be slow, and not inside a paint)

	unsigned lastNet = 0;
	long lastCode = 0;
	while (!g_quit)
	{
		long t = ui_event (30);
		if (t == WEV_CLOSE)
		{
			Pm *m = pm_of_win (ui_ev_field (&ui, EV_WIN));
			if (!m) break;
			pm_close (m);
		}
		else if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			if (!(press && key_first (hid, ch, mods)))
			{
				long code = 0, taken = 0;
				long w = uikit_w_event (&code, &taken);
				if (code) control (w, code);
				if (press && !taken && !pm_of_win (ui_ev_field (&ui, EV_WIN)))
				{
					long id = ui_menu_key (0);
					if (id >= 0) command ((int) id);
				}
			}
			if (s_cardMenu) { s_cardMenu = 0; card_menu (); }
			if (s_ctxLine >= 0) msg_menu ();
		}
		else ui_w_idle ();
		(void) lastCode;

		net_poll ();
		drain_socket ();
		unsigned now = ticks ();
		if (now - lastNet >= 1000) { lastNet = now; net_watch (); }

		if (g_statusDirty)
		{
			g_statusDirty = false;
			char t2[160]; const char *btn;
			bool want = g_panel == P_CHAT && banner_text (&btn, t2, sizeof t2) != 0;
			if (want != s_bannerOn) layout ();
			else if (want) { if (btn) ui_w_set_text (g_bannerBtn, (long) btn); ui_w_show (g_bannerBtn, btn != 0); ui_view_dirty (g_banner); }
			ui_view_dirty (g_card); ui_view_dirty (g_rTitle);
			input_sync ();
		}
		if (g_headDirty) { g_headDirty = false; ui_view_dirty (g_vTitle); }
		if (g_sideDirty) { g_sideDirty = false; side_rebuild (); ui_w_draw (g_side); ui_view_dirty (g_vTitle); }
		if (g_usersDirty) { g_usersDirty = false; users_rebuild (); }
		if (g_viewDirty) { g_viewDirty = false; chat_sync (); }
		if (g_roomsDirty || (g_listing && g_panel == P_ROOMS && now % 500 < 40)) { g_roomsDirty = false; if (g_panel == P_ROOMS) rooms_refresh (); }
		ui_view_flush ();
		pm_flush ();
	}
	if (g_sock >= 0) { send_line ("QUIT :ARMPI IRC"); close (g_sock); }
	return 0;
}
