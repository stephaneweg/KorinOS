//
// circuits -- Circuits, a puzzle game of logic gates (Onyx's Apps/circuits on UIKit, src/apps/circuits/README.md): wire
// switches, NOT, AND, OR, XOR, NAND and NOR gates and lamps on a board until the lamps light exactly as the level's truth
// table says -- from one wire to a two-bit adder, in three worlds. The window: the levels on the left (the worlds, their
// stars, the padlocks of those not open yet), in the middle the level's card (what to do, Hint, Lesson), the palette of the
// gates the level allows, the board and the message bar; on the right the truth table (a click on a row sets the switches),
// the gate count and the stars it needs, Step, Reset and Check. The simulation is live (a switch clicked, the wires and
// lamps follow at once); Step shows the signal going through, one gate depth at a time. Check tries every row: the wrong
// ones are marked, a level won gives 1 to 3 stars (fewer gates: more stars) and opens the next one.
//
// Mouse: a palette button arms its gate (click then click on the board, or drag it there); a press on an output pin
// drags a wire to an input pin (a fed input's wire is picked up by a press on it); a click on a gate selects it, a
// drag moves it; a click on a switch toggles it; a right click deletes a gate or a wire.
// Keys: F5 Check, F8 Step, F7 Live, F9 Reset, F1 the lesson, F2 the hint, 1-6 a gate of the palette, the arrows move
// the gate selected, Del / Backspace delete, Esc cancels, Ctrl+Z / Ctrl+Y undo / redo, Ctrl+N / Ctrl+P the next /
// previous level, Ctrl+O a pack of levels, Ctrl+Q quit. The language is the system's (ui_lang, data/lang/fr.txt).
//
// A screen of 800 px or less (the PocketTerm): the board has a pointer of its own that the arrows move, a cell at a time
// (Shift: four), and Enter presses -- on a switch: toggled; on a gate: taken (the arrows move it, Enter puts it down); on
// an output pin: a wire begun (the arrows, then Enter on an input pin); a gate armed (1-6): put there. A line at the
// window's foot says what the keys do. A window under 720 px wide: the levels are a drawer (L, or the button at the
// palette's left), Up / Down go from level to level, Enter closes it.
//
// Files: the packs SD0:/apps/circuits.app/levels/*.circuits (and any .circuits opened, dropped or given as the
// argument); the player's progress -- the stars, the boards, the lessons seen, the levels open --
// SD0:/home/circuits/progress.ini (kv.h). The engine: circuit.h (no UI).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <uikit.h>
#include "circuit.h"
#include "lessons.h"

using namespace circuits;

#define APPDIR		"SD0:/apps/circuits.app/"
#define HOMEDIR		"SD0:/home/circuits"
#define PROGRESS	HOMEDIR "/progress.ini"
#define DOCK_H		96

static struct ui_vars ui;

// ---- the game's state ---------------------------------------------------------------------------------------------------
static int g_lang = 0;					// 0 English, 1 French (the system's, through ui_lang)
enum { MAXPACKS = 12 };
static Pack *g_packs[MAXPACKS]; static int g_npacks = 0;
static int g_pack = -1, g_level = -1;
static Level *g_L = 0;					// the level shown
static circuits::Progress g_pr;
static Circuit g_c;					// its board
static History *g_hist;					// (on the heap: 65 texts)
static Eval g_ev;					// the board evaluated on the current row
static unsigned g_row = 0;				// the switches, as a row of the truth table (the first input the MSB)
static int g_step = -1;					// step mode: the depth computed so far; -1 live
static int g_sel = -1;					// the gate selected
static int g_selDst = -1, g_selPin = 0;			// the wire selected: the part it feeds, the pin
static int g_armed = -1;				// the palette's gate armed (-1: Select)
static int g_errPart = -1;				// the part a refused Check named (outlined red)
static bool g_checked = false;				// a Check's result shown in the table
static CheckResult g_res;
static bool g_showHint = false;
static bool g_dirty = false, g_saveFailed = false;	// the board changed since saved; the last save failed
static unsigned g_dirtyAt = 0;
static bool g_keys = false;				// a screen of 800 px or less: the arrows and Enter drive the board
static bool g_pk = false;				// a window under 720 px wide: the levels a drawer
static bool g_drawer = false;				// ... open
static bool g_quit = false;
static const Lesson *g_wantLesson = 0;			// a dialog to open once the event at hand is done
static bool g_wantResult = false;
enum { M_NONE, M_INFO, M_OK, M_ERR };
enum { SB_W = 14 };

static long g_vList, g_vCard, g_vBoard, g_vMsg, g_vTable, g_vCount, g_vHints;

static void set_message (int kind, const char *text, int stars = 0);
static void prompt ();
static void refresh ();
static void board_changed ();
static void select_gate (int i);
static void select_wire (int dst, int pin);
static void toggle_switch (int k);
static void set_row (unsigned r);
static void arm (int type);
static void level_clicked (int p, int l);

// the time in hundredths of a second
static unsigned ticks ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 100 + t.tv_nsec / 10000000);
}
// A view to repaint; an open drawer is over it, and goes after it
static void view_dirty (long v) { ui_view_dirty (v); if (g_drawer && v != g_vList) ui_view_dirty (g_vList); }
static bool ui_theme_flat () { return *(const long *) ui_theme () == 2; }
#include "gates.h"
// A word of a context: "one|IN" in the dictionary, else the word
static const char *TRC (const char *ctx, const char *s)
{
	char k[64]; snprintf (k, sizeof k, "%s|%s", ctx, s);
	const char *t = TR (k);
	return strcmp (t, k) ? t : TR (s);
}
// The engine's refusals in words (TR'd where shown)
static const char *err_word (Err e)
{
	switch (e)
	{
	case E_OVERLAP: return TRN ("No room there: parts cannot overlap.");
	case E_OUTSIDE: return TRN ("Gates go between the inputs and the outputs.");
	case E_FULL: return TRN ("The board is full: 48 gates at most.");
	case E_LOOP: return TRN ("That wire would make a loop: in this game signals only go forward.");
	case E_NOT_ALLOWED: return TRN ("This gate is not allowed in this level.");
	default: return TRN ("A wire goes from an output to an input.");
	}
}

#include "board.h"
#include "views.h"

// ---- the widgets --------------------------------------------------------------------------------------------------------------
static Board g_board;
static LevelList g_list;
static Card g_card;
static MsgBar g_msg;
static TruthTable g_table;
static CountView g_count;
static long g_btHint, g_btLesson, g_btNext, g_tb, g_noGate, g_lbTable, g_lbMode;
static long g_btLevels, g_btDel, g_btUndo, g_btRedo, g_btStep, g_btReset, g_btCheck;
static char g_tips[P_COUNT_][96];
static char g_hintText[200];
static int g_hintH = 0;
static int g_boardX, g_boardY, g_palX[P_COUNT_], g_palY;	// (their places: a drag from the palette)

