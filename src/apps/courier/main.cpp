//
// courier -- Onyx's HTTP client on ARMPI's UIKit, made after Postman: requests (GET, POST, PUT, PATCH, DELETE, HEAD,
// OPTIONS) with their query params, headers, authorization (bearer, basic, API key; inherited from the
// folder / the collection), bodies (raw JSON / XML / HTML / text / JavaScript, x-www-form-urlencoded,
// multipart form-data with files, a binary file); {{variables}} everywhere from the environment chosen,
// the collection, the globals ({{$guid}}, {{$timestamp}}... made on the spot); collections with folders;
// environments; the history; the cookie jar; the response pretty-printed (JSON, XML), its headers, its
// cookies, an image or an HTML page previewed, the exchange in a console; tests and captures (a value of
// the response set as a variable: a login's token); the request as code (cURL, HTTP, Python, JavaScript);
// Postman's collections and environments imported and exported, a cURL command imported (or pasted in
// the URL). http:// and https:// (OpenSSL), on a thread of its own: the window keeps answering, Cancel
// works.
//
// The pieces: util.h (strings, arrays), model.h (the data, Postman's formats), vars.h (the variables),
// net.h (the HTTP engine), tools.h (tests, snippets, cURL, formatting), ui.h, widgets.h (what is drawn),
// views.h (the editors), rail.h (the sidebar), dialogs.h, app.h (the tabs). README.md says what differs from Onyx.
// Files: SD0:/home/courier (model.h says which). "courier <file.json>" imports it.
//
#include "dialogs.h"

