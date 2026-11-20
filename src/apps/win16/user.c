/* user.c -- win16 : USER (classes, fenêtres, messages, minuteries, focus, peinture, rectangles, atomes,
 * propriétés, presse-papiers, chaînes), écrit sur l'affichage de Korin (be.h).
 *
 * winevdm passe chaque appel de USER à user32 de Windows ; ici USER tient lui-même l'arbre des fenêtres : une
 * fenêtre principale (ou un dialogue) est une fenêtre de Korin (be_window_open), les fenêtres filles sont dessinées
 * dedans. Menus et accélérateurs : menu.c ; dialogues : dialog.c ; contrôles : ctrl.c. La sémantique suit
 * Windows 3.1 et le code de Wine (user/, LGPL-2.1+). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "w16.h"
#include "be.h"
#include "user.h"
#include "gdi.h"

#define SC_CLOSE 0xf060
#define CW_USEDEFAULT16 ((int16_t) 0x8000)
#define HIWORD_CTL(lp) ((uint16_t) ((lp) >> 16))

void menu_attach (uint16_t hwnd);
void menu_detach (uint16_t hwnd);
void menu_publish_pending (void);
int menu_command (uint16_t hwnd, int id);
void menu_init_popups (uint16_t hwnd);

/* ======================================================================== classes */
#define MAX_CLS 96
struct cls
{
	int used;
	char name[64];
	uint16_t atom, style, hinst, hicon, hcursor, hbrush, clsextra, wndextra;
	uint32_t proc, menuname;
	uint8_t extra[32];
};
static struct cls classes[MAX_CLS];
static uint16_t next_atom = 0xc000;

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

static int find_class_name (const char *name)
{
	for (int i = 0; i < MAX_CLS; i++)
		if (classes[i].used && !strcasecmp (classes[i].name, name))
			return i;
	return -1;
}

static int class_add (const char *name, uint32_t proc, uint16_t style, uint16_t hinst, uint16_t brush, int wndextra)
{
	for (int i = 0; i < MAX_CLS; i++)
		if (!classes[i].used)
		{
			struct cls *k = &classes[i];
			memset (k, 0, sizeof *k);
			k->used = 1;
			snprintf (k->name, sizeof k->name, "%s", name);
			k->proc = proc;
			k->style = style;
			k->hinst = hinst;
			k->hbrush = brush;
			k->wndextra = (uint16_t) wndextra;
			k->atom = next_atom++;
			return i;
		}
	return -1;
}

/* ======================================================================== fenêtres */
#define MAX_WND 512
static struct wnd wnds[MAX_WND];
#define HWND_OF(i) ((uint16_t) (0x1000 + (i) * 4))
#define HWND_DESKTOP 0x0ffc

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

static int is_disabled (struct wnd *w)
{
	for (; w; w = (w->style & WS_CHILD) ? wnd_get (w->parent) : 0)
		if (w->disabled)
			return 1;
	return 0;
}

/* ======================================================================== couleurs du système, d'après le thème */
uint32_t user_theme_ref (int bt)
{
	uint32_t rgb = be_theme (bt);
	return ((rgb >> 16) & 0xff) | (rgb & 0xff00) | (rgb & 0xff) << 16;
}

uint32_t user_sys_color (int i)
{
	switch (i)
	{
	case 0: return user_theme_ref (BT_LINE);		/* COLOR_SCROLLBAR */
	case 1: case 12: return user_theme_ref (BT_FACE);	/* BACKGROUND, APPWORKSPACE */
	case 2: case 3: return user_theme_ref (BT_ACCENT);	/* légendes */
	case 4: return user_theme_ref (BT_SURFACE);		/* MENU */
	case 5: return user_theme_ref (BT_RAISED);		/* WINDOW */
	case 6: return user_theme_ref (BT_LINE_STRONG);		/* WINDOWFRAME */
	case 7: case 8: case 18: return user_theme_ref (BT_TEXT);	/* MENUTEXT, WINDOWTEXT, BTNTEXT */
	case 9: return user_theme_ref (BT_ACCENT_INK);		/* CAPTIONTEXT */
	case 10: case 11: return user_theme_ref (BT_LINE);	/* bords */
	case 13: return user_theme_ref (BT_ACCENT);		/* HIGHLIGHT */
	case 14: return user_theme_ref (BT_ACCENT_INK);		/* HIGHLIGHTTEXT */
	case 15: return user_theme_ref (BT_SURFACE);		/* BTNFACE */
	case 16: return user_theme_ref (BT_LINE_STRONG);	/* BTNSHADOW */
	case 17: case 19: return user_theme_ref (BT_TEXT3);	/* GRAYTEXT, INACTIVECAPTIONTEXT */
	case 20: return user_theme_ref (BT_RAISED);		/* BTNHIGHLIGHT */
	default: return user_theme_ref (BT_SURFACE);
	}
}

/* ======================================================================== la file des messages */
struct qmsg { uint16_t hwnd, msg, wp; uint32_t lp, time; int16_t x, y; };
#define QMAX 512
static struct qmsg queue[QMAX];
static int qhead, qtail;
static int quit_posted, quit_code;
static uint16_t focus_hwnd, capture_hwnd, active_hwnd;
static int mouse_x, mouse_y, mouse_buttons;
static long mouse_be;
static uint8_t key_char[256], key_down[256];
static uint32_t last_msg_time;
static int16_t last_msg_x, last_msg_y;

int user_post (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
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
	w16_log ("   message %04x à %04x (%04x, %08x)\n", msg, hwnd, wp, lp);
	native_wndproc fn = native_get (w->proc);
	if (fn)
		return fn (hwnd, msg, wp, lp);
	uint16_t args[5] = { hwnd, msg, wp, (uint16_t) (lp >> 16), (uint16_t) lp };
	return call16 (w->proc, args, 5, ne_ds_for (w->proc));
}

