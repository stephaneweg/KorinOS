# Korin OS sur PC x86-64 : étude

Étude du 11/11/2026, avant tout travail. Le propriétaire veut savoir si Korin peut tourner, plus tard, sur **son PC
actuel**, avec au moins **le son, l'USB, le réseau Wi-Fi et la 3D**. Cible unique : ce PC-là, pas « le PC » en général.

Décisions déjà prises par le propriétaire :

* les **kits gardent leur interface** (les tables `.abi`) ; leur logique est **réécrite en assembleur x86-64** ;
* la partie assembleur du noyau et les pilotes sont réécrits ;
* (11/11) Korin reste un **micronoyau** sur PC comme sur le Pi ;
* (11/11) dans la branche `claude/x86-64-pc`, le code qui peut servir **aux deux machines est réécrit en C**, pour la
  portabilité ; seul ce qui dépend du processeur ou de la carte reste en assembleur (FASM pour x86, FASMARM pour ARM) ;
* (11/11) premiers démarrages du ZBook sur **carte SD** (lecteur Realtek PCIe `10EC:5261`), plus tard sur le SSD.

## 0. État de la branche `claude/x86-64-pc`

Jalon 1 fait le 11/11/2026, **essayé sous QEMU q35 + OVMF seulement** :

| Fichier | Langage | Rôle |
|---|---|---|
| `src/kernel/arch/pc/boot/bootx64.asm` | FASM | chargeur UEFI : GOP, RSDP, lecture de `\korin\kernel.elf`, tables de pages du noyau, carte mémoire, `ExitBootServices` |
| `src/kernel/arch/pc/boot/bootinfo.inc`, `arch/pc/bootinfo.h` | | bloc de démarrage commun au chargeur et au noyau |
| `src/kernel/arch/x86_64/entry.asm`, `isr.asm` | FASM | entrée du noyau, `gdt_load`, 256 vecteurs |
| `src/kernel/arch/x86_64/gdt.c`, `idt.c`, `paging.c`, `cpu.h` | C | GDT + TSS (piles IST), IDT et écran d'exception, tables de pages (toute la mémoire à `0xFFFF800000000000`, framebuffer en écriture combinée, noyau à `0xFFFFFFFF80000000`) |
| `src/kernel/arch/pc/acpi.c`, `apic.c`, `ps2.c`, `pc_main.c` | C | MADT, FADT, 8259 masqués, APIC local (xAPIC et x2APIC), IOAPIC, minuterie à 1000 Hz étalonnée sur la minuterie ACPI ou le PIT, clavier PS/2 AZERTY, démarrage |
| `src/kernel/include/korin/arch.h` | C | **interface entre le noyau portable et une architecture** |
| `src/kernel/core/console.c`, `lib.c`, `pmm.c`, `monitor.c` | C | **portables** : console sur framebuffer (police Spleen du Pi), `kprintf`, pages physiques par bitmap, moniteur (`help`, `info`, `mem`, `uptime`, `clear`, `reboot`) |
| `tools/pc/build.sh` | sh (WSL) | construction : FASM de Windows par l'interop, gcc et ld de WSL |
| `tools/pc/qemu_pc.py` | Python | `run` (fenêtre) ou `test` (sans écran : capture, touches, journal du port 0xE9) |
| `tools/pc/sd.ps1` | PowerShell | copie sur une carte SD ou une clé FAT32 amovible, sans formater |

Le dossier `src/kernel/core` est le début du noyau commun : le Pi pourra l'adopter morceau par morceau, en fournissant
`korin/arch.h` depuis son code ARM64. Le moniteur et le clavier dans le noyau sont **provisoires** : dans le micronoyau,
le clavier ira dans un service dès qu'il y aura des process.

Construire, essayer, copier sur la carte :

```
wsl sh tools/pc/build.sh
python -I tools/pc/qemu_pc.py run
powershell -File tools\pc\sd.ps1 E:
```

