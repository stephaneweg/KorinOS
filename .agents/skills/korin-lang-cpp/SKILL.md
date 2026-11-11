---
name: korin-lang-cpp
description: Écrire une app ou un programme en C++ (jusqu'à C++20) pour Korin OS — g++ de /gcc, pibuild, libstdc++ (std::thread, std::filesystem…), kits appelés depuis le C++, squelette d'app complet avec menus, liste et vue dessinée. À charger pour tout code C++ ; voir aussi korin-lang-c (dossier d'app, pibuild) et korin-kit-uikit.
---

# Le C++ sous Korin

Tout ce que dit `korin-lang-c` vaut pour le C++ : dossier d'app, `app.txt`, `pibuild`, installation, vérification.
La plupart des apps du système sont en C++ (`src/apps/*/main.cpp`). Celle d'un seul fichier la plus courte est
`src/apps/disks/main.cpp`.

## Outils

- **En une ligne :** `/gcc/bin/g++ -std=gnu++20 -O2 x.cpp -o /tmp/x`.
- **Avec pibuild :** `pibuild app <dossier> <nom>` compile les `.cpp` du dossier et lie avec `g++` dès qu'il y en a
  un.
- **libstdc++ de GCC 14.2 :** conteneurs, chaînes, exceptions, `std::thread`, `std::mutex`, `std::async`,
  `std::filesystem`, `std::chrono` et `thread_local`. Les threads se répartissent sur les 4 cœurs.
- **Le temps :** sur le Pi, un fichier C++ en `-O2` se compile en 8 à 9 s avec les outils préchargés, et en 20 s la
  première fois sous QEMU.

## Les kits depuis le C++

Les en-têtes des kits (`<uikit.h>`, `<netkit_abi.h>`, `<audiokit_abi.h>`…) ont leur `extern "C"` : on les inclut
tels quels. Les fonctions prennent des `long` :
- un pointeur se passe en `(long) p` ;
- un texte en `(long) s.c_str()`.

Un petit enrobage aide :

```cpp
#define TR(s) ((const char *) ui_tr ((long) (s)))
static const unsigned *s_jet;                      // jetons du thème (ui_jetons)
static unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }   // j = TJ_x / 4
```

Les rappels (`ui_view_on`, `ui_on_resize`, `ui_w_on`) sont des fonctions libres ou des lambdas **sans capture**,
converties en pointeur de fonction. L'état passe par l'argument `data`, ou par des variables du fichier.

## Squelette

`SD0:/apps/help.app/api/skeleton/main.cpp` (dans le dépôt : `src/apps/help/data/api/skeleton/`, expliqué dans
`skeleton_fr.md`) est le modèle à copier. Il contient :
- une fenêtre redimensionnable (`ui_screen`, `ui_window_ex(..., WF_RESIZABLE)`, `ui_min_size`, `ui_on_resize`) ;
- un champ, un bouton et une liste (`ui_textbox`, `ui_pushbutton`, `ui_list`), et une vue dessinée (`ui_view`,
  `ui_view_on`) ;
- un menu avec ses raccourcis (`ui_menu`, `ui_menu_key`) ;
- les traductions (`ui_tr_load`, `data/lang-en.txt`) et l'aide (`help_fr.md`).

La boucle, dans l'ordre :
1. `WEV_CLOSE` : sortir ;
2. `WEV_MENU` : l'id est `ui_ev_field(&ui, EV_CODE)` ;
3. sinon `uikit_w_event`, et `UN_ACTIVATE` pour un clic ;
4. puis `ui_menu_key(0)` pour une touche non prise, et `ui_w_idle()` quand rien ne s'est passé ;
5. `ui_view_flush()` à chaque tour.

Copier le dossier, changer `APP` et le nom du dossier, puis `pibuild app <dossier> <nom>`.

## Pièges

- **Les exceptions et le RTTI marchent**, mais grossissent l'exécutable. Les apps du système compilent avec
  `-ffunction-sections -fdata-sections` et `--gc-sections`, ce que pibuild fait aussi.
- **Pas de bibliothèque tierce dans `/gcc`** (Boost, Qt, SDL côté dépôt…) : ce qui n'est pas dans libstdc++ ou les
  kits est à écrire, ou à porter sur le PC.
- **`std::filesystem` voit les chemins `SD0:/…`.** `path::is_absolute()` est faux pour eux, comme pour tout code qui
  attend un `/` en tête : comparez avec `realpath`, ou gardez des chemins relatifs au dossier de l'app.
- **Un appel bloquant dans la boucle fige la fenêtre :** `connect`, une grosse lecture… Mettez-le dans un
  `std::thread`, et réveillez la boucle par un court `ui_event(délai)`.
- **Une app portée d'Onyx** appelle les `ui_*` de Korin directement, jamais l'uikit C++ d'Onyx (voir `korin-port`).
