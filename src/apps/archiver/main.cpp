//
// archiver -- le gestionnaire d'archives d'Onyx (user/Apps/archiver) sur UIKit et ArchiveKit : ouvrir une archive (ZIP, tar,
// tar.gz, gz), parcourir ses dossiers (l'arbre a gauche, la liste a droite, chercher dans tous les noms), extraire le choix
// ou tout (dossiers gardes ou a plat, dans un dossier au nom de l'archive), ouvrir un fichier avec son application (extrait
// dans RAM0:), verifier l'archive, en creer une, la changer : ajouter des fichiers et des dossiers (boutons, ou un depot sur
// la fenetre), nouveau dossier, renommer, supprimer. ArchiveKit n'ecrit pas sur place : un changement recrit l'archive dans
// un fichier voisin, qui prend sa place a la fin (save_changed). Le travail se fait entre deux evenements (pump) : la barre
// de progression avance, Annuler s'arrete a la prochaine entree.
//
// Un argument : l'archive a ouvrir (Fichiers, app.txt : opens).
//
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <spawn.h>
#include <sys/stat.h>
#include <initializer_list>
#include <uikit.h>
#include <archivekit_abi.h>

extern char **environ;
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-en.txt : le source est en francais)

static struct ui_vars ui;

enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3, J_ACCENT };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

typedef unsigned long u64;

#define W_WANT		880
#define H_WANT		580
#define ROW1		40			// la barre de boutons
#define ROW2		34			// le chemin, la recherche
#define FOOT		30
#define TREE_W		210
#define CTRL_H		28
#define RECENT_MAX	8
#define RECENT_FILE	"SD0:/apps/archiver.app/recent.txt"
#define TMP_DIR		"RAM0:/archiver"
#define PATH_MAX_	400

static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static void scat (char *d, const char *s, int cap) { int n = (int) strlen (d); scpy (d + n, s, cap - n); }
static char low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static bool ci_same (const char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (low (a[i]) != low (b[i])) return false; return true; }
static bool ci_contains (const char *s, const char *q)
{
	int n = (int) strlen (q);
	for (; *s; s++) if (ci_same (s, q, n)) return true;
	return false;
}
static const char *base_of (const char *p)
{
	const char *b = p;
	for (; *p; p++) if ((*p == '/' || *p == ':') && p[1]) b = p + 1;
	return b;
}
static bool ends_ci (const char *s, const char *suf)
{
	int a = (int) strlen (s), b = (int) strlen (suf);
	return a >= b && ci_same (s + a - b, suf, b);
}
static bool exists (const char *p) { struct stat st; return stat (p, &st) == 0; }
static bool is_dir (const char *p) { struct stat st; return stat (p, &st) == 0 && S_ISDIR (st.st_mode); }

// « 812 octets », « 34,2 Ko », « 118 Mo », « 2,31 Go »
static void size_text (u64 b, char *o, int cap)
{
	if (b < 1024) snprintf (o, (size_t) cap, "%u o", (unsigned) b);
	else if (b < 1024 * 1024) snprintf (o, (size_t) cap, TR ("%u,%u Ko"), (unsigned) (b / 1024), (unsigned) (b % 1024 * 10 / 1024));
	else if (b < 100ul * 1024 * 1024) snprintf (o, (size_t) cap, TR ("%u,%u Mo"), (unsigned) (b >> 20), (unsigned) ((b & 0xFFFFF) * 10 >> 20));
	else if (b < 1024ul * 1024 * 1024) snprintf (o, (size_t) cap, TR ("%u Mo"), (unsigned) (b >> 20));
	else snprintf (o, (size_t) cap, TR ("%u,%02u Go"), (unsigned) (b >> 30), (unsigned) ((b & 0x3FFFFFFF) * 100 >> 30));
}
static void civil (long days, int *y, int *m, int *d)
{
	long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
	long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1);
	*m = (int) (mp < 10 ? mp + 3 : mp - 9);
	*y = (int) (yoe + era * 400 + (*m <= 2));
}
static void date_text (long t, char *o, int cap)
{
	if (!t) { o[0] = 0; return; }
	int y, m, d; civil (t / 86400, &y, &m, &d);
	long s = t % 86400;
	snprintf (o, (size_t) cap, "%02d/%02d/%04d %02d:%02d", d, m, y, (int) (s / 3600), (int) (s % 3600 / 60));
}

// ---- le modele : l'archive en arbre ---------------------------------------------------------------------------
// Noeud 0 : l'archive ; un dossier par partie de chemin, que l'archive le liste ou non.
struct Node
{
	char *name;
	int   parent, entry;		// l'entree de l'archive (-1 : un dossier qu'elle ne liste pas)
	bool  dir, crypt;
	u64   size, packed;
	long  time;
	int   method;
	int   files;			// fichiers dessous
	int  *kids; int nkids, kcap;
	long  tree;			// sa ligne dans l'arbre (dossiers)
};
static long  g_arc;			// l'archive ouverte (0 : aucune)
static char  g_path[PATH_MAX_];
static int   g_fmt;
static Node *g_n; static int g_count, g_cap;
static int  *g_hash; static int g_hcap;

static unsigned hstr (int parent, const char *s, int len)
{
	unsigned h = 2166136261u ^ (unsigned) parent * 16777619u;
	for (int i = 0; i < len; i++) h = (h ^ (unsigned char) s[i]) * 16777619u;
	return h;
}
static int lookup (int parent, const char *s, int len)
{
	unsigned h = hstr (parent, s, len) & (unsigned) (g_hcap - 1);
	for (;;)
	{
		int k = g_hash[h];
		if (k < 0) return -1;
		if (g_n[k].parent == parent && !strncmp (g_n[k].name, s, (size_t) len) && g_n[k].name[len] == 0) return k;
		h = (h + 1) & (unsigned) (g_hcap - 1);
	}
}
static int make_node (int parent, const char *s, int len, bool dir)
{
	if (g_count == g_cap)
	{
		g_cap = g_cap ? g_cap * 2 : 256;
		g_n = (Node *) realloc (g_n, sizeof (Node) * (size_t) g_cap);
	}
	if ((g_count + 1) * 2 > g_hcap)				// la table de hachage suit
	{
		g_hcap = g_hcap ? g_hcap * 2 : 1024;
		free (g_hash);
		g_hash = (int *) malloc (sizeof (int) * (size_t) g_hcap);
		for (int i = 0; i < g_hcap; i++) g_hash[i] = -1;
		for (int i = 1; i < g_count; i++)
		{
			unsigned h = hstr (g_n[i].parent, g_n[i].name, (int) strlen (g_n[i].name)) & (unsigned) (g_hcap - 1);
			while (g_hash[h] >= 0) h = (h + 1) & (unsigned) (g_hcap - 1);
			g_hash[h] = i;
		}
	}
	int k = g_count++;
	Node &n = g_n[k];
	memset (&n, 0, sizeof n);
	n.name = (char *) malloc ((size_t) len + 1);
	memcpy (n.name, s, (size_t) len); n.name[len] = 0;
	n.parent = parent; n.entry = -1; n.dir = dir; n.tree = -1;
	if (parent >= 0)
	{
		unsigned h = hstr (parent, s, len) & (unsigned) (g_hcap - 1);
		while (g_hash[h] >= 0) h = (h + 1) & (unsigned) (g_hcap - 1);
		g_hash[h] = k;
		Node &p = g_n[parent];
		if (p.nkids == p.kcap) { p.kcap = p.kcap ? p.kcap * 2 : 8; p.kids = (int *) realloc (p.kids, sizeof (int) * (size_t) p.kcap); }
		p.kids[p.nkids++] = k;
	}
	return k;
}
static void model_clear ()
{
	for (int i = 0; i < g_count; i++) { free (g_n[i].name); free (g_n[i].kids); }
	free (g_n); g_n = 0; g_count = g_cap = 0;
	free (g_hash); g_hash = 0; g_hcap = 0;
}
static void model_totals (int k)
{
	Node &n = g_n[k];
	if (!n.dir) { n.files = 1; return; }
	u64 s = 0, p = 0; int f = 0;
	for (int i = 0; i < n.nkids; i++)
	{
		model_totals (n.kids[i]);
		Node &c = g_n[n.kids[i]];
		s += c.size; p += c.packed; f += c.files;
	}
	n.size = s; n.packed = p; n.files = f;
}
static void model_build ()
{
	model_clear ();
	make_node (-1, base_of (g_path), (int) strlen (base_of (g_path)), true);
	long cnt = ar_count (g_arc);
	for (long i = 0; i < cnt; i++)
	{
		long inf[8];
		if (ar_entry (g_arc, i, (long) inf) < 0) continue;
		const char *nm = (const char *) inf[0];
		int par = 0;
		for (;;)
		{
			while (*nm == '/') nm++;
			const char *e = nm; while (*e && *e != '/') e++;
			int len = (int) (e - nm);
			if (!len) break;
			bool last = !*e || !e[1];
			int k = lookup (par, nm, len);
			if (k < 0) k = make_node (par, nm, len, !last || inf[4] != 0);
			if (last)
			{
				Node &n = g_n[k];
				n.entry = (int) i; n.time = inf[3]; n.method = (int) inf[5]; n.crypt = (inf[7] & 1) != 0;
				if (!n.dir) { n.size = (u64) inf[1]; n.packed = (u64) inf[2]; }
			}
			par = k;
			nm = e;
			if (last) break;
		}
	}
	model_totals (0);
}
static void node_path (int k, char *o, int cap)		// son chemin dans l'archive (le noeud 0 : vide)
{
	if (k <= 0) { o[0] = 0; return; }
	node_path (g_n[k].parent, o, cap);
	if (o[0]) scat (o, "/", cap);
	scat (o, g_n[k].name, cap);
}

