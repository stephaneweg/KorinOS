/* dialog.c -- win16 : les dialogues (DialogBox, CreateDialog, gabarits DIALOG des ressources, DefDlgProc,
 * IsDialogMessage, GetDlgItem et ses compagnons). D'après user/dialog.c de Wine (LGPL-2.1+).
 *
 * Un dialogue est une fenêtre principale de Korin (classe « #32770 », procédure DefDlgProc écrite en C) ; ses
 * contrôles sont des fenêtres filles des classes de ctrl.c. Unités de dialogue : la police du gabarit
 * (MS Sans Serif 8 : 6 × 13 pixels de base), sinon la police système (8 × 16). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "w16.h"
#include "be.h"
#include "user.h"
#include "gdi.h"

#define DWL_MSGRESULT 0
#define DWL_DLGPROC 4
#define DWL_USER 8
#define DS_SETFONT 0x40
#define DM_GETDEFID (WM_USER + 0)
#define DM_SETDEFID (WM_USER + 1)
#define IDOK 1
#define IDCANCEL 2

int user_take (void *m, uint16_t hwnd, uint16_t lo, uint16_t hi, int remove, int wait);
int native_enabled (void);
uint32_t user_dispatch (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp, uint32_t time);
uint32_t user_def_window_proc (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);
int user_register_system_class (const char *name, native_wndproc fn, uint16_t style, uint16_t brush, int wndextra);
struct qmsg { uint16_t hwnd, msg, wp; uint32_t lp, time; int16_t x, y; };

static uint32_t dlg_get32 (struct wnd *w, int off) { uint32_t v; memcpy (&v, w->extra + off, 4); return v; }
static void dlg_set32 (struct wnd *w, int off, uint32_t v) { memcpy (w->extra + off, &v, 4); }

static int is_dialog (uint16_t h)
{
	struct wnd *w = wnd_get (h);
	return w && w->dialog;
}

/* ---- DefDlgProc ---- */
static uint16_t first_tab (uint16_t dlg);
static uint16_t default_button (uint16_t dlg);

uint32_t dlg_def_proc (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	uint32_t proc = dlg_get32 (w, DWL_DLGPROC);
	if (proc && msg != WM_NCCREATE)
	{
		dlg_set32 (w, DWL_MSGRESULT, 0);
		uint16_t args[5] = { hwnd, msg, wp, (uint16_t) (lp >> 16), (uint16_t) lp };
		uint32_t r = call16 (proc, args, 5, ne_ds_for (proc)) & 0xffff;
		w = wnd_get (hwnd);
		if (!w)
			return r;
		if (r)
		{
			/* (ces messages rendent la valeur de la procédure elle-même) */
			if (msg == WM_CTLCOLOR || msg == 0x0137 || msg == WM_INITDIALOG || msg == 0x0030 || msg == 0x0087)
				return r;
			return dlg_get32 (w, DWL_MSGRESULT);
		}
	}
	switch (msg)
	{
	case WM_NCCREATE:
		return 1;
	case WM_ERASEBKGND:
		gdi_rgb_fill (wp, 0, 0, w->cw, w->ch, be_theme (BT_FACE));	/* (le fond des fenêtres de UIKit) */
		return 1;
	case WM_CLOSE:
	{
		/* (comme Windows : Annuler, s'il est là et actif) */
		user_post (hwnd, WM_COMMAND, IDCANCEL, 0);
		return 0;
	}
	case DM_GETDEFID:
	{
		uint16_t b = default_button (hwnd);
		struct wnd *k = wnd_get (b);
		return k ? (uint32_t) (k->id | 0x534b << 16) : 0;
	}
	case WM_SETFOCUS:
	{
		uint16_t f = first_tab (hwnd);
		if (f)
			user_set_focus (f);
		return 0;
	}
	case WM_CTLCOLOR:
	{
		uint16_t type = (uint16_t) (lp >> 16);
		if (type == 1 || type == 2)	/* EDIT, LISTBOX */
		{
			gdi_set_colors (wp, user_sys_color (8), user_sys_color (5));
			return 6;
		}
		gdi_set_colors (wp, user_sys_color (8), user_sys_color (15));
		return 16;
	}
	}
	return user_def_window_proc (hwnd, msg, wp, lp);
}

/* ---- contrôles d'un dialogue ---- */
static int ctl_is (struct wnd *k, const char *cls) { return !strcasecmp (k->clsname, cls); }

