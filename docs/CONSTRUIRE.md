# Construire Korin OS

Korin OS s'appelait ARMPI : les noms internes (dossiers, `armpi.inc`, `board = armpi` des paquets, `~/armpi-*`) gardent
cet ancien nom.

Deux niveaux :

* **L'assembleur** : le noyau, les services, les kits et les apps en asm. Windows suffit, quelques minutes.
* **Le C/C++ et .NET** : WebKit, Mesa, GCC, .NET, SDL, la bureautique. Dans WSL, plusieurs heures et une
  soixantaine de Go. `build.ps1` ramasse ce qui a été construit (`build/*/sdroot`) et ignore ce qui manque.

## Prérequis

**Windows** : PowerShell, Python 3, Git. QEMU dans `C:\Program Files\qemu` pour les essais. FASMARM est dans le dépôt
(`tools/fasmarm`).

**WSL** (Ubuntu 26.04 essayé, 16 cœurs, 25 Go de mémoire) :

```bash
sudo apt install build-essential git curl unzip xz-utils cmake ninja-build python3 python3-venv pkg-config bison flex gperf ruby perl texinfo gawk m4 patch qemu-user rsync
```

Lancer les scripts depuis la racine du dépôt, par `wsl` depuis PowerShell : Git Bash déforme les chemins.

## L'assembleur

```powershell
.\build.ps1                         # build\kernel8.img, build\sd.img, build\sdroot
.\build.ps1 run                     # le même, puis QEMU
.\build.ps1 sd E:                   # une carte complète (noyau 800x600), sans toucher à la partition 2
.\build.ps1 min sdcard    # la carte minimale
```

`build.ps1 min` et le gestionnaire de paquets demandent `pkg`, `pkgd` et `pkgman`, construits dans WSL
(`tools/pkg/build.sh`, voir plus bas).

