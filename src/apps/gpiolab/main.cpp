//
// gpiolab -- GPIO Lab of Onyx on ARMPI (src/apps/gpiolab/README.md): the Raspberry Pi's 40-pin header on the screen
// (GpioKit, docs/KITS.md).
//
//   The header   the 40 pins as on the board (pin 1 top left, the USB ports down): power, ground and the GPIOs, each in
//                its mode's colour, its level lit; the system's pins greyed. A click (or the arrows) chooses a GPIO.
//   The pin      what it is, its mode (Free, Input, Pull-up, Pull-down, Output, PWM), an output's level or a blink, PWM's
//                frequency and duty, its edges logged, a place in the timing chart; in the simulator, what drives an input.
//   Timing chart the chosen pins' levels over the last seconds, paused or running.
//   I2C bus      the bus on GPIO 2 / 3 scanned, what usually answers at each address; a BME280's readings, an SSD1306's
//                picture (the simulator's) and a test picture sent to it.
//   Edges        the edges of the pins logged, with their time.
//   Code         BASIC written here and run in this process, so that its pins are GPIO Lab's: the line lit as it runs,
//                Speed, Step, Stop; the console under it (the VM of src/lib/basic, from the main loop).
// The simulator (GpioKit's: a board in memory) runs when the system has no GPIO, or when chosen: nothing then touches the
// real pins. Arguments: --sim, --demo, --code, --run, --tab i2c | edges.
//
// One view fills the window and draws all of it; the controls are UIKit's, its children.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <uikit.h>
#include <gpiokit_abi.h>

#include "../../lib/basic/bas.h"
#include "../../lib/basic/basfont.h"
#include "../../lib/basic/hostutil.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int off) { return s_jet[off / 4] & 0xFFFFFF; }
#define C_BG	jet (TJ_FACE)
#define C_FIELD	jet (TJ_SURFACE)
#define C_TEXT	jet (TJ_TEXTE1)
#define C_DIM	jet (TJ_TEXTE2)
#define C_LINE	jet (TJ_LIGNE)
#define C_ACCENT jet (TJ_ACCENT)
#define C_DIS	jet (TJ_TEXTE3)
static unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }

// (a pin and an edge as GpioKit gives them: GPI_*, GPE_*)
struct GpPin { unsigned char pin, mode, level, flags; unsigned owner, freq, duty; char reason[24]; };
struct GpEvent { unsigned char pin, edge, level, lost; unsigned reserved; unsigned long long us; };
static_assert (sizeof (GpPin) == GPI_SIZE && sizeof (GpEvent) == GPE_SIZE, "gpiokit.inc");

static int W = 1120, H = 640;
enum { BANNER = 48, PANEL_H = 214, Y_MODELBL = 46, Y_MODES = 68, Y_ROW = 108, Y_CB = 150, Y_TABS = 12, TABS_H = 38, FOOT = 40 };
enum { RX = 436 };
#define HIST	512			// the timing chart's samples (about 8 s)
#define NWATCH	6			// pins in the chart at most
#define NLOG	200			// edges kept

// ---- the state ---------------------------------------------------------------------------------------
static GpPin g_pin[GP_PINS];
static int  g_sel = 17;				// the chosen GPIO
static unsigned g_hist[HIST]; static int g_nh = 0, g_h0 = 0;	// read_all samples (a ring)
static bool g_paused = false;
static int  g_watch[NWATCH] = { 17, 18, 27, 22, -1, -1 };
static bool g_blink[GP_PINS]; static unsigned long long g_blinkUs;
static GpEvent g_log[NLOG]; static int g_nlog = 0, g_log0 = 0;
static unsigned g_edgeCount[GP_PINS];
static unsigned long long g_t0;			// the edges' times: from the app's start
static unsigned char g_map[16]; static int g_found = -1;	// the I2C scan (-1: not done)
static char g_bme[96], g_i2cMsg[160];
static int  g_freqIx[GP_PINS], g_duty[GP_PINS];	// a PWM pin's choices (the frequency's entry, duty 0..1000: tenths of %)
static int  g_tab = 0, g_view = 0;		// 0 the pins, 1 the code
static char g_status[256];
static bool g_fr;				// the language is French: BASIC's messages, the first sketch, the examples

static const char *const FREQS = "50 Hz (servo)|100 Hz|500 Hz|1 kHz|10 kHz|25 kHz";
static const int FREQ_HZ[] = { 50, 100, 500, 1000, 10000, 25000 };

// the board's own colours, the same in every theme: it is a picture of a board
#define C_PCB		0x1E5B3A
#define C_PCB_EDGE	0x16452B
#define C_3V3		0xF0A030
#define C_5V		0xE0483C
#define C_GND		0x303030
static unsigned mode_col (int mode, int level)
{
	switch (mode)
	{
	case GP_M_IN: case GP_M_PULLUP: case GP_M_PULLDOWN: return level ? 0x5AA9FF : 0x2C5C9A;
	case GP_M_OUT:	return level ? 0xFFD94A : 0x8A6D16;
	case GP_M_PWM:	return 0xB57BFF;
	case GP_M_I2C:	return 0x3CC8B4;
	case GP_M_SPI:	return 0xFF7BB0;
	case GP_M_FREE:	return 0xC9D6CD;
	default:	return 0xE0E0E0;			// (an ALT function)
	}
}

static long g_v, g_viewSw, g_simSw, g_modes, g_tabs, g_drive, g_btLevel, g_btPause, g_btScan, g_btOled;
static long g_cbBlink, g_cbEdges, g_cbWatch, g_ddFreq, g_slDuty, g_lbDuty, g_lbFreq, g_lbDrive;
static long g_ed, g_btRun, g_btStep, g_btStop, g_speed, g_lbSpeed, g_ddEx;
static bool g_running, g_hasHw, g_stepMode, g_waitStep, g_stopReq, g_quit;
static int  g_runReq;				// 1 run, 2 step: the main loop starts it (never from an event)
static long g_keyQ[16]; static int g_nkeyQ;	// the keys typed while a program runs (INKEY$)

static void sync_controls (void);
static void apply_view (void);
static void refresh_pins (void);
static void log_events (const GpEvent *ev, int n);
static void set_status (const char *s) { snprintf (g_status, sizeof g_status, "%s", s); ui_view_dirty (g_v); }
static void fail (int r, const char *what) { char t[240]; snprintf (t, sizeof t, TR ("%s: %s"), what, TR ((const char *) gp_error (r))); set_status (t); }
static bool sim () { return gp_available () == 2; }
static bool watched (int pin) { for (int i = 0; i < NWATCH; i++) if (g_watch[i] == pin) return true; return false; }
static bool is_input (int m) { return m == GP_M_IN || m == GP_M_PULLUP || m == GP_M_PULLDOWN; }
static bool mine (int pin) { return g_pin[pin].owner == 0 || g_pin[pin].owner == (unsigned) getpid (); }

// ---- drawing in the view ------------------------------------------------------------------------------------
enum { PX_UI = 13, PX_SMALL = 12 };
#define V(n)	((n) * 16)
static void text (int x, int y, int h, const char *s, unsigned c, bool bold = false, int px = PX_UI)
{
	ui_text ((long) s, x, y + (h + px) / 2 - 2, c, px, (long) (bold ? ui.font_b : ui.font), 0);
}
static int text_w (const char *s, bool bold = false, int px = PX_UI) { return (int) ui_text_width ((long) s, px, (long) (bold ? ui.font_b : ui.font)); }
static void text_c (int x, int y, int w, int h, const char *s, unsigned c, bool bold = false) { text (x + (w - text_w (s, bold)) / 2, y, h, s, c, bold); }
static void rbox (int x, int y, int w, int h, int r, unsigned c) { ui_rbox (x, y, w, h, r, c, c); }
static void disc (int x, int y, int r, unsigned c) { ui_vp_disc (V (x), V (y), V (r), c); }

// ---- the header ------------------------------------------------------------------------------------------
enum { ROWH = 23, PINX0 = 202, PINX1 = 238, HTOP = 40 + BANNER + 6 };
static void pin_pos (int hp, int *x, int *y) { *x = hp & 1 ? PINX0 : PINX1; *y = HTOP + 16 + ((hp - 1) / 2) * ROWH; }

// a GPIO's mode tag beside its label
static void side_tag (int g, int x, int y, bool leftward, bool reserved)
{
	unsigned bg = C_BG;
	const char *f = reserved ? (g == 14 || g == 15 ? TR ("console") : g <= 1 ? "EEPROM" : TR ("touch")) : g_pin[g].mode != GP_M_FREE ? TR ((const char *) gp_mode_name (g_pin[g].mode)) : (const char *) gp_gpio_function (g);
	if (!f || !*f) return;
	int w = text_w (f) + 10, tx = leftward ? x - w : x;
	unsigned face = reserved ? mix (bg, C_TEXT, 30) : g_pin[g].mode != GP_M_FREE ? mix (bg, mode_col (g_pin[g].mode, 1), 90) : mix (bg, C_TEXT, 22);
	rbox (tx, y - 9, w, 18, 9, face);
	text_c (tx, y - 9, w, 18, f, reserved ? C_DIS : C_TEXT);
}