namespace cr {

enum { TOP_H = 44, ST_H = 24, DOC_H = 34, SPLIT_W = 6 };
static const char *const JSON_KINDS = "JSON files|*.json|All files|*";

static long g_top, g_status, g_bNew, g_bImport, g_envPick, g_gear, g_hsplit, g_welcome, g_wNew, g_wImport, g_wColl;
static int g_w, g_h, g_sideW = 300;
static unsigned g_saveT;			// when the last change came (saved a second later)
static bool g_stateDirty, g_saveDue;
static Str g_stMsg; static bool g_stErr; static unsigned g_stT;
static void layout ();
static void show_views ();

// ================================================================================================================
// the status bar
// ================================================================================================================
static void app_status (const char *msg, bool error)
{
	g_stMsg = msg; g_stErr = error; g_stT = now_ms ();
	ui_view_dirty (g_status);
}
static void status_paint (long, long W, long H)
{
	ui_fill (0, 0, W, H, C_FACE);
	ui_fill (0, 0, W, 1, C_LINE);
	char b[160];
	Environment *e = g_store.env ();
	snprintf (b, sizeof b, TR ("%s   |   %d cookie(s)"), e ? e->name.c () : TR ("No environment"), g_jar.list.size ());
	int w = tw (b);
	text_v ((int) W - 12 - w, 1, (int) H - 1, b, C_FAINT);
	if (!g_stMsg.empty ()) text_v (12, 1, (int) H - 1, g_stMsg.c (), g_stErr ? on_field (C_BAD) : C_DIM, F_UI, 0, (int) W - 40 - w);
}

// ================================================================================================================
// the tabs
// ================================================================================================================
static void refresh_doctabs ()
{
	s_docs.clear ();
	for (int i = 0; i < g_tabs.size (); i++)
	{
		Tab *t = g_tabs[i];
		DocInfo &d = s_docs.push ();
		d.kind = t->kind; d.dirty = t->dirty && t->kind == TAB_REQUEST;
		if (t->kind == TAB_REQUEST) { d.title = t->req.name.empty () ? TR ("Untitled Request") : t->req.name.c (); d.method = t->req.method; }
		else if (t->kind == TAB_ENV) { Environment *e = tab_env (t); d.title = e ? e->name.c () : "?"; }
		else { Collection *c = tab_coll (t); d.title = c ? c->name.c () : "?"; }
	}
	s_docSel = g_cur;
	ui_view_dirty (g_docs);
}
static void refresh_env_pick ()
{
	long sel = g_store.activeEnv + 1;
	ui_list_clear (g_envPick);
	ui_list_add (g_envPick, (long) TR ("No Environment"), 0);
	for (int i = 0; i < g_store.envs.size (); i++) ui_list_add (g_envPick, (long) g_store.envs[i]->name.c (), i + 1);
	ui_w_set_value (g_envPick, sel);
}
static void bind_env (Tab *t)
{
	Environment *e = tab_env (t);
	if (!e) return;
	bool glob = e == &g_store.globals;
	set_text (g_envName, glob ? TR ("Globals") : e->name.c ());
	ui_w_enable (g_envName, !glob);
	kv_set_rows (g_envVars, &e->vars);
	int idx = glob ? -2 : g_store.findEnv (e->id.c ());
	ui_w_show (g_envActive, !glob);
	set_text (g_envActive, idx == g_store.activeEnv ? TR ("Active") : TR ("Set Active"));
	set_text (g_envHelp, glob ? TR ("The globals: seen by every request, whatever the environment (an environment's variable of the same name wins).")
				 : TR ("Use its variables as {{name}} in the URLs, the params, the headers, the bodies and the auth, while it is the active one."));
}
static void coll_section (int s)
{
	if (s != 0) { ui_w_show (g_collDesc, 0); ui_w_show (g_collInfo, 0); }
	if (s != 1) ui_w_show (g_pCollAuth, 0);
	if (s != 2) ui_w_show (g_pCollVars, 0);
	if (s == 0) { ui_w_show (g_collDesc, 1); ui_w_show (g_collInfo, 1); }
	if (s == 1) ui_w_show (g_pCollAuth, 1);
	if (s == 2) ui_w_show (g_pCollVars, 1);
	bar_select (g_collTabs, s);
}
static int count_items (Item *it, int *folders)
{
	int n = 0;
	for (int i = 0; i < it->kids.size (); i++) { if (it->kids[i]->folder) { (*folders)++; n += count_items (it->kids[i], folders); } else n++; }
	return n;
}
static void coll_info (Collection *c)
{
	int f = 0, n = count_items (&c->root, &f), v = 0;
	for (int i = 0; i < c->vars.size (); i++) if (!c->vars[i].blank ()) v++;
	Str s; s.addf (TR ("%d request(s) in %d folder(s)   |   Auth: %s   |   %d variable(s)"), n, f, TR (c->auth.type != AUTH_NONE ? AUTH_NAMES[c->auth.type] : "No Auth"), v);
	set_text (g_collInfo, s.c ());
}
static void bind_coll (Tab *t)
{
	Collection *c = tab_coll (t);
	if (!c) return;
	set_text (g_collName, c->name.c ());
	set_text (g_collDesc, c->desc.c ());
	auth_bind (g_collAuth, &c->auth);
	kv_set_rows (g_collVars, &c->vars);
	coll_info (c);
	coll_section (g_collTabs.sel);
}
static void show_views ()
{
	Tab *t = cur_tab ();
	long w[4] = { g_welcome, g_pReq, g_pEnv, g_pColl };
	int on = !t ? 0 : t->kind == TAB_REQUEST ? 1 : t->kind == TAB_ENV ? 2 : 3;
	for (int i = 0; i < 4; i++) if (i != on) ui_w_show (w[i], 0);
	ui_w_show (w[on], 1);
}
static void send_button ();
static void select_tab (int i)
{
	g_cur = i;
	update_scope ();
	unbind_all ();
	Tab *t = cur_tab ();
	if (t)
	{
		if (t->kind == TAB_REQUEST) bind_request (t);
		else if (t->kind == TAB_ENV) bind_env (t);
		else bind_coll (t);
	}
	show_views ();
	layout ();
	send_button ();
	refresh_doctabs ();
	ui_view_dirty (g_side);
	ui_view_dirty (g_status);
	g_stateDirty = true;
}
static int add_tab (Tab *t)
{
	// after the current one
	int at = g_cur >= 0 ? g_cur + 1 : g_tabs.size ();
	g_tabs.insert (at, t);
	select_tab (at);
	return at;
}
static void close_tab (int i)
{
	if (i < 0 || i >= g_tabs.size ()) return;
	Tab *t = g_tabs[i];
	if (t->kind == TAB_REQUEST && t->dirty)
	{
		Str q; q.addf (TR ("\"%s\" has changes not saved. Close it anyway?"), t->req.name.empty () ? TR ("Untitled Request") : t->req.name.c ());
		if (!ask (TR ("Close Tab"), q.c ())) return;
	}
	if (t->job) { t->job->cancel = 1; t->job->user = 0; t->job = 0; }	// (the loop deletes it when it ends)
	unbind_all ();
	g_tabs.remove (i);
	delete t;
	int n = g_tabs.size ();
	g_cur = -1;
	select_tab (n ? imin (i, n - 1) : -1);
}
static int find_tab (int kind, const char *id)
{
	for (int i = 0; i < g_tabs.size (); i++)
	{
		Tab *t = g_tabs[i];
		if (t->kind != kind) continue;
		if (kind == TAB_REQUEST && t->itemId.eq (id)) return i;
		if (kind == TAB_ENV && t->envId.eq (id)) return i;
		if (kind == TAB_COLL && t->collId.eq (id)) return i;
	}
	return -1;
}
static void touched () { g_saveDue = true; g_saveT = now_ms (); }
static void app_tab_changed ()
{
	refresh_doctabs ();
	g_stateDirty = true;
	touched ();
}

// ================================================================================================================
// the model changed
// ================================================================================================================
static void app_refresh ()
{
	update_scope ();
	side_rebuild ();
	refresh_env_pick ();
	refresh_doctabs ();
	Tab *t = cur_tab ();
	if (t && t->kind == TAB_REQUEST) inherit_text ();
	if (t && t->kind == TAB_ENV) bind_env (t);
	if (t && t->kind == TAB_COLL) { Collection *c = tab_coll (t); if (c) coll_info (c); }
	ui_view_dirty (g_status);
	g_stateDirty = true;
}
static void app_collection_changed (Collection *c) { if (c) c->dirty = true; touched (); }
static void app_env_changed (Environment *e) { if (e) e->dirty = true; touched (); }

// ================================================================================================================
// opening
// ================================================================================================================
static void app_open_item (Item *it)
{
	if (!it || it->folder) return;
	int i = find_tab (TAB_REQUEST, it->id.c ());
	if (i >= 0) { select_tab (i); return; }
	Tab *t = new Tab;
	t->req = it->req; t->req.name = it->name; t->itemId = it->id;
	add_tab (t);
}
static void app_open_coll (Collection *c)
{
	if (!c) return;
	int i = find_tab (TAB_COLL, c->id.c ());
	if (i >= 0) { select_tab (i); return; }
	Tab *t = new Tab; t->kind = TAB_COLL; t->collId = c->id;
	add_tab (t);
}
static void app_open_env (int idx)
{
	const char *id = idx < 0 ? "globals" : g_store.envs[idx]->id.c ();
	int i = find_tab (TAB_ENV, id);
	if (i >= 0) { select_tab (i); return; }
	Tab *t = new Tab; t->kind = TAB_ENV; t->envId = id;
	add_tab (t);
}
static void app_open_history (int idx)
{
	if (idx < 0 || idx >= g_store.history.size ()) return;
	Tab *t = new Tab;
	t->req = g_store.history[idx].req;
	if (t->req.name.empty ()) t->req.name = t->req.url;
	t->dirty = false;
	add_tab (t);
}
static void app_new_request (Item *folder)
{
	if (folder)
	{
		Collection *c = g_store.collectionOf (folder);
		Str nm;
		if (!ask_name (nm, TR ("New Request"), TR ("The request's name"), TR ("New Request"))) return;
		Item *it = new Item; uuid (it->id); it->name = nm; it->req.name = nm;
		folder->add (it); folder->open = true;
		if (c) { c->root.open = true; save_collection (*c); }
		app_refresh ();
		app_open_item (it);
		return;
	}
	Tab *t = new Tab;
	t->req.name = TR ("Untitled Request");
	add_tab (t);
	ui_w_focus (g_url);
}
static void app_new_folder (Item *parent)
{
	if (!parent) return;
	Str nm;
	if (!ask_name (nm, TR ("New Folder"), TR ("The folder's name"), TR ("New Folder"))) return;
	Item *it = new Item; uuid (it->id); it->name = nm; it->folder = true; it->open = true;
	parent->add (it); parent->open = true;
	Collection *c = g_store.collectionOf (parent);
	if (c) { c->root.open = true; save_collection (*c); }
	app_refresh ();
}
static void app_new_collection ()
{
	Str nm;
	if (!ask_name (nm, TR ("New Collection"), TR ("The collection's name"), TR ("New Collection"))) return;
	Collection *c = new Collection;
	uuid (c->id); c->name = nm; c->root.open = true;
	g_store.colls.push (c);
	save_collection (*c);
	side_mode (SB_COLL);
	app_refresh ();
	app_status (TR ("The collection was made: save requests in it (Ctrl+S)."));
}
static void app_new_env ()
{
	Str nm;
	if (!ask_name (nm, TR ("New Environment"), TR ("The environment's name (e.g. Local, Staging, Production)"), TR ("New Environment"))) return;
	Environment *e = new Environment;
	uuid (e->id); e->name = nm;
	e->vars.push (KV ("baseUrl", "http://localhost:8080"));
	g_store.envs.push (e);
	save_environment (*e);
	side_mode (SB_ENV);
	app_refresh ();
	app_open_env (g_store.envs.size () - 1);
}

// ================================================================================================================
// the collections' items
// ================================================================================================================
static void app_rename_item (Item *it)
{
	Str nm;
	if (!ask_name (nm, it->folder ? TR ("Rename Folder") : TR ("Rename Request"), TR ("The new name"), it->name.c ())) return;
	it->name = nm; it->req.name = nm;
	int i = find_tab (TAB_REQUEST, it->id.c ());
	if (i >= 0) { g_tabs[i]->req.name = nm; if (i == g_cur) set_text (g_name, nm.c ()); }
	Collection *c = g_store.collectionOf (it);
	if (c) save_collection (*c);
	app_refresh ();
}
static void app_duplicate_item (Item *it)
{
	Item *p = it->parent;
	if (!p) return;
	Item *d = it->clone ();
	d->name.add (' '); d->name.add (TR ("copy")); d->req.name = d->name;
	p->add (d, p->indexOf (it) + 1);
	Collection *c = g_store.collectionOf (p);
	if (c) save_collection (*c);
	app_refresh ();
}
// the tabs of an item (and of what it holds) no longer in a collection: kept, not saved
static void detach_tabs (Item *it)
{
	for (int i = 0; i < g_tabs.size (); i++) if (g_tabs[i]->kind == TAB_REQUEST && g_tabs[i]->itemId.eq (it->id.c ())) { g_tabs[i]->itemId.clear (); g_tabs[i]->dirty = true; }
	for (int k = 0; k < it->kids.size (); k++) detach_tabs (it->kids[k]);
}
static void app_delete_item (Item *it)
{
	Str q; q.addf (it->folder ? TR ("Delete \"%s\" and everything in it? This cannot be undone.") : TR ("Delete \"%s\"? This cannot be undone."), it->name.c ());
	if (!ask (it->folder ? TR ("Delete Folder") : TR ("Delete Request"), q.c ())) return;
	Collection *c = g_store.collectionOf (it);
	detach_tabs (it);
	if (it->parent) it->parent->detach (it);
	delete it;
	if (c) save_collection (*c);
	app_refresh ();
}
static void app_move_item (Item *it, int dir)
{
	Item *p = it ? it->parent : 0;
	if (!p) return;
	int i = p->indexOf (it), j = i + dir;
	if (j < 0 || j >= p->kids.size ()) return;
	p->kids.move (i, j);
	Collection *c = g_store.collectionOf (p);
	if (c) save_collection (*c);
	app_refresh ();
}
static void app_rename_coll (Collection *c)
{
	Str nm;
	if (!ask_name (nm, TR ("Rename Collection"), TR ("The new name"), c->name.c ())) return;
	c->name = nm; save_collection (*c);
	if (cur_tab () && cur_tab ()->kind == TAB_COLL && tab_coll (cur_tab ()) == c) set_text (g_collName, nm.c ());
	app_refresh ();
}
static void app_duplicate_coll (Collection *c)
{
	json::Writer w (false); write_collection (w, *c);
	Collection *d = new Collection; char err[120];
	if (!parse_collection (*d, w.data (), (int) w.size (), err, sizeof err)) { delete d; return; }
	uuid (d->id); d->name.add (' '); d->name.add (TR ("copy"));
	g_store.colls.push (d); save_collection (*d);
	app_refresh ();
}
static void app_delete_coll (Collection *c)
{
	Str q; q.addf (TR ("Delete the collection \"%s\" and what it holds? This cannot be undone (export it first to keep a copy)."), c->name.c ());
	if (!ask (TR ("Delete Collection"), q.c ())) return;
	detach_tabs (&c->root);
	for (int i = g_tabs.size () - 1; i >= 0; i--) if (g_tabs[i]->kind == TAB_COLL && g_tabs[i]->collId.eq (c->id.c ())) { g_tabs[i]->dirty = false; close_tab (i); }
	if (!c->file.empty ()) remove (c->file.c ());
	for (int i = 0; i < g_store.colls.size (); i++) if (g_store.colls[i] == c) { g_store.colls.remove (i); break; }
	delete c;
	update_scope ();
	app_refresh ();
}
static bool file_save_dialog (char *p, int cap, const char *name, const char *kinds)
{
	mkdir ("SD0:/home/documents", 0755);
	if (kinds) ui_file_filter ((long) kinds);
	long ok = ui_file_save ((long) "SD0:/home/documents", (long) name, (long) p, cap);
	if (kinds) ui_file_filter (0);
	return ok != 0;
}
static void app_export_coll (Collection *c)
{
	Str nm; safe_name (nm, c->name.c ()); nm.add (".postman_collection.json");
	char p[256];
	if (!file_save_dialog (p, sizeof p, nm.c (), JSON_KINDS)) return;
	json::Writer w (true); write_collection (w, *c);
	if (save_json (p, w)) { Str m; m.addf (TR ("Exported to %s (Postman collection v2.1)."), p); app_status (m.c ()); }
	else app_status (TR ("The collection could not be written."), true);
}

// ================================================================================================================
// the environments
// ================================================================================================================
static void app_set_env (int idx)
{
	g_store.activeEnv = idx;
	g_stateDirty = true;
	app_refresh ();
	Str m; m.addf (TR ("Environment: %s"), idx >= 0 ? g_store.envs[idx]->name.c () : TR ("none"));
	app_status (m.c ());
}
static void app_rename_env (int idx)
{
	Environment *e = g_store.envs[idx];
	Str nm;
	if (!ask_name (nm, TR ("Rename Environment"), TR ("The new name"), e->name.c ())) return;
	e->name = nm; save_environment (*e);
	app_refresh ();
}
static void app_duplicate_env (int idx)
{
	Environment *e = new Environment (*g_store.envs[idx]);
	uuid (e->id); e->file.clear (); e->name.add (' '); e->name.add (TR ("copy"));
	g_store.envs.push (e); save_environment (*e);
	app_refresh ();
}
static void app_delete_env (int idx)
{
	Environment *e = g_store.envs[idx];
	Str q; q.addf (TR ("Delete the environment \"%s\" and its variables?"), e->name.c ());
	if (!ask (TR ("Delete Environment"), q.c ())) return;
	for (int i = g_tabs.size () - 1; i >= 0; i--) if (g_tabs[i]->kind == TAB_ENV && g_tabs[i]->envId.eq (e->id.c ())) close_tab (i);
	if (!e->file.empty ()) remove (e->file.c ());
	if (g_store.activeEnv == idx) g_store.activeEnv = -1;
	else if (g_store.activeEnv > idx) g_store.activeEnv--;
	g_store.envs.remove (idx);
	delete e;
	app_refresh ();
}
static void app_export_env (int idx)
{
	Environment *e = idx < 0 ? &g_store.globals : g_store.envs[idx];
	Str nm; safe_name (nm, idx < 0 ? "globals" : e->name.c ()); nm.add (idx < 0 ? ".postman_globals.json" : ".postman_environment.json");
	char p[256];
	if (!file_save_dialog (p, sizeof p, nm.c (), JSON_KINDS)) return;
	json::Writer w (true); write_environment (w, *e, idx < 0 ? "globals" : "environment");
	if (save_json (p, w)) { Str m; m.addf (TR ("Exported to %s."), p); app_status (m.c ()); }
	else app_status (TR ("The environment could not be written."), true);
}

// ================================================================================================================
// the history
// ================================================================================================================
static void app_clear_history ()
{
	if (!g_store.history.size ()) return;
	if (!ask (TR ("Clear History"), TR ("Delete every request of the history?"))) return;
	g_store.history.clear ();
	save_history (g_store);
	app_refresh ();
}
static void app_delete_history (int idx)
{
	g_store.history.remove (idx);
	g_store.historyDirty = true; touched ();
	app_refresh ();
}

// ================================================================================================================
// sending
// ================================================================================================================
static void run_tests (Tab *t)
{
	t->results.clear ();
	if (!t->resp || t->resp->status < 0) return;
	Eval ev (*t->resp);
	for (int i = 0; i < t->req.tests.size (); i++)
	{
		KV &k = t->req.tests[i];
		if (!k.on || k.key.empty ()) continue;
		TestResult r; r.capture = false;
		Str exp; s_trim (exp, k.value.c ());
		r.name.addf ("%s %s", k.key.c (), exp.empty () ? "exists" : exp.c ());
		r.pass = run_test (ev, k.key.c (), k.value.c (), r.detail);
		t->results.push (r);
	}
	Environment *target = g_store.env () ? g_store.env () : &g_store.globals;
	for (int i = 0; i < t->req.captures.size (); i++)
	{
		KV &k = t->req.captures[i];
		if (!k.on || k.key.empty ()) continue;
		TestResult r; r.capture = true;
		Str v, var; s_trim (var, k.key.c ());
		r.name.addf ("%s  <-  %s", var.c (), k.value.c ());
		if (ev.get (k.value.c (), v))
		{
			set_var (target->vars, var.c (), v.c ());
			target->dirty = true;
			Str shown; shown.set (v.c (), imin (v.len (), 60)); if (v.len () > 60) shown.add ("...");
			r.detail.addf ("= %s  (%s)", shown.c (), target == &g_store.globals ? TR ("globals") : target->name.c ());
			r.pass = true;
		}
		else { r.detail = TR ("not found in the response"); r.pass = false; }
		t->results.push (r);
	}
	if (target->dirty) touched ();
}
static void send_button ()
{
	Tab *t = cur_tab ();
	set_text (g_send, t && t->job ? TR ("Cancel") : TR ("Send"));
}
static void job_done (Job *j)
{
	Tab *t = (Tab *) j->user;
	bool alive = false;
	for (int i = 0; i < g_tabs.size (); i++) if (g_tabs[i] == t) alive = true;
	if (!t || !alive) { delete j; return; }
	t->job = 0;
	delete t->last;
	t->last = j; t->resp = &j->resp;
	if (!j->resp.cancelled) { g_jar = j->jar; g_jar.dirty = true; }
	run_tests (t);
	// the history (the most recent first)
	HistoryEntry h; h.when = now_unix (); h.status = j->resp.status >= 0 ? j->resp.status : -1; h.ms = j->resp.msTotal; h.req = t->req;
	h.req.events.clear ();
	g_store.history.insert (0, h);
	while (g_store.history.size () > HISTORY_MAX) g_store.history.remove (g_store.history.size () - 1);
	g_store.historyDirty = true; touched ();
	if (t == cur_tab ()) { t->respSection = t->respSection == RS_CONSOLE && j->resp.status >= 0 ? RS_BODY : t->respSection; resp_fill (); send_button (); }
	if (s_mode == SB_HIST) side_rebuild ();
	Str m;
	if (j->resp.status >= 0)
	{
		int fails = 0; for (int i = 0; i < t->results.size (); i++) if (!t->results[i].pass && !t->results[i].capture) fails++;
		m.addf (TR ("%s %s: %d %s in %d ms"), t->req.method.c (), t->req.name.c (), j->resp.status, j->resp.reason.c (), j->resp.msTotal);
		if (fails) { m.add (" -- "); m.addf (TR ("%d test(s) failed"), fails); }
		app_status (m.c (), fails > 0);
	}
	else app_status (j->resp.error.c (), !j->resp.cancelled);
	ui_view_dirty (g_status);
	ui_view_dirty (g_side);
}
static void app_send ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	if (t->job) { app_cancel (); return; }
	Collection *c = 0; Item *it = tab_item (t, &c);
	update_scope ();
	Job *j = new Job;
	j->jar = g_jar;
	prepare (j->prep, t->req, g_scope, effective_auth (t->req, it, c), g_jar);
	j->onDone = job_done; j->user = t;
	t->job = j; t->started = now_ms ();
	send_button ();
	resp_shown ();
	app_status (TR ("Sending..."));
	job_start (j);
}
// the window does not wait: connect () blocks, the job ends by itself and the loop deletes it
static void app_cancel ()
{
	Tab *t = cur_tab ();
	if (!t || !t->job) return;
	t->job->cancel = 1; t->job->user = 0; t->job = 0;
	Job *c = new Job;
	c->resp.status = -9; c->resp.cancelled = true; c->resp.error = TR ("Cancelled.");
	delete t->last;
	t->last = c; t->resp = &c->resp;
	t->results.clear ();
	resp_fill (); send_button ();
	app_status (TR ("Cancelled."));
}

