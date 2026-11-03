# Vector : spécification d'interface

Spécification du 01/11/2026 pour `src/apps/vector`, sur l'architecture de `docs/PLAN-VECTOR-NATIVE.md`. Elle ne contient pas de code.
« Vector » est le nom provisoire du plan. Les numéros d'étape (Ét.) sont ceux de son § 7.

Clean-room : aucune source, icône ni page de manuel d'Inkscape n'a servi. Les noms d'outils décrivent ce que fait l'outil ; icônes,
disposition, textes et raccourcis sont décidés ici.

## 0. Faits relevés, écarts avec le plan

Ces faits viennent du dépôt et de la documentation d'Onyx sur le PocketTerm35. Ils commandent tout le reste.

| # | Fait | Source | Conséquence |
|---|---|---|---|
| F1 | Les menus de `ui_menu` vont dans **la barre globale** d'Elegant, pas dans la fenêtre | `DESIGN-ELEGANT.md` 6.2, `menubar.asm` | **Le schéma du § 3.6 du plan est faux** : aucune ligne de menus dans la fenêtre. On gagne 28 px |
| F2 | Pas de sous-menus dans les menus d'une app : `app_menu` ne lit que `M`, `I`, `-` | `menubar.asm` | **Le § 5 du plan est faux** sur ce point. Tout menu est plat |
| F3 | 2 048 octets pour **tout** le texte des menus (`WS_MENU_MAX`) ; coupe silencieuse dans UIKit ; 64 lignes par menu | `armpi.inc`, `uikit_misc.inc`, `menubar.asm` | Environ 73 entrées de 28 octets. Les menus du § 6 en demandent 2 862 : voir § 6.13 et § 10 |
| F4 | 10 titres au plus dont ARMPI et l'app : **8 menus**. Un titre qui dépasse `largeur − 200` n'apparaît pas | `MB_MAX`, `draw_titles` | À 800 px : les 8 titres du § 6 tiennent à 9 px près (mesuré en DejaVu Sans 13) |
| F5 | Le nom montré dans la barre est le **titre de la fenêtre** | `DESIGN-ELEGANT.md` 6.2 | Titre fixe « Vector ». Jamais le nom du fichier : les menus sortiraient de la barre |
| F6 | Un menu ne défile pas. Lignes de 26 px à 800 × 600, 11 px par séparateur | `DESIGN-ELEGANT.md` 7 | Au plus 19 entrées et 4 séparateurs par menu (550 px, 572 disponibles) |
| F7 | `ui_menu_key` compare des **positions US**. Sur un clavier AZERTY, « Ctrl+Z » répond à la touche marquée W | `uk_shortcut_match` | Le libellé du menu ment. Voir § 7 et § 10 |
| F8 | Avec Ctrl, HIDManager ne donne **aucun caractère** (`key_to_char`) | `hidmanager.asm` | Pour Ctrl+lettre, l'app n'a que le code HID. Il faut la disposition active |
| F9 | Elegant garde F10, F11 et Ctrl+Alt+Gauche/Droite | `core.inc` | Jamais de raccourci sur ces touches |
| F10 | Le clavier du PocketTerm35 a 55 touches : chiffres, lettres, `/ - = ; ' , .`, Tab, Caps, **une** Maj, Fn, Ctrl, Alt, Espace, Alt, CMD, Retour, Entrée. **Pas de flèches** : la croix les donne. **Pas de crochets, de barre inverse, d'accent grave ni de `<`** | `Onyx/docs/04-USER-GUIDE.md`, `PocketTermSim.buttons.ini` | Pas de raccourci sur ces signes |
| F11 | Par Fn : Échap = Fn+Q, Suppr = Fn+Retour, Début = Fn+X, Fin = Fn+V, Page haut = Fn+B, Page bas = Fn+N, F1 à F10 sur les chiffres | mêmes sources | Échap demande deux touches. Il n'est jamais le seul chemin |
| F12 | Les boutons A, B, X, Y, L, R tapent ces lettres sur le vrai appareil ; Select et Start sont Impr. écran et Pause, sans événement clavier ordinaire | `FEUILLE-DE-ROUTE.md`, `03-DEVELOPER-GUIDE.md` | À confirmer sur l'appareil. Rien n'en dépend |
| F13 | Écran : 800 × 600 sur 3,5 pouces, soit **11,25 px par mm**. La cible de 32 px de `DESIGN-ELEGANT.md` fait 2,8 mm, 7 mm seraient l'usage courant au doigt | calcul | Le doigt ne vise pas juste : on choisit le plus proche, on écarte le retour visuel du doigt (§ 5.8) |
| F14 | Gestes de `gt911` : pose et lève = clic ; glisser vertical d'un doigt ou deux doigts = **molette** ; glisser de côté, ou doigt tenu 400 ms = glisser. **Aucun clic droit** : le « 500 ms = clic droit » de `DESIGN-ELEGANT.md` 12 n'est pas dans le pilote | `gt911.asm` | Voir § 8 |
| F15 | Haut de la fenêtre : `UK_DRAG_H` = 36 px déplacent la fenêtre, et sur un bouton (`VTF_MOVE`) dans les 32 px du haut, l'appui attend le relâchement, 8 px de mouvement déplacent la fenêtre | `uikit_window.inc`, `uikit_widgets.inc` | La barre fait **36 px** : elle couvre exactement cette zone, le canevas commence dessous |
| F16 | Zones libres : barre haute 28 / 30 / 26, panel 52 à 800 × 600, 56 en échelle L. Une fenêtre agrandie perd 4 px de chaque bord et en haut | `panel.asm`, `win_maximize` | Contenu utile : voir § 2.1 |
| F17 | `FRAME_CAP_W` = 102, `FRAME_CAP_MARGIN` = 5 : boutons de x = l − 107 à l − 5, y de 5 à 31 | `armpi.inc` | 107 px libres à droite de la barre, à toutes les tailles |
| F18 | Un dialogue `WF_MODAL` ou un menu `ui_popup_*` tient sa propre boucle | `KITS.md` | Rien n'est repeint pendant ce temps : ce qui a besoin d'un aperçu vivant n'est **jamais** dans un dialogue |

## 1. Principes

1. **Le canevas d'abord.** À 800 × 600, il garde au moins 94 % de la largeur et 86 % de la hauteur du contenu tant que le tiroir est fermé.
   Chaque pixel de chrome permanent doit se justifier.
2. **800 × 600 est la référence, pas un repli.** Toute fonction se conçoit d'abord là. Les grands écrans ajoutent de la permanence, jamais
   une fonction.
3. **Rien ne recouvre le canevas, sauf ce qui l'annote.** Le tiroir pousse le canevas au lieu de le couvrir. On voit toujours l'objet que l'on
   règle. Sur l'image, l'app ne peint que ce qui parle de l'objet : poignées, guides, mesures, indications.
4. **Pas de modal pour ce qui se regarde.** Couleur, filtre, transformation, dégradé : tout s'ajuste dans un panneau, avec un résultat vivant.
   Les dialogues servent aux fichiers et aux questions sans retour possible.
5. **Une action, trois chemins.** Toute commande a son entrée dans la recherche de commandes (`Ctrl+K`). La plupart ont aussi un menu ou
   un raccourci. C'est la réponse aux menus plats et à leur limite d'octets.
6. **Le clavier parle en lettres.** Un raccourci est une lettre, un chiffre, `=`, `-`, ou une touche nommée que le PocketTerm a. Jamais
   un signe de ponctuation, jamais une touche F seule, jamais Échap comme seul chemin.
7. **Le doigt vise large, la croix finit le travail.** Le tactile sert à choisir et à déplacer grossièrement. Le réglage fin passe par la
   croix, les champs ou la souris. On ne promet pas la précision d'une tablette sur 71 mm de large.
8. **Une seule matière.** Jetons de UIKit partout, chrome compris. Une capture en Givre et une en Onyx Noir doivent différer seulement par
   ces jetons. Le dessin de l'utilisateur ne change pas.

## 2. Disposition

### 2.1 Tailles utiles

Contenu d'une fenêtre agrandie. Largeur : écran − 8. Hauteur : écran − barre globale − panel − 8.

| Écran | Échelle | Barre globale | Panel | Contenu | Classe |
|---|---|---|---|---|---|
| 800 × 600 | M | 28 | 52 | **792 × 512** | S |
| 1024 × 768 | L | 30 | 56 | **1016 × 674** | M |
| 1280 × 720 | L | 30 | 56 | **1272 × 626** | L |
| 1920 × 1080 | L | 30 | 56 | **1912 × 986** | XL |

Les classes dépendent de la **taille du contenu**, jamais de l'écran : une fenêtre réduite sur un grand écran se comporte comme un PocketTerm.

| Classe | Largeur de contenu | Panneaux | Règles par défaut |
|---|---|---|---|
| XS | moins de 640 | tiroir pleine largeur | non |
| S | 640 à 999 | tiroir de 248 px | non |
| M | 1000 à 1199 | colonne de 264 px | oui |
| L | 1200 à 1699 | colonne de 300 px | oui |
| XL | 1700 et plus | colonne de 340 px, deux panneaux empilés | oui |

Taille minimale : `ui_min_size (480, 360)`. La fenêtre est `WF_RESIZABLE`. Au lancement : `ui_maximize` si l'écran fait moins de 1280 px de
large ; sinon la dernière taille, gardée dans les préférences.

### 2.2 Zones

| Zone | Largeur | Hauteur | Réalisation | Fond |
|---|---|---|---|---|
| Barre de commandes | toute | 36 fixe | `ui_view` plus champs enfants ; contrôles de 28 px à y = 4, 30 px à y = 3 en échelle L | `FACE`, trait `LIGNE` en bas |
| Zone libre du cadre | 107 à droite de la barre | 36 | rien : elle reste vide, elle déplace la fenêtre | `FACE` |
| Rail d'outils | 40 fixe | hauteur du canevas | `ui_view` : icônes peintes, `ui_tip_at` | `FACE`, trait `LIGNE` à droite |
| Règles | 18 | 18 | deux `ui_view`, autour de la surface | `FACE`, graduations `TEXTE3`, chiffres 11 px |
| Canevas | le reste | le reste | `ui_surface` sur une `ui_view` de même taille, qui reçoit la souris | `CREUX` autour de la page |
| Panneaux | voir classe | hauteur du canevas | `ui_view` qui défile, champs enfants | `FACE`, trait `LIGNE` à gauche |
| Bas, une ligne | toute | 32 | `ui_view` | `FACE`, trait `LIGNE` en haut |
| Bas, deux lignes | toute | 28 + 24 | `ui_view` | idem |

Dans tout ce document, un jeton s'écrit sans son préfixe : `ACCENT` veut dire `TJ_ACCENT`.

La barre n'utilise pas `ui_toolbar` : sa hauteur automatique (Hc + 16 = 44) coûterait 8 px, et elle ne prend pas d'icônes.

### 2.3 800 × 600, classe S (792 × 512)

```
x: 0    40                                    576         685      792
   +-----------------------------------------------------+---------+  y 0
   | barre de commandes de l'outil (x 4..572) | <  >  =  | libre   |
   +----+------------------------------------------------+---------+  y 36
   | 40 |                                                          |
   |rail|                  canevas 752 x 444                       |
   | 11 |                  x 40..791, y 36..479                    |
   |cel.|                                                          |
   | de |                                                          |
   | 40 |  indication d'une ligne, peinte dans l'image si besoin   |
   +----+----------------------------------------------------------+  y 480
   | R C | palette, cases de 28 x 28, 18 cases  | Calque | - % + o |  32 px
   +---------------------------------------------------------------+  y 512

   R, C : remplissage, contour. < > : annuler, rétablir. = : tiroir. o : modifié.
```

