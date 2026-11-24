// korin_ui.h : ce que la QPA korin prend à UIKit (fenêtres, pixels, événements, curseur, presse-papiers, menus,
// thème). Repris de tools/blender/ghost/ghost_korin_ui.h (et de korin_ui.h de Godot) : le C++ de Qt ne voit que ces
// fonctions ; l'en-tête généré d'UIKit (<uikit.h>, uikit_abi.h) reste dans korin_ui.c.
// Tout se fait sur le fil principal de Qt (celui de QGuiApplication).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* types d'événements (src/inc/armpi.inc, src/inc/ws_ext.inc) */
#define KQ_EV_KEY 1     /* kind 1 appui / 0 relâche ; code HID, ch caractère, mods */
#define KQ_EV_MOUSE 2   /* kind 0 déplacement, 1 appui, 2 relâche, 3 relatif ; x, y ; code = boutons */
#define KQ_EV_CLOSE 3
#define KQ_EV_WHEEL 4   /* y = crans, positif vers le haut */
#define KQ_EV_FOCUS 5   /* kind 1 gagné / 0 perdu */
#define KQ_EV_MENU 7    /* code = id de l'entrée choisie dans la barre de menus */
#define KQ_EV_RESIZE 8  /* x, y = largeur, hauteur demandées */
#define KQ_EV_DROP 9
#define KQ_EV_FRAME 10  /* le contenu a été pris : on peut redessiner */
#define KQ_EV_SCREEN 12 /* x, y = largeur, hauteur de l'écran */
#define KQ_EV_THEME 14  /* le thème du bureau a changé */

#define KQ_M_MOVE 0
#define KQ_M_DOWN 1
#define KQ_M_UP 2
#define KQ_M_REL 3

/* boutons (EV_CODE de WEV_MOUSE) */
#define KQ_BUTTON_LEFT 1
#define KQ_BUTTON_RIGHT 2
#define KQ_BUTTON_MIDDLE 4

/* EV_MODS : l'octet des modificateurs du HID (bits 0-3 à gauche, 4-7 à droite) */
#define KQ_MOD_LCTRL 0x01
#define KQ_MOD_LSHIFT 0x02
#define KQ_MOD_LALT 0x04
#define KQ_MOD_LMETA 0x08
#define KQ_MOD_RCTRL 0x10
#define KQ_MOD_RSHIFT 0x20
#define KQ_MOD_RALT 0x40 /* AltGr */
#define KQ_MOD_RMETA 0x80
#define KQ_MOD_CTRL 0x11
#define KQ_MOD_SHIFT 0x22
#define KQ_MOD_ALT 0x44
#define KQ_MOD_META 0x88

/* formes du curseur (UC_* de ws_ext.inc) */
#define KQ_CUR_ARROW 0
#define KQ_CUR_TEXT 1
#define KQ_CUR_HAND 2
#define KQ_CUR_HRESIZE 3
#define KQ_CUR_VRESIZE 4
#define KQ_CUR_WAIT 5
#define KQ_CUR_CROSS 6
#define KQ_CUR_DRAG 7
#define KQ_CUR_MOVE 8
#define KQ_CUR_NWSE 9
#define KQ_CUR_NESW 10
#define KQ_CUR_NO 11

/* genres de fenêtres */
#define KQ_WIN_FRAMED 0     /* cadre du thème, taille fixe */
#define KQ_WIN_RESIZABLE 1  /* cadre, redimensionnable */
#define KQ_WIN_POPUP 2      /* sans cadre, au-dessus de tout (menus, listes, infobulles) */
#define KQ_WIN_BORDERLESS 3 /* sans cadre */
#define KQ_WIN_DIALOG 4     /* cadre, modale (WF_MODAL) */

/* Korin n'a pas de barre de titre : les boutons du cadre sont sur le contenu, en haut à droite, et le haut du
 * contenu déplace la fenêtre. Le contenu de Qt d'une fenêtre à cadre commence KQ_TOP_BAND px plus bas (comme
 * Blender) ; la bande porte le titre, aux couleurs du thème. */
#define KQ_TOP_BAND 36

