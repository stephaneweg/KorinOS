# Mode poche : décisions et contrat entre les pièces

Maquettes validées par l'utilisateur le 5/11/2026 : https://claude.ai/artifact/1qG21UencFn3UUzGWxPx6y
(étude : `docs/DESIGN-ETUDES-ONYX.md`, section 4). Ce fichier fixe ce que les pièces du mode poche
partagent, pour qu'elles se construisent séparément puis s'emboîtent.

## Décisions de l'utilisateur

1. Accueil : **variante A**, le tableau du jour : grande heure et date, recherche, trois cartes « Aujourd'hui »
   (prochain rendez-vous de l'agenda, météo de PocketQI, musique en cours avec pause), rangée « Récentes »,
   « Toutes les apps » qui ouvre le lanceur à rail (celui du panel) en second niveau, bande « Ouvertes » de 64 px
   en bas (pastilles icône 24 + nom + ×, sans vignette).
2. Barre du haut en poche : **36 px**.
3. Densité tactile : contrôles et lignes de **36 px** (troisième ligne de `uk_mt_tab`) ; les éléments de la
   coquille sont plus grands (tuiles 60, pastilles 44, cellules d'icônes 96).
4. **Boutons d'abord**, pas de gestes de bord (un glissé du bord droit pourra venir plus tard, désactivable).
5. Notifications **dans le volet des réglages rapides** ; toast pleine largeur sous la barre, 4 s ; point d'accent
   sur l'heure quand il y en a de non lues.
6. **Vignettes** dans le commutateur (copies réduites une fois à l'ouverture), pas dans la bande de l'accueil.
7. Fenêtres de taille fixe (Calculatrice, etc.) : **plein écran, contrôles adaptatifs** (pas de carte centrée).
   Les dialogues `WF_MODAL` gardent leur taille, centrés sur un voile `TEXTE1` à 30 %.
8. Glyphe ‒ du cadre : **ramène à l'accueil** ; × ferme ; agrandir disparaît ; `FRAME_CAP_W` inchangé.
9. Changer de mode : **relancer la session** d'abord (apps fermées) ; à chaud plus tard. Sortie de secours au
   démarrage (touche Maj maintenue, ou `SD0:/options.txt`) qui force le bureau.
10. Résolution : **800 x 600** gardée.

## Contrat

* Le mode : `shell = poche` (ou `desktop`, défaut) dans `SD0:/etc/system.ini`, lu au démarrage par Elegant.
  En poche, la session est `SD0:/etc/session.poche` au lieu de `SD0:/etc/session`.
* Elegant publie sur le bus `korin.shell.mode` = `desktop` ou `poche` (les programmes peuvent aussi lire
  `system.ini`).
* Commandes de la coquille, publiées sur le bus par Elegant (touches système) ou par la barre de menus (boutons) ;
  la valeur est libre (`1`) sauf mention :
  * `korin.shell.home` : montrer l'accueil (Super, losange ◆, glyphe ‒). Un second Super revient à l'app.
  * `korin.shell.switch` : ouvrir le commutateur (Alt+Tab, Super+Tab, compteur ‹n›).
  * `korin.shell.quick` : ouvrir ou fermer les réglages rapides (Super+N, l'heure).
  * `korin.shell.sleep` : mettre en repos (Super+L, touche Sleep, tuile Repos) — l'écran de repos viendra de
    l'étude 3 ; en attendant, publié seulement.
* Programmes de la coquille (session poche) : `SD0:/sys/home` (accueil), `SD0:/sys/quick` (réglages rapides),
  le commutateur dans Elegant ou `SD0:/sys/switcher` (au choix de qui le fait), la barre de menus
  `SD0:/sys/menubar --poche`, `notifyd`.
* Drapeau de fenêtre **`WF_HOME = 16384`** (dans `src/inc/ws_ext.inc`) : la fenêtre de l'accueil. En poche,
  Elegant la tient sous toutes les fenêtres normales, en plein écran sous la barre, toujours là ; elle
  n'apparaît pas dans le compteur ni dans le commutateur.
* Les numéros d'ops `WS_OP_*` nouveaux partent de 37, en fin de liste.

## L'accueil : `SD0:/sys/home` (`src/sys/home`)

Fenêtre `WF_FRAMELESS | WF_NOLIST | WF_LISTEN | WF_HOME`, de toute la largeur, entre la barre du haut et la
réserve du bas (`ui_screen`), replacée quand elles changent. Ligne de la session poche : `SD0:/sys/home`.
Un second `home` lancé publie `korin.shell.home` et s'en va (service nommé `home`).

* **`korin.shell.home`** : l'accueil fait `WS_OP_RAISE` sur sa fenêtre et retient l'app qui était devant ; s'il
  est déjà devant toutes les fenêtres normales visibles, il remet cette app devant. **Elegant** doit donc
  laisser `WS_OP_RAISE` amener la fenêtre `WF_HOME` devant les apps (jusqu'à ce qu'une autre fenêtre soit
  montée) ; s'il préfère traiter `korin.shell.home` lui-même, l'accueil doit cesser de le faire (une seule des
  deux pièces bascule).
* **Récentes** : `SD0:/etc/recent.txt`, un nom de paquet par ligne, la plus récente d'abord (12 au plus ;
  `src/inc/recent.inc`). Tenues par `src/inc/apprun.inc` : une app qui démarre passe devant (le panel sur le
  bureau, l'accueil en poche). Complétées par les raccourcis du panel (`panel.ini`), puis par nom.
* **Ouvertes** : `WS_OP_WIN_LIST` (`ui_win_details`) relu à chaque `WEV_WINLIST` : une pastille par programme
  qui a une fenêtre normale (ni `WF_TOP`, `WF_BACK`, `WF_NOLIST`, `WF_GRAB`), dans l'ordre d'arrivée ; l'app
  de la plus avant des fenêtres visibles a l'anneau ; × et Suppr envoient `WS_OP_WIN_CLOSE` à ses fenêtres.
* **Toutes les apps** : le lanceur du panel (`src/sys/panel/launcher.inc`, partagé : le programme donne
  `LA_WFLAGS`, l'accueil sans `WF_CURTAIN`), avec l'onglet « Récentes » en tête du rail.
* **Agenda** : `SD0:/apps/calendar.app/agenda.txt` (celui de l'applet du bureau), relu chaque minute.
* **Météo** : PocketQI **ne garde aucune météo** aujourd'hui (Open-Meteo à chaque question) ; l'accueil ne fait
  aucune requête. Il lit `SD0:/home/pocketqi/weather.txt` s'il existe, et montre « Météo indisponible »
  sinon. À faire dans PocketQI (`tools.h`, `WeatherTool`) : écrire ce fichier après chaque relevé de la ville
  réglée, clés `temp = 12`, `sky = nuageux`, `city = Lyon`, `forecast = demain 14°, pluie`, `time = <secondes
  UTC>` (plus de 12 h : indisponible).

## La musique en cours : `korin.media.now`, `korin.media.control`

| Sujet | Éditeur | Valeur |
|---|---|---|
| `korin.media.now` (gardé) | le lecteur, à chaque changement (lecture, pause, reprise, recherche, arrêt, morceau suivant) ; retiré à sa sortie s'il est le sien | `<état> <position ms> <durée ms> <instant ms> <app> <titre>` : état `playing`, `paused` ou `stopped` ; instant = `SYS_TIME_MS` de la position ; durée 0 : inconnue ; app = nom du paquet (`music`) |
| `korin.media.control` | l'accueil (bouton de la carte), tout programme | `<app> <commande>` : `toggle`, `play`, `pause`, `next`, `stop` ; seul le lecteur nommé obéit |

Le lecteur ne republie pas sa position chaque seconde : l'abonné l'avance lui-même depuis l'instant tant que
l'état est `playing`. Musique (`src/apps/music`) publie et obéit ; Media Player (C++, `src/apps/media`) reste
à brancher (BusKit par `buskit_abi.h`, mêmes valeurs, app `media`).

## Elegant, la barre de menus, le commutateur

Détail dans `docs/DESIGN-ELEGANT.md` section 18. Ce que les autres pièces doivent savoir :

* **Mode** : `shell=poche` (espaces permis autour de `=`) ; Elegant publie `korin.shell.mode` (gardé) dès que
  busd répond, en bureau comme en poche. Session : `SD0:/etc/session.poche`, lancée 2 s après la prise de
  l'écran.
* **Sortie de secours** : Maj pendant ces 2 s, ou une ligne `shell=desktop` dans `SD0:/etc/options.txt` (ou
  `SD0:/options.txt`), qui l'emporte sur `system.ini` : la table et la session du bureau.
* **`korin.shell.home`** : Elegant le **publie seulement** (Super seul, glyphe ‒) ; l'accueil fait l'aller et
  le retour par `WS_OP_RAISE`. `WS_OP_RAISE` de la fenêtre `WF_HOME` cache toutes les apps jusqu'à ce qu'une
  app soit créée ou ramenée (`WS_OP_RAISE` d'une app). Sans programme d'accueil, Elegant traite lui-même
  le sujet (les apps cachées, le fond uni) ; sans busd, directement.
* **`korin.shell.switch`** : valeurs `1` (compteur de la barre, Super+Tab), `alt` (Alt+Tab), `alt-prev`
  (Maj+Alt+Tab), `commit` (Alt relâché après Alt+Tab : passer à l'app choisie). Commutateur : `SD0:/sys/switcher`.
* **`korin.shell.quick`** : Super+N et l'heure de la barre. **`korin.shell.sleep`** : Super+L.
* **`korin.notify.unread`** (gardé ; à publier par notifyd) : le nombre de notifications non lues, en décimal
  (`0` ou sujet retiré : aucune). La barre de menus poche met un point d'accent à droite de l'heure quand il
  est > 0. Elle lit les 12 premiers octets de la valeur (dans l'évènement).
* **Fenêtres** : une fenêtre à cadre est agrandie à la zone sous la barre (`WEV_RESIZE`, aussi pour une
  fenêtre de taille fixe ; une fenêtre qui garde une taille plus petite est centrée) ; `WF_MODAL` garde sa
  taille, centrée sur un voile ; `WS_OP_WIN_PLACE`, `WIN_MOVE`, `WIN_DRAG` sont sans effet sur une app ou sur
  la fenêtre `WF_HOME` ; les fenêtres sans cadre restent où elles sont (le volet des réglages rapides se place
  lui-même). Les fenêtres `WF_BACK` sont cachées tant qu'un accueil existe.
* **Compteur** de la barre : les programmes distincts de `WS_OP_LIST` (qui ne liste plus les fenêtres
  `WF_HOME`).
* **Vignettes** : `WS_OP_WIN_THUMB` = 37 (`src/inc/ws_ext.inc`).

## Les réglages rapides : `SD0:/sys/quick` (`src/sys/quick`)

Ligne de la session poche : `SD0:/sys/quick`. Il reste là, volet fermé, et l'ouvre ou le ferme à chaque
`korin.shell.quick` (un second `quick` fait de même : service `quick`, op 1 ; `quick open` ouvre dès le
départ). Fenêtre `WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_ALPHA` de 340 px, placée par lui-même à 6 px sous la
barre et du bord droit, au plus jusqu'en bas de l'écran. Il se ferme sur Échap, quand une autre fenêtre prend
le clavier (un `korin.shell.quick` dans les 400 ms qui suivent ne le rouvre pas : c'est le toucher sur l'heure),
ou après une action qui ouvre autre chose.

* Tuiles : **Wi-Fi** (rond : `wifi_leave`, ou rejoindre le premier réseau connu ; tuile : la liste de
  `src/inc/wifilist.inc`, celle de wifimenu), **Ne pas déranger** (`korin.notify.dnd`), **Son** (rond : muet ;
  tuile : volume, muet, sortie principale de `src/inc/soundlist.inc`, celle de soundmenu), **Repos** (publie
  `korin.shell.sleep` et se ferme).
* Notifications : l'historique de notifyd ; ouvrir le volet les marque lues. Une carte touchée ramène la
  fenêtre de son app ou la lance (si c'est un paquet de `SD0:/apps`) ; glissée à droite de 80 px, son × ou
  Suppr l'efface ; « Tout effacer ».
* Clavier : flèches (tuiles en grille, ← → sur le curseur = volume), Tab = ↓, Espace bascule, Entrée ouvre le
  détail ou l'app, Suppr, Échap (retour du détail, puis fermer).

## Notifications : notifyd

* Service `notify` (`src/inc/notify.inc`) : op 1 = titre, 0, texte, 0 [, app, 0] (app : le paquet qui
  l'envoie, `notify -a <app>` ; sinon le nom du process appelant, et l'app dite n'est gardée que si
  `SD0:/apps/<app>.app/main` existe) ; op 2 liste (a = rang, la plus récente d'abord → nombre, entrée `NH_*`
  dans les données) ; op 3 efface (a = id) ; op 4 tout effacer ; op 5 toutes lues. 32 gardées.
* **`korin.notify.unread`** (gardé) : publié par notifyd au démarrage (`0`) et à chaque changement.
* **`korin.notify.dnd`** (gardé) : `1` Ne pas déranger, `0` sinon ; publié par quick, lu par notifyd à chaque
  notification : plus de bulle, l'historique reste. Pas gardé au redémarrage.
* En poche (`korin.shell.mode` = `poche`, sinon `shell = poche` de `system.ini`), la bulle prend toute la
  largeur sous la barre (6 px de marge), 64 px, l'icône de l'app ; 4 s comme sur le bureau.

## Le choix du mode

Réglages > Écran, section **Interface** : cartes Bureau / Poche, « Taille des contrôles » Normale / Tactile
(`density = normal | touch` dans `SD0:/etc/theme.txt` ; « Appliquer » l'écrit et republie `korin.theme`),
« Appliquer et relancer » écrit `shell=desktop|poche` dans `SD0:/etc/system.ini` puis redémarre (init2,
`INIT_OP_REBOOT`). L'applet Thème, qui réécrit `theme.txt`, garde la ligne `density`. Setup, étape Écran et
thème : mêmes choix (Bureau | Poche, Normale | Tactile), écrits aussitôt ; « Démarrer » redémarre.
