//
// wizard.h -- adding an account, step by step (Onyx's Apps/mail/wizard.h), a page of the window and not a dialogue: the
// worker's answers come back to the loop while it shows. The name and the address; the provider recognised by the
// address's domain (accounts.h) -- Gmail, iCloud, Yahoo: an app password, the page that makes it opened in Jet;
// Outlook.com / Hotmail: Microsoft's sign-in by a code (on a phone or a PC); another one: its password, its servers
// guessed or typed by hand (IMAP or POP3, the security, the ports); then the settings tried (the worker: J_CHECK) and
// the account kept. A certificate refused: said, and "Connect anyway" (the account's verify off). And the settings of
// the accounts there are (the name, the signature, how often to look for mail, the password, removal).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef _mail_wizard_h
#define _mail_wizard_h

#include "compose.h"

namespace mailapp {

enum { WP_START, WP_APPPW, WP_OUTLOOK, WP_PASSWORD, WP_MANUAL, WP_CHECKING, WP_DONE };
static struct
{
	int page, prev; Account a; int provider;
	char err[300]; bool certBad;
	DeviceCode dc; bool haveCode, waiting, copied;
} W;
static long g_wp, g_wTitle, g_wPara, g_wPara2, g_wL[8], g_wErr, g_wCode;
static long g_wName, g_wEmail, g_wPw, g_wInHost, g_wInPort, g_wInUser, g_wOutHost, g_wOutPort, g_wOutUser, g_wOutPw;
static long g_wKind, g_wInSec, g_wOutSec, g_wKeep, g_wHand, g_wWeb, g_wMs, g_wGoog, g_wGetCode, g_wCopy, g_wAnyway, g_wBack, g_wNext, g_wCancel;

static void wz_err_paint (long, long w, long h)
{
	unsigned bg = mix (C_FACE, jet (J_DANGER), 40);
	ui_fill (0, 0, w, h, C_FACE);
	rbox (0, 0, (int) w, (int) h, 6, bg);
	icon (I_WARN, 10, ((int) h - 20) / 2, 20, jet (J_DANGER), bg);
	ui_text_wrap ((long) W.err, 40, 6, (long) w - 50, C_TEXT, face (0), FACE_PX[F_SMALL], 0);
}
static void wz_code_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FACE);
	rbox (0, 0, (int) w, (int) h, 8, C_FIELD);
	text_c (0, 0, (int) w, (int) h, W.dc.userCode, col_accent_text (), F_H1, 1);
}
static long wz_label (const char *s, int flags) { long l = ui_label (0, 0, 10, 10, (long) s, flags); ui_w_set_parent (l, g_wp); return l; }
static long wz_field (int flags = 0) { long t = ui_textbox (0, 0, 10, ctl_h (), flags); ui_w_set_parent (t, g_wp); ui_w_set_range (t, 0, 190, 0, 0); return t; }
static long wz_button (const char *s, int flags = 0) { long b = ui_pushbutton (0, 0, 100, 0, (long) s, flags); ui_w_set_parent (b, g_wp); return b; }
static long wz_drop (const char *s) { long d = ui_dropdown (0, 0, 10, ctl_h (), (long) s); ui_w_set_parent (d, g_wp); return d; }
static void wz_create ()
{
	g_wp = ui_panel (0, 0, 10, 10, 0);
	g_wTitle = wz_label ("", ULB_TITLE);
	g_wPara = wz_label ("", ULB_WRAP);
	g_wPara2 = wz_label ("", ULB_WRAP | ULB_DIM);
	for (int i = 0; i < 8; i++) g_wL[i] = wz_label ("", 0);
	g_wName = wz_field (); g_wEmail = wz_field (); g_wPw = wz_field (UTF_PASSWORD);
	g_wInHost = wz_field (); g_wInPort = wz_field (); g_wInUser = wz_field ();
	g_wOutHost = wz_field (); g_wOutPort = wz_field (); g_wOutUser = wz_field (); g_wOutPw = wz_field (UTF_PASSWORD);
	char sec[60]; snprintf (sec, sizeof sec, "SSL/TLS|STARTTLS|%s", TR ("None"));
	g_wKind = wz_drop ("IMAP|POP3"); g_wInSec = wz_drop (sec); g_wOutSec = wz_drop (sec);
	g_wKeep = ui_checkbox (0, 0, 10, 0, (long) TR ("Leave the messages on the server (POP3)"), 1); ui_w_set_parent (g_wKeep, g_wp);
	g_wHand = wz_button (TR ("Settings by hand...")); g_wWeb = wz_button (TR ("Open the page in Jet"));
	g_wMs = wz_button (TR ("Microsoft 365 (Outlook)")); g_wGoog = wz_button (TR ("Google Workspace (Gmail)"));
	g_wGetCode = wz_button (TR ("Get a code")); g_wCopy = wz_button (TR ("Copy the code"));
	g_wAnyway = wz_button (TR ("Connect anyway"));
	g_wCancel = wz_button (TR ("Cancel")); g_wBack = wz_button (TR ("Back")); g_wNext = wz_button (TR ("Next"), UB_DEFAULT);
	g_wErr = ui_view (0, 0, 10, 10, 0); ui_view_on (g_wErr, (long) wz_err_paint, 0, 0, 0); ui_w_set_parent (g_wErr, g_wp);
	g_wCode = ui_view (0, 0, 10, 10, 0); ui_view_on (g_wCode, (long) wz_code_paint, 0, 0, 0); ui_w_set_parent (g_wCode, g_wp);
}
// the values kept across the pages: into the fields when a page opens, out of them when it is left
static void wz_fill ()
{
	Account &a = W.a; char p[12];
	set_text (g_wName, a.name); set_text (g_wEmail, a.email); set_text (g_wPw, a.inSecret);
	set_text (g_wInHost, a.inHost); snprintf (p, sizeof p, "%d", a.inPort); set_text (g_wInPort, p); set_text (g_wInUser, a.inUser);
	set_text (g_wOutHost, a.outHost); snprintf (p, sizeof p, "%d", a.outPort); set_text (g_wOutPort, p); set_text (g_wOutUser, a.outUser); set_text (g_wOutPw, a.outSecret);
	ui_w_set_value (g_wKind, a.kind); ui_w_set_value (g_wInSec, a.inSec); ui_w_set_value (g_wOutSec, a.outSec); ui_w_set_value (g_wKeep, a.popKeep);
}
static void wz_read ()
{
	Account &a = W.a; int pg = W.page;
	if (pg == WP_START)
	{
		scpy (a.name, tx (g_wName), sizeof a.name);
		char e[160]; const char *s = tx (g_wEmail); while (*s == ' ') s++; scpy (e, s, sizeof e); int n = (int) strlen (e); while (n && e[n - 1] == ' ') e[--n] = 0;
		scpy (a.email, e, sizeof a.email);
	}
	if (pg == WP_APPPW || pg == WP_PASSWORD || pg == WP_MANUAL) scpy (a.inSecret, tx (g_wPw), sizeof a.inSecret);
	if (pg == WP_MANUAL)
	{
		a.kind = ui_w_get_value (g_wKind) == 1 ? K_POP3 : K_IMAP;
		scpy (a.inHost, tx (g_wInHost), sizeof a.inHost); a.inPort = atoi (tx (g_wInPort)); a.inSec = (int) ui_w_get_value (g_wInSec); scpy (a.inUser, tx (g_wInUser), sizeof a.inUser);
		scpy (a.outHost, tx (g_wOutHost), sizeof a.outHost); a.outPort = atoi (tx (g_wOutPort)); a.outSec = (int) ui_w_get_value (g_wOutSec); scpy (a.outUser, tx (g_wOutUser), sizeof a.outUser);
		scpy (a.outSecret, tx (g_wOutPw), sizeof a.outSecret);
		a.popKeep = ui_w_get_value (g_wKeep) != 0;
	}
}
static void put (long w, int x, int y, int l, int h, const char *t = 0) { ui_w_move (w, x, y, l, h); ui_w_show (w, 1); if (t) set_text (w, t); }
// the page's widgets, in the panel at (x, y0), w x h
static void wz_place (int x, int y0, int wd, int h)
{
	static long *ALL[] = { &g_wTitle, &g_wPara, &g_wPara2, &g_wL[0], &g_wL[1], &g_wL[2], &g_wL[3], &g_wL[4], &g_wL[5], &g_wL[6], &g_wL[7], &g_wErr, &g_wCode, &g_wName, &g_wEmail, &g_wPw,
		&g_wInHost, &g_wInPort, &g_wInUser, &g_wOutHost, &g_wOutPort, &g_wOutUser, &g_wOutPw, &g_wKind, &g_wInSec, &g_wOutSec, &g_wKeep, &g_wHand, &g_wWeb, &g_wMs, &g_wGoog,
		&g_wGetCode, &g_wCopy, &g_wAnyway, &g_wBack, &g_wNext, &g_wCancel };
	for (unsigned i = 0; i < sizeof ALL / sizeof ALL[0]; i++) ui_w_show (*ALL[i], 0);
	ui_w_move (g_wp, x, y0, wd, h);
	int cw = imin (580, wd - 24), cx = x + (wd - cw) / 2, c = ctl_h (), y = y0 + imax (10, (h - 400) / 3), lw = 130, fx = cx + lw, fw = cw - lw;
	Account &a = W.a; char t[400];
	int by = y0 + h - c - 12;
	put (g_wCancel, cx, by, 100, c);
	switch (W.page)
	{
	case WP_START:
		put (g_wTitle, cx, y, cw, 26, TR ("Your mail account")); y += 32;
		put (g_wPara2, cx, y, cw, 36, TR ("Gmail, Outlook.com, iCloud, Yahoo and others are recognised by their address: their servers are filled in for you.")); y += 46;
		put (g_wL[0], cx, y, lw, c, TR ("Your name")); put (g_wName, fx, y, fw, c); y += 38;
		put (g_wL[1], cx, y, lw, c, TR ("E-mail address")); put (g_wEmail, fx, y, fw, c); y += 38;
		put (g_wPara, fx, y, fw, 36, TR ("Your name is what the people you write to see."));
		break;
	case WP_APPPW:
	{
		const Provider &v = PROVIDERS[W.provider >= 0 ? W.provider : 0];
		snprintf (t, sizeof t, TR ("%s: an app password"), v.label);
		put (g_wTitle, cx, y, cw, 26, t); y += 32;
		put (g_wPara, cx, y, cw, 36, TR (v.note)); y += 42;
		put (g_wPara2, cx, y, cw, 36, TR ("Make one on the page below (in Jet), then type its letters here. Mail keeps it encrypted on this card.")); y += 42;
		put (g_wWeb, cx, y, 220, c); y += 42;
		put (g_wL[0], cx, y, lw, c, TR ("App password")); put (g_wPw, fx, y, fw, c);
		break;
	}
	case WP_PASSWORD:
		put (g_wTitle, cx, y, cw, 26, a.email); y += 32;
		snprintf (t, sizeof t, TR ("Mail will try %s (IMAP) and %s (SMTP)."), a.inHost, a.outHost);
		put (g_wPara2, cx, y, cw, 36, t); y += 42;
		put (g_wL[0], cx, y, lw, c, TR ("Password")); put (g_wPw, fx, y, fw, c); y += 42;
		put (g_wHand, cx, y, 200, c); y += 44;
		put (g_wPara, cx, y, cw, 36, TR ("A work or school address whose mail Microsoft 365 or Google Workspace keeps? Choose it:")); y += 40;
		put (g_wMs, cx, y, (cw - 10) / 2, c); put (g_wGoog, cx + (cw + 10) / 2, y, (cw - 10) / 2, c);
		break;
	case WP_OUTLOOK:
		put (g_wTitle, cx, y, cw, 26, W.provider < 0 ? TR ("Microsoft 365: sign in at Microsoft") : TR ("Outlook.com: sign in at Microsoft")); y += 32;
		put (g_wPara, cx, y, cw, 52, TR ("Microsoft does not take passwords from mail apps: you sign in on its own page, on a phone or a PC, with a code Mail gets for you.")); y += 56;
		if (!W.haveCode) { if (!W.waiting) put (g_wGetCode, cx, y, 160, c); else put (g_wPara2, cx, y, cw, 20, TR ("Asking Microsoft for a code...")); }
		else
		{
			snprintf (t, sizeof t, TR ("1. On a phone or a PC, open  %s"), W.dc.verifyUri);
			put (g_wPara2, cx, y, cw, 20, t); y += 24;
			put (g_wL[0], cx, y, cw, 20, TR ("2. Type this code:")); y += 22;
			put (g_wCode, cx, y, cw, 52); ui_view_dirty (g_wCode); y += 58;
			put (g_wL[1], cx, y, cw, 20, TR ("3. Sign in with your Microsoft account and allow \"Onyx Mail\".")); y += 24;
			put (g_wL[2], cx, y, cw, 20, W.copied ? TR ("The code is in the clipboard: paste it in Microsoft's page.") : TR ("Mail goes on by itself as soon as it is done.")); y += 26;
			put (g_wWeb, cx, y, 220, c); put (g_wCopy, cx + 230, y, 160, c);
		}
		break;
	case WP_MANUAL:
	{
		int rx = cx + 100, row = 36, pw = 56, sw = 110, hw = imax (80, cw - 100 - pw - sw - 16);
		y = y0 + 8;
		put (g_wL[0], cx, y, 100, c, TR ("Incoming")); put (g_wKind, rx, y, 100, c); y += row;
		put (g_wL[1], cx, y, 100, c, TR ("Server, port")); put (g_wInHost, rx, y, hw, c); put (g_wInPort, rx + hw + 8, y, pw, c); put (g_wInSec, rx + hw + pw + 16, y, sw, c); y += row;
		put (g_wL[2], cx, y, 100, c, TR ("User, password")); put (g_wInUser, rx, y, (cw - 108) / 2, c); put (g_wPw, rx + (cw - 108) / 2 + 8, y, (cw - 108) / 2, c); y += row + 4;
		put (g_wL[3], cx, y, 200, c, TR ("Outgoing (SMTP)")); y += row - 6;
		put (g_wL[4], cx, y, 100, c, TR ("Server, port")); put (g_wOutHost, rx, y, hw, c); put (g_wOutPort, rx + hw + 8, y, pw, c); put (g_wOutSec, rx + hw + pw + 16, y, sw, c); y += row;
		put (g_wL[5], cx, y, 100, c, TR ("User, password")); put (g_wOutUser, rx, y, (cw - 108) / 2, c); put (g_wOutPw, rx + (cw - 108) / 2 + 8, y, (cw - 108) / 2, c); y += row;
		put (g_wPara2, rx, y, cw - 100, 18, TR ("(empty: the same as for the incoming mail)")); y += 20;
		if (a.kind == K_POP3) put (g_wKeep, rx, y, cw - 100, 24);
		break;
	}
	case WP_CHECKING:
		put (g_wTitle, cx, y, cw, 26, TR ("Checking the settings...")); y += 36;
		put (g_wPara2, cx, y, cw, 20, g_m.worker.current[0] ? g_m.worker.current : TR ("Connecting..."));
		break;
	case WP_DONE:
		put (g_wTitle, cx, y, cw, 26, TR ("Your account is ready")); y += 32;
		snprintf (t, sizeof t, "%s  (%s)", a.email, a.kind == K_POP3 ? "POP3" : "IMAP");
		put (g_wPara2, cx, y, cw, 20, t); y += 28;
		put (g_wPara, cx, y, cw, 36, TR ("Mail now fetches your messages. New mail is looked for every 5 minutes (Accounts and settings)."));
		ui_w_show (g_wCancel, 0);
		break;
	}
	if (W.page != WP_START && W.page != WP_CHECKING && W.page != WP_DONE) put (g_wBack, cx + cw - 230, by, 100, c);
	if (W.page != WP_CHECKING && W.page != WP_OUTLOOK)
		put (g_wNext, cx + cw - 120, by, 120, c, W.page == WP_DONE ? TR ("Finish") : W.page == WP_APPPW || W.page == WP_PASSWORD || W.page == WP_MANUAL ? TR ("Sign in") : TR ("Next"));
	if (W.err[0])
	{
		int eh = W.certBad ? 60 : 46, ew = W.certBad ? cw - 180 : cw;
		put (g_wErr, cx, by - eh - 8, ew, eh); ui_view_dirty (g_wErr);
		if (W.certBad) put (g_wAnyway, cx + cw - 170, by - eh - 8 + (eh - c) / 2, 170, c);
	}
}
static void wz_go (int p, bool keepErr = false)
{
	W.prev = W.page; W.page = p; if (!keepErr) { W.err[0] = 0; W.certBad = false; }
	wz_fill ();
	layout ();
	if (p == WP_START) ui_w_focus (g_wName);
	else if (p == WP_APPPW || p == WP_PASSWORD) ui_w_focus (g_wPw);
	else if (p == WP_MANUAL) ui_w_focus (g_wInHost);
}
static void wz_oops (const char *s) { scpy (W.err, s, sizeof W.err); W.certBad = false; layout (); }
static void open_wizard ()
{
	if (g_m.accts.n >= 12) { ask (TR ("12 accounts at most.")); return; }
	if (g_page == P_COMPOSE) return;
	memset (&W, 0, sizeof W);
	account_defaults (W.a); W.provider = -1;
	show_page (P_WIZARD);
	W.page = WP_START; wz_go (WP_START);
}
static void wz_check ()
{
	wz_go (WP_CHECKING);
	Job *j = new Job; memset (j, 0, sizeof *j); j->kind = J_CHECK; j->acct = W.a;
	g_m.worker.cancel = 0; g_m.worker.push (j);
}
static void wz_finish ()
{
	if (g_m.accts.n >= 12) return;
	Account &a = W.a;
	g_m.accts.new_id (a.id, sizeof a.id);
	if (!a.label[0]) scpy (a.label, TR ("Mail"), sizeof a.label);
	a.colour = ACCOUNT_COLOURS[g_m.accts.n % 8];
	if (a.provider == PV_GMAIL) a.colour = 0xD93025; else if (a.provider == PV_OUTLOOK) a.colour = 0x0F6CBD;
	g_m.accts.a[g_m.accts.n++] = a;
	g_m.accts.save (); g_m.accts.save_secrets ();
	g_m.open_store (g_m.accts.n - 1);
	g_m.sync (g_m.accts.n - 1);
}
static void wz_next ()
{
	wz_read ();
	Account &a = W.a;
	switch (W.page)
	{
	case WP_START:
	{
		if (!strchr (a.email, '@') || strchr (a.email, ' ')) { wz_oops (TR ("Type your whole e-mail address (name@example.com).")); return; }
		char nm[120]; scpy (nm, a.name, sizeof nm); char em[160]; scpy (em, a.email, sizeof em);
		int keepKind = a.kind;
		account_defaults (a); scpy (a.name, nm, sizeof a.name); a.kind = keepKind; account_guess (a, em);
		W.provider = provider_of (em);
		if (W.provider >= 0 && PROVIDERS[W.provider].auth == AU_OAUTH) wz_go (WP_OUTLOOK);
		else if (W.provider >= 0 && PROVIDERS[W.provider].note[0]) wz_go (WP_APPPW);
		else wz_go (WP_PASSWORD);
		return;
	}
	case WP_APPPW: case WP_PASSWORD: case WP_MANUAL:
		if (!a.inSecret[0]) { wz_oops (TR ("Type the password.")); return; }
		// (Gmail's app password is shown in groups: the spaces off)
		if (W.page == WP_APPPW) { char t[200]; int k = 0; for (const char *p = a.inSecret; *p; p++) if (*p != ' ') t[k++] = *p; t[k] = 0; scpy (a.inSecret, t, sizeof a.inSecret); }
		wz_check ();
		return;
	case WP_DONE:
		wz_finish ();
		c_accounts_changed ();
		show_page (P_MAIL);
		select_view (SEL_UNIFIED);
		return;
	}
}
// what a widget of the page said: true when it was the page's
static bool wz_control (long w, long code)
{
	Account &a = W.a;
	if (w == g_wCancel && code == UN_ACTIVATE) { g_m.worker.cancel = 1; show_page (P_MAIL); return true; }
	if (w == g_wNext && code == UN_ACTIVATE) { wz_next (); return true; }
	if (w == g_wBack && code == UN_ACTIVATE) { wz_read (); wz_go (W.page == WP_MANUAL ? (W.prev == WP_MANUAL || W.prev == WP_CHECKING ? WP_START : W.prev) : WP_START); return true; }
	if (w == g_wHand && code == UN_ACTIVATE) { wz_read (); wz_go (WP_MANUAL); return true; }
	if (w == g_wWeb && code == UN_ACTIVATE)
	{
		if (W.page == WP_OUTLOOK) { spawn_app (WEB_PROGRAM, W.dc.verifyUri[0] ? W.dc.verifyUri : "https://login.microsoft.com/device"); return true; }
		spawn_app (WEB_PROGRAM, a.provider == PV_GMAIL ? "https://myaccount.google.com/apppasswords" : a.provider == PV_ICLOUD ? "https://account.apple.com" : a.provider == PV_YAHOO ? "https://login.yahoo.com/account/security" : "https://www.fastmail.com/settings/security/devicekeys");
		return true;
	}
	if (w == g_wMs && code == UN_ACTIVATE) { wz_read (); account_hosted (a, PV_OUTLOOK); a.inSecret[0] = 0; W.haveCode = false; W.provider = -1; wz_go (WP_OUTLOOK); return true; }
	if (w == g_wGoog && code == UN_ACTIVATE) { wz_read (); account_hosted (a, PV_GMAIL); a.inSecret[0] = 0; W.provider = provider_of ("x@gmail.com"); wz_go (WP_APPPW); return true; }
	if (w == g_wGetCode && code == UN_ACTIVATE) { W.waiting = true; Job *j = new Job; memset (j, 0, sizeof *j); j->kind = J_OAUTH_START; j->acct = a; g_m.worker.cancel = 0; g_m.worker.push (j); layout (); return true; }
	if (w == g_wCopy && code == UN_ACTIVATE) { ui_clip_set ((long) W.dc.userCode, -1); W.copied = true; layout (); return true; }
	if (w == g_wAnyway && code == UN_ACTIVATE)
	{	// the user said so: this account's certificates are not checked
		a.verify = false; wz_read ();
		wz_check ();
		return true;
	}
	if (w == g_wKind && code == UN_CHANGE)
	{
		wz_read ();
		bool pop = a.kind == K_POP3;
		char host[120]; const char *at = strrchr (a.email, '@');
		if (W.provider < 0) { snprintf (host, sizeof host, pop ? "pop.%s" : "imap.%s", at ? at + 1 : ""); scpy (a.inHost, host, sizeof a.inHost); }
		a.inPort = pop ? 995 : 993; a.inSec = SEC_TLS;
		wz_fill (); layout ();
		return true;
	}
	if (w == g_wInSec && code == UN_CHANGE)
	{
		wz_read ();
		a.inPort = a.kind == K_POP3 ? (a.inSec == SEC_TLS ? 995 : 110) : (a.inSec == SEC_TLS ? 993 : 143);
		wz_fill ();
		return true;
	}
	if (w == g_wOutSec && code == UN_CHANGE) { wz_read (); a.outPort = a.outSec == SEC_TLS ? 465 : 587; wz_fill (); return true; }
	if (code == UN_ACTIVATE && (w == g_wName || w == g_wEmail || w == g_wPw)) { wz_next (); return true; }
	return false;
}
// the worker's answers
static void wz_answer (Result *r)
{
	if (r->kind == J_OAUTH_START)
	{
		W.waiting = false;
		if (!r->ok) { wz_oops (r->err); return; }
		W.dc = r->dc; W.haveCode = true; W.copied = false; W.err[0] = 0; layout ();
		Job *j = new Job; memset (j, 0, sizeof *j); j->kind = J_OAUTH_POLL; j->acct = W.a; j->dc = W.dc; g_m.worker.push (j);
		return;
	}
	if (r->kind == J_OAUTH_POLL)
	{
		if (!r->ok || r->oauthState != 1) { W.haveCode = false; wz_oops (r->err[0] ? r->err : TR ("The sign-in did not finish.")); return; }
		scpy (W.a.access, r->access, sizeof W.a.access); scpy (W.a.refresh, r->refresh, sizeof W.a.refresh); W.a.expires = r->expires;
		wz_check ();
		return;
	}
	if (r->kind == J_CHECK && W.page == WP_CHECKING)
	{
		if (r->ok) { if (r->tokens) { scpy (W.a.access, r->access, sizeof W.a.access); scpy (W.a.refresh, r->refresh, sizeof W.a.refresh); W.a.expires = r->expires; } wz_go (WP_DONE); return; }
		int back = W.prev == WP_CHECKING ? WP_PASSWORD : W.prev;
		if (back == WP_OUTLOOK) W.haveCode = false;
		// a password refused stays on its page; the rest (a server not found, a certificate...) goes to the servers
		bool pw = ifind (r->err, "refused") || ifind (r->err, "credentials") || ifind (r->err, "password") || ifind (r->err, "AUTH");
		W.prev = WP_CHECKING;
		scpy (W.err, r->err, sizeof W.err); W.certBad = r->certBad;
		wz_go (pw || back == WP_OUTLOOK ? back : WP_MANUAL, true);
	}
}

