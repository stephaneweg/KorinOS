/*
 * applets/dock.cpp -- le volet Dock du panneau de configuration : le dockconf d'Onyx (user/Apps/dockconf), les
 * réglages de SD0:/etc/dock.ini (dockconf.cpp) en widgets de UIKit, même disposition :
 *   * les TIROIRS, de gauche à droite : le dock en a un pour chaque catégorie des applications de la carte -- ici
 *     leur ordre (^ v), ceux masqués, et l'application principale de chacun (l'icône du tiroir) ;
 *   * les BUREAUX (espaces de travail) : combien (1 à 6) et leurs noms ;
 *   * les LANCEURS après les tiroirs : ajouter une application de la liste de toutes, en retirer une, la déplacer ;
 *   * (Korin, dans l'en-tête du volet) la TAILLE du dock et la CORBEILLE.
 * Appliquer écrit dock.ini et relance la barre du bureau ; Rétablir relit ce qui est enregistré. La mise en page
 * est celle d'Onyx (700 x 470 sous le titre), les listes plus petites quand la fenêtre l'est.
 *
 * Le volet de l'ancien dock est MASQUÉ (le panel l'a remplacé : il n'est pas dans la liste de main.cpp), mais
 * gardé. Port en C++ de applets/dock.inc (Korin) : l'asm fait foi (le dockconf d'Onyx dessinait avec l'uikit C++
 * d'Onyx ; ici les widgets ui_* de UIKit, comme l'asm). Ses aides (dk_*) servent aussi aux volets Panel et
 * Imprimantes (dock.h).
 */
#include <string.h>
#include "dock.h"

/* ---- les textes (dock.inc) ---- */
static const char dk_s_drawers[] = "Tiroirs";
static const char dk_t_drawers[] = "Les tiroirs du dock, de gauche \xC3\xA0 droite : un par cat\xC3\xA9gorie d'applications";
const char dk_s_up[] = "^";
const char dk_s_down[] = "v";
const char dk_t_left[] = "Plus \xC3\xA0 gauche";
const char dk_t_right[] = "Plus \xC3\xA0 droite";
static const char dk_s_hidden[] = "Masqu\xC3\xA9";
static const char dk_t_hidden[] = "Pas de tiroir pour cette cat\xC3\xA9gorie (ses applications restent dans le menu)";
static const char dk_s_main[] = "Application principale";
static const char dk_t_main[] = "L'ic\xC3\xB4ne du tiroir : un clic dessus d\xC3\xA9marre cette application";
static const char dk_s_colon[] = " : ";
static const char dk_s_hiddenmark[] = "  -- masqu\xC3\xA9";
static const char dk_s_unknown[] = "?";
static const char dk_s_desks[] = "Bureaux";
static const char dk_s_count[] = "Nombre";
static const char dk_t_name[] = "Son nom (affich\xC3\xA9 par le carr\xC3\xA9 du dock)";
static const char dk_s_launchers[] = "Lanceurs (apr\xC3\xA8s les tiroirs)";
const char dk_s_add[] = "< Ajouter";
const char dk_s_remove[] = "Retirer";
const char dk_t_all[] = "Toutes les applications : choisissez-en une, puis < Ajouter (ou double-cliquez dessus)";
static const char dk_s_apply[] = "Appliquer";
static const char dk_s_restore[] = "R\xC3\xA9tablir";
const char dk_s_choose[] = "Choisissez d'abord une application dans la liste de droite.";
static const char dk_s_full[] = "6 lanceurs au plus.";
static const char dk_s_one[] = "Gardez au moins un tiroir.";
static const char dk_s_write[] = "Impossible d'\xC3\xA9" "crire SD0:/etc/dock.ini.";
static const char dk_s_done[] = "Appliqu\xC3\xA9 : le dock en tient compte.";
const char dk_s_empty[] = "";
static const char dk_s_size[] = "Taille";
static const char dk_s_sizes[] = "Automatique|Grande|Moyenne|Petite";
static const char dk_s_trash[] = "Corbeille";
static const char dk_s_shell[] = "Shell";
static const char *const dk_v_sizes[4] = { "auto", "grande", "moyenne", "petite" };
static const char dk_header_text[] =
	"# Le dock (SD0:/sys/dock), comme le dock.ini d'Onyx ; ecrit par le" CRLF
	"# panneau de configuration (applet Dock)." CRLF
	"# drawer    = categorie, application principale  (dans l'ordre du dock ;" CRLF
	"#             une categorie non nommee vient a la fin)" CRLF
	"# hidden    = categorie                          (sans tiroir)" CRLF
	"# launcher  = application                        (apres les tiroirs)" CRLF
	"# desk      = nom                                (bureaux, 1 a 6)" CRLF
	"# corbeille = oui | non" CRLF
	"# taille    = auto | grande | moyenne | petite" CRLF;

