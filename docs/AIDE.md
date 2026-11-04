# L'aide de Korin OS

Chaque application peut avoir une aide en Markdown, ouverte par le menu **Aide** de la barre de menus dans le lecteur
d'aide ; le même lecteur montre le guide du développeur (l'API du système en C et C++).

## Les pièces

| Pièce | Où | Rôle |
|---|---|---|
| Fichiers d'aide | `src/apps/<app>/help_fr.md`, `help_en.md`, images dans `help/` (Jet : `src/extra/onyxports/jet`) | posés dans `SD0:/apps/<app>.app/` par `build.ps1` (toutes les apps), `pibuild` (`EXTRA`) |
| Menu Aide | `src/sys/menubar/menubar.asm` (`help_probe`, `help_t`) | le process de la fenêtre active (`WL_PID`, nom dans `SYS_PROCLIST`) donne le dossier de l'app ; si `help_<langue>.md` ou l'autre langue existe, un titre **Aide** suit les menus de l'app (ou ses deux lignes vont en tête de son propre menu Aide / Help) |
| Lecteur | `src/apps/help` (C++, UIKit) : `main.cpp` (fenêtre, mise en page, dessin), `markdown.cpp` (blocs, morceaux de style) | `help` : sommaire (les apps qui ont une aide, le guide) ; `help <fichier.md>` : ce fichier |
| Guide du développeur | `src/apps/help/data/api/<page>_fr.md`, `_en.md` → `SD0:/apps/help.app/api/` | une page par kit, plus `app`, `skeleton`, `service`, `ipc`, `kit`, `libc` ; `skeleton/` et `demokit/` : fichiers à copier |
| Vérification | `tools/help/check.sh` (WSL), `tools/help/extract.py` | compile et lie chaque bloc ` ```c ` / ` ```cpp ` du guide qui a une fonction `main`, avec les options de `pibuild`, et le squelette |

## Le Markdown du lecteur

Titres `#` à `######` (la table des matières prend les trois premiers niveaux), paragraphes, `**gras**`, `*italique*`,
`` `code` ``, listes `-` et `1.` imbriquées de deux espaces, `>` citation, blocs de code (bouton **Copier**), tableaux
simples, `---`, images seules sur leur ligne (PNG, BMP : `ui_image_load`), liens vers une page (relative ; `x_fr.md`
absent : `x_en.md`), une ancre (`#titre` façon GitHub), `help:<app>`, `app:<app>`, `http(s)://` (Jet). Pas de HTML.

## Ajouter l'aide d'une app

1. Écrire `help_fr.md` et `help_en.md` dans le dossier source de l'app ; noms de menus tels que l'app les affiche
   (en anglais : ceux de son `lang/en.txt`).
2. Une capture : `help/main.png`, au plus 860 px de large.
3. Rien à changer dans la barre de menus ni dans le build : le menu Aide apparaît dès que le fichier est sur la carte.

## Vérifier le guide

```sh
sh tools/help/check.sh                  # toutes les pages, et le squelette
sh tools/help/check.sh uikit_fr netkit_en
```

Essais (4/11/2026, QEMU) : menu Aide de Fichiers en français et en anglais, Aide de Fichiers avec son image, sommaire,
guide, page UIKit, bouton Copier ; 99 exemples et le squelette compilés et liés, aucun exécuté.
