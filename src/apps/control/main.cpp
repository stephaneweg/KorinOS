/*
 * control -- le panneau de configuration (Réglages), comme le control d'Onyx : une seule fenêtre, la liste des
 * volets à gauche (icône, nom), le volet choisi à droite (applet.cpp). Les volets sont dans applets/*.cpp : Thème
 * (avec son aperçu), Région (clavier, langue, pays, fuseau), Écran, Panel, Wi-Fi, Accès à distance, Son, Manette,
 * Préchargement, .NET, Logiciels, Imprimantes. « control <volet> » l'ouvre sur ce volet (theme, clavier, ecran,
 * panelconf, wifi, remote, sound, padconf, preload, dotnet, pkgman, printconf).
 *
 * Port en C++ de control.asm (Korin), un seul source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'à sa
 * validation sur le Pi. Le cadre d'Onyx (user/Apps/control/main.cpp : des volets lancés à part, dans une surface
 * partagée) n'est pas repris : Korin réunit les volets dans le programme, et c'est ce que l'utilisateur a validé.
 */
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uikit.h>
#include <buskit_abi.h>
#include <netkit_abi.h>
#include <audiokit_abi.h>
#include <printerkit_abi.h>
#include "control.h"

struct ui_vars ui;

/* les volets, dans l'ordre de la liste ; ceux dont needs manque ne sont pas montrés (applets_load) */
static const Applet *const all_applets[] = {
	&theme_applet, &keyboard_applet, &display_applet,
	/* (le volet de l'ancien dock, masqué : le panel l'a remplacé) &dock_applet, */
	&panel_applet, &wifi_applet, &remote_applet, &sound_applet, &power_applet, &mouse_applet, &padconf_applet, &preload_applet, &startup_applet, &dotnet_applet,
	&pkgman_applet, &printers_applet, &users_applet,
};
enum { ALL_N = sizeof all_applets / sizeof all_applets[0], PLUGIN_MAX = 32 };
static const Applet *applets[ALL_N + PLUGIN_MAX];	/* les volets montrés */
static long APN;

static int name_cmp (const void *a, const void *b)
{
	return strcmp (*(const char *const *) a, *(const char *const *) b);
}

/* les volets installés par des paquets : /sys/applets/<nom>.so (control.h, « les applets installées ») */
static void plugins_load (void)
{
	DIR *d = opendir (APPLET_DIR);
	if (!d)
		return;
	static char names[PLUGIN_MAX][64];
	const char *order[PLUGIN_MAX];
	long n = 0;
	for (struct dirent *e; n < PLUGIN_MAX && (e = readdir (d));)
	{
		size_t l = strlen (e->d_name);
		if (l > 3 && l < sizeof names[0] && !strcmp (e->d_name + l - 3, ".so"))
		{
			strcpy (names[n], e->d_name);
			order[n] = names[n];
			n++;
		}
	}
	closedir (d);
	qsort (order, (size_t) n, sizeof order[0], name_cmp);
	for (long i = 0; i < n; i++)
	{
		char path[160];
		snprintf (path, sizeof path, "%s/%s", APPLET_DIR, order[i]);
		void *h = dlopen (path, RTLD_NOW | RTLD_LOCAL);
		if (!h)
		{
			fprintf (stderr, "control: %s : %s\n", path, dlerror ());
			continue;
		}
		const int *abi = (const int *) dlsym (h, "korin_applet_abi");
		const Applet *a = (const Applet *) dlsym (h, "korin_applet");
		if (!abi || !a || *abi != APPLET_ABI || !a->open || !a->apply || !a->name || !a->label)
		{
			fprintf (stderr, "control: %s : pas un volet d'ABI %d\n", path, APPLET_ABI);
			dlclose (h);
			continue;
		}
		if (a->needs && access (a->needs, F_OK) != 0)
		{
			dlclose (h);
			continue;
		}
		long k = 0;			/* (le module reste chargé) */
		while (k < APN && strcmp (applets[k]->name, a->name))
			k++;
		if (k < APN)			/* le nom d'un volet de Réglages : il prend sa place (Logiciels, pkg.so) */
			applets[k] = a;
		else
			applets[APN++] = a;
	}
}

/* les volets montrés : ceux qui ne demandent rien, ou dont le fichier est là (le volet .NET : dllpreload, du
 * paquet dotnet), puis ceux des paquets */
static void applets_load (void)
{
	APN = 0;
	for (long i = 0; i < ALL_N; i++)
		if (!all_applets[i]->needs || access (all_applets[i]->needs, F_OK) == 0)
			applets[APN++] = all_applets[i];
	plugins_load ();
}

static long current;			/* le volet ouvert */
static long side_first;			/* la première ligne montrée de la liste */
static long side_mx;			/* x de la souris (molette sur la liste ?) */
long ap_w, ap_h;
long ap_net_sub;
int ap_net_update;

/* ---- la fenêtre ---- */