static uint32_t call_proc (uint32_t proc, uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	native_wndproc fn = native_get (proc);
	if (fn)
		return fn (hwnd, msg, wp, lp);
	if (!proc)
		return 0;
	uint16_t args[5] = { hwnd, msg, wp, (uint16_t) (lp >> 16), (uint16_t) lp };
	return call16 (proc, args, 5, ne_ds_for (proc));
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

/* une zone de w (coordonnées clientes) invalide aussi les fenêtres filles qu'elle touche */
static void invalidate_rect_tree (struct wnd *w, int x0, int y0, int x1, int y1, int erase)
{
	invalidate (w, x0, y0, x1, y1, erase);
	for (int i = 0; i < MAX_WND; i++)
	{
		struct wnd *k = &wnds[i];
		if (!k->used || k->parent != w->hwnd || !(k->style & WS_CHILD) || !k->visible)
			continue;
		int kx0 = k->x + k->cx, ky0 = k->y + k->cy;
		if (x1 <= k->x || x0 >= k->x + k->w || y1 <= k->y || y0 >= k->y + k->h)
			continue;
		invalidate_rect_tree (k, x0 - kx0, y0 - ky0, x1 - kx0, y1 - ky0, 1);
	}
}

static void invalidate_tree (struct wnd *w)
{
	invalidate_rect_tree (w, 0, 0, w->cw, w->ch, 1);
}

void user_invalidate (uint16_t hwnd, int erase)
{
	struct wnd *w = wnd_get (hwnd);
	if (w)
		invalidate (w, 0, 0, w->cw, w->ch, erase);
}

/* ---- rangement 16 bits ---- */
static void put_rect (uint32_t p, int l, int t, int r, int b)
{
	if (!p)
		return;
	uint32_t a = far_lin (p);
	wr16 (a, (uint16_t) l);
	wr16 (a + 2, (uint16_t) t);
	wr16 (a + 4, (uint16_t) r);
	wr16 (a + 6, (uint16_t) b);
}

static void get_rect (uint32_t p, int *r)
{
	uint32_t a = far_lin (p);
	for (int i = 0; i < 4; i++)
		r[i] = (int16_t) rd16 (a + 2 * (uint32_t) i);
}

/* ======================================================================== création */
static void wnd_geometry (struct wnd *w)
{
	/* la zone cliente d'après le style, comme Windows 3.1 ; une fenêtre principale n'a pas de cadre chez nous :
	 * sa zone cliente garde la taille que Windows lui aurait donnée */
	int b = 0, cap = 0;
	if (w->style & WS_THICKFRAME)
		b = W16_FRAME;
	else if (w->style & (WS_BORDER | WS_DLGFRAME))
		b = (w->style & WS_DLGFRAME) && !(w->style & WS_CHILD) ? 4 : 1;
	if ((w->style & WS_CAPTION) == WS_CAPTION)
		cap = W16_CAPTION - 1;
	int top = !(w->style & WS_CHILD);
	if (top && w->menu)
		cap += W16_MENU;
	w->cx = top ? 0 : b;
	w->cy = top ? 0 : b + cap;
	w->cw = w->w - 2 * b - ((w->style & WS_VSCROLL) ? W16_SCROLL - 1 : 0);
	w->ch = w->h - 2 * b - cap - ((w->style & WS_HSCROLL) ? W16_SCROLL - 1 : 0);
	if (w->cw < 0) w->cw = 0;
	if (w->ch < 0) w->ch = 0;
}

static uint16_t sel_scratch;	/* un bloc pour les structures passées aux procédures (CREATESTRUCT…) */

static uint16_t create_window (uint32_t exstyle, int k, uint32_t cname, const char *text_raw, uint32_t wname, uint32_t style,
	int x, int y, int cw, int ch, uint16_t parent, uint16_t menu, uint16_t hinst, uint32_t param)
{
	int i;
	for (i = 0; i < MAX_WND && wnds[i].used; i++)
		;
	if (i == MAX_WND)
		return 0;
	struct wnd *w = &wnds[i];
	memset (w, 0, sizeof *w);
	w->used = 1;
	w->hwnd = HWND_OF (i);
	w->cls = k;
	w->proc = classes[k].proc;
	snprintf (w->clsname, sizeof w->clsname, "%s", classes[k].name);
	w->style = style & ~WS_VISIBLE;
	w->exstyle = exstyle;
	w->disabled = (style & WS_DISABLED) != 0;
	w->hinst = hinst ? hinst : task.hinstance;
	if (style & WS_CHILD)
	{
		w->parent = parent;
		w->id = menu;
	}
	else
	{
		w->owner = parent;
		if (menu)
			w->menu = menu;
		else if (classes[k].menuname)
		{
			extern uint16_t menu_load (uint16_t hinst, uint32_t name);
			w->menu = menu_load (classes[k].hinst ? classes[k].hinst : w->hinst, classes[k].menuname);
		}
	}
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
	if (text_raw)
		snprintf (w->text, sizeof w->text, "%s", text_raw);
	w->sb_max[0] = w->sb_max[1] = 100;
	uint16_t hwnd = w->hwnd;
	/* CREATESTRUCT 16 bits (et le texte, s'il vient de nous), dans un bloc à part le temps des messages */
	uint16_t cs_sel = global_alloc (64 + 260, 0);
	uint32_t cs = sel_base (cs_sel);
	if (!wname && text_raw)
	{
		snprintf ((char *) lin_ptr (cs + 64), 256, "%s", text_raw);
		wname = MAKESEGPTR (cs_sel, 64);
	}
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
	wr32 (cs + 30, exstyle);
	if (!user_send (hwnd, WM_NCCREATE, 0, MAKESEGPTR (cs_sel, 0)))
	{
		global_free (cs_sel);
		w->used = 0;
		return 0;
	}
	if ((int16_t) user_send (hwnd, WM_CREATE, 0, MAKESEGPTR (cs_sel, 0)) == -1)
	{
		global_free (cs_sel);
		w->used = 0;
		return 0;
	}
	global_free (cs_sel);
	w = wnd_get (hwnd);
	if (!w)
		return 0;
	user_send (hwnd, WM_SIZE, 0, (uint32_t) w->cw | (uint32_t) w->ch << 16);
	user_send (hwnd, WM_MOVE, 0, (uint32_t) (uint16_t) (w->x < 0 ? 0 : w->x) | (uint32_t) (uint16_t) (w->y < 0 ? 0 : w->y) << 16);
	if ((style & WS_CHILD) && parent)
		user_send (parent, 0x0210, WM_CREATE, MAKESEGPTR (w->id, hwnd));	/* WM_PARENTNOTIFY */
	if (style & WS_VISIBLE)
	{
		struct w16call s = { .a = { hwnd, 1 }, .n = 2 };
		extern void api_ShowWindow16 (struct w16call *);
		api_ShowWindow16 (&s);
	}
	w16_log ("fenêtre %04x (%s) « %s » : %d×%d\n", hwnd, classes[k].name, w ? w->text : "", w ? w->cw : 0, w ? w->ch : 0);
	return hwnd;
}

uint16_t user_create (uint32_t exstyle, const char *cls, const char *text, uint32_t style, int x, int y, int w, int h,
	uint16_t parent, uint16_t id, uint16_t hinst, uint32_t param)
{
	int k = find_class_name (cls);
	if (k < 0)
	{
		fprintf (stderr, "win16 : classe « %s » inconnue\n", cls);
		return 0;
	}
	return create_window (exstyle, k, 0, text, 0, style, x, y, w, h, parent, id, hinst, param);
}

void api_CreateWindow16 (struct w16call *c)
{
	int k = find_class (c->a[0]);
	if (k < 0)
	{
		fprintf (stderr, "win16 : CreateWindow, classe « %s » inconnue\n", w16_str (c->a[0]));
		c->ret = 0;
		return;
	}
	c->ret = create_window (0, k, c->a[0], c->a[1] && SELECTOROF (c->a[1]) ? w16_raw (c->a[1]) : 0, c->a[1], c->a[2],
		(int16_t) c->a[3], (int16_t) c->a[4], (int16_t) c->a[5], (int16_t) c->a[6], (uint16_t) c->a[7], (uint16_t) c->a[8],
		(uint16_t) c->a[9], c->a[10]);
}

void api_CreateWindowEx16 (struct w16call *c)
{
	int k = find_class (c->a[1]);
	if (k < 0)
	{
		fprintf (stderr, "win16 : CreateWindowEx, classe « %s » inconnue\n", w16_str (c->a[1]));
		c->ret = 0;
		return;
	}
	c->ret = create_window (c->a[0], k, c->a[1], c->a[2] && SELECTOROF (c->a[2]) ? w16_raw (c->a[2]) : 0, c->a[2], c->a[3],
		(int16_t) c->a[4], (int16_t) c->a[5], (int16_t) c->a[6], (int16_t) c->a[7], (uint16_t) c->a[8], (uint16_t) c->a[9],
		(uint16_t) c->a[10], c->a[11]);
}

/* ---- classes ---- */
static void read_wndclass (uint32_t a, struct cls *k)
{
	k->style = rd16 (a);
	k->proc = rd32 (a + 2);
	k->clsextra = rd16 (a + 6);
	k->wndextra = rd16 (a + 8);
	k->hinst = rd16 (a + 10);
	k->hicon = rd16 (a + 12);
	k->hcursor = rd16 (a + 14);
	k->hbrush = rd16 (a + 16);
	k->menuname = rd32 (a + 18);
}

void api_RegisterClass16 (struct w16call *c)
{
	uint32_t a = far_lin (c->a[0]);
	uint32_t name = rd32 (a + 22);
	char nm[64];
	snprintf (nm, sizeof nm, "%s", SELECTOROF (name) ? w16_raw (name) : "");
	int old = find_class_name (nm);
	if (old >= 0 && nm[0])
	{
		c->ret = 0;	/* (déjà là) */
		return;
	}
	int k = class_add (nm, 0, 0, 0, 0, 0);
	if (k < 0)
	{
		c->ret = 0;
		return;
	}
	read_wndclass (a, &classes[k]);
	w16_log ("classe « %s » : procédure %08x\n", classes[k].name, classes[k].proc);
	c->ret = classes[k].atom;
}

void api_UnregisterClass16 (struct w16call *c)
{
	int k = find_class (c->a[0]);
	if (k >= 0)
		classes[k].used = 0;
	c->ret = k >= 0;
}

void api_GetClassInfo16 (struct w16call *c)
{
	int k = find_class (c->a[1]);
	if (k < 0)
	{
		c->ret = 0;
		return;
	}
	uint32_t a = far_lin (c->a[2]);
	struct cls *s = &classes[k];
	wr16 (a, s->style);
	wr32 (a + 2, s->proc);
	wr16 (a + 6, s->clsextra);
	wr16 (a + 8, s->wndextra);
	wr16 (a + 10, s->hinst);
	wr16 (a + 12, s->hicon);
	wr16 (a + 14, s->hcursor);
	wr16 (a + 16, s->hbrush);
	wr32 (a + 18, s->menuname);
	wr32 (a + 22, c->a[1]);
	c->ret = 1;
}

void api_GetClassName16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int n = (int16_t) c->a[2];
	if (!w || n <= 0)
	{
		c->ret = 0;
		return;
	}
	const char *s = classes[w->cls].name;
	int l = (int) strlen (s);
	if (l > n - 1)
		l = n - 1;
	memcpy (far_ptr (c->a[1]), s, (size_t) l);
	((char *) far_ptr (c->a[1]))[l] = 0;
	c->ret = (uint32_t) l;
}

void api_GetClassWord16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	c->ret = 0;
	if (!w)
		return;
	struct cls *k = &classes[w->cls];
	switch (off)
	{
	case -10: c->ret = k->hbrush; break;	/* GCW_HBRBACKGROUND */
	case -12: c->ret = k->hcursor; break;
	case -14: c->ret = k->hicon; break;
	case -16: c->ret = k->hinst; break;	/* GCW_HMODULE */
	case -18: c->ret = k->wndextra; break;
	case -20: c->ret = k->clsextra; break;
	case -26: c->ret = k->style; break;
	case -32: c->ret = k->atom; break;
	default: if (off >= 0 && off + 2 <= 32) c->ret = (uint32_t) (k->extra[off] | k->extra[off + 1] << 8); break;
	}
}

void api_SetClassWord16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	uint16_t v = (uint16_t) c->a[2];
	c->ret = 0;
	if (!w)
		return;
	struct cls *k = &classes[w->cls];
	switch (off)
	{
	case -10: c->ret = k->hbrush; k->hbrush = v; break;
	case -12: c->ret = k->hcursor; k->hcursor = v; break;
	case -14: c->ret = k->hicon; k->hicon = v; break;
	case -26: c->ret = k->style; k->style = v; break;
	default:
		if (off >= 0 && off + 2 <= 32)
		{
			c->ret = (uint32_t) (k->extra[off] | k->extra[off + 1] << 8);
			k->extra[off] = (uint8_t) v;
			k->extra[off + 1] = (uint8_t) (v >> 8);
		}
		break;
	}
}

void api_GetClassLong16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	c->ret = 0;
	if (!w)
		return;
	if (off == -24)	/* GCL_WNDPROC */
		c->ret = classes[w->cls].proc;
	else if (off == -8)	/* GCL_MENUNAME */
		c->ret = classes[w->cls].menuname;
	else if (off >= 0 && off + 4 <= 32)
		memcpy (&c->ret, classes[w->cls].extra + off, 4);
}

void api_SetClassLong16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	c->ret = 0;
	if (!w)
		return;
	if (off == -24)
	{
		c->ret = classes[w->cls].proc;
		classes[w->cls].proc = c->a[2];
	}
	else if (off >= 0 && off + 4 <= 32)
	{
		memcpy (&c->ret, classes[w->cls].extra + off, 4);
		memcpy (classes[w->cls].extra + off, &c->a[2], 4);
	}
}

/* ======================================================================== montrer, peindre */
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
	uint16_t hwnd = w->hwnd;
	if (!(w->style & WS_CHILD))
	{
		if (show && !w->be)
		{
			char title[512];
			w16_to_utf8 ((const uint8_t *) w->text, -1, title, sizeof title);
			int x = w->x, y = w->y;
			if (w->owner && wnd_get (w->owner) && (w->style & WS_POPUP))
				x = y = -1;	/* (un dialogue : Elegant le place) */
			w->be = be_window_open (x, y, w->cw, w->ch, title[0] ? title : task.modname);
			if (!w->be)
				w16_fatal ("fenêtre de Korin refusée");
			if (w->menu)
				menu_attach (hwnd);
		}
		else if (!show && w->be)
		{
			be_window_close (w->be);
			w->be = 0;
		}
	}
	user_send (hwnd, WM_SHOWWINDOW, (uint16_t) show, 0);
	w = wnd_get (hwnd);
	if (!w)
		return;
	if (show)
	{
		invalidate_tree (w);
		if (!(w->style & WS_CHILD))
		{
			uint16_t prev = active_hwnd;
			active_hwnd = hwnd;
			if (prev != hwnd)
			{
				if (!prev)
					user_send (hwnd, WM_ACTIVATEAPP, 1, 0);
				user_send (hwnd, WM_NCACTIVATE, 1, 0);
				user_send (hwnd, WM_ACTIVATE, 1, prev);
				if (wnd_get (hwnd) && !w->dialog && (!focus_hwnd || !wnd_get (focus_hwnd)))
					user_set_focus (hwnd);
			}
		}
	}
	else
	{
		/* (repeindre ce qu'elle cachait, chez le parent) */
		struct wnd *p = (w->style & WS_CHILD) ? wnd_get (w->parent) : 0;
		if (p)
			invalidate_rect_tree (p, w->x, w->y, w->x + w->w, w->y + w->h, 1);
	}
}

void api_UpdateWindow16 (struct w16call *c)
{
	uint16_t h = (uint16_t) c->a[0];
	struct wnd *w = wnd_get (h);
	if (w && w->invalid && w->visible)
		user_send (h, WM_PAINT, 0, 0);
	/* (et les filles) */
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && wnds[i].parent == h && (wnds[i].style & WS_CHILD) && wnds[i].visible && wnds[i].invalid)
		{
			struct w16call s = { .a = { wnds[i].hwnd }, .n = 1 };
			api_UpdateWindow16 (&s);
		}
	c->ret = 1;
}

void api_InvalidateRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
	{
		/* (0 : tout le bureau, c'est-à-dire toutes nos fenêtres) */
		if (!c->a[0])
			for (int i = 0; i < MAX_WND; i++)
				if (wnds[i].used && !(wnds[i].style & WS_CHILD))
					invalidate_tree (&wnds[i]);
		return;
	}
	if (c->a[1])
	{
		int r[4];
		get_rect (c->a[1], r);
		invalidate_rect_tree (w, r[0], r[1], r[2], r[3], (int) c->a[2]);
	}
	else
		invalidate_rect_tree (w, 0, 0, w->cw, w->ch, (int) c->a[2]);
}

void api_InvalidateRgn16 (struct w16call *c)
{
	struct w16call d = { .a = { c->a[0], 0, c->a[2] }, .n = 3 };
	api_InvalidateRect16 (&d);
}