static void draw_header ()
{
	unsigned bg = C_BG, dim = C_DIM;
	int top = BANNER + 6;
	text (16, top + 4, 18, TR ("40-PIN HEADER"), dim, true);
	text (16, top + 20, 18, sim () ? TR ("simulated: click a dot to switch it") : TR ("click an output's dot to switch it"), sim () ? jet (TJ_ACCENT_FONCE) : dim);
	int bx = PINX0 - 24, by = HTOP, bw = PINX1 - PINX0 + 48, bh = 20 * ROWH + 8;
	rbox (bx, by, bw, bh, 8, C_PCB_EDGE);
	rbox (bx + 1, by + 1, bw - 2, bh - 2, 7, C_PCB);
	ui_fill (PINX0 - 13, by + 4, PINX1 - PINX0 + 26, bh - 8, 0x14402A);	// (the plastic)
	for (int hp = 1; hp <= 40; hp++)
	{
		int x, y; pin_pos (hp, &x, &y);
		int g = (int) gp_header_gpio (hp);
		const char *lab = (const char *) gp_header_label (hp);
		char label[12]; bas_cpy (label, lab, sizeof label);
		unsigned c; bool reserved = false, chosen = g >= 0 && g == g_sel;
		if (g < 0) c = !strcmp (label, "GND") ? C_GND : !strcmp (label, "3V3") ? C_3V3 : C_5V;
		else { reserved = (g_pin[g].flags & GP_F_RESERVED) != 0; c = reserved ? 0x6A6F6C : mode_col (g_pin[g].mode, g_pin[g].level); }
		if (chosen) rbox (x - 13, y - 11, 26, 22, 6, C_ACCENT);
		if (hp == 1) rbox (x - 8, y - 8, 16, 16, 2, 0xD8C690);		// (a square pad for pin 1, as printed)
		else disc (x, y, 8, 0xD8C690);
		disc (x, y, 5, c);
		if (reserved) ui_vp_line (V (x - 4), V (y + 4), V (x + 4), V (y - 4), V (2), 0xE8E8E8);
		char num[4]; snprintf (num, sizeof num, "%d", hp);
		unsigned ink = reserved ? C_DIS : C_TEXT;
		int nw = text_w (num);
		if (hp & 1)
		{
			text (bx - 6 - nw, y - 9, 18, num, dim);
			int tx = bx - 34, lw = text_w (label, g >= 0);
			text (tx - lw, y - 9, 18, label, g < 0 ? dim : ink, g >= 0);
			if (g >= 0) side_tag (g, tx - lw - 8, y, true, reserved);
		}
		else
		{
			text (bx + bw + 6, y - 9, 18, num, dim);
			int tx = bx + bw + 34;
			text (tx, y - 9, 18, label, g < 0 ? dim : ink, g >= 0);
			if (g >= 0) side_tag (g, tx + text_w (label, true) + 8, y, false, reserved);
		}
	}
	(void) bg;
	// the key
	int ky = HTOP + 20 * ROWH + 18, kx = 16;
	static const struct { const char *n; unsigned c; } KEY[] = {
		{ "input", 0x5AA9FF }, { "output", 0xFFD94A }, { "PWM", 0xB57BFF }, { "I2C", 0x3CC8B4 }, { "SPI", 0xFF7BB0 }, { "free", 0xC9D6CD }, { "system", 0x6A6F6C } };
	for (unsigned i = 0; i < sizeof KEY / sizeof KEY[0]; i++)
	{
		const char *kn = TR (KEY[i].n);
		int w = text_w (kn) + 22;
		if (kx + w > RX - 8) { kx = 16; ky += 20; }
		disc (kx + 6, ky + 9, 5, KEY[i].c);
		text (kx + 15, ky, 18, kn, dim);
		kx += w;
	}
}

// the GPIO at a point of the header, -1: none
static int header_hit (int mx, int my)
{
	for (int hp = 1; hp <= 40; hp++)
	{
		int x, y; pin_pos (hp, &x, &y);
		if (my < y - ROWH / 2 || my >= y + ROWH / 2) continue;
		bool odd = hp & 1;
		if (odd ? mx < (PINX0 + PINX1) / 2 : mx >= (PINX0 + PINX1) / 2) return (int) gp_header_gpio (hp);
	}
	return -1;
}
static void header_click (int mx, int my)
{
	int g = header_hit (mx, my);
	if (g < 0 || mx >= RX) return;
	// a click on a pin's own dot: an output turned over; on the simulator, an input driven High, then Low
	int hp = (int) gp_header_pin (g), px, py; pin_pos (hp, &px, &py);
	bool onDot = (mx - px) * (mx - px) + (my - py) * (my - py) <= 100;
	if (onDot && g_pin[g].mode == GP_M_OUT)
	{
		g_blink[g] = false;
		long r = gp_toggle (g);
		if (r < 0) fail ((int) r, TR ("Output"));
		else { char t[160]; snprintf (t, sizeof t, r ? TR ("GPIO %d set high by a click.") : TR ("GPIO %d set low by a click."), g); set_status (t); }
		refresh_pins ();
	}
	else if (onDot && sim () && is_input (g_pin[g].mode)) { gp_sim_input (g, g_pin[g].level ? 0 : 1); refresh_pins (); }
	else if (onDot && g_pin[g].mode == GP_M_FREE && !(g_pin[g].flags & GP_F_RESERVED))
		set_status (TR ("A free pin: make it an Output (the Pins view, or PINMODE in a program), then click its dot to switch it."));
	g_sel = g; sync_controls ();
}

// ---- the chosen pin -----------------------------------------------------------------------------------------
static void draw_panel (int rw)
{
	int x0 = RX, y0 = BANNER + 6;
	unsigned bg = mix (C_BG, C_FIELD, 140);
	rbox (x0, y0, rw, PANEL_H, 8, C_LINE);
	rbox (x0 + 1, y0 + 1, rw - 2, PANEL_H - 2, 7, bg);
	const GpPin &p = g_pin[g_sel];
	char t[240];
	snprintf (t, sizeof t, "GPIO %d", g_sel);
	text (x0 + 14, y0 + 10, 26, t, C_TEXT, true, 16);
	int x = x0 + 14 + text_w (t, true, 16) + 12;
	const char *f = (const char *) gp_gpio_function (g_sel);
	if (*f) snprintf (t, sizeof t, TR ("header pin %d  -  %s"), (int) gp_header_pin (g_sel), f);
	else snprintf (t, sizeof t, TR ("header pin %d"), (int) gp_header_pin (g_sel));
	text (x, y0 + 14, 20, t, C_DIM);
	int lv = p.level;
	disc (x0 + rw - 30, y0 + 23, 9, lv ? 0x5BD46A : 0x50585A);
	const char *ls = lv ? TR ("HIGH") : TR ("LOW");
	text (x0 + rw - 46 - text_w (ls, true), y0 + 13, 20, ls, lv ? C_TEXT : C_DIS, true);
	if (p.flags & GP_F_RESERVED)
	{
		snprintf (t, sizeof t, TR ("Used by the system (%s): GPIO Lab cannot take it."), TR (p.reason));
		text (x0 + 14, y0 + 52, 20, t, C_DIS);
		return;
	}
	if (!mine (g_sel))
	{
		snprintf (t, sizeof t, TR ("Another program (pid %u) has this pin."), p.owner);
		text (x0 + 14, y0 + 52, 20, t, C_DIS);
		return;
	}
	text (x0 + 14, y0 + Y_MODELBL, 18, TR ("MODE"), C_DIM, true);
	if (p.mode == GP_M_I2C || p.mode == GP_M_SPI)
		text (x0 + 14, y0 + Y_ROW + 12, 20, p.mode == GP_M_I2C ? TR ("A pin of the I2C bus (the I2C tab: Close Bus gives it back).") : TR ("A pin of SPI 0."), C_TEXT);
	if (p.mode == GP_M_PWM)
	{
		snprintf (t, sizeof t, TR ("%u Hz, %u.%02u %% high: a pulse of %u us every %u us"), p.freq, p.duty / 100, p.duty % 100,
			  (unsigned) ((unsigned long long) p.duty * 100 / (p.freq ? p.freq : 1)), 1000000u / (p.freq ? p.freq : 1));
		text (x0 + 14, y0 + PANEL_H - 30, 18, t, C_DIM);
	}
}

// ---- the timing chart ---------------------------------------------------------------------------------------
static void draw_chart (int x0, int y0, int width, int height)
{
	unsigned field = C_FIELD, ink = C_TEXT, grid = mix (field, ink, 20), dim = mix (field, ink, 130);
	rbox (x0, y0, width, height, 6, C_LINE);
	rbox (x0 + 1, y0 + 1, width - 2, height - 2, 5, field);
	int lx = 92, cw = width - lx - 10;
	for (int s = 1; s * 60 < HIST; s++)			// (the seconds, as lines)
	{
		int x = lx + cw - s * 60 * cw / HIST;
		if (x < lx) break;
		ui_fill (x0 + x, y0 + 6, 1, height - 26, grid);
		char t[8]; snprintf (t, sizeof t, "-%ds", s);
		text (x0 + x - text_w (t) / 2, y0 + height - 20, 16, t, dim);
	}
	int n = 0; for (int i = 0; i < NWATCH; i++) if (g_watch[i] >= 0) n++;
	if (n == 0) { text_c (x0, y0, width, height, TR ("No pin chosen: tick \"Show in the timing chart\" on a pin."), dim); return; }
	int rowh = (height - 30) / n; if (rowh > 50) rowh = 50;
	int y = y0 + 8;
	for (int i = 0; i < NWATCH; i++)
	{
		int g = g_watch[i];
		if (g < 0) continue;
		const GpPin &p = g_pin[g];
		char t[24]; snprintf (t, sizeof t, "GPIO%d", g);
		text (x0 + 10, y + rowh / 2 - 15, 18, t, ink, true);
		text (x0 + 10, y + rowh / 2 + 1, 16, p.flags & GP_F_RESERVED ? TR ("system") : TR ((const char *) gp_mode_name (p.mode)), dim);
		unsigned col = mode_col (p.mode == GP_M_FREE ? GP_M_IN : p.mode, 1);
		int hi = y + 6, lo = y + rowh - 8, X = x0 + lx;
		ui_fill (X, y + rowh - 1, cw, 1, grid);
		if (p.mode == GP_M_PWM)				// (too fast for the samples: its band)
		{
			int d = (int) p.duty;
			for (int x = X; x < X + cw; x += 6)
			{
				int w = 6 * d / 10000; if (w < 1 && d > 0) w = 1;
				ui_fill (x, hi, 1, lo - hi, mix (field, col, 200));
				if (w > 0) ui_fill (x, hi, w, 2, col);
				ui_fill (x + w, lo - 1, 6 - w, 2, col);
			}
			char f[48]; snprintf (f, sizeof f, TR ("%u Hz  %u.%u %%"), p.freq, p.duty / 100, (p.duty % 100) / 10);
			int fw = text_w (f, true) + 12;
			rbox (X + cw - fw - 4, y + rowh / 2 - 10, fw, 20, 6, field);
			text_c (X + cw - fw - 4, y + rowh / 2 - 10, fw, 20, f, ink, true);
		}
		else if (g_nh > 1)
		{
			int px = -1, pl = 0;
			for (int k = 0; k < g_nh; k++)
			{
				unsigned s = g_hist[(g_h0 + k) % HIST];
				int lv = (s >> g) & 1, x = X + cw - (g_nh - 1 - k) * cw / HIST;
				if (px >= 0)
				{
					ui_fill (px, (pl ? hi : lo) - 1, x - px + 1, 2, col);
					if (lv != pl) ui_fill (x - 1, hi, 2, lo - hi, col);
					if (pl && x > px) ui_fill (px, hi + 1, x - px, lo - hi - 1, mix (field, col, 40));
				}
				px = x; pl = lv;
			}
		}
		y += rowh;
	}
	if (g_paused) { const char *ps = TR ("PAUSED"); text (x0 + width - 14 - text_w (ps, true), y0 + 4, 18, ps, C_ACCENT, true); }
}

