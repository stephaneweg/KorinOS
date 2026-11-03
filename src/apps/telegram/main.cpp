//
// telegram -- Onyx's instant messenger on ARMPI's UIKit, in the way of Windows Live Messenger, on Telegram's open API
// (MTProto 2.0, written here: tl.h, tgcrypto.h, mtproto.h, client.h -- no TDLib). No bridge (README.md): ui_* handles, a view is
// a prefix and functions.
//
// The window: on the left the contact list (me and my status on the sky, the search, the Favourites, the Conversations, the
// Contacts and who is online), the conversation beside it or in a window of its own (pane.h). Before the account is there,
// the sign-in's steps (signin.h). A new message in another conversation: a notification.
//
//   telegram            the account in SD0:/home/telegram/session.dat (else the sign-in)
//   telegram --demo     made-up conversations, no network (demo.h)
//
// SD0:/home/telegram/config.ini ([telegram]): api_id, api_hash (another key than the app's own, built in), test (1: Telegram's
// test servers), pictures (0: the display pictures' column hidden), windows (0: the conversations beside the list).
// The tests' server, tools/nettest/tgsrv.py: test_host, test_port, test_rsa_n (the modulus in hex) take the place of every
// address and key built in: with test_host set no connection goes anywhere else (tgplat.h). log.txt: what the connection did.
//
// MIT licence.
//
#include <initializer_list>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uikit.h>
#include "tgplat.h"
#include "client.h"
#include "look.h"
#include "emoticons.h"
#include "rich.h"
#include "buddylist.h"
#include "pane.h"
#include "signin.h"
#include "demo.h"

#define W0		940
#define H0		640
#define LIST_W		300
#define SB_H		22
#define LIST_WIN_W	360		// the main window when the conversations have their own windows
#define NARROW_W	640		// under it, beside the list: one at a time (Onyx's pocket mode gone)
#define CW_W		640		// a conversation's window
#define CW_H		560

static long g_welcome, g_status;
static int g_w, g_h;
static void relayout ();
static void build_menu ();

// ---- a notification on the desktop: notifyd's service, as src/inc/notify.inc --------------------------------------------
static long sys_call (long n, long a, long b)
{
	register long x0 __asm__ ("x0") = a; register long x1 __asm__ ("x1") = b; register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory");
	return x0;
}
static void notify (const char *title, const char *msg)
{
	static long ch = -1;
	if (ch < 0) ch = sys_call (0x31 /* SYS_SVC_OPEN */, (long) "notify", 0);
	if (ch < 0) return;					// (no notifyd: the title and the badges say it)
	char m[512]; memset (m, 0, sizeof m);
	*(long *) m = 1;					// op 1: title, 0, text, 0
	int n = snprintf (m + 48, 460, "%s", title);
	if (n > 0 && n < 440) snprintf (m + 48 + n + 1, (size_t) (460 - n - 1), "%s", msg);
	sys_call (0x32 /* SYS_SVC_CALL */, ch, (long) m);
}

// ---- the settings ----------------------------------------------------------------------------------------

// The app's own key at my.telegram.org (the project's, public: Onyx's). config.ini's api_id / api_hash, when they are
// there, take its place.
#define TG_API_ID	35701384
#define TG_API_HASH	"e33e9fae5727447538e9aea5c0f5a0fc"

static char g_cfgExtra[800];				// (the tests' lines, kept as they are when the settings are written)

static void save_config ()
{
	char t[1400];
	int n;
	if (g_c.apiId == TG_API_ID && !strcmp (g_c.apiHash, TG_API_HASH))	// (the app's own key: not written)
		n = snprintf (t, sizeof t, "[telegram]\ntest=%d\npictures=%d\nwindows=%d\n%s", (int) g_c.test, (int) g_pictures, (int) g_windowed, g_cfgExtra);
	else n = snprintf (t, sizeof t, "[telegram]\napi_id=%d\napi_hash=%s\ntest=%d\npictures=%d\nwindows=%d\n%s", g_c.apiId, g_c.apiHash, (int) g_c.test, (int) g_pictures, (int) g_windowed, g_cfgExtra);
	tg_save (TG_DIR "config.ini", t, n);
}

