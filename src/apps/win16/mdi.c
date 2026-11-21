/* mdi.c -- win16 : l'interface à plusieurs documents de Windows 3.x (MDI) et le cadre des fenêtres filles.
 *
 * Une fenêtre fille avec barre de titre (les fenêtres MDI, quelques fenêtres flottantes) a son cadre et sa barre
 * dessinés ici, dans la fenêtre de Korin de sa fenêtre principale : titre, boutons Réduire, Agrandir (Restaurer),
 * Fermer ; un clic sur la barre l'active, la barre se tire pour la déplacer, les bords droit et bas pour la
 * redimensionner, un double clic l'agrandit.
 *
 * MDI : la classe « MDIClient » (CLIENTCREATESTRUCT : le menu Fenêtre et le premier identifiant), ses messages
 * WM_MDI* (créer, détruire, activer, agrandir, restaurer, suivante, mosaïque, cascade, réorganiser les icônes,
 * active, menu), DefFrameProc, DefMDIChildProc, TranslateMDISysAccel (Ctrl+F4, Ctrl+F6), CascadeChildWindows,
 * TileChildWindows, et la liste des fenêtres du menu Fenêtre. Différence avec Windows : une fenêtre agrandie
 * garde sa barre de titre (dans la fenêtre cliente MDI) au lieu de passer ses boutons dans la barre de menus ;
 * une fenêtre réduite est une barre de titre courte en bas de la fenêtre cliente.
 *
 * D'après la documentation de Windows 3.1 et mdi.c de Wine (LGPL-2.1) pour le comportement. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w16.h"
#include "be.h"
#include "user.h"
#include "gdi.h"

uint32_t user_def_window_proc (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);
int user_register_system_class (const char *name, native_wndproc fn, uint16_t style, uint16_t brush, int wndextra);

#define WM_NCACTIVATE		0x0086
#define WM_CHILDACTIVATE	0x0022
#define WM_GETMINMAXINFO	0x0024
#define WM_MDICREATE		0x0220
#define WM_MDIDESTROY		0x0221
#define WM_MDIACTIVATE		0x0222
#define WM_MDIRESTORE		0x0223
#define WM_MDINEXT		0x0224
#define WM_MDIMAXIMIZE		0x0225
#define WM_MDITILE		0x0226
#define WM_MDICASCADE		0x0227
#define WM_MDIICONARRANGE	0x0228
#define WM_MDIGETACTIVE		0x0229
#define WM_MDISETMENU		0x0230
#define SC_SIZE			0xf000
#define SC_MOVE			0xf010
#define SC_MINIMIZE		0xf020
#define SC_MAXIMIZE		0xf030
#define SC_NEXTWINDOW		0xf040
#define SC_CLOSE		0xf060
#define SC_RESTORE		0xf120
#define WS_MINIMIZEBOX		0x00020000u
#define WS_MAXIMIZEBOX		0x00010000u
#define WS_CLIPSIBLINGS		0x04000000u
#define CW_USEDEFAULT16		((int16_t) 0x8000)

/* ======================================================================== le cadre des fenêtres filles */
int mdi_nc_has (struct wnd *w)
{
	return w && (w->style & WS_CHILD) && ((w->style & WS_CAPTION) == WS_CAPTION);
}

/* la géométrie du cadre, dans le rectangle de la fenêtre : bord, barre de titre, boutons (de droite à gauche) */
struct ncgeo { int b, cap, cx0, cy0, cx1, cy1, close, max, min, bw; };

static void nc_geo (struct wnd *w, struct ncgeo *g)
{
	g->b = w->cx;					/* (le bord : la zone cliente commence après) */
	g->cap = w->cy - w->cx;				/* (la barre de titre : entre le bord et la zone cliente) */
	if (g->cap < 0)
		g->cap = 0;
	g->cx0 = g->b;
	g->cy0 = g->b;
	g->cx1 = w->w - g->b;
	g->cy1 = g->b + g->cap;
	g->bw = g->cap > 4 ? g->cap - 4 : 0;		/* (un bouton : carré, 2 points de marge) */
	int x = g->cx1 - 2;
	g->close = g->max = g->min = -1;
	if (w->style & WS_SYSMENU)
	{
		x -= g->bw;
		g->close = x;
		x -= 2;
	}
	if (w->style & WS_MAXIMIZEBOX)
	{
		x -= g->bw;
		g->max = x;
		x -= 2;
	}
	if (w->style & WS_MINIMIZEBOX)
	{
		x -= g->bw;
		g->min = x;
	}
}