// ---- the I2C bus --------------------------------------------------------------------------------------------
// A BME280's readings, by the datasheet's integer formulas (BST-BME280-DS002 4.2.3, 8.2).
static long wr_rd (int a, unsigned char *r, void *o, int n) { return gp_i2c_write_read (a, (long) r, 1, (long) o, n); }
static bool bme280 (int a, char *out, int cap)
{
	if (gp_i2c_reg_read (a, 0xD0) != 0x60) return false;
	unsigned char c[26], h[7], d[8];
	unsigned char r = 0x88;
	if (wr_rd (a, &r, c, 26) < 0) return false;
	r = 0xE1; if (wr_rd (a, &r, h, 7) < 0) return false;
	gp_i2c_reg_write (a, 0xF2, 1);				// humidity x1
	gp_i2c_reg_write (a, 0xF4, 0x25);			// temperature x1, pressure x1, forced
	usleep (10000);
	r = 0xF7; if (wr_rd (a, &r, d, 8) < 0) return false;
	#define U16(i) ((int) (c[i] | c[(i) + 1] << 8))
	#define S16(i) ((int) (short) (c[i] | c[(i) + 1] << 8))
	int T1 = U16 (0), T2 = S16 (2), T3 = S16 (4);
	int P1 = U16 (6), P2 = S16 (8), P3 = S16 (10), P4 = S16 (12), P5 = S16 (14), P6 = S16 (16), P7 = S16 (18), P8 = S16 (20), P9 = S16 (22);
	int H1 = c[25], H2 = (short) (h[0] | h[1] << 8), H3 = h[2], H4 = (signed char) h[3] * 16 | (h[4] & 15), H5 = (signed char) h[5] * 16 | h[4] >> 4, H6 = (signed char) h[6];
	int adcP = d[0] << 12 | d[1] << 4 | d[2] >> 4, adcT = d[3] << 12 | d[4] << 4 | d[5] >> 4, adcH = d[6] << 8 | d[7];
	int v1 = (((adcT >> 3) - (T1 << 1)) * T2) >> 11;
	int v2 = (((((adcT >> 4) - T1) * ((adcT >> 4) - T1)) >> 12) * T3) >> 14;
	int tf = v1 + v2, T = (tf * 5 + 128) >> 8;		// 1/100 degree
	long long p1 = (long long) tf - 128000, p2 = p1 * p1 * P6;
	p2 += (p1 * P5) << 17; p2 += (long long) P4 << 35;
	p1 = ((p1 * p1 * P3) >> 8) + ((p1 * P2) << 12);
	p1 = (((1LL << 47) + p1) * P1) >> 33;
	long long P = 0;
	if (p1 != 0)
	{
		P = 1048576 - adcP; P = (((P << 31) - p2) * 3125) / p1;
		p1 = ((long long) P9 * (P >> 13) * (P >> 13)) >> 25; p2 = ((long long) P8 * P) >> 19;
		P = ((P + p1 + p2) >> 8) + ((long long) P7 << 4);	// 1/256 Pa
	}
	int x = tf - 76800;
	x = (((((adcH << 14) - (H4 << 20) - (H5 * x)) + 16384) >> 15) * (((((((x * H6) >> 10) * (((x * H3) >> 11) + 32768)) >> 10) + 2097152) * H2 + 8192) >> 14));
	x = x - (((((x >> 15) * (x >> 15)) >> 7) * H1) >> 4);
	if (x < 0) x = 0;
	if (x > 419430400) x = 419430400;
	int Hm = (x >> 12) * 10 / 1024;				// 1/10 %
	long long hPa10 = P / 256 / 10;
	snprintf (out, cap, "%d.%d C   %lld.%lld hPa   %d.%d %%", T / 100, (T < 0 ? -T : T) % 100 / 10, hPa10 / 10, hPa10 % 10, Hm / 10, Hm % 10);
	return true;
	#undef U16
	#undef S16
}

// A few capitals in 5 x 7 for the SSD1306 test (the columns, bit 0 at the top).
static const struct { char c; unsigned char col[5]; } GLYPHS[] = {
	{ 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } }, { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
	{ 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } }, { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
	{ 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } }, { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
	{ 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } }, { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
	{ 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } }, { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
	{ 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } }, { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } },
	{ ' ', { 0, 0, 0, 0, 0 } },
};
static void oled_text (unsigned char *page, const char *s, int x0)
{
	for (; *s; s++, x0 += 12)
		for (unsigned i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++)
			if (GLYPHS[i].c == *s)
				for (int k = 0; k < 5 && x0 + 2 * k + 1 < 128; k++)
				{
					// twice as large: each column twice, each row twice (two pages)
					unsigned v = 0; for (int b = 0; b < 7; b++) if (GLYPHS[i].col[k] >> b & 1) v |= 3u << (2 * b);
					for (int dx = 0; dx < 2; dx++) { page[x0 + 2 * k + dx] = (unsigned char) v; page[128 + x0 + 2 * k + dx] = (unsigned char) (v >> 8); }
				}
}
static void oled_test (int a)
{
	static const unsigned char INIT[] = { 0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
					       0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
					       0x21, 0, 127, 0x22, 0, 7 };
	if (gp_i2c_write (a, (long) INIT, sizeof INIT) < 0) { snprintf (g_i2cMsg, sizeof g_i2cMsg, TR ("The display at 0x%02X did not answer."), a); return; }
	static unsigned char buf[1 + 1024];
	buf[0] = 0x40;
	unsigned char *fb = buf + 1;
	memset (fb, 0, 1024);
	for (int x = 0; x < 128; x++) { fb[x] |= 0x01; fb[7 * 128 + x] |= 0x80; }	// a frame
	for (int p = 0; p < 8; p++) { fb[p * 128] = 0xFF; fb[p * 128 + 127] = 0xFF; }
	oled_text (fb + 1 * 128, "KORIN", 34);
	oled_text (fb + 4 * 128, "GPIO LAB", 16);
	for (int x = 8; x < 120; x += 4) fb[6 * 128 + x] |= 0x18;
	// (the real bus takes a message of GPIO_MSG_BYTES at most: the picture in pieces)
	if (sim ()) gp_i2c_write (a, (long) buf, sizeof buf);
	else
		for (int o = 0; o < 1024; o += 256)
		{
			unsigned char piece[257]; piece[0] = 0x40; memcpy (piece + 1, fb + o, 256);
			gp_i2c_write (a, (long) piece, sizeof piece);
		}
	snprintf (g_i2cMsg, sizeof g_i2cMsg, TR ("A test picture was sent to the display at 0x%02X."), a);
}

