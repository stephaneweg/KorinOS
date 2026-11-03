# src/extra — WebKit et ses dépendances (copiés d'Onyx)

Code C/C++ tiers pour porter Jet (le navigateur WebKit d'Onyx) sur ARMPI. Il est construit
avec la chaîne `aarch64-onyx-elf` dans WSL, sur `src/lib/posix` (libposix), hors du selfhost.
Le contenu n'est **pas dans le dépôt** (`.gitignore`) ni sur la carte (`build.ps1` l'exclut de
`SD0:/src`) : seul ce fichier est suivi.

`sh tools/extra/fetch.sh` refait tout cela depuis le dépôt public d'Onyx, à la révision qu'il fixe.

Copié le 29/10/2026 depuis Onyx :

| Dossier | Origine |
|---|---|
| `webkit/` | arbre de travail WSL `~/webkit` (checkout partiel de WebKit `main` + série de patchs Onyx, branche `onyx`), sans `.git` ; révision dans `webkit/ONYX-REVISION.txt` |
| `onyx-tools/webkit/` | `tools/webkit` d'Onyx : scripts (`fetch.sh`, `build-web.sh`…) et série de patchs |
| `onyx-tools/ports/` | `tools/ports` d'Onyx : scripts de construction des dépendances |
| `sqlite-3.50.4`, `libxml2-2.13.8`, `curl-8.16.0`, `nghttp2-1.70.0`, `brotli-1.1.0`, `zlib-1.3.1` | `third_party/` d'Onyx (ports C) |
| `icu-78.3`, `libpng-1.6.44`, `freetype-2.14.3`, `harfbuzz-14.5.1`, `libjpeg-turbo-3.1.4`, `libwebp-1.4.0`, `skia-m154` | `third_party/` d'Onyx (ports WebKit) |

Copiés par `tools/pdf/build.sh` à son premier passage pour le **Lecteur PDF** (`src/apps/pdf`, AGPL-3.0 à cause de MuPDF) : `mupdf-1.28.5`, `jpeg-9f` depuis `third_party/` d'Onyx, avec `zlib-1.3.1` et `freetype-2.14.3` s'ils manquent.

Copiés le 29/10/2026 pour **MediaKit** (`tools/mediakit/build.sh` les lit d'ici ; hors du
selfhost, [docs/MEDIAKIT.md](../../docs/MEDIAKIT.md)) :

| Dossier | Origine |
|---|---|
| `libvpx-1.15.2` | `third_party/` d'Onyx : décodeurs VP8 / VP9 (BSD), sans les encodeurs ; `onyx/arm64` : la configuration (NEON, sans threads) |
| `dav1d-1.5.1` | `third_party/` d'Onyx : AV1 (BSD-2), 8 bits, assembleur AArch64 ; `onyx/` : `config.h`, threads factices (un seul thread) |
| `opus-1.5.2` | `third_party/` d'Onyx : libopus (BSD), flottant, C seul ; n'est plus lié à MediaKit (AudioKit décode l'Opus depuis le 29/10/2026) ; sert encore au moteur média de WebKit |

(Copie par `robocopy /E /XF *.o *.a *.d` : sans les objets des constructions d'Onyx.) La
bibliothèque `av` d'Onyx (`user/Libs/av`, code de l'utilisateur, MIT) n'est pas ici : sa copie
modifiée est suivie dans `tools/mediakit/av/`.

Pas copié : `ffmpeg` (H.264 / H.265 / AAC, GPL-2 : à décider plus tard).

Ajoutés le 29/10/2026 pour le portage (glue d'Onyx que la série de patchs de WebKit attend) :

| Dossier | Origine |
|---|---|
| `onyx-libs/av` | `user/Libs/av` d'Onyx : la bibliothèque média (démultiplexeurs, lecteur) du moteur média de WebKit (`tools/webkit/av.mk`) |
| `onyx-libs/gpucomp` | `user/Libs/gpucomp` d'Onyx : le compositeur sur le V3D ; seul son en-tête sert (sur ARMPI : souches, pas de compositeur) |
| `onyx-libs/third_party/minimp3` | `third_party/minimp3` d'Onyx (CC0) : le décodeur MP3 de `av_mp3.c` (`onyx-libs/i/a/b` : dossier vide pour son chemin relatif) |

Déjà là (copiés par ailleurs) : `dav1d-1.5.1`, `libvpx-1.15.2`, `opus-1.5.2` ; le moteur média de WebKit
ne lie qu'Opus (le son) — la vidéo viendra de MediaKit. Pas copié : `ffmpeg` (GPL : Jet reste LGPL).

Ajouté le 31/10/2026 pour **FreeBASIC** (`tools/gcc/fbc.sh`, [docs/GCC.md](../../docs/GCC.md)) :

| Dossier | Origine |
|---|---|
| `FreeBASIC-1.10.1` | paquet officiel `FreeBASIC-1.10.1-source-bootstrap.tar.xz` (dépôt GitHub `freebasic/fbc`, version 1.10.1 du 25/12/2023, dernière stable ; SHA-256 `2fae2c66b435240d4de11bb534843baa188574508a8a5bdaa5020bf729bf0a3e`, retéléchargé par le script s'il manque), intact : ce qui change pour ARMPI est dans `tools/gcc/fbc/`. Licences : compilateur GPL-2 ou plus, `rtlib` et `gfxlib2` LGPL-2 ou plus avec exception pour l'édition de liens statique, en-têtes `inc/` selon leur bibliothèque |

Licences : chaque dossier garde la sienne (WebKit LGPL-2.1/BSD, ICU, Skia BSD, curl, etc.) ;
à vérifier avec celle d'ARMPI avant toute publication.

## onyxports/ (suivi par git)

Les applications d'Onyx portées en C/C++ hors du selfhost (règle du 29/10/2026, docs/FEUILLE-DE-ROUTE.md §7) :
une par dossier (`onyxports/<nom>/` : sources reprises d'Onyx et glue pour ARMPI, script de construction).
Contrairement au reste de `src/extra`, ce dossier est dans le dépôt.
