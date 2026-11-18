---
name: armpi-musique
description: "Au plan le 18/11 : app Spotify (compte Famille = Premium) sur librespot construit par la cible Rust en bibliothèque C, et client Jamendo natif ; docs/etude/MUSIQUE.md, rien de fait"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026) : une app Korin pour Spotify (il a un compte Famille, donc Premium) « portée
en C », et un client Jamendo. Le lecteur web de Spotify ne peut pas marcher dans Jet (Widevine).

Proposé (docs/etude/MUSIQUE.md) : ne pas réécrire librespot en C (gros, et perdrait ses mises à jour) mais le
construire par la cible Rust croisée avec une façade `extern "C"`, l'app en C/C++ sur UIKit ; connexion OAuth par
oauthd (client_id de l'utilisateur), API web pour parcourir, Spotify Connect ensuite. Jamendo d'abord (API
publique, MP3 directs), pièces communes (flux, file, touches multimédia, applet) dans MediaKit/AudioKit/UIKit.
Trois questions en attente (un lecteur ou deux apps, service en arrière-plan, noms).

**How to apply:** vérifier en ligne l'état de librespot et des conditions de l'API de Spotify avant de commencer.
Voir [[armpi-rust]], [[armpi-oauth]], [[armpi-media-player]].
