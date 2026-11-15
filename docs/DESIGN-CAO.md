# CAO de Korin : moteur et étude de design

Chantier lancé le 14/11/2026 (décision de l'utilisateur du 13/11, mémoire `armpi-idee-cao.md`) : une CAO libre, aussi
facile que Fusion ou Shapr3D, **native pour Korin** (C++ sur l'UIKit de l'OS et GlKit, Pi et PC), qui succède à
**3DForge** d'Onyx. Le moteur change : **OpenCASCADE** (B-rep exact) à la place de Manifold (maillages), et
**PlaneGCS**, le solveur d'esquisses de FreeCAD, à la place d'une esquisse sans solveur. L'interface validée de 3DForge
est gardée : clic-déplacer-clic, cotes sur le dessin, frise d'historique, mode Fabrication (G-code GRBL).

Ce document a deux parties :

* **le moteur** (§15, « Construction et résultats ») : OCCT 8.0.1 et PlaneGCS portés pour Korin x86-64, et l'essai sous
  QEMU PC ;
* **l'étude de design** (§1 à §14), faite avec un designer (à la demande de l'utilisateur : « l'interface la plus
  professionnelle possible »). **Maquettes validées par l'utilisateur le 15/11** (« elles me conviennent ») ; les
  questions du §13 tranchées le même jour (toutes les recommandations retenues). L'app n'est pas encore écrite, rien n'est dans `src/`.

**Maquettes** (treize écrans, Givre / Onyx Noir / Auto en haut à droite, taille réelle ou ajustée, un bouton qui montre
la zone `FRAME_CAP_W` ; quatre écrans ont deux états, bouton *État* ; un onglet *Jetons*) :
https://claude.ai/artifact/Mudrvp7z3EWPnHAvogwFZc

**Niveau de preuve.** Les maquettes sont en HTML aux pixels réels : 1920 × 1080 pour le ZBook (valeur confirmée par
l'utilisateur pour WebStudio), 800 × 600 pour le Pi. Couleurs : les jetons de `uk_theme.c` (`uk_jet_givre`,
`uk_jet_noir`). Géométrie : Givre calme ([DESIGN-PROPOSAL.md](DESIGN-PROPOSAL.md) §4 : contrôles 32, lignes 30, texte
14/13/12, rayons 6/8/10/12, aucun fondu, accent pour le choix ou l'action principale, 96 px libres en haut à droite).
**La pièce est un vrai modèle 3D**, pas un dessin à main levée : l'équerre de 3DForge (plaque 90 × 60 × 8, coins R 8,
paroi de 37 mm, raccord R 6, coins de paroi R 12, deux lamages Ø 9 / Ø 14, un trou Ø 22, un oblong de 20 × 9), décrite
face par face, projetée, ombrée par face selon la normale, arêtes cachées retirées. Son volume, 63,5 cm³, est celui que
donnait 3DForge. Les trajets d'ébauche sont calculés aussi. Le générateur des maquettes (Python, ≈ 2 000 lignes) est
resté hors du dépôt. L'app n'a été ni compilée ni essayée ; les icônes sont dessinées pour l'étude, les vraies viendront
de la chaîne `tools/icons`.

---

## 1. Les principes

1. **La vue d'abord.** La pièce prend 1600 × 872 px sur 1920 × 1080. Une seule barre en haut, un panneau à droite,
   la frise en bas. Rien d'autre de fixe.
2. **Le geste de 3DForge reste le cœur.** Clic, on s'éloigne, clic, on monte, clic. Les cotes sont sur le dessin,
   cliquables, et prennent le clavier pendant le geste. L'opération (joindre, enlever…) est choisie pour vous.
3. **Choisir puis agir, ou agir puis choisir.** Une face choisie montre ses actions (Shapr3D) ; une commande lancée
   demande ses arêtes ou ses faces (Fusion). Les deux chemins mènent au même panneau.
4. **Une seule place pour les valeurs : le panneau de droite.** Pas de petite fenêtre qui flotte sur la pièce, sauf
   pour finir un travail (Exporter, Écrire le G-code). OK et Annuler sont toujours en bas à droite.
5. **Le clavier partout.** Chaque champ accepte une formule, une unité et un paramètre nommé. Tab passe au champ
   suivant, Entrée valide, Échap annule. Toute commande a une touche, et Ctrl+K trouve le reste.
6. **Dire l'erreur là où elle est, avec la sortie.** Jamais de boîte modale pour un congé impossible : le champ, les
   arêtes en cause et un bandeau près d'elles le disent, avec la valeur qui marche.
7. **Givre calme.** L'accent marque seulement le choix ou l'action principale (un bouton principal par vue). Les
   couleurs des axes et des états de l'esquisse ne suivent pas l'accent. Aucun fondu ; une réponse sous 50 ms, les
   calculs lourds (OpenCASCADE) en tâche de fond.

## 2. Fusion, Shapr3D, Korin

| Sujet | Fusion 360 | Shapr3D | **Korin** | Pourquoi |
|---|---|---|---|---|
| Structure | Onglets d'espaces (Solide, Surface, Tôlerie…), navigateur à gauche, frise en bas | Vue plein écran, barre à gauche, panneau contextuel | **Une barre, la vue, un panneau à droite, la frise.** Les corps dans un petit panneau flottant (3DForge) | Le volume (solides seulement) tient sur une rangée ; l'utilisateur avait retiré le panneau de gauche |
| Barre d'outils | Icônes groupées sous une légende à menu (CRÉER ▾) | Peu d'outils, icône + nom | **Groupes de Fusion** : Créer, Modifier, Combiner, Construire, Inspecter ; la légende ouvre la liste nommée | 19 outils sur 870 px ; les noms restent à un clic et dans les infobulles |
| Espaces de travail | Onglets (Design, Manufacture, Simulation…) | Aucun | **Une bascule Conception / Fabrication** (3DForge) | Deux métiers, pas six |
| Commande en cours | Petite fenêtre flottante sur la vue | Champs près du geste | **Champs sur le dessin + panneau à droite**, toujours au même endroit | La fenêtre flottante de Fusion cache la pièce et se déplace |
| Choisir d'abord | Possible, mais les menus ne s'adaptent qu'au clic droit | Le cœur du produit | **Le panneau montre les actions de ce qui est choisi**, avec leur touche | La force de Shapr3D, sans en faire le seul chemin |
| Menu au clic droit | Menu radial (marking menu) + liste | Menu court | **Menu linéaire court**, les 3 actions probables en tête ; pas de radial en v1 | UIKit n'en a pas ; la palette et les touches couvrent le besoin |
| Recherche de commande | Touche S (boîte à outils) | Recherche | **Ctrl+K** : commandes, paramètres, étapes, réglages | Le même geste que dans WebStudio (Ctrl+P) |
| Historique | Frise en bas, barre de reconstruction, rouge et jaune | Historique optionnel | **La frise de 3DForge et de Fusion**, barre glissée, étapes en erreur, clic droit | Validée par l'utilisateur ; Fusion montre que ça marche |
| Esquisse | Contraintes, bleu/noir, glyphes | Contraintes déduites, peu de glyphes | **Contraintes visibles (Fusion) + déduites pendant le tracé (Shapr3D)**, degrés de liberté toujours dits, conflit désigné | PlaneGCS donne les DDL et la liste des contraintes en conflit : on les montre |
| Congé | Flèche, chaîne tangente, variable | Flèche, très direct | **Flèche + chaîne tangente au double-clic**, rayon variable plus tard | OCCT le permet sur toute arête |
| Pousser/tirer | Commande Q à part | Le geste principal | **Q, et l'action en tête du panneau** quand une face est choisie ; opération auto | Le geste le plus rapide pour modifier une pièce |
| Erreurs | Étape en rouge, message dans une fenêtre | Message court | **Champ rouge + arêtes rouges + bandeau avec la valeur possible** ; étape rouge et cause en clair dans le panneau | Dire quoi faire, pas seulement que ça a échoué |
| Petits écrans | — | iPad : panneaux qui se replient | **Un volet à la fois** à 800 × 600 : panneau en tiroir, barre sans légendes | Le Pi doit rester utilisable |
| Fabrication | Très complet, très dense | Aucune | **Celle de 3DForge** (brut, 27 origines, ébauche, contour, G-code vérifié), plus le procédé (fraisage, résine, filament) | Validée et déjà écrite (`fcam.h`, `fprint.h`, `ffdm.h`) |

**Ce qu'on refuse, et pourquoi.** Les onglets d'espaces de Fusion (ils cachent les outils et ne servent pas à des
solides). Ses fenêtres de commande flottantes (elles couvrent la pièce). Le menu radial dans la première version (un
widget de plus à écrire pour un gain que la palette donne déjà). L'interface tout-gestes de Shapr3D (faite pour le
crayon ; à la souris, on veut des boutons visibles et des touches).

## 3. La fenêtre