Mesures de la barre : annuler, rétablir, tiroir sont trois cases de 32 px espacées de 4 px, à x = 576, 612 et 648. La partie de l'outil va de x = 4 à x = 572, soit **568 px**. À toute taille, elle s'arrête à largeur − 220.

Bas : x = 4 pastilles (60), x = 72 palette (504), x = 584 calque (96), x = 688 zoom (92 : 24 + 44 + 24), x = 782 point modifié (8 × 8).

Tiroir ouvert : le canevas passe à 504 × 444, le tiroir occupe x = 544 à 791. Il s'arrête au-dessus de la ligne du bas, qui reste entière.

```
   +-----------------------------------------------------+---------+
   | barre de commandes de l'outil            | <  >  =  | libre   |
   +----+----------------------------------+---+-------------------+  y 36
   |rail| canevas 504 x 444                | R | L | T | M | A | * |  32 px
   |    | x 40..543                        +-----------------------+  y 68
   |    |                                  | panneau, 248 x 412    |
   |    |                                  | défile                |
   +----+----------------------------------+-----------------------+  y 480
   | R C | palette                              | Calque | - % + o |
   +---------------------------------------------------------------+
```

Les onglets du tiroir : Remplissage et contour, Calques, Texte, Transformer, Aligner, puis `⋯` (menu des autres panneaux), chacun 40 × 32. Pas de bouton fermer : la case
tiroir de la barre, Tab ou un nouvel appui sur l'onglet actif le ferment.

Quand le tiroir ouvre, le centre de la vue reste le même. Si la sélection sortirait de la zone visible, la vue se recale pour la montrer, sans changer le zoom.

### 2.4 1024 × 768, classe M (1016 × 674)

```
x: 0  40 58                              752                   909  1016
   +----------------------------------------------------------+------+  y 0
   | barre de commandes (x 4..796)                  | <  >  = | 107  |
   +--+--+----------------------------------+-----------------+------+  y 36
   |  |  | règle horizontale, 18 px         | onglets, 32 px         |
   |r |r +----------------------------------+------------------------+  y 54 / 68
   |a |è |                                  |                        |
   |i |g | canevas 694 x 568                | panneau 264 x 586      |
   |l |l | x 58..751, y 54..621             | onglets compris        |
   |  |e |                                  |                        |
   +--+--+----------------------------------+------------------------+  y 622
   | R |  palette, deux rangs de 12 px si plus de 40 nuances, 28 px   |
   | C |--------------------------------------------------------------+  y 650
   |   | Calque v | x, y | indication de l'outil | nom.svg | - 100 % + |  24 px
   +------------------------------------------------------------------+  y 674
```

Rail : 14 cases de 40 px (§ 3.1).

### 2.5 1280 × 720, classe L (1272 × 626)

```
x: 0  40 58                                        972               1272
   +-------------------------------------------------------+----------+  y 0
   | barre de commandes (x 4..1052)              | <  >  = | 107      |
   +--+--+-----------------------------------------+-------+----------+  y 36
   |  |  | règle, 18 px                            | onglets, 32 px   |
   |r |r +-----------------------------------------+------------------+  y 54 / 68
   |a |è | canevas 914 x 520                       | panneau 300 x 538|
   |i |g | x 58..971, y 54..573                    | onglets compris  |
   |l |l |                                         |                  |
   +--+--+-----------------------------------------+------------------+  y 574
   | R |  palette, 28 px                                               |
   | C |----------------------------------------------------------------+  y 602
   |   | Calque v | x, y | indication | nom.svg | - 100 % +              |  24 px
   +--------------------------------------------------------------------+  y 626
```

Rail : 13 cases de 40 px.

### 2.6 1920 × 1080, classe XL (1912 × 986)

```
x: 0  40 58                                                1572          1912
   +--------------------------------------------------------------+-------+  y 0
   | barre de commandes (x 4..1692), sur une seule ligne   | < > =| 107   |
   +--+--+-------------------------------------------------+------+-------+  y 36
   |  |  | règle, 18 px                                    | onglets      |
   |r |r +-------------------------------------------------+--------------+  y 54 / 68
   |a |è |                                                 | panneau haut |
   |i |g |                                                 | 340 x 516    |
   |l |l | canevas 1514 x 880                              +--- séparateur +  y 584
   |  |e | x 58..1571, y 54..933                           | onglets      |
   |  |  |                                                 | panneau bas  |
   |24|  |                                                 | Calques par  |
   |  |  |                                                 | défaut       |
   +--+--+-------------------------------------------------+--------------+  y 934
   | R |  palette, 28 px                                                   |
   | C |---------------------------------------------------------------------+  y 962
   |   | Calque v | x, y | indication | nom.svg | - 100 % +                  |  24 px
   +-------------------------------------------------------------------------+  y 986
```

Rail plat : 24 cases de 36 px et 7 séparateurs de 4 px, soit 892 px sur 898. `ui_splitter` entre les deux panneaux, bornes 200 et 700.

### 2.7 Ce qui disparaît, dans l'ordre

Quand le contenu rétrécit, on applique ces règles dans l'ordre. Une seule source de vérité : une fonction de placement appelée par `ui_on_resize`, qui ne dépend que de
la largeur et de la hauteur du contenu. Elegant ne change la taille qu'au relâchement du bord : aucune animation.

| # | Condition | Effet | Où reste la fonction |
|---|---|---|---|
| 1 | L < 1700 | Second panneau empilé retiré | Onglets |
| 2 | L < 1000 | Colonne remplacée par le tiroir, fermé au départ | Tab, case tiroir, pastilles |
| 3 | L < 1000 ou H < 520 | Règles masquées, sauf choix explicite (Alt+R) | Vue > Règles |
| 4 | H < 600 | Bas sur une ligne : position, nom, indication quittent le bas | Indication peinte dans l'image, message au besoin |
| 5 | Largeur de barre | Contrôles retirés, priorité 5 d'abord, jusqu'à 2 (§ 3.5) | Case `⋯`, panneau Outil |
| 6 | Hauteur du rail < 892 | Rail en familles (§ 3.1) | Menu de la case, lettres |
| 7 | L < 720 | Sélecteur de calque retiré du bas | Panneau Calques |
| 8 | L < 600 | Palette réduite à 8 cases | Tiroir, Remplissage et contour |
| 9 | Hauteur du rail < 440 | Cases du rail à 36 px | Rien de perdu |
| 10 | Hauteur du rail < 396 | Cases Zoom, Main, Aides, Modifier retirées, une case `⋯` les rassemble | Menu de la case, lettres |
| 11 | L < 640 | Tiroir pleine largeur : il remplace le canevas, rail compris | Fermer le tiroir |

Le canevas ne descend jamais sous 200 × 160 px. Si le tiroir l'y forcerait, on applique la règle 11.

## 3. Outils

### 3.1 Rail

**Case** : 40 × 40 (36 × 36 en XL). **Icône** : 24 × 24, centrée.

| État | Fond | Icône |
|---|---|---|
| Repos | rien | `TEXTE2` |
| Survol | `boîte` rayon 6 `CREUX` | `TEXTE1` |
| Actif | `boîte` rayon 6 `ACCENT` | `ui_ink (ACCENT)` |
| Appui | `boîte` rayon 6 `ACCENT_FONCE` | idem |
| Indisponible à cette étape | absent du rail | |

**Vingt-quatre outils, huit groupes** : la liste du § 3.4 est l'ordre du rail plat.

**Familles** (rail de moins de 892 px) : une case montre le dernier outil utilisé de sa famille, avec un triangle de 5 px en bas à droite, `TEXTE3`.

| Case | S : 11 cases | Tient aussi à 13 cases (L) | Et à 14 cases (M) |
|---|---|---|---|
| 1 | Sélecteur | | |
| 2 | Nœuds | | |
| 3 | Formes : Rectangle, Ellipse, Étoile, Spirale, Boîte 3D | Rectangle, Boîte 3D, Spirale | |
| 4 | | Ellipse | |
| 5 | | Étoile | |
| 6 | Tracés : Stylo, Crayon, Calligraphie | Stylo, Calligraphie | |
| 7 | | | Crayon |
| 8 | Texte | | |
| 9 | Dégradé : Dégradé, Maillage | | |
| 10 | Couleur : Seau, Pipette | | |
| 11 | Modifier : Gomme, Constructeur, Ajustement, Pulvérisation | | |
| 12 | Aides : Connecteur, Mesure, Pages | | |
| 13 | Main | | |
| 14 | Zoom | | |

En colonne S, les cases 4, 5 et 7 n'existent pas : il reste 11 cases. La colonne L en ajoute deux (Ellipse, Étoile), la colonne M une de plus (Crayon).

**Ouvrir une famille**, sans minuteur : clic ou tape sur la case de l'outil **déjà actif**, ou clic droit, ou appui sur le triangle (zone de 14 × 14). C'est `ui_popup_local` au point x = 40, y = haut de la
case : libellé et lettre (`Spirale    S`). Une lettre choisit aussi directement l'outil, et la case change.

**Infobulle** : à la souris seulement, `ui_tip_at`, à droite de la case : « Rectangle (R) ».

### 3.2 Règles communes

| Touche ou geste | Effet dans tout outil de dessin |
|---|---|
| Maj pendant un glisser | Suspend l'aimantation, sauf indication contraire ci-dessous |
| Ctrl pendant un glisser | Contraint : angle par pas de 15°, ou proportions |
| Alt pendant un glisser | Depuis le centre |
| Espace tenu, ou bouton du milieu | Main temporaire |
| Échap, ou `V` | Revient au Sélecteur |
| Fin de création | L'outil reste actif. L'objet créé est sélectionné |

**Style d'un nouvel objet** : le dernier style utilisé avec cet outil, montré par les pastilles. Une pastille change la sélection, ou, sans sélection, le style du prochain objet.

### 3.3 Icônes

SVG à nous, 24 × 24, un seul tracé : `currentColor`, teinté par le jeton de l'état. Trait de 2 px, bouts ronds, zone utile de 20 × 20. Rien de plus fin que 2 px :
24 px font 2,1 mm sur le PocketTerm. Pas de remplissage multicolore. Pas de lettre, sauf Texte.

### 3.4 Détail par outil

Conventions : `Ét.` est l'étape où l'outil arrive. Le tactile décrit le mode brut ; le § 8 dit ce qui change avant l'étape 7. Barre : entre parenthèses, largeur en px, puis priorité (P1 ne part jamais, P5 part d'abord).

#### Sélection

**1. Sélecteur** : `V`. Ét. 2. Icône : flèche de pointeur pleine, pointe en haut à gauche.
* Clic : choisit l'objet du dessus. Clic dans le vide : désélectionne.
* Maj+clic : ajoute ou retire. Alt+clic : l'objet juste dessous, en boucle. Ctrl+clic : entre dans le groupe, choisit l'objet.
* Glisser sur un objet : déplace. Le déplacement part à 4 px. Ctrl : horizontal ou vertical.
* Glisser dans le vide : rectangle, garde ce qui est **entièrement** dedans. Vers la gauche : garde aussi ce qui touche. Maj : ajoute à la sélection.
* Double-clic : un groupe : entre. Un chemin : Nœuds. Un texte : Texte. Une forme : son outil. Hors du groupe : en sort.
* Poignées : § 5.1. Il n'y a qu'un seul état, pas de second clic pour tourner.
* Tactile : tape = clic. Un doigt sur un objet : déplace. Un doigt dans le vide : rectangle. Tape double = double-clic. Appui tenu : menu contextuel.
* Retour : poignées, boîte, pastille de mesure pendant un glisser.
* Barre : X (72, P1), Y (72, P1), L (72, P1), H (72, P1), verrou de proportions (28, P1), unité (44, P2), retournements et quarts de tour (4 × 32, P3). Sans sélection : X, Y, L, H grisés, et aide « Touchez un objet ».