/* ======================================================================== l'état MDI */
#define MAX_CLIENT 8
#define MAX_KIDS 64
struct mdichild { uint16_t hwnd; int state; int rx, ry, rw, rh; };	/* state : 0 normale, 1 agrandie, 2 réduite */
struct mdiclient
{
	uint16_t hwnd, hmenu_window, idfirst, active;
	int n;
	struct mdichild kids[MAX_KIDS];		/* dans l'ordre de création (les numéros du menu Fenêtre) */
};
static struct mdiclient clients[MAX_CLIENT];

static struct mdiclient *client_of (uint16_t hwnd)
{
	for (int i = 0; i < MAX_CLIENT; i++)
		if (clients[i].hwnd && clients[i].hwnd == hwnd)
			return &clients[i];
	return 0;
}

/* les fenêtres détruites sans WM_MDIDESTROY (DestroyWindow direct) quittent la liste */
static void prune (struct mdiclient *c)
{
	for (int i = 0; i < c->n;)
		if (c->kids[i].hwnd && !wnd_get (c->kids[i].hwnd))	/* (0 : en cours de création) */
		{
			if (c->active == c->kids[i].hwnd)
				c->active = 0;
			memmove (&c->kids[i], &c->kids[i + 1], sizeof c->kids[0] * (size_t) (c->n - i - 1));
			c->n--;
		}
		else
			i++;
}

static struct mdichild *kid_of (struct mdiclient *c, uint16_t hwnd)
{
	for (int i = 0; c && i < c->n; i++)
		if (c->kids[i].hwnd == hwnd)
			return &c->kids[i];
	return 0;
}

/* le client MDI d'une fenêtre fille (son parent), ou 0 */
static struct mdiclient *client_of_child (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	return w ? client_of (w->parent) : 0;
}

int mdi_is_iconic (uint16_t hwnd)
{
	struct mdichild *k = kid_of (client_of_child (hwnd), hwnd);
	return k && k->state == 2;
}

int mdi_is_zoomed (uint16_t hwnd)
{
	struct mdichild *k = kid_of (client_of_child (hwnd), hwnd);
	return k && k->state == 1;
}

/* la fenêtre active d'un client, pour la couleur de la barre de titre */
static int is_active (struct wnd *w)
{
	struct mdiclient *c = client_of (w->parent);
	if (c)
		return c->active == w->hwnd;
	/* (une fille flottante hors MDI : active si elle est devant ses sœurs) */
	return 1;
}

static uint32_t rgb (uint16_t hdc, int sys) { return gdi_ref_to_rgb (hdc, user_sys_color (sys)); }

