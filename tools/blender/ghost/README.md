# GHOST pour Korin OS (premier jet, jalon 3)

Le fenêtrage de Blender 5.2.4 sur UIKit (docs/etude/BLENDER.md § 2.1-2.2, jalon 3), sur le modèle de la
plateforme `korin` de Godot (`tools/godot/platform/korin`). **Pas encore compilé.**

| Fichier | Rôle |
|---|---|
| `ghost_korin_ui.h`, `ghost_korin_ui.c` | la couche C sur UIKit (`kui_*`), reprise de `korin_ui.{h,c}` de Godot sans OpenGL, avec `kui_window_pos`, `kui_window_place`, `kui_window_top` en plus |
| `GHOST_SystemKorin.{hh,cc}` | le système : événements (`kui_poll`), clavier HID → `GHOST_TKey`, texte UTF-8, souris, molette, focus, fermeture, taille, minuteries, presse-papiers texte, écran, grab sans « warp » |
| `GHOST_WindowKorin.{hh,cc}` | les fenêtres : création (plusieurs), titre, taille, état, curseurs standard, grab, contexte Vulkan |
| `GHOST_ContextVK_korin.patch.md` | les ajouts à `GHOST_ContextVK.{hh,cc}` : surface headless + `VkKorinSurfaceInfo` |
| `CMAKE.md` | `CMakeLists.txt` racine et de GHOST, `GHOST_ISystem.cc`, chemins d'UIKit et de Vulkan, copie des sources |

## Ce qui est fait

* Fenêtres multiples d'UIKit (cadre du thème, redimensionnables), titre, taille (`KUI_EV_RESIZE` accepté puis
  `GHOST_kEventWindowSize`), position (`ui_window_pos`, relue au plus tous les 100 ms et au focus), état normal,
  agrandi, réduit ; fermeture (`GHOST_kEventWindowClose`) ; redessin (`invalidate` → `GHOST_kEventWindowUpdate`).
* Clavier : HID → `GHOST_TKey` ; les lettres suivent la disposition (AZERTY : Ctrl+Z sur la touche Z), le reste la
  position ; la relâche reprend la touche de l'appui ; répétition (`is_repeat`) ; texte saisi en UTF-8 (AltGr
  compris, rien avec Ctrl/Alt/Méta) ; modificateurs gauche/droite tenus par une seule source (touches 0xe0-0xe7
  et `EV_MODS`), relâchés à la perte du focus.
* Souris : position (écran = origine de la fenêtre + contenu), boutons gauche/milieu/droit, molette verticale.
* Grab : pas de `GHOST_kCapabilityCursorWarp`. `GHOST_kGrabWrap` et `GHOST_kGrabHide` prennent le pointeur
  (`kui_pointer_lock`) : Elegant le cache et le fige, les déplacements arrivent relatifs (`KUI_M_REL`) et font une
  position virtuelle (comme Wayland) ; `GHOST_kGrabWrap` utilise le curseur logiciel de Blender. Le pointeur est
  repris au retour du focus. `GHOST_kGrabNormal` : rien (pas de confinement dans Elegant).
* Minuteries (`GHOST_System`), `processEvents` avec attente (au plus 100 ms par `kui_poll`, jusqu'à la prochaine
  minuterie).
* Curseurs standard → formes `UC_*` ; ceux sans équivalent laissent Blender passer à ses bitmaps.
* Presse-papiers texte, taille de l'écran (`kui_screen`, `KUI_EV_SCREEN`), DPI 96.
* Vulkan seul : contexte de fenêtre (`GHOST_kVulkanPlatformKorin`) et contexte hors écran (headless).

## TODO

* **Curseurs personnalisés** (`setWindowCustomCursorShape` : flèche) : UIKit n'a que `UC_*`, il faut une
  fonction de plus en fin d'`uikit.abi`. Idem curseurs RGBA et générés.