// One ini value (key = value; [sections] do not matter: the file has one).
static bool ini_get (char *txt, const char *key, char *out, int cap)
{
	int kn = (int) strlen (key);
	for (char *l = txt; *l; )
	{
		char *e = l; while (*e && *e != '\n') e++;
		char *p = l; while (p < e && (*p == ' ' || *p == '\t')) p++;
		if (e - p > kn && !strncmp (p, key, (size_t) kn))
		{
			char *q = p + kn; while (q < e && (*q == ' ' || *q == '\t')) q++;
			if (q < e && *q == '=')
			{
				q++; while (q < e && (*q == ' ' || *q == '\t')) q++;
				int n = 0;
				while (q + n < e && q[n] != '\r' && n < cap - 1) { out[n] = q[n]; n++; }
				while (n > 0 && out[n - 1] == ' ') n--;
				out[n] = 0;
				return true;
			}
		}
		l = *e ? e + 1 : e;
	}
	return false;
}
static void load_config ()
{
	g_c.apiId = TG_API_ID;
	snprintf (g_c.apiHash, sizeof g_c.apiHash, "%s", TG_API_HASH);
	int n = 0;
	char *t = (char *) tg_load (TG_DIR "config.ini", &n);
	if (!t) return;
	char v[600];
	if (ini_get (t, "api_id", v, sizeof v)) g_c.apiId = atoi (v);
	if (ini_get (t, "api_hash", v, sizeof v)) snprintf (g_c.apiHash, sizeof g_c.apiHash, "%s", v);
	if (!g_c.apiId || !g_c.apiHash[0]) { g_c.apiId = TG_API_ID; snprintf (g_c.apiHash, sizeof g_c.apiHash, "%s", TG_API_HASH); }
	if (ini_get (t, "test", v, sizeof v)) g_c.test = atoi (v) != 0;
	if (ini_get (t, "pictures", v, sizeof v)) g_pictures = atoi (v) != 0;
	if (ini_get (t, "windows", v, sizeof v)) g_windowed = atoi (v) != 0;
	// the tests' server: nothing built in is used then
	char host[48] = "", port[12] = "", rsa[516] = "";
	ini_get (t, "test_host", host, sizeof host);
	ini_get (t, "test_port", port, sizeof port);
	ini_get (t, "test_rsa_n", rsa, sizeof rsa);
	if (host[0])
	{
		snprintf (g_c.onlyHost, sizeof g_c.onlyHost, "%s", host);
		g_c.onlyPort = atoi (port) > 0 ? atoi (port) : 443;
		snprintf (g_tgOnlyHost, sizeof g_tgOnlyHost, "%s", host);
		if (strlen (rsa) == 512) strcpy (tgc::rsa_test_hex (), rsa);
		snprintf (g_cfgExtra, sizeof g_cfgExtra, "test_host=%s\ntest_port=%d\ntest_rsa_n=%s\n", host, g_c.onlyPort, rsa);
	}
	free (t);
}
static void save_api (int id, const char *hash)
{
	g_c.setApi (id, hash, g_c.test);
	save_config ();
}

// ---- the welcome (no conversation open) and the status bar ------------------------------------------------------

static void welcome_paint (long v, long w, long h)
{
	begin_paint (v);
	ui_rbox (0, 0, w, h, 0, TC_BG, TC_SKY_BOT);
	glow ((int) w / 2, (int) h + (int) h / 4, (int) w, (int) h / 2, TC_SKY_TOP, 120);
	int s = 120, cx = (int) w / 2, y = imax (12, (int) h / 2 - 120);
	if (h < 360) s = 72;
	avatar_default (cx - s / 2, y, s, TC_ONLINE);
	y += s + 26;
	const char *t = TR ("Pick a conversation");
	ftext (t, cx - ftw (t, PX_TITLE) / 2, y, TC_TITLE, PX_TITLE);
	y += 30;
	const char *u = TR ("Choose someone in your list to write to them. Their new messages come in at once.");
	static Rich r; rich_layout (r, ui_px (), u, imin (360, (int) w - 40), 16, false);
	rich_draw (r, u, cx - r.widest / 2, y, TC_GREY);
}
static void status_paint (long v, long w, long h)
{
	begin_paint (v);
	ui_rbox (0, 0, w, h, 0, TC_HOT_TOP, TC_HOT_BOT);
	ui_fill (0, 0, w, 1, TC_LINE);
	bool on = g_c.online;
	vp_disc (12, h / 2.0f, 4, on ? TC_ONLINE : TC_AWAY);
	fbox (g_demo ? TR ("Demonstration: nothing is sent") : on ? TR ("Connected to Telegram") : TR ("Connecting to Telegram..."), 22, 0, (int) h, TC_GREY, PX_SMALL, 0, (int) w - 30);
	const char *r = g_c.onlyHost[0] ? TR ("Test server") : "Telegram for Korin OS";
	if (w >= 520) fbox (r, (int) w - ftw (r, PX_SMALL, 1) - 10, 0, (int) h, TC_LIGHT, PX_SMALL, 1);
}

// ---- a conversation in its own window ------------------------------------------------------------------------
// The main window keeps the list; a conversation opened there comes in a window of its own -- one for each, brought
// forward when it is opened again. Its close box closes it (Telegram goes on); closing the main window ends Telegram.