| Zone | 1920 × 1080 | 800 × 600 (Pi) |
|---|---|---|
| Barre de menus d'Elegant | 28 px : Fichier, Édition, Affichage, Créer, Modifier, Inspecter, Fenêtre, Aide | 28 px, moins de menus |
| Barre d'outils | 56 px : document (nom, état), bascule, groupes (icônes 20 dans des boutons 36 × 32, légende 12 px), annuler, refaire, paramètres, Exporter ; 96 px libres à droite | 40 px : icônes seules, « … » pour le reste |
| Vue | 1600 × 872 : corps (232 px, en haut à gauche), cube de vue 112 px et 5 boutons (en haut à droite), trièdre (en bas à gauche) | toute la largeur ; corps repliés en puce, cube 84 px |
| Frise | 52 px sous la vue : lecture (5 boutons), étapes de 36 px tous les 40 px, barre bleue | 36 px, étapes de 30 px |
| Panneau de propriétés | 320 px, toute la hauteur de la vue et de la frise ; OK et Annuler en bas | tiroir de 280 px par-dessus la vue, ouvert avec la commande |
| Barre d'état | 24 px : ce qu'on peut faire maintenant, mesure rapide, filtre de sélection, grille, accrochage, unité | 24 px, la phrase seule et l'unité |
| Panel d'Elegant | 48 px | 48 px |

## 4. Les écrans, un par un

### a. Fenêtre principale

Une face plane (l'avant de la paroi) est choisie : elle est teintée d'accent et bordée d'un trait accent de 2 px. Le
panneau donne ses mesures (aire, taille, normale, plan) puis **les actions qui s'y appliquent** : pousser ou tirer
(Q), esquisser dessus (S), décaler, congé sur ses arêtes (F), percer (H), regarder (N). Dessous, le corps : couleur,
matière, volume, masse, et « solide fermé ». Le pointeur survole le bord d'un lamage : il s'éclaire avant le clic et
une bulle dit ce que c'est et ce que font le clic et le double-clic. La barre d'état reprend l'aire (mesure rapide,
comme Fusion en bas à droite).

### b. Boîte en trois clics

Le geste de 3DForge. Le centre est marqué, la largeur (90) et la profondeur (8) portent un cadenas, la hauteur est
un champ ouvert sur le dessin (« 37 » et le curseur) au bout de la flèche. Le fantôme est en accent à 20 %, ses
arêtes cachées en pointillé. Le panneau suit l'étape (Centre ✓, Largeur et profondeur ✓, Hauteur) et propose
l'opération, **choisie pour vous** (« Joindre », badge AUTO) avec la raison en une ligne. La frise montre l'étape en
cours en pointillé. La même boîte se fait sans pointeur : on tape les trois valeurs, le fantôme suit, Entrée.

### c. Esquisse et contraintes

La barre devient celle de l'esquisse : Tracer (ligne, rectangle, cercle, arc, oblong, polygone, spline), Modifier
(ajuster, décaler, congé, symétrie), Contraintes (dix glyphes), Outils (construction, projeter, cote) ; *Terminer
l'esquisse* est le bouton principal. Couleurs : **bleu** pour ce qui peut encore bouger, **noir** (blanc en Noir) pour
ce qui est fixé, **rouge** pour un conflit, gris pointillé pour la construction ; une légende reste en bas à gauche.

* *Conflit désigné* : une cote Ø 10 vient d'être posée sur un trou déjà lié à l'autre par « Égal » et coté Ø 8.
  PlaneGCS dit quelles contraintes se contredisent : les trois passent en rouge, sur le dessin et dans la liste. Le
  panneau l'explique (« Rien n'a bougé ») et propose trois sorties : retirer la nouvelle cote, supprimer « Égal », ou en
  faire une cote de référence (pilotée).
* *Tracé* : une ligne part du centre de l'arc droit. Pendant le tracé, les contraintes **déduites** apparaissent en
  glyphes fantômes (verticale, coïncidence, alignement sur le haut de l'arc gauche, avec sa ligne pointillée). Le
  panneau dit lesquelles seront ajoutées ; Maj trace sans en déduire.

Dans les deux cas l'état est dit en haut (« 1 degré de liberté ») et dans le panneau, avec ce qui bouge encore (le
rayon de l'arc droit). PlaneGCS rend ce nombre à chaque résolution (0,5 ms à l'essai).

### d. Congé sur une chaîne d'arêtes

Le bord de l'oblong : quatre arêtes, deux droites et deux demi-cercles, prises d'un double-clic (**chaîne
tangente**). Elles sont tracées en accent ; l'aperçu est la vraie forme calculée par OpenCASCADE (teinte d'aperçu),
avec le temps du calcul et le volume enlevé. La flèche, posée sur la bissectrice, se glisse ; le rayon se tape.

*Rayon trop grand* (9 mm sur une plaque de 8) : le champ passe en rouge avec la raison ; les arêtes passent en rouge ;
un bandeau près d'elles donne **le plus grand rayon possible** (7,9 mm, cherché en tâche de fond par dichotomie sur
`BRepFilletAPI_MakeFillet`) et un bouton pour l'utiliser ; OK est grisé. Le type « Variable » est montré mais pourra
attendre.

### e. Pousser/tirer une face

Le dessus de la paroi est choisi, la flèche tirée. *Tirer* : fantôme accent, « Joindre » AUTO. *Pousser* : fantôme
rouge, « Enlever » AUTO. La distance est une formule (« épaisseur + 4 ») : le badge *fx* le signale, la valeur (12 mm)
suit sous la puce et sous le champ. Étendue (distance, jusqu'à une face, à travers), côtés, dépouille.

### f. Frise : une étape en erreur, la barre déplacée

La barre bleue est glissée après « Perçage 2 » : les étapes suivantes sont grisées et pas jouées, la pièce est
montrée sans l'oblong, un bandeau le rappelle avec *Aller à la fin*. « Congé 4 » est en erreur (pastille rouge) : ses
arêtes ont disparu quand Esquisse 2 a changé. C'est le **nommage topologique** (§12). Le panneau donne la cause en
clair, ce qui en dépend, et les sorties (choisir de nouveau, désactiver, supprimer, ouvrir l'esquisse). Le détail
technique d'OCCT reste en petit. Le clic droit sur une étape ouvre son menu (modifier, mettre la barre ici, désactiver,
renommer, grouper, supprimer).

### g. Fabrication

* **g1, brut et origine.** La bascule passe à *Fabrication* ; à côté, le **procédé** (fraisage Two Trees TTC 450 en
  GRBL ; résine Photon Mono 2 ; filament Kobra X) décide des outils et du bouton principal (*G-code…* ici). Le brut est
  dessiné autour d'Équerre (5 mm sur les côtés, 2 dessus), avec les 27 points d'origine sur le dessin et dans trois
  grilles ; l'origine choisie (dessus, avant gauche) porte les axes de la machine.
* **g2, ébauche et trajets.** Sept niveaux de 6 mm, passes de 2,4 mm autour de la paroi puis de la plaque, la rampe
  d'entrée en pointillé ; les trajets derrière la pièce sont cachés par elle. La simulation se règle comme une vidéo
  (lecture, vitesse, matière restante). Le panneau finit par le résultat : longueur, durée, Z le plus bas.
* **g3, G-code vérifié.** Quatre vérifications réussies et un avertissement (coins pris à pleine largeur), le début du
  fichier en couleurs, sa taille et sa durée, *Enregistrer le G-code*.

### h. Export

Un seul dialogue. À gauche, les formats avec leur usage : STEP et IGES (exacts, pour une autre CAO), STL et 3MF (pour
imprimer), le plan 2D (DXF, SVG, PDF) et l'image PNG de 3DForge. *STEP* : AP242 par défaut, noms et couleurs ; une
phrase dit que les congés restent exacts et que l'historique ne part pas. *STL* : la finesse en quatre crans (écart et
angle dessous), binaire, l'aperçu du maillage, le nombre de triangles et la taille avant d'écrire. Ctrl+E rouvre le
dialogue avec les derniers choix.

### i. Le Pi, 800 × 600

Le congé de l'écran d, un volet à la fois : barre de 40 px sans légendes, corps repliés en puce, frise de 36 px, et
le panneau devenu un **tiroir** de 280 px qui s'ouvre avec la commande et se ferme avec OK. La flèche et la puce de
cote restent utilisables dans la partie visible de la vue.

### j. Palette de commandes (Ctrl+K)

« con » trouve Congé (F), Cône, Contour 2D, Plan de construction, une contrainte grisée avec la raison (« seulement
dans une esquisse »), le paramètre `conge_raccord` et les étapes « Congé 1…4 ». Rien n'est choisi : le panneau montre
le **document** (unités, précision, paramètres nommés avec leur formule et leur valeur, fichier).

### k. Document vide

Une carte dit quoi faire : Esquisse (en premier, bouton principal), Boîte, Cylindre, Importer ; la zone où déposer un
STEP ; les récents avec une vignette. La vue montre l'origine et ses trois plans, cliquables. Les outils qui demandent
un corps sont grisés ; la frise annonce ce qu'elle contiendra.

## 5. Sélection et filtres

* **Un clic choisit ce qui est sous le pointeur**, dans cet ordre : sommet, arête, face, corps (à quelques pixels
  près pour les sommets et les arêtes). Le survol éclaire d'avance ce qu'un clic prendrait.
* **Maj+clic** ajoute, **Ctrl+clic** retire. **Double-clic sur une arête** : sa chaîne tangente. **Double-clic sur une
  face** : la boucle de faces qui l'entoure (plus tard).
* **Tab** fait passer au candidat suivant sous le pointeur (face derrière, arête voisine), comme la sélection
  « à travers » des CAO classiques.
