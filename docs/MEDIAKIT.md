# MediaKit : la vidéo (kit précompilé en C)

Décidé le 29/10/2026 ([FEUILLE-DE-ROUTE.md](FEUILLE-DE-ROUTE.md) § 8 « Vidéo ») : les
**décodeurs vidéo** sont du C porté, dans un kit **précompilé** (chaîne d'Onyx,
hors du selfhost), utilisable par toutes les applications par sa table `kit_fn`. Le lecteur
(`SD0:/apps/mediaplayer.app`) reste en assembleur ; la bibliothèque `SD0:/apps/media.app` (plus bas) est en C++.

| | |
|---|---|
| Sur la carte | `SD0:/lib/mediakit.so` (copié de `prebuilt/mediakit.so` par `build.ps1`) |
| Table | `src/kits/mediakit/mediakit.abi` (assembleur) = `tools/mediakit/mediakit.S` (kit) ; structures et erreurs : `src/kits/mediakit/mediakit.inc` ; en C / C++ : `src/kits/mediakit/mediakit.h` |
| Sources | `tools/mediakit/` : glue (`mediakit.c`, `mk_libc.c`, `mk_math.c`, `mk_sys.h`, `mediakit.S`, `mediakit.ld`), copie modifiée de la bibliothèque **av** d'Onyx (`av/`), `Makefile`, `build.sh` ; libvpx, dav1d dans `src/extra` (hors dépôt, [README](../src/extra/README.md)) |
| Construire | dans WSL, à la racine : `sh tools/mediakit/build.sh` (≈ 15 s sur 16 cœurs ; objets dans `/tmp/armpi-mediakit`) |
| Essais | `SD0:/bin/mediatest fichier [images]`, `SD0:/apps/mediaplayer.app` ; vidéos d'essai dans `SD0:/home/video` |

## Contenu

