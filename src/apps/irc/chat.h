//
// chat.h -- a channel's messages: a ui_view, pinned to its newest line, three columns (the time, the nickname at the
// right of its column, the text): a run from one author shows its name once; a mention tinted, with a bar; arrivals
// and departures folded in one small row ("-> carl, dan   <- eve"), opened by a click; the server's lines in mono so
// that a MOTD keeps its drawing; a mark above the first unread line; a pill when messages come in below the view.
//
// The rows are laid out by chat_sync (), from the loop, and only painted by chat_paint: a scroll bar must not be
// touched from inside a view's paint function.
//
#ifndef _irc_chat_h
#define _irc_chat_h

#include "look.h"

namespace irc {

#define ROW_H	18
#define ROW_MONO 16
enum { IT_LINE, IT_PACK };
struct Item { unsigned char kind, mark; int first, n, h; };
static Item s_items[MAXLINES]; static int s_nitems, s_total, s_vh, s_vw;
static long g_chat, g_chatBar;
static bool g_showEvents = true;
static int  s_pillX, s_pillY, s_pillW;
static int  s_ctxLine = -1, s_ctxX, s_ctxY;		// a right click on a message: the loop opens the menu

struct Cols { int tx, nx, nw, x; };			// the time (-1: none), the nickname's column, the text
static Cols cols ()
{
	Cols c; int w = s_vw;
	c.tx = 10; c.nx = 54; c.nw = w >= 640 ? 104 : 88;
	if (w < 360) c.nw = 72;
	if (w < 420) { c.tx = -1; c.nx = 10; }
	c.x = c.nx + c.nw + 8;
	return c;
}
static int text_room () { return s_vw - cols ().x - 18; }

static bool groups (const Line &l) { return l.kind == LK_MSG || l.kind == LK_NOTICE; }
static bool opens (const Line &l, const Line *p)
{
	if (!groups (l)) return false;
	return !(p && groups (*p) && p->kind == l.kind && ieq (p->nick, l.nick) && l.mins - p->mins < 5 && l.mins >= p->mins);
}
static bool folds (const Line &l) { return l.kind == LK_EVENT && l.ev != JE_NONE; }

static long line_font (const Line &l, Buf *b) { return b->kind == BK_SERVER && l.kind != LK_ERROR ? f_mono () : l.kind == LK_ACTION ? f_ital () : f_sans (); }
static int line_h (Buf *b, Line &l, const Line *p)
{
	bool sv = b->kind == BK_SERVER && l.kind != LK_ERROR;
	Cols c = cols ();
	if (l.hw != s_vw)
	{
		if (sv) l.h = (short) (wrap_rows (l.text, s_vw - c.nx - 18, PX_SMALL, f_mono ()) * ROW_MONO);
		else if (l.kind == LK_EVENT) l.h = (short) (wrap_rows (l.text, text_room (), PX_SMALL, f_sans ()) * ROW_H);
		else
		{
			int first = l.kind == LK_ACTION ? text_w (l.nick, ui_px (), f_boldital ()) + 6 : 0;
			l.h = (short) (wrap_rows (l.text, text_room (), ui_px (), line_font (l, b), first) * ROW_H);
		}
		l.hw = (short) s_vw;
	}
	int h = l.h;
	if (sv) return h;
	if (opens (l, p) || l.kind == LK_ERROR) h += 6;
	if (l.mention) h += 2;
	return h;
}

// The rows of the conversation shown: lines, folded events.
static void chat_layout (Buf *b)
{
	s_nitems = 0; s_total = 0;
	const Line *prev = 0;
	for (int i = 0; i < b->count; i++)
	{
		Line &l = buf_line (b, i);
		if (folds (l))
		{
			if (!g_showEvents) continue;
			Item &it = s_items[s_nitems++];
			it.kind = IT_PACK; it.first = i; it.n = 1; it.mark = b->mark == i;
			int last = l.mins;
			while (i + 1 < b->count)
			{
				Line &n = buf_line (b, i + 1);
				if (!folds (n) || n.mins - last >= 5 || n.mins < last || b->mark == i + 1) break;
				last = n.mins; it.n++; i++;
			}
			it.h = ROW_H * (l.open ? it.n : 1);
		}
		else
		{
			Item &it = s_items[s_nitems++];
			it.kind = IT_LINE; it.first = i; it.n = 1; it.mark = b->mark == i;
			it.h = line_h (b, l, prev);
			prev = &l;
		}
		if (s_items[s_nitems - 1].mark) s_items[s_nitems - 1].h += ROW_H;
		s_total += s_items[s_nitems - 1].h;
		if (s_items[s_nitems - 1].kind == IT_PACK) prev = 0;
	}
}
static int content_y ()					// the first row's y
{
	int v = s_vh - 8;
	return 4 + (v > s_total ? v - s_total : v - s_total + g_buf[g_cur]->scroll);
}
static void chat_clamp (Buf *b)
{
	int m = s_total - (s_vh - 8); if (m < 0) m = 0;
	if (b->scroll > m) b->scroll = m;
	if (b->scroll < 0) b->scroll = 0;
	if (b->scroll == 0) b->below = 0;
}
static void chat_bar_sync (Buf *b)
{
	int v = s_vh - 8;
	bool need = s_total > v;
	ui_w_show (g_chatBar, need);
	if (need) { ui_w_set_range (g_chatBar, 0, s_total, v, 54); ui_w_set_value (g_chatBar, s_total - v - b->scroll); }
}
// The conversation laid out again (the loop, before the views are flushed): the place read stays where it is when
// lines come in below.
static void chat_sync ()
{
	Buf *b = g_buf[g_cur];
	int was = b->lastTotal;
	chat_layout (b);
	if (b->scroll > 0 && s_total > was) b->scroll += s_total - was;
	b->lastTotal = s_total;
	chat_clamp (b);
	chat_bar_sync (b);
	ui_view_dirty (g_chat);
}
static void chat_to (Buf *b, int scroll) { b->scroll = scroll; chat_clamp (b); chat_bar_sync (b); ui_view_dirty (g_chat); }

// A fold of arrivals and departures, as one row: someone who came and went inside it is not told.
static void pack_text (Buf *b, const Item &it, char *o, int cap)
{
	static const char *in[MAXLINES], *out[MAXLINES];
	int ni = 0, nu = 0;
	char ren[200] = "";
	for (int i = 0; i < it.n; i++)
	{
		const Line &l = buf_line (b, it.first + i);
		if (l.ev == JE_JOIN) in[ni++] = l.nick;
		else if (l.ev == JE_NICK) { if (ren[0]) scat (ren, sizeof ren, "   "); scat (ren, sizeof ren, l.nick); scat (ren, sizeof ren, " \xE2\x86\x92 "); scat (ren, sizeof ren, l.text); }
		else
		{
			int k = -1; for (int q = 0; q < ni; q++) if (ieq (in[q], l.nick)) k = q;
			if (k >= 0) { in[k] = in[--ni]; continue; }
			out[nu++] = l.nick;
		}
	}
	o[0] = 0;
	struct { const char **v; int n; const char *mark; } S[2] = { { in, ni, "\xE2\x86\x92 " }, { out, nu, "\xE2\x86\x90 " } };
	for (int s = 0; s < 2; s++)
	{
		if (!S[s].n) continue;
		if (o[0]) scat (o, cap, "   ");
		scat (o, cap, S[s].mark);
		for (int i = 0; i < S[s].n && i < 6; i++) { if (i) scat (o, cap, ", "); scat (o, cap, S[s].v[i]); }
		if (S[s].n > 6) { char m[40]; snprintf (m, sizeof m, TR (" and %d more"), S[s].n - 6); scat (o, cap, m); }
	}
	if (ren[0]) { if (o[0]) scat (o, cap, "   "); scat (o, cap, ren); }
	if (!o[0]) { scat (o, cap, "\xE2\x86\x92 \xE2\x86\x90 "); scat (o, cap, buf_line (b, it.first).nick); }
}
static void draw_time (const Cols &c, int y, int h, int mins)
{
	if (c.tx < 0) return;
	char t[8]; fmt_time (mins, t);
	text_box (t, c.tx, y, h, jet (J_TEXTE3), PX_SMALL, f_sans (), 40);
}
static void draw_pack (Buf *b, const Item &it, int y)
{
	Cols c = cols ();
	const Line &l0 = buf_line (b, it.first), &ln = buf_line (b, it.first + it.n - 1);
	unsigned ink = jet (J_TEXTE3);
	if (!l0.open)
	{
		char t[900]; pack_text (b, it, t, sizeof t);
		draw_time (c, y, ROW_H, ln.mins);
		text_box (t, c.x, y, ROW_H, ink, PX_SMALL, f_sans (), text_room ());
		return;
	}
	for (int i = 0; i < it.n; i++, y += ROW_H)
	{
		const Line &l = buf_line (b, it.first + i);
		char t[800];
		if (l.ev == JE_NICK) snprintf (t, sizeof t, "%s \xE2\x86\x92 %s", l.nick, l.text);
		else snprintf (t, sizeof t, "%s %s%s%s%s", l.ev == JE_JOIN ? "\xE2\x86\x92" : "\xE2\x86\x90", l.nick, l.text[0] ? " (" : "", l.text, l.text[0] ? ")" : "");
		draw_time (c, y, ROW_H, l.mins);
		text_box (t, c.x, y, ROW_H, ink, PX_SMALL, f_sans (), text_room ());
	}
}
static void draw_rows (const char *s, int x, int y, int maxw, int rowh, unsigned ink, int px, long fnt, int first = 0)
{
	for (int r = 0; ; r++)
	{
		const char *nx;
		int n = wrap_row (s, r == 0 ? maxw - first : maxw, px, fnt, &nx);
		draw_n (s, n, r == 0 ? x + first : x, y, rowh, ink, px, fnt);
		y += rowh;
		if (!*nx) break;
		s = nx;
	}
}
static void draw_line (Buf *b, const Line &l, const Line *p, int y, int h)
{
	Cols c = cols ();
	if (b->kind == BK_SERVER && l.kind != LK_ERROR)		// the server's: the time, the text, mono (a MOTD's drawing)
	{
		draw_time (c, y, ROW_MONO, l.mins);
		draw_rows (l.text, c.nx, y, s_vw - c.nx - 18, ROW_MONO, l.kind == LK_NOTICE ? alert_text () : jet (J_TEXTE2), PX_SMALL, f_mono ());
		return;
	}
	int px = ui_px ();
	bool open = opens (l, p);
	if (open || l.kind == LK_ERROR) y += 6;
	if (l.mention)
	{
		ui_fill (0, y, s_vw, h - (open ? 6 : 0), mix (jet (J_SURFACE), jet (J_ACCENT), 28));
		ui_fill (0, y, 3, h - (open ? 6 : 0), jet (J_ACCENT));
	}
	if (l.kind == LK_EVENT)					// a topic, a mode, a kick: their own row
	{
		draw_time (c, y, ROW_H, l.mins);
		draw_rows (l.text, c.x, y, text_room (), ROW_H, jet (J_TEXTE2), PX_SMALL, f_sans ());
		return;
	}
	if (l.kind == LK_ERROR)
	{
		draw_time (c, y, ROW_H, l.mins);
		text_right ("\xE2\x9A\xA0", c.nx, y, c.nw, ROW_H, jet (J_DANGER), px, f_sans ());
		draw_rows (l.text, c.x, y, text_room (), ROW_H, danger_text (), px, f_sans ());
		return;
	}
	if (l.kind == LK_INFO) { draw_rows (l.text, c.x, y, text_room (), ROW_H, jet (J_TEXTE2), px, f_sans ()); return; }
	if (l.kind == LK_ACTION)
	{
		draw_time (c, y, ROW_H, l.mins);
		text_right ("\xE2\x80\xA2", c.nx, y, c.nw, ROW_H, jet (J_TEXTE3), px, f_sans ());
		int first = text_w (l.nick, px, f_boldital ()) + 6;
		text_box (l.nick, c.x, y, ROW_H, nick_color (l.nick), px, f_boldital (), 0);
		draw_rows (l.text, c.x, y, text_room (), ROW_H, jet (J_TEXTE1), px, f_ital (), first);
		return;
	}
	if (open)
	{
		draw_time (c, y, ROW_H, l.mins);
		unsigned ink = l.kind == LK_NOTICE ? alert_text () : nick_color (l.nick);
		text_right (l.nick, c.nx, y, c.nw, ROW_H, ink, px, f_bold ());
	}
	draw_rows (l.text, c.x, y, text_room (), ROW_H, jet (J_TEXTE1), px, f_sans ());
}
static void chat_paint (long, long w, long h)
{
	Buf *b = g_buf[g_cur];
	s_vw = (int) w; s_vh = (int) h;
	ui_fill (0, 0, w, h, jet (J_SURFACE));
	if (!s_nitems)
	{
		char line[120];
		const char *a = TR ("No messages yet"), *c = TR ("Connect to a server to start.");
		if (b->kind != BK_SERVER) { snprintf (line, sizeof line, TR ("Say hello to %s."), b->name); c = line; }
		else if (g_state != ST_OFFLINE) a = TR ("No server messages yet");
		text_center (a, 0, (int) h / 2 - 22, (int) w, 22, jet (J_TEXTE2), ui_px (), f_bold ());
		text_center (c, 0, (int) h / 2, (int) w, 20, jet (J_TEXTE3), PX_SMALL, f_sans ());
		return;
	}
	int y = content_y ();
	const Line *prev = 0;
	for (int i = 0; i < s_nitems; i++)
	{
		const Item &it = s_items[i];
		if (y + it.h > 0 && y < h)
		{
			int yy = y, hh = it.h;
			if (it.mark)
			{
				const char *t = TR ("New");
				int tw = text_w (t, PX_SMALL, f_bold ());
				ui_fill (0, yy + ROW_H / 2, w - tw - 28, 1, jet (J_ACCENT));
				text_box (t, (int) w - tw - 20, yy, ROW_H, jet (J_ACCENT), PX_SMALL, f_bold (), 0);
				yy += ROW_H; hh -= ROW_H;
			}
			if (it.kind == IT_PACK) draw_pack (b, it, yy);
			else draw_line (b, buf_line (b, it.first), prev, yy, hh);
		}
		y += it.h;
		prev = it.kind == IT_LINE ? &buf_line (b, it.first) : 0;
	}
	s_pillW = 0;
	if (b->scroll > 0 && b->below > 0)
	{
		char t[60]; snprintf (t, sizeof t, TR ("\xE2\x86\x93 %d new"), b->below);
		s_pillW = text_w (t, PX_SMALL, f_bold ()) + 24;
		s_pillX = ((int) w - s_pillW) / 2; s_pillY = (int) h - 34;
		ui_rbox (s_pillX, s_pillY, s_pillW, 24, 12, jet (J_ACCENT), jet (J_ACCENT));
		text_center (t, s_pillX, s_pillY, s_pillW, 24, ink_on (jet (J_ACCENT)), PX_SMALL, f_bold ());
	}
}
static long chat_mouse (long, long kind, long x, long y, long info)
{
	if (kind != 1) return 0;
	Buf *b = g_buf[g_cur];
	int buttons = (int) (info >> 8 & 255);
	if (s_pillW && x >= s_pillX && x < s_pillX + s_pillW && y >= s_pillY && y < s_pillY + 24) { chat_to (b, 0); return 0; }
	int yy = content_y ();
	for (int i = 0; i < s_nitems; i++)
	{
		const Item &it = s_items[i];
		if (y >= yy && y < yy + it.h)
		{
			if (it.kind == IT_PACK) { Line &l = buf_line (b, it.first); if (buttons & 1) { l.open = !l.open; g_viewDirty = true; } }
			else if (buttons & 2) { s_ctxLine = it.first; s_ctxX = ui_ev_field (&ui, EV_X); s_ctxY = ui_ev_field (&ui, EV_Y); }
			break;
		}
		yy += it.h;
	}
	return 0;
}
static long chat_wheel (long, long steps)
{
	Buf *b = g_buf[g_cur];
	chat_to (b, b->scroll + (int) steps * 54);
	return 0;
}
static void chat_create ()
{
	g_chat = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_chat, (long) chat_paint, (long) chat_mouse, 0, (long) chat_wheel);
	g_chatBar = ui_scrollbar (0, 0, 10, 10, 0);
	ui_w_set_parent (g_chatBar, g_chat);
	ui_w_show (g_chatBar, 0);
}
static void chat_move (int x, int y, int w, int h)
{
	s_vw = w; s_vh = h;
	ui_w_move (g_chat, x, y, w, h);
	ui_w_move (g_chatBar, x + w - 12, y, 12, h);		// (in the window's coordinates)
	g_viewDirty = true;
}
// The bar moved.
static void chat_bar_event ()
{
	Buf *b = g_buf[g_cur];
	b->scroll = s_total - (s_vh - 8) - (int) ui_w_get_value (g_chatBar);
	chat_clamp (b);
	ui_view_dirty (g_chat);
}

} // namespace irc

#endif
