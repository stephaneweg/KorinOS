//
// turtle -- Turtle Quest, Onyx's game that teaches programming, on ARMPI (src/apps/turtle/README.md): the player writes
// BASIC with the turtle's words (world.h) to bring a turtle to its flag, pick coins, open doors, paint tiles and draw
// figures. The window, on UIKit: the levels of a pack on the left (their stars), the program in the middle (Run, Step,
// Stop, Reset, the speed; the words the level knows under it, a click writes one), the level on the right (what to do, a
// hint, the lesson of a new idea) and the turtle's board, the run played back on it -- the line being run lit in the
// program, the turtle walking. An error says where and why; a level won gives 1 to 3 stars. The level editor (Ctrl+E)
// takes the place of the list.
//
// Keys: F5 Run, F8 Step, F7 Stop, F9 Reset, F1 the lesson, F2 the hint, Ctrl+N / Ctrl+P the next / previous level, Ctrl+O
// a pack of levels, Ctrl+E the level editor. The language is the system's (ui_lang): the texts and the words shown.
//
// Files: the packs SD0:/apps/turtle.app/*.turtle (and any .turtle opened, dropped or given as the argument); the
// player's levels SD0:/home/turtle/my-levels.turtle; the players' progress SD0:/home/turtle/progress.ini.
//
// main --check: every level of the packs run with its own solution, in both languages; the result in the window's
// message and in SD0:/home/turtle/check.txt.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "world.h"
#include "ui.h"
#include "../../lib/basic/hostutil.h"

using namespace turtle;

#define APPDIR		"SD0:/apps/turtle.app"
#define USERDIR		"SD0:/home/turtle"
#define PROGRESS	USERDIR "/progress.ini"
#define USERPACK	USERDIR "/my-levels.turtle"
#define CHECKFILE	USERDIR "/check.txt"

static int g_lang = LANG_EN;				// the system's language (ui_lang), taken at the start
static inline const char *L2 (const char *en, const char *fr) { return g_lang == LANG_FR ? fr : en; }

// ---- the progress: (section, key) = value, kept in memory, written when it changed ------------------------------------
struct KV { char sec[40], key[48]; char *val; };
static Arr<KV> g_kv;
static bool g_kvDirty = false;
static const char *kv_get (const char *sec, const char *key, const char *def = "")
{
	for (int i = 0; i < g_kv.n; i++) if (!strcmp (g_kv[i].sec, sec) && !strcmp (g_kv[i].key, key)) return g_kv[i].val;
	return def;
}
static void kv_set (const char *sec, const char *key, const char *val)
{
	for (int i = 0; i < g_kv.n; i++)
		if (!strcmp (g_kv[i].sec, sec) && !strcmp (g_kv[i].key, key))
		{
			if (strcmp (g_kv[i].val, val)) { free (g_kv[i].val); g_kv[i].val = tdup (val); g_kvDirty = true; }
			return;
		}
	KV k; tcpy (k.sec, sec, sizeof k.sec); tcpy (k.key, key, sizeof k.key); k.val = tdup (val);
	g_kv.push (k);
	g_kvDirty = true;
}
static void kv_save ()
{
	if (!g_kvDirty) return;
	size_t cap = 4096, n = 0; char *o = (char *) malloc (cap);
	auto put = [&] (const char *s, bool esc) {
		for (; *s; s++)
		{
			if (n + 4 > cap) { cap *= 2; o = (char *) realloc (o, cap); }
			if (esc && *s == '\n') { o[n++] = '\\'; o[n++] = 'n'; }
			else if (esc && *s == '\\') { o[n++] = '\\'; o[n++] = '\\'; }
			else if (*s != '\r') o[n++] = *s;
		}
	};
	put ("# Turtle Quest -- the players' progress (written by the game)\n", false);
	const char *last = "";
	for (int pass = 0; pass < 2; pass++)		// (the keys without a section first)
		for (int i = 0; i < g_kv.n; i++)
		{
			const KV &k = g_kv[i];
			if ((pass == 0) != (k.sec[0] == 0)) continue;
			if (pass == 1 && strcmp (last, k.sec)) { put ("\n[", false); put (k.sec, false); put ("]\n", false); last = k.sec; }
			put (k.key, false); put (" = ", false); put (k.val, true); put ("\n", false);
		}
	if (bas_save (PROGRESS, o, (int) n)) g_kvDirty = false;
	free (o);
}
static char *load_file (const char *path)
{
	int n; char *b = bas_load (path, &n);
	return b;
}
static void kv_load ()
{
	char *s = load_file (PROGRESS);
	if (!s) return;
	char sec[40] = "";
	for (char *p = s; *p; )
	{
		char *e = strchr (p, '\n'); if (e) *e = 0;
		char *l = p; p = e ? e + 1 : p + strlen (p);
		while (*l == ' ') l++;
		if (!*l || *l == '#') continue;
		if (*l == '[') { char *c = strchr (l, ']'); if (c) *c = 0; tcpy (sec, l + 1, sizeof sec); continue; }
		char *eq = strstr (l, " = "); if (!eq) continue;
		*eq = 0;
		char *v = eq + 3, *w = v;
		for (char *r = v; *r; r++)
		{
			if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
			else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r++; }
			else if (*r != '\r') *w++ = *r;
		}
		*w = 0;
		kv_set (sec, l, v);
	}
	delete [] s;
	g_kvDirty = false;
}
static char g_player[32] = "Player";
static int stars_of (const char *id) { return atoi (kv_get (g_player, id, "0")); }

// ---- the packs --------------------------------------------------------------------------------------------------------
enum { MAXPACKS = 12 };
static Pack *g_packs[MAXPACKS]; static int g_npacks = 0;
static int g_pack = 0, g_level = 0;
static Level *g_L = 0;					// the level played (or edited)
static Arr<Seg> g_target;				// a drawing level's figure

static void say (const char *m) { ui_message ((long) "Turtle Quest", (long) m, UM_OK); }
static bool add_pack (const char *path, bool user, bool quiet)
{
	for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) return true;
	if (g_npacks >= MAXPACKS) return false;
	char *src = load_file (path);
	if (!src) { if (!quiet) say (L2 ("This file cannot be read.", "Ce fichier ne peut pas être lu.")); return false; }
	Pack *pk = new Pack; char why[160];
	bool ok = parse_pack (*pk, src, why, sizeof why);
	delete [] src;
	if (!ok)
	{
		if (!quiet) { char m[300]; snprintf (m, sizeof m, "%s\n%s", L2 ("Not a pack of levels:", "Ce n'est pas un recueil de niveaux :"), why); say (m); }
		delete pk; return false;
	}
	tcpy (pk->path, path, sizeof pk->path); pk->user = user;
	if (!pk->title[0][0])
	{
		const char *b = strrchr (path, '/'); b = b ? b + 1 : path;
		tcpy (pk->title[0], b, sizeof pk->title[0]);
		char *dot = strrchr (pk->title[0], '.'); if (dot) *dot = 0;
	}
	g_packs[g_npacks++] = pk;
	return true;
}
static void load_packs ()
{
	char names[MAXPACKS][128]; int n = 0;
	DIR *d = opendir (APPDIR);
	if (d)
	{
		struct dirent *e;
		while ((e = readdir (d)) && n < MAXPACKS)
		{
			int l = (int) strlen (e->d_name);
			if (l < 8 || strcmp (e->d_name + l - 7, ".turtle")) continue;
			tcpy (names[n++], e->d_name, sizeof names[0]);
		}
		closedir (d);
	}
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--) { char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	for (int i = 0; i < n; i++) { char p[256]; snprintf (p, sizeof p, "%s/%s", APPDIR, names[i]); add_pack (p, false, true); }
	if (access (USERPACK, F_OK) == 0) add_pack (USERPACK, true, true);
}

// ---- the window's parts --------------------------------------------------------------------------------------------------
static long g_packBox, g_vList, g_listBar;
static long g_ed, g_btRun, g_btStep, g_btStop, g_btReset, g_btNext, g_btHint, g_btLesson, g_lbSpeed, g_speed, g_lesOk;
static long g_vWords, g_vCount, g_vCard, g_vBoard, g_vMsg, g_vLesson, g_vTools;
static long g_edPanel, g_edTitle, g_edText, g_edHint, g_edWords, g_edPar, g_edDraw, g_edConcept, g_edSize, g_edW[4], g_edBtn[3];
static const char *const CONCEPT_KEYS[] = { "move", "turn", "pick", "door", "repeat", "for", "if", "sensor", "while", "maze", "variable", "sub", "pen", "draw",
	"gems", "teleport", "color", "params", "function", "recursion" };
enum { NCONCEPTS = 20 };
enum { ED_FLAGS = UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_CODE | UTA_BASIC };
static bool g_editing = false, g_showHint = false, g_lessonOn = false, g_nextOn = false, g_tiny = false;
static const Concept *g_concept = 0;

// The pen's 16 colours (QBasic's, a little brighter on the board)
static const unsigned PEN[16] = { 0x202020, 0x2F6FD0, 0x2E9E44, 0x1E9FAF, 0xD0342C, 0x9C3FB5, 0x9A6234, 0xA8A8A8,
				  0x606060, 0x5B9BFF, 0x5CCB5F, 0x4FD8E8, 0xFF6B5E, 0xE07BEF, 0xF2C230, 0xFFFFFF };
static int g_badC = -1, g_badR = -1;			// the editor: the cell its check refused (ringed red; -1 none)
static int g_padLastC[2] = { -1, -1 }, g_padLastR[2] = { -1, -1 };	// the editor: each pair's pad placed last

// ---- the playback --------------------------------------------------------------------------------------------------------
enum { P_IDLE, P_RUN, P_PAUSE, P_DONE };
static Run g_run;
static World g_view;					// the world as shown: the events applied so far
static int g_mode = P_IDLE, g_idx = 0, g_line = 0;
static double g_t = 0;					// the event under way: 0 .. 1
static bool g_stepping = false;
static unsigned g_lastUs = 0;
static long g_caret = -1;				// where the caret was before a run (the run moves it, line by line)

static void refresh ();
static void relayout ();
static void show_level (int pack, int lv, bool keepCode);
static void refresh_count ();
static void set_message (int kind, const char *text, int stars = 0);
enum { M_NONE, M_INFO, M_OK, M_ERR };

static inline const char *code () { return (const char *) ui_w_get_text (g_ed); }

// The time an event takes at the speed chosen (ms); 0: at once
static double ev_ms (const Ev &e)
{
	static const double SCALE[10] = { 4, 2.6, 1.7, 1.15, 0.8, 0.55, 0.36, 0.22, 0.1, 0 };
	int s = (int) ui_w_get_value (g_speed); if (s < 1) s = 1; if (s > 10) s = 10;
	double k = SCALE[s - 1];
	switch (e.kind)
	{
	case EV_LINE: return 110 * k;
	case EV_MOVE: { double dx = e.c - e.a, dy = e.d - e.b; return 320 * k * sqrt (dx * dx + dy * dy); }
	case EV_TURN: { double d = fabs (e.b - e.a); if (d > 180) d = 360 - d; return 240 * k * (d / 90 < 0.4 ? 0.4 : d / 90); }
	case EV_BUMP: return k ? 420 * (k < 0.5 ? 0.5 : k) : 0;
	case EV_PICK: case EV_DOOR: return 260 * k;
	case EV_TELEPORT: return 420 * k;
	}
	return 0;
}

