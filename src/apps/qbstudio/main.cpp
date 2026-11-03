//
// qbstudio -- QBStudio, l'IDE des applications de bureau en BASIC, porté d'Onyx sur UIKit (src/apps/qbstudio/README.md).
// Un projet est un dossier (SD0:/home/projects/<nom>/, son project.ini) : sa fenêtre, Main.form (un contrôle par ligne, le
// parent par l'indentation : form.h), dessinée et modifiée dans le concepteur (designer.h : les contrôles glissés de la
// boîte à outils, déplacés, dimensionnés ; les propriétés à droite : props.h) et en texte, les deux tenus au pas ; son
// code, Main.bas (les SUB des événements : convert_Click...), dans l'éditeur (codeedit.h : les couleurs de BASIC, la
// complétion après le nom d'un contrôle, les problèmes pendant la frappe) ; le code de la fenêtre, Main.form.bas, généré
// (gen.h) et en lecture seule. Run (F5) construit le programme -- la bibliothèque des contrôles, le code des fenêtres, le
// vôtre, puis Main_Run -- et le lance par SD0:/bin/basic (une erreur, au départ ou à l'exécution, est montrée dans son
// fichier à sa ligne) ; File > Make App l'écrit en application (SD0:/apps/<nom>.app : le runtime et le programme, app.txt).
//
// La fenêtre : la barre d'outils (les fichiers, les éditions, Run, Make App ; Design / Split / Code), le projet et la boîte
// à outils (dans le code : le plan) à gauche, les onglets des fichiers ouverts, le concepteur et le texte du formulaire (ou
// le code, ses listes d'objet et d'événement au-dessus), les problèmes dessous, les propriétés à droite, la barre d'état.
// Tout est aux ui_* de l'OS, sans classe autour d'eux.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app.h"
#include "../../lib/basic/bas.h"
#include "../../lib/basic/hostutil.h"
#include "codeedit.h"
#include "designer.h"
#include "props.h"
#include "gen.h"

using namespace qs;

enum { W0 = 1000, H0 = 700, LEFT_W = 210, RIGHT_W = 244, MSG_H = 118, ST_H = 24, HEAD_H = 24, TREE_H = 176, BAR_H = 6 };
static int TB_H = 44, CODEBAR_H = 38, TABS_H = 32;
#define PROJECTS "SD0:/home/projects"
static const char *SAMPLE = PROJECTS "/converter";
static const char *SETTINGS = PROJECTS "/qbstudio.ini";

// ---- le projet ---------------------------------------------------------------------------------------------------
enum { DOC_FORM, DOC_CODE, DOC_GEN, DOC_TEXT };
struct Doc
{
	char file[64]; int type;
	long ed;
	Form *form;					// DOC_FORM : le dernier arbre lu sans erreur
	int gen;					// DOC_FORM : son document généré
	bool dirty, open;
	int selPath[32], selN;				// le choix du concepteur (DOC_FORM)
};
struct Project
{
	char dir[200], name[40], title[64], mainForm[40], category[32];
	bool managed;				// l'application tourne sur la VM au lieu du code machine (project.ini : managed = yes)
	Vec<Doc> docs;
	Vec<int> tabs;				// les documents ouverts, dans l'ordre
	int cur;
	Project () : managed (false), cur (-1) { dir[0] = name[0] = title[0] = mainForm[0] = category[0] = 0; }
};
static Project g_p;
struct Problem { int kind; char text[180]; char file[64]; int line; };	// kind 0 erreur, 1 alerte, 2 information
static Vec<Problem> g_probs;
static Vec<Part> g_parts;				// les parties de la dernière construction (l'erreur d'exécution retrouvée)
static int g_view = 1;					// la vue d'un formulaire : 0 Design, 1 Split, 2 Text

// ---- les morceaux de la fenêtre ---------------------------------------------------------------------------------------
static long g_tb, g_seg, g_tree, g_outline, g_objBox, g_evBox, g_tabs, g_msgs, g_split_w;
static long g_hProject, g_hTools, g_hProps, g_hMsgs, g_stLeft, g_stMid, g_stRight;
static long b_new, b_open, b_save, b_undo, b_redo, b_cut, b_copy, b_paste, b_find, b_run, b_check, b_app;
static unsigned g_checkAt;				// le code revérifié alors (ms ; 0 : rien)
static bool g_reading;					// (un formulaire lu : le curseur de son texte ne choisit pas)
static int g_split = 640;				// la part du concepteur, en millièmes
static int g_splitTop = 0, g_splitRoom = 1;
static int g_edX, g_edY;				// où est l'éditeur montré

static void relayout ();
static void show_doc (int i);
static void check_program (bool quiet);
static void refresh_outline ();
static void refresh_codebar ();
static void refresh_tabs ();
static void refresh_msgs ();
static void save_settings ();

static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static bool ends_with (const char *s, const char *e) { int n = (int) strlen (s), k = (int) strlen (e); return n >= k && !strcasecmp (s + n - k, e); }
static void join (char *o, int cap, const char *dir, const char *f) { snprintf (o, cap, "%s%s%s", dir, dir[0] && dir[strlen (dir) - 1] != '/' ? "/" : "", f); }
static char *read_file (const char *path)
{
	int n; char *b = bas_load (path, &n);
	if (!b) return 0;
	int w = 0; for (int i = 0; i < n; i++) if (b[i] != '\r') b[w++] = b[i];
	b[w] = 0;
	return b;
}
static bool file_exists (const char *p) { struct stat s; return stat (p, &s) == 0; }
static bool write_file (const char *path, const char *s) { return bas_save (path, s, (int) strlen (s)); }
static void status (const char *s) { ui_w_set_text (g_stMid, (long) s); }
static char *latin1_dup (const char *s) { int n = (int) strlen (s); char *o = new char[n + 1]; bas_latin1 (o, n + 1, s); return o; }	// (BASIC parle Latin-1, les éditeurs UTF-8)

// ---- les mots de BASIC -----------------------------------------------------------------------------------------------
static char s_words[16384]; static int g_nwords; static const char **g_word; static unsigned char *g_wlen;
static void load_words ()
{
	bas::wordList (s_words, sizeof s_words);
	int n = 1; for (char *p = s_words; *p; p++) if (*p == ' ') n++;
	g_word = (const char **) malloc (sizeof (char *) * n); g_wlen = (unsigned char *) malloc (n);
	g_nwords = 0;
	for (char *p = s_words; *p; )
	{
		char *e = p; while (*e && *e != ' ') e++;
		g_word[g_nwords] = p; g_wlen[g_nwords] = (unsigned char) (e - p); g_nwords++;
		p = *e ? e + 1 : e;
	}
}
static long is_keyword (long word, long len)
{
	const char *w = (const char *) word;
	for (int i = 0; i < g_nwords; i++)
		if (g_wlen[i] == len) { int k = 0; while (k < len && upc (w[k]) == upc (g_word[i][k])) k++; if (k == len) return 1; }
	return 0;
}
static bool is_word0 (const char *w) { return is_keyword ((long) w, (long) strlen (w)) != 0; }

// ---- les documents -------------------------------------------------------------------------------------------------
static Doc *cur_doc () { return g_p.cur >= 0 ? &g_p.docs[g_p.cur] : 0; }
static int find_doc (const char *file) { for (int i = 0; i < g_p.docs.n; i++) if (!strcasecmp (g_p.docs[i].file, file)) return i; return -1; }
// Le formulaire dont parlent le concepteur, les listes et la complétion : celui qui est montré, sinon le dernier montré,
// sinon le premier
static int g_lastForm = -1;
static Doc *main_form ()
{
	if (g_p.cur >= 0 && g_p.cur < g_p.docs.n && g_p.docs[g_p.cur].type == DOC_FORM) return &g_p.docs[g_p.cur];
	if (g_lastForm >= 0 && g_lastForm < g_p.docs.n && g_p.docs[g_lastForm].type == DOC_FORM) return &g_p.docs[g_lastForm];
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_FORM) return &g_p.docs[i];
	return 0;
}
static void form_name_of (const Doc &d, char *o, int cap) { cpy (o, d.file, cap); char *dot = strchr (o, '.'); if (dot) *dot = 0; }
// La fenêtre du programme : celle que project.ini nomme (main =) si c'en est une, sinon la première
static Doc *start_form ()
{
	Doc *first = 0;
	for (int i = 0; i < g_p.docs.n; i++)
	{
		Doc &d = g_p.docs[i];
		if (d.type != DOC_FORM || !d.form || d.form->userControl ()) continue;
		char n[64]; form_name_of (d, n, sizeof n);
		if (ieq (n, g_p.mainForm)) return &d;
		if (!first) first = &d;
	}
	return first;
}
// Le code où vont les événements d'un formulaire : <Form>.bas, sinon le premier .bas
static Doc *code_for_form ()
{
	Doc *f = main_form ();
	if (f) { char n[64]; form_name_of (*f, n, sizeof n); strncat (n, ".bas", sizeof n - strlen (n) - 1); int i = find_doc (n); if (i >= 0) return &g_p.docs[i]; }
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_CODE) return &g_p.docs[i];
	return 0;
}
static void all_subs (Vec<char *> &subs) { for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_CODE) scan_subs (ed_text (g_p.docs[i].ed), subs); }
static void free_subs (Vec<char *> &subs) { for (int i = 0; i < subs.n; i++) free (subs[i]); subs.clear (); }
static bool sub_exists (const char *name) { Vec<char *> s; all_subs (s); bool r = has_sub (s, name); free_subs (s); return r; }

// Le code généré d'un formulaire refait
static void regenerate (Doc &d)
{
	if (d.type != DOC_FORM || !d.form || d.gen < 0) return;
	char fn[64]; form_name_of (d, fn, sizeof fn);
	Vec<char *> subs; all_subs (subs);
	Str g; generate (g, *d.form, fn, subs);
	free_subs (subs);
	long ge = g_p.docs[d.gen].ed;
	if (strcmp (ed_text (ge), g.str ())) ui_w_set_text (ge, (long) g.str ());
}

static Doc &add_doc (const char *file, int type, const char *text)
{
	Doc d; memset (&d, 0, sizeof d);
	cpy (d.file, file, sizeof d.file); d.type = type; d.gen = -1;
	d.ed = ui_textarea (0, 0, 100, 100, ED_FLAGS | (type == DOC_GEN ? UTF_READONLY : 0));
	ui_textarea_keywords (d.ed, (long) is_keyword);
	ui_w_set_text (d.ed, (long) (text ? text : ""));
	ui_w_show (d.ed, 0);
	g_p.docs.push (d);
	return g_p.docs[g_p.docs.n - 1];
}

