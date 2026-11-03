# Media Player sur ARMPI

Le Media Player d'Onyx (`user/Apps/media`) en C++, sans pont, 500 Ko : bibliothèque musicale, lecture des morceaux, vidéos,
fichiers MIDI, listes de lecture, mini-lecteur. Application `SD0:/apps/media.app` (nom d'origine d'Onyx), tiroir Multimédia du
dock. Elle s'ajoute au lecteur en assembleur `SD0:/apps/mediaplayer.app`, qui reste : il prouve le selfhost
(`src/bin/selfhost/selfhost.asm`) et ouvre un fichier sans bibliothèque. Media Player est hors du selfhost, comme la suite
bureautique.

    sh tools/office/build.sh media       # (WSL) -> build/office/sdroot/apps/media.app/{main,app.txt,lang-fr.txt}
    pibuild app /src/apps/media media    # sous ARMPI, par le GCC de la carte : 227 s sous QEMU, binaire de même taille

Musique : `SD0:/home/music` et les dossiers ajoutés. Vidéos : `SD0:/home/video` (toujours regardé) et ces mêmes dossiers.
Fichiers de l'application : `SD0:/home/media/` (`settings.ini`, `library.tsv`, `stats.tsv`, `videos.tsv`, `thumbs/*.jpg`,
`lastvideo.txt`) ; listes de lecture : `SD0:/home/music/Playlists/*.m3u`. Lancement : `SD0:/apps/media.app/main [fichier]`.

## Ce qui est repris d'Onyx

* `tags.h` (ID3, FLAC, Vorbis, WAV ; Opus ajouté), `lib.h` (bibliothèque, groupes, analyse dans un thread), `midi.h`
  (lecture d'un SMF, notes pour la vue), `videos.h` (noms, genres, épisodes) ; toutes les pages de `main.cpp` (accueil, albums,
  album, artistes, artiste, morceaux, genres, dossiers, listes, recherche, bienvenue, en lecture, vidéos), la file de
  lecture, l'aléatoire, la répétition, les favoris, les compteurs, la reprise d'une vidéo où on l'a laissée, le partage de
  `HitList` pour les zones cliquables, la géométrie des icônes, les pochettes dessinées d'après le nom.
* `data/lang-fr.txt` : le dictionnaire d'Onyx, au format de `ui_tr_load`. `jpegsave.h` : l'encodeur JPEG de `pngsave.hpp`.
* `stb_image.h` (domaine public, celui de Slides), `image.c` : JPEG, PNG, BMP dans les threads des pochettes.
* Licence : MIT d'Onyx. Le Media Player d'Onyx est en GPL-2 à cause de FFmpeg ; ce port ne le lie pas.

**Aucun pont** : la fenêtre appelle `ui_*`, le son `ak_*`, l'image `mk_*`. Rien de l'uikit C++ d'Onyx, ni `kapi_*`, ni
`av_*`, ni MeltySynth, ni minimp3 et consorts.

## Ce qui est écrit pour ARMPI

* **Fenêtre** : une `ui_view` par partie (barre du haut, contenu, barre de lecture, pied du panneau, mini-lecteur, vidéo),
  préfixes `top_`, `content_`, `now_`, `foot_`, `mini_`, `watch_`. Le panneau latéral est une `ui_list (UL_SIDEBAR)` avec
  ses icônes par `ui_list_cell` ; la recherche un `ui_textbox` ; la barre de défilement un `ui_scrollbar`. Les clics sont
  gardés (`click_on`) et faits par la boucle (`do_click`), jamais dans l'évènement de UIKit (menus et dialogues).
