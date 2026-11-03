# sheet (nom affiché : Spreadsheet) : le tableur d'Onyx sur ARMPI

Porté de `$ONYX/user\Apps\sheet`, sans pont : le moteur est celui d'Onyx ; la fenêtre est réécrite sur
`ui_*`, le XML sur DocumentKit (`dk_xml_*`), le ZIP sur ArchiveKit (`ar_*`), appels directs.

    sheet SD0:/home/documents/x.xlsx     ouvre un fichier (.xlsx, .ods, .csv, .tsv, .txt)
    sheet                                reprend ce qui a été fermé sans enregistrer, sinon le dernier fichier

Construire : `wsl sh tools/office/build.sh sheet` (croisé), ou sous ARMPI `pibuild app /src/apps/sheet sheet`.
Exemples dans `data/` : `cafe-2026.xlsx` (celui d'Onyx), `budget.ods` (écrit à la main : pas de LibreOffice ici ; deux feuilles, formules, styles).

## Repris tels quels (Onyx)

`core.h` (autonome, sans interface : `../slides` s'en sert), `book.h`, `formula.h`, `eval.h`, `funcs.h`, `fn_core.h`, `fn_more.h`, `numfmt.h`,
`input.h`, `condfmt.h`, `ops.h`, `undo.h`. Les 237 fonctions, les formats de nombres, le tri, le filtre, l'annulation.

## Repris et branchés sur les kits

`xlsx.h`, `ods.h` : mêmes lecteurs et écrivain, ligne pour ligne. `xml.h` et le ZIP de `pngsave` ont disparu : le lecteur est une poignée DocumentKit
(`XmlIn` la ferme à la fin de son bloc ; `xattr`, `xtext` ne font que la verser dans un `Buf`), l'archive une poignée ArchiveKit. Fichiers par `open` / `read` / `write`.

## Réécrits

* `render.h` : traits et fonds directement dans les pixels de la vue ; texte par `ui_text` dans `ui_clip`. Polices : le nom du fichier est gardé à
  l'enregistrement et mappé sur DejaVu sans, serif ou mono (`family_for`) ; sans et serif réduits d'un huitième, DejaVu étant plus large que Calibri ou Arial.
* `chart.h` : le même calcul ; les formes par `ui_vp_*`. Pas d'alpha dans `ui_vp_poly` : l'aire est mélangée au blanc.
* `grid.h` : la grille est **une** `ui_view` (en-têtes, sélection, poignée, colonnes redimensionnées, graphiques, éditeur en place, liste de fonctions), deux
  `ui_scrollbar` enfants, aucune classe, aucun widget par cellule. Les cellules restent dans une copie (`g_layer`) : un déplacement du curseur ne redessine
  aucun texte, seulement la sélection et les en-têtes.
* `bars.h` : barres (`ui_toolbar`), barre de formule (`ui_textbox` pour le nom et pour la ligne), onglets (`ui_tabstrip`), état (une petite vue).
* `dialogs.h` : `ui_dialog_*`, un état statique et une fonction par dialogue ; `ui_tabs` pour Format Cells.
* `main.cpp` : les commandes d'Onyx, presque ligne à ligne ; la boucle de la recette. Ce qui ouvre une boîte depuis un événement attend la fin de `ui_w_event` (`pending`).

`ui_base.h` a disparu.

## Différences avec Onyx, stubs

* **Impression** : Fichier > Imprimer, par PrinterKit (docs/IMPRESSION.md) : chaque page dessinée à deux fois sa taille dans un canevas caché, en image (pas de texte sélectionnable comme chez Onyx, les cellules se dessinent ici par `ui_text`). **Export PDF** : le message renvoie à l'imprimante PDF.
* La feuille reste blanche avec ses encres dans tous les thèmes, comme une page ; ce qui l'entoure (en-têtes, barres, dialogues) suit les jetons.
* Icônes : des glyphes DejaVu (`ui_toolbar_add`), pas les icônes vectorielles d'Onyx. Une barre qui ne tient pas : les boutons de droite se cachent, tout reste dans les menus (Onyx : un « » »).
* Boutons doubles (flèche à côté) : couleurs par `ui_colorpicker` (Automatique et Sans remplissage dans le menu Format et dans Format Cells) ; Monnaie et Bordures ouvrent toujours leur menu.
* Ligne de formule : `ui_textbox`, donc sans les références en couleur ; en cellule, elles restent colorées. La liste de fonctions n'apparaît qu'en cellule.
* Choix des polices : une liste de noms courants, le nom du document ajouté ; pas de famille réelle au-delà de sans, serif, mono ; pas d'aperçu de la police dans la liste.
* Onglets : largeur fixe de `ui_tabstrip`, pas de couleur d'onglet, pas de flèches de défilement, pas de renommage par double-clic (clic droit ou menu Sheet) ; beaucoup de feuilles débordent.
* Graphique : type choisi dans une liste, pas de boutons illustrés. Filtre automatique : dialogue centré, pas de liste accrochée à l'en-tête.
* `.ods` lu, jamais écrit, comme Onyx (`Save` propose le `.xlsx`).