// ---- le formulaire : son texte lu, son arbre écrit -------------------------------------------------------------------
static void set_problems_for (const char *file, Form *f);
// Le texte d'un formulaire relu : le concepteur et le code généré suivent (une erreur : ils gardent le dernier bon)
static void read_form (Doc &d)
{
	Form *f = new Form;
	bool ok = form_read (*f, ed_text (d.ed));
	ui_textarea_highlight (d.ed, f->errors.n ? f->errors[0].line - 1 : -1);
	set_problems_for (d.file, f);
	if (!ok) { delete f; return; }
	Form *old = d.form;
	d.form = f;
	regenerate (d);
	if (&d == cur_doc ())
	{
		g_reading = true;
		pg_drop_edit ();
		des_set_form (f);
		El *s = d.selN ? el_by_path (*f, d.selPath, d.selN) : 0;
		if (!s) s = f->root;
		des_select (s, false);
		pg_show (s);
		g_reading = false;
		refresh_codebar ();
	}
	delete old;
}
// Le concepteur (ou les propriétés) ont changé l'arbre : son texte réécrit (une opération pour Annuler)
static void set_dirty (Doc &d) { if (!d.dirty) { d.dirty = true; refresh_tabs (); } }
static void form_edited ()
{
	Doc *d = cur_doc (); if (!d || d->type != DOC_FORM || !d->form) return;
	El *s = D.sel;
	d->selN = s ? path_of (s, d->selPath, 32) : 0;
	Str t; form_write (t, *d->form, d->file);
	// le curseur : à la ligne de l'élément choisi
	int caretLine = 0;
	{
		Form tmp; form_read (tmp, t.str ());
		El *e = d->selN ? el_by_path (tmp, d->selPath, d->selN) : tmp.root;
		if (e) caretLine = e->line - 1;
	}
	g_reading = true;
	ed_replace (d->ed, t.str (), line_offset (t.str (), caretLine));
	set_dirty (*d);
	read_form (*d);
	g_reading = false;
}
static void on_select (El *e)
{
	Doc *d = cur_doc (); if (!d || d->type != DOC_FORM) return;
	d->selN = e ? path_of (e, d->selPath, 32) : 0;
	pg_show (e);
	if (e && e->line > 0 && !g_reading) { g_reading = true; ui_textarea_goto (d->ed, line_offset (ed_text (d->ed), e->line - 1)); g_reading = false; }
	char t[120]; if (e) { describe (e, t, sizeof t); status (t); }
}
static void on_element_open (El *e)
{
	if (!e) return;
	char names[4][64]; const char *labels[4];
	int n = events_of (e, names, labels, 4);
	if (n && g_openSub) g_openSub (names[0]);
}

// ---- les problèmes ---------------------------------------------------------------------------------------------------
static void add_problem (int kind, const char *file, int line, const char *text)
{
	Problem p; p.kind = kind; cpy (p.file, file ? file : "", sizeof p.file); p.line = line; cpy (p.text, text, sizeof p.text);
	g_probs.push (p);
}
static void drop_problems (const char *file) { for (int i = g_probs.n - 1; i >= 0; i--) if (!strcmp (g_probs[i].file, file)) g_probs.erase (i); }
static void set_problems_for (const char *file, Form *f)
{
	drop_problems (file);
	for (int i = 0; i < f->errors.n; i++) add_problem (0, file, f->errors[i].line, f->errors[i].msg);
	if (!f->errors.n && f->root)
	{
		// ce que le formulaire tient, ses événements traités
		int ctl = 0, handled = 0, events = 0;
		Vec<char *> subs; all_subs (subs);
		struct W { static void walk (El *e, int &c, int &h, int &ev, Vec<char *> &s) {
			if (is_control (e->kind) || e->kind == K_MENUITEM) { c++; const char *x = event_of (e->kind); if (x && e->name[0]) { ev++; char n[96]; snprintf (n, sizeof n, "%s_%s", e->name, x); if (has_sub (s, n)) h++; } }
			for (int i = 0; i < e->kids.n; i++) walk (e->kids[i], c, h, ev, s); } };
		W::walk (f->root, ctl, handled, events, subs);
		free_subs (subs);
		char t[160]; snprintf (t, sizeof t, "%d controls, %d of their %d events handled", ctl, handled, events);
		add_problem (2, file, 0, t);
	}
	if (g_msgs) refresh_msgs ();
}
static void refresh_msgs ()
{
	ui_list_clear (g_msgs);
	int errs = 0;
	for (int i = 0; i < g_probs.n; i++)
	{
		const Problem &p = g_probs[i];
		if (p.kind == 0) errs++;
		char s[300]; snprintf (s, sizeof s, "%s\t%s\t%d\t%s", TR (p.kind == 0 ? "Error" : p.kind == 1 ? "Warning" : "Info"), p.file, p.line, p.text);
		if (!p.line) { char *q = strchr (s, '\t'); if (q) { q = strchr (q + 1, '\t'); if (q && q[1] == '0' && q[2] == '\t') memmove (q + 1, q + 2, strlen (q + 2) + 1); } }
		ui_list_add (g_msgs, (long) s, 0);
	}
	char t[60]; snprintf (t, sizeof t, "%s  %d", TR ("Problems"), errs);
	ui_w_set_text (g_hMsgs, (long) t);
	ui_w_draw (g_msgs);
}
// Le programme entier : la bibliothèque, le code des fenêtres, les fichiers, le départ
static bool build (Str &prog, bool quiet)
{
	g_parts.clear ();
	add_part (prog, g_parts, "(QBStudio's controls)", LIBRARY);
	Vec<char *> ucs;					// les contrôles utilisateur, dans l'ordre : leurs numéros
	for (int i = 0; i < g_p.docs.n; i++)
	{
		Doc &d = g_p.docs[i];
		if (d.type != DOC_FORM) continue;
		if (!d.form) { if (!quiet) status (TR ("A form has errors: see the problems")); free_subs (ucs); return false; }
		if (d.form->userControl ()) { char fn[64]; form_name_of (d, fn, sizeof fn); ucs.push (strdup (fn)); }
	}
	{
		Vec<char *> subs; all_subs (subs);
		Str pg; generate_panels (pg, ucs, subs);
		free_subs (subs);
		add_part (prog, g_parts, "(the user controls)", pg.str ());
	}
	for (int i = 0; i < g_p.docs.n; i++)
	{
		Doc &d = g_p.docs[i];
		if (d.type != DOC_FORM) continue;
		regenerate (d);
		add_part (prog, g_parts, g_p.docs[d.gen].file, ed_text (g_p.docs[d.gen].ed));
	}
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_CODE) add_part (prog, g_parts, g_p.docs[i].file, ed_text (g_p.docs[i].ed));
	Doc *mf = start_form ();
	char fn[64] = ""; if (mf) form_name_of (*mf, fn, sizeof fn);
	Str st; generate_start (st, fn, ucs);
	add_part (prog, g_parts, "(the start)", st.str ());
	free_subs (ucs);
	return true;
}
static void show_line (int d, int at)
{
	Doc &D0 = g_p.docs[d];
	ui_textarea_goto (D0.ed, line_offset (ed_text (D0.ed), at > 0 ? at - 1 : 0));
	ui_textarea_highlight (D0.ed, at - 1);
}
// Le programme compilé : sa première erreur montrée (dans son fichier, à sa ligne) ; true : aucune
static bool compile_check (bas::Program **keep, bool quiet)
{
	Str prog;
	if (!build (prog, quiet)) return false;
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type != DOC_FORM) { ui_textarea_highlight (g_p.docs[i].ed, -1); drop_problems (g_p.docs[i].file); }
	for (int i = g_probs.n - 1; i >= 0; i--) if (g_probs[i].file[0] == '(') g_probs.erase (i);
	bas::Error err;
	char *l1 = latin1_dup (prog.str ());
	bas::Program *p = bas::compile (l1, &err);
	delete [] l1;
	if (!p)
	{
		const char *file = "?"; int at = 0;
		part_of (g_parts, err.line, &file, &at);
		add_problem (0, file, at, err.msg);
		int d = find_doc (file);
		if (d >= 0) ui_textarea_highlight (g_p.docs[d].ed, at - 1);
		refresh_msgs ();
		if (!quiet && d >= 0) { show_doc (d); show_line (d, at); ui_w_focus (g_p.docs[d].ed); }
		char m[200]; snprintf (m, sizeof m, "%s %d: %s", file, at, err.msg); status (m);
		return false;
	}
	refresh_msgs ();
	if (keep) *keep = p; else bas::destroy (p);
	return true;
}
static void check_program (bool quiet) { compile_check (0, quiet); }

// ---- le code : les SUB trouvées, écrites --------------------------------------------------------------------------------
// La ligne (0 d'abord) de « SUB nom » dans un texte, sinon -1
static int sub_line (const char *t, const char *name)
{
	int ln = 0; int L = (int) strlen (name);
	for (const char *p = t; *p; )
	{
		const char *q = p; while (*q == ' ' || *q == '\t') q++;
		int kw = !strncasecmp (q, "SUB ", 4) ? 4 : !strncasecmp (q, "FUNCTION ", 9) ? 9 : 0;
		if (kw) { q += kw; while (*q == ' ') q++; if (!strncasecmp (q, name, L) && !name_ch (q[L]) && q[L] != '.') return ln; }
		while (*p && *p != '\n') p++;
		if (*p) p++;
		ln++;
	}
	return -1;
}
static void open_sub (const char *name)
{
	for (int i = 0; i < g_p.docs.n; i++)
	{
		if (g_p.docs[i].type != DOC_CODE) continue;
		int ln = sub_line (ed_text (g_p.docs[i].ed), name);
		if (ln >= 0) { show_doc (i); ui_textarea_goto (g_p.docs[i].ed, line_offset (ed_text (g_p.docs[i].ed), ln + 1)); ui_w_focus (g_p.docs[i].ed); return; }
	}
	Doc *c = code_for_form (); if (!c) return;
	int i = (int) (c - &g_p.docs[0]);
	// écrite à la fin : SUB nom / (le curseur) / END SUB
	long ed = c->ed;
	long len = ui_w_text_len (ed);
	Str t; t.printf ("%sSUB %s\n  ", len && ed_text (ed)[len - 1] == '\n' ? "\n" : "\n\n", name);
	int caret = (int) len + t.n;
	t.puts ("\nEND SUB\n");
	show_doc (i);
	ui_textarea_goto (ed, len);
	ui_textarea_insert (ed, (long) t.str ());
	ui_textarea_goto (ed, caret);
	set_dirty (g_p.docs[i]);
	ui_w_focus (ed);
	refresh_outline ();
	g_checkAt = bas_ms () + 300;
	char m[120]; snprintf (m, sizeof m, "SUB %s %s", name, TR ("written")); status (m);
}