// ---- l'etat ----------------------------------------------------------------------------------------------------
static int  g_folder, g_hist[64], g_nhist;
static int  g_sort, g_desc;
static int *g_items; static int g_nitems, g_itemcap;	// les lignes de la liste : noeuds (-1 : le dossier parent)
static char g_search[128];
static char g_lastDir[PATH_MAX_] = "SD0:/home";
static char g_recent[RECENT_MAX][PATH_MAX_]; static int g_nrecent;
static bool g_quit, g_cancel, g_busy;
static long g_ww, g_wh;
static long g_btn[8];				// barre : Ouvrir Nouvelle Extraire Tout Ajouter Supprimer Verifier Infos
static long g_back, g_up, g_pathl, g_find, g_tree, g_list, g_status, g_right, g_bar, g_bcancel;
static long g_welcome, g_wopen, g_wnew, g_wlist;
static const char *BTN_TEXT[8] = { "Ouvrir", "Nouvelle", "Extraire", "Tout extraire", "Ajouter", "Supprimer", "Vérifier", "Infos" };
enum { C_OPEN = 1, C_NEW, C_EXTRACT, C_EXTRACT_ALL, C_ADD_FILE, C_ADD_DIR, C_DELETE, C_RENAME, C_NEWDIR, C_TEST, C_INFO, C_CLOSE, C_QUIT, C_OPENITEM, C_UP, C_BACK };

static void message (const char *t) { ui_message ((long) TR ("Archiveur"), (long) TR (t), UM_OK); }
static bool ask (const char *t) { return ui_message ((long) TR ("Archiveur"), (long) t, UM_YESNO) == 1; }
static void error_of (const char *what, long e)
{
	char t[300]; snprintf (t, sizeof t, TR ("%s : %s"), TR (what), (const char *) ar_strerror (e));
	message (t);
}
static void set_text (long w, const char *t) { ui_w_set_text (w, (long) t); }
static const char *text_of (long w) { return (const char *) ui_w_get_text (w); }

// ---- les lignes recentes -----------------------------------------------------------------------------------------
static void recent_load ()
{
	g_nrecent = 0;
	int f = open (RECENT_FILE, O_RDONLY);
	if (f < 0) return;
	static char b[4096];
	int n = (int) read (f, b, sizeof b - 1); close (f);
	if (n <= 0) return;
	b[n] = 0;
	for (char *s = b; *s && g_nrecent < RECENT_MAX; )
	{
		char *e = s; while (*e && *e != '\n' && *e != '\r') e++;
		char c = *e; *e = 0;
		if (*s && exists (s)) scpy (g_recent[g_nrecent++], s, PATH_MAX_);
		s = c ? e + 1 : e;
		while (*s == '\n' || *s == '\r') s++;
	}
}
static void recent_add (const char *path)
{
	static char out[4096];
	scpy (out, path, sizeof out); scat (out, "\n", sizeof out);
	int k = 1;
	for (int i = 0; i < g_nrecent && k < RECENT_MAX; i++)
		if (strcmp (g_recent[i], path)) { scat (out, g_recent[i], sizeof out); scat (out, "\n", sizeof out); k++; }
	int f = open (RECENT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f >= 0) { write (f, out, strlen (out)); close (f); }
	recent_load ();
}
static void recent_show ()
{
	ui_list_clear (g_wlist);
	for (int i = 0; i < g_nrecent; i++) ui_list_add (g_wlist, (long) g_recent[i], i);
	ui_w_draw (g_wlist);
}

