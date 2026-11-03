//
// app.h -- what Mail's window parts share (Onyx's Apps/mail/app.h, on UIKit): the model, the widgets' handles, the sizes,
// the helpers the parts call one another through (main.cpp defines them). A part is a prefix and functions: rd_ the
// reading pane, c_ the message being written, wz_ the account's assistant, st_ the settings, k_ the contacts.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef _mail_app_h
#define _mail_app_h

#include <dirent.h>
#include <spawn.h>
#include <initializer_list>
#include "ui.h"
#include "model.h"
#include "html_ui.h"

extern "C" unsigned *image_decode (const unsigned char *b, int n, int *w, int *h);	// image.c
extern char **environ;

namespace mailapp {

static const int TB_H = 46;

static Model g_m;
// What the window shows at the right of the folders. P_MAIL: the list and the reading pane; the others cover both.
enum { P_MAIL, P_COMPOSE, P_CONTACTS, P_WIZARD, P_SETTINGS, P_WELCOME };
static int  g_page = P_MAIL;
static int  g_w, g_h;
static bool g_onePane, g_sideOn = true;		// a narrow window: one pane shown (the conversation's when one is open); the folders shown
static bool g_sideWant;				// (narrow: the folders asked for by the bar's button)
static int  g_conv = -1;			// the conversation shown (view.convs index), -1 none
static int  g_cur = -1;				// the list's row the keys are on
static bool g_emlMode;				// a .eml file shown alone: read only

// what is to be drawn again (the loop does it: no widget is touched from inside a view's paint)
static bool g_sideDirty, g_listDirty, g_readDirty, g_toolDirty, g_footDirty;

// a click on something drawn by hand, for the loop: it may open a menu or a dialogue, out of ui_w_event
enum { L_NONE, L_READ, L_LIST, L_FOOT, L_CHIP, L_CARD, L_SUG, L_WELCOME };
static struct { int who; Hit h; int x, y; bool right; } g_later;
static void later (int who, const Hit &h, bool right = false)
{
	g_later.who = who; g_later.h = h; g_later.right = right;
	g_later.x = ui_ev_field (&ui, EV_X); g_later.y = ui_ev_field (&ui, EV_Y);
}

// main.cpp
static void refresh_all ();
static void layout ();
static void show_page (int p);
static void select_view (int kind, int acct = 0, int folder = 0);
static void open_conv (int k);
static void compose_new (int mode, const Ref *about = 0);	// mode: 0 new, 1 reply, 2 reply all, 3 forward
static void compose_to (const char *name, const char *email);
static void act_archive (); static void act_delete (); static void act_junk (); static void act_star (); static void act_unread ();
static void act_check ();
static void show_contacts (bool on);
static void status_note (const char *s);
static void quick_send ();

static inline const char *tx (long w) { return (const char *) ui_w_get_text (w); }
static inline long ask (const char *q, int kind = UM_OK) { return ui_message ((long) TR ("Mail"), (long) q, kind); }
static inline void set_text (long w, const char *s) { ui_w_set_text (w, (long) s); }

// a little menu at a point of the window: labels, 0 ends them -> the one chosen, -1. "-": a line (it counts); a first
// byte 1: greyed.
static long popup (int x, int y, std::initializer_list<const char *> items, int width = 200)
{
	char t[1200]; int n = 0;
	for (const char *s : items)
	{
		int l = (int) strlen (s);
		if (n + l + 2 >= (int) sizeof t) break;
		memcpy (t + n, s, (size_t) l + 1); n += l + 1;
	}
	t[n] = 0;
	return ui_popup_local (x, y, (long) t, width);
}
static long popup_list (int x, int y, const char *t, int width = 220) { return ui_popup_local (x, y, (long) t, width); }

// another program started (Jet, Cardfile): argv[1] given
static bool spawn_app (const char *path, const char *arg)
{
	char *argv[] = { (char *) path, (char *) arg, 0 };
	pid_t p;
	return posix_spawn (&p, path, 0, 0, argv, environ) == 0;
}
#define WEB_PROGRAM	"SD0:/apps/jet.app/main"

// the conversation's messages shown (refs into the model)
static int conv_refs (Ref *out, int max)
{
	if (g_conv < 0 || g_conv >= g_m.view.convs.n) return 0;
	const Conv &c = g_m.view.convs[g_conv];
	int n = c.n < max ? c.n : max;
	for (int i = 0; i < n; i++) out[i] = g_m.view.refs[c.first + i];
	return n;
}

// "Marie Dubois" of '"Marie Dubois" <m@x>' (the address's local part when no name)
static void who (const char *addrs, char *out, int cap, bool all = false)
{
	Addr a[16]; int n = parse_addrs (addrs ? addrs : "", a, 16);
	out[0] = 0;
	Buf o;
	for (int i = 0; i < n && (all || i == 0); i++) { char s[160]; addr_show (a[i], s, sizeof s); if (o.n) o.add (", "); o.add (s); }
	scpy (out, o.c (), cap);
}
static void first_email (const char *addrs, char *out, int cap) { Addr a[4]; int n = parse_addrs (addrs ? addrs : "", a, 4); scpy (out, n ? a[0].email : "", cap); }

// a date as the list shows it: 11:42 today, "Yesterday", Mon, 12 Sep, 12/09/2025
static long long local_of (long long utc) { return utc + tz_minutes () * 60LL; }
static long long day_num (long long utc) { long long l = local_of (utc); return l >= 0 ? l / 86400 : (l - 86399) / 86400; }
static void fmt_short (long long t, char *b, int cap)
{
	long long now = now_utc ();
	long long dn = day_num (now), dt = day_num (t);
	long long l = local_of (t); int sec = (int) (l - dt * 86400);
	const char *WD[7] = { TR ("Thu"), TR ("Fri"), TR ("Sat"), TR ("Sun"), TR ("Mon"), TR ("Tue"), TR ("Wed") };
	int y, m, d; civil (dt, &y, &m, &d);
	int ny, nm, nd; civil (dn, &ny, &nm, &nd);
	if (dt == dn) snprintf (b, cap, "%02d:%02d", sec / 3600, sec / 60 % 60);
	else if (dt == dn - 1) snprintf (b, cap, "%s", TR ("Yesterday"));
	else if (dn - dt < 7 && dn > dt) snprintf (b, cap, "%s", WD[((dt % 7) + 7) % 7]);
	else if (y == ny) snprintf (b, cap, "%d %s", d, TR (MONTHS3[m - 1]));
	else snprintf (b, cap, "%02d/%02d/%04d", d, m, y);
}
static void fmt_long (long long t, char *b, int cap)
{
	long long dt = day_num (t), dn = day_num (now_utc ());
	long long l = local_of (t); int sec = (int) (l - dt * 86400);
	int y, m, d; civil (dt, &y, &m, &d);
	const char *WD[7] = { TR ("Thursday"), TR ("Friday"), TR ("Saturday"), TR ("Sunday"), TR ("Monday"), TR ("Tuesday"), TR ("Wednesday") };
	if (dt == dn) snprintf (b, cap, TR ("Today, %02d:%02d"), sec / 3600, sec / 60 % 60);
	else if (dt == dn - 1) snprintf (b, cap, TR ("Yesterday, %02d:%02d"), sec / 3600, sec / 60 % 60);
	else snprintf (b, cap, "%s %d %s %d, %02d:%02d", WD[((dt % 7) + 7) % 7], d, TR (MONTHS3[m - 1]), y, sec / 3600, sec / 60 % 60);
}
// the list's day groups: 0 today, 1 yesterday, 2 this week, 3 this month, 4 older
static int day_group (long long t)
{
	long long dn = day_num (now_utc ()), dt = day_num (t);
	if (dt >= dn) return 0;
	if (dt == dn - 1) return 1;
	if (dn - dt < 7) return 2;
	int y, m, d, ny, nm, nd; civil (dt, &y, &m, &d); civil (dn, &ny, &nm, &nd);
	if (y == ny && m == nm) return 3;
	return 4;
}
static void fmt_size (long n, char *b, int cap)
{
	if (n < 1024) snprintf (b, cap, TR ("%ld bytes"), n);
	else if (n < 1024 * 1024) snprintf (b, cap, TR ("%ld KB"), (n + 512) / 1024);
	else snprintf (b, cap, TR ("%ld.%ld MB"), n / 1048576, n % 1048576 * 10 / 1048576);
}
static const char *folder_label (const Folder &f) { return f.show[0] ? f.show : f.name; }
// a folder's name as shown: in the system's language ("Archive" is also the tool's verb: "Archives" here)
static const char *folder_tr (const char *name) { return !strcmp (name, "Archive") ? TR ("Archives") : TR (name); }
static int folder_icon (const Folder &f)
{
	switch (f.special) { case SP_INBOX: return I_INBOX; case SP_SENT: return I_SENT; case SP_DRAFTS: return I_DRAFTS; case SP_TRASH: return I_TRASH;
	case SP_JUNK: return I_JUNK; case SP_ARCHIVE: case SP_ALL: return I_ARCHIVE; case SP_FLAGGED: return I_STAR; default: return I_FOLDER; }
}

} // namespace mailapp

#endif