static void draw_i2c (int x0, int y0)
{
	unsigned bg = C_BG, dim = C_DIM;
	int cs = 17, gx = x0 + 26, gy = y0 + 22;			// the 128 addresses, 8 rows of 16
	for (int c = 0; c < 16; c++) { char t[4]; snprintf (t, sizeof t, "%X", c); text_c (gx + c * cs, y0 + 2, cs, 18, t, dim); }
	for (int r = 0; r < 8; r++)
	{
		char t[4]; snprintf (t, sizeof t, "%X0", r); text (x0, gy + r * cs, cs, t, dim);
		for (int c = 0; c < 16; c++)
		{
			int a = r * 16 + c;
			bool res = a < 0x08 || a > 0x77, on = g_found > 0 && (g_map[a / 8] >> (a % 8) & 1);
			if (!on && !res) rbox (gx + c * cs + 1, gy + r * cs + 1, cs - 2, cs - 2, 3, C_LINE);
			unsigned f = on ? C_ACCENT : res ? mix (bg, C_TEXT, 14) : C_FIELD;
			rbox (gx + c * cs + 2, gy + r * cs + 2, cs - 4, cs - 4, 2, f);
		}
	}
	int ty = gy + 8 * cs + 10, rx = gx + 16 * cs + 18;
	if (g_found < 0) text (x0, ty, 20, TR ("Not scanned yet: Scan opens the bus (GPIO 2 SDA, GPIO 3 SCL)."), dim);
	else
	{
		char t[96]; snprintf (t, sizeof t, g_found == 1 ? TR ("1 device answered.") : TR ("%d devices answered."), g_found);
		text (x0, ty, 20, t, C_TEXT, true);
	}
	if (g_i2cMsg[0]) text (x0, ty + 22, 20, g_i2cMsg, dim);
	int y = y0 + 2;
	text (rx, y, 18, TR ("FOUND"), dim, true); y += 22;
	for (int a = 0; a < 128 && g_found > 0; a++)
	{
		if (!(g_map[a / 8] >> (a % 8) & 1)) continue;
		char t[128]; const char *gs = (const char *) gp_i2c_guess (a);
		snprintf (t, sizeof t, "0x%02X  %s", a, *gs ? TR (gs) : TR ("unknown device"));
		text (rx, y, 20, t, C_TEXT); y += 22;
		if ((a == 0x76 || a == 0x77) && g_bme[0]) { text (rx + 46, y, 20, g_bme, C_ACCENT, true); y += 24; }
	}
	static unsigned char oled[1024];				// the simulated display's picture
	if (sim () && g_found > 0 && (g_map[0x3C / 8] >> (0x3C % 8) & 1) && gp_sim_display ((long) oled))
	{
		int ox = rx, oy = y + 6, s = 2;
		text (ox, oy, 18, TR ("THE DISPLAY AT 0x3C (SIMULATED)"), dim, true); oy += 22;
		rbox (ox - 6, oy - 6, 128 * s + 12, 64 * s + 12, 6, 0x0A0A0C);
		for (int py = 0; py < 64; py++)
			for (int px = 0; px < 128; px++)
				if (oled[(py / 8) * 128 + px] >> (py % 8) & 1) ui_fill (ox + px * s, oy + py * s, s, s, 0x8FE3FF);
	}
}

// ---- the edges ------------------------------------------------------------------------------------------------
static void draw_edges (int x0, int y0, int width, int height)
{
	unsigned field = C_FIELD, ink = C_TEXT, dim = mix (field, ink, 130);
	rbox (x0, y0, width, height, 6, C_LINE);
	rbox (x0 + 1, y0 + 1, width - 2, height - 2, 5, field);
	text (x0 + 12, y0 + 6, 18, TR ("TIME (S)"), dim, true);
	text (x0 + 110, y0 + 6, 18, TR ("PIN"), dim, true);
	text (x0 + 190, y0 + 6, 18, TR ("EDGE"), dim, true);
	int rows = (height - 34) / 20;
	if (g_nlog == 0) text_c (x0, y0, width, height, TR ("No edge yet: tick \"Log its edges\" on an input."), dim);
	for (int i = 0; i < rows && i < g_nlog; i++)
	{
		const GpEvent &e = g_log[(g_log0 + g_nlog - 1 - i) % NLOG];	// (the newest first)
		unsigned long long us = e.us > g_t0 ? e.us - g_t0 : 0;
		char t[48]; int y = y0 + 28 + i * 20;
		snprintf (t, sizeof t, "%llu.%06llu", us / 1000000, us % 1000000); text (x0 + 12, y, 20, t, ink);
		snprintf (t, sizeof t, "GPIO%d", e.pin); text (x0 + 110, y, 20, t, ink, true);
		const char *es = e.edge == GP_RISING ? TR ("rising  (to high)") : TR ("falling (to low)");
		text (x0 + 190, y, 20, es, e.edge == GP_RISING ? jet (TJ_SUCCES) : jet (TJ_ALERTE));
		if (e.lost) { int lx = x0 + 190 + text_w (es) + 14; text (lx, y, 20, TR ("(edges lost before)"), dim); }
	}
	int x = x0 + width - 170, y = y0 + 28;				// the counts
	text (x, y0 + 6, 18, TR ("COUNTED"), dim, true);
	for (int p = 0; p < GP_PINS; p++)
	{
		if (!g_edgeCount[p]) continue;
		char t[48]; snprintf (t, sizeof t, "GPIO%d  %u", p, g_edgeCount[p]);
		text (x, y, 20, t, ink); y += 20;
		if (y > y0 + height - 20) break;
	}
}

// ---- the program's console ------------------------------------------------------------------------------------
#define CON_LINES	300
enum { CON_H = 150 };
struct ConLine { char t[160]; unsigned char kind; };		// 0 the program's, 1 an error, 2 GPIO Lab's
static ConLine g_con[CON_LINES]; static int g_conN = 0, g_conFirst = 0; static bool g_conOpen = false;	// open: the last line goes on
static ConLine &con_at (int i) { return g_con[(g_conFirst + i) % CON_LINES]; }
static void con_new (int kind)
{
	if (g_conN == CON_LINES) { g_conFirst = (g_conFirst + 1) % CON_LINES; g_conN--; }
	ConLine &L = con_at (g_conN++); L.t[0] = 0; L.kind = (unsigned char) kind; g_conOpen = true;
}
static void con_put (const char *s, int len, int kind)
{
	for (int i = 0; i < len; i++)
	{
		if (!g_conOpen || con_at (g_conN - 1).kind != kind) con_new (kind);
		if (s[i] == '\n') { g_conOpen = false; continue; }
		if (s[i] == '\r') continue;
		ConLine &L = con_at (g_conN - 1); int k = (int) strlen (L.t);
		if (k < (int) sizeof L.t - 1) { L.t[k] = s[i]; L.t[k + 1] = 0; }
	}
	ui_view_dirty (g_v);
}
static void con_say (const char *s, int kind) { g_conOpen = false; con_put (s, (int) strlen (s), kind); g_conOpen = false; }
static void con_clear () { g_conN = 0; g_conFirst = 0; g_conOpen = false; ui_view_dirty (g_v); }
static void draw_console (int x0, int y0, int width, int height)
{
	unsigned bg = jet (TJ_CREUX), ink = C_TEXT;
	rbox (x0, y0, width, height, 6, C_LINE);
	rbox (x0 + 1, y0 + 1, width - 2, height - 2, 5, bg);
	int lh = 18, rows = (height - 10) / lh, from = g_conN > rows ? g_conN - rows : 0;
	for (int i = from, y = y0 + 5; i < g_conN; i++, y += lh)
	{
		ConLine &L = con_at (i);
		ui_text ((long) L.t, x0 + 10, y + 14, L.kind == 1 ? jet (TJ_DANGER) : L.kind == 2 ? C_ACCENT : ink, 13, (long) ui.font_m, 0);
	}
	if (g_conN == 0) text (x0 + 10, y0 + 5, lh, TR ("(what the program PRINTs comes here)"), C_DIS);
}

// ---- the whole window, in the view ------------------------------------------------------------------------------
static long paint (long, long w, long h)
{
	W = (int) w; H = (int) h;
	ui_fill (0, 0, w, h, C_BG);
	int vsw = 304;							// the warning: 3.3 V; the Pins / Code switch at its right
	unsigned amber = jet (TJ_ALERTE), ink = ui_ink (amber) & 0xFFFFFF;
	rbox (12, 8, W - 24 - vsw - 8, BANNER - 14, 8, amber);
	ui_vp_disc (V (32), V (25), V (10), ink);
	text_c (26, 15, 12, 20, "!", amber, true);
	const char *w1 = TR ("3.3 V only: never connect 5 V to a GPIO pin."), *w2 = TR ("16 mA a pin at most: an LED through 330 ohm.");
	int x2 = 52 + text_w (w1, true) + 10;
	text (52, 11, 28, w1, ink, true);
	if (x2 + text_w (w2) <= W - 32 - vsw) text (x2, 11, 28, w2, ink);
	draw_header ();
	int rw = W - RX - 12, ty = BANNER + 6 + PANEL_H + Y_TABS, vy = ty + TABS_H, vh = H - vy - FOOT;
	if (g_view == 0)
	{
		draw_panel (rw);
		if (g_tab == 0) draw_chart (RX, vy, rw, vh);
		else if (g_tab == 1) draw_i2c (RX, vy);
		else draw_edges (RX, vy, rw, vh);
	}
	else draw_console (RX, H - FOOT - CON_H, rw, CON_H);
	ui_text_fit ((long) g_status, RX, H - FOOT + 23, C_DIM, PX_UI, (long) ui.font, W - RX - 16);
	return 0;
}

static long v_mouse (long, long kind, long x, long y, long)
{
	if (kind == 1) header_click ((int) x, (int) y);
	return 0;
}

// ---- the actions ------------------------------------------------------------------------------------------------
static void refresh_pins (void)
{
	if (gp_info ((long) g_pin, GP_PINS) < 0) memset (g_pin, 0, sizeof g_pin);
	ui_view_dirty (g_v);
}