* **Glisser dans le vide** : un rectangle. De gauche à droite, ce qui est entièrement dedans ; de droite à gauche, ce
  qu'il touche (SolidWorks, Fusion).
* **Filtre** dans la barre d'état : Tout, Faces, Arêtes, Sommets, Corps, Esquisses. Une commande pose son propre
  filtre (le congé ne prend que des arêtes) et le montre.
* **Échap** vide la sélection.

## 6. Naviguer dans la vue

* **Souris** (proposé, à confirmer : question 3) : bouton droit glissé, tourner ; bouton du milieu glissé, déplacer ;
  molette, zoomer vers le pointeur ; clic droit sans glisser, le menu. C'est le réglage d'Onshape ; Fusion, SolidWorks
  et 3DForge sont des préréglages.
* **Pavé tactile** du ZBook : deux doigts, tourner ; Maj + deux doigts, déplacer ; Ctrl + deux doigts (ou pincer, si
  Korin le donne un jour), zoomer.
* **Cube de vue** : un clic sur une face, une arête ou un coin oriente la vue ; *Maison* revient à la vue de départ.
  Sous le cube : maison, ajuster (F6), style (ombré, ombré avec arêtes, fil de fer), regarder la face choisie (N),
  coupe. Vues : Alt+1 avant, Alt+2 dessus, Alt+3 droite, Alt+0 isométrique.
* **Orthographique par défaut** (les cotes se lisent juste) ; perspective dans *Affichage*.

## 7. Unités, champs, paramètres, mesures

* **Unité du document** : mm (choisie à la création, changeable). Les champs acceptent d'autres unités (« 1 in »
  devient 25,4 mm, « 30° »), les quatre opérations, les parenthèses, et les **paramètres nommés** (« épaisseur * 2 »).
  Un champ calculé porte le badge *fx* et montre sa valeur.
* **Paramètres nommés** : dans le panneau quand rien n'est choisi (écran j) et par Ctrl+Maj+P. Changer une valeur
  reconstruit ce qui s'en sert. Taper « = » dans un champ crée un paramètre depuis sa valeur.
* **Précision affichée** : 0,01 mm (réglage du document) ; le calcul garde toute la précision d'OCCT.
* **Mesurer** (I) : deux clics, la distance, l'angle ou l'écart minimal, dans le panneau et sur le dessin. Choisir une
  arête ou une face donne déjà sa longueur ou son aire en bas à droite.

## 8. Dire les erreurs

| Cas | Où | Comment |
|---|---|---|
| Valeur impossible pendant une commande (congé trop grand, coque trop épaisse) | champ, dessin, bandeau près des arêtes | champ rouge + une phrase de cause ; géométrie en cause en rouge ; la valeur qui marche, avec un bouton ; OK grisé |
| Étape qui ne se reconstruit plus | frise, panneau | pastille rouge sur l'étape ; la pièce montrée jusqu'à l'étape d'avant ; cause en clair, ce qui en dépend, sorties |
| Avertissement (géométrie fragile, petite arête) | frise, panneau | pastille jaune, une phrase, pas de blocage |
| Conflit d'esquisse | dessin, panneau, barre d'état | contraintes en rouge, trois sorties ; rien ne bouge tant qu'on n'a pas choisi |
| Import STEP partiel | bandeau dans la vue | « 2 faces réparées, 1 corps ouvert » et *Voir* |
| Vérification du G-code | dialogue | chaque vérification sur sa ligne, réussie ou non ; un échec grise *Enregistrer* |

