/* user.c -- win16 : USER (classes, fenêtres, messages), écrit sur l'affichage de Korin (be.h).
 *
 * winevdm passe chaque appel de USER à user32 de Windows ; ici USER tient lui-même l'arbre des fenêtres : une
 * fenêtre principale est une fenêtre de Korin (be_window_open), les fenêtres filles sont dessinées dedans. La
 * sémantique suit Windows 3.1 et le code de Wine (user/, LGPL-2.1+). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w16.h"
#include "be.h"
#include "user.h"

/* ---- messages ---- */
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_SETTEXT 0x000c
#define WM_GETTEXT 0x000d
#define WM_GETTEXTLENGTH 0x000e
#define WM_PAINT 0x000f
#define WM_CLOSE 0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define SC_CLOSE 0xf060

#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define WS_CAPTION 0x00c00000u
#define WS_BORDER 0x00800000u
#define WS_THICKFRAME 0x00040000u
#define WS_POPUP 0x80000000u
#define CW_USEDEFAULT16 ((int16_t) 0x8000)

/* ---- classes ---- */
#define MAX_CLS 64
struct cls
{
	int used;
	char name[64];
	uint16_t atom, style, hinst, hicon, hcursor, hbrush, clsextra, wndextra;
	uint32_t proc, menuname;
};
static struct cls classes[MAX_CLS];
static uint16_t next_atom = 0xc000;

/* ---- fenêtres ---- */
#define MAX_WND 512
struct wnd
{
	int used;
	uint16_t hwnd, parent, owner, id, hinst;
	int cls;
	uint32_t proc, style, exstyle;
	int x, y, w, h;		/* le rectangle de la fenêtre, dans la zone cliente du parent (écran pour une principale) */
	int cx, cy, cw, ch;	/* la zone cliente, dans le rectangle de la fenêtre */
	char text[256];		/* le titre (CP1252, comme le programme) */
	long be;
	int visible, destroyed;
	int invalid, erase, ix0, iy0, ix1, iy1;	/* la zone à repeindre (zone cliente) */
	uint8_t extra[64];
	uint16_t props[4];
};
static struct wnd wnds[MAX_WND];
#define HWND_OF(i) ((uint16_t) (0x1000 + (i) * 4))

struct wnd *wnd_get (uint16_t h)
{
	int i = (h - 0x1000) / 4;
	if (h < 0x1000 || (h & 3) || i >= MAX_WND || !wnds[i].used)
		return 0;
	return &wnds[i];
}

static struct wnd *wnd_top (struct wnd *w)
{
	while (w && w->parent && (w->style & WS_CHILD))
		w = wnd_get (w->parent);
	return w;
}

int wnd_client_geometry (uint16_t hwnd, int *ox, int *oy, int *cw, int *ch, long *be)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	*cw = w->cw;
	*ch = w->ch;
	int x = 0, y = 0;
	struct wnd *p = w;
	while (p->parent && (p->style & WS_CHILD))
	{
		x += p->x + p->cx;
		y += p->y + p->cy;
		p = wnd_get (p->parent);
		if (!p)
			return 0;
	}
	*ox = x;
	*oy = y;
	*be = p->be;
	return p->be != 0;
}

/* ---- couleurs du système (GetSysColor) : celles de Windows 3.1, sauf les fonds et le texte, pris au thème ---- */
static uint32_t rgb_to_ref (uint32_t rgb) { return ((rgb >> 16) & 0xff) | (rgb & 0xff00) | (rgb & 0xff) << 16; }

uint32_t user_sys_color (int i)
{
	switch (i)
	{
	case 5:		/* COLOR_WINDOW */
		return 0xffffff;
	case 8:		/* COLOR_WINDOWTEXT */
	case 18:	/* COLOR_BTNTEXT */
		return 0x000000;
	case 15:	/* COLOR_BTNFACE */
	case 4:		/* COLOR_MENU */
		return 0xc0c0c0;
	case 16:	/* COLOR_BTNSHADOW */
		return 0x808080;
	case 20:	/* COLOR_BTNHIGHLIGHT */
		return 0xffffff;
	case 13:	/* COLOR_HIGHLIGHT */
		return rgb_to_ref (be_theme (3));
	case 14:	/* COLOR_HIGHLIGHTTEXT */
		return 0xffffff;
	case 1:		/* COLOR_BACKGROUND */
	case 12:	/* COLOR_APPWORKSPACE */
		return rgb_to_ref (be_theme (0));
	case 2:
	case 3:		/* légende active ou non */
		return rgb_to_ref (be_theme (3));
	case 6:		/* COLOR_WINDOWFRAME */
		return 0x000000;
	case 7:		/* COLOR_MENUTEXT */
	case 9:		/* COLOR_CAPTIONTEXT */
		return 0x000000;
	case 17:	/* COLOR_GRAYTEXT */
		return 0x808080;
	default:
		return 0xc0c0c0;
	}
}

