/* Console texte du noyau sur un framebuffer 32 bits (police Spleen 8x16). */
#ifndef KORIN_CONSOLE_H
#define KORIN_CONSOLE_H

#include <stdint.h>

struct fb_info {
	volatile uint32_t *base; /* virtuel */
	uint32_t width, height;
	uint32_t pitch;          /* pixels par ligne */
	int bgr;                 /* 1 : 0x00RRGGBB ; 0 : 0x00BBGGRR */
};

enum con_color {
	CON_NORMAL,
	CON_DIM,
	CON_ACCENT,
	CON_OK,
	CON_WARN,
	CON_ERROR,
	CON_COLORS
};

void console_init(const struct fb_info *fb);
int console_ready(void);
void console_clear(void);
void console_color(enum con_color c);
/* un octet Latin-1 (\n, \r, \b, \t compris) */
void console_putc(unsigned char c);
/* texte UTF-8 (les caractères hors Latin-1 deviennent « ? ») */
void console_write(const char *s);
/* texte à droite de la barre du haut */
void console_status(const char *s);
void console_size(uint32_t *cols, uint32_t *rows);
/* 1 : le serveur graphique a l'écran, la console se tait (le journal continue) */
void console_silence(int on);

#endif
