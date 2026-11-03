//
// buddylist.h -- the left of the window, Messenger's contact list: on top "me" on the sky (my picture in its glass frame, my
// name and my status, a click on it to change it), the search field, then the groups -- Favourites (the pinned
// conversations), Conversations (the others, the latest first: the picture, the name, the time, the last message, the
// unread count), Contacts (the buddy in the status colour, the name, the last seen) -- each group folded or not by a click
// on its title. Three views (ls_ for the list, me_ for the head) and a field; Onyx drew it all in one widget.
//
// MIT licence.
//
#ifndef TG_BUDDYLIST_H
#define TG_BUDDYLIST_H

#include "rich.h"

static tg::Client g_c;
static void open_conversation (long long peer, bool ownWindow = false);
static void add_contact_dialog ();
static int s_statusMenu;				// (asked from a view's mouse: the loop opens it, out of ui_w_event)
static long long s_rowMenu;				// (the same: a row's menu, for this peer)
static int s_rowX, s_rowY;

// The last message of a conversation in a line ("You: ok", "Bob: Lunch?", "[Photo]").
static void preview (tg::Conv *cv, char *out, int cap)
{
	out[0] = 0;
	if (!cv || !cv->n) return;
	tg::Msg &m = cv->m[cv->n - 1];
	char who[80] = "";
	if (m.out) snprintf (who, sizeof who, "%s: ", TR ("You"));
	else if (tg::ptype (cv->peer) != tg::P_USER && m.from && tg::ptype (m.from) == tg::P_USER)
	{
		tg::User *u = g_c.user (tg::pid (m.from));
		if (u && u->first) snprintf (who, sizeof who, "%s: ", u->first);
	}
	const char *what = m.text;
	char tmp[160];
	if (m.media && m.media != tg::M_SERVICE && (!m.text || !m.text[0]))
	{
		static const char *names[] = { "", TRN ("Photo"), TRN ("Video"), TRN ("Voice message"), TRN ("Audio"), TRN ("File"), TRN ("Sticker"), TRN ("GIF"), TRN ("Location"), TRN ("Contact"), TRN ("Poll"), TRN ("Link"), TRN ("Message") };
		snprintf (tmp, sizeof tmp, "[%s]%s%s", TR (names[m.media < 13 ? m.media : 12]), m.media == tg::M_STICKER ? " " : "", m.media == tg::M_STICKER ? m.extra : "");
		what = tmp;
	}
	else if (m.media == tg::M_SERVICE) { snprintf (tmp, sizeof tmp, "%s", TR ("(group event)")); what = tmp; who[0] = 0; }
	else if (m.media == tg::M_PHOTO) { snprintf (tmp, sizeof tmp, "[%s] %s", TR ("Photo"), m.text); what = tmp; }
	snprintf (out, (size_t) cap, "%s%s", who, what ? what : "");
	for (char *p = out; *p; p++) if (*p == '\n') *p = ' ';
}

// A time for a list: 14:05 today, "Mon" this week, 03/10 before.
static void short_time (int t, char *out, int cap)
{
	out[0] = 0;
	if (!t) return;
	long day = local_day (t), today = local_day (g_c.serverTime ());
	struct tm m; local_tm (t, &m);
	if (day == today) hhmm (t, out, cap);
	else if (today - day < 7 && today > day)
	{
		static const char *wd[] = { TRN ("Sun"), TRN ("Mon"), TRN ("Tue"), TRN ("Wed"), TRN ("Thu"), TRN ("Fri"), TRN ("Sat") };
		snprintf (out, (size_t) cap, "%s", TR (wd[m.tm_wday]));
	}
	else snprintf (out, (size_t) cap, "%02d/%02d", m.tm_mday, m.tm_mon + 1);
}

#define ME_H		76
#define SEARCH_H	38
#define CONV_H		50
#define CONTACT_H	26
#define GROUP_H		26
enum { G_FAV, G_CONV, G_CONTACTS, G_N };
enum { R_GROUP, R_CONV, R_CONTACT };
struct Row { int kind, group, count; long long peer; };

static long g_me, g_searchRow, g_search, g_add, g_list, g_listBar;
static bool s_folded[G_N];
static Row *s_rows; static int s_nrows, s_capRows;
static int s_lscroll, s_lhot = -1, s_lw, s_lh;		// (the list view's size)
static int s_meW = 100; static bool s_meHot;
static long long s_selected;				// the conversation shown beside the list
static unsigned s_lrev;
static bool g_listDirty = true;

