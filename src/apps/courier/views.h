//
// views.h -- the three editors a tab shows, on UIKit: a request (its URL bar, its Params / Authorization / Headers /
// Body / Tests / Settings, its response below: Body / Cookies / Headers / Tests / Console), an environment, a
// collection. The logic of Onyx's Apps/courier/views.h; a control is a ui_* handle, the widgets are made once and bound
// to the tab shown. A text given by the program sends no notice: only what the user does reaches control ().
//
#ifndef _courier_views_h
#define _courier_views_h

#include "app.h"

namespace cr {

enum { TEXT_MAX = 900000 };			// (a ui_textarea holds 1 MiB: past it, cut; Save keeps it all)
enum { SEC_PARAMS, SEC_AUTH, SEC_HEADERS, SEC_BODY, SEC_TESTS, SEC_SETTINGS };
enum { RS_BODY, RS_COOKIES, RS_HEADERS, RS_TESTS, RS_CONSOLE };
enum { BV_PRETTY, BV_RAW, BV_PREVIEW };

// the request
static long g_pReq, g_name, g_save, g_code, g_method, g_url, g_send, g_vsplit;
static Bar g_sections;
static long g_sec[6], g_lParams, g_lHeaders, g_bParamsBulk, g_bHeadersBulk;
static KVTable g_params, g_headers, g_form, g_urlenc, g_tests, g_captures;
static AuthPanel g_auth;
static long g_bodyMode, g_rawLang, g_beautify, g_bodyBulk, g_raw, g_noBody, g_binPick, g_binName;
static long g_lTests, g_lCaptures, g_follow, g_verify, g_lTimeout, g_timeout, g_lEmpty, g_lRedirects;
// the response
static Bar g_respTabs;
static long g_pResp, g_pBody, g_view, g_ctype, g_find, g_bCopy, g_bSave, g_bodyText, g_bodyImg;
static long g_lHdrs, g_lCookies, g_pResults, g_lResults, g_listResults, g_console, g_respEmpty, g_sending, g_bCancel;
static unsigned *s_imgPx; static int s_imgW, s_imgH;
// an environment
static long g_pEnv, g_envName, g_envActive, g_envExport, g_envHelp;
static KVTable g_envVars;
// a collection
static long g_pColl, g_collName, g_collNewReq, g_collExport, g_collDesc, g_collInfo, g_pCollVars, g_pCollAuth;
static Bar g_collTabs;
static AuthPanel g_collAuth;
static KVTable g_collVars;
// where they are
static int s_rx, s_ry, s_rw, s_rh;		// the area of the tab's editor
static int s_spin, s_modeW, s_viewW, s_bulkW, s_saveW, s_copyW;

extern "C" unsigned *image_decode (const unsigned char *b, int n, int *w, int *h);

// ---- the params <-> the URL's query ----------------------------------------------------------------------------------------
static void params_from_url (Request &r)
{
	KVList off, all;
	for (int i = 0; i < r.params.size (); i++) if (!r.params[i].on && !r.params[i].blank ()) off.push (r.params[i]);
	const char *qm = strchr (r.url.c (), '?');
	if (qm)
		for (const char *p = qm + 1; *p; )
		{
			const char *e = p; while (*e && *e != '&' && *e != '#') e++;
			if (e > p)
			{
				const char *eq = p; while (eq < e && *eq != '=') eq++;
				KV kv; kv.key.set (p, (int) (eq - p)); if (eq < e) kv.value.set (eq + 1, (int) (e - eq - 1));
				for (int i = 0; i < r.params.size (); i++) if (r.params[i].on && r.params[i].key.eq (kv.key.c ())) { kv.desc = r.params[i].desc; break; }
				all.push (kv);
			}
			if (*e != '&') break;
			p = e + 1;
		}
	for (int i = 0; i < off.size (); i++) all.push (off[i]);
	r.params = all;
}
static void url_from_params (Request &r)
{
	const char *u = r.url.c ();
	const char *qm = strchr (u, '?');
	const char *hash = strchr (u, '#');
	Str base; base.set (u, qm ? (int) (qm - u) : hash ? (int) (hash - u) : r.url.len ());
	Str q;
	for (int i = 0; i < r.params.size (); i++)
	{
		const KV &kv = r.params[i];
		if (!kv.on || kv.blank ()) continue;
		if (!q.empty ()) q.add ('&');
		q.add (kv.key.c ());
		if (!kv.value.empty () || strchr (u, '=')) { q.add ('='); q.add (kv.value.c ()); }
	}
	if (!q.empty ()) { base.add ('?'); base.add (q.c ()); }
	if (hash) { Str h (hash); base.add (h.c ()); }
	r.url = base;
}

// ---- the request's editor: filling it from a tab, and its edits going to the tab ----------------------------------------
static void req_counts ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	Request &r = t->req;
	int n = kv_count (g_params); bar_count (g_sections, SEC_PARAMS, n ? n : -1);
	n = kv_count (g_headers); bar_count (g_sections, SEC_HEADERS, n ? n : -1);
	bar_mark (g_sections, SEC_AUTH, r.auth.type != AUTH_INHERIT && r.auth.type != AUTH_NONE);
	bar_mark (g_sections, SEC_BODY, r.bodyMode != BODY_NONE);
	int nt = 0; for (int i = 0; i < r.tests.size (); i++) if (r.tests[i].on && !r.tests[i].key.empty ()) nt++;
	for (int i = 0; i < r.captures.size (); i++) if (r.captures[i].on && !r.captures[i].key.empty ()) nt++;
	bar_count (g_sections, SEC_TESTS, nt ? nt : -1);
	bar_mark (g_sections, SEC_SETTINGS, !r.followRedirects || r.timeoutMs || r.insecure);
}
static void edited ()
{
	Tab *t = cur_tab ();
	if (!t) return;
	t->dirty = true;
	req_counts ();
	app_tab_changed ();
}
static void show_section (int s)
{
	for (int i = 0; i < 6; i++) if (i != s) ui_w_show (g_sec[i], 0);	// (what goes first: what comes is drawn over it)
	ui_w_show (g_sec[s], 1);
	bar_select (g_sections, s);
	Tab *t = cur_tab (); if (t) t->reqSection = s;
}
static void body_mode_shown ()
{
	Tab *t = cur_tab (); if (!t) return;
	int m = t->req.bodyMode;
	ui_w_show (g_form.panel, m == BODY_FORM); ui_w_show (g_urlenc.panel, m == BODY_URLENC); ui_w_show (g_raw, m == BODY_RAW);
	ui_w_show (g_binPick, m == BODY_BINARY); ui_w_show (g_binName, m == BODY_BINARY);
	ui_w_show (g_noBody, m == BODY_NONE);
	ui_w_show (g_rawLang, m == BODY_RAW); ui_w_show (g_beautify, m == BODY_RAW && (t->req.rawLang == RAW_JSON || t->req.rawLang == RAW_XML));
	ui_w_show (g_bodyBulk, m == BODY_FORM || m == BODY_URLENC);
	if (m == BODY_FORM || m == BODY_URLENC) set_text (g_bodyBulk, (m == BODY_FORM ? g_form : g_urlenc).inBulk ? TR ("Key-Value Edit") : TR ("Bulk Edit"));
	set_text (g_binName, t->req.binary.empty () ? TR ("No file chosen: its bytes are sent as the body, its type from its name.") : t->req.binary.c ());
}
static void inherit_text ()
{
	Tab *t = cur_tab (); if (!t) return;
	Collection *c = 0; Item *it = tab_item (t, &c);
	g_auth.inherit.clear ();
	if (!c) g_auth.inherit.add (TR ("This request is not in a collection: it inherits nothing (sent without authorization). Save it in a collection, or choose a type."));
	else
	{
		const Auth &a = effective_auth (t->req, it, c);
		const char *from = c->name.c ();
		for (const Item *p = it ? it->parent : 0; p && p->parent; p = p->parent) if (p->auth.type != AUTH_INHERIT) { from = p->name.c (); break; }
		g_auth.inherit.addf (TR ("This request is using %s from \"%s\"."), TR (a.type == AUTH_NONE ? "No Auth" : AUTH_NAMES[a.type]), from);
	}
	auth_sync (g_auth);
}

