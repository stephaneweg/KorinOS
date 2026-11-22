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

## État du ZBook (à ne pas confondre avec un Jet publié)

- `/apps/jet.app/main` : un Jet **de test**, relié dans Korin (SDK 1.4.6 + ce commit) ; l'original 1.4.7 :
  `~/jet-build/jet-main-1.4.7.orig`.
- `/devtools/jet-sdk` : le SDK 1.4.6, en cours de recompilation complète avec les drapeaux Tiger Lake ; ses objets
  d'origine : `~/jet-build/webcore-orig` ; sources de WebCore modifiées par 0016.
- Relevés : `~/jet-mesures/` (sur la machine).

## La suite (étapes à venir, chacune avec son commit)

2. Relier Jet tout en Tiger Lake + libjpeg SIMD ; mesurer TF1+ contre la référence (1.4.7).
3. Lire `web: gpu: images:` ; faire le décodage des images hors du fil principal (piste : des `SkImage` à
   décodage différé, `SkImages::DeferredFromGenerator`, décodées par les cœurs qui rejouent les `SkPicture`).
4. Le défilement sur un fil à part : `ASYNC_SCROLLING` est éteint par `Source/cmake/OptionsOnyx.cmake:167` ;
   WebKit a ce qu'il faut pour les ports coordonnés (`ScrollingCoordinatorCoordinated`, `ScrollingTreeCoordinated`,
   `ThreadedCompositor`), mais le compositeur d'Onyx (`LayerTreeHostOnyx`, `GraphicsLayerOnyx`) compose sur le fil
   principal et ses couches ne sont pas des `CoordinatedPlatformLayer` : il faut un fil de composition et des nœuds
   d'arbre de défilement pour ses couches. Un plan d'abord (dans cette note).
