//
// compose.h -- writing a message, in place of the list and the reading pane (Onyx's Apps/mail/compose.h): From (the
// account), To / Cc / Bcc completed from the contacts and the addresses already written to as one types (Up / Down,
// Enter or Tab takes one), the subject, the text, the attachments (added from the card, or kept from a forwarded
// message); Send (Ctrl+Enter), Save as draft, Discard. A reply quotes the message ("On ..., X wrote:" and its lines with
// "> "), keeps its conversation (In-Reply-To, References); a forward carries its attachments. The message goes as plain
// text and as simple HTML (its paragraphs, its links, the quote with a bar), in UTF-8 (mime.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef _mail_compose_h
#define _mail_compose_h

#include "read.h"

namespace mailapp {

struct Attach { char name[200]; char type[80]; char *data; int n; };

static long g_cp, g_cTitle, g_cL[5], g_cFrom, g_cTo, g_cCc, g_cBcc, g_cSubj, g_cCcBtn, g_cBody, g_cChips, g_cSend, g_cAttach, g_cDraft, g_cDiscard, g_sug;
static struct
{
	bool ccOn; int mode; Ref about; bool haveAbout;
	char inReplyTo[220], references[1100];
	Attach att[16]; int natt;
	char title[60];
} C;
static char s_fromText[12][220];
static int  s_cW, s_cH, s_cX, s_cY;

// ---- the suggestions under an address field ----------------------------------------------------------------------------
static Contacts::Match s_sugM[8]; static int s_sugN, s_sugSel; static long s_sugOwner; static bool s_sugOn;
#define SUG_ROW	40
static void sug_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FACE);
	outline (0, 0, (int) w, (int) h, 6, jet (J_ELEVE), C_LINE);
	for (int i = 0; i < s_sugN; i++)
	{
		int y = 4 + i * SUG_ROW;
		const Contacts::Match &m = s_sugM[i];
		if (i == s_sugSel) rbox (4, y, (int) w - 8, SUG_ROW - 2, 5, col_sel ());
		avatar (24, y + 19, 14, m.name[0] ? m.name : m.email, m.email);
		text (46, y + 3, m.name[0] ? m.name : m.email, C_TEXT, F_UI, 1, (int) w - 56);
		text (46, y + 21, m.email, C_DIM, F_SMALL, 0, (int) w - 56);
	}
}
static void sug_hide ()
{
	if (!s_sugOn) return;
	s_sugOn = false; ui_w_show (g_sug, 0);
	ui_view_dirty (g_read); layout ();
}
static const char *addr_current (long w) { const char *t = tx (w); const char *c = strrchr (t, ','); c = c ? c + 1 : t; while (*c == ' ') c++; return c; }
static void sug_take (int i)
{
	long w = s_sugOwner;
	if (i < 0 || i >= s_sugN || !w) return;
	char t[2048]; const char *all = tx (w); int keep = (int) (addr_current (w) - all);
	if (keep >= (int) sizeof t) keep = sizeof t - 1;
	memcpy (t, all, (size_t) keep); t[keep] = 0;
	const Contacts::Match &m = s_sugM[i];
	char one[300];
	if (m.name[0]) { bool q = strpbrk (m.name, ",;<>@\"") != 0; snprintf (one, sizeof one, q ? "\"%s\" <%s>, " : "%s <%s>, ", m.name, m.email); }
	else snprintf (one, sizeof one, "%s, ", m.email);
	if (keep && t[keep - 1] != ' ') scpy (t + keep, " ", (int) sizeof t - keep);
	int k = (int) strlen (t); scpy (t + k, one, (int) sizeof t - k);
	set_text (w, t);
	int n = (int) strlen (t); ui_textbox_select (w, n, n);
	sug_hide ();
	ui_w_focus (w);
}
static void sug_update (long w)
{
	const char *c = addr_current (w);
	s_sugN = strlen (c) >= 1 ? g_m.contacts.complete (c, s_sugM, 6) : 0;
	s_sugSel = 0; s_sugOwner = w;
	if (!s_sugN) { sug_hide (); return; }
	long fx = w == g_cTo ? 0 : w == g_cCc ? 1 : 2;
	int x = s_cX + 20 + 62, y = s_cY + 44 + (2 + (int) fx) * 36 - 4, sw = imin (s_cW - 90, 360);
	ui_w_move (g_sug, x, y, sw, 8 + s_sugN * SUG_ROW);
	if (!s_sugOn) { s_sugOn = true; ui_w_show (g_sug, 1); }
	ui_view_dirty (g_sug);
}
static long sug_mouse (long, long kind, long, long y, long)
{
	int i = ((int) y - 4) / SUG_ROW;
	if (i < 0 || i >= s_sugN) return 0;
	if (kind == 0 && i != s_sugSel) { s_sugSel = i; ui_view_dirty (g_sug); }
	if (kind == 1) { Hit h; memset (&h, 0, sizeof h); h.a = i; later (L_SUG, h); }
	return 0;
}
// a key while the suggestions show: Up, Down, Enter, Tab, Esc. true: taken.
static bool sug_key (int hid)
{
	if (!s_sugOn || ui_w_get_focus () != s_sugOwner) return false;
	if (hid == KEY_DOWN) { s_sugSel = (s_sugSel + 1) % s_sugN; ui_view_dirty (g_sug); return true; }
	if (hid == KEY_UP) { s_sugSel = (s_sugSel + s_sugN - 1) % s_sugN; ui_view_dirty (g_sug); return true; }
	if (hid == KEY_ENTER || hid == KEY_TAB) { sug_take (s_sugSel); return true; }
	if (hid == KEY_ESC) { sug_hide (); return true; }
	return false;
}