static Pane *pn_window_of (long long peer)
{
	for (int i = 1; i < MAXPANE; i++) if (g_panes[i].win && g_panes[i].open == peer) return &g_panes[i];
	return 0;
}
static void pn_win_resize (long w, long h)
{
	Pane *P = pn_of_win (ui.win); if (!P) return;
	pn_place (P, 0, (int) w, (int) h, true);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	cv_sync (P);
	ui_w_draw (0);
	ui_update_all ();
}
static void pn_open (Pane *P, long long peer)
{
	P->open = peer;
	if (g_demo) { tg::Conv *c = g_c.conv (peer); if (c) { c->unread = 0; c->rev++; } g_c.rev++; }
	else g_c.open (peer);
	P->peer = 0;
	P->dirty = true;
	g_listDirty = true;
}
// A conversation in its window: the one it has, brought forward, else a new one -> false: no window could be made.
static bool open_window (long long peer)
{
	Pane *P = pn_window_of (peer);
	if (P) { ui_raise (P->win); return true; }
	for (int i = 1; i < MAXPANE && !P; i++) if (!g_panes[i].win) P = &g_panes[i];
	if (!P) { notify ("Telegram", TR ("Too many conversations are open: close one of their windows.")); return true; }
	char name[160];
	g_c.peerName (peer, name, sizeof name);
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int slot = (int) (P - g_panes);
	int ww = imin (CW_W, (int) scr[0] - 8), wh = imin (CW_H, (int) (scr[1] - scr[3] - scr[2]) - 32 - 96);
	long win = ui_window_ex (imin (LIST_WIN_W + 8 + slot * 24, (int) scr[0] - ww - 8), scr[3] + 8 + slot * 24, ww, wh, (long) name, WF_RESIZABLE);
	if (win <= 0) { ui_window_use (g_mainWin); return false; }
	ui_min_size (420, 360);
	ui_on_resize ((long) pn_win_resize, 0);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	pn_create (P, win, true);
	pn_open (P, peer);
	pn_place (P, 0, (int) ui.w, (int) ui.h, true);
	cv_sync (P);
	ui_w_draw (0);
	ui_w_focus (P->edit);
	ui_view_flush ();
	ui_update_all ();
	ui_window_use (g_mainWin);
	return true;
}
static void pn_close (Pane *P)
{
	long w = P->win;
	pn_free (P);
	P->win = 0; P->open = 0; P->peer = 0; P->rev = 0; P->conv_on = false; P->pickerOn = false; P->len = P->caret = 0; P->buf[0] = 0;
	ui_window_close (w);
	ui_window_use (g_mainWin);
	g_listDirty = true;
}
static void close_windows () { for (int i = 1; i < MAXPANE; i++) if (g_panes[i].win) pn_close (&g_panes[i]); }

// Is this conversation on the screen? (its window, or beside the list)
static bool peer_shown (long long peer)
{
	if (!g_windowed && peer == g_main->open) return true;
	return pn_window_of (peer) != 0;
}

// ---- a picture in its own window -------------------------------------------------------------------------------
// (Onyx started its Image Viewer; ARMPI's reads no JPEG.) The picture fit to the window, Escape or the close box ends it.