static uint16_t first_tab (uint16_t dlg)
{
	for (uint16_t h = 0x1000; h < 0x1000 + 512 * 4; h += 4)
	{
		struct wnd *k = wnd_get (h);
		if (k && (k->style & WS_CHILD) && k->parent == dlg && (k->style & WS_TABSTOP) && k->visible && !k->disabled)
			return h;
	}
	return 0;
}

static uint16_t default_button (uint16_t dlg)
{
	for (uint16_t h = 0x1000; h < 0x1000 + 512 * 4; h += 4)
	{
		struct wnd *k = wnd_get (h);
		if (k && (k->style & WS_CHILD) && k->parent == dlg && ctl_is (k, "BUTTON") && (k->style & 0x0f) == 1)	/* BS_DEFPUSHBUTTON */
			return h;
	}
	return 0;
}

/* le contrôle suivant ou précédent dans l'ordre de tabulation */
static uint16_t next_tab (uint16_t dlg, uint16_t from, int back)
{
	uint16_t list[256];
	int n = 0, cur = -1;
	for (uint16_t h = 0x1000; h < 0x1000 + 512 * 4 && n < 256; h += 4)
	{
		struct wnd *k = wnd_get (h);
		if (k && (k->style & WS_CHILD) && k->parent == dlg && (k->style & WS_TABSTOP) && k->visible && !k->disabled)
		{
			if (h == from)
				cur = n;
			list[n++] = h;
		}
	}
	if (!n)
		return 0;
	if (cur < 0)
		return list[0];
	return list[(cur + (back ? n - 1 : 1)) % n];
}

/* ---- gabarit ---- */
static const uint8_t *read_sz (const uint8_t *p, char *out, int cap, int *ord)
{
	*ord = -1;
	if (p[0] == 0xff)
	{
		*ord = p[1] | p[2] << 8;
		if (out)
			snprintf (out, (size_t) cap, "#%d", *ord);
		return p + 3;
	}
	int l = (int) strlen ((const char *) p);
	if (out)
		snprintf (out, (size_t) cap, "%s", (const char *) p);
	return p + l + 1;
}

