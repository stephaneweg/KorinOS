# Plan : un éditeur vectoriel écrit pour ARMPI

Plan du 01/11/2026. **Le développement a commencé : l'état réel est dans `src/apps/vector/README.md`.** Deux points de ce plan se sont révélés
faux (voir `docs/UX-VECTOR.md`, § 0) : les menus de `ui_menu` vont dans la barre globale, pas dans la fenêtre (§ 3.6), et les menus d'une app
n'ont pas de sous-menus (§ 5). Écrit au départ sans code d'implémentation. C'est l'un des deux plans préparés pour le même but (l'autre : un port
d'Inkscape avec une couche d'adaptation UIKit). L'utilisateur choisit ensuite.

**Le choix proposé ici** : une application C++ neuve, `src/apps/vector`, exécutable `vector` (nom provisoire), sur UIKit pour
l'interface, Skia pour le rendu, HarfBuzz et FreeType pour le texte, libxml2 pour lire et écrire le SVG, qui est son format natif.

## 1. En bref

| Question | Réponse |
|---|---|
| Faisable ? | Oui. Skia, HarfBuzz, FreeType et libxml2 sont déjà construits pour ARMPI. Un programme hors WebKit se lie à eux sans changement (mesuré, § 2). |
| Première version utile | Étape 3 : environ 45 000 lignes de C++. On dessine, on édite des nœuds, on écrit du texte, on fait des booléens et des dégradés, on exporte en PNG et PDF. |
| Parité avec Inkscape | Environ 150 000 lignes de C++ et 5 000 lignes d'assembleur dans UIKit, Elegant et le service tactile. Parité large, **pas totale** (§ 6). |
| Ce qu'on n'aura pas à la fin | Les extensions Python d'Inkscape telles quelles, la pression des tablettes (ARMPI n'en gère aucune), vingt ans de cas limites. |
| Plus gros risque | La longue traîne : le dernier cinquième de la parité coûte autant que le reste (§ 8). |

## 2. Constats

Tout ce tableau vient du dépôt ou d'une mesure faite pour ce plan.

| Sujet | Constat |
|---|---|
| Skia | m154, copie de WebKit, licence BSD-3, 545 sources (`tools/webkit/ports/skia/sources.cmake`). **Processeur seul**. `libskia.a` : 15,7 Mo, dans `~/armpi-webkit-build/sysroot/lib`. Construit par `tools/webkit/build-deps.sh`, hors du dépôt, comme HarfBuzz 14.5.1, FreeType 2.14.3, libxml2 2.13.8, libpng, libjpeg-turbo, libwebp. |
| Essai d'édition de liens | Un petit programme appelant surface raster, `SkPathBuilder`, PathOps, tirets, flou, `SkPicture`, PDF, `hb_shape` et `xmlReadMemory`, lié avec `--gc-sections` et sans ICU : **6,1 Mo dépouillé** (5,4 Mo de code, 0,7 Mo de données). Compilé et lié, **pas exécuté**. UIKit n'était pas lié dans cet essai. |
| ICU | `skiatest` fait 25 Mo parce qu'il lie ICU (16,9 Mo de données). L'éditeur peut s'en passer au début : HarfBuzz a ses propres tables Unicode (`_hb_ucd_*` dans l'essai). Le script d'Onyx le faisait déjà pour `skiademo`. |
| Skia en marche | WebCore peint avec lui sous QEMU et sur le Pi. Aucune mesure de Skia seul sur le Pi n'existe. `skiatest` n'a pas été essayé sur ARMPI (`docs/WEBKIT.md`). |
| Kit partagé | **Inutile.** `dlopen` échoue (programmes statiques, `docs/POSIX.md`) ; un kit est une table de fonctions en assembleur, et une ABI plate pour Skia demanderait des centaines de fonctions pour un seul client. Jet lie Skia statiquement. Deux lancements du même exécutable partagent déjà leurs pages (images du noyau). À revoir si une deuxième app vise Skia. |
| Construction | `tools/office/build.sh` compile tout `.c` et `.cpp` du dossier d'une app (C++20, `cortex-a72`) et a déjà des cas à part pour OpenSSL et MuPDF. Vector y ajoute son cas, avec les définitions de `skia.pc` (`SK_R32_SHIFT=16`, etc.). Comme Mail et PDF, l'app ne se construit que sur le PC tant que `libskia.a` n'est pas sous `/gcc/lib`. |
| Modèle UIKit des apps C++ | Tout passe par `ui_view` (dessin, souris, touche, molette), les poignées `ui_*`, les jetons du thème, `ui_tr`, et une boucle `ui_event`, `ui_w_event`, `ui_view_flush`. Slides (11 100 lignes hors `stb_image`, 800 Ko), Paint (20 300 lignes) et Circuits suivent cette recette. |
| Surface | `ui_surface` donne un tampon partagé qu'Elegant compose directement, sans copie, en `0x00RRGGBB`. Le N32 de Skia a les mêmes octets : aucune conversion. Double tampon possible par décalage. Jet s'en sert. |
| GPU | Pas de pilote OpenGL ES sur ARMPI, et Ganesh n'est pas dans l'arbre : Skia ne peut pas rendre par le V3D. GpuKit sait composer des quads texturés (`gk_texture`, `gk_texture_rect`, `gk_render`) : 512 textures par process, 2048 × 2048 au plus, 1024 lots. Vérifié au texel près sur le PocketTerm ; Jet y compose. Sous QEMU : `GK_E_NOGPU`, le repli est à notre charge. |
| Matériel | PocketTerm : Pi 4 de 8 Go, écran 800 × 600, quatre cœurs réels pour `std::thread`, clavier, tactile GT911. Le code doit aussi vivre sur un Pi de 2 Go. |
| Onyx | Aucun éditeur vectoriel à porter : le SVG n'y apparaît que marginalement (`3dforge`, `paint/pfile.h`). |