enum { MAXPICWIN = 4 };
struct PicWin { long win, view; char path[160]; int iw, ih; };
static PicWin g_picw[MAXPICWIN];
static PicWin *pw_of_win (long w) { for (int i = 0; i < MAXPICWIN; i++) if (g_picw[i].win == w) return &g_picw[i]; return 0; }
static void pw_paint (long v, long w, long h)
{
	begin_paint (v);
	ui_fill (0, 0, w, h, jet (J_CREUX));
	PicWin *p = 0;
	for (int i = 0; i < MAXPICWIN; i++) if (g_picw[i].view == v) p = &g_picw[i];
	if (!p) return;
	int dw = (int) w, dh = (int) ((long) p->ih * w / p->iw);
	if (dh > h) { dh = (int) h; dw = (int) ((long) p->iw * h / p->ih); }
	unsigned *px = img_get (p->path, imax (dw, 1), imax (dh, 1), false);
	if (px) blit_rect_round (px, ((int) w - dw) / 2, ((int) h - dh) / 2, dw, dh, 0);
	else fbox (TR ("Loading the picture..."), 12, 0, (int) h, jet (J_TEXTE2), ui_px (), 1);
}
static void pw_resize (long w, long h)
{
	PicWin *p = pw_of_win (ui.win); if (!p) return;
	ui_w_move (p->view, 0, 0, w, h);
	ui_view_dirty (p->view);
	ui_w_draw (0);
	ui_update_all ();
}
static void pw_close (PicWin *p)
{
	long w = p->win;
	p->win = p->view = 0;
	ui_window_close (w);
	ui_window_use (g_mainWin);
}
static void open_picture (const char *path, int pw, int ph)
{
	PicWin *p = 0;
	for (int i = 0; i < MAXPICWIN && !p; i++) if (!g_picw[i].win) p = &g_picw[i];
	if (!p) return;
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int mw = (int) scr[0] - 40, mh = (int) (scr[1] - scr[3] - scr[2]) - 32 - 96;
	if (pw <= 0 || ph <= 0) { pw = 4; ph = 3; }
	int w = imin (pw, mw), h = (int) ((long) ph * w / pw);
	if (h > mh) { h = mh; w = (int) ((long) pw * h / ph); }
	long win = ui_window_ex (24, scr[3] + 24, imax (w, 200), imax (h, 150), (long) "Telegram", WF_RESIZABLE);
	if (win <= 0) { ui_window_use (g_mainWin); return; }
	ui_on_resize ((long) pw_resize, 0);
	p->win = win; p->iw = pw; p->ih = ph;
	snprintf (p->path, sizeof p->path, "%s", path);
	p->view = ui_view (0, 0, ui.w, ui.h, 0);
	ui_view_on (p->view, (long) pw_paint, 0, 0, 0);
	ui_view_flush ();
	ui_update_all ();
	ui_window_use (g_mainWin);
}
static void pw_tick (bool pics)
{
	for (int i = 0; i < MAXPICWIN; i++)
		if (g_picw[i].win)
		{
			ui_window_use (g_picw[i].win);
			if (pics) ui_view_dirty (g_picw[i].view);
			ui_view_flush ();
		}
	ui_window_use (g_mainWin);
}

// ---- the layout -----------------------------------------------------------------------------------------

// The main window as the mode wants it: the list alone (its conversations in their own windows), or the list and the
// conversation beside it. Under NARROW_W, beside the list: one at a time, the conversation with its back button.
static void relayout ()
{
	int w = g_w, h = g_h - SB_H;
	bool ready = g_c.state == tg::AS_READY;
	bool list_only = ready && g_windowed;
	bool narrow = !list_only && w < NARROW_W;
	bool conv = ready && !list_only && g_main->open != 0;
	bool list = ready && (!narrow || !conv);
	si_show (!ready);
	if (!ready) { si_place (w, g_h); ui_w_move (g_si, 0, 0, w, g_h); }
	ls_show (list);
	if (!list) ui_w_show (g_listBar, 0);
	int lw = list_only || narrow ? w : LIST_W;
	if (list) ls_move_to (0, 0, lw, h);
	g_main->narrow = narrow;
	int px = list && !narrow ? LIST_W : 0;
	pn_place (g_main, px, w - px, h, conv);
	bool wel = ready && !conv && !list_only && !narrow;
	ui_w_show (g_welcome, wel);
	if (wel) { ui_w_move (g_welcome, LIST_W, 0, w - LIST_W, h); ui_view_dirty (g_welcome); }
	ui_w_show (g_status, ready);
	if (ready) { ui_w_move (g_status, 0, h, w, SB_H); ui_view_dirty (g_status); }
	g_listDirty = true;
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_w_draw (0);
	ui_update_all ();
}
static void on_resize (long w, long h) { g_w = (int) w; g_h = (int) h; relayout (); }

static void apply_mode ()
{
	bool list_only = g_windowed && g_c.state == tg::AS_READY;
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int availW = (int) scr[0] - 8, availH = (int) (scr[1] - scr[3] - scr[2]) - 32 - 96;
	if (list_only && g_w > LIST_WIN_W + 40) { ui_resize (LIST_WIN_W, g_h); g_w = LIST_WIN_W; g_h = (int) ui.h; }
	else if (!list_only && g_w < NARROW_W)
	{
		int nw = imin (W0, availW), nh = imin (imax (g_h, H0), availH);
		ui_resize (nw, nh); g_w = (int) ui.w; g_h = (int) ui.h;
	}
	relayout ();
}

// ---- the actions ----------------------------------------------------------------------------------------

static void open_conversation (long long peer, bool ownWindow)
{
	if (!peer) return;
	if ((g_windowed || ownWindow) && g_c.state == tg::AS_READY && open_window (peer)) return;
	if (g_windowed && g_c.state == tg::AS_READY) { g_windowed = false; apply_mode (); build_menu (); }	// (no window could be made)
	s_selected = peer;
	pn_open (g_main, peer);
	cv_sync (g_main);
	relayout ();
	ls_scroll_to (peer);
	ui_w_focus (g_main->edit);
}
static void back_to_list ()
{
	g_main->open = 0; s_selected = 0;
	relayout ();
}

// ---- the status's little menu (a click on "(Online)") ---------------------------------------------------------