// ================================================================================================================
// saving
// ================================================================================================================
static void app_save ()
{
	Tab *t = cur_tab ();
	if (!t) return;
	if (t->kind != TAB_REQUEST) { app_status (TR ("Environments and collections are saved as you edit them.")); return; }
	Collection *c = 0; Item *it = tab_item (t, &c);
	if (!it) { app_save_as (); return; }
	it->req = t->req; it->name = t->req.name;
	if (c && save_collection (*c)) { t->dirty = false; Str m; m.addf (TR ("Saved in %s."), c->name.c ()); app_status (m.c ()); }
	else app_status (TR ("The collection could not be saved."), true);
	app_refresh ();
}
static void app_save_as ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	Item *cur = tab_item (t);
	Str nm; int sel = -1;
	if (save_dialog (nm, t->req.name.empty () ? TR ("Untitled Request") : t->req.name.c (), cur ? cur->parent : 0, &sel) != 1) return;
	if (sel < 0 || sel >= s_dests.size ()) { app_status (TR ("Choose a collection or a folder to save the request to."), true); return; }
	if (nm.empty ()) nm = TR ("Untitled Request");
	SaveDest &dst = s_dests[sel];
	Item *it = new Item; uuid (it->id); it->name = nm;
	t->req.name = nm;
	it->req = t->req;
	dst.folder->add (it); dst.folder->open = true; dst.coll->root.open = true;
	t->itemId = it->id;
	if (save_collection (*dst.coll)) { t->dirty = false; Str m; m.addf (TR ("Saved in %s."), dst.coll->name.c ()); app_status (m.c ()); }
	set_text (g_name, nm.c ());
	side_mode (SB_COLL);
	app_refresh ();
}
// every change kept on the card (a second after the last)
static void save_state ();
static void flush (bool now)
{
	if (!now && now_ms () - g_saveT < 1000) return;
	for (int i = 0; i < g_store.colls.size (); i++) if (g_store.colls[i]->dirty) save_collection (*g_store.colls[i]);
	for (int i = 0; i < g_store.envs.size (); i++) if (g_store.envs[i]->dirty) save_environment (*g_store.envs[i]);
	if (g_store.globals.dirty) save_globals (g_store.globals);
	if (g_store.historyDirty) save_history (g_store);
	if (g_jar.dirty)
	{
		json::Writer w (false); w.beginArr ();
		long long now_ = now_unix ();
		for (int i = 0; i < g_jar.list.size (); i++)
		{
			Cookie &c = g_jar.list[i];
			if (!c.expires || c.expires < now_) continue;		// (the session's cookies end with it)
			w.beginObj (true);
			w.key ("domain"); w.str (c.domain.c ()); w.key ("path"); w.str (c.path.c ()); w.key ("name"); w.str (c.name.c ());
			w.key ("value"); w.str (c.value.c ()); w.key ("expires"); w.num ((long long) c.expires);
			w.key ("secure"); w.boolean (c.secure); w.key ("hostOnly"); w.boolean (c.hostOnly); w.key ("httpOnly"); w.boolean (c.httpOnly);
			w.endObj ();
		}
		w.endArr ();
		store_dirs (); save_json (F_COOKIES, w);
		g_jar.dirty = false;
	}
	if (g_stateDirty) save_state ();
	g_saveDue = false;
}
static void load_cookies ()
{
	int n = 0; char *b = read_file (F_COOKIES, &n);
	if (!b) return;
	json::Doc d;
	if (d.parse (b, (unsigned long) n, json::TOLERANT))
		for (const json::Value *e = d.root ().first (); e; e = e->next)
		{
			Cookie c;
			jstr (c.domain, (*e)["domain"]); jstr (c.path, (*e)["path"]); jstr (c.name, (*e)["name"]); jstr (c.value, (*e)["value"]);
			c.expires = (*e)["expires"].asLong (0); c.secure = (*e)["secure"].asBool (false);
			c.hostOnly = (*e)["hostOnly"].asBool (true); c.httpOnly = (*e)["httpOnly"].asBool (false);
			g_jar.list.push (c);
		}
	free (b);
}