/* control.asm : la barre du bureau relancée (n_dock, p_dockp : le panel, même depuis ce volet) */
static const char n_dock[] = "panel";
static const char p_dockp[] = "SD0:/sys/panel";

/* ---- l'état partagé (dk_data) ---- */
long dk_ntop;
static long dk_tops[DK_TOPS];
long dk_w_launchers, dk_w_all, dk_w_count, dk_w_message, dk_w_size, dk_w_trash;
long dk_w_number[DOCK_MAXDESKS], dk_w_name[DOCK_MAXDESKS];
long dk_nall;
unsigned char dk_all[APP_MAX];
char dk_file[4096];
static long dk_w_drawers, dk_w_main, dk_w_hidden;
static long dk_nvis, dk_ncatapps;		/* lignes de la liste -> dl_* ; les applications du tiroir choisi */
static unsigned char dk_vis[CAT_MAX], dk_catapps[APP_MAX];

/* ---- les aides ---- */

long dk_top (long w)
{
	if (dk_ntop < DK_TOPS)			/* (l'asm ne borne pas : 9 au plus) */
		dk_tops[dk_ntop++] = w;
	return w;
}

long dk_on (long w, dk_fn fn)
{
	ui_w_on (w, (long) fn, 0);
	return w;
}

long dk_tip (long w, const char *tip)
{
	ui_w_tip (w, (long) tip);
	return w;
}

/* on quitte le volet : ses widgets s'en vont (les enfants avec leur groupe) */
void dk_close (void)
{
	for (long i = 0; i < dk_ntop; i++)
		ui_w_destroy (dk_tops[i]);
	dk_ntop = 0;
}

void dk_draw (void) { ui_w_draw (0); }

void dk_widgets (void)
{
	ap.draw = dk_draw;
	ap.close = dk_close;
	ap.widgets = 1;
	dk_ntop = 0;
}

/* (w, parent) -> w, dans son groupe */
static long in (long w, long parent)
{
	ui_w_set_parent (w, parent);
	return w;
}

const char *dk_label (const char *name)
{
	long a = app_find (name);
	return a < 0 ? name : app_label (a);
}

/* les bureaux montrés, leurs noms ; le volet à redessiner (ceux cachés) */
void dk_show_desks (void)
{
	for (long i = 0; i < DOCK_MAXDESKS; i++)
	{
		long on = i < dl_ndesks;
		ui_w_show (dk_w_number[i], on);
		ui_w_show (dk_w_name[i], on);
		if (on)
			ui_w_set_text (dk_w_name[i], (long) dl_desk[i]);
	}
	ui_w_set_value (dk_w_count, dl_ndesks);
	ap.redraw = 1;
}

/* le message, en bas */
void dk_message (const char *text)
{
	ui_w_set_text (dk_w_message, (long) TR (text));
	ap.redraw = 1;
}

/* nom : sans les caractères que dock.ini ne garderait pas (« # » -> « - »), sans bout de caractère UTF-8 en fin
 * (coupé à DL_DESK). (Le commentaire de l'asm dit aussi « sans espaces autour » : son code ne les retire pas.) */
static void dk_bound_name (char *s)
{
	char *p = s, *last = 0;			/* après le dernier caractère entier */
	for (;;)
	{
		unsigned char c = (unsigned char) *p;
		if (!c)
			break;
		if (c == '#')
		{
			c = '-';
			*p = '-';
		}
		p++;
		if ((c & 0xC0) == 0x80)		/* (un octet de suite isolé : ne compte pas) */
			continue;
		if (c < 0x80)
		{
			last = p;
			continue;
		}
		int n = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;	/* octets de ce caractère */
		int k;
		for (k = 1; k < n && *p; k++)
			p++;
		if (k < n)
			break;
		last = p;
	}
	if (last)
		*last = 0;
}

void dk_read_name (long desk, int fill)
{
	char *d = dl_desk[desk];
	dk_copy_n (d, (const char *) ui_w_get_text (dk_w_name[desk]), DL_DESK);
	dk_bound_name (d);
	if (fill && !d[0])
	{
		d[0] = (char) ('1' + desk);
		d[1] = 0;
	}
}

