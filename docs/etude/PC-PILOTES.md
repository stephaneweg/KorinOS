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

**Le dossier racine d'un volume : un paramètre `rootpath` de plus au montage** (conception de l'utilisateur, 12/11).
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
| `tools/pc/build.sh` | `PC_SVC="a b"` (seuls ces services reconstruits), un service dont le nom est pris par une commande va dans l'initrd en `<nom>.svc` (`echo.svc`), `drivers.txt` dans l'initrd, `init` **pas** dans l'initrd (le noyau du PC le lancerait à la place de hello et il attendrait un SD0: : `PC_INIT=1` pour l'y mettre) |
| `src/kernel/arch/pc/initrd/stest.c` | essai PC : echo, mapsvc, busd (publication, lecture, liste, abonnement `BUS_CURRENT`, `WEV_BUS` par `EV_WAIT`, retrait, erreurs), hwmanager (annonces sans pilote) ; `run stest hid` : abonné de hidmanager, les touches reçues |

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