// ---- the level list ---------------------------------------------------------------------------------------------------------
enum { ROWH = 34, FOOTH = 30 };
static int g_top = 0, g_hot = -1;
static inline int list_count () { return g_pack < g_npacks ? g_packs[g_pack]->levels.n : 0; }
static int g_listW, g_listH;
static int list_rows () { int r = (g_listH - 4) / ROWH; return r > 0 ? r : 1; }
static bool list_bar () { return list_count () > list_rows (); }
static void list_clamp ()
{
	int n = list_count (), rows = list_rows ();
	if (g_top > n - rows) g_top = n - rows;
	if (g_top < 0) g_top = 0;
	ui_w_set_range (g_listBar, 0, n, rows, 1);
	ui_w_set_value (g_listBar, g_top);
}
static long list_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_BG);
	panel (0, 0, (int) w, (int) h, 6, C_FIELD, C_LINE);
	int n = list_count (), rows = list_rows ();
	int rw = (int) w - 4 - (n > rows ? 14 : 0);
	ui_clip (2, 2, w - 4, h - 4);
	for (int i = g_top; i < n && (i - g_top) * ROWH < h; i++)
	{
		const Level &L = *g_packs[g_pack]->levels[i];
		int y = 2 + (i - g_top) * ROWH, s = stars_of (L.id);
		bool sel = i == g_level && !g_editing;
		unsigned ink = C_TEXT, back = C_FIELD;
		if (sel) { back = C_ACCENT; ink = ink_on (back); rbox (4, y + 1, rw - 4, ROWH - 2, 5, back); }
		else if (i == g_hot) { back = mix (C_FIELD, C_ACCENT, 26); rbox (4, y + 1, rw - 4, ROWH - 2, 5, back); }
		// the number in a disc: filled once won
		unsigned dc = s ? jet (J_SUCCES) : mix (C_FIELD, C_TEXT, 64);
		disc (22, y + ROWH / 2, 11, dc);
		char num[8]; snprintf (num, sizeof num, "%d", i + 1);
		text_c (11, y + ROWH / 2 - 11, 22, 22, num, ink_on (dc), PX_UI, f_bold ());
		text_v (40, y, ROWH, L.titleOf (g_lang), ink, PX_UI, sel ? f_bold () : f_ui (), rw - 40 - 52);
		for (int k = 0; k < 3; k++) star (rw - 44 + k * 15, y + ROWH / 2, 6, k < s ? C_GOLD : mix (back, ink, 70), k < s);
	}
	// the pack's stars, at the foot of the list (when the levels leave room for it: no scrolling)
	if (n > 0 && n * ROWH + FOOTH <= h - 4)
	{
		int got = 0; for (int i = 0; i < n; i++) got += stars_of (g_packs[g_pack]->levels[i]->id);
		char f[80]; snprintf (f, sizeof f, L2 ("This pack: %d / %d", "Ce recueil : %d / %d"), got, 3 * n);
		if (tw (f) > w - 44) snprintf (f, sizeof f, "%d / %d", got, 3 * n);
		int fy = (int) h - 30;
		ui_fill (10, fy - 2, w - 20, 1, mix (C_FIELD, C_TEXT, 40));
		star (20, fy + 13, 6, got ? C_GOLD : mix (C_FIELD, C_TEXT, 70), got > 0);
		text_v (34, fy, 26, f, C_DIM);
	}
	ui_clip (0, 0, 0, 0);
	return 0;
}
static long list_mouse (long me, long kind, long, long y, long)
{
	int i = kind == 3 ? -1 : g_top + ((int) y - 2) / ROWH;
	if (i >= list_count ()) i = -1;
	if (i != g_hot) { g_hot = i; ui_view_dirty (me); }
	if (kind == 1 && i >= 0 && (i != g_level || g_editing)) show_level (g_pack, i, false);
	return 0;
}
static long list_wheel (long me, long n)
{
	g_top -= (int) n; list_clamp ();
	ui_view_dirty (me);
	return 0;
}
static void list_show_sel ()
{
	int rows = list_rows ();
	if (g_level < g_top) g_top = g_level;
	if (g_level >= g_top + rows) g_top = g_level - rows + 1;
	list_clamp ();
	ui_view_dirty (g_vList);
}

// ---- the board ---------------------------------------------------------------------------------------------------------------
static const char TOOL_CH[] = { '#', '.', '*', 'k', 'c', 'D', 'p', '>', ' ', '1', 'T', 'U' };
enum { NTOOLS = 12, TOOL_TURTLE = 7, TOOL_GEM = 9, TOOL_PAD1 = 10, TOOL_PAD2 = 11 };
static int g_tool = 0;
static void edit_cell (int c, int r, bool drag = false);

// The board's colours: the floor's are the game's, laid on the theme's surface (dark themes: a dark floor)
struct BoardInk { unsigned grassT, grassB, tile0, tile1, wallT, wallB, wallL, page, pageL, dot; };
static BoardInk g_bi;
static void board_ink ()
{
	bool dk = dark_theme ();
	unsigned s = C_FIELD;
	int a = dk ? 90 : 225;
	g_bi.grassT = mix (s, 0x9CCB7E, a); g_bi.grassB = mix (s, 0x86B96A, a);
	g_bi.tile0 = mix (s, 0xF4EBCD, dk ? 74 : 240); g_bi.tile1 = mix (s, 0xE8DCB6, dk ? 84 : 240);
	g_bi.wallT = mix (s, 0x8E9AAA, dk ? 170 : 245); g_bi.wallB = mix (s, 0x6C7889, dk ? 170 : 245); g_bi.wallL = mix (s, 0x7C889A, dk ? 200 : 245);
	g_bi.page = s; g_bi.pageL = mix (s, C_TEXT, 28);
	g_bi.dot = mix (s, C_TEXT, 44);
}
static unsigned tile_of (int c, int r) { return (c + r) & 1 ? g_bi.tile1 : g_bi.tile0; }

