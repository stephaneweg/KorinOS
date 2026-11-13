/* menu.c : les menus des applications, pour la barre de menus globale (menu.inc ; comme Onyx :
 * EL_OP_MENU_SET, GetActiveMenu, SendMenuCommand).
 *
 * Un menu par application, comme sur Mac : celui de la première fenêtre du programme qui en a un
 * (menu_owner), quelle que soit la fenêtre active, et le titre de sa première fenêtre (win_app).
 * Une fenêtre garde son texte de menus (WS_MENU_MAX octets au plus ; menu_specs : un emplacement
 * par place de fenêtre). Le programme l'envoie en morceaux (WS_OP_MENU_SET : b décalage, c
 * longueur totale, +40 longueur du morceau) ; le dernier morceau le publie. La barre de menus le
 * lit (WS_OP_MENU_GET, même découpage) et renvoie les choix (WS_OP_MENU_COMMAND : WEV_MENU à la
 * fenêtre active, WEV_CLOSE à la première pour Quitter ; rien pendant un dialogue modal).
 *
 * Numéro de série : chaque envoi donne au menu une génération unique (mgen) ; MENU_GET avance le
 * numéro quand la génération du menu de l'application active n'est plus celle vue la dernière
 * fois (autre application, menu remplacé, titre changé). */
#include "elegant.h"

/* fenêtre -> son emplacement de menu (WS_MENU_MAX octets) */
u8 *menu_spec_of(struct win *w)
{
	return menu_specs[w - windows];
}

/* -> fenêtre active, si c'est une fenêtre normale visible (0 : aucune) */
struct win *menu_active(void)
{
	struct win *w = win_focus;
	if (!w)
		return 0;
	if (w->mini || (w->flags & (WF_BACK | WF_TOP | WF_NOFOCUS)))
		return 0;
	return w;
}

/* fenêtre : nouvelle génération de son menu */
void menu_new_gen(struct win *w)
{
	w->mgen = (u64)++menu_gen;
}

/* fenêtre dont le titre a changé : la barre relira son menu */
void menu_title_changed(struct win *w)
{
	if (w->mlen)
		menu_new_gen(w);
}

/* WS_OP_MENU_SET : a id, b décalage, c longueur totale, +40 longueur du morceau, données : le
 * morceau -> 0 ; le dernier morceau publie le menu (c = 0 : plus de menu) */
long req_menu_set(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	u64 off = M64(msg, WM_B);   /* décalage */
	u64 total = M64(msg, WM_C); /* longueur totale */
	u64 piece = M32(msg, WM_LEN); /* morceau */
	if (total > WS_MENU_MAX || piece > WS_MENU_PIECE || off > total || piece > total - off)
		return E_INVAL;
	if (!off)
		w->mlen = 0; /* premier morceau : menu incomplet */
	u8 *d = menu_spec_of(w) + off;
	const u8 *s = msg + WM_DATA;
	for (u64 i = 0; i < piece; i++)
		d[i] = s[i];
	if (off + piece == total) { /* (sinon d'autres morceaux suivent) */
		w->mlen = total;
		menu_new_gen(w);
	}
	return 0;
}

/* fenêtre -> la première ouverte de son programme, listée : son titre est le nom de
 * l'application dans la barre, quelle que soit la fenêtre active */
struct win *win_app(struct win *w)
{
	struct win *best = w;
	u64 id = w->id;
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *o = &windows[i];
		if (!o->used || o->pid != w->pid || (o->flags & WF_NOLIST) || o->id >= id)
			continue;
		id = o->id;
		best = o;
	}
	return best;
}

/* fenêtre -> la première de son programme qui a un menu (elle-même si aucune) */
struct win *menu_owner(struct win *w)
{
	struct win *best = w;
	u64 id = ~0ULL;
	for (long i = 0; i < MAX_WINDOWS; i++) {
		struct win *o = &windows[i];
		if (!o->used || o->pid != w->pid || !o->mlen || o->id >= id)
			continue;
		id = o->id;
		best = o;
	}
	return best;
}

/* WS_OP_MENU_GET : a décalage -> numéro de série (0 : pas de fenêtre active ou pas de menu) ; b
 * id de la fenêtre, c longueur totale, d longueur du morceau ; données : titre (WS_MENU_TITLE
 * octets, NUL), puis le morceau (WS_MENU_GPIECE octets au plus, à partir du décalage) */
long req_menu_get(void)
{
	u64 off = M64(msg, WM_A); /* décalage */
	struct win *a = menu_active();
	struct win *o = a ? menu_owner(a) : 0;
	if (!o || !o->mlen) {
		menu_last_gen = 0;
		M64(msg, WM_B) = 0;
		M64(msg, WM_C) = 0;
		M64(msg, WM_D) = 0;
		msg[WM_DATA] = 0;
		return 0;
	}
	if ((long)o->mgen != menu_last_gen) { /* autre fenêtre, autre menu : nouveau numéro */
		menu_last_gen = (long)o->mgen;
		menu_serial++;
	}
	const u8 *spec = menu_spec_of(o);
	u64 len = o->mlen;
	if (off > len) /* morceau : [décalage, décalage + 336[ */
		off = len;
	u64 piece = len - off;
	if (piece > WS_MENU_GPIECE)
		piece = WS_MENU_GPIECE;
	M64(msg, WM_B) = o->id; /* id, longueur totale */
	M64(msg, WM_C) = len;
	M64(msg, WM_D) = piece;
	copy_title((char *)msg + WM_DATA, win_app(o)->title, WS_MENU_TITLE); /* titre : celui de
										 l'application */
	u8 *d = msg + WM_DATA + WS_MENU_TITLE; /* morceau */
	for (u64 i = 0; i < piece; i++)
		d[i] = spec[off + i];
	return menu_serial;
}

/* WS_OP_MENU_COMMAND : a numéro de l'entrée (-1 : Quitter), b numéro de série vu par la barre (0 :
 * sans contrôle) -> 0 ; E_AGAIN si le menu a changé entre-temps. La fenêtre active reçoit WEV_MENU
 * (EV_CODE = numéro) ; la première du programme reçoit WEV_CLOSE pour Quitter (comme sa case de
 * fermeture). Pendant un dialogue modal, rien. */
long req_menu_command(void)
{
	struct win *w = menu_active();
	if (!w)
		return E_BADF;
	if (w->flags & WF_MODAL)
		return 0;
	u64 serial = M64(msg, WM_B);
	if (serial) {
		if ((long)serial != menu_serial)
			return E_AGAIN;
		if ((long)menu_owner(w)->mgen != menu_last_gen)
			return E_AGAIN;
	}
	u64 *e = (u64 *)evbuf;
	e[0] = e[1] = e[2] = e[3] = 0;
	u32 type = WEV_MENU;
	u64 code = M64(msg, WM_A);
	if (code == ~0ULL) { /* Quitter : à l'application */
		type = WEV_CLOSE;
		code = 0;
		w = win_app(w);
	}
	M32(evbuf, EV_TYPE) = type;
	M32(evbuf, EV_CODE) = (u32)code;
	win_push(w, evbuf);
	return 0;
}