Règles : une phrase, au présent, qui dit la cause et la sortie ; jamais « erreur 0x… » au premier niveau (le détail
d'OCCT reste en petit, dans *Détails*) ; jamais de boîte modale pendant un geste.

## 9. Raccourcis

Proches de Fusion là où il en a ; une touche seule n'agit que si aucun champ n'a le clavier.

| Touche | Action | Touche | Action |
|---|---|---|---|
| **Ctrl+K** | palette de commandes | **Ctrl+Z / Ctrl+Y** | annuler / refaire |
| **S** | esquisse (Fusion : boîte à outils) | **Ctrl+S / Ctrl+O / Ctrl+N** | enregistrer, ouvrir, nouveau |
| **B** | boîte | **Ctrl+I / Ctrl+E** | importer / exporter |
| **E** | extruder | **Ctrl+Maj+P** | paramètres nommés |
| **Q** | pousser/tirer | **Entrée / Échap** | valider / annuler (ou vider la sélection) |
| **F** | congé | **Tab** | champ suivant pendant un geste ; candidat suivant sous le pointeur |
| **Maj+F** | chanfrein | **Suppr** | supprimer la sélection ou l'étape |
| **H** | perçage | **F6** | ajuster la vue |
| **M** | déplacer | **N** | regarder la face choisie |
| **I** | mesurer | **Alt+1, 2, 3, 0** | avant, dessus, droite, isométrique |
| Esquisse : **L R C A** | ligne, rectangle, cercle, arc | Esquisse : **D** | cote |
| Esquisse : **T O X** | ajuster, décaler, construction | Esquisse : **H V =** | horizontale, verticale, égal |
| Esquisse : **Maj** (maintenu) | tracer sans contraintes déduites | Esquisse : **Ctrl+Entrée** | terminer l'esquisse |
| Fabrication : **G** | écrire le G-code | Fabrication : **Espace** | lire / arrêter la simulation |

Les raccourcis sont listés dans *Aide › Raccourcis* et dans la palette ; un fichier les rend modifiables.

## 10. Les jetons

### 10.1 Ceux d'UIKit, employés tels quels

`uk_theme.c` (`TJ_*`, noms de `uk_consts.h`) : FACE `F5F6F8` / `15171B`, SURFACE `FFFFFF` / `1B1E23`, SUNKEN `ECEEF2` / `101216`, RAISED
`FFFFFF` / `262A31`, LINE `E2E5EA` / `2A2E36`, LINE_STRONG `B8BFC9` / `525966`, TEXT1 `14171C` / `ECEEF2`, TEXT2
`4C5563` / `B0B6C0`, TEXT3 `5F6877` / `8B929D`, ACCENT `2563D8` / `2F72DC`, ACCENT_LIGHT `E4ECFB` / `1C2F4D`,
ACCENT_DARK `1D4FB3` / `2560C0`, ACCENT_VIVID `2563D8` / `7DB4F2`, DANGER `D93A3F`, SUCCESS `1E8A5A` / `46A758`, WARNING
`C4861C` / `F1B33B`, MENU `FFFFFF` / `262A31`. (Givre / Onyx Noir.) En Noir, un texte accentué prend ACCENT_VIVID, comme
l'a réglé WebStudio. Les noms suivent `uk_consts.h` (anglais) ; DESIGN-PROPOSAL les donnait en français.

### 10.2 À ajouter à UIKit (en fin de table, après `TJ_ACCENT_INK`)

| Jeton | Givre | Onyx Noir | Pourquoi dans UIKit |
|---|---|---|---|
| `TJ_HOVER` | `E9ECF1` | `23272E` | proposé par DESIGN-PROPOSAL sous le nom `TJ_SURVOL`, pas encore dans la table ; toute app en a besoin |
| `TJ_AXIS_X` | `D93A3F` | `E5534B` | axes et trièdres : CAO, Arabesque, émulateurs 3D, Paint 3D un jour ; ne doivent **pas** suivre l'accent |
| `TJ_AXIS_Y` | `1E8A5A` | `46A758` | idem |
| `TJ_AXIS_Z` | `2563D8` | `4C8DF0` | idem ; sert aussi au « libre » de l'esquisse et aux trajets d'usinage |

Ces valeurs reprennent DANGER, SUCCESS et l'accent bleu par défaut, pour rester dans la famille ; elles en sont
séparées pour qu'un accent orange ou vert ne change ni les axes ni les états de l'esquisse. `TJ_SIZE` grandit de
16 octets ; `check_constants.py` doit voir la nouvelle valeur partout.

### 10.3 Calculés dans l'app (formules `ui_mix(a, b, t)`, t sur 256)

| Couleur | Formule | Givre | Onyx Noir |
|---|---|---|---|
| fond de la vue | `ui_mix(SURFACE, SUNKEN, 128)` ; Noir : `ui_mix(FACE, SUNKEN, 115)` | `F6F6F8` | `131519` |
| grille fine / tous les 50 mm | `ui_mix(vue, LINE_STRONG, 72)` / `154` | `E5E7EB` / `D1D5DC` | `25282F` / `363A43` |
| corps « Acier » (palette du document) | valeur fixe par thème | `A8B6C8` | `8794A6` |
| ombrage | corps mêlé de blanc (jusqu'à 33 %) ou de noir (jusqu'à 37 %) selon la normale | | |
| arêtes | `ui_mix(corps, presque noir, 184)` | `39414B` | `181B1E` |
| face choisie | `ui_mix(corps, ACCENT, 107)`, bord ACCENT 2 px | `7193CF` | `5D84C0` |
| survol | `ui_mix(ACCENT, blanc, 97)` ; Noir : ACCENT_VIVID | `789EE7` | `7DB4F2` |
| aperçu d'une opération | `ui_mix(corps, ACCENT, 87)` | `7B9ACD` | `6687BB` |
| fantôme Joindre / Enlever | ACCENT / DANGER, opacité 20 % / 24 %, arêtes pleines | | |
| esquisse : libre / contrainte / conflit | AXIS_Z (Noir : `ui_mix(AXIS_Z, blanc, 38)`) / TEXT1 / DANGER | `2563D8` | `679EF2` |
| trajet d'usinage / rapide / brut | AXIS_Z / WARNING pointillé / WARNING à 7 % | | |
| bulle | fond TEXT1, texte FACE (règle Givre calme) | | |

Un corps a sa couleur (cinq teintes douces dans le panneau, comme 3DForge) : c'est une donnée du document, pas du
thème ; seule la teinte par défaut dépend du thème.

## 11. Ce qui manque à UIKit et à GlKit

Ce qui existe déjà et servira : `ui_toolbar` / `ui_toolbar_add` (glyphe + libellé, `UTB_TOGGLE`, `UTB_SEP`),
`ui_segmented`, `ui_numeric`, `ui_textbox`, `ui_dropdown`, `ui_checkbox`, `ui_list` + `ui_list_cell` (lignes dessinées
par l'app), `ui_tree`, `ui_panel` + `ui_layout` (`ULY_GRID`), `ui_groupbox`, `ui_popup_ex` (icône + texte),
`ui_popup_local`, `ui_menu` + `ui_menu_key`, `ui_w_tip`, `ui_tip_at`, `ui_dialog_*`, `ui_view` (vue dessinée à la main),
`ui_canvas`, `ui_surface` + `ui_surface_source` (tampon partagé : la sortie de Mesa), `ui_pointer_lock`
(`WEV_M_REL`), `ui_cursor` (`UC_CROSS`, `UC_MOVE`…), `ui_glyph`, `ui_metric` (`UMT_TOUCH`), `ui_anchor` /
`ui_design_size`, `ui_file_open` / `ui_file_save` + `ui_file_filter`, `ui_drop_get` (déposer un STEP),
`ui_colorpicker`, l'annulation d'UIKit.

**Manques, du plus bloquant au plus petit** (nouvelles fonctions en fin de `uikit.abi`) :

1. **Des widgets au-dessus de la vue GL.** La sortie de Mesa est une surface d'Elegant posée sur sa zone ; seuls les
   boutons du cadre sont redessinés par-dessus (`surf_cover`). Or la maquette met sur la vue le panneau des corps, le
   cube, les boutons de vue, les puces de cote, les bandeaux, la palette et les menus. Deux voies : (a) **Elegant
   compose des rectangles de la fenêtre au-dessus de la surface** (proposé : `ui_surface_overlay (surface, rects, n)`,
   utile aussi au lecteur vidéo, aux émulateurs, à Jet) ; (b) l'app dessine ces widgets dans une image et la passe à
   GL comme texture. **Je recommande (a)** : les widgets restent ceux d'UIKit (focus, clavier, thème) et le coût est
   dans Elegant, une fois. Les menus et la palette, fenêtres à part, passent déjà au-dessus.
2. **Une surface GL qui suit la taille de la fenêtre.** `armgl` garde la taille de sa surface. Faire comme Avalonia
   (`ArmpiGl.cs`) : une surface de la taille de l'écran, dont on ne rattache que la zone de la vue
   (`ui_surface_move`). À mettre dans une petite aide de GlKit pour toutes les apps GL.
3. **Un champ de quantité** (`ui_quantity`) : formules, unités (mm, cm, in, °), noms de paramètres avec complétion
   (rappel vers l'app), badge *fx*, état d'erreur avec un message dessous, valeur calculée. `ui_numeric` n'a que min,
   max et +/-. Le même champ, **flottant**, sert de puce de cote : ancré à un point de la vue, il **prend les chiffres
   tapés sans clic** pendant un geste, et Tab passe à la puce suivante.
4. **Les touches seules et les noms de touches.** `ui_menu_key` sait « Ctrl+Maj+S », les lettres, les chiffres et
   F1–F12, mais pas Suppr, Échap, Entrée, Tab, les flèches ni « = ». Il faut ces noms, et qu'une **touche seule ne
   déclenche rien quand un champ a le clavier**. Plus un fichier de raccourcis modifiable.
5. **La palette de commandes** (`ui_command_palette`) : fenêtre `WF_GRAB` comme le lanceur, champ, liste virtuelle en
   groupes, recherche floue sans accents, raccourci à droite, raison d'un élément grisé ; entrées prises dans le texte
   de `ui_menu` plus celles que l'app ajoute (paramètres, étapes). WebStudio en a besoin aussi (Ctrl+P).
6. **Des menus plus riches** : colonne du raccourci, sous-menus, titres de section, élément dangereux en rouge,
   élément grisé avec sa raison ; et un **menu en grille illustré** (les formes de 3DForge). `ui_popup_ex` n'a
   qu'icône + texte.
7. **Une barre d'outils à groupes** : légende sous le groupe qui ouvre la liste nommée, bouton à menu (petit
   triangle), état actif, débordement « … » quand la place manque (800 px).
8. **Un segmenté et une liste d'options à icônes** avec badge (« AUTO ») ; `ui_segmented` ne prend que du texte.
9. **Un bandeau d'état** (`ui_notice` : info, réussite, avertissement, erreur ; une phrase ; une ou deux actions), dans
   un panneau ou flottant sur une vue. Il sert à toutes les apps.
10. **Une infobulle riche** : nom, touche (seconde couleur), une ligne d'aide. `ui_w_tip` ne prend que du texte.
11. **Un tiroir** pour les petits écrans : un panneau qui passe par-dessus le contenu, avec l'ombre du niveau 3, sans
    animation (`UPN_DRAWER` sur `ui_panel`).
12. **La souris et le pavé** : bouton du milieu glissé (vérifier qu'il arrive dans `WEV_MOUSE` +16), position de la
    molette (`WEV_WHEEL` ne donne que les crans : l'app retiendra la dernière position), défilement horizontal et
    pincement du pavé tactile (absents), curseurs *tourner* et *déplacer la vue* (`UC_*` à ajouter).
13. **Les jetons** du §10.2 (`TJ_HOVER`, `TJ_AXIS_X/Y/Z`), et **environ 90 glyphes d'outils** (16 et 20 px) par `tools/icons`, au format alpha seul
    (`ARIC` version 2) que propose DESIGN-PROPOSAL.
14. **Petit** : un analyseur de couleurs pour le G-code (`UTA_GCODE`) dans `uk_lex.c`, pour le dialogue de G-code.

**Ce qui reste dans l'app** (rien à ajouter au kit) : la frise (une `ui_view`), le cube de vue, la vue elle-même, les
glyphes de contraintes, les cotes 2D de l'esquisse, la simulation d'usinage.

**Côté GlKit / rendu** (l'app, avec ce que Mesa donne) :

* Anticrénelage : MSAA 4× si `armgl` le donne sur le v3d (à vérifier), sinon un FXAA dans le shader.
* Arêtes : GLES 3 ne garantit pas de traits plus épais que 1 px ; les arêtes se dessinent en quadrilatères à l'écran
  (shader), au-dessus des faces avec `glPolygonOffset`. Profondeur : le format réglé le 5/11 dans `armgl`.
* Sélection : par le processeur (BVH d'OCCT sur le maillage, tolérance en pixels), pas par relecture GPU (`armgl_read`
  lit de la mémoire non cachée).
* Textes de la vue (cotes, bulles) : par UIKit au-dessus (manque 1), pas dans GL.
* GL sur son propre thread et calculs d'OCCT (aperçus, rayon maximal, reconstruction) sur un autre : la leçon de
  gcemu (GL figé sur le ZBook tant qu'il partageait le thread de l'interface).
* **Le PC** : le chemin GL du ZBook (rendu logiciel ou GPU ?) et sa vitesse sur 200 000 triangles sont à mesurer avant
  de promettre la vue au même confort que sur le Pi.

## 12. Les risques

* **Nommage topologique.** Une étape garde des références à des faces et des arêtes d'OCCT ; si une étape plus haut
  change la forme, ces références peuvent pointer ailleurs ou nulle part (écran f). FreeCAD a mis des années à le
  traiter. Proposé pour la v1 : chaque référence est gardée avec sa **signature** (type, étape qui l'a créée, position,
  normale, voisines) et retrouvée par la plus proche après reconstruction ; si rien n'est assez proche, l'étape passe
  en rouge avec « choisir de nouveau ». On ne promet pas mieux en v1, et on le dit dans l'aide.
* **Le temps de calcul sur le Pi.** Un congé d'OCCT sur une pièce moyenne peut prendre des centaines de ms sur le
  Cortex-A72. Donc : aperçus en tâche de fond, l'ancien aperçu reste affiché jusqu'au nouveau, indicateur discret dans
  la barre d'état ; jamais d'interface figée.
* **Les widgets sur la vue** dépendent du manque 1 ; sans lui, la vue serait entourée de panneaux fixes, comme
  3DForge à ses débuts.
* **Le chemin GL du PC** (§11), non mesuré.

## 13. Questions à l'utilisateur

**Réponse de l'utilisateur le 15/11 : toutes les recommandations sont retenues.** L'app s'appelle **Forge**
(`forge.app`, pièces en `.forge`) ; barre d'outils à groupes avec le réglage « Montrer le nom des outils » ; bouton
droit pour tourner, milieu pour déplacer, molette vers le pointeur, préréglages Fusion, SolidWorks et 3DForge ; pas de
menu radial dans la v1 ; touches de Fusion, S pour l'esquisse et Ctrl+K pour la recherche ; fabrication : fraisage
GRBL d'abord, puis résine, puis filament. Les maquettes disent encore « CAO » pour le nom.

1. **Le nom de l'app.** *Recommandation : « Forge »*, qui garde le lien avec 3DForge sans le chiffre (le fichier
   `forge.app`, les pièces en `.forge`). Les maquettes disent « CAO » en attendant.
2. **La barre d'outils : groupes à légende (Fusion) ou nom sous chaque outil (3DForge) ?** *Recommandation : les
   groupes*, comme dans les maquettes : 19 outils sur une rangée ; les noms sont dans les légendes, les infobulles et
   la palette. Un réglage « Montrer le nom des outils » pour qui préfère 3DForge.
3. **La souris : bouton droit glissé pour tourner (Onshape), ou Maj + bouton du milieu (Fusion) ?** Et sur le ZBook,
   souris ou pavé tactile ? *Recommandation : bouton droit pour tourner, milieu pour déplacer, molette pour zoomer
   vers le pointeur*, avec des préréglages Fusion, SolidWorks et 3DForge dans les réglages.
4. **Le menu radial de Fusion : dans la v1 ou plus tard ?** *Recommandation : plus tard.* Un menu au clic droit court
   et contextuel, plus Ctrl+K, couvre le besoin et ne demande pas de widget neuf.
5. **Les touches : celles de Fusion, sauf S pour l'esquisse (et Ctrl+K pour la recherche) ?** *Recommandation : oui.*
   Ceux qui viennent de Fusion retrouvent E, Q, F, H, M, I, L, R, C, D, X ; S est plus utile pour l'esquisse que pour
   une seconde recherche.
6. **La fabrication dans la v1 : le fraisage GRBL seul, ou aussi la résine et le filament ?** *Recommandation : le
   fraisage d'abord* (le code de `fcam.h` se reprend presque tel quel, sur la section exacte d'OCCT), puis la résine
   (`fprint.h`, l'écrivain du `.pm3n` est déjà prouvé à l'octet), puis le filament (il attend toujours un G-code de la
   Kobra X). L'interface du procédé est prévue pour les trois dès le départ.

## 14. Étapes proposées

| Étape | Contenu | Effort |
|---|---|---|
| 0 | UIKit : widgets au-dessus d'une surface (manque 1), surface GL redimensionnable (2), jetons (13) — **fait le 15/11** (§15.7), sauf les glyphes | M |
| 1 | La fenêtre, la vue GL (faces, arêtes, sélection, survol, cube, navigation), les corps, le panneau — **fait le 15/11 avec un rendu par le processeur** (§15.8) | M à L |
| 2 | Formes au geste et sans pointeur, puces de cote (`ui_quantity`), pousser/tirer, booléens auto, frise — **fait le 15/11** (§15.9 ; les puces dans l'app, pas encore `ui_quantity`) | L |
| 3 | Esquisse sur PlaneGCS : tracé, contraintes déduites, glyphes, DDL, conflits ; extrusion, révolution — **fait le 15/11** (§15.10) | L |
| 4 | Congé et chanfrein (chaîne, rayon maximal), coque, perçage, décalage, réseau, symétrie, paramètres nommés — **fait le 15/11** (§15.11), sauf le décalage | L |
| 5 | Import et export STEP, IGES, STL, 3MF ; palette Ctrl+K ; mise en page 800 × 600 | M |
| 6 | Fabrication : fraisage (de `fcam.h`), puis résine, puis filament | M puis M |

S : moins d'un jour, M : quelques jours, L : une à deux semaines.

---

## 15. Le moteur : construction et résultats

### 15.1 Ce qui est porté

| Pièce | Version | Ce qui est gardé | Taille pour Korin x86-64 |
|---|---|---|---|
| **OpenCASCADE (OCCT)** | 8.0.1 (dernière stable, tag `V8_0_1`) | Foundation (TKernel, TKMath), Modeling Data, Modeling Algorithms (primitives, booléens, congés, décalages, formes, hélices, maillage, réparation), Data Exchange **STEP, IGES, STL** ; et ce qu'ils tirent : TKXCAF demande TKV3d et TKService, construits **sans OpenGL ni FreeType**, plus TKCAF/TKLCAF/TKCDF/TKVCAF | 30 bibliothèques statiques, 268 Mo de `.a` (sans symboles de débogage), 39 Mo d'en-têtes |
| **PlaneGCS** | celui de FreeCAD 1.1.4 (`src/Mod/Sketcher/App/planegcs`) | tout le solveur (DogLeg, Levenberg-Marquardt, BFGS, diagnostic par QR) | `libplanegcs.a`, 1,3 Mo |
| **Eigen** | 5.0.1 | en-têtes seuls, copiés à côté de PlaneGCS | — |

Pas de Draw, ni Tcl/Tk, ni visualisation OpenGL d'OCCT, ni Qt. **Boost n'est pas utilisé du tout** : PlaneGCS ne
s'en servait que pour `connected_components` (un graphe des paramètres et des contraintes, pour couper le système en
sous-systèmes indépendants) ; un union-find de soixante lignes le remplace (`tools/cao/planegcs/compat`), avec la
même numérotation des composantes. Le journal de FreeCAD (`Base::Console`) devient un `fprintf` qui ne parle que si
`PLANEGCS_LOG` existe.

**Ce qui a manqué à libposix : rien.** OCCT ne demande qu'une ligne de patch (`tools/cao/occt-korin.patch`) :
`OSD_Path` ne connaissait pas le système (`__armpi__`) et voulait appeler `uname` sans l'en-tête ; Korin y est déclaré
comme un système aux chemins POSIX. Les locales (`newlocale`, `strtod_l`), les threads, `dlopen`, `statvfs`, `uname` de
libposix ont suffi tels quels.

### 15.2 Construire (dans WSL, hors du dépôt)

Tout se fait sous `~/korin-cao` (sources, arbres, résultats) ; le dépôt ne garde que les scripts de `tools/cao/` :

```sh
sh tools/pc/toolchain.sh                 # la chaîne x86_64-onyx-elf, si elle manque (22 min sur 16 cœurs)
ARCH=x86_64 sh tools/posix/build.sh      # libposix du PC -> build/pc/posix
sh tools/cao/fetch.sh                    # OCCT, Eigen, FreeCAD (versions et SHA-256 fixés dans env.sh)
sh tools/cao/build-planegcs.sh           # ~/korin-cao/x86_64/planegcs   (≈ 30 s)
sh tools/cao/build-occt.sh               # ~/korin-cao/x86_64/occt       (27 min en -j6)
sh tools/cao/build-test.sh               # ~/korin-cao/x86_64/test/caotest
sh tools/pc/sysroot_cao.sh               # dans build/pc/sysroot, après sysroot_gcc.sh (paquets : §15.5 bis)
```

`ARCH=aarch64` devant chaque script fait la même chose pour le Pi (libposix de `build/posix` : `NO_TESTS=1 sh
tools/posix/build.sh`, pour ne pas toucher à `prebuilt/`). `HOST=1 sh tools/cao/build-planegcs.sh` construit PlaneGCS
pour Linux, pour un essai rapide hors de Korin.

**Piège : la mémoire.** Sur une machine de 7,8 Go, OCCT en `-j12` a fait tomber WSL (`Wsl/Service/E_UNEXPECTED`, il a
fallu `wsl --shutdown`). `env.sh` plafonne donc à **4** tâches (6 passaient seules, plus de 5,8 Go restant libres, mais WSL est partagé avec d'autres constructions) ; `JOBS=` le change. Une longue construction se lance détachée dans WSL (`setsid nohup … &`), sinon elle meurt avec `wsl.exe`.
`KEEP=1` (`ninja -k 0`) donne toutes les erreurs d'un coup. Second piège : passer `-DCMAKE_CXX_FLAGS` à CMake écrase les
options du fichier de chaîne (`tools/cao/korin.cmake`, qui porte `-specs` de libposix) ; les options d'OCCT passent
par `CMAKE_CXX_FLAGS_RELEASE`.

### 15.3 L'essai, `caotest`

`tools/cao/test/caotest.cpp`, relié en statique (28,6 Mo sans symboles), fait dans l'ordre, chaque étape
chronométrée :

1. **une esquisse PlaneGCS** : un rectangle 60 × 40 dont le coin est fixé à l'origine (4 coïncidences, 2 horizontales,
   2 verticales, 2 cotes) et deux trous de rayon 5 (cotes aux bords, rayons égaux, centres alignés), à partir d'un
   dessin faux exprès ; résolution DogLeg, puis diagnostic : **0 degré de liberté** ; puis une cote contradictoire
   (rayon 6 sur le second trou) que le diagnostic doit désigner : il rend les contraintes 11, 14 et 17 (rayon du
   premier trou, « égal », la nouvelle cote) ;
2. **une boîte** 60 × 40 × 30 et un **congé** r 6 sur une arête droite ;
3. un **booléen** (union avec un cylindre penché de 20°), puis un **congé r 2,5 sur l'arête d'intersection**, une
   ellipse : l'arête « quelconque » que 3DForge ne savait pas arrondir ;
4. un second **booléen** : les deux trous de l'esquisse, extrudés, retirés de la pièce ;
5. **contrôle** (`BRepCheck_Analyzer`) et volume à chaque étape ; **export STEP** (AP214) **relu** : même volume au
   millionième près ; **export IGES** ; **maillage** (flèche 0,05 mm) et **STL** binaire.

Lancer sous QEMU PC (disque NVMe d'essai, telnet ; `tools/cao/pctest.py` reprend `tools/python/pctest.py`) :

```sh
. ~/korin-cloud/env.sh                   # QEMU et l'edk2 avec le shell UEFI (sh tools/cloud/setup.sh qemu)
sh tools/pc/build.sh && sh tools/pc/sysroot.sh
python3 -I tools/cao/pctest.py prep
python3 -I tools/cao/pctest.py run "@300 caotest /tmp" "ls -l /tmp"
```

### 15.4 Résultats (14/11/2026)

**L'essai passe sous QEMU PC** (q35, 4 cœurs, 2 Go), Korin prêt au telnet en 18 à 55 s. Les volumes sont les mêmes,
au centième, que ceux de l'OCCT 7.9.2 d'Ubuntu sur le même programme : 72 000,0 → 71 768,23 → 73 879,71 → 73 954,51 →
69 242,12 mm³ ; 2 642 triangles. Fichiers écrits dans `RAM0:/tmp` : STEP 54 Ko, IGES 47 Ko, STL 132 Ko.

| Étape | Linux natif (i7-11800H) | Korin sous QEMU (TCG) |
|---|---:|---:|
| esquisse : résolution | 0,5 ms | 49 à 91 ms |
| esquisse : cote contradictoire (diagnostic) | 0,4 ms | 12 ms |
| boîte | 1,1 ms | 95 ms |
| congé sur une arête droite | 2,4 ms | 225 ms |
| union avec le cylindre penché | 5,4 ms | 540 ms |
| congé sur l'arête elliptique | 23,6 ms | 1 040 ms |
| retrait des deux trous | 21,9 ms | 870 ms |
| export STEP | 5,0 ms | 470 ms |
| STEP relu | 23,0 ms | 1 360 ms |
| export IGES | 3,9 ms | 146 ms |
| maillage | 29,3 ms | 1 130 ms |
| export STL | 0,3 ms | 16 ms |
| **en tout** | **119 ms** | **6,0 à 6,6 s** |

Lecture : QEMU tourne ici **sans KVM** (émulation TCG), 40 à 100 fois plus lent que le processeur. La colonne
« Linux natif » est le même programme relié à l'OCCT 7.9.2 d'Ubuntu, sur une machine au **même processeur que le
ZBook** (i7-11800H) : c'est l'ordre de grandeur attendu sur le ZBook, où libposix et l'allocateur de Korin peuvent
encore changer les temps. Le premier appel d'OCCT paie l'initialisation de ses tables (la boîte à 95 ms).

**Pas essayé sur le ZBook ni sur le Pi.** Pour le Pi (aarch64), voir §15.5 ; les paquets, §15.5 bis.

### 15.5 Le Pi (aarch64)

Les mêmes scripts avec `ARCH=aarch64` (chaîne `aarch64-onyx-elf-14.2`, libposix de `build/posix`) : **tout se
construit sans un changement de plus** (14-15/11) : PlaneGCS (1,2 Mo), OCCT (30 bibliothèques, 54 min en `-j6`, 321 Mo
avec les en-têtes), `caotest` relié pour le Pi (25,9 Mo, ELF AArch64 statique). **Pas lancé** : ni sous QEMU raspi4b,
ni sur le Pi, et pas de paquets pour le Pi tant qu'il n'a pas tourné.

### 15.5 bis Les paquets du PC (publiés le 15/11)

`tools/pc/sysroot_cao.sh` (après `sysroot_gcc.sh`) pose le moteur dans l'arbre système ; `tools/pkg/packages-pc.ini`
en fait trois paquets, publiés dans `packages/pc` de `main` :

| Paquet | Contenu | Archive |
|---|---|---|
| `occt-dev` (+ `occt-dev-data-1` à `-4`) | `/gcc/include/opencascade`, `/gcc/lib/libTK*.a` | 62 Mo en 5 archives, dans la Release « packages » |
| `planegcs-dev` | `/gcc/include/planegcs`, `/gcc/include/Eigen`, `/gcc/lib/libplanegcs.a` | 1,8 Mo |
| `caotest` | `/bin/caotest` | 11,7 Mo |

Pas de `libocct` ni de `libplanegcs` à côté des `-dev` : les bibliothèques sont statiques (décision du 13/11 : le paquet
binaire ne porte que des `.so`). Les `-dev` ne demandent pas `gcc` (≈ 220 Mo) : ils servent avec lui. Dans Korin :
`g++ -std=gnu++20 -I/gcc/include/opencascade -I/gcc/include prog.cpp -lTKDESTEP … -lTKernel -lplanegcs` (l'ordre des
bibliothèques : `tools/cao/build-test.sh`). Essai sur le ZBook : `pkg update`, `pkg add caotest`, puis `caotest /tmp`
au Terminal.

### 15.6 Suite pour le moteur

* Livrer OCCT et PlaneGCS avec le GCC de l'OS : fait pour le PC le 15/11 (§15.5 bis) ; pour le Pi, après un essai sur
  le Pi.
* Mesurer sur le ZBook (et sur le Pi) : `caotest` copié dans `SD0:/bin`, puis `caotest /tmp` au Terminal.
* Un essai de plus dans l'app : un congé de chaîne tangente et la recherche du rayon maximal (écran d), qui demandent
  plusieurs appels d'OCCT par seconde en tâche de fond.

### 15.7 Étape 0 : ce qui manquait à UIKit (15/11)

Faite le 15/11 : les manques 1, 2 et 13 du §11 (sans les glyphes d'outils).

* **Widgets par-dessus une surface** (manque 1). Elegant a une op de plus, `WS_OP_SURFACE_OVER` (38) : des zones du
  contenu de la fenêtre, 32 au plus, composées **après** sa surface, avec la transparence de l'octet haut (0 opaque,
  255 invisible). Un panneau arrondi garde donc ses coins et son ombre sur la vue 3D. Les zones appartiennent à la
  fenêtre et pas à la surface : elles restent quand GlKit rattache la surface à chaque image, et elles marchent aussi
  bien sur une surface d'UIKit que sur celle de Mesa. UIKit : `ui_surface_overlay (zones, nombre)`, rang 215, en fin
  de `uikit.abi`. Le code : `src/sys/elegant/surface.c` (Elegant en C, celui du Pi aussi), `src/kits/uikit/uk_surface.c`
  ([DESIGN-ELEGANT.md](DESIGN-ELEGANT.md) §17).
* **Une vue GL qui suit la fenêtre** (manque 2). `armgl_window_zone` existait déjà (l'hôte d'Avalonia s'en sert) :
  il manquait l'aide pour les apps. `tools/mesa/armgl/glview.h`, en en-tête seul (rien à reconstruire dans GlKit) :
  `gl_view_open` fait la surface une fois à la taille de l'écran, `gl_view_place` change sa zone à chaque
  `WEV_RESIZE` (une surface neuve seulement si la zone devient plus grande) et règle `glViewport`. Compilé pour
  x86-64 et aarch64, **pas essayé avec un GPU** : la GlKit du PC demande de reconstruire Mesa iris, et QEMU n'a pas de
  GPU.
* **Les jetons** (manque 13) : `TJ_HOVER` (100), `TJ_AXIS_X` (104), `TJ_AXIS_Y` (108), `TJ_AXIS_Z` (112), `TJ_SIZE`
  passe de 104 à 120 ; valeurs du §10.2, une ligne par thème dans `uk_theme.c` (ils ne suivent pas l'accent).
  `check_constants.py` passe. L'UIKit en assembleur (`uikit.asm`, plus servi) ne les remplit pas.

**Essai** : `SD0:/bin/overlaytest [secondes]` (`src/bin/overlaytest`) se vérifie seul. Une surface remplie par le
processeur tient lieu de vue GL (Elegant compose les deux de la même façon). Trois zones passent par-dessus : un
panneau arrondi avec un bouton, un trou, un voile rouge à moitié transparent. Le programme relit l'écran composé
(`ui_screen_grab`) et compare des pixels connus. Il agrandit ensuite la fenêtre (`ui_resize`, `ui_surface_move`) et
déplace le panneau, puis vérifie encore. Il contrôle enfin les quatre jetons. **Sous QEMU PC : PASS, 8 vérifications
sur 8**, deux fois, et la capture montre le panneau aux coins arrondis sur la surface :

```sh
sh tools/pc/build.sh && sh tools/sys/build_c.sh elegant && sh tools/pc/sysroot.sh
python3 -I tools/cao/pctest.py prep && python3 -I tools/cao/pctest.py run "@120 overlaytest"
```

Pas essayé sur le ZBook ni sur le Pi (ni sous QEMU raspi4b). Les paquets ne sont pas republiés : le système `pc`
(Elegant, UIKit) attend la prochaine publication.

### 15.8 Étape 1 : la fenêtre et la vue de Forge (15/11)

L'app s'appelle **Forge** (`src/apps/forge`, `forge.app`), C++ sur l'UIKit de l'OS, construite par `sh tools/pc/apps.sh
forge` contre l'OCCT et le PlaneGCS de `~/korin-cao/x86_64` (sautée et notée s'ils manquent). Trois parties :

* `model.cpp` : le document (corps OpenCASCADE, étapes), la pièce d'exemple, le maillage pour la vue (triangles et
  normales par face, polylignes par arête, coutures cachées), les mesures d'une face, d'une arête ou d'un corps. La
  pièce d'exemple est l'équerre de 3DForge, faite par OpenCASCADE en huit étapes : plaque, congé R 8 sur deux arêtes,
  paroi jointe, congé R 12, raccord R 6, trou Ø 22, deux lamages Ø 9 / Ø 14, oblong. Elle fait 63,99 cm³ (3DForge
  donnait 63,5), avec 24 faces, 60 arêtes et 1 188 triangles.
* `render.cpp` : **la vue rendue par le processeur** dans une surface d'UIKit (orthographique, tampon de profondeur,
  ombrage par sommet, arêtes cachées par la profondeur, grille du plan XY, axes, trièdre, cube de vue), et le choix
  sous le pointeur (une arête à moins de 5 pixels, sinon la face, par un tampon des faces). Sans UIKit : le même code
  se compile pour Linux. **Pas encore le GPU** : la GlKit du PC ne se reconstruit pas sur cette machine et QEMU n'a pas
  de GPU. Le chemin GL (`glview.h`) gardera l'interface de `View`.
* `main.cpp` : la fenêtre des maquettes. En haut, le document, la bascule Conception / Fabrication et la barre
  d'outils, dont les outils sont grisés en attendant les étapes suivantes ; Exporter (grisé). Au milieu, la vue. Le
  panneau des corps (l'œil montre ou cache), le cube de vue avec ses étiquettes, les boutons Maison, Ajuster, Arêtes
  et Grille, et les lettres du trièdre sont dessinés par UIKit et passent par-dessus la surface par
  `ui_surface_overlay`. À droite, le panneau de propriétés : le document, ou la face, l'arête, le corps choisis, avec
  leurs mesures et « solide fermé ». En bas, la frise des étapes avec la barre de reconstruction, puis la barre
  d'état (ce que fait le clic, triangles, temps du rendu). Menus Affichage (vues types Alt+1/2/3/0, Ajuster F6, arêtes,
  grille). La souris suit les décisions du 15/11 : bouton droit pour tourner, milieu pour déplacer, molette vers le
  pointeur, clic pour choisir, Échap pour ne rien choisir ; survol éclairé.

Un piège trouvé en chemin : un `ui_view` a son propre tampon et perd l'octet de transparence. Les zones posées sur la
vue sortaient donc en noir. La vue n'a pas de widget : ce qui est posé dessus est dessiné directement dans le contenu,
et la souris est lue brute dans la boucle.

**Essai sous QEMU PC** : `forge --selftest [secondes]` vérifie la pièce (24 faces, 63,99 cm³, solide valide), le
rendu, le choix d'une face et d'une arête, la vue de dessus et le cube, la rotation et le zoom. **PASS, 7 sur 7.**
Puis de vrais gestes envoyés par une tablette USB : le survol éclaire la paroi ; un clic choisit l'arête du raccord
(Arête droite, 90,00 mm, dans le panneau) ; le bouton droit glissé tourne la vue ; un clic sur DESSUS du cube donne la
vue de dessus. Temps sous QEMU (émulation TCG) : pièce faite en 2,2 à 2,5 s, maillée en 0,4 s, rendu de 944 × 556 en
0,2 à 0,5 s. Sous Linux natif sur le même processeur que le ZBook : pièce en 114 ms, maillage en 13 ms, rendu de
1 200 × 800 en 27 ms.

```sh
sh tools/pc/apps.sh forge && sh tools/pc/sysroot_apps.sh
python3 -I tools/cao/pctest.py prep --mb 500 && python3 -I tools/cao/pctest.py run "@400 forge --selftest"
```

Pas essayé sur le ZBook ni sur le Pi ; essayé en Givre seulement (le thème Onyx Noir ne l'a été que sous Linux, par
le rendu). Forge n'est pas encore publié en paquet. Suite (étape 2) : formes au geste, puces de cote, pousser/tirer,
booléens choisis pour vous, frise modifiable ; le chemin GPU dès qu'une GlKit du PC est construite.

### 15.9 Étape 2 : l'historique, les formes au geste, pousser/tirer (15/11)

* **Un historique paramétrique** (`history.cpp`). Le document est une liste d'opérations : boîte, cylindre,
  perçage, oblong, pousser/tirer, congé, chanfrein, union, soustraction. Les corps sont le résultat de leur rejeu jusqu'à
  la barre de reconstruction. Une opération qui vise des arêtes ou une face les garde par leur **signature** : le
  milieu et la tangente d'une arête, un point et la normale d'une face plane. Elle les retrouve après le rejeu, au
  plus près à 3 mm. Si rien ne correspond, ou si OpenCASCADE refuse, l'étape passe en erreur et le rejeu continue sans
  elle. L'équerre est maintenant ces huit opérations (63,99 cm³). Essai sous Linux : la plaque passée de 8 à 10 mm se
  rejoue sans erreur, les trois congés ont retrouvé leurs arêtes.
* **Les formes au geste**, comme dans 3DForge. Boîte : clic au centre de la base (sur une face plane de la pièce ou
  sur le sol, accroché au mm), on s'éloigne pour la largeur et la profondeur, clic, on monte pour la hauteur, clic.
  Cylindre : centre, diamètre, hauteur. Le **fantôme** est en accent, ou en rouge quand il enlève. L'**opération est
  choisie pour vous** : sur une face, monter joint et creuser enlève ; sur le sol, c'est un nouveau corps. Elle se
  change dans le panneau (Nouveau, Joindre, Enlever, Intersection).
* **Pousser/tirer** (Q) une face plane : on glisse le long de la normale ou on tape la distance ; tirer joint,
  pousser enlève. **Congé** (F) et **chanfrein** (Maj+F) sur les arêtes choisies (Maj+clic pour en ajouter) :
  l'aperçu est la vraie forme calculée, et un rayon trop grand est refusé avant d'être ajouté, avec la raison sur la
  puce et dans la barre d'état. **Union** et **soustraction** de deux corps.
* **Les puces de cote** sur le dessin prennent le clavier pendant le geste. On tape des chiffres, des formules
  (`2*(3+4)`) et des unités (`1 in`, `3cm+5`). Tab passe à la puce suivante, Entrée valide, Échap annule. Une valeur
  tapée est tenue (en gras) : le geste ne la change plus. Elles sont dessinées par l'app au-dessus de la vue
  (`ui_surface_overlay`), en attendant le champ `ui_quantity` d'UIKit (manque 3 du §11).
* **La frise modifiable**. Un clic sur une étape montre ses valeurs dans des champs (Appliquer : tout se rejoue),
  avec Désactiver, Barre ici et Supprimer (aussi Suppr). La **barre de reconstruction se glisse**, Fin la remet au
  bout. Une étape en erreur a un bord rouge et sa raison dans le panneau. La frise défile pour garder la barre en
  vue. **Annuler et rétablir** (Ctrl+Z, Ctrl+Y) gardent l'historique entier. Menus Édition, Créer, Modifier ; touches
  B, Q, F, Maj+F.

**Essai sous QEMU PC** : `forge --selftest` fait **9 vérifications, toutes bonnes**. Elles couvrent le rejeu de
l'exemple ; le geste de hauteur (monter de 6 mm) ; une boîte au geste jointe à la plaque (+1,00 cm³) ; pousser/tirer
avec la valeur tapée `2*2,5` (+2,64 cm³) ; une étape amont modifiée (0 erreur) ; un congé R 30 refusé puis un R 3
accepté ; la barre, annuler et rétablir. Puis de vrais gestes par tablette USB : outil Boîte, clic sur le sol, on
s'éloigne (Largeur 42, Profondeur 9 sur le dessin), clic, on monte (Hauteur 25), clic. On obtient un nouveau corps de
9,45 cm³ ; un clic sur une étape de la frise ouvre ses valeurs. Sous QEMU (TCG), un rejeu complet prend 2,5 à 5 s :
c'est l'émulation. Sous Linux natif, 90 à 150 ms.

`FORGE_TRACE=1 forge` écrit les événements de la souris de la vue (diagnostic). Pas essayé sur le ZBook ni sur le Pi.

**Paquets (15/11)** : **forge 1.0.0** (OpenCASCADE est relié dans l'app : `occt-dev` n'est pas demandé) et **pc
2026.11.7**, publiés sur `main`. Forge demande `abi >= 598` : le nouvel UIKit (`ui_surface_overlay`) et le nouvel
Elegant (`WS_OP_SURFACE_OVER`). Le `pc` publié est l'archive de la 2026.11.6, dont seuls changent `uikit.so` (dans
l'initrd, dont les 188 autres membres gardent leurs octets, et dans `lib/`), `sys/elegant` et `etc/pkg/abi`, plus
`bin/overlaytest`. Le noyau et le reste sont ceux de la 2026.11.6. Sur le ZBook : `pkg update -a` (le système
s'installe au redémarrage), puis `pkg add forge`.

### 15.10 Étape 3 : l'esquisse sur PlaneGCS, l'extrusion, la révolution (15/11)

* **L'esquisse** (`sketch.cpp`). Elle contient des points, des éléments qui les partagent, et des contraintes :
  * deux lignes qui se suivent ont le même point, donc la coïncidence va de soi ;
  * le point 0 est l'origine du plan, fixe ;
  * les éléments sont des lignes, des cercles et des arcs ;
  * les contraintes sont horizontale, verticale, coïncidence, parallèle, perpendiculaire, tangente, égal,
    concentrique, point sur un élément, distance (et ses variantes horizontale et verticale), rayon.

  PlaneGCS (DogLeg) résout après chaque changement. Son diagnostic donne trois choses : les degrés de liberté, les
  contraintes en conflit (désignées par leur nom), et les éléments encore libres. Les contours fermés deviennent des
  faces d'OpenCASCADE (`ShapeAnalysis_FreeBounds`) ; un contour dans un plus grand fait un trou. Une ligne de
  construction sert d'axe de révolution.
* **Le mode esquisse** (S) se pose sur la face plane choisie, sinon sur le plan XY ; la vue se met face au plan et la
  pièce pâlit. Les outils :
  * ligne (L), à la suite, le contour fermé en revenant au premier point ;
  * rectangle (R), deux coins, avec H et V ;
  * cercle (C), le centre puis le rayon ;
  * arc (A), le centre, le début, la fin ;
  * cote (D) : une ligne donne sa longueur, un cercle son diamètre, un arc son rayon, deux points leur distance.

  Un point se pose sur un point existant à moins de 8 pixels, sinon au mm. Une ligne presque horizontale ou verticale
  le devient (contrainte déduite ; Maj pour s'en passer). Les cotes et les glyphes des contraintes (H, V, //, 90°,
  T, =, O, Pt) sont posés sur le dessin. Un clic sur une cote la rouvre ; la valeur se tape, formules et unités
  comprises. Les couleurs disent l'état : **bleu** pour ce qui peut encore bouger, **encre** pour ce qui est fixé,
  **rouge** pour un conflit, pointillé pour la construction (X). Glisser un point montre ce qui bouge encore.
* **Les refus**, sans boîte de dialogue :
  * une contrainte qui contredit les autres est refusée et rien ne bouge ; le panneau et la barre d'état disent
    laquelle elle contredit (« Distance contredit Distance ») ;
  * une contrainte qui réduirait un élément à rien est refusée aussi, comme H sur une verticale, que PlaneGCS
    « résoudrait » en écrasant la ligne.
* **Le panneau de l'esquisse** dit l'état (« 3 degrés de liberté : ce qui est en bleu peut encore bouger » ou
  « ✓ Complètement contrainte ») et liste les dernières contraintes. Dessous : les outils (segmenté), les boutons
  des contraintes, puis Construction, Supprimer, Annuler, Terminer (Entrée).
* **Extrusion** (E) et **révolution** (Créer > Révolution) de l'esquisse choisie dans la frise, sinon de la
  dernière. Pour l'extrusion, on glisse le long de la normale ou on tape la distance. La révolution prend l'angle,
  autour de la ligne de construction. L'opération est choisie pour vous : sur une face d'un corps, sortir joint et
  creuser enlève ; sur un plan, c'est un nouveau corps. Une esquisse se rouvre depuis la frise (« Modifier
  l'esquisse ») ; l'extrusion suit.

**Essais.** Sous Linux, résultats exacts :
* le rectangle passe de 6 à 0 degré de liberté avec H/V et les cotes 40 × 20 ;
* le cercle à 0 degré de liberté avec son centre et son rayon ;
* la cote contradictoire est désignée avec ce qu'elle contredit ;
* l'extrusion donne 7 214,602 mm³ et la révolution 43 982,297 mm³, comme les valeurs calculées.

Sous QEMU PC, `forge --selftest` fait **17 vérifications, toutes bonnes**. Les 8 de l'esquisse :
* le rectangle au geste a ses H et V déduites (4 degrés de liberté) ;
* la cote tapée 50 fait déplacer le rectangle par PlaneGCS (3 degrés de liberté) ;
* une cote contradictoire est refusée, et une horizontale sur une verticale aussi ;
* le cercle ajoute 3 degrés de liberté ;
* l'extrusion de 15 mm donne +14,25 cm³ (rectangle percé), nouveau corps ;
* l'esquisse modifiée (50 → 60) est suivie par l'extrusion (+3,00 cm³).

Puis de vraies touches et de vrais clics, par la tablette USB et le clavier de QEMU : S, R, deux clics, C, deux
clics, Entrée, E, glisser, clic. On obtient un nouveau corps percé, extrudé de 46 mm ; D puis un clic sur une ligne
pose la cote et ouvre sa saisie. Pas essayé sur le ZBook ni sur le Pi.


### 15.11 Étape 4 : congé en chaîne, perçage, coque, réseau, symétrie, paramètres (15/11)

* **Congé et chanfrein.** Un double-clic sur une arête choisit sa chaîne tangente (les arêtes qui se suivent sans
  angle, `tangentChain`). Un rayon refusé donne le plus grand rayon qui passe, cherché par dichotomie
  (`filletMax` : 14 essais, forme valide, arrondi au dixième). Le panneau le dit et le bouton « Utiliser R … »
  le prend.
* **Perçage** (H, Créer > Perçage). Un clic sur une face plane pose le premier trou, au demi-mm. Maj+clic en
  ajoute. Les valeurs : le diamètre et la profondeur (0 : traversant). Le lamage reste dans le modèle (Ø et
  profondeur), modifiable depuis la frise.
* **Coque** (Modifier > Coque). Les faces à ouvrir se cliquent (Maj : plusieurs) ; on tape l'épaisseur.
  `BRepOffsetAPI_MakeThickSolid`, par jointure.
* **Réseau** (Modifier > Réseau). Il répète l'étape choisie dans la frise, sinon la dernière forme, le dernier
  perçage ou la dernière extrusion. Deux modes :
  * linéaire : le nombre et le pas, selon l'axe X, Y ou Z ;
  * circulaire : le nombre et l'angle total (360 : régulier), autour de l'axe choisi par le centre du corps.

  Chaque copie applique l'opération de l'étape répétée.
* **Symétrie** (Modifier > Symétrie). Elle prend le corps choisi, par la face choisie (son plan), sinon par le plan
  X, Y ou Z de l'origine. Deux choix : joindre (la pièce symétrique, fusionnée) ou un nouveau corps.
* **Paramètres nommés.** Ils se posent dans le panneau du document (« épaisseur = 4 », une formule peut en citer
  d'autres) ; « nom = » retire le paramètre. Partout où l'on tape une valeur (puces, champs d'une étape), une
  formule peut citer les paramètres. Une valeur donnée par formule est gardée telle qu'écrite (`Feature::expr`) et
  recalculée à chaque rejeu : changer un paramètre fait suivre toute la pièce. Annuler et rétablir couvrent les
  paramètres.
* **Ce que l'utilisateur a demandé après l'étape 3** :
  * **le clavier pendant une commande** : la première valeur a le clavier dès le début de la commande (curseur dans
    la puce) et Tab passe à la suivante. Les touches vont aux puces avant le widget qui aurait gardé le focus. Un
    clic qui valide prend aussi la valeur tapée sans Entrée ;
  * **la caméra** : la rotation passe de 0,4° à 0,25° par pixel. Une seule image est rendue par lot de
    mouvements reçus (la file vide), au lieu d'une par mouvement : la vue ne court plus derrière la souris ;
  * **le scintillement** : Elegant compose directement depuis le tampon de la surface, à tout moment (le pointeur
    qui passe suffit). Le rendu effaçait puis dessinait dans ce tampon, d'où des images vides. La vue se rend
    maintenant dans un tampon à part, recopié une fois l'image finie. Les zones posées par-dessus ne passent plus
    par le transparent quand elles restent en place : seules celles qui bougent (puces, étiquettes du cube, lettres
    du trièdre) sont effacées ;
  * **la catégorie** : Forge est rangée dans « Création » (`app.txt`).
* Pas fait : le décalage de faces (pousser/tirer le couvre pour une face plane).

**Essais.** Sous Linux, le modèle (`forgehost`) donne des résultats exacts :
* la coque, 7 152,0 mm³ ;
* le trou, π·4 ;
* le réseau linéaire ×5 ; le réseau circulaire, cohérent ;
* la symétrie double le volume ;
* la chaîne fait 12 arêtes ; le rayon maximal, 7,9.

Sous QEMU PC, `forge --selftest` fait **23 vérifications, toutes bonnes**. Les nouvelles :
* le congé de R 30 refusé propose R max 9,9 ;
* le paramètre `ep` dans la plaque (« ep * 2 ») est rejoué quand il change ;
* la coque de 2 mm donne 2 672 mm³, exact ;
* le perçage Ø 4 traversant (Tab pour la profondeur) enlève 125,66 mm³ ;
* le réseau de 3 en enlève 376,99 ;
* la symétrie fait un nouveau corps de même volume.

Puis de vrais gestes par QMP : B, un clic, on s'éloigne. Le curseur est dans « Largeur » sans rien toucher, Tab le
passe à « Profondeur » ; la rotation au bouton droit fait 30° pour 120 px. Pas essayé sur le ZBook ni sur le Pi.
