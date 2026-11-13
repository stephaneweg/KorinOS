# Touchpad du PC : acpidump et i2chid (13/11)

Le touchpad du HP ZBook Fury G8 (Windows : `ACPI\ELAN0750`, « I2C HID Device ») est un périphérique **HID over
I2C** derrière le contrôleur I2C du **LPSS d'Intel** (`0000:00:15.0`, `8086:43e8`, Tiger Lake-H, classe `0c/80`).
Rien de cela n'existe sous QEMU : le pilote est écrit d'après les spécifications et essayé hors matériel ; le vrai
essai se fait sur le ZBook. Décisions de l'utilisateur (gestes, glisser au double toucher, les 6 boutons) :
[PC-PILOTES.md](PC-PILOTES.md), « Touchpad (décision de l'utilisateur du 12/11) ».

## Ce que l'ACPI doit donner

Korin n'a pas d'interprète AML. Ce qui décrit le touchpad est dans le DSDT ou une SSDT, sous le contrôleur
(`Device (I2C0)`, `_ADR 0x00150000`) :

| Quoi | Où dans l'AML | Réglage d'i2chid |
|---|---|---|
| adresse I2C | `I2cSerialBusV2 (0x0015, …, 400000, …, "\_SB.PCI0.I2C0")` dans le `_CRS` du touchpad (`TPD0`, `TPL1`…), souvent choisi par une variable (`TPTY`, `SDM0`…) entre plusieurs gabarits | `addr` |
| registre du descripteur HID | `_DSM` de HID over I2C (GUID `3cdff6f7-4267-4555-ad05-b30a3d8938de`), fonction 1 : `Return (0x01)` ou une variable | `hid_desc` |
| vitesse du bus | `400000` du même `I2cSerialBusV2` | `speed` (kHz) |
| comptes de SCL | méthodes `FMCN` (400 kHz) / `SSCN` (100 kHz) du contrôleur : `Package { HCNT, LCNT, maintien de SDA }` | `hcnt`, `lcnt`, `sda_hold` |
| interruption | `Interrupt (ResourceConsumer, Level, ActiveLow, …) { 0x5D }` (directe : un GSI de l'IOAPIC), ou `GpioInt (Level, ActiveLow, …, "\_SB.GPI0") { 0x…}` (un plot du GPIO du PCH) | `irq`, ou `gpio` |
| alimentation | `_PS0` / `_PS3`, `_INI`, parfois un plot GPIO de remise à zéro | (pas encore : voir « Reste ») |

Les valeurs du ZBook seront lues dans ses tables, puis écrites dans `SD0:/etc/i2chid.txt` (pas de recompilation).

## acpidump (fait, 13/11)

`acpidump [DOSSIER]` (`src/svc/acpidump`, C, PC seulement) écrit chaque table dans `SD0:/acpi` (défaut), aux noms
d'`acpixtract` : `rsdp.dat`, `xsdt.dat` (ou `rsdt.dat`), `dsdt.dat`, `facs.dat`, `facp.dat`, puis chaque table de la
XSDT dans l'ordre (`ssdt1.dat`, `ssdt2.dat`… quand une signature revient), et `tables.txt` (signature, longueur,
révision, OEM, table de l'OEM, somme de contrôle). Lecture seule : le noyau recopie les tables (`ACPI_TABLE`).

* **Noyau** (`src/kernel/arch/pc/acpi.c`) : `ACPI_TABLE` donne aussi la racine (`"RSDP"`, `"XSDT"`, `"RSDT"`) et,
  avec la signature `"****"`, l'entrée d'un rang de la XSDT quelle que soit sa signature (pour énumérer sans
  connaître les signatures). Toujours réservé aux programmes de `SD0:/sys` ([ABI.md](../ABI.md), `0x4D`).
* **Deux visages, comme `iwl`** : le binaire est dans `SD0:/bin` (sans droits) et dans `SD0:/sys/svc` ; lancé de
  `SD0:/bin`, il relance `SD0:/sys/svc/acpidump` et recopie au Terminal les lignes « acpidump : … » du journal
  (`src/svc/common/sysfront.c`, commun à acpidump et i2chid).
* **Vérifié sous QEMU q35 + OVMF** (disque `mkkorindisk.py --layout zbook`, console texte) : 10 tables (RSDP, XSDT,
  DSDT de 8700 octets, FACS, FACP, APIC, HPET, MCFG, WAET, BGRT), sommes justes ; puis avec deux SSDT ajoutées par
  `-acpitable` (un faux touchpad `SYNA1202` sous `\_SB.PCI0.I2CX`, et une table d'un nom) : `ssdt1.dat` et
  `ssdt2.dat` dans l'ordre de la XSDT, **corps AML identique octet par octet** aux `.aml` compilés,
  `iasl -e ssdt1.dat ssdt2.dat -d dsdt.dat` et `iasl -d ssdt*.dat` sans erreur, le `I2cSerialBusV2` et
  l'`Interrupt` retrouvés dans le `.dsl`. `iasl -d rsdp.dat` refuse la RSDP seule (« not a valid standard ACPI
  table ») : normal, elle n'a pas l'en-tête commun (36 octets, lisible en hexadécimal).

## i2chid (écrit, pas essayé sur le matériel)

`src/svc/i2chid` (C, PC seulement, lancé à la main ; ni hwmanager ni `drivers.txt` pour l'instant) :

| Fichier | Rôle |
|---|---|
| `i2chid.c` | réglages, fonction PCI, HID over I2C, boucle, anneau de hidmanager, service « i2chid » (arrêt) |
| `dw.c` | LPSS (sortie de reset, horloge, `REMAP_ADDR`) et contrôleur DesignWare en sondage |
| `hidrd.c`, `hidrd.h` | descripteur de rapport : éléments, collections, modèle (souris, pavé) ; sans rien du système |
| `gesture.c`, `gesture.h` | du pavé à la souris : mouvement, clics, molette, glisser ; sans rien du système |
| `test/hidrd_test.c`, `test/run.sh` | essai hors matériel (`wsl sh src/svc/i2chid/test/run.sh`) |
| `skel/pc/etc/i2chid.txt` | les réglages, commentés (copiés dans `SD0:/etc` par `sysroot.sh`) |

Commandes : `i2chid probe` (fonction PCI, BAR, LPSS, DesignWare ; rien sur le bus), `i2chid scan` (et les adresses
qui répondent à une lecture d'un octet), `i2chid info [-v]` (descripteur HID, descripteur de rapport — en
hexadécimal avec `-v` —, ce qu'i2chid y comprend), `i2chid run` (le pilote, jusqu'à `i2chid stop`), `-vv` (chaque
rapport lu). Une `clé=valeur` sur la ligne remplace celle du fichier pour une fois (`i2chid info addr=0x2c`).

### Contrôleur

* **Fonction PCI** : la première des I2C du LPSS d'Intel connues (Tiger Lake-H `43e8`…`43eb`, `43ad`, `43ae`,
  Tiger Lake-LP, Alder Lake, Comet Lake, Cannon Lake), ou `pci=`. Prise à pcid (`PCI_OP_CLAIM`), passée en D0
  (PMCSR) si elle dormait.
* **BAR0 à 0** : le journal pcid du ZBook dit `BAR0 mém. 0 (4 Ko), 64 bits` : le micrologiciel n'a pas donné
  d'adresse (Windows et Linux en donnent une eux-mêmes). i2chid en choisit une (`bar = auto`) : un trou de 4 Ko
  entre les petites BAR 64 bits du bus 0 (là où le micrologiciel range celles du PCH), hors de toute BAR et de toute
  fenêtre de pont. D'après le journal du ZBook, ce sera **`0x6053ba6000`** (seul trou entre `0x6053a00000` et
  `0x6053baa000` : `14.2`, `1f.4`, `16.0` autour). Risque : une fonction cachée du PCH (P2SB, PMC) qui y décoderait ;
  pour l'éviter, l'adresse que Windows donne (Gestionnaire de périphériques, « Intel Serial IO I2C Host
  Controller - 43E8 », Ressources) peut être écrite en `bar=`.
* **LPSS** (`BAR + 0x200`, d'après `intel-lpss.c` de Linux) : capacités (`0x2FC`, sorte 0 = I2C), remise à zéro
  puis sortie (`RESETS` = 7), adresse de la BAR dans `REMAP_ADDR`, horloge en marche (`CLOCK_PARAMS` bit 0).
* **DesignWare** : `IC_COMP_TYPE` = `0x44570140` vérifié, profondeur des FIFO lue ; maître, adresses de 7 bits,
  RESTART permis ; 400 kHz (`speed`) ; HCNT / LCNT calculés comme Linux (`i2c_dw_scl_hcnt/lcnt`, temps de descente
  300 ns) pour une **horloge d'entrée de 100 MHz** (`clock_mhz`, celle que Linux donne aux Tiger Lake), ou ceux de
  `FMCN` (`hcnt`, `lcnt`) ; maintien de SDA 300 ns. Une transaction : les octets écrits, puis les lectures (RESTART
  sur la première), STOP sur la dernière, jamais plus de lectures en vol que la FIFO de réception ; abandon
  (`IC_TX_ABRT_SOURCE` : adresse sans réponse, donnée refusée, arbitrage perdu) et délai rendus au journal.
  **En sondage** : aucune interruption du contrôleur.

### HID over I2C (spécification de Microsoft 1.0)

Descripteur HID (30 octets au registre `hid_desc`, version `0x0100` vérifiée), descripteur de rapport (au registre
qu'il donne), `SET_POWER` ON, `RESET` (réponse attendue : un rapport de longueur 0, 1 s au plus ; sinon on
continue), rapports d'entrée lus au registre d'entrée sans écriture d'adresse (`wMaxInputLength` octets, longueur
dans les 2 premiers), `SET_REPORT` Feature pour les réglages (opcode 3, registre de données, longueur, numéro).
À l'arrêt : retour en mode souris, `SET_POWER` SLEEP, fonction rendue à pcid.

### Descripteur de rapport et modèle

`hidrd.c` lit les éléments courts (globaux avec PUSH / POP, locaux avec listes et plages d'usages, principaux), les
collections (application : l'usage de la collection ; logique ou physique : un rang, celui d'un doigt), et garde
chaque valeur d'Input, d'Output et de Feature (position en bits, taille, logique, physique, unité). Le modèle :

* **pavé** (« precision touchpad ») : dans la collection Touch Pad (`0x0D/0x05`), chaque collection de doigt
  (Tip Switch, Confidence, Contact Identifier, X, Y absolus), Contact Count, Scan Time, **boutons 1 à 3** ; la taille
  en mm d'après le physique (cm ou pouce, exposant d'unité) ;
* **souris** : chaque rapport qui a un X relatif hors du pavé (jusqu'à 4) : le touchpad en souris (`COL01` de
  Windows) et le **pointeur du clavier** (`COL05`, « ELAN PointStick », 3 boutons au-dessus du pavé) ;
* **réglages** : Input Mode (`0x0D/0x52`), Surface Switch, Button Switch, Contact Count Maximum.

Au lancement : avec un pavé et un Input Mode, i2chid écrit **Input Mode = 3** (pavé) puis Surface et Button Switch
à 1 ; si c'est refusé, ou `mode = mouse`, il garde le mode souris (le touchpad démarre en souris : `COL01`). Les
rapports du pointeur du clavier passent dans les deux modes.

### Gestes (gesture.c)

Repris de l'écran tactile (`src/svc/gt911`, d'Onyx `sys/touch.cpp`) là où ils ont un sens sur un touchpad :