void api_ValidateRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (w)
		w->invalid = w->erase = 0;
}

void api_ValidateRgn16 (struct w16call *c) { api_ValidateRect16 (c); }

void api_GetUpdateRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	c->ret = w && w->invalid;
	if (c->a[1])
		put_rect (c->a[1], w && w->invalid ? w->ix0 : 0, w && w->invalid ? w->iy0 : 0, w && w->invalid ? w->ix1 : 0,
			w && w->invalid ? w->iy1 : 0);
}

void api_GetClientRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w && c->a[0] == HWND_DESKTOP)
	{
		put_rect (c->a[1], 0, 0, 640, 480);
		return;
	}
	put_rect (c->a[1], 0, 0, w ? w->cw : 0, w ? w->ch : 0);
}

/* la position d'une fenêtre « à l'écran » : les fenêtres principales sont en (x, y) si elles l'ont demandé, (0, 0)
 * sinon (Elegant les place, le programme n'a pas besoin de le savoir) */
static void screen_origin (struct wnd *w, int *sx, int *sy)
{
	int x = 0, y = 0;
	while (w && (w->style & WS_CHILD))
	{
		x += w->x + w->cx;
		y += w->y + w->cy;
		w = wnd_get (w->parent);
	}
	if (w)
	{
		x += (w->x < 0 ? 0 : w->x) + w->cx;
		y += (w->y < 0 ? 0 : w->y) + w->cy + ((w->menu && !(w->style & WS_CHILD)) ? W16_CAPTION + W16_MENU : W16_CAPTION);
	}
	*sx = x;
	*sy = y;
}

void api_GetWindowRect16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
	{
		put_rect (c->a[1], 0, 0, 640, 480);
		return;
	}
	int sx, sy;
	screen_origin (w, &sx, &sy);
	sx -= w->cx;
	sy -= w->cy;
	if (!(w->style & WS_CHILD))
	{
		sx = w->x < 0 ? 0 : w->x;
		sy = w->y < 0 ? 0 : w->y;
	}
	put_rect (c->a[1], sx, sy, sx + w->w, sy + w->h);
}

static void map_point (uint16_t from, uint16_t to, int *x, int *y)
{
	int fx = 0, fy = 0, tx = 0, ty = 0;
	struct wnd *f = wnd_get (from), *t = wnd_get (to);
	if (f)
		screen_origin (f, &fx, &fy);
	if (t)
		screen_origin (t, &tx, &ty);
	*x += fx - tx;
	*y += fy - ty;
}

void api_ClientToScreen16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[1]);
	int x = (int16_t) rd16 (p), y = (int16_t) rd16 (p + 2);
	map_point ((uint16_t) c->a[0], 0, &x, &y);
	wr16 (p, (uint16_t) x);
	wr16 (p + 2, (uint16_t) y);
}

void api_ScreenToClient16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[1]);
	int x = (int16_t) rd16 (p), y = (int16_t) rd16 (p + 2);
	map_point (0, (uint16_t) c->a[0], &x, &y);
	wr16 (p, (uint16_t) x);
	wr16 (p + 2, (uint16_t) y);
}

void api_MapWindowPoints16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[2]);
	for (int i = 0; i < (int16_t) c->a[3]; i++, p += 4)
	{
		int x = (int16_t) rd16 (p), y = (int16_t) rd16 (p + 2);
		map_point ((uint16_t) c->a[0], (uint16_t) c->a[1], &x, &y);
		wr16 (p, (uint16_t) x);
		wr16 (p + 2, (uint16_t) y);
	}
}

/* ---- peindre ---- */
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

/* pour les contrôles en C */
uint16_t user_begin (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	w->invalid = w->erase = 0;
	return gdi_dc_for_window (hwnd);
}

void user_end (uint16_t hdc) { gdi_release_dc (hdc); }

void api_GetDC16 (struct w16call *c)
{
	if (!c->a[0] || !wnd_get ((uint16_t) c->a[0]))
	{
		/* (le DC de l'écran : un DC d'information, rien n'est tracé) */
		extern void api_CreateIC16 (struct w16call *);
		api_CreateIC16 (c);
		return;
	}
	c->ret = gdi_dc_for_window ((uint16_t) c->a[0]);
}

void api_GetDCEx16 (struct w16call *c) { api_GetDC16 (c); }
void api_GetWindowDC16 (struct w16call *c) { api_GetDC16 (c); }

void api_ReleaseDC16 (struct w16call *c)
{
	gdi_release_dc ((uint16_t) c->a[1]);
	c->ret = 1;
}

void api_ScrollWindow16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
		return;
	/* (le plus simple et toujours juste : tout repeindre, les filles suivent) */
	int dx = (int16_t) c->a[1], dy = (int16_t) c->a[2];
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && wnds[i].parent == w->hwnd && (wnds[i].style & WS_CHILD) && !c->a[3])
		{
			wnds[i].x += dx;
			wnds[i].y += dy;
		}
	invalidate_tree (w);
}

/* ======================================================================== détruire */
static void destroy (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w || w->destroyed)
		return;
	w->destroyed = 1;
	if (!(w->style & WS_CHILD) && w->visible)
	{
		w->visible = 0;
		user_send (hwnd, WM_SHOWWINDOW, 0, 0);
	}
	user_send (hwnd, WM_DESTROY, 0, 0);
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && (wnds[i].parent == hwnd || wnds[i].owner == hwnd) && wnds[i].hwnd != hwnd)
			destroy (wnds[i].hwnd);
	user_send (hwnd, WM_NCDESTROY, 0, 0);
	w = wnd_get (hwnd);
	if (!w)
		return;
	if (w->be)
	{
		menu_detach (hwnd);
		be_window_close (w->be);
	}
	if (w->style & WS_CHILD)
	{
		struct wnd *p = wnd_get (w->parent);
		if (p && !p->destroyed && w->visible)
			invalidate_rect_tree (p, w->x, w->y, w->x + w->w, w->y + w->h, 1);
	}
	if (focus_hwnd == hwnd)
		focus_hwnd = 0;
	if (capture_hwnd == hwnd)
		capture_hwnd = 0;
	if (active_hwnd == hwnd)
	{
		active_hwnd = w->owner;
		if (w->owner && wnd_get (w->owner))
			user_set_focus (w->owner);
	}
	free (w->cdata);
	w->cdata = 0;
	w->used = 0;
}

void api_DestroyWindow16 (struct w16call *c)
{
	destroy ((uint16_t) c->a[0]);
	c->ret = 1;
}

/* ======================================================================== messages */
void api_PostQuitMessage16 (struct w16call *c)
{
	quit_posted = 1;
	quit_code = (int16_t) c->a[0];
}

