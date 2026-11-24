/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * Korin OS : UIKit pour GHOST (ghost_korin_ui.h). Meme montage que la plateforme korin de Godot
 * (tools/godot/platform/korin/korin_ui.c) : une fenetre de Blender est une fenetre d'UIKit, cadre et
 * couleurs du theme ; son contenu est la surface d'une chaine d'echange de Vulkan (VkKit, WSI de
 * Korin) que Elegant compose sans recopie. Pas d'OpenGL ici.
 *
 * UIKit garde une « fenetre courante » (ui.win, ui.w, ui.h) : chaque fonction qui vise une fenetre
 * la choisit d'abord par ui_window_use. Tout se fait sur le fil principal de Blender (celui de GHOST).
 */

#include "ghost_korin_ui.h"

#include <string.h>
#include <uikit.h>

/* le cadre de Blender : contenu fonce sous les boutons du cadre, comme Godot et SDL
 * (des drapeaux du theme, pas des couleurs) */
#define ROLE_DARK (WF_GLYPH_LIGHT | WF_BODY_DARK)

static struct ui_vars ui;

int kui_open(void)
{
  return uikit_open(&ui);
}

void kui_screen(int *w, int *h, int *bottom, int *top)
{
  long scr[4] = {0, 0, 0, 0};
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

long kui_window(int x, int y, int w, int h, const char *title, int kind)
{
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
  return ui_window_ex(x, y, w, h, (long)(title ? title : "Blender"), role);
}

void kui_window_close(long win)
{
  if (win > 0) { /* (0 fermerait la courante) */
    ui_window_close(win);
  }
}

void kui_window_title(long win, const char *title)
{
  ui_window_use(win);
  ui_title((long)(title ? title : ""));
}

void kui_window_resize(long win, int w, int h)
{
  ui_window_use(win);
  ui_resize(w, h);
}

void kui_window_size(long win, int *w, int *h)
{
  ui_window_use(win);
  *w = (int)ui.w;
  *h = (int)ui.h;
}

int kui_window_pos(long win, int *x, int *y)
{
  long pos[2] = {0, 0}; /* (deux mots de 64 bits : comme Terminal et Jamendo) */
  ui_window_use(win);
  if (ui_window_pos((long)pos) < 0) {
    return -1;
  }
  *x = (int)pos[0];
  *y = (int)pos[1];
  return 0;
}

void kui_window_place(long win, int x, int y)
{
  ui_window_use(win);
  ui_window_place(x, y);
}

void kui_window_top(long win, int on)
{
  ui_window_use(win);
  ui_window_top(on ? 1 : 0);
}

void kui_window_maximize(long win)
{
  ui_window_use(win);
  ui_maximize(); /* (comme le bouton agrandir ; Elegant repond par WEV_RESIZE) */
}

void kui_window_minimize(long win)
{
  ui_window_use(win);
  ui_minimize();
}

void kui_window_raise(long win)
{
  ui_raise(win);
}

int kui_poll(kui_event *ev, int wait_ms)
{
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

void kui_cursor(long win, int shape)
{
  ui_window_use(win);
  ui_cursor(shape);
}

void kui_pointer_lock(long win, int on)
{
  ui_window_use(win);
  ui_pointer_lock(on);
}

void kui_clip_set(const char *utf8)
{
  ui_clip_set((long)utf8, -1);
}

int kui_clip_get(char *buf, int cap)
{
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