| Geste | Événement |
|---|---|
| un doigt glisse | `EV_MOUSE_REL` ; accélération : 5 points/mm lent (≤ 20 mm/s), 15 points/mm vite (≥ 150 mm/s), × `pointer` % |
| toucher bref (< 220 ms, < 2,5 mm) | clic gauche ; deux doigts : droit ; trois : milieu (`tap = 0` : aucun) |
| double toucher : un toucher, puis le doigt reposé dans les 300 ms (`TP_DTAP_MS` de gt911), à moins de 10 mm | **glisser** : bouton gauche appuyé jusqu'au lever ; sans bouger : double clic |
| deux doigts | `EV_WHEEL`, un cran par 3 mm (`scroll`) ; la page suit les doigts (`natural = 1`, comme gt911) |
| boutons du pavé (1 à 3) | gauche, droit, milieu ; un pavé à un seul bouton (« clickpad ») : droit à deux doigts ou dans le coin bas droit |
| pointeur du clavier | ses déplacements et ses 3 boutons tels quels |
| contact sans « Confidence » | ignoré (paume) |

Mode « hybride » de Windows (plus de contacts que de places dans un rapport : le premier rapport annonce le
nombre, les suivants à 0 complètent la trame) : géré.

### Interruption : sondage en attendant

Sans interprète AML, l'interruption du touchpad n'est connue que par le `_CRS` lu à la main :

