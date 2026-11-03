# La suite bureautique

Quatre applications reprises d'Onyx, et deux kits en assembleur écrits pour elles. Tiroir **Bureautique** du dock.

| App | Sources | Sur la carte | Fichiers | Binaire |
|---|---|---|---|---|
| **Letters** (traitement de texte) | `src/apps/letters` | `/apps/letters.app` | RTF, DOCX, ODT, texte ; export PDF | 701 Ko |
| **Spreadsheet** (tableur) | `src/apps/sheet` | `/apps/sheet.app` | XLSX, CSV ; ODS en lecture | 910 Ko |
| **Slides** (présentations) | `src/apps/slides` | `/apps/slides.app` | PPTX, ODP ; export PDF, PNG | 800 Ko |
| **Cardfile** (base de fiches) | `src/apps/cardfile` | `/apps/cardfile.app` | `.card`, CSV | 316 Ko |

Chaque dossier a son `README.md` : ce qui est repris d'Onyx, ce qui est écrit pour ARMPI, ce qui manque.

## Les règles suivies

* **Aucun pont** : les apps appellent `ui_*`, `ar_*`, `dk_*` et POSIX. Rien de l'uikit C++ d'Onyx, ni `kapi_*`.
* **Les moteurs sont repris, les interfaces réécrites** : modèle, formats, formules, mise en page viennent d'Onyx ; barres,
  vues et dialogues sont refaits sur UIKit.
* **Le thème est celui du bureau** : jetons de `ui_jetons`, jamais `theme.txt`.
* **Hors du selfhost**, comme Koton : du C++, compilé par GCC, sur le PC ou sous ARMPI. Les deux kits, eux, sont dans le selfhost.

## Les noms

Les apps gardent leur nom d'Onyx. **DocumentKit et ArchiveKit n'existaient pas dans Onyx** : le premier n'y était qu'une analyse
(`IDEAS.md`), et le moteur d'archives vit dans FileKit. Ce sont des kits nouveaux, écrits d'après ce code et d'après les besoins des apps.

## Les kits, décision par kit

[KITS.md](KITS.md) donne leur API.

* **ArchiveKit, en assembleur** (`src/kits/archivekit`, 36 fonctions) : deflate, inflate, zlib, gzip, ZIP en mémoire et sur la carte,
  tar, tar.gz. Des formats à spécifications stables, du calcul sur des octets, déjà une expérience en assembleur avec PNG, et un
  gain réel sur le Pi.
* **DocumentKit, en assembleur** (`src/kits/documentkit`, 60 fonctions, 13 Ko) : lecteur XML, UTF-8, CP1252, longueurs Office et ODF,
  couleurs, dates, CSV, reconnaissance des formats, relations. C'est la plomberie entière et sans état, écrite plusieurs fois dans Onyx.
* **Pas de `dk_doc` ni de `dk_book`** : les moteurs de documents restent dans les apps. Chacun a un seul client, travaille en flottants
  et sur de grosses structures C++, et une ABI les gèlerait sans rien apporter.

## Ce qui a été ajouté à UIKit

Tout est en fin de `uikit.abi` (détail dans [KITS.md](KITS.md)).

* **`ui_view`** : une zone dessinée par le programme, avec ses pixels, sa souris, ses touches, sa molette, ses enfants. Grille, page,
  diapo et fiche en sont. Elle évite de recopier l'arbre de vues de Koton dans chaque app.
* `UL_HSCROLL`, `ui_list_cell`, `ui_date_popup`, `UTF_RIGHT`, `UTF_ERROR`, `UTA_FORMTAB`, `ui_dialog_at`.
* Elegant donne les touches Maj et Ctrl avec la souris.
* Corrections : second `ui_list_virtual`, capture perdue après un menu, flèches du calendrier, clic dans un champ, `ui_clip` dans une
  vue, bouton à bascule par `ui_w_set_value`.

## Construire

Sous ARMPI, depuis la fenêtre PiBuild ou un terminal ([GCC.md](GCC.md)) :

```sh
pibuild app /src/apps/letters letters
pibuild app /src/apps/sheet sheet
pibuild app /src/apps/slides slides
pibuild app /src/apps/cardfile cardfile
```

Letters inclut `../cardfile/model.h` et `../slides/jpeg.c` : ces dossiers doivent rester côte à côte, ce qui est le cas sur la carte.

Sur le PC, plus vite, dans WSL et depuis la racine du dépôt, avec les mêmes options que `pibuild.mk` :

```sh
sh tools/gcc/kits.sh
sh tools/office/build.sh
```

`build.ps1` met ensuite le résultat (`build/office/sdroot`) sur la carte.

## Essais

`tools/office/tryimg.py` donne à chaque essai sa propre carte, pour que plusieurs QEMU tournent ensemble sans refaire `build/sd.img`.

Temps de `pibuild` sous QEMU, pour une construction complète :

| App | Temps | Taille |
|---|---|---|
| Cardfile | 100 s | 316 Ko |
| Letters | 701 s | 701 Ko |
| Spreadsheet | 15 min 48 s | 910 Ko |
| Slides | 530 à 920 s | 800 Ko |

Chaque valeur vient d'un essai séparé. Les quatre à la suite, sur l'arbre final et outils préchargés : environ 25 minutes.
Sur le Pi, c'est à mesurer. Le code produit n'est pas identique à celui de la chaîne croisée, car les options de processeur
diffèrent, mais les tailles sont les mêmes. Les kits, Elegant, `pibuild` et PiBuild, eux, s'assemblent à l'octet près sous ARMPI.

## Ce qui manque

Le détail est dans le `README.md` de chaque app.

* **Polices** : DejaVu seulement (sans, serif, mono). Le nom d'origine est gardé à l'enregistrement, mais les lignes et les pages
  se coupent autrement que dans Word ou PowerPoint.
* **Impression** : par PrinterKit ([IMPRESSION.md](IMPRESSION.md)). Spreadsheet et Cardfile n'ont pas d'export PDF non plus.
  Celui de Slides est fait d'images.
* **Images** : PNG, BMP, JPEG ; GIF, WebP et PCX ne sont pas lus. Le décodeur JPEG vit dans `src/apps/slides/jpeg.c` (stb_image),
  partagé avec Letters : sa vraie place serait un kit d'images.
* **Spreadsheet** n'écrit pas ODS, comme dans Onyx.
* **Icônes** : des glyphes DejaVu au lieu des icônes vectorielles d'Onyx.
* **ZIP** : pas de chiffrement, pas de ZIP64 à l'écriture, pas de modification d'une archive existante.

## Pas essayé

* **Rien de la suite n'a tourné sur le vrai Pi** : tout est vérifié sous QEMU. Ni les temps, ni le tactile, ni l'usage en 800x600 réel.
* Word, Excel, PowerPoint et LibreOffice n'ont jamais relu les fichiers écrits : ils sont vérifiés par Python et par les apps
  elles-mêmes. Aucun `.odt` ni `.ods` écrit par LibreOffice n'a servi en entrée.
* Déposer un fichier sur une fenêtre, et quelques commandes d'usage rare (Remplacer, Insérer une image depuis un fichier, table
  des matières de Letters).
