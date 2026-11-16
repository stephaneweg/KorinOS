/**************************************************************************/
/*  korin_ui.c                                                            */
/**************************************************************************/
/* Korin OS : UIKit et armgl pour la plateforme korin de Godot (korin_ui.h).
 * Meme montage que le pilote video de SDL (tools/sdl/armpi/SDL_armpivideo.c) :
 * une fenetre de Godot est une fenetre d'UIKit, cadre et couleurs du theme ;
 * son contenu est une surface du GPU (armgl_window) que Elegant compose sans
 * recopie.
 */

#include "korin_ui.h"

#include <string.h>
#include <uikit.h>

#include "armgl.h"

/* le cadre d'un jeu : contenu fonce sous les boutons du cadre, comme SDL */
#define ROLE_DARK (WF_GLYPH_LIGHT | WF_BODY_DARK)

static struct ui_vars ui;

int kui_open(void) {
	return uikit_open(&ui);
}

void kui_screen(int *w, int *h, int *bottom, int *top) {
	long scr[4] = { 0, 0, 0, 0 };
	ui_screen((long)scr);
	if (w) {
		*w = (int)scr[0];
	}
	if (h) {
		*h = (int)scr[1];
	}
	if (bottom) {
		*bottom = (int)scr[2];
	}
	if (top) {
		*top = (int)scr[3];
	}
}

long kui_window(int x, int y, int w, int h, const char *title, int kind) {
	long role = ROLE_DARK;
	switch (kind) {
		case KUI_WIN_RESIZABLE:
			role |= WF_RESIZABLE;
			break;
		case KUI_WIN_FULLSCREEN:
			role = WF_FRAMELESS | WF_TOP;
			break;
		case KUI_WIN_BORDERLESS:
			role = WF_FRAMELESS;
			break;
		default:
			break;
	}
	return ui_window_ex(x, y, w, h, (long)(title ? title : "Godot"), role);
}

void kui_window_close(long win) {
	ui_window_close(win);
}

void kui_window_title(long win, const char *title) {
	ui_window_use(win);
	ui_title((long)(title ? title : ""));
}

void kui_window_resize(long win, int w, int h) {
	ui_window_use(win);
	ui_resize(w, h);
}

void kui_window_size(long win, int *w, int *h) {
	ui_window_use(win);
	*w = (int)ui.w;
	*h = (int)ui.h;
}

void kui_window_maximize(long win) {
	ui_window_use(win);
	ui_maximize(); /* (Elegant repond par WEV_RESIZE) */
}

void kui_window_minimize(long win) {
	ui_window_use(win);
	ui_minimize();
}

void kui_window_raise(long win) {
	ui_raise(win);
}

int kui_poll(kui_event *ev, int wait_ms) {
	long t = ui_event(wait_ms);
	if (t <= 0) {
		return 0;
	}
	memset(ev, 0, sizeof *ev);
	ev->type = (int)t;
	ev->kind = ui_ev_field(&ui, EV_KIND);
	ev->x = ui_ev_field(&ui, EV_X);
	ev->y = ui_ev_field(&ui, EV_Y);
	ev->code = ui_ev_field(&ui, EV_CODE);
	ev->ch = ui_ev_field(&ui, EV_CHAR);
	ev->mods = ui_ev_field(&ui, EV_MODS);
	ev->win = ui_ev_field(&ui, EV_WIN);
	if (t == WEV_PAD) {
		memcpy(ev->axes, ui.ev + 16, sizeof ev->axes); /* WEV_PAD_AXES (pad.inc) */
		ev->pad = ui_ev_field(&ui, 4);
		ev->pad_buttons = (unsigned)ui_ev_field(&ui, 8);
	}
	return (int)t;
}

void kui_cursor(long win, int shape) {
	ui_window_use(win);
	ui_cursor(shape);
}

void kui_pointer_lock(long win, int on) {
	ui_window_use(win);
	ui_pointer_lock(on);
}

void kui_clip_set(const char *utf8) {
	ui_clip_set((long)utf8, -1);
}

int kui_clip_get(char *buf, int cap) {
	if (cap <= 0) {
		return 0;
	}
	long n = ui_clip_get((long)buf, cap);
	if (n <= 0) {
		buf[0] = 0;
		return 0;
	}
	buf[cap - 1] = 0;
	return (int)n;
}

/* --- OpenGL ES par armgl (GlKit sur le PC, Mesa liee sur le Pi) --- */

int kgl_init(void) {
	return armgl_init(0);
}

void *kgl_window(long win, int screen_w, int screen_h) {
	return armgl_window(win, 0, 0, screen_w, screen_h);
}

void kgl_zone(void *surface, long win, int w, int h) {
	if (surface) {
		armgl_window_zone(surface, win, 0, 0, w, h);
	}
}

void *kgl_context(void) {
	return armgl_context(3, 0, 0);
}

int kgl_make_current(void *context, void *surface) {
	return armgl_make_current(context, surface);
}

int kgl_swap(void *surface) {
	return armgl_swap(surface);
}

void *kgl_proc(const char *name) {
	return armgl_proc(name);
}

/* Le chargeur GL de Godot (drivers/gles3, GLAD) demande les fonctions par eglGetProcAddress
 * quand EGL_STATIC est defini : armgl_proc en tient lieu. Aucun autre egl* n'est appele. */
void *eglGetProcAddress(const char *name) {
	return armgl_proc(name);
}