* **Conteneurs** (bibliothèque av d'Onyx, `user/Libs/av`) : WebM / Matroska (Cues : recherche),
  MP4 / ISO BMFF (fragmenté compris), RIFF (WAV). Pas de FLAC ni de MP3 nus : ce sont des fichiers
  audio, pour AudioKit (`MK_ARMPI` les retire de `av_demux.c`).
* **Vidéo** : libvpx 1.15.2 (VP8, VP9 ; NEON), dav1d 1.5.1 (AV1, 8 bits, assembleur AArch64),
  I420 non compressé. Un seul thread de décodage (dav1d : ses threads factices d'Onyx).
* **Conversion** : YUV 4:2:0 → XRGB8888 (`av_yuv_to_rgb` d'Onyx, NEON, octet haut à 0) ; à une
  autre taille : bilinéaire (16.16) dans le kit.
* **Son** : **pas de codec audio dans MediaKit** (règle de l'utilisateur). Les paquets de la
  piste audio sont décodés par AudioKit (Opus, Vorbis, MP3, FLAC : `ak_codec_*`, en assembleur),
  le PCM brut est converti dans le kit. Le
  rééchantillonnage d'av (Catmull-Rom, repli des canaux en stéréo) rend du s16 stéréo à la
  fréquence voulue.
* Pas de FFmpeg (H.264, H.265, AAC : GPL-2, à décider plus tard).

### Le point de branchement de l'API audio par paquets

Un seul endroit de MediaKit décode du son : **`audio_decode_packet()`** dans
`tools/mediakit/mediakit.c` (appelé par `mk_audio_read`), par le décodeur de la bibliothèque
av. Pour Opus, Vorbis, MP3 et FLAC, ce décodeur est **`av/av_audiokit.c`** (29/10/2026) : l'API
par paquets d'AudioKit (`ak_codec_open` avec les données d'initialisation de la piste,
`ak_codec_decode` en flottant, `ak_codec_reset` après une recherche). AudioKit est trouvé par
`lib_open("audiokit")` (rangs 34 à 38 de sa table) ; l'appel passe par `ak_call`, qui donne
d8-d15 pour perdus (les fonctions du kit ne les gardent pas). Données d'initialisation : celles
de Matroska telles quelles ; MP4 : `dOps` refait en `OpusHead`, `dfLa` sans ses 4 premiers
octets. Opus : l'avance (`pre-skip`) retirée au début, `DiscardPadding` à la fin ; Opus et Vorbis
de 3 à 8 canaux remis dans l'ordre WAVE. PCM → conversion ; tout autre codec (AAC…) :
`mk_audio_open` rend `MK_E_UNSUP`, la piste est muette et un avertissement va sur la sortie
standard. Un programme peut aussi prendre les paquets bruts (`mk_audio_packet`) et les donner
lui-même à AudioKit. libopus est sorti du kit (955 Kio).

Essais (QEMU) : `mediatest` sur `SD0:/home/video/*.webm|mp4` (Opus) et sur
`SD0:/res/audiotest/*.mka` (Opus CELT et SILK, Vorbis, MP3, FLAC en Matroska, faits par un
petit multiplexeur, `tools/audiotest/mkmka.py`, à partir des fichiers d'essai d'AudioKit).

## Système : une glue, pas la couche posix

Un kit est chargé **dans le process de l'application** (souvent un programme en assembleur), à
une adresse fixe pour tout le système, variables recopiées par process. `libposix` ne convient
pas : bibliothèque statique pour un exécutable à adresse fixe, avec `crt0posix` (jamais lancé
dans un programme en assembleur) et newlib à variables par thread (`errno` dans `TPIDR_EL0`, que
les programmes en assembleur n'ont pas). MediaKit est donc un kit précompilé :

* **code indépendant de la position** (`-fPIE`, visibilité cachée), lié à 0 (`mediakit.ld`) ; le
  kit applique lui-même ses relocations `R_AARCH64_RELATIVE` (≈ 280, toutes dans les variables :
  `tools/mediakit/finish.py` le vérifie) au premier appel de chaque process ; `build.sh` refuse un
  kit qui aurait des variables par thread (`.tbss`, `.tdata`) ;
* **bibliothèque C à soi** (`mk_libc.c`) : mémoire, chaînes, `qsort`, `vsnprintf`, `setjmp` /
  `longjmp` (libvpx), `exp`, `cos`, `sin` (`mk_math.c`) ; ni newlib ni libm ;
* **tas** : régions `VM_MAP` du noyau (à la demande) ; petits blocs (≤ 64 Kio) en classes de
  puissances de deux taillées dans des morceaux de 1 Mio, gros blocs une région chacun (rendue
  par `VM_UNMAP`) ; `memalign` ; un verrou (le kit peut servir plusieurs threads) ;
* **pile des décodeurs** : chaque poignée a une pile de 2 Mio (`VM_MAP`, peuplée à la demande) ;
  `mk_open`, `mk_video_next`, `mk_audio_read`, `mk_seek` y basculent (`mk_on_stack`) : dav1d
  et libvpx veulent plus que les 256 Kio du thread principal ;
* **fichiers** : `fichier_sys` d'AppKit (rang 13 de sa table, `lib_open("appkit")` : AppKit est
  déjà dans tout programme) ; **journal** : `SYS_PRINT` par AppKit (sortie standard) ;
  **horloge** : `CNTPCT_EL0`.

Appels système utilisés : `VM_MAP`, `VM_UNMAP`, `YIELD`, `LIB_OPEN`, `EXIT` (abort). Aucun nouveau.

## API

Fonctions C (AAPCS64 : x0-x7 arguments, résultat dans x0 ; **x0-x18 et v0-v7, v16-v31
changés**, x19-x28 et d8-d15 gardés). Poignée opaque (> 0), une poignée ne sert qu'à un thread à
la fois. Structures : champs de 64 bits (offsets `MKI_*`, `MKT_*`, `MKF_*`, `MKP_*`, `MKS_*` de
`mediakit.inc`). Erreurs négatives (`MK_E_*`, numéros d'ARMPI).

| N° | Fonction | Arguments | Résultat |
|---|---|---|---|
| 0 | `mk_version` | – | `0x00010000` |
| 1 | `mk_open` | chemin | poignée ; `MK_E_NOENT`, `MK_E_UNSUP` (format), `MK_E_NOMEM`… Lit jusqu'à connaître les pistes ; choisit la première vidéo décodable et la première piste audio |
| 2 | `mk_close` | poignée | 0 |
| 3 | `mk_info` | poignée, `MKI_*` (152 o) | 0 : durée (us), nombre de pistes, pistes choisies, taille d'affichage, images/s × 1000, fréquence, canaux, codecs, décodés par MediaKit ?, noms (conteneur, codecs) |
| 4 | `mk_track` | poignée, n, `MKT_*` (136 o) | 0 : genre, codec, taille, fréquence, canaux, durée, données d'initialisation du codec (pour AudioKit), décodable, noms, délai (pré-saut d'Opus) |
| 5 | `mk_select` | poignée, piste vidéo, piste audio (-1 : aucune) | 0 (décodeurs rouverts) |
| 6 | `mk_video_next` | poignée, `MKF_*` (0 : rien) | 1 : image décodée, **gardée** (pts, durée, taille, temps de décodage) ; 0 fin |
| 7 | `mk_video_convert` | poignée, pixels, pas (octets), largeur, hauteur | 0 : l'image gardée en XRGB8888 à cette taille (même taille : NEON ; sinon bilinéaire) |
| 8 | `mk_video_read` | les deux à la fois (x5 = `MKF_*`) | 1, 0 fin |
| 9 | `mk_audio_open` | poignée, fréquence | 0 : `mk_audio_read` rendra du s16 stéréo à cette fréquence ; `MK_E_UNSUP` : codec pour AudioKit (muet, avertissement) ; `MK_E_NODATA` : pas de son |
| 10 | `mk_audio_read` | poignée, tampon, trames max, &pts (0 : rien) | trames écrites (0 : fin) ; pts (us) de la première |
| 11 | `mk_audio_packet` | poignée, `MKP_*` | 1 : le paquet suivant de la piste audio, brut (données au kit jusqu'à l'appel suivant), 0 fin — pour un décodeur par paquets ; à ne pas mêler à `mk_audio_read` |
| 12 | `mk_seek` | poignée, us | temps du point d'accès atteint (≤ demandé ; l'image clé d'avant) ; `MK_E_SPIPE` sans index (seul 0 marche) |
| 13 | `mk_stats` | poignée, `MKS_*` | 0 : images décodées, temps de décodage, trames audio, paquets jetés, octets des gros blocs du tas |
| 14 | `mk_error_text` | erreur | texte UTF-8 (au kit) |

Les paquets sont rangés par piste (une file par piste choisie, bornée à 4096 paquets / 48 Mio :
une piste que personne ne lit perd ses plus vieux paquets) ; lire la vidéo et le son en même temps
dans un même thread est le cas prévu (le lecteur).

En assembleur, deux façons de l'appeler : `kit_use '../../kits/mediakit/mediakit.abi'` (relais
habituels ; `kits_open` termine le programme si le kit manque : `mediatest`), ou, pour donner un
message clair quand il manque, `lib_open("mediakit", 15)` puis `f_kit_load` d'AppKit et une
table d'adresses à soi (`mediaplayer` : `mk_load`, macro `mkcall`).

## Mesures (QEMU raspi4b, indicatives)

`mediatest` (décodage + conversion à la taille de l'image, un cœur) :

| Fichier | Image | Images/s | Décodage seul |
|---|---|---|---|
| `sintel-vp8-opus.webm` | VP8 640 × 360 | 79,7 | 8,5 ms |
| `sintel-vp9-opus.webm` | VP9 854 × 480 | 36,8 | 19 ms |
| `test-av1.mp4` | AV1 96 × 64 | 467 | 2,0 ms |
| `test-vp9.webm` | VP9 96 × 64 | 490 | 1,9 ms |

Son Opus 48 kHz → s16 stéréo 48 kHz : 2 s décodées en ≈ 95 ms. `mediaplayer` (lecture réelle,
image mise à l'échelle 680 × 382, son par AudioKit vers la carte son USB de QEMU) : VP8 360p
46 images montrées sur 48 (22,3 images/s pour 24), VP9 480p 37 sur 48 (15,8 images/s : trop
lourd pour QEMU, les images en retard sont sautées), clips 96 × 64 : 50 sur 50. Le son sort
(`build/out.wav` de QEMU). Sur le Pi 4 réel (1,5 GHz, NEON réel), Onyx annonce 480p VP9 et 360p
AV1 fluides sur un cœur.

## Le lecteur (`src/apps/mediaplayer`, en assembleur)

Fenêtre redimensionnable (UIKit) : zone de l'image (la vidéo ajustée et centrée, fond noir), barre
de lecture : lecture / pause, « Ouvrir » (`ui_file_open`, dossier `SD0:/home/video`), glissière de
position (recherche), temps « m:ss / m:ss », volume. Menus Fichier (Ouvrir, Fermer) et Lecture
(lecture / pause, au début) ; Espace, ← / → (± 5 s), Ctrl+O, Ctrl+W. Arguments : `mediaplayer
[fichier] [loop] [autoexit]` (essais : bilan des images montrées / sautées sur la sortie standard
à chaque fin).

* **Image** : une surface attachée d'Elegant (`ui_surface`, DESIGN-ELEGANT.md 17) à la taille de
  la zone ajustée ; MediaKit convertit et met à l'échelle **directement dans le tampon partagé**,
  puis `ui_surface_damage` : aucune recopie. Redimensionner (ou agrandir) déplace la surface
  (`ui_surface_move`) et reconvertit l'image gardée.
* **Son** : `mk_audio_open(44100)` et un flux AudioKit s16 stéréo 44,1 kHz (le format des flux de
  `sound` : pas de conversion dans AudioKit) ; le flux est tenu plein sans attendre (`ak_space`).
* **Horloge** : celle du son = pts du premier paquet écrit + `ak_position` / 44 100 ; sans son
  (pas de piste, codec pour AudioKit, pas de sortie, ou son qui n'avance plus pendant 1,5 s) :
  l'horloge du système. Une image est montrée quand l'horloge atteint son pts ; en retard de plus
  de 150 ms : sautée (4 de suite au plus).
* **Pause** : flux fermé, horloge arrêtée ; reprise : flux neuf, horloge du premier paquet lu.
  **Recherche** : `mk_seek` (au point d'accès d'avant), son rouvert.
* MediaKit absent : message dans la fenêtre et à « Ouvrir » (`ui_message`).

## La bibliothèque multimédia (`src/apps/media`, en C++)

Le Media Player d'Onyx (`user/Apps/media` : bibliothèque musicale, vidéos, MIDI) est porté **sans pont**, hors du selfhost,
sous son nom d'origine : `SD0:/apps/media.app`, à côté du lecteur en assembleur. Détails, différences et ce qui n'est pas
vérifié : [src/apps/media/README.md](../src/apps/media/README.md). Il prend MediaKit par `mediakit.h` et les relais de
`mediakit_abi.h` (`-lmediakit`) : une seule copie des décodeurs, partagée.

| Onyx (`user/Apps/media`) | Port |
|---|---|
| `videos.h` : `av_demux_*`, `av_decoder_supported_track` | `mk_open`, `mk_info`, `mk_close` (`probe_video`) |
| `thumbs.h` : `av_demux_seek`, `av_decoder_*`, `av_yuv_to_rgb` | une poignée à elles : `mk_seek`, `mk_video_next`, `mk_video_convert`, le recadrage reste au programme |
| `watch.h`, `av_player_*` | la boucle de `mediaplayer` dans un thread (`src/apps/media/watch.h`) : surface attachée, AudioKit, horloge du son |
| `decode.h`, `codecs.c`, `midi.h` (MeltySynth) | **AudioKit** : `ak_play` (fichiers, MIDI, FM Song), `ak_dec_*`, rien dans MediaKit |
| imagekit, `kapi_*` | `image.c` (stb_image), `jpegsave.h`, POSIX, pthread |

Une vidéo que MediaKit refuse à `mk_open` (H.264 seul) reste invisible : il faudrait ouvrir sans décodeur pour la montrer
avec son codec.

## Ce qui manque

* H.264 / H.265 / AAC (FFmpeg, à décider) ; FLAC, MP3, Vorbis dans les conteneurs vidéo : muets
  jusqu'à l'API par paquets d'AudioKit.
* Un seul thread de décodage ; `mediaplayer` décode dans sa boucle (une vidéo lourde ralentit
  l'interface), `media` dans un thread à lui, sans images d'avance.
* Pas de double tampon dans la surface (déchirure possible pendant qu'Elegant compose).
* Recherche dans un WebM sans Cues : seulement au début.
* Sur le PocketTerm : vérifier VP9 480p et AV1 360p à 24-30 images/s, le son (prise jack, HDMI ou
  USB), la synchronisation, l'agrandissement en 800 × 600.
