# Créer une application

Une application de Korin OS est un dossier `SD0:/apps/<nom>.app` : son exécutable `main`, son manifeste `app.txt`, et ce dont elle a besoin (données, traductions, aide). Elle s'écrit en C ou en C++, se compile avec GCC dans Korin OS lui-même ou sur un PC, et passe par les **kits** du système pour tout ce qui touche à l'écran, au son ou au réseau.

## Les outils

| Où | Quoi | Commande |
|---|---|---|
| Korin OS | GCC 14.2, G++, binutils, GNU make | `/gcc/bin/gcc`, `/gcc/bin/g++`, `/gcc/bin/make` |
| Korin OS | construire et installer un dossier | `pibuild [app\|bin\|sys\|svc] dossier nom` |
| Korin OS | la même chose dans une fenêtre | l'app **PiBuild** |
| PC (WSL) | la chaîne croisée `aarch64-onyx-elf-14.2` | `sh tools/office/build.sh <nom>` |

Sous Korin OS, `/gcc` est un chemin virtuel (le dossier `SD0:/gcc`) ; un programme compilé est un ELF statique, lié à la libc (newlib), à la couche POSIX (`libposix`) et à `libstdc++`. Les kits viennent avec : `/gcc/lib/lib<kit>.a` contient les relais de chaque kit, `/gcc/include/kits` leurs en-têtes, et l'éditeur de liens ne garde que ce que le programme appelle.

## Le dossier d'une app

```
SD0:/home/projets/bonjour/      les sources (le nom du dossier donne celui de l'app)
  main.cpp                    ou main.c ; tous les .c et .cpp du dossier et de ses sous-dossiers sont compilés
  app.txt                     le manifeste
  data/                       copié tel quel dans le dossier de l'app (jamais compilé)
    lang-en.txt               les textes de l'app en anglais
  help_fr.md, help_en.md      l'aide (menu Aide de la barre de menus)
  help/                       ses images
```

Une fois construite, l'app est dans `/apps/bonjour.app/` : `main`, `app.txt`, le contenu de `data/`, `help_*.md` et `help/`. Le menu Korin OS, le lanceur du panneau et PocketQI la trouvent d'eux-mêmes.

## Le manifeste : app.txt

Des lignes `clé = valeur`, `#` pour un commentaire (le `#` est interdit dans les valeurs).

```
# Manifeste de Bonjour
name        = Bonjour
category    = Développement
opens       = txt
description = Une app d'exemple qui dit bonjour.
keywords    = exemple bonjour salut
help        = Le bouton Bonjour affiche un message.
```

| Clé | Rôle |
|---|---|
| `name` | le nom affiché (menu Korin OS, lanceur, barre de menus), traduit par le dictionnaire du système |
| `category` | la catégorie du lanceur et du menu Korin OS (Accessoires, Bureautique, Création, Développement, Internet, Jeux, Multimédia, Système) ; sans elle : Autres |
| `opens` | les extensions qu'elle ouvre (Fichiers lui passe le chemin en argument) |
| `description`, `keywords`, `help` | ce que PocketQI sait de l'app : une phrase, des mots-clés, des lignes d'aide (plusieurs `help`) |
| `llm.<action>`, `run.<action>` | une action que PocketQI peut lancer : sa description, puis les arguments donnés à l'app |

L'icône de l'app est `icon.rgba` dans son dossier (rendue par `tools/icons` pour les apps du système) ; sans icône, le lanceur en dessine une.

## Le plus petit programme

Un programme en ligne de commande, lancé depuis le Terminal :

```c
#include <stdio.h>

int main (int argc, char **argv)
{
	printf ("Bonjour de Korin OS ! (%d argument%s)\n", argc - 1, argc == 2 ? "" : "s");
	for (int i = 1; i < argc; i++)
		printf ("  %s\n", argv[i]);
	return 0;
}
```

```sh
/gcc/bin/gcc bonjour.c -o SD0:/home/bonjour
SD0:/home/bonjour un deux
```

## La plus petite app graphique

Une fenêtre aux couleurs du thème, un bouton, un message. Elle laisse libre le coin en haut à droite, où sont les boutons de la fenêtre (`FRAME_CAP_W`) : Korin OS n'a pas de barre de titre, les 32 pixels du haut servent à déplacer la fenêtre.

