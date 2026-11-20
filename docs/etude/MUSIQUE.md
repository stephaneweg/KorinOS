# Étude : Spotify et Jamendo dans Korin

Demandée le 18/11/2026. **Fait le 18/11** (construit, pas encore essayé : il faut un client_id de chaque service) :
1. UIKit et Elegant : `ui_window_place`, `ui_window_top`, `ui_window_pos` (WS_OP_WIN_TOP) ;
2. le mini-lecteur du Media Player, en carte en bas à droite, au-dessus ; fermer pendant la lecture réduit ;
3. **Jamendo** (`src/apps/jamendo`) : populaires, nouveautés, recherche, radios par genre, téléchargement ;
4. **Spotify** : librespot 0.7.1 construit pour `x86_64-unknown-korin` en bibliothèque C (`src/lib/korinspot`,
   `tools/spotify/build.sh` : sept caisses patchées, `tools/spotify/patches`, mio par poll(2)) ; l'app
   (`src/apps/spotify`) : OAuth par oauthd (client_id de l'utilisateur, retour `http://127.0.0.1:8898/`), API web
   (titres aimés, playlists, recherche), son par un flux AudioKit ; **Spotify Connect** (Korin, appareil du compte,
   par le nuage de Spotify : \`ks_connect\`). **Reste** : les pochettes, les
   touches multimédia et les pièces communes dans les kits (§3) ; korinspot pour le Pi.

**Spotify refait le 20/11 : le lecteur web.** Depuis que Jet lit les contenus protégés par Widevine
(docs/etude/WIDEVINE.md), le lecteur web de Spotify joue dans Jet (le ZBook, le 20/11). Décisions de l'utilisateur
(20/11) : l'app Spotify devient le **lecteur web dans sa propre fenêtre**, sur le moteur de Jet ; l'interface UIKit,
librespot (`src/lib/korinspot`, `tools/spotify`) et Spotify Connect sont **retirés** du dépôt (ils sont dans l'historique
de git, jusqu’à b71cf646) ; la session de l'app est **à part** de celle de Jet. Pas de Client ID : c'est le client web
de Spotify, connecté avec le compte de l'utilisateur (le Web Playback SDK n'était pas une voie : il exige le jeton
d'une app enregistrée, portée `streaming`, et Premium).

* `src/apps/spotify/main.c` : le **lanceur commun des apps web** ; il prend le nom de son dossier et devient Jet par
  `execv` (le même process : le panneau voit l'app Spotify) : `/apps/jet.app/main --app=spotify`. Le reste est dans
  `webapp.txt` de l'app (`title`, `url`, `sites` : les domaines dont les nouvelles fenêtres restent dans l'app, les
  autres vont dans Jet ; `player` : un lecteur connu, pour la carte). Netflix et Disney+ (demandés le 20/11, à faire
  quand la lenteur de Jet sera résolue) : un dossier `src/apps/<nom>` avec ce même `main.c`, un `webapp.txt` et un
  `app.txt`, rien de plus.
* Le nom des process : après `execv`, le noyau nomme un process d'après son fichier (`jet`) et le panneau aurait
  montré Jet. Le lanceur appelle `korin_exec_name (nom)` de libposix (proc.c : `EXP_NAME` de `SYS_EXEC`) ; les process
  web et réseau de WebKit sont lancés par le lanceur (`engine_set_helper_path`), qui redevient Jet de la même façon :
  tous s'appellent `spotify`, le panneau montre l'app et ramène sa fenêtre.
* `src/extra/onyxports/jet/webapp.cpp` : le **mode app** de Jet. Une fenêtre sans barre d'adresse ni onglets ; les
  données des sites dans `HOME/<nom>/webkit` (`engine_set_data_name`) ; le module Widevine proposé au premier
  lancement s'il manque (cdmupdate, le même que la case des réglages de Jet) ; l'UA Chrome sous Linux sur spotify.com
  (`widevine_site` d'engine_webkit.cpp, comme dans Jet) ; les autres fenêtres (`target=_blank`, `window.open`) dans
  Jet. Un lecteur connu (table `g_players`) : son état lu dans la page chaque seconde (`engine_run_script` : Media
  Session si WebKit l'a, sinon les `data-testid` de la barre de lecture de Spotify), ses boutons pressés par un
  script. Fermer pendant la lecture réduit en **carte** en bas à droite, au-dessus (`ui_window_place`,
  `ui_window_top`) ; la page reste derrière, la carte passe par-dessus sa surface (`ui_surface_overlay`).
* Fragile : les `data-testid` de Spotify peuvent changer ; la carte montre alors le titre de la page, ses boutons ne
  font plus rien, la lecture dans la fenêtre continue de marcher.
* Essai : `tools/linux/test_spotify_app.sh` (QEMU PC : cdmupdate, puis l'app ; sans compte). L'identification à
  Spotify est faite par l'utilisateur, jamais par un essai.

Ce qui suit (§ 1) est l'étude du 18/11 (voie librespot), gardée pour mémoire.

Le reste de l'étude : L'utilisateur a un compte **Spotify Famille** (Premium) et veut une app
Korin pour Spotify, et un client Jamendo. Ce qui suit n'a pas été revérifié en ligne : à confirmer au début du
chantier (protocoles et conditions des deux services changent).

## 1. Spotify

### Pourquoi pas le lecteur web dans Jet

Le lecteur web exige **Widevine** (DRM fermé de Google, fourni seulement à Chrome, Firefox, Edge, Safari). Notre
WebKit ne l'a pas et ne peut pas l'avoir : la page s'ouvre, rien ne joue.

### La base : librespot

**librespot** (Rust, licence MIT) est le client libre du protocole de Spotify : connexion (OAuth), clés audio,
flux Ogg Vorbis chiffrés (AES-CTR), Spotify Connect. Il sert à spotifyd, Raspotify, ncspot. Il demande un compte
**Premium** (Famille l'est). Toléré, pas officiel : un changement du protocole peut le casser par moments (il suit
en général en quelques jours).

| Voie | Travail | Jugement |
|---|---|---|
| **A. Réécrire librespot en C** | ~20 000 lignes de Rust : protocole « access point » (Shannon, Diffie-Hellman, protobuf), Mercury, clés audio, AES-CTR, Vorbis, Connect | gros, et à refaire à chaque changement de Spotify sans profiter des correctifs de librespot : **déconseillé** |
| **B. librespot construit pour Korin** (cible Rust croisée `x86_64-unknown-korin`, comme `rg` et les modules Rust de Python), exposé en **bibliothèque C** (petite façade `extern "C"` : connexion, charger un titre, lecture, pause, position, volume, événements) | la façade et le son (backend vers AudioKit ou `sound`) | **recommandé** : le protocole reste celui de librespot, mis à jour avec lui |
| **C. librespot en service** (`spotifyd` à la Korin) piloté par l'app | comme B, plus un canal entre l'app et le service | utile pour que la musique continue app fermée ; peut venir après B |

### L'app (C ou C++ sur UIKit)

* **Connexion** : OAuth de Spotify par `oauthd` (fournisseur `[spotify]`, `client_id` d'une app créée par
  l'utilisateur sur le tableau de bord des développeurs de Spotify, comme pour Google et GitHub) ; le jeton sert à
  librespot et à l'API web ; rangé dans le coffre.
* **Parcourir** par l'**API web** de Spotify (HTTPS, JSON) : recherche, mes playlists, titres aimés, albums,
  artistes, pochettes. Les applications en mode développement sont limitées (quelques utilisateurs déclarés, des
  points d'accès retirés en 2024 comme les recommandations) : suffisant pour un usage personnel, à vérifier.
* **Jouer** par librespot : file d'attente, lecture aléatoire, répétition, barre de position, volume.
* **Spotify Connect** : Korin visible comme appareil dans l'app Spotify du téléphone (« jouer sur le ZBook »), et
  l'inverse (piloter le téléphone) si l'API le permet.
* Intégration : touches multimédia du clavier, notification du titre, applet de la barre (titre, pause), aide.
  Thème par `ui_tokens()`.

## 2. Jamendo

Musique libre (Creative Commons). **API publique** (v3.0) : un `client_id` gratuit sur le portail des
développeurs de Jamendo ; recherche de titres, albums, artistes, radios, playlists ; **flux MP3 directs** (et
téléchargement selon la licence). Pas de DRM, pas de Premium.

* App native (C sur UIKit) : recherche, genres, radios de Jamendo, pochettes, lecture par MediaKit/FFmpeg (MP3),
  file d'attente, téléchargement dans `HOME:/Musique` avec la licence en étiquette ; le `client_id` dans le coffre.
* Petit : quelques jours. Peut servir de banc d'essai pour l'interface commune (§3) avant Spotify.

## 3. Ce qui sert aux deux (dans les kits)

Lecture en flux, file d'attente, touches multimédia, applet « en cours de lecture » et notification : dans
MediaKit / AudioKit et UIKit, pas dans chaque app (la règle des kits). Le Media Player existant peut s'en servir
aussi.

## 4. Ordre (décidé le 18/11/2026)

1. **UIKit et Elegant** : une app peut déplacer et dimensionner sa fenêtre (la carte en bas à droite de l'écran,
   au-dessus du panneau), et la garder **au-dessus des autres** ; à la fin des tables `.abi`.
2. **Media Player** (`src/apps/media`) : le mini-lecteur d'Onyx (carte en bas à droite, au-dessus), et fermer la
   fenêtre pendant la lecture réduit en carte au lieu de quitter.
3. **Jamendo** : l'app, et les pièces communes du §3 dans les kits.
4. **Spotify** : librespot pour Korin (voie B), puis l'app, puis Spotify Connect.

## 5. Questions

1. ~~Un seul lecteur ou deux apps ?~~ **Deux apps** (décision du 18/11) : une pour Spotify, une pour Jamendo ;
   les pièces communes (§3) restent dans les kits.
2. ~~La musique continue-t-elle app fermée ?~~ **Oui, comme le mini-lecteur du Media Player d'Onyx** (décision du
   18/11, vérifié dans le dépôt d'Onyx : `user/Apps/media/main.cpp`, `enter_mini`, `MiniView` ; docs/media/README.md
   d'Onyx). Dans Onyx, la fenêtre se **réduit en une carte en bas à droite de l'écran, au-dessus des autres
   fenêtres** : pochette, titre, précédent / lecture / suivant, la position ; ses boutons : la fenêtre en grand,
   fermer (la musique s'arrête). Onyx le faisait par un bouton, la fermeture arrêtant la musique ; **pour Korin,
   fermer la fenêtre pendant la lecture fait la même chose** : réduire en carte au lieu de quitter (la croix de la
   carte arrête et quitte). Le process reste le même, pas de service (voie C).
   Dans le port Korin (`src/apps/media`, README « Mini-lecteur »), la fenêtre se réduit **sur place** : UIKit ne
   sait pas encore déplacer une fenêtre ni la garder au-dessus des autres. **À ajouter à UIKit/Elegant** (déplacer
   et dimensionner sa fenêtre, « toujours au-dessus »), puis aux trois apps : Media Player, Spotify, Jamendo.
3. Noms des apps.