static void sign_out ()
{
	if (g_demo) return;
	if (ui_message ((long) TR ("Sign out"), (long) TR ("Sign out of Telegram on this computer? Your conversations stay on Telegram."), UM_YESNO) != 1) return;
	g_c.logOut ();
	close_windows ();
	g_main->open = 0; s_selected = 0;
	relayout ();
}
static void status_menu ()
{
	const char *a = TR ("Online"), *b = TR ("Appear offline"), *c = TR ("Sign out");
	char t[200]; int n = 0;
	for (const char *s : { a, b, "-", c }) { strcpy (t + n, s); n += (int) strlen (s) + 1; }
	t[n] = 0;
	switch (ui_popup_local (76, 52, (long) t, 200))
	{
	case 0: g_c.setOnline (true); break;
	case 1: g_c.setOnline (false); break;
	case 3: sign_out (); break;
	}
	s_meHot = false; ui_cursor (UC_ARROW);
	ui_view_dirty (g_me);
}
static void row_menu (long long peer)
{
	const char *a = TR ("Open"), *b = TR ("Open in its own window");
	char t[200]; int n = 0;
	strcpy (t, a); n = (int) strlen (a) + 1; strcpy (t + n, b); n += (int) strlen (b) + 1; t[n] = 0;
	switch (ui_popup_local (s_rowX, s_rowY, (long) t, 220))
	{
	case 0: open_conversation (peer); break;
	case 1: open_conversation (peer, true); break;
	}
}

// ---- adding a contact by phone number ----------------------------------------------------------------------

static long s_addF[3];
static long add_dialog_fn (long d, long w, long code)		// (Enter in a field: Add)
{
	if (code == UN_ACTIVATE && (w == s_addF[0] || w == s_addF[1] || w == s_addF[2])) ui_dialog_close (d, 1);
	return 0;
}
static void add_contact_dialog ()
{
	if (g_c.state != tg::AS_READY) return;
	enum { W_ = 400, H_ = 214 };
	int c = ctl_h (), pad = 16;
	bool err = false;
	char phone[40] = "", first[80] = "", last[80] = "";
	for (;;)
	{
		long d = ui_dialog_begin (W_, H_, (long) TR ("Add a Contact"));
		long note = ui_label (pad, 10, W_ - 2 * pad, 20, (long) (err ? TR ("The phone number, with its country code: +33 6 12 34 56 78") : TR ("Their phone number, with its country code.")), ULB_DIM);
		(void) note;
		static const char *const L[3] = { TRN ("Phone number"), TRN ("First name"), TRN ("Last name") };
		long *f = s_addF;
		for (int i = 0; i < 3; i++)
		{
			ui_label (pad, 38 + i * 38, 120, c, (long) TR (L[i]), 0);
			f[i] = ui_textbox (pad + 124, 38 + i * 38, W_ - 2 * pad - 124, c, 0);
			ui_w_set_range (f[i], 0, i ? 60 : 30, 0, 0);
		}
		ui_w_set_text (f[0], (long) phone); ui_w_set_text (f[1], (long) first); ui_w_set_text (f[2], (long) last);
		long x = ui_dialog_button (d, 0, (long) TR ("Add"), 1, UB_DEFAULT, W_ - 12);
		ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
		ui_dialog_cancel (d, 0);
		ui_w_focus (f[0]);
		long r = ui_dialog_run (d, (long) add_dialog_fn, 0);
		snprintf (phone, sizeof phone, "%s", (const char *) ui_w_get_text (f[0]));
		snprintf (first, sizeof first, "%s", (const char *) ui_w_get_text (f[1]));
		snprintf (last, sizeof last, "%s", (const char *) ui_w_get_text (f[2]));
		ui_dialog_end (d);
		if (r != 1) return;
		int digits = 0;
		for (const char *p = phone; *p; p++) if (*p >= '0' && *p <= '9') digits++;
		if (digits >= 6) break;
		err = true;
	}
	if (g_demo)				// (the demo: the person made up)
	{
		static long long id = 2000;
		demo_user (id, first[0] ? first : phone, last, "", tg::ST_RECENTLY, g_c.serverTime (), true);
		long long peer = tg::pkey (tg::P_USER, id++);
		tg::Conv *c2 = g_c.convs.add (peer); c2->peer = peer; c2->inList = true; c2->loaded = c2->complete = true; c2->topDate = g_c.serverTime ();
		g_c.added = peer; g_c.rev++; g_c.dialogs ();
	}
	else g_c.addContact (phone, first, last);
}

// ---- the menus ------------------------------------------------------------------------------------------

