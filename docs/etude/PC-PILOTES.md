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

### hwmanager : interface prévue (pas changé)

hwmanager est en asm ARM et ne tourne pas sur PC ; sur le Pi il ne recevra jamais `HW_OP_PCI`. Rien
n'y est donc changé. Quand il passera en C, il devra :

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
  Sans ces options, rien ne change.
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

## 4. Pas vérifié, à essayer

Sur le **vrai ZBook** (l'utilisateur ; ne jamais écrire sur le NVMe) :

* `pcid -l` : la MCFG d'HP, les ~30 fonctions, les deux xHCI (`8086:43ED` du PCH, `8086:9A17`
  Thunderbolt 4), le lecteur Realtek `10EC:5261`, l'HDA `8086:43C8`, l'AX201 `8086:43F0` ; le GPU Intel
  doit apparaître sans mesure de BAR (console intacte) ;
* `usbd 0000:00:14.0` (xHCI du PCH, adresse à confirmer par `pcid -l`) : reprise au BIOS (message si le
  BIOS ne rend pas la main), scratchpads (le Tiger Lake en demande), ports à PPC, MSI-X, clé USB et
  clavier USB ;
* un hub USB 3 (les ports du ZBook passent souvent par des hubs internes) : **les hubs SuperSpeed ne
  sont pas gérés** (ni en asm) : un périphérique derrière un hub USB 3 n'est vu que par la moitié
  USB 2 du hub, ce qui marche si le hub l'expose.

Sur le **vrai Pi** (rien à faire tant que la carte garde l'asm) : si un jour `build/svc/aarch64/usbd`
remplace `usbd.asm`, essayer d'abord sur un Pi 4 : lien PCIe, microcode du VL805, clavier et clé sur
les ports USB-A ; mais il manque d'abord le DWC2 et l'isochrone en C (sinon le port USB-C et les cartes
son USB disparaissent).

## 5. Reste

1. **Plusieurs xHCI** : le ZBook en a deux ; hwmanager lancerait deux usbd, et le second ne peut pas
   s'inscrire sous « usb » (il s'arrête). À faire : un seul usbd à plusieurs contrôleurs (le second
   lancement passe son adresse au premier, ou usbd prend tous les xHCI de pcid), poignée = contrôleur
   + slot.
2. hwmanager en C avec `HW_OP_PCI` (§2) ; init2, vfs, busd sur PC, puis la chaîne complète
   init2 → hwmanager → pcid → usbd → usbkeyboard / usbstorage.
3. Services de classe en C (usbkeyboard, usbmouse, usbgamepad : `hiddrv.inc` ; usbstorage ; usbnet ;
   usbaudio) : ils sont en asm ARM ; usbdtest prouve déjà le protocole côté PC.
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
