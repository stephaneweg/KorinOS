# 05 — Stockage, systèmes de fichiers et audio (transposition Circle → asm AArch64 FASMARM)

Cible : Raspberry Pi 4 (BCM2711, Cortex-A72, AArch64), OS mono-espace écrit en FASMARM, style MenuetOS
(procédural, appels système numérotés, pas de POO). Référence : `ref/circle` (Circle, GPLv3).

> **Licence** — Circle est sous GPLv3 : une transposition ligne à ligne en asm est une œuvre dérivée
> (l'OS hériterait de la GPLv3). FatFs (ChaN) est sous licence BSD 1 clause (« FatFs license »), très
> permissive. Les registres matériels, eux, ne sont pas protégés : réécrire d'après les datasheets
> (SD Physical Layer Simplified Spec, SDHCI 3.0, BCM2711 ARM Peripherals) + la lecture de Circle comme
> « documentation vivante » est la voie propre.

Toutes les adresses ARM ci-dessous supposent `ARM_IO_BASE = 0xFE000000` (mode « low peripheral », par défaut
sur RPi4 ; `include/circle/bcm2835.h:30`). Adresse « bus » (vue DMA/VPU) d'un périphérique :
`(addr & 0xFFFFFF) + 0x7E000000` ; adresse bus d'une RAM : `(addr & ~0xC0000000) | 0xC0000000`
(`BUS_ADDRESS`, `bcm2835.h:57`, `GPU_MEM_BASE = 0xC0000000` non caché L2).

---

## Sommaire

- [A1. EMMC2 (SDHCI) — carte SD](#a1-emmc2-sdhci--carte-sd)
- [A2. Partitions MBR / GPT](#a2-partitions-mbr--gpt)
- [A3. FAT32 (lib/fs/fat et FatFs)](#a3-fat32)
- [A4. exFAT](#a4-exfat)
- [A5. Choix : FatFs vs lib/fs/fat, VFS, appels système, cache](#a5-choix-de-portage-vfs-appels-système-cache)
- [A6. USB Mass Storage (second bloc-device)](#a6-usb-mass-storage)
- [B1. Couche commune CSoundBaseDevice + DMA sons](#b1-couche-commune-soundbasedevice-et-dmasoundbuffers)
- [B2. PWM (jack 3,5 mm)](#b2-pwm-audio-jack-35-mm)
- [B3. HDMI (MAI + IEC958)](#b3-hdmi-audio-mai--iec958)
- [B4. I2S / PCM](#b4-i2s-pcm)
- [B5. USB Audio (UAC1/UAC2)](#b5-usb-audio-uac1uac2)
- [B6. Entrée micro](#b6-entrée-micro)
- [B7. Mixeur logiciel](#b7-mixeur-logiciel-absent-de-circle--à-concevoir)
- [C. Plan de transposition global, priorités, tailles](#c-plan-de-transposition-global)
- [D. Pseudo-asm](#d-pseudo-asm-aarch64)

---

## Volumétrie Circle (wc -l)

| Fichier | Lignes | Rôle |
|---|---:|---|
| `addon/SDCard/emmc.cpp` / `.h` | 2659 / 197 | Pilote SD (SDHCI Arasan/EMMC2), PIO |
| `addon/SDCard/sdhost.cpp` | 1583 | Contrôleur SDHOST (RPi1-3 uniquement, `USE_SDHOST`) — **ignorer** |
| `addon/SDCard/mmchost.cpp` | 200 | Glue pour sdhost — ignorer |
| `lib/fs/partitionmanager.cpp` + `partition.cpp` | 138 + 99 | MBR primaire seul |
| `lib/fs/fat/*.cpp` (fat, fatcache, fatdir, fatfs, fatinfo) | 237+367+553+479+375 = 2011 | FAT16/32 natif, minimal |
| `addon/fatfs/ff.c` / `ff.h` / `ffconf.h` | 7255 / 448 / 313 | FatFs R0.16 |
| `addon/fatfs/ffunicode.c` | 10908 | Tables de pages de code (≈98 % de données CJK) |
| `addon/fatfs/diskio.cpp` / `ffsystem.cpp` | 314 / 200 | Glue bloc-device / mutex |
| `lib/usb/usbmassdevice.cpp` | 734 | USB BOT + SCSI |
| `lib/sound/soundbasedevice.cpp` / `.h` | 1096 / 313 | File, conversions, IEC958 |
| `lib/sound/dmasoundbuffers.cpp` | 423 | Double tampon DMA + IRQ |
| `lib/sound/pwmsoundbasedevice.cpp` | 257 | PWM audio |
| `lib/sound/hdmisoundbasedevice.cpp` | 794 | HDMI audio (MAI) |
| `lib/sound/i2ssoundbasedevice.cpp` | 483 | I2S (PCM) TX/RX |
| `lib/sound/usbsoundbasedevice.cpp` | 631 | Pont son ↔ USB audio |
| `lib/usb/usbaudiostreaming.cpp` / `usbaudiocontrol.cpp` / `usbaudiofunctopology.cpp` | 1119 / 225 / 589 | UAC1/UAC2 |
| `lib/sound/usbsoundcontroller.cpp` | 455 | Volume/mute/jack USB |
| `lib/sound/pcm512xsoundcontroller.cpp`, `wm8960soundcontroller.cpp` | 223, 339 | Codecs I2C de HAT |
| `lib/dmachannel.cpp` / `dma4channel.cpp` | 566 / 572 | DMA générique / DMA4 (40 bits) |
| `lib/gpioclock.cpp` | 171 | Clock Manager (PWM/PCM/GPCLK) |
| `lib/usb/xhci*.cpp` (tous) | 2952 | Hôte xHCI (VL805), y compris TRB isochrones |
| `addon/vc4/sound/vchiq*.cpp` | 728 | Audio via VCHIQ (firmware) — alternative, à éviter |

---

# PARTIE A — STOCKAGE

## A1. EMMC2 (SDHCI) — carte SD

### Contexte RPi4
- Le slot micro-SD du RPi4 est câblé sur **EMMC2** = `ARM_EMMC2_BASE = ARM_IO_BASE + 0x340000 = 0xFE340000`
  (`include/circle/bcm2711.h:31`). L'ancien Arasan (`0xFE300000`) reste disponible pour SDIO/Wi-Fi ou une
  carte « externe » sur GPIO22-27 (`TDeviceSelector SDCardExternal`, `emmc.cpp:540-544`).
- Circle n'utilise **ni DMA ni IRQ** pour les transferts : tout est **PIO + polling** sur `EMMC_INTERRUPT`
  (`SD_CARD_INTERRUPTS` commenté, `emmc.cpp:92`). IRQ disponible : `ARM_IRQ_ARASANSDIO = GIC_SPI(126)` → INTID 158
  (`bcm2711int.h:60`) — uniquement utile pour insertion/retrait.
- Pas de configuration GPIO nécessaire pour EMMC2 (pins dédiées) ; le constructeur ne touche aux GPIO que pour
  l'Arasan (`emmc.cpp:562-600`).

### Carte des registres (offset depuis 0xFE340000, `emmc.cpp:98-125`)

| Off | Nom | Usage |
|---|---|---|
| 0x00 | ARG2 (= SDMA sys addr en SDHCI) | non utilisé |
| 0x04 | BLKSIZECNT | `[11:0]` taille bloc (512), `[31:16]` nb blocs (≤ 65535) |
| 0x08 | ARG1 | argument de commande |
| 0x0C | CMDTM | `[29:24]` index, `[23:22]` type (3=abort), `[21]` data, `[20]` idx chk, `[19]` CRC chk, `[17:16]` réponse (0 none,1 136,2 48,3 48busy), `[5]` multi, `[4]` dir (1=carte→hôte), `[3:2]` auto-CMD (1=CMD12), `[1]` blkcnt en, `[0]` DMA |
| 0x10-0x1C | RESP0..3 | réponse (R2 : 128 bits sans CRC, décalée de 8 bits) |
| 0x20 | DATA | FIFO PIO 32 bits |
| 0x24 | STATUS | `[0]` CMD inhibit, `[1]` DAT inhibit, `[16]` carte insérée, `[23:20]` DAT3..0 |
| 0x28 | CONTROL0 (Host ctrl + Power ctrl) | `[1]` bus 4 bits, `[5]` 8 bits, `[8]` bus power, `[11:9]` tension (111=3,3 V), `[3:4]` sélection DMA (SDHCI) |
| 0x2C | CONTROL1 (Clock ctrl + Timeout + SW reset) | `[0]` clk interne en, `[1]` clk stable, `[2]` SD clk en, `[7:6]` div hi, `[15:8]` div lo, `[19:16]` timeout data, `[24]` SRST_HC, `[25]` SRST_CMD, `[26]` SRST_DATA |
| 0x30 | INTERRUPT | W1C. `[0]` CMD done, `[1]` DATA done, `[4]` write ready, `[5]` read ready, `[8]` card, `[15]` ERR (résumé), `[16..]` erreurs (CMD timeout/CRC/endbit/index, DATA timeout/CRC/endbit, current limit, ACMD12, ADMA) |
| 0x34 | IRPT_MASK | sélection des bits qui *apparaissent* dans INTERRUPT |
| 0x38 | IRPT_EN | bits qui lèvent l'IRQ ARM |
| 0x3C | CONTROL2 | à 0 (UHS non utilisé) |
| 0x40/0x44 | CAPABILITIES_0/1 | |
| 0xFC | SLOTISR_VER | `[23:16]` version SDHCI (`m_hci_ver`) |

### Séquence d'init (Circle, `Initialize` → `CardInit` → `CardReset`)
1. **Tension I/O 3,3 V** (spécifique RPi4, `emmc.cpp:622-633`) : mailbox tag `0x00038041 SET_GPIO_STATE`,
   GPIO d'expandeur `128+4 = 132`, état 0 → coupe le régulateur 1,8 V du bus SD. Attendre 5 ms.
2. **PowerOn** (`emmc.cpp:762`) : tag `SET_POWER_STATE (0x00028001)`, device 0 (SD), état `ON|WAIT` (3).
3. **Reset contrôleur** : `CONTROL1 |= 1<<24`, clocks off (bits 0 et 2), attendre que `CONTROL1 & (7<<24) == 0` (1 s max).
4. **Power bus SDHCI** (RPi4 seulement, `emmc.cpp:1531-1536`) : `CONTROL0 |= 0xF << 8` (power + 3,3 V), 2 ms.
5. Attendre `STATUS[16]` (carte présente, 500 ms).
6. `CONTROL2 = 0`. **Horloge de base** : mailbox `GET_CLOCK_RATE (0x00030002)` id **12** (`CLOCK_ID_EMMC2`) ;
   par défaut 100 MHz si 0.
7. **Horloge d'identification 400 kHz** : diviseur SDHCI 10 bits (Circle n'emploie que des puissances de 2,
   `GetClockDivider`, `emmc.cpp:809`) : `div = 2^(ceil(log2(base/f))-1)` ; `CONTROL1 = (CONTROL1 & ~(0x3FF<<6)) | (div&0xFF)<<8 | ((div>>8)&3)<<6 | 1`,
   timeout data `11<<16` ; attendre bit1 (stable) ; puis `|= 4` (SD clock on), 2 ms.
8. `IRPT_EN = 0` (pas d'IRQ), `INTERRUPT = 0xFFFFFFFF`, `IRPT_MASK = 0xFFFFFFFF & ~0x100`.
9. **CMD0** (GO_IDLE, pas de réponse).
10. **CMD8** arg `0x1AA` (R7). Timeout ⇒ carte v1 (*normal*) : faire `ResetCmd` (`CONTROL1 |= 1<<25`, attendre 0) et
    effacer `INTERRUPT` bit16 ; sinon vérifier `(r0 & 0xFFF) == 0x1AA` ⇒ v2+.
11. **CMD5** (sonde SDIO) : timeout attendu pour une SD ; idem ResetCmd (`emmc.cpp:1738`).
12. **ACMD41** = CMD55 (arg `RCA<<16`, ici 0) + CMD41. D'abord arg 0 (inquiry OCR), puis boucle avec
    `0x00FF8000 | (v2 ? 1<<30 : 0)` (HCS) jusqu'à `r0 bit31 = 1` (fin de busy). `SDHC = r0 bit30`.
    Circle attend **500 ms** entre essais (`emmc.cpp:1839`) — inutilement lent, 10 ms suffisent (timeout total 1 s).
13. Passer à **25 MHz** (`SwitchClockRate` : attendre `STATUS & 3 == 0`, SD clock off, nouveau diviseur, on ; 2 ms entre étapes).
14. (option 1,8 V/UHS — désactivée : `SD_1_8V_SUPPORT` commenté. **Ne pas implémenter** au début.)
15. **CMD2** (ALL_SEND_CID, R2) → CID. **CMD3** (SEND_RELATIVE_ADDR, R6) → `RCA = r0>>16`.
16. **CMD9** (SEND_CSD, R2, arg RCA<<16) → capacité (CSD v2 : `C_SIZE` bits 69:48 → `(C_SIZE+1)*512 KiB`). Attention :
    les réponses R2 dans RESP0..3 sont **décalées de 8 bits** (CRC retiré) — `GetCSDField` gère ce décalage.
17. **CMD7** (SELECT_CARD, R1b, arg RCA<<16) → état *transfer*.
18. Si SDSC : **CMD16** SET_BLOCKLEN 512. `BLKSIZECNT[11:0] = 0x200`.
19. **ACMD51** (SEND_SCR, 8 octets : bloc 8 × 1) → `sd_bus_widths`, version. Big-endian ! Restaurer 512 ensuite.
20. Si SD ≥ 1.10 : **CMD6** mode 0 `0x00FFFFF0` (64 octets) → bit HS ; si HS : CMD6 `0x80FFFFF1` puis **50 MHz**
    (`SD_HIGH_SPEED` désactivé par défaut dans Circle → reste 25 MHz).
21. Si 4 bits supportés : masquer bit8 de IRPT_MASK, **ACMD6** arg 2, `CONTROL0 |= 2`, rétablir masque.
22. `INTERRUPT = 0xFFFFFFFF`. La carte est prête.

### Lecture/écriture (`DoRead`/`DoWrite` → `EnsureDataMode` → `DoDataCommand` → `IssueCommandInt`)
- `EnsureDataMode` (`emmc.cpp:2384`) : CMD13 (SEND_STATUS) → `état = (r0>>9)&0xF` ; 3 (stby) ⇒ CMD7 ; 5 (data) ⇒ CMD12 + ResetDat ;
  autre ≠ 4 ⇒ réinit complète. Optimisation possible : ne faire CMD13 qu'après une erreur.
- Adresse : **SDHC/SDXC = n° de bloc**, **SDSC = octets** (`block*512`).
- Commande : CMD17/CMD24 (1 bloc), **CMD18/CMD25** multi-blocs avec `BLKCNT_EN | AUTO_CMD12 | MULTI` (table `sd_commands`, `emmc.cpp:290`).
- Émission (`emmc.cpp:969-1160`, version non-SDHOST, mode polling) :
  1. `BLKSIZECNT = 512 | n<<16` ; `ARG1 = lba` ; `CMDTM = cmd_reg`.
  2. Attendre `INTERRUPT & 0x8001` ; lire ; écrire `0xFFFF0001` (W1C) ; erreur si `(irpts & 0xFFFF0001) != 1`.
  3. Lire RESP0 (ou RESP0..3).
  4. Pour chaque bloc : attendre `INTERRUPT & (0x8000 | 0x20)` (lecture) ou `0x10` (écriture), W1C, puis 128 × `ldr/str w` sur DATA.
  5. Si data ou R1b : attendre `INTERRUPT & 0x8002` (transfer complete), W1C `0xFFFF0002`. Accepter `0x100002`
     (data timeout + transfer complete simultanés : TC prioritaire, HCSS 2.2.17).
- 3 tentatives, puis RCA invalidée ⇒ réinit à la prochaine I/O.
- Tampon **aligné sur 4**.

### DMA (non implémenté dans Circle) — options
- **SDMA** : adresse dans 0x00, interruption DMA à chaque frontière (512 KiB par défaut) — simple.
- **ADMA2** (SDHCI 3.0, reg `0x58` ADMA_SYS_ADDR, `CONTROL0[4:3] = 10b`) : table de descripteurs 64 bits
  `{attr:16 (Valid=1, End=2, Int=4, Act=0b10<<4 = tran), len:16, addr:32}`. Erreur ADMA = bit 25 de INTERRUPT.
- Contraintes BCM2711 : l'EMMC2 n'adresse pas toute la RAM (dma-ranges Linux : fenêtre basse ; rester **sous 1 Go**,
  adresse bus directe sans 0xC0000000 pour EMMC2 — à valider sur matériel). Cohérence : *clean* D-cache avant écriture,
  *invalidate* après lecture.
- Gain : PIO ≈ 20-25 Mo/s à 50 MHz 4 bits avec un A72 qui ne fait rien d'autre ; ADMA libère le CPU. **Priorité basse** ;
  commencer en PIO.

### Pièges EMMC2
1. Oublier `CONTROL0 |= 0xF00` (bus power) ⇒ CMD0 sans effet (spécifique RPi4/SDHCI standard).
2. Ne pas couper le 1,8 V (GPIO expandeur 4) après un boot qui a laissé l'I/O en 1,8 V ⇒ timeouts aléatoires.
3. Timeouts CMD8/CMD5 *normaux* : sans `ResetCmd` + W1C du bit 16, toutes les commandes suivantes échouent.
4. W1C : écrire la valeur lue dans INTERRUPT efface aussi des bits non traités ; Circle écrit des masques ciblés.
5. Réponses R2 décalées de 8 bits ; SCR et statut CMD6 big-endian.
6. SDSC = adressage octet.
7. Bloc-size 8/64 pour ACMD51/CMD6, à restaurer à 512.
8. `BLKSIZECNT` limite à 65535 blocs par commande.
9. Délai entre changements de clock (2 ms) et `STATUS & 3` nul avant de toucher la clock.

### Estimation asm
Init + R/W PIO (sans 1,8 V/UHS/eMMC/SDIO) : **≈ 900-1200 lignes FASMARM**, ~4-5 Ko de code. ADMA2 : +200 lignes.

---

## A2. Partitions MBR / GPT

- **Circle natif** (`lib/fs/partitionmanager.cpp`) : lit le secteur 0, vérifie `0xAA55` à 510, 4 entrées primaires à 0x1BE,
  ignore type 0 / 0x05 / 0x0F (étendues) / 0xEF (EFI) ; pas de GPT. Expose `emmc1-1..4`.
- **FatFs** : MBR primaire ; GPT si `FF_LBA64 = 1` (Circle : 0, `ffconf.h:218`) ; `FF_MIN_GPT = 0x10000000` secteurs.

Entrée MBR (16 o) : `+0 statut, +4 type, +8 LBA début (u32), +12 nb secteurs (u32)`. Types utiles :
`0x0B/0x0C` FAT32 (CHS/LBA), `0x0E` FAT16 LBA, `0x07` exFAT/NTFS, `0xEE` GPT protecteur.

GPT : LBA1 = en-tête `"EFI PART"` ; `+72` LBA table, `+80` nb entrées, `+84` taille entrée (128) ; entrée :
`+0` GUID type (16), `+16` GUID unique, `+32` premier LBA (u64), `+40` dernier LBA. GUID « Microsoft basic data »
`EBD0A0A2-B9E5-4433-87C0-68B6B72699C7` (stocké en mixed-endian). CRC32 d'en-tête/table : vérification facultative au début.

Le firmware RPi4 boote sur MBR+FAT32 (GPT supporté par l'EEPROM récente). **Priorité : MBR seul**, GPT en option (~150 lignes).

---

## A3. FAT32

### Structures (`include/circle/fs/fat/fatfsdef.h`, `addon/fatfs/ff.c:150-260` macros `BPB_*`/`BS_*`/`DIR_*`)

**Boot sector / BPB** (offsets) :

| Off | Taille | Champ | Remarque |
|---|---|---|---|
| 0x0B | 2 | BytsPerSec | 512 (FatFs Circle : `FF_MAX_SS = 512`) |
| 0x0D | 1 | SecPerClus | puissance de 2, 1..128 |
| 0x0E | 2 | RsvdSecCnt | ≥ 1 (souvent 32 en FAT32) |
| 0x10 | 1 | NumFATs | 2 |
| 0x11 | 2 | RootEntCnt | 0 en FAT32 |
| 0x13 | 2 | TotSec16 | 0 → utiliser TotSec32 |
| 0x16 | 2 | FATSz16 | 0 en FAT32 |
| 0x20 | 4 | TotSec32 | |
| 0x24 | 4 | FATSz32 | |
| 0x28 | 2 | ExtFlags | bit7 = miroir off, bits 3:0 FAT active |
| 0x2C | 4 | RootClus | souvent 2 |
| 0x30 | 2 | FSInfo | souvent 1 |
| 0x1FE | 2 | 0xAA55 | |

**FSInfo** : `0x41615252` @0, `0x61417272` @484, FreeCount @488, NextFree @492, `0xAA550000` @508. Indice seulement
(peut être faux : FatFs l'ignore si `FF_FS_NOFSINFO` bit 0).

**Dérivées** :
```
FATStart   = PartLBA + RsvdSecCnt
DataStart  = FATStart + NumFATs * FATSz32
nClusters  = (TotSec - (DataStart-PartLBA)) / SecPerClus
Type       : nClusters < 4085 → FAT12 ; < 65525 → FAT16 ; sinon FAT32  (règle officielle : par compte de clusters, jamais par la chaîne "FAT32   ")
Sector(c)  = DataStart + (c - 2) * SecPerClus
FATEntry(c): secteur FATStart + (c*4)/512, offset (c*4)%512, valeur & 0x0FFFFFFF
             0 libre ; 0x0FFFFFF7 mauvais ; >= 0x0FFFFFF8 fin de chaîne (EOC)
             En écriture, préserver les 4 bits hauts et écrire dans toutes les FAT (miroir).
```

**Entrée de répertoire 8.3 (32 octets)** : `Name[11]` (0x00 = fin, 0xE5 = libre, 0x05 = 0xE5 réel), `Attr`@11
(RO 1, HID 2, SYS 4, VOL 8, DIR 0x10, ARC 0x20, **LFN = 0x0F**), `NTRes`@12 (bits 3/4 : base/extension en minuscule),
`CrtTimeTenth`@13, `CrtTime`@14, `CrtDate`@16, `LstAccDate`@18, **`FstClusHI`@20**, `WrtTime`@22, `WrtDate`@24,
**`FstClusLO`@26**, **`FileSize`@28**. Date : `YYYYYYYMMMMDDDDD` (1980+), heure : `HHHHHMMMMMMSSSSS` (sec/2).

**Entrée LFN (32 o)** précédant l'entrée 8.3, en ordre inverse : `Ord`@0 (n° 1..20, `|0x40` sur la dernière physique = première lue),
`Name1`@1 (5 × UCS-2), `Attr=0x0F`@11, `Type=0`@12, **`Chksum`@13**, `Name2`@14 (6 × UCS-2), `FstClusLO=0`@26, `Name3`@28 (2 × UCS-2).
13 caractères par entrée, terminé par 0x0000 puis bourrage 0xFFFF. Somme de contrôle du nom court :
```
sum = 0 ; for i in 0..10 : sum = ((sum & 1) << 7) + (sum >> 1) + Name[i]   (8 bits)
```
(FatFs : `sum_sfn`, `ff.c` ; comparaison insensible à la casse via `ff_wtoupper`). Création LFN : générer un nom court unique
`NOM~N.EXT` (FatFs `gen_numname`, hachage CRC à partir de ~5 collisions).

### Implémentation native Circle (`lib/fs/fat`)
- `CFATInfo` (375 l.) : parse BPB, FAT16/FAT32 seulement (pas FAT12), FSInfo.
- `CFATCache` (367 l.) : **100 tampons** de 512 o (`FAT_BUFFERS`, `fatfsdef.h:12`) en liste LRU chaînée, write-back, `Flush`.
- `CFAT` (237 l.) : `GetClusterEntry`, `SetClusterEntry` (toutes FATs), `AllocateCluster` (recherche linéaire), `FreeClusterChain`.
- `CFATDirectory` (553 l.) : **répertoire racine uniquement**, **noms 8.3 uniquement** (`Name2FAT`), pas de LFN, pas de sous-répertoires.
- `CFATFileSystem` (479 l.) : `FileOpen/Create/Close/Read/Write`, `RootFindFirst/Next`, handles fixes.
→ Trop limité pour un OS de bureau (pas de dossiers, pas de noms longs, pas d'exFAT). Utile comme **modèle de cache**.

### FatFs (`addon/fatfs`, R0.16)
- Configuration Circle (`ffconf.h`) : R/W, `FF_USE_LFN 3` (tampon LFN sur le tas), `FF_MAX_LFN 255`, `FF_CODE_PAGE 850`,
  `FF_FS_RPATH 2`, `FF_VOLUMES 7` nommés `"SD","SD2","USB","USB2","USB3","FD","NVME"`, `FF_FS_REENTRANT 1`,
  **`FF_FS_EXFAT 0`**, `FF_LBA64 0`, `FF_FS_TINY 0` (tampon de 512 o par fichier).
- `diskio.cpp` mappe les volumes vers `"emmc1"`, `"emmc2"`, `"umsd1..3"`, `"nvme1"` via `CDeviceNameService` (`Seek`+`Read`/`Write`).
- Coeur : `find_volume`/`mount_volume` → `check_fs` ; `get_fat`/`put_fat` ; `create_chain`/`remove_chain` ;
  `clst2sect` ; `dir_sdi`/`dir_next`/`dir_alloc`/`dir_find`/`dir_read`/`dir_register`/`dir_remove` ;
  `create_name` (analyse de chemin, LFN, SFN) ; `follow_path` ; API `f_open/f_read/f_write/f_lseek/f_close/f_opendir/f_readdir/f_mkdir/f_unlink/f_rename/f_stat/f_getfree/f_truncate/f_sync` (`ff.c:3744-5390`).
- Fenêtre secteur unique (`fs->win[512]`, `move_window`/`sync_window`) partagée par FAT + répertoires : simple mais beaucoup d'I/O ;
  un cache de blocs sous FatFs (cf. A5) compense.

---

## A4. exFAT

Activable dans FatFs par `FF_FS_EXFAT 1` (exige `FF_USE_LFN ≥ 1` ; `FF_LBA64 1` pour > 2 Tio). Le code exFAT de `ff.c`
représente **≈ 1190 lignes** sous `#if FF_FS_EXFAT` (comptage awk ; 40+ blocs). Cartes SDXC (> 32 Go) sont livrées formatées exFAT.

### Boot sector (secteur 0 de la partition ; région principale = 12 secteurs, sauvegarde aux secteurs 12-23)

| Off | Taille | Champ |
|---|---|---|
| 0 | 3 | `EB 76 90` |
| 3 | 8 | `"EXFAT   "` |
| 11 | 53 | à zéro (MustBeZero — distingue de FAT) |
| 64 | 8 | PartitionOffset |
| 72 | 8 | VolumeLength (secteurs) |
| 80 | 4 | FatOffset (secteurs) |
| 84 | 4 | FatLength |
| 88 | 4 | ClusterHeapOffset |
| 92 | 4 | ClusterCount |
| 96 | 4 | FirstClusterOfRootDirectory |
| 100 | 4 | VolumeSerialNumber |
| 104 | 2 | FileSystemRevision (0x0100) |
| 106 | 2 | VolumeFlags (bit0 ActiveFat, bit1 VolumeDirty, bit2 MediaFailure) |
| 108 | 1 | BytesPerSectorShift (9..12) |
| 109 | 1 | SectorsPerClusterShift (cluster ≤ 32 Mio) |
| 110 | 1 | NumberOfFats (1, 2 = TexFAT) |
| 112 | 1 | PercentInUse |
| 510 | 2 | 0xAA55 |

Secteur 11 = checksum (u32 répété) des secteurs 0-10 en sautant les octets 106,107,112 :
`sum = ROR32(sum,1) + byte`. FatFs le vérifie au montage (`ff.c` ~`check_fs`/`mount_volume`).
`Sector(c) = PartLBA + ClusterHeapOffset + (c-2) << SectorsPerClusterShift`.

### FAT et bitmap
- La FAT (u32 par cluster, `0xFFFFFFFF` EOC, `0xFFFFFFF7` mauvais) **n'est valable que pour les chaînes fragmentées**.
- **Bitmap d'allocation** (fichier système, entrée 0x81) : 1 bit par cluster à partir du cluster 2, **seule source de vérité** pour l'occupation.
  FatFs : `find_bitmap`, `change_bitmap` (`ff.c` bloc `#if FF_FS_EXFAT` ~l. 1292-1470).
- **NoFatChain** (bit1 des GeneralSecondaryFlags de l'entrée Stream) : le fichier est contigu, la FAT n'est **pas** lue ni écrite pour lui ;
  cluster suivant = c+1. Si un fichier contigu se fragmente en écriture, FatFs « matérialise » la chaîne dans la FAT (`fill_last_frag`/`fill_first_frag`).

### Entrées de répertoire (32 o, `EntryType` bit7 = InUse, ensembles d'entrées)

| Type | Nom | Champs clés |
|---|---|---|
| 0x81 | Allocation Bitmap | `+1` flags, `+20` FirstCluster, `+24` DataLength (u64) |
| 0x82 | Up-case Table | `+4` TableChecksum, `+20` FirstCluster, `+24` DataLength |
| 0x83 | Volume Label | `+1` nb car., `+2` 11 × UTF-16 |
| 0x85 | **File** (primaire) | `+1` SecondaryCount (≥2), `+2` **SetChecksum** (u16), `+4` FileAttributes (u16, mêmes bits que FAT), `+8` Create TS, `+12` Modify TS, `+16` Access TS, `+20/21` 10 ms, `+22..24` UTC offsets |
| 0xC0 | **Stream Extension** | `+1` GeneralSecondaryFlags (bit0 AllocationPossible, **bit1 NoFatChain**), `+3` NameLength, `+4` **NameHash** (u16), `+8` ValidDataLength (u64), `+20` FirstCluster, `+24` DataLength (u64) |
| 0xC1 | **File Name** | `+2` 15 × UTF-16 (autant d'entrées que `ceil(NameLength/15)`) |
| 0xA0/0xA1/0xA2 | GUID, padding TexFAT, ACL | ignorer |
| 0x00 | fin de répertoire | |
| 0x05/0x40/0x41 | entrées supprimées (bit7=0) | réutilisables |

- **SetChecksum** : u16, `sum = ROR16(sum,1) + byte` sur toutes les entrées de l'ensemble en sautant les octets 2-3 de la première (FatFs `xdir_sum`).
- **NameHash** : même fonction sur les octets du nom **mis en majuscules via la table up-case** (FatFs `xname_sum`).
- **ValidDataLength ≤ DataLength** : au-delà de VDL, lire des zéros (FatFs gère dans `f_read`).
- **Table up-case** : 128 Kio non compressée ou compressée (`0xFFFF, n` = n caractères identité). FatFs **n'utilise pas** la table du volume :
  il utilise `ff_wtoupper` (`ffunicode.c`, table compacte ~1 Ko) — compatible pour l'immense majorité des noms.
- Pas de nom court, pas de « . » / « .. » dans les répertoires exFAT ; la racine n'a pas d'entrée Stream (taille = chaîne FAT).
- Horodatage : format DOS 32 bits + 10 ms + offset UTC.

### Coût exFAT
≈ 1200 lignes C → **≈ 2500-3500 lignes asm**. Recommandation : **phase 2**, après FAT32 R/W stable. Lecture seule exFAT d'abord (~1200 lignes asm) : bitmap non nécessaire en lecture.

---

## A5. Choix de portage, VFS, appels système, cache

### FatFs vs lib/fs/fat

| Critère | lib/fs/fat (Circle) | FatFs (ChaN) |
|---|---|---|
| Lignes C utiles | ~2000 C++ | ~5000 (ff.c sans commentaires) + ~1200 exFAT |
| Sous-répertoires | non | oui |
| LFN | non | oui (+ table OEM ↔ Unicode) |
| exFAT / GPT | non | oui (options) |
| Robustesse | faible, peu testé | énorme base installée |
| Licence | GPLv3 | BSD-1 |
| Style | POO (5 classes) | **C procédural, structures plates (FATFS, FIL, DIR), fonctions statiques** → transposition asm quasi mécanique |

**Recommandation : transposer FatFs**, configuration réduite :
- `FF_FS_READONLY 0`, `FF_FS_MINIMIZE 0`, `FF_USE_LFN 1` (tampon statique, mono-thread noyau) puis 3,
  `FF_CODE_PAGE 437` ou **UTF-8 natif via `FF_LFN_UNICODE 2`** (ne garder de `ffunicode.c` que `ff_wtoupper` et une page SBCS
  — éviter les 10 000 lignes CJK), `FF_FS_RPATH 0` (le VFS gère le chemin courant), `FF_USE_FIND 0`, `FF_USE_MKFS 0`, `FF_USE_FASTSEEK 0`,
  `FF_FS_REENTRANT 0` (sérialiser au niveau syscall, cf. MenuetOS : un seul verrou FS), `FF_FS_TINY 1` (une seule fenêtre de 512 o par volume,
  le cache de blocs fait le reste), `FF_FS_EXFAT 0` puis 1.
- Fonctions à porter en priorité : `ld_word/ld_dword/st_*` (→ `ldrh/ldr` non alignés, AArch64 les tolère en mémoire normale),
  `move_window`, `sync_window`, `clst2sect`, `get_fat`, `put_fat`, `create_chain`, `remove_chain`, `dir_*`, `create_name`, `follow_path`,
  `mount_volume`, `f_open/read/write/lseek/close/opendir/readdir/stat/unlink/mkdir/rename/sync/getfree`.

Estimation asm FAT12/16/32 + LFN + R/W : **≈ 5000-7000 lignes FASMARM, ~18-25 Ko de code**. + exFAT ~3000 lignes / 8 Ko.

### Couche bloc-device (à la place de `CDevice`/`diskio.cpp`)
Table de descripteurs procéduraux :
```
blkdev:  .read  dq proc (x0=dev, x1=lba, x2=count, x3=buf) -> x0=0/err
         .write dq proc
         .sectors dq   ; capacité
         .ssize  dw 512
         .flags  dw    ; amovible, RO, présent
         .priv   dq    ; état pilote (RCA, LUN...)
```
Instances : `sd0` (EMMC2), `usb0..3` (MSD), plus tard `ram0`. Les partitions = `{blkdev*, lba_offset, nsect}` (comme `CPartition`, 99 l.).

### Cache de blocs
- Modèle `CFATCache` : N tampons 512 o, LRU, write-back, `Flush` explicite. Améliorations :
  - **table de hachage** (lba & mask → liste) au lieu d'une recherche linéaire sur 100 entrées ;
  - tampons de **4 Kio alignés** (8 secteurs) pour profiter du multi-bloc CMD18/CMD25 ;
  - **read-ahead** séquentiel (détecter lba = dernier+1) pour les lectures de fichiers ;
  - bit *dirty* + flush périodique (timer, 1-2 s) et au démontage/éjection ;
  - les lectures de données de fichier ≥ 1 cluster aligné **contournent** le cache (DMA/PIO direct vers le tampon utilisateur, comme FatFs le fait déjà pour les secteurs entiers).
- Taille recommandée : 256 × 4 Kio = 1 Mio (la RAM est abondante). Code : **≈ 400-600 lignes asm**.

### VFS minimal + appels système style MenuetOS
MenuetOS/KolibriOS utilisent une fonction système unique (58/70) prenant un **bloc d'information** — à reproduire :
```
SVC #0 avec x8 = 70 (FS), x0 = pointeur sur bloc :
  +0  u32 sous-fonction : 0 lire fichier, 1 lire dossier, 2 créer/écraser, 3 écrire, 4 tronquer/étendre,
                          5 infos (stat), 6 attributs, 7 exécuter, 8 supprimer, 9 mkdir, 10 renommer
  +4  u32 réservé/flags
  +8  u64 offset (lecture/écriture) ou index de départ (readdir)
  +16 u64 taille / nombre d'entrées
  +24 u64 pointeur tampon utilisateur
  +32 chaîne chemin UTF-8 terminée par 0 ("/sd/dir/fichier.txt", "/usb0/...")
retour : x0 = code d'erreur (0 OK, 5 non trouvé, 6 EOF, 8 disque plein, 10 accès refusé, 11 erreur périph.), x1 = octets/entrées traités
```
- Modèle **sans handle** (offset fourni à chaque appel) comme MenuetOS : pas d'état par processus, robuste et simple. Implémentation :
  ouvrir FatFs → `f_lseek` → `f_read` → fermer à chaque appel ; ajouter un **cache des derniers FIL ouverts** (clé = chemin) pour éviter
  de re-parcourir le chemin. Option ultérieure : handles `open/read/write/seek/close` (sous-fonctions 20-24) si les apps le demandent.
- Préfixe de montage : `/sd` → volume FatFs 0 (partition 1 de `sd0`), `/usbN` → volumes 2..4, `/rd` ramdisk.
- `readdir` renvoie des enregistrements fixes (attributs u32, dates, taille u64, nom UTF-8 264 o) comme le format KolibriOS « BDFE ».
- Code VFS + dispatch : **≈ 600-900 lignes asm**.

---

## A6. USB Mass Storage

`lib/usb/usbmassdevice.cpp` (734 l.) — **second bloc-device**, dépend de la pile xHCI (VL805 sur PCIe, cf. étude USB).
- Classe 08h, sous-classe 06h (SCSI transparent), protocole 50h (**Bulk-Only Transport**). 2 endpoints bulk IN/OUT.
- **CBW** 31 octets : `'USBC' = 0x43425355`, tag, `dCBWDataTransferLength`, flags (0x80 = IN), LUN, longueur CB, CB[16].
- **CSW** 13 octets : `'USBS' = 0x53425355`, tag, résidu, statut (0 OK, 1 échec, 2 phase error → reset recovery).
- Séquence `Configure` : INQUIRY (0x12) → vérifier PDT 0x00 (ou 0x0E RBC) → TEST UNIT READY (0x00) en boucle, REQUEST SENSE (0x03)
  si échec → READ CAPACITY(10) (0x25 : dernier LBA + taille bloc, **big-endian**) → I/O par READ(10) 0x28 / WRITE(10) 0x2A
  (LBA 32 bits BE, nb blocs 16 bits BE). Pas de READ(16) ⇒ limite 2 Tio.
- Pièges : clés lentes à devenir prêtes (retries TEST UNIT READY), STALL sur bulk IN ⇒ CLEAR_FEATURE(ENDPOINT_HALT) puis lire le CSW,
  certains périphériques ne supportent que des transferts ≤ 64 Kio ou 120 Kio (découper), LUN multiples (GET MAX LUN 0xFE ignoré par Circle).
- Estimation asm : **≈ 500-700 lignes** au-dessus d'une API xHCI bulk (`usb_bulk(dev, ep, buf, len)`).

---

# PARTIE B — AUDIO

## B1. Couche commune (soundbasedevice et dmasoundbuffers)

### `CSoundBaseDevice` (`lib/sound/soundbasedevice.cpp`, 1096 l.)
- Formats (`soundbasedevice.h:42`) : U8, S16, S24 (3 o), S24_32, **U32 (HW PWM)**, **IEC958 (HW HDMI)**, Float32. Tous entrelacés LE.
- Deux modes d'alimentation : (1) surcharger `GetChunk(buf, n)` (callback appelé depuis l'IRQ DMA) ; (2) `Write()` vers une **file circulaire
  d'octets** (`AllocateQueue(ms)`), `m_nInPtr/m_nOutPtr`, une case vide pour distinguer plein/vide (`GetQueueBytesFree/Avail`, l. 862-890),
  `Enqueue/Dequeue` octet par octet (lent — à remplacer par deux `memcpy` en asm), protégé par spinlock.
- `Write()` convertit **à l'entrée** (format application → format HW) via `ConvertSoundFormat` (l. 697) : normalisation en s32 pleine échelle puis :
  - S16 : `>>16` ; S24 : `>>8` ;
  - **U32 (PWM)** : `((s64)v + 2^31) * RangeMax >> 32` → valeur 0..Range-1 ;
  - **IEC958** : `(v >> 4) & 0x0FFFFFF0` (24 bits dans [27:4]) + bit 31 parité.
  - Mono → stéréo dupliqué ; canaux échangés si `AreChannelsSwapped()` (PWM RPi4).
- `GetChunkInternal` (l. 778) : vide la file dans le tampon DMA, complète par des *null frames* (silence = milieu de plage pour PWM),
  puis **post-traitement IEC958** par blocs de 192 trames : bit C (bit 30) depuis `m_uchIEC958Status`, recalcul parité (bit 31),
  préambule B (`0x0F` dans [3:0]) sur la trame 0. Appelle le callback *need data* si file < seuil.
- Entrée (capture) : symétrique — `AllocateReadQueue`, `SetReadFormat`, `Read()`, `PutChunk()` appelé par l'IRQ RX, `ConvertReadSoundFormat`.

### `CDMASoundBuffers` (`lib/sound/dmasoundbuffers.cpp`, 423 l.) — utilisé par PWM et I2S
- Alloue un canal **DMA Lite** (`AllocateDMAChannel(DMA_CHANNEL_LITE)` → sur RPi4 le plus haut libre ≤ `DMA_CHANNEL_MAX = 7`, en pratique 7 ;
  canaux 7-10 = Lite, 11-14 = DMA4 ; masque des canaux libres donné par le firmware, tag `0x00060001`).
- **Deux control blocks + deux tampons** de `nChunkSize` mots u32, alloués en `HEAP_DMA30` (< 1 Go), **chaînés en anneau**
  (`CB0.next = CB1`, `CB1.next = CB0`) ; alignement CB **32 octets**.
- Sortie : `TI = DREQ<<16 | burst 0 | SRC_WIDTH(128b) | SRC_INC | DEST_DREQ | WAIT_RESP | INTEN` ; `SRC = bus(buf)`, `DST = 0x7Exxxxxx (FIFO)`.
  Entrée : `DEST_WIDTH | DEST_INC | SRC_DREQ`, source = FIFO.
- Démarrage : `DMA_ENABLE |= 1<<ch` (0xFE007FF0), `CS = RESET`, attendre, IRQ `GIC_SPI(80+ch)` (attention **7&8 partagent SPI 87, 9&10 SPI 88**),
  remplir CB0 (silence pour le premier), `CONBLK_AD = bus(CB0)`, `CS = WAIT_OUTSTANDING_WRITES | panic 15<<20 | prio 1<<16 | ACTIVE`, puis remplir CB1.
- IRQ (fin de chaque CB, `INTEN`) : W1C `INT_STATUS (0xFE007FE0)` bit ch, réécrire CS (efface `INT`/`END`), si `CS_ERROR` → échec ;
  sinon **remplir le tampon qui vient d'être consommé** (`m_nNextBuffer ^= 1`) + ajuster `TXFR_LEN` du CB + clean D-cache (tampon + CB).
  Arrêt : `NEXTCONBK = 0` → le DMA s'arrête après le CB courant (état *Terminating* → *Idle*).

Registres DMA (canal n, base `0xFE007000 + n*0x100`, `include/circle/dmacommon.h:61-102`) : `CS +0`, `CONBLK_AD +4`, `TI +8`, `SOURCE_AD +0xC`,
`DEST_AD +0x10`, `TXFR_LEN +0x14`, `STRIDE +0x18`, `NEXTCONBK +0x1C`, `DEBUG +0x20`. Global : `INT_STATUS 0xFE007FE0`, `ENABLE 0xFE007FF0`.
Control block (`dmachannel.h:33`) : `TI, SOURCE_AD, DEST_AD, TXFR_LEN, STRIDE, NEXTCONBK, res, res` (8 × u32).

DREQ (`dmacommon.h:26-47`, RPi4) : **PWM1 = 1**, PCM TX = 2, PCM RX = 3, SMI 4, PWM0 = 5, SPI TX 6 / RX 7, **HDMI0 = 10**, EMMC 11, UART TX 12, RX 14.

## B2. PWM audio (jack 3,5 mm)

`lib/sound/pwmsoundbasedevice.cpp` (257 l.).
- RPi4 : **PWM1** à `0xFE20C800` (`ARM_PWM1_BASE`), DREQ **1**, horloge PWM **125 MHz** (`CLOCK_RATE`).
- Broches : **GPIO 40 et 41, ALT0** (PWM1_0 / PWM1_1) ; `GetGPIOPin` renvoie 41 pour *AudioLeft* sur modèle ≥ 3 (`machineinfo.cpp:525`) et
  `ArePWMChannelsSwapped()` = vrai sur RPi4 ⇒ **inverser L/R** dans les données.
- Registres : `CTL +0`, `STA +4`, `DMAC +8`, `RNG1 +0x10`, `DAT1 +0x14`, `FIF1 +0x18`, `RNG2 +0x20`, `DAT2 +0x24`.
- **Clock Manager** PWM : `CM_PWMCTL = 0xFE101000 + 20*8 = 0xFE1010A0`, `CM_PWMDIV = 0xFE1010A4` (`gpioclock.cpp:66`), mot de passe `0x5A<<24`.
  Séquence : `CTL = PASSWD|KILL(1<<5)`, attendre `BUSY(1<<7) = 0`, `DIV = PASSWD | DIVI<<12 | DIVF`, `CTL = PASSWD | MASH<<9 | SRC`, puis `|= ENAB(1<<4)`.
  Source RPi4 : **PLLD = 750 MHz** (src 6) → DIVI 6, DIVF 0 = 125 MHz (`machineinfo.cpp:610-616` : oscillateur 54 MHz, PLLD 750 MHz).
- Init (`RunPWM`) : clock 125 MHz, 2 ms ; `Range = round(125e6 / fs)` (44100 → **2834**, ≈ 11,5 bits ; 48000 → 2604) ; `RNG1 = RNG2 = Range` ;
  `CTL = PWEN1 | USEF1 | PWEN2 | USEF2 | CLRF1` (+ `MSEN1|MSEN2` si mode M/S demandé — Circle par défaut : mode PWM « sigma-delta » ; M/S donne
  un spectre plus propre mais moins de résolution apparente) ; 2 ms.
- Start : `DMAC = ENAB(1<<31) | PANIC 7<<8 | DREQ 7` ; effacer `RPTL1/2` ; démarrer DMA. **Une seule FIFO (FIF1) partagée** : les mots sont consommés
  alternativement canal 1 / canal 2 ⇒ le tampon DMA est simplement L,R,L,R… en u32 (valeur 0..Range-1).
- Flush : `CTL |= CLRF1`, 10 µs, tampons DMA à zéro.
- Pièges : `Range` entre 256 et 65535 (assert) ; silence = `Range/2` (sinon clic) ; démarrer/arrêter l'horloge cause un « pop » → la laisser tourner ;
  qualité limitée (~11 bits, bruit) — Circle propose aussi `pwmsounddevice.cpp` (154 l., 44,1 kHz stéréo, lecture d'un tampon 8 bits).
- Estimation asm : **≈ 250 lignes** (+ DMA son commun ≈ 300 lignes + clock manager ≈ 60 lignes).

## B3. HDMI audio (MAI + IEC958)

`lib/sound/hdmisoundbasedevice.cpp` (794 l.) — **ne fonctionne que si le firmware a déjà initialisé HDMI0 avec audio** (moniteur annonçant l'audio dans son EDID) :
contrôle `RamPacketConfig.Enable (bit16)` sinon erreur « Requires HDMI display with audio support ». On ne reprogramme ni la PHY ni le mode vidéo.

### Adresses RPi4 (HDMI0 ; `bcm2835.h:379-382`)
`ARM_HDMI_BASE = 0xFEF00700`, `ARM_HD_BASE = 0xFEF20000`, `ARM_PHY_BASE = 0xFEF00F00`, `ARM_RAM_BASE = 0xFEF01B00`.

| Registre | Adresse RPi4 | Champs |
|---|---|---|
| MaiChannelMap | HDMI+0x9C | = `0b00010000` (RPi4) |
| MaiConfig | HDMI+0xA0 | `BitReverse(26) | FormatReverse(27) | ChannelMask 0b11` |
| AudioPacketConfig | HDMI+0xB8 | `CeaMask[..0]=0b11`, `BFrameIdentifier<<10 = 0x0F`, `ZeroDataOnInactiveChannels(24)`, `ZeroDataOnSampleFlat(29)` |
| RamPacketConfig | HDMI+0xBC | `AudioPacketIdentifier(4)`, `Enable(16)` |
| RamPacketStatus | HDMI+0xC4 | bit4 |
| CrpConfig | HDMI+0xC8 | `ExternalCtsEnable(24) | N` |
| Cts0 / Cts1 | HDMI+0xCC / 0xD0 | CTS |
| MaiControl | HD+0x10 | `Reset(0) ErrFull(1) ErrEmpty(2) Enable(3) ChNum[4..] Flush(9) Full(11) WholSample(12) ChAlign(13) Delayed(15)` |
| MaiThreshold | HD+0x14 | DREQ low/high, panic low/high = 16 chacun |
| MaiFormat | HD+0x18 | `SampleRateCode<<8 | AudioFormat(PCM=2)<<16` |
| MaiData | HD+0x1C | FIFO (bus `0x7EF2001C`) |
| MaiSampleRate | HD+0x20 | `N<<8 | (M-1)` |
| RamPacketAudio0..8 | RAM+0x90..0xB0 | Audio InfoFrame |
| TxPhyPowerDownControl | PHY+0x04 | bit4 `RngGenPowerDown` |

### Séquence (`Start` → `RunHDMI` → `SetAudioInfoFrame`)
1. Horloge audio = **108 MHz** fixe (RPi4) ; horloge pixel = mailbox `GET_CLOCK_RATE` `CLOCK_ID_PIXEL_BVB` (14).
2. `MaiSampleRate` : approximation rationnelle `108e6 / fs ≈ N/M` (N 24 bits, M ≤ 256) (`rational_best_approximation`).
3. ACR (Audio Clock Regeneration) : `N = fs*128/1000` (48 kHz → 6144), `CTS = pixclk * N / (128*fs)`.
4. Écrire dans l'ordre : `MaiControl = Reset|Flush|Delayed|ErrEmpty|ErrFull` ; `MaiSampleRate` ; `MaiFormat` (code : index dans
   {8000,11025,12000,16000,22050,24000,32000,44100,48000,64000,88200,96000,128000,176400,192000} + 1) ; `MaiThreshold = 0x10101010` ;
   `MaiConfig` ; `MaiChannelMap` ; `AudioPacketConfig` ; `CrpConfig = ExternalCts | N` ; `Cts0 = Cts1 = CTS`.
5. **Audio InfoFrame** : effacer bit4 de RamPacketConfig, attendre bit4 de RamPacketStatus = 0 (100 ms), écrire
   `Audio0 = 0x000A0184` (type 0x84, version 1, longueur 10), `Audio1 = 0x00000170` (checksum 0x70, CC = 1 → 2 canaux), `Audio2..8 = 0`,
   remettre bit4, attendre statut = 1.
6. DMA : canal Lite, **cyclique 2 tampons** (`CDMAChannel::SetupCyclicIOWrite`, `dmachannel.cpp:265`), même TI que PWM, **DREQ 10**, destination MaiData.
7. `TxPhyPowerDownControl &= ~bit4` ; `MaiControl = 2<<4 | WholSample | ChannelAlign | Enable`.
8. Arrêt : `MaiControl = Delayed|ErrEmpty|ErrFull`, `PHY |= bit4`. Reset : InfoFrame off, `MaiControl = Reset`, `= ErrFull`, `= Flush`.
- Mode polling sans DMA (constructeur à 1 argument) : `IsWritable()` (= `!MaiControl.Full`) + `WriteSample()`.

### Format IEC958 (S/PDIF) par sous-trame (mot u32 écrit dans MaiData)
```
bit 31     P  parité paire sur bits 4..30 (Circle : parity32 sur le mot entier avant pose de P)
bit 30     C  bit de statut canal : bit (n° trame) du bloc de 192 trames
bit 29     U  user data = 0
bit 28     V  validity = 0 (échantillon valide)
bits 27..4    échantillon 24 bits signé (MSB en 27)
bits 3..0     préambule : 0x0F (= IEC958_B_FRAME_PREAMBLE) sur les 2 sous-trames de la trame 0 (début de bloc), 0 sinon
```
- Un **bloc = 192 trames = 384 sous-trames** (stéréo) ; le chunk DMA doit être multiple de 384 mots pour que la numérotation reste alignée
  (Circle traite `nChunkSize` par pas de `IEC958_SUBFRAMES_PER_BLOCK`).
- Octets de statut canal (`soundbasedevice.cpp:142-165`, 5 octets = 40 premiers bits C, le reste 0) :
  `[0] = 0b100` (grand public, PCM, pas de copyright, pas de pré-accentuation), `[1] = 0` (catégorie générale), `[2] = 0`,
  `[3] = Fs` (44,1 k = 0, 48 k = 2, 32 k = 3, 96 k = 10, 192 k = 14, 88,2 k = 8, 176,4 k = 12, 22,05 k = 4, 24 k = 6),
  `[4] = 0b1011 | OrigFs<<4` (longueur 24 bits ; OrigFs : 44,1 k = 15, 48 k = 13, 96 k = 5, …).
- `MaiConfig.BitReverse|FormatReverse` : le bloc MAI attend ce format « inversé » — garder exactement les valeurs de Circle.
- Pièges : si le moniteur n'a pas d'audio, le firmware ne programme pas le packet RAM ⇒ abandon ; HDMI1 non géré (autres adresses, DREQ 17 ?) ;
  ACR faux ⇒ son haché/absent sur certains téléviseurs ; vider le FIFO (`Flush`) avant tout redémarrage ; parité fausse ⇒ silence total.
- Estimation asm : **≈ 400-500 lignes** (+ DMA commun).

## B4. I2S (PCM)

`lib/sound/i2ssoundbasedevice.cpp` (483 l.) — HAT DAC (PCM5102A sans I2C, PCM512x/WM8960 via I2C), micro I2S.
- Bloc PCM : `ARM_PCM_BASE = 0xFE203000` : `CS_A +0`, `FIFO_A +4` (bus `0x7E203004`), `MODE_A +8`, `RXC_A +0xC`, `TXC_A +0x10`, `DREQ_A +0x14`, `INTEN_A +0x18`, `INTSTC_A +0x1C`.
- Broches RPi4 : **GPIO18 PCM_CLK, 19 PCM_FS, 20 PCM_DIN, 21 PCM_DOUT, ALT0**.
- Horloge maître : Clock Manager **PCM** (`CM_PCMCTL = 0xFE101098`, `CM_PCMDIV = 0xFE10109C`), source PLLD 750 MHz :
  `BCLK = 2 canaux × 32 bits × fs` ; `DIVI = 750e6/(64*fs)`, `DIVF = reste*4096/fs` ; MASH 1 si DIVF ≠ 0 (`i2ssoundbasedevice.cpp:72-84`).
  En **esclave** (`bSlave`) : pas d'horloge, `MODE_A |= CLKM | FSM` (BCLK/LRCLK fournis par le codec).
- `RunI2S` : `CS_A = 0` ; `|= TXCLR|RXCLR` ; `TXC_A = RXC_A = CH1WEX|CH1EN|1<<20 (pos 1) | CH2WEX|CH2EN|33<<4 (pos 33)` (largeur 32 bits = WEX + WID 0, décalage 1 bit = format I2S) ;
  `MODE_A = CLKI | FSI | (63<<10 FLEN) | (32 FSLEN)` ; GPIO ; `CS_A |= STBY`, 50 µs, `|= EN`.
- `Start` : `DREQ_A` TX = 0x18 (`<<8`), RX = 0x18 ; `CS_A |= DMAEN` ; DMA TX (DREQ 2) et/ou RX (DREQ 3) via `CDMASoundBuffers` ; `CS_A |= TXON` / `RXON | RXSEX` (extension de signe).
- Format HW : S24_32 (24 bits signés dans 32) en sortie et en entrée.
- Codec : PCM512x/WM8960 configurés en I2C (`pcm512xsoundcontroller.cpp` 223 l., `wm8960soundcontroller.cpp` 339 l.) ; détection automatique par sonde I2C dans Circle.
- Pièges : FIFO 64 mots partagée TX/RX ; `TXERR/RXERR` en cas de sous/débordement (effacer via W1C dans CS) ; synchronisation `SYNC` (bit 24) pour attendre
  la prise en compte entre domaines d'horloge ; la PLLD peut être modifiée par le firmware si `core_freq` change — figer via config.txt.
- Estimation asm : **≈ 300 lignes** (+ ~150 par codec I2C).

## B5. USB Audio (UAC1/UAC2)

`lib/sound/usbsoundbasedevice.cpp` (631 l.), `lib/usb/usbaudiostreaming.cpp` (1119 l.), `usbaudiocontrol.cpp` (225), `usbaudiofunctopology.cpp` (589), `usbsoundcontroller.cpp` (455).
- Classe 01h ; sous-classe 01h AudioControl (topologie : Input/Output Terminal, Feature Unit volume/mute, Selector, Clock Source UAC2),
  02h AudioStreaming (interfaces alternatives : alt 0 = zéro bande passante, alt n = format). Protocole `0x20` = **UAC2**.
- Sélection de l'alternate setting : descripteur CS `AS_GENERAL` → `FORMAT_TYPE` Type I (seul supporté) : nb canaux, taille de sous-trame (2/3/4 o),
  résolution (16/24) ; endpoint **isochrone data** (`bmAttributes & 0x33 == 0x01`).
- Fréquence : UAC1 `SET_CUR SAMPLING_FREQ_CONTROL` vers l'**endpoint** (3 octets) ; UAC2 vers l'**entité Clock Source** (4 octets) (`usbaudiostreaming.cpp:647-700`).
- Synchronisation :
  - **asynchrone** (OUT avec EP feedback `bmAttributes & 0x3F == 0x11`) : lire le feedback (3 o en FS 10.14, 4 o en HS 16.16) pour ajuster la taille des paquets ;
  - **synchrone** (`bmAttributes & 0x0C == 0x0C`) : accumulateur `m_nSyncAccu` ⇒ tailles de paquets 44/45 trames à 44,1 kHz (`UpdateChunkSize`, l. 1092) ;
  - adaptatif : taille fixe `fs × frame / frameRate` arrondie.
- Transferts : un URB par ms (FS : 1 paquet ; HS : 8 micro-trames/`bInterval`), soumis en asynchrone, **double tampon** ; complétion → `GetChunk` (TX) / `PutChunk` (RX).
- **Dépendance xHCI isochrone** : Circle l'implémente (`xhciendpoint.cpp:365-385`) : un TRB `ISOCH` par paquet avec `SIA` (Start Isoch ASAP), `IOC` sur le dernier,
  `TD_SIZE` décroissant ; contexte endpoint `ISOCH_OUT/IN` (l. 616). Sur RPi ≤ 3 (DWHCI) l'entrée audio USB n'est **pas** supportée (`usbaudiostreaming.cpp:291`).
  Pour notre OS : il faut d'abord une pile xHCI avec **anneaux de transfert isochrones** (SIA évite de gérer le n° de frame ; mais attention aux *underruns* :
  si l'anneau se vide, l'hôte signale *Ring Underrun* et il faut ré-amorcer).
- Estimation asm : **≈ 1500-2000 lignes** (parsing descripteurs + contrôle + streaming + feedback), hors xHCI isochrone (+ ~300 lignes dans le pilote xHCI).

## B6. Entrée micro

Le RPi4 **n'a aucune entrée audio analogique** (le jack 4 pôles est sortie audio + vidéo composite). Options :

| Option | Matériel | Support Circle | Effort asm |
|---|---|---|---|
| **Micro I2S MEMS** (INMP441, SPH0645, ICS-43434) | 3,3 V, BCLK=GPIO18, LRCL=GPIO19, DOUT micro→**GPIO20 (PCM_DIN)**, L/R à GND = canal gauche | **Oui** : `CI2SSoundBaseDevice` en `DeviceModeRXOnly` (ou TXRX), `AllocateReadQueue`/`SetReadFormat`/`Read` ; démonstration `sample/42-soundinput` (I2S → PWM, enregistreur WAV sur SD) | **Faible** : réutilise PCM + DMA RX (DREQ 3) ; ~150 lignes au-delà de la sortie I2S |
| **Micro / carte son USB** (UAC1 capture) | n'importe quelle clé son USB à 5 € | **Oui sur RPi4/5** : `usbaudiostreaming` en entrée (`m_bIsOutput = 0`), `SelectInputTerminal`, `SetVolume` ; `sample/42-soundinput` avec `USE_USB` | **Élevé** : nécessite xHCI isochrone IN |
| HAT codec (WM8960, Seeed ReSpeaker) | I2S + I2C | Oui (`wm8960soundcontroller.cpp`, sélection jack micro/line-in) | Moyen |
| ADC SPI (MCP3008) échantillonné | SPI | Non (pas de couche son) | Qualité médiocre — déconseillé |

Spécificités INMP441 : données 24 bits MSB-first, **décalage d'un BCLK** (format I2S standard ⇒ `RXC_A CH1POS = 1` comme Circle), 64 BCLK par trame
(⇒ 32 bits par canal, conforme à `CHANLEN = 32`), le micro **ne pilote la ligne que pour son canal** (l'autre canal = bruit/0 → ignorer) ; le RPi doit être **maître**
(le micro n'a pas d'horloge). Plage BCLK 2,048-4,096 MHz ⇒ fs ≥ 32 kHz (48 kHz recommandé : 3,072 MHz). Données lues : `RXSEX` étend le signe ⇒ s32 avec 24 bits utiles.

**Recommandation** : micro I2S INMP441 = chemin le plus court (même bloc PCM que la sortie I2S, DMA RX identique au TX). USB micro en phase tardive.

## B7. Mixeur logiciel (absent de Circle — à concevoir)

Circle n'a **aucun mixeur** : un seul producteur par périphérique son (une file). Pour un OS multitâche style MenuetOS :
- **N canaux applicatifs** (ex. 16) : chacun = anneau d'échantillons **s16 stéréo à fs système** (48 kHz), volume Q15 par canal, état (actif, pause, underrun).
- **Rééchantillonnage** à l'entrée si l'application soumet 44,1/22,05 kHz : interpolation linéaire en Q16 (suffisant) ; conversion U8/S24/Float → s16/s32.
- **Mixage dans le callback DMA** (IRQ) ou dans un thread noyau réveillé par l'IRQ : pour chaque trame, `acc(s32) = Σ ch[i] * vol[i] >> 15`, saturation
  (`SQADD`/`SQXTN` NEON : 8 échantillons s16 par instruction → mixage de 16 voies × 1024 trames ≈ quelques µs), volume maître, puis **conversion HW**
  (PWM : `(acc + 32768) * Range >> 16` ; HDMI : `acc<<8` → IEC958 + bits C/P/préambule ; I2S : `acc<<8` S24_32).
- Latence : chunk 1024 mots stéréo à 48 kHz ≈ 10,7 ms par tampon ; double tampon ⇒ ~21 ms.
- Syscalls proposés (fonction 55 « son » à la MenuetOS) : `55.0 ouvrir canal (fs, format, canaux)` → id ; `55.1 écrire (id, buf, taille)` → octets acceptés ;
  `55.2 volume (id, g)` ; `55.3 fermer` ; `55.4 état/espace libre` ; `55.10 sélection sortie (PWM/HDMI/I2S/USB)` ; `55.20 ouvrir capture` / `55.21 lire`.
- Estimation asm : **≈ 600-900 lignes** (avec NEON).

---

# C. Plan de transposition global

| Prio | Module | Dépend de | Lignes asm (est.) | Code |
|---|---|---|---:|---:|
| 1 | Mailbox (power, clock rate, GPIO expandeur) — probablement déjà fait (vidéo) | — | 100 | 0,5 Ko |
| 2 | **EMMC2 PIO** init + R/W multi-blocs | 1, timer | 1000-1200 | 4-5 Ko |
| 3 | Bloc-device + partitions MBR | 2 | 200 | 1 Ko |
| 4 | **Cache de blocs** (hash, LRU, write-back) | 3 | 400-600 | 2 Ko |
| 5 | **FatFs FAT32 lecture** (mount, follow_path, LFN, f_read, readdir) | 4 | 2500-3000 | 9 Ko |
| 6 | **VFS + syscall 70** (lecture, readdir, stat) | 5 | 600-900 | 3 Ko |
| 7 | FatFs écriture (create/extend chain, dir_register, LFN gen, unlink, mkdir, rename, sync) | 5 | 2500-3500 | 10 Ko |
| 8 | **DMA son commun + PWM** (+ Clock Manager) | GIC, DMA | 600 | 2,5 Ko |
| 9 | Couche son : file, conversions, **mixeur** | 8 | 800-1100 | 4 Ko |
| 10 | **HDMI audio** IEC958 | 8, 9, firmware HDMI | 400-500 | 2 Ko |
| 11 | **I2S TX/RX** + micro INMP441 | 8, 9 | 400-450 | 2 Ko |
| 12 | exFAT (lecture puis écriture) | 5, 7 | 2500-3500 | 8 Ko |
| 13 | USB Mass Storage | xHCI bulk | 500-700 | 2,5 Ko |
| 14 | GPT | 3 | 150 | 0,5 Ko |
| 15 | ADMA2 EMMC2 | 2 | 200-300 | 1 Ko |
| 16 | USB Audio UAC1/2 (sortie puis capture) | xHCI isochrone | 1500-2000 (+300 xHCI) | 7 Ko |
| — | Codecs I2C (PCM512x, WM8960) | I2C | 150 chacun | |

**Total stockage** (2-7, 12-15) ≈ 11 000-14 000 lignes ; **total audio** (8-11, 16) ≈ 3 700-5 000 lignes. Jalon minimal utile (« lire un fichier de la carte SD
et jouer un WAV sur le jack ») = modules 1-6 + 8-9 ≈ **6 500-8 000 lignes**.

---

# D. Pseudo-asm AArch64

Conventions : `x0..x7` arguments/retour, `x19..x28` callee-saved, structure volume `VOL` en mémoire :
```
struc VOL {
  .part_lba   dq ?   ; LBA début partition
  .fat_start  dq ?   ; LBA absolu de la FAT n°0
  .data_start dq ?   ; LBA absolu du cluster 2
  .n_clusters dd ?   ; nb de clusters + 2 (borne max valide exclusive)
  .spc_shift  db ?   ; log2(SecPerClus)
  .num_fats   db ?
  .fat_size   dd ?   ; secteurs par FAT
  .root_clus  dd ?
  .bdev       dq ?   ; -> blkdev
}
```
`cache_get(x0 = bdev, x1 = lba) -> x0 = ptr tampon 512 o ou 0` (le cache gère la lecture), `bdev_read(x0 = bdev, x1 = lba, x2 = n, x3 = buf) -> x0 = 0/err`.

### D1. Entrée FAT32 suivante (`get_fat`)
```asm
; fat32_next: x0 = VOL*, w1 = cluster courant
; retour : w0 = cluster suivant ; 0x0FFFFFFF si EOC ; 1 si erreur (cluster invalide / bad / I/O)
fat32_next:
        stp     x29, x30, [sp, #-32]!
        mov     x29, sp
        stp     x19, x20, [sp, #16]
        mov     x19, x0
        cmp     w1, #2                      ; clusters 0/1 réservés
        b.lo    .err
        ldr     w2, [x19, VOL.n_clusters]
        cmp     w1, w2
        b.hs    .err
        lsl     w20, w1, #2                 ; offset octet = c*4
        ldr     x1, [x19, VOL.fat_start]
        add     x1, x1, x20, lsr #9         ; LBA = fat_start + (c*4)/512
        and     w20, w20, #0x1FC            ; offset dans le secteur
        ldr     x0, [x19, VOL.bdev]
        bl      cache_get                   ; x0 = tampon
        cbz     x0, .err
        ldr     w0, [x0, x20]               ; entrée LE 32 bits (alignée sur 4)
        and     w0, w0, #0x0FFFFFFF         ; 28 bits utiles
        cmp     w0, #2
        b.lo    .err                        ; 0 (libre) ou 1 dans une chaîne = corruption
        mov     w2, #0x0FFFFFF7
        cmp     w0, w2
        b.eq    .err                        ; cluster défectueux
        b.lo    .ok                         ; maillon normal
        mov     w0, #0x0FFFFFFF             ; >= 0x0FFFFFF8 : normaliser EOC
.ok:    ldp     x19, x20, [sp, #16]
        ldp     x29, x30, [sp], #32
        ret
.err:   mov     w0, #1
        b       .ok
```

### D2. Lecture d'un cluster entier (contourne le cache, multi-blocs)
```asm
; clus_read: x0 = VOL*, w1 = cluster, x2 = tampon (taille = 512 << spc_shift, aligné 4)
; retour x0 = 0 OK / !=0 erreur
clus_read:
        stp     x29, x30, [sp, #-16]!
        mov     x29, sp
        sub     w1, w1, #2                  ; c - 2
        ldrb    w4, [x0, VOL.spc_shift]
        lsl     x1, x1, x4                  ; (c-2) << spc_shift   (x1 étendu à zéro par l'écriture w1)
        ldr     x5, [x0, VOL.data_start]
        add     x1, x1, x5                  ; LBA absolu
        mov     x3, x2                      ; buf
        mov     x2, #1
        lsl     x2, x2, x4                  ; nb secteurs = SecPerClus
        ldr     x0, [x0, VOL.bdev]
        bl      bdev_read                   ; -> CMD18 + auto-CMD12 si n>1 (EMMC2)
        ldp     x29, x30, [sp], #16
        ret
```

### D3. Lire un fichier en suivant sa chaîne (offset arbitraire)
```asm
; file_read: x0 = VOL*, w1 = premier cluster, x2 = offset fichier, x3 = taille demandée,
;            x4 = buf destination, x5 = taille fichier
; retour : x0 = octets lus, ou -1 si erreur
file_read:
        stp     x29, x30, [sp, #-96]!
        mov     x29, sp
        stp     x19, x20, [sp, #16]
        stp     x21, x22, [sp, #32]
        stp     x23, x24, [sp, #48]
        stp     x25, x26, [sp, #64]
        mov     x19, x0                     ; VOL
        mov     w20, w1                     ; cluster courant
        mov     x21, x2                     ; offset
        mov     x22, x4                     ; dst
        ; borner la taille à (taille_fichier - offset)
        subs    x6, x5, x2
        csel    x6, xzr, x6, lo
        cmp     x3, x6
        csel    x23, x3, x6, lo             ; x23 = restant
        mov     x26, x23                    ; total pour le retour
        ldrb    w7, [x19, VOL.spc_shift]
        add     w7, w7, #9
        mov     x24, #1
        lsl     x24, x24, x7                ; x24 = octets/cluster
        ; sauter les clusters entiers avant l'offset
        lsr     x25, x21, x7                ; nb de sauts
.skip:  cbz     x25, .loop
        mov     x0, x19
        mov     w1, w20
        bl      fat32_next
        cmp     w0, #2
        b.lo    .fail
        mov     w8, #0x0FFFFFFF
        cmp     w0, w8
        b.eq    .eof                        ; offset au-delà de la chaîne
        mov     w20, w0
        sub     x25, x25, #1
        b       .skip
.loop:  cbz     x23, .done
        ; position dans le cluster
        sub     x9, x24, #1
        and     x9, x21, x9                 ; off_in = offset & (bpc-1)
        sub     x10, x24, x9                ; dispo dans ce cluster
        cmp     x10, x23
        csel    x10, x10, x23, lo           ; n = min(dispo, restant)
        cbnz    x9, .partial
        cmp     x10, x24
        b.ne    .partial
        ; cas rapide : cluster complet aligné -> directement dans dst
        mov     x0, x19
        mov     w1, w20
        mov     x2, x22
        bl      clus_read
        cbnz    x0, .fail
        b       .adv
.partial:
        ; secteur(s) via le cache : LBA = data_start + (c-2)<<spc + off_in>>9
        ; (boucle secteur par secteur, memcpy de min(512-(off&511), n) octets)
        mov     x0, x19
        mov     w1, w20
        mov     x2, x9                      ; off_in
        mov     x3, x22                     ; dst
        mov     x4, x10                     ; n
        bl      clus_copy_partial           ; via cache_get + memcpy
        cbnz    x0, .fail
.adv:   add     x21, x21, x10
        add     x22, x22, x10
        sub     x23, x23, x10
        cbz     x23, .done
        sub     x9, x24, #1
        tst     x21, x9
        b.ne    .loop                       ; encore dans le même cluster
        mov     x0, x19                     ; frontière : maillon suivant
        mov     w1, w20
        bl      fat32_next
        cmp     w0, #2
        b.lo    .fail
        mov     w8, #0x0FFFFFFF
        cmp     w0, w8
        b.eq    .done                       ; chaîne plus courte que FileSize : s'arrêter
        mov     w20, w0
        b       .loop
.eof:
.done:  sub     x0, x26, x23                ; octets effectivement copiés
.ret:   ldp     x25, x26, [sp, #64]
        ldp     x23, x24, [sp, #48]
        ldp     x21, x22, [sp, #32]
        ldp     x19, x20, [sp, #16]
        ldp     x29, x30, [sp], #96
        ret
.fail:  mov     x0, #-1
        b       .ret
```
Optimisations ultérieures : fusionner les clusters **contigus** (c, c+1, …) en une seule commande CMD18 (≈ ce que fait FatFs avec `cc` dans `f_read`) ;
en exFAT `NoFatChain` ⇒ `next = c + 1` sans lire la FAT ; mémoriser (offset, cluster) dans le descripteur de fichier pour éviter de re-parcourir la chaîne (FatFs `fp->clust`).

### D4. Control blocks DMA pour PWM (double tampon cyclique) et démarrage
```asm
; Constantes RPi4
PERI_BASE        = 0xFE000000
DMA_BASE         = PERI_BASE + 0x007000
DMA_ENABLE       = DMA_BASE + 0xFF0
DMA_INT_STATUS   = DMA_BASE + 0xFE0
PWM1_BASE        = PERI_BASE + 0x20C800
PWM_FIF1_BUS     = 0x7E20C818            ; (0xFE20C818 & 0xFFFFFF) + 0x7E000000
DREQ_PWM1        = 1
TI_INTEN         = 1 shl 0
TI_WAIT_RESP     = 1 shl 3
TI_DEST_DREQ     = 1 shl 6
TI_SRC_INC       = 1 shl 8
TI_SRC_WIDTH     = 1 shl 9
TI_PERMAP_SHIFT  = 16
CS_ACTIVE        = 1 shl 0
CS_RESET         = 1 shl 31
CS_WAIT_WRITES   = 1 shl 28
TI_PWM = (DREQ_PWM1 shl TI_PERMAP_SHIFT) or TI_SRC_WIDTH or TI_SRC_INC \
         or TI_DEST_DREQ or TI_WAIT_RESP or TI_INTEN       ; = 0x00010349

; Zone DMA en RAM basse (< 1 Go), CB alignés 32 octets
        align 32
pwm_cb0:   dw 0,0,0,0,0,0,0,0            ; TI, SRC, DST, LEN, STRIDE, NEXT, res, res
pwm_cb1:   dw 0,0,0,0,0,0,0,0
        align 64
pwm_buf0:  rd CHUNK                      ; CHUNK mots u32 : L,R,L,R… (valeurs 0..Range-1)
pwm_buf1:  rd CHUNK

; adresse bus d'une RAM = (a & 0x3FFFFFFF) | 0xC0000000  (deux instructions and/orr ci-dessous)

; pwm_dma_setup: x0 = n° canal DMA (Lite, ex. 7)
pwm_dma_setup:
        stp     x29, x30, [sp, #-32]!
        mov     x29, sp
        str     x19, [sp, #16]
        mov     x19, x0
        ; --- remplir CB0 ---
        adr     x1, pwm_cb0
        adr     x2, pwm_buf0
        adr     x3, pwm_cb1
        mov     w4, #TI_PWM and 0xFFFF
        movk    w4, #TI_PWM shr 16, lsl #16
        str     w4, [x1, #0]                ; TI
        and     w5, w2, #0x3FFFFFFF
        orr     w5, w5, #0xC0000000
        str     w5, [x1, #4]                ; SOURCE_AD = bus(buf0)
        mov     w6, #PWM_FIF1_BUS and 0xFFFF
        movk    w6, #PWM_FIF1_BUS shr 16, lsl #16
        str     w6, [x1, #8]                ; DEST_AD = FIFO PWM1 (pas d'incrément)
        mov     w7, #CHUNK*4
        str     w7, [x1, #12]               ; TXFR_LEN (octets)
        str     wzr, [x1, #16]              ; STRIDE
        and     w8, w3, #0x3FFFFFFF
        orr     w8, w8, #0xC0000000
        str     w8, [x1, #20]               ; NEXTCONBK = bus(CB1)
        stp     wzr, wzr, [x1, #24]
        ; --- remplir CB1 (symétrique, NEXT = CB0) ---
        adr     x2, pwm_buf1
        str     w4, [x3, #0]
        and     w5, w2, #0x3FFFFFFF
        orr     w5, w5, #0xC0000000
        str     w5, [x3, #4]
        str     w6, [x3, #8]
        str     w7, [x3, #12]
        str     wzr, [x3, #16]
        and     w8, w1, #0x3FFFFFFF
        orr     w8, w8, #0xC0000000
        str     w8, [x3, #20]               ; NEXTCONBK = bus(CB0) -> anneau
        stp     wzr, wzr, [x3, #24]
        ; tampons initiaux = silence (Range/2) puis nettoyer le D-cache (CB + tampons)
        bl      pwm_fill_silence
        adr     x0, pwm_cb0
        mov     x1, #64                     ; 2 CB
        bl      dcache_clean_range          ; DC CVAC par ligne de 64 o + DSB SY
        adr     x0, pwm_buf0
        mov     x1, #CHUNK*8
        bl      dcache_clean_range
        ; --- activer et réinitialiser le canal ---
        mov     x9, #DMA_ENABLE and 0xFFFF
        movk    x9, #(DMA_ENABLE shr 16) and 0xFFFF, lsl #16
        ldr     w10, [x9]
        mov     w11, #1
        lsl     w11, w11, w19
        orr     w10, w10, w11
        str     w10, [x9]                   ; DMA_ENABLE |= 1<<ch
        mov     x12, #DMA_BASE and 0xFFFF
        movk    x12, #(DMA_BASE shr 16) and 0xFFFF, lsl #16
        add     x12, x12, x19, lsl #8       ; base canal = DMA_BASE + ch*0x100
        mov     w13, #CS_RESET shr 16
        lsl     w13, w13, #16
        str     w13, [x12, #0]              ; CS = RESET
.rst:   ldr     w14, [x12, #0]
        tbnz    w14, #31, .rst
        ; --- PWM : DMAC = ENAB | PANIC 7 | DREQ 7 (seuils FIFO) ---
        mov     x15, #PWM1_BASE and 0xFFFF
        movk    x15, #(PWM1_BASE shr 16) and 0xFFFF, lsl #16
        mov     w16, #0x0707
        orr     w16, w16, #0x80000000
        str     w16, [x15, #8]              ; PWM_DMAC
        ; --- lancer : CONBLK_AD = bus(CB0), CS = WAIT_WRITES | PANIC 15 | PRIO 1 | ACTIVE ---
        adr     x1, pwm_cb0
        and     w2, w1, #0x3FFFFFFF
        orr     w2, w2, #0xC0000000
        str     w2, [x12, #4]
        mov     w3, #(15 shl 4) or 1        ; panic(15)<<20 | prio(1)<<16, construit en 2 temps
        lsl     w3, w3, #16
        orr     w3, w3, #CS_WAIT_WRITES
        orr     w3, w3, #CS_ACTIVE
        str     w3, [x12, #0]
        dsb     sy
        ldr     x19, [sp, #16]
        ldp     x29, x30, [sp], #32
        ret

; IRQ DMA (INTID 32+80+ch ; ch 7 et 8 partagent 119) :
;   - W1C DMA_INT_STATUS bit ch ; relire CS et le réécrire (efface INT/END) ; si CS bit8 (ERROR) -> arrêt
;   - tampon libre = pwm_next (alterner 0/1) : mixer/convertir CHUNK mots, DC CVAC, DSB
;   - pour arrêter : écrire 0 dans NEXTCONBK (+0x1C) -> fin après le CB en cours
```
(Les instructions `mov`/`movk` avec expressions sont à adapter à la syntaxe exacte de FASMARM ; les `and wX, wY, #0x3FFFFFFF` sont des immédiats logiques valides.)
Points de vigilance : CB et tampons **< 1 Go** (adresses bus 32 bits du DMA legacy), **alignement 32 o** des CB, nettoyage D-cache
avant chaque remise du tampon au DMA (`DC CVAC` + `DSB`), et **pas** de `TI_SRC_WIDTH` 128 bits si le tampon n'est pas aligné sur 16 o.

---

## Références Circle citées
`addon/SDCard/emmc.cpp` (98-205 registres/commandes, 540-660 init RPi4, 762-930 power/clock, 969-1160 PIO, 1506-2330 CardReset, 2384-2615 data) ·
`include/circle/bcm2711.h:31` · `include/circle/bcm2711int.h:33-62` · `include/circle/bcm2835.h:30,41-57,122,181-221,248,352-386` ·
`lib/fs/partitionmanager.cpp:100-125` · `include/circle/fs/fat/fatfsdef.h` · `lib/fs/fat/fat.cpp`, `fatcache.cpp`, `fatdir.cpp` ·
`addon/fatfs/ffconf.h`, `ff.c` (3744-5390 API ; blocs `#if FF_FS_EXFAT`), `diskio.cpp:30-36` · `lib/usb/usbmassdevice.cpp:40-200` ·
`lib/sound/soundbasedevice.cpp:140-165, 669-930` · `lib/sound/dmasoundbuffers.cpp` · `include/circle/dmacommon.h:26-102` · `include/circle/dmachannel.h:33` ·
`lib/sound/pwmsoundbasedevice.cpp` · `lib/gpioclock.cpp:33-90` · `lib/machineinfo.cpp:518-560, 590-620, 665-720` ·
`lib/sound/hdmisoundbasedevice.cpp` · `lib/sound/i2ssoundbasedevice.cpp` · `lib/sound/usbsoundbasedevice.cpp` · `lib/usb/usbaudiostreaming.cpp:246-400, 647-830, 1092` ·
`lib/usb/xhciendpoint.cpp:365-385, 616` · `sample/42-soundinput/` (README, config.h, kernel.cpp).
