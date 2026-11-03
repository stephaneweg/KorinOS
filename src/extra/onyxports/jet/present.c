/*
 * present.c -- la fenetre de Jet sur UIKit et la presentation de l'image de WebKit (present.h ;
 * docs/WEBKIT.md, etape 4). La page : une surface attachee d'Elegant (ui_surface) ou WebKit peint
 * directement, montree par ui_surface_damage ; sans elle, une copie dans le tampon de la fenetre
 * puis ui_update. L'image du compositeur : Elegant la lit dans le tampon ou le GPU l'a laissee
 * (jet_page_show), une image sur deux : celle qui est montree n'est jamais ecrite.
 */
#include <string.h>
#include <uikit.h>
#include "present.h"

#define JET_WF_RESIZABLE 512			/* (ws_ext.inc : bords et bouton agrandir) */

static struct ui_vars s_ui;
static int s_open;
static long s_surf;				/* la surface de la page (0 : aucune) */
static uint32_t *s_px;				/* ses pixels */
static int s_stride;				/* (en pixels) */
static int s_ox, s_oy, s_pw, s_ph;		/* la zone de la page dans la fenetre */
static int s_area;				/* 1 : zone donnee (jet_page_area) */

/* ui_surface_pixels rend quatre valeurs (x0 pixels, x1 pas en octets, x2, x3 la taille) : les
 * relais generes n'en rendent qu'une */
static uint32_t *surface_pixels (long s, long *pitch)
{
	register long x0 __asm__ ("x0") = s;
	register long x1 __asm__ ("x1");
	__asm__ volatile ("bl ui_surface_pixels" : "+r" (x0), "=r" (x1) : : "x2", "x3", "x16", "x17", "x30", "memory", "cc");
	*pitch = x1;
	return (uint32_t *) x0;
}

int jet_window_open (int width, int height, const char *title, int *w, int *h)
{
	if (!s_open)
	{
		if (!uikit_open (&s_ui))
			return 0;
		if (width <= 0 || height <= 0)		/* (0 : la taille de l'ecran, moins les barres) */
		{
			long scr[4] = { 800, 600, 0, 0 };
			ui_screen ((long) scr);
			if (width <= 0) width = scr[0] > 1080 ? 1000 : (int) scr[0] - 40;
			if (height <= 0) height = scr[1] - scr[2] - scr[3] > 780 ? 700 : (int) (scr[1] - scr[2] - scr[3]) - 70;
		}
		if (ui_window_ex (-1, -1, width, height, (long) title, JET_WF_RESIZABLE) <= 0 || s_ui.surf == 0)
			return 0;
		ui_min_size (420, 300);
		s_open = 1;
		ui_fill (0, 0, s_ui.w, s_ui.h, 0xFFFFFF);
		ui_update_all ();
	}
	if (w) *w = (int) s_ui.w;
	if (h) *h = (int) s_ui.h;
	return 1;
}

void *jet_ui_vars (void)
{
	return &s_ui;
}

void jet_window_title (const char *title)
{
	if (s_open)
		ui_title ((long) title);
}

uint32_t *jet_page_area (int x, int y, int w, int h, int *stride)
{
	if (!s_open || w <= 0 || h <= 0)
		return 0;
	s_ox = x; s_oy = y; s_pw = w; s_ph = h;
	s_area = 1;
	if (s_surf == 0)
		s_surf = ui_surface (x, y, w, h, WS_SF_XRGB8888);
	else if (!ui_surface_move (s_surf, x, y, w, h))
	{
		ui_surface_free (s_surf);
		s_surf = ui_surface (x, y, w, h, WS_SF_XRGB8888);
	}
	s_px = 0;
	if (s_surf)
	{
		long pitch = 0;
		s_px = surface_pixels (s_surf, &pitch);
		s_stride = (int) (pitch / 4);
		if (s_px == 0)
		{
			ui_surface_free (s_surf);
			s_surf = 0;
		}
	}
	if (stride)
		*stride = s_px ? s_stride : 0;
	return s_px;
}

void jet_present (const uint32_t *pixels, int stride, int x, int y, int w, int h)
{
	if (!s_open || pixels == 0)
		return;
	int pw = s_area ? s_pw : (int) s_ui.w, ph = s_area ? s_ph : (int) s_ui.h;
	/* (borne a la page) */
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > pw) w = pw - x;
	if (y + h > ph) h = ph - y;
	if (w <= 0 || h <= 0)
		return;
	if (s_px)
	{
		/* la surface : WebKit y a peint (rien a copier), ou une image a soi copiee dedans */
		if (pixels != s_px)
			for (int row = 0; row < h; row++)
				memcpy (s_px + (long) (y + row) * s_stride + x, pixels + (long) (y + row) * stride + x, (size_t) w * 4);
		ui_surface_damage (s_surf, x, y, w, h);
		return;
	}
	/* Skia N32 (octets B, G, R, A) = 0xAARRGGBB : le format de la fenetre, alpha ignore */
	int ox = s_area ? s_ox : 0, oy = s_area ? s_oy : 0;
	for (int row = 0; row < h; row++)
		memcpy (s_ui.surf + (long) (oy + y + row) * s_ui.w + ox + x, pixels + (long) (y + row) * stride + x,
			(size_t) w * 4);
	ui_update (ox + x, oy + y, w, h);
}

int jet_page_show (int buffer, int pitch, long offset, int x, int y, int w, int h)
{
	if (s_surf == 0)
		return 0;
	if (!ui_surface_source (s_surf, buffer, pitch, offset))
		return 0;
	if (buffer >= 0)
		ui_surface_damage (s_surf, x, y, w, h);
	return 1;
}

int jet_window_event (int wait_ms, struct jet_event *ev)
{
	if (!s_open)
		return JET_EV_NONE;
	long t = ui_event (wait_ms);
	if (t <= 0)
		return JET_EV_NONE;
	if (ev)
	{
		ev->type = (int) t;
		ev->kind = ui_ev_field (&s_ui, EV_KIND);
		ev->x = ui_ev_field (&s_ui, EV_X);
		ev->y = ui_ev_field (&s_ui, EV_Y);
		ev->code = ui_ev_field (&s_ui, EV_CODE);
		ev->ch = ui_ev_field (&s_ui, EV_CHAR);
		ev->mods = ui_ev_field (&s_ui, EV_MODS);
	}
	return (int) t;
}

void jet_window_close (void)
{
	if (s_open)
	{
		if (s_surf)
			ui_surface_free (s_surf);
		s_surf = 0;
		s_px = 0;
		ui_close ();
		s_open = 0;
	}
}
