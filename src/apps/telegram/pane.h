//
// pane.h -- what shows one conversation: the header on the sky (the picture in its glass frame, the name, the status or
// "... is typing a message"), the bar of a person who is not a contact, the messages in the way of Messenger -- "Alice
// says:" over what she wrote, the time on the right (and for ours the ticks: sent, read), a day's line, a group's events in
// grey, the emoticons as pictures --, the display pictures' column, and where a message is written: a strip of tools, the
// text itself -- wrapped, its emoticons drawn as pictures while it is written --, Send.
//
// A pane lives in the main window, beside the list, or in a window of its own, one more window of this program (UIKit's
// ui_window_use, as IRC's private windows): Onyx's ChatView, ChatEdit, InputBar... are ui_views here, each named by its pane.
// One prefix, cv_ the messages, ed_ the text, pn_ the pane; the view's callbacks find their pane by pn_of ().
//
// MIT licence.
//
#ifndef TG_PANE_H
#define TG_PANE_H

#include <spawn.h>
#include "buddylist.h"
#include "../media/jpegsave.h"

extern char **environ;
static bool g_demo = false, g_pictures = true;
static bool g_windowed = true;			// (config.ini windows=) a conversation opens in its own window
static long g_mainWin;
static void notify (const char *title, const char *msg);

#define LEFT		30			// the messages' left margin
#define TIMEW		64			// the times' column
#define DP_W		150			// the display pictures' column
#define BAR_H		34
#define STRIP		30
#define ED_CAP		4096

// The picture to send with the next message (attached by the button or a drop): the JPEG made of it, its size, a small
// view of it, its name.
struct Attach { unsigned char *jpg; int n, w, h; unsigned *thumb; int tw, th; char name[80]; };

enum { I_DAY, I_HEAD, I_MSG, I_SERVICE };
struct Item { int kind, y, h, msg; Rich *rich; int big, phW, phH; bool label; };

struct Pane
{
	long win;				// its window (0: the slot is free)
	bool alone;				// a window of its own
	long long open;				// the conversation shown (0: none)
	Attach att;
	long head, back, bar, barAdd, barBlock, barHide, chat, chatBar, strip, edit, send, dp, picker;
	int x, w, h, headH, inputH, cw, chH;	// where it is: its left, size; its header, its line; the messages' size
	bool dp_on, bar_on, conv_on, narrow;
	// the messages
	Item *it; int n, cap, total, scroll;
	bool stick;
	long long peer;
	unsigned rev;
	int lw;
	bool hand, loading;
	bool dirty;
	// the text
	char buf[ED_CAP];
	int len, caret, escroll;
	Rich r;
	unsigned erev, elaid;
	int ew, eh, elw;
	bool blinkOff; long long blinkAt;
	// the picker
	bool pickerOn; int pickHot;
	int stripHot, closeX;
	unsigned seen;
};

enum { MAXPANE = 13 };
static Pane g_panes[MAXPANE];			// [0]: the main window's
#define g_main (&g_panes[0])
static Pane *s_pickFor;				// (the strip's picture button: the loop opens the dialogue)
static int s_bar3;

static Pane *pn_of (long v)
{
	for (int i = 0; i < MAXPANE; i++)
	{
		Pane *p = &g_panes[i];
		if (p->win && (p->chat == v || p->head == v || p->bar == v || p->strip == v || p->edit == v || p->dp == v || p->picker == v)) return p;
	}
	return 0;
}
static Pane *pn_of_win (long w) { for (int i = 0; i < MAXPANE; i++) if (g_panes[i].win == w) return &g_panes[i]; return 0; }
static Pane *pn_of_widget (long w)
{
	for (int i = 0; i < MAXPANE; i++)
	{
		Pane *p = &g_panes[i];
		if (p->win && (p->chatBar == w || p->send == w || p->back == w || p->barAdd == w || p->barBlock == w || p->barHide == w)) return p;
	}
	return 0;
}

static void spawn_app (const char *path, const char *arg)
{
	char *argv[3] = { (char *) path, (char *) arg, 0 };
	pid_t p;
	posix_spawn (&p, path, 0, 0, argv, environ);
}
static void open_link (const char *url)
{
	char u[600];
	if (!strncmp (url, "www.", 4) || !strncmp (url, "t.me/", 5)) snprintf (u, sizeof u, "https://%s", url);
	else snprintf (u, sizeof u, "%s", url);
	spawn_app ("SD0:/apps/jet.app/main", u);
}
struct OpenPic { char path[160]; int w, h; };
static OpenPic s_openPic;			// (asked from a view's mouse: the loop opens the window, main.cpp)

