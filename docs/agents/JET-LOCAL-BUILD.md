# Jet construit dans Korin (agent local, ZBook) : ce qui est fait, ce que la session du cloud doit reprendre

Note de l'agent local (Claude Code sous Korin, sur le ZBook), tenue à jour à chaque étape. Suite de
`JET-TF1-ETAPES-RESULTATS.md`, `JET-TF1-ETAPES-2-RESULTATS.md` et `JET-SDK.md`. Décisions de l'utilisateur du
22/11/2026 : rendre Jet aussi fluide que Chrome sur le ZBook ; construire **dans Korin** ce qui peut l'être (pas de
cloud obligatoire) ; Korin PC n'a qu'à tourner sur **ce** processeur ; et quatre leviers, dans cet ordre :

1. les drapeaux du processeur (Tiger Lake) pour WebKit et ses dépendances ;
2. le SIMD x86 de libjpeg-turbo (il n'y en a pas sur le PC aujourd'hui) ;
3. le décodage des images hors du fil principal ;
4. le défilement sur un fil à part (`ASYNC_SCROLLING`).

## Étape 1 (commit de cette note) : la base

### Dans Jet (sources de `src/extra/onyxports/jet`, à construire)

- **`engine_set_user_css`** (`engine.h`, `engine_webkit.cpp`) + **`/apps/<app>.app/user.css`** lu par `webapp.cpp` :
  une feuille de style propre à une app web, ajoutée au cadre principal de chaque page avant celles du site (un
  `WKUserScript` au début du document). **TF1+** : `src/apps/tf1plus/user.css` (`body { pointer-events: auto
  !important; }`) — le site bascule `pointer-events` sur `<body>` en défilant et chaque bascule recalculait le style
  des ≈ 13 000 éléments (≈ 350 ms) ; avec la feuille : 41-58 images/s au lieu de 1. `tools/pc/apps.sh` copie
  `user.css` avec `webapp.txt`. **tf1plus 1.0.3** (`versions.ini`) demande **`jet >= 1.4.8`** (`packages-pc.ini`).
- **Le défilement animé** (`engine_webkit.cpp`) : `ScrollAnimatorEnabled` (faux par défaut sur ce port, vrai sur GTK
  et WPE) allumé par `WKPreferencesSetBoolValueForKeyForTesting` ; `JET_SMOOTH_SCROLL=0` ou
  `/etc/web-no-smooth-scroll` l'éteint. Jugement de l'utilisateur à la molette : en attente.
- Vérifié sur le ZBook avec le SDK 1.4.6 (`engine_webkit.o`, `webapp.o` recompilés, `jetlink`) : Jet démarre,
  `webapp: user.css (773 bytes) in every page`, `smooth scrolling on`.

### Dans WebKit

- **`tools/webkit/patches/0016-image-decode-draw-times.patch`** : une ligne `web: gpu: images:` toutes les 2 s (avec
  `ONYX_WEB_GPU_DEBUG` ou `/etc/web-gpu-debug`) : le décodage synchrone des images (fil principal ou non : nombre,
  ms, Mpx, `BitmapImageSource::nativeImageAtIndexCacheIfNeeded`) et les dessins d'images
  (`GraphicsContextSkia::drawNativeImage` : nombre, ms, le pire). Fait sur les sources du SDK 1.4.6 ; ne touche pas
  les fichiers de 0015. But : l'enregistrement du dessin coûte 0,75 à 1,7 s par fenêtre de 2 s quand TF1+ charge ;
  ≈ 10 ms par image de 1920 px (page locale de 24 images) ; forcer `DecodingMode::Asynchronous` dans
  `RenderBoxModelObject::decodingModeForImageDraw` n'a **rien** changé (essai annulé) : il faut savoir ce qui coûte.

### Les drapeaux du processeur

- Le processeur : **i7-11800H** (Tiger Lake) ; AVX2, FMA, BMI2, VAES, SHA, et l'AVX-512 — mais **le noyau de Korin
  n'autorise pas l'AVX-512** (XCR0 = `0x7` : x87, SSE, AVX ; ni opmask ni ZMM). Une instruction AVX-512 ferait
  planter le programme ; l'autoriser est un changement du noyau (XCR0 bits 5-7, une zone XSAVE plus grande) à
  décider à part.
- **`tools/webkit/env.sh`** : `ONYX_CPU_FLAGS="-march=tigerlake -mno-avx512f -mtune=tigerlake"` pour le PC
  (WebKit et ses dépendances seulement ; le noyau, libposix et les apps du PC restent en x86-64 de base).

