/* ctrl.c -- win16 : les contrôles de Windows 3.x (BUTTON, STATIC, EDIT, LISTBOX, COMBOBOX, SCROLLBAR), écrits en C
 * et dessinés aux couleurs du thème de Korin (jamais de couleurs en dur : be_theme). Leur comportement suit
 * Windows 3.1 et user/ de Wine (LGPL-2.1+) : messages BM_*, EM_*, LB_*, CB_*, notifications par WM_COMMAND. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include "w16.h"
#include "be.h"
#include "user.h"
#include "gdi.h"

int user_register_system_class (const char *name, native_wndproc fn, uint16_t style, uint16_t brush, int wndextra);
uint32_t user_def_window_proc (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);
void dialog_register (void);

#define T(x) be_theme (x)

/* le fond donné par le parent (WM_CTLCOLOR) : couleur 0x00RRGGBB, ou -1 si creux */
static long ctl_bg (uint16_t hwnd, uint16_t hdc, int type)
{
	uint16_t br = user_ctl_brush (hwnd, hdc, type);
	int hollow;
	uint32_t ref = gdi_brush_color (br, &hollow);
	if (hollow || !br)
		return -1;
	return (long) gdi_ref_to_rgb (hdc, ref);
}

static void prep_font (struct wnd *w, uint16_t hdc)
{
	if (w->font)
		gdi_set_font (hdc, w->font);
	else if (user_dlg_font ())
		gdi_set_font (hdc, user_dlg_font ());
}

/* le texte d'un libellé sans les « & » (le soulignement des raccourcis n'est pas montré) */
static int strip_amp (const char *s, char *out, int cap)
{
	int k = 0;
	for (; *s && k < cap - 1; s++)
	{
		if (*s == '&')
		{
			if (s[1] == '&')
				out[k++] = *s++;
			continue;
		}
		out[k++] = *s;
	}
	out[k] = 0;
	return k;
}

static uint32_t listbox_impl (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);

/* ======================================================================== BUTTON */
#define BM_GETCHECK (WM_USER + 0)
#define BM_SETCHECK (WM_USER + 1)
#define BM_GETSTATE (WM_USER + 2)
#define BM_SETSTATE (WM_USER + 3)
#define BM_SETSTYLE (WM_USER + 4)
struct btn { int check, pressed, focus, hot; };

static struct btn *btn_of (struct wnd *w)
{
	if (!w->cdata)
		w->cdata = calloc (1, sizeof (struct btn));
	return w->cdata;
}

static void btn_paint (struct wnd *w)
{
	struct btn *b = btn_of (w);
	uint16_t hdc = user_begin (w->hwnd);
	prep_font (w, hdc);
	int kind = (int) (w->style & 0x0f);
	char lab[256];
	strip_amp (w->text, lab, sizeof lab);
	int th = gdi_text_height (hdc, 0), tw = gdi_text_width (hdc, lab, -1, 0);
	uint32_t text = w->disabled ? T (BT_TEXT3) : T (BT_TEXT);
	if (kind == 0 || kind == 1 || kind == 8)	/* PUSHBUTTON, DEFPUSHBUTTON, USERBUTTON */
	{
		long bg = ctl_bg (w->hwnd, hdc, 3);
		if (bg >= 0)
			gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, (uint32_t) bg);
		int def = kind == 1;
		uint32_t fill = b->pressed ? T (BT_SUNKEN) : def ? T (BT_ACCENT) : T (BT_RAISED);
		uint32_t border = def ? T (BT_ACCENT) : b->focus ? T (BT_ACCENT) : T (BT_LINE_STRONG);
		gdi_rgb_rbox (hdc, 0, 0, w->cw, w->ch, 5, fill, border);
		uint32_t ink = def && !b->pressed ? T (BT_ACCENT_INK) : text;
		gdi_text (hdc, (w->cw - tw) / 2 + (b->pressed ? 1 : 0), (w->ch - th) / 2 + (b->pressed ? 1 : 0), lab, -1, ink, 0);
	}
	else if (kind == 7)	/* GROUPBOX */
	{
		long bg = ctl_bg (w->hwnd, hdc, 3);
		uint32_t bgc = bg >= 0 ? (uint32_t) bg : T (BT_SURFACE);
		int y = th / 2;
		gdi_rgb_frame (hdc, 0, y, w->cw, w->ch, T (BT_LINE));
		if (lab[0])
		{
			gdi_rgb_fill (hdc, 6, 0, 6 + tw + 6, th, bgc);
			gdi_text (hdc, 9, 0, lab, -1, text, 0);
		}
	}
	else if (kind == 0x0b)	/* OWNERDRAW */
	{
		user_end (hdc);
		uint16_t sel = global_alloc (32, 0);
		uint32_t d = sel_base (sel);
		uint16_t hdc2 = gdi_dc_for_window (w->hwnd);
		wr16 (d, 4);			/* ODT_BUTTON */
		wr16 (d + 2, w->id);
		wr16 (d + 4, 0);
		wr16 (d + 6, 1);		/* ODA_DRAWENTIRE */
		wr16 (d + 8, (uint16_t) ((b->pressed ? 1 : 0) | (b->focus ? 0x10 : 0) | (w->disabled ? 4 : 0)));
		wr16 (d + 10, w->hwnd);
		wr16 (d + 12, hdc2);
		wr16 (d + 14, 0); wr16 (d + 16, 0); wr16 (d + 18, (uint16_t) w->cw); wr16 (d + 20, (uint16_t) w->ch);
		wr32 (d + 22, 0);
		user_send (w->parent, WM_DRAWITEM, w->id, MAKESEGPTR (sel, 0));
		gdi_release_dc (hdc2);
		global_free (sel);
		return;
	}
	else	/* cases et boutons radio */
	{
		long bg = ctl_bg (w->hwnd, hdc, 3);
		if (bg >= 0)
			gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, (uint32_t) bg);
		int radio = kind == 4 || kind == 9;
		int box = 13, by = (w->ch - box) / 2;
		int left = (w->style & 0x20) != 0;	/* BS_LEFTTEXT */
		int bx = left ? w->cw - box : 0;
		uint32_t acc = w->disabled ? T (BT_TEXT3) : T (BT_ACCENT);
		if (radio)
		{
			gdi_rgb_disc (hdc, bx + box / 2, by + box / 2, box / 2 + 1, b->check ? acc : T (BT_LINE_STRONG));
			gdi_rgb_disc (hdc, bx + box / 2, by + box / 2, box / 2, T (BT_RAISED));
			if (b->check)
				gdi_rgb_disc (hdc, bx + box / 2, by + box / 2, 3, acc);
		}
		else
		{
			gdi_rgb_rbox (hdc, bx, by, bx + box, by + box, 3, b->check ? acc : T (BT_RAISED), b->check ? acc : T (BT_LINE_STRONG));
			if (b->check == 1)
			{
				uint32_t ink = T (BT_ACCENT_INK);
				gdi_rgb_line (hdc, bx + 3, by + 6, bx + 5, by + 9, ink);
				gdi_rgb_line (hdc, bx + 5, by + 9, bx + 10, by + 3, ink);
				gdi_rgb_line (hdc, bx + 3, by + 7, bx + 5, by + 10, ink);
				gdi_rgb_line (hdc, bx + 5, by + 10, bx + 10, by + 4, ink);
			}
			else if (b->check == 2)
				gdi_rgb_fill (hdc, bx + 3, by + 6, bx + box - 3, by + 8, T (BT_ACCENT_INK));
		}
		int tx = left ? 0 : box + 6;
		gdi_text (hdc, tx, (w->ch - th) / 2, lab, -1, text, 0);
		if (b->focus)
			gdi_rgb_fill (hdc, tx, (w->ch + th) / 2, tx + tw, (w->ch + th) / 2 + 1, T (BT_ACCENT));
	}
	user_end (hdc);
}

static void radio_uncheck_group (struct wnd *w)
{
	/* (les boutons radio automatiques du même groupe : de WS_GROUP au suivant) */
	uint16_t first = w->hwnd;
	for (uint16_t h = w->hwnd; h >= 0x1000; h -= 4)
	{
		struct wnd *k = wnd_get (h);
		if (!k || k->parent != w->parent)
			continue;
		first = h;
		if (k->style & WS_GROUP)
			break;
	}
	for (uint16_t h = first; h < 0x1000 + 512 * 4; h += 4)
	{
		struct wnd *k = wnd_get (h);
		if (!k || k->parent != w->parent || !(k->style & WS_CHILD))
			continue;
		if (h != first && (k->style & WS_GROUP))
			break;
		if (h != w->hwnd && !strcasecmp (k->clsname, "BUTTON") && (k->style & 0x0f) == 9)
			user_send (h, BM_SETCHECK, 0, 0);
	}
}

static void btn_click (struct wnd *w)
{
	int kind = (int) (w->style & 0x0f);
	uint16_t h = w->hwnd;
	if (kind == 3)	/* AUTOCHECKBOX */
		user_send (h, BM_SETCHECK, !btn_of (w)->check, 0);
	else if (kind == 6)	/* AUTO3STATE */
		user_send (h, BM_SETCHECK, (uint16_t) ((btn_of (w)->check + 1) % 3), 0);
	else if (kind == 9)	/* AUTORADIOBUTTON */
	{
		radio_uncheck_group (w);
		user_send (h, BM_SETCHECK, 1, 0);
	}
	user_notify (h, 0);	/* BN_CLICKED */
}