static int row_h (const Row &r) { return r.kind == R_GROUP ? GROUP_H : r.kind == R_CONV ? CONV_H : CONTACT_H; }
static int ls_content () { int h = 0; for (int i = 0; i < s_nrows; i++) h += row_h (s_rows[i]); return h + 8; }
static int ls_row_at (int my)
{
	int y = -s_lscroll;
	for (int i = 0; i < s_nrows; i++) { int h = row_h (s_rows[i]); if (my >= y && my < y + h) return i; y += h; }
	return -1;
}
static void ls_clamp ()
{
	int most = ls_content () - s_lh;
	if (s_lscroll > most) s_lscroll = most;
	if (s_lscroll < 0) s_lscroll = 0;
}
static void ls_bar ()
{
	int t = ls_content ();
	bool need = t > s_lh;
	ui_w_show (g_listBar, need);
	if (need) { ui_w_set_range (g_listBar, 0, t, s_lh, 54); ui_w_set_value (g_listBar, s_lscroll); }
}
static void ls_ensure (int i)
{
	int y = 0;
	for (int k = 0; k < i; k++) y += row_h (s_rows[k]);
	int h = row_h (s_rows[i]);
	if (y < s_lscroll) s_lscroll = y;
	else if (y + h > s_lscroll + s_lh) s_lscroll = y + h - s_lh;
	ls_clamp (); ls_bar (); ui_view_dirty (g_list);
}

static bool ls_matches (long long peer)
{
	const char *q = (const char *) ui_w_get_text (g_search);
	if (!q || !q[0]) return true;
	char name[160];
	g_c.peerName (peer, name, sizeof name);
	for (const char *s = name; *s; s++)
	{
		int k = 0;
		while (q[k] && s[k] && ((s[k] | 32) == (q[k] | 32))) k++;
		if (!q[k]) return true;
	}
	if (tg::ptype (peer) == tg::P_USER) { tg::User *u = g_c.user (tg::pid (peer)); if (u && u->username && strstr (u->username, q)) return true; }
	return false;
}
static void ls_push (int kind, int group, long long peer, int count = 0)
{
	if (s_nrows == s_capRows) { s_capRows = s_capRows ? s_capRows * 2 : 128; s_rows = (Row *) realloc (s_rows, sizeof (Row) * (size_t) s_capRows); }
	s_rows[s_nrows].kind = kind; s_rows[s_nrows].group = group; s_rows[s_nrows].peer = peer; s_rows[s_nrows].count = count;
	s_nrows++;
}
static bool contact_before (long long a, long long b, int now)
{
	tg::User *ua = g_c.user (tg::pid (a)), *ub = g_c.user (tg::pid (b));
	int sa = user_ts (ua, now), sb = user_ts (ub, now);
	if (sa != sb) return sa < sb;
	char na[96], nb[96];
	g_c.peerName (a, na, sizeof na); g_c.peerName (b, nb, sizeof nb);
	return strcasecmp (na, nb) < 0;
}
static void ls_rebuild ()
{
	s_nrows = 0;
	g_c.dialogs ();
	int nf = 0, nc = 0;
	for (int i = 0; i < g_c.norder; i++)
	{
		tg::Conv *cv = g_c.conv (g_c.order[i]);
		if (!cv || !ls_matches (cv->peer)) continue;
		if (cv->pinned) nf++; else nc++;
	}
	if (nf)
	{
		ls_push (R_GROUP, G_FAV, 0, nf);
		if (!s_folded[G_FAV]) for (int i = 0; i < g_c.norder; i++) { tg::Conv *cv = g_c.conv (g_c.order[i]); if (cv && cv->pinned && ls_matches (cv->peer)) ls_push (R_CONV, G_FAV, cv->peer); }
	}
	ls_push (R_GROUP, G_CONV, 0, nc);
	if (!s_folded[G_CONV]) for (int i = 0; i < g_c.norder; i++) { tg::Conv *cv = g_c.conv (g_c.order[i]); if (cv && !cv->pinned && ls_matches (cv->peer)) ls_push (R_CONV, G_CONV, cv->peer); }
	// the contacts: online first, then by name
	int start = s_nrows, nk = 0;
	ls_push (R_GROUP, G_CONTACTS, 0, 0);
	int now = g_c.serverTime ();
	for (int i = 0; i < g_c.users.n; i++)
	{
		tg::User *u = g_c.users.a[i];
		if (!u->contact || u->id == g_c.selfId || u->deleted) continue;
		long long peer = tg::pkey (tg::P_USER, u->id);
		if (!ls_matches (peer)) continue;
		nk++;
		if (!s_folded[G_CONTACTS]) ls_push (R_CONTACT, G_CONTACTS, peer);
	}
	s_rows[start].count = nk;
	for (int i = start + 2; i < s_nrows; i++)
	{
		Row k = s_rows[i]; int j = i - 1;
		while (j > start && contact_before (k.peer, s_rows[j].peer, now)) { s_rows[j + 1] = s_rows[j]; j--; }
		s_rows[j + 1] = k;
	}
	ls_clamp ();
}

