# Le squelette d'app en C++

Une application complète, courte, à copier pour commencer la sienne : une fenêtre redimensionnable aux couleurs du thème, un champ, un bouton, une liste, une vue dessinée à la main, des menus avec raccourcis, des textes traduits et une aide. Elle se compile telle quelle sous Korin OS et sur le PC.

Ses fichiers sont aussi sur la carte, dans `SD0:/apps/help.app/api/skeleton/` :

```sh
mkdir -p SD0:/home/projets
cp -r SD0:/apps/help.app/api/skeleton SD0:/home/projets/skeleton
pibuild app SD0:/home/projets/skeleton skeleton
```

L'app s'installe dans `/apps/skeleton.app` ; elle apparaît dans le lanceur, catégorie **Développement**, sous le nom **Squelette**. Pour la renommer : renommer le dossier, changer `APP` dans `main.cpp` et `name` dans `app.txt`.

| Fichier | Rôle |
|---|---|
| `main.cpp` | le programme |
| `app.txt` | le manifeste : nom, catégorie, description |
| `data/lang-en.txt` | les textes en anglais, posés dans `/apps/skeleton.app/lang-en.txt` |
| `help_fr.md`, `help_en.md` | l'aide, ouverte par le menu **Aide** de la barre de menus |

## main.cpp

```cpp
//
// main.cpp -- squelette d'une application de Korin OS en C++ (guide du developpeur : SD0:/apps/help.app/api).
//
// Tout ce qu'une app fait d'ordinaire, en peu de lignes :
//   * une fenetre redimensionnable, sans barre de titre (le chrome de Korin OS) : les 32 px du haut la deplacent,
//     les boutons de la fenetre sont en haut a droite (FRAME_CAP_W) : rien d'interactif n'y va ;
//   * les couleurs du theme du bureau (jetons de UIKit : ui_jetons), jamais des couleurs ecrites en dur ;
//   * des widgets (bouton, champ de texte, liste) et une vue dessinee a la main (ui_view) ;
//   * des menus dans la barre de menus (ui_menu), leurs raccourcis (ui_menu_key) ;
//   * les textes traduits (ui_tr : la cle est le texte francais, data/lang-en.txt donne l'anglais).
//
// Construire sous Korin OS (le dossier donne le nom de l'app : /apps/<dossier>.app) :
//     pibuild app SD0:/home/projets/skeleton skeleton
// Sur le PC (WSL, chaine de tools/gcc) : copier le dossier dans src/apps/<nom>, puis
//     sh tools/office/build.sh <nom>
//
#include <stdio.h>
#include <string.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))
static const char *APP = "skeleton";		// le nom du dossier de l'app : /apps/<APP>.app (a changer avec lui)

static struct ui_vars ui;			// la fenetre courante : poignee, pixels, taille, dernier evenement, polices

// ---- le theme : les jetons de UIKit (TJ_* de uikit.inc, 32 bits chacun) ----------------------------------------------
static const unsigned *s_jet;
enum { J_FACE = 0, J_SURFACE = 1, J_CREUX = 2, J_LIGNE = 5, J_TEXTE1 = 7, J_TEXTE2 = 8, J_ACCENT = 10 };
static unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

// ---- l'etat de l'app -------------------------------------------------------------------------------------------------
enum { TOPH = 48, CAP = FRAME_CAP_W + 8 };	// barre du haut ; place laissee aux boutons de la fenetre
enum { M_NEW = 1, M_CLEAR, M_ABOUT };		// les entrees des menus (id >= 0)
static long g_add, g_field, g_list, g_view;	// les widgets
static int g_count;				// elements ajoutes

// la vue dessinee a la main : ses coordonnees partent de son coin (0, 0)
static void view_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_SURFACE));
	ui_rbox (16, 16, w - 32, 72, 10, jet (J_CREUX), jet (J_CREUX));
	char t[64];
	snprintf (t, sizeof t, TR ("%d \xC3\xA9l\xC3\xA9ments"), g_count);
	ui_text ((long) t, 32, 62, jet (J_TEXTE1), 22, (long) ui.font_b, 0);
	ui_text ((long) TR ("Une vue dessin\xC3\xA9" "e par l'app (ui_view)."), 16, 116, jet (J_TEXTE2), 13, (long) ui.font, 0);
}

static void add_item ()
{
	const char *s = (const char *) ui_w_get_text (g_field);
	if (!s || !*s) return;
	ui_list_add (g_list, (long) s, g_count++);
	ui_w_set_text (g_field, (long) "");
	ui_view_dirty (g_view);
}

// les widgets suivent la taille de la fenetre
static void place (long w, long h)
{
	ui_w_move (g_field, 12, 9, w - CAP - 12 - 130, 30);
	ui_w_move (g_add, w - CAP - 120, 9, 120, 30);
	ui_w_move (g_list, 12, TOPH, 260, h - TOPH - 12);
	ui_w_move (g_view, 284, TOPH, w - 296, h - TOPH - 12);
}
static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	place (w, h);
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	return 0;
}

// un choix dans la barre de menus (ou son raccourci) -> false : quitter
static bool command (long id)
{
	switch (id)
	{
	case M_NEW: ui_w_focus (g_field); break;
	case M_CLEAR: ui_list_clear (g_list); g_count = 0; ui_view_dirty (g_view); break;
	case M_ABOUT: ui_message ((long) TR ("Squelette"), (long) TR ("Une app de Korin OS en C++ sur UIKit."), UM_OK); break;
	}
	return true;
}

int main (int, char **)
{
	if (!uikit_open (&ui)) return 1;	// les kits ouverts, les polices chargees
	char dict[96];				// les textes de l'app dans la langue du systeme (data/lang-<langue>.txt)
	snprintf (dict, sizeof dict, "SD0:/apps/%s.app/lang-%s.txt", APP, (const char *) ui_lang ());
	ui_tr_load ((long) dict);
	s_jet = (const unsigned *) ui_jetons ();
	// au milieu de l'ecran, sous la barre de menus
	long scr[4] = { 1024, 768, 0, 0 };	// largeur, hauteur, reserve en bas, en haut
	ui_screen ((long) scr);
	long w = 640, h = 420;
	if (ui_window_ex ((scr[0] - w) / 2, scr[3] + 40, w, h, (long) TR ("Squelette"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (480, 300);
	ui_on_resize ((long) on_resize, 0);

	g_field = ui_textbox (0, 0, 10, 30, 0);
	ui_textbox_hint (g_field, (long) TR ("Un \xC3\xA9l\xC3\xA9ment"));
	g_add = ui_pushbutton (0, 0, 10, 30, (long) TR ("Ajouter"), UB_DEFAULT);
	g_list = ui_list (0, 0, 10, 10, 0);
	g_view = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_view, (long) view_paint, 0, 0, 0);
	ui_menu ((long) "MFichier\n" "I1\tNouvel \xC3\xA9l\xC3\xA9ment\tCtrl+N\n" "I2\tTout effacer\n"
			"MAide\n" "I3\t\xC3\x80 propos\n");		// (traduit par UIKit : data/lang-en.txt)
	on_resize (ui.w, ui.h);
	ui_w_focus (g_field);

	for (;;)
	{
		long t = ui_event (100);	// attend un evenement (100 ms au plus)
		if (t == WEV_CLOSE) break;	// case de fermeture, ou Quitter du menu au nom de l'app
		if (t == WEV_MENU) { if (!command (ui_ev_field (&ui, EV_CODE))) break; }
		else if (t > 0)
		{
			long code = 0, taken = 0;
			long wid = uikit_w_event (&code, &taken);	// l'evenement va au widget concerne
			if ((wid == g_add && code == UN_ACTIVATE) || (wid == g_field && code == UN_ACTIVATE)) add_item ();
			if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND) && !taken)
			{
				long id = ui_menu_key (0);			// Ctrl+N...
				if (id >= 0 && !command (id)) break;
			}
		}
		else ui_w_idle ();		// (rien pendant 100 ms : les infobulles)
		ui_view_flush ();		// redessine les vues marquees par ui_view_dirty
	}
	return 0;
}
```