// ---- the packs and the progress -----------------------------------------------------------------------------------------
static const char *base_name (const char *path)
{
	const char *b = path;
	for (const char *p = path; *p; p++) if (*p == '/' || *p == ':') b = p + 1;
	return b;
}
static bool ends_circuits (const char *name) { int l = (int) strlen (name); return l > 9 && !strcasecmp (name + l - 9, ".circuits"); }
// A pack read and added (opened: from a file -- all its levels open) -> its index, -1 (quiet: no dialog)
static int add_pack (const char *path, bool opened, bool quiet)
{
	for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) return i;
	if (g_npacks >= MAXPACKS) return -1;
	char m[600];
	kv_doc *d = kv_load (path, KV_PIPES);
	if (!d)
	{
		if (!quiet) { snprintf (m, sizeof m, TR ("\xE2\x80\x9C%s\xE2\x80\x9D cannot be read."), base_name (path)); ui_message ((long) TR ("Open Level Pack"), (long) m, UM_OK); }
		return -1;
	}
	Pack *pk = new Pack; char why[200] = "";
	bool ok = parse_pack_kv (*pk, d, why, sizeof why);
	kv_free (d);
	if (ok)
	{
		g_packs[g_npacks] = pk;
		ok = ids_unique (g_packs, g_npacks + 1, why, sizeof why);
	}
	if (!ok)
	{
		if (!quiet)
		{
			int n = snprintf (m, sizeof m, TR ("\xE2\x80\x9C%s\xE2\x80\x9D cannot be opened:"), base_name (path));
			snprintf (m + n, sizeof m - (size_t) n, "\n%s.\n\n%s", why, TR ("No level was added."));
			ui_message ((long) TR ("Open Level Pack"), (long) m, UM_OK);
		}
		delete pk; return -1;
	}
	snprintf (pk->path, sizeof pk->path, "%s", path);
	pk->opened = opened;
	if (!pk->title[0][0])
	{
		snprintf (pk->title[0], sizeof pk->title[0], "%s", base_name (path));
		char *dot = strrchr (pk->title[0], '.'); if (dot) *dot = 0;
	}
	return g_npacks++;
}
static void load_packs ()
{
	const char *dir = APPDIR "levels";
	char names[MAXPACKS][128]; int n = 0;
	DIR *d = opendir (dir);
	if (d)
	{
		struct dirent *e;
		while ((e = readdir (d)) && n < MAXPACKS)
			if (ends_circuits (e->d_name)) snprintf (names[n++], sizeof names[0], "%s", e->d_name);
		closedir (d);
	}
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--) { char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	for (int i = 0; i < n; i++) { char p[256]; snprintf (p, sizeof p, "%s/%s", dir, names[i]); add_pack (p, false, true); }
}
static void save_progress ()
{
	if (kv_save (g_pr.kv, PROGRESS, "# Circuits -- the player's progress (written by the game)") == 0) { g_saveFailed = false; return; }
	if (!g_saveFailed) set_message (M_ERR, TR ("Progress not saved: the card is full or read-only."));
	g_saveFailed = true;
}
static void save_board ()				// the board of the level shown into the progress, written
{
	if (!g_L) return;
	static char t[TEXTCAP];
	write_text (g_c, t, sizeof t);
	g_dirty = false;
	if (!strcmp (g_pr.circuit (g_L->id), t)) return;
	g_pr.setCircuit (g_L->id, t);
	save_progress ();
}
// The level after / before (p, l) in the list -> false: none
static bool next_of (int p, int l, int *np, int *nl)
{
	if (p < 0) return false;
	if (l + 1 < g_packs[p]->n) { *np = p; *nl = l + 1; return true; }
	for (int q = p + 1; q < g_npacks; q++) if (g_packs[q]->n) { *np = q; *nl = 0; return true; }
	return false;
}
static bool prev_of (int p, int l, int *np, int *nl)
{
	if (p < 0) return false;
	if (l > 0) { *np = p; *nl = l - 1; return true; }
	for (int q = p - 1; q >= 0; q--) if (g_packs[q]->n) { *np = q; *nl = g_packs[q]->n - 1; return true; }
	return false;
}
static bool next_open ()
{
	int np, nl;
	return next_of (g_pack, g_level, &np, &nl) && level_open (g_pr, *g_packs[np], nl);
}

