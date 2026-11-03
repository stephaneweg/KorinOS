# Photos

La photothèque d'Onyx (`user/Apps/photos`, `docs/photos/README.md` d'Onyx) sur ARMPI, sur l'UIKit de l'OS, sans pont : une vue
est un `ui_view` et des fonctions, un contrôle une poignée `ui_*`. Licence MIT d'Onyx, gardée dans les fichiers.

    photos              SD0:/apps/photos.app/main
    photos SD0:/x.jpg   cette photo en grand

Dossiers suivis : `SD0:/home/pictures`, le `DCIM` de chaque volume (`SD0:`..`SD3:`, `USB0:`..`USB3:`), et ceux ajoutés à la main.
Fichiers : `SD0:/home/photos` (`library.db`, `thumbs/`, `albums/`, `folders.txt`, `outbox/` pour Mail). Corbeille :
`SD0:/home/.trash` (`docs/FEUILLE-DE-ROUTE.md`) ; Fichiers ne la montre pas encore, on y reprend une photo à la main.

Compilation : `wsl sh tools/office/build.sh photos` (rien que les kits et libposix ; `pibuild` sous ARMPI non essayé).

## Repris et écrit

| Fichier | Origine |
|---|---|
| `exif.h`, `imgops.h` | d'Onyx ; fichiers POSIX |
| `lib.h`, `thumbs.h` | d'Onyx ; fichiers POSIX, fils `pthread`, files relues par la boucle au lieu de `kapi_post` |
| `app.h` | d'Onyx |
| `grid.h`, `viewer.h`, `editor.h` | la logique et le dessin d'Onyx, sans classe de widget : `ui_view`, tracé `ui_vp_*`, pixels directs |
| `share.h` | d'Onyx ; `pdfwrite.h` et `pngsave.hpp` viennent de Letters et Paint, le PDF se dessine dans un `ui_canvas` caché |
| `ui.h` | d'Onyx pour les icônes et les dates ; jetons `ui_jetons` |
| `main.cpp`, `keys.h` | **réécrits** : boucle, disposition, menus. La souris se traite après `ui_w_event` : dialogues et menus s'y ouvrent |
| `image.cpp` | une ligne : les décodeurs de Paint (stb_image, simplewebp, PCX ; JPEG, PNG, GIF, BMP, WebP, PCX) |
| `data/lang-fr.txt` | le `lang/fr.txt` d'Onyx au format de `ui_tr_load` |

Rien n'a été ajouté à UIKit.

## Différences avec Onyx

* **Barre et volet du visionneur et de l'éditeur** : ils suivent le thème (Givre, Onyx Noir) ; seul le tour de la photo reste
  sombre. Onyx les avait tout sombres : les glyphes de fenêtre, fixés à l'ouverture, ne se liraient plus sur Givre.
* **Pas de « Fond d'écran »** : le fond de bureau d'ARMPI est `voronoy`, il n'y a pas de fond image.
* **Pas de « Copier »** : le presse-papiers d'ARMPI ne prend que du texte.
* **Impression** : Fichier > Imprimer (ou le menu de partage), par PrinterKit (docs/IMPRESSION.md) : une photo par page, en JPEG.
* **Corbeille** : un dossier simple (voir plus haut), Fichiers ne sait pas encore restaurer.
* Le volet de gauche est `ui_list` (`UL_SIDEBAR`) au lieu de `SidePanel` : sous 620 px il disparaît (rail et tiroir non portés), les
  menus de la barre donnent les mêmes vues. Le détail d'une photo est fermé par défaut sous 760 px.
* Diaporama sur F5, comme Onyx.

Essayé sous QEMU seulement, avec de petites images : bibliothèque, EXIF (date, appareil, orientation), date dans le nom, sélection,
album, favori, recherche, photo en grand, filtre et enregistrement (EXIF gardé), diaporama, PDF, Givre et Onyx Noir. Sur le Pi
restent à mesurer les photos de 12 Mpx (`load_full` décode tout : 48 Mo).
