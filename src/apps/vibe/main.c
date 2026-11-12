/*
 * vibe/main.c -- Vibe, l'agent de code de Mistral dans une fenêtre de Korin (docs/PYTHON-PORT.md « Étape 9 »).
 *
 * La façade graphique du moteur de Mistral Vibe (vibe-acp, en Python sur la carte) : elle lui parle ACP par libacp
 * (src/lib/acp), comme le client terminal agent (tools/agent). En C sur l'UIKit de l'OS seulement (ui_*), pour être
 * reprise telle quelle par la version x86-64 de Korin.
 *
 *   vibe [--cwd DOSSIER] [--continue] [--agent COMMANDE]
 *
 * La forme est celle d'un agent en ligne de commande (l'interface de Vibe elle-même, ou Claude Code dans un
 * terminal) : un terminal sobre, police à chasse fixe sur le fond sombre du thème (comme le Terminal de Korin,
 en dessous d'une barre d'outils claire), trois couleurs seulement : la demande (« > », l'accent du thème), la réponse de l'agent (claire), les
 * lignes du système (outils, notes, temps : gris discret, « ● » et « └ ») ; les diffs en rouge et vert. En tête de la
 * conversation un cadre d'accueil (version, modèle, dossier) ; en bas l'invite et une barre d'état (dossier, mode
 * d'approbation, jetons du contexte). Trois volets, comme une app d'agent de code : à gauche (repliable) les séances
 * du dossier, pour passer de l'une à l'autre ; au milieu la conversation ; à droite (repliable) des onglets :
 * Fichiers (l'arborescence du dossier de travail, « ● » pour ce que Vibe a modifié, le contenu du fichier choisi ;
 * double-clic : ouvert dans son app), Infos (agent, modèle, mode, séance, jetons, fichiers modifiés), Commandes
 * (une commande bash dans le dossier de travail, sa sortie). Les volets se retirent d'eux-mêmes quand la fenêtre est
 * étroite (mode poche).
 *
 * Une approbation demandée par l'agent ouvre un dialogue modal (WF_MODAL) : Autoriser une fois, Pour la séance,
 * Refuser (Échap). Le mode « ask » est proposé d'office.
 *
 * Touches : Entrée envoie, Échap interrompt un tour (session/cancel), Maj+Tab change de mode, Page préc. / suiv.
 * font défiler, Ctrl+N : une nouvelle séance ; /help, /new, /continue, /mode ; toute autre commande « / » va à
 * l'agent (ses compétences). Au tactile (mode poche) : contrôles plus hauts, la conversation défile au doigt.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <uikit.h>
#include "acp.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))

enum { M_NEW = 1, M_LAST, M_FOLDER, M_LEFT, M_RIGHT, M_QUIT, M_ASK = 11, M_EDITS, M_AUTO };
enum { I_WELCOME, I_USER, I_AGENT, I_THOUGHT, I_TOOL, I_DIFF, I_NOTE, I_ERROR, I_SIGNIN };

typedef struct item {
	int kind;
	jbuf text;		/* le texte ; I_TOOL : le titre ; I_DIFF : « @ chemin » puis des lignes « - », « + », «   » */
	char *id;		/* I_TOOL : toolCallId */
	char *status;		/* I_TOOL */
	char *extra;		/* I_TOOL : les premières lignes de la sortie d'une commande */
	int h, hw;		/* hauteur mesurée pour la largeur hw (hw = 0 : à mesurer) */
} item;

static struct ui_vars ui;
static const unsigned char *s_jet;
static unsigned jet (int off) { return *(const unsigned *) (s_jet + off) & 0xFFFFFF; }
static unsigned mix (unsigned a, unsigned b, int n) { return (unsigned) ui_mix (a, b, n); }

static acp_t *A;
static item *items;
static int nitems, capitems;
static int g_ww, g_wh, CTL, PX, PAD, FOOT_H, FOOT_BASE, BAR_H, SBW, TOUCH, LEFT_W, RIGHT_W;
static int g_left_want = 1, g_right_want = 1, g_left, g_right;	/* volets voulus, volets montrés */
static long g_vchat, g_sbar, g_vfoot;
static long g_bleft, g_bmode, g_bnew, g_bright;	/* la barre d'outils, dans la bande du titre */
static long g_lsess, g_sess;			/* à gauche : les séances */
static long g_tabs, g_pg[3], g_tree, g_flabel, g_fview, g_info, g_cmdline, g_cmdrun, g_cmdout;	/* à droite */
static char *g_shown;				/* le fichier montré */
static int TOOL_H;
static int g_scroll, g_total, g_stick = 1;
static double g_t0, g_turn_t0;
static char g_cwd[512];
static char g_agent[80];		/* « @mistralai/mistral-vibe 3.1.2 » */
static long g_used, g_size;		/* jetons du contexte */
static char *g_url;			/* le lien de connexion en cours */
static int g_btn_copy[4], g_btn_open[4];
static int g_drag_y = -1, g_drag_scroll, g_dragged;
static char *g_paths[2048];		/* chemins de l'arborescence (donnée des nœuds) */
static int g_npaths;
static char *g_modified[256];		/* fichiers modifiés pendant la séance */
static int g_nmod;

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (double) t.tv_sec + t.tv_nsec / 1e9;
}

static void repaint (void);
static void relayout (void);
static void fill_tree (void);
static void show_info (void);
static void refresh_sessions (void);

/* --- les éléments de la conversation ------------------------------------------------------------------------------- */

static item *add (int kind, const char *text)
{
	if (nitems == capitems)
	{
		capitems = capitems ? capitems * 2 : 64;
		items = realloc (items, (size_t) capitems * sizeof *items);
	}
	item *it = &items[nitems++];
	memset (it, 0, sizeof *it);
	it->kind = kind;
	jb_init (&it->text);
	if (text) jb_puts (&it->text, text);
	g_stick = 1;
	return it;
}

static void clear_items (void)
{
	for (int i = 0; i < nitems; i++)
	{
		jb_free (&items[i].text);
		free (items[i].id);
		free (items[i].status);
		free (items[i].extra);
	}
	nitems = 0;
	add (I_WELCOME, NULL);
}

static item *find_tool (const char *id)
{
	for (int i = nitems - 1; i >= 0; i--)
		if (items[i].kind == I_TOOL && items[i].id && !strcmp (items[i].id, id)) return &items[i];
	return NULL;
}

static void setstr (char **dst, const char *s) { char *n = s ? strdup (s) : NULL; free (*dst); *dst = n; }
static void changed (item *it) { it->hw = 0; }

/* --- mesure et dessin ------------------------------------------------------------------------------------------------- */

static long f_mono (void) { return ui_font_get (UFN_MONO); }
static long f_mono_b (void) { return ui_font_get (UFN_MONOBOLD); }
static long f_ui (void) { return (long) ui.font; }
static long f_ui_b (void) { return (long) ui.font_b; }

static unsigned c_bg (void) { return jet (TJ_SOMBRE); }
static unsigned c_agent (void) { return mix (c_bg (), 0xFFFFFF, 225); }
static unsigned c_sys (void) { return mix (c_bg (), 0xFFFFFF, 120); }
static unsigned c_user (void) { return mix (jet (TJ_ACCENT), 0xFFFFFF, 90); }
static unsigned c_red (void) { return mix (jet (TJ_DANGER), 0xFFFFFF, 60); }
static unsigned c_green (void) { return mix (jet (TJ_SUCCES), 0xFFFFFF, 60); }

/* le texte du modèle sans les marques de Markdown qui se verraient (gras, code en ligne, # des titres, clôtures ```) */
static char *plain (const char *p, size_t n)
{
	char *o = malloc (n + 1);
	size_t k = 0;
	for (size_t i = 0; i < n; i++)
	{
		if (p[i] == '`' && i + 2 < n && p[i + 1] == '`' && p[i + 2] == '`')
		{
			while (i + 1 < n && p[i + 1] != '\n') i++;	/* la clôture et le reste de sa ligne (```python) */
			if (i + 1 < n) i++;
			continue;
		}
		if (p[i] == '*' && i + 1 < n && p[i + 1] == '*') { i++; continue; }
		if (p[i] == '`') continue;
		if (p[i] == '#' && (i == 0 || p[i - 1] == '\n'))
		{
			size_t j = i;
			while (j < n && p[j] == '#') j++;
			if (j < n && p[j] == ' ') { i = j; continue; }
		}
		o[k++] = p[i];
	}
	while (k && (o[k - 1] == '\n' || o[k - 1] == ' ')) k--;
	while (k && o[0] == '\n') { memmove (o, o + 1, k - 1); k--; }
	o[k] = 0;
	return o;
}

static int wrap (const char *t, int x, int y, int w, unsigned col, long font, int measure)
{
	if (!*t) return 0;
	return (int) ui_text_wrap ((long) t, x, y, w, col, font, PX, measure);
}

static int LH (void) { return PX + 5; }
static int IND (void) { return (int) ui_text_width ((long) "> ", PX, f_mono ()); }
static int GAP (void) { return TOUCH ? 12 : 8; }