## Ce qu'il faut retenir

- **`uikit_open (&ui)`** ouvre UIKit et AppKit et charge les polices ; `ui` reçoit la fenêtre courante (`ui.w`, `ui.h`, `ui.surf`), le dernier événement (`ui.ev`) et les polices (`ui.font`, `ui.font_b`, `ui.font_m`).
- **Les couleurs** viennent de `ui_jetons ()` : `J_FACE` (fond), `J_SURFACE` (champs, documents), `J_TEXTE1`, `J_ACCENT`... Elles suivent le thème choisi dans Réglages.
- **`CAP = FRAME_CAP_W + 8`** : le bouton est placé à gauche des boutons de la fenêtre.
- **La boucle** : `ui_event` attend ; `uikit_w_event` donne l'événement au widget concerné et rend son code (`UN_ACTIVATE` : clic, Entrée) ; `ui_menu_key` traduit une touche en entrée de menu ; `ui_view_flush` redessine les vues marquées.
- **`ui_on_resize`** : la fonction est appelée quand la fenêtre change de taille ; elle replace les widgets.
- **Les textes** sont écrits en français dans le source ; `TR (...)` (`ui_tr`) les traduit par le dictionnaire chargé par `ui_tr_load`. Les menus passés à `ui_menu` sont traduits par UIKit lui-même, ligne par ligne (un libellé et son raccourci sont séparés par une tabulation, dans le dictionnaire aussi).

## app.txt

```
# Manifeste de l'app : lu par la barre de menus, le panneau et PocketQI (docs/ABI.md).
# name : le nom affiche (traduit par lang/en.txt) ; category : la categorie du lanceur.
name     = Squelette
category = Développement
description = Le squelette d'une application de Korin OS en C++ sur UIKit.
keywords = exemple squelette modèle
```

## data/lang-en.txt

```
# Les textes de l'app en anglais (cle = le texte francais du source, une tabulation separe un raccourci) :
# pibuild pose data/lang-en.txt dans /apps/<nom>.app, main.cpp le lit par ui_tr_load.
Squelette = Skeleton
%d éléments = %d items
Une vue dessinée par l'app (ui_view). = A view drawn by the app (ui_view).
Un élément = An item
Ajouter = Add
Une app de Korin OS en C++ sur UIKit. = A Korin OS app in C++ on UIKit.
Fichier = File
Nouvel élément	Ctrl+N = New Item	Ctrl+N
Tout effacer = Clear All
Aide = Help
À propos = About
```

## Construire sur le PC

```sh
cp -r src/apps/help/data/api/skeleton src/apps/skeleton
sh tools/office/build.sh skeleton
```

`tools/help/check.sh` compile aussi le squelette à chaque vérification du guide.

## Pour aller plus loin

- [UIKit](uikit_fr.md) : tous les widgets, les dialogues, les images, plusieurs fenêtres.
- [Créer une application](app_fr.md) : les clés de `app.txt`, `pibuild`.
