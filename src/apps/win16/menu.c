/* menu.c -- win16 : menus et accélérateurs.
 *
 * Les menus d'une fenêtre Win16 vont dans la barre de menus globale de Korin (ui_menu), comme ceux de toute app
 * de Korin ; un choix revient en WM_COMMAND. Les états (grisé, coché) suivent par ui_menu_state. Les sous-menus
 * imbriqués sont mis à plat (« Parent › Entrée »). TrackPopupMenu ouvre un menu surgissant de UIKit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w16.h"
#include "be.h"
#include "user.h"

#define MF_GRAYED 0x0001
#define MF_DISABLED 0x0002
#define MF_BITMAP 0x0004
#define MF_CHECKED 0x0008
#define MF_POPUP 0x0010
#define MF_END 0x0080
#define MF_OWNERDRAW 0x0100
#define MF_BYPOSITION 0x0400
#define MF_SEPARATOR 0x0800

#define MAX_MENUS 512
#define MAX_ITEMS 64
struct item { uint16_t flags, id; char text[96]; int pub; };
struct menu { int used, n; struct item it[MAX_ITEMS]; };
static struct menu menus[MAX_MENUS];
static int menus_dirty;
#define HMENU_OF(i) ((uint16_t) (0x6000 + (i) * 4))

static struct menu *menu_get (uint16_t h)
{
	int i = (h - 0x6000) / 4;
	if (h < 0x6000 || (h & 3) || i >= MAX_MENUS || !menus[i].used)
		return 0;
	return &menus[i];
}

static uint16_t menu_new (void)
{
	for (int i = 0; i < MAX_MENUS; i++)
		if (!menus[i].used)
		{
			memset (&menus[i], 0, sizeof menus[i]);
			menus[i].used = 1;
			return HMENU_OF (i);
		}
	return 0;
}

static void menu_free (uint16_t h)
{
	struct menu *m = menu_get (h);
	if (!m)
		return;
	for (int i = 0; i < m->n; i++)
		if (m->it[i].flags & MF_POPUP)
			menu_free (m->it[i].id);
	m->used = 0;
}

/* un gabarit MENU de Windows 3.x : en-tête (version, décalage), puis les entrées */
static const uint8_t *parse_items (uint16_t hmenu, const uint8_t *p, const uint8_t *end)
{
	struct menu *m = menu_get (hmenu);
	while (p + 2 <= end)
	{
		uint16_t flags = (uint16_t) (p[0] | p[1] << 8);
		p += 2;
		uint16_t id = 0;
		if (!(flags & MF_POPUP))
		{
			id = (uint16_t) (p[0] | p[1] << 8);
			p += 2;
		}
		const char *text = (const char *) p;
		p += strlen (text) + 1;
		if (m && m->n < MAX_ITEMS)
		{
			struct item *it = &m->it[m->n++];
			it->flags = flags & ~MF_END;
			snprintf (it->text, sizeof it->text, "%s", text);
			if (!text[0] && !(flags & MF_POPUP) && !id)
				it->flags |= MF_SEPARATOR;
			if (flags & MF_POPUP)
			{
				uint16_t sub = menu_new ();
				it->id = sub;
				p = parse_items (sub, p, end);
			}
			else
				it->id = id;
		}
		if (flags & MF_END)
			break;
	}
	return p;
}

uint16_t menu_load (uint16_t hinst, uint32_t name)
{
	uint32_t size;
	const uint8_t *r = kernel_resource (hinst, 4, name, &size);	/* RT_MENU */
	if (!r || size < 4)
	{
		w16_log ("   menu %s introuvable\n", w16_str (name));
		return 0;
	}
	uint16_t h = menu_new ();
	int off = r[2] | r[3] << 8;
	parse_items (h, r + 4 + off, r + size);
	return h;
}

void api_LoadMenu16 (struct w16call *c) { c->ret = menu_load ((uint16_t) c->a[0], c->a[1]); }

void api_LoadMenuIndirect16 (struct w16call *c)
{
	const uint8_t *r = far_ptr (c->a[0]);
	uint16_t h = menu_new ();
	int off = r[2] | r[3] << 8;
	parse_items (h, r + 4 + off, r + 65536);
	c->ret = h;
}

