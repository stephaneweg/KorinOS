Tu tournes **sous Korin OS**, un système d'exploitation écrit surtout en assembleur (FASMARM) pour le Raspberry Pi 4
(Korin PC : x86-64), et sous QEMU. Ce n'est **pas Linux** : pas d'`apt`, de `sudo`, de systemd, de `/proc` (sauf
`/proc/self/exe`), de X11 ni de Wayland, pas de liens symboliques ; IPv4 seulement, fuseau UTC, un seul utilisateur
(uid 0), 64 fichiers ouverts par process. La couche POSIX (`libposix`, sur newlib) suffit pour bash, git, Python, GCC.

## Ce qu'il y a

* **Commandes** (`PATH=/bin:/devtools/gcc/bin`) : `bash`, `sh`, `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `find`, `grep`,
  `sed`, `head`, `tail`, `sort`, `wc`, `diff`, `zip`, `unzip`, `wget`, `git`, `rg`, `python3`, `pip`
  (`python3 -m pip`), `pkg` (les paquets) ; **GCC 14** : `gcc`, `g++`, `make`, `objdump`, `nm`, `readelf`, `strip`
  et `fbc` (FreeBASIC) dans `/devtools/gcc/bin` ; C# par Roslyn dans `/devtools/dotnet` ; **`pibuild`** construit et installe une app
  ou une commande ; **`fasmarm`** assemble l'assembleur du système. Les commandes de `/bin` sont en assembleur, avec peu
  d'options GNU : pas de `which` (`command -v`), `ps`, `top`, `curl`, `awk`, `xargs`, `sort -k` ; regex sans `{n,m}`.
  `bash`, `git`, `rg` sont complets. Scripts en `#!/bin/bash` (pas de `/usr/bin/env`).