// ---- the session: the tabs open, the environment chosen, the sidebar -------------------------------------------------------
static void save_state ()
{
	json::Writer w (true);
	w.beginObj ();
	Environment *e = g_store.env ();
	w.key ("environment"); w.str (e ? e->id.c () : "");
	w.key ("sidebar"); w.num (s_mode);
	w.key ("sidebarWidth"); w.num (g_sideW);
	w.key ("current"); w.num (g_cur);
	w.key ("tabs"); w.beginArr ();
	for (int i = 0; i < g_tabs.size (); i++)
	{
		Tab *t = g_tabs[i];
		w.beginObj ();
		w.key ("kind"); w.num (t->kind);
		if (t->kind == TAB_ENV) { w.key ("env"); w.str (t->envId.c ()); }
		else if (t->kind == TAB_COLL) { w.key ("coll"); w.str (t->collId.c ()); }
		else
		{
			w.key ("item"); w.str (t->itemId.c ());
			w.key ("section"); w.num (t->reqSection);
			if (t->split > 0) { w.key ("split"); w.num (t->split); }
			// a request changed, or in no collection: its working copy kept
			if (t->dirty || t->itemId.empty ()) { w.key ("dirty"); w.boolean (t->dirty); w.key ("request"); write_request_alone (w, t->req); }
		}
		w.endObj ();
	}
	w.endArr ();
	w.endObj ();
	store_dirs ();
	save_json (F_STATE, w);
	g_stateDirty = false;
}
static void load_state ()
{
	int n = 0; char *b = read_file (F_STATE, &n);
	if (!b) return;
	json::Doc d;
	if (d.parse (b, (unsigned long) n, json::TOLERANT))
	{
		const json::Value &r = d.root ();
		g_store.activeEnv = g_store.findEnv (r["environment"].asStr (""));
		side_mode (iclamp (r["sidebar"].asInt (SB_COLL), 0, 2));
		int sw = r["sidebarWidth"].asInt (0);
		if (sw > 0) g_sideW = sw;
		for (const json::Value *e = r["tabs"].first (); e; e = e->next)
		{
			int kind = (*e)["kind"].asInt (0);
			Tab *t = new Tab; t->kind = kind;
			if (kind == TAB_ENV)
			{
				t->envId.set ((*e)["env"].asStr (""));
				if (!t->envId.eq ("globals") && g_store.findEnv (t->envId.c ()) < 0) { delete t; continue; }
			}
			else if (kind == TAB_COLL)
			{
				t->collId.set ((*e)["coll"].asStr (""));
				if (!g_store.findColl (t->collId.c ())) { delete t; continue; }
			}
			else
			{
				t->itemId.set ((*e)["item"].asStr (""));
				t->reqSection = iclamp ((*e)["section"].asInt (0), 0, 5);
				t->split = (*e)["split"].asInt (-1);
				Item *it = g_store.findItem (t->itemId.c ());
				if (!it) t->itemId.clear ();
				if (e->has ("request")) { read_request_alone (t->req, (*e)["request"]); t->dirty = (*e)["dirty"].asBool (false); }
				else if (it) { t->req = it->req; t->req.name = it->name; }
				else { delete t; continue; }
			}
			g_tabs.push (t);
		}
		int c = r["current"].asInt (0);
		g_cur = -1;
		if (g_tabs.size ()) select_tab (iclamp (c, 0, g_tabs.size () - 1));
	}
	free (b);
}