// A day's title: Today, Yesterday, or the date.
static void day_title (int t, char *out, int cap)
{
	long d = local_day (t), today = local_day (g_c.serverTime ());
	if (d == today) { snprintf (out, (size_t) cap, "%s", TR ("Today")); return; }
	if (d == today - 1) { snprintf (out, (size_t) cap, "%s", TR ("Yesterday")); return; }
	struct tm m; local_tm (t, &m);
	static const char *mon[] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"), TRN ("June"), TRN ("July"), TRN ("August"), TRN ("September"), TRN ("October"), TRN ("November"), TRN ("December") };
	static const char *wd[] = { TRN ("Sunday"), TRN ("Monday"), TRN ("Tuesday"), TRN ("Wednesday"), TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday") };
	snprintf (out, (size_t) cap, "%s %d %s %d", TR (wd[m.tm_wday]), m.tm_mday, TR (mon[m.tm_mon]), m.tm_year + 1900);
}

// A name short: a user's first name, a chat's title.
static void short_name (long long peer, char *out, int cap)
{
	if (tg::ptype (peer) == tg::P_USER)
	{
		tg::User *u = g_c.user (tg::pid (peer));
		if (u && u->first && u->first[0]) { snprintf (out, (size_t) cap, "%s", u->first); return; }
	}
	g_c.peerName (peer, out, cap);
}

// ---- the header -------------------------------------------------------------------------------------------

static void head_paint (long v, long w, long h)
{
	Pane *P = pn_of (v); if (!P) return;
	begin_paint (v);
	sky (0, 0, (int) w, (int) h);
	long long peer = P->open;
	if (!peer) return;
	int now = g_c.serverTime ();
	tg::Conv *c = g_c.conv (peer);
	bool user = tg::ptype (peer) == tg::P_USER;
	tg::User *u = user ? g_c.user (tg::pid (peer)) : 0;
	unsigned st = user ? ts_colour (user_ts (u, now)) : ts_colour (TS_GROUP);
	int s = (int) h - 26, x = P->narrow && !P->alone ? 56 : 16;
	avatar_framed (g_c, peer, x, ((int) h - s) / 2, s, st);
	int tx = x + s + 18;
	char name[160];
	g_c.peerName (peer, name, sizeof name);
	bool small = h < 60;
	int ty = small ? 6 : 12;
	ftext (name, tx, ty, TC_TITLE, small ? PX_BIG : PX_TITLE, 2, (int) w - tx - 14);
	char line[200] = "";
	unsigned col = TC_GREY;
	if (c && c->typingUntil)
	{
		char who[80];
		short_name (c->typingUser, who, sizeof who);
		snprintf (line, sizeof line, c->typingKind == 1 ? TR ("%s is recording a voice message...") : TR ("%s is typing a message..."), who);
		col = TC_LINK;
	}
	else if (user) status_text (g_c, u, line, sizeof line);
	else
	{
		tg::Chat *ch = g_c.chat (tg::pid (peer));
		if (ch && ch->members) snprintf (line, sizeof line, ch->kind == 3 ? TR ("%d subscribers") : TR ("%d members"), ch->members);
		else snprintf (line, sizeof line, "%s", ch && ch->kind == 3 ? TR ("channel") : TR ("group"));
	}
	if (user && u && u->username && u->username[0] && !(c && c->typingUntil))
	{
		char l2[240]; snprintf (l2, sizeof l2, "%s  -  @%s", line, u->username); snprintf (line, sizeof line, "%s", l2);
	}
	ftext (line, tx, small ? 28 : 40, col, ui_px (), c && c->typingUntil ? 1 : 0, (int) w - tx - 14);
}

// ---- the bar of a person who is not a contact: "Add to contacts", "Block" (or "Unblock"), the cross ---------

static void bar_paint (long v, long w, long h)
{
	Pane *P = pn_of (v); if (!P) return;
	begin_paint (v);
	ui_fill (0, 0, w, h, jet (J_FACE));			// (the buttons' focus ring wants the face under them)
	ui_fill (0, 0, imax (0, s_bar3 - 4), h, C.noteBg);
	ui_fill (0, h - 1, w, 1, TC_LINE);
	tg::Conv *c = P->open ? g_c.conv (P->open) : 0;
	if (!c) return;
	char name[80], t[200];
	short_name (P->open, name, sizeof name);
	vp_disc (16, h / 2.0f, 8, jet (J_ALERTE));
	int ew = ftw ("!", ui_px (), 2);
	fbox ("!", 16 - ew / 2, 0, (int) h, ink_on (jet (J_ALERTE)), ui_px (), 2);
	if (c->blocked) snprintf (t, sizeof t, TR ("You blocked %s: they cannot write to you."), name);
	else snprintf (t, sizeof t, TR ("%s is not in your contacts."), name);
	fbox (t, 32, 0, (int) h, TC_INK, ui_px (), 0, s_bar3 > 40 ? s_bar3 - 40 : 1);
}
static void bar_layout (Pane *P)
{
	tg::Conv *c = P->open ? g_c.conv (P->open) : 0;
	if (!c) return;
	bool blocked = c->blocked;
	ui_w_set_text (P->barAdd, (long) (blocked ? TR ("Unblock") : TR ("Add to contacts")));
	ui_w_show (P->barBlock, !blocked); ui_w_show (P->barHide, !blocked);
	int ch = ctl_h (), y = P->headH + (BAR_H - ch) / 2, x = P->x + P->cw - 6;
	if (!blocked)
	{
		x -= 32; ui_w_move (P->barHide, x, y, 30, ch);
		int bw = ftw (TR ("Block"), ui_px (), 2) + 22;
		x -= bw + 4; ui_w_move (P->barBlock, x, y, bw, ch);
	}
	int aw = ftw (blocked ? TR ("Unblock") : TR ("Add to contacts"), ui_px (), 2) + 22;
	x -= aw + 4; ui_w_move (P->barAdd, x, y, aw, ch);
	s_bar3 = x - P->x;
}

// ---- the messages ----------------------------------------------------------------------------------------------

static inline bool is_picture (const tg::Msg &m) { return m.media == tg::M_PHOTO && (m.photoId || m.local); }
static void cv_fit_picture (Pane *P, const tg::Msg &m, int &w, int &h)
{
	int mw = imax (80, imin (320, P->cw - LEFT - TIMEW - 24)), mh = 260;
	int pw = m.pw > 0 ? m.pw : 4, ph = m.ph > 0 ? m.ph : 3;
	w = mw; h = (int) ((long) ph * w / pw);
	if (h > mh) { h = mh; w = (int) ((long) pw * h / ph); }
	if (w < 40) w = 40;
	if (h < 30) h = 30;
}
static void cv_clamp (Pane *P)
{
	int most = P->total - P->chH;
	if (P->scroll > most) P->scroll = most;
	if (P->scroll < 0) P->scroll = 0;
}
static void cv_bar (Pane *P)
{
	bool need = P->total > P->chH;
	ui_w_show (P->chatBar, need);
	if (need) { ui_w_set_range (P->chatBar, 0, P->total, P->chH, 48); ui_w_set_value (P->chatBar, P->scroll); }
}
static Item &cv_push (Pane *P, int kind, int msg, int h)
{
	if (P->n == P->cap) { P->cap = P->cap ? P->cap * 2 : 64; P->it = (Item *) realloc (P->it, sizeof (Item) * (size_t) P->cap); }
	Item &it = P->it[P->n++];
	memset (&it, 0, sizeof it);
	it.kind = kind; it.msg = msg; it.h = h; it.y = P->total;
	P->total += h;
	return it;
}
static void cv_free (Pane *P)
{
	for (int i = 0; i < P->n; i++) delete P->it[i].rich;
	P->n = 0; P->total = 0;
}

// The text of a message that is not text: "[Photo]", a service's sentence.
static void media_text (tg::Msg &m, char *out, int cap)
{
	out[0] = 0;
	if (m.media == tg::M_SERVICE)
	{
		char who[80], whom[80] = "";
		short_name (m.from, who, sizeof who);
		if (m.svcUser) short_name (tg::pkey (tg::P_USER, m.svcUser), whom, sizeof whom);
		switch (m.service)
		{
		case tg::SV_CREATE: snprintf (out, (size_t) cap, TR ("%s created the group \"%s\""), who, m.extra); break;
		case tg::SV_TITLE: snprintf (out, (size_t) cap, TR ("%s changed the group's name to \"%s\""), who, m.extra); break;
		case tg::SV_PHOTO: snprintf (out, (size_t) cap, TR ("%s changed the group's picture"), who); break;
		case tg::SV_ADD: snprintf (out, (size_t) cap, TR ("%s added %s"), who, whom); break;
		case tg::SV_LEFT: snprintf (out, (size_t) cap, TR ("%s left the group"), who); break;
		case tg::SV_REMOVE: snprintf (out, (size_t) cap, TR ("%s removed %s"), who, whom); break;
		case tg::SV_JOINED_LINK: snprintf (out, (size_t) cap, TR ("%s joined the group"), who); break;
		case tg::SV_PIN: snprintf (out, (size_t) cap, TR ("%s pinned a message"), who); break;
		case tg::SV_CALL: snprintf (out, (size_t) cap, "%s", TR ("A call")); break;
		case tg::SV_JOINED_TG: snprintf (out, (size_t) cap, TR ("%s joined Telegram"), who); break;
		case tg::SV_SCREENSHOT: snprintf (out, (size_t) cap, TR ("%s took a screenshot"), who); break;
		default: snprintf (out, (size_t) cap, "%s", TR ("(an event of the group)")); break;
		}
		return;
	}
	static const char *names[] = { "", TRN ("Photo"), TRN ("Video"), TRN ("Voice message"), TRN ("Audio"), TRN ("File"), TRN ("Sticker"), TRN ("GIF"), TRN ("Location"), TRN ("Contact"), TRN ("Poll"), TRN ("Link"), TRN ("Something this app cannot show yet") };
	if (m.extra && m.extra[0] && m.media != tg::M_STICKER) snprintf (out, (size_t) cap, "[%s: %s]", TR (names[m.media < 13 ? m.media : 12]), m.extra);
	else snprintf (out, (size_t) cap, "[%s]", TR (names[m.media < 13 ? m.media : 12]));
}

// What an item's text is: its message's, a sticker's emoji.
static inline const char *item_text (const Item &it, tg::Msg &m) { return m.media == tg::M_STICKER && it.big ? m.extra : m.text; }

static void cv_lay (Pane *P, tg::Conv *c)
{
	cv_free (P);
	if (!c) return;
	int px = ui_px (), W = P->cw;
	int textW = imax (60, W - LEFT - TIMEW - 20);
	P->total = 8;
	long lastDay = -1;
	long long lastFrom = -1;
	int lastDate = 0;
	for (int i = 0; i < c->n; i++)
	{
		tg::Msg &m = c->m[i];
		long d = local_day (m.date);
		if (d != lastDay) { cv_push (P, I_DAY, i, 34); lastDay = d; lastFrom = -1; }
		if (m.media == tg::M_SERVICE)
		{
			char t[300]; media_text (m, t, sizeof t);
			Item &it = cv_push (P, I_SERVICE, i, 0);
			it.rich = new Rich; rich_layout (*it.rich, PX_SMALL, t, W - 60, 14, false);
			it.h = it.rich->height () + 10; P->total += it.h;
			lastFrom = -1;
			continue;
		}
		long long from = m.from ? m.from : c->peer;
		if (from != lastFrom || m.date - lastDate > 600) { cv_push (P, I_HEAD, i, lastFrom == -1 ? 24 : 30); lastFrom = from; }
		lastDate = m.date;
		Item &it = cv_push (P, I_MSG, i, 0);
		it.rich = new Rich;
		if (is_picture (m))			// a photo: the picture, its caption under it
		{
			cv_fit_picture (P, m, it.phW, it.phH);
			rich_layout (*it.rich, px, m.text, textW, 19);
			it.h = it.phH + 6 + (m.text[0] ? it.rich->height () + 2 : 0);
			P->total += it.h;
			continue;
		}
		if (m.media)				// a sticker's emoji big; the others a label, their caption under it
		{
			if (m.media == tg::M_STICKER && m.extra && m.extra[0] && emo_at (m.extra, (int) strlen (m.extra), &it.big, false) >= 0) it.big = 48;
			else { it.big = 0; it.label = true; }
		}
		else it.big = rich_only_emo (m.text) ? 32 : 0;
		rich_layout (*it.rich, px, item_text (it, m), textW, it.big ? it.big : 19);
		int lh = it.label ? 22 : 0;
		it.h = imax (it.rich->height () + 3 + lh, 20);
		P->total += it.h;
	}
	P->total += 10;
}

// The conversation laid out again, its bar (from the loop: never inside a paint).
static void cv_sync (Pane *P)
{
	tg::Conv *c = P->open ? g_c.conv (P->open) : 0;
	if (P->open != P->peer) { P->stick = true; P->scroll = 0; }
	int oldTotal = P->total, oldScroll = P->scroll;
	P->peer = P->open; P->rev = c ? c->rev : 0; P->lw = P->cw;
	cv_lay (P, c);
	if (P->stick) P->scroll = P->total - P->chH;
	else if (P->total > oldTotal && oldScroll < 60) P->scroll = oldScroll + (P->total - oldTotal);	// (older ones came above)
	cv_clamp (P);
	cv_bar (P);
	ui_view_dirty (P->chat);
	if (P->pickerOn) ui_view_dirty (P->picker);
}
static void cv_scroll_by (Pane *P, int d)
{
	P->scroll += d;
	cv_clamp (P);
	P->stick = P->scroll >= P->total - P->chH - 4;
	cv_bar (P);
	ui_view_dirty (P->chat);
	if (P->pickerOn) ui_view_dirty (P->picker);
	if (P->scroll < 60 && P->open) g_c.loadOlder (P->open);
}

static void media_icon (int media, int x, int y)
{
	switch (media)
	{
	case tg::M_PHOTO: case tg::M_GIF:
	{
		ui_rbox (x, y + 1, 15, 12, 2, 0x8EC5F0, 0x4A8FD0);
		vp_disc (x + 4.0f, y + 5.0f, 2, 0xFFE27A);
		float t[6] = { x + 2.0f, y + 12.0f, x + 7.0f, y + 6.0f, x + 12.0f, y + 12.0f };
		vp_poly (t, 3, 0x2E7D32);
		break;
	}
	case tg::M_VIDEO:
	{
		ui_rbox (x, y + 2, 11, 10, 2, 0x6A7A8C, 0x3A4A5C);
		float t[6] = { x + 11.0f, y + 7.0f, x + 15.0f, y + 3.0f, x + 15.0f, y + 11.0f };
		vp_poly (t, 3, 0x3A4A5C);
		break;
	}
	case tg::M_VOICE: case tg::M_AUDIO:
		vp_rrect (x + 5.0f, (float) y, 5, 9, 2.5f, TC_GREY);
		vp_arc (x + 7.5f, y + 6.0f, 5, 180, 360, 1.2f, TC_GREY);
		vp_line (x + 7.5f, y + 11.0f, x + 7.5f, y + 14.0f, 1.2f, TC_GREY);
		break;
	case tg::M_GEO:
	{
		vp_disc (x + 7.0f, y + 5.0f, 5, 0xE04848);
		float t[6] = { x + 3.0f, y + 8.0f, x + 11.0f, y + 8.0f, x + 7.0f, y + 14.0f };
		vp_poly (t, 3, 0xE04848);
		vp_disc (x + 7.0f, y + 5.0f, 2, 0xFFFFFF);
		break;
	}
	default:
	{
		float t[10] = { x + 2.0f, (float) y, x + 9.0f, (float) y, x + 13.0f, y + 4.0f, x + 13.0f, y + 14.0f, x + 2.0f, y + 14.0f };
		vp_poly (t, 5, TC_GREY);
		float i[10] = { x + 3.0f, y + 1.0f, x + 9.0f, y + 1.0f, x + 12.0f, y + 4.0f, x + 12.0f, y + 13.0f, x + 3.0f, y + 13.0f };
		vp_poly (i, 5, TC_BG);
		break;
	}
	}
}

static void cv_draw_picture (Pane *P, tg::Msg &m, int x, int y, int w, int h)
{
	char path[160];
	unsigned *px = g_c.msgPhoto (m, path, sizeof path) ? img_get (path, w, h, false) : 0;
	if (px) blit_rect_round (px, x, y, w, h, 6);
	else
	{
		ui_rbox (x, y, w, h, 6, TC_HOT_BOT, TC_SEL_BOT);
		media_icon (tg::M_PHOTO, x + w / 2 - 8, y + h / 2 - 14);
		const char *l = TR ("Loading the picture...");
		ftext (l, x + (w - ftw (l, PX_SMALL, 1)) / 2, y + h / 2 + 6, TC_GREY, PX_SMALL, 1);
	}
	if (m.pending || m.failed)
	{
		char s[80];
		if (m.failed) snprintf (s, sizeof s, "%s", TR ("Not sent"));
		else snprintf (s, sizeof s, TR ("Sending... %d%%"), m.progress);
		int sw = ftw (s, PX_SMALL, 2) + 16;
		ui_rbox (x + 8, y + h - 28, sw, 20, 10, jet (J_TEXTE1), jet (J_TEXTE1));
		fbox (s, x + 16, y + h - 28, 20, m.failed ? jet (J_DANGER) : jet (J_SURFACE), PX_SMALL, 2);
		if (!m.failed)
		{
			int bw = w - 16;
			ui_fill (x + 8, y + h - 5, bw, 3, TC_LIGHT);
			ui_fill (x + 8, y + h - 5, bw * m.progress / 100, 3, TC_ONLINE);
		}
	}
}

// the time on the right, and for ours the ticks (a clock while it goes, a red ! if it could not)
static void cv_draw_time (Pane *P, tg::Conv *c, tg::Msg &m, int y, int big, int lineH)
{
	char tm[8]; hhmm (m.date, tm, sizeof tm);
	int tx = P->cw - 12 - TIMEW + 6, ty = big ? y + 4 : y + (lineH - fh (PX_SMALL)) / 2;
	ftext (tm, tx, ty, TC_LIGHT, PX_SMALL);
	if (m.out)
	{
		float kx = (float) (tx + ftw (tm, PX_SMALL) + 5), ky = (float) (ty + 3);
		if (m.failed) ftext ("!", (int) kx, ty, TC_BUSY, PX_SMALL, 2);
		else if (m.pending)
		{
			vp_arc (kx + 4, ky + 4, 4, 0, 360, 1, TC_LIGHT);
			vp_line (kx + 4, ky + 4, kx + 4, ky + 1, 1, TC_LIGHT);
			vp_line (kx + 4, ky + 4, kx + 6, ky + 5, 1, TC_LIGHT);
		}
		else
		{
			bool read = c->readOutMax >= m.id;
			unsigned k = read ? C.ticks : TC_LIGHT;
			vp_line (kx, ky + 4, kx + 3, ky + 7, 1.4f, k); vp_line (kx + 3, ky + 7, kx + 8, ky, 1.4f, k);
			if (read) { vp_line (kx + 5, ky + 6, kx + 6, ky + 7, 1.4f, k); vp_line (kx + 6, ky + 7, kx + 11, ky, 1.4f, k); }
		}
	}
	if (m.edited) ftext (TR ("edited"), tx - ftw (TR ("edited"), PX_SMALL, 1) - 6, ty, TC_LIGHT, PX_SMALL, 1);
}
static void cv_note (int w, int y, const char *s)
{
	int tw = ftw (s, ui_px (), 1);
	ui_rbox ((w - tw) / 2 - 12, y, tw + 24, 22, 11, TC_HOT_TOP, TC_HOT_BOT);
	fbox (s, (w - tw) / 2, y, 22, TC_GREY, ui_px (), 1);
}
static void cv_draw_item (Pane *P, tg::Conv *c, Item &it, int y)
{
	tg::Msg &m = c->m[it.msg];
	int W = P->cw, px = ui_px ();
	switch (it.kind)
	{
	case I_DAY:
	{
		char t[80]; day_title (m.date, t, sizeof t);
		int tw = ftw (t, PX_SMALL, 2), cx = (W - 12) / 2;
		ui_fill (20, y + 17, imax (0, cx - tw / 2 - 30), 1, TC_LINE);
		ui_fill (cx + tw / 2 + 10, y + 17, imax (0, W - 12 - 20 - (cx + tw / 2 + 10)), 1, TC_LINE);
		ftext (t, cx - tw / 2, y + 10, TC_GREY, PX_SMALL, 2);
		return;
	}
	case I_HEAD:
	{
		long long from = m.from ? m.from : c->peer;
		char name[120];
		if (m.out) snprintf (name, sizeof name, "%s", g_c.self () ? (g_c.self ()->first ? g_c.self ()->first : "") : "");
		else short_name (from, name, sizeof name);
		int ty = y + it.h - fh (px) - 2;
		bool grp = tg::ptype (c->peer) != tg::P_USER;
		if (grp && !m.out) avatar_round (g_c, from, 6, ty - 1, 18);
		else buddy (8, ty + 1, 14, m.out ? TC_ME : TC_THEM);
		int nw = ftw (name, px, 2);
		unsigned nc = m.out ? TC_ME : TC_THEM;
		if (!m.out && grp) nc = readable (tc_peer[peer_colour (tg::pid (from))][1], TC_BG, 4.2);	// (a group: each one's colour)
		ftext (name, LEFT, ty, nc, px, 2, W - LEFT - 120);
		ftext (TR ("says:"), LEFT + imin (nw, W - LEFT - 120) + 5, ty, TC_GREY, px);
		return;
	}
	case I_SERVICE:
	{
		char buf[300]; media_text (m, buf, sizeof buf);
		rich_draw (*it.rich, buf, (W - 12 - it.rich->widest) / 2, y + 5, TC_GREY, 1);
		return;
	}
	case I_MSG:
	{
		unsigned ink = m.pending ? TC_GREY : m.failed ? TC_BUSY : TC_INK;
		if (it.phW)
		{
			cv_draw_picture (P, m, LEFT, y + 2, it.phW, it.phH);
			if (m.text && m.text[0]) rich_draw (*it.rich, m.text, LEFT, y + it.phH + 6, ink);
			cv_draw_time (P, c, m, y, 0, it.rich->lineH);
			return;
		}
		int ly = y;
		if (it.label)
		{
			char lab[300]; media_text (m, lab, sizeof lab);
			media_icon (m.media, LEFT, y + 3);
			fbox (lab, LEFT + 18, y, 20, TC_GREY, px, 1, W - LEFT - TIMEW - 36);
			ly += 22;
		}
		rich_draw (*it.rich, item_text (it, m), LEFT, ly, ink);
		cv_draw_time (P, c, m, y, it.big, it.rich->lineH);
		return;
	}
	}
}
static void cv_paint (long v, long w, long h)
{
	Pane *P = pn_of (v); if (!P) return;
	begin_paint (v);
	ui_fill (0, 0, w, h, TC_BG);
	if (!P->open) return;
	tg::Conv *c = g_c.conv (P->open);
	if (c && P->peer == P->open && P->rev == c->rev)	// (the items are this conversation's, as it is: else cv_sync is coming)
		for (int i = 0; i < P->n; i++)
		{
			Item &it = P->it[i];
			int y = it.y - P->scroll;
			if (y + it.h < 0 || y > h) continue;
			cv_draw_item (P, c, it, y);
		}
	if (c && c->loading) cv_note ((int) w, 8, TR ("Loading the messages..."));
	else if (c && !c->n && c->loaded) cv_note ((int) w, (int) h / 2 - 10, TR ("No messages here yet. Say hello!"));
}

// the picture or the link under (mx, my) (content coordinates)
static bool cv_link_at (Pane *P, int mx, int my, char *url, int cap)
{
	tg::Conv *c = P->open ? g_c.conv (P->open) : 0;
	if (!c || P->peer != P->open || P->rev != c->rev) return false;
	for (int i = 0; i < P->n; i++)
	{
		Item &it = P->it[i];
		if (it.kind != I_MSG || my < it.y || my >= it.y + it.h) continue;
		tg::Msg &m = c->m[it.msg];
		int oy = it.phW ? it.phH + 6 : it.label ? 22 : 0;
		return rich_link (*it.rich, item_text (it, m), mx - LEFT, my - it.y - oy, url, cap);
	}
	return false;
}
static bool cv_picture_at (Pane *P, int mx, int my, char *path, int cap, int *pw = 0, int *ph = 0)
{
	tg::Conv *c = P->open ? g_c.conv (P->open) : 0;
	if (!c || P->peer != P->open || P->rev != c->rev) return false;
	for (int i = 0; i < P->n; i++)
	{
		Item &it = P->it[i];
		if (it.kind != I_MSG || !it.phW || my < it.y + 2 || my >= it.y + 2 + it.phH || mx < LEFT || mx >= LEFT + it.phW) continue;
		if (pw) { *pw = c->m[it.msg].pw; *ph = c->m[it.msg].ph; }
		return g_c.msgPhoto (c->m[it.msg], path, cap);
	}
	return false;
}
static void pn_picker_show (Pane *P, bool on);
static long cv_mouse (long v, long kind, long x, long y, long info)
{
	Pane *P = pn_of (v); if (!P) return 0;
	if (kind == 3) { if (P->hand) { P->hand = false; ui_cursor (UC_ARROW); } return 0; }
	if (kind == 2) return 0;
	char url[512], pic[160];
	bool link = cv_link_at (P, (int) x, (int) y + P->scroll, url, sizeof url);
	int pw = 0, ph = 0;
	bool picture = !link && cv_picture_at (P, (int) x, (int) y + P->scroll, pic, sizeof pic, &pw, &ph);
	if ((link || picture) != P->hand) { P->hand = link || picture; ui_cursor (P->hand ? UC_HAND : UC_ARROW); }
	if (kind == 1 && ((info >> 8) & 1))
	{
		if (P->pickerOn) { pn_picker_show (P, false); return 0; }
		if (link) open_link (url);
		else if (picture) { snprintf (s_openPic.path, sizeof s_openPic.path, "%s", pic); s_openPic.w = pw; s_openPic.h = ph; }
	}
	return 0;
}
static long cv_wheel (long v, long steps)
{
	Pane *P = pn_of (v); if (P) cv_scroll_by (P, (int) steps * 48);
	return 0;
}

// ---- the display pictures' column ----------------------------------------------------------------------------

static void dp_paint (long v, long w, long h)
{
	Pane *P = pn_of (v); if (!P) return;
	begin_paint (v);
	ui_rbox (0, 0, w, h, 0, TC_SKY_BOT, TC_BG);
	ui_fill (0, 0, 1, h, TC_LINE);
	if (!P->open) return;
	int now = g_c.serverTime ();
	int s = imin ((int) w - 32, 96);
	bool user = tg::ptype (P->open) == tg::P_USER;
	unsigned st = user ? ts_colour (user_ts (g_c.user (tg::pid (P->open)), now)) : ts_colour (TS_GROUP);
	avatar_framed (g_c, P->open, ((int) w - s) / 2, 18, s, st);
	long long me = tg::pkey (tg::P_USER, g_c.selfId);
	int ms = s * 3 / 4;
	avatar_framed (g_c, me, ((int) w - ms) / 2, (int) h - P->inputH + (P->inputH - ms) / 2, ms, g_c.wantOnline () && g_c.online ? TC_ONLINE : TC_OFFLINE);
}

// ---- the text ---------------------------------------------------------------------------------------------------

static void send_current (Pane *P);
static void ed_changed (Pane *P);

static void ed_lay (Pane *P)
{
	if (P->elaid == P->erev && P->elw == P->ew) return;
	rich_layout (P->r, ui_px (), P->buf, P->ew - 14, 19);
	if (!P->r.lines) P->r.lines = 1;
	P->elaid = P->erev; P->elw = P->ew;
}
static void ed_clear (Pane *P) { P->len = P->caret = 0; P->buf[0] = 0; P->escroll = 0; ed_changed (P); }
static void ed_insert (Pane *P, const char *s)
{
	int n = (int) strlen (s);
	if (P->len + n >= ED_CAP) n = ED_CAP - 1 - P->len;
	if (n <= 0) return;
	memmove (P->buf + P->caret + n, P->buf + P->caret, (size_t) (P->len - P->caret + 1));
	memcpy (P->buf + P->caret, s, (size_t) n);
	P->len += n; P->caret += n;
	ed_changed (P);
}
// the emoticon sequence (or the character) ending at / starting at i
static int ed_before (Pane *P, int i)
{
	if (i <= 0) return 0;
	ed_lay (P);
	for (int k = 0; k < P->r.n; k++) if (P->r.p[k].kind == RP_EMO && P->r.p[k].off + P->r.p[k].len == i) return P->r.p[k].off;
	return u8_prev (P->buf, i);
}
static int ed_after (Pane *P, int i)
{
	if (i >= P->len) return P->len;
	ed_lay (P);
	for (int k = 0; k < P->r.n; k++) if (P->r.p[k].kind == RP_EMO && P->r.p[k].off == i) return i + P->r.p[k].len;
	return u8_next (P->buf, i, P->len);
}
// the caret's place for byte offset o: x (px) and line
static void ed_caret_xy (Pane *P, int o, int *x, int *line)
{
	ed_lay (P);
	Rich &R = P->r;
	int px = 0, ln = 0, from = 0, fpx = ui_px ();
	int space = rich_w (fpx, " ", 1);
	for (int k = 0; k < R.n; k++)
	{
		RichPiece &pc = R.p[k];
		if (pc.off > o) break;
		if (o < pc.off + pc.len)
		{
			*line = pc.line;
			*x = pc.x + (pc.kind == RP_EMO ? 0 : rich_w (fpx, P->buf + pc.off, o - pc.off));
			return;
		}
		px = pc.x + pc.w + (pc.kind == RP_EMO ? 1 : 0); ln = pc.line; from = pc.off + pc.len;
	}
	// after the last piece before o: the spaces and line breaks between
	for (int i = from; i < o; i++)
	{
		if (P->buf[i] == '\n') { ln++; px = 0; }
		else if (P->buf[i] == ' ' && !(px == 0 && i > 0 && P->buf[i - 1] == '\n')) px += space;
	}
	*x = px; *line = ln;
}
// the byte offset nearest (x, y) in the text
static int ed_offset_at (Pane *P, int x, int y)
{
	ed_lay (P);
	Rich &R = P->r;
	int line = imax (0, y / (R.lineH ? R.lineH : 16));
	int best = 0, bestD = 1 << 30;
	for (int o = 0; o <= P->len; o = o < P->len ? ed_after (P, o) : P->len + 1)
	{
		int cx, cl; ed_caret_xy (P, o, &cx, &cl);
		if (cl == line)
		{
			int d = cx > x ? cx - x : x - cx;
			if (d < bestD) { bestD = d; best = o; }
		}
		else if (cl > line && bestD == 1 << 30) { best = o; break; }
		if (o == P->len) break;
	}
	if (bestD == 1 << 30 && line >= R.lines) best = P->len;
	return best;
}
static void ed_clamp (Pane *P)
{
	int most = P->r.lines * P->r.lineH + 8 - P->eh;
	if (P->escroll > most) P->escroll = most;
	if (P->escroll < 0) P->escroll = 0;
}
static void ed_ensure (Pane *P)
{
	int x, l; ed_caret_xy (P, P->caret, &x, &l);
	int y = l * P->r.lineH;
	if (y < P->escroll) P->escroll = y;
	if (y + P->r.lineH + 8 > P->escroll + P->eh) P->escroll = y + P->r.lineH + 8 - P->eh;
	ed_clamp (P);
}
static void ed_changed (Pane *P) { P->erev++; ed_lay (P); ed_ensure (P); P->blinkOff = false; ui_view_dirty (P->edit); }
static void ed_paint (long v, long w, long h)
{
	Pane *P = pn_of (v); if (!P) return;
	begin_paint (v);
	P->ew = (int) w; P->eh = (int) h;
	ed_lay (P);
	bool focus = ui_w_get_focus () == v;
	ui_fill (0, 0, w, h, TC_BG);
	int x0 = 6, y0 = 4 - P->escroll;
	if (!P->len && !focus) ftext (TR ("Type a message here"), x0, 6, TC_LIGHT, ui_px (), 1);
	rich_draw (P->r, P->buf, x0, y0, TC_INK);
	if (focus && !P->blinkOff)
	{
		int cx, cl;
		ed_caret_xy (P, P->caret, &cx, &cl);
		ui_fill (x0 + cx, y0 + cl * P->r.lineH + 2, 1, P->r.lineH - 3, TC_INK);
	}
	ui_frame (0, 0, w, h, focus ? jet (J_ACCENT) : jet (J_LIGNE_FORTE));
}
static long ed_mouse (long v, long kind, long x, long y, long)
{
	Pane *P = pn_of (v); if (!P) return 0;
	if (kind == 0 || kind == 3) return 0;
	if (kind == 1)
	{
		if (P->pickerOn) pn_picker_show (P, false);
		ui_w_focus (v);
		P->caret = ed_offset_at (P, (int) x - 6, (int) y - 4 + P->escroll);
		P->blinkOff = false;
		ui_view_dirty (v);
	}
	return 0;
}
static long ed_wheel (long v, long steps)
{
	Pane *P = pn_of (v); if (!P) return 0;
	P->escroll += (int) steps * P->r.lineH; ed_clamp (P); ui_view_dirty (v);
	return 0;
}
static void typed_something (Pane *P)
{
	tgc::entropy_add (0, 0);			// (the keys' timing: a little more randomness)
	if (P->open && !g_demo) g_c.typing (P->open);
}
static long ed_key (long v, long hid, long ch, long mods)
{
	Pane *P = pn_of (v); if (!P) return -1;
	bool ctrl = mods & UI_MOD_CTRL, shift = mods & UI_MOD_SHIFT, alt = mods & UI_MOD_ALT;
	P->blinkOff = false;
	switch (hid)
	{
	case KEY_ENTER:
		if (shift) { ed_insert (P, "\n"); typed_something (P); }
		else send_current (P);
		return 0;
	case KEY_BACKSPACE:
		if (P->caret > 0)
		{
			int s = ed_before (P, P->caret);
			memmove (P->buf + s, P->buf + P->caret, (size_t) (P->len - P->caret + 1));
			P->len -= P->caret - s; P->caret = s;
			ed_changed (P); typed_something (P);
		}
		return 0;
	case KEY_DELETE:
		if (P->caret < P->len)
		{
			int e = ed_after (P, P->caret);
			memmove (P->buf + P->caret, P->buf + e, (size_t) (P->len - e + 1));
			P->len -= e - P->caret;
			ed_changed (P);
		}
		return 0;
	case KEY_LEFT: P->caret = ed_before (P, P->caret); ed_ensure (P); ui_view_dirty (v); return 0;
	case KEY_RIGHT: P->caret = ed_after (P, P->caret); ed_ensure (P); ui_view_dirty (v); return 0;
	case KEY_HOME: { int x, l; ed_caret_xy (P, P->caret, &x, &l); P->caret = ed_offset_at (P, 0, l * P->r.lineH + 1); ui_view_dirty (v); return 0; }
	case KEY_END: { int x, l; ed_caret_xy (P, P->caret, &x, &l); P->caret = ed_offset_at (P, 100000, l * P->r.lineH + 1); ui_view_dirty (v); return 0; }
	case KEY_UP: case KEY_DOWN:
	{
		int x, l; ed_caret_xy (P, P->caret, &x, &l);
		l += hid == KEY_UP ? -1 : 1;
		if (l < 0 || l >= P->r.lines) return -1;
		P->caret = ed_offset_at (P, x, l * P->r.lineH + 1);
		ed_ensure (P); ui_view_dirty (v);
		return 0;
	}
	}
	if (ctrl && hid == 0x19)			// Ctrl+V
	{
		static char b[ED_CAP];
		int n = (int) ui_clip_get ((long) b, sizeof b - 1);
		if (n > 0) { b[n] = 0; ed_insert (P, b); typed_something (P); }
		return 0;
	}
	if (ctrl && hid == 0x04) { P->caret = P->len; ed_changed (P); return 0; }	// Ctrl+A (as on Onyx: the end)
	if (ctrl && hid == 0x06) { ui_clip_set ((long) P->buf, P->len); return 0; }	// Ctrl+C: the text (no selection here)
	if (ch >= 32 && !ctrl && !alt)
	{
		char u[8]; int n = u8_put (u, (unsigned) ch); u[n] = 0;
		ed_insert (P, u); typed_something (P);
		return 0;
	}
	return -1;
}

// ---- the emoticons' picker ----------------------------------------------------------------------------------

#define PK_COLS 8
#define PK_CELL 30
#define PK_W	(PK_COLS * PK_CELL + 12)
#define PK_H	(((EMO_COUNT + PK_COLS - 1) / PK_COLS) * PK_CELL + 34)
static void pk_paint (long v, long w, long h)
{
	Pane *P = pn_of (v); if (!P) return;
	begin_paint (v);
	ui_rbox (0, 0, w, h, 0, TC_BG, TC_HOT_TOP);
	ui_frame (0, 0, w, h, TC_SEL_RIM);
	int hot = P->pickHot;
	ftext (hot >= 0 ? TR (emo_info[hot].name) : TR ("Emoticons"), 8, 6, TC_GREY, PX_SMALL, hot >= 0 ? 2 : 0);
	if (hot >= 0 && emo_info[hot].text) ftext (emo_info[hot].text, (int) w - 8 - ftw (emo_info[hot].text, PX_SMALL), 6, TC_LIGHT, PX_SMALL);
	for (int i = 0; i < EMO_COUNT; i++)
	{
		int x = 6 + (i % PK_COLS) * PK_CELL, y = 26 + (i / PK_COLS) * PK_CELL;
		if (i == hot) { ui_rbox (x, y, PK_CELL, PK_CELL, 4, TC_HOT_TOP, TC_SEL_BOT); ui_frame (x, y, PK_CELL, PK_CELL, TC_SEL_RIM); }
		emo_draw (i, x + 4, y + 4, PK_CELL - 8);
	}
}
static long pk_mouse (long v, long kind, long x, long y, long)
{
	Pane *P = pn_of (v); if (!P) return 0;
	int c = ((int) x - 6) / PK_CELL, r = ((int) y - 26) / PK_CELL;
	int i = kind != 3 && y >= 26 && x >= 6 && c < PK_COLS ? r * PK_COLS + c : -1;
	if (i >= EMO_COUNT) i = -1;
	if (i != P->pickHot) { P->pickHot = i; ui_view_dirty (v); }
	if (kind == 1 && i >= 0)
	{
		char u[8]; int n = u8_put (u, emo_info[i].cp); u[n] = 0;
		ed_insert (P, u);
		pn_picker_show (P, false);
		ui_w_focus (P->edit);
	}
	return 0;
}
static void pn_picker_show (Pane *P, bool on)
{
	if (P->pickerOn == on) return;
	P->pickerOn = on; P->pickHot = -1;
	ui_w_show (P->picker, on);
	if (on) ui_view_dirty (P->picker);
	else { ui_view_dirty (P->chat); ui_view_dirty (P->strip); }
}

// ---- the strip -----------------------------------------------------------------------------------------------

static void attach_clear (Pane *P)
{
	delete [] P->att.jpg; free (P->att.thumb);
	memset (&P->att, 0, sizeof P->att);
}
static void picture_icon (int x, int y)
{
	ui_rbox (x, y, 20, 16, 3, 0x9FD0F5, 0x4A8FD0);
	ui_frame (x, y, 20, 16, 0x2E6EAE);
	vp_disc (x + 6.0f, y + 5.0f, 2, 0xFFE27A);
	float a[6] = { x + 2.0f, y + 14.0f, x + 9.0f, y + 6.0f, x + 15.0f, y + 14.0f }, b[6] = { x + 10.0f, y + 14.0f, x + 14.0f, y + 9.0f, x + 19.0f, y + 14.0f };
	vp_poly (a, 3, 0x2E7D32); vp_poly (b, 3, 0x4CAF50);
}
static void strip_paint (long v, long w, long h)
{
	Pane *P = pn_of (v); if (!P) return;
	begin_paint (v);
	ui_fill (0, 0, w, h, TC_INPUT_BG);
	ui_fill (0, 0, w, 1, TC_LINE);
	int x = 8;
	if (P->stripHot == 0) ui_rbox (x - 2, 3, 42, 24, 4, TC_HOT_TOP, TC_HOT_BOT);
	emo_draw (EMO_SMILE, x + 2, 6, 18);
	ftext ("\xE2\x96\xBE", x + 26, 8, TC_GREY, 11);
	int px = x + 46;
	if (P->stripHot == 1) ui_rbox (px - 2, 3, 30, 24, 4, TC_HOT_TOP, TC_HOT_BOT);
	picture_icon (px + 3, 7);
	int hx = px + 36;
	if (P->att.jpg)						// the picture attached: its view, its name, the cross
	{
		int cw = (int) w - hx - 8;
		ui_rbox (hx, 3, cw, 24, 12, TC_BG, TC_HOT_TOP);
		ui_frame (hx + 6, 3, cw - 12, 1, TC_SEL_RIM);
		if (P->att.thumb) blit_rect_round (P->att.thumb, hx + 4, 5, P->att.tw, P->att.th, 3);
		char lab[180];
		snprintf (lab, sizeof lab, TR ("Picture: %s (%d x %d) - sent with the next message"), P->att.name, P->att.w, P->att.h);
		ftext (lab, hx + 8 + P->att.tw, 9, TC_INK, PX_SMALL, 0, cw - P->att.tw - 34);
		P->closeX = hx + cw - 14;
		fbox ("\xE2\x9C\x95", P->closeX - 5, 3, 24, P->stripHot == 2 ? TC_BUSY : TC_GREY, 12);
	}
	else ftext (TR ("Enter: send - Shift+Enter: a new line"), hx, 9, TC_LIGHT, PX_SMALL, 1, (int) w - hx - 10);
}
static long strip_mouse (long v, long kind, long x, long y, long info)
{
	Pane *P = pn_of (v); if (!P) return 0;
	int hot = kind == 3 ? -1 : x >= 6 && x < 48 ? 0 : x >= 52 && x < 82 ? 1 : P->att.jpg && x >= P->closeX - 10 && x < P->closeX + 10 ? 2 : -1;
	(void) y;
	if (hot != P->stripHot) { P->stripHot = hot; ui_view_dirty (v); }
	if (kind != 1 || !((info >> 8) & 1)) return 0;
	if (hot == 0) pn_picker_show (P, !P->pickerOn);
	else if (P->pickerOn) pn_picker_show (P, false);
	if (hot == 1) s_pickFor = P;
	if (hot == 2) { attach_clear (P); ui_view_dirty (v); }
	return 0;
}

// ---- the pictures sent ------------------------------------------------------------------------------------

#define PIC_MAX		1280		// the longest side sent (Telegram's photos go to 2560; this keeps them light)
#define PIC_QUALITY	87

// Pixels (0xAARRGGBB) made the picture to send: brought down to PIC_MAX, JPEG; its view for the strip. False: no memory.
static bool attach_pixels (Pane *P, unsigned *px, int w, int h, const char *name)
{
	if (!px || w <= 0 || h <= 0) return false;
	int dw = w, dh = h;
	if (dw > PIC_MAX || dh > PIC_MAX)
	{
		if (dw >= dh) { dh = (int) ((long) h * PIC_MAX / w); dw = PIC_MAX; }
		else { dw = (int) ((long) w * PIC_MAX / h); dh = PIC_MAX; }
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
	}
	unsigned *small = dw != w || dh != h ? scale_box (px, w, h, dw, dh) : 0;
	const unsigned *src = small ? small : px;
	unsigned n = 0;
	unsigned char *jpg = media::jpeg_encode (src, dw, dh, PIC_QUALITY, &n);
	if (!jpg || !n) { free (small); return false; }
	attach_clear (P);
	P->att.jpg = jpg; P->att.n = (int) n; P->att.w = dw; P->att.h = dh;
	P->att.th = 20; P->att.tw = imax (8, imin (40, dw * 20 / dh));
	P->att.thumb = scale_box (src, dw, dh, P->att.tw, P->att.th);
	snprintf (P->att.name, sizeof P->att.name, "%s", name);
	free (small);
	ui_view_dirty (P->strip);
	ui_w_focus (P->edit);
	return true;
}
static bool picture_name (const char *p)
{
	const char *e = strrchr (p, '.');
	if (!e) return false;
	static const char *const X[] = { ".jpg", ".jpeg", ".png", ".gif", ".bmp" };
	for (int i = 0; i < 5; i++) if (!strcasecmp (e, X[i])) return true;
	return false;
}
static bool attach_file (Pane *P, const char *path)
{
	int w, h;
	unsigned *px = img_load (path, &w, &h);
	if (!px) { notify ("Telegram", TR ("This file is not a picture the app can read.")); return false; }
	const char *base = path;
	for (const char *q = path; *q; q++) if (*q == '/' || *q == ':') base = q + 1;
	bool ok = attach_pixels (P, px, w, h, base);
	free (px);
	return ok;
}
static void choose_picture (Pane *P)
{
	static char dir[260] = "SD0:/home";
	char path[300] = "";
	long was = ui_window_use (P->win);
	ui_file_filter ((long) TR ("Pictures|*.jpg;*.jpeg;*.png;*.gif;*.bmp|All files|*"));
	long ok = ui_file_open ((long) dir, (long) path, sizeof path);
	ui_file_filter (0);
	ui_window_use (was);
	ui_raise (P->win);				// (the keyboard back to where the message is written)
	if (!ok) return;
	snprintf (dir, sizeof dir, "%s", path);
	for (int i = (int) strlen (dir) - 1; i > 0; i--) if (dir[i] == '/') { dir[i] = 0; break; }
	attach_file (P, path);
}

static void demo_msg (long long peer, int id, long long from, bool out, int date, const char *text, int media = tg::M_NONE, const char *extra = "");
static void send_current (Pane *P)
{
	if (!P->open || (!P->len && !P->att.jpg)) return;
	// (the spaces and line breaks around the text dropped)
	int a = 0, b = P->len;
	while (a < b && (P->buf[a] == ' ' || P->buf[a] == '\n')) a++;
	while (b > a && (P->buf[b - 1] == ' ' || P->buf[b - 1] == '\n')) b--;
	if (P->att.jpg)					// a picture, the text its caption
	{
		char *cap = tg::sdup (P->buf + a, b - a);
		g_c.sendPhoto (P->open, P->att.jpg, P->att.n, P->att.w, P->att.h, cap);
		if (g_demo)
		{
			tg::Conv *c = g_c.conv (P->open);
			if (c && c->n) { c->m[c->n - 1].progress = 60; c->rev++; }
		}
		free (cap);
		attach_clear (P);
		ed_clear (P);
		ui_view_dirty (P->strip);
		return;
	}
	if (a == b) return;
	char *t = tg::sdup (P->buf + a, b - a);
	if (g_demo)
	{
		static int id = 1000;
		demo_msg (P->open, id++, tg::pkey (tg::P_USER, g_c.selfId), true, g_c.serverTime (), t);
		tg::Conv *c = g_c.conv (P->open); c->typingUntil = 0; c->rev++; g_c.rev++; g_c.dialogs ();
	}
	else g_c.send (P->open, t);
	free (t);
	P->stick = true;
	ed_clear (P);
}

// ---- the pane ----------------------------------------------------------------------------------------------------

// Its widgets, in the current window.
static void pn_create (Pane *P, long win, bool alone)
{
	P->win = win; P->alone = alone; P->stick = true; P->pickHot = -1; P->stripHot = -1; P->erev = 1;
	P->head = ui_view (0, 0, 10, 10, 0);
	ui_view_on (P->head, (long) head_paint, 0, 0, 0);
	P->back = ui_pushbutton (0, 0, 32, 0, (long) "\xE2\x86\x90", UB_GHOST);
	ui_w_set_parent (P->back, P->head);
	ui_w_tip (P->back, (long) TR ("Back to the list"));
	ui_w_show (P->back, 0);
	P->bar = ui_view (0, 0, 10, 10, 0);
	ui_view_on (P->bar, (long) bar_paint, 0, 0, 0);
	P->barAdd = ui_pushbutton (0, 0, 100, 0, (long) TR ("Add to contacts"), UB_GHOST); ui_w_set_parent (P->barAdd, P->bar);
	P->barBlock = ui_pushbutton (0, 0, 60, 0, (long) TR ("Block"), UB_GHOST); ui_w_set_parent (P->barBlock, P->bar);
	P->barHide = ui_pushbutton (0, 0, 26, 0, (long) "\xE2\x9C\x95", UB_GHOST); ui_w_set_parent (P->barHide, P->bar);
	ui_w_show (P->bar, 0);
	P->chat = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (P->chat, (long) cv_paint, (long) cv_mouse, 0, (long) cv_wheel);
	P->chatBar = ui_scrollbar (0, 0, 10, 10, 0);
	ui_w_set_parent (P->chatBar, P->chat);
	ui_w_show (P->chatBar, 0);
	P->strip = ui_view (0, 0, 10, 10, 0);
	ui_view_on (P->strip, (long) strip_paint, (long) strip_mouse, 0, 0);
	P->edit = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (P->edit, (long) ed_paint, (long) ed_mouse, (long) ed_key, (long) ed_wheel);
	ui_w_cursor (P->edit, UC_TEXT);
	P->send = ui_pushbutton (0, 0, 82, 0, (long) TR ("Send"), UB_DEFAULT);
	P->dp = ui_view (0, 0, 10, 10, 0);
	ui_view_on (P->dp, (long) dp_paint, 0, 0, 0);
	P->picker = ui_view (0, 0, PK_W, PK_H, 0);
	ui_view_on (P->picker, (long) pk_paint, (long) pk_mouse, 0, 0);
	ui_w_show (P->picker, 0);
}
// Placed at x in a window w x h: the header, the bar, the messages, the line to write in, the pictures' column. The
// window's room decides: a low one has a thin header and a thin line.
static void pn_place (Pane *P, int x, int w, int h, bool conv)
{
	P->x = x; P->w = w; P->h = h; P->conv_on = conv;
	P->headH = h < 420 ? 52 : 76; P->inputH = h < 520 ? 88 : 112;
	bool dp = conv && g_pictures && w >= 640;
	P->dp_on = dp;
	int cw = dp ? w - DP_W : w;
	bool bar = conv && g_c.showBar (g_c.conv (P->open));
	P->bar_on = bar;
	int bh = bar ? BAR_H : 0;
	P->cw = imax (cw, 40); P->chH = imax (20, h - P->headH - P->inputH - bh);
	ui_w_show (P->head, conv); ui_w_show (P->chat, conv); ui_w_show (P->strip, conv); ui_w_show (P->edit, conv); ui_w_show (P->send, conv);
	ui_w_show (P->dp, dp); ui_w_show (P->bar, bar);
	ui_w_show (P->back, conv && P->narrow && !P->alone);
	if (!conv) { ui_w_show (P->chatBar, 0); if (P->pickerOn) pn_picker_show (P, false); return; }
	int c = ctl_h ();
	ui_w_move (P->head, x, 0, w, P->headH);
	ui_w_move (P->back, x + 12, (P->headH - c) / 2, 32, c);
	ui_w_move (P->bar, x, P->headH, cw, BAR_H);
	if (bar) bar_layout (P);
	ui_w_move (P->chat, x, P->headH + bh, P->cw, P->chH);
	ui_w_move (P->chatBar, x + P->cw - 12, P->headH + bh, 12, P->chH);
	int iy = h - P->inputH;
	ui_w_move (P->strip, x, iy, cw, STRIP);
	int bw = 82;
	P->ew = cw - bw - 28; P->eh = P->inputH - STRIP - 10;
	ui_w_move (P->edit, x + 10, iy + STRIP + 2, P->ew, P->eh);
	ui_w_move (P->send, x + cw - bw - 10, iy + STRIP + 2, bw, c);
	ui_w_move (P->dp, x + cw, P->headH, DP_W, h - P->headH);
	ui_w_move (P->picker, x + 6, imax (0, iy - PK_H - 2), PK_W, PK_H);
	ed_ensure (P);
	P->dirty = true;
	ui_view_dirty (P->head); ui_view_dirty (P->bar); ui_view_dirty (P->strip); ui_view_dirty (P->edit); ui_view_dirty (P->dp);
}
static void pn_free (Pane *P)
{
	cv_free (P);
	free (P->it); P->it = 0; P->cap = 0;
	attach_clear (P);
}
// Each turn of the loop, in the pane's window: what changed.
static void pn_tick (Pane *P, unsigned rev)
{
	if (!P->conv_on) return;
	tg::Conv *c = P->open ? g_c.conv (P->open) : 0;
	if (P->dirty || P->open != P->peer || (c ? c->rev : 0) != P->rev || P->cw != P->lw) { P->dirty = false; cv_sync (P); }
	if (P->seen != rev)
	{
		P->seen = rev;
		ui_view_dirty (P->head); ui_view_dirty (P->dp);
		if (P->bar_on) { bar_layout (P); ui_view_dirty (P->bar); }	// (its buttons first: a button changed repaints the bar)
		bool bar = c && g_c.showBar (c);
		if (bar != P->bar_on) pn_place (P, P->x, P->w, P->h, true);
	}
	long long t = tg_unix_ms ();
	if (t - P->blinkAt >= 500 && ui_w_get_focus () == P->edit)
	{
		P->blinkAt = t; P->blinkOff = !P->blinkOff;
		ui_view_dirty (P->edit);
	}
}

#endif
