/* gdi.h -- win16 : GDI pour USER (contrôles, dialogues, menus) */
#ifndef W16_GDI_H
#define W16_GDI_H
#include <stdint.h>

/* un DC sur la zone cliente d'une fenêtre (BeginPaint, GetDC) */
uint16_t gdi_dc_for_window (uint16_t hwnd);
uint16_t gdi_dc_for_window_nc (uint16_t hwnd);	/* le rectangle entier de la fenêtre (cadre, barre de titre) */
/* découpe le DC à ce rectangle (coordonnées de l'appareil, zone cliente) : la zone à repeindre de BeginPaint */
void gdi_dc_clip (uint16_t hdc, int l, int t, int r, int b);
void gdi_release_dc (uint16_t hdc);
void gdi_set_rect_rgn (uint16_t hrgn, int l, int t, int r, int b);
void gdi_fill_rect_brush (uint16_t hdc, int l, int t, int r, int b, uint16_t hbrush);
uint32_t gdi_brush_color (uint16_t hbrush, int *hollow);	/* COLORREF */

/* tracés en coordonnées du DC, couleurs 0x00RRGGBB (les contrôles) */
void gdi_rgb_fill (uint16_t hdc, int l, int t, int r, int b, uint32_t rgb);
void gdi_rgb_frame (uint16_t hdc, int l, int t, int r, int b, uint32_t rgb);
void gdi_rgb_rbox (uint16_t hdc, int l, int t, int r, int b, int radius, uint32_t fill, uint32_t border);
void gdi_rgb_line (uint16_t hdc, int x0, int y0, int x1, int y1, uint32_t rgb);
void gdi_rgb_disc (uint16_t hdc, int cx, int cy, int radius, uint32_t rgb);
/* texte CP1252 (n < 0 : jusqu'au NUL) avec la police choisie dans le DC (font = 0) ou une autre */
int gdi_text (uint16_t hdc, int x, int y, const char *s, int n, uint32_t rgb, uint16_t font);
int gdi_text_width (uint16_t hdc, const char *s, int n, uint16_t font);
int gdi_text_height (uint16_t hdc, uint16_t font);
uint16_t gdi_get_font (uint16_t hdc);
void gdi_set_font (uint16_t hdc, uint16_t font);
uint32_t gdi_text_color (uint16_t hdc);
uint32_t gdi_bk_color (uint16_t hdc);
void gdi_set_colors (uint16_t hdc, uint32_t text_ref, uint32_t bk_ref);
uint32_t gdi_ref_to_rgb (uint16_t hdc, uint32_t ref);
uint16_t gdi_stock (int i);
uint16_t gdi_icon_load (uint16_t hinst, uint32_t name);
uint16_t gdi_icon_default (void);	/* IDI_APPLICATION */
void gdi_icon_draw (uint16_t hdc, int x, int y, uint16_t hicon);
uint16_t gdi_bitmap_from_dib (const uint8_t *dib, uint32_t size);
void gdi_invert (uint16_t hdc, int l, int t, int r, int b);
/* DrawText de USER */
int gdi_draw_text (uint16_t hdc, const char *s, int n, int *rect, unsigned fmt);

#endif
