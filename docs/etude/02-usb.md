# 02 — Pile USB du Raspberry Pi 4 (analyse de Circle → transposition FASMARM AArch64)

> Source analysée : `./ref\circle` (lecture seule). Tous les chemins ci-dessous sont relatifs à ce dossier.
> Cible : RPi 4 (BCM2711, Cortex-A72, AArch64), OS en assembleur pur, style MenuetOS (procédural, pas d'objets).

---

## 0. Vue d'ensemble et décision d'architecture

```
 USB-A x4  ──►  VIA VL805 (xHCI 1.0, PCIe Gen2 x1)  ──PCIe──►  contrôleur PCIe BCM2711 (RC @ 0xFD500000)
                  ├─ root port 1 : USB2  → HUB USB2 interne VL805 (2109:3431, HS, 4 ports)  → claviers/souris LS/FS
                  └─ root ports 2..5 : USB3 (SuperSpeed uniquement, 1 par prise)
 USB-C (alim) ──►  DWC2 OTG @ 0xFE980000 (SPI 73)   +   xHCI interne BCM2711 @ 0xFE9C0000 (SPI 176, si otg_mode=1)
```

- Circle choisit le contrôleur via `include/circle/usb/usbhcidevice.h` : RPi1-3 → `CDWHCIDevice`, **RPi4 → `CXHCIDevice`**, RPi5 → `CUSBSubSystem`. `lib/usb/Makefile` ne compile **pas** `dwhci*.cpp` pour RPi4.
- Configuration VL805 (`include/circle/usb/xhciconfig.h`) : `XHCI_SUPPORTED_VERSION 0x100`, `MAX_SLOTS 32`, `MAX_PORTS 5`, **contextes 32 octets** (CSZ=0), `XHCI_IS_USB2_PORT(p) = (p==1)`.
  Commentaire du fichier : *USB2 : port 1 ; USB3 : ports 2-5*.
- **Hub interne du VL805 : oui (à confirmer sur cible, mais quasi-certain)**. Il n'y a qu'un seul port racine USB2 pour 4 prises physiques : les 4 prises USB2 passent donc par un hub HS (sous Linux : `2109:3431 VIA Labs, Inc. Hub` sur le bus 1). Le code Circle ne le nomme pas, mais il le gère par le chemin générique `CUSBStandardHub` + `CXHCIUSBDevice::EnableHubFunction()` + champs TT du slot context.
  **Conséquence majeure : un clavier/souris LS/FS est TOUJOURS derrière un hub HS → driver de hub + Transaction Translator (TTHubSlotID/TTPortNumber/route string) obligatoires dès le MVP.**
- **DWC OTG (USB-C) : à ignorer au départ.** Sur RPi4 Circle ne l'utilise qu'en mode *gadget* (`lib/usb/gadget/dwusbgadget*.cpp`). Le driver hôte `dwhci*.cpp` (~3 800 lignes : `dwhcidevice.cpp` 1889, `dwhcixferstagedata.cpp` 620, ordonnanceurs de trames, split transactions, canaux) est réservé aux RPi1-3. Le port USB-C sert d'alimentation.
- **Alternative intéressante pour le bring-up** : le **xHCI interne** du BCM2711 (`sysconfig.h`, option `USE_XHCI_INTERNAL`, `ARM_XHCI0_BASE = 0xFE9C0000`, IRQ `GIC_SPI(176)`, requiert `otg_mode=1` dans `config.txt`, version 0x110, contextes 64 octets, 1 port, pas de PCIe ni de firmware VL805 ; seulement une mise sous tension via property tag `SET_POWER_STATE` device `USB_HCD` sur les modèles ≠ 4B). Il permet de valider le cœur xHCI **sans PCIe** (clavier sur adaptateur OTG USB-C, Pi alimenté par le GPIO 5 V). Le même code asm doit alors supporter CSZ=1 (contextes 64 o) : prévoir une constante `CTX_SIZE`.

---

## 1. Volume du code Circle concerné (lignes `wc -l`)

| Bloc | Fichiers | Lignes |
|---|---|---|
| Pont PCIe | `lib/bcmpciehostbridge.cpp` (+ .h 162) | 1 319 (≈ 650 utiles pour RPi4, le reste RPi5/MDIO/MSI) |
| Cœur xHCI | `lib/usb/xhci*.cpp` (11 fichiers) | 2 952 (`xhciendpoint` 668, `xhcidevice` 475, `xhciusbdevice` 327, `xhcirootport` 325, `xhcicommandmanager` 266, `xhcimmiospace` 184, `xhciring` 181, `xhcieventmanager` 151, `xhcisharedmemallocator` 142, `xhcislotmanager` 121, `xhciroothub` 112) |
| En-têtes xHCI | `include/circle/usb/xhci*.h` | 1 442 (dont `xhci.h` 541 : registres, TRB, contextes) |
| USB générique | `usbdevice` 772, `usbstandardhub` 633, `usbdevicefactory` 392, `usbendpoint` 292, `usbhostcontroller` 248, `usbstring` 248, `usbconfigparser` 229, `usbfunction` 215, `usbrequest` 170 | ≈ 3 200 |
| HID | `usbhiddevice` 274, `usbkeyboard` 272, `usbmouse` 386 ; `lib/input/keymap.cpp` 352 + `keymap_fr.h`, `keyboardbehaviour.cpp` 193 | ≈ 1 500 |
| Mass storage | `usbmassdevice.cpp` | 734 |
| DWC (non retenu) | `dwhci*.cpp` | ≈ 3 800 |

Total utile pour clavier+souris sur RPi4 : **≈ 8 000 lignes C++** (dont beaucoup de C++ « plomberie » : classes, allocateurs, logs, asserts, plug-and-play générique).

---

## 2. Pont PCIe BCM2711 (`lib/bcmpciehostbridge.cpp`, port du Linux `pcie-brcmstb.c`)

### 2.1 Adresses et fenêtres (RPi4)

| Élément | Valeur | Source |
|---|---|---|
| Base registres RC | `0xFD500000` (`ARM_PCIE_HOST_BASE`), taille 0x9310 | `include/circle/bcm2711.h` |
| Fenêtre **outbound** (CPU→PCIe) | CPU `0x6_0000_0000` → bus PCIe `0xF800_0000`, taille `0x0400_0000` (64 Mo) | `memorymap64.h` `MEM_PCIE_RANGE_*` |
| BAR0 xHCI VL805 | = `0xF8000000` côté PCIe ⇒ **MMIO xHCI vu par le CPU à `0x6_0000_0000`** (`ARM_XHCI0_BASE = MEM_PCIE_RANGE_START_VIRTUAL`) | `bcm2711.h` |
| Fenêtre **inbound** (DMA PCIe→RAM) | lue dans le DTB `/scb/pcie@7d500000` propriété `dma-ranges` (7 cellules : flags, bus hi/lo, cpu hi/lo, size hi/lo). Défaut : bus 0 → CPU 0, taille = RAM ; si RAM ≥ 4 Go et B0/rev≥5 : **bus = `0x4_0000_0000`** | `lib/machineinfo.cpp` `GetPCIeDMAMemory()` |
| Offset DMA | `s_nDMAAddress` ; `XHCI_TO_DMA(p) = p | dma_bus_base` ; `XHCI_FROM_DMA(a) = a & 0xFFFFFFFF` | `xhci.h` |
| IRQ INTx (INTA) | `ARM_IRQ_PCIE_HOST_INTA = GIC_SPI(143)` = **INTID GIC 175** (utilisé par Circle) | `bcm2711int.h` |
| IRQ MSI | `GIC_SPI(148)` = INTID 180 — code MSI présent mais `#if 0` (non utilisé) | idem |

### 2.2 Registres utilisés (offsets depuis 0xFD500000)

| Offset | Nom | Usage |
|---|---|---|
| 0x00AC | `BRCM_PCIE_CAP_REGS` | capability PCIe du RC (LNKCAP +0x0C, LNKSTA +0x12, RTCTL +0x1C, LNKCTL2 +0x30) |
| 0x0188 | `RC_CFG_VENDOR_SPECIFIC_REG1` | bits[3:2] ENDIAN_MODE_BAR2 = 0 |
| 0x043C | `RC_CFG_PRIV1_ID_VAL3` | bits[23:0] class code = `0x060400` (pont PCI-PCI) |
| 0x4008 | `MISC_MISC_CTRL` | b12 SCB_ACCESS_EN=1, b13 CFG_READ_UR_MODE=1, b[21:20] MAX_BURST=0 (128 o), b[31:27] SCB0_SIZE |
| 0x400C/0x4010 | `CPU_2_PCIE_MEM_WIN0_LO/HI` | adresse bus de la fenêtre outbound |
| 0x402C / 0x403C | `RC_BAR1_CONFIG_LO` / `RC_BAR3_CONFIG_LO` | SIZE (b[4:0]) = 0 → désactivés |
| 0x4034/0x4038 | `RC_BAR2_CONFIG_LO/HI` | fenêtre inbound : adresse bus | taille encodée |
| 0x4060 | `MISC_EOI_CTRL` | EOI MSI (non utilisé) |
| 0x4068 | `MISC_PCIE_STATUS` | b4 PHYLINKUP, b5 DL_ACTIVE, b7 PORT (1 = mode RC) |
| 0x406C | `MISC_REVISION` | b[15:0] révision |
| 0x4070 | `CPU_2_PCIE_MEM_WIN0_BASE_LIMIT` | b[15:4] base Mo (12 bits bas), b[31:20] limite Mo (12 bits bas) |
| 0x4080 / 0x4084 | `..._BASE_HI` / `..._LIMIT_HI` | b[7:0] = (Mo >> 12) |
| 0x4204 | `MISC_HARD_PCIE_HARD_DEBUG` (RPi4) | b27 SERDES_IDDQ, b1 CLKREQ_DEBUG_ENABLE |
| 0x4300 | `PCIE_INTR2_CPU_BASE` (RPi4) | +0x8 CLR, +0x10 MASK_SET |
| 0x8000 | `PCIE_EXT_CFG_DATA` | fenêtre 4 Ko de config space du device sélectionné |
| 0x9000 | `PCIE_EXT_CFG_INDEX` | `bus<<20 | slot<<15 | func<<12` |
| 0x9210 | `PCIE_RGR1_SW_INIT_1` | b0 PERST (1=assert), b1 INIT_GENERIC (1=reset pont) |

Encodage taille inbound (`encode_ibar_size`) : `log2 ∈ [12,15] → log2-12+0x1C` ; `log2 ∈ [16,37] → log2-15`. Attention : l'`ilog2` de Circle est un **plafond** (3 Go → 32). Pour 4 Go : SIZE = 17 et SCB0_SIZE = 17.

### 2.3 Séquence exacte d'init (RPi4), `pcie_setup()` puis `enable_bridge()`

```
1  SW_INIT_1 (0x9210) |= 2        ; INIT_GENERIC=1  (reset pont)        + relecture
2  SW_INIT_1          |= 1        ; PERST=1 (reset fondamental)          + relecture
3  attendre 100 µs
4  SW_INIT_1          &= ~2       ; sortie de reset du pont
5  HARD_DEBUG (0x4204) &= ~(1<<27); SERDES_IDDQ=0                       + relecture
6  attendre 100 µs
7  rev = MISC_REVISION & 0xFFFF
8  MISC_CTRL : |= (1<<12)|(1<<13) ; &= ~(3<<20)
9  RC_BAR2_CONFIG_LO = lo32(dma_bus) | enc(size) ; RC_BAR2_CONFIG_HI = hi32(dma_bus)
10 MISC_CTRL.SCB0_SIZE[31:27] = ilog2(size) - 15
11 RC_BAR1_CONFIG_LO.SIZE = 0 ; RC_BAR3_CONFIG_LO.SIZE = 0
12 INTR2_CPU CLR (0x4308) = 0xFFFFFFFF ; MASK_SET (0x4310) = 0xFFFFFFFF   (+ relectures)
13 set_gen(2) : LNKCAP(0xB8)[3:0]=2 ; LNKCTL2(0xDC)[3:0]=2  (Gen2 5 GT/s)
14 SW_INIT_1 &= ~1                ; PERST=0
15 attendre 100 ms (spec CEM)
16 boucler jusqu'à 100 ms : STATUS(0x4068) & 0x30 == 0x30 (PHYLINKUP & DL_ACTIVE)  → sinon "Link down"
17 vérifier STATUS & 0x80 (mode RC)
18 fenêtre outbound :
     WIN0_LO(0x400C)=0xF8000000 ; WIN0_HI(0x4010)=0
     BASE_LIMIT(0x4070) : BASE=(0x6000 & 0xFFF)=0, LIMIT=(0x6003F & 0xFFF)=0x03F  → 0x03F00000
     BASE_HI(0x4080)=6 ; LIMIT_HI(0x4084)=6
19 PRIV1_ID_VAL3(0x043C)[23:0] = 0x060400
20 VENDOR_REG1(0x0188)[3:2] = 0
21 HARD_DEBUG(0x4204) |= 2        ; CLKREQ_DEBUG_ENABLE
--- enable_bridge() : config space du RC = registres directs (base+off), bus 0 dev 0
22 vérifier class (0x08)>>8 == 0x060400 et header type (0x0E) == 1
23 CACHE_LINE_SIZE(0x0C)=16 ; SECONDARY_BUS(0x19)=1 ; SUBORDINATE_BUS(0x1A)=1
24 MEMORY_BASE(0x20)=0xF800 ; MEMORY_LIMIT(0x22)=0xF800 ; BRIDGE_CONTROL(0x3E)=1 (PARITY)
25 RTCTL (0xAC+0x1C) = 0x10 (CRSSVE)
26 COMMAND(0x04) = 0x0146 (MEMORY|MASTER|PARITY|SERR)
```

### 2.4 Notification firmware VL805 + activation du device (`lib/usb/xhcidevice.cpp`, `Initialize()`)

Les RPi4 récents (rev ≥ 1.4, Pi 400, CM4) n'ont plus d'EEPROM SPI pour le VL805 : **c'est le VideoCore qui charge le firmware du VL805** quand on le lui demande, **après** le reset PCIe et **avant** tout accès au BAR.

Property tag `PROPTAG_NOTIFY_XHCI_RESET = 0x00030058` (`include/circle/bcmpropertytags.h`), valeur = `bus<<20 | slot<<15 | func<<12` = **`0x00100000`** (bus 1, slot 0, func 0, cf. `XHCI_PCIE_*` dans `xhciconfig.h`). Buffer mailbox (canal 8, `0xFE00B880`, adresse bus = phys | `0xC0000000`, aligné 16) :

```
dd 7*4, 0            ; taille totale, code requête
dd 0x00030058, 4, 4  ; tag, taille buffer valeur, taille requête
dd 0x00100000        ; valeur
dd 0                 ; tag de fin
```

Puis `enable_device(0x0C0330, 0, 0)` sur **bus 1** (via `EXT_CFG_INDEX=0x00100000`, données à `base+0x8000`) :
- vérifier `class>>8 == 0x0C0330` (xHCI) et header type 0 ;
- `CACHE_LINE_SIZE = 16` ;
- `BAR0 (0x10) = 0xF8000000 | 4` (type mémoire 64 bits) ; `BAR1 (0x14) = 0` ;
- `INTERRUPT_PIN (0x3D)` doit valoir 1 (INTA) — Circle le force sinon ;
- `COMMAND = 0x0146` (INTx **non** désactivé).

Ensuite seulement : `read16(0x6_0000_0002) == 0x0100` (HCIVERSION) sinon erreur.

---

## 3. Cœur xHCI (`lib/usb/xhci*.cpp`, `include/circle/usb/xhci.h`)

### 3.1 Espaces MMIO (`xhcimmiospace.cpp`)

| Base | Calcul |
|---|---|
| CAP | `0x6_0000_0000` |
| OP | CAP + `read8(CAP+0x00)` (CAPLENGTH) |
| DB (doorbells) | CAP + (`CAP+0x14` DBOFF & ~3) ; doorbell n = DB + 4n |
| RT (runtime) | CAP + (`CAP+0x18` RTSOFF & ~0x1F) ; interrupteur 0 à RT+0x20 |
| PORTS | OP + 0x400 + 0x10·(port-1) |

Registres-clés : CAP `HCSPARAMS1 (0x04)` [7:0] MaxSlots, [31:24] MaxPorts ; `HCSPARAMS2 (0x08)` [31:27] MaxScratchpadBufs (lo seulement chez Circle) ; `HCCPARAMS1 (0x10)` b2 CSZ, [31:16] xECP.
OP : `USBCMD 0x00` (b0 R/S, b1 HCRST, b2 INTE) ; `USBSTS 0x04` (b0 HCH, b2 HSE, b3 EINT RW1C, b4 PCD, b11 CNR) ; `PAGESIZE 0x08` (b0 = 4K) ; `CRCR 0x18/0x1C` (b0 RCS) ; `DCBAAP 0x30/0x34` ; `CONFIG 0x38` [7:0] MaxSlotsEn.
PORTSC (+0x00) : b0 CCS, b1 PED (**RW1C : écrire 1 désactive le port !**), b3 OCA, b4 PR, [8:5] PLS, b9 PP, [13:10] Speed, b17 CSC, b18 PEC, b21 PRC (changements RW1C).
Interrupteur 0 (RT+0x20) : `IMAN +0x00` (b0 IP RW1C, b1 IE), `IMOD +0x04`, `ERSTSZ +0x08`, `ERSTBA +0x10/0x14`, `ERDP +0x18/0x1C` (b3 EHB RW1C).
Les accès 64 bits sont faits en **2×32 bits, lo puis hi** (`op_write64`, `rt_write64`).

### 3.2 Structures de données

**TRB (16 octets)** : `+0 Parameter (u64)`, `+8 Status (u32)`, `+12 Control (u32)` ; Control b0 = **Cycle**, [15:10] = type.
Types : 1 Normal, 2 Setup, 3 Data, 4 Status, 6 Link, 9 EnableSlot, 10 DisableSlot, 11 AddressDevice, 12 ConfigureEndpoint, 13 EvaluateContext, 14 ResetEndpoint, 15 StopEndpoint, 16 SetTRDequeue, 23 NoOp ; événements 32 Transfer, 33 CommandCompletion, 34 PortStatusChange, 37 HostController.
Codes de complétion : 1 Success, 6 Stall, 9 NoSlots, 13 ShortPacket, 14/15 Under/Overrun, 21 EventRingFull.
Champs : Link b1 TC (toggle cycle) ; Transfer Status [16:0] longueur, [21:17] TD size, [31:22] interrupteur ; Control b2 ISP, b5 IOC, b6 IDT, [17:16] TRT (Setup : 0 no data, 2 OUT, 3 IN), b16 DIR (Data/Status : 1=IN). Event : Status[31:24] code, [23:0] longueur **résiduelle** ; Control [20:16] EPID, [31:24] SlotID ; PortStatus : Parameter[31:24] PortID.

**Anneaux (`xhciring.cpp`)** : tableau de N TRB, **aligné 64 o, ne traversant pas une frontière de 64 Ko**. Commande (64 TRB) et transfert (64 TRB) : le dernier TRB est un **Link** vers le début avec TC=1. Événement (256 TRB) : pas de Link, décrit par l'ERST.
- *Enqueue (producteur)* : écrire Param/Status, puis Control | cycle courant ; `idx++` ; si `idx == N-1` : `Link.Control ^= C` (le Link prend le cycle courant), `cycle ^= 1` (car TC), `idx = 0`. Cycle initial = 1.
- *Dequeue événement (consommateur)* : entrée valide si `(TRB.Control & 1) == cycle` ; après traitement `idx++`, si `idx == N` → `idx=0, cycle ^= 1` ; puis écrire `ERDP = dma(&ring[idx]) | EHB(8)`.
- Détection « anneau plein » de Circle (`GetEnqueueTRB`) : TRB suivant déjà au cycle courant — heuristique faible (pas de suivi du dequeue HC) ; acceptable avec ≤ 2 requêtes en vol par endpoint.

**ERST** (16 o, aligné 64) : `u64 RingSegmentBase ; u32 RingSegmentSize (=256) ; u32 0`.

**DCBAA** : `(MaxSlotsEn+1)` × u64, aligné 64. `[0]` = adresse DMA du tableau de scratchpad, `[slot]` = adresse DMA du Device Context.
**Scratchpad** : n pages de 4 Ko alignées 4 Ko + tableau de n u64 (adresses DMA).

**Contextes (VL805 : 32 octets chacun)** — `xhci.h` :
```
Slot ctx  dw0: RouteString[19:0] Speed[23:20] MTT[25] Hub[26] ContextEntries[31:27]
          dw1: MaxExitLatency[15:0] RootHubPortNumber[23:16] NumberOfPorts[31:24]
          dw2: TTHubSlotID[7:0] TTPortNumber[15:8] TTT[17:16] Interrupter[31:22]
          dw3: USBDeviceAddress[7:0] SlotState[31:27]
EP ctx    dw0: EPState[2:0] Mult[9:8] MaxPStreams[14:10] LSA[15] Interval[23:16]
          dw1: CErr[2:1] EPType[5:3] HID[7] MaxBurst[15:8] MaxPacketSize[31:16]
          dw2-3: TRDequeuePointer (b0 = DCS)
          dw4: AverageTRBLength[15:0] MaxESITPayload[31:16]
Device ctx (0x400) : Slot @0, EP DCI n @ n*0x20       (DCI 1 = EP0)
Input ctx  (0x420) : ICC @0 (dw0 Drop flags, dw1 Add flags), puis Device ctx @0x20
```
EPType : 4 Control, 2/6 Bulk OUT/IN, 3/7 Interrupt OUT/IN, 1/5 Isoch. **DCI/EPID = 2·num + (IN?1:0)** ; doorbell target = DCI.
Vitesse slot (PSI VL805) : 1 FS, 2 LS, 3 HS, 4 SS (`XHCI_USB_SPEED_TO_PSI`).

### 3.3 Allocation mémoire DMA (`xhcisharedmemallocator.cpp`, `memory.h`)

- Toutes les structures xHCI (anneaux, ERST, DCBAA, device contexts, scratchpad) sont allouées dans la **région « coherent »** : pages `COHERENT_SLOT_XHCI_START..END` = de +1 Mo à +4 Mo de `MEM_COHERENT_REGION` (3 Mo), mappée **Device-nGnRnE (non cachée)** (`memory64.cpp` MAIR index COHERENT = 0x00 ; `translationtable64.cpp`).
- Allocateur bump + free-list de blocs fixes de 2048 o (align 64, frontière 64 Ko). Défauts : align 64, boundary 64 Ko. Zéro-rempli.
- Les **input contexts** (alloués sur le tas cacheable, alignés 4 Ko) et les **buffers de données** (tas) passent par `CleanAndInvalidateDataCacheRange` avant soumission et après complétion.

### 3.4 Séquence d'init xHCI (`CXHCIDevice::Initialize`, managers)

```
 1 [PCIe + tag 0x30058 + enable_device]           (cf. §2)
 2 HCIVERSION == 0x0100
 3 lire CAPLENGTH/DBOFF/RTSOFF/HCSPARAMS1-2/HCCPARAMS1 ; vérifier CSZ==0 ; MaxPorts ≤ 5
 4 HWReset : attendre USBSTS.CNR==0 (≤100 ms) ; USBCMD |= HCRST ; attendre HCRST==0 (≤20 ms)
   (Circle ne vérifie pas HCH=1 avant ; la spec l'exige : R/S=0 puis attendre HCH=1)
 5 PAGESIZE & 1 (4 Ko) obligatoire
 6 SlotManager : CONFIG.MaxSlotsEn = 32 ; DCBAA (33×8, align 64) ; DCBAAP = dma(DCBAA)
 7 EventManager : ring 256 TRB ; ERST[0] = {dma(ring), 256} ;
      ERSTSZ=1 ; ERSTBA=dma(ERST) ; ERDP=dma(ring) ; IMOD=500 (×250 ns = 125 µs) ; IMAN |= IE
   (spec : écrire ERSTSZ, ERDP puis ERSTBA en dernier — préférable dans notre code)
 8 CommandManager : ring 64 TRB ; CRCR = dma(ring) | RCS(1)
 9 Scratchpad : n pages 4 Ko + tableau ; DCBAA[0] = dma(tableau)
10 RootHub : un objet par port (1..MaxPorts)
11 ConnectIRQ(INTID 175, level) ; USBCMD |= INTE ; USBCMD |= R/S
12 RootHub.Initialize : attendre 300 ms ; pour chaque port : RootPort.Initialize puis Configure
```

**Port racine (`xhcirootport.cpp`)** : si `PORTSC.CCS` : port USB2 (1) → `PORTSC = (v | PR) & ~PED`, attendre PR==0 (≤100 ms) ; port USB3 → attendre PLS==U0 (≤100 ms). Attendre 100 ms, lire Speed (PORTSC[13:10]). Changement : `StatusChanged()` réécrit `PORTSC & ~PED` pour acquitter les bits RW1C.

### 3.5 Commandes (`xhcicommandmanager.cpp`)

`DoCommand(control, p1, p2, status)` : remplit le TRB à l'enqueue, `Control |= cycle`, `IncrementEnqueue`, `DSB`, **doorbell 0 = 0**, puis attend (≤ 3 s) un drapeau positionné **par le handler d'IRQ** (`CommandCompleted` vérifie que `event.Parameter` == adresse du TRB émis). Résultat : code de complétion + SlotID (Control[31:24] de l'événement).
- EnableSlot (type 9) → SlotID.
- AddressDevice (11) : Param = dma(input ctx) ; Control |= slot<<24 ; BSR (b9) = 0 ⇒ **le xHC envoie lui-même SET_ADDRESS** (adresse = SlotID).
- ConfigureEndpoint (12), EvaluateContext (13), ResetEndpoint (14 : ep<<16|slot<<24), SetTRDequeue (16 : Param = dma(TRB)|DCS), DisableSlot (10).

### 3.6 Événements & IRQ (`xhcieventmanager.cpp`, `xhcidevice.cpp::InterruptHandler`)

Handler (IRQ INTx niveau, INTID 175) :
```
s = USBSTS ; USBSTS = s | EINT          ; acquitte EINT (RW1C)
IMAN = IMAN | IP                        ; acquitte IP (RW1C), garde IE
si s & HCH → "HC halted", fin
jusqu'à 16 fois : HandleEvents()        ; traite 1 TRB valide, avance le dequeue
ERDP = dma(dernier dequeue) | EHB       ; ou relit ERDP & ~0xF | EHB si rien
```
Dispatch : 32 Transfer → `SlotManager.TransferEvent(cc, résiduel, slot, epid)` → endpoint ; 33 CmdCompletion → CommandManager ; 34 PortStatusChange → RootHub.StatusChanged(port) ; 37 HC event (log Event Ring Full).
**En polling, rien ne change côté HC** : les événements sont écrits dans l'anneau même si INTE/IE=0 ; il suffit de lire l'anneau.

### 3.7 Device & endpoints (`xhciusbdevice.cpp`, `xhciendpoint.cpp`)

**Énumération côté xHCI (`CXHCIUSBDevice::Initialize`)** :
1. Device context (0x400, mémoire cohérente) ; EnableSlot → `slot` ; `DCBAA[slot] = dma(devctx)`.
2. Input ctx : `Add = 3` (A0|A1) ; Slot : `RootHubPortNumber`, `RouteString`, `Speed=PSI`, `ContextEntries=1` ; si derrière un hub HS et device LS/FS (`IsSplit`) : `TTHubSlotID = slot du hub`, `TTPortNumber = port du hub`, `MTT` ; EP0 : `TRDP = dma(ring)|1`, `CErr=3`, `EPType=4`, `AvgTRBLen=8`, `MPS = 8 (LS/FS) | 64 (HS) | 512 (SS)`.
3. AddressDevice (BSR=0) ; **attendre 50 ms** ; l'adresse USB = SlotID.
4. `CUSBDevice::Initialize()` (§4).

**Endpoint (`CXHCIEndpoint` ctor avec descripteur)** : MPS = `wMaxPacketSize & 0x7FF` ; DCI ; type ; `Interval` converti (`ConvertInterval`) : HS/SS → `bInterval-1` (borné 0..15) ; LS/FS → plus petit `i ∈ [3,10]` tel que `125 µs·2^i ≥ 1 ms·bInterval` (ex. bInterval=10 → i=7). Input ctx : `Add = Drop = 1<<DCI` ; EP : `TRDP|DCS`, `EPType`, `MPS`, `CErr=3`, Interrupt : `Interval`, `AvgTRBLen=16`, `MaxESITPayload=MPS`. Puis **ConfigureEndpoint**.
(Circle ne met ni A0 ni `ContextEntries ≥ DCI` : le VL805 le tolère ; **notre version doit suivre la spec** : A0=1, copie du slot ctx, `ContextEntries = DCI max`.)

**SetMaxPacketSize (EP0, FS seulement)** : EvaluateContext avec `Add = 2`, EP0.MPS = `bMaxPacketSize0`.

**Transferts (`TransferAsync`)** :
- Bulk/Interrupt : **un seul TRB Normal** (`type 1 | IOC`, Status = len, Param = dma(buf)) — pas de découpage aux frontières de 64 Ko (piège pour le mass storage).
- Control : Setup (`type 2 | IDT | TRT<<16`, Status = 8, Param = les 8 octets du setup inline) ; Data optionnel (`type 3 | DIR_IN?`, Param = dma(buf), Status = len) ; Status (`type 4 | IOC | DIR`, DIR=1 si pas de data ou data OUT, 0 si data IN).
- `DSB` puis **doorbell[slot] = DCI**.
- Complétion : code 1 ou 13 (short) → `actual = len - résiduel` ; 14/15 ignorés ; autres = erreur (STALL → `ResetFromHalted` : ResetEndpoint + SetTRDequeue(enqueue, cycle)).
- `Transfer()` bloquant = async + attente d'un drapeau (≤ 3 s) mis par le handler d'IRQ.

**Hub (`EnableHubFunction`)** : EvaluateContext `Add = Drop = 1` (A0) avec `Slot.Hub=1`, `NumberOfPorts`, et si HS : `MTT`, `TTT` (Linux utilise plutôt ConfigureEndpoint ; à garder en tête si le VL805 refuse).

---

## 4. Couche USB générique

### 4.1 Énumération (`lib/usb/usbdevice.cpp` `CUSBDevice::Initialize/Configure`)

1. `GET_DESCRIPTOR(DEVICE, 8)` (EP0 MPS provisoire) → `bMaxPacketSize0` → `SetMaxPacketSize`.
2. `GET_DESCRIPTOR(DEVICE, 18)` ; vérifier bLength=18, type=1.
3. **Pas de `SET_ADDRESS` sur RPi4** (`#if RASPPI <= 3` uniquement — c'est AddressDevice qui le fait).
4. `GET_DESCRIPTOR(CONFIGURATION, 9)` → `wTotalLength` (≤ 1024) → `GET_DESCRIPTOR(CONFIGURATION, wTotalLength)`.
5. `CUSBConfigurationParser` (`usbconfigparser.cpp`) : marche `bLength` par `bLength`, valide l'ordre Config → Interface → Endpoint, longueurs attendues, `bLength==0` = fin.
6. Chaînes optionnelles (`usbstring.cpp`, langue puis iManufacturer/iProduct) — **à omettre dans le MVP**.
7. `SET_CONFIGURATION(bConfigurationValue)` + 50 ms.
8. Pour chaque interface (alt-setting 0 seulement) : nom `"intC-S-P"` (classe-sous-classe-protocole en hexa) → `CUSBDeviceFactory::GetDevice()` ; `Initialize()` puis, après tous les ports, `Configure()`.

Requêtes : `GET_DESCRIPTOR = 0x80,6,(type<<8)|idx,wIndex,len` ; `SET_CONFIGURATION = 0x00,9,val,0,0` ; `SET_INTERFACE = 0x01,11,alt,if,0` (`usbfunction.cpp`, seulement si alt≠0).

### 4.2 Fabrique (`usbdevicefactory.cpp`)

| Nom | Driver |
|---|---|
| `int9-0-0`, `int9-0-2` | `CUSBStandardHub` |
| `int3-1-1` | clavier boot (`CUSBKeyboardDevice`, sauf exceptions VID/PID) |
| `int3-1-2` | souris (`CUSBMouseDevice`) |
| `int3-0-0/2`, `int3-1-0` | HID générique (gamepads…) |
| `int8-6-50` | mass storage BOT/SCSI |

### 4.3 Hub (`usbstandardhub.cpp`) — indispensable (hub interne VL805)

```
Initialize : GET_DESCRIPTOR class (0xA0, 6, 0x2900, 0, 9) → bNbrPorts, wHubCharacteristics
             HasMultipleTTs = (bInterfaceProtocol == 2) ; TTThinkTime = (wHubChar>>5)&3
             EnableHubFunction()  (EvaluateContext slot.Hub=1…)
Configure  : 1 endpoint Interrupt IN obligatoire → ConfigureEndpoint
EnumeratePorts :
   pour chaque port p : SET_FEATURE(PORT_POWER=8)  (0x23, 3, 8, p, 0)
   attendre 510 ms
   pour chaque port : GET_STATUS (0xA3, 0, 0, p, 4) → wPortStatus/wChange
        si CONNECTION(b0) : SET_FEATURE(PORT_RESET=4) ; 100 ms ; GET_STATUS
        si ENABLE(b1) : vitesse = b9 LS / b10 HS / sinon FS
        nouveau device : RootHubPortID = celui du hub ; RouteString = route_hub | (p << 4·profondeur)
                         TT = ce hub si hub HS et device < HS
        Initialize (§3.7 + §4.1)
   puis Configure de chaque device ; vérifs over-current (GET_STATUS hub/ports)
Hot-plug : URB sur l'EP interrupt (bitmap des ports changés) → GET_STATUS, CLEAR_FEATURE C_PORT_ENABLE(17)/C_PORT_RESET(20)/C_PORT_CONNECTION(16)
```

---

## 5. HID : clavier et souris

### 5.1 Base HID (`usbhiddevice.cpp` `ConfigureHID`)
- Cherche l'endpoint **Interrupt IN** (rapport) et un éventuel Interrupt OUT ; ConfigureEndpoint pour chacun.
- Si interface `class 3 / subclass 1 (boot) / protocol > 0` : `SET_PROTOCOL` (`0x21, 0x0B, val, ifnum, 0`) avec **val = 0 (BOOT) pour le clavier (protocole 1)** et **1 (REPORT) pour la souris (protocole 2)** — Circle parse ensuite le report descriptor de la souris.
- Boucle : `StartRequest()` = URB async sur l'EP IN ; à la complétion → `ReportHandler(buf, len)` puis ré-émission (abandon après 50 erreurs consécutives).
- Pas de `SET_IDLE` chez Circle (recommandé quand même : `0x21, 0x0A, 0, if, 0` pour ne recevoir que les changements).

### 5.2 Clavier (`usbkeyboard.cpp`)
- Rapport boot 8 octets : `[0]` modificateurs (b0 LCtrl … b7 RGUI), `[1]` réservé, `[2..7]` 6 codes d'usage HID.
- `SetLEDs` : `SET_REPORT (0x21, 0x09, 0x0200, if, 1)` avec 1 octet (b0 Num, b1 Caps, b2 Scroll) — envoyé aussi à l'init (« force certains adaptateurs à fonctionner »).
- Diff avec le rapport précédent : modificateurs → codes 0x80+i ; touche absente du nouveau rapport → relâchée ; nouvelle → pressée. Codes 0x01-0x03 (rollover/erreur) à filtrer.
- Traduction : `lib/input/keymap.cpp` + `keymap_fr.h` (AZERTY) : table `[code HID][K_NORMTAB, K_SHIFTTAB, K_ALTTAB, K_ALTSHIFTTAB, K_CTRLTAB]` (u16) ; `keyboardbehaviour.cpp` gère CapsLock/NumLock/auto-repeat. En asm : table `dw` de ~0x68 × 5 entrées ≈ 1 Ko.

### 5.3 Souris (`usbmouse.cpp`)
- Circle : lit le HID report descriptor (`GET_DESCRIPTOR 0x81, 6, 0x2200, if, len`), parse les items (Usage Page, Usage X/Y/Wheel, Report Size/Count, Report ID) pour trouver offsets/tailles en bits ; clamp ±127.
- **MVP asm : forcer le protocole BOOT** (`SET_PROTOCOL 0`) ⇒ rapport fixe 3 octets `[boutons, dx (s8), dy (s8)]` (+ molette en octet 3 sur beaucoup de souris, non garanti). Le parseur de report descriptor vient en phase 2 (molette, souris sans boot).

### 5.4 Mass storage (option, `usbmassdevice.cpp`)
Bulk IN + Bulk OUT ; CBW 31 o (signature `0x43425355`, tag, longueur, flags b7=IN, LUN, CB 16 o) → data → CSW 13 o (`0x53425355`, résidu, status 0/1/2). Séquence : INQUIRY (0x12) → TEST UNIT READY (0x00) en boucle avec REQUEST SENSE (0x03) → READ CAPACITY(10) (0x25, bloc 512 exigé) → READ(10) 0x28 / WRITE(10) 0x2A. Récupération : `BULK_ONLY_MASS_STORAGE_RESET (0xFF)` + clear halt. Penser à découper les TRB Normal aux frontières de 64 Ko (Circle ne le fait pas).

---

## 6. Pièges (récapitulatif)

1. **Non-cohérence DMA** : le PCIe du BCM2711 ne snoope pas les caches ARM. Circle : structures en mémoire **Device-nGnRnE** + `dc civac` pour buffers/input contexts. **Recommandation asm : une zone DMA unique non cachée** (ex. 2-4 Mo, MAIR Normal-NC `0x44` ou Device) contenant anneaux, contextes, input contexts ET buffers (descripteurs, rapports HID) → plus aucune maintenance de cache. Avec Normal-NC : `dmb oshst` avant d'écrire le mot Control (cycle) et `dsb sy` avant chaque doorbell. Si MMU désactivée, tout est Device : fonctionne mais lent pour le reste de l'OS.
2. **Adresses bus ≠ physiques** : `dma = phys | dma_bus_base` (0 ou `0x4_0000_0000` selon carte/firmware : lire `dma-ranges` du DTB). Les pointeurs relus dans les événements (`Parameter`) sont des adresses bus : masquer avec `& 0xFFFFFFFF` (Circle) ou soustraire la base.
3. **Limite DMA** : Circle n'utilise pas la RAM ≥ 3 Go (`MEM_HIGHMEM_END`, « not safe to be DMA-able ») ; MSI target 0xFFFFFFFC ne doit pas tomber dans la fenêtre inbound. **Placer toute la zone DMA USB sous 1 Go** (sûr sur tous les modèles et steppings).
4. **MMIO xHCI à `0x6_0000_0000`** : au-delà de 4 Go → l'OS doit mapper cette zone (Device-nGnRE) dans les tables de pages avec un IPS/T0SZ suffisant (≥ 35 bits).
5. **Alignements** : TRB rings 64 o et sans franchir 64 Ko ; DCBAA, ERST, contextes 64 o ; scratchpad 4 Ko (PAGESIZE) ; input context 64 o (Circle : 4 Ko). Buffer de données d'un TRB : ne doit pas franchir 64 Ko.
6. **Ordre** : tag `0x30058` APRÈS link-up PCIe et AVANT accès au BAR ; sinon HCIVERSION ≠ 0x100 / bus error.
7. **PORTSC** : ne jamais réécrire PED=1 (désactive le port) ; bits de changement RW1C ; ne pas réécrire PR involontairement.
8. **Hub interne + TT** : clavier/souris LS/FS → slot ctx avec `TTHubSlotID`, `TTPortNumber`, `RouteString`, `RootHubPortNumber=1`, Speed PSI LS=2/FS=1. Sans cela : AddressDevice échoue (Transaction Error).
9. **Délais** : 300 ms après R/S ; 100 ms après reset port ; 50 ms après AddressDevice et SET_CONFIGURATION ; 510 ms après PORT_POWER hub.
10. **EP0 FS** : MPS initial 8 puis EvaluateContext avec `bMaxPacketSize0`.
11. **Interruption niveau** : acquitter `USBSTS.EINT` **et** `IMAN.IP` puis mettre à jour `ERDP|EHB` **avant** l'EOI GIC, sinon re-déclenchement infini.
12. **CSZ** : VL805 = 32 o, xHCI interne = 64 o → constante `CTX_SIZE`.
13. **Contextes spec** : ConfigureEndpoint avec A0 et `ContextEntries` correct (Circle s'en passe sur VL805).
14. **Scratchpad** : `MaxScratchpadBufs` = HCSPARAMS2[31:27] (+ [25:21] hi, ignoré par Circle) ; DCBAA[0] obligatoire si > 0.

---

## 7. Plan de transposition en asm (routines minimales, ordonnées)

Prérequis OS (hors USB) : UART debug, timer `CNTPCT_EL0`/`CNTFRQ_EL0` (`delay_us`, `delay_ms`, `timeout`), mailbox property (canal 8), MMU (identité ; Device pour 0xFE000000+ / 0xFD500000 / 0x6_0000_0000 ; zone DMA non cachée < 1 Go), GIC-400 (dist `0xFF841000`, CPU `0xFF842000`).

| # | Routine | Rôle | ~lignes asm |
|---|---|---|---|
| **Phase 1 — PCIe** |||
| 1 | `pcie_init` | §2.3 étapes 1-21 | 120 |
| 2 | `pcie_cfg_sel` / `pcie_cfg_rd/wr` | config bus 0 direct, bus 1 via INDEX | 30 |
| 3 | `pcie_bridge_enable` | §2.3 étapes 22-26 | 40 |
| 4 | `vl805_notify` | tag 0x30058 val 0x100000 | 30 |
| 5 | `pcie_xhci_enable` | class, BAR0/1, INTA, COMMAND | 40 |
| **Phase 2 — cœur xHCI (polling)** |||
| 6 | `dma_alloc` | bump allocator aligné (align, boundary 64K), zéro-rempli | 40 |
| 7 | `xhci_probe` | version, bases CAP/OP/DB/RT/PORT, paramètres | 50 |
| 8 | `xhci_reset` | stop, HCH, HCRST, CNR | 40 |
| 9 | `ring_init` / `ring_enqueue` | anneau + Link TC, cycle | 60 |
| 10 | `xhci_setup` | MaxSlotsEn, DCBAA, scratchpad, ERST/event ring, CRCR, R/S | 100 |
| 11 | `evt_peek` / `evt_advance` | lecture cycle, ERDP|EHB | 40 |
| 12 | `xhci_cmd` | commande + attente événement 33 | 70 |
| 13 | `xhci_wait_xfer` | attente événement 32 (slot, epid) + dispatch des autres | 60 |
| **Phase 3 — énumération** |||
| 14 | `rootport_scan` / `rootport_reset` | CCS, PR/U0, vitesse | 70 |
| 15 | `usb_dev_new` | EnableSlot, devctx, input ctx, AddressDevice, TT/route | 120 |
| 16 | `usb_ctrl` | Setup/Data/Status + doorbell + attente | 80 |
| 17 | `usb_get_desc`, `usb_set_config`, `usb_set_ep0_mps` | requêtes standard, EvaluateContext | 70 |
| 18 | `usb_parse_config` | parcours des descripteurs, sélection interface/EP | 90 |
| 19 | `usb_ep_configure` | ConfigureEndpoint (A0+Ai, ContextEntries, Interval) | 90 |
| **Phase 4 — hub** |||
| 20 | `hub_init` | hub desc, slot.Hub/NumberOfPorts/MTT/TTT | 60 |
| 21 | `hub_enum_ports` | power, status, reset, vitesse, récursion `usb_dev_new` | 150 |
| **Phase 5 — HID** |||
| 22 | `hid_init` | SET_PROTOCOL boot, SET_IDLE 0, LEDs | 60 |
| 23 | `hid_queue_in` | TRB Normal IOC sur EP interrupt + doorbell | 30 |
| 24 | `usb_poll` | boucle principale : vide l'anneau d'événements, dispatch par (slot,DCI) → handler, ré-arme | 80 |
| 25 | `kbd_report` + `keymap_fr` | diff, modificateurs, traduction, file d'événements, LEDs | 200 + ~1 Ko données |
| 26 | `mouse_report` | boot 3-4 octets → dx, dy, boutons, (molette) | 50 |
| **Phase 6 — IRQ** |||
| 27 | `gic_enable_175` | ISENABLER5 b15, ITARGETSR, ICFGR niveau, priorité | 30 |
| 28 | `xhci_irq` | §3.6 + EOI ; `usb_poll` appelé depuis l'IRQ ou par signal | 60 |
| **Phase 7 — hot-plug** |||
| 29 | événements 34 (root) + EP interrupt du hub | attach/detach, DisableSlot | 150 |
| **Option** |||
| 30 | `msd_*` | BOT/SCSI (Inquiry, TUR, ReadCap, Read/Write 10) | 500 |

**Estimation** : clavier + souris en polling (phases 1-5) ≈ **2 300 lignes asm**, + IRQ ≈ 100, + hot-plug ≈ 150 ⇒ **≈ 2 500-2 800 lignes** (≈ 10-14 Ko de code + ~1-2 Ko de tables), contre ~8 000 lignes C++ chez Circle. Mass storage : +500-600 lignes. Données runtime : ~64 Ko de zone DMA suffisent pour 8 devices (anneaux 1 Ko chacun, devctx 1 Ko, scratchpad n×4 Ko, event ring 4 Ko).

Ordre de mise au point conseillé : (a) link-up PCIe + lecture HCIVERSION ; (b) commande **NoOp** (type 23) → événement 33 code 1 ; (c) port racine 1 → EnableSlot/AddressDevice du **hub interne** (HS, pas de TT) ; (d) descripteurs du hub ; (e) clavier derrière le hub ; (f) souris ; (g) IRQ.

---

## 8. Pseudo-asm AArch64 (syntaxe FASMARM, à ajuster)

Conventions : `x19` = base CAP xHCI, variables globales dans `usb_vars` ; `DMA_OFS` = base bus DMA (0 ou 0x4_0000_0000) ; structure d'anneau `RING { .va dq, .enq dd, .cyc dd }` ; taille TRB 16 ; zone DMA non cachée (Normal-NC).

### 8.1 Enqueue générique d'un TRB

```asm
; ring_enqueue : x0 = ^RING, x1 = Parameter (u64), w2 = Status, w3 = Control (sans bit C)
; retour : x0 = adresse VA du TRB écrit.   Détruit x4-x8
ring_enqueue:
        ldr     x4, [x0, RING.va]
        ldr     w5, [x0, RING.enq]
        ldr     w6, [x0, RING.cyc]
        add     x7, x4, x5, lsl #4          ; TRB = va + enq*16
        str     x1, [x7, #0]                ; Parameter
        str     w2, [x7, #8]                ; Status
        orr     w8, w3, w6                  ; Control | cycle
        dmb     oshst                       ; param/status visibles avant le cycle
        str     w8, [x7, #12]
        add     w5, w5, #1
        cmp     w5, #RING_TRBS-1            ; dernier slot = Link TRB
        b.ne    .done
        add     x8, x4, #(RING_TRBS-1)*16
        ldr     w1, [x8, #12]
        eor     w1, w1, #1                  ; Link prend le cycle courant
        str     w1, [x8, #12]
        eor     w6, w6, #1                  ; TC=1 → bascule du cycle producteur
        mov     w5, #0
.done:  str     w5, [x0, RING.enq]
        str     w6, [x0, RING.cyc]
        mov     x0, x7
        ret
```

### 8.2 Lecture / avance de l'anneau d'événements

```asm
; evt_peek : retour x0 = VA du TRB événement valide, ou 0
evt_peek:
        adr     x9, evt_ring_state           ; {va, deq, cyc}
        ldr     x0, [x9, RING.va]
        ldr     w1, [x9, RING.enq]           ; ici = index de dequeue
        ldr     w2, [x9, RING.cyc]
        add     x0, x0, x1, lsl #4
        ldr     w3, [x0, #12]
        and     w3, w3, #1
        cmp     w3, w2
        csel    x0, x0, xzr, eq
        dmb     oshld                        ; lire le reste du TRB après le cycle
        ret

; evt_advance : consomme l'événement courant et publie ERDP
evt_advance:
        adr     x9, evt_ring_state
        ldr     x0, [x9, RING.va]
        ldr     w1, [x9, RING.enq]
        ldr     w2, [x9, RING.cyc]
        add     w1, w1, #1
        cmp     w1, #EVT_TRBS
        b.ne    @f
        mov     w1, #0
        eor     w2, w2, #1
@@:     str     w1, [x9, RING.enq]
        str     w2, [x9, RING.cyc]
        add     x0, x0, x1, lsl #4           ; VA du nouveau dequeue
        ldr     x3, [DMA_OFS_var]
        orr     x0, x0, x3                   ; → adresse bus
        orr     x0, x0, #8                   ; EHB (RW1C)
        ldr     x4, [xhci_ir0]               ; RT + 0x20
        str     w0, [x4, #0x18]              ; ERDP lo
        lsr     x0, x0, #32
        str     w0, [x4, #0x1C]              ; ERDP hi
        ret
```

### 8.3 Commande sur le command ring + attente de l'événement (polling)

```asm
; xhci_cmd : w0 = Control (type<<10 | slot<<24 | flags), x1 = Parameter, w2 = Status
; retour : w0 = completion code (ou -2 timeout), w1 = SlotID
xhci_cmd:
        stp     x29, x30, [sp, #-48]!
        stp     x19, x20, [sp, #16]
        stp     x21, x22, [sp, #32]
        mov     w3, w0
        adr     x0, cmd_ring
        bl      ring_enqueue                 ; x0 = VA du TRB de commande
        ldr     x4, [DMA_OFS_var]
        orr     x20, x0, x4                  ; x20 = adresse bus attendue dans l'événement
        dsb     sy
        ldr     x5, [xhci_db]                ; base doorbells
        str     wzr, [x5]                    ; DB[0] = 0 (cible : command ring)
        bl      timer_now                    ; x0 = ticks
        mov     x21, x0
.poll:  bl      evt_peek
        cbz     x0, .tmo
        ldr     w6, [x0, #12]                ; Control
        ubfx    w7, w6, #10, #6              ; type
        cmp     w7, #33                      ; Command Completion Event
        b.ne    .other
        ldr     x8, [x0, #0]                 ; Parameter = adresse bus du TRB de commande
        cmp     x8, x20
        b.ne    .skip                        ; (complétion obsolète)
        ldr     w9, [x0, #8]
        lsr     w19, w9, #24                 ; completion code
        lsr     w22, w6, #24                 ; SlotID
        bl      evt_advance
        mov     w0, w19
        mov     w1, w22
        b       .ret
.other: bl      usb_dispatch_event           ; x0 = TRB : transfert / port status / HC
.skip:  bl      evt_advance
        b       .poll
.tmo:   bl      timer_now
        sub     x0, x0, x21
        ldr     x1, [ticks_3s]
        cmp     x0, x1
        b.lo    .poll
        mov     w0, #-2
.ret:   ldp     x21, x22, [sp, #32]
        ldp     x19, x20, [sp, #16]
        ldp     x29, x30, [sp], #48
        ret

; exemples d'appel
;   NoOp          : mov w0, #(23<<10)                       ; x1=0, w2=0
;   EnableSlot    : mov w0, #(9<<10)
;   AddressDevice : w0 = (11<<10) | slot<<24 ; x1 = bus(input_ctx)
;   ConfigureEP   : w0 = (12<<10) | slot<<24 ; x1 = bus(input_ctx)
;   EvaluateCtx   : w0 = (13<<10) | slot<<24 ; x1 = bus(input_ctx)
```

### 8.4 Control transfer (Setup / Data / Status) sur EP0

```asm
; usb_ctrl : x0 = ^DEVICE (slot, ring EP0), x1 = setup 8 octets packés
;            (bmRequestType | bRequest<<8 | wValue<<16 | wIndex<<32 | wLength<<48)
;            x2 = VA buffer (zone DMA non cachée) ; longueur = wLength
; retour : w0 = octets transférés, ou -1
usb_ctrl:
        stp     x29, x30, [sp, #-64]!
        stp     x19, x20, [sp, #16]
        stp     x21, x22, [sp, #32]
        stp     x23, x24, [sp, #48]
        mov     x19, x0
        mov     x20, x1
        mov     x21, x2
        lsr     x22, x1, #48                 ; w22 = wLength
        tst     x1, #0x80                    ; direction IN ?
        cset    w23, ne                      ; w23 = 1 si IN
        ; --- TRT : 0 no data / 2 OUT / 3 IN
        mov     w3, #0
        cbz     w22, .setup
        mov     w3, #2
        orr     w3, w3, w23                  ; 3 si IN
.setup: lsl     w3, w3, #16
        orr     w3, w3, #(2<<10)             ; type Setup
        orr     w3, w3, #(1<<6)              ; IDT : setup inline
        add     x0, x19, DEVICE.ep0_ring
        mov     x1, x20                      ; Parameter = 8 octets du setup
        mov     w2, #8                       ; TRB transfer length = 8
        bl      ring_enqueue
        ; --- Data stage
        cbz     w22, .status
        mov     w3, #(3<<10)                 ; type Data
        lsl     w4, w23, #16
        orr     w3, w3, w4                   ; DIR = IN ?
        add     x0, x19, DEVICE.ep0_ring
        ldr     x4, [DMA_OFS_var]
        orr     x1, x21, x4                  ; adresse bus du buffer
        mov     w2, w22                      ; longueur
        bl      ring_enqueue
        ; --- Status stage : direction opposée (IN si pas de data ou data OUT)
.status:
        mov     w3, #(4<<10) or (1<<5)       ; type Status + IOC
        cbz     w22, .stin
        cbnz    w23, .stgo                   ; data IN → status OUT (DIR=0)
.stin:  orr     w3, w3, #(1<<16)             ; DIR = IN
.stgo:  add     x0, x19, DEVICE.ep0_ring
        mov     x1, #0
        mov     w2, #0
        bl      ring_enqueue
        ; --- doorbell slot, cible DCI 1 (EP0)
        dsb     sy
        ldrb    w5, [x19, DEVICE.slot]
        ldr     x6, [xhci_db]
        mov     w7, #1
        str     w7, [x6, x5, lsl #2]         ; DB[slot] = 1
        ; --- attente du Transfer Event (type 32) pour (slot, DCI 1)
        mov     w0, w5                       ; slot
        mov     w1, #1                       ; DCI
        bl      xhci_wait_xfer               ; → w0 = code, w1 = résiduel ; dispatch les autres
        cmp     w0, #1                       ; Success
        ccmp    w0, #13, #4, ne              ; ou Short Packet
        b.ne    .err
        sub     w0, w22, w1                  ; octets réels = wLength - résiduel
        b       .out
.err:   cmp     w0, #6                       ; STALL → Reset Endpoint + Set TR Dequeue
        b.ne    @f
        mov     x0, x19
        mov     w1, #1
        bl      ep_reset_from_halt
@@:     mov     w0, #-1
.out:   ldp     x23, x24, [sp, #48]
        ldp     x21, x22, [sp, #32]
        ldp     x19, x20, [sp, #16]
        ldp     x29, x30, [sp], #64
        ret

; exemple : GET_DESCRIPTOR(DEVICE, 18)
;   mov x1, #0x0680 ; movk x1,#0x0100,lsl #16 ; movk x1,#18,lsl #48   (wIndex=0)
;   adr x2, dma_desc_buf ; bl usb_ctrl
```

Remarques : (1) un seul transfert de contrôle en vol par device, donc pas besoin de file d'URB ; (2) la phase IRQ réutilise ces routines : le handler appelle `evt_peek/evt_advance` + `usb_dispatch_event`, et `xhci_cmd`/`usb_ctrl` attendent alors un drapeau au lieu de lire l'anneau (ou on conserve le polling pendant l'énumération et on n'active l'IRQ que pour les rapports HID).

---

## 9. Synthèse

- Faisable en asm : le chemin xHCI (VL805) est régulier (anneaux + contextes) ; la difficulté est concentrée dans le **bring-up PCIe** (≈ 30 écritures de registres précises), le **tag firmware 0x30058**, la **non-cohérence DMA** et le **hub interne avec TT** obligatoire pour clavier/souris.
- Ignorer DWC OTG ; garder le xHCI interne (0xFE9C0000, `otg_mode=1`) comme banc d'essai optionnel sans PCIe.
- MVP polling clavier+souris ≈ 2 500 lignes asm ; IRQ (INTx, INTID 175) puis hot-plug ensuite ; mass storage BOT en option (+500).