static int g_cs = 40, g_ox = 0, g_oy = 0;		// a cell's size, the grid's place
static void board_fit (int width, int height)
{
	if (!g_L) return;
	int w = g_L->w, h = g_L->h;
	g_cs = (width - 16) / w; int c2 = (height - 16) / h; if (c2 < g_cs) g_cs = c2;
	if (g_cs > 64) g_cs = 64;
	if (g_cs < 8) g_cs = 8;
	g_ox = (width - g_cs * w) / 2; g_oy = (height - g_cs * h) / 2;
}
static inline double px (double x) { return g_ox + x * g_cs + g_cs / 2; }
static inline double py (double y) { return g_oy + y * g_cs + g_cs / 2; }
// A pen's colour on the board: on a page, as it is (lifted when the page would hide it: a theme's page is not always
// white); a trail on the floor, softer
static unsigned ink_pen (int c, int cx = 0, int cy = 0)
{
	unsigned p = PEN[c & 15];
	if (!g_L || !g_L->draw) return mix (tile_of (cx, cy), p, 190);
	int d = luma (p) - luma (g_bi.page);
	return d > -48 && d < 48 ? mix (p, C_TEXT, 150) : p;
}
static void tile (int c, int r, char k, const World &w, double pickT, int pickCell)
{
	int x = g_ox + c * g_cs, y = g_oy + r * g_cs, s = g_cs;
	bool draw = g_L->draw;
	if (k == ' ') return;
	if (k == '#')
	{
		rbox (x + 1, y + 1, s - 2, s - 2, s / 8, g_bi.wallT, g_bi.wallB);
		ui_fill (x + s / 5, y + s / 2, s * 3 / 5, 1, g_bi.wallL);
		return;
	}
	if (draw) { ui_fill (x + s / 2, y + s / 2, 2, 2, g_bi.dot); return; }
	unsigned fl = tile_of (c, r);
	ui_fill (x, y, s, s, fl);
	if (k == 'p')
	{
		unsigned dash = mix (fl, 0xB06BD8, 220);
		if (w.painted[r][c]) ui_fill (x + 2, y + 2, s - 4, s - 4, mix (fl, PEN[w.color & 15], 150));
		for (int q = 3; q < s - 3; q += 6) { ui_fill (x + q, y + 2, 3, 2, dash); ui_fill (x + q, y + s - 4, 3, 2, dash); ui_fill (x + 2, y + q, 2, 3, dash); ui_fill (x + s - 4, y + q, 2, 3, dash); }
	}
	int cx = x + s / 2, cy = y + s / 2;
	double sc = (pickCell == c + r * 256) ? 1 - pickT : 1;
	if (k == '*')
	{
		unsigned pole = dark_theme () ? 0xC8AE86 : 0x5A4632;	// (the pole stands out of the floor)
		disc (cx - s / 6, y + s * 5 / 6, s / 8 + 1, mix (fl, pole, 120));
		seg (cx - s / 6, y + s / 6, cx - s / 6, y + s * 5 / 6, s < 24 ? 1.5 : 2, pole);
		int f[6] = { V (cx - s / 6), V (y + s / 6), V (cx + s / 3), V (y + s / 6 + s / 8), V (cx - s / 6), V (y + s / 6 + s / 4) };
		ui_vp_poly ((long) f, 3, 0xE0483E);
	}
	else if (k == 'c' && sc > 0.02)
	{
		double rr = (int) (s * 0.24 * sc) + 1;
		disc (cx, cy, rr, 0xC8961E);
		disc (cx, cy, rr * 3 / 4, 0xF6CF3C);
		disc (cx - rr / 3, cy - rr / 3, rr / 4, 0xFFF3B0);
	}
	else if (k == 'k' && sc > 0.02)
	{
		double rr = (int) (s * 0.14 * sc) + 1;
		disc (cx - rr, cy, rr, 0xD9A21B);
		disc (cx - rr, cy, rr / 2, fl);
		seg (cx - rr / 2, cy, cx + rr * 2, cy, rr * 3 / 5, 0xD9A21B);
		seg (cx + rr * 3 / 2, cy, cx + rr * 3 / 2, cy + rr, rr / 2, 0xD9A21B);
	}
	else if (k == 'T' || k == 'U') pad (cx, cy, s, k - 'T');
	else if (k == 'D')
	{
		rbox (x + 2, y + 2, s - 4, s - 4, s / 6, 0xB0773F, 0x8A5A2B);
		ui_fill (x + s / 2, y + 4, 1, s - 8, 0x6E4520);
		disc (cx + s / 5, cy - s / 16, s / 14 + 1, 0x2A1A0C);
		seg (cx + s / 5, cy, cx + s / 5, cy + s / 8, s / 18 + 1, 0x2A1A0C);
	}
}
// A gem, over the pen's trail: the next one to pick ringed (not in the editor); a gem being picked shrinks away
static void gem_tile (int c, int r, char k, const World &w, double pickT, int pickCell, bool edit)
{
	if (k < '1' || k > '9' || g_L->draw) return;
	double sc = (pickCell == c + r * 256) ? 1 - pickT : 1;
	if (sc <= 0.02) return;
	int cx = g_ox + c * g_cs + g_cs / 2, cy = g_oy + r * g_cs + g_cs / 2;
	if (!edit && k - '0' == w.gems + 1 && pickCell != c + r * 256)
	{
		ring_line (cx, cy, g_cs * 0.44, 3.5, 0x6A5A30);
		ring_line (cx, cy, g_cs * 0.44, 2.2, 0xFFFFFF);
	}
	gem (cx, cy, g_cs, k - '0', sc);
}
static void draw_turtle (double x, double y, double hd, double k = 1)	// (k: its size -- the jump shrinks it)
{
	if (k < 0.04) return;
	double cx = px (x), cy = py (y), s = g_cs * k;
	auto at = [&] (double deg, double dist, double &ox, double &oy) {
		double a = (hd + deg) * DEG;
		ox = cx + sin (a) * dist * s; oy = cy - cos (a) * dist * s;
	};
	double lx, ly;
	for (int i = 0; i < 4; i++)
	{
		static const int LEG[4] = { 45, -45, 135, -135 };
		at (LEG[i], 0.33, lx, ly); disc (lx, ly, s / 9, 0x6FB86A);
	}
	at (180, 0.38, lx, ly); disc (lx, ly, s / 16, 0x6FB86A);
	at (0, 0.40, lx, ly); disc (lx, ly, s * 0.15, 0x7CC576);
	at (-16, 0.45, lx, ly); disc (lx, ly, s / 34 + 0.5, 0x1E2A1E);
	at (16, 0.45, lx, ly); disc (lx, ly, s / 34 + 0.5, 0x1E2A1E);
	disc (cx, cy, s * 0.34, 0x2F7A35);
	disc (cx, cy, s * 0.27, 0x4FA052);
	for (int i = 0; i < 6; i++) { at (i * 60 + 30, 0.19, lx, ly); disc (lx, ly, s / 19, 0x3B8A3F); }
	disc (cx, cy, s / 10, 0x5DB560);
}
static long board_paint (long, long width, long height)
{
	ui_fill (0, 0, width, height, C_BG);
	if (!g_L) return 0;
	board_ink ();
	board_fit ((int) width, (int) height);
	bool draw = g_L->draw;
	bool edit = g_editing && g_mode == P_IDLE;		// (the editor's map; its Test plays as a level)
	if (draw) panel (0, 0, (int) width, (int) height, 10, g_bi.page, C_LINE);
	else rbox (0, 0, (int) width, (int) height, 10, g_bi.grassT, g_bi.grassB);
	const World &w = g_view;
	// the event under way
	const Ev *e = (g_mode == P_RUN || g_mode == P_PAUSE) && g_idx < g_run.ev.n ? &g_run.ev[g_idx] : 0;
	double t = g_t;
	int pickCell = e && (e->kind == EV_PICK || e->kind == EV_DOOR) ? e->i : -1;
	for (int r = 0; r < g_L->h; r++)
		for (int c = 0; c < g_L->w; c++)
		{
			char k = edit ? g_L->map[r][c] : w.cell[r][c];
			if (edit && (k == '>' || k == '<' || k == '^' || k == 'v')) k = '.';
			tile (c, r, k, w, t, pickCell);
		}
	if (draw && g_target.n)				// the figure to reproduce
		for (int i = 0; i < g_target.n; i++)
		{
			const Seg &s = g_target[i];
			seg (px (s.x1), py (s.y1), px (s.x2), py (s.y2), g_cs / 6 + 3, g_L->draw == 2 ? mix (g_bi.page, PEN[s.color & 15], 85) : g_bi.pageL);	// (a colour level: each line tinted)
		}
	// the pen's lines
	double lw = draw ? 3 : g_cs / 10 + 2;
	for (int i = 0; i < w.segs.n; i++)
	{
		const Seg &s = w.segs[i];
		seg (px (s.x1), py (s.y1), px (s.x2), py (s.y2), lw, ink_pen (s.color, (int) s.x1, (int) s.y1));
	}
	double tx = w.x, ty = w.y, th = w.h;
	if (e && e->kind == EV_MOVE)
	{
		tx = e->a + (e->c - e->a) * t; ty = e->b + (e->d - e->b) * t;
		if (w.pen) seg (px (e->a), py (e->b), px (tx), py (ty), lw, ink_pen (w.color, (int) e->a, (int) e->b));
	}
	else if (e && e->kind == EV_TURN)
	{
		double d = e->b - e->a; while (d > 180) d -= 360; while (d < -180) d += 360;
		th = e->a + d * t;
	}
	else if (e && e->kind == EV_BUMP)
	{
		double k = sin (t * 3.14159) * 0.28;
		tx = e->a + (e->c - e->a) * k; ty = e->b + (e->d - e->b) * k;
	}
	for (int r = 0; r < g_L->h; r++)		// the gems, over the trail (their numbers stay readable)
		for (int c = 0; c < g_L->w; c++) gem_tile (c, r, edit ? g_L->map[r][c] : w.cell[r][c], w, t, pickCell, edit);
	double tk = 1;
	if (e && e->kind == EV_TELEPORT)		// the jump: into pad A, a dotted arc, out of its twin B -- no pen line
	{
		int q = w.pad_at ((int) e->a, (int) e->b) == 2 ? 1 : 0;
		double ax = px (e->a), ay = py (e->b), bx = px (e->c), by = py (e->d);
		double lift = sqrt ((bx - ax) * (bx - ax) + (by - ay) * (by - ay)) / 3;
		double mx = (ax + bx) / 2.0, my = (ay + by) / 2.0 - lift;
		for (int i = 1; i < 24; i++)
		{
			double u = i / 24.0;
			if (u > t + 0.04) break;
			double x = (1 - u) * (1 - u) * ax + 2 * u * (1 - u) * mx + u * u * bx, y = (1 - u) * (1 - u) * ay + 2 * u * (1 - u) * my + u * u * by;
			disc (x, y, 2.25, PAD_RING[q]);
		}
		if (t < 0.5) { pad ((int) ax, (int) ay, g_cs, q, t * 2, g_bi.tile0); tx = e->a; ty = e->b; tk = 1 - t * 2; }
		else { pad ((int) bx, (int) by, g_cs, q, (1 - t) * 2, g_bi.tile0); tx = e->c; ty = e->d; tk = t * 2 - 1; }
	}
	if (edit && g_badC >= 0 && g_badC < g_L->w && g_badR >= 0 && g_badR < g_L->h) ring_line (px (g_badC), py (g_badR), g_cs * 0.44, 3, jet (J_DANGER));
	if (edit)
	{
		for (int r = 0; r < g_L->h; r++) for (int c = 0; c < g_L->w; c++)
		{
			char k = g_L->map[r][c];
			if (k == '>' || k == '<' || k == '^' || k == 'v') draw_turtle (c, r, k == '^' ? 0 : k == '>' ? 90 : k == 'v' ? 180 : 270);
		}
		unsigned gl = mix (draw ? g_bi.page : g_bi.grassB, C_TEXT, 60);
		for (int c = 0; c <= g_L->w; c++) ui_fill (g_ox + c * g_cs, g_oy, 1, g_cs * g_L->h, gl);
		for (int r = 0; r <= g_L->h; r++) ui_fill (g_ox, g_oy + r * g_cs, g_cs * g_L->w, 1, gl);
	}
	else draw_turtle (tx, ty, th, tk);
	if (e && e->kind == EV_BUMP && t > 0.3 && t < 0.9)	// a star where it bumped
		star ((int) px (e->a + (e->c - e->a) * 0.5), (int) py (e->b + (e->d - e->b) * 0.5), g_cs / 4 + 2, 0xFFD23F, true);
	// what the turtle carries
	if (!g_editing && (w.keys || w.coinsTotal || w.gemsTotal))	// (Coins, a stone and Gems, Keys: what applies)
	{
		char co[48] = "", ge[48] = "", ke[48] = "";
		if (w.coinsTotal) snprintf (co, sizeof co, "%s %d / %d", L2 ("Coins", "Pièces"), w.coins, w.coinsTotal);
		if (w.gemsTotal) snprintf (ge, sizeof ge, "%s %d / %d", L2 ("Gems", "Gemmes"), w.gems, w.gemsTotal);
		if (w.keys) snprintf (ke, sizeof ke, "%s %d", L2 ("Keys", "Clés"), w.keys);
		const int GAP = 18, ICON = 20;
		int bw = 20, parts = 0;
		const char *part[3] = { co, ge, ke };
		for (int i = 0; i < 3; i++) if (part[i][0]) { bw += (parts ? GAP : 0) + tw (part[i], PX_UI, f_bold ()) + (i == 1 ? ICON : 0); parts++; }
		int bx = (int) width - bw - 8, x = bx + 10;
		panel (bx, 8, bw, 24, 12, C_FIELD, C_LINE);
		parts = 0;
		for (int i = 0; i < 3; i++)
		{
			if (!part[i][0]) continue;
			if (parts++) x += GAP;
			if (i == 1)				// (the next gem's stone; all picked: a check mark)
			{
				if (w.gems < w.gemsTotal) gem (x + 8, 20, 22, w.gems + 1, 1, false);
				else { seg (x + 3, 20, x + 7, 24, 2, jet (J_SUCCES)); seg (x + 7, 24, x + 14, 15, 2, jet (J_SUCCES)); }
				x += ICON;
			}
			text_v (x, 8, 24, part[i], C_TEXT, PX_UI, f_bold ());
			x += tw (part[i], PX_UI, f_bold ());
		}
	}
	return 0;
}
static bool g_boardDown = false, g_boardDrag = false;
static long board_mouse (long, long kind, long mx, long my, long info)
{
	if (!g_editing || !g_L) return 0;
	if (kind == 3) { g_boardDown = g_boardDrag = false; return 0; }
	int c = ((int) mx - g_ox) / g_cs, r = ((int) my - g_oy) / g_cs;
	bool in = mx >= g_ox && my >= g_oy && c < g_L->w && r < g_L->h;
	// (a press, then a drag: the turtle and the portals are placed by a click only; a drag of gems never cycles one)
	if (kind == 1) { g_boardDown = true; if (in) { edit_cell (c, r); g_boardDrag = g_tool != TOOL_TURTLE && g_tool != TOOL_PAD1 && g_tool != TOOL_PAD2; } }
	else if (kind == 2) g_boardDown = g_boardDrag = false;
	else if (g_boardDown && g_boardDrag && in && ((info >> 8) & 1) && (g_tool == TOOL_GEM ? !(g_L->map[r][c] >= '1' && g_L->map[r][c] <= '9') : g_L->map[r][c] != TOOL_CH[g_tool])) edit_cell (c, r, true);
	return 0;
}

// ---- the words the level knows: a click writes one ------------------------------------------------------------------------------
static const char *const CONTROL_WORDS[] = { "REPEAT", "FOR", "IF", "WHILE", "SUB", "FUNCTION" };
static const char *const CONTROL_FR[] = { "REPETER", "POUR", "SI", "TANTQUE", "SUB", "FONCTION" };
struct Chip { int x, y, w, id; char text[24]; };
enum { CHIPH = 24, MAXCHIPS = 32 };
static Chip g_chips[MAXCHIPS]; static int g_nchips = 0, g_chipHot = -1;
static void words_build (int width)
{
	g_nchips = 0;
	if (!g_L) return;
	int x = 0, y = 0;
	auto add = [&] (const char *t, int id) {
		if (g_nchips >= MAXCHIPS) return;
		int w = tw (t, PX_SMALL, f_bold ()) + 18;
		if (x + w > width && x > 0) { x = 0; y += CHIPH + 5; }
		Chip &c = g_chips[g_nchips++]; c.x = x; c.y = y; c.w = w; c.id = id; tcpy (c.text, t, sizeof c.text);
		x += w + 5;
	};
	for (int i = 0; WORDS[i].name; i++) if (g_L->knows (WORDS[i].id)) add (word_name (WORDS[i].id, g_lang), WORDS[i].id);
	// the control words the level's idea brings (and the ones before it)
	static const char *const ORDER[] = { "repeat", "for", "if", "sensor", "while", "maze", "variable", "sub", "pen", "draw",
		"gems", "teleport", "color", "params", "function", "recursion", 0 };
	int lv = -1; for (int i = 0; ORDER[i]; i++) if (!strcmp (ORDER[i], g_L->topic)) lv = i;
	const int NEED[6] = { 0, 1, 2, 4, 7, 14 };	// REPEAT from "repeat", FOR from "for", IF "if", WHILE "while", SUB "sub",
							// FUNCTION "function" (and "recursion" after it)
	for (int k = 0; k < 6; k++) if (lv >= NEED[k]) add (g_lang == LANG_FR ? CONTROL_FR[k] : CONTROL_WORDS[k], 100 + k);
}
static long words_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_BG);
	for (int i = 0; i < g_nchips; i++)
	{
		const Chip &c = g_chips[i];
		unsigned edge = c.id >= 100 ? C_ACCENT : jet (J_SUCCES);
		unsigned face = mix (C_FIELD, edge, i == g_chipHot ? 90 : 40);
		panel (c.x, c.y, c.w, CHIPH, 12, face, edge);
		text_c (c.x, c.y, c.w, CHIPH, c.text, C_TEXT, PX_SMALL, f_bold ());
	}
	return 0;
}
static long words_mouse (long me, long kind, long mx, long my, long)
{
	int h = -1;
	if (kind != 3) for (int i = 0; i < g_nchips; i++) if (mx >= g_chips[i].x && mx < g_chips[i].x + g_chips[i].w && my >= g_chips[i].y && my < g_chips[i].y + CHIPH) h = i;
	if (h != g_chipHot)
	{
		g_chipHot = h; ui_view_dirty (me);
		static char tip[200];
		if (h >= 0 && g_chips[h].id < 100) { tcpy (tip, word_help (g_chips[h].id, g_lang), sizeof tip); ui_w_tip (me, (long) tip); }
		else ui_w_tip (me, 0);
	}
	if (kind == 1 && h >= 0 && g_mode != P_RUN && g_mode != P_PAUSE)
	{
		const Chip &c = g_chips[h];
		char ins[160];
		if (c.id >= 100)
		{
			static const char *const TPL_EN[] = { "REPEAT 4\n  \nEND REPEAT", "FOR i = 1 TO 5\n  \nNEXT", "IF  THEN\n  \nEND IF", "WHILE NOT ONGOAL ()\n  \nWEND", "SUB MyWord\n  \nEND SUB", "FUNCTION MyValue (x)\n  MyValue = x\nEND FUNCTION" };
			static const char *const TPL_FR[] = { "REPETER 4\n  \nFIN REPETER", "POUR i = 1 JUSQUE 5\n  \nSUITE", "SI  ALORS\n  \nFIN SI", "TANTQUE NON SURBUT ()\n  \nFIN TANTQUE", "SUB MonMot\n  \nFIN SUB", "FONCTION MaValeur (x)\n  MaValeur = x\nFIN FONCTION" };
			tcpy (ins, (g_lang == LANG_FR ? TPL_FR : TPL_EN)[c.id - 100], sizeof ins);
		}
		else
		{
			bool fn = false; for (int i = 0; WORDS[i].name; i++) if (WORDS[i].id == c.id) fn = WORDS[i].kind != 's';
			snprintf (ins, sizeof ins, fn ? "%s ()" : "%s ", c.text);
		}
		ui_textarea_insert (g_ed, (long) ins);
		ui_w_focus (g_ed);
		refresh_count ();
	}
	return 0;
}