// ---- the response ---------------------------------------------------------------------------------------------------------
static void text_in (long w, const char *s, int n)
{
	if (n <= TEXT_MAX) { set_text (w, s); return; }
	Str c; c.set (s, TEXT_MAX);
	c.add ('\n'); c.add (TR ("... (cut here: Save keeps it all)")); c.add ('\n');
	set_text (w, c.c ());
}
static void resp_body_fill ()
{
	Tab *t = cur_tab ();
	if (!t || !t->resp) return;
	Response &r = *t->resp;
	const char *ct = r.header ("Content-Type"); if (!ct) ct = "";
	{ char b[80]; s_copy (b, ct, sizeof b); char *semi = strchr (b, ';'); if (semi) *semi = 0; set_text (g_ctype, b); }
	int ctl = (int) strlen (ct);
	bool json = s_findi (ct, ctl, "json") >= 0, xml = s_findi (ct, ctl, "xml") >= 0, html = s_findi (ct, ctl, "html") >= 0;
	bool image = s_findi (ct, ctl, "image/") >= 0;
	const char *b = r.body ? r.body : ""; int n = r.bodyLen;
	if (!json && !xml && !html && n) { int i = 0; while (i < n && is_space (b[i])) i++; if (i < n && (b[i] == '{' || b[i] == '[')) json = true; else if (i < n && b[i] == '<') xml = true; }
	int view = t->bodyView;
	free (s_imgPx); s_imgPx = 0;
	if (view == BV_PREVIEW && image) s_imgPx = image_decode ((const unsigned char *) b, n, &s_imgW, &s_imgH);
	ui_w_show (g_bodyImg, s_imgPx != 0);
	ui_w_show (g_bodyText, s_imgPx == 0);
	if (s_imgPx) { ui_view_dirty (g_bodyImg); return; }
	// a binary body: its bytes as hexadecimal, a line a 16
	bool binary = false;
	for (int i = 0; i < n && i < 2048; i++) { unsigned char c = (unsigned char) b[i]; if (c == 0 || c < 8) { binary = true; break; } }
	Str out;
	if (view == BV_PREVIEW && image) { out = TR ("This image cannot be previewed."); out.add ('\n'); }
	else if (binary)
	{
		int m = imin (n, 64 * 1024);
		for (int i = 0; i < m; i += 16)
		{
			out.addf ("%08x  ", i);
			for (int k = 0; k < 16; k++) { if (i + k < m) out.addf ("%02x ", (unsigned char) b[i + k]); else out.add ("   "); if (k == 7) out.add (' '); }
			out.add (' ');
			for (int k = 0; k < 16 && i + k < m; k++) { unsigned char c = (unsigned char) b[i + k]; out.add (c >= 32 && c < 127 ? (char) c : '.'); }
			out.add ('\n');
		}
		if (n > m) { out.addf (TR ("... (%d bytes in all)"), n); out.add ('\n'); }
	}
	else if (view == BV_PRETTY && json && pretty_json (out, b, n)) {}
	else if (view == BV_PRETTY && xml && !html && pretty_xml (out, b, n)) {}
	else if (view == BV_PREVIEW && html) html_text (out, b, n);
	else out.set (b, n);
	text_in (g_bodyText, out.c (), out.len ());
}
static void bar_layout_resp ();
static void resp_shown ()
{
	Tab *t = cur_tab ();
	if (!t) return;
	bool busy = t->job != 0, has = t->resp != 0 && !busy;
	int s = t->respSection;
	bool ok = has && t->resp->status >= 0;
	long w[7] = { g_sending, g_respEmpty, g_pBody, g_lCookies, g_lHdrs, g_pResults, g_console };
	bool on[7] = { busy, !busy && !ok && s != RS_CONSOLE, ok && s == RS_BODY, ok && s == RS_COOKIES, ok && s == RS_HEADERS, ok && s == RS_TESTS, !busy && s == RS_CONSOLE };
	for (int i = 0; i < 7; i++) if (!on[i]) ui_w_show (w[i], 0);	// (what goes first: what comes is drawn over it)
	for (int i = 0; i < 7; i++) if (on[i]) ui_w_show (w[i], 1);
	bar_select (g_respTabs, s);
	ui_view_dirty (g_respTabs.v);
	ui_view_dirty (g_respEmpty);
}
static void list_row (long list, const char *a, const char *b, const char *c = 0)
{
	Str s;
	const char *cols[3] = { a, b, c };
	for (int i = 0; i < 3 && cols[i]; i++)
	{
		if (i) s.add ('\t');
		for (const char *p = cols[i]; *p; p++) s.add (*p == '\t' || *p == '\n' || *p == '\r' ? ' ' : *p);
	}
	ui_list_add (list, (long) s.c (), 0);
}
static void cookie_text (Str &out, const Cookie &c)
{
	out.addf ("%s%s  path %s%s%s", c.hostOnly ? "" : ".", c.domain.c (), c.path.c (), c.secure ? "  secure" : "", c.httpOnly ? "  HttpOnly" : "");
	if (c.expires) { Str e; iso_time (e, c.expires); out.add ("  "); out.add (TR ("expires")); out.add (' '); out.add (e.c ()); }
}
static void resp_fill ()
{
	Tab *t = cur_tab ();
	ui_list_clear (g_lHdrs); ui_list_clear (g_lCookies); ui_list_clear (g_listResults);
	if (t && t->resp)
	{
		Response &r = *t->resp;
		for (int i = 0; i < r.headers.size (); i++) list_row (g_lHdrs, r.headers[i].key.c (), r.headers[i].value.c ());
		for (int i = 0; i < r.setCookies.size (); i++)
		{
			const Cookie &c = r.setCookies[i];
			Str d; cookie_text (d, c);
			list_row (g_lCookies, c.name.c (), c.value.c (), d.c ());
		}
		int pass = 0, tot = 0, set = 0;
		for (int i = 0; i < t->results.size (); i++)
		{
			TestResult &x = t->results[i];
			if (x.capture) set++; else { tot++; if (x.pass) pass++; }
			list_row (g_listResults, x.capture ? TR ("SET") : x.pass ? TR ("PASS") : TR ("FAIL"), x.name.c (), x.detail.c ());
		}
		if (t->results.size ())
		{
			Str h; h.addf (TR ("%d / %d passed"), pass, tot);
			if (set) { h.add ("    "); h.addf (TR ("%d variable(s) set"), set); }
			set_text (g_lResults, h.c ());
		}
		else set_text (g_lResults, TR ("No tests for this request: add checks in its Tests tab (status 200, json.id exists...)."));
		text_in (g_console, r.console.c (), r.console.len ());
		bar_count (g_respTabs, RS_HEADERS, r.headers.size () ? r.headers.size () : -1);
		bar_count (g_respTabs, RS_COOKIES, r.setCookies.size () ? r.setCookies.size () : -1);
		g_respTabs.badge[RS_TESTS] = pass == tot ? 0 : on_field (C_BAD);
		bar_count (g_respTabs, RS_TESTS, tot ? pass : -1);
		ui_w_set_value (g_view, t->bodyView);
		resp_body_fill ();
	}
	else
	{
		set_text (g_console, "");
		bar_count (g_respTabs, RS_HEADERS, -1); bar_count (g_respTabs, RS_COOKIES, -1); bar_count (g_respTabs, RS_TESTS, -1);
	}
	resp_shown ();
}
// a tab into the editors: the tables point at nothing that may go
static void unbind_all ()
{
	for (int i = 0; i < s_nkv; i++) kv_set_rows (*s_kv[i], 0);
	g_auth.auth = 0; g_collAuth.auth = 0;
}
static void bulk_buttons ()
{
	set_text (g_bParamsBulk, g_params.inBulk ? TR ("Key-Value Edit") : TR ("Bulk Edit"));
	set_text (g_bHeadersBulk, g_headers.inBulk ? TR ("Key-Value Edit") : TR ("Bulk Edit"));
}
static void bind_request (Tab *t)
{
	Request &r = t->req;
	set_text (g_name, r.name.c ());
	ui_w_set_value (g_method, method_index (r.method.c ()));
	set_text (g_url, r.url.c ());
	kv_set_rows (g_params, &r.params);
	kv_set_rows (g_headers, &r.headers);
	kv_set_rows (g_form, &r.form);
	kv_set_rows (g_urlenc, &r.urlenc);
	kv_set_rows (g_tests, &r.tests);
	kv_set_rows (g_captures, &r.captures);
	auth_bind (g_auth, &r.auth);
	inherit_text ();
	ui_w_set_value (g_bodyMode, r.bodyMode);
	ui_w_set_value (g_rawLang, r.rawLang);
	set_text (g_raw, r.raw.c ());
	ui_w_set_value (g_follow, r.followRedirects);
	ui_w_set_value (g_verify, !r.insecure);
	{ char b[16]; if (r.timeoutMs) snprintf (b, sizeof b, "%d", r.timeoutMs); else b[0] = 0; set_text (g_timeout, b); }
	body_mode_shown ();
	show_section (t->reqSection);
	req_counts ();
	resp_fill ();
}