void api_CreateMenu16 (struct w16call *c) { (void) c; c->ret = menu_new (); }
void api_CreatePopupMenu16 (struct w16call *c) { (void) c; c->ret = menu_new (); }
void api_DestroyMenu16 (struct w16call *c) { menu_free ((uint16_t) c->a[0]); c->ret = 1; menus_dirty = 1; }
void api_IsMenu16 (struct w16call *c) { c->ret = menu_get ((uint16_t) c->a[0]) != 0; }

/* une entrée par commande (dans les sous-menus aussi) ou par position */
static struct item *find_item (uint16_t hmenu, uint16_t which, uint16_t flags, struct menu **owner, int *pos)
{
	struct menu *m = menu_get (hmenu);
	if (!m)
		return 0;
	if (flags & MF_BYPOSITION)
	{
		if (which >= m->n)
			return 0;
		if (owner) *owner = m;
		if (pos) *pos = which;
		return &m->it[which];
	}
	for (int i = 0; i < m->n; i++)
	{
		if (!(m->it[i].flags & MF_POPUP) && m->it[i].id == which)
		{
			if (owner) *owner = m;
			if (pos) *pos = i;
			return &m->it[i];
		}
		if (m->it[i].flags & MF_POPUP)
		{
			struct item *r = find_item (m->it[i].id, which, flags, owner, pos);
			if (r)
				return r;
		}
	}
	return 0;
}

static void set_item (struct item *it, uint16_t flags, uint16_t id, uint32_t newitem)
{
	it->flags = flags & ~(MF_BYPOSITION | MF_END);
	it->id = id;
	it->text[0] = 0;
	if (!(flags & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW)) && newitem && SELECTOROF (newitem))
		snprintf (it->text, sizeof it->text, "%s", w16_raw (newitem));
	menus_dirty = 1;
}

void api_AppendMenu16 (struct w16call *c)
{
	struct menu *m = menu_get ((uint16_t) c->a[0]);
	if (!m || m->n >= MAX_ITEMS)
	{
		c->ret = 0;
		return;
	}
	set_item (&m->it[m->n++], (uint16_t) c->a[1], (uint16_t) c->a[2], c->a[3]);
	c->ret = 1;
}

void api_InsertMenu16 (struct w16call *c)
{
	uint16_t hm = (uint16_t) c->a[0], pos = (uint16_t) c->a[1], flags = (uint16_t) c->a[2];
	struct menu *m = menu_get (hm);
	int at;
	struct menu *owner = m;
	if (!m)
	{
		c->ret = 0;
		return;
	}
	if (pos == 0xffff)
		at = m->n;
	else if (!find_item (hm, pos, flags & MF_BYPOSITION, &owner, &at))
	{
		owner = m;
		at = m->n;
	}
	if (owner->n >= MAX_ITEMS)
	{
		c->ret = 0;
		return;
	}
	memmove (&owner->it[at + 1], &owner->it[at], sizeof owner->it[0] * (size_t) (owner->n - at));
	owner->n++;
	set_item (&owner->it[at], flags, (uint16_t) c->a[3], c->a[4]);
	c->ret = 1;
}

void api_ModifyMenu16 (struct w16call *c)
{
	struct item *it = find_item ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], 0, 0);
	if (it)
		set_item (it, (uint16_t) c->a[2], (uint16_t) c->a[3], c->a[4]);
	c->ret = it != 0;
}

static void remove_item (struct w16call *c, int destroy)
{
	struct menu *owner;
	int pos;
	struct item *it = find_item ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], &owner, &pos);
	c->ret = 0;
	if (!it)
		return;
	if (destroy && (it->flags & MF_POPUP))
		menu_free (it->id);
	memmove (&owner->it[pos], &owner->it[pos + 1], sizeof owner->it[0] * (size_t) (owner->n - pos - 1));
	owner->n--;
	menus_dirty = 1;
	c->ret = 1;
}

void api_RemoveMenu16 (struct w16call *c) { remove_item (c, 0); }
void api_DeleteMenu16 (struct w16call *c) { remove_item (c, 1); }

