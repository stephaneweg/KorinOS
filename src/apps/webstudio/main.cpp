//
// WebStudio -- l'outil de développement web de Korin (docs/DESIGN-WEBSTUDIO.md) : un site est un dossier ; on l'écrit,
// le serveur de Korin le sert (webd --dev, docs/WEB.md), F5 l'ouvre dans Jet, chaque enregistrement recharge la page.
//
//   * l'accueil (aucun site) : Nouveau site, Ouvrir un dossier, les récents, les sites du serveur de ce PC ;
//   * l'espace de travail : à gauche l'arbre seul (double-clic : déplier un dossier, ouvrir un fichier), au centre les
//     onglets et le code, en bas le journal du serveur et la recherche dans le site, la barre d'état ;
//   * menus dans la barre de menus d'Elegant ; raccourcis de VS Code là où ils existent.
//
// C++ sur UIKit, appels ui_* directs, couleurs par les jetons du thème (aucune en dur). Les couleurs du code : HTML par
// l'analyseur XML d'UIKit, CSS et JavaScript par celui de C# (en attendant des analyseurs à eux dans UIKit).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
//
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <initializer_list>
#include <uikit.h>
#include "templates.h"

extern char **environ;

#define TR(s)		((const char *) ui_tr ((long) (s)))
#define WEBD		"/bin/webd"
#define JET		"/apps/jet.app/main"
#define GIT		"/bin/git"
#define TERMINAL	"/apps/terminal.app/main"
#define SETTINGS	"HOME:/webstudio.ini"
#define SITES_DIR	"HOME:/sites"
#define SITES_INI	"/etc/web/sites.ini"
#define TEMPLATES	"/apps/webstudio.app/templates"	/* le paquet webstudio-templates */

namespace ws {

static struct ui_vars ui;
static const unsigned *s_jet;
static inline unsigned jet (int off) { return s_jet[off / 4] & 0xFFFFFF; }
static int ctl_h () { return (int) ui_metric (UMT_CTL); }
static int imin (int a, int b) { return a < b ? a : b; }
static int imax (int a, int b) { return a > b ? a : b; }
static void cpy (char *d, const char *s, size_t n) { snprintf (d, n, "%s", s ? s : ""); }

enum { DOC_MAX = 32, NODE_MAX = 3000, RECENT_MAX = 8, HIT_MAX = 400, LOG_MAX = 400, CAP_W = 110 };
enum { K_PLAIN, K_HTML, K_CSS, K_JS };

struct Doc { char path[512]; char rel[300]; long ed; bool dirty; int kind; };
struct Node { char path[512]; long id; bool dir; };
struct Hit { int doc_node; int line; };

static char g_root[512];		// le site ouvert ("" : l'accueil)
static char g_name[128];
static Doc g_docs[DOC_MAX];
static int g_ndocs, g_cur = -1;
static Node *g_nodes;
static int g_nnodes;
static Hit g_hits[HIT_MAX];
static char g_hitpath[HIT_MAX][300];
static int g_nhits;
static char g_recent[RECENT_MAX][512];
static int g_nrecent;
static bool g_quit, g_bottom = true, g_treeShown = true;
static int g_bottomTab;			// 0 serveur, 1 recherche

// le serveur (webd --dev) et Jet
static pid_t g_srv = -1, g_jet = -1;
static int g_srvIn = -1, g_srvOut = -1, g_port;
static char g_srvBuf[4096];
static int g_srvLen;

// les widgets
static long w_site, w_srv, b_srv, b_jet, b_pub;				// la barre du haut
static long h_title, h_sub, h_new, h_open, h_recentT, h_recent, h_servedT, h_served, h_hint;	// l'accueil
static long w_tree, w_tabs, w_seg, w_log, w_hits, w_status, w_pos;	// l'espace de travail

static int TOP_H, TABS_H, ST_H, TREE_W, BOT_H;

// ---- les fichiers -----------------------------------------------------------------------------------------------------
static bool file_exists (const char *p) { struct stat st; return stat (p, &st) == 0; }
static bool is_dir (const char *p) { struct stat st; return stat (p, &st) == 0 && S_ISDIR (st.st_mode); }
static void join (char *o, size_t n, const char *a, const char *b)
{
	size_t l = strlen (a);
	snprintf (o, n, "%s%s%s", a, l && a[l - 1] != '/' ? "/" : "", b);
}
static const char *base_name (const char *p) { const char *s = strrchr (p, '/'); return s ? s + 1 : p; }
static bool has_ci (const char *s, const char *w)	// (s contient w, sans la casse)
{
	size_t n = strlen (w);
	for (; *s; s++) if (!strncasecmp (s, w, n)) return true;
	return false;
}
static const char *ext_of (const char *p) { const char *b = base_name (p), *d = strrchr (b, '.'); return d ? d + 1 : ""; }

static char *read_file (const char *path, long *len)
{
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc ((size_t) n + 1);
	if (b && fread (b, 1, (size_t) n, f) != (size_t) n) { free (b); b = 0; }
	fclose (f);
	if (b) b[n] = 0;
	if (len) *len = n;
	return b;
}
static bool write_file (const char *path, const char *text)
{
	FILE *f = fopen (path, "wb");
	if (!f) return false;
	size_t n = strlen (text);
	bool ok = fwrite (text, 1, n, f) == n;
	return fclose (f) == 0 && ok;
}
static void mkdirs (const char *path)
{
	char t[512];
	cpy (t, path, sizeof t);
	for (char *p = t + 1; *p; p++)
		if (*p == '/') { *p = 0; mkdir (t, 0755); *p = '/'; }
	mkdir (t, 0755);
}
static int kind_of (const char *p)
{
	const char *e = ext_of (p);
	if (!strcasecmp (e, "html") || !strcasecmp (e, "htm") || !strcasecmp (e, "xml") || !strcasecmp (e, "svg") ||
		!strcasecmp (e, "php") || !strcasecmp (e, "cshtml") || !strcasecmp (e, "razor")) return K_HTML;
	if (!strcasecmp (e, "css")) return K_CSS;
	if (!strcasecmp (e, "js") || !strcasecmp (e, "mjs") || !strcasecmp (e, "json") || !strcasecmp (e, "cs") ||
		!strcasecmp (e, "ts")) return K_JS;
	return K_PLAIN;
}
static bool is_text (const char *p)
{
	static const char *const T[] = { "html", "htm", "css", "js", "mjs", "json", "xml", "svg", "txt", "md", "php", "cs",
		"cshtml", "razor", "ts", "ini", "csv", "map", "webmanifest", "htaccess", 0 };
	const char *e = ext_of (p);
	for (int i = 0; T[i]; i++) if (!strcasecmp (e, T[i])) return true;
	return false;
}
static void rel_of (const char *path, char *o, size_t n)
{
	size_t l = strlen (g_root);
	const char *r = !strncmp (path, g_root, l) ? path + l : path;
	while (*r == '/') r++;
	cpy (o, r, n);
}

// une commande (sortie et erreurs dans out) -> son code de sortie
static int run (const char *const *argv, char *out, size_t cap, const char *cwd = 0)
{
	if (out && cap) out[0] = 0;
	int p[2];
	if (pipe (p) != 0) return -1;
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, p[1], 1);
	posix_spawn_file_actions_adddup2 (&fa, p[1], 2);
	posix_spawn_file_actions_addclose (&fa, p[0]);
	char old[512] = "";
	if (cwd) { if (!getcwd (old, sizeof old)) old[0] = 0; chdir (cwd); }
	pid_t pid;
	int r = posix_spawn (&pid, argv[0], &fa, 0, (char *const *) argv, environ);
	if (cwd && old[0]) chdir (old);
	posix_spawn_file_actions_destroy (&fa);
	close (p[1]);
	size_t n = 0;
	char sink[512];
	if (r == 0)
		for (ssize_t k; (k = read (p[0], out && n + 1 < cap ? out + n : sink, out && n + 1 < cap ? cap - 1 - n : sizeof sink)) > 0;)
			if (out && n + 1 < cap) n += (size_t) k;
	close (p[0]);
	if (out && cap) out[n] = 0;
	if (r != 0) { if (out) snprintf (out, cap, "%s : %s", argv[0], strerror (r)); return -1; }
	int st = 0;
	waitpid (pid, &st, 0);
	return WIFEXITED (st) ? WEXITSTATUS (st) : -1;
}
static pid_t launch (const char *const *argv, const char *cwd = 0)
{
	char old[512] = "";
	if (cwd) { if (!getcwd (old, sizeof old)) old[0] = 0; chdir (cwd); }
	pid_t pid;
	int r = posix_spawn (&pid, argv[0], 0, 0, (char *const *) argv, environ);
	if (cwd && old[0]) chdir (old);
	return r == 0 ? pid : -1;
}

// ---- les réglages : les sites récents ------------------------------------------------------------------------------------
static void settings_save ()
{
	char t[RECENT_MAX * 530 + 200];
	int n = snprintf (t, sizeof t, "# WebStudio : les sites récents, le plus récent d'abord\n");
	for (int i = 0; i < g_nrecent; i++) n += snprintf (t + n, sizeof t - (size_t) n, "recent = %s\n", g_recent[i]);
	mkdir ("HOME:", 0755);
	write_file (SETTINGS, t);
}
static void settings_load ()
{
	char *t = read_file (SETTINGS, 0);
	g_nrecent = 0;
	for (char *l = t; l && *l && g_nrecent < RECENT_MAX;) {
		char *e = strchr (l, '\n');
		if (e) *e = 0;
		if (!strncmp (l, "recent = ", 9) && is_dir (l + 9)) cpy (g_recent[g_nrecent++], l + 9, sizeof g_recent[0]);
		l = e ? e + 1 : 0;
	}
	free (t);
}
static void recent_push (const char *dir)
{
	int k = g_nrecent;
	for (int i = 0; i < g_nrecent; i++) if (!strcmp (g_recent[i], dir)) k = i;
	if (k == g_nrecent && g_nrecent < RECENT_MAX) g_nrecent++;
	if (k >= RECENT_MAX) k = RECENT_MAX - 1;
	for (int i = k; i > 0; i--) memcpy (g_recent[i], g_recent[i - 1], sizeof g_recent[0]);
	cpy (g_recent[0], dir, sizeof g_recent[0]);
	settings_save ();
}

