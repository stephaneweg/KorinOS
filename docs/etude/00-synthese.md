# ARMPI OS — Synthèse de l'étude de faisabilité

> Un OS de type MenuetOS pour **Raspberry Pi 4**, **100 % assembleur AArch64 (FASMARM)**,
> 64 bits, SMP, préemptif, auto-hébergé.
> Date de l'étude : 28/10/2026. Références : Circle (`ref/circle`), Onyx (`le depot d'Onyx`),
> fasm/FASMARM (`ref/fasm`, `tools/fasmarm`).

| Doc | Sujet |
|---|---|
| [01-coeur-systeme](01-coeur-systeme.md) | boot, EL2→EL1, MMU, exceptions, GIC-400, timer, SMP, ordonnanceur, mailbox, UART |
| [02-usb](02-usb.md) | PCIe + VL805 xHCI, hub, HID clavier/souris, mass storage |
| [03-reseau-materiel](03-reseau-materiel.md) | Ethernet GENET, WiFi CYW43455 (SDIO), firmware, WPA2 |
| [04-tcpip-gui](04-tcpip-gui.md) | pile TCP/IP, API socket, framebuffer, gestionnaire de fenêtres, modèle MenuetOS |
| [05-stockage-audio](05-stockage-audio.md) | EMMC2/SD, FAT32, exFAT, VFS, audio PWM/HDMI/I2S/USB, micro |
| [06-gpu-onyx](06-gpu-onyx.md) | ce qu'Onyx a validé : framebuffer, DMA, compositeur, **V3D direct** |
| [07-fasmarm-autohebergement](07-fasmarm-autohebergement.md) | porter FASMARM sous ARMPI |

## 1. Verdict

**Faisable**, à condition de découper en jalons qui tournent chacun sur matériel. Rien dans le
matériel n'exige du C : toutes les séquences d'init sont des écritures de registres et des
appels mailbox. Trois postes concentrent le risque : **USB** (PCIe + xHCI + hub obligatoire
avant le moindre clavier), **WiFi** (SDIO + firmware Broadcom + WPA2) et **FASMARM natif**
(57 000 lignes x86 à porter).

Le squelette `src/` démarre déjà dans QEMU raspi4b : EL2→EL1, vecteurs, UART, framebuffer
1024x768x32, fenêtre dessinée.

## 2. Exigences retenues

| Exigence | Décision |
|---|---|
| 64 bits | AArch64 exclusivement (noyau EL1h, apps EL0). Aucun mode AArch32. |
| SMP | 4 cœurs symétriques dès l'ordonnanceur v1 : file de prêts par cœur + vol de tâches, spinlocks `ldaxr/stlxr` (ou LSE absent sur A72 → LL/SC), IPI via GIC SGI (reschedule, TLB shootdown, arrêt). |
| Préemptif | Timer physique EL1 (IRQ PPI 30) **par cœur**, 1 kHz (CNTFRQ 54 MHz → 54 000). Noyau non préemptible au départ (comme Onyx), préemption au retour vers EL0. |
| Pagination par process | Pages 64 Ko ou 4 Ko (à trancher, cf. §4), TTBR0 par process + **ASID 16 bits**, noyau en TTBR1 (moitié haute), pages noyau globales → pas de vidage TLB au changement de process. |
| Multithread | Threads = entités ordonnancées, process = espace d'adressage + handles. Mutex/événements par futex. |
| **Contexte FP/NEON** | Trame de thread = x0-x30, SP_EL0, ELR, SPSR, TPIDR_EL0 **+ q0-q31, FPCR, FPSR (528 o)**. Voir §3. |
| GUI | Gestionnaire de fenêtres et compositeur dans le noyau (modèle MenuetOS), surfaces par fenêtre, rectangles sales, blits NEON, sortie par DMA 2D (recette Onyx). |
| USB | xHCI VL805 via PCIe, hub, HID (clavier, souris), mass storage, puis isochrone (audio). |
| Réseau | GENET d'abord (simple, polling), WiFi ensuite ; TCP/IP maison d'après les RFC. |
| Stockage | EMMC2 PIO → cache de blocs → **FAT32 puis exFAT** (transposition de FatFs, BSD) → VFS. |
| Audio | PWM (jack) d'abord, puis HDMI (IEC958), I2S, USB audio ; **entrée micro** : I2S (INMP441) puis USB (UAC capture). Mixeur NEON. |
| GPU | Palier 1 framebuffer + DMA ; palier 2 HVS/curseur (non validé) ; palier 3 V3D direct (recette Onyx). |
| Auto-hébergement | FASMARM **porté nativement** en AArch64 (traducteur source x86→A64 + validation bit à bit). Pas d'émulateur. |

## 3. Contexte FP/NEON et SMP

La sauvegarde paresseuse « classique » (on laisse l'état FP dans le cœur et on ne le sauve
qu'à la prochaine utilisation) devient piégeuse en SMP : l'état FP d'un thread peut rester
vivant sur un autre cœur que celui où il reprend. Choix retenu, simple et sûr :

1. Un thread démarre avec `CPACR_EL1.FPEN` qui piège EL0. À sa **première** instruction
   FP/SIMD (exception EC 0x07), le noyau positionne `TH_FP_USED` et lève le piège.
2. À chaque changement de contexte, si `TH_FP_USED`, on sauve et restaure **en entier**
   q0-q31 + FPCR + FPSR (`stp q0,q1,[x0],32` ×16, environ 100 cycles). Pas d'état FP
   « en vol » entre cœurs.
3. Le noyau n'utilise pas FP/SIMD, sauf dans des routines marquées (blit, mixeur, checksum)
   encadrées par `kfpu_begin`/`kfpu_end`, qui sauvent les registres q utilisés. La préemption
   est coupée pendant ce temps.
4. Les appels système ne touchent pas l'état FP. L'ABI déclare q0-q31 préservés par le noyau.

Onyx fait déjà la sauvegarde complète q0-q31/FPSR/FPCR à la préemption EL0 et l'a validée sur
matériel (voir 06, §autres éléments).

## 4. Décisions encore ouvertes

| Question | Options | Recommandation provisoire |
|---|---|---|
| Granule de pages | 4 Ko (fin, standard) / 64 Ko (Circle et Onyx, tables plus petites) | **4 Ko** pour les process (granularité de `brk`, piles, mmap de fichiers), blocs 2 Mo pour le noyau. Onyx prouve aussi que 64 Ko marche. |
| Licence | Circle GPLv3, pilotes réseau dérivés GPLv2, FatFs BSD, fasm licence propre | Si l'OS doit être libre de licence : **réimplémenter d'après la doc** (« clean room »), Circle servant de carte. Sinon : GPLv3 et transposition directe, plus rapide. **À trancher avant d'écrire les pilotes.** |
| WPA2 | supplicant interne de la puce / port de wpa_supplicant | interne (`sup_wpa`), à valider sur le firmware choisi. |
| Format des exécutables | binaire plat MenuetOS (en-tête `MENUET01`) / ELF64 | en-tête plat style MenuetOS pour les apps asm, chargeur ELF64 ensuite (utile à l'émulateur et aux outils). |

## 5. Architecture cible

```
 EL0   apps asm (en-tête plat ou ELF64)   |  émulateur i386 (FASMARM bootstrap)
       ---------------------- SVC #0, x8 = n° fonction, x0-x5 args ----------------------
 EL1   appels système (table)  |  WM + compositeur  |  VFS  |  sockets  |  son (mixeur)
       ordonnanceur SMP (files/cœur, IPI)  |  mémoire (pages phys., VM, ASID)  |  IRQ/GIC
       pilotes : UART  mailbox  FB/DMA  PCIe→xHCI→hub→HID/MSD  EMMC2  SDIO→WiFi  GENET
                 PWM/HDMI/I2S audio  V3D
 EL2   (quitté au boot)        firmware VideoCore : start4.elf (config.txt, DTB)