/* ---- la file des messages ---- */
struct qmsg { uint16_t hwnd, msg, wp; uint32_t lp, time; int16_t x, y; };
#define QMAX 256
static struct qmsg queue[QMAX];
static int qhead, qtail;
static int quit_posted, quit_code;
static uint16_t focus_hwnd, capture_hwnd;
static int mouse_x, mouse_y, mouse_buttons;
static uint8_t key_char[256];

static int post (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	int n = (qtail + 1) % QMAX;
	if (n == qhead)
		return 0;
	queue[qtail].hwnd = hwnd;
	queue[qtail].msg = msg;
	queue[qtail].wp = wp;
	queue[qtail].lp = lp;
	queue[qtail].time = be_ticks ();
	queue[qtail].x = (int16_t) mouse_x;
	queue[qtail].y = (int16_t) mouse_y;
	qtail = n;
	return 1;
}

uint32_t user_send (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w || !w->proc)
		return 0;
	uint16_t args[5] = { hwnd, msg, wp, (uint16_t) (lp >> 16), (uint16_t) lp };
	w16_log ("   message %04x à %04x (%04x, %08x)\n", msg, hwnd, wp, lp);
	return call16 (w->proc, args, 5, w->hinst ? w->hinst : task.hinstance);
}

static void invalidate (struct wnd *w, int x0, int y0, int x1, int y1, int erase)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w->cw) x1 = w->cw;
	if (y1 > w->ch) y1 = w->ch;
	if (x1 <= x0 || y1 <= y0)
		return;
	if (!w->invalid)
	{
		w->ix0 = x0; w->iy0 = y0; w->ix1 = x1; w->iy1 = y1;
	}
	else
	{
		if (x0 < w->ix0) w->ix0 = x0;
		if (y0 < w->iy0) w->iy0 = y0;
		if (x1 > w->ix1) w->ix1 = x1;
		if (y1 > w->iy1) w->iy1 = y1;
	}
	w->invalid = 1;
	if (erase)
		w->erase = 1;
}

static void invalidate_tree (struct wnd *w)
{
	invalidate (w, 0, 0, w->cw, w->ch, 1);
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && wnds[i].parent == w->hwnd && (wnds[i].style & WS_CHILD) && wnds[i].visible)
			invalidate_tree (&wnds[i]);
}

/* ---- rangement 16 bits ---- */
static void put_rect (uint32_t p, int l, int t, int r, int b)
{
	uint32_t a = far_lin (p);
	wr16 (a, (uint16_t) l);
	wr16 (a + 2, (uint16_t) t);
	wr16 (a + 4, (uint16_t) r);
	wr16 (a + 6, (uint16_t) b);
}

/* ---- l'API ---- */
void api_InitApp16 (struct w16call *c) { c->ret = 1; }

void api_MessageBox16 (struct w16call *c)
{
	char text[1024], title[256];
	snprintf (text, sizeof text, "%s", w16_str (c->a[1]));
	snprintf (title, sizeof title, "%s", c->a[2] ? w16_str (c->a[2]) : "Erreur");
	c->ret = (uint32_t) be_message (title, text, (int) c->a[3]);
}

void api_RegisterClass16 (struct w16call *c)
{
	uint32_t a = far_lin (c->a[0]);
	struct cls *k = 0;
	for (int i = 0; i < MAX_CLS && !k; i++)
		if (!classes[i].used)
			k = &classes[i];
	if (!k)
	{
		c->ret = 0;
		return;
	}
	memset (k, 0, sizeof *k);
	k->used = 1;
	k->style = rd16 (a);
	k->proc = rd32 (a + 2);
	k->clsextra = rd16 (a + 6);
	k->wndextra = rd16 (a + 8);
	k->hinst = rd16 (a + 10);
	k->hicon = rd16 (a + 12);
	k->hcursor = rd16 (a + 14);
	k->hbrush = rd16 (a + 16);
	k->menuname = rd32 (a + 18);
	uint32_t name = rd32 (a + 22);
	snprintf (k->name, sizeof k->name, "%s", SELECTOROF (name) ? (const char *) far_ptr (name) : "");
	k->atom = next_atom++;
	w16_log ("classe « %s » : procédure %08x\n", k->name, k->proc);
	c->ret = k->atom;
}