**2. Nœuds** : `A`. Ét. 2. Icône : courbe avec un carré plein au milieu et une poignée ronde.
* Clic sur un chemin : le sélectionne et montre ses nœuds. Clic sur un nœud : le choisit. Maj+clic : ajoute. Glisser dans le vide : rectangle de nœuds.
* Clic sur un segment : choisit ses deux bouts. Glisser un segment : le courbe. Double-clic sur un segment : ajoute un nœud. Double-clic sur un nœud : change son type.
* Glisser un nœud : le déplace, avec ses poignées. Glisser une poignée : la déplace. Ctrl : par pas de 15°. Maj : l'autre poignée est libérée. Alt : garde la longueur.
* Retour : retire les nœuds choisis. Entrée : rien. `Ctrl+Tab` : nœud suivant. Flèches : déplacent les nœuds choisis.
* Tactile : tape choisit, un doigt déplace, avec loupe (§ 5.8). Double tape sur un segment : ajoute. Appui tenu sur un nœud : menu (types, supprimer, couper, joindre).
* Retour : contour survolé, cible agrandie, tangentes.
* Barre : insérer (32, P1), supprimer (32, P1), joindre (32, P2), couper (32, P2), type : coin, lisse, symétrique, auto (4 × 32, P1), segment droit, courbe (2 × 32, P3), X, Y (72 × 2, P2), montrer les poignées (32, P5), montrer le contour de l'effet (32, P5).

#### Formes

Communes : glisser crée. Double-clic sur la forme avec son outil : options dans le panneau Outil. Touches de § 3.2. Tactile : un doigt crée. Retour : contour en cours, pastille L × H.

**3. Rectangle** : `R`. Ét. 2. Icône : rectangle sans remplissage, un coin arrondi.
* Ctrl : carré ou proportion entière. Poignée de rayon : deux ronds sur le coin haut droit, glisser. Clic sur l'un : remet à zéro.
* Barre : L, H (72 × 2, P1), Rx, Ry (72 × 2, P2), unité (44, P3), remettre à zéro (32, P4).

**4. Ellipse** : `E`. Ét. 2. Icône : ellipse sans remplissage, avec un point au centre.
* Ctrl : cercle. Poignées d'arc : deux ronds sur le bord, glisser hors de la forme fait un arc ouvert, dedans une part.
* Barre : Rx, Ry (72 × 2, P1), début, fin (64 × 2, P2), arc, part, secteur fermé (3 × 32, P2), cercle complet (32, P3).

**5. Étoile et polygone** : `Y`. Ét. 2. Icône : étoile à cinq branches, contour.
* Ctrl : angle par 15°. Poignées : pointe (rayon, rotation), creux (rapport). Maj sur la poignée : arrondi. Alt : hasard.
* Barre : polygone ou étoile (2 × 32, P1), sommets (64, P1), rapport (72, P1), arrondi (72, P2), hasard (72, P3), remettre à zéro (32, P4).

**6. Spirale** : `S`. Ét. 2. Icône : spirale de deux tours.
* Ctrl : rotation par 15°. Poignées : bout extérieur, bout intérieur.
* Barre : tours (64, P1), divergence (72, P2), rayon intérieur (72, P3), remettre à zéro (32, P4).

**7. Boîte 3D** : `O`. Ét. 7. Icône : cube en perspective, trois faces.
* Glisser : plan de base. Maj pendant le glisser : hauteur. Poignées de points de fuite sur le canevas.
* Barre : pour X, Y, Z : angle (64 × 3, P2), point de fuite fini ou parallèle (3 × 32, P1).

#### Tracés

**8. Stylo** : `P`. Ét. 2, Spiro et B-spline Ét. 6. Icône : plume vue de biais avec sa fente.
* Clic : point de coin. Glisser : point lisse. Clic sur le premier point : ferme, curseur ou anneau `SUCCES` à l'approche.
* Entrée, ou double-clic : termine ouvert. Retour : retire le dernier point. Échap : abandonne. Ctrl : 15°. Alt : déplace le point en cours. Maj : casse la tangente.
* Clic sur le bout d'un chemin choisi : le continue.
* Tactile : tape = point de coin, un doigt = point lisse. Le dernier segment suit le doigt avec un décalage de 56 px vers le haut (§ 5.8).
* Retour : segment fantôme vers le pointeur, en `ACCENT`, anneau de fermeture.
* Barre : mode : Bézier, droites, Spiro, B-spline (4 × 32, P1), forme du trait : rien, épaisseur croissante, décroissante, ellipse (dropdown 96, P3), terminer (32, P1), abandonner (32, P1).

**9. Crayon** : `N`. Ét. 2. Icône : crayon en diagonale, pointe en bas à gauche.
* Glisser : trace à main levée. Partir du bout d'un chemin choisi : le continue. Maj : ajoute le trait au chemin choisi.
* Tactile : un doigt trace. Les points intermédiaires sont ceux que donne l'événement : à mesurer à l'étape 0.
* Retour : trait brut mince en `ACCENT`, puis courbe lissée au relâchement.
* Barre : lissage (dropdown ou champ 72, P1), mode : Bézier, Spiro, B-spline (3 × 32, P2), forme du trait (96, P3).

**10. Calligraphie** : `B`. Ét. 7. Icône : plume plate et trait à épaisseur variable.
* Glisser : trace une forme remplie. Pression absente : **la vitesse** amincit le trait (réglage).
* Barre : largeur (72, P1), angle (64, P1), fixation (72, P2), amincissement par la vitesse (72, P2), tremblement (72, P4), préréglages (dropdown 96, P3).

#### Texte

**11. Texte** : `T`. Ét. 3. Icône : grand T à empattements.
* Clic : texte libre au point. Glisser : texte dans un cadre. Clic sur un texte : caret. Double-clic : mot. Triple-clic : ligne.
* Dans le texte : Ctrl+B gras, Ctrl+I italique, Ctrl+Entrée valide, Échap valide et revient au Sélecteur.
* Alt+flèches : décale la lettre. Alt+Maj+flèches : décalage plus grand. Ctrl+Alt+haut et bas : interligne.
* Tactile : tape place le caret. Le clavier du PocketTerm reste le seul moyen de saisie, aucun clavier à l'écran.
* Retour : caret 1 px `ACCENT`, sélection `ACCENT_CLAIR` mélangée à 60 %, cadre pointillé, carré de débordement `DANGER` si le texte dépasse.
* Barre : police (menu 148, P1), taille (56, P1), gras, italique (2 × 32, P1), alignement (menu 40, P2), interligne (64, P3), espacement (64, P4), valider (32, P1).

#### Couleur

**12. Dégradé** : `G`. Ét. 3. Icône : carré à bandes qui s'éclaircissent, flèche dessus.
* Glisser sur un objet : crée un dégradé du point de départ au point d'arrivée. Ctrl : 15°. Double-clic sur le segment : ajoute un arrêt. Retour : retire l'arrêt choisi.
* Glisser un arrêt : le déplace. Clic sur un arrêt : le choisit, les pastilles règlent sa couleur.
* Tactile : un doigt crée, déplace. Tape sur le segment : ajoute. Appui tenu sur un arrêt : supprimer.
* Barre : linéaire, radial (2 × 32, P1), remplissage ou contour (2 × 32, P1), arrêt : décalage (72, P2), inverser (32, P2), répétition (dropdown 96, P3).

**13. Maillage** : `M`. Ét. 5. Icône : carré à grille courbe.
* Clic sur un objet : crée la grille. Double-clic sur une ligne : divise. Glisser un nœud ou une poignée : déforme.
* Barre : lignes (56, P1), colonnes (56, P1), lissage (dropdown 96, P3), échanger coins (32, P4).

**14. Seau** : `F`. Ét. 7. Icône : seau penché et goutte.
* Clic : remplit la zone fermée sous le pointeur, crée une forme. Maj+clic : réunit au dernier remplissage. Alt+glisser : zone dessinée à la main.
* Barre : canal (dropdown 96, P2), seuil (72, P1), croissance (72, P2), combler les fentes (dropdown 96, P3).

**15. Pipette** : `I`. Ét. 2. Icône : pipette en diagonale et sa goutte.
* Clic : remplissage de la sélection, ou du prochain objet. Maj+clic : contour. Alt+clic : copie aussi le style complet. Glisser : moyenne de la zone.
* Tactile : un doigt montre la couleur dans une pastille décalée, relâcher applique.
* Barre : remplissage ou contour (2 × 32, P1), avec opacité (32, P2), moyenne (72, P3).

#### Modifier

**16. Gomme** : `W`. Ét. 7. Icône : gomme en parallélogramme, ligne dessous.
* Glisser : mode choisi. Maj : ajoute à la dernière gomme. Barre : supprimer les objets touchés, couper, découper (3 × 32, P1), largeur (72, P1).

**17. Constructeur de formes** : `K`. Ét. 7. Icône : trois disques qui se chevauchent, zone du centre pleine.
* Sur une sélection : les régions se révèlent au survol. Glisser sur des régions : les réunit. Alt+glisser : les retire. Entrée : applique.
* Tactile : un doigt balaie, les régions touchées restent `ACCENT_CLAIR`.
* Barre : réunir, retirer (2 × 32, P1), garder le reste (32, P2), appliquer (32, P1), abandonner (32, P1).

**18. Ajustement** : `U`. Ét. 7. Icône : flèches qui poussent un trait ondulé.
* Glisser : modifie sous un cercle. Barre : mode (menu 148, P1), largeur (72, P1), force (72, P1), canaux de couleur (3 × 32, P4).

**19. Pulvérisation** : `J`. Ét. 7. Icône : bombe et trois points.
* Glisser : dépose des copies ou un clone de la sélection. Barre : copies, clones, union (3 × 32, P1), rayon (72, P1), quantité (72, P1), dispersion (72, P3), rotation (72, P4), échelle (72, P4).

#### Aides

**20. Connecteur** : `L`. Ét. 7. Icône : deux carrés reliés par un trait coudé.
* Glisser d'un objet à un autre : relie. Les points d'attache se montrent au survol. Barre : droit, coudé (2 × 32, P1), écart (72, P2), courbure (72, P3), éviter (32, P3).

**21. Mesure** : `Q`. Ét. 7. Icône : règle en diagonale à graduations.
* Glisser : mesure distance et angle, et les croisements. Entrée : transforme en objet. Maj : suspend l'aimantation. Barre : unité (44, P1), précision (56, P3), taille du texte (56, P4), convertir en objet (32, P2).

**22. Pages** : `D`. Ét. 4. Icône : deux feuilles décalées, coin plié.
* Glisser dans le vide : nouvelle page. Clic : choisit. Glisser un bord : redimensionne. Barre : format (menu 148, P1), L, H (72 × 2, P1), ajouter (32, P1), supprimer (32, P1), déplacer le contenu avec (32, P3).

#### Vue

**23. Main** : `H`. Ét. 1. Icône : main ouverte.
* Glisser : déplace la vue. Double-clic : ajuste à la page. Tactile : un doigt déplace, deux doigts aussi. Barre : les boutons d'ajustement de Zoom.

