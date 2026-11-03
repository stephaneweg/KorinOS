//
// mail -- Onyx's mail client on ARMPI's UIKit (README.md). Gmail (an app password), Outlook.com / Hotmail (Microsoft's
// sign-in by a code), any IMAP account, POP3 + SMTP. Three columns:
//   * at the left the unified inbox (every account's Inbox), the starred messages, then each account and its folders
//     (Inbox, Sent, Drafts, Archive, Junk, Trash and its own, the unread counted); Contacts; Accounts and settings;
//     what the worker is doing;
//   * in the middle the conversations (grouped as Gmail does -- View > Conversations --, or one message a row), by
//     day: who, the subject, the text's start, when, how many, a paper clip, a star, the account's stripe; All /
//     Unread; the search (who, subject, text, every account);
//   * at the right the conversation (read.h), or what is being written (compose.h), or the contacts (contactsui.h).
// A narrow window: one pane at a time, the folders behind the bar's button. New mail is looked for every few minutes
// (each account's setting) and told by a notification. The messages, the folders, the accounts live on the card
// (store.h, accounts.h; the passwords and tokens encrypted); the servers are talked to by a worker thread (sync.h)
// while the window stays live. "mail SD0:/x.eml" shows a message file; "mail --attach <list>" a new message with files,
// "mail --compose <draft>" one filled from a draft -- To:, Cc:, Subject:, a blank line, the text.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#include "contactsui.h"

using namespace mailapp;

namespace mailapp {

static html::UiHost s_host;
static char g_note[300]; static unsigned g_noteT;
static int  s_certAcct = -1;			// the account whose certificate was refused (the foot's error offers to go on)
static long g_mainWin;
static void status_note (const char *s) { scpy (g_note, s, sizeof g_note); g_noteT = clock_ms (); g_footDirty = true; }

// ---- a notification on the desktop: notifyd's service, as src/inc/notify.inc --------------------------------------------------------
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
	if (ch < 0) return;					// (no notifyd: the title and the badge say it)
	char m[512]; memset (m, 0, sizeof m);
	*(long *) m = 1;					// op 1: title, 0, text, 0
	int n = snprintf (m + 48, 460, "%s", title);
	if (n > 0 && n < 440) snprintf (m + 48 + n + 1, (size_t) (460 - n - 1), "%s", msg);
	sys_call (0x32 /* SYS_SVC_CALL */, ch, (long) m);
}

// ---- the tool bar ------------------------------------------------------------------------------------------------------------
static long g_tb, g_tSide, g_tBack, g_bNew, g_tReply, g_tAll, g_tFwd, g_tArch, g_tDel, g_tJunk, g_tStar, g_tCheck, g_search;
static unsigned s_qT; static char s_lastQ[200];
static void tb_create ()
{
	g_tb = ui_toolbar (0, 0, 10, TB_H);
	auto tool = [] (const char *g, const char *tip, int flags = 0) { long t = ui_toolbar_add (g_tb, (long) g, 0, flags); ui_w_tip (t, (long) tip); return t; };
	g_tSide = tool ("\xE2\x89\xA1", TR ("Folders"));
	g_tBack = tool ("\xE2\x86\x90", TR ("Back to the list"));
	g_tReply = tool ("\xE2\x86\xA9", TR ("Reply (Ctrl+R)"));
	g_tAll = tool ("\xE2\x87\x87", TR ("Reply all"));
	g_tFwd = tool ("\xE2\x86\xAA", TR ("Forward (Ctrl+L)"));
	g_tArch = tool ("\xE2\x96\xA4", TR ("Archive"));
	g_tDel = tool ("\xE2\x9C\x95", TR ("Delete (Del)"));
	g_tJunk = tool ("\xE2\x8A\x98", TR ("Junk"));
	g_tStar = tool ("\xE2\x98\x85", TR ("Star (Ctrl+S)"), UTB_TOGGLE);
	g_tCheck = tool ("\xE2\x86\xBB", TR ("Check for new mail (F5)"));
	g_bNew = ui_pushbutton (0, 0, 120, 0, (long) TR ("New message"), UB_DEFAULT);
	ui_w_set_parent (g_bNew, g_tb);
	g_search = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_textbox_hint (g_search, (long) TR ("Search the mail"));
	ui_w_set_range (g_search, 0, 190, 0, 0);
	ui_w_set_parent (g_search, g_tb);
}
static void tb_sync ()
{
	static int last = -1, lastStar = -1;
	bool have = g_conv >= 0 && g_page == P_MAIL;
	if ((int) have != last)
	{
		last = have;
		for (long t : { g_tReply, g_tAll, g_tFwd, g_tArch, g_tDel, g_tJunk, g_tStar }) ui_w_enable (t, have);
	}
	int st = 0;
	if (have) { Ref r[60]; int n = conv_refs (r, 60); for (int k = 0; k < n; k++) if (g_m.msg (r[k]).flags & F_FLAGGED) st = 1; }
	if (st != lastStar) { lastStar = st; ui_w_set_value (g_tStar, st); }
}

// ---- the left column: UIKit's side list; a row's icon, its depth, its count drawn by side_cell ------------------------------------
static bool g_open[12] = { true, true, true, true, true, true, true, true, true, true, true, true };	// an account's folders shown
enum { S_UNIFIED = 1, S_STARRED, S_ADD, S_ACCOUNT = 100, S_FOLDER = 1000, S_PER_ACCT = 400, SI_ACCOUNT = 2000 };
static inline int folder_id (int a, int f) { return S_FOLDER + a * S_PER_ACCT + f; }
struct SideRow { int id, ic, depth, badge; char label[80]; };
static SideRow s_rows[400]; static int s_nrows;
static unsigned g_sideSig = ~0u;			// (what the items were made for)
static long g_side, g_foot, g_split;
static int  s_sideW;

static int folder_count (const Folder &F) { return F.special == SP_DRAFTS ? F.msgs.n : (F.special == SP_SENT || F.special == SP_TRASH || F.special == SP_ALL) ? 0 : F.unread; }
static void side_add (int id, const char *label, int ic, int depth = 0, int badge = 0)
{
	if (s_nrows >= 400) return;
	SideRow &r = s_rows[s_nrows++]; r.id = id; r.ic = ic; r.depth = depth; r.badge = badge;
	scpy (r.label, label, sizeof r.label);
	ui_list_add (g_side, (long) (label[0] == '#' ? label + 1 : label), id);
}
static void side_select ()
{
	const Selection &s = g_m.sel;
	int id = g_page == P_CONTACTS ? -1 : s.kind == SEL_UNIFIED ? S_UNIFIED : s.kind == SEL_STARRED ? S_STARRED : s.kind == SEL_FOLDER ? folder_id (s.acct, s.folder) : -1;
	int row = -1;
	for (int i = 0; i < s_nrows; i++) if (s_rows[i].id == id) row = i;
	if (ui_w_get_value (g_side) != row) ui_w_set_value (g_side, row);
}
static void side_build ()			// the items: made again when the accounts, the folders or a count change
{
	unsigned sig = 2166136261u;
	auto mx = [&] (unsigned v) { sig = (sig ^ v) * 16777619u; };
	auto mxs = [&] (const char *c) { for (; *c; c++) mx ((unsigned char) *c); mx (0); };
	mx ((unsigned) g_m.unread_inboxes ());
	for (int a = 0; a < g_m.accts.n; a++)
	{
		mxs (g_m.accts.a[a].label); mx (g_open[a] ? 1 : 0); mx (g_m.accts.a[a].colour);
		Store &st = *g_m.stores[a];
		for (int f = 0; f < st.folders.n; f++) { Folder &F = st.folders[f]; mxs (F.name); mx (F.noselect ? 1 : 0); mx ((unsigned) folder_count (F)); }
	}
	if (sig == g_sideSig) { side_select (); return; }
	g_sideSig = sig;
	ui_list_clear (g_side); s_nrows = 0;
	side_add (S_UNIFIED, TR ("All inboxes"), I_UNIFIED, 0, g_m.unread_inboxes ());
	side_add (S_STARRED, TR ("Starred"), I_STAR);
	for (int a = 0; a < g_m.accts.n && a < 12; a++)
	{
		Account &A = g_m.accts.a[a];
		side_add (S_ACCOUNT + a, A.label, SI_ACCOUNT + a);
		if (!g_open[a]) continue;
		Store &st = *g_m.stores[a];
		for (int f = 0; f < st.folders.n && f < S_PER_ACCT; f++)
		{
			Folder &F = st.folders[f];
			if (F.noselect) continue;
			int depth = 0; if (F.delim) for (const char *p = F.name; *p; p++) if (*p == F.delim) depth++;
			// ([Gmail]/x: the [Gmail] level not counted)
			if (A.provider == PV_GMAIL && !strncmp (F.name, "[Gmail]", 7)) depth--;
			depth = imax (0, imin (depth, 3));
			Buf nm; mutf7_decode (nm, folder_label (F));
			side_add (folder_id (a, f), folder_tr (nm.c ()), folder_icon (F), 1 + depth, imin (folder_count (F), 999));
		}
	}
	if (!g_m.accts.n) side_add (S_ADD, TR ("Add an account..."), I_PLUS);
	side_select ();
}
static long side_cell (long, long row, long, long x0, long y0, long w0, long h0, long sel)
{
	if (row < 0 || row >= s_nrows) return 0;
	const SideRow &r = s_rows[row];
	int x = (int) x0 + 6 + r.depth * 12, y = (int) y0, w = (int) w0, h = (int) h0;
	unsigned bg = sel ? C_ACCENT : 0;
	unsigned fg = sel ? ink_on (bg) : C_TEXT;
	if (r.ic >= SI_ACCOUNT)
	{
		int a = r.ic - SI_ACCOUNT; if (a >= g_m.accts.n) return 1;
		Account &A = g_m.accts.a[a];
		unsigned c = A.colour & 0xFFFFFF;
		rbox (x, y + (h - 18) / 2, 18, 18, 4, c);
		char ini[8]; initials (A.label, ini); ini[(unsigned char) ini[0] >= 0xC0 ? 2 : 1] = 0;
		text_c (x, y + (h - 18) / 2, 18, 18, ini, ink_on (c), F_SMALL, 1);
		text_v (x + 24, y, h, r.label, fg, F_UI, 1, (int) x0 + w - x - 24 - 18);
		text_v ((int) x0 + w - 16, y, h, g_open[a] ? "\xE2\x96\xBE" : "\xE2\x96\xB8", sel ? fg : C_FAINT, F_SMALL);
		return 1;
	}
	unsigned ic = sel ? fg : r.ic == I_STAR ? C_STAR : r.ic == I_PLUS ? col_accent_text () : C_DIM;
	icon (r.ic, x, y + (h - 16) / 2, 16, ic, sel ? bg : C_FIELD);
	int right = (int) x0 + w - 6;
	if (r.badge)
	{
		char t[12]; snprintf (t, sizeof t, "%d", r.badge);
		int bw = imax (18, tw (t, F_SMALL, 1) + 10);
		unsigned b = sel ? mix (bg, fg, 60) : jet (J_LIGNE_FORTE);
		rbox (right - bw, y + (h - 16) / 2, bw, 16, 8, b);
		text_c (right - bw, y + (h - 16) / 2, bw, 16, t, sel ? fg : C_TEXT, F_SMALL, 1);
		right -= bw + 4;
	}
	text_v (x + 22, y, h, r.label, fg, F_UI, r.badge ? 1 : 0, right - x - 22);
	return 1;
}
static void side_chosen (int row)
{
	if (row < 0 || row >= s_nrows) return;
	int id = s_rows[row].id;
	if (g_page != P_MAIL && g_page != P_CONTACTS && id != S_ADD) { side_select (); return; }	// (a page open: it is left first)
	if (id >= S_FOLDER) { show_contacts (false); select_view (SEL_FOLDER, (id - S_FOLDER) / S_PER_ACCT, (id - S_FOLDER) % S_PER_ACCT); }
	else if (id >= S_ACCOUNT) { int a = id - S_ACCOUNT; g_open[a] = !g_open[a]; g_sideDirty = true; return; }	// (its folders folded, shown)
	else if (id == S_UNIFIED) { show_contacts (false); select_view (SEL_UNIFIED); }
	else if (id == S_STARRED) { show_contacts (false); select_view (SEL_STARRED); }
	else if (id == S_ADD) { side_select (); open_wizard (); return; }
	if (g_onePane && g_sideWant) { g_sideWant = false; layout (); }
}

