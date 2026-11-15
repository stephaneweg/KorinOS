/* glview.h : une vue GL dans une fenêtre d'UIKit, qui suit la taille de la fenêtre (docs/DESIGN-CAO.md §11, manque 2).
 * En-tête seul, sur armgl (GlKit) : rien à reconstruire dans le kit.
 *
 * La surface GL est faite une fois à une taille maximale (l'écran, par défaut) ; seule sa zone montrée change
 * quand la fenêtre change de taille (armgl_window_zone), comme le fait l'hôte d'Avalonia (ArmpiGl.cs) : pas de
 * nouvelle surface ni de nouveaux tampons à chaque WEV_RESIZE. Une zone plus grande que la surface en fait une
 * nouvelle, à la nouvelle taille.
 *
 *   struct gl_view v;
 *   gl_view_open (&v, win, 0, 0, sw, sh);         une fois, le contexte créé (armgl_context) ; win : ui_vars.win
 *   gl_view_place (&v, win, x, y, w, h);          la zone de la vue dans le contenu, à chaque WEV_RESIZE
 *   armgl_make_current (ctx, v.surf);             puis dessiner en 0, 0, v.w, v.h ; armgl_swap (v.surf)
 *   gl_view_close (&v);
 *
 * Les widgets d'UIKit posés sur la vue (panneaux, cotes, boutons) : ui_surface_overlay, avec leurs zones.
 * Pas essayé avec un vrai GPU (ni sur le ZBook ni sur le Pi). */
#ifndef KORIN_GLVIEW_H
#define KORIN_GLVIEW_H

#include <armgl.h>
#include <GLES3/gl3.h>

struct gl_view {
	void *surf;      /* la surface d'armgl (0 : pas de surface) */
	long win;        /* la fenêtre où elle est montrée */
	int cap_w, cap_h; /* sa taille réelle : la plus grande zone sans nouvelle surface */
	int x, y, w, h;  /* la zone montrée, dans le contenu */
};

/* surface de cap_w × cap_h (d'ordinaire la taille de l'écran : une fenêtre agrandie y tient), montrée en x, y du
 * contenu de la fenêtre win ; gl_view_place donne ensuite la vraie zone -> 1 (0 : pas de surface) */
static inline int gl_view_open (struct gl_view *v, long win, int x, int y, int cap_w, int cap_h)
{
	v->surf = armgl_window (win, x, y, cap_w, cap_h);
	v->win = win;
	v->cap_w = cap_w;
	v->cap_h = cap_h;
	v->x = x;
	v->y = y;
	v->w = cap_w;
	v->h = cap_h;
	return v->surf != 0;
}

/* la zone de la vue change (WEV_RESIZE, un panneau ouvert ou fermé) : armgl_window_zone si elle tient dans la
 * surface, sinon une surface neuve à la nouvelle taille ; glViewport suit si la surface est courante -> 1 (0 : pas
 * de surface). Une surface neuve n'est pas courante : armgl_make_current (contexte, v->surf) avant de dessiner. */
static inline int gl_view_place (struct gl_view *v, long win, int x, int y, int w, int h)
{
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	if (!v->surf || w > v->cap_w || h > v->cap_h) {
		int cw = w > v->cap_w ? w : v->cap_w, ch = h > v->cap_h ? h : v->cap_h;
		if (v->surf)
			armgl_window_free (v->surf);
		v->surf = armgl_window (win, x, y, cw, ch);
		v->cap_w = cw;
		v->cap_h = ch;
		if (!v->surf)
			return 0;
	}
	armgl_window_zone (v->surf, win, x, y, w, h);
	v->win = win;
	v->x = x;
	v->y = y;
	v->w = w;
	v->h = h;
	glViewport (0, 0, w, h);
	return 1;
}

/* la surface rendue (elle ne doit plus être courante : armgl_make_current vers une surface hors écran avant) */
static inline void gl_view_close (struct gl_view *v)
{
	if (v->surf)
		armgl_window_free (v->surf);
	v->surf = 0;
}

#endif