static uint32_t button_impl (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	struct btn *b = btn_of (w);
	int kind = (int) (w->style & 0x0f);
	switch (msg)
	{
	case WM_PAINT:
		btn_paint (w);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		if (kind == 7)
			return 0;
		b->pressed = 1;
		user_capture (hwnd);
		if (w->style & WS_TABSTOP)
			user_set_focus (hwnd);
		user_invalidate (hwnd, 0);
		return 0;
	case WM_LBUTTONUP:
		if (!b->pressed)
			return 0;
		b->pressed = 0;
		user_capture (0);
		user_invalidate (hwnd, 0);
		{
			int x = (int16_t) lp, y = (int16_t) (lp >> 16);
			if (x >= 0 && y >= 0 && x < w->cw && y < w->ch)
				btn_click (w);
		}
		return 0;
	case WM_KEYDOWN:
		if (wp == 0x20)
		{
			b->pressed = 1;
			user_invalidate (hwnd, 0);
		}
		return 0;
	case WM_KEYUP:
		if (wp == 0x20 && b->pressed)
		{
			b->pressed = 0;
			user_invalidate (hwnd, 0);
			btn_click (w);
		}
		return 0;
	case WM_SETFOCUS:
		b->focus = 1;
		user_invalidate (hwnd, 0);
		user_notify (hwnd, 6);	/* BN_SETFOCUS (Win32, sans effet chez Win16) */
		return 0;
	case WM_KILLFOCUS:
		b->focus = 0;
		b->pressed = 0;
		user_invalidate (hwnd, 0);
		return 0;
	case BM_GETCHECK:
		return (uint32_t) b->check;
	case BM_SETCHECK:
		if (b->check != wp)
		{
			b->check = wp;
			user_invalidate (hwnd, 0);
		}
		return 0;
	case BM_GETSTATE:
		return (uint32_t) (b->check | (b->pressed ? 4 : 0) | (b->focus ? 8 : 0));
	case BM_SETSTATE:
		b->pressed = wp != 0;
		user_invalidate (hwnd, 0);
		return 0;
	case BM_SETSTYLE:
		w->style = (w->style & ~0xffffu) | wp;
		user_invalidate (hwnd, 0);
		return 0;
	case WM_GETDLGCODE:
		return (uint32_t) (kind == 1 ? 0x10 : kind == 0 ? 0x20 : (kind == 4 || kind == 9) ? 0x40 : kind == 7 ? 0x100 : 0x2000);
	case WM_SETTEXT:
	case WM_ENABLE:
		user_invalidate (hwnd, 1);
		break;
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

/* ======================================================================== STATIC */
static uint32_t static_impl (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	int kind = (int) (w->style & 0x1f);
	switch (msg)
	{
	case WM_NCCREATE:
		if (kind == 3 && w->text[0])	/* SS_ICON : le texte est le nom de l'icône */
		{
			uint32_t name = 0;
			uint16_t sel = 0;
			if (w->text[0] == '#')
				name = (uint32_t) atoi (w->text + 1);
			else
			{
				sel = global_alloc (64, 0);
				snprintf ((char *) lin_ptr (sel_base (sel)), 64, "%s", w->text);
				name = MAKESEGPTR (sel, 0);
			}
			uint16_t ic = gdi_icon_load (w->hinst, name);
			if (sel)
				global_free (sel);
			w->cdata = calloc (1, sizeof (uint16_t));
			*(uint16_t *) w->cdata = ic;
		}
		return 1;
	case WM_PAINT:
	{
		uint16_t hdc = user_begin (hwnd);
		prep_font (w, hdc);
		long bg = ctl_bg (hwnd, hdc, 6);
		switch (kind)
		{
		case 3:	/* SS_ICON */
			if (bg >= 0)
				gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, (uint32_t) bg);
			if (w->cdata)
				gdi_icon_draw (hdc, 0, 0, *(uint16_t *) w->cdata);
			break;
		case 4: case 5: case 6:	/* rectangles pleins : noir, gris, blanc -> trait fort, trait, relief */
			gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, kind == 4 ? T (BT_TEXT) : kind == 5 ? T (BT_LINE_STRONG) : T (BT_RAISED));
			break;
		case 7: case 8: case 9:	/* cadres */
			if (bg >= 0)
				gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, (uint32_t) bg);
			gdi_rgb_frame (hdc, 0, 0, w->cw, w->ch, kind == 7 ? T (BT_TEXT) : kind == 8 ? T (BT_LINE_STRONG) : T (BT_RAISED));
			break;
		default:
		{
			if (bg >= 0)
				gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, (uint32_t) bg);
			int rc[4] = { 0, 0, w->cw, w->ch };
			unsigned fmt = (kind == 1 ? 0x01 : kind == 2 ? 0x02 : 0) | ((kind == 0xb || kind == 0xc) ? 0x20 : 0x10)
				| ((w->style & 0x80) ? 0x800 : 0);
			gdi_set_colors (hdc, w->disabled ? user_theme_ref (BT_TEXT3) : gdi_text_color (hdc), gdi_bk_color (hdc));
			extern void api_SetBkMode16 (struct w16call *);
			struct w16call bm = { .a = { hdc, 1 }, .n = 2 };
			api_SetBkMode16 (&bm);
			gdi_draw_text (hdc, w->text, -1, rc, fmt);
			break;
		}
		}
		user_end (hdc);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_SETTEXT:
		user_def_window_proc (hwnd, msg, wp, lp);
		user_invalidate (hwnd, 1);
		return 1;
	case 0x0170:	/* STM_SETICON */
	{
		uint16_t old = w->cdata ? *(uint16_t *) w->cdata : 0;
		if (!w->cdata)
			w->cdata = calloc (1, sizeof (uint16_t));
		*(uint16_t *) w->cdata = wp;
		user_invalidate (hwnd, 1);
		return old;
	}
	case WM_NCHITTEST:
		return (uint32_t) -1;	/* HTTRANSPARENT */
	case WM_GETDLGCODE:
		return 0x100;	/* DLGC_STATIC */
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

/* ======================================================================== EDIT */
#define EM_GETSEL (WM_USER + 0)
#define EM_SETSEL (WM_USER + 1)
#define EM_GETRECT (WM_USER + 2)
#define EM_GETMODIFY (WM_USER + 8)
#define EM_SETMODIFY (WM_USER + 9)
#define EM_GETLINECOUNT (WM_USER + 10)
#define EM_LINEINDEX (WM_USER + 11)
#define EM_LINELENGTH (WM_USER + 17)
#define EM_REPLACESEL (WM_USER + 18)
#define EM_GETLINE (WM_USER + 20)
#define EM_LIMITTEXT (WM_USER + 21)
#define EM_CANUNDO (WM_USER + 22)
#define EM_LINEFROMCHAR (WM_USER + 25)
#define EM_SETPASSWORDCHAR (WM_USER + 28)
#define EM_SETREADONLY (WM_USER + 31)
#define ES_MULTILINE 0x0004
#define ES_PASSWORD 0x0020
#define ES_READONLY 0x0800
#define ES_UPPERCASE 0x0008
#define ES_LOWERCASE 0x0010
#define ES_NUMBER 0x2000

struct edit { char *buf; int len, cap, caret, anchor, scroll, top, limit, modified, focus; char pass; };

static struct edit *edit_of (struct wnd *w)
{
	if (!w->cdata)
	{
		struct edit *e = calloc (1, sizeof *e);
		e->cap = 256;
		e->buf = calloc (1, (size_t) e->cap);
		e->limit = (w->style & ES_MULTILINE) ? 65535 : 30000;
		e->pass = (w->style & ES_PASSWORD) ? '*' : 0;
		w->cdata = e;
	}
	return w->cdata;
}

static void edit_set (struct edit *e, const char *s)
{
	int n = (int) strlen (s);
	if (n + 1 > e->cap)
	{
		e->cap = n + 256;
		e->buf = realloc (e->buf, (size_t) e->cap);
	}
	memcpy (e->buf, s, (size_t) n + 1);
	e->len = n;
	e->caret = e->anchor = 0;
	e->scroll = e->top = 0;
}

static void edit_replace (struct wnd *w, struct edit *e, const char *s, int n)
{
	int a = e->caret < e->anchor ? e->caret : e->anchor, b = e->caret < e->anchor ? e->anchor : e->caret;
	if (e->len - (b - a) + n > e->limit)
		n = e->limit - (e->len - (b - a));
	if (n < 0)
		n = 0;
	int nl = e->len - (b - a) + n;
	if (nl + 1 > e->cap)
	{
		e->cap = nl + 256;
		e->buf = realloc (e->buf, (size_t) e->cap);
	}
	memmove (e->buf + a + n, e->buf + b, (size_t) (e->len - b + 1));
	memcpy (e->buf + a, s, (size_t) n);
	for (int i = 0; i < n; i++)
	{
		char *ch = e->buf + a + i;
		if (w->style & ES_UPPERCASE) *ch = (char) toupper ((unsigned char) *ch);
		if (w->style & ES_LOWERCASE) *ch = (char) tolower ((unsigned char) *ch);
	}
	e->len = nl;
	e->caret = e->anchor = a + n;
	e->modified = 1;
}

/* les lignes d'un champ multiligne : début de la ligne i */
static int edit_line_start (struct edit *e, int line)
{
	int l = 0, i = 0;
	while (l < line && i < e->len)
	{
		if (e->buf[i] == '\n')
			l++;
		i++;
	}
	return l == line ? i : -1;
}

static int edit_line_of (struct edit *e, int pos)
{
	int l = 0;
	for (int i = 0; i < pos && i < e->len; i++)
		if (e->buf[i] == '\n')
			l++;
	return l;
}

static int edit_line_len (struct edit *e, int start)
{
	int i = start;
	while (i < e->len && e->buf[i] != '\n' && e->buf[i] != '\r')
		i++;
	return i - start;
}

static int edit_lines (struct edit *e)
{
	int l = 1;
	for (int i = 0; i < e->len; i++)
		if (e->buf[i] == '\n')
			l++;
	return l;
}

static void edit_paint (struct wnd *w)
{
	struct edit *e = edit_of (w);
	uint16_t hdc = user_begin (w->hwnd);
	prep_font (w, hdc);
	long bg = ctl_bg (w->hwnd, hdc, 1);
	uint32_t bgc = bg >= 0 ? (uint32_t) bg : T (BT_RAISED);
	uint32_t txt = w->disabled ? T (BT_TEXT3) : gdi_ref_to_rgb (hdc, gdi_text_color (hdc));
	int border = (w->style & WS_BORDER) != 0;
	gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, bgc);
	if (border)
		gdi_rgb_frame (hdc, 0, 0, w->cw, w->ch, e->focus ? T (BT_ACCENT) : T (BT_LINE_STRONG));
	int pad = border ? 3 : 1, lh = gdi_text_height (hdc, 0);
	int a = e->caret < e->anchor ? e->caret : e->anchor, b = e->caret < e->anchor ? e->anchor : e->caret;
	if (w->style & ES_MULTILINE)
	{
		int nlines = edit_lines (e), vis = (w->ch - 2 * pad) / (lh ? lh : 1);
		int cl = edit_line_of (e, e->caret);
		if (cl < e->top) e->top = cl;
		if (vis > 0 && cl >= e->top + vis) e->top = cl - vis + 1;
		for (int l = e->top, y = pad; l < nlines && y < w->ch - pad; l++, y += lh)
		{
			int s = edit_line_start (e, l), n = edit_line_len (e, s);
			if (a != b && b > s && a < s + n)
			{
				int sa = a > s ? a : s, sb = b < s + n ? b : s + n;
				int xa = pad + gdi_text_width (hdc, e->buf + s, sa - s, 0), xb = pad + gdi_text_width (hdc, e->buf + s, sb - s, 0);
				gdi_rgb_fill (hdc, xa, y, xb, y + lh, T (BT_ACCENT_LIGHT));
			}
			gdi_text (hdc, pad, y, e->buf + s, n, txt, 0);
			if (e->focus && cl == l)
			{
				int cx = pad + gdi_text_width (hdc, e->buf + s, e->caret - s, 0);
				gdi_rgb_fill (hdc, cx, y, cx + 1, y + lh, T (BT_ACCENT));
			}
		}
	}
	else
	{
		char shown[1024];
		int n = e->len < 1023 ? e->len : 1023;
		for (int i = 0; i < n; i++)
			shown[i] = e->pass ? e->pass : e->buf[i];
		shown[n] = 0;
		int cx = gdi_text_width (hdc, shown, e->caret, 0);
		int room = w->cw - 2 * pad;
		if (cx - e->scroll > room - 2) e->scroll = cx - room + 2;
		if (cx < e->scroll) e->scroll = cx;
		if (e->scroll < 0) e->scroll = 0;
		int y = (w->ch - lh) / 2;
		if (a != b)
		{
			int xa = pad - e->scroll + gdi_text_width (hdc, shown, a, 0), xb = pad - e->scroll + gdi_text_width (hdc, shown, b, 0);
			gdi_rgb_fill (hdc, xa, y, xb, y + lh, T (BT_ACCENT_LIGHT));
		}
		gdi_text (hdc, pad - e->scroll, y, shown, n, txt, 0);
		if (e->focus)
			gdi_rgb_fill (hdc, pad - e->scroll + cx, y, pad - e->scroll + cx + 1, y + lh, T (BT_ACCENT));
		if (border)
			gdi_rgb_frame (hdc, 0, 0, w->cw, w->ch, e->focus ? T (BT_ACCENT) : T (BT_LINE_STRONG));
	}
	user_end (hdc);
}