// the foot of the column: the contacts, the settings, what is going on
#define FOOT_H	100
enum { FT_CONTACTS = 1, FT_SETTINGS, FT_ERROR };
static HitList s_ftHits; static int s_ftHot = -1;
static void foot_row (long w, int y, int kind, int ic, const char *label, bool selected)
{
	int x = 8, rw = (int) w - 16;
	unsigned bg = jet (J_CREUX);
	if (selected) { bg = C_ACCENT; rbox (x, y, rw, 28, 6, bg); }
	else if (s_ftHot == kind) { bg = mix (bg, C_TEXT, 20); rbox (x, y, rw, 28, 6, bg); }
	icon (ic, x + 6, y + 6, 16, selected ? ink_on (bg) : C_DIM, bg);
	text_v (x + 28, y, 28, label, selected ? ink_on (bg) : C_TEXT, F_UI, selected ? 1 : 0, rw - 34);
	s_ftHits.add (0, y, (int) w, 28, kind);
}
static void foot_paint (long, long w, long h)
{
	unsigned bg = jet (J_CREUX);
	ui_fill (0, 0, w, h, bg);
	s_ftHits.clear ();
	foot_row (w, 4, FT_CONTACTS, I_PERSON, TR ("Contacts"), g_page == P_CONTACTS);
	foot_row (w, 34, FT_SETTINGS, I_GEAR, TR ("Accounts and settings"), g_page == P_SETTINGS);
	bool noteOn = g_note[0] && clock_ms () - g_noteT < 8000;
	const char *st = g_m.worker.current[0] ? g_m.worker.current : noteOn ? g_note : g_m.lastError;
	bool isErr = !g_m.worker.current[0] && !noteOn && g_m.lastError[0];
	if (st && st[0])
	{
		int sy = 68;
		if (isErr) icon (I_WARN, 10, sy + 1, 14, jet (J_DANGER), bg);
		// (a long text over two lines: the first one cut, the rest under it)
		int tx0 = isErr ? 28 : 12, wd = (int) w - tx0 - 6;
		unsigned ink = isErr ? col_danger_text () : C_DIM;
		ui_text_wrap ((long) st, tx0, sy - 3, wd, ink, face (0), FACE_PX[F_SMALL], 0);
		if (isErr) s_ftHits.add (0, sy - 5, (int) w, 34, FT_ERROR);
	}
}
static long foot_mouse (long, long kind, long x, long y, long info)
{
	const Hit *h = s_ftHits.at ((int) x, (int) y);
	int nh = kind == 3 || !h ? -1 : h->kind;
	if (nh != s_ftHot) { s_ftHot = nh; ui_view_dirty (g_foot); }
	if (kind == 1 && (info >> 8 & 1) && h) later (L_FOOT, *h);
	return 0;
}
static void foot_act ()
{
	switch (g_later.h.kind)
	{
	case FT_CONTACTS: show_contacts (g_page != P_CONTACTS); break;
	case FT_SETTINGS: if (g_page == P_SETTINGS) { st_save (); show_page (P_MAIL); } else open_settings (); break;
	case FT_ERROR:
		if (s_certAcct >= 0 && s_certAcct < g_m.accts.n)
		{
			char q[500]; snprintf (q, sizeof q, "%s\n\n%s", g_m.lastError, TR ("Go on anyway, without checking this server's certificate?"));
			if (ask (q, UM_YESNO) == 1)
			{
				g_m.accts.a[s_certAcct].verify = false; g_m.accts.save ();
				g_m.worker.drop_session (g_m.accts.a[s_certAcct].id);
				g_m.sync (s_certAcct);
			}
			s_certAcct = -1;
		}
		else ask (g_m.lastError);
		g_m.lastError[0] = 0; g_footDirty = true;
		break;
	}
}

// ---- the middle column ------------------------------------------------------------------------------------------------------
static long g_lhead, g_seg, g_list, g_lsBar;
static int  s_segW = 120;
#define ROW_H	66
#define GROUP_H	24
#define LHEAD_H	44
static int  s_lsW, s_lsH, s_lsSy, s_lsContent, s_lsHot = -1, s_lsMax = -1, s_lsVal, s_lsVis;
static html::Vec<int> s_rowY;				// a conversation's top (content coordinates; a group's title above it when it opens one)