// ---- la vue ------------------------------------------------------------------------------------------------------
static bool writable () { return g_arc && g_fmt != AR_FMT_GZIP; }
static int  sel_node ()
{
	long v = ui_w_get_value (g_list);
	if (v < 0 || v >= g_nitems) return -1;
	return g_items[v];
}
static void update_status ()
{
	char l[200] = "", r[160] = "", a[32], b[32];
	if (g_arc)
	{
		int k = sel_node ();
		if (k > 0)
		{
			size_text (g_n[k].size, a, sizeof a);
			snprintf (l, sizeof l, "%s  -  %s", g_n[k].name, a);
		}
		else
		{
			int items = g_nitems - (g_nitems && g_items[0] < 0);
			snprintf (l, sizeof l, TR (g_search[0] ? "%d élément(s) trouvé(s)" : items > 1 ? "%d éléments" : "%d élément"), items);
		}
		size_text (g_n[0].size, a, sizeof a); size_text (g_n[0].packed, b, sizeof b);
		snprintf (r, sizeof r, TR (g_n[0].files > 1 ? "%s  -  %d fichiers  -  %s" : "%s  -  %d fichier  -  %s"), (const char *) ar_format_name (g_fmt), g_n[0].files, a);
	}
	set_text (g_status, l); set_text (g_right, r);
}
static void update_state ()
{
	bool a = g_arc != 0, w = writable ();
	int k = a ? sel_node () : -1;
	ui_w_enable (g_btn[2], k > 0);
	ui_w_enable (g_btn[3], a);
	ui_w_enable (g_btn[4], w);
	ui_w_enable (g_btn[5], w && k > 0);
	ui_w_enable (g_btn[6], a);
	ui_w_enable (g_btn[7], a);
	ui_w_enable (g_back, g_nhist > 0);
	ui_w_enable (g_up, a && g_folder > 0);
	ui_menu_state (C_EXTRACT, k > 0 ? 0 : UMS_DISABLED);
	ui_menu_state (C_EXTRACT_ALL, a ? 0 : UMS_DISABLED);
	ui_menu_state (C_ADD_FILE, w ? 0 : UMS_DISABLED);
	ui_menu_state (C_ADD_DIR, w ? 0 : UMS_DISABLED);
	ui_menu_state (C_DELETE, w && k > 0 ? 0 : UMS_DISABLED);
	ui_menu_state (C_RENAME, w && k > 0 ? 0 : UMS_DISABLED);
	ui_menu_state (C_NEWDIR, w ? 0 : UMS_DISABLED);
	ui_menu_state (C_TEST, a ? 0 : UMS_DISABLED);
	ui_menu_state (C_INFO, a ? 0 : UMS_DISABLED);
	ui_menu_state (C_CLOSE, a ? 0 : UMS_DISABLED);
	update_status ();
}
static int cmp_nodes (int x, int y)
{
	if (x < 0) return -1;					// (le dossier parent reste en tete)
	if (y < 0) return 1;
	const Node &a = g_n[x], &b = g_n[y];
	if (a.dir != b.dir) return a.dir ? -1 : 1;
	long c = 0;
	switch (g_sort)
	{
	case 1: c = a.size < b.size ? -1 : a.size > b.size; break;
	case 2: c = a.packed < b.packed ? -1 : a.packed > b.packed; break;
	case 3: { u64 p = a.size ? a.packed * 1000 / a.size : 0, q = b.size ? b.packed * 1000 / b.size : 0; c = p < q ? -1 : p > q; break; }
	case 4: c = a.time < b.time ? -1 : a.time > b.time; break;
	case 5: c = a.method - b.method; break;
	}
	if (!c) c = strcasecmp (a.name, b.name);
	return (int) (g_desc ? -c : c);
}
static void sort_items (int *t, int *a, int n)		// fusion : un dossier de dix mille fichiers reste vif
{
	if (n < 2) return;
	int m = n / 2;
	sort_items (t, a, m); sort_items (t, a + m, n - m);
	int i = 0, j = m, k = 0;
	while (i < m && j < n) t[k++] = cmp_nodes (a[j], a[i]) < 0 ? a[j++] : a[i++];
	while (i < m) t[k++] = a[i++];
	while (j < n) t[k++] = a[j++];
	memcpy (a, t, sizeof (int) * (size_t) n);
}
static void item_add (int k)
{
	if (g_nitems == g_itemcap)
	{
		g_itemcap = g_itemcap ? g_itemcap * 2 : 256;
		g_items = (int *) realloc (g_items, sizeof (int) * (size_t) g_itemcap);
	}
	g_items[g_nitems++] = k;
}
static void row_text (int k, char *o, int cap)
{
	const Node &n = g_n[k];
	char nm[300], s[32] = "", p[32] = "", r[16] = "", d[32] = "";
	if (g_search[0]) node_path (k, nm, sizeof nm); else scpy (nm, n.name, sizeof nm);
	if (!n.dir || n.nkids)
	{
		size_text (n.size, s, sizeof s);
		size_text (n.packed, p, sizeof p);
		if (n.size) { int v = (int) (100 - n.packed * 100 / n.size); snprintf (r, sizeof r, "%d %%", v < 0 ? 0 : v); }
	}
	date_text (n.time, d, sizeof d);
	const char *m = n.dir ? "" : n.crypt ? TR ("Chiffré") : n.method == 8 ? "Deflate" : n.method == 0 ? (g_fmt == AR_FMT_TAR || g_fmt == AR_FMT_TGZ ? "Tar" : TR ("Stocké")) : "?";
	snprintf (o, (size_t) cap, "%s\t%s\t%s\t%s\t%s\t%s", nm, s, p, r, d, m);
}
static void list_fill ()
{
	g_nitems = 0;
	if (g_search[0])
	{
		for (int i = 1; i < g_count; i++) if (ci_contains (g_n[i].name, g_search)) item_add (i);
	}
	else
	{
		if (g_folder > 0) item_add (-1);
		for (int i = 0; i < g_n[g_folder].nkids; i++) item_add (g_n[g_folder].kids[i]);
	}
	int *t = (int *) malloc (sizeof (int) * (size_t) (g_nitems + 1));
	sort_items (t, g_items, g_nitems);
	free (t);
	ui_list_clear (g_list);
	static char row[600];
	for (int i = 0; i < g_nitems; i++)
	{
		int k = g_items[i];
		if (k < 0) { ui_list_add (g_list, (long) "..\t\t\t\t\t", 1); continue; }
		row_text (k, row, sizeof row);
		ui_list_add (g_list, (long) row, g_n[k].dir ? 1 : 0);
	}
	ui_w_set_value (g_list, -1);
	ui_w_draw (g_list);
}
static void path_update ()
{
	char p[PATH_MAX_ + 8], f[PATH_MAX_];
	node_path (g_folder, f, sizeof f);
	if (g_search[0]) snprintf (p, sizeof p, TR ("Recherche dans %s"), base_of (g_path));
	else snprintf (p, sizeof p, "%s%s%s", g_arc ? base_of (g_path) : "", f[0] ? " / " : "", f);
	set_text (g_pathl, p);
}
static void show_folder (int k, bool remember)
{
	if (k != g_folder && remember)
	{
		if (g_nhist == 64) { memmove (g_hist, g_hist + 1, 63 * sizeof (int)); g_nhist--; }
		g_hist[g_nhist++] = g_folder;
	}
	g_folder = k;
	list_fill ();
	path_update ();
	if (g_n[k].tree >= 0 && ui_w_get_value (g_tree) != g_n[k].tree) { ui_w_set_value (g_tree, g_n[k].tree); ui_w_draw (g_tree); }
	update_state ();
}
static void tree_add (int k, long parent)
{
	long id = ui_tree_add (g_tree, parent, (long) g_n[k].name, k);
	g_n[k].tree = id;
	for (int i = 0; i < g_n[k].nkids; i++) if (g_n[g_n[k].kids[i]].dir) tree_add (g_n[k].kids[i], id);
}
static void show_arc (bool on)
{
	ui_w_show (g_welcome, !on); ui_w_show (g_wopen, !on); ui_w_show (g_wnew, !on); ui_w_show (g_wlist, !on);
	ui_w_show (g_tree, on && g_ww >= 640); ui_w_show (g_list, on);
	ui_w_show (g_find, on); ui_w_show (g_back, on); ui_w_show (g_up, on); ui_w_show (g_pathl, on);
	ui_w_show (g_status, on); ui_w_show (g_right, on);
	ui_fill (0, ROW1, g_ww, g_wh - ROW1, jet (J_FACE));
	if (!on) { ui_title ((long) TR ("Archiveur")); recent_show (); ui_view_dirty (g_welcome); }
}
static void arc_close ()
{
	if (g_arc) ar_close (g_arc);
	g_arc = 0; g_path[0] = 0; g_folder = 0; g_nhist = 0; g_search[0] = 0;
	model_clear ();
	make_node (-1, "", 0, true);
	ui_tree_clear (g_tree);
	set_text (g_find, "");
	show_arc (false);
	ui_list_clear (g_list); g_nitems = 0;
	update_state ();
}
// (x0 : la vue suit l'archive) -> false : elle ne s'ouvre pas
static bool arc_open (const char *path, bool quiet = false)
{
	long a = ar_open ((long) path);
	if (a < 0 || !a)
	{
		if (!quiet) { char t[PATH_MAX_ + 40]; snprintf (t, sizeof t, TR ("Impossible d'ouvrir %s"), path); error_of (t, a); }
		return false;
	}
	if (g_arc) ar_close (g_arc);
	g_arc = a;
	scpy (g_path, path, sizeof g_path);
	g_fmt = (int) ar_format (a);
	model_build ();
	ui_tree_clear (g_tree);
	tree_add (0, -1);
	ui_tree_expand (g_tree, g_n[0].tree, 1);
	g_folder = 0; g_nhist = 0; g_search[0] = 0;
	set_text (g_find, "");
	show_arc (true);
	char t[300]; snprintf (t, sizeof t, TR ("%s - Archiveur"), base_of (path));
	ui_title ((long) t);
	scpy (g_lastDir, path, sizeof g_lastDir);
	char *e = strrchr (g_lastDir, '/'); if (e) *e = 0;
	recent_add (path);
	show_folder (0, false);
	ui_w_draw (0);
	return true;
}

