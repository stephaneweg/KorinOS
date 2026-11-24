# Blender sur Korin PC

Chantier ouvert le 23/11/2026 à la demande de l'utilisateur : **porter Blender dans Korin**. Ce document est l'étude
de départ : ce que Blender demande, ce que Korin a déjà, le chemin par jalons et les risques. Il sera tenu à jour
(§ 0) au fil des jalons, comme [VULKAN.md](VULKAN.md) ou [LINUX-HOST.md](LINUX-HOST.md).

## 0. En bref

| Jalon | Quoi | État |
|---|---|---|
| 0 | Source, versions, chaîne CMake de Korin, scripts `tools/blender` | **fait (23/11)** : `env.sh`, `korin.cmake`, `versions.py`, `fetch.sh` (Blender 5.2.4 et les sources de classe A) |
| 1 | Les dépendances indispensables, croisées pour Korin PC (Python 3.13, OIIO, OCIO, OpenEXR, TBB, shaderc…) | **fait (24/11)**, construit seulement (rien lancé dans Korin) ; numpy reporté au jalon 4 |
| 2 | **Blender sans écran** (`WITH_HEADLESS`) : `blender -b` charge un .blend, exécute du Python, rend par Cycles sur le processeur | **fait (24/11), sous QEMU PC (KVM)** : `SCENE OK` et `RENDU OK` (Cycles, 4 fils, 0,7 s) |
| 3 | **GHOST de Korin** : fenêtres UIKit, entrées, presse-papier ; rendu par Vulkan (VkKit, WSI de Korin) | à faire |
| 4 | Le reste : Embree, OpenSubdiv, Manifold, OpenVDB, son, vidéo, import/export (USD, Alembic…) | à faire |
| 5 | Paquet `blender`, app (`app.txt`, icône, catégorie), compétence, essai sur le ZBook | à faire |

**Cible : Korin PC (x86-64, le ZBook) seulement.** Le Pi n'a ni Vulkan (v3dv écarté, [MESA.md](../MESA.md)) ni OpenGL
de bureau : V3D s'arrête à OpenGL ES 3.0, et Blender ne sait pas dessiner en ES.

## 1. La version

| Version | Sortie | Python | Remarque |
|---|---|---|---|
| **5.3.0** | 10/11/2026 | 3.13 | la dernière ; version ordinaire, plus suivie après la 5.4 |
| 5.2.4 LTS | 10/11/2026 | 3.13 | suivie deux ans ; mêmes dépendances à peu près |
| 4.5.16 LTS | 10/11/2026 | 3.11 | ancienne ; pas d'intérêt |

Les dépendances lues ici sont celles de la **5.3.0** (`build_files/build_environment/cmake/versions.cmake`, clone
partiel dans `~/korin-blender/src` sous WSL). **Recommandation : la 5.2 LTS**, une base qui ne bouge plus pendant un
port long ; on passera à la LTS suivante d'un coup. La 5.3 se défend si l'utilisateur veut la dernière.

Blender se construit avec **GCC 14.2** (`RELEASE_GCC_VERSION`) : exactement la chaîne `x86_64-onyx-elf-14.2` de Korin.

## 2. Ce que Blender demande au système

### 2.1 L'affichage : Vulkan, pas OpenGL

Blender dessine son interface et ses vues (EEVEE, Workbench) par un **moteur GPU** à choisir : OpenGL 4.3 *de
bureau* (profil core) ou **Vulkan** (`WITH_VULKAN_BACKEND`, actif par défaut sous Linux depuis la 5.x). Korin PC n'a
**pas** d'OpenGL de bureau (Mesa iris construit en GL ES seulement, `tools/mesa/configure-iris.sh` : `-Dopengl=false`),
mais il a **Vulkan** par anv (VkKit, [VULKAN.md](VULKAN.md)), déjà éprouvé par Godot en Forward+.