// Les kits qu'importe le projet : « #import <kit> » en début de ligne -- et UIKit, que la bibliothèque des contrôles importe.
struct KitName { char name[32]; };
static void imported_kits (Vec<KitName> &out)
{
	KitName u; cpy (u.name, "UIKit", sizeof u.name); out.push (u);
	for (int i = 0; i < g_p.docs.n; i++)
	{
		if (g_p.docs[i].type != DOC_CODE) continue;
		for (const char *p = ed_text (g_p.docs[i].ed); p && *p; )
		{
			while (*p == ' ' || *p == '\t') p++;
			if (!strncasecmp (p, "#import", 7) && (p[7] == ' ' || p[7] == '\t'))
			{
				p += 7; while (*p == ' ' || *p == '\t') p++;
				KitName k; int n = 0;
				while (name_ch (*p) && n < 31) k.name[n++] = *p++;
				k.name[n] = 0;
				bool known = false;
				for (int j = 0; j < out.n; j++) if (ieq (out[j].name, k.name)) { known = true; if (j) out[j] = k; }
				if (n && !known) out.push (k);
			}
			while (*p && *p != '\n') p++;
			if (*p) p++;
		}
	}
}
// Les fonctions et structures d'un kit (sa description, SD0:/lib/<kit>.bi : ce que BASIC lit lui-même), pour la liste
static void kit_members (const char *kit, Vec<Compl> &out)
{
	char low[32]; int n = 0;
	for (; kit[n] && n < 31; n++) low[n] = (char) (kit[n] >= 'A' && kit[n] <= 'Z' ? kit[n] + 32 : kit[n]);
	low[n] = 0;
	int len = 0;
	char *bi = bas_kit_source (low, &len);
	if (!bi) return;
	for (char *l = bi; l && *l; )
	{
		char *e = strchr (l, '\n'); if (e) *e = 0;
		char w[6][160]; int nw = 0;
		for (char *p = l; *p && nw < 6; )
		{
			while (*p == ' ' || *p == '\t' || *p == '\r') p++;
			if (!*p) break;
			int k = 0; while (*p && *p != ' ' && *p != '\t' && *p != '\r') { if (k < 159) w[nw][k++] = *p; p++; }
			w[nw++][k] = 0;
		}
		Compl c; c.detail[0] = 0;
		if (nw >= 3 && !strcmp (w[0], "struct") && !(w[1][0] >= '0' && w[1][0] <= '9'))
		{ cpy (c.name, w[1], sizeof c.name); c.kind = 't'; snprintf (c.detail, sizeof c.detail, "TYPE, %s bytes", w[2]); out.push (c); }
		else if (nw >= 4 && w[0][0] != '#' && strcmp (w[0], "field") && w[1][0] >= '0' && w[1][0] <= '9')
		{
			cpy (c.name, w[0], sizeof c.name); c.kind = 'f';
			Str d; d.puts ("(");
			const char *args = strcmp (w[3], "-") ? w[3] : "", *nm = nw > 5 ? w[5] : "";
			for (int i = 0; args[i]; i++)
			{
				char one[48]; int k = 0;
				while (*nm && *nm != ',' && k < 47) one[k++] = *nm++;
				one[k] = 0; if (*nm == ',') nm++;
				if (!one[0]) snprintf (one, sizeof one, "arg%d", i + 1);
				if (i) d.puts (", ");
				char t = args[i];
				if (t == 's') d.printf ("%s$", one);
				else if (t == 'c') d.printf ("ADDRESSOF %s", one);
				else if (t == 'I' || t == 'L' || t == 'F' || t == 'D') d.printf ("BYREF %s", one);
				else d.puts (one);
			}
			d.puts (")");
			if (w[2][0] == 's') d.puts (" -> text$"); else if (w[2][0] == 'l') d.puts (" -> handle"); else if (w[2][0] != 'v') d.puts (" -> number");
			cpy (c.detail, d.str (), sizeof c.detail);
			out.push (c);
		}
		l = e ? e + 1 : 0;
	}
	delete [] bi;
	// par nom (la description suit l'ordre de la table du kit)
	for (int i = 1; i < out.n; i++)
	{
		Compl c = out[i]; int j = i;
		while (j > 0 && strcasecmp (out[j - 1].name, c.name) > 0) { out[j] = out[j - 1]; j--; }
		out[j] = c;
	}
}
// La complétion : les propriétés et méthodes d'un contrôle, les fonctions d'un kit, les noms
static void complete (const char *obj, Vec<Compl> &out)
{
	auto add = [&] (const char *n, char k, const char *d) { Compl c; cpy (c.name, n, sizeof c.name); c.kind = k; cpy (c.detail, d, sizeof c.detail); out.push (c); };
	Vec<KitName> kits; imported_kits (kits);
	if (obj[0]) for (int i = 0; i < kits.n; i++) if (ieq (obj, kits[i].name)) { kit_members (kits[i].name, out); return; }
	if (obj[0])
	{
		// l'objet d'un formulaire (la fenêtre ; un contrôle utilisateur), un contrôle d'un formulaire
		El *e = 0;
		for (int i = 0; i < g_p.docs.n && !e; i++)
		{
			Doc &d = g_p.docs[i];
			if (d.type != DOC_FORM || !d.form) continue;
			char n[64]; form_name_of (d, n, sizeof n);
			if (ieq (obj, n))
			{
				add ("Width", 'p', "INTEGER"); add ("Height", 'p', "INTEGER");
				if (d.form->userControl ()) add ("Visible", 'p', "-1: a Host shows it"); else add ("Close", 'm', "");
				return;
			}
			e = d.form->named (obj);
		}
		if (!e) return;
		if (e->kind == K_HOST)
		{
			add ("Content", 'p', "a user control (NOTHING: none)"); add ("Load", 'm', "a user control"); add ("Unload", 'm', "");
			add ("Enabled", 'p', "-1 / 0"); add ("Visible", 'p', "-1 / 0"); add ("Move", 'm', "x, y, w, h"); add ("handle", 'p', "UIKit's panel");
			return;
		}
		add ("Text", 'p', "STRING");
		if (e->kind == K_SLIDER || e->kind == K_PROGRESS || e->kind == K_LISTBOX || e->kind == K_DROPDOWN) add ("Value", 'p', "DOUBLE");
		if (e->kind == K_CHECKBOX) add ("Checked", 'p', "-1 / 0");
		add ("Enabled", 'p', "-1 / 0"); add ("Visible", 'p', "-1 / 0");
		add ("Focus", 'm', ""); add ("Move", 'm', "x, y, w, h");
		if (e->kind == K_LISTBOX || e->kind == K_DROPDOWN) { add ("AddItem", 'm', "s$"); add ("Count", 'p', "INTEGER"); }
		if (e->kind == K_LISTBOX) add ("Clear", 'm', "");
		add ("handle", 'p', "UIKit's widget");
		return;
	}
	for (int i = 0; i < kits.n; i++) add (kits[i].name, 'K', "kit");
	for (int i = 0; i < g_p.docs.n; i++)			// chaque formulaire : son objet, ses contrôles
	{
		Doc &d = g_p.docs[i];
		if (d.type != DOC_FORM || !d.form) continue;
		char n[64]; form_name_of (d, n, sizeof n);
		add (n, 'c', d.form->userControl () ? "User control" : "Window");
		struct Wk { static void walk (El *e, Vec<Compl> &o) {
			if (e->name[0] && e->kind != K_WINDOW) { Compl c; cpy (c.name, e->name, sizeof c.name); c.kind = 'c'; cpy (c.detail, e->kind == K_MENUITEM ? "Menu item" : KIND_NAMES[e->kind], sizeof c.detail); o.push (c); }
			for (int i = 0; i < e->kids.n; i++) walk (e->kids[i], o); } };
		Wk::walk (d.form->root, out);
	}
	Vec<char *> subs; all_subs (subs);
	for (int i = 0; i < subs.n; i++) add (subs[i], 's', "SUB");
	free_subs (subs);
	for (int i = 0; i < g_nwords; i++) { char w[40]; int l = imin (g_wlen[i], 39); memcpy (w, g_word[i], l); w[l] = 0; add (w, 'k', ""); }
}

