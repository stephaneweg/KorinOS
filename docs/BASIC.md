# Le BASIC d'Onyx sur ARMPI

Autre outil que FreeBASIC (`/gcc/bin/fbc`) : une VM à bytecode, avec code natif AArch64, reprise d'Onyx.

## Asm ou C++, et ordre de travail

| Ordre | Morceau | Où | Langage | Pourquoi |
|---|---|---|---|---|
| 1 | Compilateur, VM, JIT, écran, 3D logicielle | `src/lib/basic` | C++ | repris tels quels (sauf `#include`, et `windowFlags` avant `window`) ; 7 000 lignes portables, un seul client |
| 2 | Runtime `/bin/basic` | `src/apps/basic` | C++ | l'hôte de la VM sur `ui_*`, `ak_*`, `gk_*`, POSIX |
| 3 | `#import` | `src/apps/basic/baskits.h`, `src/lib/basic/bi`, `tools/kits/abi2bas.py` | C++, Python | noms d'Onyx (`UIKit.window`...) dans le runtime, autres kits générés depuis leur `.abi` |
| 4 | QBasic | `src/apps/qbasic` | C++ | interface sur UIKit |
| 5 | GpioKit, service `gpio` | `src/kits/gpiokit`, `src/svc/gpio` | asm | système : selfhost, comme les autres kits ; préfixe `gp_` (`gk_` est à GpuKit) |
| 6 | GPIO Lab | `src/apps/gpiolab` | C++ | interface sur UIKit |
| 7 | QBStudio, Turtle Quest | `src/apps/qbstudio`, `src/apps/turtle` | C++ | moteurs repris (`world.h`, `form.h`, `gen.h`), interfaces réécrites |

Ajouts à UIKit : `UTA_BASIC`, `ui_textarea_insert`. Construire : `sh tools/office/build.sh basic qbasic gpiolab`, ou
`pibuild bin /src/apps/basic basic`, `pibuild app /src/apps/qbasic qbasic`.

## Vérifié sous QEMU

* Les `.bas` d'Onyx inchangés : `graphics`, `guess`, `gui`, `kits`, `uikit`, `planets3d`, `antigrav` (3D par le repli CPU),
  `gpio_bme280` (simulateur).
* Suite de tests d'Onyx : 11 sur 24 identiques ; les autres écarts vus sont de forme (libellé des erreurs, date, entrée
  absente, journal de l'hôte de test), sauf `t24_kits` (kit de test absent) ; `t7_errors` n'est pas lu en entier.
* FULLSCREEN, `NOTIFY`, `basic` depuis le Terminal, une app autonome (Make App), une erreur revenue à sa ligne dans QBasic.
* QBasic, QBStudio, GPIO Lab, Turtle Quest en Givre et Onyx Noir ; Turtle Quest : 48 niveaux sur 48 à 3 étoiles (`main --check`).
* `pibuild app /src/apps/qbasic qbasic` sous ARMPI : réussi, 447 344 octets comme la chaîne croisée. Les trois autres apps et
  `basic` n'ont été compilés que par la chaîne croisée, avec les mêmes options.

## Ouvert

* **3D par le GPU : préparée, jamais vue.** Sous QEMU c'est le repli CPU. `basic -g` a fait passer `planets3d` dans `gk_render` à
  blanc : toutes les images acceptées, listes construites. Il a fallu `bas3dclip.h` (GpuKit ne découpe pas, Onyx oui) et le
  correctif des tampons de 64 Mo de GpuKit (repris de la branche Viewport3D). L'image rendue par le V3D reste à voir sur le Pi.
* `#import` : FileKit, ImageKit, SystemKit, AppKit ne sont que ce que les exemples appellent.
* Manettes (`PAD`, `PADS`, `STICK`) : `WEV_PAD` ; les pads 0..2 sont ceux des pilotes, le 3 le clavier mappé par `gamepad.ini`, lu seulement par `PAD(3)`, comme dans Onyx où il faut le demander. Une manette n'est connue qu'à son premier événement : aucun appel ne dit ce qui est branché. Essayé sous QEMU par le clavier mappé seulement : QEMU n'émule aucune manette USB.
* GPIO : service jamais essayé sur le vrai Pi ; sous QEMU les broches seulement ; PWM, I2C, SPI réels non testables ; le
  simulateur l'est. Pas de présentation PocketUI pour GPIO Lab (640 x 480).
* Turtle Quest et QBStudio : leurs manques sont dans leur `README.md`.
