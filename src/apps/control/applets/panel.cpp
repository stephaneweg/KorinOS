/*
 * applets/panel.cpp -- le volet Panel du panneau de configuration : les réglages de /etc/panel.ini en widgets
 * de UIKit :
 *   * les RACCOURCIS du panel : ajouter une application de la liste de toutes, en retirer une, la déplacer ;
 *   * les BUREAUX (espaces de travail) : combien (1 à 6) et leurs noms ;
 *   * dans l'en-tête du volet, la TAILLE du panel et la CORBEILLE.
 * Appliquer écrit panel.ini et relance le panel ; Rétablir relit ce qui est enregistré.
 *
 * Port en C++ de applets/panel.inc et src/inc/panelconf.inc (Korin ; le panel n'existe pas dans Onyx). Reprend du
 * volet de l'ancien dock (dock.cpp, masqué) ses aides, les bureaux, la taille et la corbeille (dock.h).
 */
#include <armpi/userconf.h>
#include <string.h>
#include "dock.h"

/* ---- panelconf (src/inc/panelconf.inc ; src/sys/common/panelconf.c pour korinrt) ----
 *	pin       = terminal		un raccourci, dans l'ordre (12 au plus)
 *	desk      = Principal		un espace de travail, son nom (1 à 6)
 *	corbeille = oui | non
 *	taille    = auto | grande | moyenne | petite
 * Sans panel.ini : dock.ini de l'ancien dock (ses lanceurs deviennent des raccourcis), sinon terminal et fichiers. */
enum { PANEL_MAXPINS = 12 };
#define PAN_PATH	"/etc/panel.ini"

static long pan_npin;
static char pan_pin[PANEL_MAXPINS][APP_NAME];

/* un raccourci de plus */
static void pan_add (const char *name)
{
	if (!*name || pan_npin >= PANEL_MAXPINS)
		return;
	dk_copy_n (pan_pin[pan_npin++], name, APP_NAME);
}

/* (ini_read) */
static void pan_line (char *key, char *val, void *data)
{
	if (!strcmp (key, "pin") || !strcmp (key, "launcher") || !strcmp (key, "lanceur"))
		pan_add (val);
	else
		dl_line (key, val, data);
}

static void pan_load (void)
{
	dl_ncats = 0;
	dl_nlaunchers = 0;
	dl_ndesks = 0;
	pan_npin = 0;
	dl_trash = 1;
	dl_size = -1;
	char cp_[160], cq_[160];
	if (!ini_read (korin_conf_path ("panel.ini", cp_, sizeof cp_, 0), pan_line) && !ini_read (korin_conf_path ("dock.ini", cq_, sizeof cq_, 0), pan_line))
	{
		pan_add (DL_DEFAULT_TERMINAL);
		pan_add (DL_DEFAULT_FILES);
	}
	dl_desks_default ();
}

/* app -> 1 si elle est une partie du bureau (catégorie Shell : ni dans le lanceur, ni parmi les raccourcis) */
static long pan_shell (long app)
{
	long c = apps[app].cat;
	if (c >= CAT_MAX)			/* (0xFF, sans catégorie : l'asm lit ca_nom hors du tableau ; ici, pas Shell) */
		return 0;
	return dk_ci_cmp (cats[c], "Shell") == 0;
}

/* ---- les textes (panel.inc) ---- */
static const char pnl_s_pins[] = "Raccourcis";
static const char pnl_s_on[] = "Sur le panel";
static const char pnl_s_all[] = "Toutes les applications";
static const char pnl_t_name[] = "Son nom (affich\xC3\xA9 par le carr\xC3\xA9 du panel)";
static const char pnl_s_already[] = "Cette application a d\xC3\xA9j\xC3\xA0 un raccourci.";
static const char pnl_s_full[] = "12 raccourcis au plus.";
static const char pnl_s_write[] = "Impossible d'\xC3\xA9" "crire /etc/panel.ini.";
static const char pnl_s_done[] = "Appliqu\xC3\xA9 : le panel en tient compte.";
static const char pnl_header[] =
	"# Le panel (/sys/panel) ; ecrit par le panneau de configuration" CRLF
	"# (applet Panel)." CRLF
	"# pin       = application       (raccourcis, 12 au plus)" CRLF
	"# desk      = nom               (bureaux, 1 a 6)" CRLF
	"# corbeille = oui | non" CRLF
	"# taille    = auto | grande | moyenne | petite" CRLF;