// ---- the instructions' count, the stars' thresholds ---------------------------------------------------------------------------
static long count_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_BG);
	if (!g_L) return 0;
	char t[80]; snprintf (t, sizeof t, L2 ("Instructions: %d", "Instructions : %d"), count_instructions (code ()));
	text_v (0, 0, (int) h, t, C_DIM);
	int x = tw (t) + 18;
	auto stars = [&] (int n, int val) {
		for (int i = 0; i < n; i++) { star (x + 6, (int) h / 2, 6, C_GOLD, true); x += 13; }
		char v[24]; snprintf (v, sizeof v, " \xE2\x89\xA4 %d", val);
		text_v (x, 0, (int) h, v, C_DIM);
		x += tw (v) + 12;
	};
	if (g_L->par3) { stars (3, g_L->par3); stars (2, g_L->par2); }
	if (!g_editing)
	{
		char bk[64]; snprintf (bk, sizeof bk, "%s.best", g_L->id);
		int best = atoi (kv_get (g_player, bk, "0"));
		if (best > 0) { snprintf (t, sizeof t, L2 ("Best: %d", "Record : %d"), best); text_v (x, 0, (int) h, t, C_DIM, PX_UI, 0, (int) w - x); }
	}
	return 0;
}
static void refresh_count () { ui_view_dirty (g_vCount); }

// ---- the level's card: its title, what to do, the hint -----------------------------------------------------------------------------
static bool g_cardBelow = false;			// a narrow window: the card's Hint and Lesson under its text
static const char *card_text ()
{
	return g_editing ? L2 ("Draw the level with the tools on the left, write a solution, Test it (it sets the instructions for the stars), then Save.",
		"Dessine le niveau avec les outils à gauche, écris une solution, Teste-la (elle fixe les instructions pour les étoiles), puis Enregistre.")
		: g_L->textOf (g_lang);
}
static void hint_text (char *h, int cap) { snprintf (h, cap, "%s %s", L2 ("Hint:", "Indice :"), g_L->hintOf (g_lang)); }
static int card_room (int width) { return width - 28 - (g_editing || g_cardBelow ? 104 : 274); }	// (the window's buttons at the top right)
static long card_paint (long, long width, long height)
{
	ui_fill (0, 0, width, height, C_BG);
	if (!g_L) return 0;
	char t[160];
	if (g_editing) snprintf (t, sizeof t, "%s -- %s", L2 ("Level editor", "Éditeur de niveaux"), g_L->titleOf (g_lang));
	else snprintf (t, sizeof t, "%d. %s", g_level + 1, g_L->titleOf (g_lang));
	int room = card_room ((int) width);
	int px_ = tw (t, PX_BIG, f_bold ()) <= room ? PX_BIG : 14;
	text (14, 9, t, C_TEXT, px_, f_bold (), room);
	int y = 10 + fh (PX_BIG) + 2;
	y += wrap (card_text (), 14, y, room, C_DIM);
	if (g_showHint && !g_editing)
	{
		char h[520]; hint_text (h, sizeof h);
		wrap (h, 14, y + 2, (int) width - 28, mix (jet (J_ALERTE), C_TEXT, 120));
	}
	return 0;
}
static int card_need (int width)
{
	if (!g_L) return 80;
	int h = 10 + fh (PX_BIG) + 2 + wrap (card_text (), 0, 0, card_room (width), 0, PX_UI, 0, true) + 8;
	if (g_showHint && !g_editing) { char hh[520]; hint_text (hh, sizeof hh); h += 2 + wrap (hh, 0, 0, width - 28, 0, PX_UI, 0, true); }
	if (g_cardBelow && !g_editing) h += 34;		// (Hint and Lesson under the text)
	return h < 76 ? 76 : h;
}

// ---- the message under the board -----------------------------------------------------------------------------------------------
static int g_msgKind = M_NONE, g_msgStars = 0; static char g_msgText[300] = "";
static long msg_paint (long, long width, long height)
{
	ui_fill (0, 0, width, height, C_BG);
	if (g_msgKind == M_NONE || !g_msgText[0]) return 0;
	unsigned edge = g_msgKind == M_OK ? jet (J_SUCCES) : g_msgKind == M_ERR ? jet (J_DANGER) : C_ACCENT;
	panel (0, 0, (int) width, (int) height, 10, mix (C_FIELD, edge, 28), edge);
	int x = 14, cy = (int) height / 2;
	if (g_msgKind == M_OK && g_msgStars) for (int k = 0; k < 3; k++) { star (x + 11, cy, 11, k < g_msgStars ? C_GOLD : mix (C_FIELD, C_TEXT, 50), true); x += 26; }
	else
	{
		disc (x + 10, cy, 10, edge);
		if (g_msgKind == M_OK) { seg (x + 5, cy, x + 9, cy + 4, 2, ink_on (edge)); seg (x + 9, cy + 4, x + 16, cy - 5, 2, ink_on (edge)); }
		else text_c (x, cy - 10, 20, 20, g_msgKind == M_ERR ? "!" : "i", ink_on (edge), PX_UI, f_bold ());
		x += 28;
	}
	int room = (int) width - x - 18 - (g_nextOn ? 140 : 0);
	const char *nl = strchr (g_msgText, '\n');
	if (nl)
	{
		char head[200]; int n = (int) (nl - g_msgText); if (n > 199) n = 199; memcpy (head, g_msgText, n); head[n] = 0;
		int need = fh () + 2 + wrap (nl + 1, 0, 0, room, 0, PX_UI, 0, true);
		int y = (int) (height - need) / 2; if (y < 3) y = 3;
		text (x + 6, y, head, C_TEXT, PX_UI, f_bold (), room);
		wrap (nl + 1, x + 6, y + fh () + 2, room, C_TEXT);
	}
	else
	{
		int need = wrap (g_msgText, 0, 0, room, 0, PX_UI, 0, true);
		int y = (int) (height - need) / 2; if (y < 3) y = 3;
		wrap (g_msgText, x + 6, y, room, C_TEXT);
	}
	return 0;
}
static void set_message (int kind, const char *text_, int stars)
{
	g_msgKind = kind; g_msgStars = stars; tcpy (g_msgText, text_, sizeof g_msgText);
	ui_view_dirty (g_vMsg);
}

// ---- the lesson: a new idea's card, over the board --------------------------------------------------------------------------------
// The card's text laid out from y 58 (drawn when draw): where it ends (past height - 56: it does not fit)
static int lesson_flow (int width, int height, bool draw, int px_)
{
	int y = 58;
	const char *s = g_concept->text[g_lang];
	while (*s)
	{
		const char *e = strchr (s, '\n'); int n = e ? (int) (e - s) : (int) strlen (s);
		char line[400]; if (n > 399) n = 399; memcpy (line, s, n); line[n] = 0;
		s = e ? e + 1 : s + n;
		if (line[0] == ' ' && line[1] == ' ')		// code: in the monospaced face, on a tint
		{
			if (draw && y < height - 56)
			{
				ui_fill (18, y - 1, width - 36, fh (px_) + 3, mix (C_FIELD, C_ACCENT, 28));
				text (24, y, line + 2, jet (J_TEXTE1), px_, f_mono ());
			}
			y += fh (px_) + 3;
			continue;
		}
		if (!line[0]) { y += px_ == PX_UI ? 8 : 6; continue; }
		if (!strcmp (line, "@palette"))		// the pen's 16 colours, each with its number (a narrow box: two rows of 8)
		{
			int per = (width - 40) / 16 >= 20 ? 16 : 8, sw = (width - 40) / per, sh = px_ == PX_UI ? 22 : 18;
			for (int i = 0; i < 16; i++)
			{
				int x = 20 + (i % per) * sw, yy = y + 2 + (i / per) * (sh + 3);
				if (!draw || yy >= height - 56) continue;
				panel (x, yy, sw - 2, sh, 4, PEN[i], C_LINE);
				char d[4]; snprintf (d, sizeof d, "%d", i);
				text_c (x, yy, sw - 2, sh, d, ink_on (PEN[i]), sw < 24 ? PX_SMALL : PX_UI, f_bold ());
			}
			y += (16 / per) * (sh + 3) + 7; continue;
		}
		y += wrap (line, 20, y, width - 40, C_TEXT, px_, 0, !draw || y >= height - 56) + 3;
	}
	return y;
}
static long lesson_paint (long, long width, long height)
{
	ui_fill (0, 0, width, height, C_BG);
	if (!g_concept) return 0;
	ui_fill (0, 0, width, height, C_ACCENT);
	ui_fill (1, 44, width - 2, height - 45, C_FIELD);
	char t[120]; snprintf (t, sizeof t, "%s  %s", L2 ("New idea:", "Nouvelle idée :"), g_concept->title[g_lang]);
	if (tw (t, PX_BIG, f_bold ()) > width - 36) tcpy (t, g_concept->title[g_lang], sizeof t);
	text_v (18, 0, 44, t, ink_on (C_ACCENT), PX_BIG, f_bold (), (int) width - 36);
	// the text; smaller when it would not fit above OK (a narrow window)
	bool small = lesson_flow ((int) width, (int) height, false, PX_UI) > height - 56;
	lesson_flow ((int) width, (int) height, true, small ? PX_SMALL : PX_UI);
	return 0;
}
static void show_lesson (bool show)
{
	if (show && g_L) g_concept = find_concept (g_L->topic);
	bool on = show && g_concept;
	if (on)
	{
		char k[40]; snprintf (k, sizeof k, "seen.%s", g_L->topic);
		kv_set (g_player, k, "1"); kv_save ();
	}
	if (on == g_lessonOn) return;
	g_lessonOn = on;
	ui_w_show (g_vLesson, on);
	ui_view_dirty (g_vLesson);
	refresh ();
}

// ---- the level editor's tools ----------------------------------------------------------------------------------------------------
enum { TOOLCELLH = 34 };
static long tools_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_BG);
	static const char *const EN[NTOOLS] = { "Wall", "Floor", "Flag", "Key", "Coin", "Door", "Paint", "Turtle", "Water", "Gem", "Portal 1", "Portal 2" };
	static const char *const FR[NTOOLS] = { "Mur", "Sol", "Drapeau", "Clé", "Pièce", "Porte", "Peinture", "Tortue", "Eau", "Gemme", "Portail 1", "Portail 2" };
	static const unsigned SW[9] = { 0x7C889A, 0xF0E6C6, 0xE0483E, 0xD9A21B, 0xF6CF3C, 0x9A6234, 0xB06BD8, 0x4FA052, 0x4A90C8 };
	int c = ((int) w - 2 * 4) / 3;
	for (int i = 0; i < NTOOLS; i++)
	{
		int x = (i % 3) * (c + 4), y = (i / 3) * TOOLCELLH;
		if (i == g_tool) rbox (x, y, c, 30, 6, C_ACCENT);
		else panel (x, y, c, 30, 6, jet (J_ELEVE), C_LINE);
		if (i == TOOL_GEM) gem (x + 11, y + 15, 18, 1, 1, false);
		else if (i == TOOL_PAD1 || i == TOOL_PAD2) pad (x + 11, y + 15, 18, i - TOOL_PAD1);
		else rbox (x + 5, y + 9, 11, 12, 3, SW[i]);
		const char *nm = g_lang == LANG_FR ? FR[i] : EN[i];
		text_v (x + 19, y, 30, nm, i == g_tool ? ink_on (C_ACCENT) : C_TEXT, tw (nm) > c - 22 ? PX_SMALL : PX_UI, 0, c - 21);
	}
	return 0;
}
static long tools_mouse (long me, long kind, long mx, long my, long)
{
	long w, h; uikit_canvas_pixels (me, &w, &h);
	if (kind != 1) return 0;
	int c = ((int) w - 2 * 4) / 3, i = (int) (my / TOOLCELLH) * 3 + (int) mx / (c + 4);
	if (i >= 0 && i < NTOOLS) { g_tool = i; ui_view_dirty (me); }
	return 0;
}