static const char *ls_title ()
{
	static char t[200];
	switch (g_m.sel.kind)
	{
	case SEL_UNIFIED: return TR ("All inboxes");
	case SEL_STARRED: return TR ("Starred");
	case SEL_SEARCH: snprintf (t, sizeof t, "\xE2\x80\x9C%s\xE2\x80\x9D", g_m.search); return t;
	default:
	{
		if (g_m.sel.acct >= g_m.accts.n) return "";
		Store &s = *g_m.stores[g_m.sel.acct];
		if (g_m.sel.folder >= s.folders.n) return "";
		Buf b; mutf7_decode (b, folder_label (s.folders[g_m.sel.folder]));
		scpy (t, folder_tr (b.c ()), sizeof t); return t;
	}
	}
}
static void lhead_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FIELD);
	ui_fill (0, h - 1, w, 1, C_LINE);
	text_v (12, 0, (int) h, ls_title (), C_TEXT, F_H2, 1, (int) w - s_segW - 28);
}
static void ls_bar_sync ()
{
	if (s_lsMax != s_lsContent || s_lsVis != s_lsH) { s_lsMax = s_lsContent; s_lsVis = s_lsH; ui_w_set_range (g_lsBar, 0, s_lsContent, s_lsH, 60); }
	ui_w_show (g_lsBar, s_lsContent > s_lsH);
	if (s_lsVal != s_lsSy) { s_lsVal = s_lsSy; ui_w_set_value (g_lsBar, s_lsSy); }
}
static void ls_sync ()
{
	View &v = g_m.view;
	s_rowY.clear ();
	int y = 0, lastG = -1;
	for (int i = 0; i < v.convs.n; i++)
	{
		int g = day_group (v.convs[i].date);
		if (g != lastG) { lastG = g; y += GROUP_H; }
		s_rowY.push (y);
		y += ROW_H + 2;
	}
	s_lsContent = y + 8;
	int mx = s_lsContent - s_lsH; if (s_lsSy > (mx > 0 ? mx : 0)) s_lsSy = mx > 0 ? mx : 0;
	ls_bar_sync ();
	ui_view_dirty (g_list); ui_view_dirty (g_lhead);
}
static void ls_scroll (int v)
{
	int mx = s_lsContent - s_lsH;
	if (v > mx) v = mx; if (v < 0) v = 0;
	if (v != s_lsSy) { s_lsSy = v; ui_view_dirty (g_list); ls_bar_sync (); }
}
static void ls_reveal (int i)
{
	if (i < 0 || i >= s_rowY.n) return;
	int y = s_rowY[i];
	if (y - GROUP_H < s_lsSy) ls_scroll (y - GROUP_H);
	else if (y + ROW_H > s_lsSy + s_lsH) ls_scroll (y + ROW_H - s_lsH + 6);
}
static void draw_row (int i, int y, int W)
{
	const Conv &c = g_m.view.convs[i];
	const Ref &last = g_m.view.refs[c.first + c.n - 1];
	const Msg &m = g_m.msg (last);
	bool sel = i == g_conv && g_page == P_MAIL;
	int x = 4;
	if (sel) rbox (x, y, W - 4, ROW_H, 8, col_sel ());
	else if (s_lsHot == i) rbox (x, y, W - 4, ROW_H, 8, col_hover ());
	// the account's stripe
	ui_fill (W - 4, y + 10, 3, ROW_H - 20, g_m.accts.a[last.acct].colour & 0xFFFFFF);
	// the unread dot
	if (c.unread) icon (I_DOT, x, y + 25, 10, C_ACCENT);
	// who: in a sent folder, to whom
	const Folder &F = g_m.stores[last.acct]->folders[last.folder];
	bool sent = F.special == SP_SENT || F.special == SP_DRAFTS;
	char name[200];
	const Msg &first = g_m.msg (g_m.view.refs[c.first]);
	who (sent ? m.to : first.from, name, sizeof name);
	char plain[200]; scpy (plain, name, sizeof plain);
	if (sent) { char t[220]; snprintf (t, sizeof t, TR ("To: %s"), name[0] ? name : TR ("(nobody)")); scpy (name, t, sizeof name); }
	char em[160]; first_email (sent ? m.to : first.from, em, sizeof em);
	avatar (x + 28, y + ROW_H / 2, 17, plain[0] ? plain : "?", em);
	int tx0 = x + 54, tw0 = W - tx0 - 10;
	char date[40]; fmt_short (c.date, date, sizeof date);
	int dw = tw (date, F_SMALL);
	// the name, the count
	char cnt[12] = ""; if (c.n > 1) snprintf (cnt, sizeof cnt, "%d", c.n);
	int cntW = cnt[0] ? tw (cnt, F_SMALL, 1) + 12 : 0;
	int nameMax = tw0 - dw - 10 - cntW;
	text (tx0, y + 6, name, C_TEXT, F_UI, c.unread ? 1 : 0, nameMax);
	if (cnt[0])
	{
		int nw = imin (tw (name, F_UI, c.unread ? 1 : 0), nameMax);
		rbox (tx0 + nw + 6, y + 7, cntW, 16, 8, C_LINE);
		text_c (tx0 + nw + 6, y + 7, cntW, 16, cnt, C_DIM, F_SMALL, 1);
	}
	text (tx0 + tw0 - dw, y + 8, date, c.unread ? col_accent_text () : C_DIM, F_SMALL, c.unread ? 1 : 0);
	// the subject, the paper clip, the star
	int iconsW = (c.attach ? 18 : 0) + (c.flagged ? 18 : 0);
	text (tx0, y + 25, m.subject && m.subject[0] ? m.subject : TR ("(no subject)"), C_TEXT, F_UI, c.unread ? 1 : 0, tw0 - iconsW - 4);
	int ix = tx0 + tw0 - iconsW;
	if (c.attach) { icon (I_CLIP, ix, y + 25, 16, C_DIM); ix += 18; }
	if (c.flagged) icon (I_STAR, ix, y + 24, 16, C_STAR);
	// the text's start
	text (tx0, y + 45, m.preview ? m.preview : "", C_DIM, F_SMALL, 0, tw0);
}
static void ls_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FIELD);
	ui_fill (w - 1, 0, 1, h, C_LINE);
	int W = (int) w - 10;
	View &v = g_m.view;
	const char *GROUPS[5] = { TR ("TODAY"), TR ("YESTERDAY"), TR ("THIS WEEK"), TR ("THIS MONTH"), TR ("OLDER") };
	int lastG = -1;
	for (int i = 0; i < v.convs.n && i < s_rowY.n; i++)
	{
		int g = day_group (v.convs[i].date), y = s_rowY[i] - s_lsSy;
		if (g != lastG) { lastG = g; if (y > -4 && y - GROUP_H < h) text (12, y - GROUP_H + 6, GROUPS[g], C_FAINT, F_TINY, 1); }
		if (y + ROW_H > 0 && y < h) draw_row (i, y, W);
	}
	if (!v.convs.n)
	{
		const char *t = g_m.accts.n == 0 ? TR ("No account yet.") : g_m.search[0] ? TR ("Nothing found.") : g_m.unreadOnly ? TR ("Nothing unread.") : g_m.worker.busy ? TR ("Getting the mail...") : TR ("No messages.");
		text_c (0, 50, W, 24, t, C_DIM, F_MID);
	}
}
static int ls_at (int my)
{
	int y = my + s_lsSy;
	for (int i = 0; i < s_rowY.n; i++) if (y >= s_rowY[i] && y < s_rowY[i] + ROW_H) return i;
	return -1;
}
static long ls_mouse (long, long kind, long, long y, long info)
{
	int i = kind == 3 ? -1 : ls_at ((int) y);
	if (i != s_lsHot) { s_lsHot = i; ui_view_dirty (g_list); }
	if (kind == 1 && i >= 0)
	{
		int b = (int) (info >> 8 & 255);
		Hit h; memset (&h, 0, sizeof h); h.a = i;
		if (b & 3) later (L_LIST, h, (b & 2) != 0);
	}
	return 0;
}
static long ls_wheel (long, long steps) { ls_scroll (s_lsSy + (int) steps * 60); return 0; }
static long ls_key (long, long hid, long, long)
{
	int n = g_m.view.convs.n;
	if (hid == KEY_DOWN && g_conv < n - 1) { open_conv (g_conv + 1); ls_reveal (g_conv); return 0; }
	if (hid == KEY_UP && g_conv > 0) { open_conv (g_conv - 1); ls_reveal (g_conv); return 0; }
	if (hid == KEY_PGDN) { ls_scroll (s_lsSy + s_lsH - 40); return 0; }
	if (hid == KEY_PGUP) { ls_scroll (s_lsSy - s_lsH + 40); return 0; }
	if (hid == KEY_DELETE) { act_delete (); return 0; }
	return -1;
}
static void row_menu (int mx, int my)
{
	Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
	bool unread = false, starred = false; for (int i = 0; i < n; i++) { if (!(g_m.msg (r[i]).flags & F_SEEN)) unread = true; if (g_m.msg (r[i]).flags & F_FLAGGED) starred = true; }
	long c = popup (mx, my, { TR ("Reply"), TR ("Forward"), "-", unread ? TR ("Mark as read") : TR ("Mark as unread"), starred ? TR ("Remove the star") : TR ("Star"), "-", TR ("Archive"), TR ("Junk"), TR ("Delete"), "-", TR ("Move to...") }, 180);
	switch (c)
	{
	case 0: compose_new (1, &r[n - 1]); break;
	case 1: compose_new (3, &r[n - 1]); break;
	case 3: g_m.set_flag (r, n, F_SEEN, unread); g_m.build (); refresh_all (); break;
	case 4: act_star (); break;
	case 6: act_archive (); break;
	case 7: act_junk (); break;
	case 8: act_delete (); break;
	case 10:
	{
		// the account's folders
		Store &s = *g_m.stores[r[0].acct];
		char t[2400]; int k = 0, used = 0; int ids[40];
		for (int f = 0; f < s.folders.n && k < 30; f++)
		{
			if (s.folders[f].noselect || f == r[0].folder) continue;
			Buf b; mutf7_decode (b, folder_label (s.folders[f]));
			const char *l = folder_tr (b.c ()); int ln = imin ((int) strlen (l), 70);
			memcpy (t + used, l, (size_t) ln); t[used + ln] = 0; used += ln + 1;
			ids[k++] = f;
		}
		t[used] = 0;
		long ch = popup_list (mx, my, t);
		if (ch >= 0 && ch < k) { g_m.move (r, n, -1, ids[ch]); g_conv = -1; rd_clear (); select_view (g_m.sel.kind, g_m.sel.acct, g_m.sel.folder); }
		break;
	}
	}
}

// ---- no account yet ---------------------------------------------------------------------------------------------------------------
static long g_welcome, g_wAdd;
static void welcome_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FIELD);
	int cy = (int) h / 2 - 110;
	icon (I_SEND, (int) w / 2 - 30, cy, 60, C_ACCENT);
	text_c (0, cy + 74, (int) w, 34, TR ("Welcome to Mail"), C_TEXT, F_H1, 1);
	text_c (0, cy + 112, (int) w, 22, TR ("Gmail, Outlook.com, iCloud, Yahoo, any IMAP or POP3 account."), C_DIM, F_UI);
}