/* un élément : dessiné à y, ou mesuré (measure = 1) -> hauteur */
static int draw_item (item *it, int width, int y, int measure)
{
	const char *t = jb_cstr (&it->text);
	int x = PAD, w = width - 2 * PAD, ind = IND ();
	switch (it->kind)
	{
	case I_WELCOME:
	{
		/* le cadre d'accueil : l'agent et sa version, le modèle, le dossier, l'aide */
		char l1[160], l2[160];
		snprintf (l1, sizeof l1, "%s", *g_agent ? g_agent : TR ("Mistral Vibe (démarrage…)"));
		snprintf (l2, sizeof l2, "%s", *acp_model (A) ? acp_model (A) : TR ("modèle : à l'ouverture de la séance"));
		const char *l4 = TR ("/help pour l'aide · Maj+Tab : mode · Échap : interrompre");
		int h4 = wrap (l4, 0, 0, w - 28, 0, f_mono (), 1), h3 = wrap (g_cwd, 0, 0, w - 28, 0, f_mono (), 1);
		int h = 12 + 2 * LH () + 6 + h3 + 6 + h4 + 12;
		if (measure) return h;
		ui_frame (x, y, w, h, mix (c_bg (), jet (TJ_ACCENT), 160));
		int yy = y + 12 + PX;
		ui_text ((long) l1, x + 14, yy, c_agent (), PX, f_mono_b (), w - 28);
		yy += LH ();
		ui_text ((long) l2, x + 14, yy, c_sys (), PX, f_mono (), w - 28);
		yy += LH () - PX + 6;
		wrap (g_cwd, x + 14, yy, w - 28, c_sys (), f_mono (), 0);
		yy += h3 + 6;
		wrap (l4, x + 14, yy, w - 28, c_sys (), f_mono (), 0);
		return h;
	}
	case I_USER:
	{
		int h = wrap (t, 0, 0, w - ind, 0, f_mono (), 1) + 8;
		if (!measure)
		{
			ui_fill (0, y, width, h, mix (c_bg (), jet (TJ_ACCENT), 36));
			ui_text ((long) ">", x, y + 4 + PX, c_user (), PX, f_mono_b (), 0);
			wrap (t, x + ind, y + 4, w - ind, c_user (), f_mono (), 0);
		}
		return h;
	}
	case I_AGENT:
	{
		char *p = plain (t, strlen (t));
		int h = wrap (p, x + ind, y, w - ind, c_agent (), f_mono (), measure);
		if (!measure && h) ui_text ((long) "●", x, y + PX, c_agent (), PX, f_mono (), 0);
		free (p);
		return h;
	}
	case I_THOUGHT:
		return wrap (t, x + ind, y, w - ind, c_sys (), ui_font_get (UFN_ITALIC), measure);
	case I_TOOL:
	{
		/* ● Titre (état), puis pour une commande « └ » et ses premières lignes */
		int eh = it->extra && *it->extra ? wrap (it->extra, 0, 0, w - 2 * ind, 0, f_mono (), 1) : 0;
		if (measure) return LH () + eh;
		int failed = it->status && !strcmp (it->status, "failed");
		int done = it->status && !strcmp (it->status, "completed");
		unsigned dot = failed ? jet (TJ_DANGER) : done ? jet (TJ_SUCCES) : jet (TJ_ALERTE);
		ui_text ((long) "●", x, y + PX, dot, PX, f_mono (), 0);
		const char *st = failed ? TR ("échec") : done ? "" : "…";
		int sw = *st ? (int) ui_text_width ((long) st, PX, f_mono ()) + 8 : 0;
		ui_text_fit ((long) t, x + ind, y + PX, c_sys (), PX, f_mono_b (), w - ind - sw);
		if (*st) ui_text ((long) st, x + w - sw + 8, y + PX, failed ? c_red () : c_sys (), PX, f_mono (), 0);
		if (eh)
		{
			ui_text ((long) "└", x + ind, y + LH () + PX, c_sys (), PX, f_mono (), 0);
			wrap (it->extra, x + 2 * ind, y + LH (), w - 2 * ind, c_sys (), f_mono (), 0);
		}
		return LH () + eh;
	}
	case I_DIFF:
	{
		int n = 1;
		for (const char *p = t; *p; p++) if (*p == '\n') n++;
		int h = n * LH ();
		if (measure) return h;
		const char *p = t;
		int yy = y + PX;
		char line[512];
		while (*p)
		{
			size_t k = strcspn (p, "\n"), c = k < sizeof line - 1 ? k : sizeof line - 1;
			memcpy (line, p, c);
			line[c] = 0;
			if (line[0] == '@')
			{
				ui_text ((long) "└", x + ind, yy, c_sys (), PX, f_mono (), 0);
				ui_text_fit ((long) (line + 2), x + 2 * ind, yy, c_sys (), PX, f_mono (), w - 2 * ind);
			}
			else
			{
				unsigned col = line[0] == '-' ? c_red () : line[0] == '+' ? c_green () : c_sys ();
				ui_text_fit ((long) line, x + 2 * ind, yy, col, PX, f_mono (), w - 2 * ind);
			}
			yy += LH ();
			p += k;
			if (*p) p++;
		}
		return h;
	}
	case I_NOTE:
		return wrap (t, x + ind, y, w - ind, c_sys (), f_mono (), measure);
	case I_ERROR:
		return wrap (t, x + ind, y, w - ind, c_red (), f_mono (), measure);
	case I_SIGNIN:
	{
		const char *head = TR ("Connexion au compte Mistral : ouvrez ce lien dans un navigateur, ici ou sur un autre appareil (téléphone, PC).");
		int h1 = wrap (head, 0, 0, w - ind, 0, f_mono (), 1), h2 = wrap (t, 0, 0, w - ind, 0, f_mono (), 1);
		int bh = CTL;
		int h = h1 + 6 + h2 + 10 + bh + 4;
		if (measure) return h;
		ui_text ((long) "●", x, y + PX, jet (TJ_ALERTE), PX, f_mono (), 0);
		wrap (head, x + ind, y, w - ind, c_agent (), f_mono (), 0);
		wrap (t, x + ind, y + h1 + 6, w - ind, c_user (), f_mono (), 0);
		int yy = y + h1 + 6 + h2 + 10, bx = x + ind;
		const char *l1 = TR ("Copier le lien"), *l2 = TR ("Ouvrir dans Jet");
		int w1 = (int) ui_text_width ((long) l1, PX, f_ui ()) + 28, w2 = (int) ui_text_width ((long) l2, PX, f_ui ()) + 28;
		g_btn_copy[0] = bx; g_btn_copy[1] = yy; g_btn_copy[2] = w1; g_btn_copy[3] = bh;
		ui_rbox (bx, yy, w1, bh, 6, jet (TJ_ACCENT), jet (TJ_ACCENT));
		ui_text ((long) l1, bx + 14, yy + bh / 2 + PX / 2 - 1, 0xFFFFFF, PX, f_ui (), 0);
		bx += w1 + 10;
		if (access ("/apps/jet.app/main", X_OK) == 0)
		{
			g_btn_open[0] = bx; g_btn_open[1] = yy; g_btn_open[2] = w2; g_btn_open[3] = bh;
			ui_frame (bx, yy, w2, bh, c_sys ());
			ui_text ((long) l2, bx + 14, yy + bh / 2 + PX / 2 - 1, c_agent (), PX, f_ui (), 0);
		}
		else g_btn_open[2] = 0;
		return h;
	}
	}
	return 0;
}

static int left (void) { return g_left ? LEFT_W : 0; }
static int right (void) { return g_right ? RIGHT_W : 0; }
static int chat_w (void) { return g_ww - left () - right (); }
static int view_h (void) { return g_wh - TOOL_H - FOOT_H - BAR_H; }

static void clamp_scroll (void)
{
	int most = g_total - view_h ();
	if (most < 0) most = 0;
	if (g_stick || g_scroll > most) g_scroll = most;
	if (g_scroll < 0) g_scroll = 0;
	g_stick = g_scroll >= most;
	ui_w_set_range (g_sbar, 0, g_total > view_h () ? g_total : view_h (), view_h (), 40);
	ui_w_set_value (g_sbar, g_scroll);
}

static void relayout (void)
{
	int width = chat_w (), y = PAD;
	for (int i = 0; i < nitems; i++)
	{
		item *it = &items[i];
		if (it->hw != width) { it->h = draw_item (it, width, 0, 1); it->hw = width; }
		y += it->h + GAP ();
	}
	g_total = y + PAD;
	clamp_scroll ();
	ui_view_dirty (g_vchat);
}

static long chat_paint (long v, long wl, long hl)
{
	(void) v;
	int width = (int) wl, height = (int) hl;
	ui_fill (0, 0, width, height, c_bg ());
	int y = PAD - g_scroll;
	g_btn_copy[2] = g_btn_open[2] = 0;
	for (int i = 0; i < nitems; i++)
	{
		item *it = &items[i];
		if (it->hw != width) { it->h = draw_item (it, width, 0, 1); it->hw = width; }
		if (y + it->h >= 0 && y < height) draw_item (it, width, y, 0);
		y += it->h + GAP ();
	}
	/* l'indicateur de défilement, quand la conversation dépasse */
	if (g_total > height && height > 20)
	{
		int th = height * height / g_total;
		if (th < 24) th = 24;
		int ty = (int) ((long) (height - th) * g_scroll / (g_total - height > 0 ? g_total - height : 1));
		ui_rbox (width - 6, ty + 2, 4, th - 4, 2, mix (c_bg (), 0xFFFFFF, 80), mix (c_bg (), 0xFFFFFF, 80));
	}
	return 0;
}

static void open_url_in_jet (void)
{
	if (!g_url) return;
	pid_t p = fork ();
	if (p == 0)
	{
		execl ("/apps/jet.app/main", "jet", g_url, (char *) NULL);
		_exit (127);
	}
}

