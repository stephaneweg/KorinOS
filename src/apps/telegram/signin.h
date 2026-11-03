//
// signin.h -- the window before the account is there, Messenger's sign-in: the sky, the two buddies in their glass frame,
// then one step at a time -- the app's key (api_id, api_hash: my.telegram.org's, once), the phone number and "Sign in as"
// (Online / Appear offline), the code Telegram sent, the cloud password, a new account's name. The server's refusals in
// plain words. One ui_view for the page, the fields and the button on it (on a card of the theme's face: the fields'
// focus ring wants it under them).
//
// MIT licence.
//
#ifndef TG_SIGNIN_H
#define TG_SIGNIN_H

#include "pane.h"

static void save_api (int id, const char *hash);

// The server's error in the user's words.
static void friendly_error (const char *e, int wait, char *out, int cap)
{
	struct { const char *code, *text; } t[] = {
		{ "PHONE_NUMBER_INVALID", TRN ("This phone number is not valid. Give it with its country code: +33 6 12 34 56 78.") },
		{ "PHONE_CODE_INVALID", TRN ("This code is not the right one.") },
		{ "PHONE_CODE_EMPTY", TRN ("Type the code Telegram sent.") },
		{ "PHONE_CODE_EXPIRED", TRN ("This code has expired: ask for a new one (Use another number).") },
		{ "PASSWORD_HASH_INVALID", TRN ("This password is not the right one.") },
		{ "PHONE_NUMBER_BANNED", TRN ("Telegram has banned this phone number.") },
		{ "PHONE_NUMBER_FLOOD", TRN ("Too many codes asked for this number: try again later.") },
		{ "PHONE_PASSWORD_FLOOD", TRN ("Too many tries: try again later.") },
		{ "API_ID_INVALID", TRN ("This api_id and api_hash are not valid: check them at my.telegram.org.") },
		{ "API_ID_PUBLISHED_FLOOD", TRN ("This api_id cannot be used: make your own at my.telegram.org.") },
		{ "FIRSTNAME_INVALID", TRN ("This first name is not valid.") },
		{ "PAYMENT_REQUIRED", TRN ("Telegram asks for a payment to sign in with this number, which this app cannot do.") },
		{ "PASSWORD_PARAMS", TRN ("Telegram sent a kind of password this app does not know.") },
		{ "AUTH_KEY_UNREGISTERED", TRN ("You were signed out: sign in again.") },
		{ "SESSION_REVOKED", TRN ("This session was closed from another device: sign in again.") } };
	for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
		if (!strcmp (e, t[i].code)) { snprintf (out, (size_t) cap, "%s", TR (t[i].text)); return; }
	if (!strncmp (e, "FLOOD_WAIT", 10))
	{
		if (wait >= 120) snprintf (out, (size_t) cap, TR ("Too many tries: wait %d minutes."), (wait + 59) / 60);
		else snprintf (out, (size_t) cap, TR ("Too many tries: wait %d seconds."), wait);
		return;
	}
	snprintf (out, (size_t) cap, TR ("Telegram says: %s"), e);
}

#define COLW	340
static long g_si, g_siF1, g_siF2, g_siGo;
static int s_shown = -1;			// the state the page was made for
static bool s_siBusy, s_siOffline, s_siLink, s_siStat;
static int s_siTop, s_siAv = 112, s_siSpin, s_siCardY, s_siCardH;
static int s_lnk[4], s_stat[4];
static char s_siLocal[200];
static unsigned s_siRev;