* **`irq = N`** (`Interrupt` direct) : la ligne N de l'IOAPIC, par `IRQ_BIND` / `IRQ_WAIT` ; le noyau règle les
  lignes ≥ 16 en niveau, actif bas, ce que donne le `_CRS` d'un touchpad ;
* **`gpio = adresse`** (`GpioInt`) : i2chid lit toutes les `poll` ms le bit 1 (état d'entrée) du registre DW0 du
  plot, et ne lit le touchpad que si l'interruption est tenue (`gpio_low = 1` : active à 0). L'adresse du plot se
  calcule d'après la communauté GPIO du PCH (`INT34C6` : `_CRS` du GPIO, ou P2SB) et le numéro du plot ; le
  passage par l'IRQ 14 partagée du GPIO viendra plus tard ;
* **sinon : sondage du touchpad toutes les 8 ms** (`poll`), un rapport par passage. Un touchpad lu sans
  interruption peut redonner son dernier rapport : en mode pavé, un rapport au même temps de balayage est ignoré ;
  **en mode souris** (et pour le pointeur du clavier) un rapport répété ne se reconnaît pas — le premier essai
  dira si l'ELAN rend une longueur 0 quand il n'a rien (comme après un RESET), ce que la spécification laisse
  ouvert.

### Vérifié (13/11)

* **Hors matériel** (`wsl sh src/svc/i2chid/test/run.sh`) : quatre descripteurs d'exemple, écrits d'après les
  gabarits habituels (pas des copies de vrais appareils) : souris de démarrage avec molette ; pavé à la Synaptics
  (souris rapport 2, pavé rapport 3 à 5 doigts, Input Mode rapport 4, sélection rapport 5, cm) ; pavé à la ELAN
  (pavé rapport 4 à 3 doigts, Contact ID de 4 bits, X/Y de 12 bits en pouces, 3 boutons, touchpad en souris
  rapport 1, pointeur du clavier rapport 5, Input Mode de 2 octets avec Device Index). Modèles, tailles en mm,
  écriture de l'Input Mode, rapports de souris, puis sur chaque pavé : glissé (dx > 0, dy = 0), toucher → clic
  gauche, toucher à deux doigts → droit, deux doigts vers le bas → molette, double toucher et glissé → bouton tenu
  pendant le glissé puis relâché, double toucher → double clic, clic du pavé (clickpad : coin bas droit → droit ;
  pavé à boutons : bouton 2 → droit), mode hybride, pointeur du clavier à côté du pavé. **Tout est juste.**
* **QEMU q35** (console texte) : `i2chid probe`, `i2chid run` → « pas de contrôleur I2C du LPSS d'Intel (classe
  0c/80, 8086:43e8…) : rien à faire », code 0 ; `i2chid stop` → « i2chid ne tourne pas » ; un réglage faux
  (`addr=zz`) refusé ; une ligne inconnue du fichier signalée. **Bureau inchangé** (rien ne lance i2chid).