/* ---- les listes ---- */

/* choice : les raccourcis */
static void pnl_fill (long choice)
{
	ui_list_clear (dk_w_launchers);
	for (long i = 0; i < pan_npin; i++)
		ui_list_add (dk_w_launchers, (long) dk_label (pan_pin[i]), 0);
	ui_w_set_value (dk_w_launchers, choice >= pan_npin - 1 ? pan_npin - 1 : choice);
	ui_w_draw (dk_w_launchers);
}

/* tout d'après les réglages (à l'ouverture, Rétablir) */
static void pnl_all_fill (void)
{
	ui_list_clear (dk_w_all);		/* toutes les applications, par nom, sauf les parties du bureau */
	dk_nall = 0;
	for (long i = 0; i < apps_n; i++)
	{
		long a = ap_order[i];
		if (pan_shell (a))
			continue;
		dk_all[dk_nall++] = (unsigned char) a;
		ui_list_add (dk_w_all, (long) app_label (a), 0);
	}
	pnl_fill (0);
	dk_show_desks ();
	ui_w_set_value (dk_w_size, dl_size + 1);
	ui_w_set_value (dk_w_trash, dl_trash);
}

/* ---- les actions ---- */

/* (bouton « < Ajouter », double-clic dans la liste de toutes) */
static void pnl_on_add (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	long v = ui_w_get_value (dk_w_all);
	if (v < 0 || v >= dk_nall)
	{
		dk_message (dk_s_choose);
		return;
	}
	const char *name = apps[dk_all[v]].name;
	for (long i = 0; i < pan_npin; i++)	/* déjà là ? */
		if (!strcmp (pan_pin[i], name))
		{
			dk_message (pnl_s_already);
			return;
		}
	if (pan_npin >= PANEL_MAXPINS)
	{
		dk_message (pnl_s_full);
		return;
	}
	pan_add (name);
	pnl_fill (pan_npin - 1);
}

static void pnl_on_remove (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	long v = ui_w_get_value (dk_w_launchers);
	if (v < 0 || v >= pan_npin)
		return;
	pan_npin--;				/* les suivants remontent */
	memmove (pan_pin[v], pan_pin[v + 1], (size_t) (pan_npin - v) * APP_NAME);
	pnl_fill (v);
}

/* dir = sens : le raccourci choisi, une place plus loin */
static void pnl_move (long dir)
{
	long v = ui_w_get_value (dk_w_launchers);
	if (v < 0)
		return;
	long n = v + dir;			/* sa nouvelle place */
	if (n < 0 || n >= pan_npin || v >= pan_npin)
		return;
	char t[APP_NAME];
	memcpy (t, pan_pin[v], APP_NAME);
	memcpy (pan_pin[v], pan_pin[n], APP_NAME);
	memcpy (pan_pin[n], t, APP_NAME);
	pnl_fill (n);
}

static void pnl_on_left (long, long code)
{
	if (code == UN_ACTIVATE)
		pnl_move (-1);
}

static void pnl_on_right (long, long code)
{
	if (code == UN_ACTIVATE)
		pnl_move (1);
}

static void pnl_on_restore (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	pan_load ();
	pnl_all_fill ();
	dk_message (dk_s_empty);
}

