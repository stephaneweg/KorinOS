# Tu tournes sous Korin OS

Ce Vibe tourne **sous Korin OS**, un système d'exploitation écrit en assembleur AArch64 pour le Raspberry Pi 4
(et sous QEMU). Ce n'est **pas Linux** : pas de `apt`, de `sudo`, de systemd, de `/proc` (sauf `/proc/self/exe`),
de X11 ni de Wayland. Un seul utilisateur (uid 0). La couche POSIX (`libposix`) suffit pour bash, git, Python, GCC.

## Ce qu'il y a

* **Commandes** (`PATH=/bin:/gcc/bin`) : `bash`, `sh`, `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `find`, `grep`,
  `sed`, `head`, `tail`, `sort`, `wc`, `diff`, `zip`, `unzip`, `wget`, `git`, `rg`, `python3`, `pip`
  (`python3 -m pip`) ; **GCC 14** : `gcc`, `g++`, `make`, `objdump`, `nm`, `readelf`, `strip` et **`fbc`**
  (FreeBASIC) dans `/gcc/bin` ; C# par Roslyn dans `/dotnet` ; **`pibuild`** construit et installe une app ou une
  commande. Pas de `which` (`command -v`), `ps`, `top`, `curl`, `awk`, `xargs`.
* `/tmp` est en mémoire et peut ne pas exister au démarrage : `mkdir -p /tmp` avant de s'en servir.
* **Chemins** : un arbre POSIX par-dessus les volumes de Korin. `/sd0` = la carte système (`SD0:/`), `/sd1` = la
  partition des données (`SD1:/`), `/tmp` = la RAM. Raccourcis : `/apps`, `/bin`, `/gcc`, `/etc`, `/python` (tous
  sur `SD0:`). `HOME` = `/sd0`. La forme du système (`SD0:/apps/x.app`) est acceptée aussi, mais écris la forme
  POSIX (`/sd0/...`).
* **Lent** : sous QEMU comme sur le Pi, une compilation C prend de quelques secondes à une minute ; ne relance pas une
  commande parce qu'elle tarde.

## Les compétences de Korin

* **`korin-app`** : écrire, compiler et installer une **application** (fenêtre, boutons, menus) ou une commande en C
  ou C++. À charger **avant** d'écrire du code pour Korin.
* **`korin-csharp`**, **`korin-freebasic`** : la même chose en C# (.NET 10, WinForms sur UIKit, Avalonia) et en
  FreeBASIC.
* **`korin`** : le système : les **kits** (UIKit, AudioKit, NetKit...), la libc et ce que la couche POSIX n'a pas,
  les services, le guide du développeur complet (dans le dossier de la compétence).
* **`korin-diag`** : diagnostiquer : process, charge, journal du noyau, fautes, mémoire, carte, réseau, son, GPU.

## Règles de la maison

* La documentation, les commentaires et les textes affichés sont **en français** ; les noms de fichiers,
  d'exécutables, de fonctions et de paramètres **en anglais**.
* Toute interface graphique passe par **UIKit** (`ui_*`, `#include <uikit.h>`) : jamais de SDL, GTK, Qt ni de
  framebuffer à la main. Les couleurs viennent **du thème** (jetons `TJ_*` de `ui_jetons ()`), jamais en dur.
* Ne modifie pas le système (`/sd0/sys`, `/bin`, `/etc`, `/gcc`, `/python`, `/dotnet`) sans qu'on te le demande ;
  les projets vont dans `/sd0/home/projects/<nom>`. Les sources du système sont à lire dans `/sd0/src` (compétence
  `korin`).
* Ne lance **pas** une app graphique au premier plan dans le shell : elle ne rend pas la main et bloque l'outil.
  Voir la compétence `korin-app` pour la lancer.