/* jetons du thème (TJ_* de src/kits/uikit/uikit.inc, en index de mots) */
#define KQ_TJ_FACE 0
#define KQ_TJ_SURFACE 1
#define KQ_TJ_SUNKEN 2
#define KQ_TJ_RAISED 3
#define KQ_TJ_RAISED_LOW 4
#define KQ_TJ_LINE 5
#define KQ_TJ_LINE_STRONG 6
#define KQ_TJ_TEXT1 7
#define KQ_TJ_TEXT2 8
#define KQ_TJ_TEXT3 9
#define KQ_TJ_ACCENT 10
#define KQ_TJ_ACCENT_LIGHT 11
#define KQ_TJ_ACCENT_DARK 12
#define KQ_TJ_ACCENT_VIVID 13
#define KQ_TJ_DANGER 14
#define KQ_TJ_SUCCESS 15
#define KQ_TJ_WARNING 16
#define KQ_TJ_BAR 17
#define KQ_TJ_DOCK 18
#define KQ_TJ_TITLE_ACTIVE 19
#define KQ_TJ_TITLE_INACTIVE 20
#define KQ_TJ_OUTLINE 21
#define KQ_TJ_DARK 22
#define KQ_TJ_MENU 23
#define KQ_TJ_ACCENT_INK 24
#define KQ_TJ_HOVER 25

typedef struct kq_event {
	int type, kind, x, y, code, ch, mods;
	long win;
} kq_event;

/* UIKit ouvert, polices chargées -> 1 ; 0 : pas d'UIKit (pas de bureau) */
int kq_open(void);
/* largeur, hauteur de l'écran, bandes réservées en bas et en haut (panneau, barre de menus) */
void kq_screen(int *w, int *h, int *bottom, int *top);
/* -> poignée de la fenêtre (<= 0 : refusée) ; x, y : place du cadre sur l'écran */
long kq_window(int x, int y, int w, int h, const char *title, int kind);
void kq_window_close(long win);
void kq_window_title(long win, const char *title);
/* dessine la bande de titre (KQ_TOP_BAND px en haut du contenu) et la montre */
void kq_title_band(long win, const char *title, int active);
/* la fenêtre prend la taille demandée (son tampon suit) */
void kq_window_resize(long win, int w, int h);
void kq_window_size(long win, int *w, int *h);
/* x, y du contenu de la fenêtre sur l'écran -> 0 (< 0 : inconnue) */
int kq_window_pos(long win, int *x, int *y);
void kq_window_place(long win, int x, int y);
void kq_window_top(long win, int on);
void kq_window_maximize(long win);
void kq_window_minimize(long win);
void kq_window_raise(long win);
void kq_window_min_size(long win, int w, int h);
/* les pixels de la fenêtre (0x00RRGGBB, pas = w * 4) ; valables jusqu'au prochain kq_window_resize */
unsigned *kq_window_pixels(long win, int *w, int *h);
/* montre un rectangle du contenu (coordonnées de la fenêtre d'UIKit) */
void kq_window_update(long win, int x, int y, int w, int h);
/* -> type de l'événement (0 : aucun dans le délai) */
int kq_poll(kq_event *ev, int wait_ms);
void kq_cursor(long win, int shape);
void kq_clip_set(const char *utf8);
/* -> octets (texte terminé par NUL dans buf), 0 : rien */
int kq_clip_get(char *buf, int cap);
/* le texte des menus de la fenêtre (docs/EGUI.md, lignes M, I, -, S … E, M*) : 0 aucun ; -> 0 / erreur */
long kq_menu(long win, const char *text);
/* les états d'entrées de menu (paires id, état : 1 grisée, 2 cochée), un seul envoi */
void kq_menu_states(const long *pairs, long n);
/* le tableau des jetons du thème (0x00RRGGBB, KQ_TJ_*) */
const unsigned *kq_tokens(void);
/* une métrique d'UIKit (ui_metric) : KQ_UMT_* -> pixels (KQ_UMT_TOUCH : 1 en densité tactile) ; -1 : inconnue */
#define KQ_UMT_PX 0
#define KQ_UMT_SMALL 1
#define KQ_UMT_ROW 2
#define KQ_UMT_CTL 3
#define KQ_UMT_SB 4
#define KQ_UMT_HDR 5
#define KQ_UMT_MENU 6
#define KQ_UMT_TOUCH 7
int kq_metric(int which);
/* les polices : chemin du fichier de la police de l'interface (UIKit) ou 0 */
const char *kq_font_dir(void);

#ifdef __cplusplus
}
#endif
