---
name: korin
description: Comment marche Korin OS, le système sur lequel tu tournes - volumes et chemins, la libc et la couche POSIX (ce qui marche, ce qui manque), les kits (UIKit, AudioKit, NetKit, GpuKit, MediaKit...) et leurs fonctions, les services, l'IPC, le bus, les paquets, les fichiers de configuration. Contient le guide du développeur complet. À utiliser pour toute question sur le système ou avant d'écrire du code qui appelle un kit.
user-invocable: true
---

# Korin OS

Korin OS est un système d'exploitation pour le Raspberry Pi 4 (aussi sous QEMU `raspi4b`), écrit en **assembleur
AArch64** (FASMARM) : noyau, services, kits et beaucoup d'apps. Les programmes peuvent aussi s'écrire en **C et C++**
(GCC 14.2 dans le système, `/gcc`), en C# (.NET, `/dotnet`), en FreeBASIC, en Python 3.14 (`python3`). Ce n'est pas
Linux : pas de paquets Linux, pas de X11, pas de `/proc`.

## Le guide du développeur (à lire plutôt que deviner)

Les pages sont dans **`reference/`**, à côté de ce fichier (les mêmes que l'app Aide, en anglais). Chaque exemple de
code y compile tel quel avec le GCC de la carte.

| Page | Contenu |
|---|---|
| `reference/index_en.md` | sommaire, liste des kits |
| `reference/app_en.md` | une app : dossier, `app.txt`, `pibuild`, les règles (voir aussi la compétence `korin-app`) |
| `reference/skeleton_en.md` | le squelette d'app complet en C++ (menus, champ, liste, vue dessinée, traductions) |
| `reference/libc_en.md` | newlib, libstdc++, libposix : fichiers, process, threads, mémoire, signaux, réseau, `dlopen` (**corrections plus bas**) |
| `reference/uikit_en.md` | **UIKit** : fenêtres, événements, dessin, texte, thème, widgets, vues, dialogues, menus, presse-papier, glisser-déposer, images, plusieurs fenêtres, traduction, pièges |
| `reference/audiokit_en.md` | AudioKit : lecture, capture, formats, mélangeur, synthèse FM, MIDI |
| `reference/mediakit_en.md` | MediaKit : vidéo WebM, MP4, VP8, VP9, AV1 |
| `reference/gpukit_en.md` | GpuKit : le GPU V3D du Pi, 3D |
| `reference/netkit_en.md` | NetKit : sockets, DNS, état du réseau, heure, Wi-Fi |
| `reference/gpiokit_en.md` | GpioKit : le connecteur 40 broches, GPIO, PWM, I2C, SPI |
| `reference/archivekit_en.md` | ArchiveKit : compression, ZIP, tar, gzip |
| `reference/documentkit_en.md` | DocumentKit : XML, encodages, OOXML, ODF, CSV |
| `reference/printerkit_en.md` | PrinterKit : dialogue d'impression, pages, PDF, imprimantes réseau |
| `reference/emukit_en.md` | EmuKit : base des émulateurs (fenêtre, temps, son, manettes) |
| `reference/service_en.md` | écrire un service (un programme de `SD0:/sys` qui répond sous un nom) |
| `reference/ipc_en.md` | l'IPC : services nommés, canaux, mémoire partagée, attente |
| `reference/kit_en.md` | écrire un kit (une bibliothèque partagée par tous les programmes) |

Chercher une fonction : `rg -n "ui_list_add" reference/` ; sa signature exacte :
`rg -n "ui_list_add" /gcc/include/kits/`.

## Les sources du système : `/sd0/src`

Les sources de Korin sont sur la carte (paquet `korin-srcs` ; s'il manque : `pkg add korin-srcs`), avec la même
arborescence que le dépôt, sauf `src/extra` (WebKit et les grosses bibliothèques tierces). Commentaires en français.
**À lire, pas à modifier** (ce n'est pas ce qui tourne : le système installé vient des paquets).

| Dossier | Contenu |
|---|---|
| `/sd0/src/kernel` | le noyau (assembleur) |
| `/sd0/src/kits/<kit>` | chaque kit : `<kit>.abi` (la liste des fonctions, avec leurs registres et un commentaire : la référence la plus précise), `<kit>.asm` et ses `.inc` (comment la fonction marche vraiment) |
| `/sd0/src/inc` | les définitions communes de l'assembleur (`armpi.inc`, constantes, ABI) |
| `/sd0/src/svc`, `/sd0/src/sys` | les services (`vfs`, `fatfs`, `netd`, `busd`, `sound`, `usbd`...) et les programmes système (`elegant`, `menubar`, `panel`, `home`, `setup`...) |
| `/sd0/src/bin` | les commandes de `/bin` (assembleur ; `pkg` en C++) |
| `/sd0/src/apps/<app>` | les apps : en assembleur (`<app>.asm`) ou en **C/C++** (`main.c`, `main.cpp`, avec `app.txt`, `lang/`, `help_*.md`) |
| `/sd0/src/lib` | `posix` (libposix : ce que fait vraiment une fonction POSIX), `acp` (libacp, le client de Vibe en C), `basic` |

Pour savoir ce que fait une fonction de kit : `rg -n "ui_list_add" /sd0/src/kits/uikit/uikit.abi`, puis sa
définition dans les `.inc`. Pour voir comment une vraie app fait quelque chose (menus, glisser-déposer, plusieurs
fenêtres, impression, son) : `rg -l "ui_dialog" /sd0/src/apps --glob "*.c*"`. Des apps en C/C++ de taille modeste :
`disks`, `photos`, `calendar`, `cardfile`, `pkgman`, `notes` (C++), `vibe` (C, `main.c`).

## Les kits vus du C

Un **kit** est une bibliothèque partagée du système (`SD0:/lib/<kit>.so`, en assembleur). Depuis le C : l'en-tête
`/gcc/include/kits/<kit>_abi.h` (ou `uikit.h`, `mediakit.h`, `emukit.h`, `printerkit.h`, qui l'englobent) et le
relais `/gcc/lib/lib<kit>.a` ; `pibuild` les donne tous, `ld` ne garde que ce qui est appelé. Toutes les fonctions
prennent et rendent des `long` (les chaînes et les pointeurs se passent en `(long) p`). On ouvre un kit une fois au
début : `uikit_open (&ui)` pour UIKit (il ouvre AppKit aussi), `<kit>_kit_open ()` pour les autres (voir la page du
kit). **AppKit** (fichiers, lancement, chaînes) n'a pas d'en-tête : la libc s'en sert à la place du programme.

| Kit | En-tête | Pour |
|---|---|---|
| UIKit | `uikit.h` | toute interface graphique (fonctions `ui_*`) |
| AudioKit | `audiokit_abi.h` | le son |
| MediaKit | `mediakit.h` | la vidéo |
| GpuKit | `gpukit_abi.h` | le GPU |
| NetKit | `netkit_abi.h` | état du réseau, Wi-Fi, heure (les sockets BSD passent par lui sans qu'on l'appelle) |
| GpioKit | `gpiokit_abi.h` | GPIO, I2C, SPI, PWM |
| ArchiveKit | `archivekit_abi.h` | zip, tar, gzip |
| DocumentKit | `documentkit_abi.h` | XML, OOXML, ODF, CSV |
| PrinterKit | `printerkit.h` | l'impression |
| EmuKit | `emukit.h` | les émulateurs |
| BusKit | `buskit_abi.h` | le bus du système (`busd`) : sujets publiés (`korin.theme`, `korin.shell.mode`...), s'abonner |

## Volumes, chemins, fichiers

* Un chemin du système nomme un **volume** : `SD0:/` (carte système, FAT32), `SD1:/` (partition des données de
  l'utilisateur, exFAT), `RAM0:/` (mémoire, effacée à l'arrêt), `USB0:/`.
* **Arbre POSIX** (table `/etc/paths.txt`) : `/sd0` = `SD0:/`, `/sd1` = `SD1:/`, `/ram0`, `/usb0`, et `/bin`, `/gcc`,
  `/apps`, `/lib`, `/dotnet`, `/etc`, `/python` (sur `SD0:`), `/tmp` (`RAM0:/tmp`). Un `/x` hors de la table vise
  `SD0:/x`. `getcwd` et `realpath` rendent la forme POSIX (`/sd0/home/x`). `HOME=/sd0`, `PATH=/bin:/gcc/bin`.
* Où sont les choses : apps `/sd0/apps/<nom>.app/` (`main` + `app.txt`), commandes `/bin`, programmes système et
  services `SD0:/sys` et `SD0:/sys/svc` (démarrés par `init2`), kits `SD0:/lib/*.so`, ressources `SD0:/res`
  (polices, icônes, claviers `res/keyb`, dictionnaires `res/lang`), réglages `/etc`.
* `SD1:` appartient à l'utilisateur : n'y écrire que ce qu'il demande.
* FAT : une seule date par fichier (à 2 s près), pas de liens (`ln` échoue), pas de propriétaires ; le bit
  d'exécution existe (`chmod +x`, gardé par vfs). 254 octets au plus par chemin, 64 fichiers ouverts par programme.

## La libc et la couche POSIX : corrections à la page `libc_en.md`

La page du guide date d'avant deux chantiers ; ce qui est vrai **aujourd'hui** :

* **`fork`, `vfork`, `execve` marchent** (même pid après `exec`, descripteurs hérités) ; `posix_spawn` reste plus
  rapide. `system` et `popen` lancent la commande **sans shell** (le premier mot est le programme : ni tubes, ni
  redirections, ni guillemets) : pour cela, lancer `sh -c "a | b"` soi-même.
* **Chemins** : voir plus haut (arbre POSIX, `HOME=/sd0`, `realpath` en forme POSIX), et non « la racine du volume
  courant ».
* **`chmod`** règle le bit d'exécution ; `stat` rend `0755` pour un exécutable, `0644` sinon.
* Toujours absent : IPv6, sockets `AF_UNIX` nommés, `getifaddrs`, `timer_create`, fuseaux horaires (tout est en
  UTC), `/proc` sauf `/proc/self/exe`, verrous de fichiers (sans effet), `SIGPIPE`.
* `connect` attend toujours, même non bloquant : dans une app graphique, un thread à part.
* Threads : pile de 256 Ko par défaut, 64 Mo pour le thread principal ; quatre cœurs.

## Le système en bref

* **Elegant** (`SD0:/sys`) est le serveur de fenêtres et le compositeur ; la **barre de menus** en haut porte les
  menus de l'app au premier plan ; le **panneau** (en bas) a le lanceur ; il n'y a **pas de barre de titre** (les
  boutons de fenêtre sont dans le coin haut droit du contenu, `FRAME_CAP_W`).
* **Thèmes** : Givre (clair, par défaut) et Onyx Noir (sombre), choisis dans Réglages, appliqués à chaud
  (`/etc/theme.txt`, sujet `korin.theme` du bus). Les apps prennent leurs couleurs dans les jetons d'UIKit.
* **Mode poche** (`shell = poche` dans `/etc/system.ini`) : écran tactile de 800 × 600, chaque fenêtre en plein
  écran, contrôles de 36 px (`ui_metric (UMT_CTL)`, `UMT_TOUCH`).
* **Langue** : `lang` dans `/etc/system.ini` (`fr` par défaut, `en`) ; les sources sont en français, traduits par
  les dictionnaires (`ui_tr`).
* **Paquets** : `pkg list [-a]`, `pkg info <nom>`, `pkg add <nom>`, `pkg delete <nom>`, `pkg update -a` (le dépôt de
  Korin) ; l'app **Paquets** (pkgman) fait pareil.
* **Diagnostic** (process, journal du noyau, fautes, réseau...) : compétence `korin-diag`. **Outils utiles** : `sysstat` (les process et leurs appels système : voir si une app tourne), `screenshot
  /tmp/screen.bmp` (capture de l'écran), `journal` (journal du noyau dans `SD0:/journal.txt`), `df`, `netstat`,
  `ping`, `wget`, `git`, `rg`, `python3`.
* **PocketQI** est l'assistante du bureau ; elle lit le `app.txt` des apps (`description`, `keywords`, `help`).

## Règles de la maison

* Documentation, commentaires, messages de commit **en français** ; noms de fichiers, d'exécutables, de fonctions,
  de paramètres **en anglais**.
* L'interface passe par **UIKit**, les couleurs par **les jetons du thème** ; coin haut droit libre ; mise en page qui
  suit la taille de la fenêtre (mode poche).
* POSIX sert à porter des programmes, pas à être conforme : ce qui manque se contourne avec ce qui existe.
* Pour la sûreté : ne pas toucher à `SD0:/sys`, `/etc`, `/bin`, `/lib`, `/gcc` sans demande explicite ; pas de
  `reboot` sans accord.