static int edit_hit (struct wnd *w, struct edit *e, int x, int y)
{
	uint16_t hdc = gdi_dc_for_window (w->hwnd);
	prep_font (w, hdc);
	int pad = (w->style & WS_BORDER) ? 3 : 1, lh = gdi_text_height (hdc, 0);
	int s = 0, n = e->len, off = -e->scroll;
	if (w->style & ES_MULTILINE)
	{
		int l = e->top + (y - pad) / (lh ? lh : 1);
		if (l >= edit_lines (e)) l = edit_lines (e) - 1;
		if (l < 0) l = 0;
		s = edit_line_start (e, l);
		n = edit_line_len (e, s);
		off = 0;
	}
	int best = s;
	for (int i = 0; i <= n; i++)
	{
		int cx = pad + off + gdi_text_width (hdc, e->buf + s, i, 0);
		if (cx <= x + 2)
			best = s + i;
	}
	gdi_release_dc (hdc);
	return best;
}

static void edit_changed (struct wnd *w)
{
	uint16_t h = w->hwnd;
	user_invalidate (h, 0);
	user_notify (h, 0x400);	/* EN_UPDATE */
	user_notify (h, 0x300);	/* EN_CHANGE */
}

static uint32_t edit_impl (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	struct edit *e = edit_of (w);
	int ml = (w->style & ES_MULTILINE) != 0, ro = (w->style & ES_READONLY) != 0;
	switch (msg)
	{
	case WM_NCCREATE:
		edit_set (e, w->text);
		return 1;
	case WM_PAINT:
		edit_paint (w);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SETTEXT:
		edit_set (e, lp ? (const char *) far_ptr (lp) : "");
		e->modified = 0;
		edit_changed (w);
		return 1;
	case WM_GETTEXT:
	{
		int n = e->len < wp - 1 ? e->len : wp - 1;
		if (n < 0)
			return 0;
		memcpy (far_ptr (lp), e->buf, (size_t) n);
		((char *) far_ptr (lp))[n] = 0;
		return (uint32_t) n;
	}
	case WM_GETTEXTLENGTH:
		return (uint32_t) e->len;
	case WM_SETFOCUS:
		e->focus = 1;
		if (!ml)
		{
			e->anchor = 0;
			e->caret = e->len;
		}
		user_invalidate (hwnd, 0);
		user_notify (hwnd, 0x100);	/* EN_SETFOCUS */
		return 0;
	case WM_KILLFOCUS:
		e->focus = 0;
		user_invalidate (hwnd, 0);
		user_notify (hwnd, 0x200);	/* EN_KILLFOCUS */
		return 0;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		user_set_focus (hwnd);
		e->caret = edit_hit (w, e, (int16_t) lp, (int16_t) (lp >> 16));
		if (!(wp & 4))
			e->anchor = e->caret;
		user_capture (hwnd);
		user_invalidate (hwnd, 0);
		return 0;
	case WM_MOUSEMOVE:
		if (wp & 1)
		{
			e->caret = edit_hit (w, e, (int16_t) lp, (int16_t) (lp >> 16));
			user_invalidate (hwnd, 0);
		}
		return 0;
	case WM_LBUTTONUP:
		user_capture (0);
		return 0;
	case WM_CHAR:
	{
		if (ro)
			return 0;
		char ch = (char) wp;
		if (ch == 8)
		{
			if (e->caret == e->anchor && e->caret > 0)
				e->anchor = e->caret - 1;
			edit_replace (w, e, "", 0);
		}
		else if (ch == 13 && ml)
			edit_replace (w, e, "\r\n", 2);
		else if ((unsigned char) ch >= 32 && (!(w->style & ES_NUMBER) || isdigit ((unsigned char) ch)))
			edit_replace (w, e, &ch, 1);
		else
			return 0;
		edit_changed (w);
		return 0;
	}
	case WM_KEYDOWN:
	{
		int shift = user_key_down (0x10) != 0;
		int ctrl = user_key_down (0x11) != 0;
		int old = e->caret;
		switch (wp)
		{
		case 0x25: if (e->caret > 0) e->caret--; break;
		case 0x27: if (e->caret < e->len) e->caret++; break;
		case 0x24: e->caret = ml ? edit_line_start (e, edit_line_of (e, e->caret)) : 0; break;
		case 0x23: e->caret = ml ? edit_line_start (e, edit_line_of (e, e->caret)) + edit_line_len (e, edit_line_start (e, edit_line_of (e, e->caret))) : e->len; break;
		case 0x26:
		case 0x28:
			if (ml)
			{
				int l = edit_line_of (e, e->caret), col = e->caret - edit_line_start (e, l);
				l += wp == 0x26 ? -1 : 1;
				if (l >= 0 && l < edit_lines (e))
				{
					int s = edit_line_start (e, l), n = edit_line_len (e, s);
					e->caret = s + (col < n ? col : n);
				}
			}
			break;
		case 0x2e:	/* Suppr */
			if (ro)
				return 0;
			if (e->caret == e->anchor && e->caret < e->len)
				e->anchor = e->caret + 1;
			edit_replace (w, e, "", 0);
			edit_changed (w);
			return 0;
		default:
			if (ctrl && (wp == 'C' || wp == 'X' || wp == 'V' || wp == 'A'))
			{
				uint16_t m = wp == 'C' ? 0x301 : wp == 'X' ? 0x300 : 0x302;
				if (wp == 'A')
				{
					e->anchor = 0;
					e->caret = e->len;
					user_invalidate (hwnd, 0);
				}
				else
					user_send (hwnd, m, 0, 0);
			}
			return 0;
		}
		(void) old;
		if (!shift)
			e->anchor = e->caret;
		user_invalidate (hwnd, 0);
		return 0;
	}
	case 0x0300:	/* WM_CUT */
	case 0x0301:	/* WM_COPY */
	{
		int a = e->caret < e->anchor ? e->caret : e->anchor, b = e->caret < e->anchor ? e->anchor : e->caret;
		if (a == b || e->pass)
			return 0;
		char u[8192];
		w16_to_utf8 ((const uint8_t *) e->buf + a, b - a, u, sizeof u);
		extern void be_clip_put (const char *);
		be_clip_put (u);
		if (msg == 0x0300 && !ro)
		{
			edit_replace (w, e, "", 0);
			edit_changed (w);
		}
		return 0;
	}
	case 0x0302:	/* WM_PASTE */
	{
		if (ro)
			return 0;
		extern int be_clip_take (char *, int);
		char u[8192];
		uint8_t raw[8192];
		if (be_clip_take (u, sizeof u) <= 0)
			return 0;
		int n = w16_from_utf8 (u, raw, sizeof raw);
		if (!ml)
			for (int i = 0; i < n; i++)
				if (raw[i] == '\r' || raw[i] == '\n')
					n = i;
		edit_replace (w, e, (const char *) raw, n);
		edit_changed (w);
		return 0;
	}
	case 0x0303:	/* WM_CLEAR */
		if (!ro)
		{
			edit_replace (w, e, "", 0);
			edit_changed (w);
		}
		return 0;
	case EM_GETSEL:
	{
		int a = e->caret < e->anchor ? e->caret : e->anchor, b = e->caret < e->anchor ? e->anchor : e->caret;
		return (uint32_t) a | (uint32_t) b << 16;
	}
	case EM_SETSEL:
	{
		int a = (int16_t) lp, b = (int16_t) (lp >> 16);
		if (a < 0 || a > e->len) a = e->len;
		if (b < 0 || b > e->len) b = e->len;
		e->anchor = a;
		e->caret = b;
		user_invalidate (hwnd, 0);
		return 1;
	}
	case EM_GETRECT:
		if (lp)
		{
			uint32_t r = far_lin (lp);
			wr16 (r, 2); wr16 (r + 2, 2); wr16 (r + 4, (uint16_t) (w->cw - 2)); wr16 (r + 6, (uint16_t) (w->ch - 2));
		}
		return 0;
	case EM_GETMODIFY: return (uint32_t) e->modified;
	case EM_SETMODIFY: e->modified = wp; return 0;
	case EM_GETLINECOUNT: return (uint32_t) edit_lines (e);
	case EM_LINEINDEX: { int l = (int16_t) wp < 0 ? edit_line_of (e, e->caret) : (int16_t) wp; int s = edit_line_start (e, l); return s < 0 ? 0xffff : (uint32_t) s; }
	case EM_LINELENGTH: { int pos = (int16_t) wp < 0 ? e->caret : (int16_t) wp; return (uint32_t) edit_line_len (e, edit_line_start (e, edit_line_of (e, pos))); }
	case EM_LINEFROMCHAR: return (uint32_t) edit_line_of (e, (int16_t) wp < 0 ? e->caret : (int16_t) wp);
	case EM_REPLACESEL:
	{
		const char *s = far_ptr (lp);
		edit_replace (w, e, s, (int) strlen (s));
		edit_changed (w);
		return 0;
	}
	case EM_GETLINE:
	{
		int s = edit_line_start (e, wp);
		if (s < 0)
			return 0;
		int n = edit_line_len (e, s), cap = rd16 (far_lin (lp));
		if (n > cap)
			n = cap;
		memcpy (far_ptr (lp), e->buf + s, (size_t) n);
		return (uint32_t) n;
	}
	case EM_LIMITTEXT: e->limit = wp ? wp : 30000; return 0;
	case EM_CANUNDO: return 0;
	case EM_SETPASSWORDCHAR: e->pass = (char) wp; user_invalidate (hwnd, 0); return 0;
	case EM_SETREADONLY: if (wp) w->style |= ES_READONLY; else w->style &= ~ES_READONLY; return 1;
	case WM_GETDLGCODE:
		return 0x80 | 0x08 | 0x01 | (ml ? 0x04 : 0);	/* WANTCHARS, HASSETSEL, WANTARROWS (, WANTALLKEYS) */
	case WM_ENABLE:
		user_invalidate (hwnd, 1);
		return 0;
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

/* ======================================================================== LISTBOX */
#define LB_ADDSTRING (WM_USER + 1)
#define LB_INSERTSTRING (WM_USER + 2)
#define LB_DELETESTRING (WM_USER + 3)
#define LB_RESETCONTENT (WM_USER + 5)
#define LB_SETSEL (WM_USER + 6)
#define LB_SETCURSEL (WM_USER + 7)
#define LB_GETSEL (WM_USER + 8)
#define LB_GETCURSEL (WM_USER + 9)
#define LB_GETTEXT (WM_USER + 10)
#define LB_GETTEXTLEN (WM_USER + 11)
#define LB_GETCOUNT (WM_USER + 12)
#define LB_SELECTSTRING (WM_USER + 13)
#define LB_DIR (WM_USER + 14)
#define LB_GETTOPINDEX (WM_USER + 15)
#define LB_FINDSTRING (WM_USER + 16)
#define LB_GETSELCOUNT (WM_USER + 17)
#define LB_GETSELITEMS (WM_USER + 18)
#define LB_SETTOPINDEX (WM_USER + 24)
#define LB_GETITEMRECT (WM_USER + 25)
#define LB_GETITEMDATA (WM_USER + 26)
#define LB_SETITEMDATA (WM_USER + 27)
#define LB_SETCARETINDEX (WM_USER + 31)
#define LB_GETCARETINDEX (WM_USER + 32)
#define LB_SETITEMHEIGHT (WM_USER + 33)
#define LB_GETITEMHEIGHT (WM_USER + 34)
#define LB_FINDSTRINGEXACT (WM_USER + 35)
#define LBS_NOTIFY 0x0001
#define LBS_SORT 0x0002
#define LBS_MULTIPLESEL 0x0008
#define LBS_OWNERDRAW 0x0030
#define LBS_HASSTRINGS 0x0040
#define LBS_EXTENDEDSEL 0x0800

struct lbitem { char *text; uint32_t data; int sel; };
struct listbox { struct lbitem *it; int n, cap, cur, top, focus, item_h, combo; uint16_t combo_hwnd; };

static struct listbox *lb_of (struct wnd *w)
{
	if (!w->cdata)
	{
		struct listbox *l = calloc (1, sizeof *l);
		l->cur = -1;
		w->cdata = l;
	}
	return w->cdata;
}

static int lb_has_strings (struct wnd *w)
{
	return !(w->style & LBS_OWNERDRAW) || (w->style & LBS_HASSTRINGS);
}

static int lb_insert (struct wnd *w, struct listbox *l, int at, uint32_t lp, int sort)
{
	if (l->n == l->cap)
	{
		l->cap = l->cap ? l->cap * 2 : 32;
		l->it = realloc (l->it, sizeof l->it[0] * (size_t) l->cap);
	}
	const char *s = lb_has_strings (w) && lp ? (const char *) far_ptr (lp) : "";
	if (sort && lb_has_strings (w))
		for (at = 0; at < l->n && strcasecmp (l->it[at].text, s) <= 0; at++)
			;
	if (at < 0 || at > l->n)
		at = l->n;
	memmove (&l->it[at + 1], &l->it[at], sizeof l->it[0] * (size_t) (l->n - at));
	l->it[at].text = strdup (s);
	l->it[at].data = lb_has_strings (w) ? 0 : lp;
	l->it[at].sel = 0;
	l->n++;
	if (l->cur >= at)
		l->cur++;
	return at;
}

static int lb_find (struct listbox *l, int start, const char *s, int exact)
{
	int n = (int) strlen (s);
	for (int k = 0; k < l->n; k++)
	{
		int i = (start + 1 + k) % l->n;
		if (exact ? !strcasecmp (l->it[i].text, s) : !strncasecmp (l->it[i].text, s, (size_t) n))
			return i;
	}
	return -1;
}

static int lb_item_h (struct wnd *w, struct listbox *l)
{
	if (l->item_h)
		return l->item_h;
	uint16_t hdc = gdi_dc_for_window (w->hwnd);
	prep_font (w, hdc);
	int h = gdi_text_height (hdc, 0) + 2;
	gdi_release_dc (hdc);
	return h;
}

static void lb_paint (struct wnd *w)
{
	struct listbox *l = lb_of (w);
	uint16_t hdc = user_begin (w->hwnd);
	prep_font (w, hdc);
	long bg = ctl_bg (w->hwnd, hdc, 2);
	uint32_t bgc = bg >= 0 ? (uint32_t) bg : T (BT_RAISED);
	uint32_t txt = gdi_ref_to_rgb (hdc, gdi_text_color (hdc));
	gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, bgc);
	int ih = lb_item_h (w, l), pad = (w->style & WS_BORDER) ? 1 : 0;
	int vis = (w->ch - 2 * pad) / ih, sbw = (l->n > vis) ? 6 : 0;
	for (int i = l->top, y = pad; i < l->n && y < w->ch - pad; i++, y += ih)
	{
		int sel = (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) ? l->it[i].sel : i == l->cur;
		if (sel)
			gdi_rgb_fill (hdc, pad, y, w->cw - pad - sbw, y + ih, l->focus || l->combo ? T (BT_ACCENT) : T (BT_SUNKEN));
		if ((w->style & LBS_OWNERDRAW) && w->parent)
		{
			user_end (hdc);
			uint16_t sel16 = global_alloc (32, 0);
			uint32_t d = sel_base (sel16);
			uint16_t hdc2 = gdi_dc_for_window (w->hwnd);
			prep_font (w, hdc2);
			wr16 (d, 2);	/* ODT_LISTBOX */
			wr16 (d + 2, w->id);
			wr16 (d + 4, (uint16_t) i);
			wr16 (d + 6, 1);
			wr16 (d + 8, (uint16_t) (sel ? 1 : 0));
			wr16 (d + 10, w->hwnd);
			wr16 (d + 12, hdc2);
			wr16 (d + 14, (uint16_t) pad); wr16 (d + 16, (uint16_t) y); wr16 (d + 18, (uint16_t) (w->cw - pad - sbw)); wr16 (d + 20, (uint16_t) (y + ih));
			wr32 (d + 22, l->it[i].data);
			user_send (w->parent, WM_DRAWITEM, w->id, MAKESEGPTR (sel16, 0));
			gdi_release_dc (hdc2);
			global_free (sel16);
			hdc = user_begin (w->hwnd);
			prep_font (w, hdc);
			continue;
		}
		const char *t = l->it[i].text;
		char line[256];
		int k = 0;
		for (; t[k] && k < 255; k++)
			line[k] = t[k] == '\t' ? ' ' : t[k];
		line[k] = 0;
		gdi_text (hdc, pad + 3, y + 1, line, -1, sel && (l->focus || l->combo) ? T (BT_ACCENT_INK) : txt, 0);
	}
	if (sbw)
	{
		int track = w->ch - 2 * pad;
		int th = track * vis / l->n, ty = track * l->top / l->n;
		if (th < 10) th = 10;
		gdi_rgb_rbox (hdc, w->cw - pad - 5, pad + ty, w->cw - pad - 1, pad + ty + th, 2, T (BT_LINE_STRONG), T (BT_LINE_STRONG));
	}
	if (w->style & WS_BORDER)
		gdi_rgb_frame (hdc, 0, 0, w->cw, w->ch, l->focus ? T (BT_ACCENT) : T (BT_LINE_STRONG));
	user_end (hdc);
}

static void lb_visible (struct wnd *w, struct listbox *l)
{
	int vis = w->ch / lb_item_h (w, l);
	if (vis < 1)
		vis = 1;
	if (l->cur >= 0 && l->cur < l->top)
		l->top = l->cur;
	if (l->cur >= l->top + vis)
		l->top = l->cur - vis + 1;
	if (l->top > l->n - vis)
		l->top = l->n - vis;
	if (l->top < 0)
		l->top = 0;
}

static void lb_dir (struct wnd *w, struct listbox *l, uint16_t attr, const char *spec)
{
	char dir[300], pat[64], path[700];
	snprintf (dir, sizeof dir, "%s", spec);
	char *b = strrchr (dir, '\\');
	char *p = b ? b + 1 : (dir[1] == ':' ? dir + 2 : dir);
	snprintf (pat, sizeof pat, "%s", p);
	*p = 0;
	if (!dos_to_host (dir[0] ? dir : ".", path, sizeof path))
		return;
	DIR *d = opendir (path);
	if (!d)
		return;
	struct dirent *e;
	uint16_t sel = global_alloc (64, 0);
	while ((e = readdir (d)))
	{
		if (e->d_name[0] == '.' && strcmp (e->d_name, ".."))
			continue;
		char full[1000];
		snprintf (full, sizeof full, "%s/%s", path, e->d_name);
		DIR *t = opendir (full);
		int isdir = t != 0;
		if (t)
			closedir (t);
		char name[64];
		if (isdir)
		{
			if (!(attr & 0x10))
				continue;
			snprintf (name, sizeof name, "[%s]", e->d_name);
		}
		else
		{
			if (attr & 0x8000)	/* DDL_EXCLUSIVE : seulement les dossiers ou lecteurs */
				continue;
			extern int wild_match_dos (const char *pat, const char *name);
			const char *q = pat;
			int ok = 1;
			if (strcmp (q, "*.*") && strcmp (q, "*"))
			{
				const char *dot = strchr (q, '.'), *ndot = strrchr (e->d_name, '.');
				if (dot && dot[1] != '*' && (!ndot || strcasecmp (dot + 1, ndot + 1)))
					ok = 0;
			}
			if (!ok || strlen (e->d_name) > 12)
				continue;
			snprintf (name, sizeof name, "%s", e->d_name);
		}
		for (char *c = name; *c; c++)
			*c = (char) tolower ((unsigned char) *c);
		snprintf ((char *) lin_ptr (sel_base (sel)), 64, "%s", name);
		lb_insert (w, l, -1, MAKESEGPTR (sel, 0), 1);
	}
	closedir (d);
	if (attr & 0x4000)	/* DDL_DRIVES */
	{
		snprintf ((char *) lin_ptr (sel_base (sel)), 64, "[-c-]");
		lb_insert (w, l, -1, MAKESEGPTR (sel, 0), 0);
		snprintf ((char *) lin_ptr (sel_base (sel)), 64, "[-d-]");
		lb_insert (w, l, -1, MAKESEGPTR (sel, 0), 0);
	}
	global_free (sel);
}

static uint32_t listbox_impl (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	struct listbox *l = lb_of (w);
	int multi = (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0;
	switch (msg)
	{
	case WM_PAINT:
		lb_paint (w);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_NCDESTROY:
		for (int i = 0; i < l->n; i++)
			free (l->it[i].text);
		free (l->it);
		break;
	case WM_SETFOCUS:
		l->focus = 1;
		user_invalidate (hwnd, 0);
		return 0;
	case WM_KILLFOCUS:
		l->focus = 0;
		user_invalidate (hwnd, 0);
		return 0;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
	{
		user_set_focus (hwnd);
		int i = l->top + ((int16_t) (lp >> 16) - ((w->style & WS_BORDER) ? 1 : 0)) / lb_item_h (w, l);
		if (i < 0 || i >= l->n)
			return 0;
		if (multi)
			l->it[i].sel = !l->it[i].sel;
		l->cur = i;
		user_invalidate (hwnd, 0);
		if (w->style & LBS_NOTIFY)
			user_notify (hwnd, msg == WM_LBUTTONDBLCLK ? 2 : 1);	/* LBN_DBLCLK, LBN_SELCHANGE */
		return 0;
	}
	case WM_KEYDOWN:
	{
		int old = l->cur, vis = w->ch / lb_item_h (w, l);
		switch (wp)
		{
		case 0x26: l->cur = l->cur > 0 ? l->cur - 1 : 0; break;
		case 0x28: l->cur = l->cur < l->n - 1 ? l->cur + 1 : l->n - 1; break;
		case 0x21: l->cur -= vis; if (l->cur < 0) l->cur = 0; break;
		case 0x22: l->cur += vis; if (l->cur >= l->n) l->cur = l->n - 1; break;
		case 0x24: l->cur = 0; break;
		case 0x23: l->cur = l->n - 1; break;
		case 0x20: if (multi && l->cur >= 0) { l->it[l->cur].sel = !l->it[l->cur].sel; user_invalidate (hwnd, 0); } return 0;
		default: return 0;
		}
		lb_visible (w, l);
		user_invalidate (hwnd, 0);
		if (old != l->cur && (w->style & LBS_NOTIFY))
			user_notify (hwnd, 1);
		return 0;
	}
	case WM_VSCROLL:
		l->top += wp == 0 ? -1 : wp == 1 ? 1 : wp == 2 ? -5 : wp == 3 ? 5 : 0;
		if (l->top > l->n - 1) l->top = l->n - 1;
		if (l->top < 0) l->top = 0;
		user_invalidate (hwnd, 0);
		return 0;
	case WM_GETDLGCODE:
		return 0x01;	/* DLGC_WANTARROWS */
	case LB_ADDSTRING:
		user_invalidate (hwnd, 0);
		return (uint32_t) lb_insert (w, l, -1, lp, (w->style & LBS_SORT) != 0);
	case LB_INSERTSTRING:
		user_invalidate (hwnd, 0);
		return (uint32_t) lb_insert (w, l, (int16_t) wp, lp, 0);
	case LB_DELETESTRING:
	{
		int i = (int16_t) wp;
		if (i < 0 || i >= l->n)
			return 0xffff;
		free (l->it[i].text);
		memmove (&l->it[i], &l->it[i + 1], sizeof l->it[0] * (size_t) (l->n - i - 1));
		l->n--;
		if (l->cur >= l->n) l->cur = l->n - 1;
		user_invalidate (hwnd, 0);
		return (uint32_t) l->n;
	}
	case LB_RESETCONTENT:
		for (int i = 0; i < l->n; i++)
			free (l->it[i].text);
		l->n = 0;
		l->cur = -1;
		l->top = 0;
		user_invalidate (hwnd, 0);
		return 0;
	case LB_SETSEL:
	{
		int i = (int16_t) lp;
		if (i == -1)
			for (int k = 0; k < l->n; k++)
				l->it[k].sel = wp;
		else if (i >= 0 && i < l->n)
			l->it[i].sel = wp;
		user_invalidate (hwnd, 0);
		return 0;
	}
	case LB_SETCURSEL:
	{
		int i = (int16_t) wp;
		l->cur = i >= 0 && i < l->n ? i : -1;
		lb_visible (w, l);
		user_invalidate (hwnd, 0);
		return l->cur < 0 && i != -1 ? 0xffff : (uint32_t) (uint16_t) l->cur;
	}
	case LB_GETSEL:
	{
		int i = (int16_t) wp;
		if (i < 0 || i >= l->n)
			return 0xffff;
		return multi ? (uint32_t) l->it[i].sel : i == l->cur;
	}
	case LB_GETCURSEL:
		return (uint32_t) (uint16_t) l->cur;
	case LB_GETTEXT:
	{
		int i = (int16_t) wp;
		if (i < 0 || i >= l->n)
			return 0xffff;
		if (!lb_has_strings (w))
		{
			wr32 (far_lin (lp), l->it[i].data);
			return 4;
		}
		strcpy (far_ptr (lp), l->it[i].text);
		return (uint32_t) strlen (l->it[i].text);
	}
	case LB_GETTEXTLEN:
	{
		int i = (int16_t) wp;
		return i >= 0 && i < l->n ? (uint32_t) strlen (l->it[i].text) : 0xffff;
	}
	case LB_GETCOUNT:
		return (uint32_t) l->n;
	case LB_SELECTSTRING:
	{
		int i = lb_find (l, (int16_t) wp, w16_raw (lp), 0);
		if (i < 0)
			return 0xffff;
		l->cur = i;
		lb_visible (w, l);
		user_invalidate (hwnd, 0);
		return (uint32_t) i;
	}
	case LB_FINDSTRING:
	{
		int i = l->n ? lb_find (l, (int16_t) wp, w16_raw (lp), 0) : -1;
		return (uint32_t) (uint16_t) i;
	}
	case LB_FINDSTRINGEXACT:
	{
		int i = l->n ? lb_find (l, (int16_t) wp, w16_raw (lp), 1) : -1;
		return (uint32_t) (uint16_t) i;
	}
	case LB_DIR:
		lb_dir (w, l, wp, w16_raw (lp));
		user_invalidate (hwnd, 0);
		return (uint32_t) (l->n - 1);
	case LB_GETTOPINDEX:
		return (uint32_t) l->top;
	case LB_SETTOPINDEX:
		l->top = (int16_t) wp;
		user_invalidate (hwnd, 0);
		return 0;
	case LB_GETSELCOUNT:
	{
		int n = 0;
		for (int i = 0; i < l->n; i++)
			n += l->it[i].sel;
		return (uint32_t) n;
	}
	case LB_GETSELITEMS:
	{
		int n = 0;
		uint32_t p = far_lin (lp);
		for (int i = 0; i < l->n && n < wp; i++)
			if (l->it[i].sel)
				wr16 (p + 2 * (uint32_t) n++, (uint16_t) i);
		return (uint32_t) n;
	}
	case LB_GETITEMRECT:
	{
		int ih = lb_item_h (w, l), y = ((int16_t) wp - l->top) * ih;
		uint32_t r = far_lin (lp);
		wr16 (r, 0); wr16 (r + 2, (uint16_t) y); wr16 (r + 4, (uint16_t) w->cw); wr16 (r + 6, (uint16_t) (y + ih));
		return 1;
	}
	case LB_GETITEMDATA:
	{
		int i = (int16_t) wp;
		return i >= 0 && i < l->n ? l->it[i].data : 0xffffffff;
	}
	case LB_SETITEMDATA:
	{
		int i = (int16_t) wp;
		if (i < 0 || i >= l->n)
			return 0xffff;
		l->it[i].data = lp;
		return 0;
	}
	case LB_SETCARETINDEX:
		l->cur = (int16_t) wp;
		user_invalidate (hwnd, 0);
		return 0;
	case LB_GETCARETINDEX:
		return (uint32_t) (uint16_t) l->cur;
	case LB_SETITEMHEIGHT:
		l->item_h = (int16_t) lp;
		return 0;
	case LB_GETITEMHEIGHT:
		return (uint32_t) lb_item_h (w, l);
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

/* ======================================================================== COMBOBOX : un champ et une liste cachée */
#define CB_GETEDITSEL (WM_USER + 0)
#define CB_LIMITTEXT (WM_USER + 1)
#define CB_SETEDITSEL (WM_USER + 2)
#define CB_ADDSTRING (WM_USER + 3)
#define CB_DELETESTRING (WM_USER + 4)
#define CB_DIR (WM_USER + 5)
#define CB_GETCOUNT (WM_USER + 6)
#define CB_GETCURSEL (WM_USER + 7)
#define CB_GETLBTEXT (WM_USER + 8)
#define CB_GETLBTEXTLEN (WM_USER + 9)
#define CB_INSERTSTRING (WM_USER + 10)
#define CB_RESETCONTENT (WM_USER + 11)
#define CB_FINDSTRING (WM_USER + 12)
#define CB_SELECTSTRING (WM_USER + 13)
#define CB_SETCURSEL (WM_USER + 14)
#define CB_SHOWDROPDOWN (WM_USER + 15)
#define CB_GETITEMDATA (WM_USER + 16)
#define CB_SETITEMDATA (WM_USER + 17)
#define CB_GETDROPPEDCONTROLRECT (WM_USER + 18)
#define CB_SETITEMHEIGHT (WM_USER + 19)
#define CB_GETITEMHEIGHT (WM_USER + 20)
#define CB_SETEXTENDEDUI (WM_USER + 21)
#define CB_GETEXTENDEDUI (WM_USER + 22)
#define CB_GETDROPPEDSTATE (WM_USER + 23)
#define CB_FINDSTRINGEXACT (WM_USER + 24)

struct combo { uint16_t lb; char text[256]; int focus, len; };

static struct combo *cb_of (struct wnd *w)
{
	if (!w->cdata)
		w->cdata = calloc (1, sizeof (struct combo));
	return w->cdata;
}

static void cb_paint (struct wnd *w)
{
	struct combo *cb = cb_of (w);
	uint16_t hdc = user_begin (w->hwnd);
	prep_font (w, hdc);
	int kind = (int) (w->style & 3);
	int h = gdi_text_height (hdc, 0) + 8;
	if (h > w->ch)
		h = w->ch;
	long bg = ctl_bg (w->hwnd, hdc, 3);
	if (bg >= 0)
		gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, (uint32_t) bg);
	uint32_t txt = w->disabled ? T (BT_TEXT3) : T (BT_TEXT);
	gdi_rgb_rbox (hdc, 0, 0, w->cw, h, 4, kind == 3 ? T (BT_RAISED) : T (BT_RAISED), cb->focus ? T (BT_ACCENT) : T (BT_LINE_STRONG));
	gdi_text (hdc, 6, (h - gdi_text_height (hdc, 0)) / 2, cb->text, -1, txt, 0);
	if (cb->focus && kind == 2)
	{
		int cx = 6 + gdi_text_width (hdc, cb->text, -1, 0);
		gdi_rgb_fill (hdc, cx, 4, cx + 1, h - 4, T (BT_ACCENT));
	}
	/* la flèche */
	int ax = w->cw - 14, ay = h / 2 - 2;
	for (int i = 0; i < 4; i++)
		gdi_rgb_fill (hdc, ax + i, ay + i, ax + 8 - i, ay + i + 1, txt);
	user_end (hdc);
}

static void cb_select (struct wnd *w, struct combo *cb, int i, int notify)
{
	struct wnd *lw = wnd_get (cb->lb);
	if (!lw)
		return;
	listbox_impl (cb->lb, LB_SETCURSEL, (uint16_t) i, 0);
	struct listbox *l = lb_of (lw);
	if (i >= 0 && i < l->n)
		snprintf (cb->text, sizeof cb->text, "%s", l->it[i].text);
	else
		cb->text[0] = 0;
	user_invalidate (w->hwnd, 0);
	if (notify)
		user_notify (w->hwnd, 1);	/* CBN_SELCHANGE */
}

static void cb_drop (struct wnd *w, struct combo *cb)
{
	struct wnd *lw = wnd_get (cb->lb);
	if (!lw)
		return;
	struct listbox *l = lb_of (lw);
	if (!l->n)
		return;
	user_notify (w->hwnd, 7);	/* CBN_DROPDOWN */
	char *labels = malloc ((size_t) l->n * 130 + 2);
	int o = 0;
	for (int i = 0; i < l->n; i++)
	{
		char u[128];
		w16_to_utf8 ((const uint8_t *) l->it[i].text, -1, u, sizeof u);
		if (i == l->cur)
			labels[o++] = 2;	/* (coché) */
		o += snprintf (labels + o, 130, "%s", u[0] ? u : " ") + 1;
	}
	labels[o] = 0;
	int h = gdi_text_height (0, 0) + 8;
	int r = user_popup (w->hwnd, 0, h, labels, w->cw);
	free (labels);
	if (r >= 0)
		cb_select (w, cb, r, 1);
	user_notify (w->hwnd, 8);	/* CBN_CLOSEUP */
}

static uint32_t combo_impl (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	struct combo *cb = cb_of (w);
	int kind = (int) (w->style & 3);
	if (msg >= CB_ADDSTRING && msg <= CB_FINDSTRINGEXACT && !cb->lb)
		return 0xffff;
	switch (msg)
	{
	case WM_CREATE:
	{
		/* la liste cachée : elle garde les éléments */
		uint32_t ls = (w->style & 0x2) == 0 ? 0 : 0;
		(void) ls;
		cb->lb = user_create (0, "LISTBOX", "", WS_CHILD | ((w->style & 0x100) ? LBS_SORT : 0) | ((w->style & 0x30) ? (w->style & 0x30) : 0)
			| ((w->style & 0x200) ? LBS_HASSTRINGS : 0), 0, 0, 0, 0, hwnd, 1000, w->hinst, 0);
		struct wnd *lw = wnd_get (cb->lb);
		if (lw)
			lb_of (lw)->combo = 1;
		return 0;
	}
	case WM_PAINT:
		cb_paint (w);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SETFOCUS:
		cb->focus = 1;
		user_invalidate (hwnd, 0);
		user_notify (hwnd, 3);	/* CBN_SETFOCUS */
		return 0;
	case WM_KILLFOCUS:
		cb->focus = 0;
		user_invalidate (hwnd, 0);
		user_notify (hwnd, 4);	/* CBN_KILLFOCUS */
		return 0;
	case WM_LBUTTONDOWN:
		user_set_focus (hwnd);
		if (kind == 3 || (int16_t) lp > w->cw - 22)
			cb_drop (w, cb);
		return 0;
	case WM_KEYDOWN:
	{
		struct wnd *lw = wnd_get (cb->lb);
		struct listbox *l = lw ? lb_of (lw) : 0;
		if (!l)
			return 0;
		if (wp == 0x26 && l->cur > 0)
			cb_select (w, cb, l->cur - 1, 1);
		else if (wp == 0x28 && l->cur < l->n - 1)
			cb_select (w, cb, l->cur + 1, 1);
		else if (wp == 0x73 || (wp == 0x28 && user_key_down (0x12)))
			cb_drop (w, cb);
		return 0;
	}
	case WM_CHAR:
		if (kind == 2 || kind == 1)
		{
			int n = (int) strlen (cb->text);
			if (wp == 8 && n)
				cb->text[n - 1] = 0;
			else if (wp >= 32 && n < 254)
			{
				cb->text[n] = (char) wp;
				cb->text[n + 1] = 0;
			}
			user_invalidate (hwnd, 0);
			user_notify (hwnd, 5);	/* CBN_EDITCHANGE */
		}
		else if (wp >= 32)
		{
			char s[2] = { (char) wp, 0 };
			struct wnd *lw = wnd_get (cb->lb);
			if (lw)
			{
				int i = lb_find (lb_of (lw), lb_of (lw)->cur, s, 0);
				if (i >= 0)
					cb_select (w, cb, i, 1);
			}
		}
		return 0;
	case WM_GETDLGCODE:
		return 0x01 | (kind != 3 ? 0x80 : 0);
	case WM_SETTEXT:
		snprintf (cb->text, sizeof cb->text, "%s", lp ? (const char *) far_ptr (lp) : "");
		user_invalidate (hwnd, 0);
		return 1;
	case WM_GETTEXT:
	{
		int n = (int) strlen (cb->text);
		if (n > wp - 1)
			n = wp - 1;
		if (n < 0)
			return 0;
		memcpy (far_ptr (lp), cb->text, (size_t) n);
		((char *) far_ptr (lp))[n] = 0;
		return (uint32_t) n;
	}
	case WM_GETTEXTLENGTH:
		return (uint32_t) strlen (cb->text);
	case CB_ADDSTRING: return listbox_impl (cb->lb, LB_ADDSTRING, wp, lp);
	case CB_INSERTSTRING: return listbox_impl (cb->lb, LB_INSERTSTRING, wp, lp);
	case CB_DELETESTRING: return listbox_impl (cb->lb, LB_DELETESTRING, wp, lp);
	case CB_DIR: return listbox_impl (cb->lb, LB_DIR, wp, lp);
	case CB_GETCOUNT: return listbox_impl (cb->lb, LB_GETCOUNT, wp, lp);
	case CB_GETCURSEL: return listbox_impl (cb->lb, LB_GETCURSEL, wp, lp);
	case CB_GETLBTEXT: return listbox_impl (cb->lb, LB_GETTEXT, wp, lp);
	case CB_GETLBTEXTLEN: return listbox_impl (cb->lb, LB_GETTEXTLEN, wp, lp);
	case CB_RESETCONTENT: cb->text[0] = 0; user_invalidate (hwnd, 0); return listbox_impl (cb->lb, LB_RESETCONTENT, wp, lp);
	case CB_FINDSTRING: return listbox_impl (cb->lb, LB_FINDSTRING, wp, lp);
	case CB_FINDSTRINGEXACT: return listbox_impl (cb->lb, LB_FINDSTRINGEXACT, wp, lp);
	case CB_GETITEMDATA: return listbox_impl (cb->lb, LB_GETITEMDATA, wp, lp);
	case CB_SETITEMDATA: return listbox_impl (cb->lb, LB_SETITEMDATA, wp, lp);
	case CB_SETITEMHEIGHT: return 0;
	case CB_GETITEMHEIGHT: return listbox_impl (cb->lb, LB_GETITEMHEIGHT, wp, lp);
	case CB_SELECTSTRING:
	{
		uint32_t r = listbox_impl (cb->lb, LB_SELECTSTRING, wp, lp);
		if (r != 0xffff)
			cb_select (w, cb, (int) r, 0);
		return r;
	}
	case CB_SETCURSEL:
		cb_select (w, cb, (int16_t) wp, 0);
		return (uint32_t) (uint16_t) (int16_t) wp;
	case CB_SHOWDROPDOWN:
		if (wp)
			cb_drop (w, cb);
		return 1;
	case CB_GETDROPPEDSTATE:
	case CB_GETEXTENDEDUI:
	case CB_SETEXTENDEDUI:
	case CB_LIMITTEXT:
		return 0;
	case CB_GETEDITSEL:
		return (uint32_t) strlen (cb->text) << 16;
	case CB_SETEDITSEL:
		return 1;
	case CB_GETDROPPEDCONTROLRECT:
		if (lp)
		{
			uint32_t r = far_lin (lp);
			wr16 (r, 0); wr16 (r + 2, 0); wr16 (r + 4, (uint16_t) w->cw); wr16 (r + 6, (uint16_t) (w->ch + 100));
		}
		return 1;
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

/* ======================================================================== SCROLLBAR */
struct sbar { int min, max, pos, drag; };

static struct sbar *sb_of (struct wnd *w)
{
	if (!w->cdata)
	{
		struct sbar *s = calloc (1, sizeof *s);
		s->max = 100;
		w->cdata = s;
	}
	return w->cdata;
}

static void sb_send (struct wnd *w, int code, int pos)
{
	uint16_t msg = (w->style & 1) ? WM_VSCROLL : WM_HSCROLL;
	if (w->parent)
		user_send (w->parent, msg, (uint16_t) code, MAKESEGPTR (w->hwnd, pos));
}

static uint32_t scroll_impl (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	struct sbar *s = sb_of (w);
	int vert = (w->style & 1) != 0;
	int len = vert ? w->ch : w->cw, thick = vert ? w->cw : w->ch;
	int range = s->max - s->min;
	int thumb = thick < 12 ? 12 : thick, track = len - 2 * thick - thumb;
	int tpos = thick + (range > 0 && track > 0 ? (s->pos - s->min) * track / range : 0);
	switch (msg)
	{
	case WM_PAINT:
	{
		uint16_t hdc = user_begin (hwnd);
		gdi_rgb_fill (hdc, 0, 0, w->cw, w->ch, T (BT_SUNKEN));
		uint32_t a = T (BT_TEXT3);
		/* les flèches */
		for (int i = 0; i < 4; i++)
		{
			int c = thick / 2;
			if (vert)
			{
				gdi_rgb_fill (hdc, c - i, thick / 2 - 2 + i, c + i + 1, thick / 2 - 1 + i, a);
				gdi_rgb_fill (hdc, c - i, len - thick / 2 + 2 - i, c + i + 1, len - thick / 2 + 3 - i, a);
			}
			else
			{
				gdi_rgb_fill (hdc, thick / 2 - 2 + i, c - i, thick / 2 - 1 + i, c + i + 1, a);
				gdi_rgb_fill (hdc, len - thick / 2 + 2 - i, c - i, len - thick / 2 + 3 - i, c + i + 1, a);
			}
		}
		if (vert)
			gdi_rgb_rbox (hdc, 2, tpos, w->cw - 2, tpos + thumb, 3, T (BT_LINE_STRONG), T (BT_LINE_STRONG));
		else
			gdi_rgb_rbox (hdc, tpos, 2, tpos + thumb, w->ch - 2, 3, T (BT_LINE_STRONG), T (BT_LINE_STRONG));
		user_end (hdc);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_LBUTTONDOWN:
	{
		int p = vert ? (int16_t) (lp >> 16) : (int16_t) lp;
		if (p < thick)
			sb_send (w, 0, 0);	/* SB_LINEUP */
		else if (p >= len - thick)
			sb_send (w, 1, 0);	/* SB_LINEDOWN */
		else if (p < tpos)
			sb_send (w, 2, 0);	/* SB_PAGEUP */
		else if (p >= tpos + thumb)
			sb_send (w, 3, 0);	/* SB_PAGEDOWN */
		else
		{
			s->drag = 1;
			user_capture (hwnd);
			return 0;
		}
		sb_send (w, 8, 0);	/* SB_ENDSCROLL */
		return 0;
	}
	case WM_MOUSEMOVE:
		if (s->drag && track > 0)
		{
			int p = (vert ? (int16_t) (lp >> 16) : (int16_t) lp) - thick - thumb / 2;
			int v = s->min + p * range / track;
			if (v < s->min) v = s->min;
			if (v > s->max) v = s->max;
			sb_send (w, 5, v);	/* SB_THUMBTRACK */
			s->drag = 2 + v - s->min;
		}
		return 0;
	case WM_LBUTTONUP:
		if (s->drag)
		{
			int v = s->drag >= 2 ? s->drag - 2 + s->min : s->pos;
			s->drag = 0;
			user_capture (0);
			sb_send (w, 4, v);	/* SB_THUMBPOSITION */
			sb_send (w, 8, 0);
		}
		return 0;
	case WM_USER + 0x60:	/* (SetScrollPos) */
	{
		int old = s->pos;
		s->pos = (int16_t) wp;
		if (s->pos < s->min) s->pos = s->min;
		if (s->pos > s->max) s->pos = s->max;
		if (lp)
			user_invalidate (hwnd, 0);
		return (uint32_t) (uint16_t) old;
	}
	case WM_USER + 0x61:
		return (uint32_t) (uint16_t) s->pos;
	case WM_USER + 0x62:
		s->min = (int16_t) wp;
		s->max = (int16_t) lp;
		if (s->pos < s->min) s->pos = s->min;
		if (s->pos > s->max) s->pos = s->max;
		user_invalidate (hwnd, 0);
		return 0;
	case WM_USER + 0x63:
		return (uint32_t) (uint16_t) s->min | (uint32_t) (uint16_t) s->max << 16;
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

/* ======================================================================== mode natif : les widgets de UIKit
 * (demande de l'utilisateur, 20/11 : « utiliser l'uikit y compris pour le theming, comme ça ça ressemblera à des
 * apps natives »). Un contrôle standard devient un widget de UIKit posé à sa place dans la fenêtre de Korin : UIKit
 * le dessine et le fait vivre (survol, focus, clavier, thème) ; nos structures restent la vérité pour le programme
 * (LB_GETTEXT, BM_GETCHECK…), poussée vers le widget, et ce que fait l'utilisateur revient en notifications
 * (BN_CLICKED, EN_CHANGE, LBN_SELCHANGE, CBN_SELCHANGE, WM_VSCROLL). Restent à nous : cadres de groupe, contrôles
 * dessinés par le programme (owner-draw), listes à choix multiple, et tout si W16_NATIVE=0 (ou sur l'hôte). */
#define MAX_NAT 512
struct nat { uint16_t hwnd; long wd, be; int kind, items_dirty, shown, enabled, x, y, w, h; };
static struct nat nats[MAX_NAT];
static int native_off = -1;

static struct nat *nat_of (uint16_t hwnd, int make)
{
	struct nat *fr = 0;
	for (int i = 0; i < MAX_NAT; i++)
	{
		if (nats[i].hwnd == hwnd)
			return &nats[i];
		if (!nats[i].hwnd && !fr)
			fr = &nats[i];
	}
	if (!make || !fr)
		return 0;
	memset (fr, 0, sizeof *fr);
	fr->hwnd = hwnd;
	fr->kind = -2;	/* (pas encore décidé) */
	return fr;
}

/* le genre de widget de ce contrôle, -1 : dessiné par nous */
static int native_kind (struct wnd *w)
{
	if (native_off < 0)
	{
		const char *e = getenv ("W16_NATIVE");
		native_off = e && e[0] == '0';
	}
	if (native_off)
		return -1;
	const char *c = w->clsname;
	uint32_t st = w->style;
	struct wnd *base = w;
	(void) base;
	/* (les classes du programme qui reprennent une classe du système : TButton de Delphi… ; le type vient de
	 * la procédure de fenêtre d'origine, ici retrouvée par le nom de la classe système passé à ctrl_*_proc) */
	if (!strcasecmp (c, "BUTTON") || w->cdata_kind == 1)
	{
		int k = (int) (st & 0x0f);
		if (k == 0) return BW_PUSH;
		if (k == 1) return BW_DEFPUSH;
		if (k == 2 || k == 3 || k == 5 || k == 6) return BW_CHECK;
		if (k == 4 || k == 9) return BW_RADIO;
		if (k == 7) return BW_GROUP;
		return -1;
	}
	if (!strcasecmp (c, "STATIC") || w->cdata_kind == 2)
	{
		int k = (int) (st & 0x1f);
		if (k == 0 || k == 0x0b || k == 0x0c) return BW_LABEL;
		if (k == 1) return BW_LABEL_CENTER;
		if (k == 2) return BW_LABEL_RIGHT;
		return -1;
	}
	if (!strcasecmp (c, "EDIT") || w->cdata_kind == 3)
		return (st & ES_MULTILINE) ? BW_TEXTAREA : (st & ES_PASSWORD) ? BW_PASSWORD : BW_TEXT;
	if (!strcasecmp (c, "LISTBOX") || w->cdata_kind == 4)
	{
		struct listbox *l = w->cdata;
		if ((st & (LBS_OWNERDRAW | LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) || (l && l->combo))
			return -1;
		return BW_LIST;
	}
	if (!strcasecmp (c, "COMBOBOX") || w->cdata_kind == 5)
	{
		if (st & 0x30)	/* owner-draw */
			return -1;
		return (st & 3) == 3 ? BW_DROPDOWN : BW_COMBO;
	}
	if (!strcasecmp (c, "SCROLLBAR") || w->cdata_kind == 6)
		return (st & 1) ? BW_VSCROLL : BW_HSCROLL;
	return -1;
}

static int all_visible (struct wnd *w)
{
	for (; w; w = (w->style & WS_CHILD) ? wnd_get (w->parent) : 0)
		if (!w->visible)
			return 0;
	return 1;
}

static void utf8_of (const char *cp, char *out, int cap, int amp)
{
	char tmp[1024];
	if (amp)
	{
		strip_amp (cp, tmp, sizeof tmp);
		cp = tmp;
	}
	w16_to_utf8 ((const uint8_t *) cp, -1, out, cap);
}

/* les éléments d'une liste ou d'une liste déroulante, au format de UIKit */
static void items_text (struct wnd *w, char *out, int cap, char sep)
{
	struct listbox *l = 0;
	if (w->cdata_kind == 4 || !strcasecmp (w->clsname, "LISTBOX"))
		l = w->cdata;
	else
	{
		struct combo *cb = w->cdata;
		struct wnd *lw = cb ? wnd_get (cb->lb) : 0;
		l = lw ? lw->cdata : 0;
	}
	int o = 0;
	out[0] = 0;
	for (int i = 0; l && i < l->n && o < cap - 260; i++)
	{
		char u[256];
		w16_to_utf8 ((const uint8_t *) l->it[i].text, -1, u, sizeof u);
		for (char *q = u; *q; q++)
			if (*q == sep)
				*q = '/';
		o += snprintf (out + o, (size_t) (cap - o), "%s%s", i ? (char[2]) { sep, 0 } : "", u);
	}
}

/* le mode natif est-il là (UIKit, et pas W16_NATIVE=0) ? */
int native_enabled (void)
{
	if (native_off < 0)
	{
		const char *e = getenv ("W16_NATIVE");
		native_off = e && e[0] == '0';
	}
	return !native_off && be_ui_metric (BEM_PX) > 0;
}

static int list_cur (struct wnd *w)
{
	if (w->cdata_kind == 4 || !strcasecmp (w->clsname, "LISTBOX"))
		return w->cdata ? ((struct listbox *) w->cdata)->cur : -1;
	struct combo *cb = w->cdata;
	struct wnd *lw = cb ? wnd_get (cb->lb) : 0;
	return lw && lw->cdata ? ((struct listbox *) lw->cdata)->cur : -1;
}

/* le widget du contrôle : créé, placé, montré ou caché selon le contrôle et ses parents ; 1 s'il est là */
static int native_sync (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	struct nat *n = nat_of (hwnd, 1);
	if (!n)
		return 0;
	if (n->kind == -2)
		n->kind = native_kind (w);
	if (n->kind < 0)
		return 0;
	int ox = 0, oy = 0, cw = 0, ch = 0;
	long be = 0;
	int have = wnd_client_geometry (hwnd, &ox, &oy, &cw, &ch, &be);
	if (!have)
		be = 0;
	if (n->wd && n->be != be)
		n->wd = 0;	/* (la fenêtre de Korin a été fermée : ses widgets avec elle) */
	int vis = have && all_visible (w);
	if (!vis)
	{
		if (n->wd && n->shown)
			be_w_show (n->wd, 0);
		n->shown = 0;
		return 0;
	}
	int x = ox - w->cx, y = oy - w->cy, ww = w->w, hh = w->h;
	/* (une liste déroulante de Win16 compte son menu ouvert dans sa hauteur : le widget n'a que son champ) */
	if ((n->kind == BW_DROPDOWN || n->kind == BW_COMBO) && be_ui_metric (BEM_CTL) > 0 && hh > be_ui_metric (BEM_CTL))
		hh = be_ui_metric (BEM_CTL);
	if (n->wd && n->items_dirty && n->kind == BW_DROPDOWN)
	{
		be_w_destroy (n->wd);	/* (les éléments d'une liste déroulante sont donnés à sa création) */
		n->wd = 0;
	}
	if (!n->wd)
	{
		char text[4096];
		long extra = 0;
		switch (n->kind)
		{
		case BW_DROPDOWN:
		case BW_COMBO:
			items_text (w, text, sizeof text, n->kind == BW_DROPDOWN ? '|' : '\n');
			break;
		case BW_CHECK:
			extra = btn_of (w)->check != 0;
			utf8_of (w->text, text, sizeof text, 1);
			break;
		case BW_RADIO:
		{
			/* (le groupe : le premier bouton du groupe, comme WS_GROUP) */
			uint16_t first = hwnd;
			for (uint16_t h = hwnd; h >= 0x1000; h -= 4)
			{
				struct wnd *k = wnd_get (h);
				if (!k || k->parent != w->parent || !(k->style & WS_CHILD))
					continue;
				first = h;
				if (k->style & WS_GROUP)
					break;
			}
			extra = first;
			utf8_of (w->text, text, sizeof text, 1);
			break;
		}
		case BW_TEXT:
		case BW_PASSWORD:
		case BW_TEXTAREA:
		{
			struct edit *e = edit_of (w);
			w16_to_utf8 ((const uint8_t *) e->buf, e->len, text, sizeof text);
			extra = (w->style & ES_READONLY) ? BWF_READONLY : 0;
			if (n->kind == BW_TEXTAREA)
				extra |= ((w->style & WS_VSCROLL) ? BWF_VSCROLL : 0) | ((w->style & WS_HSCROLL) ? BWF_HSCROLL : 0);
			break;
		}
		default:
			utf8_of (w->text, text, sizeof text, n->kind >= BW_LABEL && n->kind <= BW_LABEL_RIGHT ? !(w->style & 0x80) : 1);
			break;
		}
		n->wd = be_w_create (be, n->kind, x, y, ww, hh, text, extra);
		if (!n->wd)
		{
			n->kind = -1;	/* (UIKit refuse : nous dessinons) */
			return 0;
		}
		n->be = be;
		n->x = x; n->y = y; n->w = ww; n->h = hh;
		n->shown = 1;
		n->enabled = 1;
		n->items_dirty = 1;
		if (n->kind == BW_RADIO && btn_of (w)->check)
			be_w_set_value (n->wd, 1);
	}
	if (n->x != x || n->y != y || n->w != ww || n->h != hh)
	{
		be_w_move (n->wd, x, y, ww, hh);
		n->x = x; n->y = y; n->w = ww; n->h = hh;
	}
	if (n->items_dirty)
	{
		n->items_dirty = 0;
		if (n->kind == BW_LIST)
		{
			struct listbox *l = w->cdata;
			be_w_list_clear (n->wd);
			for (int i = 0; l && i < l->n; i++)
			{
				char u[256];
				w16_to_utf8 ((const uint8_t *) l->it[i].text, -1, u, sizeof u);
				be_w_list_add (n->wd, u);
			}
		}
		if (n->kind == BW_LIST || n->kind == BW_DROPDOWN || n->kind == BW_COMBO)
			be_w_set_value (n->wd, list_cur (w));
		if (n->kind == BW_COMBO)
		{
			char u[512];
			w16_to_utf8 ((const uint8_t *) cb_of (w)->text, -1, u, sizeof u);
			be_w_set_text (n->wd, u);
		}
	}
	int en = !w->disabled;
	if (en != n->enabled)
	{
		be_w_enable (n->wd, en);
		n->enabled = en;
	}
	if (!n->shown)
	{
		be_w_show (n->wd, 1);
		n->shown = 1;
	}
	return 1;
}

static void native_forget (uint16_t hwnd)
{
	struct nat *n = nat_of (hwnd, 0);
	if (!n)
		return;
	long be;
	int ox, oy, cw, ch;
	if (n->wd && wnd_client_geometry (hwnd, &ox, &oy, &cw, &ch, &be) && be == n->be)
		be_w_destroy (n->wd);
	memset (n, 0, sizeof *n);
}

/* un contrôle natif : son widget le dessine */
static int native_has (uint16_t hwnd)
{
	struct nat *n = nat_of (hwnd, 0);
	return n && n->wd && n->kind >= 0;
}

/* ce que le programme change : poussé vers le widget */
static void native_after (uint16_t hwnd, uint16_t msg, uint16_t wp)
{
	struct nat *n = nat_of (hwnd, 0);
	struct wnd *w = wnd_get (hwnd);
	if (!n || n->kind < 0 || !w)
		return;
	if (!n->wd)
	{
		if (msg == WM_SHOWWINDOW || msg == WM_MOVE || msg == WM_SIZE)
			native_sync (hwnd);
		return;
	}
	switch (msg)
	{
	case WM_SHOWWINDOW:
	case WM_MOVE:
	case WM_SIZE:
	case WM_ENABLE:
		native_sync (hwnd);
		break;
	case WM_SETTEXT:
		if (n->kind == BW_TEXT || n->kind == BW_PASSWORD || n->kind == BW_TEXTAREA)
		{
			struct edit *e = edit_of (w);
			char u[8192];
			w16_to_utf8 ((const uint8_t *) e->buf, e->len, u, sizeof u);
			be_w_set_text (n->wd, u);
		}
		else if (n->kind == BW_COMBO)
		{
			char u[512];
			w16_to_utf8 ((const uint8_t *) cb_of (w)->text, -1, u, sizeof u);
			be_w_set_text (n->wd, u);
		}
		else if (n->kind != BW_LIST && n->kind != BW_DROPDOWN)
		{
			char u[1024];
			utf8_of (w->text, u, sizeof u, 1);
			be_w_set_text (n->wd, u);
		}
		break;
	case BM_SETCHECK:
		if (n->kind == BW_CHECK || n->kind == BW_RADIO)
			be_w_set_value (n->wd, wp != 0);
		break;
	case WM_USER + 0x60:	/* (SetScrollPos) */
	case WM_USER + 0x62:	/* (SetScrollRange) */
	{
		struct sbar *s = sb_of (w);
		be_w_set_range (n->wd, s->min, s->max, (s->max - s->min) / 10 + 1, 1);
		be_w_set_value (n->wd, s->pos);
		break;
	}
	default:
		if (n->kind == BW_LIST && msg >= LB_ADDSTRING && msg <= LB_FINDSTRINGEXACT && msg != LB_GETTEXT
			&& msg != LB_GETTEXTLEN && msg != LB_GETCOUNT && msg != LB_GETCURSEL && msg != LB_GETITEMDATA)
		{
			n->items_dirty = 1;
			native_sync (hwnd);
		}
		if ((n->kind == BW_DROPDOWN || n->kind == BW_COMBO) && msg >= CB_ADDSTRING && msg <= CB_FINDSTRINGEXACT
			&& msg != CB_GETCOUNT && msg != CB_GETCURSEL && msg != CB_GETLBTEXT && msg != CB_GETLBTEXTLEN && msg != CB_GETITEMDATA)
		{
			n->items_dirty = 1;
			if (msg == CB_SETCURSEL || msg == CB_SELECTSTRING)
			{
				be_w_set_value (n->wd, list_cur (w));
				n->items_dirty = 0;
			}
		}
		break;
	}
}

/* ce que fait l'utilisateur dans un widget (user.c, BE_WIDGET) */
void ctrl_native_event (long wd, int code)
{
	struct nat *n = 0;
	for (int i = 0; i < MAX_NAT && !n; i++)
		if (nats[i].hwnd && nats[i].wd == wd)
			n = &nats[i];
	if (!n)
		return;
	uint16_t hwnd = n->hwnd;
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return;
	if (user_get_focus () != hwnd && n->kind != BW_LABEL && n->kind != BW_LABEL_CENTER && n->kind != BW_LABEL_RIGHT)
		user_set_focus (hwnd);
	switch (n->kind)
	{
	case BW_PUSH:
	case BW_DEFPUSH:
		if (code == BWN_ACTIVATE)
			user_notify (hwnd, 0);	/* BN_CLICKED */
		break;
	case BW_CHECK:
	{
		int k = (int) (w->style & 0x0f);
		long v = be_w_get_value (n->wd);
		if (k == 3 || k == 6)	/* automatiques : l'état suit le widget */
			btn_of (w)->check = (int) v;
		else
			be_w_set_value (n->wd, btn_of (w)->check);	/* (le programme décide, par BM_SETCHECK) */
		user_notify (hwnd, 0);
		break;
	}
	case BW_RADIO:
		if ((w->style & 0x0f) == 9)
		{
			/* (les autres boutons du groupe : décochés par UIKit, nos états suivent) */
			for (int i = 0; i < MAX_NAT; i++)
				if (nats[i].hwnd && nats[i].kind == BW_RADIO && nats[i].wd)
				{
					struct wnd *k = wnd_get (nats[i].hwnd);
					if (k && k->parent == w->parent)
						btn_of (k)->check = (int) be_w_get_value (nats[i].wd);
				}
			btn_of (w)->check = 1;
		}
		else
			be_w_set_value (n->wd, btn_of (w)->check);
		user_notify (hwnd, 0);
		break;
	case BW_TEXT:
	case BW_PASSWORD:
	case BW_TEXTAREA:
		if (code == BWN_CHANGE)
		{
			struct edit *e = edit_of (w);
			const char *t = be_w_get_text (n->wd);
			uint8_t raw[8192];
			int len = w16_from_utf8 (t ? t : "", raw, sizeof raw);
			if (len + 1 > e->cap)
			{
				e->cap = len + 256;
				e->buf = realloc (e->buf, (size_t) e->cap);
			}
			memcpy (e->buf, raw, (size_t) len + 1);
			e->len = len;
			e->caret = e->anchor = len;
			e->modified = 1;
			user_notify (hwnd, 0x400);	/* EN_UPDATE */
			user_notify (hwnd, 0x300);	/* EN_CHANGE */
		}
		else if (code == BWN_ACTIVATE)
			user_post (hwnd, WM_KEYDOWN, 0x0d, 1);	/* (Entrée : au dialogue, le bouton par défaut) */
		break;
	case BW_LIST:
	{
		struct listbox *l = w->cdata;
		if (l)
			l->cur = (int) be_w_get_value (n->wd);
		if (w->style & LBS_NOTIFY)
			user_notify (hwnd, code == BWN_ACTIVATE ? 2 : 1);	/* LBN_DBLCLK, LBN_SELCHANGE */
		break;
	}
	case BW_DROPDOWN:
	case BW_COMBO:
	{
		struct combo *cb = cb_of (w);
		struct wnd *lw = wnd_get (cb->lb);
		struct listbox *l = lw ? lw->cdata : 0;
		long v = be_w_get_value (n->wd);
		if (n->kind == BW_COMBO && code == BWN_CHANGE)
		{
			const char *t = be_w_get_text (n->wd);
			w16_from_utf8 (t ? t : "", (uint8_t *) cb->text, sizeof cb->text);
		}
		if (l && v >= 0 && v < l->n && v != l->cur)
		{
			l->cur = (int) v;
			snprintf (cb->text, sizeof cb->text, "%s", l->it[v].text);
			user_notify (hwnd, 1);	/* CBN_SELCHANGE */
		}
		else if (n->kind == BW_COMBO && code == BWN_CHANGE)
			user_notify (hwnd, 5);	/* CBN_EDITCHANGE */
		break;
	}
	case BW_VSCROLL:
	case BW_HSCROLL:
	{
		struct sbar *s = sb_of (w);
		s->pos = (int) be_w_get_value (n->wd);
		sb_send (w, 5, s->pos);	/* SB_THUMBTRACK */
		sb_send (w, 4, s->pos);	/* SB_THUMBPOSITION */
		sb_send (w, 8, 0);	/* SB_ENDSCROLL */
		break;
	}
	}
}

/* après le WM_PAINT d'une fenêtre : les widgets de ses filles par-dessus (le fond du parent a pu les couvrir) */
void ctrl_native_redraw_under (uint16_t hwnd)
{
	for (int i = 0; i < MAX_NAT; i++)
	{
		if (!nats[i].hwnd || !nats[i].wd || !nats[i].shown)
			continue;
		struct wnd *k = wnd_get (nats[i].hwnd);
		int under = 0;
		while (k && (k->style & WS_CHILD) && !under)
		{
			under = k->parent == hwnd;
			k = wnd_get (k->parent);
		}
		if (under)
			be_w_draw (nats[i].wd);
	}
}

/* une fenêtre montrée, cachée ou fermée : les widgets de ses filles suivent */
void ctrl_native_sync_tree (uint16_t hwnd)
{
	for (int i = 0; i < MAX_NAT; i++)
	{
		if (!nats[i].hwnd || nats[i].kind < 0)
			continue;
		struct wnd *k = wnd_get (nats[i].hwnd);
		int under = nats[i].hwnd == hwnd;
		while (k && (k->style & WS_CHILD) && !under)
		{
			under = k->parent == hwnd;
			k = wnd_get (k->parent);
		}
		if (under)
			native_sync (nats[i].hwnd);
	}
}

/* le focus du programme sur un contrôle natif : le widget le prend */
void ctrl_native_focus (uint16_t hwnd)
{
	struct nat *n = nat_of (hwnd, 0);
	if (n && n->wd && n->kind >= 0)
		be_w_focus (n->wd);
}

/* les procédures des classes du système : le mode natif autour des nôtres */
static uint32_t native_wrap (int ck, native_wndproc impl, uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (w && !w->cdata_kind)
		w->cdata_kind = ck;
	if (msg == WM_PAINT && native_sync (hwnd))
	{
		struct nat *n = nat_of (hwnd, 0);
		uint16_t hdc = user_begin (hwnd);	/* (valide la fenêtre) */
		user_end (hdc);
		be_w_draw (n->wd);
		return 0;
	}
	if (msg == WM_ERASEBKGND && native_has (hwnd))
		return 1;
	if (msg == WM_NCDESTROY)
		native_forget (hwnd);
	uint32_t r = impl (hwnd, msg, wp, lp);
	if (msg == WM_SETFOCUS)
		ctrl_native_focus (hwnd);
	native_after (hwnd, msg, wp);
	return r;
}

uint32_t ctrl_button_proc (uint16_t h, uint16_t m, uint16_t wp, uint32_t lp) { return native_wrap (1, button_impl, h, m, wp, lp); }
uint32_t ctrl_static_proc (uint16_t h, uint16_t m, uint16_t wp, uint32_t lp) { return native_wrap (2, static_impl, h, m, wp, lp); }
uint32_t ctrl_edit_proc (uint16_t h, uint16_t m, uint16_t wp, uint32_t lp) { return native_wrap (3, edit_impl, h, m, wp, lp); }
uint32_t ctrl_listbox_proc (uint16_t h, uint16_t m, uint16_t wp, uint32_t lp) { return native_wrap (4, listbox_impl, h, m, wp, lp); }
uint32_t ctrl_combo_proc (uint16_t h, uint16_t m, uint16_t wp, uint32_t lp) { return native_wrap (5, combo_impl, h, m, wp, lp); }
uint32_t ctrl_scroll_proc (uint16_t h, uint16_t m, uint16_t wp, uint32_t lp) { return native_wrap (6, scroll_impl, h, m, wp, lp); }

void ctrl_register (void)
{
	/* CS_DBLCLKS (8), CS_PARENTDC (0x80) ; fond : COLOR_BTNFACE + 1 ou COLOR_WINDOW + 1 */
	user_register_system_class ("BUTTON", ctrl_button_proc, 0x88, 0, 0);
	user_register_system_class ("STATIC", ctrl_static_proc, 0x80, 0, 0);
	user_register_system_class ("EDIT", ctrl_edit_proc, 0x88, 0, 0);
	user_register_system_class ("LISTBOX", ctrl_listbox_proc, 0x88, 0, 0);
	user_register_system_class ("COMBOBOX", ctrl_combo_proc, 0x88, 0, 0);
	user_register_system_class ("SCROLLBAR", ctrl_scroll_proc, 0x88, 0, 0);
	dialog_register ();
}