// ---- le travail : une barre, Annuler ----------------------------------------------------------------------------
static void busy_begin (const char *what, long max)
{
	g_busy = true; g_cancel = false;
	set_text (g_status, TR (what));
	ui_w_show (g_right, false);
	ui_w_set_range (g_bar, 0, max > 0 ? max : 1, 0, 0);
	ui_w_set_value (g_bar, 0);
	ui_w_show (g_bar, true); ui_w_show (g_bcancel, true);
	ui_w_draw (0); ui_view_flush ();
	ui_cursor (UC_WAIT);
}
static void busy_end ()
{
	g_busy = false;
	ui_w_show (g_bar, false); ui_w_show (g_bcancel, false); ui_w_show (g_right, true);
	ui_cursor (UC_ARROW);
	ui_fill (0, g_wh - FOOT, g_ww, FOOT, jet (J_FACE));
	ui_w_draw (0);
	update_status ();
}
// les evenements arrives pendant le travail : Annuler, fermer -> false : on s'arrete
static bool pump (long v)
{
	ui_w_set_value (g_bar, v);
	for (;;)
	{
		long t = ui_event (0);
		if (t <= 0) break;
		if (t == WEV_CLOSE) { g_quit = g_cancel = true; break; }
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);
		if (w == g_bcancel && code == UN_ACTIVATE) g_cancel = true;
	}
	ui_view_flush ();
	return !g_cancel;
}

// ---- extraire ----------------------------------------------------------------------------------------------------
struct Job { int node; char rel[PATH_MAX_]; };
static Job *g_jobs; static int g_njobs, g_jobcap;
static u64 g_jobbytes;
static void job_add (int k, const char *rel)
{
	if (g_njobs == g_jobcap) { g_jobcap = g_jobcap ? g_jobcap * 2 : 256; g_jobs = (Job *) realloc (g_jobs, sizeof (Job) * (size_t) g_jobcap); }
	Job &j = g_jobs[g_njobs++];
	j.node = k; scpy (j.rel, rel, PATH_MAX_);
	g_jobbytes += g_n[k].size;
}
static void jobs_of (int k, const char *rel, bool flat)
{
	const Node &n = g_n[k];
	if (!n.dir) { job_add (k, flat ? n.name : rel); return; }
	if (!flat && rel[0]) job_add (k, rel);
	for (int i = 0; i < n.nkids; i++)
	{
		char r[PATH_MAX_];
		scpy (r, rel, sizeof r);
		if (r[0]) scat (r, "/", sizeof r);
		scat (r, g_n[n.kids[i]].name, sizeof r);
		jobs_of (n.kids[i], r, flat);
	}
}
// un nom d'archive ne sort jamais du dossier choisi
static bool safe_rel (const char *r)
{
	if (r[0] == '/') return false;
	for (const char *p = r; *p; )
	{
		const char *e = p; while (*e && *e != '/') e++;
		if (e - p == 2 && p[0] == '.' && p[1] == '.') return false;
		if (memchr (p, ':', (size_t) (e - p))) return false;
		p = *e ? e + 1 : e;
	}
	return true;
}
static void mkdirs (const char *path)
{
	char p[PATH_MAX_ + 64]; scpy (p, path, sizeof p);
	for (char *s = p; *s; s++)
		if (*s == '/' && s > p && s[-1] != ':') { *s = 0; mkdir (p, 0755); *s = '/'; }
	mkdir (p, 0755);
}
// les entrees de g_jobs dans dest -> fichiers ecrits
static int run_jobs (const char *dest, bool replace)
{
	int done = 0, skipped = 0, bad = 0, firstErr = 0;
	mkdirs (dest);
	u64 sum = 0;
	long scale = g_jobbytes >> 30 ? 1024 : 1;		// (ui_progress prend des long : 1 Ko de grain au-dela du Go)
	busy_begin ("Extraction...", (long) g_jobbytes / scale + 1);
	for (int i = 0; i < g_njobs; i++)
	{
		if (!pump ((long) sum / scale)) break;
		const Job &j = g_jobs[i];
		const Node &n = g_n[j.node];
		sum += n.size;
		if (!safe_rel (j.rel)) { skipped++; continue; }
		char to[PATH_MAX_ + 128]; snprintf (to, sizeof to, "%s/%s", dest, j.rel);
		if (n.dir) { mkdirs (to); continue; }
		if (n.crypt) { bad++; if (!firstErr) firstErr = -13; continue; }
		if (!replace && exists (to)) { skipped++; continue; }
		long r = ar_extract (g_arc, n.entry, (long) to);
		if (r < 0) { bad++; if (!firstErr) firstErr = (int) r; continue; }
		done++;
		set_text (g_status, n.name);
	}
	busy_end ();
	char t[300];
	if (g_quit) return done;
	if (bad) { snprintf (t, sizeof t, TR ("%d fichier(s) extrait(s), %d en erreur : %s"), done, bad, (const char *) ar_strerror (firstErr)); message (t); }
	else if (g_cancel) { snprintf (t, sizeof t, TR ("Extraction annulée : %d fichier(s) extrait(s)."), done); set_text (g_status, t); }
	else { snprintf (t, sizeof t, TR ("%d fichier(s) extrait(s) dans %s%s"), done, dest, skipped ? TR (" (certains ignorés)") : ""); set_text (g_status, t); }
	return done;
}

// le dialogue d'extraction
static struct { long dest, sub, keep, repl, browse; } xd;
static long xd_fn (long d, long w, long code)
{
	(void) d;
	if (w == xd.browse && code == UN_ACTIVATE)
	{
		static char p[PATH_MAX_];
		if (ui_folder_choose ((long) text_of (xd.dest), (long) p, sizeof p) == 1) set_text (xd.dest, p);
	}
	return 0;
}
static void cmd_extract (bool all)
{
	if (!g_arc) return;
	int k = all ? 0 : sel_node ();
	if (k < 0 || (!all && k == 0)) return;
	int W = 500, H = 248;
	long d = ui_dialog_begin (W, H, (long) TR (all ? "Tout extraire" : "Extraire"));
	if (!d) return;
	ui_label (16, 40, 90, CTRL_H, (long) "Vers", 0);
	xd.dest = ui_textbox (70, 40, W - 186, CTRL_H, 0);
	set_text (xd.dest, g_lastDir);
	xd.browse = ui_pushbutton (W - 108, 40, 92, CTRL_H, (long) "Parcourir...", 0);
	xd.sub = ui_checkbox (16, 82, W - 32, 24, (long) TR ("Dans un dossier au nom de l'archive"), all ? 1 : 0);
	xd.keep = ui_checkbox (16, 110, W - 32, 24, (long) TR ("Garder les dossiers"), 1);
	xd.repl = ui_checkbox (16, 138, W - 32, 24, (long) TR ("Remplacer les fichiers qui existent"), 0);
	long e = ui_dialog_button (d, 0, (long) "Extraire", 1, UB_DEFAULT, W - 12);
	ui_dialog_button (d, 1, (long) "Annuler", 0, 0, e);
	ui_dialog_cancel (d, 0);
	int r = (int) ui_dialog_run (d, (long) xd_fn, 0);
	static char dest[PATH_MAX_];
	scpy (dest, text_of (xd.dest), sizeof dest);
	bool sub = ui_w_get_value (xd.sub) != 0, keep = ui_w_get_value (xd.keep) != 0, repl = ui_w_get_value (xd.repl) != 0;
	ui_dialog_end (d);
	ui_w_draw (0);
	if (r != 1 || !dest[0]) return;
	scpy (g_lastDir, dest, sizeof g_lastDir);
	if (sub)
	{
		char nm[160]; scpy (nm, base_of (g_path), sizeof nm);
		for (const char *suf : { ".tar.gz", ".tgz", ".zip", ".tar", ".gz" })
			if (ends_ci (nm, suf)) { nm[strlen (nm) - strlen (suf)] = 0; break; }
		scat (dest, "/", sizeof dest); scat (dest, nm, sizeof dest);
	}
	g_njobs = 0; g_jobbytes = 0;
	jobs_of (k, all ? "" : g_n[k].name, !keep);
	run_jobs (dest, repl);
}