void api_EnableMenuItem16 (struct w16call *c)
{
	struct item *it = find_item ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], 0, 0);
	if (!it)
	{
		c->ret = 0xffff;
		return;
	}
	c->ret = it->flags & (MF_GRAYED | MF_DISABLED);
	it->flags = (uint16_t) ((it->flags & ~(MF_GRAYED | MF_DISABLED)) | (c->a[2] & (MF_GRAYED | MF_DISABLED)));
	menus_dirty = 1;
}

void api_CheckMenuItem16 (struct w16call *c)
{
	struct item *it = find_item ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], 0, 0);
	if (!it)
	{
		c->ret = 0xffff;
		return;
	}
	c->ret = it->flags & MF_CHECKED;
	it->flags = (uint16_t) ((it->flags & ~MF_CHECKED) | (c->a[2] & MF_CHECKED));
	menus_dirty = 1;
}

void api_GetMenuState16 (struct w16call *c)
{
	struct item *it = find_item ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[2], 0, 0);
	if (!it)
	{
		c->ret = 0xffff;
		return;
	}
	struct menu *sub = (it->flags & MF_POPUP) ? menu_get (it->id) : 0;
	c->ret = sub ? (uint32_t) ((sub->n << 8) | (it->flags & 0xff)) : it->flags;
}

void api_GetMenuString16 (struct w16call *c)
{
	struct item *it = find_item ((uint16_t) c->a[0], (uint16_t) c->a[1], (uint16_t) c->a[4], 0, 0);
	int n = (int16_t) c->a[3];
	c->ret = 0;
	if (!it || n <= 0 || !c->a[2])
		return;
	int l = (int) strlen (it->text);
	if (l > n - 1)
		l = n - 1;
	memcpy (far_ptr (c->a[2]), it->text, (size_t) l);
	((char *) far_ptr (c->a[2]))[l] = 0;
	c->ret = (uint32_t) l;
}

void api_GetMenuItemCount16 (struct w16call *c) { struct menu *m = menu_get ((uint16_t) c->a[0]); c->ret = m ? (uint32_t) m->n : 0xffff; }

void api_GetMenuItemID16 (struct w16call *c)
{
	struct menu *m = menu_get ((uint16_t) c->a[0]);
	int pos = (int) c->a[1];
	c->ret = 0xffff;
	if (m && pos < m->n && !(m->it[pos].flags & MF_POPUP))
		c->ret = m->it[pos].id;
}

void api_GetSubMenu16 (struct w16call *c)
{
	struct menu *m = menu_get ((uint16_t) c->a[0]);
	int pos = (int16_t) c->a[1];
	c->ret = m && pos >= 0 && pos < m->n && (m->it[pos].flags & MF_POPUP) ? m->it[pos].id : 0;
}

void api_GetMenu16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	c->ret = w && !(w->style & WS_CHILD) ? w->menu : 0;
}

void menu_attach (uint16_t hwnd);

void api_SetMenu16 (struct w16call *c)
{
	struct wnd *w = wnd_get ((uint16_t) c->a[0]);
	if (!w || (w->style & WS_CHILD))
	{
		c->ret = 0;
		return;
	}
	w->menu = (uint16_t) c->a[1];
	if (w->be)
		menu_attach (w->hwnd);
	c->ret = 1;
}

void api_DrawMenuBar16 (struct w16call *c) { (void) c; menus_dirty = 1; }
void api_HiliteMenuItem16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_SetMenuItemBitmaps16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_GetMenuCheckMarkDimensions (struct w16call *c) { (void) c; c->ret = 0x000d000d; }

/* le menu système : un menu à part, que personne ne montre */
static uint16_t sys_menu;
void api_GetSystemMenu16 (struct w16call *c)
{
	(void) c;
	if (!sys_menu)
		sys_menu = menu_new ();
	c->ret = sys_menu;
}