**24. Zoom** : `Z`. Ét. 1. Icône : loupe, plus au centre.
* Clic : zoom avant au point. Maj+clic : arrière. Glisser : zone. Barre : valeur (72, P1), 100 %, 200 % (2 × 40, P2), sélection, dessin, page (3 × 32, P1), zoom précédent (32, P3).

### 3.5 Barre contextuelle : règles

* Placement : recette `row_place` de Letters, avec priorités et largeur minimale, de x = 4 à la limite de droite. Le groupe `annuler, rétablir, tiroir` n'en fait pas partie.
* Contrôles : case de 32 × 28, champ de 56 à 72 × 28, dropdown de 96 ou 148. Écart de 2 px, de 8 px entre groupes.
* Les vides de la barre déplacent la fenêtre. Un bouton dans les 32 px du haut agit au relâchement (F15). C'est sans conséquence.
* **Pas de glisser sur un libellé dans la barre** : 8 px de mouvement y déplacent la fenêtre. Molette et flèches haut et bas sur le champ font varier la valeur.
* Une case `⋯` (P1) est toujours là, à la fin de la partie de l'outil. Elle ouvre le **panneau Outil**.
* Le panneau Outil répète tous les réglages de l'outil en colonne. Mêmes modèles, mêmes valeurs.
* Champ : expression et unité acceptées, `10+2`, `5 mm`, `50%`. Entrée valide. Tab passe au suivant. Échap ou Ctrl+Z rendent la valeur précédente. Refusé : contour `DANGER` (`UTF_ERROR`), la valeur précédente reste.

## 4. Panneaux

### 4.1 Liste et ordre

| Ordre | Panneau | Raccourci | Ét. | Sur les onglets |
|---|---|---|---|---|
| 1 | Remplissage et contour | `C`, Ctrl+Maj+F | 2 | Oui |
| 2 | Calques et objets | Ctrl+L | 3 | Oui |
| 3 | Texte et police | Ctrl+T | 3 | Oui |
| 4 | Transformer | Ctrl+Maj+M | 2 | Oui |
| 5 | Aligner et distribuer | Ctrl+Maj+L | 4 | Oui |
| 6 | Outil | case `⋯` de la barre | 2 | Dans `⋯` |
| 7 | Nuanciers | menu | 4 | Dans `⋯` |
| 8 | Symboles | menu | 4 | Dans `⋯` |
| 9 | Filtres | menu | 5 | Dans `⋯` |
| 10 | Effets de chemin | Ctrl+Alt+P | 6 | Dans `⋯` |
| 11 | Pages | outil Pages | 4 | Dans `⋯` |
| 12 | Document | Ctrl+Maj+E | 3 | Dans `⋯` |
| 13 | Export | Ctrl+E | 3 | Dans `⋯` |
| 14 | Éditeur XML | Ctrl+Maj+X | 4 | Dans `⋯` |
| 15 | Historique | menu | 2 | Dans `⋯` |

L'onglet de la classe S : 40 × 32, icône seule. Infobulle à la souris. À partir de M, 48 × 32. Le nom du panneau est en tête de panneau : Sans gras 13, hauteur 28.

Un panneau sans sélection utile ne se cache pas : ses champs sont grisés, avec une phrase d'une ligne en `TEXTE3` : « Sélectionnez un objet pour régler son remplissage. »

**Un seul fichier par panneau**, chacun lit le document et s'abonne à ses changements. L'application immédiate est la règle : pas de bouton Appliquer, sauf Export et Filtres lourds. Une valeur qui varie
se fond en une transaction (`§ 3.3` du plan).

### 4.2 Tiroir, classe S

* Ouvrir : case tiroir (x = 648), Tab quand le canevas a le focus, `C`, pastilles, raccourci d'un panneau.
* Fermer : case tiroir, Tab, onglet actif, Échap. Tab dans un champ passe au champ suivant : il ne ferme rien.
* Mouvement : aucune animation. `ui_surface_move`, le canevas repeint la bande perdue ou gagnée à partir des tuiles.
* Le tiroir mémorise son onglet et sa position de défilement.
* Il reste ouvert pendant que l'on dessine. Rien ne le ferme tout seul.

**Accessible d'un geste, tiroir fermé** :

| Geste | Résultat |
|---|---|
| Tape sur une case de palette | Remplissage de la sélection |
| Maj + tape sur une case, ou appui tenu | Contour. Menu de la case : remplissage, contour, modifier |
| Tape sur la pastille R ou C | Tiroir, Remplissage et contour, section voulue |
| Barre | X, Y, L, H, et tout réglage de l'outil |
| `Ctrl+K` | Toutes les commandes |
| Tape sur le bloc Calque | Menu : calques du document, nouveau calque, panneau |
| Tape sur % | Menu : 50, 100, 200 %, sélection, dessin, page |

### 4.3 Remplissage et contour

Largeur utile : 232 px à 248 (marges de 8). Hauteur libre : le panneau défile. Tous les contrôles sont les mêmes à toute taille ; les plus grandes colonnes élargissent la zone de couleur et les champs.

**En-tête, ordre fixe** :

| Rang | Contenu | Détail |
|---|---|---|
| 1 | `ui_segmented` Remplissage, Contour, Trait | 28 px. Trois pages sur un seul panneau. La page suit la pastille touchée |
| 2 | Aperçu de la sélection | 28 px : R et C côte à côte, « Plusieurs valeurs » si besoin |

**Pages Remplissage et Contour, même structure**

| Rang | Contenu | Détail |
|---|---|---|
| 3 | Genre de peinture | 8 cases de 29 × 28 : aucune, uni, linéaire, radial, maillage, motif, hachure, héritée. Bascule. Ctrl+clic : aucune |
| 4 | Selon le genre, le bloc ci-dessous | |
| 5 | Règle de remplissage | Remplissage seulement. Deux cases 32 × 28 : pair-impair, non nul |

**Bloc uni** :

| Rang | Contenu | Détail |
|---|---|---|
| a | Modèle | `ui_segmented` TSL, TSV, RVB, CMJN, OKLCH. CMJN et OKLCH à l'étape 10 |
| b | Zone 2D | 232 × 112. Teinte et saturation ou saturation et valeur selon le modèle. Anneau de 11 px, cible 40 |
| c | Barre de teinte | 232 × 20, curseur rectangulaire |
| d | Barre d'opacité | 232 × 20, damier sous la couleur |
| e | Pipette | case 32 × 28, puis champ hexadécimal `#RRGGBBAA` de 96 px, puis champ opacité 64 px |
| f | Trois champs numériques | Selon le modèle, 72 px chacun, libellé glissable |
| g | Récents | 12 cases de 18 × 18 |
| h | Palette du document | Deux rangs de 12 cases, en liste défilante, ajout `+` |

Toutes ces zones sont dessinées par l'app sur `ui_view`. Pas de `ui_color_dialog` : il est modal (F18). Le tirage d'une zone change la couleur sur le canevas à chaque déplacement, et ne fait qu'une entrée d'annulation.

**Bloc dégradé** :

| Rang | Contenu | Détail |
|---|---|---|
| a | Liste des dégradés | Dropdown (menu) avec nom ; aperçu dans le rang suivant |
| b | Barre des arrêts | 232 × 28, damier dessous, triangles de 11 px, cible 32 en tactile. Tape dans la barre : ajoute |
| c | Arrêt choisi | Décalage 72 px, supprimer, 32 px. La couleur se règle dans le bloc uni complet, qui suit |
| d | Options | Inverser, répéter (aucune, reflet, directe), éditer sur le canevas |

**Bloc motif, hachure, maillage** : liste, échelle, rotation, plus les options du genre. Aucune zone 2D.

**Page Trait**

| Rang | Contenu | Détail |
|---|---|---|
| 1 | Épaisseur | Champ 72, unité 44, glissière 100 |
| 2 | Tirets | Dropdown de 232 px : motifs dessinés si le menu prend des icônes (§ 10), sinon texte `4, 2`. Champ décalage 72 |
| 3 | Bouts | Trois cases : plat, rond, carré |
| 4 | Jointures | Trois cases : angle, rond, biseau. Champ limite 64, grisé sauf angle |
| 5 | Marqueurs | Trois boutons de 72 px : début, milieu, fin. Chacun ouvre un menu |
| 6 | Ordre de peinture | Dropdown : remplissage, contour, marqueurs |
| 7 | Séparateur | |
| 8 | Opacité de l'objet | Champ 64, glissière 160 |
| 9 | Mode de mélange | Dropdown 232 |
| 10 | Flou | Champ 64, glissière 160 |

### 4.4 Calques et objets

* Liste virtuelle (`ui_list_virtual` et `ui_list_cell`) à **une seule colonne dessinée par l'app** : retrait de 12 px par niveau, triangle, œil, verrou, pastille de couleur, nom.
  Pas besoin d'attendre `ui_tree_virtual` : on tient sans.
* Ligne de 28 px en S, 26 px au-delà. Cases œil et verrou de 28 × 28 : en S, elles sont à droite, sous le pouce.
* Barre du bas, 32 px : nouveau, supprimer, monter, descendre, grouper.
* Glisser pour réordonner à la souris. Au doigt, appui tenu : menu (monter, descendre, déplacer vers, renommer, verrouiller, masquer).
* Double-clic : renommer dans `ui_ask`, qui bloque sans mal car rien n'a besoin d'être regardé.

### 4.5 Autres panneaux

| Panneau | Contenu, de haut en bas |
|---|---|
| Texte et police | Liste de polices (virtuelle, aperçu dans la police seulement à partir de la classe M), taille, style, alignement, interligne, espacement, sens, OpenType (Ét. 7), texte brut dans un `ui_textarea` |
| Transformer | `ui_segmented` Déplacer, Échelle, Rotation, Inclinaison, Matrice ; champs ; relatif, appliquer à chaque objet. Aperçu vivant, sans bouton Appliquer |
| Aligner et distribuer | Relatif à (dropdown), grille de 6 × 3 cases de 36 × 32 pour aligner, de 6 × 2 pour distribuer ; nœuds si outil Nœuds |
| Outil | Tous les réglages de l'outil actif, plus remise à zéro |
| Nuanciers | Dropdown de palette, grille de cases, import de fichier `.gpl` |
| Symboles | Dropdown de bibliothèque, grille de 56 × 56, ajouter la sélection |
| Filtres | Liste de nos préréglages, champ de filtre, curseurs ; éditeur de primitives à partir de M, plein tiroir en S |
| Effets de chemin | Pile de l'objet, ajouter (menu), paramètres ; poignées sur le canevas |
| Pages | Liste, format, marges, ordre |
| Document | Unité, format, échelle, grille, guides, couleur de page, métadonnées |
| Export | Zone (page, dessin, sélection, personnalisée), L, H, dpi, format, fichier, Exporter. Le seul panneau avec un bouton principal |
| Éditeur XML | Arbre à gauche et attributs dessous en S (empilés) ; côte à côte à partir de L ; texte par `ui_textarea` |
| Historique | Liste des transactions. Clic : revient à cet état |

## 5. Canevas

### 5.1 Sélection et poignées

Tailles en px d'écran, jamais à l'échelle du zoom.

