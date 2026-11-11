# ARMPI OS — architecture

ARMPI est un système d'exploitation 64 bits pour Raspberry Pi 4 (Pi 5 en cours), écrit entièrement en
assembleur AArch64 avec FASMARM, dans l'esprit de MenuetOS : petit, direct, graphique.

## Démarrage

1. Le firmware charge `kernel8.img` (Pi 4) ou `kernel_2712.img` (Pi 5) à `0x80000`, cœur 0
   en EL2 ; les cœurs 1-3 attendent dans la *spin table* (Pi 4) ou le firmware BL31 (Pi 5).
2. `kernel.asm` : passage EL2 → EL1h, vecteurs, BSS, UART, mémoire ARM (mailbox), framebuffer,
   MMU + caches, ordonnanceur du cœur 0, console.
3. Allocateur de pages, GIC-400, timer 1 kHz, réveil des cœurs 1-3 (`smp_start`).
4. Carte SD → montage des partitions (`SD0:` FAT32, `SD1:` exFAT), `SD0:/etc/options.txt`, USB,
   réseau (thread `wifi` : firmware puis connexion de `SD0:/etc/wifi.txt`), son, interface
   graphique, tactile, GPU, puis les programmes de `SD0:/etc/startup.txt`.

## Organisation des sources (`src/`)

La carte cible est choisie à l'assemblage : `fasmarm kernel.asm kernel8.img` (Pi 4, par
défaut) ou `fasmarm kernel.asm kernel_2712.img -d ARCH=pi5`. `kernel.asm` inclut
`arch/<carte>/board.inc` (adresses), le code générique, puis `arch/<carte>/arch.inc`
(pilotes). Les deux noyaux s'assemblent sous Windows comme sous ARMPI (auto-hébergement),
à l'identique.

