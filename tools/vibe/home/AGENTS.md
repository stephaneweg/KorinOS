# Tu tournes sous Korin OS

Ce Vibe tourne **sous Korin OS**, un système d'exploitation écrit en assembleur AArch64 pour le Raspberry Pi 4
(et sous QEMU). Ce n'est **pas Linux** : pas de `apt`, de `sudo`, de systemd, de `/proc` (sauf `/proc/self/exe`),
de X11 ni de Wayland. Un seul utilisateur (uid 0). La couche POSIX (`libposix`) suffit pour bash, git, Python, GCC.

## Ce qu'il y a

* **Commandes** (`PATH=/bin:/sbin:/devtools/gcc/bin:/devtools/rust/bin` ; ajouter `/devtools/rust/bin` s'il y manque) : `bash`, `sh`, `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `find`, `grep`,
  `sed`, `head`, `tail`, `sort`, `wc`, `diff`, `zip`, `unzip`, `wget`, `git`, `rg`, `python3`, `pip`
  (`python3 -m pip`) ; **GCC 14** : `gcc`, `g++`, `make`, `objdump`, `nm`, `readelf`, `strip` et **`fbc`**
  (FreeBASIC) dans `/devtools/gcc/bin` ; C# par Roslyn dans `/devtools/dotnet` ; **`pibuild`** construit et installe une app ou une
  commande. Pas de `which` (`command -v`), `ps`, `top`, `curl`, `awk`, `xargs`.
* **Avec le paquet unixtools** (BusyBox), `/devtools/bin` est devant `/bin` dans ton `PATH` : `awk`, `xargs`, `which`,
  `env`, `sed -E`, `grep -E`, `sort -k`, `find -regex`, `head -c`, `tar`, `diff`, `sha256sum`, `timeout`… avec leurs
  options habituelles (`command -v busybox` pour savoir s'il est là ; sinon `pkg add unixtools`).
* `/tmp` est en mémoire et peut ne pas exister au démarrage : `mkdir -p /tmp` avant de s'en servir.
* **Chemins** : un seul arbre, comme Unix. `/` = le volume système (`/apps`, `/bin`, `/etc`, `/home`, et les outils
  dans `/devtools` : `gcc`, `python`, `dotnet`) ; les autres volumes sont montés dans des dossiers : `/tmp` = la RAM,
  `/media/usb0` = une clé USB, `/media/sd1` = la partition des données de l'utilisateur (sur le Pi). `HOME` =
  le dossier personnel de l'utilisateur (`/home/<nom>`, `/home` sans compte), aussi écrit `HOME:/` (le seul alias,
  accepté partout) ; `~` dans bash. Pas de préfixe `SD0:` ni de « volume courant ».
* **Lent** : sous QEMU comme sur le Pi, une compilation C prend de quelques secondes à une minute ; ne relance pas une
  commande parce qu'elle tarde.

## Obligatoire avant tout code pour Korin

**Avant d'écrire, de modifier ou de compiler un programme pour Korin, appelle la compétence `korin-app`** (ou
`korin-csharp`, `korin-freebasic`, `korin-rust` selon la langue ; et **`korin-graphics`** pour tout ce qui dessine ou
bouge : jeu, animation), même si tu crois savoir faire : Korin n'est pas Linux et son API
ne se devine pas. Pour toute fonction de kit dont la compétence ne donne pas d'exemple, **lis sa ligne dans le guide**
(`reference/uikit_en.md` de la compétence `korin`) ou dans `/devtools/gcc/include/kits/uikit_abi.h` avant de l'appeler.

Pièges déjà rencontrés :

* `ui_vp_line`, `ui_vp_disc`, `ui_vp_poly` comptent en **1/16 de pixel** (`x * 16`), et `ui_vp_line` prend
  `x0, y0, x1, y1, épaisseur, couleur` : sinon rien ne s'affiche.
* `KEY_LEFT`, `KEY_SPACE`... sont déjà dans `uikit_abi.h` : ne pas les redéfinir.
* Une animation avance au temps (`clock_gettime`), pas à chaque événement ; dessiner toute l'image puis un seul
  `ui_update_all`, puis attendre `WEV_FRAME` (`ui_frame_request`) : pas de scintillement (`korin-graphics`).
