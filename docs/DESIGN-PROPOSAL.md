# ARMPI : revue de l'interface et proposition visuelle

Revue du 2/11/2026, sous QEMU, en Givre et en Onyx Noir, à 800x600 et 1024x768, sur le build du 1/11 (`c586c06`), puis sur `claude/onyx-apps2` (`e5bd0bd`), qui apporte les nouvelles icônes. Rien dans `src/` n'a changé. Tout le visuel est dans [`design-proposal/`](design-proposal/) : captures, prototypes, maquettes, paires avant/après, outils.

## Résumé

L'interface a déjà une bonne base : des jetons, un kit cohérent, une structure claire. Elle paraît datée pour cinq raisons précises, et aucune ne remet en cause le chrome sans barre, les deux thèmes ou le panel :

1. le fond d'écran est le plus fort contraste de l'écran et a la même couleur que l'accent ;
2. DejaVu Sans à 13 px, large et non hintée, donne une impression molle et prend de la place ;
3. les barres d'outils utilisent des caractères Unicode comme icônes ;
4. les widgets ont des dégradés, des coins non anticrénelés et des ombres de 4 px ;
5. sept apps et trois programmes système ignorent le thème.

**Recommandation : la direction A, « Givre calme ».** Elle garde toutes les décisions prises et apporte une grande partie du gain avec très peu de travail. La preuve est dans `design-proposal/prototype/` : ces captures sont l'OS réel, sans recompiler, avec seulement le tableau de jetons, la palette du fond et Inter patchés dans une copie privée de l'image.

Les 89 icônes de `tools/icons` règlent le plus gros problème d'icônes. Elles deviennent la base de la direction A : je les garde telles quelles, la couleur vit là et la coquille reste neutre. **Elles sont sur `claude/onyx-apps2`, pas encore dans `main` : cette fusion est le prérequis.**

Les trois premiers changements, après cette fusion : les jetons et le fond, puis Inter avec repli sur DejaVu, puis les glyphes des barres d'outils dans la chaîne `tools/icons`.

## État de la mise en œuvre

Sur la branche `claude/os-design-review-eae628`, avec `claude/onyx-apps2` fusionnée. Rien dans `main`. Essayé sous QEMU seulement, à 800x600, surtout en Givre, avec des essais en Onyx Noir. `build.ps1` passe. Captures de l'OS réel : [`design-proposal/mise-en-oeuvre/`](design-proposal/mise-en-oeuvre/).