// ---- the window's parts laid out -----------------------------------------------------------------------------------------------
static void repaint ()
{
	ui_fill (0, 0, ui.w, ui.h, C_FACE);
	if (s_sideW && s_sideW < ui.w) ui_fill (0, 0, s_sideW, TB_H, jet (J_CREUX));	// (the panel goes up to the top)
	for (long v : { g_foot, g_lhead, g_list, g_read, g_welcome, g_cChips, g_wCode, g_wErr, g_kCard }) ui_view_dirty (v);
	ui_w_draw (0);
	ui_update_all ();
}
static void tb_place (int W, bool narrow)
{
	int c = ctl_h (), by = (TB_H - c) / 2, x = 8;
	bool back = g_onePane && g_conv >= 0 && g_page == P_MAIL;
	ui_w_show (g_tSide, narrow); ui_w_show (g_tBack, back);
	if (narrow) { ui_w_move (g_tSide, x, by, 32, c); x += 34; }
	if (back) { ui_w_move (g_tBack, x, by, 32, c); x += 34; }
	int nw = W < 560 ? 40 : tw (TR ("New message"), F_UI, 1) + 24;
	set_text (g_bNew, W < 560 ? "\xE2\x9C\x8E" : TR ("New message"));
	if (narrow) { ui_w_move (g_bNew, x, by, nw, c); x += nw + 10; }
	else { ui_w_move (g_bNew, 8, by, s_sideW - 16, c); x = s_sideW + 8; }	// (in the panel)
	int sw = W >= 1000 ? 240 : W >= 760 ? 170 : W >= 620 ? 120 : 0, rx = W - 112 - 32;	// (left of the window's buttons)
	bool list = !sw && g_conv < 0 && g_page == P_MAIL;		// (very narrow, the list: the search in the tools' place)
	if (list) sw = rx - 6 - x;
	ui_w_move (g_tCheck, rx, by, 32, c);
	ui_w_show (g_search, sw > 0);
	if (sw) ui_w_move (g_search, rx - 6 - sw, by, sw, c);
	int room = rx - 6 - sw - 6;
	long T[7] = { g_tReply, g_tAll, g_tFwd, g_tArch, g_tDel, g_tJunk, g_tStar };
	for (int i = 0; i < 7; i++)
	{
		if (i == 3) x += 10;
		bool fit = x + 32 <= room && !list;
		ui_w_show (T[i], fit);
		if (fit) ui_w_move (T[i], x, by, 32, c);
		x += 34;
	}
}
static void layout ()
{
	int W = g_w, H = g_h;
	if (!W) return;
	bool narrow = W < 640;
	int side = W >= 1000 ? 206 : W >= 860 ? 186 : 156, list = W >= 1300 ? 360 : W >= 860 ? 290 : 226;
	g_onePane = narrow;
	if (!narrow) g_sideWant = false;
	bool sideShown = !narrow || g_sideWant;
	if (narrow) side = g_sideWant ? W : 0;
	s_sideW = side;
	tb_place (W, narrow);
	int area = H - TB_H;
	ui_w_show (g_side, sideShown); ui_w_show (g_foot, sideShown);
	if (sideShown)
	{
		ui_w_move (g_side, 0, TB_H, side, area - FOOT_H);
		ui_w_move (g_foot, 0, H - FOOT_H, side, FOOT_H);
	}
	bool main = !(narrow && g_sideWant);
	int p = g_page == P_MAIL && !g_m.accts.n ? P_WELCOME : g_page, x = narrow ? 0 : side, cw = W - x;
	if (!main) p = -1;
	bool lst = p == P_MAIL && (!g_onePane || g_conv < 0), rdg = p == P_MAIL && (!g_onePane || g_conv >= 0);
	ui_w_show (g_lhead, lst); ui_w_show (g_list, lst); ui_w_show (g_read, rdg); ui_w_show (g_rdBand, rdg && s_nsh > 0);
	ui_w_show (g_cp, p == P_COMPOSE); ui_w_show (g_kp, p == P_CONTACTS); ui_w_show (g_wp, p == P_WIZARD); ui_w_show (g_sp, p == P_SETTINGS);
	ui_w_show (g_welcome, p == P_WELCOME);
	if (lst)
	{
		int lw = g_onePane ? cw : list;
		ui_w_move (g_lhead, x, TB_H, lw, LHEAD_H);
		s_segW = tw (TR ("All"), F_UI, 1) + tw (TR ("Unread"), F_UI, 1) + 44;
		ui_w_move (g_seg, x + lw - s_segW - 8, TB_H + (LHEAD_H - 24) / 2, s_segW, 24);
		s_lsW = lw; s_lsH = area - LHEAD_H;
		ui_w_move (g_list, x, TB_H + LHEAD_H, lw, s_lsH);
		ui_w_move (g_lsBar, x + lw - 11, TB_H + LHEAD_H, 10, s_lsH);
		g_listDirty = true;
	}
	if (rdg) { int off = g_onePane ? 0 : list; rd_move (x + off, TB_H, cw - off, area); }
	if (p == P_COMPOSE) c_place (x, TB_H, cw, area);
	if (p == P_CONTACTS) k_place (x, TB_H, cw, area);
	if (p == P_WIZARD) wz_place (x, TB_H, cw, area);
	if (p == P_SETTINGS) st_place (x, TB_H, cw, area);
	if (p == P_WELCOME)
	{
		ui_w_move (g_welcome, x, TB_H, cw, area);
		ui_w_move (g_wAdd, x + (cw - 170) / 2, TB_H + area / 2 + 36, 170, ctl_h () + 6);
	}
	g_sideDirty = g_toolDirty = g_footDirty = true;
	repaint ();
}
static void on_resize (long w, long h) { g_w = (int) w; g_h = (int) h; layout (); }
static void show_page (int p)
{
	g_page = p;
	layout ();
}

// ---- what the parts call --------------------------------------------------------------------------------------------------------
static void refresh_all ()
{
	static int was = -1;
	int conv = g_conv >= 0;
	if (g_onePane && was != conv && g_page == P_MAIL) { was = conv; layout (); }
	was = conv;
	g_sideDirty = g_listDirty = g_footDirty = g_toolDirty = true;
	static int lastN = -1;					// (the title: how many unread)
	int n = g_emlMode ? 0 : g_m.unread_inboxes ();
	if (n != lastN)
	{
		lastN = n;
		char t[40] = "Mail"; if (n) snprintf (t, sizeof t, "(%d) Mail", n);
		ui_title ((long) t);
	}
}
static void select_view (int kind, int acct, int folder)
{
	bool same = g_m.sel.kind == kind && g_m.sel.acct == acct && g_m.sel.folder == folder;
	g_m.sel.kind = kind; g_m.sel.acct = acct; g_m.sel.folder = folder;
	if (kind == SEL_FOLDER && acct < g_m.accts.n)
	{
		Folder &f = g_m.stores[acct]->folders[folder];
		bool first = !f.opened;
		f.opened = true;
		g_m.stores[acct]->load_index (f);
		if (first || !same) g_m.sync (acct, false, folder);
	}
	g_m.build ();
	if (!same) { g_conv = -1; rd_clear (); s_lsSy = 0; g_cur = g_m.view.convs.n ? 0 : -1; }
	if (g_cur >= g_m.view.convs.n) g_cur = g_m.view.convs.n - 1;
	if (!same) layout ();
	refresh_all ();
}
// the view rebuilt (a sync came): the conversation shown kept if it is still there
static void rebuild_keep ()
{
	Ref keep; memset (&keep, 0, sizeof keep); bool had = false;
	if (g_conv >= 0 && g_conv < g_m.view.convs.n) { const Conv &c = g_m.view.convs[g_conv]; keep = g_m.view.refs[c.first + c.n - 1]; had = true; }
	long keepUid = had ? g_m.msg (keep).uid : 0; int keepA = keep.acct, keepF = keep.folder;
	g_m.build ();
	int found = -1;
	if (had)
		for (int i = 0; i < g_m.view.convs.n && found < 0; i++)
			for (int k = 0; k < g_m.view.convs[i].n; k++) { const Ref &r = g_m.view.refs[g_m.view.convs[i].first + k]; if (r.acct == keepA && r.folder == keepF && g_m.msg (r).uid == keepUid) { found = i; break; } }
	if (found != g_conv)
	{
		g_conv = found;
		if (found < 0) rd_clear (); else { Ref r[60]; int n = conv_refs (r, 60); rd_load (r, n); }
	}
	else if (found >= 0)
	{	// the refs may have moved (a message deleted above): its Shown kept, its ref renewed
		Ref r[60]; int n = conv_refs (r, 60);
		if (n != s_nsh) rd_load (r, n);
		else for (int i = 0; i < n; i++) s_sh[i].ref = r[i];
	}
}
static void open_conv (int k)
{
	if (k < 0 || k >= g_m.view.convs.n || g_page != P_MAIL) return;
	bool wasNone = g_conv < 0, hadBand = s_nsh > 0;
	g_conv = k; g_cur = k;
	Ref r[60]; int n = conv_refs (r, 60);
	rd_load (r, n);
	// read: its unread messages marked
	Ref un[60]; int nu = 0; for (int i = 0; i < n; i++) if (!(g_m.msg (r[i]).flags & F_SEEN)) un[nu++] = r[i];
	if (nu) { g_m.set_flag (un, nu, F_SEEN, true); g_m.view.convs[k].unread = false; }
	if (!hadBand || (g_onePane && wasNone)) layout ();
	refresh_all ();
}
// the messages an action on the conversation moves: not the replies shown from Sent (unless Sent is the list)
static int action_refs (Ref *out, int max)
{
	Ref r[60]; int n = conv_refs (r, 60), k = 0;
	bool inSent = g_m.sel.kind == SEL_FOLDER && g_m.stores[g_m.sel.acct]->folders[g_m.sel.folder].special == SP_SENT;
	for (int i = 0; i < n && k < max; i++) if (inSent || g_m.stores[r[i].acct]->folders[r[i].folder].special != SP_SENT) out[k++] = r[i];
	return k;
}
static void after_change ()
{
	g_conv = -1; rd_clear ();
	g_m.build ();
	layout ();
	refresh_all ();
}
static void act_archive ()
{
	Ref r[60]; int n = action_refs (r, 60); if (!n) return;
	int keep = g_conv;
	if (!g_m.move (r, n, SP_ARCHIVE)) { status_note (TR ("This account has no Archive folder.")); return; }
	after_change (); status_note (TR ("Archived."));
	if (keep < g_m.view.convs.n && !g_onePane) open_conv (keep);
}
static void act_delete ()
{
	Ref r[60]; int n = action_refs (r, 60); if (!n) return;
	int keep = g_conv;
	g_m.remove (r, n);
	after_change (); status_note (TR ("Deleted."));
	if (keep < g_m.view.convs.n && !g_onePane) open_conv (keep);
}
static void act_junk ()
{
	Ref r[60]; int n = action_refs (r, 60); if (!n) return;
	int keep = g_conv;
	if (!g_m.move (r, n, SP_JUNK)) { status_note (TR ("This account has no Junk folder.")); return; }
	after_change (); status_note (TR ("Moved to Junk."));
	if (keep < g_m.view.convs.n && !g_onePane) open_conv (keep);
}
static void act_star ()
{
	Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
	bool any = false; for (int i = 0; i < n; i++) if (g_m.msg (r[i]).flags & F_FLAGGED) any = true;
	if (any) g_m.set_flag (r, n, F_FLAGGED, false); else g_m.set_flag (&r[n - 1], 1, F_FLAGGED, true);
	g_m.build (); refresh_all (); g_readDirty = true;
}
static void act_unread ()
{
	Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
	g_m.set_flag (&r[n - 1], 1, F_SEEN, false);
	g_m.build (); refresh_all ();
}
static void act_check () { g_m.sync_all (); g_m.newMail = 0; refresh_all (); }
static void show_contacts (bool on)
{
	if (on && g_page == P_COMPOSE) return;
	if (on == (g_page == P_CONTACTS)) return;
	if (on) { g_m.contacts.load (); g_page = P_CONTACTS; k_reload (); ui_w_focus (g_kSearch); }
	else show_page (P_MAIL);
	refresh_all ();
}
static void search_changed ()
{
	const char *q = tx (g_search);
	if (!strcmp (q, g_m.search)) return;
	static Selection before; static bool searching;
	if (q[0] && !searching) { before = g_m.sel; searching = true; }
	scpy (g_m.search, q, sizeof g_m.search);
	if (q[0]) { show_contacts (false); g_m.sel.kind = SEL_SEARCH; }
	else if (searching) { g_m.sel = before; searching = false; }
	g_conv = -1; rd_clear (); s_lsSy = 0;
	g_m.build ();
	g_cur = g_m.view.convs.n ? 0 : -1;
	layout ();
	refresh_all ();
}