## 3. Architecture

### 3.1 Découpage

Trois couches, dans cet ordre de dépendance :

| Couche | Contenu | Dépend de |
|---|---|---|
| `geom/` | chemins en `double`, Bézier, transformations, tolérances, ajustement de courbes, décalages | rien : se compile et se teste sur le PC, natif |
| `doc/`, `style/`, `render/`, `text/`, `lpe/` | document, CSS, annulation, rendu, texte, effets de chemin | Skia, HarfBuzz, FreeType, libxml2 ; **jamais UIKit** |
| `tools/`, `ui/`, `main.cpp` | outils, panneaux, fenêtre | tout, et UIKit |

Les deux premières couches s'utilisent sans fenêtre : `vector --render a.svg b.png` et `vector --actions script a.svg`.
C'est le socle de toute la vérification sous QEMU (§ 7).

**Coordonnées** : `double` dans le document, comme le SVG l'exige pour un zoom profond. Skia travaille en `float` : on envoie
les chemins dans le repère de la tuile ou de la vue, jamais en coordonnées absolues. Le chemin à soi n'est donc pas `SkPath`.

### 3.2 Modèle de document

**L'arbre XML est la vérité.** Tout le reste en dérive et peut être reconstruit.

1. **`XNode`** : arbre à nous, avec éléments, attributs, texte, commentaires, espaces de noms. libxml2 ne sert qu'à l'entrée et
   à la sortie. Tout ce qui est inconnu est gardé et réécrit : un fichier ouvert puis enregistré ne perd rien.
2. **`SvgObject`** : une classe par élément, avec ses attributs analysés, remise à jour par notification quand un attribut
   change. Rectangles, cercles, chemins, texte, `use`, symboles, marqueurs, dégradés, motifs, découpes, masques, filtres.
3. **Style** : analyseur CSS à nous (attribut `style`, `<style>`, attributs de présentation, héritage, cascade, sélecteurs).
4. **Références** : index des `id` et graphe de dépendances (`href`, `url(#…)`, clones, effets de chemin), pour savoir quoi
   recalculer et pour renommer ou supprimer proprement.
5. **Compatibilité Inkscape** : on lit et on réécrit ses espaces de noms pour ce qui a un sens commun : calques
   (`inkscape:groupmode`), types de nœuds (`sodipodi:nodetypes`), guides et grilles (`sodipodi:namedview`), pages, formes
   paramétriques, `inkscape:original-d`. Ces faits s'observent sur des fichiers (§ 9). Le `d` contient toujours le résultat
   calculé : n'importe quel lecteur SVG affiche le bon dessin.

### 3.3 Annulation

Un **journal d'opérations atomiques** sur `XNode` : attribut changé (ancien, nouveau), nœud inséré, retiré, déplacé. Une
transaction regroupe les opérations d'un geste et porte un libellé pour le menu et l'historique.

* Annuler, c'est jouer l'inverse. Le même chemin de notification met à jour les objets et le rendu : il n'y a pas de second
  code à écrire pour chaque commande.