| Étape | État |
|---|---|
| Prérequis : icônes de `claude/onyx-apps2` | fusionnées dans la branche |
| 1. Jetons et fond | fait, `theme.txt` sans couleurs, nettoyage de la carte dans `build.ps1 sd` |
| 2. Inter avec repli | fait : `F_FALLBACK`, DejaVu au premier glyphe absent, retour à DejaVu si Inter manque ; interligne 1,4. Italiques encore en DejaVu, tailles en dur pas reprises |
| 3. Glyphes | fait pour les barres d'outils : 37 glyphes, table `uk_tool_map`, sans toucher aux apps. Le format reste RGBA en trois tailles, 4 Ko par glyphe, sans passer à l'alpha seul |
| 4. Widgets | fait : coins anticrénelés, boutons plats, rayon 8, contrôles de 32 et lignes de 30, pas d'anneau autour des grandes zones. Les dix apps C++ qui recopiaient la hauteur en dur sont alignées |
| 5. Chrome et composition | fait. RDP pas réessayé, gain NEON pas mesuré sur le Pi |
| 6. Coquille | fait : panel de 48 px, corbeille en glyphe, lanceur à rail et feuille opaque, volets du son et du Wi-Fi à vraie ombre, applets Rendez-vous et Notes translucides, à place fixe sous la barre de menus, sans déplacement. La proposition d'une carte unique, opaque, est écartée par l'utilisateur |
| 7. Apps | fait : Réglages en lignes, Fichiers (en-tête, précédent, suivant, fil d'Ariane, vues), sept apps sur les jetons, zones des boutons libres dans Sheet, Slides et QBStudio, recherche de Cardfile décalée |
| Français | `res/lang/fr.txt` : menus, infobulles, boutons et étiquettes de Letters, Sheet, Slides et GameLib, traduits par UIKit, sans recompiler. Restent en anglais les textes composés par les apps (barres d'état, « Untitled ») et les panneaux de Slides, qui ne passent pas par ces widgets. Cardfile, hors demande, reste en anglais, sauf les mots communs du dictionnaire |

`WS_MENU_MAX` passe à 4096 octets et la barre de menus garde 130 px à droite au lieu de 200 : les menus français sont plus longs.

Reste à essayer sur le Pi : lisibilité d'Inter, gain NEON (F11), RDP, ouverture du lanceur. Les fichiers Inter viennent d'une installation locale : à remplacer par les fichiers officiels.

**Couleur d'accent :** huit choix dans Réglages, Thème (bleu, rouge orangé, vert vif, jaune, violet, fuchsia, sarcelle, graphite), une valeur par thème. `theme.txt` : `accent = <nom>` ou `0xRRGGBB`. UIKit dérive les nuances et `TJ_ACCENT_INK`, sombre sur les accents clairs. Les apps qui écrivent en blanc en dur sur l'accent gardent ce blanc.

## 1. Comment la revue a été faite

* QEMU sur une copie privée de `build/sd.img`, ports à part, sans préchargement de Jet. La carte SD et `main` n'ont pas été touchés.
* 39 captures retenues sur une centaine, dans les deux thèmes pour l'essentiel. Jet n'est pas capturé : sa fenêtre n'était pas ouverte au moment du cliché.
* Sources lues : `uikit_theme.inc`, `uikit_controls.inc`, `uikit_widgets.inc`, `uikit_frame.inc`, `uikit.asm`, `ttf.asm`, `elegant/core.inc`, `panel.asm`, `launcher.inc`, `voronoy.asm`, `DESIGN-ELEGANT.md`.
* **Rien n'a été essayé sur le PocketTerm.** Les temps de la section 6 sont des estimations, pas des mesures. Le rendu à 285 ppi reste à juger sur l'appareil, surtout pour Inter, dont les traits sont plus fins que ceux de DejaVu.

Il y a trois niveaux de preuve dans ce document :

| Dossier | Nature |
|---|---|
| `captures/` | l'OS actuel |
| `prototype/` | l'OS réel, dans une image privée. Build `c586c06` : `p*` jetons et fond, `q*` en plus Inter. Build `e5bd0bd` : `i*` tel quel avec les nouvelles icônes ; `r*_desk` jetons et fond ; `r*_control`, `r*_files` en plus Inter |
| `maquettes/` | images dessinées par `outils/screens.py` aux pixels réels, avec les vraies polices et les vraies icônes de `tools/icons`, sans hinting comme `ttf.asm` ; elles supposent les étapes 3 à 7 faites |

## 2. Diagnostic

### Hiérarchie

* **Le fond domine tout.** Les facettes bleues et grises, à fort contraste, passent devant les fenêtres dans l'ordre de lecture. Le bleu du fond est celui de `TJ_ACCENT` : l'accent ne signale plus rien. [Bureau](design-proposal/captures/g800_shell.png), [lanceur](design-proposal/captures/g800_launcher.png).
* **Le bouton « apps »** est l'objet le plus saturé de l'écran alors qu'il sert peu. Rien ne ramène l'œil vers la fenêtre active.
* **Fenêtre active et fenêtre inactive se ressemblent.** Mêmes couleurs, même ombre de 4 px, mêmes glyphes ou presque. [Deux fenêtres](design-proposal/captures/g800_two_b.png). Sans barre de titre, c'est l'ombre et les glyphes qui doivent porter ce signal.
* **Réglages : le contenu reste en haut à gauche**, avec un grand vide dessous et « Appliquer » seul en bas. [Thème](design-proposal/captures/g800_control.png).

### Typographie

* Une seule famille installée pour l'interface, DejaVu Sans, large : les titres de catégorie du lanceur passent à la ligne, « Game Boy Advan » est coupé.
* Les tailles sont trop proches pour hiérarchiser : 11, 12, 13, 15. Le gras en fait presque seul le travail.
* `ui_text_wrap` impose un interligne de 1,5 : deux lignes d'aide paraissent deux paragraphes ([Thème](design-proposal/captures/g800_control.png)).
* Il y a 71 tailles en dur dans `uikit_*.inc`, et `ui_button` écrit encore en 14 px.

### Espacements et densité à 800x600

* Marges et rythmes varient d'une app à l'autre : Calculatrice sans marge haute, Bienvenue avec bandeau, Réglages et Fichiers avec des marges différentes.
* **Choisir dans 15 boutons radio.** Écran montre 15 résolutions sur deux colonnes, avec un seul élément dans la seconde. Région en empile 19 ([Région](design-proposal/captures/g800_ctl_region.png)). Une liste déroulante tient en une ligne, et le tactile y gagne.
* Dans le lanceur, la neuvième catégorie reste seule sur une seconde ligne.
* Barre de menus et panel prennent 80 px sur 600, soit 13 %.

### Couleurs et contraste

Rapports WCAG calculés sur les jetons (`outils/tokens.py`). Le seuil est 4,5 pour du texte.

| Paire | Givre actuel | Givre proposé | Noir actuel | Noir proposé |
|---|---|---|---|---|
| `TEXTE1` / `FACE` | 15,3 | 16,6 | 15,5 | 15,5 |
| `TEXTE2` / `FACE` | 5,5 | 7,0 | 7,7 | 8,8 |
| `TEXTE3` / `FACE` | **2,9** | 5,2 | **4,0** | 5,7 |
| `TEXTE3` / `CREUX` | **2,7** | 4,8 | **3,5** | 6,0 |
| blanc / `ACCENT` | **4,47** | 5,5 | **3,7** | 4,6 |
| blanc / `DANGER` | **3,9** | 4,5 | **3,9** | 4,5 |

* `TEXTE3` sert à des textes qu'il faut lire (aide, sous-titres, placeholders). En Givre il est loin du seuil.
* En Onyx Noir, `SURFACE` (`0x0B0C0E`) est plus sombre que `FACE` : listes et champs paraissent des trous noirs, et [Fichiers](design-proposal/captures/n800_files.png) a un panneau quasi noir à côté d'une barre grise. En sombre, ce qui monte doit s'éclaircir.
* Menu et panel sont transparents à 10-20 % sur un fond chargé : le texte n'a pas de contraste stable.

### Cohérence entre apps

* **Sept apps et trois programmes système ignorent le thème.** `ui_button` et les constantes `UI_*` sont figés à la compilation. Calculatrice reste [claire en Onyx Noir](design-proposal/captures/n800_calculator.png). Touchés : calculator, clock, monitor, piano, recorder, targets, welcome, soundmenu, wifimenu, notifyd.
* **Le bouton carré** de `ui_button` côtoie le bouton arrondi de `ui_pushbutton` ([Bienvenue](design-proposal/captures/g800_welcome.png)). Le [volet du son](design-proposal/captures/g800_sound.png) a un contour noir et un bouton gris : il ne ressemble à rien d'autre.
* **Trois langues d'interface.** Fichiers et Mail sont en français ; Letters, Sheet, Slides et GameLib en anglais. Dans Letters, le menu de l'app dit « Quitter » à côté de « File, Edit » ([Letters](design-proposal/captures/g800_letters_menu.png)).
* Slides propose encore les thèmes Peach, Steel, Sage, Brick et Slate ([Slides](design-proposal/captures/g800_slides.png)) : ce sont des thèmes de document, mais les noms prêtent à confusion avec ceux qui ont été retirés.
* Le panneau gauche change d'une app à l'autre : blanc sur gris dans `uidemo`, gris dans Fichiers et Mail.

### États

* Le panel n'a pas d'état de survol : seule une bulle apparaît. Le trait d'app ouverte est très fin.
* Le focus de l'éditeur encadre toute la zone de texte d'un double trait bleu : c'est le signal le plus fort de la fenêtre pour une information banale.
* Boutons, champs et menus ont leurs états, et ils sont bons. Le survol des listes aussi. La base est saine.

### Icônes

Le diagnostic a changé en cours de revue. Les captures de `captures/` montrent l'ancien état ; celles de `prototype/i*` montrent l'état de `claude/onyx-apps2`.

* **Apps : réglé.** 89 images à 12 tailles, d'une même main, avec un maître allégé sous 26 px. Plus d'initiales, et Réglages a de vraies icônes ([Réglages](design-proposal/prototype/ig_control.png), [lanceur](design-proposal/prototype/in_desk_launcher.png)).
* **Système : réglé en partie.** `ui_glyph` colorie six glyphes par un jeton du thème. La corbeille reste une tuile au milieu de glyphes monochromes, et l'engrenage noir côtoie une alimentation rouge.
* **Barres d'outils : reste à faire.** Elles utilisent des caractères Unicode comme icônes : ⇧ ⇩ pour ouvrir et enregistrer dans Letters, sans sens ni épaisseur commune, et avec un rectangle vide vers x = 297 à vérifier.
* **Tuiles sombres en Onyx Noir.** Terminal, Calculatrice et Corbeille se fondent presque dans le panel (`0x1F2228`). Le liseré de `art.py` gagnerait à être plus clair sur ces trois-là.
* Dossiers dégradés dans Fichiers : à voir avec le reste de la série.

### Défauts relevés en passant

Ils ne relèvent pas du design et ne sont pas corrigés ici.

| Où | Constat |
|---|---|
| Agenda, Notes | au démarrage, le widget passe parfois sous la barre de menus, son titre coupé ([capture](design-proposal/captures/g1024_shell.png)) ; selon les démarrages, la place est bonne |
| Sheet | le `+` du zoom recouvre réduire : la barre d'outils ne laisse pas `FRAME_CAP_W` libres ([zoom](design-proposal/captures/sheet-zone-boutons.png)) |
| Dialogues | les modaux portent le glyphe agrandir ([Message](design-proposal/captures/g800_dlg_msg.png)) |
| Éditeur | dans mon essai, Ctrl+O affiche « Erreur -2 » dans la barre d'état au lieu d'un dialogue, et fermer un document modifié n'a rien demandé |
| Letters | sous Inter, les glyphes absents donnent une tuile rayée (`.notdef`) : voir étape 2 |

## 3. Deux directions

### A. Givre calme (recommandée)

Le contenu d'abord, la coquille s'efface. Mêmes structures, mêmes thèmes, plus de retenue : un seul accent qui veut dire quelque chose, un fond ton sur ton qui garde les facettes, des surfaces opaques, de vraies ombres.

Elle garde le panel et met en valeur les nouvelles icônes : sur une coquille neutre, elles deviennent la seule couleur vive de l'écran avec l'accent. Elle se fait par étapes, avec un gain visible à chacune.

### B. Instrument

L'OS comme appareil de mesure de poche. Monochrome, un orange signal, 4 ou 0 px de rayon, filets au lieu d'ombres, grille de points en fond, chiffres en mono, panel à la manière d'une liste de tâches, listes denses.

C'est la plus marquée, la plus dense (Fichiers en lignes de 27 px) et la moins coûteuse à composer : aucune transparence. Elle a une vraie identité pour une machine comme le PocketTerm. Elle demande en revanche de tout refaire d'un coup, laisse moins de place au doigt, et surtout elle va contre les 89 icônes qui viennent d'être faites : les maquettes B les remplacent par des traits monochromes.

| | A | B |
|---|---|---|
| Bureau | [Givre](design-proposal/maquettes/a-givre-bureau.png), [Noir](design-proposal/maquettes/a-noir-bureau.png) | [Givre](design-proposal/maquettes/b-givre-bureau.png), [Noir](design-proposal/maquettes/b-noir-bureau.png) |
| Réglages | [Givre](design-proposal/maquettes/a-givre-reglages.png), [Noir](design-proposal/maquettes/a-noir-reglages.png) | [Givre](design-proposal/maquettes/b-givre-reglages.png), [Noir](design-proposal/maquettes/b-noir-reglages.png) |
| Fichiers | [Givre](design-proposal/maquettes/a-givre-fichiers.png), [Noir](design-proposal/maquettes/a-noir-fichiers.png) | [Givre](design-proposal/maquettes/b-givre-fichiers.png), [Noir](design-proposal/maquettes/b-noir-fichiers.png) |

**Pourquoi A.** Elle respecte ce qui est décidé, garde le travail sur les icônes, chaque étape se livre seule, et le premier gain se voit dès l'étape 1. B reste possible plus tard : si les jetons et les métriques sont bien en place, ce sera un troisième jeu de valeurs plus un chrome. On peut aussi lui emprunter tout de suite une idée : les chiffres alignés et la vue liste dense dans Fichiers.

## 4. La direction A en détail

### Principes

1. **La couleur vit dans les icônes.** Tout le reste est neutre, sauf l'accent, qui marque ce qui est choisi ou l'action principale.
2. **Opaque par défaut.** La transparence coûte du temps de composition et du contraste ; elle ne reste que sur le voile du lanceur et les ombres.
3. **L'élévation se lit.** Plus un objet est haut, plus il est clair en sombre, et plus son ombre est grande.
4. **Le dessin suit le pixel.** Formes sur la grille de 4, filets de 1 px, coins anticrénelés, rien de flou.
5. **La rapidité tient lieu d'animation.**

### Palette

Valeurs prêtes pour `uk_jet_givre` et `uk_jet_noir`, dans `outils/tokens.py`. Les valeurs en gras changent.

| Jeton | Givre actuel | **Givre** | Noir actuel | **Noir** |
|---|---|---|---|---|
| `FACE` | `F5F6F8` | `F5F6F8` | `15171B` | `15171B` |
| `SURFACE` | `FFFFFF` | `FFFFFF` | `0B0C0E` | **`1B1E23`** |
| `CREUX` | `EBEEF2` | **`ECEEF2`** | `1F2228` | **`101216`** |
| `ELEVE` | `FFFFFF` | `FFFFFF` | `2C3038` | **`262A31`** |
| `ELEVE_BAS` | `F4F5F7` | **`FFFFFF`** | `2C3038` | **`262A31`** |
| `LIGNE` | `DDE1E7` | **`E2E5EA`** | `343842` | **`2A2E36`** |
| `LIGNE_FORTE` | `C3C9D2` | **`B8BFC9`** | `4A505C` | **`525966`** |
| `TEXTE1` | `1B1F24` | **`14171C`** | `ECEEF2` | `ECEEF2` |
| `TEXTE2` | `5B6472` | **`4C5563`** | `A4AAB5` | **`B0B6C0`** |
| `TEXTE3` | `8A93A0` | **`5F6877`** | `717783` | **`8B929D`** |
| `ACCENT` | `2F74E0` | **`2563D8`** | `3D86DA` | **`2F72DC`** |
| `ACCENT_CLAIR` | `DCE8FB` | **`E4ECFB`** | `1E3350` | **`1C2F4D`** |
| `ACCENT_FONCE` | `245DBA` | **`1D4FB3`** | `2F6DB5` | **`2560C0`** |
| `ACCENT_VIF` | `2F74E0` | **`2563D8`** | `7DB4F2` | `7DB4F2` |
| `DANGER` | `E5484D` | **`D93A3F`** | `E5484D` | **`D93A3F`** |
| `SUCCES` | `30A46C` | **`1E8A5A`** | `46A758` | `46A758` |
| `ALERTE` | `F1B33B` | **`C4861C`** | `F1B33B` | `F1B33B` |
| `BARRE` | `1AF7F8FA` | **`08F7F8FA`** puis opaque | `1A15171B` | **`0815171B`** puis opaque |
| `DOCK` | `33F2F4F7` | **`08F5F6F8`** puis opaque | `261F2228` | **`081B1E23`** puis opaque |
| `MENU` | `FFFFFF` | `FFFFFF` | `1F2228` | **`262A31`** |

En Noir, un texte accentué sur fond sombre passe par `ACCENT_VIF` : `ACCENT` y fait 3,6, il est réservé aux aplats à texte blanc. `LIGNE_FORTE` reste sous 3:1 ; c'est voulu pour garder des contours discrets, et les champs ont aussi un fond et un libellé.

Nouveaux jetons, ajoutés après `TJ_MENU` : `TJ_SURVOL` (`E9ECF1`, `23272E`), `TJ_OMBRE_F` (opacité de l'ombre).

Fond, cinq couleurs :

* Givre : `D6DEE9 DCE3ED D1DAE6 E1E7EF CDD7E4`, en remplacement de `pal_givre` ;
* Noir : `1A1E26 1D222B 181C23 20252F 1B2029`, en remplacement de `pal_noir`.

Les facettes restent la signature d'ARMPI, ton sur ton. Le liseré passe de `ton 150` à environ `ton 134`.

### Typographie

**Inter** Regular et SemiBold pour l'interface, DejaVu Sans Mono pour le code et le terminal, Liberation et les autres polices de document inchangées.

* **Elle marche déjà** : `ttf.asm` lit les fichiers Inter tels quels ([capture réelle](design-proposal/prototype/qg_control.png), [Fichiers](design-proposal/prototype/qg_files.png)).
* **Elle est plus étroite** : sur des libellés français, environ 5 % de moins que DejaVu Sans, à taille égale.
* **Elle pèse moins** : 310 et 316 Ko contre 757 et 706 Ko. Chaque process lit et garde ses polices : 1,80 Mo aujourd'hui, 0,97 Mo avec Inter, soit 830 Ko de moins par app, et moins de lecture SD à chaque lancement.
* Licence OFL, compatible avec DejaVu et Liberation dans `build/sdroot/fonts`. À prendre à la source officielle ; le fichier utilisé ici pour l'essai vient d'une installation locale.

| Rôle | Police | px | Interligne | Couleur |
|---|---|---|---|---|
| Titre de page | SemiBold | 20 | 26 | `TEXTE1` |
| Titre de section, dialogue | SemiBold | 16 | 22 | `TEXTE1` |
| Texte, menus, libellés | Regular | 13 | 18 | `TEXTE1` |
| Nom d'app, élément choisi | SemiBold | 13 | 18 | `TEXTE1` |
| Secondaire, état, en-têtes | Regular ou SemiBold | 12 | 16 | `TEXTE2`, `TEXTE3` |
| Code, terminal, chemins | DejaVu Sans Mono | 13 | 18 | `TEXTE1` |

Quatre tailles, plus de 11 ni de 15. Interligne des textes coupés : 1,4 au lieu de 1,5.

### Grille, rayons, ombres

**Grille de 4 px.** Pas utiles : 4, 8, 12, 16, 24. Marge de fenêtre 16 à 800x600 et plus, 12 en échelle P.

| Métrique (L / M / P) | Aujourd'hui | Proposé |
|---|---|---|
| Contrôle | 30 / 28 / 26 | 32 / 32 / 28 |
| Ligne de liste | 28 / 26 / 24 | 30 / 30 / 26 |
| Ligne de menu | 28 / 26 / 24 | 30 / 30 / 26 |
| Barre de menus | 30 / 28 / 26 | 28 / 28 / 26 |
| Panel | 52 env. | 48 |

| Rayon | Usage |
|---|---|
| 6 | lignes de liste, cases, bulles |
| 8 | boutons, champs, onglets |
| 10 | fenêtres, menus, cartes |
| 12 | dialogues |
| 14 | lanceur |
| moitié du côté | interrupteurs, curseurs |

| Niveau | Objet | Ombre (Givre / Noir) |
|---|---|---|
| 0 | fond | aucune |
| 1 | panel, barre de menus, carte de bureau | filet `LIGNE` |
| 2 | fenêtre inactive | 2 px, flou 6, 10 % / 25 % |
| 3 | fenêtre active, menu | 4 px, flou 14, 16 % / 42 %, plus 1 px, flou 3, 10 % / 30 % |
| 4 | dialogue, lanceur | 6 px, flou 18, 19 % / 50 % |

Le flou est calculé une fois, dans l'image du cadre, comme les anneaux d'aujourd'hui : aucun flou à la composition. Il faut 12 px de bord (section 6).

### Icônes

Trois familles, une seule chaîne de fabrication, `tools/icons` :

| Famille | État | Rendu |
|---|---|---|
| Icônes d'apps et d'applets | faites, à garder | 89 images, colorées ; panel 32, lanceur 48, Réglages 20 |
| Glyphes système | six faits | monochromes, coloriés par `ui_glyph` ; ajouter corbeille, son, réseau, Wi-Fi |
| Glyphes d'outils | à faire | monochromes, 16, 18 et 20 px, environ 60 à 200 selon les apps |

**Je ne recommande plus une police d'icônes** : `ui_glyph` et `art.py` font déjà le travail, avec un maître dédié aux petites tailles, ce qu'une police sans hinting ne donne pas. Il reste à adapter le format.

* Un glyphe système pèse 49 Ko pour ses 13 tailles. Trente glyphes dans Letters feraient 1,5 Mo lus et gardés.
* Glyphes d'outils en trois tailles seulement : environ 4 Ko chacun, 1 Ko si le format passe en alpha seul (`ARIC` version 2, à ajouter sans casser la version 1).
* Style : trait de 1,5, bouts ronds, sur la grille de 24, à calquer sur `glyph-gear` et `glyph-power`.

Une petite amélioration à `art.py` : éclaircir le liseré des tuiles sombres en Onyx Noir. Rien d'autre à changer aux icônes.

### Mouvement

Un Pi 4 sans GPU côté bureau ne doit pas animer de grandes surfaces. Règle : **réponse sous 50 ms, aucun fondu**.

* Survol, appui, focus, menus, dialogues : immédiats.
* Lanceur : apparaît d'un coup. Une translation de 8 px en 3 images est envisageable après mesure.
* Déplacement de fenêtre : le contenu suit.
* Aucun flou, aucun fondu d'opacité plein écran.

### Composants

Planche : [Givre](design-proposal/maquettes/a-givre-composants.png), [Noir](design-proposal/maquettes/a-noir-composants.png).

| Composant | Rendu |
|---|---|
| Bouton standard | `ELEVE` uni, contour `LIGNE_FORTE`, rayon 8, sans dégradé ; survol `TJ_SURVOL` ; appui `CREUX` |
| Bouton principal | `ACCENT` uni, sans contour, SemiBold ; survol et appui `ACCENT_FONCE` ; un seul par vue |
| Bouton fantôme | rien au repos ; survol `TJ_SURVOL` |
| Désactivé | `CREUX` ou contour `LIGNE`, texte `TEXTE3` |
| Focus | anneau de 2 px `ACCENT` à 45 %, à 1 px du contrôle ; jamais autour d'une grande zone de contenu |
| Champ | `SURFACE`, contour `LIGNE_FORTE`, focus : contour `ACCENT` et anneau |
| Liste | ligne 30, survol et sélection en `boîte` rayon 6, retrait de 6 px ; sélection avec focus `ACCENT`, sans focus `ACCENT_CLAIR` |
| Panneau latéral | `CREUX` pleine hauteur, partout le même ; choix en `ACCENT_CLAIR` ; icônes colorées de 20 px pour des apps ou applets, glyphes pour des lieux |
| Onglets | rail `CREUX`, segment actif `SURFACE`, rayon 8 |
| Menus | `MENU`, niveau 3, rayon 10, ligne 30, élément actif `ACCENT` |
| Dialogues | titre 16, texte `TEXTE2`, boutons à droite, pas de glyphe agrandir |
| Bulle | fond `TEXTE1`, texte `FACE`, rayon 6 |
| Barre d'état | 24 px, 12 px, `TEXTE2` |

### Coquille

**Barre de menus, 28 px.** Quasi opaque puis opaque, filet `LIGNE`. Losange accent, nom de l'app en SemiBold, menus en `TEXTE2`. À droite, son et réseau monochromes, heure en `TEXTE1`, date dès l'échelle L.

**Panel, 48 px.** Une seule surface, filet en haut. [Bureau](design-proposal/avant-apres/bureau.png).

* Gauche : bouton « apps » fantôme, `glyph-apps` en `TEXTE1`, sans la pastille bleue.
* Centre gauche : les icônes de `tools/icons` en 32 px, pas de 40, trait d'app ouverte de 12 × 3.
* Droite : espaces de travail en un seul contrôle segmenté de 26 px, puis corbeille, presse-papiers, réglages, alimentation, tous monochromes en `TEXTE2`, 20 px, y compris une corbeille en glyphe. L'alimentation ne devient rouge qu'au survol.
* Survol : fond `TJ_SURVOL` en plus de la bulle.

**Lanceur.** [Avant/après](design-proposal/avant-apres/lanceur.png).

* Voile sur le bureau, feuille opaque `FACE` de 784 × 508, rayon 14.
* Catégories dans un rail à gauche, 34 px par ligne : les neuf tiennent et le doigt les atteint.
* Recherche de 38 px avec icône, grille de 5 colonnes, icônes de 48, libellés d'une ligne avec « … ».
* Les widgets de bureau ne transparaissent plus.

**Fenêtres.** Chrome sans barre conservé, rayon 10, ombres des niveaux 2 et 3.

* Active : glyphes `TEXTE2`, grande ombre. Inactive : glyphes `TEXTE3`, petite ombre.
* Survol des glyphes : `TJ_SURVOL` ; fermer : `DANGER`, glyphe blanc.
* `FRAME_CAP_W` reste libre. Les modaux n'ont que fermer.

**Squelette d'app.** Tout écran suit le même plan : panneau `CREUX` éventuel, en-tête de 44 px avec la zone des glyphes libre, contenu `SURFACE`, barre d'état de 24 px. C'est la règle qui rendra les apps cohérentes.

**Bureau.** Une seule carte, opaque, en haut à droite, seulement si elle a du contenu. Aujourd'hui, deux cadres translucides annoncent qu'il n'y a rien.

### Écrans

| Écran | Changement | Comparer |
|---|---|---|
| Bureau | fond calme, carte unique, panel neutre, Calculatrice aux jetons | [Givre](design-proposal/avant-apres/bureau.png), [Noir](design-proposal/avant-apres/bureau-noir.png) |
| Lanceur | feuille opaque, rail, icônes de 48 | [Givre](design-proposal/avant-apres/lanceur.png), [Noir](design-proposal/avant-apres/lanceur-noir.png) |
| Réglages | titre 20, lignes libellé + contrôle, listes déroulantes, Annuler et Appliquer | [Givre](design-proposal/avant-apres/reglages.png), [Noir](design-proposal/avant-apres/reglages-noir.png) |
| Fichiers | fil d'Ariane, vues icônes et liste, volumes et lieux monochromes, dossiers plats | [Givre](design-proposal/avant-apres/fichiers.png), [Noir](design-proposal/avant-apres/fichiers-noir.png) |
| Letters | icônes dessinées, groupes séparés, deux rangées alignées, règle calme, page sur fond `CREUX` | [Givre](design-proposal/avant-apres/letters.png), [Noir](design-proposal/avant-apres/letters-noir.png) |
| 1024x768 | mêmes règles, plus d'espace | [Bureau](design-proposal/maquettes/a-givre-bureau-1024.png), [Fichiers](design-proposal/maquettes/a-noir-fichiers-1024.png) |

## 5. Les décisions déjà prises : ce que je signalerais

Elles sont toutes gardées. Trois points méritent un regard.

1. **Sans barre de titre, une fenêtre sans en-tête propre n'a plus de nom.** Dans Calculatrice ou un dialogue, seul le menu global le dit, et seulement pour la fenêtre active. Le chrome n'est pas en cause. Proposition légère : `ui_title` affiche le titre en 13 px dans les 32 px virtuels des fenêtres sans en-tête. Coût : UIKit seul.
2. **`FRAME_CAP_W` (102 px) et le tactile.** Sheet déborde déjà dessus à 800 px. Par ailleurs, les boutons font 28 × 26 alors que `DESIGN-ELEGANT.md` vise 32 px au doigt. Je laisserais la taille et je corrigerais les barres d'outils, puis je déciderais sur l'appareil.
3. **La transparence de la barre et du panel.** Elle est dans l'esprit d'origine, mais elle baisse le contraste et oblige à recomposer sous le panel. Avec un fond calme, elle ne se voit presque plus : autant les rendre opaques.

## 6. Ce que ça demande

**Nature** : J jetons seuls ; K UIKit ; E Elegant ; A app par app. **Taille** : S moins d'un jour, M quelques jours, L une à deux semaines.

Les temps viennent d'un calcul, pas d'une mesure : `bb_blit_any` mélange pixel par pixel, en scalaire, sur trois canaux, soit environ 15 ns par pixel sur un A72 à 1,5 GHz. À vérifier avec F11 sur le Pi.

| Changement | Nature | Taille | Coût à l'exécution |
|---|---|---|---|
| Jetons Givre et Noir | J | S | nul |
| Palette et liseré du fond | `voronoy` | S | nul, dessiné une fois |
| Inter, chemins de `uikit.asm` | K | S | **gain** : 830 Ko et lecture SD par process |
| Repli sur DejaVu pour glyphe absent | K | M | nul tant que le repli ne sert pas |
| `uk_mt_tab` : métriques | K | S | nul ; à vérifier dans les apps à géométrie fixe |
| Interligne, tailles en dur | K | M | nul |
| Glyphes d'outils dans `tools/icons`, format alpha | `art.py`, K | M | environ 1 à 4 Ko par glyphe lu, gardés par programme |
| Glyphes système manquants | `art.py`, panel, menubar | S | nul |
| Liseré des tuiles sombres | `art.py` | S | nul |
| Boutons plats, champs, focus | K | S | **gain** léger, moins de dégradés |
| `ui_rbox` anticrénelé | K | M | environ 30 pixels mêlés par coin, négligeable |
| `ui_button` redirigé vers les jetons | K | S | nul |
| `FRAME_BORDER` 4 → 12, `FRAME_RADIUS` 10 | K, E | M | 0,5 ms au lieu de 0,15 ms pour les bords d'une grande fenêtre redessinée ; +5 % de mémoire de cadre |
| Mélange alpha NEON dans `bb_blit_any` | E | M | **gain** de 6 à 8 fois sur toute transparence (estimé) |
| Barre et panel opaques | J, panel | S | **gain** de 0,9 ms par recomposition complète |
| Panel : pas, groupes, survol | panel | M | nul |
| Lanceur, rail et feuille opaque | panel | M | **gain** : 6 ms de mélange en moins à l'ouverture |
| Volets son et Wi-Fi, widgets | sys | M | nul |
| Lignes de réglages, liste déroulante | K puis A | M | nul |
| Sept apps sur les jetons | A | M | nul |
| Fichiers : en-tête, fil d'Ariane | A | M | nul |
| Barres d'outils de Letters, Sheet, Slides, Mail | A | L | nul |
| Français dans les apps anglaises | A | M | nul |

Pièges à connaître :

* **`voronoy` reconnaît le thème en comparant `TJ_FACE` à des constantes.** Changer `FACE` ferait retomber sur la palette calculée. La table garde les valeurs actuelles, mais il vaut mieux ajouter un test sur `TJ_SOMBRE`.
* **`theme.txt` prime sur les jetons.** La carte actuelle contient `accent`, `menubar`, `dock`, `active` ; sans nettoyage, ces clés écrasent les nouvelles valeurs. Il faut réécrire `SD0:/etc/theme.txt` à la mise à jour, avec `theme = <nom>` seul, ce qu'écrit déjà le panneau.
* **Inter n'a pas les symboles de DejaVu.** Le prototype montre une tuile rayée pour ⇧, ⇩ et d'autres ([Letters](design-proposal/prototype/qg_letters.png)). `ui_glyph` a déjà réglé ce cas pour le panel ([capture](design-proposal/prototype/rn_control.png)), pas pour les barres d'outils. Le repli doit arriver **avec** Inter. Il ne charge DejaVu qu'au premier glyphe manquant.
* **`ttf.asm` ignore le crénage.** Le prototype reste bon ; c'est à revoir sur l'appareil.
* **`FRAME_BORDER` est aussi dans** `control.asm`, `files.asm`, `rdpd.asm`, `elegant/remote.inc`, `elegant/screen.inc`, `uikit_dialog.inc`, `uikit_window.inc` et `uikit_frame.inc`. C'est un changement à rebuild complet, et RDP est à réessayer. Après fusion, `tools/gcc/kits.sh`, `tools/office/build.sh` et `tools/webkit/stage.sh`.
* **Tables en ajout seul.** Toute nouvelle fonction va en fin d'`uikit.abi`, et `TJ_SURVOL`, `TJ_OMBRE_F` après `TJ_MENU`. `TJ_SIZE` grandit : vérifier tout programme qui copie les jetons à taille fixe, à commencer par l'aperçu de l'applet Thème.
* **Ports C++ et .NET.** Ils prennent leurs métriques et jetons de UIKit : à essayer après l'étape 3, surtout Koton et WinForms.
* **Les 89 icônes** sont lues une par une : l'ouverture du lanceur sur le Pi est à mesurer avant de toucher à son dessin.
* **Le panneau gauche de Noir** dépend de `CREUX` plus sombre que `FACE` : à regarder dans chaque app qui l'emploie comme survol.

## 7. Étapes

Chaque étape se livre seule et se voit.

**Prérequis : fusionner `claude/onyx-apps2` dans `main`.** Les icônes et `ui_glyph` y sont. C'est à faire avant tout le reste, car `uikit_icons.inc` et `panel.asm` y ont beaucoup changé.

**Étape 1 : jetons et fond.** S. Tableaux de jetons, palette et liseré de `voronoy`, nettoyage de `theme.txt`, correction de la comparaison `TJ_FACE`. Déjà prouvé : [bureau](design-proposal/prototype/rg_desk.png), [lanceur](design-proposal/prototype/rg_desk_launcher.png), [lanceur en Noir](design-proposal/prototype/rn_desk_launcher.png), [Fichiers en Noir](design-proposal/prototype/pn_files.png).

**Étape 2 : Inter avec repli.** S à M. Polices, repli paresseux sur DejaVu, échelle typographique, interligne. Prototype : [Réglages](design-proposal/prototype/rg_control.png), [Noir](design-proposal/prototype/rn_control.png), [Fichiers](design-proposal/prototype/rg_files.png). **Premier essai sur le Pi : c'est là qu'on saura si Inter tient à cette densité.** Sinon, Inter Medium en texte courant.

**Étape 3 : glyphes.** M. Glyphes système manquants, format alpha dans `ui_glyph`, jeu d'outils dans `art.py`, liseré des tuiles sombres. Les apps les adoptent à l'étape 7.

**Étape 4 : widgets.** M. Coins anticrénelés, boutons plats, focus, listes, panneau latéral unique, métriques, `ui_button` sur les jetons. Le bouton carré disparaît sans toucher aux apps.

**Étape 5 : chrome et composition.** M. `FRAME_BORDER` 12, rayon 10, ombres par niveau, actif et inactif distincts ; NEON dans `bb_blit_any`, barre et panel opaques. À mesurer avec F11 avant et après.

**Étape 6 : coquille.** M. Panel, lanceur, volets du son et du Wi-Fi, carte de bureau.

**Étape 7 : apps.** L. Lignes de réglages et listes déroulantes dans Réglages ; Calculatrice et les six autres apps ; Fichiers ; barres d'outils de Letters, Sheet, Slides, Mail ; français partout.

Les étapes 1 et 2 donnent à elles seules les captures de `prototype/`.

## 8. Ce qui reste à décider

1. **Direction A ou B.** B remplacerait les nouvelles icônes.
2. **Inter** comme police d'interface, après essai sur le PocketTerm, ou DejaVu gardée.
3. **Fusion de `claude/onyx-apps2`** : quand. Tout ce qui suit en dépend.
4. **Lanceur** : rail à gauche et feuille opaque, ou onglets en haut gardés.
5. **Apps en anglais** : traduire Letters, Sheet, Slides et GameLib, ou garder ces noms de menus.
6. **Titre dans les 32 px virtuels** pour les fenêtres sans en-tête : oui ou non.
7. **Cible de 32 px** pour les trois glyphes : à juger au doigt sur l'appareil.
