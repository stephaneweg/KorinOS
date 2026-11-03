//
// pm.h -- a private conversation's window, a messenger's: the other's disc and whether they are there, the bubbles
// (theirs on the left, yours on the right in the accent), the line you type. On Onyx it was the same program started
// again, talking to the connection by mailboxes; here it is one more window of this program (UIKit's ui_window_use):
// it reads the conversation's Buf directly and no process stands between them.
//
#ifndef _irc_pm_h
#define _irc_pm_h

#include "chat.h"

namespace irc {

void say (int bi, const char *s, bool action);			// main.cpp

#define PW		360
#define PH		440
#define PM_HEAD		48
#define PM_IN		44
enum { MAXPM = 12 };
struct Pm { long win, head, msgs, bar, input, send, more; Buf *b; int scroll, total, vw, vh; bool dirty; };
static Pm g_pm[MAXPM];

static Pm *pm_of (Buf *b) { for (int i = 0; i < MAXPM; i++) if (g_pm[i].win && g_pm[i].b == b) return &g_pm[i]; return 0; }
static Pm *pm_of_win (long win) { for (int i = 0; i < MAXPM; i++) if (g_pm[i].win && g_pm[i].win == win) return &g_pm[i]; return 0; }
static Pm *pm_of_view (long v) { for (int i = 0; i < MAXPM; i++) if (g_pm[i].win && (g_pm[i].msgs == v || g_pm[i].head == v)) return &g_pm[i]; return 0; }
static Pm *pm_of_widget (long w)
{
	for (int i = 0; i < MAXPM; i++)
		if (g_pm[i].win && (g_pm[i].input == w || g_pm[i].send == w || g_pm[i].more == w || g_pm[i].bar == w)) return &g_pm[i];
	return 0;
}

static bool bubble (const Line &l) { return l.kind == LK_MSG || l.kind == LK_NOTICE; }
static bool new_time (const Line &l, const Line *p) { return !p || l.mins - p->mins >= 15 || l.mins < p->mins; }
static int bub_max (Pm *m) { return (m->vw - 12) * (m->vw < 300 ? 86 : 74) / 100 - 20; }
static bool discs (Pm *m) { return m->vw >= 300; }

static int pm_line_h (Pm *m, Buf *b, int i)
{
	const Line &l = buf_line (b, i);
	const Line *p = i ? &buf_line (b, i - 1) : 0;
	int h = new_time (l, p) ? ROW_H + 10 : 0;
	if (!bubble (l)) return h + wrap_rows (l.text, m->vw - 60, PX_SMALL, f_sans ()) * ROW_H + 8;
	h += wrap_rows (l.text, bub_max (m), ui_px (), f_sans ()) * ROW_H + 12;
	return h + ((p && bubble (*p) && p->self == l.self && !new_time (l, p)) ? 2 : 8);
}
static void pm_head_paint (long v, long w, long h)
{
	Pm *m = pm_of_view (v); if (!m) return;
	Buf *b = m->b;
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (0, h - 1, w, 1, jet (J_LIGNE));
	avatar (10, ((int) h - 28) / 2, 28, b->name);
	text_box (b->name, 48, 6, 18, jet (J_TEXTE1), ui_px (), f_bold (), (int) w - 96);
	bool on = g_state == ST_ONLINE && !b->peerGone;
	ui_vp_disc (52 * 16, 33 * 16, 4 * 16, on ? jet (J_SUCCES) : jet (J_TEXTE3));
	char s[140];
	if (on) snprintf (s, sizeof s, TR ("Online, %s"), g_server);
	else scpy (s, sizeof s, b->peerGone ? TR ("Has left IRC") : TR ("Offline"));
	text_box (s, 62, 24, 18, jet (J_TEXTE2), PX_SMALL, f_sans (), (int) w - 110);
}
static void pm_msgs_paint (long v, long w, long h)
{
	Pm *m = pm_of_view (v); if (!m) return;
	Buf *b = m->b;
	int px = ui_px ();
	m->vw = (int) w; m->vh = (int) h;
	ui_fill (0, 0, w, h, jet (J_SURFACE));
	if (!b->count)
	{
		avatar (((int) w - 64) / 2, (int) h / 2 - 76, 64, b->name);
		text_center (b->name, 0, (int) h / 2 - 4, (int) w, 20, jet (J_TEXTE1), px, f_bold ());
		char s[80]; snprintf (s, sizeof s, TR ("Say hello to %s."), b->name);
		text_center (s, 0, (int) h / 2 + 18, (int) w, 20, jet (J_TEXTE3), PX_SMALL, f_sans ());
		return;
	}
	int v0 = (int) h - 8;
	int y = 4 + (v0 > m->total ? v0 - m->total : v0 - m->total + m->scroll);
	for (int i = 0; i < b->count; i++)
	{
		const Line &l = buf_line (b, i);
		const Line *p = i ? &buf_line (b, i - 1) : 0, *nx = i + 1 < b->count ? &buf_line (b, i + 1) : 0;
		int lh = pm_line_h (m, b, i);
		if (y + lh > 0 && y < h)
		{
			int yy = y;
			if (new_time (l, p))
			{
				char t[8]; fmt_time (l.mins, t);
				text_center (t, 0, yy + 4, (int) w, ROW_H, jet (J_TEXTE3), PX_SMALL, f_sans ());
				yy += ROW_H + 10;
			}
			if (!bubble (l))
			{
				char t[TEXT_MAX + 40];
				if (l.kind == LK_ACTION) snprintf (t, sizeof t, "\xE2\x80\xA2 %s %s", l.nick, l.text);
				else scpy (t, sizeof t, l.ev == JE_QUIT ? TR ("Has left IRC") : l.text);
				unsigned ink = l.kind == LK_ERROR ? danger_text () : jet (J_TEXTE2);
				long f = l.kind == LK_ACTION ? f_ital () : f_sans ();
				const char *s = t;
				for (yy += 4; ; yy += ROW_H)
				{
					const char *nxs; int n = wrap_row (s, (int) w - 60, PX_SMALL, f, &nxs);
					char c[TEXT_MAX + 8]; memcpy (c, s, (size_t) imin (n, TEXT_MAX)); c[imin (n, TEXT_MAX)] = 0;
					text_center (c, 0, yy, (int) w, ROW_H, ink, PX_SMALL, f);
					if (!*nxs) break;
					s = nxs;
				}
			}
			else
			{
				yy += (p && bubble (*p) && p->self == l.self && !new_time (l, p)) ? 2 : 8;
				int widest = 0, rows = wrap_rows (l.text, bub_max (m), px, f_sans (), 0, &widest);
				int bw = widest + 20, bh = rows * ROW_H + 12;
				int lx = discs (m) ? 42 : 10;
				int x = l.self ? (int) w - 18 - bw : lx;
				unsigned fill = l.self ? jet (J_ACCENT) : jet (J_CREUX);
				unsigned ink = l.self ? ink_on (fill) : l.kind == LK_NOTICE ? alert_text () : jet (J_TEXTE1);
				ui_rbox (x, yy, bw, bh, 12, fill, fill);
				draw_rows (l.text, x + 10, yy + 6, bub_max (m), ROW_H, ink, px, f_sans ());
				bool lastOfRun = !(nx && bubble (*nx) && nx->self == l.self && !new_time (*nx, &l));
				if (!l.self && lastOfRun && discs (m)) avatar (10, yy + bh - 24, 24, b->name);
			}
		}
		y += lh;
	}
}
static long pm_msgs_wheel (long v, long steps)
{
	Pm *m = pm_of_view (v); if (!m) return 0;
	m->scroll += (int) steps * 54; m->dirty = true;
	return 0;
}
// Laid out before the flush: its height, its bar (pm's own chat_sync).
static void pm_sync (Pm *m)
{
	Buf *b = m->b;
	if (!m->vw) { long W = ui.w; m->vw = (int) W; m->vh = (int) ui.h - PM_HEAD - PM_IN; }
	int was = m->total; m->total = 0;
	for (int i = 0; i < b->count; i++) m->total += pm_line_h (m, b, i);
	if (m->scroll > 0 && m->total > was) m->scroll += m->total - was;
	int v = m->vh - 8, mx = imax (0, m->total - v);
	m->scroll = imax (0, imin (m->scroll, mx));
	bool need = m->total > v;
	ui_w_show (m->bar, need);
	if (need) { ui_w_set_range (m->bar, 0, m->total, v, 54); ui_w_set_value (m->bar, m->total - v - m->scroll); }
	ui_view_dirty (m->msgs); ui_view_dirty (m->head);
}
static void pm_layout (Pm *m, int w, int h)
{
	m->vw = w; m->vh = h - PM_HEAD - PM_IN;
	ui_w_move (m->head, 0, 0, w, PM_HEAD);
	ui_w_move (m->more, w - 40, (PM_HEAD - ctl_h ()) / 2, 32, ctl_h ());
	ui_w_move (m->msgs, 0, PM_HEAD, w, m->vh);
	ui_w_move (m->bar, w - 12, PM_HEAD, 12, m->vh);
	int c = ctl_h ();
	ui_w_move (m->input, 10, h - PM_IN + (PM_IN - c) / 2, w - 20 - 40, c);
	ui_w_move (m->send, w - 44, h - PM_IN + (PM_IN - c) / 2, 34, c);
	m->dirty = true;
}
static void pm_on_resize (long w, long h)
{
	Pm *m = pm_of_win (ui.win); if (!m) return;
	pm_layout (m, (int) w, (int) h);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	pm_sync (m);
	ui_w_draw (0);
	ui_update_all ();
}
static long g_mainWin;
// A private conversation shown: its window, made if it is not there. The keyboard goes to it only when asked for.
static void pm_show (Buf *b)
{
	Pm *m = pm_of (b);
	if (m) { ui_raise (m->win); return; }
	for (int i = 0; i < MAXPM && !m; i++) if (!g_pm[i].win) m = &g_pm[i];
	if (!m) { info_here (TR ("Too many private conversations are open.")); return; }
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int slot = (int) (m - g_pm);
	long win = ui_window_ex (imin (360 + slot * 24, (int) scr[0] - PW - 8), scr[3] + 24 + slot * 24, PW, imin (PH, (int) (scr[1] - scr[3] - scr[2]) - 32 - 96), (long) b->name, WF_RESIZABLE);
	if (win <= 0) { ui_window_use (g_mainWin); info_here (TR ("The window could not be opened.")); return; }
	memset (m, 0, sizeof *m);
	m->win = win; m->b = b; b->win = win;
	ui_min_size (280, 300);
	ui_on_resize ((long) pm_on_resize, 0);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	m->head = ui_view (0, 0, 10, 10, 0);
	ui_view_on (m->head, (long) pm_head_paint, 0, 0, 0);
	m->more = ui_pushbutton (0, 0, 32, 0, (long) "\xE2\x8B\xAF", UB_GHOST);
	ui_w_set_parent (m->more, m->head);
	ui_w_tip (m->more, (long) TR ("Who is this?"));
	m->msgs = ui_view (0, 0, 10, 10, 0);
	ui_view_on (m->msgs, (long) pm_msgs_paint, 0, 0, (long) pm_msgs_wheel);
	m->bar = ui_scrollbar (0, 0, 10, 10, 0);
	ui_w_set_parent (m->bar, m->msgs);
	ui_w_show (m->bar, 0);
	m->input = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_w_set_range (m->input, 0, 400, 0, 0);
	char hint[80]; snprintf (hint, sizeof hint, TR ("Message to %s"), b->name);
	ui_textbox_hint (m->input, (long) hint);
	m->send = ui_pushbutton (0, 0, 34, 0, (long) "\xE2\x9E\xA4", UB_GHOST);
	ui_w_tip (m->send, (long) TR ("Send"));
	pm_layout (m, (int) ui.w, (int) ui.h);
	b->unread = b->mentions = 0;
	g_sideDirty = true;
	pm_sync (m);
	ui_w_draw (0);
	ui_w_focus (m->input);
	ui_update_all ();
	ui_window_use (g_mainWin);
}
static void pm_close (Pm *m)
{
	m->b->win = 0;
	long w = m->win;
	memset (m, 0, sizeof *m);
	ui_window_close (w);
	ui_window_use (g_mainWin);
	g_sideDirty = true;
}
static void pm_line_added (Buf *b) { Pm *m = pm_of (b); if (m) m->dirty = true; }
static void pm_state (Buf *b) { Pm *m = pm_of (b); if (m) m->dirty = true; }

// What a window's widgets said (the loop): true: it was one of a private window's.
static bool pm_control (long w, long code)
{
	Pm *m = pm_of_widget (w); if (!m) return false;
	if (w == m->bar) { if (code == UN_CHANGE) { m->scroll = m->total - (m->vh - 8) - (int) ui_w_get_value (w); ui_window_use (m->win); ui_view_dirty (m->msgs); ui_window_use (g_mainWin); } return true; }
	if (w == m->more && code == UN_ACTIVATE)
	{
		if (g_sock >= 0) { char l[96] = "WHOIS "; scat (l, sizeof l, m->b->name); send_line (l); }
		return true;
	}
	if ((w == m->input && code == UN_ACTIVATE) || (w == m->send && code == UN_ACTIVATE))
	{
		char s[512]; scpy (s, sizeof s, (const char *) ui_w_get_text (m->input));
		if (!s[0]) return true;
		ui_w_set_text (m->input, (long) "");
		int bi = -1; for (int i = 0; i < g_nbuf; i++) if (g_buf[i] == m->b) bi = i;
		if (bi >= 0)
		{
			if (!strncmp (s, "/me ", 4)) say (bi, s + 4, true); else say (bi, s, false);
		}
		ui_w_focus (m->input);
		m->scroll = 0; m->dirty = true;
		return true;
	}
	return true;
}
// Each turn of the loop: the windows that changed.
static void pm_flush ()
{
	for (int i = 0; i < MAXPM; i++)
	{
		Pm *m = &g_pm[i];
		if (!m->win) continue;
		ui_window_use (m->win);
		if (m->dirty) { m->dirty = false; pm_sync (m); }
		ui_view_flush ();
	}
	ui_window_use (g_mainWin);
}

} // namespace irc

#endif