// ================================================================================================================
// import, code, cookies
// ================================================================================================================
static bool import_text (const char *s, int n, const char *from)
{
	Str t; s_trim (t, s, n);
	if (t.empty ()) return false;
	char err[200]; err[0] = 0;
	if (s_startsi (t.c (), "curl"))
	{
		Tab *tab = new Tab; Str e;
		if (!parse_curl (tab->req, t.c (), e)) { delete tab; Str m; m.addf (TR ("The cURL command could not be read: %s"), e.c ()); tell (TR ("Import"), m.c ()); return false; }
		tab->dirty = true;
		add_tab (tab);
		app_status (TR ("The cURL command is a new request: Save it in a collection (Ctrl+S)."));
		return true;
	}
	json::Doc d;
	if (!d.parse (t.c (), (unsigned long) t.len (), json::TOLERANT))
	{
		Str m; m.addf (TR ("%s is neither a cURL command nor JSON (%s, line %d)."), from, d.error (), d.errLine ());
		tell (TR ("Import"), m.c ());
		return false;
	}
	const json::Value &r = d.root ();
	if (r["info"].isObj () && r["item"].isArr ())
	{
		Collection *c = new Collection;
		if (!parse_collection (*c, t.c (), t.len (), err, sizeof err)) { delete c; tell (TR ("Import"), err); return false; }
		if (g_store.findColl (c->id.c ())) uuid (c->id);		// (already here: a copy)
		c->root.open = true;
		g_store.colls.push (c);
		save_collection (*c);
		side_mode (SB_COLL);
		app_refresh ();
		int f = 0, reqs = count_items (&c->root, &f);
		Str m; m.addf (TR ("Imported the collection \"%s\" (%d request(s))."), c->name.c (), reqs);
		app_status (m.c ());
		return true;
	}
	if (r["values"].isArr ())
	{
		bool glob = s_eq (r["_postman_variable_scope"].asStr (""), "globals");
		Environment *e = new Environment;
		if (!parse_environment (*e, t.c (), t.len (), err, sizeof err)) { delete e; tell (TR ("Import"), err); return false; }
		if (glob)
		{
			for (int i = 0; i < e->vars.size (); i++) set_var (g_store.globals.vars, e->vars[i].key.c (), e->vars[i].value.c ());
			delete e;
			save_globals (g_store.globals);
			app_refresh ();
			app_status (TR ("The globals were imported (merged)."));
			return true;
		}
		if (g_store.findEnv (e->id.c ()) >= 0) uuid (e->id);
		g_store.envs.push (e);
		save_environment (*e);
		side_mode (SB_ENV);
		app_refresh ();
		Str m; m.addf (TR ("Imported the environment \"%s\" (%d variable(s))."), e->name.c (), e->vars.size ());
		app_status (m.c ());
		return true;
	}
	tell (TR ("Import"), TR ("This JSON is neither a Postman collection (info, item) nor an environment (values)."));
	return false;
}
static bool import_file (const char *path)
{
	int n = 0; char *b = read_file (path, &n, 32 << 20);
	if (!b) { Str m; m.addf (TR ("%s cannot be read."), path); tell (TR ("Import"), m.c ()); return false; }
	bool ok = import_text (b, n, base_name (path));
	free (b);
	return ok;
}
static void app_import ()
{
	Str path, text;
	int r = import_dialog (path, text);
	if (r == 2) import_file (path.c ());
	else if (r == 1) import_text (text.c (), text.len (), TR ("The text"));
}
static bool app_paste_curl (const char *text)
{
	Str t; s_trim (t, text);
	if (!s_startsi (t.c (), "curl ")) return false;
	Tab *tab = cur_tab ();
	if (!tab || tab->kind != TAB_REQUEST) return false;
	Request r; Str e;
	if (!parse_curl (r, t.c (), e)) return false;
	r.name = tab->req.name; r.tests = tab->req.tests; r.captures = tab->req.captures;
	tab->req = r;
	tab->dirty = true;
	unbind_all ();
	bind_request (tab);
	app_tab_changed ();
	app_status (TR ("The cURL command was read into the request."));
	return true;
}
static void app_code ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	Collection *c = 0; Item *it = tab_item (t, &c);
	update_scope ();
	code_dialog (t->req, effective_auth (t->req, it, c));
}
static void app_cookies () { cookies_dialog (); ui_view_dirty (g_status); }