| Élément | Forme | S souris | Autres souris | Tactile : vue | Tactile : cible |
|---|---|---|---|---|---|
| Poignée de coin | Carré | 11 | 9 | 13 | 48 |
| Poignée de côté | Carré tourné de 45° | 11 | 9 | 13 | 48 |
| Rotation | Disque au bout d'une tige de 24 px | 13 | 11 | 15 | 48 |
| Pivot | Croix de 13 px dans un anneau | 13 | 13 | 15 | 40 |
| Nœud de coin | Carré | 9 | 7 | 11 | 44 |
| Nœud lisse | Rond | 9 | 7 | 11 | 44 |
| Nœud symétrique | Losange | 9 | 7 | 11 | 44 |
| Poignée de nœud | Petit disque, tige de 1 px | 7 | 7 | 9 | 40 |
| Poignée de forme (rayon, sommet) | Rond à contour double | 11 | 9 | 13 | 44 |
| Arrêt de dégradé | Carré tourné, rempli de sa couleur | 11 | 9 | 13 | 44 |

La cible souris vaut 4 px de plus que la vue. Zones de cible qui se chevauchent : **le centre le plus proche gagne**, puis la priorité : poignée de nœud, nœud, poignée de sélection, objet. La
cible d'un trait est de 5 px à la souris, de 16 px au doigt.

**Petits objets**, boîte à l'écran :

| Plus petit côté | Poignées |
|---|---|
| 96 px et plus | 8 poignées, rotation, pivot si demandé |
| 48 à 95 px | 4 coins, rotation, côtés au survol seulement |
| 24 à 47 px | 4 coins décalés de 8 px vers l'extérieur, rotation |
| Moins de 24 px | 4 coins décalés de 14 px, rotation. Le glisser au centre déplace toujours |

**Transformations** :

| Geste | Résultat |
|---|---|
| Coin | Échelle |
| Coin + Ctrl | Proportions gardées |
| Coin + Alt | Depuis le pivot |
| Côté | Échelle sur un axe |
| Côté + Alt | Inclinaison |
| Rotation | Tourne autour du pivot |
| Rotation + Ctrl | Par 15° |
| Pivot | Se déplace. Double-clic : retourne au centre |
| Sélection d'un seul objet | Pivot montré si la souris survole la rotation, ou panneau Transformer ouvert |

**Plusieurs objets** : une boîte fine par objet, et les poignées autour de l'ensemble.

### 5.2 Couleurs de la surcouche

La surcouche est peinte par Skia dans l'image, avec les jetons lus à chaque changement de thème. Le document, lui, ne suit pas le thème.
**Double trait** partout : le trait en couleur, posé sur un halo de 3 px `SURFACE` à 70 %. Ainsi une ligne reste visible sur n'importe quel dessin, dans les deux thèmes.

| Élément | Jeton | Trait |
|---|---|---|
| Autour de la page, canevas vide | `CREUX` | |
| Bord de page | `LIGNE_FORTE` | 1 px, ombre `CONTOUR` 2 px |
| Marges de page, marge d'impression | `TEXTE3` | 1 px, tirets 4/4 |
| Boîte de sélection | `ACCENT` | 1 px, tirets 4/4 |
| Boîtes des objets d'une sélection multiple | `ACCENT_CLAIR` | 1 px |
| Poignée | fond `SURFACE`, contour `ACCENT` | contour 1,5 px |
| Poignée survolée, choisie | fond `ACCENT`, contour `SURFACE` | |
| Nœud | fond `SURFACE`, contour `TEXTE1` | 1,5 px |
| Nœud choisi | fond `ACCENT` | |
| Tracé en cours, survol d'un contour | `ACCENT_VIF` | 1,5 px |
| Rectangle de sélection | contour `ACCENT`, fond `ACCENT` à 12 % | 1 px |
| Guide | `ALERTE` | 1 px ; choisi 2 px |
| Grille | `LIGNE` | 1 px ; majeure `LIGNE_FORTE` |
| Aimantation : point trouvé | `SUCCES` | croix de 11 px, libellé |
| Fermeture de tracé | `SUCCES` | anneau de 17 px |
| Danger : texte qui déborde, objet verrouillé | `DANGER` | |
| Damier de transparence | `SURFACE`, `CREUX` | cases de 8 px |
| Pastille de mesure | fond `TEXTE1` à 92 %, texte `ui_ink` | rayon 5, 12 px |

### 5.3 Pastille de mesure

Elle se peint à 16 px en bas à droite du pointeur à la souris, à 72 px au-dessus du doigt en tactile, et reste dans le canevas. Contenu : `L × H`, `angle`, `+dx, +dy`. Hauteur 20 px. Elle
disparaît au relâchement.

### 5.4 Guides, grilles, aimantation

* Tirer depuis une règle : nouveau guide. Ctrl pendant le tirage : guide diagonal.
* Règles cachées : Vue > Ajouter un guide vertical, ou horizontal. Le guide naît au centre de la vue.
* Clic sur un guide : le choisit, Retour le supprime. Double-clic : champs position et angle dans le panneau Document.
* Guides, grille, aimantation : Alt+U, Alt+G, Alt+A. L'état de l'aimantation est aussi une case dans le bas (M+) et dans le panneau Outil.
* Tolérance d'aimantation : 8 px à la souris, 20 px au doigt, toujours à l'écran.
* Priorité : nœuds, chemins, guides, grille, milieu de boîte.

### 5.5 Règles

18 px. Origine à la page. Unité du document. Marqueur du pointeur : trait de 1 px `ACCENT`. Sélection : bande `ACCENT_CLAIR` sur la règle. Pas de graduation à moins de 4 px d'écart, même
règle que Letters (`every`).

### 5.6 Zoom et vue

| Sujet | Règle |
|---|---|
| Plage | 1 % à 25 600 % |
| Pas | Facteur 1,25 par cran, `=` ou `-` |
| Point fixe | Sous le pointeur à la molette et au doigt ; centre de la vue au clavier |
| Au lancement | Page ajustée, 8 px de marge |
| Bord de vue | La page peut sortir de la vue, sans jamais être perdue : 64 px restent visibles |
| Zoom précédent | Pile de 16 vues |
| Rendu | Tuiles agrandies d'abord, puis remplacées (plan § 3.4). Bandes vides : `CREUX` |
| Modes | Normal, contours, sans filtres. Mode non normal : pastille en haut à gauche du canevas, 12 px, `ALERTE` : « Contours » |

### 5.7 Curseurs

Formes `UC_*` existantes en attendant `ui_cursor_image` (§ 10). Point chaud donné pour la version image. Au doigt, il n'y a pas de curseur : le § 5.8 les remplace.

| Situation | Forme voulue, 24 × 24 | Point chaud | En attendant |
|---|---|---|---|
| Sélecteur, vide | Flèche | 2, 2 | `UC_ARROW` |
| Sur un objet | Flèche avec quatre flèches | 2, 2 | `UC_MOVE` |
| Poignée de coin | Double flèche selon l'angle | centre | `UC_NWSE`, `UC_NESW` |
| Poignée de côté | Double flèche | centre | `UC_HRESIZE`, `UC_VRESIZE` |
| Rotation | Arc à deux flèches | centre | `UC_HAND` |
| Inclinaison | Deux flèches opposées décalées | centre | `UC_HRESIZE`, `UC_VRESIZE` |
| Nœuds | Flèche creuse | 2, 2 | `UC_ARROW` |
| Nœud survolé | Flèche et carré | 2, 2 | `UC_HAND` |
| Formes, Stylo, Crayon, Calligraphie | Croix fine et icône 10 px | centre de la croix | `UC_CROSS` |
| Stylo sur le premier point | Croix et anneau | centre | `UC_HAND` |
| Texte | Barre d'insertion | milieu | `UC_TEXT` |
| Dégradé, Maillage | Croix et petit dégradé | centre | `UC_CROSS` |
| Seau | Seau | bec | `UC_CROSS` |
| Pipette | Pipette | pointe | `UC_CROSS` |
| Gomme, Ajustement, Pulvérisation | Cercle à la taille de l'outil, peint dans l'image | centre | `UC_CROSS` |
| Main, repos | Main ouverte | milieu | `UC_HAND` |
| Main, tenue | Main fermée | milieu | `UC_MOVE` |
| Zoom | Loupe plus, moins avec Maj | centre de la loupe | `UC_CROSS` |
| Tuiles en cours | Rien : jamais de sablier sur le canevas | | |
| Interdit : calque verrouillé | Interdit | centre | `UC_NO` |

### 5.8 Doigt : voir ce que l'on fait

Le doigt couvre environ 100 px d'un écran de 800 px.

* **Loupe** : outils Nœuds, Stylo, Dégradé, en tactile. 128 × 128 px, zoom × 4, 200 ms après le début du glisser. Elle se peint en haut à gauche du canevas, ou en haut à droite si le doigt
  y est. Cadre `LIGNE_FORTE`, réticule `ACCENT`.
* **Décalage** : le point travaillé par le Stylo est 56 px au-dessus du doigt, relié par une tige. Le doigt ne couvre pas le point.
* **Croix du PocketTerm** : après un toucher, on affine par les flèches, 1 unité par défaut, Alt : 1 px d'écran.
* **Réticule** : Alt+Entrée. Une croix peinte, `ACCENT`, 21 px. Flèches : 8 px. Maj : 1 px. Entrée : clic. Entrée au début d'un tracé d'outil : appui, puis Entrée : relâchement. C'est le pointeur précis sans souris. Reprend
  l'idée de Circuits. Ét. 7.

## 6. Menus et barre d'état

### 6.1 Règles

* Titres : **Fichier, Édition, Vue, Objet, Chemin, Texte, Effets, Aide**. Mesures à 13 px : le dernier finit à x = 591, la limite est 600. Il n'y a pas de place pour un neuvième, ni pour
  allonger un titre. « Affichage » au lieu de « Vue » suffirait à perdre Aide. Le nom de l'app ne doit pas dépasser 57 px en gras.
* Le menu au nom de l'application contient Quitter, Ctrl+Q : il vient d'Elegant.
* Un menu ne porte pas d'état qui exige de lire la marque `c` pour agir : les cases sont dans la barre ou le panneau, le menu les répète.
* États : le programme **reconstruit le texte** avec les marques `d` et `c`, sans passer par `ui_menu_state` (64 états seulement). Mise à jour regroupée, 200 ms au plus après un changement.
* Libellés dynamiques : « Annuler : Déplacer ». 28 caractères au plus.
* ★ marque le **noyau**, tenu sous les 2 048 octets si `WS_MENU_MAX` ne grandit pas (§ 10). Hors noyau, l'entrée reste dans `Ctrl+K`.
* Ordre dans chaque menu : du plus fréquent au moins fréquent, par groupes.

### 6.2 Fichier

| Entrée | Raccourci | ★ |
|---|---|---|
| Nouveau | Ctrl+N | ★ |
| Nouveau depuis un modèle… | | |
| Ouvrir… | Ctrl+O | ★ |
| Fichiers récents… | | ★ |
| Rouvrir la version enregistrée | | |
| - | | |
| Enregistrer | Ctrl+S | ★ |
| Enregistrer sous… | Ctrl+Maj+S | ★ |
| Enregistrer une copie… | | |
| - | | |
| Importer… | Ctrl+I | ★ |
| Exporter… | Ctrl+E | ★ |
| Imprimer… | Ctrl+P | |
| - | | |
| Document… | Ctrl+Maj+E | ★ |
| Fermer | Ctrl+W | ★ |

« Fichiers récents… » ouvre un dialogue : un menu plat ne peut pas les loger.

### 6.3 Édition