```

Cœurs : symétriques. Onyx dédie des cœurs (son sur le cœur 1, réseau sur le cœur 3) ; on peut
garder cette idée en option (affinité de threads noyau), sans la figer.

## 6. Feuille de route

Chaque jalon se termine par un test sur **Pi 4 réel** (et dans QEMU quand c'est possible :
QEMU raspi4b n'émule ni PCIe/USB, ni GENET, ni WiFi, ni V3D).

| # | Jalon | Contenu | Lignes asm (est.) |
|---|---|---|---|
| 0 | **Boot** ✅ | EL2→EL1, vecteurs, UART, framebuffer, dessin | 300 (fait) |
| 1 | **Noyau de base** ✅ | MMU (identité puis TTBR0/TTBR1), allocateur de pages, tas noyau, GIC, timer, police bitmap, console | 2 500 |
| 2 | **SMP + préemption** ✅ | réveil des cœurs 1-3 (spin table), files par cœur, threads noyau, trame complète FP, spinlocks, IPI | 1 500 |
| 3 | **Process EL0** ✅ | espaces d'adressage + ASID, appels système, chargeur d'apps, threads utilisateur, futex | 2 000 |
| 4 | **Stockage** ✅ | EMMC2 PIO, cache de blocs, FAT32 (lecture puis écriture), VFS, chargement d'apps depuis la SD | 6 000 |
| 5 | **USB** ✅ (DWC2 testé ; xHCI écrit, non testé) | PCIe, xHCI, hub, HID clavier/souris (polling → IRQ) | 2 800 |
| 6 | **GUI** ✅ | WM noyau, événements, appels système de dessin, curseur, compositeur NEON, apps démo | 4 000 |
| 7 | **Réseau** ✅ (USB-ECM testé ; GENET écrit, non testé) | GENET + ARP/IPv4/ICMP/UDP/DHCP/DNS/TCP + sockets | 6 800 |
| 8 | **Audio** ✅ (USB testé ; PWM/HDMI/I2S écrits, non testés) | PWM + DMA, mixeur, puis HDMI, I2S (sortie + **micro**) | 4 000 |
| 9 | **exFAT + clés USB** ✅ (MSD testé sur le port USB-C/DWC2) | exFAT complet, clé USB (Bulk-Only/SCSI) montée en /usb | 3 200 |
| 11 | **WiFi** (écrit, non testé) | SDIO, firmware CYW43455, scan/join, WPA2 par le supplicant de la puce | 1 900 |
| 12 | **GPU V3D** (écrit, non testé) | recette Onyx : CL binner/renderer, shaders QPU binaires, IRQ 74 | 3 000 |
| 13 | **USB audio** ✅ | isochrone xHCI, UAC1 lecture + capture micro | 1 500 |
| 14 | **Auto-hébergement** ✅ (identique octet pour octet) | FASMARM natif AArch64 (traducteur x86→A64, validation bit à bit) | outil + ~90 000 générées |

**Total écrit à la main (jalons 0-13) : environ 45 000 lignes**, du même ordre que MenuetOS64.
Le jalon 4 passe avant l'USB : sans clavier, on peut déjà charger et lancer des apps depuis la
SD, et tester le multitâche avec des démos graphiques.

## 7. Pièges déjà rencontrés (squelette)

- **FASMARM : tailles de données à la mode ARM.** `dw` = 32 bits, `dd` = 64 bits (`rw`/`rd`
  idem). Un `dd` dans un message mailbox double la taille du tampon sans erreur visible.
- Immédiats **sans `#`** ; `b.ne`, `movk x0,v,lsl 16`, `ldr x0,[label]` (littéral PC-relatif).
- Ordre des pixels : demander l'ordre **0** donne `0x00RRGGBB` sous QEMU (à confirmer sur Pi).
- La mailbox s'écrit après avoir testé le statut de la boîte 1 (`+0x38`), pas de la boîte 0.
- QEMU : `screendump` ne rafraîchit l'écran que si un afficheur est attaché (`-vnc`).
- À venir (d'après les études) : framebuffer en mémoire **Normal Non-Cacheable**, pas Device
  (écritures non alignées et `stp q` fautent en Device) ; DMA non cohérent avec le cache (zone
  DMA non cachée sous 1 Go) ; tag `NOTIFY_XHCI_RESET` obligatoire avant l'USB ; xHCI
  au-delà de 4 Go (`0x6_0000_0000`) ; timer à activer **sur chaque cœur**.

## 8. Outillage installé

| Outil | Emplacement |
|---|---|
| FASMARM 1.44 | `tools/fasmarm/fasmarm.exe` |
| QEMU (raspi4b) | `C:\Program Files\qemu` |
| Firmware Pi 4 | `firmware/` (start4.elf, fixup4.dat, DTB) |
| Circle | `ref/circle` |
| Sources fasm 1.73 | `ref/fasm` |
| Build | `build.ps1` (`run` = QEMU, `sd E:` = copie sur carte SD) |
