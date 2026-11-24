# GHOST Korin : les retouches de CMake et de GHOST_ISystem.cc

Ce qu'il faut changer dans la source de Blender 5.2.4 pour construire GHOST sur UIKit (`WITH_GHOST_KORIN`).
Numéros de ligne : ceux de la 5.2.4 d'origine. Les retouches de fichiers existants vont dans
`tools/blender/patches/blender.patch` (`tools/blender/mkpatch.sh`) ; les sources nouvelles sont copiées (voir § 5).

## 1. `CMakeLists.txt` (racine)

### 1.1 L'option (après celle de SDL, l. 337-341)

```cmake
option(WITH_GHOST_SDL "\
Enable building Blender against SDL for windowing rather than the native APIs"
  OFF
)
mark_as_advanced(WITH_GHOST_SDL)

# Korin OS : fenetres et evenements par UIKit (tools/blender/ghost), dessin par Vulkan (VkKit).
# KORIN vient du fichier de chaine (tools/blender/korin.cmake).
if(KORIN)
  option(WITH_GHOST_KORIN "Enable building Blender against UIKit (Korin OS) for windowing" ON)
else()
  set(WITH_GHOST_KORIN OFF)
endif()
mark_as_advanced(WITH_GHOST_KORIN)
```

(`WITH_GHOST_X11` et `WITH_GHOST_WAYLAND` sont encore déclarées par le bloc `if(UNIX AND NOT (APPLE OR HAIKU))`
qui suit, l. 343-366 : CMake voit un « Linux » ; on les éteint par le § 1.2 et par le cache, § 4.)

### 1.2 Les incompatibilités (avec celles du sans-écran, l. 1349-1361)

Après :

```cmake
if(UNIX AND NOT (APPLE OR HAIKU))
  set_and_warn_incompatible(WITH_HEADLESS WITH_GHOST_WAYLAND OFF)
  set_and_warn_incompatible(WITH_HEADLESS WITH_GHOST_X11 OFF)
endif()
set_and_warn_incompatible(WITH_HEADLESS WITH_GHOST_SDL OFF)
```

ajouter :

```cmake
set_and_warn_incompatible(WITH_HEADLESS WITH_GHOST_KORIN OFF)
if(WITH_GHOST_KORIN)
  # Korin OS : UIKit seul (pas de X11, de Wayland ni de SDL), Vulkan seul (pas d'OpenGL de bureau),
  # pas de methode de saisie ni d'OpenXR.
  set_and_warn_incompatible(WITH_GHOST_KORIN WITH_GHOST_X11 OFF)
  set_and_warn_incompatible(WITH_GHOST_KORIN WITH_GHOST_WAYLAND OFF)
  set_and_warn_incompatible(WITH_GHOST_KORIN WITH_GHOST_SDL OFF)
  set_and_warn_incompatible(WITH_GHOST_KORIN WITH_OPENGL_BACKEND OFF)
  set_and_warn_incompatible(WITH_GHOST_KORIN WITH_INPUT_IME OFF)
  set_and_warn_incompatible(WITH_GHOST_KORIN WITH_XR_OPENXR OFF)
endif()
```

(`set_and_warn_incompatible` ne touche à une option que si elle est définie : `WITH_GHOST_X11` et
`WITH_GHOST_WAYLAND` le sont, § 1.1.)

### 1.3 Le résumé de la configuration (l. 2554-2555)

```cmake
  info_cfg_option(WITH_GHOST_DEBUG)
  info_cfg_option(WITH_GHOST_SDL)
  info_cfg_option(WITH_GHOST_KORIN)
```

## 2. `intern/ghost/CMakeLists.txt`

Une branche de plus dans la chaîne `if(WITH_HEADLESS) … elseif(WITH_GHOST_SDL) …`, juste après le sans-écran
(l. 136-138) :

```cmake
if(WITH_HEADLESS)
  add_definitions(-DWITH_HEADLESS)
elseif(WITH_GHOST_KORIN)
  # Korin OS : GHOST sur UIKit (sources copiees de tools/blender/ghost par tools/blender/build.sh).
  list(APPEND SRC
    intern/GHOST_SystemKorin.cc
    intern/GHOST_WindowKorin.cc
    intern/ghost_korin_ui.c

    intern/GHOST_SystemKorin.hh
    intern/GHOST_WindowKorin.hh
    intern/ghost_korin_ui.h
  )
  add_definitions(-DWITH_GHOST_KORIN)

  # UIKit : uikit.h et uikit_abi.h (genere) sont dans build/pc/gcc/include/kits (tools/pc/apps.sh les y met),
  # libuikit.a dans build/pc/gcc/lib : les chemins viennent de tools/blender/build.sh (§ 4).
  list(APPEND INC_SYS
    ${KORIN_UIKIT_INCLUDE_DIRS}
  )
  list(APPEND LIB
    ${KORIN_UIKIT_LIBRARY}
  )
elseif(WITH_GHOST_SDL)
```