static uint16_t create_dialog (uint16_t hinst, const uint8_t *t, uint16_t owner, uint32_t proc, uint32_t param, int modal)
{
	uint32_t style = (uint32_t) (t[0] | t[1] << 8 | t[2] << 16 | (uint32_t) t[3] << 24);
	int n = t[4];
	int x = (int16_t) (t[5] | t[6] << 8), y = (int16_t) (t[7] | t[8] << 8), cx = (int16_t) (t[9] | t[10] << 8), cy = (int16_t) (t[11] | t[12] << 8);
	const uint8_t *p = t + 13;
	char menu[64], cls[64], caption[256], face[64] = "";
	int ord, pt = 0;
	p = read_sz (p, menu, sizeof menu, &ord);
	int menu_ord = ord;
	p = read_sz (p, cls, sizeof cls, &ord);
	p = read_sz (p, caption, sizeof caption, &ord);
	if (style & DS_SETFONT)
	{
		pt = p[0] | p[1] << 8;
		p = read_sz (p + 2, face, sizeof face, &ord);
	}
	/* unités de dialogue -> pixels */
	int bx = 8, by = 16;
	uint16_t hfont = 0;
	if (style & DS_SETFONT)
	{
		/* (une police de pt points à 96 ppp : la police de Korin à la même taille) */
		int px = pt * 96 / 72;
		uint16_t sel = global_alloc (64, 0);
		uint32_t lf = sel_base (sel);
		wr16 (lf, (uint16_t) -px);
		wr16 (lf + 8, 400);
		snprintf ((char *) lin_ptr (lf + 18), 32, "%s", face);
		struct w16call cf = { .a = { MAKESEGPTR (sel, 0) }, .n = 1 };
		extern void api_CreateFontIndirect16 (struct w16call *);
		api_CreateFontIndirect16 (&cf);
		hfont = (uint16_t) cf.ret;
		global_free (sel);
		uint16_t hdc = gdi_dc_for_window (0);
		bx = gdi_text_width (hdc, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", -1, hfont) / 52;
		by = gdi_text_height (hdc, hfont);
		gdi_release_dc (hdc);
		if (bx < 4) bx = 4;
		if (by < 8) by = 8;
	}
	/* mode natif (widgets de UIKit) : les unités suivent la police de UIKit, comme les « grandes polices » de
	 * Windows ; le dialogue grandit juste assez pour ses widgets */
	int upx = native_enabled () ? be_ui_metric (BEM_PX) : 0;
	if (upx > 0)
	{
		int nbx = be_text_width ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", upx, 0) / 52;
		int nby = be_font_height (upx);
		if (nbx > bx)
			bx = nbx;
		if (nby > by)
			by = nby;
	}
#define DX(v) ((v) * bx / 4)
#define DY(v) ((v) * by / 8)
	uint32_t wstyle = (style & ~(WS_VISIBLE | WS_CHILD)) | WS_POPUP;
	int dw = DX (cx), dh = DY (cy);
	/* (la taille donnée est celle de la zone cliente : on ajoute le cadre que wnd_geometry retire) */
	uint16_t hwnd = user_create (0, cls[0] ? cls : "#32770", caption, wstyle & ~WS_VISIBLE, -1, -1, dw, dh + W16_CAPTION, owner,
		0, hinst, param);
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	(void) x;
	(void) y;
	w->dialog = 1;
	w->cw = dw;
	w->ch = dh;
	w->font = hfont;
	dlg_set32 (w, DWL_DLGPROC, proc);
	if (menu_ord >= 0 || menu[0])
	{
		extern uint16_t menu_load (uint16_t hinst, uint32_t name);
		(void) menu_load;
	}
	/* les contrôles */
	uint16_t focus = 0;
	for (int i = 0; i < n; i++)
	{
		int ix = (int16_t) (p[0] | p[1] << 8), iy = (int16_t) (p[2] | p[3] << 8);
		int iw = (int16_t) (p[4] | p[5] << 8), ih = (int16_t) (p[6] | p[7] << 8);
		uint16_t id = (uint16_t) (p[8] | p[9] << 8);
		uint32_t is = (uint32_t) (p[10] | p[11] << 8 | p[12] << 16 | (uint32_t) p[13] << 24);
		p += 14;
		char icls[64], text[256];
		if (p[0] & 0x80)
		{
			static const char *pre[] = { "BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX" };
			int k = p[0] & 0x7f;
			snprintf (icls, sizeof icls, "%s", k < 6 ? pre[k] : "STATIC");
			p++;
		}
		else
			p = read_sz (p, icls, sizeof icls, &ord);
		p = read_sz (p, text, sizeof text, &ord);
		if (ord >= 0)
			snprintf (text, sizeof text, "#%d", ord);
		int extra = p[0];
		p += 1 + extra;
		w16_log ("   contrôle %d : %s « %s » id %u, style %08x\n", i, icls, text, id, is);
		uint16_t h = user_create (0, icls, text, is | WS_CHILD, DX (ix), DY (iy), DX (iw), DY (ih), hwnd, id, hinst, 0);
		if (!h)
			continue;
		if (hfont)
			user_send (h, WM_SETFONT, hfont, 0);
		if (!focus && (is & WS_TABSTOP) && !(is & WS_DISABLED))
			focus = h;
	}
	if (!wnd_get (hwnd))
		return 0;
	if (hfont)
		user_send (hwnd, WM_SETFONT, hfont, 0);
	uint32_t r = user_send (hwnd, WM_INITDIALOG, focus, param);
	if (!wnd_get (hwnd))
		return 0;
	if (r && focus)
		user_set_focus (focus);
	if ((style & WS_VISIBLE) || modal)
	{
		struct w16call s = { .a = { hwnd, 1 }, .n = 2 };
		extern void api_ShowWindow16 (struct w16call *);
		api_ShowWindow16 (&s);
		if (r && focus && wnd_get (focus))
			user_set_focus (focus);
	}
	return hwnd;
}

int dlg_is_message (uint16_t dlg, struct qmsg *m);

static int dialog_loop (uint16_t hwnd, uint16_t owner)
{
	struct wnd *ow = wnd_get (owner);
	int was = 0;
	if (ow)
	{
		was = ow->disabled;
		ow->disabled = 1;
	}
	struct qmsg m;
	int result = -1;
	for (;;)
	{
		struct wnd *w = wnd_get (hwnd);
		if (!w || w->dlg_end || w16_exit)
		{
			result = w ? w->dlg_result : result;
			break;
		}
		user_take (&m, 0, 0, 0, 1, 1);
		if (w16_exit)
			break;
		if (m.msg == WM_QUIT)
		{
			user_post (0, WM_QUIT, m.wp, 0);	/* (rendu à la boucle principale) */
			struct wnd *x = wnd_get (hwnd);
			result = x ? x->dlg_result : 0;
			break;
		}
		if (!dlg_is_message (hwnd, &m))
		{
			struct w16call t = { 0 };
			(void) t;
			if (m.msg == WM_KEYDOWN || m.msg == WM_SYSKEYDOWN)
			{
				extern void api_TranslateMessage16 (struct w16call *);
				/* (TranslateMessage veut un MSG en mémoire 16 bits : rangé dans un bloc temporaire) */
				uint16_t sel = global_alloc (32, 0);
				uint32_t a = sel_base (sel);
				wr16 (a, m.hwnd);
				wr16 (a + 2, m.msg);
				wr16 (a + 4, m.wp);
				wr32 (a + 6, m.lp);
				struct w16call tm = { .a = { MAKESEGPTR (sel, 0) }, .n = 1 };
				api_TranslateMessage16 (&tm);
				global_free (sel);
			}
			user_dispatch (m.hwnd, m.msg, m.wp, m.lp, m.time);
		}
	}
	struct wnd *w = wnd_get (hwnd);
	if (w)
	{
		result = w->dlg_result;
		struct w16call d = { .a = { hwnd }, .n = 1 };
		extern void api_DestroyWindow16 (struct w16call *);
		api_DestroyWindow16 (&d);
	}
	ow = wnd_get (owner);
	if (ow)
	{
		ow->disabled = was;
		user_set_focus (owner);
	}
	return result;
}

static const uint8_t *dialog_template (uint16_t hinst, uint32_t name)
{
	uint32_t size;
	const uint8_t *t = kernel_resource (hinst, 5, name, &size);	/* RT_DIALOG */
	if (!t)
		fprintf (stderr, "win16 : dialogue %s introuvable\n", w16_str (name));
	return t;
}

/* DialogBoxParam (hinst, gabarit, propriétaire, procédure, paramètre) */
void api_DialogBoxParam16 (struct w16call *c)
{
	const uint8_t *t = dialog_template ((uint16_t) c->a[0], c->a[1]);
	c->ret = 0xffff;
	if (!t)
		return;
	uint16_t h = create_dialog ((uint16_t) c->a[0], t, (uint16_t) c->a[2], c->a[3], c->a[4], 1);
	if (h)
		c->ret = (uint32_t) (uint16_t) dialog_loop (h, (uint16_t) c->a[2]);
}

void api_DialogBox16 (struct w16call *c)
{
	struct w16call d = { .a = { c->a[0], c->a[1], c->a[2], c->a[3], 0 }, .n = 5 };
	api_DialogBoxParam16 (&d);
	c->ret = d.ret;
}

void api_DialogBoxIndirectParam16 (struct w16call *c)
{
	uint16_t s = (uint16_t) (c->a[1] | 7);
	c->ret = 0xffff;
	if (!sel_valid (s))
		return;
	uint16_t h = create_dialog ((uint16_t) c->a[0], lin_ptr (sel_base (s)), (uint16_t) c->a[2], c->a[3], c->a[4], 1);
	if (h)
		c->ret = (uint32_t) (uint16_t) dialog_loop (h, (uint16_t) c->a[2]);
}

void api_DialogBoxIndirect16 (struct w16call *c)
{
	struct w16call d = { .a = { c->a[0], c->a[1], c->a[2], c->a[3], 0 }, .n = 5 };
	api_DialogBoxIndirectParam16 (&d);
	c->ret = d.ret;
}

void api_CreateDialogParam16 (struct w16call *c)
{
	const uint8_t *t = dialog_template ((uint16_t) c->a[0], c->a[1]);
	c->ret = t ? create_dialog ((uint16_t) c->a[0], t, (uint16_t) c->a[2], c->a[3], c->a[4], 0) : 0;
}

void api_CreateDialog16 (struct w16call *c)
{
	struct w16call d = { .a = { c->a[0], c->a[1], c->a[2], c->a[3], 0 }, .n = 5 };
	api_CreateDialogParam16 (&d);
	c->ret = d.ret;
}

void api_CreateDialogIndirectParam16 (struct w16call *c)
{
	c->ret = create_dialog ((uint16_t) c->a[0], far_ptr (c->a[1]), (uint16_t) c->a[2], c->a[3], c->a[4], 0);
}

void api_CreateDialogIndirect16 (struct w16call *c)
{
	c->ret = create_dialog ((uint16_t) c->a[0], far_ptr (c->a[1]), (uint16_t) c->a[2], c->a[3], 0, 0);
}

void api_EndDialog16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (w)
	{
		w->dlg_end = 1;
		w->dlg_result = (int16_t) c->a[1];
		/* (un dialogue non modal se détruit lui-même ; un modal sort de sa boucle) */
	}
	c->ret = w != 0;
}

