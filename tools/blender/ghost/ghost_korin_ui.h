/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * Korin OS : ce que GHOST prend a UIKit (fenetres, evenements, curseur, presse-papiers).
 * Repris de la plateforme korin de Godot (tools/godot/platform/korin/korin_ui.h), sans la partie
 * OpenGL (kgl_*) : Blender ne dessine que par Vulkan (VkKit). Le C++ de GHOST ne voit que ces
 * fonctions : l'en-tete genere d'UIKit (<uikit.h>, uikit_abi.h) reste dans ghost_korin_ui.c.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* types d'evenements (src/inc/armpi.inc, src/inc/ws_ext.inc) */
#define KUI_EV_KEY 1    /* kind 1 appui / 0 relache ; code HID, ch caractere, mods */
#define KUI_EV_MOUSE 2  /* kind 0 deplacement, 1 appui, 2 relache, 3 relatif ; x, y ; code = boutons */
#define KUI_EV_CLOSE 3
#define KUI_EV_WHEEL 4  /* y = crans */
#define KUI_EV_FOCUS 5  /* kind 1 gagne / 0 perdu */
#define KUI_EV_RESIZE 8 /* x, y = largeur, hauteur demandees */
#define KUI_EV_DROP 9
#define KUI_EV_FRAME 10 /* le contenu a ete pris : le programme peut redessiner */
#define KUI_EV_PAD 11
#define KUI_EV_SCREEN 12 /* x, y = largeur, hauteur de l'ecran */
#define KUI_EV_THEME 14

/* genres d'evenements de souris (EV_KIND de WEV_MOUSE) */
#define KUI_M_MOVE 0
#define KUI_M_DOWN 1
#define KUI_M_UP 2
#define KUI_M_REL 3 /* pointeur pris (kui_pointer_lock) : x, y = dx, dy */

/* boutons (EV_CODE de WEV_MOUSE) : bit 0 gauche, 1 droit, 2 milieu */
#define KUI_BUTTON_LEFT 1
#define KUI_BUTTON_RIGHT 2
#define KUI_BUTTON_MIDDLE 4

/* EV_MODS : l'octet des modificateurs du HID (bits 0-3 a gauche, 4-7 a droite) */
#define KUI_MOD_LCTRL 0x01
#define KUI_MOD_LSHIFT 0x02
#define KUI_MOD_LALT 0x04
#define KUI_MOD_LMETA 0x08
#define KUI_MOD_RCTRL 0x10
#define KUI_MOD_RSHIFT 0x20
#define KUI_MOD_RALT 0x40 /* AltGr */
#define KUI_MOD_RMETA 0x80
#define KUI_MOD_CTRL 0x11
#define KUI_MOD_SHIFT 0x22
#define KUI_MOD_ALT 0x44
#define KUI_MOD_META 0x88

/* formes du curseur (UC_* de ws_ext.inc) */
#define KUI_CUR_ARROW 0
#define KUI_CUR_TEXT 1
#define KUI_CUR_HAND 2
#define KUI_CUR_HRESIZE 3
#define KUI_CUR_VRESIZE 4
#define KUI_CUR_WAIT 5
#define KUI_CUR_CROSS 6
#define KUI_CUR_DRAG 7
#define KUI_CUR_MOVE 8
#define KUI_CUR_NWSE 9
#define KUI_CUR_NESW 10
#define KUI_CUR_NO 11

/* genres de fenetres pour kui_window */
#define KUI_WIN_FRAMED 0     /* cadre du theme */
#define KUI_WIN_RESIZABLE 1  /* cadre, redimensionnable */
#define KUI_WIN_FULLSCREEN 2 /* sans cadre, au-dessus de tout (plein ecran) */
#define KUI_WIN_BORDERLESS 3 /* sans cadre */

typedef struct kui_event {
  int type, kind, x, y, code, ch, mods;
  long win;
  short axes[4]; /* KUI_EV_PAD : quatre axes */
  unsigned pad_buttons;
  int pad;
} kui_event;

/* UIKit ouvert, polices chargees -> 1 ; 0 : pas d'UIKit (pas de bureau) */
int kui_open(void);
/* largeur, hauteur de l'ecran, bandes reservees en bas et en haut (panneau, barre de menus) */
void kui_screen(int *w, int *h, int *bottom, int *top);
/* -> poignee de la fenetre (<= 0 : refusee) ; c'est aussi le numero que VkKorinSurfaceInfo.window attend */
long kui_window(int x, int y, int w, int h, const char *title, int kind);
void kui_window_close(long win);
void kui_window_title(long win, const char *title);
/* la fenetre prend la taille demandee (son tampon suit) */
void kui_window_resize(long win, int w, int h);
void kui_window_size(long win, int *w, int *h);
/* x, y du contenu de la fenetre sur l'ecran -> 0 (< 0 : inconnue) */
int kui_window_pos(long win, int *x, int *y);
/* le contenu de la fenetre va en x, y de l'ecran (son cadre autour) */
void kui_window_place(long win, int x, int y);
/* 1 : la fenetre reste au-dessus des autres ; 0 : comme les autres */
void kui_window_top(long win, int on);
void kui_window_maximize(long win);
void kui_window_minimize(long win);
void kui_window_raise(long win);
/* -> type de l'evenement (0 : aucun dans le delai) */
int kui_poll(kui_event *ev, int wait_ms);
void kui_cursor(long win, int shape);
void kui_pointer_lock(long win, int on);
void kui_clip_set(const char *utf8);
/* -> octets (texte termine par NUL dans buf), 0 : rien */
int kui_clip_get(char *buf, int cap);

#ifdef __cplusplus
}
#endif