// ---- writing -----------------------------------------------------------------------------------------------------------------------
static int default_account ()
{
	if (g_m.sel.kind == SEL_FOLDER && g_m.sel.acct < g_m.accts.n) return g_m.sel.acct;
	return 0;
}
static void compose_open (const char *title)
{
	c_accounts_changed ();
	scpy (C.title, title, sizeof C.title);
	set_text (g_cTitle, title);
	show_page (P_COMPOSE);
	refresh_all ();
}
// the quoted text of a message: its plain text, else its HTML's text
static void quoted_text (const Ref &r, Buf &out, bool forward)
{
	Store &st = *g_m.stores[r.acct]; Folder &f = st.folders[r.folder]; const Msg &m = f.msgs[r.msg];
	int len; char *raw = st.body (f, m.uid, &len);
	Buf t;
	if (raw)
	{
		Mime M; M.parse (raw, len);
		int p = M.body_part (false);
		if (p >= 0)
		{
			if (ieq (M.parts[p].sub, "html")) { Buf h; M.text (M.parts[p], h); html::Html x; x.parse (h.c (), h.n); s_host.pics = 0; x.layout (s_host, 600); x.plain_text (t); }
			else M.text (M.parts[p], t);
		}
		free (raw);
	}
	else t.add (m.preview ? m.preview : "");
	char date[80]; fmt_long (m.date, date, sizeof date);
	char from[300]; who (m.from, from, sizeof from);
	if (forward)
	{
		out.addf ("\n\n---------- %s ----------\n", TR ("Forwarded message"));
		out.addf ("From: %s\nDate: %s\nSubject: %s\nTo: %s\n\n", m.from ? m.from : "", date, m.subject ? m.subject : "", m.to ? m.to : "");
		out.add (t.c ());
		return;
	}
	out.add ("\n\n"); out.addf (TR ("On %s, %s wrote:"), date, from); out.addc ('\n');
	const char *p = t.c ();
	while (*p)
	{
		const char *e = strchr (p, '\n'); int n = e ? (int) (e - p) : (int) strlen (p);
		if (n && p[n - 1] == '\r') { out.add (*p == '>' ? ">" : "> "); out.add (p, n - 1); out.addc ('\n'); }
		else { out.add (n && *p == '>' ? ">" : "> "); out.add (p, n); out.addc ('\n'); }
		if (!e) break;
		p = e + 1;
	}
}
static void add_signature (Buf &b, int acct)
{
	if (acct < g_m.accts.n && g_m.accts.a[acct].signature[0]) { b.add ("\n\n-- \n"); b.add (g_m.accts.a[acct].signature); }
}
static void compose_new (int mode, const Ref *about)
{
	if (g_emlMode) { ask (TR ("This message is a file. To answer it, open Mail with your accounts.")); return; }
	if (!g_m.accts.n) { open_wizard (); return; }
	if (g_page == P_COMPOSE && !c_empty () && ask (TR ("Leave the message being written?"), UM_YESNO) != 1) return;
	if (g_page == P_WIZARD || g_page == P_SETTINGS) return;
	Ref ab; if (about) ab = *about;				// (it may live in what reset clears)
	c_reset ();
	C.mode = mode;
	int acct = about ? ab.acct : default_account ();
	c_accounts_changed (); ui_w_set_value (g_cFrom, acct);
	Buf body;
	if (mode == 0 || !about) { add_signature (body, acct); set_text (g_cBody, body.c ()); compose_open (TR ("New message")); ui_w_focus (g_cTo); return; }
	C.about = ab; C.haveAbout = true;
	const Msg &m = g_m.msg (ab);
	const char *subj = m.subject ? m.subject : "";
	char s[400];
	if (mode == 3) snprintf (s, sizeof s, istarts (subj, "Fwd:") ? "%s" : "Fwd: %s", subj);
	else snprintf (s, sizeof s, istarts (subj, "Re:") ? "%s" : "Re: %s", subj);
	set_text (g_cSubj, s);
	if (mode == 1 || mode == 2)
	{
		Buf to; to.add (m.replyTo && m.replyTo[0] ? m.replyTo : m.from ? m.from : "");
		const char *me = g_m.accts.a[acct].email;
		if (mode == 2)
		{
			Addr a[32]; Buf cc;
			const char *lists[2] = { m.to, m.cc };
			for (int l = 0; l < 2; l++) { int n = parse_addrs (lists[l] ? lists[l] : "", a, 32); for (int i = 0; i < n; i++) { if (ieq (a[i].email, me) || ifind (to.c (), a[i].email)) continue; if (cc.n) cc.add (", "); if (a[i].name[0]) cc.addf ("\"%s\" <%s>", a[i].name, a[i].email); else cc.add (a[i].email); } }
			if (cc.n) { C.ccOn = true; set_text (g_cCc, cc.c ()); }
		}
		// a reply to one's own sent message: to its recipients
		if (ifind (to.c (), me) && m.to) { to.clear (); to.add (m.to); }
		Buf t2; t2.add (to.c ()); if (t2.n) t2.add (", ");
		set_text (g_cTo, t2.c ());
		scpy (C.inReplyTo, m.msgid ? m.msgid : "", sizeof C.inReplyTo);
		Buf refs; if (m.refs && *m.refs) { refs.add (m.refs); refs.addc (' '); } if (m.msgid) refs.add (m.msgid);
		scpy (C.references, refs.c (), sizeof C.references);
	}
	add_signature (body, acct);
	quoted_text (ab, body, mode == 3);
	if (mode == 3)
	{	// the attachments carried over
		Store &st = *g_m.stores[ab.acct]; Folder &f = st.folders[ab.folder];
		int len; char *raw = st.body (f, m.uid, &len);
		if (raw)
		{
			Mime M; M.parse (raw, len); int at[16]; int n = M.attachments (at, 16);
			for (int i = 0; i < n && C.natt < 16; i++)
			{
				Attach &A = C.att[C.natt++];
				scpy (A.name, M.parts[at[i]].name[0] ? M.parts[at[i]].name : "attachment", sizeof A.name);
				snprintf (A.type, sizeof A.type, "%s/%s", M.parts[at[i]].type, M.parts[at[i]].sub);
				Buf b; M.decoded (M.parts[at[i]], b); A.n = b.n; A.data = b.take ();
			}
			free (raw);
		}
	}
	set_text (g_cBody, body.c ());
	compose_open (mode == 3 ? TR ("Forward") : mode == 2 ? TR ("Reply all") : TR ("Reply"));
	ui_textarea_goto (g_cBody, 0);
	ui_w_focus (mode == 3 ? g_cTo : g_cBody);
}
static void compose_to (const char *name, const char *email)
{
	compose_new (0);
	if (g_page != P_COMPOSE) return;
	char t[400]; if (name && *name) snprintf (t, sizeof t, strpbrk (name, ",;<>@\"") ? "\"%s\" <%s>, " : "%s <%s>, ", name, email); else snprintf (t, sizeof t, "%s, ", email);
	set_text (g_cTo, t);
	ui_w_focus (g_cSubj);
}
static void compose_close () { c_reset (); show_page (P_MAIL); refresh_all (); }
static void compose_discard ()
{
	if (!c_empty () && ask (TR ("Throw away this message?"), UM_YESNO) != 1) return;
	compose_close ();
}
static void compose_ccbcc () { C.ccOn = true; layout (); ui_w_focus (g_cCc); }
static bool compose_attach_path (const char *path)
{
	if (C.natt >= 16) { ask (TR ("16 attachments at most.")); return false; }
	int len; char *b = file_read (path, &len);
	if (!b) { ask (TR ("The file could not be read.")); return false; }
	if (len > 20 * 1024 * 1024) { free (b); ask (TR ("This file is too big to send by mail (20 MB at most).")); return false; }
	Attach &A = C.att[C.natt++];
	const char *nm = strrchr (path, '/'); nm = nm ? nm + 1 : path;
	scpy (A.name, nm, sizeof A.name); scpy (A.type, mime_type_of (nm), sizeof A.type);
	A.data = b; A.n = len;
	layout ();
	return true;
}
static void compose_attach ()
{
	char path[300]; path[0] = 0;
	if (ui_file_open ((long) "SD0:/home/documents", (long) path, sizeof path)) compose_attach_path (path);
}
// "mail --attach <list>": a new message with the files the list names (one path a line)
static void compose_with_files (const char *list)
{
	int len; char *b = file_read (list, &len);
	if (!b) return;
	compose_new (0);
	if (g_page == P_COMPOSE)
		for (char *l = b; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			int k = (int) strlen (l); if (k && l[k - 1] == '\r') l[--k] = 0;
			if (l[0]) compose_attach_path (l);
			l = e ? e + 1 : 0;
		}
	free (b);
}
// "mail --compose <file>": a new message filled from a draft -- "To: ...", "Cc: ...", "Subject: ..." lines, a blank
// line, the text (UTF-8) -- for the person to check and send
static bool draft_field (const char *l, const char *name)	// l starts with "name:" (its case ignored)
{
	for (; *name; l++, name++) if ((*l | 0x20) != (*name | 0x20)) return false;
	return true;
}
static void compose_from_draft (const char *path)
{
	int len; char *b = file_read (path, &len);
	if (!b) return;
	compose_new (0);
	if (g_page == P_COMPOSE)
	{
		char *l = b;
		while (l && *l)
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			int k = (int) strlen (l); if (k && l[k - 1] == '\r') l[--k] = 0;
			char *next = e ? e + 1 : 0;
			if (!l[0]) { l = next; break; }				// (the blank line: the text follows)
			const char *v = strchr (l, ':'); v = v ? v + 1 : l + k; while (*v == ' ') v++;
			if (draft_field (l, "to:")) set_text (g_cTo, v);
			else if (draft_field (l, "subject:")) set_text (g_cSubj, v);
			else if (draft_field (l, "cc:")) { set_text (g_cCc, v); C.ccOn = true; layout (); }
			l = next;
		}
		if (l && *l) set_text (g_cBody, l);
		ui_w_focus (g_cBody);
	}
	free (b);
}
// the message made: its bytes, its recipients
static bool compose_build (Buf &raw, Job *j, bool draft)
{
	int a = (int) ui_w_get_value (g_cFrom); if (a < 0 || a >= g_m.accts.n) a = 0;
	Account &A = g_m.accts.a[a];
	char from[400]; snprintf (from, sizeof from, A.name[0] ? "\"%s\" <%s>" : "%s%s", A.name[0] ? A.name : "", A.email);
	// the recipients: To, Cc, Bcc (Bcc not in the headers)
	static char em[64][160]; int n = 0;
	const char *lists[3] = { tx (g_cTo), tx (g_cCc), tx (g_cBcc) };
	for (int l = 0; l < 3 && n < 64; l++)
	{
		static Addr t[64]; int k = parse_addrs (lists[l], t, 64 - n);
		for (int i = 0; i < k; i++) if (strchr (t[i].email, '@')) scpy (em[n++], t[i].email, 160);
	}
	if (!n && !draft) { ask (TR ("Whom to? Type an address in To.")); return false; }
	for (int i = 0; i < n; i++) { const char *at = strchr (em[i], '@'); if (!draft && (!at || !strchr (at, '.') || strchr (em[i], ' '))) { char q[300]; snprintf (q, sizeof q, TR ("\"%s\" does not look like an e-mail address."), em[i]); ask (q); return false; } }
	if (!draft && !tx (g_cSubj)[0] && ask (TR ("Send it without a subject?"), UM_YESNO) != 1) return false;
	Buf html; text_to_html (tx (g_cBody), html);
	static Attachment at[16];
	for (int i = 0; i < C.natt; i++) { at[i].name = C.att[i].name; at[i].type = C.att[i].type; at[i].data = C.att[i].data; at[i].n = C.att[i].n; at[i].cid = 0; }
	Outgoing o; memset (&o, 0, sizeof o);
	o.from = from; o.to = tx (g_cTo); o.cc = tx (g_cCc); o.subject = tx (g_cSubj);
	o.inReplyTo = C.inReplyTo; o.references = C.references;
	o.text = tx (g_cBody); o.html = html.c (); o.att = at; o.natt = C.natt;
	o.date = now_utc (); o.tzMin = tz_minutes ();
	build (raw, o);
	j->kind = draft ? J_DRAFT : J_SEND;
	j->acct = A; scpy (j->root, g_m.stores[a]->root, sizeof j->root);
	for (int i = 0; i < n; i++) scpy (j->rcpt[i], em[i], 160);
	j->nrcpt = n;
	Folder *dest = g_m.stores[a]->special (draft ? SP_DRAFTS : SP_SENT);
	if (dest) scpy (j->dest, dest->name, sizeof j->dest);
	j->appendSent = A.provider != PV_GMAIL && A.provider != PV_OUTLOOK;
	j->raw = (char *) malloc (raw.n + 1); memcpy (j->raw, raw.c (), raw.n); j->rawLen = raw.n;
	return true;
}
// a POP3 account's own Sent / Drafts: the message kept on the card
static void keep_local (int a, int special, const Buf &raw)
{
	Store &s = *g_m.stores[a]; Folder *f = s.special (special); if (!f) return;
	s.load_index (*f);
	static Envelope e; envelope_of (raw.c (), raw.n, e);
	e.uid = f->uidnext > 0 ? f->uidnext : s.max_uid (*f) + 1; f->uidnext = e.uid + 1; e.flags = F_SEEN | (special == SP_DRAFTS ? F_DRAFT : 0);
	Msg m; msg_from_env (m, e);
	char pv[240]; preview_from_raw (raw.c (), raw.n, pv, sizeof pv); free (m.preview); m.preview = sdup (pv);
	s.insert (*f, m); s.keep_body (*f, e.uid, raw.c (), raw.n);
	Store::count (*f); s.save_index (*f); s.save_folders ();
}
static void compose_send ()
{
	if (g_page != P_COMPOSE) return;
	Buf raw; Job *j = new Job; memset (j, 0, sizeof *j);
	if (!compose_build (raw, j, false)) { job_free (j); return; }
	int a = (int) ui_w_get_value (g_cFrom); if (a < 0 || a >= g_m.accts.n) a = 0;
	// the people written to: remembered for completion
	static Addr ad[64]; const char *lists[3] = { tx (g_cTo), tx (g_cCc), tx (g_cBcc) };
	for (int l = 0; l < 3; l++) { int k = parse_addrs (lists[l], ad, 64); for (int i = 0; i < k; i++) if (strchr (ad[i].email, '@')) g_m.contacts.wrote_to (ad[i].name, ad[i].email, now_utc ()); }
	// the message answered: \Answered / $Forwarded
	if (C.haveAbout && C.mode >= 1 && C.mode <= 3)
	{
		Ref r = C.about;
		if (r.acct < g_m.accts.n && r.folder < g_m.stores[r.acct]->folders.n && r.msg < g_m.stores[r.acct]->folders[r.folder].msgs.n)
			g_m.set_flag (&r, 1, C.mode == 3 ? F_FORWARDED : F_ANSWERED, true);
	}
	if (g_m.accts.a[a].kind == K_POP3) keep_local (a, SP_SENT, raw);
	g_m.worker.push (j);
	compose_close ();
	status_note (TR ("Sending..."));
}
static void compose_draft ()
{
	if (g_page != P_COMPOSE) return;
	Buf raw; Job *j = new Job; memset (j, 0, sizeof *j);
	if (!compose_build (raw, j, true)) { job_free (j); return; }
	int a = (int) ui_w_get_value (g_cFrom); if (a < 0 || a >= g_m.accts.n) a = 0;
	if (g_m.accts.a[a].kind == K_POP3) { keep_local (a, SP_DRAFTS, raw); job_free (j); refresh_all (); }
	else g_m.worker.push (j);
	status_note (TR ("The draft is kept in Drafts."));
}
static void quick_send ()
{
	if (!s_nsh || !tx (g_quick)[0]) return;
	Ref r = s_sh[s_nsh - 1].ref;
	char t[600]; scpy (t, tx (g_quick), sizeof t);
	compose_new (1, &r);
	if (g_page != P_COMPOSE) return;
	// the quick reply's text above the quote, sent at once
	Buf b; b.add (t); b.add (tx (g_cBody));
	set_text (g_cBody, b.c ());
	compose_send ();
}

