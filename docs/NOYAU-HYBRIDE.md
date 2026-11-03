# ARMPI — vers un noyau hybride

Analyse demandée avant le chantier 5 (réseau) : **qu'est-ce qui doit vraiment rester dans le
noyau**, et comment sortir le reste en *services* en espace utilisateur, sans perdre ce qui fait
ARMPI (tout en assembleur, assemblable par FASMARM sous Windows et sous ARMPI, rapide sur un Pi 4).

« Hybride » au sens de Windows NT ou XNU : un noyau qui garde ce qui doit être rapide ou sûr
(mémoire, ordonnanceur, IPC, disque de démarrage), et des **serveurs** en espace utilisateur pour
le reste (fenêtres — déjà fait —, réseau, son, systèmes de fichiers distants), qu'un plantage
n'emporte pas avec le système.

## 1. Le noyau aujourd'hui

28 500 lignes (hors `tests/`), en gros :

| Bloc | Fichiers | Lignes | Appels |
|---|---|---:|---|
| Cœur : démarrage, MMU, pages, ordonnanceur SMP, threads, futex, exceptions, GIC, minuteur | `kernel`, `vm`, `pmm`, `sched`, `proc`, `vectors`, `gic`, `timer`, `spinlock`, `cache`, `arch/*/mmu, smp, board` | ≈ 3 300 | 0–11, 25, 27 |
| Fichiers : VFS, FAT32, exFAT, cache de blocs, SD (SDHCI), tubes, lancement d'ELF | `vfs`, `fat32`, `exfat`, `bcache`, `sdhci`, `pipe` | ≈ 5 060 | 12–24, 26 |
| Graphisme et entrée : console texte, cadre d'écran, clavier (HID, dispositions), tactile, diagnostic, mécanismes du serveur de fenêtres | `console`, `fb`, `font8x16`, `input`, `keymap`, `usb_hid`, `touch`, `diag`, `ws` | ≈ 3 940 | 53, 66–81 |
| Kits (bibliothèques partagées) | `kits` | 395 | 54 |
| USB : xHCI (Pi 4 : derrière PCIe), DWC2, stockage | `pcie`, `usb_xhci`, `usb_dwc2`, `usb_msd` | ≈ 3 780 | — |
| **Réseau** : IPv4, ARP, ICMP, UDP, TCP, DHCP, DNS, sockets ; Ethernet GENET ; Ethernet USB (ECM) | `net`, `genet`, `usb_ecm` | ≈ 4 770 | 40–52 |
| **Wi-Fi** : CYW43455 sur SDIO, firmware, WPA2 (supplicant de la puce) | `wifi`, `sdio` | ≈ 2 880 | 36–39 |
| **Son** : PWM, HDMI, I2S (micro), USB Audio | `audio`, `audio_hdmi`, `audio_i2s`, `usb_audio` | ≈ 2 230 | 28–35 |
| GPU V3D | `v3d` | 1 200 | — |

Ce qui est déjà sorti : le **serveur de fenêtres** (Elegant, chantier 4.2) — le noyau ne garde que
des mécanismes (tampons partagés 70–73, requêtes 74–77, événements 78–81).

**Constat décisif :** hormis le minuteur, **aucun pilote n'utilise d'interruption**. Chacun est
un thread noyau qui sonde son matériel (`thread_create` + `thread_sleep` : GENET, Wi-Fi, xHCI,
DWC2, son, pile réseau). Un pilote en espace utilisateur ferait exactement pareil : un process
qui sonde les registres qu'on lui a mappés. Il n'y a donc **pas besoin de remonter les
interruptions vers l'espace utilisateur** pour commencer (le chantier le plus délicat des
micro-noyaux).

Les dépendances des blocs à sortir vers le reste du noyau sont minces :