static int in_rect (const int *r, int x, int y) { return r[2] && x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]; }

static long chat_mouse (long v, long kind, long x, long y, long info)
{
	(void) v;
	if (kind == 1)		/* appui : peut-être un glisser (tactile) */
	{
		g_drag_y = (int) y;
		g_drag_scroll = g_scroll;
		g_dragged = 0;
	}
	else if (kind == 0 && g_drag_y >= 0 && ((info >> 8) & 1))
	{
		int dy = (int) y - g_drag_y;
		if (dy > 6 || dy < -6) g_dragged = 1;
		if (g_dragged)
		{
			g_stick = 0;
			g_scroll = g_drag_scroll - dy;
			clamp_scroll ();
			ui_view_dirty (g_vchat);
		}
	}
	else if (kind == 2)
	{
		if (!g_dragged && g_url)
		{
			if (in_rect (g_btn_copy, (int) x, (int) y))
			{
				ui_clip_set ((long) g_url, -1);
				add (I_NOTE, TR ("Lien copié dans le presse-papiers."));
				relayout ();
			}
			else if (in_rect (g_btn_open, (int) x, (int) y)) open_url_in_jet ();
		}
		g_drag_y = -1;
	}
	else if (kind == 3) g_drag_y = -1;
	return 0;
}

static void scroll_by (int dy)
{
	g_stick = 0;
	g_scroll += dy;
	clamp_scroll ();
	ui_view_dirty (g_vchat);
}

static long chat_wheel (long v, long n) { (void) v; scroll_by ((int) -n * 48); return 0; }

/* --- les volets : à gauche les séances, à droite les fichiers, les infos et les commandes ---------------------------- */

static int skip_name (const char *n)
{
	return n[0] == '.' && (!n[1] || !strcmp (n, "..") || !strcmp (n, ".git") || !strcmp (n, ".vibe"));
}

static int cmp_names (const void *a, const void *b) { return strcmp (*(char *const *) a, *(char *const *) b); }

static int is_modified (const char *path)
{
	for (int i = 0; i < g_nmod; i++) if (!strcmp (g_modified[i], path)) return 1;
	return 0;
}

/* un dossier dans l'arbre, deux niveaux de profondeur au plus (les dossiers d'abord) ; « ● » : modifié par Vibe */
static void tree_dir (long parent, const char *dir, int depth)
{
	DIR *d = opendir (dir);
	if (!d) return;
	char *names[512];
	int n = 0;
	struct dirent *e;
	while ((e = readdir (d)) && n < 512) if (!skip_name (e->d_name)) names[n++] = strdup (e->d_name);
	closedir (d);
	qsort (names, (size_t) n, sizeof *names, cmp_names);
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < n; i++)
		{
			char path[1024];
			snprintf (path, sizeof path, "%s/%s", dir, names[i]);
			struct stat st;
			int isdir = stat (path, &st) == 0 && S_ISDIR (st.st_mode);
			if (isdir != (pass == 0) || g_npaths >= 2048) continue;
			char label[300];
			snprintf (label, sizeof label, isdir ? "%s/" : is_modified (path) ? "%s  ●" : "%s", names[i]);
			g_paths[g_npaths] = strdup (path);
			long id = ui_tree_add (g_tree, parent, (long) label, g_npaths++);
			if (isdir && depth < 2) tree_dir (id, path, depth + 1);
		}
	for (int i = 0; i < n; i++) free (names[i]);
}

static void fill_tree (void)
{
	ui_tree_clear (g_tree);
	for (int i = 0; i < g_npaths; i++) free (g_paths[i]);
	g_npaths = 0;
	tree_dir (-1, g_cwd, 0);
	if (g_right) ui_w_draw (g_tree);
}

static const char *rel (const char *path)
{
	size_t n = strlen (g_cwd);
	return !strncmp (path, g_cwd, n) && path[n] == '/' ? path + n + 1 : path;
}

/* le contenu d'un fichier, sous l'arbre (64 Kio au plus ; un fichier binaire n'est pas montré) */
static void show_file (const char *p)
{
	struct stat st;
	if (!p || stat (p, &st) != 0 || S_ISDIR (st.st_mode)) return;
	if (p != g_shown) setstr (&g_shown, p);		/* (p peut être g_shown lui-même : ne pas le libérer) */
	p = g_shown;
	char *buf = malloc (65536 + 64);
	size_t n = 0;
	FILE *f = fopen (p, "rb");
	if (f) { n = fread (buf, 1, 65536, f); fclose (f); }
	buf[n] = 0;
	if (memchr (buf, 0, n)) snprintf (buf, 64, TR ("(fichier binaire, %ld octets)"), (long) st.st_size);
	else if (st.st_size > 65536) strcat (buf, "\n…");
	ui_w_set_text (g_fview, (long) buf);
	free (buf);
	char t[300];
	snprintf (t, sizeof t, "%s%s", rel (p), is_modified (p) ? TR ("  · modifié par Vibe") : "");
	ui_w_set_text (g_flabel, (long) t);
	if (g_right) { ui_w_draw (g_flabel); ui_w_draw (g_fview); }
}

static void note_modified (const char *path)
{
	if (is_modified (path) || g_nmod >= 256) return;
	g_modified[g_nmod++] = strdup (path);
}

/* les infos de la séance */
static void show_info (void)
{
	jbuf b;
	jb_init (&b);
	jb_printf (&b, "%s\n  %s\n\n", TR ("Agent"), *g_agent ? g_agent : "…");
	jb_printf (&b, "%s\n  %s\n\n", TR ("Modèle"), *acp_model (A) ? acp_model (A) : "…");
	jb_printf (&b, "%s\n  %s\n\n", TR ("Mode"), *acp_mode (A) ? acp_mode (A) : "…");
	jb_printf (&b, "%s\n  %s\n\n", TR ("Séance"), acp_session_id (A) ? acp_session_id (A) : "…");
	jb_printf (&b, "%s\n  %s\n\n", TR ("Dossier de travail"), g_cwd);
	if (g_size > 0) jb_printf (&b, "%s\n  %ld / %ld (%ld %%)\n\n", TR ("Contexte (jetons)"), g_used, g_size, g_used * 100 / g_size);
	jb_printf (&b, "%s\n", TR ("Fichiers modifiés pendant la séance"));
	if (!g_nmod) jb_puts (&b, TR ("  aucun"));
	for (int i = 0; i < g_nmod; i++) jb_printf (&b, "  %s\n", rel (g_modified[i]));
	ui_w_set_text (g_info, (long) jb_cstr (&b));
	jb_free (&b);
	if (g_right && ui_w_get_value (g_tabs) == 1) ui_w_draw (g_info);
}

/* les séances du dossier (session/list), à gauche : la plus récente en tête, la séance ouverte marquée */
static char *g_sess_id[200];
static int g_nsess;

static int cmp_updated (const void *a, const void *b)
{
	return strcmp (json_gets (*(const json_t *const *) b, "updatedAt"), json_gets (*(const json_t *const *) a, "updatedAt"));
}

static void on_sessions (acp_t *a, void *data, const json_t *res, const json_t *err)
{
	(void) a; (void) data;
	if (err) return;
	const json_t *s = json_get (res, "sessions");
	int n = json_len (s);
	const json_t **v = malloc ((size_t) (n ? n : 1) * sizeof *v);
	for (int i = 0; i < n; i++) v[i] = json_at (s, i);
	qsort (v, (size_t) n, sizeof *v, cmp_updated);
	ui_list_clear (g_sess);
	for (int i = 0; i < g_nsess; i++) free (g_sess_id[i]);
	g_nsess = 0;
	const char *cur = acp_session_id (A);
	for (int i = 0; i < n && g_nsess < 200; i++)
	{
		const char *id = json_gets (v[i], "sessionId"), *title = json_gets (v[i], "title");
		const char *at = json_gets (v[i], "updatedAt");
		char when[32] = "";
		if (strlen (at) >= 16) snprintf (when, sizeof when, "%.2s/%.2s %.5s", at + 8, at + 5, at + 11);
		char t[300];
		snprintf (t, sizeof t, "%s%s\t%s", cur && !strcmp (cur, id) ? "● " : "", *title ? title : TR ("(sans titre)"), when);
		g_sess_id[g_nsess++] = strdup (id);
		ui_list_add (g_sess, (long) t, g_nsess - 1);
	}
	free (v);
	if (g_left) ui_w_draw (g_sess);
}

static void refresh_sessions (void)
{
	int st = acp_state (A);
	if (st != ACP_IDLE && st != ACP_TURN) return;
	jbuf p;
	jb_init (&p);
	jb_puts (&p, "{\"cwd\":");
	jb_str (&p, g_cwd);
	jb_putc (&p, '}');
	acp_rpc_request (A, "session/list", jb_cstr (&p), on_sessions, NULL);
	jb_free (&p);
}

/* l'onglet Commandes : bash -c dans le dossier de travail, la sortie à mesure (32 Kio gardés) */
static int g_cmd_fd = -1;
static pid_t g_cmd_pid;
static jbuf g_cmd_out;

static void cmd_show (void)
{
	if (g_cmd_out.n > 32768)
	{
		size_t cut = g_cmd_out.n - 32768;
		memmove (g_cmd_out.p, g_cmd_out.p + cut, g_cmd_out.n - cut + 1);
		g_cmd_out.n -= cut;
	}
	ui_w_set_text (g_cmdout, (long) jb_cstr (&g_cmd_out));
	if (g_right && ui_w_get_value (g_tabs) == 2) ui_w_draw (g_cmdout);
}