// ---- les vues -----------------------------------------------------------------------------------------------------------
struct TreeRow { long id; int doc; };
static Vec<TreeRow> g_treeRows;
static void refresh_tree ()
{
	ui_tree_clear (g_tree); g_treeRows.clear ();
	long root = ui_tree_add (g_tree, -1, (long) (g_p.title[0] ? g_p.title : g_p.name), 0);
	long forms = ui_tree_add (g_tree, root, (long) TR ("Forms"), 0), code = ui_tree_add (g_tree, root, (long) TR ("Code"), 0);
	long gen = ui_tree_add (g_tree, root, (long) TR ("Generated"), 0), res = ui_tree_add (g_tree, root, (long) TR ("Resources"), 0);
	for (int i = 0; i < g_p.docs.n; i++)
	{
		long par = g_p.docs[i].type == DOC_FORM ? forms : g_p.docs[i].type == DOC_CODE ? code : g_p.docs[i].type == DOC_GEN ? gen : res;
		TreeRow r; r.id = ui_tree_add (g_tree, par, (long) g_p.docs[i].file, i + 1); r.doc = i;
		g_treeRows.push (r);
	}
	ui_tree_add (g_tree, res, (long) "project.ini", 0);
	long open[5] = { root, forms, code, gen, res };
	for (int i = 0; i < 5; i++) ui_tree_expand (g_tree, open[i], 1);
}
static void tree_pick ()
{
	long id = ui_w_get_value (g_tree);
	for (int i = 0; i < g_treeRows.n; i++) if (g_treeRows[i].id == id && g_treeRows[i].doc != g_p.cur) { show_doc (g_treeRows[i].doc); return; }
}
// Le plan : les SUB du code ; un clic y va
static Vec<int> g_outLines;
static void refresh_outline ()
{
	Doc *d = cur_doc ();
	ui_list_clear (g_outline); g_outLines.clear ();
	if (!d || d->type == DOC_FORM) return;
	const char *t = ed_text (d->ed); int ln = 0;
	for (const char *p = t; *p; )
	{
		const char *q = p; while (*q == ' ' || *q == '\t') q++;
		if (!strncasecmp (q, "SUB ", 4) || !strncasecmp (q, "FUNCTION ", 9) || !strncasecmp (q, "PROPERTY ", 9) || !strncasecmp (q, "TYPE ", 5)
		    || !strncasecmp (q, "CLASS ", 6) || !strncasecmp (q, "INTERFACE ", 10) || !strncasecmp (q, "VIRTUAL ", 8) || !strncasecmp (q, "OVERRIDE ", 9) || !strncasecmp (q, "ABSTRACT ", 9))
		{
			char s[64]; int k = 0; while (q[k] && q[k] != '\n' && q[k] != '(' && k < 60) { s[k] = q[k]; k++; }
			s[k] = 0; ui_list_add (g_outline, (long) s, 0); g_outLines.push (ln);
		}
		while (*p && *p != '\n') p++;
		if (*p) p++;
		ln++;
	}
}
static void outline_pick ()
{
	Doc *d = cur_doc (); int i = (int) ui_w_get_value (g_outline);
	if (!d || i < 0 || i >= g_outLines.n) return;
	ui_textarea_goto (d->ed, line_offset (ed_text (d->ed), g_outLines[i]));
	ui_w_focus (d->ed);
}
// Les listes d'objet et d'événement au-dessus du code. (L'élément 0 des événements dit quoi faire : choisir le même
// événement deux fois doit écrire ou montrer sa SUB deux fois.)
static Vec<char *> g_objNames; static char g_evNames[4][64]; static int g_nev;
static void refresh_events ()
{
	int o = (int) ui_w_get_value (g_objBox);
	Doc *f = main_form ();
	const char *labels[4]; g_nev = 0;
	if (f && f->form && o >= 0 && o < g_objNames.n)
	{
		El *e = o == 0 ? f->form->root : f->form->named (g_objNames[o]);
		if (e) g_nev = events_of (e, g_evNames, labels, 4);
	}
	ui_list_clear (g_evBox);
	ui_list_add (g_evBox, (long) (g_nev ? TR ("(its event: its SUB)") : TR ("(no events)")), 0);
	for (int i = 0; i < g_nev; i++) ui_list_add (g_evBox, (long) labels[i], 0);
	ui_w_set_value (g_evBox, 0);
}
static void refresh_codebar ()
{
	for (int i = 0; i < g_objNames.n; i++) free (g_objNames[i]);
	g_objNames.clear ();
	Doc *f = main_form ();
	if (f && f->form)
	{
		char fn[64]; form_name_of (*f, fn, sizeof fn);
		g_objNames.push (strdup (fn));
		struct Wk { static void walk (El *e, Vec<char *> &o) { if (e->name[0] && e->kind != K_WINDOW && event_of (e->kind)) o.push (strdup (e->name)); for (int i = 0; i < e->kids.n; i++) walk (e->kids[i], o); } };
		Wk::walk (f->form->root, g_objNames);
	}
	int keep = (int) ui_w_get_value (g_objBox);
	ui_list_clear (g_objBox);
	for (int i = 0; i < g_objNames.n; i++) ui_list_add (g_objBox, (long) g_objNames[i], 0);
	ui_w_set_value (g_objBox, keep >= 0 && keep < g_objNames.n ? keep : 0);
	refresh_events ();
}
static void ev_picked ()
{
	int i = (int) ui_w_get_value (g_evBox) - 1;
	Doc *f = main_form ();
	ui_w_set_value (g_evBox, 0);
	if (f && f->form && i >= 0 && i < g_nev) open_sub (g_evNames[i]);
}
static void refresh_tabs ()
{
	ui_list_clear (g_tabs);
	int cur = -1;
	for (int i = 0; i < g_p.tabs.n; i++)
	{
		Doc &d = g_p.docs[g_p.tabs[i]];
		char t[80]; snprintf (t, sizeof t, "%s%s", d.file, d.dirty ? " *" : "");
		ui_tabstrip_add (g_tabs, (long) t, g_p.tabs[i]);
		if (g_p.tabs[i] == g_p.cur) cur = i;
	}
	ui_w_set_value (g_tabs, cur);
}
static void show_doc (int i)
{
	if (i < 0 || i >= g_p.docs.n) return;
	pg_commit ();
	if (P.pend) { P.pend = false; form_edited (); }
	Doc &d = g_p.docs[i];
	if (!d.open) { d.open = true; g_p.tabs.push (i); }
	g_p.cur = i;
	bool form = d.type == DOC_FORM;
	for (int k = 0; k < g_p.docs.n; k++) ui_w_show (g_p.docs[k].ed, k == i && !(form && g_view == 0));
	if (form) g_lastForm = i;
	{ static char fname[64]; Doc *mf = main_form (); if (mf) { form_name_of (*mf, fname, sizeof fname); g_formName = fname; } }	// (les SUB des événements : <Form>_Load...)
	ui_w_show (D.view, form && g_view != 2);
	ui_w_show (T.view, form); ui_w_show (g_outline, !form);
	ui_w_show (g_objBox, !form); ui_w_show (g_evBox, !form);
	ui_w_set_text (g_hTools, (long) (form ? TR ("Toolbox") : TR ("Outline")));
	if (form)
	{
		g_reading = true;
		des_set_form (d.form);
		El *s = d.form && d.selN ? el_by_path (*d.form, d.selPath, d.selN) : d.form ? d.form->root : 0;
		des_select (s, false); pg_show (s);
		g_reading = false;
	}
	else { des_set_form (0); pg_show (0); refresh_codebar (); }
	refresh_outline ();
	refresh_tabs ();
	ui_w_set_text (g_stLeft, (long) d.file);
	relayout ();
	if (!form || g_view == 2) ui_w_focus (d.ed); else ui_w_focus (D.view);
}
static void set_view (int v) { g_view = v; ui_w_set_value (g_seg, v); }
static void view_changed ()
{
	g_view = (int) ui_w_get_value (g_seg);
	Doc *d = cur_doc ();
	if (d && d->type != DOC_FORM && g_view != 2) { Doc *f = main_form (); if (f) { show_doc ((int) (f - &g_p.docs[0])); return; } }
	if (d) show_doc (g_p.cur);
}
// Le concepteur remplace le texte (tout effacé, puis écrit) : deux opérations pour le journal de UIKit. Annuler ou rétablir
// qui tombe entre les deux va jusqu'au bout : un formulaire n'est jamais vide.
static void form_whole (Doc &d, int op) { if (d.type == DOC_FORM && !ed_text (d.ed)[0]) ui_textarea_edit (d.ed, op); }
// Un éditeur a changé (texte tapé, annulé)
static void text_changed (Doc &d)
{
	if (d.type == DOC_GEN) return;
	set_dirty (d);
	if (d.type == DOC_FORM)
	{
		if (!g_reading && D.sel) d.selN = path_of (D.sel, d.selPath, 32);
		read_form (d);
	}
	else { ui_textarea_highlight (d.ed, -1); g_checkAt = bas_ms () + 600; }	// (vérifié quand la frappe s'arrête)
}
static void complete_here (bool dotOnly)
{
	Doc *d = cur_doc ();
	if (!d || d->type == DOC_GEN || d->type == DOC_FORM) return;
	long col = 0; uikit_textarea_caret (d->ed, &col);
	int x = g_edX + 60 + (int) col * (ui_px () - 5), y = g_edY + 8;	// (l'éditeur ne dit pas où est son curseur à l'écran)
	if (qs_complete_at (d->ed, imin (x, (int) ui.w - 260), y, dotOnly)) text_changed (*d);
}
// Le curseur : la ligne et la colonne ; dans le texte d'un formulaire, l'élément de sa ligne choisi
static long g_lastLine = -1, g_lastCol = -1;
static void caret_watch ()
{
	Doc *d = cur_doc (); if (!d) return;
	long col = 0, line = uikit_textarea_caret (d->ed, &col);
	if (line == g_lastLine && col == g_lastCol) return;
	g_lastLine = line; g_lastCol = col;
	char t[48]; snprintf (t, sizeof t, "Ln %ld, Col %ld", line + 1, col + 1);
	ui_w_set_text (g_stRight, (long) t);
	if (d->type == DOC_FORM && d->form && !g_reading && ui_w_get_focus () == d->ed)
	{
		El *e = el_at_line (d->form->root, (int) line + 1);
		if (e && e != D.sel) { g_reading = true; des_select (e, false); pg_show (e); d->selN = path_of (e, d->selPath, 32); g_reading = false; }
	}
}

// ---- le projet : lu, écrit --------------------------------------------------------------------------------------------
static void ini_get (const char *ini, const char *key, char *out, int cap)
{
	int L = (int) strlen (key);
	for (const char *p = ini; *p; )
	{
		while (*p == ' ' || *p == '\t') p++;
		if (!strncasecmp (p, key, L) && (p[L] == ' ' || p[L] == '='))
		{
			const char *v = p + L; while (*v == ' ' || *v == '=') v++;
			int k = 0; while (v[k] && v[k] != '\n' && v[k] != '\r' && k < cap - 1) { out[k] = v[k]; k++; }
			while (k > 0 && out[k - 1] == ' ') k--;
			out[k] = 0; return;
		}
		while (*p && *p != '\n') p++;
		if (*p) p++;
	}
}
static void clear_project ()
{
	pg_drop_edit ();
	des_set_form (0); pg_show (0);
	for (int i = 0; i < g_p.docs.n; i++) { ui_w_destroy (g_p.docs[i].ed); delete g_p.docs[i].form; }
	g_p.docs.clear (); g_p.tabs.clear (); g_p.cur = -1; g_lastForm = -1;
	g_probs.clear (); refresh_msgs ();
}
static bool load_project (const char *dir)
{
	char ip[260]; join (ip, sizeof ip, dir, "project.ini");
	char *ini = read_file (ip);
	if (!ini) { char m[300]; snprintf (m, sizeof m, "%s %s", TR ("No project.ini in"), dir); status (m); return false; }
	clear_project ();
	cpy (g_p.dir, dir, sizeof g_p.dir);
	{ int n = (int) strlen (g_p.dir); if (n > 1 && g_p.dir[n - 1] == '/') g_p.dir[n - 1] = 0; }
	g_p.name[0] = g_p.title[0] = g_p.mainForm[0] = g_p.category[0] = 0;
	ini_get (ini, "name", g_p.name, sizeof g_p.name); if (!g_p.name[0]) cpy (g_p.name, base_name (g_p.dir), sizeof g_p.name);
	ini_get (ini, "title", g_p.title, sizeof g_p.title); if (!g_p.title[0]) cpy (g_p.title, g_p.name, sizeof g_p.title);
	ini_get (ini, "main", g_p.mainForm, sizeof g_p.mainForm); if (!g_p.mainForm[0]) cpy (g_p.mainForm, "Main", sizeof g_p.mainForm);
	ini_get (ini, "category", g_p.category, sizeof g_p.category); if (!g_p.category[0]) cpy (g_p.category, "Développement", sizeof g_p.category);
	char mg[16] = "no"; ini_get (ini, "managed", mg, sizeof mg); g_p.managed = ieq (mg, "yes");
	char files[512] = ""; ini_get (ini, "files", files, sizeof files);
	delete [] ini;
	// les fichiers (un formulaire, son code ; puis le reste)
	for (char *p = files; *p; )
	{
		while (*p == ' ') p++;
		char f[64]; int k = 0; while (*p && *p != ' ' && k < 63) f[k++] = *p++;
		f[k] = 0; if (!k) break;
		char fp[300]; join (fp, sizeof fp, g_p.dir, f);
		char *t = read_file (fp);
		int type = ends_with (f, ".form") ? DOC_FORM : ends_with (f, ".bas") ? DOC_CODE : DOC_TEXT;
		add_doc (f, type, t ? t : "");
		delete [] t;
	}
	// chaque formulaire : son code généré (un document en lecture seule), son arbre
	int n = g_p.docs.n;
	for (int i = 0; i < n; i++)
	{
		if (g_p.docs[i].type != DOC_FORM) continue;
		char gn[80]; snprintf (gn, sizeof gn, "%s.bas", g_p.docs[i].file);
		add_doc (gn, DOC_GEN, "");
		g_p.docs[i].gen = g_p.docs.n - 1;
	}
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_FORM) read_form (g_p.docs[i]);
	for (int i = 0; i < g_p.docs.n; i++) g_p.docs[i].dirty = false;
	refresh_tree ();
	Doc *mf = main_form ();
	show_doc (mf ? (int) (mf - &g_p.docs[0]) : 0);
	Doc *cf = code_for_form (); if (cf && !cf->open) { cf->open = true; g_p.tabs.push ((int) (cf - &g_p.docs[0])); refresh_tabs (); }
	save_settings ();
	check_program (true);
	char t[100]; snprintf (t, sizeof t, "QBStudio - %s", g_p.title); ui_title ((long) t);
	status (TR ("Project opened"));
	return true;
}
static void save_project_ini ()
{
	Str s;
	s.puts ("# a QBStudio project: its window, its code, what Make App writes\n[project]\n");
	s.printf ("name = %s\ntitle = %s\nmain = %s\nfiles =", g_p.name, g_p.title, g_p.mainForm);
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type != DOC_GEN) s.printf (" %s", g_p.docs[i].file);
	s.printf ("\ncategory = %s\ncompiled = yes\nmanaged = %s\nstandalone = yes\n", g_p.category, g_p.managed ? "yes" : "no");
	char p[260]; join (p, sizeof p, g_p.dir, "project.ini");
	write_file (p, s.str ());
}
static bool save_all ()
{
	if (!g_p.dir[0]) return false;
	bool ok = true;
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_FORM) regenerate (g_p.docs[i]);
	for (int i = 0; i < g_p.docs.n; i++)
	{
		Doc &d = g_p.docs[i];
		if (!d.dirty && d.type != DOC_GEN) continue;
		char p[300]; join (p, sizeof p, g_p.dir, d.file);
		if (!write_file (p, ed_text (d.ed))) ok = false; else d.dirty = false;
	}
	save_project_ini ();
	refresh_tabs ();
	status (ok ? TR ("Saved") : TR ("Some files could not be written"));
	return ok;
}
static bool changed () { for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].dirty) return true; return false; }
static bool confirm_close ()
{
	if (!changed ()) return true;
	long r = ui_message ((long) "QBStudio", (long) TR ("Save the project's changes?"), UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) save_all ();
	return true;
}