// ---- the accounts' settings ---------------------------------------------------------------------------------------------------------
static long g_sp, g_stList, g_stTitle, g_stL[5], g_stName, g_stLabel, g_stEvery, g_stSig, g_stPw, g_stVerify, g_stSrv, g_stAdd, g_stDel, g_stClose;
static int s_stCur;
static const int EVM[6] = { 0, 1, 5, 15, 30, 60 };
static void st_create ()
{
	g_sp = ui_panel (0, 0, 10, 10, 0);
	auto lab = [] (const char *s, int f) { long l = ui_label (0, 0, 10, 10, (long) s, f); ui_w_set_parent (l, g_sp); return l; };
	auto fld = [] (int f) { long t = ui_textbox (0, 0, 10, ctl_h (), f); ui_w_set_parent (t, g_sp); ui_w_set_range (t, 0, 190, 0, 0); return t; };
	auto btn = [] (const char *s, int f) { long b = ui_pushbutton (0, 0, 100, 0, (long) s, f); ui_w_set_parent (b, g_sp); return b; };
	g_stList = ui_list (0, 0, 10, 10, UL_NOSORT); ui_w_set_parent (g_stList, g_sp);
	g_stTitle = lab ("", ULB_TITLE);
	static const char *const L[5] = { "Your name", "Shown as", "New mail", "Signature (added to what you write)", "New password" };
	for (int i = 0; i < 5; i++) g_stL[i] = lab (TR (L[i]), 0);
	g_stName = fld (0); g_stLabel = fld (0); g_stPw = fld (UTF_PASSWORD);
	char ev[200]; snprintf (ev, sizeof ev, "%s|%s|%s|%s|%s|%s", TR ("By hand only"), TR ("Every minute"), TR ("Every 5 minutes"), TR ("Every 15 minutes"), TR ("Every 30 minutes"), TR ("Every hour"));
	g_stEvery = ui_dropdown (0, 0, 10, ctl_h (), (long) ev); ui_w_set_parent (g_stEvery, g_sp);
	g_stSig = ui_textarea (0, 0, 10, 10, UTA_FORMTAB); ui_w_set_parent (g_stSig, g_sp);
	g_stVerify = ui_checkbox (0, 0, 10, 0, (long) TR ("Check the servers' certificates"), 1); ui_w_set_parent (g_stVerify, g_sp);
	g_stSrv = lab ("", ULB_DIM | ULB_WRAP);
	g_stAdd = btn (TR ("Add an account..."), 0); g_stDel = btn (TR ("Remove..."), 0); g_stClose = btn (TR ("Close"), UB_DEFAULT);
}
static void st_fill ()
{
	ui_list_clear (g_stList);
	for (int i = 0; i < g_m.accts.n; i++) ui_list_add (g_stList, (long) g_m.accts.a[i].label, i);
	bool any = s_stCur < g_m.accts.n;
	if (any) ui_w_set_value (g_stList, s_stCur);
	static long *F[] = { &g_stTitle, &g_stL[0], &g_stL[1], &g_stL[2], &g_stL[3], &g_stL[4], &g_stName, &g_stLabel, &g_stEvery, &g_stSig, &g_stPw, &g_stVerify, &g_stSrv, &g_stDel };
	for (unsigned i = 0; i < sizeof F / sizeof F[0]; i++) ui_w_show (*F[i], any);
	if (!any) return;
	Account &a = g_m.accts.a[s_stCur];
	set_text (g_stTitle, a.email); set_text (g_stName, a.name); set_text (g_stLabel, a.label); set_text (g_stSig, a.signature); set_text (g_stPw, "");
	int sel = 2; for (int i = 0; i < 6; i++) if (EVM[i] == a.checkMinutes) sel = i;
	ui_w_set_value (g_stEvery, sel); ui_w_set_value (g_stVerify, a.verify);
	ui_w_show (g_stPw, a.auth == AU_PASSWORD); ui_w_show (g_stL[4], a.auth == AU_PASSWORD);
	char srv[400];
	snprintf (srv, sizeof srv, TR ("%s %s:%d (%s)  \xC2\xB7  SMTP %s:%d (%s)%s"), a.kind == K_POP3 ? "POP3" : "IMAP", a.inHost, a.inPort, a.inSec == SEC_TLS ? "SSL/TLS" : a.inSec == SEC_STARTTLS ? "STARTTLS" : TR ("plain"),
		a.outHost, a.outPort, a.outSec == SEC_TLS ? "SSL/TLS" : a.outSec == SEC_STARTTLS ? "STARTTLS" : TR ("plain"), a.auth == AU_OAUTH ? TR ("  \xC2\xB7  Microsoft sign-in") : "");
	set_text (g_stSrv, srv);
}
static void st_place (int x, int y0, int w, int h)
{
	ui_w_move (g_sp, x, y0, w, h);
	int pad = 14, c = ctl_h (), lw = imin (190, w / 3), bx = x + pad + lw + 14, cw = w - 2 * pad - lw - 14, y = y0 + 8, by = y0 + h - c - 10, fx = bx + 120, fw = cw - 120;
	ui_w_move (g_stList, x + pad, y0 + 10, lw, h - 2 * c - 34);
	ui_w_move (g_stAdd, x + pad, by, lw, c);
	ui_w_move (g_stClose, x + w - pad - 92, by, 92, c);
	ui_w_move (g_stDel, x + w - pad - 92 - 108, by, 100, c);
	ui_w_move (g_stTitle, bx, y, cw, 26); y += 32;
	long *a[3] = { &g_stName, &g_stLabel, &g_stEvery };
	for (int i = 0; i < 3; i++, y += 34) { ui_w_move (g_stL[i], bx, y, 116, c); ui_w_move (*a[i], fx, y, fw, c); }
	ui_w_move (g_stL[3], bx, y, cw, 20); y += 22;
	int sh = imax (40, by - y - 34 - 26 - 40 - 8);
	ui_w_move (g_stSig, bx, y, cw, sh); y += sh + 6;
	ui_w_move (g_stL[4], bx, y, 116, c); ui_w_move (g_stPw, fx, y, fw, c); y += 34;
	ui_w_move (g_stVerify, bx, y, cw, 24); y += 26;
	ui_w_move (g_stSrv, bx, y, cw, 36);
}
static void st_save ()
{
	if (s_stCur >= g_m.accts.n) return;
	Account &a = g_m.accts.a[s_stCur];
	scpy (a.name, tx (g_stName), sizeof a.name);
	if (tx (g_stLabel)[0]) scpy (a.label, tx (g_stLabel), sizeof a.label);
	long e = ui_w_get_value (g_stEvery); a.checkMinutes = EVM[e >= 0 && e < 6 ? e : 2];
	scpy (a.signature, tx (g_stSig), sizeof a.signature);
	bool wasV = a.verify; a.verify = ui_w_get_value (g_stVerify) != 0;
	if (a.auth == AU_PASSWORD && tx (g_stPw)[0]) { scpy (a.inSecret, tx (g_stPw), sizeof a.inSecret); set_text (g_stPw, ""); g_m.accts.save_secrets (); }
	g_m.accts.save ();
	if (wasV != a.verify) g_m.worker.drop_session (a.id);
}
// a folder and what it holds (an account's store: a later account may get its id)
static void rm_tree (const char *path)
{
	DIR *d = opendir (path); if (!d) return;
	struct dirent *e;
	while ((e = readdir (d)))
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, "..")) continue;
		char p[300]; snprintf (p, sizeof p, "%s/%s", path, e->d_name);
		struct stat st;
		if (stat (p, &st) == 0 && S_ISDIR (st.st_mode)) rm_tree (p); else unlink (p);
	}
	closedir (d);
	rmdir (path);
}
static void open_settings ()
{
	if (g_page == P_COMPOSE) return;
	s_stCur = 0;
	show_page (P_SETTINGS);
	st_fill ();
	layout ();
}
static bool st_control (long w, long code)
{
	if (w == g_stClose && code == UN_ACTIVATE) { st_save (); c_accounts_changed (); show_page (P_MAIL); select_view (SEL_UNIFIED); return true; }
	if (w == g_stAdd && code == UN_ACTIVATE) { st_save (); open_wizard (); return true; }
	if (w == g_stList && code == UN_CHANGE) { long v = ui_w_get_value (w); if (v >= 0 && v < g_m.accts.n && v != s_stCur) { st_save (); s_stCur = (int) v; st_fill (); layout (); } return true; }
	if (w == g_stDel && code == UN_ACTIVATE && s_stCur < g_m.accts.n)
	{
		char q[300]; snprintf (q, sizeof q, TR ("Remove %s from Mail? Its messages stay on the server; Mail's copy on this card goes."), g_m.accts.a[s_stCur].email);
		if (ask (q, UM_YESNO) != 1) return true;
		g_conv = -1; rd_clear ();
		rm_tree (g_m.stores[s_stCur]->root);
		delete g_m.stores[s_stCur];
		for (int i = s_stCur; i < g_m.accts.n - 1; i++) { g_m.accts.a[i] = g_m.accts.a[i + 1]; g_m.stores[i] = g_m.stores[i + 1]; g_m.stores[i]->acct = &g_m.accts.a[i]; }
		g_m.accts.n--; g_m.stores[g_m.accts.n] = 0;
		g_m.accts.save (); g_m.accts.save_secrets ();
		g_m.sel.kind = SEL_UNIFIED; g_m.build ();
		if (s_stCur >= g_m.accts.n) s_stCur = imax (0, g_m.accts.n - 1);
		st_fill (); layout ();
		return true;
	}
	return false;
}

} // namespace mailapp

#endif