* Les petites valeurs répétées (un curseur que l'on tire) se fondent en une seule opération.
* Pendant un glisser, seule une transformation temporaire change ; le journal reçoit le résultat au relâchement.
* Le journal a une limite en mémoire (64 Mo par défaut) ; les plus anciennes transactions tombent.

Cette règle a un prix : aucune commande ne peut changer l'état autrement que par l'arbre. La sélection et la vue, elles, ne
sont pas dans le journal.

### 3.4 Rendu

```
XNode -> SvgObject -> DrawItem (boîte, chemin Skia, peinture, SkPicture) -> R-tree
                                         |
                       tuiles 256 x 256 par niveau de zoom, rendues par des threads
                                         |
       image de la vue = tuiles + calque de l'objet tiré + surcouche (poignées, guides)
                                         |
                            surface d'Elegant (ui_surface), double tampon
```

* **Tuiles** : 256 Ko pièce. 800 × 600 en demande 20 (5 Mo), 1920 × 1080 en demande 54 (14 Mo). Cache de 64 Mo par défaut.
* **Threads** : trois travailleurs pour les tuiles. Le fil principal garde l'entrée, la surcouche et **tous** les appels `ui_*` :
  UIKit n'est pas prévu pour plusieurs threads. Tant que des tuiles sont en cours, il attend `ui_event` avec un délai court.
* **Priorité** : du pointeur vers les bords. Un numéro de génération abandonne le travail devenu inutile.
* **Invalidation** : la boîte de ce qui change, ancienne et nouvelle, réunies, sert à retrouver les tuiles touchées.
* **Glisser** : le dessin sans l'objet reste en cache, l'objet est rendu à part (la technique des calques de Slides). Déplacer un
  objet ne repeint pas la scène, s'il n'y a ni filtre ni mélange qui le lie à son fond.
* **Zoom** : les tuiles existantes sont agrandies tout de suite, puis remplacées par les bonnes.
* **Modes** : normal, contours seuls, sans filtres. Ils existent dès le début pour garder la main sur un gros dessin.
* **GPU** : optionnel, tard (étape 10). Le CPU suffit à 800 × 600 ; une composition par `gk_render` servira aux grands écrans et au
  zoom fluide, avec repli sur le CPU, obligatoire sous QEMU. Il faudra composer plus de tuiles que 512 textures n'en permettent
  sur de très grands écrans : une texture en contiendra plusieurs.

**Objectifs sur le Pi 4, à confirmer à l'étape 0** : ce ne sont pas des mesures.

| Action | Objectif |
|---|---|
| Déplacer la vue, tuiles en cache | 60 images/s à 800 × 600 |
| Déplacer un objet simple | 30 images/s ou mieux sur un dessin de 5 000 objets |
| Repeindre tout l'écran, 5 000 objets sans filtre | moins de 300 ms, le pointeur restant fluide |
| Ouvrir un SVG de 1 Mo | moins de 2 s |
| Mémoire, dessin moyen | moins de 200 Mo |

### 3.5 Outils

Une interface commune (`press`, `move`, `release`, `key`, `overlay`, barre de commandes contextuelle), un fichier par outil. Service
commun : test de touche (R-tree, puis contour avec tolérance), aimantation, préférences, surcouche dessinée par Skia dans la vue.

### 3.6 Panneaux et fenêtre

```
+--------------------------------------------------------------+
| Menus (ui_menu)                         FRAME_CAP_W libres -> |
| Barre de commandes de l'outil (ui_toolbar)                    |
+----+------------------------------------------+---------------+
| O  | règle                                    | panneau       |
| u  | +--------------------------------------+ | à onglets     |
| t  | | vue (ui_surface, Skia)               | | (tiroir sous  |
| i  | |                                      | |  800 px)      |
| l  | +--------------------------------------+ |               |
| s  |                                          |               |
+----+------------------------------------------+---------------+
| palette de couleurs | calque | état | zoom                     |
+--------------------------------------------------------------+
```

* **Le thème** : jetons de `ui_jetons` partout, Givre et Onyx Noir compris. Les poignées suivent aussi les jetons, le dessin du
  document non. Le bureau autour de la page est `CREUX`.
* **La vue** n'est pas une `ui_view` mais une surface, car Elegant ne peut rien poser par-dessus. Les poignées sont donc peintes
  dans l'image par nos soins ; règles et barres sont autour, jamais dessus.
* **800 × 600** est le cas de référence, pas un cas dégradé : outils en rail étroit, panneau en tiroir, barre contextuelle qui
  retire d'abord ce qui compte le moins (la recette `row_place` de Letters).
* **Panneaux** : Remplissage et contour, Calques et objets, Texte, Transformer, Aligner, Éditeur XML, Nuanciers, Symboles,
  Filtres, Effets de chemin, Pages, Document, Export. Chacun dans son fichier, et chacun lit le même document.

### 3.7 Fichiers

| Dossier | Contenu | Lignes à terme |
|---|---|---|
| `geom/` | chemins, Bézier, ajustement, décalage, booléens d'appoint | 14 000 |
| `doc/`, `style/` | XML, objets SVG, CSS, journal, références, unités | 25 000 |
| `render/` | éléments de dessin, tuiles, filtres, dégradés, motifs | 15 000 |
| `text/` | mise en forme, retour à la ligne, texte sur chemin, polices | 12 000 |
| `lpe/` | une classe par effet | 18 000 |
| `tools/` | une classe par outil, aimantation | 25 000 |
| `ui/` | panneaux, barres, dialogues, icônes | 22 000 |
| `io/`, `ext/` | export, import, extensions, vectorisation | 19 000 |

Binaire attendu, sur la base de Slides (environ 70 octets par ligne) : **9 Mo** à l'étape 3, **16 Mo** à la fin.

## 4. Ce que Skia fournit, ce qu'il faut écrire

### Déjà dans `libskia.a`

Vérifié dans `sources.cmake` ou par l'essai de liens.

| Besoin | Skia |
|---|---|
| Remplissage et contour anticrénelés | raster, `SkStroke`, jointures, bouts, `skpathutils::FillPathWithPaint` (contour vers chemin) |
| Dégradés | linéaire, radial, conique, angulaire |
| Dégradé de maillage | `drawPatch`, `SkVertices`, `SkPatchUtils`, à assembler |
| Tirets, coins, rugosité, découpe de contour | `SkDashPathEffect`, `SkCornerPathEffect`, `SkDiscretePathEffect`, `SkTrimPathEffect` |
| Booléens | PathOps : union, intersection, différence, ou exclusif, simplification, en `float` |
| Mesure le long d'un chemin | `SkContourMeasure` : longueur, point, tangente, tronçon |
| Filtres | flou, ombre, matrice de couleur, table, mélange, décalage, fusion, morphologie, déplacement, éclairage, convolution, image, bruit de Perlin |
| Enregistrement | `SkPicture`, `SkPictureRecorder` |
| Texte | `SkTextBlob` à partir d'identifiants de glyphes, contours de glyphes, `SkRSXform` pour le texte sur chemin, `SkFontMgr_onyx` |
| PDF | `SkPDF` : vectoriel, polices embarquées, métadonnées |
| Images | PNG, JPEG, WebP en lecture et écriture |
| Couleur | `skcms`, espaces de travail, sRGB |
| Programmes de pixels | `SkRuntimeEffect` (SkSL par le pipeline raster, sans GPU) |

### Absent, décidé

| Absent | Décision |
|---|---|
| `SkParsePath` : l'en-tête est là, **la source n'est ni compilée ni dans l'arbre** | notre lecteur de `d` : il faut de toute façon des `double`, et 200 lignes suffisent |
| `modules/svg` (SkSVG) : pas dans l'arbre | on ne le récupère pas. Il rend du SVG, il n'édite pas. Notre arbre est indispensable |
| `modules/skshaper`, `skparagraph` | pas repris : la mise en page SVG n'est pas celle de skparagraph ; HarfBuzz directement |
| `src/svg` (`SkSVGCanvas`) | sans objet, nous avons l'arbre |
| Ganesh, Graphite | § 3.4 |
| Sous-ensemble de polices PDF | `SK_PDF_USE_HARFBUZZ_SUBSET` n'est pas défini. À activer dans `CMakeLists.txt`, sans quoi les PDF sont plus gros |
| PDF/X, CMJN | absent de Skia : export propre à écrire, étape 10 |

### À écrire

* Analyse et écriture SVG, CSS, unités, `viewBox`, `preserveAspectRatio`.
* Résolution du style en `SkPaint`, y compris marqueurs, motifs, `use`, découpes, masques.
* Le graphe de filtres SVG traduit en `SkImageFilter`, avec les espaces de couleur et les sous-régions du SVG.
* Toute la mise en page du texte.
* Toute la géométrie d'édition : nœuds, poignées, ajustement de courbes, décalage, interpolation, effets de chemin.
* Toute l'application.

**PathOps n'est pas une garantie.** Sa précision est en `float`, et `Op` peut échouer (il rend un résultat optionnel) ou
donner un résultat faux sur des courbes presque confondues. Voir § 8.

## 5. Ce qui manque à UIKit

Règles : ajout en fin de `uikit.abi` seulement, `uikit.so` reconstruit avec, et **ce qui peut servir à d'autres va dans UIKit**.

| Besoin | État | Ajout | Où | Étape |
|---|---|---|---|---|
| Tracer au doigt | `gt911` traduit : glisser vertical à un doigt = **molette**, deux doigts = molette. Un trait vertical au doigt est impossible | mode brut par fenêtre : événements de contact avec identifiant, pincement | service `gt911`, Elegant, UIKit (assembleur) | 7 |
| Icônes d'outils | `ui_toolbar_add` prend un glyphe | bouton à image teintée par un jeton, bouton à flèche | UIKit | 2 |
| Curseurs | 12 formes fixes | curseur image (`UC_*` + pixels, point chaud) | Elegant, UIKit | 2 |
| Nombre réel avec unité | `ui_numeric` entier ; `ui_slider` entier | champ réel, unité, pas, glisser sur le libellé | UIKit | 2 |
| Couleur | `ui_colorpicker` : palette 8 × 5 | TSL, TSV, alpha, hexadécimal, pipette ; d'abord dans l'app sur `ui_view` | app, puis UIKit | 2 |
| Arbre | `ui_tree` sans lignes dessinées ni glisser ; rien de virtuel | `ui_tree_virtual`, `ui_tree_cell`, glisser (déjà dans la liste de `docs/UIKIT-MANQUES.md`) | UIKit | 3, 4 |
| Panneaux flottants | plusieurs fenêtres oui, « toujours au-dessus » non (`UIKIT-MANQUES.md`) | rôle `WF_*` de fenêtre outil | Elegant, UIKit | 4 |
| Molette | ni position ni modificateurs dans le rappel d'une vue ; pas de roue horizontale | l'app lit `ui.ev` avant `ui_w_event` ; rien à ajouter d'abord | app | 1 |
| Touche relâchée | le rappel de vue ne le dit pas | même lecture de `ui.ev` (`WEV_KEY` +4 le dit) : Espace tenu pour déplacer | app | 1 |
| Points intermédiaires | entiers ; regroupement éventuel des déplacements **pas vérifié** | à mesurer à l'étape 0 ; sinon file d'événements | Elegant | 0 |
| Grands menus | `ui_menu`, sous-menus, états et raccourcis existent ; limite de taille du texte **pas vérifiée** pour plusieurs centaines d'entrées | à mesurer | UIKit | 2 |
| Aperçu dans les dialogues de fichiers | absent | `ui_file_preview` | UIKit | 7 |
| Redimensionnement en direct, plein écran | absents | utiles, pas indispensables | Elegant | 11 |

Déjà suffisant : `ui_surface_*`, `ui_view`, `ui_splitter`, `ui_tabs`, `ui_panel`, `ui_list_cell`, `ui_textarea` (éditeur XML), `ui_file_filter`,
`ui_clip_put` et `ui_clip_take` (format `image/svg+xml`), `ui_clip_set_image`, `ui_drag_start`, `ui_tr`, `ui_window_use`.

## 6. Comparaison avec Inkscape

Référence : **1.4.4**, stable, et la branche **1.5-dev** (GTK 4, nouveau système de couleurs, lasso, CapyPDF). Sources : § 10.
La cible bouge : la parité veut dire 1.4.4 au moins.

Verdicts : **Parité** : même service, avec nos propres algorithmes. **Partiel** : l'usage courant, pas tout. **Non** : pas visé.
« Ét. » : étape où la fonction devient utilisable.

| Domaine | Fonction | Skia donne | À écrire | Ét. | Verdict |
|---|---|---|---|---|---|
| **Tracés** | Stylo Bézier, Spiro, B-spline | rendu | tout l'outil | 2, 6 | Parité |
| | Crayon, lissage | rendu | ajustement de Schneider | 2 | Parité |
| | Calligraphie | rendu | modèle de plume | 7 | Partiel : **pas de pression** ; vitesse et inclinaison simulées |
| | Formes : rectangle, ellipse, étoile, spirale, 3D | primitives | formes paramétriques | 2, 7 | Parité |
| **Nœuds** | Types, poignées, ajout, suppression, jonction, coupe | rien | tout | 2 | Parité |
| | Lissage auto, simplification, transformation de plusieurs nœuds | rien | tout | 4 | Parité |
| **Booléens** | Union, différence, intersection, exclusion, division | PathOps | robustesse, découpe de chemin | 3 | Partiel au début, parité visée à l'étape 11 : risque R2 |
| | Décalage de chemin, contour vers chemin | contour Skia | décalage propre, nettoyage | 3, 6 | Parité |
| | Shape Builder | PathOps | l'outil | 7 | Parité |
| **Effets de chemin** | Une quinzaine nommée par le manuel 1.3 ; davantage dans l'app, nombre exact non vérifié | tirets, découpe, mesure | chaque effet, empilement, poignées sur le canevas, `original-d` | 6 | Partiel : le cœur (Power Stroke, motif le long du chemin, coins, miroir, copies en rotation, enveloppe, décalage, rugosité, booléen) ; l'échange de paramètres avec Inkscape n'est pas garanti |
| **Texte** | Texte courant et dans une forme, styles par caractère | glyphes, blobs | toute la mise en page, édition sur canevas | 3, 7 | Parité |
| | Texte sur chemin, crénage manuel, `dx`, `dy`, `rotate` | mesure, `SkRSXform` | placement | 3, 4 | Parité |
| | OpenType : fonctionnalités, polices variables | HarfBuzz, FreeType | panneau | 7 | Parité |
| | Droite à gauche, vertical, écritures complexes | HarfBuzz (mise en forme) | BiDi et coupure de lignes : risque R5 | 7 | Partiel |
| | Choix de polices | `SkFontMgr_onyx` | panneau, paquets de polices | 3 | Parité du service, mais ARMPI n'a que DejaVu aujourd'hui |
| | Éditeur de police SVG | rien | tout | — | Non |
| **Dégradés** | Linéaire, radial, poignées sur le canevas, arrêts | shaders | outil | 3 | Parité |
| | Dégradé de maillage | `drawPatch` | outil, découpe en patchs, export | 5 | Partiel |
| | Hachures (`hatch`) | motifs | rendu et écriture | 5 | Partiel |
| **Filtres SVG** | Primitives `fe*` | `SkImageFilter` pour presque toutes | traduction, sous-régions, `linearRGB` | 5 | Parité |
| | Éditeur de filtres | rien | panneau | 5 | Parité |
| | Galerie de filtres | rien | **nos propres préréglages**, ceux d'Inkscape sont sous sa licence | 5 | Partiel : moins nombreux |
| | Vitesse | NEON | cache par objet | 5 | Plus lent que sur PC : R3 |
| **Calques** | Calques, sous-calques, verrouillage, masquage, blend, opacité | `saveLayer`, modes de mélange | panneau | 3 | Parité |
| | Panneau Calques et objets | rien | `ui_tree` étendu | 3, 4 | Parité |
| **Symboles, clones** | `use`, clones, symboles, bibliothèques | `SkPicture` | panneau, nos bibliothèques | 4 | Parité du service, bibliothèques à créer |
| | Clones en tuiles | rien | dialogue | 7 | Parité |
| **Masques, motifs** | `clipPath`, `mask`, `marker`, `pattern` | découpe, shaders | édition | 4 | Parité |
| **Aide au dessin** | Aimantation, guides, grilles, règles, mesure | rien | tout | 4, 7 | Parité |
| | Pages multiples | PDF par page | outil Pages | 4 | Parité |
| **Autres outils** | Seau, gomme, pulvérisation, ajustement, connecteurs, pipette | rendu | chaque outil ; routage à nous | 7 | Parité, connecteurs Partiel |
| **Vectorisation** | Un seuil, couleurs, lignes centrales, pixel art | rien | algorithme écrit d'après l'article publié, quantification, § 9 | 8 | Partiel : qualité à mesurer contre Potrace |
| **Export** | PNG par zone, objet, échelle | encodeur | dialogue, lots | 3 | Parité |
| | PDF | `SkPDF` | filtres en images, liens, texte conservé | 3 | Parité courante ; PDF/X, CMJN : Non avant l'étape 10 |
| | SVG simple, EPS, PS, DXF, HPGL, EMF, WMF | rien | un écrivain par format | 8 | Partiel : SVG, EPS, DXF, HPGL d'abord |
| **Import** | SVG, images, PDF | SVG à nous, PDF par outil séparé | § 9, MuPDF | 8 | Partiel |
| | AI récents, CDR, VSD, Affinity | rien | rien | — | Non, sauf les AI en PDF |
| **Couleur** | RGB, TSL, TSV, nuanciers | sRGB | panneaux, fichiers de palettes | 2, 4 | Parité |
| | ICC, CMJN, OkLCh (1.5) | `skcms` pour les profils | stockage, épreuvage | 10 | Partiel |
| **Extensions** | Protocole `.inx`, SVG par l'entrée et la sortie standard | rien | lanceur, dialogue généré, exécutables natifs | 9 | Parité du protocole |
| | Extensions Python fournies, `inkex` | rien | Python absent d'ARMPI | — | **Non**. Un port de Python serait un autre chantier, et ces extensions restent sous GPL ; on écrit nativement les plus utiles |
| **Automatisation** | Ligne de commande, actions | rien | `--render`, `--actions` | 1, 9 | Parité |
| **XML** | Éditeur XML en direct | rien | panneau | 4 | Parité |
| **Tablettes** | Pression, inclinaison | rien | pilote USB, événements | — | **Non**, faute de matériel et de pilote |

**Où Inkscape reste devant à la fin** : les extensions, la profondeur des formats, l'épreuvage couleur, les milliers de corrections
accumulées. **Où ce plan peut faire mieux** : réactivité sur le Pi, interface pensée pour 800 × 600 et le tactile, thème du bureau,
démarrage. Ce sont des buts, à mesurer ; rien de cela n'est acquis.

## 7. Étapes

Tailles en lignes de C++ ajoutées. Chaque étape finit dans `src/apps/vector` avec son `README.md` et la colonne « utilisable » vraie.

**Méthode de vérification, commune** :

* `tools/office/tryimg.py` donne une carte par essai ; `tools/qemu_drive.py` joue clics et touches (AZERTY, attendre 24 s).
* `vector --render` produit un PNG sous QEMU, comparé sur le PC à une référence (`tools/vector/compare.py`, tolérance par pixel).
* Références : suite de tests de resvg (MIT) et W3C SVG 1.1, et Inkscape utilisé **en boîte noire** sur le PC.
* **Aller-retour** : ouvrir, enregistrer, rouvrir : arbre identique.
* **Annulation au hasard** : `--actions` joue des opérations tirées au sort, tout annuler rend le fichier de départ, à l'octet.
* QEMU ne dit rien de la vitesse ni du GPU. Les chiffres viennent du Pi, à chaque étape marquée ★.
* `selfhost` seulement aux jalons, pas à chaque étape.

| Ét. | Contenu | Utilisable | Taille | Vérification sous QEMU |
|---|---|---|---|---|
| **0** ★ | Cas `vector` dans `build.sh` ; fenêtre, `ui_surface`, scène Skia écrite en dur, tuiles, trois travailleurs, banc de 5 000 objets, mesure des événements souris | Rien pour l'utilisateur : **décide si l'architecture tient** | 1 500 | La fenêtre s'ouvre, thèmes Givre et Onyx Noir, capture ; le banc donne ses temps sur le Pi |
| **1** | XML, CSS, objets SVG, rendu statique, `use`, dégradés, découpes ; déplacement et zoom ; `--render` | Visionneuse SVG, miniatures | 12 000 | 70 % de la suite resvg ciblée (structure, formes, peinture) ; aller-retour sur 100 fichiers |
| **2** | Sélection, transformations, journal, formes, stylo, crayon, nœuds, Remplissage et contour, enregistrement ; ajouts UIKit | Petit éditeur de formes et de tracés | 15 000 | Scénario `qemu_drive` : dessiner, annuler, enregistrer, rouvrir ; annulation au hasard |
| **3** ★ | **Première version utile** : texte (gauche à droite, HarfBuzz), dégradés, calques, booléens, contour vers chemin, export PNG et PDF, panneaux en tiroir à 800 × 600 | Logos, schémas, affiches simples | 15 000 | Fichier de démonstration créé au clavier et à la souris ; PDF rouvert dans `pdf` ; fichier rouvert sans perte dans Inkscape sur le PC |
| **4** | Aimantation, guides, grilles, aligner, clones, symboles, marqueurs, motifs, masques, pages, éditeur XML, nuanciers | Dessin technique, mise en page | 18 000 | 90 % de la suite resvg sur ces éléments ; scénarios par panneau |
| **5** ★ | Filtres, éditeur, galerie à nous, modes de mélange, maillage, hachures | Illustration | 12 000 | Suite `filters` de resvg ; temps par filtre sur le Pi |
| **6** | Effets de chemin, empilement, Spiro, B-spline, décalage dynamique | Dessin paramétrique | 18 000 | Comparaison visuelle à Inkscape en boîte noire, effet par effet |
| **7** | Calligraphie, ajustement, pulvérisation, seau, gomme, connecteurs, mesure, Shape Builder, texte dans une forme, OpenType, BiDi, mode tactile | Le reste de la boîte à outils | 14 000 + 2 000 asm | Scénarios par outil. Le **tactile ne se vérifie pas sous QEMU** : sur le PocketTerm |
| **8** | Vectorisation, EPS, DXF, HPGL, SVG simple, import PDF par outil séparé | Passerelles | 12 000 | Bitmaps d'essai comparés à Potrace sur le PC ; fichiers relus par Inkscape en boîte noire |
| **9** | `.inx`, dialogues générés, extensions natives, actions | Automatisation, extensions écrites pour ARMPI | 8 000 | Extension d'essai qui ajoute un objet par entrée et sortie standard |
| **10** ★ | ICC, CMJN, OkLCh, composition GpuKit | Pré-presse simple, grands écrans | 8 000 | Repli CPU sous QEMU ; GPU sur le Pi seulement |
| **11** ★ | Campagne : gros fichiers, fichiers d'Inkscape réels, robustesse des booléens, performances, documentation | **Parité déclarée**, au sens du § 6 | 15 000 | Toute la suite resvg, corpus de fichiers, 24 h d'annulation au hasard |

**Total** : environ 150 000 lignes de C++, 5 000 d'assembleur. La comparaison interne : **7 à 8 fois Paint**.

Durée indicative, à prendre comme un ordre de grandeur : 30 à 40 sessions pour le tout, dont 8 à 10 jusqu'à l'étape 3, plus les
essais de l'utilisateur sur le Pi. Rien dans le projet ne donne une mesure à cette échelle.

**Point d'arrêt** : chaque étape à partir de la 3 donne une application qui a son intérêt. On peut s'arrêter à la 5 avec un bon
éditeur : c'est l'atout de ce plan face à un port, qui n'a rien à montrer avant longtemps.

## 8. Risques

| # | Risque | Probabilité | Effet | Parade |
|---|---|---|---|---|
| R1 | **La longue traîne** : chaque fonction d'Inkscape a des années de cas limites ; la parité recule, et 1.5 ajoute des choses | Certaine | **Le plus gros** : la dernière étape peut doubler | La parité est définie par le § 6 et par des suites de tests, pas par « tout ». Point d'arrêt à chaque étape |
| R2 | **PathOps** échoue sur des courbes presque confondues | Élevée | Booléens faux | Banc de 10 000 paires à l'étape 3 ; contrôle d'aire du résultat ; repli sur aplatissement, découpe en polygones et ré-ajustement, écrit dans `geom/` |
| R3 | Filtres lents sur le Pi (flou large, éclairage, grand écran) | Élevée | Illustrations pénibles | Cache par objet, rendu en basse résolution pendant l'édition, mode sans filtres |
| R4 | Skia seul plus lent que prévu sur le Pi | Moyenne | Architecture à revoir | Étape 0 d'abord, sur le Pi, avant tout le reste |
| R5 | Texte sans ICU : pas de BiDi ni de coupure de lignes standard | Certaine | Arabe et hébreu limités | Gauche à droite d'abord. Ensuite, soit un petit module UAX 9 et 14 à nous, soit ICU en plus (16 Mo). Décision à l'étape 7 |
| R6 | `malloc` de newlib sous quatre threads | Moyenne | Contention dans les tuiles | Arène par tuile, `SkArenaAlloc`, moins d'allocations par image. Mesure à l'étape 0 |
| R7 | Tactile : le service traduit les gestes, assembleur à changer | Certaine, coût connu | Pas de tracé au doigt avant l'étape 7 | Clavier et souris d'abord ; mode brut par fenêtre |
| R8 | Un seul jeu de polices | Certaine | Les fichiers reçus s'affichent mal | Paquets de polices libres par `pkg` ; police d'origine gardée dans le fichier |
| R9 | Contamination du clean-room | Moyenne | Code à refaire | Règles du § 9, sources citées dans chaque fichier d'algorithme |
| R10 | Échange avec Inkscape imparfait (effets de chemin, texte) | Élevée | Fichiers rouverts différemment | Le `d` et les `tspan` calculés sont toujours écrits ; essai de relecture à chaque étape |
| R11 | Dépendances hors du dépôt : `sysroot` à reconstruire si libposix change, `skia.pc` à suivre | Moyenne | Pixels inversés, binaires invalides | Options lues dans `skia.pc`, jamais recopiées ; `tools/webkit/sysroot.sh` dans la procédure |
| R12 | `float` de Skia à zoom extrême | Moyenne | Tremblement | Chemins remis dans le repère de la tuile |
| R13 | Limites système : 128 tampons partagés, 512 textures | Faible | Refus d'une surface, GPU | Un seul tampon pour la vue, atlas pour le GPU |

## 9. Licence et clean-room

### Faits

| Composant | Licence | Vérifié |
|---|---|---|
| Inkscape | Sources surtout GPL-2.0-or-later, **binaires complets sous GPL-3.0-or-later** | `COPYING` de la branche `master` |
| lib2geom | LGPL-2.1 ou MPL-1.1, avec une question ouverte sur sa dépendance à GSL | source secondaire, ticket Inkscape |
| Potrace 1.16 | GPL-2.0-or-later ; le nom est une marque ; licence commerciale à part ; article public de l'auteur | site de Potrace |
| Skia | BSD-3 | `LICENSE` du dépôt |
| libxml2, HarfBuzz, FreeType, libpng, libjpeg-turbo, libwebp | permissives | à relire dans `src/extra` avant publication |
| MuPDF | AGPL-3.0 | `src/extra/README.md` |

### Lire le code d'Inkscape est-il interdit ?

**La loi ne l'interdit pas, la règle du projet si.** Le projet est clean-room hors Onyx, et Inkscape n'est pas Onyx. Si un développeur
lit le code GPL puis écrit une fonction voisine, le résultat peut être tenu pour une œuvre dérivée, donc sous GPL. ARMPI n'a pas encore
choisi sa licence : il ne faut pas décider à sa place. Ce n'est pas un avis juridique.

**Règle proposée** : les sessions de ce plan ne lisent aucune de ces sources.

| Interdit | Permis |
|---|---|
| Le code d'Inkscape, `lib2geom`, livarot, `inkex`, Potrace, libavoid | Les spécifications : SVG 1.1 et 2, CSS, Filter Effects, PDF, OpenType, Unicode |
| Ses icônes, modèles, palettes, filtres de galerie, symboles, tutoriels, extensions : ce sont des données sous licence | Les articles et ouvrages publiés, cités dans la source : Schneider (ajustement), Selinger (vectorisation), Hobby, Tiller et Hanson (décalages) |
| Copier le texte de son manuel | Le manuel, pour savoir ce qu'une fonction fait, et le logiciel en boîte noire |
| | Les fichiers SVG qu'il écrit, pour apprendre les attributs à relire : ce sont des faits d'interopérabilité |
| | La documentation du protocole `.inx`, sans en recopier le code |
| | Skia, HarfBuzz, FreeType : lire leurs sources, ils sont liés à l'app |

Vectorisation : l'article de Selinger décrit la méthode. Une implémentation écrite d'après lui est permise par cette règle, mais ne doit
pas s'appeler Potrace.

### Licence de l'application

Tous les composants liés sont permissifs : **MIT** (celle des apps de l'utilisateur) convient. **MuPDF ne doit pas être lié à `vector`**, sans
quoi l'app passe en AGPL, comme `pdf`. L'import PDF sera un programme séparé, qui écrit du SVG sur sa sortie, appelé par le mécanisme des
extensions. À revoir au moment de publier.

Le plan concurrent (port) impose la GPL-3 à l'app et à sa couche d'adaptation. C'est une différence de fond entre les deux.

## 10. Sources et limites de la vérification

* Version stable **1.4.4**, sortie le 06/05/2026 : dépôt Ubuntu [inkscape PPA](https://ubuntuupdates.org/ppa/inkscape?dist=noble) et sources secondaires. **Le site inkscape.org a refusé nos lectures
  (403)** : les notes de 1.4.4 n'ont pas été lues.
* 1.5 : [CMakeLists.txt de master](https://gitlab.com/inkscape/inkscape/-/raw/master/CMakeLists.txt) (1.5-dev), [notes de 1.5](https://wiki.inkscape.org/wiki/index.php/Release_notes/1.5) (GTK 4, couleurs, lasso, CapyPDF), sans date de sortie.
* Licence : [COPYING de master](https://gitlab.com/inkscape/inkscape/-/raw/master/COPYING) ; lib2geom : [discussion Guix](https://patches.guix-patches.cbaines.net/comment/237238), [ticket 784](https://gitlab.com/inkscape/inkscape/issues/784).
* Potrace : [site officiel](https://potrace.sourceforge.net/). Tests : [suite resvg](https://github.com/razrfalcon/resvg-test-suite). Effets de chemin : [manuel 1.3](https://inkscape-manuals.readthedocs.io/en/1.3/live-path-effects.html). Extensions : [How to Write Extensions](https://inkscape.org/develop/extensions), [ScriptingHOWTO](https://wiki.inkscape.org/wiki/ScriptingHOWTO).
* Nouveautés de 1.4 : [Linuxiac](https://linuxiac.com/inkscape-1-4-vector-graphics-editor-released/).

**Pas vérifié** : le nombre exact d'effets de chemin ; la liste exacte des formats d'Inkscape, en partie de mémoire ; Shape Builder, que je connais
comme un outil vectoriel alors que Linuxiac le décrit pour les images : à contrôler dans l'app ; la licence du manuel ; celle de la suite W3C pour un
usage dans nos tests. **Toutes les tailles et durées sont des estimations.** Les objectifs de vitesse n'ont aucune mesure derrière eux.

## 11. Résumé

* **Effort total** : environ **150 000 lignes de C++ et 5 000 d'assembleur**, en 12 étapes, soit 30 à 40 sessions (ordre de grandeur).
* **Première version utile** : **étape 3**, environ **45 000 lignes**, 8 à 10 sessions, binaire de 9 Mo : tracés, nœuds, texte, dégradés,
  calques, booléens, export PNG et PDF.
* **Plus gros risque** : **la longue traîne de la parité** (R1). Sur le plan technique pur, c'est la robustesse de PathOps (R2). Et la toute
  première inconnue à lever est la vitesse de Skia seul sur le Pi : l'étape 0 sert à ça.