* **Thème** : jetons de `ui_jetons` (`ui.h`), jamais `theme.txt`. Pris au lancement. Restent des couleurs fixes là où le
  fond vient de la pochette (bandeau d'album, « en lecture », mini-lecteur), le rouleau MIDI (toujours sombre).
* **Musique** (`player.h`) : `ak_play` d'AudioKit (décodeurs, MIDI, FM, thread, flux). Un petit thread à nous ouvre et ferme,
  parce qu'un MIDI charge la SoundFont de 32 Mo : la fenêtre n'attend jamais. Plus de décodeur ni de synthé dans le programme.
* **Vidéo** (`watch.h`) : la boucle de `mediaplayer` dans un thread : MediaKit convertit dans la surface attachée
  (`ui_surface`, aucune recopie), AudioKit joue, l'horloge est celle du son ; la fenêtre ne fait que signaler les images. La
  surface se déplace sous un verrou. Bilan de la dernière vidéo : `SD0:/home/media/lastvideo.txt`.
* **Vignettes** (`thumbs.h`) : une poignée MediaKit à elles, cachées en JPEG ; **pochettes** (`covers.h`) : `image.c`.
* **Plein écran** : `ui_dialog_at` (fenêtre sans cadre, comme le diaporama de Slides), la surface dedans.

## Différences avec Onyx

* **Contrôles de la vidéo** : dans deux barres, au-dessus et au-dessous de l'image, toujours visibles, pas par-dessus :
  Elegant compose la surface au-dessus du contenu de la fenêtre, on ne peut rien dessiner sur elle. **En plein écran,
  aucun contrôle** : clavier (Espace, ← →, ↑ ↓, m, 0-9, Esc, f) et clic. À la fin, la surface part et les boutons Revoir /
  Épisode suivant s'affichent sur la vignette.
* **Mini-lecteur** : la fenêtre se réduit sur place. UIKit ne sait pas la déplacer : pas de carte en bas à droite, ni au-dessus
  des autres.
* **Formats vidéo** : ceux de MediaKit, WebM, Matroska, MP4 ; VP8, VP9, AV1. Pas de FFmpeg, donc ni H.264, ni H.265, ni AAC, ni
  AVI, MPEG, WMV... Une vidéo H.264 que MediaKit refuse à l'ouverture n'apparaît pas dans la bibliothèque (Onyx la montrait
  avec son codec). Son illisible (AAC) : la vidéo joue muette.
* **Musique** : ce qu'AudioKit lit, MP3, Ogg Vorbis, Opus (en plus d'Onyx), FLAC, WAV, MIDI, FM Song. Pas de M4A.
* **SoundFont** : celle du système (`ak_sf_default`), pas de choix (`soundfont` de `settings.ini` d'Onyx retiré). Sans elle,
  la banque FM d'AudioKit.
* **Pas de sortie son** : message d'erreur à la lecture (Onyx faisait défiler le temps en silence). Une vidéo garde
  l'horloge du système et joue muette.
* **Clavier seul** (le curseur de `objnav`, modes pocket et console d'Onyx) : pas repris, ARMPI n'a pas PocketUI.
* **« Afficher dans le gestionnaire de fichiers »** : retiré.
* **Fichier abîmé** : un MP3, FLAC, Ogg, Opus ou WAV entièrement illisible figure dans la bibliothèque sous son nom (les
  étiquettes manquent sans erreur), et dit « ne peut pas être lu » quand on le joue.

## Vérifié (QEMU raspi4b, captures lues)

* Givre, Onyx Noir, Milk ; 1024 × 768 et 800 × 600. Accueil, albums (pochette PNG et JPEG de dossier, pochettes dessinées),
  album, artistes, genres, dossiers, bienvenue, recherche, menu contextuel ouvert, dialogue de nouvelle liste, « en lecture »,
  mini-lecteur.
* Son : FM Song, MIDI (GeneralUser GS, rouleau de notes) ; signal lu dans le `out.wav` de QEMU. Vidéo VP8 + Opus 640 × 360 :
  son et image, lecture, pause, position, volume, plein écran aller et retour, reprise au bon endroit, vignette et barre rouge.
* VP8 360p : 410 images montrées, 0 sautée en 20,8 s, décodage 13,4 ms par image, son actif. VP9 480p joué en 800 × 600.
* Fichiers abîmés (.mp3, .flac, .ogg, .opus, .wav, .mid, .fms, .webm, .mp4) : pas de faute, message à la lecture.
  `tags.h` et `midi.h` seuls, sur PC, ASan + UBSan : 36 000 fichiers abîmés sans faute.
* `pibuild app /src/apps/media media` par le GCC natif sous QEMU : 227 s, binaire de 500 712 octets comme la chaîne croisée.

## Non vérifié

* **Rien n'a été essayé sur le vrai Pi** : ni le son (jack, HDMI, USB), ni les performances vidéo (VP9 480p, AV1 360p), ni la
  synchronisation sur la durée d'un film, ni le tactile. `lastvideo.txt` donne la mesure à faire.
* Le binaire natif n'a pas été relancé (il fait la même taille que celui essayé). Double-clic sur une image pour le plein
  écran, renommer et effacer une liste, propriétés (dialogue écrit, jamais ouvert), Maj / Ctrl + clic, pochette dans une
  étiquette ID3 ou FLAC, aléatoire et répétition, épisodes d'une série, fin de vidéo (Revoir).
* Ouverture par le gestionnaire de fichiers (`opens` dans `app.txt`).
