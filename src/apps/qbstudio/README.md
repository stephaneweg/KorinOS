# QBStudio

L'IDE des applications de bureau en BASIC d'Onyx, porté sur ARMPI : une fenêtre dessinée (`.form`), son code (`.bas`), Exécuter
(F5), Créer l'application. Un projet est un dossier de `SD0:/home/projects/<nom>/` avec son `project.ini` ; deux exemples sont
posés sur la carte (`skel/sd0/home/projects/converter`, `pages`). Aide dans l'application : `data/help.txt`.

## Repris d'Onyx

- `form.h` (lecture, écriture et mise en page des `.form`) et `gen.h` (le code généré, la bibliothèque des contrôles) : tels
  quels, sauf un `#include <strings.h>`.
- Les règles du concepteur (`designer.h`) : créer, nommer, chercher un élément, trouver où il tombe, les poignées, la grille,
  la boîte à outils. Celles des propriétés (`props.h`) : les lignes de chaque genre, les événements.
- Dans `main.cpp` : la logique du projet (documents, `project.ini`, modèles de projets, `build`, complétion, plan, problèmes,
  listes d'objet et d'événement).
- Les projets d'exemple. `category` y vaut `Développement`.

## Réécrit sur UIKit

Aucune classe autour d'un widget, aucun pont : des appels `ui_*` directs. Les dessins prennent les jetons `TJ_*` (`app.h`).

- Le concepteur est une `ui_view`. Dans Onyx, c'étaient les vrais widgets sous une couche transparente ; UIKit n'a pas de
  couche transparente sur des enfants, donc les contrôles sont **redessinés à la main** aux jetons, au plus près de ce que UIKit
  montrera. Un bouton `default` n'y est pas accentué, parce que le code généré ne l'accentue pas non plus.
- La boîte à outils et la grille des propriétés sont des `ui_view` ; un texte s'y tape dans une `ui_textbox` enfant.
- Les onglets (`ui_tabstrip`), le projet (`ui_tree`), le plan et les problèmes (`ui_list`), la barre entre dessin et texte
  (`ui_splitter`), les vues (`ui_segmented`), la barre d'outils (`ui_toolbar`), les menus (`ui_menu`), les dialogues.
- L'éditeur : un `ui_textarea` par document. Le concepteur réécrit le texte par `UTE_ALL` puis `ui_textarea_insert`, ce qui fait
  **deux** opérations au journal de UIKit (tout effacé, puis écrit) ; `form_whole` (`main.cpp`) fait un pas de plus quand Annuler
  ou Rétablir tombe sur le texte vide, pour que l'utilisateur ne voie qu'une opération.
- Exécuter : `SD0:/bin/basic -i 2 -d <dossier> /tmp/<nom>.bas`. C'est le **descripteur 2**, pas 3 : le fils ne reçoit un tube
  de vfs que sur 0, 1, 2 (`adddup2` de libposix, voir `src/lib/posix/proc.c`).
- Créer l'application : toujours autonome (`SD0:/bin/basic` + `bas::attachBax`), avec `app.txt` et l'`icon.bmp` du projet s'il
  en a une. Le texte des éditeurs est en UTF-8, BASIC parle Latin-1 : la conversion se fait en un seul point (`latin1_dup`).
- Les réglages et le dernier projet sont dans `SD0:/home/projects/qbstudio.ini` (Onyx : dans le dossier de l'application).

## Ce qui manque

- **Le débogueur** : Onyx ne l'a pas non plus (il n'existe que dans l'étude).
- **Le mode de poche** (écran de moins de 800 px, tiroirs, clavier seul avec `objnav`) : pas porté. La fenêtre demande au moins
  760 x 480.
- La complétion est un menu (`ui_popup_local`), ou un dialogue à liste au-delà de 22 noms, et non une liste qui suit la
  frappe. Le menu se pose **en haut de l'éditeur**, pas au curseur : UIKit ne dit pas où est le curseur à l'écran quand le texte
  défile. Ctrl+J remplace Ctrl+Espace.
- Les mots de BASIC ne sont pas remis en capitales à la frappe. Les guides d'indentation et le rouge sous les lignes fautives
  non plus : seule la première erreur est éclairée (`ui_textarea_highlight`).
- Le texte d'un `.form` est coloré comme du BASIC.
- Le choix `compiled` / `standalone` de Make App n'existe plus : une application d'ARMPI est un exécutable.
- Le mode `-bench` et la mesure `alwaysFull` du concepteur ne sont pas repris : il n'y a plus de widgets à refaire.

## Défauts connus hors de ce dossier

- **Une application faite par Make App ne démarre pas depuis le menu** : le fichier `main` est correct (runtime, programme et
  fin `OBAXAPP1` vérifiés sur la carte) et l'application est listée, mais le journal dit `usage: basic ...`. `basic` cherche
  son programme attaché par `__posix_exe_path ()`, déduit d'`argv[0]` ; l'hypothèse, non vérifiée, est que le lanceur ne donne
  pas le chemin complet dans `argv[0]`.
- **En thème sombre, la fenêtre d'un programme lancé a un fond clair** autour de ses contrôles, qui sont sombres : `/bin/basic`
  ne remplit pas la fenêtre de `TJ_FACE` à sa création (`uk_window` de `src/apps/basic/baskits.h`). Après un redimensionnement,
  le fond est juste.
- **QBasic garde `-i 3`** (`src/apps/qbasic/main.cpp`) : ses erreurs d'exécution se perdent, pour la même raison qu'ici.

## Essayé

Sous QEMU seulement, jamais sur le Pi. Vu sur capture, dans le thème indiqué :

- Givre et Onyx Noir : ouverture de converter, concepteur, boîte à outils, propriétés, texte, code.
- Givre : déplacer un contrôle, en ajouter un depuis la boîte à outils, changer une propriété ; Supprimer puis Ctrl+Z dans le
  concepteur ; erreur de compilation montrée dans son fichier ; erreur d'exécution ramenée à sa ligne ; dialogue Nouveau
  projet ; agrandir ; français.
- Onyx Noir : menu de complétion après un point ; onglet Événements ; `pages` lancé (contrôles utilisateur dans leurs Host).
- Les deux : F5, la fenêtre du convertisseur s'ouvre, Convert répond (68, glissière, barre d'état).

## Pas essayé

Couper, copier et coller des éléments ; Ajouter un module, un contrôle utilisateur ; créer réellement un nouveau projet (le
dialogue seulement) ; Réglages ; Rechercher ; l'aide ; choisir une entrée dans le dialogue de complétion d'un kit ; le glisser de
la barre entre dessin et texte ; les barres de défilement du concepteur et des propriétés ; fermer un onglet ; la compilation
par `pibuild app /src/apps/qbstudio qbstudio` dans l'OS (seule la chaîne croisée de `tools/office/build.sh` a servi).