static int find_class (uint32_t name)
{
	for (int i = 0; i < MAX_CLS; i++)
	{
		if (!classes[i].used)
			continue;
		if (!SELECTOROF (name) ? classes[i].atom == OFFSETOF (name) : !strcasecmp (classes[i].name, (const char *) far_ptr (name)))
			return i;
	}
	return -1;
}

static void wnd_geometry (struct wnd *w)
{
	/* la zone cliente d'après le style, comme Windows 3.1 */
	int b = 0, cap = 0;
	if (w->style & WS_THICKFRAME)
		b = W16_FRAME;
	else if (w->style & (WS_BORDER | WS_CAPTION))
		b = 1;
	if ((w->style & WS_CAPTION) == WS_CAPTION)
		cap = W16_CAPTION - 1;
	w->cx = b;
	w->cy = b + cap;
	w->cw = w->w - 2 * b;
	w->ch = w->h - 2 * b - cap;
	if (w->cw < 0) w->cw = 0;
	if (w->ch < 0) w->ch = 0;
}

void api_CreateWindow16 (struct w16call *c)
{
	uint32_t cname = c->a[0], wname = c->a[1], style = c->a[2];
	int x = (int16_t) c->a[3], y = (int16_t) c->a[4], cw = (int16_t) c->a[5], ch = (int16_t) c->a[6];
	uint16_t parent = (uint16_t) c->a[7], menu = (uint16_t) c->a[8], hinst = (uint16_t) c->a[9];
	uint32_t param = c->a[10];
	int k = find_class (cname);
	if (k < 0)
	{
		w16_log ("CreateWindow : classe « %s » inconnue\n", w16_str (cname));
		c->ret = 0;
		return;
	}
	int i;
	for (i = 0; i < MAX_WND && wnds[i].used; i++)
		;
	if (i == MAX_WND)
	{
		c->ret = 0;
		return;
	}
	struct wnd *w = &wnds[i];
	memset (w, 0, sizeof *w);
	w->used = 1;
	w->hwnd = HWND_OF (i);
	w->cls = k;
	w->proc = classes[k].proc;
	w->style = style & ~WS_VISIBLE;
	w->hinst = hinst ? hinst : task.hinstance;
	if (style & WS_CHILD)
	{
		w->parent = parent;
		w->id = menu;
	}
	else
		w->owner = parent;
	if (x == CW_USEDEFAULT16)
		x = y = -1;
	if (cw == CW_USEDEFAULT16)
	{
		cw = 480;
		ch = 320;
	}
	w->x = x;
	w->y = y;
	w->w = cw;
	w->h = ch;
	wnd_geometry (w);
	if (wname && SELECTOROF (wname))
		snprintf (w->text, sizeof w->text, "%s", (const char *) far_ptr (wname));
	/* CREATESTRUCT 16 bits, dans un bloc à part le temps des messages */
	uint16_t cs_sel = global_alloc (64, 0);
	uint32_t cs = sel_base (cs_sel);
	wr32 (cs, param);
	wr16 (cs + 4, w->hinst);
	wr16 (cs + 6, menu);
	wr16 (cs + 8, parent);
	wr16 (cs + 10, (uint16_t) ch);
	wr16 (cs + 12, (uint16_t) cw);
	wr16 (cs + 14, (uint16_t) y);
	wr16 (cs + 16, (uint16_t) x);
	wr32 (cs + 18, style);
	wr32 (cs + 22, wname);
	wr32 (cs + 26, cname);
	wr32 (cs + 30, 0);
	uint16_t hwnd = w->hwnd;
	if (!user_send (hwnd, WM_NCCREATE, 0, MAKESEGPTR (cs_sel, 0)))
	{
		global_free (cs_sel);
		w->used = 0;
		c->ret = 0;
		return;
	}
	if ((int16_t) user_send (hwnd, WM_CREATE, 0, MAKESEGPTR (cs_sel, 0)) == -1)
	{
		global_free (cs_sel);
		w->used = 0;
		c->ret = 0;
		return;
	}
	global_free (cs_sel);
	w = wnd_get (hwnd);
	if (!w)
	{
		c->ret = 0;
		return;
	}
	user_send (hwnd, WM_SIZE, 0, (uint32_t) w->cw | (uint32_t) w->ch << 16);
	user_send (hwnd, WM_MOVE, 0, (uint32_t) (uint16_t) (w->x < 0 ? 0 : w->x) | (uint32_t) (uint16_t) (w->y < 0 ? 0 : w->y) << 16);
	if (style & WS_VISIBLE)
	{
		struct w16call s = { .a = { hwnd, 1 }, .n = 2 };
		extern void api_ShowWindow16 (struct w16call *);
		api_ShowWindow16 (&s);
	}
	w16_log ("fenêtre %04x « %s » : %d×%d\n", hwnd, w->text, w->cw, w->ch);
	c->ret = hwnd;
}