Le reste du fichier convient tel quel : `UNIX` est vrai, donc `GHOST_SystemPathsUnix` (l. 499-503) ;
`GHOST_ContextVK.cc` vient de `WITH_VULKAN_BACKEND` (l. 85-97) ; `GHOST_ContextEGL` n'est pris qu'avec OpenGL
(l. 473-481), éteint.

`add_definitions(-DWITH_GHOST_KORIN)` ne vaut que pour `intern/ghost` : c'est assez, `GHOST_ContextVK.hh` n'est
inclus que par des sources de GHOST (vérifié dans la 5.2.4 : `intern/ghost/intern/*` seulement). Rien à
définir dans `source/blender/windowmanager` (ses `WITH_GHOST_WAYLAND` ne concernent que Wayland).

## 3. `intern/ghost/intern/GHOST_ISystem.cc`

### 3.1 L'en-tête (l. 18-31)

```cpp
#if defined(WITH_GHOST_X11) && defined(WITH_GHOST_WAYLAND)
#  include "GHOST_SystemWayland.hh"
#  include "GHOST_SystemX11.hh"
#elif defined(WITH_GHOST_X11)
#  include "GHOST_SystemX11.hh"
#elif defined(WITH_GHOST_WAYLAND)
#  include "GHOST_SystemWayland.hh"
#elif defined(WITH_GHOST_KORIN)
#  include "GHOST_SystemKorin.hh"
#elif defined(WITH_GHOST_SDL)
#  include "GHOST_SystemSDL.hh"
```