// ---- the run -------------------------------------------------------------------------------------------------------------------
// (a line, 1-based, of the program: lit, 0: none)
static void hi_line (int line) { ui_textarea_highlight (g_ed, line > 0 ? line - 1 : -1); }
static long line_offset (int ln)			// (0-based)
{
	const char *t = code (); long p = 0;
	for (; ln > 0 && t[p]; p++) if (t[p] == '\n') ln--;
	return p;
}
static void show_line (int line) { ui_textarea_goto (g_ed, line_offset (line - 1)); }
static void caret_keep ()
{
	long col, l = uikit_textarea_caret (g_ed, &col);
	g_caret = line_offset ((int) l) + col;
}
static void caret_back () { if (g_caret >= 0) { ui_textarea_goto (g_ed, g_caret); g_caret = -1; } }
static void set_running (bool on)
{
	ui_w_style (g_ed, ED_FLAGS | (on ? UTF_READONLY : 0));
	ui_w_enable (g_btRun, !(on && g_mode == P_RUN));
	ui_w_enable (g_btStop, on);
}
static void show_next (bool on)
{
	if (on == g_nextOn) return;
	g_nextOn = on;
	ui_w_show (g_btNext, on);
	ui_view_dirty (g_vMsg);
}
static void reset_view ()
{
	if (g_L) g_view.reset (*g_L);
	g_mode = P_IDLE; g_idx = 0; g_t = 0; g_line = 0; g_stepping = false;
	hi_line (0);
	show_next (false);
	set_running (false);
	ui_view_dirty (g_vBoard);
}
static void save_code ()
{
	if (!g_L || g_editing) return;
	char k[64]; snprintf (k, sizeof k, "%s.code", g_L->id);
	kv_set (g_player, k, code ());
	kv_save ();
}
// The record made from the program, the playback started (step: a statement at a time)
static bool start_run (bool step)
{
	if (!g_L) return false;
	show_lesson (false);
	reset_view ();
	save_code ();
	run_program (*g_L, code (), g_lang, g_run, g_L->draw ? &g_target : 0);
	if (g_run.result == R_COMPILE)
	{
		hi_line (g_run.errLine); show_line (g_run.errLine);
		set_message (M_ERR, g_run.msg);
		return false;
	}
	set_message (M_INFO, step ? L2 ("Step by step: F8 (or Step) runs the lit line.", "Pas à pas : F8 (ou Pas) exécute la ligne éclairée.") : L2 ("Running...", "En route..."));
	caret_keep ();
	g_mode = P_RUN; g_stepping = step; g_lastUs = 0;
	set_running (true);
	return true;
}
static void finish_run ()
{
	g_mode = P_DONE; g_line = 0;
	set_running (false);
	hi_line (0);
	caret_back ();
	const Run &R = g_run;
	if (R.result == R_WON)
	{
		const char *m = R.stars == 3 ? L2 ("Perfect!", "Parfait !") : R.stars == 2 ? L2 ("Well done!", "Bravo !") : L2 ("You made it!", "Réussi !");
		char more[160] = "";
		if (R.stars < 3 && g_L->par3)
			snprintf (more, sizeof more, L2 ("%d instructions: aim for %d for three stars.", "%d instructions : vise %d pour 3 étoiles."), R.count, g_L->par3);
		else snprintf (more, sizeof more, L2 ("%d instruction(s).", "%d instruction(s)."), R.count);
		char all[400]; snprintf (all, sizeof all, "%s\n%s", m, more);
		if (!g_editing)
		{
			int was = stars_of (g_L->id);
			if (R.stars > was) { char v[8]; snprintf (v, sizeof v, "%d", R.stars); kv_set (g_player, g_L->id, v); }
			// the best program: the fewest instructions of a won run
			char bk[64]; snprintf (bk, sizeof bk, "%s.best", g_L->id);
			if (new_best (kv_get (g_player, bk, ""), R.count)) { char v[12]; snprintf (v, sizeof v, "%d", R.count); kv_set (g_player, bk, v); }
			kv_save ();
			show_next (g_level + 1 < g_packs[g_pack]->levels.n || g_pack + 1 < g_npacks);
			ui_view_dirty (g_vList);
			refresh_count ();
		}
		else if (g_L->par3 == 0 || R.count < g_L->par3)		// the editor: the solution sets the stars' counts
		{
			g_L->par3 = R.count; g_L->par2 = R.count + (R.count + 2) / 3;
			char p[24]; snprintf (p, sizeof p, "%d %d", g_L->par3, g_L->par2); ui_w_set_text (g_edPar, (long) p);
			refresh_count ();
		}
		set_message (M_OK, all, R.stars);
	}
	else
	{
		if (R.errLine > 0) { hi_line (R.errLine); show_line (R.errLine); }
		set_message (R.result == R_LOST ? M_INFO : M_ERR, R.msg);
	}
}
// The playback, at each turn of the loop: the events due since the last one
static void playback_tick ()
{
	if (g_mode != P_RUN) return;
	unsigned now = bas_us ();
	double dt = g_lastUs ? (unsigned) (now - g_lastUs) / 1000.0 : 16;
	if (dt > 100) dt = 100;
	if (dt < 1) dt = 16;
	g_lastUs = now;
	int guard = 0;
	bool linePassed = false;
	while (g_mode == P_RUN && g_idx < g_run.ev.n && guard++ < 4000)
	{
		const Ev &e = g_run.ev[g_idx];
		if (e.kind == EV_LINE && g_stepping && linePassed) { g_mode = P_PAUSE; set_running (true); break; }
		double ms = ev_ms (e);
		if (ms > 0)
		{
			g_t += dt / ms;
			if (g_t < 1) { dt = 0; break; }
			dt = (g_t - 1) * ms; g_t = 0;
		}
		apply (g_view, e);
		if (e.kind == EV_LINE)
		{
			if (e.line != g_line) { g_line = e.line; hi_line (e.line); show_line (e.line); }
			if (g_stepping) linePassed = true;
		}
		if (e.kind == EV_PRINT && g_run.out)
		{
			char m[300]; snprintf (m, sizeof m, "%s %s", L2 ("The turtle says:", "La tortue dit :"), g_run.out + e.i);
			int l = (int) strlen (m); while (l && (m[l - 1] == '\n' || m[l - 1] == '\r')) m[--l] = 0;
			if (l > (int) strlen (L2 ("The turtle says:", "La tortue dit :")) + 1) set_message (M_INFO, m);
		}
		g_idx++;
		if (dt <= 0 && ev_ms (e) > 0) break;
	}
	if (g_mode == P_RUN && g_idx >= g_run.ev.n) finish_run ();
	ui_view_dirty (g_vBoard);
}

// ---- actions ------------------------------------------------------------------------------------------------------------------------
static void do_run ()
{
	if (g_mode == P_PAUSE) { g_stepping = false; g_mode = P_RUN; g_lastUs = 0; set_running (true); return; }
	if (g_mode == P_RUN) return;
	start_run (false);
}
static void do_step ()
{
	if (g_mode == P_PAUSE) { g_stepping = true; g_mode = P_RUN; g_lastUs = 0; return; }
	if (g_mode == P_RUN) { g_stepping = true; return; }
	if (start_run (true))
	{
		// to the first statement, lit, and wait there
		while (g_idx < g_run.ev.n && g_run.ev[g_idx].kind != EV_LINE) { apply (g_view, g_run.ev[g_idx]); g_idx++; }
		if (g_idx < g_run.ev.n) { g_line = g_run.ev[g_idx].line; hi_line (g_line); show_line (g_line); apply (g_view, g_run.ev[g_idx]); g_idx++; }
		g_mode = P_PAUSE; set_running (true);
		if (g_idx >= g_run.ev.n) finish_run ();
		ui_view_dirty (g_vBoard);
	}
}
static void do_stop ()
{
	if (g_mode != P_RUN && g_mode != P_PAUSE) return;
	g_mode = P_IDLE; g_t = 0; hi_line (0);
	set_running (false);
	caret_back ();
	set_message (M_INFO, L2 ("Stopped. Reset puts the turtle back at its start.", "Arrêté. « Au départ » remet la tortue à sa place."));
	ui_view_dirty (g_vBoard);
}
static void do_reset () { if (g_caret >= 0) caret_back (); reset_view (); set_message (M_NONE, ""); }
static void do_next ()
{
	if (g_editing || g_pack >= g_npacks) return;
	if (g_level + 1 < g_packs[g_pack]->levels.n) show_level (g_pack, g_level + 1, false);
	else if (g_pack + 1 < g_npacks) show_level (g_pack + 1, 0, false);
}
static void do_prev () { if (!g_editing && g_level > 0) show_level (g_pack, g_level - 1, false); }
static void do_hint () { if (g_editing) return; g_showHint = !g_showHint; relayout (); }
static void do_lesson () { if (!g_editing) show_lesson (!g_lessonOn); }

// A level shown: its program (the player's last, or its start), the board reset; its lesson the first time
static void packs_fill ();
static void leave_editor ();
static void show_level (int pack, int lv, bool keepCode)
{
	if (g_npacks == 0) return;
	if (!g_editing) save_code ();
	if (g_mode == P_RUN || g_mode == P_PAUSE) { g_mode = P_IDLE; g_caret = -1; }
	if (g_editing) leave_editor ();
	if (pack != g_pack) { g_pack = pack; g_top = 0; ui_w_set_value (g_packBox, pack); }
	g_level = lv;
	g_L = g_packs[pack]->levels[lv];
	g_target.clear ();
	if (g_L->draw) target_of (*g_L, g_target);
	set_language (g_lang);
	if (!keepCode)
	{
		char k[64]; snprintf (k, sizeof k, "%s.code", g_L->id);
		const char *c = kv_get (g_player, k, 0);
		if (!c) c = g_L->start ? g_L->start : "";
		ui_w_set_text (g_ed, (long) c);
		ui_textarea_goto (g_ed, (long) strlen (c));
	}
	g_showHint = false;
	reset_view ();
	set_message (M_NONE, "");
	refresh_count ();
	kv_set ("", "pack", g_packs[pack]->path); { char v[8]; snprintf (v, sizeof v, "%d", lv); kv_set ("", "level", v); }
	kv_save ();
	char k[40]; snprintf (k, sizeof k, "seen.%s", g_L->topic);
	g_concept = find_concept (g_L->topic);
	bool first = !kv_get (g_player, k, "")[0] && g_concept;
	if (first) { kv_set (g_player, k, "1"); kv_save (); }
	if (first != g_lessonOn) { g_lessonOn = first; ui_w_show (g_vLesson, first); }
	relayout ();
	list_show_sel ();
	ui_w_focus (g_ed);
}