// ---- the worker's results ------------------------------------------------------------------------------------------------------
static void on_result (Result *r)
{
	if (g_page == P_WIZARD && (r->kind == J_CHECK || r->kind == J_OAUTH_START || r->kind == J_OAUTH_POLL)) { wz_answer (r); job_free (r->job); result_free (r); return; }
	if (r->kind == J_CHECK || r->kind == J_OAUTH_START || r->kind == J_OAUTH_POLL) { job_free (r->job); result_free (r); return; }	// (the assistant left)
	if (r->kind == J_PICTURE)
	{
		Remote *rm = remote_find (r->job->uids);
		if (rm)
		{
			int w, h; unsigned *px = r->ok && r->data ? image_decode ((const unsigned char *) r->data, r->dataLen, &w, &h) : 0;
			if (px) { rm->px = px; rm->w = w; rm->h = h; } else rm->failed = true;
		}
		rd_relayout ();
		job_free (r->job); result_free (r);
		return;
	}
	int changed = g_m.apply (r);
	if (r->certBad) { s_certAcct = g_m.acct_index (r->acctId); }
	if (r->kind == J_SYNC && r->ok && !r->err[0]) { int a = g_m.acct_index (r->acctId); if (a == s_certAcct) s_certAcct = -1; g_m.lastError[0] = 0; }
	if (r->kind == J_SEND)
	{
		if (r->ok) { status_note (r->err[0] ? r->err : TR ("Sent.")); int a = g_m.acct_index (r->acctId); if (a >= 0 && g_m.accts.a[a].kind == K_IMAP) g_m.sync (a, false, g_m.stores[a]->special (SP_SENT) ? g_m.stores[a]->index_of (g_m.stores[a]->special (SP_SENT)) : -1); }
		else
		{	// (the page is gone: what was written stays on the card)
			char p[120]; snprintf (p, sizeof p, HOME_DIR "/unsent-%ld.eml", (long) now_utc ());
			bool kept = file_save (p, r->job->raw, r->job->rawLen);
			char q[600]; snprintf (q, sizeof q, "%s", r->err[0] ? r->err : TR ("The message could not be sent."));
			if (kept) { int n = (int) strlen (q); snprintf (q + n, sizeof q - n, "\n\n%s %s", TR ("It is kept in"), p); }
			ui_message ((long) TR ("Mail: not sent"), (long) q, UM_OK);
		}
	}
	if (r->kind == J_DRAFT && r->ok) { int a = g_m.acct_index (r->acctId); Folder *d = a >= 0 ? g_m.stores[a]->special (SP_DRAFTS) : 0; if (d) g_m.sync (a, false, g_m.stores[a]->index_of (d)); }
	if (r->kind == J_BODY && r->ok) rd_body_came (r->acctId, r->folder, r->uid);
	if (changed & 1) rebuild_keep ();
	job_free (r->job); result_free (r);
	// new mail: told
	if (g_m.newMail > 0)
	{
		char t[200]; snprintf (t, sizeof t, g_m.newMail == 1 ? TR ("A new message") : TR ("%d new messages"), g_m.newMail);
		notify (TR ("Mail"), t);
		g_m.newMail = 0;
	}
	refresh_all ();
}

