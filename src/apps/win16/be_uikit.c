/* be_uikit.c -- win16 : l'affichage sur UIKit (be.h).
 *
 * Une fenêtre Win16 principale = une fenêtre de Korin (ui_window_ex), sans barre : un bandeau de BAND_H pixels
 * en haut porte le titre aux couleurs du thème (UIKit déplace la fenêtre depuis ce bandeau ; FRAME_CAP_W reste
 * libre à droite pour les boutons d'Elegant), la zone cliente du programme est dessous. */
#ifndef W16_HOST
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <uikit.h>
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
	b->w = w;
	b->ch = h;
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

int be_text (long win, int x, int y, const char *s, uint32_t color, int cx, int cy, int cw, int ch)
{
	if (!find (win))
		return 0;
	use (win);
	ui_clip (cx, cy + BAND_H, cw, ch);
	ui_text ((long) s, x, y + BAND_H + be_font_ascent (), color, TEXT_PX, (long) ui.font, 0);
	ui_clip (0, 0, 0, 0);
	return (int) ui_text_width ((long) s, TEXT_PX, (long) ui.font);
}

int be_text_width (const char *s)
{
	return (int) ui_text_width ((long) s, TEXT_PX, (long) ui.font);
}

int be_font_height (void) { return 16; }
int be_font_ascent (void) { return 12; }

uint32_t be_theme (int what)
{
	static const int t[] = { TJ_SURFACE, TJ_TEXT1, TJ_LINE_STRONG, TJ_ACCENT };
	return tok (t[what & 3]);
}

int be_wait (int ms, struct be_event *ev)
{
	memset (ev, 0, sizeof *ev);
	long t = ui_event (ms);
	if (t <= 0)
		return 0;
	ev->win = ui_ev_field (&ui, EV_WIN);
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

void be_fatal (const char *msg)
{
	if (ui_ok)
		ui_message ((long) "Windows 3.x", (long) msg, UM_OK);
}
#endif
