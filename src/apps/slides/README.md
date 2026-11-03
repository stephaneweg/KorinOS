# Slides

Le programme de présentation d'Onyx (`user/Apps/slides`) sur l'UIKit d'ARMPI, sans pont : les vues sont des `ui_view`, les
contrôles des poignées UIKit, l'XML passe par DocumentKit (`dk_xml_*`), le ZIP par ArchiveKit (`ar_*`), appels directs.
`.odp` et `.pptx`, lus et écrits. Compile avec le GCC natif : `pibuild app /src/apps/slides slides`.

    slides SD0:/home/documents/a.pptx

## Repris d'Onyx

| Fichier | État |
|---|---|
| `model.h`, `master.h`, `find.h` | tels quels, sauf le décodage des images |
| `text.h` | mise en page reprise ; mesures et dessin sur les polices de UIKit |
| `odp.h`, `pptx.h` | tels quels ; lecteur XML et ZIP remplacés par des appels aux kits (`core.h`) ; `deck_sane` ajouté (valeurs d'un fichier abîmé) |
| `editor.h` | tel quel, sauf le presse-papiers (`ui_clip_*`) |
| `render.h` | calques, formes, tableaux, graphiques repris ; `gpucomp` et `vpaint` remplacés par 200 lignes de C++ (ci-dessous) |
| `show.h` | effets, transitions, console du présentateur repris ; plein écran par `ui_dialog_at` |
| `view.h` | la souris, les poignées, les guides, le texte sur la diapo repris, sur une `ui_view` |
| `panes.h`, `sidebar.h`, `main.cpp` | réécrits sur `ui_view` et les contrôles ; la logique des commandes reste celle d'Onyx ligne à ligne |

## Écrit ici

* **Compositeur logiciel** (`render.h`) : calques prémultipliés gardés en cache, déplacés, tournés, mis à l'échelle, fondus,
  découpés ; déplacer un objet ou jouer un effet ne redessine rien.
* **Remplissage de chemins** (`VPath`) : couverture à aire signée, comme `ttf.asm`.
* **Texte dans un calque** : `ui_text` peint en noir sur blanc dans un canevas caché (le masque), la couverture est mélangée
  au calque. Le texte est placé mot par mot là où la mise en page le met.
  **Conséquence** : un canevas et une vue sont une seule cible à la fois dans UIKit ; tout ce qui rend du texte se fait hors
  du dessin d'une vue : `prepare ()` dans la boucle principale, avant `ui_view_flush` ; les fonctions de dessin des vues ne
  font que recopier et poser des contrôles.
* **JPEG** : `jpeg.c`, `stb_image.h` (celui d'Onyx, partie JPEG seulement), UIKit ne lit que BMP et PNG.
* **PNG et PDF** : `png_encode` (`core.h`), `export_pdf` (`main.cpp`) sur le deflate d'ArchiveKit.

Ajouté à UIKit : `ui_dialog_at (x, y)` (181), le prochain dialogue sans cadre à ce point, devant tout : le plein écran.

## Différences avec Onyx

* **Polices** : ARMPI a DejaVu Sans, Serif et Mono. Le nom de police du document est gardé et réécrit ; il choisit la plus
  proche des trois. DejaVu est plus large que Liberation Sans : une ligne peut se couper ailleurs. Pas de crénage entre
  mots, pas de tailles fractionnaires (les pixels de `ui_text` sont entiers), plus de 160 px : ramené à 160.
* **PDF** : une image par page (1280 px, deflate). Pas de pages de notes, pas de documents à distribuer, pas de polices
  embarquées, texte non sélectionnable.
* **Impression** : Fichier > Imprimer, par PrinterKit (docs/IMPRESSION.md) : une image de 2400 px par diapositive.
* **Images** : PNG, JPEG, BMP. GIF, WebP, PCX : un cadre de remplacement à l'écran, le fichier est gardé et réécrit.
  PNG entrelacé : idem (limite de `ui_image_decode`). Coller une image du presse-papiers : absent.
* **Barres** : glyphes DejaVu au lieu des icônes vectorielles d'Onyx (`ui_toolbar`). Pas de bouton à flèche : la galerie de
  mises en page, le type de graphique, passent par un menu ou un dialogue.
* **Couleurs** : `ui_colorpicker` (palette 8 × 5) : les dix couleurs du thème n'y sont pas ; une couleur égale à une couleur
  du thème reste celle du thème. « Automatique » et « sans couleur » absents du sélecteur (le type de remplissage ou de
  trait le permet).
* **Rechercher** : modal, pas de dialogue flottant : à chaque résultat il se ferme, la diapo se montre, il se rouvre.
* **Champ numérique du panneau** : validé par Entrée ou les boutons − et +, pas en quittant le champ ; sur une demi-ligne,
  sans boutons.
* **PocketTerm / manette** : le mode de poche d'Onyx (tiroirs, objets au clavier, indications) n'est pas repris ; à 800x600
  les colonnes rétrécissent, les notes se cachent (Affichage > Notes), le titre de la barre de menus se réduit.
* **Lecture XML** : `dk_xml_attr` sans préfixe prend aussi un attribut préfixé, le premier de la balise : `r:id` placé
  avant `id` dans un `p:sldId` serait mal lu (l'`attr_np` d'Onyx était exact). Aucun fichier connu n'est dans ce cas.

## Fichiers

`SD0:/home/documents` par défaut ; `SD0:/home/slides/recovered.odp` : le document gardé à la fermeture avec des changements
non enregistrés, proposé au lancement suivant. `data/` : `cafe-2026.pptx`, `cafe-2026.odp`, les exemples d'Onyx.