static void apply_pwm (int g)
{
	long r = gp_pwm (g, FREQ_HZ[g_freqIx[g]], g_duty[g] * 10);
	if (r < 0) fail ((int) r, "PWM");
	else { char t[160]; snprintf (t, sizeof t, TR ("GPIO %d: PWM at %d Hz, %d.%d %% high."), g, FREQ_HZ[g_freqIx[g]], g_duty[g] / 10, g_duty[g] % 10); set_status (t); }
}
static void show_duty (void) { char t[64]; snprintf (t, sizeof t, TR ("Duty  %d.%d %%"), g_duty[g_sel] / 10, g_duty[g_sel] % 10); ui_w_set_text (g_lbDuty, (long) t); }
static void on_mode ()
{
	static const int MODES[] = { GP_M_FREE, GP_M_IN, GP_M_PULLUP, GP_M_PULLDOWN, GP_M_OUT, GP_M_PWM };
	int m = MODES[ui_w_get_value (g_modes) < 0 ? 0 : ui_w_get_value (g_modes)];
	g_blink[g_sel] = false;
	if (m == GP_M_PWM)
	{
		if (!(g_pin[g_sel].flags & GP_F_PWM)) { set_status (TR ("PWM is on GPIO 12, 13, 18 and 19 only.")); refresh_pins (); sync_controls (); return; }
		apply_pwm (g_sel);
	}
	else
	{
		long r = gp_mode (g_sel, m);
		if (r < 0) fail ((int) r, TR ("Mode"));
		else
		{
			char t[96];
			if (m == GP_M_FREE) snprintf (t, sizeof t, TR ("GPIO %d: given back."), g_sel);
			else snprintf (t, sizeof t, TR ("GPIO %d: %s."), g_sel, TR ((const char *) gp_mode_name (m)));
			set_status (t);
		}
	}
	refresh_pins (); sync_controls ();
}
static void toggle_level (void)
{
	if (g_pin[g_sel].mode != GP_M_OUT) return;
	g_blink[g_sel] = false;
	long r = gp_toggle (g_sel);
	if (r < 0) fail ((int) r, TR ("Output"));
	refresh_pins (); sync_controls ();
}
static void on_edges ()
{
	bool on = ui_w_get_value (g_cbEdges) != 0;
	long r = gp_edges (g_sel, on ? GP_BOTH : 0);
	if (r < 0) { fail ((int) r, TR ("Edges")); ui_w_set_value (g_cbEdges, 0); }
	refresh_pins ();
}
static void on_watch ()
{
	bool on = ui_w_get_value (g_cbWatch) != 0;
	if (on && !watched (g_sel))
	{
		int i = 0; while (i < NWATCH && g_watch[i] >= 0) i++;
		if (i == NWATCH) { for (int k = 0; k < NWATCH - 1; k++) g_watch[k] = g_watch[k + 1]; i = NWATCH - 1; }
		g_watch[i] = g_sel;
	}
	if (!on) for (int i = 0; i < NWATCH; i++) if (g_watch[i] == g_sel) g_watch[i] = -1;
	ui_view_dirty (g_v);
}
static void do_scan (void)
{
	long r = gp_i2c_open (0);
	if (r < 0) { fail ((int) r, "I2C"); g_found = -1; }
	else
	{
		g_found = (int) gp_i2c_scan ((long) g_map);
		if (g_found < 0) { fail (g_found, TR ("I2C scan")); g_found = -1; }
		g_bme[0] = 0;
		for (int a = 0x76; a <= 0x77 && g_found > 0; a++) if (g_map[a / 8] >> (a % 8) & 1) bme280 (a, g_bme, sizeof g_bme);
		if (g_found >= 0) set_status (TR ("The I2C bus scanned."));
	}
	refresh_pins (); sync_controls ();
}
static void on_oled ()
{
	int a = g_found > 0 && (g_map[0x3C / 8] >> 4 & 1) ? 0x3C : g_found > 0 && (g_map[0x3D / 8] >> 5 & 1) ? 0x3D : -1;
	if (a < 0) snprintf (g_i2cMsg, sizeof g_i2cMsg, "%s", TR ("No SSD1306 display found (0x3C or 0x3D)."));
	else oled_test (a);
	ui_view_dirty (g_v);
}

static void sh (long w, bool on) { ui_w_show (w, on); }
// The controls follow the chosen pin and the view.
static void sync_controls (void)
{
	const GpPin &p = g_pin[g_sel];
	bool code = g_view == 1;
	bool usable = !code && !(p.flags & GP_F_RESERVED) && mine (g_sel);
	bool bus = p.mode == GP_M_I2C || p.mode == GP_M_SPI;
	sh (g_modes, usable && !bus);
	if (usable && !bus)
	{
		int s = p.mode == GP_M_FREE ? 0 : p.mode == GP_M_IN ? 1 : p.mode == GP_M_PULLUP ? 2 : p.mode == GP_M_PULLDOWN ? 3 : p.mode == GP_M_OUT ? 4 : p.mode == GP_M_PWM ? 5 : -1;
		ui_w_set_value (g_modes, s);
	}
	bool out = usable && p.mode == GP_M_OUT, pwm = usable && (p.flags & GP_F_PWM) && p.mode == GP_M_PWM, in = usable && is_input (p.mode);
	sh (g_btLevel, out); sh (g_cbBlink, out);
	ui_w_set_text (g_btLevel, (long) (p.level ? TR ("Set Low") : TR ("Set High")));
	ui_w_set_value (g_cbBlink, g_blink[g_sel]);
	sh (g_ddFreq, pwm); sh (g_slDuty, pwm); sh (g_lbDuty, pwm); sh (g_lbFreq, pwm);
	if (pwm) { ui_w_set_value (g_ddFreq, g_freqIx[g_sel]); ui_w_set_value (g_slDuty, g_duty[g_sel]); show_duty (); }
	sh (g_cbEdges, in); ui_w_set_value (g_cbEdges, (p.flags & GP_F_EDGES) != 0);
	bool s = sim ();
	sh (g_drive, in && s); sh (g_lbDrive, in && s);
	ui_w_set_value (g_cbWatch, watched (g_sel)); sh (g_cbWatch, !code);
	sh (g_tabs, !code);
	sh (g_btPause, !code && g_tab == 0); sh (g_btScan, !code && g_tab == 1); sh (g_btOled, !code && g_tab == 1);
	const long ide[] = { g_ed, g_btRun, g_btStep, g_btStop, g_speed, g_lbSpeed, g_ddEx };
	for (long w : ide) sh (w, code);
	ui_view_dirty (g_v);
	ui_w_draw (0);
}
static void apply_view (void) { sync_controls (); }

static void sync_sim_switch (void)
{
	ui_w_set_value (g_simSw, sim ());
	ui_w_enable (g_simSw, g_hasHw);			// (no GPIO here: the simulator only)
}
static void use_sim_as (bool on)
{
	if (g_running) { set_status (TR ("Stop the program first: then switch the simulator.")); sync_sim_switch (); return; }
	if (on == sim ()) { sync_sim_switch (); return; }
	gp_release ();
	gp_sim (on ? 1 : 0);
	set_status (sim () ? TR ("The simulator drives the pins: nothing touches the real header.") : TR ("The Raspberry Pi's header."));
	memset (g_blink, 0, sizeof g_blink);
	g_found = -1;
	sync_sim_switch ();
	refresh_pins (); sync_controls ();
}
static void release_all (void)
{
	gp_release ();
	memset (g_blink, 0, sizeof g_blink);
	g_found = -1;
	set_status (TR ("Every pin given back: they are inputs again."));
	refresh_pins (); sync_controls ();
}

// The demonstration: an LED on GPIO 17, a servo on GPIO 18, a button on GPIO 27 (pulled up: pressed = 0), a switch on
// GPIO 22, the bus scanned, the display's test.
static void demo (void)
{
	gp_mode (17, GP_M_OUT); g_blink[17] = true;
	g_freqIx[18] = 0; g_duty[18] = 75; gp_servo (18, 1500);
	gp_mode (27, GP_M_PULLUP); gp_edges (27, GP_BOTH);
	gp_mode (22, GP_M_PULLDOWN);
	do_scan ();
	if (g_found > 0) oled_test (0x3C);
	g_sel = 18;
	set_status (TR ("The demonstration: an LED on GPIO 17, a servo on GPIO 18, a button on GPIO 27."));
}

// ---- the program, run here ------------------------------------------------------------------------------------
static bool ui_round (long ms);
static bool pump (void) { if (!ui_round (0)) { g_quit = true; return false; } return true; }
static const int SPEED_MS[11] = { 0, 900, 600, 400, 250, 150, 90, 50, 25, 8, 0 };	// a line's wait, by the Speed