static void run_command (void)
{
	const char *c = (const char *) ui_w_get_text (g_cmdline);
	while (*c == ' ') c++;
	if (!*c || g_cmd_fd >= 0) return;
	char *cmd = strdup (c);
	jb_printf (&g_cmd_out, "%s$ %s\n", g_cmd_out.n ? "\n" : "", cmd);
	int fd[2];
	if (pipe (fd) == 0)
	{
		pid_t p = fork ();
		if (p == 0)
		{
			dup2 (fd[1], 1);
			dup2 (fd[1], 2);
			close (fd[0]);
			close (fd[1]);
			if (chdir (g_cwd) < 0) _exit (126);
			execl ("/bin/bash", "bash", "-c", cmd, (char *) NULL);
			_exit (127);
		}
		close (fd[1]);
		if (p > 0) { g_cmd_pid = p; g_cmd_fd = fd[0]; fcntl (g_cmd_fd, F_SETFL, O_NONBLOCK); }
		else close (fd[0]);
	}
	free (cmd);
	ui_w_set_text (g_cmdline, (long) "");
	cmd_show ();
}

static void cmd_pump (void)
{
	if (g_cmd_fd < 0) return;
	char buf[4096];
	ssize_t n;
	int got = 0;
	while ((n = read (g_cmd_fd, buf, sizeof buf)) > 0) { jb_putn (&g_cmd_out, buf, (size_t) n); got = 1; }
	if (n == 0)
	{
		int st = 0;
		close (g_cmd_fd);
		g_cmd_fd = -1;
		waitpid (g_cmd_pid, &st, 0);
		jb_printf (&g_cmd_out, "[%d]\n", WIFEXITED (st) ? WEXITSTATUS (st) : -1);
		got = 1;
		fill_tree ();
	}
	if (got) cmd_show ();
}

/* --- la mise en page ------------------------------------------------------------------------------------------------- */

static void metrics (void)
{
	TOUCH = (int) ui_metric (UMT_TOUCH);
	CTL = (int) ui_metric (UMT_CTL);
	if (CTL < 26) CTL = 26;
	PX = (int) ui_metric (UMT_PX);
	if (PX < 11) PX = 13;
	SBW = (int) ui_metric (UMT_SB);
	if (SBW < 10) SBW = 12;
	PAD = TOUCH ? 16 : 12;
	FOOT_BASE = CTL + 16;
	FOOT_H = FOOT_BASE;		/* (plus haut quand la demande a plusieurs lignes : foot_fit) */
	TOOL_H = CTL + 14;
	BAR_H = PX + 12;
	LEFT_W = 220;
	RIGHT_W = 340;
}

static void show_pages (void)
{
	/* les enfants d'une page suivent leur page ; les pages suivent l'onglet choisi */
	int tab = (int) ui_w_get_value (g_tabs);
	for (int i = 0; i < 3; i++) ui_w_show (g_pg[i], g_right && tab == i);
	ui_w_show (g_tabs, g_right);
}

static void lay_out (void)
{
	int w = g_ww, h = g_wh;
	/* les volets se retirent d'eux-mêmes quand la fenêtre est étroite (mode poche : 800 px de large) */
	g_left = g_left_want && w >= 640;
	g_right = g_right_want && w - left () >= 760;
	int L = left (), R = right (), M = w - L - R;
	ui_w_show (g_lsess, g_left);
	ui_w_show (g_sess, g_left);
	if (g_left)
	{
		ui_w_move (g_lsess, 12, TOOL_H + 10, LEFT_W - 24, 20);
		ui_w_move (g_sess, 6, TOOL_H + 34, LEFT_W - 12, h - TOOL_H - 40);
	}
	if (g_right)
	{
		int X = w - R, y0 = TOOL_H + 40, ph = h - y0;
		ui_w_move (g_tabs, X, TOOL_H, R, h - TOOL_H);
		for (int i = 0; i < 3; i++) ui_w_move (g_pg[i], X, y0, R, ph);
		int th = (ph - 30) * 2 / 5;
		ui_w_move (g_tree, X + 6, y0 + 6, R - 12, th);
		ui_w_move (g_flabel, X + 8, y0 + th + 12, R - 16, 20);
		ui_w_move (g_fview, X + 6, y0 + th + 34, R - 12, ph - th - 40);
		ui_w_move (g_info, X + 6, y0 + 6, R - 12, ph - 12);
		int bw = (int) ui_text_width ((long) TR ("Lancer"), PX, f_ui ()) + 28;
		ui_w_move (g_cmdline, X + 6, y0 + 6, R - 18 - bw, CTL);
		ui_w_move (g_cmdrun, X + R - 6 - bw, y0 + 6, bw, CTL);
		ui_w_move (g_cmdout, X + 6, y0 + CTL + 12, R - 12, ph - CTL - 18);
	}
	show_pages ();
	/* la conversation prend toute la largeur du milieu, comme le pied ; la barre de défilement d'UIKit (claire) est
	   remplacée par un indicateur fin dessiné dans la vue */
	ui_w_move (g_vchat, L, TOOL_H, M, view_h ());
	ui_w_show (g_sbar, 0);
	/* la barre d'outils : Séances, le nom, le mode, Nouvelle séance ; Panneau à droite, avant les boutons du cadre */
	int by = (TOOL_H - CTL) / 2;
	int wl = (int) ui_text_width ((long) "☰", PX, f_ui ()) + 24;
	ui_w_move (g_bleft, 8, by, wl, CTL);
	int bx = 8 + wl + 12 + (int) ui_text_width ((long) "Vibe", PX + 3, f_ui ()) + 20;
	int wm = (int) ui_text_width ((long) "Mode : accept-edits ▾", PX, f_ui ()) + 28;
	int wn = (int) ui_text_width ((long) TR ("Nouvelle séance"), PX, f_ui ()) + 28;
	int wr = (int) ui_text_width ((long) TR ("Panneau"), PX, f_ui ()) + 28;
	int xr = w - FRAME_CAP_W - 12 - wr;
	ui_w_show (g_bright, w - left () >= 760 || g_right_want == 0);
	ui_w_move (g_bright, xr, by, wr, CTL);
	ui_w_show (g_bmode, xr - bx >= wm);
	ui_w_show (g_bnew, xr - bx >= wm + wn + 8);
	ui_w_move (g_bmode, bx, by, wm, CTL);
	ui_w_move (g_bnew, bx + wm + 8, by, wn, CTL);
	ui_w_move (g_vfoot, L, TOOL_H + view_h (), M, FOOT_H + BAR_H);
	ui_view_dirty (g_vfoot);
	relayout ();
}

/* --- le pied : l'invite et la barre d'état, une vue dessinée sur le fond sombre ------------------------------------- */
/* Le champ de saisie est dessiné ici plutôt qu'un ui_textbox : il reste sombre quel que soit le thème, comme le reste
   du terminal. Une ligne UTF-8, le curseur en octets, l'historique (haut, bas), le collage (Ctrl+V), le bouton
   Envoyer / Interrompre dessiné à droite, le mode et les jetons dans la barre d'état (un clic : le menu du mode). */

static char g_in[4096];
static size_t g_inlen, g_incur;
static char *g_hist[100];
static int g_nhist, g_hpos;
static int g_btn[4], g_moderect[4];
static void send_text (void);
static void interrupt (void);
static void mode_popup (void);
static void cycle_mode (void);
static void scroll_by (int dy);

static void paint_foot (void) { ui_view_dirty (g_vfoot); }
static void send_button (void) { ui_view_dirty (g_vfoot); }