static void me_paint (long v, long w, long h)
{
	begin_paint (v);
	ui_fill (0, 0, w, h, TC_SKY_BOT);
	sky (0, 0, (int) w, (int) h);
	long long me = tg::pkey (tg::P_USER, g_c.selfId);
	bool on = g_c.online;
	unsigned st = g_c.wantOnline () && on ? TC_ONLINE : TC_OFFLINE;
	avatar_framed (g_c, me, 14, 12, 52, st);
	char name[128];
	g_c.peerName (me, name, sizeof name);
	ftext (name, 78, 8, TC_TITLE, PX_BIG, 2, (int) w - 90);
	const char *s = !on ? TR ("Connecting...") : g_c.wantOnline () ? TR ("Online") : TR ("Appear offline");
	char lab[80];
	snprintf (lab, sizeof lab, "(%s)", s);
	int lw = ftw (lab, ui_px ());
	s_meW = lw + 40;
	if (s_meHot) { ui_rbox (74, 28, lw + 40, 20, 3, TC_HOT_TOP, TC_HOT_BOT); }
	buddy (78, 30, 14, st);
	ftext (lab, 96, 30, TC_GREY, ui_px ());
	ftext ("\xE2\x96\xBE", 98 + lw, 30, TC_GREY, 11);
	// my handle in grey
	tg::User *u = g_c.self ();
	char hd[80] = "";
	if (u && u->username && u->username[0]) snprintf (hd, sizeof hd, "@%s", u->username);
	else if (u && u->phone && u->phone[0]) snprintf (hd, sizeof hd, "+%s", u->phone);
	ftext (hd, 78, 52, TC_GREY, PX_SMALL, 1, (int) w - 90);
}
static long me_mouse (long, long kind, long x, long y, long info)
{
	bool hot = kind != 3 && x >= 74 && x < 74 + s_meW && y >= 28 && y < 50;
	if (hot != s_meHot) { s_meHot = hot; ui_view_dirty (g_me); ui_cursor (hot ? UC_HAND : UC_ARROW); }
	if (kind == 1 && hot && ((info >> 8) & 1)) s_statusMenu = 1;
	return 0;
}