// ---- ouvrir un fichier de l'archive : extrait dans RAM0:, ouvert par son application ------------------------------
// with : « Ouvrir avec » (UIKit) ; sinon l'application de son extension (ui_assoc_open : app.txt, fileassoc.ini)
static void open_item (int k, bool with = false)
{
	const Node &n = g_n[k];
	if (n.dir) { if (g_search[0]) { g_search[0] = 0; set_text (g_find, ""); } show_folder (k, true); return; }
	if (n.crypt) { message ("Ce fichier est chiffré : ArchiveKit ne sait pas le lire."); return; }
	mkdir (TMP_DIR, 0755);
	char to[PATH_MAX_]; snprintf (to, sizeof to, "%s/%s", TMP_DIR, n.name);
	long r = ar_extract (g_arc, n.entry, (long) to);
	if (r < 0) { error_of ("Impossible d'extraire le fichier", r); return; }
	if ((with ? ui_open_with ((long) to) : ui_assoc_open ((long) to)) < 0) message ("Impossible de lancer l'application de ce fichier.");
}

// ---- changer l'archive : elle est recrite a cote, puis prend la place de l'ancienne -------------------------------
struct Change
{
	const bool *drop;			// les entrees retirees (par indice d'entree ; 0 : aucune)
	const char *from, *to;			// un prefixe de nom remplace (0 : aucun)
	const char *const *adds; int nadds;	// les chemins a ajouter
	const char *into;			// leur dossier dans l'archive ("" : la racine)
	const char *newdir;			// un dossier a creer
};
static bool save_changed (const Change &c)
{
	char tmp[PATH_MAX_ + 8]; snprintf (tmp, sizeof tmp, "%s.tmp", g_path);
	long cnt = ar_count (g_arc);
	long w = ar_create ((long) tmp, g_fmt);
	if (w < 0 || !w) { error_of ("Impossible d'écrire l'archive", w); return false; }
	busy_begin ("Écriture de l'archive...", cnt + c.nadds + 1);
	long err = 0;
	for (long i = 0; i < cnt && !err; i++)
	{
		if (!pump (i)) { err = 1; break; }
		if (c.drop && c.drop[i]) continue;
		long inf[8];
		if (ar_entry (g_arc, i, (long) inf) < 0) continue;
		char nm[PATH_MAX_ + 200];
		scpy (nm, (const char *) inf[0], sizeof nm);
		if (c.from)
		{
			int fl = (int) strlen (c.from);
			if (!strncmp (nm, c.from, (size_t) fl) && (nm[fl] == 0 || nm[fl] == '/'))
			{
				char rest[PATH_MAX_]; scpy (rest, nm + fl, sizeof rest);
				snprintf (nm, sizeof nm, "%s%s", c.to, rest);
			}
		}
		ar_set_time (w, inf[3]);
		if (inf[4]) { err = ar_add_dir (w, (long) nm); continue; }
		long b = ar_load (g_arc, i);
		if (b < 0 || !b) { err = b ? b : -5; break; }
		err = ar_add_mem (w, (long) nm, ar_data (b), ar_bytes (b), inf[5] ? AR_DEFAULT : 0);
		ar_free (b);
	}
	ar_set_time (w, 0);
	if (!err && c.newdir) err = ar_add_dir (w, (long) c.newdir);
	for (int i = 0; i < c.nadds && !err; i++)
	{
		if (!pump (cnt + i)) { err = 1; break; }
		char nm[PATH_MAX_ + 200];
		snprintf (nm, sizeof nm, "%s%s%s", c.into, c.into[0] ? "/" : "", base_of (c.adds[i]));
		err = ar_add_file (w, (long) nm, (long) c.adds[i], AR_DEFAULT);
	}
	long fin = 0;
	if (!err) { fin = ar_finish (w); if (fin < 0) err = fin; }
	ar_free (w);
	busy_end ();
	if (err)
	{
		unlink (tmp);
		if (err != 1) error_of ("L'archive n'a pas été changée", err);
		return false;
	}
	ar_close (g_arc); g_arc = 0;
	static char keep[PATH_MAX_]; scpy (keep, g_path, sizeof keep);
	unlink (keep);
	if (rename (tmp, keep)) { char t[500]; snprintf (t, sizeof t, TR ("L'archive a été écrite sous le nom %s"), tmp); message (t); arc_close (); return false; }
	int f = g_folder; char fp[PATH_MAX_]; node_path (f, fp, sizeof fp);
	arc_open (keep);
	if (!g_arc) { arc_close (); return false; }
	int at = 0;					// le dossier montre, s'il existe encore
	for (char *s = fp; *s; )
	{
		char *e = s; while (*e && *e != '/') e++;
		int k = lookup (at, s, (int) (e - s));
		if (k < 0) break;
		at = k; s = *e ? e + 1 : e;
	}
	show_folder (at, false);
	return true;
}
static void cmd_add (bool folder)
{
	if (!writable ()) return;
	static char p[PATH_MAX_];
	long r = folder ? ui_folder_choose ((long) g_lastDir, (long) p, sizeof p) : ui_file_open ((long) g_lastDir, (long) p, sizeof p);
	ui_w_draw (0);
	if (r != 1) return;
	char into[PATH_MAX_]; node_path (g_folder, into, sizeof into);
	const char *adds[1] = { p };
	Change c = {}; c.adds = adds; c.nadds = 1; c.into = into;
	save_changed (c);
}
static void mark_drop (int k, bool *drop)
{
	const Node &n = g_n[k];
	if (n.entry >= 0) drop[n.entry] = true;
	for (int i = 0; i < n.nkids; i++) mark_drop (n.kids[i], drop);
}
static void cmd_delete ()
{
	int k = sel_node ();
	if (!writable () || k <= 0) return;
	char t[400]; snprintf (t, sizeof t, TR ("Retirer « %s » de l'archive ?"), g_n[k].name);
	if (!ask (t)) return;
	long cnt = ar_count (g_arc);
	bool *drop = (bool *) calloc ((size_t) cnt + 1, 1);
	mark_drop (k, drop);
	Change c = {}; c.drop = drop; c.into = "";
	save_changed (c);
	free (drop);
}
static void cmd_rename ()
{
	int k = sel_node ();
	if (!writable () || k <= 0) return;
	static char nm[200]; scpy (nm, g_n[k].name, sizeof nm);
	if (ui_ask ((long) TR ("Renommer"), (long) TR ("Nouveau nom"), (long) nm, sizeof nm) != 1) { ui_w_draw (0); return; }
	ui_w_draw (0);
	if (!nm[0] || strchr (nm, '/')) return;
	char from[PATH_MAX_], to[PATH_MAX_]; node_path (k, from, sizeof from);
	node_path (g_n[k].parent, to, sizeof to);
	if (to[0]) scat (to, "/", sizeof to);
	scat (to, nm, sizeof to);
	Change c = {}; c.from = from; c.to = to; c.into = "";
	save_changed (c);
}
static void cmd_newdir ()
{
	if (!writable ()) return;
	static char nm[200]; nm[0] = 0;
	if (ui_ask ((long) TR ("Nouveau dossier"), (long) TR ("Nom du dossier"), (long) nm, sizeof nm) != 1) { ui_w_draw (0); return; }
	ui_w_draw (0);
	if (!nm[0] || strchr (nm, '/')) return;
	char d[PATH_MAX_]; node_path (g_folder, d, sizeof d);
	if (d[0]) scat (d, "/", sizeof d);
	scat (d, nm, sizeof d);
	Change c = {}; c.newdir = d; c.into = "";
	save_changed (c);
}