| Entrée | Raccourci | ★ |
|---|---|---|
| Annuler | Ctrl+Z | ★ |
| Rétablir | Ctrl+Y | ★ |
| Historique… | | |
| - | | |
| Couper | Ctrl+X | ★ |
| Copier | Ctrl+C | ★ |
| Coller | Ctrl+V | ★ |
| Coller sur place | Ctrl+Maj+V | ★ |
| Coller le style | Ctrl+Alt+V | ★ |
| Dupliquer | Ctrl+D | ★ |
| Supprimer | Retour | ★ |
| - | | |
| Tout sélectionner | Ctrl+A | ★ |
| Tous les calques | Ctrl+Alt+A | |
| Inverser la sélection | Ctrl+Maj+I | |
| Désélectionner | Ctrl+Maj+A | ★ |
| Sélectionner le même… | | |
| - | | |
| Rechercher… | Ctrl+F | ★ |
| Éditeur XML… | Ctrl+Maj+X | |
| Préférences… | Ctrl+Maj+P | ★ |

### 6.4 Vue

| Entrée | Raccourci | ★ |
|---|---|---|
| Zoom avant | = | ★ |
| Zoom arrière | - | ★ |
| Zoom 100 % | 1 | ★ |
| Zoom 200 % | 2 | |
| Ajuster à la sélection | 3 | ★ |
| Ajuster au dessin | 4 | ★ |
| Ajuster à la page | 5 | ★ |
| Zoom précédent | Alt+Z | |
| - | | |
| Contours | Alt+C | ★ |
| Sans filtres | Alt+F | |
| - | | |
| Règles | Alt+R | ★ |
| Grille | Alt+G | ★ |
| Guides | Alt+U | ★ |
| Aimantation | Alt+A | ★ |
| Ajouter un guide vertical | | |
| Ajouter un guide horizontal | | |
| - | | |
| Panneaux | Tab | ★ |
| Réticule | Alt+Entrée | |

### 6.5 Objet

| Entrée | Raccourci | ★ |
|---|---|---|
| Grouper | Ctrl+G | ★ |
| Dégrouper | Ctrl+Maj+G | ★ |
| - | | |
| Monter | Ctrl+Maj+Haut | ★ |
| Descendre | Ctrl+Maj+Bas | ★ |
| Premier plan | Alt+Maj+Haut | ★ |
| Arrière-plan | Alt+Maj+Bas | ★ |
| - | | |
| Miroir horizontal | Maj+H | ★ |
| Miroir vertical | Maj+V | ★ |
| Quart de tour à droite | Maj+R | |
| Quart de tour à gauche | Maj+L | |
| - | | |
| Transformer… | Ctrl+Maj+M | ★ |
| Aligner… | Ctrl+Maj+L | ★ |
| Cloner | Alt+D | |
| Délier le clone | Alt+Maj+D | |
| Découper par l'objet du dessus | | |
| Masquer par l'objet du dessus | | |
| Retirer découpe et masque | | |
| - | | |
| Nouveau calque | Ctrl+Maj+N | ★ |
| Calques et objets… | Ctrl+L | ★ |

Ce menu est le plus long du lot : voir le décompte du § 6.13.

### 6.6 Chemin

| Entrée | Raccourci | ★ |
|---|---|---|
| Objet vers chemin | Ctrl+Maj+C | ★ |
| Contour vers chemin | Ctrl+Alt+C | ★ |
| Vectoriser une image… | | |
| - | | |
| Union | Ctrl+Alt+U | ★ |
| Différence | Ctrl+Alt+D | ★ |
| Intersection | Ctrl+Alt+I | ★ |
| Exclusion | Ctrl+Alt+E | ★ |
| Division | Ctrl+Alt+B | ★ |
| Couper le contour | | |
| - | | |
| Combiner | Ctrl+Alt+K | |
| Séparer | Ctrl+Maj+K | |
| - | | |
| Décaler… | Ctrl+Alt+O | |
| Simplifier | Ctrl+Alt+L | ★ |
| Inverser le sens | Ctrl+Alt+R | |

### 6.7 Texte

| Entrée | Raccourci | ★ |
|---|---|---|
| Texte et police… | Ctrl+T | ★ |
| - | | |
| Mettre sur le chemin | | ★ |
| Retirer du chemin | | |
| Mettre dans la forme | | |
| Retirer de la forme | | |
| - | | |
| Retirer le crénage | | |
| Insérer un caractère… | | |
| Convertir en chemin | | |

### 6.8 Effets

| Entrée | Raccourci | ★ |
|---|---|---|
| Remplissage et contour… | Ctrl+Maj+F | ★ |
| Nuanciers… | | |
| Symboles… | | |
| - | | |
| Filtres… | | ★ |
| Effets de chemin… | Ctrl+Alt+P | ★ |
| - | | |
| Extensions… | | |
| Dernière extension | Alt+E | |

### 6.9 Aide

| Entrée | Raccourci | ★ |
|---|---|---|
| Rechercher une commande… | Ctrl+K | ★ |
| Raccourcis clavier… | | ★ |
| Gestes… | | |
| - | | |
| Rapport du document… | | |
| À propos de Vector | | ★ |

### 6.10 Recherche de commandes

`Ctrl+K`, ou entrée du menu Aide. Dialogue `ui_dialog_begin`, 480 × 340, plafonné à la zone libre, un champ en haut et une liste virtuelle dessous. Il contient **toutes** les actions : menus, panneaux, outils, bascules. Le filtre
porte sur les mots du libellé, sans accents ni casse. Chaque ligne montre son raccourci. Entrée exécute, flèches pour choisir, Échap ou un clic dehors ferment. Le dialogue est modal, c'est sans gêne : il se ferme avant d'agir.

### 6.11 Menu contextuel

`ui_popup_local` au point. Ouvert par clic droit, appui tenu, Maj+Entrée.

| Contexte | Entrées |
|---|---|
| Objet | Couper, Copier, Coller, Dupliquer, Supprimer, -, Grouper, Monter, Descendre, -, Remplissage et contour, Transformer |
| Vide | Coller, Coller sur place, -, Tout sélectionner, -, Ajuster à la page, Document |
| Nœud | Coin, Lisse, Symétrique, Auto, -, Couper, Joindre, Supprimer |
| Guide | Verrouiller, Supprimer, Propriétés |

### 6.12 Barre d'état

Deux lignes dès H ≥ 600 : palette, puis état. Sinon une ligne, § 2.3.

Ligne d'état, 24 px, Sans 12, `TEXTE2`, de gauche à droite :

| Élément | Largeur | Contenu | Retrait, par manque de place |
|---|---|---|---|
| Calque | 148 | Dropdown : œil, verrou, nom | 7e |
| Pointeur | 120 | `x 120,5  y 88` dans l'unité | 3e |
| Indication | Reste, au moins 120 | Ce que fait l'outil, ce que font les touches, ou le nombre d'objets | 5e |
| Aimantation | 28 | Case | 2e |
| Nom du fichier | 200 | `logo.svg`, coupé par `ui_text_fit`, `•` si modifié | 4e |
| Rendu | 64 | `Rendu…` en `TEXTE3` tant que des tuiles attendent, sinon vide | 1er |
| Zoom | 92 | Moins, valeur, plus | Jamais |

**Indications** : phrase courte, à l'impératif, liée à l'état : « Glissez pour créer. Ctrl : carré. Alt : depuis le centre. » Elles passent par `ui_tr`.

**En classe S**, l'indication se peint en bas du canevas : bande de 20 px, fond `FACE` à 85 %, texte `TEXTE1` 12 px. Elle apparaît 3 s après un changement d'outil et pendant un glisser. Une
préférence la coupe. Le nom du fichier n'est pas visible en permanence : il apparaît 3 s dans cette bande à l'ouverture et à l'enregistrement.

### 6.13 Décompte

Texte de `ui_menu`, compté avec des identifiants de trois chiffres, sans marques `d` ni `c` (2 octets chacune). Hauteur à 800 × 600, marges comprises, pour 572 px disponibles.

| Menu | Entrées | Séparateurs | Hauteur | Octets | Octets du noyau ★ |
|---|---|---|---|---|---|
| Fichier | 13 | 3 | 383 | 367 | 239 |
| Édition | 18 | 3 | 513 | 503 | 347 |
| Vue | 18 | 3 | 513 | 430 | 267 |
| Objet | 19 | 4 | 550 | 583 | 356 |
| Chemin | 14 | 3 | 409 | 406 | 241 |
| Texte | 8 | 2 | 242 | 230 | 69 |
| Effets | 7 | 2 | 216 | 195 | 107 |
| Aide | 5 | 1 | 153 | 146 | 101 |
| **Total** | **102** | | | **2 860** | **1 727** |

Le menu complet dépasse la limite de 812 octets. Le noyau tient, marques comprises : il reste 321 octets.

## 7. Clavier

### 7.1 Règles de lecture

1. **Lettres sans Ctrl** : lues par `EV_CHAR`, qui suit la disposition. Maj et Caps ne changent pas la lettre : la comparaison ignore la casse, l'état de Maj vient des modificateurs.
2. **Lettres avec Ctrl** : pas de caractère (F8). L'app traduit le code HID par la disposition active (§ 10, P0). Le libellé du menu dit donc vrai sur tout clavier.
3. **Chiffres 1 à 5** : par **code HID** (0x1E à 0x22), sans modificateur. En AZERTY, ces touches donnent `&é"'(` sans Maj. C'est la recette de Circuits. Pas de 6 : sur AZERTY, sa touche tape `-`.
4. **`=` et `-`** : par caractère. Tous deux sont sans Maj sur AZERTY comme sur le PocketTerm.
5. **Alt droit (AltGr)** ne compte pas comme Alt pour un raccourci (bit 0x40) : il sert à taper. Seul Alt gauche (0x04) compte.
6. **Jamais** : F10, F11, Ctrl+Alt+Gauche ou Droite, signes de ponctuation, Super.
7. **Dans un champ** : le champ prend d'abord. Ctrl+Z dans un champ annule le champ. Échap rend le focus au canevas.
8. **Touches optionnelles** : Suppr, Début, Fin, Page, F1 restent acceptées sur un grand clavier. Aucune n'est seule à porter une fonction.

### 7.2 Tableau

Colonne PocketTerm : ce qu'il faut faire si ce n'est pas la même touche.