static void ls_draw_row (Row &r, int i, int y, int h, int w0)
{
	int w = w0 - 16;
	if (r.kind == R_GROUP)
	{
		static const char *titles[] = { TRN ("Favourites"), TRN ("Conversations"), TRN ("Contacts") };
		fbox (s_folded[r.group] ? "\xE2\x96\xB8" : "\xE2\x96\xBE", 10, y, h, TC_TITLE, 12);
		char t[80];
		snprintf (t, sizeof t, "%s (%d)", TR (titles[r.group]), r.count);
		fbox (t, 24, y, h, i == s_lhot ? TC_LINK : TC_TITLE, ui_px (), 2);
		return;
	}
	bool sel = r.peer == s_selected, hot = i == s_lhot;
	if (sel) { ui_rbox (3, y + 1, w - 2, h - 2, 3, TC_SEL_TOP, TC_SEL_BOT); ui_frame (3, y + 1, w - 2, h - 2, TC_SEL_RIM); }
	else if (hot) { ui_rbox (3, y + 1, w - 2, h - 2, 3, TC_HOT_TOP, TC_HOT_BOT); }
	int now = g_c.serverTime (), px = ui_px ();
	char name[160];
	g_c.peerName (r.peer, name, sizeof name);
	if (r.kind == R_CONTACT)
	{
		tg::User *u = g_c.user (tg::pid (r.peer));
		int ts = user_ts (u, now);
		buddy (14, y + (h - 16) / 2, 16, ts_colour (ts));
		int nw = imin (ftw (name, px), w - 50);
		fbox (name, 36, y, h, ts == TS_OFFLINE ? TC_GREY : TC_INK, px, 0, w - 50);
		char st[80];
		status_text (g_c, u, st, sizeof st);
		char dash[90];
		snprintf (dash, sizeof dash, "- %s", st);
		if (36 + nw + 8 < w - 20) fbox (dash, 36 + nw + 6, y, h, TC_LIGHT, PX_SMALL, 1, w - (36 + nw + 6) - 4);
		return;
	}
	tg::Conv *cv2 = g_c.conv (r.peer);
	avatar_round (g_c, r.peer, 10, y + (h - 36) / 2, 36);
	if (tg::ptype (r.peer) == tg::P_USER)
	{
		tg::User *u = g_c.user (tg::pid (r.peer));
		if (u && user_ts (u, now) == TS_ONLINE)
		{
			vp_disc (41, y + (h - 36) / 2 + 31.0f, 6, TC_BG);
			vp_disc (41, y + (h - 36) / 2 + 31.0f, 4, TC_ONLINE);
		}
	}
	int tx = 54;
	char tm[16];
	short_time (cv2 ? cv2->topDate : 0, tm, sizeof tm);
	int tmw = ftw (tm, PX_SMALL);
	bool group = tg::ptype (r.peer) != tg::P_USER;
	int nx = tx;
	if (group) { buddy (tx, y + 7, 14, C.group, true); nx += 17; }
	ftext (name, nx, y + 6, TC_INK, px, 2, w - nx - tmw - 10);
	ftext (tm, w - tmw - 4, y + 8, cv2 && cv2->unread ? TC_LINK : TC_LIGHT, PX_SMALL);
	char pv[200];
	int unread = cv2 ? cv2->unread : 0;
	int pillW = 0;
	if (unread)
	{
		char t[16]; snprintf (t, sizeof t, "%d", imin (unread, 999));
		int uw = ftw (t, PX_SMALL, 2);
		pillW = imax (uw + 12, 20);
		ui_rbox (w - pillW - 2, y + 27, pillW, 17, 8, mix (TC_UNREAD, C.unreadInk, 40), TC_UNREAD);
		fbox (t, w - pillW - 2 + (pillW - uw) / 2, y + 27, 17, C.unreadInk, PX_SMALL, 2);
	}
	if (cv2 && cv2->typingUntil)
	{
		ftext (TR ("typing..."), tx, y + 27, TC_LINK, px, 1, w - tx - pillW - 8);
		return;
	}
	preview (cv2, pv, sizeof pv);
	// the first line of it, its emoticons drawn small, "..." if it goes on
	static Rich rr;
	int lim = w - tx - pillW - 8, dots = ftw ("...", px);
	rich_layout (rr, px, pv, lim - dots, 15, true);
	char buf[256];
	int ex = 0;
	for (int k = 0; k < rr.n; k++)
	{
		RichPiece &pc = rr.p[k];
		if (pc.line > 0) break;
		ex = pc.x + pc.w;
		if (pc.kind == RP_EMO) { emo_draw (pc.emo, tx + pc.x, y + 28, 15); continue; }
		int len = imin (pc.len, 255);
		memcpy (buf, pv + pc.off, (size_t) len); buf[len] = 0;
		ftext (buf, tx + pc.x, y + 28, TC_GREY, px);
	}
	if (rr.lines > 1) ftext ("...", tx + ex, y + 28, TC_GREY, px);
}
static void ls_paint (long v, long w, long h)
{
	begin_paint (v);
	s_lw = (int) w; s_lh = (int) h;
	ui_fill (0, 0, w, h, TC_BG);
	int y = -s_lscroll;
	for (int i = 0; i < s_nrows; i++)
	{
		Row &r = s_rows[i];
		int rh = row_h (r);
		if (y + rh > 0 && y < h) ls_draw_row (r, i, y, rh, (int) w);
		y += rh;
	}
	ui_fill (w - 1, 0, 1, h, TC_LINE);
	const char *q = (const char *) ui_w_get_text (g_search);
	if (!g_c.dialogsLoaded) ftext (TR ("Loading your conversations..."), 16, imin (y + 8, (int) h - 24), TC_GREY, ui_px ());
	else if (!g_c.norder) ftext (q[0] ? TR ("Nobody by that name.") : TR ("No conversations yet."), 16, imin (y + 8, (int) h - 24), TC_GREY, ui_px ());
}
static long ls_mouse (long, long kind, long x, long y, long info)
{
	int row = kind == 3 ? -1 : ls_row_at ((int) y);
	if (row != s_lhot) { s_lhot = row; ui_view_dirty (g_list); }
	if (kind != 1 || row < 0) return 0;
	int buttons = (int) (info >> 8 & 255), clicks = (int) (info & 255);
	Row &r = s_rows[row];
	if (buttons & 2) { if (r.kind != R_GROUP) { s_rowMenu = r.peer; s_rowX = ui_ev_field (&ui, EV_X); s_rowY = ui_ev_field (&ui, EV_Y); } return 0; }
	if (r.kind == R_GROUP) { s_folded[r.group] = !s_folded[r.group]; g_listDirty = true; }
	else open_conversation (r.peer, clicks >= 2);
	(void) x;
	return 0;
}
static long ls_wheel (long, long steps)
{
	s_lscroll += (int) steps * 3 * CONTACT_H;
	ls_clamp (); ls_bar (); ui_view_dirty (g_list);
	return 0;
}
// keys from the window: up / down move the selection
static bool ls_move (int d)
{
	int cur = -1;
	for (int i = 0; i < s_nrows; i++) if (s_rows[i].kind != R_GROUP && s_rows[i].peer == s_selected) cur = i;
	for (int i = cur + d; i >= 0 && i < s_nrows; i += d)
		if (s_rows[i].kind != R_GROUP) { open_conversation (s_rows[i].peer); ls_ensure (i); return true; }
	return false;
}
static void ls_scroll_to (long long peer) { for (int i = 0; i < s_nrows; i++) if (s_rows[i].peer == peer) { ls_ensure (i); break; } }