void api_ShowWindow16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int cmd = (int) c->a[1];
	if (!w)
	{
		c->ret = 0;
		return;
	}
	c->ret = (uint32_t) w->visible;
	int show = cmd != 0;
	if (show == w->visible)
		return;
	w->visible = show;
	if (!(w->style & WS_CHILD))
	{
		if (show && !w->be)
		{
			char title[512];
			extern int w16_to_utf8 (const uint8_t *, int, char *, int);
			w16_to_utf8 ((const uint8_t *) w->text, -1, title, sizeof title);
			w->be = be_window_open (w->x, w->y, w->cw, w->ch, title);
			if (!w->be)
				w16_fatal ("fenêtre de Korin refusée");
			focus_hwnd = w->hwnd;
		}
		else if (!show && w->be)
		{
			be_window_close (w->be);
			w->be = 0;
		}
	}
	user_send (w->hwnd, WM_SHOWWINDOW, (uint16_t) show, 0);
	if (show)
	{
		w = wnd_get ((uint16_t) c->a[0]);
		if (w)
			invalidate_tree (w);
	}
}

void api_UpdateWindow16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (w && w->invalid && w->visible)
		user_send (w->hwnd, WM_PAINT, 0, 0);
	c->ret = 1;
}

void api_InvalidateRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
		return;
	if (c->a[1])
	{
		uint32_t r = far_lin (c->a[1]);
		invalidate (w, (int16_t) rd16 (r), (int16_t) rd16 (r + 2), (int16_t) rd16 (r + 4), (int16_t) rd16 (r + 6), (int) c->a[2]);
	}
	else
		invalidate (w, 0, 0, w->cw, w->ch, (int) c->a[2]);
}

void api_ValidateRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (w)
		w->invalid = w->erase = 0;
}

void api_GetClientRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	put_rect (c->a[1], 0, 0, w ? w->cw : 0, w ? w->ch : 0);
}

void api_GetWindowRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
		return;
	int ox = 0, oy = 0, cw, ch;
	long be;
	if (w->style & WS_CHILD)
		wnd_client_geometry (w->parent, &ox, &oy, &cw, &ch, &be);
	int x = w->x < 0 ? 0 : w->x, y = w->y < 0 ? 0 : w->y;
	put_rect (c->a[1], ox + x, oy + y, ox + x + w->w, oy + y + w->h);
}

void api_BeginPaint16 (struct w16call *c)
{
	uint16_t hwnd = (uint16_t) c->a[0];
	struct wnd *w = wnd_get (hwnd);
	if (!w)
	{
		c->ret = 0;
		return;
	}
	uint16_t hdc = gdi_dc_for_window (hwnd);
	int x0 = w->invalid ? w->ix0 : 0, y0 = w->invalid ? w->iy0 : 0;
	int x1 = w->invalid ? w->ix1 : 0, y1 = w->invalid ? w->iy1 : 0;
	int erase = w->erase;
	w->invalid = w->erase = 0;
	int ferase = 0;
	if (erase)
		ferase = !user_send (hwnd, WM_ERASEBKGND, hdc, 0);
	uint32_t ps = far_lin (c->a[1]);
	wr16 (ps, hdc);
	wr16 (ps + 2, (uint16_t) ferase);
	put_rect (c->a[1] + 4, x0, y0, x1, y1);
	wr16 (ps + 12, 0);
	wr16 (ps + 14, 0);
	memset (lin_ptr (ps + 16), 0, 16);
	c->ret = hdc;
}

void api_EndPaint16 (struct w16call *c)
{
	uint32_t ps = far_lin (c->a[1]);
	gdi_release_dc (rd16 (ps));
	c->ret = 1;
}