void api_DefDlgProc16 (struct w16call *c)
{
	/* (appelée par le programme : la partie « par défaut » seulement, sans rappeler sa procédure) */
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	uint32_t keep = w ? dlg_get32 (w, DWL_DLGPROC) : 0;
	if (w)
		dlg_set32 (w, DWL_DLGPROC, 0);
	c->ret = dlg_def_proc ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
	w = wnd_get ((uint16_t) c->a[0]);
	if (w)
		dlg_set32 (w, DWL_DLGPROC, keep);
}

/* ---- IsDialogMessage : Tab, Entrée, Échap ---- */
int dlg_is_message (uint16_t dlg, struct qmsg *m)
{
	struct wnd *t = wnd_get (m->hwnd);
	if (!t || (m->hwnd != dlg && !(t->parent == dlg && (t->style & WS_CHILD))))
		return 0;
	if (m->msg == WM_KEYDOWN)
	{
		uint32_t code = user_send (m->hwnd, WM_GETDLGCODE, 0, 0);
		switch (m->wp)
		{
		case 0x09:	/* Tab */
			if (code & 2)	/* DLGC_WANTTAB */
				break;
		{
			uint16_t n = next_tab (dlg, m->hwnd, user_key_down (0x10) != 0);
			if (n)
				user_set_focus (n);
			return 1;
		}
		case 0x0d:	/* Entrée */
		{
			if (code & 4)	/* DLGC_WANTALLKEYS */
				break;
			uint32_t def = user_send (dlg, DM_GETDEFID, 0, 0);
			uint16_t id = (def >> 16) == 0x534b ? (uint16_t) def : IDOK;
			struct wnd *f = wnd_get (m->hwnd);
			if (f && !strcasecmp (f->clsname, "BUTTON") && ((f->style & 0x0f) <= 1))
				id = f->id;
			user_send (dlg, WM_COMMAND, id, MAKESEGPTR (0, 0));
			return 1;
		}
		case 0x1b:	/* Échap */
			user_send (dlg, WM_COMMAND, IDCANCEL, 0);
			return 1;
		}
	}
	return 0;
}