// ---- the level editor --------------------------------------------------------------------------------------------------------------------
static Level g_edLevel;					// the level being edited (a copy)
static void edit_size ()
{
	char t[40]; snprintf (t, sizeof t, "%d x %d", g_edLevel.w, g_edLevel.h);
	ui_w_set_text (g_edSize, (long) t);
}
static void edit_cell (int c, int r, bool drag)
{
	Level &L = g_edLevel;
	char k = TOOL_CH[g_tool];
	g_badC = g_badR = -1;				// (the fault ringed: cleared by an edit)
	if (g_mode != P_IDLE) reset_view ();
	if (g_tool == TOOL_GEM) edit_gem (L, c, r, drag);	// (world.h: the lowest free number; a click on a gem: its next)
	else if (g_tool == TOOL_PAD1 || g_tool == TOOL_PAD2)	// (a pair keeps two pads: a third removes the older)
	{
		int q = g_tool - TOOL_PAD1;
		edit_pad (L, c, r, q + 1, g_padLastC[q], g_padLastR[q]);
	}
	else if (k == '>')					// the turtle: moved here, or turned when it is here already
	{
		char cur = L.map[r][c];
		static const char ROT[] = "^>v<";
		const char *p = cur ? strchr (ROT, cur) : 0;
		if (p) L.map[r][c] = ROT[(p - ROT + 1) % 4];
		else
		{
			for (int y = 0; y < L.h; y++) for (int x = 0; x < L.w; x++) if (L.map[y][x] && strchr (ROT, L.map[y][x])) L.map[y][x] = '.';
			L.map[r][c] = '>';
		}
	}
	else
	{
		if (L.map[r][c] && strchr ("^>v<", L.map[r][c])) return;	// (the turtle stays: move it with its tool)
		if (k == '*') for (int y = 0; y < L.h; y++) for (int x = 0; x < L.w; x++) if (L.map[y][x] == '*') L.map[y][x] = '.';
		L.map[r][c] = k;
	}
	ui_view_dirty (g_vBoard);
}
static void edit_resize (int dw, int dh)
{
	Level &L = g_edLevel;
	int w = L.w + dw, h = L.h + dh;
	if (w < 3 || h < 3 || w > MAXW || h > MAXH) return;
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++) if (y >= L.h || x >= L.w) L.map[y][x] = (y == h - 1 || x == w - 1 || y == 0 || x == 0) ? '#' : '.';
		L.map[y][w] = 0;
	}
	// the new border: walls
	if (dw > 0) for (int y = 0; y < h; y++) { L.map[y][w - 2] = L.map[y][w - 2] == '#' && y > 0 && y < h - 1 ? '.' : L.map[y][w - 2]; L.map[y][w - 1] = '#'; }
	if (dh > 0) for (int x = 0; x < w; x++) { if (x > 0 && x < w - 1 && L.map[h - 2][x] == '#') L.map[h - 2][x] = '.'; L.map[h - 1][x] = '#'; }
	L.w = w; L.h = h;
	edit_size ();
	g_view.reset (L);
	ui_view_dirty (g_vBoard);
}
static void edit_collect ()			// the fields into the level
{
	Level &L = g_edLevel;
	tcpy (L.title[g_lang], (const char *) ui_w_get_text (g_edTitle), sizeof L.title[0]);
	tcpy (L.text[g_lang], (const char *) ui_w_get_text (g_edText), sizeof L.text[0]);
	tcpy (L.hint[g_lang], (const char *) ui_w_get_text (g_edHint), sizeof L.hint[0]);
	tcpy (L.words, (const char *) ui_w_get_text (g_edWords), sizeof L.words);
	const char *par = (const char *) ui_w_get_text (g_edPar);
	L.par3 = atoi (par); const char *q = strchr (par, ' '); L.par2 = q ? atoi (q) : L.par3 + 2;
	int d = (int) ui_w_get_value (g_edDraw); L.draw = d < 0 || d > 2 ? 0 : d;	// (No / Shape / Colours)
	int ci = (int) ui_w_get_value (g_edConcept); if (ci < 0 || ci >= NCONCEPTS) ci = 0;
	tcpy (L.topic, CONCEPT_KEYS[ci], sizeof L.topic);
	free (L.solution); L.solution = tdup (code ());
}
static void open_editor (bool fresh)
{
	if (g_mode == P_RUN || g_mode == P_PAUSE) do_stop ();
	if (!g_editing) save_code ();
	if (fresh || !g_L)
	{
		Level blank; blank.w = 10; blank.h = 7;
		for (int y = 0; y < blank.h; y++) { for (int x = 0; x < blank.w; x++) blank.map[y][x] = (y == 0 || x == 0 || y == blank.h - 1 || x == blank.w - 1) ? '#' : '.'; blank.map[y][blank.w] = 0; }
		blank.map[3][1] = '>'; blank.map[3][8] = '*';
		tcpy (blank.title[0], "My level", sizeof blank.title[0]); tcpy (blank.title[1], "Mon niveau", sizeof blank.title[1]);
		tcpy (blank.topic, "move", sizeof blank.topic);
		blank.solution = tdup (g_lang == LANG_FR ? "AVANCER 7\n" : "FORWARD 7\n");
		g_edLevel.set (blank);
		snprintf (g_edLevel.id, sizeof g_edLevel.id, "my-%u", bas_ms () % 100000);
	}
	else if (g_L != &g_edLevel) g_edLevel.set (*g_L);
	g_editing = true;
	g_L = &g_edLevel;
	g_target.clear ();
	ui_w_set_text (g_edTitle, (long) g_edLevel.titleOf (g_lang)); ui_w_set_text (g_edText, (long) g_edLevel.textOf (g_lang)); ui_w_set_text (g_edHint, (long) g_edLevel.hintOf (g_lang));
	ui_w_set_text (g_edWords, (long) g_edLevel.words);
	char p[24]; snprintf (p, sizeof p, "%d %d", g_edLevel.par3, g_edLevel.par2); ui_w_set_text (g_edPar, (long) (g_edLevel.par3 ? p : ""));
	ui_w_set_value (g_edDraw, g_edLevel.draw >= 0 && g_edLevel.draw <= 2 ? g_edLevel.draw : 1);
	g_badC = g_badR = -1; g_padLastC[0] = g_padLastC[1] = g_padLastR[0] = g_padLastR[1] = -1;
	int ci = 0; for (int i = 0; i < NCONCEPTS; i++) if (!strcmp (CONCEPT_KEYS[i], g_edLevel.topic)) ci = i;
	ui_w_set_value (g_edConcept, ci);
	ui_w_set_text (g_ed, (long) (g_edLevel.solution ? g_edLevel.solution : ""));
	refresh_count ();
	edit_size ();
	reset_view ();
	if (g_lessonOn) { g_lessonOn = false; ui_w_show (g_vLesson, 0); }
	set_message (M_INFO, L2 ("Click or drag on the board with a tool. The turtle tool turns the turtle when clicked on it.", "Clique ou glisse sur le plateau avec un outil. L'outil tortue fait tourner la tortue quand on clique dessus."));
	relayout ();
}
static void leave_editor () { g_editing = false; }
// The level's gems and portals checked (world.h's check_level, the pack reader's verdict): refused, the message in red
// and the cell at fault ringed red; nothing run or written
static bool edit_check ()
{
	LevelFault f;
	if (check_level (g_edLevel, f)) return true;
	if (g_mode != P_IDLE) reset_view ();
	char m[200]; level_fault_text (f, g_lang, m, sizeof m);
	g_badC = f.c; g_badR = f.r;
	set_message (M_ERR, m);
	ui_view_dirty (g_vBoard);
	return false;
}
static void ed_test ()
{
	edit_collect ();
	if (!edit_check ()) return;
	g_target.clear ();
	if (g_edLevel.draw) target_of (g_edLevel, g_target);
	g_edLevel.par3 = 0;				// (the solution sets them again)
	start_run (false);
}
static void ed_save ()
{
	edit_collect ();
	if (!edit_check ()) return;
	if (!g_edLevel.id[0]) snprintf (g_edLevel.id, sizeof g_edLevel.id, "my-%u", bas_ms () % 100000);
	// the player's pack: loaded (or made), the level put in place of the one with its id, or added
	int pi = -1;
	for (int i = 0; i < g_npacks; i++) if (g_packs[i]->user) pi = i;
	if (pi < 0)
	{
		if (g_npacks >= MAXPACKS) return;
		Pack *pk = new Pack; pk->user = true; tcpy (pk->path, USERPACK, sizeof pk->path);
		tcpy (pk->title[0], "My levels", sizeof pk->title[0]); tcpy (pk->title[1], "Mes niveaux", sizeof pk->title[1]);
		g_packs[g_npacks] = pk; pi = g_npacks++;
	}
	Pack &pk = *g_packs[pi];
	int at = -1;
	for (int i = 0; i < pk.levels.n; i++) if (!strcmp (pk.levels[i]->id, g_edLevel.id)) at = i;
	bool copyOfBuiltin = at < 0 && strncmp (g_edLevel.id, "my-", 3) != 0;
	if (copyOfBuiltin) { char id[32]; snprintf (id, sizeof id, "my-%.27s", g_edLevel.id); tcpy (g_edLevel.id, id, sizeof g_edLevel.id); for (int i = 0; i < pk.levels.n; i++) if (!strcmp (pk.levels[i]->id, g_edLevel.id)) at = i; }
	if (at < 0) { pk.levels.push (new Level); at = pk.levels.n - 1; }
	pk.levels[at]->set (g_edLevel);
	mkdir (USERDIR, 0755);
	char *txt = write_pack (pk);
	bool ok = bas_save (pk.path, txt, (int) strlen (txt));
	free (txt);
	packs_fill ();
	char m[200]; snprintf (m, sizeof m, ok ? L2 ("Saved in \"%s\".", "Enregistré dans « %s ».") : L2 ("Could not write %s.", "Impossible d'écrire %s."), ok ? pk.titleOf (g_lang) : pk.path);
	set_message (ok ? M_OK : M_ERR, m, 0);
}
static void ed_close ()
{
	int p = g_pack, l = g_level;
	for (int i = 0; i < g_npacks; i++) if (g_packs[i]->user) for (int k = 0; k < g_packs[i]->levels.n; k++) if (!strcmp (g_packs[i]->levels[k]->id, g_edLevel.id)) { p = i; l = k; }
	show_level (p, l, false);
}