void api_GetDC16 (struct w16call *c)
{
	c->ret = gdi_dc_for_window ((uint16_t) c->a[0]);
}

void api_ReleaseDC16 (struct w16call *c)
{
	gdi_release_dc ((uint16_t) c->a[1]);
	c->ret = 1;
}

static void destroy (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w || w->destroyed)
		return;
	w->destroyed = 1;
	user_send (hwnd, WM_DESTROY, 0, 0);
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && (wnds[i].parent == hwnd || wnds[i].owner == hwnd))
			destroy (wnds[i].hwnd);
	user_send (hwnd, WM_NCDESTROY, 0, 0);
	w = wnd_get (hwnd);
	if (!w)
		return;
	if (w->be)
		be_window_close (w->be);
	if (focus_hwnd == hwnd)
		focus_hwnd = 0;
	if (capture_hwnd == hwnd)
		capture_hwnd = 0;
	w->used = 0;
}

void api_DestroyWindow16 (struct w16call *c)
{
	destroy ((uint16_t) c->a[0]);
	c->ret = 1;
}

void api_PostQuitMessage16 (struct w16call *c)
{
	quit_posted = 1;
	quit_code = (int16_t) c->a[0];
}

void api_PostMessage16 (struct w16call *c)
{
	c->ret = (uint32_t) post ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
}

void api_SendMessage16 (struct w16call *c)
{
	c->ret = user_send ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
}

void api_DefWindowProc16 (struct w16call *c)
{
	uint16_t hwnd = (uint16_t) c->a[0], msg = (uint16_t) c->a[1], wp = (uint16_t) c->a[2];
	uint32_t lp = c->a[3];
	struct wnd *w = wnd_get (hwnd);
	c->ret = 0;
	if (!w)
		return;
	switch (msg)
	{
	case WM_NCCREATE:
		c->ret = 1;
		break;
	case WM_CLOSE:
		destroy (hwnd);
		break;
	case WM_SYSCOMMAND:
		if ((wp & 0xfff0) == SC_CLOSE)
			user_send (hwnd, WM_CLOSE, 0, 0);
		break;
	case WM_ERASEBKGND:
	{
		uint16_t br = classes[w->cls].hbrush;
		if (br)
			gdi_fill_rect_brush (wp, 0, 0, w->cw, w->ch, br);
		c->ret = 1;
		break;
	}
	case WM_PAINT:
	{
		w->invalid = w->erase = 0;
		break;
	}
	case WM_SETTEXT:
		snprintf (w->text, sizeof w->text, "%s", lp ? (const char *) far_ptr (lp) : "");
		if (w->be)
		{
			char title[512];
			extern int w16_to_utf8 (const uint8_t *, int, char *, int);
			w16_to_utf8 ((const uint8_t *) w->text, -1, title, sizeof title);
			be_window_title (w->be, title);
		}
		c->ret = 1;
		break;
	case WM_GETTEXT:
	{
		int n = (int) strlen (w->text);
		if (n > wp - 1)
			n = wp - 1;
		if (n < 0)
			n = 0;
		if (wp)
		{
			memcpy (far_ptr (lp), w->text, (size_t) n);
			((char *) far_ptr (lp))[n] = 0;
		}
		c->ret = (uint32_t) n;
		break;
	}
	case WM_GETTEXTLENGTH:
		c->ret = (uint32_t) strlen (w->text);
		break;
	case WM_QUERYENDSESSION:
		c->ret = 1;
		break;
	}
}

/* ---- la boucle des messages ---- */
static uint16_t hid_to_vk (int key, int ch)
{
	if (key >= 0x04 && key <= 0x1d)
		return (uint16_t) ('A' + key - 0x04);
	if (key >= 0x1e && key <= 0x26)
		return (uint16_t) ('1' + key - 0x1e);
	if (key == 0x27)
		return '0';
	if (key >= 0x3a && key <= 0x45)
		return (uint16_t) (0x70 + key - 0x3a);	/* F1..F12 */
	switch (key)
	{
	case 0x28: return 0x0d;
	case 0x29: return 0x1b;
	case 0x2a: return 0x08;
	case 0x2b: return 0x09;
	case 0x2c: return 0x20;
	case 0x49: return 0x2d;	/* Inser */
	case 0x4a: return 0x24;
	case 0x4b: return 0x21;
	case 0x4c: return 0x2e;
	case 0x4d: return 0x23;
	case 0x4e: return 0x22;
	case 0x4f: return 0x27;
	case 0x50: return 0x25;
	case 0x51: return 0x28;
	case 0x52: return 0x26;
	case 0xe0: case 0xe4: return 0x11;
	case 0xe1: case 0xe5: return 0x10;
	case 0xe2: case 0xe6: return 0x12;
	}
	return (uint16_t) (ch > 0 && ch < 0x80 ? ch : 0);
}