// ---- the pieces drawn ---------------------------------------------------------------------------------------------------
static void resp_chip (long W, long H)
{
	Tab *t = cur_tab ();
	if (!t || !t->resp || t->job) return;
	Response &r = *t->resp;
	int x = (int) W - 8;
	char sz[32], tm[32], st[96];
	if (r.status < 0) snprintf (st, sizeof st, "%s", TR ("Error")); else snprintf (st, sizeof st, "%d %s", r.status, r.reason.c ());
	human_size (sz, sizeof sz, r.bodyLen);
	snprintf (tm, sizeof tm, "%d ms", r.msTotal);
	int ws = tw (sz), wt = tw (tm), wst = tw (st, F_UI, 1);
	int room = (int) W - g_respTabs.x[g_respTabs.n] - 8;
	// what fits: the status always, then the time, then the size
	bool showT = wst + 16 + 16 + wt + 8 <= room, showS = showT && wst + 16 + 32 + wt + ws + 8 <= room;
	if (wst + 16 > room) return;
	unsigned c = on_field (C_OK);
	if (showS) { x -= ws; text_v (x, 0, (int) H - 2, sz, c); x -= 16; }
	if (showT) { x -= wt; text_v (x, 0, (int) H - 2, tm, c); x -= 16; }
	x -= wst + 16;
	unsigned sc = status_color (r.status);
	rbox (x, ((int) H - 24) / 2, wst + 16, 24, 6, mix (C_FACE, sc, 34));
	text_v (x + 8, 0, (int) H - 2, st, sc, F_UI, 1);
}
static void empty_paint (long, long W, long H)
{
	ui_fill (0, 0, W, H, C_FACE);
	Tab *t = cur_tab ();
	if (t && t->resp && t->resp->status < 0)
	{
		int cy = (int) H / 2 - 40;
		const char *h = t->resp->cancelled ? TR ("The request was cancelled") : TR ("Could not send the request");
		unsigned red = t->resp->cancelled ? C_DIM : on_field (C_BAD);
		text_c (0, cy, (int) W, 22, h, red, F_MID, 1);
		int w = imin ((int) W - 60, 560);
		text_wrap (((int) W - w) / 2, cy + 28, w, t->resp->error.c (), C_DIM);
		return;
	}
	text_c (0, (int) H / 2 - 26, (int) W, 22, TR ("Enter the URL and click Send to get a response"), C_DIM, F_MID, 1);
	int w = imin ((int) W - 40, 520);
	const char *s = TR ("Ctrl+Enter sends too; {{variables}} come from the environment chosen, top right.");
	if (tw (s) <= w) text_c (0, (int) H / 2, (int) W, 20, s, C_FAINT);
	else text_wrap (((int) W - w) / 2, (int) H / 2, w, s, C_FAINT);
}
static void sending_paint (long, long W, long H)
{
	ui_fill (0, 0, W, H, C_FACE);
	int cx = (int) W / 2, cy = (int) H / 2 - 36;
	for (int i = 0; i < 8; i++)
	{
		float a = (i + s_spin) * 3.14159265f / 4;
		int r = 14 - 0;
		unsigned c = mix (C_FACE, C_ACCENT, 40 + (i * 216) / 8);
		ui_vp_disc (V (cx) + (int) (V (r) * cosf (a)), V (cy) + (int) (V (r) * sinf (a)), V (3), c);
	}
	Tab *t = cur_tab ();
	char b[64]; unsigned ms = t && t->job ? now_ms () - t->started : 0;
	snprintf (b, sizeof b, TR ("Sending request... %u.%u s"), ms / 1000, ms / 100 % 10);
	text_c (0, cy + 24, (int) W, 20, b, C_DIM, F_UI, 1);
}
static void image_paint (long v, long W, long H)
{
	ui_fill (0, 0, W, H, C_FIELD);
	if (!s_imgPx || s_imgW <= 0 || s_imgH <= 0) return;
	begin_paint (v);
	// fitted, never enlarged
	long dw = s_imgW, dh = s_imgH;
	if (dw > W) { dh = dh * W / dw; dw = W; }
	if (dh > H) { dw = dw * H / dh; dh = H; }
	if (dw < 1 || dh < 1 || !g_px) return;
	int ox = (int) (W - dw) / 2, oy = (int) (H - dh) / 2;
	unsigned bg = C_FIELD;
	for (long y = 0; y < dh; y++)
	{
		uint32_t *d = g_px + (y + oy) * g_pw + ox;
		const unsigned *s = s_imgPx + (y * s_imgH / dh) * s_imgW;
		for (long x = 0; x < dw; x++)
		{
			unsigned p = s[x * s_imgW / dw], a = p >> 24;
			d[x] = a == 255 ? (p & 0xFFFFFF) : (unsigned) ui_mix (bg, p & 0xFFFFFF, a) & 0xFFFFFF;
		}
	}
}