static void ls_create ()
{
	g_me = ui_view (0, 0, 10, ME_H, 0);
	ui_view_on (g_me, (long) me_paint, (long) me_mouse, 0, 0);
	g_searchRow = ui_panel (0, 0, 10, SEARCH_H, 0);
	g_search = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_w_set_parent (g_search, g_searchRow);
	ui_w_set_range (g_search, 0, 60, 0, 0);
	ui_textbox_hint (g_search, (long) TR ("Search..."));
	g_add = ui_pushbutton (0, 0, 32, 0, (long) "+", UB_GHOST);
	ui_w_set_parent (g_add, g_searchRow);
	ui_w_tip (g_add, (long) TR ("Add a contact by phone number"));
	g_list = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_list, (long) ls_paint, (long) ls_mouse, 0, (long) ls_wheel);
	g_listBar = ui_scrollbar (0, 0, 10, 10, 0);
	ui_w_set_parent (g_listBar, g_list);
	ui_w_show (g_listBar, 0);
}
static void ls_move_to (int x, int y, int w, int h)
{
	int c = ctl_h ();
	ui_w_move (g_me, x, y, w, ME_H);
	ui_w_move (g_searchRow, x, y + ME_H, w, SEARCH_H);
	ui_w_move (g_search, x + 10, y + ME_H + (SEARCH_H - c) / 2, w - 58, c);
	ui_w_move (g_add, x + w - 42, y + ME_H + (SEARCH_H - c) / 2, 32, c);
	int ly = y + ME_H + SEARCH_H;
	s_lw = w; s_lh = h - ME_H - SEARCH_H;
	ui_w_move (g_list, x, ly, w, s_lh);
	ui_w_move (g_listBar, x + w - 13, ly, 12, s_lh);
	ls_clamp (); ls_bar ();
	ui_view_dirty (g_me); ui_view_dirty (g_list);
}
static void ls_show (bool on)
{
	ui_w_show (g_me, on); ui_w_show (g_searchRow, on); ui_w_show (g_list, on);
}

#endif