static const char *si_go_label ()
{
	switch (g_c.state)
	{
	case tg::AS_NEED_API: return TR ("Continue");
	case tg::AS_PHONE: return TR ("Sign in");
	case tg::AS_SIGNUP: return TR ("Create my account");
	default: return TR ("Next");
	}
}
static const char *si_link ()
{
	switch (g_c.state)
	{
	case tg::AS_CODE: case tg::AS_PASSWORD: case tg::AS_SIGNUP: return TR ("Use another number");
	default: return 0;
	}
}
static void si_labels (const char **l1, const char **l2)
{
	*l1 = *l2 = 0;
	switch (g_c.state)
	{
	case tg::AS_NEED_API: *l1 = "api_id"; *l2 = "api_hash"; break;
	case tg::AS_PHONE: *l1 = TR ("Phone number"); break;
	case tg::AS_CODE: *l1 = TR ("Code"); break;
	case tg::AS_PASSWORD: *l1 = TR ("Password"); break;
	case tg::AS_SIGNUP: *l1 = TR ("First name"); *l2 = TR ("Last name (optional)"); break;
	default: break;
	}
}
static void si_explain (char *out, int cap)
{
	out[0] = 0;
	const char *ph = g_c.phone[0] == '+' ? g_c.phone + 1 : g_c.phone;
	switch (g_c.state)
	{
	case tg::AS_NEED_API:
		snprintf (out, (size_t) cap, "%s", TR ("Telegram asks every app for its own key. Once only: sign in at my.telegram.org with your phone, open \"API development tools\", create an app (any name), then copy its api_id and api_hash here."));
		break;
	case tg::AS_PHONE:
		snprintf (out, (size_t) cap, "%s", TR ("Type your phone number with its country code. Telegram will send you a code, in the Telegram app on your phone or by SMS."));
		break;
	case tg::AS_CODE:
		if (!strcmp (g_c.codeType, "app")) snprintf (out, (size_t) cap, TR ("Telegram sent a code to your Telegram app on another device (+%s)."), ph);
		else if (!strcmp (g_c.codeType, "sms")) snprintf (out, (size_t) cap, TR ("Telegram sent a code by SMS to +%s."), ph);
		else if (!strcmp (g_c.codeType, "call")) snprintf (out, (size_t) cap, TR ("Telegram is calling +%s to tell you a code."), ph);
		else snprintf (out, (size_t) cap, "%s", TR ("Telegram sent you a code."));
		break;
	case tg::AS_PASSWORD:
		if (g_c.passwordHint[0]) snprintf (out, (size_t) cap, TR ("Your account is protected by a password (two-step verification). Its hint: %s"), g_c.passwordHint);
		else snprintf (out, (size_t) cap, "%s", TR ("Your account is protected by a password (two-step verification)."));
		break;
	case tg::AS_SIGNUP:
		snprintf (out, (size_t) cap, "%s", TR ("This number has no Telegram account yet. Your name makes one: it is what your contacts will see."));
		break;
	default: break;
	}
}
static void si_place (int w, int h)
{
	int cx = w / 2, c = ctl_h ();
	s_siAv = h >= 620 ? 112 : h >= 520 ? 84 : 60;
	s_siTop = h > 620 ? 40 : 14;
	int y = s_siTop + s_siAv + (h >= 520 ? 22 : 12) + fh (PX_HUGE) + 6;
	char ex[600]; si_explain (ex, sizeof ex);
	static Rich r; rich_layout (r, ui_px (), ex, COLW, 16, false);
	y += r.height () + 14;
	s_siCardY = y;
	y += 10 + fh (ui_px ()) + 3;
	ui_w_move (g_siF1, cx - COLW / 2 + 12, y, COLW - 24, c);
	y += c;
	bool two = g_c.state == tg::AS_NEED_API || g_c.state == tg::AS_SIGNUP;
	if (two) { y += 8 + fh (ui_px ()) + 3; ui_w_move (g_siF2, cx - COLW / 2 + 12, y, COLW - 24, c); y += c; }
	if (g_c.state == tg::AS_PHONE) y += 34;
	y += 10 + (h >= 520 ? fh (ui_px ()) + 4 : 0);
	ui_w_move (g_siGo, cx - 75, y, 150, c + 2);
	s_siCardH = y + c + 2 + 10 - s_siCardY;
	ui_view_dirty (g_si);
}
// the fields for the state
static void si_setup ()
{
	s_shown = (int) g_c.state;
	ui_w_set_text (g_siF1, (long) ""); ui_w_set_text (g_siF2, (long) "");
	ui_w_style (g_siF1, g_c.state == tg::AS_PASSWORD ? UTF_PASSWORD : 0);
	ui_w_show (g_siF2, g_c.state == tg::AS_NEED_API || g_c.state == tg::AS_SIGNUP);
	if (g_c.state == tg::AS_PHONE && g_c.phone[0]) ui_w_set_text (g_siF1, (long) g_c.phone);
	s_siLocal[0] = 0;
	ui_w_set_text (g_siGo, (long) si_go_label ());
	si_place ((int) ui.w, (int) ui.h);
	ui_w_focus (g_siF1);
}
static void si_submit ()
{
	if (g_c.busy) return;
	const char *f1 = (const char *) ui_w_get_text (g_siF1), *f2 = (const char *) ui_w_get_text (g_siF2);
	switch (g_c.state)
	{
	case tg::AS_NEED_API:
	{
		int id = atoi (f1);
		if (id <= 0 || strlen (f2) != 32) { snprintf (s_siLocal, sizeof s_siLocal, "%s", TR ("The api_id is a number, the api_hash 32 letters and digits.")); ui_view_dirty (g_si); return; }
		s_siLocal[0] = 0;
		save_api (id, f2);
		break;
	}
	case tg::AS_PHONE:
		if (strlen (f1) < 6) { snprintf (s_siLocal, sizeof s_siLocal, "%s", TR ("Type your phone number with its country code.")); ui_view_dirty (g_si); return; }
		s_siLocal[0] = 0;
		g_c.sendCode (f1);
		break;
	case tg::AS_CODE: if (f1[0]) g_c.signIn (f1); break;
	case tg::AS_PASSWORD: if (f1[0]) g_c.checkPassword (f1); break;
	case tg::AS_SIGNUP: if (f1[0]) g_c.signUp (f1, f2); break;
	default: break;
	}
	ui_view_dirty (g_si);
}
static void si_tick ()
{
	if (s_shown != (int) g_c.state) si_setup ();
	bool busy = g_c.busy;
	if (busy != s_siBusy)
	{
		s_siBusy = busy;
		ui_w_enable (g_siGo, !busy);
		ui_w_set_text (g_siGo, (long) (busy ? TR ("Please wait...") : si_go_label ()));
		ui_view_dirty (g_si);
	}
	if (s_siRev != g_c.rev) { s_siRev = g_c.rev; ui_view_dirty (g_si); }
	if (busy && ++s_siSpin % 6 == 0) ui_view_dirty (g_si);
}
static void si_paint (long v, long w, long h)
{
	begin_paint (v);
	sky (0, 0, (int) w, (int) h, false);
	glow ((int) w / 2, (int) h + (int) h / 5, (int) w, (int) h / 2, TC_BG, 110);
	int cx = (int) w / 2, s = s_siAv, px = ui_px ();
	avatar_default (cx - s / 2, s_siTop, s, g_c.state == tg::AS_PHONE && !s_siOffline ? TC_ONLINE : C.group);
	int y = s_siTop + s + (h >= 520 ? 22 : 12);
	const char *title = TR ("Sign in to Telegram");
	switch (g_c.state)
	{
	case tg::AS_NEED_API: title = TR ("Welcome to Telegram"); break;
	case tg::AS_CODE: title = TR ("Enter the code"); break;
	case tg::AS_PASSWORD: title = TR ("Your cloud password"); break;
	case tg::AS_SIGNUP: title = TR ("Your name"); break;
	default: break;
	}
	ftext (title, cx - ftw (title, PX_HUGE) / 2, y, TC_TITLE, PX_HUGE);
	y += fh (PX_HUGE) + 6;
	char ex[600];
	si_explain (ex, sizeof ex);
	static Rich r;
	rich_layout (r, px, ex, COLW, 16, false);
	rich_draw (r, ex, cx - COLW / 2, y, TC_GREY);
	// the card under the fields: the theme's face
	int cl = cx - COLW / 2;
	ui_rbox (cl - 2, s_siCardY, COLW + 4, s_siCardH, 6, jet (J_FACE), jet (J_FACE));
	ui_frame (cl - 2, s_siCardY + 5, 1, imax (0, s_siCardH - 10), TC_LINE);
	ui_frame (cl + COLW + 1, s_siCardY + 5, 1, imax (0, s_siCardH - 10), TC_LINE);
	const char *l1 = 0, *l2 = 0;
	si_labels (&l1, &l2);
	int fy = s_siCardY + 10;
	if (l1) ftext (l1, cl + 12, fy, jet (J_TEXTE1), px, 2);
	if (l2) ftext (l2, cl + 12, fy + fh (px) + 3 + ctl_h () + 8, jet (J_TEXTE1), px, 2);
	// "Sign in as: Online"
	if (g_c.state == tg::AS_PHONE)
	{
		int sy = fy + fh (px) + 3 + ctl_h () + 10, lx = cl + 12;
		const char *la = TR ("Sign in as:");
		ftext (la, lx, sy, jet (J_TEXTE1), px);
		int sx = lx + ftw (la, px) + 8;
		const char *st = s_siOffline ? TR ("Appear offline") : TR ("Online");
		int sw = ftw (st, px) + 40;
		if (s_siStat) ui_rbox (sx - 4, sy - 3, sw, 22, 3, mix (jet (J_FACE), jet (J_ACCENT), 24), mix (jet (J_FACE), jet (J_ACCENT), 44));
		buddy (sx, sy, 15, s_siOffline ? TC_OFFLINE : TC_ONLINE);
		ftext (st, sx + 20, sy, readable (jet (J_ACCENT), jet (J_FACE), 4.5), px);
		ftext ("\xE2\x96\xBE", sx + sw - 16, sy, jet (J_TEXTE2), 11);
		s_stat[0] = sx - 4; s_stat[1] = sy - 3; s_stat[2] = sw; s_stat[3] = 22;
	}
	// the error, the spinner, the link: under the card
	int uy = s_siCardY + s_siCardH + 6;
	char err[300] = "";
	if (s_siLocal[0]) snprintf (err, sizeof err, "%s", s_siLocal);
	else if (g_c.error[0]) friendly_error (g_c.error, g_c.floodWait, err, sizeof err);
	if (err[0])
	{
		static Rich er; rich_layout (er, px, err, COLW, 16, false);
		rich_draw (er, err, cx - COLW / 2, uy, readable (jet (J_DANGER), TC_BG, 4.5));
		uy += er.height () + 4;
	}
	if (g_c.busy)
	{
		long sx = cx + 75 + 22, sy = s_siCardY + s_siCardH - 26;
		for (int i = 0; i < 8; i++)
		{
			int k = (i * 8 - (s_siSpin / 6) * 8) & 63;
			vp_disc (sx + 8 * fcos (k), sy - 8 * fsin (k), 2, mix (jet (J_FACE), jet (J_ACCENT), 60 + i * 24));
		}
	}
	const char *link = si_link ();
	if (link)
	{
		int lw = ftw (link, px), lx = cx - lw / 2;
		ftext (link, lx, uy, TC_LINK, px);
		if (s_siLink) ui_fill (lx, uy + px + 2, lw, 1, TC_LINK);
		s_lnk[0] = lx; s_lnk[1] = uy; s_lnk[2] = lw; s_lnk[3] = fh (px);
	}
	const char *foot = TR ("Telegram for Korin OS - an unofficial client, made on Telegram's open API.");
	if (uy + 40 < h) ftext (foot, cx - ftw (foot, PX_SMALL, 1) / 2, (int) h - 20, TC_GREY, PX_SMALL, 1);
}
static long si_mouse (long v, long kind, long x, long y, long info)
{
	bool lh = kind != 3 && si_link () && x >= s_lnk[0] && x < s_lnk[0] + s_lnk[2] && y >= s_lnk[1] && y < s_lnk[1] + s_lnk[3];
	bool sh = kind != 3 && g_c.state == tg::AS_PHONE && x >= s_stat[0] && x < s_stat[0] + s_stat[2] && y >= s_stat[1] && y < s_stat[1] + s_stat[3];
	if (lh != s_siLink || sh != s_siStat) { s_siLink = lh; s_siStat = sh; ui_view_dirty (v); ui_cursor (lh || sh ? UC_HAND : UC_ARROW); }
	if (kind == 1 && ((info >> 8) & 1))
	{
		if (lh) { g_c.backToPhone (); s_siLocal[0] = 0; }
		if (sh) { s_siOffline = !s_siOffline; ui_view_dirty (v); }
	}
	return 0;
}
static void si_create ()
{
	g_si = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_si, (long) si_paint, (long) si_mouse, 0, 0);
	g_siF1 = ui_textbox (0, 0, COLW - 24, ctl_h (), 0);
	g_siF2 = ui_textbox (0, 0, COLW - 24, ctl_h (), 0);
	ui_w_set_range (g_siF1, 0, 120, 0, 0); ui_w_set_range (g_siF2, 0, 120, 0, 0);
	g_siGo = ui_pushbutton (0, 0, 150, 0, (long) TR ("Sign in"), UB_DEFAULT);
	ui_w_set_parent (g_siF1, g_si); ui_w_set_parent (g_siF2, g_si); ui_w_set_parent (g_siGo, g_si);
}
static void si_show (bool on)
{
	ui_w_show (g_si, on);
}

#endif
