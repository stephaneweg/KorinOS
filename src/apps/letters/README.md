# Letters

Le traitement de texte d'Onyx (`user/Apps/letters`) sur ARMPI, sur l'UIKit de l'OS, sans pont : aucune classe autour
d'un contrôle, aucune émulation de `kapi_*`, ni de l'uikit d'Onyx.

    letters [fichier]             ouvre un .rtf, .docx, .odt, .txt
    letters --merge JOB           les documents d'un publipostage (merge.h ; Cardfile le lance)

Fichiers de l'app : `SD0:/home/letters` (document de reprise, requête de publipostage). Documents : `SD0:/home/documents`.
Compilation : `pibuild app /src/apps/letters letters` (GCC natif) ou `wsl sh tools/office/build.sh letters`.
Il faut `../cardfile/model.h` (le modèle des fiches sert au publipostage), `documentkit`, `archivekit`, `uikit`.

## Repris d'Onyx

| Fichier | Origine |
|---|---|
| `doc.h` | tel quel, sauf `encode_text` toujours en UTF-8 |
| `edit.h` | tel quel, sauf l'insertion d'image et le presse-papiers (`ui_clip_set`, `ui_clip_get`) |
| `layout.h` | tel quel, sauf les polices (`font.h`) et le crénage |
| `fileio.h` (RTF, TXT, HTML) | tel quel, sauf les images |
| `docx.h`, `odt.h` | même code ; le lecteur `XmlReader` remplacé par les appels `dk_xml_*`, le ZIP par `ar_*` |
| `merge.h` | moteur repris ; dialogue réécrit |
| `pdfwrite.h` | `user/Libs/pdf/pdfwrite.h` ; le `deflate` et le CRC viennent d'ArchiveKit, le `Buf` est recopié |

## Écrit pour ARMPI

* `font.h` : les avances. Chaque caractère est mesuré une fois par `ui_text_width` à 1000 px, gardé (table par face pour
  les lettres latines, table de hachage pour le reste), puis mis à l'échelle : aucune mesure à la frappe, la coupure des
  lignes est la même à tous les zooms. Une frappe ne remet en page que le paragraphe touché (`Para::dirty`, comme Onyx).
* `view.h` : la page est une `ui_view` (pas de nouveau widget : `richtextbox` n'existe pas dans UIKit et n'a pas été ajouté),
  deux `ui_scrollbar`. Le texte va par mots (`ui_text`) ; le curseur clignote en inversant ses pixels, sans redessin.
* `ui.h` : les deux `ui_toolbar` (placement à la main : les listes rétrécissent, puis ce qui compte le moins quitte la
  ligne ; tout reste dans les menus), règle et barre d'état en `ui_view`.
* `dialogs.h` : les dialogues sur `ui_dialog_*`. `office.h` : fichiers POSIX, appels typés des kits, images, PNG.
* Les couleurs viennent des jetons de thème ; la page reste blanche, comme du papier.

`xml.h`, le ZIP et le PNG de `pngsave` ont disparu. `icons.h` aussi : les outils sont des glyphes DejaVu.

## Différences avec Onyx, ce qui manque

* **Polices** : trois familles seulement (DejaVu Sans, Serif, Sans Mono, celles de `ui_font_get`). Le nom de police du
  document est gardé à l'enregistrement, mais Times New Roman se dessine en DejaVu Serif, plus large : les pages ne coupent
  pas comme sous Word. Pas de mono italique. Pas de crénage dans la mise en page.
* **Tailles non entières** : `ui_text` prend des px entiers ; la position des mots est exacte, leur largeur peut différer
  d'un px ou deux à certains zooms.
* **Images** : BMP et PNG par `ui_image_decode`. **JPEG** : `jpeg.c`, le décodeur de Slides (stb_image) ; le fichier est gardé
  à l'identique à l'enregistrement et dans le PDF. GIF, WebP, PCX : refusés.
* **Impression** : Fichier > Imprimer, par PrinterKit (docs/IMPRESSION.md) et le code de l'export PDF (`pdfprint.h`).
* **Export PDF** : porté, polices DejaVu embarquées en sous-ensembles, signets depuis les titres, plage de pages. Sans
  « photos en JPEG » ni ouverture à la fin (pas de lecteur PDF) ; les caractères hors du plan de base (> U+FFFF) sortent
  en glyphe absent.
* **Dialogues** : pas d'aperçu dans Police, Paragraphe, Mise en page. Sélecteur de couleur : les pastilles d'UIKit, avec
  « Auto » / « None » à côté (dialogue Police, menu Format), à la place des palettes à double bouton. Pas de grille « insérer
  un tableau » sur la barre : le dialogue. Le choix de la police se fait dans une liste fixe plus celles du document.
* **Rechercher et remplacer** : le dialogue se ferme à chaque étape pour montrer la page, puis revient : un dialogue
  ARMPI est une seconde fenêtre et l'autre ne se repeint pas dessous. Même chose pour l'aperçu du publipostage.
* **Publipostage** : `merge.h` complet (fusion dans un document, ou en fichiers). Le document fusionné d'un clic s'ouvre
  dans un second Letters, lancé par `posix_spawn`. Cardfile : son menu Publipostage demande la lettre, tous les
  enregistrements ou celui-ci, et ouvre le résultat ; la sortie en fichiers reste au dialogue de Letters.
* **Mode pocket** (ligne d'aide, raccourcis de pavé) : absent. Fenêtre étroite : règle cachée sous 720 px, page à la largeur.