// ---- la barre d'état ------------------------------------------------------------------------------------------------------
static void status (const char *s) { ui_w_set_text (w_status, (long) s); ui_w_draw (w_status); }
static void status_pos ()
{
	char t[160] = "";
	if (g_cur >= 0) {
		long col = 0, ln = uikit_textarea_caret (g_docs[g_cur].ed, &col);
		static const char *const K[] = { "Texte", "HTML", "CSS", "JavaScript" };
		snprintf (t, sizeof t, "Ln %ld, Col %ld    UTF-8    %s", ln + 1, col + 1, K[g_docs[g_cur].kind]);
	}
	ui_w_set_text (w_pos, (long) t);
	ui_w_draw (w_pos);
}

// ---- le serveur : webd --dev ----------------------------------------------------------------------------------------------
static void srv_show ()
{
	char t[200];
	if (!g_root[0]) t[0] = 0;
	else if (g_srv > 0 && g_port) snprintf (t, sizeof t, "\xE2\x97\x8F %s  http://localhost:%d", TR ("En marche"), g_port);
	else if (g_srv > 0) snprintf (t, sizeof t, "%s", TR ("Démarrage..."));
	else snprintf (t, sizeof t, "\xE2\x97\x8B %s", TR ("Serveur arrêté"));
	ui_w_set_text (w_srv, (long) t);
	ui_w_set_text (b_srv, (long) TR (g_srv > 0 ? "Arrêter" : "Démarrer"));
	ui_w_draw (0);
}
static void srv_stop ()
{
	if (g_srv <= 0) return;
	if (g_srvIn >= 0) { write (g_srvIn, "quit\n", 5); close (g_srvIn); }
	g_srvIn = -1;
	for (int i = 0; i < 20 && waitpid (g_srv, 0, WNOHANG) == 0; i++) usleep (50000);
	if (waitpid (g_srv, 0, WNOHANG) == 0) { kill (g_srv, SIGTERM); waitpid (g_srv, 0, 0); }
	if (g_srvOut >= 0) close (g_srvOut);
	g_srvOut = -1;
	g_srv = -1;
	g_port = 0;
	g_srvLen = 0;
	srv_show ();
}
static bool srv_start ()
{
	if (g_srv > 0 || !g_root[0]) return g_srv > 0;
	if (!file_exists (WEBD)) {
		ui_message ((long) "WebStudio", (long) TR ("Le serveur web de Korin n'est pas installé : installez le paquet « Serveur web » (webd) dans Logiciels."), UM_OK);
		return false;
	}
	int in[2], out[2];
	if (pipe (in) != 0) return false;
	if (pipe (out) != 0) { close (in[0]); close (in[1]); return false; }
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, in[0], 0);
	posix_spawn_file_actions_adddup2 (&fa, out[1], 1);
	posix_spawn_file_actions_adddup2 (&fa, out[1], 2);
	posix_spawn_file_actions_addclose (&fa, in[1]);
	posix_spawn_file_actions_addclose (&fa, out[0]);
	const char *argv[] = { WEBD, "--dev", g_root, "--port", "8080", 0 };
	int r = posix_spawn (&g_srv, WEBD, &fa, 0, (char *const *) argv, environ);
	posix_spawn_file_actions_destroy (&fa);
	close (in[0]);
	close (out[1]);
	if (r != 0) { close (in[1]); close (out[0]); g_srv = -1; status (TR ("Le serveur n'a pas démarré.")); return false; }
	g_srvIn = in[1];
	g_srvOut = out[0];
	fcntl (g_srvOut, F_SETFL, fcntl (g_srvOut, F_GETFL) | O_NONBLOCK);
	g_port = 0;
	srv_show ();
	return true;
}
static void srv_reload () { if (g_srvIn >= 0) write (g_srvIn, "reload\n", 7); }

// une ligne de webd --dev : « # listen N », « # ... », ou heure, méthode, chemin, code, octets, ms, remarque
static void srv_line (char *l)
{
	if (!strncmp (l, "# listen ", 9)) { g_port = atoi (l + 9); srv_show (); return; }
	if (l[0] == '#' || !l[0]) return;
	char *f[7] = { 0 };
	int n = 0;
	for (char *p = l; p && n < 7; n++) { f[n] = p; p = strchr (p, '\t'); if (p) *p++ = 0; }
	if (n < 6) return;
	char row[700];
	snprintf (row, sizeof row, "%s\t%s\t%s\t%s\t%s\t%s ms\t%s", f[0], f[1], f[2], f[3], f[4], f[5], n > 6 && f[6] ? f[6] : "");
	if (ui_list_count (w_log) >= LOG_MAX) ui_list_remove (w_log, 0);
	long i = ui_list_add (w_log, (long) row, atoi (f[3]));
	ui_w_set_value (w_log, i);
	if (g_bottom && g_bottomTab == 0) ui_w_draw (w_log);
}
static void srv_poll ()
{
	if (g_srv <= 0) return;
	if (waitpid (g_srv, 0, WNOHANG) == g_srv) {	// (parti : port pris partout, webd absent, erreur)
		g_srv = -1;
		if (g_srvIn >= 0) close (g_srvIn);
		g_srvIn = -1;
	}
	for (;;) {
		if (g_srvOut < 0) break;
		ssize_t k = read (g_srvOut, g_srvBuf + g_srvLen, sizeof g_srvBuf - 1 - (size_t) g_srvLen);
		if (k <= 0) break;
		g_srvLen += (int) k;
		g_srvBuf[g_srvLen] = 0;
		char *s = g_srvBuf, *e;
		while ((e = strchr (s, '\n'))) { *e = 0; srv_line (s); s = e + 1; }
		g_srvLen = (int) strlen (s);
		memmove (g_srvBuf, s, (size_t) g_srvLen + 1);
		if (g_srvLen >= (int) sizeof g_srvBuf - 1) g_srvLen = 0;
	}
	if (g_srv <= 0) { if (g_srvOut >= 0) close (g_srvOut); g_srvOut = -1; g_port = 0; srv_show (); }
}

// ---- l'arbre ----------------------------------------------------------------------------------------------------------------
struct Ent { char name[256]; bool dir; };
static int ent_cmp (const void *a, const void *b)
{
	const Ent *x = (const Ent *) a, *y = (const Ent *) b;
	if (x->dir != y->dir) return x->dir ? -1 : 1;
	return strcasecmp (x->name, y->name);
}
static void tree_dir (const char *dir, long parent, int depth)
{
	DIR *d = opendir (dir);
	if (!d) return;
	Ent *e = (Ent *) malloc (sizeof (Ent) * 512);
	int n = 0;
	for (struct dirent *de; e && n < 512 && (de = readdir (d));) {
		if (de->d_name[0] == '.') continue;	// (.git, .webstudio...)
		cpy (e[n].name, de->d_name, sizeof e[n].name);
		char p[512];
		join (p, sizeof p, dir, de->d_name);
		e[n].dir = is_dir (p);
		n++;
	}
	closedir (d);
	qsort (e, (size_t) n, sizeof (Ent), ent_cmp);
	for (int i = 0; i < n && g_nnodes < NODE_MAX; i++) {
		Node &nd = g_nodes[g_nnodes];
		join (nd.path, sizeof nd.path, dir, e[i].name);
		nd.dir = e[i].dir;
		nd.id = ui_tree_add (w_tree, parent, (long) e[i].name, g_nnodes + 1);
		g_nnodes++;
		if (nd.dir && depth < 12) tree_dir (nd.path, nd.id, depth + 1);
	}
	free (e);
}
static void tree_load ()
{
	ui_tree_clear (w_tree);
	g_nnodes = 0;
	if (g_root[0]) tree_dir (g_root, -1, 0);
	ui_w_draw (w_tree);
}
static Node *tree_sel ()
{
	long id = ui_w_get_value (w_tree);
	for (int i = 0; i < g_nnodes; i++) if (g_nodes[i].id == id) return &g_nodes[i];
	return 0;
}
// le dossier où créer : celui choisi, celui du fichier choisi, sinon la racine
static void target_dir (char *o, size_t n)
{
	Node *s = tree_sel ();
	if (!s) { cpy (o, g_root, n); return; }
	cpy (o, s->path, n);
	if (!s->dir) { char *sl = strrchr (o, '/'); if (sl) *sl = 0; }
}

