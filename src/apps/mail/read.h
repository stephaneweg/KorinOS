//
// read.h -- Mail's right column (Onyx's Apps/mail/read.h, on a ui_view): a conversation. Its subject, its account and
// folder; its messages oldest first, the earlier ones folded (who, the first words, when; a click opens one), the last
// one open: who, to whom, when, Reply / Reply all / Forward, the text (Mail's own HTML renderer, html.h; a text message:
// the plain text with its links found and its quoted lines greyed), the remote pictures held back ("Show the
// pictures"), the attachments (opened in their app, or saved); at the bottom a quick reply. A message not on the card
// yet is fetched (the worker) and shown when it comes. Onyx's WebKit applet (webview.h) is not ported.
//
// The messages are laid out by rd_sync (), from the loop, and only painted by rd_paint: a scroll bar must not be touched
// from inside a view's paint function. An HTML message written for a light page keeps it on a dark theme: a white
// sheet, under its own colours.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef _mail_read_h
#define _mail_read_h

#include "app.h"

namespace mailapp {

// ---- the remote pictures fetched (shared by the messages; kept while Mail runs) ----------------------------------------------
struct Remote { char *url; unsigned *px; int w, h; bool failed, asked; };
static Remote g_remote[64]; static int g_nremote;
static Remote *remote_find (const char *url) { for (int i = 0; i < g_nremote; i++) if (!strcmp (g_remote[i].url, url)) return &g_remote[i]; return 0; }
static Remote *remote_add (const char *url)
{
	if (g_nremote == 64) { free (g_remote[0].url); free (g_remote[0].px); for (int i = 1; i < 64; i++) g_remote[i - 1] = g_remote[i]; g_nremote--; }
	Remote &r = g_remote[g_nremote++]; memset (&r, 0, sizeof r); r.url = sdup (url);
	return &r;
}

// ---- a message shown ------------------------------------------------------------------------------------------------------------
struct Shown;
struct MsgPictures : html::Pictures
{
	Shown *s;
	const unsigned *get (const char *src, int *w, int *h) override;
};
struct Inline { char cid[160]; unsigned *px; int w, h; bool tried; };
struct Shown
{
	Ref ref; bool open, loading, missing;
	char *raw; int rawLen;			// the message's bytes (its .eml)
	Mime *mime;
	html::Html *html; int layW;		// its text laid out at layW
	bool isHtml, remoteOk;
	int att[24]; int natt;			// its attachments' parts
	Inline inl[16]; int ninl;
	int y, h;				// its place in the pane (content coordinates)
	int bodyY;				// where its text starts
	unsigned *zpx; int zw, zh;		// a text wider than the pane: drawn once, scaled down to fit
};
static void shown_free (Shown &s)
{
	free (s.raw); delete s.mime; delete s.html; free (s.zpx);
	for (int i = 0; i < s.ninl; i++) free (s.inl[i].px);
	memset (&s, 0, sizeof s);
}
static html::UiHost *g_host;
static MsgPictures g_pics;			// (one provider, pointed at the message laid out or drawn)
const unsigned *MsgPictures::get (const char *src, int *w, int *h)
{
	if (!s || !s->mime) return 0;
	if (istarts (src, "cid:"))
	{
		const char *cid = src + 4;
		for (int i = 0; i < s->ninl; i++) if (!strcmp (s->inl[i].cid, cid)) { if (!s->inl[i].px) return 0; *w = s->inl[i].w; *h = s->inl[i].h; return s->inl[i].px; }
		if (s->ninl == 16) return 0;
		Inline &in = s->inl[s->ninl++]; memset (&in, 0, sizeof in); scpy (in.cid, cid, sizeof in.cid); in.tried = true;
		int k = s->mime->by_cid (cid); if (k < 0) return 0;
		Buf b; s->mime->decoded (s->mime->parts[k], b);
		if (!b.n) return 0;
		int iw, ih; unsigned *px = image_decode ((const unsigned char *) b.p, b.n, &iw, &ih);
		if (!px) return 0;
		in.px = px; in.w = iw; in.h = ih;
		*w = iw; *h = ih; return px;
	}
	if (!s->remoteOk) return 0;
	const char *url = src; char tmp[1024];
	if (istarts (src, "//")) { snprintf (tmp, sizeof tmp, "https:%s", src); url = tmp; }
	if (!istarts (url, "http://") && !istarts (url, "https://")) return 0;
	Remote *r = remote_find (url);
	if (!r)
	{	// asked of the worker; the pane laid out again when it comes
		r = remote_add (url); r->asked = true;
		Job *j = g_m.job (J_PICTURE, s->ref.acct); scpy (j->uids, url, sizeof j->uids); g_m.worker.push (j);
		return 0;
	}
	if (!r->px) return 0;
	*w = r->w; *h = r->h; return r->px;
}

// a message's parts (once): its attachments, HTML or not
static void shown_parse (Shown &s)
{
	if (s.mime || !s.raw) return;
	s.mime = new Mime; s.mime->parse (s.raw, s.rawLen);
	s.natt = s.mime->attachments (s.att, 24);
	int p = s.mime->body_part (true);
	s.isHtml = p >= 0 && ieq (s.mime->parts[p].sub, "html");
}
static inline bool sheet (const Shown &s) { return s.isHtml && luma (C_FIELD) < 128; }	// (a dark theme: an HTML message on white)
static void css_hex (char *o, unsigned c) { snprintf (o, 12, "#%06x", c & 0xFFFFFF); }

// a message's text: its HTML (or its plain text as HTML), laid out at w
static void shown_layout (Shown &s, int w)
{
	if (!s.raw || !s.open) return;
	shown_parse (s);
	if (!s.html)
	{
		s.html = new html::Html;
		s.html->textColour = sheet (s) ? 0xFF202124u : 0xFF000000u | C_TEXT;
		css_hex (s.html->quoteCss, C_DIM); css_hex (s.html->linkCss, col_accent_text ());
		int p = s.mime->body_part (true);
		Buf t; if (p >= 0) s.mime->text (s.mime->parts[p], t);
		if (s.isHtml) s.html->parse (t.c (), t.n); else s.html->parse_text (t.c (), t.n);
		s.layW = -1;
	}
	if (s.layW != w)
	{
		free (s.zpx); s.zpx = 0;
		g_pics.s = &s; g_host->pics = &g_pics;
		s.html->layout (*g_host, w);
		s.layW = w;
	}
}

// ---- the pane ---------------------------------------------------------------------------------------------------------------------
static long g_read, g_rdBar, g_rdBand, g_quick, g_qsend;
static Shown s_sh[60]; static int s_nsh;
static int s_sy, s_contentH, s_rdW, s_rdH, s_barMax = -1, s_barVis, s_barVal;
static HitList s_rdHits;
static bool s_handCursor;
enum { H_EXPAND = 1, H_REPLY, H_REPLYALL, H_FORWARD, H_PICS, H_ATT_OPEN, H_SAVEALL, H_STAR, H_SENDER };
#define RD_BAR		12
#define QR_H		48
static int rd_pad () { return s_rdW < 440 ? 14 : 22; }
static int text_w_of () { return s_rdW - 2 * rd_pad () - RD_BAR; }

static void rd_clear () { for (int i = 0; i < s_nsh; i++) shown_free (s_sh[i]); s_nsh = 0; s_sy = 0; g_readDirty = true; }
static void rd_fetch (Shown &s)
{
	if (s.raw) return;
	Store &st = *g_m.stores[s.ref.acct]; Folder &f = st.folders[s.ref.folder]; const Msg &m = f.msgs[s.ref.msg];
	s.raw = st.body (f, m.uid, &s.rawLen);
	if (!s.raw && !s.loading)
	{
		if (g_m.accts.a[s.ref.acct].kind == K_POP3) { s.missing = true; return; }
		s.loading = true; g_m.fetch_body (s.ref.acct, s.ref.folder, m.uid);
	}
}
// a conversation's messages: the unread ones and the last open
static void rd_load (const Ref *refs, int n)
{
	rd_clear ();
	for (int i = 0; i < n && i < 60; i++)
	{
		Shown &s = s_sh[s_nsh++]; memset (&s, 0, sizeof s); s.ref = refs[i];
		const Msg &m = g_m.msg (refs[i]);
		s.open = !(m.flags & F_SEEN) || i == n - 1;
	}
	for (int i = 0; i < s_nsh; i++) if (s_sh[i].open) rd_fetch (s_sh[i]);
	set_text (g_quick, "");
	g_readDirty = true;
}
// a body came (the worker): shown if it is one of these
static void rd_body_came (const char *acctId, const char *folder, long uid)
{
	for (int i = 0; i < s_nsh; i++)
	{
		Shown &s = s_sh[i];
		Store &st = *g_m.stores[s.ref.acct];
		if (strcmp (st.acct->id, acctId) || strcmp (st.folders[s.ref.folder].name, folder) || st.folders[s.ref.folder].msgs[s.ref.msg].uid != uid) continue;
		s.loading = false; rd_fetch (s);
		g_readDirty = true;
	}
}
static void rd_relayout () { for (int i = 0; i < s_nsh; i++) if (s_sh[i].html) s_sh[i].layW = -1; g_readDirty = true; }

// a text wider than the pane: drawn at its own width once in a canvas of its own, then averaged down to the pane's
static void rd_zoom (Shown &s, unsigned bg)
{
	int W = s.html->width (), H = s.html->height (), w = text_w_of ();
	if (W <= 0 || H <= 0 || w <= 0) return;
	if (H > 6000) H = 6000;
	long c = ui_canvas (0, 0, W, H); if (!c) return;
	ui_w_show (c, 0);
	ui_canvas_begin (c);
	long cw, ch; uint32_t *px = uikit_canvas_pixels (c, &cw, &ch);
	if (px)
	{
		for (long i = 0; i < cw * ch; i++) px[i] = bg;
		g_host->target (px, (int) cw, (int) ch); g_pics.s = &s; g_host->pics = &g_pics;
		s.html->paint (*g_host, 0, 0, 0, 0, W, H);
	}
	ui_canvas_end ();
	int h = (int) ((long long) H * w / W); if (h < 1) h = 1;
	s.zpx = px ? (unsigned *) malloc (sizeof (unsigned) * (size_t) w * (size_t) h) : 0;
	if (s.zpx)
	{
		s.zw = w; s.zh = h;
		for (int j = 0; j < h; j++)
		{
			int y0 = (int) ((long long) j * H / h), y1 = (int) ((long long) (j + 1) * H / h); if (y1 <= y0) y1 = y0 + 1;
			for (int i = 0; i < w; i++)
			{
				int x0 = (int) ((long long) i * W / w), x1 = (int) ((long long) (i + 1) * W / w); if (x1 <= x0) x1 = x0 + 1;
				unsigned r = 0, g = 0, b = 0, n = 0;
				for (int yy = y0; yy < y1 && yy < H; yy++) for (int xx = x0; xx < x1 && xx < W; xx++) { unsigned p = px[yy * cw + xx]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; n++; }
				s.zpx[j * w + i] = n ? (r / n) << 16 | (g / n) << 8 | (b / n) : bg;
			}
		}
	}
	ui_w_destroy (c);
}
static int rd_view_h () { return s_rdH; }
static void rd_bar_sync ()
{
	int vh = rd_view_h ();
	bool on = s_contentH > vh;
	if (s_barMax != s_contentH || s_barVis != vh) { s_barMax = s_contentH; s_barVis = vh; ui_w_set_range (g_rdBar, 0, s_contentH, vh, 48); }
	ui_w_show (g_rdBar, on);
	if (s_barVal != s_sy) { s_barVal = s_sy; ui_w_set_value (g_rdBar, s_sy); }
}
static void rd_scroll (int v)
{
	int mx = s_contentH - rd_view_h ();
	if (v > mx) v = mx; if (v < 0) v = 0;
	if (v != s_sy) { s_sy = v; ui_view_dirty (g_read); rd_bar_sync (); }
}
// What the paint will want, made from the loop: the fonts, the pictures, the texts laid out, the height.
static void rd_measure ()
{
	int W = s_rdW - RD_BAR, pad = rd_pad (), y = pad;
	if (s_nsh)
	{
		const Msg &last = g_m.msg (s_sh[s_nsh - 1].ref);
		const char *subj = last.subject && last.subject[0] ? last.subject : TR ("(no subject)");
		y += (tw (subj, F_H1, 1) > W - 2 * pad ? 2 : 1) * (fh (F_H1) + 2) + 4 + 32;
	}
	for (int i = 0; i < s_nsh; i++)
	{
		Shown &s = s_sh[i];
		if (i) y += 1;
		if (!s.open) { y += 50; continue; }
		y += 12 + 52;
		if (s.missing || !s.raw) { y += 30; continue; }
		shown_layout (s, text_w_of ());
		if (!s.remoteOk && s.html->remote_pictures ()) y += 42;
		int bh = s.html->height ();
		unsigned bg = s.html->background ();
		bool hasBg = bg >> 24 && (bg & 0xFFFFFF) != (C_FIELD & 0xFFFFFF);
		if (s.html->width () > text_w_of () + 2)
		{
			if (!s.zpx) rd_zoom (s, hasBg ? bg & 0xFFFFFF : sheet (s) ? 0xFFFFFF : C_FIELD);
			if (s.zpx) bh = s.zh;
		}
		y += bh + 14;
		if (s.natt) y += 22 + ((s.natt + 1) / 2) * 52 + 4;
		y += 10;
	}
	s_contentH = y + 20;
}
static void rd_sync ()
{
	rd_measure ();
	rd_bar_sync ();
	int mx = s_contentH - rd_view_h (); if (s_sy > (mx > 0 ? mx : 0)) { s_sy = mx > 0 ? mx : 0; rd_bar_sync (); }
	ui_view_dirty (g_read);
	char ph[220], who2[160]; ph[0] = 0;
	if (s_nsh) { who (g_m.msg (s_sh[s_nsh - 1].ref).from, who2, sizeof who2); snprintf (ph, sizeof ph, TR ("Reply to %s..."), who2); ui_textbox_hint (g_quick, (long) ph); }
}
// the remote pictures held back: a bar ("Show the pictures") at y, which it moves down
static void pictures_bar (int &y, int W, int i, int pad)
{
	int bw = W - 2 * pad;
	unsigned bg = mix (C_FIELD, C_STAR, 40);
	rbox (pad, y, bw, 32, 6, bg);
	icon (I_PICTURE, pad + 10, y + 7, 18, C_DIM, bg);
	text_v (pad + 36, y, 32, TR ("Pictures from the web are hidden."), C_TEXT, F_SMALL, 0, bw - 190);
	const char *lb = TR ("Show the pictures"); int lw = tw (lb, F_SMALL, 1);
	text_v (pad + bw - lw - 12, y, 32, lb, col_accent_text (), F_SMALL, 1);
	s_rdHits.add (pad + bw - lw - 20, y, lw + 20, 32, H_PICS, i);
	y += 42;
}
// a message's attachments at y (moved down past them)
static void attachments (Shown &s, int &y, int W, int i, int pad)
{
	if (!s.natt) return;
	long total = 0; for (int k = 0; k < s.natt; k++) total += s.mime->size_of (s.mime->parts[s.att[k]]);
	char sz[40]; fmt_size (total, sz, sizeof sz);
	char l[120], c[60]; snprintf (c, sizeof c, s.natt > 1 ? TR ("%d attachments") : TR ("%d attachment"), s.natt);
	snprintf (l, sizeof l, "%s  \xC2\xB7  %s  \xC2\xB7  ", c, sz);
	text (pad, y, l, C_DIM, F_SMALL, 1);
	int lx = pad + tw (l, F_SMALL, 1);
	const char *sv = s.natt > 1 ? TR ("Save all") : TR ("Save");
	text (lx, y, sv, col_accent_text (), F_SMALL, 1);
	s_rdHits.add (lx - 4, y - 2, tw (sv, F_SMALL, 1) + 8, 18, H_SAVEALL, i);
	y += 22;
	int cw = (W - 2 * pad - 10) / 2; if (cw > 260) cw = 260;
	for (int k = 0; k < s.natt; k++)
	{
		int cx = pad + (k % 2) * (cw + 10), cy = y + (k / 2) * 52;
		const Part &p = s.mime->parts[s.att[k]];
		outline (cx, cy, cw, 44, 6, C_FIELD, C_LINE);
		bool pic = ieq (p.type, "image");
		icon (pic ? I_PICTURE : I_FILE, cx + 10, cy + 10, 24, pic ? col_accent_text () : C_DIM, C_FIELD);
		const char *nm = p.name[0] ? p.name : pic ? TR ("picture") : TR ("attachment");
		text (cx + 42, cy + 6, nm, C_TEXT, F_SMALL, 1, cw - 50);
		char ps[40]; fmt_size (s.mime->size_of (p), ps, sizeof ps);
		text (cx + 42, cy + 23, ps, C_DIM, F_SMALL);
		s_rdHits.add (cx, cy, cw, 44, H_ATT_OPEN, i, k);
	}
	y += ((s.natt + 1) / 2) * 52 + 4;
}
static void rd_paint (long v, long w0, long h0)
{
	int width = (int) w0, height = (int) h0;
	begin_paint (v);
	ui_fill (0, 0, width, height, C_FIELD);
	s_rdHits.clear ();
	if (!s_nsh)
	{
		int cy = height / 2 - 40;
		icon (I_INBOX, width / 2 - 24, cy - 30, 48, C_FAINT);
		text_c (0, cy + 30, width, 24, g_m.view.convs.n ? TR ("Choose a conversation to read it.") : TR ("Nothing here."), C_DIM, F_MID);
		return;
	}
	int W = width - RD_BAR, pad = rd_pad (), y = pad - s_sy;
	const Msg &last = g_m.msg (s_sh[s_nsh - 1].ref);
	// the subject, the chips
	const char *subj = last.subject && last.subject[0] ? last.subject : TR ("(no subject)");
	{
		// (a long subject over two lines)
		char a[400], b[400]; scpy (a, subj, sizeof a); b[0] = 0;
		if (tw (a, F_H1, 1) > W - 2 * pad)
		{
			int cut = (int) strlen (a);
			while (cut > 0) { while (cut > 0 && a[cut] != ' ') cut--; char t = a[cut]; a[cut] = 0; int ww = tw (a, F_H1, 1); a[cut] = t; if (ww <= W - 2 * pad || cut == 0) break; cut--; }
			if (cut > 0) { scpy (b, a + cut + 1, sizeof b); a[cut] = 0; }
		}
		text (pad, y, a, C_TEXT, F_H1, 1, W - 2 * pad); y += fh (F_H1) + 2;
		if (tw (subj, F_H1, 1) > W - 2 * pad) { if (b[0]) text (pad, y, b, C_TEXT, F_H1, 1, W - 2 * pad); y += fh (F_H1) + 2; }
	}
	y += 4;
	{
		Account &a = g_m.accts.a[s_sh[s_nsh - 1].ref.acct];
		const Folder &f = g_m.stores[s_sh[s_nsh - 1].ref.acct]->folders[s_sh[s_nsh - 1].ref.folder];
		int x = pad;
		Buf fl; mutf7_decode (fl, folder_label (f));
		const char *chips[2] = { a.label, folder_tr (fl.c ()) };
		for (int k = 0; k < 2; k++)
		{
			int cw = tw (chips[k], F_SMALL, 1) + 16;
			unsigned bg = k ? C_LINE : a.colour & 0xFFFFFF;
			rbox (x, y, cw, 20, 10, bg);
			text_c (x, y, cw, 20, chips[k], k ? C_DIM : ink_on (bg), F_SMALL, 1);
			x += cw + 6;
		}
		y += 32;
	}
	// the messages
	for (int i = 0; i < s_nsh; i++)
	{
		Shown &s = s_sh[i];
		const Msg &m = g_m.msg (s.ref);
		s.y = y + s_sy;
		char name[160], date[60]; who (m.from, name, sizeof name); if (!name[0]) scpy (name, TR ("(unknown)"), sizeof name);
		char em[160]; first_email (m.from, em, sizeof em);
		if (i) { ui_fill (pad, y, W - 2 * pad, 1, C_LINE); y += 1; }
		if (!s.open)
		{	// folded: who, the first words, when
			int rh = 50;
			if (y + rh > 0 && y < height)
			{
				avatar (pad + 16, y + rh / 2, 16, name, em);
				fmt_short (m.date, date, sizeof date);
				int dw = tw (date, F_SMALL);
				text (pad + 42, y + 8, name, C_TEXT, F_UI, (m.flags & F_SEEN) ? 0 : 1, W - 2 * pad - 60 - dw);
				text (W - pad - dw, y + 9, date, C_DIM, F_SMALL);
				text (pad + 42, y + 27, m.preview ? m.preview : "", C_DIM, F_SMALL, 0, W - 2 * pad - 50);
			}
			s_rdHits.add (0, y, W, rh, H_EXPAND, i);
			y += rh; s.h = rh;
			continue;
		}
		// the header
		int hy = y + 12;
		avatar (pad + 20, hy + 20, 20, name, em);
		int nx = pad + 52;
		fmt_long (m.date, date, sizeof date);
		int dw = tw (date, F_SMALL);
		text (nx, hy + 1, name, C_TEXT, F_MID, 1, W - nx - dw - pad - 10);
		s_rdHits.add (nx, hy, imin (tw (name, F_MID, 1), W - nx - dw - pad - 10), 20, H_SENDER, i);
		char toLine[300], tos[240]; who (m.to, tos, sizeof tos, true);
		char myEmail[160]; scpy (myEmail, g_m.accts.a[s.ref.acct].email, sizeof myEmail);
		bool toMe = m.to && ifind (m.to, myEmail) && !strchr (m.to, ',');
		snprintf (toLine, sizeof toLine, TR ("%s  \xC2\xB7  to %s"), em, toMe ? TR ("me") : tos[0] ? tos : TR ("(nobody)"));
		text (nx, hy + 22, toLine, C_DIM, F_SMALL, 0, W - nx - pad - 110);
		text (W - pad - dw, hy + 2, date, C_DIM, F_SMALL);
		// reply, reply all, forward, star
		int ix = W - pad - 4 * 26;
		static const int IC[4] = { I_REPLY, I_REPLYALL, I_FORWARD, I_STAR };
		static const int HK[4] = { H_REPLY, H_REPLYALL, H_FORWARD, H_STAR };
		for (int k = 0; k < 4; k++)
		{
			bool on = k == 3 && (m.flags & F_FLAGGED);
			icon (k == 3 ? (on ? I_STAR : I_STAR_O) : IC[k], ix + k * 26 + 3, hy + 21, 18, on ? C_STAR : C_DIM);
			s_rdHits.add (ix + k * 26, hy + 18, 26, 24, HK[k], i);
		}
		y = hy + 52;
		// the text
		if (s.missing) { text (pad, y, TR ("This message is not on the card any more."), C_DIM); y += 30; }
		else if (!s.raw) { text (pad, y, s.loading ? TR ("Getting the message...") : "", C_DIM); y += 30; }
		else if (s.html)
		{
			if (!s.remoteOk && s.html->remote_pictures ()) pictures_bar (y, W, i, pad);
			s.bodyY = y + s_sy;
			int bh = s.html->height ();
			unsigned bg = s.html->background ();
			bool hasBg = bg >> 24 && (bg & 0xFFFFFF) != (C_FIELD & 0xFFFFFF);
			int tw0 = text_w_of ();
			if (s.zpx)
			{	// wider than the pane: its picture, scaled down
				bh = s.zh;
				for (int j = 0; j < s.zh; j++) { int yy = y + j; if (yy < 0 || yy >= g_ph) continue; memcpy (g_px + yy * g_pw + pad, s.zpx + j * s.zw, sizeof (unsigned) * (size_t) s.zw); }
			}
			else if (y < height && y + bh > 0)
			{
				if (hasBg) ui_fill (pad - 8, y - 8, tw0 + 16, bh + 16, bg & 0xFFFFFF);
				else if (sheet (s)) rbox (pad - 8, y - 8, tw0 + 16, bh + 16, 4, 0xFFFFFF);
				g_host->target (g_px, (int) g_pw, (int) g_ph); g_pics.s = &s; g_host->pics = &g_pics;
				s.html->paint (*g_host, pad, y, 0, 0, W, height);
			}
			y += bh + 14;
			attachments (s, y, W, i, pad);
		}
		y += 10;
		s.h = y + s_sy - s.y;
	}
}
// The link of a message's text under a point of the pane, 0 when there is none.
static const char *link_under (int mx, int my)
{
	int pad = rd_pad ();
	for (int i = 0; i < s_nsh; i++)
	{
		Shown &s = s_sh[i]; if (!s.html || !s.open) continue;
		int dy = s.bodyY - s_sy;
		int lx = mx - pad, ly = my - dy;
		if (s.zpx && s.zw > 0) { lx = (int) ((long long) lx * s.html->width () / s.zw); ly = (int) ((long long) ly * s.html->width () / s.zw); }
		const char *href = s.html->link_at (lx, ly);
		if (href) return href;
	}
	return 0;
}
static char s_link[1200];				// (a link clicked: the loop opens it)
static long rd_mouse (long, long kind, long x, long y, long info)
{
	int mx = (int) x, my = (int) y;
	if (kind == 0)
	{
		bool hand = link_under (mx, my) || s_rdHits.at (mx, my);
		if (hand != s_handCursor) { s_handCursor = hand; ui_cursor (hand ? UC_HAND : UC_ARROW); }
		return 0;
	}
	if (kind == 3) { if (s_handCursor) { s_handCursor = false; ui_cursor (UC_ARROW); } return 0; }
	if (kind != 1 || !(info >> 8 & 1)) return 0;
	const Hit *h = s_rdHits.at (mx, my);
	if (h) { later (L_READ, *h); return 0; }
	const char *href = link_under (mx, my);
	if (href) { Hit k; memset (&k, 0, sizeof k); k.kind = -1; later (L_READ, k); scpy (s_link, href, sizeof s_link); }
	return 0;
}
static long rd_wheel (long, long steps) { rd_scroll (s_sy + (int) steps * 48); return 0; }

static void open_link (const char *href)
{
	if (istarts (href, "mailto:"))
	{
		char a[200]; scpy (a, href + 7, sizeof a); char *q = strchr (a, '?'); if (q) *q = 0;
		Buf d; html::decode_text (d, a, (int) strlen (a));
		compose_to ("", d.c ());
		return;
	}
	if (istarts (href, "http://") || istarts (href, "https://"))
	{
		// asked first: a link in a mail may not go where it says
		char q[700]; snprintf (q, sizeof q, "%s\n\n%.600s", TR ("Open this link in Jet?"), href);
		if (ask (q, UM_YESNO) != 1) return;
		spawn_app (WEB_PROGRAM, href);
	}
}
static void safe_name (const char *in, char *out, int cap)
{
	int k = 0; for (const char *p = in; *p && k < cap - 1; p++) out[k++] = strchr ("/\\:*?\"<>|", *p) ? '_' : *p;
	out[k] = 0; if (!k) scpy (out, "attachment", cap);
}
static bool write_att (Shown &s, int k, const char *path)
{
	Buf b; s.mime->decoded (s.mime->parts[s.att[k]], b);
	return file_save (path, b.c (), b.n);
}
// the program that opens a file's extension: the manifests' "opens = ..." (SD0:/apps/<name>.app/app.txt)
static bool app_for (const char *name, char *out, int cap, char *label, int lcap)
{
	const char *dot = strrchr (name, '.'); if (!dot || !dot[1]) return false;
	DIR *d = opendir ("SD0:/apps"); if (!d) return false;
	bool found = false;
	struct dirent *e;
	while (!found && (e = readdir (d)))
	{
		int n = (int) strlen (e->d_name);
		if (n < 5 || strcmp (e->d_name + n - 4, ".app")) continue;
		char p[300]; snprintf (p, sizeof p, "SD0:/apps/%s/app.txt", e->d_name);
		int len; char *b = file_read (p, &len); if (!b) continue;
		const char *o = strstr (b, "\nopens");
		if (o && (o = strchr (o, '=')))
		{
			const char *end = strchr (o, '\n'); if (!end) end = o + strlen (o);
			int el = (int) strlen (dot + 1);
			for (const char *q = o + 1; q < end; )
			{
				while (q < end && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
				const char *w = q; while (q < end && *q != ' ' && *q != '\t' && *q != '\r') q++;
				if (q - w == el && ieqn (w, dot + 1, el)) found = true;
			}
		}
		if (found)
		{
			snprintf (out, cap, "SD0:/apps/%s/main", e->d_name);
			scpy (label, e->d_name, lcap); char *x = strrchr (label, '.'); if (x) *x = 0;
		}
		free (b);
	}
	closedir (d);
	return found;
}
static void attachment_menu (Shown &s, int k, int x, int y)
{
	const Part &p = s.mime->parts[s.att[k]];
	char nm[200]; safe_name (p.name[0] ? p.name : "attachment", nm, sizeof nm);
	char tmp[300]; snprintf (tmp, sizeof tmp, HOME_DIR "/tmp/%s", nm);
	char prog[320], app[40]; bool can = app_for (nm, prog, sizeof prog, app, sizeof app);
	char ol[90];
	if (can) snprintf (ol, sizeof ol, TR ("Open (%s)"), app); else snprintf (ol, sizeof ol, "\x01%s", TR ("Open"));
	long r = popup (x, y, { ol, TR ("Save as...") });
	if (r == 0 && can)
	{
		mkdirs (HOME_DIR "/tmp");
		if (!write_att (s, k, tmp) || !spawn_app (prog, tmp)) ask (TR ("The attachment could not be written to the card."));
	}
	else if (r == 1)
	{
		char out[300]; mkdirs ("SD0:/home/documents");
		if (ui_file_save ((long) "SD0:/home/documents", (long) nm, (long) out, sizeof out) && !write_att (s, k, out)) ask (TR ("The attachment could not be written."));
	}
}
static void save_all (Shown &s)
{
	char dir[300]; scpy (dir, "SD0:/home/documents", sizeof dir);
	mkdirs (dir);
	if (!ui_folder_choose ((long) dir, (long) dir, sizeof dir)) return;
	int ok = 0;
	for (int k = 0; k < s.natt; k++)
	{
		char nm[200]; safe_name (s.mime->parts[s.att[k]].name, nm, sizeof nm);
		char path[500]; snprintf (path, sizeof path, "%s/%s", dir, nm);
		if (write_att (s, k, path)) ok++;
	}
	char m[400]; snprintf (m, sizeof m, TR ("%d of %d saved in %s."), ok, s.natt, dir);
	status_note (m);
}
static void sender_menu (Shown &s, int x, int y)
{
	const Msg &m = g_m.msg (s.ref);
	Addr a[2]; int n = parse_addrs (m.from ? m.from : "", a, 2); if (!n) return;
	bool known = g_m.contacts.find (a[0].email) >= 0;
	long r = popup (x, y, { TR ("Write to them"), known ? TR ("Show in the contacts") : TR ("Add to the contacts"), TR ("Copy the address") });
	if (r == 0) compose_to (a[0].name, a[0].email);
	else if (r == 1)
	{
		if (!known) { Contact c; memset (&c, 0, sizeof c); scpy (c.name, a[0].name[0] ? a[0].name : a[0].email, sizeof c.name); scpy (c.email, a[0].email, sizeof c.email); g_m.contacts.add (c); g_m.contacts.save (); status_note (TR ("Added to the contacts.")); }
		show_contacts (true);
	}
	else if (r == 2) ui_clip_set ((long) a[0].email, -1);
}
// what a click on the pane asked for (the loop: menus and dialogues may open)
static void rd_act ()
{
	Hit h = g_later.h; int x = g_later.x, y = g_later.y;
	if (h.kind == -1) { open_link (s_link); return; }
	if (h.a < 0 || h.a >= s_nsh) return;
	Shown &s = s_sh[h.a];
	switch (h.kind)
	{
	case H_EXPAND: s.open = true; rd_fetch (s); if (!(g_m.msg (s.ref).flags & F_SEEN)) g_m.set_flag (&s.ref, 1, F_SEEN, true); g_readDirty = true; break;
	case H_REPLY: compose_new (1, &s.ref); break;
	case H_REPLYALL: compose_new (2, &s.ref); break;
	case H_FORWARD: compose_new (3, &s.ref); break;
	case H_STAR: { bool on = !(g_m.msg (s.ref).flags & F_FLAGGED); g_m.set_flag (&s.ref, 1, F_FLAGGED, on); refresh_all (); g_readDirty = true; break; }
	case H_PICS: s.remoteOk = true; s.layW = -1; g_readDirty = true; break;
	case H_SENDER: sender_menu (s, x, y); break;
	case H_ATT_OPEN: attachment_menu (s, h.b, x, y); break;
	case H_SAVEALL: save_all (s); break;
	}
}
static long rd_key (long, long hid, long, long)
{
	if (hid == KEY_PGDN || hid == 0x2C) { rd_scroll (s_sy + rd_view_h () - 40); return 0; }
	if (hid == KEY_PGUP) { rd_scroll (s_sy - rd_view_h () + 40); return 0; }
	if (hid == KEY_DOWN) { rd_scroll (s_sy + 48); return 0; }
	if (hid == KEY_UP) { rd_scroll (s_sy - 48); return 0; }
	return -1;
}
static void rd_create ()
{
	g_read = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_read, (long) rd_paint, (long) rd_mouse, (long) rd_key, (long) rd_wheel);
	g_rdBar = ui_scrollbar (0, 0, 10, 10, 0);
	ui_w_set_parent (g_rdBar, g_read);
	g_rdBand = ui_panel (0, 0, 10, 10, 0);
	g_quick = ui_textbox (0, 0, 10, ctl_h (), 0);
	ui_w_set_parent (g_quick, g_rdBand);
	g_qsend = ui_pushbutton (0, 0, 70, 0, (long) TR ("Send"), 0);
	ui_w_set_parent (g_qsend, g_rdBand);
}
static void rd_move (int x, int y, int w, int h)
{
	bool q = s_nsh > 0;
	int c = ctl_h ();
	int vh = q ? h - QR_H : h;
	bool again = w != s_rdW;
	s_rdW = w; s_rdH = vh;
	ui_w_move (g_read, x, y, w, vh);
	ui_w_move (g_rdBar, x + w - RD_BAR, y, RD_BAR, vh);
	ui_w_show (g_rdBand, q);
	if (q)
	{
		ui_w_move (g_rdBand, x, y + vh, w, QR_H);
		ui_w_move (g_quick, x + 12, y + vh + (QR_H - c) / 2, w - 24 - 78, c);
		ui_w_move (g_qsend, x + w - 12 - 70, y + vh + (QR_H - c) / 2, 70, c);
	}
	if (again) rd_relayout (); else g_readDirty = true;
}

} // namespace mailapp

#endif