* Après un `pibuild`, vérifier la dernière ligne (`Installe : ...`) ; une erreur de lien `undefined reference to
  ui_...` : `ls /devtools/gcc/lib/libuikit.a` (s'il manque : `pkg update gcc`).

## Les compétences de Korin

* **`korin-app`** : écrire, compiler et installer une **application** (fenêtre, boutons, menus) ou une commande en C
  ou C++. À charger **avant** d'écrire du code pour Korin.
* **`korin-csharp`**, **`korin-freebasic`** : la même chose en C# (.NET 10, WinForms sur UIKit, Avalonia) et en
  FreeBASIC ; **`korin-rust`** : Rust (cargo, UIKit en FFI, sur PC).
* **`korin`** : le système : les **kits** (UIKit, AudioKit, NetKit...), la libc et ce que la couche POSIX n'a pas,
  les services, le guide du développeur complet (dans le dossier de la compétence).
* **`korin-graphics`** : dessiner, animer, faire un jeu (affichage, boucle, entrées, images, tactile).
* **`korin-widget`** : un widget du bureau (carte sur le fond d'écran comme l'Agenda et les Notes épinglées) :
  sa fenêtre, sa place, son aspect, sa boucle, son lancement à l'ouverture de la session.
* **`korin-game`** : un jeu d'arcade en C comme ceux du système (Tetris, Invaders, Arkanoid) : le cadre des
  jeux (en-têtes joints), plein écran, pause, manette, sons, record ; et comment concevoir le jeu.
* **`korin-diag`** : diagnostiquer : process, charge, journal du noyau, fautes, mémoire, carte, réseau, son, GPU.
* **`korin-forge`** (avec Forge, sur le PC) : modéliser une pièce 3D en pilotant Forge, la CAO, par ses outils
  MCP `forge_*` ; à charger pour toute demande de pièce, de modèle 3D ou de CAO.
* **`korin-terracube`** (avec TerraCube, sur le PC) : piloter le jeu de blocs par ses outils MCP `terracube_*` ou en
  Python : construire, les circuits de cuivre (portes logiques, horloges, pistons, machines), l'eau et la lave, le
  mode survie ; à charger dès qu'on parle de TerraCube ou de construire dans le jeu.
* **`korin-godot`** (sur le PC) : faire un jeu avec Godot 4 et l'exporter en app de Korin (greffon Korin Export,
  export sans fenêtre) ; à charger dès qu'on parle de Godot.
* **Les compétences des apps** : une app peut apporter la sienne, `korin-<app>` (son `app.txt` a la clé
  `skill = SKILL.md`, le fichier est dans `/apps/<app>.app/`, trouvé par `skill_paths`) : Forge, TerraCube,
  Arabesque, Koton... Pour se servir d'une app (créer un fichier qu'elle ouvre, la piloter), charge sa compétence
  avant de lire ses sources ; sans compétence ni serveur MCP, lis ses exemples (`/home/public/<app>`), puis ses
  sources.

## Règles de la maison

* La documentation, les commentaires et les textes affichés sont **en français** ; les noms de fichiers,
  d'exécutables, de fonctions et de paramètres **en anglais**.
* Toute interface graphique passe par **UIKit** (`ui_*`, `#include <uikit.h>`) : jamais de SDL, GTK, Qt ni de
  framebuffer à la main. Les couleurs viennent **du thème** (jetons `TJ_*` de `ui_tokens ()`), jamais en dur.
* Ne modifie pas le système (`/sys`, `/bin`, `/etc`, `/devtools/gcc`, `/devtools/python`, `/devtools/dotnet`) sans qu'on te le demande ;
  les projets vont dans **`~/projects/<nom>`** (`$HOME/projects`, jamais `/home/projects`). Les sources du système
  sont à lire dans `/src` (compétence `korin`).
* Ne lance **pas** une app graphique au premier plan dans le shell : elle ne rend pas la main et bloque l'outil.
  Voir la compétence `korin-app` pour la lancer.