/* réécrit /etc/panel.ini, relance le panel */
static void pnl_apply (void)
{
	for (long i = 0; i < dl_ndesks; i++)	/* les noms des bureaux */
		dk_read_name (i, 1);
	char *o = dk_put (dk_file, pnl_header);
	for (long i = 0; i < pan_npin; i++)	/* les raccourcis */
	{
		o = dk_put (o, "pin       = ");
		o = dk_put (o, pan_pin[i]);
		o = dk_put (o, CRLF);
	}
	dk_put_tail (o);			/* les bureaux, la corbeille, la taille */
	char cp_[160];
	if (file_write (korin_conf_path ("panel.ini", cp_, sizeof cp_, 1), dk_file) < 0)
	{
		dk_message (pnl_s_write);
		return;
	}
	relaunch ("panel", "/sys/panel");
	dk_message (pnl_s_done);
}

static void pnl_on_apply (long, long code)
{
	if (code == UN_ACTIVATE)
		pnl_apply ();
}

/* ---- le volet ---- */

static void pnl_open (void)
{
	applis_load ();				/* (les applications installées depuis) */
	apps_sort ();
	pan_load ();
	dk_widgets ();
	long pw = ui.w - SIDE_W, ph = ui.h - AP_HEAD;	/* le volet */
	if (pw > DK_W)
		pw = DK_W;
	long gh = ph - 54;			/* hauteur des groupes */
	long gw = pw - 230;			/* largeur du groupe des raccourcis */
	long x = SIDE_W + 10, y = AP_HEAD + 6;	/* son coin */

	/* ---- les raccourcis */
	long g = dk_top (ui_groupbox (x, y, gw, gh, (long) TR (pnl_s_pins)));
	long lw = (long) ((unsigned long) (gw - 30) >> 1);	/* largeur d'une liste */
	long lh = gh - (DK_CT + 18 + DK_BTN + 16);		/* leur hauteur */
	ui_w_set_parent (ui_label (x + 10, y + DK_CT - 2, lw, 18, (long) pnl_s_on, 0), g);	/* sur le panel */
	dk_w_launchers = ui_list (x + 10, y + DK_CT + 18, lw, lh, 0);
	ui_w_set_parent (dk_w_launchers, g);
	long by = y + DK_CT + 26 + lh;		/* y des boutons */
	long w = ui_pushbutton (x + 10, by, lw - 84, DK_BTN, (long) dk_s_remove, 0);	/* Retirer */
	ui_w_set_parent (w, g);
	dk_on (w, pnl_on_remove);
	w = ui_pushbutton (x + lw - 68, by, 36, DK_BTN, (long) dk_s_up, 0);	/* ^ */
	ui_w_set_parent (w, g);
	dk_tip (dk_on (w, pnl_on_left), dk_t_left);
	w = ui_pushbutton (x + lw - 28, by, 36, DK_BTN, (long) dk_s_down, 0);	/* v */
	ui_w_set_parent (w, g);
	dk_tip (dk_on (w, pnl_on_right), dk_t_right);
	ui_w_set_parent (ui_label (x + lw + 20, y + DK_CT - 2, lw, 18, (long) pnl_s_all, 0), g);	/* toutes */
	dk_w_all = ui_list (x + lw + 20, y + DK_CT + 18, lw, lh, 0);
	ui_w_set_parent (dk_w_all, g);
	dk_tip (dk_on (dk_w_all, pnl_on_add), dk_t_all);
	w = ui_pushbutton (x + lw + 20, by, lw, DK_BTN, (long) dk_s_add, 0);	/* < Ajouter */
	ui_w_set_parent (w, g);
	dk_on (w, pnl_on_add);

	/* ---- les bureaux */
	dk_desks_group (x + gw + 10, y, gh, pnl_t_name);

	/* ---- le message, Appliquer, Rétablir ; dans l'en-tête, à gauche des boutons de la fenêtre : la taille, la
	 * corbeille */
	dk_bottom (pw, ph, pnl_on_apply, pnl_on_restore);
	dk_header (pw + SIDE_W - FRAME_CAP_W);
	pnl_all_fill ();
}

const Applet panel_applet = { "panelconf", "Panel", "Panel", "panelconf", pnl_open, pnl_apply };
