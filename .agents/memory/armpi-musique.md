---
name: armpi-musique
description: "18/11 : UIKit/Elegant, mini-lecteur, Jamendo ; Spotify refait le 20/11 : le lecteur web dans sa fenêtre sur le moteur de Jet (mode app, Widevine), librespot retiré"
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

**20/11, Spotify refait (décisions de l'utilisateur)** : le lecteur web open.spotify.com dans sa propre fenêtre, sur
le moteur de Jet (`--app=spotify` : src/extra/onyxports/jet/webapp.cpp ; src/apps/spotify/main.c n'est qu'un execv) ;
session à part (HOME/spotify/webkit) ; interface UIKit, librespot (korinspot, tools/spotify) et Connect **supprimés**
du dépôt (« Tout supprimer »). Carte : état lu par script (data-testid de Spotify), page derrière (ui_surface_overlay).
Le paquet spotify demande jet. Essai : tools/linux/test_webapp.sh ; la connexion au compte est faite par
l'utilisateur. docs/etude/MUSIQUE.md (début).
**Publié le 20/11** (jet 1.4.1, spotify 1.1.0, ce77e2a7) ; **essayé par l'utilisateur sur le ZBook** : l'app tourne, la carte réduite marche.
**Netflix (20/11)** : src/apps/netflix, même coquille (lanceur commun src/lib/webapp/launcher.c, webapp.txt maximize = 1) ; jet 1.4.2 (F11, Agrandir, WEV_RESIZE passé à UIKit : sans lui ni les bords ni agrandir ne suivaient). Puis Disney+, TF1+, Prime Video (disneyplus, tf1plus, primevideo), même coquille ; essai : APP=… tools/linux/test_webapp.sh.
**Cookies (20/11)** : on ne restait pas connecté (signalé par l'utilisateur) : CookieJarDB sans reset() après hasCookies/hasHttpOnlyCookie, commit SQLite repoussé ; patches/0011-cookies-commit.patch, WebCore reconstruit (webkit et webkit-gl), docs/WEBKIT.md. Piège : grep ne voyait rien après le premier octet nul d'une ligne (une base SQLite) ; ce n'était pas fatfs (corrigé le 20/11 au soir, dans le cloud).
**Passation (20/11 soir)** : docs/agents/REPRISE-APPS-WEB.md (apps web, cookies, défaut de fatfs à corriger, tools/linux/sqltest).
Piège : un patch de WebKit appliqué à la main va aussi dans le .armpi-patches de webkit et webkit-gl (sinon fetch.sh le réapplique et build-webkit.sh s'arrête).
**20/11 au soir (cloud, branche claude/eloquent-meitner-zapblv)** : le « défaut de cohérence de fatfs » de la passation n'existe pas : c'était grep (C du PC et asm du Pi, coupés au premier nul ; corrigés : chaque morceau entre deux nuls est cherché) ; tools/linux/sqltest scan lit par 1 Mo (FS_READ_DIRECT), 4 Ko et 100 octets : tout voit les écritures, pendant et après. Jet écrivait dans /home/downloads et /home/<app> (signalé par l'utilisateur, compte trolly, HOME du Terminal juste) : la libposix liée en statique dans Jet était antérieure à la règle HOME (16/11) ; build-jet.sh refait maintenant le sysroot (tools/webkit/sysroot.sh) quand src/lib/posix est plus récent ; Jet corrige HOME lui-même (fix_home : getpwuid) et déplace /home/<app> vers HOME/<app> au premier lancement (move_old_home). Jet à reconstruire et publier (1.4.2).
Netflix, Disney+, TF1+, Prime Video : needs = jet >= 1.4.2 ; à publier avec jet 1.4.2 (décision du 20/11 soir), Jet reconstruit dans le WSL de l'utilisateur ; marche à suivre : docs/agents/REPRISE-APPS-WEB.md.
**Publié le 21/11 (cloud, accord de l'utilisateur)** : jet 1.4.2 (Jet construit dans le cloud : WebKit 74 min sur 4 cœurs, GlKit tiré du paquet pc ; main de même taille que 1.4.1), netflix, disneyplus, tf1plus, primevideo 1.0.0 (121 paquets, main 579b905). Essayé sous QEMU PC avec un compte /home/trolly : cookies de Spotify gardés (sp_t identique après Ctrl+Q et relance, vrai site par le proxy), données sous HOME, anciens dossiers de /home déplacés, téléchargement dans HOME/downloads. Pièges du cloud : le proxy GitHub refuse les envois binaires (415) et la suppression de branche (403) ; la Release passe par une branche jetable release-upload/* et son workflow (GITHUB_TOKEN) ; pub.py --api / --manual-upload. Reste : l'utilisateur vérifie sur le ZBook qu'il reste connecté à Spotify ; branche release-upload/jet-1.4.2 à supprimer par l'utilisateur.
**Disney+ (21/11, signalé par l'utilisateur)** : l'accueil connecté reste longtemps sur son animation de chargement, puis tourne bien ; Netflix et Spotify sans souci. Pas Widevine (essai EME sous QEMU avec le vrai module : réponses en moins de 300 ms), process de Disney+ presque à l'arrêt (cpuwatch) : une requête qui traîne ou une erreur de script. À voir avec /etc/web-console et /etc/web-trace-net sur le ZBook si l'utilisateur le demande. Aussi : 3 missingImage (WebKit sans AVIF : USE_AVIF=OFF, et Jet se dit Chrome sur les sites Widevine).
**21/11, charge du ZBook** (cpuwatch de l'utilisateur : coeurs à 90 %) : curlThread du process réseau de Jet tournait à vide (poll de libposix : boucle de 1 ms sur les tubes et sockets de NetKit ; seuls les canaux locaux et la mémoire partagée dorment dans le noyau) ; corrigé : curl du PC avec un réveil en socketpair (tools/webkit/ports/curl/build.sh), 0 appel/s au repos ; libposix donne les noms des threads au noyau. Pas corrigé : netd fait 7 000 à 12 000 appels/s tout seul ; les sockets de NetKit pendant un transfert (boucle de 1 ms) ; cdmhost (8 en vie sur le ZBook, à ~100 s chacun) ne se reproduit pas sous QEMU (au repos, s'arrête avec son onglet). Jet à republier (1.4.3) avec : /home/.jet suivi, noms des threads, curl.