// ---- les documents ----------------------------------------------------------------------------------------------------------
static long is_js_word (long w, long n)
{
	static const char *const K[] = { "function", "return", "const", "let", "var", "if", "else", "for", "while", "do",
		"switch", "case", "break", "continue", "new", "class", "extends", "import", "export", "from", "default", "async",
		"await", "try", "catch", "finally", "throw", "typeof", "instanceof", "in", "of", "this", "null", "undefined",
		"true", "false", "yield", "delete", "void", "static", "get", "set", "super", 0 };
	for (int i = 0; K[i]; i++) if ((long) strlen (K[i]) == n && !strncmp ((const char *) w, K[i], (size_t) n)) return 1;
	return 0;
}
static void relayout ();
static void repaint ();
static void refresh_tabs ()
{
	ui_list_clear (w_tabs);
	int sel = -1;
	for (int i = 0; i < g_ndocs; i++) {
		char t[300];
		snprintf (t, sizeof t, "%s%s", base_name (g_docs[i].path), g_docs[i].dirty ? " \xE2\x80\xA2" : "");
		ui_tabstrip_add (w_tabs, (long) t, i);
		if (i == g_cur) sel = i;
	}
	ui_w_set_value (w_tabs, sel);
	ui_w_draw (w_tabs);
}
static void show_doc (int i)
{
	for (int k = 0; k < g_ndocs; k++) ui_w_show (g_docs[k].ed, k == i);
	g_cur = i;
	refresh_tabs ();
	relayout ();
	if (i >= 0) ui_w_focus (g_docs[i].ed);
	status_pos ();
	repaint ();
}
static int doc_find (const char *path)
{
	for (int i = 0; i < g_ndocs; i++) if (!strcmp (g_docs[i].path, path)) return i;
	return -1;
}
static int open_doc (const char *path)
{
	int i = doc_find (path);
	if (i >= 0) { show_doc (i); return i; }
	if (!is_text (path)) {
		// (une image, une police... : dans Jet si le serveur tourne)
		char m[700];
		snprintf (m, sizeof m, "%s\n%s", base_name (path), TR ("Ce fichier n'est pas du texte : WebStudio ne l'ouvre pas (F5 le montre dans Jet)."));
		ui_message ((long) "WebStudio", (long) m, UM_OK);
		return -1;
	}
	if (g_ndocs >= DOC_MAX) { status (TR ("Trop d'onglets ouverts : fermez-en un.")); return -1; }
	long len = 0;
	char *t = read_file (path, &len);
	if (!t) { status (TR ("Le fichier ne peut pas être lu.")); return -1; }
	Doc &d = g_docs[g_ndocs];
	memset (&d, 0, sizeof d);
	cpy (d.path, path, sizeof d.path);
	rel_of (path, d.rel, sizeof d.rel);
	d.kind = kind_of (path);
	long fl = UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL;
	if (d.kind == K_HTML) fl |= UTA_CODE | UTA_XML;
	else if (d.kind == K_CSS || d.kind == K_JS) fl |= UTA_CODE | UTA_CS;
	d.ed = ui_textarea (0, 0, 100, 100, fl);
	if (d.kind == K_JS) ui_textarea_keywords (d.ed, (long) is_js_word);
	ui_w_set_text (d.ed, (long) t);
	free (t);
	g_ndocs++;
	show_doc (g_ndocs - 1);
	return g_ndocs - 1;
}
static bool save_doc (int i)
{
	if (i < 0 || i >= g_ndocs) return true;
	Doc &d = g_docs[i];
	if (!write_file (d.path, (const char *) ui_w_get_text (d.ed))) {
		char m[700];
		snprintf (m, sizeof m, "%s\n%s", d.path, TR ("ne peut pas être écrit."));
		ui_message ((long) "WebStudio", (long) m, UM_OK);
		return false;
	}
	d.dirty = false;
	return true;
}
static void save_current ()
{
	if (g_cur < 0) return;
	if (save_doc (g_cur)) {
		refresh_tabs ();
		srv_reload ();
		char m[400];
		snprintf (m, sizeof m, "%s %s", g_docs[g_cur].rel, TR ("enregistré"));
		status (m);
	}
}
static void save_all ()
{
	bool any = false;
	for (int i = 0; i < g_ndocs; i++) if (g_docs[i].dirty && save_doc (i)) any = true;
	refresh_tabs ();
	if (any) { srv_reload (); status (TR ("Tout est enregistré")); }
}
// -> false : annulé
static bool close_doc (int i)
{
	if (i < 0 || i >= g_ndocs) return true;
	if (g_docs[i].dirty) {
		char q[600];
		snprintf (q, sizeof q, "%s « %s » ?", TR ("Enregistrer les changements de"), base_name (g_docs[i].path));
		long r = ui_message ((long) "WebStudio", (long) q, UM_YESNOCANCEL);
		if (r == 0) return false;
		if (r == 1 && !save_doc (i)) return false;
		if (r == 1) srv_reload ();
	}
	ui_w_destroy (g_docs[i].ed);
	memmove (&g_docs[i], &g_docs[i + 1], sizeof (Doc) * (size_t) (g_ndocs - i - 1));
	g_ndocs--;
	int c = g_cur > i ? g_cur - 1 : g_cur == i ? imin (i, g_ndocs - 1) : g_cur;
	show_doc (c);
	return true;
}

// ---- l'accueil et le site --------------------------------------------------------------------------------------------------
static void home_fill ()
{
	ui_list_clear (h_recent);
	for (int i = 0; i < g_nrecent; i++) {
		char row[700];
		snprintf (row, sizeof row, "%s\t%s", base_name (g_recent[i]), g_recent[i]);
		ui_list_add (h_recent, (long) row, i);
	}
	// les sites du serveur web de ce PC (/etc/web/sites.ini)
	ui_list_clear (h_served);
	char *t = read_file (SITES_INI, 0);
	char name[128] = "", root[512] = "", binds[400] = "";
	bool en = true;
	auto flush = [&] () {
		if (!name[0]) return;
		char row[1200];
		snprintf (row, sizeof row, "%s\t%s\t%s\t%s", name, root, binds, TR (en ? "actif" : "désactivé"));
		ui_list_add (h_served, (long) row, 0);
		name[0] = root[0] = binds[0] = 0;
		en = true;
	};
	for (char *l = t; l && *l;) {
		char *e = strchr (l, '\n');
		if (e) *e = 0;
		while (*l == ' ' || *l == '\t') l++;
		if (!strncmp (l, "[site ", 6)) { flush (); cpy (name, l + 6, sizeof name); char *b = strchr (name, ']'); if (b) *b = 0; }
		else if (!strncmp (l, "root", 4) && strchr (l, '=')) cpy (root, strchr (l, '=') + 2, sizeof root);
		else if (!strncmp (l, "bind", 4) && strchr (l, '='))
			snprintf (binds + strlen (binds), sizeof binds - strlen (binds), "%s%s", binds[0] ? ", " : "", strchr (l, '=') + 2);
		else if (!strncmp (l, "enabled", 7) && strchr (l, '=')) en = atoi (strchr (l, '=') + 1) != 0;
		l = e ? e + 1 : 0;
	}
	flush ();
	free (t);
}
static void title_update ()
{
	char t[200];
	if (g_root[0]) snprintf (t, sizeof t, "WebStudio \xE2\x80\x94 %s", g_name);
	else snprintf (t, sizeof t, "WebStudio");
	ui_title ((long) t);
	ui_w_set_text (w_site, (long) (g_root[0] ? g_name : ""));
}
static bool close_site ()
{
	while (g_ndocs > 0) { g_cur = g_ndocs - 1; if (!close_doc (g_ndocs - 1)) return false; }
	srv_stop ();
	g_root[0] = 0;
	g_name[0] = 0;
	ui_list_clear (w_log);
	ui_list_clear (w_hits);
	tree_load ();
	home_fill ();
	title_update ();
	relayout ();
	repaint ();
	return true;
}
static void open_site (const char *dir)
{
	if (!is_dir (dir)) { status (TR ("Ce dossier n'existe pas.")); return; }
	if (g_root[0] && !close_site ()) return;
	cpy (g_root, dir, sizeof g_root);
	size_t l = strlen (g_root);
	while (l > 1 && g_root[l - 1] == '/' && g_root[l - 2] != ':') g_root[--l] = 0;
	cpy (g_name, base_name (g_root), sizeof g_name);
	recent_push (g_root);
	title_update ();
	tree_load ();
	relayout ();
	// la page d'accueil du site, si elle est là
	char p[600];
	join (p, sizeof p, g_root, "index.html");
	if (file_exists (p)) open_doc (p);
	srv_start ();
	repaint ();
	status (TR ("Site ouvert. F5 : le voir dans Jet ; Ctrl+S : enregistrer (la page se recharge)."));
}