void api_IsDialogMessage16 (struct w16call *c)
{
	uint32_t a = far_lin (c->a[1]);
	struct qmsg m = { rd16 (a), rd16 (a + 2), rd16 (a + 4), rd32 (a + 6), rd32 (a + 10), 0, 0 };
	c->ret = 0;
	if (!is_dialog ((uint16_t) c->a[0]))
		return;
	struct wnd *t = wnd_get (m.hwnd);
	if (!t || (m.hwnd != (uint16_t) c->a[0] && !(t->parent == (uint16_t) c->a[0])))
		return;
	if (!dlg_is_message ((uint16_t) c->a[0], &m))
	{
		if (m.msg == WM_KEYDOWN)
		{
			extern void api_TranslateMessage16 (struct w16call *);
			struct w16call tm = { .a = { c->a[1] }, .n = 1 };
			api_TranslateMessage16 (&tm);
		}
		user_dispatch (m.hwnd, m.msg, m.wp, m.lp, m.time);
	}
	c->ret = 1;
}

/* ---- les contrôles par leur numéro ---- */
uint16_t dlg_item (uint16_t dlg, uint16_t id)
{
	for (uint16_t h = 0x1000; h < 0x1000 + 512 * 4; h += 4)
	{
		struct wnd *k = wnd_get (h);
		if (k && (k->style & WS_CHILD) && k->parent == dlg && k->id == id)
			return h;
	}
	return 0;
}

void api_GetDlgItem16 (struct w16call *c) { c->ret = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]); }
void api_GetDlgCtrlID16 (struct w16call *c) { struct wnd *w = wnd_get ((uint16_t) c->a[0]); c->ret = w ? w->id : 0; }

void api_SendDlgItemMessage16 (struct w16call *c)
{
	uint16_t h = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]);
	c->ret = h ? user_send (h, (uint16_t) c->a[2], (uint16_t) c->a[3], c->a[4]) : 0;
}

void api_SetDlgItemText16 (struct w16call *c)
{
	uint16_t h = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]);
	if (h)
		user_send (h, WM_SETTEXT, 0, c->a[2]);
}