// ---- verifier, infos -------------------------------------------------------------------------------------------
static void cmd_test ()
{
	if (!g_arc) return;
	long cnt = ar_count (g_arc), bad = 0, first = 0;
	char firstName[300] = "";
	busy_begin ("Vérification...", cnt + 1);
	for (long i = 0; i < cnt; i++)
	{
		if (!pump (i)) break;
		long r = ar_test (g_arc, i);
		if (r < 0) { if (!bad) { first = r; scpy (firstName, (const char *) ar_name (g_arc, i), sizeof firstName); } bad++; }
	}
	busy_end ();
	if (g_quit) return;
	char t[500];
	if (g_cancel) snprintf (t, sizeof t, "%s", TR ("Vérification annulée."));
	else if (bad) snprintf (t, sizeof t, TR ("%ld entrée(s) en erreur. La première, %s : %s"), bad, firstName, (const char *) ar_strerror (first));
	else snprintf (t, sizeof t, TR ("Tout est bon : %ld entrée(s) vérifiée(s)."), cnt);
	message (t);
}
static void cmd_info ()
{
	if (!g_arc) return;
	char a[32], b[32], t[700];
	size_text (g_n[0].size, a, sizeof a); size_text (g_n[0].packed, b, sizeof b);
	int ratio = g_n[0].size ? (int) (100 - g_n[0].packed * 100 / g_n[0].size) : 0;
	int dirs = 0; for (int i = 1; i < g_count; i++) dirs += g_n[i].dir;
	snprintf (t, sizeof t, TR ("%s\n\nFormat : %s\n%d fichier(s), %d dossier(s)\nTaille d'origine : %s\nTaille dans l'archive : %s\nCompression : %d %%"),
		g_path, (const char *) ar_format_name (g_fmt), g_n[0].files, dirs, a, b, ratio < 0 ? 0 : ratio);
	message (t);
}

