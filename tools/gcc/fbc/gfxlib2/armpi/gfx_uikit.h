/* gfx_uikit.h : ce que le pilote de gfxlib2 voit d'UIKit. Deux fichiers parce qu'uikit.h et les
   en-tetes de FreeBASIC definissent les memes noms (KEY_*). */
#ifndef _GFX_UIKIT_H
#define _GFX_UIKIT_H

#include <stdint.h>

/* un evenement d'UIKit : type = WEV_*, reste comme ui_ev */
struct uikit_event
{
	int type, kind, x, y, code, ch, mods;
};

enum { UIKIT_KEY = 1, UIKIT_MOUSE = 2, UIKIT_CLOSE = 3, UIKIT_WHEEL = 4, UIKIT_FOCUS = 5 };

int uikit_screen_size (int *w, int *h);			/* la taille de l'ecran -> 1 si UIKit est la */
int uikit_window_open (int w, int h, const char *title);
void uikit_window_close (void);
uint32_t *uikit_window_pixels (int *w, int *h);		/* 0x00RRGGBB, pas = *w * 4 */
void uikit_window_update (int x, int y, int w, int h);
void uikit_window_title (const char *title);
int uikit_next_event (int wait_ms, struct uikit_event *e);	/* -> 0 : rien */

#endif