class LabHost : public bas::Host
{
public:
	unsigned lastPump = 0;
	void out (const char *s, int n) override
	{
		static char t[512];				// (BASIC's text is code page 437; the console's UTF-8)
		for (int i = 0; i < n; )
		{
			int m = 0;
			while (i < n && m < 508)
			{
				unsigned c = bas437ToLatin1[(unsigned char) s[i++]];
				if (c < 0x80) t[m++] = (char) c;
				else { t[m++] = (char) (0xC0 | c >> 6); t[m++] = (char) (0x80 | (c & 63)); }
			}
			con_put (t, m, 0);
		}
	}
	int inputLine (char *buf, int cap) override
	{
		(void) cap; buf[0] = 0;
		con_say (TR ("INPUT: no keyboard input in GPIO Lab -- the program stops here."), 1);
		return -1;
	}
	int inkey (char *o) override
	{
		if (!g_nkeyQ) return 0;
		long k = g_keyQ[0];
		for (int i = 1; i < g_nkeyQ; i++) g_keyQ[i - 1] = g_keyQ[i];
		g_nkeyQ--;
		o[0] = (char) k;
		return 1;
	}
	int keyPending (char *o) override { if (!g_nkeyQ) return 0; o[0] = (char) g_keyQ[0]; return 1; }
	void cls (int) override { con_clear (); }
	bool poll () override
	{
		unsigned now = bas_us ();
		if (now - lastPump >= 15000u) { lastPump = now; pump (); }
		return !g_stopReq && !g_quit;
	}
	void sleepMs (int ms) override
	{
		unsigned long long end = (unsigned long long) gp_now_us () + (unsigned long long) (ms > 0 ? ms : 0) * 1000u;
		for (;;)
		{
			if (!pump () || g_stopReq) return;
			unsigned long long now = (unsigned long long) gp_now_us ();
			if (now >= end) return;
			unsigned long long left = (end - now) / 1000u;
			usleep ((unsigned) (left > 10 ? 10 : left > 0 ? left : 1) * 1000);
		}
	}
	double timer () override { return (double) ((unsigned long long) gp_now_us () / 1000u) / 1000.0; }
	unsigned clockUs () override { return bas_us (); }
	unsigned seed () override { return (unsigned) gp_now_us (); }
	void notify (const char *title, const char *text) override { char t[200]; snprintf (t, sizeof t, TR ("%s: %s"), title, text); con_say (t, 2); }
	// a line about to run: lit, then the Speed's wait -- or, step by step, the wait for Step
	int lit = -1;
	bool onStatement (int line) override
	{
		int sp = g_speed ? (int) ui_w_get_value (g_speed) : 10;
		if (g_stepMode || sp < 10)
		{
			if (lit != line) { lit = line; ui_textarea_highlight (g_ed, line - 1); }
			if (g_stepMode)
			{
				g_waitStep = true;
				while (g_waitStep && g_stepMode && !g_stopReq && pump ()) usleep (10000);
			}
			else sleepMs (SPEED_MS[sp]);
		}
		else if (poll () && lit != line && bas_us () - lastPump < 2000u)
		{ lit = line; ui_textarea_highlight (g_ed, line - 1); }		// (full speed: the line shown at each round of the window)
		return !g_stopReq && !g_quit;
	}
	// GPIO: GpioKit itself: the program's pins are GPIO Lab's
	bool i2c = false, spi = false;
	int gpio (int op, int a, int b, int c, const char *in, int inLen, char *out, int outCap) override
	{
		int r;
		switch (op)
		{
		case GP_MODE:	return (int) gp_mode (a, b);
		case GP_WRITE:	return (int) gp_write (a, b);
		case GP_READ:	return (int) gp_read (a);
		case GP_PWM:	return (int) gp_pwm (a, b, c);
		case GP_SERVO:	return (int) gp_servo (a, b);
		case GP_EDGES:	return (int) gp_edges (a, b);
		case GP_EVENTS:
		{
			static GpEvent ev[32]; int max = outCap / 2 < 32 ? outCap / 2 : 32;
			r = max > 0 ? (int) gp_events ((long) ev, max, 0) : 0;
			for (int k = 0; k < r; k++) { out[2 * k] = (char) ev[k].pin; out[2 * k + 1] = (char) ev[k].edge; }
			log_events (ev, r);			// (the Edges tab sees them too)
			return r;
		}
		case GP_FREE:	if (a < 0) { i2c = spi = false; return (int) gp_release (); } return (int) gp_mode (a, GP_M_FREE);
		case GP_SIM:	return (int) gp_sim (a);
		case GP_I2C_OPEN: r = (int) gp_i2c_open (a); i2c = r >= 0; return r;
		case GP_SPI_OPEN: r = (int) gp_spi_open (a, b); spi = r >= 0; return r;
		default: break;
		}
		if (op == GP_SPI_XFER) { if (!spi && (r = (int) gp_spi_open (0, 0)) < 0) return r; spi = true; return (int) gp_spi_transfer (a, (long) in, (long) out, inLen); }
		if (!i2c) { if ((r = (int) gp_i2c_open (0)) < 0) return r; i2c = true; }
		switch (op)
		{
		case GP_I2C_REG_READ:	return (int) gp_i2c_reg_read (a, b);
		case GP_I2C_REG_WRITE:	return (int) gp_i2c_reg_write (a, b, c);
		case GP_I2C_XFER:	r = (int) gp_i2c_write_read (a, (long) in, inLen, (long) out, b); return r < 0 ? r : b > 0 ? r : 0;
		case GP_I2C_SCAN:	return outCap >= 16 ? (int) gp_i2c_scan ((long) out) : GP_NODEV;
		default:		return GP_NODEV;
		}
	}
	const char *gpioError (int code) override { return TR ((const char *) gp_error (code)); }
};

static void log_events (const GpEvent *ev, int n)
{
	for (int i = 0; i < n; i++)
	{
		if (g_nlog < NLOG) g_log[(g_log0 + g_nlog++) % NLOG] = ev[i];
		else { g_log[g_log0] = ev[i]; g_log0 = (g_log0 + 1) % NLOG; }
		if (ev[i].pin < GP_PINS) g_edgeCount[ev[i].pin]++;
	}
}

// The sketch: kept between two sessions
#define SKETCH "SD0:/home/gpiolab/sketch.bas"
static const char *const DEFAULT_SKETCH =
	"' GPIO Lab: write BASIC here and press Run (F5) -- the header on the left\n"
	"' shows the pins as the program drives them. Step (F8): a line at a time.\n"
	"' An LED on GPIO 17 (pin 11, through 330 ohm to GND) blinks; a button on\n"
	"' GPIO 27 (pin 13, to GND) is counted. On the simulator, click GPIO 27's\n"
	"' dot on the header to press it.\n"
	"PINMODE 17, \"OUT\"\n"
	"PINMODE 27, \"PULLUP\"\n"
	"ON PIN (27, 2) GOSUB Pressed\n"
	"FOR i = 1 TO 10\n"
	"  PIN 17 = 1: PAUSE 300\n"
	"  PIN 17 = 0: PAUSE 300\n"
	"NEXT\n"
	"PRINT \"Done:\"; presses; \"presses\"\n"
	"END\n"
	"\n"
	"Pressed:\n"
	"  presses = presses + 1\n"
	"  PRINT \"Pressed!\"; presses\n"
	"  RETURN\n";
// The same in French (the comments in UTF-8; the texts it PRINTs in ASCII: BASIC's console is code page 437)
static const char *const DEFAULT_SKETCH_FR =
	"' GPIO Lab : écrivez du BASIC ici et appuyez sur Exécuter (F5) -- le connecteur\n"
	"' à gauche montre les broches comme le programme les pilote. Pas à pas (F8) :\n"
	"' une ligne à la fois. Une LED sur le GPIO 17 (broche 11, par 330 ohms vers GND)\n"
	"' clignote ; un bouton sur le GPIO 27 (broche 13, vers GND) est compté. Sur le\n"
	"' simulateur, cliquez sur le point du GPIO 27 du connecteur pour l'enfoncer.\n"
	"MODEBROCHE 17, \"SORTIE\"\n"
	"MODEBROCHE 27, \"RAPPELHAUT\"\n"
	"SUR BROCHE (27, 2) GOSUB Appui\n"
	"POUR i = 1 JUSQUE 10\n"
	"  BROCHE 17 = 1: PAUSE 300\n"
	"  BROCHE 17 = 0: PAUSE 300\n"
	"SUITE\n"
	"AFFICHER \"Fini :\"; appuis; \"appuis\"\n"
	"FIN\n"
	"\n"
	"Appui:\n"
	"  appuis = appuis + 1\n"
	"  AFFICHER \"Appui !\"; appuis\n"
	"  RETOUR\n";
static const char *first_sketch (void) { return g_fr ? DEFAULT_SKETCH_FR : DEFAULT_SKETCH; }
static void save_sketch (void) { const char *t = (const char *) ui_w_get_text (g_ed); bas_save (SKETCH, t, bas_len (t)); }
static bool load_into_editor (const char *path)
{
	int n; char *b = bas_load (path, &n);
	if (!b) return false;
	ui_w_set_text (g_ed, (long) b);
	delete [] b;
	ui_textarea_highlight (g_ed, -1);
	return true;
}

static void set_run_buttons (void)
{
	ui_w_set_text (g_btRun, (long) (g_running && !g_stepMode ? TR ("Running") : g_running ? TR ("Continue") : TR ("Run")));
	ui_w_enable (g_btStop, g_running);
	ui_w_style (g_ed, UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_CODE | UTA_BASIC | (g_running ? UTF_READONLY : 0));
}

// A compiler's or a run's error in the console, at its line (in French: BASIC's words in French too)
static void say_error (const bas::Error &err)
{
	char m[240], t[320];
	if (g_fr) bas::frenchMessage (err.msg, m, sizeof m);
	else snprintf (m, sizeof m, "%s", err.msg);
	snprintf (t, sizeof t, TR ("Line %d: %s"), err.line, m);
	con_say (t, 1);
}
static long line_offset (int ln)
{
	const char *t = (const char *) ui_w_get_text (g_ed); long p = 0;
	for (; ln > 0 && t[p]; p++) if (t[p] == '\n') ln--;
	return p;
}

// The program in the editor, compiled and run (the main loop calls it: g_runReq)
static void run_program (bool step)
{
	save_sketch ();
	con_clear ();
	ui_textarea_highlight (g_ed, -1);
	bas::Error err; err.line = 0; err.msg[0] = 0;
	bas::Program *p = bas::compile ((const char *) ui_w_get_text (g_ed), &err);
	if (!p)
	{
		say_error (err);
		if (err.line > 0) { ui_textarea_goto (g_ed, line_offset (err.line - 1)); ui_textarea_highlight (g_ed, err.line - 1); }
		return;
	}
	bas::setManaged (p, true);			// (the VM: the statement hook lights the lines)
	LabHost host;
	host.lineHook = true;
	g_running = true; g_stepMode = step; g_waitStep = false; g_stopReq = false; g_nkeyQ = 0;
	set_run_buttons ();
	con_say (step ? TR ("Step by step: F8 (or Step) runs the lit line.") : TR ("Running..."), 2);
	unsigned long long t0 = gp_now_us ();
	int r = bas::run (p, host, &err);
	unsigned long long ms = ((unsigned long long) gp_now_us () - t0) / 1000u;
	char t[200];
	if (r != 0 && err.msg[0])
	{
		say_error (err);
		if (err.line > 0) { ui_textarea_goto (g_ed, line_offset (err.line - 1)); ui_textarea_highlight (g_ed, err.line - 1); }
	}
	else
	{
		snprintf (t, sizeof t, g_stopReq ? TR ("Stopped (%llu.%llu s).") : TR ("Ended (%llu.%llu s): the pins stay as it left them."), ms / 1000, ms % 1000 / 100);
		con_say (t, 2);
		ui_textarea_highlight (g_ed, -1);
	}
	bas::destroy (p);
	g_running = false; g_stepMode = false; g_waitStep = false;
	refresh_pins (); sync_controls (); set_run_buttons ();
	ui_w_focus (g_ed);
}