| Touche | Action | PocketTerm |
|---|---|---|
| V | Sélecteur | |
| A | Nœuds | |
| R | Rectangle | |
| E | Ellipse | |
| Y | Étoile | |
| S | Spirale | |
| O | Boîte 3D | |
| P | Stylo | |
| N | Crayon | |
| B | Calligraphie | |
| T | Texte | |
| G | Dégradé | |
| M | Maillage | |
| F | Seau | |
| I | Pipette | |
| W | Gomme | |
| K | Constructeur de formes | |
| U | Ajustement | |
| J | Pulvérisation | |
| L | Connecteur | |
| Q | Mesure | |
| D | Pages | |
| H | Main | |
| Z | Zoom | |
| X | Échange remplissage et contour | |
| C | Panneau Remplissage et contour | |
| Espace tenu | Main temporaire | |
| Tab | Panneaux | |
| Entrée | Termine, applique, valide | |
| Échap | Abandonne, puis Sélecteur | Fn+Q ; sans Fn : `V`, Ctrl+Entrée, case abandonner |
| Retour | Supprime la sélection, ou le dernier point | |
| Suppr | Comme Retour | Fn+Retour |
| Ctrl+Tab | Objet ou nœud suivant | |
| Ctrl+Maj+Tab | Objet ou nœud précédent | |
| Maj+Entrée | Menu contextuel | |
| Alt+Entrée | Réticule | |
| Ctrl+Entrée | Valide le texte | |
| Flèches | Déplace la sélection de 1 unité ; sans sélection, déplace la vue de 48 px | Croix |
| Maj+flèches | × 10 | Croix |
| Alt+flèches | 1 px d'écran | Croix |
| Ctrl+Gauche ou Droite | Rotation de 15° | Croix |
| Ctrl+Haut ou Bas | Échelle de ± 10 % | Croix |
| Ctrl+Maj+Gauche ou Droite | Rotation de 1° | Croix |
| Ctrl+Maj+Haut | Monter | Croix |
| Ctrl+Maj+Bas | Descendre | Croix |
| Alt+Maj+Haut | Premier plan | Croix |
| Alt+Maj+Bas | Arrière-plan | Croix |
| = | Zoom avant | |
| - | Zoom arrière | |
| 1 | Zoom 100 % | |
| 2 | Zoom 200 % | |
| 3 | Ajuster à la sélection | |
| 4 | Ajuster au dessin | |
| 5 | Ajuster à la page | |
| Maj+H | Miroir horizontal | |
| Maj+V | Miroir vertical | |
| Maj+R | Quart de tour à droite | |
| Maj+L | Quart de tour à gauche | |
| Alt+R | Règles | |
| Alt+G | Grille | |
| Alt+U | Guides | |
| Alt+A | Aimantation | |
| Alt+C | Contours | |
| Alt+F | Sans filtres | |
| Alt+Z | Zoom précédent | |
| Alt+D | Cloner | |
| Alt+Maj+D | Délier le clone | |
| Alt+E | Dernière extension | |
| Ctrl+N | Nouveau | |
| Ctrl+O | Ouvrir | |
| Ctrl+S | Enregistrer | |
| Ctrl+Maj+S | Enregistrer sous | |
| Ctrl+I | Importer | |
| Ctrl+E | Exporter | |
| Ctrl+P | Imprimer | |
| Ctrl+W | Fermer | |
| Ctrl+Q | Quitter | |
| Ctrl+Z | Annuler | |
| Ctrl+Y | Rétablir | |
| Ctrl+Maj+Z | Rétablir | |
| Ctrl+X | Couper | |
| Ctrl+C | Copier | |
| Ctrl+V | Coller | |
| Ctrl+Maj+V | Coller sur place | |
| Ctrl+Alt+V | Coller le style | |
| Ctrl+D | Dupliquer | |
| Ctrl+A | Tout sélectionner | |
| Ctrl+Alt+A | Tout, dans tous les calques | |
| Ctrl+Maj+A | Désélectionner | |
| Ctrl+Maj+I | Inverser la sélection | |
| Ctrl+F | Rechercher | |
| Ctrl+K | Rechercher une commande | |
| Ctrl+G | Grouper | |
| Ctrl+Maj+G | Dégrouper | |
| Ctrl+L | Calques et objets | |
| Ctrl+T | Texte et police | |
| Ctrl+Maj+F | Remplissage et contour | |
| Ctrl+Maj+M | Transformer | |
| Ctrl+Maj+L | Aligner | |
| Ctrl+Maj+N | Nouveau calque | |
| Ctrl+Maj+E | Document | |
| Ctrl+Maj+P | Préférences | |
| Ctrl+Maj+X | Éditeur XML | |
| Ctrl+Maj+C | Objet vers chemin | |
| Ctrl+Alt+C | Contour vers chemin | |
| Ctrl+Alt+U | Union | |
| Ctrl+Alt+D | Différence | |
| Ctrl+Alt+I | Intersection | |
| Ctrl+Alt+E | Exclusion | |
| Ctrl+Alt+B | Division | |
| Ctrl+Alt+K | Combiner | |
| Ctrl+Maj+K | Séparer | |
| Ctrl+Alt+O | Décaler | |
| Ctrl+Alt+L | Simplifier | |
| Ctrl+Alt+R | Inverser le sens | |
| Ctrl+Alt+P | Effets de chemin | |

Dans le Texte, Ctrl+B et Ctrl+I passent avant tout. Ctrl+I reste Importer hors du texte.

Toute la table est modifiable dans `SD0:/home/vector/keys.ini`, de la forme `action = touche`. Elle sert aussi à régénérer les libellés de menus.

### 7.3 Note de pouces

Sur le PocketTerm, Ctrl et Alt sont à gauche de la barre d'espace, et l'unique Maj au bout droit de l'avant-dernier rang. Les combinaisons à deux modificateurs demandent donc les deux pouces et une
lettre : à essayer sur l'appareil. Aucune ne porte une action fréquente, et rien n'est sur Ctrl+Maj+Alt. Les boutons A, B, X, Y, L, R tapent des lettres (F12) : ils choisissent donc Nœuds, Calligraphie, l'échange de couleurs,
Étoile, Connecteur et Rectangle. C'est une coïncidence acceptée, on pourra les remapper dans `keys.ini` après un essai.

## 8. Tactile et souris

T0 : les gestes traduits par `gt911`, aujourd'hui (F14). T1 : mode brut, étape 7.

**Reconnaître un doigt** : UIKit ne dit pas d'où vient le pointeur. Tant que § 10 n'est pas fait, une préférence le tranche : « Mode tactile : automatique » veut dire actif si l'écran fait 800 px de large
ou moins. Elle change les cibles (§ 5.1), la loupe, le décalage, la tolérance.

| Intention | Souris | T0 | T1 |
|---|---|---|---|
| Clic | Bouton gauche | Tape | Tape |
| Double-clic | Double | Double tape | Double tape |
| Glisser l'outil | Tenu et glissé | Départ de côté ; ou tenir **400 ms**, puis glisser dans tout sens | Un doigt, dans tout sens |
| Tracer verticalement | Glisser | Tenir 400 ms d'abord : un départ vertical serait une molette | Un doigt |
| Menu contextuel | Bouton droit | Tenir **600 ms** sans bouger | Tenir 500 ms sans bouger |
| Ajouter à la sélection | Maj+clic | Maj + tape | Maj + tape |
| Déplacer la vue, vertical | Molette | Un doigt vertical, ou deux doigts | Deux doigts |
| Déplacer la vue, horizontal | Maj+molette | Outil Main, ou Maj + molette de deux doigts, ou croix | Deux doigts |
| Déplacer la vue, libre | Espace tenu, bouton du milieu | Outil Main | Deux doigts |
| Zoom | Ctrl+molette | Ctrl + deux doigts verticaux | Pincer |
| Zoom sur une zone | Outil Zoom, glisser | Outil Zoom | Outil Zoom |
| Annuler | Ctrl+Z | Case annuler, Ctrl+Z | Tape à deux doigts, case, Ctrl+Z |
| Rétablir | Ctrl+Y | Case rétablir | Tape à trois doigts ou case |
| Choisir un outil | Clic | Tape | Tape |
| Famille d'outils | Clic sur l'actif, clic droit | Tape sur l'actif | Tape sur l'actif |
| Défiler dans un panneau | Molette | Un doigt vertical | Un doigt vertical |
| Régler un champ | Molette, glisser le libellé | Deux doigts sur le champ, flèches, croix | Glisser le libellé |
| Palette : remplissage | Clic | Tape | Tape |
| Palette : contour | Maj+clic | Maj + tape, menu | Idem |
| Palette : défiler | Molette | Glisser de côté | Glisser de côté |
| Survol | Oui | Absent : toute information doit exister sans lui | Absent |

Détails T0 :

* L'appui n'arrive à l'app qu'à 10 px de côté ou 400 ms. Une tape arrive en appui plus relâchement collés. Le retour visuel ne doit pas attendre un survol.
* Pour le menu contextuel, l'appui reçu reste immobile (6 px) 200 ms de plus : il ouvre. Sinon c'est un glisser. Total 600 ms.
* Un seul doigt vertical donne une molette : dans le canevas, elle déplace la vue. L'outil Crayon en T0 demande donc de tenir d'abord. L'indication le dit.
* Un glisser en cours lâche si un second doigt se pose (F14) : l'action se termine, elle ne s'annule pas.

Mode T1 : les contacts arrivent avec leur identifiant, et un seul doigt reste l'outil. Le mode brut ne couvre que **la zone du canevas** : ailleurs, panneaux compris, les gestes de T0 restent vrais, et un doigt
vertical y fait défiler.

## 9. États, erreurs, enregistrement

### 9.1 États vides

| État | Ce qu'on voit |
|---|---|
| Premier lancement, aucun fichier | Page A4 vide, Sélecteur actif. Au centre de la page, en `TEXTE3`, peint et non dans le document : « R rectangle, P stylo, T texte. Ctrl+K pour tout chercher. » Il disparaît au premier objet |
| Calque verrouillé ou masqué | Au premier essai de dessin : bandeau « Calque verrouillé. Déverrouiller ? » avec bouton dans la bande d'indication |
| Rien de sélectionné | Barre de Sélecteur grisée, panneaux avec leur phrase |
| Pas de résultat de recherche | « Aucun objet ne correspond. » |
| Bibliothèque de symboles vide | « Sélectionnez des objets, puis touchez + pour créer un symbole. » |
| Une seule police | Texte du panneau : « DejaVu seulement. Installez d'autres polices avec pkg. » |
| Tuiles en cours | Tuile agrandie ou `CREUX`, jamais de sablier, jamais de blanc |

### 9.2 Messages

Trois niveaux seulement.

| Niveau | Support | Durée | Emploi |
|---|---|---|---|
| Indication | Bande ou barre d'état | 3 s ou jusqu'à la suivante | « Rien à grouper » |
| Bandeau | Bande de 28 px au bas du canevas, peinte, fond `ALERTE` ou `DANGER`, texte `ui_ink`, avec un bouton ; zone tactile 32 px | Jusqu'à l'action, ou 8 s | Polices manquantes, opération refusée |
| Dialogue | `ui_message`, modal | Jusqu'à réponse | Perte de données possible, seulement |

Un message dit ce qui s'est passé et ce qu'on peut faire. Il ne s'excuse pas.

| Cas | Comportement |
|---|---|
| Fichier illisible, XML cassé | Dialogue : ligne et colonne de la première erreur. Le document courant ne change pas |
| Éléments inconnus | Gardés et réécrits (plan § 3.2). Rien à dire, sinon dans le Rapport du document |
| Élément non rendu | Pastille `ALERTE` dans le bas : « 3 éléments non rendus », ouvre le Rapport |
| Polices manquantes | Bandeau : « 2 polices manquantes, remplacées à l'écran. » Le nom d'origine reste écrit dans le fichier |
| Booléen raté | Rien ne change, indication : « Opération impossible sur ces chemins. Essayez Simplifier. » Le contrôle d'aire du plan (R2) passe après l'opération |
| Filtre trop lent | Rendu de brouillon au-delà de 300 ms ; texte `Rendu…`, jamais de blocage |
| Mémoire basse | Bandeau : les tuiles retombent en basse résolution, le journal se réduit |
| Disque plein, écriture refusée | Dialogue ; le fichier d'origine reste intact (§ 9.3) ; proposer Enregistrer sous |
| Fichier changé sur le disque | À l'enregistrement : dialogue Remplacer, Enregistrer sous, Annuler |
| Format d'export impossible | Entrée grisée avec sa raison dans l'indication |

### 9.3 Enregistrement