/* -> largeur, hauteur de la fenêtre selon la place sur l'écran ; ap_w, ap_h, ap.percol */
static void ap_height (long *pw, long *ph)
{
	long scr[4];
	long w = AP_WIN_W, h = AP_WIN_H;
	if (ui_screen ((long) scr) >= 0)
	{
		unsigned long sw = (unsigned long) (scr[0] - 2 * FRAME_BORDER);	/* largeur : le volet d'Onyx si l'écran */
		if (sw < (unsigned long) w)					/* le permet */
			w = (long) sw;
		h = scr[1] - scr[2] - scr[3] - (FRAME_TITLE_H + FRAME_BORDER + 24);	/* hauteur - réserves - cadre - marge */
	}
	if (h > AP_WIN_H)
		h = AP_WIN_H;
	if (h < 320)
		h = 320;
	ap_w = w;
	ap_h = h;
	ap.percol = (long) ((unsigned long) (h - (AP_HEAD + AP_BOTTOM + 8)) / ROW_H);	/* lignes par colonne */
	*pw = w;
	*ph = h;
}

/* l'écran a changé : la fenêtre à la taille qu'il permet -> 1 si elle a changé (le programme redessine ;
 * pendant le dialogue d'Écran, WEV_SCREEN est perdu : son volet appelle ceci) */
long ap_fit (void)
{
	long ow = ap_w, oh = ap_h, w, h;
	ap_height (&w, &h);
	if (w == ow && h == oh)
	{
		ap_border ();
		return 0;
	}
	ui_resize (w, h);
	ap_border ();
	return 1;
}

/* taille donnée par Elegant (WEV_RESIZE, mode poche) : la fenêtre la prend -> 1 si elle a changé */
static long ap_set_size (long w, long h)
{
	if (ap_w == w && ap_h == h)
		return 0;
	ap_w = w;
	ap_h = h;
	ap.percol = (long) ((unsigned long) (h - (AP_HEAD + AP_BOTTOM + 8)) / ROW_H);
	ui_resize (w, h);
	ap_border ();
	return 1;
}

/* ---- la liste des volets ---- */

/* -> lignes de la liste que la fenêtre montre (1 au moins) */
static long side_vis (void)
{
	long n = (ui.h - 12) / SIDE_H;
	return n < 1 ? 1 : n;
}

/* side_first ramené dans [0, APN - visibles] */
static void side_border (void)
{
	long m = APN - side_vis ();
	if (m < 0)
		m = 0;
	if (side_first > m)
		side_first = m;
	if (side_first < 0)
		side_first = 0;
}

/* le volet ouvert dans la partie visible de la liste */
static void side_show (void)
{
	long v = side_vis (), f = side_first;
	if (current < f)
		f = current;			/* au-dessus : il devient le premier */
	if (current >= f + v)
		f = current - v + 1;		/* au-dessous : le dernier */
	side_first = f;
	side_border ();
}

/* n = crans de la molette (> 0 : vers le haut) */
static void side_wheel (long n)
{
	side_first -= n;
	side_border ();
}