char *dk_put (char *o, const char *text)
{
	return stpcpy (o, text);
}

void dk_on_count (long, long code)
{
	if (code != UN_CHANGE)
		return;
	long n = ui_w_get_value (dk_w_count);
	for (long i = dl_ndesks; i < n; i++)	/* ceux qu'on ajoute : leur numéro */
	{
		dl_desk[i][0] = (char) ('1' + i);
		dl_desk[i][1] = 0;
	}
	for (long i = 0; i < dl_ndesks; i++)	/* les noms tapés restent */
		dk_read_name (i, 0);
	dl_ndesks = n;
	dk_show_desks ();
}

void dk_on_size (long w, long code)
{
	if (code == UN_CHANGE)
		dl_size = ui_w_get_value (w) - 1;
}

void dk_on_trash (long w, long code)
{
	if (code == UN_CHANGE)
		dl_trash = ui_w_get_value (w);
}

/* x, y = coin, h = hauteur, tip = l'infobulle d'un nom : le groupe Bureaux (Nombre, un champ par bureau) */
long dk_desks_group (long x, long y, long h, const char *tip)
{
	long g = dk_top (ui_groupbox (x, y, 200, h, (long) TR (dk_s_desks)));
	in (ui_label (x + 10, y + DK_CT + 4, 70, 20, (long) dk_s_count, 0), g);
	dk_w_count = dk_on (in (ui_numeric (x + 84, y + DK_CT, 106, DK_BTN, 1, DOCK_MAXDESKS, dl_ndesks), g),
			    dk_on_count);
	unsigned long line = (unsigned long) (h - (DK_CT + 40 + 4)) / DOCK_MAXDESKS;	/* une ligne de nom (32 au plus) */
	if (line > 32)
		line = 32;
	long lh = (long) line;
	for (long i = 0; i < DOCK_MAXDESKS; i++)
	{
		char num[2] = { (char) ('1' + i), 0 };	/* son numéro */
		dk_w_number[i] = in (ui_label (x + 10, y + DK_CT + 44 + lh * i, 20, 20, (long) num, 0), g);
		unsigned long fh = (unsigned long) (lh - 2);	/* son nom */
		if (fh > 28)
			fh = 28;
		dk_w_name[i] = dk_tip (in (ui_textbox (x + 30, y + DK_CT + 40 + lh * i, 158, (long) fh, 0), g), tip);
	}
	return g;
}

/* pw, ph = taille du volet : le message, Appliquer, Rétablir */
void dk_bottom (long pw, long ph, dk_fn apply, dk_fn restore)
{
	long y = ph + AP_HEAD - 42;		/* y des boutons */
	dk_w_message = dk_top (ui_label (SIDE_W + 12, y + 6, pw - 226, 20, (long) dk_s_empty, ULB_DIM));
	long right = pw + SIDE_W;		/* bord droit du volet */
	dk_on (dk_top (ui_pushbutton (right - 196, y, 90, 32, (long) dk_s_apply, UB_DEFAULT)), apply);
	dk_on (dk_top (ui_pushbutton (right - 100, y, 90, 32, (long) dk_s_restore, 0)), restore);
}

/* right = bord droit : (dans l'en-tête) la taille, la corbeille */
void dk_header (long right)
{
	dk_w_trash = dk_on (dk_top (ui_checkbox (right - 110, 10, 100, 24, (long) TR (dk_s_trash), dl_trash)),
			    dk_on_trash);
	dk_w_size = dk_on (dk_top (ui_dropdown (right - 262, 10, 142, 26, (long) TR (dk_s_sizes))), dk_on_size);
	dk_top (ui_label (right - 314, 13, 48, 20, (long) dk_s_size, 0));
}

/* les bureaux, la corbeille, la taille : la fin de dock.ini et de panel.ini -> fin */
char *dk_put_tail (char *o)
{
	for (long i = 0; i < dl_ndesks; i++)
	{
		o = dk_put (o, "desk      = ");
		o = dk_put (o, dl_desk[i]);
		o = dk_put (o, CRLF);
	}
	o = dk_put (o, "corbeille = ");
	o = dk_put (o, dl_trash ? "oui" : "non");
	o = dk_put (o, CRLF);
	o = dk_put (o, "taille    = ");
	o = dk_put (o, dk_v_sizes[(dl_size + 1) & 3]);
	return dk_put (o, CRLF);
}

/* ---- les listes ---- */