enum { M_SEARCH = 1, M_ADD, M_SIGNOUT, M_QUIT, M_ONLINE, M_OFFLINE, M_PICTURES, M_WINDOWS, M_ABOUT };
static bool g_quit;
static char s_menus[900];
static void build_menu ()
{
	snprintf (s_menus, sizeof s_menus,
		"MTelegram\nI1\t%s\tCtrl+F\nI2\t%s\tCtrl+N\n-\nI3\t%s\nI4\t%s\tCtrl+Q\n"
		"M%s\nI5\t%s\nI6\t%s\n"
		"M%s\nI7\t%s\nI8\t%s\n"
		"M%s\nI9\t%s\n",
		TR ("Search"), TR ("Add a Contact..."), TR ("Sign Out"), TR ("Quit"),
		TR ("Status"), TR ("Online"), TR ("Appear Offline"),
		TR ("View"), g_pictures ? TR ("Hide the Display Pictures") : TR ("Show the Display Pictures"),
		g_windowed ? TR ("Conversations Beside the List") : TR ("Conversations in Their Own Windows"),
		TR ("Help"), TR ("About Telegram for Korin OS"));
	ui_menu ((long) s_menus);
}
static void command (int id)
{
	switch (id)
	{
	case M_SEARCH: if (g_c.state == tg::AS_READY) ui_w_focus (g_search); break;
	case M_ADD: add_contact_dialog (); break;
	case M_SIGNOUT: sign_out (); break;
	case M_QUIT: g_quit = true; break;
	case M_ONLINE: g_c.setOnline (true); ui_view_dirty (g_me); break;
	case M_OFFLINE: g_c.setOnline (false); ui_view_dirty (g_me); break;
	case M_PICTURES:
		g_pictures = !g_pictures; save_config ();
		relayout ();
		for (int i = 1; i < MAXPANE; i++)
			if (g_panes[i].win) { ui_window_use (g_panes[i].win); pn_place (&g_panes[i], 0, (int) ui.w, (int) ui.h, true); ui_fill (0, 0, ui.w, ui.h, jet (J_FACE)); ui_w_draw (0); ui_update_all (); }
		ui_window_use (g_mainWin);
		build_menu ();
		break;
	case M_WINDOWS:
		g_windowed = !g_windowed;
		save_config ();
		if (!g_windowed) close_windows ();
		else { g_main->open = 0; s_selected = 0; }
		apply_mode ();
		build_menu ();
		break;
	case M_ABOUT:
		ui_message ((long) TR ("About Telegram for Korin OS"), (long) TR ("Telegram for Korin OS 1.0 - an unofficial Telegram client in the way of the old Messenger, from Onyx, on Telegram's open API (MTProto 2.0). MIT licence."), UM_OK);
		break;
	}
}

// ---- what the widgets say, the keys ----------------------------------------------------------------------------------

// A click on a widget (out of ui_w_event: dialogues may open here).
static void control (long w, long code)
{
	if (w == g_search) { if (code == UN_CHANGE) { s_lscroll = 0; g_listDirty = true; } return; }
	if (w == g_add) { if (code == UN_ACTIVATE) add_contact_dialog (); return; }
	if (w == g_listBar) { if (code == UN_CHANGE) { s_lscroll = (int) ui_w_get_value (w); ls_clamp (); ui_view_dirty (g_list); } return; }
	if (w == g_siGo || w == g_siF1 || w == g_siF2) { if (code == UN_ACTIVATE) si_submit (); return; }
	Pane *P = pn_of_widget (w);
	if (!P) return;
	if (w == P->chatBar) { if (code == UN_CHANGE) { P->scroll = (int) ui_w_get_value (w); cv_clamp (P); P->stick = P->scroll >= P->total - P->chH - 4; ui_view_dirty (P->chat); if (P->pickerOn) ui_view_dirty (P->picker); if (P->scroll < 60) g_c.loadOlder (P->open); } return; }
	if (code != UN_ACTIVATE) return;
	if (w == P->send) { send_current (P); ui_w_focus (P->edit); }
	else if (w == P->back) back_to_list ();
	else if (w == P->barAdd)
	{
		tg::Conv *c = g_c.conv (P->open);
		if (c && c->blocked) g_c.block (P->open, false);
		else g_c.addToContacts (P->open);
	}
	else if (w == P->barBlock)
	{
		char name[80], q[200];
		short_name (P->open, name, sizeof name);
		snprintf (q, sizeof q, TR ("Block %s? They will not be able to write to you or call you."), name);
		if (ui_message ((long) TR ("Block"), (long) q, UM_YESNO) == 1) g_c.block (P->open, true);
	}
	else if (w == P->barHide) g_c.hideBar (P->open);
}
// A key before the widgets. true: taken.
static bool key_first (int hid, int ch, int mods)
{
	Pane *P = pn_of_win (ui_ev_field (&ui, EV_WIN));
	bool ctrl = mods & UI_MOD_CTRL, alt = mods & UI_MOD_ALT;
	PicWin *W = pw_of_win (ui_ev_field (&ui, EV_WIN));
	if (W) { if (hid == KEY_ESC) pw_close (W); return W != 0; }
	if (hid == KEY_ESC && P && P->pickerOn) { ui_window_use (P->win); pn_picker_show (P, false); ui_window_use (g_mainWin); return true; }
	if (g_c.state != tg::AS_READY) return false;
	if (P && P->conv_on && (hid == KEY_PGUP || hid == KEY_PGDN)) { cv_scroll_by (P, (hid == KEY_PGUP ? -1 : 1) * (P->chH - 40)); return true; }
	if (ctrl && (hid == KEY_UP || hid == KEY_DOWN) && ui_ev_field (&ui, EV_WIN) == g_mainWin) return ls_move (hid == KEY_UP ? -1 : 1);
	// a letter typed in the messages: it goes to the line
	if (P && P->conv_on && ch >= 32 && !ctrl && !alt && ui_w_get_focus () == P->chat) ui_w_focus (P->edit);
	return false;
}
// pictures dropped from the Files: the first one attached
static void drop_pictures (Pane *P)
{
	static char data[512];
	long kind = 0;
	long n = uikit_drop_get (data, sizeof data - 1, &kind);
	if (n <= 0 || kind != UDT_FILES || !P || !P->open || g_c.state != tg::AS_READY) return;
	data[n] = 0;
	char path[300];
	for (const char *p = data; *p; )
	{
		int k = 0;
		while (p[k] && p[k] != '\n') k++;
		if (k > 0 && k < (int) sizeof path)
		{
			memcpy (path, p, (size_t) k); path[k] = 0;
			if (picture_name (path)) { attach_file (P, path); return; }
		}
		p += k; if (*p) p++;
	}
}

