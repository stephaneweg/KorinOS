# Reprendre Korin OS depuis le cloud

Pour un agent de code (Claude Code dans le cloud) sur une **machine Linux x86-64 neuve** (Ubuntu ou Debian), sans
WSL, sans Windows, sans le ZBook ni le Pi, sans la carte SD : construire Korin PC et le Pi, et les essayer sous QEMU.
Lire d'abord [AGENTS.md](../../AGENTS.md), puis `.agents/memory/MEMORY.md` et `.agents/memory/armpi-x86-pc.md`.
Chantiers passés au cloud avec leur fiche : la suite Craft (apps Rust et egui) : [CRAFT-HANDOVER.md](CRAFT-HANDOVER.md).

## 1. Mise en place

```sh
git clone --depth 1 --branch main https://github.com/stephaneweg/KorinOS korin && cd korin
sudo sh tools/cloud/setup.sh root      # paquets apt, /opt/toolchains : la seule étape root
sh tools/cloud/setup.sh all            # ou une liste d'étapes : base toolchains qemu verify
. ~/korin-cloud/env.sh                 # dans chaque shell, avant les scripts du dépôt
```

Un clone superficiel (`--depth 1`) suffit : ≈ 950 Mo ; l'historique complet fait 1,6 Go.

`tools/cloud/setup.sh` est idempotent (ce qui est fait et vérifié est gardé), télécharge depuis les sources
officielles à des versions fixées (SHA-256 vérifiés, ou révision git), et ne demande root qu'à l'étape `root`, qui le
dit. Sa sortie, `~/korin-cloud/env.sh`, donne aux scripts du dépôt ce qui changeait d'une machine à l'autre :

