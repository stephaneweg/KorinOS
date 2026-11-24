# GHOST_ContextVK : la branche `WITH_GHOST_KORIN`

Ce qu'il faut ajouter à `intern/ghost/intern/GHOST_ContextVK.hh` et `GHOST_ContextVK.cc` de Blender 5.2.4 pour
que `GHOST_WindowKorin` dessine par Vulkan dans une fenêtre d'UIKit. Les numéros de ligne sont ceux de la
source 5.2.4 d'origine (avant toute retouche) ; ils se décalent au fil des insertions : chercher le texte cité.
Les retouches vont dans `tools/blender/patches/blender.patch` par `tools/blender/mkpatch.sh` (retoucher la source
sous git, puis relancer `mkpatch.sh`).

## Le principe

Pas d'extension de surface propre à Korin : une surface de **`VK_EXT_headless_surface`** avec, dans le `pNext` de
`VkHeadlessSurfaceCreateInfoEXT`, la structure **`VkKorinSurfaceInfo`** (`vulkan_korin.h`, VkKit) qui nomme la
fenêtre d'Elegant. La WSI de Korin (`tools/mesa/wsi/wsi_common_korin.c`) montre alors les images de la chaîne
d'échange dans cette fenêtre. C'est le montage de Godot (`tools/godot/platform/korin/
rendering_context_driver_vulkan_korin.cpp`), éprouvé en Forward+.

* **Plateforme** : une valeur de plus, `GHOST_kVulkanPlatformKorin`, dans la branche « Linux » du contexte
  (`platform_`). Le constructeur ne change pas de signature : la poignée de la fenêtre d'UIKit passe par le
  paramètre `window` de X11 (`Window` vaut `void *` dans une construction sans X11, `GHOST_ContextVK.hh` l. 25-26),
  et la taille de la fenêtre par `wayland_window_info` (un `GHOST_ContextVK_WindowInfo`, déjà fait pour une WSI
  qui ne connaît pas la taille de sa surface).
* **Taille de la chaîne d'échange** = taille de la fenêtre : la surface headless rend `currentExtent =
  0xFFFFFFFF` ; Blender prend alors `wayland_window_info_->size` (l. 1412-1418), et refait la chaîne quand la
  taille change (l. 928-944). Ces deux blocs, gardés par `WITH_GHOST_WAYLAND`, s'ouvrent aussi à
  `WITH_GHOST_KORIN`. `GHOST_WindowKorin::setSize` tient `vulkan_window_info_` à jour (redimensionnement,
  plein écran).
* **Instance** : `VK_KHR_surface` + `VK_EXT_headless_surface` (au lieu de xlib/wayland), par
  `getPlatformSpecificSurfaceExtension`.
* **Pas de fonction liée en direct** : `vkCreateHeadlessSurfaceEXT` est prise par `vkGetInstanceProcAddr` (le
  chargeur minimal de VkKit, `tools/mesa/vkkit/vkkit_loader.c`, ne donne que `vkGetInstanceProcAddr` et les
  commandes globales ; voir aussi `CMAKE.md`, « Vulkan »).
* **Contexte hors écran** (`GHOST_SystemKorin::createOffscreenContext`) : `GHOST_kVulkanPlatformHeadless`, sans
  surface, comme `GHOST_SystemHeadless` : rien à changer.

## GHOST_ContextVK.hh

### 1. L'énumération des plateformes (l. 53-61)

Avant :

```cpp
enum GHOST_TVulkanPlatformType {
  GHOST_kVulkanPlatformHeadless = 0,
#ifdef WITH_GHOST_X11
  GHOST_kVulkanPlatformX11 = 1,
#endif
#ifdef WITH_GHOST_WAYLAND
  GHOST_kVulkanPlatformWayland = 2,
#endif
};
```

Après (ajout avant l'accolade fermante, l. 61) :

```cpp
#ifdef WITH_GHOST_WAYLAND
  GHOST_kVulkanPlatformWayland = 2,
#endif
#ifdef WITH_GHOST_KORIN
  /** Korin OS : surface headless + #VkKorinSurfaceInfo (VkKit). La poignee de la fenetre d'UIKit
   * passe par le parametre `window`, la taille par `wayland_window_info`. */
  GHOST_kVulkanPlatformKorin = 3,