// ---- les modèles de site (paquet webstudio-templates : TEMPLATES/<id>/, template.ini : name, description, order) ----
// une palette : « palette = id | Nom | #c1 #c2 ... » dans template.ini ; TEMPLATES/<id>/palettes/<pal>.css devient
// css/palette.css du site (des variables CSS ; sa première ligne dit « webstudio-palette: <modèle>/<pal> »)
enum { PAL_MAX = 8, PAL_COLS = 6 };
struct Pal { char id[32]; char name[48]; unsigned col[PAL_COLS]; int ncol; };
struct Tpl { char id[64]; char name[80]; char desc[400]; int order; Pal pal[PAL_MAX]; int npal; };
enum { TPL_MAX = 48, BUILTIN = 3 };
static Tpl g_tpl[TPL_MAX];
static int g_ntpl;
static const char *const BUILTIN_IDS[BUILTIN] = { "empty", "page", "showcase" };
static const char *const BUILTIN_NAMES[BUILTIN] = { "Vide", "Page simple (HTML, CSS, JS)", "Site vitrine (plusieurs pages)" };
static const char *const BUILTIN_DESC[BUILTIN] = {
	"Un dossier et un index.html minimal.",
	"Une page, sa feuille de style et son script.",
	"Quatre pages au menu commun (accueil, services, contact, page 404) et une feuille de style.",
};
static int tpl_cmp (const void *a, const void *b)
{
	const Tpl *x = (const Tpl *) a, *y = (const Tpl *) b;
	return x->order != y->order ? x->order - y->order : strcasecmp (x->name, y->name);
}
static bool pal_parse (Pal &p, char *v)
{
	memset (&p, 0, sizeof p);
	char *f[3] = { v, 0, 0 };
	for (int i = 1; i < 3; i++) { f[i] = f[i - 1] ? strchr (f[i - 1], '|') : 0; if (f[i]) *f[i]++ = 0; }
	for (int i = 0; i < 3; i++) {
		if (!f[i]) return false;
		while (*f[i] == ' ') f[i]++;
		for (size_t l = strlen (f[i]); l && f[i][l - 1] == ' ';) f[i][--l] = 0;
	}
	cpy (p.id, f[0], sizeof p.id);
	cpy (p.name, f[1], sizeof p.name);
	for (char *c = strchr (f[2], '#'); c && p.ncol < PAL_COLS; c = strchr (c + 1, '#'))
		p.col[p.ncol++] = (unsigned) strtoul (c + 1, 0, 16) & 0xFFFFFF;
	return p.id[0] && p.name[0];
}
static void templates_load ()
{
	g_ntpl = 0;
	DIR *d = opendir (TEMPLATES);
	for (struct dirent *e; d && g_ntpl < TPL_MAX && (e = readdir (d));) {
		if (e->d_name[0] == '.') continue;
		char ini[600];
		snprintf (ini, sizeof ini, TEMPLATES "/%s/template.ini", e->d_name);
		char *t = read_file (ini, 0);
		if (!t) continue;
		Tpl &tp = g_tpl[g_ntpl];
		memset (&tp, 0, sizeof tp);
		cpy (tp.id, e->d_name, sizeof tp.id);
		cpy (tp.name, e->d_name, sizeof tp.name);
		tp.order = 1000;
		for (char *l = t; l && *l;) {
			char *nl = strchr (l, '\n');
			if (nl) *nl = 0;
			char *eq = strchr (l, '=');
			if (*l != '#' && eq) {
				char *k = l, *v = eq + 1;
				*eq = 0;
				while (*v == ' ') v++;
				for (char *z = eq - 1; z >= k && (*z == ' ' || *z == '\t'); z--) *z = 0;
				size_t vl = strlen (v);
				while (vl && (v[vl - 1] == '\r' || v[vl - 1] == ' ')) v[--vl] = 0;
				if (!strcmp (k, "name")) cpy (tp.name, v, sizeof tp.name);
				else if (!strcmp (k, "description")) cpy (tp.desc, v, sizeof tp.desc);
				else if (!strcmp (k, "order")) tp.order = atoi (v);
				else if (!strcmp (k, "palette") && tp.npal < PAL_MAX) pal_parse (tp.pal[tp.npal], v) && tp.npal++;
			}
			l = nl ? nl + 1 : 0;
		}
		free (t);
		g_ntpl++;
	}
	if (d) closedir (d);
	qsort (g_tpl, (size_t) g_ntpl, sizeof (Tpl), tpl_cmp);
}
// un fichier du modèle : un texte voit {{SITE}}, {{YEAR}}, {{INITIAL}} remplacés ; le reste est copié tel quel
static bool tpl_file (const char *src, const char *dst, const char *site)
{
	long n = 0;
	char *t = read_file (src, &n);
	if (!t) return false;
	FILE *f = fopen (dst, "wb");
	if (!f) { free (t); return false; }
	if (!is_text (src)) fwrite (t, 1, (size_t) n, f);
	else {
		time_t now = time (0);
		struct tm tm;
		gmtime_r (&now, &tm);
		char year[8], initial[8];
		snprintf (year, sizeof year, "%d", tm.tm_year + 1900);
		// la première lettre, entière en UTF-8 (« É » tient sur deux octets), en capitale si elle est ASCII
		size_t il = 1;
		while (site[0] && il < 4 && ((unsigned char) site[il] & 0xC0) == 0x80) il++;
		if (!site[0]) il = 0;
		memcpy (initial, site, il);
		initial[il] = 0;
		if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= 32;
		for (char *p = t; *p;) {
			const char *v = 0;
			int k = 0;
			if (!strncmp (p, "{{SITE}}", 8)) { v = site; k = 8; }
			else if (!strncmp (p, "{{YEAR}}", 8)) { v = year; k = 8; }
			else if (!strncmp (p, "{{INITIAL}}", 11)) { v = initial; k = 11; }
			if (v) { fputs (v, f); p += k; }
			else fputc (*p++, f);
		}
	}
	free (t);
	return fclose (f) == 0;
}
static void tpl_copy (const char *src, const char *dst, const char *site, bool top = true)
{
	mkdirs (dst);
	DIR *d = opendir (src);
	for (struct dirent *e; d && (e = readdir (d));) {
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, "..") || !strcmp (e->d_name, "template.ini")) continue;
		if (top && !strcmp (e->d_name, "palettes")) continue;	// (la palette choisie seulement : css/palette.css)
		char a[700], b[700];
		join (a, sizeof a, src, e->d_name);
		join (b, sizeof b, dst, e->d_name);
		if (is_dir (a)) tpl_copy (a, b, site, false);
		else tpl_file (a, b, site);
	}
	if (d) closedir (d);
}