### Le SDK : deux défauts corrigés, un à corriger dans `jet-sdk.py`

- `tools/webkit/jet-sdk/jetbuild` : la commande d'un lot (`UnifiedSource-*.cpp`) n'était jamais trouvée (le motif
  cherchait un espace après le chemin, qui finit la ligne) ; `-idirafter …/PrivateHeaders/WebCore` ajouté ;
  `JETFLAGS` pour des drapeaux en plus.
- **À corriger dans `tools/webkit/jet-sdk.py`** : les en-têtes de `webcore/WebCore/PrivateHeaders/WebCore` sont des
  renvois d'une ligne (`#include "/devtools/jet-sdk/webkit-gl/…/harfbuzz/HbUniquePtr.h"`) ; un `#include "X.h"` du
  vrai fichier (`FontPlatformData.h` → `HbUniquePtr.h`) n'est alors plus trouvé et **aucune** commande de
  `commands.txt` ne compile telle quelle, même `WKRunLoop.cpp`. Copier les en-têtes au lieu de renvois, ou ajouter
  `-idirafter <SDK>/webcore/WebCore/PrivateHeaders/WebCore` dans chaque commande.
- `jetlink` écrit par défaut dans `/tmp`, qui ne fait que 256 Mo en mémoire sur le ZBook (170 + 133 Mo n'y tiennent
  pas) : lui donner une sortie (`jetlink /tmp2/jet-main`, après `mount /tmp2 ram:3072M ramfs`).
- `jet-sdk-get.sh` : le `wget` de `/bin` n'a pas HTTPS ; les parts se téléchargent par Python (`urllib`), puis
  `unzip` ; `/devtools` est inscriptible sans `sudo` (qui demande un mot de passe).

### Construire dans Korin : `tools/webkit/local/`

| Script | Ce qu'il fait |
|---|---|
| `rebuild-sdk.py JOBS "DRAPEAUX"` | recompile **tous** les objets de `commands.txt` du SDK (sans CMake ni Ninja), en parallèle, reprend où il s'est arrêté ; ZBook : 10 jobs, ≈ 1 h, ≈ 7 Go de mémoire ; 5 échecs sans effet (3 en-têtes précompilés `.gch` absents du SDK, `LLIntSettingsExtractor`, `LLIntOffsetsExtractor` : des outils de construction) |
| `jetcc.sh NOM` | recompile un source de Jet (`src/extra/onyxports/jet/NOM.cpp`) en `jet_NOM.o` du SDK, avec la commande de `WKRunLoop.cpp` (comme `build-jet.sh`) |
| `build-nasm.sh` | NASM 2.16.03 (pas de paquet, et son `configure` ne reconnaît pas Korin) : source officielle, `config.h` tiré de `config.h.in`, GCC ; vérifié de zéro |
| `build-jpeg.sh` | **libjpeg-turbo 3.1.4 avec le SIMD x86-64** (SSE2 et AVX2, choisis au lancement) sans CMake : les sources de `CMakeLists.txt`, les 27 `.asm` par NASM ; la source officielle (la copie d'Onyx de `src/extra` n'a que `simd/arm`). Piège : `jconfigint.h` refait `#undef WITH_SIMD` puis le redéfinit par ses propres `#cmakedefine` (sinon ni SIMD ni codage arithmétique) |

Mesure provisoire (machine chargée par la recompilation) : un JPEG de 651 × 867, 8,2 ms sans SIMD (la `libjpeg.a`
du SDK), 5,8 ms avec. À refaire au repos.

### Ce que la session du cloud peut faire de cette étape

1. **jet 1.4.8** : les sources de Jet de ce commit, les patchs jusqu'à 0016, `ONYX_CPU_FLAGS` de `env.sh` ; puis
   publier jet 1.4.8, puis **tf1plus 1.0.3** (avec `user.css`). Si 0016 gêne, le laisser de côté : ce n'est que de
   la mesure.