Variables facultatives de `build.ps1` : `ONYX_DIR` (une copie d'Onyx pour la SoundFont, sinon `build\onyx`),
`ARMPI_KOTON_SONGS` (un dossier de chansons `.sq` à poser dans `SD0:/home/koton/songs`).

## FASMARM

`tools/fasmarm/fasmarm.exe` assemble tout le système. Ses sources sont dans le dépôt, pour Windows et pour Korin OS :

| Quoi | Où |
|---|---|
| fasm 1.73.30, sources et `FASM.EXE` | `ref/fasm17330/w` |
| l'ajout ARM (FASMARM 1.44) | `tools/fasmarm/source` |
| le traducteur x86 → AArch64 | `tools/x86toa64.py` |
| le point d'entrée sous Korin OS | `src/bin/fasmarm/x86/armpi_main.asm` |
| le résultat traduit, généré | `src/bin/fasmarm/fasmarm.asm` |

```powershell
python tools\fasmarm\rebuild.py
```

Le script refait `fasmarm.exe`, puis la version native, dans `build\fasmarm`, et compare. Le `.exe` ne diffère que par la
date et la somme de l'en-tête PE, et assemble le même noyau, octet pour octet. La version native garde la même taille ;
l'ordre de ses tables dépend du hachage de Python (`src/bin/fasmarm/LISEZMOI.md`).

## Les sources tierces

`src/extra` n'est pas dans le dépôt. `tools/extra/fetch.sh` le refait depuis le dépôt public d'Onyx, à une révision
fixée, et récupère la chaîne d'outils :

```bash
sh tools/extra/fetch.sh toolchain      # aarch64-onyx-elf 14.2 dans /opt/toolchains
sh tools/extra/fetch.sh                # build/onyx, puis src/extra (bibliothèques, WebKit)
```

`/opt/toolchains` doit être accessible en écriture : `sudo mkdir -p /opt/toolchains && sudo chown $USER /opt/toolchains`.
`ONYX_BANCS=1` ajoute ce que lisent les bancs de comparaison (`tools/emu/check_*.sh`, `tools/audiotest`) ; les scripts
prennent `$ONYX`, ou `build/onyx` par défaut.

| Quoi | Version | Vérifiée par |
|---|---|---|
| Onyx (`third_party`, `tools/webkit`, `tools/ports`) | `7114ea1ce5c3` | révision git, `tools/extra/fetch.sh` |
| WebKit | `b8a7a626127c` + 25 patchs d'Onyx + `tools/extra/patches` + `tools/webkit/patches` | révision git |
| aarch64-onyx-elf | 14.2 | `SHA256SUMS` de `onyx-toolchain` |
| Mesa | 26.2.0, `9f0a761020bc`, `tools/mesa/armpi.patch` | étiquette git |
| outils de Mesa | meson 1.12.1, mako 1.4.3, pyyaml 6.0.3, packaging 26.3, ply 3.11 | `tools/mesa/fetch.sh` |
| ANGLE | de WebKit `b8a7a626127c`, `tools/mesa/angle/armpi.patch` | révision git |
| .NET | runtime v10.0.14, SDK 10.0.403, `tools/dotnet/armpi.patch` | SHA-512, `tools/dotnet/setup.sh` |
| GCC, binutils, GMP, MPFR, MPC, make | 14.2, 2.43 et suivants | SHA-256, `tools/gcc/build.sh` |
| FreeBASIC | 1.10.1, `tools/gcc/fbc/armpi.patch` | SHA-256, `tools/gcc/fbc.sh` |
| OpenSSL | `tools/openssl/build.sh`, `armpi.patch` | SHA-256 |
| SDL 3.4.18, sdl2-compat 2.32.74, ogg, vorbis, physfs, fmt, glm, libsamplerate, OpenAL | `tools/sdl/fetch.sh` | étiquettes git |
| SuperTux 0.7.0-rev.1, `tools/sdl/supertux.patch` ; SuperTuxKart 1.5 | `tools/sdl/fetch.sh`, `stage.sh` | étiquette git, SHA-256 |
| Avalonia 12.1.4, SkiaSharp 3.119.4, Skia `7dbfc07dd331`, HarfBuzz 8.3.1 | `tools/avalonia/env.sh` | révision git, étiquettes |

Une étiquette git n'est pas une somme : une étiquette déplacée en amont changerait la source sans alerte.

## Le C/C++, dans l'ordre

Chaque ligne suppose les précédentes.

```bash
sh tools/posix/build.sh               # libposix
sh tools/mediakit/build.sh            # prebuilt/mediakit.so
sh tools/openssl/build.sh
sh tools/webkit/build-deps.sh         # le sysroot et 11 dépendances (zlib, ICU, Skia...) : Mesa, .NET et SDL s'en servent
sh tools/gcc/build.sh                 # GCC et binutils sous Korin OS, /gcc sur la carte
sh tools/gcc/make.sh
sh tools/gcc/kits.sh                  # les kits depuis le C
sh tools/gcc/fbc.sh                   # FreeBASIC
sh tools/office/build.sh              # la bureautique et les apps C++
sh tools/pkg/build.sh                 # pkg, pkgd, pkgman
```

Mesa, puis ce qui en dépend :

```bash
sh tools/mesa/fetch.sh ; sh tools/mesa/configure.sh ; sh tools/mesa/build-libs.sh ; sh tools/mesa/build-armgl.sh
sh tools/emu/build.sh                 # les émulateurs en C++ (docs/EMULATEURS.md)
```

Jet et WebKit (≈ 2 h, [WEBKIT.md](WEBKIT.md)) :

```bash
sh tools/webkit/build-jsc.sh install
sh tools/webkit/build-media.sh ; sh tools/webkit/build-ui.sh
sh tools/webkit/build-webcore.sh ; TARGET=WebKit sh tools/webkit/build-webcore.sh
sh tools/webkit/build-wctest.sh ; sh tools/webkit/build-wk2test.sh
sh tools/mesa/build-angle.sh ; sh tools/mesa/build-webkit.sh
sh tools/webkit/build-jet.sh ; sh tools/webkit/stage.sh
```

.NET et Avalonia ([DOTNET.md](DOTNET.md), [AVALONIA.md](AVALONIA.md)) :

```bash
sudo sh tools/dotnet/setup.sh apt ; sh tools/dotnet/setup.sh
sh tools/dotnet/build.sh ; sh tools/dotnet/stage.sh ; sh tools/dotnet/kits.sh
sh tools/avalonia/build-skia.sh ; sh tools/avalonia/build-host.sh ; sh tools/avalonia/stage.sh
```

SDL et les deux jeux ([SDL.md](SDL.md)) :

```bash
sh tools/sdl/fetch.sh ; sh tools/sdl/build-sdl.sh ; sh tools/sdl/build-compat.sh ; sh tools/sdl/build-deps.sh
sh tools/sdl/build-supertux.sh ; sh tools/sdl/build-stk.sh ; sh tools/sdl/stage.sh
```

Pour finir, sous Windows : `.\build.ps1`, puis `.\build.ps1 sd E:` ou `.\build.ps1 min DOSSIER`.

## La carte minimale

`.\build.ps1 min sdcard` : firmware, noyaux 800 × 600, modules, services, `sys`, `bin`, kits, polices,
ressources, `etc`, les apps de base et le gestionnaire de paquets, avec une base des paquets qui ne décrit que ce qui est
sur la carte. Environ 30 Mo. Le reste s'installe par Logiciels. À copier sur une partition FAT32. Le dossier `sdcard/` du dépôt est cette carte, toute construite.

Le Wi-Fi : copier `etc/wifi.example.txt` en `etc/wifi.txt` et le remplir. Ce fichier ne va jamais dans un paquet ni
dans le dépôt.

## Les images de carte

```powershell
.uild.ps1 sd build\sdcard-full
python -I tools\mkimages.py --min sdcard --full build\sdcard-full --url-full https://adresse/de/la/release
```

`img` reçoit `korin_lite.img.xz`, `korin_full.img.xz` (4 Go, une partition FAT32) et `os-list.json`,
la liste pour Raspberry Pi Imager avec les tailles et les SHA-256. La carte complète a besoin de ce qui est construit
dans WSL. GitHub refuse un fichier de plus de 100 Mo : l'image complète va alors dans une Release, hors de git.
SuperTux et SuperTuxKart (près de 1,2 Go) restent hors de l'image complète (`HORS_FULL` de `tools/mkimages.py`) :
ils s'installent par Logiciels.

Au premier démarrage, les deux images ouvrent Setup ([PREMIER-DEMARRAGE.md](PREMIER-DEMARRAGE.md)).

## Publier des paquets

[PAQUETS.md](PAQUETS.md). Il faut la clé privée, qui n'est dans aucun dépôt.

## Ce qui reste propre à une machine

Les outils vers le Pi (`tools/mesa/pisync.py`, `tools/gcc/pitest.py`) lisent son adresse dans `$ARMPI_PI`.
`tools/emu/build_n3ds_bench.sh` attend la chaîne `arm-gnu-toolchain-14.2.rel1` sous `~/toolchains`.