// Un nouveau projet : son dossier, sa fenêtre d'après un modèle
static const char *const T_WINDOW_FORM =
"# Main.form -- the window (QBStudio's designer writes it; it reads as you see it)\n"
"Window Main \"%s\" size=360x220 resizable\n"
"  Column padding=14 gap=10\n"
"    Label \"Your name:\"\n"
"    TextBox who \"\" fill\n"
"    Label hello \"\" fill\n"
"    Spacer\n"
"    Row align=right\n"
"      Button greet \"Say hello\" default\n";
static const char *const T_WINDOW_CODE =
"' Main.bas -- what the app does (its window: Main.form)\n\n"
"SUB Main_Load\n  who.Focus\nEND SUB\n\n"
"SUB greet_Click\n  hello.Text = \"Hello, \" + who.Text + \"!\"\nEND SUB\n";
static const char *const T_DOC_FORM =
"# Main.form -- the window (QBStudio's designer writes it; it reads as you see it)\n"
"Window Main \"%s\" size=520x360 min=360x240 resizable\n"
"  Menu\n"
"    \"&File\"\n"
"      \"&New\" name=mnuNew key=Ctrl+N\n"
"      \"&Open...\" name=mnuOpen key=Ctrl+O\n"
"      \"&Save\" name=mnuSave key=Ctrl+S\n"
"      -\n"
"      \"&Quit\" name=mnuQuit key=Ctrl+Q\n"
"  Column padding=10 gap=8\n"
"    Row gap=8\n"
"      TextBox line \"\" fill\n"
"      Button add \"Add\" default\n"
"    ListBox items fill grow=1\n"
"  StatusBar status \"Ready\"\n";
static const char *const T_DOC_CODE =
"' Main.bas -- a list kept in a file: New, Open, Save (its window: Main.form)\n\n"
"DIM SHARED path$\n\n"
"SUB Main_Load\n  line.Focus\nEND SUB\n\n"
"SUB add_Click\n  IF line.Text <> \"\" THEN items.AddItem line.Text: line.Text = \"\"\n  line.Focus\nEND SUB\n\n"
"SUB mnuNew_Click\n  r = MSGBOX(\"New\", \"Start a new list?\", 1)\nEND SUB\n\n"
"SUB mnuOpen_Click\n  f$ = OPENFILE$(\"SD:/home/documents\")\n  IF f$ = \"\" THEN EXIT SUB\n  path$ = f$\n"
"  OPEN f$ FOR INPUT AS #1\n  DO WHILE NOT EOF(1)\n    LINE INPUT #1, a$\n    items.AddItem a$\n  LOOP\n  CLOSE #1\n  status.Text = f$\nEND SUB\n\n"
"SUB mnuSave_Click\n  IF path$ = \"\" THEN path$ = SAVEFILE$(\"SD:/home/documents\", \"list.txt\")\n  IF path$ = \"\" THEN EXIT SUB\n"
"  status.Text = \"Saved: \" + path$\nEND SUB\n\n"
"SUB mnuQuit_Click\n  Main.Close\nEND SUB\n";
static const char *const T_EMPTY_FORM =
"# Main.form -- the window (QBStudio's designer writes it; it reads as you see it)\n"
"Window Main \"%s\" size=400x280 resizable\n"
"  Column padding=12 gap=8\n";
static const char *const T_EMPTY_CODE = "' Main.bas -- what the app does (its window: Main.form)\n\nSUB Main_Load\nEND SUB\n";