static long foot_paint (long v, long wl, long hl)
{
	(void) v;
	int w = (int) wl, h = (int) hl;
	(void) h;
	ui_fill (0, 0, w, FOOT_H + BAR_H, c_bg ());
	unsigned line = mix (c_bg (), 0xFFFFFF, 50);
	ui_fill (PAD, 0, w - 2 * PAD, 1, line);
	ui_fill (PAD, FOOT_H, w - 2 * PAD, 1, line);
	int st = acp_state (A), turn = st == ACP_TURN;
	/* le bouton, à droite : un contour clair, l'accent quand il interrompt */
	const char *bl = turn ? TR ("Interrompre") : TR ("Envoyer");
	int bw = (int) ui_text_width ((long) TR ("Interrompre"), PX, f_ui ()) + 28, bh = CTL - 4;
	int bx = w - PAD - bw, by = (FOOT_BASE - bh) / 2;
	g_btn[0] = bx; g_btn[1] = by; g_btn[2] = bw; g_btn[3] = bh;
	int active = turn || (st == ACP_IDLE && g_inlen);
	unsigned bcol = turn ? jet (TJ_ALERTE) : active ? c_user () : mix (c_bg (), 0xFFFFFF, 70);
	ui_frame (bx, by, bw, bh, bcol);
	int tw = (int) ui_text_width ((long) bl, PX, f_ui ());
	ui_text ((long) bl, bx + (bw - tw) / 2, by + bh / 2 + PX / 2 - 1, active ? c_agent () : c_sys (), PX, f_ui (), 0);
	/* l'invite et le texte : une ligne par retour à la ligne (Maj+Entrée) ; celle du curseur défile pour le montrer */
	int ind = IND (), x0 = PAD + ind, avail = bx - 12 - x0, base = FOOT_BASE / 2 + PX / 2 - 1;
	ui_text ((long) ">", PAD, base, c_user (), PX, f_mono_b (), 0);
	if (!g_inlen)
	{
		ui_text_fit ((long) TR ("Demandez quelque chose à Vibe, ou /help (Maj+Entrée : à la ligne)"), x0, base, c_sys (), PX, f_mono (), avail);
		ui_fill (x0, base - PX + 1, 2, PX + 3, c_user ());
	}
	else
	{
		g_in[g_inlen] = 0;
		/* six lignes montrées au plus, celle du curseur comprise */
		int curl = 0;
		for (size_t i = 0; i < g_incur; i++) if (g_in[i] == '\n') curl++;
		int first = curl > 5 ? curl - 5 : 0, row = 0, li = 0;
		size_t p = 0;
		while (p <= g_inlen && row < 6)
		{
			size_t e = p;
			while (e < g_inlen && g_in[e] != '\n') e++;
			if (li >= first)
			{
				int y = base + row * LH ();
				char save = g_in[e];
				g_in[e] = 0;
				if (li == curl)
				{
					size_t s0 = p, c = g_incur;
					char sc = g_in[c];
					g_in[c] = 0;
					while (s0 < c && ui_text_width ((long) (g_in + s0), PX, f_mono ()) > avail - 4)
					{
						do s0++; while (s0 < c && ((unsigned char) g_in[s0] & 0xC0) == 0x80);
					}
					int cx = x0 + (int) ui_text_width ((long) (g_in + s0), PX, f_mono ());
					g_in[c] = sc;
					ui_text_fit ((long) (g_in + s0), x0, y, c_user (), PX, f_mono (), avail);
					ui_fill (cx, y - PX + 1, 2, PX + 3, c_user ());
				}
				else ui_text_fit ((long) (g_in + p), x0, y, c_user (), PX, f_mono (), avail);
				g_in[e] = save;
				row++;
			}
			li++;
			p = e + 1;
		}
	}
	/* la barre d'état : l'état ou le dossier ; le mode et les jetons */
	char s[300], r[200];
	if (st == ACP_STARTING) snprintf (s, sizeof s, TR ("démarrage de Vibe… %.0f s"), now () - g_t0);
	else if (st == ACP_SIGNING_IN) snprintf (s, sizeof s, "%s", TR ("connexion au compte : ouvrez le lien"));
	else if (st == ACP_OPENING) snprintf (s, sizeof s, "%s", TR ("ouverture de la séance…"));
	else if (turn) snprintf (s, sizeof s, acp_pending_permission (A) ? TR ("approbation demandée") : TR ("Vibe travaille… %.0f s · Échap : interrompre"), now () - g_turn_t0);
	else if (st == ACP_DEAD) snprintf (s, sizeof s, "%s", TR ("l'agent s'est arrêté"));
	else snprintf (s, sizeof s, "%s", g_cwd);
	const char *m = acp_mode (A);
	if (g_size > 0) snprintf (r, sizeof r, TR ("%s (Maj+Tab) · %ld %% de %ldk jetons"), *m ? m : "…", g_used * 100 / g_size, g_size / 1000);
	else snprintf (r, sizeof r, TR ("%s (Maj+Tab)"), *m ? m : "…");
	int rw = (int) ui_text_width ((long) r, PX - 1, f_mono ());
	int yb = FOOT_H + BAR_H / 2 + PX / 2 - 1, rx = w - PAD - rw;
	g_moderect[0] = rx; g_moderect[1] = FOOT_H; g_moderect[2] = rw; g_moderect[3] = BAR_H;
	ui_text_fit ((long) s, PAD, yb, c_sys (), PX - 1, f_mono (), rx - 2 * PAD);
	ui_text ((long) r, rx, yb, st == ACP_IDLE && !strcmp (m, "ask") ? c_sys () : jet (TJ_ALERTE), PX - 1, f_mono (), 0);
	return 0;
}

/* la hauteur du pied suit le nombre de lignes de la demande (six au plus) */
static void foot_fit (void)
{
	int n = 1;
	for (size_t i = 0; i < g_inlen; i++) if (g_in[i] == '\n') n++;
	if (n > 6) n = 6;
	int h = FOOT_BASE + (n - 1) * LH ();
	if (h != FOOT_H) { FOOT_H = h; g_stick = 1; repaint (); }
}

static void in_set (const char *t)
{
	g_inlen = strlen (t);
	if (g_inlen >= sizeof g_in) g_inlen = sizeof g_in - 1;
	memcpy (g_in, t, g_inlen);
	g_in[g_inlen] = 0;
	g_incur = g_inlen;
}

static void in_insert (const char *t, size_t n)
{
	if (g_inlen + n >= sizeof g_in) return;
	memmove (g_in + g_incur + n, g_in + g_incur, g_inlen - g_incur);
	memcpy (g_in + g_incur, t, n);
	g_inlen += n;
	g_incur += n;
	g_in[g_inlen] = 0;
}

static long foot_key (long v, long code, long ch, long mods)
{
	(void) v;
	int ctrl = (mods & UI_MOD_CTRL) != 0;
	if ((code == 0x28 || code == 0x58) && (mods & UI_MOD_SHIFT)) in_insert ("\n", 1);	/* Maj+Entrée : à la ligne */
	else if (code == 0x28 || code == 0x58) { send_text (); return UN_CHANGE; }		/* Entrée : envoyer */
	/* Échap : efface la demande en cours ; le champ vide pendant un tour : interrompt */
	else if (code == KEY_ESC) { if (g_inlen) g_inlen = g_incur = 0; else { interrupt (); return UN_CHANGE; } }
	else if (code == 0x2B && (mods & UI_MOD_SHIFT)) { cycle_mode (); return UN_CHANGE; }	/* Maj+Tab */
	else if (ctrl && code == 0x19)							/* Ctrl+V */
	{
		char buf[2048];
		long n = ui_clip_get ((long) buf, sizeof buf);
		for (long i = 0; i < n && buf[i]; i++) if (buf[i] == '\r' || buf[i] == '\t') buf[i] = ' ';
		if (n > 0) in_insert (buf, strlen (buf));
	}
	else if (ctrl && code == 0x18) { g_inlen = g_incur = 0; }			/* Ctrl+U */
	else if (ctrl) return -1;							/* les raccourcis du menu */
	else if (code == 0x2A && g_incur)						/* Retour arrière */
	{
		size_t b = g_incur;
		do b--; while (b && ((unsigned char) g_in[b] & 0xC0) == 0x80);
		memmove (g_in + b, g_in + g_incur, g_inlen - g_incur);
		g_inlen -= g_incur - b;
		g_incur = b;
	}
	else if (code == 0x4C && g_incur < g_inlen)					/* Suppr */
	{
		size_t e = g_incur + 1;
		while (e < g_inlen && ((unsigned char) g_in[e] & 0xC0) == 0x80) e++;
		memmove (g_in + g_incur, g_in + e, g_inlen - e);
		g_inlen -= e - g_incur;
	}
	else if (code == 0x50) { while (g_incur && ((unsigned char) g_in[--g_incur] & 0xC0) == 0x80); }
	else if (code == 0x4F) { if (g_incur < g_inlen) do g_incur++; while (g_incur < g_inlen && ((unsigned char) g_in[g_incur] & 0xC0) == 0x80); }
	else if (code == 0x4A) g_incur = 0;
	else if (code == 0x4D) g_incur = g_inlen;
	else if (code == 0x52 || code == 0x51)
	{
		/* haut, bas : de ligne en ligne dans une demande de plusieurs lignes, sinon l'historique */
		size_t ls = g_incur;
		while (ls && g_in[ls - 1] != '\n') ls--;
		size_t col = g_incur - ls;
		char *nx = memchr (g_in + g_incur, '\n', g_inlen - g_incur);
		if (code == 0x52 && ls)
		{
			size_t ps = ls - 1;
			while (ps && g_in[ps - 1] != '\n') ps--;
			g_incur = ps + (col < ls - 1 - ps ? col : ls - 1 - ps);
		}
		else if (code == 0x51 && nx)
		{
			size_t ns = (size_t) (nx - g_in) + 1, ne = ns;
			while (ne < g_inlen && g_in[ne] != '\n') ne++;
			g_incur = ns + (col < ne - ns ? col : ne - ns);
		}
		else if (code == 0x52 && g_hpos > 0) in_set (g_hist[--g_hpos]);
		else if (code == 0x51 && g_hpos < g_nhist) { g_hpos++; in_set (g_hpos < g_nhist ? g_hist[g_hpos] : ""); }
		while (g_incur < g_inlen && ((unsigned char) g_in[g_incur] & 0xC0) == 0x80) g_incur++;
	}
	else if (code == 0x4B) { scroll_by (-view_h () + 40); return UN_CHANGE; }
	else if (code == 0x4E) { scroll_by (view_h () - 40); return UN_CHANGE; }
	else if (ch >= 0x20 && ch != 0x7F)
	{
		/* le caractère (point de code) en UTF-8 */
		char u[4];
		size_t n;
		unsigned c = (unsigned) ch;
		if (c < 0x80) { u[0] = (char) c; n = 1; }
		else if (c < 0x800) { u[0] = (char) (0xC0 | (c >> 6)); u[1] = (char) (0x80 | (c & 63)); n = 2; }
		else if (c < 0x10000) { u[0] = (char) (0xE0 | (c >> 12)); u[1] = (char) (0x80 | ((c >> 6) & 63)); u[2] = (char) (0x80 | (c & 63)); n = 3; }
		else { u[0] = (char) (0xF0 | (c >> 18)); u[1] = (char) (0x80 | ((c >> 12) & 63)); u[2] = (char) (0x80 | ((c >> 6) & 63)); u[3] = (char) (0x80 | (c & 63)); n = 4; }
		in_insert (u, n);
	}
	else return -1;
	foot_fit ();
	ui_view_dirty (g_vfoot);
	return UN_CHANGE;
}