void api_PostMessage16 (struct w16call *c)
{
	if (c->a[0] == 0xffff)	/* HWND_BROADCAST : nos fenêtres principales */
	{
		for (int i = 0; i < MAX_WND; i++)
			if (wnds[i].used && !(wnds[i].style & WS_CHILD))
				user_post (wnds[i].hwnd, (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
		c->ret = 1;
		return;
	}
	c->ret = (uint32_t) user_post ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
}

void api_PostAppMessage16 (struct w16call *c) { c->ret = (uint32_t) user_post (0, (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]); }

void api_SendMessage16 (struct w16call *c)
{
	if (c->a[0] == 0xffff)
	{
		for (int i = 0; i < MAX_WND; i++)
			if (wnds[i].used && !(wnds[i].style & WS_CHILD))
				user_send (wnds[i].hwnd, (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
		c->ret = 0;
		return;
	}
	c->ret = user_send ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
}

void api_CallWindowProc16 (struct w16call *c)
{
	c->ret = call_proc (c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], (uint16_t) c->a[3], c->a[4]);
}

void api_InSendMessage16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_ReplyMessage16 (struct w16call *c) { (void) c; }

uint32_t user_def_window_proc (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	switch (msg)
	{
	case WM_NCCREATE:
		return 1;
	case WM_CLOSE:
		destroy (hwnd);
		return 0;
	case WM_SYSCOMMAND:
		if ((wp & 0xfff0) == SC_CLOSE)
			user_send (hwnd, WM_CLOSE, 0, 0);
		return 0;
	case WM_ERASEBKGND:
	{
		uint16_t br = classes[w->cls].hbrush;
		if (br)
			gdi_fill_rect_brush (wp, 0, 0, w->cw, w->ch, br);
		return 1;
	}
	case WM_PAINT:
	{
		/* (comme Windows : BeginPaint/EndPaint, l'effacement compris) */
		uint16_t hdc = gdi_dc_for_window (hwnd);
		int erase = w->erase;
		w->invalid = w->erase = 0;
		if (erase)
			user_send (hwnd, WM_ERASEBKGND, hdc, 0);
		gdi_release_dc (hdc);
		return 0;
	}
	case WM_SETTEXT:
		snprintf (w->text, sizeof w->text, "%s", lp ? (const char *) far_ptr (lp) : "");
		if (w->be)
		{
			char title[512];
			w16_to_utf8 ((const uint8_t *) w->text, -1, title, sizeof title);
			be_window_title (w->be, title);
		}
		return 1;
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
		return (uint32_t) n;
	}
	case WM_GETTEXTLENGTH:
		return (uint32_t) strlen (w->text);
	case WM_QUERYENDSESSION:
	case 0x0013:	/* WM_QUERYOPEN */
		return 1;
	case WM_MOUSEACTIVATE:
		return 1;
	case WM_CTLCOLOR:
	{
		/* le fond des contrôles : le thème (le dialogue lui-même, ses textes) */
		gdi_set_colors (wp, user_sys_color (8), user_sys_color (HIWORD_CTL (lp) == 1 || HIWORD_CTL (lp) == 2 ? 5 : 15));
		return HIWORD_CTL (lp) == 1 || HIWORD_CTL (lp) == 2 ? 6 : 16;	/* COLOR_WINDOW + 1, COLOR_BTNFACE + 1 */
	}
	case WM_SETFONT:
		w->font = wp;
		return 0;
	case WM_GETFONT:
		return w->font;
	case WM_KEYDOWN:
		if (wp == 0x79)	/* F10 */
			return 0;
		return 0;
	case WM_SYSKEYDOWN:
		if (wp == 0x73 && (lp & 0x20000000u))	/* Alt+F4 */
			user_post (wnd_top (w)->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
		return 0;
	case WM_VSCROLL:
	case WM_HSCROLL:
		return 0;
	}
	return 0;
}

void api_DefWindowProc16 (struct w16call *c)
{
	c->ret = user_def_window_proc ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
}

/* ======================================================================== la boucle des messages */
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
	if (ch >= 'a' && ch <= 'z')
		return (uint16_t) (ch - 32);
	return (uint16_t) (ch > 0 && ch < 0x80 ? ch : 0);
}

int user_key_down (int vk) { return key_down[vk & 0xff]; }

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

/* une fenêtre principale bloquée par un dialogue modal (elle, ou son propriétaire, est désactivée) */
static int blocked (struct wnd *top)
{
	return top && top->disabled;
}

static uint32_t last_click_time;
static uint16_t last_click_hwnd;
static int last_click_x, last_click_y;

static void translate_event (const struct be_event *e)
{
	struct wnd *top = by_be (e->win);
	if (!top)
		return;
	switch (e->type)
	{
	case BE_CLOSE:
		if (!blocked (top))
			user_post (top->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
		break;
	case BE_MENU:
		if (!blocked (top))
			menu_command (top->hwnd, e->key);
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
	case BE_FOCUS:
		if (e->x && top->hwnd != active_hwnd && !blocked (top))
		{
			uint16_t prev = active_hwnd;
			active_hwnd = top->hwnd;
			user_send (top->hwnd, WM_ACTIVATE, 1, prev);
		}
		break;
	case BE_WHEEL:
	{
		int lx, ly;
		struct wnd *w = hit (top, mouse_x, mouse_y, &lx, &ly);
		for (; w; w = (w->style & WS_CHILD) ? wnd_get (w->parent) : 0)
			if (w->style & WS_VSCROLL || !(w->style & WS_CHILD))
				break;
		if (w)
			for (int i = 0; i < (e->key < 0 ? -e->key : e->key); i++)
				user_post (w->hwnd, WM_VSCROLL, e->key < 0 ? 0 : 1, 0);	/* SB_LINEUP, SB_LINEDOWN */
		break;
	}
	case BE_MOUSE_MOVE:
	case BE_MOUSE_DOWN:
	case BE_MOUSE_UP:
	{
		int lx = 0, ly = 0;
		mouse_x = e->x;
		mouse_y = e->y;
		mouse_be = e->win;
		struct wnd *w;
		if (capture_hwnd && wnd_get (capture_hwnd))
		{
			w = wnd_get (capture_hwnd);
			int ox, oy, cw, ch;
			long be;
			wnd_client_geometry (w->hwnd, &ox, &oy, &cw, &ch, &be);
			lx = e->x - ox;
			ly = e->y - oy;
		}
		else
			w = hit (top, e->x, e->y, &lx, &ly);
		int old = mouse_buttons;
		uint16_t msg = WM_MOUSEMOVE;
		if (e->type == BE_MOUSE_DOWN)
		{
			int b = e->button & ~old;
			if (!b)
				b = e->button ? e->button : 1;
			mouse_buttons = old | b;
			msg = (b & 1) ? WM_LBUTTONDOWN : (b & 2) ? WM_RBUTTONDOWN : WM_MBUTTONDOWN;
			uint32_t now = be_ticks ();
			if (msg == WM_LBUTTONDOWN && w->hwnd == last_click_hwnd && now - last_click_time < 450
				&& abs (lx - last_click_x) < 4 && abs (ly - last_click_y) < 4 && (classes[w->cls].style & 8))	/* CS_DBLCLKS */
			{
				msg = WM_LBUTTONDBLCLK;
				last_click_time = 0;
			}
			else if (msg == WM_LBUTTONDOWN)
			{
				last_click_time = now;
				last_click_hwnd = w->hwnd;
				last_click_x = lx;
				last_click_y = ly;
			}
		}
		else if (e->type == BE_MOUSE_UP)
		{
			int b = old & ~e->button;
			if (!b)
				b = old ? old : 1;
			mouse_buttons = old & ~b;
			msg = (b & 1) ? WM_LBUTTONUP : (b & 2) ? WM_RBUTTONUP : WM_MBUTTONUP;
		}
		if (is_disabled (w) || blocked (top))
			break;
		uint16_t mk = (uint16_t) ((mouse_buttons & 1 ? 1 : 0) | (mouse_buttons & 2 ? 2 : 0) | (mouse_buttons & 4 ? 0x10 : 0)
			| (e->mods & 2 ? 4 : 0) | (e->mods & 1 ? 8 : 0));
		user_post (w->hwnd, msg, mk, (uint32_t) (uint16_t) lx | (uint32_t) (uint16_t) ly << 16);
		break;
	}
	case BE_KEY_DOWN:
	case BE_KEY_UP:
	{
		uint16_t vk = hid_to_vk (e->key, e->ch);
		key_down[0x10] = (e->mods & 2) ? 0x80 : 0;
		key_down[0x11] = (e->mods & 1) ? 0x80 : 0;
		key_down[0x12] = (e->mods & 4) ? 0x80 : 0;
		if (!vk)
			break;
		key_down[vk & 0xff] = e->type == BE_KEY_DOWN ? 0x80 : 0;
		if (e->type == BE_KEY_DOWN)
		{
			int ch = e->ch;
			if ((e->mods & 1) && vk >= 'A' && vk <= 'Z')
				ch = vk - 'A' + 1;	/* (Ctrl+lettre : le caractère de contrôle, comme Windows) */
			uint8_t c8[4] = { 0 };
			if (ch >= 0x80)
			{
				char u[8] = { 0 };
				int n = 0;
				if (ch < 0x800) { u[n++] = (char) (0xc0 | ch >> 6); u[n++] = (char) (0x80 | (ch & 0x3f)); }
				else { u[n++] = (char) (0xe0 | ch >> 12); u[n++] = (char) (0x80 | ((ch >> 6) & 0x3f)); u[n++] = (char) (0x80 | (ch & 0x3f)); }
				w16_from_utf8 (u, c8, 4);
				ch = c8[0];
			}
			key_char[vk & 0xff] = (uint8_t) (ch > 0 && ch < 256 ? ch : 0);
		}
		if (blocked (top))
			break;
		uint16_t to = focus_hwnd && wnd_get (focus_hwnd) && wnd_top (wnd_get (focus_hwnd)) == top ? focus_hwnd : top->hwnd;
		int alt = e->mods & 4;
		user_post (to, e->type == BE_KEY_DOWN ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN) : (alt ? WM_SYSKEYUP : WM_KEYUP), vk,
			1u | (e->type == BE_KEY_UP ? 0xc0000000u : 0) | (alt ? 0x20000000u : 0));
		break;
	}
	}
}

/* ---- minuteries (SetTimer) ---- */
#define MAX_TIMERS 64
static struct { int used; uint16_t hwnd, id; uint32_t proc, ms, next; int pending; } timers[MAX_TIMERS];

static int timers_due (void)
{
	uint32_t now = be_ticks ();
	int got = 0;
	for (int i = 0; i < MAX_TIMERS; i++)
		if (timers[i].used && !timers[i].pending && (int32_t) (now - timers[i].next) >= 0)
		{
			timers[i].next = now + timers[i].ms;
			timers[i].pending = 1;
			user_post (timers[i].hwnd, WM_TIMER, timers[i].id, timers[i].proc);
			got = 1;
		}
	return got;
}

static int timers_wait_ms (int max)
{
	uint32_t now = be_ticks ();
	int best = max;
	for (int i = 0; i < MAX_TIMERS; i++)
		if (timers[i].used && !timers[i].pending)
		{
			int d = (int) (int32_t) (timers[i].next - now);
			if (d < best)
				best = d < 0 ? 0 : d;
		}
	int m = mm_next_ms ();
	if (m >= 0 && m < best)
		best = m;
	return best;
}

void api_SetTimer16 (struct w16call *c)
{
	uint16_t hwnd = (uint16_t) c->a[0], id = (uint16_t) c->a[1];
	uint32_t ms = c->a[2] ? c->a[2] : 1;
	int slot = -1;
	for (int i = 0; i < MAX_TIMERS; i++)
		if (timers[i].used && timers[i].hwnd == hwnd && (hwnd ? timers[i].id == id : 0))
			slot = i;
	for (int i = 0; i < MAX_TIMERS && slot < 0; i++)
		if (!timers[i].used)
			slot = i;
	if (slot < 0)
	{
		c->ret = 0;
		return;
	}
	if (!hwnd)
		id = (uint16_t) (slot + 1);
	timers[slot].used = 1;
	timers[slot].hwnd = hwnd;
	timers[slot].id = id;
	timers[slot].proc = c->a[3];
	timers[slot].ms = ms < 10 ? 10 : ms;
	timers[slot].next = be_ticks () + timers[slot].ms;
	timers[slot].pending = 0;
	c->ret = id ? id : 1;
}

void api_SetSystemTimer16 (struct w16call *c) { api_SetTimer16 (c); }

void api_KillTimer16 (struct w16call *c)
{
	c->ret = 0;
	for (int i = 0; i < MAX_TIMERS; i++)
		if (timers[i].used && timers[i].hwnd == (uint16_t) c->a[0] && timers[i].id == (uint16_t) c->a[1])
		{
			timers[i].used = 0;
			c->ret = 1;
		}
}

static int find_invalid (void)
{
	for (int i = 0; i < MAX_WND; i++)
		if (wnds[i].used && wnds[i].visible && wnds[i].invalid)
		{
			struct wnd *t = wnd_top (&wnds[i]);
			if (!t || !t->visible || !t->be)
				continue;
			/* (une fille ne se peint pas si un parent n'est pas visible) */
			struct wnd *p = &wnds[i];
			int ok = 1;
			while (p && (p->style & WS_CHILD))
			{
				p = wnd_get (p->parent);
				if (!p || !p->visible)
					ok = 0;
			}
			/* (les parents d'abord : ils effacent le fond) */
			struct wnd *q = (wnds[i].style & WS_CHILD) ? wnd_get (wnds[i].parent) : 0;
			if (q && q->invalid && q->visible)
				continue;
			if (ok)
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

static int msg_match (const struct qmsg *m, uint16_t hwnd, uint16_t lo, uint16_t hi)
{
	if (hwnd)
	{
		struct wnd *w = wnd_get (m->hwnd);
		int ok = m->hwnd == hwnd;
		while (!ok && w && (w->style & WS_CHILD))
		{
			ok = w->parent == hwnd;
			w = wnd_get (w->parent);
		}
		if (!ok)
			return 0;
	}
	if (lo || hi)
		return m->msg >= lo && m->msg <= hi;
	return 1;
}

/* prend un message : 1 si *m est rempli ; wait : attendre qu'il y en ait un */
int user_take (struct qmsg *m, uint16_t hwnd, uint16_t lo, uint16_t hi, int remove, int wait)
{
	for (;;)
	{
		menu_publish_pending ();
		mm_poll ();
		timers_due ();
		for (int i = qhead; i != qtail; i = (i + 1) % QMAX)
			if (msg_match (&queue[i], hwnd, lo, hi))
			{
				*m = queue[i];
				if (remove)
				{
					/* (retirer de la file en gardant l'ordre des autres) */
					for (int j = i; j != qhead; j = (j - 1 + QMAX) % QMAX)
						queue[j] = queue[(j - 1 + QMAX) % QMAX];
					qhead = (qhead + 1) % QMAX;
					if (m->msg == WM_TIMER)
						for (int t = 0; t < MAX_TIMERS; t++)
							if (timers[t].used && timers[t].hwnd == m->hwnd && timers[t].id == m->wp)
								timers[t].pending = 0;
				}
				last_msg_time = m->time;
				last_msg_x = m->x;
				last_msg_y = m->y;
				return 1;
			}
		if (quit_posted && !hwnd)
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
		if (i >= 0 && (!lo || (lo <= WM_PAINT && hi >= WM_PAINT)))
		{
			memset (m, 0, sizeof *m);
			m->hwnd = wnds[i].hwnd;
			m->msg = WM_PAINT;
			m->time = be_ticks ();
			return 1;
		}
		if (!wait)
			return 0;
		if (be_wait (timers_wait_ms (200), &e))
			translate_event (&e);
		if (w16_exit)
			return 0;
	}
}

void api_GetMessage16 (struct w16call *c)
{
	struct qmsg m;
	user_take (&m, (uint16_t) c->a[1], (uint16_t) c->a[2], (uint16_t) c->a[3], 1, 1);
	put_msg (c->a[0], &m);
	c->ret = m.msg != WM_QUIT;
}

void api_PeekMessage16 (struct w16call *c)
{
	struct qmsg m;
	int remove = (int) (c->a[4] & 1);
	if (!user_take (&m, (uint16_t) c->a[1], (uint16_t) c->a[2], (uint16_t) c->a[3], remove, 0))
	{
		c->ret = 0;
		return;
	}
	put_msg (c->a[0], &m);
	c->ret = 1;
}

void api_WaitMessage16 (struct w16call *c)
{
	(void) c;
	struct qmsg m;
	user_take (&m, 0, 0, 0, 0, 1);
}

void api_GetMessageTime (struct w16call *c) { (void) c; c->ret = last_msg_time; }
void api_GetMessagePos (struct w16call *c) { (void) c; c->ret = (uint32_t) (uint16_t) last_msg_x | (uint32_t) (uint16_t) last_msg_y << 16; }
void api_GetInputState16 (struct w16call *c) { (void) c; c->ret = qhead != qtail; }
void api_GetQueueStatus16 (struct w16call *c) { (void) c; c->ret = qhead != qtail ? 0xff : 0; }
void api_SetMessageQueue16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_TranslateMessage16 (struct w16call *c)
{
	uint32_t a = far_lin (c->a[0]);
	uint16_t msg = rd16 (a + 2), vk = rd16 (a + 4);
	c->ret = 0;
	if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && key_char[vk & 0xff])
	{
		user_post (rd16 (a), msg == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR, key_char[vk & 0xff], rd32 (a + 6));
		c->ret = 1;
	}
}

uint32_t user_dispatch (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp, uint32_t time)
{
	if (msg == WM_TIMER && lp)
	{
		uint16_t args5[5] = { hwnd, msg, wp, (uint16_t) (time >> 16), (uint16_t) time };
		return call16 (lp, args5, 5, ne_ds_for (lp));
	}
	uint32_t r = user_send (hwnd, msg, wp, lp);
	struct wnd *w = wnd_get (hwnd);
	if (msg == WM_PAINT && w && w->invalid)
		w->invalid = w->erase = 0;	/* (une procédure qui ne peint pas : Windows valide pour elle) */
	if (msg == WM_COMMAND)
		menu_init_popups (0);
	return r;
}

void api_DispatchMessage16 (struct w16call *c)
{
	uint32_t a = far_lin (c->a[0]);
	c->ret = user_dispatch (rd16 (a), rd16 (a + 2), rd16 (a + 4), rd32 (a + 6), rd32 (a + 10));
}

/* ======================================================================== focus, capture, activation */
uint16_t user_set_focus (uint16_t hwnd)
{
	uint16_t old = focus_hwnd;
	if (old == hwnd)
		return old;
	focus_hwnd = hwnd;
	if (old && wnd_get (old))
		user_send (old, WM_KILLFOCUS, hwnd, 0);
	if (hwnd && wnd_get (hwnd))
		user_send (hwnd, WM_SETFOCUS, old, 0);
	return old;
}

uint16_t user_get_focus (void) { return focus_hwnd; }
void user_capture (uint16_t hwnd) { capture_hwnd = hwnd; }

void api_SetFocus16 (struct w16call *c) { c->ret = user_set_focus ((uint16_t) c->a[0]); }
void api_GetFocus16 (struct w16call *c) { (void) c; c->ret = focus_hwnd; }
void api_SetCapture16 (struct w16call *c) { c->ret = capture_hwnd; capture_hwnd = (uint16_t) c->a[0]; }
void api_GetCapture16 (struct w16call *c) { (void) c; c->ret = capture_hwnd; }

void api_ReleaseCapture16 (struct w16call *c) { (void) c; capture_hwnd = 0; }
void api_GetActiveWindow16 (struct w16call *c) { (void) c; c->ret = active_hwnd; }

void api_SetActiveWindow16 (struct w16call *c)
{
	c->ret = active_hwnd;
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (w && !(w->style & WS_CHILD))
		active_hwnd = w->hwnd;
}

void api_BringWindowToTop16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_GetLastActivePopup16 (struct w16call *c) { c->ret = c->a[0]; }
void api_ShowOwnedPopups16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_IsIconic16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_IsZoomed16 (struct w16call *c) { (void) c; c->ret = 0; }

void api_EnableWindow16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
	{
		c->ret = 0;
		return;
	}
	int was = w->disabled;
	w->disabled = !c->a[1];
	if (was != w->disabled)
	{
		user_send (w->hwnd, WM_ENABLE, (uint16_t) !w->disabled, 0);
		invalidate (w, 0, 0, w->cw, w->ch, 1);
	}
	c->ret = (uint32_t) was;
}

void api_IsWindowEnabled16 (struct w16call *c) { struct wnd *w = wnd_get ((uint16_t) c->a[0]); c->ret = w && !w->disabled; }
void api_IsWindow16 (struct w16call *c) { c->ret = wnd_get ((uint16_t) c->a[0]) != 0; }
void api_IsWindowVisible16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int v = w && w->visible;
	while (v && w && (w->style & WS_CHILD))
	{
		w = wnd_get (w->parent);
		v = w && w->visible;
	}
	c->ret = (uint32_t) v;
}
void api_IsChild16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[1]);
	c->ret = 0;
	while (w && (w->style & WS_CHILD))
	{
		if (w->parent == (uint16_t) c->a[0])
		{
			c->ret = 1;
			return;
		}
		w = wnd_get (w->parent);
	}
}