* **Cacher le curseur** hors grab (`setWindowCursorVisibility` : sans effet).
* **Glisser-déposer** de fichiers (`KUI_EV_DROP`, `ui_drop_get` → `GHOST_EventDragnDrop`).
* **IME**, **tablette** (pression, inclinaison), **NDOF**, **pavé tactile** (gestes), molette horizontale,
  boutons 4 et 5.
* **Presse-papiers image** (UIKit a `ui_clip_set_image` / `ui_clip_get_image`) et couleur de l'écran
  (`ui_screen_grab`) pour la pipette.
* **Plein écran** : premier jet (fenêtre au-dessus, contenu sur l'écran, le cadre déborde) ; le vrai
  (fenêtre sans cadre `KUI_WIN_FULLSCREEN`, comme Godot) demande de refaire la surface de Vulkan.
* **État** agrandi/réduit par le cadre : Elegant ne le dit pas, `getState` rend l'état demandé par Blender.
* **Cadre** : `getWindowBounds` = le contenu seul ; les boutons du cadre d'Elegant sont dessinés en haut à
  droite du contenu (Godot laisse la place, `window_get_safe_title_margins`) : ils couvriront le haut à droite de
  la barre du haut de Blender.
* Échelle de l'interface (`getDPIHint` d'après `ui_scale` ou un réglage).

## À vérifier à la compilation et au premier lancement

* Signatures : relues sur `GHOST_ISystem.hh`, `GHOST_System.hh`, `GHOST_IWindow.hh`, `GHOST_Window.hh` de la
  5.2.4 ; toutes les fonctions virtuelles pures sont faites (système : `processEvents`, `getModifierKeys`,
  `getButtons`, `getCapabilities`, presse-papiers, `getMilliSeconds`, écrans, curseur, contextes,
  `createWindow`, `setConsoleWindowState` ; fenêtre : `newDrawingContext`, titre, bornes, tailles, conversions,
  état, ordre, `invalidate`, curseurs, visibilité).
* `GHOST_ContextVK` : la retouche n'est pas encore appliquée (`GHOST_kVulkanPlatformKorin` n'existe pas sans
  elle) ; `Window` doit valoir `void *` (pas de X11).
* **Liaison de Vulkan** : Blender appelle Vulkan en direct, le chargeur de VkKit ne donne que
  `vkGetInstanceProcAddr` et les commandes globales (voir `CMAKE.md` § 4.3) : le point le plus lourd.
* UIKit :
  * `ui_event` : attente négative non documentée (on attend au plus 100 ms et on boucle) ;
  * `EV_MODS` sur les événements de clavier : supposé l'octet du HID (bits 0-3 gauche, 4-7 droite, ce que
    disent `KUI_MOD_CTRL = 0x11`…) ; le caractère (`EV_CHAR`) avec Ctrl tenu : lettre ou code de contrôle, les deux
    cas sont pris ;
  * les touches de modificateur arrivent-elles comme `WEV_KEY` (codes 0xe0-0xe7) ? Les deux cas sont pris ;
  * la répétition d'une touche tenue : des appuis sans relâche (comme Godot le suppose) ;
  * `WEV_WHEEL` : crans dans `EV_Y`, positif vers le haut ; rien d'horizontal connu ;
  * `ui_maximize` : bascule-t-il (agrandi → normal) ? `setState(Normal)` l'appelle une seconde fois ;
  * `ui_resize` ne répond pas par `WEV_RESIZE` (l'événement de taille est posé par GHOST) ;
  * `ui_window_place` / `ui_window_top` sur une fenêtre à cadre, pour le plein écran ;
  * pointeur pris : il reste où il a été pris (pas de déplacement à la fin du grab), et il est rendu à la perte
    du clavier (`ws_ext.inc`) ;
  * la poignée de `ui_window_ex` est bien le numéro attendu par `VkKorinSurfaceInfo.window` (oui pour Godot).
* WSI de Korin : `currentExtent` à 0xFFFFFFFF (la taille vient de la fenêtre), modes de présentation, formats.
* Coordonnées : l'origine des fenêtres est relue au début de `processEvents` (au plus tous les 100 ms) ; une
  fenêtre déplacée entre deux relectures donne un écart passager.