void api_GetDlgItemText16 (struct w16call *c)
{
	uint16_t h = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]);
	c->ret = h ? user_send (h, WM_GETTEXT, (uint16_t) c->a[3], c->a[2]) : 0;
	if (!h && c->a[3])
		((char *) far_ptr (c->a[2]))[0] = 0;
}

void api_SetDlgItemInt16 (struct w16call *c)
{
	char buf[16];
	if (c->a[3])
		snprintf (buf, sizeof buf, "%d", (int16_t) c->a[2]);
	else
		snprintf (buf, sizeof buf, "%u", (unsigned) (uint16_t) c->a[2]);
	uint16_t h = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]);
	if (!h)
		return;
	uint16_t sel = global_alloc (16, 0);
	memcpy (lin_ptr (sel_base (sel)), buf, strlen (buf) + 1);
	user_send (h, WM_SETTEXT, 0, MAKESEGPTR (sel, 0));
	global_free (sel);
}

void api_GetDlgItemInt16 (struct w16call *c)
{
	uint16_t h = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]);
	c->ret = 0;
	if (c->a[2])
		wr16 (far_lin (c->a[2]), 0);
	if (!h)
		return;
	uint16_t sel = global_alloc (64, 0);
	user_send (h, WM_GETTEXT, 64, MAKESEGPTR (sel, 0));
	const char *s = (const char *) lin_ptr (sel_base (sel));
	char *end;
	long v = strtol (s, &end, 10);
	int ok = end != s && (c->a[3] || v >= 0) && v >= -32768 && v <= 65535;
	global_free (sel);
	if (c->a[2])
		wr16 (far_lin (c->a[2]), (uint16_t) ok);
	c->ret = ok ? (uint16_t) v : 0;
}

void api_CheckDlgButton16 (struct w16call *c)
{
	uint16_t h = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]);
	if (h)
		user_send (h, WM_USER + 1, (uint16_t) c->a[2], 0);	/* BM_SETCHECK */
	c->ret = 1;
}

void api_IsDlgButtonChecked16 (struct w16call *c)
{
	uint16_t h = dlg_item ((uint16_t) c->a[0], (uint16_t) c->a[1]);
	c->ret = h ? user_send (h, WM_USER + 0, 0, 0) : 0;	/* BM_GETCHECK */
}

void api_CheckRadioButton16 (struct w16call *c)
{
	for (uint16_t id = (uint16_t) c->a[1]; id <= (uint16_t) c->a[2]; id++)
	{
		uint16_t h = dlg_item ((uint16_t) c->a[0], id);
		if (h)
			user_send (h, WM_USER + 1, id == (uint16_t) c->a[3], 0);
		if (id == 0xffff)
			break;
	}
	c->ret = 1;
}

void api_GetNextDlgTabItem16 (struct w16call *c) { c->ret = next_tab ((uint16_t) c->a[0], (uint16_t) c->a[1], (int) c->a[2]); }

void api_GetNextDlgGroupItem16 (struct w16call *c)
{
	/* (le suivant du groupe : le suivant tout court, faute de mieux) */
	struct wnd *w = wnd_get ((uint16_t) c->a[1]);
	c->ret = c->a[1];
	if (!w)
		return;
	for (uint16_t h = (uint16_t) (c->a[1] + 4); h < 0x1000 + 512 * 4; h += 4)
	{
		struct wnd *k = wnd_get (h);
		if (k && (k->style & WS_CHILD) && k->parent == (uint16_t) c->a[0])
		{
			if (k->style & WS_GROUP)
				break;
			c->ret = h;
			return;
		}
	}
}

void api_MapDialogRect16 (struct w16call *c)
{
	uint32_t r = far_lin (c->a[1]);
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	int bx = 6, by = 13;
	if (w && w->font)
	{
		uint16_t hdc = gdi_dc_for_window (0);
		bx = gdi_text_width (hdc, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", -1, w->font) / 52;
		by = gdi_text_height (hdc, w->font);
		gdi_release_dc (hdc);
	}
	for (int i = 0; i < 4; i++)
	{
		int v = (int16_t) rd16 (r + 2 * (uint32_t) i);
		wr16 (r + 2 * (uint32_t) i, (uint16_t) ((i & 1) ? v * by / 8 : v * bx / 4));
	}
}

void api_GetDialogBaseUnits16 (struct w16call *c) { (void) c; c->ret = 8 | 16 << 16; }

void dialog_register (void)
{
	user_register_system_class ("#32770", dlg_def_proc, 0x0008, 16, 30);
}