2. **libjpeg-turbo avec SIMD sur le PC** (`tools/webkit/ports/libjpeg-turbo/build.sh` : `WITH_SIMD=OFF` sur x86-64
   aujourd'hui) : il faut `nasm` dans le cloud et `simd/x86_64` (source officielle, ou ajouter le dossier à la copie
   d'Onyx). Ou reprendre `tools/webkit/local/build-jpeg.sh`.
3. Le SDK : `jet-sdk.py` (les en-têtes de renvoi, ci-dessus), le prochain SDK en 1.4.8.

## Étape 2 : les outils, et pourquoi la construction locale a été arrêtée

### Construits dans Korin et publiés (paquets du PC)

| Paquet | Version | Fichiers | Script (`tools/webkit/local/`) |
|---|---|---|---|
| `nasm` | 2.16.3 (NASM 2.16.03) | `/devtools/gcc/bin/nasm` | `build-nasm.sh` (`configure` d'autoconf ne reconnaît pas Korin : `config.h` tiré de `config.h.in`) |
| `ninja` | 1.12.1 | `/devtools/gcc/bin/ninja` | `build-ninja.sh` + `ninja-korin.patch` (pas de `getloadavg` ; au plus 16 commandes à la fois par défaut : 64 fichiers ouverts par process, au-delà de ≈ 20 commandes `posix_spawn` et `pipe` rendent `EMFILE`) ; amorçage `configure.py --bootstrap --platform=linux`, `CXXFLAGS=-D_GNU_SOURCE` |
| `libjpeg-turbo` | 3.1.4 | `/devtools/gcc/lib/libjpeg.a`, `jpeglib.h`, `jconfig.h`, `jmorecfg.h`, `jerror.h` dans `/devtools/gcc/include` | `build-jpeg.sh` (SIMD SSE2 + AVX2, `-march=tigerlake -mno-avx512f`) ; mesuré au repos : JPEG 651 × 867, **3,4 ms au lieu de 5,0** (×1,5) |

`stage-tools.sh` assemble l'arbre de ces paquets ; `pub.py NOM --new --tree ARBRE` les publie.

### Recompilation complète de WebKit dans Korin : arrêtée (à faire dans le cloud)

`rebuild-sdk.py` (drapeaux Tiger Lake) a fait 773 objets sur ≈ 1590 en 1 h, puis est tombé à ≈ 20 objets par quart
d'heure : les compilateurs attendaient le disque à 97 %. Ce qui a été appris sur les volumes de Korin :

- **FAT32 (`/`, service `fatfs`)** : un seul fil ; avec 10 compilations il est à 99 % d'un cœur. Pire : un dossier de
  milliers de fichiers (`webcore/WebCore/PrivateHeaders/WebCore`, les lots unifiés) se parcourt à chaque recherche
  de nom : la copie du SDK depuis le FAT32 vers `/home` est tombée à **62 Ko/s**. Le SDK de Jet n'est pas fait pour
  vivre sur le FAT32 : le mettre sur `/home` (ext4) à l'installation (`jet-sdk-get.sh`), avec `JETSDK`.
- **`ramfs`** (`mount POINT ram:TAILLE ramfs`) : **4096 entrées au plus** (fichiers + dossiers ; le SDK en a ≈ 53 000)
  et **4 Go par fichier** au plus ; **pas de démontage** (`eject` ne traite que les clés USB) : le montage part au
  redémarrage.
- **Un montage ne se pose pas sur un point déjà monté** ; un sous-dossier d'un volume se monte ailleurs
  (`mount /devtools/x blk.home ext4fs /trolly/x` : vérifié).
- **Piste de l'utilisateur, pas encore essayée** : un disque en mémoire formaté en ext4 — une image sur un `ramfs`
  (un seul fichier, ≤ 4 Go), vue comme un bloc par `/sys/svc/imgblk img3 FICHIER`, `mkfs -t ext4 blk.img3`, montée par
  `ext4fs`.
- **Les en-têtes précompilés** (`cmake_pch.hxx.gch`, 90 à 660 Mo, recréables avec un `cmake_pch.hxx.cxx` d'une ligne
  `/* generated by CMake */`) : sur le FAT32 ils ralentissent tout (chaque compilation relit 400 à 660 Mo) ; ne pas
  les faire là.
- GNU make : `mkfifo` n'existe pas sous Korin, la file de jobs n'est pas créée et make compile un fichier à la fois ;
  `MAKEFLAGS=--jobserver-style=pipe` la passe par un tube.
- CMake 3.31.6 : son `bootstrap` démarre (tests du compilateur passés) ; arrêté à 6 fichiers sur ≈ 250 (le FAT32).

## Pour la nouvelle session du cloud : ce qu'il faut faire

0. **D'abord, avant WebKit : rendre le volume système rapide sous charge** — `docs/agents/FICHIERS-PERF.md` (un cache
   des noms dans `vfs`, avec les absents ; un index par dossier dans `fatfs` ; les lectures en parallèle dans
   `fatfs`, avec plusieurs requêtes en vol dans `blkserv` et `nvme`). Demande de l'utilisateur du 22/11/2026 : c'est ce qui a fait s'effondrer la construction locale.
1. **Reconstruire WebKit pour le PC** avec ce dépôt : `ONYX_CPU_FLAGS` de `tools/webkit/env.sh` (Tiger Lake, sans
   AVX-512), les patchs jusqu'à **0016**, **libjpeg-turbo avec son SIMD x86** (`tools/webkit/ports/libjpeg-turbo/
   build.sh` a `WITH_SIMD=OFF` sur x86-64 : il faut `nasm` et `simd/x86_64`, absent de la copie d'Onyx — prendre la
   source officielle 3.1.4, ou reprendre `tools/webkit/local/build-jpeg.sh`) ; puis Jet avec les sources de
   `src/extra/onyxports/jet` (user.css, défilement animé).
2. **Publier jet 1.4.8**, puis **tf1plus 1.0.3** (il apporte `user.css` et demande `jet >= 1.4.8`).
3. **Le SDK 1.4.8** : corriger `tools/webkit/jet-sdk.py` (les en-têtes de renvoi de `PrivateHeaders`, voir plus haut ;
   joindre les `cmake_pch.hxx.cxx`) ; `jet-sdk-get.sh` : télécharger en Python (pas de HTTPS dans `wget`), installer
   sur `/home` (ext4) plutôt que dans `/devtools` (FAT32).
4. **Ensuite, après la publication** (demande de l'utilisateur du 22/11/2026) : **porter CMake** pour Korin PC (paquet
   `cmake`, `/devtools/gcc/bin/cmake`, ses modules dans `/devtools/gcc/share/cmake-3.31`), **et les autres outils
   qu'il faut pour construire WebKit sous Korin PC** : Perl (générateurs de liaisons et de code de WebCore), Ruby
   (`offlineasm`, le LLInt de JavaScriptCore), gperf, bison, flex, unifdef — chacun en paquet, comme `nasm` et
   `ninja` (déjà publiés), avec son script dans `tools/webkit/local/`. But : reconstruire WebKit entièrement dans
   Korin (CMake + Ninja), sans le SDK préparé dans le cloud.

5. **Les bibliothèques du SDK en paquets de développement** (demande de l'utilisateur du 22/11/2026) : chaque
   bibliothèque de `sysroot/lib` du SDK (les `.a` et leurs en-têtes) en paquet `<nom>-dev` à part, comme `occt-dev`
   et `planegcs-dev`, dans `/devtools/gcc/lib` et `/devtools/gcc/include` : `zlib-dev`, `libpng-dev`, `libwebp-dev`,
   `freetype-dev`, `harfbuzz-dev`, `icu-dev`, `skia-dev`, `sqlite-dev`, `libxml2-dev`, `brotli-dev`, `nghttp2-dev`,
   `openssl-dev`, `curl-dev`… (`libjpeg-turbo` est déjà publié de cette façon : 3.1.4, avec son SIMD). Les mêmes
   drapeaux que WebKit (`ONYX_CPU_FLAGS`), avec leurs besoins entre eux (`needs`) et leur licence. Elles serviront à
   tout portage (pas seulement Jet), et le SDK n'aura plus qu'à les demander au lieu de les embarquer.

## État du ZBook (nettoyé le 22/11 au soir)

- `/apps/jet.app/main` : l'original de jet 1.4.7, remis ; `/apps/tf1plus.app/user.css` ôté (Jet 1.4.7 l'ignorerait).
- Dossiers de travail effacés (`~/jet-build`, `~/jet-sdk-dl`, `/devtools/jet-sdk-fat`, la copie partielle de
  `/home/trolly/jet-sdk`) ; les relevés restent dans `~/jet-mesures/`. Un dossier vide `/devtools/jet-sdk` (point du
  ramdisk, effacé au redémarrage) est à ôter après le redémarrage.

## La suite côté agent local (après le Jet 1.4.8 publié)

- Mesurer TF1+ avec jet 1.4.8 (lignes `web: gpu: images:` de 0016) ; décider du décodage des images hors du fil
  principal (piste : des `SkImage` à décodage différé, `SkImages::DeferredFromGenerator`, décodées par les cœurs qui
  rejouent les `SkPicture`).
- Le défilement sur un fil à part : `ASYNC_SCROLLING` est éteint par `Source/cmake/OptionsOnyx.cmake:167` ; WebKit
  a ce qu'il faut pour les ports coordonnés (`ScrollingCoordinatorCoordinated`, `ScrollingTreeCoordinated`,
  `ThreadedCompositor`), mais le compositeur d'Onyx (`LayerTreeHostOnyx`, `GraphicsLayerOnyx`) compose sur le fil
  principal et ses couches ne sont pas des `CoordinatedPlatformLayer` : il faut un fil de composition et des nœuds
  d'arbre de défilement pour ses couches. Un plan d'abord.

## Étape 3 (session du cloud, 22-23/11/2026) : ce qui est fait

Branche `claude/serene-newton-051kkg`, fusionnée dans `main` le 23/11.

### 0. Le volume système rapide sous charge

Fait : points 1, 2, 4 et 5 de `FICHIERS-PERF.md` (cache des noms de `vfs`, index par dossier et 8 fils de `fatfs`,
plusieurs requêtes en vol dans `blkserv` et 8 files dans `nvme`). Mesures et marche à suivre sur le ZBook :
`FICHIERS-PERF.md`, « Fait ». **Pas encore essayé sur le ZBook.**

### 1. WebKit reconstruit de zéro pour le PC

- **Les drapeaux du processeur ne servaient pas** : `ONYX_CPU_FLAGS` (env.sh) ne vaut que pour les scripts qui le
  lisent ; tout ce qui passe par CMake (WebKit, ANGLE, libjpeg-turbo, freetype, harfbuzz, ICU, curl...) prend ses
  drapeaux de `tools/webkit/armpi-toolchain.cmake`, qui n'en mettait aucun sur le PC. Corrigé :
  `-march=tigerlake -mno-avx512f -mtune=tigerlake` (ou `ONYX_CPU_FLAGS` de l'environnement), et `-O2`
  (`build-webcore.sh` : `CMAKE_*_FLAGS_RELEASE`).
- **libjpeg-turbo avec son SIMD x86-64** (`tools/webkit/ports/libjpeg-turbo/build.sh`) : la source officielle 3.1.4
  (SHA-256 fixé, `~/.cache/korin-src`), `simd/x86_64` par nasm (SSE2 et AVX2, choisis au lancement).
- **FFmpeg de Jet avec son assembleur x86** (`tools/mediakit/ffmpeg.sh`, `FLAVOR=jet` sur le PC) : jusqu'ici
  `--disable-asm` ; maintenant nasm, `--cpu=tigerlake`, sans AVX-512 (H.264, HEVC, AAC : les chemins SSE/AVX2).
  Nouveau correctif `ffmpeg-patches/0003-tx-x86-max-8192.patch` : les FFT x86 s'arrêtent à 8192 points comme les
  tables du C (0001). Le MediaKit du système reste sans assembleur. **libvpx (VP8/VP9) et dav1d (AV1) restent en C
  seul sur le PC** (`codecs.mk`, `AV_ARCH=generic`) : leur assembleur x86 serait le prochain gain pour YouTube.
- Le correctif **0012** ne s'appliquait plus à l'arbre de base (son dernier morceau avait le contexte de l'arbre
  WebGL, que `enable-webgl.py` fait ensuite) : corrigé.
- Patchs 0001 à **0016** compris ; sources de Jet de `src/extra/onyxports/jet` (user.css, défilement animé).
- GlKit : `libglkit.a` (le relais du kit, 280 Ko) repris du SDK 1.4.6 ; Mesa n'est pas reconstruit (le kit
  `glkit.so` du système ne change pas).

### 2. Le SDK

`tools/webkit/jet-sdk.py` : les liens de `PrivateHeaders` deviennent des **copies** (plus des renvois d'une ligne) ;
les `cmake_pch.hxx.cxx` sont joints. `jet-sdk-get.sh VERSION [DOSSIER]` : téléchargement par Python (le `wget` de
`/bin` n'a pas HTTPS), installation ailleurs que `/devtools/jet-sdk` (sur `/home`, en ext4) avec
`relocate-sdk.py` (joint au SDK) ; puis `JETSDK=DOSSIER`.

### 4 et 5. Les outils et les bibliothèques en paquets

`tools/devtools/` (voir son `build.sh`) : **cmake 3.31.6, perl 5.40.0, ruby 3.3.6, m4 1.4.19, bison 3.8.2,
gperf 3.1, unifdef 2.12, pkgconf 2.3.0**, chacun en paquet dans `/devtools/gcc`, et **13 paquets `-dev`**
(`stage-dev.sh`) : zlib, libpng, libwebp, freetype, harfbuzz, icu, skia, sqlite, libxml2, brotli, nghttp2,
openssl, curl, construits comme WebKit (Tiger Lake), avec leurs `.pc` et leur licence. Essayés dans Korin sous
QEMU (perl : File::Spec, JSON::PP, List::Util, POSIX ; ruby : digest, json, pathname, date ; cmake -P avec
execute_process ; m4, bison, unifdef, pkgconf). **flex 2.6.4** se construit mais **se bloque à l'exécution** (sa
chaîne de filtres : fork, tubes, dup2 vers m4 ; la fin de fichier n'arrive jamais : à examiner côté tubes de
`vfs` et libposix) : pas en paquet. WebKit n'en a pas besoin (bison oui : XPathGrammar).

Ces paquets sont **publiés sur la branche** (`pub.py --new --base HEAD --no-push`) : la fusion dans `main` les
rend disponibles. Ensuite, dans Korin : `pkg add cmake perl ruby gperf bison unifdef pkgconf` (et ninja, nasm,
python, gcc) pour essayer CMake + Ninja sur WebKit.

### 3. Jet 1.4.8 : construit, essayé sous QEMU, publié le 23/11 (avec le SDK 1.4.8)

- WebKit de zéro : 7746 cibles en 1 h 43 (4 cœurs), sans erreur ; Jet relié (133 Mo). Les seules instructions
  AVX-512 du binaire sont dans les chemins choisis au lancement d'OpenSSL (AES-GCM, ChaCha, Poly1305) et de simdutf
  (icelake), qui lisent XCR0 : comme avant ; `-mno-avx512f` tient pour le reste.
- Sous QEMU (q35, sans KVM, `-cpu max`, sur les nouveaux `vfs` / `fatfs` / `nvme`) : la page d'accueil, une page de
  JavaScript (JIT, WebCrypto par OpenSSL, user agent) et 12 JPEG 1920 × 1080 (libjpeg-turbo et son SIMD) s'affichent.
  Une page WebGL arrête son process web : le kit GlKit n'était pas dans l'arbre d'essai (Mesa n'est pas construit
  dans cette session) ; sur le ZBook il y est.
- `pub.py jet --api --dry-run` passe (seul `apps/jet.app/main` change ; `cdmhost` et `cdmupdate` gardés) ; à publier
  avec `--abi 629` (le système publié est à 629, les relais de UIKit de ce Jet viennent des sources d'aujourd'hui).
- **Publié le 23/11** (accord de l'utilisateur) : `pc-jet-1.4.8.opk` et `jet-sdk-1.4.8-{1,2,3}.zip` dans la Release
  par la branche jetable `release-upload/jet-1.4.8` et le workflow « Release upload », puis `pub.py jet --api
  --manual-upload --abi 629`. Dans Korin : `pkg update`, et pour le SDK `bash jet-sdk-get.sh 1.4.8 /home/<vous>/jet-sdk`.
  Tout se refait au besoin : `build-deps.sh`, `fetch-angle.sh`, `build-webkit.sh`, `build-ui.sh`, `build-media.sh`,
  `build-jet.sh`, `jet-sdk.sh` (≈ 2 h 30 sur 4 cœurs ; `build/pc/mesa/lib/libglkit.a` repris d'un SDK publié).
- Puis **tf1plus 1.0.3** (`user.css`, `needs = jet >= 1.4.8`) : après jet.

### Le paquet système `pc` 2026.11.93 : publié le 23/11 (accord de l'utilisateur)

Seulement six fichiers (`--only`) : `korin/kernel.elf` (le compteur des pages, 917b76a), `korin/initrd.tar`,
`sys/svc/vfs`, `sys/svc/fatfs`, `sys/svc/nvme` et `bin/fsbench` (nouveau). Les autres programmes du paquet, que le
GCC du cloud compile autrement, sont restés ceux de 2026.11.92. Les nouveaux services de fichiers ne sont essayés
que sous QEMU : sur le ZBook, `pkgd` installe le paquet ; en cas de problème, revenir à 2026.11.92 (noyau, initrd,
`vfs`, `fatfs`, `nvme`).