| Bloc | Ce qu'il prend au noyau | Son équivalent en espace utilisateur |
|---|---|---|
| `net` | pages (`pmm_alloc_n`), verrous, threads, `time_ms`, `netif_send` / `netq_pop` (les trames) | `sbrk` / mémoire DMA, futex, threads (5), `time_ms` (4), canal de trames |
| `wifi` + `sdio` | pages, `kload_file` (firmware), boîte aux lettres (MAC, horloge), GPIO, `udelay` | mémoire DMA, `open`/`read`, appel « firmware » réservé, registres GPIO mappés, attente active |
| `genet` | pages, `dcache_flush_range`, boîte aux lettres (MAC) | mémoire DMA **non cachée** (plus de vidage), appel « firmware » |
| `audio` | pages, DMA, boîte aux lettres, PWM / HDMI / I2S, **USB Audio** | idem ; l'USB Audio est un cas à part (voir 3.4) |

## 2. Ce que fait Onyx

Onyx (C++ sur Circle) est **monolithique pour les pilotes** : USB, SD, FatFs, réseau, son sont
ceux de Circle, dans le noyau, et ses applications passent par la table `kapi` (aujourd'hui en
EL0 avec appels système). Ce qu'il sort, c'est **l'interface** : le serveur Elegant
(`user/Servers`), les **démons** lancés par `init` (`SD:/etc/autostart`) et des **services IPC
nommés** (`control`, `screenshot` : un programme s'enregistre sous un nom, les autres lui
envoient des messages). Les dossiers FTP montés (`FTPx:`) sont servis par un programme.

ARMPI peut aller plus loin qu'Onyx parce que ses pilotes sont à lui, petits, et déjà écrits en
threads qui sondent : les sortir coûte peu.

## 3. Proposition

### 3.1 Ce qui reste dans le noyau

* **Le cœur** : mémoire virtuelle, pages, ordonnanceur SMP, threads, futex, exceptions,
  minuteur, GIC, process et chargeur ELF, kits.
* **Un IPC général** (nouveau, voir 3.3) — c'est lui qui rend le reste possible.
* **Le disque de démarrage et ses fichiers** *(dans un premier temps : ils sortent ensuite,
  §4)* : VFS, FAT32, exFAT, cache de blocs, SDHCI, tubes.
  Raison : tout le reste se charge depuis la carte (un serveur de fichiers en espace utilisateur
  devrait lui-même être chargé... depuis la carte), et c'est le chemin le plus sollicité
  (`open` / `read` de chaque programme, des polices, des kits). NT et XNU gardent aussi leurs
  systèmes de fichiers dans le noyau. Le VFS apprend en revanche à **déléguer un volume à un
  serveur** (3.3) : c'est ce qui servira `FTPx:`.
* **Le minimum pour se dépanner** : console texte, clavier USB (HID) et tactile, diagnostic
  F12, journal (`klog`). Si tous les serveurs tombent, on doit encore pouvoir taper au clavier.
  Pour la même raison, la **pile USB reste dans le noyau** pour l'instant (3.4).
