//
// qbasic -- the BASIC editor (QBasic style) of Onyx on ARMPI (src/apps/qbasic/README.md), on UIKit.
//
//   * The program is split into MODULES, edited one at a time: the main module and each SUB / FUNCTION (View >
//     SUBs... / F2 lists them; Edit > New SUB... / New FUNCTION... creates one). On disk it is one ordinary .bas
//     file: the main module, then every SUB / FUNCTION block (what QBasic writes too).
//   * Run > Start (F5) checks the syntax (the compiler is linked in) -- an error jumps to its module and line -- then
//     saves a copy to /tmp and runs it with /bin/basic -i: a run-time error comes back on a pipe and jumps the same way.
//   * File > Make App... turns the program into an app: SD0:/apps/<name>.app/main, the runtime with the compiled
//     program after it, and app.txt.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <uikit.h>

#include "../../lib/basic/bas.h"
#include "../../lib/basic/hostutil.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))
// (a word followed by a blank: the dictionary keeps no blanks at the ends)
static const char *TS (const char *s)
{
	static char o[120], k[120]; int n = 0;
	while (s[n] && n < 118) { k[n] = s[n]; n++; }
	k[n - 1] = 0;
	snprintf (o, sizeof o, "%s ", TR (k));
	return o;
}
#define W	760
#define H	540
#define MAXMOD	64

static struct ui_vars ui;
static long g_ed, g_head, g_status, g_pos;