// ---- the menus --------------------------------------------------------------------------------------------------------------------------
enum { MAXPLAYERS = 8 };
enum { C_RUN = 1, C_STEP, C_STOP, C_RESET, C_NEXT, C_PREV, C_LESSON, C_HINT, C_QUIT, C_OPEN, C_EDIT, C_NEW, C_NEWPLAYER, C_ABOUT, C_PLAYER = 100, C_PACK = 120 };
static char g_players[MAXPLAYERS][32]; static int g_nplayers = 0;
static char g_menuText[4096];
static void build_menu ()
{
	int n = 0;
	auto M = [&] (const char *title) { n += snprintf (g_menuText + n, sizeof g_menuText - n, "M%s\n", title); };
	auto I = [&] (int id, const char *label, const char *key = "") { n += snprintf (g_menuText + n, sizeof g_menuText - n, key[0] ? "I%d\t%s\t%s\n" : "I%d\t%s\n", id, label, key); };
	auto S = [&] () { n += snprintf (g_menuText + n, sizeof g_menuText - n, "-\n"); };
	M (L2 ("Game", "Jeu"));
	I (C_RUN, L2 ("Run", "Lancer"), "F5");
	I (C_STEP, L2 ("Step", "Pas à pas"), "F8");
	I (C_STOP, L2 ("Stop", "Arrêter"), "F7");
	I (C_RESET, L2 ("Reset", "Au départ"), "F9");
	S ();
	I (C_NEXT, L2 ("Next Level", "Niveau suivant"), "Ctrl+N");
	I (C_PREV, L2 ("Previous Level", "Niveau précédent"), "Ctrl+P");
	I (C_LESSON, L2 ("Lesson", "Leçon"), "F1");
	I (C_HINT, L2 ("Hint", "Indice"), "F2");
	S ();
	I (C_QUIT, L2 ("Quit", "Quitter"), "Ctrl+Q");
	M (L2 ("Levels", "Niveaux"));
	for (int i = 0; i < g_npacks; i++) I (C_PACK + i, g_packs[i]->titleOf (g_lang));
	S ();
	I (C_OPEN, L2 ("Open Level Pack...", "Ouvrir un recueil..."), "Ctrl+O");
	S ();
	I (C_EDIT, L2 ("Edit This Level", "Modifier ce niveau"), "Ctrl+E");
	I (C_NEW, L2 ("New Level", "Nouveau niveau"));
	M (L2 ("Player", "Joueur"));
	for (int i = 0; i < g_nplayers; i++) I (C_PLAYER + i, g_players[i]);
	S ();
	I (C_NEWPLAYER, L2 ("New Player...", "Nouveau joueur..."));
	I (C_ABOUT, L2 ("About Turtle Quest", "À propos de Turtle Quest"));
	ui_menu ((long) g_menuText);
	for (int i = 0; i < g_nplayers; i++) ui_menu_state (C_PLAYER + i, strcmp (g_players[i], g_player) ? 0 : UMS_CHECKED);
}
static void packs_fill ()
{
	ui_list_clear (g_packBox);
	for (int i = 0; i < g_npacks; i++) ui_list_add (g_packBox, (long) g_packs[i]->titleOf (g_lang), i);
	ui_w_set_value (g_packBox, g_pack);
	build_menu ();
}
static void m_open ()
{
	char path[256];
	ui_file_filter ((long) "*.turtle");
	long ok = ui_file_open ((long) USERDIR, (long) path, sizeof path);
	ui_file_filter (0);
	if (!ok) return;
	if (add_pack (path, false, false)) { packs_fill (); for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) show_level (i, 0, false); }
}
static void players_read ()
{
	g_nplayers = 0;
	char buf[400]; tcpy (buf, kv_get ("", "players", ""), sizeof buf);
	for (char *p = buf; *p && g_nplayers < MAXPLAYERS; )
	{
		char *e = strchr (p, '|'); if (e) *e = 0;
		if (*p) tcpy (g_players[g_nplayers++], p, 32);
		p = e ? e + 1 : p + strlen (p);
	}
	if (!g_nplayers) { tcpy (g_players[0], L2 ("Player", "Joueur"), 32); g_nplayers = 1; }
}
static void players_write ()
{
	char buf[400] = ""; for (int i = 0; i < g_nplayers; i++) { if (i) strcat (buf, "|"); strcat (buf, g_players[i]); }
	kv_set ("", "players", buf); kv_set ("", "player", g_player); kv_save ();
}
static void choose_player (int i)
{
	if (i < 0 || i >= g_nplayers) return;
	save_code ();
	tcpy (g_player, g_players[i], sizeof g_player);
	players_write (); build_menu ();
	if (!g_editing) show_level (g_pack, g_level, false);
	ui_view_dirty (g_vList);
}
static void m_newplayer ()
{
	if (g_nplayers >= MAXPLAYERS) { say (L2 ("Eight players at most.", "Huit joueurs au plus.")); return; }
	char name[32] = "";
	if (!ui_ask ((long) L2 ("New player", "Nouveau joueur"), (long) L2 ("The player's name:", "Le nom du joueur :"), (long) name, 25)) return;
	for (char *p = name; *p; p++) if (*p == '|' || *p == '[' || *p == ']' || *p == '=') *p = ' ';
	if (!name[0]) return;
	for (int i = 0; i < g_nplayers; i++) if (!strcmp (g_players[i], name)) { choose_player (i); return; }
	tcpy (g_players[g_nplayers++], name, 32);
	choose_player (g_nplayers - 1);
}
static void command (int id)
{
	if (id >= C_PACK && id < C_PACK + (int) MAXPACKS) { if (id - C_PACK < g_npacks) show_level (id - C_PACK, 0, false); return; }
	if (id >= C_PLAYER && id < C_PLAYER + (int) MAXPLAYERS) { choose_player (id - C_PLAYER); return; }
	switch (id)
	{
	case C_RUN: if (g_editing && g_mode == P_IDLE) ed_test (); else do_run (); break;
	case C_STEP: do_step (); break;
	case C_STOP: do_stop (); break;
	case C_RESET: do_reset (); break;
	case C_NEXT: do_next (); break;
	case C_PREV: do_prev (); break;
	case C_LESSON: do_lesson (); break;
	case C_HINT: do_hint (); break;
	case C_OPEN: m_open (); break;
	case C_EDIT: open_editor (false); break;
	case C_NEW: open_editor (true); break;
	case C_NEWPLAYER: m_newplayer (); break;
	case C_ABOUT: say (L2 ("Turtle Quest -- learn to program with a turtle.\nThe programs are Onyx BASIC, with the turtle's words.\n\nMIT License -- the Onyx contributors.",
		"Turtle Quest -- apprends à programmer avec une tortue.\nLes programmes sont en Onyx BASIC, avec les mots de la tortue.\n\nLicence MIT -- les contributeurs d'Onyx.")); break;
	}
}

// ---- the window ------------------------------------------------------------------------------------------------------------------------
enum { PAD = 10, LISTW = 220, EDW = 340, TOOLH = 32, COUNTH = 22, MSGH = 62 };
static void layout ()
{
	int W = (int) ui.w, H = (int) ui.h;
	bool narrow = W < 920;			// (a small window: the levels and the program narrower, Hint and Lesson under the card's text)
	g_tiny = W < 760;			// (too small for the list: the Levels menu and Ctrl+N, Ctrl+P choose)
	int listW = narrow && !g_editing ? 170 : LISTW;
	bool list = !g_editing && !g_tiny;
	ui_w_show (g_packBox, list); ui_w_show (g_vList, list); ui_w_show (g_edPanel, g_editing);
	int mid = list || g_editing ? PAD + listW + PAD : PAD;
	int edw = narrow ? 270 : EDW;
	if (g_tiny) edw = (W - mid - 2 * PAD) * 45 / 100;
	if (W > 1000)
	{
		int bw = H - 120, most = W - mid - EDW - 2 * PAD;
		if (bw < 400) bw = 400;
		if (bw > most) bw = most;
		edw = W - mid - PAD - bw - PAD;
	}
	int right = mid + edw + PAD, rw = W - right - PAD;
	// left: the pack and its levels (the level editor's panel stays where it is made)
	ui_w_move (g_packBox, PAD, PAD, listW, 28);
	g_listW = listW; g_listH = H - 2 * PAD - 36;
	ui_w_move (g_vList, PAD, (int) PAD + 36, listW, g_listH);
	list_clamp ();
	ui_w_show (g_listBar, list && list_bar ());
	ui_w_move (g_listBar, PAD + listW - 14, (int) PAD + 36 + 2, 12, g_listH - 4);
	// the middle: the buttons, the speed, the program, its words, its count
	int x = mid;
	long bts[4] = { g_btRun, g_btStep, g_btStop, g_btReset };
	int bw[4] = { 84, 70, 70, 0 };
	if (edw < 300) { bw[0] = 58; bw[1] = bw[2] = 56; }
	if (edw < 240) { bw[0] = bw[1] = bw[2] = edw / 4 - 5; }
	bw[3] = edw - (bw[0] + bw[1] + bw[2]) - 3 * 6;
	for (int i = 0; i < 4; i++) { ui_w_move (bts[i], x, PAD, bw[i], TOOLH); x += bw[i] + 6; }
	ui_w_move (g_lbSpeed, mid, PAD + TOOLH + 6, 70, 22);
	ui_w_move (g_speed, mid + 72, PAD + TOOLH + 6, edw - 72, 22);
	int ey = PAD + TOOLH + 6 + 22 + 8;
	words_build (edw);
	int wordsH = 1; for (int i = 0; i < g_nchips; i++) if (g_chips[i].y + CHIPH > wordsH) wordsH = g_chips[i].y + CHIPH;
	int edh = H - ey - PAD - COUNTH - 8 - wordsH - 8;
	ui_w_move (g_ed, mid, ey, edw, edh);
	ui_w_move (g_vWords, mid, ey + edh + 8, edw, wordsH);
	ui_w_move (g_vCount, mid, H - PAD - COUNTH, edw, COUNTH);
	// the right: the card, the board, the message
	g_cardBelow = rw < 490;
	int ch = card_need (rw);
	ui_w_move (g_vCard, right, PAD, rw, ch);
	int bty = g_cardBelow ? PAD + ch - 38 : PAD + 10;
	int btr = g_cardBelow ? 0 : 104;					// (left of the window's buttons)
	ui_w_move (g_btLesson, right + rw - 90 - btr, bty, 80, 28);
	ui_w_move (g_btHint, right + rw - 176 - btr, bty, 80, 28);
	ui_w_show (g_btHint, !g_editing); ui_w_show (g_btLesson, !g_editing);
	int by = PAD + ch + PAD;
	ui_w_move (g_vBoard, right, by, rw, H - by - PAD - MSGH - PAD);
	ui_w_move (g_vMsg, right, H - PAD - MSGH, rw, MSGH);
	ui_w_move (g_btNext, right + rw - 140, H - PAD - MSGH + 15, 126, 32);
	// the lesson: centred on the whole right column (the card and the board), so a long card does not shrink it
	int colH = H - PAD - MSGH - PAD - PAD;
	int lw = rw - 20 < 560 ? rw - 20 : 560, lh = colH - 16;
	if (g_concept) { int need = lesson_flow (lw, 100000, false, PX_UI) + 56; if (need < lh) lh = need; }
	if (lh < 200) lh = 200;
	int lx = right + (rw - lw) / 2, ly = PAD + (colH - lh) / 2;
	ui_w_move (g_vLesson, lx, ly, lw, lh);
	ui_w_move (g_lesOk, lx + lw - 120, ly + lh - 46, 104, 30);
	long vs[] = { g_vList, g_vWords, g_vCount, g_vCard, g_vBoard, g_vMsg, g_vLesson, g_vTools };
	for (long v : vs) ui_view_dirty (v);
}
static void refresh ()
{
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	long vs[] = { g_vList, g_vWords, g_vCount, g_vCard, g_vBoard, g_vMsg, g_vLesson, g_vTools };
	for (long v : vs) ui_view_dirty (v);
	ui_w_draw (0);
	ui_update_all ();
}
static void relayout () { layout (); refresh (); }
static long on_resize (long, long) { relayout (); return 0; }

static long ed_label (int y, const char *en, const char *fr, int x = 0, int w = LISTW)
{
	long l = ui_label (PAD + x, PAD + y, w, 18, (long) L2 (en, fr), ULB_DIM);
	ui_w_set_parent (l, g_edPanel);
	return l;
}
static long ed_field (int y, int max, int x = 0, int w = LISTW)
{
	long t = ui_textbox (PAD + x, PAD + y, w, 26, 0);
	ui_w_set_range (t, 0, max, 0, 0);
	ui_w_set_parent (t, g_edPanel);
	return t;
}
static long ed_button (int x, int y, int w, int h, const char *t)
{
	long b = ui_pushbutton (PAD + x, PAD + y, w, h, (long) t, 0);
	ui_w_set_parent (b, g_edPanel);
	return b;
}
static void make_editor (int H)
{
	g_edPanel = ui_panel (PAD, PAD, LISTW, H - 2 * PAD, 0);
	int y = 0;
	int b0 = 58, b1 = 88;
	g_edBtn[0] = ed_button (0, y, b0, 30, L2 ("Test", "Tester"));
	g_edBtn[1] = ed_button (b0 + 6, y, b1, 30, L2 ("Save", "Enregistrer"));
	g_edBtn[2] = ed_button (b0 + b1 + 12, y, LISTW - b0 - b1 - 12, 30, L2 ("Close", "Fermer"));
	y += 36;
	ed_label (y, "Idea taught", "Idée enseignée"); y += 18;
	char opts[600]; int n = 0;
	for (int i = 0; i < NCONCEPTS; i++) { const Concept *c = find_concept (CONCEPT_KEYS[i]); n += snprintf (opts + n, sizeof opts - n, "%s%s", i ? "|" : "", c ? c->title[g_lang] : CONCEPT_KEYS[i]); }
	g_edConcept = ui_dropdown (PAD, PAD + y, LISTW, 26, (long) opts); ui_w_set_parent (g_edConcept, g_edPanel); y += 32;
	ed_label (y, "Title", "Titre"); y += 18;
	g_edTitle = ed_field (y, 70); y += 32;
	ed_label (y, "What to do", "Ce qu'il faut faire"); y += 18;
	g_edText = ed_field (y, 400); y += 32;
	ed_label (y, "Hint", "Indice"); y += 18;
	g_edHint = ed_field (y, 400); y += 32;
	ed_label (y, "Words (empty: all)", "Mots (vide : tous)"); y += 18;
	g_edWords = ed_field (y, 250); y += 32;
	ed_label (y, "3 and 2 stars", "3 et 2 étoiles", 0, 100); ed_label (y, "Drawing", "Dessin", 110, LISTW - 110); y += 18;
	g_edPar = ed_field (y, 20, 0, 100);
	g_edDraw = ui_dropdown (PAD + 110, PAD + y, LISTW - 110, 26, (long) L2 ("No|Shape|Colours", "Non|Forme|Couleurs")); ui_w_set_parent (g_edDraw, g_edPanel);
	ui_w_tip (g_edDraw, (long) L2 ("A drawing level: reproduce the solution's figure -- its shape, or its shape and colours",
		"Un niveau de dessin : reproduire la figure de la solution -- sa forme, ou sa forme et ses couleurs"));
	y += 34;
	ed_label (y, "Size", "Taille"); y += 18;
	g_edSize = ui_label (PAD, PAD + y, 44, 26, (long) "", 0); ui_w_set_parent (g_edSize, g_edPanel);
	{
		const char *const T[4] = { L2 ("W-", "L-"), L2 ("W+", "L+"), "H-", "H+" };
		int bw = (LISTW - 46 - 3 * 4) / 4, x = 46;
		for (int i = 0; i < 4; i++) { g_edW[i] = ed_button (x, y, i == 3 ? LISTW - x : bw, 26, T[i]); x += bw + 4; }
	}
	y += 34;
	ed_label (y, "Tool", "Outil"); y += 18;
	g_vTools = ui_view (PAD, PAD + y, LISTW, 4 * TOOLCELLH, 0);
	ui_view_on (g_vTools, (long) tools_paint, (long) tools_mouse, 0, 0);
	ui_w_set_parent (g_vTools, g_edPanel);
	ui_w_show (g_edPanel, 0);
}