// ---- the message bar, the state of the buttons --------------------------------------------------------------------------
static void set_message (int kind, const char *text, int stars)
{
	g_msg.kind = kind; g_msg.stars = stars; snprintf (g_msg.text_, sizeof g_msg.text_, "%s", text);
	bool next = kind == M_OK && next_open () && !g_pk;
	ui_w_show (g_btNext, next); g_msg.roomForNext = next;
	view_dirty (g_vMsg);
}
static bool first_level () { return g_pack == 0 && g_level == 0; }
static void prompt ()
{
	if (!g_L) return;
	if (g_pk) set_message (M_INFO, g_L->textOf (g_lang));	// (the card has only the title there)
	else if (first_level () && !g_pr.stars (g_L->id)) set_message (M_INFO, TR ("Welcome to Circuits! Wire the switch A to the lamp, then Check (F5)."));
	else set_message (M_INFO, TR ("Every lamp follows the switches. When you think it is right: Check (F5)."));
}
static void step_message ()
{
	char m[300];
	if (g_step >= g_ev.maxDepth) snprintf (m, sizeof m, TR ("Step %d of %d: every lamp is computed. F9: back to the start \xC2\xB7 F7: live."), g_step, g_ev.maxDepth);
	else if (g_step == 0) snprintf (m, sizeof m, TR ("Step 0 of %d: no gate is computed yet. F8: the first depth \xC2\xB7 F7: live."), g_ev.maxDepth);
	else snprintf (m, sizeof m, TR ("Step %d of %d: the gates of depth %d are computed. F8: the next depth \xC2\xB7 F9: back to the start \xC2\xB7 F7: live."), g_step, g_ev.maxDepth, g_step);
	set_message (M_INFO, m);
}
// A button's state, drawn when it changes
static void w_enable (long w, bool on, signed char *was)
{
	if (*was == (signed char) on) return;
	*was = (signed char) on;
	ui_w_enable (w, on); ui_w_draw (w);
}
static void update_tools ()
{
	static signed char su = -1, sr = -1, sd = -1, ss = -1, st = -1;
	w_enable (g_btUndo, g_hist->canUndo (), &su);
	w_enable (g_btRedo, g_hist->canRedo (), &sr);
	w_enable (g_btDel, g_sel >= 0 || g_selDst >= 0, &sd);
	w_enable (g_btReset, g_step >= 0, &ss);
	if (st != (signed char) (g_step >= 0)) { st = (signed char) (g_step >= 0); ui_w_style (g_btStep, g_step >= 0 ? UB_DEFAULT : 0); ui_w_style (g_btCheck, g_step >= 0 ? 0 : UB_DEFAULT); ui_w_draw (g_btStep); ui_w_draw (g_btCheck); }
	static char mode[128], old[128];
	if (g_step >= 0) snprintf (mode, sizeof mode, TR ("Step %d of %d \xC2\xB7 F7: back to live"), g_step, g_ev.maxDepth);
	else snprintf (mode, sizeof mode, "%s", g_keys ? TR ("Live: Enter on a switch flips it.") : TR ("Live: click a switch or a row."));
	if (strcmp (mode, old))
	{
		strcpy (old, mode);
		ui_w_set_text (g_lbMode, (long) mode); ui_w_style (g_lbMode, g_step >= 0 ? ULB_BOLD : ULB_DIM);
		ui_w_draw (g_lbMode);
	}
}
// The board evaluated on the current row (live, or to the step's depth), every view of it redrawn
static void refresh ()
{
	if (!g_L) return;
	if (g_step >= 0) evaluate_to (g_c, g_row, g_step, g_ev); else evaluate (g_c, g_row, g_ev);
	update_tools ();
	view_dirty (g_vBoard); view_dirty (g_vTable); view_dirty (g_vCount);
}
// After the board changed (pushed: an edit; not: an undo / redo): step mode left, the Check's marks cleared, saved soon
static void after_edit (bool push)
{
	if (g_step >= 0) { g_step = -1; prompt (); }
	g_checked = false; g_errPart = -1;
	if (push) g_hist->push (g_c);
	if (g_sel >= g_c.n || (g_sel >= 0 && !gate_type (g_c.p[g_sel].type))) g_sel = -1;
	if (g_selDst >= 0 && (g_selDst >= g_c.n || g_c.p[g_selDst].in[g_selPin] < 0)) g_selDst = -1;
	g_dirty = true; g_dirtyAt = ticks ();
	if (g_msg.kind == M_ERR || g_msg.kind == M_OK) prompt ();
	refresh ();
}
static void board_changed () { after_edit (true); }
static void select_gate (int i) { g_sel = i; g_selDst = -1; update_tools (); view_dirty (g_vBoard); }
static void select_wire (int dst, int pin) { g_selDst = dst; g_selPin = pin; g_sel = -1; update_tools (); view_dirty (g_vBoard); }
static void set_row (unsigned r)
{
	g_row = r;
	if (g_step >= 0) { g_step = 0; refresh (); step_message (); return; }
	refresh ();
}
static void toggle_switch (int k) { set_row (g_row ^ (1u << (g_c.ni - 1 - k))); }
static void arm (int type)
{
	g_armed = type;
	for (int t = 0; t < P_COUNT_; t++) if (g_pal[t]) view_dirty (g_pal[t]);
	if (type >= 0)
	{
		select_gate (-1);
		char m[200]; snprintf (m, sizeof m, g_keys ? TR ("%s: the arrows choose its place, Enter puts it (Esc: cancel).") : TR ("%s: click a free place on the board to put it (Esc: cancel)."), gate_word (type));
		set_message (M_INFO, m);
	}
	else if (g_msg.kind == M_INFO) prompt ();
	view_dirty (g_vBoard);
}