void mdi_nc_paint (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	if (!mdi_nc_has (w))
		return;
	struct ncgeo g;
	nc_geo (w, &g);
	uint16_t hdc = gdi_dc_for_window_nc (hwnd);
	int act = is_active (w);
	uint32_t frame = rgb (hdc, 6), face = rgb (hdc, 15), cap = act ? rgb (hdc, 2) : rgb (hdc, 4);
	uint32_t ink = act ? rgb (hdc, 9) : rgb (hdc, 18);
	/* le bord : le fond des boutons, un trait de cadre autour */
	gdi_rgb_fill (hdc, 0, 0, w->w, g.b, face);
	gdi_rgb_fill (hdc, 0, w->h - g.b, w->w, w->h, face);
	gdi_rgb_fill (hdc, 0, 0, g.b, w->h, face);
	gdi_rgb_fill (hdc, w->w - g.b, 0, w->w, w->h, face);
	gdi_rgb_frame (hdc, 0, 0, w->w, w->h, frame);
	/* la barre de titre */
	gdi_rgb_fill (hdc, g.cx0, g.cy0, g.cx1, g.cy1, cap);
	gdi_rgb_fill (hdc, g.cx0, g.cy1 - 1, g.cx1, g.cy1, frame);
	int tx1 = g.min >= 0 ? g.min : g.max >= 0 ? g.max : g.close >= 0 ? g.close : g.cx1;
	int th = gdi_text_height (hdc, gdi_stock (13));
	/* (le DC du cadre a son origine au coin de la fenêtre : la découpe est dans ce repère) */
	gdi_dc_clip (hdc, g.cx0, g.cy0, tx1 - 4, g.cy1);
	gdi_text (hdc, g.cx0 + 6, g.cy0 + (g.cap - th) / 2, w->text, -1, ink, gdi_stock (13));
	gdi_dc_clip (hdc, 0, 0, w->w, w->h);
	/* les boutons : un carré au fond des boutons, et son dessin */
	int by = g.cy0 + 2, bs = g.bw;
	int st = 0;
	struct mdichild *k = kid_of (client_of (w->parent), hwnd);
	if (k)
		st = k->state;
	int bx[3] = { g.min, g.max, g.close };
	for (int i = 0; i < 3; i++)
	{
		int x = bx[i];
		if (x < 0 || bs <= 4)
			continue;
		gdi_rgb_fill (hdc, x, by, x + bs, by + bs, face);
		gdi_rgb_frame (hdc, x, by, x + bs, by + bs, frame);
		uint32_t glyph = rgb (hdc, 18);
		int m = bs / 4, x0 = x + m, y0 = by + m, x1 = x + bs - m - 1, y1 = by + bs - m - 1;
		if (i == 0)				/* réduire : un trait en bas */
			gdi_rgb_fill (hdc, x0, y1 - 1, x1 + 1, y1 + 1, glyph);
		else if (i == 1 && st == 0)		/* agrandir : un carré */
		{
			gdi_rgb_frame (hdc, x0, y0, x1 + 1, y1 + 1, glyph);
			gdi_rgb_fill (hdc, x0, y0, x1 + 1, y0 + 2, glyph);
		}
		else if (i == 1)			/* restaurer : deux carrés */
		{
			int d = (x1 - x0) / 3;
			gdi_rgb_frame (hdc, x0 + d, y0, x1 + 1, y1 + 1 - d, glyph);
			gdi_rgb_fill (hdc, x0, y0 + d, x1 + 1 - d, y1 + 1, face);
			gdi_rgb_frame (hdc, x0, y0 + d, x1 + 1 - d, y1 + 1, glyph);
		}
		else					/* fermer : une croix */
		{
			gdi_rgb_line (hdc, x0, y0, x1, y1, glyph);
			gdi_rgb_line (hdc, x0 + 1, y0, x1 + 1, y1, glyph);
			gdi_rgb_line (hdc, x1, y0, x0, y1, glyph);
			gdi_rgb_line (hdc, x1 + 1, y0, x0 + 1, y1, glyph);
		}
	}
	gdi_release_dc (hdc);
}

/* ======================================================================== activer, placer */
static void refresh_menu (struct mdiclient *c)
{
	extern void menu_mdi_list (uint16_t hmenu, uint16_t idfirst, int n, const char *const *titles, int active);
	if (!c->hmenu_window)
		return;
	prune (c);
	const char *titles[MAX_KIDS];
	int act = -1;
	for (int i = 0; i < c->n; i++)
	{
		struct wnd *w = wnd_get (c->kids[i].hwnd);
		titles[i] = w ? w->text : "";
		if (c->kids[i].hwnd == c->active)
			act = i;
		if (w)
			w->id = (uint16_t) (c->idfirst + i);	/* (comme Windows : l'identifiant suit la place dans la liste) */
	}
	menu_mdi_list (c->hmenu_window, c->idfirst, c->n, titles, act);
}

static void activate (struct mdiclient *c, uint16_t hwnd)
{
	prune (c);
	uint16_t old = c->active;
	if (hwnd && !kid_of (c, hwnd))
		return;
	if (old == hwnd)
	{
		if (hwnd)
			user_set_focus (hwnd);
		return;
	}
	c->active = hwnd;
	if (old && wnd_get (old))
	{
		user_send (old, WM_NCACTIVATE, 0, 0);
		user_send (old, WM_MDIACTIVATE, 0, (uint32_t) hwnd | (uint32_t) old << 16);
		mdi_nc_paint (old);
	}
	if (hwnd && wnd_get (hwnd))
	{
		user_raise (hwnd);
		user_invalidate (hwnd, 1);
		user_send (hwnd, WM_NCACTIVATE, 1, 0);
		user_send (hwnd, WM_MDIACTIVATE, 1, (uint32_t) hwnd | (uint32_t) old << 16);
		mdi_nc_paint (hwnd);
		user_set_focus (hwnd);
	}
	refresh_menu (c);
}