(l'ajout : les deux lignes `WITH_GHOST_KORIN`, avant SDL, l. 25.)

### 3.2 `createSystem` (avant `#elif defined(WITH_GHOST_SDL)`, l. 143)

```cpp
#elif defined(WITH_GHOST_KORIN)
    backends_attempted.push_back({"KORIN"});
    try {
      CLOG_INFO(&LOG, "Create Korin system");
      system_ = new GHOST_SystemKorin();
    }
    catch (const std::runtime_error &e) {
      /* (pas d'UIKit : le bureau Elegant ne tourne pas ; `blender -b` marche sans) */
      if (verbose) {
        backends_attempted.back().failure_msg = e.what();
      }
      delete system_;
      system_ = nullptr;
    }
#elif defined(WITH_GHOST_SDL)
```

`GHOST_ISystem::getSystemBackend()` rend alors `"KORIN"` (l. 167). Le reste de `createSystem` (le message
d'échec, `init()`) ne change pas.

## 4. La configuration et les chemins (tools/blender)

### 4.1 Un cache pour l'interface : `tools/blender/config-gui.cmake` (à écrire au jalon 3)

Le cache du jalon 2 (`config-headless.cmake`) avec, à la place du bloc « Sans écran ni GPU » :

```cmake
set(WITH_HEADLESS            OFF CACHE BOOL "" FORCE)
set(WITH_GHOST_KORIN         ON  CACHE BOOL "" FORCE)
set(WITH_VULKAN_BACKEND      ON  CACHE BOOL "" FORCE)
set(WITH_OPENGL_BACKEND      OFF CACHE BOOL "" FORCE)
set(WITH_GHOST_X11           OFF CACHE BOOL "" FORCE)
set(WITH_GHOST_WAYLAND       OFF CACHE BOOL "" FORCE)
set(WITH_GHOST_SDL           OFF CACHE BOOL "" FORCE)
set(WITH_XR_OPENXR           OFF CACHE BOOL "" FORCE)
set(WITH_INPUT_IME           OFF CACHE BOOL "" FORCE)
set(WITH_INPUT_NDOF          OFF CACHE BOOL "" FORCE)
set(WITH_RENDERDOC           OFF CACHE BOOL "" FORCE)
```

### 4.2 UIKit : où sont l'en-tête et la bibliothèque

Comme Godot (`tools/godot/build.sh` : `KORIN_KITS=$ROOT/build/pc/gcc` ; `tools/godot/platform/korin/detect.py` :
`kits/include/kits`, `src/kits/uikit/include`, `kits/lib/libuikit.a`) :

| Quoi | Où | Fait par |
|---|---|---|
| `uikit.h` (en-tête C d'UIKit) | `src/kits/uikit/include/uikit.h`, copié dans `build/pc/gcc/include/kits/` | `tools/pc/apps.sh` |
| `uikit_abi.h` (prototypes et constantes générés : `ui_*`, `WF_*`, `WEV_*`, `EV_*`) | `build/pc/gcc/include/kits/` | `tools/pc/apps.sh` (`abi2c.py`) |
| `libuikit.a` (relais vers `/lib/uikit.so`) | `build/pc/gcc/lib/libuikit.a` | `tools/pc/apps.sh` |

À ajouter à la ligne `cmake` de `tools/blender/build.sh` (`$ROOT` : la racine du dépôt, donnée par `env.sh`) :

```sh
	-DKORIN_UIKIT_INCLUDE_DIRS="$ROOT/build/pc/gcc/include/kits;$ROOT/src/kits/uikit/include" \
	-DKORIN_UIKIT_LIBRARY="$ROOT/build/pc/gcc/lib/libuikit.a" \
```

et une vérification en tête, comme Godot :

```sh
[ "$CONFIG" = headless ] || [ -f "$ROOT/build/pc/gcc/lib/libuikit.a" ] || \
	{ echo "build.sh : d'abord sh tools/pc/apps.sh (libuikit.a)" >&2; exit 1; }
```

### 4.3 Vulkan

* **En-têtes** : `build/pc/mesa/include` (ceux de Mesa, avec `vulkan/vulkan_korin.h` ; `env.sh` le donne déjà
  dans `$VKINC`). `WITH_LIBS_PRECOMPILED=OFF`
  fait passer `platform_unix.cmake` par `pkg_check_modules(VULKAN REQUIRED vulkan)` (l. 165-168) : soit un
  `vulkan.pc` dans le préfixe des dépendances (`KORIN_BLENDER_DEPS`) qui donne ces en-têtes et la bibliothèque
  ci-dessous, soit une retouche de `platform_unix.cmake` (`elseif(KORIN)` : `VULKAN_INCLUDE_DIRS`,
  `VULKAN_LIBRARIES` posés à la main). Même chose pour **shaderc** (à construire, jalon 3).
* **Bibliothèque** : `build/pc/mesa/lib/libvkkit.a` (VkKit). **Point dur, à régler avant la liaison** : son
  chargeur minimal (`tools/mesa/vkkit/vkkit_loader.c`) n'exporte que `vkGetInstanceProcAddr`, `vkCreateInstance`
  et `vkEnumerateInstance*` ; Godot passe par volk. Blender appelle les fonctions de Vulkan **en direct**
  (`vkCreateSwapchainKHR`, `vkCmdDraw`, `vkDestroySurfaceKHR`… dans `GHOST_ContextVK.cc` et
  `source/blender/gpu/vulkan`) : il faut des trampolines pour tout le cœur 1.0-1.3 et les extensions qu'il lie
  (générés depuis `vk.xml`, ajoutés au chargeur de VkKit), ou faire passer Blender par volk. `GHOST_ContextVK` de
  Korin prend déjà `vkCreateHeadlessSurfaceEXT` par `vkGetInstanceProcAddr`.
* Ordre de liaison : `libuikit.a` et `libvkkit.a` après les objets (Godot : dans `LIBS`, fin de ligne) ; GHOST les
  met dans `LIB` de `bf_intern_ghost`, CMake les répète en fin de ligne.

## 5. Les sources nouvelles

`tools/blender/mkpatch.sh` fait un `git diff` : les fichiers **non suivis** n'y entrent pas. Les six sources de
`tools/blender/ghost/` sont donc copiées dans `intern/ghost/intern/` par `tools/blender/build.sh`, après la
retouche :

```sh
# GHOST pour Korin (tools/blender/ghost) : sources nouvelles, hors de blender.patch
for f in GHOST_SystemKorin.cc GHOST_SystemKorin.hh GHOST_WindowKorin.cc GHOST_WindowKorin.hh \
	ghost_korin_ui.c ghost_korin_ui.h; do
	cmp -s "$BL/ghost/$f" "$BSRC/intern/ghost/intern/$f" || cp "$BL/ghost/$f" "$BSRC/intern/ghost/intern/$f"
done
```

(`cmp` avant `cp` : ninja ne recompile pas ce qui n'a pas changé.) Les fichiers `.md` de ce dossier ne sont pas
copiés.
