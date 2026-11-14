# Pilotes du PC : PCIe, MSI/MSI-X, pcid, xHCI

Suivi du chantier commencé le 12/11/2026 sur la branche `claude/x86-64-pc` (voir
[ETUDE-X86.md](ETUDE-X86.md)). Un autre agent doit pouvoir reprendre d'ici : décisions, fait,
vérifié, reste.

## 1. Décisions

* **Micronoyau** : le noyau ne fait que donner l'accès (registres, interruptions, tables du
  micrologiciel). L'énumération du PCIe (pcid), les pilotes (usbd…) sont des services.
* **Le moins d'appels nouveaux** : deux, en fin de la plage `0x40` (`docs/ABI.md`) :
  * `SYS_ACPI_TABLE` (`0x4D`) : copie d'une table ACPI. pcid y prend la MCFG (adresse de l'ECAM) ;
    ACPICA en aura besoin (DSDT, SSDT), et les tables ACPI sont dans la RAM, que `DEV_MAP` refuse.
  * `SYS_IRQ_MSI` (`0x4E`) : des vecteurs MSI/MSI-X ; le pilote reçoit, par vecteur, l'adresse et la
    donnée à écrire dans son périphérique, et une **ligne** (`0x100`…) qui se lie ensuite par les appels
    existants `IRQ_BIND` / `IRQ_ACK` / `IRQ_WAIT` / `IRQ_UNBIND`, comme toute ligne.
  * Rien d'autre : l'espace de configuration (ECAM) et les BAR sont des registres, mappés par
    `DEV_MAP` tel quel (il refuse seulement la RAM) ; le DMA cohérent est `DMA_ALLOC`. Pas de
    `PCI_CONFIG` dans le noyau : pcid lit et écrit l'ECAM lui-même.
  * Sens sur le Pi : `ACPI_TABLE` n'a pas d'objet (pas d'ACPI, `E_NOSYS` par la table du noyau asm) ;
    `IRQ_MSI` pourrait prendre le même sens sur le Pi 5 (le RP1 lève ses interruptions par le contrôleur
    MSI du pont PCIe du BCM2712) et sur le Pi 4 (MSI du pont du BCM2711 vers le GIC) ; pas fait.
* **Masque des MSI** : une MSI est un front, sans IOAPIC à masquer. Le noyau tient le masque (une
  interruption arrivée masquée est gardée, rendue au démasquage) : même contrat qu'une ligne de
  l'IOAPIC pour `hw.c` (« masquée à la levée, démasquée par `IRQ_ACK` »).
* **Vecteurs** : `0x40`–`0xB7` pour les GSI de l'IOAPIC (120, autant que l'IOAPIC du PCH de Tiger
  Lake), `0xB8`–`0xEF` pour les MSI sur **chaque** cœur (56 × nombre de cœurs), `0xF0` l'IPI du TLB.
  Les lignes MSI sont réparties sur les cœurs en tournant ; 128 lignes MSI au plus (`IRQ_MSI_LINES`).
  Une adresse MSI ne vise qu'un id d'APIC < 256 (sans remappage) : c'est le cas du ZBook.
* **pcid joue pour le PCIe le rôle de usbd pour l'USB** (demande du 12/11) : il annonce chaque fonction
  à hwmanager (`HW_OP_PCI`), qui lance le pilote de `drivers.txt` (`pci:VVVV:DDDD`, `pci:cc/ss/pp`,
  `pci:cc/ss`, `pci:cc`, de la plus précise à la moins précise) avec l'adresse « `SSSS:BB:DD.F` ».
  Un pilote **prend** sa fonction (`PCI_OP_CLAIM` : décodage mémoire, maître du bus, INTx coupé ;
  rendue à sa fin, maître du bus coupé), mappe ses BAR et ses MSI lui-même (`pcilib.c`).
* **Les pilotes gardent les interfaces existantes** : usbd annonce les interfaces USB à hwmanager
  (`HW_OP_USB`) et parle aux services de classe par `src/inc/usb.inc` inchangé ; stockage = `blk.*`
  monté par vfs ; réseau par netd ; son par sound ; entrées par hidmanager ; branchements par busd.
* **xHCI en C commun** : `xhci.c` est le même pour le Pi et le PC ; `plat_<arch>.c` est la couche de
  la machine. **Le Pi garde `usbd.asm`** : QEMU `raspi4b` n'émule pas de PCIe (l'asm le détecte et
  s'efface : « absent (émulateur, pas de PCIe) »), le clavier et la tablette de `tools/qemu_drive.py`
  passent par le **DWC2** (port USB-C), que le C n'a pas ; rien ne peut donc vérifier le C du VL805
  ici, et la règle est de ne jamais remplacer l'asm du Pi par du C non vérifié.

## 2. Fait

### Noyau du PC

| Fichier | Fait |
|---|---|
| `src/inc/syscalls.inc` | `SYS_ACPI_TABLE = 0x4D`, `SYS_IRQ_MSI = 0x4E` |
| `src/kernel/include/korin/hw.h` | lignes MSI (`IRQ_MSI_BASE` 0x100, 128 lignes), `IRQ_LINES` = 0x180 ; interface `arch_irq_msi_*`, `arch_firmware_table` |
| `src/kernel/core/hw.c` | `sys_irq_msi`, `sys_acpi_table` ; une ligne MSI n'est liée que par son propriétaire ; rendue par `IRQ_UNBIND` (dernier détenteur) et à la fin du process |
| `src/kernel/core/syscall.c` | les deux appels |
| `src/kernel/arch/pc/msi.c` | vecteurs `0xB8`–`0xEF` par cœur, répartition en tournant, masque logiciel et interruption gardée |
| `src/kernel/arch/pc/apic.c`, `apic.h` | GSI limités à `0x78` ; lignes ≥ `0x100` vers `msi.c` ; `info` du moniteur montre les MSI prises |
| `src/kernel/arch/pc/acpi.c`, `acpi.h` | `acpi_table(sig, rang)` (XSDT ou RSDT ; DSDT et FACS par la FADT) |
| `src/kernel/arch/pc/initrd/ktest.c` | 10 essais de plus : `ACPI_TABLE` (APIC, DSDT, absente), `IRQ_MSI` (adresses `0xFEExxxxx`, vecteurs, deux vecteurs distincts, liaison, `IRQ_WAIT` à délai, rendue, refus de 0) |

### pcid (`src/svc/pcid/`, C, pour le PC)

* `pcid.c` : MCFG par `ACPI_TABLE` ; chaque bus mappé (1 Mo) au premier passage, descente par les
  ponts depuis le premier bus du segment (numéros du micrologiciel, rien n'est renuméroté) ; par
  fonction : identité, classe, sous-système, BAR (taille mesurée décodage coupé, **sauf l'affichage**,
  classe 03, dont le framebuffer GOP sert à la console), capacités MSI, MSI-X, PCIe, énergie ;
  annonces `HW_OP_PCI` (hwmanager absent : rien, dit une fois) ; service « pci ».
  `pcid -l` : liste et fin ; `pcid -n` : service sans annonce (essai au moniteur).
* `pci.h` : protocole (`PCI_OP_COUNT`, `INFO`, `FIND`, `CLAIM`, `RELEASE`, `CFG_READ`, `CFG_WRITE`,
  `RESCAN`), `struct pci_info` (160 o, mêmes décalages partout), API de `pcilib.c`. En asm :
  `src/inc/pci.inc` ; `HW_OP_PCI = 3` dans `src/inc/usb.inc` (à la suite de `HW_OP_USB_RETRAIT`).
* `pcilib.c` (compilé avec chaque pilote) : `pci_parse_addr`, `pci_format_addr`, `pci_open`,
  `pci_claim`, `pci_find_class`, `pci_cfg_read/write`, `pci_map_bar`, `pci_irq_setup` (MSI-X : table
  mappée, entrées écrites, fonction démasquée ; sinon MSI à un vecteur).
* `skel/sd0/etc/drivers.txt` : `pci:0c/03/30 = SD0:/sys/svc/usbd` (le hwmanager du Pi ignore une clé
  `pci:` : elle tombe dans le test « `i2c1:` » et sort).
* Pour le Pi : le Pi 4 n'a qu'une fonction PCIe (le VL805, derrière un pont sans ECAM ni ACPI) et le
  Pi 5 une seule aussi (le RP1, mis en place par le micrologiciel) : pcid ne leur apporterait rien sans
  une autre découverte (le pont du BCM2711 a ses registres d'index et de données, `EXT_CFG_INDEX` /
  `EXT_CFG_DATA`, que `plat_aarch64.c` sait déjà manier). Le code est portable (il se compile pour
  AArch64) ; seule `read_mcfg` dépend de l'ACPI.

### hwmanager : interface (faite en C, voir « Services du système en C » plus bas)

hwmanager est passé en C le 12/11 (`src/svc/hwmanager`) avec ce qui suit ; le texte d'origine :

1. accepter `HW_OP_PCI` (3) : `a` = adresse, `b` = `cc << 16 | ss << 8 | pp`, `c` = `VVVV << 16 | DDDD`,
   `d` = sous-système ; répondre 0 tout de suite (pcid annonce en `SVC_CALL`, une fonction après l'autre) ;
2. lire dans `drivers.txt` les clés `pci:` (comme `usb:`, table de 16 lignes au moins : le ZBook a
   une trentaine de fonctions) et chercher `pci:VVVV:DDDD`, `pci:cc/ss/pp`, `pci:cc/ss`, `pci:cc` ;
3. lancer le pilote avec « `SSSS:BB:DD.F` » (`pci_format_addr`) ; noter le pid ; un pilote qui meurt
   peut être relancé (comme `soc:`), pcid lui rend la fonction (`SVC_OP_END`).

Ordre de démarrage prévu sur PC : init2 lance busd, hwmanager, puis pcid (`services.txt`), et non plus
usbd ; hwmanager lance usbd pour chaque xHCI.

### usbd en C (`src/svc/usbd/*.c`)

| Fichier | Rôle |
|---|---|
| `usbd.c` | le cœur, repris de `usbd.asm` : table de 16 périphériques (numéros en tournant), tuyaux d'interruption (réponse différée), tampons partagés des pilotes (`BUF_CREATE` / `MAP` / `GRANT`), bulk, bulk sans attente, `CLEAR_FEATURE(ENDPOINT_HALT)` suivi de la remise de l'extrémité, annonces `HW_OP_USB` / `HW_OP_USB_RETRAIT` à hwmanager, publication `korin.usb.device.<n>` sur busd (protocole de `buspub.inc`, sans BusKit), fin d'un pilote (`SVF_ENDS`) ; isochrone : `E_NOSYS` |
| `xhci.c` | le xHCI commun, repris de `xhci.inc` : zone DMA de 2 Mo, anneaux de commandes, d'événements, de transferts ; reprise au BIOS (capacité USB Legacy Support : le ZBook émule le clavier USB à l'UEFI) ; ports alimentés si PPC ; scratchpad de n'importe quelle taille (zone à part) ; énumération des ports racines et des hubs USB 2 (route, TT) ; branchements et retraits (événement de changement de port, et scrutation) ; contrôle, interruption (deux tampons), bulk (tampon de 64 Ko aligné), **bulk IN sans attente** (l'asm rendait `E_NOSYS` sur xHCI : usbnet en a besoin) ; interruptions par MSI-X si la machine en donne, sinon sondage |
| `usbd.h` | interface entre les trois ; barrières (x86 : compilateur ; AArch64 : `dmb`/`dsb` comme l'asm) |
| `plat_x86_64.c` | PC : la fonction de pcid (adresse en argument, sinon le premier xHCI), BAR0, MSI-X par `pci_irq_setup` ; DMA cohérent, adresse = physique |
| `plat_aarch64.c` | Pi, **jamais essayé** : détection de l'émulateur, RP1 du Pi 5 (décalage DMA `0x10_0000_0000`), pont PCIe du Pi 4 et VL805 (repris ligne à ligne de `xh_pcie_init`, `NOTIFY_XHCI_RESET`), fenêtre DMA de 1 Go ; sondage |
| `svc.conf` | `sources = ../pcid/pcilib.c` |

Le C se construit pour les deux machines (`build/svc/aarch64/usbd` existe) mais **n'est copié nulle part
sur la carte du Pi** : `build.ps1` ne connaît pas `build/svc`.

### Pilotes de classe USB en C (12/11), communs au Pi et au PC

Repris pas à pas de l'asm, mêmes messages, même protocole (`usb.inc` avec usbd, anneau `HID_OP_PILOTE` de
hidmanager, `blk.*` pour vfs, netd, sound), mêmes bizarreries (commentaires « comme l'asm »). L'asm reste à
côté dans chaque dossier ; `build.ps1` met le C sur la carte à sa place (liste `$svc_c`), puisqu'il est
vérifié sous QEMU raspi4b (§3).

| Fichier | Rôle |
|---|---|
| `src/svc/usbhid/` (`hid.h`, `hiddrv.c`, `hidparse.c`, `hidpad.c`) | le corps commun des pilotes HID (`hiddrv.inc`), la lecture du descripteur de rapport et le décodage (`hid.inc`), les manettes (`hidpad.inc`) ; pas un service (pas de `main`) : chaque pilote le compile par son `svc.conf`. `hidparse.c` pourra servir au touchpad I2C-HID |
| `src/svc/usbkeyboard/usbkeyboard.c`, `usbmouse/usbmouse.c`, `usbgamepad/usbgamepad.c` | trois services comme aujourd'hui : `hid_driver_run("usbkeyboard", 0)`… (usbgamepad : `pad` = 1) |
| `src/svc/usbstorage/usbstorage.c` | Bulk-Only / SCSI, tampon partagé de 68 Ko, montage en `USBn:` par un thread, retrait (busd, sinon sondage) ; le serveur de blocs est `src/svc/common/blkserv.c` (celui d'imgblk, écrit en même temps par une autre session), BusKit importé |
| `src/svc/netif/` (`netif.h`, `netif.c`) | côté pilote d'une interface de netd (`netif.inc`) : inscription, anneau de trames, état du lien, réinscription |
| `src/svc/usbnet/usbnet.c` | CDC-ECM tel quel (pas de NCM ni de RNDIS : la configuration ECM est choisie par `SET_CONFIGURATION`, comme l'asm) |
| `src/svc/sndif/` (`sndif.h`, `sndif.c`) | côté pilote de sound (`sndif.inc`), sans `sndif_irq_*` (DMA du SoC : pwmaudio, hdmiaudio restent en asm) |
| `src/svc/usbaudio/usbaudio.c` | UAC1 : lecture (tuyau isochrone OUT, ~24 ms d'avance), capture ouverte à la première écoute |
| `src/svc/usbclasstest/usbclasstest.c` | essai : sur PC, joue hwmanager (lance les pilotes par `SYS_MOD_LAUNCH` comme `drivers.txt`), hidmanager (anneaux, événements dits), vfs (montage, puis essai de `blk.usb0` / `blk.usb0p1`) et netd (inscription, requête ARP vers 10.0.2.2) ; sur le Pi, `usbclasstest watch N` s'abonne à hidmanager comme Elegant et dit les événements reçus |

Bizarreries de l'asm gardées : l'intervalle haute vitesse `(1 << ((bInterval-1) & 31)) >> 3` (bInterval 0 donne
un intervalle énorme) ; `hid_parse` lit les données d'un item sans regarder la fin (le message a 16 octets de
plus en C) ; `pad_parse` ne tient pas de position par numéro de rapport (un Report ID met la position à 8) ;
usbstorage compare la longueur du CSW sans signe (une erreur passe et c'est le CSW précédent, resté dans le
tampon, qui est refusé), notifie « est prêt » même si vfs refuse le montage, et ne referme pas les canaux
`blk.usbN` ouverts pour trouver un nom libre ; usbaudio ouvre la capture avec 0 octet par ms ; usbnet n'utilise
pas le numéro d'interface. Une différence voulue : la division d'un axe de manette par -1 avec -2³¹ ne fait pas
de faute en C (comme `sdiv`).

### Outils

* `tools/svc/build_c.sh` : services en C (comme `tools/bin/build_c.sh`, même runtime korinrt) →
  `build/svc/<arch>/<nom>` ; `*_aarch64.c` / `*_x86_64.c` pour une seule machine ; `svc.conf`
  (`import`, `sources`, `arch`).
* `tools/pc/build.sh` : met `build/svc/x86_64/*` dans l'initrd ; `PC_QUICK=1` ne reconstruit pas
  les kits ni les commandes (20 s au lieu de 3 min).
* `tools/pc/qemu_pc.py` : `--type "ligne"` (tapée en AZERTY, plusieurs permis, `--pause`), `--usb`
  (qemu-xhci + usb-kbd), `--usbdisk IMG`, `--usbnet`, `--usbkbd-late` (clavier branché à chaud après
  les lignes : tant qu'un usb-kbd est là, QEMU lui donne toutes les touches et le moniteur PS/2 n'en
  reçoit plus ; `input-send-event` avec `device=` fait planter le QEMU de Windows), `--usbkeys`.
  Sans ces options, rien ne change. Depuis le 12/11 aussi : `--usbmouse` (usb-mouse, id `umouse`),
  `--usbtablet` (usb-tablet, id `utab`), `--late "CMD"` (à la fin, plusieurs permis : « abs X Y » et
  « btn left 1 » par QMP, sinon une commande du moniteur de QEMU : `mouse_move 10 -5`, `device_add
  usb-tablet,bus=xhci.0,id=utab`, `device_del ucle`) ; la clé de `--usbdisk` a l'id `ucle`.
* `src/svc/usbdtest/usbdtest.c` : client du service « usb » sans pilote de classe (liste, clavier
  HID boot, clé Bulk-Only/SCSI, carte réseau), sur la sortie standard ; marche avec l'asm comme avec
  le C (même protocole).

## 3. Vérifié (QEMU q35 + OVMF, 12/11)

* `ktest` 47/47, `ptest` 22/22, `itest` 29/29, `mtest` 19/19, `btest` 10/10, `wtest` 12/12 sur **1, 4 et
  16 cœurs**.
* `pcid -l` : MCFG (ECAM à `0xE0000000`, bus 0–255), les fonctions de q35 (pont hôte, VGA sans mesure,
  ISA, SATA avec MSI 64 bits, SMBus, `1b36:000d` qemu-xhci avec MSI-X 16 et BAR0 64 bits à
  `0xC0_0000_0000`).
* `pcid -n` puis `usbd` (sans argument : le premier xHCI) puis `usbdtest`, sur 1 et 16 cœurs :
  * xHCI version 1.00, 8 ports, 32 slots, **MSI-X** (ligne 256), contrôleur pris à pcid ;
  * usb-storage (port 1, SuperSpeed) et usb-net (port 6, plein débit) énumérés, descripteurs lus,
    interfaces annoncées (hwmanager et busd absents : dit une fois) ;
  * `usbdtest` : les deux périphériques, interfaces et extrémités ;
  * `usbdtest disk` : INQUIRY « QEMU QEMU HARDDISK », TEST UNIT READY, READ CAPACITY 8192 × 512, blocs
    0 et 1 de l'image relus à l'octet près (`55 aa` en fin de bloc 0) ;
  * `usbdtest net` : interface de données 0a/00/00, bulk IN 82, OUT 02 ;
  * clavier **branché à chaud** (port 6 ou 7) : détecté par l'événement de port, énuméré ; `usbdtest
    kbd` : SET_PROTOCOL, SET_IDLE, tuyau, puis les rapports des touches a, b, Maj+c (`04`, `05`,
    `02 … 06`, relâchements), réveils par la MSI-X ;
  * après `ktest` (qui prend et rend deux vecteurs), la MSI-X de usbd doit aller sur le cœur 2 (la
    répartition tourne ; le cœur visé n'est pas affiché) : les touches arrivent pareil (16 cœurs).

QEMU raspi4b (12/11, carte tirée de `build/sdroot`, emplacement 3) : le journal de démarrage montre
`usbd.asm` sur le **DWC2** (« dwc2 : port racine actif », hub à 8 ports, clavier et tablette `0627:0001`),
ce qui confirme qu'aucun xHCI n'y est émulé. `usbdtest` sur cette carte n'a pas pu tourner : telnet
ouvrait des sessions qui finissaient aussitôt (bash, code 129), sur un `build/sdroot` que d'autres
sessions reconstruisaient au même moment ; à refaire (il doit lister les mêmes périphériques que le
journal, par le même protocole que sur PC).

### Pilotes de classe USB en C (12/11)

**PC** (QEMU q35, `qemu-xhci`, 4 cœurs ; `usbclasstest 240`, `pcid -n`, `usbd` tapés au moniteur ; puis
`--usbkbd-late`, `--usbmouse`, `--usbnet`, `--usbdisk`, tablette branchée à chaud, clé retirée à chaud) :
* usbd annonce, `usbclasstest` lance `usbstorage 1 0`, `usbmouse 2 0`, `usbnet 3 0`, plus tard
  `usbkeyboard 4 0` (clavier branché à chaud derrière le hub de QEMU) et `usbgamepad 5 0` (usb-tablet,
  03/00/00) ; « usbkeyboard : peripherique 4 interface 0 : clavier », « usbmouse … : souris »,
  « usbgamepad … : souris » (la tablette lue d'après son descripteur, la lecture des manettes ayant dit non) ;
* clavier (a, b, Maj+c) : `04` enfoncée / relâchée, `05`, `e1` (mods 02), `06`, `e1` relâchée ; souris
  (`mouse_move 10 -5`, bouton) : relative 10 -5, puis boutons 1 et 0 ; tablette : absolue 16000 8000,
  boutons 1 puis 0 ;
* clé : « QEMU QEMU HARDDISK », 8 Mo, montage « USB0 blk.usb0p1 fatfs » demandé à vfs ; `blk.usb0` (16384
  secteurs) et `blk.usb0p1` (14336, à 2048) : INFO, ATTACHER, secteur 0 (`55 aa`, « ARMPI1.0 »), 200
  secteurs de la partition = ceux du disque (deux commandes SCSI), 200 secteurs écrits vers la fin de la
  partition, relus par le disque, données d'origine remises, lecture hors bornes et adresse hors du tampon
  refusées (-22) ; clé retirée (`device_del`) : « démontage usb0 », « usbstorage : cle retiree, volume
  demonte », fin du pilote ;
* usbnet : « configuration ECM choisie » (QEMU présente d'abord RNDIS), « ECM pret, MAC 52:54:00:12:34:56 »,
  inscription « usb0 » (type 3, priorité 10), lien 1 à 100 Mb/s redit toutes les 2 s ; une requête ARP
  mise dans l'anneau d'émission : la réponse « 10.0.2.2 est 52:55:0a:00:02:02 » revient dans l'anneau de
  réception (émission bulk et lecture bulk sans attente du xHCI en C) ;
* essais du noyau inchangés : `ktest` 47/47, `ptest` 22/22, `itest` 29/29, `mtest` 19/19 (`ptest`, `itest`,
  `mtest` par `urun` : un process aux droits du matériel n'a pas de `fork`), `btest` 10/10, `wtest` 12/12
  (par `run`) ; 4 cœurs.

**Pi** (QEMU raspi4b, emplacement 5, cartes tirées de `build/sdroot` : A = pilotes en asm, C = les six pilotes
en C à leur place ; usbd, hwmanager… les mêmes ; script au démarrage par `startup.txt`) :
* mêmes lignes de usbd, hwmanager et des pilotes sur les deux cartes (« usbkeyboard : peripherique 1
  interface 0 : clavier », « usbgamepad : peripherique 2 interface 0 : souris », « usbnet : ECM pret, MAC
  52:54:00:12:34:57 », « usbstorage : support de 8 Mo », montage USB0 -> 0, « usbaudio : USB : QEMU USB
  Audio », « lecture, 48000 Hz ») ;
* clé `USB0:` (image d'essai de 8 Mo) : `ls`, `cat`, copie vers la clé et `diff` (« copie identique »),
  écriture puis relecture, `usbtest` (300 000 octets relus sans erreur, fichier de 225 000 octets, 1 Mo
  écrit et relu, dossier), `eject` : sorties identiques ; les deux images de clé ne diffèrent ensuite que
  de 24 octets, dans les secteurs des dossiers (les heures) ;
* entrées vues par un abonné de hidmanager (`usbclasstest watch`), pendant que QEMU tape « hello »,
  Entrée, Maj+a, Ctrl+b et bouge, clique (gauche, droit) et fait tourner la molette sur la tablette :
  130 événements identiques (touches avec le caractère de la disposition, répétitions, souris absolue,
  boutons, molette) ; un seul de moins en C, une position de tablette répétée pendant la molette (QEMU
  fusionne ses rapports selon le moment) ; captures du bureau : pointeur au même endroit ;
* usbnet : DHCP (10.0.2.15), `ping 10.0.2.2` 4 sur 4, et une session telnet depuis le PC (« echo »)
  passe par l'usbnet en C ;
* usbaudio : `play` d'un WAV d'essai sur la carte son USB de QEMU : dans le fichier WAV de QEMU, le son
  joué dure 38 403 trames (asm) et 38 402 (C), même puissance (RMS 3033 et 3032), même fréquence (~1555 Hz) ;
  les échantillons diffèrent d'une fraction de trame (la phase du rééchantillonnage de sound au départ du
  flux), pas de l'usbaudio, qui recopie les trames telles quelles.

## 4. Pas vérifié, à essayer

Sur le **vrai ZBook** (l'utilisateur ; ne jamais écrire sur le NVMe) :

* `pcid -l` : la MCFG d'HP, les ~30 fonctions, les deux xHCI (`8086:43ED` du PCH, `8086:9A17`
  Thunderbolt 4), le lecteur Realtek `10EC:5261`, l'HDA `8086:43C8`, l'AX201 `8086:43F0` ; le GPU Intel
  doit apparaître sans mesure de BAR (console intacte) ;
* `usbd 0000:00:14.0` (xHCI du PCH, adresse à confirmer par `pcid -l`) : reprise au BIOS (message si le
  BIOS ne rend pas la main), scratchpads (le Tiger Lake en demande), ports à PPC, MSI-X, clé USB et
  clavier USB ;
* les pilotes de classe en C sur le xHCI du PCH : clavier et souris USB (rapports d'un vrai clavier :
  NKRO, rapports numérotés d'un clavier composite), clé USB (secteurs de 512 o ; montage dès qu'il y aura
  vfs sur PC), casque USB plus tard (isochrone) ;
* un hub USB 3 (les ports du ZBook passent souvent par des hubs internes) : **les hubs SuperSpeed ne
  sont pas gérés** (ni en asm) : un périphérique derrière un hub USB 3 n'est vu que par la moitié
  USB 2 du hub, ce qui marche si le hub l'expose.

Sur le **vrai Pi**, les pilotes de classe en C arrivent sur la carte au prochain `build.ps1` (liste
`$svc_c`) : à essayer avec un vrai clavier et une vraie souris USB, une clé USB (lecture, écriture, retrait
pendant le montage), le casque USB (lecture et micro), une manette USB, et une carte réseau CDC-ECM si
l'utilisateur en a une. En cas d'ennui, retirer le nom de `$svc_c` remet l'asm.

Pour usbd sur le **vrai Pi** (rien à faire tant que la carte garde l'asm) : si un jour `build/svc/aarch64/usbd`
remplace `usbd.asm`, essayer d'abord sur un Pi 4 : lien PCIe, microcode du VL805, clavier et clé sur
les ports USB-A ; mais il manque d'abord le DWC2 et l'isochrone en C (sinon le port USB-C et les cartes
son USB disparaissent).

## 5. Reste

1. **Plusieurs xHCI** : le ZBook en a deux ; hwmanager lancerait deux usbd, et le second ne peut pas
   s'inscrire sous « usb » (il s'arrête). À faire : un seul usbd à plusieurs contrôleurs (le second
   lancement passe son adresse au premier, ou usbd prend tous les xHCI de pcid), poignée = contrôleur
   + slot. **Fait le 13/11** (« Souris et USB à deux contrôleurs », à la fin) : le second lancement
   confie sa fonction au premier.
2. hwmanager en C avec `HW_OP_PCI` (§2) ; init2, vfs, busd sur PC, puis la chaîne complète
   init2 → hwmanager → pcid → usbd → usbkeyboard / usbstorage. **Fait** pour hwmanager, busd, init2
   (« Services du système en C », plus bas) ; la chaîne pcid → hwmanager → usbd → pilotes tourne sur PC
   (lancée au moniteur) ; reste init2 au démarrage du PC, quand il aura un disque servi.
3. Pilotes de classe USB en C : **faits** (§2, vérifiés §3). Reste : une vraie manette USB (QEMU n'en a
   pas : `hidpad.c` n'est essayé ni en asm ni en C), la capture de usbaudio (QEMU usb-audio n'a pas
   d'entrée), usbaudio sur PC (il attend l'isochrone d'usbd en C : `E_NOSYS` aujourd'hui, « tuyau
   isochrone refusé »), une clé à secteurs de 4 Ko (refusée, comme l'asm).
4. Isochrone en C (`iso.inc`), hubs SuperSpeed, DWC2 en C (pour que le Pi prenne le C).
5. `IRQ_MSI` sur le Pi (contrôleurs MSI des ponts PCIe), pour que le VL805 et le RP1 ne sondent plus.
6. pcid : ponts sans bus attribué (rien renuméroté aujourd'hui), branchement à chaud du PCIe
   (Thunderbolt), gestion d'énergie (D3 → D0 au `CLAIM`).

Suite (décision de l'utilisateur du 12/11 : pas de réseau par téléphone, le Wi-Fi avant l'Ethernet, qu'il
n'utilise pas) : pilotes de classe USB en C (hiddrv, usbstorage, hidmanager, usbaudio ; usbnet sans NCM ni
RNDIS), un usbd pour les deux xHCI, hwmanager en C, lecteur Realtek `10EC:5261` (démarrage sur carte SD),
**AX201**, HDA (`pci:04/03`, codec ALC285) ; e1000e en dernier, seulement si un réseau sous QEMU devient utile.

**Vrai ZBook, 13/11 (journal `SD0:/journal.txt`)** : le NVMe met ≈ 8,4 s à publier `blk.korin` (d'où `wait blk.korin`
dans `init.txt`) ; le second xHCI (`00:14.0`) : « service usb refusé (-17) », 3 relances puis abandon (point 1
ci-dessus : un seul usbd) ; **rtsx** : « premier tampon de commandes : -123 » (DMA de la puce), abandonné après 3
relances. **Décision de l'utilisateur** : rtsx désactivé dans `drivers.txt` pour l'instant ; à reprendre lancé
**seulement quand une carte est présente ou insérée**, et arrêté **avec son service de fichiers** quand on la
retire. Le défilement de la console était lent (0,25 s par ligne en bas d'écran) : le PAT n'était réglé que sur
le cœur 0 (corrigé : `KORIN_PAT` sur chaque cœur).

**Pour plus tard (demandes de l'utilisateur du 14/11, retours du ZBook)** :
* **Presse-papier** : un clic sur l'applet du panneau affiche l'historique des éléments copiés, comme dans Onyx
  (texte et images ; recoller un élément ancien).
* **Coller une image** par Ctrl+V (Telegram d'abord, puis toute app qui accepte une image) : vérifier que le
  presse-papier de Korin porte bien les images (format, taille), sinon l'ajouter à UIKit.
* **Glisser-déposer depuis le gestionnaire de fichiers vers les apps** : une image déposée dans Telegram part dans
  la conversation ; un fichier déposé sur une app l'ouvre. Principe (l'utilisateur, 14/11) : un simple message à
  l'app avec le chemin du fichier, l'app décide. **Le protocole existe déjà** (`src/inc/ws_ext.inc` : `WS_OP_DRAG`,
  `WEV_DROP`, `WS_OP_DROP_GET` / `ui_drop_get`, genre `UDT_FILES` = chemins, un par ligne) et beaucoup d'apps
  reçoivent déjà un dépôt (Telegram : images, Koton, letters, notes, paint, imageview…). **Ce qui manque** : le
  gestionnaire de fichiers (`src/apps/files`, en C comme en asm) ne lance pas de glisser `UDT_FILES` depuis sa
  liste ; puis vérifier les apps qui n'acceptent pas encore de dépôt.

**Après la validation du C (demande de l'utilisateur du 13/11)** : retirer l'asm remplacé par le C (kits, services,
commandes de `/bin`, interrupteurs `INIT_C`, `FILES_C`, `APPKIT_C` ; PS/2 et moniteur du noyau du PC une fois la
console validée), quand le C est essayé sur le vrai Pi ; selfhost et pibuild doivent alors construire le C par le
GCC de l'OS. Ce qui ne sert qu'au Pi reste en asm.

**Ordre du plan (décisions de l'utilisateur du 13/11)** : bureau Elegant (fait) → apps C/C++ (fait) → apps asm
(hors Onyx : fait ; d'Onyx et émulateurs, avec un JIT AArch64 et un JIT x86-64 : en cours) et KotonStudio →
GCC, FreeBASIC, fasm dans l'OS (fait) → pilote GPU Intel (étape 1 faite ; puis Mesa iris, GlKit, GpuKit ; les apps
qui passent par le GPU, Avalonia et WebGL s'essaient seulement après lui ; Vector aussi, après le GPU ; une fois Mesa
porté sur le PC, n64emu, gcemu et n3dsemu (3DS) affichent par Mesa, et `tools/pc/armgl_none.c` disparaît) → **pilotes du
touchpad, du son et du Wi-Fi, ensemble** (13/11 : touchpad I²C HID de `00:15.0` sur le LPSS d'Intel, son Intel
`04/01` DSP/HDA avec le codec, Wi-Fi AX201 `8086:43f0` ; ordre du Wi-Fi avant l'Ethernet gardé) (faits et validés sur le ZBook le 13/11) → **Jet**
(WebKit, WebGL par GlKit ; avancé avant .NET le 13/11) → .NET, les dll (WinForms, Avalonia, Studio…) (fait le 14/11 sous QEMU : « .NET pour le PC », à la fin ; Avalonia sur le processeur, GPU ensuite) → **puis, ajoutés à la fin le 13/11** : bash ; CPython (avec ses modules
Rust) ; la cible Rust `x86_64-unknown-korin` (faite le 13/11, QEMU seulement : [RUST.md](../RUST.md), « Le PC ») ; **git** (tools/git, sur OpenSSL et zlib du PC ; `git clone` en HTTPS après le
Wi-Fi) et **rg** (tools/rg, en Rust : après la cible Rust), avant Vibe (13/11) ; Vibe et sa façade (Vibe tourne déjà sur le Pi ; l'utilisateur veut
ensuite s'en servir avec **Kimi K3** comme modèle, Vibe acceptant d'autres fournisseurs que Mistral : à faire une fois le
système au point ; dans la façade : un assistant pour ajouter un compte de fournisseur, avec un lien vers sa page de
connexion ouvert dans Jet pour obtenir la clé d'API, et le choix du modèle au démarrage de chaque session) → **serveur web
maison** (décision du 14/11 : pas Apache ; un serveur écrit pour Korin, plus simple à intégrer au système et à
WebStudio ; PHP et .NET (ASP.NET Core)) et **WebStudio**, l'outil de développement web (nom choisi le 14/11 ; HTML, XML,
JavaScript, CSS ; lance le serveur dans le dossier du projet, à la WebMatrix ; aperçu dans Jet ; d'abord une étude de
design par un designer, pour un outil léché, pro et facile : faite le 14/11, [DESIGN-WEBSTUDIO.md](../DESIGN-WEBSTUDIO.md), à valider ; **C++ natif sur UIKit**, choisi par l'utilisateur le 14/11)
(avancés avant SDL le 14/11 ; précisions du 14/11 : **un applet du panneau de configuration** pour le serveur, où l'on ajoute des **sites** à la IIS :
le dossier racine et **plusieurs bindings** par site (nom d'hôte, port, schéma http/https) ; **d'abord sans PHP ni .NET**,
mais avec une interface de gestionnaires (par extension ou par chemin, process ou module) prévue dès le départ pour les
brancher facilement ensuite ; **WebStudio couvre la syntaxe PHP et C#** (Razor/ASP.NET compris), pour faire des sites PHP
et ASP.NET ; **certificats** (14/11) dans l'applet, sur OpenSSL : générer un certificat **auto-signé**, créer une **demande (CSR)**
avec sa clé privée, puis **intégrer le certificat reçu** du fournisseur (et sa chaîne) et l'associer aux bindings https ; **Let's Encrypt** (14/11) : client **ACME** (RFC 8555) dans le serveur, sur OpenSSL : compte ACME
(conditions acceptées par l'utilisateur), défi **HTTP-01** servi par le serveur lui-même (port 80 joignable depuis
Internet, nom de domaine pointant vers la machine), **renouvellement automatique** (certificats de 90 jours, à 30 jours
de la fin), essais sur l'environnement *staging* ; DNS-01 (jokers) plus tard, il demande l'API du fournisseur DNS) → SDL 3 + sdl2-compat avec SuperTux et SuperTuxKart ; le générateur de KotonStudio (fait :
Koton et ses générateurs sont sur le PC) → le GPU NVIDIA pour la 3D, s'il aboutit → **Godot 4** (runtime et
éditeur dans Korin, pour faire des jeux qui ciblent Korin : plateforme `korin` sur UIKit, GlKit et AudioKit, rendu
Compatibility, GDScript ; C# après .NET ; ajouté le 13/11, placé après le GPU NVIDIA) → le noyau du Pi en C (ci-dessous).

**Tout à la fin du plan (décision de l'utilisateur du 13/11)** : le noyau du Pi sur le noyau commun en C
(`src/kernel/core`), avec une couche Pi (GIC, minuterie, MMU AArch64, vecteurs, cache ; ce qui ne sert qu'au Pi
peut rester en asm), comparé sous QEMU puis sur le vrai Pi, vitesse de démarrage surveillée ; ensuite
`src/kernel/*.asm` est retiré.

**Quand le projet sera stable (demande de l'utilisateur du 13/11)** : livrer avec le GCC de l'OS (`/gcc`, sur le Pi et
le PC) les bibliothèques tierces que les ports construisent déjà hors de Korin, en-têtes + `.a` (et `.so` quand
dlopen/`-shared` s'y prête), avec un `.pc` de pkg-config pour chacune, afin qu'une app compilée **dans Korin** puisse
s'en servir comme des kits (déjà livrés : libposix, les kits `include/kits` + `lib/lib<kit>.a`, libglkit.a, les `.bi`
de FreeBASIC). À livrer : zlib, OpenSSL, curl, SQLite, libffi, FreeType, HarfBuzz, libpng, libjpeg-turbo, libwebp,
Skia, libvpx, dav1d, Opus, Mesa (EGL, GLES), SDL 3 (+ sdl2-compat) quand il sera porté, et ce que les ports suivants
ajouteront (OpenCASCADE…). Une version par bibliothèque, en deux paquets séparés du paquet de GCC (décision du 13/11) : **`lib<nom>`**, binaire, avec
les `.so` seulement (ce que demandent les apps installées) ; **`<nom>-dev`**, avec les `.a`, les en-têtes et le `.pc` (pour
compiler dans Korin, dépend de `lib<nom>`) ; et une seule construction partagée par les ports (Jet, git, Python…) pour ne pas en avoir
plusieurs copies divergentes.

**CAO native (demande de l'utilisateur du 13/11, à la fin du plan, après les bibliothèques partagées)** : une app de CAO
libre aussi facile que Fusion ou Shapr3D, successeur de 3DForge d'Onyx (`docs/3dforge/README.md` du dépôt Onyx), dont on
garde l'interface validée (gestes clic-déplacer-clic, cotes modifiables sur le dessin, corps flottants, timeline,
mode Fabrication en G-code GRBL : surfaçage adaptatif, contour 2D) et dont on change le moteur, cause de ses limites
(congés seulement sur arêtes droites ou circulaires, pas de STEP, esquisse sans solveur) : **OpenCASCADE** au lieu de
Manifold (congés et chanfreins sur toutes arêtes, rayon variable, coques, dépouilles, balayages, lissages, STEP), 
**PlaneGCS** (le solveur d'esquisse de FreeCAD, LGPL) pour de vraies contraintes, une vraie timeline paramétrique
(point délicat : références stables des arêtes et faces). App C++ sur UIKit et GlKit, jetons du thème, Pi et PC,
tactile d'abord. Préalable : OpenCASCADE construit pour Korin (paquets `libocct` / `occt-dev`). D'abord une étude
de design (maquettes validées par l'utilisateur), comme pour 3DForge.

**Couche de compatibilité Linux, pour Claude Code (décision de l'utilisateur du 13/11)** : faire tourner le binaire
officiel **non modifié** de Claude Code (version musl, `claude-code-linux-x64-musl`, puis arm64 sur le Pi), connecté
par l'abonnement de l'utilisateur, sans clé d'API ; revient sur l'écart de la couche du 11/11 (voie jugée la moins
intrusive et la moins coûteuse, pour être autonome dans Korin). Forme retenue : une **personnalité Linux par
process** ; le noyau ne fait que renvoyer les `syscall` du code de l'app (façon « Syscall User Dispatch ») vers
**`liblinux.so`**, runtime préchargé comme un kit et lié au process quand la personnalité est activée (ELF Linux
reconnu à l'`exec`, ou `linux <programme>`, ou `app.txt`), qui traduit et exécute par libposix ; ses appels à elle
(sa plage d'adresses) partent comme des appels de Korin. Un kit **`TuxKit`** (table `.abi`) pour les programmes de
Korin : lancer un programme Linux, la vue Linux des fichiers, les paquets Alpine. À ajouter : epoll, eventfd,
signalfd, timerfd, inotify, pidfd, `futex` dans le noyau, `clone` des threads, `mprotect` sur un mapping de fichier,
trame des signaux de Linux, netlink minimal, `/proc` et `/dev` synthétisés, vue `/lib` `/usr` (musl d'Alpine)
`/tmp` `/home/user` `/etc`. Pièges : `FS`/`TPIDR_EL0` appartient à l'app (liblinux a son propre espace de thread),
adresse de liblinux hors de ce que l'app demande. Étapes : busybox statique → programmes dynamiques d'Alpine et
`apk` → Node.js et Bun officiels → Claude Code (`xdg-open` → Jet pour la connexion ; son outil Bash lance le bash de
Korin). D'abord l'étude (`docs/etude/LINUX-COMPAT.md`) ; lire les conditions d'Anthropic ; couper la mise à jour
automatique ou la faire passer par la couche.

## Touchpad (décision de l'utilisateur du 12/11)

Le touchpad du ZBook (ELAN en I2C-HID, bus I2C LPSS d'Intel, décrit par l'ACPI) aura son pilote **après le reste**
(services, Wi-Fi, son, GPU). Morceaux : hôte I2C DesignWare (pcid), ACPI (adresse, GPIO d'interruption ; ACPICA),
HID sur I2C, puis les rapports HID décodés par le même code C que les pilotes HID de l'USB. **Les gestes
s'inspirent du pilote de l'écran tactile** (`src/svc/gt911`, gestes repris d'Onyx `sys/touch.cpp` : un doigt
glisse ou reste posé → glisser, deux doigts → molette, appui long, délais `TP_*`), pour que le touchpad et l'écran
tactile se comportent pareil. D'ici là : souris USB externe (`usbmouse`), et peut-être le port souris PS/2 si le BIOS
du ZBook y présente le touchpad (à essayer sur la machine).

**Glisser au touchpad** (décision du 12/11) : un **double toucher** (« double clic » du touchpad) commence le
glisser : un toucher, puis le doigt reposé aussitôt et gardé posé → bouton gauche appuyé ; le doigt bouge l'objet ;
le lever relâche le bouton. Un toucher simple reste un clic ; les autres gestes viennent de gt911 (deux doigts →
molette).

**Relevé sous Windows** (12/11) : un seul appareil, `ACPI\ELAN0750` (« I2C HID Device »), sur le contrôleur I2C
d'Intel `8086:43E8` (Serial IO I2C Host Controller), avec cinq collections HID :

| Collection | Rôle |
|---|---|
| `COL03` « HID-compliant touch pad » | touchpad de précision : position de chaque doigt, et les 3 boutons sous le touchpad |
| `COL01` « HID-compliant mouse » | le même touchpad en souris simple (repli) |
| `COL05` « ELAN PointStick Device » | le pointeur du clavier, en souris, avec les 3 boutons au-dessus du touchpad |
| `COL02` (propre à ELAN), `COL04` (Input Configuration) | réglages, passage d'un mode à l'autre |

Les 6 boutons arrivent donc dans les rapports HID de ce même appareil (numéro de rapport par collection) : le pilote
lit le descripteur de rapports, puis rend des clics ordinaires à hidmanager. Le clavier interne est vu deux fois : en
PS/2 (`ACPI\HPQ8002`) et, pour les touches spéciales, en HID par `INTC816`.

## À la fin du plan : évaluer un pilote NTFS (demande de l'utilisateur du 12/11)

**Tâche de la toute fin du portage** (précision de l'utilisateur du 12/11) : seulement quand la version PC de Korin fonctionnera.

**But final (12/11)** : démarrer aussi bien Windows que Korin **sur la même partition** NTFS du NVMe. Korin y vit
dans un dossier (par exemple `\korin`), et une **option de configuration de vfs**, facultative, fait de ce dossier la
racine du volume système : `SD0:/` montre `<volume NTFS>:/korin` (une sorte de chroot du volume). Conséquences à
prévoir :

* le pilote NTFS devra **écrire** (fichiers du système, réglages, paquets), pas seulement lire : l'écriture sûre
  (journal `$LogFile`, allocation, index) devient nécessaire ;
* l'option de vfs : un réglage lu au démarrage (avec `mounts.txt` ou à côté), qui donne le volume réel et le dossier
  racine ; les chemins qui sortiraient du dossier (`..` à la racine) restent dedans ; à rapprocher des idées de
  montage dans un dossier et d'UnionFS (mémoire du dépôt) ;
* le démarrage : `BOOTX64.EFI`, le noyau et l'initrd sur la partition système EFI (FAT32), choisis au menu de démarrage
  de l'UEFI à côté de Windows ; l'initrd monte ensuite le NTFS ;
* Windows doit laisser le volume propre : **démarrage rapide et veille prolongée éteints** (sinon NTFS est « sale » et
  Korin doit refuser d'écrire) ; **BitLocker** : déchiffrer et chiffrer à la volée en AES-XTS, ou partition non
  chiffrée (choix de l'utilisateur) ;
* tant que l'utilisateur ne l'a pas décidé explicitement, **aucune écriture sur le NVMe** : les essais se font sur
  des images NTFS sous QEMU, puis sur une clé ou un disque externe en NTFS.

**Le dossier racine d'un volume : un paramètre `rootpath` de plus au montage** (conception de l'utilisateur, 12/11 ; **fait** : voir « Fait (12/11) » plus bas).
**Priorité (12/11) : ce chantier (`rootpath`, `BOOT:`, clé `boot =` des paquets, carte double) passe **avant**
NTFS, dès que vfs en C est vérifié sur le Pi ; il sert déjà aux premiers essais du ZBook sur la même carte SD que le Pi.**
Aujourd'hui, un volume se monte par « volume, source, service » à trois endroits ; on y ajoute un quatrième
paramètre facultatif, le dossier de la source qui devient la racine du volume :

| Où | Aujourd'hui | Avec `rootpath` |
|---|---|---|
| démarrage (`init.txt` de l'initramfs) | `vfs SD0 blk.sd0p1 fatfs` | `vfs SD0 blk.nvme0p1 ntfs /korin` |
| `SD0:/etc/mounts.txt` | `SD1 = blk.sd0p2, fatfs` | `SD1 = blk.nvme0p1, ntfs, /data` (par exemple) |
| commande `mount` | `mount VOLUME: [source [service]]` | `mount SD0: blk.nvme0p1 ntfs /korin` |

Exemple : `vfs SD0 blk.nvme0p1 ntfs /korin` démarre Korin sur la partition 1 du NVMe vue comme `SD0:`, avec
le dossier `/korin` de cette partition comme racine : `SD0:/sys/elegant` est le fichier `\korin\sys\elegant` de
Windows. **Paramètre omis ou « / » : le fonctionnement actuel.** La racine est appliquée par vfs (la résolution
des chemins préfixe `rootpath` avant d'envoyer au service FS, et un `..` à la racine reste à la racine), donc elle
marche avec tous les services de fichiers (fatfs, ntfs, ramfs), pas seulement NTFS. À faire dans vfs en C (lot des
services de fichiers), une fois vérifié sur le Pi ; utile aussi sans NTFS (un volume FAT32 ou exFAT partagé).

**Une carte SD pour les deux machines** (idée de l'utilisateur, 12/11), grâce à `rootpath` : la partition 1 (FAT32)
porte les deux systèmes, chacun dans son dossier, et chaque initrd monte le sien comme racine de `SD0:`.

| Sur la partition 1 | Pour qui |
|---|---|
| à la racine : `kernel8.img`, `config.txt`, `start4.elf`, DTB, modules de démarrage (`*.mod`) | le firmware du Pi (il ne lit que la racine) |
| `EFI/BOOT/BOOTX64.EFI`, `korin/kernel.elf`, `korin/initrd.tar` | l'UEFI du PC (chemin des supports amovibles) |
| `/korinARM` | le système du Pi : `init.txt` de son initramfs : `vfs SD0 blk.sd0p1 fatfs /korinARM` |
| `/korinPC` | le système du PC : son initrd : `vfs SD0 blk.<lecteur>p1 fatfs /korinPC` |
| partition 2 (exFAT, `SD1:`), racine « / » | les données de l'utilisateur, **communes** aux deux machines |

Points à régler le moment venu :
* **les fichiers de démarrage restent hors de la racine de `SD0:`** : ce qui les écrit aujourd'hui (`config.txt` par
  les Réglages et la résolution à chaud, le noyau et les modules par `pkg` et `selfhost`, `setup`) doit viser la vraie
  racine de la partition : un second volume sur la même partition avec `rootpath` « / » (par exemple `BOOT:`) ;
* **un seul service fatfs pour les deux volumes** d'une même partition (vfs envoie au même service avec deux
  préfixes), sinon deux caches de fatfs écriraient la même FAT sans se voir ;
* la taille de la partition 1 : les deux systèmes complets (étape Disque de `setup`, qui agrandit la FAT au montage) ;
* la construction : `build.ps1` saurait faire cette carte double (fichiers du Pi à la racine et `/korinARM`, ceux du PC
  dans `EFI/`, `korin/` et `/korinPC`) ; le chemin `/etc/…`, les chemins virtuels (`/bin` → `SD0:/bin`) et les
  paquets ne changent pas, puisque chaque système voit sa propre racine.

**Le volume `BOOT:` et le gestionnaire de paquets** (remarque de l'utilisateur, 12/11). Le paquet `armpi`
(`tools/pkg/packages.ini`) installe aujourd'hui `kernel8.img`, `kernel_2712.img`, `*.mod` et `config.txt` à la racine
de `SD0:`, et `pi-firmware` y met `start4.elf`, `fixup4.dat`, les DTB, `overlays/` et `firmware/` ; avec un
`rootpath`, ils finiraient dans `/korinARM` où le firmware ne les lit pas. D'où :
* **`BOOT:` toujours monté** : la vraie racine de la partition de démarrage (`rootpath` « / », même service fatfs que
  `SD0:`) ; sur une carte sans `rootpath`, `BOOT:` et `SD0:` montrent la même chose ;
* **une clé `boot =` dans `packages.ini`** : les fichiers d'un paquet qui vont dans `BOOT:/` (noyaux, modules de
  démarrage, `config.txt`, firmware, DTB ; sur PC `EFI/BOOT/BOOTX64.EFI` et `korin/`) ; `pkg` (`src/lib/pkg/pkglib.h`,
  qui traite déjà `kernel8.img` à part) les écrit dans `BOOT:/`, le reste dans `SD0:/` ; `mkrepo.py --boot` sait déjà
  ce qui va à la racine ;
* **les autres écrivains de la racine** passent aussi par `BOOT:` : les Réglages (`config.txt`, résolution à chaud),
  `setup` (étape Disque), `selfhost` et `pibuild` (noyau, modules), `mkinitramfs` ;
* sur la carte double, les paquets du Pi et ceux du PC ont des fichiers de démarrage différents (`kernel8.img` contre
  `EFI/BOOT/BOOTX64.EFI`) : pas de collision ; seul un fichier commun aux deux (aucun aujourd'hui) poserait problème.

**Fait (12/11) : `rootpath`, `BOOT:` à la demande, clé `boot =`** (détail : [FICHIERS.md](../FICHIERS.md), 4.4 ;
[PAQUETS.md](../PAQUETS.md)). Changement de l'utilisateur en cours de route : `BOOT:` **n'est plus toujours
monté** ; il se monte à la demande, le temps qu'un programme écrive un fichier de démarrage, puis se démonte (sur le
PC, ce sera la partition EFI de Windows, qu'on ne laisse pas ouverte). Il est décrit par une ligne
`BOOT = source, service, rootpath, ondemand` de `SD0:/etc/mounts.txt` ; sans elle, les fichiers de démarrage vont à
la racine de `SD0:` comme avant.

* vfs en C : quatrième mot `rootpath` (init.txt, `mounts.txt`, `VFS_MOUNT`), appliqué en envoyant au service
  (`vol_chemin`), jamais montré ; deux volumes d'une même partition et d'un même service partagent le service et
  son cache ; « `BOOT:` » est un vrai nom de volume ; ops 49 `VFS_VOLINFO`, 50 `VFS_BOOT_BEGIN`, 51 `VFS_BOOT_END`.
* korinrt : `vu_mount_root`, `vu_volinfo`, `vu_boot_begin` / `vu_boot_end`, `vu_source` lit le rootpath ; `mount`
  (`mount VOLUME: source service rootpath`, la liste montre le rootpath), `df` / `eject` (le service d'un volume
  partagé), `mkfs` (refuse une partition qui sert un autre volume), `mkinitramfs` (modules et `config.txt` dans
  `BOOT:`).
* paquets : clé `boot =` (`packages.ini` : `armpi`, `pi-firmware`), `mkrepo.py` (manifeste, base, contenu),
  `pkglib.h` (installation, retrait, commit vers `BOOT:/`) ; versions montées : armpi 2026.11.30, pkgman 1.0.8,
  pi-firmware 1.0.3, korin-srcs 1.0.12 (rien de publié).
* outils : `SVC_OUT` / `BIN_OUT` (`tools/svc/build_c.sh`, `tools/bin/build_c.sh`), `PKG_OUT` (`tools/pkg/build.sh`),
  `--esp` (`tools/pc/qemu_sd.py`) : des essais sans toucher aux binaires de `build.ps1`.

**Reste pour la carte double** : `build.ps1` qui la construit (fichiers du Pi à la racine, `/korinARM` et
`/korinPC`, l'initramfs du Pi avec `vfs SD0 blk.sd0p1 fatfs /korinARM`, l'initrd du PC avec le sien, et dans chaque
`mounts.txt` la ligne `BOOT = blk.sd0p1, fatfs, /, ondemand`) ; les paquets du PC (`boot = EFI/BOOT/BOOTX64.EFI
korin/`) ; le vrai Pi (rien d'essayé sur le matériel) ; sur le PC, `BOOT:` sur la partition EFI quand Korin vivra sur
NTFS (une autre source que `SD0:` : un second fatfs, arrêté à la fin) ; `uname -v` (date de `SD0:/kernel8.img`,
absente avec un rootpath) ; `vfs.asm`, `mount.asm`, `mkinitramfs.asm` (selfhost) n'ont rien de tout cela ; `eject`
ne démonte que les clés USB (un `BOOT:` monté à la main reste monté).

**Vérifié (12/11)** :

* **Pi, carte normale** (QEMU raspi4b, emplacement 2, cartes tirées de `build/sdroot`, supprimées ensuite) : la
  suite de `tools/svc/fs_compare.sh` (`fstest`, `fstest2`, `vfstest`, `exfattest`, `cachetest`, `directtest`,
  `ramtest`, `aktest -f`, `posixtest file io exec fork ipc`…) puis toutes les lignes des `test.txt` de `src/bin`
  (2001 essais), services et commandes C d'avant contre ceux-ci : mêmes sorties et mêmes codes, sauf les temps, la
  taille de `vfs` vue par `vfstest`, les générations des pid, deux numéros de fichier ouvert de `fstest2`
  (4 et 5 au lieu de 5 et 6 : un fichier ouvert de plus chez fatfs à ce moment, pas expliqué, aucun échec), et les
  lignes de `mount` qui changent exprès (usage avec `rootpath`) ; démarrage : SD0, RAM0, IMG0, SD1 montés, pas de
  `BOOT:`.
* **Pi, carte double** : tout le système dans `/korinARM`, noyau, modules, `config.txt` et `overlays/` à la racine,
  `init.txt` de l'initramfs `vfs SD0 blk.sd0p1 fatfs /korinARM` ; `mounts.txt` : `BOOT = blk.sd0p1, fatfs, /,
  ondemand` et `DATA = blk.sd0p1, fatfs, /korinARM/home`. Bureau complet (capture). Par telnet : `pwd` → `SD0:/`,
  `cd ..` reste à `SD0:/`, `ls SD0:/` montre le système (pas `kernel8.img`), `/bin` et `/etc` (chemins virtuels)
  justes, `SD0:/..` inaccessible, `vfstest` 0 échec ; `mount` montre `/korinARM` et `DATA:` sur le même service
  (aucun second fatfs), `ls DATA:/` = `home`, `df DATA:`, `mkfs -y DATA:` refusé ; `pkg add bootdemo` (dépôt local
  signé par une clé d'essai) : `bootdemo.txt` dans `BOOT:/`, `etc/bootdemo.ini` dans `SD0:/etc`, `boot =` dans la
  base, `BOOT:` monté puis démonté (« vfs : montage BOOT -> 0 », « BOOT (fin) ») ; `mkinitramfs` : `bootdemo.mod` et
  la ligne `initramfs` de `BOOT:/config.txt`, `BOOT:` rendu de même ; `mount BOOT:` à la main : la racine de la
  partition (`korinARM`, noyau, modules) ; `pkg delete bootdemo` retire `BOOT:/bootdemo.txt` et
  `SD0:/etc/bootdemo.ini`. À chaud : `mount USB6: blk.img0p1 fatfs /rp` sur l'image d'essai (service d'`IMG0:`
  partagé), `ls`, `cat`, `cd ..` à la racine, `df`, `mv IMG0:/rp/f.txt USB6:/g.txt` (rename entre deux volumes d'un
  même service) ; `/absent` → « pas de tel volume », `/../x` → « invalide », un fichier → « pas un dossier ».
* **PC** (QEMU q35 + OVMF, `sdhci-pci` + carte d'essai de 64 Mo, `tools/pc/qemu_sd.py --esp` sur une copie de
  `build/pc/esp` avec vfs, fatfs, mount… de cet essai, sous `build/pc.lock`) : `hwmanager`, `pcid` (sdhci), `run vfs
  SD0 blk.sd0p1 fatfs /korinPC` : `ls SD0:/`, `cat`, `ls SD0:/sub` dans `/korinPC`, `SD0:/..` inaccessible ;
  `DATA = blk.sd0p1, fatfs, /data` de `mounts.txt` monté sur le même service ; `mkinitramfs` monte `BOOT:` à la
  demande (« montage BOOT -> 0 »), écrit `a.mod` à la racine de la partition, le rend à sa fin (« BOOT (fin) ») ;
  `mount BOOT:` : `a.mod`, `data`, `korinPC`, `racine.txt`.

**Pas vérifié** : le vrai Pi et le vrai ZBook ; `pkg commit` d'un paquet étagé vers `BOOT:` (seule l'installation
directe et le retrait sont essayés) ; pkgd et pkgman reconstruits (même `pkglib.h`) ; un `BOOT:` sur une autre
partition que `SD0:` (second fatfs arrêté à la fin) ; le paquet `armpi` complet avec sa clé `boot =`.

Questions à trancher :

* **Pour quoi faire** : lire les fichiers de la partition Windows du NVMe depuis Korin (documents, médias), en
  lecture seule d'abord. L'écriture sur le NVMe reste **interdite** tant que l'utilisateur ne l'a pas décidé
  explicitement (il porte Windows et BitLocker).
* **Ce qu'il faut d'abord** : un pilote NVMe en C (lecture), et le **déchiffrement BitLocker** si la partition est
  chiffrée (clé de récupération ou mot de passe donnés par l'utilisateur ; AES-XTS, format FVE documenté par
  dislocker et libbde, sous GPL et LGPL : comme cartes seulement).
* **Le format** : MFT, attributs résidents et non résidents, listes d'extensions (runlists), index B+ des dossiers
  (`$I30`), noms longs Unicode et `$UpCase`, fichiers compressés (LZNT1), creux et chiffrés (EFS : non), liens
  physiques, points de jonction et liens symboliques (Korin n'en a pas : à montrer comme fichiers spéciaux), flux
  nommés (ADS : à ignorer d'abord), journal `$LogFile` (en lecture : refuser si le volume n'a pas été fermé
  proprement, comme le fait ntfs-3g, ou lire quand même).
* **Références** : la documentation de Microsoft (ce qui est public), le projet Linux-NTFS (« NTFS Documentation »,
  libre), ntfs3 et ntfs-3g (GPL : cartes seulement, sans copie). Licence du code de Korin : la sienne.
* **Taille estimée** : lecture seule sans compression ≈ 3 000 à 4 000 lignes de C (service FS comme fatfs, même
  protocole avec vfs) ; compression LZNT1 ≈ 300 ; écriture sûre (journal, allocation, mise à jour des index) :
  beaucoup plus, et risquée.
* **Essais** : sous QEMU avec des images NTFS faites par Windows (`diskpart`, VHD) ou mkntfs, jamais sur le vrai
  disque ; comparaison des arbres de fichiers et des sommes avec Windows.
* **Verdict à rendre** : faisabilité, coût, ordre (après le Wi-Fi, le son, le GPU, le touchpad), et lecture seule
  ou lecture et écriture.

## Carte SD : lecteur du PC (Realtek RTS5261), SDHCI, couche commune en C (12/11)

But : que Korin lise et écrive lui-même la carte SD de démarrage du ZBook (le chargeur UEFI n'y lit que le
noyau et l'initrd) et la monte en `SD0:`, comme sur le Pi.

### Décisions suivies

* Pilotes en services, lancés par hwmanager d'après `drivers.txt` (clés `pci:`) ; ce qui sert au Pi et au
  PC est écrit une fois en C ; **le Pi garde `src/svc/sd/sd.asm`** (rien n'a pu vérifier le C sur le Pi) ;
  le protocole `blk.*` (`src/inc/blk.inc`) ne change pas.
* Trois couches séparées (`src/svc/sdmmc/sdmmc.h`) :

| Fichier | Rôle |
|---|---|
| `src/svc/sdmmc/sdcard.c` | protocole de la carte (spécification simplifiée publique de la SD Association) : CMD0, CMD8, CMD55/ACMD41 (HCS), CID (décrit), RCA, CSD (capacité v1/v2), CMD7, CMD16 (SDSC), SCR (ACMD51), bus 4 bits (ACMD6), haute vitesse (CMD6 question puis bascule, retour à 25 MHz si le premier bloc ne se relit pas), adresse en octets (SDSC) ou en blocs (SDHC/SDXC), CMD17/18/24/25 par morceaux, CMD13 après chaque écriture (état « tran »), sur une erreur : reprise du contrôleur, CMD12, CMD13, un second essai ; retrait et remise |
| `src/svc/sdmmc/blkserv.c` | le service des blocs repris de `src/svc/inc/blkserv.inc` : `blk.sd0`, `blk.sd0p1`… (MBR : p1-p4, logiques p5-p12 ; carte sans table : disque seul), `BLK_INFO`, `ATTACHER`, `LIRE`, `ECRIRE`, `DMA`, `LIRE_D`, `STATS`, `UHS` ; un thread par périphérique, appels au matériel sérialisés ; **en plus** : GPT (MBR protecteur `0xEE`), `LIRE_D` par la fenêtre de l'appelant (`SVC_MAP_CALLER`), support retiré (`E_IO`) puis remis (table relue, fenêtres mises à jour, nouvelles partitions enregistrées) ; premier nom libre `sdN` |
| `src/svc/sdhci/sdhci.c` | hôte SDHCI (spécification publique « SD Host Controller Simplified », v4.20) : mots de 32 bits seulement (comme l'EMMC2 du Pi l'exige), diviseur de 10 bits (v3+) ou puissance de 2 (v1/v2), PIO et ADMA2 (descripteurs de 32 ou 64 bits), Auto CMD12, DMA pris après un essai (64 blocs lus en PIO puis en DMA, identiques), droit dans le tampon du client si son adresse physique est atteignable, sinon par un tampon de 128 Ko ; retour au PIO sur une erreur ; MSI par `IRQ_WAIT` si la machine en donne, sinon sondage |
| `src/svc/sdhci/plat_x86_64.c` | PC : fonction de pcid (classe `08/05`, interface 00 ou 01), BAR de l'emplacement 0, MSI/MSI-X par `pcilib` |
| `src/svc/sdhci/plat_aarch64.c` | Pi, **jamais essayé** : EMMC2, Arasan, BCM2712 essayés comme `sd.asm`, horloge du micrologiciel, ligne 158 partagée, pas de coupure d'alimentation, DMA par le tampon non caché seulement |
| `src/svc/rtsx/rtsx.c` | hôte Realtek PCIe (PC seulement) : voir plus bas ; **jamais essayé** |
| `src/svc/blktest/blktest.c` | client d'essai de `blk.*` sans vfs (voir « Vérifié ») |
| `tools/pc/qemu_sd.py` | QEMU q35 avec `sdhci-pci` et `sd-card` (lecteur « sdcard »), étapes `type:`, `hmp:` (retrait : `eject -f sdcard`, remise : `change sdcard IMAGE raw`), `sleep:` ; réutilise `qemu_pc.py` sans le modifier |

* `skel/sd0/etc/drivers.txt` : `pci:08/05 = SD0:/sys/svc/sdhci`, `pci:10ec:5261 = SD0:/sys/svc/rtsx`.
* Nommage : comme le Pi, le disque est `blk.sd0` et la partition 1 `blk.sd0p1` ; `SD0:` = `blk.sd0p1` (la
  carte faite par `tools/pc/sd.ps1` ou formatée par Windows a une table MBR et une partition FAT32 :
  `EFI\BOOT\BOOTX64.EFI`, `korin\…`). `mounts.txt` n'a rien à changer : sur le Pi, `SD0` vient des arguments
  de vfs donnés par init (« `SD0 blk.sd0p1 fatfs` ») ; sur PC, init devra lancer le pilote de la carte (ou
  hwmanager → pcid → rtsx) avant vfs, avec la même ligne. La ligne `SD1 = blk.sd0p2` échouera simplement sur
  une carte du PC à une seule partition.

### Le lecteur Realtek (`src/svc/rtsx/rtsx.c`)

Ce n'est pas un SDHCI. Références : **rtsx(4) d'OpenBSD** (licence ISC, lu et repris : accès aux registres
internes par `HAIMR`, tampon de commandes `HCBAR`/`HCBCTLR`, ADMA `HDBAR`/`HDBCTLR`, `BIPR`/`BIER`, noms des
registres de `rtsxreg.h`) ; **rtsx_pci de Linux** (GPL : seulement comme carte de ce qui est propre au
5261, aucun code copié) ; aucune documentation de Realtek.

**OpenBSD ne gère pas le 5261** : `rtsx_pci.c` ne connaît que RTS5209, 5227, 5229, 522A, 5249, 525A,
RTL8402, 8411, 8411B. Le pilote a donc deux profils : « 5261 » (d'après la carte de Linux) et « OpenBSD »
(les autres puces, d'après rtsx(4), pas essayé non plus).

Propre au 5261 (d'après Linux `rts5261.c`, `rtsx_pcr.c`, `rtsx_pci_sdmmc.c`) :

| Point | 5261 | anciennes puces (OpenBSD) |
|---|---|---|
| BAR | BAR0 (le 525A : BAR1) | BAR0 |
| démarrage | `FW_CFG1` (0xFF55) : horloge du MCU non coupée ; SSC allumé par `0xFF60` | `FPDCTL` (0xFC00) ; PHY 0x00 = 0xBA42 |
| alimentation | `LDO1_CFG1` (0xFF73) à 3,3 V, `LDO1233318_POW_CTL` (0xFF70) : LDO1 puis LDO3318, 20 ms ; `CRC_DUMMY_0` bit 0 ; coupure : les 4 LDO | `CARD_PWR_CTL` en deux temps et `PWR_GATE_CTRL` |
| signaux 3,3 V | `CARD_PWR_CTL` PUPDC, PHY 0x08 bit 9, `DV3318_CFG` (0xFF71), `SD_PAD_CTL`, sortance `0xFD5A/5E/5F` (table de Linux, indice des réglages du fabricant) | `SD30_DRIVE_SEL` |
| tirages | `CARD_PULL_CTL2/3` = AA/E9 (coupés : 55/D5) | CTL1-3 |
| horloge | SSC = 2 × la carte ; départ 30 MHz / 128 (60 / 256 après la version C) ; n = f − 4, doublé par diviseur tant que < 76, **puis n / 2 − 1** dans `SSC_DIV_N_0` ; profondeurs 5261 (512K → 4, puis − diviseur) ; mcu = 125 / f + 3 | table 400 kHz / 25 / 50 MHz |
| OCP | `LDO1_CFG0` (0xFF72) : OCP et limite, seuil 1040 mA, limite 2000 mA, filtre 800 µs ; `OCPCTL` ; `OCPSTAT` lu après l'alimentation et à chaque tour de surveillance (surintensité : carte coupée) | — |
| descripteurs ADMA | adresse << 32, **longueur << 16**, attributs | longueur << 12 |
| arrêt | `DMA_RST_CTL_0` (0xFEBF) bits 7 et 6 | `DMACTL` bit 7 |
| divers | réglages du fabricant en configuration PCI 0x724/0x814 (prise inversée → `PETXCFG` 0x30) ; `PRSNT#` relâché (`AUTOLOAD_CFG4`) ; `FW_STATUS` (0xFF56) : SD Express possible (jamais utilisé) | — |

Pas fait (volontairement) : efuse, LTR, L1 sub-states, RTD3, 1,8 V/UHS, SD Express, accord des phases.
ASPM coupé (lien PCIe et `ASPM_FORCE_CTL`), `CLKREQ#` tenu bas (horloge PCIe toujours là), D3 → D0 si
besoin. La puce n'a que 32 bits d'adresse : tampon de commandes (1 Ko), descripteurs et tampon de 128 Ko pris
par `DMA_ALLOC` et vérifiés sous 4 Go (sinon le pilote s'arrête en le disant). Lectures : une liste
(commande, longueurs, DMA, `AUTO_READ_2`) puis l'ADMA, fin attendue par `TRANS_OK`, puis `DMA_DONE` vérifié ;
écritures : la commande, puis `AUTO_WRITE_3` et l'ADMA ; CMD12 à la main après plusieurs blocs ; CMD6 et
ACMD51 par le tampon ping-pong.

Options : `-d` diagnostic **sans rien écrire** dans la puce (registres et configuration, puis fin) ;
`-t` traces de chaque étape avec les registres (et `-v`) ; `-tt` en plus chaque écriture de registre dont la
relecture diffère ; `-v` chaque commande SD ; `-r` lecture seule ; `-n NOM`.

### Vérifié (12/11)

**PC, QEMU q35 + OVMF, `sdhci-pci` + `sd-card`** (`tools/pc/qemu_sd.py`, images de `tools/mkdisk.py`,
supprimées ensuite), avec `pcid -n` puis `sdhci`, puis `blktest` :

* carte de 64 Mo (SDSC, contrôleur version 2, capacités de QEMU `057834b4`) : identification (CMD8, ACMD41,
  CID « QEMU! », CSD v1 : 131 072 secteurs, SCR), bus 4 bits, CMD6 : **50 MHz** ; ADMA2 32 bits pris après
  l'essai (sans MSI : `sdhci-pci` de QEMU n'en a pas, sondage) ; `blk.sd0` et `blk.sd0p1` (secteurs 2048 à
  131 071) ;
* carte de 4 Go (SDHC, contrôleur version 3 avec `capareg=157834b4`, diviseur de 10 bits) : CSD v2 :
  8 388 608 secteurs, **ADMA2 64 bits**, 50 MHz ;
* `blktest all -w` sur les deux, **0 échec** : 2048 secteurs d'un coup = les mêmes lus par 1, 2, 7, 8, 255,
  256, 257, 1000 ; au-delà de la fin refusé ; dernier secteur lu ; `blk.sd0p1` commence au secteur 2048 du
  disque (55 aa) ; `BLK_LIRE_D` (300 secteurs) identique ; écritures de 1, 2, 8, 255, 256, 257, 1024, 2048,
  4096 secteurs relues identiques puis contenu d'origine remis ; ≈ 125 Mo/s en lecture par 1 Mo, ≈ 67 Mo/s
  en écriture (QEMU) ; 5 220 commandes ;
* PIO forcé (`sdhci -p`) : mêmes essais justes (≈ 1 Mo/s sous QEMU) ;
* **retrait** (`eject -f sdcard`) : « carte retirée », `blk.sd0` en erreur (`E_IO`) ; **remise**
  (`change sdcard …`) : carte ré-identifiée, table relue, `blktest all` de nouveau sans échec (il fallait
  acquitter « carte retirée » dans l'état des interruptions : QEMU attend ce bit pour remettre la carte) ;
* **chaîne complète** : `hwmanager` (C, `drivers.txt` de l'initrd), puis `pcid` : « pci 0000:00:02.0
  (08/05/01 1b36:0007) -> SD0:/sys/svc/sdhci » ; puis `vfs SD0 blk.sd0p1 fatfs` (vfs et fatfs en C) :
  « montage SD0 -> 0 », `ls -l SD0:/` liste la carte, `cp SD0:/drivers.txt SD0:/copie.txt` puis `diff` :
  identiques (code 0) ;
* `rtsx` sous QEMU : « aucun lecteur Realtek connu », fin propre ; `ktest` 47/47.

**Pi, QEMU raspi4b** (emplacement 7, copie de `build/sd.img` dans mon dossier temporaire, supprimée ensuite) :
le module de démarrage `sd` remplacé par le C (`build/svc/aarch64/sdhci` enveloppé comme `module.inc`) :
EMMC2 essayé d'abord (pas de carte sous QEMU, ≈ 3 s), puis l'Arasan : carte de 4 Go identifiée, bus 4 bits,
50 MHz, PIO (l'Arasan de QEMU n'annonce pas l'ADMA2, comme pour l'asm), `blk.sd0`, `p1`, `p2` ; vfs monte
SD0 et SD1 ; le bureau complet s'affiche ; préchargement de `init2` fini en 129 s. Même carte, même essai
avec `sd.asm` : le préchargement n'était pas fini au bout de 150 s. **Ce n'est pas une vérification sur le
vrai Pi** : l'EMMC2 réel (DMA, IRQ 158 partagée avec le Wi-Fi, 1,8 V/DDR50 de `BLK_UHS` que le C n'a pas)
reste à essayer ; la carte garde `sd.asm`.

### Premier essai sur le ZBook (à faire par l'utilisateur)

Rien n'est écrit sur le NVMe : les pilotes ne parlent qu'au lecteur de cartes, et les écritures des essais
ne vont que sur la carte. Utiliser une carte **d'essai** (pas une carte qui porte des données à garder).

Préparer (sous Windows) :

```
wsl sh tools/pc/build.sh
powershell -File tools\pc\sd.ps1 E:
fsutil file createnew E:\KORINLOG.TXT 1048576
```

(`KORINLOG.TXT` : 1 Mo réservé à la racine, où `blktest log` recopiera le journal du noyau.)

Démarrer le ZBook sur la carte (F9). Au moniteur `korin>`, dans l'ordre, une étape à la fois (`clear` avant
chaque commande, et une **photo de l'écran** après chacune) :

1. `run pcid -l` : la ligne du lecteur, `0000:BB:DD.F 10ec:5261 ff/00/00` (noter l'adresse) ;
2. `run pcid -n` ;
3. `run rtsx -d` : **diagnostic sans rien écrire** dans la puce : version (`DUMMY_REG`), BIPR (bit 16 : carte
   présente), registres internes, configuration 0x724/0x814 ;
4. `run rtsx -t -r` : initialisation pas à pas (registres après chaque étape), carte en **lecture seule** ;
   attendu : « premier tampon de commandes exécuté », « carte présente : coupée, puis alimentée »,
   « prête après N ACMD41 », CID, CSD, SCR, « bus 4 bits », « service blk.sd0 », `blk.sd0p1` ;
5. `run blktest info`, puis `run blktest all` (lectures seulement : tailles, partition, débit) ;
6. si tout est juste : redémarrer, `run pcid -n`, `run rtsx` (lecture et écriture), puis
   `run blktest safe -w` (écrit dans l'espace libre avant la première partition, **après l'avoir sauvé**,
   relit, remet l'original, relit) ; puis `run blktest log` : le journal dans `KORINLOG.TXT`, lisible sous
   Windows (bloc-notes) ;
7. avec `vfs` et `fatfs` en C (initrd) : `run vfs SD0 blk.sd0p1 fatfs` puis `run ls -l SD0:/`.

Ne **jamais** lancer `blktest all -w` sur la vraie carte (il écrit les 4 derniers Mo, puis les remet : c'est
pour les images d'essai).

Lire les traces : chaque ligne commence par `rtsx :` ; une étape qui ne finit pas laisse sa dernière ligne
à l'écran. Ce qu'elles veulent dire :

| Trace | Cause probable |
|---|---|
| `registres illisibles (BIPR = ffffffff)` | la fonction est endormie (D3) ou éteinte par le BIOS ; voir le réglage du lecteur de cartes dans le BIOS |
| `premier tampon de commandes : -110` | la puce n'exécute pas le tampon (DMA vers la mémoire) : adresse > 4 Go, maître du bus pas allumé par pcid, ou MCU arrêté (`FW_CFG1`) |
| `mémoire DMA sous 4 Go introuvable` | `DMA_ALLOC` a rendu de la mémoire haute : à corriger dans le noyau (zone basse pour le DMA 32 bits) |
| `SURINTENSITÉ` | la protection du LDO1 a coupé la carte (seuil trop bas ? carte en court-circuit ?) : noter `OCPSTAT` |
| `ACMD41 : jamais prête`, `CMD8 … pas de réponse` | horloge ou alimentation de la carte : comparer `SSC_DIV_N_0`, `CLK_DIV`, `SD_CFG1`, `0xFF70`-`0xFF73` dans les registres de `-t` |
| `CMD2 (CID) sans réponse`, CRC7 faux | réponse R2 lue de travers (tampon ping-pong) ou horloge trop haute |
| erreurs de lecture après « carte prête » | ADMA (format des descripteurs du 5261), `DMA_DONE jamais vu`, ou la haute vitesse (le pilote revient alors à 25 MHz) |
| `TRANS_OK avant DMA_DONE` (avec `-v`) | à noter : la fin du tampon de commandes arrive avant celle du DMA |

### Reste

1. **Essayer `rtsx` sur le ZBook** (ci-dessus) ; puis le lancer par la chaîne init2 → hwmanager → pcid
   (`pci:10ec:5261`) et init → vfs « `SD0 blk.sd0p1 fatfs` » quand le démarrage du PC prendra `SD0:` sur la
   carte.
2. Pi : remplacer `sd.asm` par le C seulement après un essai sur un **vrai Pi 4** (EMMC2, DMA par le tampon
   non caché, ligne 158 partagée avec le Wi-Fi) ; il manque au C le passage en 1,8 V / DDR50 de `BLK_UHS`
   (l'expandeur de GPIO du micrologiciel) et les compteurs de temps de `BLK_STATS`. Comparaison faisable sous
   QEMU raspi4b (fait une fois, ci-dessus) : module `sd` du C, même carte, mêmes commandes.
3. `sdhci` : lignes INTx (sans ACPI `_PRT`, une fonction sans MSI sonde), emplacements 2 et plus, SDMA.
4. `rtsx` : 1,8 V et UHS (SDR50/SDR104, accord des phases), efuse, économies d'énergie (ASPM, L1, RTD3),
   SD Express (PCIe), cartes MMC ; profil OpenBSD jamais essayé.
5. Noyau : `DMA_ALLOC` sous 4 Go garanti (le ZBook a 32 Go ; aujourd'hui la mémoire basse est prise d'abord,
   ce qui suffit au démarrage).

## Services de fichiers en C (12/11)

Lot « fichiers » : `vfs`, `fatfs` (FAT32 et exFAT, cache de données), `ramfs`, `imgblk` réécrits en C,
communs au Pi et au PC, **comportement identique à l'asm** : mêmes protocoles (vfs, `fs.inc`, `blk.inc`,
`file_sys` d'AppKit inchangé), mêmes codes d'erreur, mêmes tables et mêmes limites (4096 nœuds de ramfs,
512 fichiers ouverts de fatfs, 128 tubes / 512 bouts / 4096 propriétaires de vfs…), même cache de fatfs
(blocs de 64 Ko, horloge, `SVF_MEMORY`), mêmes journaux. Le pilote de la carte du Pi (`src/svc/sd`) reste en
asm.

### Fichiers

| Fichier | Rôle (traduction de) |
|---|---|
| `src/svc/common/fssvc.h` | message (`struct msg`, champ pour champ comme `MSG_*`), constantes des protocoles |
| `src/svc/common/fsowner.c`, `.h` | propriétaires des fichiers ouverts, tampons partagés (`src/svc/inc/fsowner.inc`) : fatfs et ramfs |
| `src/svc/common/blkserv.c`, `.h` | serveur des périphériques blocs, MBR et partitions logiques, un thread par partition (`src/svc/inc/blkserv.inc`) ; repris aussi par `rtsx`, `sdhci`, `usbstorage` |
| `src/svc/imgblk/imgblk.c` | `imgblk.asm` |
| `src/svc/ramfs/ramfs.c` | `ramfs.asm` |
| `src/svc/fatfs/fatfs.c`, `fat32.c`, `exfat.c`, `fatfs.h` | `fatfs.asm` (service, cache des secteurs, cache de données, nœuds, ops, `dostime.inc`), `fat32.asm`, `exfat.asm` |
| `src/svc/vfs/vfs.c`, `path.c`, `exec.c`, `pipes.c`, `pids.c`, `vfs.h` | `vfs.asm`, `path.inc`, `exec.inc`, `pipes.inc`, `pids.inc` (et `ini_read` d'`applist.inc`, `buspub.inc`) |
| `src/svc/modules/vfs.asm`, `fatfs.asm` | `FILES_C` : le module prend le binaire C (comme `APPKIT_C`) |
| `build.ps1` | `vfs`, `fatfs`, `ramfs`, `imgblk` dans `$svc_c` (sur la carte) et `-d FILES_C=1` pour les modules quand `build/svc/aarch64/vfs` et `fatfs` existent |
| `tools/svc/build_c.sh` | `src/svc/common` dans le chemin des en-têtes |
| `tools/svc/fs_compare.sh` | essai par comparaison asm / C sur deux cartes du Pi (ci-dessous) |

### Bizarreries reproduites (commentées « comme l'asm »)

* ramfs : un fichier effacé encore ouvert rend `E_INVAL` (son nœud peut être repris) ; une écriture de 0 octet
  après la fin agrandit le fichier ; la première unité `k`/`m` trouvée dans « `ram:<taille>` » compte.
* fatfs : `FS_CACHE` avec une valeur inconnue vide le cache ; l'étiquette « `NO NAME` » de FAT32 n'est jamais
  effacée (comparée sur 8 octets après la coupe des blancs) ; `utf8_to_utf16` lit tout octet de tête non
  ASCII autre que `C0`-`DF` et `F0`-`FF` comme un caractère de trois octets, et la comparaison des noms exFAT
  lit un caractère de 4 octets comme s'il en avait 3 ; une erreur de lecture de la FAT pendant un parcours de
  chaîne est suivie telle quelle (cluster 0).
* vfs : la ligne de montage du démarrage est journalisée coupée (« `vfs : montage SD0 -> 0` ») ;
  `idx_renommer` (index `.exec`) compare les deux chemins au bit `0x20` près (« `abc` » et « `abc ` » sont
  « le même ») ; `pid_stdio_drop` passe aussi sur les fiches libres ; les tampons globaux de `monter` sont
  partagés par le thread principal et celui de `mounts.txt` comme dans l'asm.

### Écarts voulus

* fatfs : `dir_entry_at` rend « pas d'entrée » pour un répertoire sous le cluster 2. La racine synthétique de
  `fat_lookup` n'a ni index ni entrée ; l'asm lisait un index non initialisé de la pile et un `utime` de la
  racine d'un volume FAT32 pouvait écrire dans la FAT. Les champs `index` / `first` de la racine synthétique
  sont mis à 0.
* imgblk / blkserv : la place d'un client garde le numéro de son tampon (l'asm le recalculait depuis
  l'adresse, même résultat).

### Vérifié

**Pi (QEMU raspi4b, emplacement 2)** : `sh tools/svc/fs_compare.sh cards` puis `sh tools/svc/fs_compare.sh` :
deux cartes tirées de `build/sdroot`, mêmes commandes de `/bin`, seuls les services de fichiers diffèrent (sur
la carte et dans les modules `vfs.mod`, `fatfs.mod`) ; SD1: est l'image d'essai `test_exfat.img` (refaite
depuis `build/exfat_root`, imgblk `img1`, `mounts.txt` changé). Au démarrage : `fstest`, `fstest2`,
`vfstest`, `exfattest`, `cachetest`, `directtest`, `writetest`, `writebench`, `readbench`, `pipetest`,
`exectest`, `ramtest RAM0`, `ramtest IMG0`, `aktest -f`, `posixtest file io exec fork ipc`, puis **toutes les
lignes des `test.txt`** de `src/bin` (1976 essais) : **sorties et codes identiques**. Seules différences : les
temps (`writetest`, `writebench`, `readbench`, `ramtest`, `pipetest`, `exectest`, `posixtest fork`), la taille
de `SD0:/sys/svc/vfs` vue par `vfstest` (93 544 → 21 176 octets) et un octet de la taille d'un `.tgz` (`arc` :
les dates dans les en-têtes). Détail : vfstest 0 échec, pipetest 31/31, exectest 17/17, exfattest 0 échec,
cachetest et directtest PASS, posixtest 144 réussis / 2 sautés (noms de 200 caractères sur RamFS), aktest 0
échec ; `fstest2` a le même échec avec l'asm et le C (« `fs.ram0 (fils) : ouvertures laissees ouvertes -> 58` »).
Démarrage complet et bureau (capture) avec les services C : volumes SD0, RAM0, IMG0, SD1 montés, bureau Givre
affiché. Le C lit la carte plus vite (`readbench`, 256 Ko par requête : 115 → 250 Mo/s sous
QEMU).

**PC (QEMU q35, 4 cœurs)** : au moniteur, `run vfs`, `urun mount RAM0: ram:16M ramfs`, `urun mount SD0:
ram:64M ramfs` (SD0: en mémoire), les images FAT32 (`test.img`) et exFAT (`exfat_root`) recopiées sur SD0: par
un programme provisoire (initrd, `MOD_READ`, retiré depuis), `run imgblk img0 SD0:/test.img`, `run imgblk img1
SD0:/test_exfat.img`, `urun mount IMG0: blk.img0p1 fatfs`, `urun mount SD1: blk.img1p1 fatfs` : ramfs et fatfs
lancés par vfs depuis l'initrd (`MOD_LAUNCH`), imgblk client de vfs, fatfs sur imgblk sur ramfs. `ls`,
`ramtest RAM0` et `ramtest IMG0` (tout juste), `exfattest` 0 échec, `cachetest IMG0:/cache.bin` PASS,
`directtest SD1:/gros.bin` PASS, `aktest -f` 0 échec, `fstest2` : ram0 et img0 tout juste, 4 échecs qui
viennent du montage d'essai (SD0: est un ramfs : un fichier effacé n'y reste pas lisible ; pas de
`SD0:/bin/fstest2` pour le fils ; le contrôle des registres de `file_sys` est propre à AArch64), `vfstest` :
les essais de chemins, dossiers, `/`, `..`, ouverture et effacement passent, ceux qui demandent `SD0:/bin`,
`SD0:/apps`, `SD0:/sys` échouent en `E_NOENT` (absents de ce SD0:).

### Pas vérifié

* Le vrai Pi ; le selfhost. Retour à l'asm : retirer les quatre noms de `$svc_c` dans `build.ps1`.
* PC : vfs lancé par init (`SD0 blk.sd0p1 fatfs`) sur une vraie carte (`rtsx`, `sdhci`) ; les tubes et le
  lancement de programmes depuis un vrai `SD0:/bin` ; `mounts.txt` et la publication sur le bus (pas de busd
  lancé pendant l'essai).
* Une image vérifiée par `fsck.fat` / `fsck.exfat` après les écritures du C (la comparaison porte sur ce que
  les programmes relisent).

## Services du système en C (lot « démarrage et système », 12/11)

init, init2, hwmanager, busd, hidmanager, mapsvc et echo sont réécrits en C, **une seule fois pour le Pi et
le PC**, comportement repris de l'asm ligne à ligne (mêmes messages, même protocole, mêmes fichiers lus ; les
bizarreries gardées, avec le commentaire « comme l'asm »). Sur le Pi, **le C remplace l'asm** (vérifié,
ci-dessous) : `build.ps1` copie `build/svc/aarch64/<nom>` des services de la liste `$svc_c` sur la carte à
la place de l'asm, et assemble `init.mod` avec l'init en C (`-d INIT_C=1`, `src/svc/modules/init.asm`,
comme `APPKIT_C`). L'asm reste assemblé (selfhost). Les C se construisent dans WSL :
`sh tools/svc/build_c.sh` (avant `.\build.ps1`).

| Fichier | Rôle |
|---|---|
| `src/svc/svclib/svclib.c`, `svclib.h` | commun aux services : `ini_read` / `ini_parse`, `ini_trim`, `str_copy_n` (de `applist.inc`), constantes `SVF_*`, `SVC_OP_*`, `INIT_OP_*`, `BUF_VA_BASE` ; sortie par `puts` et `print_dec` d'AppKit, comme l'asm |
| `src/svc/echo/echo.c`, `src/svc/mapsvc/mapsvc.c` | services d'essai (svctest, maptest) |
| `src/svc/busd/busd.c` | le bus (`src/inc/bus.inc`) : 128 sujets, 256 abonnements, `WS_POST` |
| `src/svc/hwmanager/hwmanager.c` | le cœur : `drivers.txt` (`hdmi0:audio`, `soc:`, `usb:`, **`pci:`** 32 lignes, `i2c1:`), annonces `HW_OP_USB`, `HW_OP_USB_RETRAIT`, **`HW_OP_PCI`** (clés `pci:VVVV:DDDD`, `pci:cc/ss/pp`, `pci:cc/ss`, `pci:cc` ; pilote lancé avec « `SSSS:BB:DD.F` », réponse 0 ; un pilote PCIe arrêté sur une erreur est relancé 3 fois, comme `soc:`), relances des pilotes du SoC, notifications USB, `options.txt` |
| `src/svc/hwmanager/hwmanager_aarch64.c` | la couche du Pi : I2C 1 en logiciel sur GPIO 2/3 (de `src/svc/inc/i2c.inc`), sonde des adresses, révision de la carte (boîte aux lettres), `soc:genet` / `soc:wifi` (`no_wifi`, `no_ethernet`), son HDMI (EDID par le firmware, « hot plug » de HDMI0 toutes les 2 s, `no_hdmiaudio`) |
| `src/svc/hwmanager/hwmanager_x86_64.c` | la couche du PC : ni I2C, ni SoC, ni HDMI (les adresses du Pi ne sont jamais touchées) ; **sans SD0:**, `drivers.txt` vient de l'initrd (`MOD_READ`) et un pilote que `SYS_SPAWN` ne trouve pas est lancé depuis l'initrd (`MOD_LAUNCH` du même nom) |
| `src/svc/hidmanager/hidmanager.c` | entrées : anneaux des pilotes et des abonnés, dispositions (FR et US intégrées, `keyb=`, `SD0:/res/keyb/<NOM>.txt`), répétition, manettes (`gamepad.ini`, `EV_PAD`) ; un thread lecteur |
| `src/svc/init2/init2.c` | étapes 2 et 3, relances, `lastclock`, `paths.txt`, DMA de la carte, `pkg commit`, `INIT_OP_LAUNCH`, `KIT_LOAD`, `REBOOT`, `init2 preload` |
| `src/svc/init2/init2_aarch64.c`, `init2_x86_64.c` | le redémarrage : bloc PM du Pi 4 (chien de garde, partition 63 pour l'arrêt, tryboot) ; PC : rien encore (« le redémarrage n'a pas eu lieu ») |
| `src/svc/init/init.c` | étape 1 (`init.txt`, `MOD_LAUNCH`, attente de SD0:, `BOOT_FILE`, `BOOT_READY`, init2) |
| `src/svc/modules/init.asm` | `INIT_C` : l'init en C dans `init.mod` |
| `build.ps1` | `$svc_c` (services C vérifiés, sur la carte), `INIT_C` |
| `tools/svc/compare.sh` | comparaison asm / C sur le Pi (cartes `build/card_sys_a` / `_c`, modules `build/modules_sys_a` / `_c`, tous les `test.txt` de `src/bin` au démarrage, journaux de l'UART) |
| `tools/pc/build.sh` | `PC_SVC="a b"` (seuls ces services reconstruits), un service dont le nom est pris par une commande va dans l'initrd en `<nom>.svc` (`echo.svc`), `drivers.txt` dans l'initrd, `init` **pas** dans l'initrd (le noyau du PC le lancerait à la place de hello et il attendrait un SD0: : `PC_INIT=1` pour l'y mettre) ; depuis le 13/11, `init` et `init.txt` y sont toujours (« Démarrage jusqu'au shell ») |
| `src/kernel/arch/pc/initrd/stest.c` | essai PC : echo, mapsvc, busd (publication, lecture, liste, abonnement `BUS_CURRENT`, `WEV_BUS` par `EV_WAIT`, retrait, erreurs), hwmanager (annonces sans pilote) ; `run stest hid [NOM]` : abonné de hidmanager, les touches reçues (NOM : la disposition `SD0:/res/keyb/<NOM>.txt` d'abord, `HID_OP_KEYMAP`) |

**Ce qui diffère de l'asm** (sans effet visible) : un programme C ouvre AppKit au départ (runtime korinrt :
echo et mapsvc ne l'ouvraient pas) ; les messages bâtis sur la pile sans mise à zéro dans l'asm (`notifier`,
`sd_dma_request`) partent à zéro.

**Bizarreries gardées** : mapsvc op 3 rend la taille (l'en-tête de l'asm dit 0), une taille 0 lit ou écrit un
octet, une op inconnue est traitée comme 4 avec un mappage en écriture ; busd : désabonner tout rend le nombre,
même 0 ; hwmanager et init2 répondent `E_INVAL` à un `SVC_OP_SIGNAL` ; hwmanager : « retire » sans accent,
`DET_FAIT` jamais mis, un « # » coupe la ligne de `drivers.txt` même dans une valeur ; hidmanager : une touche
sans caractère vaut 0x20 pour le mapping des manettes, `pad_hex2` ne vérifie pas le « : », le dernier jeu de
boutons qui correspond l'emporte, décalages modulo 32, octets UTF-8 de tête de 4 octets lus comme 3 ; init2 :
un `SVC_OP_END` de pid 0 vise une place libre, une relance qui échoue ne compte pas.

**Vérifié sous QEMU raspi4b** (emplacement 1, `sh tools/svc/compare.sh`, cartes tirées de `build/sdroot` le
12/11) :

* démarrage : mêmes services lancés, mêmes lignes au journal de l'UART (init, init2, hwmanager « émulateur :
  rien du SoC », annonces USB du DWC2 et leurs pilotes, hidmanager, préchargement), aux temps et à l'ordre
  d'entrelacement près ; même bureau (captures) ;
* **1 938 essais** (tous les `test.txt` de `src/bin`, lancés au démarrage) : sorties identiques, sauf ftp,
  whois, dltest et un `ls` qui en dépend, parce que `tools/nettest/binsrv.py` a démarré entre les deux cartes ;
  relancés serveur en place (`compare.sh dltest ftp whois ls`) : **115 essais identiques**. Au journal des deux
  cartes, les mêmes `init2 : launch par …` (svctest, maptest : echo, mapsvc, hwtest) et 302 `kit_load` ;
* bus : les essais de `src/bin/bus/test.txt` (publication, lecture, liste, valeurs gardées, retrait, erreurs) ;
* hidmanager : Terminal ouvert à la souris (tablette USB), « echo qwz » tapé au clavier USB → « echo azw »
  (AZERTY intégré), Maj, chiffres ; lanceur ouvert à la souris : captures identiques. `keyb=XT` dans
  `options.txt`, une disposition d'essai (`US.txt` plus des lignes avec `U+`, « - », « -x », UTF-8 de 2 et 3
  octets, tabulations, CR, code non hexadécimal, code trop grand, commentaire indenté) : « disposition XT (59
  touches) » des deux côtés, « echo qwz » tapé → « echo a-é », « ; » sans caractère : captures identiques
  (cet essai a trouvé une erreur du C, corrigée : le chemin du fichier de disposition, `str_cat` d'AppKit
  copie au lieu d'ajouter) ;
* init en C dans `init.mod` : SD0: monté, `BOOT_FILE`, verrou levé, init2 lancé ;
* telnet (`tools/kits/run_pi.sh`) : `bus list`, `bus get korin.usb.device.1` : mêmes sujets et valeurs, sauf
  l'ordre des sujets (celui des premières publications de netd, sound… : course au démarrage), la génération
  de `korin.audio.state` et `korin.clock.offset` (l'heure).

Tailles (Pi) : echo 281 → 2 824 o, mapsvc 475 → 3 064, busd 3 408 → 4 864, hwmanager 25 032 → 14 984,
hidmanager 23 728 → 10 592, init2 6 684 → 9 720, init 2 487 → 4 328 (le C porte le runtime korinrt ; l'asm de
hwmanager et hidmanager écrit ses `.bss` dans le fichier).

**Vérifié sous QEMU q35** (PC, `run stest` au moniteur, `--usb --usbdisk`, `--usbkbd-late`) :

* `stest` : 34 essais réussis (echo : 10 000 allers-retours en ~1 s ; mapsvc ; busd avec `WEV_BUS` reçus par
  `EV_WAIT` ; hwmanager) ;
* chaîne complète **pcid → hwmanager (`HW_OP_PCI`) → usbd → usbstorage / usbkeyboard** : hwmanager prend
  `drivers.txt` de l'initrd, `pci:0c/03/30` lance usbd avec `0000:00:02.0`, usbd annonce la clé (08/06/50 →
  usbstorage) et le clavier branché à chaud (03/01/01 → usbkeyboard), `bus list` montre `korin.usb.device.1` ;
* hidmanager : usbkeyboard s'y inscrit ; `run stest hid` reçoit les touches q, w, Maj+a, 1 du clavier USB
  → 'a', 'z', 'Q', '&' (AZERTY).

**Pas vérifié** : sur le vrai Pi (sonde I2C du GT911, révision de la carte et `soc:genet` / `soc:wifi`, EDID
et branchement HDMI, redémarrage et arrêt par le bloc PM, tryboot, `pkg commit` au démarrage) : le code de la
couche du Pi est repris ligne à ligne de l'asm, mais QEMU n'a ni I2C ni HDMI ni firmware réel ; les manettes
(`EV_PAD_RAW` : QEMU n'en donne pas ; la tablette passe par usbgamepad en souris) et `gamepad.ini` ; init2 et
init sur PC (pas encore de disque servi).

## Réseau et accès distant en C (12/11)

Lot « réseau et accès distant » : `netd` (la pile IPv4), `vterm`, `telnetd`, `ftpd`, `vncd`, `rdpd` réécrits en C,
communs au Pi et au PC, comportement de l'asm (protocoles, messages, délais, bizarreries notées « comme l'asm »).
Les pilotes réseau du Pi (`genet`, `wifi`) restent en asm : netd leur parle par l'anneau de `src/svc/inc/netif.inc`,
inchangé. L'asm reste dans le dépôt (selfhost) ; `build.ps1` met le C sur la carte (`$svc_c`).

| Service | Fichiers | Tailles asm / C Pi / C PC |
|---|---|---|
| netd | `src/svc/netd/netd.h`, `netd.c` (threads, anneaux, interfaces, tampons des process, ops 20-25), `stack.c` (ARP, IPv4, ICMP, UDP, TCP NewReno, DHCP, DNS, boucle locale), `ops.c` (sockets, propriétaires après fork, ping, état), `bg.c` (notifications, `korin.net.state`, NTP, fuseau, `clock.cfg`) | 43 672 / 32 960 / 34 472 |
| vterm | `src/svc/vterm/vterm.c` | 7 936 / 10 376 / 11 008 |
| telnetd | `src/svc/telnetd/telnetd.c` | 6 712 / 8 456 / 8 160 |
| ftpd | `src/svc/ftpd/ftpd.c` | 8 392 / 13 464 / 12 832 |
| vncd | `src/svc/vncd/vncd.c`, `des.c` | 8 336 / 11 256 / 11 312 |
| rdpd | `src/svc/rdpd/rdpd.c`, `sha256.c`, `lz4.c` | 12 824 / 18 720 / 21 720 |

Commun : `src/svc/inc/remotepw.c` / `.h` (mot de passe de l'accès à distance, traduction de `src/inc/remotepw.inc`,
ajouté par `sources = ../inc/remotepw.c` dans `svc.conf`). Essai : `src/svc/netdtest/netdtest.c` (boucle locale
seule, par NetKit : ping et `localhost`, N connexions TCP vers un serveur d'écho dans un second thread, petits
messages et 100 Ko relus octet pour octet, UDP ; marche sur le Pi et sur PC).

Machine : `tcp_iss` prend `CNTPCT_EL0` sur le Pi, `rdtsc` sur PC ; `NST_FREQ` (op 23) vaut `CNTFRQ_EL0` sur le Pi,
0 sur PC (pas de pilote qui chronomètre). Barrières `dmb ish` / `ldar` / `stlr` de l'asm : atomiques de GCC.

### La boucle locale « une fois sur deux » : cause et correction

Ce n'était pas la boucle locale. `op_accept` attend (25 s au plus) en regardant la place du socket d'écoute dans la
table, sans vérifier qu'elle est encore à l'appelant. Quand le process meurt (ou ferme le socket) pendant
l'attente, la place est libérée, puis reprise par l'écoute d'un autre process (même place, état `LISTEN`) : le thread
de netd encore en attente lui vole la première connexion et l'inscrit au pid mort. Le vrai serveur ne la voit
jamais (`httpd` : requête vide, `wget` : « connexion coupée » ; `whois` vers `tcpbench` : rien), et le socket n'est
jamais rendu (fuite : la table de 64 sockets se remplit, d'où des `-12` comme `ultest echec`). **L'asm a le même
défaut** (non corrigé, comme demandé). Le C vérifie à chaque tour, sous le verrou, que le socket est encore à
l'appelant (`sko_find`, comme le fait déjà `op_recv`) : `E_BADF` sinon ; même contrôle dans les attentes de
`op_send` et `op_connect`. Mesuré sous QEMU raspi4b, même carte, seuls les services diffèrent :

| | asm | C |
|---|---|---|
| `netdtest 40` trois fois de suite | 1 erreur (tour 0 d'un essai, `recv` -11 après 10 s) | 120 / 120 |
| `httpd &` puis 6 × `wget 127.0.0.1` | le 1er : « connexion coupée » | 6 / 6 |
| `tcpbench &` puis 4 × `whois -h 127.0.0.1 -p 5001 E…` | 4 / 4 ce jour-là (ailleurs : rien en retour) | 4 / 4 |

Sans la correction, le C donnait exactement l'erreur de l'asm (même tour 0, même -11).

### Bizarreries gardées (notées dans le code)

* netd : `sock_free` ne remet pas les autres champs à zéro ; `op_bind` compte le socket lui-même (un second `bind`
  du même port : `E_ADDRINUSE`) ; une réponse ARP garde les octets 42-59 de la trame ARP précédente ; `op_stats`
  laisse les 64 premiers octets du message sans interface ; `rtt_seq` nul vaut « aucune mesure » ; `op_ntp` attend
  20 s ; une op inconnue (et `SVC_OP_SIGNAL`) reçoit `E_INVAL`.
* vterm : `VT_OPEN` qui échoue au second bout ne rend pas le compteur du maître ; une fin de ligne sans place compte
  l'octet pris ; `VERASE` compare aussi un `cc` nul.
* telnetd : IAC IAC et IAC IP ne touchent pas l'état « CR vu » ; mot de passe : 200 octets comptés, 128 gardés.
* ftpd : `PASV` ferme l'écoute avant de regarder s'il y en a une ; `PORT` : un nombre > 255 déborde ; `STOR` : 60 s
  sans rien = fin (226) ; tous les « : » sautés dans « SD0:/… » ; réponses fixes traduites sans leur CR LF.
* vncd, rdpd : voir l'en-tête de `vncd.c` et `rdpd.c` (version lue sur un chiffre, décalage modulo 64, titre lu un
  octet trop loin, rectangle plus grand que `PACK_MAX` non envoyé…).

### Écarts voulus

* netd : la correction d'`op_accept` / `op_send` / `op_connect` ci-dessus.
* ftpd : la pile d'un thread de session est gardée par sa place et reprise (l'asm en prenait une neuve à chaque
  connexion, sans jamais la rendre).
* rdpd : l'ancienne copie d'une fenêtre qui grandit est rendue (l'asm la perdait) ; champs inutilisés des messages
  à Elegant à zéro.
* Partout : tas du runtime (`korinrt`) au lieu de `malloc` d'AppKit ; netd n'appelle pas `malloc` (pages par
  `SYS_SBRK`, comme l'asm).

### Vérifié

**Pi, QEMU raspi4b, emplacement 6** (cartes `build/card_bin_netsvc_a` / `_c` tirées de `build/sdroot`, mêmes
commandes de `/bin`, seuls les six services diffèrent ; serveurs de l'hôte `tools/nettest/binsrv.py`) :

* lot réseau de `/bin` (`BIN_TAG=netsvc sh tools/bin/compare.sh ping nslookup netstat httpd wget whois ftp wifi
  tcpbench ultest dltest`) : **170 essais, sorties et codes identiques** avec le netd corrigé (un premier passage
  avant la correction : 7 lignes de différence, toutes des échecs de l'asm : `whois` sans écho, `ultest` -12,
  `dltest` 0 octet, après des vols de connexions) ;
* démarrage (journal de l'UART) : `vterm : pret`, `netd : en service`, `netd : interface usb0 (pilote N)`,
  `NET : DHCP ok, adresse 10.0.2.15  passerelle 10.0.2.2  DNS 10.0.2.3`, `netd : USB — 10.0.2.15`,
  `netd : NTP 1794499456 (UTC)` (heure juste), les quatre serveurs en écoute ; `netstat` : interface, bail, sockets ;
* telnet (bash par vterm) : commandes, tubes (`tr`, `sort | head`, `grep -c`), redirections, `cd`, boucle, code de
  sortie ; Ctrl+C et IAC IP interrompent `sleep 30` (code 130) ; mauvais mot de passe : mêmes sorties que l'asm ;
* FTP (`ftplib` de l'hôte, passif par 2100) : mauvais mot de passe 530, racine (volumes), `CWD`, `LIST`, `STOR`
  300 000 octets puis `SIZE` et `RETR` identiques, `XCRC` égal au CRC32 de l'hôte, `MKD`, `DELE`, `RMD`, `RNFR` 502,
  commande inconnue 500, `EPSV` 229, `QUIT` : mêmes réponses que l'asm ;
* VNC (vncdotool, authentification DES « essai ») : capture 1024 × 768 identique à l'asm hors fond tiré au hasard et
  horloge ; clic sur le Terminal du panel, saisie « echo vnc » + Entrée : la commande tourne dans le Terminal ;
* RDP : `tools/remote/rdp_auth_test.py` (mauvais mot de passe, ancien client, défi neuf, bon mot de passe, CAPS puis
  CURSOR) ; `tools/rdp_test.py` derrière un enveloppeur qui fait le HMAC (ONYXRDP2) : une ronde, 26 PIXELS en LZ4,
  Terminal 91 177 / 91 200 points égaux à la capture VNC (le même nombre avec l'asm), clic + « ls » + Entrée : ses
  pixels reviennent.

**PC, QEMU q35** (sous `build/pc.lock`, `PC_QUICK=1`) : `run netd` puis deux fois `urun netdtest 12` : netd sans
aucun pilote, ping 127.0.0.1, `localhost`, 2 × 12 connexions TCP (dont 100 Ko relus), UDP : 0 erreur. vterm, telnetd,
ftpd, vncd, rdpd se construisent pour x86-64 mais n'ont pas tourné sur PC (ni bash, ni Elegant, ni HIDManager sur
PC pour l'instant).

### Pas vérifié

* Sur le **vrai Pi** : GENET et Wi-Fi par netd en C (même anneau, mais QEMU n'a que usbnet), débits (`tcpbench`),
  sondes de fin de rafale et retransmissions sous pertes réelles ; Onyx Remote (client graphique).
* vncd en 8 et 16 bits, molette, touches spéciales et AltGr ; rdpd en 16 bits, cadres, `MOVE`, `CLOSE`, `DESKTOP`,
  reprise par un nouveau client ; telnetd à 8 sessions ; ftpd en mode actif (`PORT`).
* netd en C sur PC avec un vrai pilote réseau (l'AX201 en premier).

## Menu de démarrage (12/11)

Korin a démarré sur le vrai ZBook, lancé à la main depuis le menu du firmware HP (Échap, F9, « Boot From EFI
File », `EFI\Korin\korin.efi`). Le chargeur a maintenant son propre menu, pour démarrer Korin ou Windows sans
passer par F9.

**Tout Korin est sur la partition KORIN OS** (décision de l'utilisateur, 12/11 ; NVMe, disque 0, entrée GPT n° 4 pour Korin (Windows dit « partition 5 »),
FAT32, 10 Go, `D:` sous Windows, nom de volume « KORIN OS ») : le chargeur et son menu (`\EFI\Korin\korin.efi`), le
noyau, l'initrd, `boot.cfg` (`\korin\…`), puis tout le système. **Rien de Korin sur la partition EFI de Windows** :
nos outils ne l'écrivent plus jamais (seul `korin.ps1 cleanesp` y retire ce qu'une ancienne installation y avait
mis) ; l'entrée « Korin OS » du firmware (NVRAM, pas un fichier) mène à `\EFI\Korin\korin.efi` **sur KORIN OS**.

### Choix : dans le chargeur

Le menu est **dans `korin.efi`** (`src/kernel/arch/pc/boot/menu.inc`, inclus par `bootx64.asm`), pas dans un
programme UEFI à part : le chargeur ouvre déjà le volume et lit des fichiers, le menu n'ajoute que l'affichage,
`LoadImage`/`StartImage` et deux variables ; une seule entrée du firmware (`\EFI\Korin\korin.efi`), rien de plus
à copier ni à garder d'accord ; Korin se charge sans second chaînage. Un programme à part ne vaudrait que pour
plusieurs noyaux ou plusieurs systèmes de plus, ce qui n'est pas le cas.

### Ce que fait le menu

* Entrées : **1 Korin OS**, **2 Windows** (seulement si `\EFI\Microsoft\Boot\bootmgfw.efi` est sur **l'un des
  volumes** que le firmware connaît), **Réglages du firmware (BIOS)** (seulement si `OsIndicationsSupported` a le
  bit `EFI_OS_INDICATIONS_BOOT_TO_FW_UI`), **Redémarrer**, **Éteindre** (`ResetSystem` à froid / extinction).
* Clavier : flèches haut et bas, Début, Fin, Entrée ; un chiffre lance sa ligne (aussi `& é " ' ( - è _ ç`, la
  rangée du haut d'un AZERTY sans Maj, si le firmware traduit le clavier). Compte à rebours vers le choix par
  défaut ; **une touche l'arrête** (le menu attend alors sans limite : le chien de garde du firmware est coupé).
* Affichage : console texte de l'UEFI (`ConOut`), dans le mode que le firmware a choisi, bloc centré, sélection
  blanc sur bleu, messages en jaune. Le mode texte n'est pas changé : la police de l'UEFI est fixe (8 × 19 points),
  un autre mode ne change que le nombre de cases, et `SetMode` change la résolution de l'écran sur certains
  firmwares (le noyau reprend ensuite le framebuffer GOP tel qu'il est). Si la console n'a pas les lettres
  accentuées (`TestString`), elles deviennent leur lettre de base.
* **Où est Windows** (`find_windows`) : `LocateHandleBuffer` de `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`, puis, pour chaque
  volume, `OpenVolume` et `Open` de `\EFI\Microsoft\Boot\bootmgfw.efi` **en lecture seule** (refermés aussitôt).
  Le premier volume qui l'a **et** qui est une partition système EFI (`EFI_PARTITION_INFO_PROTOCOL`, champ
  `System` = 1) gagne ; à défaut (firmware sans ce protocole), le premier volume qui l'a. Traces :
  `boot: bootmgfw.efi sur le volume N`, `boot: ce volume est une partition systeme EFI : choisi`, ou
  `boot: pas de bootmgfw.efi, pas d'entree windows`.
* **Windows** : chemin de périphérique complet = celui de **ce volume** (`DevicePath` de son handle) + un nœud de
  fichier `\EFI\Microsoft\Boot\bootmgfw.efi` + le nœud de fin ; `LoadImage` (le firmware lit le fichier lui-même)
  puis `StartImage`, avec un chien de garde de 5 min comme le firmware. `bootmgfw.efi` a donc le même
  `DeviceHandle` (la partition EFI) et le même `FilePath` que s'il était lancé par son entrée du firmware, et
  trouve `\EFI\Microsoft\Boot\BCD` sur sa partition. S'il échoue ou rend la main : message en jaune avec le code EFI
  et son nom (`EFI_SECURITY_VIOLATION`, `EFI_NOT_FOUND`…), retour au menu sans compte à rebours.
* **Réglages du firmware** : `OsIndications |= 1` (variable non volatile), puis redémarrage ; le firmware ouvre
  ses réglages et efface le bit.
* **Korin** : `\korin\kernel.elf`, `\korin\initrd.tar` (et `\korin\boot.cfg`) sont lus sur **le volume d'où
  `korin.efi` a démarré** (`LoadedImage->DeviceHandle` : sur le ZBook, KORIN OS), puis GOP, ACPI,
  `ExitBootServices`. Noyau absent ou invalide : message et retour au menu (au lieu de rendre la main au firmware).
* Traces sur le port `0xE9` (`boot: menu`, `boot: lance windows`…), comme la console du noyau : sous QEMU, elles
  sont dans `build/pc/debug.log`.

### Les fichiers sur KORIN OS, vus de Korin

KORIN OS est entière à Korin : quand `SD0:` sera cette partition (`blk.nvme0p4`, **sans `rootpath`**), les fichiers
du démarrage sont des fichiers ordinaires du système :

| Windows (`D:`) | Korin | Rôle |
|---|---|---|
| `D:\EFI\Korin\korin.efi` | `SD0:/EFI/Korin/korin.efi` | le chargeur et son menu (entrée du firmware, F9) |
| `D:\korin\kernel.elf` | `SD0:/korin/kernel.elf` | le noyau |
| `D:\korin\initrd.tar` | `SD0:/korin/initrd.tar` | les premiers programmes |
| `D:\korin\boot.cfg` | `SD0:/korin/boot.cfg` | les réglages du menu |

`pkg` pourra donc les mettre à jour comme n'importe quel fichier de `SD0:` (un paquet du PC qui les contient,
sans traitement à part). **`BOOT:` n'est plus nécessaire sur le PC**, sauf pour d'éventuels fichiers de la partition
EFI de Windows (aucun aujourd'hui) ; il reste utile au Pi et à la carte double (`rootpath`).

### `\korin\boot.cfg`

Sur le même volume que le chargeur (KORIN OS), texte, une clé par ligne, `#` pour un commentaire, casse et blancs
libres, fins de ligne Windows ou Unix, BOM UTF-8 accepté ; une ligne inconnue ou fausse est ignorée :

```
default = korin          # korin | windows (défaut : windows s'il est là, sinon korin)
timeout = 5              # secondes, 0 à 3600 (défaut 5) ; 0 = sans menu
cmdline = keyb=BE        # options du noyau, 127 caractères au plus, séparées par des espaces
```

* **Sans fichier : menu, Windows par défaut, 5 s** : le PC qui démarrerait sur `korin.efi` sans réglage finit
  toujours dans Windows.
* `timeout = 0` : pas de menu, le choix par défaut tout de suite, **sauf si une touche est déjà tenue** au
  lancement du chargeur ; si ce choix échoue (Windows rend la main), le menu s'affiche.
* `cmdline` : recopié dans le bloc de démarrage (`bootinfo.cmdline`, 128 octets à la fin, `BB_SIZE` = 240 ; le
  noyau l'affiche, « Options : … »). Options lues par le noyau :
  * `keyb=BE` : le clavier PS/2 du moniteur en **AZERTY belge** (`ps2_layout` de `src/kernel/arch/pc/ps2.c` ;
    Latin-1, touches mortes `^ ¨ ´ ~` et accent grave comprises, sans €) ; le noyau dit « Clavier PS/2 : prêt
    (AZERTY belge) ». Sans option (ou un autre nom) : l'AZERTY de France, comme avant. Les dispositions des programmes
    (hidmanager, `SD0:/res/keyb`) se choisissent à part : `docs/CLAVIER.md`.
* `tools/pc/build.sh` écrit `build/pc/esp/korin/boot.cfg` (`default = korin`, `timeout = 0`) s'il n'existe pas :
  sous QEMU, Korin démarre tout de suite, comme avant le menu (les essais gardent leur minutage). `sd.ps1` et
  `korin.ps1 install` ne le copient pas : une clé faite par `sd.ps1` montre le menu (Korin par défaut, pas de
  Windows sur la clé… sauf si le PC a une partition EFI de Windows : le menu la trouve maintenant sur le disque
  interne, et Windows devient le choix par défaut).

### `tools/pc/korin.ps1` (nouveau) et `tools/pc/esp.ps1` (ancien)

**`korin.ps1`** installe sur la partition KORIN OS. Elle est trouvée par son nom de volume « KORIN OS » (`-Drive D:`
pour choisir s'il y en a plusieurs) et vérifiée avant toute écriture : une seule, avec une lettre, **FAT32**, type
GPT **Basic data** (`{ebd0a0a2-…}`), **ni la partition système EFI** (type `{c12a7328-…}` ou `IsSystem`), **ni
celle de Windows** (`C:`, `IsBoot`), sur **le disque de Windows** (même numéro de disque que `C:`), pas sur un disque
USB, SD ou MMC. `-DryRun` : à blanc, rien n'est écrit (ni fichier, ni entrée du firmware), les commandes qui
changeraient quelque chose sont affichées. `install` et `config` n'ont pas besoin d'invite administrateur (copie sur
du FAT32) ; `remove`, `default`, `undefault`, `cleanesp` oui (`bcdedit`, `mountvol`) ; `show` montre l'ordre du
firmware seulement en administrateur.

| Action | Ce qu'elle fait |
|---|---|
| `install` | copie `build\pc\esp\EFI\BOOT\BOOTX64.EFI` en `\EFI\Korin\korin.efi`, et `\korin\kernel.elf`, `\korin\initrd.tar` sur KORIN OS (place vérifiée) |
| `show` | la partition (disque, bus, numéro, place), les quatre fichiers (taille, date), `boot.cfg`, l'ordre du firmware et les entrées « Korin OS » (sur KORIN OS ou non) |
| `config -Default korin -Timeout 5 [-Cmdline "..."]` | écrit `\korin\boot.cfg` (ASCII, fins de ligne Unix) ; `config -Clear` le retire (défauts) |
| `remove` | retire les quatre fichiers, puis `\EFI\Korin`, `\EFI`, `\korin` s'ils sont vides (le reste de KORIN OS ne bouge pas) ; refuse tant qu'une entrée « Korin OS » existe (`undefault` d'abord, ou `-Force`) |
| `default` | refuse si Secure Boot est actif ou si BitLocker protège `C:` (`-Force`) ; montre l'ordre ; `bcdedit /copy {bootmgr} /d "Korin OS"` (nouvelle entrée du firmware), `bcdedit /set {id} device partition=D:` (la lettre trouvée), `bcdedit /set {id} path \EFI\Korin\korin.efi`, `bcdedit /set {fwbootmgr} displayorder {id} /addfirst` ; montre l'ordre et l'entrée après. Une entrée Korin déjà là est reprise (pas de doublon) et **remise sur KORIN OS** (`device`, `path`) : une ancienne entrée qui menait à la partition EFI est ainsi corrigée |
| `undefault` | montre l'ordre ; `bcdedit /set {fwbootmgr} displayorder {bootmgr} /addfirst`, puis `bcdedit /delete {id}` des seules entrées dont le chemin est `\EFI\Korin\korin.efi` (sur KORIN OS ou sur la partition EFI ; jamais `{bootmgr}` ni une entrée qui mène à `bootmgfw.efi`) |
| `cleanesp` | monte la partition EFI (`mountvol X: /S`), vérifie FAT et `\EFI\Microsoft\Boot\bootmgfw.efi`, liste `\EFI`, puis retire **seulement** `\EFI\Korin` et `\korin` (après les avoir listés ; refus s'ils contiennent `bootmgfw.efi`, `bootmgr.efi` ou `BCD`), démonte ; refuse tant qu'une entrée « Korin OS » ne mène pas à KORIN OS (`default` ou `undefault` d'abord, ou `-Force`). `\EFI\Microsoft`, `\EFI\Boot` et le reste : lus, jamais écrits |

Les entrées sont reconnues par `bcdedit /enum firmware /v` (GUID et valeurs seulement : les intitulés des champs
peuvent être traduits selon la langue de Windows) ; une entrée est « sur KORIN OS » si son bloc contient
`partition=D:` (la lettre de KORIN OS ; une partition sans lettre, comme la partition EFI, s'affiche
`partition=\Device\HarddiskVolumeN`).

**`esp.ps1`** (l'ancienne installation sur la partition EFI) reste utilisable : `show` (lecture), `remove` (retire
`\EFI\Korin` et `\korin` de la partition EFI, refuse si une entrée « Korin OS » mène encore à la partition EFI) ;
`install` et `config` n'écrivent sur la partition EFI qu'avec **`-Esp`** ; `default` et `undefault` n'y sont plus
(message : voir `korin.ps1`).

### Marche à suivre sur le ZBook (par l'utilisateur, dans l'ordre)

Toutes les commandes dans une invite PowerShell **en tant qu'administrateur**, depuis `C:\Projects\ARMPI`
(ci-dessous, « `… korin.ps1` » abrège `powershell -ExecutionPolicy Bypass -File tools\pc\korin.ps1`).

1. Prérequis, comme avant : BitLocker de `C:` entièrement déchiffré (`manage-bde -status C:`), clé de récupération
   sous la main ; Secure Boot désactivé (F10) ; démarrage rapide de Windows éteint.
2. Construire : `wsl sh tools/pc/build.sh`.
3. **D'abord enlever ce qui est sur la partition EFI** (installé à la main plus tôt) :
   * `powershell -ExecutionPolicy Bypass -File tools\pc\korin.ps1 cleanesp -DryRun` : lire la liste (seulement
     `\EFI\Korin\…` et `\korin\…`) et l'ordre ;
   * si une ancienne entrée « Korin OS » du firmware existe (refus de `cleanesp`) :
     `… korin.ps1 undefault` (Windows Boot Manager en premier, entrée retirée) ;
   * `powershell -ExecutionPolicy Bypass -File tools\pc\korin.ps1 cleanesp`.
4. **Installer sur KORIN OS** : `… korin.ps1 install -DryRun` (vérifie la partition : la ligne « Partition KORIN OS :
   D: (disque 0 …, NVMe, partition 5) » ; Korin la voit en entrée GPT 4, blk.nvme0p4), puis `… korin.ps1 install`, puis `… korin.ps1 show`.
5. **Essayer sans rien changer à l'ordre du firmware** : redémarrer, Échap, F9, « Boot From EFI File », le volume
   **KORIN OS** (pas la partition EFI), `EFI`, `Korin`, `korin.efi` → le menu, avec « Windows » (trouvé sur la
   partition EFI) ; essayer « Windows » (Windows doit démarrer normalement), puis de nouveau F9 et « Korin OS »,
   « Réglages du firmware », « Redémarrer », « Éteindre ».
6. Seulement si tout va bien : `… korin.ps1 config -Default windows -Timeout 5` (ou `korin`), puis
   `… korin.ps1 default -DryRun` (lire les commandes), puis `… korin.ps1 default` (lire l'ordre avant et après :
   « Korin OS » en premier, `partition=D:`, `\EFI\Korin\korin.efi`). Au redémarrage, le menu de Korin vient seul.
7. Pour revenir en arrière : `… korin.ps1 undefault` (Windows Boot Manager en premier, entrée Korin retirée) ; si
   Windows ne démarrait plus du tout : F9 dans le firmware, « Windows Boot Manager ».

À savoir : certains firmwares HP, et parfois les mises à jour de Windows, remettent Windows Boot Manager en
premier ; `korin.ps1 default` se relance alors sans doublon. Avec Secure Boot actif, le firmware refuse
`korin.efi` (message d'HP, puis en général l'entrée suivante, Windows : non essayé) ; si Secure Boot est remis plus
tard, le menu ne s'affiche plus, rien d'autre ne casse. Si le firmware HP refusait une entrée de démarrage qui mène
à une partition autre que la partition EFI (pas vu, mais pas essayé), F9 « Boot From EFI File » reste possible et
`undefault` revient à l'état d'avant.

### Vérifié (QEMU q35 + OVMF seulement, 12/11)

**Démarrage depuis KORIN OS** : `tools/pc/mkkorindisk.py` fait une image GPT de 160 Mo qui imite le NVMe du ZBook
(p1 partition système EFI, FAT32 « SYSTEM », avec le **faux Windows** `tools/pc/fakewin/bootmgfw.asm` et un faux
`BCD`, **rien de Korin** ; p2 MSR ; p3 « C: » NTFS imité ; p5 Basic data, FAT32 « KORIN OS », avec
`\EFI\Korin\korin.efi`, `\korin\kernel.elf`, `\korin\initrd.tar`, `boot.cfg` avec `--cfg`) ; aucun volume n'a de
`\EFI\BOOT\BOOTX64.EFI`, OVMF finit dans son shell, dont le `startup.nsh` (sur p5, pour l'essai seulement) lance
`fsN:\EFI\Korin\korin.efi` comme le ferait F9 « Boot From EFI File ». `qemu_pc.py --disk IMAGE` (nouveau ;
`snapshot=on`, l'image n'est pas écrite ; le comportement par défaut ne change pas), variables à part
(`--vars build/pc/ovmf_vars_korin.fd`). Le shell montre `FS0` = `HD(1,GPT,…)` (EFI) et `FS1` = `HD(5,GPT,…)` (KORIN) :

* sans `boot.cfg` : `boot: bootmgfw.efi sur le volume 1`, `boot: ce volume est une partition systeme EFI : choisi` ;
  menu à 5 entrées, Windows sélectionné, compte à rebours ; puis faux Windows : `DeviceHandle :
  PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x0,0xFFFF,0x0)/HD(1,GPT,821CCF92-…,0x800,0x20800)` (la partition EFI, pas
  KORIN), `BCD trouve`, `FilePath : \EFI\Microsoft\Boot\bootmgfw.efi` (nœud de fichier puis fin), **verdict OK** ;
* une touche dans le faux Windows → menu, « Windows a rendu la main (code 0x8000000000000015 EFI_ABORTED) » ;
  puis `1` → Korin démarre de KORIN OS (« Noyau prêt. », initrd de 5090 Ko) ;
* `boot.cfg` `default = korin`, `timeout = 3` sur KORIN OS : lu, Windows quand même trouvé, Korin au bout de 3 s,
  `run ktest` **47 ok, 0 échec** ;
* sans `bootmgfw.efi` nulle part (`--no-windows`) : `boot: pas de bootmgfw.efi, pas d'entree windows`, menu à
  4 entrées, Korin par défaut, 5 s, puis Korin ;
* `bootmgfw.efi` qui n'est pas un programme (`--bad-windows`) : « Windows : LoadImage a échoué (code
  0x8000000000000003 EFI_UNSUPPORTED) », retour au menu ;
* `build/pc/esp` en vvfat (sans `--disk`, comme les autres essais) : rien ne change, Korin tout de suite,
  `run ktest` **47 ok, 0 échec**.

Avant (même jour, volume unique `build/pc/esp-menu` de `tools/pc/menu_esp.sh`, Windows et Korin sur le même
volume), toujours valable : flèche haut pendant le compte à rebours, `timeout = 0`, `default = windows`, BOM et
CRLF dans `boot.cfg`, `kernel.elf` vide, « Réglages du firmware » (`OsIndications`, `--reboot`), « Redémarrer » et
« Éteindre ».

**Scripts PowerShell** : analyse de syntaxe de `korin.ps1`, `esp.ps1`, `sd.ps1` ; essais à blanc où `bcdedit.exe`,
`mountvol`, `Get-Volume`, `Get-Partition`, `Get-Disk`, Secure Boot, BitLocker et `DriveInfo` sont des **fonctions
d'imitation** (rien de réel lancé), KORIN OS et la partition EFI imitées par des dossiers : `install` et `config`
copient et écrivent bien sur la fausse KORIN (`boot.cfg` en ASCII, `\n`) ; `default` (sans entrée, avec une
ancienne entrée sur la partition EFI, avec une entrée sur KORIN), `undefault`, `remove`, `cleanesp` à blanc donnent
les bonnes commandes ; refus vérifiés : aucun volume « KORIN OS », deux volumes, partition EFI, autre disque,
exFAT, `cleanesp` avec une ancienne entrée, `remove` avec une entrée, actions d'administrateur sans invite
administrateur, `esp.ps1 install` sans `-Esp`, `esp.ps1 default`/`undefault` ; la fausse partition EFI est restée
intacte.

### Pas vérifié

* Tout sur le **vrai ZBook** : `korin.efi` lancé depuis KORIN OS par F9, `find_windows` sur le firmware HP
  (`EFI_PARTITION_INFO_PROTOCOL` présent ou non : sans lui, le premier volume qui a `bootmgfw.efi`), le vrai
  `bootmgfw.efi` chaîné depuis la partition EFI (BCD, Windows qui démarre), console et police de l'UEFI d'HP,
  clavier (chiffres AZERTY), `OsIndications`, `ResetSystem`.
* La préférence pour la partition système EFI quand un autre volume a aussi `bootmgfw.efi` (sous OVMF, la partition
  EFI est toujours le premier volume).
* `korin.ps1` et `esp.ps1` en vrai : jamais lancés (ni `bcdedit`, ni `mountvol`, ni copie sur `D:`) ; en
  particulier `bcdedit /set {id} device partition=D:` sur une entrée du firmware, et l'entrée de démarrage qui en
  sort dans la NVRAM d'HP (`HD(5,GPT,…)/\EFI\Korin\korin.efi`).
* Les chiffres `& é " ' ( - è _ ç` (QEMU envoie un clavier US à OVMF).

## Noms logiques des volumes (décision du 12/11)

Après le portage complet de Korin, et pas avant : `SD0:` devient **`SYS:`** (système) et `SD1:` devient
**`HOME:`** (données de l'utilisateur) ; `RAM0:`, `IMG0:`, `USB0:` restent. D'abord des alias dans vfs (les anciens
noms mènent aux nouveaux), puis le renommage dans le dépôt par un script (3 724 `SD0:` dans 831 fichiers au 12/11),
suite d'essais avant et après. `BOOT:` (sur PC, la partition EFI de Windows) n'est monté qu'à la demande, par le
programme qui doit y écrire, et démonté aussitôt ; depuis que tout Korin est sur KORIN OS (« Menu de démarrage »),
le PC n'en a plus besoin pour démarrer : le chargeur, le noyau et l'initrd sont des fichiers ordinaires de `SD0:`.

## Partition KORIN dédiée, plus de double démarrage NTFS pour l'utilisateur (12/11)

L'utilisateur a créé une partition **KORIN OS** de 10 Go en FAT32 sur le NVMe (GPT n° 4) : Korin y est chez lui,
sa racine est la racine du système (`SD0:` sur `blk.nvme0p4`, sans `rootpath`). **Il ne fera pas de double démarrage
sur la partition NTFS de Windows** : le pilote NTFS et le démarrage dans un dossier de la partition Windows deviennent
une **fonction facultative**, pour un utilisateur qui ne voudrait pas de partitions multiples, sans priorité, en toute
fin. `rootpath` reste utile : carte double Pi/PC, et **`HOME:` dans un dossier de la partition KORIN** (sur PC, pas de
partition de données : `SD1 = blk.nvme0p4, fatfs, /home` dans `mounts.txt`, même service fatfs que `SD0:` ; le jour
d'une vraie partition de données, seule cette ligne change).

## Dépôt de paquets du PC (décision du 12/11)

Un index par plateforme, signé par la même clé : le Pi garde `packages/index.txt`, `index.sig` et `packages/pkgs/`
(`board = armpi`, rien ne change pour les cartes déjà installées) ; le PC a son sous-dossier `packages/pc/` avec son
`index.txt`, `index.sig` (`board = pc`) et `packages/pc/pkgs/*.opk`. Pas de suffixe de plateforme dans un même dossier
(collisions de noms, nettoyage de `mkrepo.py`). Les paquets sans code machine (polices, documents, exemples,
SoundFonts, peut-être les DLL .NET) restent une seule fois dans `packages/pkgs/` : l'index du PC les cite par leur
chemin (`../pkgs/…`, taille et SHA-256 comme d'habitude), marqués `arch = any`. `pkg` du PC : l'adresse
`…/packages/pc/` dans `etc/pkg/pkg.ini` ; `mkrepo.py` construit et nettoie un dossier par plateforme.

## NVMe : le disque interne du ZBook (12/11)

But : que Korin lise le disque NVMe du ZBook (SK hynix PC711, GPT, il porte Windows) et **écrive seulement** sur sa
partition **KORIN OS** (GPT n° 4, Basic data, FAT32), pour y monter `SD0:`. Une fois `ExitBootServices` passé, le
micrologiciel n'aide plus : il faut un pilote.

### Fait

| Fichier | Rôle |
|---|---|
| `src/svc/nvme/nvme.c` | le contrôleur, d'après les spécifications publiques « NVM Express Base » 1.4 et « NVM Command Set » ; commun (rien de x86) : remise à zéro (le pilote NVMe de l'UEFI le laisse en marche : `CC.EN` coupé, `RDY` attendu selon `CAP.TO`), file d'administration (32), IDENTIFY du contrôleur (modèle, série, micrologiciel, `MDTS`, cache d'écriture `VWC`), liste des espaces de noms actifs (CNS 2, sinon 1…NN), IDENTIFY de l'espace de noms (taille, formats ; seuls les blocs de 512 sans métadonnées sont servis), `SET FEATURES` nombre de files, **une** paire de files d'E/S (64 entrées ; une par cœur plus tard), lecture et écriture par **PRP** (PRP1, PRP2, **liste PRP** d'une page au-delà de deux pages), morceaux de `min(MDTS, 1 Mo)`, **16 commandes en vol** par requête, FLUSH, **arrêt propre** (`FLUSH`, files d'E/S rendues, `CC.SHN = 01`, `CSTS.SHST = 10`) ; interruption par **MSI-X** (sinon MSI, sinon sondage ; `-p` : sondage forcé), attente courte (`IRQ_WAIT` de 5 ms : une MSI arrivée avant l'attente n'est pas perdue longtemps) ; les données vont **droit** dans le tampon partagé du client (adresse physique de `BUF_INFO`) ou dans ses pages (`BLK_READ_D` : `SVC_CALLER_PHYS`), sinon par un tampon de 1 Mo ; un thread vide le cache du disque une seconde après la dernière écriture (le PC n'a pas encore d'extinction qui préviendrait le pilote) ; `nvme stop` (arrêt propre puis fin du pilote) et `nvme flush` par `SVC_SIGNAL` sur `blk.nvme0` |
| `src/svc/nvme/policy.c` | qui a le droit d'écrire (ci-dessous) |
| `src/svc/nvme/plat_x86_64.c` | PC : la fonction de pcid (`pci:01/08/02`, ou le premier NVMe), BAR0, MSI-X par `pcilib` ; `E_BUSY` (déjà pris) : fin sans erreur |
| `src/svc/nvme/plat_aarch64.c` | Pi : pas de NVMe (pas de pcid) ; garde le pilote construisible pour les deux machines |
| `src/svc/common/blkserv.c`, `.h` | le serveur de blocs commun (imgblk, usbstorage) : **GPT** en plus (MBR protecteur `0xEE`, partitions numérotées **comme leurs entrées** : la 5e entrée est `p5` même si elle est placée avant la 4e, CRC de l'en-tête et des entrées vérifiés, 16 partitions servies, 1024 entrées lues au plus), noms `p10`… en décimal, et trois **crochets** facultatifs : `blk_table_fn` (toute la table avant de servir : lecture seule partition par partition, nom de plus `blk.<alias>`), `blk_deny_fn` (écriture refusée), `blk_signal_fn` (`SVC_SIGNAL` du disque). Sans crochet, sur un disque MBR, rien ne change (essayé : clé `usbstorage`, `blktest usb0 all -w`) ; une clé GPT montre maintenant ses partitions au lieu d'une seule « p1 » de type `0xEE` |
| `src/svc/blktest/blktest.c` | `blktest DISQUE deny -w` : écritures au début, au milieu, à la fin, qui doivent être refusées, contenu relu identique (image d'essai seulement : si le refus manquait, le motif serait écrit) |
| `skel/sd0/etc/drivers.txt` | `pci:01/08/02 = SD0:/sys/svc/nvme` |
| `skel/sd0/etc/nvme.txt` | les règles (dans l'initrd du PC : `tools/pc/build.sh` le copie, `PC_NVME_RULES=fichier` pour un autre) |
| `tools/pc/mkgpt.py` | image d'essai : disque GPT de 256 Mo qui imite le ZBook (p1 EFI FAT32, p2 MSR, p3 « C: » NTFS imité, p5 KORIN OS FAT32 placée **avant** p4 récupération NTFS imitée, attributs `8000000000000001`), GUID fixes ; variantes `badcrc`, `dup` (p3 aussi « KORIN OS »), `nolabel` ; `sums` (SHA-256 de chaque zone : partitions, MBR + GPT, trous, GPT de secours) et `compare` |
| `tools/pc/qemu_nvme.py` | QEMU q35 avec `-device nvme,serial=KORINTEST` sur l'image (refuse ce qui n'est pas un fichier ordinaire), `--mdts N`, étapes comme `qemu_sd.py`, variables d'OVMF à part (`build/pc/ovmf_vars_nvme.fd` : OVMF y inscrit le disque NVMe) |

Options : `nvme [-r] [-v] [-d] [-p] [-n NOM] [-N NSID] [-c FICHIER] [-w RÈGLE]... [-G] [SSSS:BB:DD.F]` ; `-d`
diagnostic (contrôleur et espaces de noms, puis fin : rien de servi) ; `-w` : une ligne de règles, « + » pour une
espace (`-w write=type=basic,label=KORIN+OS`).

### Sécurité : le mécanisme retenu

Par défaut **rien ne s'écrit**. Trois verrous l'un derrière l'autre :

1. **Des règles explicites**, dans `nvme.txt` (module de l'initrd : le pilote démarre avant `SD0:`, qui est sur ce
   disque même), `-c FICHIER` ou `-w` :
   ```
   write = type=basic, label=KORIN OS      # la partition qui remplit TOUTES les conditions s'écrit
   name = korin, type=basic, label=KORIN OS # elle est aussi servie en blk.korin
   ```
   Conditions : `label=` (nom du volume FAT ou exFAT, lu dans le secteur de démarrage **ou** l'entrée de volume de
   la racine, sans casse), `guid=` (GUID unique de la partition), `type=` (`basic`, `efi`, `msr`, `recovery`,
   `linux` ou un GUID), `part=` (numéro d'entrée), `disk=`. **Une règle qui désigne plusieurs partitions n'en ouvre
   aucune** (« AMBIGU »).
2. **Des refus qu'aucune règle ne lève** : le disque entier (`blk.nvme0`) ; une GPT au CRC faux ; les types EFI, MSR,
   récupération de Windows, LDM, espaces de stockage ; les attributs GPT « requise par la plateforme » (bit 0) et
   « lecture seule » (bit 60) ; un contenu **NTFS, BitLocker, ReFS ou inconnu** (seuls FAT12/16/32 et exFAT
   s'écrivent : C: ne peut pas s'ouvrir, même par `part=3`) ; une partition hors de la zone utile de la GPT, sur la
   table, ou qui en chevauche une autre ; `-r` : tout (les règles sont lues et le journal dit ce qu'elles
   *auraient* ouvert).
3. **La garde du plus bas niveau** (`policy_write_ok`, appelée par `do_rw` pour **chaque** écriture, juste avant
   d'écrire les commandes dans la file) : la plage entière doit tomber dans une partition ouverte par 1 et 2, sinon
   refus et ligne « GARDE » au journal, même si le service blk au-dessus s'était trompé. Au démarrage, la garde est
   essayée **sans rien écrire** (secteurs 0, 1, 2-33, la fin du disque, le disque entier, les bords de chaque
   partition ouverte) ; une erreur à cet essai met tout en lecture seule.

Une écriture refusée rend `E_PERM` au client (`fatfs` voit de plus `BLK_INFO` « lecture seule » et ne l'essaie même
pas) et laisse une ligne « écriture REFUSÉE : blk.nvme0p3, secteurs … pid … » (les 20 premières, puis une sur
1000). `-G` (essai de la garde : le service blk ouvre tout, la garde seule refuse) n'est accepté que sur le disque
émulé de QEMU (modèle « QEMU NVMe Ctrl », série `KORINTEST`).

### Monter SD0: sur la partition KORIN (proposition)

Par le **nom de plus** donné par la règle `name = korin, …` : `blk.korin` est la partition dont le volume s'appelle
KORIN OS, quel que soit son numéro (on ne dépend ni de l'ordre des entrées ni d'un GUID à recopier). Au démarrage du
PC, `init.txt` de l'initrd (fait le 13/11 autrement : `pcid -n`, `nvme`, `vfs SD0 blk.korin fatfs`, voir « Démarrage jusqu'au shell ») :

```
hwmanager
pcid
vfs SD0 blk.korin fatfs
```

(hwmanager lance `nvme` par `pci:01/08/02` ; ou `pcid -n` puis `nvme` directement). Dans `mounts.txt`, `HOME:` dans un
dossier de la même partition : `SD1 = blk.korin, fatfs, /home` — **le même nom de source que `SD0`**, pour que vfs
reconnaisse la partition déjà servie et partage le même fatfs (il compare le nom de la source). Le numéro (`blk.nvme0p4`)
marche aussi ; un GUID se met dans la règle (`name = korin, guid=…`) si l'on veut plus strict que le nom de volume.
Attention : fatfs attend `blk.korin` 3 s ; sur le ZBook, `nvme` doit avoir fini (remise à zéro du contrôleur, IDENTIFY,
GPT) dans ce délai, sinon le montage échoue (à voir au premier essai ; sinon, faire attendre vfs).

### Vérifié (QEMU q35 + OVMF, 4 cœurs, images de `mkgpt.py`, supprimées ensuite)

* `pcid -n`, `nvme` : « QEMU NVMe Ctrl », NVMe 1.4, **contrôleur laissé en marche par OVMF puis remis à zéro**,
  MSI-X ligne 256, file de 64, 16 en vol, espace de noms 1 de 524 288 blocs de 512, formats lus ; GPT : 128 entrées,
  CRC juste ; p1 « SYSTEM » FAT32 EFI, p2 MSR, p3 NTFS, p4 récupération NTFS, **p5 « KORIN OS » FAT32** (numéros =
  entrées, p5 avant p4 sur le disque) ; règle 1 : p5 en lecture et écriture, règle 2 : `blk.korin` ; garde : 14 essais,
  0 erreur ;
* `blktest nvme0 all` et `all` de chaque partition : lectures de 1 à 2048 secteurs identiques par morceaux de 1, 2,
  7, 8, 255, 256, 257, 1000, au-delà de la fin refusé, `BLK_READ_D` (300 secteurs, pages de l'appelant) identique ;
  **≈ 450 à 730 Mo/s** en lecture par 1 Mo (QEMU) ;
* `blktest nvme0p5 all -w` : écritures de 1, 2, 8, 255, 256, 257, 1024, 2048, 4096 secteurs relues identiques puis
  remises, **≈ 190-200 Mo/s** en écriture par 1 Mo ; avec `--mdts 2` (16 Ko par commande : listes PRP, 8 000
  commandes) et `-p` (sondage) aussi ;
* `blktest deny -w` sur `nvme0`, `p1`, `p2`, `p3`, `p4` : 15 écritures refusées (-1), 15 lignes « écriture REFUSÉE »,
  contenus relus inchangés ; `-r` : p5 refusée aussi (« serait permise, mais -r ») ; `-w write=part=3`,
  `type=efi`, `part=2` : refusés (NTFS, EFI, MSR) ; image `badcrc` : p5 refusée (« CRC de la GPT faux ») ; `dup` :
  « AMBIGU », rien d'ouvert ; `nolabel` + `-w write=guid=8628E1D3-…` et `name=data,guid=…` : p5 ouverte et servie en
  `blk.data`, `all -w` juste ; `-G` : p1, p3, p4 ouvertes au service blk, **les 9 écritures refusées par la garde**
  (« GARDE : écriture des secteurs 100352 à 100359 refusée ») ;
* **sommes avant/après** de chaque zone (`mkgpt.py sums` / `compare`) après chaque séance : rien n'a changé hors de
  p5 (et p5 non plus quand blktest remet l'original) ;
* `nvme stop` : « arrêt propre en 0 ms (cache vidé : oui, CSTS 00000009) », fin du pilote ; un nouveau `nvme` reprend
  le contrôleur arrêté (`CC 00464001`) ; `nvme flush` ;
* **chaîne** `hwmanager` → `pcid` (« pci 0000:00:02.0 (01/08/02 1b36:0010) -> SD0:/sys/svc/nvme ») → `nvme` →
  `vfs SD0 blk.korin fatfs` (« montage SD0 -> 0 ») : `ls -l SD0:/`, `cp SD0:/docs/gros.bin SD0:/copie.bin` (3 Mo) et
  `diff` (code 0), `cp` + `diff` de `etc/drivers.txt` ; `mount SYS: blk.nvme0p1 fatfs` : la partition EFI se lit
  (`EFI/Microsoft/Boot/BCD`), `cp … SYS:/essai.txt` échoue (fatfs en lecture seule) ; sommes : **seule p5 a
  changé**, et `fsck.fat -n` de p5 extraite : propre ;
* essais du noyau : `ktest` 47/47, `ptest` 22/22, `itest` 29/29, `mtest` 19/19, `btest` 10/10, `wtest` 12/12 ;
  `usbstorage` (MBR, serveur commun modifié) : `blktest usb0 all -w` sans échec.

### Premier essai sur le ZBook (à faire par l'utilisateur, d'abord en lecture seule)

Rien ne s'écrit sur le NVMe tant qu'on ne lance pas `nvme` sans `-r`. Avant tout : vérifier sous Windows que la
partition KORIN OS est bien en **FAT32** avec ce nom de volume, et noter son GUID (`Get-Partition -DiskNumber 0 |
Format-List PartitionNumber,Guid,Type,Size`) ; BitLocker : le volume C: chiffré ne gêne pas (lecture seule, refusé
de toute façon) ; **démarrage rapide de Windows éteint** (sinon Windows peut garder en mémoire un état de la partition
FAT32 et ne pas voir ce que Korin y écrit).

Préparer : `wsl sh tools/pc/build.sh`, carte ou clé de démarrage (`sd.ps1`), ou la partition KORIN OS (`korin.ps1 install`). Démarrer Korin, au
moniteur, une étape à la fois (photo de l'écran après chacune) :

1. `run pcid -l` : la ligne du NVMe, `… 1c5c:… 01/08/02` (noter l'adresse) ;
2. `run pcid -n` ;
3. `run nvme -d` : **diagnostic** (rien de servi) : modèle « SK hynix PC711 … », version, `CAP`, `MDTS`, cache,
   espaces de noms et formats (attendu : blocs de 512 ; si 4096 : le pilote refuse, me le dire) ;
4. `run nvme -r` : **tout en lecture seule** : la GPT (CRC juste), p1 EFI FAT32, p2 MSR, p3 NTFS (ou BitLocker),
   p4 récupération, p5 « KORIN OS » FAT32, « règle 1 : p5 serait permise en écriture, mais -r » ;
5. `run blktest nvme0 info`, `run blktest nvme0p4 all` (**sans** `-w`), `run blktest nvme0p3 read 0 1` ;
6. `run vfs SD0 blk.korin fatfs`, `urun ls -l SD0:/` (en lecture seule : fatfs voit « lecture seule ») ;
7. seulement si tout est juste : redémarrer, `run pcid -n`, `run nvme` (p5 en lecture et écriture : vérifier la
   ligne « écriture permise sur 1 partition(s) »), `run vfs SD0 blk.korin fatfs`, `urun cp SD0:/… SD0:/essai.txt`,
   `urun diff …`, puis `run nvme stop` (arrêt propre) avant d'éteindre ; sous Windows, `essai.txt` doit être lisible
   et `chkdsk D:` (lecture seule, sans `/f`) propre.

Ne **jamais** lancer `blktest … all -w`, `deny -w` ni `safe -w` sur le vrai disque (ce sont des essais d'image).

### Reste

1. **Unifier les deux serveurs de blocs** : `src/svc/sdmmc/blkserv.c` (sdhci, rtsx) a encore ce que le commun n'a pas :
   pilote par structure avec contexte (`struct blk_driver`, pas de variables globales), **support retiré puis remis**
   (`check()`, table relue, fenêtres mises à jour, nouvelles partitions), premier nom libre `sdN`, compteurs du
   pilote ; le commun a ce que l'autre n'a pas : `BLK_READ_D` par les pages de l'appelant (DMA direct), crochets
   (table, refus, signaux), CRC de la GPT, `blk_type_ro` par type MBR, DMA/UHS. À faire : un seul `blkserv.c` dans
   `src/svc/common` avec la structure du pilote de `sdmmc` et les ajouts du commun, puis sdhci et rtsx dessus.
   Même numérotation GPT (rang d'entrée + 1) des deux côtés déjà.
2. Plusieurs files d'E/S (une par cœur, vecteurs MSI-X par file), plusieurs espaces de noms servis (`nvme0n2`),
   blocs de 4096 (lecture-modification-écriture), `DSM`/TRIM, gestion d'énergie (APST), délai et reprise d'une
   commande perdue (aujourd'hui : erreur, et contrôleur déclaré mort si `CFS`).
3. L'extinction et le redémarrage du PC (init2) doivent appeler `nvme stop` (cache vidé, `CC.SHN`) ; d'ici là, le
   thread de vidage limite la perte à une seconde d'écritures.
4. `fatfs` et vfs : attente plus longue de `blk.korin` au démarrage si le vrai contrôleur est lent.
5. Le paquet `armpi` porte `etc/drivers.txt` (une ligne de plus) et `etc/nvme.txt` est nouveau : version à monter
   au prochain paquet (`tools/pkg/versions.ini`), comme pour les lignes `pci:` précédentes.

## Périphériques de blocs : blkdev et modules (décision du 12/11)

Modèle retenu, **quand tout sera stable** (pas pendant le portage) : un serveur générique `blkdev` porte la couche
d'abstraction (protocole `blk.*`, partitions MBR et GPT, bornes, clients, protection en écriture, retrait du
support) ; hwmanager (ou usbd) en lance une instance par périphérique trouvé, qui charge le **module** du pilote
matériel (nvme, sdhci, rtsx, usbstorage, imgblk) : une bibliothèque partagée exportant une petite table (`init`,
`read`, `write`, `flush`, `present`), figée comme une table de kit et chargée comme les kits (`LIB_OPEN`). D'ici là :
un pilote complet par périphérique, mais une seule copie de `blkserv.c` (réunir `src/svc/common` et
`src/svc/sdmmc`), déjà écrite dans la forme du modèle (le pilote donne une structure de fonctions, la couche commune
fait la boucle de service).

Même modèle, plus tard, pour les autres familles de pilotes (remarque de l'utilisateur, 12/11) : **le son**
(sorties USB, PWM, HDMI du Pi, HDA du PC ; entrées I2S, USB : aujourd'hui chaque pilote inclut `src/svc/sndif`, qui
l'inscrit auprès du service `sound`) et **le réseau** (genet, Wi-Fi, usbnet, plus tard AX201 : `src/svc/netif` vers
netd). Un serveur générique par famille (`blkdev`, `snddev`, `netdev`), une instance par périphérique, le pilote en
module ; d'ici là, une seule copie de chaque couche commune (`blkserv`, `sndif`, `netif`), écrite dans la forme du
modèle. Les pilotes propres au Pi (pwmaudio, hdmiaudio, i2smic, genet, wifi) restent en asm et gardent leur forme
actuelle.

### Couche commune des périphériques de blocs : une seule copie (12/11)

Les deux copies de `blkserv.c` sont réunies dans **`src/svc/common/blkserv.c`** (+ `.h`) ; `src/svc/sdmmc/blkserv.c`
est supprimé (point 1 du « Reste » de NVMe fait). Les cinq pilotes s'en servent : `imgblk`, `usbstorage`, `nvme`,
`sdhci`, `rtsx` (`svc.conf` : `../common/blkserv.c`) ; `blktest` n'en a pas besoin (client). Le protocole `blk.*`
(`src/inc/blk.inc`, `src/svc/inc/blkserv.inc` du Pi) ne change pas.

Forme du modèle futur : le pilote remplit une **`struct blk_driver`** et appelle **`blk_serve(&d)`**, qui ne revient
pas ; la couche ne sait rien du matériel. La structure est documentée en tête de `blkserv.h` comme une **future table
de module** (champs ajoutés à la fin seulement, `version` = `BLK_DRIVER_V1`) :

| Champ | Rôle (0 : pas fourni) |
|---|---|
| `version`, `sector_size`, `ctx`, `name` | version de la table, 512 seulement, contexte passé à chaque opération, « sd0 », « nvme0 »… |
| `sectors`, `read_only`, `type_ro`, `poll_ms` | capacité d'un support fixe (0 : inconnue), tout en lecture seule, type MBR protégé, intervalle de `present` (500 ms) |
| `read`, `write` | secteurs du disque (la couche ajoute le début de la partition) ; sans `write` : tout en lecture seule |
| `flush`, `stop` | `SVC_SIGNAL` `BLK_SIG_FLUSH` (2) et `BLK_SIG_STOP` (1, puis fin du pilote) : « nvme flush », « nvme stop » |
| `present`, `capacity` | support amovible : présence (et « autre support ») toutes les `poll_ms`, capacité (0 : absent) ; sans `present` : support fixe, le thread principal sert le disque comme l'asm |
| `read_sg`, `dma`, `uhs`, `stats` | `BLK_READ_D` (pages de l'appelant ; sinon `E_AGAIN`), `BLK_DMA`, `BLK_UHS` (sinon 0), `BLK_STATS` (sinon `E_INVAL`) |
| `table`, `denied`, `signal`, `log` | toute la table avant de servir (lecture seule, nom de plus `blk.<alias>`), écriture refusée, autres bits de signal, une ligne du journal de la couche |

Ce que la couche réunit : MBR et logiques, GPT avec CRC et numéros d'entrée, 16 partitions, noms de plus, clients
et tampons, bornes, lecture seule, `BLK_READ_D` par `SVC_MAP_CALLER` et `SVC_CALLER_PHYS`, retrait (tout en `E_IO`)
et remise (table relue, fenêtres mises à jour, nouvelles partitions, disparues en `E_IO`), `blk_free_name`. Pris à
l'ancienne copie de `sdmmc` pour tous : disque sans table reconnu aussi par exFAT et FAT12/16, partition hors du disque
ignorée ou coupée, tampon d'un client terminé démappé seulement si plus personne ne le tient, lecture et écriture de
0 secteur sans appel au pilote. Gardé de l'asm pour tous : taille 0 au `BLK_ATTACH` (pas de fenêtre), lecture seule
vérifiée avant le nombre, `BLK_STATS` hors verrou, fin de la chaîne des EBR. La carte SD : `sd_blk_driver`
(`sdcard.c`) remplit les opérations communes aux hôtes (erreurs de la carte en `E_IO` sauf `E_PERM`/`E_INVAL`,
`BLK_STATS` = commandes, journal par `sd_log`), l'hôte donne `present` (rtsx : surintensité, retrait, remise) ; sdhci
remplace `read`, `write`, `read_sg` (retour au PIO). Le verrou `sd_lock` de `sdcard.c` est retiré (c'est celui de la
couche). Écart : un `SVC_SIGNAL` sur sdhci ou rtsx n'a plus de réponse `E_INVAL` (comme nvme).

**Vérifié** (12/11, images d'essai supprimées ensuite) :
* PC, QEMU q35 : `sdhci` sur une carte de 64 Mo (MBR), une de 64 Mo avec étendue et logiques p5, p6 (et une p4 hors
  du disque, ignorée) et la GPT de `mkgpt.py` (contrôleur v3, ADMA2 64 bits) : `blktest all -w` 0 échec (mêmes
  sommes, ≈ 124 Mo/s, 5 222 commandes comme avant), retrait puis remise (`E_IO`, puis table relue, de nouveau sans
  échec), échange GPT → MBR → GPT à chaud (p2-p4 fermées, p5 et p6 déplacées, puis rouvertes) ; chaîne
  `hwmanager` → `pcid` → `sdhci` → `vfs SD0 blk.sd0p1 fatfs`, `cp` et `diff` justes ; `rtsx` : « aucun lecteur
  Realtek connu », fin propre ;
* PC : `nvme` sur `mkgpt.py` : règles, `blk.korin`, garde (14 essais), `all` (≈ 727 Mo/s), `nvme0p5 all -w`, `deny -w`
  sur `nvme0` et p1-p4 (15 refus dits), `nvme flush`, `vfs SD0 blk.korin fatfs` + `cp` 3 Mo + `diff` (code 0),
  `nvme stop` (arrêt propre) ; `-G` : 6 écritures refusées par la garde, `SYS:` (p1) monté, écritures de fatfs
  refusées ; sommes : seule p5 change (et rien avec `-G`) ;
* PC : `usbstorage` par `usbclasstest` (montage, 200 secteurs écrits et remis, bornes), `blktest usb0 all -w` 0 échec,
  retrait à chaud ; clé GPT : p1-p5 servies, sommes inchangées ; `ktest` 47/47, `ptest` 22/22, `itest` 29/29,
  `mtest` 19/19, `btest` 10/10, `wtest` 12/12 ;
* Pi, QEMU raspi4b (emplacement 3, carte tirée de `build/sdroot`, `imgblk` et `usbstorage` nouveaux) : IMG0:, SD1:
  (image exFAT d'essai), USB0: montés ; `ramtest IMG0`, `exfattest` (0 échec), `fstest2` (0 échec) ; `imgblk` sur
  l'image à logiques et sur la GPT, `blktest` (lancé par `services.txt`) : partitions justes, `img3p5 all -w` et
  `usb0 all -w` 0 échec ; clé : `ls`, `cp` + `diff`, écriture relue, `eject`.

**Pas vérifié** : `rtsx` sur le ZBook (il passe par `sd_blk_driver` et son `present`, relus avec soin, construit
seulement) ; `sdhci` sur le vrai Pi ; le vrai NVMe ; une clé retirée pendant un transfert.

## Démarrage jusqu'au shell (13/11)

Le PC démarre maintenant **sans le moniteur**, jusqu'à un shell texte sur la partition KORIN OS : chargeur →
noyau → `init` (initrd) → `pcid`, `nvme`, `vfs SD0 blk.korin fatfs` → `SD0:/sys/svc/init2` → busd, hwmanager,
hidmanager, annonces du PCIe → services (vterm, **ps2kbd**) → **console** (sh sur un pseudo-terminal). Le
moniteur du noyau reste, sur option.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/ps2kbd/ps2kbd.c` | le clavier PS/2 (i8042, le clavier interne du ZBook) en service, PC seulement (`svc.conf` : `arch = x86_64`) : ports par `IO_PORT`, IRQ 1 par `IRQ_BIND` / `IRQ_WAIT` (ligne de l'IOAPIC tirée de la MADT : remplacement de source ISA 1 ; 50 ms d'attente au plus, le contrôleur est relu à chaque réveil : une IRQ sur front perdue ne bloque pas ; IRQ refusée : sondage à 10 ms), jeu 1 → codes HID (préfixes E0, Pause E1 ignorée, fausses Maj ignorées, octets de la souris ignorés), modificateurs comme un rapport USB, appuis répétés du clavier ignorés (hidmanager répète) ; s'inscrit auprès de hidmanager **comme usbkeyboard** (`HID_OP_PILOTE`, même anneau, `EV_KEY` code / appui / modificateurs). Disposition (FR, BE, US, touches mortes) : hidmanager, `keyb=` de `SD0:/etc/options.txt` |
| `src/inc/syscalls.inc`, `src/kernel/core/hw.c`, `src/kernel/arch/pc/ps2.c` | **`IO_PORT` (0x4F)** : un octet lu ou écrit ; droits de `SD0:/sys` ; liste fermée de l'architecture (PC : `0x60`, `0x64`) ; `E_BUSY` tant que le noyau tient l'i8042 ; Pi : `E_NOSYS` (table de l'asm). `docs/ABI.md` |
| `src/kernel/arch/pc/apic.c` | la ligne ISA 1 est aux pilotes, sauf quand le moniteur tient le clavier (`ps2_owned`) |
| `src/kernel/arch/pc/pc_main.c` | option **`monitor`** (`cmdline` de `\korin\boot.cfg`) : le noyau prend le clavier PS/2 et lance le moniteur et `hello` (pas `init` : `run init` à la main) ; sans elle, `init`, et un thread de secours : si `SD0:` n'est pas servi en 45 s (`BOOT_READY`), « SD0: n'est pas venu en 45 s : moniteur de secours », clavier PS/2 et moniteur |
| `src/kernel/core/proc.c`, `svc.c` | **entrées / sorties données au lancement** : `SPAWN_IMAGE` / `SPAWN_PRELOADED` mettent entrée, sortie, erreur des params dans l'avis de naissance (c, d, données ; comme `svc_naissance` du Pi) et leur masque dans `PROC_FLAGS` ; avant, le noyau du PC les perdait : un `sh` lancé sur un pty ou des tubes ne voyait pas son entrée (`V_STDIO` vide) et s'arrêtait aussitôt |
| `src/kernel/core/console.c` | curseur (souligné sous la prochaine case), saut de page (`\f`) = écran effacé |
| `src/svc/console/console.c` | la **console texte** (commune, construite aussi pour le Pi, qui ne la lance pas) : un pty de vterm, `SD0:/bin/bash` sinon `SD0:/bin/sh` sur l'esclave, dans un groupe à lui au premier plan (comme telnetd) ; abonnée de hidmanager, touches traduites comme le Terminal (`vt_keys.inc` : Entrée CR, Retour arrière DEL, Ctrl+lettre par la place HID, flèches, F1-F12, Alt gauche = ESC devant) écrites sur le maître : vterm fait l'écho, l'effacement, Ctrl+C (`SIGINT` au premier plan) ; la sortie du maître à la console du noyau (`SYS_PRINT`) : `ESC [ 2 J` / `3 J` → saut de page (« clear »), les autres séquences retirées ; taille du pty = grille de la console du noyau (`SCREEN_INFO`, police 8 × 16, doublée dès 2560 points) ; sh fini → « [shell fini] » et un nouveau (2 s d'attente s'il a vécu moins de 2 s) ; un fil, sondage à 10 ms |
| `src/svc/init2/init2.c`, `init2.h`, `init2_aarch64.c`, `init2_x86_64.c` | deux crochets de la couche : `plat_buses` (Pi : `usbd`, comme avant ; PC : attend hwmanager, puis `PCI_OP_ANNOUNCE` à pcid) et `plat_text_console` (Pi : 0, Elegant lit `shell=` lui-même ; PC : 1 si `shell=console` dans `SD0:/etc/system.ini` ou sans `SD0:/sys/elegant`) ; console : pas d'Elegant, `services.txt`, puis `SD0:/sys/svc/console` surveillé (relancé sur une erreur), ni attente de « ws » ni `startup.txt` |
| `src/svc/pcid/pcid.c`, `pci.h` | **`PCI_OP_ANNOUNCE` (9)** : annonce à hwmanager les fonctions qu'aucun pilote ne tient, **sans relire le bus** (`PCI_OP_RESCAN` remesure les BAR, décodage coupé : dangereux pour le NVMe qui sert `SD0:`) |
| `skel/pc/etc/` | réglages du PC : `init.txt` (dans l'initrd : `pcid -n`, `nvme`, `vfs SD0 blk.korin fatfs`), `mounts.txt` (`RAM0 = ram:256M, ramfs`, `SD1 = blk.korin, fatfs, /home`), `services.txt` (vterm, ps2kbd), `system.ini` (`shell=console`), `options.txt` (`#keyb=BE`) |
| `tools/pc/build.sh` | `init` toujours dans l'initrd (plus de `PC_INIT`), `init.txt` aussi (`PC_INIT_TXT=fichier` pour un autre) ; le `boot.cfg` de QEMU (`build/pc/esp`) a `cmdline = monitor` : les essais au moniteur (`run ktest`…) ne changent pas (un `boot.cfg` fait par ce script sans `cmdline` est refait) |
| `tools/pc/sysroot.sh` | **`build/pc/sysroot`** (WSL, après `build.sh`) : `bin/` (build/bin/x86_64), `sys/svc/` (build/svc/x86_64), `lib/` (build/kits/x86_64, comme `SD0:/lib` du Pi), `etc/` (de `skel/sd0/etc` : paths, drivers, nvme, exec, termcap ; puis `skel/pc/etc`), `res/keyb/`, `res/tz.txt`, `home/` vide ; ni apps graphiques, ni GCC, ni `etc/pkg` (dépôt du Pi). 170 fichiers, ≈ 2 Mo |
| `tools/pc/korin.ps1` | `install` copie aussi l'arbre (place vérifiée avec lui) et écrit la liste dans **`\korin\installed.txt`** ; une installation suivante ne retire **que** les fichiers de cette liste qui ne sont plus dans l'arbre (puis leurs dossiers vides) ; `etc\options.txt` n'est écrit que s'il manque (vos réglages), `\home` est créé s'il manque et jamais touché ; `remove` retire les fichiers de la liste (sauf `etc\options.txt`), leurs dossiers vides et la liste ; `show` : nombre de fichiers installés par dossier, absents, `\home`, lignes actives d'`options.txt` ; `-NoSystem` : comme avant (chargeur, noyau, initrd) ; `-DryRun` comme avant |
| `tools/pc/mkkorindisk.py` | `--layout zbook` (p1 EFI, p2 MSR, p3 « C: », **p4 KORIN OS**, p5 récupération, attributs `8000000000000001`), `--sysroot DIR` (l'arbre à la racine de KORIN OS) |
| `tools/pc/qemu_nvme.py`, `qemu_pc.py` | `--boot-disk` (OVMF démarre du disque NVMe lui-même, `startup.nsh` → `korin.efi`, comme F9), `--snapshot`, étapes `key:` (sendkey) et `shot:` ; `<` et `>` dans la frappe AZERTY |

### Décisions

* **Accès aux ports : un appel système, pas le bitmap d'E/S du TSS.** Le bitmap demanderait un TSS par process
  (ou sa recopie à chaque bascule, sur chaque cœur) et donnerait au process l'accès direct, sans que le noyau sache
  qui tient le contrôleur. L'appel est plus simple, sûr (liste fermée de deux ports, droits de `SD0:/sys`), et
  permet au noyau de refuser (`E_BUSY`) quand le moniteur tient l'i8042 : jamais deux lecteurs. Coût : un appel
  par octet, négligeable pour un clavier.
* **Qui lance ps2kbd : init2, par `SD0:/etc/services.txt` du PC** (surveillé, relancé sur une erreur), comme le dit
  `drivers.txt` pour un matériel qu'on ne détecte pas : ni pcid ni usbd ne voient l'i8042. ps2kbd s'arrête sans
  erreur (code 0, pas de relance) s'il n'y a pas de contrôleur ou si le moniteur le tient. Plus tard : une
  découverte ACPI (`PNP0303`, HPQ8002 sur le ZBook) dans hwmanager.
* **Le moniteur** n'est plus lancé que par l'option `monitor` (ou en secours) ; il sera retiré plus tard.
* **Pas d'Elegant sur le PC** : la décision est dans la couche du PC d'init2 ; le Pi garde exactement son ordre
  (busd, hwmanager, hidmanager, usbd, Elegant, `services.txt`, « ws », `startup.txt`).
* **pcid** est lancé par init avec `-n` (hwmanager n'existe pas encore) ; init2 lui demande les annonces quand
  hwmanager tourne. `nvme`, déjà lancé par init et qui tient sa fonction, n'est pas annoncé (pas de second pilote).
* **`HOME:` / `SD1:`** : le dossier `/home` de KORIN OS (`rootpath`), même service fatfs que `SD0:`.
* La disposition du clavier des programmes est celle de hidmanager : `keyb=BE` dans **`SD0:/etc/options.txt`**
  (`D:\etc\options.txt` sous Windows) ; `keyb=BE` de `boot.cfg` ne vaut que pour le moniteur du noyau.

### Vérifié (QEMU q35 + OVMF, 4 cœurs, 13/11)

Disque de `mkkorindisk.py --layout zbook --no-windows --sysroot build/pc/sysroot`, en **NVMe**, démarré de lui-même
(`qemu_nvme.py --boot-disk`) : `startup.nsh` → `korin.efi` sur KORIN OS → noyau → `init : pcid -> 2`, `nvme -> 3`,
`vfs -> 4` ; nvme : GPT juste, **p4 « KORIN OS »** en lecture et écriture et en `blk.korin`, p5 récupération en
lecture seule, garde 14 essais 0 erreur ; `vfs : montage SD0 -> 0`, `ram0 -> 0`, `sd1 -> 0` ; `options.txt` au noyau,
`BOOT_READY` ; init2 : busd, hwmanager, hidmanager, « pcid annonce le PCIe a hwmanager -> 5 » (pont, affichage,
ISA, SATA, SMBus : aucun pilote ; le NVMe n'est pas annoncé), « shell=console : console texte », vterm,
« ps2kbd : clavier PS/2 inscrit auprès de hidmanager, IRQ 1 sur la ligne 1 », console. Au clavier PS/2 de QEMU
(AZERTY de hidmanager) : `ls -l SD0:/` (EFI, bin, etc, home, korin, lib, res, sys), `cat SD0:/etc/system.ini`,
`echo bonjour > SD1:/t.txt` puis `cat SD1:/t.txt` → `bonjour`, `uname -a` (« x86_64 … 4 coeurs »), `ps` (pcid,
nvme, vfs, fatfs, ramfs, init2, busd, hwmanager, hidmanager, vterm, ps2kbd, console, sh), `sleep 100` puis
**Ctrl+C** → `^C`, fin de `sleep` (code -2), `echo fin` ; Retour arrière (`echo b⌫x` → `x`), Maj (majuscules,
chiffres), `exit` → « [shell fini] », un nouveau sh ; curseur visible (captures `build/pc/shell_t3b.png`).
* Option `monitor` (même disque, `cmdline = monitor`) : « Options : monitor », « Clavier PS/2 : prêt pour le
  moniteur », `hello`, `run ktest` **47 ok, 0 échec**, `run ps2kbd` → « l'i8042 est au moniteur du noyau » (code 0),
  `run pcid -n`, `run nvme -r`, `run vfs SD0 blk.korin fatfs` (SD0, RAM0, SD1 montés), `urun ls SD0:/etc`.
* Secours : `build/pc/esp` sans `monitor` (pas de KORIN OS) : « vfs : montage SD0 -> -2 », « init : SD0: ne repond
  pas », 45 s puis « moniteur de secours », clavier et moniteur (`uptime`).
* `build/pc/esp` (QEMU par défaut, `cmdline = monitor`) : `run ktest` 47/47 comme avant.
* `korin.ps1` : analyse de syntaxe ; **lancé seulement en `-DryRun`**, avec des fonctions d'imitation (`Get-Volume`,
  `Get-Partition`, `Get-Disk`, `bcdedit.exe`) qui désignent une fausse KORIN OS (lecteur PowerShell sur un dossier) :
  `install` (170 fichiers : bin 111, sys 34, lib 10, etc 10, res 5, `New-Item \home`, la liste), `show`, `remove`
  (163 retraits, `etc\options.txt` gardé) ; les fonctions de l'arbre (`Install-System`…), tirées du fichier sans le
  lancer, sur ce dossier : première copie, puis un arbre réduit (sans `bin\cat`, `res\keyb`, `res\tz.txt`) : ces
  fichiers retirés et `res` vide retiré, `options.txt` modifié gardé, un fichier étranger de `bin` et
  `home\perso.txt` intacts.
* Pi (rien ne change) : `check_constants.py` passe ; `wsl sh tools/svc/build_c.sh` (tous, aarch64 et x86_64)
  passe ; QEMU raspi4b (emplacement 2, carte tirée de `build/sdroot` avec les services C de la liste `$svc_c` et
  le nouvel init2) : même ordre au journal (busd, hwmanager, hidmanager, **usbd**, elegant, `services.txt`), bureau
  Givre affiché (capture).

### Pas vérifié

* Tout sur le **vrai ZBook** : l'i8042 de l'EC d'HP (HPQ8002) piloté depuis un service (même séquence que le
  `ps2.c` du noyau, qui y marche), sa ligne d'IRQ d'après la MADT d'HP, le délai de `nvme` avant que fatfs
  abandonne `blk.korin` (3 s : sinon le moniteur de secours vient au bout de 45 s), `SD1:` sur `/home`, le
  framebuffer en 1920 × 1080 (240 × 66 cases).
* Une **extinction propre** : init2 du PC ne sait pas encore arrêter ni redémarrer (`nvme stop`) ; le thread de
  `nvme` vide le cache du disque une seconde après la dernière écriture : attendre quelques secondes avant
  d'éteindre (bouton d'alimentation tenu).
* `exec` (libposix) avec des entrées / sorties nouvelles : le noyau du PC n'envoie pas encore l'avis
  `SVC_OP_EXEC` à vfs (`exec_stdio` du Pi).
* Les lignes « [process N (nom) fini, code 0] » du noyau s'affichent au milieu de la console (journal du noyau sur
  le même écran) ; les messages des services aussi (`SYS_PRINT`).
* Un clavier USB sur le vrai xHCI du ZBook avec cette chaîne (usbd lancé par hwmanager après `PCI_OP_ANNOUNCE`,
  un usbd par contrôleur : le second refuse le service « usb ») ; la disposition `keyb=BE` dans `options.txt` avec
  ps2kbd (même chemin que usbkeyboard, vérifié pour l'USB).

## Souris et USB à deux contrôleurs (13/11)

Sur le vrai ZBook (journal du 13/11), hwmanager lançait un usbd par xHCI : `0000:00:0d.0` (8086:9A17,
Thunderbolt / Type-C) prenait le service « usb », le second (`0000:00:14.0`, 8086:43ED, ports USB-A) échouait
(« service usb refusé (-17) »), 3 relances, abandon : les ports USB-A ne marchaient pas. Et le bureau du PC
(Elegant, porté à côté) a besoin d'une souris avant le pilote I2C du touchpad.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/usbd/xhci.c` | tout l'état d'un contrôleur dans `struct xhc` (registres, zone DMA, anneaux, slots, tuyaux, bulk sans attente) ; les fonctions travaillent sur le contrôleur courant `X`, choisi à l'entrée par la poignée ; **`xh_add`** ajoute un contrôleur (4 au plus), `xh_poll` les parcourt tous, `xh_polling`, `xh_no_irq` ; messages « xhci N : branchement port… » |
| `src/svc/usbd/usbd.c`, `usbd.h` | poignée de périphérique = **contrôleur << 8 \| slot** (tuyau d'interruption : contrôleur << 8 \| numéro), opaque pour usbd.c ; les numéros de périphérique (1..16), le protocole des pilotes de classe et les annonces à hwmanager ne changent pas. **`USB_OP_ADD_CONTROLLER` (10)** : une fonction PCIe (texte « SSSS:BB:DD.F » en +48) → 0, `E_EXIST` (déjà servie), `E_INVAL` (vide) ; au démarrage, si `SVC_REGISTER "usb"` rend `E_EXIST` et qu'une adresse est donnée, usbd la **confie à l'usbd en service** et s'arrête (code 0 ; `E_EXIST` si elle est déjà servie ; 1 sinon). Toutes les lignes MSI-X sont liées au même bit du canal (le signal fait vider tous les contrôleurs) |
| `src/svc/usbd/plat_x86_64.c` | une `pci_info` par contrôleur (`xh_plat.priv`, `irq_setup(p)`) ; la même fonction deux fois → `E_EXIST` ; une fonction qui n'est pas un xHCI (`0c/03/30`) ou dont la BAR0 ne se mappe pas est **rendue à pcid** |
| `src/svc/usbd/plat_aarch64.c` | un seul contrôleur (VL805 ou RP1) : `E_EXIST` au second appel ; la carte du Pi garde `usbd.asm` (`usbd` n'est pas dans `$svc_c` de `build.ps1`) |
| `src/svc/pcid/pcilib.c`, `pci.h` | `pci_release` (`PCI_OP_RELEASE`) |
| `src/svc/hwmanager/hwmanager.c` | un pilote PCIe qui s'arrête avec **`E_EXIST` (-17)** n'est pas relancé (« déjà servi, pas de relance ») ; les autres erreurs : 3 relances comme avant. Rien ne change pour le Pi (pas de `pci:`) |
| `src/svc/ps2kbd/ps2kbd.c` | **la souris PS/2** (port auxiliaire de l'i8042) dans le même service que le clavier : un seul propriétaire des ports 0x60 / 0x64, un verrou autour de la lecture du contrôleur (bit 5 de l'état : octet de la souris), IRQ 12 d'après la MADT (remplacement de source ISA 12) dans un second thread (`IRQ_WAIT`, 50 ms au plus ; sondage à 10 ms si l'IRQ est refusée). Initialisation : `A8` (port 2), configuration relue (horloge du port 2 restée coupée → pas de port 2), `FF` → `FA AA 00`, essai IntelliMouse (fréquences 200, 100, 80 puis `F2` : identifiant 3 → paquets de 4 octets avec la molette, sinon 3), 100 paquets/s, `F4`, IRQ 12 et horloge du port 2 dans la configuration. Paquets : resynchronisation sur le bit 3 de l'octet 0, paquet coupé oublié après 100 ms, débordement → déplacement nul. Événements **comme usbmouse**, dans le même anneau que le clavier (hidmanager n'en donne qu'un par process) : `EV_MOUSE_REL` (dx, dy vers le bas, boutons bit 0 gauche, 1 droit, 2 milieu), `EV_WHEEL` (> 0 vers le haut, comme la molette USB) |
| `skel/pc/etc/services.txt` | commentaire (ps2kbd porte la souris ; aucun nouveau service) |
| `tools/pc/qemu_nvme.py` | **`--qemu "ARGS"`** : arguments de plus pour QEMU (deux `qemu-xhci`, périphériques USB…) |

`drivers.txt` ne change pas : `pci:0c/03/30 = SD0:/sys/svc/usbd` reste ; la ligne rtsx reste en commentaire.

### Décisions

* **Le second lancement confie son adresse au premier** (plutôt qu'un usbd qui prendrait lui-même tous les xHCI de
  pcid) : l'ordre des annonces de pcid ne compte pas, hwmanager ne change pas (un pilote par fonction, lancé avec
  son adresse), et la fonction est prise par l'usbd qui la sert (pcid la lui rend s'il s'arrête).
* **Un état par contrôleur et un contrôleur courant** plutôt qu'un pointeur passé partout : xhci.c garde ses
  fonctions telles quelles (`X->…`), usbd est d'un seul fil ; `slot_free` remet `X` après `usbd_detach`.
* **La souris dans ps2kbd**, pas un service `ps2mouse` à part : un seul process tient l'i8042 (les octets des deux
  ports arrivent par le même port 0x60 : deux lecteurs se voleraient leurs octets).
* Le geste « double toucher = début du glisser » est pour le futur pilote I2C du touchpad, pas pour ce PS/2.

### Vérifié (QEMU q35 + OVMF, 4 cœurs, 13/11)

Disque de `mkkorindisk.py --layout zbook --no-windows --sysroot build/pc/sysroot` en NVMe, démarré de lui-même
(`qemu_nvme.py --boot-disk --snapshot`), deux contrôleurs : `--qemu "-device qemu-xhci,id=xa … -device
qemu-xhci,id=xb …"` (image supprimée ensuite) :

* pcid voit `0000:00:03.0` et `0000:00:04.0` (1b36:000d, MSI-X) ; hwmanager lance usbd pour chacun ; le second :
  « usbd : 0000:00:04.0 confié à l'usbd en service », `[process 12 (usbd) fini, code 0]`, **pas de relance** ;
  l'usbd en service : « un autre xHCI : 0000:00:04.0 », « interruption sur la ligne 258 » (la première : 257) ;
* clavier USB sur `xa`, tablette et clé sur `xb` au démarrage : « xhci 0 : … 03/01/01 → usbkeyboard », « xhci 1 :
  … 08/06/50 → usbstorage », « xhci 1 : … 03/00/00 → usbgamepad » ; touches du clavier USB reçues
  (`usbclasstest watch` : `EV_KEY` 'p', 's' ; commandes tapées dans la console) ; `blktest usb0 read 0` sur la clé
  du second contrôleur : 1 secteur, 0 échec ;
* **branchement à chaud** : une souris USB sur `xa` (« xhci 0 : branchement port 6 » → usbmouse), puis sur `xb`
  (« xhci 1 : branchement port 6 » → usbmouse ; `mouse_move 7 -3`, bouton 1 → `EV_MOUSE_REL 7 -3 0`, `… 0 0 1`) ;
  une clé FAT (image GPT) branchée à chaud sur `xb` : « montage de USB0 blk.usb0p1 fatfs -> 0 », `ls USB0:/` →
  `EFI` ;
* **retraits** : souris de `xb` (usbmouse fini, code 0), clé de `xb` (« cle retiree, volume demonte », usbstorage
  fini, `ls USB0:/` inaccessible), clavier de `xa` (usbkeyboard fini) : chaque fois sur le bon contrôleur
  (« xhci 1 : retrait port … », « xhci 0 : retrait port 5 ») ;
* à la main dans la console : `usbd 0000:00:04.0` et `usbd 0000:00:03.0` → « est déjà servi », **code -17** ;
  `usbd 0000:00:1f.3` (SMBus) → « n'est pas un xHCI », refusé (-19), code 1, fonction rendue ; ensuite une
  souris branchée à chaud sur le second contrôleur est encore vue ;
* **souris PS/2** de QEMU (sans tablette USB : `info mice` → « QEMU PS/2 Mouse » active) : « ps2kbd : souris PS/2
  (IntelliMouse, molette) inscrite, IRQ 12 sur la ligne 12 » ; `usbclasstest watch` (abonné de hidmanager, comme
  Elegant) : `mouse_move 10 5` → `EV_MOUSE_REL 10 5 0` ; `mouse_move -300 -2` → `-127 -2`, `-127 0`, `-46 0`
  (QEMU découpe) ; `mouse_button 1 / 2 / 4` → boutons 1, 2, 4 ; `mouse_move 0 0 1` → `EV_WHEEL 1`, `0 0 -2` →
  `EV_WHEEL -1` (QEMU envoie un cran) ; la molette d'une souris USB donne le même signe (`EV_WHEEL 1`) ; le
  clavier PS/2 et la console marchent comme avant ;
* `wsl sh tools/svc/build_c.sh` : usbd, hwmanager, pcid, nvme, sdhci (aarch64 et x86_64), rtsx, ps2kbd
  construits sans avertissement.

### Pas vérifié

* Tout sur le **vrai ZBook** : les ports USB-A (`00:14.0`, 8086:43ED) servis par l'usbd du Thunderbolt (ou
  l'inverse, selon l'ordre de pcid) ; deux MSI-X sur deux contrôleurs Intel ; le **touchpad en PS/2** : l'EC d'HP
  peut ne présenter aucune souris sur le port auxiliaire tant que le touchpad est en I2C-HID (alors « pas de souris
  sur le port auxiliaire », le clavier reste seul) ; un touchpad qui répond en PS/2 donnerait des mouvements
  relatifs et les boutons (pas de gestes, pas de molette à deux doigts, sauf si l'EC émule l'IntelliMouse) ; le
  PointStick pourrait aussi arriver par là.
* Sur le Pi, rien ne change : usbd.asm reste sur la carte ; le hwmanager en C ne reçoit pas de `pci:` (sa
  nouvelle branche n'est jamais prise) ; pas d'essai QEMU raspi4b de ce lot.
* L'usbd en service qui s'arrête sur une erreur : hwmanager relance celui de la première fonction ; le second
  contrôleur n'est repris qu'à la prochaine annonce de pcid (rien ne la redemande aujourd'hui).
* Un contrôleur dont l'initialisation échoue après la prise (zone DMA, `hc_init`) reste pris par usbd (sa zone
  DMA n'est pas rendue) ; une clé sans partition sur USB : usbstorage ne voit pas son retrait (comme avant, quel
  que soit le contrôleur).

## Bureau Elegant en C (13/11)

Elegant (le serveur graphique, `src/sys/elegant`, ~8 300 lignes d'asm) et les programmes de la session du bureau
(`menubar`, `panel` et son lanceur, `voronoy`, `notifyd`, `clipd`, `clockd`, ~7 500 lignes) sont réécrits en C,
**une seule fois pour le Pi et le PC**, comportement repris de l'asm (mêmes protocoles `WS_OP_*` / `WEV_*` /
`ws_ext.inc`, mêmes modes `shell=` desktop, poche, console, même `SD0:/etc/session`, mêmes thèmes par les jetons
d'UIKit, même chrome sans barre, mêmes messages au journal ; bizarreries gardées et commentées « comme l'asm »).
Sur le Pi, le C remplace l'asm sur la carte (`$sys_c` de `build.ps1`) ; l'asm reste dans le dépôt (selfhost).
Sur le PC, init2 lance Elegant (`shell=desktop`) et le bureau arrive (QEMU q35).

### Fichiers

| Fichier | Rôle (traduction de) |
|---|---|
| `tools/sys/build_c.sh` | construit les programmes C de `src/sys` (comme `tools/svc/build_c.sh`) : `build/sys/aarch64/<nom>`, `build/sys/x86_64/<nom>` ; `sys.conf` (`import`, `sources`, `arch`, `opt`) ; `SYS_COMPILE_ONLY=1` compile sans lier |
| `src/sys/common/sys_consts.h` | korinrt + svclib + `src/kits/uikit/uk_consts.h` (les constantes de l'asm en valeurs) |
| `src/sys/elegant/elegant.h`, `elegant_core.h`, `elegant_ext.h` | fiche d'une fenêtre (`struct win`), table d'un mode (`struct policy`, `POL_*`), état commun, prototypes |
| `src/sys/elegant/elegant.c` | `elegant.asm` : boucle, choix du mode (`shell=`, `options.txt`), variables |
| `core.c`, `menu.c`, `desktop.c`, `shell.c` | `core.inc` (zones sales, composition, curseur, entrées de HIDManager, fenêtres, requêtes), `menu.inc`, `desktop.inc` (session), `shell.inc` (+ `buspub.inc`) |
| `present.c`, `present_aarch64.c`, `present_x86_64.c` | `present.inc` : commun ; **Pi** : DMA 2D, boîte aux lettres, `dc cvac` ; **PC** : copie SSE2 par paquets de 64 octets vers le framebuffer GOP (écriture combinée), `sfence` |
| `uikit_ops.c`, `surface.c`, `snapshot.c`, `remote.c`, `pocket.c` | `uikit_ops.inc`, `surface.inc`, `snapshot.inc`, `remote.inc`, `pocket.inc` |
| `screen.c`, `screen_aarch64.c`, `screen_x86_64.c` | `screen.inc` ; l'EDID par le firmware (Pi) ; PC : pas d'EDID, `WS_MODE` refusé par le noyau (une résolution, celle du GOP) |
| `src/sys/menubar/menubar.c` | `menubar.asm` (relais local `call_r4_7` pour `ui_popup_ex`, 7 arguments et 3 registres rendus) |
| `src/sys/panel/panel.c`, `launcher.c`, `panel.h` | `panel.asm`, `launcher.inc` |
| `src/sys/voronoy`, `notifyd`, `clipd`, `clockd` | les `.asm` du même nom |
| `src/sys/common/` | les inclusions partagées en C : `applist`, `dockconf`, `panelconf`, `recent`, `apprun`, `wifidraw`, `sounddraw`, `alarms`, `gamesnd`, `clip.h` (pour home, dock, quick, agenda… quand ils passeront en C) |
| `src/bin/movebench` | mesure : une fenêtre de 800 × 520 déplacée 120 fois (`WS_OP_WIN_MOVE`), chaque pas attend le `WEV_FRAME` qui suit sa recomposition |
| `tools/sys/compare.sh` | comparaison asm / C sur le Pi : cartes `build/card_ws_a` / `_c`, `run NOM SCRIPT` (script de `qemu_drive.py`, captures `build/wscmp/NOM_<a\|c>_*.png`), variantes `THEME="Onyx Noir"`, `SHELL_MODE=poche` ; au démarrage `bench`, `movebench` et les lignes datées du journal (`journal`) ; sans préchargement (`preload.txt` vide) |
| `build.ps1` | `$sys_c` : les sept programmes C dans `SD0:/sys` à la place de l'asm |
| `src/svc/init2/*` | `plat_shell_lost` : PC, la console texte si « ws » ne répond pas en 20 s ou si Elegant est abandonné après 3 relances ; Pi : rien (comme l'asm) |
| `skel/pc/etc/session`, `system.ini` | session du PC (voronoy, menubar, panel, notifyd, clipd, clockd ; pas agenda, stickies, pkgd, printd) ; `shell=desktop` |
| `tools/pc/sysroot.sh` | construit `src/sys` (sauf `PC_QUICK=1`) ; met dans l'arbre `sys/<nom>`, `fonts/` (`ref/fonts`), `res/icons` (icônes sans app, de `tools/icons/rendered`), `res/lang` (`tools/lang/merge.py`), `etc/theme.txt`, `panel.ini`, `places.ini`, `fileassoc.ini`, `dock.ini` |
| `src/kernel/core/svc.c` | **correction du noyau du PC** : la shootdown du TLB des vues d'une requête (`SVC_REPLY`, `SVC_FORWARD`, fin d'un process) se fait après avoir rendu `svc_lock` |

Tailles (asm → C Pi / C PC, octets) : elegant 59 576 → 56 456 / 58 448 ; menubar 50 704 → 19 416 / 17 264 ;
panel 46 640 → 25 440 / 23 672 ; voronoy 8 432 → 5 112 / 4 608 ; notifyd 24 968 → 7 512 / 7 088 ; clipd 5 592 →
7 896 / 7 440 ; clockd 29 296 → 17 032 / 15 888 (l'asm écrit ses `.bss` dans le fichier).

### Décisions

* **Un seul C, deux couches** : ce qui ne sert qu'au Pi (DMA 2D, boîte aux lettres, EDID, cache) est dans
  `*_aarch64.c`, le PC dans `*_x86_64.c` ; le reste est commun. Elegant n'importe qu'AppKit (comme l'asm) ;
  `sys.conf` d'Elegant : `opt = -O3` (composition).
* **Noms** : ceux de l'asm, mots français traduits (`hid_abonner` → `hid_subscribe`, `win_minimise` →
  `win_minimize`, `req_mesure` → `req_measure`, `mode_bureau` → `mode_desktop`, `mode_poche` → `mode_pocket`,
  `bu_*` → `dk_*`) ; une fenêtre est un `struct win *`.
* **Le PC** : `board_pi4()` rend 0, donc le journal dit « copie vers l'écran par le processeur (pas un Pi 4) »,
  comme l'asm ailleurs qu'un Pi 4, et l'octet alpha est forcé (le GOP l'ignore).
* **Le secours du PC** : `shell=desktop` dans `system.ini` ; sans `SD0:/sys/elegant` ou avec `shell=console`, la
  console texte comme avant ; si Elegant ne répond pas ou tombe, la console texte (une fois).
* **Le Terminal** n'est pas sur le PC : c'est une app en assembleur (`src/apps/terminal`) ; les apps C/C++ du PC
  viennent de `tools/pc/apps.sh` (autre lot).

### Écarts voulus (le reste est l'asm, bizarreries comprises)

* `session_start` copie chaque ligne par défaut avant de la couper (l'asm coupait la chaîne en place : en mode
  poche, « `SD0:/sys/menubar --poche` » était suivi d'un lancement inutile de « `--poche` »).
* `req_snapshot` lit la génération demandée avant d'écrire la hauteur dans le même champ (l'asm comparait
  toujours à `scr_h` : vncd recopiait presque tout l'écran, et rien quand la génération valait la hauteur).
* menubar : `WEV_SCREEN` passe la hauteur de la barre à `ui_resize` (l'asm passait son adresse) ; longueur de
  `bus_get` bornée ; tampons des menus avec marge (128 fenêtres, menu de 4 Ko).
* panel : une catégorie 0xFF (plus de place pour « Autres ») est sautée (l'asm lisait `ca_nom[255]`).
* alarms : `al_label_copy` borné à `AL_LABEL_MAX` ; notifyd : 8 octets à 0 après le message.
* `cur_req_win` garde l'id de la fenêtre (comme l'asm), déclaré `u64`.

Bizarreries gardées (exemples) : `win_by_id` s'arrête au premier id même d'un autre pid ; le mélange alpha par
groupes de 4 pixels et son arrondi ; `resize_end` ignore x0 ; `reserve_update` ne redessine que l'ancienne place ;
`req_win_place` rend 0 quand le mode poche tient la place ; clockd : « Arrêter » sans `lang_tr` (anglais : reste
« Arrêter ») ; panel : un changement de résolution pendant le lanceur ne fait que redessiner.

### Vérifié

**Pi (QEMU raspi4b, emplacement 2, 13/11)** : `sh tools/sys/compare.sh cards`, puis `run givre`, `THEME="Onyx
Noir" run onyx`, `SHELL_MODE=poche run poche` : deux cartes tirées de `build/sdroot` (services C de `$svc_c`),
seuls les sept programmes de `SD0:/sys` diffèrent. Captures côte à côte **identiques à l'asm** (hors cellules de
voronoy, tirées au hasard, et l'heure) : bureau Givre et Onyx Noir (menubar, panel en bas, applets agenda et notes,
notification de pkgd), lanceur ouvert au panel (catégories, grille, recherche « ter » au clavier → « Aucune
application », comme l'asm en AZERTY), Terminal ouvert depuis le panel, `ls` tapé, menu « Korin OS » de menubar
(catégories, Fenêtres ouvertes), mode poche (accueil de home, horloge, récentes). Journaux de l'UART : mêmes lignes
(elegant, clockd, panel), aucune faute ni code d'erreur.

**Vitesse (Pi sous QEMU, asm / C, 1024 × 768)** — deux mesures par carte et par variante :

| | asm | C |
|---|---|---|
| composition de tout l'écran avec transparences (`bench`), Givre | 29,5 / 29,1 ms | 30,3 / 29,7 ms |
| sans transparence, Givre | 27,7 / 27,7 ms | 27,0 / 27,2 ms |
| idem, Onyx Noir (avec / sans) | 37,6 et 35,0 / 35,3 et 35,5 ms | 34,2 et 32,7 / 28,7 et 27,5 ms |
| idem, mode poche (avec / sans) | 17,0 et 19,5 / 17,1 et 17,0 ms | 18,6 et 18,6 / 16,9 et 17,6 ms |
| déplacement d'une fenêtre de 800 × 520 (`movebench`), Givre | 19,9 / 19,4 ms par pas | 19,7 / 18,7 ms |
| idem, Onyx Noir | 22,1 / 20,3 ms | 19,4 / 21,8 ms |
| démarrage : « elegant : ecran pris » (Givre / Onyx) | 10,5 / 8,8 s | 9,6 / 8,7 s |
| démarrage : « panel : bureau prêt » (Givre / Onyx) | 18,6 / 16,9 s | 16,3 / 18,2 s |

Le C va **aussi vite que l'asm** : les écarts sont dans le bruit de QEMU (± 10 % d'une mesure à l'autre sur la même
carte). Pas besoin de toucher aux options du compilateur ; pas d'asm hybride. Ces temps sont ceux de l'émulateur
(processeur, sans DMA 2D) : le vrai Pi n'est pas mesuré.

**PC (QEMU q35 + OVMF, 4 cœurs, 1280 × 800, 13/11)** : disque de `mkkorindisk.py --layout zbook --no-windows
--sysroot <arbre de sysroot.sh> --cfg "default = korin\ntimeout = 0"`, `qemu_nvme.py --boot-disk --snapshot` :
init2 → « SD0:/sys/elegant -> 11 », « elegant : copie vers l'écran par le processeur (pas un Pi 4) », « elegant :
ecran pris, en service », « clockd : alarmes lues : 0 », « panel : bureau prêt » ; à l'écran (copie de la mémoire
vidéo par `pmemsave`, voir plus bas) : fond voronoy, menubar en haut (Korin OS, réseau, horloge), panel en bas
(lanceur, espaces 1 à 4, corbeille, presse-papiers, réglages, arrêt), curseur. À la **souris PS/2** de QEMU (lot
de l'autre agent) : le lanceur s'ouvre (« Toutes », recherche, « Aucune application » : pas d'apps dans l'arbre),
« fi » tapé au **clavier PS/2** dans la recherche, Échap le ferme ; le menu « Korin OS » de menubar s'ouvre (À propos,
Fenêtres ouvertes). Avec deux apps C++ du PC dans l'arbre (`calendar.app`, `notes.app` de
`tools/pc/apps.sh`) : le lanceur les montre (« Bureautique »), « no » tapé ne garde que Notes, **Entrée l'ouvre** :
fenêtre de Notes (cadre sans barre, liste, éditeur), menubar prend ses menus (Notes, Fichier, Édition, Note,
Présentation), le menu « Note » s'ouvre à la souris, le panel montre l'app ouverte. `bench` : 4,2 ms par image avec transparences, 3,8 sans ; `movebench` : 15,8 ms par pas (une
image par rafraîchissement, `WS_FRAME_MS`). Secours : `sys/elegant` remplacé par `false` → « le shell ne repond
pas », « pas de bureau : console texte en secours », console et `echo bonjour` → `bonjour`, puis les 3 relances
d'Elegant et « abandon ».

Trouvé et corrigé pendant l'essai : **un blocage des quatre cœurs du noyau du PC** (`src/kernel/core/svc.c`) :
`SVC_REPLY` faisait `vm_tlb_shootdown` sous `svc_lock`, interruptions coupées ; un autre cœur qui attendait le verrou
ne répondait jamais à l'IPI. Elegant et ses clients au démarrage (beaucoup de requêtes avec vues mappées) le
déclenchaient presque à chaque fois (« `info registers -a` » : tous les cœurs dans `arch_cpu_relax`, l'un dans
`smp_tlb_shootdown` appelé par `sys_svc_reply`).

### Pas vérifié

* Le **vrai Pi** et le **vrai ZBook**. Sur le ZBook : le framebuffer GOP en 1920 × 1080 en écriture combinée (la
  vitesse de la copie SSE2 n'est pas mesurée sur le vrai matériel), le touchpad en PS/2 (s'il ne répond pas, pas de
  pointeur : seul le clavier marche), la hauteur des barres d'après la résolution.
* Les **captures de QEMU q35** (`screendump`) restent celles de la console texte après la prise de l'écran par
  Elegant, alors que la mémoire vidéo contient le bureau : les captures du PC sont faites par `pmemsave` de la
  mémoire vidéo (`0x80000000`) puis converties (BGRX). À comprendre (QEMU et la projection en écriture combinée ?).
* L'**horloge** de menubar montre le temps depuis le démarrage sur le PC (00:00) : comme l'asm, elle suit
  `korin.clock.offset` (publié par netd après NTP, absent du PC) ou `clock.cfg` ; il manque une source de l'heure
  (CMOS) publiée sur le bus.
* Le **Terminal** et les autres apps en assembleur ne sont pas sur le PC ; le glisser d'une fenêtre à la
  souris, le redimensionnement, les espaces de travail, le presse-papiers (clipd) et les notifications
  (notifyd) ne sont essayés que sur le Pi (par les mêmes captures que l'asm) ; sur le PC seulement ce qui est écrit
  plus haut.
* Mode poche sur le PC (home, quick, switcher sont en asm : absents).
* Le selfhost (l'asm reste assemblé, il n'est plus sur la carte).

## Chaîne x86-64 et apps C/C++ (13/11)

Les programmes C et C++ de Korin (newlib, libposix, libstdc++, les kits) se construisent pour le PC, **des
mêmes sources que le Pi**. Ni Mesa, ni Skia sur GPU, ni Avalonia, ni WebKit, ni .NET (après le pilote du GPU
Intel, décision de l'utilisateur).

### La chaîne : `x86_64-onyx-elf` (`tools/pc/toolchain.sh`)

* La recette d'`aarch64-onyx-elf-14.2` d'Onyx (son script est gardé dans
  `/opt/toolchains/aarch64-onyx-elf-14.2/share/onyx-toolchain/`), seule la cible change : binutils 2.43, GCC 14.2
  (C, C++, threads POSIX, TLS natif par `%fs`), newlib 4.4 (`--enable-newlib-reent-thread-local`…), libstdc++, le
  même correctif de libstdc++ (`tools/pc/patches/gcc-14.2.0-libstdcxx-onyx.patch`, qui vaut pour `*-onyx-*` : d'où
  le nom), les mêmes en-têtes d'Onyx (`pthread.h`, `sys/_pthreadtypes.h`, `semaphore.h`, `sys/dirent.h`,
  `sys/features.h`, repris de la chaîne AArch64 : LP64 des deux côtés). Sources de gnu.org et sourceware, SHA-256
  vérifiés (reprises de `~/.cache/armpi-gcc/dl` si elles y sont).
* Un seul écart : pour `x86_64-*-elf`, newlib prenait le cas par défaut de `configure.host`
  (`-DMISSING_SYSCALL_NAMES` : `_close_r` → `close`, pas de `libc/syscalls`) ; le script ajoute un cas
  `x86_64-onyx-*` comme `aarch64` : `close` → `_close_r` → `_close`, que fournit libposix, comme sur le Pi.
* Hôte : le GCC 15 de WSL, en `-std=gnu17` / `gnu++17` (GMP 6.3 et GCC 14 n'acceptent pas C23). 13 min sur 16
  cœurs ; `/opt/toolchains/x86_64-onyx-elf-14.2` (242 Mo) ; recette (`STAGES=check`) passée : threads POSIX,
  `_GLIBCXX_HAS_GTHREADS`, horloges, `_GLIBCXX_HAVE_TLS`, `dirent`, `_REENT_THREAD_LOCAL`, TLS natif,
  `<limits.h>` et `<pthread.h>`.

### libposix du PC (`ARCH=x86_64 sh tools/posix/build.sh`)

Les mêmes fichiers (`src/lib/posix`), des `#if defined(__x86_64__)` là où la machine compte ;
`build/pc/posix/{include,lib}`, les essais dans `build/pc/posixtest/` (`prebuilt/` reste au Pi).

| Point | Pi | PC |
|---|---|---|
| Appel système | `svc 0`, x8 | `syscall`, rax ; rdi rsi rdx r10 r8 r9 (`armpi.h`) |
| `file_sys` d'AppKit | x8 = numéro | 7e argument de System V (`armpi.c`), comme korinrt |
| Démarrage (`crt0posix.S`) | x0 = ligne | rdi = ligne, rsp aligné, `call __posix_start` |
| Pointeur de thread | `TPIDR_EL0`, écrit en EL0 | base de FS : **`SYS_TLS_SET`** (0x0E) pour le thread principal, `THREAD_CREATE_EX` pour les autres |
| Disposition TLS (`tls.c`) | variante 1 : TCB puis bloc ; `struct __posix_thread` sous TP | **variante 2** : bloc du programme sous TP (`align_up (taille, p_align)`, le calcul de ld), TCB à TP avec `%fs:0` = TP, `struct __posix_thread` après le TCB (`POSIX_TCB_SIZE`, 64) ; la réserve des modules de dlopen sous le bloc (décalages négatifs) |
| Hôte `-rdynamic` (`start.c`) | GLOB_DAT, ABS64, JUMP_SLOT | aussi `R_X86_64_TPOFF64` : ld laisse ainsi errno, stdout… exportés, lus en « initial exec » |
| dlopen (`dlopen.c`) | relocations AArch64, TLSDESC | celles de x86-64 sous les mêmes noms (`RT_*`), TLSDESC (gnu2) : `movq 8(%rax), %rax` |
| Fin d'un gestionnaire de signal | x30 → `svc` | `ret` → `__posix_sigreturn` (`syscall`), trame de `sigframe.c` |
| `<sys/ucontext.h>` | Linux arm64 | Linux x86-64 (`gregs[REG_RIP]`…, FXSAVE à +320) |
| Horloge (`time.c`) | `CNTPCT_EL0` / `CNTFRQ_EL0` | `rdtsc`, à la fréquence de **`CLOCK_GET 3`** (le TSC mesuré au démarrage) ; 0 : `TIME_MS` |
| Divers | | `pause` pour `yield`, `uname` x86_64, `syscall ()` aux numéros de Linux x86-64, `getauxval (AT_HWCAP)` = 0 |
| Spec, script de liens | | `-shared`, `-export-dynamic` passés à ld (la spec de `x86_64-*-elf` ne le fait pas) ; `*cc1plus` reçoit les options de `*cpp` (le g++ de cette cible ne passe pas `%(cpp)` à cc1plus) ; `-z noexecstack` ; `posix.ld` lié à `0x400000` comme les commandes du PC, `.plt.got`, `.iplt` |

**Le Pi n'a pas changé** : la libposix d'avant et d'après (36 objets désassemblés avec leur `.rodata`,
`crt0posix.o`, `dlmod.o`, `posix.ld`, `posix.specs`, `dlhost*.ld`) et les essais (`posixtest`, `posixtest-cxx`,
`ttytest`, `jobtest`, `dllinktest`, modules `dltest`) sont **identiques à l'octet** : un `posixtest` sous QEMU
raspi4b n'aurait rien appris de plus (il n'a pas été relancé). `emukit.so` du Pi : identique aussi.

`<armpi/syscall.h>` (nouveau, libposix) : `armpi_syscall (n, a…f)` et `armpi_relax ()` pour le C des apps, Pi et
PC ; les apps qui appelaient le noyau par `svc 0` en asm le prennent (budget, calendar, disks, help, mail, notes,
pocketqi, telegram, BASIC) ; elles se construisent toujours pour le Pi (`tools/office/build.sh`).

### Noyau du PC

* **`SYS_TLS_SET` (0x0E)** : base de FS du thread appelant (gardée à chaque commutation, reprise par `fork`) ; Pi :
  `E_NOSYS` (table de l'asm). **`CLOCK_GET 3`** : fréquence du TSC (Pi : `E_INVAL`). **`THREAD_CREATE_EX`**, TLS
  -1 : celui de l'appelant (`arch_get_tls`), pour EmuKit, qui ne peut pas lire la base de FS d'un programme
  (docs/ABI.md).
* Pile du thread principal : **64 Mo** à la demande, comme le Pi (256 Ko avant : trop peu pour les apps C++).

### Les kits depuis le C (`tools/kits/abi2c.py --arch x86_64`)

Mêmes en-têtes `<kit>_abi.h`, relais `jmp *imports(%rip)`. Les fonctions « ret » d'UIKit (`ui_w_event`,
`ui_list_get`, `ui_surface_pixels`… : `kit.conf`) rendent en C du kit une structure de quatre `long` en mémoire ;
leur relais la reçoit sur sa pile, rend r0 et garde r0..r3 dans `uikit__ret` (`__thread`), où `uikit_call3`
d'`uikit.h` lit les autres valeurs (le chemin AArch64 ne change pas). Sortie : `build/pc/gcc/include/kits`,
`build/pc/gcc/lib/lib<kit>.a` (comme `/gcc` du Pi).

### Les apps (`sh tools/pc/apps.sh`, `build/pc/apps/status.txt`)

Mêmes sources et options que `tools/office/build.sh`. Bibliothèques tierces dans `build/pc/thirdparty` :
**OpenSSL 3.5.9** (`ARCH=x86_64 sh tools/openssl/build.sh`, cible `korin-x86_64` d'après `linux-x86_64`, avec
son assembleur x86-64 ; essais `openssltest`, `openssltest-cxx`) et **MuPDF 1.28.5** (`ARCH=x86_64 sh
tools/pdf/build.sh`, avec zlib, libjpeg et FreeType de `src/extra`). `tools/pc/sysroot_apps.sh [arbre]` (appelé par
`tools/pc/sysroot.sh` avec `PC_APPS=1`) met dans l'arbre les essais de libposix et d'OpenSSL (`bin/`), les apps
(`apps/<nom>.app/`), `bin/basic` et `lib/*.bi` : 52 Mo de plus, trop pour la partition de 65 Mo des disques de
`mkkorindisk.py` (les essais en gardent une partie).

**Construites (34)** : letters, sheet, slides, cardfile, media, circuits, basic (`bin/basic`), qbasic, qbstudio,
gpiolab, turtle, cosmozoom, irc, mail, telegram, courier, pocketqi (OpenSSL), calendar, notes, paint, pdf (MuPDF),
photos, archiver, iconedit, disks, pinball, critters, arabesque, help, imageview, screenshot, budget, ndsemu
(l'interpréteur : son JIT n'est construit que pour AArch64).

**Construites, mais un kit leur manque sur le PC** (essayées après le GPU) : media (MediaKit : `prebuilt/mediakit.so`
du Pi seulement, libvpx et dav1d à construire pour x86-64), cosmozoom et la 3D de qbasic / turtle (GpuKit : version
du PC après le pilote du GPU Intel).

**Pas construites** :

| App | Pourquoi |
|---|---|
| printd (`src/sys/printd`) | son `main.cpp` appelle le noyau par `svc 0` en asm : à passer à `<armpi/syscall.h>` (src/sys est au chantier d'Elegant) ; `apps.sh` le saute tant que la ligne y est |
| vector | Skia, HarfBuzz, libxml2 du sysroot de WebKit (`tools/webkit/build-deps.sh`) : Skia à construire pour x86-64 (rendu processeur), avec WebKit |
| koton | construit par `tools/webkit/build-koton.sh` sur le sysroot d'Onyx (FreeType…) ; ses lignes d'asm ont leur variante x86-64 (`ui/view.cpp`, `ui/audio.h`) ; reste son script pour le PC |
| gcemu, n3dsemu | Mesa (GlKit) relié en statique, JIT AArch64, `gx_asm.asm` (FASMARM) pour gcemu : après le GPU ; l'interpréteur seul demandera de séparer le rendu GL |
| n64emu | cœur en FASMARM : avec les apps asm |

### Vérifié (QEMU q35 + OVMF, 4 cœurs, disque NVMe de `mkkorindisk.py --layout zbook`, 13/11)

* `posixtest` du PC, groupe par groupe : misc **24**/0, thread **19**/0, io **9**/0, time **7**/0, file **76**/0
  (2 sautés : noms de 200 octets sur RamFS, comme le Pi), proc **14**/0, fork **15**/0 (1 sauté : pas de TCP en
  boucle locale sans netd), signal **18**/0, dl **26**/0 (modules C et C++, TLS des modules, `dllinktest` relié à un
  `.so`), ipc 27/1, xbit 30/2, alarm 12/1, exec 7/8, mem : une faute après 2 échecs ; `posixtest-cxx` **82**/0.
* `openssltest` 19/0 (SHA, AES-GCM, ChaCha20-Poly1305, RSA-2048, ECDSA, Ed25519, X25519, ML-KEM-768), `openssltest-cxx`
  (4 `std::thread`) ; `uname -a`.
* Bureau Elegant (`shell=desktop`, l'app dans `SD0:/etc/startup.txt`) : **Tableur** (sheet), **Letters**, **Budget
  mensuel**, **Bestioles** (critters) s'ouvrent, captures `build/pc/apps_g1a.png` à `apps_g4a.png`.
* `ktest` 47/0 avec le nouveau noyau ; `tools/check_constants.py` passe.

### Écarts connus (noyau du PC, pas libposix)

* **exec** (8) : le noyau du PC n'envoie pas encore l'avis `SVC_OP_EXEC` à vfs (voir « Démarrage jusqu'au shell ») :
  descripteurs au-delà de 2, `envp`, fils d'avant l'exec.
* **`mprotect` d'une projection privée de fichier ou d'objet partagé** (mem 2, ipc 1) : `sys_vm_protect` n'accepte
  que des régions anonymes (`covered (…, anon_only)`) ; à faire en gardant la copie sur écriture.
* **xbit** (2) : la règle vise `SD0:/apps/files.app`, une app asm pas encore sur le PC ; **alarm** (1) : une lecture
  de tube interrompue en 2,7 s (attendu : moins) sous QEMU.
* Pas essayé : les apps réseau (mail, irc, telegram, courier : pas de réseau sous QEMU ici), l'impression, le son,
  ndsemu (sans jeu), basic / qbasic, sur le vrai ZBook.

### Pour l'étape « GCC dans Korin x86-64 »

* La chaîne croisée est là : `tools/gcc/build.sh` (GCC canadien pour le Pi) se reprend avec `TOOL` et `TC` de
  `x86_64-onyx-elf`, `--host=--target=x86_64-onyx-elf`, sans `--with-cpu=cortex-a72` ni les retouches propres à
  AArch64 (`host_detect_local_cpu`) ; la spec de l'arbre (`mkspecs.awk`) doit garder les ajouts du PC
  (`%{shared:-shared}`, `-export-dynamic`, `*cc1plus`).
* libposix du PC (`build/pc/posix`), `dlhost*.ld`, `libgcc_mod.a`, `crt0posix.o` vont dans `/gcc/x86_64-onyx-elf/armpi`
  comme sur le Pi ; les interfaces des kits (`build/pc/gcc/include/kits`, `lib<kit>.a`) dans `/gcc`.
* GNU make (`tools/gcc/make.sh`) : à reconstruire pour x86-64 ; `pibuild.mk` n'a rien de propre à AArch64 hors
  de `fasmarm` (le `fasm` x86-64 du PC à sa place).

## GCC, FreeBASIC et fasm dans Korin PC (13/11)

Comme sur le Pi, les compilateurs tournent **dans** Korin sur PC : GCC 14.2 (C, C++), binutils 2.43, GNU make
4.4.1, FreeBASIC 1.10.1 et fasm 1.73 (x86, x86-64). Mêmes scripts que le Pi, avec `ARCH=x86_64` ; rien ne change
pour le Pi (sans `ARCH`, mêmes variables, mêmes options, mêmes fichiers).

### Construire (WSL, depuis la racine du dépôt)

```
ARCH=x86_64 sh tools/gcc/build.sh     # GCC et binutils canadiens : ~/.cache/korin-gcc-x86_64, 13 min sur 16 cœurs
ARCH=x86_64 sh tools/gcc/kits.sh      # interfaces des kits : include/kits, lib/lib<kit>.a, include/freebasic/kits
ARCH=x86_64 sh tools/gcc/make.sh      # GNU make ; share/pibuild.mk avec fasm à la place de fasmarm
ARCH=x86_64 sh tools/gcc/fbc.sh       # fbc, rtlib, gfxlib2 (pilote UIKit) : ~/.cache/korin-fbc-x86_64, 3 min
sh tools/pc/fasm.sh                   # build/pc/fasm/fasm (après sh tools/bin/build_c.sh : korinrt)
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_gcc.sh [arbre]
```

Tout va dans `build/pc/gccroot/gcc` (l'équivalent de `build/gcc/sdroot/gcc` du Pi), que `tools/pc/sysroot_gcc.sh`
copie en `gcc/` de l'arbre (`SD0:/gcc`, vu comme `/gcc`), avec `bin/fasm`, le source de fasm
(`gcc/share/fasm/source`), les essais (`gcc/test/pc` de `tools/pc/gcctest`, `gcc/test/fasm`) et, dans
`etc/exec.txt`, `/gcc/x86_64-onyx-elf/bin` à la place du dossier d'AArch64.

| Script | Pour le PC (`ARCH=x86_64`) |
|---|---|
| `tools/gcc/build.sh` | chaîne `x86_64-onyx-elf-14.2`, libposix de `build/pc/posix` (+ `libgcc_mod.a` pour `gcc -shared`), sans `-mcpu=cortex-a72` ni `--with-cpu` ; la retouche `host_detect_local_cpu` d'AArch64 ne sert pas (`-march=native` du x86 lit `cpuid`). La spec installée (`mkspecs.awk`) garde les ajouts du PC : `%{shared:-shared}`, `-export-dynamic`, `*cc1plus` avec les options de `*cpp`, `-L/gcc/lib` |
| `tools/gcc/kits.sh` | `abi2c.py --arch x86_64` (relais `jmp *imports(%rip)`, fonctions « ret » d'UIKit) ; les `.bi` sont ceux du Pi |
| `tools/gcc/make.sh` | `--host=x86_64-onyx-elf` |
| `tools/gcc/fbc.sh` | `TARGET=armpi-x86_64`, `TARGET_ARCH=x86_64` ; deux retouches de `fbc.bas` faites par le script sur le PC seulement (`armpi.patch` inchangé) : l'`as` de l'arbre est `x86_64-onyx-elf/bin/as`, et `-target armpi` vise x86_64 |

### fasm (`src/bin/fasm`, `tools/pc/fasm.sh`)

Le cœur de fasm 1.73.32 (`ref/fasm/SOURCE`, celui de `FASM.EXE` du noyau du PC) et `LINUX/X64/MODES.INC` (les
macros qui font tourner le cœur, écrit en adresses de 32 bits, en mode 64 bits) tels quels. L'interface avec
Korin est neuve :

* `korin.asm` : `FASM.ASM` de Linux x64 en **objet ELF64** (`main`, argc et argv du runtime) ; `system.inc` : les
  fonctions de `SYSTEM.INC` (open, create, read, write, lseek, close, display_*, erreurs, horodatage,
  variables d'environnement), dont les appels de Linux deviennent des appels aux fonctions C de
  `c/fasm_sys.c` (macros `csave` / `crestore` : registres que le C peut détruire gardés, pile alignée à 16) ;
* `c/fasm_sys.c` : sur korinrt (fichiers par AppKit, chemins à volume, dossier courant), lié à `0x400000`
  comme les commandes du PC (`bin.ld`) ; la mémoire de travail est un tableau de 256 Mo en `.bss` paresseux,
  donc sous 4 Go comme le veut le cœur (l'arène de `VM_MAP` est à `0x5800000000`, et `SBRK` prend ses pages
  tout de suite) ; `-m` en donne moins. Un ELF exécutable (`format ELF64 executable`) reçoit le bit x (`V_CHMOD`).
* Dans un sous-dossier `c/` et sans `fasm.asm` : ni `tools/bin/build_c.sh` ni `build.ps1` (FASMARM du Pi) ne le
  prennent. `fasm` du PC : 124 Ko.

Un programme pour Korin PC en assembleur : `format ELF64 executable 3 at 0x400000`, ligne d'arguments dans
`rdi`, `syscall` (numéros de `src/inc/syscalls.inc`), AppKit par `SYS_LIB_OPEN` (`src/bin/fasm/test/hello.asm`,
dans l'arbre `/gcc/test/fasm/hello.asm`). `pibuild` sur un dossier `<nom>/<nom>.asm` l'assemble par `/bin/fasm`.

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, disque NVMe de `mkkorindisk.py --layout zbook --korin-mb 400`, 13/11)

Console texte (`shell=console`), `sh /gcc/test/pc/run.sh` puis `run2.sh` tapés au moniteur ; temps de
`timeit` (`tools/pc/gcctest/timeit.c`, compilé dans Korin au début de l'essai), fichiers dans `/tmp` (RAM0:) :

| Dans Korin PC | Résultat | Temps |
|---|---|---|
| `gcc hello.c -o hello && hello` | « bonjour de gcc sous ARMPI » | 8,8 s (cc1 lu du disque la première fois) |
| `gcc -O2 hello.c` | idem | 16,7 s |
| `g++ hello_io.cpp` (iostream, string, vector, sort) | « bonjour de iostream : Korin g++ x86-64 » | 48,9 s |
| `g++ -O2 hello.cpp` (STL, héritage, lambda, exception) | « exception attrapee, reponse = 42 » | 39,2 s |
| `gcc -I/gcc/include/kits uiapp.c -luikit` (le modèle d'app de `korin-lang-c`) | 185 Ko | 15,4 s |
| `make essai` (`test/projet` : 2 C, 1 C++, lien) | « PROJET OK » | 40,1 s |
| `fbc hello.bas` | « Bonjour depuis FreeBASIC, sur Korin PC », « somme … 55 » | 9,6 s |
| `fasm hello.asm hello_asm && hello_asm` | « Bonjour depuis fasm, sur Korin PC ! » (272 octets) | 0,16 s |
| `fasm korin.asm fasm.o` (fasm se réassemble) | 5 passes, 220 576 octets (même objet que `FASM.EXE`, vérifié à l'octet sous Linux) | 2,3 s |
| `pibuild bin <dossier C> hc`, puis `hc` | installé dans `/bin`, lancé | 9,5 s |
| `pibuild bin <dossier asm> hasm`, puis `hasm` | assemblé par `/bin/fasm`, installé, lancé | 0,56 s |

Bureau (`shell=desktop`, `SD0:/etc/startup.txt` : `SD0:/bin/sh /gcc/test/pc/ui.sh`) : `uiapp.c` compilé dans
Korin puis lancé, **sa fenêtre s'ouvre** (cadre sans barre, texte, bouton aux couleurs de Givre, « Bonjour » dans
menubar) : `build/pc/gcc_uiapp.png` (mémoire vidéo par `pmemsave`).

Ces temps sont ceux de QEMU sans accélération (TCG) : le vrai ZBook n'est pas mesuré.

**Taille** : 139 Mo de plus dans l'arbre (`gcc/` 136 Mo : `cc1plus` 37 Mo, `cc1` 35 Mo, `libstdc++.a` 6,2 Mo,
`bin/` 20 Mo, en-têtes 18 Mo, FreeBASIC 5,4 Mo) : la partition KORIN OS de `mkkorindisk.py` doit avoir 400 Mo
(`--korin-mb 400`). `tools/pc/korin.ps1` ne copie pas encore `gcc/` (`$sysTop` : bin, sys, lib, etc, res, apps,
fonts) : à ajouter pour le ZBook (partition KORIN de 10 Go : la place y est).

### Pas fait, écarts

* make : « jobserver mkfifo : Function not implemented » (`-j4` de `pibuild` sans jobserver : les règles tournent
  quand même) et « Clock skew detected » : l'horloge du PC part de 0 au démarrage (pas de CMOS lu, pas de NTP), les
  fichiers de l'arbre sont datés du PC hôte ; pas d'effet sur ces essais.
* Pas de `preload` de `cc1` / `cc1plus` dans l'arbre du PC (`etc/preload.txt` n'est pas copié) : chaque compilation
  relit `cc1plus` (37 Mo) du disque.
* fbc : `wchar` reste « unsigned int » (celui d'AArch64 ; newlib x86-64 : `int`) ; gfxlib2 (fenêtre) et les `.bi`
  des kits ne sont essayés que par la compilation de `hello.bas` (pas `gfx.bas` ni `kitui.bas`).
* fasm : une seule tâche à la fois (la sauvegarde des registres de `csave` est statique) ; les fichiers d'en-tête
  de fasm (`ref/fasm/INCLUDE`, Win32) ne sont pas dans l'arbre ; il n'y a pas d'inclusions de Korin pour
  l'assembleur x86-64 (constantes, macro `sys` : `src/inc/x86/sys.inc` n'est pas copié).
* Sur le Pi, `/gcc/aarch64-onyx-elf/armpi` n'a pas `libgcc_mod.a` (`gcc -shared` dans l'OS) : non corrigé pour
  garder le Pi identique.
* Rien n'est encore en paquet (`tools/pkg/versions.ini`) pour le PC.

## GPU Intel, étape 1 (13/11)

Étude complète : [GPU-INTEL.md](GPU-INTEL.md) (architecture, ioctls d'iris et leur traduction, GGTT/PPGTT,
execlists sans GuC, forcewake, micrologiciels, cohérence LLC, MSI, pourquoi ne pas toucher à l'affichage, étapes).
Le GPU intégré du ZBook : `0000:00:02.0 8086:9A60` (Tiger Lake-H GT1, Gen12 LP, 32 EU). QEMU n'émule aucun GPU Intel :
**rien n'est essayé sur le vrai GPU**.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/intelgpu/` (`igpu.h`, `intelgpu.c`, `submit.c`, `simu.c`, `svc.conf` : PC seulement) | la **sonde**, lancée à la main seulement (ni hwmanager, ni `drivers.txt`) : `intelgpu probe [submit] [-v] [simu \| simu-hang]` (GPU-INTEL.md § 11). Va dans `SD0:/sys/svc/intelgpu` par `tools/pc/sysroot.sh` (et dans l'initrd, comme tout `build/svc/x86_64`, sans être lancée) |
| `src/svc/pcid/pcid.c` | à la fin d'un pilote, le maître du bus n'est plus coupé pour l'**affichage** (classe 03) : le framebuffer du GOP en dépend peut-être |
| `tools/mesa/build-clc.sh`, `configure-iris.sh`, `build-iris.sh`, `korin-pc.patch`, `libdrm-pc/` | Mesa 26.2 avec **iris** pour Korin PC, faux libdrm « i915 » (mode à blanc), `gltest` relié (GPU-INTEL.md § 12) |
| `tools/mesa/armgl/armgl.c` | `#ifdef ARMGL_IRIS` (le Pi : `armgl.o` identique) |
| `docs/etude/GPU-INTEL.md` | l'étude |

Noyau du PC : **rien à ajouter** pour cette étape (`DEV_MAP` projette la BAR0 de 16 Mo à `0x60_5200_0000`,
`DMA_ALLOC` donne le contigu, `IRQ_MSI` existe). Machine de construction (WSL) : paquets LLVM 21, clang, libclc,
SPIRV-LLVM-Translator, SPIRV-Tools installés par `apt` pour `mesa_clc` (liste dans `build-clc.sh`).

### Vérifié (QEMU q35 + OVMF, 4 cœurs, disque `mkkorindisk.py --layout zbook --korin-mb 120`, bureau Elegant, 13/11)

* `intelgpu probe -v` : « affichage 1234:1111 à 0000:00:01.0 : pas Intel, laissé », « pas de GPU Intel (aucune
  fonction d'affichage 8086) : rien à faire », code 0 ; le bureau vient (« panel : bureau prêt ») ; rien d'autre ne
  change au démarrage (la sonde n'est lancée par personne).
* `intelgpu probe submit simu` (faux GPU en mémoire, vraie zone `DMA_ALLOC`) : tout le chemin — identité, fusibles
  (32 EU), horodatage mesuré, balayage de 1 Mi entrées de la GGTT (« entrées 0..7e8 valides » = un framebuffer de
  1920 × 1080, page de garde reconnue), fenêtre libre à 256 Mo, 64 PTE écrites et relues, image du contexte (82 mots),
  soumission (descripteur `00000020:10008119`), « RÉUSSI : le lot a écrit 4b4f5249, l'anneau 31555047 », arrêt,
  registres remis (« rendu à la fin » = « au départ »), « GGTT : 64 entrées remises comme avant », forcewake rendu,
  code 0.
* `intelgpu probe submit simu-hang -v` : « ÉCHEC : rien en 500 ms », « le moteur ne s'arrête pas », remise à zéro
  du rendu (`RESET_CTL`, `GDRST` bit 1), registres et GGTT remis, code 1 ; chaque écriture journalisée.
* `gltest --dry` (iris, à blanc) : « Mesa Intel(R) UHD Graphics (TGL GT1) / OpenGL ES 3.2 », shaders compilés,
  4 lots, **PASS** ; `gltest` sans `--dry` : repli « pas de GPU ».
* `tools/check_constants.py` passe ; `wsl sh tools/svc/build_c.sh intelgpu pcid` sans avertissement (pcid aussi
  pour AArch64).

### Marche à suivre sur le ZBook (par l'utilisateur)

1. Construire et installer comme d'habitude (PowerShell administrateur, `C:\Projects\ARMPI`) :
   `wsl sh tools/pc/build.sh`, `wsl sh tools/pc/sysroot.sh`, puis `… korin.ps1 install -DryRun`,
   `… korin.ps1 install` (le noyau, l'initrd avec le nouveau pcid, `\sys\svc\intelgpu`).
2. Pour lire la sortie à l'écran : la console texte. Dans `D:\etc\system.ini`, `shell=console` (remettre
   `shell=desktop` après). Sinon, au bureau, tout est quand même dans le journal (étape 5).
3. Démarrer Korin. Dans la console, **d'abord la lecture seule** :
   `SD0:/sys/svc/intelgpu probe`
   Rien n'est écrit, sauf le forcewake (pris puis rendu) et le registre de commande PCI (remis).
4. Si la dernière ligne est « sonde finie (rien d'anormal) » et que l'écran est normal :
   `SD0:/sys/svc/intelgpu probe submit -v`
5. `journal` (écrit `SD0:/journal.txt`), redémarrer sous Windows, rapporter `D:\journal.txt` (lignes « intelgpu »).

**Ce qu'il faut regarder** : « 0000:00:02.0 8086:9a60 » ; `GMCH_CTRL` (taille de la GGTT, mémoire volée), BDSM ;
« forcewake pris » ; « fusibles … 32 EU » (une autre valeur : à corriger dans le faux libdrm) ; « horodatage du rendu :
… kHz mesurés » (attendu ≈ 19 200 ; 0 : le GT dort, forcewake mal pris) ; `PAT du GPU` ; `PLANE_SURF` (où est le
framebuffer dans la GGTT) ; « rendu au départ » (`CTL` doit finir par 0 : anneau éteint) ; les plages de la GGTT ;
avec `submit` : « fenêtre de la GGTT », puis **« RÉUSSI : le lot a écrit 4b4f5249, l'anneau 31555047 »** (le GPU a
exécuté notre lot) ou « ÉCHEC » avec les registres et le CSB (à rapporter tels quels : ils disent pourquoi) ;
« rendu à la fin », « GGTT : 64 entrées remises comme avant », « sonde finie ».

**Signes de danger** (s'arrêter, ne pas relancer, rapporter) : l'écran devient noir, se fige ou montre des
déchets (tenir le bouton d'alimentation : rien n'est écrit sur le disque par la sonde) ; une ligne
« **DANGER** » (le moteur n'a pas pu être arrêté : la sonde reste en vie exprès, ne pas la tuer, éteindre au bouton) ;
« ANOMALIE : GDRST » ; « GGTT … relues autrement » ; une faute du noyau. « ÉCHEC » seul n'est pas un danger : le
moteur est remis à zéro et l'état d'avant est rendu.

### Pas vérifié, ensuite

* Tout sur le vrai GPU : la séquence d'execlists, l'image de contexte réduite à sa partie « anneau » (le risque
  principal : un refus du matériel donnerait « ÉCHEC », qu'on corrigera d'après le CSB), les adresses des registres
  de Gen12 (PRM), la remise à zéro du rendu, l'effet de pcid sur le maître du bus de l'affichage.
* Étape 2 : le service `intelgpu` (GPU-INTEL.md § 13) ; étape 3 : le faux libdrm branché dessus et `gltest` sur le
  ZBook ; étape 4 : GlKit et GpuKit du PC.

## GPU Intel, étape 2 (13/11)

Le vrai pilote : le service `intelgpu` sert le GPU du ZBook à plusieurs clients, le faux libdrm d'iris est branché
dessus, `gltest` dessine par lui. Toujours **lancé à la main** (ni hwmanager ni `drivers.txt`) tant que `gltest`
n'est pas validé sur le ZBook. Étude : [GPU-INTEL.md](GPU-INTEL.md) (§ 3 à 9). **Rien n'est encore essayé sur le
vrai GPU** : tout est vérifié sous QEMU avec le faux GPU (`simu`).

### Le journal de l'étape 1 sur le ZBook

« RÉUSSI : le lot a écrit 4b4f5249, l'anneau 31555047, en 1 ms », tout remis, écran intact. Le CSB
(`8001:3ff8000 3ff8000:8000`) se lit au format de Gen12 : le numéro du contexte (11 bits) est dans les bits 25:15
des deux mots, `0x7FF` = repos. Entrée 0 : « du repos vers le contexte 1 », entrée 1 : « du contexte 1 vers le
repos ». C'est ce que le service attend avant de soumettre le contexte suivant (le matériel a alors sauvé l'image).

**Les accusés du forcewake à 0** : pas une anomalie. Les adresses sont les bonnes pour Gen12 (`FORCEWAKE_GT`
`0xA188` / accusé `0x130044`, `FORCEWAKE_RENDER` `0xA278` / accusé `0x0D84`, PRM de Tiger Lake, les mêmes qu'i915).
La sonde ne montrait l'accusé qu'**avant** la prise (personne ne tient le GT : 0) et **après l'avoir rendu**
(0 aussi) ; pendant, il valait bien 1, sinon `wait_bits` aurait écrit « ANOMALIE : forcewake … sans accusé ». Le
journal montre maintenant les trois (« accusés GT 00000001, rendu 00000001 (bit 0 attendu) ») ; comme i915, on
attend que l'accusé soit retombé avant de demander, et après l'avoir rendu.

**La sortie** : chaque ligne va au journal du noyau (`SYS_PRINT`) et, au bureau, aussi à la sortie standard
(le Terminal). En console texte, une seule fois (la console du noyau est l'écran).

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/intelgpu/igproto.h` | le protocole (`IG_OP_*`, `struct ig_bind`, `struct ig_info`, page d'état `IGS_*`), partagé avec le faux libdrm |
| `intelgpu.c` | commandes `probe` (étape 1), `test`, `start`, `serve`, `status`, `stop` ; forcewake ; journal |
| `gt.c` | PAT du GPU (0 WB, 1 WC, 2 WT, 3 UC, 4-7 WB : l'index 0 ne change pas, le ZBook a 3 partout), **MOCS** de Tiger Lake (64 entrées globales `0x4000`, L3 `0xB020`) : les index que Mesa emploie (2 interne, 3 non caché, 61 affichable, 48), **contournements** : GT 2 (Wa_14011059788, Wa_14015795083), moteur 7 (Wa_1606700617, Wa_1409804808, Wa_1606931601, Wa_14010229206, Wa_1406941453, Wa_18011464164/Wa_22010931296, Wa_1607297627…), contexte 6 (COMMON_SLICE_CHICKEN3 `CPS aware`, préemption au niveau du groupe, FF_MODE2 GS 224 / TDS 128, Wa_1806527549, Wa_1606376872, Wa_1508744258) ; interruptions MSI (maître `0x190010`, RCS0 seulement, jamais l'affichage) |
| `ppgtt.c` | PPGTT à 4 niveaux par client (tables en `DMA_ALLOC` par blocs de 2 Mo, recyclées) ; chaîne de pages de garde pour tout trou (une lecture hors BO ne fait pas de faute) |
| `engine.c` | fenêtre de 4096 entrées de la GGTT (anciennes valeurs remises), page d'état du moteur (CSB), page d'état partagée ; **contexte doré** : premier contexte écrit à la main (comme la sonde) avec les contournements du contexte, l'image complète sauvée par le matériel devient le modèle de tout contexte ; un anneau de 16 Ko par contexte ; un lot = invalidation (PIPE_CONTROL : TLB, caches ; table auxiliaire `0x4208`), `MI_BATCH_BUFFER_START` en PPGTT (non privilégié), vidage, numéro écrit dans la page d'état (PIPE_CONTROL GGTT), `MI_USER_INTERRUPT` ; numéros uniques et croissants, un contexte à la fois (les lots consécutifs d'un même contexte partent ensemble), le suivant quand le CSB dit « repos » ; 4 s sans progrès : remise à zéro du seul rendu (`RESET_CTL`, `GDRST` bit 1), lots en vol perdus, contexte banni (iris en refait un), le reste continue ; 4 blocages de suite : « GPU perdu » ; l'essai `test` |
| `serve.c` | le service : deux fils (requêtes ; moteur : interruption ou sondage, CSB, réponses différées des attentes), un client par process, tout rendu à sa fin (`SVC_OP_END`) dès que ses contextes sont au repos ; tempête d'interruptions sans objet : retour au sondage |
| `simu.c` | le faux GPU de l'étape 2 : image (LRI), anneau, lots en PPGTT par les vraies tables, longueurs des commandes, lots chaînés et de second niveau, `MI_STORE_DATA_IMM`, `PIPE_CONTROL`, `MI_STORE_REGISTER_MEM`, `MI_COPY_MEM_MEM` exécutés, commandes 3D comptées, CSB au format du ZBook ; aucune adresse hors des tables (« FAUTE ») |
| `tools/mesa/libdrm-pc/korin_i915.c` | le faux libdrm branché : BO dans des morceaux (`BUF_CREATE` 32 Mo, `BUF_GRANT` au service, `IG_OP_CHUNK`) ; `IG_OP_BIND` des BO pas encore à leur adresse (softpin d'iris) ; un contexte du service par rang de moteur du contexte d'iris (rendu et calcul : deux contextes) ; lots du copieur tenus pour finis sans partir (iris n'y met que son initialisation) ; syncobj = numéro du dernier lot ; `GEM_WAIT`, `GEM_BUSY`, `SYNCOBJ_WAIT` par la page d'état, `IG_OP_WAIT` seulement s'il faut attendre ; `REG_READ` horodatage, `GET_RESET_STATS` ; le mode à blanc inchangé |
| `tools/mesa/armgl/gltest.c` | `KORIN_PC` seulement : `--win` (l'image du triangle recopiée par le processeur dans une fenêtre UIKit), et en simu : pixels non vérifiés. Le `gltest.o` du Pi est **identique** (désassemblage et données comparés) ; `armgl.c` n'a pas changé |
| `tools/mesa/build-iris.sh` | `igproto.h`, `gltest` lié à `libuikit.a` du PC, copié dans `build/pc/mesa/sdroot/bin/gltest` |
| `tools/pc/sysroot.sh` | `gltest` dans `SD0:/bin` du PC s'il est construit |

### Vérifié sous QEMU (q35 + OVMF, 4 cœurs, NVMe, `mkkorindisk.py --layout zbook --korin-mb 120`, console texte, 13/11)

* `intelgpu test simu` : identité, PAT `3 1 2 0 3 3 3 3`, 28 MOCS chargés, contournements, fenêtre de la GGTT à
  `0x10000000` (comme la sonde), moteur en execlists, **contexte doré** sauvé, « RÉUSSI : MI_STORE_DATA_IMM 4b4f5249,
  PIPE_CONTROL 32555047 » (lot en PPGTT à l'adresse `0x100000000000` : quatre niveaux), « RÉUSSI : 64 lots sur deux
  contextes … 0 faux », registres et 4096 entrées de la GGTT remis, forcewake rendu, code 0.
* `intelgpu test simu-hang` : au troisième essai, « BLOCAGE : pas de progrès », remise à zéro, « contexte 1 banni
  (E_IO) », « RÉUSSI : l'autre contexte repart après la remise à zéro », tout remis, code 0.
* `intelgpu start simu`, puis `gltest` (deux fois de suite), `intelgpu status`, `intelgpu stop` : iris ouvre le
  service (« service intelgpu en SIMU »), « Mesa Intel(R) UHD Graphics (TGL GT1) / OpenGL ES 3.2 », shaders
  compilés, lots liés (BIND), soumis, exécutés par le faux GPU sans faute de page, attendus par la page d'état ;
  « simu : 5 lots soumis et finis par le faux GPU : PASS » ; le second `gltest` repart d'un client neuf (le
  premier rendu par `SVC_OP_END`) ; `stop` : « service fini (rien d'anormal) », GGTT et registres remis.
* `wsl sh tools/svc/build_c.sh intelgpu` sans avertissement (`-Werror`) ; `tools/check_constants.py` passe.

Pas vérifiable ici : tout ce qui touche au vrai GPU (le contexte doré sauvé par le matériel, les contournements,
les MOCS, la PPGTT, les MSI, la remise à zéro sur un vrai blocage, les pixels d'iris), et `gltest --win` au
bureau (UIKit) sous QEMU avec le service en simu reste à faire à la prochaine session graphique.

### Marche à suivre sur le ZBook (par l'utilisateur)

1. Construire et installer (PowerShell **administrateur**, `C:\Projects\ARMPI` ; « `… korin.ps1` » abrège
   `powershell -ExecutionPolicy Bypass -File tools\pc\korin.ps1`) :
   `wsl sh tools/pc/build.sh`, `wsl sh tools/mesa/build-iris.sh`, `wsl sh tools/pc/sysroot.sh`, puis
   `… korin.ps1 install -DryRun` (la ligne « Partition KORIN OS : D: … »), puis `… korin.ps1 install`.
2. Démarrer Korin (bureau), ouvrir le **Terminal**. Les lignes « intelgpu : … » s'y affichent maintenant.
3. **D'abord l'essai seul** (pas de client, tout est remis à la fin) :
   `SD0:/sys/svc/intelgpu test`
   Attendu : « forcewake pris … accusés GT 00000001, rendu 00000001 », « contexte doré : N mots non nuls sauvés par
   le matériel » (N de l'ordre de plusieurs milliers), « RÉUSSI : MI_STORE_DATA_IMM 4b4f5249, PIPE_CONTROL
   32555047 », « RÉUSSI : 64 lots sur deux contextes … 0 faux », « interruption MSI sur la ligne … », « GGTT : 4096
   entrées remises comme avant », « essai fini (rien d'anormal) ».
4. Si c'est bon : le service, puis le rendu :
   `SD0:/sys/svc/intelgpu start`
   `gltest --win`
   Attendu : « armgl_init 1 (GPU) », « Mesa Intel(R) UHD Graphics (TGL GT1) », cinq lignes « PASS » (fond, trois
   sommets, centre) puis trois pour la texture, « 0 ecart(s) : PASS » ; une fenêtre montre un **triangle rouge,
   vert, bleu sur fond bleu nuit**. Fermer la fenêtre. Puis `SD0:/sys/svc/intelgpu status` (lots donnés = finis,
   interruptions MSI comptées, blocages 0) et `SD0:/sys/svc/intelgpu stop`.
5. `journal` (écrit `SD0:/journal.txt`), redémarrer sous Windows, rapporter `D:\journal.txt` (lignes
   « intelgpu », « gltest », « korin_i915 ») et `D:\gltest.ppm` (l'image du triangle).

Si `start` dit « interruption … » puis que les lots traînent, essayer `SD0:/sys/svc/intelgpu stop` puis
`SD0:/sys/svc/intelgpu start -p` (sondage, sans MSI). Pour un journal détaillé : `start -v` (chaque soumission, chaque
entrée du CSB).

**Signes de danger** (s'arrêter, ne pas relancer, rapporter) : l'écran devient noir, se fige ou montre des
déchets (bouton d'alimentation tenu : rien n'est écrit sur le disque par le service) ; une ligne **« DANGER »** (le
moteur n'a pas pu être arrêté : le process reste en vie exprès, ne pas le tuer, éteindre au bouton) ;
« ANOMALIE : GDRST » ; « GGTT … relues autrement » ; « tempête d'interruptions » répétée ; une faute du noyau.
**Pas des dangers** : « ÉCHEC » à l'essai (le moteur est remis à zéro et tout est rendu), « BLOCAGE » suivi de
« remise à zéro finie » (le GPU a rejeté un lot ; le journal dit lequel), des « FAIL » de pixels dans `gltest`.
Ne pas tuer le service (`kill`) : l'arrêter par `intelgpu stop`, qui remet la GGTT et les registres.

### Ensuite

* Selon le journal du ZBook : corriger ce que le matériel refuse (image du contexte, contournements, MSI).
* `gltest` validé : le service dans `drivers.txt` (`pci:8086:9a60 = SD0:/sys/svc/intelgpu serve`), RC6 et la
  fréquence (RPS) au lieu du forcewake tenu.
* **GlKit du PC** (étape 4) : `armpi_drm_window` du faux libdrm (une surface d'Elegant de deux images WB, tranches
  d'un tampon donné au service, présentées par `WS_OP_SURFACE_ATTACH` comme sur le Pi, sans vidage grâce au LLC),
  `glkit.S` et `glkit.ld` en x86-64, `build-glkit.sh` pour le PC (même `glkit.abi`) ; puis `armgl_none.c` retiré de
  `tools/pc/apps.sh`.
* **GpuKit du PC** (même `gpukit.abi`) : les mêmes fonctions sur iris (les `gk_*` par des shaders GL ES de GlKit),
  les textures en BO du faux libdrm ; ce qui ne sert qu'au Pi (`gk_draw` en asm du V3D) reste au Pi.
* Présentation par le copieur (BCS0) vers le framebuffer du GOP (GPU-INTEL.md § 10), Elegant composé par le GPU.

## Apps asm en C (13/11)

Les apps de `src/apps` écrites seulement en assembleur FASMARM et qui ne viennent **pas** d'Onyx sont portées en C,
**un seul source pour le Pi et le PC** : terminal, files, welcome, editor, viewer, calculator, uidemo, builder,
mediaplayer, mixer, music, piano, recorder, targets, bubbles, arkanoid. Comportement repris de l'asm (mêmes
fenêtres, menus, raccourcis, textes, `lang/` et aides inchangés, bizarreries gardées) ; appels directs aux kits
(`ui_*`, `buskit_abi.h`, `audiokit_abi.h`, `mediakit.h`), la libc et POSIX à la place d'AppKit. L'asm reste dans
chaque dossier (assemblé par `build.ps1`, retiré après validation sur le vrai Pi).

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/apps/terminal/main.c`, `vt.c`, `vt_draw.c`, `vt_keys.c`, `term.h` | `terminal.asm`, `vt.inc`, `vt_draw.inc`, `vt_keys.inc` : même analyseur VT/xterm, même dessin par l'ombre, mêmes touches ; vterm par ses messages (`SYS_SVC_CALL`), le shell par `f_spawn_stdio` d'AppKit (`armpi_kit_open`) : les poignées de vterm passent telles quelles en entrée / sortie, comme l'asm |
| `src/apps/<app>/main.c` | les quinze autres (un fichier chacune) |
| `src/inc/gamefb.h`, `gamedraw.h`, `gamepause.h`, `gamesound.h`, `gamepad.h` | `gamefb.inc`, `gameart.inc`, `gamepause.inc`, `gamesnd.inc`, `pad.inc` en C (mêmes noms, mêmes calculs entiers, donc mêmes pixels) ; tout `static`, inclus par le `main.c` d'un jeu ; pour les autres jeux asm quand ils passeront en C |
| `tools/office/build.sh` | les seize apps dans sa liste : leur `main` remplace celui de l'asm sur la carte du Pi (`build\office\sdroot`, recopié par `build.ps1` après l'asm : pas d'autre interrupteur) |
| `tools/pc/apps.sh` | les seize apps et **pkgman** (OpenSSL, `src/lib/pkg`) ; l'aide (`help_*.md`, `help/`) va avec l'app |
| `tools/pc/sysroot_apps.sh` | l'icône de chaque app (`tools/icons/rendered/<nom>.rgba` → `icon.rgba`), comme `build.ps1` |
| `src/lib/pkg/pkglib.h` | l'appel système par `<armpi/syscall.h>` (plus de `svc 0` en asm) : pkgman, pkg, pkgd, setup se construisent pour le PC |
| `tools/apps/compare.sh` | comparaison asm / C sur le Pi : cartes `build/card_app<TAG>_a` / `_c` tirées de `build/sdroot` (kits de `build/kits/aarch64`), `run NOM SCRIPT [a\|c] -- LIGNE` (la ligne ajoutée à `startup.txt`, captures `build/appcmp/NOM_<a\|c>_*.png`) ; `keyb=US` (le clavier de `qemu_drive.py`) ; emplacement 2, verrou `build/qemu2.lock` |
| `tools/qemu_drive.py` | `type` connaît aussi la barre oblique inverse, les crochets, les accolades, `?`, `^` et l'accent grave |
| `tools/pc/qemu_pc.py` | `KORIN_PC_DEBUGLOG` : le journal du port 0xE9 ailleurs que `build/pc/debug.log` (deux QEMU du PC en même temps) |
| `tools/pkg/versions.ini` | armpi, korin-srcs, arkanoid, builder, calculator, mediaplayer, music, piano, recorder, targets, uidemo, bubbles : +1 (rien de publié) |

Tailles (asm → C Pi / C PC, octets ; le C lie libposix et newlib en statique) : terminal 18 880 → 148 208 / 176 920 ;
files 37 294 → 168 784 / 197 496 ; welcome 7 544 → 135 920 / 160 536 ; editor 10 759 → 152 336 / 181 048 ; viewer
7 920 → 135 936 / 160 568 ; calculator 9 720 → 140 016 / 164 632 ; uidemo 22 440 → 148 224 / 176 952 ; builder
8 888 → 152 304 / 181 016 ; mediaplayer 19 240 → 160 536 / 193 336 ; mixer 14 088 → 140 016 / 164 632 ; music
15 656 → 148 224 / 172 856 ; piano 10 640 → 135 968 / 164 696 ; recorder 11 912 → 160 504 / 189 208 ; targets
9 000 → 140 016 / 168 728 ; bubbles 37 000 → 180 976 / 205 592 ; arkanoid 38 136 → 176 880 / 205 592.

### Origine (pas d'Onyx)

En-tête et `git log --follow` de chaque `.asm` : aucune ne cite Onyx ; l'historique commence au dépôt du 03/11
(bubbles : nouveau jeu du 05/11 ; arkanoid : « un casse-briques neuf dans l'esprit de LBreakout2 », pas un port).
**Pas portées ici** parce qu'elles viennent d'Onyx (à refaire depuis son source, absent de cette machine) : 2048,
clock, control, fmtracker, freecell, gamelib, invaders, mandelbrot, pipes, same, snake, solitaire, taskman,
tetris, et les émulateurs (nesemu, gbemu, gbaemu, snesemu, n64emu).

### Écarts avec l'asm

* **Arguments** : la ligne brute de l'asm (`ui_arg`) devient `argv[1..]` recollés par des espaces (editor, viewer,
  builder, music, recorder) ; seuls des espaces doubles ou entre guillemets dans un chemin différeraient.
  mediaplayer reconnaît `loop` et `autoexit` comme mots, le dernier autre mot est le fichier.
* **Fichiers** : `opendir` / `readdir` (+ un `stat` par fichier dans files : la libc ne donne pas la taille),
  `rmdir` ou `unlink` au lieu de `SYS_UNLINK`, `posix_spawn` au lieu de `SYS_SPAWN` / `f_spawn_pipe` (builder : la
  sortie d'erreur aussi dans le tube, comme l'asm) ; les codes « Erreur -N » viennent d'`errno` (newlib = Linux
  jusqu'à 34 ; files ramène `ENOTEMPTY` et `ENOENT` aux numéros de Korin).
* files : le message de démontage à vfs met `len = 16` (l'asm laissait l'ancienne valeur). viewer : un id de menu
  inconnu ne fait rien (l'asm repassait en Ajuster). editor : la lecture boucle jusqu'à la taille du fichier (un
  seul `read` dans l'asm).
* `sounddraw.c` et `gamesnd.c` de `src/sys/common` sont écrits pour korinrt : mixer garde sa copie de `sd_slider`,
  `sd_check`, `sd_meter`, les jeux `gamesound.h` ; à fondre plus tard dans une version partagée par les apps.
* Couleurs : les jetons du thème partout où l'asm les prend ; restent fixes ce qui est du contenu et l'était dans
  l'asm (palette ANSI du Terminal, dossiers jaunes de Fichiers, touches du piano, vumètre, fond vidéo, terrain de
  Cibles, canevas d'uidemo, décors des jeux).

### Vérifié

**Pi (QEMU raspi4b, emplacement 2, `tools/apps/compare.sh`, 13/11)** : chaque app asm puis C sur deux cartes tirées
de `build/sdroot`, captures comparées au pixel (hors fond voronoy tiré au hasard, heure, notification de pkgd) :
* **terminal** : `ls -la SD0:/apps` (bash), couleurs SGR (gras, 256 couleurs, 24 bits, souligné, inverse), 30
  lignes puis Maj+Page haut / bas dans l'historique, Ctrl+L ; fenêtre agrandie : `cols=112 lines=37`
  (VT_SWINSZ, SIGWINCH), `sleep 50` puis Ctrl+C, écran alternatif (`?1049h` / `l`), `exit` ferme le Terminal :
  **identique à l'asm** ;
* **files** : vue icônes et liste, tri, navigation clavier et souris, menus contextuels, Ctrl+N, confirmation de
  Suppr, accès rapides, ligne d'état ; **welcome** (frappe) ; **editor** (fichier ouvert, couleurs du code, frappe,
  Rechercher) ; **viewer** (PNG, Ctrl+O) ; **calculator** (clavier et souris, division par zéro) ; **uidemo** (les
  11 pages) ; **builder** (construction de `targets.asm` par pibuild, même journal) ; **targets** (partie, fin,
  Rejouer) : identiques ;
* **music** (liste, lecture MP3, pause, suivant, glisser, arrêt), **mediaplayer** (WebM VP9 + Opus, pause, avance,
  Ctrl+O) ; **piano** (même fréquence et même décroissance dans `out_4469.wav`), **mixer** (fenêtre), **recorder**
  (enregistrer, sauver ; `--test` : mêmes lignes sur l'UART) ; **bubbles** et **arkanoid** (titre, partie, tirs,
  raquette, vie perdue, menu de pause) : identiques ;
* toutes construites aussi avec `-Wall -Wextra`, sans avertissement, pour les deux cibles.

**PC (QEMU q35 + OVMF, 4 cœurs, disque de `mkkorindisk.py --layout zbook --korin-mb 120`, arbre de `sysroot.sh` +
`sysroot_apps.sh` (82 Mo), 13/11)** : captures par `pmemsave` de la mémoire vidéo :
* **welcome** lancé par init2 sans `startup.txt` (« `SD0:/apps/welcome.app/main -> 15` », plus de -2), « bonjour »
  tapé ; le panel montre maintenant ses apps épinglées (Fichiers, Terminal, Éditeur, Calculatrice) ;
* **terminal** : `sh` du PC (pas de bash), `ls` puis `ls -l SD0:/apps` tapés au clavier PS/2 ;
* **files** : SD0:, flèches et Entrée → `SD0: › bin`, 124 éléments ; **editor** avec `SD0:/etc/system.ini`,
  **viewer** avec une PNG, **calculator** « 12+7*3= » → 57 ; **uidemo**, **pkgman** (« serveur introuvable » : pas de
  réseau), **bubbles** et **arkanoid** en plein écran (partie lancée), **music** et **mediaplayer** : fenêtres.

### Pas vérifié

* Le **vrai Pi** et le **vrai ZBook**.
* Le **son sur le PC** (pas de pilote de son, pas de service `sound`) : music, mediaplayer, piano, recorder, mixer
  et les sons des jeux s'ouvrent mais restent muets ; **MediaKit** n'existe que pour le Pi (`prebuilt/mediakit.so`)
  : mediaplayer du PC ne lit rien.
* Sous QEMU : les menus de la barre au clic (ni l'asm ni le C ne s'ouvrent au clic de QEMU), l'éjection d'une clé
  et le bus de montage dans files, `korin.media.control`, le redimensionnement de mediaplayer, l'écoute et MP3 /
  Opus du recorder, les glissers du mixer, le record de targets, le mode poche de la calculatrice.
* Le selfhost (`pibuild` des dossiers d'apps, qui contiennent maintenant les deux sources).

### Ce qui manque aux kits

Rien pour ces apps, hors du PC : MediaKit (libvpx, dav1d à construire pour x86-64) et le son. `tools/kits/abi2c.py`
lit le commentaire de `ui_on_resize` comme deux arguments (`ui_on_resize (fn, 0)` dans les apps) : sans effet.

## KotonStudio pour le PC (13/11)

`sh tools/pc/apps.sh koton` (dans la liste par défaut) construit Koton (`src/apps/koton`) pour x86-64, **des mêmes
sources que le Pi**, avec les options de `tools/webkit/build-koton.sh` (`-std=gnu++17 -fno-exceptions -fno-rtti
-fno-threadsafe-statics -DFD_SETSIZE=1024`), la chaîne `x86_64-onyx-elf`, libposix du PC et `libuikit.a` /
`libaudiokit.a` de `build/pc/gcc`. FreeType n'est pas demandé : Koton dessine son texte par UIKit ; le
`-I…/freetype2` de `build-koton.sh` ne sert à rien. Fonction `build_koton` d'`apps.sh` :

* `build/pc/apps/sdroot/apps/koton.app/` : `main` (1 Mo), `app.txt`, `drums.json`, `help_*.md`, `help/` ; l'icône
  par `sysroot_apps.sh` (`tools/icons/rendered/koton.rgba`), les mots par `tools/lang/merge.py` (`lang/fr.txt`, dans
  `res/lang` de `sysroot.sh`) ;
* `plugins/<id>/{main,plugin.json}` : les 11 greffons, un programme chacun (POSIX seul, sans kit) ;
* `bin/kptest` ; `home/koton/songs/demo.kson` et `home/koton/demos/*.kson`, comme `stage.sh` et `build.ps1` sur le
  Pi (Koton les cherche là, pas dans `koton.app`).

**Plus d'asm AArch64 dans Koton** : `w_event` et `drop_get` (`ui/view.cpp`) passent par `uikit_w_event` /
`uikit_drop_get` de `<uikit.h>` (Pi et PC), l'horloge du fil de rendu (`ui/audio.h`) par `clock_gettime`
(libposix : `CNTPCT` sur le Pi, TSC sur le PC). Pour le Pi, seuls `main.o` et `ui_view.o` changent (comparés à
une construction des sources d'avant, `build-koton.sh` dans un `WK_OUT` à part) ; greffons et `kptest`
identiques à l'octet. Paquet `koton` 1.0.3.

### Vérifié (QEMU q35 + OVMF, 4 cœurs, `mkkorindisk.py --layout zbook --korin-mb 200`, bureau Elegant)

`SD0:/etc/startup.txt` : `SD0:/bin/kptest` puis `SD0:/apps/koton.app/main SD0:/home/koton/demos/neons.kson`.

* **`kptest` : 107 réussis, 0 échec** : 11 greffons au catalogue, chaque process prêt, notes des générateurs, deux
  instances d'un effet, un process tué (`fm2`) vu et relancé, un hôte mort (le greffon finit seul).
* **Koton s'ouvre** agrandi avec « Néons » : pistes, blocs, chaîne sonore de la basse (greffon `subsynth`, effet
  `eq3`), générateurs, menus en français (capture `build/pc/koton_pc.png`).
* **Sans son** : le PC n'a pas de service `sound` ; `audiokit.so` est là, `ak_open` échoue : la barre d'état dit
  « pas de sortie son », la banque est la FM d'AudioKit (« FM bank (no SoundFont) », pas de `.sf2` sur le
  disque), aucun fil de rendu n'est lancé. Espace (sendkey) 3 min après le démarrage : la position reste à 1.1.1, rien ne joue,
  Koton reste ouvert sans faute (pas vérifié que la touche lui est bien arrivée).

### Pas fait

* Le son (pilote et service `sound` du PC), une SoundFont sur le disque, l'export WAV, le vrai ZBook.
* `vector` reste le seul C++ hors du PC parmi les apps (Skia) ; la ligne « koton » du tableau « Pas construites »
  plus haut est dépassée.

## Émulateurs d'Onyx, Pi et PC (13/11)

Les émulateurs repartent du source d'Onyx (`$ONYX/user/Emulators`, `user/Apps/*emu`), sur EmuKit, sans pont : un
source pour le Pi et le PC. Chaque recompilateur doit avoir deux générateurs, AArch64 et x86-64. L'asm reste dans
`src/apps/<emu>` (assemblé par `build.ps1`, retiré après validation sur le vrai Pi).

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/apps/{nesemu,gbemu,gbaemu,snesemu}/{nes,gb,gba,snes}/` | les cœurs d'Onyx tels quels (ceux que l'asm reproduit déjà image pour image) |
| `src/apps/{nesemu,gbemu,gbaemu,snesemu}/main.cpp` | le `main` de l'asm en C++ : mêmes boutons par leur place, PAL (en-tête, ou nom « (Europe) »… pour la NES), sauvegardes (`save` rend 0 tant que rien ne change), remise à zéro de la SNES qui garde sa SRAM, message d'erreur traduit (`lang/fr.txt`, nouveau pour gbemu et gbaemu) ; menus, trois vues, pause, son, manettes, `WEV_PAD` : EmuKit, comme pour l'asm |
| `src/apps/n64emu/n64/` | le cœur N64 d'Onyx avec les ajouts de `tools/emu/n64ref` (recompilateur, invalidation par le DMA, `texMemo`) |
| `src/apps/n64emu/n64/n64_jit.cpp` | le recompilateur R4300, **partie commune** : décodage du MIPS, blocs, cycles, sorties, chaînage, invalidation |
| `src/apps/n64emu/n64/n64_jit_a64.h`, `n64_jit_x64.h` | les deux **générateurs** (fonctions `be_*`) : AArch64 (registres MIPS fixés, celui d'avant) et x86-64 (registres MIPS en mémoire, SSE2 seulement) |
| `src/apps/n64emu/main.cpp`, `bas3d.h` | n64emu en C++ (EmuKit, rendu logiciel du BASIC 3D d'Onyx, cadence PAL, `emu_frame_same`), construit pour le PC ; `gl/*.c` gardés par `#ifndef N64_CPP` |
| `src/apps/gcemu/gc/gc_jit_x64.cpp` | le recompilateur Gekko → x86-64 **d'Onyx**, à côté de `gc_jit.cpp` (AArch64) ; vide sur le Pi |
| `tools/pc/armgl_none.c` | armgl du PC tant que GlKit n'y est pas : `armgl_init` rend `ARMGL_NONE` (gcemu : GX sur la machine, images par `soft.cpp`) |
| `tools/emu/build.sh` | NES, GB, GBA, SNES pour le Pi (`-O3 -fno-tree-vectorize -finline-limit=1000`), binaires retirés de leurs symboles ; `build/emu/sdroot` passe devant l'asm sur la carte |
| `tools/pc/apps.sh` | nesemu, gbemu, gbaemu, snesemu, n64emu (`-DN64_CPP`), gcemu (avec `armgl_none.c`) pour le PC |
| `tools/emu/romgen.py` | `gb-stress` (Game Boy et Color : LR35902 au hasard, MBC1 2 3 5) et `gc-stress` (DOL : Gekko au hasard, entiers, flottants, paires de simples) : ROM d'essai à nous |
| `tools/emu/bench_pi.sh` | asm contre C++ dans Korin sous QEMU raspi4b (emplacement 3) : 900 images `--thumb` de chacun, temps (`time` de bash) et vignettes comparées |
| `tools/emu/check_n64_jit.sh` | interprète contre blocs, x86-64 natif (WSL) et AArch64 (qemu-aarch64), contre l'ancien recompilateur, avec la somme du code produit (`ref_n64` l'imprime) |
| `tools/emu/check_gc_jit.sh`, `ref_gc.cpp` (`GC_REGS=1`) | les deux recompilateurs du Gekko et l'interprète, aux trames où ils sont au même cycle |
| `tools/pkg/versions.ini` | nesemu, gbemu, gbaemu, snesemu 1.0.4 ; n64emu 1.0.4 ; gcemu 1.0.2 (rien de publié) |

### Vérifié

**Cœurs (WSL, `tools/emu/check_*.sh`, `ONYX=$ONYX/user/Emulators` = les copies de `src/apps`)** : asm et C++ donnent les
mêmes images, le même son, les mêmes cycles, image par image : NES 44 cas (mappers 0 1 2 3 4 7 66, CHR-RAM, PAL, démo),
Game Boy 30 ROM `gb-stress` (DMG et Color, MBC1 2 3 5 et sans MBC), SNES 7 (LoROM, HiROM, PAL), GBA 12 (SRAM, Flash 64 et
128 Ko, EEPROM).

**Recompilateur N64 (`check_n64_jit.sh`, ROM `n64-stress`, graines 1 à 6, 200 à 300 images)** : en x86-64 natif,
l'interprète et les blocs donnent la même machine à chaque image (cycles, registres, RDRAM, image, son, listes) — de 1 076
à 1 122 808 blocs traduits (graines 1 et 5 : code qui se réécrit, chaînages défaits sans cesse) ; en AArch64
(qemu-aarch64) aussi, et la même chose que l'ancien recompilateur, avec **la même somme du code produit** : le générateur
AArch64 écrit le code d'avant octet pour octet, le Pi ne perd rien à la séparation. (Les interprètes x86-64 et AArch64
diffèrent entre eux : flottants et NaN de chaque machine, conversions hors limites ; chacun est égal à ses blocs.)

**Recompilateurs du Gekko (`check_gc_jit.sh 12 1 2 3`, `gc-stress` entiers, flottants, paires)** : les blocs s'arrêtent à
la fin d'un bloc, pas à l'instruction, et les deux générateurs ne coupent pas toujours au même endroit ; aux trames où
x86-64 et AArch64 sont au même cycle (4 à 10 sur 12 par ROM), tout est égal (image, mémoire, pc, GPR, FPR et paires, CR,
LR, CTR, XER, MSR, FPSCR) ; l'interprète aussi aux trames où il est aligné. Interprètes x86-64 et AArch64 égaux.

**Pi (QEMU raspi4b, emplacement 3, `tools/emu/bench_pi.sh`, carte tirée de `build/sdroot`)** : chaque ROM d'essai lancée par l'app asm puis par l'app C++
(`--thumb` : 900 images sans fenêtre, chargement compris, un passage ; binaires de `tools/emu/build.sh` avec les réglages
retenus) : **les 8 vignettes sont identiques à l'octet**, asm et C++. Temps (secondes) :

| ROM | asm | C++ | C / asm |
|---|---|---|---|
| NES `nes-demo` | 16,5 | 12,7 | −23 % |
| NES `nes-stress` MMC3 | 21,1 | 13,2 | −37 % |
| Game Boy `gb-stress` DMG | 118,9 | 143,1 | +20 % |
| Game Boy Color `gb-stress` | 118,2 | 164,3 | +39 % |
| GBA `gba-stress` SRAM | 86,1 | 55,2 | −36 % |
| GBA `gba-stress` Flash | 17,6 | 12,6 | −28 % |
| SNES `snes-stress` LoROM | 18,5 | 31,7 | +71 % |
| SNES `snes-stress` HiROM | 21,5 | 32,8 | +53 % |

QEMU système (TCG, mémoire par softmmu) pénalise le C++, qui touche plus la mémoire que l'asm (registres fixés) ; sous
qemu-aarch64 (ci-dessous) la SNES est à égalité. **Le Game Boy et la SNES restent donc à mesurer sur le vrai Pi (F12)**
avant de retirer l'asm ; s'ils y sont plus lents, la suite est la boucle du processeur en asm par cible (`step` du GB,
`run_frame` du 65816).

**Vitesse sous qemu-aarch64 (mode utilisateur, GCC 15, 300 images, les deux derniers de trois passages, ms par image)** :
QEMU ne dit rien du Pi, c'est une comparaison entre les deux.

| | asm | C++ `-O3` | C++ retenu |
|---|---|---|---|
| NES (`nes-stress` MMC3) | 9,8 / 7,5 | 8,6 / 7,3 | 8,7 / 6,3 |
| Game Boy Color (`gb-stress`) | 60,5 / 46,2 | 79,1 / 57,9 | 60,7 / 51,5 |
| GBA (`gba-stress` SRAM) | 21,2 / 18,2 | 23,9 / 21,9 | 23,9 / 20,7 |
| SNES (`snes-stress`) | 10,7 / 10,5 | 10,9 / 10,9 | 10,9 / 11,4 |

« Retenu » : `-O3 -fno-tree-vectorize -finline-limit=1000` (la vectorisation n'apporte rien à ces cœurs et coûte des
copies ; inliner `read8`, `tick`, `getR`… les rapproche de l'asm). Le Game Boy passe de +30 % à +0 à 12 %, la NES est
plus rapide que l'asm, la SNES à égalité ; la GBA reste ~10 % derrière. Les mesures sont bruitées (plusieurs QEMU en même
temps) : la vitesse vraie est celle du Pi, F12.

**PC (QEMU q35 + OVMF, 4 cœurs, disque `mkkorindisk.py --layout zbook --korin-mb 200`, arbre de `sysroot.sh` + les apps,
une app par démarrage depuis `startup.txt`, captures)** : nesemu (démo `nes-demo` : le motif qui défile), gbemu
(`gb-stress` Color), gbaemu, snesemu, n64emu (« n64emu : recompilateur actif » dans le journal : le JIT x86-64 tourne
dans Korin, `mmap` exécutable compris, menu Processeur), gcemu (`gc-stress`, menu Processeur) : fenêtre, menus et image,
sans faute.

### État des recompilateurs

| Émulateur | AArch64 | x86-64 | Décodage commun |
|---|---|---|---|
| n64emu | C++ (`n64_jit_a64.h`) = l'ancien ; le Pi garde `n64_jit.inc` (asm) | `n64_jit_x64.h`, vérifié | oui (`n64_jit.cpp`) |
| gcemu | `gc_jit.cpp` | `gc_jit_x64.cpp` d'Onyx, vérifié contre AArch64 | non : deux fichiers complets d'Onyx, à factoriser |
| n3dsemu | `n3ds_jit.cpp`, `n3ds_shjit.cpp` | **pas fait** | décodage (`classify`, `Ins`) déjà à part, générateur A64 seul |
| ndsemu | `nds_jit.cpp` | pas fait (interprète sur le PC) | — |
| gcemu GX (`gxes_rec.cpp`, `gx_asm.asm`) | asm et C++ | le C++ (sans asm) | — |

### Pas fait, ensuite

* **Le vrai Pi** : vitesse (F12) des quatre en C++, son, manette USB ; **le JIT C++ du N64 n'y tourne pas** : le Pi garde
  `n64emu.asm` (rendu par Mesa, `n64_gfx.inc` est allé bien au-delà du `n64_gfx.cpp` d'Onyx : combineur, deux textures,
  brouillard, `n64gl_batch`). Passer le Pi au C++ demande de porter ce GX vers `n64::Machine`, puis de vérifier le
  recompilateur AArch64 C++ sur le vrai matériel (**invalidation du cache d'instructions** : `flushCode`, que QEMU ne
  montre pas).
* **n3dsemu pour le PC** : son interprète VFP est en asm AArch64 en ligne (`n3ds_arm.cpp`, `msr fpcr`, `fcmp`), les
  boucles de pixels dans `n3ds_kern.S` ; il faut d'abord une version C de ces parties, puis le générateur x86-64 de
  l'ARM11 (drapeaux NZCV de l'invité à recalculer : x86 n'a pas la retenue de soustraction d'ARM) et celui des shaders
  PICA ; aucune ROM libre 3DS ici pour l'essayer.
* **gcemu** : décodage commun aux deux recompilateurs ; l'image par GPU sur le PC attend GlKit du PC (`armgl_none.c`
  à retirer) ; aucun jeu essayé sur le PC (pas de ROM libre GameCube hors des DOL d'essai).
* Les sons sur le PC (pas de service `sound`), le mode poche, le thème Onyx Noir des quatre (EmuKit est commun : rien ne
  change par rapport à l'asm).

### gcemu figé sur le ZBook (14/11)

Retour du ZBook : Wind Waker reste noir, Kirby s'arrête après le choix 50/60 Hz ; la fenêtre ne se déplace plus et ne se
ferme plus (il faut tuer l'app).

**Cause (reproduite sous QEMU)** : `Player::pump` (`src/apps/gcemu/gxes_gl.cpp`), appelé par le crochet GL **sur le
thread de la fenêtre**, jouait l'anneau des images tant que `rd != wr`. Le GX finit une image dès qu'une place se libère :
quand la machine va plus vite que le GL, ce tour ne finit jamais (fenêtre figée), et chaque image y est « en retard »,
donc jamais montrée (écran noir, ou l'image d'avant, le choix 50/60 Hz que Kirby dessine lui-même dans le XFB). Sur le
Pi, la machine est plus lente que le GL ; sur le ZBook, le Gekko recompilé en x86-64 la dépasse. Fermer ne pouvait pas
marcher non plus : `em_stop` attendait sans limite la machine, qui attendait le GX (`gxSync`), qui attendait une place
que plus personne ne libérait.

**Corrigé** (gcemu 1.0.4, EmuKit dans `armpi` 2026.11.37 et `pc` 2026.11.3) :

| Fichier | Changement |
|---|---|
| `src/apps/gcemu/gxes_gl.cpp` | `pump` joue les images finies à son entrée, pas plus ; la dernière est montrée |
| `src/apps/gcemu/main.cpp` | le GL sur son propre thread (`gc-gl`) : la fenêtre ne fait que donner la zone et prendre le compte des images ; fin par `_exit` après la carte ; la trace |
| `src/kits/emukit/emukit.c` | `em_stop` : 2 s au plus (le crochet GL tourne pendant ce temps), puis la machine est abandonnée et le programme sort ; Ouvrir une autre ROM refusé si elle ne s'arrête pas |
| `src/apps/gcemu/gc/gc_gx.cpp`, `gc.h` | `pause` dans les attentes du GX sur x86-64 ; `gxWaiting` pour la trace |
| `tools/emu/gchb/` | `gxdemo.dol`, un programme GameCube à nous (docs/EMULATEURS.md, « GameCube : tools/emu/gchb ») et `pctest_gc.py` |

**Vérifié sous QEMU q35** (4 cœurs, TCG, GlKit en softpipe, `pctest_gc.py`, `gxdemo.dol`) : avant le correctif du lecteur,
la trace montre l'anneau qui avance (`rd` 1 → 36) et **plus aucune image montrée** après les trois premières du GX
(`gl 0`), l'écran figé sur la première : le défaut du ZBook (le GL déjà sur son thread : la fenêtre restait libre). Après : le GX à l'écran (carré texturé, triangles), `gl` 1 à 3 images par
seconde du noyau, la croix ferme l'app (`gcemu : fin` au journal). `gcemu` construit aussi pour le Pi
(`tools/emu/build_gcemu.sh`) ; **rien essayé sur le Pi ni sur le ZBook**.

**La trace pour le ZBook** : `touch SD0:/etc/gcemu-trace` (ou `gcemu --trace`), lancer le jeu, puis lire
`SD0:/home/gamelib/gc/gcemu-trace.txt` (gardé même si l'app est tuée) ; détail des champs : docs/EMULATEURS.md, « La
trace de gcemu ». Sous QEMU, l'heure du noyau avance 11 fois moins vite que le TSC de libposix (`mono`) : sur le ZBook,
les deux doivent avancer ensemble.

Pas couvert par le DOL : le DSP (AX, microcodes), le disque, les interruptions (tout par sondage).

## Apps d'Onyx en C/C++ (13/11)

Les apps de `src/apps` qui n'existaient dans Korin qu'en assembleur FASMARM mais **viennent d'Onyx** sont refaites
en C++ **depuis leur source d'Onyx** (`user/Apps/<nom>` du dépôt d'Onyx), un seul source pour le Pi et le PC :
2048, clock, control, fmtracker, freecell, gamelib, invaders, mandelbrot, pipes, same, snake, solitaire, taskman,
tetris. Règle suivie : le moteur d'Onyx (règles, modèle, formats, calculs) est repris ; les kits de Korin sont appelés
en direct (`ui_*`, `ak_*`, `bus_*`, NetKit, ArchiveKit), la libc et POSIX à la place d'AppKit ; **aucun pont, aucune
couche onyxcompat, rien de l'uikit C++ d'Onyx**. L'asm de Korin, qui avait beaucoup ajouté à Onyx (plein écran,
manette, menu de pause, décors, record, sons, thème, mode poche, langues), **fait foi pour ce qui se voit** : tout
ce qu'il fait est gardé. L'asm reste dans chaque dossier (assemblé par `build.ps1`), à retirer après validation sur
le vrai Pi. Chaque app a un `README.md` : ce qui vient d'Onyx, de l'asm, les écarts.

### Fichiers

| App | Fichiers | D'Onyx | De l'asm de Korin |
|---|---|---|---|
| 2048, snake, same, tetris, invaders, pipes | `main.cpp` | les règles (grilles, tirages, scores, vitesses) | la présentation par `gamefb.h`, `gamedraw.h`, `gamepause.h`, `gamesound.h`, `gamepad.h` (les traductions C de `gamefb.inc`… : mêmes pixels), plein écran, manette, pause, record |
| freecell, solitaire | `main.cpp`, **`src/inc/cards.h`** (nouveau : `cards.inc` en C, modèle et animation de fin de `games/cards.h` d'Onyx) | règles, parties numérotées de Microsoft (FreeCell), donne par graine (Solitaire) | dessin des cartes, glisser, annuler, Auto, fin |
| gamelib | `main.cpp` | la Game Library : consoles, cartes, vignettes | lancement des émulateurs comme `gamelib.asm` (`SD0:/apps/<emu>.app/main --full chemin`, `--thumb image chemin`), dossiers, filtres, manette |
| clock | `main.cpp`, `alarms.cpp`, `alarms.h` | le modèle des alarmes, dates, trois sons | onglets, cadran, réglage, dialogue d'alarme ; `SD0:/etc/alarms.txt` et `korin.clock.alarms` compatibles avec clockd (C) |
| taskman | `main.cpp` | onglets, grille triée, courbes | colonnes, genre, CPU par process, threads, tas, commutations, confirmation, interfaces de netd |
| mandelbrot | `main.cpp`, `fractal.h`, `kernels.cpp` | (petit : 4 types en virgule fixe) | les 13 types, signets, palettes, PNG ; `kernels.inc` (NEON) en C++ portable (vecteurs de GCC, `fma` là où l'asm a `fmla`, `-O3` par `#pragma`) |
| fmtracker | `main.cpp`, `fms.h`, `view.h`, `ins.h` | `FmsSong`, `.FMS` / `.FMI`, annulation, presse-papiers, lecture | la fenêtre entière, éditeur d'instrument en entiers, menus ; mêmes fichiers octet pour octet, WAV par le décodeur FM Song d'AudioKit |
| control | `main.cpp`, `applet.cpp`, `applist.cpp`, `control.h`, `applets/*.cpp` (theme, keyboard, display, dock, dockconf, panel, wifi, remote, sound, padconf, preload, dotnet, pkgman, printers) | `control` d'Onyx, principes de ses apps de réglage (displayconf, keyconf, wpaconf, soundconf…) | les volets de `applets/*.inc` (refaits par Korin) ; copies locales de `wifidraw`, `dockconf`, `panelconf`, `applist` (celles de `src/sys/common` sont pour korinrt) |

* `tools/office/build.sh`, `tools/pc/apps.sh` : les quatorze dans les listes ; `apps.sh` met aussi `ins/`, `lang/`
  dans `fmtracker.app` et `songs/` dans `home/music/fms` (comme `build.ps1`).
* `tools/pkg/versions.ini` : armpi (control, taskman), korin-srcs et les douze paquets d'apps : +1 (rien de publié).

Tailles (asm → C Pi / C PC, octets) : 2048 8 832 → 140 032 / 164 664 ; clock 34 424 → 160 512 / 193 336 ; control
231 848 → 226 064 / 254 776 ; fmtracker 53 736 → 275 440 / 316 456 ; freecell 80 872 → 156 400 / 181 016 ; gamelib
13 160 → 148 864 / 177 592 ; invaders 29 568 → 176 880 / 213 784 ; mandelbrot 43 144 → 275 608 / 328 920 ; pipes
29 424 → 180 976 / 213 784 ; same 26 544 → 168 704 / 201 528 ; snake 29 656 → 172 784 / 201 496 ; solitaire
68 672 → 156 400 / 181 016 ; taskman 90 856 → 152 320 / 181 048 ; tetris 29 456 → 176 880 / 213 784.

### Écarts avec l'asm (le reste est l'asm, bizarreries comprises et commentées)

* Arguments : `argv[1..]` au lieu de la ligne brute (`ui_arg`) ; seuls des espaces doubles ou des guillemets dans un
  chemin différeraient.
* 2048 se redessine aux couleurs d'un nouveau thème dès `WEV_THEME` (l'asm attendait le coup suivant).
* clock lit aussi la valeur gardée de `korin.clock.offset` au lancement : même heure que menubar, sur le PC aussi
  (l'asm ne lisait que `clock.cfg`).
* taskman : une colonne de tri à -1 trie par nom (l'asm lisait hors de sa table).
* mandelbrot : jusqu'à 16 threads (`sysconf`) au lieu de 4, points par quatre au lieu de deux (même résultat),
  tampons libérés.
* gamelib : nom de la carte choisie en `TJ_ACCENT_INK` (blanc en dur dans l'asm), images libérées au rafraîchissement.
* control : la liste des dispositions de Région montre « Français » là où l'asm montre « FR » (ce que décrit l'en-tête
  de `keyboard.inc` ; l'asm perd sans doute le nom dans un registre détruit par `str_eq`) ; une clé Wi-Fi de plus de
  63 octets est refusée (l'asm écrasait le 0 final) ; `dllpreload` lancé sans argument.
* fmtracker : notes d'un motif en tableau à sa taille (Onyx) au lieu d'une case fixe de 8 × 256 : sans effet visible.

### Vérifié

**Pi (QEMU raspi4b, emplacement 2, `tools/apps/compare.sh`, 13/11)** : asm et C sur deux cartes tirées de
`build/sdroot`, mêmes scripts de `qemu_drive.py` :
* **clock** (horloge, +1 h, alarmes : liste, modification, désactivation, Suppr, ajout ; Givre et Onyx Noir),
  **mandelbrot** (13 types, zoom, palette, glisser, signets, choix de c ; PNG relu égal à l'écran), **control** (les
  volets, Givre et Onyx Noir, thème changé à chaud, résolution changée puis rendue par Échap ; `services.txt`,
  `sound.txt`, `preload.txt` écrits identiques), **fmtracker** (`.FMS` édité et enregistré : même fichier),
  **freecell** (partie 617, annuler, partie 1 résolue en 80 coups), **solitaire** (Auto, glisser, annuler, talon,
  donne résolue), **gamelib** (ROM d'essai, filtres, F5, double-clic : nesemu lancé sur la ROM, vignettes) :
  **identiques au pixel** hors heure, fond voronoy, animations tirées au hasard (fins de partie ; Clifford, De Jong,
  Buddhabrot : partage des lots entre threads, comme l'asm) et coins arrondis.
* **taskman** : quatre onglets, tri, Entrée, Suppr et sa confirmation : même mise en page (valeurs vivantes).
* **2048, snake, same, tetris, pipes, invaders** : titre, partie, pause, fin, record : même rendu et même
  comportement, pas au pixel (hasard semé à l'heure, fonds animés), comparés à l'œil.
* Vitesse de mandelbrot (QEMU, asm / C) : Mandelbrot 536 / 552 ms, Multibrot 3 1 591 / 973 ms, Lyapunov 7 766 /
  4 168 ms, Buddhabrot 9 825 / 10 009 ms (TCG : rien sur le vrai NEON).
* Toutes construites avec `-Wall -Wextra` sans avertissement dans leurs sources (restent ceux de `uikit.h` et
  `gamefb.h` compilés en C++ : `-Wcast-function-type`).

**PC (QEMU q35 + OVMF, 4 cœurs, 1280 × 800, disque de `mkkorindisk.py --layout zbook --no-windows --korin-mb 200`,
arbre de `sysroot.sh` + les quatorze apps, une app par démarrage dans `startup.txt`, 13/11)** : captures de la
mémoire vidéo par `pmemsave`, planches `build/pc/onyx_apps_1.png` à `_4.png`. Chaque app s'ouvre (« init2 :
SD0:/apps/<nom>.app/main -> N », aucune faute) : **2048** (flèches : tuiles glissées), **snake** (Entrée, Haut : le
serpent tourne), **tetris** (partie, Espace), **clock** (même heure que menubar), **same**, **pipes** (tuyaux posés au
clavier), **invaders** (tir), **freecell 617**, **solitaire 1**, **gamelib** (« Aucun jeu pour l'instant » : pas de
ROM), **mandelbrot** (30 ms l'image), **taskman** (23 process, threads, tas), **fmtracker** (`ABDEL.FMS` ouvert depuis
`home/music/fms`), **control** (Thème ; `control ecran` : 1280x800, seule résolution ; `control sound`).

### Pas vérifié

* Le **vrai Pi** et le **vrai ZBook**. **Invaders** : sous QEMU la version C a perdu ses vies plus vite que l'asm dans
  deux parties (canon immobile : 24 s contre ~60 s), alors que le code des bombes, des tirs et des collisions, relu
  ligne à ligne, est le même : peut-être le hasard (deux essais), à regarder sur le Pi.
* Les menus de la barre au clic sous QEMU (ni l'asm ni le C ne s'ouvrent au clic) : Recommencer, Tirer trois cartes,
  Plein écran… seulement par lecture du code ; « Appliquer et relancer » d'Écran ; le Wi-Fi avec une vraie puce ; le
  son (Essayer de clock, FM Tracker, jeux).
* Sur le PC : la charge par cœur de taskman ; NetKit du PC pour `wifi_country` et NTP (volet Région) ; le glisser
  des cartes à la souris, le redimensionnement, mandelbrot sur plus de 4 cœurs, le PNG enregistré ; les émulateurs
  lancés par gamelib.
* Le selfhost (`pibuild` sur des dossiers qui contiennent les deux sources).

### Ce qui manque aux kits

Rien : aucun ajout aux tables `.abi`. `src/inc/cards.h` est le seul en-tête commun nouveau. Les copies locales
(`wifidraw`, `dockconf`, `panelconf`, `applist` dans control, `alarms` dans clock) seraient à fondre avec celles de
`src/sys/common` dans une version pour libposix, quand programmes système et apps partageront le même C.

## Son du PC, HDA (13/11)

Le son du ZBook par un pilote **Intel HDA** en C, sans le DSP du cAVS ; étude : [SON-PC.md](SON-PC.md). Le service
`sound` n'existait qu'en asm : il est repris en C pour les deux machines (le Pi garde l'asm).

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/sound/sound.c` | `sound` en C, pas à pas d'après `sound.asm`, `mix.inc`, `settings.inc` (protocole, anneaux, tampon d'état, `sound.txt`, `korin.audio.state`, avis) ; PC : pas de canaux DMA ; ajout : op 20, `d` = volume de départ d'une sortie que `sound.txt` ne retient pas |
| `src/svc/hda/hda.c` | contrôleur : remise à zéro, `cAVS` (PPCTL à 0 : GPROCEN coupé ; lien 0 de ML allumé), CORB/RIRB 256 entrées (RIRBSTS effacé à chaque lecture : sinon QEMU s'arrête après RINTCNT réponses), repli en commande immédiate (CORB qui ne prend plus les commandes, ou `hda_immediate`), MSI ; flux de sortie (BDL 4 × 512 trames, IOC, LPIB), flux d'entrée lancé à la demande (`SR_WANTED`), options |
| `src/svc/hda/codec.c` | codec générique : AFG, widgets, listes de connexions (plages comprises), chemins en profondeur (DAC libre d'abord), mixers (entrée voulue ouverte, les autres fermées), amplis à 0 dB, EAPD, détection de présence (événement non sollicité + relecture toutes les 500 ms), ALC285 : coefficients 0x36/0x10 (EAPD), Fury G8 `103c:8870`/`103c:886d` : 100 ms puis coefficient 0x65 = 0, LED GPIO 0 et 2 éteintes |
| `src/svc/hda/plat_x86_64.c` | pcid : fonction, D3 → D0, BAR0, MSI ; Intel : TCSEL, DEVC (snoop), CGCTL.MISCBDCGE autour de la remise à zéro ; NVIDIA : 0x4c/0x4d/0x4e |
| `src/svc/sndif/sndif.c`, `.h` | `sndif_volume0` (volume de départ, champ `d` de l'op 20 ; 0 pour usbaudio, l'asm du Pi l'ignore) |
| `skel/sd0/etc/drivers.txt` | `pci:04/03 = SD0:/sys/svc/hda`, `pci:8086:43c8 = SD0:/sys/svc/hda` |
| `skel/pc/etc/services.txt` | `SD0:/sys/svc/sound` |

La sortie s'inscrit en type 1 (« jack » : sortie analogique, derrière l'USB) sous le nom « Analogique (ALC285) »,
l'entrée en type 2 (micro hors USB) « Micro (ALC285) ». Volume dans sound ; **volume de départ 50** (gain 0,25,
−12 dB) ; options de `SD0:/etc/options.txt` : `hda_volume=N`, `hda_quirks=0` (codec générique pur),
`hda_gpio=0xMM` (GPIO du codec en sortie à 1, essai), `hda_nomsi`, `hda_immediate`.

### Vérifié sous QEMU (q35 + OVMF, 4 cœurs, NVMe, `mkkorindisk.py --layout zbook --korin-mb 200`, bureau, 13/11)

`-device intel-hda -device hda-duplex,audiodev=snd0 -audiodev wav,id=snd0,path=…`, `startup.txt` : `sontest`,
`soundctl`, `recorder --test 1` (boucle) pendant `sontest`, `recorder --test 0` (micro) :
* chaîne pcid → hwmanager (`pci:04/03`) → `hda` ; « codec 0 : 1af4:0022 … pin 03 : sortie de ligne en prise → DAC
  02 », « entrée : pin 05 → ADC 04 » ; sound : « sortie 2 « Analogique (1af4:0022) », 44100 Hz », devenue principale,
  volume 50 ; interruption MSI.
* **le wav de QEMU** (44,1 kHz stéréo) : la gamme 261 → 523 Hz puis la **440,000 Hz** (615 périodes, passages par
  zéro interpolés), **crête 2 500** = 10 000 (sontest) × 0,25 (volume 50), RMS 1 768 (sinus pur), gauche = droite,
  **aucun trou** ; 4,4 s de son pour les 202 860 trames. (La « cadence » que dit sontest, ~65 000 trames/s, vient
  de l'horloge de l'invité sous TCG : le wav a la bonne durée.)
* `recorder --test 1` (boucle de la principale) pendant sontest : **crête 9 999**, comme sur le Pi (mélange de
  `sound.c`) ; `--test 0` : « capture ouverte (44100 Hz) », puis « capture fermée » 1 s après ; avec
  `-audiodev none` les trames du micro (silence) arrivent (647 168 octets) ; avec `wav`, QEMU n'a pas d'entrée (0).
* `ich9-intel-hda` + `hda-micro` : « haut-parleur », « micro en prise », même wav (440,000 Hz, crête 2 500).
* `hda_immediate` + `hda_volume=30` : énumération et lecture par l'interface immédiate, crête 900 (0,09 × 10 000).
* Pi (QEMU raspi4b, emplacement 3, `usb-audio`) avec le `usbaudio` reconstruit (sndif changé) contre celui de la
  carte : mêmes lignes, `sontest` 202 860 trames en 4 644 ms dans les deux, wav à 48 kHz : crête 9 411, la 438,6 /
  438,7 Hz (horloge de l'USB de QEMU) dans les deux : le son du Pi ne change pas.
* `tools/check_constants.py` passe ; `build_c.sh sound hda` sans avertissement (`-Werror`).

Pas vérifiable sous QEMU : le cAVS (PP, ML, CGCTL), l'ALC285 et ses coefficients, la prise casque (les codecs de
QEMU n'ont pas de détection de présence), le micro réel, le HDA de la NVIDIA.

### Marche à suivre sur le ZBook (par l'utilisateur)

1. Construire et installer (PowerShell administrateur, `C:\Projects\ARMPI`) : `wsl sh tools/pc/build.sh`,
   `wsl sh tools/pc/sysroot.sh`, `… korin.ps1 install -DryRun` (la ligne « Partition KORIN OS : D: »), puis
   `… korin.ps1 install`. **Casque débranché** pour le premier démarrage.
2. Démarrer Korin. Le volume de départ est modéré (50 : −12 dB). Dans le Terminal : `soundctl` (la sortie
   « Analogique (ALC285) » doit être principale), puis `sontest` : **une gamme de do à do (8 notes de 0,3 s) puis
   un la tenu 2 s**, assez doux, par les haut-parleurs.
3. Brancher un casque : le son passe au casque (haut-parleurs coupés) ; `sontest` de nouveau ; débrancher : retour aux
   haut-parleurs. Monter le volume par la barre de menus ou `soundctl volume 2 70` si c'est trop faible.
4. Micro de la prise (casque avec micro) : `SD0:/apps/recorder.app/main --test 0` en parlant : « crête » non nulle.
5. `journal`, redémarrer sous Windows, rapporter de `D:\journal.txt` toutes les lignes « hda », « sound » et
   « ALC285 » : les deux contrôleurs (« hda 00:1f.3 », « hda 01:00.1 »), « cAVS : PPCTL … », « cAVS : lien 0 … »,
   « codecs présents : … », « codec 0 : 10ec:0285 … sous-système 103c:… », chaque ligne « pin … » (configs,
   chemins), « coefficient … », « prise … branchée / débranchée ».

**Signes de problème** : « le CORB ne répond plus » (repli : marche quand même, à noter) ; « remise à zéro : CRST
reste à 0 » ou « codecs présents : 0000 » (le lien HDA ne s'allume pas : le DSP tient peut-être le lien, rapporter les
lignes cAVS) ; « le flux de sortie n'avance pas » (DMA : rapporter LPIB/CTL/STS) ; aucun son malgré « en service »
(essayer `hda_gpio=0x02` puis `0x08` dans `options.txt`, ou `hda_quirks=0`, un essai par démarrage, et rapporter) ;
son au casque mais pas aux haut-parleurs ou l'inverse (rapporter les lignes « pin »). Un son fort ou un craquement
continu : couper par la barre de menus (muet) ou `soundctl volume 2 0 1`. Le HDA de la NVIDIA doit dire « pas de
sortie analogique sur ce contrôleur : fin » (HDMI pas servi : normal).

### Reste

* **DMIC** du ZBook (micros intégrés) : seulement par le DSP (SOF : micrologiciel signé, topologie, IPC) ; hors de portée.
* **HDMI/DP** : codec d'Intel (attend l'audio de l'affichage, `intelgpu`), HDMI de la NVIDIA (attend un affichage
  NVIDIA) ; ELD, InfoFrame, canaux.
* ALC285 : étalonnage de l'ampli du casque (`alc285_hp_init` de Linux, coefficients 0x58…) et mode casque-micro
  combiné (0x48, 0x49, 0x44, 0x63) si le casque ou son micro restent muets ; horloge du lien (`SCF`) ; veille du codec
  et D3.
* `sound.c` sur le Pi : à comparer à l'asm (`tools/svc/compare.sh`) avant de l'y mettre ; les sons des émulateurs sur le
  PC passent maintenant par sound.

## bash sur le PC (13/11)

GNU bash 5.2.37, **le même que le Pi** ([BASH.md](../BASH.md)), construit pour Korin PC : `SD0:/bin/bash` de l'arbre
du PC. La console texte (`src/svc/console`) et le Terminal (`src/apps/terminal`) le prenaient déjà s'il existe
(sinon `sh`) : rien n'y change.

### Construire (WSL)

```
ARCH=x86_64 sh tools/posix/build.sh   # libposix du PC, d'abord
ARCH=x86_64 sh tools/bash/build.sh    # build/pc/bash/bash (1,3 Mo), ~/.cache/korin-bash-x86_64
sh tools/pc/sysroot.sh                # le copie en bin/bash s'il est construit, avec etc/bash.bashrc
```

* `tools/bash/build.sh` : `ARCH=x86_64` (chaîne `x86_64-onyx-elf`, `build/pc/posix/lib/posix.specs`, sans
  `-mcpu=cortex-a72`), mêmes retouches et mêmes réponses à `configure`. Deux points valent pour les deux :
  `CC_FOR_BUILD="gcc -std=gnu17"` par défaut (le GCC 15 de WSL compile `mkbuiltins` en C23, où `f ()` ne prend
  aucun argument : la construction du Pi le passait à la main) et l'archive prise dans `~/.cache/korin-src/bash` si
  `/opt/src/bash` manque. **Pi** : `prebuilt/bash` refait par l'ancien et le nouveau script, identique à l'octet.
* `TESTS=1` : la suite d'essais de bash dans `build/pc/bash/tests` (pas essayée sur le PC).

### Ce qu'il manquait au noyau du PC (commit à part, le Pi ne change pas)

* **SIGTTOU d'un terminal** (`sys_sig_send` avec `SIGF_TTY`) : le noyau du PC l'envoyait même quand tous les
  threads le masquent. Le fils de bash fait `tcsetpgrp` avec SIGTTOU masqué (`give_terminal_to`) : le signal restait
  en attente et l'arrêtait dès qu'il le démasquait, d'où un « Stopped » à chaque commande. Comme `sig_send_tty` de
  `jobs.asm` : rien d'envoyé si le signal est ignoré ou masqué partout (1), ni si le groupe est orphelin (2).
* **exec** : `sys_exec` ignorait `EXP_STDIN..` et `EXP_MARKS`. Il pose maintenant le masque des entrées / sorties,
  envoie l'avis `SVC_OP_EXEC` à vfs (`exec_stdio` du Pi) et pose les marques des poignées (`exec_marquer` : celles
  de la liste vérifiées avant, posées après le point de non-retour, une poignée marquée `0x80000000` hors de la
  liste fermée). Sans cela, après `fork` + `dup2` + exec, le programme écrivait sur le terminal (`$(uname -m)`,
  tubes, `> fichier`) et libposix perdait son état (descripteurs au-delà de 2, environnement, fils) : c'était
  l'écart « exec 7/8 » de « Chaîne x86-64 et apps C/C++ ».

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, `mkkorindisk.py --layout zbook --korin-mb 400`, bureau, 13/11)

Terminal du bureau ouvert par `SD0:/etc/startup.txt`, `keyb=US`, touches par `sendkey` ; captures
`build/pc/bash_*.png` (mémoire vidéo par `pmemsave`) :

* `echo $BASH_VERSION` : `5.2.37(1)-release` ; l'invite `korin:\w\$` de `/etc/bash.bashrc` ; `cd SD0:/home/t`.
* **Édition de ligne** : flèche du haut (historique), Début, flèches, insertion ; **Tab** complète
  `ls SD0:/apps/term` en `SD0:/apps/terminal.app/`, puis `ma` en `main` (`bash_tab.png`).
* **Script `#!/bin/bash`** rendu exécutable par `chmod +x` puis `./demo.sh` : tableau et tableau associatif,
  `${!a[@]}`, `${v^^}`, `for`, `while (( ))`, `$(uname -m)`, `$( cat | grep -c )`, `> /tmp/…`, `tr | head`, `2>&1 |`
  (`bash_script.png`). Aussi `x=$(echo hi)`, `` `echo` ``, `echo abc | tr a-z A-Z`, `> /tmp/z; cat`.
* **Tâches** : `sleep 300 &`, `sleep 200 &`, `jobs`, `fg %1` puis Ctrl+C (code 130), `kill %2` (« Terminated »),
  Ctrl+C sur une commande au premier plan sans tuer bash (`bash_jobs.png`) ; `sleep 40`, **Ctrl+Z**
  (« Stopped »), `jobs`, `bg`, `fg`, Ctrl+C (`bash_ctrlz.png`).
* **Console texte** (`shell=console`) : bash y démarre, boucle, tube, Ctrl+C (code 130).
* **posixtest** (lancé par bash, `bash_posixtest.png`) : exec **15/0** (avant : 7/8), fork 15/0 (1 sauté), proc 14/0,
  signal 18/0, misc 24/0, io 9/0, time 7/0, thread 19/0, xbit 32/0 (avec les apps dans l'arbre : `files.app` y est), dl 26/0 ; ipc 27/1 et alarm
  12/1 comme avant (`mprotect` d'une projection privée, délai sous QEMU). `ttytest` 33/1 et `jobtest` (lancé depuis un
  shell, il s'arrête après 10 essais) : mêmes résultats avec l'ancien et le nouveau noyau, à la console sans bash.

### Pas fait, écarts

* Pas de commande `clear` sur le PC (Ctrl+L de readline la remplace) ; une touche tapée pendant qu'une commande
  finit peut s'afficher une fois de trop devant l'invite (écho du terminal avant que readline reprenne la main,
  comme sous Unix).
* `jobtest` est fait pour tourner sans terminal (il devient meneur de session) : depuis un shell, `setsid` échoue
  et il s'arrête ; et le noyau du PC ne jette pas encore les signaux d'arrêt envoyés à un **groupe orphelin**
  (seul le cas SIGTTIN / SIGTTOU d'un terminal le fait).
* Les essais de bash (`TESTS=1`), le vrai ZBook, `korin.ps1 install` (il copie `bin/` : bash y part) : pas essayés.
* Pas de paquet `bash` pour le PC (`tools/pkg`).

## Extinction et redémarrage du PC (13/11)

Sur le PC, `shutdown`, `reboot` et le bouton Éteindre du bureau ne faisaient rien (init2 du PC n'avait pas de
couche d'arrêt, le dialogue `SD0:/sys/shutdown` n'existait qu'en asm). Même chemin que le Pi pour l'utilisateur :
la commande ou le dialogue demandent à init2 (`INIT_OP_REBOOT`, bit 1 : arrêt), qui répond, attend 500 ms, puis
passe la main à la couche de la machine.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/kernel/arch/pc/power.c` | `arch_power_off` : ACPI **S5** sans interprète d'AML : `SLP_TYPa/b` lus dans le paquet `\_S5_` du DSDT (NameOp, `\` facultatif, PackageOp, PkgLength, éléments Zero/One/Byte/Word/DWord ; sinon une SSDT, sinon 0), mode ACPI par `SMI_CMD` ← `ACPI_ENABLE` si `SCI_EN` est à 0 (3 s d'attente), `WAK_STS` effacé, `wbinvd`, `SLP_TYP` puis `SLP_TYP \| SLP_EN` dans `PM1a_CNT` et `PM1b_CNT` (ports ou mémoire, les `X_` de l'ACPI 2+ d'abord) ; matériel réduit : `SLEEP_CONTROL_REG` ; **repli** 3 s après : la console du noyau reprend l'écran (`console_silence(0)`), « Korin OS est arrêté… vous pouvez éteindre l'ordinateur (bouton d'alimentation tenu quelques secondes) », cœur arrêté. `arch_reboot` (repris de `pc_main.c`) : `RESET_REG` (ports, mémoire, ou configuration PCI du bus 0) si `RESET_REG_SUP`, puis `0xCF9` 0x02 puis 0x06, puis `0x64` ← `0xFE`, puis **triple faute** (IDT vide, `int3`), 500 ms entre chaque. Avant les deux : les autres cœurs arrêtés par une **NMI** (« tous sauf moi » : arrêtés même interruptions coupées). Au démarrage, une ligne au journal : `ACPI : extinction \_S5_ (SLP_TYP a/b), PM1a_CNT …, SMI_CMD … ACPI_ENABLE …, mode ACPI actif / inactif ; redémarrage port cf9 = …` |
| `src/kernel/arch/pc/acpi.c`, `acpi.h` | la FADT : `SMI_CMD`, `ACPI_ENABLE`, `PM1a/b_EVT_BLK`, `PM1a/b_CNT_BLK`, leurs `X_` (mémoire ou ports), `SLEEP_CONTROL_REG`, `SLEEP_STATUS_REG` |
| `src/inc/syscalls.inc`, `src/kernel/core/hw.c`, `syscall.c` | **`SYS_POWER` (0x7F)** : `POWER_OFF` 1, `POWER_REBOOT` 2 ; droits de `SD0:/sys` ; ne revient pas ; Pi : `E_NOSYS` (sa table s'arrête à 0x7D). `docs/ABI.md` |
| `src/kernel/core/monitor.c` | `poweroff` au moniteur (sans rien vider : `run nvme stop` d'abord) |
| `src/svc/init2/init2_x86_64.c` | `plat_reboot` du PC : **SIGTERM** à tous les process sauf init, init2, ce qui tient les disques et les volumes (pcid, nvme, vfs, fatfs, ramfs, imgblk, usbd, usbstorage, sdhci, rtsx) et les pilotes d'un matériel qui s'arrête avec la machine (hda, intelgpu) ; 3 s, puis **SIGKILL** pour ceux qui restent (« arrêt forcé de … ») ; 200 ms pour les avis de fin (fichiers fermés chez fatfs : entrées des répertoires écrites) ; `SD0:/sys/svc/intelgpu stop` si le service tourne (3 s au plus) ; chaque volume de `VFS_VOLUMES` : **`FS_SYNC`**, puis **`VFS_UNMOUNT`**, `SD0:` en dernier (vfs arrête fatfs) ; **`BLK_SIG_STOP`** sur `blk.nvme0` (FLUSH, files rendues, `CC.SHN`, `SHST`), nvme attendu jusqu'à sa fin (12 s au plus) ; « init2 : prêt en N ms » ; `SYS_POWER`. Le Pi ne passe pas par là (`init2_aarch64.c` inchangé) |
| `src/bin/shutdown/shutdown.c` | **nouvelle commande** (Pi et PC) : `shutdown` (ou `-h`, `-P`, `now`) éteint, `shutdown -r` redémarre, autre chose : l'usage (code 2) ; comme `reboot` : message, demande à init2, échec (code 1) si init2 manque ou si la machine tourne encore 10 s après |
| `src/bin/reboot/reboot.c` | sur le PC, l'échec dit « le PC » (le Pi : inchangé) |
| `src/sys/shutdown/shutdown.c`, `sys.conf` | le **dialogue** en C, traduit ligne à ligne de `shutdown.asm`, un seul source pour le Pi et le PC (`build.ps1` : `$sys_c`, il remplace l'asm sur la carte du Pi) ; seul écart : « le PC tourne toujours » sur le PC |
| `tools/pc/qemu_nvme.py` | étape **`wait:S`** : attendre que QEMU s'arrête de lui-même et écrire ses événements (`SHUTDOWN`, `guest-shutdown` / `guest-reset`) ; QEMU qui s'arrête pendant une autre étape : plus d'exception, « QEMU s'est arrêté de lui-même » |

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, NVMe, `mkkorindisk.py --layout zbook --korin-mb 120`, copie du disque sans `--snapshot`, `-no-reboot`)

* Journal : `ACPI : extinction \_S5_ (SLP_TYP 0/0), PM1a_CNT port 604, PM1b_CNT 0, SMI_CMD b2 ACPI_ENABLE 2, mode
  ACPI actif ; redémarrage port cf9 = f` (la FADT et le DSDT de QEMU q35).
* Console texte (`shell=console`, sh) : `echo essai-sd0 > SD0:/t0.txt`, `echo essai-sd1 > SD1:/t1.txt`, `shutdown`
  → « shutdown : extinction... », init2 : SIGTERM (hidmanager, ps2kbd, sound, busd, console, hwmanager, shutdown,
  sh, vterm finis, code -15), `sd0` et `sd1` vidés (`FS_SYNC` → 0 ; `ram0` : -22, ramfs n'a pas de sync), `ram0`,
  `sd1`, `sd0` démontés, « nvme : arrêt propre en 0 ms (cache vidé : oui, CSTS 00000009) », « Extinction demandée
  par le process 7 (init2) » → **QEMU : `SHUTDOWN` `guest-shutdown`**, code 0. Même chose avec `reboot` → QEMU
  s'arrête (`-no-reboot`).
* Bureau (`shell=desktop`, tablette USB) : le Terminal ouvert par le panel, deux `echo` puis `shutdown` → extinction ;
  bouton d'alimentation du panel → le dialogue (capture), **Éteindre** → « Extinction... » dans le dialogue puis
  **`guest-shutdown`** ; **Redémarrer** → **`guest-reset`**. Elegant, panel, menubar, voronoy, clockd, terminal
  finissent au SIGTERM ; notifyd, clipd, usbgamepad seulement au SIGKILL (3 s de plus) ; « init2 : prêt en
  4 105 ms ».
* **Après chaque essai, l'image relue dans WSL** (`mtype -i IMG@@71M`) : les fichiers écrits juste avant sur
  `SD0:` et `SD1:` (`/home`) sont là avec leur contenu, et `fsck.vfat -n` de la partition KORIN OS ne trouve rien.
* Repli : un noyau d'essai qui écrit un mauvais `SLP_TYP` → 3 s plus tard le bureau disparaît et « Korin OS est
  arrêté : les fichiers sont enregistrés et les disques arrêtés. L'extinction par l'ACPI n'a pas eu lieu : vous
  pouvez éteindre l'ordinateur (bouton d'alimentation tenu quelques secondes). » s'affiche (capture).
* Pi (QEMU raspi4b, emplacement 3, carte tirée de `build/sdroot`, dialogue lancé par `startup.txt`) : asm contre
  C, le dialogue est **identique au pixel** ; Échap le ferme (code 0) ; le bouton d'alimentation le rouvre ;
  Éteindre → « init2 : redémarrage demandé par le pid 43 », QEMU s'arrête, dans les deux. `SD0:/bin/shutdown`
  (C) sur le Pi : « shutdown : extinction... », init2 prévenu, QEMU s'arrête.
* `tools/check_constants.py` passe ; `build_c.sh` (`-Werror`) sans avertissement pour init2, shutdown (commande et
  dialogue), reboot.

Pas vérifiable sous QEMU : l'ACPI d'HP (le `\_S5_`, le passage en mode ACPI, le `RESET_REG`), le FLUSH du vrai
SK hynix.

### Marche à suivre sur le ZBook (par l'utilisateur)

1. Construire et installer comme d'habitude : `wsl sh tools/pc/build.sh`, `wsl sh tools/pc/sysroot.sh` (puis
   `sysroot_apps.sh` pour le Terminal), `… korin.ps1 install -DryRun`, puis `… korin.ps1 install`.
2. Démarrer Korin. Dans le Terminal : `echo essai > SD1:/essai.txt`, puis `shutdown`. Attendu : le bureau
   disparaît après 3 à 5 s, quelques lignes du noyau, puis **la machine s'éteint** (voyant éteint). Si le message
   « vous pouvez éteindre l'ordinateur » reste à l'écran : S5 n'a pas marché, éteindre au bouton (les fichiers
   sont déjà écrits) et me le dire.
3. Rallumer sous Windows : `D:\home\essai.txt` doit être là ; `chkdsk D:` (sans `/f`) propre.
4. Démarrer Korin, bouton d'alimentation du panel (en bas à droite) → **Redémarrer** : la machine doit
   redémarrer (le menu de démarrage, puis Korin). Puis `reboot` au Terminal, et **Éteindre** par le dialogue.
5. Rapporter de `D:\journal.txt` (`journal` dans Korin avant d'éteindre) la ligne « ACPI : extinction … » (les
   `SLP_TYP`, `PM1a_CNT`, `SMI_CMD`, le mode ACPI, le `RESET_REG`).

**Signes de problème** : écran figé sans le message (un cœur ou un service bloqué) ; « nvme ne s'est pas arrêté
en 12 s » ; un redémarrage qui reste figé ou qui éteint (rapporter la ligne « ACPI : extinction … ; redémarrage … »).

### Pas fait, ensuite

* **`_PTS` et `_GTS`** (méthodes AML à appeler avant S5) : pas d'interprète d'AML ; la plupart des machines
  s'éteignent sans, mais certaines veulent `_PTS(5)` (l'EC prévenu). S'il le faut : ACPICA (prévu dans un
  service) ou un mini-interprète pour `_PTS` seul.
* **Veille S3** (et S0ix du Tiger Lake), **couvercle** (`_LID`, événement GPE de l'EC), **bouton
  d'alimentation** (événement fixe `PWRBTN_STS` de PM1 : il faut le SCI, donc les GPE et l'EC) : rien encore ; le
  bouton tenu 4 s coupe toujours le courant.
* Les pilotes ne sont pas arrêtés un par un : usbd, hda, sdhci, rtsx restent en marche jusqu'à la coupure (le
  matériel s'éteint ou se remet à zéro avec la machine) ; intelgpu par `stop` ; une clé USB montée est vidée et
  démontée comme les autres volumes, mais pas arrêtée (pas de SYNCHRONIZE CACHE).
* notifyd, clipd, usbgamepad ignorent SIGTERM (bloqués dans un appel qui ne s'interrompt pas) : 3 s de plus ; un
  vrai avis de fin de session (Elegant prévient les apps : « enregistrer ? ») reste à faire, sur le Pi aussi.
* Le Pi garde son arrêt : `INIT_OP_REBOOT` n'y vide ni ne démonte les volumes (fatfs y écrit tout de suite, sauf
  les entrées des fichiers encore ouverts).
* Mode poche du PC : home et quick n'existent qu'en asm (pas sur le PC) ; la barre de menus n'a pas d'entrée
  Éteindre (comme sur le Pi).

## Retours du ZBook : bash, casque USB, RVZ (13/11)

Trois défauts vus sur le vrai ZBook : bash refusait les programmes de `SD0:/sys` (« Not owner »), un casque USB
était vu mais muet (« tuyau isochrone refusé »), gcemu ne voyait pas les jeux en RVZ.

### bash : « Not owner » (Pi et PC)

`posix_spawn` de libposix (donc `exec`, `system`, `popen`, et bash par `fork` + `exec`) passait un programme de
`SD0:/sys` à `f_spawn_file` d'AppKit, qui le refuse sans droits (`EPERM`, « Not owner » de newlib). `sh` passe par
`fc_spawn` d'AppKit, qui le fait lancer par init2. **Le Pi avait le même défaut** (vérifié : l'ancien bash du Pi,
`SD0:/sys/svc/hwtest` → « Not owner », code 126).

* `src/lib/posix/proc.c` : dans `do_spawn_locked`, un fichier `ATTR_SYS` demandé par un process sans
  `PROC_RIGHTS` part à init2 (`INIT_OP_LAUNCH`, chemin résolu par vfs, ligne d'arguments), comme `fc_spawn`. Le
  fils est noté « détaché » : son père est init2, `waitpid` attend sa fin (`SYS_PROC_INFO` toutes les 20 ms) et
  rend 0 (le vrai code n'arrive qu'à init2). Sa sortie va au journal, comme depuis `sh`.
* Séparation gardée : rien ne change dans le noyau ni dans init2 (il n'accepte que `SD0:/sys/…`, sans `..`) ; un
  programme hors de `SD0:/sys` reste lancé sans droits ; `urun` et wtest ne sont pas touchés.
* `prebuilt/bash` (Pi) et `build/pc/bash/bash` reconstruits. bash est lié en statique : **tout programme POSIX doit
  être relié** pour avoir la correction (gcc 1.0.10 : la libposix de `/gcc/lib`, à refaire par
  `tools/gcc/build.sh`) ; essais du Pi (`posixtest`…) reconstruits ; bash 1.0.2, system-tests 1.0.1.
* Docs : `docs/POSIX.md`, la compétence `korin-posix`, le guide `libc_fr.md`.

Vérifié : **PC** (QEMU q35, bash lancé au démarrage) : `SD0:/sys/svc/intelgpu status` → « init2 : launch par 23
bash : SD0:/sys/svc/intelgpu -> 24 », code 0 ; `intelgpu start`, `$( … )`, `… & wait $!` ; l'ancien bash dans la
même image : « Not owner », 126. `posixtest` exec 15/0, fork 15/0 (1 sauté), proc 14/0, signal 18/0, misc 24/0,
io 9/0, comme avant. **Pi** (QEMU raspi4b, emplacement 3) : `SD0:/sys/svc/hwtest` par le nouveau bash → lancé par
init2, code 0 ; l'ancien → « Not owner ».

Vu en passant (pas de cette correction : l'ancien bash aussi) : sur le PC, le fils d'un `bash -c 'prog &'` dont
le père finit tout de suite meurt parfois d'une faute de page avant son exec (`cr2` 0 ou 0x40) : course entre la
fin du père et le fils copié sur écriture, à chercher dans le noyau du PC.

### Casque USB : l'isochrone d'usbd en C (PC)

* `src/svc/usbd/iso.c` : la partie xHCI d'`iso.inc` (le DWC2 du Pi reste à `usbd.asm`) : `USB_OP_ISO_OPEN` /
  `ISO_CLOSE`, anneau partagé `UI_*` avec le pilote, paquet OUT de (octets par ms 16.16) arrondi à la trame, 12
  TRB Isoch « au plus tôt » (SIA) en file par tuyau, remis à chaque tour de la boucle ; les fins sont suivies par
  le **pointeur du TRB** (une fin qui en saute, Missed Service, compte les précédentes) ; sous- et
  sur-alimentation de l'anneau ignorées ; IN : les octets reçus vers l'anneau. Fin du pilote ou retrait : fermé
  (`UI_STATE` = 1 au retrait).
* `src/svc/usbd/xhci.c` : `xh_iso_open` (Configure Endpoint Isoch OUT 1 / IN 5, intervalle 1 ms, CErr 0),
  `xh_iso_push`, `xh_iso_bell`, `xh_iso_close` (Stop Endpoint puis Drop : un autre réglage de l'interface peut
  être configuré ensuite), cases de 1 Ko à `Z_ISO` (0x140000) de la zone DMA ; les événements d'un tuyau
  isochrone vont à `usbd_iso_event`.
* usbaudio ne change pas (il attendait déjà l'isochrone d'usbd).

Vérifié sous QEMU (q35, `-device qemu-xhci -device usb-audio,audiodev=snd0 -audiodev wav`) : « usbd : isochrone 0
: périphérique 1, extrémité 01 OUT, 192 o max., 192,00 o/ms », la sortie « USB : QEMU USB Audio » 48 kHz devient
principale ; `sontest` dans le wav : la gamme 261,6 → 523,3 Hz puis **la 440,0 Hz**, 4 410 ms de son, crête 9 411
(comme le `usbaudio` du Pi sous QEMU), gauche = droite, **aucun trou**. Retrait (`device_del`) : « isochrone 0
fermé (23 097 paquets, 2 880 octets manquants) », usbaudio s'arrête ; rebranchement : nouveau tuyau, `sontest`
de nouveau entier dans le wav. (La « cadence » que dit sontest vient de l'horloge de l'invité sous TCG.)

Pas vérifié : **le micro (IN)** (QEMU n'a pas de micro USB), un casque **haute vitesse** (un paquet par ms : un
UAC1 haute vitesse avec bInterval 1 attendrait un paquet par micro-trame), le vrai ZBook. Le Pi garde `usbd.asm`
(son iso.inc) : usbd en C n'est toujours pas sur sa carte.

**Sur le ZBook** : construire (`wsl sh tools/svc/build_c.sh usbd`, `tools/pc/build.sh`, `sysroot.sh`), installer,
brancher le casque ; `soundctl` doit montrer « USB : <nom du casque> » principale ; `sontest` dans le casque.
Rapporter du journal les lignes « usbaudio … » et « usbd : isochrone … » ; au retrait, « … fermé (N paquets, M
octets manquants) » (un M qui grandit sans cesse : l'anneau se vide).

### GameCube : RVZ et WIA (Pi et PC)

* `src/apps/gcemu/rvz.cpp`, `rvz.h` : **écrits d'après la description publique du format** (docs/WiaAndRvz.md de
  Dolphin), sans son code : en-têtes, zones brutes (ramenées au multiple de 0x8000), groupes (RVZ : bit 31
  compressé, taille 0 = zéros, empaquetage), données de remplissage regénérées par le générateur de Fibonacci
  retardé des disques (graine de 17 mots, avancée de position % 0x8000) ; **Zstandard** (le défaut de Dolphin) et
  sans compression ; bzip2, LZMA, LZMA2 et la « purge » de WIA sont refusés avec un message. Cache de 4 groupes ;
  rien n'est décompressé en entier. GameCube seulement (pas de Wii).
* `src/apps/gcemu/main.cpp` : un fichier qui commence par `RVZ\1` / `WIA\1` passe par rvz.cpp (taille : celle de
  l'ISO d'origine) ; filtre `*.iso;*.gcm;*.rvz;*.wia;*.dol` ; essai `gcemu --rvzcheck image.rvz image.iso`.
  (`app.txt`, `opens` : pas touché, un autre agent y travaillait ; Game Library : l'autre agent.)
* `tools/zstd/build.sh` : zstd 1.5.7 (BSD), archive officielle vérifiée par sa somme, décompression seulement, sans
  asm → `build/zstd/{aarch64,x86_64}` ; appelé par `tools/emu/build_gcemu.sh` (Pi) et `tools/pc/apps.sh` (PC)
  s'il manque.
* `tools/emu/rvzgen.py` : `iso` (une image GameCube d'essai à nous : en-tête, **apploader minimal** qui demande
  une lecture puis rend une boucle, zones de données au hasard, de zéros et de remplissage), `rvz` (encodeur
  RVZ / WIA, zstd ou rien, groupes de 32 Kio à 2 Mio), `check` (décodeur Python à part).
* gcemu 1.0.3.

Vérifié : le décodeur Python et rvz.cpp (WSL, zstd de l'hôte) relisent **à l'octet** l'ISO de 40 Mio pour RVZ zstd
(groupes de 32 Kio, 128 Kio, 2 Mio niveau 19), RVZ sans compression (256 Kio), WIA zstd et WIA sans compression
(3 304 lectures de tailles diverses, à la suite puis au hasard : 0 écart). **Dans Korin** : PC (QEMU q35)
`gcemu --rvzcheck` a.rvz et d.wia : 0 écart, une ISO refusée ; `gcemu b.rvz` au bureau : le disque démarre par
son apploader (fenêtre « GameCube - b - 64 fps »). **Pi** (QEMU raspi4b, emplacement 3) : `--rvzcheck` a.rvz et
d.wia : 0 écart (438 s et 341 s sous TCG).

Pas vérifié : un **vrai RVZ fait par Dolphin** (DolphinTool n'est pas installé ; le paquet `dolphin-emu` de WSL
n'a pas été installé sans accord) : l'encodeur d'essai et le décodeur suivent la même lecture de la description,
un écart d'interprétation (avancée du générateur, zones brutes) ne se verrait qu'avec un vrai fichier ; un vrai
jeu ; la vitesse sur le vrai matériel.

**Sur le ZBook ou le Pi** : ouvrir un `.rvz` dans gcemu (Ctrl+O, ou `gcemu SD1:/Roms/GameCube/<jeu>/<jeu>.rvz`).
S'il ne démarre pas, le journal dit pourquoi (« Only uncompressed or Zstandard… » : reconvertir en RVZ zstd dans
Dolphin). Pour trancher un doute sur le décodeur : sous Windows, `DolphinTool convert -f iso` du même `.rvz`,
copier l'ISO à côté (pas sur SD1 du Pi), puis `gcemu --rvzcheck jeu.rvz jeu.iso` (« 0 ecarts » attendu).

## Retours du ZBook : bureau (13/11)

Retours de l'utilisateur sur le vrai ZBook, corrigés **d'un seul source pour le Pi et le PC**.

### Ce qui change

| Retour | Cause | Correction |
|---|---|---|
| Un clic sur le haut-parleur de la barre n'ouvre pas le volet du son | `SD0:/sys/soundmenu` n'existait qu'en asm : absent du PC (menubar en C le lance, rien ne venait) ; `wifimenu` de même | `src/sys/soundmenu/soundmenu.c`, `src/sys/wifimenu/wifimenu.c` (+ `sys.conf`) : traduits ligne à ligne de `soundmenu.asm`, `wifimenu.asm`, `soundlist.inc` et `wifilist.inc` (korinrt, `sounddraw.c` et `wifidraw.c` de `src/sys/common`, thread des travaux du Wi-Fi par `SYS_THREAD_CREATE`) ; construits par `tools/sys/build_c.sh`, mis dans l'arbre du PC par `sysroot.sh` (tout `build/sys/x86_64`) et sur la carte du Pi par `$sys_c` de `build.ps1` |
| Réglages, volet Son : un curseur pour le volume | quatre choix 25 / 50 / 75 / 100 % | UIKit avait déjà `ui_slider` (aucun ajout à la table) ; le cadre de Réglages a une ligne `R_SLIDER` (le widget placé sur sa ligne, caché quand elle défile hors du volet, valeur et unité à droite) ; le volet Son passe en une colonne, le volume change **aussitôt** (`ak_out_volume`) et le curseur suit les changements faits ailleurs (`SS_GEN`, `ap.tick`) ; « Aucune sortie. » sans principale. Seul endroit de Réglages où se règle le volume |
| Le volet .NET doit venir avec le paquet .NET | toujours dans la liste | `Applet.needs` (`control.h`) : un fichier sans lequel le volet n'est pas montré ; le volet .NET demande `SD0:/dotnet/dllpreload`, du paquet `dotnet` (`files = dotnet/`) : sans le paquet, pas de volet (`control dotnet` ouvre alors le premier). Voie choisie : la plus simple et robuste, sans module chargé ; noté dans `docs/PAQUETS.md` |
| Les émulateurs dans la catégorie Jeux du lanceur | `category = Jeux` dans leurs `app.txt` | comme Onyx (`category = Emulators`, que le lanceur, le dock et le menubar d'Onyx sautent) : `category = Émulateurs` pour nesemu, gbemu, gbaemu, snesemu, n64emu, gcemu, n3dsemu, ndsemu ; `cat_hidden` (`src/sys/common/applist.c`) : le lanceur du panel (onglets, Toutes, Récentes, recherche) et le menu Korin de menubar (table `cat_map` des lignes) ne la montrent pas ; les apps ouvertes restent dans le panel. Game Library seule dans Jeux ; les émulateurs restent lançables par elle et par leurs fichiers (`opens`). Dans Logiciels, leurs paquets passent dans la catégorie « Émulateurs » |
| Game Library : chaque jeu en double | sur le PC, `SD1:` est le dossier `SD0:/home` (`mounts.txt` : `SD1 = blk.korin, fatfs, /home`) : `SD0:/home/roms` et `SD1:/roms` sont le même dossier, tous deux balayés | un fichier déjà vu par un autre chemin (même nom, même console, même taille, même date : `stat`) n'est pas ajouté ; vaut aussi pour un dossier de `folders.txt` qui en contient un autre. `.rvz` ajouté aux extensions GameCube |

`tools/pkg/versions.ini` : armpi 2026.11.35 (soundmenu, wifimenu, menubar, panel, control), gamelib 1.0.8, nesemu,
gbemu, gbaemu, snesemu, n64emu 1.0.5, ndsemu 1.0.2, n3dsemu 1.0.2, gcemu 1.0.3 (`app.txt`), korin-srcs 1.0.15 (rien
de publié). `tools/sys/compare.sh` : `BOOT_EXTRA="commande"` (ajoutée à `boot.sh`).

Tailles (asm → C Pi / C PC, octets) : soundmenu → 8 032 / 7 112 ; wifimenu → 10 184 / 9 456.

### Vérifié

**PC (QEMU q35 + OVMF, 4 cœurs, NVMe, `mkkorindisk.py --layout zbook --korin-mb 400`, `intel-hda` + `hda-duplex`,
tablette USB, arbre de `sysroot.sh` + `sysroot_apps.sh` avec un `home/Roms/<console>/<jeu>/<fichier>` de ROM
d'essai générées (et des `.sav`), captures par `pmemsave`, 13/11)** :
* clic sur le haut-parleur de menubar → **le volet du son** (« Analogique (1af4:0022) », 50 %, Muet, Mixer…) ; le
  curseur glissé → 67 % ; Échap le ferme ;
* Réglages, volet Son : **le curseur** à 50 % ; glissé → 81 %, et `soundctl` dans le Terminal dit « volume 81,
  principale » ; `soundctl volume 2 30` → le curseur de Réglages passe à 30 % ; **pas de volet .NET** dans la liste
  (pas de paquet dotnet sur le PC) ;
* `SD0:/sys/wifimenu 1200` (le PC n'a pas de Wi-Fi : menubar montre l'adresse au clic sur l'icône du réseau, comme
  sur un Pi sans Wi-Fi) : le menu s'ouvre, « Wi-Fi — pas de Wi-Fi », « Aucun réseau visible », « Paramètres
  Wi-Fi... » ;
* lanceur : plus d'onglet « Émulateurs », **Jeux** sans émulateur (2048 … Game Library … Tetris) ; menu Korin sans
  « Émulateurs », Internet → IRC, Mail, Telegram (la table des lignes suit) ;
* Game Library : **9 jeux pour 9 ROM** (« Tous les jeux 9 », Game Boy 2, GameCube 2 dont le `.rvz`), plus de
  doublon ; les `.sav` ne comptent pas.

**Pi (QEMU raspi4b, emplacement 2, `tools/sys/compare.sh`, cartes `SYS_C="soundmenu wifimenu"` tirées de
`build/sdroot`, 13/11)** :
* **soundmenu** ouvert par un clic sur le haut-parleur de la barre (`usb-audio` de QEMU : « USB : QEMU USB
  Audio », 100 %), puis un clic sur le curseur (30 %), Muet, Échap : la fenêtre du C est **identique au pixel** à
  celle de l'asm (0 pixel différent une fois décalée d'un pixel : l'icône bouge avec la largeur de l'heure ; reste
  le curseur de la souris au même décalage) ; le fond (voronoy) est tiré au hasard ;
* **wifimenu** : pas de Wi-Fi sous QEMU, donc menubar ne le lance pas ; lancé du Terminal ou de `boot.sh`, l'asm
  comme le C se ferment aussitôt (« terminé (code 0) » : la fenêtre ne prend pas le clavier, `WEV_FOCUS` 0, comme
  le dit l'en-tête) : même comportement, mais pas de capture à comparer. Son dessin n'est vu que sur le PC (où
  l'Elegant en C lui donne le clavier) ;
* Réglages, Game Library, panel et menubar : construits pour le Pi sans avertissement ; essayés sous QEMU du PC
  seulement (même source).

### Pas vérifié, reste

* Le **vrai ZBook** et le **vrai Pi** ; wifimenu avec une vraie puce Wi-Fi (liste, clé, connexion) en C.
* L'accueil du **mode poche** (`sys/home`, asm, `launcher.inc`) montre encore la catégorie Émulateurs (pas porté en C).
* Le doublon de Game Library n'a pas été reproduit avec l'ancienne version (la cause se lit dans `mounts.txt` du
  PC) ; les ROM de l'utilisateur ne sont pas essayées ici (seulement leur structure, lue sur `D:\home\Roms`).
* Sur le PC, un clic sur l'icône du réseau montre l'adresse (pas de Wi-Fi) ; wifimenu n'y est lancé que si une
  interface Wi-Fi est prête, comme sur le Pi.
* `build/sdroot` n'a pas été refait (`build.ps1`) : les cartes ont encore l'asm de menubar et panel.

## GPU Intel, étape 3 : GlKit et GpuKit (13/11)

L'étape 2 est validée sur le ZBook (13/11) : `intelgpu start` puis `gltest --win` disent « 0 écart(s) : PASS »
et `D:\gltest.ppm` est le bon triangle. C'était bien **le GPU** : `gltest` ne vérifie les pixels et n'écrit
`SD0:/gltest.ppm` que si `armgl_init` a rendu `ARMGL_GPU` hors simu (sans GPU, il dit « repli : pas de GPU ») ;
le journal de cette séance n'a pas été écrit (`D:\journal.txt` est celui de la sonde, 08:57, rien d'inquiétant :
« RÉUSSI », tout remis). Deux défauts vus par l'utilisateur, corrigés ici : `intelgpu` muet au Terminal, et
`gltest --win` qui n'ouvrait en fait aucune fenêtre (l'option était lue puis oubliée).

### Ce qui change

| Fichier | Rôle |
|---|---|
| `tools/mesa/build-glkit-pc.sh` | **GlKit du PC** : Mesa (iris et softpipe) et armgl du PC liés en objet partagé, mis en kit (même table `src/kits/glkit/glkit.abi`, même chargeur `glkit.c`) → `build/pc/mesa/sdroot/lib/glkit.so` (27 Mo, `SD0:/lib` par `sysroot.sh`, **pas** dans l'initrd) ; `build/pc/mesa/lib/libglkit.a` (relais, chargeur, 197 imports de la libc du programme) ; `gltest` lié à GlKit (490 Ko au lieu de 28 Mo). Appelé à la fin de `build-iris.sh` |
| `tools/mesa/glkit/glkit.c` | (`#ifdef __x86_64__`) les relocations de x86-64 : `RELATIVE`, `64`, `GLOB_DAT`, `JUMP_SLOT`, `TPOFF64`, `IRELATIVE`, et le modèle TLS « general dynamic » de `gcc -fPIC` (`DTPMOD64`, `DTPOFF64`, un `__tls_get_addr` caché dans le kit : module 1 = la zone du kit dans le bloc TLS du programme, 2 = une variable de la libc du programme) |
| `glkit_x86_64.S`, `glkit_x86_64.ld`, `host/glkit_lazy_x86_64.S` | la table du kit et le résolveur TLSDESC ; le script : **fin des relocations hors de la section** (ld de x86-64 ajoute les siennes après les sections d'entrée : il en manquait une) et **constructeurs C++ en `.ctors`** gardés (la chaîne `x86_64-onyx-elf` les met là ; `--gc-sections` les jetait et les tableaux statiques de GLSL restaient à zéro : faute dans `strcmp` à la compilation des shaders) ; le premier appel d'une fonction GL (r11 = rang du relais) |
| `tools/mesa/glkit/finish.py` | relais et relocations de x86-64 selon `e_machine` ; la sortie du Pi refaite par le nouveau script est identique à l'octet |
| `tools/mesa/libdrm-pc/korin_i915.c` | **`armpi_drm_window`** : un tampon partagé de deux images, morceau du service (`BUF_GRANT`, `IG_OP_CHUNK`), deux BO ; présenté par `WS_OP_SURFACE_ATTACH` / `DAMAGE` (comme le Pi), après un `clflush` de l'image (iris écrit une image importée sans le LLC) ; `armpi_drm_flush` vide de même (`armgl_buffers`) ; **`korin_sw_winsys`** : le `sw_winsys` de softpipe (images d'une fenêtre et d'`armgl_buffers` rendues sur place) ; `korin_i915_describe` (ce qui rend, en clair) |
| `tools/mesa/armgl/armgl.c`, `armgl.h` | (`ARMGL_IRIS`) sans service `intelgpu` : **softpipe**, `armgl_init` rend `ARMGL_SOFT` (3) ; `ARMGL_F_NOSOFT` (8) ou `KORIN_GL=nosoft` : `ARMGL_NONE` comme avant |
| `tools/mesa/configure-iris.sh`, `build-iris.sh` | `-Dgallium-drivers=iris,softpipe` (un dossier déjà configuré est complété par `meson configure`) |
| `tools/mesa/armgl/gltest.c` | (`KORIN_PC`) « RENDU PAR le GPU Intel 9a60 … / le processeur (softpipe) / le faux GPU (simu) » au début et à la fin ; pixels vérifiés aussi sous softpipe ; `--win` : une fenêtre où GlKit rend directement (`armgl_window`), le triangle qui pulse, images/s |
| `src/kits/gpukit/gpukit.c`, `kit.conf` | **GpuKit du PC** : les 13 fonctions de `gpukit.abi` en C, x86-64 seulement (`arch = x86_64` : le Pi garde son asm) ; `gk_draw` au processeur par le rastériseur du BASIC 3D d'Onyx (`bas3d.h` en C) ; `gk_alloc` par `VM_MAP` |
| `src/kits/gpukit/gl/gkgl.c`, `gpukit_gl.h` | **GpuKit par le GPU** : un kit ne peut pas porter Mesa (il lui faut la libc du programme), donc le programme C/C++ appelle `gk_gl_attach()` et GpuKit reçoit son rendu GL (`gk_debug (GK_DBG_BACKEND, table)`) : shaders texel × couleur + couleur 2, test d'alpha, Z, faces, les 13 mélanges, filtres et répétitions, matrice par lot, surface hors écran relue dans la cible ; `gk_texture`/`gk_texture_rect` en textures GL. Sans GPU Intel : rien n'est attaché (le processeur) ; `KORIN_GPUKIT=soft` l'attache sur softpipe (essais), `=cpu` jamais |
| `tools/kits/build_c.sh` | clé `arch` de `kit.conf` |
| `src/kits/gpukit/gpukit.inc` | `GK_DBG_BACKEND = 4` (une constante ; `gpukit.asm` du Pi inchangé) |
| `src/apps/cosmozoom/main.cpp`, `src/apps/n64emu/main.cpp` | (`__x86_64__`) `gk_gl_attach()` ; n64emu passe ses sommets et lots (déjà au format de GpuKit) à `gk_draw`, rendus à la taille de la sortie, et dit « n64emu : rendu par … » ; sans GPU : son rendu logiciel |
| `tools/pc/apps.sh` | cosmozoom et n64emu liés à `gkgl.o` et `libglkit.a`, gcemu à `libglkit.a` (le GX par GlKit : GPU Intel, sinon softpipe) ; **`tools/pc/armgl_none.c` retiré** |
| `src/svc/intelgpu/intelgpu.c`, `tools/pc/sysroot.sh` | **`SD0:/bin/intelgpu`** (le même programme) : bash lance tout programme de `SD0:/sys` par init2, dont la sortie va au journal seulement — d'où le silence ; lancé de `bin` (sans droits), `intelgpu start / status / stop` parlent au Terminal, `test` et `probe` lancent `SD0:/sys/svc/intelgpu` et recopient ses lignes du journal ; `start` finit par « PRÊT : GPU Intel 8086:9a60 rév. 1 (Tiger Lake GT1, UHD Graphics), 32 EU … » ; au bureau, les lignes vont aussi au pseudo-terminal (pas seulement à un tube) ; `serve` déjà servi : `E_EXIST` (hwmanager ne relance pas) |
| `skel/sd0/etc/drivers.txt` | **`pci:8086:9a60 = SD0:/sys/svc/intelgpu serve`** : hwmanager lance le service au démarrage (le journal de la sonde n'a rien d'inquiétant et `gltest` est validé ; `#` devant la ligne pour revenir au lancement à la main) |
| `tools/pc/qemu_pc.py` | `&` dans les lignes tapées (AZERTY) |

### Vérifié sous QEMU (q35 + OVMF, 4 cœurs, TCG, NVMe, `mkkorindisk.py --layout zbook --korin-mb 400`, bureau, 13/11)

* **GlKit chargé** : « init2 : kit_load … glkit -> 0 » ; `armgl_init` en 0,8 à 1,4 s la première fois (27 Mo lus).
* **`gltest --win` sans service (softpipe)** : « armgl_init 3 (processeur, softpipe) », « RENDU PAR le processeur
  (softpipe de Mesa …) », « Mesa / softpipe / OpenGL ES 3.1 », shaders compilés, les 8 pixels **PASS**, « 0 ecart(s) :
  PASS » ; la fenêtre « gltest : GlKit, processeur (softpipe) » montre le triangle rendu directement dans la surface
  d'Elegant, **22 images/s** sous TCG.
* **`intelgpu start simu`, `gltest`, `intelgpu stop`** : iris par le service en simu (« armgl_init 1 (GPU) », « RENDU
  PAR le faux GPU du service intelgpu », « Mesa Intel(R) UHD Graphics (TGL GT1) / OpenGL ES 3.2 », 5 lots soumis et
  finis : PASS) ; service fini sans anomalie.
* **`intelgpu` au Terminal** (`SD0:/bin/intelgpu`) : `start simu` → l'état du service puis « PRÊT, en SIMU : un faux
  GPU en mémoire (8086:9a60, 32 EU imités) », « service intelgpu : pid 25 ; pour l'arrêter : intelgpu stop » ;
  `test simu` : les lignes de l'essai au Terminal ; `status` ; `stop`.
* **Cosmo Zoom** : `KORIN_GPUKIT=soft cosmozoom demo` (GpuKit → `gkgl.c` → GlKit → softpipe) : l'écran titre et la
  course en 3D à pleine taille (textures, ciel, ombres, transparences) ; sans variable, sans GPU : GpuKit au
  processeur (`gk_draw` en C, à moitié taille), même image.
* **gcemu** lié à GlKit (`gc-stress`) : fenêtre et menus, GlKit chargé (softpipe), pas de faute (image noire comme
  avec `--nogl` : la ROM d'essai n'affiche rien). **n64emu** (`n64-stress`) : GpuKit et GlKit chargés, sans GPU
  le rendu logiciel, pas de faute. Aucun jeu commercial n'est essayé (aucune ROM ici).
* Le Pi ne change pas : `armgl.o`, `gltest.o`, `glkit.o`, `glkit_host.o` recompilés pour le Pi avant et après
  (désassemblage et données identiques), `glkit.so` du Pi refait par le nouveau `finish.py` identique à
  `prebuilt/glkit.so` ; aucun `gpukit.so` d'AArch64 n'est construit ; `tools/check_constants.py` passe.

**Trouvé en passant, sur le Pi (pas corrigé : le Pi ne devait pas changer)** : `glkit.ld` du Pi met
`__rela_dyn_end` dans la section, et ld y ajoute ses relocations après : **4 relocations de `.rela.dyn` (0x60
octets) ne sont jamais faites** par `glkit.c` sur le Pi. GlKit y marche, mais c'est un risque ; la correction est
celle de `glkit_x86_64.ld` (le symbole après la section), avec la carte à refaire.

### Marche à suivre sur le ZBook (par l'utilisateur)

1. Construire et installer (PowerShell **administrateur**, `C:\Projects\ARMPI`) :
   `wsl sh tools/pc/build.sh`, `wsl sh tools/mesa/build-iris.sh` (Mesa complété par softpipe la première fois :
   quelques minutes ; puis GlKit), `wsl sh tools/pc/apps.sh cosmozoom n64emu gcemu`,
   `wsl sh tools/pc/sysroot.sh`, `wsl sh tools/pc/sysroot_apps.sh`, puis `… korin.ps1 install -DryRun`, puis
   `… korin.ps1 install` (`drivers.txt` et `SD0:/lib/glkit.so` compris).
2. Démarrer Korin (bureau). **Le service démarre seul** (hwmanager, `drivers.txt`). Dans le Terminal :
   `intelgpu start` → « le service tourne déjà » puis **« PRÊT : GPU Intel 8086:9a60 rév. 1 … 32 EU »** ;
   `intelgpu status` (lots, interruptions, blocages 0).
   (Si `D:\etc\init.txt` a gardé une ligne `intelgpu start` : l'enlever, init lit celui de l'initrd et l'ignore.)
3. `gltest --win` : « RENDU PAR le GPU Intel 9a60 rév. 1, 32 EU (iris, service intelgpu) », 8 PASS, « 0 ecart(s) :
   PASS », puis une **fenêtre** où le GPU rend directement un triangle qui pulse ; la ligne « … images/s ». Fermer.
4. **Cosmo Zoom** : le lanceur (catégorie Jeux, « Cosmo Zoom! »), ou au Terminal `SD0:/apps/cosmozoom.app/main`.
   Il prend le GPU tout seul (`gk_gl_attach`) : rendu à pleine taille (au processeur, il rend à moitié taille).
   Plein écran ; flèches pour tourner, Haut ou X accélère, Entrée valide, Échap pour le menu de pause ; F12
   montre les images/s. `SD0:/apps/cosmozoom.app/main demo` : la course de démonstration.
5. n64emu, gcemu : ouvrir un jeu comme d'habitude ; « n64emu : rendu par le GPU Intel … » au Terminal s'il est
   lancé de là.
6. `journal`, puis rapporter `D:\journal.txt` (lignes « intelgpu », « korin_i915 », « glkit »).

**Signes de danger** : ceux de l'étape 2 (écran noir ou figé, « DANGER », « ANOMALIE : GDRST », tempête
d'interruptions). Un « BLOCAGE » suivi de « remise à zéro finie » n'en est pas un : l'app passe au processeur
(GpuKit) ou s'arrête proprement. Pour revenir au lancement à la main : `#` devant la ligne `pci:8086:9a60` de
`D:\etc\drivers.txt`.

### L'écran : présenté par le processeur

Elegant compose toujours dans le framebuffer du GOP par le processeur : le GPU rend dans des tampons du système
(surfaces des fenêtres), Elegant les lit (cohérence : `clflush` avant de montrer). **Pour présenter par le GPU**
(GPU-INTEL.md § 10) : le copieur BCS0 (contexte du service, `XY_FAST_COPY_BLT`) de l'image d'une fenêtre vers le
framebuffer, par les entrées de la GGTT que le GOP a déjà mises (`PLANE_SURF` à 0) — sans écrire un registre de
l'affichage ; il faudra un `IG_OP_BLIT` (source : morceau du client, destination : adresse GGTT du framebuffer,
rectangle), qu'Elegant appelle pour les surfaces au lieu de les recopier, MOCS non caché vers l'affichage, et
Elegant composé par le GPU ensuite. Puis RC6 et la fréquence (RPS) au lieu du forcewake tenu.

### Pas fait, ensuite

* Rien de l'étape 3 n'est essayé sur le vrai GPU (QEMU n'en a pas) : la fenêtre par le GPU (`armpi_drm_window`,
  `clflush`), `gkgl.c` sur iris, Cosmo Zoom et les émulateurs sur le GPU.
* **n3dsemu** n'est pas encore construit pour le PC (pas dans `tools/pc/apps.sh`) ; il a déjà le code GL du Pi.
* **QBasic 3D, Turtle, Viewport3D** passent par GpuKit : au processeur sur le PC (`gk_draw` en C) ; pour le GPU,
  le runtime BASIC (C++) peut appeler `gk_gl_attach()` comme Cosmo Zoom (à faire), .NET quand il sera sur le PC.
  Un programme en assembleur ne peut pas attacher le GPU (il faut la libc : GlKit).
* n64emu du PC n'a pas le combineur N64 en GLSL du Pi (`gl/n64gl.c`, fait pour l'asm) : ses lots passent par
  GpuKit (texel × couleur), comme son rendu logiciel.
* `gk_texture_info` (texture partagée avec Mesa par adresse physique) : `GK_E_NOGPU` sur le PC.
* gcemu sans GPU Intel prend softpipe (lent) ; `--nogl` garde son rendu `soft.cpp`.

## Wi-Fi AX201, étape 1 (13/11)

Le Wi-Fi Intel AX201 du ZBook (`0000:00:14.3`, `8086:43f0`, CNVi de Tiger Lake-H, MAC « QuZ », module RF « Hr2 ») :
étude complète dans **[WIFI-PC.md](WIFI-PC.md)** (famille 22000, transport « gen2 », micrologiciel, mode MVM, ce que
le SoftMAC laisse à l'hôte, les 7 étapes du chantier, les pannes probables). Cette étape : le pilote **`iwl`**
(`src/svc/iwl`, C, PC seulement) **lancé à la main** (pas dans `drivers.txt`) met le micrologiciel en marche, lit le
NVM et fait **un balayage** ; tout au journal du noyau et au Terminal ; en cas d'erreur, il journalise les registres et
les tables d'erreur du micrologiciel puis arrête la puce proprement.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/iwl/iwl.c` | commande (`fwinfo`, `probe`, `info`, `scan`), journal (« iwl : … », journal du noyau et Terminal ; lancé de `SD0:/bin/iwl` sans droits, il lance `SD0:/sys/svc/iwl` et recopie ses lignes, comme `intelgpu`), fonction PCIe par pcid (D0, `RETRY_TIMEOUT`, BAR0), **MSI** à un vecteur (MSI-X coupé ; `-p` : sondage), fichier du micrologiciel, arrêt |
| `src/svc/iwl/trans.c` | transport gen2 : CSR, accès aux registres internes et à la SRAM, `start_hw` (NIC_READY, persistance, remise à zéro, porte d'alimentation du CNVi, APM, `UREG_CHICK`), `start_fw` (context info, LTR de démarrage, `UREG_CPU_INIT_RUN`), anneau de réception (512 RBD, 511 RB de 4 Ko, rempli à l'interruption ALIVE), file de commandes (32 TFD), interruptions ou sondage, journal des registres et des erreurs, arrêt (poignée de main, DMA coupé, remise à zéro) |
| `src/svc/iwl/mvm.c` | vivant (v6), sonnette PNVM, `INIT_EXTENDED_CFG`, `NVM_ACCESS_COMPLETE`, `INIT_COMPLETE`, `NVM_GET_INFO`, MAC des CSR, antennes, `BT_CONFIG`, `SOC_CONFIGURATION`, `MCC_UPDATE` (« ZZ »), `SCAN_CFG` v5, `SCAN_REQ_UMAC` v15, trames reçues, fin du balayage, la liste |
| `src/svc/iwl/iwlfw.h`, `fw.c`, `cmds.c`, `ieee.c`, `crypto.c` | sans matériel, compilés aussi sur l'hôte : fichier TLV (sections, capacités, versions), nom du fichier d'après `HW_REV`/`RF_ID`, context info, commandes et réponses à l'octet, balises et éléments RSN/WPA, SHA-1, HMAC, PBKDF2, PRF, PTK, MIC EAPOL, AES-128 et son déballage |
| `firmware/pc/iwlwifi/` | `iwlwifi-QuZ-a0-hr-b0-77.ucode` (77.3cda2a2c.0, linux-firmware), `LICENCE.iwlwifi_firmware`, `SOURCE.txt` ; `docs/LICENCES.md` |
| `tools/pc/iwlfw.sh` | les variantes Qu-b0 / Qu-c0 depuis linux-firmware, sommes sha256 fixées (pas dans le dépôt) |
| `tools/pc/sysroot.sh` | `iwl` aussi dans `SD0:/bin`, le dossier du micrologiciel dans `SD0:/lib/firmware` |
| `tools/pc/iwltest/` | `run.sh` (WSL) : `iwltest.c` contre le vrai fichier et les vecteurs, `check.py` : contre-épreuve indépendante |

Décisions : micrologiciel **gardé dans le dépôt** avec sa licence (comme celui du Pi : construction sans réseau, et le
fichier essayé est celui contre lequel les commandes sont vérifiées) ; **API 77 seulement** (`iwl` vérifie les
versions du TLV 48 et refuse d'envoyer une structure qu'il ne connaît pas) ; **pas de contexte MAC** pour le
balayage (le micrologiciel n'en demande plus) ; régulation « ZZ » avant tout balayage (LAR) ; MSI à un vecteur par les
causes de `CSR_INT`, le MSI-X à plusieurs vecteurs avec le trafic de données (étape 5).

### Vérifié (hors matériel, 13/11)

* **`sh tools/pc/iwltest/run.sh`** (WSL) : **142 vérifications, 0 échec** : le vrai fichier (51 sections : LMAC 14 dont
  le CSS à `0x433000`, UMAC 15 à `0x80433000`, pages 20 ; 1 388 928 octets ; image unifiée ; PHY_SKU `0x330018` ;
  versions : balayage 15, config. 5, vivant 6, NVM 4, MCC 1/5, SoC 2, BT 6 ; capacités LAR, UMAC, latence du SoC,
  poignée de main ; fichier tronqué ou corrompu refusé) ; nom du fichier pour `0x354`/`0x10a100` (QuZ), `0x332` (Qu C0),
  `0x331` + Hr1 (Qu B0), Gf refusé ; réponses (vivant v3 et v6, NVM v4, MCC v4, longueur fausse refusée, MAC des CSR,
  fin de balayage) ; un RB à deux paquets (vivant, balise) ; balises : WPA2-PSK, transition WPA2/WPA3 (MFPC), WPA3
  (SAE, MFPR, réponse de sonde), WPA (TKIP), WEP, ouvert, caché, entreprise, trame de données ignorée, élément coupé,
  RSN version 2 refusée ; **SHA-1** (FIPS), **HMAC-SHA1** (RFC 2202 n° 1 et 6), **PBKDF2** (RFC 6070, et les deux PSK
  de 802.11-2020 J.4), **PRF-512** (J.3 n° 1), **AES-128** (FIPS 197 C.1), **déballage** (RFC 3394 4.1, intégrité).
* **`check.py`** : la commande `SCAN_REQ_UMAC` v15 (1 940 octets), reconstruite champ par champ dans l'ordre des
  structures publiques, est **identique octet pour octet** ; la **PTK** (KCK, KEK, TK) et le **MIC EAPOL** sont
  identiques à `hashlib`/`hmac` de Python ; le **context info** (1 792 octets) aussi (le piège : UMAC à +192 avant
  le LMAC à +704).
* **QEMU q35** (OVMF, 4 cœurs, NVMe, `mkkorindisk.py --layout zbook --korin-mb 400`, console texte) : `iwl fwinfo`
  lit `SD0:/lib/firmware/iwlwifi-QuZ-a0-hr-b0-77.ucode` et le décrit ; `iwl probe`, `iwl scan` : « **pas d'AX201** :
  aucune fonction 8086:43f0 … parmi 0 fonction(s) réseau PCIe ; rien n'est touché » (code 0) ; avec un e1000 ajouté,
  il est nommé (« pas un Wi-Fi Intel 22000 »), puis `iwl info -p 00:05.0` le force : prise par pcid, BAR0 mappée,
  « l'horloge du MAC ne se stabilise pas », journal des registres, arrêt, fonction rendue, fin propre (code 1) ;
  `iwl` de `SD0:/bin` relaie bien les lignes du service. **Bureau** (arbre normal, `shell=desktop`) : démarre comme
  avant (Elegant, barre, panel), rien d'iwl au démarrage. `tools/check_constants.py` passe. Le Pi ne change pas
  (aucun fichier du Pi touché ; `iwl` n'est construit que pour x86-64).

### Pas vérifié

Tout ce qui touche la vraie puce : la mise en route (NIC_READY, porte d'alimentation, ALIVE), le context info lu par
la ROM, la réception, les commandes, le balayage, l'arrêt ; la MSI. Le Terminal du bureau (l'arbre d'essai n'a pas
d'apps : relais vérifié en console, même chemin que `intelgpu`).

### Marche à suivre sur le ZBook (par l'utilisateur)

1. Construire et installer (PowerShell **administrateur**, `C:\Projects\ARMPI`) : `wsl sh tools/pc/build.sh`,
   `wsl sh tools/pc/sysroot.sh` (et `sysroot_apps.sh` comme d'habitude), `… korin.ps1 install -DryRun`, puis
   `… korin.ps1 install`. Vérifier sur `D:` : `D:\lib\firmware\iwlwifi-QuZ-a0-hr-b0-77.ucode` (1 406 720 octets),
   `D:\bin\iwl`, `D:\sys\svc\iwl`.
2. Démarrer Korin. **Wi-Fi permis** (pas de mode avion dans le BIOS ni par la touche du clavier).
3. Au Terminal, dans l'ordre :
   * `iwl fwinfo` → « version 77.3cda2a2c.0 », « LMAC 14, UMAC 15, pages 20 », « balayage 15 … ».
   * `iwl probe` → « 0000:00:14.3 : 8086:43f0 rév. … » puis **« identité : HW_REV 0000035x (MAC type 035 : QuZ), RF_ID
     0010a… (RF type 10a …) … radio permise »** et « micrologiciel attendu : iwlwifi-QuZ-a0-hr-b0-77.ucode (dans
     l'arbre du PC) », puis « puce arrêtée (DMA coupé …) », « sonde finie (rien d'anormal) ».
   * `iwl info` → « micrologiciel donné à la puce … », **« VIVANT : statut cafe … »**, « initialisation du micrologiciel
     finie », les trois lignes « NVM : … » (bandes 2,4 et 5 GHz, normes n ac ax, antennes 3/3, **adresse MAC** : la
     même que sous Windows, `getmac` ou `ipconfig /all`), « régulation : pays … », « PRÊT : … », puis l'arrêt.
   * `iwl scan` → « balayage actif sur N canaux … », « balayage fini en … ms (statut 1, complet), N trames reçues »,
     **la liste des réseaux** (signal, canal, sécurité, BSSID, norme, SSID), du plus fort au plus faible : la box de la
     maison doit y être, en « WPA2 » ou « WPA2/WPA3 ». Aussi `iwl scan passif`, et `iwl scan ssid=NOM` pour un réseau
     caché.
   * En cas de problème, la même commande avec `-v` (et `-p` : sans MSI) ; `iwl scan -vv` montre chaque balise.
4. `journal`, puis rapporter `D:\journal.txt` (toutes les lignes « iwl : », et celles de pcid pour 00:14.3).

**Signes de problème** (WIFI-PC.md § 8) : « la puce ne se dit pas prête », « pas de « vivant » en 3 s » (rapporter les
lignes « registres » et « démarrage sécurisé »), « ERREUR du micrologiciel » (rapporter « erreur UMAC / LMAC »), « pas
de réponse à la commande », « ALIVE non vue en 1,5 s », « 0 réseau(x) vu(s) », « arrêt : le maître du bus ne
s'arrête pas ». Aucun n'est dangereux pour le reste du PC : la puce est remise à zéro, et pcid coupe son accès à la
mémoire (maître du bus) en reprenant la fonction. `iwl` ne touche qu'à la puce Wi-Fi (aucune écriture sur un disque).
Si le PC se fige pendant `iwl` : bouton tenu, puis rapporter le journal du démarrage suivant.

### Premier essai sur le ZBook (13/11) : registres internes illisibles, corrigé

Journal de l'utilisateur (`D:\iwlprobe.txt`, `D:\iwlinfo.txt`) : `iwl fwinfo` et `iwl probe` justes —
`8086:43f0 rév. 11`, sous-système `8086:0074`, **`HW_REV 00000351`** (QuZ, étape 1), **`RF_ID 0010a100`** (Hr2) : le
fichier `iwlwifi-QuZ-a0-hr-b0-77.ucode` est le bon. Mais **toutes les lectures des registres internes** (par
`HBUS_TARG_PRPH_*` : CNVI, OTP, démarrage sécurisé, PC de l'UMAC et du LMAC, RFH) rendent **`A5A5A5A2`** (valeur
d'erreur du bus de la puce : l'accès n'est pas accordé), déjà dans `probe` ; `iwl info` : « pas de vivant en 3 s »,
`CSR_INT` à 0, context info bien écrit, anneau rempli, rien reçu. L'écriture qui lance le micrologiciel
(`UREG_CPU_INIT_RUN`) n'a donc sans doute jamais atteint la puce.

Cause retenue (deux défauts du pilote, qui s'additionnent) :
* **attente trop courte après `MAC_ACCESS_REQ`** : `udelay` comptait 2 lectures de registre par microseconde,
  pensé pour un lien PCIe ; le CNVi est **dans le PCH**, une lecture y est bien plus courte, et l'horloge du MAC étant
  déjà prête (`INIT_DONE`), le sondage rendait aussitôt : la première lecture interne partait avant le réveil de
  l'accès ;
* **lecture-modification-écriture sur une valeur d'erreur** : la porte d'alimentation du CNVi (`HPM_HIPM_GEN_CFG`)
  était réécrite à partir de cette lecture, soit `A5A5A5A2 | FORCE_ACTIVE` puis `… | PG_EN | SLP_EN` : des bits au
  hasard (sommeil compris) dans un registre de gestion d'énergie, ce qui peut laisser la puce dans un état où plus
  aucun accès interne ne marche, **peut-être jusqu'à une vraie coupure** (domaine toujours alimenté).

Corrections (`trans.c`, `iwl.c`) :
* `udelay` **étalonné** au lancement (lectures par microseconde mesurées sur 4 ms, au journal : « attentes
  courtes : N lectures de registre par microseconde ») ; 10 µs de marge après chaque prise d'accès MAC ;
* **jamais d'écriture tirée d'une valeur d'erreur** (`A5A5A5Ax`, `5A5A5A5x`, `FFFFFFFF`) : `prph_rmw` refuse et le
  dit ; le bit de persistance n'est touché que sur une lecture valide ;
* **comme la sonde d'iwlwifi** avant toute remise à zéro (`trans_probe_access`) : puce prête, `INIT_DONE`, accès MAC,
  puis le **bus AUX du CNVi donné au MAC** (`WFPM_CTRL_REG` bit 31, ce que fait iwlwifi avant de lire `SD_REG_VER` et
  `CNVI_AUX_MISC_CHIP`) ;
* **un essai d'accès explicite** (`prph_check`) à chaque étape, au journal : « accès aux registres internes
  après INIT_DONE / après le bus AUX / après la remise à zéro / après l'APM / avant le lancement : **OK** | PARTIEL |
  **ÉCHEC** », avec `GP_CNTRL` pendant l'accès et quatre registres sûrs (`WFPM_CTRL`, `CNVI`, `HPM_DEBUG`, `HIPM`) ;
  `ÉCHEC` après la remise à zéro : arrêt **avant** toute écriture interne ; `ÉCHEC` avant le lancement : micrologiciel
  non lancé ;
* au lancement, `UREG_CPU_INIT_RUN` et la LTR **relus** (« lancement : LTR …, UREG_CPU_INIT_RUN relu … »).

Vérifié hors matériel : `iwltest` 142/142, contre-épreuve inchangée ; QEMU q35 : « pas d'AX201 », et forcé sur un
e1000, l'échelle des essais s'arrête à l'échec sans écriture interne, fin propre.

**Nouvelle marche à suivre** (après `build.sh`, `sysroot.sh`, `korin.ps1 install` comme plus haut) :
1. **Couper vraiment le ZBook** (arrêter, pas redémarrer ; attendre 10 s), au cas où le premier essai aurait laissé de
   mauvais bits dans la gestion d'énergie du CNVi ; démarrer Korin.
2. `iwl probe -v` : attendu « attentes courtes : N … » (N vers 5 à 20), « accès aux registres internes après
   INIT_DONE : **OK** », « bus AUX donné au MAC : … CNVI **non A5A5A5Ax** », puis « … après la remise à zéro : OK »,
   « … après l'APM : OK », l'identité avec `CNVI` et `OTP` lisibles.
3. Si c'est OK : `iwl info -v`, puis `iwl scan` (attendus : ceux de la marche à suivre plus haut).
4. Si `ÉCHEC` dès « après INIT_DONE » : démarrer **Windows une fois** (son pilote remet le CNVi en ordre), l'arrêter
   proprement, revenir à Korin et refaire `iwl probe -v`. Rapporter dans tous les cas les lignes « iwl : » du journal.

### Deuxième essai sur le ZBook (13/11, après extinction) : l'ordre des lectures

Journal (`D:\iwlprobe.txt`, `D:\iwlinfo.txt`) : « attentes courtes : **0** lectures de registre par microseconde »
(une lecture du CNVi y dure **plus d'une microseconde** : l'étalonnage par lectures ne valait rien) ; accès
« PARTIEL » : `WFPM_CTRL` se lit (`00000000`) mais `CNVI`, puis `HPM_DEBUG` et `HIPM` rendent `A5A5A5A2` ; à la prise
d'accès suivante, `WFPM_CTRL` **lui-même** rend `A5A5A5A2` ; après la remise à zéro, il se relit (`00000000`) et `CNVI`
échoue de nouveau ; `HIPM` illisible, le garde-fou arrête avant toute écriture interne (aucune écriture au hasard
cette fois).

Lecture : les registres `HPM_*` existent bien sur la 22000 (iwlwifi 5.10 à 6.12 les lit et les écrit pour les CNVi
intégrés : persistance, porte d'alimentation), mais **une lecture refusée bloque le bus des registres internes
jusqu'à la remise à zéro suivante** : `CNVI_AUX_MISC_CHIP` a été lu avant que `WFPM_CTRL` bit 31 (« ENABLE_WFPM ») ne
soit posé, et tout ce qui suivait a échoué. iwlwifi (5.10 comme 6.12), à la sonde de toute puce 8000 et plus, pose
**d'abord** ce bit puis lit `CNVI_AUX_MISC_CHIP` ; il ne lit **aucun** registre de l'UMAC (`UREG_*`, `UMAG_*`) avant le
micrologiciel.

Corrections (`trans.c`, `iwl.c`) :
* **attentes courtes au compteur du processeur** (`rdtsc`, étalonné sur 20 ms, jamais nul ; repli sur 4 lectures
  par µs si la mesure est absurde) ; au journal : « compteur à N MHz ; une lecture de registre de la puce dure N ns » ;
* **`ENABLE_WFPM` d'abord**, à la sonde puis après chaque remise à zéro (`wfpm_enable`, relu ; refus d'écrire si
  `WFPM_CTRL` est illisible) ;
* l'essai d'accès ne lit plus que ce qu'iwlwifi lit lui-même avant le micrologiciel, **dans l'ordre**
  `WFPM_CTRL`, `HPM_DEBUG`, `HIPM_GEN_CFG`, `CNVI`, et **s'arrête à la première erreur** (« OK n/4 », « PARTIEL »,
  « ÉCHEC ») ; plus aucune lecture d'`OTP_MINOR`, d'`UREG_CHICK`, de `UREG_CPU_INIT_RUN` ou de la LTR avant le
  lancement (le lancement les écrit, sans les relire) ;
* gardés : jamais d'écriture tirée d'une valeur d'erreur ; arrêt avant toute écriture interne ou avant le lancement
  si l'essai échoue.

Vérifié hors matériel : `iwltest` 142/142 ; QEMU q35 (e1000 forcé) : fin propre.

**Marche à suivre** (après `build.sh`, `sysroot.sh`, `korin.ps1 install`) :
1. `iwl probe -v` : attendus « attentes courtes : compteur à … MHz ; une lecture … dure … ns », « WFPM à la sonde :
   WFPM_CTRL 00000000 -> 80000000 » (avec `-v`), « accès aux registres internes à la sonde : **OK (4/4 …)** », puis
   « après la remise à zéro : OK », « après l'APM : OK », l'identité avec `CNVI` lisible (pas `A5A5A5Ax`).
2. Si OK : `iwl info -v` (« lancement : LTR … écrite, UREG_CPU_INIT_RUN = 1 écrit », puis « VIVANT : statut cafe »),
   puis `iwl scan`.
3. Rapporter les lignes « iwl : » (`journal`, `D:\journal.txt`), surtout la première ligne « accès aux registres
   internes » qui n'est pas OK et son « n/4 ».

### Troisième essai sur le ZBook (13/11) : le micrologiciel tourne ; taille de MCC_UPDATE corrigée

Journal (`D:\iwlprobe.txt`, `D:\iwlinfo.txt`) : accès aux registres internes **OK 4/4** à chaque étape, interruption
ALIVE vue, **« VIVANT : statut cafe »** (LMAC 77, UMAC 25), configuration initiale, fin de l'initialisation, **NVM lu**
(version 1227, carte 5, 4 adresses MAC réservées, 2,4 et 5 GHz, n/ac/ax, antennes 3/3, LAR, 13 + 25 canaux valides),
adresse MAC lue (la même que sous Windows ; elle n'est pas recopiée ici), `TX_ANT` (1.98), `BT_CONFIG` (1.9b),
`SOC_CONFIGURATION` (2.01) acceptés. **Puis `MCC_UPDATE` (1.c8) : assertion du micrologiciel** (`SW_ERR`), erreur UMAC
`id 201002fd`, `data 000601c8 0000001c 00000020` : la commande n° 6 (`1.c8`), **28 octets attendus, 32 reçus**.

Cause : `iwl` envoyait 32 octets ; la structure publique (`mcc` 16 bits, `source_id`, réservé, `key` 32 bits, 20
octets réservés) en fait **28**. Corrigé ; toutes les tailles des commandes sont maintenant des constantes nommées
(`iwlfw.h`), et la contre-épreuve (`check.py`) les recalcule d'après les structures publiques : `INIT_EXTENDED_CFG` 4,
`NVM_ACCESS_COMPLETE` 4, `NVM_GET_INFO` 4, `TX_ANT` 4, `BT_CONFIG` 8, `SOC_CONFIGURATION` 8, **`MCC_UPDATE` 28**,
`SCAN_CFG` 12, `SCAN_REQ_UMAC` 1 940 : justes. Le reste de la séquence jusqu'au balayage (`SCAN_CFG` v5,
`SCAN_REQ_UMAC` v15) était déjà vérifié octet pour octet.

**Marche à suivre** (après `build.sh`, `sysroot.sh`, `korin.ps1 install`) : `iwl info -v` → après « latence du SoC »,
« régulation : pays « XX » (source …), 51 canaux dont N permis », « configuration du balayage », « PRÊT » ; puis
`iwl scan` → « balayage actif sur N canaux », « balayage fini … (statut 1, complet) », la liste des réseaux ; aussi
`iwl scan passif`. En cas d'assertion : rapporter les lignes « erreur UMAC » et « erreur LMAC » (l'identifiant et le
`data` disent quelle commande et quelle taille).

## Wi-Fi AX201, étapes 2 à 4 : connexion, WPA2, service « wifi » (13/11)

Après l'étape 1 réussie sur le ZBook (`iwl info` jusqu'à PRÊT, `iwl scan` liste les réseaux), `iwl` devient le
**pilote en service** du Wi-Fi du PC, lancé par hwmanager (`pci:8086:43f0 = SD0:/sys/svc/iwl serve`), avec
**le même service « wifi » que le Pi** : NetKit (`wifi_*`), la commande `wifi`, Réglages › Wi-Fi et `wifimenu`
servent tels quels. Rien de cela n'a tourné sur la vraie puce : c'est l'essai de l'utilisateur.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/iwl/assoc.c` | la station : contexte MAC (station, BSSID), contexte PHY (canal, 20 MHz) et RLC v2, liaison, station du point d'accès (ADD_STA v12, n° 0), file d'émission (SCD_QUEUE_CONFIG v3, TID 15, 64 TFD, table des longueurs), protection de session ; authentification « système ouvert », association (station 802.11a/g : ni HT ni WMM, débits du point d'accès), contexte MAC « associé » (AID, DTIM et TSF de la dernière balise) ; EAPOL, puis PTK et GTK dans la puce (ADD_STA_KEY v3 : CCMP en matériel ; GTK TKIP aussi) ; trames de données (commande TX gen2 de 20 o, en-tête, données, 3 TB ; débit fixé par l'hôte : 6 Mb/s pour la gestion, 12 à 54 Mb/s OFDM selon le signal pour les données) ; réponses TX (places rendues par le SSN) ; déconnexion et balises manquées (24 de suite) = lien perdu |
| `src/svc/iwl/mlme.c` | sans matériel : trames d'authentification, d'association, de déconnexion ; Ethernet ↔ 802.11 (LLC/SNAP) ; l'élément RSN de la station (CCMP, PSK ; refuse MFP obligatoire, SAE seul, PSK-SHA256 seul) ; le supplicant WPA2-personnel (messages 1 à 4, renouvellement de la GTK, compteurs, MIC, déballage AES) |
| `src/svc/iwl/wifisvc.c` | le service « wifi » repris de `src/svc/wifi/wifi.asm` : opérations 1 à 8 (état, balayage en entrées de 48 o, rejoindre, quitter, oublier, connus, priorité, pays), `SD0:/etc/wifi.txt` lu et écrit au même format (copie `wifi.bak`), connexion automatique, `korin.net.wifi` sur le bus, notifications « Clé refusée pour … », « Aucun réseau connu » ; jamais de clé au journal |
| `src/svc/iwl/iwl.c` | `iwl serve` : la puce, `wlan0` inscrite chez netd (`NIF_WIFI`, priorité 20, comme le Pi), le service, le thread de la puce (réception, trames de netd, lien perdu) ; `be_*` : la puce pour le service, sous un verrou ; `iwl` à la main (`fwinfo`, `probe`, `info`, `scan`) refuse poliment quand le service tient la puce |
| `src/svc/iwl/trans.c` | `send_cmd_v` (version dans l'en-tête : RLC v2) ; pas de lecture imbriquée de l'anneau (les réponses EAPOL et les clés partent après, `sta_pending`) |
| `src/svc/init2/init2_x86_64.c` | à l'extinction : `iwl` gardé jusqu'au bout, puis `SVC_SIGNAL 1` sur « wifi » : déconnexion, puce arrêtée, DMA coupé ; attente 3 s |
| `skel/sd0/etc/drivers.txt` | `pci:8086:43f0 = SD0:/sys/svc/iwl serve` (deux lignes) |
| `tools/pc/iwltest/mlmetest.c` | point d'accès simulé : RFC 3394 (emballage), RSN, trames, données, 4 voies, GTK, mauvaise clé |

Connexion automatique, comme le Pi (docs/RESEAU.md 4.2) : au démarrage le réseau connu préféré (priorité puis
signal ; balayage dirigé s'il le faut) ; puis balayage et essai des réseaux connus présents ; lien perdu
(déconnexion du point d'accès, 24 balises manquées) : le même réseau après 2 s, puis le cycle ; tant qu'aucun réseau
connu n'est joint, un balayage toutes les 5 s, 10 s… jusqu'à 60 s, **sans fin** ; un `BALAYER` (Réglages ›
Actualiser, ouverture de wifimenu) qui voit un réseau connu alors qu'on n'est pas connecté **relance la connexion
tout de suite** (attente remise à 5 s) ; après `QUITTER`, plus rien jusqu'au prochain `REJOINDRE`. Connecté, un
`BALAYER` balaye sans couper la connexion (temps hors canal 120 TU, suspension 30, comme iwlwifi) : la liste de
wifimenu et de Réglages reste fraîche. Le pays (`pays=`, opération 8) est noté ; avec LAR la puce suit la
réglementation qu'elle détecte (iwlwifi n'envoie pas non plus le pays de l'utilisateur).

Limites (étapes 5 et 6) : débit fixe (pas de TLC ni d'agrégation, station a/g : 54 Mb/s au plus), pas d'économie
d'énergie gérée, un seul point d'accès à la fois (pas d'itinérance), WPA3-SAE, PSK-SHA256 et MFP obligatoire refusés
(« non pris en charge » au journal), WPA1/TKIP par paire refusé (GTK TKIP acceptée).

### Vérifié (hors matériel, 13/11)

* `sh tools/pc/iwltest/run.sh` : **163 vérifications, 0 échec**, dont : RFC 3394 4.1 (emballage et déballage) ;
  l'élément RSN `30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 02 00 00` ; refus de MFP obligatoire et de
  SAE seul ; la trame d'authentification octet pour octet ; la demande d'association (SSID, 8 débits + 1 étendu, RSN) ;
  les réponses (statut, AID) ; Ethernet → 802.11 (ToDS, protégé, LLC/SNAP, n° de séquence) et 802.11 → Ethernet
  (FromDS, en-tête CCMP sauté ; null data ignorée) ; **4 voies contre un point d'accès simulé** : message 2 (MIC,
  key_info 010a, SNonce, RSN, compteur), message 3 accepté (TK, GTK n° 1, RSC), message 4 (key_info 030a), message 3
  rejoué refusé, **renouvellement de la GTK** (n° 2, réponse 0302), **mauvaise clé** (MIC du message 2 faux pour le
  point d'accès, message 3 refusé) ;
* `check.py` : le **MIC du message 2** refait avec `hashlib`/`hmac` ; les **tailles des commandes de la station**
  recalculées d'après les structures publiques : MAC_CONTEXT 148, PHY_CONTEXT 32, RLC 32, BINDING 28,
  SESSION_PROTECTION 24, ADD_STA 48, ADD_STA_KEY 76, SCD_QUEUE_CONFIG 36, TX gen2 20 : justes ;
* QEMU q35 + OVMF (console, disque de `mkkorindisk.py`, sans AX201) : hwmanager ne lance rien ;
  `SD0:/sys/svc/iwl serve` → « iwl : pas d'AX201 : aucune fonction 8086:43f0 … ; rien n'est touché », code 0 ;
  `wifi` → « pas de WiFi (puce absente, firmware manquant ou QEMU) », `wifi scan` → « pas de WiFi » (comme sans Wi-Fi
  sur le Pi) ; `shutdown` : extinction propre (sd0, sd1, nvme). `tools/check_constants.py` passe.

### Pas vérifié

Tout ce qui touche la vraie puce : contextes et station acceptés par le micrologiciel, émission des trames de gestion
(débit, antenne B), réponses du point d'accès, EAPOL réel, clés dans la puce, CCMP, trames de données, DHCP sur
`wlan0`, balises manquées, reconnexion, arrêt par init2 ; Réglages et `wifimenu` avec une vraie liste (ils ne
changent pas : même protocole que le Pi).

### Marche à suivre sur le ZBook (par l'utilisateur)

1. Construire et installer comme à l'étape 1 (`build.sh`, `sysroot.sh`, `korin.ps1 install`) : `etc\drivers.txt`,
   `sys\svc\iwl` et init2 à jour. `D:\etc\wifi.txt` (copié du Pi) est lu tel quel et gardé par l'installation.
2. Démarrer Korin. **Rien à taper** : hwmanager lance `iwl serve`. Au bout de quelques secondes, l'icône Wi-Fi de la
   barre passe à « connecté » (barres de signal) si un réseau de `wifi.txt` est à portée ; sinon :
   * menu Wi-Fi de la barre (clic sur l'icône) : la liste des réseaux, cadenas ; clic sur le réseau, saisir la clé,
     **Se connecter** ; ou Réglages › Wi-Fi : **Actualiser**, choisir le réseau, clé, **Se connecter** (mémorisé dans
     `wifi.txt`).
3. Vérifier au Terminal : `wifi` (état « connecté », SSID, signal), `netstat -i` (wlan0, adresse du bail DHCP),
   **`ping`** (passerelle puis un nom), `nslookup example.com`, puis une page (Jet) ou `git clone https://…`.
4. Essais de comportement : éteindre la box une minute puis la rallumer (le PC doit se reconnecter seul, en 5 à 60 s ;
   Réglages › Actualiser accélère) ; `wifi leave` (déconnexion, pas de reconnexion) puis `wifi join NomDuReseau`
   (reconnexion avec la clé mémorisée) ; extinction normale (« init2 : iwl arrêté » au journal).
5. Rapporter `D:\journal.txt` (`journal`) : toutes les lignes « iwl : » (dont « wifi : … », « connexion à … »,
   « associé (AID …) », « EAPOL : … », « CONNECTÉ à … », « lien perdu »), celles de netd (« DHCP », « Wi-Fi … »), et en
   cas d'échec les lignes « ÉCHEC », « erreur UMAC / LMAC », « registres ». **Jamais la clé** : elle n'est écrite nulle
   part au journal.

**Signes de problème** : « ÉCHEC : … (commande x.yy) » ou « erreur UMAC » (une commande que le micrologiciel refuse :
l'identifiant et le `data` disent laquelle) ; « pas de réponse à l'authentification / à l'association » (la puce
n'émet pas, ou pas sur le bon canal) ; « échange EAPOL à 4 voies pas fini (pas de message 1) » (le point d'accès n'a
pas reçu l'association ou attend autre chose) ; « (MIC faux) » ou « Clé refusée » : la clé ; « associé » puis rien
au DHCP : les trames de données (rapporter aussi `netstat`). Pour revenir au lancement à la main : `#` devant la ligne
`pci:8086:43f0` de `D:\etc\drivers.txt`.

## Son : la musique des apps sur le PC (13/11)

Retour du ZBook : le son sort (HDA et casque USB, même défaut sur les deux), `sontest` est juste, mais la musique de
Cosmo Zoom et d'Arabesque sonne « comme des ressorts », plus grave que sur le Pi.

### Cause : pas de SoundFont sur le PC

Les deux apps jouent leur musique sur le synthé General MIDI d'AudioKit (`ak_synth_new (0, …)`), qui prend la
SoundFont du système (`SD0:/res/soundfonts/GeneralUser-GS.sf2`, `ak_sf_default`) et, **sans elle, sa banque FM**.
La carte du Pi la reçoit de `build.ps1` (depuis le clone d'Onyx) ; `tools/pc/sysroot.sh` ne la copiait pas : sur
le ZBook, toute la musique passait par la banque FM. Ses instruments ont l'énergie dans les harmoniques hautes
(fondamentale faible : le « ressort ») et certains sonnent une octave plus bas (basse synthé, programme 38 : 55 Hz
pour un la2 à 110 Hz). Ce n'est ni un écart asm → C ni un écart Pi → PC :

* **asm → C, Pi → PC écartés** : le banc différentiel d'AudioKit (`src/kits/audiokit/test/cmp`, `run.sh synth fm`)
  donne 0 écart asm/C sous `qemu-aarch64` et un C x86-64 identique au C d'AArch64 (58 274 lignes d'état pour le
  synthé, voix et oscillateurs compris ; 21 234 pour la FM). Rendu hors ligne des mêmes notes (programmes 0, 38, 81,
  89, 98 ; la1 à la4) par le même source C compilé pour AArch64 (`qemu-aarch64`) et pour x86-64 : **mêmes octets**
  (MD5 égaux), avec la banque FM comme avec la SoundFont. Pas de troncature, ni de `char`, ni de flottant qui diffère.
* **pilotes et fréquence écartés** : `sontest` juste sur les deux sorties ; la chaîne `sound` → `hda` sous QEMU rend
  la SoundFont à la bonne hauteur (ci-dessous).

### Correction

`tools/pc/sysroot.sh` copie la SoundFont dans `res/soundfonts/` (avec sa licence), cherchée dans `$ONYX_DIR`
(chemin WSL ou Windows), `build/onyx` (clone de `tools/extra/fetch.sh`), un clone voisin `../Onyx`, puis
`build/sdroot` ou `build/sdcard-full` ; sinon il le dit (« la musique des apps sera celle de la banque FM »).
32 Mo de plus sur KORIN OS ; `korin.ps1 install` la copie avec `\res`. Rien ne change pour le Pi (aucun source du
kit, du service ni des apps touché).

### Vérifié

QEMU q35 + OVMF, `mkkorindisk.py --layout zbook --korin-mb 400`, `-device intel-hda -device hda-duplex -audiodev
wav`, `startup.txt` : `play SD0:/res/t.mid` (fichier fait pour l'essai : la4 au programme 81, la2 au 38, do4 au 0,
la5 au 81, une seconde chacun) ; hauteur par autocorrélation, timbre par centroïde du spectre :

| Note | Attendu | PC avant (banque FM) | PC après (SoundFont) | Hôte, SoundFont (= Pi) |
|---|---|---|---|---|
| la4, lead 81 | 440,0 Hz | 440,5 Hz, pic à 8 797 Hz (20ᵉ harmonique), centroïde 8 785 Hz | 440,4 Hz, pic 440,3 Hz, centroïde 3 061 Hz | 440,6 Hz, centroïde 3 492 Hz |
| la2, basse 38 | 110,0 Hz | **55,0 Hz** (une octave plus bas), pic 1 043 Hz | 109,7 Hz, pic 109,9 Hz, centroïde 573 Hz | 109,6 Hz, centroïde 526 Hz |
| do4, piano 0 | 261,6 Hz | 261,6 Hz, pic 1 306 Hz (5ᵉ harmonique) | 262,6 Hz, pic 261,4 Hz | 261,4 Hz |
| la5, lead 81 | 880,0 Hz | 881,9 Hz, pic 20 339 Hz, centroïde 11 911 Hz | 880,6 Hz, pic 882,3 Hz, centroïde 4 113 Hz | 880,8 Hz, centroïde 4 009 Hz |

La sortie « avant » de QEMU est celle de la banque FM rendue sur l'hôte (basse à 55 Hz, mêmes pics) ; la sortie
« après » est celle de la SoundFont, comme sur le Pi (le C d'AArch64, égal à l'asm, rend les mêmes octets que le C
x86-64). `tools/pc/sysroot.sh` relancé : `build/pc/sysroot/res/soundfonts/GeneralUser-GS.sf2` (32 319 396 octets).

### Reste

* **Sur le ZBook** (par l'utilisateur) : `wsl sh tools/pc/sysroot.sh` (la ligne « attention : pas de SoundFont » ne
  doit pas sortir), `korin.ps1 install`, puis Arabesque et Cosmo Zoom : la musique doit être celle du Pi. La première
  note peut tarder (la SoundFont de 32 Mo se lit au premier synthé, dans le thread du son).
* Le dépôt de paquets du PC (`packages/pc/`, pas encore fait) devra citer le paquet `soundfonts` (`arch = any`).
* `build.ps1` ne cherche la SoundFont que dans `$env:ONYX_DIR` et `build\onyx` : sur ce PC, aucun des deux n'existe
  (le clone est `C:\Projects\Onyx`) ; une carte du Pi refaite de zéro n'aurait pas la SoundFont (paquet
  `soundfonts` à installer, ou `ONYX_DIR`).
* La banque FM elle-même (repli sans SoundFont) est fidèle à l'asm, mais loin d'un General MIDI : la basse 38 une
  octave plus bas, des leads très brillants. Pas touchée (même son que le Pi sans SoundFont).

## Touchpad du ZBook (13/11)

Étude complète : [TOUCHPAD-PC.md](TOUCHPAD-PC.md). Le touchpad (`ELAN0750`, HID over I2C sur le contrôleur I2C du
LPSS `0000:00:15.0`, `8086:43e8`) n'est décrit que par l'ACPI (adresse I2C, registre du descripteur HID par
`_DSM`, interruption `Interrupt` ou `GpioInt`), et Korin n'a pas d'interprète AML : on lit les tables du ZBook
d'abord, puis le pilote prend ces valeurs dans un fichier.

* **`acpidump`** (fait) : `SD0:/acpi/<signature>.dat` (noms d'`acpixtract` : `dsdt.dat`, `ssdt1.dat`…) et
  `tables.txt` ; `ACPI_TABLE` donne aussi `"RSDP"`, `"XSDT"`, `"RSDT"` et `"****"` (l'entrée d'un rang de la
  XSDT). Même binaire dans `SD0:/bin` et `SD0:/sys/svc` (comme `iwl` : `src/svc/common/sysfront.c`). Vérifié sous
  QEMU q35, avec deux SSDT injectées : sommes justes, AML identique, `iasl -d` sans erreur.
* **`i2chid`** (écrit, essayé hors matériel seulement) : contrôleur DesignWare du LPSS en sondage (sortie de reset,
  horloge, 400 kHz, écriture puis lecture avec RESTART), **BAR0 donnée par i2chid** (le micrologiciel l'a laissée à
  0 : `0x6053ba6000` d'après le journal pcid, ou `bar=`), HID over I2C (descripteurs, `SET_POWER`, `RESET`,
  Input Mode = 3 par `SET_REPORT` Feature), descripteur de rapport (pavé « precision touchpad », souris, pointeur
  du clavier, boutons 1 à 3) et gestes de gt911 (glissé, toucher, **double toucher → glisser**, deux doigts →
  `EV_WHEEL`). Interruption : `irq =` (directe) ou `gpio =` (plot lu) si les tables les donnent, **sinon sondage
  toutes les 8 ms**. Réglages : `SD0:/etc/i2chid.txt` (`skel/pc/etc/i2chid.txt`). Lancé à la main
  (`i2chid probe | scan | info | run | stop`).
* **Vérifié** : `src/svc/i2chid/test/run.sh` (descripteurs d'exemple souris, Synaptics, ELAN : modèles et gestes,
  tout juste) ; sous QEMU, i2chid dit « pas de contrôleur I2C du LPSS d'Intel » et s'arrête, bureau inchangé.
* **Marche à suivre** : `acpidump` sur le ZBook (tables dans `D:\acpi`), puis remplir `i2chid.txt`, puis
  `i2chid probe -v`, `scan`, `info -v`, `run` (TOUCHPAD-PC.md, « Marche à suivre »).
* **Tables du ZBook** (13/11, TOUCHPAD-PC.md « Ce que disent les tables du ZBook ») : `\_SB.PC00.I2C0.TPD0`
  (`ELAN0750`, `PNP0C50`) sur 00:15.0, **adresse 0x2C** (pas 0x15 : en dur dans `_INI`), **descripteur HID au
  registre 0x20**, 400 kHz, **GpioInt niveau actif bas** (Windows : IRQ 1024), plot donné par `GPDI` de la NVS ;
  le `_PS3` du contrôleur le met en D3 **et efface la BAR0** (le « BAR0 0 » de pcid), le `_PS0` est vide ; FMCN
  dans la NVS. Noyau : `ACPI_TABLE "FWMM"` lit la mémoire NVS du micrologiciel ; i2chid y lit le plot et le FMCN
  (`nvs_*` de `i2chid.txt`, déjà rempli) et sonde le plot (un mot MMIO toutes les 8 ms) au lieu du touchpad.
* **Essai sur le ZBook** (13/11) : le touchpad **fonctionne**. Ensuite : vitesse dans `SD0:/etc/pointer.ini`
  appliquée par hidmanager (touchpad 200 % par défaut, souris 100 %, accélération des souris au choix), volet
  **Souris** des Réglages (à chaud, `HID_OP_POINTER`) ; hidmanager garde les boutons **par pilote** (touchpad et
  souris USB ensemble : un mouvement de l'un ne relâche plus le bouton tenu sur l'autre ; vérifié sous QEMU, PS/2
  et USB) ; i2chid **au démarrage** (`pci:8086:43e8` de `drivers.txt`), arrêt propre sur le SIGTERM d'init2.
* **Reste** : AML à la main, interruption par le GPIO du PCH (IRQ partagée), toucher et sens du défilement au
  panneau, défilement horizontal.

## Terminal muet et journal (13/11)

Deux défauts vus sur le vrai ZBook : après quelques `iwl probe`, `iwl info`, `i2chid probe`, ces commandes ne
montraient plus rien au Terminal (les autres commandes, si ; un nouveau Terminal n'y changeait rien), et `journal`
restait sans rien dire jusqu'au Ctrl+C, `D:\journal.txt` figé à 08:57.

### Terminal muet : la façade perdait sa place dans le journal

* Les copies de `SD0:/bin` (`iwl`, `i2chid`, `acpidump`, `intelgpu`) lancent leur jumeau de `SD0:/sys/svc` par
  init2 et recopient ses lignes du journal du noyau. Leur position dans le journal était **un nombre d'octets** :
  la taille rendue par `SYS_KLOG` au lancement, puis les lignes au-delà. Mais `SYS_KLOG` rend **les 64 derniers
  Ko** d'un anneau : une fois plein, la taille reste 65 536, aucune ligne n'est plus « au-delà », pour tous les
  lancements suivants et dans tout Terminal (rien n'est bloqué : les programmes tournent, leurs lignes vont au
  journal, la façade ne les voit pas). Sur le ZBook, le journal (pcid, usbd, iwl, i2chid…) passe vite les 64 Ko.
* Ce qui l'accélérait : chaque ligne d'un programme lancé par init2 au bureau arrivait **deux fois** au journal
  (`vslog` écrivait au journal, puis à la sortie standard, qui sans sortie donnée par le père est la console,
  c'est-à-dire encore le journal), et deux fois au Terminal.
* **Correction** (`src/svc/common/sysfront.c`, `sysfront.h`) : la position est **la dernière ligne complète vue,
  horodatage compris** (`[  123.456] …`), cherchée de nouveau dans le tampon à chaque tour ; si elle est sortie
  de l'anneau, tout le tampon est nouveau (sans sa première ligne coupée). Exporté : `klog_follow_start`,
  `klog_follow(fn)`. `intelgpu` s'en sert (sa copie supprimée, `svc.conf` : `../common/sysfront.c`). `vslog`
  n'écrit plus à la sortie standard sans sortie donnée par le père (plus de doublon).
* **`iwl` n'est pas corrigé ici** : `src/svc/iwl/iwl.c` (`klog_len`, `klog_show`, `front`) a sa propre copie
  du même suivi, et le chantier Wi-Fi y travaille. À faire là : remplacer `klog_show(&from)` par
  `klog_follow(show_line)` (le filtre « iwl : » et le saut des doublons dans `show_line`), `long from =
  klog_len()` par `klog_follow_start()`, et ajouter `../common/sysfront.c` aux `sources` de `svc.conf`. Tant que
  ce n'est pas fait, `iwl probe` redevient muet dès que le journal est plein.

### `journal` : 20 s de silence

* `journal` (C comme asm) était fait pour `startup.txt` : 20 s d'attente avant d'écrire `SD0:/journal.txt`, une
  seconde écriture à 60 s, sans rien dire avant. Tapé au Terminal, il semblait bloqué ; un Ctrl+C avant 20 s
  ne laissait rien d'écrit. Rien d'autre : ni droit, ni volume, ni init2 (journal est dans `SD0:/bin`, lancé sans
  init2).
* **Correction** (`src/bin/journal/journal.c`) : écrit **tout de suite** ; avec une sortie donnée par son père
  (`PROC_FLAGS` bit 1 : Terminal, console, tube), il s'arrête là (code 1 si l'écriture échoue) ; sans (lancé
  par `startup.txt`), il réécrit après 20 s puis 60 s comme avant. Vaut aussi pour le Pi (même source).
* Rappel : `journal.txt` est l'anneau, soit les **64 derniers Ko** ; après beaucoup de lignes, le début du
  démarrage n'y est plus.

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, disque de `mkkorindisk.py --layout zbook --korin-mb 400`, arbre de
`sysroot.sh` + `sysroot_apps.sh`, Terminal ouvert par `startup.txt`, 13/11)

* **Reproduit avec les anciens binaires** : `for i in 1 … 40; do i2chid probe > /tmp/o; done`, puis
  `i2chid probe` et `acpidump` : **rien** au Terminal (le journal de QEMU montre qu'ils ont tourné ; avant que
  l'anneau soit plein, mêmes commandes : chaque ligne deux fois). L'ancien `journal` : premier « écrit » après
  20 s, pas fini 75 s après.
* **Corrigé** (disque sans `--snapshot`) : 40 fois `i2chid probe` et `acpidump` (80 lancements par la façade),
  puis `i2chid probe`, `acpidump`, `intelgpu probe` : leurs lignes au Terminal, **une seule fois** chacune ;
  `journal` → « journal : SD0:/journal.txt écrit » aussitôt, invite rendue ; `ls -l` : 65 536 octets (l'anneau
  plein) ; `shutdown`. L'image relue dans WSL (`mdir`, `mtype -i IMG@@71M`) : `journal.txt` daté de l'heure de
  l'essai, 65 536 octets, finit par les lignes `intelgpu` de ce démarrage.
* **Console texte** (`shell=console`) : les mêmes 80 lancements puis `journal` (« écrit » aussitôt), `i2chid probe`,
  `intelgpu probe` : leurs lignes recopiées (en blanc) ; la console du noyau montre aussi le journal lui-même (en
  gris), comme avant.
* **Pi** (QEMU raspi4b, emplacement 3, `BIN_TAG=tj sh tools/bin/compare.sh journal`) : le C écrit une fois et rend
  0 aussitôt, l'asm deux fois (seule différence, voulue ; `test.txt` mis à jour) ; `SD0:/journal.txt` non vide.

### Pas vérifié

* Le vrai ZBook ; `iwl` (voir plus haut).
* `intelgpu` (le service) écrit encore chaque ligne deux fois au journal (son `glog`, pas `vslog`) ; la façade
  n'en montre qu'une.

## Jet pour le PC (13/11)

Jet, le navigateur WebKit de Korin (WebKit2 : les process UI, web et réseau d'un même programme), se construit pour
Korin PC x86-64 **des mêmes sources et des mêmes scripts que le Pi**, avec `ARCH=x86_64` : JavaScriptCore **avec le
JIT x86-64** (de base, DFG et FTL), WebGL par ANGLE et **GlKit** (Mesa iris sur le GPU Intel, softpipe sans lui), curl
sur l'OpenSSL du PC, et pour les sites le **user agent de Safari de bureau** (macOS). Rien ne change pour le Pi.

### Construire (WSL, depuis la racine du dépôt ; ≈ 1 h la première fois sur 16 cœurs)

```
ARCH=x86_64 sh tools/webkit/build-deps.sh                    # sysroot (libposix du PC) + les 11 dépendances : 6 min
ARCH=x86_64 INTERP=ftl sh tools/webkit/build-jsc.sh install  # jsc seul, avec le JIT (pour les essais) : 8 min
ARCH=x86_64 sh tools/webkit/build-ui.sh                      # UIKit depuis le C (relais x86-64)
ARCH=x86_64 sh tools/webkit/build-media.sh                   # libonyxav (son par AudioKit)
ARCH=x86_64 sh tools/mesa/build-webkit.sh                    # WebKit, ANGLE, JIT : 41 min (7733 cibles)
ARCH=x86_64 sh tools/webkit/build-jet.sh                     # Jet, lié à GlKit du PC (tools/mesa/build-iris.sh avant)
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_apps.sh && sh tools/pc/sysroot_webkit.sh   # TESTS=1 : jsc, pages d'essai
```

Tout va dans `~/korin-webkit-x86_64` : `sysroot/`, `ports/`, `webkit/` (`src/extra/webkit` et les patchs de
`tools/webkit/patches`), `webkit-gl/` (la même avec ANGLE et `enable-webgl.py`), `webcore/`, `jsc-ftl/`, `out/`.

| Fichier | Ce qui change |
|---|---|
| `tools/webkit/env.sh`, `armpi-toolchain.cmake`, `sysroot.sh` | `ARCH=x86_64` : chaîne `x86_64-onyx-elf`, libposix du PC et ses specs (`onyx.specs` = `posix.specs` + `__onyx__`, avec `*cc1plus`) ; sans `ARCH`, les variables et les commandes de CMake du Pi sont les mêmes (comparées) ; `ONYX_ARCH`, `ONYX_CPU_FLAGS` en plus |
| `tools/webkit/ports/libpng`, `libjpeg-turbo`, `skia` | filtres SSE2 de libpng ; libjpeg-turbo sans SIMD (la copie n'a que `simd/arm`) ; Skia : `SkOpts_x86_baseline.cpp` (les optimisations SSSE3 / AVX2 choisies à l'exécution manquent à la copie : SSE2 de base) |
| `tools/webkit/build-jsc.sh` | `INTERP=ftl` |
| `tools/webkit/patches/0002-x86-avx-osxsave.patch` | JSC n'émet de l'AVX que si le système sauve les registres YMM (`CPUID.OSXSAVE`, `XCR0`) : **le noyau du PC ne sauve que l'état SSE (FXSAVE)**, l'AVX de Tiger Lake y serait une faute #UD |
| `tools/webkit/patches/0003-x86-user-agent.patch` | le user agent propre à WebKit (pages `file:`, `SD0:/etc/web-desktop-ua`) dit `X11; Linux x86_64` |
| `tools/mesa/build-webkit.sh`, `tools/webkit/build-jet.sh`, `build-ui.sh`, `build-media.sh`, `av.mk` | chemin x86-64 ; JIT : `-DENABLE_JIT=ON -DENABLE_DFG_JIT=ON -DENABLE_FTL_JIT=ON` |
| `tools/webkit/glue/webkit2_armpi.c`, `src/extra/onyxports/jet/present.c` | `syscall` (`armpi/syscall.h`), horloge par `clock_gettime` ; `ui_surface_pixels` par `uikit_call3` |
| `src/extra/onyxports/jet/engine_webkit.cpp` | sur le PC, les sites `http(s)` reçoivent `Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.2 Safari/605.1.15` (le Pi : Chrome sur Android, inchangé) |
| `src/extra/onyxports/jet/main.cpp` | sur le PC, **chemin logiciel par défaut** (la toile WebGL relue et peinte dans la page) : gpucomp est le compositeur du V3D, il composerait ici au processeur et la toile y resterait vide (sa texture n'est partagée avec Mesa que sur le Pi) ; `SD0:/etc/web-gpu` ou `--gpu` : le compositeur |
| `tools/pc/sysroot_webkit.sh` (nouveau) | dans l'arbre : `apps/jet.app` (main, app.txt, start.html, aides, icône), `etc/ca-bundle` (absent de l'arbre du PC : https), `SD0:/apps/jet.app/main` dans `etc/preload.txt` ; `TESTS=1` : `bin/jsc`, `webkit/` (pages et scripts d'essai) |
| `tools/webkit/tests/jitbench.js`, `jsctest.sh`, `jetpc.html` | banc du JIT ; une page : user agent, JavaScript, WebGL, CSS |

**Le JIT sur Korin PC** : rien à changer au noyau ni à libposix. JSC réserve son pool de code (1 Go) en `mmap`
anonyme RWX (accepté par `VM_MAP`, le noyau du PC ne fait pas de W^X), `mprotect` des régions anonymes marche, le
cache d'instructions de x86 est cohérent ; pas de signaux (les « VM traps » par sondage, pas de suspension de thread :
le collecteur suit `Heap::finishCollectionsBeforeReleasingAccess` d'Onyx, comme le Pi). Seul l'AVX demandait la
retouche ci-dessus. WebAssembly : l'interpréteur IPInt (BBQ et OMG éteints).

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, `-cpu max`, `mkkorindisk.py --layout zbook --korin-mb 600`, arbre de `build/pc/sysroot` + `sysroot_webkit.sh` avec `TESTS=1`)

* **jsc** (`bash SD0:/webkit/jsctest.sh` par `startup.txt`) : `42`, `smoke.js` **27/27** avec le JIT ; `jitbench.js`
  (mêmes résultats dans les trois modes) :

  | jitbench.js | JIT (de base, DFG, FTL) | sans FTL | LLInt seul (`JSC_useJIT=false`) |
  |---|---|---|---|
  | fib(27) | 232 ms | 219 | 634 |
  | 20 M additions | 493 | 357 | 14 720 |
  | crible 2 M × 3 | 960 | 2 458 | 60 837 |
  | mandelbrot 300 × 300 | 774 | 952 | 16 976 |
  | n corps, 200 000 pas | 1 658 | 1 428 | 4 372 |
  | 1 M objets | 443 | 387 | 1 804 |
  | tri de 200 000 nombres | 735 | 832 | 2 086 |
  | chaîne de 300 000 | 53 | 56 | 643 |
  | **total** | **5 348 ms** | 6 689 | **102 072** (19 fois plus lent) |

  Sous TCG l'interpréteur est plus pénalisé que le code natif : sur le ZBook l'écart sera moindre.
* **Jet, page locale** (`SD0:/apps/jet.app/main /webkit/jetpc.html`) : les trois process, « compositing off (the
  software path) », `glkit -> 0`, page chargée en 4,6 s ; grille CSS, ombres, accents, grec, cyrillique ; JavaScript :
  5 M tours en 274 ms ; **WebGL** : contexte `webgl` (WebGL 1.0 : sous softpipe, ANGLE ne donne pas d'ES 3), triangle
  rendu, pixel du centre 255,153,26 (attendu), erreur GL 0 (capture `build/pc/jet_pc_local.png`). Image de Jet
  (126 Mo) préchargée par init2 en 313 ms.
* **Réseau** (`usb-net` de QEMU, réseau user ; usbnet ECM, DHCP 10.0.2.15) : `http://10.0.2.2:8765/jetpc.html` (un
  serveur de la machine hôte) chargée, le serveur voit la requête ; la page lit
  `navigator.userAgent` = **`Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)
  Version/26.2 Safari/605.1.15`** (journal : « user agent: desktop Safari's ») ; **`https://example.com/`** chargée en
  1,3 s (certificat vérifié par OpenSSL et `etc/ca-bundle`, `s.js` aussi) dans une deuxième fenêtre (captures
  `jet_pc_http.png`, `jet_pc_https.png`).
* Le Pi ne change pas : `libuikit_c.a`, `uitest`, `libonyxav.a`, `avplay` refaits par les scripts d'avant et
  d'après, identiques ; `present.o`, `webkit2_armpi.o`, `engine_webkit.o`, `main.o` compilés pour le Pi avant et
  après : code et données identiques ; le fichier de chaîne CMake donne les mêmes commandes.

**Taille ajoutée à l'arbre** : `apps/jet.app` 123,6 Mo (`main` 126 417 080 octets dépouillés : 98,8 Mo de code,
27,6 Mo de données dont ICU) ; `etc/ca-bundle` 0,2 Mo. Avec `TESTS=1` : `bin/jsc` 48,7 Mo et `webkit/` (quelques Ko).

### Marche à suivre sur le ZBook (par l'utilisateur, après installation sur D:)

1. Le bureau ; le lanceur (catégorie Internet) ou le Terminal : `SD0:/apps/jet.app/main` (la page d'accueil), ou
   avec une adresse : `SD0:/apps/jet.app/main https://fr.wikipedia.org/`. Le Wi-Fi doit être connecté (`wifi`).
2. Pages à essayer : `https://example.com/`, `https://fr.wikipedia.org/`, `https://www.youtube.com/` (**la vidéo**
   depuis le 13/11 : VP9 / AV1 par Media Source ; dire la définition choisie, les images sautées : clic droit sur la
   vidéo, « Statistiques pour les geeks », si le site le permet), `https://github.com/`, un site d'actualités.
   Les onglets (Ctrl+T, plusieurs sites, Ctrl+Tab), une recherche tapée dans l'adresse (Google), se connecter à un
   site (Gmail), fermer Jet, le relancer : toujours connecté.
   Les sites doivent servir leur version de bureau (Safari sur Mac). `https://www.whatismybrowser.com/` montre le
   user agent.
3. **WebGL** sur le GPU Intel (le service `intelgpu` tourne : `intelgpu status`) : `https://get.webgl.org/`,
   `https://get.webgl.org/webgl2/`, `https://threejs.org/examples/` (par exemple `webgl_animation_keyframes`),
   `https://webglsamples.org/aquarium/aquarium.html` ; dire les images/s et si l'image est juste. Sans le service :
   softpipe (lent, mais juste).
4. Le JIT : `https://browserbench.org/JetStream/` ou `https://browserbench.org/Speedometer3.1/` (le score) ;
   avec `TESTS=1` : `bash SD0:/webkit/jsctest.sh` au Terminal (JIT contre LLInt, résultat aussi dans
   `SD0:/webkit/jsctest.txt`).
5. `journal`, puis rapporter `D:\journal.txt` (lignes `web:`, `glkit`, `intelgpu`). `SD0:/etc/web-console` (fichier
   vide) met la console des pages au journal.

### Pas vérifié, reste

* Rien sur le vrai ZBook : WebGL sur iris (WebGL 2 attendu, ES 3.2), le JIT sur un vrai Tiger Lake, la vitesse réelle,
  les sites lourds en version de bureau.
* **AVX** : le noyau du PC pourrait l'activer (XSAVE / XRSTOR, `CR4.OSXSAVE`, `XCR0`, zone de sauvegarde par thread
  et dans `sigframe.c`) ; JSC, Skia et Mesa s'en serviraient alors d'eux-mêmes.
* Skia sans ses optimisations SSSE3 / AVX2 (sources à reprendre de la révision de WebKit, `src/extra/skia-m154`).
* Le compositeur sur le GPU Intel (gpucomp sur iris, ou l'image de la toile partagée avec Elegant) : aujourd'hui le
  chemin logiciel ; WebGL relu à chaque image.
* WebAssembly BBQ / OMG (éteints, comme le Pi) ; polices CJK (seulement
  DejaVu : carrés sur les pages chinoises / japonaises, comme sur le Pi).

### Vidéo, onglets, recherche, cookies (13/11, suite)

Le détail est dans [WEBKIT.md](../WEBKIT.md) (« Étape 6 — son et vidéo », « Jet : onglets, barre d'adresse,
cookies ») ; ce qui touche le PC :

* **Vidéo** : libvpx (VP8, VP9) et dav1d (AV1) liés dans `libonyxav.a` en C générique (`AV_ARCH=generic` de
  `codecs.mk` : ni SSE / AVX de libvpx, ni l'assembleur nasm de dav1d), un thread chacun ; Media Source construit.
  Décodage seul (`avplay --decode`) mesuré sur un i7-11800H (Tiger Lake, comme le ZBook), le même C compilé pour
  Linux : **VP9 480p 181 images/s, 720p 86 ; AV1 480p 157 ; VP8 360p 347** : la 720p de YouTube passe largement
  sur un cœur, la 1080p VP9 serait juste (≈ 38 images/s estimées) — `av_type_supported` n'annonce de toute façon
  pas plus de 1,5 fois 854 × 480 (720p). Sous QEMU (TCG) : 21,5 images/s en VP9 480p ; Jet y joue les clips de
  `video.html` jusqu'au bout avec le son, l'image dans la page.
* **Onglets** : un WKContext par onglet, donc **un process web par onglet** (trois onglets : trois pids) ; un `kill`
  ferme seulement son onglet ; un changement de process de WebKit (autre site) garde l'onglet.
* **Cookies** : `SD0:/home/jet/webkit/cookies.db` ; sur le ZBook `SD0:` est la partition KORIN (FAT32) : la base
  SQLite (journal de retour) y tient ; vérifié sous QEMU après extinction et redémarrage.
* Taille : `apps/jet.app/main` 126 417 080 → **127 334 648** octets (+0,9 Mo : les codecs 0,8, les onglets, la
  recherche, les cookies) ; avec `TESTS=1`, `bin/avplay` (1,3 Mo) et `webkit/clips` (3,1 Mo) en plus.

* **14/11** (retours du ZBook, détail dans WEBKIT.md, « Jet sur le ZBook : retours du 14/11/2026 ») : onglet neuf
  à l'adresse vide ; plus de requêtes au journal sans `--trace-net` ; la vidéo ne fige plus la page (repeints
  limités à ce que la page suit) et `--trace-media` ; **le JIT de WebAssembly (BBQ, OMG)** sur le PC : 51 fois
  l'interpréteur sous QEMU (hCaptcha de claude.ai : sa preuve de travail en WebAssembly).

* **14/11, suite** : **le compositeur de Jet sur le GPU Intel** (gpucomp sur OpenGL ES par GlKit,
  `tools/webkit/glue/gpucomp_gl.c`), par défaut quand le service `intelgpu` répond ; la vidéo en couche ; YUV → RGB
  en SSE2 et décodeurs sur 4 fils (VP9 720p 147 images/s, 1080p 61, AV1 1080p 60 en natif). Détail et mesure sur le
  ZBook : WEBKIT.md, « Le compositeur sur le GPU du PC ».

Arbre du PC à refaire (WSL, depuis la racine du dépôt, dans cet ordre ; le dernier en dernier) :

```
ARCH=x86_64 sh tools/mesa/build-webkit.sh       # les patchs 0004, 0005 (fetch.sh) et le JIT de WebAssembly
ARCH=x86_64 sh tools/webkit/build-media.sh && ARCH=x86_64 sh tools/webkit/build-jet.sh
ARCH=x86_64 INTERP=ftlwasm sh tools/webkit/build-jsc.sh install   # (TESTS=1 seulement : jsc et wasmtest.sh)
~/jetav-venv/bin/python -I tools/webkit/mkclips.py ~/korin-webkit-x86_64/clips   # (TESTS=1 seulement)
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_apps.sh && sh tools/pc/sysroot_webkit.sh && sh tools/pc/sysroot_gcc.sh
```

## git et rg sur le PC (13/11)

git 2.51.0 (avec libcurl, OpenSSL, zlib : HTTP et HTTPS) et rg (ripgrep réécrit en C sur PCRE2), **les mêmes que le
Pi** ([GIT.md](../GIT.md), [RG.md](../RG.md)), construits pour Korin PC x86-64 ; Vibe et les autres outils peuvent s'en
servir comme sur le Pi (`/bin/git`, `/bin/rg`). Rien ne change pour le Pi.

### Construire (WSL)

```
ARCH=x86_64 sh tools/posix/build.sh   # libposix du PC, d'abord
ARCH=x86_64 sh tools/git/build.sh     # build/pc/git/sdroot (OpenSSL du PC : build/pc/thirdparty/openssl) : ≈ 4 min
ARCH=x86_64 sh tools/rg/build.sh      # build/pc/rg/rg (PCRE2 10.46 avec le JIT x86-64)
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_git.sh   # (avant sysroot_gcc.sh, qui reste le dernier)
```

| Fichier | Ce qui change |
|---|---|
| `tools/git/build.sh`, `config.mak` | `ARCH=x86_64` : chaîne `x86_64-onyx-elf`, libposix du PC, sans `-mcpu`, `uname_M=x86_64`, OpenSSL 3.5.9 du PC (celui des apps et de Jet) ; `config.mak` reçoit `@TC@`, `@CPU@`, `@SSL@` (pour le Pi : le même fichier qu'avant, comparé) ; archives aussi dans `~/.cache/korin-src/git-deps` |
| `tools/rg/build.sh` | `ARCH=x86_64` : `build/pc/rg/rg` ; archive aussi dans `~/.cache/korin-src/rg-deps` (PCRE2 10.46 de GitHub, la source amont). **Pi** : `prebuilt/rg` refait par l'ancien et le nouveau script, identique à l'octet |
| `tools/rg/cardtest.py` | `--dest` : l'arborescence d'essai et `rgtest.sh` dans un autre arbre (celui du PC) |
| `tools/pc/sysroot_git.sh` (nouveau) | dans l'arbre : `bin/git`, `bin/git-core/` (`git-remote-http(s)`, scripts), `etc/gitconfig`, `etc/git-core/templates/`, `etc/ca-bundle` s'il manque (https), `bin/rg` ; ~~`protocol.version = 0`~~ (retiré le 14/11 : « Réveils perdus ») ; refuse un programme qui n'est pas x86-64 |

Rien à changer au noyau du PC ni à libposix : les retouches de git (volumes, `X_OK`, transport local) valent telles
quelles (`__armpi__` des specs du PC).

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, NVMe, `mkkorindisk.py --layout zbook --korin-mb 400`, copie de `build/pc/sysroot` sans `gcc/` ni Jet + `sysroot_git.sh`, bureau, `SD0:/bin/bash SD0:/t/gittest.sh` par `startup.txt`, `usb-net` en réseau user)

* **git, 80 essais sur 80** (avec le protocole 0 ; sortie relue dans l'image par `mtype`), sur RAM0: et SD1: : `init`, `add -A`,
  `.gitignore`, `status`, `commit`, `log`, `diff`, `branch`, `switch`, fusion « ort », `log --graph --all`, `tag -a`,
  `show --stat`, `stash` / `pop`, `checkout --`, `mv`, `rm`, `gc`, `fsck`, `count-objects` (≈ 20 s par volume sous
  TCG) ; `config --global` ; `clone` local SD1: → RAM0:.
* **HTTP** (`tools/git/serve.py` dans WSL, `http://10.0.2.2:<port>`) : `ls-remote`, `clone` intelligent (2 s), `clone`
  simple (`/dumb/`), `push` (branche existante et nouvelle branche), `fetch`, `pull`.
* **HTTPS** : `git clone --depth 1 https://github.com/octocat/Hello-World.git` en 3 s, certificat vérifié par
  `etc/ca-bundle` ; un serveur HTTP joint en `https://` : « wrong version number », code 128.
* **rg** (`cardtest.py --dest build/pc/gittest/sysroot/t/rg` : `src/` de Korin, 2 553 fichiers, un dépôt git, des
  binaires) : **26 appels sur 26** identiques aux sorties attendues (ici celles du rg de l'hôte : WSL n'a pas
  ripgrep ; difftest.py le compare au vrai ripgrep), plus entrée standard, chemin implicite, arbre sur RAM0:, `--files` ;
  « JIT is available » (le `mmap` exécutable de libposix du PC). Grep `-l` sur `src/` : 5 à 7 s sous TCG.
* **Clone HTTP en protocole 2 : arrêté** (5 fois sur 5 dans un script, SD1: comme RAM0:) : `git-remote-http` fini
  (code 0), `index-pack` attend la fin du paquet, `git clone` attend (`sysstat` : `svc_call`, 1 appel/s) ; tué, `index-pack`
  dit « early EOF ». Jamais avec `GIT_TRACE`, **jamais en protocole 0**. En protocole 2, `git clone` démultiplexe lui-même
  la bande latérale dans un thread (`start_async`) vers `index-pack` ; en protocole 0, c'est `git-remote-http`. Un essai
  qui imite ce schéma (fils écrivain, thread qui recopie, fils lecteur, `waitpid` pendant ce temps) passe : **cause non
  trouvée** (tubes, threads, `poll` du noyau ou de libposix du PC). Contournement : `sysroot_git.sh` met
  `protocol.version = 0` dans l'`etc/gitconfig` du PC ; tous les essais ci-dessus passent avec lui.
  **Cause trouvée et corrigée le 14/11** (futex du noyau après un fork, « Réveils perdus » plus bas) ; le
  contournement est retiré.

**Taille ajoutée à l'arbre** : 18,5 Mo (`bin/git` 4,1 Mo, `git-remote-http` et `git-remote-https` 7,2 Mo chacun : pas de
liens sur FAT, `rg` 0,85 Mo, scripts, modèle, `gitconfig`).

### Pas vérifié, reste

* Le vrai ZBook (Wi-Fi, https vers GitHub, un vrai dépôt de milliers de fichiers) ; `korin.ps1 install` (il copie
  `bin/` et `etc/` : git et rg y partent).
* Pas de paquet `git` ni `rg` pour le PC (`tools/pkg`) ; `sysroot_git.sh` n'est pas appelé par `sysroot.sh`.
* Les mêmes limites que sur le Pi (GIT.md « Limites ») ; un éditeur (`core.editor`) et `less` comme pageur.

## Python et Vibe pour le PC (13/11)

CPython 3.14.10, ses modules C (tree-sitter, cffi, pyyaml) et Rust (pydantic-core, rpds-py, cryptography, le harness
de Vibe), Mistral Vibe 3.1.2 (`vibe-acp`, `vibe-app-server`), le client terminal `agent` et l'app `vibe.app`, pour
Korin PC, **des mêmes sources et scripts que le Pi** ([PYTHON-PORT.md](../PYTHON-PORT.md),
[PYTHON-SUITE.md](../PYTHON-SUITE.md)), avec `ARCH=x86_64`. Et, pour le Pi comme pour le PC, les **comptes de
fournisseurs** (Mistral, **Kimi de Moonshot AI**, toute API compatible OpenAI) et le **choix du modèle à chaque
séance** (PYTHON-PORT.md, « Comptes et choix du modèle »).

### Construire (WSL, depuis la racine du dépôt ; ≈ 25 min la première fois sur 16 cœurs)

```
ARCH=x86_64 sh tools/posix/build.sh                 # libposix du PC (l'arbre POSIX, dlopen)
ARCH=x86_64 sh tools/openssl/build.sh               # build/pc/thirdparty/openssl, s'il manque
ARCH=x86_64 SKIP_TESTS=1 bash tools/rust/build.sh   # la cible x86_64-unknown-korin (~/.cache/korin-rust-x86_64)
ARCH=x86_64 sh tools/python/build.sh                # CPython : build/pc/python (≈ 5 min)
ARCH=x86_64 bash tools/python/site-packages.sh      # modules C et Rust, Vibe, pip... (≈ 15 min ; NO_RUST=1 : l'étape 3 seule)
ARCH=x86_64 sh tools/agent/build.sh                 # build/pc/agent/sdroot/bin/agent
sh tools/pc/apps.sh vibe                            # build/pc/apps/sdroot/apps/vibe.app
ARCH=x86_64 sh tools/bash/build.sh                  # bash relié à cette libposix (HOME=/sd0)
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_apps.sh && sh tools/pc/sysroot_python.sh   # TESTS=1 : les essais
```

| Fichier | Ce qui change |
|---|---|
| `tools/python/arch.sh` (nouveau) | les variables de la machine, lues par `build.sh`, `build-ext.sh`, `build-rust-ext.sh`, `site-packages.sh`, `tools/vibe/build-korin.sh` : chaîne, libposix, OpenSSL, `-mcpu`, `--host` (`x86_64-unknown-linux-gnu`), étiquette des roues (`linux_x86_64`), cible Rust ; travail dans `~/.cache/korin-python-x86_64` et `~/.cache/korin-vibe-x86_64`, sortie dans `build/pc/python`. Sans `ARCH` : le Pi, inchangé |
| `tools/python/build-rust-ext.sh` | la cible et la roue de la machine ; la version de `getrandom` prise dans `vendor/` de la chaîne |
| `tools/rust/build.sh`, `getrandom.patch` | getrandom 0.3.4 (repris de main) : le harness de Vibe (jsonschema 0.49.1) le demande |
| `korin.patch` de CPython | rien : `get_platform()` rend `linux-x86_64` de lui-même (`uname -m` de libposix) |
| `src/lib/posix`, `src/svc/vfs` (C et asm), `src/bin/sh`, `paths.txt` | **l'arbre POSIX des chemins** de main (étape 6 : `/sd0`, `/sd1`, `/ram0`, `getcwd` en forme POSIX, `HOME=/sd0`) ; `VFS_PATHLIST` (op 25) écrit en C pour le vfs du PC |
| `tools/agent/build.sh`, `tools/pc/apps.sh`, `tools/office/build.sh` | `agent` pour x86-64 ; `vibe.app` (libacp compilée avec l'app) pour le PC et le Pi |
| `src/apps/vibe/main.c`, compétences de `tools/vibe/home` | les noms anglais de l'UIKit de la branche (`TJ_DARK`, `ui_tokens`...) ; l'assistant de comptes, le choix du modèle, `--account` |
| `tools/pc/sysroot_python.sh` (nouveau) | dans l'arbre : `bin/python3`, `python/`, `bin/vibe-acp`, `vibe-app-server`, `vibe-account`, `agent`, `apps/vibe.app` (et son icône), `.vibe/` (AGENTS.md et compétences, `tools/vibe/home.py`) ; `TESTS=1` : `Lib/test`, modules d'essai, `home/pytest` |
| `tools/python/pctest.py` (nouveau) | essais sous QEMU q35 : une copie de `build/pc/sysroot` + `sysroot_python.sh`, telnetd, disque de `mkkorindisk.py --layout zbook --cfg "default = korin..."` ; commandes par telnet (`hostfwd` de `usb-net`), `shot:` et `sleep:` |

### Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, 6 Go, NVMe, `pctest.py prep --mb 1400`, 13/11)

| Essai | Résultat |
|---|---|
| `posixtest tree` (libposix et vfs du PC) | **23 / 23** (`getcwd` → `/sd0`, `/x` hors du volume courant, `realpath`, `HOME`) |
| `step2.py` (CPython : stdlib, ssl, sqlite, zlib, ctypes, threads, sous-process, asyncio...) | **16 OK**, 1 sauté (`--net` : joindrait api.mistral.ai) |
| `step3.py --gcc` (modules C en `.so`, pip) | **9 OK**, 1 sauté (`--net`) : tree-sitter, cffi (fermetures de libffi), `pip list` (104 paquets), **`pip install mmh3` compilé par le GCC de l'OS** (66 s) |
| `step5.py` (modules Rust : pydantic-core, rpds, cryptography, joserfc, une panique Rust) | **12 / 12** |
| `step6.py` (arbre POSIX vu de Python, harness `_native` 10 Mo en cdylib, `vibe-acp` et `vibe-app-server` répondent à `initialize`) | **12 / 12** ; `initialize` de vibe-acp en 57 s, de vibe-app-server en 47 s |
| `tools/vibe/accounttest.py --agent` contre `tools/vibe/fakellm.py` (le faux fournisseur, sur le PC en `10.0.2.2:8099`) | **17 / 17** : `vibe-account` (add, set-key par l'entrée standard, models, add-model, use, list, une clé refusée → 401 sans l'afficher) ; `vibe-acp` : `session/new` **sur ce fournisseur sans compte Mistral**, les modèles de la séance (`fake-model-1`, `fake-model-2`, `mistral-medium-3.5`), `set_config_option model`, un tour : « Réponse du faux modèle fake-model-2 » ; **`agent`** dans un pty : la liste des modèles à la nouvelle séance, « 2 », un tour servi par `fake-model-2` |
| `vibe.app` sur le bureau (captures `build/pc/pytest/vibe_model2.png`, `vibe_account.png`) | la séance s'ouvre sur `fake-model-1` (50 s), le dialogue **« Modèle de cette séance »** paraît de lui-même ; `--account` : l'**assistant** (fournisseur, note, nom, adresse, clé masquée, « Page des clés (Jet) », modèle, « Lister ») ; Givre |

Aucun vrai fournisseur n'a été joint, aucune clé réelle saisie (la clé d'essai est celle du faux serveur).

**Taille ajoutée à l'arbre** (`sysroot_python.sh`) : ≈ 186 Mo : `bin/python3` 18 Mo, `python314.zip` 7,2 Mo, en-têtes
2,5 Mo, `site-packages` 155 Mo (dont le harness de Vibe 17 Mo, cryptography 4,9 Mo, pydantic-core 5,1 Mo) ;
`vibe.app` 0,3 Mo, `agent` 0,2 Mo, `.vibe` 0,4 Mo. Avec `TESTS=1` : 223 Mo de plus (`Lib/test`, pytest...).

### Poser le tout dans l'arbre du PC

```
mkdir build/pc.lock && { sh tools/pc/sysroot_python.sh; rmdir build/pc.lock; }    # (WSL, après les constructions)
```
puis `tools/pc/korin.ps1 install` comme d'habitude (par l'utilisateur). Au premier lancement : `vibe` au lanceur ou
`agent` au Terminal ; **Vibe → Ajouter un compte...** (ou `vibe-account wizard`, ou `agent` puis `/account`) :
Kimi (Moonshot AI), « Page des clés (Jet) », créer une clé, la coller, « Lister », choisir le modèle Kimi, Ajouter.

### Pas vérifié, reste

* L'assistant **à la souris** (Lister, Ajouter, la relance de vibe-acp) : seulement affiché ; ses étapes sont celles de
  `vibe-account`, essayées. Le vrai Moonshot (adresses `api.moonshot.ai/v1` et la page des clés : à confirmer par
  l'utilisateur), le vrai ZBook, Jet ouvrant la page des clés.
* La séance complète avec un vrai modèle (outils, approbations, `git` et `rg` de l'arbre du PC) : comme l'étape 7 du Pi.
* **RamFS refuse un `rename` d'un dossier à un autre** (`EINVAL`) : Vibe en fait dans `$VIBE_HOME/logs` ; un
  `VIBE_HOME` dans `/tmp` ne marche donc pas (sur `/sd0`, si). À corriger dans le service ramfs.
* FAT ne garde pas les droits : `.env` est demandé en 600, `stat` rend 644.
* Pas de paquet `python` ni `vibe` pour le PC (`tools/pkg`) ; `/gcc` de l'arbre garde l'ancienne libposix tant que
  `sysroot_gcc.sh` (`STAGES=posix`) n'est pas refait ; les autres programmes reliés avant l'arbre POSIX gardent
  l'ancien `getcwd`.
* Vitesse (étape 11) : `initialize` ~ 1 min sous QEMU.

## Retour du ZBook : Vibe reste à l'ouverture de la séance (14/11)

`vibe.app` démarre, le compte s'ajoute, puis la barre d'état reste à l'ouverture de la séance et Envoyer ne fait rien ;
`D:\.vibe\logs\vibe.log` vide, pas de `config.toml`. Détail dans [PYTHON-PORT.md](../PYTHON-PORT.md), « Diagnostic ».

* **Le compte** est écrit au bon endroit : `HOME=/sd0`, `$VIBE_HOME` = `/sd0/.vibe` = `D:\.vibe` pour l'app,
  `vibe-account` et vibe-acp. Sans `config.toml`, c'est un compte **Mistral** (clé dans `.env` seulement, ou connexion
  par le navigateur) : Vibe connaît déjà ce fournisseur.
* **La cause n'est pas trouvée** : sous QEMU q35 à 16 cœurs, faux fournisseur en HTTPS, `session/new` puis un tour
  passent, et les réveils entre fils (tube et `poll`, asyncio, sous-process) ne perdent rien. Différences avec le
  ZBook : le compte Mistral (à l'ouverture d'une séance, Vibe va chercher sur le réseau le catalogue des modèles et
  les réglages de GrowthBook, avec délai), le vrai réseau (Wi-Fi, DNS, TLS de Python, jamais essayé sur le ZBook),
  16 vrais cœurs. Le bogue de réveil de `poll` entre fils (git protocole 2) en est un bon candidat.
* **Rendu visible** : journaux `D:\.vibe\logs\server.log` (sortie d'erreur de vibe-acp) et `vibe.log` (niveau INFO),
  la requête qui attend et depuis combien de temps dans la barre d'état, au-delà de 180 s un dialogue « Relancer
  Vibe ? », menu Vibe : Relancer Vibe.
* **`sys.executable`** d'un script `#!` (vibe-acp, vibe-app-server) valait `/sd0/SD0:/bin/python3` : corrigé dans
  `korin.patch` (`/bin/python3`), step6 12/12 avec ce `python3`.
* Vu une fois sous QEMU (4 cœurs, step6 en fond) : un `sleep 400` de bash qui ne s'est jamais terminé
  (`build/pc/pytest/sleep_hang.log`) ; pas reproduit ensuite. Peut-être le même réveil perdu.
* **Sous QEMU à 8 cœurs**, une fois sur deux : `PANIQUE : pmm_free : page 2623000 déjà libre ou hors carte` au
  démarrage, juste après telnetd (`build/pc/pytest/panic_smp8.log`).

### Marche à suivre sur le ZBook (par l'utilisateur)

Après `wsl sh tools/pc/sysroot_python.sh` (il pose `bin/vibe-diag`) et `tools\pc\korin.ps1 install` :

1. Au Terminal : `vibe-diag --net --prompt bonjour` (≈ 5 min au plus ; sans `--prompt`, aucun tour n'est envoyé au
   fournisseur ; `--net` ouvre seulement une connexion TLS vers lui). Le rapport est **`D:\vibe-debug.txt`** ; les
   valeurs de `.env` y sont masquées.
2. Pour l'app : `SD0:/apps/vibe.app/main --debug`, attendre le dialogue ou l'ouverture, puis rapporter
   `D:\.vibe\logs\server.log` et `D:\.vibe\logs\vibe.log`. Pour le client terminal : `agent --debug`.

## Réveils perdus, courses SMP, AVX et fréquence du processeur (14/11)

Retours du ZBook (Vibe bloqué à « démarrage de la session », YouTube qui se fige dans Jet) et, sous QEMU,
`git clone` HTTP en protocole 2 arrêté 5 fois sur 5 (« git et rg sur le PC »). Cherché sous QEMU q35 (TCG,
`-cpu max`), 4, 8 et 16 cœurs, par un banc qui reproduit ces motifs (`wakebench`, plus bas).

### La cause du blocage de git : un réveil perdu des futex après un fork

Instrumenté au moment du blocage (`V_PIPE_DUMP` de vfs : chaque tube, ses bouts, les pid qui les tiennent,
les lectures en attente ; et l'état des threads du noyau) :

```
vfs : tube 3 r=1 w=0 avail=14 bouts : [5 R pid 31 35]       sortie de git-remote-http (fini) : 14 octets jamais lus
vfs : tube 4 r=1 w=1 avail=0 bouts : [4 R pid 35] [7 W pid 31]   entrée d'index-pack : attend
vfs : tube 7 r=1 w=1 avail=0 bouts : [10 R pid 31] [11 W pid 35] sortie d'index-pack : git clone attend
thread du démultiplexeur (git clone) : bloqué dans FUTEX_WAIT (valeur 2 : verrou disputé), sans délai
```

Les tubes étaient justes ; le thread qui recopie la bande latérale dormait pour toujours sur un verrou.
**Le noyau du PC prenait comme clé d'un futex l'adresse physique du mot** (pour la mémoire partagée entre
process). Après un fork, les pages du père sont en copie sur écriture : la première écriture du père (ici
`pthread_mutex_unlock` dans les fonctions d'après-fork de libposix, qui rendent les verrous pris avant le
fork) lui donne une **autre page physique** ; le thread qui attendait sur l'ancienne n'est plus trouvé par
`FUTEX_WAKE`. git en protocole 2 fork index-pack pendant que son thread démultiplexeur attend un de ces
verrous (malloc, stdio) : 5 fois sur 5 ; jamais avec `GIT_TRACE` ni en protocole 0 (autre minutage, pas de
thread). Le Pi n'a pas ce défaut : sa clé est (process, adresse virtuelle) (`threads.asm`).

**Correction** (`src/kernel/core/thread.c`) : clé (espace d'adresses, adresse virtuelle) pour un mot privé,
adresse physique seulement pour une page `VM_SHARED` (mémoire partagée, la même pour tous). Le
contournement `protocol.version = 0` est retiré de `tools/pc/sysroot_git.sh`.

### Les autres courses trouvées par le banc (noyau du PC, sauf la première : libposix)

| Défaut | Effet | Correction |
|---|---|---|
| fork d'un process à threads : la description d'un descripteur garde, dans le fils, la référence d'un thread du père qui lisait ou écrivait | le `close` du fils (ou FD_CLOEXEC à l'exec) ne rend jamais le bout de tube à vfs : un programme lancé ensuite garde l'écrivain du tube de sa propre entrée, **jamais de fin** (git, subprocess de Python avec des threads) | `src/lib/posix/fd.c` : liste de toutes les descriptions ; dans le fils, références recomptées d'après la table, verrous remis à zéro, orphelines rendues |
| deux threads écrivent la même page de programme juste après un fork | le second, qui attendait le verrou pendant la copie du premier, ne trouvait pas de région (pages d'un ELF) : **SIGSEGV** | `mm_fault` : page déjà là avec les droits voulus -> 0 |
| fork : pages mises en lecture seule, TLB des autres cœurs vidés **après** le verrou | un autre thread du père écrivait encore, par son ancienne traduction, dans la page laissée au fils pendant qu'un troisième faisait la copie : écriture perdue, **tas de malloc abîmé** (plantages dans `free` sous 8 et 16 cœurs) | `mm_fork` vide les TLB sous le verrou |
| copie sur écriture : seul le TLB du cœur courant est vidé | les autres threads du process lisent encore l'ancienne page (lecture seule) : tas lu de travers | `mm_fault` : invalidation sur tous les cœurs (process à plusieurs threads), l'ancienne page rendue après |
| invalidation des TLB sous un verrou | un cœur qui attend un verrou les interruptions coupées ne répond pas à l'IPI : étreinte fatale (c'est pourquoi tout se faisait après les verrous) | `arch_cpu_relax` et l'attente de `shoot_lock` répondent aux invalidations (drapeau par cœur, `smp_tlb_poll`) |
| `SVC_MAP_CALLER` : vue d'1 Mo qui ne commence pas au début d'une page | 257 pages pour 256 places, la dernière sur la vue suivante (traduction écrasée, épingle rendue deux fois) | 257 places, vues espacées de 2 Mo |
| `SVC_MAP_CALLER` : traduction lue et page épinglée sans le verrou de l'appelant | une copie sur écriture et la fin d'un fils entre les deux : épingle sur une page libre | `mm_pin_page` sous le verrou |
| vue d'un service retirée : épingles rendues avant l'invalidation des TLB | un autre thread du service pouvait écrire dans une page déjà rendue | `unmap_one` : traductions, invalidation, puis épingles |
| fork : le fils ne tenait pas l'image du père | fin ou exec du père : une image passagère et ses pages de code rendues pendant que le fils les exécutait | `image_hold` dans `sys_fork` |
| signal d'un process : le premier thread de la liste (le plus récent) réveillé, après avoir rendu le verrou | un thread auxiliaire qui ne masquait pas SIGCHLD prenait le signal, le principal restait dans `sigsuspend` ; et le thread pouvait être rendu entre-temps | d'abord le thread qui attend (sigwait, sigsuspend), puis le plus petit tid ; réveil sous le verrou (aussi la boucle d'exec) |
| trames de signal dans des tampons statiques par cœur | l'appel est préemptible : deux trames sur le même cœur se mélangeaient | tampons du tas |

**La double libération de page (trouvée le 14/11 au soir, antérieure à ces corrections)** :
`PANIQUE : pmm_free : page … déjà libre`, que le banc faisait sous 4 cœurs avec `forklock` et `waitthr` (une
fois en 10 à 40 minutes), aussi avec le noyau d'avant (et le seul correctif des futex), et que l'agent de Vibe a
vue au démarrage sous 8 cœurs. **Cause** : `kfree` d'un gros bloc relisait `b->pages` (l'en-tête, dans la
première page du bloc) à chaque tour de la boucle qui rend ses pages ; la première rendue est justement
celle de l'en-tête, qu'un autre cœur pouvait reprendre aussitôt (`pmm_alloc` ne passe pas par le verrou du
tas : une page de programme, mise à zéro ou copiée) ; la boucle rendait alors un nombre de pages faux, et
les pages d'un autre bloc vivant (une pile de thread, une table de poignées d'IPC), plus tard rendues une
seconde fois. Correction : la taille lue avant la boucle (`kheap.c`).

Pour la trouver, et gardées (peu coûteuses) : une **table des gros blocs vivants** dans `kheap.c` (adresse,
taille, pile d'appel de l'allocation ; un bloc rendu deux fois ou dont l'en-tête a changé arrête le noyau
avec les deux piles) ; un **bitmap des pages du tas** dans `pmm.c` (une page du tas rendue ou prise hors de
`kfree`, une page libre prise par `pmm_ref`, une page donnée qui a encore des détenteurs arrêtent le noyau
avec la pile : la première erreur, pas sa conséquence lointaine).

**Vérifié** (noyau final, `wakebench 10` en boucle pendant plus d'une heure sous chaque nombre de cœurs, QEMU
TCG, `-cpu max`, 3 Go) : **4 cœurs : 225 922 process** (≈ 1 650 tours des 9 scénarios), **8 cœurs : 218 585**,
**16 cœurs : 197 971**, sans panique, sans échec ni blocage du banc (la boucle s'arrêtait au premier). Avant la
correction : une panique en 2 à 40 minutes sous 4 cœurs.

**Un blocage complet sous 4 cœurs**, trouvé en même temps : `smp_tlb_shootdown` prenait `shoot_lock` sans
couper les interruptions ; préempté en le tenant, le thread ne revenait plus, les autres cœurs l'attendant
interruptions coupées (sous un autre verrou). Le verrou se prend maintenant interruptions coupées.

### Le banc : `wakebench` (`src/lib/posix/test/wakebench.c`)

`wakebench [N] [scénario…]`, chaque scénario N fois, un chien de garde (`WAKEBENCH_WD`, 30 s) : « BLOQUE »
et code 2 ; `WAKEBENCH_DUMP=1` met l'état des tubes de vfs au journal. Scénarios : `cowfutex` (un thread
attend un verrou que le principal rend après un fork), `forklock` (threads qui prennent un verrou et font
des malloc pendant des fork), `pipefork` (un thread bloqué dans `write` sur un tube plein pendant fork + exec
du lecteur), `pipeeof` (poll d'un tube dont le dernier écrivain se ferme ailleurs), `gitdemux` (le schéma
de git), `sockpair` (socketpair, poll, SCM_RIGHTS, fin), `sigchld` (sigsuspend), `condvar` (ping-pong,
sémaphore), `waitthr` (waitpid dans trois threads). Construit pour le PC par `ARCH=x86_64 sh
tools/posix/build.sh` (`build/pc/posixtest/wakebench`, `avxtest`).

| QEMU, `wakebench 5` par scénario | cowfutex | forklock | pipefork | pipeeof | gitdemux | sockpair | sigchld | condvar | waitthr |
|---|---|---|---|---|---|---|---|---|---|
| noyau et libposix d'avant, 4 cœurs | BLOQUE | BLOQUE | BLOQUE | ok | BLOQUE | ok | BLOQUE | ok | BLOQUE |
| après, 4, 8 et 16 cœurs | ok | ok | ok | ok | ok | ok | ok | ok | ok |

En boucle (`wakebench 10`, 40 fois de suite, ≈ 9 000 process) : **8 et 16 cœurs, 40/40** après les
corrections du TLB (avant : plantages de `free` au 6e et au 25e tour) ; 4 cœurs : la double libération
ci-dessus, une fois.

**git** (`git clone` HTTP en **protocole 2**, `tools/git/serve.py`) : avant, bloqué 3 fois sur 4 ; après le
seul correctif des futex, **10 clones sur 10** (≈ 9 s chacun sous TCG). Voir « Vérifié ».

### AVX : XSAVE dans le noyau du PC

* `src/kernel/arch/x86_64/cpu.c` : si CPUID annonce XSAVE, `CR4.OSXSAVE` et **XCR0 = x87 | SSE | AVX** (7)
  sur chaque cœur ; une zone par thread de la taille donnée par CPUID 0xD (832 octets avec l'AVX), alignée
  sur 64 ; **XSAVEOPT** aux bascules s'il est là (XSAVE complet dans la zone d'un autre : fork, trames),
  XRSTOR ; exec repart d'une zone à l'état initial (YMM compris). Sans XSAVE : FXSAVE comme avant.
* **AVX-512 : éteint par défaut**, option `avx512` de `cmdline` dans `\korin\boot.cfg` pour l'allumer
  (opmask, ZMM : zone de 2,7 Ko). Décision : aucun programme de Korin n'a encore de chemin AVX-512 construit
  (JSC n'en émet pas, Skia est en SSE2 de base, libvpx et dav1d en C générique) ; la zone tripler pour rien,
  et la trame d'un signal (≈ 3,3 Ko) dépasserait MINSIGSTKSZ (2048) : une pile de secours de cette taille
  déborderait. À rallumer par défaut quand dav1d ou Skia auront leur assembleur AVX-512. Option `noxsave` :
  FXSAVE seul (comme avant, pour comparer).
* **Trame des signaux** (`sigframe.c`) : l'état FPU reste à `ucontext + 320` (le pointeur `fpregs` à +224),
  aligné sur 64, en XSAVE avec les marques de Linux (`FP_XSTATE_MAGIC1` dans les octets 464..511 de la zone
  FXSAVE, taille et XCR0 ; `FP_XSTATE_MAGIC2` après la zone) ; au retour, l'en-tête XSAVE est assaini
  (XSTATE_BV dans XCR0, XCOMP_BV à 0 : pas de #GP dans le noyau) ; une trame sans les marques : FXRSTOR.
* JSC (`tools/webkit/patches/0002-x86-avx-osxsave.patch`) lit `CPUID.1:ECX.OSXSAVE` puis `XGETBV` : il
  verra AVX tout seul (vu par `avxtest` : « AVX 1, OSXSAVE 1, AVX2 1 ; XCR0 7 »). Le JIT n'est pas à
  reconstruire ; Skia et Mesa garderont leurs options de construction (SSE2) tant qu'elles ne sont pas
  revues.
* Journal au démarrage : `FPU : XSAVEOPT, XCR0 7 (x87, SSE, AVX), 832 octets par thread`.
* `avxtest [threads] [tours]` (`src/lib/posix/test/avxtest.c`) : chaque thread charge ses 16 YMM, tourne
  sans y toucher pendant que le principal lui envoie des SIGUSR1 dont le gestionnaire écrase les 16 YMM et
  vérifie la trame (alignée sur 64, MAGIC1) ; puis relit. Sous QEMU (`-cpu max`, AVX2 sans AVX-512) :
  **12 threads, 20 tours, OK** (plusieurs centaines de signaux), 4 cœurs.

### Fréquence du processeur : P-states et fréquence effective (`src/kernel/arch/pc/pstate.c`)

* Sur chaque cœur, au démarrage (après l'IDT) : **HWP** si CPUID 6 EAX bit 7 : `IA32_PM_ENABLE` (0x770) = 1,
  `IA32_HWP_REQUEST` (0x774) : minimum = Lowest et maximum = Highest de `IA32_HWP_CAPABILITIES` (0x771),
  Desired = 0 (le processeur choisit), **EPP = 0 (performance)** (bit 10), sinon `IA32_ENERGY_PERF_BIAS` = 0.
  Sans HWP mais avec EIST : EIST et turbo permis (`IA32_MISC_ENABLE`), puis `IA32_PERF_CTL` (0x199) au
  rapport du turbo d'un cœur (`MSR_TURBO_RATIO_LIMIT`, sinon `MSR_PLATFORM_INFO`). Seulement chez Intel.
  Options de `cmdline` : `pstate=balanced` (EPP 0x80), `pstate=off` (rien n'est touché).
* Chaque MSR est lu ou écrit « sûr » : une #GP (MSR absent, QEMU) est rattrapée par `idt.c` (l'instruction
  est sautée, le MSR laissé) ; QEMU n'annonce ni HWP, ni EIST, ni APERF/MPERF : rien n'est touché, le
  journal le dit (`CPU : ni HWP ni EIST…`, `CPU : pas d'APERF/MPERF…`), tout démarre (4, 8, 16 cœurs).
* **Fréquence effective** : APERF/MPERF (0xE8, 0xE7) lus chaque seconde sur chaque cœur (minuterie) ;
  MHz = TSC × ΔAPERF / ΔMPERF (la fréquence moyenne quand le cœur travaille). Au démarrage, le cœur 0
  travaille 50 ms et le journal dit : `CPU : fréquence effective du cœur 0 sous charge : … MHz (TSC … MHz,
  plus haut demandé … MHz)`.
* `CPU_LOAD` (0x7E) : `places | (1 << 32)` rend 32 octets par cœur {%, ms occupées, MHz effectifs, MHz les
  plus hauts demandés} (16 octets comme avant sans ce bit).
* Commande **`cpufreq`** (`src/bin/cpufreq`, korinrt) : une ligne par cœur (MHz effectifs de la dernière
  seconde, charge) ; `cpufreq charge` : d'abord 2 s de calcul sur un cœur (le turbo d'un cœur). Le Pi :
  « pas de mesure » (CPU_LOAD absent).
* **Pas vérifié sur le vrai matériel** : QEMU n'émule pas HWP ; la vérification est à faire sur le ZBook.

### Vérifié (QEMU q35 + OVMF, TCG, `-cpu max`, NVMe, arbre de `build/pc/sysroot` sans `gcc/` ni `python/`, avec git, Jet et les essais ; noyau final de `tools/pc/build.sh`)

* `wakebench 20` (les 9 scénarios) : **4, 8 et 16 cœurs, tout OK** ; en boucle 40 fois (`wakebench 10`) :
  8 cœurs 40/40, 16 cœurs sans faute de `free` (voir ci-dessus pour 4 cœurs).
* `avxtest 12 20` : **OK** sous 8 et 16 cœurs (1 470 et 2 408 signaux pendant que les threads gardaient
  leurs YMM), `XCR0 7` ; journal `FPU : XSAVEOPT, XCR0 7 (x87, SSE, AVX), 832 octets par thread`.
* `cpufreq`, `cpufreq charge` : une ligne par cœur, « ? MHz » (QEMU n'a pas d'APERF/MPERF), la charge ;
  journal `CPU : pas d'APERF/MPERF…` ; démarrage normal à 4, 8 et 16 cœurs (aucun MSR touché).
* `posixtest fork thread signal io proc ipc` : **103 / 1**, l'échec est l'écart connu (`mprotect` d'une
  projection privée d'objet partagé, « Écarts connus » plus haut) ; `mem` : les deux écarts connus.
  **Corrigé le 14/11 au soir** (`mprotect` des projections, plus bas) : `posixtest mem ipc fork thread signal io
  proc file` **216 / 0** (2 sautés).
* **git, essais de l'agent git en protocole 2** (git relié à la nouvelle libposix, `etc/gitconfig` sans
  `protocol.version`) : **78 / 80**, les deux restes ne sont pas de git : `commit` « nothing to commit » (le
  dépôt du serveur, copié de celui de l'agent git, avait déjà ce commit) et `rgtest.sh` (pas copié dans
  cet arbre) ; `clone` HTTP intelligent en 3 s, `push`, `fetch`, `pull`, `clone` https de GitHub en 6 s ;
  `clone.sh 5` sous 16 cœurs : 5 sur 5.
* Le Pi : libposix construite pour AArch64 (dans un autre dossier, `NO_TESTS=1`) sans erreur ; `prebuilt/`
  pas touché ; le noyau du Pi (asm) ne change pas ; vfs en C reçoit `V_PIPE_DUMP` (inutile au Pi, sans
  effet sinon) ; `tools/check_constants.py` passe.

### À faire sur le ZBook (par l'utilisateur, après `korin.ps1 install`)

1. `journal`, puis dans `D:\journal.txt` les lignes `FPU :` (attendu : `XSAVEOPT, XCR0 7 (x87, SSE, AVX), 832
   octets`) et `CPU :` (attendu : `HWP actif, performances … à 46 …, EPP 0 (performance)` et une fréquence
   effective proche de 4 600 MHz sous charge).
2. Au Terminal : `cpufreq`, puis `cpufreq charge` (attendu : un cœur vers 4 600 MHz ; tous les cœurs
   chargés : 4 200 environ). Si la machine chauffe trop ou que la batterie fond : `cmdline = pstate=balanced`
   dans `\korin\boot.cfg`.
3. `avxtest` (dans l'arbre d'essai, `bin/avxtest`) : `AVX 1, OSXSAVE 1, AVX2 1, AVX-512F 1 ; XCR0 7`, puis
   OK.
4. `git clone https://github.com/octocat/Hello-World.git SD1:/hw` (protocole 2 désormais) ; Vibe et YouTube
   dans Jet comme avant (les blocages de threads après un fork sont corrigés ; dire si l'un reste).

### `mprotect` d'une projection de fichier ou de mémoire partagée (14/11, soir)

L'écart connu (« Écarts connus » de la chaîne x86-64), que le dlopen de Rust a dû contourner et dont la
couche Linux aura besoin : `VM_PROTECT` n'acceptait que les régions anonymes. Il accepte maintenant aussi les
projections d'un objet partagé (`mm.c`, `covered (…, 2)`) : les pages en copie sur écriture le restent (une
écriture copiera : une projection privée ne change jamais l'objet), celles de l'objet changent de droits sur
place ; `PROT_NONE` sur une page déjà copiée d'une projection privée est refusé (`EINVAL` : x86 n'a pas de
page présente sans accès, elle partirait avec ses écritures). `posixtest mem` et `ipc` passent en entier
(« mprotect RW on a shared read-only page », « a page read, then mprotect RX », « code run from the shared
pages of a file », « mmap MAP_PRIVATE then mprotect: the object does not change ») ; l'essai d'exécution
depuis un fichier avait le code d'AArch64 : le PC a maintenant le sien (`mov eax, 42 ; ret`), le Pi garde le
sien.

### Dans l'arbre du PC

`wakebench` et `avxtest` sont construits dans `build/pc/posixtest` (`ARCH=x86_64 sh tools/posix/build.sh`),
que `tools/pc/sysroot_apps.sh` copie dans `bin/` ; `cpufreq` est une commande en C (`build/bin/x86_64`), que
`tools/pc/sysroot.sh` copie dans `bin/` et que l'initrd contient. Posés aussi dans `build/pc/sysroot/bin`
(14/11) : `korin.ps1 install` les copie sur D:.

### Reste

* Fait le 14/11 au soir : la double libération de page (voir plus haut).
* `SVC_MAP_CALLER` du PC : une page épinglée par un service reste partagée par un `fork` de l'appelant (un
  autre thread) ; une écriture de l'appelant la copie et le service écrit dans l'ancienne. Le Pi a `PTE_PIN`
  (`mapcaller.asm` : fork recopie une page épinglée) ; à reprendre pour le PC.
* `sleep 400` jamais revenu une fois chez l'agent de Vibe (pas reproduit) : `sys_wait` revoit toutes les
  20 ms, `sched_sleep` ne dépend que de la minuterie du cœur 0 ; rien trouvé.
* Vibe sur le ZBook et YouTube dans Jet : à réessayer avec ce noyau et cette libposix (asyncio et WebKit
  font des fork / spawn depuis des process à threads : les défauts des futex et des descripteurs les
  touchaient).
* AVX-512 par défaut (quand un programme s'en servira) ; Skia et libvpx/dav1d avec leur SIMD x86.

## Retour du ZBook : la connexion au compte Mistral expire aussitôt (14/11)

Connexion accordée dans Jet, mais Vibe répondait `Browser sign-in timed out.`, puis libacp bouclait sur
`Unknown browser sign-in attempt`. Détail dans [PYTHON-PORT.md](../PYTHON-PORT.md), « Diagnostic ».

* **Cause : l'horloge.** Le noyau lit la CMOS comme UTC, Windows l'écrit à l'heure locale : Korin avait une heure
  d'avance (deux l'été), et `CLOCK_SET` ne reculant jamais, NTP ne la corrigeait pas. **Corrigé** : netd passe l'heure
  NTP avec `0x4E545021` (« heure sûre », troisième argument de `CLOCK_SET`, noyau du PC), qui peut reculer ; vérifié
  sous QEMU avec une CMOS en avance d'une heure (`pctest.py --qemu "-rtc base=..."`). Sans réseau, l'avance reste
  jusqu'au premier NTP (une option `rtc=local` avec le fuseau, à lire avant NTP, serait l'étape suivante).
* **Vibe n'en dépend plus** (`tools/vibe/korin.patch`) : délai de connexion compté en temps monotone d'après la date
  du serveur ; **libacp ne boucle plus** : erreur dite, « Réessayer avec un nouveau lien ? » (`vibe.app`), `/login`
  (`agent`). Essais : `tools/vibe/signintest.py` 11/11 contre `fakellm.py` décalé d'une heure.
* Sous QEMU (TCG), l'horloge de Korin prend du retard sur NTP (≈ 56 s en 2 min) : à surveiller sur le ZBook
  (`vibe-diag --net`).

### À refaire sur le ZBook (par l'utilisateur)

```
wsl sh -c 'ARCH=x86_64 sh tools/agent/build.sh'
wsl sh tools/pc/apps.sh vibe
wsl sh tools/pc/build.sh                       # le noyau (CLOCK_SET) et l'initrd
wsl sh tools/svc/build_c.sh netd
wsl sh tools/pc/sysroot.sh && wsl sh tools/pc/sysroot_apps.sh && wsl sh tools/pc/sysroot_python.sh
tools\pc\korin.ps1 install
```
(`build/pc/python/sdroot` a déjà le Vibe retouché ; `ARCH=x86_64 bash tools/python/site-packages.sh` le refait
depuis `korin.patch`.) Puis : démarrer Korin **avec le Wi-Fi** (NTP corrige l'heure), `vibe-diag --net` au
Terminal (l'écart à NTP et au serveur de Mistral doit être de quelques secondes), puis `vibe.app` et la connexion ;
si elle échoue encore : `vibe-diag --net --prompt bonjour`, et rapporter `D:\vibe-debug.txt`.

## .NET pour le PC (14/11)

.NET 10.0.14 (CoreCLR, RyuJIT x64, la bibliothèque de linux-x64, l'hôte `dotnet`), Roslyn et csbuild dans l'OS, nos
assemblys (kits, WinForms, Studio), Avalonia 12.1.4 et son hôte `dotnet-skia` : **des mêmes scripts que le Pi**, avec
`ARCH=x86_64`. Détail, tableau des écarts et essais : [DOTNET.md](../DOTNET.md), « Le PC (Korin x86-64) ».

### Construire (WSL, depuis la racine du dépôt)

```
ARCH=x86_64 sh tools/dotnet/build.sh
ARCH=x86_64 sh tools/dotnet/stage.sh && ARCH=x86_64 sh tools/dotnet/kits.sh
ARCH=x86_64 sh tools/dotnet/hello.sh check                 # les essais en un seul fichier (TESTS=1)
ARCH=x86_64 sh tools/avalonia/build-skia.sh && ARCH=x86_64 sh tools/avalonia/build-desktop.sh
ARCH=x86_64 sh tools/avalonia/build-host.sh && ARCH=x86_64 sh tools/avalonia/stage.sh
sh tools/pc/build.sh                                       # le noyau (si_code, si_pid)
mkdir build/pc.lock && { sh tools/pc/sysroot_dotnet.sh; rmdir build/pc.lock; }
```
puis `tools\pc\korin.ps1 install` (par l'utilisateur) : `dotnet` est un dossier de l'arbre système (`$sysTop`).

### Noyau du PC (deux commits à part)

* `#DE`, `#UD`, `#GP` : `si_code` (et `si_addr` nul pour `#GP`) de Linux. Sans lui, `DivideByZeroException` arrêtait
  le programme (« Illegal instruction »).
* `si_pid` des signaux envoyés par un process : la PAL ignore une injection d'activation (`SIGRTMIN`, suspension pour
  le GC) qui ne vient pas d'elle ; un GC pendant une boucle de calcul sans appel ne finissait jamais. **Le noyau du Pi
  (asm) a sans doute le même défaut** : à essayer avec `tools/dotnet/stress`.

### Vérifié (QEMU q35 + OVMF, TCG, `-cpu max`, NVMe, `tools/dotnet/pctest.py prep --with-gcc`)

`dotnetcheck` 39 / 39, `nettest` 16 / 16 (https vérifié par `/etc/ca-bundle`), `stress 60` sans échec sous 4, 8 et 16
cœurs, `pibuild` de C# (cshello, un `.csproj` avec bibliothèque, un projet Avalonia avec son XAML), `avdemo --auto`,
fenêtres d'`avdemo`, du Studio WinForms et du Studio Avalonia sur le bureau, Réglages > .NET. `hello.dll` en 4,5 à 7 s.
≈ 155 Mo de plus dans l'arbre.

### Pas vérifié, reste

* Le vrai ZBook (temps de démarrage réels, `cpufreq` pendant le JIT).
* Avalonia sur le GPU Intel : Skia Ganesh construit sur Mesa iris (comme `tools/mesa/build-skia.sh` pour le Pi) et
  `armgl` de GlKit à la place de `armgl_cpu.o` ; GpuKit pour `view3d` et `gpudemo`.
* Pas de paquet `dotnet` pour le PC (`tools/pkg`).