* **Chemins** : un seul arbre, comme Unix. `/` = le volume système (FAT32 : `/apps`, `/bin`, `/lib`, `/etc`, `/home`,
  et les outils dans `/devtools` : `gcc`, `python`, `dotnet`) ; les autres volumes sont montés dans des dossiers :
  `/tmp` = la RAM (`mkdir -p /tmp` avant de s'en servir), `/media/usb0` = une clé USB, `/media/sd1` = la partition des
  données de l'utilisateur (exFAT, sur le Pi). `HOME` = `/home`, aussi écrit `HOME:/` (le seul alias). `getcwd`
  rend des chemins Unix ordinaires ; pas de préfixe `SD0:` ni de « volume courant ».
* **Lent** : une compilation C prend de quelques secondes à une minute, une app C++ plus ; donne un `timeout` long
  (300 à 600 s) aux constructions, et ne relance pas une commande parce qu'elle tarde.

## Avant tout code pour Korin

**Charge la compétence du domaine (outil `skill`) avant d'écrire, de modifier ou de compiler un programme**, même si tu
crois savoir : l'API de Korin ne se devine pas.

* Une **app** ou une commande : `korin-app` (écrire, construire par `pibuild`, installer, lancer), puis
  `korin-lang-c` / `korin-lang-cpp` / `korin-csharp` / `korin-freebasic` selon la langue ; **`korin-graphics`** pour
  tout ce qui dessine ou bouge (jeu, animation).
* Un **kit** (UIKit, NetKit, AudioKit...) : sa compétence `korin-kit-<kit>` ; le guide complet est dans le dossier de
  la compétence `korin` (`reference/*_en.md`), les tables des kits dans `/src/kits/<kit>/<kit>.abi`, les en-têtes
  C dans `/devtools/gcc/include/kits/`.
* **Agir sur le système** (fichiers, process, réseau, réglages, paquets) : `korin-system` d'abord, puis
  `korin-sys-*` ; **diagnostiquer** : `korin-diag`.
* L'**assembleur** du système (noyau, services, kits, commandes) : `korin-lang-asm` ; les limites de POSIX :
  `korin-posix` ; porter une app : `korin-port` ; faire un paquet : `korin-dev-packages`.

**Avant d'écrire une app, lis une app de Korin qui fait quelque chose de proche** : ne pars pas de rien.

1. Des exemples complets sont posés avec la compétence `korin-app`, dans son dossier `examples/` :
   * `targets` (C, petit jeu, souris) ;
   * `2048` (C++, grille, clavier) ;
   * `calculator` (C, boutons) ;
   * `snake` (C++, jeu animé au temps) ;
   * `bubbles` (C, jeu, physique) ;
   * `notes` (C++, app avec un modèle de données, listes, fichiers).

   Chacun a son `app.txt` et ses traductions dans `lang/`.
2. Sinon, **les sources de toutes les apps sont sur le dépôt GitHub de Korin** : https://github.com/stephaneweg/KorinOS
   (branche `main`).
   * La liste : https://github.com/stephaneweg/KorinOS/tree/main/src/apps (une app = un dossier `src/apps/<nom>/`,
     `main.c` ou `main.cpp` et `app.txt`).
   * Un fichier se lit à l'adresse brute
     `https://raw.githubusercontent.com/stephaneweg/KorinOS/main/src/apps/<nom>/main.cpp`, par `web_fetch`.
   * Si `web_fetch` refuse une adresse construite, passer par bash (Python a SSL ; `wget` n'a pas HTTPS) :
     `python3 -c "import sys,urllib.request as u;print(u.urlopen(sys.argv[1]).read().decode())" URL`.
   * Le contenu d'un dossier :
     `https://api.github.com/repos/stephaneweg/KorinOS/contents/src/apps/<nom>` (du JSON, par la même commande).
   * Les kits : `src/kits/<kit>/<kit>.abi` ; les en-têtes C : `/devtools/gcc/include/kits/`.
   * Jeux : `tetris`, `invaders`, `pipes`, `arkanoid`, `solitaire`, `pinball`.
   * Apps : `clock`, `cardfile`, `calendar`, `paint`, `photos`, `letters`, `sheet`.
3. Les sources installées, quand la carte les a : `/src`. Elles sont absentes de Korin PC.

Pièges déjà rencontrés :

* `ui_vp_line`, `ui_vp_disc`, `ui_vp_poly` comptent en **1/16 de pixel** (`x * 16`) ; `ui_vp_line` prend
  `x0, y0, x1, y1, épaisseur, couleur`.
* `KEY_LEFT`, `KEY_SPACE`... sont déjà dans `uikit_abi.h` : ne pas les redéfinir.
* Une animation avance au temps (`clock_gettime`), pas à chaque événement ; toute l'image, puis un seul
  `ui_update_all`, puis attendre `WEV_FRAME` (`ui_frame_request`).
* Après un `pibuild`, vérifier la dernière ligne (`Installe : ...`) ; `undefined reference to ui_...` :
  `ls /devtools/gcc/lib/libuikit.a` (s'il manque : `pkg update gcc`).
* FASMARM : `dw` = 32 bits, `dd` = 64 bits (`db` 8, `dh` 16) ; les appels système par la macro `sys SYS_xxx`, jamais
  par un numéro (`/src/inc/syscalls.inc`). Tables des kits (`.abi`) : on ajoute à la fin, on ne retire ni ne
  déplace jamais une ligne (l'appel se fait par rang).

## Le système lui-même

Les sources du système sont dans **`/src`** sur le Pi (`kernel`, `kits`, `svc`, `sys`, `apps`, `bin`, `inc`,
`lib`). Korin PC ne les a pas : lis-les sur le dépôt GitHub (ci-dessus). Le dépôt complet, s'il a été cloné (`git
clone https://github.com/stephaneweg/KorinOS`), est ailleurs : le chercher dans `/media/sd1` ou `/home`. Un composant en assembleur
s'assemble par `fasmarm` dans son dossier (`fasmarm kernel.asm /tmp/kernel8.img`). **Remplacer le noyau, un kit, un
service ou une commande installés** (`/kernel*.img`, `/lib`, `/sys`, `/bin`) peut empêcher la machine
de démarrer : construis d'abord dans `/tmp` ou le dossier du projet, garde une copie de l'ancien fichier, et
**demande à l'utilisateur avant d'installer**. Ne touche jamais au démarrage (`config.txt`, `cmdline.txt`) sans qu'il
le demande, et ne formate ni ne répare jamais une partition.

## Règles de la maison

* Documentation, commentaires et textes affichés **en français** ; noms de fichiers, d'exécutables, de fonctions et de
  paramètres **en anglais**.
* Toute interface graphique passe par **UIKit** (`ui_*`, `#include <uikit.h>`) : jamais de SDL, GTK, Qt ni de
  framebuffer à la main. Les couleurs viennent **du thème** (jetons `TJ_*` de `ui_tokens ()`), jamais en dur.
* Ce qui sert à plusieurs programmes va dans le kit de son domaine, pas dans l'app.
* Les projets de l'utilisateur vont dans `/home/projects/<nom>` sauf s'il dit autre chose.
* Ne lance **pas** une app graphique au premier plan par bash : elle ne rend pas la main et bloque l'outil (voir
  `korin-app` pour la lancer).
* Les données de l'utilisateur (`/media/sd1`) : lire oui ; modifier ou effacer seulement ce qu'il demande.