void api_GetParent16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	c->ret = w ? ((w->style & WS_CHILD) ? w->parent : w->owner) : 0;
}

void api_SetParent16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
		return;
	c->ret = w->parent;
	w->parent = (uint16_t) c->a[1];
}

void api_GetDesktopWindow16 (struct w16call *c) { (void) c; c->ret = HWND_DESKTOP; }

void api_GetWindow16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int rel = (int) c->a[1];
	c->ret = 0;
	if (!w)
		return;
	uint16_t parent = (w->style & WS_CHILD) ? w->parent : 0;
	int idx = (int) (w - wnds);
	switch (rel)
	{
	case 0:	/* GW_HWNDFIRST */
	case 1:	/* GW_HWNDLAST */
		for (int i = 0; i < MAX_WND; i++)
		{
			int k = rel == 0 ? i : MAX_WND - 1 - i;
			if (wnds[k].used && ((wnds[k].style & WS_CHILD) ? wnds[k].parent : 0) == parent)
			{
				c->ret = wnds[k].hwnd;
				return;
			}
		}
		break;
	case 2:	/* GW_HWNDNEXT */
		for (int k = idx + 1; k < MAX_WND; k++)
			if (wnds[k].used && ((wnds[k].style & WS_CHILD) ? wnds[k].parent : 0) == parent)
			{
				c->ret = wnds[k].hwnd;
				return;
			}
		break;
	case 3:	/* GW_HWNDPREV */
		for (int k = idx - 1; k >= 0; k--)
			if (wnds[k].used && ((wnds[k].style & WS_CHILD) ? wnds[k].parent : 0) == parent)
			{
				c->ret = wnds[k].hwnd;
				return;
			}
		break;
	case 4:	/* GW_OWNER */
		c->ret = w->owner;
		break;
	case 5:	/* GW_CHILD */
		for (int k = 0; k < MAX_WND; k++)
			if (wnds[k].used && (wnds[k].style & WS_CHILD) && wnds[k].parent == w->hwnd)
			{
				c->ret = wnds[k].hwnd;
				return;
			}
		break;
	}
}

void api_GetTopWindow16 (struct w16call *c)
{
	struct w16call d = { .a = { c->a[0], 5 }, .n = 2 };
	if (!c->a[0])
	{
		c->ret = active_hwnd;
		return;
	}
	api_GetWindow16 (&d);
	c->ret = d.ret;
}

void api_GetNextWindow16 (struct w16call *c)
{
	struct w16call d = { .a = { c->a[0], c->a[1] == 2 ? 2 : 3 }, .n = 2 };
	api_GetWindow16 (&d);
	c->ret = d.ret;
}

static void enum_call (uint32_t proc, uint16_t hwnd, uint32_t lp, int *go)
{
	uint16_t args[3] = { hwnd, (uint16_t) (lp >> 16), (uint16_t) lp };
	*go = (int) (call16 (proc, args, 3, ne_ds_for (proc)) & 0xffff);
}

void api_EnumWindows16 (struct w16call *c)
{
	int go = 1;
	for (int i = 0; i < MAX_WND && go; i++)
		if (wnds[i].used && !(wnds[i].style & WS_CHILD))
			enum_call (c->a[0], wnds[i].hwnd, c->a[1], &go);
	c->ret = 1;
}

void api_EnumTaskWindows16 (struct w16call *c)
{
	int go = 1;
	for (int i = 0; i < MAX_WND && go; i++)
		if (wnds[i].used && !(wnds[i].style & WS_CHILD))
			enum_call (c->a[1], wnds[i].hwnd, c->a[2], &go);
	c->ret = 1;
}

void api_EnumChildWindows16 (struct w16call *c)
{
	int go = 1;
	uint16_t parent = (uint16_t) c->a[0];
	for (int i = 0; i < MAX_WND && go; i++)
	{
		struct wnd *w = &wnds[i];
		if (!w->used || !(w->style & WS_CHILD))
			continue;
		struct wnd *p = w;
		int under = 0;
		while (p && (p->style & WS_CHILD) && !under)
		{
			under = p->parent == parent;
			p = wnd_get (p->parent);
		}
		if (under)
			enum_call (c->a[1], w->hwnd, c->a[2], &go);
	}
	c->ret = 1;
}

void api_FindWindow16 (struct w16call *c)
{
	c->ret = 0;
	for (int i = 0; i < MAX_WND; i++)
	{
		struct wnd *w = &wnds[i];
		if (!w->used || (w->style & WS_CHILD))
			continue;
		if (c->a[0] && find_class (c->a[0]) != w->cls)
			continue;
		if (c->a[1] && strcmp (w16_raw (c->a[1]), w->text))
			continue;
		c->ret = w->hwnd;
		return;
	}
}

void api_WindowFromPoint16 (struct w16call *c)
{
	struct wnd *top = by_be (mouse_be);
	int lx, ly;
	c->ret = top ? hit (top, mouse_x, mouse_y, &lx, &ly)->hwnd : 0;
}

void api_ChildWindowFromPoint16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int x = (int16_t) c->a[1], y = (int16_t) c->a[2];
	c->ret = w ? w->hwnd : 0;
	for (int i = 0; w && i < MAX_WND; i++)
	{
		struct wnd *k = &wnds[i];
		if (k->used && (k->style & WS_CHILD) && k->parent == w->hwnd && x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h)
			c->ret = k->hwnd;
	}
}

void api_GetCursorPos16 (struct w16call *c)
{
	struct wnd *top = by_be (mouse_be);
	int x = mouse_x, y = mouse_y;
	if (top)
		map_point (top->hwnd, 0, &x, &y);
	uint32_t p = far_lin (c->a[0]);
	wr16 (p, (uint16_t) x);
	wr16 (p + 2, (uint16_t) y);
}

void api_SetCursorPos16 (struct w16call *c) { (void) c; }