/* la fenêtre la plus profonde sous un point de la fenêtre de Korin (coordonnées de la zone cliente principale) */
static struct wnd *hit (struct wnd *top, int x, int y, int *lx, int *ly)
{
	struct wnd *best = top;
	*lx = x;
	*ly = y;
	for (int again = 1; again;)
	{
		again = 0;
		for (int i = MAX_WND - 1; i >= 0; i--)
		{
			struct wnd *k = &wnds[i];
			if (!k->used || !k->visible || k->parent != best->hwnd || !(k->style & WS_CHILD))
				continue;
			if (*lx >= k->x && *lx < k->x + k->w && *ly >= k->y && *ly < k->y + k->h)
			{
				*lx -= k->x + k->cx;
				*ly -= k->y + k->cy;
				best = k;
				again = 1;
				break;
			}
		}
	}
	return best;
}

static struct wnd *by_be (long be)
{
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && wnds[i].be == be)
			return &wnds[i];
	return 0;
}

static void translate_event (const struct be_event *e)
{
	struct wnd *top = by_be (e->win);
	if (!top)
		return;
	switch (e->type)
	{
	case BE_CLOSE:
		post (top->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
		break;
	case BE_RESIZE:
	{
		int pw, ph, stride;
		be_pixels (top->be, &pw, &ph, &stride);
		top->w += pw - top->cw;
		top->h += ph - top->ch;
		top->cw = pw;
		top->ch = ph;
		user_send (top->hwnd, WM_SIZE, 0, (uint32_t) pw | (uint32_t) ph << 16);
		top = by_be (e->win);
		if (top)
			invalidate_tree (top);
		break;
	}
	case BE_MOUSE_MOVE:
	case BE_MOUSE_DOWN:
	case BE_MOUSE_UP:
	{
		int lx, ly;
		struct wnd *w = capture_hwnd && wnd_get (capture_hwnd) ? wnd_get (capture_hwnd) : hit (top, e->x, e->y, &lx, &ly);
		if (capture_hwnd && w)
		{
			int ox, oy, cw, ch;
			long be;
			wnd_client_geometry (w->hwnd, &ox, &oy, &cw, &ch, &be);
			lx = e->x - ox;
			ly = e->y - oy;
		}
		mouse_x = e->x;
		mouse_y = e->y;
		int old = mouse_buttons;
		uint16_t msg = WM_MOUSEMOVE;
		if (e->type == BE_MOUSE_DOWN)
		{
			int b = e->button & ~old;
			if (!b)
				b = e->button ? e->button : 1;
			mouse_buttons = old | b;
			msg = (b & 1) ? WM_LBUTTONDOWN : (b & 2) ? WM_RBUTTONDOWN : WM_MBUTTONDOWN;
		}
		else if (e->type == BE_MOUSE_UP)
		{
			int b = old & ~e->button;
			if (!b)
				b = old ? old : 1;
			mouse_buttons = old & ~b;
			msg = (b & 1) ? WM_LBUTTONUP : (b & 2) ? WM_RBUTTONUP : WM_MBUTTONUP;
		}
		uint16_t mk = (uint16_t) ((mouse_buttons & 1 ? 1 : 0) | (mouse_buttons & 2 ? 2 : 0) | (mouse_buttons & 4 ? 0x10 : 0)
			| (e->mods & 2 ? 4 : 0) | (e->mods & 1 ? 8 : 0));
		post (w->hwnd, msg, mk, (uint32_t) (uint16_t) lx | (uint32_t) (uint16_t) ly << 16);
		break;
	}
	case BE_KEY_DOWN:
	case BE_KEY_UP:
	{
		uint16_t vk = hid_to_vk (e->key, e->ch);
		if (!vk)
			break;
		if (e->type == BE_KEY_DOWN)
			key_char[vk & 0xff] = (uint8_t) (e->ch > 0 && e->ch < 256 ? e->ch : 0);
		uint16_t to = focus_hwnd ? focus_hwnd : top->hwnd;
		int alt = e->mods & 4;
		post (to, e->type == BE_KEY_DOWN ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN) : (alt ? WM_SYSKEYUP : WM_KEYUP), vk,
			1u | (e->type == BE_KEY_UP ? 0xc0000000u : 0) | (alt ? 0x20000000u : 0));
		break;
	}
	case BE_FOCUS:
		break;
	}
}

