# Étude : Spotify et Jamendo dans Korin

Demandée le 18/11/2026. Rien n'est fait. L'utilisateur a un compte **Spotify Famille** (Premium) et veut une app
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

## 4. Ordre proposé

1. **Jamendo** (rapide, sans risque, pose les pièces communes du §3).
2. **librespot pour Korin** (voie B) avec une commande d'essai qui joue un titre.
3. **L'app Spotify**, puis Spotify Connect, puis le service (voie C) si utile.

## 5. Questions

1. ~~Un seul lecteur ou deux apps ?~~ **Deux apps** (décision du 18/11) : une pour Spotify, une pour Jamendo ;
   les pièces communes (§3) restent dans les kits.
2. ~~La musique continue-t-elle app fermée ?~~ **Oui** (décision du 18/11) : fermer la fenêtre pendant la lecture
   ne coupe pas la musique ; la fenêtre disparaît et **un petit lecteur s'affiche sur le bureau** (pochette, titre,
   précédent, lecture/pause, suivant ; un clic rouvre l'app ; une croix arrête et quitte). L'utilisateur le décrit
   comme le Media Player venu d'Onyx ; dans Korin, `media.app` a un mini-lecteur (`enter_mini`, sa fenêtre réduite
   en carte), mais **fermer la fenêtre arrête la musique** (`src/apps/media/main.cpp`, en tête) : la même chose est à
   faire pour lui. Pièce commune dans UIKit/MediaKit (le mini-lecteur du bureau), servie aux trois apps ; le process
   de l'app reste en vie sans fenêtre principale (pas besoin du service de la voie C pour cela).
3. Noms des apps.