/* ---- la barre de Korin ---- */
static void label_utf8 (const char *src, char *out, int cap, char *shortcut, int scap)
{
	char raw[128];
	int k = 0;
	shortcut[0] = 0;
	for (const char *p = src; *p && k < (int) sizeof raw - 1; p++)
	{
		if (*p == '\t')
		{
			w16_to_utf8 ((const uint8_t *) p + 1, -1, shortcut, scap);
			break;
		}
		if (*p == '&')
		{
			if (p[1] == '&')
				raw[k++] = *p++;
			continue;
		}
		raw[k++] = *p;
	}
	raw[k] = 0;
	w16_to_utf8 ((const uint8_t *) raw, -1, out, cap);
}

static int emit_items (uint16_t hmenu, const char *prefix, char *buf, int o, int cap, int depth)
{
	struct menu *m = menu_get (hmenu);
	if (!m || depth > 4)
		return o;
	for (int i = 0; i < m->n && o < cap - 200; i++)
	{
		struct item *it = &m->it[i];
		char lab[200], sc[64];
		label_utf8 (it->text, lab, sizeof lab, sc, sizeof sc);
		if (it->flags & MF_SEPARATOR)
			o += snprintf (buf + o, (size_t) (cap - o), "-\n");
		else if (it->flags & MF_POPUP)
		{
			char p2[200];
			snprintf (p2, sizeof p2, "%s%s › ", prefix, lab);
			o = emit_items (it->id, p2, buf, o, cap, depth + 1);
		}
		else
			o += snprintf (buf + o, (size_t) (cap - o), "I%u\t%s%s\t%s\n", it->id, prefix, lab, sc);
	}
	return o;
}

static void send_states (long be, uint16_t hmenu, int depth)
{
	struct menu *m = menu_get (hmenu);
	if (!m || depth > 4)
		return;
	for (int i = 0; i < m->n; i++)
	{
		struct item *it = &m->it[i];
		if (it->flags & MF_POPUP)
		{
			send_states (be, it->id, depth + 1);
			continue;
		}
		if (it->flags & MF_SEPARATOR)
			continue;
		int st = ((it->flags & (MF_GRAYED | MF_DISABLED)) ? 1 : 0) | ((it->flags & MF_CHECKED) ? 2 : 0);
		if (st || it->pub)
			be_menu_state (be, it->id, st);
		it->pub = st;
	}
}

static void publish (struct wnd *w)
{
	struct menu *m = menu_get (w->menu);
	if (!w->be)
		return;
	if (!m)
	{
		be_menu_set (w->be, 0);
		return;
	}
	char *buf = malloc (8192);
	int o = 0;
	for (int i = 0; i < m->n && o < 8000; i++)
	{
		struct item *it = &m->it[i];
		char lab[200], sc[64];
		label_utf8 (it->text, lab, sizeof lab, sc, sizeof sc);
		if (it->flags & MF_POPUP)
		{
			o += snprintf (buf + o, (size_t) (8192 - o), "M%s\n", lab);
			o = emit_items (it->id, "", buf, o, 8192, 1);
		}
		else if (!(it->flags & MF_SEPARATOR))
			o += snprintf (buf + o, (size_t) (8192 - o), "M%s\nI%u\t%s\t%s\n", lab, it->id, lab, sc);
	}
	if (o > 2040)	/* (WS_MENU_MAX : 2048 octets) */
	{
		o = 2040;
		while (o > 0 && buf[o - 1] != '\n')
			o--;
	}
	buf[o] = 0;
	be_menu_set (w->be, buf);
	send_states (w->be, w->menu, 0);
	free (buf);
}

static int in_init;

/* WM_INITMENU et WM_INITMENUPOPUP : le programme met à jour ses états avant qu'on les montre */
static void init_popups_of (struct wnd *w)
{
	struct menu *m = menu_get (w->menu);
	if (!m)
		return;
	uint16_t h = w->hwnd;
	user_send (h, WM_INITMENU, w->menu, 0);
	for (int i = 0; i < m->n && wnd_get (h); i++)
		if (m->it[i].flags & MF_POPUP)
			user_send (h, WM_INITMENUPOPUP, m->it[i].id, (uint32_t) i);
}