static long foot_mouse (long v, long kind, long x, long y, long info)
{
	(void) v; (void) info;
	if (kind != 2) return 0;
	int r[4];
	memcpy (r, g_btn, sizeof r);
	/* le bouton : Envoyer (comme Entrée), ou Interrompre pendant un tour (comme Échap) */
	if (x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]) { if (acp_state (A) == ACP_TURN) interrupt (); else send_text (); }
	else if (x >= g_moderect[0] && y >= g_moderect[1]) mode_popup ();
	ui_w_focus (g_vfoot);
	return 0;
}

static void add_history (const char *t)
{
	if (!*t || (g_nhist && !strcmp (g_hist[g_nhist - 1], t))) { g_hpos = g_nhist; return; }
	if (g_nhist == 100) { free (g_hist[0]); memmove (g_hist, g_hist + 1, 99 * sizeof *g_hist); g_nhist--; }
	g_hist[g_nhist++] = strdup (t);
	g_hpos = g_nhist;
}

static void mode_button (void)
{
	char t[80];
	const char *m = acp_mode (A);
	snprintf (t, sizeof t, "%s : %s ▾", TR ("Mode"), *m ? m : "…");
	ui_w_set_text (g_bmode, (long) t);
}

static void mode_popup (void)
{
	static const char *const M[3] = { "ask", "accept-edits", "auto-approve" };
	char t[200];
	int k = 0;
	for (int i = 0; i < 3; i++) { strcpy (t + k, M[i]); k += (int) strlen (M[i]) + 1; }
	t[k] = 0;
	long r = ui_popup_local (PAD + (int) ui_text_width ((long) "Vibe", PX + 3, f_ui ()) + 24, TOOL_H - 2, (long) t, 200);
	if (r >= 0 && r < 3) acp_set_mode (A, M[r]);
}

static void repaint (void)
{
	/* la mise en page d'abord (montrer ou cacher un widget le dessine), puis les fonds, puis tous les widgets */
	mode_button ();
	lay_out ();
	ui_fill (0, 0, g_ww, TOOL_H, jet (TJ_FACE));
	ui_fill (0, TOOL_H - 1, g_ww, 1, jet (TJ_LIGNE));
	int wl = (int) ui_text_width ((long) "☰", PX, f_ui ()) + 24;
	ui_text ((long) "Vibe", 8 + wl + 12, TOOL_H / 2 + (PX + 3) / 2 - 1, jet (TJ_TEXTE1), PX + 3, f_ui_b (), 0);
	if (g_left)
	{
		ui_fill (0, TOOL_H, LEFT_W, g_wh - TOOL_H, jet (TJ_CREUX));
		ui_fill (LEFT_W - 1, TOOL_H, 1, g_wh - TOOL_H, jet (TJ_LIGNE));
	}
	if (g_right)
	{
		ui_fill (g_ww - RIGHT_W, TOOL_H, RIGHT_W, g_wh - TOOL_H, jet (TJ_FACE));
		ui_fill (g_ww - RIGHT_W, TOOL_H, 1, g_wh - TOOL_H, jet (TJ_LIGNE));
	}
	send_button ();
	paint_foot ();
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
}

static void on_resize (long w, long h)
{
	g_ww = (int) w;
	g_wh = (int) h;
	metrics ();
	for (int i = 0; i < nitems; i++) changed (&items[i]);
	repaint ();
}

/* --- les événements de libacp ---------------------------------------------------------------------------------------- */

static item *last_text;

static void ev_ready (void *ud, const char *agent, const char *version)
{
	(void) ud;
	snprintf (g_agent, sizeof g_agent, "%s %s", agent, version);
	changed (&items[0]);
	char t[200];
	snprintf (t, sizeof t, TR ("prêt en %.1f s"), now () - g_t0);
	add (I_NOTE, t);
	relayout ();
}

static void ev_sign_in (void *ud, const char *url, const char *expires)
{
	(void) ud; (void) expires;
	setstr (&g_url, url);
	add (I_SIGNIN, url);
	relayout ();
	repaint ();
}

static void ev_signed_in (void *ud, int ok, const char *st)
{
	(void) ud;
	for (int i = nitems - 1; i >= 0; i--)
		if (items[i].kind == I_SIGNIN)
		{
			jb_free (&items[i].text);
			memmove (&items[i], &items[i + 1], (size_t) (nitems - i - 1) * sizeof *items);
			nitems--;
			break;
		}
	setstr (&g_url, NULL);
	if (ok) add (I_NOTE, TR ("Connecté au compte Mistral."));
	else
	{
		char t[160];
		snprintf (t, sizeof t, TR ("Connexion non faite (%s)."), st);
		add (I_ERROR, t);
	}
	relayout ();
}

static void ev_session (void *ud, const char *id, const char *mode, int loaded)
{
	(void) ud; (void) mode;
	last_text = NULL;
	changed (&items[0]);
	char t[200];
	snprintf (t, sizeof t, loaded ? TR ("séance %s reprise") : TR ("séance %s ouverte"), id);
	add (I_NOTE, t);
	relayout ();
	repaint ();
	ui_w_focus (g_vfoot);
}

static void ev_mode (void *ud, const char *mode) { (void) ud; (void) mode; mode_button (); ui_w_draw (g_bmode); paint_foot (); }

static void ev_text (void *ud, int kind, const char *text)
{
	(void) ud;
	/* le texte de l'utilisateur : rejoué par session/load ; pendant un tour, Vibe renvoie la demande déjà affichée */
	if (kind == ACP_USER_TEXT && acp_state (A) == ACP_TURN) return;
	int k = kind == ACP_USER_TEXT ? I_USER : kind == ACP_THOUGHT_TEXT ? I_THOUGHT : I_AGENT;
	if (!last_text || last_text != &items[nitems - 1] || last_text->kind != k) last_text = add (k, NULL);
	jb_puts (&last_text->text, text);
	changed (last_text);
	relayout ();
}

static char *first_lines (const char *t, int max)
{
	jbuf b;
	jb_init (&b);
	int n = 0;
	const char *p = t;
	while (*p && n < max)
	{
		size_t k = strcspn (p, "\n");
		if (n) jb_putc (&b, '\n');
		jb_putn (&b, p, k > 200 ? 200 : k);
		n++;
		p += k;
		if (*p) p++;
	}
	if (*p) jb_puts (&b, "\n…");
	return b.p ? b.p : strdup ("");
}

/* le diff d'une modification (lignes communes du début et de la fin retirées, deux de contexte) dans b */
static void diff_text (jbuf *b, const char *path, const char *old, const char *new_)
{
	static const char *ol[400], *nl[400];
	static size_t on[400], nn[400];
	int na = 0, nb = 0;
	for (const char *p = old; *p && na < 400;) { size_t k = strcspn (p, "\n"); ol[na] = p; on[na++] = k; p += k; if (*p) p++; }
	for (const char *p = new_; *p && nb < 400;) { size_t k = strcspn (p, "\n"); nl[nb] = p; nn[nb++] = k; p += k; if (*p) p++; }
	int pre = 0, suf = 0;
	while (pre < na && pre < nb && on[pre] == nn[pre] && !memcmp (ol[pre], nl[pre], on[pre])) pre++;
	while (suf < na - pre && suf < nb - pre && on[na - 1 - suf] == nn[nb - 1 - suf]
		&& !memcmp (ol[na - 1 - suf], nl[nb - 1 - suf], on[na - 1 - suf])) suf++;
	jb_puts (b, "@ ");
	jb_puts (b, path);
	for (int i = pre > 2 ? pre - 2 : 0; i < pre; i++) { jb_puts (b, "\n  "); jb_putn (b, ol[i], on[i]); }
	int shown = 0;
	for (int i = pre; i < na - suf && shown < 30; i++, shown++) { jb_puts (b, "\n- "); jb_putn (b, ol[i], on[i]); }
	for (int i = pre; i < nb - suf && shown < 30; i++, shown++) { jb_puts (b, "\n+ "); jb_putn (b, nl[i], nn[i]); }
	if (shown >= 30) jb_puts (b, "\n  …");
	for (int i = nb - suf, k = 0; i < nb && k < 2; i++, k++) { jb_puts (b, "\n  "); jb_putn (b, nl[i], nn[i]); }
}

static char *diff_shown[256];
static int ndiff;

static int first_diff (const char *id)
{
	for (int i = 0; i < ndiff; i++) if (!strcmp (diff_shown[i], id)) return 0;
	if (ndiff < 256) diff_shown[ndiff++] = strdup (id);
	return 1;
}

static void show_diff (const acp_tool *t)
{
	if (!t->diff_path || !first_diff (t->id)) return;
	item *it = add (I_DIFF, NULL);
	diff_text (&it->text, t->diff_path, t->diff_old, t->diff_new);
	note_modified (t->diff_path);
	show_file (t->diff_path);
	show_info ();
}