static void cmd_new ()
{
	enum { DW = 460, DH = 270 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("New Project"));
	int y = 14, h = ctl_h ();
	ui_label (16, y, 130, h, (long) TR ("Folder name"), 0);
	long eName = ui_textbox (150, y, 290, h, 0); ui_w_set_text (eName, (long) "myapp");
	y += h + 8;
	ui_label (16, y, 130, h, (long) TR ("Title"), 0);
	long eTitle = ui_textbox (150, y, 290, h, 0); ui_w_set_text (eTitle, (long) "My App");
	y += h + 12;
	ui_label (16, y, 400, h, (long) TR ("Start from:"), 0);
	y += h;
	static const char *const L[3] = { "A window (controls in a column)", "A document app (menu, list, status bar)", "An empty window" };
	long r[3];
	for (int i = 0; i < 3; i++) { r[i] = ui_radio (24, y, 410, 24, (long) TR (L[i]), 1); y += 26; }
	ui_w_set_value (r[0], 1);
	long x = ui_dialog_button (d, 0, (long) TR ("Create"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	ui_w_focus (eName);
	long ok = ui_dialog_run (d, 0, 0);
	char name[40], title[64]; cpy (name, (const char *) ui_w_get_text (eName), sizeof name); cpy (title, (const char *) ui_w_get_text (eTitle), sizeof title);
	int k = ui_w_get_value (r[1]) ? 1 : ui_w_get_value (r[2]) ? 2 : 0;
	ui_dialog_end (d);
	if (ok != 1 || !name[0]) return;
	for (int i = 0; name[i]; i++) if (!name_ch (name[i])) { ui_message ((long) TR ("New Project"), (long) TR ("The folder's name: letters, digits and _ only"), UM_OK); return; }
	if (!confirm_close ()) return;
	char dir[200]; snprintf (dir, sizeof dir, PROJECTS "/%s", name);
	char ip[260]; join (ip, sizeof ip, dir, "project.ini");
	if (file_exists (ip)) { ui_message ((long) TR ("New Project"), (long) TR ("There is a project of that name already"), UM_OK); return; }
	mkdir ("SD0:/home", 0755); mkdir (PROJECTS, 0755); mkdir (dir, 0755);
	const char *form = k == 0 ? T_WINDOW_FORM : k == 1 ? T_DOC_FORM : T_EMPTY_FORM;
	const char *code = k == 0 ? T_WINDOW_CODE : k == 1 ? T_DOC_CODE : T_EMPTY_CODE;
	char ft[2048]; snprintf (ft, sizeof ft, form, title);
	char p[260];
	join (p, sizeof p, dir, "Main.form"); write_file (p, ft);
	join (p, sizeof p, dir, "Main.bas"); write_file (p, code);
	Str ini; ini.printf ("# a QBStudio project: its window, its code, what Make App writes\n[project]\nname = %s\ntitle = %s\nmain = Main\nfiles = Main.form Main.bas\ncategory = Développement\ncompiled = yes\n", name, title);
	write_file (ip, ini.str ());
	load_project (dir);
}
static void cmd_open ()
{
	char p[256];
	ui_file_filter ((long) "Project files|*.ini;*.bas;*.form|All files|*");
	long ok = ui_file_open ((long) PROJECTS, (long) p, sizeof p);
	ui_file_filter (0);
	if (!ok || !confirm_close ()) return;
	// le projet : le dossier du fichier choisi
	char dir[256]; cpy (dir, p, sizeof dir);
	char *b = (char *) base_name (dir); if (b > dir) b[-1] = 0;
	load_project (dir);
}
static bool ask_line (const char *title, const char *q, char *out, int cap) { return ui_ask ((long) title, (long) q, (long) out, cap) != 0; }
static void cmd_add_module ()
{
	if (!g_p.dir[0]) return;
	char n[40] = "Module1";
	if (!ask_line (TR ("Add Module"), TR ("The module's name (its file: <name>.bas)"), n, sizeof n)) return;
	for (int i = 0; n[i]; i++) if (!name_ch (n[i])) { ui_message ((long) TR ("Add Module"), (long) TR ("Letters, digits and _ only"), UM_OK); return; }
	char f[64]; snprintf (f, sizeof f, "%s.bas", n);
	if (find_doc (f) >= 0) return;
	char t[200]; snprintf (t, sizeof t, "' %s -- (a module of %s: its SUBs and FUNCTIONs)\n\n", f, g_p.title);
	Doc &d = add_doc (f, DOC_CODE, t); d.dirty = true;
	refresh_tree ();
	show_doc (g_p.docs.n - 1);
	save_all ();
}
// Un contrôle utilisateur : un panneau de contrôles dessiné comme une fenêtre, montré dans un Host -- <Nom>.form (sa racine :
// UserControl) et <Nom>.bas pour ses événements
static void cmd_add_usercontrol ()
{
	if (!g_p.dir[0]) return;
	char n[40] = "Panel1";
	const char *ttl = TR ("Add User Control");
	if (!ask_line (ttl, TR ("Its name (the object in the code; its files: <name>.form, <name>.bas)"), n, sizeof n)) return;
	for (int i = 0; n[i]; i++) if (!name_ch (n[i])) { ui_message ((long) ttl, (long) TR ("Letters, digits and _ only"), UM_OK); return; }
	if (is_word0 (n)) { ui_message ((long) ttl, (long) TR ("That is a word of BASIC: another name, please"), UM_OK); return; }
	char ff[64], fc[64], fg[64]; snprintf (ff, sizeof ff, "%s.form", n); snprintf (fc, sizeof fc, "%s.bas", n); snprintf (fg, sizeof fg, "%s.form.bas", n);
	if (find_doc (ff) >= 0 || find_doc (fc) >= 0) { ui_message ((long) ttl, (long) TR ("The project has files of that name already"), UM_OK); return; }
	char t[400];
	snprintf (t, sizeof t, "# %s -- a user control: a panel of controls, shown in a Host (host.Content = %s)\nUserControl %s size=320x200\n  Column padding=12 gap=8\n    Label \"%s\"\n", ff, n, n, n);
	add_doc (ff, DOC_FORM, t); int fi = g_p.docs.n - 1; g_p.docs[fi].dirty = true;
	add_doc (fg, DOC_GEN, ""); g_p.docs[fi].gen = g_p.docs.n - 1;
	snprintf (t, sizeof t, "' %s -- what the user control %s does (its controls: %s)\n\nSUB %s_Load\nEND SUB\n", fc, n, ff, n);
	add_doc (fc, DOC_CODE, t); g_p.docs[g_p.docs.n - 1].dirty = true;
	read_form (g_p.docs[fi]);
	refresh_tree ();
	show_doc (fi);
	save_all ();
	status (TR ("User control added: put a Host in the window, then host.Content = its name"));
}
static void cmd_project_settings ()
{
	if (!g_p.dir[0]) return;
	enum { DW = 460, DH = 170 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("The project"));
	int y = 14, h = ctl_h ();
	ui_label (16, y, 130, h, (long) TR ("Title"), 0);
	long eTitle = ui_textbox (150, y, 290, h, 0); ui_w_set_text (eTitle, (long) g_p.title);
	y += h + 8;
	ui_label (16, y, 130, h, (long) TR ("Category"), 0);
	long eCat = ui_textbox (150, y, 290, h, 0); ui_w_set_text (eCat, (long) g_p.category);
	y += h + 8;
	long cMan = ui_checkbox (16, y, DW - 32, 24, (long) TR ("Managed (runs on the VM instead of machine code)"), g_p.managed);
	long x = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	long ok = ui_dialog_run (d, 0, 0);
	if (ok == 1)
	{
		cpy (g_p.title, (const char *) ui_w_get_text (eTitle), sizeof g_p.title);
		cpy (g_p.category, (const char *) ui_w_get_text (eCat), sizeof g_p.category);
		g_p.managed = ui_w_get_value (cMan) != 0;
	}
	ui_dialog_end (d);
	if (ok == 1) { save_project_ini (); refresh_tree (); }
	relayout ();
}

// ---- Run, Make App ------------------------------------------------------------------------------------------------------
static int g_run = -1;					// le bout de lecture du tube d'erreurs du programme lancé (-1 : aucun)
static pid_t g_runPid = 0; static bool g_runErr = false;
static void cmd_run ()
{
	if (!g_p.dir[0]) return;
	pg_commit (); if (P.pend) { P.pend = false; form_edited (); }
	save_all ();
	bas::Program *p = 0;
	if (!compile_check (&p, false)) return;
	bas::destroy (p);
	Str prog; build (prog, false);
	mkdir ("/tmp", 0755);
	char tmp[200]; snprintf (tmp, sizeof tmp, "/tmp/%s.bas", g_p.name);
	char *l1 = latin1_dup (prog.str ());
	bool wr = write_file (tmp, l1);
	delete [] l1;
	if (!wr) { status (TR ("Cannot write /tmp")); return; }
	if (g_run >= 0) { close (g_run); g_run = -1; }
	int fd[2];
	if (pipe (fd)) return;
	// (-i 2 : le fils n'a un tube de vfs que sur 0, 1, 2 -- libposix, adddup2 ; au-delà il ne le reçoit pas)
	char args[600]; snprintf (args, sizeof args, "%s-i 2 -d \"%s\" \"%s\"", g_p.managed ? "-m " : "", g_p.dir, tmp);
	pid_t pid = bas_spawn ("SD0:/bin/basic", args, fd[1], 2);
	close (fd[1]);
	if (pid <= 0) { close (fd[0]); status (TR ("Cannot start SD0:/bin/basic")); return; }
	fcntl (fd[0], F_SETFL, O_NONBLOCK);
	g_run = fd[0]; g_runPid = pid; g_runErr = false;
	char m[120]; snprintf (m, sizeof m, "%s %s", TR ("Running"), g_p.title); status (m);
}
// Une erreur d'exécution de /bin/basic -i : « ligne\0message\0 »
static void run_watch ()
{
	if (g_run < 0) return;
	// (la fin se voit à waitpid, avant de lire : un tube vide peut rendre 0 sans que le programme ait fini, et ce qu'il a
	// écrit avant de finir reste à lire)
	int st;
	bool ended = waitpid (g_runPid, &st, WNOHANG) == g_runPid;
	char m[220];
	int n = (int) read (g_run, m, sizeof m - 1);
	if (n > 0)
	{
		m[n] = 0;
		int line = atoi (m); const char *msg = m + strlen (m) + 1;
		const char *file = "?"; int at = 0;
		if (line > 0)					// (autre chose sur l'erreur standard : pas pour nous)
		{
			if (part_of (g_parts, line, &file, &at))
			{
				add_problem (0, file, at, msg);
				int d = find_doc (file);
				if (d >= 0) { show_doc (d); show_line (d, at); }
			}
			char t[260]; snprintf (t, sizeof t, "%s %s %d: %s", TR ("Runtime error:"), file, at, msg); status (t);
			refresh_msgs ();
			g_runErr = true;
		}
	}
	if (!ended) return;
	if (!g_runErr) status (TR ("The program has ended"));
	close (g_run); g_run = -1; g_runPid = 0;
}
static void cmd_check () { if (compile_check (0, false)) status (TR ("No problems: the program compiles")); }
// Une application d'ARMPI est un exécutable : le runtime (SD0:/bin/basic), le programme après lui
static void cmd_make_app ()
{
	if (!g_p.dir[0]) return;
	save_all ();
	bas::Program *p = 0;
	if (!compile_check (&p, false)) return;
	char dir[128]; snprintf (dir, sizeof dir, "SD0:/apps/%s.app", g_p.name);
	mkdir (dir, 0755);
	char path[200]; join (path, sizeof path, dir, "main");
	if (g_p.managed) bas::setManaged (p, true);
	char *bytes; int n = bas::saveBax (p, &bytes);
	bas::destroy (p);
	int r = 0; char *rt = bas_load ("SD0:/bin/basic", &r);
	char *all = 0; int an = rt ? bas::attachBax (rt, r, bytes, n, &all) : 0;
	delete [] rt; delete [] bytes;
	bool ok = all && bas_save (path, all, an);
	delete [] all;
	if (!ok) { char m[260]; snprintf (m, sizeof m, "%s %s", TR ("Cannot write"), path); status (m); return; }
	Str txt; txt.printf ("# Written by QBStudio > Make App\nname = %s\ncategory = %s\n", g_p.title, g_p.category);
	join (path, sizeof path, dir, "app.txt"); write_file (path, txt.str ());
	// l'icône : icon.bmp du projet, s'il y en a une
	char ic[260]; join (ic, sizeof ic, g_p.dir, "icon.bmp");
	int isz = 0; char *ib = bas_load (ic, &isz);
	if (ib) { join (path, sizeof path, dir, "icon.bmp"); bas_save (path, ib, isz); delete [] ib; }
	char m[200]; snprintf (m, sizeof m, "%s %s", TR ("App made:"), dir); status (m);
}

// ---- les éditions : à ce qui a le clavier --------------------------------------------------------------------------------
static bool des_focus () { return ui_w_get_focus () == D.view; }
static void cmd_edit (int op)
{
	Doc *d = cur_doc (); if (!d) return;
	if (ui_textarea_edit (d->ed, op) != UN_CHANGE) return;
	form_whole (*d, op);
	text_changed (*d);
}
static void cmd_copy ()
{
	Doc *d = cur_doc (); if (!d) return;
	if (des_focus () && D.sel) { Str t; el_write (t, D.sel, 0); ui_clip_set ((long) t.str (), -1); }
	else ui_textarea_edit (d->ed, UTE_COPY);
}
static void cmd_cut ()
{
	Doc *d = cur_doc (); if (!d) return;
	if (des_focus ()) { if (D.sel && D.form && D.sel != D.form->root) { cmd_copy (); des_remove_selected (); } }
	else cmd_edit (UTE_CUT);
}
static void cmd_paste ()
{
	Doc *d = cur_doc (); if (!d) return;
	if (!des_focus ()) { cmd_edit (UTE_PASTE); return; }
	if (d->type != DOC_FORM || !d->form) return;
	// des éléments copiés : lus comme des lignes de formulaire, mis après le choix
	char *b = (char *) malloc (65536); if (!b) return;
	long n = ui_clip_get ((long) b, 65536); if (n <= 0) { free (b); return; }
	b[n < 65535 ? n : 65535] = 0;
	Str src; src.puts ("Window Clip\n");
	for (char *p = b; *p; ) { src.puts ("  "); while (*p && *p != '\n') src.put (*p++); src.put ('\n'); if (*p) p++; }
	free (b);
	Form tmp; form_read (tmp, src.str ());
	if (!tmp.root || !tmp.root->kids.n) return;
	El *k = tmp.root->kids[0]; tmp.root->kids.erase (0);
	// un nom déjà pris : un autre
	struct Fix { static void names (Form &f, El *e) { if (e->name[0] && f.named (e->name)) { char base[32]; cpy (base, e->name, sizeof base); int l = (int) strlen (base); while (l && base[l - 1] >= '0' && base[l - 1] <= '9') base[--l] = 0; unique_name (f, base, e->name, sizeof e->name); } for (int i = 0; i < e->kids.n; i++) names (f, e->kids[i]); } };
	Fix::names (*d->form, k);
	El *s = D.sel ? D.sel : d->form->root;
	if (is_container (s->kind)) { D.dropPar = s; D.dropIdx = s->kids.n; }
	else { D.dropPar = s->parent ? s->parent : d->form->root; D.dropIdx = s->index () + 1; }
	D.dropOk = true;
	des_do_drop (k, false);
	D.dropOk = false;
}
static void cmd_find ()
{
	Doc *d = cur_doc (); if (!d) return;
	static char w[64] = "";
	if (!ask_line (TR ("Find"), TR ("Find (in the file shown):"), w, sizeof w) || !w[0]) return;
	if (d->type == DOC_FORM && g_view == 0) { set_view (1); show_doc (g_p.cur); }
	if (!ui_textarea_find (d->ed, (long) w)) status (TR ("Not found")); else ui_w_focus (d->ed);
}
static void cmd_view_code ()
{
	Doc *d = cur_doc ();
	if (d && d->type == DOC_FORM) { Doc *c = code_for_form (); if (c) show_doc ((int) (c - &g_p.docs[0])); }
	else { Doc *f = main_form (); if (f) { if (g_view == 2) set_view (1); show_doc ((int) (f - &g_p.docs[0])); } }
}
static void show_form_as (int v) { set_view (v); Doc *f = main_form (); if (f) show_doc ((int) (f - &g_p.docs[0])); }
static void cmd_generated () { Doc *f = main_form (); if (f && f->gen >= 0) show_doc (f->gen); }

// La grille, le dernier projet : dans SD0:/home/projects/qbstudio.ini
static void save_settings ()
{
	char t[320]; snprintf (t, sizeof t, "grid = %d\nsnap = %d\nsplit = %d\nlast = %s\n", g_showGrid ? 1 : 0, g_snap ? 1 : 0, g_split, g_p.dir);
	write_file (SETTINGS, t);
}
static void load_settings (char *last, int cap)
{
	last[0] = 0;
	char *t = read_file (SETTINGS); if (!t) return;
	char v[8] = "";
	ini_get (t, "grid", v, sizeof v); if (v[0]) g_showGrid = v[0] != '0';
	v[0] = 0; ini_get (t, "snap", v, sizeof v); if (v[0]) g_snap = v[0] != '0';
	v[0] = 0; ini_get (t, "split", v, sizeof v); if (v[0]) { int s = atoi (v); if (s >= 150 && s <= 850) g_split = s; }
	ini_get (t, "last", last, cap);
	delete [] t;
}

// ---- les menus -------------------------------------------------------------------------------------------------------------
enum { M_NEW = 1, M_OPEN, M_SAMPLE, M_SAVE, M_APP, M_QUIT, M_UNDO = 10, M_REDO, M_CUT, M_COPY, M_PASTE, M_DELETE, M_ALL, M_FIND, M_COMPLETE,
	M_DESIGN = 20, M_SPLIT, M_FORMTEXT, M_CODEFORM, M_GENERATED, M_GRID, M_SNAP, M_ADDMOD = 30, M_ADDUC, M_SETTINGS, M_RUN = 40, M_CHECK, M_HELP = 50, M_ABOUT };
static const char MENUS[] =
	"MFile\n"
	"I1\tNew Project...\n" "I2\tOpen Project...\tCtrl+O\n" "I3\tOpen the Example\n" "-\n" "I4\tSave All\tCtrl+S\n" "-\n" "I5\tMake App\n" "-\n" "I6\tQuit\tCtrl+Q\n"
	"MEdit\n"
	"I10\tUndo\tCtrl+Z\n" "I11\tRedo\tCtrl+Y\n" "-\n" "I12\tCut\tCtrl+X\n" "I13\tCopy\tCtrl+C\n" "I14\tPaste\tCtrl+V\n" "I15\tDelete\n" "I16\tSelect All\tCtrl+A\n" "-\n"
	"I17\tFind...\tCtrl+F\n" "I18\tComplete\tCtrl+J\n"
	"MView\n"
	"I20\tDesign\n" "I21\tSplit\n" "I22\tThe Form's Text\n" "I23\tCode / Form\tF7\n" "I24\tGenerated Code\n" "-\n" "I25c\tGrid\n" "I26c\tSnap to Grid\n"
	"MProject\n"
	"I30\tAdd Module...\n" "I31\tAdd User Control...\n" "I32\tSettings...\n"
	"MRun\n"
	"I40\tRun\tF5\n" "I41\tCheck\n" "I5\tMake App\n"
	"MHelp\n"
	"I50\tQBStudio Help\tF1\n" "I51\tAbout QBStudio\n";

static bool g_quit;
static void cmd_quit () { if (confirm_close ()) g_quit = true; }
static void command (int id)
{
	switch (id)
	{
	case M_NEW: cmd_new (); break;
	case M_OPEN: cmd_open (); break;
	case M_SAMPLE: if (confirm_close ()) load_project (SAMPLE); break;
	case M_SAVE: save_all (); break;
	case M_APP: cmd_make_app (); break;
	case M_QUIT: cmd_quit (); break;
	case M_UNDO: cmd_edit (UTE_UNDO); break;
	case M_REDO: cmd_edit (UTE_REDO); break;
	case M_CUT: cmd_cut (); break;
	case M_COPY: cmd_copy (); break;
	case M_PASTE: cmd_paste (); break;
	case M_DELETE: if (des_focus ()) des_remove_selected (); break;
	case M_ALL: { Doc *d = cur_doc (); if (d) { ui_textarea_edit (d->ed, UTE_ALL); ui_w_focus (d->ed); } break; }
	case M_FIND: cmd_find (); break;
	case M_COMPLETE: complete_here (false); break;
	case M_DESIGN: show_form_as (0); break;
	case M_SPLIT: show_form_as (1); break;
	case M_FORMTEXT: show_form_as (2); break;
	case M_CODEFORM: cmd_view_code (); break;
	case M_GENERATED: cmd_generated (); break;
	case M_GRID: g_showGrid = !g_showGrid; ui_menu_state (M_GRID, g_showGrid ? UMS_CHECKED : 0); save_settings (); des_dirty (); break;
	case M_SNAP: g_snap = !g_snap; ui_menu_state (M_SNAP, g_snap ? UMS_CHECKED : 0); save_settings (); break;
	case M_ADDMOD: cmd_add_module (); break;
	case M_ADDUC: cmd_add_usercontrol (); break;
	case M_SETTINGS: cmd_project_settings (); break;
	case M_RUN: cmd_run (); break;
	case M_CHECK: cmd_check (); break;
	case M_HELP: bas_spawn ("SD0:/apps/editor.app/main", "SD0:/apps/qbstudio.app/help.txt", -1, 0); break;
	case M_ABOUT: ui_message ((long) TR ("About QBStudio"), (long) TR ("QBStudio: desktop apps in BASIC -- a window drawn, its code written, run, made an app."), UM_OK); break;
	}
}

// ---- la fenêtre ----------------------------------------------------------------------------------------------------------------
static void repaint ()
{
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	des_dirty (); ui_view_dirty (T.view); pg_dirty ();
	ui_w_draw (0);
	ui_update_all ();
}
static void relayout ()
{
	int W = (int) ui.w, H = (int) ui.h;
	ui_w_move (g_tb, 0, 0, W, TB_H);
	int top = TB_H, bottom = H - ST_H;
	ui_w_move (g_stLeft, 10, bottom, 170, ST_H);
	ui_w_move (g_stMid, 190, bottom, imax (20, W - 190 - 130), ST_H);
	ui_w_move (g_stRight, W - 126, bottom, 116, ST_H);
	// à gauche : le projet, la boîte à outils (ou le plan)
	ui_w_move (g_hProject, 8, top, LEFT_W - 8, HEAD_H);
	ui_w_move (g_tree, 0, top + HEAD_H, LEFT_W, TREE_H);
	int ty = top + HEAD_H + TREE_H;
	ui_w_move (g_hTools, 8, ty, LEFT_W - 8, HEAD_H);
	tool_place_view (0, ty + HEAD_H, LEFT_W, imax (10, bottom - ty - HEAD_H));
	ui_w_move (g_outline, 4, ty + HEAD_H + 4, LEFT_W - 8, imax (10, bottom - ty - HEAD_H - 8));
	// à droite : les propriétés
	ui_w_move (g_hProps, W - RIGHT_W + 8, top, RIGHT_W - 8, HEAD_H);
	pg_place (W - RIGHT_W, top + HEAD_H, RIGHT_W, bottom - top - HEAD_H);
	// au milieu
	int cx = LEFT_W + 1, cw = W - LEFT_W - RIGHT_W - 2;
	ui_w_move (g_tabs, cx, top, imax (60, cw - 242), TABS_H);
	ui_w_move (g_seg, cx + cw - 236, top + (TABS_H - ctl_h ()) / 2, 234, ctl_h ());	// (the toolbar's right is the window's buttons)
	int y = top + TABS_H;
	Doc *d = cur_doc ();
	bool form = d && d->type == DOC_FORM;
	// les problèmes : sous le code ; sous un formulaire seulement quand il y a une erreur
	int errs = 0; for (int i = 0; i < g_probs.n; i++) if (g_probs[i].kind == 0) errs++;
	int msgH = form && !errs ? 0 : MSG_H;
	ui_w_show (g_msgs, msgH != 0); ui_w_show (g_hMsgs, msgH != 0);
	if (msgH) { ui_w_move (g_hMsgs, cx + 8, bottom - msgH, cw - 8, HEAD_H); ui_w_move (g_msgs, cx, bottom - msgH + HEAD_H, cw, msgH - HEAD_H); }
	if (d && !form)
	{
		ui_w_move (g_objBox, cx + 8, y + (CODEBAR_H - ctl_h ()) / 2, 200, ctl_h ());
		ui_w_move (g_evBox, cx + 216, y + (CODEBAR_H - ctl_h ()) / 2, 180, ctl_h ());
		y += CODEBAR_H;
	}
	int areaH = imax (20, bottom - y - msgH);
	ui_w_show (g_split_w, d && form && g_view == 1);
	if (d)
	{
		g_edX = cx; g_edY = y;
		if (form)
		{
			// Split : le concepteur, la barre à glisser, le texte du formulaire
			int bar = g_view == 1 ? BAR_H : 0;
			int dh = g_view == 0 ? areaH : g_view == 1 ? (areaH - bar) * g_split / 1000 : 0;
			g_splitTop = y; g_splitRoom = imax (1, areaH - bar);
			if (g_view != 2) des_place (cx, y, cw, dh);
			if (g_view == 1)
			{
				ui_w_move (g_split_w, cx, y + dh, cw, bar);
				ui_w_set_range (g_split_w, y + g_splitRoom * 150 / 1000, y + g_splitRoom * 850 / 1000, 0, 1);
				ui_w_set_value (g_split_w, y + dh);
			}
			if (g_view != 0) { g_edY = y + dh + bar; ui_w_move (d->ed, cx, y + dh + bar, cw, areaH - dh - bar); }
		}
		else ui_w_move (d->ed, cx, y, cw, areaH);
	}
	repaint ();
}
static long on_resize (long, long) { relayout (); return 0; }

// La nouvelle d'un widget
static int g_typed, g_keyHid;				// le caractère et le code de la touche en cours
static void control (long w, long code)
{
	if (w == g_seg && code == UN_CHANGE) view_changed ();
	else if (w == g_tree && (code == UN_CHANGE || code == UN_ACTIVATE)) tree_pick ();
	else if (w == g_outline && (code == UN_CHANGE || code == UN_ACTIVATE)) outline_pick ();
	else if (w == g_objBox && code == UN_CHANGE) refresh_events ();
	else if (w == g_evBox && code == UN_CHANGE) ev_picked ();
	else if (w == g_msgs && code == UN_CHANGE)
	{
		int i = (int) ui_w_get_value (g_msgs);
		if (i < 0 || i >= g_probs.n) return;
		Problem p = g_probs[i];
		int d = find_doc (p.file);
		if (d < 0) return;
		if (g_p.docs[d].type == DOC_FORM && g_view == 0) set_view (1);
		show_doc (d);
		if (p.line > 0) { show_line (d, p.line); ui_w_focus (g_p.docs[d].ed); }
	}
	else if (w == g_tabs && code == UN_CHANGE)
	{
		int i = (int) ui_w_get_value (g_tabs);
		if (i >= 0 && i < g_p.tabs.n && g_p.tabs[i] != g_p.cur) show_doc (g_p.tabs[i]);
	}
	else if (w == g_tabs && code == UN_CLOSE)
	{
		int i = (int) ui_tabstrip_closing (g_tabs);
		if (i < 0 || i >= g_p.tabs.n || g_p.tabs.n < 2 || g_p.docs[g_p.tabs[i]].dirty) return;	// (rien n'est perdu par une fermeture d'onglet)
		int d = g_p.tabs[i];
		g_p.docs[d].open = false; g_p.tabs.erase (i);
		if (d == g_p.cur) show_doc (g_p.tabs[imin (i, g_p.tabs.n - 1)]); else refresh_tabs ();
	}
	else if (w == g_split_w && code == UN_CHANGE)
	{
		int s = ((int) ui_w_get_value (w) - g_splitTop) * 1000 / g_splitRoom;
		s = s < 150 ? 150 : s > 850 ? 850 : s;
		if (s != g_split) { g_split = s; relayout (); }
	}
	else if (code == UN_ACTIVATE)
	{
		long b[] = { b_new, b_open, b_save, b_undo, b_redo, b_cut, b_copy, b_paste, b_find, b_run, b_check, b_app };
		static const int C[] = { M_NEW, M_OPEN, M_SAVE, M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_FIND, M_RUN, M_CHECK, M_APP };
		for (unsigned i = 0; i < sizeof b / sizeof b[0]; i++) if (w == b[i]) { command (C[i]); return; }
	}
	else if (code == UN_CHANGE)
	{
		for (int i = 0; i < g_p.docs.n; i++)
			if (g_p.docs[i].ed == w)
			{
				if (g_keyHid == 0x1D || g_keyHid == 0x1C) form_whole (g_p.docs[i], g_keyHid == 0x1D ? UTE_UNDO : UTE_REDO);	// (Ctrl+Z, Ctrl+Y dans l'éditeur)
				text_changed (g_p.docs[i]);
				if (g_typed == '.' && g_p.cur == i && g_p.docs[i].type == DOC_CODE) complete_here (true);	// (après un point : ce que l'objet a)
				return;
			}
	}
}
static long tool_button (const char *glyph, const char *label, const char *tip, long fl = 0)
{
	long b = ui_toolbar_add (g_tb, (long) glyph, (long) label, fl);
	ui_w_tip (b, (long) TR (tip));
	return b;
}
static void build_window ()
{
	s_jet = (const unsigned *) ui_jetons ();
	TB_H = ctl_h () + 14; TABS_H = ctl_h () + 2; CODEBAR_H = ctl_h () + 8;
	g_tb = ui_toolbar (0, 0, 100, TB_H);
	b_new = tool_button ("\xE2\x98\x90", 0, "New project...");
	b_open = tool_button ("\xE2\x87\xA7", 0, "Open a project... (Ctrl+O)");
	b_save = tool_button ("\xE2\x87\xA9", 0, "Save all (Ctrl+S)");
	b_undo = tool_button ("\xE2\x86\xB6", 0, "Undo (Ctrl+Z)", UTB_SEP);
	b_redo = tool_button ("\xE2\x86\xB7", 0, "Redo (Ctrl+Y)");
	b_cut = tool_button ("\xE2\x9C\x82", 0, "Cut (Ctrl+X)", UTB_SEP);
	b_copy = tool_button ("\xE2\x8A\x9E", 0, "Copy (Ctrl+C)");
	b_paste = tool_button ("\xE2\x96\xA3", 0, "Paste (Ctrl+V)");
	b_find = tool_button (0, TR ("Find"), "Find... (Ctrl+F)", UTB_SEP);
	b_run = tool_button ("\xE2\x96\xB6", TR ("Run"), "Run (F5): build the program and start it", UTB_SEP);
	b_check = tool_button ("\xE2\x9C\x93", TR ("Check"), "Check: compile without running");
	b_app = tool_button ("\xE2\x9A\x99", ui.w < 900 ? 0 : TR ("Make App"), "Make App: the program as an app (SD0:/apps/<name>.app)");	// (narrow: the window's buttons keep their place)
	g_seg = ui_segmented (0, 0, 234, ctl_h (), (long) TR ("Design|Split|Code"));
	ui_w_set_value (g_seg, g_view);
	ui_w_tip (g_seg, (long) TR ("The form's views: the drawing, both, its text"));

	g_hProject = ui_label (0, 0, 10, HEAD_H, (long) TR ("Project"), ULB_BOLD);
	g_tree = ui_tree (0, 0, LEFT_W, TREE_H);
	g_hTools = ui_label (0, 0, 10, HEAD_H, (long) TR ("Toolbox"), ULB_BOLD);
	tool_create ();
	g_outline = ui_list (0, 0, LEFT_W, 100, UL_NOSORT);
	ui_w_show (g_outline, 0);
	g_hProps = ui_label (0, 0, 10, HEAD_H, (long) TR ("Properties"), ULB_BOLD);
	pg_create ();
	g_tabs = ui_tabstrip (0, 0, 100, TABS_H, UTS_CLOSE);
	g_objBox = ui_dropdown (0, 0, 200, ctl_h (), (long) "");
	g_evBox = ui_dropdown (0, 0, 180, ctl_h (), (long) "");
	ui_w_show (g_objBox, 0); ui_w_show (g_evBox, 0);
	ui_w_tip (g_evBox, (long) TR ("The object, its event: its SUB"));
	g_hMsgs = ui_label (0, 0, 10, HEAD_H, (long) "", ULB_BOLD);
	g_msgs = ui_list (0, 0, 100, 100, UL_NOSORT);
	ui_list_column (g_msgs, (long) "", 70, 0);
	ui_list_column (g_msgs, (long) "", 120, 0);
	ui_list_column (g_msgs, (long) "", 44, ULC_RIGHT);
	ui_list_column (g_msgs, (long) "", 500, 0);
	des_create ();
	g_split_w = ui_splitter (0, 0, 100, BAR_H, 0, 1000);
	ui_w_show (g_split_w, 0);
	g_stLeft = ui_label (0, 0, 10, ST_H, (long) "", ULB_BOLD);
	g_stMid = ui_label (0, 0, 10, ST_H, (long) "", 0);
	g_stRight = ui_label (0, 0, 10, ST_H, (long) "", ULB_RIGHT);
	ui_menu ((long) MENUS);
	ui_menu_state (M_GRID, g_showGrid ? UMS_CHECKED : 0);
	ui_menu_state (M_SNAP, g_snap ? UMS_CHECKED : 0);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/qbstudio.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	bas::setKitSource (bas_kit_source);
	load_words ();
	g_isWord = is_word0;
	g_textW = [] (const char *s) -> int { char t[160]; plain (s, t, sizeof t); return tw (t); };
	g_complete = complete;
	g_onSelect = on_select; g_onFormEdited = form_edited; g_onElementOpen = on_element_open; g_onDesignHint = status;
	g_subExists = sub_exists; g_openSub = open_sub;
	mkdir ("SD0:/home", 0755); mkdir (PROJECTS, 0755);
	char last[200]; load_settings (last, sizeof last);

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (W0, (int) scr[0] - 8), wh = imin (H0, (int) (scr[1] - scr[3]) - imax ((int) scr[2], 96) - 32);
	if (ui_window_ex (0, scr[3], ww, wh, (long) "QBStudio", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (760, 480);
	ui_on_resize ((long) on_resize, 0);
	build_window ();
	relayout ();

	// le projet : nommé à l'appel (son dossier, son project.ini, un de ses fichiers), sinon le dernier, sinon l'exemple
	bool ok = false;
	if (argc > 1 && argv[1][0])
	{
		char dir[256]; cpy (dir, argv[1], sizeof dir);
		if (ends_with (dir, ".ini") || ends_with (dir, ".form") || ends_with (dir, ".bas")) { char *b = (char *) base_name (dir); if (b > dir) b[-1] = 0; }
		ok = load_project (dir);
	}
	if (!ok && last[0]) { char ip[300]; join (ip, sizeof ip, last, "project.ini"); if (file_exists (ip)) ok = load_project (last); }
	if (!ok) load_project (SAMPLE);
	repaint ();

	while (!g_quit)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE) { cmd_quit (); continue; }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			g_typed = press ? ui_ev_field (&ui, EV_CHAR) : 0;
			g_keyHid = press && (ui_ev_field (&ui, EV_MODS) & UI_MOD_CTRL) ? ui_ev_field (&ui, EV_CODE) : 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press)
			{
				long id = ui_menu_key (0);
				// (ce que l'éditeur fait lui-même : pas deux fois)
				bool edit = id >= M_UNDO && id <= M_ALL;
				if (id >= 0 && !(edit && taken)) command ((int) id);
			}
		}
		else ui_w_idle ();
		pg_tick ();
		caret_watch ();
		run_watch ();
		if (g_checkAt && (int) (bas_ms () - g_checkAt) >= 0)
		{
			g_checkAt = 0;
			check_program (true);
			Doc *f = main_form (); if (f) { regenerate (*f); if (f->form) set_problems_for (f->file, f->form); }
			refresh_outline ();
			pg_dirty ();
		}
		ui_view_flush ();
	}
	save_settings ();
	return 0;
}