void menu_init_popups (uint16_t hwnd)
{
	if (in_init)
		return;
	in_init = 1;
	for (uint16_t h = 0x1000; h < 0x1000 + 512 * 4; h += 4)
	{
		struct wnd *w = wnd_get (h);
		if (w && w->be && w->menu && (!hwnd || h == hwnd))
			init_popups_of (w);
	}
	in_init = 0;
}

void menu_attach (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w || !w->be)
		return;
	menu_init_popups (hwnd);
	w = wnd_get (hwnd);
	if (w)
		publish (w);
}

void menu_detach (uint16_t hwnd)
{
	struct wnd *w = wnd_get (hwnd);
	if (w && w->be)
		be_menu_set (w->be, 0);
}

void menu_publish_pending (void)
{
	if (!menus_dirty)
		return;
	menus_dirty = 0;
	for (uint16_t h = 0x1000; h < 0x1000 + 512 * 4; h += 4)
	{
		struct wnd *w = wnd_get (h);
		if (w && w->be && !(w->style & WS_CHILD))
			publish (w);
	}
}

int menu_command (uint16_t hwnd, int id)
{
	struct wnd *w = wnd_get (hwnd);
	if (!w)
		return 0;
	struct item *it = find_item (w->menu, (uint16_t) id, 0, 0, 0);
	if (it && (it->flags & (MF_GRAYED | MF_DISABLED)))
		return 0;
	user_post (hwnd, WM_COMMAND, (uint16_t) id, 0);
	return 1;
}

/* TrackPopupMenu (hmenu, flags, x, y écran, réservé, hwnd, rect) */
void api_TrackPopupMenu16 (struct w16call *c)
{
	uint16_t hmenu = (uint16_t) c->a[0], hwnd = (uint16_t) c->a[5];
	struct menu *m = menu_get (hmenu);
	c->ret = 0;
	if (!m)
		return;
	user_send (hwnd, WM_INITMENUPOPUP, hmenu, 0);
	char labels[4096];
	uint16_t ids[64];
	int o = 0, n = 0;
	for (int i = 0; i < m->n && o < 4000; i++)
	{
		struct item *it = &m->it[i];
		char lab[200], sc[64];
		label_utf8 (it->text, lab, sizeof lab, sc, sizeof sc);
		if (it->flags & MF_SEPARATOR)
			o += snprintf (labels + o, sizeof labels - (size_t) o, "-") + 1;
		else
		{
			char pre[3] = { 0 };
			int k = 0;
			if (it->flags & (MF_GRAYED | MF_DISABLED)) pre[k++] = 1;
			if (it->flags & MF_CHECKED) pre[k++] = 2;
			o += snprintf (labels + o, sizeof labels - (size_t) o, "%s%s%s", pre, lab, (it->flags & MF_POPUP) ? " ›" : "") + 1;
		}
		ids[n++] = (it->flags & (MF_POPUP | MF_SEPARATOR | MF_GRAYED | MF_DISABLED)) ? 0xffff : it->id;
	}
	labels[o] = 0;
	/* (x, y : écran -> zone cliente de hwnd) */
	uint16_t sel = global_alloc (8, 0);
	wr16 (sel_base (sel), (uint16_t) c->a[2]);
	wr16 (sel_base (sel) + 2, (uint16_t) c->a[3]);
	struct w16call s2c = { .a = { hwnd, MAKESEGPTR (sel, 0) }, .n = 2 };
	extern void api_ScreenToClient16 (struct w16call *);
	api_ScreenToClient16 (&s2c);
	int x = (int16_t) rd16 (sel_base (sel)), y = (int16_t) rd16 (sel_base (sel) + 2);
	global_free (sel);
	int r = user_popup (hwnd, x, y, labels, 180);
	if (r >= 0 && r < n && ids[r] != 0xffff)
	{
		user_post (hwnd, WM_COMMAND, ids[r], 0);
		c->ret = 1;
	}
}

/* ---- accélérateurs ---- */
#define MAX_ACCEL 32
static struct { int used, n; uint8_t f[128]; uint16_t key[128], cmd[128]; } accels[MAX_ACCEL];