// ---- the level shown ------------------------------------------------------------------------------------------------------
enum { PAD = 10, LISTW = 214, RIGHTW = 238, TBH = 46, MSGH = 50, TIPW = 40 };
static int g_palN = 0;					// the palette's gates shown (palette_setup)
static void redraw_all ()				// (something hidden, or moved: what is under it comes back)
{
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	ui_view_dirty (g_vCard); ui_view_dirty (g_vBoard); ui_view_dirty (g_vMsg); ui_view_dirty (g_vTable); ui_view_dirty (g_vCount); ui_view_dirty (g_vList);
	if (g_vHints) ui_view_dirty (g_vHints);
	for (int t = 0; t < P_COUNT_; t++) if (g_pal[t]) ui_view_dirty (g_pal[t]);
	ui_w_draw (0);
	ui_update_all ();
}
// Three columns: the levels (a fixed width), the level's card, palette, board and message bar in the middle (taking
// what is left: the board scales its cell to it), the bench (a fixed width) on the right. Narrower than 920: the
// list narrower; lower than 600: Step, Reset and Check on one row, the table's rows shorter (they scroll when they do
// not fit). Under 720 wide (640 x 480): the list a drawer over the window, the card a title, and the message on the
// bench: the board has the height.
static void layout ()
{
	const int W = (int) ui.w, H = (int) ui.h - g_hintH;
	bool pk = W < 720;
	if (pk != g_pk) { g_pk = pk; g_drawer = false; }
	bool low = H < 600;
	const int P = low ? 6 : (int) PAD, GAP = low ? 4 : 6, MH = g_pk ? 66 : low ? 40 : (int) MSGH;
	int listW = W < 920 ? 172 : (int) LISTW;
	int mid = P + listW + P, rx = W - P - RIGHTW, mw = rx - P - mid;
	if (g_pk) { mid = P; mw = rx - P - mid; }
	if (g_pk)
	{
		g_list.width = 250; g_list.height = H;
		ui_w_move (g_vList, 0, 0, g_list.width, g_list.height);
		ui_w_show (g_vList, g_drawer);
	}
	else
	{
		g_list.width = listW; g_list.height = H - 2 * P;
		ui_w_move (g_vList, P, P, listW, H - 2 * P);
		ui_w_show (g_vList, 1);
	}
	ui_w_move (g_sbList, (g_pk ? 0 : P) + g_list.width - SB_W - 2, (g_pk ? 0 : P) + 2, SB_W, g_list.height - 4);
	// compact: Hint and Lesson on the bench (in the place of its title), the card's text on its whole width
	bool compact = low || W < 920;
	g_card.btnRoom = compact ? 0 : 180; g_card.titleOnly = g_pk;
	g_card.width = mw;
	int ch = g_card.need (); g_card.height = ch;
	ui_w_move (g_vCard, mid, P, mw, ch);
	long parent = compact ? 0 : g_vCard;
	ui_w_set_parent (g_btHint, parent); ui_w_set_parent (g_btLesson, parent);
	ui_w_move (g_btHint, compact ? rx : mid + mw - 176, compact ? P : P + 10, compact ? (RIGHTW - 6) / 2 : 80, 28);
	ui_w_move (g_btLesson, compact ? rx + (RIGHTW + 6) / 2 : mid + mw - 90, compact ? P : P + 10, compact ? (RIGHTW - 6) / 2 : 80, 28);
	ui_w_show (g_lbTable, !compact);
	int ty = P + ch + GAP;
	ui_w_move (g_tb, mid, ty, mw, TBH);
	// the palette; Delete, Undo, Redo at the toolbar's right; no room after the palette: Undo and Redo away (Ctrl+Z, Ctrl+Y)
	int bx = mid + 6;
	ui_w_show (g_btLevels, g_pk);
	if (g_pk) { ui_w_move (g_btLevels, bx, ty + (TBH - 34) / 2, 34, 34); bx += 38; }
	g_palY = ty + (TBH - 40) / 2; g_palX[P_SWITCH] = bx;
	ui_w_move (g_pal[P_SWITCH], bx, g_palY, 40, 40);
	bx += 46;
	int gx = bx;
	for (int t = P_NOT; t < P_COUNT_; t++)
	{
		bool on = g_L && g_L->allows (t);
		ui_w_show (g_pal[t], on);
		if (on) { g_palX[t] = gx; ui_w_move (g_pal[t], gx, g_palY, 40, 40); gx += 41; }
	}
	bool undo = mw - 112 >= gx - mid + 6;
	ui_w_show (g_btUndo, undo); ui_w_show (g_btRedo, undo);
	ui_w_move (g_btRedo, mid + mw - 40, ty + (TBH - 34) / 2, 34, 34); ui_w_move (g_btUndo, mid + mw - 74, ty + (TBH - 34) / 2, 34, 34);
	int dx = undo ? mid + mw - 112 : mid + mw - 40;
	ui_w_move (g_btDel, dx, ty + (TBH - 34) / 2, 34, 34);
	ui_w_move (g_noGate, bx + 2, ty + (TBH - 30) / 2, dx - bx - 6 < 290 ? dx - bx - 6 : 290, 30);
	int by = ty + TBH + GAP, bh = g_pk ? H - P - by : H - P - MH - GAP - by;
	g_board.resize (mw, bh); g_boardX = mid; g_boardY = by;
	ui_w_move (g_vBoard, mid, by, mw, bh);
	g_msg.width = g_pk ? (int) RIGHTW : mw; g_msg.height = MH; g_msg.px = g_pk ? PX_SMALL : PX_UI; g_msg.lines = g_pk ? 4 : 2;
	ui_w_move (g_lbTable, rx, P, RIGHTW, 22);
	// the bench's buttons: Step and Reset over Check; low: the three on one row at the bottom
	int bw = low ? (RIGHTW - 12) / 3 : (RIGHTW - 6) / 2, sy = low ? H - P - 32 : H - P - 36 - 8 - 32;
	// (their words shorter on one row: "short|Step" in the dictionary)
	ui_w_set_text (g_btStep, (long) (low ? TRC ("short", "Step") : TR ("Step"))); ui_w_set_text (g_btReset, (long) (low ? TRC ("short", "Reset") : TR ("Reset")));
	ui_w_move (g_btStep, rx, sy, bw, 32);
	ui_w_move (g_btReset, low ? rx + bw + 6 : rx + (RIGHTW + 6) / 2, sy, bw, 32);
	ui_w_move (g_btCheck, low ? rx + 2 * (bw + 6) : rx, low ? sy : H - P - 36, low ? RIGHTW - 2 * (bw + 6) : RIGHTW, low ? 32 : 36);
	int foot = sy;					// (what is over the buttons: pocket's message)
	if (g_pk) { foot = sy - GAP - MH; ui_w_move (g_vMsg, rx, foot, RIGHTW, MH); }
	else { ui_w_move (g_vMsg, mid, H - P - MH, mw, MH); ui_w_move (g_btNext, mid + mw - 140, H - P - MH + (MH - 30) / 2, 126, 30); }
	// the table: its rows shorter (15 px at the least) when the count and the buttons leave it less than it needs; they
	// scroll, and the count goes, when even so it does not fit
	int tt = compact ? P + 28 + GAP : P + 24;	// the table's top
	g_table.rh = TruthTable::RH;
	g_table.width = RIGHTW;
	int room = foot - GAP - 66 - GAP - tt;
	if (g_L && g_table.need () > room) { int r = (room - TruthTable::Y0 - 8) / g_L->rows (); g_table.rh = r < 15 ? 15 : r > TruthTable::RH ? (int) TruthTable::RH : r; }
	bool count = g_table.need () <= room;
	g_table.height = g_table.need ();
	if (g_table.height > foot - GAP - tt) g_table.height = foot - GAP - tt;
	ui_w_move (g_vTable, rx, tt, RIGHTW, g_table.height);
	int cy = tt + g_table.height + GAP + 4;
	g_count.width = RIGHTW; g_count.height = 66;
	ui_w_show (g_vCount, count);
	ui_w_move (g_vCount, rx, cy, RIGHTW, 66);
	ui_w_move (g_lbMode, rx, sy - 22, RIGHTW, 18);
	ui_w_show (g_lbMode, cy + 66 <= sy - 22 && !g_keys && count);	// (16 rows at the minimum height: no room; the keys: the hint line says it)
	if (g_vHints) ui_w_move (g_vHints, 0, H, W, g_hintH);
	g_list.showSel ();
	redraw_all ();
}
// The palette: the gates the level allows, in their order (the others hidden); none: the label
static void palette_setup ()
{
	int k = 0; g_palN = 0;
	for (int t = P_NOT; t < P_COUNT_; t++)
	{
		if (!g_L->allows (t)) continue;
		k++; g_palN = k;
		snprintf (g_tips[t], sizeof g_tips[t], TR ("Put a gate: %s (%d)"), gate_word (t), k);
		ui_w_tip (g_pal[t], (long) g_tips[t]);
	}
	ui_w_show (g_noGate, k == 0);
	arm (-1);
}
// The lesson of the level's concept (first: the first time -- remembered)
static bool show_lesson (bool first)
{
	const Lesson *ls = g_L && g_L->topic[0] ? find_lesson (g_L->topic) : 0;
	if (!ls)
	{
		if (!first) set_message (M_INFO, TR ("This level has no lesson of its own: its idea was taught before."));
		return false;
	}
	g_wantLesson = ls;
	char m[200];
	if (ls->gate >= 0) snprintf (m, sizeof m, TR ("New gate: %s. Read the card, then build."), gate_word (ls->gate));
	else snprintf (m, sizeof m, TR ("New idea: %s. Read the card, then build."), ls->title[g_lang]);
	set_message (M_INFO, m);
	if (first) { g_pr.setSeen (g_L->topic); save_progress (); }
	return true;
}
static void show_level (int p, int l)
{
	if (p < 0 || p >= g_npacks || l < 0 || l >= g_packs[p]->n) return;
	if (g_board.drag != Board::D_NONE) g_board.cancel ();
	save_board ();
	g_pack = p; g_level = l; g_L = g_packs[p]->lv[l];
	g_c.setup (*g_L);
	const char *t = g_pr.circuit (g_L->id);
	if (t[0] && !read_text (g_c, *g_L, t)) g_c.setup (*g_L);
	g_hist->start (g_c);
	g_row = 0; g_step = -1; g_sel = g_selDst = -1; g_errPart = -1; g_checked = false; g_showHint = false; g_dirty = false;
	palette_setup ();
	g_pr.setLast (base_name (g_packs[p]->path), g_L->id);
	if (!(g_L->topic[0] && !g_pr.seen (g_L->topic) && show_lesson (true))) prompt ();
	save_progress ();
	refresh ();
	layout ();
}
static void drawer (bool open)
{
	if (!g_pk || open == g_drawer) return;
	g_drawer = open;
	ui_w_show (g_vList, open);
	if (open) { g_list.showSel (); ui_w_draw (g_vList); }
	else redraw_all ();
}
static void level_clicked (int p, int l)
{
	if (level_open (g_pr, *g_packs[p], l)) drawer (false);	// (chosen: the drawer away)
	if (p == g_pack && l == g_level) return;
	if (level_open (g_pr, *g_packs[p], l)) { show_level (p, l); return; }
	int pp, pl; char m[200];
	if (prev_of (p, l, &pp, &pl)) snprintf (m, sizeof m, TR ("Locked: win \xE2\x80\x9C%s\xE2\x80\x9D first."), g_packs[pp]->lv[pl]->titleOf (g_lang));
	else snprintf (m, sizeof m, "%s", TR ("This level is locked."));
	set_message (M_INFO, m);
}

