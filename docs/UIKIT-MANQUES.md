# UIKit d'ARMPI face à l'uikit d'Onyx : ce qui manque

**Règle (30/10/2026) : un programme porté d'Onyx n'embarque pas l'uikit d'Onyx, et aucun pont.** Il appelle directement
les `ui_*` ; ce qui manque s'y ajoute, en fin de `uikit.abi`. Modèle : `src/apps/koton` (`ui/view.h`).

Ce fichier était la cartographie du 29/10/2026, quand UIKit n'était qu'une couche de dessin (version 35).
Ce n'est plus vrai : UIKit a ses widgets depuis (`uikit_widgets.inc`, poignées et fonctions `ui_w_*`, sur
le modèle de l'API C `flat.h` d'Onyx). Liste des fonctions : `docs/KITS.md`, « UIKit : les widgets » ;
démonstration : `src/apps/uidemo`. Reste ici l'ordre suivi, et ce qui manque.

## Ordre suivi

1. [x] **Barre de défilement et liste** (fait : `ui_scrollbar`, `ui_list*`, `ui_w_*`, version 60) (une ou plusieurs colonnes, sélection, double-clic, molette) :
   navigateur, front end FASMARM, applet Wi-Fi, lecteur, dock.
2. [x] **Champ texte** (fait : `ui_textbox*`, `ui_text_prefix`, `ui_text_hit`, presse-papiers d'Elegant `ui_clip_*`, version 70) (curseur, clic, sélection, presse-papiers ; avec la largeur d'un préfixe et un
   presse-papiers système) : tous les formulaires, la clé Wi-Fi.
3. [x] **Dialogues modaux** (fait : `ui_message`, `ui_ask`, `ui_file_open`, `ui_file_save`, `ui_folder_choose`, `ui_dialog_*`, avec bouton et étiquette, version 84) : message, saisie, puis ouvrir / enregistrer (sur 1 et 2).
4. [x] **Case à cocher, radio, interrupteur, liste déroulante** (fait : `ui_checkbox`, `ui_radio`, `ui_switch`, `ui_dropdown`, `ui_combobox`, version 89) : applets de réglages.
5. [x] **Texte multiligne** (fait : `ui_textarea*`, version 92) (d'après l'éditeur) : éditeur, FASMARM, suite bureautique.
6. [x] **Slider, barre de progression** (fait, avec nombre ±, molette, vu-mètre, LCD : version 99) : mixer, lecteur, volume.
7. [x] **Fenêtre redimensionnable, séparateur de volets** (fait : `WF_RESIZABLE`, `WEV_RESIZE`, `ui_resize`, `ui_min_size`, `ui_on_resize`, `ui_splitter`, version 110) : navigateur, suite bureautique.
8. [x] **Onglets, barre d'outils** (fait : `ui_tabs`, `ui_tabstrip`, `ui_segmented`, `ui_toolbar`, `ui_panel`, `ui_groupbox`, version 121).
9. [x] **Images** (imagekit : BMP, PNG d'abord) et visionneuse (fait dans UIKit : `ui_image_*`, `ui_imagebox`, version 128 ; visionneuse `src/apps/viewer` ; un imagekit à part pourra les reprendre avec GIF, JPEG).
10. [x] **Glisser-déposer, infobulles, formes du curseur, `TR()`** (fait, avec les menus : entrées grisées, cochées, raccourcis par le kit ; italique si la police existe ; version 140).

11. [x] **Plusieurs fenêtres par programme** (fait : `ui_window_use`, `ui_window_close`, `ui_window_count`, `ui_w_window`, rôle `WF_MODAL` d'Elegant, version 182) : les dialogues sont de vraies fenêtres qui bloquent les autres ; sélecteur de fichiers à hauteur d'Onyx. Premier prérequis de `System.Windows.Forms` (docs/DOTNET.md). Démonstration : `src/bin/multiwin`.

## État après le chantier des widgets (version 166 de `uikit.abi`, complété depuis)

Fait (docs/KITS.md, « UIKit : les widgets » ; démonstration `src/apps/uidemo`, onze pages) :
bouton à états, étiquette, case, radio, interrupteur, champ (mot de passe, presse-papiers),
texte multiligne et éditeur de code (numéros, barres, couleurs de l'assembleur, annuler /
rétablir, recherche), liste à colonnes et grille de données virtuelle, panneau latéral, liste
déroulante, liste modifiable, arborescence, barre de défilement, glissière, molette, nombre ±,
progression, vu-mètre, LCD, contrôle segmenté, onglets à pages, barre d'onglets de documents,
barre d'outils, séparateur de volets, panneau, cadre de groupe, mise en page (piles, grille),
canevas, formes anticrénelées, image (BMP, PNG) et boîte d'image, calendrier, sélecteur de
date, palette, sélecteur de couleur ; dialogues modaux (message, saisie, ouvrir, enregistrer,
dossier, couleur), plusieurs fenêtres par programme, fenêtre redimensionnable, glisser-déposer entre fenêtres, infobulles, formes
du curseur, `TR()`, menus (grisées, cochées, raccourcis par le kit), polices DejaVu (italique,
serif...), indications de manette, navigation entre objets. L'éditeur (`src/apps/editor`) et la
visionneuse (`src/apps/viewer`) les utilisent.

Reste (ou choisi autrement) :

* **`richtextbox`** (texte à styles par caractère : traitement de texte) : pas porté ; il
  demande un tampon d'attributs parallèle, des tailles multiples et la coupure en lignes à
  styles mêlés ; à faire avec la suite bureautique (les polices sont là : `ui_font_get`).
* **`sidepanel`** : seule la présentation « bureau » (liste à sections) ; les présentations
  PocketUI (rail, tiroir, feuille) attendent le mode PocketUI d'Elegant.
* **`objnav`** : la recherche du voisin ; les modes déplacer / redimensionner au clavier restent
  aux programmes (Slides, concepteur de formulaires).
* **`padhint`** : les glyphes sont dessinés (pastilles, croix) mais pas les jeux de boutons de
  chaque console.
* Déplacer sa fenêtre depuis le programme, fenêtre toujours au-dessus (mini-lecteur de Media Player : réduit sur place) ;
  dessiner par-dessus une surface attachée (contrôles de la vidéo : dans des barres autour d'elle).
* Redimensionnement « en direct » (Elegant change la taille au relâché du bord) ; plein écran.
* Images : GIF, JPEG, WebP, PCX (un imagekit à part) ; PNG entrelacé.
* Arborescence : glisser. (Grille de données : défilement horizontal fait, `UL_HSCROLL`.)
* Espaces de travail : le menu de fenêtre d'Onyx (Déplacer vers…, Sur tous les bureaux) manque ; une
  fenêtre reste sur l'espace où elle s'est ouverte.
* Focus clavier : les widgets des panneaux suivent l'ordre de création (pas d'ordre de tabulation
  à part).