* **SVG natif, écriture sûre** : fichier temporaire à côté, relecture de la taille, renommage, ancien gardé en `.bak` une génération.
* Sérialisation sur le fil principal, sur une copie instantanée si le document dépasse 200 000 nœuds. Écriture dans un thread, à cause de la lenteur de la SD. Pendant ce temps, on peut dessiner.
* Indicateur : point modifié `ALERTE` (S), `•` après le nom (M+). Pendant l'écriture, `Enregistrement…` dans la bande. Ensuite, 2 s : « Enregistré, 124 Ko ».
* **Fermer avec changements** : `ui_message`, `UM_YESNOCANCEL` : « Enregistrer les changements de logo.svg ? » Enregistrer, Ne pas enregistrer, Annuler.
* Fichier Ouvrir ou Nouveau alors que le document courant n'est pas vide : **nouveau process, nouvelle fenêtre**. Un document par fenêtre, pas d'onglets : 792 px ne les logent pas, et les pages du code sont partagées.
* Export, Filtres : barre de progression dans le panneau. Annulable. Le dessin reste possible.

### 9.4 Récupération

* Copie automatique : `SD0:/home/vector/recovered/<nom>-<id>.svg`, toutes les 60 s **si** le document a changé. Écrite par le thread d'enregistrement. Sans effet visible, sauf `Copie…` en `TEXTE3` dans le bas (M+).
* Effacée à la fermeture propre. Modèle : `recovered.odp` de Slides.
* Au lancement, si une copie existe : dialogue en liste : nom, date, taille, avec **Rouvrir**, **Plus tard** et **Supprimer**. Une copie rouverte s'ouvre comme document sans nom, modifié.
* `Ctrl+Z` traverse l'enregistrement. Le journal n'est pas écrit sur le disque : une copie rouverte commence un nouvel historique, ce que le dialogue n'a pas à expliquer.
* Plantage pendant un rendu de tuile : rien à faire de plus, la copie a au plus 60 s.

## 10. Ce qu'il faut ajouter à UIKit et autour

| Pr. | Ajout | Où | Étape | Si cela manque |
|---|---|---|---|---|
| **P0** | `WS_MENU_MAX` de 2 048 à 8 192 octets. Soit 32 × 6 Ko de plus dans Elegant. Vérifier `uk_menubuf`, le tampon de `menubar`, le message de `WS_OP`. UIKit doit **refuser** plutôt que couper | Elegant, UIKit, menubar | 2 | Menus réduits au noyau ★. Tout le reste dans `Ctrl+K`. Pas de perte de fonction, mais les menus cessent d'être complets |
| **P0** | `ui_key_char (hid, mods)` : le caractère de la disposition active, même avec Ctrl. Et `ui_menu_key` qui l'utilise pour les lettres : corrige toutes les apps en AZERTY | HIDManager, UIKit | 2 | Table FR et US dans l'app, d'après `keyb=` d'`options.txt`. Autres dispositions fausses ; changement à chaud ignoré |
| **P0** | Mesure : points intermédiaires de souris à 60 Hz sans regroupement | Elegant | 0 | Crayon et Calligraphie anguleux |
| **P1** | Source du pointeur dans `WEV_MOUSE` : souris ou tactile | HIDManager, Elegant, UIKit | 3 | Préférence manuelle. Souris sur un PocketTerm : cibles trop larges, loupe inutile |
| **P1** | `ui_cursor_image (pixels, l, h, x, y)`, 32 × 32 au plus, teinté par l'app | Elegant, UIKit | 2 | Table du § 5.7, colonne En attendant. Moins lisible, utilisable |
| **P1** | Mode brut par fenêtre et par zone : contacts, identifiant, pincement | gt911, Elegant, UIKit | 7 | T0 pour toujours : tracer demande de tenir 400 ms, pas de pincement ni de pan libre. **Le plus gros sacrifice tactile** |
| **P1** | `ui_popup_ex` : icône en pixels par entrée | UIKit | 2 | Texte et lettre seuls dans les familles, et les marqueurs et tirets décrits par des mots |
| **P1** | Champ réel à unité : expression, pas, molette, `UTF_RIGHT` | UIKit, d'abord dans l'app | 2 | Reste dans l'app sur `ui_textbox`. Pas de partage avec d'autres apps, pas de perte |
| **P2** | Événement de changement de thème | Elegant, UIKit | 2 | Relire les jetons à `WEV_FOCUS`, un repeint de retard |
| **P2** | Plein écran du contenu : sans barre haute ni panel, 800 × 600 utiles (`WF_CURTAIN` vu de plus près) | Elegant, UIKit | 3 | 80 px de hauteur perdus à 800 × 600, soit 13 % de canevas |
| **P2** | Sous-menus (`>`) dans `app_menu` | menubar | 4 | Menus plats, dialogues « Fichiers récents… », « Sélectionner le même… ». Déjà la référence de ce document |
| **P2** | Molette horizontale et position dans le rappel de vue | Elegant, UIKit | 2 | L'app lit `ui.ev` (plan § 5). Pas de roue horizontale |
| **P2** | `ui_tree_virtual`, `ui_tree_cell`, glisser | UIKit | 4 | Liste à cellules (§ 4.4). L'éditeur XML et Calques n'en ont pas besoin |
| **P2** | Icônes teintées dans `ui_toolbar` et onglets | UIKit | 2 | Tout passe par `ui_view`, comme Circuits. Le coût est pour les autres apps, pas pour Vector |
| **P3** | Fenêtre outil toujours au-dessus (`WF_TOOL`) | Elegant, UIKit | 4 | Pas de panneaux détachés à 1920 × 1080. Les deux colonnes empilées suffisent |
| **P3** | `ui_file_preview` | UIKit | 7 | Ouvrir sans aperçu ; Fichiers récents montrent le nom seul |
| **P3** | Redimensionnement en direct | Elegant | 11 | La mise en page saute au relâchement |
| **P3** | Menus qui défilent | UIKit | | Limite de 19 entrées maintenue |
| **P3** | Option « ce widget garde son glisser » dans les 36 px du haut | UIKit | 2 | Pas de glisser sur libellé dans la barre. Molette et flèches suffisent |
| **P3** | Select et Start livrés à l'app | HIDManager | 7 | Pas de touches de plus |

Ne manque pas, et ne doit pas être ajouté : modificateurs à l'écran (le PocketTerm a son clavier), clavier virtuel, barre de titre, mise en page par fichiers, thèmes de l'app.

## 11. Critères de revue sur captures QEMU

Chaque capture existe **deux fois**, Givre et Onyx Noir, à 800 × 600, et à 1920 × 1080 pour les étapes 3, 5, 7 et 11. `qemu_drive` : AZERTY, attente de 24 s. QEMU ne juge ni le tactile ni la vitesse.

**À chaque capture** :

* Le haut droit de la fenêtre reste vide sur 107 px, les trois glyphes du cadre ne touchent rien.
* Aucune couleur figée : une capture convertie de thème ne change que par les jetons. Rien de blanc dur en Onyx Noir, sauf le document et l'encre d'accent.
* Canevas à la taille du § 2 au pixel près, pas de bande vide d'1 px autour.
* Le bureau autour de la page est `CREUX`. Une poignée, un guide restent lisibles sur un carré noir et sur un carré blanc.
* Texte à 12 px ou plus, 11 px pour les règles seulement. Pas de libellé coupé sans « … ».
* Grille de 4 px, 8 px entre groupes. Icônes alignées sur le même axe.
* Tous les titres de menus sont visibles à 800 × 600.

| Étape | Capture | Ce que je regarde |
|---|---|---|
| 0 | Fenêtre, scène en dur | Espace libre du cadre, tailles du § 2.3 ; `CREUX` ; pas de bavure aux coins de la surface |
| 1 | SVG ouvert, zoom 400 % | Tuiles sans couture, indication de mode, barre de Zoom, 100 % correct contre `--render` |
| 2 | Rail, chaque outil actif | Icône d'état actif lisible, barre sans chevauchement ; chaque barre aux quatre tailles, `⋯` quand il faut |
| 2 | Sélection de 8 objets ; objet de 20 px ; objet tourné | Poignées selon § 5.1, double trait, poignées hors des petits objets, rotation visible |
| 2 | Tracé au stylo en cours | Segment fantôme, anneau de fermeture, indication juste |
| 2 | Nœuds | Trois formes de nœuds distinctes à 9 px sur l'écran réel ; choix bien marqué |
| 2 | Tiroir ouvert, Remplissage | Canevas poussé, sélection encore visible, zone de couleur à 232 × 112, pas de champ coupé, défilement |
| 2 | Annuler dans le menu | Libellé dynamique, entrées grisées, longueur à 800 × 600 sans dépasser |
| 3 | Calques, Texte, Dégradé | Lignes de 28 px, œil et verrou à droite ; caret ; arrêts lisibles sur fond damier |
| 3 | Export | Seul bouton principal de l'app. Barre de progression, dessin possible derrière |
| 4 | Guides, grille, aimantation | Hiérarchie : la grille ne doit jamais égaler un guide. Croix d'aimantation et libellé |
| 4 | Aligner | Grille de cases sans libellés coupés à 248 px |
| 5 | Filtre appliqué à un gros objet | Brouillon puis final, `Rendu…`, pointeur vivant |
| 6 | Effet de chemin | Poignées de paramètres distinctes des nœuds |
| 7 | Chaque outil restant ; mode tactile forcé | Cibles visibles, loupe placée à l'opposé du doigt, décalage du Stylo |
| 7 | Sur le Pi, au doigt : essai réel | Tape sur une case du rail, une poignée sans viser longtemps ; fréquence d'erreur |
| 9 | Dialogues générés | Hauteur à 800 × 600, jetons, pas de débordement |
| 11 | Reprise générale | Toutes les lignes de ce tableau, puis un fichier réel à 800 × 600 du début à la fin, clavier seul, sans souris |

Sur le Pi à chaque étape ★ : le test des 7 mm. Mesurer le taux d'erreur à la tape sur le rail. S'il dépasse 10 %, élargir d'abord les cibles fantômes, puis réduire le nombre de cases.

## 12. Résumé

1. **Le canevas prime** : pas de menus dans la fenêtre (ils sont dans la barre globale), barre de 36 px, rail de 40 px, bas de 32 px à 800 × 600 : canevas de 752 × 444.
2. **Le tiroir pousse** le canevas au lieu de le couvrir : sélection et réglages restent visibles ensemble.
3. **Pas de modal pour ce qui se regarde** : couleur, filtres, transformations sont dans des panneaux vivants.
4. **Huit menus plats**, limités par la largeur et 2 048 octets, plus `Ctrl+K` qui contient tout.
5. **Raccourcis lettres et chiffres** : aucun signe, aucune touche F, Échap jamais seul, Ctrl+lettre lu selon la disposition.
6. **Un seul état de poignées**, cibles tactiles de 44 à 48 px, le plus proche gagne, loupe et réticule pour la précision.

**Les trois décisions les plus discutables** :

* **Tiroir qui pousse plutôt qu'un panneau par-dessus** : à 800 px, il laisse 504 px de canevas. C'est le prix de voir l'effet en direct, et il coûte un `ui_surface_move` à chaque ouverture.
* **Raccourcis d'outils à la façon des éditeurs de maquettes** (`V`, `A`, `P`), et non ceux d'Inkscape : meilleurs au clavier de poche, mais ils déroutent les habitués, d'où `keys.ini`.
* **40 px pour le rail à 11,25 px/mm, soit 3,6 mm** : très en dessous des 7 mm d'usage au doigt. J'assume que le PocketTerm se pilote surtout à la croix, au clavier et à la souris USB, et à l'ongle ou au stylet passif pour le rail. Si l'essai réel de l'étape 7 le contredit, il faudra un rail à deux pages.
