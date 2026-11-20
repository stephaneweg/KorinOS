/* be_uikit.c -- win16 : l'affichage sur UIKit (be.h).
 *
 * Une fenêtre Win16 principale = une fenêtre de Korin (ui_window_ex), sans barre : un bandeau de BAND_H pixels
 * en haut porte le titre aux couleurs du thème (UIKit déplace la fenêtre depuis ce bandeau ; FRAME_CAP_W reste
 * libre à droite pour les boutons d'Elegant), la zone cliente du programme est dessous. */
#ifndef W16_HOST
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include "be.h"

#define BAND_H 30
#define TITLE_PX 14
#define TEXT_PX 13
#define MAX_WIN 64

static struct ui_vars ui;
static int ui_ok;
struct bewin { long h; int w, ch; char title[160]; };
static struct bewin wins[MAX_WIN];

static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }

int be_init (void)
{
	ui_ok = uikit_open (&ui);
	return ui_ok;
}

static struct bewin *find (long h)
{
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].h && wins[i].h == h)
			return &wins[i];
	return 0;
}

static void use (long h)
{
	ui_window_use (h);
}

static void draw_band (struct bewin *b)
{
	use (b->h);
	ui_fill (0, 0, ui.w, BAND_H, tok (TJ_SURFACE));
	ui_fill (0, BAND_H - 1, ui.w, 1, tok (TJ_LINE));
	ui_text_fit ((long) b->title, 12, BAND_H - 10, tok (TJ_TEXT1), TITLE_PX, (long) ui.font_b,
		ui.w - FRAME_CAP_W - 24 > 0 ? ui.w - FRAME_CAP_W - 24 : 10);
	ui_update (0, 0, ui.w, BAND_H);
}

long be_window_open (int x, int y, int w, int h, const char *title)
{
	struct bewin *b = 0;
	for (int i = 0; i < MAX_WIN && !b; i++)
		if (!wins[i].h)
			b = &wins[i];
	if (!b)
		return 0;
	if (w < FRAME_CAP_W + 40)
		w = FRAME_CAP_W + 40;
	if (h < 1)
		h = 1;
	long hw = ui_window_ex (x < 0 ? -1 : x, y < 0 ? -1 : y, w, h + BAND_H, (long) title, 0);
	if (hw <= 0)
		return 0;
	b->h = hw;
	use (hw);
	if (getenv ("W16_EVLOG"))
		fprintf (stderr, "win16 : fenêtre de Korin %ld « %s »\n", hw, title);
	/* (le cadre d'Elegant peut prendre quelques pixels : la zone doit avoir la taille demandée) */
	if (ui.w != w || ui.h != h + BAND_H)
		ui_resize (w + (w - ui.w), h + BAND_H + (h + BAND_H - ui.h));
	b->w = (int) ui.w;
	b->ch = (int) ui.h - BAND_H;
	snprintf (b->title, sizeof b->title, "%s", title);
	draw_band (b);
	return hw;
}

void be_window_close (long win)
{
	struct bewin *b = find (win);
	if (!b)
		return;
	ui_window_close (win);
	b->h = 0;
}

void be_window_title (long win, const char *title)
{
	struct bewin *b = find (win);
	if (!b)
		return;
	snprintf (b->title, sizeof b->title, "%s", title);
	use (win);
	ui_title ((long) title);
	draw_band (b);
}

void be_window_resize (long win, int w, int h)
{
	struct bewin *b = find (win);
	if (!b)
		return;
	use (win);
	ui_resize (w, h + BAND_H);
	b->w = (int) ui.w;
	b->ch = (int) ui.h - BAND_H;
	draw_band (b);
}

uint32_t *be_pixels (long win, int *w, int *h, int *stride)
{
	struct bewin *b = find (win);
	if (!b)
		return 0;
	use (win);
	*w = (int) ui.w;
	*h = (int) ui.h - BAND_H;
	*stride = (int) ui.w;
	return ui.surf + ui.w * BAND_H;
}