// ---- the actions ----------------------------------------------------------------------------------------------------------
static void inputs_text (unsigned row, char *out, int cap)	// "A = 1 and B = 0", "A = 1, B = 0 and Cin = 1"
{
	int n = 0; out[0] = 0;
	for (int k = 0; k < g_L->ninputs; k++)
	{
		char sep[16];
		if (k == 0) sep[0] = 0;
		else if (k + 1 == g_L->ninputs) snprintf (sep, sizeof sep, " %s ", TR ("and"));
		else strcpy (sep, ", ");
		n += snprintf (out + n, n < cap ? (size_t) (cap - n) : 0, "%s%s = %u", sep, g_L->inName[k], (row >> (g_L->ninputs - 1 - k)) & 1);
	}
}
static void do_check ()
{
	if (!g_L) return;
	if (g_board.drag != Board::D_NONE) g_board.cancel ();
	g_step = -1;
	CheckResult r = check (g_c, *g_L);
	char m[400];
	if (r.err == E_LAMP_OPEN)
	{
		g_errPart = r.errPart; g_checked = false;
		snprintf (m, sizeof m, TR ("Lamp %s is not connected: wire a gate's output to it, then Check again."), g_c.p[r.errPart].name);
		set_message (M_ERR, m); refresh (); return;
	}
	if (r.err != E_OK)
	{
		g_errPart = r.errPart; g_checked = false;
		snprintf (m, sizeof m, TR ("This %s gate (outlined in red) has an input not connected: wire it, then Check again."), gate_word (g_c.p[r.errPart].type));
		set_message (M_ERR, m); refresh (); return;
	}
	g_errPart = -1; g_checked = true; g_res = r;
	if (!r.won)
	{
		g_row = (unsigned) r.firstWrong;
		int o = 0; while (o + 1 < g_L->noutputs && ((g_L->want[o] ^ r.got[o]) >> r.firstWrong & 1) == 0) o++;
		bool light = (g_L->want[o] >> r.firstWrong) & 1;
		char head[80], ins[120], body[300];
		if (r.nwrong == 1) snprintf (head, sizeof head, "%s", TR ("1 row is wrong:"));
		else snprintf (head, sizeof head, TR ("%d rows are wrong:"), r.nwrong);
		inputs_text ((unsigned) r.firstWrong, ins, sizeof ins);
		if (g_L->noutputs == 1) snprintf (body, sizeof body, light ? TR ("with %s the lamp must light.") : TR ("with %s the lamp must stay off."), ins);
		else snprintf (body, sizeof body, light ? TR ("with %s lamp %s must light.") : TR ("with %s lamp %s must stay off."), ins, g_L->outName[o]);
		snprintf (m, sizeof m, "%s %s %s", head, body, TR ("The switches are set on that row."));
		set_message (M_ERR, m); refresh (); return;
	}
	// won: the stars kept, the next level opened, the board saved
	bool rec = g_pr.record (g_L->id, r.stars);
	unlock_after (g_pr, g_packs, g_npacks, g_pack, g_level);
	{ static char t[TEXTCAP]; write_text (g_c, t, sizeof t); g_pr.setCircuit (g_L->id, t); g_dirty = false; save_progress (); }
	if (r.stars == 3) snprintf (m, sizeof m, TR ("Well done! %d %s \xE2\x80\x94 the best possible."), r.gates, gates_word (r.gates));
	else snprintf (m, sizeof m, TR ("Well done! %d %s. Three stars need %d."), r.gates, gates_word (r.gates), g_L->par3);
	set_message (M_OK, m, r.stars);
	int np, nl;
	g_result.stars = r.stars; g_result.gates = r.gates; g_result.record = rec; g_result.last = !next_of (g_pack, g_level, &np, &nl);
	g_wantResult = true;
	view_dirty (g_vList);
	refresh ();
}
static void do_step ()
{
	if (!g_L) return;
	if (g_step < 0) g_step = 0;
	else { evaluate (g_c, g_row, g_ev); if (g_step < g_ev.maxDepth) g_step++; }
	g_checked = false;
	refresh (); step_message ();
}
static void do_live () { if (g_step < 0) return; g_step = -1; refresh (); prompt (); }
static void do_reset () { if (g_step < 0) return; g_step = 0; refresh (); step_message (); }
static void do_next ()
{
	int np, nl;
	if (next_of (g_pack, g_level, &np, &nl) && level_open (g_pr, *g_packs[np], nl)) show_level (np, nl);
	else set_message (M_INFO, TR ("No next level open yet."));
}
static void do_prev () { int np, nl; if (prev_of (g_pack, g_level, &np, &nl)) show_level (np, nl); }
static void do_clear () { if (!g_L) return; g_c.clear (); select_gate (-1); board_changed (); }
static void do_undo () { if (g_L && g_hist->undo (g_c, *g_L)) { g_sel = g_selDst = -1; after_edit (false); } }
static void do_redo () { if (g_L && g_hist->redo (g_c, *g_L)) { g_sel = g_selDst = -1; after_edit (false); } }
static void do_delete ()
{
	if (!g_L) return;
	if (g_sel >= 0) { g_c.remove (g_sel); g_sel = -1; board_changed (); }
	else if (g_selDst >= 0) { g_c.disconnect (g_selDst, g_selPin); g_selDst = -1; board_changed (); }
}
static void do_hint ()
{
	if (!g_L) return;
	g_showHint = !g_showHint;
	if (g_showHint) { char m[600]; snprintf (m, sizeof m, "%s %s", TR ("Hint:"), g_L->hintOf (g_lang)); set_message (M_INFO, m); }
	else prompt ();
	layout ();
}
static void do_lesson () { show_lesson (false); }
static void copy_table ()
{
	if (!g_L) return;
	static char t[4096];
	truth_table_text (*g_L, g_checked ? &g_res : 0, t, sizeof t);
	ui_clip_set ((long) t, -1);
	set_message (M_INFO, TR ("Truth table copied."));
}
static void copy_circuit ()
{
	if (!g_L) return;
	static char t[TEXTCAP];
	write_text (g_c, t, sizeof t);
	ui_clip_set ((long) t, -1);
	set_message (M_INFO, TR ("Circuit copied."));
}
static void open_pack (const char *path)
{
	int p = add_pack (path, true, false);
	if (p < 0) return;
	view_dirty (g_vList);
	show_level (p, 0);
}
static void move_sel (int dx, int dy)
{
	if (g_sel < 0) return;
	if (g_c.move (g_sel, g_c.p[g_sel].x + dx, g_c.p[g_sel].y + dy) == E_OK) board_changed ();
}
static void arm_nth (int n)					// the n-th gate of the palette (0-based)
{
	for (int t = P_NOT; t < P_COUNT_; t++) if (g_L && g_L->allows (t) && n-- == 0) { arm (t); return; }
}

