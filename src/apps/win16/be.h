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
/* le texte (UTF-8) : y = haut de la cellule ; rend l'avance ; découpé au rectangle donné */
int be_text (long win, int x, int y, const char *s, uint32_t color, int cx, int cy, int cw, int ch);
int be_text_width (const char *s);
int be_font_height (void);
int be_font_ascent (void);
/* le thème du bureau (jetons de ui_tokens) : 0 fond, 1 texte, 2 trait, 3 accent */
uint32_t be_theme (int what);

enum { BE_NONE, BE_CLOSE, BE_MOUSE_MOVE, BE_MOUSE_DOWN, BE_MOUSE_UP, BE_KEY_DOWN, BE_KEY_UP, BE_RESIZE, BE_FOCUS };
struct be_event
{
	int type;
	long win;
	int x, y;		/* dans la zone cliente */
	int button;		/* 1 gauche, 2 droit, 4 milieu */
	int key;		/* code HID (KEY_*) */
	int ch;			/* caractère Unicode, 0 sinon */
	int mods;		/* 1 Ctrl, 2 Maj, 4 Alt */
};
/* attend un événement au plus ms millisecondes -> 1 si *ev est rempli */
int be_wait (int ms, struct be_event *ev);
uint32_t be_ticks (void);
/* boîte de message : type MB_* de Windows (bas : 0 OK, 1 OK/Annuler, 3 Oui/Non/Annuler, 4 Oui/Non) -> ID* */
int be_message (const char *title, const char *text, int type);
void be_fatal (const char *msg);

#ifdef __cplusplus
}
#endif
#endif