void be_present (long win, int x, int y, int w, int h)
{
	if (!find (win))
		return;
	use (win);
	ui_update (x, y + BAND_H, w, h);
}

static long font_of (int style)
{
	switch (style)
	{
	case 1: return (long) ui.font_b;
	case 2: return (long) ui.font_m;
	case 3: return ui_font_get (UFN_SERIF);
	case 4: return ui_font_get (UFN_SERIFBOLD);
	case 5: return ui_font_get (UFN_ITALIC);
	default: return (long) ui.font;
	}
}

int be_text (long win, int x, int y, const char *s, uint32_t color, int cx, int cy, int cw, int ch, int px, int style)
{
	if (!find (win))
		return 0;
	use (win);
	if (px <= 0)
		px = TEXT_PX;
	ui_clip (cx, cy + BAND_H, cw, ch);
	ui_text ((long) s, x, y + BAND_H + be_font_ascent (px), color, px, font_of (style), 0);
	ui_clip (0, 0, 0, 0);
	return (int) ui_text_width ((long) s, px, font_of (style));
}

/* du texte dans un tampon à part (un bitmap) : UIKit écrit dans les pixels de ui_vars, échangés le temps du tracé */
int be_text_buf (uint32_t *dst, int dw, int dh, int x, int y, const char *s, uint32_t color, int cx, int cy, int cw,
	int ch, int px, int style)
{
	if (px <= 0)
		px = TEXT_PX;
	uint32_t *ssurf = ui.surf;
	long sw = ui.w, sh = ui.h;
	ui.surf = dst;
	ui.w = dw;
	ui.h = dh;
	ui_clip (cx, cy, cw, ch);
	ui_text ((long) s, x, y + be_font_ascent (px), color, px, font_of (style), 0);
	ui_clip (0, 0, 0, 0);
	ui.surf = ssurf;
	ui.w = sw;
	ui.h = sh;
	return (int) ui_text_width ((long) s, px, font_of (style));
}

int be_text_width (const char *s, int px, int style)
{
	return (int) ui_text_width ((long) s, px > 0 ? px : TEXT_PX, font_of (style));
}

int be_font_height (int px) { return ((px > 0 ? px : TEXT_PX) * 5 + 3) / 4; }
int be_font_ascent (int px) { return ((px > 0 ? px : TEXT_PX) * 15 + 8) / 16; }

void be_menu_set (long win, const char *text)
{
	if (!find (win))
		return;
	use (win);
	ui_menu ((long) (text && *text ? text : 0));
}

void be_menu_state (long win, int id, int state)
{
	if (!find (win))
		return;
	use (win);
	ui_menu_state (id, state);
}

int be_popup (long win, int x, int y, const char *labels, int w)
{
	if (!find (win))
		return -1;
	use (win);
	long r = uikit_call3 ((long (*) ()) ui_popup_local, x, y + BAND_H, (long) labels, w, 0, 0);
	return r >= 0 ? (int) r : -1;
}

uint32_t be_theme (int what)
{
	static const int t[] = { TJ_SURFACE, TJ_TEXT1, TJ_LINE_STRONG, TJ_ACCENT, TJ_RAISED, TJ_SUNKEN, TJ_LINE,
		TJ_ACCENT_INK, TJ_TEXT3, TJ_FACE, TJ_ACCENT_LIGHT, TJ_HOVER };
	return tok (t[what >= 0 && what < 12 ? what : 0]);
}