static void ev_tool (void *ud, const acp_tool *t)
{
	(void) ud;
	last_text = NULL;
	item *it = find_tool (t->id);
	if (!it)
	{
		it = add (I_TOOL, t->title);
		it->id = strdup (t->id);
	}
	else
	{
		it->text.n = 0;
		jb_puts (&it->text, t->title);
	}
	setstr (&it->status, t->status);
	int done = !strcmp (t->status, "completed"), failed = !strcmp (t->status, "failed");
	if ((done || failed) && *t->text && (!strcmp (t->kind, "execute") || failed) && !it->extra)
		it->extra = first_lines (t->text, 8);
	changed (it);
	if (done || failed) show_diff (t);
	relayout ();
}

static const char *option_label (const acp_option *o)
{
	if (!strcmp (o->kind, "allow_once")) return TR ("Autoriser une fois");
	if (!strcmp (o->kind, "allow_always")) return TR ("Pour la séance");
	if (!strncmp (o->kind, "reject", 6)) return TR ("Refuser");
	return o->name;
}

/* l'approbation : un dialogue modal ; trois réponses (une fois, pour la séance, refuser) */
static void ev_permission (void *ud, long req, const acp_tool *t, int n, const acp_option *o)
{
	(void) ud;
	last_text = NULL;
	paint_foot ();
	int once = -1, session = -1, reject = -1;
	for (int i = 0; i < n; i++)
	{
		if (!strcmp (o[i].kind, "allow_once") && once < 0) once = i;
		else if (!strcmp (o[i].kind, "allow_always") && session < 0 && !strstr (o[i].id, "permanent")) session = i;
		else if (!strncmp (o[i].kind, "reject", 6) && reject < 0) reject = i;
	}
	int W = g_ww - 40 < 620 ? g_ww - 40 : 620;
	if (W < 320) W = 320;
	int has_diff = t->diff_path != NULL;
	int DH = has_diff ? 220 : !strcmp (t->kind, "execute") ? 70 : 0;
	int H = 44 + 54 + DH + CTL + 36;
	long d = ui_dialog_begin (W, H, (long) TR ("Vibe demande votre accord"));
	if (!d) { acp_permit (A, req, reject >= 0 ? o[reject].id : NULL); return; }
	int y = 44, x = 16;
	ui_label (x, y, W - 2 * x, 48, (long) t->title, ULB_BOLD | ULB_WRAP);
	y += 54;
	if (DH)
	{
		long ta = ui_textarea (x, y, W - 2 * x, DH - 10, UTF_READONLY | UTF_MONO);
		jbuf b;
		jb_init (&b);
		if (has_diff) diff_text (&b, t->diff_path, t->diff_old, t->diff_new);
		else jb_puts (&b, t->title);
		ui_w_set_text (ta, (long) jb_cstr (&b));
		jb_free (&b);
	}
	long e = W - 12;
	if (reject >= 0) e = ui_dialog_button (d, 2, (long) option_label (&o[reject]), 3, 0, e);
	if (session >= 0) e = ui_dialog_button (d, 1, (long) option_label (&o[session]), 2, 0, e);
	if (once >= 0) ui_dialog_button (d, 0, (long) option_label (&o[once]), 1, UB_DEFAULT, e);
	ui_dialog_cancel (d, 3);
	int res = (int) ui_dialog_run (d, 0, 0);
	ui_dialog_end (d);
	const char *pick = res == 1 && once >= 0 ? o[once].id : res == 2 && session >= 0 ? o[session].id
		: reject >= 0 ? o[reject].id : NULL;
	acp_permit (A, req, pick);
	char note[200];
	snprintf (note, sizeof note, "└ %s", res == 1 ? TR ("autorisé une fois") : res == 2 ? TR ("autorisé pour la séance") : TR ("refusé"));
	add (I_NOTE, note);
	relayout ();
	repaint ();
}

static void ev_turn_end (void *ud, const char *reason)
{
	(void) ud;
	last_text = NULL;
	char t[160];
	const char *fr = !strcmp (reason, "end_turn") ? NULL : !strcmp (reason, "cancelled") ? TR ("interrompu")
		: !strcmp (reason, "max_tokens") ? TR ("réponse trop longue") : !strcmp (reason, "refusal") ? TR ("refusé") : reason;
	if (fr) snprintf (t, sizeof t, "(%s · %.1f s)", fr, now () - g_turn_t0);
	else snprintf (t, sizeof t, "(%.1f s)", now () - g_turn_t0);
	add (I_NOTE, t);
	fill_tree ();			/* l'agent a pu créer ou retirer des fichiers */
	if (g_shown) show_file (g_shown);
	show_info ();
	refresh_sessions ();		/* le titre de la séance vient après le premier tour */
	relayout ();
	repaint ();
	ui_w_focus (g_vfoot);
}

static void ev_error (void *ud, const char *msg)
{
	(void) ud;
	last_text = NULL;
	add (I_ERROR, msg);
	relayout ();
	repaint ();
}

static void ev_exited (void *ud)
{
	(void) ud;
	add (I_ERROR, TR ("L'agent s'est arrêté (journal : /tmp/vibe-app.log)."));
	relayout ();
	repaint ();
}

static void ev_usage (void *ud, long used, long size)
{
	(void) ud;
	g_used = used;
	g_size = size;
	paint_foot ();
}

/* --- les commandes ------------------------------------------------------------------------------------------------- */

static const char *CFG = "/sd0/.vibe/korin-app.txt";

static void save_cwd (void)
{
	mkdir ("/sd0/.vibe", 0700);
	FILE *f = fopen (CFG, "w");
	if (f) { fprintf (f, "%s\n", g_cwd); fclose (f); }
}

static void load_cwd (void)
{
	FILE *f = fopen (CFG, "r");
	if (f)
	{
		if (fgets (g_cwd, sizeof g_cwd, f)) g_cwd[strcspn (g_cwd, "\n")] = 0;
		fclose (f);
	}
	struct stat st;
	if (!*g_cwd || stat (g_cwd, &st) != 0) snprintf (g_cwd, sizeof g_cwd, "/sd0/home");
}

static void new_session (const char *load, const char *cwd)
{
	if (acp_state (A) != ACP_IDLE) return;
	clear_items ();
	last_text = NULL;
	for (int i = 0; i < g_nmod; i++) free (g_modified[i]);
	g_nmod = 0;
	acp_open (A, load, cwd);
	fill_tree ();
	show_info ();
	relayout ();
	repaint ();
}

static void cycle_mode (void)
{
	static const char *const M[3] = { "ask", "accept-edits", "auto-approve" };
	const char *m = acp_mode (A);
	int i = 0;
	while (i < 3 && strcmp (M[i], m)) i++;
	acp_set_mode (A, M[(i + 1) % 3]);
}

static void help (void)
{
	add (I_NOTE, TR ("Entrée : envoyer · Échap : interrompre le tour · Maj+Tab : mode (ask demande avant chaque "
		"modification et chaque commande ; accept-edits modifie sans demander ; auto-approve ne demande rien)\n"
		"/new : nouvelle séance · /continue : reprendre la dernière séance du dossier · /mode [nom] · /help\n"
		"Les autres commandes « / » vont à Vibe (ses compétences). Le panneau de gauche : double-clic sur un fichier "
		"pour l'ouvrir."));
	relayout ();
}

/* Échap, ou le bouton pendant un tour : session/cancel */
static void interrupt (void)
{
	if (acp_state (A) != ACP_TURN) return;
	acp_cancel (A);
	add (I_NOTE, TR ("interruption du tour…"));
	relayout ();
}

/* Entrée ou Envoyer : rien sans texte, rien pendant un tour */
static void send_text (void)
{
	g_in[g_inlen] = 0;
	const char *t = g_in;
	while (*t == ' ') t++;
	if (!*t || acp_state (A) != ACP_IDLE) return;
	char *copy = strdup (t);
	add_history (copy);
	g_inlen = g_incur = 0;
	foot_fit ();
	paint_foot ();
	if (!strcmp (copy, "/help")) help ();
	else if (!strcmp (copy, "/new")) new_session (NULL, NULL);
	else if (!strcmp (copy, "/continue")) new_session ("last", NULL);
	else if (!strncmp (copy, "/mode", 5) && (copy[5] == 0 || copy[5] == ' '))
	{
		const char *m = copy + 5;
		while (*m == ' ') m++;
		if (*m) acp_set_mode (A, m);
		else cycle_mode ();
	}
	else if (acp_prompt (A, copy))
	{
		last_text = NULL;
		add (I_USER, copy);
		g_turn_t0 = now ();
		relayout ();
		repaint ();
	}
	free (copy);
}

static void build_menu (void)
{
	static char t[600];
	const char *m = acp_mode (A);
	snprintf (t, sizeof t,
		"MVibe\nI%d\t%s\tCtrl+N\nI%d\t%s\nI%d\t%s\n-\nI%d\t%s\nI%d\t%s\n-\nI%d\t%s\n"
		"MMode\nI%d\t%sask : %s\nI%d\t%saccept-edits : %s\nI%d\t%sauto-approve : %s\n",
		M_NEW, TR ("Nouvelle séance"), M_LAST, TR ("Reprendre la dernière séance"), M_FOLDER, TR ("Dossier de travail..."),
		M_LEFT, TR ("Volet des séances"), M_RIGHT, TR ("Volet de droite (fichiers, infos, commandes)"), M_QUIT, TR ("Quitter"),
		M_ASK, !strcmp (m, "ask") ? "• " : "", TR ("demander avant chaque action"),
		M_EDITS, !strcmp (m, "accept-edits") ? "• " : "", TR ("modifications sans demander"),
		M_AUTO, !strcmp (m, "auto-approve") ? "• " : "", TR ("tout sans demander"));
	ui_menu ((long) t);
}