void api_LoadAccelerators16 (struct w16call *c)
{
	uint32_t size;
	const uint8_t *r = kernel_resource ((uint16_t) c->a[0], 9, c->a[1], &size);	/* RT_ACCELERATOR */
	c->ret = 0;
	if (!r)
		return;
	for (int i = 0; i < MAX_ACCEL; i++)
		if (!accels[i].used)
		{
			accels[i].used = 1;
			int n = 0;
			for (uint32_t o = 0; o + 5 <= size && n < 128; o += 5)
			{
				accels[i].f[n] = r[o];
				accels[i].key[n] = (uint16_t) (r[o + 1] | r[o + 2] << 8);
				accels[i].cmd[n] = (uint16_t) (r[o + 3] | r[o + 4] << 8);
				n++;
				if (r[o] & 0x80)
					break;
			}
			accels[i].n = n;
			c->ret = (uint32_t) (0x6f00 + i);
			return;
		}
}

void api_TranslateAccelerator16 (struct w16call *c)
{
	int t = (int) c->a[1] - 0x6f00;
	c->ret = 0;
	if (t < 0 || t >= MAX_ACCEL || !accels[t].used || !c->a[2])
		return;
	uint32_t a = far_lin (c->a[2]);
	uint16_t msg = rd16 (a + 2), key = rd16 (a + 4);
	if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN && msg != WM_CHAR && msg != WM_SYSCHAR)
		return;
	int shift = user_key_down (0x10) != 0, ctrl = user_key_down (0x11) != 0, alt = user_key_down (0x12) != 0;
	for (int i = 0; i < accels[t].n; i++)
	{
		uint8_t f = accels[t].f[i];
		int hit = 0;
		if (f & 1)	/* FVIRTKEY */
			hit = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && key == accels[t].key[i]
				&& shift == !!(f & 4) && ctrl == !!(f & 8) && alt == !!(f & 0x10);
		else
			hit = (msg == WM_CHAR || msg == WM_SYSCHAR) && key == accels[t].key[i] && (alt == !!(f & 0x10));
		if (hit)
		{
			uint16_t hwnd = (uint16_t) c->a[0];
			struct wnd *w = wnd_get (hwnd);
			if (w && w->menu)
			{
				struct item *it = find_item (w->menu, accels[t].cmd[i], 0, 0, 0);
				if (it && (it->flags & (MF_GRAYED | MF_DISABLED)))
				{
					c->ret = 1;
					return;
				}
			}
			user_send (hwnd, WM_COMMAND, accels[t].cmd[i], 0x10000);
			c->ret = 1;
			return;
		}
	}
}

/* le menu Fenêtre d'un programme MDI (mdi.c) : les fenêtres filles « &1 Titre »… (identifiants idfirst + i), la
 * fenêtre active cochée, après un séparateur ; la liste d'avant est retirée */
#define MDI_SEP_ID 0xfffe
void menu_mdi_list (uint16_t hmenu, uint16_t idfirst, int n, const char *const *titles, int active)
{
	struct menu *m = menu_get (hmenu);
	if (!m)
		return;
	for (int i = 0; i < m->n;)
		if ((m->it[i].id >= idfirst && m->it[i].id < idfirst + 200 && !(m->it[i].flags & MF_POPUP))
			|| ((m->it[i].flags & MF_SEPARATOR) && m->it[i].id == MDI_SEP_ID))
		{
			memmove (&m->it[i], &m->it[i + 1], sizeof m->it[0] * (size_t) (m->n - i - 1));
			m->n--;
		}
		else
			i++;
	if (n > 0 && m->n < MAX_ITEMS)
	{
		struct item *it = &m->it[m->n++];
		memset (it, 0, sizeof *it);
		it->flags = MF_SEPARATOR;
		it->id = MDI_SEP_ID;
	}
	for (int i = 0; i < n && i < 9 && m->n < MAX_ITEMS; i++)
	{
		struct item *it = &m->it[m->n++];
		memset (it, 0, sizeof *it);
		it->flags = (uint16_t) (i == active ? 0x0008 : 0);	/* MF_CHECKED */
		it->id = (uint16_t) (idfirst + i);
		snprintf (it->text, sizeof it->text, "&%d %s", i + 1, titles[i]);
	}
	menus_dirty = 1;
}