// ---- the hint line at the window's foot (the keys drive): what the keys do, by what the view is doing -------------------------
static void hints ()
{
	if (!g_keys) return;
	char h[200]; int n = 0;
#define H(key, what) n += snprintf (h + n, sizeof h - (size_t) n, "%s\t%s\n", key, what)
	if (g_drawer) { H ("\xE2\x86\x95", TR ("level")); H ("Enter", TR ("play it")); H ("L", TR ("close")); }
	else if (g_board.drag == Board::D_WIRE) { H ("\xE2\x86\x94", TR ("to an input pin")); H ("Enter", TR ("plug")); H ("Esc", TR ("cancel")); }
	else if (g_board.drag != Board::D_NONE) { H ("\xE2\x86\x94", TR ("move")); H ("Enter", TR ("put down")); H ("Esc", TR ("cancel")); }
	else if (g_armed >= 0) { H ("\xE2\x86\x94", TR ("place")); H ("Enter", TR ("put")); H ("Esc", TR ("cancel")); }
	else if (g_step >= 0) { H ("F8", TR ("next depth")); H ("F9", TR ("start")); H ("F7", TR ("live")); }
	else
	{
		H ("\xE2\x86\x94", TR ("point")); H ("Enter", TR ("press"));
		if (g_palN > 0) { char k[8]; snprintf (k, sizeof k, g_palN > 1 ? "1-%d" : "%d", g_palN); H (k, TR ("gate")); }
		H ("F5", TR ("Check")); H ("F8", TR ("Step")); H ("F2", TR ("Hint"));
	}
#undef H
	if (n && h[n - 1] == '\n') h[n - 1] = 0;
	if (strcmp (h, g_hintText)) { strcpy (g_hintText, h); ui_view_dirty (g_vHints); }
}
static void hints_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_BG);
	ui_pad_hints ((long) g_hintText, 8, 2, h - 4, 0);
}

// ---- the views' callbacks ---------------------------------------------------------------------------------------------------
static void board_paint (long, long, long) { g_board.paint (); }
static long board_mouse (long, long kind, long x, long y, long info)
{
	if (kind == 1) { drawer (false); ui_w_focus (g_vBoard); }
	g_board.mouse ((int) kind, (int) x, (int) y, (int) (info >> 8 & 255));
	return 0;
}
static void list_paint (long, long, long) { g_list.paint (); }
static long list_mouse (long, long kind, long x, long y, long info) { g_list.mouse ((int) kind, (int) x, (int) y, (int) (info >> 8 & 255)); return 0; }
static long list_wheel (long, long n) { g_list.wheel ((int) n); return 0; }
static void card_paint (long, long, long) { g_card.paint (); }
static void msg_paint (long, long, long) { g_msg.paint (); }
static void table_paint (long, long, long) { g_table.paint (); }
static long table_mouse (long, long kind, long, long y, long info) { g_table.mouse ((int) kind, (int) y, (int) (info >> 8 & 255)); return 0; }
static void count_paint (long, long, long) { g_count.paint (); }
static long pal_mouse (long v, long kind, long x, long y, long info)
{
	int t = pal_index (v), type = pal_type (t);
	if (kind == 3) { if (g_palHot == t) { g_palHot = -1; view_dirty (v); } return 0; }
	if (g_palHot != t) { int was = g_palHot; g_palHot = t; if (was >= 0) view_dirty (g_pal[was]); view_dirty (v); }
	if (kind == 1 && (info >> 8 & 1))
	{
		g_palPress = t;
		drawer (false);
		arm (type < 0 || g_armed == type ? -1 : type);
		return 0;
	}
	if (g_palPress < 0 || g_armed < 0) { if (kind == 2) g_palPress = -1; return 0; }
	// (the drag: its point in the board's coordinates)
	int bx = (int) x + g_palX[t] - g_boardX, by = (int) y + g_palY - g_boardY;
	g_board.px = bx; g_board.py = by;
	g_board.inside = bx >= 0 && by >= 0 && bx < g_board.width && by < g_board.height;
	if (kind == 2)
	{
		g_palPress = -1;
		if (g_board.inside) g_board.release (bx, by);	// (let go on the palette itself: a click, the gate stays armed)
		else g_board.inside = false;
	}
	view_dirty (g_vBoard);
	return 0;
}