// ---- creer une archive ---------------------------------------------------------------------------------------
static struct { long path, fmt, level, list, addf, addd, del; } nd;
static char g_sources[32][PATH_MAX_]; static int g_nsources;
static const char *const FMT_EXT[4] = { ".zip", ".tar", ".tar.gz", ".gz" };
static const int FMT_ID[4] = { AR_FMT_ZIP, AR_FMT_TAR, AR_FMT_TGZ, AR_FMT_GZIP };
static void nd_list ()
{
	ui_list_clear (nd.list);
	for (int i = 0; i < g_nsources; i++) ui_list_add (nd.list, (long) g_sources[i], is_dir (g_sources[i]) ? 1 : 0);
	ui_w_draw (nd.list);
}
static void nd_fix_ext ()			// le chemin suit le format choisi
{
	static char p[PATH_MAX_]; scpy (p, text_of (nd.path), sizeof p);
	for (const char *suf : { ".tar.gz", ".tgz", ".zip", ".tar", ".gz" })
		if (ends_ci (p, suf)) { p[strlen (p) - strlen (suf)] = 0; break; }
	long f = ui_w_get_value (nd.fmt);
	scat (p, FMT_EXT[f >= 0 && f < 4 ? f : 0], sizeof p);
	set_text (nd.path, p);
}
static long nd_fn (long d, long w, long code)
{
	(void) d;
	static char p[PATH_MAX_];
	if (w == nd.fmt && code == UN_CHANGE) nd_fix_ext ();
	if (code != UN_ACTIVATE) return 0;
	if (w == nd.addf && g_nsources < 32)
	{
		if (ui_file_open ((long) g_lastDir, (long) p, sizeof p) == 1) { scpy (g_sources[g_nsources++], p, PATH_MAX_); nd_list (); }
	}
	else if (w == nd.addd && g_nsources < 32)
	{
		if (ui_folder_choose ((long) g_lastDir, (long) p, sizeof p) == 1) { scpy (g_sources[g_nsources++], p, PATH_MAX_); nd_list (); }
	}
	else if (w == nd.del)
	{
		long v = ui_w_get_value (nd.list);
		if (v >= 0 && v < g_nsources)
		{
			for (long i = v; i + 1 < g_nsources; i++) strcpy (g_sources[i], g_sources[i + 1]);
			g_nsources--; nd_list ();
		}
	}
	return 0;
}
static void cmd_new ()
{
	int W = 540, H = 404;
	long d = ui_dialog_begin (W, H, (long) "Nouvelle archive");
	if (!d) return;
	g_nsources = 0;
	ui_label (16, 40, 70, CTRL_H, (long) "Fichier", 0);
	nd.path = ui_textbox (84, 40, W - 100, CTRL_H, 0);
	char p[PATH_MAX_ + 16]; snprintf (p, sizeof p, "%s/archive.zip", g_lastDir);
	set_text (nd.path, p);
	ui_label (16, 78, 70, CTRL_H, (long) "Format", 0);
	nd.fmt = ui_dropdown (84, 78, 130, CTRL_H, (long) "ZIP|tar|tar.gz|gz");
	ui_label (232, 78, 90, CTRL_H, (long) "Compression", 0);
	nd.level = ui_dropdown (328, 78, W - 344, CTRL_H, (long) TR ("Normale|Rapide|Forte|Aucune"));
	ui_w_set_value (nd.fmt, 0); ui_w_set_value (nd.level, 0);
	ui_label (16, 116, W - 32, 20, (long) "Contenu", ULB_BOLD);
	nd.list = ui_list (16, 138, W - 32 - 120, H - 138 - 60, UL_ICONS);
	ui_list_column (nd.list, 0, W - 32 - 130, 0);
	nd.addf = ui_pushbutton (W - 128, 138, 112, CTRL_H, (long) "Fichier...", 0);
	nd.addd = ui_pushbutton (W - 128, 172, 112, CTRL_H, (long) "Dossier...", 0);
	nd.del = ui_pushbutton (W - 128, 206, 112, CTRL_H, (long) "Retirer", 0);
	long e = ui_dialog_button (d, 0, (long) "Créer", 1, UB_DEFAULT, W - 12);
	ui_dialog_button (d, 1, (long) "Annuler", 0, 0, e);
	ui_dialog_cancel (d, 0);
	int r = (int) ui_dialog_run (d, (long) nd_fn, 0);
	static char path[PATH_MAX_]; scpy (path, text_of (nd.path), sizeof path);
	long f = ui_w_get_value (nd.fmt), l = ui_w_get_value (nd.level);
	ui_dialog_end (d);
	ui_w_draw (0);
	if (r != 1 || !path[0]) return;
	if (f < 0 || f > 3) f = 0;
	if (exists (path)) { char t[500]; snprintf (t, sizeof t, TR ("%s existe déjà. Le remplacer ?"), path); if (!ask (t)) return; }
	if (!g_nsources) { message ("Une archive vide : ajoutez d'abord des fichiers."); return; }
	if (FMT_ID[f] == AR_FMT_GZIP && (g_nsources != 1 || is_dir (g_sources[0]))) { message ("Le format gz ne garde qu'un seul fichier."); return; }
	static const long LEVEL[4] = { AR_DEFAULT, AR_FAST, AR_BEST, AR_STORE };
	long w = ar_create ((long) path, FMT_ID[f]);
	if (w < 0 || !w) { error_of ("Impossible de créer l'archive", w); return; }
	busy_begin ("Création de l'archive...", g_nsources + 1);
	long err = 0;
	for (int i = 0; i < g_nsources && !err; i++)
	{
		if (!pump (i)) { err = 1; break; }
		err = ar_add_file (w, 0, (long) g_sources[i], LEVEL[l >= 0 && l < 4 ? l : 0]);
	}
	long fin = 0;
	if (!err) { fin = ar_finish (w); if (fin < 0) err = fin; }
	ar_free (w);
	busy_end ();
	if (err) { unlink (path); if (err != 1) error_of ("Impossible de créer l'archive", err); return; }
	arc_open (path);
}
static void cmd_open ()
{
	static char p[PATH_MAX_];
	ui_file_filter ((long) "*.zip;*.tar;*.gz;*.tgz");
	long r = ui_file_open ((long) g_lastDir, (long) p, sizeof p);
	ui_file_filter (0);
	ui_w_draw (0);
	if (r == 1) arc_open (p);
}
static void on_drop ()
{
	static char b[8192];
	long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = 0;
	const char *adds[32]; int na = 0;
	for (char *s = b; *s && na < 32; )
	{
		char *e = s; while (*e && *e != '\n' && *e != '\r') e++;
		char c = *e; *e = 0;
		if (*s) adds[na++] = s;
		s = c ? e + 1 : e;
		while (*s == '\n' || *s == '\r') s++;
	}
	if (!na) return;
	if (!g_arc) { arc_open (adds[0]); return; }
	if (!writable ()) { message ("Une archive gz ne garde qu'un seul fichier."); return; }
	char t[200]; snprintf (t, sizeof t, TR ("Ajouter %d élément(s) à l'archive ?"), na);
	if (!ask (t)) return;
	char into[PATH_MAX_]; node_path (g_folder, into, sizeof into);
	Change c = {}; c.adds = adds; c.nadds = na; c.into = into;
	save_changed (c);
}
static void cmd_add_menu ()
{
	char t[64]; int n = 0;
	for (const char *s : { TR ("Des fichiers..."), TR ("Un dossier..."), TR ("Nouveau dossier...") }) { strcpy (t + n, s); n += (int) strlen (s) + 1; }
	t[n] = 0;
	long x = 8;
	for (int i = 0; i < 4; i++) x += ui_text_width ((long) BTN_TEXT[i], 13, (long) ui.font) + 26;
	long r = ui_popup_local (x, ROW1, (long) t, 190);
	if (r == 0) cmd_add (false);
	else if (r == 1) cmd_add (true);
	else if (r == 2) cmd_newdir ();
}
static void go_up () { if (g_folder > 0) show_folder (g_n[g_folder].parent, true); }
static void go_back () { if (g_nhist) { int k = g_hist[--g_nhist]; show_folder (k, false); } }
static void activate_row ()
{
	long v = ui_w_get_value (g_list);
	if (v < 0 || v >= g_nitems) return;
	if (g_items[v] < 0) go_up(); else open_item (g_items[v]);
}
static void context_menu ()
{
	int k = sel_node ();
	if (k <= 0) return;
	char t[160]; int n = 0;
	bool w = writable (), file = !g_n[k].dir;
	const char *L[5] = { TR (file ? "Ouvrir avec son application" : "Ouvrir"), TR ("Ouvrir avec..."), TR ("Extraire..."), TR ("Renommer..."), TR ("Supprimer") };
	for (int i = 0; i < (w ? 5 : 3); i++) if (i != 1 || file) { strcpy (t + n, L[i]); n += (int) strlen (L[i]) + 1; }
	t[n] = 0;
	long r = ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), (long) t, 220);
	if (r > 0 && !file) r++;			// (un dossier : pas de « Ouvrir avec »)
	switch (r)
	{
	case 0: open_item (k); break;
	case 1: open_item (k, true); break;
	case 2: cmd_extract (false); break;
	case 3: cmd_rename (); break;
	case 4: cmd_delete (); break;
	}
}
static void command (int c)
{
	if (g_busy) return;
	switch (c)
	{
	case C_OPEN: cmd_open (); break;
	case C_NEW: cmd_new (); break;
	case C_EXTRACT: cmd_extract (false); break;
	case C_EXTRACT_ALL: cmd_extract (true); break;
	case C_ADD_FILE: cmd_add (false); break;
	case C_ADD_DIR: cmd_add (true); break;
	case C_DELETE: cmd_delete (); break;
	case C_RENAME: cmd_rename (); break;
	case C_NEWDIR: cmd_newdir (); break;
	case C_TEST: cmd_test (); break;
	case C_INFO: cmd_info (); break;
	case C_CLOSE: arc_close (); break;
	case C_QUIT: g_quit = true; break;
	case C_OPENITEM: activate_row (); break;
	case C_UP: go_up (); break;
	case C_BACK: go_back (); break;
	}
}
static void control (long w, long code)
{
	if (code == UN_ACTIVATE)
	{
		static const int BC[8] = { C_OPEN, C_NEW, C_EXTRACT, C_EXTRACT_ALL, 0, C_DELETE, C_TEST, C_INFO };
		for (int i = 0; i < 8; i++)
			if (w == g_btn[i]) { if (i == 4) cmd_add_menu (); else command (BC[i]); return; }
		if (w == g_back) go_back ();
		else if (w == g_up) go_up ();
		else if (w == g_wopen) cmd_open ();
		else if (w == g_wnew) cmd_new ();
		else if (w == g_list) activate_row ();
		else if (w == g_wlist)
		{
			long v = ui_w_get_value (g_wlist);
			if (v >= 0 && v < g_nrecent) { static char p[PATH_MAX_]; scpy (p, g_recent[v], sizeof p); arc_open (p); }
		}
		return;
	}
	if (w == g_list && code == UN_CHANGE) update_state ();
	else if (w == g_list && code == UN_CONTEXT) context_menu ();
	else if (w == g_list && code == UN_SORT)
	{
		long desc = 0, col = uikit_list_sorted (g_list, &desc);
		g_sort = (int) col; g_desc = (int) desc;
		list_fill ();
	}
	else if (w == g_tree && code == UN_CHANGE)
	{
		long id = ui_w_get_value (g_tree);
		if (id >= 0)
		{
			long data = 0;
			uikit_call3 ((long (*) ()) ui_tree_get, g_tree, id, 0, 0, &data, 0);
			if ((int) data != g_folder || g_search[0])
			{
				g_search[0] = 0; set_text (g_find, "");
				show_folder ((int) data, true);
			}
		}
	}
	else if (w == g_find && code == UN_CHANGE)
	{
		scpy (g_search, text_of (g_find), sizeof g_search);
		list_fill (); path_update (); update_state ();
	}
}

