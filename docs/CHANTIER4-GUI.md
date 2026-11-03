# Chantier 4 — l'interface en espace utilisateur et le bureau Elegant

Plan d'après l'étude d'Onyx (`docs/GUI-USERSPACE-STUDY.md`, `docs/02-KERNEL-INTERNALS.md` §10,
`user/Servers/elegant`, `user/Apps/{menubar,dock,control,theme}`, `user/Kits/uikit/{skin,theme}.cpp`).

## Ce que fait Onyx

* **Elegant est le serveur graphique**, un process utilisateur (`/bin/elegant --serve`) :
  gestionnaire de fenêtres, compositeur, routage des entrées. Le noyau ne garde que des
  **mécanismes** (`ws_ctl`) : un serveur enregistré, l'écran qu'on lui confie, l'anneau des
  entrées brutes, un canal requête/réponse (le noyau donne au serveur le pid de l'appelant), le
  dépôt d'événements dans la file d'un programme, des tampons de pixels partagés. Le
  compositeur noyau a été supprimé (43 Ko de noyau en moins).
* **Le bureau, ce sont des applications UIKit ordinaires**, lancées au démarrage de session :
  le fond d'écran (`voronoy`), la **barre de menus** (30 px en haut : menu Onyx, nom et menus de
  l'application active, zone d'état, son, Wi-Fi, horloge), le **dock** (80 px en bas : tiroirs
  par catégorie, lanceurs, espaces de travail, corbeille, point sous les applications
  ouvertes).
* **Les cadres de fenêtres sont dessinés par l'application** (UIKit), le serveur ne fait que les
  copier et les toucher : barre de titre 28 px, bord 4 px, coins arrondis 8 px ; style `cde`
  (boutons encadrés) ou `milk` (perles de couleur).
* **Le panneau de configuration** liste des applets (fichiers `.lnk`) ; chacun est un process
  qui dessine dans une surface partagée : Thème, Mode, Affichage, Panneau (dock), Son,
  Clavier et souris, Langue et région, Wi-Fi, Paquets... Réglages en fichiers `clé = valeur`.
* **Le thème** : `etc/theme.txt` (`clé = 0xRRGGBB` : fenêtre, accent, inactif, barre de menus,
  dock, style), lu par chaque application au démarrage ; nuances calculées d'une couleur de
  base.

## Ce que je propose pour ARMPI

Même découpage, en assembleur, en plusieurs étapes vérifiables (chacune testée dans QEMU, sur
le PocketTerm, et assemblée sous ARMPI).

### Étape 4.1 — mécanismes du noyau

* **Pixels partagés** : objets de mémoire partagée comptés (créer, mapper chez un autre
  process, libérer au dernier), sur le modèle des pages partagées des kits.