// ================================================================================================================
// the window's parts
// ================================================================================================================
static void mark (int x, int y, int s)
{
	rbox (x, y, s, s, s / 4, C_ACCENT);
	icon (I_SEND, x + s / 8, y + s / 8, s * 3 / 4, ink_on (C_ACCENT));
}
static void top_paint (long, long W, long H)
{
	ui_fill (0, 0, W, H, C_FACE);
	ui_fill (0, H - 1, W, 1, C_LINE);
	mark (12, ((int) H - 28) / 2, 28);
	text_v (48, 0, (int) H, "Courier", C_TEXT, F_H2, 1);
	if (W >= 860)
	{
		const char *e = TR ("Environment");
		text_v ((int) W - FRAME_CAP_W - 8 - 36 - 6 - 200 - 8 - tw (e), 0, (int) H, e, C_DIM);
	}
}
static void welcome_paint (long, long W, long H)
{
	ui_fill (0, 0, W, H, C_FACE);
	int cx = (int) W / 2, y = (int) H / 2 - 110;
	mark (cx - 32, y, 64);
	text_c (0, y + 76, (int) W, 28, "Courier", C_TEXT, F_H1, 1);
	int w = (int) W - 40;
	const char *s[3] = { TR ("Build, send and test HTTP requests, the way Postman does."),
			     TR ("Ctrl+N a new request    Ctrl+Enter send    Ctrl+S save    Ctrl+W close the tab"),
			     TR ("Paste a cURL command in the URL, or import Postman's collections and environments (Ctrl+O).") };
	text_c (0, y + 110, (int) W, 20, s[0], C_DIM);
	for (int i = 1; i < 3; i++) { if (tw (s[i]) <= w) text_c (0, (int) H / 2 + 90 + (i - 1) * 24, (int) W, 20, s[i], C_FAINT); else text_wrap (20, (int) H / 2 + 90 + (i - 1) * 40, w, s[i], C_FAINT); }
}
static void layout ()
{
	int W = g_w, H = g_h, ch = ctl_h ();
	if (!W) return;
	ui_w_move (g_top, 0, 0, W, TOP_H);
	int by = (TOP_H - ch) / 2;
	int x = 48 + tw ("Courier", F_H2, 1) + 16;
	int nw = btn_w (TR ("New")), iw = btn_w (TR ("Import"));
	ui_w_move (g_bNew, x, by, nw, ch);
	ui_w_move (g_bImport, x + nw + 6, by, iw, ch);
	int r = W - FRAME_CAP_W - 8;
	ui_w_move (g_gear, r - 36, by, 36, ch);
	int pw = imin (200, imax (80, r - 42 - (x + nw + iw + 6) - 8));
	ui_w_move (g_envPick, r - 42 - pw, by, pw, ch);
	ui_view_dirty (g_top);
	ui_w_move (g_status, 0, H - ST_H, W, ST_H);
	ui_view_dirty (g_status);
	// the sidebar | the main area
	int area = H - TOP_H - ST_H;
	int minS = 190, maxS = imax (minS, W - 420);
	g_sideW = iclamp (g_sideW, minS, maxS);
	ui_w_set_range (g_hsplit, minS, maxS, 0, 0);
	ui_w_set_value (g_hsplit, g_sideW);
	side_place (0, TOP_H, g_sideW, area);
	ui_w_move (g_hsplit, g_sideW, TOP_H, SPLIT_W, area);
	int mx = g_sideW + SPLIT_W, mw = W - mx;
	ui_w_move (g_docs, mx, TOP_H, mw, DOC_H);
	ui_view_dirty (g_docs);
	int cy = TOP_H + DOC_H, ah = area - DOC_H;
	ui_w_move (g_welcome, mx, cy, mw, ah);
	ui_view_dirty (g_welcome);
	int total = 150 * 3 + 16, bx = mx + (mw - imin (total, mw - 20)) / 2, bw = (imin (total, mw - 20) - 16) / 3;
	for (int i = 0; i < 3; i++) ui_w_move (i == 0 ? g_wNew : i == 1 ? g_wImport : g_wColl, bx + i * (bw + 8), cy + ah / 2 + 30, bw, ch + 4);
	views_place (mx, cy, mw, ah);
	ui_update_all ();
}
static void on_resize (long w, long h) { g_w = (int) w; g_h = (int) h; layout (); ui_w_draw (0); ui_view_flush (); }

