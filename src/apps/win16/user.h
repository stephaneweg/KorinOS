/* user.h -- win16 : ce que USER partage avec GDI */
#ifndef W16_USER_H
#define W16_USER_H
#include <stdint.h>

/* les mesures de Windows 3.1 (GetSystemMetrics) : le programme calcule ses tailles avec elles */
#define W16_CAPTION 20
#define W16_FRAME 4
#define W16_MENU 18

struct wnd;
struct wnd *wnd_get (uint16_t hwnd);
/* l'origine de la zone cliente de hwnd dans la fenêtre de Korin, sa taille, la fenêtre de Korin */
int wnd_client_geometry (uint16_t hwnd, int *ox, int *oy, int *cw, int *ch, long *be);
uint32_t user_sys_color (int index);	/* COLORREF */
uint32_t user_send (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);

/* GDI (gdi.c) pour USER */
uint16_t gdi_dc_for_window (uint16_t hwnd);
void gdi_release_dc (uint16_t hdc);
void gdi_fill_rect_brush (uint16_t hdc, int l, int t, int r, int b, uint16_t hbrush);

#endif