* **Canal du serveur** : `ws_register` (un seul serveur), `ws_call` (requête synchrone d'un
  programme, avec son pid), `ws_next` / `ws_reply` (côté serveur), `ws_post` (événement dans la
  file d'un programme : la file actuelle de `win_event` est réutilisée).
* **Écran et entrées** : le serveur reçoit le framebuffer mappé dans son espace et lit la file
  d'entrées brutes du noyau (clavier, souris, tactile).
* **Fin d'un process** : le noyau prévient le serveur (fermeture de ses fenêtres).
* Repli : si le serveur ne démarre pas ou meurt, la console texte du noyau reprend l'écran.

### Étape 4.2 — le serveur graphique en process (fait)

* Le contenu de `gui.asm` (compositeur, fenêtres, routage des entrées, curseur) devient un
  programme : `SD0:/sys/elegant.elf`. UIKit change de transport (requêtes au serveur au lieu
  d'appels système) ; **les applications ne changent pas** (elles ne passent que par UIKit,
  sauf le titre de fenêtre dans l'éditeur et le navigateur, qui passera par UIKit).
* Le noyau perd son gestionnaire de fenêtres et sa barre des tâches.

Réalisé :

* `src/sys/elegant/elegant.asm` → `SD0:/sys/elegant`, lancé par le noyau (`ws_start`) sauf avec
  `no_gui`. Cœur commun `elegant/core.inc`, interface d'un mode `elegant/mode.inc` (table
  `POL_*` : nom, hauteur réservée, fond, avant-plan, clic, survol, tic, fenêtres changées),
  mode `elegant/desktop.inc` (« desktop », le bureau d'avant). `shell=` de
  `SD0:/etc/system.ini` choisit le mode ; `console` laisse la console texte.
* Requêtes `WS_OP_*` (créer, zone modifiée, fermer, titre, écran) ; la surface est un tampon
  partagé créé par Elegant et autorisé au programme ; événements par `ws_post` / `ev_wait`
  (`+28` : fenêtre). Les touches arrivent traduites par le noyau (`ws_input`, caractère en
  `+4`) : `keyb=` et l'appel 69 restent valables.
* UIKit 16 : même interface pour les programmes, plus `ui_title`, `ui_close`, `ui_screen`
  (l'éditeur et le navigateur passent par `ui_title`). `ui_window` attend le serveur jusqu'à
  10 s (programmes de `startup.txt` lancés avant lui).
* Noyau : `gui.asm` supprimé (le rastériseur TrueType n'y est plus), appels 60-65 retirés,
  141 → 129 Ko. Si le serveur s'arrête, la console texte reprend l'écran.
* Vérifié dans QEMU (bureau, menu, clavier, titres, déplacement, fermeture, dessin, terminal)
  et sous ARMPI : les deux noyaux, `elegant.elf` et `uikit.so` réassemblés à l'identique.

### Étape 4.3 — le bureau Elegant (fait)

* Cadres dessinés par UIKit, styles `cde` et `milk`, couleurs du thème.
* `SD0:/etc/theme.txt` (même clés qu'Onyx), lu par UIKit et le serveur.
* Programmes de session (`SD0:/etc/session`) : **fond d'écran** (dégradé / Voronoï),
  **barre de menus** (menu ARMPI, application active, horloge, réseau), **dock** (lanceurs,
  applications ouvertes).

Réalisé (design : `docs/DESIGN-ELEGANT.md`, maquettes `docs/maquettes/`) :

* **Protocole** : rôles de fenêtre `WF_*` (sans cadre, fond, dessus, sans focus, hors liste, à
  l'écoute de la liste, capture, réserve d'un bord, transparence), `WS_OP_FRAME`, `WS_OP_LIST`,
  `WS_OP_RAISE`, `WS_OP_MESURE`, événement `WEV_WINLIST`. Elegant ne dessine plus de texte :
  il compose (les bandes du cadre avec transparence, puis le contenu) et route les entrées ;
  boutons fermer et réduire, fenêtres réduites, bords réservés.
* **UIKit** : cadres dessinés par le programme (deux copies, styles `plat`, `cde`, `milk`),
  thème `SD0:/etc/theme.txt` (clés d'Onyx ; thèmes Givre — défaut — et Onyx Noir, jetons
  `TJ_*`), échelles L / M / P, icônes dessinées (`ui_icone`), menus à ombre, icônes,
  séparateurs et sous-menus (`ui_popup_ex`), liste des fenêtres.
* **Session** (`SD0:/etc/session`) : `voronoy` (fond Voronoï aux couleurs du thème),
  `menubar` (menu ARMPI par catégories, application active, réseau, heure), `dock`
  (structure du dock d'Onyx : tiroirs à languette, lanceurs, pupitre, corbeille ;
  `SD0:/etc/dock.ini` ; catégories et libellés : les manifestes `app.txt` des paquets).
* **Coût des ombres** : `SD0:/bin/bench` (QEMU 1024x768 : 12,7 ms par image avec
  transparences, 11,3 sans). À mesurer sur le Pi 4 ; trop lourd : pas d'ombre, un contour
  opaque de 1 px (demande de l'utilisateur).
* Reste du design pour plus tard (avec l'adaptation des applications) : contrôles des
  applications aux couleurs du thème, focus clavier, bulles du dock, espaces de travail.

### Étape 4.4 — le panneau de configuration

* **Fait.** `control` (« Réglages ») : **une seule fenêtre**, comme le control d'Onyx — la liste
  des applets à gauche (icône, nom), l'applet choisi dans le volet à droite (titre, réglages,
  message, « Appliquer »). `control <applet>` ouvre directement `theme`, `clavier`, `ecran` ou
  `dockconf` ; ↑ ↓ changent d'applet, Entrée applique, Échap ferme.
* Les applets sont des modules de `control` (`src/apps/control/applets/*.inc`) sur le cadre
  `inc/applet.inc` (lignes : titres, choix, cases, notes ; ou dessin libre) :
  * **Thème** : les 8 thèmes, avec un **aperçu** (un petit bureau aux couleurs du thème coché :
    UIKit `ui_theme_texte`, version 32) ; Appliquer écrit `SD0:/etc/theme.txt`, relance fond,
    barre de menus et dock, et le panneau se remet aux nouvelles couleurs (cadre compris) ;
  * **Clavier et langue** : les dispositions de `SD0:/res/keyb`, appliquées aussitôt (`keymap`)
    et gardées (`keyb=` dans `SD0:/etc/options.txt`) ; langue : français ;
  * **Écran** : la résolution (`ecran=LxH` dans `options.txt`, de 640x480 à 1920x1080), lue
    par le noyau au démarrage ;
  * **Dock**, le dockconf d'Onyx en widgets de UIKit, même disposition (la fenêtre du panneau a
    le volet d'Onyx, 700 x 470, si l'écran le permet) : les tiroirs (ordre ^ v, Masqué, application
    principale), les bureaux (1 à 6, leurs noms), les lanceurs (6 au plus), et dans l'en-tête la
    taille et la corbeille propres à ARMPI ; Appliquer réécrit `dock.ini` et relance le dock,
    Rétablir relit le fichier. Une application installée apporte sa catégorie : un tiroir de plus.
* Trop de lignes pour le volet (petit écran) : elles **défilent** — molette, ou un toucher sur
  la barre à droite (le PocketTerm n'a pas de molette).
* Le « ⚙ » du pupitre du dock ouvre le panneau.
* Plus tard : Wi-Fi (chantier 5), Son (chantier 6).

## Décisions (28/10/2026)

1. **PocketTerm** : Elegant réduit (dock rétréci sur un écran étroit, comme Onyx). D'autres
   modes viendront plus tard (PocketUI, console de jeu, comme Onyx) : l'architecture le
   prévoit dès maintenant — **un cœur de serveur commun** (fenêtres, compositeur, routage) et
   des **réglages par mode** (crochets, comme `struct ws_policy` d'Onyx), le mode étant choisi
   par `shell=` dans `SD0:/etc/system.ini` (`desktop` → Elegant).
2. **Noms d'Onyx** : `elegant`, `menubar`, `dock`, `control`, `SD0:/etc/theme.txt`.
3. **Ordre** : 4.1 (noyau) → 4.2 (serveur en espace utilisateur, bureau actuel), validés sur le
   PocketTerm, puis 4.3 (bureau Elegant) et 4.4 (panneau de configuration).