// ---- what the editors tell ------------------------------------------------------------------------------------------------------------
static void on_table () { edited (); }
static void on_params_table ()
{
	Tab *t = cur_tab (); if (!t) return;
	url_from_params (t->req);
	set_text (g_url, t->req.url.c ());
	edited ();
}
static void on_auth_changed () { inherit_text (); edited (); }
static void on_env_vars ()
{
	Environment *e = tab_env (cur_tab ());
	app_env_changed (e);
}
static void on_coll_vars () { Collection *c = tab_coll (cur_tab ()); app_collection_changed (c); if (c) coll_info (c); }
static void on_coll_auth () { Collection *c = tab_coll (cur_tab ()); app_collection_changed (c); if (c) coll_info (c); }
static void kv_pick_file (KVTable &t, KVRow &r)
{
	KV &k = (*t.rows)[r.idx];
	if (!k.file)
	{
		char p[256];
		if (!ui_file_open ((long) "SD0:/home", (long) p, sizeof p)) return;
		k.value = p;
	}
	k.file = !k.file;
	if (t.rows->last ().blank () == false) t.rows->push ();
	kv_rebind (t);
	kv_changed (t);
}
static void on_beautify ()
{
	Tab *t = cur_tab (); if (!t || t->kind != TAB_REQUEST) return;
	Str out;
	if (t->req.rawLang == RAW_JSON && pretty_json (out, t->req.raw.c (), t->req.raw.len ())) { set_text (g_raw, out.c ()); t->req.raw = out; edited (); }
	else if (t->req.rawLang == RAW_XML && pretty_xml (out, t->req.raw.c (), t->req.raw.len ())) { set_text (g_raw, out.c ()); t->req.raw = out; edited (); }
	else app_status (TR ("The body could not be formatted (is it valid JSON?)."), true);
}
static void on_save_body ()
{
	Tab *t = cur_tab (); if (!t || !t->resp) return;
	const char *ct = t->resp->header ("Content-Type"); if (!ct) ct = "";
	int l = (int) strlen (ct);
	const char *ext = s_findi (ct, l, "json") >= 0 ? "json" : s_findi (ct, l, "html") >= 0 ? "html" : s_findi (ct, l, "xml") >= 0 ? "xml"
			: s_findi (ct, l, "png") >= 0 ? "png" : s_findi (ct, l, "jpeg") >= 0 ? "jpg" : s_findi (ct, l, "gif") >= 0 ? "gif" : "txt";
	char nm[64]; snprintf (nm, sizeof nm, "response.%s", ext);
	char p[256];
	if (!file_save_dialog (p, sizeof p, nm, 0)) return;
	if (save_file (p, t->resp->body ? t->resp->body : "", t->resp->bodyLen)) { Str m; m.addf (TR ("The response was saved: %s"), p); app_status (m.c ()); }
	else app_status (TR ("The response could not be written."), true);
}
static void on_gear ()
{
	long c = popup (g_w - FRAME_CAP_W - 8 - 240, TOP_H - 2, { TR ("Cookies..."), TR ("Manage Environments"), "-", TR ("Clear the History...") }, 240);
	if (c == 0) app_cookies ();
	else if (c == 1) side_mode (SB_ENV);
	else if (c == 3) app_clear_history ();
}
static void on_bar (int k, int i)
{
	Tab *t = cur_tab (); if (!t) return;
	Bar *b = s_bars[k];
	if (b == &g_sections) show_section (i);
	else if (b == &g_respTabs) { t->respSection = i; resp_shown (); }
	else if (b == &g_collTabs) coll_section (i);
}
static void doc_menu (int i, int x, int y)
{
	long c = popup (x + g_sideW + SPLIT_W, y + TOP_H, { TR ("Close"), TR ("Close Other Tabs"), TR ("Close All Tabs"), "-", TR ("Duplicate") });
	if (c == 0) close_tab (i);
	else if (c == 1) { Tab *keep = g_tabs[i]; for (int k = g_tabs.size () - 1; k >= 0; k--) if (g_tabs[k] != keep) close_tab (k); }
	else if (c == 2) { for (int k = g_tabs.size () - 1; k >= 0; k--) close_tab (k); }
	else if (c == 4 && g_tabs[i]->kind == TAB_REQUEST)
	{
		Tab *t = new Tab; t->req = g_tabs[i]->req; t->req.name.add (' '); t->req.name.add (TR ("copy")); t->dirty = true;
		select_tab (i); add_tab (t);
	}
}
static void run_later ()
{
	int who = g_later.who;
	if (!who) return;
	g_later.who = L_NONE;
	int a = g_later.a, b = g_later.b, x = g_later.x, y = g_later.y;
	switch (who)
	{
	case L_BAR: on_bar (a, b); break;
	case L_DOC: select_tab (a); break;
	case L_DOC_NEW: app_new_request (0); break;
	case L_DOC_CLOSE: close_tab (a); break;
	case L_DOC_MENU: doc_menu (a, x, y); break;
	case L_SIDE_HEAD: if (s_mode == SB_COLL) app_new_collection (); else if (s_mode == SB_ENV) app_new_env (); else app_clear_history (); break;
	case L_SIDE_RAIL: side_mode (a); break;
	case L_SIDE_ROW: if (a < s_rows.size ()) side_click (s_rows[a], x, b != 0); break;
	case L_SIDE_MENU: if (a < s_rows.size ()) { SideRow r = s_rows[a]; side_menu (r, x, y + TOP_H); } break;
	}
}
// what a widget said (out of ui_w_event: the dialogues and the popups may open here)
static void control (long w, long code)
{
	Tab *t = cur_tab ();
	if (side_control (w, code)) return;
	if (kv_control (w, code, kv_pick_file)) return;
	if (auth_control (g_auth, w, code) || auth_control (g_collAuth, w, code)) return;
	bool act = code == UN_ACTIVATE, chg = code == UN_CHANGE;
	if (w == g_bNew || w == g_wNew) { if (act) app_new_request (0); }
	else if (w == g_bImport || w == g_wImport) { if (act) app_import (); }
	else if (w == g_wColl) { if (act) app_new_collection (); }
	else if (w == g_gear) { if (act) on_gear (); }
	else if (w == g_envPick) { if (chg) app_set_env ((int) ui_w_get_value (w) - 1); }
	else if (w == g_hsplit) { if (chg) { g_sideW = (int) ui_w_get_value (w); g_stateDirty = true; touched (); layout (); ui_w_draw (0); } }
	else if (w == g_vsplit) { if (chg && t) { t->split = (int) ui_w_get_value (w) - s_ry; g_stateDirty = true; touched (); layout (); ui_w_draw (0); } }
	else if (w == g_bCancel) { if (act) app_cancel (); }
	else if (t && t->kind == TAB_REQUEST)
	{
		Request &r = t->req;
		if (w == g_name) { if (chg) { r.name = tx (w); edited (); } }
		else if (w == g_save) { if (act) app_save (); }
		else if (w == g_code) { if (act) app_code (); }
		else if (w == g_method) { if (chg) { r.method = METHODS[iclamp ((int) ui_w_get_value (w), 0, M_COUNT - 1)]; edited (); } }
		else if (w == g_send) { if (act) app_send (); }
		else if (w == g_url)
		{
			if (act) app_send ();
			else if (chg)
			{
				// a cURL command pasted (a whole text at once, not one typed)
				const char *s = tx (w);
				if ((int) strlen (s) > r.url.len () + 6 && app_paste_curl (s)) return;
				r.url = s;
				params_from_url (r);
				kv_refill (g_params);
				edited ();
			}
		}
		else if (w == g_bParamsBulk) { if (act) { kv_bulk (g_params, !g_params.inBulk); bulk_buttons (); } }
		else if (w == g_bHeadersBulk) { if (act) { kv_bulk (g_headers, !g_headers.inBulk); bulk_buttons (); } }
		else if (w == g_bodyMode) { if (chg) { r.bodyMode = (int) ui_w_get_value (w); body_mode_shown (); edited (); } }
		else if (w == g_rawLang) { if (chg) { r.rawLang = (int) ui_w_get_value (w); body_mode_shown (); edited (); } }
		else if (w == g_raw) { if (chg) { r.raw.set (tx (w), (int) ui_w_text_len (w)); edited (); } }
		else if (w == g_beautify) { if (act) on_beautify (); }
		else if (w == g_bodyBulk) { if (act) { KVTable &k = r.bodyMode == BODY_FORM ? g_form : g_urlenc; kv_bulk (k, !k.inBulk); body_mode_shown (); } }
		else if (w == g_binPick)
		{
			char p[256];
			if (act && ui_file_open ((long) "SD0:/home", (long) p, sizeof p)) { r.binary = p; body_mode_shown (); edited (); }
		}
		else if (w == g_follow) { if (chg) { r.followRedirects = ui_w_get_value (w) != 0; edited (); } }
		else if (w == g_verify) { if (chg) { r.insecure = ui_w_get_value (w) == 0; edited (); } }
		else if (w == g_timeout) { if (chg) { r.timeoutMs = imax (0, atoi (tx (w))); edited (); } }
		else if (w == g_view) { if (chg) { t->bodyView = (int) ui_w_get_value (w); resp_body_fill (); resp_shown (); } }
		else if (w == g_find) { if (!s_eq (tx (w), "") && !ui_textarea_find (g_bodyText, (long) tx (w)) && act) app_status (TR ("Not found.")); }
		else if (w == g_bCopy) { if (act && t->resp) { ui_clip_set ((long) tx (g_bodyText), ui_w_text_len (g_bodyText)); app_status (TR ("The response's body is in the clipboard.")); } }
		else if (w == g_bSave) { if (act) on_save_body (); }
	}
	else if (t && t->kind == TAB_ENV)
	{
		Environment *e = tab_env (t);
		if (!e) return;
		bool glob = e == &g_store.globals;
		if (w == g_envName) { if (chg && !glob && tx (w)[0]) { e->name = tx (w); app_env_changed (e); refresh_doctabs (); refresh_env_pick (); ui_view_dirty (g_side); } }
		else if (w == g_envActive) { if (act && !glob) { int idx = g_store.findEnv (e->id.c ()); app_set_env (idx == g_store.activeEnv ? -1 : idx); } }
		else if (w == g_envExport) { if (act) app_export_env (glob ? -1 : g_store.findEnv (e->id.c ())); }
	}
	else if (t && t->kind == TAB_COLL)
	{
		Collection *c = tab_coll (t);
		if (!c) return;
		if (w == g_collName) { if (chg && tx (w)[0]) { c->name = tx (w); app_collection_changed (c); refresh_doctabs (); ui_view_dirty (g_side); } }
		else if (w == g_collDesc) { if (chg) { c->desc.set (tx (w), (int) ui_w_text_len (w)); app_collection_changed (c); } }
		else if (w == g_collNewReq) { if (act) app_new_request (&c->root); }
		else if (w == g_collExport) { if (act) app_export_coll (c); }
	}
}