/* choice : les tiroirs, une ligne par catégorie qui a des applications */
static void dk_fill_drawers (long choice)
{
	dk_nvis = 0;				/* les catégories montrées */
	for (long i = 0; i < dl_ncats; i++)
		if (dl_napps[i])
			dk_vis[dk_nvis++] = (unsigned char) i;
	ui_list_clear (dk_w_drawers);
	for (long i = 0; i < dk_nvis; i++)
	{
		long r = dk_vis[i];
		char txt[96 + CAT_NAME + APP_LABEL];	/* « catégorie : application » */
		char *o = stpcpy (txt, dl_cat_name (r));
		o = stpcpy (o, dk_s_colon);
		const char *lab;
		if (dl_app[r][0])
			lab = dk_label (dl_app[r]);
		else				/* (aucune choisie : la première par nom) */
		{
			long a = dl_main (r);
			lab = a < 0 ? dk_s_unknown : app_label (a);
		}
		stpcpy (o, lab);
		if (dl_hidden[r])
			tr_cat (txt, dk_s_hiddenmark);
		ui_list_add (dk_w_drawers, (long) txt, i);
	}
	ui_w_set_value (dk_w_drawers, choice >= dk_nvis - 1 ? dk_nvis - 1 : choice);
}

/* -> la ligne (dl_*) du tiroir choisi (-1 : aucun) */
static long dk_cur (void)
{
	long v = ui_w_get_value (dk_w_drawers);
	if (v < 0 || v >= dk_nvis)
		return -1;
	return dk_vis[v];
}

/* les applications du tiroir choisi, son « Masqué » */
static void dk_fill_main (void)
{
	ui_list_clear (dk_w_main);
	dk_ncatapps = 0;
	long r = dk_cur ();
	ui_w_set_value (dk_w_hidden, r < 0 ? 0 : dl_hidden[r]);
	long sel = -1;				/* celle choisie */
	if (r >= 0)
		for (long i = 0; i < apps_n; i++)
		{
			long a = ap_order[i];
			if (apps[a].cat != dl_cat[r])
				continue;
			dk_catapps[dk_ncatapps++] = (unsigned char) a;
			if (!strcmp (apps[a].name, dl_app[r]))
				sel = dk_ncatapps - 1;
			ui_list_add (dk_w_main, (long) app_label (a), 0);
		}
	if (sel < 0 && dk_ncatapps)
		sel = 0;
	ui_w_set_value (dk_w_main, sel);
	ui_w_draw (dk_w_main);
}

/* choice : les lanceurs */
static void dk_fill_launchers (long choice)
{
	ui_list_clear (dk_w_launchers);
	for (long i = 0; i < dl_nlaunchers; i++)
		ui_list_add (dk_w_launchers, (long) dk_label (dl_launcher[i]), 0);
	ui_w_set_value (dk_w_launchers, choice >= dl_nlaunchers - 1 ? dl_nlaunchers - 1 : choice);
	ui_w_draw (dk_w_launchers);
}

/* tout d'après dl_* (à l'ouverture, Rétablir) */
static void dk_all_fill (void)
{
	ui_list_clear (dk_w_all);		/* toutes les applications, par nom, sauf les parties du bureau */
	dk_nall = 0;
	for (long i = 0; i < apps_n; i++)
	{
		long a = ap_order[i];
		/* (0xFF, sans catégorie : l'asm lit ca_nom hors du tableau ; ici, pas Shell) */
		if (apps[a].cat < CAT_MAX && !dk_ci_cmp (cats[apps[a].cat], dk_s_shell))
			continue;
		dk_all[dk_nall++] = (unsigned char) a;
		ui_list_add (dk_w_all, (long) app_label (a), 0);
	}
	dk_fill_drawers (0);
	dk_fill_main ();
	dk_fill_launchers (0);
	dk_show_desks ();
	ui_w_set_value (dk_w_size, dl_size + 1);	/* (Korin) */
	ui_w_set_value (dk_w_trash, dl_trash);
}

/* ---- les actions (ui_w_on : widget, code) ---- */

static void dk_on_drawer (long, long code)
{
	if (code == UN_CHANGE)
		dk_fill_main ();
}

/* les tiroirs, le même choisi */
static void dk_redo_drawers (void) { dk_fill_drawers (ui_w_get_value (dk_w_drawers)); }

static void dk_on_main (long, long code)
{
	if (code != UN_CHANGE)
		return;
	long r = dk_cur ();
	if (r < 0)
		return;
	long v = ui_w_get_value (dk_w_main);
	if (v < 0 || v >= dk_ncatapps)
		return;
	dk_copy_n (dl_app[r], apps[dk_catapps[v]].name, APP_NAME);
	dk_redo_drawers ();
}