// What a widget said
static void control (long w, long code_)
{
	if (w == g_ed) { if (code_ == UN_CHANGE) { refresh_count (); if (g_mode == P_DONE) g_mode = P_IDLE; hi_line (0); } return; }
	if (w == g_packBox && (code_ == UN_CHANGE || code_ == UN_ACTIVATE)) { int p = (int) ui_w_get_value (g_packBox); if (p >= 0 && p < g_npacks && p != g_pack) show_level (p, 0, false); return; }
	if (w == g_listBar && code_ == UN_CHANGE) { g_top = (int) ui_w_get_value (g_listBar); ui_view_dirty (g_vList); return; }
	if (code_ != UN_ACTIVATE) return;
	if (w == g_btRun) { if (g_editing && g_mode == P_IDLE) ed_test (); else do_run (); }
	else if (w == g_btStep) do_step ();
	else if (w == g_btStop) do_stop ();
	else if (w == g_btReset) do_reset ();
	else if (w == g_btNext) do_next ();
	else if (w == g_btHint) do_hint ();
	else if (w == g_btLesson) do_lesson ();
	else if (w == g_lesOk) show_lesson (false);
	else if (w == g_edBtn[0]) ed_test ();
	else if (w == g_edBtn[1]) ed_save ();
	else if (w == g_edBtn[2]) { ed_close (); return; }
	else if (w == g_edW[0]) edit_resize (-1, 0);
	else if (w == g_edW[1]) edit_resize (1, 0);
	else if (w == g_edW[2]) edit_resize (0, -1);
	else if (w == g_edW[3]) edit_resize (0, 1);
	else return;
	ui_w_focus (g_ed);
}

// BASIC's words and the turtle's, for the editor's colours (bas::wordList: capitals, separated by blanks)
static char s_words[16384];
static long is_keyword (long word, long len)
{
	const char *w = (const char *) word;
	for (const char *p = s_words; *p; )
	{
		const char *e = p; while (*e && *e != ' ') e++;
		if (e - p == len) { int k = 0; while (k < len && tup (w[k]) == p[k]) k++; if (k == len) return 1; }
		p = *e ? e + 1 : e;
	}
	return 0;
}

// main --check: every level by its own solution, in both languages -> the verdict in the message, the failures in a file
static void self_check ()
{
	int levels = 0, bad = 0, three = 0;
	static char out[8192]; int n = 0;
	for (int p = 0; p < g_npacks; p++)
		for (int l = 0; l < g_packs[p]->levels.n; l++)
		{
			const Level &L = *g_packs[p]->levels[l];
			levels++;
			Arr<Seg> t;
			if (L.draw && !target_of (L, t)) { bad++; n += snprintf (out + n, sizeof out - n, "%s: its figure\n", L.id); continue; }
			bool fine = true;
			for (int lang = 0; lang < 2 && fine; lang++)
			{
				Run R;
				run_program (L, L.solution ? L.solution : "", lang, R, L.draw ? &t : 0);
				if (R.result != R_WON) { fine = false; n += snprintf (out + n, sizeof out - n, "%s (%d): result %d line %d: %s\n", L.id, lang, R.result, R.errLine, R.msg); }
				else if (lang == 0 && R.stars == 3) three++;
				else if (lang == 0) n += snprintf (out + n, sizeof out - n, "%s: %d stars, %d instructions, par %d\n", L.id, R.stars, R.count, L.par3);
			}
			if (!fine) bad++;
			if (n > (int) sizeof out - 400) n = (int) sizeof out - 400;
		}
	set_language (g_lang);
	n += snprintf (out + n, sizeof out - n, "%d levels, %d failed, %d with three stars\n", levels, bad, three);
	bas_save (CHECKFILE, out, n);
	char m[120]; snprintf (m, sizeof m, "--check\n%d levels, %d failed, %d with three stars (%s)", levels, bad, three, CHECKFILE);
	set_message (bad ? M_ERR : M_OK, m, 0);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	mkdir ("SD0:/home", 0755);
	mkdir (USERDIR, 0755);
	kv_load ();
	g_lang = !strcmp ((const char *) ui_lang (), "fr") ? LANG_FR : LANG_EN;
	set_language (g_lang);
	bas::wordList (s_words, sizeof s_words);
	players_read ();
	tcpy (g_player, kv_get ("", "player", g_players[0]), sizeof g_player);
	load_packs ();
	bool check = argc > 1 && !strcmp (argv[1], "--check");
	const char *arg = argc > 1 && !check && argv[1][0] ? argv[1] : 0;
	if (arg) add_pack (arg, false, false);

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int sw = (int) scr[0], sh = (int) (scr[1] - scr[3] - scr[2]);
	int ww = 1000 < sw - 8 ? 1000 : sw - 8, wh = 640 < sh - 40 ? 640 : sh - 40;
	if (ui_window_ex (0, scr[3], ww, wh, (long) "Turtle Quest", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (sw < 800 ? 480 : 800, sw < 800 ? 320 : 440);
	ui_on_resize ((long) on_resize, 0);
	if (g_npacks == 0)
	{
		say (L2 ("No level found in SD0:/apps/turtle.app.", "Aucun niveau dans SD0:/apps/turtle.app."));
		return 1;
	}

	g_packBox = ui_dropdown (PAD, PAD, LISTW, 28, (long) "");
	g_vList = ui_view (PAD, PAD + 36, LISTW, 400, 0);
	ui_view_on (g_vList, (long) list_paint, (long) list_mouse, 0, (long) list_wheel);
	g_listBar = ui_scrollbar (0, 0, 12, 100, 0);
	ui_w_show (g_listBar, 0);
	g_btRun = ui_pushbutton (0, 0, 80, TOOLH, (long) L2 ("Run", "Lancer"), UB_DEFAULT);
	g_btStep = ui_pushbutton (0, 0, 70, TOOLH, (long) L2 ("Step", "Pas"), 0);
	g_btStop = ui_pushbutton (0, 0, 70, TOOLH, (long) L2 ("Stop", "Arrêt"), 0);
	g_btReset = ui_pushbutton (0, 0, 70, TOOLH, (long) L2 ("Reset", "Au départ"), 0);
	ui_w_tip (g_btRun, (long) L2 ("Run the program (F5)", "Lancer le programme (F5)"));
	ui_w_tip (g_btStep, (long) L2 ("One statement at a time (F8)", "Une instruction à la fois (F8)"));
	g_lbSpeed = ui_label (0, 0, 70, 22, (long) L2 ("Speed", "Vitesse"), 0);
	g_speed = ui_slider (0, 0, 200, 22, 1, 10, atoi (kv_get ("", "speed", "5")));
	g_ed = ui_textarea (0, 0, 300, 300, ED_FLAGS);
	ui_textarea_keywords (g_ed, (long) is_keyword);
	g_vWords = ui_view (0, 0, EDW, 60, 0);
	ui_view_on (g_vWords, (long) words_paint, (long) words_mouse, 0, 0);
	g_vCount = ui_view (0, 0, EDW, COUNTH, 0);
	ui_view_on (g_vCount, (long) count_paint, 0, 0, 0);
	g_vCard = ui_view (0, 0, 400, 100, 0);
	ui_view_on (g_vCard, (long) card_paint, 0, 0, 0);
	g_btHint = ui_pushbutton (0, 0, 80, 28, (long) L2 ("Hint", "Indice"), 0); ui_w_set_parent (g_btHint, g_vCard);
	g_btLesson = ui_pushbutton (0, 0, 80, 28, (long) L2 ("Lesson", "Leçon"), 0); ui_w_set_parent (g_btLesson, g_vCard);
	g_vBoard = ui_view (0, 0, 400, 400, 0);
	ui_view_on (g_vBoard, (long) board_paint, (long) board_mouse, 0, 0);
	g_vMsg = ui_view (0, 0, 400, MSGH, 0);
	ui_view_on (g_vMsg, (long) msg_paint, 0, 0, 0);
	g_btNext = ui_pushbutton (0, 0, 126, 32, (long) L2 ("Next level", "Niveau suivant"), UB_DEFAULT); ui_w_set_parent (g_btNext, g_vMsg);
	ui_w_show (g_btNext, 0);
	make_editor ((int) ui.h);
	g_vLesson = ui_view (0, 0, 520, 340, 0);
	ui_view_on (g_vLesson, (long) lesson_paint, 0, 0, 0);
	g_lesOk = ui_pushbutton (0, 0, 104, 30, (long) "OK", UB_DEFAULT); ui_w_set_parent (g_lesOk, g_vLesson);
	ui_w_show (g_vLesson, 0);
	layout ();

	packs_fill ();
	// where the player was (or the pack given)
	int p = 0, l = 0;
	if (arg) { for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, arg)) p = i; }
	else
	{
		const char *pp = kv_get ("", "pack", "");
		for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, pp)) { p = i; l = atoi (kv_get ("", "level", "0")); }
		if (l >= g_packs[p]->levels.n) l = 0;
	}
	show_level (p, l, false);
	if (check) { if (g_lessonOn) show_lesson (false); self_check (); }

	for (;;)
	{
		long t = ui_event (g_mode == P_RUN ? 15 : 50);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) { int id = ui_ev_field (&ui, EV_CODE); if (id == C_QUIT) break; command (id); }
		else if (t == WEV_DROP)
		{
			char path[256]; long kind = 0;
			long n = uikit_drop_get (path, sizeof path - 1, &kind);
			if (n > 0)
			{
				path[n] = 0; char *e = strchr (path, '\n'); if (e) *e = 0;
				if (add_pack (path, false, false)) { packs_fill (); for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) show_level (i, 0, false); }
			}
		}
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = press ? ui_ev_field (&ui, EV_CODE) : 0;
			long code_ = 0, taken = 0, w = uikit_w_event (&code_, &taken);
			if (w && code_) control (w, code_);
			if (press)
			{
				long id = ui_menu_key (0);
				if (id == C_QUIT) break;
				if (id >= 0) command ((int) id);
				else if (hid == KEY_ESC) { if (g_lessonOn) show_lesson (false); else do_stop (); }
			}
		}
		else ui_w_idle ();
		playback_tick ();
		ui_view_flush ();
	}
	save_code ();
	{ char v[8]; snprintf (v, sizeof v, "%d", (int) ui_w_get_value (g_speed)); kv_set ("", "speed", v); kv_save (); }
	return 0;
}