/* toute la fenêtre : la liste, puis le volet */
void draw (void)
{
	side_border ();
	ui_fill (0, 0, SIDE_W, ui.h, tok (TJ_SUNKEN));	/* la liste : plus foncée, comme Fichiers */
	ui_fill (SIDE_W - 1, 0, 1, ui.h, tok (TJ_LINE));
	for (long i = side_first; i < APN; i++)		/* (la liste défile : les lignes qui tiennent) */
	{
		long y = (i - side_first) * SIDE_H + 12;
		if (y + SIDE_H - 4 > ui.h)
			break;
		if (current == i)		/* choisi : en creux accentué */
			ui_rbox (8, y, SIDE_W - 16, SIDE_H - 4, 8, tok (TJ_ACCENT_LIGHT), tok (TJ_ACCENT_LIGHT));
		ui_icon ((long) applets[i]->icon, 16, y + (SIDE_H - 4 - SIDE_IC) / 2, SIDE_IC);
		ui_text ((long) TR (applets[i]->label), 16 + SIDE_IC + 10, y + (SIDE_H - 4) / 2 + 5, tok (TJ_TEXT1), 13,
			 current == i ? FB () : F (), SIDE_W - 16 - SIDE_IC - 18);
	}
	long v = side_vis ();			/* pas toutes : la barre de défilement */
	if (v < APN)
	{
		unsigned long track = (unsigned long) (ui.h - 24);
		long y = (long) ((unsigned long) side_first * track / APN) + 12;
		long h = (long) ((unsigned long) v * track / APN);
		ui_rbox (SIDE_W - 7, y, 3, h, 1, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
	}
	pane_draw ();
	ui_update_all ();
}

/* ---- un volet ---- */

/* i = volet : l'ouvre dans le volet (l'état remis à zéro) */
static void open_applet (long i)
{
	if (ap.close)				/* le volet d'avant s'en va (ses widgets) */
	{
		void (*c) (void) = ap.close;
		ap.close = 0;
		c ();
	}
	long percol = ap.percol;
	memset (&ap, 0, sizeof ap);
	ap.percol = percol;
	ap.colw = COL_W;
	current = i;
	ap.title = applets[i]->title;
	applets[i]->open ();
}

static void apply (void)
{
	applets[current]->apply ();
	draw ();
}

static void go (long i)
{
	if (i == current)
		return;
	open_applet (i);
	side_show ();
	draw ();
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui))
		return 1;
	netkit_kit_open ();
	audiokit_kit_open ();
	printerkit_kit_open ();
	long bus = buskit_kit_open ();
	applis_load ();
	applets_load ();
	long first = 0;				/* le volet nommé, sinon le premier */
	if (argc > 1)
	{
		char arg[64] = "";		/* (la ligne d'arguments, comme ui_arg de l'asm) */
		for (int i = 1; i < argc; i++)
		{
			if (i > 1)
				strncat (arg, " ", sizeof arg - 1 - strlen (arg));
			strncat (arg, argv[i], sizeof arg - 1 - strlen (arg));
		}
		for (long i = 0; i < APN; i++)
			if (strcmp (applets[i]->name, arg) == 0)
			{
				first = i;
				break;
			}
	}
	long w, h;
	ap_height (&w, &h);
	if (ui_window (w, h, (long) TR ("Réglages")) < 0)
		return 0;
	long s = bus ? bus_subscribe ((long) "korin.net.*", 0) : 0;	/* le réseau (volet Wi-Fi) : le bus */
	ap_net_sub = s > 0 ? s : 0;
	open_applet (first);
	side_show ();
	draw ();
	for (;;)
	{
		long timeout = 60000;
		if (ap.tick)			/* volet rafraîchi chaque seconde */
			timeout = 1000;
		if (ap.widgets)			/* (widgets : leurs infobulles) */
			timeout = 250;
		long t = ui_event (timeout);
		if (t == WEV_CLOSE)
			break;
		bool tick = false;
		if (t == 0)
		{
			if (ap.widgets)
				ui_w_idle ();
			tick = true;
		}
		else if (t == WEV_BUS)		/* korin.net.* : le volet relit l'état */
		{
			if (ui_ev_field (&ui, BEV_SUB) != ap_net_sub)
				continue;
			ap_net_update = 1;
			tick = true;
		}
		if (tick)
		{
			if (ap.tick && ap.tick ())
				draw ();
			continue;
		}
		if (t == WEV_SCREEN || t == WEV_RESIZE)
		{
			long changed = t == WEV_RESIZE ? ap_set_size (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y))
						       : ap_fit ();	/* agrandie (mode poche) : tout l'espace */
			if (changed && ap.widgets)	/* (widgets : refaits à la nouvelle taille) */
				open_applet (current);
			draw ();
			continue;
		}
		if (t == WEV_PAD)		/* une manette : le volet qui la montre */
		{
			if (ap.pad && ap.pad ())
				draw ();
			continue;
		}
		if (t == WEV_MOUSE)		/* (où est la souris : pour la molette) */
			side_mx = ui_ev_field (&ui, EV_X);
		if (t == WEV_WHEEL && (unsigned long) side_mx < SIDE_W)	/* molette sur la liste : elle défile */
		{
			side_wheel (ui_ev_field (&ui, EV_Y));
			draw ();
			continue;
		}
		if (ap.widgets)			/* volet en widgets : à eux d'abord */
		{
			long code, taken;
			uikit_w_event (&code, &taken);
			if (ap.redraw)		/* (un widget caché, un message changé) */
			{
				ap.redraw = 0;
				draw ();
			}
			if (taken)
				continue;
			t = ui_ev_field (&ui, EV_TYPE);
		}
		if (t == WEV_KEY)
		{
			if (ui_ev_field (&ui, EV_KIND) == 0)
				continue;
			if (ap.key && ap.key ())	/* le volet d'abord (champ de saisie) */
			{
				draw ();
				continue;
			}
			long code = ui_ev_field (&ui, EV_CODE);
			if (code == KEY_ESC)
				break;
			if (code == KEY_ENTER)
				apply ();
			else if (code == KEY_UP && current > 0)
				go (current - 1);	/* volet précédent, suivant */
			else if (code == KEY_DOWN && current + 1 < APN)
				go (current + 1);
			continue;
		}
		if (t == WEV_WHEEL)		/* molette : les lignes défilent */
		{
			if (pane_wheel (ui_ev_field (&ui, EV_Y)))
				draw ();
			continue;
		}
		if (t != WEV_MOUSE || ui_ev_field (&ui, EV_KIND) != 1)
			continue;
		long x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
		if (y < 36)			/* le haut du volet déplace la fenêtre */
		{
			ui_window_drag ();
			continue;
		}
		if (x < SIDE_W)			/* la liste */
		{
			if (y - 12 < 0)
				continue;
			long i = (long) ((unsigned long) (y - 12) / SIDE_H) + side_first;
			if (i < APN)
				go (i);
			continue;
		}
		int r = pane_click (x, y);
		if (r == 1)
			apply ();
		else if (r)
			draw ();
	}
	return 0;
}
