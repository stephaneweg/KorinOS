# WebKit sur ARMPI (le navigateur Jet d'Onyx)

Portage de WebKit — le moteur de **Jet**, le navigateur d'Onyx — sur ARMPI. Point de départ : le
portage d'Onyx (docs/08-WEBKIT-PORT.md d'Onyx), qui tourne sur le Pi chez Onyx. Principe (décision
de l'utilisateur) : **n'adapter que la glue propre à Onyx** ; la couche système est `posix`
(docs/POSIX.md, à la place de libonyxposix) ; WebKit est **l'exception au selfhost** : il se
construit dans WSL avec la chaîne d'Onyx, jamais sur le Pi. Le port garde son nom WebKit `Onyx`
(`PORT=Onyx`, `WTF_OS_ONYX`, `__onyx__`) : la série de patchs d'Onyx s'applique telle quelle ;
`__armpi__` dit en plus qu'on est sur ARMPI.

## Où sont les choses

| Où | Quoi |
|---|---|
| `src/extra/webkit/` (hors git) | WebKit patché par la série d'Onyx (révision : `ONYX-REVISION.txt`) |
| `src/extra/<dépendance>/` (hors git) | ICU, Skia, HarfBuzz, FreeType, curl, nghttp2, brotli, zlib, libxml2, SQLite, libpng, libjpeg-turbo, libwebp, Opus ; `onyx-libs/` : `av` (bibliothèque média d'Onyx), `gpucomp` (son en-tête) — `src/extra/README.md` |
| `src/extra/onyxports/jet/` (suivi) | l'application Jet sur ARMPI : `engine.h` / `engine_webkit.cpp` (le moteur d'Onyx), `main.cpp` (l'habillage sur UIKit), `present.h` / `present.c` (la fenêtre et **le point de présentation** : la surface attachée d'Elegant), `app.txt`, `start.html` |
| `tools/webkit/` | les scripts (WSL) : `env.sh`, `armpi-toolchain.cmake`, `sysroot.sh`, `build-deps.sh` + `ports/`, `fetch.sh`, `build-jsc.sh`, `build-webcore.sh`, `build-wctest.sh`, `build-wk2test.sh`, `build-jet.sh`, `build-media.sh` + `av.mk`, `build-ui.sh`, `stage.sh` ; `glue/` (la glue d'Onyx récrite pour ARMPI) ; `tests/` |
| `tools/kits/abi2c.py` | l'interface C d'un kit générée depuis son `.abi` (prototypes, relais, constantes) |
| `src/kits/uikit/include/uikit.h` | l'en-tête C d'UIKit (sur l'interface générée) |
| `src/lib/posix/include/armpi/kit.h` | les kits depuis le C (`armpi_kit_open`, `armpi_kit_fn`) |
| `~/armpi-webkit-build/` (WSL) | tout ce qui est construit : `sysroot/`, `ports/`, `webkit/` (copie de travail), `jsc-cloop/`, `webcore/`, `av/`, `uikit/`, `out/` (binaires dépouillés) |
| `build/webkit/sdroot/` | ce que `stage.sh` dépose pour la carte ; `build.ps1` le recopie : `SD0:/apps/jet.app`, `SD0:/apps/koton.app` (Koton se construit avec le sysroot C++ de ce chantier). Les essais (`SD0:/bin/jsc`, `wctest`, `wk2test`, `avplay`, `uitest`, `kptest`, `curltest`, `SD0:/webkit/` : pages et scripts d'essai) seulement avec `TESTS=1 sh tools/webkit/stage.sh` (depuis le 5/11/2026) |

Aucun binaire n'est dans git (jsc fait 31 Mo, wctest ≈ 80 Mo).

## Construire (WSL, depuis la racine du dépôt)

```sh
sh tools/webkit/build-deps.sh        # sysroot (libposix) + les 11 dépendances : 6 min 36 s (16 cœurs)
sh tools/webkit/build-jsc.sh install # jsc (C_LOOP) : 8 min ; INTERP=llint pour le LLInt + WebAssembly
sh tools/webkit/build-media.sh       # libonyxav.a (son par AudioKit) + avplay
sh tools/webkit/build-ui.sh          # UIKit depuis le C (libuikit_c.a) + uitest
sh tools/webkit/build-webcore.sh     # PAL + WebCore (≈ 1 h la première fois, 12 travaux)
sh tools/webkit/build-wctest.sh      # wctest : une page -> PNG, ou --view : dans une fenêtre
TARGET=WebKit sh tools/webkit/build-webcore.sh   # libWebKit.a (WebKit2) : 47 min
sh tools/webkit/build-wk2test.sh     # wk2test : WebKit2 sans fenêtre (UI + web + réseau) -> PNG
sh tools/mesa/build-angle.sh         # ANGLE, puis build-armgl.sh : Mesa pour WebGL (docs/MESA.md)
sh tools/mesa/build-webkit.sh        # le WebKit de Jet, avec WebGL, dans ~/armpi-mesa
sh tools/webkit/build-jet.sh         # Jet : SD0:/apps/jet.app/main
sh tools/webkit/stage.sh             # -> build/webkit/sdroot, puis .\build.ps1 sous Windows (TESTS=1 : avec les essais)
```

Outils de l'hôte : `cmake ninja perl python3 ruby gperf unifdef pkg-config` (+ `ccache`), la chaîne
`/opt/toolchains/aarch64-onyx-elf-14.2`. Le sysroot (`sysroot.sh`) recopie libposix
(`tools/posix/build.sh`, `NO_TESTS=1`) et écrit `onyx.specs` / `onyx-cc.specs` aux noms qu'attendent
les scripts d'Onyx. Après une modification de `src/lib/posix` : `sh tools/webkit/sysroot.sh`.

## Étape 1 — sysroot et dépendances : fait

Les scripts de `tools/ports` d'Onyx, repris dans `tools/webkit/ports` (chemins, polices
`SD0:/fonts/` pour `SkFontMgr_onyx`, données ICU liées dans le programme, CA de curl
`SD0:/etc/ca-bundle`, sans `skiademo`). **Les onze dépendances se construisent contre libposix
sans rien y changer.** Outils d'essai dans `~/armpi-webkit-build/ports/bin` (sqlite3, xmllint, curl,
icutest, hbtest, skiatest) : construits, pas encore essayés sur ARMPI.

## Étape 2 — jsc (C_LOOP, sans JIT) : fait

`jsc` (31,5 Mo dépouillé, avec les données ICU) tourne sous QEMU au premier essai, sans changement
de WebKit ni de posix :

* `jsc -e print(6*7)` → `42` ;
* `jsc SD0:/webkit/smoke.js` → `smoke: ok (27 checks)` : arithmétique, BigInt, chaînes, Unicode,
  `normalize`, expressions régulières (groupes nommés), `Date` (ISO, `Date.parse`, `Date.now`),
  JSON, `Intl.NumberFormat` / `DateTimeFormat` / `Collator` (ICU), classes à champs privés, `Map`,
  tableaux typés, 100 000 objets, générateurs, déstructuration, `Promise`, `async`, microtâches,
  `setTimeout` ;
* `jsc SD0:/webkit/bench.js` (QEMU sur le PC, pas le Pi) : fib(25) 138 ms ; 2 M additions 473 ms ;
  100 000 objets 404 ms ; 30 000 concaténations + split 75 ms ; tri de 30 000 nombres 233 ms ;
  10 000 expressions régulières 71 ms ; JSON 2 × 200 Ko 74 ms.
* Chargement (le chargeur d'images, ci-dessous) : premier lancement 12 à 28 s sous QEMU (la carte
  émulée lit ≈ 1 à 2,5 Mo/s) ; épinglé (`preload SD0:/bin/jsc`) : **partagé en 0 à 1 ms**.

## Le chargement des gros programmes (noyau, AppKit)

Fait avant de lancer WebKit, à la demande de l'utilisateur, comme les « program images » d'Onyx :
tout lancement passe par une **image** construite sans tampon ni copie (`IMAGE_CREATE` : les pages
du fichier mappées chez AppKit, qui y lit le fichier ; `IMAGE_SEAL` : vérifiée, les pages du fichier
deviennent celles des segments), clé = chemin résolu + taille, second lancement partagé, images
épinglées (`preload`) ou passagères, une ligne au journal par lancement. Plus de limite de 16 Mo.
Détails : docs/ABI.md, « Images des exécutables ». Reste à faire : garder une image passagère après
la fin de son dernier process et l'éviction sous pression mémoire (étape d d'Onyx), le remplissage
paresseux depuis le fichier (étape b).

## Étape 3 — WebCore : une page locale rendue

`wctest` (repris d'Onyx) : WebCore seul (« empty clients », pas de réseau), une page HTML lue, mise
en page, peinte par Skia sur le processeur :

```
wctest SD0:/webkit/page1.html RAM0:/page1.png        # -> titre, éléments, texte, PNG
wctest --view SD0:/webkit/page1.html [secondes]      # la page dans une fenêtre UIKit, la molette défile
```

Construit : `libWebCore.a` en 40 min (12 travaux), `wctest` 82 Mo (60 Mo de code, 23 Mo de données
dont ICU). **Sous QEMU, au premier essai, sans changer WebCore ni posix** :

* `wctest SD0:/webkit/page1.html RAM0:/page1.png` → `title: Onyx WebCore test`, `elements: 41`, le
  texte (UTF-8 : « été, Ελληνικά, Кириллица » — les DejaVu de `SD0:/fonts` par `SkFontMgr_onyx`), `script:
  ran, 6 * 7 = 42` (JavaScriptCore dans WebCore), `painted: 800 x 600 -> RAM0:/page1.png (47465
  bytes)` — **exactement la taille du PNG d'Onyx** sur son banc et sur le Pi ;
* `wctest --view SD0:/webkit/page1.html` : la page dans une fenêtre (capture
  `docs/assets/webkit-wctest.png` : boîtes CSS, flexbox, grille, dégradé arrondi, SVG, canevas,
  texte sans / avec empattements / chasse fixe, contrôles de formulaire du thème Adwaita) ;
  `long.html` défile à la molette (`docs/assets/webkit-defilement.png`) : 94 à 131 ms par image
  repeinte sous QEMU, première image 9 s après le lancement (image épinglée : initialisation de
  WebCore, polices lues sur la carte, mise en page ; pas encore détaillé) ;
* `preload SD0:/bin/wctest` : 80 553 Ko, 20 137 pages partagées dont 43 en copie sur écriture ; les
  lancements suivants : `image SD0:/bin/wctest : partagée en 1 ms`.

Pas encore : le réseau (`http://`, `https://` : curl + OpenSSL sont liés, mais WebCore seul n'a pas
de chargeur — c'est le process réseau de WebKit2), les sous-ressources hors `data:`.

## Étape 4 — affichage : le point de présentation

`src/extra/onyxports/jet/present.h` : `jet_window_open`, `jet_window_event`, `jet_window_close` et
**`jet_present (pixels, pas, x, y, l, h)`, la seule fonction qui montre l'image**. **Basculée le
30/10/2026 sur la surface attachée d'Elegant** (`ui_surface_*` d'UIKit, DESIGN-ELEGANT.md 17) :
`jet_page_area (x, y, l, h, &pas)` crée (ou déplace) la surface de la page et rend ses pixels ;
WebKit y peint directement ; `jet_present` ne fait plus que `ui_surface_damage` (une image à soi
y est copiée ; sans surface, ou sans zone de page comme `wctest --view` : copie dans le tampon de
la fenêtre puis `ui_update`, comme avant).

**Composition par le GPU (30/10/2026)** : Jet compose par défaut avec le compositeur d'Onyx
(`LayerTreeHostOnyx`, `USE(GRAPHICS_LAYER_ONYX)`) ; `--nogpu` ou `SD0:/etc/web-nogpu` : le chemin
logiciel de WebKit2 (DrawingAreaCoordinatedGraphics → `ShareableBitmap` → `BackingStoreSkia` →
`WKPagePaint`), `--gpu` force le compositeur.

* `gpucomp` d'Onyx (`src/extra/onyx-libs/gpucomp/gpucomp.c`) est lié **tel quel** ; ses appels au
  GPU du noyau d'Onyx (`kapi_gpu_*`, paliers v53–v72) sont faits par GpuKit dans
  `tools/webkit/glue/gpucomp_armpi.c` (en-tête : `tools/webkit/glue/include/appkit/appkit.h`). Une
  image de plus de 1024 lots ou 49 152 sommets y est coupée en images successives (par-dessus).
  Sans GPU (QEMU, `no_gpu`) gpucomp compose sur le processeur : « backend CPU » dans le journal.
* Surfaces (`onyx_surface_*` de `webkit2_armpi.c`) : tampons partagés **sous 1 Go** (`BUF_LOW` : le
  GPU compose directement dedans, « target path: straight into the surface ») et ouverts à tous
  les process (`BUF_GRANT` pid -1 : le process web, Elegant).
* Vérifié sous QEMU (backend CPU) : la page d'accueil, premier image 15 ms, journal
  `web: gpu: ...`. Sur le PocketTerm (30/10/2026) : backend GPU, Wikipédia en https, première image
  en 1 ms.

## Étape 5 — UIKit depuis le C

* `tools/kits/abi2c.py <kit>.abi OUT [--const .inc...]` : `<kit>_abi.h` (un prototype par ligne
  `kit_fn`, arguments `long` comptés dans le commentaire, le commentaire du `.abi` au-dessus ; les
  constantes numériques des `.inc` en `#define`), `<kit>_abi.S` (relais `adrp / ldr / br x16` vers la
  table), `<kit>_abi_open.c` (`<kit>_kit_open` : `armpi_kit_open` puis la table). Un kit qui grandit
  par la fin (UIKit, autre chantier) : relancer `build-ui.sh`, rien d'autre.
* `src/kits/uikit/include/uikit.h` : inclut `uikit_abi.h`, ajoute `struct ui_vars` (la macro
  `ui_data`), `ui_ev_field`, `uikit_open`.
* `uitest` (SD0:/bin) : une fenêtre ouverte, dessinée (pixels directs, texte TrueType, bouton),
  ses événements, fermée — **depuis le C, vérifié sous QEMU (capture)**.
* Jet : **commencé** le 30/10/2026 (section « Jet sur ARMPI » plus bas) : le moteur d'Onyx tel
  quel, un habillage écrit pour ARMPI sur les widgets d'UIKit.

## Étape 6 — son et vidéo

* La bibliothèque média d'Onyx (`src/extra/onyx-libs/av`, `tools/webkit/av.mk` → `libonyxav.a`),
  que pilote `MediaPlayerPrivateOnyx` : démultiplexeurs (WebM, MP4, WAV, FLAC, MP3), FLAC, MP3, PCM,
  Opus. **Le son sort par AudioKit** : `glue/av_sound_armpi.c` (un flux s16 stéréo 44,1 kHz,
  `ak_open` / `ak_write` / `ak_space` / `ak_latency` / `ak_close`), sous les noms `kapi_sound_*`
  qu'attend `av_player.c` compilé avec `-DAV_KAPI_SOUND` (`glue/include/appkit/appkit.h`) : le code
  d'Onyx est inchangé. Essai : `avplay SD0:/webkit/tone.wav` → `avplay: ok : 3.00 s joues en 3.12 s,
  son par AudioKit` (QEMU, sortie USB audio).
* **La vidéo (13/11/2026)** : `av.mk` compile les décodeurs d'Onyx avec les sources et les drapeaux
  de `codecs.mk` — **libvpx 1.15.2** (VP8, VP9 profil 0, 8 bits) et **dav1d 1.5.1** (AV1, 8 bits), un
  thread chacun ; le Pi : `AV_ARCH=aarch64` (NEON de libvpx, assembleur AArch64 de dav1d), le PC :
  `generic` (le C seul) — et `av_vpx.c`, `av_dav1d.c` avec `AV_WITH_VPX` / `AV_WITH_DAV1D` (comme le
  Media Player, mais liés dans Jet : pas par MediaKit). `glue/av_video_armpi.c` (`av__stub_of`) n'est
  plus que le recours des codecs absents. Rien d'autre à changer : `MediaPlayerPrivateOnyx` montre les
  images (peintes, ou une couche du compositeur), l'horloge est celle du son.
  `canPlayType` / `isTypeSupported` (`av_type_supported`) disent oui à WebM et MP4 en VP8, VP9, AV1
  avec Opus ; « fluide » jusqu'à 854 × 480 pour VP8 / VP9 et 640 × 360 pour AV1 (les mesures du Pi
  d'Onyx), accepté jusqu'à 1,5 fois (720p pour VP9) : c'est ce que YouTube consulte pour choisir.
  **Media Source** (MSE) est construit (`ENABLE_MEDIA_SOURCE`, `MediaSourcePrivateOnyx`), ManagedMediaSource
  non ; **WebAudio** non (`ENABLE_WEB_AUDIO` éteint : `AudioContext` absent).
  Ne sont pas lus : H.264, H.265, AAC (Onyx ne les avait pas sans FFmpeg), **Ogg** (pas de
  démultiplexeur Ogg dans la bibliothèque : `audio/ogg` répond non), Vorbis (pas de décodeur), DRM.
  Taille : `libonyxav.a` 0,82 → 2,58 Mo (Pi), 0,96 → 2,56 Mo (PC) ; **Jet** 105 958 328 → 106 679 336
  octets sur le Pi (+0,7 Mo), 126 417 080 → 127 203 640 sur le PC (+0,8 Mo).
* **Essais** : `tools/webkit/mkclips.py` fait des clips libres (motif synthétique qui bouge, ton de
  440 Hz ; PyAV dans WSL : libvpx, SVT-AV1, Opus, LAME) ; `tools/webkit/tests/video.html` joue chacun
  et écrit au journal (`SD0:/etc/web-console`) les images montrées, sautées et les images/s
  (`getVideoPlaybackQuality`), les réponses de `canPlayType` et `isTypeSupported`, puis le VP9 par
  Media Source ; `tools/webkit/tests/avbench.sh` : `avplay --types`, `avplay --decode` (décodage et
  conversion en BGRA, au plus vite) et des lectures. `TESTS=1 sh tools/pc/sysroot_webkit.sh` les met
  dans l'arbre du PC (`SD0:/webkit`, `SD0:/webkit/clips`, `SD0:/bin/avplay`).

  | `avplay --decode` (un cœur) | QEMU q35, TCG | i7-11800H natif (Tiger Lake, comme le ZBook ; le même C) |
  |---|---|---|
  | VP9 426 × 240 | — | 762 images/s |
  | VP8 640 × 360 | 37,7 images/s | 347 |
  | VP9 854 × 480 | 21,5 | 181 |
  | VP9 1280 × 720 | 8,4 | 86 |
  | AV1 426 × 240 | — | 375 |
  | AV1 640 × 360 | — | 256 |
  | AV1 854 × 480 | 15,4 | 157 |

  (Le natif : `avplay --decode` compilé pour Linux x86-64 dans WSL, la même bibliothèque en C
  générique : l'ordre de grandeur du ZBook.) Sous QEMU (TCG, 4 cœurs), Jet joue chaque clip de
  `video.html` jusqu'au bout avec le son : en 480p et 720p le décodeur ne suit pas (21 images/s pour
  30) et le lecteur saute les images en retard jusqu'à l'image clé suivante (un clip de 4 s n'en a
  qu'une) ; en 240p le décodeur suit, mais la page repeinte par le processeur à chaque image s'en tient à
  4 à 6 images/s (« Jet : onglets... », « Vérifié »). Le son des pages sort (QEMU
  `intel-hda`, enregistré en WAV) : MP3, FLAC, WAV, l'Opus des WebM.

## WebKit2 (le multi-process) — fait le 30/10/2026

Le navigateur d'Onyx est WebKit2 : trois process (UI, web, réseau) d'un même programme, reliés par
l'IPC de WebKit (`Platform/IPC/unix/ConnectionUnix.cpp`, `SharedMemoryUnix.cpp` de WebCore,
`ProcessLauncherOnyx.cpp`). Ce qui lui manquait est **dans le noyau d'ARMPI** depuis le 30/10/2026
(`src/kernel/ipc.asm`, appels `0x70`–`0x7D` : [ABI.md](ABI.md) ; côté C : [POSIX.md](POSIX.md),
« IPC entre process », où est aussi expliqué le choix d'objets du noyau à poignées plutôt que de
nœuds de vfs) :

1. **`socketpair (AF_UNIX, SOCK_SEQPACKET)`** (et `SOCK_STREAM`, `SOCK_DGRAM`) : un canal du noyau
   à deux bouts, chacun avec son anneau de 64 Ko ; `poll` / `select` dorment dans le noyau
   (`HANDLE_WAIT`), `shutdown` réveille le thread de surveillance de la connexion ; fin : `recv` →
   0, `send` → `EPIPE` ;
2. **`SCM_RIGHTS`** : les canaux et la mémoire partagée passent dans un message (les poignées
   portées gardent une référence ; un message jamais lu les rend) ;
3. **`memfd_create` / `shm_open` + `ftruncate` + `mmap (MAP_SHARED)`** : des objets du noyau dont
   les pages sont projetées (régions `VMK_SHARED`, peuplées à la demande) dans chaque process qui
   les reçoit — partagées, pas copiées ; rendues à la dernière référence (poignée, message,
   projection, nom). Plus de limite des `BUF_*` (128, 16 Mo) : 1 023 poignées par process, 1 Go
   par objet ;
4. **l'héritage** : `posix_spawn` donne au fils, à leur numéro, les canaux et objets sans
   `FD_CLOEXEC` (`HANDLE_INHERIT` / `HANDLE_LIST`) — le socket du process web, dont le numéro est
   dans `argv[3]`.

WebKit lui-même n'a pas changé : `libWebKit.a` (compilé le 29/10/2026 contre les mêmes en-têtes de
libposix) est seulement relié à la nouvelle libposix. `IPC::Semaphore` (futex sur une page
partagée) n'est compilé que pour Linux : pas utilisé ici (pas de process GPU).

**wk2test** (d'Onyx : WebKit2 sans fenêtre, `tools/webkit/build-wk2test.sh`, 95 Mo) sous QEMU :

```
preload SD0:/bin/wk2test                 (93 Mo épinglés ; les trois process les partagent en 1 ms)
wk2test /webkit/page1.html /wk2.png
  ready: 98 ms  started: 1071 ms  committed: 1256 ms  loaded: 5664 ms
  title: Onyx WebCore test   elements: 41   text: Hello from WebCore on Onyx ... 6 * 7 = 42 ...
  painted: 800 x 600 -> /wk2.png (47355 bytes)
```

Le process réseau sert `file:` (« web: net start / net done 200 »), le process web met en page et
exécute le script, le process UI peint la page (chemin logiciel : `ShareableBitmap` sur
`memfd`, `WKPagePaint`) ; à la fin les trois process s'arrêtent (code 0). `long.html` (206
éléments) : chargée en 4,6 s. Une adresse s'écrit `/chemin` (un `file:` d'ARMPI part du lecteur
courant, `SD0:`) : `file://SD0:/x` n'est pas une URL valide pour WebKit.

## Jet sur ARMPI (commencé le 30/10/2026)

`src/extra/onyxports/jet`, construit par `tools/webkit/build-jet.sh` (comme wk2test : les sources
C++ avec la commande d'une source de WebKit), posé par `stage.sh` dans `SD0:/apps/jet.app`
(`main`, `app.txt` — catégorie Internet —, `start.html`) :

* **le moteur d'Onyx** (`engine.h`, `engine_webkit.cpp`, l'API C de WebKit2 : `WKView`, `WKPage`,
  navigation, `alert` / `confirm`, `<select>`, téléchargements, curseur, presse-papiers), adapté
  seulement aux chemins d'ARMPI (`SD0:/etc/web-*`, `SD0:/apps/jet.app/main`), aux lecteurs de
  trois lettres dans l'adresse tapée et aux caractères Unicode du clavier ;
* **l'habillage** (`main.cpp`, écrit pour ARMPI sur les widgets d'UIKit : `uikit.h`) : barre
  d'outils (← → ↻ ⌂, `ui_toolbar`), adresse (`ui_textbox` : un clic la choisit toute, Entrée
  charge, Échap rend la page ; Ctrl+L), barre d'état (chargement, lien survolé, échecs), souris,
  molette, clavier vers la page (codes HID → codes d'Onyx), `<select>` par `ui_popup_local`,
  `alert` / `confirm` par `ui_message`, nouvelle fenêtre (`target=_blank`) = un onglet (13/11/2026,
  ci-dessous ; avant : le programme relancé) ;
* **la présentation** (`present.c`) : la page est une **surface attachée d'Elegant**
  (`ui_surface`) ; WebKit peint directement dans son tampon partagé, `jet_present` ne fait que
  `ui_surface_damage` — plus de copie (sans surface : copie dans la fenêtre, comme avant).

Vérifié sous QEMU (lancé par `SD0:/etc/startup.txt`) : les trois process, `page1.html` et
`long.html` affichées (captures), molette, adresse tapée (le clavier de l'essai est AZERTY côté
ARMPI : l'adresse tapée par QEMU en QWERTY arrive transformée). Le chargement de 95 Mo prend ≈ 1 min
sous QEMU la première fois ; ensuite le process web et le process réseau partagent l'image.

Reste pour Jet : menus Édition, Affichage, Historique (Fichier, Navigation, Réglages : faits le
13/11/2026), fenêtre redimensionnable
(`WF_RESIZABLE`, `ui_on_resize` → `engine_resize`), fenêtres des téléchargements et de la
console (`downloads.cpp`, `console.cpp` d'Onyx, des process à part), zoom, recherche, menu du clic
droit, l'icône du dock, et surtout **le réseau réel** (http / https par le process réseau : curl +
OpenSSL) à essayer sur le PocketTerm.


## Jet : onglets, barre d'adresse, cookies (13/11/2026)

**Onglets** (`main.cpp`, `engine.h`, `engine_webkit.cpp`), disposés comme Safari : la barre d'outils
inchangée (← → ↻ ⌂, l'adresse, **⌕**), dessous une **rangée d'onglets** sur toute la largeur — largeur
égale, 150 px au moins puis la rangée défile ; au bout **<** et **>** (grisés quand rien n'est de ce
côté ; la molette aussi) et **+** ; l'onglet actif est toujours ramené en vue. Titre centré, **×** à
gauche sur l'onglet actif et au survol, un cercle de points qui tourne pendant le chargement. Dessin à
la main (`draw_tabs` : `ui_fill`, `ui_rbox`, `ui_text_fit`, `ui_glyph`), dans le style de
`ui_tabstrip` d'UIKit : fond `TJ_SUNKEN`, trait `TJ_LINE`, onglet actif `TJ_SURFACE`, textes
`TJ_TEXT1/2/3`, indicateur `TJ_ACCENT`, survol `ui_tone` — aucune couleur en dur ; redessinée à
`WEV_THEME` (Givre, Onyx Noir). Raccourcis : **Ctrl+T**, **Ctrl+W**, **Ctrl+Tab** / **Ctrl+Maj+Tab**,
**Ctrl+PgSuiv / PgPréc** (lettres par la position HID, comme l'éditeur et Ctrl+L) ; bouton du milieu
sur un onglet : fermer ; bouton du milieu ou **Ctrl+clic** sur un lien : un onglet derrière, après
celui-là (l'API C ne donne pas le bouton de la navigation : `engine_mouse` note le clic, `decideAction`
le reconnaît) ; `target=_blank`, `window.open` (`createNewPage`) : un onglet montré. Une adresse par
onglet en argument (`main url1 url2 ...`). Menus d'Elegant : **Fichier** (Nouvel onglet, Nouvelle
fenêtre, Fermer l'onglet, Quitter), **Navigation**, **Réglages**.

**Un process web par onglet** : chaque onglet (`struct Tab`) a son **WKContext** — WebKit donne aux pages
d'un même contexte et d'un même site le même process web ; avec un contexte par onglet, jamais. Le
process réseau et les données sont communs (un seul data store, ci-dessous). Le pid du process web de
chaque onglet va au journal (`web: [...] tab N: web process pid P`, de nouveau quand il change). **Si le
process web d'un onglet meurt, l'onglet se ferme** : `webProcessDidTerminate` du client de navigation
(raison : plantage ou kill, limite de mémoire ou de processeur ; pas `RequestedByClient`, la fermeture
voulue) → `processEnded (tab)` → l'habillage le ferme à la fin du tour (`close_dead_tabs`) ; le dernier
fermé, un onglet neuf sur la page d'accueil. `webProcessCrashed` de la vue ne compte pas : il vient
aussi quand WebKit passe la page à un autre process (navigation vers un autre site). Un onglet caché
est `WKViewSetVisible (false)` ; avec le compositeur (le Pi), l'onglet montré redemande son image
(`WKPageForceRepaint`). Essai : `tools/webkit/tests/tabkill.sh N S` (attend le pid de l'onglet N au
journal, puis `kill`).

**Barre d'adresse** (`engine_typed_url`) : un schéma (`http:`, `https:`, `file:`, `about:`, `data:`,
`blob:`, `mailto:`...) → l'adresse ; un chemin, un lecteur Korin (`SD0:/x`) → `file:` ; sans espace,
un domaine au TLD plausible (lettres, deux au moins ; `xn--`) → `https://` ; `localhost` ou une IPv4,
avec un port → `http://` ; sinon (espaces, pas de point, `?` en tête) → la page de résultats du moteur
choisi, les mots encodés. Moteurs : **Google** (défaut), Bing, DuckDuckGo, Yahoo, Qwant, Ecosia,
Startpage, Brave Search : le bouton **⌕** (menu, coché) ou le menu Réglages ; gardé dans
`SD0:/home/jet/settings.ini` (`search=google`, comme les autres apps : `SD0:/home/<app>`) ; invite
« Rechercher avec Google ou saisir une adresse ».

**Cookies et données des sites gardés** : le port curl ne remplit `cookieStorageFile` que sous Cocoa
(`WebsiteDataStoreConfiguration::initializePaths`) : `CookieJarDB` restait en mémoire. Jet crée un **data
store persistant** (`make_data_store`, `WKWebsiteDataStoreCreateWithConfiguration`), le même pour tous
les onglets (un process réseau, un pot de cookies) :

| Quoi | Où |
|---|---|
| cookies (SQLite de `CookieJarDB`) | `SD0:/home/jet/webkit/cookies.db` |
| localStorage, IndexedDB, service workers, Cache Storage, stockage général, WebSQL, clés média, ITP, sites isolés | `SD0:/home/jet/webkit/<sorte>` |
| cache HTTP | `SD0:/home/jet/cache/network` |
| HSTS, services alternatifs (pas de réglage dans l'API C) | défaut de `WebsiteDataStoreOnyx` : `SD0:/var/webkit` |

Les chemins donnés à WebKit sont sans volume (`/home/jet/webkit` : son code de chemins ne prend que ceux-là
pour absolus) ; Jet fait de **`SD0:/` son dossier de travail** (`chdir` au début de `main`, dans chaque
process : les fils en héritent aussi) : ils sont sur `SD0:` d'où que Jet soit lancé. SQLite y est en
journal de retour (le patch d'Onyx de `SQLiteDatabase` : pas de WAL, Korin n'a pas de projection
partagée en écriture d'un fichier) ; chaque cookie est écrit à sa réception. Les cookies de session
(sans `Expires` / `Max-Age`) sont effacés de la base à son ouverture (`CookieJarDB`), ceux qui ont une
date restent. **Réglages > Effacer les cookies et données des sites…** (et le menu de ⌕) :
`engine_clear_site_data` (cookies, localStorage, IndexedDB, service workers, Cache Storage, cache HTTP,
caches en mémoire).

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, 2 Go, Korin PC ; 13/11/2026)

Disque de `mkkorindisk.py --layout zbook --no-windows --korin-mb 600`, sur une copie de l'arbre de
`build/pc/sysroot` + `TESTS=1 sh tools/pc/sysroot_webkit.sh`, `intel-hda` (son enregistré en WAV),
`usb-tablet`, `usb-net` (réseau user de QEMU ; un serveur http d'essai sur la machine hôte) ; Jet lancé
par `SD0:/etc/startup.txt`, clics et touches par QMP (`tools/pc/qemu_nvme.py`, `abs:` / `btn:` / `key:`).

* **Onglets** : `main /webkit/page1.html /webkit/long.html /webkit/jetpc.html` → trois onglets, **trois
  process web** (pids 34, 36, 37), le réseau partagé (capture `build/pc/jet_pc_tabs.png`) ;
  `tabkill.sh 2 40` tue le process de l'onglet 2 (`kill 36`) : `tab 2: the web process ended (crash or
  killed)`, **seul l'onglet 2 se ferme**, les deux autres restent (`jet_pc_tabkill.png`). Clic sur un
  onglet, Ctrl+Tab (du dernier au premier), cinq Ctrl+T (huit onglets, autant de process web : la rangée
  défile, `<` actif, `>` grisé, l'onglet actif en vue ; `jet_pc_tabs_scroll.png`), `<` deux fois,
  Ctrl+W (la touche W par sa position) ferme l'onglet montré et montre son voisin ; le dernier fermé
  (process tué), un onglet neuf s'ouvre. Navigation de `10.0.2.2` vers `example.com` : WebKit change de
  process web (pid 35 → 49), **l'onglet reste** (`jet_pc_swap.png`).
* **Barre d'adresse** : `10.0.2.2:18765/check` → `http://10.0.2.2:18765/check` ; `example.com` →
  `https://example.com` (chargée, https) ; `korin os` → `https://www.google.com/search?q=korin+os`
  (page de Google chargée par le réseau de QEMU) ; ⌕ → DuckDuckGo (menu coché, `jet_pc_search.png`) →
  `https://duckduckgo.com/?q=korin+os`, gardé dans `settings.ini`.
* **Cookies** : la page `/set` du serveur pose `jetperm` (`Max-Age`) et `jetsess` (de session) et met
  `localStorage.jet = 42` ; Jet fermé par sa croix (tous ses process finissent, code 0), la machine
  éteinte (`shutdown`) puis redémarrée sur la même image : le serveur reçoit **`Cookie: jetperm=yes`**
  (le cookie de session est oublié, comme prévu) ; au démarrage suivant la page lit `localStorage.jet =
  42` d'avant. `SD0:/home/jet/webkit` : `cookies.db` (24 Ko), `localstorage/`, `storage/`... « Effacer
  les cookies et données des sites » : construit, pas essayé.
* **Vidéo** (`http://10.0.2.2:18765/video.html`, onglet 1, avec les deux autres ouverts) : chaque clip
  joué jusqu'au bout, l'image dans la page (`jet_pc_video.png`) ; Media Source (VP9 240p d'un seul
  `appendBuffer`) joué jusqu'au bout. Sous TCG, la page est repeinte par le processeur à chaque image :
  4 à 6 images/s montrées en 240p (VP9, AV1, Media Source), les clips plus grands sautent presque tout
  (décodage plus lent que le temps réel ici) ; sur le ZBook le décodage seul fait 180 images/s en VP9
  480p (tableau de l'étape 6). Son : MP3, FLAC, WAV et l'Opus des WebM entendus (WAV de QEMU) ; Ogg
  refusé (`NotSupportedError`, attendu). `canPlayType` / `isTypeSupported` : oui pour VP8, VP9, AV1,
  Opus en WebM / MP4, MP3 ; non pour H.264, AAC, Ogg. Une fois sur deux essais semblables (vidéo en http
  + trois onglets + `usb-net`), la machine virtuelle s'est figée au deuxième clip (plus rien au journal
  du noyau, écran figé) ; pas reproduit ensuite : à surveiller.
* **Le Pi** : `libonyxav.a` (NEON, assembleur de dav1d), `avplay` et Jet construits pour aarch64 (Jet
  106 744 808 octets) ; **pas essayé sous QEMU raspi4b** (la carte du Pi n'a pas été refaite).

Pas vérifié : rien sur le vrai ZBook ni le vrai Pi ; YouTube (MSE réel, ses choix de format) ; le Pi
avec le compositeur et plusieurs onglets (l'image redemandée au changement d'onglet :
`WKPageForceRepaint`) ; le clic du milieu sur un lien (Ctrl+clic et le bouton du milieu sont reconnus par
le clic envoyé juste avant, pas essayés) ; « Effacer les cookies et données des sites ».

## https : le fichier des autorités (30/10/2026)

`SD0:/etc/ca-bundle` (le paquet Mozilla, repris d'Onyx ; `skel/sd0/etc`) : curl est construit avec ce
chemin (`tools/webkit/ports/curl/build.sh`), mais la glue d'Onyx dans WebCore
(`Source/WebCore/platform/network/onyx/CurlSSLHandleOnyx.cpp`) imposait celui d'Onyx,
`SD:/res/ca-bundle` : toute page https échouait (`CurlErrorDomain 77`). Corrigé dans la copie de
travail (`src/extra/webkit` et `~/armpi-webkit-build/webkit`) : `SD0:/etc/ca-bundle`, ou le fichier
que nomme `CURL_CA_BUNDLE`. Vérifié sous QEMU avec le `curl` d'ARMPI (`ports/bin/curl.elf`, posé en
`SD0:/bin/curltest`) : certificat d'example.com validé. Les binaires construits avant (wctest,
wk2test sans JIT) gardent l'ancien chemin.

## Points du système à surveiller

* **Gros exécutables** : réglé (images sans limite, ci-dessus). Sous QEMU, 30 Mo se chargent en
  12–28 s ; sur le Pi la carte lit bien plus vite (Onyx : 16 Mo/s). `preload` évite la relecture.
* **Réservations de JSC** : `jsc` tourne (C_LOOP : pas de Gigacage ni de JIT ; `USE_SYSTEM_MALLOC`).
  Le LLInt + WebAssembly (`INTERP=llint`) et les réservations de plusieurs Go (`VM_MAP` `PROT_NONE`)
  restent à essayer.
* **Threads** : un process JSC/WebCore en crée une dizaine (collecteur, minuteries, file d'attente
  de travail) : rien n'a manqué.
* **ICU** : données liées dans le programme (15 Mo, filtrées) ; **polices** : `SD0:/fonts` (DejaVu).

## À essayer sur le PocketTerm

`jsc -e print(6*7)`, `jsc SD0:/webkit/smoke.js`, `jsc SD0:/webkit/bench.js` (les premiers vrais
chiffres), `preload SD0:/bin/jsc` puis `jsc ...` (journal : `partagée en N ms`), `avplay
SD0:/webkit/tone.wav` (jack ou HDMI), `uitest`, `wctest SD0:/webkit/page1.html RAM0:/page1.png` et
`wctest --view SD0:/webkit/page1.html` (le tactile : glisser vertical = molette).

WebKit2 et Jet (30/10/2026) : `posixtest ipc` (27 réussis attendus), `preload SD0:/bin/wk2test`
puis `wk2test /webkit/page1.html /wk2.png` (les trois process ; temps de chargement réels), puis
**Jet** depuis le dock (catégorie Internet) ou `SD0:/apps/jet.app/main` : la page d'accueil, une
page locale (`/webkit/long.html` dans l'adresse), la molette et le glisser tactile, puis **une
vraie adresse** en Wi-Fi (`example.com`, `https://en.wikipedia.org/`) : le process réseau (curl,
OpenSSL) a servi https sous QEMU ([OPENSSL.md](OPENSSL.md)), pas encore sur le Pi. Au journal (`SD0:/etc/startup.txt` →
`SD0:/bin/journal`) : les lignes `web: ...` des trois process.

## État (30/10/2026)

| Étape | État | Vérifié par |
|---|---|---|
| Chargeur d'images (noyau, AppKit) | fait | memtest 40 PASS, non-régression complète, jsc et wctest épinglés puis partagés en 0–1 ms |
| 1. Sysroot et dépendances | fait | les 11 construites contre libposix, sans changement de posix |
| 2. jsc (C_LOOP) | fait | `42`, `smoke: ok (27 checks)`, `bench.js` |
| 3. WebCore | fait | `wctest` : titre, texte, script, PNG de 47 465 octets |
| IPC entre process (noyau, libposix) | fait (30/10/2026) | `posixtest ipc` 27 réussis, non-régression complète |
| WebKit2 | tourne (30/10/2026) | `wk2test` : process UI, web, réseau ; page chargée et peinte (PNG de 47 355 octets) |
| 4. Affichage | surface attachée d'Elegant (`jet_page_area`, `jet_present`) | Jet, captures |
| 5. Habillage de Jet | Jet commencé : moteur d'Onyx, barre d'outils, adresse, état, page | captures de Jet sous QEMU (pages locales, molette) |
| Onglets, recherche, cookies (13/11/2026) | un process web par onglet, moteur de recherche au choix, données des sites gardées | QEMU q35 : 3 onglets, `kill` du process de l'un, cookie et localStorage après redémarrage |
| 6. Son / vidéo | son par AudioKit ; vidéo VP8, VP9, AV1 (libvpx, dav1d liés, 13/11/2026), Media Source | `avplay` : 3 s jouées en 3,12 s ; `avplay --decode`, `video.html` sous QEMU q35 |
