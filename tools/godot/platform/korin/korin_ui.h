/**************************************************************************/
/*  korin_ui.h                                                            */
/**************************************************************************/
/* Korin OS : ce que la plateforme korin de Godot prend a UIKit (fenetres,
 * evenements, curseur, presse-papiers) et a armgl (OpenGL ES 3.0 par GlKit,
 * Mesa). Le C++ de Godot ne voit que ces fonctions : les en-tetes generes
 * d'UIKit et d'armgl restent dans korin_ui.c (docs/GODOT.md).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* types d'evenements (src/inc/armpi.inc, src/inc/ws_ext.inc) */
#define KUI_EV_KEY 1 /* kind 1 appui / 0 relache ; code HID, ch caractere, mods */
#define KUI_EV_MOUSE 2 /* kind 0 deplacement, 1 appui, 2 relache, 3 relatif ; x, y ; code = boutons */
#define KUI_EV_CLOSE 3
#define KUI_EV_WHEEL 4 /* y = crans */
#define KUI_EV_FOCUS 5 /* kind 1 gagne / 0 perdu */
#define KUI_EV_RESIZE 8 /* x, y = largeur, hauteur demandees */
#define KUI_EV_DROP 9
#define KUI_EV_PAD 11
#define KUI_EV_SCREEN 12 /* x, y = largeur, hauteur de l'ecran */
#define KUI_EV_THEME 14

#define KUI_M_REL 3

/* EV_MODS : les deux touches de chaque modificateur */
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
#define KUI_WIN_FRAMED 0 /* cadre du theme */
#define KUI_WIN_RESIZABLE 1 /* cadre, redimensionnable */
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
long kui_window(int x, int y, int w, int h, const char *title, int kind);
void kui_window_close(long win);
void kui_window_title(long win, const char *title);
/* la fenetre prend la taille demandee (son tampon suit) */
void kui_window_resize(long win, int w, int h);
void kui_window_size(long win, int *w, int *h);
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

/* OpenGL ES 3.0 par armgl : -> 0 pas de GL, 1 GPU, 3 rendu par le processeur (softpipe) */
int kgl_init(void);
/* surface du GPU pour la fenetre win, de la taille de l'ecran ; la zone montree suit kgl_zone */
void *kgl_window(long win, int screen_w, int screen_h);
void kgl_zone(void *surface, long win, int w, int h);
void *kgl_context(void);
int kgl_make_current(void *context, void *surface);
int kgl_swap(void *surface);
void *kgl_proc(const char *name);

#ifdef __cplusplus
}
#endif