/* la suivante dans l'ordre de la liste (WM_MDINEXT, Ctrl+F6), ou la précédente */
static void activate_next (struct mdiclient *c, uint16_t from, int prev)
{
	prune (c);
	if (!c->n)
		return;
	int i = 0;
	for (; i < c->n; i++)
		if (c->kids[i].hwnd == from)
			break;
	if (i == c->n)
		i = 0;
	int j = prev ? (i - 1 + c->n) % c->n : (i + 1) % c->n;
	activate (c, c->kids[j].hwnd);
}

static void client_size (struct mdiclient *c, int *w, int *h)
{
	struct wnd *cw = wnd_get (c->hwnd);
	*w = cw ? cw->cw : 400;
	*h = cw ? cw->ch : 300;
}

static void place (uint16_t hwnd, int x, int y, int w, int h)
{
	struct w16call m = { .a = { hwnd, (uint16_t) x, (uint16_t) y, (uint16_t) w, (uint16_t) h, 1 }, .n = 6 };
	extern void api_MoveWindow16 (struct w16call *);
	api_MoveWindow16 (&m);
	mdi_nc_paint (hwnd);
}

static void maximize (struct mdiclient *c, struct mdichild *k)
{
	struct wnd *w = wnd_get (k->hwnd);
	if (!w)
		return;
	if (k->state == 0)
	{
		k->rx = w->x; k->ry = w->y; k->rw = w->w; k->rh = w->h;
	}
	k->state = 1;
	int cw, ch;
	client_size (c, &cw, &ch);
	place (k->hwnd, 0, 0, cw, ch);
	activate (c, k->hwnd);
}

static void restore (struct mdiclient *c, struct mdichild *k)
{
	if (k->state == 0)
		return;
	k->state = 0;
	place (k->hwnd, k->rx, k->ry, k->rw, k->rh);
	activate (c, k->hwnd);
}

static void minimize (struct mdiclient *c, struct mdichild *k)
{
	struct wnd *w = wnd_get (k->hwnd);
	if (!w || k->state == 2)
		return;
	if (k->state == 0)
	{
		k->rx = w->x; k->ry = w->y; k->rw = w->w; k->rh = w->h;
	}
	k->state = 2;
	/* une barre de titre courte, en bas de la fenêtre cliente, à la suite des autres réduites */
	int cw, ch, n = 0;
	client_size (c, &cw, &ch);
	for (int i = 0; i < c->n; i++)
		if (&c->kids[i] != k && c->kids[i].state == 2)
			n++;
	int bw = 160, bh = w->cy + w->cx;
	int per = cw / bw > 0 ? cw / bw : 1;
	place (k->hwnd, (n % per) * bw, ch - bh * (1 + n / per), bw, bh);
	/* la suivante devient active */
	if (c->active == k->hwnd)
	{
		for (int i = c->n - 1; i >= 0; i--)
			if (c->kids[i].state != 2)
			{
				activate (c, c->kids[i].hwnd);
				return;
			}
	}
}

static void cascade (struct mdiclient *c)
{
	prune (c);
	int cw, ch, step = 22, k = 0;
	client_size (c, &cw, &ch);
	for (int i = 0; i < c->n; i++)
	{
		if (c->kids[i].state == 2)
			continue;
		c->kids[i].state = 0;
		int x = (k * step) % (cw / 2 > step ? cw / 2 : step), y = (k * step) % (ch / 2 > step ? ch / 2 : step);
		place (c->kids[i].hwnd, x, y, cw * 3 / 4, ch * 3 / 4);
		user_raise (c->kids[i].hwnd);
		k++;
	}
	if (c->active)
	{
		user_raise (c->active);
		user_invalidate (c->active, 1);
	}
}

static void tile (struct mdiclient *c, int horizontal)
{
	prune (c);
	int cw, ch, n = 0;
	client_size (c, &cw, &ch);
	for (int i = 0; i < c->n; i++)
		n += c->kids[i].state != 2;
	if (!n)
		return;
	int cols = horizontal ? 1 : 1, rows = 1;
	if (horizontal)
		rows = n;
	else
	{
		while (cols * cols < n)
			cols++;
		rows = (n + cols - 1) / cols;
	}
	int k = 0;
	for (int i = 0; i < c->n; i++)
	{
		if (c->kids[i].state == 2)
			continue;
		c->kids[i].state = 0;
		int r = k / cols, col = k % cols;
		int ncols = (r == rows - 1 && n % cols) ? n % cols : cols;
		int w = cw / ncols, h = ch / rows;
		place (c->kids[i].hwnd, col * w, r * h, w, h);
		k++;
	}
}