// ---- the attachments' chips ------------------------------------------------------------------------------------------------
static HitList s_chips;
static void chips_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FACE);
	s_chips.clear ();
	int x = 0;
	for (int i = 0; i < C.natt; i++)
	{
		char sz[30]; fmt_size (C.att[i].n, sz, sizeof sz);
		char l[260]; snprintf (l, sizeof l, "%s (%s)", C.att[i].name, sz);
		int cw = imin (tw (l, F_SMALL) + 54, 260);
		if (x + cw > w) break;
		outline (x, 2, cw, 26, 13, C_FIELD, C_LINE);
		icon (I_CLIP, x + 8, 7, 16, C_DIM);
		text_v (x + 28, 2, 26, l, C_TEXT, F_SMALL, 0, cw - 54);
		icon (I_CLOSE, x + cw - 22, 8, 14, C_DIM);
		s_chips.add (x + cw - 26, 2, 26, 26, 1, i);
		x += cw + 8;
	}
}
static long chips_mouse (long, long kind, long x, long y, long info)
{
	if (kind != 1 || !(info >> 8 & 1)) return 0;
	const Hit *h = s_chips.at ((int) x, (int) y);
	if (h) later (L_CHIP, *h);
	return 0;
}

// ---- the page -----------------------------------------------------------------------------------------------------------------
static void c_accounts_changed ()
{
	int n = imin (g_m.accts.n, 12);
	long sel = ui_w_get_value (g_cFrom);
	ui_list_clear (g_cFrom);
	for (int i = 0; i < n; i++)
	{
		snprintf (s_fromText[i], sizeof s_fromText[i], "%s <%s>", g_m.accts.a[i].name[0] ? g_m.accts.a[i].name : g_m.accts.a[i].label, g_m.accts.a[i].email);
		ui_list_add (g_cFrom, (long) s_fromText[i], i);
	}
	ui_w_set_value (g_cFrom, sel >= 0 && sel < n ? sel : 0);
}
static void c_create ()
{
	g_cp = ui_panel (0, 0, 10, 10, 0);
	g_cTitle = ui_label (0, 0, 10, 24, (long) "", ULB_TITLE); ui_w_set_parent (g_cTitle, g_cp);
	static const char *const L[5] = { "From", "To", "Cc", "Bcc", "Subject" };
	for (int i = 0; i < 5; i++) { g_cL[i] = ui_label (0, 0, 60, ctl_h (), (long) TR (L[i]), ULB_DIM); ui_w_set_parent (g_cL[i], g_cp); }
	g_cCcBtn = ui_pushbutton (0, 0, 64, 0, (long) TR ("Cc Bcc"), UB_GHOST); ui_w_set_parent (g_cCcBtn, g_cp);	// (first: Tab goes from To to the subject)
	g_cFrom = ui_dropdown (0, 0, 10, ctl_h (), (long) ""); ui_w_set_parent (g_cFrom, g_cp);
	long *f[4] = { &g_cTo, &g_cCc, &g_cBcc, &g_cSubj };
	for (int i = 0; i < 4; i++) { *f[i] = ui_textbox (0, 0, 10, ctl_h (), 0); ui_w_set_parent (*f[i], g_cp); ui_w_set_range (*f[i], 0, i == 3 ? 300 : 2000, 0, 0); }
	g_cBody = ui_textarea (0, 0, 10, 10, UTA_VSCROLL | UTA_HSCROLL | UTA_FORMTAB); ui_w_set_parent (g_cBody, g_cp);
	g_cChips = ui_view (0, 0, 10, 30, 0); ui_view_on (g_cChips, (long) chips_paint, (long) chips_mouse, 0, 0); ui_w_set_parent (g_cChips, g_cp);
	g_cSend = ui_pushbutton (0, 0, 92, 0, (long) TR ("Send"), UB_DEFAULT); ui_w_set_parent (g_cSend, g_cp);
	ui_w_tip (g_cSend, (long) TR ("Ctrl+Enter"));
	g_cAttach = ui_pushbutton (0, 0, 100, 0, (long) TR ("Attach..."), 0); ui_w_set_parent (g_cAttach, g_cp);
	g_cDraft = ui_pushbutton (0, 0, 110, 0, (long) TR ("Save draft"), 0); ui_w_set_parent (g_cDraft, g_cp);
	g_cDiscard = ui_pushbutton (0, 0, 92, 0, (long) TR ("Discard"), UB_GHOST); ui_w_set_parent (g_cDiscard, g_cp);
	g_sug = ui_view (0, 0, 10, 10, 0); ui_view_on (g_sug, (long) sug_paint, (long) sug_mouse, 0, 0); ui_w_set_parent (g_sug, g_cp); ui_w_show (g_sug, 0);
}
static void c_place (int x, int y0, int w, int h)
{
	int pad = 20, lw = 62, c = ctl_h (), row = 36, fx = x + pad + lw, fw = w - 2 * pad - lw;
	s_cX = x; s_cY = y0; s_cW = w; s_cH = h;
	ui_w_move (g_cp, x, y0, w, h);
	int y = y0 + 8;
	ui_w_move (g_cTitle, x + pad, y, w - 2 * pad, 26); y += 36;
	ui_w_move (g_cL[0], x + pad, y, lw, c); ui_w_move (g_cFrom, fx, y, fw, c); y += row;
	ui_w_move (g_cL[1], x + pad, y, lw, c); ui_w_move (g_cTo, fx, y, fw - (C.ccOn ? 0 : 72), c);
	ui_w_show (g_cCcBtn, !C.ccOn); ui_w_move (g_cCcBtn, fx + fw - 64, y, 64, c); y += row;
	ui_w_show (g_cL[2], C.ccOn); ui_w_show (g_cCc, C.ccOn); ui_w_show (g_cL[3], C.ccOn); ui_w_show (g_cBcc, C.ccOn);
	if (C.ccOn)
	{
		ui_w_move (g_cL[2], x + pad, y, lw, c); ui_w_move (g_cCc, fx, y, fw, c); y += row;
		ui_w_move (g_cL[3], x + pad, y, lw, c); ui_w_move (g_cBcc, fx, y, fw, c); y += row;
	}
	ui_w_move (g_cL[4], x + pad, y, lw, c); ui_w_move (g_cSubj, fx, y, fw, c); y += row + 2;
	int by = y0 + h - c - 10, ay = by - (C.natt ? 34 : 0);
	ui_w_move (g_cBody, x + pad, y, w - 2 * pad, ay - y - 6);
	ui_w_show (g_cChips, C.natt > 0);
	if (C.natt) { ui_w_move (g_cChips, x + pad, ay, w - 2 * pad, 30); ui_view_dirty (g_cChips); }
	ui_w_move (g_cSend, x + pad, by, 92, c);
	ui_w_move (g_cAttach, x + pad + 100, by, 100, c);
	ui_w_move (g_cDraft, x + pad + 208, by, 110, c);
	ui_w_move (g_cDiscard, x + w - pad - 92, by, 92, c);
	if (w < 440) ui_w_move (g_cDraft, x + pad + 208, by, imax (30, w - 2 * pad - 208 - 100), c);
}
static void c_reset ()
{
	for (int i = 0; i < C.natt; i++) free (C.att[i].data);
	C.natt = 0; C.ccOn = false; C.haveAbout = false; C.inReplyTo[0] = C.references[0] = 0;
	set_text (g_cTo, ""); set_text (g_cCc, ""); set_text (g_cBcc, ""); set_text (g_cSubj, ""); set_text (g_cBody, "");
	s_sugOn = false; ui_w_show (g_sug, 0);
}
static bool c_empty () { return !tx (g_cTo)[0] && !tx (g_cSubj)[0] && !tx (g_cBody)[0] && !C.natt; }

