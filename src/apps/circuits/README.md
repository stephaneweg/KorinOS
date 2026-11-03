# circuits : le jeu de portes logiques d'Onyx sur ARMPI

Porté de `$ONYX/user\Apps\circuits`, sans pont : le moteur est celui d'Onyx ; la fenêtre est réécrite sur `ui_*`.
On câble interrupteurs, portes NON, ET, OU, OUX, NON-ET, NON-OU et lampes jusqu'à ce que les lampes suivent la table de vérité :
20 niveaux en trois mondes, d'un seul fil à un additionneur de deux bits.

    circuits                                reprend là où le joueur en était
    circuits SD0:/home/circuits/x.circuits  ouvre un paquet de niveaux (tous ses niveaux ouverts)

Construire : `wsl sh tools/office/build.sh circuits` (croisé), ou sous ARMPI `pibuild app /src/apps/circuits circuits`.
Niveaux : `data/levels/*.circuits`. Dictionnaire : `data/lang/fr.txt`. Progression : `SD0:/home/circuits/progress.ini`.

## Repris tels quels (Onyx)

`circuit.cpp`, `circuit.h` (le plateau, l'évaluation, Vérifier, le texte des circuits, l'annulation, les routes, les paquets,
la progression), `lessons.h`, les trois paquets de niveaux. Seuls changements : les appels `fk_kv_*` sont devenus `kv_*`.

## Réécrits

* `kv.h` : le document `[section]` / `clé = valeur` de FileKit (`kvtext.inc` d'Onyx), en `open` / `read` / `write`. ARMPI n'a pas de
  FileKit et Circuits est le seul à s'en servir : il reste dans l'app.
* `gates.h` : les couleurs viennent des jetons de UIKit ; les symboles des portes par `ui_vp_poly`, `ui_vp_line`, `ui_vp_disc`.
  `ui_text_wrap` n'ayant ni limite de lignes ni centrage, le texte coupé est fait ici, avec `ui_text_prefix`.
* `board.h`, `views.h` : une `ui_view` par vue d'Onyx (niveaux, carte, plateau, message, table, compteur, chaque bouton de palette),
  et une `struct` au lieu d'une classe `Widget`. Le reste est en `ui_pushbutton`, `ui_toolbar`, `ui_label`, `ui_scrollbar`.
* `main.cpp` : les commandes d'Onyx, presque ligne à ligne, la boucle de la recette, `layout ()`.

## Différences avec Onyx

* **Thème** : Onyx garde un plateau vert pâle et des cartes de couleurs fixes dans tous les thèmes. Ici tout suit le bureau : papier
  = `SURFACE`, bandes = `CREUX`, fil à 1 = `SUCCES`, lampe et étoiles = `ALERTE`, erreur = `DANGER`.
* **Leçon et résultat** : des dialogues UIKit (`ui_dialog_*`), pas des cartes par-dessus le plateau. Ils ouvrent après l'événement
  en cours (`g_wantLesson`, `g_wantResult`).
* **Pas d'alpha** dans `ui_vp_*` : fantômes, halo de lampe et sélection sont mélangés à la main avec le fond (`mix`).
* **Palette** : une petite vue par bouton dans la barre, le glisser vers le plateau passe par sa capture (`pal_mouse`).
  Annuler, Rétablir et Supprimer sont des boutons à glyphe. Pas à pas, Au départ et Vérifier sont du texte, sans icône.
* **Petits écrans** : pointeur au clavier et ligne d'aide dès que l'écran fait 800 px de large ou moins (PocketTerm). Fenêtre de moins
  de 720 px : niveaux en tiroir (L ou le bouton ≡), carte réduite au titre, texte du niveau et messages sur le banc. C'est
  une disposition d'ARMPI : la dimension d'Onyx en 640 x 480 prenait tout l'écran, ici le dock reste.
* **Table de vérité** : elle défile si ses lignes ne tiennent pas (4 entrées à 640 x 480) ; la ligne courante reste visible.
* **Touche L** : sans Ctrl + Page Haut ; les touches 1 à 6 se lisent en code de position, pour marcher en AZERTY sans Maj.
* **Pas de manette** : `WEV_PAD` n'est pas branché ; la ligne d'aide nomme les touches du clavier.
* `TRC` : clés `contexte|mot` du dictionnaire, comme Onyx. Les messages à `\n` sont coupés en deux clés, `ui_tr_load` ne les lit pas.
