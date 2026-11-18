# Étude : Widevine dans Jet (Netflix, Disney+, Prime Video, Spotify web…)

Demandée le 18/11/2026 (à partir de la question : pourquoi LibreWolf peut télécharger Widevine et pas Jet ?). Rien
n'est fait. **Vient après la couche Linux** (décision du 13/11, mémoire `armpi-linux-compat` ; son étude
`docs/etude/LINUX-COMPAT.md` n'est pas encore écrite), dont il dépend. Ce qui suit vient de ce qu'on savait le 18/11, pas d'une vérification en ligne : à revoir au début.

## 1. Le problème

Netflix, Disney+, Prime Video, Deezer, le lecteur web de Spotify exigent **EME + Widevine** (le DRM de Google).
Le module (CDM) est un **binaire fermé** de Google, fourni pour Windows, macOS, ChromeOS, Android et Linux
(`libwidevinecdm.so`, x86-64 ; ARM64 officieux). Korin ne peut ni le recompiler ni le charger tel quel (il attend
glibc et les appels système de Linux), et WebKit n'a pas d'hôte pour lui (Apple utilise FairPlay ; WPE/GTK passent
par OpenCDM/Thunder). LibreWolf peut le faire parce qu'il reste un Firefox sur un système que Google sert.

## 2. La voie

1. **Process hôte du module, en personnalité Linux** (comme le `plugin-container` de Firefox) : un petit programme
   Linux (glibc, à côté : la couche Linux doit charger un `.so` glibc dynamique, pas seulement un binaire musl
   statique) qui charge `libwidevinecdm.so` et lui donne l'hôte de l'**interface CDM de Chromium**
   (`cdm::ContentDecryptionModule_10/11`, la même que Firefox) : sessions, messages de licence, déchiffrement (et
   décodage si le module le demande), minuteries, stockage.
2. **EME dans Jet** : `ENABLE_ENCRYPTED_MEDIA` de WebKit avec un `CDMPrivate`/`CDMInstance` à nous qui parle au
   process hôte par IPC ; les échantillons chiffrés de MediaKit/FFmpeg passent par lui (audio d'abord : Spotify web,
   Deezer ; puis vidéo).
3. **Une case dans les réglages de Jet** : « Lire les contenus protégés (Widevine, module de Google) ». Cochée, Jet
   télécharge le module **chez Google**, sur la machine de l'utilisateur, au moment voulu, avec sa version et son
   empreinte ; jamais dans nos paquets (seul Google peut le distribuer). Mise à jour du module de la même façon.
4. **Identification du navigateur** : pour ces sites, Jet se présente comme un Firefox ou un Chrome sous Linux
   (Netflix refuse les navigateurs inconnus).

## 3. Ce qu'on peut en attendre

* Niveau **L3** (sans protection matérielle) : Netflix et Disney+ plafonnent en général à **720p** sur Linux ;
  Spotify et Deezer (audio) sans limite gênante.
* **Fragile** : Google peut changer le module ou ses règles, les services peuvent durcir leurs contrôles ; peut
  casser sans recours.
* **Droit** : zone grise tolérable pour un usage personnel (module téléchargé chez Google par l'utilisateur) ; un
  Korin distribué à d'autres ne l'active pas d'office.

## 4. Étapes

| # | Étape | Fini quand |
|---|---|---|
| 0 | Couche Linux capable de charger un `.so` glibc dans un process Linux | un programme glibc dynamique d'essai tourne |
| 1 | Process hôte + interface CDM, essayé hors de Jet | une licence de démonstration obtenue, un échantillon déchiffré |
| 2 | EME dans Jet (audio) | le lecteur web de Spotify joue un titre |
| 3 | Vidéo | Netflix et Disney+ jouent (720p) |
| 4 | Case des réglages, téléchargement et mise à jour du module | de la case à la lecture sans manipulation |