/* ======================================================================== la classe MDIClient */
/* CLIENTCREATESTRUCT 16 bits : hWindowMenu, idFirstChild ; MDICREATESTRUCT : szClass, szTitle, hOwner, x, y, cx,
 * cy, style, lParam */
static uint16_t mdi_create (struct mdiclient *c, uint32_t mcs)
{
	uint32_t b = far_lin (mcs);
	char cls[64], title[256];
	uint32_t pcls = rd32 (b), ptitle = rd32 (b + 4);
	if (SELECTOROF (pcls))
		snprintf (cls, sizeof cls, "%s", w16_raw (pcls));
	else
		snprintf (cls, sizeof cls, "#%u", OFFSETOF (pcls));
	snprintf (title, sizeof title, "%s", ptitle ? w16_raw (ptitle) : "");
	uint16_t hinst = rd16 (b + 8);
	int x = (int16_t) rd16 (b + 10), y = (int16_t) rd16 (b + 12), cx = (int16_t) rd16 (b + 14), cy = (int16_t) rd16 (b + 16);
	uint32_t style = rd32 (b + 18);
	int cw, ch;
	client_size (c, &cw, &ch);
	prune (c);
	int k = c->n % 8;
	if (x == CW_USEDEFAULT16 || x < -1000)
	{
		x = k * 22;
		y = k * 22;
	}
	if (cx == CW_USEDEFAULT16 || cx <= 0)
	{
		cx = cw * 3 / 4;
		cy = ch * 3 / 4;
	}
	if (c->n >= MAX_KIDS)
		return 0;
	style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE;
	struct mdichild *kid = &c->kids[c->n++];
	memset (kid, 0, sizeof *kid);
	uint16_t h = user_create (0x40, cls, title, style & ~WS_VISIBLE, x, y, cx, cy, c->hwnd,
		(uint16_t) (c->idfirst + c->n - 1), hinst, mcs);	/* WS_EX_MDICHILD (0x40) ; lpCreateParams : le MDICREATESTRUCT */
	if (!h)
	{
		c->n--;
		return 0;
	}
	kid->hwnd = h;
	struct w16call s = { .a = { h, 1 }, .n = 2 };
	extern void api_ShowWindow16 (struct w16call *);
	api_ShowWindow16 (&s);
	activate (c, h);
	if (style & 0x01000000u)	/* WS_MAXIMIZE */
		maximize (c, kid);
	else if (style & 0x20000000u)	/* WS_MINIMIZE */
		minimize (c, kid);
	refresh_menu (c);
	return h;
}

static void mdi_destroy (struct mdiclient *c, uint16_t hwnd)
{
	struct mdichild *k = kid_of (c, hwnd);
	if (!k)
		return;
	int was = c->active == hwnd;
	if (was)
		c->active = 0;
	extern void api_DestroyWindow16 (struct w16call *);
	struct w16call d = { .a = { hwnd }, .n = 1 };
	api_DestroyWindow16 (&d);
	prune (c);
	if (was && c->n)
	{
		/* la plus devant des restantes */
		int best = -1, bz = -1;
		for (int i = 0; i < c->n; i++)
		{
			struct wnd *w = wnd_get (c->kids[i].hwnd);
			if (w && w->z > bz)
			{
				bz = w->z;
				best = i;
			}
		}
		if (best >= 0)
			activate (c, c->kids[best].hwnd);
	}
	refresh_menu (c);
	user_invalidate (c->hwnd, 1);
}