// ---- la fenetre ------------------------------------------------------------------------------------------------
static void welcome_paint (long, long w, long)
{
	ui_fill (0, 0, w, 140, jet (J_FACE));
	ui_text ((long) TR ("Archiveur"), 0, 34, jet (J_TEXTE1), 22, (long) ui.font_b, 0);
	ui_text ((long) TR ("Ouvrez une archive ZIP, tar, tar.gz ou gz pour la parcourir et en extraire les fichiers,"), 0, 62, jet (J_TEXTE2), 13, (long) ui.font, 0);
	ui_text ((long) TR ("créez-en une, ou déposez une archive sur cette fenêtre."), 0, 82, jet (J_TEXTE2), 13, (long) ui.font, 0);
	ui_text ((long) TR ("Archives récentes"), 0, 132, jet (J_TEXTE2), 12, (long) ui.font_b, 0);
}
static void lay_out (long w, long h)
{
	g_ww = w; g_wh = h;
	long x = 8;
	long lim = w - FRAME_CAP_W - 8;
	for (int i = 0; i < 8; i++)
	{
		long bw = ui_text_width ((long) BTN_TEXT[i], 13, (long) ui.font) + 24;
		ui_w_show (g_btn[i], x + bw <= lim);
		ui_w_move (g_btn[i], x, 6, bw, 30);
		x += bw + 2;
	}
	long y = ROW1 + 3;
	ui_w_move (g_back, 8, y, 30, CTRL_H);
	ui_w_move (g_up, 40, y, 30, CTRL_H);
	long fw = w < 640 ? 130 : 210;
	ui_w_move (g_find, w - fw - 8, y, fw, CTRL_H);
	ui_w_move (g_pathl, 78, y, w - fw - 8 - 78 - 8, CTRL_H);
	long top = ROW1 + ROW2, bh = h - top - FOOT;
	long tw = w >= 640 ? TREE_W : 0;
	if (g_arc) ui_w_show (g_tree, tw > 0);
	ui_w_move (g_tree, 8, top, tw ? tw : 8, bh);
	ui_w_move (g_list, tw ? tw + 14 : 8, top, tw ? w - tw - 22 : w - 16, bh);
	ui_w_move (g_status, 12, h - FOOT + 4, w / 2 - 12, 22);
	ui_w_move (g_right, w / 2, h - FOOT + 4, w / 2 - 12, 22);
	ui_w_move (g_bar, w / 2, h - FOOT + 8, w / 2 - 104, 14);
	ui_w_move (g_bcancel, w - 96, h - FOOT + 3, 88, 24);
	ui_w_move (g_welcome, 24, ROW1 + 10, w - 48, 140);
	ui_w_move (g_wopen, 24, ROW1 + 112 - 2, 0, 0);
	ui_w_move (g_wnew, 0, 0, 0, 0);
	long wy = ROW1 + 160;
	ui_w_move (g_wlist, 24, wy, w - 48, h - wy - 16);
	ui_w_move (g_wopen, w - 24 - 190, ROW1 + 16, 190, 30);
	ui_w_move (g_wnew, w - 24 - 190, ROW1 + 52, 190, 30);
	ui_view_dirty (g_welcome);
}
static void on_resize (long w, long h) { ui_fill (0, 0, w, h, jet (J_FACE)); lay_out (w, h); ui_w_draw (0); }

int main (int argc, char **argv)
{
	if (!uikit_open (&ui) || !archivekit_kit_open ()) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/archiver.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	for (int i = 0; i < 8; i++) BTN_TEXT[i] = TR (BTN_TEXT[i]);
	s_jet = (const unsigned *) ui_jetons ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = W_WANT < scr[0] - 16 ? W_WANT : (int) scr[0] - 16, wh = H_WANT < scr[1] - scr[2] - scr[3] - 16 ? H_WANT : (int) (scr[1] - scr[2] - scr[3] - 16);
	if (ui_window_ex (-1, -1, ww, wh, (long) TR ("Archiveur"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (480, 340);
	ui_menu ((long)
		"MFichier\n" "I1\tOuvrir...\tCtrl+O\n" "I2\tNouvelle archive...\tCtrl+N\n" "I12\tFermer l'archive\n" "-\n" "I13\tQuitter\tCtrl+Q\n"
		"MExtraire\n" "I3\tExtraire le choix...\tCtrl+E\n" "I4\tTout extraire...\n"
		"MArchive\n" "I5\tAjouter des fichiers...\n" "I6\tAjouter un dossier...\n" "I9\tNouveau dossier...\n" "I8\tRenommer...\n" "I7\tSupprimer\n" "-\n" "I10\tVérifier\tCtrl+T\n" "I11\tInformations\tCtrl+I\n");
	for (int i = 0; i < 8; i++) g_btn[i] = ui_pushbutton (0, 0, 80, 30, (long) BTN_TEXT[i], UB_GHOST);
	g_back = ui_pushbutton (0, 0, 30, CTRL_H, (long) "<", 0);
	g_up = ui_pushbutton (0, 0, 30, CTRL_H, (long) "^", 0);
	ui_w_tip (g_back, (long) "Dossier d'avant");
	ui_w_tip (g_up, (long) "Dossier parent");
	g_pathl = ui_label (0, 0, 100, CTRL_H, (long) "", ULB_BOLD);
	g_find = ui_textbox (0, 0, 200, CTRL_H, 0);
	ui_textbox_hint (g_find, (long) TR ("Chercher dans l'archive"));
	g_tree = ui_tree (0, 0, 100, 100);
	g_list = ui_list (0, 0, 100, 100, UL_HEADER | UL_USERSORT | UL_ICONS | UL_HSCROLL);
	ui_list_column (g_list, (long) TR ("Nom"), 190, 0);
	ui_list_column (g_list, (long) TR ("Taille"), 76, ULC_RIGHT);
	ui_list_column (g_list, (long) TR ("Compressé"), 98, ULC_RIGHT);
	ui_list_column (g_list, (long) TR ("Gain"), 64, ULC_RIGHT);
	ui_list_column (g_list, (long) TR ("Date"), 126, 0);
	ui_list_column (g_list, (long) TR ("Méthode"), 86, 0);
	g_status = ui_label (0, 0, 100, 22, (long) "", ULB_DIM);
	g_right = ui_label (0, 0, 100, 22, (long) "", ULB_DIM | ULB_RIGHT);
	g_bar = ui_progress (0, 0, 100, 14, 1, 0);
	g_bcancel = ui_pushbutton (0, 0, 88, 24, (long) "Annuler", 0);
	ui_w_show (g_bar, false); ui_w_show (g_bcancel, false);
	g_welcome = ui_view (0, 0, 100, 100, 0);
	ui_view_on (g_welcome, (long) welcome_paint, 0, 0, 0);
	g_wopen = ui_pushbutton (0, 0, 190, 30, (long) "Ouvrir une archive...", UB_DEFAULT);
	g_wnew = ui_pushbutton (0, 0, 190, 30, (long) "Nouvelle archive...", 0);
	g_wlist = ui_list (0, 0, 100, 100, UL_ICONS);
	ui_list_column (g_wlist, 0, 700, 0);
	ui_on_resize ((long) on_resize, 0);
	make_node (-1, "", 0, true);
	recent_load ();
	lay_out (ui.w, ui.h);
	arc_close ();
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_w_draw (0);
	ui_update_all ();
	if (argc > 1 && argv[1][0]) arc_open (argv[1]);
	while (!g_quit)
	{
		long t = ui_event (500);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP) on_drop ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
				else
				{
					int k = ui_ev_field (&ui, EV_CODE);
					if (k == 0x28) activate_row ();			// Entree
					else if (k == 0x2A) go_up ();			// Retour arriere
					else if (k == 0x4C) cmd_delete ();		// Suppr
				}
			}
		}
		else ui_w_idle ();
		ui_view_flush ();
	}
	if (g_arc) ar_close (g_arc);
	return 0;
}