// ---- the menus ----------------------------------------------------------------------------------------------------------------
enum { MI_NEXT = 1, MI_PREV, MI_RESTART, MI_QUIT, MI_UNDO, MI_REDO, MI_DEL, MI_CLEAR, MI_COPYT, MI_COPYC, MI_CHECK, MI_STEP, MI_LIVE, MI_RESET, MI_OPEN, MI_LEVELS,
       MI_LESSON, MI_HINT, MI_ABOUT };
static void m_open ()
{
	mkdir (HOMEDIR, 0777);
	char path[256] = "";
	ui_file_filter ((long) "*.circuits");
	long got = ui_file_open ((long) HOMEDIR, (long) path, sizeof path);
	ui_file_filter (0);
	if (got && path[0]) open_pack (path);
}
static void m_about ()
{
	char m[300]; snprintf (m, sizeof m, "%s\n%s", TR ("Circuits 1.0 \xE2\x80\x94 wire logic gates until the lamps match the truth table."), TR ("MIT License."));
	ui_message ((long) TR ("About Circuits"), (long) m, UM_OK);
}
static void build_menu ()
{
	static char b[2000]; int n = 0;
#define MENU(t) n += snprintf (b + n, sizeof b - (size_t) n, "M%s\n", TR (t))
#define ITEM(id, t, k) n += snprintf (b + n, sizeof b - (size_t) n, "I%d\t%s\t%s\n", (int) id, TR (t), k)
#define SEP() n += snprintf (b + n, sizeof b - (size_t) n, "-\n")
	MENU ("Game");
	ITEM (MI_NEXT, "Next Level", "Ctrl+N"); ITEM (MI_PREV, "Previous Level", "Ctrl+P"); ITEM (MI_RESTART, "Restart Level", ""); SEP ();
	ITEM (MI_QUIT, "Quit", "Ctrl+Q");
	MENU ("Edit");
	ITEM (MI_UNDO, "Undo", "Ctrl+Z"); ITEM (MI_REDO, "Redo", "Ctrl+Y"); SEP ();
	ITEM (MI_DEL, "Delete", TR ("Del")); ITEM (MI_CLEAR, "Clear Board", ""); SEP ();
	ITEM (MI_COPYT, "Copy Truth Table", ""); ITEM (MI_COPYC, "Copy Circuit", "");
	MENU ("Simulate");
	ITEM (MI_CHECK, "Check", "F5"); SEP ();
	ITEM (MI_STEP, "Step", "F8"); ITEM (MI_LIVE, "Live", "F7"); ITEM (MI_RESET, "Reset", "F9");
	MENU ("Levels");
	ITEM (MI_OPEN, "Open Level Pack...", "Ctrl+O");
	MENU ("Help");
	ITEM (MI_LESSON, "Lesson", "F1"); ITEM (MI_HINT, "Hint", "F2"); SEP ();
	ITEM (MI_ABOUT, "About Circuits", "");
#undef MENU
#undef ITEM
#undef SEP
	ui_menu ((long) b);
}
static void command (int id)
{
	switch (id)
	{
	case MI_NEXT: do_next (); break;
	case MI_PREV: do_prev (); break;
	case MI_RESTART: case MI_CLEAR: do_clear (); break;
	case MI_QUIT: g_quit = true; break;
	case MI_UNDO: do_undo (); break;
	case MI_REDO: do_redo (); break;
	case MI_DEL: do_delete (); break;
	case MI_COPYT: copy_table (); break;
	case MI_COPYC: copy_circuit (); break;
	case MI_CHECK: do_check (); break;
	case MI_STEP: do_step (); break;
	case MI_LIVE: do_live (); break;
	case MI_RESET: do_reset (); break;
	case MI_OPEN: m_open (); break;
	case MI_LESSON: do_lesson (); break;
	case MI_HINT: do_hint (); break;
	case MI_ABOUT: m_about (); break;
	}
}
// A file dropped on the window: a pack
static void on_drop ()
{
	char b[468]; long kind = 0;
	long n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = 0;
	for (int i = 0; b[i]; i++) if (b[i] == '\n' || b[i] == '\r') { b[i] = 0; break; }
	if (ends_circuits (b)) open_pack (b);
}
// A control's news
static void control (long w, long code)
{
	if (code == UN_DROP) { on_drop (); return; }
	if (w == g_sbList) { if (code == UN_CHANGE) { g_list.sc = (int) ui_w_get_value (g_sbList); view_dirty (g_vList); } return; }
	if (code != UN_ACTIVATE) return;
	if (w == g_btHint) do_hint ();
	else if (w == g_btLesson) do_lesson ();
	else if (w == g_btNext) do_next ();
	else if (w == g_btStep) do_step ();
	else if (w == g_btReset) do_reset ();
	else if (w == g_btCheck) do_check ();
	else if (w == g_btUndo) do_undo ();
	else if (w == g_btRedo) do_redo ();
	else if (w == g_btDel) do_delete ();
	else if (w == g_btLevels) drawer (!g_drawer);
	else return;
	ui_w_focus (g_vBoard);					// (Enter is the board's again, not the button's)
}
// A key no control took
static void key (int hid, int mods)
{
	enum { HID_L = 0x0F, HID_1 = 0x1E, HID_6 = 0x23 };
	if (g_drawer)						// the list has the keys
	{
		if (hid == KEY_UP) do_prev ();
		else if (hid == KEY_DOWN) do_next ();
		else if (hid == KEY_ENTER || hid == KEY_ESC || hid == HID_L) drawer (false);
		return;
	}
	if (hid == HID_L && g_pk) { drawer (true); return; }
	switch (hid)
	{
	case KEY_DELETE: case KEY_BACKSPACE: do_delete (); return;
	case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN: case KEY_ENTER:
		if (g_keys) { g_board.key (hid, mods); return; }
		if (hid == KEY_ENTER) return;
		move_sel (hid == KEY_LEFT ? -1 : hid == KEY_RIGHT ? 1 : 0, hid == KEY_UP ? -1 : hid == KEY_DOWN ? 1 : 0);
		return;
	case KEY_ESC:						// a drag, then the armed gate, then the selection
		g_board.kheld = false;
		if (g_board.drag != Board::D_NONE) { g_board.cancel (); prompt (); }
		else if (g_armed >= 0) arm (-1);
		else { select_gate (-1); select_wire (-1, 0); }
		return;
	}
	if (hid >= HID_1 && hid <= HID_6) arm_nth (hid - HID_1);
}