// ---- a turn of the loop ------------------------------------------------------------------------------------------------

static void title_sync ()
{
	static int last = -1;
	int n = 0;
	if (g_c.state == tg::AS_READY)
		for (int i = 0; i < g_c.convs.n; i++) if (g_c.convs.a[i]->inList) n += g_c.convs.a[i]->unread;
	if (n == last) return;
	last = n;
	char t[32] = "Telegram";
	if (n) snprintf (t, sizeof t, "(%d) Telegram", n);
	ui_title ((long) t);
}
// a new message elsewhere than the conversation looked at: a notification
static void notifications ()
{
	while (g_c.ninbox > 0)
	{
		tg::Client::Incoming in = g_c.inbox[0];
		memmove (g_c.inbox, g_c.inbox + 1, sizeof g_c.inbox[0] * (size_t) (g_c.ninbox - 1));
		g_c.ninbox--;
		if (g_demo) continue;
		tg::Conv *c = g_c.conv (in.peer);
		if (!c) continue;
		if (peer_shown (in.peer)) { g_c.markRead (c); continue; }
		char name[160], pv[200], title[200];
		g_c.peerName (in.peer, name, sizeof name);
		preview (c, pv, sizeof pv);
		snprintf (title, sizeof title, TR ("%s says:"), name);
		notify (title, pv);
	}
}
static void tick ()
{
	static int s_state = -1;
	static unsigned s_rev;
	static long long s_tmo;
	if (!g_demo) g_c.tick ();
	if ((int) g_c.state != s_state)
	{
		bool was = s_state == tg::AS_READY;
		s_state = (int) g_c.state;
		if (s_state == tg::AS_READY && !was && s_siOffline) g_c.setOnline (false);
		if (s_state != tg::AS_READY) { g_main->open = 0; s_selected = 0; close_windows (); }
		apply_mode ();
		build_menu ();
		title_sync ();
	}
	if (g_c.state == tg::AS_READY)
	{
		if (g_c.rev != s_rev || g_listDirty)
		{
			if (g_c.rev != s_rev) { title_sync (); ui_view_dirty (g_me); ui_view_dirty (g_status); }
			s_rev = g_c.rev;
			g_listDirty = false;
			ls_rebuild (); ls_bar ();
			ui_view_dirty (g_list);
		}
		if (g_c.barError)
		{
			g_c.barError = false;
			char e[200]; friendly_error (g_c.error, g_c.floodWait, e, sizeof e);
			ui_message ((long) "Telegram", (long) e, UM_OK);
		}
		notifications ();
		if (g_c.added)					// (a contact added: their conversation; or why not)
		{
			long long a = g_c.added;
			g_c.added = 0;
			if (a > 0) open_conversation (a);
			else if (a == -1) ui_message ((long) TR ("Add a Contact"), (long) TR ("This phone number has no Telegram account (or its owner does not let others find them by their number)."), UM_OK);
			else { char t[300], e[200]; friendly_error (g_c.error, g_c.floodWait, e, sizeof e); snprintf (t, sizeof t, TR ("The contact could not be added. %s"), e); ui_message ((long) TR ("Add a Contact"), (long) t, UM_OK); }
		}
		long long t = tg_unix_ms ();
		if (t - s_tmo > 30000) { s_tmo = t; ui_view_dirty (g_list); }	// (the times, who is still online)
	}
	else si_tick ();
	bool pics = img_poll ();			// (the card's thread has brought pictures)
	if (pics) { ui_view_dirty (g_list); ui_view_dirty (g_me); }
	unsigned r = g_c.rev;
	for (int i = 0; i < MAXPANE; i++)
	{
		Pane *P = &g_panes[i];
		if (!P->win) continue;
		ui_window_use (P->win);
		if (pics && P->conv_on) { ui_view_dirty (P->chat); ui_view_dirty (P->head); ui_view_dirty (P->dp); if (P->pickerOn) ui_view_dirty (P->picker); }
		pn_tick (P, r);
		ui_view_flush ();
	}
	ui_window_use (g_mainWin);
	if (s_openPic.path[0]) { open_picture (s_openPic.path, s_openPic.w, s_openPic.h); s_openPic.path[0] = 0; }
	pw_tick (pics);
	tg_log_flush (false);
}

