---
name: korin-port
description: Porter une application ou une bibliothèque vers Korin OS — règles du projet (apps d'Onyx sans pont et sur UIKit, thème du bureau, nom d'origine gardé, moteur repris et interface réécrite), ports tiers, ce qui manque à ajouter au kit ou à libposix. À charger avant tout portage.
---

# Porter vers Korin

## Les règles

- **Une app d'Onyx :**
  - garde son nom d'origine (letters, sheet, slides, cardfile…) ;
  - appelle **directement** les `ui_*` de UIKit et les autres kits (`ar_*`, `dk_*`…) et POSIX ;
  - **n'embarque ni pont, ni onyxcompat, ni l'uikit C++ d'Onyx, ni `kapi_*`**.
  - Ce qui manque à UIKit s'y ajoute, **en fin de `uikit.abi`** (`docs/UIKIT-MANQUES.md`). Le modèle est
    `src/apps/koton`.
- **Le moteur est repris, l'interface réécrite :** modèle, formats, formules et mise en page viennent de l'original ;
  barres, vues et dialogues sont refaits sur UIKit.
- **Le thème est celui du bureau,** Givre et Onyx Noir compris : les jetons de `ui_tokens()`, jamais de couleur en
  dur, jamais de lecture de `theme.txt`.
- **Un port tiers** (SDL, Avalonia, une bibliothèque C…) peut avoir une couche d'adaptation sur UIKit : la règle
  « sans pont » ne vaut que pour les apps d'Onyx.
- **Ce qui sert à plusieurs programmes** va dans le kit de son domaine.
- **POSIX s'agrandit à la demande :** si le port bute sur une fonction absente, on l'ajoute à `libposix`
  (`src/lib/posix`) avec un essai dans `posixtest`, plutôt que de la contourner dans chaque app.
- **Les noms d'exécutables, de sources et de scripts sont en anglais**, la documentation en français. Chaque app
  portée a un `README.md` : ce qui vient de l'original, ce qui est écrit pour Korin, ce qui manque.

## Méthode

1. **Lire l'original :** son moteur, ses fichiers, ses dépendances. Lister ce qui touche au système : fenêtres, son,
   réseau, fichiers, threads.
2. **Faire correspondre chaque besoin à un kit** (`korin-kit-*`) ou à POSIX (`korin-posix`) ; noter ce qui manque.
3. **Construire le moteur seul**, sans interface, avec `/devtools/gcc/bin/g++`, et le faire tourner sur un essai.
4. **Écrire l'interface sur UIKit**, en partant du squelette (`korin-lang-cpp`) ; ajouter les menus, l'aide
   (`help_fr.md`) et la traduction (`data/lang-en.txt`).
5. **Mettre `app.txt`**, avec les clés de PocketQI (`description`, `keywords`, `llm.*`), et construire avec `pibuild` ou
   `tools/office/build.sh`.
6. **Essayer dans les deux thèmes,** en mode bureau et en mode poche (fenêtres en plein écran adaptatif). Faire des
   captures avec `screenshot`.
7. **Ajouter l'app au paquet** (`korin-dev-packages`), si le dépôt est concerné.

## Pièges fréquents

- **Les chemins** forment un seul arbre Unix (`/home/x`, `/tmp`, `/media/usb0`) : `path[0] == '/'` marche. Seul l'alias
  `HOME:/…` (le dossier personnel) est absolu sans `/` en tête ; `getcwd` et `realpath` rendent `/…`.
- **`system("a | b")`** ne passe pas par un shell (`korin-posix`).
- **Pas de `dlopen` :** les greffons se lient en statique.
- **`connect` bloquant et appels longs :** à mettre dans un thread, pour ne pas figer la fenêtre.
- **Dessin :** une app qui dessinait seule en continu passe par une vue (`ui_view`) et `ui_view_flush`.