// ---- the menus -------------------------------------------------------------------------------------------------------------------
enum { M_NEW = 1, M_CHECK, M_ADD, M_SETTINGS, M_CONTACTS, M_SAVE, M_FIND = 10, M_CONV = 20, M_UNREAD, M_REPLY = 30, M_REPLYALL, M_FORWARD, M_SEND, M_ARCHIVE, M_DELETE, M_JUNK, M_STAR, M_MARK };
static char s_menus[1500];
static void menus_make ()
{
	snprintf (s_menus, sizeof s_menus,
		"MMail\nI1\t%s\tCtrl+N\nI2\t%s\tF5\n-\nI3\t%s\nI4\t%s\nI5\t%s\n-\nI6\t%s\n"
		"M%s\nI10\t%s\tCtrl+F\n"
		"M%s\nI20c\t%s\nI21\t%s\n"
		"M%s\nI30\t%s\tCtrl+R\nI31\t%s\nI32\t%s\tCtrl+L\nI33\t%s\tCtrl+Enter\n-\nI34\t%s\nI35\t%s\nI36\t%s\n-\nI37\t%s\tCtrl+S\nI38\t%s\tCtrl+U\n",
		TR ("New Message"), TR ("Check for New Mail"), TR ("Add an Account..."), TR ("Accounts and Settings..."), TR ("Contacts"), TR ("Save the Message as .eml..."),
		TR ("Edit"), TR ("Find..."),
		TR ("View"), TR ("Conversations (grouped)"), TR ("Unread Only"),
		TR ("Message"), TR ("Reply"), TR ("Reply All"), TR ("Forward"), TR ("Send"), TR ("Archive"), TR ("Delete"), TR ("Junk"), TR ("Star / Unstar"), TR ("Mark as Unread"));
}
static void save_eml ()
{
	Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
	Store &st = *g_m.stores[r[n - 1].acct]; Folder &f = st.folders[r[n - 1].folder]; const Msg &m = f.msgs[r[n - 1].msg];
	int len; char *raw = st.body (f, m.uid, &len);
	if (!raw) { ask (TR ("Open the message first (it is fetched then).")); return; }
	char nm[120]; safe_name (m.subject && m.subject[0] ? m.subject : "message", nm, 100); strcat (nm, ".eml");
	char out[300]; mkdirs ("SD0:/home/documents");
	ui_file_filter ((long) "Mail messages|*.eml|All files|*");
	long ok = ui_file_save ((long) "SD0:/home/documents", (long) nm, (long) out, sizeof out);
	ui_file_filter (0);
	if (ok && !file_save (out, raw, len)) ask (TR ("The file could not be written."));
	free (raw);
}
static void command (int id)
{
	Ref r[60]; int n = conv_refs (r, 60);
	switch (id)
	{
	case M_NEW: compose_new (0); break;
	case M_CHECK: act_check (); break;
	case M_ADD: open_wizard (); break;
	case M_SETTINGS: open_settings (); break;
	case M_CONTACTS: show_contacts (g_page != P_CONTACTS); break;
	case M_SAVE: save_eml (); break;
	case M_FIND: ui_w_focus (g_search); break;
	case M_CONV: g_m.grouped = !g_m.grouped; ui_menu_state (M_CONV, g_m.grouped ? UMS_CHECKED : 0); g_conv = -1; rd_clear (); g_m.build (); layout (); refresh_all (); break;
	case M_UNREAD: g_m.unreadOnly = !g_m.unreadOnly; ui_w_set_value (g_seg, g_m.unreadOnly); ui_menu_state (M_UNREAD, g_m.unreadOnly ? UMS_CHECKED : 0); g_m.build (); g_conv = -1; rd_clear (); layout (); refresh_all (); break;
	case M_REPLY: if (n) compose_new (1, &r[n - 1]); break;
	case M_REPLYALL: if (n) compose_new (2, &r[n - 1]); break;
	case M_FORWARD: if (n) compose_new (3, &r[n - 1]); break;
	case M_SEND: compose_send (); break;
	case M_ARCHIVE: act_archive (); break;
	case M_DELETE: act_delete (); break;
	case M_JUNK: act_junk (); break;
	case M_STAR: act_star (); break;
	case M_MARK: act_unread (); break;
	}
}