// ---- the message made from the pane -------------------------------------------------------------------------------------------
static const char *mime_type_of (const char *name)
{
	static const struct { const char *e, *t; } T[] = { { "pdf", "application/pdf" }, { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
		{ "gif", "image/gif" }, { "webp", "image/webp" }, { "txt", "text/plain" }, { "html", "text/html" }, { "htm", "text/html" }, { "zip", "application/zip" },
		{ "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" }, { "odt", "application/vnd.oasis.opendocument.text" },
		{ "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" }, { "ods", "application/vnd.oasis.opendocument.spreadsheet" },
		{ "csv", "text/csv" }, { "mp3", "audio/mpeg" }, { "ogg", "audio/ogg" }, { "wav", "audio/wav" }, { "mp4", "video/mp4" }, { "eml", "message/rfc822" },
		{ "rtf", "application/rtf" }, { "card", "text/plain" }, { "bas", "text/plain" } };
	const char *dot = strrchr (name, '.');
	if (dot) for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (ieq (dot + 1, T[i].e)) return T[i].t;
	return "application/octet-stream";
}
// the text as simple HTML: its paragraphs and line breaks, its links, the quoted lines with a bar
static void text_to_html (const char *t, Buf &o)
{
	o.add ("<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body><div style=\"font-family:Arial,Helvetica,sans-serif;font-size:14px;line-height:1.45\">");
	bool inQuote = false;
	const char *p = t;
	while (*p)
	{
		const char *e = strchr (p, '\n'); int n = e ? (int) (e - p) : (int) strlen (p);
		bool q = n && *p == '>';
		if (q && !inQuote) { o.add ("<blockquote style=\"margin:0 0 0 4px;border-left:3px solid #c8ccd0;padding-left:10px;color:#5f6368\">"); inQuote = true; }
		if (!q && inQuote) { o.add ("</blockquote>"); inQuote = false; }
		const char *l = p; int ln = n;
		if (q) { l++; ln--; if (ln && *l == ' ') { l++; ln--; } }
		// the line, its links made links
		for (int i = 0; i < ln; )
		{
			if ((ln - i > 8 && !memcmp (l + i, "https://", 8)) || (ln - i > 7 && !memcmp (l + i, "http://", 7)))
			{
				int j = i; while (j < ln && !strchr (" <>\"", l[j])) j++;
				while (j > i && strchr (".,;:!?)", l[j - 1])) j--;
				o.add ("<a href=\""); html::Html::escape (o, l + i, j - i); o.add ("\">"); html::Html::escape (o, l + i, j - i); o.add ("</a>");
				i = j; continue;
			}
			int j = i; while (j < ln && !(l[j] == 'h' && (j + 7 < ln) && (!memcmp (l + j, "http://", 7) || !memcmp (l + j, "https://", 8)))) j++;
			if (j == i) j = i + 1;
			html::Html::escape (o, l + i, j - i);
			i = j;
		}
		o.add ("<br>\n");
		if (!e) break;
		p = e + 1;
	}
	if (inQuote) o.add ("</blockquote>");
	o.add ("</div></body></html>\n");
}

} // namespace mailapp

#endif