int main (int argc, char **argv)
{
	for (int i = 1; i < argc; i++) if (!strcmp (argv[i], "--demo")) g_demo = true;
	if (!uikit_open (&ui) || !archivekit_kit_open ()) return 1;
	vp_tables ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (W0, (int) scr[0] - 8), wh = imin (H0, (int) (scr[1] - scr[3] - scr[2]) - 32 - 96);
	g_mainWin = ui_window_ex (0, scr[3], ww, wh, (long) "Telegram", WF_RESIZABLE);
	if (g_mainWin <= 0) return 1;
	ui_min_size (300, 360);
	ui_on_resize ((long) on_resize, 0);
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/telegram.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	mkdir ("SD0:/home", 0755);
	mkdir ("SD0:/home/telegram", 0755);
	mkdir (TG_DIR "cache", 0755);			// (the profile photos, the pictures)
	look_make ();
	tl::load ();
	snprintf (g_c.dir, sizeof g_c.dir, "%s", TG_DIR);
	g_c.lang = (const char *) ui_lang ();
	load_config ();

	g_w = (int) ui.w; g_h = (int) ui.h;
	ui_fill (0, 0, g_w, g_h, jet (J_FACE));
	ls_create ();
	g_welcome = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_welcome, (long) welcome_paint, 0, 0, 0);
	pn_create (g_main, g_mainWin, false);
	g_status = ui_view (0, 0, 10, SB_H, 0);
	ui_view_on (g_status, (long) status_paint, 0, 0, 0);
	si_create ();
	build_menu ();

	if (g_demo)
	{
		g_c.demo = true;
		g_windowed = false;				// (the whole window: a conversation's own by a double click)
		demo_fill ();
		ls_rebuild ();
		s_selected = tg::pkey (tg::P_USER, 1001);
		pn_open (g_main, s_selected);
	}
	else g_c.begin ();
	f_ital ();					// (read once, not inside a paint)
	si_setup ();
	apply_mode ();

	while (!g_quit)
	{
		long t = ui_event (30);
		if (t == WEV_CLOSE)
		{
			PicWin *W = pw_of_win (ui_ev_field (&ui, EV_WIN));
			Pane *P = pn_of_win (ui_ev_field (&ui, EV_WIN));
			if (W) pw_close (W);
			else if (!P || !P->alone) break;
			else pn_close (P);
		}
		else if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP) { Pane *P = pn_of_win (ui_ev_field (&ui, EV_WIN)); drop_pictures (P); }
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			if (press) tgc::entropy_add (&hid, sizeof hid);
			if (!(press && key_first (hid, ch, mods)))
			{
				long code = 0, taken = 0;
				long w = uikit_w_event (&code, &taken);
				if (code) control (w, code);
				if (press && !taken && ui_ev_field (&ui, EV_WIN) == g_mainWin)
				{
					long id = ui_menu_key (0);
					if (id >= 0) command ((int) id);
				}
			}
			if (s_statusMenu) { s_statusMenu = 0; status_menu (); }
			if (s_rowMenu) { long long p = s_rowMenu; s_rowMenu = 0; row_menu (p); }
			if (s_pickFor) { Pane *P = s_pickFor; s_pickFor = 0; choose_picture (P); }
		}
		else ui_w_idle ();
		tick ();
	}
	if (!g_demo) g_c.end ();
	tg_log_flush (true);
	tg_save_drain ();
	return 0;
}