static int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static char up (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
static bool starts_kw (const char *p, const char *kw)	// "SUB" at p, then a blank / end
{
	int i = 0; while (kw[i]) { if (up (p[i]) != kw[i]) return false; i++; }
	return p[i] == ' ' || p[i] == '\t' || p[i] == 0 || p[i] == '\n' || p[i] == '\r' || p[i] == '(';
}

// A method's VIRTUAL / OVERRIDE before SUB / FUNCTION: the place after it (else p).
static const char *skip_modifier (const char *p)
{
	int n = starts_kw (p, "VIRTUAL") ? 7 : starts_kw (p, "OVERRIDE") ? 8 : 0;
	if (!n || p[n] == '(') return p;
	const char *q = p + n; while (*q == ' ' || *q == '\t') q++;
	return starts_kw (q, "SUB") || starts_kw (q, "FUNCTION") ? q : p;
}

// ---- the modules ------------------------------------------------------------------------------
struct Module { char name[48]; int kind; char *text; };		// kind 0 main, 1 SUB, 2 FUNCTION
static Module g_mod[MAXMOD]; static int g_nmod = 0, g_cur = 0;
static char g_path[256] = "";
static bool g_dirty = false;

static void set_status (const char *a, const char *b = "")
{
	char s[200]; snprintf (s, sizeof s, "%s%s", a, b);
	ui_w_set_text (g_status, (long) s);
}

static void free_modules () { for (int i = 0; i < g_nmod; i++) delete [] g_mod[i].text; g_nmod = 0; }
static char *dup_range (const char *s, int n) { char *d = new char[n + 1]; for (int i = 0; i < n; i++) d[i] = s[i]; d[n] = 0; return d; }

// Split a whole program into modules.
static void split (const char *src)
{
	free_modules ();
	int n = slen (src);
	// main module = every line not inside a SUB / FUNCTION block
	char *main = new char[n + 2]; int mn = 0;
	int i = 0;
	bool iface = false;					// in INTERFACE ... END INTERFACE: its SUB lines are declarations
	while (i < n)
	{
		int ls = i; while (i < n && src[i] != '\n') i++;
		int le = i; if (i < n) i++;
		const char *p = src + ls; while (*p == ' ' || *p == '\t') p++;
		if (starts_kw (p, "INTERFACE") && p[9] == ' ') iface = true;
		if (iface && starts_kw (p, "END")) { const char *t = p + 3; while (*t == ' ') t++; if (starts_kw (t, "INTERFACE")) iface = false; }
		p = skip_modifier (p);
		int kind = iface ? 0 : starts_kw (p, "SUB") ? 1 : starts_kw (p, "FUNCTION") ? 2 : 0;
		if (kind && g_nmod < MAXMOD - 1)
		{
			// name
			const char *q = p + (kind == 1 ? 3 : 8); while (*q == ' ') q++;
			char name[48]; int k = 0;
			while (*q && *q != ' ' && *q != '(' && *q != '\n' && *q != '\r' && k < 47) name[k++] = *q++;
			name[k] = 0;
			// up to END SUB / END FUNCTION (inclusive)
			int bs = ls;
			for (;;)
			{
				if (i >= n) break;
				int l2 = i; while (i < n && src[i] != '\n') i++;
				if (i < n) i++;
				const char *r = src + l2; while (*r == ' ' || *r == '\t') r++;
				if (starts_kw (r, "END"))
				{
					const char *t = r + 3; while (*t == ' ') t++;
					if (starts_kw (t, kind == 1 ? "SUB" : "FUNCTION")) break;
				}
			}
			int be = i; while (be > bs && (src[be - 1] == '\n' || src[be - 1] == '\r')) be--;
			Module md; scpy (md.name, name, sizeof md.name); md.kind = kind; md.text = dup_range (src + bs, be - bs);
			if (g_nmod == 0) g_nmod = 1;				// slot 0 = main (filled below)
			g_mod[g_nmod++] = md;
			continue;
		}
		for (int j = ls; j < le; j++) if (src[j] != '\r') main[mn++] = src[j];
		main[mn++] = '\n';
	}
	while (mn > 0 && main[mn - 1] == '\n') mn--;
	main[mn] = 0;
	if (g_nmod == 0) g_nmod = 1;
	scpy (g_mod[0].name, "(main)", sizeof g_mod[0].name); g_mod[0].kind = 0; g_mod[0].text = main;
}

// The editor's text back into the current module.
static void sync_current ()
{
	if (!g_ed || g_cur < 0 || g_cur >= g_nmod) return;
	delete [] g_mod[g_cur].text;
	const char *t = (const char *) ui_w_get_text (g_ed);
	g_mod[g_cur].text = dup_range (t, slen (t));
	// a renamed header: follow it
	if (g_mod[g_cur].kind)
	{
		const char *p = g_mod[g_cur].text; while (*p == ' ' || *p == '\t' || *p == '\n') p++;
		p = skip_modifier (p);
		const char *q = p; while (*q && *q != ' ') q++;
		while (*q == ' ') q++;
		char name[48]; int k = 0;
		while (*q && *q != ' ' && *q != '(' && *q != '\n' && k < 47) name[k++] = *q++;
		name[k] = 0;
		if (k) scpy (g_mod[g_cur].name, name, sizeof g_mod[g_cur].name);
	}
}

// Whole program: main, then each SUB / FUNCTION. starts[i] = first line (1-based) of module i.
static char *compose (int *starts)
{
	sync_current ();
	int total = 16;
	for (int i = 0; i < g_nmod; i++) total += slen (g_mod[i].text) + 4;
	char *s = new char[total]; int n = 0, line = 1;
	for (int i = 0; i < g_nmod; i++)
	{
		if (i > 0) { s[n++] = '\n'; line++; }
		if (starts) starts[i] = line;
		for (const char *p = g_mod[i].text; *p; p++) { s[n++] = *p; if (*p == '\n') line++; }
		s[n++] = '\n'; line++;
	}
	s[n] = 0;
	return s;
}

static void show_module (int i)
{
	if (i < 0 || i >= g_nmod) return;
	g_cur = i;
	ui_w_set_text (g_ed, (long) g_mod[i].text);
	char h[80];
	if (g_mod[i].kind == 0) scpy (h, TR ("Main module"), sizeof h);
	else snprintf (h, sizeof h, "%s %s", g_mod[i].kind == 1 ? "SUB" : "FUNCTION", g_mod[i].name);
	ui_w_set_text (g_head, (long) h);
	ui_w_focus (g_ed);
	ui_w_draw (0);
}

static void set_title_status ()
{
	char t[280]; snprintf (t, sizeof t, "%s  ", g_path[0] ? g_path : TR ("Untitled"));
	set_status (t, g_dirty ? TR ("(modified)") : "");
}

// The offset of a line (0-based) of the editor's text.
static long line_offset (int ln)
{
	const char *t = (const char *) ui_w_get_text (g_ed); long p = 0;
	for (; ln > 0 && t[p]; p++) if (t[p] == '\n') ln--;
	return p;
}
// Jump to a line (1-based) of the composed program.
static void goto_program_line (int line)
{
	int starts[MAXMOD]; char *s = compose (starts); delete [] s;
	int m = 0;
	for (int i = 0; i < g_nmod; i++) if (starts[i] <= line) m = i;
	show_module (m);
	ui_textarea_goto (g_ed, line_offset (line - starts[m]));
	ui_textarea_highlight (g_ed, line - starts[m]);
}

// ---- small dialogs -----------------------------------------------------------------------------
static bool ask_text (const char *title, char *out, int cap)
{
	if (!ui_ask ((long) "QBasic", (long) title, (long) out, cap)) return false;
	return out[0] != 0;
}

// The compile dialog (Run > Make .bax, File > Make App): how the program is made. "Managed": it will run on the VM
// (as before machine code); without it /bin/basic runs it in machine code. The choice is kept.
static bool g_optManaged = false;
static bool ask_compile (const char *title)
{
	enum { DW = 460, DH = 110 };
	long d = ui_dialog_begin (DW, DH, (long) title);
	long c = ui_checkbox (14, 14, DW - 28, 24, (long) TR ("Managed (runs on the VM instead of machine code)"), g_optManaged);
	long x = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	long r = ui_dialog_run (d, 0, 0);
	if (r == 1) g_optManaged = ui_w_get_value (c) != 0;
	ui_dialog_end (d);
	return r == 1;
}

// View > SUBs...: every module; Edit / Delete.
static long s_subsList;
static long subs_fn (long d, long w, long code) { if (w == s_subsList && code == UN_ACTIVATE) ui_dialog_close (d, 1); return 0; }
static int subs_dialog (int *sel)
{
	enum { DW = 380, DH = 330 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("SUBs and FUNCTIONs"));
	s_subsList = ui_list (12, 12, DW - 24, DH - 70, 0);
	for (int i = 0; i < g_nmod; i++)
	{
		char s[64];
		if (i == 0) scpy (s, TR ("(main module)"), sizeof s);
		else snprintf (s, sizeof s, "%s %s", g_mod[i].kind == 1 ? "SUB" : "FUNCTION", g_mod[i].name);
		ui_list_add (s_subsList, (long) s, 0);
	}
	ui_w_set_value (s_subsList, g_cur);
	long x = ui_dialog_button (d, 0, (long) TR ("Edit"), 1, UB_DEFAULT, DW - 12);
	x = ui_dialog_button (d, 1, (long) TR ("Delete"), 2, 0, x);
	ui_dialog_button (d, 2, (long) TR ("Cancel"), 0, 0, x);
	ui_dialog_cancel (d, 0);
	ui_w_focus (s_subsList);
	long r = ui_dialog_run (d, (long) subs_fn, 0);
	*sel = (int) ui_w_get_value (s_subsList);
	ui_dialog_end (d);
	return (int) r;
}

// ---- commands ------------------------------------------------------------------------------------
static void op_save ();
static bool confirm_discard ()
{
	if (!g_dirty) return true;
	long r = ui_message ((long) "QBasic", (long) TR ("The program has changed. Save it first?"), UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { op_save (); return !g_dirty; }
	return true;
}

static void load_text (const char *src)
{
	split (src);
	g_cur = 0;
	show_module (0);
	g_dirty = false;
}

static void op_new ()
{
	if (!confirm_discard ()) return;
	g_path[0] = 0;
	load_text ("");
	set_title_status ();
}

static bool load_file (const char *path)
{
	int n;
	char *b = bas_load (path, &n);
	if (!b) { set_status (TS ("Cannot open "), path); return false; }
	load_text (b);
	delete [] b;
	scpy (g_path, path, sizeof g_path);
	set_title_status ();
	return true;
}

static const char *basename_of (const char *p) { int n = slen (p); while (n > 0 && p[n - 1] != '/' && p[n - 1] != ':') n--; return p + n; }
static void start_dir (char *d, int cap)
{
	if (!g_path[0]) { scpy (d, "SD0:/home/basic", cap); return; }
	scpy (d, g_path, cap);
	int e = (int) (basename_of (d) - d);
	if (e > 1 && d[e - 1] == '/' && d[e - 2] != ':') e--;
	d[e] = 0;
}
static void op_open ()
{
	if (!confirm_discard ()) return;
	char p[256], d[256]; start_dir (d, sizeof d);
	ui_file_filter ((long) "*.bas");
	long ok = ui_file_open ((long) d, (long) p, sizeof p);
	ui_file_filter (0);
	if (ok) load_file (p);
}

static bool write_to (const char *path)
{
	char *s = compose (0);
	bool ok = bas_save (path, s, slen (s));
	delete [] s;
	return ok;
}

static void op_save_as ()
{
	char p[256], d[256]; start_dir (d, sizeof d);
	ui_file_filter ((long) "*.bas");
	long ok = ui_file_save ((long) d, (long) (g_path[0] ? basename_of (g_path) : "program.bas"), (long) p, sizeof p);
	ui_file_filter (0);
	if (!ok) return;
	int n = slen (p);
	if (!(n > 4 && p[n - 4] == '.' && (p[n - 3] | 32) == 'b')) scpy (p + n, ".bas", sizeof p - n);
	if (!write_to (p)) { set_status (TS ("Cannot save "), p); return; }
	scpy (g_path, p, sizeof g_path);
	g_dirty = false;
	set_title_status ();
}

static void op_save ()
{
	if (!g_path[0]) { op_save_as (); return; }
	if (!write_to (g_path)) { set_status (TS ("Cannot save "), g_path); return; }
	g_dirty = false;
	set_title_status ();
}

static void new_proc (int kind)
{
	char name[48] = "";
	if (!ask_text (kind == 1 ? TR ("Name of the new SUB") : TR ("Name of the new FUNCTION (end with $ for text)"), name, sizeof name)) return;
	for (int i = 0; name[i]; i++) if (name[i] == ' ') { set_status (TR ("A name has no spaces")); return; }
	for (int i = 1; i < g_nmod; i++)
		if (strcasecmp (g_mod[i].name, name) == 0) { show_module (i); set_status (TS ("Already exists: "), name); return; }
	if (g_nmod >= MAXMOD) { set_status (TR ("Too many SUBs / FUNCTIONs")); return; }
	sync_current ();
	char t[160];
	int n = kind == 1 ? snprintf (t, sizeof t, "SUB %s ()\n    \nEND SUB", name) : snprintf (t, sizeof t, "FUNCTION %s ()\n    \nEND FUNCTION", name);
	Module &m = g_mod[g_nmod];
	scpy (m.name, name, sizeof m.name); m.kind = kind; m.text = dup_range (t, n);
	g_nmod++;
	show_module (g_nmod - 1);
	ui_textarea_goto (g_ed, line_offset (1));
	g_dirty = true;
	set_title_status ();
}

static void op_subs ()
{
	sync_current ();
	int sel, r = subs_dialog (&sel);
	if (r == 1 && sel >= 0) show_module (sel);
	else if (r == 2 && sel > 0)
	{
		char q[120]; snprintf (q, sizeof q, "%s %s?", TR ("Delete"), g_mod[sel].name);
		if (!ui_message ((long) TR ("Delete"), (long) q, UM_YESNO)) return;
		delete [] g_mod[sel].text;
		for (int i = sel; i + 1 < g_nmod; i++) g_mod[i] = g_mod[i + 1];
		g_nmod--;
		g_dirty = true;
		show_module (0);
		set_title_status ();
	}
	else if (r == 2) set_status (TR ("The main module cannot be deleted"));
}

// Syntax check; on error jump there. true = fine.
static bool check (bool quiet)
{
	int starts[MAXMOD];
	char *s = compose (starts);
	bas::Error e;
	bas::Program *p = bas::compile (s, &e);
	delete [] s;
	if (!p)
	{
		goto_program_line (e.line);
		set_status (TS ("Syntax error: "), e.msg);
		return false;
	}
	bas::destroy (p);
	if (!quiet) set_status (TR ("No syntax errors."));
	return true;
}

// The program being run: the read end of its error pipe (-1: none).
static int g_run = -1;
static void op_run ()
{
	if (!check (true)) return;
	mkdir ("/tmp", 0755);
	char tmp[256]; snprintf (tmp, sizeof tmp, "/tmp/%s", g_path[0] ? basename_of (g_path) : "untitled.bas");
	if (!write_to (tmp)) { set_status (TS ("Cannot write "), tmp); return; }
	ui_textarea_highlight (g_ed, -1);
	if (g_run >= 0) { close (g_run); g_run = -1; }
	int fd[2];
	if (pipe (fd)) return;
	// basic -i 2 [-d <the program's folder>] <copy> (a pipe reaches a child on its descriptors 0 to 2)
	char args[600]; int k = snprintf (args, sizeof args, "-i 2 ");
	if (g_path[0]) { char dir[256]; start_dir (dir, sizeof dir); k += snprintf (args + k, sizeof args - k, "-d \"%s\" ", dir); }
	snprintf (args + k, sizeof args - k, "\"%s\"", tmp);
	pid_t p = bas_spawn ("SD0:/bin/basic", args, fd[1], 2);
	close (fd[1]);
	if (p <= 0) { close (fd[0]); set_status (TR ("Cannot start SD0:/bin/basic")); return; }
	fcntl (fd[0], F_SETFL, O_NONBLOCK);
	g_run = fd[0];
	set_status (TS ("Running "), basename_of (tmp));
}

// A run-time error from /bin/basic -i: "line\0message\0".
static void run_watch ()
{
	if (g_run < 0) return;
	char m[200];
	int n = (int) read (g_run, m, sizeof m - 1);
	if (n < 0) return;
	if (n > 0)
	{
		m[n] = 0;
		int line = atoi (m);
		const char *msg = m + slen (m) + 1;
		if (line > 0) goto_program_line (line);
		set_status (TS ("Runtime error: "), msg);
	}
	else set_title_status ();
	close (g_run); g_run = -1;
	int st; while (waitpid (-1, &st, WNOHANG) > 0) {}
}

static void op_goto ()
{
	char t[16] = "";
	if (!ask_text (TR ("Go to line (of the whole program)"), t, sizeof t)) return;
	int n = atoi (t);
	if (n > 0) goto_program_line (n);
}

// The program compiled (bytecode: runs without parsing) into a .bax file. true = written.
// alone: a standalone executable -- the runtime (SD0:/bin/basic) with the program after it.
static bool write_bax (const char *path, bool managed, bool alone = false)
{
	int starts[MAXMOD];
	char *s = compose (starts);
	bas::Error e;
	bas::Program *p = bas::compile (s, &e);
	delete [] s;
	if (!p) { check (false); return false; }
	if (managed) bas::setManaged (p, true);
	char *bytes; int n = bas::saveBax (p, &bytes);
	bas::destroy (p);
	if (alone)
	{
		int r;
		char *rt = bas_load ("SD0:/bin/basic", &r);
		char *all = 0; int an = rt ? bas::attachBax (rt, r, bytes, n, &all) : 0;
		delete [] rt; delete [] bytes;
		if (!all) return false;
		bytes = all; n = an;
	}
	bool ok = bas_save (path, bytes, n);
	delete [] bytes;
	return ok;
}
// Run > Make .bax: <program>.bax beside the .bas (the Files app opens it, CHAIN...).
static void op_make_bax ()
{
	if (!g_path[0]) { op_save (); if (!g_path[0]) return; }
	char out[260]; scpy (out, g_path, sizeof out);
	int e = slen (out), d = e; while (d > 0 && out[d - 1] != '.' && out[d - 1] != '/') d--;
	if (d > 0 && out[d - 1] == '.') e = d - 1;
	scpy (out + e, ".bax", sizeof out - e);
	if (!ask_compile ("Make .bax")) return;
	if (write_bax (out, g_optManaged)) set_status (TS ("Compiled: "), out);
	else set_status (TS ("Cannot write "), out);
}

// File > Make App...: SD0:/apps/<name>.app/{main, app.txt}. An app of ARMPI is an executable: the runtime and the program.
static void op_make_app ()
{
	char name[40] = "";
	if (g_path[0]) { scpy (name, basename_of (g_path), sizeof name); int n = slen (name); if (n > 4 && name[n - 4] == '.') name[n - 4] = 0; }
	if (!ask_text (TR ("App folder name (apps/<name>.app)"), name, sizeof name)) return;
	for (int i = 0; name[i]; i++) if (name[i] == ' ' || name[i] == '/' || name[i] == ':') { set_status (TR ("No spaces or / in an app folder name")); return; }
	char title[64]; scpy (title, name, sizeof title);
	if (!ask_text (TR ("App title (shown under its icon)"), title, sizeof title)) return;
	if (!ask_compile ("Make App")) return;
	char dir[128], p[160];
	snprintf (dir, sizeof dir, "SD0:/apps/%s.app", name);
	mkdir (dir, 0755);
	snprintf (p, sizeof p, "%s/main", dir);
	if (!write_bax (p, g_optManaged, true)) { set_status (TS ("Cannot write "), p); return; }
	char txt[200]; int k = snprintf (txt, sizeof txt, "# Written by QBasic > Make App\nname = %s\ncategory = Développement\n", title);
	snprintf (p, sizeof p, "%s/app.txt", dir);
	bas_save (p, txt, k);
	set_status (TS ("App created: "), dir);
}

static void op_help () { bas_spawn ("SD0:/apps/editor.app/main", "SD0:/apps/qbasic.app/help.txt", -1, 0); }
static void op_examples ()
{
	if (!confirm_discard ()) return;
	char p[256];
	ui_file_filter ((long) "*.bas");
	long ok = ui_file_open ((long) "SD0:/basic/examples", (long) p, sizeof p);
	ui_file_filter (0);
	if (ok) load_file (p);
}

// ---- the window --------------------------------------------------------------------------------
enum { M_NEW = 1, M_OPEN, M_EXAMPLES, M_SAVE, M_SAVEAS, M_APP, M_NEWSUB = 10, M_NEWFN, M_GOTO, M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE,
	M_SUBS = 20, M_RUN = 30, M_CHECK, M_BAX, M_HELP = 40 };

static const char MENUS[] =
	"MFile\n"
	"I1\tNew\tCtrl+N\n" "I2\tOpen...\tCtrl+O\n" "I3\tExamples...\n" "I4\tSave\tCtrl+S\n" "I5\tSave As...\n" "-\n" "I6\tMake App...\n"
	"MEdit\n"
	"I13\tUndo\tCtrl+Z\n" "I14\tRedo\tCtrl+Y\n" "-\n" "I15\tCut\tCtrl+X\n" "I16\tCopy\tCtrl+C\n" "I17\tPaste\tCtrl+V\n" "-\n"
	"I10\tNew SUB...\n" "I11\tNew FUNCTION...\n" "-\n" "I12\tGo to Line...\tCtrl+G\n"
	"MView\n"
	"I20\tSUBs...\tF2\n"
	"MRun\n"
	"I30\tStart\tF5\n" "I31\tCheck Syntax\tCtrl+K\n" "I32\tMake .bax\n"
	"MHelp\n"
	"I40\tKeywords\tF1\n";

static void command (int id)
{
	switch (id)
	{
	case M_NEW: op_new (); break;
	case M_OPEN: op_open (); break;
	case M_EXAMPLES: op_examples (); break;
	case M_SAVE: op_save (); break;
	case M_SAVEAS: op_save_as (); break;
	case M_APP: op_make_app (); break;
	case M_NEWSUB: new_proc (1); break;
	case M_NEWFN: new_proc (2); break;
	case M_GOTO: op_goto (); break;
	case M_UNDO: ui_textarea_edit (g_ed, UTE_UNDO); break;
	case M_REDO: ui_textarea_edit (g_ed, UTE_REDO); break;
	case M_CUT: ui_textarea_edit (g_ed, UTE_CUT); break;
	case M_COPY: ui_textarea_edit (g_ed, UTE_COPY); break;
	case M_PASTE: ui_textarea_edit (g_ed, UTE_PASTE); break;
	case M_SUBS: op_subs (); break;
	case M_RUN: op_run (); break;
	case M_CHECK: check (false); break;
	case M_BAX: op_make_bax (); break;
	case M_HELP: op_help (); break;
	}
}

// BASIC's words, for the editor's colours (bas::wordList: capitals, separated by blanks)
static char s_words[16384];
static long is_keyword (long word, long len)
{
	const char *w = (const char *) word;
	for (const char *p = s_words; *p; )
	{
		const char *e = p; while (*e && *e != ' ') e++;
		if (e - p == len && !strncasecmp (p, w, len)) return 1;
		p = *e ? e + 1 : e;
	}
	return 0;
}

enum { STRIP_H = 22, HEAD_H = 36 };
static void layout (long w, long h)
{
	ui_w_move (g_head, 8, 0, w - 120, HEAD_H);			// (left of the window's buttons)
	ui_w_move (g_ed, 0, HEAD_H, w, h - HEAD_H - STRIP_H);
	ui_w_move (g_status, 8, h - STRIP_H, w - 106, STRIP_H);
	ui_w_move (g_pos, w - 90, h - STRIP_H, 82, STRIP_H);
}
static long on_resize (long w, long h) { ui_fill (0, 0, w, h, ((const unsigned *) ui_jetons ())[TJ_FACE / 4]); layout (w, h); ui_w_draw (0); ui_update_all (); return 0; }

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/qbasic.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	bas::setKitSource (bas_kit_source);
	bas::wordList (s_words, sizeof s_words);
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = W < (int) scr[0] - 8 ? W : (int) scr[0] - 8, wh = H < (int) (scr[1] - scr[3] - scr[2]) - 128 ? H : (int) (scr[1] - scr[3] - scr[2]) - 128;
	if (ui_window_ex (0, scr[3], ww, wh, (long) "QBasic", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (420, 260);
	ui_on_resize ((long) on_resize, 0);
	mkdir ("SD0:/home/basic", 0755);

	g_head = ui_label (0, 0, 10, 10, (long) "", ULB_BOLD);
	g_ed = ui_textarea (0, 0, 10, 10, UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_CODE | UTA_BASIC);
	ui_textarea_keywords (g_ed, (long) is_keyword);
	g_status = ui_label (0, 0, 10, 10, (long) "", ULB_DIM);
	g_pos = ui_label (0, 0, 10, 10, (long) "1:1", ULB_DIM | ULB_RIGHT);
	ui_fill (0, 0, ui.w, ui.h, ((const unsigned *) ui_jetons ())[TJ_FACE / 4]);
	layout (ui.w, ui.h);
	ui_menu ((long) MENUS);

	if (argc > 1 && argv[1][0]) load_file (argv[1]);
	else { load_text ("' Welcome to BASIC. Run > Start (F5) runs the program.\nPRINT \"Hello, world!\"\n"); set_title_status (); }
	ui_w_focus (g_ed);
	ui_w_draw (0);
	ui_update_all ();

	long lastLine = -1, lastCol = -1;
	for (;;)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE) { if (confirm_discard ()) break; continue; }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP)
		{
			char p[256]; long kind = 0;
			long n = uikit_drop_get (p, sizeof p - 1, &kind);
			if (n > 0 && confirm_discard ()) { p[n] = 0; char *e = strchr (p, '\n'); if (e) *e = 0; load_file (p); }
		}
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0, w = uikit_w_event (&code, &taken);
			if (w == g_ed && code == UN_CHANGE && !g_dirty) { g_dirty = true; set_title_status (); ui_textarea_highlight (g_ed, -1); }
			if (press)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
			}
		}
		else ui_w_idle ();
		run_watch ();
		long col, l = uikit_textarea_caret (g_ed, &col);
		if (l != lastLine || col != lastCol)
		{
			lastLine = l; lastCol = col;
			char s[40]; snprintf (s, sizeof s, "%ld:%ld", l + 1, col + 1);
			ui_w_set_text (g_pos, (long) s);
		}
	}
	return 0;
}