// ---- the menus -------------------------------------------------------------------------------------------------------------------
enum { C_NEW = 1, C_NEW_COLL, C_NEW_ENV, C_IMPORT, C_EXPORT, C_SAVE, C_SAVE_AS, C_CLOSE, C_SEND, C_CODE, C_BEAUTIFY, C_NEXT, C_PREV, C_COLLS, C_ENVS, C_HIST, C_COOKIES, C_CLEAR };
static char s_menus[1500];
static void menus_make ()
{
	snprintf (s_menus, sizeof s_menus,
		"MCourier\nI1\t%s\tCtrl+N\nI2\t%s\nI3\t%s\n-\nI4\t%s\tCtrl+O\nI5\t%s\n-\nI6\t%s\tCtrl+S\nI7\t%s\nI8\t%s\tCtrl+W\n"
		"M%s\nI9\t%s\tCtrl+Enter\nI10\t%s\nI11\t%s\tCtrl+B\n-\nI12\t%s\nI13\t%s\n"
		"M%s\nI14\t%s\nI15\t%s\nI16\t%s\tCtrl+H\n"
		"M%s\nI17\t%s\nI18\t%s\n",
		TR ("New Request"), TR ("New Collection"), TR ("New Environment"), TR ("Import..."), TR ("Export Collection..."), TR ("Save"), TR ("Save As..."), TR ("Close Tab"),
		TR ("Request"), TR ("Send"), TR ("Code..."), TR ("Beautify the Body"), TR ("Next Tab"), TR ("Previous Tab"),
		TR ("View"), TR ("Collections"), TR ("Environments"), TR ("History"),
		TR ("Tools"), TR ("Cookies..."), TR ("Clear the History..."));
}
static void command (int id)
{
	switch (id)
	{
	case C_NEW: app_new_request (0); break;
	case C_NEW_COLL: app_new_collection (); break;
	case C_NEW_ENV: app_new_env (); break;
	case C_IMPORT: app_import (); break;
	case C_EXPORT:
	{
		Collection *c = tab_coll (cur_tab ());
		if (!c && g_store.colls.size () == 1) c = g_store.colls[0];
		if (!c) app_status (TR ("Open a request of the collection (or the collection) to export it; or use its menu in the sidebar."), true);
		else app_export_coll (c);
		break;
	}
	case C_SAVE: app_save (); break;
	case C_SAVE_AS: app_save_as (); break;
	case C_CLOSE: if (g_cur >= 0) close_tab (g_cur); break;
	case C_SEND: app_send (); break;
	case C_CODE: app_code (); break;
	case C_BEAUTIFY: on_beautify (); break;
	case C_NEXT: if (g_tabs.size ()) select_tab ((g_cur + 1) % g_tabs.size ()); break;
	case C_PREV: if (g_tabs.size ()) select_tab ((g_cur + g_tabs.size () - 1) % g_tabs.size ()); break;
	case C_COLLS: side_mode (SB_COLL); break;
	case C_ENVS: side_mode (SB_ENV); break;
	case C_HIST: side_mode (SB_HIST); break;
	case C_COOKIES: app_cookies (); break;
	case C_CLEAR: app_clear_history (); break;
	}
}
// a key before the widgets. true: taken.
enum { KEY_F5 = 0x3E };
static bool key_first (int hid, int mods)
{
	if ((hid == KEY_ENTER && (mods & UI_MOD_CTRL)) || hid == KEY_F5) { app_send (); return true; }
	if (hid == KEY_ESC) { Tab *t = cur_tab (); if (t && t->job) { app_cancel (); return true; } }
	return false;
}
static void build_window ()
{
	int ch = ctl_h ();
	g_w = (int) ui.w; g_h = (int) ui.h;
	ui_fill (0, 0, g_w, g_h, C_FACE);
	g_top = ui_view (0, 0, 10, TOP_H, 0); ui_view_on (g_top, (long) top_paint, 0, 0, 0);
	g_bNew = ui_pushbutton (0, 0, 76, ch, (long) TR ("New"), 0); ui_w_set_parent (g_bNew, g_top); ui_w_tip (g_bNew, (long) TR ("A new request (Ctrl+N)"));
	g_bImport = ui_pushbutton (0, 0, 84, ch, (long) TR ("Import"), 0); ui_w_set_parent (g_bImport, g_top); ui_w_tip (g_bImport, (long) TR ("Postman collections, environments, cURL commands (Ctrl+O)"));
	g_envPick = ui_dropdown (0, 0, 200, ch, (long) ""); ui_w_set_parent (g_envPick, g_top); ui_w_tip (g_envPick, (long) TR ("The environment whose variables the requests use"));
	g_gear = ui_pushbutton (0, 0, 36, ch, (long) "\xE2\x8B\xAF", UB_GHOST); ui_w_set_parent (g_gear, g_top); ui_w_tip (g_gear, (long) TR ("Cookies, history"));
	g_status = ui_view (0, 0, 10, ST_H, 0); ui_view_on (g_status, (long) status_paint, 0, 0, 0);
	side_build ();
	g_hsplit = ui_splitter (g_sideW, TOP_H, SPLIT_W, 10, 190, 500);
	g_docs = ui_view (0, 0, 10, DOC_H, 0); ui_view_on (g_docs, (long) docs_paint, (long) docs_mouse, 0, 0);
	g_welcome = ui_view (0, 0, 10, 10, 0); ui_view_on (g_welcome, (long) welcome_paint, 0, 0, 0);
	g_wNew = ui_pushbutton (0, 0, 150, ch, (long) TR ("New Request"), UB_DEFAULT); ui_w_set_parent (g_wNew, g_welcome);
	g_wImport = ui_pushbutton (0, 0, 150, ch, (long) TR ("Import"), 0); ui_w_set_parent (g_wImport, g_welcome);
	g_wColl = ui_pushbutton (0, 0, 150, ch, (long) TR ("New Collection"), 0); ui_w_set_parent (g_wColl, g_welcome);
	views_build ();
	ui_w_show (g_pReq, 0); ui_w_show (g_pEnv, 0); ui_w_show (g_pColl, 0);
	menus_make ();
	ui_menu ((long) s_menus);
}

} // namespace cr

using namespace cr;

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	netkit_kit_open ();
	s_jet = (const unsigned *) ui_jetons ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (1200, (int) scr[0] - 8), wh = imin (760, (int) (scr[1] - scr[3] - scr[2]) - 32);
	long win = ui_window_ex (0, scr[3], ww, wh, (long) "Courier", WF_RESIZABLE);
	if (win <= 0) return 1;
	ui_min_size (640, 420);
	ui_on_resize ((long) on_resize, 0);
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/courier.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	if (ww < 900) g_sideW = 240;
	build_window ();
	load_store (g_store);
	load_cookies ();
	side_mode (SB_COLL);
	load_state ();
	app_refresh ();
	show_views ();
	send_button ();
	layout ();
	ui_maximize ();
	if (!g_tabs.size ()) app_status (TR ("Welcome. New (Ctrl+N) makes a request; Import (Ctrl+O) reads Postman's collections."));
	ui_w_draw (0);
	ui_view_flush ();
	// a file named on the command line: imported, the window at its size (the import may ask)
	if (argc > 1 && argv[1][0]) import_file (argv[1]);

	unsigned lastTick = 0, lastSpin = 0;
	for (;;)
	{
		long t = ui_event (30);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP)
		{
			static char b[65536]; long kind = 0;
			long n = uikit_drop_get (b, sizeof b - 1, &kind);
			if (n > 0)
			{
				b[n] = 0;
				if (kind == UDT_FILES) { char *e = strchr (b, '\n'); if (e) *e = 0; import_file (b); }
				else import_text (b, (int) n, TR ("The text dropped"));
			}
		}
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

		jobs_poll ();
		unsigned now = now_ms ();
		Tab *ct = cur_tab ();
		if (ct && ct->job && now - lastSpin >= 100) { lastSpin = now; s_spin++; ui_view_dirty (g_sending); }
		if (now - lastTick >= 250)
		{
			lastTick = now;
			if (g_saveDue || g_stateDirty || g_jar.dirty) flush (false);
			if (!g_stMsg.empty () && !g_stErr && now - g_stT > 8000) { g_stMsg.clear (); ui_view_dirty (g_status); }
		}
		ui_view_flush ();
	}
	flush (true);
	save_state ();
	if (g_jobs.size ()) _exit (0);		// (a job under way may be stuck in connect ())
	return 0;
}