static int find_invalid (void)
{
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && wnds[i].visible && wnds[i].invalid)
		{
			struct wnd *t = wnd_top (&wnds[i]);
			if (t && t->visible && t->be)
				return i;
		}
	return -1;
}

static void put_msg (uint32_t lpmsg, const struct qmsg *m)
{
	uint32_t a = far_lin (lpmsg);
	wr16 (a, m->hwnd);
	wr16 (a + 2, m->msg);
	wr16 (a + 4, m->wp);
	wr32 (a + 6, m->lp);
	wr32 (a + 10, m->time);
	wr16 (a + 14, (uint16_t) m->x);
	wr16 (a + 16, (uint16_t) m->y);
}

/* prend un message : 1 si *m est rempli ; wait : attendre qu'il y en ait un */
static int take (struct qmsg *m, int remove, int wait)
{
	for (;;)
	{
		if (qhead != qtail)
		{
			*m = queue[qhead];
			if (remove)
				qhead = (qhead + 1) % QMAX;
			return 1;
		}
		if (quit_posted)
		{
			memset (m, 0, sizeof *m);
			m->msg = WM_QUIT;
			m->wp = (uint16_t) quit_code;
			if (remove)
				quit_posted = 0;
			return 1;
		}
		/* les événements de Korin déjà là */
		struct be_event e;
		int got = 0;
		while (be_wait (0, &e))
		{
			translate_event (&e);
			got = 1;
		}
		if (got)
			continue;
		int i = find_invalid ();
		if (i >= 0)
		{
			memset (m, 0, sizeof *m);
			m->hwnd = wnds[i].hwnd;
			m->msg = WM_PAINT;
			m->time = be_ticks ();
			return 1;
		}
		if (!wait)
			return 0;
		if (be_wait (200, &e))
			translate_event (&e);
		if (w16_exit)
			return 0;
	}
}

void api_GetMessage16 (struct w16call *c)
{
	struct qmsg m;
	take (&m, 1, 1);
	put_msg (c->a[0], &m);
	c->ret = m.msg != WM_QUIT;
}

void api_PeekMessage16 (struct w16call *c)
{
	struct qmsg m;
	int remove = (int) (c->a[4] & 1);
	if (!take (&m, remove, 0))
	{
		c->ret = 0;
		return;
	}
	put_msg (c->a[0], &m);
	c->ret = 1;
}

void api_TranslateMessage16 (struct w16call *c)
{
	uint32_t a = far_lin (c->a[0]);
	uint16_t msg = rd16 (a + 2), vk = rd16 (a + 4);
	c->ret = 0;
	if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && key_char[vk & 0xff])
	{
		post (rd16 (a), msg == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR, key_char[vk & 0xff], rd32 (a + 6));
		c->ret = 1;
	}
}

void api_DispatchMessage16 (struct w16call *c)
{
	uint32_t a = far_lin (c->a[0]);
	uint16_t hwnd = rd16 (a), msg = rd16 (a + 2), wp = rd16 (a + 4);
	uint32_t lp = rd32 (a + 6);
	if (msg == WM_TIMER && lp)
	{
		uint16_t args[4] = { hwnd, msg, wp, 0 };
		uint32_t t = rd32 (a + 10);
		args[3] = (uint16_t) (t >> 16);
		uint16_t args5[5] = { hwnd, msg, wp, (uint16_t) (t >> 16), (uint16_t) t };
		(void) args;
		c->ret = call16 (lp, args5, 5, task.hinstance);
		return;
	}
	c->ret = user_send (hwnd, msg, wp, lp);
	struct wnd *w = wnd_get (hwnd);
	if (msg == WM_PAINT && w && w->invalid)
		w->invalid = w->erase = 0;	/* (une procédure qui ne peint pas : Windows valide pour elle) */
}

/* ---- divers ---- */
void api_LoadIcon16 (struct w16call *c) { c->ret = 0x7f00; }
void api_LoadCursor16 (struct w16call *c) { c->ret = 0x7f01; }
void api_SetCursor16 (struct w16call *c) { c->ret = 0x7f01; }
void api_GetSysColor16 (struct w16call *c) { c->ret = user_sys_color ((int) c->a[0]); }