**Choix proposé : Vulkan seul** (`WITH_OPENGL_BACKEND=OFF`, donc pas d'epoxy). Ce que le moteur Vulkan de Blender
5.3 exige (`source/blender/gpu/vulkan/vk_backend.cc`) :

| Exigence | anv sur le Gen12 du ZBook |
|---|---|
| Vulkan 1.1 au moins | 1.4 |
| `VK_KHR_swapchain` | oui, par la WSI de Korin (surface headless + `VkKorinSurfaceInfo`) |
| `VK_KHR_dynamic_rendering`, `timeline_semaphore`, `buffer_device_address`, `separate_depth_stencil_layouts` | oui (cœur de 1.2/1.3) |
| geometry shaders, dual source blend, image cube array, stores et atomiques en fragment, `drawIndirectFirstInstance` | oui sur Gen12 |
| facultatif : `dynamic_rendering_local_read`, `graphics_pipeline_library`, `host_image_copy`, `external_memory_fd`… | à relever au premier lancement (`vulkaninfo` ou le journal de Blender) |

Blender compile ses shaders GLSL en SPIR-V **à l'exécution**, par **shaderc** (glslang) : une bibliothèque C++ de plus,
sans besoin du système. Il garde un cache de SPIR-V sur disque : le premier lancement sera lent, les suivants non.

**L'essai sans GPU.** QEMU n'a pas de GPU Intel : anv n'y fait qu'un essai à blanc. Pour essayer l'interface sous
QEMU, il faudrait **lavapipe** (Vulkan sur le processeur, de Mesa, par LLVM ; LLVM est déjà construit pour Korin
avec Rust). Option à décider au jalon 3 : sans lui, chaque essai de l'interface se fait sur le ZBook.

### 2.2 Le fenêtrage : GHOST

Blender parle au système par **GHOST** (`intern/ghost`) : `GHOST_System*` (événements, écrans, presse-papier,
curseurs, minuteries), `GHOST_Window*` (fenêtres, titre, taille, état), `GHOST_Context*` (GL ou Vulkan). Il existe
des versions X11, Wayland, Win32, Cocoa, **SDL** et **Headless**.

* **SDL** est tentant (SDL 3 est porté, [SDL.md](../SDL.md)) mais il ne convient pas : sa version de Korin n'a pas
  Vulkan, et GHOST SDL est une version « de développement » (une fenêtre, pas de glisser-déposer, presse-papier
  réduit).
* **Proposé : `GHOST_SystemKorin` et `GHOST_WindowKorin` sur UIKit**, comme la plateforme `korin` de Godot
  (`tools/godot/platform/korin` : `display_server_korin.cpp`, `korin_ui.c`, `rendering_context_driver_vulkan_korin.cpp`).
  Le contexte : `GHOST_ContextVK` avec une branche Korin pour la surface (comme sa branche SDL), le reste du contexte
  tel quel.

Ce que GHOST demande à Korin, par ordre d'importance :

| Besoin | Korin |
|---|---|
| plusieurs fenêtres de premier niveau (Blender ouvre préférences, rendu, éditeurs détachés dans des fenêtres) | UIKit multi-fenêtres (bloc par fenêtre) |
| souris : position, boutons, **molette fine et pavé tactile** (zoom, rotation de la vue), capture et **déplacement du curseur** (`setCursorPosition`, rotation continue) | à vérifier : SDL notait l'absence de souris relative et de capture (`SDL.md` § 7) |
| clavier : touches physiques (raccourcis), modificateurs, répétition, texte saisi (accents AZERTY) | codes de touche physiques connus (ports Linux, mémoire `armpi-linux-ports`) |
| curseurs standard et personnalisés (bitmap) | à vérifier dans UIKit |
| presse-papier texte (et image) | presse-papier du système |
| taille de l'écran, échelle (DPI), plein écran | `WEV_SCREEN`, plein écran d'Elegant |
| glisser-déposer de fichiers depuis le gestionnaire | plus tard (idée notée dans la mémoire) |
| dialogue de fichiers | **inutile** : Blender a son propre navigateur |

Ce qui manque à UIKit s'y ajoute en fin de `uikit.abi` (règle des ports), pas dans Blender.

### 2.3 POSIX

Blender est un gros programme C/C++ multithread ordinaire. Points à surveiller, connus des ports précédents :

* **Threads** : pthread, TLS (`thread_local`), beaucoup de threads (TBB) : déjà éprouvé par Godot et WebKit.
* **TBB** (oneTBB 2022.3) : ses chemins `__linux__` (futex, `sched_getaffinity`, `/proc`) ne seront pas pris,
  comme pour tout code tiers sous Korin (`__linux__` n'est pas défini) ; il faudra peut-être un chemin générique
  pthread, ou ajouter `sched_getaffinity` à libposix.
* **Mémoire** : Blender réserve beaucoup ; l'allocateur de TBB (`WITH_TBB_MALLOC_PROXY`) remplace malloc : à couper
  d'abord.
* **dlopen** : existe ([dlopen](../../.agents/memory/armpi-dlopen.md)) ; Python s'en sert pour ses modules en .so
  (numpy).
* **Fichiers** : `std::filesystem`, `realpath`, liens absents (Blender sait s'en passer), sauvegarde par
  renommage (`rename` atomique), surveillance de fichiers (pas d'inotify : Blender s'en passe sur les plateformes
  sans).
* **Chemins de Blender** (`GHOST_SystemPathsUnix`) : réglages dans `~/.config/blender/5.x`, scripts et données à côté
  de l'exécutable (`WITH_INSTALL_PORTABLE`) : à poser sur l'arbre de Korin (`/apps/blender.app/…`, `HOME:/.config`).
* **Python embarqué** : Blender exige **Python 3.13** exactement (`PYTHON_VERSION 3.13.13`) ; Korin a CPython 3.14
  ([PYTHON-PORT.md](../PYTHON-PORT.md)) : on refait la même construction en 3.13, avec les mêmes retouches. numpy
  2.3 se construit par meson et Cython (croisé, comme les roues de l'étape 3 de Python).

## 3. Les dépendances

Versions de la 5.3.0. **Classe A** : sans elles, pas de Blender utile. **B** : fonctions importantes, à ajouter au
jalon 4. **C** : plus tard ou jamais. « Korin » : déjà construit pour Korin PC (sysroot de Jet, paquets `-dev`,
ports précédents).

| Bibliothèque | Version | Classe | Korin | Remarque |
|---|---|---|---|---|
| zlib, libpng, freetype, libwebp, libxml2, sqlite, OpenSSL, brotli | — | A | **oui** (`<lib>-dev`) | freetype doit avoir brotli (Blender le vérifie) |
| libjpeg-turbo | 2.1.3 | A | probable (Jet) | à vérifier |
| Python + numpy | 3.13.13, 2.3.4 | A | 3.14 seulement | refaire en 3.13 |
| OpenImageIO | 3.1.16 | A | non | **obligatoire** (`find_package(OpenImageIO REQUIRED)`) |
| OpenEXR, Imath, libdeflate | 3.4.10, 3.2.2, 1.18 | A | non | pour OIIO |
| libtiff | 4.7.1 | A | non | pour OIIO |
| OpenColorIO (+ yaml-cpp, pystring, expat, minizip-ng) | 2.5.0 | A | non | gestion des couleurs ; tout Blender s'en sert |
| fmt, robin-map | 12.1, 1.3.0 | A | non | en-têtes surtout |
| oneTBB | 2022.3 | A | non | sans lui Blender tourne, mais sur un seul cœur par endroits |
| shaderc (glslang, SPIRV-Tools) | — | A | non | Vulkan |
| en-têtes Vulkan | — | A | **oui** (VkKit) | `build/pc/mesa/include/vulkan` |
| Eigen, Bullet, libmv, Mantaflow, audaspace | — | A | dans `extern/` | construits avec Blender |
| FFTW3 | 3.3.10 | B | non | océan, compositeur |
| OpenSubdiv | 3.7.0 | B | non | subdivision sur GPU/CPU |
| Manifold | 3.5.2 | B | non | booléens rapides |
| Embree | 4.4.1 | B | non | accélère Cycles (ISPC non requis) |
| OpenImageDenoise | — | B | non | débruitage de Cycles (gros : ISPC, oneDNN) |
| OpenVDB (+ blosc) | 13.0 | B | non | volumes |
| Alembic, USD, MaterialX | 1.8.3, 26.08, 1.39.4 | B/C | non | import/export ; USD est très gros (Boost) |
| FFmpeg (+ x264, vpx, opus…) | 8.1 | B | Jet a des décodeurs | vidéo du séquenceur |
| son (OpenAL / SDL / Jack / PulseAudio) | — | B | AudioKit | une sortie audaspace sur AudioKit ou par SDL 3 |
| OSL, LLVM | 1.15.6, 22.1.7 | C | LLVM de Rust (autre version) | shaders OSL de Cycles |
| Cycles sur GPU (CUDA, HIP, oneAPI, Metal) | — | C | — | rien sur l'Intel UHD ; NVIDIA un jour |
| OpenXR, 3Dconnexion (spnav), Jack, Wayland, X11 | — | C | — | coupés |

Elles se construisent **dans WSL, en croisé** (comme OCCT : `tools/cao/korin.cmake`, CMake voit « Linux » sans
`__linux__`), en bibliothèques statiques, rangées dans `~/korin-blender/x86_64/deps`. Blender a ses propres
recettes (`build_files/build_environment`, un superbuild par `ExternalProject`) : on peut s'en servir avec notre
fichier de chaîne, ou écrire nos scripts comme pour Godot et OCCT. **Proposé : nos scripts**, une bibliothèque par
fonction de `tools/blender/deps.sh`, avec les options et les versions de Blender ; le superbuild suppose trop de
choses sur l'hôte (Python, meson, `ldd`).

## 4. Le chemin

### Jalon 0 : la base (fait le 23/11)

* `tools/blender/env.sh` : version (5.2.4), dossiers sous `~/korin-blender` (WSL), `JOBS` au plus 4, liste
  `DEPS_A`, fonction `cmake_korin` (configure, construit et installe une dépendance dans `x86_64/deps`).
* `tools/blender/korin.cmake` : fichier de chaîne, repris de la CAO, avec `-g0` et `--gc-sections`.
* `tools/blender/versions.py` : lit les URL et les sommes dans `versions.cmake` de Blender, qui fait foi (rien n'est
  recopié chez nous).
* `tools/blender/fetch.sh` : la source de Blender (somme du `.sha256` publié) et les archives de `DEPS_A`
  (`sh tools/blender/fetch.sh [NOM…]`).
* Les retouches de Blender iront dans `tools/blender/korin.patch` (comme Godot), créé au premier besoin.

### Jalon 1 : les dépendances A (fait le 23-24/11)

`sh tools/blender/deps.sh [nom…]` (WSL, depuis la racine du dépôt) construit tout en statique dans
`~/korin-blender/x86_64/deps`, une fonction par bibliothèque, avec les options des recettes de Blender
(`build_files/build_environment/cmake/<nom>.cmake`), sans Python ni outils ; une bibliothèque faite (marque
`obj/<nom>/.fait`) est sautée, `FORCE=1` la refait. `sh tools/blender/python.sh` fait Python.

| Bibliothèque | Durée (JOBS=4) | Remarque |
|---|---|---|
| zlib, libpng, libjpeg, libwebp, freetype, brotli | — | **recopiées** du sysroot de Jet (`jetlibs`), même processeur (`-march=tigerlake -mno-avx512f`) |
| Imath, libdeflate, OpenEXR, libtiff | 2 min | OpenEXR : retouche (voir plus bas) ; pièges des flottants par signal coupés |
| fmt, robin-map, expat, yaml-cpp, pystring, minizip-ng | 30 s | pystring compilé à la main (pas d'installation) |
| OpenColorIO 2.5 | 1 min | retouche (`alloca`) |
| oneTBB 2022.3 | 30 s | statique, tbbmalloc sans proxy ; **pas encore essayé dans Korin** (chemins sans `__linux__`) |
| OpenImageIO 3.1.13 | 6 min | retouche ; sans OpenJPEG, OpenJPH, libheif, FFmpeg, freetype (à ajouter au besoin) |
| shaderc v2025.4 (glslang, SPIRV-Tools), SPIRV-Reflect, VMA, en-têtes Vulkan 1.4.341 | 5 min | seulement `libshaderc_combined.a` : la cible `.so` ne se lie pas |
| CPython 3.13.13 | 8 min | `tools/python/build.sh` avec `PYVER`, `PYPATCH`, `STATIC_LIBPY=1` : `libpython3.13.a` (12,5 Mo) et la bibliothèque standard ; un Python 3.13 de l'hôte est construit d'abord |
| numpy 2.3.4 | — | **reporté au jalon 4** : se construit par meson (fichier croisé à écrire) ; Blender démarre sans (`WITH_PYTHON_NUMPY=OFF`) |

Les retouches (`tools/blender/patches/<nom>.patch`, posées par `deps.sh`, marque `.korin-patch` dans la source) :

* **OpenEXR** : `<threads.h>` existe dans la chaîne (newlib) mais pas `machine/_threads.h` : la voie pthread,
  comme FreeBSD (`!defined(__armpi__)`).
* **OpenColorIO** : `alloca` dans `argparse.cpp`. En C++, `<cstdlib>` va droit au `stdlib.h` de newlib par
  `#include_next` sans passer par celui de libposix, et newlib ne déclare pas `alloca` en `-std=c++17` strict.
  libposix déclare maintenant `alloca` dans son `stdlib.h` sous `_GNU_SOURCE` (comme glibc), ce qui suffit au C.
* **OpenImageIO** : Korin rangé avec Linux pour `strcasecmp_l` et la locale « C » (newlib les a), avec GNU/Hurd
  pour `sysutil` (en-têtes POSIX ; pas de chemin de l'exécutable, faute de `/proc`).
* **Python 3.13** : `patches/python-3.13.patch`, les retouches de `tools/python/korin.patch` refaites pour la 3.13
  (le morceau de `strerror_r` n'y sert pas).

Pièges :

* **Jamais `-I` vers le sysroot de Jet** : il contient des copies des en-têtes de libposix (`fcntl.h`…), et leur
  `#include_next` y retombe au lieu d'aller à newlib (`off_t` inconnu). Les bibliothèques de Jet sont recopiées.
* `-specs=posix.specs` une seule fois : CMake reprend les `CMAKE_<LANG>_FLAGS` sur la ligne de liaison.
* Le `build/pc/posix` peut retarder sur la source de libposix : `ARCH=x86_64 sh tools/posix/build.sh`.
* `wsl bash -c '… $X …'` développe `$X` avant bash : lancer des scripts (`wsl -- sh tools/blender/…`).

Rien n'a encore tourné dans Korin : la vraie épreuve de ces bibliothèques est le jalon 2.

### Jalon 2 : Blender sans écran (le premier vrai résultat)

`WITH_HEADLESS=ON`, `WITH_CYCLES=ON`, sans Embree ni OSL, `WITH_VULKAN_BACKEND=OFF`, `WITH_OPENGL_BACKEND=OFF`,
`WITH_INSTALL_PORTABLE=ON`, tout le facultatif coupé. Essais sous QEMU PC :

1. `blender -b --version`, puis `blender -b --python-expr "import bpy; print(bpy.app.version_string)"` ;
2. `blender -b -f 1` sur une scène simple : un rendu Cycles CPU en PNG, comparé à celui de Blender sous Linux
   (même scène, mêmes échantillons) ;
3. un script qui crée un objet, applique des modificateurs et enregistre un .blend, rouvert par Blender sous Windows.

C'est la preuve que le moteur, Python et Cycles tournent ; tout ce qui suit est l'affichage.

**Fait le 24/11.** Construire (WSL, depuis la racine du dépôt) :

```sh
sh tools/blender/deps.sh            # les dépendances (une fois)
sh tools/blender/python.sh          # CPython 3.13 (une fois)
sh tools/blender/hosttools.sh       # datatoc, shader_tool, makesdna, makesrna pour l'hôte (une fois)
JOBS=6 sh tools/blender/build.sh    # Blender : ~/korin-blender/x86_64/install-headless (blender 148 Mo, arbre 254 Mo)
```

Essayer (KVM : `-u root`) : `. ~/korin-cloud/env.sh` (fasm pour Linux, OVMF avec le shell UEFI), puis
`python3 -I tools/blender/pctest.py prep --mb 1200` et
`python3 -I tools/blender/pctest.py run --qemu "-accel kvm" "chmod +x /apps/blender/blender" "@600 /apps/blender/blender -b --factory-startup --python-exit-code 1 --python /home/blender/scene.py" "@900 /apps/blender/blender -b --factory-startup --python-exit-code 1 --python /home/blender/render.py -- /tmp/cube.png 16"`.

| Essai | Résultat (QEMU PC, KVM, 4 cœurs, 4 Go) |
|---|---|
| `blender -b --version` | `Blender 5.2.4 LTS` |
| `test/scene.py` : singe, modificateurs, matériau, enregistré puis relu | `SCENE OK` (12 s, démarrage compris) ; la subdivision est sans effet (pas d'OpenSubdiv : jalon 4) |
| `test/render.py` : Cycles sur le processeur, 320 × 240, 16 échantillons | `RENDU OK` : 0,7 s sur 4 fils, pixel central gris (0,545) |

Ce qu'il a fallu, en plus du jalon 1 :

* **Les outils de la construction** (`datatoc`, `shader_tool`, `makesdna`, `makesrna`) sont lancés par la construction
  elle-même : compilés pour Korin, ils plantent sous Linux. `hosttools.sh` les construit pour l'hôte (dépendances
  en `HOST=1`, `host-fix.cmake`), et la retouche de Blender met `${CMAKE_CROSSCOMPILING_EMULATOR}` devant leurs
  appels ; `build.sh` y met `hosttool.sh`, qui lance l'outil de même nom de l'hôte.
* **Retouches de Blender** (`patches/blender.patch`, tirée par `mkpatch.sh` de la source mise sous git) :
  `OS_KORIN` dans `BLI_build_config.h` (POSIX générique), `statvfs` pour l'espace libre, pas de `-lc` ni de
  `-latomic` (variable `KORIN` de `korin.cmake`), `oiiotool` facultatif, brotli cherché sous Korin.
* **libposix** : `off64_t` (`sys/types.h`), `timespec_get` et `TIME_UTC` (`time.h`, `time.c`), `alloca` en mode
  strict (`machine/stdlib.h`, vu aussi du `<cstdlib>` du C++) ; essai dans `posixtest` (groupe `time`), pas encore
  lancé dans Korin.
* **Dépendances de plus** : Zstd, Epoxy (meson croisé : `meson_korin`), Eigen, OpenJPH (OpenEXR la cite dans sa
  config), brotli et FreeType 2.13.3 (celle de Jet n'a pas le cache `FTC_*`).
* **Python** : les bibliothèques de ses modules intégrés (`lib/korin-deps` : mpdec, HACL, expat, sqlite, ffi,
  OpenSSL) liées après `libpython3.13.a` ; deux symboles de l'expat de CPython renommés (`PyExpat_*`) pour ne pas
  heurter celle d'OpenColorIO ; `pymodules.sh` pose les paquets purs que Blender livre (attrs, cattrs, requests…).
* **OpenImageIO** : la mémoire physique par `sysconf(_SC_PHYS_PAGES)`.
* **Essai** : le binaire copié sur la FAT demande `chmod +x` (comme Godot) ; l'arbre d'essai est dans WSL
  (`~/korin-blender/blendertest`), les chemins de `/devtools` dépassant 260 caractères sous `/mnt/c`.

### Jalon 3 : l'interface

* GHOST Korin sur UIKit (§ 2.2), contexte Vulkan sur la WSI de Korin ; `WITH_VULKAN_BACKEND=ON`.
* Sous QEMU : la fenêtre, les événements et les menus seulement si lavapipe est construit ; sinon essais sur le ZBook
  (paquet d'essai par la mise à jour habituelle).
* Critère : ouvrir Blender, l'écran de démarrage, le cube par défaut, tourner la vue, passer en Edit Mode, rendre
  une image par F12 (Cycles CPU, EEVEE sur le GPU).

### Jalon 4 : les fonctions (au fil des besoins)

Embree et OpenSubdiv d'abord (vitesse), puis Manifold, FFTW3, OIDN, le son sur AudioKit, FFmpeg, OpenVDB, puis les
formats d'échange. Chacun ajouté sans casser les autres.

### Jalon 5 : la livraison

Paquet `blender` (gros : par la Release, comme Godot), `app.txt` (catégorie Graphisme, clés de PocketQI), icône,
compétence `korin-app-blender` pour piloter Blender par Python (`blender -b --python`), ce que Claude Code et Vibe
savent déjà faire.

## 5. Risques

| Risque | Poids | Parade |
|---|---|---|
| **Taille de la construction** : Blender + dépendances, des heures ; l'édition de liens demande plusieurs Go ; WSL n'a que 9 Go | fort | `JOBS=4` (un WSL de 7,8 Go est déjà tombé à -j12), une seule grosse construction à la fois, `-g0`, liens par `ld.gold` ou `mold` si la chaîne le permet |
| **Vitesse du GPU Intel UHD (TGL GT1)** : EEVEE est lourd | moyen | Workbench et Cycles CPU d'abord ; EEVEE en résolution réduite ; la NVIDIA plus tard (NVK) |
| **Le moteur Vulkan sur anv de Korin** : WSI maison, mémoire du faux libdrm, beaucoup de pipelines | moyen | journal de Blender (`--debug-gpu`), essais par étapes, lavapipe sous QEMU |
| **TBB et la mémoire** (beaucoup de threads, gros `mmap`) | moyen | malloc de libposix d'abord, TBB réglé au nombre de cœurs |
| **Python 3.13 exact** | faible | la construction 3.14 se reprend ; numpy par meson croisé |
| GHOST : souris capturée, curseur déplacé par le programme | moyen | à ajouter à UIKit et Elegant s'ils manquent |
| Licence | faible | Blender est GPL-3 ; paquet séparé avec ses sources citées, comme les autres ports GPL |

## 6. Décisions de l'utilisateur (23/11)

| Question | Réponse |
|---|---|
| La version | **Blender 5.2 LTS** (5.2.4 aujourd'hui) |
| Le moteur GPU | **Vulkan seul** ; OpenGL de bureau peut-être un jour, pas maintenant |
| Les essais de l'interface sous QEMU | **lavapipe** : Vulkan sur le processeur, construit pour Korin |
