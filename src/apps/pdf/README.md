# Lecteur PDF

Le lecteur de PDF d'Onyx (`user/Apps/pdf`, `docs/pdf/README.md` d'Onyx) sur ARMPI, sur l'UIKit de l'OS, sans pont.

**Licence : AGPL-3.0.** MuPDF (Artifex) est sous AGPL-3.0 ; l'application qui s'y lie l'est donc aussi, le reste de l'OS
n'en change pas (simple agrégat). Sources de MuPDF : `third_party/mupdf-1.28.5` d'Onyx, copiées dans `src/extra` (non suivi).

    pdf                 SD0:/apps/pdf.app/main : l'accueil
    pdf SD0:/x.pdf      ce document (app.txt : opens = pdf ; Fichiers l'ouvre ainsi)

Fichiers : `SD0:/home/pdf/settings.ini`, `SD0:/home/pdf/recent.tsv`.

Compilation, sur le PC seulement (`libmupdf.a` n'est pas sous `/gcc`, 9 Mo) :

    wsl sh tools/gcc/kits.sh            (les relais des kits)
    wsl sh tools/pdf/build.sh           (build/pdf/libmupdf.a, copie d'Onyx dans src/extra la première fois)
    wsl sh tools/office/build.sh pdf

MuPDF est réduit comme dans Onyx (`tools/pdf/mupdf.mk`) : fitz et pdf seulement, ni XPS, EPUB, HTML, SVG, ni JavaScript, ni
gestion de couleur ICC, ni polices CJK et Noto (une police incorporée s'affiche quand même, les 14 polices standard restent).
`HAVE_SIGSETJMP=0` : newlib n'a pas `sigsetjmp` et `__unix__` est défini.

## Repris et écrit

| Fichier | Origine |
|---|---|
| `engine.h` | d'Onyx ; fichiers POSIX, verrous et fil `pthread`, file relue par la boucle au lieu de `kapi_post` |
| `ui.h` | d'Onyx pour la géométrie des icônes ; jetons `ui_jetons`, tracé `ui_vp_*` |
| `main.cpp` | la logique d'Onyx, réécrite sur `ui_view`, `ui_tabstrip`, `ui_textbox`, `ui_dialog_*`, `ui_popup_local` |
| `data/lang-fr.txt` | le `lang/fr.txt` d'Onyx au format de `ui_tr_load` |

Rien n'a été ajouté à UIKit.

## Ce qu'il fait

Onglets, accueil (documents récents repris à leur page, dossiers), pages continues, une à une ou deux par deux, zoom (pleine
largeur, page entière, 50 à 400 %, Ctrl+molette), rotation, panneau Pages / Sommaire / Chercher, recherche à mesure de la frappe
(casse, mots entiers), sélection et copie (double-clic : un mot, triple : une ligne), liens internes et web (Jet), mots de
passe, Propriétés (Description, Polices, Sécurité), enregistrer une copie, plein écran (F5).

## Différences avec Onyx

* **Outil « main » (nouveau)** : un glisser sur la page la fait défiler au lieu de choisir du texte. Allumé par défaut sur un
  écran de 800 px de large au plus, pour le doigt ; le double appui choisit toujours un mot, et ⋮ ▸ Copier le copie.
* La liste du panneau glisse au doigt ; en plein écran un appui à gauche recule, à droite avance, en haut à droite sort.
* Plein écran sur **F5** : Elegant garde F10 et F11 (diagnostic).
* Fenêtre étroite (moins de 640 px) : le panneau passe par-dessus les pages, au lieu du tiroir `SidePanel` d'Onyx.
* Impression : Fichier > Imprimer, par PrinterKit (docs/IMPRESSION.md) : les pages par MuPDF à 300 points par pouce ; vers l'imprimante PDF avec toutes les pages, le fichier est copié.
* Pas d'annotations ni de formulaires, comme Onyx.
* La première page d'un document récent vient d'une ouverture à part par l'ouvrier, comme Onyx ; un document protégé n'en a pas.

Le dessin dans une vue ne s'imbrique pas : `ui_w_show` dessine aussitôt, donc rien qui passe par `layout_parts` ne doit
s'exécuter depuis un `view_paint` (sinon les dessins suivants partent dans les pixels de la vue).

Essayé sous QEMU seulement (manuels d'Onyx : Koton, Ledger). Sous QEMU une page prend plusieurs secondes ; à mesurer sur le Pi.