int be_wait (int ms, struct be_event *ev)
{
	memset (ev, 0, sizeof *ev);
	long t = ui_event (ms);
	if (t <= 0)
		return 0;
	ev->win = ui_ev_field (&ui, EV_WIN);
	/* les widgets d'abord : un événement qu'ils prennent devient BE_WIDGET (ou rien) */
	{
		struct bewin *bw = find (ev->win);
		if (bw)
			use (bw->h);
		long code = 0, taken = 0;
		long wd = uikit_w_event (&code, &taken);
		if (wd && code)
		{
			ev->type = BE_WIDGET;
			ev->key = (int) wd;
			ev->x = (int) code;
			return 1;
		}
		if (taken)
			return 0;
	}
	static int evlog = -1;
	if (evlog < 0)
		evlog = getenv ("W16_EVLOG") != 0;
	if (evlog)	/* (W16_EVLOG : les événements de UIKit, pour les essais) */
		fprintf (stderr, "win16 : événement %ld, fenêtre %ld, sorte %d, %d,%d, code %d\n", t, ev->win,
			ui_ev_field (&ui, EV_KIND), ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), ui_ev_field (&ui, EV_CODE));
	struct bewin *b = find (ev->win);
	int x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y) - BAND_H;
	int mods = ui_ev_field (&ui, EV_MODS);
	ev->mods = (mods & UI_MOD_CTRL ? 1 : 0) | (mods & UI_MOD_SHIFT ? 2 : 0) | (mods & UI_MOD_ALT ? 4 : 0);
	switch (t)
	{
	case WEV_CLOSE:
		ev->type = BE_CLOSE;
		return 1;
	case WEV_MOUSE:
	{
		int kind = ui_ev_field (&ui, EV_KIND), code = ui_ev_field (&ui, EV_CODE);
		ev->x = x;
		ev->y = y;
		ev->button = code & 7;
		if (kind == 1 && y < 0)
		{
			if ((code & 1) && b && ui_ev_field (&ui, EV_X) < (int) ui.w - FRAME_CAP_W)
			{
				use (b->h);
				ui_window_drag ();
			}
			return 0;
		}
		ev->type = kind == 1 ? BE_MOUSE_DOWN : kind == 2 ? BE_MOUSE_UP : BE_MOUSE_MOVE;
		return 1;
	}
	case WEV_KEY:
		ev->type = ui_ev_field (&ui, EV_KIND) ? BE_KEY_DOWN : BE_KEY_UP;
		ev->key = ui_ev_field (&ui, EV_CODE);
		ev->ch = ui_ev_field (&ui, EV_CHAR);
		return 1;
	case WEV_RESIZE:
		if (b)
		{
			use (b->h);
			b->w = (int) ui.w;
			b->ch = (int) ui.h - BAND_H;
			draw_band (b);
		}
		ev->type = BE_RESIZE;
		return 1;
	case WEV_THEME:
		if (b)
			draw_band (b);
		return 0;
	case WEV_FOCUS:
		ev->type = BE_FOCUS;
		ev->x = ui_ev_field (&ui, EV_KIND);
		return 1;
	case WEV_MENU:
		ev->type = BE_MENU;
		ev->key = ui_ev_field (&ui, EV_CODE);
		return 1;
	case WEV_WHEEL:
		ev->type = BE_WHEEL;
		ev->key = ui_ev_field (&ui, EV_Y);
		return 1;
	}
	return 0;
}