Sur la carte : `EFI\BOOT\BOOTX64.EFI` et `korin\kernel.elf`. Le chargeur lit le noyau par l'UEFI, avant d'en sortir :
le noyau n'a donc pas besoin de pilote du lecteur de cartes pour démarrer. Pour lire d'autres fichiers, le chargeur
devra charger un disque en mémoire (ramdisk) de la même façon, en attendant un pilote du lecteur Realtek (ou de l'USB).
Le HP doit permettre de démarrer sur la carte SD (menu F9) ; sinon, la même copie marche sur une clé USB.

Les décisions qui restent à prendre sont rassemblées à la fin (§11).

---

## 1. La machine

Relevé fait sous Windows (WMI) le 11/11/2026.

| Élément | Matériel | Identifiant PCI |
|---|---|---|
| Machine | HP ZBook Fury 17.3 G8 (carte mère HP `886D`), 32 Go | |
| Processeur | Intel Core i7-11800H (Tiger Lake-H, 8 cœurs, 16 fils) | |
| Micrologiciel | **UEFI** | |
| GPU intégré | Intel UHD Graphics (Tiger Lake, **Gen12 Xe-LP**, 32 EU) | `8086:9A60` |
| GPU séparé | NVIDIA T1200 Laptop (Turing TU117) | `10DE:1FBC` |
| Son | contrôleur HDA / Smart Sound d'Intel + codec **Realtek ALC285** ; HDA de la NVIDIA (HDMI) | `8086:43C8`, codec `10EC:0285` |
| USB | xHCI du PCH (USB 3.2) et xHCI Thunderbolt 4 du processeur (USB 3.1) | `8086:43ED`, `8086:9A17` |
| Wi-Fi | **Intel Wi-Fi 6 AX201** 160 MHz (CNVi) | `8086:43F0` |
| Ethernet | Intel I219-V (famille e1000e) | `8086:15FA` |
| Disque | NVMe SK hynix PC711 512 Go (**porte Windows**) | |

Le clavier interne est très probablement en PS/2 (contrôleur i8042 émulé par l'EC) et le pavé tactile en **I2C-HID**
(bus I2C LPSS d'Intel) ; à confirmer par les tables ACPI.

---

## 2. Ce qui existe aujourd'hui

Comptes de lignes du dépôt (sources `.asm`/`.inc` d'un côté, `.c`/`.cpp`/`.h`/`.cs` de l'autre) :

| Zone | Assembleur ARM64 | C / C++ / C# |
|---|---:|---:|
| `src/kernel` | 21 752 | 0 |
| `src/svc` | 54 079 | 0 |
| `src/sys` | 27 268 | 3 888 |
| `src/kits` | 83 634 | 386 |
| `src/bin` | 185 130 | 279 |
| `src/apps` | 99 305 | 225 232 |
| `src/lib` | 0 | 32 288 |
| `src/inc` | 12 823 | 813 |
| **Total** | **≈ 484 000** | **≈ 263 000** |

Plus grands kits : AudioKit 31 577 lignes (surtout les codecs MP3, Vorbis, Opus, FLAC), UIKit 27 571, ArchiveKit
6 695, GpuKit 3 942, AppKit 3 922. Plus grands services : `usbd` 6 145, `netd` 6 131, `fatfs` 5 572, `wifi` 4 957,
`vfs` 3 760.

Ce qui aide :

* `src/kernel/arch/{common,pi4,pi5}` sépare déjà ce qui dépend de la **carte** (`docs/ARCHITECTURE.md`, « Interface
  d'une carte »). Mais tout le reste du noyau suppose AArch64 : il faut une séparation de plus, celle du
  **processeur**.
* Les pilotes sont déjà **hors du noyau**, dans des services (`usbd`, `sound`, `wifi`, `v3d`…) : leur interface avec
  le reste du système (`src/inc/usb.inc`, `sound.inc`, `netd.inc`…) ne change pas.
* Le montage Mesa du Pi (`tools/mesa/libdrm`, faux libdrm qui sert de winsys) est le modèle de la 3D sur PC (§7.4).
* `tools/x86toa64.py` a déjà traduit FASM de x86 vers ARM64, à l'octet près : un traducteur de source à source a déjà
  marché une fois dans ce projet (§9).

---

## 3. Forme du portage

### 3.1 Une deuxième architecture, pas une troisième carte

Proposition :

| Dossier | Contenu |
|---|---|
| `src/kernel/arch/aarch64/` | ce qui dépend d'ARM64 et qui est aujourd'hui mêlé au code générique (vecteurs, MMU, changement de contexte, GIC, appels système) |
| `src/kernel/arch/x86_64/` | la même chose pour x86-64 |
| `src/kernel/arch/pc/` | la « carte » PC : UEFI, ACPI, PCIe, APIC, console GOP |

`build.ps1 -d ARCH=pc` choisit l'assembleur (**FASM** pour x86-64, FASMARM pour ARM64) et les dossiers.

### 3.2 L'assembleur

**FASM 1.73** sait produire de l'ELF64 x86-64 et du PE64 (pour l'application UEFI, §4.1). C'est l'outil d'origine
de FASMARM : mêmes macros, mêmes `struc`, mêmes directives. **Attention** : dans FASM d'origine, `dw` = 16 bits et
`dd` = 32 bits (`dq` = 64), alors que dans FASMARM `dw` = 32 bits et `dd` = 64 bits. Les sources x86 doivent suivre la
convention de FASM, et les en-têtes partagés entre les deux (structures publiques, `syscalls.inc`) ne doivent pas
employer `dw`/`dd` sans macro de taille commune (`d32`, `d64`…).

Auto-hébergement : FASM est lui-même écrit en assembleur x86 ; il tournerait **nativement** sous Korin x86, avec la
même couche d'appels système que celle du port de FASMARM.

### 3.3 La chaîne C

Une cible `x86_64-korin-elf` pour GCC 14.2, sur le modèle d'`aarch64-onyx-elf` (WSL), avec newlib et `libposix`. À
vérifier : les morceaux de `libposix` en assembleur ou en asm en ligne (souches d'appels système, `setjmp`, signaux),
qui sont à réécrire. Tout le C, le C++, le C# (le runtime .NET a son JIT x64), FreeBASIC, Mesa, SDL, Skia, Avalonia,
OpenSSL, WebKit se recompilent.

---

## 4. Le noyau x86-64

Le noyau compte 21 752 lignes ; tout ce qui suit est à écrire, le reste (ordonnanceur, process, IPC, `SVC_FORWARD`,
signaux, mmap) se transpose en gardant la même logique.

### 4.1 Démarrage

* Une petite **application UEFI** en FASM (PE64), `BOOTX64.EFI` sur une clé USB FAT32 : elle lit le **framebuffer
  GOP**, la carte mémoire, l'adresse des tables ACPI (RSDP), charge le noyau ELF et les fichiers de démarrage, appelle
  `ExitBootServices` et saute dans le noyau **déjà en mode long** (pas de code 16 bits pour le cœur 0).
* Le framebuffer GOP remplace `fb_init` (mailbox du VideoCore). Sa résolution est celle que l'UEFI a choisie
  (l'écran interne en natif).

### 4.2 Processeur

| Fonction | ARM64 aujourd'hui | x86-64 |
|---|---|---|
| Vecteurs d'exception | table `VBAR_EL1` | **IDT** (256 entrées), **GDT**, **TSS** (piles IST pour les fautes doubles et les NMI) |
| Tables de pages | 4 niveaux, granule 4 Ko, `TTBR0/1` | 4 niveaux (PML4), pages de 4 Ko, 2 Mo, 1 Go, `CR3`, bits NX, PCID si utile |
| Appels système | `svc #0` | `syscall`/`sysret` (MSR `LSTAR`, `STAR`, `FMASK`), `swapgs` |
| Contexte FP | NEON (`q0`–`q31`) | **XSAVE**/`XRSTOR` (SSE, AVX2 ; AVX-512 existe sur ce processeur) |
| Interruptions | GIC-400 | **LAPIC** + **IOAPIC**, et surtout **MSI/MSI-X** pour les périphériques PCIe |
| Minuterie | timer générique ARM | minuterie du LAPIC en mode TSC-deadline, TSC invariant pour l'heure, HPET en secours |
| SMP | spin table, PSCI | `INIT`-`SIPI`-`SIPI` vers les cœurs de la MADT, avec un **trampoline 16 bits** sous 1 Mo |
| Atomiques et verrous | `ldaxr`/`stlxr`, `dmb` | `lock cmpxchg`, `xadd`, `pause` ; le modèle mémoire de x86 est plus fort : la plupart des barrières disparaissent |
| Invalidation du TLB sur les autres cœurs | `tlbi …is` (diffusé par le matériel) | **IPI** de « shootdown » (logiciel) |
| Caches et DMA | entretien explicite (`dc civac`) | DMA **cohérent** : l'entretien des caches disparaît des pilotes |

### 4.3 Carte PC

* **ACPI, tables statiques** : MADT (cœurs, APIC), MCFG (adresse ECAM du PCIe), FADT, HPET, DMAR (à ignorer au
  début : laisser l'IOMMU éteint). Suffisant pour démarrer.
* **ACPI, AML** : il faut un interpréteur pour l'extinction (`_S5`), la batterie, le capot, les boutons, les
  périphériques I2C (pavé tactile) et parfois le routage des interruptions. **ACPICA** (C, double licence BSD/GPL) se
  compile en service ; l'écrire en asm n'a pas d'intérêt.
* **PCIe** : énumération par ECAM, BAR, MSI-X ; un service `pcid` qui remplace la découverte par adresses fixes du Pi.
* Console série : ce PC n'en a pas. Débogage par l'écran GOP, par QEMU (`-serial`) et plus tard par le réseau.

---

## 5. L'ABI des kits et des appels système

### 5.1 Convention d'appel

Proposition : **System V AMD64**, celle de GCC, pour que le C appelle les kits sans adaptateur.

| | ARM64 (aujourd'hui) | x86-64 |
|---|---|---|
| Arguments entiers | `x0`–`x7` | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`, puis **la pile** |
| Arguments flottants | `v0`–`v7` | `xmm0`–`xmm7` |
| Retour | `x0` (`x1`) | `rax` (`rdx`) |
| Registres sauvegardés par l'appelé | `x19`–`x28`, `x29`, `x30` | `rbx`, `rbp`, `r12`–`r15` |
| Alignement de la pile | 16 octets | 16 octets **avant le `call`** |
| Appel système | `x8` = numéro, `x0`… | `rax` = numéro, `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` (`rcx` et `r11` détruits par `syscall`) |

Conséquences :

* une fonction de kit à 7 ou 8 arguments les reçoit par la pile sur x86 ; la macro d'entrée des kits s'en charge une
  fois pour toutes ;
* 16 registres au lieu de 31 : la logique des kits ne se traduit pas ligne à ligne, elle se réécrit (variables sur la
  pile, boucles repensées) ;
* les numéros d'appels système (`src/inc/syscalls.inc`) restent les mêmes ; seule la macro `sys` change.

### 5.2 Le `.abi` comme source unique

Les deux architectures partagent les tables `.abi` (même ordre, même numérotation, « on ajoute à la fin » pour les
deux à la fois). Les en-têtes C, les `.bi` de FreeBASIC, les déclarations C# et les souches de chaque architecture
devraient en être **générés**, pour que les deux versions ne puissent pas diverger.

### 5.3 Structures publiques

Les deux architectures sont en 64 bits little-endian : les structures publiques (événements, blocs de fenêtre,
tampons audio…) gardent les **mêmes champs aux mêmes décalages**. Il reste à vérifier les alignements des champs de
8 octets et des `double`, qui suivent les mêmes règles dans les deux ABI.

### 5.4 Tests communs

Une suite de tests en **C**, par kit, qui appelle l'API et compare les résultats (images rendues par UIKit, sorties des
codecs d'AudioKit, archives d'ArchiveKit…), sans rien de propre à ARM. Recompilée pour les deux architectures, elle
prouve que la version x86 se comporte comme l'originale. C'est la condition pour réécrire 84 000 lignes de kits sans
dériver.

---

## 6. QEMU d'abord

QEMU `-M q35` avec le micrologiciel **OVMF** (UEFI) émule, sur ce même PC, presque tout ce qu'il faut pour la première
moitié du travail, sans redémarrer :

| Besoin | Émulé par QEMU | Sur le ZBook |
|---|---|---|
| UEFI, GOP, ACPI | OVMF | UEFI d'HP |
| USB | `qemu-xhci` | xHCI Intel |
| Son | `intel-hda` + `hda-duplex` | HDA + ALC285 |
| Réseau filaire | `e1000e` (82574) | I219-V (même famille, autre PHY), **sans câble** chez le propriétaire |
| Réseau USB | `usb-net` (ECM, RNDIS) | partage de connexion d'un téléphone |
| Stockage | `usb-storage`, NVMe, AHCI | clé USB |
| Clavier | PS/2 | i8042 (EC) |
| 3D | aucun GPU Intel | Gen12 |
| Wi-Fi | aucun | AX201 |

Avec KVM (WSL2 ou Linux), QEMU x86 tourne à vitesse native : bien plus rapide que l'émulation `raspi4b`.

---

## 7. Les quatre objectifs

### 7.1 USB : facile

* Deux contrôleurs **xHCI** Intel. Korin en a déjà un pilote (`src/svc/usbd/xhci.inc`, pour le VL805 du Pi 4 et le RP1
  du Pi 5) : même standard, même logique, à réécrire en x86 avec le PCIe et les MSI-X.
* Le xHCI Thunderbolt (`9A17`) sert les ports USB-C ; il se programme comme un xHCI ordinaire. Le multiplexage des
  ports USB-C est fait par le micrologiciel (PMC) ; le Thunderbolt lui-même est hors sujet.
* Les classes USB déjà écrites (`usbkeyboard`, `usbmouse`, `usbstorage`, `usbaudio`, `usbnet`, `usbgamepad`) passent
  par `usb.inc` et se réécrivent sans changer d'interface.
* Le DMA est cohérent sur x86 : tout l'entretien des caches du pilote du Pi disparaît.

### 7.2 Son : moyen

* Contrôleur **HDA** d'Intel, en mode HDA classique (pas le DSP Smart Sound) : CORB/RIRB pour parler au codec, flux
  BDL en DMA. La spécification Intel HDA est publique.
* Codec **Realtek ALC285** : parcours du graphe de widgets (spécification HDA), puis les réglages propres à HP
  (GPIO de l'ampli des haut-parleurs, DEL de sourdine, coefficients du codec). Les correctifs de Linux
  (`patch_realtek.c`, GPL) servent de **carte**, sans copie, comme Circle pour le Pi.
* Obtenus ainsi : **haut-parleurs, prise casque, micro de la prise casque**. Le casque USB passe par `usbaudio`.
* Hors de portée au début : les **micros numériques intégrés** (DMIC), reliés au DSP, qui demandent son firmware
  (SOF). Le son HDMI (HDA de la NVIDIA ou de l'Intel) est pour plus tard.
* Le service `sound` et AudioKit ne changent pas d'interface ; seul un pilote `hdaudio` s'ajoute, à côté de
  `pwmaudio` et `hdmiaudio`.

### 7.3 Réseau : le Wi-Fi dès le premier démarrage

Le propriétaire **n'a pas de câble Ethernet** : sur le ZBook, le seul réseau est le Wi-Fi. Le pilote de l'AX201 fait
donc partie de la **première version** qui démarre sur la machine, pas de la fin. Or `netd`, telnet, FTP, le dépôt de
paquets et le débogage à distance dépendent tous du réseau.

**Ethernet I219-V : seulement sous QEMU.** Le pilote e1000e (descripteurs en anneaux, documentation Intel publique)
sert à mettre au point `netd` sous QEMU (`-device e1000e`). Le PHY de l'I219 passe par MDIO et diffère de l'82574 de
QEMU ; la version du ZBook n'est pas prioritaire.

**Filet de secours : le partage de connexion USB d'un téléphone.** `usbnet` ne gère aujourd'hui que **CDC-ECM**. Les
téléphones Android partagent leur connexion en **RNDIS** (anciens) ou **CDC-NCM** (récents) ; l'iPhone utilise un
protocole d'Apple, hors sujet. Ajouter NCM, et RNDIS si besoin, à `usbnet` donne un réseau sur le ZBook **sans
dépendre du Wi-Fi**, pour mettre au point le Wi-Fi lui-même à distance. Ce sont de petits ajouts à un pilote existant,
mis au point sous QEMU (`usb-net` sait aussi faire RNDIS).

**Mise au point sans QEMU.** QEMU n'émule aucune puce Wi-Fi Intel (et le passage du vrai AX201 à une machine virtuelle
demande Linux et VFIO, pas Windows). Le pilote se met au point **sur le ZBook** : traces à l'écran, journal écrit sur
la clé USB, puis par le réseau du téléphone (ci-dessus) dès qu'il marche.

**Commencer par un réseau ouvert.** Un point d'accès sans mot de passe (partage du téléphone en mode ouvert, le temps
des essais) sépare les deux problèmes : d'abord le firmware, le balayage et l'association, ensuite seulement le WPA2.

**Wi-Fi AX201 : le plus dur des quatre objectifs.**

Sur le Pi, la puce Broadcom est « FullMAC » : son firmware fait l'association **et** le WPA2 (supplicant `idsup` de la
puce). Korin n'a donc **ni pile 802.11 ni supplicant**. Avec l'AX201, le système doit fournir :

| Morceau | Contenu |
|---|---|
| Transport PCIe | chargement du firmware (`iwlwifi-QuZ-a0-hr-b0-*.ucode`, binaire redistribuable, ≈ 1,5 Mo), anneaux de commandes et de trames (TFD), « context info », interruptions MSI-X |
| Commandes du firmware | jeu de commandes « MVM » : contexte PHY, MAC, station, balayage, clés ; le firmware fait l'adaptation du débit et le chiffrement matériel |
| Pile 802.11 (MLME) | balayage, authentification, association, gestion des trames, itinérance plus tard |
| Supplicant WPA2-PSK | PBKDF2-SHA1 pour la clé, poignée de main en quatre temps (EAPOL), dérivation PTK/GTK, pose des clés dans le firmware ; **WPA3-SAE** plus tard si la box l'exige |
| Règlementation | domaine de régulation, canaux permis |

Références : **`iwx(4)` et `net80211` d'OpenBSD**, sous licence **ISC**, qui gèrent l'AX200/AX201 et contiennent une
pile 802.11 et un supplicant WPA2 complets. Le pilote `iwlwifi` de Linux (GPL) ne sert que de carte.

La crypto du supplicant (SHA-1, HMAC, AES key wrap) peut venir d'OpenSSL, déjà porté, si le supplicant est un service
en C ; ou être écrite en asm x86 (AES-NI, SHA-NI existent sur ce processeur). Comme le Wi-Fi vient tôt, le supplicant
en C impose d'avoir la chaîne `x86_64-korin-elf` et `libposix` **avant** le premier démarrage sur le ZBook (§11) ; en
asm, il n'attend que le noyau. Le reste de la crypto est petit : SHA-1, HMAC-SHA1, PBKDF2 et AES key wrap tiennent en
quelques centaines de lignes.

Note : `armpi-wifi-coupures` (mémoire du projet) soupçonnait déjà l'AX201 de ce PC dans les pertes d'accusés vues
depuis le Pi ; sans rapport direct, mais c'est la même puce.

### 7.4 3D : moyenne à dure, sur le GPU Intel

**Ce qu'on vise** : le GPU **Intel Gen12** intégré. La NVIDIA T1200 est laissée de côté : en mode hybride, l'écran
interne est branché sur l'Intel, et la NVIDIA ne servirait qu'à calculer pour lui (plus tard, si jamais).

**Côté Mesa : rien à écrire.** Mesa a déjà **iris** (OpenGL ES et OpenGL) et **anv** (Vulkan) pour Tiger Lake. C'est du
C, recompilé pour `x86_64-korin-elf`.

**Côté système : le même montage que sur le Pi.** Sur le Pi, un faux libdrm (`tools/mesa/libdrm`) imite les ioctls de
v3d et parle au service `v3d`. Sur PC, le faux libdrm imite l'interface **`i915`** (ou `xe`) et parle à un service
`i915` :

| ioctl | Rôle |
|---|---|
| `GETPARAM`, `QUERY` | identité de la puce, topologie des EU, capacités |
| `GEM_CREATE`, `GEM_MMAP_OFFSET`, `GEM_SET_DOMAIN`, `GEM_WAIT`, `GEM_CLOSE` | tampons dans la mémoire du système |
| `GEM_CONTEXT_CREATE`, `VM_CREATE` | contextes et espaces d'adresses du GPU (PPGTT) |
| `GEM_EXECBUFFER2` | soumission des lots de commandes |
| `SYNCOBJ_*` | synchronisation |

Le service doit : prendre la main sur le GPU (« forcewake »), monter la GGTT et les PPGTT, démarrer le moteur de rendu
et soumettre par **execlists** (le mode par défaut de Tiger Lake ; le firmware GuC n'est pas nécessaire), gérer les
interruptions de fin de lot. Intel publie la documentation de Tiger Lake (PRM, volumes du moteur de commandes, de la
mémoire et des registres).

**L'affichage reste au GOP.** On ne touche **pas** au moteur d'affichage de l'Intel (modes, ports eDP/HDMI, horloges,
firmware DMC) : c'est la plus grosse part d'`i915` et on peut s'en passer. L'UEFI a déjà allumé l'écran interne en
résolution native ; son framebuffer est dans la mémoire « volée » (stolen) du GPU, que le GPU peut viser par la GGTT.
Le rendu finit donc dans le framebuffer GOP, par une copie du GPU (blitter) ou directement. Prix : pas de changement
de résolution, pas de deuxième écran, pas de synchronisation verticale fiable au début.

Rétroéclairage : laissé allumé par l'UEFI ; son réglage passe par ACPI ou le registre PWM du GPU (plus tard).

Puissance : 32 EU Gen12, largement au-dessus du V3D du Pi 4 : Avalonia, Jet avec WebGL, SDL et les émulateurs en
profitent.

---

## 8. Ce qu'il faut en plus

| Besoin | Matériel | Difficulté | Remarque |
|---|---|---|---|
| Clavier interne | i8042 (PS/2) | facile | QEMU l'émule |
| Pavé tactile | **I2C-HID** sur l'I2C LPSS d'Intel | moyenne | trouvé par ACPI (`_HID` `PNP0C50`) ; le format HID est déjà lu pour l'USB |
| Extinction, batterie, capot, boutons | ACPI (AML) | moyenne | ACPICA (§4.3) |
| Stockage | clé USB (`usbstorage`) | facile | voir §10 pour le NVMe |
| Heure | horloge CMOS (RTC), puis NTP | facile | Korin est en UTC, la RTC du PC sous Windows est en heure locale |
| Économie d'énergie | P-states, C-states | plus tard | le processeur reste à sa fréquence de base au début |

---

## 9. Les programmes en assembleur hors des kits

Les commandes de `/bin` (185 000 lignes), les services (54 000), les programmes système (27 000) et les apps en asm
(99 000) sont aussi de l'ARM64. La règle des kits (même interface, logique réécrite) ne les couvre pas. Trois voies,
qui peuvent se combiner :

1. **Réécriture en asm x86**, programme par programme, en commençant par ce qui démarre le système (`init`, `vfs`,
   `fatfs`, `ramfs`, `netd`, Elegant, `bash` est déjà en C).
2. **Passage en C** pour ce qui doit exister sur les deux architectures sans doubler l'effort.
3. **Traducteur de source à source ARM64 → x86-64**, dans l'esprit de `tools/x86toa64.py` (qui a fait le chemin
   inverse) : la plupart des appels passent par des macros (`sys`, appels de kits), ce qui aide ; le manque de
   registres oblige à loger `x16`–`x28` en mémoire, le code est plus lent mais juste. Utile pour les centaines de
   petites commandes et les jeux, pas pour le noyau ni les kits.

Une règle peu coûteuse dès aujourd'hui : écrire les **nouveaux** services et outils en C plutôt qu'en asm ARM, quand
rien ne l'empêche, et ne plus mêler du code propre à AArch64 au code générique du noyau.

---

## 10. Sécurité de la machine

* **Ne jamais écrire sur le NVMe** : il porte Windows et les données du propriétaire. Korin démarre et vit sur une
  **clé USB**. Le pilote NVMe, s'il vient un jour, est d'abord en lecture seule. C'est la même règle que pour `SD1:`
  sur le Pi.
* **BitLocker** : si le chiffrement de Windows est actif, désactiver le Secure Boot ou démarrer autre chose peut faire
  demander la **clé de récupération** au démarrage suivant de Windows. Le propriétaire doit vérifier qu'il l'a avant
  de toucher au BIOS.
* Réglages du BIOS, à faire par le propriétaire : démarrage sur clé USB, **Secure Boot désactivé** (ou signature de
  `BOOTX64.EFI` plus tard), graphisme en mode **hybride** (écran interne sur l'Intel).
* Ne pas toucher au micrologiciel de l'EC ni aux variables UEFI autres que celles du démarrage.

---

## 11. Jalons proposés

| Jalon | Contenu | Où |
|---|---|---|
| 0 | séparation `arch/aarch64` dans le noyau actuel, sans changer de comportement ; règles de taille communes (`d32`, `d64`) | Pi, QEMU `raspi4b` |
| 1 | `BOOTX64.EFI`, noyau x86-64 qui affiche sur le GOP, IDT, pagination, LAPIC, clavier PS/2 | QEMU `q35` + OVMF |
| 2 | process ELF x86-64, appels système, SMP, ramfs, `vfs`, `fatfs` sur clé USB (xHCI + `usbstorage`) | QEMU |
| 3 | chaîne `x86_64-korin-elf`, newlib, `libposix`, `bash` ; ACPICA en service | QEMU |
| 4 | `netd` sur e1000e ; `usbnet` avec NCM et RNDIS | QEMU |
| 5 | premier démarrage sur le ZBook, **en mode texte** : clé USB, écran GOP, clavier, partage de connexion du téléphone | ZBook |
| 6 | **AX201** : firmware, commandes MVM, balayage et association sur un réseau ouvert, puis pile 802.11 et supplicant WPA2 | ZBook |
| 7 | kits réécrits (AppKit, UIKit d'abord) avec la suite de tests commune ; Elegant, bureau ; HDA + ALC285 | QEMU puis ZBook |
| 8 | service `i915` + faux libdrm, Mesa iris, puis Avalonia et Jet sur le GPU | ZBook |
| 9 | pavé tactile I2C-HID, batterie, extinction, puis le reste des programmes (§9) | ZBook |

Le Wi-Fi passe avant l'interface graphique : sans lui, le ZBook n'a pas de réseau, et tout le reste se met au point
plus vite avec telnet, FTP et le dépôt de paquets. Le jalon 5 n'a besoin d'aucun kit graphique.

Ordre de grandeur relatif, du plus petit au plus gros : USB < réseau USB < son < noyau x86 < 3D < Wi-Fi < kits <
programmes asm hors kits.

---

## 12. Décisions à prendre

1. **Convention d'appel des kits sur x86** : System V AMD64 (proposée) ou une convention propre.
2. **Génération** des en-têtes et souches depuis les `.abi` : oui ou non, et avant le portage.
3. **Programmes asm hors kits** (§9) : réécriture, passage en C, traducteur, ou un mélange (lequel pour quoi).
4. **Nouveaux programmes en C** dès maintenant quand c'est possible : oui ou non.
5. **Supplicant WPA2** : service en C avec OpenSSL (la chaîne C doit alors précéder le premier démarrage sur le
   ZBook), ou asm x86 (il n'attend que le noyau).
6. **Réseau de secours** : ajouter NCM et RNDIS à `usbnet` pour le partage de connexion d'un téléphone, oui ou non ;
   et quel téléphone (Android ou iPhone) est disponible.
7. **AudioKit** : réécrire les codecs (≈ 30 000 lignes) en asm x86, ou les compiler depuis des bibliothèques C sous
   la même interface.
8. **Nom de la carte** dans `build.ps1` (`pc`, `zbook`…) et place de l'étude dans `docs/FEUILLE-DE-ROUTE.md`.
