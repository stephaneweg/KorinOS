---
name: armpi-musique
description: "18/11 : les 4 étapes faites et construites (UIKit/Elegant, mini-lecteur, Jamendo, Spotify avec librespot pour Korin et Connect), pas encore essayées : il faut les client_id"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026) : une app Korin pour Spotify (il a un compte Famille, donc Premium) « portée
en C », et un client Jamendo. Le lecteur web de Spotify ne peut pas marcher dans Jet (Widevine).

Proposé (docs/etude/MUSIQUE.md) : ne pas réécrire librespot en C (gros, et perdrait ses mises à jour) mais le
construire par la cible Rust croisée avec une façade `extern "C"`, l'app en C/C++ sur UIKit ; connexion OAuth par
oauthd (client_id de l'utilisateur), API web pour parcourir, Spotify Connect ensuite. Jamendo d'abord (API
publique, MP3 directs), pièces communes (flux, file, touches multimédia, applet) dans MediaKit/AudioKit/UIKit.
Décision du 18/11 : **deux apps** (Spotify, Jamendo). Décision du 18/11 : fermer la fenêtre ne coupe pas la musique, la fenêtre se réduit en carte en bas à droite, au-dessus des autres (le mini-lecteur du Media Player d'Onyx, vérifié dans son dépôt) ; le port Korin la réduit sur place faute de déplacement de fenêtre et de « toujours au-dessus » dans UIKit : à ajouter, puis aux trois apps. Question en attente : noms.

**How to apply:** vérifier en ligne l'état de librespot et des conditions de l'API de Spotify avant de commencer.
Voir [[armpi-rust]], [[armpi-oauth]], [[armpi-media-player]].

**18/11, ordre donné par l'utilisateur** : 1) UIKit/Elegant (déplacer sa fenêtre, toujours au-dessus), 2) le mini-lecteur du Media Player et la fermeture qui réduit en carte, 3) Jamendo, 4) Spotify. Après les priorités (essais du build world, boutons du touchpad, Fichiers en deux vues).

**18/11, fait (construit pour le PC, pas essayé)** : 395cac7a (ui_window_place, ui_window_top), 10b35594 (mini-lecteur
du Media Player, ui_window_pos), 040227ce (Jamendo), acae5d5b et 75c8c47b (Spotify : librespot 0.7.1 pour
x86_64-unknown-korin, src/lib/korinspot, tools/spotify/build.sh avec 7 caisses patchées ; l'app, OAuth par oauthd,
Connect par le nuage). Pour essayer : un client_id Jamendo (devportal.jamendo.com) et une app Spotify (retour
http://127.0.0.1:8898/). Reste : pochettes, touches multimédia, pièces communes dans les kits (§3), korinspot pour le Pi.