uint32_t be_ticks (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (uint32_t) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

int be_message (const char *title, const char *text, int type)
{
	int kind = type & 0xf;
	long um = kind == 4 ? UM_YESNO : kind == 3 ? UM_YESNOCANCEL : kind == 1 ? UM_OKCANCEL : UM_OK;
	long r = ui_message ((long) title, (long) text, um);
	if (kind == 4)
		return r == 1 ? 6 : 7;	/* IDYES, IDNO */
	if (kind == 3)
		return r == 1 ? 6 : r == 2 ? 7 : 2;
	if (kind == 1)
		return r == 1 ? 1 : 2;	/* IDOK, IDCANCEL */
	return 1;
}

int be_file_open (const char *dir, const char *filter, char *out, int cap)
{
	ui_file_filter ((long) filter);
	long r = ui_file_open ((long) dir, (long) out, cap);
	ui_file_filter (0);
	return r == 1;
}

int be_file_save (const char *dir, const char *name, const char *filter, char *out, int cap)
{
	ui_file_filter ((long) filter);
	long r = ui_file_save ((long) dir, (long) name, (long) out, cap);
	ui_file_filter (0);
	return r == 1;
}

long be_color (const char *title, uint32_t rgb)
{
	return ui_color_dialog ((long) title, rgb);
}

void be_clip_put (const char *utf8)
{
	ui_clip_set ((long) utf8, -1);
}

int be_clip_take (char *buf, int cap)
{
	return (int) ui_clip_get ((long) buf, cap);
}

/* ---- les widgets de UIKit (ctrl.c, mode natif) : posés dans la fenêtre de Korin sous le bandeau ---- */
long be_w_create (long win, int kind, int x, int y, int w, int h, const char *text, long extra)
{
	if (!find (win))
		return 0;
	use (win);
	y += BAND_H;
	long t = (long) (text ? text : "");
	if (kind == BW_TEXT || kind == BW_PASSWORD || kind == BW_TEXTAREA)
		extra = ((extra & BWF_READONLY) ? UTF_READONLY : 0) | ((extra & BWF_VSCROLL) ? UTA_VSCROLL : 0)
			| ((extra & BWF_HSCROLL) ? UTA_HSCROLL : 0);
	switch (kind)
	{
	case BW_PUSH: return ui_pushbutton (x, y, w, h, t, 0);
	case BW_DEFPUSH: return ui_pushbutton (x, y, w, h, t, UB_DEFAULT);
	case BW_CHECK: return ui_checkbox (x, y, w, h, t, extra);
	case BW_RADIO: return ui_radio (x, y, w, h, t, extra);
	case BW_TEXT: { long wd = ui_textbox (x, y, w, h, extra); if (wd) ui_w_set_text (wd, t); return wd; }
	case BW_PASSWORD: { long wd = ui_textbox (x, y, w, h, UTF_PASSWORD | extra); if (wd) ui_w_set_text (wd, t); return wd; }
	case BW_TEXTAREA: { long wd = ui_textarea (x, y, w, h, extra); if (wd) ui_w_set_text (wd, t); return wd; }
	case BW_LIST: return ui_list (x, y, w, h, UL_NOSORT);
	case BW_DROPDOWN: return ui_dropdown (x, y, w, h, t);
	case BW_COMBO: return ui_combobox (x, y, w, h, t);
	case BW_VSCROLL: return ui_scrollbar (x, y, w, h, 0);
	case BW_HSCROLL: return ui_scrollbar (x, y, w, h, USB_HORIZONTAL);
	case BW_LABEL: return ui_label (x, y, w, h, t, ULB_WRAP);
	case BW_LABEL_CENTER: return ui_label (x, y, w, h, t, ULB_WRAP | ULB_CENTER);
	case BW_LABEL_RIGHT: return ui_label (x, y, w, h, t, ULB_WRAP | ULB_RIGHT);
	case BW_GROUP: return ui_groupbox (x, y, w, h, t);
	}
	return 0;
}

int be_ui_metric (int what)
{
	return (int) ui_metric (what == BEM_CTL ? UMT_CTL : what == BEM_ROW ? UMT_ROW : UMT_PX);
}

static void wuse (long wd) { long win = ui_w_window (wd); if (win) use (win); }
void be_w_move (long wd, int x, int y, int w, int h) { wuse (wd); ui_w_move (wd, x, y + BAND_H, w, h); }
void be_w_show (long wd, int show) { wuse (wd); ui_w_show (wd, show); }
void be_w_enable (long wd, int on) { wuse (wd); ui_w_enable (wd, on); }
void be_w_focus (long wd) { wuse (wd); ui_w_focus (wd); }
void be_w_set_text (long wd, const char *text) { wuse (wd); ui_w_set_text (wd, (long) text); }
const char *be_w_get_text (long wd) { wuse (wd); return (const char *) ui_w_get_text (wd); }
void be_w_set_value (long wd, long v) { wuse (wd); ui_w_set_value (wd, v); }
long be_w_get_value (long wd) { wuse (wd); return ui_w_get_value (wd); }
void be_w_set_range (long wd, long mn, long mx, long page, long step) { wuse (wd); ui_w_set_range (wd, mn, mx, page, step); }
void be_w_list_add (long wd, const char *text) { wuse (wd); ui_list_add (wd, (long) text, 0); }
void be_w_list_clear (long wd) { wuse (wd); ui_list_clear (wd); }
void be_w_draw (long wd) { wuse (wd); ui_w_draw (wd); }
void be_w_destroy (long wd) { wuse (wd); ui_w_destroy (wd); }

/* ---- le synthé (comme src/apps/arabesque/audio.h) : un fil tient le synthé General MIDI d'AudioKit et nourrit le
 * flux bloc après bloc ; la SoundFont se charge dans le fil, la fenêtre n'attend jamais. Les messages MIDI du
 * programme arrivent sous g_lk, entre deux blocs. ---- */
enum { BLOCK = 256, RATE = 44100 };
static pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_tid;
static volatile int g_started, g_synth_ok;
static long g_synth;

static void *synth_main (void *arg)
{
	(void) arg;
	if (!audiokit_kit_open ())
		return 0;
	/* (les numéros d'AudioKit commencent à 0 : seul un résultat négatif est une erreur) */
	long s = ak_synth_new (0, 0);
	if (s < 0)
		return 0;
	long out = ak_open (RATE, 2, 16, 0);
	pthread_mutex_lock (&g_lk);
	g_synth = s;
	g_synth_ok = 1;
	pthread_mutex_unlock (&g_lk);
	static short buf[BLOCK * 2];
	for (;;)
	{
		pthread_mutex_lock (&g_lk);
		ak_synth_render (s, (long) buf, BLOCK);
		pthread_mutex_unlock (&g_lk);
		if (out >= 0)
			((long (*) (long, long, long)) ak_write_all) (out, (long) buf, sizeof buf);
		else
			usleep (5000);
	}
	return 0;
}

int be_synth_start (void)
{
	if (!g_started)
		g_started = pthread_create (&g_tid, 0, synth_main, 0) == 0 ? 1 : -1;
	return g_started > 0;
}

void be_synth_midi (int ch, int cmd, int a, int b)
{
	pthread_mutex_lock (&g_lk);
	if (g_synth_ok)
		ak_synth_midi (g_synth, ch, cmd, a, b);
	pthread_mutex_unlock (&g_lk);
}

void be_synth_reset (void)
{
	pthread_mutex_lock (&g_lk);
	if (g_synth_ok)
		for (int c = 0; c < 16; c++)
		{
			ak_synth_midi (g_synth, c, 0xB0, 64, 0);
			ak_synth_midi (g_synth, c, 0xB0, 123, 0);
		}
	pthread_mutex_unlock (&g_lk);
}

/* la sortie PCM de la machine DOS (son.c) : un fil remplit des blocs de 44,1 kHz stéréo, AudioKit les joue */
static void (*pcm_fill) (int16_t *buf, int frames);

static void *pcm_main (void *arg)
{
	(void) arg;
	if (!audiokit_kit_open ())
		return 0;
	long out = ak_open (RATE, 2, 16, 0);
	if (out < 0)
		return 0;
	static int16_t buf[BLOCK * 2];
	for (;;)
	{
		pcm_fill (buf, BLOCK);
		((long (*) (long, long, long)) ak_write_all) (out, (long) buf, sizeof buf);
	}
	return 0;
}

int be_pcm_start (void (*fill) (int16_t *buf, int frames))
{
	static int started;
	pthread_t t;
	if (started)
		return started > 0;
	pcm_fill = fill;
	started = pthread_create (&t, 0, pcm_main, 0) == 0 ? 1 : -1;
	return started > 0;
}

void be_fatal (const char *msg)
{
	if (ui_ok)
		ui_message ((long) "Windows 3.x", (long) msg, UM_OK);
}
#endif