#endif
};
```

Rien d'autre dans l'en-tête : `window_` (`Window`, donc `void *` sans X11) et `wayland_window_info_` existent déjà
(l. 249, 253).

## GHOST_ContextVK.cc

### 2. Les en-têtes (l. 17-24)

Avant :

```cpp
#else /* X11/WAYLAND. */
#  ifdef WITH_GHOST_X11
#    include <vulkan/vulkan_xlib.h>
#  endif
#  ifdef WITH_GHOST_WAYLAND
#    include <vulkan/vulkan_wayland.h>
#  endif
#endif
```

Après (ajout après le bloc Wayland, avant le `#endif` de la l. 24) :

```cpp
#  ifdef WITH_GHOST_WAYLAND
#    include <vulkan/vulkan_wayland.h>
#  endif
#  ifdef WITH_GHOST_KORIN
#    include <cstdint>
#    if __has_include(<vulkan/vulkan_korin.h>)
/* build/pc/mesa/include/vulkan/vulkan_korin.h (VkKit, docs/etude/VULKAN.md § 3) */
#      include <vulkan/vulkan_korin.h>
#    else
/* (la meme que tools/mesa/vkkit/include/vulkan/vulkan_korin.h, si Blender prend d'autres en-tetes
 * de Vulkan, comme Godot) */
#      define VK_STRUCTURE_TYPE_KORIN_SURFACE_INFO ((VkStructureType)0x4B4F5201)
typedef struct VkKorinSurfaceInfo {
  VkStructureType sType;
  const void *pNext;
  long window;
  int x, y;
} VkKorinSurfaceInfo;
#    endif
#  endif
#endif
```

### 3. Refaire la chaîne d'échange quand la fenêtre change de taille (`swapBufferAcquire`, l. 928-944)

Remplacer la garde de la l. 928 :

```cpp
#ifdef WITH_GHOST_WAYLAND
    /* Wayland doesn't provide a WSI with windowing capabilities, therefore cannot detect whether
```

par :

```cpp
#if defined(WITH_GHOST_WAYLAND) || defined(WITH_GHOST_KORIN)
    /* Wayland doesn't provide a WSI with windowing capabilities, therefore cannot detect whether
     * (Korin : pas plus, la surface headless ne connait pas la taille de la fenetre)
```

Le corps (`if (wayland_window_info_) { ... recreateSwapchain(use_hdr_swapchain); }`) et le `#endif` de la
l. 944 restent tels quels.

### 4. La taille de la chaîne d'échange (`recreateSwapchain`, l. 1412-1418)

Remplacer :

```cpp
#ifdef WITH_GHOST_WAYLAND
    /* Wayland doesn't provide a windowing API via WSI. */
    if (wayland_window_info_) {
```

par :

```cpp
#if defined(WITH_GHOST_WAYLAND) || defined(WITH_GHOST_KORIN)
    /* Wayland doesn't provide a windowing API via WSI (Korin : surface headless, de meme). */
    if (wayland_window_info_) {
```

(le reste du bloc et son `#endif`, l. 1418, ne changent pas : `width`/`height` = taille de la fenêtre, 1280 x 720
si elle est nulle).

### 5. L'extension de surface de l'instance (`getPlatformSpecificSurfaceExtension`, l. 1616-1639)

Dans le `switch (platform_)`, après le cas Wayland (l. 1629-1633), avant `case GHOST_kVulkanPlatformHeadless:`
(l. 1634) :

```cpp
#  ifdef WITH_GHOST_KORIN
    case GHOST_kVulkanPlatformKorin:
      return VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME;
      break;
#  endif
    case GHOST_kVulkanPlatformHeadless:
      break;
```

`initializeDrawingContext` active alors `VK_KHR_SURFACE_EXTENSION_NAME` et `VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME`
(l. 1697-1699), puis `VK_KHR_swapchain` sur le périphérique (l. 1716) : rien d'autre à faire. (Le bloc
`VK_EXT_surface_maintenance1`, l. 1700-1713, s'applique s'il est annoncé : à vérifier avec la WSI de Korin, voir
« À vérifier ».)

### 6. Une surface de fenêtre ? (`initializeDrawingContext`, l. 1650-1665)

Dans le `switch (platform_)` qui calcule `use_window_surface`, après le cas Wayland (l. 1656-1660), avant
`case GHOST_kVulkanPlatformHeadless:` (l. 1661) :

```cpp
#  ifdef WITH_GHOST_KORIN
    case GHOST_kVulkanPlatformKorin:
      /* la poignee de la fenetre d'UIKit (> 0), passee par `window` */
      use_window_surface = (window_ != nullptr);
      break;
#  endif
    case GHOST_kVulkanPlatformHeadless:
      use_window_surface = false;
      break;
```

