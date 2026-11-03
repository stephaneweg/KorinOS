# Turtle Quest

Le jeu d'Onyx qui apprend à programmer : on écrit du BASIC (Onyx BASIC, avec les mots de la tortue) pour mener une
tortue à son drapeau, ramasser pièces et gemmes, ouvrir des portes, prendre des portails, peindre des cases et
dessiner des figures. 5 recueils, 48 niveaux, en français ou en anglais selon `ui_lang()`.

Touches : F5 lancer, F8 pas à pas, F7 arrêter, F9 au départ, F1 leçon, F2 indice, Ctrl+N / Ctrl+P niveau suivant /
précédent, Ctrl+O ouvrir un recueil, Ctrl+E éditeur de niveaux, Échap ferme la leçon ou arrête.

## Fichiers

| Où | Quoi |
|----|------|
| `SD0:/apps/turtle.app/*.turtle` | les recueils (`data/*.turtle`, à plat : pibuild copie `data/*` à plat, build.sh aussi) |
| `SD0:/home/turtle/progress.ini` | les joueurs : étoiles, programmes, records, leçons vues |
| `SD0:/home/turtle/my-levels.turtle` | les niveaux du joueur (l'éditeur) |
| `SD0:/home/turtle/check.txt` | le verdict de `main --check` |

## Repris d'Onyx

* `world.h`, le moteur, presque mot pour mot : niveaux, recueils, monde, comptage, enregistrement de la partie par un
  `bas::Host`, règles de l'éditeur. Seuls changent le chemin de `bas.h` et des commentaires.
* `data/*.turtle`, tels quels.
* `core.cpp` : le compilateur et la VM de `src/lib/basic`, comme QBasic. La partie tourne sur la VM (`managed`), pour le
  crochet d'instruction.

## Réécrit

`main.cpp` et `ui.h`, sur UIKit, sans pont : appels `ui_*` directs.

* Contrôles de UIKit : boutons, glissière, liste déroulante, champs, `ui_textarea` (`UTA_CODE | UTA_BASIC`, mots-clés
  par `bas::wordList`), `ui_panel` pour l'éditeur, menus texte (`ui_menu`), `ui_ask`, `ui_file_open`.
* Dessinés à la main, une `ui_view` chacun : liste des niveaux, plateau, mots cliquables, compte d'instructions,
  carte du niveau, message, leçon, outils de l'éditeur.
* Couleurs : les surfaces, textes, traits et accents viennent de `ui_jetons`. Le sol et les murs sont les couleurs du
  jeu mêlées à la surface du thème (sombres en Onyx Noir). La page de dessin est la surface du thème, et un crayon qui
  s'y confondrait est éclairci. Restent fixes les objets du jeu : pièce, gemmes, portails, tortue, 16 couleurs du crayon.
* Pas d'alpha dans `ui_vp_*` : les anneaux sont tracés en segments, les halos sont des mélanges de couleurs.
* Ajout à Onyx : les recueils sont aussi dans le menu Niveaux, pour les fenêtres trop étroites pour la liste.
* `main --check` remplace le test sur PC d'Onyx : chaque niveau résolu par sa solution, dans les deux langues.

## Manque

* Le mode Pocket d'Onyx (tiroir des niveaux, éditeur en trois étapes, ligne d'aide) : pas porté. Sous 760 px de large,
  la liste disparaît, et on choisit par le menu Niveaux et Ctrl+N / Ctrl+P.
* Pas de marque d'erreur dans la marge : `ui_textarea_highlight` n'a qu'une ligne éclairée, l'erreur est donc éclairée.
* Pendant une partie, la ligne suivie est montrée par `ui_textarea_goto`, qui déplace le curseur ; il revient à sa
  place à la fin.
* Pas d'icône : `icon.bmp` d'Onyx n'est pas repris.
* Onyx n'a pas de son dans ce jeu : rien à porter.
* Sous environ 580 px de haut, le bas des outils de l'éditeur est coupé (Tester, Enregistrer, Fermer restent en haut).

## Essayé sous QEMU (captures vues)

Givre et Onyx Noir : ouverture, leçon, niveau 1 tapé et gagné à 3 étoiles, Niveau suivant, erreur à l'exécution (Bong),
coloration, menu Joueur (Noir), indice et bouton Leçon (Noir), plateau du Grand final et ses portails, gagné à 3 étoiles
(Noir), pas à pas et ligne éclairée (Givre), une figure en couleurs tracée en cours de partie (Givre), figure cible de Sierpinski (Noir), palette de la leçon (Givre).
Éditeur : ouvert en Noir ; en Givre, pièce posée, Tester, Enregistrer, Fermer, « Mes niveaux » dans la liste des recueils.
Anglais : une partie avec erreur. Fenêtre réduite à 800 x 486 (Noir), avant le dernier réglage de la largeur des boutons, non revu depuis. `main --check` : 48 niveaux, 0 échec, 48 à 3 étoiles.

## Pas essayé

* Le vrai Pi, le Pocket et toute largeur sous 760 px.
* `pibuild app /src/apps/turtle turtle` sur l'appareil : seul `tools/office/build.sh` a compilé (mêmes options).
* Nouveau joueur, Ouvrir un recueil, le glisser-déposer d'un `.turtle`, l'ouverture par argument, le glisser de cases et
  les boutons de taille dans l'éditeur, les outils Gemme et Portail, le refus d'une carte fausse, la barre de
  défilement de la liste.