static void do_run (void)
{
	if (g_running) { g_stepMode = false; g_waitStep = false; set_run_buttons (); return; }	// (Continue)
	g_runReq = 1;
}
static void do_step (void)
{
	if (g_running) { if (g_stepMode) g_waitStep = false; else g_stepMode = true; set_run_buttons (); return; }
	g_runReq = 2;
}
static void do_stop (void) { if (g_running) g_stopReq = true; }

#define NEX	7
static const char *const EXAMPLES[NEX] = { "Examples...", "Blink an LED", "A button", "A servo", "A BME280 sensor (I2C)", "An SSD1306 display (I2C)", "The first sketch" };
// (in SD0:/basic/examples; written in French: in its fr/)
static const char *const EXAMPLE_FILES[NEX] = { 0, "gpio_blink.bas", "gpio_button.bas", "gpio_servo.bas", "gpio_bme280.bas", "gpio_oled.bas", 0 };
static void on_example ()
{
	int i = (int) ui_w_get_value (g_ddEx);
	if (i <= 0 || g_running) return;
	if (i == 6) ui_w_set_text (g_ed, (long) first_sketch ());
	else
	{
		char path[96], t[240];		// in French, the example written in French, else the English one
		snprintf (path, sizeof path, "SD0:/basic/examples/fr/%s", EXAMPLE_FILES[i]);
		bool ok = g_fr && load_into_editor (path);
		if (!ok) { snprintf (path, sizeof path, "SD0:/basic/examples/%s", EXAMPLE_FILES[i]); ok = load_into_editor (path); }
		if (!ok) { snprintf (t, sizeof t, TR ("Cannot read %s"), path); con_say (t, 1); }
		else { snprintf (t, sizeof t, TR ("%s: %s"), TR (EXAMPLES[i]), path); con_say (t, 2); }
	}
	ui_w_set_value (g_ddEx, 0);
}

// ---- the menus, the events ---------------------------------------------------------------------------------------
enum { M_OPEN = 1, M_SAVE, M_QUIT, M_RUN = 10, M_STEP, M_STOP, M_SIM = 20, M_RELEASE, M_DEMO };
static char s_menus[800];
static void build_menus ()
{
	snprintf (s_menus, sizeof s_menus,
		"M%s\n" "I1\t%s\tCtrl+O\n" "I2\t%s\tCtrl+S\n" "-\n" "I3\t%s\tCtrl+Q\n"
		"M%s\n" "I10\t%s\tF5\n" "I11\t%s\tF8\n" "I12\t%s\n"
		"M%s\n" "I20\t%s\tCtrl+M\n" "I21\t%s\tCtrl+R\n" "-\n" "I22\t%s\n",
		TR ("File"), TR ("Open Program..."), TR ("Save Program As..."), TR ("Quit"),
		TR ("Program"), TR ("Run"), TR ("Step"), TR ("Stop"),
		TR ("Board"), TR ("Use the Simulator"), TR ("Release Every Pin"), TR ("Demonstration Set-up"));
	ui_menu ((long) s_menus);
}
static void show_code ()
{
	if (g_view != 1) { g_view = 1; ui_w_set_value (g_viewSw, 1); apply_view (); }
	ui_w_focus (g_ed);
}
static void command (int id)
{
	switch (id)
	{
	case M_OPEN:
	{
		char p[256];
		ui_file_filter ((long) "*.bas");
		long ok = !g_running && ui_file_open ((long) "SD0:/basic/examples", (long) p, sizeof p);
		ui_file_filter (0);
		if (ok) { load_into_editor (p); show_code (); }
		break;
	}
	case M_SAVE:
	{
		char p[256];
		ui_file_filter ((long) "*.bas");
		long ok = ui_file_save ((long) "SD0:/home/gpiolab", (long) "sketch.bas", (long) p, sizeof p);
		ui_file_filter (0);
		if (ok)
		{
			const char *t = (const char *) ui_w_get_text (g_ed); char m[300];
			snprintf (m, sizeof m, bas_save (p, t, bas_len (t)) ? TR ("Saved: %s") : TR ("Cannot read %s"), p);
			con_say (m, 2);
		}
		break;
	}
	case M_QUIT: g_quit = true; break;
	case M_RUN: show_code (); do_run (); break;
	case M_STEP: show_code (); do_step (); break;
	case M_STOP: do_stop (); break;
	case M_SIM: use_sim_as (!sim ()); break;
	case M_RELEASE: release_all (); break;
	case M_DEMO: demo (); refresh_pins (); sync_controls (); break;
	}
}
static void control (long w, long code)
{
	if (w == g_modes) on_mode ();
	else if (w == g_viewSw) { g_view = ui_w_get_value (g_viewSw) == 1; apply_view (); if (g_view) ui_w_focus (g_ed); }
	else if (w == g_tabs) { g_tab = ui_w_get_value (g_tabs) < 0 ? 0 : (int) ui_w_get_value (g_tabs); sync_controls (); }
	else if (w == g_simSw) use_sim_as (ui_w_get_value (g_simSw) != 0);
	else if (w == g_btLevel) toggle_level ();
	else if (w == g_cbBlink) g_blink[g_sel] = ui_w_get_value (g_cbBlink) != 0;
	else if (w == g_cbEdges) on_edges ();
	else if (w == g_cbWatch) on_watch ();
	else if (w == g_ddFreq) { g_freqIx[g_sel] = (int) ui_w_get_value (g_ddFreq); if (g_pin[g_sel].mode == GP_M_PWM) apply_pwm (g_sel); refresh_pins (); sync_controls (); }
	else if (w == g_slDuty) { g_duty[g_sel] = (int) ui_w_get_value (g_slDuty); show_duty (); if (g_pin[g_sel].mode == GP_M_PWM) apply_pwm (g_sel); refresh_pins (); }
	else if (w == g_drive) { long d = ui_w_get_value (g_drive); gp_sim_input (g_sel, d == 0 ? -1 : d == 1 ? 0 : 1); refresh_pins (); }
	else if (w == g_btPause) { g_paused = !g_paused; ui_w_set_text (g_btPause, (long) (g_paused ? TR ("Run") : TR ("Pause"))); ui_view_dirty (g_v); }
	else if (w == g_btScan) do_scan ();
	else if (w == g_btOled) on_oled ();
	else if (w == g_btRun) do_run ();
	else if (w == g_btStep) do_step ();
	else if (w == g_btStop) do_stop ();
	else if (w == g_speed)
	{
		char t[48]; long v = ui_w_get_value (g_speed);
		snprintf (t, sizeof t, v >= 10 ? TR ("Speed: full") : TR ("Speed: %d"), (int) v);
		ui_w_set_text (g_lbSpeed, (long) t);
	}
	else if (w == g_ddEx) on_example ();
	(void) code;
}
static void key_press (int hid, int ch)
{
	enum { HID_SPACE = 0x2C, HID_ESC_ = 0x29, HID_UP_ = 0x52, HID_DOWN_ = 0x51, HID_LEFT_ = 0x50, HID_RIGHT_ = 0x4F };
	if (hid == HID_ESC_ && g_running) { do_stop (); return; }
	if (g_running) { if (ch > 0 && ch < 256 && g_nkeyQ < 16) g_keyQ[g_nkeyQ++] = ch; return; }
	if (g_view != 0) return;
	if (hid == HID_SPACE) { toggle_level (); return; }
	int step = hid == HID_UP_ ? -2 : hid == HID_DOWN_ ? 2 : hid == HID_LEFT_ ? -1 : hid == HID_RIGHT_ ? 1 : 0;
	if (!step) return;
	for (int n = (int) gp_header_pin (g_sel) + step; n >= 1 && n <= 40; n += step)
		if (gp_header_gpio (n) >= 0) { g_sel = (int) gp_header_gpio (n); sync_controls (); return; }
}

// What moves by itself: the blinks, the edges, the chart's samples
static unsigned long long g_nextRefresh;
static void tick ()
{
	unsigned long long now = gp_now_us ();
	if (now >= g_blinkUs + 250000)				// (2 Hz)
	{
		g_blinkUs = now;
		for (int p = 0; p < GP_PINS; p++) if (g_blink[p] && g_pin[p].mode == GP_M_OUT) gp_toggle (p);
	}
	static GpEvent ev[32];
	if (!g_running)
	{
		long n = gp_events ((long) ev, 32, 0);
		if (n > 0) log_events (ev, (int) n);
	}
	static unsigned long long nextSample;
	if (!g_paused && now >= nextSample)
	{
		nextSample = now + 16000;
		unsigned s = (unsigned) gp_read_all ();
		if (g_nh < HIST) g_hist[(g_h0 + g_nh++) % HIST] = s;
		else { g_hist[g_h0] = s; g_h0 = (g_h0 + 1) % HIST; }
	}
	if (now >= g_nextRefresh)				// (the picture: 12 times a second)
	{
		g_nextRefresh = now + 80000;
		refresh_pins ();
		if (g_view == 0) { ui_w_set_text (g_btLevel, (long) (g_pin[g_sel].level ? TR ("Set Low") : TR ("Set High"))); }
	}
}

// A round of the window -> false: it is closed.
static bool ui_round (long ms)
{
	long t = ui_event (ms);
	if (t == WEV_CLOSE) return false;
	if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
	else if (t > 0)
	{
		bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
		int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR);
		long code = 0, taken = 0, w = uikit_w_event (&code, &taken);
		if (code) control (w, code);
		if (press && !taken) { long id = ui_menu_key (0); if (id > 0) command ((int) id); else key_press (hid, ch); }
	}
	else ui_w_idle ();
	tick ();
	ui_view_flush ();
	return !g_quit;
}