### 7. La création de la surface (`initializeDrawingContext`, l. 1746-1776)

Dans le `switch (platform_)` de « Initialize VkSurface », après le cas Wayland (l. 1760-1771), avant
`case GHOST_kVulkanPlatformHeadless: {` (l. 1772) :

```cpp
#  ifdef WITH_GHOST_KORIN
      case GHOST_kVulkanPlatformKorin: {
        /* Korin OS (VkKit) : surface headless, la fenetre d'Elegant dans le pNext ; sa chaine
         * d'echange montre ses images dans la fenetre (WSI de Korin, wsi_common_korin.c). La taille
         * est celle de la chaine (imageExtent), donc celle de wayland_window_info_. */
        VkKorinSurfaceInfo korin_info = {};
        korin_info.sType = VK_STRUCTURE_TYPE_KORIN_SURFACE_INFO;
        korin_info.pNext = nullptr;
        korin_info.window = long(reinterpret_cast<intptr_t>(window_));
        korin_info.x = 0;
        korin_info.y = 0;

        VkHeadlessSurfaceCreateInfoEXT surface_create_info = {};
        surface_create_info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
        surface_create_info.pNext = &korin_info;
        surface_create_info.flags = 0;

        /* (pris par vkGetInstanceProcAddr : le chargeur de VkKit n'exporte pas les fonctions des
         * extensions) */
        PFN_vkCreateHeadlessSurfaceEXT create_headless_surface =
            reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
                vkGetInstanceProcAddr(instance_vk.vk_instance, "vkCreateHeadlessSurfaceEXT"));
        if (create_headless_surface == nullptr) {
          return GHOST_kFailure;
        }
        VK_CHECK(create_headless_surface(
                     instance_vk.vk_instance, &surface_create_info, nullptr, &surface_),
                 GHOST_kFailure);
        break;
      }
#  endif
      case GHOST_kVulkanPlatformHeadless: {
        surface_ = VK_NULL_HANDLE;
        break;
      }
```

`vkDestroySurfaceKHR` (destructeur, l. 855 et 871) détruit cette surface comme les autres : rien à ajouter.

## Ce que fait GHOST_WindowKorin (déjà écrit)

```cpp
/* GHOST_WindowKorin::newDrawingContext (GHOST_WindowKorin.cc) */
GHOST_ContextVK *context = new GHOST_ContextVK(
    want_context_params_,
    GHOST_kVulkanPlatformKorin,
    reinterpret_cast<Window>(static_cast<intptr_t>(kui_win_)), /* X11 « window » : la poignee d'UIKit */
    nullptr,              /* X11 display */
    nullptr,              /* wl_surface */
    nullptr,              /* wl_display */
    &vulkan_window_info_, /* la taille de la fenetre (GHOST_ContextVK_WindowInfo) */
    1, 2,
    preferred_device_,
    &hdr_info_);
```

Toute autre demande (`GHOST_kDrawingContextTypeNone`, pas d'OpenGL : `WITH_OPENGL_BACKEND=OFF`) rend `nullptr` :
`GHOST_Window::setDrawingContextType` pose alors un `GHOST_ContextNone`.

## À vérifier à la première construction

* `Window` vaut bien `void *` (pas de `WITH_GHOST_X11`) : sinon le `reinterpret_cast` et `window_ != nullptr`
  ne compilent pas. Avec X11 un jour, passer la poignée autrement (un membre `korin_window_`).
* `-Wswitch` : chaque `switch (platform_)` reçoit son `case GHOST_kVulkanPlatformKorin` (§ 5, 6, 7) ; il n'y en
  a pas d'autre dans le fichier (vérifié : l. 1623, 1650, 1747).
* La WSI de Korin : `currentExtent` (0xFFFFFFFF attendu), `minImageCount`, modes de présentation (Blender
  préfère `MAILBOX`, prend `FIFO` sinon, `selectPresentMode` l. 1241), formats (`selectSurfaceFormat`, l. 1290 : B8G8R8A8_UNORM
  est dans sa liste, l'ordre d'Elegant), `VK_EXT_surface_maintenance1` et `VK_EXT_swapchain_colorspace`
  (facultatifs).
* La valeur `long` de `VkKorinSurfaceInfo.window` : c'est la poignée que rend `ui_window_ex` (`kui_window`),
  comme dans Godot ; `vulkan_korin.h` parle de « ui_window_id d'UIKit », fonction qui n'existe pas dans
  `uikit.abi` : la poignée et le numéro d'Elegant sont donc les mêmes (Godot Forward+ marche ainsi).