// ---- the window -----------------------------------------------------------------------------------------------------------------
static void on_resize (long, long) { layout (); }
static long make_view (long paint, long mouse, long wheel, int flags = 0)
{
	long v = ui_view (0, 0, 20, 20, flags);
	ui_view_on (v, paint, mouse, 0, wheel);
	return v;
}
static long glyph_button (const char *glyph, const char *tip)
{
	long b = ui_pushbutton (0, 0, 34, 34, (long) glyph, UB_GHOST);
	ui_w_tip (b, (long) tip);
	ui_w_set_parent (b, g_tb);
	return b;
}
static long bench_button (const char *text, const char *tip, long style = 0)
{
	long b = ui_pushbutton (0, 0, 100, 32, (long) text, style);
	ui_w_tip (b, (long) tip);
	return b;
}
static void build_window ()
{
	g_vCard = make_view ((long) card_paint, 0, 0);
	g_btHint = ui_pushbutton (0, 0, 80, 28, (long) TR ("Hint"), 0); ui_w_tip (g_btHint, (long) "F2");
	g_btLesson = ui_pushbutton (0, 0, 80, 28, (long) TR ("Lesson"), 0); ui_w_tip (g_btLesson, (long) "F1");
	// the palette: Select, the six gates (those of the level shown), Delete / Undo / Redo at the right
	g_tb = ui_toolbar (0, 0, 508, TBH);
	g_btLevels = glyph_button ("\xE2\x89\xA1", TR ("Levels"));
	for (int t = P_NOT - 1; t < P_COUNT_; t++)
	{
		int k = t == P_NOT - 1 ? (int) P_SWITCH : t;
		g_pal[k] = make_view ((long) pal_paint, (long) pal_mouse, 0);
		ui_w_set_parent (g_pal[k], g_tb);
	}
	ui_w_tip (g_pal[P_SWITCH], (long) TR ("Select and move (Esc)"));
	g_noGate = ui_label (0, 0, 290, 30, (long) TR ("No gate in this level: a wire is enough."), ULB_DIM);
	ui_w_set_parent (g_noGate, g_tb);
	g_btDel = glyph_button ("\xE2\x9C\x95", TR ("Delete (Del)"));
	g_btUndo = glyph_button ("\xE2\x86\xB6", TR ("Undo (Ctrl+Z)"));
	g_btRedo = glyph_button ("\xE2\x86\xB7", TR ("Redo (Ctrl+Y)"));
	g_vBoard = make_view ((long) board_paint, (long) board_mouse, 0, UVW_FOCUS);
	g_vMsg = make_view ((long) msg_paint, 0, 0);
	g_btNext = ui_pushbutton (0, 0, 126, 30, (long) TR ("Next level"), UB_DEFAULT);
	ui_w_set_parent (g_btNext, g_vMsg);
	ui_w_show (g_btNext, 0);
	// the bench: the truth table, the count, the simulation
	g_lbTable = ui_label (0, 0, RIGHTW, 22, (long) TR ("Truth table"), ULB_BOLD);
	g_vTable = make_view ((long) table_paint, (long) table_mouse, 0);
	g_vCount = make_view ((long) count_paint, 0, 0);
	g_lbMode = ui_label (0, 0, RIGHTW, 18, (long) "", ULB_DIM);
	g_btStep = bench_button (TR ("Step"), TR ("One gate depth more (F8)"));
	g_btReset = bench_button (TR ("Reset"), TR ("Back to step 0 (F9)"));
	g_btCheck = bench_button (TR ("Check"), TR ("Check every row (F5)"), UB_DEFAULT);
	if (g_keys) { g_hintH = 24; g_vHints = make_view ((long) hints_paint, 0, 0); }
	// (last: an open drawer covers the window)
	g_vList = make_view ((long) list_paint, (long) list_mouse, (long) list_wheel);
	g_sbList = ui_scrollbar (0, 0, SB_W, 100, 0);
	ui_w_set_parent (g_sbList, g_vList);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{ char lang[80]; snprintf (lang, sizeof lang, APPDIR "lang/%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	g_lang = !strcmp ((const char *) ui_lang (), "fr") ? 1 : 0;
	mkdir ("SD0:/home", 0777);
	mkdir (HOMEDIR, 0777);
	g_hist = new History;
	g_board.init (); g_list.init ();
	g_card.btnRoom = 180; g_table.hot = -1; g_table.rh = TruthTable::RH; g_table.first = 0;
	load_packs ();
	g_pr.attach (kv_load (PROGRESS, KV_ESCAPES));
	if (g_npacks && g_packs[0]->n && !g_pr.isOpen (g_packs[0]->lv[0]->id)) open_first (g_pr, g_packs, g_npacks);

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	g_keys = scr[0] <= 800;
	// (the frame takes 32; a dock that reserves nothing floats over the desktop's foot: the window stays above it)
	int ww = scr[0] - 8 < 1000 ? (int) scr[0] - 8 : 1000, wh = (int) (scr[1] - scr[3] - scr[2]) - 32 - (scr[2] ? 0 : DOCK_H);
	if (wh > 620) wh = 620;
	if (ui_window_ex (0, scr[3], ww, wh, (long) "Circuits", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (640, 400);
	ui_on_resize ((long) on_resize, 0);
	build_window ();
	build_menu ();
	hints ();
	layout ();
	ui_w_focus (g_vBoard);
	if (g_npacks == 0)
	{
		ui_message ((long) "Circuits", (long) TR ("No level found in SD0:/apps/circuits.app/levels."), UM_OK);
		return 1;
	}
	// where the player was; then the pack given (after: a refusal's dialog over the level shown)
	int p = 0, l = 0;
	const char *lp = g_pr.lastPack (), *ll = g_pr.lastLevel ();
	for (int i = 0; i < g_npacks; i++)
		if (!strcmp (base_name (g_packs[i]->path), lp))
		{
			int k = g_packs[i]->find (ll);
			if (k >= 0 && level_open (g_pr, *g_packs[i], k)) { p = i; l = k; }
		}
	const char *arg = argc > 1 ? argv[1] : "";
	if (arg[0] && ends_circuits (arg)) { int ap = add_pack (arg, true, true); if (ap >= 0) { p = ap; l = 0; } }
	show_level (p, l);
	if (arg[0] && (!ends_circuits (arg) || strcmp (g_packs[p]->path, arg))) open_pack (arg);	// (refused: the dialog says why)

	while (!g_quit)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
				else key (hid, mods);
			}
		}
		else ui_w_idle ();
		if (g_dirty && ticks () - g_dirtyAt >= 100) save_board ();	// (1 s after the last change)
		hints ();
		ui_view_flush ();
		if (g_wantResult)
		{
			g_wantResult = false;
			if (result_dialog ()) do_next ();
			ui_view_flush ();
		}
		if (g_wantLesson)
		{
			const Lesson *ls = g_wantLesson;
			g_wantLesson = 0;
			lesson_dialog (ls);
			if (g_msg.kind == M_INFO) prompt ();
			ui_view_flush ();
		}
	}
	save_board ();
	return 0;
}