static void dk_on_hidden (long, long code)
{
	if (code != UN_CHANGE)
		return;
	long r = dk_cur ();
	if (r < 0)
		return;
	dl_hidden[r] = (unsigned char) ui_w_get_value (dk_w_hidden);
	dk_redo_drawers ();
}

/* dir = sens : le tiroir choisi, une place plus loin (parmi ceux montrés : les catégories sans application gardent
 * la leur) */
static void dk_move (long dir)
{
	long v = ui_w_get_value (dk_w_drawers);
	if (v < 0)
		return;
	long n = v + dir;			/* sa nouvelle place */
	if (n < 0 || n >= dk_nvis)
		return;
	dl_swap (dk_vis[v], dk_vis[n]);
	dk_fill_drawers (n);
}

static void dk_on_left (long, long code)
{
	if (code == UN_ACTIVATE)
		dk_move (-1);
}

static void dk_on_right (long, long code)
{
	if (code == UN_ACTIVATE)
		dk_move (1);
}

/* (bouton « < Ajouter », double-clic dans la liste de toutes) */
static void dk_on_add (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	long v = ui_w_get_value (dk_w_all);
	if (v < 0 || v >= dk_nall)
		dk_message (dk_s_choose);
	else if (dl_nlaunchers >= DOCK_MAXLAUNCHERS)
		dk_message (dk_s_full);
	else
	{
		dl_launcher_add (apps[dk_all[v]].name);
		dk_fill_launchers (dl_nlaunchers - 1);
	}
}

static void dk_on_remove (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	long v = ui_w_get_value (dk_w_launchers);
	if (v < 0 || v >= dl_nlaunchers)
		return;
	dl_nlaunchers--;			/* les suivants remontent */
	memmove (dl_launcher[v], dl_launcher[v + 1], (size_t) (dl_nlaunchers - v) * APP_NAME);
	dk_fill_launchers (v);
}

/* dir = sens : le lanceur choisi, une place plus loin */
static void dk_move_launcher (long dir)
{
	long v = ui_w_get_value (dk_w_launchers);
	if (v < 0)
		return;
	long n = v + dir;
	if (n < 0 || n >= dl_nlaunchers || v >= dl_nlaunchers)
		return;
	char t[APP_NAME];
	memcpy (t, dl_launcher[v], APP_NAME);
	memcpy (dl_launcher[v], dl_launcher[n], APP_NAME);
	memcpy (dl_launcher[n], t, APP_NAME);
	dk_fill_launchers (n);
}

static void dk_on_lup (long, long code)
{
	if (code == UN_ACTIVATE)
		dk_move_launcher (-1);
}

static void dk_on_ldown (long, long code)
{
	if (code == UN_ACTIVATE)
		dk_move_launcher (1);
}

static void dk_on_restore (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	dock_layout_load ();
	dk_all_fill ();
	dk_message (dk_s_empty);
}

/* réécrit SD0:/etc/dock.ini, relance la barre du bureau */
static void dk_apply (void)
{
	for (long i = 0; i < dl_ndesks; i++)	/* les noms des bureaux */
		dk_read_name (i, 1);
	long shown = 0;				/* un tiroir au moins */
	for (long i = 0; i < dk_nvis; i++)
		if (!dl_hidden[dk_vis[i]])
			shown++;
	if (!shown)
	{
		dk_message (dk_s_one);
		return;
	}
	char *o = dk_put (dk_file, dk_header_text);
	for (long i = 0; i < dl_ncats; i++)	/* les catégories, dans l'ordre */
	{
		o = dk_put (o, "drawer    = ");
		o = dk_put (o, dl_cat_name (i));
		o = dk_put (o, ", ");
		o = dk_put (o, dl_app[i]);
		o = dk_put (o, CRLF);
	}
	for (long i = 0; i < dl_ncats; i++)	/* celles masquées */
		if (dl_hidden[i])
		{
			o = dk_put (o, "hidden    = ");
			o = dk_put (o, dl_cat_name (i));
			o = dk_put (o, CRLF);
		}
	for (long i = 0; i < dl_nlaunchers; i++)	/* les lanceurs */
	{
		o = dk_put (o, "launcher  = ");
		o = dk_put (o, dl_launcher[i]);
		o = dk_put (o, CRLF);
	}
	dk_put_tail (o);			/* les bureaux, la corbeille, la taille */
	if (file_write (DL_PATH, dk_file) < 0)
	{
		dk_message (dk_s_write);
		return;
	}
	relaunch (n_dock, p_dockp);
	dk_message (dk_s_done);
}

