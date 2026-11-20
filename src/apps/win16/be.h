/* be.h -- win16 : ce que USER et GDI demandent à l'affichage. be_uikit.c le fait sur UIKit (Korin) ;
 * tools/win16/host_be.c le fait sur l'hôte (WSL) pour les essais, sans fenêtre. */
#ifndef W16_BE_H
#define W16_BE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int be_init (void);
/* une fenêtre de Korin dont la zone cliente fait w × h (x, y < 0 : au choix d'Elegant) -> poignée, 0 si échec */
long be_window_open (int x, int y, int w, int h, const char *title);
void be_window_close (long win);
void be_window_title (long win, const char *title);
void be_window_resize (long win, int w, int h);
/* les pixels de la zone cliente (0x00RRGGBB), pas en pixels */
uint32_t *be_pixels (long win, int *w, int *h, int *stride);
void be_present (long win, int x, int y, int w, int h);
/* le texte (UTF-8) : y = haut de la cellule ; px = taille, style 0 normal, 1 gras, 2 chasse fixe ; rend l'avance ;
 * découpé au rectangle donné ; dst = pixels où écrire (0 : la fenêtre), dw × dh, pas dw */
int be_text (long win, int x, int y, const char *s, uint32_t color, int cx, int cy, int cw, int ch, int px, int style);
int be_text_buf (uint32_t *dst, int dw, int dh, int x, int y, const char *s, uint32_t color, int cx, int cy, int cw,
	int ch, int px, int style);
int be_text_width (const char *s, int px, int style);
int be_font_height (int px);
int be_font_ascent (int px);
/* les menus de la fenêtre dans la barre de Korin (texte de ui_menu ; 0 : aucun) ; états : id, UMS_* */
void be_menu_set (long win, const char *text);
void be_menu_state (long win, int id, int state);
/* menu surgissant en (x, y) de la zone cliente : libellés à la suite (« - » séparateur ; 0x01 grisé, 0x02 coché),
 * une chaîne vide à la fin -> numéro choisi, -1 sinon */
int be_popup (long win, int x, int y, const char *labels, int w);
/* le thème du bureau (jetons de ui_tokens) */
enum { BT_SURFACE, BT_TEXT, BT_LINE_STRONG, BT_ACCENT, BT_RAISED, BT_SUNKEN, BT_LINE, BT_ACCENT_INK, BT_TEXT3, BT_FACE,
	BT_ACCENT_LIGHT, BT_HOVER };
uint32_t be_theme (int what);

enum { BE_NONE, BE_CLOSE, BE_MOUSE_MOVE, BE_MOUSE_DOWN, BE_MOUSE_UP, BE_KEY_DOWN, BE_KEY_UP, BE_RESIZE, BE_FOCUS,
	BE_MENU, BE_WHEEL, BE_WIDGET };
struct be_event
{
	int type;
	long win;
	int x, y;		/* dans la zone cliente */
	int button;		/* 1 gauche, 2 droit, 4 milieu */
	int key;		/* code HID (KEY_*) ; BE_MENU : l'id de l'entrée ; BE_WHEEL : crans */
	int ch;			/* caractère Unicode, 0 sinon */
	int mods;		/* 1 Ctrl, 2 Maj, 4 Alt */
};
/* attend un événement au plus ms millisecondes -> 1 si *ev est rempli */
int be_wait (int ms, struct be_event *ev);
uint32_t be_ticks (void);
/* boîte de message : type MB_* de Windows (bas : 0 OK, 1 OK/Annuler, 3 Oui/Non/Annuler, 4 Oui/Non) -> ID* */
int be_message (const char *title, const char *text, int type);
void be_fatal (const char *msg);
/* dialogues de Korin : fichier à ouvrir, à enregistrer (filtre « *.a;*.b », 0 : tous) -> 1 et le chemin ; couleur */
int be_file_open (const char *dir, const char *filter, char *out, int cap);
int be_file_save (const char *dir, const char *name, const char *filter, char *out, int cap);
long be_color (const char *title, uint32_t rgb);
void be_clip_put (const char *utf8);
int be_clip_take (char *buf, int cap);
/* les widgets de UIKit (les contrôles standard de Win16 en mode natif : ctrl.c) ; x, y dans la zone cliente de la
 * fenêtre de Korin ; textes en UTF-8 ; les événements reviennent en BE_WIDGET (key = widget, x = UN_*) */
enum { BW_PUSH, BW_DEFPUSH, BW_CHECK, BW_RADIO, BW_TEXT, BW_PASSWORD, BW_TEXTAREA, BW_LIST, BW_DROPDOWN, BW_COMBO,
	BW_VSCROLL, BW_HSCROLL, BW_LABEL, BW_LABEL_CENTER, BW_LABEL_RIGHT, BW_GROUP };
/* les mesures de UIKit (ui_metric) : taille du texte, hauteur d'un contrôle, d'une ligne ; 0 sans UIKit */
enum { BEM_PX, BEM_CTL, BEM_ROW };
int be_ui_metric (int what);
enum { BWN_CHANGE = 1, BWN_ACTIVATE = 2 };			/* codes des événements (UN_CHANGE, UN_ACTIVATE) */
enum { BWF_READONLY = 1, BWF_VSCROLL = 2, BWF_HSCROLL = 4 };	/* extra des champs et zones de texte */
long be_w_create (long win, int kind, int x, int y, int w, int h, const char *text, long extra);
void be_w_move (long wd, int x, int y, int w, int h);
void be_w_show (long wd, int show);
void be_w_enable (long wd, int on);
void be_w_focus (long wd);
void be_w_set_text (long wd, const char *text);
const char *be_w_get_text (long wd);
void be_w_set_value (long wd, long v);
long be_w_get_value (long wd);
void be_w_set_range (long wd, long min, long max, long page, long step);
void be_w_list_add (long wd, const char *text);
void be_w_list_clear (long wd);
void be_w_draw (long wd);
void be_w_destroy (long wd);
/* la sortie MIDI : le synthé General MIDI d'AudioKit (la SoundFont du système), joué par un fil à part comme dans
 * Arabesque ; be_synth_start le lance, be_synth_midi y envoie un message (canal, commande, données) */
int be_synth_start (void);
void be_synth_midi (int ch, int cmd, int a, int b);
void be_synth_reset (void);
/* la sortie PCM (44 100 Hz, stéréo, 16 bits) : fill est appelée par un fil à part, un bloc à la fois */
int be_pcm_start (void (*fill) (int16_t *buf, int frames));

#ifdef __cplusplus
}
#endif
#endif