// ---- building ---------------------------------------------------------------------------------------------------------------
static void on_table ();
static void on_params_table ();
static void on_auth_changed ();
static void on_env_vars ();
static void on_coll_vars ();
static void on_coll_auth ();

static long label_in (long panel, const char *s, int flags = 0)
{
	long l = ui_label (0, 0, 10, ctl_h (), (long) s, flags); in_panel (l, panel); return l;
}
static long button_in (long panel, const char *s, int w = 0, int flags = 0)
{
	long b = ui_pushbutton (0, 0, w, ctl_h (), (long) s, flags); in_panel (b, panel); return b;
}
static long list_in (long panel, const char *c0, int w0, const char *c1, int w1, const char *c2 = 0, int w2 = 0)
{
	long l = ui_list (0, 0, 10, 10, UL_HEADER | UL_NOSORT | UL_HSCROLL);
	ui_list_column (l, (long) c0, w0, 0); ui_list_column (l, (long) c1, w1, 0);
	if (c2) ui_list_column (l, (long) c2, w2, 0);
	in_panel (l, panel);
	return l;
}
// a ui_segmented's width: its words, UK_SEG_PAD each
static int seg_w (const char *s)
{
	int w = 8;
	char b[120]; s_copy (b, s, sizeof b);
	for (char *p = b; p; )
	{
		char *e = strchr (p, '|'); if (e) *e = 0;
		w += tw (p, F_UI, 1) + 24;
		p = e ? e + 1 : 0;
	}
	return w;
}
static void views_build ()
{
	int ch = ctl_h ();
	// ---- the request ----
	g_pReq = ui_panel (0, 0, 10, 10, 0);
	g_name = ui_textbox (0, 0, 10, ch, 0); in_panel (g_name, g_pReq); ui_textbox_hint (g_name, (long) TR ("Untitled Request")); ui_w_tip (g_name, (long) TR ("The request's name"));
	g_save = button_in (g_pReq, TR ("Save"), btn_w (TR ("Save"))); ui_w_tip (g_save, (long) TR ("Save in its collection (Ctrl+S)"));
	g_code = button_in (g_pReq, "</>", 44, UB_GHOST); ui_w_tip (g_code, (long) TR ("The request as code: cURL, HTTP, Python, JavaScript"));
	g_method = ui_dropdown (0, 0, 104, ch, (long) "GET|POST|PUT|PATCH|DELETE|HEAD|OPTIONS"); in_panel (g_method, g_pReq);
	g_url = ui_textbox (0, 0, 10, ch, 0); in_panel (g_url, g_pReq); ui_textbox_hint (g_url, (long) TR ("Enter the URL, or paste a cURL command"));
	g_send = button_in (g_pReq, TR ("Send"), 84, UB_DEFAULT); ui_w_tip (g_send, (long) TR ("Send the request (Ctrl+Enter)"));
	bar_new (g_sections, g_pReq, "Params|Authorization|Headers|Body|Tests|Settings");
	for (int i = 0; i < 6; i++) { g_sec[i] = ui_panel (0, 0, 10, 10, 0); in_panel (g_sec[i], g_pReq); }
	// params, headers
	g_lParams = label_in (g_sec[SEC_PARAMS], TR ("Query Params"), ULB_BOLD);
	s_bulkW = imax (btn_w (TR ("Bulk Edit")), btn_w (TR ("Key-Value Edit")));
	g_bParamsBulk = button_in (g_sec[SEC_PARAMS], TR ("Bulk Edit"), s_bulkW, UB_GHOST);
	kv_init (g_params, g_sec[SEC_PARAMS], "Key", "Value", "Description", "Key", "Value", true, false, on_params_table);
	auth_init (g_auth, g_sec[SEC_AUTH], false, on_auth_changed);
	g_lHeaders = label_in (g_sec[SEC_HEADERS], TR ("Sent too: User-Agent, Accept, Accept-Encoding, Host, Connection, the cookies of the jar"), ULB_DIM);
	g_bHeadersBulk = button_in (g_sec[SEC_HEADERS], TR ("Bulk Edit"), s_bulkW, UB_GHOST);
	kv_init (g_headers, g_sec[SEC_HEADERS], "Key", "Value", "Description", "Key", "Value", true, false, on_table);
	// body
	long sb = g_sec[SEC_BODY];
	g_bodyMode = ui_segmented (0, 0, 10, ch, (long) TR ("none|form-data|urlencoded|raw|binary")); in_panel (g_bodyMode, sb);
	s_modeW = seg_w (TR ("none|form-data|urlencoded|raw|binary"));
	{
		char l[80] = ""; for (int i = 0; i < RAW_COUNT; i++) { if (i) strcat (l, "|"); strcat (l, RAW_NAMES[i]); }
		g_rawLang = ui_dropdown (0, 0, 120, ch, (long) l); in_panel (g_rawLang, sb);
	}
	g_beautify = button_in (sb, TR ("Beautify"), 92, UB_GHOST); ui_w_tip (g_beautify, (long) TR ("Indent the JSON or the XML (Ctrl+B)"));
	g_bodyBulk = button_in (sb, TR ("Bulk Edit"), s_bulkW, UB_GHOST);
	kv_init (g_form, sb, "Key", "Value", "Description", "Key", "Value", true, true, on_table);
	kv_init (g_urlenc, sb, "Key", "Value", "Description", "Key", "Value", true, false, on_table);
	g_raw = ui_textarea (0, 0, 10, 10, UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_FORMTAB); in_panel (g_raw, sb);
	g_noBody = label_in (sb, TR ("This request does not have a body."), ULB_DIM | ULB_CENTER);
	g_binPick = button_in (sb, TR ("Select File"), 120);
	g_binName = label_in (sb, "", ULB_DIM);
	// tests
	long st = g_sec[SEC_TESTS];
	g_lTests = label_in (st, TR ("Tests: status 200, json.data.id exists, header.Content-Type contains json, time < 800"), ULB_DIM);
	kv_init (g_tests, st, "Check", "Expected", "", "status", "200", false, false, on_table);
	g_lCaptures = label_in (st, TR ("Captures: variables set from the response, in the active environment (else the globals)"), ULB_DIM);
	kv_init (g_captures, st, "Variable", "From the response", "", "token", "json.access_token", false, false, on_table);
	// settings
	long ss = g_sec[SEC_SETTINGS];
	g_follow = ui_switch (0, 0, 10, ch, (long) TR ("Automatically follow redirects"), 1); in_panel (g_follow, ss);
	g_verify = ui_switch (0, 0, 10, ch, (long) TR ("Verify the server's TLS certificate (/etc/ca-bundle)"), 1); in_panel (g_verify, ss);
	g_lTimeout = label_in (ss, TR ("Request timeout (ms)"));
	g_timeout = ui_textbox (0, 0, 110, ch, 0); in_panel (g_timeout, ss); ui_textbox_hint (g_timeout, (long) "30000");
	g_lRedirects = label_in (ss, TR ("301, 302 and 303 go on as GET; 307 and 308 keep the method and the body."), ULB_DIM);

	g_vsplit = ui_splitter (0, 100, 10, 6, 100, 400); in_panel (g_vsplit, g_pReq);

	// ---- the response ----
	g_pResp = ui_panel (0, 0, 10, 10, 0); in_panel (g_pResp, g_pReq);
	bar_new (g_respTabs, g_pResp, "Body|Cookies|Headers|Tests|Console");
	g_respTabs.extra = resp_chip;
	g_pBody = ui_panel (0, 0, 10, 10, 0); in_panel (g_pBody, g_pResp);
	g_view = ui_segmented (0, 0, 190, ch, (long) TR ("Pretty|Raw|Preview")); in_panel (g_view, g_pBody);
	s_viewW = seg_w (TR ("Pretty|Raw|Preview"));
	g_ctype = label_in (g_pBody, "", ULB_DIM);
	g_find = ui_textbox (0, 0, 130, ch, 0); in_panel (g_find, g_pBody); ui_textbox_hint (g_find, (long) TR ("Find in the body"));
	g_bCopy = button_in (g_pBody, TR ("Copy"), s_copyW, UB_GHOST); ui_w_tip (g_bCopy, (long) TR ("Copy the body"));
	s_saveW = btn_w (TR ("Save...")); s_copyW = btn_w (TR ("Copy"));
	g_bSave = button_in (g_pBody, TR ("Save..."), s_saveW, UB_GHOST); ui_w_tip (g_bSave, (long) TR ("Save the response to a file"));
	g_bodyText = ui_textarea (0, 0, 10, 10, UTF_READONLY | UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL); in_panel (g_bodyText, g_pBody);
	g_bodyImg = ui_view (0, 0, 10, 10, 0); ui_view_on (g_bodyImg, (long) image_paint, 0, 0, 0); in_panel (g_bodyImg, g_pBody);
	g_lCookies = list_in (g_pResp, TR ("Name"), 140, TR ("Value"), 220, TR ("Domain, path, expiry"), 340);
	g_lHdrs = list_in (g_pResp, TR ("Key"), 220, TR ("Value"), 520);
	g_pResults = ui_panel (0, 0, 10, 10, 0); in_panel (g_pResults, g_pResp);
	g_lResults = label_in (g_pResults, "", ULB_BOLD);
	g_listResults = list_in (g_pResults, TR ("Result"), 70, TR ("Check"), 260, TR ("Found"), 340);
	g_console = ui_textarea (0, 0, 10, 10, UTF_READONLY | UTF_MONO | UTA_VSCROLL | UTA_HSCROLL); in_panel (g_console, g_pResp);
	g_respEmpty = ui_view (0, 0, 10, 10, 0); ui_view_on (g_respEmpty, (long) empty_paint, 0, 0, 0); in_panel (g_respEmpty, g_pResp);
	g_sending = ui_view (0, 0, 10, 10, 0); ui_view_on (g_sending, (long) sending_paint, 0, 0, 0); in_panel (g_sending, g_pResp);
	g_bCancel = button_in (g_sending, TR ("Cancel"), 100); ui_w_tip (g_bCancel, (long) TR ("Stop the request (Esc)"));

	// ---- an environment ----
	g_pEnv = ui_panel (0, 0, 10, 10, 0);
	g_envName = ui_textbox (0, 0, 10, ch, 0); in_panel (g_envName, g_pEnv);
	g_envActive = button_in (g_pEnv, TR ("Set Active"), 110);
	g_envExport = button_in (g_pEnv, TR ("Export..."), btn_w (TR ("Export...")), UB_GHOST);
	g_envHelp = label_in (g_pEnv, "", ULB_DIM);
	kv_init (g_envVars, g_pEnv, "Variable", "Value", "", "Add a new variable", "", false, false, on_env_vars);

	// ---- a collection ----
	g_pColl = ui_panel (0, 0, 10, 10, 0);
	g_collName = ui_textbox (0, 0, 10, ch, 0); in_panel (g_collName, g_pColl);
	g_collNewReq = button_in (g_pColl, TR ("Add Request"), btn_w (TR ("Add Request")));
	g_collExport = button_in (g_pColl, TR ("Export..."), btn_w (TR ("Export...")), UB_GHOST);
	bar_new (g_collTabs, g_pColl, "Overview|Authorization|Variables");
	g_collInfo = label_in (g_pColl, "", ULB_DIM);
	g_collDesc = ui_textarea (0, 0, 10, 10, UTA_VSCROLL | UTA_FORMTAB); in_panel (g_collDesc, g_pColl);
	g_pCollAuth = ui_panel (0, 0, 10, 10, 0); in_panel (g_pCollAuth, g_pColl);
	auth_init (g_collAuth, g_pCollAuth, true, on_coll_auth);
	g_pCollVars = ui_panel (0, 0, 10, 10, 0); in_panel (g_pCollVars, g_pColl);
	kv_init (g_collVars, g_pCollVars, "Variable", "Value", "Description", "Add a new variable", "", true, false, on_coll_vars);
}