void api_GetSystemMetrics16 (struct w16call *c)
{
	static const int16_t m[] = { 640, 480, 17, 17, 20, 1, 1, 4, 4, 17, 17, 32, 32, 32, 32, 18, 640, 460, 0, 1,
		17, 17, 17, 0, 0, 0, 0, 16, 16, 100, 26, 20, 4, 4, 4, 4, 4, 4, 32, 32, 0, 0, 0, 0 };
	int i = (int16_t) c->a[0];
	c->ret = (i >= 0 && i < (int) (sizeof m / sizeof m[0])) ? (uint16_t) m[i] : 0;
}

void api_SetFocus16 (struct w16call *c)
{
	c->ret = focus_hwnd;
	focus_hwnd = (uint16_t) c->a[0];
}

void api_GetFocus16 (struct w16call *c) { c->ret = focus_hwnd; }
void api_SetCapture16 (struct w16call *c) { c->ret = capture_hwnd; capture_hwnd = (uint16_t) c->a[0]; }
void api_ReleaseCapture (struct w16call *c) { (void) c; capture_hwnd = 0; }
void api_ReleaseCapture16 (struct w16call *c) { (void) c; capture_hwnd = 0; }
void api_IsWindow16 (struct w16call *c) { c->ret = wnd_get ((uint16_t) c->a[0]) != 0; }
void api_IsWindowVisible16 (struct w16call *c) { struct wnd *w = wnd_get ((uint16_t) c->a[0]); c->ret = w && w->visible; }
void api_GetParent16 (struct w16call *c) { struct wnd *w = wnd_get ((uint16_t) c->a[0]); c->ret = w ? (w->parent ? w->parent : w->owner) : 0; }

void api_SetWindowText16 (struct w16call *c)
{
	user_send ((uint16_t) c->a[0], WM_SETTEXT, 0, c->a[1]);
}

void api_GetWindowText16 (struct w16call *c)
{
	c->ret = user_send ((uint16_t) c->a[0], WM_GETTEXT, (uint16_t) c->a[2], c->a[1]);
}

void api_GetWindowWord16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	c->ret = 0;
	if (!w)
		return;
	if (off >= 0 && off + 2 <= (int) sizeof w->extra)
		c->ret = (uint32_t) (w->extra[off] | w->extra[off + 1] << 8);
	else if (off == -6)	/* GWW_HINSTANCE */
		c->ret = w->hinst;
	else if (off == -8)	/* GWW_HWNDPARENT */
		c->ret = w->parent;
	else if (off == -12)	/* GWW_ID */
		c->ret = w->id;
}

void api_SetWindowWord16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	c->ret = 0;
	if (w && off >= 0 && off + 2 <= (int) sizeof w->extra)
	{
		c->ret = (uint32_t) (w->extra[off] | w->extra[off + 1] << 8);
		w->extra[off] = (uint8_t) c->a[2];
		w->extra[off + 1] = (uint8_t) (c->a[2] >> 8);
	}
}

void api_GetWindowLong16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	c->ret = 0;
	if (!w)
		return;
	if (off >= 0 && off + 4 <= (int) sizeof w->extra)
		memcpy (&c->ret, w->extra + off, 4);
	else if (off == -4)	/* GWL_WNDPROC */
		c->ret = w->proc;
	else if (off == -16)
		c->ret = w->style | (w->visible ? WS_VISIBLE : 0);
	else if (off == -20)
		c->ret = w->exstyle;
}

void api_SetWindowLong16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	uint32_t v = c->a[2];
	c->ret = 0;
	if (!w)
		return;
	if (off >= 0 && off + 4 <= (int) sizeof w->extra)
	{
		memcpy (&c->ret, w->extra + off, 4);
		memcpy (w->extra + off, &v, 4);
	}
	else if (off == -4)
	{
		c->ret = w->proc;
		w->proc = v;
	}
	else if (off == -16)
	{
		c->ret = w->style;
		w->style = v & ~WS_VISIBLE;
	}
}

int user_window_count (void)
{
	int n = 0;
	for (int i = 0; i < MAX_WND; i++)
		n += wnds[i].used && !(wnds[i].style & WS_CHILD);
	return n;
}

void user_init (void)
{
	memset (wnds, 0, sizeof wnds);
	memset (classes, 0, sizeof classes);
}