static int command (long id)
{
	switch (id)
	{
	case M_NEW: new_session (NULL, NULL); break;
	case M_LAST: new_session ("last", NULL); break;
	case M_FOLDER:
	{
		char buf[512];
		if (acp_state (A) == ACP_IDLE && ui_folder_choose ((long) g_cwd, (long) buf, sizeof buf))
		{
			snprintf (g_cwd, sizeof g_cwd, "%s", buf);
			save_cwd ();
			new_session (NULL, g_cwd);
		}
		break;
	}
	case M_LEFT:
	case M_RIGHT:
		if (id == M_LEFT) g_left_want = !g_left_want;
		else g_right_want = !g_right_want;
		for (int i = 0; i < nitems; i++) changed (&items[i]);
		repaint ();
		break;
	case M_QUIT: return 0;
	case M_ASK: acp_set_mode (A, "ask"); break;
	case M_EDITS: acp_set_mode (A, "accept-edits"); break;
	case M_AUTO: acp_set_mode (A, "auto-approve"); break;
	}
	return 1;
}

static void control (long w, long code)
{
	if (code == UN_ACTIVATE && w == g_bmode) mode_popup ();
	else if (code == UN_ACTIVATE && w == g_bnew) command (M_NEW);
	else if (code == UN_ACTIVATE && w == g_bleft) command (M_LEFT);
	else if (code == UN_ACTIVATE && w == g_bright) command (M_RIGHT);
	else if ((code == UN_ACTIVATE || code == UN_CHANGE) && w == g_tree)
	{
		/* un clic : le contenu à droite ; double-clic ou Entrée : ouvert dans son app */
		long data = -1;
		uikit_call3 ((long (*) ()) ui_tree_get, g_tree, ui_w_get_value (g_tree), 0, 0, &data, 0);
		if (data >= 0 && data < g_npaths)
		{
			show_file (g_paths[data]);
			struct stat st;
			if (code == UN_ACTIVATE && stat (g_paths[data], &st) == 0 && !S_ISDIR (st.st_mode)) ui_assoc_open ((long) g_paths[data]);
		}
	}
	else if (code == UN_ACTIVATE && w == g_sess)
	{
		long i = ui_w_get_value (g_sess);
		if (i >= 0 && i < g_nsess && (!acp_session_id (A) || strcmp (acp_session_id (A), g_sess_id[i]))) new_session (g_sess_id[i], NULL);
	}
	else if (w == g_tabs && code == UN_CHANGE) { show_pages (); if (ui_w_get_value (g_tabs) == 1) show_info (); ui_w_draw (0); }
	else if (code == UN_ACTIVATE && (w == g_cmdline || w == g_cmdrun)) run_command ();
	else if (w == g_sbar && code == UN_CHANGE)
	{
		g_scroll = (int) ui_w_get_value (g_sbar);
		g_stick = 0;
		clamp_scroll ();
		ui_view_dirty (g_vchat);
	}
}

int main (int argc, char **argv)
{
	acp_options o = { 0 };
	o.mode = "ask";
	o.sign_in = 1;
	o.client = "korin-vibe";
	o.log = "/tmp/vibe-app.log";
	char *agent_argv[16] = { "vibe-acp", NULL };
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--cwd") && i + 1 < argc) snprintf (g_cwd, sizeof g_cwd, "%s", argv[++i]);
		else if (!strcmp (argv[i], "--continue")) o.load = "last";
		else if (!strcmp (argv[i], "--agent") && i + 1 < argc)
		{
			char *s = strdup (argv[++i]);
			int n = 0;
			for (char *t = strtok (s, " "); t && n < 15; t = strtok (NULL, " ")) agent_argv[n++] = t;
			agent_argv[n] = NULL;
		}
	}
	if (!*g_cwd) load_cwd ();
	o.cwd = g_cwd;
	o.argv = agent_argv;
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned char *) ui_jetons ();
	metrics ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int w = 900 < scr[0] - 40 ? 900 : (int) scr[0] - 40;
	int h = 640;
	if (h > scr[1] - scr[2] - scr[3] - 16) h = (int) (scr[1] - scr[2] - scr[3] - 16);
	/* (le terminal commence sous la barre d'outils : les glyphes du cadre restent sur la barre, au thème) */
	long win = ui_window_ex (-1, -1, w, h, (long) "Vibe", WF_RESIZABLE);
	if (win <= 0) return 1;
	ui_min_size (360, 320);
	g_ww = (int) ui.w;
	g_wh = (int) ui.h;
	g_lsess = ui_label (0, 0, 10, 20, (long) TR ("Séances de ce dossier"), ULB_BOLD);
	g_sess = ui_list (0, 0, 10, 10, 0);
	g_tabs = ui_tabs (0, 0, 10, 10, (long) TR ("Fichiers|Infos|Commandes"));
	for (int i = 0; i < 3; i++) g_pg[i] = ui_tabs_page (g_tabs, i);
	g_tree = ui_tree (0, 0, 10, 10);
	g_flabel = ui_label (0, 0, 10, 20, (long) TR ("Un clic sur un fichier le montre ici."), ULB_DIM);
	g_fview = ui_textarea (0, 0, 10, 10, UTF_READONLY | UTF_MONO);
	ui_w_set_parent (g_tree, g_pg[0]);
	ui_w_set_parent (g_flabel, g_pg[0]);
	ui_w_set_parent (g_fview, g_pg[0]);
	g_info = ui_textarea (0, 0, 10, 10, UTF_READONLY);
	ui_w_set_parent (g_info, g_pg[1]);
	g_cmdline = ui_textbox (0, 0, 10, CTL, UTF_MONO);
	ui_textbox_hint (g_cmdline, (long) TR ("une commande (bash), dans le dossier de travail"));
	g_cmdrun = ui_pushbutton (0, 0, 60, CTL, (long) TR ("Lancer"), 0);
	g_cmdout = ui_textarea (0, 0, 10, 10, UTF_READONLY | UTF_MONO);
	ui_w_set_parent (g_cmdline, g_pg[2]);
	ui_w_set_parent (g_cmdrun, g_pg[2]);
	ui_w_set_parent (g_cmdout, g_pg[2]);
	jb_init (&g_cmd_out);
	g_vchat = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vchat, (long) chat_paint, (long) chat_mouse, 0, (long) chat_wheel);
	g_sbar = ui_scrollbar (0, 0, SBW, 10, 0);
	g_vfoot = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_vfoot, (long) foot_paint, (long) foot_mouse, (long) foot_key, 0);
	g_bmode = ui_pushbutton (0, 0, 90, CTL, (long) "Mode", UB_GHOST);
	g_bnew = ui_pushbutton (0, 0, 90, CTL, (long) TR ("Nouvelle séance"), UB_GHOST);
	g_bleft = ui_pushbutton (0, 0, 40, CTL, (long) "☰", UB_GHOST);
	g_bright = ui_pushbutton (0, 0, 90, CTL, (long) TR ("Panneau"), UB_GHOST);
	ui_on_resize ((long) on_resize, 0);

	g_t0 = now ();
	acp_events ev = { 0 };
	ev.ready = ev_ready; ev.sign_in = ev_sign_in; ev.signed_in = ev_signed_in; ev.session = ev_session;
	ev.mode = ev_mode; ev.text = ev_text; ev.tool = ev_tool; ev.permission = ev_permission; ev.turn_end = ev_turn_end;
	ev.error = ev_error; ev.exited = ev_exited; ev.usage = ev_usage;
	add (I_WELCOME, NULL);
	A = acp_start (&o, &ev, NULL);
	add (I_NOTE, TR ("Démarrage du moteur de Vibe (Python) : sous Korin, cela peut prendre une minute ou deux."));
	build_menu ();
	fill_tree ();
	show_info ();
	repaint ();
	ui_w_focus (g_vfoot);

	char last_mode[32] = "";
	int last_state = -1;
	double last_foot = 0;
	int run = 1;
	while (run)
	{
		long t = ui_event (40);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) run = command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_THEME) { for (int i = 0; i < nitems; i++) changed (&items[i]); repaint (); }
		else if (t > 0)
		{
			int press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int k = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
			/* Maj+Tab : le mode ; Échap : interrompre (avant le champ, qui les prendrait) */
			if (press && k == 0x2B && (mods & UI_MOD_SHIFT)) { cycle_mode (); continue; }
			if (press && k == KEY_ESC && acp_state (A) == ACP_TURN && !g_inlen) { interrupt (); continue; }
			long code = 0, taken = 0;
			long w2 = uikit_w_event (&code, &taken);
			if (code) control (w2, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) run = command (id);
				else if (k == 0x4B) scroll_by (-view_h () + 40);	/* Page préc. */
				else if (k == 0x4E) scroll_by (view_h () - 40);		/* Page suiv. */
			}
		}
		else ui_w_idle ();
		acp_pump (A, 0);
		cmd_pump ();
		int st = acp_state (A);
		if (st == ACP_IDLE && last_state == ACP_OPENING) { refresh_sessions (); show_info (); }
		if (st != last_state) { last_state = st; paint_foot (); }
		else if ((st == ACP_TURN || st == ACP_STARTING) && now () - last_foot > 1) { last_foot = now (); paint_foot (); }
		if (strcmp (last_mode, acp_mode (A)))
		{
			snprintf (last_mode, sizeof last_mode, "%s", acp_mode (A));
			build_menu ();
			mode_button ();
			ui_w_draw (g_bmode);
			paint_foot ();
		}
		ui_view_flush ();
	}
	acp_close (A);
	return 0;
}
