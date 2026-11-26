# Korin OS : consignes pour les agents de code

Korin OS (board `armpi`) est un système d'exploitation pour Raspberry Pi 4, écrit surtout en **assembleur FASMARM**,
avec une couche POSIX (newlib + `libposix`), GCC 14.2 dans l'OS, et des applications en assembleur, C, C++, C# et
BASIC. Ce n'est **pas Linux** : ne supposez rien de Linux sans l'avoir vérifié. La documentation est en français dans
`docs/` ; le guide de l'API C des applications est dans `src/apps/help/data/api/` (installé sur la carte dans
`/apps/help.app/api/`).

Les compétences de `.agents/skills/` détaillent chaque domaine ; chargez celle qui correspond à la tâche :

| Famille | Compétences |
|---|---|
| Agir sur le système | `korin-system` (les bases : volumes, shell, commandes, prudence), `korin-sys-files`, `korin-sys-processes`, `korin-sys-network`, `korin-sys-settings`, `korin-sys-packages` |
| Langages | `korin-lang-asm`, `korin-lang-c`, `korin-lang-cpp`, `korin-lang-freebasic`, `korin-lang-qbasic`, `korin-lang-csharp`, `korin-lang-csharp-avalonia`, `korin-lang-csharp-winforms` |
| Kits (API) | `korin-kit-uikit`, `-appkit`, `-netkit`, `-audiokit`, `-mediakit`, `-gpukit`, `-glkit`, `-gpiokit`, `-buskit`, `-archivekit`, `-documentkit`, `-printerkit`, `-emukit` |
| Transversal | `korin-posix` (limites de la couche POSIX), `korin-port` (porter une app), `korin-dev-packages` (faire et publier des paquets) |

Le langage dit **comment** appeler un kit ; la compétence du kit dit **quoi** appeler.

## Ce qui fait se tromper

- **Un seul arbre, à la Unix** (depuis le 15/11/2026) : `/` est le volume système (ext4 sur un PC installé,
  `/home` aussi ; FAT32 sur la carte du Pi et les disques d'essai ; FAT32 et exFAT : disques amovibles), les autres volumes sont
  montés dans un dossier : `/tmp` (en mémoire), `/media/usb0`… (clés), `/media/sd1` (la partition exFAT des
  données de l'utilisateur, sur le Pi), `/net/…` (partages), `/boot` (monté à la demande). Plus de `SD0:` ni
  d'autre `VOLUME:` : trois alias, **`HOME:/`**, le dossier personnel de l'utilisateur en cours (`/home` tant
  qu'il n'y a pas de comptes), **`PUBLIC:/`** (`/home/public`, partagé entre les comptes) et **`APPS:/`** (`/apps`). Les outils de développement sont dans `/devtools` (`gcc`, `dotnet`, `python`).
  Voir `docs/FICHIERS.md` et `docs/etude/MULTI-UTILISATEUR.md`.
- **Pas de liens symboliques**, pas de `/proc`, pas de `/usr/bin/env` (scripts en `#!/bin/bash`), IPv4 seulement,
  fuseau UTC, un seul utilisateur (pour l'instant), 64 fichiers ouverts par process.
- **Les commandes de `/bin` sont en assembleur**, avec peu d'options GNU : pas d'`awk`, de `xargs`, de `sort -k` ;
  regex sans `{n,m}`. `bash`, `git`, `rg` sont complets.
- **Couleurs** : jamais en dur. Une app prend les jetons du thème par `ui_tokens()` (Givre, Onyx Noir…).
- **FASMARM** : `dw` = 32 bits, `dd` = 64 bits (`db` 8, `dh` 16). Appels système par la macro `sys SYS_xxx`, jamais par
  un numéro (`src/inc/syscalls.inc`).
- **Tables des kits** (`src/kits/*/*.abi`) : on ajoute à la fin, on ne retire ni ne déplace jamais. Renommer
  une ligne ne change pas l'ABI (appel par rang) si tous les sources suivent : `tools/rename_ids.py`. Les noms
  français des fonctions et de ce qui est public passent en anglais au fil du port de chaque kit en C.

## Le dépôt

| Dossier | Contenu |
|---|---|
| `src/kernel`, `src/svc`, `src/sys` | noyau, services (vfs, netd, busd, hid…), programmes système |
| `src/kits` | les kits partagés (`.so`) et leurs tables `.abi` |
| `src/bin`, `src/apps` | commandes, applications (`<nom>/` donne `<nom>.app`) |
| `src/inc`, `src/lib` | macros et inclusions asm ; bibliothèques (`posix`…) |
| `skel/sd0`, `skel/sd1` | fichiers fixes des cartes (`etc/`…) ; la sortie construite va dans `build/sdroot` |
| `tools/` | scripts de construction (gcc, pkg, office, webkit, testing…) |
| `docs/` | études et référence (`CONSTRUIRE.md`, `KITS.md`, `ABI.md`, `POSIX.md`, `GCC.md`, `PAQUETS.md`…) |

## Construire et essayer (PC Windows)

- `.\build.ps1` (assemble tout), `.\build.ps1 run` (puis QEMU), `.\build.ps1 test` (démarrage sans écran),
  `.\build.ps1 sd E:` (carte complète). Le C et le C++ se construisent **dans WSL** (`wsl sh tools/…`), pas dans Git Bash
  qui abîme les chemins. Ordre et détails : `docs/CONSTRUIRE.md`.
- `build.ps1` lance `tools/check_constants.py` : une constante redéfinie avec une autre valeur arrête la construction.
- QEMU (`-M raspi4b`) n'a pas de GPU et ne montre pas les fautes de cache du JIT ; clics et captures scriptés :
  `tools/qemu_drive.py` (clavier US).
- Le Pi : `python -I tools/armpi_remote.py telnet HOST "commande"` (aussi `ftp`, `vnc`).
- **Ne jamais écrire, formater ni réparer `/media/sd1` sur le Pi** (l'ancien `SD1:`) : c'est la partition personnelle
  de l'utilisateur.

## Conventions

- Noms d'exécutables, de sources, de paramètres et de scripts **en anglais** ; documentation et messages de commit
  **en français** (« Zone : ce qui change »). Une app portée garde son nom d'origine.
- Un changement de fichiers d'un paquet demande d'augmenter sa version (`tools/pkg/versions.ini`, `mkrepo.py --bump`).
- Ce qui sert à plusieurs programmes va dans le kit de son domaine, pas dans l'app.
- POSIX sert à porter des apps et des bibliothèques, pas à être conforme : on ajoute ce qui manque quand un port le
  demande.

## Mémoire du projet

`.agents/memory/` : la mémoire des sessions de Claude Code (décisions de l'utilisateur, état des chantiers, pièges),
un fichier par sujet ; l'index est `.agents/memory/MEMORY.md`. Lisez l'index avant de commencer, puis les fichiers
du domaine de la tâche (`armpi-x86-pc.md` pour le PC). Ce qu'ils disent était vrai à leur date : vérifiez dans le
code. Reprise par des agents du cloud : `docs/agents/CLOUD.md`.