| Dossier | Contenu |
|---|---|
| `src/` | code **générique** : noyau, systèmes de fichiers, réseau, interface, pilotes de protocole |
| `src/kernel/arch/common/` | pilotes communs aux Raspberry Pi 4 et 5, paramétrés par `board.inc` |
| `src/kernel/arch/pi4/` | Raspberry Pi 4 (BCM2711) |
| `src/kernel/arch/pi5/` | Raspberry Pi 5 (BCM2712 + RP1) : démarrage seulement, non testé ([PI5.md](PI5.md)) |
| `src/inc/` | en-têtes communs à plusieurs catégories (`armpi.inc`, `syscalls.inc` : numéros des appels, partagés avec le noyau, `kit.inc`, `applist.inc`, `notify.inc`, `sounddraw.inc`, `lib.inc`) et protocoles des services (`fs.inc`, `blk.inc`, `usb.inc`, `netd.inc`, `sound.inc`) |
| `src/lib/posix/` | la couche POSIX des programmes C (`libposix.a`, newlib, chaîne d'Onyx ; `tools/posix/build.sh`, hors du selfhost : [POSIX.md](POSIX.md)) |
| `src/kits/<kit>/` | un kit (`appkit`, `uikit`, `netkit`) : `.asm`, `.abi`, `.inc` → `SD0:/lib/<kit>.so` |
| `src/apps/<nom>/` | une application : `<nom>.asm`, `app.txt` (manifeste), ses `.inc` → `SD0:/apps/<nom>.app` |
| `src/bin/<nom>/` | une commande → `SD0:/bin/<nom>` |
| `src/sys/<nom>/` | un programme du système (Elegant, session du bureau) → `SD0:/sys/<nom>` |
| `src/svc/<nom>/` | un service → `SD0:/sys/svc/<nom>` ; `src/svc/inc/` : code commun aux services ; `src/svc/modules/` : modules de démarrage |

Un programme par dossier, nommé comme lui : le dossier décide de sa place sur la carte
(`build.ps1`). Un `.inc` utilisé par un seul programme est dans son dossier.

### Interface d'une carte (ce que `arch/<carte>/` fournit)

| Symboles | Rôle |
|---|---|
| `PERI_BASE`, `UART0_BASE`, `MBOX_BASE`, `GICD_BASE`, `GICC_BASE`, `SD_BASE_A/B` | adresses (`board.inc`) |
| `uart_init`, `uart_putc` | console série |
| `mmu_init`, `TCR_VALUE`, `SCTLR_*` | tables de pages du noyau (carte mémoire de la carte) |
| `smp_wake` | réveil d'un cœur (spin table, PSCI) ; le reste du démarrage SMP est commun |
| `mbox_*`, `fb_init` | firmware VideoCore (mémoire, horloges, MAC, framebuffer) |
| `sd_init`, `sd_transfer`, `board_sd_clock` | carte SD |
| `gpio_fsel`, `gpio_get`, `gpio_set`, `gpio_pull` | broches (I2C logiciel du tactile) |
| `sdio_host_init`, `sdio_host_bus4`, `sdio_cmd52/53`, `sdio_f0r/f1r/f0w/f1w` | hôte SDIO de la puce WiFi |
| `board_is_emulated` | vrai si QEMU (les pilotes de matériel absent s'abstiennent) |
| `genet_init`, `audio_init`, `sys_snd_*`, `v3d_init` | pilotes propres à la carte (des souches sur Pi 5) |

### Modules

| Fichier | Rôle |
|---|---|
| `kernel.asm` | point d'entrée, ordre d'initialisation, options de démarrage, BSS |
| `inc/macros.inc`, `inc/mmu.inc` | macros (`movl`, `cpu_index`...), descripteurs de pages ARMv8 |
| `cache.asm`, `spinlock.asm`, `pmm.asm` | caches, verrous et atomiques, pages physiques (zones basse et haute) |
| `memmap.asm` | RAM annoncée par le firmware (mailbox, DTB, révision), mappage du noyau au-delà du 1er Go |
| `vmspace.asm` | régions paresseuses, fautes de page des process, copie sur écriture, appels `VM_*` (0x06–0x0C) |
| `preload.asm` | exécutables préchargés (`SD0:/etc/preload.txt`) : modèles, `SPAWN_PRELOADED`, `IMAGE_*` |
| `sched.asm` | threads, ordonnanceur préemptif SMP (priorités), contexte FP/NEON, `TPIDR_EL0` |
| `threads.asm` | appels des threads (0x20–0x2A) : création, fin, join, priorité, nom, kill, liste, futex |
| `gic.asm`, `timer.asm` | interruptions, IPI, tick par cœur, attentes (`udelay`, `delay_ms`) |
| `vectors.asm` | exceptions : IRQ, appels système, piège FP, fautes |
| `vm.asm`, `proc.asm`, `syscall.asm` | espaces d'adressage (ASID), chargeur ELF64, appels système |
| `spawn.asm` | création d'un process depuis une image donnée par AppKit (`spawn_image`), `proc_flags`, `wait` ; le noyau ne lit aucun fichier : chemins, descripteurs, tubes et lecture des exécutables sont chez AppKit et les services (`vfs`, FatFS, RamFS, `sd` : `src/svc`, docs/FICHIERS.md) |
| `kits.asm` | kits partagés : table, chargement d'une image, mappage par process (`lib_open` = kit_find, `kit_register`) ; seul `appkit.so` vient des modules d'initramfs |
| `bootfiles.asm` | fichiers donnés au noyau par init (`boot_file` : `options.txt`, firmware et réglages du WiFi) |
| `console.asm` | console texte (journal `klog`) |
| `net.asm` | pile TCP/IP, files de trames, carte active (`netif_set` : Ethernet > WiFi > USB) |
| `wifi.asm` | WiFi CYW43455 : backplane, firmware, SDPCM/ioctls, scan, WPA2 (hôte SDIO fourni par la carte) |
| `ws.asm` | mécanismes du serveur graphique (tampons partagés, requêtes, événements, écran) |
| `arch/common/` | `pl011.asm`, `mailbox.asm`, `fb.asm`, `sdhci.asm`, `smp.asm` |
| `arch/pi4/` | `board.asm` (spin table, horloges), `gpio.asm`, `uart.asm`, `mmu.asm`, `sdio.asm` (Arasan), `genet.asm`, `audio*.asm`, `v3d.asm` |
| `arch/pi5/` | `board.asm` (PSCI), `gpio.asm` (RP1), `uart.asm`, `mmu.asm`, `stubs.asm` |

## Mémoire

* **RAM vue** (`memmap.asm`) : sous 1 Go, la mailbox (`GET_ARM_MEMORY` : la RAM ARM sous le
  VideoCore) ; au-delà, les nœuds `memory` du DTB (adresse donnée par le firmware dans `x0`) et
  ses réservations. Si la révision de la carte (taille de RAM, bits 20–22) en annonce plus que le
  DTB (DTB absent, ou celui de QEMU `raspi4b` : un seul nœud de 960 Mo pour 2 Go), la disposition
  du BCM2711 pour cette taille : `[1 Go, min(taille, 0xFC000000)[` puis `[4 Go, taille[` (Pi 4 de
  2, 4 ou 8 Go). Journal : `RAM vue : N Mo (source), zone basse … zone haute …` et
  `Plages de RAM : … ; révision …`.
* Noyau : **identité sur toute la RAM** (RAM ARM cacheable, mémoire VideoCore non cacheable,
  blocs de 1 Go au-delà du 1er Go, de 2 Mo dans le 4e Go à côté des périphériques) et
  `0xFC000000-0xFFFFFFFF` (périphériques), dans les entrées L1[0..255] partagées par tous : une
  page physique, basse ou haute, s'adresse directement.
* **Allocateur de pages** (`pmm.asm`) : une bitmap sur toute la RAM (après le noyau), suivie des
  compteurs de références (copie sur écriture, plus bas), deux zones.
  **Zone basse** (< 1 Go) : ce qui donne une adresse physique au matériel —
  `pmm_alloc_low(_n)` : `DMA_ALLOC` (DMA « lite » 30 bits, DMA 2D d'Elegant, `sound` et ses
  pilotes, `usbd`, `sd`, `genet`), tampons du V3D, piles des cœurs secondaires (lues MMU coupée),
  `BUF_CREATE` avec `BUF_LOW`. **Zone haute** : tout le reste la préfère (`pmm_alloc(_n)` : pages
  et tables des process, PCB, piles du noyau, kits, images, tampons partagés), puis se rabat sur la
  zone basse. Les tampons de la mailbox sont des variables du noyau (zone basse). La zone haute
  s'ouvre une fois `SD0:/etc/options.txt` lu (`Mémoire haute : +N Ko`), sauf option
  `no_highmem` (le 1er Go seul, pour un diagnostic).
* Pi 5 : même code (DTB du firmware) ; non essayé, la RAM du Pi 5 au-delà du 1er Go n'est prise
  que si son DTB la décrit (pas de repli par la révision : disposition du BCM2712 non codée).
* Process : `0x40_0000_0000 - 0x7F_FFFF_FFFF` (L1[256..511]), pages 4 Ko non globales,
  ASID propre. Code à `0x40_0000_0000`, tas par `sbrk` (jusqu'à `0x50_0000_0000`), kits à
  `0x50_0000_0000`, **régions de `VM_MAP`** (mmap) dans l'arène `0x58_0000_0000 - 0x68_0000_0000`,
  tampons partagés (surfaces de fenêtres) à `0x68_0000_0000 + n × 16 Mo`, framebuffer chez
  le serveur graphique à `0x70_0000_0000`, registres et DMA à `0x78_…`/`0x7A_…`, pile 256 Ko sous
  `0x7F_FFFF_0000`.
* **Pagination à la demande** (`vmspace.asm`) : régions réservées, peuplées à la première touche
  (faute de page résolue, l'accès reprend) — régions de `VM_MAP`, `.bss` de chaque exécutable, pile
  du thread principal (sauf ses 16 Ko du haut). `VM_UNMAP`, `VM_PROTECT`, `VM_ADVISE`, `VM_QUERY`,
  `VM_STATS`, `VM_PAGEINFO` ([ABI.md](ABI.md), 0x06–0x0C) pour la couche POSIX. Le noyau peuple
  un tampon avant d'y lire ou écrire (`check_user`) ; une faute EL1 sur une page paresseuse ou en
  copie sur écriture est résolue, jamais fatale.
* **Images des exécutables** (`preload.asm`) : tout lancement passe par une image, chargée une
  fois (clé : chemin résolu + taille) — le fichier est lu par AppKit directement dans les pages de
  l'image (`IMAGE_CREATE` / `IMAGE_SEAL`, sans tampon ni copie, sans limite de taille) ; un
  lancement reprend ses pages — code et lecture seule partagés (`PTE_SHARED`), données en **copie
  sur écriture** (`PTE_COW`, copiées page par page à la première écriture), `.bss` à la demande.
  Image épinglée (`SD0:/etc/preload.txt`, commande `preload`) ou passagère (rendue avec son dernier
  process) ; vfs fait oublier l'image d'un fichier ouvert en écriture ou supprimé (`unload` aussi).
  Option `no_preload` : l'ancien chemin (tampon de 16 Mo, `SPAWN_IMAGE`). [ABI.md](ABI.md),
  « Images des exécutables ».
* **`fork` en copie sur écriture** (`fork.asm`, `vmspace.asm`, 11/11/2026) : un **compteur de
  références par page** (`pmm.asm` : 16 bits par page de toute la RAM vue, après la bitmap, soit
  1 Mo pour 2 Go, 4 Mo pour 8 Go) compte les détenteurs **de plus** d'une page privée ; 0 pour
  toute autre page (un seul détenteur, comme avant). `pmm_ref`, `pmm_put` (libère au dernier) :
  atomiques (`LDAXRH`/`STLXRH`), sans verrou, appelables sous `P_VMLOCK`. Le compteur ne monte
  que sous `P_VMLOCK` d'un détenteur (`fork`, mapcaller) : une faute qui y lit 0 tient seule la
  page. Les trois bits logiciels d'une entrée :

  | Entrée | Ce que c'est | Fin de l'espace, `munmap`, `DONTNEED` | `fork` | Écriture |
  |---|---|---|---|---|
  | sans bit | page privée | `pmm_put` | partagée (+1) ; inscriptible : lecture seule + `PTE_COW` chez le père et le fils | permise si RW |
  | `PTE_COW` | privée partagée par `fork` (ou qui l'a été) | `pmm_put` | idem | compteur à 0 : inscriptible sur place ; sinon copiée, `pmm_put` de l'ancienne |
  | `PTE_PIN` (bit 58) | privée épinglée par mapcaller (l'entrée tient une référence) | `pmm_put` | **copie immédiate** (un service y écrit peut-être) | permise si RW ; avec `PTE_COW` : copiée |
  | `PTE_SHARED` (55) | page d'un objet : image, code d'un kit, mémoire partagée, tampon, écran, registres, DMA | jamais | telle quelle, une référence à l'objet | jamais si elle ne l'est pas |
  | `PTE_SHARED` + `PTE_COW` (56) | objet en copie sur écriture (données d'une image, `VM_COW`) | jamais | telle quelle | copiée, l'objet garde la sienne |

  `fork` passe les pages inscriptibles du père en lecture seule (droits seuls : pas de rupture
  avant construction), puis invalide les TLB de son ASID sur tous les cœurs avant de rendre
  `P_VMLOCK` et avant que le fils tourne ; les autres threads du père continuent (une écriture
  passée avant l'invalidation tombe dans la page encore commune, comme avant le `fork` ; après,
  elle fait une faute qui copie). `mprotect` garde les trois bits et met `PTE_COW` sur une page
  partagée qui devient inscriptible ; `sbrk` ne rend jamais inscriptible une page `PTE_COW`.
  `VM_PAGEINFO` : `PI_FORKED` (128). Mesures (QEMU) : un `fork` d'un process de 16 Mo passe de
  55 à 3–6 ms, 200 `fork` + `exit` de ce process de 8,3 à 2,3–2,7 s ; une boucle bash (tubes,
  `$(…)`, sous-shells) va 26 % plus vite. Une première écriture coûte une faute (sous QEMU,
  ~100–150 µs avec la TLBI diffusée ; un fils qui réécrit 16 Mo met 0,4–0,6 s au lieu de 0,06 s).
  Essai : `cowtest` (33 essais).

## Ordonnancement

* Une liste circulaire de threads par cœur, quantum 10 ms, tick 1 kHz par cœur ; trois
  priorités (normale, haute, temps réel souple) avec vieillissement : le plus prioritaire passe
  devant, aucun thread prêt n'attend indéfiniment ([ABI.md](ABI.md), threads).
* `TPIDR_EL0` (TLS d'EL0) sauvé et rechargé à chaque commutation ; `threads.asm` : création
  (TLS, priorité, nom), fin avec code, `join`, `kill` différé (au retour en EL0), liste, futex
  avec délai.
* Préemption au retour d'IRQ (noyau préemptible, verrous en `irqsave`).
* IPI (SGI 0) pour réveiller un cœur ; `thread_sleep`, futex, threads morts récupérés.
* FP/NEON : premier usage piégé (registres remis à zéro), puis sauvegarde complète
  q0-q31/FPCR/FPSR à chaque commutation des threads qui l'utilisent.

## Interface graphique

* En espace utilisateur, comme dans Onyx : **Elegant** (`SD0:/sys/elegant`,
  `src/sys/elegant/elegant.asm`) est le serveur graphique, lancé par le noyau au démarrage. Le noyau
  ne garde que des mécanismes (`ws.asm`) ; sans serveur, la console texte garde l'écran.
* Elegant : un cœur commun (`elegant/core.inc` : fenêtres, recomposition dans un tampon
  arrière, routage des entrées, requêtes) et un mode (`elegant/desktop.inc` : fond, barre des
  tâches, menu, horloge), choisi par `shell=` de `SD0:/etc/system.ini`.
* Une surface 32 bpp par fenêtre, dans un tampon partagé créé par le serveur et mappé chez le
  programme : il dessine sans appel système puis signale la zone (requête au serveur).
* Événements dans la file du process (64) : clavier traduit par le noyau (disposition
  active), souris (capture pendant un clic), molette, focus, fermeture.

## Construction et test

```powershell
.\build.ps1          # apps + noyau + image SD de test (build\sd.img)
.\build.ps1 run      # QEMU raspi4b avec la carte, clavier et tablette USB
.\build.ps1 sd E:    # copie firmware + noyau sur une vraie carte (FAT32)
```

Tests automatisés : `python -I tools/qemu_drive.py script.txt` (clics, frappes, captures).

## Diagnostic

* **F12** (noyau, `src/kernel/diag.asm`) : écrit dans le journal du noyau (liaison série,
  `journal`) l'état des verrous, l'heure, le framebuffer, la file d'entrées, les requêtes au
  serveur graphique, et pour chaque cœur sa dernière adresse interrompue (noyau, EL0) et chaque
  thread avec sa pile d'appels du noyau.
* **F11** (Elegant) : chaque fenêtre (rôle, géométrie, mises à jour reçues, pixels de contrôle)
  et l'historique des 64 derniers événements (requêtes, zones salies, recompositions) ;
  **F10** : tout recomposer.
* `tools/fas_sym.py` : retrouve l'étiquette d'une adresse à partir d'un `.fas`
  (`fasmarm -s`) ; `tools/qemu_drive.py` : `hmp <commande>` (ex. `pmemsave`).
