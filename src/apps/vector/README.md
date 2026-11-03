# Vector

Éditeur de dessin vectoriel d'ARMPI, écrit pour lui (pas un port). SVG natif. Plan : `docs/PLAN-VECTOR-NATIVE.md`. Interface : `docs/UX-VECTOR.md`, revue : `docs/UX-VECTOR-REVUE.md`.
Construire : `sh tools/office/build.sh vector` dans WSL (Skia et HarfBuzz viennent du sysroot de `tools/webkit/build-deps.sh`). Hors du selfhost.

    vector [a.svg]
    vector --render a.svg b.png [zoom]
    vector --roundtrip a.svg [b.svg]
    vector --bench N [sortie.txt]
    vector --fuzz a.svg N              annulation au hasard : tout annuler rend le fichier à l'octet
    vector --batch liste rapport       tools/vector/suite.py : la suite de tests de resvg
    vector --export a.svg b fmt        svg (simple), eps, dxf, plt, pdf
    vector --delay S --render ...      attend S secondes : sous QEMU, vfs est pris par les préchargements du démarrage

Essais : `tools/vector/suite.py`, `tools/vector/montage.py`, `tools/vector/geomtest.cpp` (sur le PC).

## État, QEMU seulement

**Rien n'a tourné sur le Pi** : ni la vitesse, ni le tactile.

| Partie | État |
|---|---|
| Moteur, dégradés, maillages, motifs, découpes, masques, marqueurs, `use`, images, texte HarfBuzz, filtres, tuiles en threads | essayés ; dernier score mesuré à la suite resvg : 1 055 / 1 618 sans le texte, texte seul 166 / 355 ; les corrections qui ont suivi n'ont pas été remesurées |
| Journal | `--fuzz` : 3 000 transactions, exact à l'octet |
| Formes, Stylo, Crayon, Nœuds, Texte, Dégradé, Calligraphie, Seau | essayés à l'écran |
| Sélection, groupes, copier-coller, Union, Constructeur, Aligner, annulation | essayés à l'écran |
| Grille, règles, guides, clones, éditeur XML, panneaux Filtres et Effets de chemin | vus à l'écran |
| Exports PNG, PDF, EPS, DXF, HPGL, SVG simple, extensions `.inx` | essayés ; aucun fichier relu par un autre logiciel |
| Gomme, Ajustement, Pulvérisation, Connecteur, Mesure, vectorisation, découpe et masque, texte vers chemin, joindre et couper | **écrits, compilés, pas ou à peine essayés** |
| Thèmes | Givre et Onyx Noir vus à 800 x 600 ; autres tailles pas vues |

## Défauts connus et manques

* Tactile : pas de mode brut, un trait vertical au doigt reste impossible (`gt911` en fait une molette).
* Menus : noyau seulement, `WS_MENU_MAX` reste à 2 048 ; le reste par `Ctrl+K`. AZERTY : table dans l'app (`key_letter`), pas de `ui_key_char`.
* Rien n'a été ajouté à UIKit. Curseurs : formes `UC_*` existantes.
* Pas de panneau Outil `⋯`, Transformer sans aperçu direct, pas d'outils Pages, Boîte 3D, Maillage.
* Pas d'ICC ni de CMJN, pas de BiDi. `SK_PDF_USE_HARFBUZZ_SUBSET` absent : les PDF embarquent les polices entières.
* Paramètres d'effets de chemin : les nôtres, pas d'échange avec Inkscape. Position des guides dans le fichier : compatibilité non vérifiée.
* Export EPS, DXF, HPGL : pas de dégradés ni de filtres.
* Déplacer un objet refait la scène à chaque image : pas encore de calque pour l'objet tiré.
* Éléments d'un autre espace de noms au même nom : pris pour du SVG.