static void dk_on_apply (long, long code)
{
	if (code == UN_ACTIVATE)
		dk_apply ();
}

/* ---- le volet ---- */

static void dk_open (void)
{
	applis_load ();				/* (les applications installées depuis) */
	apps_sort ();
	dock_layout_load ();
	dk_widgets ();
	long pw = ui.w - SIDE_W, ph = ui.h - AP_HEAD;	/* le volet */
	if (pw > DK_W)
		pw = DK_W;
	unsigned long miss = DK_H - ph < 0 ? 0 : (unsigned long) (DK_H - ph);	/* hauteur des tiroirs (262 chez */
	long dh = 262 - (long) (miss / 3 * 2);				/* Onyx), les 2/3 de ce qui manque */
	long ly = dh + 10, lh = ph - 48 - ly;	/* y des lanceurs, leur hauteur */
	long listh = dh - 82;			/* hauteur de la liste des tiroirs */
	long gw = pw - 230;			/* largeur du groupe des tiroirs */
	long x = SIDE_W + 10, y = AP_HEAD + 6;	/* coin du groupe */

	/* ---- les tiroirs */
	long g = dk_top (ui_groupbox (x, y, gw, dh, (long) TR (dk_s_drawers)));	/* toutes les catégories */
	dk_w_drawers = dk_tip (dk_on (in (ui_list (x + 10, y + DK_CT, gw - 180, listh, 0), g), dk_on_drawer), dk_t_drawers);
	dk_tip (dk_on (in (ui_pushbutton (x + 10, y + DK_CT + 8 + listh, 34, DK_BTN, (long) dk_s_up, 0), g), dk_on_left),
		dk_t_left);
	dk_tip (dk_on (in (ui_pushbutton (x + 48, y + DK_CT + 8 + listh, 34, DK_BTN, (long) dk_s_down, 0), g), dk_on_right),
		dk_t_right);
	dk_w_hidden = dk_tip (dk_on (in (ui_checkbox (x + 96, y + DK_CT + 10 + listh, 150, 24, (long) TR (dk_s_hidden), 0), g),
				     dk_on_hidden), dk_t_hidden);
	in (ui_label (x + gw - 158, y + DK_CT - 2, 148, 18, (long) dk_s_main, 0), g);	/* Application principale */
	dk_w_main = dk_tip (dk_on (in (ui_list (x + gw - 158, y + DK_CT + 18, 148, listh + 18, 0), g), dk_on_main), dk_t_main);

	/* ---- les bureaux */
	x += gw + 10;
	dk_desks_group (x, y, dh, dk_t_name);

	/* ---- les lanceurs */
	x = SIDE_W + 10;
	y += ly - 6;
	g = dk_top (ui_groupbox (x, y, pw - 20, lh, (long) TR (dk_s_launchers)));
	long h2 = lh - 38;			/* hauteur des listes */
	y += DK_CT;
	dk_w_launchers = in (ui_list (x + 10, y, 200, h2, 0), g);
	unsigned long bl = (unsigned long) (h2 + 4) / 3;	/* une ligne de boutons (32 au plus) */
	if (bl > 32)
		bl = 32;
	long b = (long) bl;
	dk_on (in (ui_pushbutton (x + 218, y, 96, b - 4, (long) dk_s_add, 0), g), dk_on_add);
	dk_on (in (ui_pushbutton (x + 218, y + b, 96, b - 4, (long) dk_s_remove, 0), g), dk_on_remove);
	dk_on (in (ui_pushbutton (x + 218, y + 2 * b, 46, b - 4, (long) dk_s_up, 0), g), dk_on_lup);
	dk_on (in (ui_pushbutton (x + 268, y + 2 * b, 46, b - 4, (long) dk_s_down, 0), g), dk_on_ldown);
	dk_w_all = dk_tip (dk_on (in (ui_list (x + 322, y, pw - 352, h2, 0), g), dk_on_add), dk_t_all);	/* toutes */

	/* ---- le message, Appliquer, Rétablir ; (Korin) dans l'en-tête : la taille, la corbeille */
	dk_bottom (pw, ph, dk_on_apply, dk_on_restore);
	dk_header (pw + SIDE_W);
	dk_all_fill ();
}

const Applet dock_applet = { "dockconf", "Dock", "Dock", "dockconf", dk_open, dk_apply };
