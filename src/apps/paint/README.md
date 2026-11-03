# Dessin : le Paint d'Onyx sur ARMPI

Porté de `$ONYX/user\Apps\paint`, sans pont : le document, les outils et les formats sont ceux d'Onyx ; la fenêtre est
réécrite sur `ui_*`. Il remplace le Dessin en assembleur (`paint.asm`, retiré). Programme `paint`, `SD0:/apps/paint.app`, nom
affiché Dessin, tiroir Création. Une image de calques transparents : ruban, barre d'options de l'outil, canevas, panneau des
calques, barre d'état.

    sh tools/office/build.sh paint       # (WSL) -> build/office/sdroot/apps/paint.app/{main,app.txt,lang/}, hors du selfhost
    paint [image]                        # une image à ouvrir, sinon la reprise après une fermeture sans enregistrer

Fichiers : `SD0:/home/paint/recovered.ora` (la reprise), `SD0:/home/paint/gradients/*.ggr` (vos dégradés).

## Repris d'Onyx

`pdoc.h` (calques, modes de mélange, annulation par tuiles), `psel.h`, `pbrush.h`, `pgrad.h`, `padjust.h`, `raster.h`, la logique
de `pview.h`, `pfile.h` (OpenRaster), `picons.h` (la géométrie des icônes), `image.cpp` (`imgload.hpp` : PCX) et `pngsave.hpp`
tels quels ou presque (`kapi_*` remplacés par POSIX). `stb_image.h` (domaine public), `simplewebp.h` (BSD-3).

Lit ORA, PNG, JPEG, BMP, GIF (première image), WebP, PCX. Enregistre ORA ; exporte PNG, JPEG, BMP, GIF.

## Réécrit

* `plook.h` : jetons du thème, texte, un chemin de formes pour les icônes (`ui_vp_*` n'a ni trou ni alpha), le menu surgissant.
* `pui.h` : une `ui_view` par partie (ruban, options, calques, état). Dialogues : `ui_dialog_*` ; la galerie des pinceaux et
  l'éditeur de dégradés sont une `ui_view` dans un dialogue. Les popups et dialogues partent de la boucle (`later`), pas du rappel
  de souris.
* `ptext.h` : UIKit n'a pas FreeType ; chaque ligne est dessinée en blanc sur noir dans un `ui_canvas` caché, dont les pixels donnent
  la couverture.

## Différences avec Onyx

* **Composition par le processeur** : `gpucomp` (V3D) n'est pas porté. Le canevas garde une image composée, refaite sur les
  rectangles changés ; plus proche voisin dès 100 %, bilinéaire en dessous. Les modes de mélange sont ceux de `gpucomp`, écrits dans
  `pdoc.h`.
* **Thème** : tout suit les jetons de UIKit (bureau autour de l'image = `CREUX`) ; le damier, le papier et les couleurs de l'image non.
* **Toucher, stylet** : le bouton gauche dessine avec la couleur choisie (1 ou 2 : la seule qu'a un doigt), le droit avec l'autre.
  La loupe déplace l'image au glisser. Poignées de dégradé et barres de défilement plus grandes.
* **Petite fenêtre** (moins de 1090 px, PocketTerm) : ruban compact sans noms de groupes, formes en menu, barre d'options qui
  coulisse (›, molette) ; panneau des calques masqué sous 960 px (bouton dans la barre d'état).
* **Pas de mode de poche d'Onyx** (tâches à la croix directionnelle sans pointeur) : le PocketTerm a son écran tactile.
* **Trois familles de polices** (DejaVu Sans, Serif, Mono) : celles de UIKit.
* **Impression** : Fichier > Imprimer, par PrinterKit (docs/IMPRESSION.md) : l'image aplatie, à sa taille ou réduite, centrée.
* **Presse-papiers** : `ui_clip_set_image` n'a pas d'alpha. Le collage reprend l'alpha des pixels copiés ici tant que l'image du
  presse-papiers est la leur ; une image venue d'ailleurs est opaque. Un fichier image copié dans Fichiers se colle aussi.
* **`opens = ora`** seulement : les images continuent de s'ouvrir dans leur visionneuse, comme dans Onyx.
* Thème et polices : pas d'entrées grisées ni de raccourcis dans les menus surgissants (`ui_popup_local`) : une entrée impossible
  n'y figure pas.