static bool name_ok (const char *n)
{
	if (!*n) return false;
	for (; *n; n++)
		if (!((*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') || *n == '-' || *n == '_' || *n == '.'))
			return false;
	return true;
}
static void make_file (const char *dir, const char *name, const char *fmt, const char *a = "", const char *b = "")
{
	char p[600], t[8192];
	join (p, sizeof p, dir, name);
	char *sl = strrchr (p, '/');
	if (sl) { *sl = 0; mkdirs (p); *sl = '/'; }
	snprintf (t, sizeof t, fmt, a, b);
	write_file (p, t);
}
// la palette <pal> du modèle t dans le site dir : TEMPLATES/<id>/palettes/<pal>.css -> dir/css/palette.css
static bool pal_apply (const Tpl &t, int pal, const char *dir)
{
	if (pal < 0 || pal >= t.npal) return false;
	char src[600], dst[600], css[600];
	snprintf (src, sizeof src, TEMPLATES "/%s/palettes/%s.css", t.id, t.pal[pal].id);
	join (css, sizeof css, dir, "css");
	mkdirs (css);
	join (dst, sizeof dst, css, "palette.css");
	return tpl_file (src, dst, base_name (dir));
}
// le modèle et la palette d'un site : la première ligne de css/palette.css (« webstudio-palette: modèle/palette »)
static int site_template (const char *dir, char *pal, size_t n)
{
	char p[600];
	join (p, sizeof p, dir, "css/palette.css");
	char *t = read_file (p, 0);
	if (!t) return -1;
	int r = -1;
	const char *m = strstr (t, "webstudio-palette:");
	if (m && m - t < 400) {
		m += 18;
		while (*m == ' ') m++;
		char id[64] = "";
		size_t i = 0;
		while (m[i] && m[i] != '/' && i < sizeof id - 1) { id[i] = m[i]; i++; }
		id[i] = 0;
		if (m[i] == '/') {
			const char *q = m + i + 1;
			size_t k = 0;
			while (q[k] && q[k] != ' ' && q[k] != '*' && q[k] != '\n' && k < n - 1) { pal[k] = q[k]; k++; }
			pal[k] = 0;
			for (int j = 0; j < g_ntpl; j++) if (!strcmp (g_tpl[j].id, id)) r = j;
		}
	}
	free (t);
	return r;
}
static void open_site (const char *dir);
// le site : créé dans SITES_DIR/<nom> depuis le modèle (0..BUILTIN-1 : intégrés ; puis g_tpl) -> false (message dit)
static bool create_site (const char *name, int model, int pal, bool git, bool pub)
{
	if (!name_ok (name)) { ui_message ((long) TR ("Nouveau site"), (long) TR ("Le nom : lettres, chiffres, - _ et . seulement."), UM_OK); return false; }
	char dir[512];
	mkdirs (SITES_DIR);
	join (dir, sizeof dir, SITES_DIR, name);
	if (file_exists (dir)) { ui_message ((long) TR ("Nouveau site"), (long) TR ("Un dossier de ce nom existe déjà dans HOME:/sites."), UM_OK); return false; }
	mkdirs (dir);
	if (model >= BUILTIN && model - BUILTIN < g_ntpl) {
		char src[600];
		snprintf (src, sizeof src, TEMPLATES "/%s", g_tpl[model - BUILTIN].id);
		tpl_copy (src, dir, name);
		pal_apply (g_tpl[model - BUILTIN], pal, dir);
	} else if (model == 0) make_file (dir, "index.html", T_EMPTY_INDEX, name, name);
	else if (model == 1) {
		make_file (dir, "index.html", T_PAGE_INDEX, name, name);
		make_file (dir, "css/style.css", T_STYLE);
		make_file (dir, "js/app.js", T_SCRIPT);
	} else {
		static const struct { const char *file, *title, *body; } P[] = {
			{ "index.html", "Accueil", "    <h1>Bienvenue</h1>\n    <p>La page d'accueil du site.</p>\n" },
			{ "services.html", "Services", "    <h1>Services</h1>\n    <p>Ce que nous proposons.</p>\n" },
			{ "contact.html", "Contact", "    <h1>Contact</h1>\n    <p>Écrivez-nous.</p>\n" },
			{ "404.html", "Page introuvable", "    <h1>Page introuvable</h1>\n    <p><a href=\"/\">Retour à l'accueil</a></p>\n" },
		};
		for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
			char head[4096], t[8192], title[200], p[600];
			snprintf (title, sizeof title, "%s \xE2\x80\x94 %s", P[i].title, name);
			snprintf (head, sizeof head, T_SHOP_HEAD, title, name);
			snprintf (t, sizeof t, "%s%s%s", head, P[i].body, T_SHOP_FOOT);
			join (p, sizeof p, dir, P[i].file);
			write_file (p, t);
		}
		make_file (dir, "css/style.css", T_STYLE);
	}
	static char out[2000];
	if (git && file_exists (GIT)) {
		const char *a1[] = { GIT, "init", "-q", "-b", "main", 0 };
		run (a1, out, sizeof out, dir);
	}
	if (pub) {
		const char *a2[] = { WEBD, "site", "add", name, dir, "http://*:80", 0 };
		if (run (a2, out, sizeof out) != 0) ui_message ((long) TR ("Nouveau site"), (long) out, UM_OK);
	}
	open_site (dir);
	return true;
}
static long g_dlgModel, g_dlgDesc, g_dlgPal, g_dlgSw, g_dlgPalLbl;
static const Tpl *g_dlgTpl;	// (Site › Palette : le modèle du site ouvert)
// les pastilles de la palette choisie : des bandes côte à côte, un filet autour
static void pal_swatch ()
{
	const Tpl *t = g_dlgTpl;
	if (!t) {
		int m = (int) ui_w_get_value (g_dlgModel);
		t = m >= BUILTIN && m - BUILTIN < g_ntpl ? &g_tpl[m - BUILTIN] : 0;
	}
	int v = (int) ui_w_get_value (g_dlgPal);
	const Pal *p = t && v >= 0 && v < t->npal ? &t->pal[v] : 0;
	long w = 0, h = 0;
	unsigned *px = (unsigned *) uikit_canvas_pixels (g_dlgSw, &w, &h);
	if (!px) return;
	unsigned face = jet (TJ_FACE), line = jet (TJ_LINE);
	for (long i = 0; i < w * h; i++) px[i] = face;
	if (p && p->ncol) {
		long cw = w / p->ncol;
		for (long y = 0; y < h; y++)
			for (long x = 0; x < cw * p->ncol; x++)
				px[y * w + x] = (y == 0 || y == h - 1 || x == 0 || x == cw * p->ncol - 1) ? line : p->col[x / cw];
	}
	ui_w_draw (g_dlgSw);
}
// la liste des palettes du modèle choisi (aucune : la liste est grisée)
static void pal_fill ()
{
	int m = (int) ui_w_get_value (g_dlgModel);
	const Tpl *t = m >= BUILTIN && m - BUILTIN < g_ntpl ? &g_tpl[m - BUILTIN] : 0;
	ui_list_clear (g_dlgPal);
	if (t && t->npal) for (int i = 0; i < t->npal; i++) ui_list_add (g_dlgPal, (long) t->pal[i].name, i);
	else ui_list_add (g_dlgPal, (long) TR ("Celle du modèle"), 0);
	ui_w_set_value (g_dlgPal, 0);
	ui_w_enable (g_dlgPal, t && t->npal > 1);
	ui_w_draw (g_dlgPal);
	pal_swatch ();
}
static void model_desc ()
{
	int m = (int) ui_w_get_value (g_dlgModel);
	const char *d = m < BUILTIN ? TR (BUILTIN_DESC[m]) : m - BUILTIN < g_ntpl ? g_tpl[m - BUILTIN].desc : "";
	char t[600];
	if (!g_ntpl) snprintf (t, sizeof t, "%s\n%s", d, TR ("D'autres modèles (portfolio, restaurant, blog, CV...) : paquet « Modèles de sites » dans Logiciels."));
	else cpy (t, d, sizeof t);
	ui_w_set_text (g_dlgDesc, (long) t);
	ui_w_draw (g_dlgDesc);
}
static long dlg_event (long, long w, long code)
{
	if (w == g_dlgModel && code == UN_CHANGE) { model_desc (); pal_fill (); }
	if (w == g_dlgPal && code == UN_CHANGE) pal_swatch ();
	return 0;
}
static void cmd_new_site ()
{
	templates_load ();
	enum { DW = 560, DH = 420 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Nouveau site"));
	int y = 16, h = ctl_h ();
	ui_label (16, y, 120, h, (long) TR ("Nom"), 0);
	long eName = ui_textbox (140, y, DW - 156, h, 0);
	ui_w_set_text (eName, (long) "mon-site");
	y += h + 10;
	ui_label (16, y, 120, h, (long) TR ("Modèle"), 0);
	char items[4096] = "";
	for (int i = 0; i < BUILTIN; i++) snprintf (items + strlen (items), sizeof items - strlen (items), "%s%s", i ? "|" : "", TR (BUILTIN_NAMES[i]));
	for (int i = 0; i < g_ntpl; i++) snprintf (items + strlen (items), sizeof items - strlen (items), "|%s", g_tpl[i].name);
	g_dlgModel = ui_dropdown (140, y, DW - 156, h, (long) items);
	ui_w_set_value (g_dlgModel, g_ntpl ? BUILTIN : 1);
	y += h + 8;
	g_dlgDesc = ui_label (140, y, DW - 156, 3 * 20, (long) "", ULB_WRAP | ULB_DIM);
	model_desc ();
	y += 3 * 20 + 10;
	ui_label (16, y, 120, h, (long) TR ("Palette"), 0);
	g_dlgPal = ui_dropdown (140, y, 200, h, 0);
	g_dlgSw = ui_canvas (352, y + 3, DW - 368, h - 6);
	g_dlgTpl = 0;
	pal_fill ();
	y += h + 10;
	ui_label (16, y, 120, h, (long) TR ("Dossier"), 0);
	ui_label (140, y, DW - 156, h, (long) SITES_DIR "/<nom>", ULB_DIM);
	y += h + 10;
	long cGit = file_exists (GIT) ? ui_checkbox (140, y, DW - 156, h, (long) TR ("Créer un dépôt git"), 1) : 0;
	if (cGit) y += h + 6;
	long cPub = ui_checkbox (140, y, DW - 156, h, (long) TR ("Ajouter aussi au serveur web de ce PC"), 0);
	long x = ui_dialog_button (d, 0, (long) TR ("Créer le site"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	ui_w_focus (eName);
	long ok = ui_dialog_run (d, (long) dlg_event, 0);
	char name[100];
	cpy (name, (const char *) ui_w_get_text (eName), sizeof name);
	int model = (int) ui_w_get_value (g_dlgModel), pal = (int) ui_w_get_value (g_dlgPal);
	bool git = cGit && ui_w_get_value (cGit), pub = ui_w_get_value (cPub);
	ui_dialog_end (d);
	repaint ();
	if (ok == 1) create_site (name, model, pal, git, pub);
}
// Site › Palette : une autre palette du modèle du site ouvert (css/palette.css est remplacé)
static void cmd_palette ()
{
	if (!g_root[0]) return;
	templates_load ();
	char cur[32] = "";
	int t = site_template (g_root, cur, sizeof cur);
	if (t < 0 || g_tpl[t].npal < 1) {
		ui_message ((long) TR ("Palette"), (long) TR ("Ce site n'a pas de palette : il ne vient pas d'un modèle qui en propose (ou css/palette.css a perdu sa première ligne)."), UM_OK);
		repaint ();
		return;
	}
	const Tpl &tp = g_tpl[t];
	enum { DW = 480, DH = 206 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Palette du site"));
	int y = 16, h = ctl_h ();
	char m[200];
	snprintf (m, sizeof m, "%s %s", TR ("Modèle :"), tp.name);
	ui_label (16, y, DW - 32, h, (long) m, ULB_DIM);
	y += h + 10;
	ui_label (16, y, 100, h, (long) TR ("Palette"), 0);
	g_dlgTpl = &tp;
	g_dlgModel = 0;
	g_dlgPal = ui_dropdown (120, y, 180, h, 0);
	for (int i = 0; i < tp.npal; i++) ui_list_add (g_dlgPal, (long) tp.pal[i].name, i);
	int sel = 0;
	for (int i = 0; i < tp.npal; i++) if (!strcmp (tp.pal[i].id, cur)) sel = i;
	ui_w_set_value (g_dlgPal, sel);
	g_dlgSw = ui_canvas (312, y + 3, DW - 328, h - 6);
	y += h + 10;
	ui_label (16, y, DW - 32, 2 * 20, (long) TR ("css/palette.css est remplacé : vos changements de couleurs dans ce fichier sont perdus."), ULB_WRAP | ULB_DIM);
	long x = ui_dialog_button (d, 0, (long) TR ("Appliquer"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	pal_swatch ();
	long ok = ui_dialog_run (d, (long) dlg_event, 0);
	int pal = (int) ui_w_get_value (g_dlgPal);
	ui_dialog_end (d);
	g_dlgTpl = 0;
	repaint ();
	if (ok != 1) return;
	char p[600];
	join (p, sizeof p, g_root, "css/palette.css");
	int di = doc_find (p);
	if (di >= 0 && !close_doc (di)) return;	// (ouvert : enregistré ou abandonné, puis relu)
	if (!pal_apply (tp, pal, g_root)) { ui_message ((long) TR ("Palette"), (long) TR ("La palette n'a pas pu être écrite."), UM_OK); repaint (); return; }
	if (di >= 0) open_doc (p);
	srv_reload ();
	char st[200];
	snprintf (st, sizeof st, "%s %s", TR ("Palette :"), tp.pal[pal].name);
	status (st);
}
static void cmd_open_folder ()
{
	char dir[512];
	cpy (dir, g_root[0] ? g_root : SITES_DIR, sizeof dir);
	mkdirs (SITES_DIR);
	if (!ui_folder_choose ((long) dir, (long) dir, sizeof dir)) { repaint (); return; }
	open_site (dir);
}

// ---- Jet ---------------------------------------------------------------------------------------------------------------------
static void url_of_current (char *o, size_t n)
{
	char rel[600] = "";
	if (g_cur >= 0 && g_docs[g_cur].kind == K_HTML) cpy (rel, g_docs[g_cur].rel, sizeof rel);
	if (!strcasecmp (base_name (rel), "index.html")) rel[strlen (rel) - strlen ("index.html")] = 0;
	size_t k = (size_t) snprintf (o, n, "http://localhost:%d/", g_port);
	for (const char *p = rel; *p && k + 4 < n; p++) {
		if (*p == ' ') k += (size_t) snprintf (o + k, n - k, "%%20");
		else o[k++] = *p;
	}
	o[k] = 0;
}
static void cmd_view ()
{
	if (!g_root[0]) return;
	if (g_srv <= 0 && !srv_start ()) return;
	for (int i = 0; i < 60 && !g_port && g_srv > 0; i++) { usleep (50000); srv_poll (); }
	if (!g_port) { status (TR ("Le serveur ne répond pas encore : réessayez F5.")); return; }
	if (g_jet > 0 && kill (g_jet, 0) == 0 && waitpid (g_jet, 0, WNOHANG) == 0) {
		srv_reload ();				// (Jet est ouvert : la page se recharge)
		status (TR ("Page rechargée dans Jet."));
		return;
	}
	if (!file_exists (JET)) { ui_message ((long) "WebStudio", (long) TR ("Jet n'est pas installé."), UM_OK); return; }
	char url[800];
	url_of_current (url, sizeof url);
	const char *argv[] = { JET, url, 0 };
	g_jet = launch (argv);
	status (g_jet > 0 ? url : TR ("Jet ne peut pas être lancé."));
}

// ---- les fichiers du site ---------------------------------------------------------------------------------------------------
static void cmd_new_file (bool folder)
{
	if (!g_root[0]) return;
	char dir[512], name[200] = "";
	target_dir (dir, sizeof dir);
	if (!folder) cpy (name, "page.html", sizeof name);
	if (!ui_ask ((long) TR (folder ? "Nouveau dossier" : "Nouveau fichier"), (long) TR ("Nom (un sous-dossier : css/a.css) :"), (long) name, sizeof name)) { repaint (); return; }
	repaint ();
	if (!name[0] || strstr (name, "..") || name[0] == '/' || strchr (name, ':')) { status (TR ("Ce nom ne convient pas.")); return; }
	char p[700];
	join (p, sizeof p, dir, name);
	if (file_exists (p)) { status (TR ("Ce fichier existe déjà.")); return; }
	if (folder) mkdirs (p);
	else {
		char *sl = strrchr (p, '/');
		if (sl) { *sl = 0; mkdirs (p); *sl = '/'; }
		char t[1024] = "";
		if (kind_of (p) == K_HTML) snprintf (t, sizeof t, T_NEW_HTML, base_name (p));
		write_file (p, t);
	}
	tree_load ();
	if (!folder) open_doc (p);
	srv_reload ();
}
static void cmd_rename ()
{
	Node *s = tree_sel ();
	if (!s) { status (TR ("Choisissez d'abord un fichier ou un dossier dans l'arbre.")); return; }
	char name[200], from[512];
	cpy (from, s->path, sizeof from);
	cpy (name, base_name (from), sizeof name);
	if (!ui_ask ((long) TR ("Renommer"), (long) TR ("Nouveau nom :"), (long) name, sizeof name) || !name_ok (name)) { repaint (); return; }
	char to[600];
	cpy (to, from, sizeof to);
	char *sl = strrchr (to, '/');
	if (sl) snprintf (sl + 1, sizeof to - (size_t) (sl + 1 - to), "%s", name);
	if (rename (from, to) != 0) { status (TR ("Le renommage a échoué.")); repaint (); return; }
	int d = doc_find (from);
	if (d >= 0) { cpy (g_docs[d].path, to, sizeof g_docs[d].path); rel_of (to, g_docs[d].rel, sizeof g_docs[d].rel); refresh_tabs (); }
	tree_load ();
	repaint ();
}
static void remove_tree (const char *p)
{
	if (is_dir (p)) {
		DIR *d = opendir (p);
		for (struct dirent *e; d && (e = readdir (d));) {
			if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, "..")) continue;
			char c[600];
			join (c, sizeof c, p, e->d_name);
			remove_tree (c);
		}
		if (d) closedir (d);
		rmdir (p);
	} else
		unlink (p);
}
static void cmd_delete ()
{
	Node *s = tree_sel ();
	if (!s) { status (TR ("Choisissez d'abord un fichier ou un dossier dans l'arbre.")); return; }
	char p[512], q[700];
	cpy (p, s->path, sizeof p);
	snprintf (q, sizeof q, "%s « %s »%s ?", TR ("Supprimer"), base_name (p), s->dir ? TR (" et tout ce qu'il contient") : "");
	if (!ui_message ((long) "WebStudio", (long) q, UM_YESNO)) { repaint (); return; }
	for (int i = g_ndocs - 1; i >= 0; i--)
		if (!strncmp (g_docs[i].path, p, strlen (p))) { g_docs[i].dirty = false; close_doc (i); }
	remove_tree (p);
	tree_load ();
	srv_reload ();
	repaint ();
}

// ---- la recherche ---------------------------------------------------------------------------------------------------------
static char g_find[200];
static void cmd_find ()
{
	if (g_cur < 0) return;
	if (!ui_ask ((long) TR ("Rechercher"), (long) TR ("Texte :"), (long) g_find, sizeof g_find)) { repaint (); return; }
	repaint ();
	if (!ui_textarea_find (g_docs[g_cur].ed, (long) g_find)) status (TR ("Introuvable."));
	ui_w_focus (g_docs[g_cur].ed);
}
static void find_in (const char *dir)
{
	DIR *d = opendir (dir);
	for (struct dirent *e; d && g_nhits < HIT_MAX && (e = readdir (d));) {
		if (e->d_name[0] == '.') continue;
		char p[600];
		join (p, sizeof p, dir, e->d_name);
		if (is_dir (p)) { find_in (p); continue; }
		if (!is_text (p)) continue;
		char *t = read_file (p, 0);
		int ln = 0;
		for (char *l = t; l && *l && g_nhits < HIT_MAX; ln++) {
			char *nl = strchr (l, '\n');
			if (nl) *nl = 0;
			if (has_ci (l, g_find)) {
				char rel[300], row[900], line[200];
				rel_of (p, rel, sizeof rel);
				while (*l == ' ' || *l == '\t') l++;
				cpy (line, l, sizeof line);
				for (char *c = line; *c; c++) if (*c == '\t') *c = ' ';
				snprintf (row, sizeof row, "%s\t%d\t%s", rel, ln + 1, line);
				cpy (g_hitpath[g_nhits], p, sizeof g_hitpath[0]);
				g_hits[g_nhits].line = ln;
				ui_list_add (w_hits, (long) row, g_nhits);
				g_nhits++;
			}
			l = nl ? nl + 1 : 0;
		}
		free (t);
	}
	if (d) closedir (d);
}
static void set_bottom (int tab);
static void cmd_find_site ()
{
	if (!g_root[0]) return;
	if (!ui_ask ((long) TR ("Rechercher dans le site"), (long) TR ("Texte :"), (long) g_find, sizeof g_find) || !g_find[0]) { repaint (); return; }
	ui_list_clear (w_hits);
	g_nhits = 0;
	find_in (g_root);
	g_bottom = true;
	set_bottom (1);
	char m[200];
	snprintf (m, sizeof m, "%d %s", g_nhits, TR (g_nhits == 1 ? "résultat" : "résultats"));
	status (m);
}
static void goto_line (int doc, int line)
{
	const char *t = (const char *) ui_w_get_text (g_docs[doc].ed);
	long off = 0;
	for (int l = 0; l < line && t[off]; off++) if (t[off] == '\n') l++;
	ui_textarea_goto (g_docs[doc].ed, off);
	ui_w_focus (g_docs[doc].ed);
	status_pos ();
}

// ---- Git, Publier, Terminal -----------------------------------------------------------------------------------------------
static void cmd_git ()
{
	if (!g_root[0]) return;
	if (!file_exists (GIT)) { ui_message ((long) "Git", (long) TR ("Git n'est pas installé (paquet git)."), UM_OK); return; }
	static char out[4000];
	char gd[600];
	join (gd, sizeof gd, g_root, ".git");
	if (!is_dir (gd)) {
		if (ui_message ((long) "Git", (long) TR ("Ce site n'a pas de dépôt git. En créer un ?"), UM_YESNO)) {
			const char *a[] = { GIT, "init", "-q", "-b", "main", 0 };
			run (a, out, sizeof out, g_root);
			status (TR ("Dépôt git créé (branche main)."));
		}
		repaint ();
		return;
	}
	const char *s[] = { GIT, "status", "--short", "--branch", 0 };
	run (s, out, sizeof out, g_root);
	char q[4400];
	snprintf (q, sizeof q, "%s\n\n%s", out[0] ? out : "", TR ("Valider tous les changements ?"));
	if (!ui_message ((long) "Git", (long) q, UM_YESNO)) { repaint (); return; }
	char msg[300] = "";
	if (!ui_ask ((long) "Git", (long) TR ("Message de la validation :"), (long) msg, sizeof msg) || !msg[0]) { repaint (); return; }
	save_all ();
	const char *a[] = { GIT, "add", "-A", 0 };
	run (a, out, sizeof out, g_root);
	const char *c[] = { GIT, "commit", "-q", "-m", msg, 0 };
	int r = run (c, out, sizeof out, g_root);
	repaint ();
	if (r != 0) ui_message ((long) "Git", (long) (out[0] ? out : TR ("La validation a échoué.")), UM_OK);
	else status (TR ("Changements validés."));
}
static void cmd_publish ()
{
	if (!g_root[0]) return;
	if (!file_exists (WEBD)) { ui_message ((long) TR ("Publier"), (long) TR ("Le serveur web n'est pas installé (paquet webd)."), UM_OK); return; }
	char bind[200] = "http://*:80";
	if (!ui_ask ((long) TR ("Publier sur le serveur web de ce PC"), (long) TR ("Liaison du site (http://*:80, https://nom.exemple) :"), (long) bind, sizeof bind)) { repaint (); return; }
	save_all ();
	static char out[2000];
	const char *a[] = { WEBD, "site", "add", g_name, g_root, bind, 0 };
	int r = run (a, out, sizeof out);
	if (r != 0 && strstr (out, "existe")) {	// (déjà publié : la liaison seulement)
		const char *b[] = { WEBD, "site", "bind", g_name, "add", bind, 0 };
		r = run (b, out, sizeof out);
	}
	char m[2400];
	snprintf (m, sizeof m, "%s\n\n%s", out, TR (r == 0 ? "Le site est dans Réglages › Serveur web ; le serveur doit être démarré." : "La publication a échoué."));
	ui_message ((long) TR ("Publier"), (long) m, UM_OK);
	repaint ();
}
static void cmd_terminal ()
{
	if (!g_root[0]) return;
	const char *a[] = { TERMINAL, 0 };
	if (launch (a, g_root) <= 0) status (TR ("Le Terminal ne peut pas être lancé."));
}
// un 404 du journal : créer le fichier qui manque
static void log_activate ()
{
	long v = ui_w_get_value (w_log);
	long code = 0;
	const char *row = (const char *) uikit_list_get (w_log, v, &code);
	if (!row || code != 404) return;
	char path[600] = "";
	const char *f = row;
	for (int k = 0; k < 2 && f; k++) { f = strchr (f, '\t'); if (f) f++; }
	if (!f) return;
	const char *e = strchr (f, '\t');
	snprintf (path, sizeof path, "%.*s", (int) (e ? e - f : (long) strlen (f)), f);
	if (path[0] != '/' || strstr (path, "..") || !strcmp (path, "/favicon.ico")) return;
	char q[700];
	snprintf (q, sizeof q, "%s %s ?", TR ("Créer"), path + 1);
	if (!ui_message ((long) "WebStudio", (long) q, UM_YESNO)) { repaint (); return; }
	char p[700], t[1024] = "";
	join (p, sizeof p, g_root, path + 1);
	char *sl = strrchr (p, '/');
	if (sl) { *sl = 0; mkdirs (p); *sl = '/'; }
	if (kind_of (p) == K_HTML) snprintf (t, sizeof t, T_NEW_HTML, base_name (p));
	write_file (p, t);
	tree_load ();
	if (is_text (p)) open_doc (p);
	repaint ();
}

// ---- la mise en page ----------------------------------------------------------------------------------------------------
static void set_bottom (int tab)
{
	g_bottomTab = tab;
	ui_w_set_value (w_seg, tab);
	relayout ();
	repaint ();
}
static void relayout ()
{
	int W = (int) ui.w, H = (int) ui.h, h = ctl_h ();
	bool home = !g_root[0];
	// la barre du haut (les 110 px de droite : les glyphes de la fenêtre)
	int y = (TOP_H - h) / 2;
	ui_w_move (w_site, 12, y, 200, h);
	ui_w_move (w_srv, 220, y, 280, h);
	ui_w_move (b_srv, 506, y, 96, h);
	ui_w_move (b_jet, 608, y, 150, h);
	ui_w_move (b_pub, imax (764, W - CAP_W - 110), y, 104, h);
	for (long w : { w_srv, b_srv, b_jet, b_pub }) ui_w_show (w, !home);
	ui_w_show (w_site, !home);
	// l'accueil
	for (long w : { h_title, h_sub, h_new, h_open, h_recentT, h_recent, h_servedT, h_served, h_hint }) ui_w_show (w, home);
	if (home) {
		int cw = imin (960, W - 48), x = (W - cw) / 2, top = imax (TOP_H + 20, (H - 560) / 3);
		int lw = imin (320, cw / 3), rx = x + lw + 40, rw = cw - lw - 40;
		ui_w_move (h_title, x, top, lw, 36);
		ui_w_move (h_sub, x, top + 40, lw, 44);
		ui_w_move (h_new, x, top + 100, lw, h + 8);
		ui_w_move (h_open, x, top + 108 + h + 8, lw, h + 8);
		ui_w_move (h_hint, x, top + 130 + 2 * (h + 8), lw, 80);
		ui_w_move (h_recentT, rx, top, rw, 24);
		ui_w_move (h_recent, rx, top + 28, rw, 220);
		ui_w_move (h_servedT, rx, top + 264, rw, 24);
		ui_w_move (h_served, rx, top + 292, rw, imax (120, H - ST_H - 20 - (top + 292)));
	}
	// l'espace de travail
	ui_w_show (w_tree, !home && g_treeShown);
	ui_w_show (w_tabs, !home);
	ui_w_show (w_seg, !home && g_bottom);
	ui_w_show (w_log, !home && g_bottom && g_bottomTab == 0);
	ui_w_show (w_hits, !home && g_bottom && g_bottomTab == 1);
	int tw = g_treeShown ? imin (TREE_W, W / 4) : 0;
	int bh = g_bottom ? imin (BOT_H, H / 3) : 0;
	int mainH = H - TOP_H - ST_H;
	if (!home) {
		if (tw) ui_w_move (w_tree, 0, TOP_H, tw, mainH);
		int cx = tw ? tw + 1 : 0, cw = W - cx;
		ui_w_move (w_tabs, cx, TOP_H, cw, TABS_H);
		int ey = TOP_H + TABS_H, eh = mainH - TABS_H - bh;
		for (int i = 0; i < g_ndocs; i++) ui_w_move (g_docs[i].ed, cx, ey, cw, eh);
		if (bh) {
			int by = ey + eh;
			ui_w_move (w_seg, cx + 8, by + 4, 220, h);
			ui_w_move (w_log, cx, by + h + 8, cw, bh - h - 8);
			ui_w_move (w_hits, cx, by + h + 8, cw, bh - h - 8);
		}
	}
	for (int i = 0; i < g_ndocs; i++) ui_w_show (g_docs[i].ed, !home && i == g_cur);
	ui_w_move (w_status, 12, H - ST_H + 2, W / 2, ST_H - 4);
	ui_w_move (w_pos, W / 2, H - ST_H + 2, W / 2 - 12, ST_H - 4);
}
static void repaint ()
{
	int W = (int) ui.w, H = (int) ui.h;
	ui_fill (0, 0, W, H, jet (TJ_FACE));
	ui_fill (0, TOP_H - 1, W, 1, jet (TJ_LINE));
	ui_fill (0, H - ST_H, W, ST_H, jet (TJ_BAR));
	ui_fill (0, H - ST_H, W, 1, jet (TJ_LINE));
	if (g_root[0]) {
		int tw = g_treeShown ? imin (TREE_W, W / 4) : 0;
		if (tw) ui_fill (tw, TOP_H, 1, H - TOP_H - ST_H, jet (TJ_LINE));
		if (g_cur < 0) {			// (aucun onglet : ce qu'on peut faire)
			int cx = tw + 1, cw = W - cx, y = TOP_H + TABS_H + 40;
			const char *L[] = { "Double-clic sur un fichier de l'arbre : l'ouvrir.", "F5 : voir le site dans Jet ; Ctrl+S : enregistrer, la page se recharge.",
				"Ctrl+Maj+F : rechercher dans le site.", 0 };
			for (int i = 0; L[i]; i++) {
				const char *s = TR (L[i]);
				int w = (int) ui_text_width ((long) s, 13, (long) ui.font);
				ui_text ((long) s, cx + (cw - w) / 2, y + 24 * i, jet (TJ_TEXT3), 13, (long) ui.font, cw);
			}
		}
	}
	ui_w_draw (0);
	ui_update_all ();
}
static long on_resize (long, long) { relayout (); repaint (); return 0; }

// ---- les menus ------------------------------------------------------------------------------------------------------------
enum { M_NEWSITE = 1, M_OPENDIR, M_NEWFILE, M_NEWDIR, M_RENAME, M_DELETE, M_SAVE, M_SAVEALL, M_CLOSETAB, M_CLOSESITE, M_QUIT,
	M_UNDO = 20, M_REDO, M_CUT, M_COPY, M_PASTE, M_ALL, M_FIND, M_FINDSITE,
	M_TREE = 40, M_BOTTOM, M_LOG, M_HITS,
	M_VIEW = 50, M_STOP, M_PUBLISH, M_GIT, M_TERMINAL, M_PALETTE };
static const char MENUS[] =
	"MFichier\n"
	"I1\tNouveau site...\tCtrl+N\n" "I2\tOuvrir un dossier...\tCtrl+O\n" "-\n"
	"I3\tNouveau fichier...\n" "I4\tNouveau dossier...\n" "I5\tRenommer...\n" "I6\tSupprimer...\n" "-\n"
	"I7\tEnregistrer\tCtrl+S\n" "I8\tEnregistrer tout\tCtrl+Maj+S\n" "I9\tFermer l'onglet\tCtrl+W\n" "I10\tFermer le site\n" "-\n"
	"I11\tQuitter\tCtrl+Q\n"
	"MÉdition\n"
	"I20\tAnnuler\tCtrl+Z\n" "I21\tRétablir\tCtrl+Y\n" "-\n" "I22\tCouper\tCtrl+X\n" "I23\tCopier\tCtrl+C\n" "I24\tColler\tCtrl+V\n"
	"I25\tTout sélectionner\tCtrl+A\n" "-\n" "I26\tRechercher...\tCtrl+F\n" "I27\tRechercher dans le site...\tCtrl+Maj+F\n"
	"MAffichage\n"
	"I40c\tArbre des fichiers\tCtrl+B\n" "I41c\tPanneau du bas\tCtrl+J\n" "-\n" "I42\tJournal du serveur\n" "I43\tRésultats de la recherche\n"
	"MSite\n"
	"I50\tVoir dans Jet\tF5\n" "I51\tArrêter le serveur\tMaj+F5\n" "-\n" "I52\tPublier sur le serveur web de ce PC...\n"
	"I53\tGit...\n" "I54\tOuvrir un terminal ici\n" "-\n" "I55\tPalette...\n";

static void edit (long op)
{
	if (g_cur < 0) return;
	if (ui_textarea_edit (g_docs[g_cur].ed, op) == UN_CHANGE && !g_docs[g_cur].dirty) { g_docs[g_cur].dirty = true; refresh_tabs (); }
}
static void cmd_quit ()
{
	if (close_site ()) g_quit = true;
}
static void command (int id)
{
	switch (id) {
	case M_NEWSITE: cmd_new_site (); break;
	case M_OPENDIR: cmd_open_folder (); break;
	case M_NEWFILE: cmd_new_file (false); break;
	case M_NEWDIR: cmd_new_file (true); break;
	case M_RENAME: cmd_rename (); break;
	case M_DELETE: cmd_delete (); break;
	case M_SAVE: save_current (); break;
	case M_SAVEALL: save_all (); break;
	case M_CLOSETAB: close_doc (g_cur); break;
	case M_CLOSESITE: close_site (); break;
	case M_QUIT: cmd_quit (); break;
	case M_UNDO: edit (UTE_UNDO); break;
	case M_REDO: edit (UTE_REDO); break;
	case M_CUT: edit (UTE_CUT); break;
	case M_COPY: edit (UTE_COPY); break;
	case M_PASTE: edit (UTE_PASTE); break;
	case M_ALL: edit (UTE_ALL); break;
	case M_FIND: cmd_find (); break;
	case M_FINDSITE: cmd_find_site (); break;
	case M_TREE: g_treeShown = !g_treeShown; ui_menu_state (M_TREE, g_treeShown ? UMS_CHECKED : 0); relayout (); repaint (); break;
	case M_BOTTOM: g_bottom = !g_bottom; ui_menu_state (M_BOTTOM, g_bottom ? UMS_CHECKED : 0); relayout (); repaint (); break;
	case M_LOG: g_bottom = true; set_bottom (0); break;
	case M_HITS: g_bottom = true; set_bottom (1); break;
	case M_VIEW: cmd_view (); break;
	case M_STOP: srv_stop (); status (TR ("Serveur arrêté.")); break;
	case M_PUBLISH: cmd_publish (); break;
	case M_GIT: cmd_git (); break;
	case M_TERMINAL: cmd_terminal (); break;
	case M_PALETTE: cmd_palette (); break;
	}
}
static int g_keyHid;
static void control (long w, long code)
{
	if (w == h_new && code == UN_ACTIVATE) cmd_new_site ();
	else if (w == h_open && code == UN_ACTIVATE) cmd_open_folder ();
	else if (w == h_recent && code == UN_ACTIVATE) {
		long v = ui_w_get_value (h_recent);
		if (v >= 0 && v < g_nrecent) { char d[512]; cpy (d, g_recent[v], sizeof d); open_site (d); }
	} else if (w == h_served && code == UN_ACTIVATE) {
		long v = ui_w_get_value (h_served);
		const char *row = (const char *) uikit_list_get (h_served, v, 0);
		const char *r = row ? strchr (row, '\t') : 0;
		if (r) { char d[512]; cpy (d, r + 1, sizeof d); char *e = strchr (d, '\t'); if (e) *e = 0; open_site (d); }
	} else if (w == b_srv && code == UN_ACTIVATE) { if (g_srv > 0) srv_stop (); else srv_start (); }
	else if (w == b_jet && code == UN_ACTIVATE) cmd_view ();
	else if (w == b_pub && code == UN_ACTIVATE) cmd_publish ();
	else if (w == w_tree && code == UN_ACTIVATE) {	// (double-clic sur un fichier ; un dossier se déplie seul)
		Node *s = tree_sel ();
		if (s && !s->dir) open_doc (s->path);
	} else if (w == w_tabs && code == UN_CHANGE) {
		long v = ui_w_get_value (w_tabs);
		if (v >= 0 && v < g_ndocs && v != g_cur) show_doc ((int) v);
	} else if (w == w_tabs && code == UN_CLOSE) close_doc ((int) ui_tabstrip_closing (w_tabs));
	else if (w == w_seg && code == UN_CHANGE) set_bottom ((int) ui_w_get_value (w_seg));
	else if (w == w_log && code == UN_ACTIVATE) log_activate ();
	else if (w == w_hits && code == UN_ACTIVATE) {
		long v = ui_w_get_value (w_hits);
		if (v >= 0 && v < g_nhits) { int d = open_doc (g_hitpath[v]); if (d >= 0) goto_line (d, g_hits[v].line); }
	} else if (code == UN_CHANGE)
		for (int i = 0; i < g_ndocs; i++)
			if (g_docs[i].ed == w) {
				if (!g_docs[i].dirty) { g_docs[i].dirty = true; refresh_tabs (); }
				return;
			}
}

static void build_window ()
{
	s_jet = (const unsigned *) ui_tokens ();
	int h = ctl_h ();
	TOP_H = h + 20; TABS_H = h + 4; ST_H = 26; TREE_W = ui.w < 1000 ? 200 : 280; BOT_H = 220;
	w_site = ui_label (0, 0, 10, h, (long) "", ULB_BOLD);
	w_srv = ui_label (0, 0, 10, h, (long) "", 0);
	b_srv = ui_pushbutton (0, 0, 96, h, (long) TR ("Arrêter"), 0);
	b_jet = ui_pushbutton (0, 0, 150, h, (long) TR ("Voir dans Jet (F5)"), UB_DEFAULT);
	ui_w_tip (b_jet, (long) TR ("Lance le serveur si besoin et ouvre la page dans Jet ; ensuite, chaque enregistrement la recharge."));
	b_pub = ui_pushbutton (0, 0, 104, h, (long) TR ("Publier..."), 0);
	ui_w_tip (b_pub, (long) TR ("Ajoute ce site au serveur web de ce PC (Réglages › Serveur web)."));

	h_title = ui_label (0, 0, 10, 36, (long) "WebStudio", ULB_TITLE);
	h_sub = ui_label (0, 0, 10, 44, (long) TR ("Écrire un site, le servir sur Korin, le voir tout de suite."), ULB_WRAP | ULB_DIM);
	h_new = ui_pushbutton (0, 0, 10, h, (long) TR ("Nouveau site...  (Ctrl+N)"), UB_DEFAULT);
	h_open = ui_pushbutton (0, 0, 10, h, (long) TR ("Ouvrir un dossier...  (Ctrl+O)"), 0);
	h_hint = ui_label (0, 0, 10, 80, (long) TR ("Les sites vont dans HOME:/sites. Un site est un simple dossier : tout dossier s'ouvre."), ULB_WRAP | ULB_DIM);
	h_recentT = ui_label (0, 0, 10, 24, (long) TR ("Récents"), ULB_BOLD);
	h_recent = ui_list (0, 0, 10, 10, UL_HEADER | UL_NOSORT);
	ui_list_column (h_recent, (long) TR ("Site"), 180, 0);
	ui_list_column (h_recent, (long) TR ("Dossier"), 420, 0);
	h_servedT = ui_label (0, 0, 10, 24, (long) TR ("Sur le serveur web de ce PC"), ULB_BOLD);
	h_served = ui_list (0, 0, 10, 10, UL_HEADER | UL_NOSORT);
	ui_list_column (h_served, (long) TR ("Site"), 110, 0);
	ui_list_column (h_served, (long) TR ("Dossier"), 200, 0);
	ui_list_column (h_served, (long) TR ("Liaisons"), 190, 0);
	ui_list_column (h_served, (long) TR ("État"), 80, 0);

	w_tree = ui_tree (0, 0, 10, 10);
	w_tabs = ui_tabstrip (0, 0, 10, TABS_H, UTS_CLOSE);
	w_seg = ui_segmented (0, 0, 220, h, (long) TR ("Serveur|Recherche"));
	w_log = ui_list (0, 0, 10, 10, UL_HEADER | UL_NOSORT);
	static const struct { const char *t; int w; long f; } LC[] = { { "Heure", 80, 0 }, { "Méthode", 80, 0 }, { "Chemin", 320, 0 },
		{ "Code", 60, 0 }, { "Octets", 90, ULC_RIGHT }, { "Durée", 70, ULC_RIGHT }, { "Remarque", 260, 0 } };
	for (auto &c : LC) ui_list_column (w_log, (long) TR (c.t), c.w, c.f);
	ui_w_tip (w_log, (long) TR ("Double-clic sur un 404 : créer le fichier qui manque."));
	w_hits = ui_list (0, 0, 10, 10, UL_HEADER | UL_NOSORT);
	ui_list_column (w_hits, (long) TR ("Fichier"), 260, 0);
	ui_list_column (w_hits, (long) TR ("Ligne"), 60, ULC_RIGHT);
	ui_list_column (w_hits, (long) TR ("Texte"), 600, 0);
	w_status = ui_label (0, 0, 10, 20, (long) "", ULB_DIM);
	w_pos = ui_label (0, 0, 10, 20, (long) "", ULB_DIM | ULB_RIGHT);
	ui_menu ((long) MENUS);
	ui_menu_state (M_TREE, UMS_CHECKED);
	ui_menu_state (M_BOTTOM, UMS_CHECKED);
}

} // namespace ws

using namespace ws;

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	signal (SIGPIPE, SIG_IGN);
	g_nodes = (Node *) calloc (NODE_MAX, sizeof (Node));
	settings_load ();
	long scr[4] = { 1280, 800, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (1500, (int) scr[0] - 16), wh = imin (940, (int) (scr[1] - scr[3]) - imax ((int) scr[2], 60) - 16);
	if (ui_window_ex (8, scr[3] + 4, ww, wh, (long) "WebStudio", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (640, 420);
	ui_on_resize ((long) on_resize, 0);
	build_window ();
	home_fill ();
	title_update ();
	relayout ();
	repaint ();
	if (argc > 3 && !strcmp (argv[1], "--new")) {	// webstudio --new NOM MODELE [PALETTE] (empty, page, showcase, ou un modèle installé)
		templates_load ();
		int m = -1;
		for (int i = 0; i < BUILTIN; i++) if (!strcmp (argv[3], BUILTIN_IDS[i])) m = i;
		for (int i = 0; i < g_ntpl; i++) if (!strcmp (argv[3], g_tpl[i].id)) m = BUILTIN + i;
		if (m < 0) status (TR ("Ce modèle n'est pas installé."));
		else {
			int pal = 0;
			if (argc > 4 && m >= BUILTIN)
				for (int i = 0; i < g_tpl[m - BUILTIN].npal; i++) if (!strcmp (argv[4], g_tpl[m - BUILTIN].pal[i].id)) pal = i;
			create_site (argv[2], m, pal, false, false);
		}
	} else if (argc > 1 && is_dir (argv[1])) open_site (argv[1]);

	long lastCaret = -1;
	while (!g_quit) {
		long t = ui_event (50);
		if (t == WEV_CLOSE) { cmd_quit (); continue; }
		if (t == WEV_MENU) command ((int) ui_ev_field (&ui, EV_CODE));
		else if (t > 0) {
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			g_keyHid = press && (ui_ev_field (&ui, EV_MODS) & UI_MOD_CTRL) ? ui_ev_field (&ui, EV_CODE) : 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press) {
				long id = ui_menu_key (0);
				bool editop = id >= M_UNDO && id <= M_ALL;	// (ce que l'éditeur fait lui-même : pas deux fois)
				if (id >= 0 && !(editop && taken)) command ((int) id);
			}
		} else
			ui_w_idle ();
		srv_poll ();
		if (g_cur >= 0) {
			long col = 0, ln = uikit_textarea_caret (g_docs[g_cur].ed, &col);
			if (ln * 100000 + col != lastCaret) { lastCaret = ln * 100000 + col; status_pos (); }
		}
	}
	srv_stop ();
	return 0;
}
