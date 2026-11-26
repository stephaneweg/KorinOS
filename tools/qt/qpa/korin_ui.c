/* korin_ui.c : UIKit pour la QPA korin de Qt (korin_ui.h). Même montage que GHOST de Blender
 * (tools/blender/ghost/ghost_korin_ui.c) : une fenêtre de premier niveau de Qt est une fenêtre d'UIKit (cadre et
 * couleurs du thème) ; Qt dessine en raster dans son QImage, la QPA recopie les rectangles changés dans les pixels
 * de la fenêtre (ui.surf) et les montre (ui_update).
 *
 * UIKit garde une « fenêtre courante » (ui.win, ui.surf, ui.w, ui.h) : chaque fonction qui vise une fenêtre la
 * choisit d'abord par ui_window_use. */
#include "korin_ui.h"

#include <string.h>
#include <sys/stat.h>
#include <uikit.h>

static struct ui_vars ui;

int kq_open(void)
{
	return uikit_open(&ui);
}

void kq_screen(int *w, int *h, int *bottom, int *top)
{
	long scr[4] = {0, 0, 0, 0};
	ui_screen((long)scr);
	if (w) *w = (int)scr[0];
	if (h) *h = (int)scr[1];
	if (bottom) *bottom = (int)scr[2];
	if (top) *top = (int)scr[3];
}

long kq_window(int x, int y, int w, int h, const char *title, int kind)
{
	long role = (kind & KQ_WIN_ALPHA) ? WF_ALPHA : 0;
	switch (kind & 0xff) {
	case KQ_WIN_RESIZABLE: role |= WF_RESIZABLE; break;
	case KQ_WIN_POPUP: role |= WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_NOFOCUS; break;	/* (le clavier reste à la fenêtre de l'app : Qt lui donne le popup) */
	case KQ_WIN_BORDERLESS: role |= WF_FRAMELESS; break;
	case KQ_WIN_DIALOG: role |= WF_RESIZABLE | WF_MODAL; break;
	default: break;
	}
	return ui_window_ex(x, y, w, h, (long)(title ? title : "Qt"), role);
}

void kq_window_close(long win)
{
	if (win > 0) /* (0 fermerait la courante) */
		ui_window_close(win);
}

void kq_window_title(long win, const char *title)
{
	ui_window_use(win);
	ui_title((long)(title ? title : ""));
}

/* clair (1) ou foncé (0) : la luminance d'une couleur 0x00RRGGBB */
static int kq_is_dark(unsigned rgb)
{
	const unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
	return (r * 299 + g * 587 + b * 114) / 1000 < 128;
}

void kq_title_band(long win, const char *title, int active)
{
	if (win <= 0)
		return;
	ui_window_use(win);
	const unsigned *tok = (const unsigned *)ui_tokens(); /* (les couleurs du thème, jamais en dur) */
	const long w = ui.w;
	if (w <= 0 || ui.h < KQ_TOP_BAND)
		return;
	const unsigned face = tok[TJ_FACE / 4] & 0xFFFFFF;
	const int dark = kq_is_dark(face);
	unsigned ink = tok[(active ? TJ_TEXT1 : TJ_TEXT2) / 4] & 0xFFFFFF;
	ui_fill(0, 0, w, KQ_TOP_BAND, face);
	ui_fill(0, KQ_TOP_BAND - 1, w, 1, tok[TJ_LINE / 4] & 0xFFFFFF);
	ui_frame_glyphs(dark ? 1 : 0);
	/* le titre centré, sans empiéter sur les boutons du cadre (à droite, FRAME_CAP_W) */
	const char *t = title ? title : "";
	const long px = 14, room = w - 2 * (FRAME_CAP_W + 8);
	if (*t && room > 40) {
		long tw = ui_text_width((long)t, px, (long)ui.font);
		if (tw > room)
			tw = room;
		const long x = (w - tw) / 2;
		ui_text_fit((long)t, x, (KQ_TOP_BAND + px) / 2 - 1, ink, px, (long)ui.font, room);
	}
	ui_update(0, 0, w, KQ_TOP_BAND);
}

void kq_window_resize(long win, int w, int h)
{
	ui_window_use(win);
	ui_resize(w, h);
}

void kq_window_size(long win, int *w, int *h)
{
	ui_window_use(win);
	*w = (int)ui.w;
	*h = (int)ui.h;
}

int kq_window_pos(long win, int *x, int *y)
{
	long pos[2] = {0, 0}; /* (deux mots de 64 bits : comme Terminal et Jamendo) */
	ui_window_use(win);
	if (ui_window_pos((long)pos) < 0)
		return -1;
	*x = (int)pos[0];
	*y = (int)pos[1];
	return 0;
}

void kq_window_place(long win, int x, int y)
{
	ui_window_use(win);
	ui_window_place(x, y);
}

void kq_window_top(long win, int on)
{
	ui_window_use(win);
	ui_window_top(on ? 1 : 0);
}

void kq_window_maximize(long win)
{
	ui_window_use(win);
	ui_maximize(); /* (comme le bouton agrandir ; Elegant répond par WEV_RESIZE) */
}

void kq_window_minimize(long win)
{
	ui_window_use(win);
	ui_minimize();
}

void kq_window_raise(long win)
{
	ui_raise(win);
}

void kq_window_min_size(long win, int w, int h)
{
	ui_window_use(win);
	ui_min_size(w, h);
}

unsigned *kq_window_pixels(long win, int *w, int *h)
{
	ui_window_use(win);
	*w = (int)ui.w;
	*h = (int)ui.h;
	return ui.surf;
}

void kq_window_update(long win, int x, int y, int w, int h)
{
	ui_window_use(win);
	ui_update(x, y, w, h);
}

int kq_poll(kq_event *ev, int wait_ms)
{
	long t = ui_event(wait_ms);
	if (t <= 0)
		return 0;
	memset(ev, 0, sizeof *ev);
	ev->type = (int)t;
	ev->kind = ui_ev_field(&ui, EV_KIND);
	ev->x = ui_ev_field(&ui, EV_X);
	ev->y = ui_ev_field(&ui, EV_Y);
	ev->code = ui_ev_field(&ui, EV_CODE);
	ev->ch = ui_ev_field(&ui, EV_CHAR);
	ev->mods = ui_ev_field(&ui, EV_MODS);
	ev->win = ui_ev_field(&ui, EV_WIN);
	return (int)t;
}

void kq_cursor(long win, int shape)
{
	ui_window_use(win);
	ui_cursor(shape);
}

void kq_clip_set(const char *utf8)
{
	ui_clip_set((long)utf8, -1);
}

int kq_clip_get(char *buf, int cap)
{
	if (cap <= 0)
		return 0;
	long n = ui_clip_get((long)buf, cap);
	if (n <= 0) {
		buf[0] = 0;
		return 0;
	}
	buf[cap - 1] = 0;
	return (int)n;
}

long kq_menu(long win, const char *text)
{
	ui_window_use(win);
	return ui_menu((long)text);
}

void kq_menu_states(const long *pairs, long n)
{
	if (n > 0)
		ui_menu_states((long)pairs, n);
}

const unsigned *kq_tokens(void)
{
	return (const unsigned *)ui_tokens();
}

int kq_metric(int which)
{
	return (int)ui_metric(which);
}

const char *kq_font_dir(void)
{
	/* (26/11/2026) les polices du système : /res/fonts sur le PC, /fonts avant (et sur le Pi) */
	struct stat st;
	return stat("/res/fonts", &st) == 0 ? "/res/fonts" : "/fonts";
}