/* ---- position et taille ---- */
static void move_window (struct wnd *w, int x, int y, int cw, int ch, int repaint)
{
	int moved = x != w->x || y != w->y, sized = cw != w->w || ch != w->h;
	struct wnd *p = (w->style & WS_CHILD) ? wnd_get (w->parent) : 0;
	if (p && w->visible)
		invalidate_rect_tree (p, w->x, w->y, w->x + w->w, w->y + w->h, 1);
	w->x = x;
	w->y = y;
	w->w = cw;
	w->h = ch;
	wnd_geometry (w);
	if (!(w->style & WS_CHILD) && w->be && sized)
		be_window_resize (w->be, w->cw, w->ch);
	uint16_t h = w->hwnd;
	if (sized)
		user_send (h, WM_SIZE, 0, (uint32_t) w->cw | (uint32_t) w->ch << 16);
	w = wnd_get (h);
	if (w && moved)
		user_send (h, WM_MOVE, 0, (uint32_t) (uint16_t) x | (uint32_t) (uint16_t) y << 16);
	w = wnd_get (h);
	if (w && (repaint || sized))
		invalidate_tree (w);
}

void api_MoveWindow16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (w)
		move_window (w, (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3], (int16_t) c->a[4], (int) c->a[5]);
	c->ret = w != 0;
}

void api_SetWindowPos16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	uint16_t f = (uint16_t) c->a[6];
	if (!w)
	{
		c->ret = 0;
		return;
	}
	int x = (f & 2) ? w->x : (int16_t) c->a[2], y = (f & 2) ? w->y : (int16_t) c->a[3];	/* SWP_NOMOVE */
	int cw = (f & 1) ? w->w : (int16_t) c->a[4], ch = (f & 1) ? w->h : (int16_t) c->a[5];	/* SWP_NOSIZE */
	if (!(f & 3) || (x != w->x || y != w->y || cw != w->w || ch != w->h))
		move_window (w, x, y, cw, ch, !(f & 8));
	if (f & 0x40)	/* SWP_SHOWWINDOW */
	{
		struct w16call s = { .a = { w->hwnd, 1 }, .n = 2 };
		api_ShowWindow16 (&s);
	}
	else if (f & 0x80)	/* SWP_HIDEWINDOW */
	{
		struct w16call s = { .a = { w->hwnd, 0 }, .n = 2 };
		api_ShowWindow16 (&s);
	}
	c->ret = 1;
}

/* WINDOWPLACEMENT 16 bits : length, flags, showCmd, ptMin, ptMax, rcNormal */
void api_GetWindowPlacement16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	uint32_t p = far_lin (c->a[1]);
	if (!w)
	{
		c->ret = 0;
		return;
	}
	wr16 (p, 22);
	wr16 (p + 2, 0);
	wr16 (p + 4, (uint16_t) (w->visible ? 1 : 0));
	wr32 (p + 6, 0xffffffff);
	wr32 (p + 10, 0xffffffff);
	put_rect (c->a[1] + 14, w->x < 0 ? 0 : w->x, w->y < 0 ? 0 : w->y, (w->x < 0 ? 0 : w->x) + w->w, (w->y < 0 ? 0 : w->y) + w->h);
	c->ret = 1;
}

void api_SetWindowPlacement16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
	{
		c->ret = 0;
		return;
	}
	int r[4];
	get_rect (c->a[1] + 14, r);
	move_window (w, r[0], r[1], r[2] - r[0], r[3] - r[1], 1);
	uint16_t show = rd16 (far_lin (c->a[1]) + 4);
	struct w16call s = { .a = { w->hwnd, show }, .n = 2 };
	api_ShowWindow16 (&s);
	c->ret = 1;
}

/* ---- texte, mots et longs de la fenêtre ---- */
void api_SetWindowText16 (struct w16call *c) { user_send ((uint16_t) c->a[0], WM_SETTEXT, 0, c->a[1]); }
void api_GetWindowText16 (struct w16call *c) { c->ret = user_send ((uint16_t) c->a[0], WM_GETTEXT, (uint16_t) c->a[2], c->a[1]); }
void api_GetWindowTextLength16 (struct w16call *c) { c->ret = user_send ((uint16_t) c->a[0], WM_GETTEXTLENGTH, 0, 0); }

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
		c->ret = (w->style & WS_CHILD) ? w->parent : w->owner;
	else if (off == -12)	/* GWW_ID */
		c->ret = w->id;
}

void api_SetWindowWord16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int16_t off = (int16_t) c->a[1];
	c->ret = 0;
	if (!w)
		return;
	if (off >= 0 && off + 2 <= (int) sizeof w->extra)
	{
		c->ret = (uint32_t) (w->extra[off] | w->extra[off + 1] << 8);
		w->extra[off] = (uint8_t) c->a[2];
		w->extra[off + 1] = (uint8_t) (c->a[2] >> 8);
	}
	else if (off == -12)
	{
		c->ret = w->id;
		w->id = (uint16_t) c->a[2];
	}
	else if (off == -6)
	{
		c->ret = w->hinst;
		w->hinst = (uint16_t) c->a[2];
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
		c->ret = w->style | (w->visible ? WS_VISIBLE : 0) | (w->disabled ? WS_DISABLED : 0);
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
	else if (off == -20)
	{
		c->ret = w->exstyle;
		w->exstyle = v;
	}
}

/* ---- propriétés ---- */
static int prop_find (struct wnd *w, uint32_t name)
{
	for (int i = 0; i < w->nprops; i++)
		if (!SELECTOROF (name) ? (w->props[i].atom && w->props[i].atom == OFFSETOF (name))
			: (!w->props[i].atom && !strcasecmp (w->props[i].name, w16_raw (name))))
			return i;
	return -1;
}

void api_SetProp16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	c->ret = 0;
	if (!w)
		return;
	int i = prop_find (w, c->a[1]);
	if (i < 0)
	{
		if (w->nprops >= 16)
			return;
		i = w->nprops++;
		w->props[i].atom = SELECTOROF (c->a[1]) ? 0 : OFFSETOF (c->a[1]);
		snprintf (w->props[i].name, sizeof w->props[i].name, "%s", SELECTOROF (c->a[1]) ? w16_raw (c->a[1]) : "");
	}
	w->props[i].value = (uint16_t) c->a[2];
	c->ret = 1;
}

void api_GetProp16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int i = w ? prop_find (w, c->a[1]) : -1;
	c->ret = i >= 0 ? w->props[i].value : 0;
}

void api_RemoveProp16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int i = w ? prop_find (w, c->a[1]) : -1;
	c->ret = 0;
	if (i < 0)
		return;
	c->ret = w->props[i].value;
	w->props[i] = w->props[--w->nprops];
}

/* ---- barres de défilement d'une fenêtre (dessinées par la fenêtre elle-même : pas encore) ---- */
static int sb_index (struct w16call *c) { return c->a[1] == 1 ? 1 : 0; }	/* SB_VERT */

void api_SetScrollPos16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
		return;
	if (!strcasecmp (w->clsname, "SCROLLBAR"))
	{
		c->ret = user_send (w->hwnd, 0x0400 + 0x60, (uint16_t) c->a[2], c->a[3]);	/* (ctrl.c : SBM_SETPOS) */
		return;
	}
	int i = sb_index (c);
	c->ret = (uint32_t) w->sb_pos[i];
	w->sb_pos[i] = (int16_t) c->a[2];
}

void api_GetScrollPos16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (w && !strcasecmp (w->clsname, "SCROLLBAR"))
	{
		c->ret = user_send (w->hwnd, 0x0400 + 0x61, 0, 0);
		return;
	}
	c->ret = w ? (uint32_t) (uint16_t) w->sb_pos[sb_index (c)] : 0;
}

void api_SetScrollRange16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w)
		return;
	if (!strcasecmp (w->clsname, "SCROLLBAR"))
	{
		user_send (w->hwnd, 0x0400 + 0x62, (uint16_t) c->a[2], (uint16_t) c->a[3]);
		return;
	}
	int i = sb_index (c);
	w->sb_min[i] = (int16_t) c->a[2];
	w->sb_max[i] = (int16_t) c->a[3];
}

void api_GetScrollRange16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int mn = 0, mx = 0;
	if (w && !strcasecmp (w->clsname, "SCROLLBAR"))
	{
		uint32_t r = user_send (w->hwnd, 0x0400 + 0x63, 0, 0);
		mn = (int16_t) r;
		mx = (int16_t) (r >> 16);
	}
	else if (w)
	{
		mn = w->sb_min[sb_index (c)];
		mx = w->sb_max[sb_index (c)];
	}
	if (c->a[2])
		wr16 (far_lin (c->a[2]), (uint16_t) mn);
	if (c->a[3])
		wr16 (far_lin (c->a[3]), (uint16_t) mx);
}

void api_ShowScrollBar16 (struct w16call *c) { (void) c; }
void api_EnableScrollBar16 (struct w16call *c) { (void) c; c->ret = 1; }

/* ======================================================================== rectangles */
void api_SetRect16 (struct w16call *c) { put_rect (c->a[0], (int16_t) c->a[1], (int16_t) c->a[2], (int16_t) c->a[3], (int16_t) c->a[4]); }
void api_SetRectEmpty16 (struct w16call *c) { put_rect (c->a[0], 0, 0, 0, 0); }
void api_CopyRect16 (struct w16call *c) { int r[4]; get_rect (c->a[1], r); put_rect (c->a[0], r[0], r[1], r[2], r[3]); c->ret = 1; }
void api_IsRectEmpty16 (struct w16call *c) { int r[4]; get_rect (c->a[0], r); c->ret = r[2] <= r[0] || r[3] <= r[1]; }

void api_PtInRect16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[0], r);
	int x = (int16_t) c->a[1], y = (int16_t) (c->a[1] >> 16);
	c->ret = x >= r[0] && x < r[2] && y >= r[1] && y < r[3];
}

void api_OffsetRect16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[0], r);
	int dx = (int16_t) c->a[1], dy = (int16_t) c->a[2];
	put_rect (c->a[0], r[0] + dx, r[1] + dy, r[2] + dx, r[3] + dy);
}

void api_InflateRect16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[0], r);
	int dx = (int16_t) c->a[1], dy = (int16_t) c->a[2];
	put_rect (c->a[0], r[0] - dx, r[1] - dy, r[2] + dx, r[3] + dy);
}

void api_IntersectRect16 (struct w16call *c)
{
	int a[4], b[4];
	get_rect (c->a[1], a);
	get_rect (c->a[2], b);
	int l = a[0] > b[0] ? a[0] : b[0], t = a[1] > b[1] ? a[1] : b[1], r = a[2] < b[2] ? a[2] : b[2], bt = a[3] < b[3] ? a[3] : b[3];
	if (r <= l || bt <= t)
	{
		put_rect (c->a[0], 0, 0, 0, 0);
		c->ret = 0;
		return;
	}
	put_rect (c->a[0], l, t, r, bt);
	c->ret = 1;
}

void api_UnionRect16 (struct w16call *c)
{
	int a[4], b[4];
	get_rect (c->a[1], a);
	get_rect (c->a[2], b);
	int ea = a[2] <= a[0] || a[3] <= a[1], eb = b[2] <= b[0] || b[3] <= b[1];
	if (ea && eb)
	{
		put_rect (c->a[0], 0, 0, 0, 0);
		c->ret = 0;
		return;
	}
	if (ea)
		memcpy (a, b, sizeof a);
	else if (!eb)
	{
		if (b[0] < a[0]) a[0] = b[0];
		if (b[1] < a[1]) a[1] = b[1];
		if (b[2] > a[2]) a[2] = b[2];
		if (b[3] > a[3]) a[3] = b[3];
	}
	put_rect (c->a[0], a[0], a[1], a[2], a[3]);
	c->ret = 1;
}

void api_EqualRect16 (struct w16call *c) { int a[4], b[4]; get_rect (c->a[0], a); get_rect (c->a[1], b); c->ret = !memcmp (a, b, sizeof a); }