| Variable | Pour | Défaut sans elle (la machine de l'utilisateur) |
|---|---|---|
| `KORIN_TOOLCHAINS` | les chaînes `aarch64-onyx-elf-14.2`, `x86_64-onyx-elf-14.2` (tous les scripts de `tools/`) | `/opt/toolchains` |
| `KORIN_FASM` | fasm 1.73.32 pour Linux (`tools/pc/build.sh`, `fasm.sh`, `menu_esp.sh`, `mkkorindisk.py`) | `ref/fasm/FASM.EXE` par l'interop de WSL |
| `KORIN_FASMARM`, `FASMARM` | FASMARM 1.44 pour Linux (`build.ps1`, `tools/ci/carte_linux.py`, `tools/emu/build-n64.sh`, `build_gcemu.sh`) | `tools/fasmarm/fasmarm.exe` |
| `KORIN_QEMU_X86`, `KORIN_OVMF_CODE`, `KORIN_OVMF_VARS` | QEMU q35 + OVMF (`tools/pc/qemu_pc.py` et ce qui s'en sert : `qemu_nvme.py`, `qemu_sd.py`, les `pctest.py`) | `C:\Program Files\qemu` sous Windows ; sous Linux, le PATH et l'OVMF de la distribution |
| `ARMPI_QEMU` | `qemu-system-aarch64` (raspi4b) : `tools/ci/boot_test.py`, `qemu_drive.py` | `C:\Program Files\qemu\…` |
| `BUILD_PYTHON` | le Python 3.14 de construction de CPython (`tools/python/*.sh`) | `/usr/bin/python3.14` |

Sans ces variables, les scripts font exactement ce qu'ils faisaient (la machine de l'utilisateur ne change pas).
`TOOLCHAINS=$HOME/toolchains sh tools/cloud/setup.sh …` évite `/opt` (et l'étape root, si les paquets sont là).

### Les étapes et leur durée

Mesurées dans WSL (Ubuntu 26.04, 16 cœurs, 25 Go), dans un dossier neuf, avec un `HOME` et un `PATH` sans rien de
Windows ; à refaire sur la machine du cloud (moins de cœurs : plus long).

| Étape | Ce qu'elle fait | Durée |
|---|---|---|
| `root` | apt : compilateurs, cmake, ninja, bison, flex, gperf, ruby, mtools, QEMU (x86 et arm), OVMF, ce qu'il faut pour construire QEMU, libicu ; si la distribution les a : Python 3.14, LLVM 21 + libclc (Mesa iris), qemu-user | quelques minutes |
| `base` | fasm et FASMARM **assemblés ici depuis les sources du dépôt** (`ref/fasm17330`, `ref/fasm/SOURCE`, `tools/fasmarm/source`), binaires attendus vérifiés par SHA-256 ; `python` → `python3` | < 1 min |
| `toolchains` | `aarch64-onyx-elf-14.2` précompilée (dépôt `stephaneweg/onyx-toolchain`, SHA-256 de son `SHA256SUMS`) ; `x86_64-onyx-elf-14.2` **construite** (`tools/pc/toolchain.sh` : rien de publié) | 13 min (AArch64 : < 1 min ; x86-64 : 12 min sur 16 cœurs, 242 Mo) ; < 1 min avec `X86_TC_URL` |
| `qemu` | l'edk2 des sources de QEMU 10.2.1 (OVMF **avec le shell UEFI**, comme le QEMU de Windows : l'OVMF des distributions ne l'a pas, et les disques de `mkkorindisk.py` démarrent par `startup.nsh`) ; vérifie q35 et raspi4b ; raspi4b demande **QEMU 9.0** (Ubuntu 24.04 n'a que 8.2) : sinon QEMU 10.2.1 construit dans `~/korin-cloud/qemu` ; sans root et sans QEMU : les `.deb` extraits | < 1 min ; 2 min s'il faut construire QEMU (16 cœurs) |
| `sources` | archives de bash, git, rg (`~/.cache/korin-src`, par SHA-1 sur snapshot.debian.org, SHA-256 vérifiés) ; la SoundFont d'Onyx | < 1 min |
| `python` | uv 0.9.5 ; Python 3.14 par uv si la distribution n'en a pas | < 1 min |
| `verify` | `tools/pc/build.sh` (noyau, kits, commandes, services, Pi et PC), puis QEMU q35 : moniteur du noyau, `run ktest` (bilan sans échec exigé) ; puis le Pi : `build.ps1` par `pwsh` (sinon `tools/ci/carte_linux.py`) et `tools/ci/boot_test.py` sous raspi4b (`VERIFY_PI=0` : sans) | 2 min (construction 1 min, QEMU q35 44 s, Pi 15 s) |
| `rust` | `tools/rust/build.sh` (rustup à part, Rust 1.97.1 + rust-src, cible `x86_64-unknown-korin` ; `RUST_ARCHES="x86_64 aarch64"` : les deux) | 1,5 min |
| `webkit` | `tools/extra/fetch.sh` : `src/extra` (bibliothèques et WebKit d'Onyx, révision fixée) | 1 min (clones partiels ; ≈ 2 Go) |
| `dotnet` | `tools/dotnet/setup.sh` : SDK 10.0.403, runtime 10.0.14, crossgen2 (SHA-512) dans `~/armpi-dotnet` | < 1 min (≈ 600 Mo) |
| `pwsh` | PowerShell 7.4.6 pour Linux (`korin.ps1 -DryRun`, `build.ps1`) | < 1 min |

**Distribution** : les deux chaînes précompilées demandent **glibc 2.38** ou plus (Ubuntu 24.04, Debian 13, Ubuntu
26.04) ; plus ancienne : construire la x86-64 (`tools/pc/toolchain.sh`) et l'AArch64
(`share/onyx-toolchain/build-onyx-toolchain.sh` d'une chaîne existante, ou le dépôt Onyx). Le GCC 13 d'Ubuntu 24.04
construit le noyau et tout `tools/pc/build.sh` sans avertissement (vérifié).

**Vérifié le 14/11**, deux fois, sans rien de la machine de l'utilisateur (ni `/opt/toolchains`, ni `~/korin-*`, ni
Windows dans le `PATH`) :

* WSL Ubuntu 26.04, `HOME` et `TOOLCHAINS` neufs, sans root : `base toolchains qemu` (QEMU des `.deb` extraits)
  `sources python pwsh verify rust webkit dotnet` : tout passe ; `ktest` 47 / 47, le Pi démarre en 7 s ;
* **Ubuntu 24.04 neuve** (`ubuntu-base` dans un espace de noms, en root) : `root`, `base sources python`, puis
  `toolchains` avec la chaîne x86-64 en archive (`X86_TC_URL=file://…` : la relocalisation de `/home/…` vers
  `/opt/toolchains` et la recette passent), `qemu` (QEMU 8.2 de la distribution sans raspi4b : QEMU 10.2.1 construit,
  2 min), `pwsh verify` : `ktest` 47 / 47 avec GCC 13, le Pi démarre en 6 s.

Au-delà de `setup.sh`, dans l'environnement neuf de WSL : `ARCH=x86_64` `tools/posix/build.sh` (11 s),
`tools/openssl/build.sh` (30 s), `tools/webkit/build-deps.sh` (3,7 min), `CONFIG_ONLY=1 tools/dotnet/build.sh` (la
configuration de CoreCLR ; il fallait créer `artifacts/obj` dans un clone neuf de dotnet/runtime : corrigé),
`tools/pc/sysroot.sh` et le bureau depuis un disque NVMe d'essai (section 4). Pas essayé jusqu'au bout : Jet, .NET,
Python/Vibe, GCC dans l'OS (des heures ; leurs scripts sont ceux de la machine de l'utilisateur).

`all` = `base toolchains qemu sources python pwsh verify rust webkit dotnet` : la construction de Jet et de .NET
elle-même reste à lancer (section 3), elle prend des heures.

## 2. Ce que les scripts supposaient (inventaire du 14/11)

**Indispensable** (noyau, services, kits, commandes, apps C/C++, Pi en asm) :

* les deux chaînes dans `/opt/toolchains` : `aarch64-onyx-elf-14.2` (d'Onyx, précompilée : `tools/extra/fetch.sh
  toolchain`) et `x86_64-onyx-elf-14.2` (`tools/pc/toolchain.sh`, qui prend les en-têtes d'Onyx dans la chaîne
  AArch64 : celle-ci d'abord). `tools/pc/build.sh` construit kits, commandes et services **pour les deux machines** :
  il lui faut la chaîne AArch64 même pour le PC ;
* le GCC de l'hôte (x86-64) pour le noyau du PC, les kits, les commandes et les services du PC (`-Werror` ; GCC 15
  d'Ubuntu 26.04 et GCC 13 d'Ubuntu 24.04 vérifiés : un autre peut donner d'autres avertissements) ;
* fasm : `ref/fasm/FASM.EXE` par l'interop de WSL → `KORIN_FASM` ; FASMARM : `tools/fasmarm/fasmarm.exe` →
  `KORIN_FASMARM` ou `tools/ci/carte_linux.py` (docs/CI.md) ; les deux, assemblés sous Linux, donnent **la même
  sortie, octet pour octet** (vérifié le 14/11 : le noyau du Pi et ses 216 programmes asm ; `kernel.elf` du PC ;
  `BOOTX64.EFI` à la date et à la somme de l'en-tête PE près) ;
* QEMU et OVMF : `C:\Program Files\qemu` codé dans `qemu_pc.py` → variables ci-dessus ;
* `python` (les scripts écrits pour Windows l'appellent ainsi) ;
* pour la musique des apps : la SoundFont d'Onyx (`build/onyx/sdcard/res/soundfonts` ou `../Onyx`, sinon la banque FM) ;
* `build.ps1` (Pi) : PowerShell de Windows ; sous Linux, `tools/ci/carte_linux.py` (sans PowerShell : la carte
  `sdcard/` du dépôt + l'asm neuf) ou `pwsh build.ps1` avec `KORIN_FASMARM`.

**Pour bash, git, rg** : les archives `.orig` de Debian (`/opt/src/*` ou `~/.cache/korin-src/*`) : étape `sources`.

**Pour Jet (WebKit)** : `src/extra`, hors de git (`tools/extra/fetch.sh` : Onyx dans `build/onyx`, WebKit dans
`~/armpi-fetch/webkit` puis copié) ; ruby, perl, gperf, cmake, ninja ; arbres dans `~/korin-webkit-x86_64` (PC) et
`~/armpi-webkit-build` (Pi). Mesa iris (GlKit du PC) : LLVM 21, clang, libclc, SPIR-V (`tools/mesa/build-clc.sh`).

**Pour .NET et Avalonia** : `~/armpi-dotnet` (`tools/dotnet/setup.sh`), `~/armpi-avalonia`, l'ICU du sysroot de
Jet (`build-deps.sh` avant), dotnet/runtime cloné par `tools/dotnet/build.sh`.

**Pour Python et Vibe** : Python 3.14 de construction (`BUILD_PYTHON`), uv, la cible Rust ; `~/.cache/korin-python*`,
`~/.cache/korin-vibe*`. Les sources (CPython, libffi, roues) sont téléchargées par les scripts, versions fixées.

**Pour Rust** : rien à installer : `tools/rust/build.sh` installe son rustup à part dans `~/.cache/korin-rust*`.

**Ce qui reste propre à la machine de l'utilisateur** (inutile dans le cloud) : `tools/pc/korin.ps1`, `sd.ps1`,
`esp.ps1` (installation sur D: ou sur une carte), `tools/armpi_remote.py` et `ARMPI_PI` (le vrai Pi),
`tools/*/compare.sh`, `run_pi.sh`, `pi_run.sh` (QEMU de Windows par PowerShell), `tools/pkg/publish.sh` (clé privée),
`tools/emu/build_n3ds_bench.sh` (`~/toolchains/arm-gnu-toolchain-14.2.rel1`), les bancs `tools/emu/check_*.sh` et
`tools/audiotest` (`ONYX_BANCS=1 sh tools/extra/fetch.sh`).

## 3. Construire Korin PC, dans l'ordre

Après `. ~/korin-cloud/env.sh`, depuis la racine du dépôt (détail : [PC-PILOTES.md](../etude/PC-PILOTES.md), chaque
section « Construire » ; Pi : [CONSTRUIRE.md](../CONSTRUIRE.md)) :

```sh
sh tools/pc/build.sh                          # noyau, initrd, kits, commandes, services (Pi et PC)
ARCH=x86_64 sh tools/posix/build.sh           # libposix du PC
ARCH=x86_64 sh tools/openssl/build.sh ; ARCH=x86_64 sh tools/pdf/build.sh
sh tools/pc/apps.sh                           # apps C/C++ (build/pc/apps/status.txt)
ARCH=x86_64 sh tools/bash/build.sh ; ARCH=x86_64 sh tools/git/build.sh ; ARCH=x86_64 sh tools/rg/build.sh
ARCH=x86_64 sh tools/gcc/build.sh ; ARCH=x86_64 sh tools/gcc/kits.sh ; ARCH=x86_64 sh tools/gcc/make.sh
ARCH=x86_64 sh tools/gcc/fbc.sh ; sh tools/pc/fasm.sh
# Jet : PC-PILOTES.md « Jet pour le PC » (≈ 1 h sur 16 cœurs) ; Python/Vibe : « Python et Vibe pour le PC »
# (≈ 25 min) ; .NET : « .NET pour le PC »
```

Puis **l'arbre système** `build/pc/sysroot`, toujours dans cet ordre (`sysroot.sh` le refait à neuf) :

```sh
sh tools/pc/sysroot.sh
sh tools/pc/sysroot_apps.sh
sh tools/pc/sysroot_webkit.sh        # si Jet est construit
sh tools/pc/sysroot_git.sh
mkdir build/pc.lock && { sh tools/pc/sysroot_python.sh; rmdir build/pc.lock; }
mkdir build/pc.lock && { sh tools/pc/sysroot_dotnet.sh; rmdir build/pc.lock; }
sh tools/pc/sysroot_gcc.sh           # TOUJOURS en dernier
```

**Piège** : `tools/python/build.sh` **vide** `build/pc/python/sdroot` (site-packages compris) : relancer
`ARCH=x86_64 bash tools/python/site-packages.sh` après lui, avant `sysroot_python.sh`.

## 4. Essayer sous QEMU

**PC, noyau seul** (le moniteur du noyau : `build/pc/esp/korin/boot.cfg` de `build.sh` dit `cmdline = monitor`) :

```sh
python3 -I tools/pc/qemu_pc.py test --wait 15 --type "run ktest" --after 25    # ktest, ptest, itest, mtest, btest, wtest
```

Le journal du port 0xE9 est dans `build/pc/debug.log` (écrit à la fin de la sortie), la capture dans
`build/pc/screen.png`. Le clavier du noyau est AZERTY : `--type` traduit, `--keys` prend les noms US de sendkey.

**PC, système complet** (bureau Elegant, depuis un disque NVMe d'essai qui imite le ZBook) :

```sh
sh tools/pc/sysroot.sh               # (et les sysroot_*.sh voulus, section 3)
python3 -I tools/pc/mkkorindisk.py build/pc/test.img --layout zbook --korin-mb 200 --sysroot build/pc/sysroot \
    --cfg "$(printf 'default = korin\ntimeout = 0')"
python3 -I tools/pc/qemu_nvme.py build/pc/test.img --boot-disk --snapshot --vars build/pc/vars_test.fd \
    --wait 50 --step "shot:desk" --log build/pc/desk.log
```

Sans `--cfg`, le menu de démarrage part sur le faux Windows du disque d'essai. Vérifié le 14/11 dans l'environnement
neuf : le bureau Elegant (Givre, barre de menus, panneau) en 50 s, `build/pc/desk.png`.

Python, Vibe, .NET, Rust : `tools/python/pctest.py`, `tools/dotnet/pctest.py`, `tools/rust/testimage.py --pc`
(réseau `usb-net` en mode user ; **jamais de vrai fournisseur ni de vraie clé** : `tools/vibe/fakellm.py`,
`tools/nettest`). Ports QMP : `47000 + pid % 1000` ; ne pas prendre 4461 ni 4471 ; ne tuer que son QEMU.

**Pi** (QEMU raspi4b) :

```sh
python3 -I tools/ci/carte_linux.py           # l'asm assemblé, posé sur la carte sdcard/ du dépôt -> build/sd.img
python3 -I tools/ci/boot_test.py --timeout 300   # marqueurs de démarrage sur l'UART (docs/CI.md)
python3 -I tools/qemu_drive.py …             # clics et captures (clavier US)
```

`pwsh build.ps1` marche sous Linux (étape `pwsh`, `KORIN_FASMARM` de `env.sh` ; vérifié le 14/11 : carte complète en
≈ 30 s, démarrage raspi4b correct) et pose aussi ce qui est en C pour le Pi (kits, services, Elegant de
`build/*/aarch64`, que `tools/pc/build.sh` construit) ; `carte_linux.py` ne le fait pas. `build.ps1 sd`/`min` visent
une carte ou un dossier de Windows : pas dans le cloud. Le telnet du Pi sous QEMU : `remote.txt`
« password = essai » et `ARMPI_REMOTE_PW`, ≈ 80 s d'attente.

## 5. Ce qu'un agent du cloud ne peut pas faire

* **Pas de ZBook, pas de Pi, pas de D:, pas de carte SD** : aucun essai sur le vrai matériel (GPU Intel réel, Wi-Fi
  AX201, son HDA, touchpad, NVMe réel, V3D du Pi, Wi-Fi du Pi) ; QEMU n'a ni GPU ni les fautes de cache du JIT.
  Écrire dans le rapport ce qui reste « à essayer sur le ZBook / le Pi, par l'utilisateur », avec la marche à suivre.
* **Pas d'installation** : `tools/pc/korin.ps1 install` écrit sur D: ; dans le cloud, au plus
  `pwsh tools/pc/korin.ps1 … -DryRun` (étape `pwsh`). Jamais annoncer une installation.
* **Publication de paquets** : la clé de signature des paquets de Korin est dans la variable d'environnement
  `OnyxPkgKey` de l'environnement du cloud (la clé PEM elle-même, sur une ligne ; la même que `onyx_pkg-key.pem.txt`,
  vérifiée contre `skel/sd0/etc/pkg/onyx.pub`). `tools/pkg/publish.sh` la prend quand `ONYX_PKG_KEY_FILE` manque.
  **Jamais l'afficher** (ni `echo`, ni `env`, ni un `print` de diagnostic), jamais dans le dépôt ni dans un fichier
  hors d'un temporaire 600 ; ne publier (et surtout pousser) que sur demande de l'utilisateur.
* **Pas de push** sans demande de l'utilisateur.

## 6. Règles de travail

* [AGENTS.md](../../AGENTS.md) : noms en anglais, documentation et commits en français (« Zone : ce qui change »),
  LF, tables `.abi` qu'on ne fait que compléter, couleurs par `ui_tokens()`, `sys SYS_xxx`.
* La mémoire : `.agents/memory/MEMORY.md` puis le fichier du sujet ; elle disait vrai à sa date, vérifier dans le
  code ; y ajouter ce qui changera la prochaine session (décisions de l'utilisateur, pièges).
* **Ne jamais écrire, formater ni réparer `/media/sd1` sur le Pi** (partition personnelle de l'utilisateur), ni le NVMe
  Windows du ZBook. Aucune clé, aucun mot de passe dans le dépôt ni dans un journal ; `D:\etc\wifi.txt`,
  `D:\.vibe\.env` et `D:\acpi\*` n'existent pas dans le cloud et n'y viennent pas.
* Commits avec chemins explicites (jamais `git add -A`) ; la fin du message : `Co-Authored-By:` comme demandé.
* Un essai qui ne peut pas se faire est **dit**, pas présenté comme fait.

## 7. État et plan (14/11)

* Branche `main` (l'ancienne `claude/x86-64-pc`, qui a remplacé `main` le 18/11) : Korin PC démarre sur le ZBook (bureau Elegant, Wi-Fi, son, touchpad, GPU Intel par Mesa
  iris, Jet, git, rg, Python et Vibe validés par l'utilisateur) ; .NET (runtime, WinForms, Avalonia sur le processeur,
  Studio) vérifié sous QEMU seulement. Le détail : `.agents/memory/armpi-x86-pc.md`.
* **Prochain chantier** : le **serveur web maison et WebStudio** (PC-PILOTES.md, « Ordre du plan ») : serveur écrit
  pour Korin, **applet du panneau de configuration** des sites (dossier racine, plusieurs **bindings** : hôte, port,
  http/https), interface de gestionnaires prévue pour PHP et .NET (branchés plus tard), **certificats** (auto-signé,
  CSR, certificat du fournisseur et sa chaîne) et **Let's Encrypt** (ACME, HTTP-01, renouvellement, essais sur
  *staging* : jamais en production sans l'utilisateur) ; WebStudio en **C++ natif sur UIKit** (décision du 14/11), étude
  de design faite le 14/11 et à valider : `docs/DESIGN-WEBSTUDIO.md` (maquettes en lien, contrat avec le serveur au §5.4). Ensuite SDL 3,
  le GPU NVIDIA, Godot, le noyau du Pi en C (« Ordre du plan » et « Reste » de PC-PILOTES.md).
* Le 18/11, `claude/x86-64-pc` a **remplacé** `main` (sans fusion, à la demande de l'utilisateur) ; le dossier
  `packages/` de l'ancienne `main` (dépôt servi par GitHub Pages) a été repris tel quel. On travaille sur `main`.

## 8. Ce qui rendrait la mise en place plus rapide

* **Publier la chaîne `x86_64-onyx-elf-14.2` précompilée** (Release GitHub, ou un dossier du dépôt
  `onyx-toolchain` comme l'AArch64 : archive `.tar.xz` découpée sous 100 Mo + `SHA256SUMS` + sources GPL) ; ensuite
  `X86_TC_URL=… X86_TC_SHA256=… sh tools/cloud/setup.sh toolchains` (ou l'écrire en dur dans `setup.sh`) remplace
  les ≈ 12 min de construction.
* Publier aussi, au besoin : QEMU ≥ 9.0 construit (pour Ubuntu 24.04), `src/extra` en archive (plus rapide que le
  clone d'Onyx et de WebKit), les arbres de `~/korin-webkit-x86_64/sysroot` (dépendances de Jet) et la sortie de
  `tools/dotnet/build.sh` : ce sont les heures de construction.
