/* gfx_uikit.c : la fenetre de gfxlib2 sur UIKit (gfx_driver_uikit.c). Le cadre, la barre de titre et
   leurs couleurs sont ceux du theme du bureau, comme pour toute fenetre d'UIKit. */

#include <uikit.h>
#include "gfx_uikit.h"

static struct ui_vars s_ui;
static int s_open;

static int open_kit (void)
{
	if (!s_open)
		s_open = uikit_open (&s_ui) ? 1 : -1;
	return s_open > 0;
}

int uikit_screen_size (int *w, int *h)
{
	long scr[4] = { 0, 0, 0, 0 };

	if (!open_kit ())
		return 0;
	ui_screen ((long) scr);
	*w = (int) scr[0];
	*h = (int) (scr[1] - scr[2] - scr[3]);		/* (moins les barres) */
	return *w > 0 && *h > 0;
}

int uikit_window_open (int w, int h, const char *title)
{
	if (!open_kit ())
		return 0;
	return ui_window (w, h, (long) title) > 0 && s_ui.surf != 0;
}

void uikit_window_close (void)
{
	ui_close ();
	s_ui.surf = 0;
}

uint32_t *uikit_window_pixels (int *w, int *h)
{
	*w = (int) s_ui.w;
	*h = (int) s_ui.h;
	return s_ui.surf;
}

void uikit_window_update (int x, int y, int w, int h)
{
	ui_update (x, y, w, h);
}

void uikit_window_title (const char *title)
{
	ui_title ((long) title);
}

int uikit_next_event (int wait_ms, struct uikit_event *e)
{
	int type = (int) ui_event (wait_ms);

	e->type = type;
	if (type <= 0)
		return 0;
	e->kind = ui_ev_field (&s_ui, EV_KIND);
	e->x = ui_ev_field (&s_ui, EV_X);
	e->y = ui_ev_field (&s_ui, EV_Y);
	e->code = ui_ev_field (&s_ui, EV_CODE);
	e->ch = ui_ev_field (&s_ui, EV_CHAR);
	e->mods = ui_ev_field (&s_ui, EV_MODS);
	return 1;
}