void api_SubtractRect16 (struct w16call *c)
{
	int a[4];
	get_rect (c->a[1], a);
	put_rect (c->a[0], a[0], a[1], a[2], a[3]);
	c->ret = 1;
}

void api_FillRect16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[1], r);
	gdi_fill_rect_brush ((uint16_t) c->a[0], r[0], r[1], r[2], r[3], (uint16_t) c->a[2]);
	c->ret = 1;
}

void api_FrameRect16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[1], r);
	uint16_t hdc = (uint16_t) c->a[0], br = (uint16_t) c->a[2];
	gdi_fill_rect_brush (hdc, r[0], r[1], r[2], r[1] + 1, br);
	gdi_fill_rect_brush (hdc, r[0], r[3] - 1, r[2], r[3], br);
	gdi_fill_rect_brush (hdc, r[0], r[1], r[0] + 1, r[3], br);
	gdi_fill_rect_brush (hdc, r[2] - 1, r[1], r[2], r[3], br);
	c->ret = 1;
}

void api_InvertRect16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[1], r);
	gdi_invert ((uint16_t) c->a[0], r[0], r[1], r[2], r[3]);
}

void api_DrawFocusRect16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[1], r);
	uint16_t hdc = (uint16_t) c->a[0];
	for (int x = r[0]; x < r[2]; x += 2)
	{
		gdi_invert (hdc, x, r[1], x + 1, r[1] + 1);
		gdi_invert (hdc, x, r[3] - 1, x + 1, r[3]);
	}
	for (int y = r[1] + 2; y < r[3] - 1; y += 2)
	{
		gdi_invert (hdc, r[0], y, r[0] + 1, y + 1);
		gdi_invert (hdc, r[2] - 1, y, r[2], y + 1);
	}
}

void api_DrawText16 (struct w16call *c)
{
	int r[4];
	get_rect (c->a[3], r);
	int n = (int16_t) c->a[2];
	c->ret = (uint32_t) gdi_draw_text ((uint16_t) c->a[0], far_ptr (c->a[1]), n, r, (unsigned) c->a[4]);
	if (c->a[4] & 0x400)
		put_rect (c->a[3], r[0], r[1], r[2], r[3]);
}

void api_GrayString16 (struct w16call *c)
{
	/* (sans procédure : le texte en gris) */
	uint16_t hdc = (uint16_t) c->a[0];
	if (!c->a[2])
	{
		int n = (int16_t) c->a[4];
		const char *s = far_ptr (c->a[3]);
		gdi_text (hdc, (int16_t) c->a[5], (int16_t) c->a[6], s, n ? n : -1, be_theme (BT_TEXT3), 0);
	}
	c->ret = 1;
}

void api_DrawIcon16 (struct w16call *c) { gdi_icon_draw ((uint16_t) c->a[0], (int16_t) c->a[1], (int16_t) c->a[2], (uint16_t) c->a[3]); c->ret = 1; }

/* ======================================================================== ressources de USER */
void api_LoadIcon16 (struct w16call *c)
{
	uint16_t h = c->a[0] ? gdi_icon_load ((uint16_t) c->a[0], c->a[1]) : 0;
	c->ret = h ? h : 0x7f00;
}

void api_LoadCursor16 (struct w16call *c) { (void) c; c->ret = 0x7f01; }
void api_SetCursor16 (struct w16call *c) { (void) c; c->ret = 0x7f01; }
void api_GetCursor16 (struct w16call *c) { (void) c; c->ret = 0x7f01; }
void api_ShowCursor16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_CreateIcon16 (struct w16call *c) { (void) c; c->ret = 0x7f00; }
void api_DestroyIcon16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_DestroyCursor16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_LoadBitmap16 (struct w16call *c)
{
	uint32_t size;
	const uint8_t *dib = c->a[0] ? kernel_resource ((uint16_t) c->a[0], 2, c->a[1], &size) : 0;	/* RT_BITMAP */
	c->ret = dib ? gdi_bitmap_from_dib (dib, size) : 0;
	if (!c->ret)
		w16_log ("   LoadBitmap %s introuvable\n", w16_str (c->a[1]));
}

void api_LoadString16 (struct w16call *c)
{
	uint16_t id = (uint16_t) c->a[1];
	int n = (int16_t) c->a[3];
	uint32_t size;
	const uint8_t *p = kernel_resource ((uint16_t) c->a[0], 6, (uint32_t) ((id >> 4) + 1), &size);	/* RT_STRING */
	c->ret = 0;
	if (n > 0 && c->a[2])
		((char *) far_ptr (c->a[2]))[0] = 0;
	if (!p)
		return;
	for (int i = 0; i < (id & 15); i++)
		p += 1 + p[0];
	int l = p[0];
	if (n <= 0)
		return;
	if (l > n - 1)
		l = n - 1;
	memcpy (far_ptr (c->a[2]), p + 1, (size_t) l);
	((char *) far_ptr (c->a[2]))[l] = 0;
	c->ret = (uint32_t) l;
}

/* ======================================================================== atomes */
#define MAX_ATOMS 512
static struct { char name[64]; int refs; } atoms[MAX_ATOMS];

static uint16_t atom_add (const char *s)
{
	if (s[0] == '#')
		return (uint16_t) atoi (s + 1);
	int fr = -1;
	for (int i = 0; i < MAX_ATOMS; i++)
	{
		if (atoms[i].refs && !strcasecmp (atoms[i].name, s))
		{
			atoms[i].refs++;
			return (uint16_t) (0xc000 + 0x100 + i);
		}
		if (!atoms[i].refs && fr < 0)
			fr = i;
	}
	if (fr < 0)
		return 0;
	snprintf (atoms[fr].name, sizeof atoms[fr].name, "%s", s);
	atoms[fr].refs = 1;
	return (uint16_t) (0xc000 + 0x100 + fr);
}

static int atom_index (uint16_t a) { int i = a - 0xc100; return i >= 0 && i < MAX_ATOMS && atoms[i].refs ? i : -1; }

void api_GlobalAddAtom16 (struct w16call *c) { c->ret = SELECTOROF (c->a[0]) ? atom_add (w16_raw (c->a[0])) : OFFSETOF (c->a[0]); }
void api_AddAtom16 (struct w16call *c) { api_GlobalAddAtom16 (c); }

void api_GlobalFindAtom16 (struct w16call *c)
{
	const char *s = w16_raw (c->a[0]);
	c->ret = 0;
	for (int i = 0; i < MAX_ATOMS; i++)
		if (atoms[i].refs && !strcasecmp (atoms[i].name, s))
			c->ret = (uint32_t) (0xc100 + i);
}

void api_FindAtom16 (struct w16call *c) { api_GlobalFindAtom16 (c); }

void api_GlobalDeleteAtom16 (struct w16call *c)
{
	int i = atom_index ((uint16_t) c->a[0]);
	if (i >= 0)
		atoms[i].refs--;
	c->ret = 0;
}

void api_DeleteAtom16 (struct w16call *c) { api_GlobalDeleteAtom16 (c); }

void api_GlobalGetAtomName16 (struct w16call *c)
{
	int i = atom_index ((uint16_t) c->a[0]);
	int n = (int16_t) c->a[2];
	char tmp[16];
	const char *s = i >= 0 ? atoms[i].name : (snprintf (tmp, sizeof tmp, "#%u", (unsigned) c->a[0]), tmp);
	int l = (int) strlen (s);
	if (n <= 0)
	{
		c->ret = 0;
		return;
	}
	if (l > n - 1)
		l = n - 1;
	memcpy (far_ptr (c->a[1]), s, (size_t) l);
	((char *) far_ptr (c->a[1]))[l] = 0;
	c->ret = (uint32_t) l;
}

void api_GetAtomName16 (struct w16call *c) { api_GlobalGetAtomName16 (c); }
void api_RegisterWindowMessageA (struct w16call *c) { c->ret = atom_add (w16_raw (c->a[0])); }
void api_RegisterClipboardFormat16 (struct w16call *c) { c->ret = atom_add (w16_raw (c->a[0])); }

/* ======================================================================== presse-papiers
 * Tous les formats sont gardés ici (format -> bloc global, comme Windows : le bloc appartient au presse-papiers) ;
 * le texte (CF_TEXT) est aussi donné au presse-papiers de Korin, et repris de lui s'il a changé ailleurs. */
#define MAX_CLIPF 16
static uint16_t clip_owner;
static struct { uint16_t fmt, h; } clipf[MAX_CLIPF];
static int nclipf;
static char clip_last[8192];	/* le texte que nous avons donné à Korin */

static int clip_find (uint16_t fmt)
{
	for (int i = 0; i < nclipf; i++)
		if (clipf[i].fmt == fmt)
			return i;
	return -1;
}

/* le texte de Korin, s'il n'est pas le nôtre, remplace le contenu (un copier fait dans une autre app) */
static void clip_sync (void)
{
	char buf[8192];
	int n = be_clip_take (buf, sizeof buf);
	if (n <= 0 || !strcmp (buf, clip_last))
		return;
	for (int i = 0; i < nclipf; i++)
		global_free (clipf[i].h | 7);
	nclipf = 0;
	snprintf (clip_last, sizeof clip_last, "%s", buf);
	uint8_t raw[8192];
	int l = w16_from_utf8 (buf, raw, sizeof raw);
	uint16_t s = global_alloc ((uint32_t) l + 1, 0);
	memcpy (lin_ptr (sel_base (s)), raw, (size_t) l + 1);
	clipf[nclipf].fmt = 1;
	clipf[nclipf++].h = s;
}

void api_OpenClipboard16 (struct w16call *c) { clip_owner = (uint16_t) c->a[0]; c->ret = 1; }
void api_CloseClipboard16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_EmptyClipboard16 (struct w16call *c)
{
	(void) c;
	for (int i = 0; i < nclipf; i++)
		global_free (clipf[i].h | 7);
	nclipf = 0;
	c->ret = 1;
}

void api_SetClipboardData16 (struct w16call *c)
{
	uint16_t fmt = (uint16_t) c->a[0], h = (uint16_t) c->a[1];
	c->ret = h;
	int i = clip_find (fmt);
	if (i < 0)
	{
		if (nclipf >= MAX_CLIPF)
		{
			c->ret = 0;
			return;
		}
		i = nclipf++;
	}
	else if (clipf[i].h && clipf[i].h != h)
		global_free (clipf[i].h | 7);
	clipf[i].fmt = fmt;
	clipf[i].h = h;
	if (fmt == 1 && h && sel_valid (h | 7))	/* CF_TEXT */
	{
		w16_to_utf8 (lin_ptr (sel_base (h | 7)), -1, clip_last, sizeof clip_last);
		be_clip_put (clip_last);
	}
}

void api_GetClipboardData16 (struct w16call *c)
{
	clip_sync ();
	int i = clip_find ((uint16_t) c->a[0]);
	c->ret = i >= 0 ? clipf[i].h : 0;
}

void api_IsClipboardFormatAvailable16 (struct w16call *c)
{
	clip_sync ();
	c->ret = clip_find ((uint16_t) c->a[0]) >= 0;
}

void api_EnumClipboardFormats16 (struct w16call *c)
{
	clip_sync ();
	int i = c->a[0] ? clip_find ((uint16_t) c->a[0]) + 1 : 0;
	c->ret = i >= 0 && i < nclipf ? clipf[i].fmt : 0;
}

void api_CountClipboardFormats16 (struct w16call *c) { (void) c; clip_sync (); c->ret = (uint32_t) nclipf; }
void api_GetClipboardOwner16 (struct w16call *c) { (void) c; c->ret = clip_owner; }

