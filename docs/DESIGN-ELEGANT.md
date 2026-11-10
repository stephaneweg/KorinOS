# Elegant — spécification de design (bureau moderne, dock d'Onyx)

> **Depuis le 1/11/2026** : les fenêtres n'ont plus de barre de titre, le panel remplace le dock, et seuls les thèmes Givre et Onyx Noir existent. Les sections sur la barre de titre, le dock et les styles CDE et Milk décrivent l'état antérieur ; `docs/FEUILLE-DE-ROUTE.md` dit l'état actuel.

> **Depuis le 2/11/2026** : palette, police (Inter), ombres et géométrie du cadre (bord 12, rayon 10) suivent `docs/DESIGN-PROPOSAL.md` ; les valeurs de ce document sont celles d'avant quand elles diffèrent.

Document de référence pour le bureau Elegant (chantier 4.3 de `CHANTIER4-GUI.md`) : couleurs,
tailles, états, et pour chaque élément **la recette de dessin** avec les primitives de UIKit.
Maquettes : `docs/maquettes/elegant-800x600.svg` (thème Givre, échelle M) et
`docs/maquettes/elegant-1024x768.svg` (thème Onyx Noir, échelle L).

Conventions :

* Couleurs `0xRRGGBB`. Avec transparence : `0xTTRRGGBB`, **TT = transparence** (0x00 opaque,
  0xFF invisible), comme l'octet haut des pixels des fenêtres `WF_ALPHA` et des cadres.
* `ton(c, n)` = `ui_tone` (n 0..255, 128 = inchangée, < 128 plus sombre, > 128 plus clair) ;
  `mêler(a, b, t)` = `ui_mix` (t 0..256 = part de b) ; `encre(c)` = `ui_ink`.
* `boîte(x, y, l, h, r, haut, bas)` = `ui_rbox` (dégradé vertical, coins sans anticrénelage) ;
  `plein` = `ui_fill` ; `contour` = `ui_frame` (1 px).