* **Les mécanismes graphiques** actuels (tampons partagés, entrée brute, prise d'écran).

### 3.2 Ce qui sort, en serveurs (`SD0:/sys/svc/<nom>`)

Les binaires des services sont dans **`SD0:/sys/svc`** (décision du 28/10/2026) : `netd`,
`sond`, `vfs`, les services de FS (`fat32`, `exfat`, `ftpfs`, `ramfs`...), les pilotes. La
session du bureau (Elegant, barre de menus, dock, fond) reste dans `SD0:/sys`.


| Serveur | Contenu | Remplace | Quand |
|---|---|---|---|
| `elegant` | fenêtres, compositeur | — (fait, 4.2) | fait |
| **`netd`** | pile IPv4 / TCP / UDP / DHCP / DNS, sockets ; pilotes GENET et Wi-Fi (SDIO, firmware, WPA2) | appels 36–52 | **chantier 5** |
| **`sound`** | mixage, flux (anneaux partagés avec AudioKit), sorties et entrées de ses pilotes (`pwmaudio`, `hdmiaudio`, `i2smic`, `usbaudio`) | appels 28–35 | **fait** (chantier 6, [SON.md](SON.md)) |
| `ftpfs` | un volume `FTPx:` (client FTP) | — | après le réseau |
| `rdpd` | bureau à distance (client onyxremote) | — | après le réseau |

Le noyau perd ≈ 9 900 lignes (réseau, Wi-Fi, son) et ses 25 appels système les plus exposés —
ceux qui lisent des paquets venus du réseau. Un TCP qui plante tue `netd`, pas le système ; la
session le relance (comme le bureau relance le dock).

Les applications ne voient pas la différence : **NetKit** garde son interface (socket,
connect, send, recv...) mais parle à `netd` par IPC au lieu des appels 40–52 ; un futur
**AudioKit** fait de même avec `sond`. Les anciens numéros d'appel restent réservés et
répondent « non disponible » (la table d'appels est en ajout seul, comme l'ABI des kits).

### 3.3 Les mécanismes à ajouter au noyau

1. **Services nommés** — généralisation de ce qu'Elegant utilise déjà (74–77) :
   `svc_register(nom)`, `svc_open(nom) -> canal`, `svc_call(canal, message)`,
   `svc_next(message, attente)`, `svc_reply(numero, message)`, avec les tampons partagés
   existants (70–73) pour les gros transferts sans copie. Elegant devient un service comme un
   autre (son ABI ne change pas : `ws_*` restent, appuyés sur le même code).
2. **Accès au matériel, réservé aux serveurs** :
   * `dev_map(adresse physique, taille)` : mappe des registres (le noyau a déjà `vm_map_phys`) ;
   * `dma_alloc(taille) -> (adresse, adresse physique)` : mémoire contiguë **non cachée** (plus
     de vidage de cache à faire depuis l'espace utilisateur) ;
   * `fw_call(message)` : la boîte aux lettres du firmware (MAC, horloges, alimentation) ;
   * plus tard, `irq_wait(numero)` si un pilote a vraiment besoin d'interruptions.
   Seuls les programmes de `SD0:/sys` (donc `SD0:/sys/svc`) ont ce droit (décision du
   28/10/2026) ; les autres
   reçoivent « interdit ».
3. **Canaux de trames** : les cartes réseau que le noyau garde (Ethernet USB / ECM, avec la
   pile USB) sont offertes à `netd` comme des files de trames brutes (lire / écrire une trame).
4. **Volumes délégués** : `vol_serve(nom)` — le VFS transmet les `open` / `read` / `readdir`...
   d'un volume (`FTP1:`) au serveur qui l'a pris, par l'IPC du point 1 (comme FUSE sous Linux).
5. **Relance** : un serveur qui meurt est signalé (comme `WS_OP_EXIT` aujourd'hui) ; la
   session le relance, ses clients reçoivent « service redémarré » et rouvrent.

### 3.4 Cas à part

* **USB** (≈ 3 800 lignes + HID, ECM, Audio) : le clavier en dépend, le stockage `USB0:` aussi,
  et l'xHCI du Pi 4 doit être réveillé tôt (PCIe, firmware VL805). Il reste dans le noyau pour
  l'instant ; ses fonctions *réseau* (ECM) et *son* (USB Audio) sont exposées à `netd` / `sond`
  par des canaux bruts (trames, échantillons). À reconsidérer quand un pilote USB tombera sur
  une vraie panne.
* **V3D** : pas encore utilisé par le bureau ; sa place naturelle est à côté d'Elegant (un
  serveur graphique qui pilote le GPU), à décider quand le compositeur l'utilisera.
* **Tactile GT911 / clavier** : restent (dépannage).

### 3.5 Coût

* **Vitesse** : un appel `send` / `recv` passe de un appel système à un aller-retour IPC (deux
  appels, deux changements de process, quelques µs sur un A72) ; les données passent par un
  tampon partagé, sans copie de plus. Le réseau d'un Pi 4 (Ethernet 1 Gb/s, Wi-Fi bien moins)
  ne le sentira pas ; le disque, lui, reste direct.
* **Travail** : le code ne se réécrit pas, il se **déplace** — `net.asm`, `wifi.asm`, `genet.asm`
  deviennent des inclusions d'un programme `netd.asm`, et leurs ≈ 20 appels au noyau sont
  remplacés (tableau du §1). Le Wi-Fi n'a jamais tourné sur un vrai Pi : il sera **mis au point
  directement dans `netd`**, où un plantage ne fige pas tout et où l'on peut le relancer.
* **Auto-hébergement** : `netd` et `sond` sont des programmes ordinaires, assemblés par
  `build.ps1` et par `selfhost` (identiques des deux côtés).

## 4. Cible : le stockage aussi en espace utilisateur (décision du 28/10/2026)

Le §3.1 gardait les fichiers dans le noyau parce qu'il en a besoin pour charger les programmes.
L'utilisateur préfère les sortir aussi, **block devices compris** — le découpage qu'il avait
mis en œuvre sur son OS x86 en FreeBASIC, et celui de Minix 3 (proche de QNX) :

```
 application ──open──▶ VFS ──(volume → service)──▶ service FS (fat32, exfat, ext2, ftp...)
      │                  remet l'objet « fichier »            │
      └──read/write/map ─────────────────────────────────────▶ │ ──blocs──▶ block device
                                                                               (pilote SD, USB ;
                                                                                partitions)
```

* **Pilotes de disque** (SD, plus tard USB) : un **block device** par disque découvert, et un
  par **partition** (une fenêtre sur le disque : les numéros de blocs décalés). La lecture de la
  table MBR / GPT est un petit module commun, pas refaite par chaque pilote.
* **Monter** un volume = choisir un block device et un service de FS : cela **lance un process
  du FS pour ce volume** (une instance par volume, comme Minix 3 : un volume abîmé ne plante
  que lui, et l'état d'un process est celui d'un seul volume ; le code d'un FS est petit). Le
  service s'enregistre auprès du **VFS** sous son nom de volume (`SD0:`, `USB0:`...).
* **Monter, c'est toujours la même opération** : une *source*, un *service de FS*, un *nom de
  volume*, des *options*. La source est un block device (`sd0p1`, `usb0p1`) **ou un chemin
  réseau** (`ftp://hote:port/dossier`, avec identifiants si besoin). `ftpfs` n'est donc qu'un
  service de FS comme `fat32` : le VFS ne fait aucune différence entre `SD0:` et `FTP1:`.
  La source peut aussi être **de la mémoire**, avec sa taille (`ram:16M`) : un service `ramfs`
  dont les fichiers sont directement des tampons partagés (le mappage sans copie y est
  naturel ; fichiers temporaires, `RAM0:`), ou un block device en mémoire si l'on veut y mettre
  un autre FS (une image FAT à préparer, par exemple).
  Les montages permanents sont décrits dans `SD0:/etc` (par exemple `mounts.txt`) ; leurs
  mots de passe ne sont **pas** dans ce fichier en clair : demandés au montage, ou gardés à
  part (fichier lisible des seuls services système), à décider au chantier FTP.
* **Le VFS ne sert qu'à l'ouverture** : il trouve le service du volume, qui crée un **objet
  « fichier »** — son service, son numéro chez ce service, son mode : **bloc** (accès au
  hasard, `seek`) ou **flux** (tube, socket, FTP). Le noyau garde cet objet derrière le
  descripteur de l'application (héritage au `spawn`, fermeture à la mort du process, stdio) ;
  `read` / `write` vont **directement** au service FS, sans repasser par le VFS.
* **Pas de copie** : un fichier que le FS a chargé en entier (programme, kit, police, petit
  document) est un tampon partagé (appels 70–73) qu'il **mappe chez le client** — en lecture
  seule, partagé entre tous ses lecteurs ; une écriture par `write` modifie le même tampon,
  que tous voient. Les gros fichiers (vidéo, image disque) restent en accès par blocs, à
  travers des tampons partagés aussi (le pilote écrit directement dans le tampon du FS).
* **Lancer un programme** (`spawn`, `lib_open`) : un programme de l'espace utilisateur mappe
  l'exécutable par le VFS, puis demande au noyau de créer le process **à partir de cette
  image** (`proc_create(image)`). Le noyau ne lit plus aucun fichier.
* **Démarrage** : le pilote SD, FAT32 et le VFS sont des **modules de démarrage**, déjà en
  mémoire quand le noyau démarre (inclus dans `kernel8.img`, ou chargés par le firmware avec
  `initramfs` de `config.txt`) ; le noyau les lance, puis tout se charge par le VFS. Leurs
  images restant en mémoire, un service de fichiers qui plante peut être **relancé**.

Ce découpage remplace les « volumes délégués » du §3.3-4 (c'est la même chose, généralisée).
Il est mis au point d'abord sur `ftpfs`, qui ne peut pas empêcher le système de démarrer,
avant d'y passer la carte SD.

### init2 et hwmanager (décision du 28/10/2026, fait)

Le noyau ne décide plus de ce qui démarre (`src/svc/init2/init2.asm`, `src/svc/hwmanager/hwmanager.asm`) :
* `init` (étape 1, initramfs) monte `SD0:` puis lance **`init2`** depuis le disque ;
* `init2` lance **`hwmanager`** et **Elegant** (le shell choisi par `system.ini`) et les
  services de `services.txt` ; il relance un service qui s'arrête sur une erreur (code de
  sortie non nul : l'avis de fin le porte), 3 fois au plus ;
* **étape 3** : quand **Elegant est prêt** (le service « ws » répond), `init2` lance les
  applications en démarrage automatique (`startup.txt`) — plus de programme qui attend
  l'interface en boucle ;
* **`hwmanager`** est un service de **découverte du matériel** : il écoute ce qui est présent
  et ce qui se branche (USB, tactile, cartes réseau...) et charge les pilotes voulus —
  `usbkeyboard`, `usbmouse`, `touch`... — devenus des services, au lieu des pilotes que le
  noyau démarre aujourd'hui d'après `options.txt` (pour l'instant, un squelette qui
  s'enregistre et attend).

## 5. Ordre proposé

**Avancement (28/10/2026)** :
* modules de démarrage par `initramfs` : lus par le noyau (QEMU ; essai sur le Pi à faire) ;
* **étape 1 faite** — services nommés (`src/kernel/svc.asm`, appels 82–86) : un thread qui
  attend est réveillé aussitôt par la requête ou la réponse (plus de sommeils de 1 ms) ;
  droits (`P_DROITS`) pour les programmes de `SD0:/sys` ; Elegant passe par le service « ws »
  sans changer son ABI (l'ancienne file du noyau est retirée) ; service d'essai
  `src/svc/echo` → `SD0:/sys/svc/echo`, mesuré par `SD0:/bin/svctest` : **quelques µs par aller-retour sur
  le PocketTerm (Pi 4)** — 10 000 en une cinquantaine de ms — contre ≈ 250 µs sous QEMU. Les sources des services sont dans **`src/svc`** ; le noyau
  est élagué à mesure que ses parties sortent.
* **étape 2 faite** — accès au matériel (`src/kernel/hardware.asm`, appels 87–89), pour les
  seuls programmes de `SD0:/sys` : `dev_map` (zones de périphériques déclarées par la carte,
  `DEV_ZONE*` de `board.inc` : jamais la RAM ni le GIC ; fenêtre `0x78_0000_0000`),
  `dma_alloc` (pages contiguës, à zéro, non cachées, libérées avec le process ; fenêtre
  `0x7A_0000_0000`), `fw_call` (boîte aux lettres du firmware). Essai : `SD0:/sys/svc/hwtest`
  (minuteur système, refus de la RAM, DMA relue, révision de la carte), lancé par `svctest` :
  **validé sur le PocketTerm** (28/10/2026).
* **Stockage, étape A faite** (28/10/2026) — volumes servis en espace utilisateur :
  * le noyau délègue tout volume qu'il ne tient pas (`src/kernel/remote.asm`) : `open`,
    `stat`, `mkdir`, `unlink`, `chdir` partent au service **`vfs`** (`src/svc/vfs/vfs.asm`), qui
    transmet au service du volume avec le chemin relatif ; l'ouverture rend l'**objet fichier**
    (service, numéro, attributs, taille) que le noyau garde derrière le descripteur ; `read`,
    `write`, `readdir` vont ensuite **directement** au service ; `close` est un avis sans
    réponse ; protocole commun : `src/inc/fs.inc` ;
  * **`ramfs`** (`src/svc/ramfs/ramfs.asm`) : arborescence en mémoire ; `vfs` le lance pour chaque
    ligne de **`SD0:/etc/mounts.txt`** (`RAM0 = ram:16M, ramfs`) ; il s'enregistre sous
    `fs.ram0` ; un service qui s'arrête fait disparaître son volume ;
  * le noyau lance **`SD0:/etc/services.txt`** (ici `vfs`) avant `startup.txt` ;
  * `SD0:/bin/ramtest` : tout passe, sous QEMU et **sur le PocketTerm** (28/10/2026). Sur le
    Pi 4 : 256 Ko écrits / relus en **4 / 3 ms sur `RAM0:`** (565 messages de 464 octets :
    ≈ 6 µs chacun, l'aller-retour mesuré par `svctest` — le coût des services est celui des
    messages, rien d'autre), contre **1 429 / 139 ms sur `SD0:`** (FAT32 du noyau sur la carte :
    l'écriture par blocs de 4 Ko est lente, à reprendre dans le service FatFS).
* **Stockage, étape B faite** (28/10/2026) — périphériques blocs et FAT32 en services :
  * **périphériques blocs** (`src/inc/blk.inc`, serveur commun `src/svc/inc/blkserv.inc`) : un service
    `blk.<nom>` par disque et par partition (table MBR lue par le serveur commun : une
    partition n'est qu'une fenêtre sur le disque), un thread par périphérique ; les données
    passent par un **tampon partagé** du client (attaché une fois), sans copie ;
  * pilotes : **`sdblk`** (la carte, par le **canal de blocs** du noyau — appel 93 — tant que le
    pilote SD y reste ; **lecture seule** : la partition personnelle n'est jamais écrite) et
    **`imgblk`** (un fichier image, lecture et écriture) ;
  * **`fat32`** (`src/svc/fatfs/fat32.asm`) : le FAT32 du noyau **inclus tel quel**
    (`src/kernel/fat32.asm`, une seule source tant que le noyau le garde) et la gestion des
    fichiers ouverts reprise de `vfs.asm` ; son cache de secteurs est **dans** le tampon
    partagé (le service bloc y lit directement) ; 8 Ko de code ;
  * montages : `SD9 = blk.sd0p1 ro, fat32` (comparaison avec `SD0:`), `IMG0 = blk.img0p1, fat32`
    (`SD0:/test.img`, 8 Mo, fabriquée par `tools/mkdisk.py --taille 8`) ; services lancés par
    `services.txt` (`sdblk sd0 0`, `imgblk img0 SD0:/test.img`) ;
  * QEMU : `ramtest IMG0` passe entièrement (écriture, relecture, seek, readdir, chdir,
    unlink) ; `ramtest SD9` : `uikit.so` lu par `fat32` et par le noyau, **0 octet différent**,
    écriture refusée.
  * **Validé sur le PocketTerm** (28/10/2026) : `ramtest IMG0` et `ramtest SD9` passent.
    256 Ko sur `IMG0:` (fat32 → imgblk → fichier image sur le FAT32 du noyau) : écrits en
    2 063 ms, **relus en 143 ms — autant que `SD0:` par le noyau (142 ms)** malgré trois
    couches ; l'écriture suit la lenteur de l'écriture du noyau sur la carte (1 423 ms).
* **Stockage, étape C (bascule) faite sous QEMU** (28/10/2026) — **mode services** : `SD0:` est
  servi entièrement en espace utilisateur ; le noyau ne touche plus la carte.
  * **Deux modes**, choisis par `initramfs` : avec le module `init`, mode services ; sans lui,
    le noyau garde la carte comme avant (mode noyau : secours, et carte du Pi 5).
  * **`init`** (`src/svc/init/init.asm`), seul module lancé par le noyau : il lit **`init.txt`**
    (module texte : `sd sd0`, `vfs SD0 blk.sd0p1 fat32`), lance ces services depuis initramfs
    (appel 94 `mod_lancer` ; 95 `mod_lire`), attend que `SD0:` réponde, puis **lève le verrou**
    (96 `demarrage_pret`). Un thread du noyau attendait ce verrou ; il fait alors la suite du
    démarrage (options, clavier, pilotes, Elegant) et l'**étape 2 depuis le disque**
    (`services.txt`, `startup.txt`). Seuls les services nécessaires au démarrage sont dans
    initramfs : `init`, `sd`, `vfs`, `fat32`, **`appkit.so`** (le noyau cherche un kit parmi
    les modules avant `SD0:/lib`) et `init.txt` (`src/svc/modules/`).
  * **`sd`** (`src/svc/sd/sd.asm`) : le pilote SDHCI du noyau inclus tel quel, sur le contrôleur
    mappé (`dev_map`) et l'horloge du firmware (`fw_call`) ; il sert `blk.sd0`, `blk.sd0p1`... ;
    une partition **exFAT est servie en lecture seule** (la partition personnelle).
  * `vfs` monte d'abord le volume reçu en arguments, prend un service de démarrage dans
    initramfs, les autres sur le disque, et fait les montages de `mounts.txt` dans un second
    thread (il sert déjà `SD0:`, d'où se chargent leurs services).
  * Le noyau lit ses propres fichiers par les services (`dist_charger` : `kload_file`,
    `kspawn_path`) ; un thread du noyau appelle un service avec réponse (pid « noyau »).
  * Corrigé au passage, un vieux bogue : un thread terminé était libéré sans quitter la liste
    des threads de son process (`futex_wake`, le réveil des services parcouraient de la mémoire
    libérée).
  * QEMU : bureau complet, `ramtest SD0` (écriture et relecture par les services) passe.
  * **Validé sur le PocketTerm** (28/10/2026) : démarrage en mode services (le pilote `sd`
    pilote l'EMMC2 depuis l'espace utilisateur), bureau, clavier, tactile, applications ;
    `SD0:/journal.txt` écrit par les services ; `chkdsk` de Windows : FAT32 sans erreur.
  * **FatFS** (`src/svc/fatfs/fatfs.asm`, 13 Ko) : **un seul service pour FAT32 et exFAT**, le format
    reconnu au montage (`fat32.asm` et `exfat.asm` du noyau inclus tels quels), lecture et
    écriture dans les deux formats (création, dossiers, suppression, taille à la fermeture :
    aiguillées selon le volume). Écriture exFAT validée d'abord sur une **image exFAT
    d'essai** (`IMG1:`, `SD0:/test_exfat.img`, `tools/mkdisk.py --exfat … --taille 16`), puis
    sur la partition exFAT de la carte QEMU, **relue ensuite par l'exFAT du noyau** sans
    erreur. `SD1 = blk.sd0p2, fatfs` : la partition personnelle, en **lecture et écriture**
    (choix de l'utilisateur, 28/10/2026) ; **validé sur le PocketTerm** : `ramtest SD1` sur la
    vraie partition (115 Go, clusters de 128 Ko), `chkdsk` de Windows sans erreur ensuite.
    Les services de systèmes de fichiers se nomment en `…FS` (FatFS, RamFS, plus tard FtpFS).
* **Élagage fait** (28/10/2026) — le **mode noyau a disparu** : le démarrage passe toujours par
  `init` (sans lui, le noyau le dit et ne monte rien). Point de retour : la branche git
  **`mode-noyau`** et l'étiquette **`avant-elagage`** (commit `110f9a0`).
  * Sortis du noyau : le pilote de la carte SD, FAT32, exFAT (vers `src/svc/sd/sdhci.asm`,
    `src/svc/fatfs/fat32.asm`, `exfat.asm`, où les services `sd` et FatFS les incluent), le cache
    de blocs (512 Ko de mémoire rendus), la table des volumes, le montage des partitions.
  * `vfs.asm` du noyau, renommé **`files.asm`**, ne garde que les descripteurs, les tubes, les chemins, le lancement des
    programmes ; tous les appels fichiers passent par les services (`remote.asm`).
  * Le canal de blocs (appel 93) ne sert plus que le stockage USB (disque 1) : une clé USB se
    servira par `sdblk usb0 1` + FatFS (à brancher avec hwmanager) ; le Wi-Fi garde les noms
    des registres SDHCI (`inc/sdhci.inc`).
  * Noyau Pi 4 : 142 Ko → 127 Ko ; plus de 1 000 lignes en moins (et ~2 950 déplacées).
  * Le noyau du **Pi 5** n'a plus de carte SD tant que le service `sd` ne connaît pas son
    contrôleur (il n'avait jamais été testé).
  * À faire (demande) : **remplacement des kits** — un kit reconstruit sur le disque doit
    remplacer la version en mémoire pour les programmes lancés ensuite, l'ancienne (marquée
    obsolète) restant à ceux qui l'utilisent jusqu'au dernier, puis libérée.

1. **Services nommés** (3.3-1), Elegant porté dessus sans changer son ABI ; un petit service
   d'essai (écho) et sa mesure d'aller-retour sur le Pi 4.
2. **Droits et accès au matériel** (3.3-2) : `dev_map`, `dma_alloc`, `fw_call`, réservés à
   `SD0:/sys`.
3. **`netd`** (chantier 5) : la pile et GENET déplacés tels quels, NetKit branché sur le
   service ; puis le Wi-Fi mis au point dans `netd`, et l'applet Wi-Fi du panneau.
4. **Canaux de trames** pour l'Ethernet USB (3.3-3).
5. **`sond`** (chantier 6) et AudioKit ; applet Son.
6. **Le protocole des services FS** (§4 : VFS, objet fichier, mappage), servi d'abord par
   `ftpfs` (`FTPx:`) alors que les fichiers de la carte sont encore dans le noyau ; `rdpd`.
7. **Le stockage sort** : modules de démarrage, `proc_create(image)`, pilote SD et partitions,
   FAT32 et exFAT en services, enfin le VFS lui-même (et le stockage USB par le même chemin).

### Toute la RAM (fait le 29/10/2026)

Fait comme prévu ci-dessous : voir ARCHITECTURE.md (`memmap.asm`, `pmm.asm`).
Avant, le noyau ne gérait que le **premier Go** (l'allocateur de pages, `PMM_MAX_PAGES`, et
la vue « en identité » du noyau). Un Pi 4 en a jusqu'à 8. Pour tout prendre :
* une **vue linéaire de toute la RAM** dans la moitié noyau (les pages ne s'adressent plus
  directement par leur adresse physique, mais par `adresse + décalage`) et un allocateur
  à la taille de la RAM (taille lue au firmware, comme aujourd'hui) ;
* des **zones** : certaines mémoires doivent rester basses — boîte aux lettres du firmware et
  DMA « ancien » du SoC (sous 1 Go, adresses de bus sur 30 bits), d'autres non (GENET, xHCI
  par PCIe : 40 bits) ; `dma_alloc` prendra un drapeau « mémoire basse » ;
* au-delà de 3 Go, la fenêtre des périphériques (`0xFC00_0000`) est contournée par le
  firmware : la RAM haute commence à `0x1_0000_0000` (deux plages à gérer).
C'est un chantier du noyau à faire avant que les services (`netd`, `sond`, le stockage)
n'aient besoin de beaucoup de mémoire.

## 6. Questions ouvertes

1. ~~Systèmes de fichiers dans le noyau ?~~ Non : ils sortent aussi (§4).
2. **USB** : la pile reste dans le noyau dans un premier temps (son stockage passera par des
   block devices).
3. ~~Droits~~ : tout programme de `SD0:/sys` (donc `SD0:/sys/svc`). Pas de droits accordés
   à des kits : un kit tourne *dans* le process de l'application (mêmes tables de pages, EL0),
   le noyau ne pourrait pas lui réserver un droit sans le donner à toute l'application, et
   deux applications pilotant le même matériel se gêneraient. **AudioKit** passe par un
   **anneau partagé** (décision du 28/10/2026) : `sond` donne à chaque client un tampon
   circulaire mappé chez lui ; le kit y écrit les échantillons directement (ni appel système ni
   IPC par écriture), `sond` mélange et alimente le matériel. Un mode exclusif (latence
   minimale, pour un jeu) pourra s'y ajouter.
4. ~~Modules de démarrage~~ : **noyau + `initramfs`** (décision du 28/10/2026). Le firmware
   charge les fichiers nommés (`initramfs a.mod,b.mod 0x02000000`, séparés par des virgules :
   il les met bout à bout) ; chacun commence par un en-tête de 64 octets (« ARMPIMOD »,
   taille, nom ; `src/svc/inc/module.inc`) que le noyau parcourt (`src/kernel/modules.asm`),
   puis il réserve leurs pages et efface les signatures. QEMU : `--modules` du pilote d'essai.
   Deux modules d'essai (`essai1`, `essai2`) valident le chargement sur le Pi.