```c
#include <uikit.h>

static struct ui_vars ui;

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	const unsigned *jet = (const unsigned *) ui_jetons ();		// les couleurs du thème
	ui_window (360, 160, (long) "Bonjour");
	ui_fill (0, 0, ui.w, ui.h, jet[0]);				// TJ_FACE : le fond des fenêtres
	ui_text ((long) "Une app de Korin OS.", 20, 70, jet[7], 15, (long) ui.font, 0);	// TJ_TEXTE1
	long b = ui_pushbutton (20, 100, 140, 32, (long) "Bonjour", UB_DEFAULT);
	ui_w_draw (0);
	ui_update_all ();
	for (;;)
	{
		long t = ui_event (100);
		if (t == WEV_CLOSE)
			break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			if (uikit_w_event (&code, &taken) == b && code == UN_ACTIVATE)
				ui_message ((long) "Bonjour", (long) "Bonjour, le monde !", UM_OK);
		}
	}
	return 0;
}
```

Pour une vraie app (fenêtre redimensionnable, menus, traductions, vue dessinée), partir du [squelette](skeleton_fr.md).

## Construire sous Korin OS

Dans le Terminal :

```sh
pibuild app SD0:/home/projets/bonjour bonjour
```

`pibuild` regarde le dossier : `main.cpp`, `main.c`, `bonjour.cpp` ou `bonjour.c` doit exister. Il compile tout avec `-O2`, lie avec tous les kits, installe dans `/apps/bonjour.app/main` avec `app.txt`, `data/`, `help_*.md` et `help/`, puis l'app se lance par son nom dans le lanceur. Les objets restent dans `/tmp/pibuild/bonjour` : la fois suivante, seul ce qui a changé est recompilé, en-têtes compris. L'app **PiBuild** fait la même chose dans une fenêtre et montre la sortie du compilateur.

| Type | Installé dans |
|---|---|
| `app` | `/apps/<nom>.app/main` (le défaut) |
| `bin` | `/bin/<nom>`, une commande du Terminal |
| `sys` | `SD0:/sys/<nom>`, un programme du système (droits du système : un [service](service_fr.md)) |
| `svc` | `SD0:/sys/svc/<nom>`, un service lancé par init |

> **Remarque** : sous QEMU, la première compilation C++ prend une vingtaine de secondes (`cc1plus` lu sur la carte) ; sur le Pi, `hello.cpp` se compile en 9 s. `SD0:/etc/preload.txt` peut garder les outils en mémoire.

## Construire sur le PC

Plus rapide pour une grosse app : la même chaîne, dans WSL, depuis la racine du dépôt.

```sh
sh tools/gcc/kits.sh
cp -r ~/bonjour src/apps/bonjour
sh tools/office/build.sh bonjour
```

Le résultat va dans `build/office/sdroot/apps/bonjour.app/`, que `build.ps1` met sur la carte. Mêmes options que `pibuild` : ce qui se compile sur le PC se compile dans Korin OS.

## Les règles d'une app de Korin OS

- **L'interface passe par UIKit** (`ui_*`) : fenêtres, widgets, dialogues, menus. Pas de boîte à outils embarquée : UIKit suit le thème et la taille d'écran choisis.
- **Les couleurs viennent du thème** : `ui_jetons ()` rend les jetons `TJ_*` (fond, surface, texte, accent...), qui changent avec le thème du bureau (Givre, Onyx Noir). Jamais de couleur écrite en dur pour l'interface.
- **Le coin en haut à droite reste libre** sur `FRAME_CAP_W` pixels : les boutons de la fenêtre y sont dessinés par-dessus le contenu.
- **Les menus vont dans la barre de menus** (`ui_menu`) ; Quitter, dans le menu au nom de l'app, arrive comme `WEV_CLOSE`.
- **Les textes se traduisent** : `ui_tr ("texte français")` rend la traduction dans la langue du système ; le dictionnaire de l'app (`data/lang-en.txt`) se charge par `ui_tr_load`.
- **Une app a son aide** : `help_fr.md` et `help_en.md` ([le format](index_fr.md#aide-dune-application)).

## Voir aussi

- [Le squelette d'app en C++](skeleton_fr.md)
- [UIKit](uikit_fr.md), [la libc et la couche POSIX](libc_fr.md)
- [Écrire un service](service_fr.md)