* **Contour arrondi** (n'existe pas en primitive) : `boîte` de la couleur du contour, puis
  `boîte` intérieure en retrait de 1 px, rayon − 1. Pour un **anneau** semi-transparent dans une
  surface `WF_ALPHA` : on écrit les pixels (pas de mélange dans son propre tampon), donc on
  dessine de l'extérieur vers l'intérieur, chaque anneau écrasant le centre du précédent.
* Les tailles de texte sont en px (rastériseur TrueType maison, `ui_text`, y = ligne de base).
  **Ligne de base d'un texte centré** dans une boîte de hauteur h : `y + arrondi((h + 0,72 × taille) / 2)`
  (h 28, taille 13 → y + 19 ; h 26 → y + 18 ; h 24 → y + 17 ; h 30 → y + 20).

---

## 1. Principes

1. **Plat et calme** : aplats, dégradés à peine visibles (± 12 niveaux de `ui_tone`), une seule
   couleur vive (l'accent) pour l'état « choisi / actif ».
2. **Profondeur par l'ombre, pas par le relief** : fenêtres, menus, tiroirs et dock flottent
   au-dessus du fond grâce à des anneaux d'ombre transparents (section 3.2), plus de biseaux CDE.
3. **Rayons cohérents** : 5–6 px (contrôles, lignes survolées), 8 px (fenêtres, menus),
   12 px (tiroirs), 16–18 px (dock), moitié du côté pour les pastilles.
4. **Grille de 4 px** pour les espacements, **cibles de 24 px au moins** (32 sur le PocketTerm
   pour le tactile, voir section 11).
5. **Le dock garde la structure d'Onyx** (tiroirs à languette, lanceurs, pupitre central avec
   espaces de travail et boutons système, corbeille, points des applis ouvertes, tailles
   80/60/40 réduites jusqu'à 60/34/22) ; seul le rendu change.

---

## 2. Échelles (tailles selon l'écran)

L'échelle est choisie au démarrage de chaque programme (UIKit, d'après `ui_screen`) : **L** si
la hauteur ≥ 720, **M** si 540 ≤ hauteur < 720, **P** sinon (et aussi P si la largeur < 720).
1024x768 → L, 800x600 (PocketTerm) → M, 640x480 → P. Toutes les valeurs ci-dessous existent en
table dans UIKit (`uk_echelle`, 3 colonnes) ; `SD0:/etc/theme.txt` peut forcer `echelle = L|M|P`
(clé nouvelle, absente = `auto`).

| Élément | L | M | P |
|---|---|---|---|
| Barre de menus (hauteur) | 30 | 28 | 26 |
| Police d'interface / petite / titre de section | 13 / 12 / 15 | 13 / 12 / 15 | 12 / 11 / 14 |
| Barre de titre (`FRAME_TITLE_H`) | 28 | 28 | 26 |
| Bouton, champ, onglets (hauteur) | 30 | 28 | 26 |
| Ligne de liste | 28 | 26 | 24 |
| Ligne de menu | 28 | 26 | 24 |
| Marge du contenu d'une fenêtre | 16 | 12 | 8 |
| Dock DH / CW / ICON (de base) | 80 / 60 / 40 | 72 / 56 / 40 | 64 / 50 / 36 |
| Dock minima (dock trop large) | 60 / 34 / 22 | 56 / 34 / 22 | 52 / 34 / 22 |
| Dock GAP (vide transparent dessous) | 12 | 6 | 4 |
| Dock rayon / marge sous l'icône (`BAS`) | 18 / 14 | 16 / 14 | 14 / 12 |
| Languette (`STRIP_H`, zone cliquable) | 16 | 16 | 14 |
| Tiroir RH / SI / COLW | 40 / 28 / 200 | 38 / 28 / 184 | 34 / 24 / 168 |
| Carré d'espace de travail SQW × SQH | 34 × 25 | 30 × 22 | 26 × 19 |
| Barre de défilement (largeur) | 8 | 10 | 10 |

---

## 3. Couleurs et thèmes

### 3.1 Jetons

UIKit calcule au chargement du thème une table de **jetons** (`uk_jetons`, 4 octets chacun,
`TJ_*`), que tous les dessins utilisent (fini les constantes `UI_*` figées de `uikit.inc`) :

| Jeton | Rôle | Givre (clair) | Onyx Noir (sombre) |
|---|---|---|---|
| `TJ_FACE` | fond des fenêtres, barres d'outils | `0xF5F6F8` | `0x15171B` |
| `TJ_SURFACE` | champs, listes, menus, tiroirs | `0xFFFFFF` | `0x0B0C0E` (menus, tiroirs : `TJ_CREUX`) |
| `TJ_CREUX` | survol discret, fond des onglets | `0xEBEEF2` | `0x1F2228` |
| `TJ_ELEVE` | face des boutons | `0xFFFFFF` → `0xF4F5F7` | `0x2C3038` |
| `TJ_LIGNE` | séparateurs, bord des listes | `0xDDE1E7` | `0x343842` |
| `TJ_LIGNE_FORTE` | bord des boutons et champs, pouce de défilement | `0xC3C9D2` | `0x4A505C` |
| `TJ_TEXTE1` | texte principal | `0x1B1F24` | `0xECEEF2` |
| `TJ_TEXTE2` | texte secondaire, glyphes | `0x5B6472` | `0xA4AAB5` |
| `TJ_TEXTE3` | désactivé, indications, en-têtes | `0x8A93A0` | `0x717783` |
| `TJ_ACCENT` | sélection active, bouton principal, focus | `0x2F74E0` | `0x3D86DA` |
| `TJ_ACCENT_CLAIR` | sélection sans focus, titre de menu ouvert | `0xDCE8FB` | `0x1E3350` |
| `TJ_ACCENT_FONCE` | accent appuyé, bord du bouton principal | `0x245DBA` | `0x2F6DB5` |
| `TJ_ACCENT_VIF` | points des applis ouvertes | `0x2F74E0` | `0x7DB4F2` |
| `TJ_DANGER` / `TJ_SUCCES` / `TJ_ALERTE` | fermer survolé, alimentation / réussite / avertissement | `0xE5484D` / `0x30A46C` / `0xF1B33B` | `0xE5484D` / `0x46A758` / `0xF1B33B` |
| `TJ_BARRE` | barre de menus | `0x1AF7F8FA` | `0x1A15171B` |
| `TJ_DOCK` | face du dock | `0x33F2F4F7` | `0x261F2228` |
| `TJ_TITRE_ACTIF` | barre de titre active | `0xE9EDF2` | `0x1F2228` |
| `TJ_TITRE_INACTIF` | barre de titre inactive | `0xF5F6F8` | `0x15171B` |
| `TJ_CONTOUR` | anneau de 1 px autour des fenêtres, menus | `0xC4000000` | `0x40000000` |

Le titre de menu ouvert (barre de menus) utilise `TJ_ACCENT_CLAIR` en Givre, `0x2C3038` en
Onyx Noir (= `TJ_ELEVE`).

### 3.2 Ombres (anneaux)

Une ombre est **trois anneaux de 1 px de noir transparent** autour de l'objet, puis l'anneau de
contour (`TJ_CONTOUR`). Coûte 4 boîtes. Transparences (de l'extérieur vers l'intérieur) :

| | anneau 0 | anneau 1 | anneau 2 | contour |
|---|---|---|---|---|
| Fenêtre active, menu, tiroir | `0xF4` | `0xEC` | `0xE0` | `TJ_CONTOUR` |
| Fenêtre inactive | `0xF8` | `0xF2` | `0xEA` | TT de `TJ_CONTOUR` + 0x10 |
| Dock | `0xF6` | `0xEE` | `0xE3` | voir 8.3 |

### 3.3 Thèmes et `SD0:/etc/theme.txt`

Les clés restent celles d'Onyx (`theme`, `style`, `active`, `inactive`, `window`, `button`,
`field`, `accent`, `menubar`, `dock`, `outline`) ; ARMPI lit en plus `button` et `field`
(noms d'Onyx, ignorés jusqu'ici). Nouveautés : la valeur **`style = plat`** (le look de ce
document ; `cde` et `milk` restent), les thèmes nommés **Givre** et **Onyx Noir** (nom hérité
d'Onyx), et la clé facultative `echelle`.

Givre (thème clair par défaut d'ARMPI) :

```
theme    = Givre
style    = plat
active   = 0xE9EDF2
inactive = 0xF5F6F8
window   = 0xF5F6F8
button   = auto
field    = auto
accent   = 0x2F74E0
menubar  = 0xF7F8FA
dock     = 0xF2F4F7
outline  = dark
```

Onyx Noir (sombre, valeurs des jetons `UK_N_*` d'Onyx) :

```
theme    = Onyx Noir
style    = plat
active   = 0x1F2228
inactive = 0x15171B
window   = 0x15171B
button   = 0x2C3038
field    = 0x0B0C0E
accent   = 0x3D86DA
menubar  = 0x15171B
dock     = 0x1F2228
outline  = black
```

Table `uk_named` : ajouter `givre` (cadre `0xE9EDF2`, style plat) et `onyx noir`
(`0x1F2228`, style plat ; nom à espace : comparer la valeur entière jusqu'à la fin de ligne,
espaces de fin retirés). Les thèmes Peach, Steel, Sage, Brick, Slate, Milk restent ; avec
`style = plat` leurs jetons sont **dérivés** :

* Fenêtre W claire (`uk_bright(W) ≥ 110`) : SURFACE = `ton(W, 236)`, CREUX = `ton(W, 123)`,
  LIGNE = `ton(W, 116)`, LIGNE_FORTE = `ton(W, 102)`, TEXTE1 = `encre(W)` (0x1B1F24 au lieu
  du noir pur), TEXTE2 = `mêler(W, TEXTE1, 170)`, TEXTE3 = `mêler(W, TEXTE1, 120)`,
  ACCENT_CLAIR = `mêler(SURFACE, A, 40)`, ACCENT_FONCE = `ton(A, 104)`, TITRE_ACTIF = `active`,
  TITRE_INACTIF = `inactive`.
* Fenêtre W sombre : SURFACE = `ton(W, 67)`, CREUX = `ton(W, 133)`, ELEVE = `ton(W, 140)`,
  LIGNE = `ton(W, 145)`, LIGNE_FORTE = `ton(W, 155)`, TEXTE1 = `0xECEEF2`,
  TEXTE2 = `mêler(TEXTE1, W, 90)`, TEXTE3 = `mêler(TEXTE1, W, 150)`,
  ACCENT_CLAIR = `mêler(W, A, 70)`, ACCENT_VIF = `ton(A, 170)`.

`outline` : `none` → contour TT = 0xFF (absent), `dark` → valeurs ci-dessus, `black` →
contour `0x40000000` même en clair.

---

## 4. Typographie

| Usage | Police | Taille L/M (P) | Couleur |
|---|---|---|---|
| Texte d'interface, libellés, menus | DejaVu Sans | 13 (12) | TEXTE1 |
| Titres de fenêtre, nom de l'appli (barre de menus), heure | DejaVu Sans Bold | 13 (12) | TEXTE1 / TEXTE3 inactif |
| Secondaire (barre d'état, types, tailles, bulles) | DejaVu Sans | 12 (11) | TEXTE2 / TEXTE3 |
| En-têtes de colonnes, titre de tiroir secondaire | DejaVu Sans Bold | 11 | TEXTE3 |
| Titre de section dans une fenêtre | DejaVu Sans Bold | 15 (14) | TEXTE1 |
| Grand titre (bandeau de Bienvenue) | DejaVu Sans Bold | 22 (18) | blanc sur accent |
| Terminal, éditeur, chemins | DejaVu Sans Mono | 13 (12) | TEXTE1 |

Texte trop long : coupé au dernier caractère qui tient, suivi de « … » (U+2026).

---

## 5. Fond d'écran (`voronoy`)

### 5.1 Voronoï « facettes douces » (motif par défaut)

* **Cellules** : N = arrondi(largeur × hauteur / 28 000), au moins 8 (1024x768 → 28,
  800x600 → 17, 640x480 → 11). Graines tirées avec une distance minimale entre elles de
  0,62 × √(l × h / N) (rejet ; 200 essais par graine au plus) : des cellules régulières, plus
  grandes et plus calmes qu'aujourd'hui. Graines dans [−20, l + 20] × [−20, h + 20] pour que
  les bords ne soient pas coupés net.
* **Couleurs** : chaque cellule prend une des 5 couleurs de la palette du thème (tirage) :
  * Givre : `0x7FA6EC 0x5A8BE0 0xA9C2F0 0xC4CEDD 0x4B78CF` ;
  * Onyx Noir : `0x1C2433 0x22314A 0x18202C 0x2B4266 0x202838` ;
  * autre thème : `mêler(A, blanc, 80)`, `ton(A, 116)`, `mêler(A, blanc, 140)`,
    `mêler(W, A, 60)`, `ton(A, 100)` (A accent, W fenêtre ; sombre : les mêmes avec `ton(…, 60)`).
* **Relief** : par pixel, `ton(couleur, 136 − 30 × y / h)` (plus clair en haut, plus sombre en
  bas ; en sombre `128 − 24 × y / h`).
* **Liseré** : un pixel est sur une arête quand `(d2² − d1²) / (2 × |g2 − g1|) < 1` (d1, d2 :
  distances aux deux graines les plus proches g1, g2 — c'est la distance exacte à la
  médiatrice) ; il prend `ton(couleur, 150)` (clair) au lieu des bords sombres actuels.
* Coût : ~ l × h × N comparaisons, déjà le cas aujourd'hui ; dessin une fois au démarrage.

### 5.2 Autres motifs et configuration

`SD0:/etc/fond.ini` (nouveau) :

```
# Le fond d'écran (SD0:/sys/voronoy.elf)
motif    = voronoi        ; voronoi | degrade | uni
cellules = auto           ; auto | nombre
couleurs = auto           ; auto | 0xRRGGBB, 0xRRGGBB, ... (2 à 6)
relief   = 30             ; écart de ton haut / bas (0 = aucun)
lisere   = oui            ; oui | non
graine   = hasard         ; hasard | nombre (même fond à chaque démarrage)
```

`degrade` : dégradé diagonal entre les deux premières couleurs, tramé (bruit ±2 niveaux par
pixel, xorshift) pour éviter les bandes. `uni` : la première couleur.

---

## 6. Barre de menus (`menubar`)

Fenêtre `WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_RESERVE`, **plus `WF_ALPHA`**, hauteur 30/28/26.

* **Fond** : `plein` de `TJ_BARRE` (transparence 0x1A : le fond d'écran transparaît à peine),
  ligne de 1 px `TJ_LIGNE` (opaque) en bas.
* **Menu ARMPI**, à gauche :
  * logo : glyphe « ◆ » (U+25C6) DejaVu Sans 15 px, `TJ_ACCENT`, x = 12, ligne de base centrée ;
  * « ARMPI » DejaVu Sans Bold 13, TEXTE1, x = 31 ;
  * zone cliquable : x 6 → fin du mot + 8, toute la hauteur ;
  * survol : `boîte(6, 4, l, H − 8, 5)` en `TJ_CREUX` ; ouvert : même boîte en
    `TJ_ACCENT_CLAIR` (Givre) / `0x2C3038` (Onyx Noir), texte inchangé.
* **Nom de l'application active** : 10 px après la zone d'« ARMPI », Sans Bold 13, TEXTE1 ;
  si elle a publié des menus, ce nom devient un menu, suivi de ses menus (section 6.2).
* **Zone d'état**, de droite à gauche, marge droite 12 :
  1. **Heure** « 14:32 », Sans Bold 13, TEXTE1, alignée à droite ;
  2. **Date** « mer. 28 oct. », Sans 13, TEXTE2, 10 px avant l'heure — seulement si la largeur
     ≥ 1000 ;
  3. **Réseau**, 14 px avant (16 × 14 px), centré verticalement :
     * Wi-Fi : 4 barres de 3 px de large, 1 px d'écart, hauteurs 4/7/10/13, alignées en bas
       (bas = milieu + 6) ; barres allumées (TEXTE1) selon le signal, les autres TEXTE3 ;
     * filaire : 3 boîtes 6 × 5 rayon 1 (une en haut, deux en bas) reliées par des traits de
       1 px (barre horizontale 14 px, 3 pattes), TEXTE1 ;
     * pas de réseau : barres en TEXTE3 et petite croix « × » 9 px TEXTE2 en bas à droite.
     L'adresse (« 10.0.2.15 ») n'est plus affichée en permanence : clic sur l'icône → petit
     menu (section 7) : interface, adresse, état, « Réglages Wi-Fi… ».
  * Survol d'un élément de la zone d'état : `boîte` rayon 5, `TJ_CREUX`, 4 px de marge.
* **Clavier** : la touche Super seule (ou F10) ouvre le menu ARMPI ; flèches, Entrée, Échap.

### 6.1 Contenu du menu ARMPI

```
◆ À propos d'ARMPI…          (lance bienvenue)
──────────
[icône] Accessoires        ›  (sous-menu : applis de la catégorie, ordre alphabétique)
[icône] Création           ›
[icône] Internet           ›
[icône] Jeux               ›
[icône] Système            ›
──────────
Fenêtres ouvertes          ›  (sous-menu : titres ; un choix ramène la fenêtre)
──────────
Réglages…                     (désactivé tant que config n'existe pas, chantier 4.4)
⏻ Éteindre…                   (glyphe d'alimentation en TJ_DANGER)
```

L'icône d'une catégorie est celle de son application principale (`dock.ini`, section 8.6).
Les catégories viennent de `SD0:/etc/applist.ini` (8.6), comme le dock : une seule source.

### 6.2 Barre de menus globale (menus des applications, comme Onyx)

* Une application publie ses menus avec `ui_menu` (texte `M<titre>` / `I<id>\t<libellé>\t<raccourci>`
  / `-`, docs/ABI.md) ; Elegant les garde sur la fenêtre (`elegant/menu.inc`), un emplacement
  de 2 Ko par fenêtre.
* La barre lit le menu de la fenêtre active (`ui_menu_get`) toutes les 200 ms et à chaque
  `WEV_WINLIST` (changement de focus) ; elle ne refait ses titres que si le numéro de série
  change. Sans menu : comportement d'avant (le titre de la fenêtre active).
* Avec un menu : après ARMPI, le **nom de l'application** (titre de sa fenêtre, Sans Bold 13,
  220 px au plus) ouvre un menu « Quitter  Ctrl+Q » (envoie `WEV_CLOSE`) ; puis ses menus en
  Sans 13, 18 px d'un texte à l'autre, zone de clic de 8 px de chaque côté, mêmes états que le
  menu ARMPI (survol `TJ_CREUX`, ouvert `TJ_ACCENT_CLAIR`). Les titres qui ne tiennent pas
  (200 px avant le bord droit) ne sont pas montrés.
* Un clic ouvre le menu déroulant (section 7, `ui_popup_ex` drapeau 16 : raccourci en TEXTE3
  aligné à droite), largeur max(180, libellé + raccourci + 60), au plus 420. Un choix envoie
  `WEV_MENU` (id) au programme par `ui_menu_command`. Un clic sur un autre titre pendant
  qu'un menu est ouvert ouvre celui-là ; sur le même, le ferme.
* Les raccourcis restent dans l'application (elle traite Ctrl+touche elle-même) ; la barre ne
  fait que les afficher. Exemple : Bienvenue, menu « Fichier » → « Fermer  Ctrl+W ».

---

## 7. Menus déroulants (menu ARMPI, sous-menus, menus des applis, `ui_popup`)

* **Fenêtre** `WF_ALPHA` agrandie de 4 px de chaque côté pour l'ombre (3.2) : anneaux 0–2,
  contour (anneau 3, rayon 9), puis le corps en retrait de 4, **rayon 8**, `TJ_SURFACE`
  (Givre) / `TJ_CREUX` (Onyx Noir). Position : bord gauche aligné sur le titre − 4, haut =
  bas de la barre − 2.
* **Largeur** : max(200, plus long libellé + 76), au plus 320. **Marge** verticale intérieure 6.
* **Ligne** : hauteur 28/26/24.
  * icône facultative 16 × 16 à x = 11 (dans le corps), centrée ; libellé Sans 13 à x = 36
    (même sans icône, pour aligner) ; flèche de sous-menu « › » (U+203A) Sans 14 TEXTE3,
    alignée à droite à 14 px du bord ; raccourci éventuel en TEXTE3 aligné à droite.
  * **survol / sélection clavier** : `boîte(5, y, l − 10, h, 5)` en `TJ_ACCENT`, libellé,
    flèche et raccourci en blanc ; l'icône garde ses couleurs.
  * **désactivée** : libellé TEXTE3, pas de survol.
* **Séparateur** : 11 px de haut, trait 1 px `TJ_LIGNE` à y + 5, de x = 10 à l − 10.
* **En-tête de groupe** (facultatif) : Sans Bold 11 TEXTE3, hauteur 22, non cliquable.
* **Sous-menu** : s'ouvre à droite (ou à gauche s'il sort de l'écran), son premier élément
  aligné sur la ligne parente (haut du sous-menu = haut de la ligne − 10), chevauchement de
  6 px ; ouverture au survol après 150 ms, ou au clic (tactile), ou →.
* **Clavier** : ↑ ↓ (en boucle, sautent séparateurs et lignes désactivées), → / Entrée ouvre
  un sous-menu, ← / Échap remonte, Entrée exécute, une lettre va au libellé suivant qui
  commence par elle.

---

## 8. Dock (`dock`) — structure d'Onyx, rendu modernisé

### 8.1 Structure (identique à `Onyx/user/Apps/dock/main.cpp`)

De gauche à droite, les **emplacements** — d'abord les **tiroirs** (un par catégorie, ordre de
`dock.ini`), puis les **lanceurs**, puis la **corbeille** — répartis moitié à gauche, moitié à
droite du **pupitre central** (l'impair à gauche : `ngauche = (n + 1) / 2`).

* **Tiroir** : l'icône de l'application principale de la catégorie (un clic la lance ou
  ramène sa fenêtre) ; la **languette** en haut du lanceur (zone `STRIP_H`) ouvre le tiroir
  au-dessus.
* **Lanceur** : une application (clic : lancer ou ramener, `ui_raise`).
* **Corbeille** : clic → `fichiers SD0:/.corbeille` ; masquée si `corbeille = non` (8.6).
* **Pupitre** : à gauche verrou (haut) et engrenage (bas), au milieu les espaces de travail,
  à droite alimentation (haut) et presse-papiers (bas).
* **Point** sous un lanceur dont une application a une fenêtre ouverte (pour un tiroir : une
  de ses applis), et dans le tiroir à droite de l'application.
* Clic droit sur le dock : menu « Réglages du dock… » (désactivé avant config).

### 8.2 Disposition (algorithme d'Onyx, constantes de l'échelle)

```
pgRows = (nespaces <= 3) ? 1 : 2 ;  pgCols = ceil(nespaces / pgRows)
pgW    = 30 + pgCols*(SQW+5) - 5 + 6 + 30
fixe   = 24 + pgW + (ngauche ? 8 : 0) + (n > ngauche ? 8 : 0) ;  place = largeur - 16
si fixe + n*CW > place :            (dock trop large : tout rétrécit ensemble)
    CW   = max(CWmin, (place - fixe) / n)
    ICON = max(ICONmin, CW * ICON0 / CW0)
    DH   = max(DHmin, DH0 - (CW0 - CW) * (DH0 - DHmin) / (CW0 - CWmin))
x = 12 ; lanceurs de gauche (x += CW) ; pgX = x + 8 ; x = pgX + pgW + 8 ; lanceurs de droite
DW = x + 12 ;  DX = (largeur - DW) / 2 ;  DY = hauteur - DH - GAP
```

Exemples (5 tiroirs, 2 lanceurs, corbeille, 4 espaces) : 1024x768 → DW 659, DX 182, DY 676 ;
800x600 → DW 619, DX 90, DY 522 ; 640x480 → CW 50, DW 563, DX 38, DY 412.

Positions verticales dans le dock (origine = haut du dock) : icône à `y = DH − ICON − BAS`,
point à `y = DH − 9`, languette de 1 à `STRIP_H`, surlignage de survol de 19 à `DH − 7`.

**Fenêtre** : comme Onyx (`WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST | WF_ALPHA |
WF_RESERVE`), mais **4 px de marge à gauche et à droite et 30 px au-dessus** (`LABEL_H`, la
bulle) : x = DX − 4, largeur DW + 8, hauteur DH + GAP + 30. Tiroir ouvert : la fenêtre couvre
l'écran sous la barre, presque transparente (`0xFE000000`) pour attraper le clic ailleurs.
La zone réservée en bas est DH + GAP.

### 8.3 Le panneau

1. **Ombre basse** : `boîte(DX + 8, DY + 6, DW − 16, DH, rayon)` noir transparence `0xE6` —
   elle dépasse de 6 px dans le GAP.
2. **Anneaux** à 3, 2, 1 px autour du panneau : noir `0xF6`, `0xEE`, `0xE3` (3.2).
3. **Face** : **plate dans les deux thèmes** (demande de l'utilisateur, 28/10/2026 : le dock
   sombre aussi plat que le clair) — `boîte(DX, DY, DW, DH, rayon, ton(dock, 132),
   ton(dock, 124))`, un écart de 8 niveaux seulement, avec la transparence de `TJ_DOCK`
   (Givre 0x33 ≈ 80 % opaque, Onyx Noir 0x26).
4. **Bord** 1 px : `ton(dock, 92)` transparence 0x4D (clair) / `TJ_LIGNE` 0x4D (sombre).
5. **Reflet** : ligne de 1 px blanche transparence 0x4D à DY + 1, de DX + rayon à
   DX + DW − rayon — **en clair seulement** (en sombre, pas de reflet : rien qui bombe).
6. **Séparateurs** (remplacent les rainures d'Onyx) : 1 px, de DY + 14 à DY + DH − 14,
   `ton(dock, 100)` (clair) / `TJ_LIGNE` (sombre), transparence 0x66 ; entre deux lanceurs
   voisins (x + CW − 1) et de part et d'autre du pupitre (pgX − 5, pgX + pgW + 3).

### 8.4 Les emplacements

* **Icône** ICON × ICON (section 9), centrée sur cx = DX + x + CW/2.
* **Survol** : `boîte(x + 4, 19, CW − 8, DH − 26, 10)` blanc transparence 0xB3 (clair) /
  `0xD9FFFFFF` (sombre) ; plus la **bulle** (8.5).
* **Appui** : même boîte en noir transparence 0xE0 ; l'icône descend de 1 px.
* **Dépôt** (glisser un fichier, plus tard) : survol + contour arrondi `TJ_ACCENT`.
* **Point d'appli ouverte** : `boîte(cx − 2, DY + DH − 9, 5, 5, 2)` en `TJ_ACCENT_VIF`.
* **Languette d'un tiroir** (zone cliquable : toute la largeur du lanceur, y < STRIP_H + 2) :
  * dessin : onglet de 36 × 12 (CW < 60 : `(CW − 8)/2 × 2` de large), centré sur cx, de y = 1 à
    13, **coins bas** arrondis rayon 6 (une `boîte` commencée 6 px au-dessus du bord haut et
    rognée par la fenêtre — ou un indicateur « coins du bas seulement » ajouté à `ui_rbox`,
    comme `UK_BL | UK_BR` d'Onyx) ;
  * repos : `ton(dock, 112)` (clair) / `TJ_ELEVE` (sombre) ; survol : `ton(dock, 100)` /
    `ton(TJ_ELEVE, 150)` ; ouvert : `TJ_ACCENT` ;
  * chevron 7 × 4 en pixels : pour la rangée j = 0..3, deux rectangles 2 × 1 en
    (cx − j − 2) et (cx + j) ; vers le haut (fermé) rangées de cy − 2 à cy + 1, vers le bas
    (ouvert) de cy + 1 à cy − 2 ; couleur TEXTE2, blanc quand ouvert.
* **Corbeille** : glyphe d'Onyx (`trash_glyph`) avec S(v) = v × ICON / 40 : couvercle
  `boîte(cx−14, 8, 28, 4, 2)`, poignée `boîte(cx−5, 4, 10, 5, 2)`, cuve `boîte(cx−11, 14, 22,
  24, 4)` en TEXTE2, trois nervures 2 × 16 couleur de la face ; pleine : feuille blanche
  `boîte(cx−8, 2, 16, 9, 2)` dépassant derrière le couvercle.

### 8.5 Bulle (nom au survol)

Au-dessus de l'emplacement survolé, dans la bande `LABEL_H` : `boîte` rayon 6, hauteur 22,
largeur texte + 20, fond `0x141B1F24` (presque noir, dans les deux thèmes), texte Sans 12 blanc
centré ; bas de la bulle à DY − 6 ; maintenue dans l'écran. Textes : libellé de l'appli ;
languette : « Accessoires — ouvrir le tiroir » ; carré : « Espace 2 » ; « Verrouiller »,
« Réglages », « Éteindre », « Presse-papiers », « Corbeille ».

### 8.6 Tiroir ouvert

* Boîte : `pw = colonnes × COLW + 12`, `ph = 44 + rangées × RH + 8` ; rangées max =
  `(DY − 12 − barre − 8 − 44) / RH` ; centré sur la languette, maintenu à 4 px des bords ;
  bas à DY − 12.
* Ombre : anneaux 3.2 autour ; corps `boîte(…, 12)` en `TJ_SURFACE` (Givre) / `TJ_CREUX`
  (Onyx Noir), transparence 0x08.
* **Pointe** vers la languette : triangle de 18 × 8 sous le corps, centré sur cx, rempli par
  rangées (comme Onyx), ses deux bords en pixels mêlés de `TJ_CONTOUR`.
* **En-tête** : catégorie en Sans Bold 13 TEXTE2 à x + 14, centré dans 36 px ; à droite
  « 3 applications » Sans 11 TEXTE3 ; trait `TJ_LIGNE` à y + 36, de x + 10 à x + pw − 10.
* **Ligne** (y + 42 + i × RH, x + 6, largeur COLW, hauteur RH − 2) : icône SI à x + 8 centrée,
  libellé Sans 13 à x + 46 (**gras pour l'application principale**), point 6 × 6 rayon 3 à
  x + COLW − 18 si ouverte. Survol / clavier : `boîte(…, 8)` en `TJ_ACCENT`, texte et point
  blancs. Ordre alphabétique des libellés.
* Clavier : flèches dans le tiroir, Entrée lance, Échap ferme ; clic ailleurs : ferme.

### 8.7 Pupitre central

* Fond : `boîte(pgX, 8, pgW, DH − 16, 10)` en `ton(dock, 112)` transparence 0x40 (clair) /
  `TJ_FACE` 0x40 (sombre) ; contour arrondi `ton(dock, 92)` / `TJ_LIGNE`, transparence 0x80.
* **Espaces de travail** : carrés SQW × SQH rayon 5, en 1 ou 2 rangées
  (`x = pgX + 30 + col × (SQW + 5)`, `y = (DH − 2 × SQH − 6)/2 + rangée × (SQH + 6)` ; une
  rangée : `(DH − SQH)/2`).
  * courant : fond `mêler(ACCENT, blanc, 60)` (clair) / `TJ_ACCENT_CLAIR` (sombre), contour
    `TJ_ACCENT`, second contour extérieur `TJ_ACCENT` transparence 0x8C ; ses fenêtres en
    miniature (rectangles : face `ton(FACE, 120)`, barre de 2 px TEXTE3 ; celle qui a le focus
    en SURFACE avec barre ACCENT) ;
  * autres : fond `ton(dock, 128)` / `TJ_CREUX`, contour `ton(dock, 96)` / `TJ_LIGNE`, numéro
    Sans 11 TEXTE3 centré s'il est vide ; survol : fond `ton(…, 150)`.
  * Phase 1 (le serveur n'a pas encore d'espaces de travail) : **un seul carré**, l'écran
    courant avec ses fenêtres en miniature (pgW = 100) ; les lignes `espace =` sont déjà lues.
* **Boutons système** (cible 24 × 25, survol `boîte` rayon 6 blanc transparence 0xB0) :
  centres en (pgX + 15, DH × 27/80) verrou, (pgX + 15, DH × 53/80) engrenage,
  (pgX + pgW − 17, DH × 27/80) alimentation, (pgX + pgW − 17, DH × 53/80) presse-papiers.
  * verrou : anse `boîte(cx−4, cy−8, 8, 10, 4)` TEXTE2 évidée par `boîte(cx−2, cy−6, 4, 8, 2)`
    couleur du pupitre ; corps `boîte(cx−6, cy−2, 12, 9, 2)` TEXTE2 ;
  * engrenage : glyphe « ⚙ » (U+2699, DejaVu Sans) 17 px TEXTE2 ;
  * alimentation : disque 14 `TJ_DANGER`, disque 10 couleur du pupitre, encoche
    `plein(cx−3, cy−8, 6, 6)` couleur du pupitre, barre `plein(cx−1, cy−8, 2, 8)` DANGER ;
  * presse-papiers : planche `boîte(cx−6, cy−6, 12, 14, 2)` TEXTE2, feuille
    `boîte(cx−4, cy−3, 8, 9, 1)` couleur du pupitre, pince `boîte(cx−3, cy−8, 6, 4, 1)`,
    deux lignes 6 × 1 ;
  * application absente (lock, clipboard n'existent pas encore) : glyphe en TEXTE3
    transparence 0x80, pas de survol ni de clic. Alimentation → `SD0:/sys/shutdown`, le dialogue
    Redémarrer, Éteindre, Annuler.

### 8.8 Configuration : `SD0:/etc/applist.ini` et `SD0:/etc/dock.ini`

Les programmes d'ARMPI sont des `.elf` sans `app.txt` : leurs **catégories et libellés** vont
dans un fichier commun (lu par le dock, la barre de menus, plus tard config) :

```
# SD0:/etc/applist.ini — nom = catégorie, libellé [, couleur de l'icône]
bienvenue    = Système, Bienvenue
calculatrice = Accessoires, Calculatrice
cibles       = Jeux, Cibles
dessin       = Création, Dessin
editeur      = Accessoires, Éditeur
fichiers     = Système, Fichiers
horloge      = Accessoires, Horloge
magneto      = Création, Magnéto
taskman      = Système, Gestionnaire de tâches
navigateur   = Internet, Navigateur
piano        = Création, Piano
terminal     = Système, Terminal
```

Une application de `SD0:/apps` absente du fichier : catégorie « Autres », libellé = nom avec
majuscule initiale. UTF-8 tel quel (le rastériseur affiche les accents).

```
# SD0:/etc/dock.ini — le dock (SD0:/sys/dock.elf)
# tiroir  = catégorie, application principale   (ordre des tiroirs)
# lanceur = application                          (après les tiroirs)
# desk    = nom                                  (espaces de travail, 1 à 6)
# hidden  = catégorie                            (sans tiroir)
tiroir    = Accessoires, editeur
tiroir    = Création, dessin
tiroir    = Internet, navigateur
tiroir    = Jeux, cibles
tiroir    = Système, taskman
lanceur   = terminal
lanceur   = fichiers
desk      = 1
desk      = 2
desk      = 3
desk      = 4
corbeille = oui                ; oui | non
taille    = auto               ; auto | grande | moyenne | petite (échelle du dock seul)
```

`drawer` et `launcher` (clés d'Onyx, celles qu'écrit l'applet) valent `tiroir` et `lanceur`.
Lecture commune au dock et à l'applet : `src/inc/dockconf.inc`, comme `dock_layout_load` d'Onyx.
Le dock a un tiroir pour chaque catégorie des applications, sauf `hidden` et Shell, Settings,
Emulators (et Réglages, Paramètres, Émulateurs) : une catégorie que le fichier ne nomme pas vient
à la fin, par nom ; une catégorie sans application n'a pas de tiroir mais garde sa place. Absence
du fichier : les catégories par nom, terminal et files, 4 espaces.

---

## 9. Icônes des applications

`ui_icone(nom, x, y, taille)` cherche d'abord une **image pré-rendue** : `SD0:/apps/<nom>.app/icon.rgba`
(elle part avec le paquet de l'app), puis `SD0:/res/icons/<nom>.rgba` (applets de Réglages, corbeille, paquets
sans app). Les images viennent de `tools/icons` (`python tools/icons/build.py` : dessins dans `art.py`, rendus
commités dans `tools/icons/rendered`, copiés par build.ps1, qui n'a pas besoin de Python) : plusieurs tailles nettes dans
un fichier, alpha prémultiplié, format en tête de `build.py`. Le kit les lit à la demande et les garde en
mémoire par programme (`uikit_icons.inc`). Une app nouvelle : ajouter son dessin dans `art.py`, relancer `build.py`.

**Repli, pour une app tierce sans image** : le dessin procédural ci-dessous, sur une **grille
de 40** mise à l'échelle par `u = taille / 40` (coordonnées arrondies au pixel ; sous 20 px, les
détails de 1 px sont omis). Utilisées à 40 (dock), 28 (tiroirs), 16 (menus, listes).

**Tuile commune** : `boîte(0, 0, T, T, arrondi(T × 0,25), ton(c, 140), ton(c, 116))`, reflet
`plein(r, 1, T − 2r, 1)` blanc transparence 0xA6 ; tuiles sombres (piano, terminal) : contour
arrondi `0x3A404A`. Puis le **symbole**, en blanc sauf indication (coordonnées sur 40) :

| Appli | c (tuile) | Symbole |
|---|---|---|
| bienvenue | `0x4F5BD5` indigo | losange ARMPI : glyphe « ◆ » Sans 26 centré (ou carré 18 × 18 tourné), point central 6 × 6 couleur c |
| calculatrice | `0x5B6472` ardoise | écran `boîte(9,6,22,8,2)` `0xDCEFE2` ; 3 × 3 touches `boîte(9/17/25, 17/23/29, 6, 5, 1)`, la dernière `0xF59E0B` |
| cibles | `0xE5484D` rouge | disques concentriques centrés (20,20) : 28 blanc, 20 c, 12 blanc, 5 c (`boîte` rayon = côté/2) |
| dessin | `0xF59E0B` ambre | palette `boîte(6,10,28,20,10)` ; godets 5 × 5 ronds en (13,16) `0xE5484D`, (20,14) `0x2F74E0`, (27,16) `0x30A46C`, (13,23) `0x6E56CF` ; trou du pouce 6 × 6 en (25,24) couleur c |
| editeur | `0x6E56CF` violet | page `boîte(11,7,18,26,3)` ; lignes 2 px `mêler(c, blanc, 150)` en y 12/16/20 (12 de long) et 24 (8 de long), x = 14 |
| fichiers | `0x1BA1B5` sarcelle | dossier : onglet `boîte(8,11,11,6,2)` `0xD7F0F4`, corps `boîte(8,14,24,17,3)` |
| horloge | `0xE8833A` orange | cadran disque 24 ; aiguilles `plein(19,12,2,9)` et `plein(19,19,7,2)` `0x1B1F24` ; centre disque 4 c |
| magneto | `0xD6336C` framboise | onde : 7 barres de 2 px de large, rayon 1, tous les 4 px dès x = 7, hauteurs 6/12/20/14/18/8/4 centrées sur y = 20 |
| moniteur | `0x30A46C` vert | écran `boîte(6,9,28,20,3)` `0x0F2E1F` ; tracé de pouls en traits de 2 px `0x9BF0BF` : plat (9→16, y 19), pic haut (15–19, y 12), creux (21, y 23), bosse (23–26, y 17), plat (25→32, y 19) ; pied `plein(17,30,6,3)` + `boîte(13,32,14,2,1)` |
| navigateur | `0x1E88E5` bleu ciel | globe disque 26 ; méridien : `boîte(14,7,12,26,6)` c puis `boîte(16,9,8,22,4)` blanc ; équateur `plein(7,19,26,2)` c ; axe `plein(19,7,2,26)` c |
| piano | `0x2B2F36` noir laqué | 4 touches blanches `boîte(7 + 7i, 10, 6, 20, 1)` ; 3 noires `plein(11 + 7i, 10, 4, 12)` `0x111317` |
| terminal | `0x1B1F24` | « >_ » DejaVu Sans Mono 16, `0x7CE38B`, x = 9, ligne de base 26 |
| (inconnue) | couleur tirée du nom (somme des octets mod 8 parmi les 8 couleurs ci-dessus) | initiale en Sans Bold 20 blanche centrée |

La 3ᵉ valeur facultative d'`applist.ini` remplace c. Les icônes 16 px des listes de Fichiers :
dossier `boîte(0,2,7,3,1)` `ton(0xF2B544, 110)` + `boîte(0,4,16,11,2)` `0xF2B544` ; fichier :
contour arrondi 12 × 16 TEXTE3, intérieur SURFACE.

---

## 10. Fenêtres (style `plat`)

Géométrie inchangée (`FRAME_BORDER` 4, `FRAME_RADIUS` 8, `FRAME_TITLE_H` 28 ; 26 en P) : le
**bord de 4 px devient l'ombre**.

* **Bord** (dans le tampon du cadre, transparence par pixel) : anneau 0 (rayon 12), 1 (11),
  2 (10) noirs transparents (3.2), anneau 3 (rayon 9) = `TJ_CONTOUR` ; la table
  d'anticrénelage `uk_corner` s'applique au coin du contenu (rayon 8, retrait 4). Actif /
  inactif : deux copies du cadre, comme aujourd'hui.
* **Barre de titre** (retrait 4, hauteur 28) : `plein` `TJ_TITRE_ACTIF` / `TJ_TITRE_INACTIF` ;
  en sombre, reflet 1 px blanc transparence 0xF0 en haut ; trait 1 px `TJ_LIGNE` sur la
  dernière ligne de la barre. Plus de dégradé ni de biseau.
* **Titre** : Sans Bold 13 centré (ligne de base y + 19) ; actif TEXTE1, inactif TEXTE3 ;
  coupé avec « … » pour laisser 8 px avant les boutons.
* **Boutons** (24 × 20, rayon 5, y = 4 dans la barre ; de droite à gauche : fermer à 6 px du
  bord, agrandir − 26, réduire − 52 ; menu à gauche à x = 6). `FRAME_BTN_W` 24, `_H` 20,
  `_Y` 4, `_EDGE` 6, `_STEP` 26.
  * glyphes : fermer « × » (U+00D7) Sans 16 centré ; agrandir contour 10 × 10 rayon 1 ;
    réduire `plein` 10 × 2 au centre ; menu trois traits 10 × 2 espacés de 4 ;
  * repos : glyphe TEXTE2 (inactif TEXTE3) ; survol : fond `boîte` `TJ_CREUX` (Givre) /
    `TJ_ELEVE` (Onyx Noir) ; appui : `ton(fond survol, 112)` ; **fermer survolé : fond
    `TJ_DANGER`, glyphe blanc** ; appui `ton(DANGER, 108)` ; désactivé (agrandir, menu tant
    qu'ils ne servent pas) : TEXTE3 transparence 0x80, sans survol.
* Style `milk` modernisé (optionnel) : perles de 12 px (`boîte` rayon 6) fermer `0xFF5F57`,
  réduire `0xFEBC2E`, agrandir `0x28C840`, contour `ton(c, 100)` ; survol de la zone des trois :
  glyphes « × – + » Sans 9 `ton(c, 60)` ; inactif : perles `0xD5D7DB` (`0x3A3E46` en sombre).
* **Contenu** : fond `TJ_FACE`. Une fenêtre inactive ne change pas son contenu (seul le cadre
  s'éteint).
* **Focus clavier** dans une fenêtre : voir 11.8.
* **À distance (rdpd, Onyx Remote)** : chaque fenêtre est montrée seule sur le PC ; Elegant
  donne son contenu et ses deux copies du cadre telles que dessinées (`WS_OP_WIN_READ`, ABI.md),
  le client les compose lui-même. Les coins arrondis et l'ombre passent par l'octet haut
  (transparence, 0 opaque) ; il n'y a donc rien à dessiner de plus pour l'accès à distance. Les
  fenêtres du fond (`WF_BACK`, le fond d'écran) ne sont pas montrées une à une : elles forment
  l'image du bureau (pseudo-fenêtre `0xFFFFFFFF`, à la demande du client).

---

## 11. Contrôles des applications (UIKit)

Hauteur de contrôle `Hc` = 30/28/26 selon l'échelle ; rayon 6 ; texte Sans 13 centré.

### 11.1 Boutons

| État | Standard | Principal (défaut, Entrée) | Fantôme (barres d'outils) |
|---|---|---|---|
| Repos | contour arrondi LIGNE_FORTE ; intérieur `boîte` ELEVE : Givre `0xFFFFFF → 0xF4F5F7`, Onyx `ton(ELEVE,136) → ELEVE` ; texte TEXTE1 | contour ACCENT_FONCE ; intérieur `ton(ACCENT,140) → ACCENT` ; texte blanc | rien ; texte TEXTE1 |
| Survol | bas du dégradé `ton(ELEVE, 120)` | intérieur `ton(ACCENT,150) → ton(ACCENT,136)` | `boîte` CREUX |
| Appui | intérieur uni `ton(ELEVE, 112)` | intérieur uni ACCENT_FONCE | `boîte` `ton(CREUX, 116)` |
| Désactivé | contour LIGNE, intérieur FACE, texte TEXTE3 | intérieur `mêler(FACE, ACCENT, 90)`, texte blanc transparence 0x60 | texte TEXTE3 |

Largeur : texte + 2 × 14 (au moins 80 pour un bouton de dialogue) ; boutons de dialogue en bas
à droite, principal le plus à droite, 8 px entre eux.

### 11.2 Champs de texte

Contour arrondi LIGNE_FORTE, intérieur SURFACE, texte à x + 10, indication (placeholder)
TEXTE3. Curseur : `plein` 1 × 16 `TJ_ACCENT`, clignotement 530 ms. Sélection : fond
ACCENT_CLAIR (focus) ou CREUX. **Focus** : contour ACCENT + anneau de focus (11.8).
Désactivé : intérieur FACE, texte TEXTE3.

### 11.3 Listes

* Zone : `plein` SURFACE pleine largeur, traits LIGNE dessus et dessous (pas de cadre).
* En-têtes de colonnes (facultatifs) : hauteur 24, Sans Bold 11 TEXTE3, trait LIGNE dessous.
* Ligne : hauteur 28/26/24 ; icône 16 à x = marge + 2 ; texte Sans 13 à marge + 26 ;
  colonnes secondaires Sans 12 TEXTE3 (tailles alignées à droite).
* Survol : `boîte(6, y, l − 24, h, 5)` CREUX (Givre) / FACE (Onyx). Sélection sans focus :
  même boîte ACCENT_CLAIR, texte TEXTE1. Sélection avec focus : ACCENT, texte et icône
  inchangée, texte blanc.

### 11.4 Onglets (contrôle segmenté)

Rail `boîte(x, y, Σ + 4, Hc, 8)` CREUX (Givre) / SURFACE (Onyx) ; chaque segment = texte Bold
13 + 24. Segment actif : `boîte(…, y + 2, l, Hc − 4, 6)` SURFACE (Onyx : ELEVE) posée sur une
`boîte` de 1 px plus haute en LIGNE (ombre d'un pixel en bas), texte TEXTE1 gras ; inactifs :
texte TEXTE2, survol TEXTE1. Onglets de page (au-dessus d'un contenu) : même rendu, centrés.

### 11.5 Barres de défilement

Superposées au contenu, sans flèches. Rail invisible (survol : `boîte` CREUX transparence 0x80).
Pouce `boîte(…, largeur − 2, longueur, rayon (largeur − 2)/2)` à 2 px du bord : LIGNE_FORTE
(Onyx `0x4A505C`) ; survol `ton(…, 100)` ; appui `ton(…, 84)`. Longueur ≥ 24. Largeur 8 (L),
10 (M, P, tactile). Visibles dès que le contenu dépasse.

### 11.6 Cases, boutons radio, glissière

* Case 18 × 18 rayon 4 : contour arrondi LIGNE_FORTE, intérieur SURFACE ; cochée : `boîte`
  ACCENT et « ✓ » (U+2713) Sans Bold 13 blanc centré. Libellé à + 26.
* Radio : disque 18 (contour LIGNE_FORTE) ; choisi : disque ACCENT, point blanc 6.
* Glissière : rail 4 px rayon 2 LIGNE, partie remplie ACCENT ; bouton disque 18 blanc, contour
  LIGNE_FORTE (focus : anneau).

### 11.7 Barres d'outils et d'état

* Barre d'outils : hauteur Hc + 16, fond FACE, boutons fantômes à 2 px d'écart, trait LIGNE
  dessous (ou début d'une liste).
* Barre d'état / de dialogue : hauteur 24 (texte seul) ou 44 (avec boutons) ; trait LIGNE
  dessus ; texte Sans 12 TEXTE2 à la marge.

### 11.8 Focus clavier

Le contrôle qui a le focus reçoit un **anneau** : `boîte(x − 3, y − 3, l + 6, h + 6, r + 3)` en
`mêler(FACE, ACCENT, 90)`, puis `boîte(x − 1, y − 1, l + 2, h + 2, r + 1)` FACE, puis le
contrôle (avec contour ACCENT pour un champ). Tab / Maj+Tab parcourt les contrôles dans l'ordre
de création ; Espace actionne un bouton, Entrée le bouton principal, Échap le bouton
« Annuler » / « Fermer ». Dans un menu ou un tiroir, le focus est le surlignage d'accent.

---

## 12. Adaptations 800x600 (PocketTerm) et 640x480

**800x600 — échelle M (le vrai matériel).** L'écran de 3,5″ a une densité très forte
(~285 px/pouce) : on **ne réduit pas les textes** (13 px reste le minimum lisible) ni les icônes
du dock (40) ; on gagne de la place sur les **espacements** :

* barre de menus 28, dock 72 de haut + GAP 6 (zone de travail : y 28 → 522, soit 494 px au
  lieu de 478 avec les tailles d'Onyx : barre 30, dock 80, GAP 12) ; pas de date dans la barre ;
* fenêtres : marges 12, lignes de liste 26 ; **une fenêtre ouverte se place agrandie** si sa
  taille demandée dépasse 90 % de la zone de travail ;
* tactile : un appui long (500 ms) = clic droit ; languette du tiroir : la zone cliquable couvre
  aussi toute la largeur du lanceur sur 16 px ; sous-menus ouverts au toucher (pas au survol) ;
  barres de défilement 10 px, glisser dans une liste fait défiler ;
* bulles du dock affichées seulement à la souris (pas au toucher, le doigt les cache).

**640x480 — échelle P.** Police 12 (titres 12 gras), barre 26, titres de fenêtres 26, contrôles
26, lignes 24, marges 8, dock 64/50/36 (GAP 4) puis rétrécissement jusqu'à 52/34/22 ; un seul
rang d'espaces de travail si ≤ 2, et pas plus de 4 espaces affichés ; tiroirs : COLW 168,
RH 34, icônes 24 ; menus : largeur min 180. Fond : 11 cellules.

**1024x768 — échelle L** : valeurs d'Onyx (dock 80/60/40, barre 30), date dans la barre.

---

## 13. Récapitulatif des fichiers de configuration

| Fichier | Nouveau ? | Rôle |
|---|---|---|
| `SD0:/etc/theme.txt` | existant | clés d'Onyx + `style = plat`, thèmes Givre / Onyx Noir, `button`, `field`, `echelle` |
| `SD0:/etc/fond.ini` | nouveau | motif du fond d'écran (5.2) |
| `SD0:/etc/applist.ini` | nouveau | catégorie, libellé, couleur d'icône de chaque appli (8.8) |
| `SD0:/etc/dock.ini` | nouveau | tiroirs, lanceurs, espaces, corbeille, taille (8.8) |

---

## 14. Changements à implémenter (du plus visible au moins visible)

1. **Jetons et thèmes** (`uikit_theme.inc`) : table `uk_jetons` (TJ_*), `style = plat`, thèmes
   nommés Givre et Onyx Noir, clés `button`, `field`, `echelle`, dérivation 3.3 ;
   `theme.txt` de la carte passé en Givre. (Prérequis de tout le reste.)
2. **Fond d'écran** (`voronoy.asm`) : graines espacées et moins nombreuses, palette du thème,
   relief haut/bas, liseré clair ; `SD0:/etc/fond.ini`.
3. **Dock** (`dock.asm`) : structure d'Onyx — `applist.ini` + `dock.ini`, tiroirs à languette,
   lanceurs, corbeille, pupitre (1 espace en phase 1, boutons système grisés si absents),
   points, bulle, rétrécissement selon la largeur ; panneau translucide avec ombre.
4. **Icônes procédurales** (`ui_icone` dans UIKit) pour les 12 applis + repli par initiale ;
   utilisées par le dock, les tiroirs, les menus.
5. **Cadres de fenêtres** (`uikit_frame.inc`) : style plat, bord de 4 px en anneaux d'ombre,
   barre de titre unie + trait, nouveaux boutons 24 × 20 (fermer rouge au survol),
   `FRAME_BTN_*` mis à jour dans `armpi.inc`.
6. **Barre de menus** (`menubar.asm`) : `WF_ALPHA`, logo ◆, états du titre, icône réseau
   (barres / filaire) au lieu de l'adresse, date en L, heure en gras.
7. **Menus déroulants** (`ui_popup`) : ombre, rayon 8, lignes 28/26/24, icônes 16, survol
   d'accent arrondi, séparateurs, sous-menus ; menu ARMPI par catégories (6.1).
8. **Tiroir du dock** : panneau à pointe, en-tête, lignes avec icônes 28, clavier.
9. **Contrôles** (`uikit.inc` → jetons) : boutons standard / principal / fantôme, champs,
   listes, onglets segmentés, barres de défilement superposées, cases, glissière ; puis
   reprise de Bienvenue et Fichiers (barre d'outils fantôme, barre de dialogue).
10. **Échelles L/M/P** (`uk_echelle`) appliquées partout ; règles tactiles du PocketTerm
    (appui long, sous-menus au toucher).
11. **Focus clavier** : anneau de focus, Tab / Maj+Tab, Super ou F10 pour le menu ARMPI,
    navigation au clavier dans menus et tiroirs.
12. **Finitions** : bulles du dock, survols de la zone d'état, menu réseau, style `milk`
    modernisé, motifs `degrade` / `uni` du fond.

## 15. Recomposition et présentation (serveur Elegant)

Le serveur ne dessine pas les fenêtres : il les **compose** dans un tampon arrière (taille de
l'écran, mémoire cachée), puis **présente** les zones changées au framebuffer.
Sources : `src/sys/elegant/core.inc` (`dirty`, `compose`, `compose_clip`) et
`src/sys/elegant/present.inc`.

**Zones sales** — une liste de 16 rectangles (`dirty_list`), pas une seule union : l'horloge
de la barre de menus et le curseur en bas de l'écran ne font plus recomposer tout l'écran.
Une zone nouvelle est fusionnée avec une zone de la liste quand l'aire de leur union ne dépasse
pas la somme de leurs aires + 4 096 pixels (zones qui se chevauchent, se touchent ou sont
proches) ; l'union est alors réessayée contre les autres. Liste pleine : repli sur l'union de
tout (compté dans F11, « listes de zones pleines »).

**Recomposition** — `compose` prend la liste, redessine chaque zone (clip = la zone : fond,
fenêtres couche par couche, barres et menus, curseur s'il la touche ; une entrée 100 de
l'historique F11 par zone), puis présente toutes les zones ensemble. vncd et `screenshot`
reçoivent l'**union** des zones de chaque recomposition (une génération de `snap_record`) ;
rdpd lit les fenêtres elles-mêmes (générations par fenêtre) : rien ne change pour eux.

**Présentation par DMA 2D** (comme Onyx : `CCompositorTask::Present`, option `dispdma` ; Circle
`CDMAChannel::SetupMemCopy2D`) :
* un canal **complet** du BCM2711 (le plus haut des canaux 0-6 que le firmware laisse à l'ARM,
  `GET_DMA_CHANNELS` : **6 sur le Pi 4**, comme le framebuffer de Circle). Le son du noyau prend
  les canaux « lite » (7 et plus) ; sd, usbd, genet ont leur propre DMA ;
* chaque zone est lue **en place** dans le tampon arrière (pas source = largeur de l'écran × 4)
  et écrite dans le framebuffer (pas = `fb_pitch`) ; une chaîne de blocs de contrôle (un par
  zone) est lancée d'un coup, et Elegant repart (entrées, requêtes) pendant le transfert ;
* le tampon arrière vient de `dma_alloc` avec `DMA_CACHED` (contigu, caché : le dessin reste
  rapide) ; chaque ligne d'une zone est nettoyée (`DC CVAC`, permis en EL0 par
  `SCTLR_EL1.UCI`) avant le lancement ; les blocs de contrôle sont dans une page non cachée ;
* avant de redessiner, `compose` attend la fin du transfert précédent (`present_wait` : le
  canal lu entre deux `SYS_YIELD`, sans interruption, comme Onyx) ; rien d'autre n'écrit dans
  le tampon arrière ;
* sûreté : un essai au démarrage (2 lignes copiées d'une zone à une autre, largeur 128 bits)
  doit réussir ; en marche, une erreur du canal ou un transfert sans fin après 100 ms remettent
  le canal à zéro et font revenir **définitivement** à la copie par le processeur (tout l'écran
  recomposé). Le journal dit le chemin pris : « elegant : copie vers l'écran par DMA 2D
  asynchrone, canal 6 » ou « … par le processeur (raison) » ; F11 donne le chemin et le nombre
  de transferts lancés.

Options (`SD0:/etc/options.txt`) : `no_dispdma` — copie par le processeur ; `dispdma_qemu` —
sous QEMU, le DMA en 32 bits. QEMU émule le DMA 2D mais pas la largeur 128 bits (erreur du
canal) : l'essai échoue, d'où la copie processeur par défaut sous QEMU ; en 32 bits il copie mot
à mot (≈ 100 ms par écran 1024×768 contre ≈ 11 ms par le processeur, `bench`) : bon pour
vérifier la chaîne de blocs, pas pour mesurer.

Écarts à Onyx : la présentation est dans le serveur en espace utilisateur (pas un thread du
noyau) ; plusieurs zones par recomposition, en une chaîne de blocs (Onyx : un rectangle par
présentation) ; repli automatique sur la copie processeur si le DMA ne répond pas ; l'essai
mesure aussi le nombre de lignes copiées pour `YLENGTH` (le BCM2711 en copie YLENGTH + 1).

---

## 16. Extensions du protocole pour les widgets de UIKit (`src/inc/ws_ext.inc`)

Opérations ajoutées à Elegant pour UIKit (numéros après ceux de `armpi.inc`, à y fusionner) :

| Op | Rôle |
|---|---|
| `WS_OP_CLIP_SET` (18) | presse-papiers du système : a décalage, b longueur totale (≤ 64 Kio), +40 longueur du morceau (≤ 464), données : le morceau ; décalage 0 = nouveau contenu (génération + 1) |
| `WS_OP_CLIP_GET` (19) | a décalage → longueur du morceau ; b longueur totale, c génération ; données : le morceau |
| `WS_OP_MAXIMIZE` (27) | a id → 0 : comme le bouton agrandir ; `WEV_RESIZE` suit (`E_INVAL` sans `WF_RESIZABLE`) |
| `WS_OP_CURSOR_GET` (28) | → forme `UC_*` dessinée (`rdpd`, pour le message CURSOR) |
| `WS_OP_DESK` (29) | a espace (-1 : garder), b nombre (0 : garder ; 1 à 8) → courant \| nombre << 8 \| compteur << 16. Une fenêtre normale s'ouvre sur l'espace courant et n'est montrée que là ; `WF_BACK` et `WF_TOP` sont sur tous ; `WS_OP_RAISE` d'une fenêtre d'un autre espace y va ; Ctrl+Alt+Gauche / Droite : l'espace voisin |
| `WS_OP_SCREEN_SET` (30) | a largeur, b hauteur, c attente de confirmation en ms (0 : définitif) → 0 ; `E_BUSY` une attente en cours, `E_INVAL`, `E_IO` (16.1) |
| `WS_OP_SCREEN_KEEP` (31) | a 1 garder, 0 revenir tout de suite → 0 |
| `WS_OP_SCREEN_MODES` (32) | → nombre ; a, b taille native (0 : inconnue), c, d taille actuelle ; données : entrées `WSM_*` (largeur, hauteur, `WSMF_EDID`, `WSMF_NATIVE`) |
| `WS_OP_WIN_PLACE` (33) | a id d'une fenêtre du programme, b x, c y → 0 : toute couche (le dock se recentre) |
| `WEV_SCREEN` (12) | +8 largeur, +12 hauteur : à chaque fenêtre après un changement de résolution |
| Rôle `WF_MODAL` (1024) | dialogue modal : tant qu'il est là, les autres fenêtres de son programme (même pid) sont bloquées. Un clic sur l'une d'elles, contenu ou cadre, la passe devant, passe le dialogue par-dessus et s'arrête là ; `WS_OP_RAISE` (le dock) amène le dialogue. Pas de bouton réduire. Le plus en avant des dialogues du programme est celui qui répond (`win_blocker`) |
| `WS_OP_RESIZE` (21) | a id, b largeur, c hauteur → 0 ; a nouveau tampon partagé, b / c décalages des deux cadres (comme `CREATE`) ; l'ancien tampon est rendu. Rôle `WF_RESIZABLE` (512) : Elegant envoie `WEV_RESIZE` (8 ; +8 largeur, +12 hauteur) quand on relâche le bord droit / bas (ou le coin) glissé, ou au clic du bouton agrandir (zone libre de l'écran ; un second clic rend la place et la taille d'avant, gardées dans `win_restore`). Pas de redimensionnement « en direct » : la taille change au relâché |
| `WS_OP_CURSOR` (22) | a id, b forme (`UC_ARROW`, `UC_TEXT`, `UC_HAND`, `UC_HRESIZE`, `UC_VRESIZE`, `UC_WAIT`, `UC_CROSS`) : la forme du curseur sur le contenu de cette fenêtre (dessins 12 × 19 avec point chaud dans `uikit_ops.inc`) ; Elegant prend `UC_DRAG` pendant un glisser et ↔ sur le bord d'une fenêtre redimensionnable |
| `WS_OP_DRAG` (23) | a genre (`UDT_TEXT`, `UDT_FILES`), +40 octets (≤ 464), données : pendant un appui tenu dans son contenu, le programme commence un glisser ; au relâché, la fenêtre sous le pointeur reçoit `WEV_DROP` (9 ; +8 x, +12 y dans son contenu, +16 genre, +20 octets) |
| `WS_OP_DROP_GET` (24) | → octets ; a genre, b octets ; données : le dernier glisser déposé |
| `WS_OP_WIN_POS` (20) | a id (une fenêtre du programme) → a x, b y du contenu à l'écran, c largeur, d hauteur (menu d'une liste déroulante sous son widget) |

**Où vit le presse-papiers** : dans Elegant (`elegant/uikit_ops.inc`, tampon de 64 Kio pris dans son
tas au premier envoi). Tous les programmes graphiques lui parlent déjà, il vit autant que la
session, et le noyau n'a plus à garder des données des programmes (micro-noyau) : les appels
67-68 (`clip_set`, `clip_get`) du noyau pourront être retirés. Pas de nouveau service à lancer ni
à découvrir. Texte UTF-8 seulement pour l'instant ; un type (image, liste de fichiers pour le
glisser-déposer) pourra s'ajouter dans un registre libre du message.

---

### 16.1 Résolution à chaud (`elegant/screen.inc`)

`SYS_WS_MODE` demande au firmware un autre framebuffer, mappé à la même adresse chez Elegant ; le firmware
met l'image à l'échelle de la sortie HDMI, le mode HDMI lui-même ne change pas. Le tampon arrière est pris une
fois pour toutes pour 2560 x 1600 (il n'y a pas de libération de mémoire DMA) : seuls son pas et l'écran
changent. Puis, dans cet ordre :

* les fenêtres `WF_RESERVE` du bas descendent ou montent avec le bas de l'écran, les bords réservés sont
  recalculés ;
* une fenêtre agrandie remplit la nouvelle zone libre (`WEV_RESIZE`), sa place et sa taille d'avant sont
  ramenées dans l'écran ; une fenêtre `WF_RESIZABLE` trop grande reçoit `WEV_RESIZE` ; les autres sont
  replacées, barre de titre visible ; une fenêtre sans cadre reste dans l'écran ;
* chaque fenêtre reçoit `WEV_SCREEN` : `menubar`, `dock`, `voronoy` et le panneau se refont. UIKit y revoit
  son échelle (`ui_echelle`) ; une barre ou un dock qui change de taille refait les bords réservés, et les
  fenêtres agrandies suivent (`reserve_update`) ;
* les tampons de copie de `vncd` et `rdpd` sont rendus, refaits à leur prochain appel (autre numéro dans la
  réponse). `rdpd` envoie SCREEN à son client ; RFB ne le permet pas à un client ordinaire : `vncd` coupe la
  session, le client revient à la nouvelle taille.

Le minuteur de confirmation est dans Elegant : le panneau donne 17 s à `WS_OP_SCREEN_SET` et compte 15 s pour
l'utilisateur. S'il meurt, ou si rien ne s'affiche, Elegant revient seul (`screen_tick`). Le choix n'est écrit
(`ecran=` de `SD0:/etc/options.txt`, lu par le noyau au démarrage) qu'après « Garder ».

Pendant un dialogue (`ui_dialog_run`), `WEV_SCREEN` n'atteint pas les autres fenêtres du programme : l'applet
Écran refait lui-même le panneau après le sien (`ap_fit`). Un programme plein écran qui ne suit pas `WEV_SCREEN`
garde sa fenêtre : découpée, ou le bureau autour.

## 17. Surface attachée à une fenêtre (`elegant/surface.inc`)

Décidé le 29/10/2026 (FEUILLE-DE-ROUTE.md § 8) : on garde le schéma de Jet chez Onyx (le
processus web compose dans une surface partagée, puis signale les zones modifiées) mais la
surface est un **tampon partagé d'ARMPI** et Elegant compose **directement** depuis elle — la
recopie d'Onyx (`onyxsurface.c` → tampon de la fenêtre) disparaît. Sert au lecteur vidéo
(`mediaplayer`), aux émulateurs, aux programmes SDL portés, à Jet.

| Op | Rôle |
|---|---|
| `WS_OP_SURFACE_ATTACH` (25) | a id, b tampon (-1 : détacher), c x \| y << 32, d l \| h << 32 ; données : format, pas, décalage. Elegant mappe le tampon (`BUF_MAP` : le programme l'a autorisé par `BUF_GRANT` ; sinon `E_PERM` et le pid d'Elegant en a), vérifie que le tampon couvre la zone (`VM_PAGEINFO` du dernier octet : une zone plus grande que le tampon ferait tomber le serveur), puis salit le contenu |
| `WS_OP_SURFACE_DAMAGE` (26) | a id, b n rectangles (0 : toute la surface), données : x, y, l, h en 16 bits : les zones sales du compositeur (§ 15), bornées à la surface visible |

* **Formats** : `WS_SF_XRGB8888` (0x00RRGGBB, octet haut ignoré : copie opaque par `bb_blit`) et
  `WS_SF_RGB565` (converti à la composition, `bb_blit565`). Pas libre (≥ une ligne), décalage
  du premier pixel libre : double tampon dans un seul tampon partagé.
* **Composition** : `draw_window` copie le contenu de la fenêtre puis la surface par-dessus, à
  sa zone, découpée par le clip de la zone sale et par le contenu (une fenêtre redimensionnée
  plus petite coupe la surface). Une table `surfaces` (64 o par place de fenêtre, comme
  `windows`) : adresse chez Elegant, tampon, zone, pas, format.
* **Vie** : `req_create` remet l'entrée à zéro, `win_destroy` (fermeture, fin du programme)
  détache ; le tampon est démappé chez Elegant (`BUF_FREE`) quand plus aucune fenêtre ne s'en
  sert. Le programme garde son tampon (il le rend lui-même, ou à sa fin).
* **rdpd** : `WS_OP_WIN_READ` (partie 0) pose la surface par-dessus le contenu recopié
  (`surf_read_over` : le tampon arrière et le clip redirigés vers le tampon de rdpd, comme pour
  le fond) ; `ATTACH` et `DAMAGE` font avancer la génération du contenu (`W_GEN`).
* **Déchirure** : Elegant lit la surface pendant que le programme écrit ; pour l'éviter, deux
  images dans le tampon et `ATTACH` au décalage de celle qui est finie avant `DAMAGE`.
* UIKit : `ui_surface`, `ui_surface_pixels`, `ui_surface_damage`, `ui_surface_damage_list`,
  `ui_surface_move`, `ui_surface_free` (version 172) ; essai `SD0:/bin/surfacetest [s] [rgb565]`
  (trois phases : zone 320 × 240, déplacée et réduite dans le même tampon, agrandie dans un
  tampon neuf). Mesuré sous QEMU : ≈ 125 images 320 × 240 écrites et signalées par seconde
  (XRGB8888 comme RGB565) ; Elegant en compose au plus une par tour de sa boucle (≈ 8 ms).

## 18. Mode poche (`elegant/pocket.inc`)

Contrat entre les pièces et décisions de l'utilisateur : `docs/MODE-POCHE.md` ; étude :
`docs/DESIGN-ETUDES-ONYX.md` section 4. Le même serveur, le même UIKit, les mêmes apps : une
table `POL_*` de plus (`mode_poche`), choisie par `shell=poche` dans `SD0:/etc/system.ini`
(espaces permis autour de `=`).

**Crochets ajoutés à la table** (`mode.inc`) : `POL_FLAGS` (valeur ; `POLF_POCHE`), `POL_PLACE`
(fenêtre créée), `POL_KEY` (touche brute, avant l'app et avant F10/F11), `POL_GONE` (fenêtre qui
disparaît), `POL_RAISE` (`WS_OP_RAISE`). Le bureau les laisse vides. En poche, `POL_WINS` (liste,
ordre, focus changés) trie les fenêtres (`pk_restack`).

**Session et sortie de secours.** Elegant attend 2 s (`PK_GRACE`) avant de lancer
`SD0:/etc/session.poche` (repli sans fichier : menubar `--poche`, home, quick, switcher, notifyd).
Maj appuyée pendant ce temps (ou tenue quand le clavier est reconnu) : la table du bureau et
`SD0:/etc/session` à la place. Une ligne `shell=desktop` dans `SD0:/etc/options.txt` (ou
`SD0:/options.txt`) l'emporte sur `system.ini` : à écrire depuis un PC si Maj ne suffit pas.

**Placement.** Une fenêtre à cadre (redimensionnable ou de taille fixe) est agrandie à la zone
sous la barre par le chemin du bouton agrandir (`win_restore`, `WEV_RESIZE`) ; si elle garde une
taille plus petite (programme qui ignore `WEV_RESIZE`), elle est centrée dans la zone. Un dialogue
`WF_MODAL` garde sa taille, centré ; `veil_fill` pose juste sous lui un voile `TEXTE1` de Givre
(0x14171C, les deux thèmes) à 30 % sur ce qui est déjà composé (l'app, ou l'accueil). Les fenêtres
sans cadre gardent leur place. `WS_OP_WIN_PLACE`, `WIN_MOVE`, `WIN_DRAG` sont sans effet sur une
app ou l'accueil ; ni poignée ni bords.

**Une app devant.** De l'avant vers l'arrière, la première app (et les dialogues au-dessus d'elle)
est montrée, les autres reçoivent `MINI_BEHIND` (ni composées ni cliquables). L'accueil
(`WF_HOME`, couche du fond, la zone sous la barre comme une agrandie) est caché sous l'app devant,
même plus petite que la zone (Réglages, Terminal : centrés sur le fond uni du thème, lu dans
`theme.txt` au démarrage puis suivi par `korin.theme`) ; les fenêtres `WF_BACK` (l'applet agenda) le sont dès qu'un accueil existe.
`WS_OP_RAISE` de la fenêtre `WF_HOME` la met devant (toutes les apps cachées) jusqu'à ce qu'une
app soit créée ou ramenée. Le focus va à la fenêtre montrée la plus en avant, sinon à l'accueil ;
il n'est pas pris à une fenêtre `WF_TOP` (volets, commutateur). Fermer l'app devant (sans autre
fenêtre de son programme) montre l'accueil.

**Cadre.** `POLF_POCHE` : `draw_framed` ne pose que × et, à la place d'agrandir, le trait de
réduire (pris dans l'image du cadre de UIKit, une case plus à droite) ; `win_hit` en fait
`Z_CLOSE` et `Z_MINI` ; `Z_MINI` publie `korin.shell.home`. `FRAME_CAP_W` et UIKit inchangés.

**Touches** (`pk_key`) : Super seul → `korin.shell.home` ; Alt+Tab → `korin.shell.switch` =
`alt` (avec Maj : `alt-prev`), puis `commit` au relâché d'Alt ; Super+Tab → `korin.shell.switch`
= `1` ; Super+N → `korin.shell.quick` ; Super+L → `korin.shell.sleep` ; F11 → barre du haut
masquée (`MINI_BAR` ; `recompute_reserve` ignore les fenêtres cachées, les agrandies suivent),
à la place du diagnostic F11 du bureau. Une touche prise l'est aussi à son relâché.

**Bus** (`elegant/shell.inc`, sans BusKit : `src/svc/inc/buspub.inc`) : Elegant ouvre busd dès
qu'il répond (essai toutes les 500 ms), publie `korin.shell.mode` (gardé ; aussi en bureau),
suit `korin.theme` (le fond quand il n'y a pas d'accueil) et `korin.shell.home`, qu'il ne traite
que sans programme d'accueil (SD0:/sys/home fait lui-même l'aller et le retour par
`WS_OP_RAISE`). Les évènements arrivent dans la file d'Elegant, lue à chaque tour
(`SYS_EV_WAIT`). Sans busd, l'accueil est montré directement.

**Vignettes.** `WS_OP_WIN_THUMB` (37, `snapshot.inc`) : le contenu de la fenêtre et sa surface
attachée, recopiés au début du tampon de l'appelant (celui de `WS_OP_SNAPSHOT`), réduits sur place
par moyenne de blocs, puis posés au décalage demandé ; le reste du tampon sert de brouillon.

**Commutateur** (`src/sys/switcher` : un programme UIKit, le serveur ne dessine pas de texte) :
endormi sur `korin.shell.switch` ; fenêtre plein écran `WF_TOP | WF_GRAB | WF_ALPHA`, voile à 50 %,
cartes de 240 × 190 (vignette 240 × 150 en proportion, icône 20, nom 14, ×), trois visibles,
la plus récente d'abord, la choisie = l'app d'avant (la première si l'accueil est devant), anneau
d'accent de 2 px. Tab et flèches : suivante ; Entrée, toucher : passer ; Suppr, × : fermer
(`WS_OP_WIN_CLOSE`) ; Échap, toucher ailleurs : annuler ; glisser horizontal : défiler.