* **Pas vérifié** : tout ce qui touche le matériel (BAR, LPSS, DesignWare, HID over I2C, l'ELAN, les vitesses du
  pointeur et de la molette ressenties).

## Marche à suivre sur le ZBook

1. `wsl sh tools/pc/build.sh`, `wsl sh tools/pc/sysroot.sh`, installer (le **noyau** aussi : `ACPI_TABLE` a
   changé), démarrer Korin.
2. Au Terminal : **`acpidump`**. Les tables sont dans `SD0:/acpi`, c'est-à-dire **`D:\acpi`** sous Windows ;
   `tables.txt` doit dire « somme juste » partout. Les donner (ou `iasl -e ssdt*.dat -d dsdt.dat`, puis chercher
   `ELAN`, `PNP0C50`, `I2cSerialBusV2`, `GpioInt`, `FMCN`).
3. D'après les tables, remplir `SD0:/etc/i2chid.txt` (`addr`, `hid_desc`, `irq` ou `gpio`, `hcnt`/`lcnt`).
4. `i2chid probe -v` (BAR choisie, LPSS, DesignWare), `i2chid scan` (l'ELAN doit répondre, en `15` sans doute),
   `i2chid info -v` (descripteurs : les copier ici), puis `i2chid run` ; le touchpad est une souris pour le
   bureau ; `i2chid stop` l'arrête. Tout est au journal (`journal`).

## Reste

* l'AML : `_PS0`, `_INI`, `_DSM` lus à la main pour l'instant ; ACPICA (ou un petit interprète) plus tard ;
* l'interruption par l'IRQ partagée du GPIO du PCH (contrôleur `INT34C6`), et le pilote GPIO ;
* le lancement par hwmanager (`drivers.txt`, `pci:8086:43e8`) une fois essayé ;
* les réglages au panneau (vitesse, toucher, sens du défilement) ;
* défilement horizontal (deux doigts de côté) : hidmanager n'a qu'une molette verticale.