// ---- what a widget said (out of ui_w_event: the dialogues and the popups may open here) ---------------------------------------------
static void control (long w, long code)
{
	if (g_page == P_WIZARD && wz_control (w, code)) return;
	if (g_page == P_SETTINGS && st_control (w, code)) return;
	if (g_page == P_CONTACTS && k_control (w, code)) return;
	if (w == g_side) { if (code == UN_CHANGE) side_chosen ((int) ui_w_get_value (w)); }
	else if (w == g_bNew) { if (code == UN_ACTIVATE) compose_new (0); }
	else if (w == g_tCheck) act_check ();
	else if (w == g_tSide) { g_sideWant = !g_sideWant; layout (); }
	else if (w == g_tBack) { g_conv = -1; rd_clear (); layout (); refresh_all (); }
	else if (w == g_search) { if (code == UN_CHANGE) s_qT = clock_ms () | 1; }
	else if (w == g_seg) { if (code == UN_CHANGE) { g_m.unreadOnly = ui_w_get_value (w) == 1; ui_menu_state (M_UNREAD, g_m.unreadOnly ? UMS_CHECKED : 0); g_m.build (); g_conv = -1; rd_clear (); layout (); refresh_all (); } }
	else if (w == g_lsBar) { if (code == UN_CHANGE) { s_lsSy = s_lsVal = (int) ui_w_get_value (w); ui_view_dirty (g_list); } }
	else if (w == g_rdBar) { if (code == UN_CHANGE) { s_sy = s_barVal = (int) ui_w_get_value (w); ui_view_dirty (g_read); } }
	else if (w == g_qsend) { if (code == UN_ACTIVATE) quick_send (); }
	else if (w == g_quick) { if (code == UN_ACTIVATE) quick_send (); }
	else if (w == g_wAdd) { if (code == UN_ACTIVATE) open_wizard (); }
	else if (w == g_cTo || w == g_cCc || w == g_cBcc) { if (code == UN_CHANGE) sug_update (w); }
	else if (w == g_cCcBtn) { if (code == UN_ACTIVATE) compose_ccbcc (); }
	else if (w == g_cSend) { if (code == UN_ACTIVATE) compose_send (); }
	else if (w == g_cAttach) { if (code == UN_ACTIVATE) compose_attach (); }
	else if (w == g_cDraft) { if (code == UN_ACTIVATE) compose_draft (); }
	else if (w == g_cDiscard) { if (code == UN_ACTIVATE) compose_discard (); }
	else
	{
		Ref r[60]; int n = conv_refs (r, 60);
		if (!n || code != UN_ACTIVATE) return;
		if (w == g_tReply) compose_new (1, &r[n - 1]);
		else if (w == g_tAll) compose_new (2, &r[n - 1]);
		else if (w == g_tFwd) compose_new (3, &r[n - 1]);
		else if (w == g_tArch) act_archive ();
		else if (w == g_tDel) act_delete ();
		else if (w == g_tJunk) act_junk ();
		else if (w == g_tStar) act_star ();
	}
}
// a key before the widgets. true: taken.
static bool key_first (int hid, int mods)
{
	if (g_page == P_COMPOSE)
	{
		if (hid == KEY_ENTER && (mods & UI_MOD_CTRL)) { compose_send (); return true; }
		if (sug_key (hid)) return true;
	}
	if (hid == KEY_ESC)
	{
		if (g_page == P_COMPOSE) { compose_discard (); return true; }
		if (g_page == P_CONTACTS) { show_contacts (false); return true; }
		if (g_page == P_SETTINGS) { st_save (); show_page (P_MAIL); return true; }
		if (g_page == P_WIZARD) { g_m.worker.cancel = 1; show_page (P_MAIL); return true; }
		if (tx (g_search)[0]) { set_text (g_search, ""); search_changed (); ui_w_focus (g_list); return true; }
		if (g_onePane && g_sideWant) { g_sideWant = false; layout (); return true; }
		if (g_onePane && g_conv >= 0) { g_conv = -1; rd_clear (); layout (); refresh_all (); return true; }
	}
	if (hid == KEY_DOWN && ui_w_get_focus () == g_search) { ui_w_focus (g_list); return true; }
	return false;
}
// what is marked drawn now (the loop's end; a menu about to open too)
static void draw_dirty ()
{
	static bool band;
	if ((s_nsh > 0) != band) { band = s_nsh > 0; if (g_page == P_MAIL) layout (); }		// (the quick reply's band comes and goes)
	if (g_sideDirty) { g_sideDirty = false; side_build (); ui_w_draw (g_side); }
	if (g_toolDirty) { g_toolDirty = false; tb_sync (); }
	if (g_listDirty) { g_listDirty = false; ls_sync (); }
	if (g_readDirty) { g_readDirty = false; rd_sync (); }
	if (g_footDirty) { g_footDirty = false; ui_view_dirty (g_foot); }
	ui_view_flush ();
}
static void run_later ()
{
	int who = g_later.who;
	if (!who) return;
	g_later.who = L_NONE;
	switch (who)
	{
	case L_READ: rd_act (); break;
	case L_FOOT: foot_act (); break;
	case L_LIST: open_conv (g_later.h.a); if (g_later.right) { draw_dirty (); row_menu (g_later.x, g_later.y); } break;
	case L_CHIP: { int i = g_later.h.a; if (i < C.natt) { free (C.att[i].data); for (int k = i; k < C.natt - 1; k++) C.att[k] = C.att[k + 1]; C.natt--; layout (); } break; }
	case L_CARD: { int sel = k_sel (); if (sel >= 0) { Contact c; g_m.contacts.get (s_kOrder[sel], c); compose_to (c.name, g_later.h.a ? c.email2 : c.email); } break; }
	case L_SUG: sug_take (g_later.h.a); break;
	}
}
static void build_window ()
{
	s_jet = (const unsigned *) ui_jetons ();
	g_w = (int) ui.w; g_h = (int) ui.h;
	ui_fill (0, 0, g_w, g_h, jet (J_FACE));
	tb_create ();
	g_side = ui_list (0, 0, 10, 10, UL_SIDEBAR | UL_NOSORT);
	ui_list_cell (g_side, (long) side_cell);
	g_foot = ui_view (0, 0, 10, 10, 0); ui_view_on (g_foot, (long) foot_paint, (long) foot_mouse, 0, 0);
	g_lhead = ui_view (0, 0, 10, 10, 0); ui_view_on (g_lhead, (long) lhead_paint, 0, 0, 0);
	char sg[60]; snprintf (sg, sizeof sg, "%s|%s", TR ("All"), TR ("Unread"));
	g_seg = ui_segmented (0, 0, 120, 24, (long) sg); ui_w_set_parent (g_seg, g_lhead);
	g_list = ui_view (0, 0, 10, 10, UVW_FOCUS); ui_view_on (g_list, (long) ls_paint, (long) ls_mouse, (long) ls_key, (long) ls_wheel);
	g_lsBar = ui_scrollbar (0, 0, 10, 10, 0); ui_w_set_parent (g_lsBar, g_list);
	rd_create ();
	g_welcome = ui_view (0, 0, 10, 10, 0); ui_view_on (g_welcome, (long) welcome_paint, 0, 0, 0);
	g_wAdd = ui_pushbutton (0, 0, 170, 0, (long) TR ("Add an account"), UB_DEFAULT); ui_w_set_parent (g_wAdd, g_welcome);
	c_create (); k_create (); wz_create (); st_create ();
	ui_w_show (g_cp, 0); ui_w_show (g_kp, 0); ui_w_show (g_wp, 0); ui_w_show (g_sp, 0);
	menus_make ();
	ui_menu ((long) s_menus);
	ui_menu_state (M_CONV, UMS_CHECKED);
}

} // namespace mailapp

// "mail SD0:/x.eml": the file shown as a message of a pseudo account (read only)
static bool open_eml (const char *path)
{
	int len; char *raw = file_read (path, &len);
	if (!raw) return false;
	Account &a = g_m.accts.a[0]; account_defaults (a);
	scpy (a.id, "eml", sizeof a.id); scpy (a.label, TR ("File"), sizeof a.label); a.kind = K_POP3; a.checkMinutes = 0;
	g_m.accts.n = 1;
	Store *s = new Store; s->acct = &a; scpy (s->root, HOME_DIR "/tmp/eml", sizeof s->root);
	g_m.stores[0] = s;
	Folder &f = s->add_folder ("INBOX"); const char *bn = strrchr (path, '/'); scpy (f.show, bn ? bn + 1 : path, sizeof f.show); f.special = SP_INBOX; f.loaded = true;
	static Envelope e; envelope_of (raw, len, e); e.uid = 1; e.flags = F_SEEN;
	Msg m; msg_from_env (m, e);
	char pv[240]; preview_from_raw (raw, len, pv, sizeof pv); free (m.preview); m.preview = sdup (pv);
	s->insert (f, m);
	mkdirs (HOME_DIR "/tmp/eml"); s->keep_body (f, 1, raw, len);
	free (raw);
	g_emlMode = true;
	return true;
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	netkit_kit_open ();
	s_jet = (const unsigned *) ui_jetons ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (1000, (int) scr[0] - 8), wh = imin (640, (int) (scr[1] - scr[3] - scr[2]) - 32 - 96);
	g_mainWin = ui_window_ex (0, scr[3], ww, wh, (long) "Mail", WF_RESIZABLE);
	if (g_mainWin <= 0) return 1;
	ui_min_size (480, 320);
	ui_on_resize ((long) on_resize, 0);
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/mail.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	mkdirs (HOME_DIR);
	mkdirs ("SD0:/home/documents");
	g_host = &s_host;
	tz_minutes ();

	const char *a = argc > 1 ? argv[1] : "";
	char attachList[300] = "", draft[300] = "";
	if (!strcmp (a, "--attach") && argc > 2) { scpy (attachList, argv[2], sizeof attachList); a = ""; }
	else if (!strcmp (a, "--compose") && argc > 2) { scpy (draft, argv[2], sizeof draft); a = ""; }
	if (a[0] && open_eml (a)) {}
	else g_m.open ();

	build_window ();
	g_m.worker.start ();
	c_accounts_changed ();
	g_m.build ();
	layout ();
	refresh_all ();
	if (g_emlMode) { g_m.sel.kind = SEL_FOLDER; g_m.sel.acct = 0; g_m.sel.folder = 0; g_m.build (); g_listDirty = true; open_conv (0); }
	else
	{
		if (g_m.view.convs.n && !g_onePane) open_conv (0);
		if (g_m.accts.n) g_m.sync_all ();
		if (attachList[0]) compose_with_files (attachList);
		if (draft[0]) compose_from_draft (draft);
	}
	ui_view_flush ();

	unsigned lastSec = 0;
	static char lastCur[120]; static int lastBusy; static unsigned noteSeen;
	for (;;)
	{
		long t = ui_event (30);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
			if (!(press && key_first (hid, mods)))
			{
				long code = 0, taken = 0;
				long w = uikit_w_event (&code, &taken);
				if (code) control (w, code);
				if (press && !taken) { long id = ui_menu_key (0); if (id >= 0) command ((int) id); }
			}
			run_later ();
		}
		else ui_w_idle ();

		for (Result *r; (r = g_m.worker.take ()); ) on_result (r);
		unsigned now = clock_ms ();
		if (now - lastSec >= 1000)
		{
			lastSec = now;
			if (!g_emlMode) g_m.tick ();
			if (g_listDirty == false && g_m.worker.busy && !g_m.view.convs.n) g_listDirty = true;
		}
		if (s_qT && now - s_qT > 300) { s_qT = 0; search_changed (); }
		// the worker's line
		int b = g_m.worker.busy ? 1 : 0;
		if (b != lastBusy || strcmp (lastCur, g_m.worker.current)) { lastBusy = b; scpy (lastCur, g_m.worker.current, sizeof lastCur); g_footDirty = true; if (g_page == P_WIZARD && W.page == WP_CHECKING && g_m.worker.current[0]) { set_text (g_wPara2, g_m.worker.current); } }
		if (g_noteT != noteSeen && now - g_noteT > 8000) { noteSeen = g_noteT; g_footDirty = true; }

		draw_dirty ();
	}
	g_m.worker.stop ();
	return 0;
}