// The window resized: the header keeps its width, the right column takes the rest
static void layout (long w, long h)
{
	W = (int) w; H = (int) h;
	int rw = W - RX - 12, py = BANNER + 6;
	ui_w_move (g_v, 0, 0, w, h);
	ui_w_move (g_viewSw, W - 116 - 192, 10, 192, 30);	// (left of the window's buttons)
	ui_w_move (g_simSw, RX - 136, py + 2, 124, 26);
	ui_w_move (g_modes, RX + 14, py + Y_MODES, rw - 28, 28);
	ui_w_move (g_btLevel, RX + 14, py + Y_ROW, 110, 30);
	ui_w_move (g_cbBlink, RX + 136, py + Y_ROW + 4, 150, 24);
	ui_w_move (g_lbFreq, RX + 14, py + Y_ROW + 4, 80, 22);
	ui_w_move (g_ddFreq, RX + 94, py + Y_ROW, 150, 30);
	ui_w_move (g_lbDuty, RX + 252, py + Y_ROW + 4, 170, 22);	// ("Rapport cyclique  100.0 %")
	ui_w_move (g_slDuty, RX + 424, py + Y_ROW, rw - 438, 30);
	ui_w_move (g_lbDrive, RX + 14, py + Y_ROW + 4, 120, 22);
	ui_w_move (g_drive, RX + 134, py + Y_ROW, 220, 28);
	ui_w_move (g_cbEdges, RX + 14, py + Y_CB, 184, 24);
	ui_w_move (g_cbWatch, RX + 200, py + Y_CB, 260, 24);
	int ty = py + PANEL_H + Y_TABS;
	ui_w_move (g_tabs, RX, ty, 360, 28);
	ui_w_move (g_btPause, W - 12 - 90, ty, 90, 28);
	ui_w_move (g_btScan, W - 12 - 90, ty, 90, 28);
	ui_w_move (g_btOled, W - 12 - 90 - 8 - 130, ty, 130, 28);
	int x = RX, cy = py, ddx = W - 12 - 150;
	ui_w_move (g_btRun, x, cy, 96, 30); x += 102;
	ui_w_move (g_btStep, x, cy, 80, 30); x += 86;
	ui_w_move (g_btStop, x, cy, 70, 30); x += 80;
	ui_w_move (g_lbSpeed, x, cy + 4, 86, 22); x += 88;
	int ws = ddx - 12 - x; ws = ws > 120 ? 120 : ws < 40 ? 40 : ws;
	ui_w_move (g_speed, x, cy, ws, 30);
	ui_w_move (g_ddEx, ddx, cy, 150, 30);
	int ey = cy + 40;
	ui_w_move (g_ed, RX, ey, rw, H - FOOT - ey - CON_H - 8);
	ui_view_dirty (g_v);
}
static long on_resize (long w, long h) { layout (w, h); ui_w_draw (0); ui_update_all (); return 0; }

static long kid (long w) { ui_w_set_parent (w, g_v); return w; }
// "a|b|c": each word in the language
static long segs (const char *list)
{
	static char o[200], w[48]; int n = 0;
	for (const char *p = list; ; p++)
	{
		int k = 0; while (*p && *p != '|' && k < 47) w[k++] = *p++;
		w[k] = 0;
		n += snprintf (o + n, sizeof o - n, "%s%s", n ? "|" : "", TR (w));
		if (!*p) break;
	}
	return (long) o;
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

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	if (!gpiokit_kit_open ()) { ui_message ((long) "GPIO Lab", (long) TR ("GpioKit (SD0:/lib/gpiokit.so) is not on this system."), UM_OK); return 1; }
	s_jet = (const unsigned *) ui_jetons ();
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/gpiolab.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	g_fr = !strcmp ((const char *) ui_lang (), "fr");
	bas::setDialect (bas::frenchDialect ());	// BASIC in French too, whatever the language (before wordList)
	bas::wordList (s_words, sizeof s_words);
	bool wantSim = false, wantDemo = false, codeArg = false, runArg = false;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--sim")) wantSim = true;
		else if (!strcmp (argv[i], "--demo")) wantDemo = true;
		else if (!strcmp (argv[i], "--code")) codeArg = true;
		else if (!strcmp (argv[i], "--run")) runArg = true;
		else if (!strcmp (argv[i], "--tab") && i + 1 < argc) { i++; g_tab = !strcmp (argv[i], "i2c") ? 1 : !strcmp (argv[i], "edges") ? 2 : 0; }
	}
	g_hasHw = gp_available () == 1;				// (under QEMU GpioKit takes its simulator)
	if (wantSim) gp_sim (1);
	for (int p = 0; p < GP_PINS; p++) { g_freqIx[p] = 3; g_duty[p] = 500; }
	g_t0 = gp_now_us ();
	mkdir ("SD0:/home", 0755);
	mkdir ("SD0:/home/gpiolab", 0755);

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int avail = (int) (scr[1] - scr[3]) - 32 - (scr[2] > 96 ? (int) scr[2] : 96);	// (the frame; the dock, reserved or not yet)
	int ww = W < (int) scr[0] - 8 ? W : (int) scr[0] - 8, wh = H < avail ? H : avail;
	if (ui_window_ex (0, scr[3], ww, wh, (long) "GPIO Lab", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (940, 580);
	ui_on_resize ((long) on_resize, 0);

	g_v = ui_view (0, 0, ww, wh, 0);
	ui_view_on (g_v, (long) paint, (long) v_mouse, 0, 0);
	g_viewSw = kid (ui_segmented (0, 0, 10, 10, segs ("Pins|Code")));
	g_simSw = kid (ui_switch (0, 0, 10, 10, (long) TR ("Simulator"), sim ()));
	ui_w_tip (g_simSw, (long) TR ("On: a board in memory -- click an input's dot to drive it, nothing touches the real pins. Off: the Pi's header."));
	g_modes = kid (ui_segmented (0, 0, 10, 10, segs ("Free|Input|Pull-up|Pull-down|Output|PWM")));
	g_btLevel = kid (ui_pushbutton (0, 0, 10, 10, (long) TR ("Set High"), 0));
	g_cbBlink = kid (ui_checkbox (0, 0, 10, 10, (long) TR ("Blink (2 Hz)"), 0));
	g_lbFreq = kid (ui_label (0, 0, 10, 10, (long) TR ("Frequency"), 0));
	g_ddFreq = kid (ui_dropdown (0, 0, 10, 10, (long) FREQS));
	g_lbDuty = kid (ui_label (0, 0, 10, 10, (long) "", 0));
	g_slDuty = kid (ui_slider (0, 0, 10, 10, 0, 1000, 500));
	g_lbDrive = kid (ui_label (0, 0, 10, 10, (long) TR ("Driven outside"), 0));
	g_drive = kid (ui_segmented (0, 0, 10, 10, segs ("Nothing|Low|High")));
	g_cbEdges = kid (ui_checkbox (0, 0, 10, 10, (long) TR ("Log its edges"), 0));
	g_cbWatch = kid (ui_checkbox (0, 0, 10, 10, (long) TR ("Show in the timing chart"), 1));
	g_tabs = kid (ui_segmented (0, 0, 10, 10, segs ("Timing Chart|I2C Bus|Edges")));
	g_btPause = kid (ui_pushbutton (0, 0, 10, 10, (long) TR ("Pause"), 0));
	g_btScan = kid (ui_pushbutton (0, 0, 10, 10, (long) TR ("Scan"), 0));
	g_btOled = kid (ui_pushbutton (0, 0, 10, 10, (long) TR ("Test Display"), 0));
	g_btRun = kid (ui_pushbutton (0, 0, 10, 10, (long) TR ("Run"), 0));
	g_btStep = kid (ui_pushbutton (0, 0, 10, 10, (long) TR ("Step"), 0));
	g_btStop = kid (ui_pushbutton (0, 0, 10, 10, (long) TR ("Stop"), 0));
	char sp[48]; snprintf (sp, sizeof sp, TR ("Speed: %d"), 6);
	g_lbSpeed = kid (ui_label (0, 0, 10, 10, (long) sp, 0));
	g_speed = kid (ui_slider (0, 0, 10, 10, 1, 10, 6));
	{
		char names[300]; int n = 0;
		for (int i = 0; i < NEX; i++) n += snprintf (names + n, sizeof names - n, "%s%s", i ? "|" : "", TR (EXAMPLES[i]));
		g_ddEx = kid (ui_dropdown (0, 0, 10, 10, (long) names));
	}
	g_ed = kid (ui_textarea (0, 0, 10, 10, UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_CODE | UTA_BASIC));
	ui_textarea_keywords (g_ed, (long) is_keyword);
	if (!load_into_editor (SKETCH)) ui_w_set_text (g_ed, (long) first_sketch ());
	layout (ui.w, ui.h);
	build_menus ();
	set_run_buttons ();
	refresh_pins ();
	set_status (sim () ? TR ("The simulator drives the pins: nothing touches the real header.") : TR ("Choose a pin on the header."));
	sync_sim_switch ();
	if (wantDemo) demo ();
	refresh_pins ();
	ui_w_set_value (g_tabs, g_tab);
	g_view = codeArg ? 1 : 0; ui_w_set_value (g_viewSw, g_view);
	apply_view ();
	if (g_view == 1) ui_w_focus (g_ed);
	ui_w_draw (0);
	ui_update_all ();
	if (runArg) g_runReq = 1;
	// the loop, with the programs started from it -- never from an event: their run gives the window its rounds (pump)
	while (!g_quit && ui_round (16))
		if (g_runReq) { int r = g_runReq; g_runReq = 0; run_program (r == 2); }
	save_sketch ();
	gp_release ();
	return 0;
}