// ---- the layouts --------------------------------------------------------------------------------------------------------------
static void section_place (int x, int y, int w, int h)
{
	int ch = ctl_h ();
	for (int i = 0; i < 6; i++) ui_w_move (g_sec[i], x, y, w, h);
	int px = x + 12, pw = w - 24, y1 = y + 4;
	// params, headers: a line, its table
	ui_w_move (g_lParams, px, y1, pw - s_bulkW - 10, ch); ui_w_move (g_bParamsBulk, x + w - 12 - s_bulkW, y1, s_bulkW, ch);
	kv_place (g_params, px, y1 + ch + 2, pw, imax (KV_HEAD + KV_ROW, h - ch - 10));
	auth_place (g_auth, x, y, w, h);
	ui_w_move (g_lHeaders, px, y1, pw - s_bulkW - 10, ch); ui_w_move (g_bHeadersBulk, x + w - 12 - s_bulkW, y1, s_bulkW, ch);
	kv_place (g_headers, px, y1 + ch + 2, pw, imax (KV_HEAD + KV_ROW, h - ch - 10));
	// the body: the modes, what they need at their right (under them when it has no room)
	int mw = imin (s_modeW, pw);
	ui_w_move (g_bodyMode, px, y1, mw, ch);
	bool two = mw + 12 + imax (120 + 8 + 92, s_bulkW) > pw;
	int oy = two ? y1 + ch + 4 : y1, ox = two ? px : px + mw + 12;
	ui_w_move (g_rawLang, ox, oy, 120, ch);
	ui_w_move (g_beautify, ox + 128, oy, 92, ch);
	ui_w_move (g_bodyBulk, x + w - 12 - s_bulkW, y1 + (two ? ch + 4 : 0), s_bulkW, ch);
	int by = oy + ch + 4, bh = imax (KV_HEAD + KV_ROW, y + h - 6 - by);
	kv_place (g_form, px, by, pw, bh);
	kv_place (g_urlenc, px, by, pw, bh);
	ui_w_move (g_raw, px, by, pw, bh);
	ui_w_move (g_noBody, px, by + 10, pw, ch);
	ui_w_move (g_binPick, px, by + 4, 120, ch);
	ui_w_move (g_binName, px + 130, by + 4, pw - 130, ch);
	// tests, captures
	int half = (h - 8 - 2 * (ch + 2)) / 2;
	ui_w_move (g_lTests, px, y1, pw, ch);
	kv_place (g_tests, px, y1 + ch, pw, imax (KV_HEAD + KV_ROW, half));
	int cy = y1 + ch + half + 2;
	ui_w_move (g_lCaptures, px, cy, pw, ch);
	kv_place (g_captures, px, cy + ch, pw, imax (KV_HEAD + KV_ROW, half));
	// settings
	int sy = y1 + 4, row = ch + 8;
	ui_w_move (g_follow, px, sy, imin (pw, 420), ch);
	ui_w_move (g_verify, px, sy + row, imin (pw, 480), ch);
	ui_w_move (g_lTimeout, px, sy + 2 * row, 190, ch);
	ui_w_move (g_timeout, px + 196, sy + 2 * row, 110, ch);
	ui_w_move (g_lRedirects, px, sy + 3 * row, pw, ch);
}
static void response_place (int x, int y, int w, int h)
{
	int ch = ctl_h ();
	ui_w_move (g_pResp, x, y, w, h);
	ui_w_move (g_respTabs.v, x, y, w, 32);
	ui_view_dirty (g_respTabs.v);
	int cy = y + 32, ah = imax (20, h - 32);
	ui_w_move (g_pBody, x, cy, w, ah);
	int r = x + w - 8;
	ui_w_move (g_bSave, r - s_saveW, cy + 4, s_saveW, ch); r -= s_saveW + 4;
	ui_w_move (g_bCopy, r - s_copyW, cy + 4, s_copyW, ch); r -= s_copyW + 8;
	int vw = s_viewW, fw = imin (130, imax (60, r - x - 8 - vw - 8));
	ui_w_move (g_find, r - fw, cy + 4, fw, ch); r -= fw + 8;
	ui_w_move (g_view, x + 8, cy + 4, vw, ch);
	ui_w_move (g_ctype, x + vw + 16, cy + 4, imax (1, r - x - vw - 16), ch);
	ui_w_show (g_ctype, r - x - vw - 16 > 50);
	ui_w_move (g_bodyText, x + 8, cy + ch + 8, w - 16, imax (20, ah - ch - 12));
	ui_w_move (g_bodyImg, x + 8, cy + ch + 8, w - 16, imax (20, ah - ch - 12));
	ui_w_move (g_lCookies, x + 8, cy + 6, w - 16, ah - 10);
	ui_w_move (g_lHdrs, x + 8, cy + 6, w - 16, ah - 10);
	ui_w_move (g_pResults, x, cy, w, ah);
	ui_w_move (g_lResults, x + 12, cy + 4, w - 24, ch);
	ui_w_move (g_listResults, x + 8, cy + ch + 8, w - 16, imax (20, ah - ch - 12));
	ui_w_move (g_console, x + 8, cy + 6, w - 16, ah - 10);
	ui_w_move (g_respEmpty, x, cy, w, ah);
	ui_w_move (g_sending, x, cy, w, ah);
	ui_w_move (g_bCancel, x + (w - 100) / 2, cy + ah / 2 + 18, 100, ch);
}
static int req_head_h () { return 3 * 6 + 2 * ctl_h () + 32; }
static void request_place ()
{
	int x = s_rx, y = s_ry, w = s_rw, h = s_rh, ch = ctl_h ();
	Tab *t = cur_tab ();
	ui_w_move (g_pReq, x, y, w, h);
	int r = x + w - 10, sw = btn_w (TR ("Save"));
	ui_w_move (g_code, r - 44, y + 6, 44, ch); r -= 50;
	ui_w_move (g_save, r - sw, y + 6, sw, ch); r -= sw + 6;
	ui_w_move (g_name, x + 10, y + 6, imax (60, r - x - 10), ch);
	int y2 = y + 6 + ch + 6;
	ui_w_move (g_method, x + 10, y2, 104, ch);
	ui_w_move (g_send, x + w - 10 - 84, y2, 84, ch);
	ui_w_move (g_url, x + 120, y2, imax (60, w - 120 - 10 - 84 - 6), ch);
	int y3 = y2 + ch + 6;
	ui_w_move (g_sections.v, x, y3, w, 32);
	ui_view_dirty (g_sections.v);
	int sy = y3 + 32, head = sy - y;
	// the divider: from the pane's top; its bounds leave both sides some room
	int lo = head + 60, hi = imax (lo, h - 96);
	int s = t && t->split > 0 ? t->split : head + (h - head) * 48 / 100;
	s = iclamp (s, lo, hi);
	if (t) t->split = s;
	ui_w_set_range (g_vsplit, y + lo, y + hi, 0, 0);
	ui_w_set_value (g_vsplit, y + s);
	ui_w_move (g_vsplit, x, y + s, w, 6);
	section_place (x, sy, w, y + s - sy);
	response_place (x, y + s + 6, w, h - s - 6);
}
static void env_place ()
{
	int x = s_rx, y = s_ry, w = s_rw, h = s_rh, ch = ctl_h ();
	ui_w_move (g_pEnv, x, y, w, h);
	int r = x + w - 12;
	ui_w_move (g_envActive, r - 110, y + 10, 110, ch); r -= 118;
	int ew = btn_w (TR ("Export..."));
	ui_w_move (g_envExport, r - ew, y + 10, ew, ch); r -= ew + 8;
	ui_w_move (g_envName, x + 12, y + 10, imax (100, r - x - 12), ch);
	ui_w_move (g_envHelp, x + 14, y + 12 + ch, w - 28, ch);
	kv_place (g_envVars, x + 12, y + 16 + 2 * ch, w - 24, imax (KV_HEAD + KV_ROW, h - 24 - 2 * ch));
}
static void coll_place ()
{
	int x = s_rx, y = s_ry, w = s_rw, h = s_rh, ch = ctl_h ();
	ui_w_move (g_pColl, x, y, w, h);
	int r = x + w - 12;
	int aw = btn_w (TR ("Add Request")), ew = btn_w (TR ("Export..."));
	ui_w_move (g_collNewReq, r - aw, y + 10, aw, ch); r -= aw + 8;
	ui_w_move (g_collExport, r - ew, y + 10, ew, ch); r -= ew + 8;
	ui_w_move (g_collName, x + 12, y + 10, imax (100, r - x - 12), ch);
	ui_w_move (g_collTabs.v, x, y + 16 + ch, w, 32);
	ui_view_dirty (g_collTabs.v);
	int cy = y + 16 + ch + 32, ah = imax (40, h - (cy - y));
	ui_w_move (g_collInfo, x + 14, cy + 4, w - 28, ch);
	ui_w_move (g_collDesc, x + 12, cy + ch + 8, w - 24, imax (20, ah - ch - 14));
	ui_w_move (g_pCollAuth, x, cy, w, ah);
	auth_place (g_collAuth, x, cy, w, ah);
	ui_w_move (g_pCollVars, x, cy, w, ah);
	kv_place (g_collVars, x + 12, cy + 8, w - 24, imax (KV_HEAD + KV_ROW, ah - 14));
}
static void views_place (int x, int y, int w, int h)
{
	s_rx = x; s_ry = y; s_rw = w; s_rh = h;
	Tab *t = cur_tab ();
	if (t && t->kind == TAB_ENV) env_place ();
	else if (t && t->kind == TAB_COLL) coll_place ();
	else request_place ();
}

} // namespace cr

#endif
