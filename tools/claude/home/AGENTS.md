Tu tournes **sous Korin OS**, un système d'exploitation écrit surtout en assembleur (FASMARM) pour le Raspberry Pi 4
(Korin PC : x86-64), et sous QEMU. Ce n'est **pas Linux** : pas d'`apt`, de `sudo`, de systemd, de `/proc` (sauf
`/proc/self/exe`), de X11 ni de Wayland, pas de liens symboliques ; IPv4 seulement, fuseau UTC, un seul utilisateur
(uid 0), 64 fichiers ouverts par process. La couche POSIX (`libposix`, sur newlib) suffit pour bash, git, Python, GCC.

## Ce qu'il y a

* **Commandes** (`PATH=/bin:/gcc/bin`) : `bash`, `sh`, `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `find`, `grep`,
  `sed`, `head`, `tail`, `sort`, `wc`, `diff`, `zip`, `unzip`, `wget`, `git`, `rg`, `python3`, `pip`
  (`python3 -m pip`), `pkg` (les paquets) ; **GCC 14** : `gcc`, `g++`, `make`, `objdump`, `nm`, `readelf`, `strip`
  et `fbc` (FreeBASIC) dans `/gcc/bin` ; C# par Roslyn dans `/dotnet` ; **`pibuild`** construit et installe une app
  ou une commande ; **`fasmarm`** assemble l'assembleur du système. Les commandes de `/bin` sont en assembleur, avec peu
  d'options GNU : pas de `which` (`command -v`), `ps`, `top`, `curl`, `awk`, `xargs`, `sort -k` ; regex sans `{n,m}`.
  `bash`, `git`, `rg` sont complets. Scripts en `#!/bin/bash` (pas de `/usr/bin/env`).
* **Chemins** : un arbre POSIX par-dessus les volumes. `/sd0` = la carte système (`SD0:/`, FAT32), `/sd1` = la
  partition des données de l'utilisateur (`SD1:/`, exFAT), `/tmp` = la RAM (`RAM0:/tmp` ; `mkdir -p /tmp` avant de
  s'en servir). Raccourcis : `/apps`, `/bin`, `/gcc`, `/lib`, `/etc`, `/python`, `/dotnet` (sur `SD0:`). `HOME` =
  `/sd0`. La forme `SD0:/apps/x.app` est acceptée aussi ; `getcwd` la rend parfois. Écris la forme POSIX (`/sd0/...`).
* **Lent** : une compilation C prend de quelques secondes à une minute, une app C++ plus ; donne un `timeout` long
  (300 à 600 s) aux constructions, et ne relance pas une commande parce qu'elle tarde.

## Avant tout code pour Korin

**Charge la compétence du domaine (outil `skill`) avant d'écrire, de modifier ou de compiler un programme**, même si tu
crois savoir : l'API de Korin ne se devine pas.

* Une **app** ou une commande : `korin-app` (écrire, construire par `pibuild`, installer, lancer), puis
  `korin-lang-c` / `korin-lang-cpp` / `korin-csharp` / `korin-freebasic` selon la langue ; **`korin-graphics`** pour
  tout ce qui dessine ou bouge (jeu, animation).
* Un **kit** (UIKit, NetKit, AudioKit...) : sa compétence `korin-kit-<kit>` ; le guide complet est dans le dossier de
  la compétence `korin` (`reference/*_en.md`), les tables des kits dans `/sd0/src/kits/<kit>/<kit>.abi`, les en-têtes
  C dans `/gcc/include/kits/`.
* **Agir sur le système** (fichiers, process, réseau, réglages, paquets) : `korin-system` d'abord, puis
  `korin-sys-*` ; **diagnostiquer** : `korin-diag`.
* L'**assembleur** du système (noyau, services, kits, commandes) : `korin-lang-asm` ; les limites de POSIX :
  `korin-posix` ; porter une app : `korin-port` ; faire un paquet : `korin-dev-packages`.

Pièges déjà rencontrés :

* `ui_vp_line`, `ui_vp_disc`, `ui_vp_poly` comptent en **1/16 de pixel** (`x * 16`) ; `ui_vp_line` prend
  `x0, y0, x1, y1, épaisseur, couleur`.
* `KEY_LEFT`, `KEY_SPACE`... sont déjà dans `uikit_abi.h` : ne pas les redéfinir.
* Une animation avance au temps (`clock_gettime`), pas à chaque événement ; toute l'image, puis un seul
  `ui_update_all`, puis attendre `WEV_FRAME` (`ui_frame_request`).
* Après un `pibuild`, vérifier la dernière ligne (`Installe : ...`) ; `undefined reference to ui_...` :
  `ls /gcc/lib/libuikit.a` (s'il manque : `pkg update gcc`).
* FASMARM : `dw` = 32 bits, `dd` = 64 bits (`db` 8, `dh` 16) ; les appels système par la macro `sys SYS_xxx`, jamais
  par un numéro (`/sd0/src/inc/syscalls.inc`). Tables des kits (`.abi`) : on ajoute à la fin, on ne retire ni ne
  déplace jamais une ligne (l'appel se fait par rang).

## Le système lui-même

Les sources du système sont dans **`/sd0/src`** (`kernel`, `kits`, `svc`, `sys`, `apps`, `bin`, `inc`, `lib`) ; le
dépôt complet de Korin, s'il a été cloné, est ailleurs (le chercher : `/sd1`, `/sd0/home`). Un composant en assembleur
s'assemble par `fasmarm` dans son dossier (`fasmarm kernel.asm /tmp/kernel8.img`). **Remplacer le noyau, un kit, un
service ou une commande installés** (`/sd0/kernel*.img`, `/sd0/lib`, `/sd0/sys`, `/sd0/bin`) peut empêcher la machine
de démarrer : construis d'abord dans `/tmp` ou le dossier du projet, garde une copie de l'ancien fichier, et
**demande à l'utilisateur avant d'installer**. Ne touche jamais au démarrage (`config.txt`, `cmdline.txt`) sans qu'il
le demande, et ne formate ni ne répare jamais une partition.

## Règles de la maison

* Documentation, commentaires et textes affichés **en français** ; noms de fichiers, d'exécutables, de fonctions et de
  paramètres **en anglais**.
* Toute interface graphique passe par **UIKit** (`ui_*`, `#include <uikit.h>`) : jamais de SDL, GTK, Qt ni de
  framebuffer à la main. Les couleurs viennent **du thème** (jetons `TJ_*` de `ui_tokens ()`), jamais en dur.
* Ce qui sert à plusieurs programmes va dans le kit de son domaine, pas dans l'app.
* Les projets de l'utilisateur vont dans `/sd0/home/projects/<nom>` sauf s'il dit autre chose.
* Ne lance **pas** une app graphique au premier plan par bash : elle ne rend pas la main et bloque l'outil (voir
  `korin-app` pour la lancer).
* Les données de l'utilisateur (`/sd1`) : lire oui ; modifier ou effacer seulement ce qu'il demande.
