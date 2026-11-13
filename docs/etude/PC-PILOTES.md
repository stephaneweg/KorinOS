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
   + slot.
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