static uint32_t mdiclient_proc (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct mdiclient *c = client_of (hwnd);
	switch (msg)
	{
	case WM_CREATE:
	{
		for (int i = 0; i < MAX_CLIENT && !c; i++)
			if (!clients[i].hwnd || !wnd_get (clients[i].hwnd))
			{
				c = &clients[i];
				memset (c, 0, sizeof *c);
				c->hwnd = hwnd;
			}
		if (!c)
			return (uint32_t) -1;
		uint32_t ccs = lp ? rd32 (far_lin (lp)) : 0;	/* CREATESTRUCT.lpCreateParams */
		if (ccs)
		{
			c->hmenu_window = rd16 (far_lin (ccs));
			c->idfirst = rd16 (far_lin (ccs) + 2);
		}
		return 0;
	}
	case WM_DESTROY:
		if (c)
			c->hwnd = 0;
		break;
	case WM_MDICREATE:
		return c && lp ? mdi_create (c, lp) : 0;
	case WM_MDIDESTROY:
		if (c)
			mdi_destroy (c, wp);
		return 0;
	case WM_MDIACTIVATE:
		if (c)
		{
			struct mdichild *k = kid_of (c, wp);
			if (k && k->state == 2)
				restore (c, k);
			activate (c, wp);
		}
		return 0;
	case WM_MDIRESTORE:
		if (c && kid_of (c, wp))
			restore (c, kid_of (c, wp));
		return 0;
	case WM_MDIMAXIMIZE:
		if (c && kid_of (c, wp))
			maximize (c, kid_of (c, wp));
		return 0;
	case WM_MDINEXT:
		if (c)
			activate_next (c, wp ? wp : c->active, lp != 0);
		return 0;
	case WM_MDITILE:
		if (c)
			tile (c, (wp & 1) != 0);	/* MDITILE_HORIZONTAL */
		return 1;
	case WM_MDICASCADE:
		if (c)
			cascade (c);
		return 1;
	case WM_MDIICONARRANGE:
		if (c)
		{
			for (int i = 0; i < c->n; i++)
				if (c->kids[i].state == 2)
				{
					c->kids[i].state = 0;
					minimize (c, &c->kids[i]);
				}
		}
		return 0;
	case WM_MDIGETACTIVE:
	{
		if (!c)
			return 0;
		prune (c);
		struct mdichild *k = kid_of (c, c->active);
		return (uint32_t) c->active | (uint32_t) (k && k->state == 1) << 16;
	}
	case WM_MDISETMENU:
	{
		if (!c)
			return 0;
		struct wnd *frame = wnd_get (wnd_get (hwnd) ? wnd_get (hwnd)->parent : 0);
		uint16_t old = frame ? frame->menu : 0;
		if (!wp)
		{
			uint16_t mf = (uint16_t) lp, mw = (uint16_t) (lp >> 16);
			if (mf && frame)
			{
				extern void api_SetMenu16 (struct w16call *);
				struct w16call s = { .a = { frame->hwnd, mf }, .n = 2 };
				api_SetMenu16 (&s);
			}
			if (mw)
			{
				if (c->hmenu_window && c->hmenu_window != mw)
				{
					extern void menu_mdi_list (uint16_t, uint16_t, int, const char *const *, int);
					menu_mdi_list (c->hmenu_window, c->idfirst, 0, 0, -1);
				}
				c->hmenu_window = mw;
			}
		}
		refresh_menu (c);
		return old;
	}
	case WM_SIZE:
		if (c)
		{
			/* la fenêtre agrandie suit la taille du client */
			prune (c);
			for (int i = 0; i < c->n; i++)
				if (c->kids[i].state == 1)
					place (c->kids[i].hwnd, 0, 0, (int16_t) (lp & 0xffff), (int16_t) (lp >> 16));
		}
		break;
	case WM_SETFOCUS:
		if (c && c->active && wnd_get (c->active))
			user_set_focus (c->active);
		return 0;
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

void mdi_register (void)
{
	/* fond : COLOR_APPWORKSPACE + 1 */
	user_register_system_class ("MDIClient", mdiclient_proc, 0, 13, 0);
}

/* ======================================================================== les fonctions de l'API */
/* DefFrameProc (hwnd, hwndMDIClient, msg, wParam, lParam) */
void api_DefFrameProc16 (struct w16call *c)
{
	uint16_t hwnd = (uint16_t) c->a[0], hc = (uint16_t) c->a[1], msg = (uint16_t) c->a[2], wp = (uint16_t) c->a[3];
	uint32_t lp = c->a[4];
	struct mdiclient *mc = hc ? client_of (hc) : 0;
	switch (msg)
	{
	case WM_COMMAND:
		if (mc)
		{
			prune (mc);
			if (wp >= mc->idfirst && wp < mc->idfirst + mc->n)
			{
				struct mdichild *k = &mc->kids[wp - mc->idfirst];
				if (k->state == 2)
					restore (mc, k);
				activate (mc, k->hwnd);
				c->ret = 0;
				return;
			}
			/* (une commande du menu système de la fenêtre agrandie) */
			if (wp >= 0xf000 && mc->active)
			{
				c->ret = user_send (mc->active, WM_SYSCOMMAND, wp, lp);
				return;
			}
		}
		c->ret = 0;
		return;
	case WM_SIZE:
		if (hc && wnd_get (hc))
			place (hc, 0, 0, (int16_t) (lp & 0xffff), (int16_t) (lp >> 16));
		c->ret = 0;
		return;
	case WM_SETFOCUS:
		if (hc && wnd_get (hc))
			user_set_focus (hc);
		c->ret = 0;
		return;
	}
	c->ret = user_def_window_proc (hwnd, msg, wp, lp);
}

/* DefMDIChildProc (hwnd, msg, wParam, lParam) */
void api_DefMDIChildProc16 (struct w16call *c)
{
	uint16_t hwnd = (uint16_t) c->a[0], msg = (uint16_t) c->a[1], wp = (uint16_t) c->a[2];
	uint32_t lp = c->a[3];
	struct mdiclient *mc = client_of_child (hwnd);
	struct mdichild *k = kid_of (mc, hwnd);
	c->ret = 0;
	switch (msg)
	{
	case WM_CLOSE:
		if (mc)
		{
			user_send (mc->hwnd, WM_MDIDESTROY, hwnd, 0);
			return;
		}
		break;
	case WM_SYSCOMMAND:
		if (mc && k)
			switch (wp & 0xfff0)
			{
			case SC_MAXIMIZE: maximize (mc, k); return;
			case SC_RESTORE: restore (mc, k); return;
			case SC_MINIMIZE: minimize (mc, k); return;
			case SC_NEXTWINDOW: activate_next (mc, hwnd, 0); return;
			}
		break;
	case WM_CHILDACTIVATE:
		if (mc)
			activate (mc, hwnd);
		return;
	case WM_SETFOCUS:
		if (mc && mc->active != hwnd)
			activate (mc, hwnd);
		break;
	case WM_SETTEXT:
		c->ret = user_def_window_proc (hwnd, msg, wp, lp);
		if (mc)
			refresh_menu (mc);
		mdi_nc_paint (hwnd);
		return;
	case WM_GETMINMAXINFO:
		return;
	}
	c->ret = user_def_window_proc (hwnd, msg, wp, lp);
}

/* TranslateMDISysAccel (hwndClient, MSG) : Ctrl+F4 ferme la fenêtre active, Ctrl+F6 passe à la suivante ;
 * MSG 16 bits : hwnd, message, wParam, lParam */
void api_TranslateMDISysAccel16 (struct w16call *c)
{
	struct mdiclient *mc = client_of ((uint16_t) c->a[0]);
	c->ret = 0;
	if (!mc || !c->a[1])
		return;
	uint32_t m = far_lin (c->a[1]);
	uint16_t msg = rd16 (m + 2), vk = rd16 (m + 4);
	if ((msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) || !user_key_down (0x11))
		return;
	prune (mc);
	if (vk == 0x73 && mc->active)	/* VK_F4 */
	{
		user_post (mc->active, WM_SYSCOMMAND, SC_CLOSE, 0);
		c->ret = 1;
	}
	else if (vk == 0x75)		/* VK_F6 */
	{
		activate_next (mc, mc->active, user_key_down (0x10) != 0);
		c->ret = 1;
	}
}

/* CascadeChildWindows, TileChildWindows (parent, action) : comme WM_MDICASCADE, WM_MDITILE */
void api_CascadeChildWindows16 (struct w16call *c) { c->ret = user_send ((uint16_t) c->a[0], WM_MDICASCADE, (uint16_t) c->a[1], 0); }
void api_TileChildWindows16 (struct w16call *c) { c->ret = user_send ((uint16_t) c->a[0], WM_MDITILE, (uint16_t) c->a[1], 0); }
void api_ArrangeIconicWindows16 (struct w16call *c) { c->ret = user_send ((uint16_t) c->a[0], WM_MDIICONARRANGE, 0, 0); }

/* ======================================================================== la souris sur le cadre */
static struct { uint16_t hwnd; int mode, sx, sy, x0, y0, w0, h0; } drag;	/* mode : 1 déplacer, 2 taille */

int mdi_nc_dragging (void) { return drag.hwnd != 0; }

/* un événement de la souris pendant un glisser (sx, sy : coordonnées de la fenêtre de Korin) */
void mdi_nc_drag (int type, int sx, int sy)
{
	struct wnd *w = wnd_get (drag.hwnd);
	if (!w)
	{
		drag.hwnd = 0;
		return;
	}
	if (type == BE_MOUSE_UP)
	{
		drag.hwnd = 0;
		return;
	}
	int dx = sx - drag.sx, dy = sy - drag.sy;
	if (drag.mode == 1)
		place (w->hwnd, drag.x0 + dx, drag.y0 + dy, drag.w0, drag.h0);
	else
	{
		int nw = drag.w0 + dx, nh = drag.h0 + dy;
		if (nw < 100) nw = 100;
		if (nh < w->cy + w->cx + 20) nh = w->cy + w->cx + 20;
		place (w->hwnd, drag.x0, drag.y0, nw, nh);
	}
}

/* un clic dans le cadre d'une fille (wx, wy : dans le rectangle de la fenêtre ; sx, sy : dans la fenêtre de
 * Korin) -> 1 si pris */
int mdi_nc_mouse (uint16_t hwnd, int type, int dblclick, int wx, int wy, int sx, int sy)
{
	struct wnd *w = wnd_get (hwnd);
	if (!mdi_nc_has (w) || type != BE_MOUSE_DOWN)
		return mdi_nc_has (w);
	struct mdiclient *mc = client_of (w->parent);
	struct mdichild *k = kid_of (mc, hwnd);
	if (mc)
		activate (mc, hwnd);
	else
	{
		user_raise (hwnd);
		user_invalidate (hwnd, 1);
		mdi_nc_paint (hwnd);
	}
	w = wnd_get (hwnd);
	if (!w)
		return 1;
	struct ncgeo g;
	nc_geo (w, &g);
	int inbar = wx >= g.cx0 && wx < g.cx1 && wy >= g.cy0 && wy < g.cy1;
	int by = g.cy0 + 2;
	if (inbar && wy >= by && wy < by + g.bw)
	{
		if (g.close >= 0 && wx >= g.close && wx < g.close + g.bw)
		{
			user_post (hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
			return 1;
		}
		if (g.max >= 0 && wx >= g.max && wx < g.max + g.bw)
		{
			user_post (hwnd, WM_SYSCOMMAND, k && k->state != 0 ? SC_RESTORE : SC_MAXIMIZE, 0);
			return 1;
		}
		if (g.min >= 0 && wx >= g.min && wx < g.min + g.bw)
		{
			user_post (hwnd, WM_SYSCOMMAND, k && k->state == 2 ? SC_RESTORE : SC_MINIMIZE, 0);
			return 1;
		}
	}
	if (inbar && dblclick && (w->style & WS_MAXIMIZEBOX))
	{
		user_post (hwnd, WM_SYSCOMMAND, k && k->state != 0 ? SC_RESTORE : SC_MAXIMIZE, 0);
		return 1;
	}
	if (k && k->state == 1)
		return 1;		/* (agrandie : ni déplacer ni retailler) */
	memset (&drag, 0, sizeof drag);
	drag.sx = sx;
	drag.sy = sy;
	drag.x0 = w->x;
	drag.y0 = w->y;
	drag.w0 = w->w;
	drag.h0 = w->h;
	if (inbar)
		drag.mode = 1;
	else if ((w->style & WS_THICKFRAME) && (wx >= w->w - g.b - 2 || wy >= w->h - g.b - 2) && !(k && k->state == 2))
		drag.mode = 2;
	if (drag.mode)
		drag.hwnd = hwnd;
	return 1;
}

/* un clic dans une fenêtre : la fenêtre MDI qui la contient (ou elle-même) devient active ; une fille flottante
 * avec barre de titre passe devant */
void mdi_click (uint16_t hwnd)
{
	for (struct wnd *w = wnd_get (hwnd); w && (w->style & WS_CHILD); w = wnd_get (w->parent))
	{
		struct mdiclient *c = client_of (w->parent);
		if (c)
		{
			if (c->active != w->hwnd)
				activate (c, w->hwnd);
			return;
		}
	}
}