/* ======================================================================== chaînes */
void api_AnsiUpper16 (struct w16call *c)
{
	if (!SELECTOROF (c->a[0]))
	{
		c->ret = (uint32_t) toupper ((int) (c->a[0] & 0xff));
		return;
	}
	for (char *p = far_ptr (c->a[0]); *p; p++)
		*p = (char) toupper ((unsigned char) *p);
	c->ret = c->a[0];
}

void api_AnsiLower16 (struct w16call *c)
{
	if (!SELECTOROF (c->a[0]))
	{
		c->ret = (uint32_t) tolower ((int) (c->a[0] & 0xff));
		return;
	}
	for (char *p = far_ptr (c->a[0]); *p; p++)
		*p = (char) tolower ((unsigned char) *p);
	c->ret = c->a[0];
}

void api_AnsiUpperBuff16 (struct w16call *c)
{
	char *p = far_ptr (c->a[0]);
	int n = (int) (c->a[1] ? c->a[1] : 65536);
	for (int i = 0; i < n; i++)
		p[i] = (char) toupper ((unsigned char) p[i]);
	c->ret = (uint32_t) n;
}

void api_AnsiLowerBuff16 (struct w16call *c)
{
	char *p = far_ptr (c->a[0]);
	int n = (int) (c->a[1] ? c->a[1] : 65536);
	for (int i = 0; i < n; i++)
		p[i] = (char) tolower ((unsigned char) p[i]);
	c->ret = (uint32_t) n;
}

void api_AnsiNext16 (struct w16call *c) { const char *p = far_ptr (c->a[0]); c->ret = *p ? c->a[0] + 1 : c->a[0]; }
void api_AnsiPrev16 (struct w16call *c) { c->ret = c->a[1] > c->a[0] ? c->a[1] - 1 : c->a[0]; }
void api_IsCharAlphaA (struct w16call *c) { int ch = (int) (c->a[0] & 0xff); c->ret = isalpha (ch) || ch >= 0xc0; }
void api_IsCharAlphaNumericA (struct w16call *c) { int ch = (int) (c->a[0] & 0xff); c->ret = isalnum (ch) || ch >= 0xc0; }
void api_IsCharUpperA (struct w16call *c) { int ch = (int) (c->a[0] & 0xff); c->ret = isupper (ch) || (ch >= 0xc0 && ch < 0xe0); }
void api_IsCharLowerA (struct w16call *c) { int ch = (int) (c->a[0] & 0xff); c->ret = islower (ch) || ch >= 0xe0; }

/* wsprintf (cdecl, arguments variables) et wvsprintf : d, i, u, x, X, c, s (pointeurs longs), l, -, 0, largeur */
static int do_wsprintf (char *out, int cap, const char *fmt, uint32_t args)
{
	int o = 0;
	for (const char *p = fmt; *p && o < cap - 1; p++)
	{
		if (*p != '%')
		{
			out[o++] = *p;
			continue;
		}
		p++;
		if (*p == '%')
		{
			out[o++] = '%';
			continue;
		}
		char spec[16] = "%";
		int k = 1;
		while ((*p == '-' || *p == '0' || *p == '#') && k < 6)
			spec[k++] = *p++;
		int width = 0;
		while (*p >= '0' && *p <= '9')
			width = width * 10 + (*p++ - '0');
		int prec = -1;
		if (*p == '.')
		{
			p++;
			prec = 0;
			while (*p >= '0' && *p <= '9')
				prec = prec * 10 + (*p++ - '0');
		}
		int lng = 0;
		if (*p == 'l' || *p == 'L')
		{
			lng = 1;
			p++;
		}
		char conv = *p;
		char tmp[512];
		spec[k] = 0;
		char f[32];
		switch (conv)
		{
		case 'd':
		case 'i':
		{
			int32_t v = lng ? (int32_t) rd32 (args) : (int16_t) rd16 (args);
			args += lng ? 4 : 2;
			snprintf (f, sizeof f, "%s%d%sd", spec, width, prec >= 0 ? "" : "");
			snprintf (tmp, sizeof tmp, f, v);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		{
			uint32_t v = lng ? rd32 (args) : rd16 (args);
			args += lng ? 4 : 2;
			snprintf (f, sizeof f, "%s%d%c", spec, width, conv);
			snprintf (tmp, sizeof tmp, f, v);
			break;
		}
		case 'c':
			tmp[0] = (char) rd16 (args);
			tmp[1] = 0;
			args += 2;
			break;
		case 's':
		{
			uint32_t sp = rd32 (args);
			args += 4;
			const char *s = sp ? (const char *) far_ptr (sp) : "(null)";
			snprintf (f, sizeof f, "%s%d.%ds", spec, width, prec >= 0 ? prec : 400);
			snprintf (tmp, sizeof tmp, f, s);
			break;
		}
		default:
			tmp[0] = 0;
			break;
		}
		for (int i = 0; tmp[i] && o < cap - 1; i++)
			out[o++] = tmp[i];
		if (!*p)
			break;
	}
	out[o] = 0;
	return o;
}

void api_wsprintf16 (struct w16call *c)
{
	(void) c;
	/* (cdecl : sur la pile, après l'adresse de retour : lpOut, lpFmt, puis les arguments) */
	uint32_t sp = cpu_sreg_base (S_SS) + (uint16_t) cpu_reg (R_ESP);
	uint32_t out = rd32 (sp + 4), fmt = rd32 (sp + 8);
	char buf[1024];
	int n = do_wsprintf (buf, sizeof buf, w16_raw (fmt), sp + 12);
	memcpy (far_ptr (out), buf, (size_t) n + 1);
	c->ret = (uint32_t) n;
}

void api_wvsprintf16 (struct w16call *c)
{
	char buf[1024];
	int n = do_wsprintf (buf, sizeof buf, w16_raw (c->a[1]), far_lin (c->a[2]));
	memcpy (far_ptr (c->a[0]), buf, (size_t) n + 1);
	c->ret = (uint32_t) n;
}

/* ======================================================================== divers */
void api_InitApp16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_MessageBox16 (struct w16call *c)
{
	char text[1024], title[256];
	snprintf (text, sizeof text, "%s", w16_str (c->a[1]));
	snprintf (title, sizeof title, "%s", c->a[2] ? w16_str (c->a[2]) : "Erreur");
	c->ret = (uint32_t) be_message (title, text, (int) c->a[3]);
}

void api_MessageBeep16 (struct w16call *c) { (void) c; }
void api_GetSysColor16 (struct w16call *c) { c->ret = user_sys_color ((int) c->a[0]); }
void api_SetSysColors16 (struct w16call *c) { (void) c; }

void api_GetSystemMetrics16 (struct w16call *c)
{
	static const int16_t m[] = { 640, 480, 17, 17, 20, 1, 1, 4, 4, 17, 17, 32, 32, 32, 32, 18, 640, 460, 0, 1,
		17, 17, 17, 0, 0, 0, 0, 16, 16, 100, 26, 20, 4, 4, 4, 4, 4, 4, 32, 32, 0, 0, 0, 0 };
	int i = (int16_t) c->a[0];
	c->ret = (i >= 0 && i < (int) (sizeof m / sizeof m[0])) ? (uint16_t) m[i] : 0;
}

void api_GetKeyState16 (struct w16call *c) { c->ret = key_down[c->a[0] & 0xff] ? 0xff80 : 0; }
void api_GetAsyncKeyState16 (struct w16call *c) { c->ret = key_down[c->a[0] & 0xff] ? 0x8000 : 0; }
void api_GetKeyboardState16 (struct w16call *c) { memcpy (far_ptr (c->a[0]), key_down, 256); }
void api_SetKeyboardState16 (struct w16call *c) { memcpy (key_down, far_ptr (c->a[0]), 256); }
void api_GetDoubleClickTime16 (struct w16call *c) { (void) c; c->ret = 450; }
void api_GetCaretBlinkTime16 (struct w16call *c) { (void) c; c->ret = 530; }
void api_CreateCaret16 (struct w16call *c) { (void) c; }
void api_DestroyCaret16 (struct w16call *c) { (void) c; }
void api_ShowCaret16 (struct w16call *c) { (void) c; }
void api_HideCaret16 (struct w16call *c) { (void) c; }
void api_SetCaretPos16 (struct w16call *c) { (void) c; }

/* crochets (SetWindowsHook) : acceptés, jamais appelés */
void api_SetWindowsHook16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_SetWindowsHookEx16 (struct w16call *c) { (void) c; c->ret = 0x5a5a; }
void api_UnhookWindowsHook16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_UnhookWindowsHookEx16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_CallNextHookEx16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_DefHookProc16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_CallMsgFilter16 (struct w16call *c) { (void) c; c->ret = 0; }

/* WinHelp : pas encore de lecteur d'aide de Windows */
void api_WinHelp16 (struct w16call *c)
{
	w16_log ("WinHelp (%s, %u)\n", w16_str (c->a[1]), (unsigned) c->a[2]);
	c->ret = 1;
}

void api_ExitWindows16 (struct w16call *c) { (void) c; exit (0); }

/* MDI : pas pris en charge (DefFrameProc fait comme DefWindowProc) */
void api_DefFrameProc16 (struct w16call *c) { c->ret = user_def_window_proc ((uint16_t) c->a[0], (uint16_t) c->a[2], (uint16_t) c->a[3], c->a[4]); }
void api_DefMDIChildProc16 (struct w16call *c) { c->ret = user_def_window_proc ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]); }
void api_TranslateMDISysAccel16 (struct w16call *c) { (void) c; c->ret = 0; }

int user_window_count (void)
{
	int n = 0;
	for (int i = 0; i < MAX_WND; i++)
		n += wnds[i].used && !(wnds[i].style & WS_CHILD);
	return n;
}

uint16_t user_ctl_brush (uint16_t hwnd, uint16_t hdc, int type)
{
	struct wnd *w = wnd_get (hwnd);
	uint16_t parent = w && (w->style & WS_CHILD) ? w->parent : 0;
	if (!parent)
		return (uint16_t) user_def_window_proc (hwnd, WM_CTLCOLOR, hdc, MAKESEGPTR (type, hwnd));
	return (uint16_t) user_send (parent, WM_CTLCOLOR, hdc, MAKESEGPTR (type, hwnd));
}

void user_notify (uint16_t hwnd, uint16_t code)
{
	struct wnd *w = wnd_get (hwnd);
	if (w && (w->style & WS_CHILD) && w->parent)
		user_send (w->parent, WM_COMMAND, w->id, MAKESEGPTR (code, hwnd));
}

int user_popup (uint16_t hwnd, int x, int y, const char *labels, int wd)
{
	int ox, oy, cw, ch;
	long be;
	if (!wnd_client_geometry (hwnd, &ox, &oy, &cw, &ch, &be))
		return -1;
	return be_popup (be, ox + x, oy + y, labels, wd);
}

static uint16_t dlg_font;
uint16_t user_dlg_font (void) { return dlg_font; }

void user_init (void)
{
	memset (wnds, 0, sizeof wnds);
	memset (classes, 0, sizeof classes);
	uint32_t defproc = native_register (user_def_window_proc);
	(void) defproc;
	ctrl_register ();
}

/* les classes du système (ctrl.c, dialog.c les donnent) */
int user_register_system_class (const char *name, native_wndproc fn, uint16_t style, uint16_t brush, int wndextra)
{
	return class_add (name, native_register (fn), style, 0, brush, wndextra);
}
