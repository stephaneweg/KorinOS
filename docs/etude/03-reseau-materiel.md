# 03 — Pilotes réseau matériels du RPi4 (analyse de Circle → transposition FASMARM AArch64)

Source analysée : `./ref\circle` (HEAD `7a81e1b4`, 2026-09-25). Lecture seule.
Toutes les adresses sont des adresses ARM « low peripheral » du BCM2711 (`ARM_IO_BASE = 0xFE000000`, `include/circle/bcm2835.h:30`).

## 0. Synthèse

| Pilote | Fichiers Circle | Lignes (wc -l) | Lignes de code utiles* | Difficulté asm | Estimation asm |
|---|---|---|---|---|---|
| Ethernet GENET v5 + PHY BCM54213PE (MDIO) | `lib/bcm54213.cpp`, `include/circle/bcm54213.h` | 1933 + 205 | ~1335 | **faible** | 900–1300 lignes asm, 4–6 Ko de code |
| Hôte SDHCI legacy (Arasan) pour SDIO | `addon/wlan/emmc.c` | 628 | ~546 | moyenne | 350–450 lignes, ~2 Ko |
| WiFi CYW43455 (SDIO, backplane, SDPCM/BCDC, ioctl) | `addon/wlan/ether4330.c` | 2679 | ~2374 | **élevée** | 1800–2500 lignes, 10–14 Ko |
| Adaptateur Circle (C++ ↔ Plan 9) | `addon/wlan/bcm4343.cpp/.h`, `p9*.cpp/.h` | 367+113 ; p9* ≈ 1 500 | — | sans objet | 0 (supprimé) |
| WPA2 (supplicant hôte) | sous-module `addon/wlan/hostap` (rsta2/hostap, commit `966cab4a`) | **non cloné** (dossier vide) | dizaines de milliers (C) | prohibitif | à éviter → supplicant interne de la puce |

\* lignes non vides et hors commentaires (`grep -cvE`).

Conclusions :
1. **GENET d'abord** : ~1300 lignes C linéaires, aucun firmware, aucune pile de protocole, descripteurs DMA dans des *registres* MMIO (pas de cohérence de cache sur les descripteurs). Peut fonctionner **entièrement en polling** (Circle n'utilise déjà pas l'IRQ RX ni l'IRQ de lien).
2. **WiFi** : faisable mais 3–4× plus gros, dépend d'un firmware binaire de ~600 Ko + nvram + CLM, d'un protocole SDIO/SDPCM complet et de nombreux timings. Circle **ne fait pas** le WPA2 dans le pilote : il porte `wpa_supplicant` (hostap) côté hôte. Pour un OS asm, la voie réaliste est le **supplicant interne du firmware** (`sup_wpa` + `WLC_SET_WSEC_PMK`), que Circle n'utilise pas (voir §2.9).

---

## 1. Ethernet GENET v5 (BCM2711) + PHY BCM54213PE

### 1.1 Matériel
- MAC GENET v5 intégré au SoC, base `ARM_BCM54213_BASE = 0xFD580000` (taille 64 Ko) — `include/circle/bcm2711.h:58`.
- PHY externe BCM54213PE en RGMII, accessible via le contrôleur MDIO de l'UniMAC : `ARM_BCM54213_MDIO = base + 0xE14` (`bcm2711.h:59`), adresse PHY **1** (Circle essaie 1 puis 0, `mii_probe()` l.1653).
- IRQ : `ARM_IRQ_BCM54213_0 = GIC_SPI(157)` (INTID 189), `ARM_IRQ_BCM54213_1 = GIC_SPI(158)` (INTID 190) — `include/circle/bcm2711int.h:63-64`.
- Pilote porté de Linux `bcmgenet.c` / `mdio-bcm-unimac.c` (GPLv2) → toute transposition fidèle est une œuvre dérivée GPL.

### 1.2 Carte des registres (offsets depuis 0xFD580000)

| Bloc | Offset | Registres clés utilisés |
|---|---|---|
| SYS | 0x0000 | `SYS_REV_CTRL` 0x00 (bits 27:24 = 6 → GENET v5), `SYS_PORT_CTRL` 0x04 (= 3 `PORT_MODE_EXT_GPHY`), `SYS_RBUF_FLUSH_CTRL` 0x08 (bit1 = reset UMAC) |
| EXT | 0x0080 | `EXT_RGMII_OOB_CTRL` 0x8C : bit4 `RGMII_LINK`, bit5 `OOB_DISABLE`, bit6 `RGMII_MODE_EN`, bit16 `ID_MODE_DIS` |
| INTRL2_0 | 0x0200 | STAT +0, SET +4, CLEAR +8, MASK_STATUS +0xC, MASK_SET +0x10, MASK_CLEAR +0x14 ; bits : 16 `TXDMA_MBDONE` (ring16), 13 `RXDMA_MBDONE`, 4/5 link up/down |
| INTRL2_1 | 0x0240 | idem ; bits 0..15 = TX ring n, bits 16..31 = RX ring n |
| RBUF | 0x0300 | `RBUF_CTRL` 0x300 (bit1 `RBUF_ALIGN_2B`), `RBUF_TBUF_SIZE_CTRL` 0x3B4 (=1) |
| UMAC | 0x0800 | `UMAC_CMD` 0x808, `UMAC_MAC0` 0x80C, `UMAC_MAC1` 0x810, `UMAC_MAX_FRAME_LEN` 0x814 (=1536), `UMAC_TX_FLUSH` 0xB34, `UMAC_MIB_CTRL` 0xD80, `UMAC_MDIO_CMD` 0xE14, `UMAC_MDF_CTRL` 0xE50, `UMAC_MDF_ADDR` 0xE54.. |
| RDMA descripteurs | 0x2000 | 256 × 12 octets (0x2000–0x2BFF) |
| RDMA rings | 0x2C00 | ring n à 0x2C00 + 0x40·n ; **ring 16 = 0x3000** |
| RDMA global | 0x3040 | `RING_CFG` 0x3040, `CTRL` 0x3044, `STATUS` 0x3048, `SCB_BURST_SIZE` 0x304C, `INDEX2RING_0..7` 0x30B0–0x30CC |
| TDMA descripteurs | 0x4000 | 256 × 12 octets |
| TDMA rings | 0x4C00 | ring n à 0x4C00 + 0x40·n ; ring 0 = 0x4C00, ring 16 = 0x5000 |
| TDMA global | 0x5040 | `RING_CFG` 0x5040, `CTRL` 0x5044, `SCB_BURST_SIZE` 0x504C, `ARB_CTRL` 0x506C, `PRIORITY_0..2` 0x5070–0x5078 |
| HFB | 0x8000 / 0xFC00 | filtres matériels : remis à zéro puis désactivés (`hfb_init`, 6144 mots à 0) |

Calcul (`bcm54213.cpp` l.80-84, 461-515) : `GENET_xDMA_REG_OFF = xDMA_OFFSET + 256*12`, `DMA_RINGS_SIZE = 0x40*17 = 0x440`, tables `dma_regs[]` (v3+) et `genet_dma_ring_regs[]` (v4+).

**Registres d'un ring** (offset dans le bloc ring, `genet_dma_ring_regs`) :
`0x00` RX:WRITE_PTR / TX:READ_PTR, `0x04` (HI), `0x08` RX:**PROD_INDEX** / TX:**CONS_INDEX** (HW), `0x0C` RX:**CONS_INDEX** / TX:**PROD_INDEX** (SW), `0x10` RING_BUF_SIZE (`size<<16 | 2048`), `0x14` START_ADDR, `0x1C` END_ADDR (en **mots**), `0x24` MBUF_DONE_THRESH, `0x28` TX:FLOW_PERIOD / RX:XON_XOFF, `0x2C` TX:WRITE_PTR / RX:READ_PTR.

**Descripteur (3 mots, dans la RAM de descripteurs du GENET = MMIO)** :
```
+0 LENGTH_STATUS : [31:16] longueur, [15] OWN, [14] EOP, [13] SOP, [12] WRAP,
                   TX : [12:7] QTAG(0x3F), [6] APPEND_CRC ; RX : [4] LG, [3] NO, [2] RXER, [1] CRC_ERR, [0] OV
+4 ADDRESS_LO    : adresse physique ARM du tampon (Circle n'applique PAS BUS_ADDRESS, l.1551)
+8 ADDRESS_HI    : 0
```

### 1.3 Séquence d'initialisation (`Initialize()` l.574-650)
1. **Probe** : `SYS_REV_CTRL` → major = bits 27:24 ; 6 ⇒ v5, sinon échec.
2. `reset_umac` : `RBUF_FLUSH_CTRL=0`, 10 µs ; `UMAC_CMD=0` ; `UMAC_CMD=CMD_SW_RESET|CMD_LCL_LOOP_EN` (bits 13|15), 2 µs ; `UMAC_CMD=0`.
3. `umac_reset2` : `RBUF_FLUSH_CTRL |= 2`, 10 µs, `&= ~2`, 10 µs.
4. `init_umac` : reset_umac ; `MIB_CTRL=7` puis 0 ; `MAX_FRAME_LEN=1536` ; `RBUF_CTRL |= RBUF_ALIGN_2B` ; `TBUF_SIZE_CTRL=1` ; masquer/acquitter toutes les IRQ (INTRL2_0/1 MASK_SET=CLEAR=0xFFFFFFFF).
5. Lire `UMAC_CMD.CRC_FWD` (bit 6) → savoir s'il faut retirer 4 octets de FCS en RX.
6. **MAC** : mailbox VideoCore tag `0x00010003` (GET_MAC_ADDRESS) → `UMAC_MAC0 = m0<<24|m1<<16|m2<<8|m3`, `UMAC_MAC1 = m4<<8|m5`.
7. `dma_disable` : TDMA/RDMA `CTRL &= ~(DMA_EN | 1<<17)` ; `UMAC_TX_FLUSH=1`, 10 µs, `=0`.
8. `init_dma` :
   - RDMA `SCB_BURST_SIZE=8` ; **ring RX 16** sur les 256 descripteurs : pour chaque descripteur, tampon 2048 o, `dc civac` sur le tampon, écrire ADDRESS_LO/HI ; ring : PROD=0, CONS=0, BUF_SIZE=`256<<16|2048`, XON_XOFF=`5<<16|16`, START=0, READ_PTR=0, WRITE_PTR=0, END=`256*3-1=767` ; `RING_CFG = 1<<16` ; `CTRL = 1<<17` (DMA_EN conservé s'il l'était).
   - TDMA `SCB_BURST_SIZE=8` ; `ARB_CTRL=2` (strict priority) ; rings TX 0..3 (32 desc. chacun, desc. 0–127) + ring 16 (128 desc., 128–255). Par ring : PROD=CONS=0, MBUF_DONE_THRESH=10, FLOW_PERIOD=`1536<<16` (0 pour ring16), BUF_SIZE=`size<<16|2048`, START=READ=WRITE=`start*3`, END=`end*3-1` ; PRIORITY_0..2, `RING_CFG = 0x1000F`, `CTRL = bits 1..4 | bit17`.
9. `enable_dma` : RDMA/TDMA `CTRL |= DMA_EN | 1<<17`.
10. `hfb_init` : HFB_CTRL=0, FLT_ENABLE=0, INDEX2RING=0, 12 FLT_LEN=0, 6144 mots HFB=0.
11. Connexion IRQ 189/190 (Circle) — **optionnel** en asm.
12. `mii_probe` : PHY id=1 ; lecture factice BMSR (contournement bug MDIO 1er accès) ; `phy_read_status` ; `mii_setup` ; `mii_config` : `SYS_PORT_CTRL=3`, `EXT_RGMII_OOB_CTRL |= RGMII_MODE_EN | ID_MODE_DIS`.
13. `netif_start` : `UMAC_CMD |= TX_EN|RX_EN` (bits 0|1) ; activer IRQ TX (ring16 : INTRL2_0 MASK_CLEAR bit16 ; rings 0–3 : INTRL2_1 MASK_CLEAR bits 0–3).
14. `set_rx_mode` : `CMD_PROMISC=0` ; filtre MDF : broadcast puis MAC propre puis multicast ; chaque entrée = 2 mots à `MDF_ADDR + 8·k` (`a0<<8|a1`, `a2<<24|..|a5`) et `MDF_CTRL |= 1<<(16-k)`.

### 1.4 PHY / MDIO (l.1790-1933)
- Commande `UMAC_MDIO_CMD` (0xE14) : `[29] START_BUSY`, `[28] READ_FAIL`, `[27:26] op (2=RD, 1=WR)`, `[25:21] PHY addr`, `[20:16] reg`, `[15:0] data`. Écrire la commande, puis RMW `|START_BUSY`, attendre que bit 29 retombe (timeout 10 ms), lire.
- `phy_read_status` : BMSR(1) bit2 = lien ; STAT1000(0x0A) & CTRL1000(0x09)<<2 → 1000 FD/HD ; LPA(5) & ADVERTISE(4) → 100/10 ; pause = LPA bit10 si FD. **L'auto-négociation est lancée par le firmware VideoCore**, Circle ne programme jamais BMCR.
- `mii_setup` (au changement) : `EXT_RGMII_OOB_CTRL = (&~OOB_DISABLE) | RGMII_LINK` ; `UMAC_CMD` bits 3:2 = vitesse (0=10, 1=100, 2=1000), bit10 `HD_EN` si half-duplex, bits 8|28 si pas de pause.
- Le lien est **sondé toutes les 2 s** par une tâche (`lib/net/phytask.cpp` → `UpdatePHY()`); les IRQ de lien « ne marchent pas » (commentaire l.1149).

### 1.5 Datapath
- **TX** (`SendFrame` l.665) : ring TX 0 (`TX_RING_INDEX=1` ⇒ ring 0, desc. 0–31). Copier la trame dans un tampon DMA de 1536 o, padding à 60 o, `dc civac`, écrire `ADDRESS_LO/HI` puis `LENGTH_STATUS = len<<16 | 0x3F<<7 | APPEND_CRC | SOP | EOP`, `prod_index = (prod_index+1) & 0xFFFF`, écrire `TDMA_PROD_INDEX` (0x4C0C). Récupération (`tx_reclaim`) : `TDMA_CONS_INDEX` (0x4C08) − c_index = tampons libérés. Circle la fait en IRQ ; en asm, **la faire au début de chaque envoi** (polling) suffit.
- **RX** (`ReceiveFrame` l.740) : `p = RDMA_PROD_INDEX` (0x3008) ; bits 31:16 = compteur de rejets (remise à 0 si ≥ 0xC000) ; si `(p - c_index) & 0xFFFF > 0` : lire LENGTH_STATUS du descripteur `read_ptr`, vérifier SOP+EOP et erreurs, `len -= 2` (pad d'alignement IP en tête), `-4` si CRC_FWD, copier depuis `buffer+2`, avancer `read_ptr` (bouclage 255→0), `c_index++`, écrire `RDMA_CONS_INDEX` (0x300C). **Pas d'IRQ RX** (polling depuis la boucle réseau).
- Circle réalloue un tampon RX à chaque trame (`rx_refill`) : inutile en asm — tampons statiques, `dc ivac/civac` avant lecture, rendre le même tampon.

### 1.6 Pièges
- Descripteurs = MMIO (Device-nGnRE) : pas de cache, mais **les tampons** sont en RAM cacheable → `dc cvac` avant TX, `dc ivac` (ou civac) avant de lire un tampon RX, `dsb sy` avant d'écrire PROD/CONS_INDEX.
- Tampons alignés 64 o (ligne de cache A72), adresse physique < 4 Go (ADDRESS_HI=0).
- Commentaire Circle : « should use BUS_ADDRESS() here, but does not work » → le GENET voit les adresses physiques ARM directement.
- Ordre : écrire ADDRESS avant LENGTH_STATUS ; `umac_enable_set(..., false)` attend 2 ms (trame max).
- Trame RX décalée de 2 octets (RBUF_ALIGN_2B) ; trame TX < 60 o à padder.
- Taille mémoire : 256 × 2 Ko RX = 512 Ko + 128×1,5 Ko TX ; on peut réduire le ring RX (ex. 64 desc. = 128 Ko) en adaptant `END_ADDR` et `BUF_SIZE`.
- Pas de config BMCR : si on veut forcer la vitesse/relancer l'autoneg, ajouter `mdio_write(0, 0x1200)`.

### 1.7 Plan de transposition asm (procédural)
Modules : `genet_init`, `genet_mdio_rd/wr`, `genet_phy_poll` (appelée toutes les ~2 s par le timer), `genet_tx`, `genet_rx_poll`, `genet_reclaim`. État global dans une structure fixe en `.bss` (rx_cidx, rx_rdptr, tx_pidx, tx_cidx, tx_free, link, speed, duplex, mac[6]).
Estimation : init ~350 instr., MDIO+PHY ~150, TX/RX/reclaim ~200, utilitaires (delay CNTPCT, cache) ~60 → **~800 instructions ≈ 3,2 Ko**, **~1000–1300 lignes de source** avec tables de constantes. Prérequis : mailbox VC (MAC), timer générique (`CNTPCT_EL0`, 54 MHz), MMU avec mémoire périphérique Device (0xFD580000).

---

## 2. WiFi CYW43455 (BCM4345/6) sur SDIO — ce que fait Circle

### 2.1 Architecture logicielle Circle
`CBcm4343Device` (`bcm4343.cpp`) est une fine couche C++ au-dessus du pilote **Plan 9** `ether4330.c` (R. Miller, GPLv2 via Plan9License) et de l'hôte SD Plan 9 `emmc.c`, avec une émulation Plan 9 (`p9arch/p9chan/p9proc/p9ether/p9util` : `qlock`, `sleep/wakeup`, `kproc`, `Block`, `waserror`…). Deux threads noyau : `wifireader` (`rproc`, réception/dispatch) et `wifitimer` (`lproc`, scan périodique). Les commandes passent par une interface texte (`Control("join ...")`, `etherbcmctl` l.2488) héritée de `/net/ether0/ctl` de Plan 9.

Table firmware (`ether4330.c` l.~290) : pour RPi4 `{0x4345, rev 6, "brcmfmac43455-sdio.bin", ".txt", ".clm_blob"}`.

### 2.2 Câblage RPi4
- Contrôleur : EMMC legacy Arasan SDHCI `EMMCREGS = VIRTIO+0x300000 = 0xFE300000` (`emmc.c:28`), IRQ `ARM_IRQ_ARASANSDIO = GIC_SPI(126)` → INTID 158 (`bcm2711int.h:60`). La carte SD est sur EMMC2 (0xFE340000) → **pas de conflit** sur Pi4 (le `#warning` de `bcm4343.cpp` ne concerne que RASPPI ≤ 3).
- `sdioinit()` (`ether4330.c` l.~555) : GPIO 48–53 → ALT0 (déconnecte l'EMMC de l'ancien routage SD), GPIO **34–39 → ALT3** ; pin 34 (CLK) sans pull, 35–39 (CMD, D0–D3) pull-up.
  - GPFSEL3 (`0xFE20000C`, pins 30–39, 3 bits/pin, ALT3 = `0b111`).
  - Pull BCM2711 : `GPIO_PUP_PDN_CNTRL_REG2` (`0xFE2000EC`, pins 32–47, 2 bits/pin, `01` = pull-up, `00` = aucun) — `p9arch.cpp` gpiopull, RASPPI==4.
- Alimentation `WL_REG_ON` : sur Pi4 elle est sur l'expandeur GPIO du VideoCore (expgpio 1) et activée par le firmware ; Circle **ne la touche pas** (sur Pi5 il pilote GPIO28). Ne pas mettre `dtoverlay=disable-wifi` dans `config.txt`. Si besoin : mailbox `SET_GPIO_STATE` (0x00038041) GPIO 128+1 (non utilisé par Circle — à vérifier).
- Horloge de base : mailbox `GET_CLOCK_RATE` id 1 (EMMC) (`getclkrate(ClkEmmc)`), repli 100 MHz.

### 2.3 Hôte SDHCI (`emmc.c`)
Registres (offsets, nommage Broadcom ≈ SDHCI standard) : `ARG2` 0x00, `BLKSIZECNT` 0x04 (`count<<16 | size`), `ARG1` 0x08, `CMDTM` 0x0C (Transfer Mode + Command en **une écriture 32 bits**), `RESP0..3` 0x10–0x1C, `DATA` 0x20, `STATUS` 0x24 (bit0 CMD_INHIBIT, bit1 DAT_INHIBIT), `CONTROL0` 0x28 (bit1 bus 4 bits, bit2 high-speed), `CONTROL1` 0x2C (bit0 CLK_INTLEN, bit1 CLK_STABLE, bit2 CLK_EN, 15:8 + 7:6 diviseur 10 bits, 19:16 timeout données, bits 24/25/26 resets HC/CMD/DATA), `INTERRUPT` 0x30 (W1C), `IRPT_MASK` 0x34 (= status enable), `IRPT_EN` 0x38 (= signal enable vers GIC), `CONTROL2` 0x3C, `SLOTISR_VER` 0xFC.

Bits INTERRUPT : 0 CMD_DONE, 1 DATA_DONE, 4 WRITE_RDY, 5 READ_RDY, **8 CARD (SDIO)**, 15 ERR, 16 CTO, 17 CCRC, 18 CEND, 19 CBAD, 20 DTO, 21 DCRC, 22 DEND, 24 ACMD.

`cmdinfo[]` (CMDTM, hors index) : CMD0 `Ixchken` ; CMD3 `Resp48|Ixchk|Crcchk` ; **CMD5 `Resp48`** (pas de CRC : R4) ; CMD7 `Resp48busy|...` ; **CMD52 `Resp48|Ixchk|Crcchk` = 0x001A0000** ; **CMD53 idem + `Isdata` (bit21)**, + `Card2host` (bit4) si lecture, + `Multiblock|Blkcnten` (bits 5|1) si count ≠ 1.

Séquences :
- `emmcinit` : `CONTROL1 = SRST_HC` ; 10 ms ; attendre bit24 = 0 ; `CONTROL1 = SRST_DATA` ; 10 ms ; `CONTROL1 = 0`.
- `emmcenable` → `emmcclk(400 kHz)` : `div = ext/(2f)` arrondi au-dessus, `CONTROL1 = clkdiv(div) | 14<<16 | CLK_EN | CLK_INTLEN`, puis attente `CLK_STABLE` (≤ 1000 × 1 ms) ; `IRPT_EN=0`, `IRPT_MASK=~0`, `INTERRUPT=~0`.
- `emmccmd` : si `CMD_INHIBIT` → reset CMD ; si `DAT_INHIBIT` et commande data/busy → reset DATA ; `ARG1=arg` ; acquitter interruptions pendantes (sauf CARD) ; `CMDTM = idx<<24 | flags` ; attendre `CMD_DONE|ERR` (≤ 1 s) ; erreur → reset CMD + exception ; acquitter (sauf DATA_DONE/RDY) ; lire RESP0. Effets de bord : après CMD7 → horloge **25 MHz** ; après **CMD52 écriture CCCR 0x07** (`arg & ~0xFF == 0x80000E00`) avec valeur 2 → `CONTROL0 |= 4 bits` (high-speed laissé commenté).
- `emmciosetup` : `BLKSIZECNT = count<<16 | size`.
- `emmcio` (Pi4) : **DMA legacy** canal « normal », DREQ 11 (`DmaDevEmmc`), adresses bus `0xC0000000|phys` (`BUS_ADDRESS`) vs `0x7E300020` ; si le tampon n'est pas aligné 64 o ou len non multiple de 64 → tampon de rebond 4 Ko ; invalidation cache après lecture. Puis attente `DATA_DONE` (≤ 3 s). Sur Pi5, PIO : attendre `READ_RDY/WRITE_RDY`, transférer `blksize/4` mots via `DATA`. **En asm, commencer en PIO** (pas de moteur DMA ni de cohérence à gérer).
- `sdiocardintr` : acquitte CARD, attend bit 8 (en activant `IRPT_EN.CARD` et dormant), ré-acquitte.
- Piège majeur : `WR()` insère **2 µs (horloge rapide) / 20 µs (400 kHz) avant chaque écriture de registre** + `dsb` (errata Arasan/BCM : deux écritures dans moins de 2 cycles SD sont perdues). Accès **32 bits uniquement**.

### 2.4 Énumération SDIO (`sdioinit`, `ether4330.c`)
1. `CMD0` (GO_IDLE) — reset largeur/fréquence côté hôte.
2. `CMD5` arg 0 (via `trysdiocmd`, ignore timeout) puis jusqu'à 5× `CMD5` arg `V3_3 = 3<<20` toutes les 100 ms jusqu'à OCR bit31 (prêt).
3. `CMD3` → RCA = resp>>16 ; `CMD7` arg `RCA<<16` (→ 25 MHz).
4. CMD52 Fn0 : `CCCR 0x13 |= 2` (EHS), `CCCR 0x07 |= 2` (bus 4 bits → l'hôte suit), FBR1 `0x110/0x111 = 64` (taille bloc F1), FBR2 `0x210/0x211 = 512` (taille bloc F2), `CCCR 0x02 |= 1<<1` (IOEN F1), `CCCR 0x04 = 0` (INTEN), attendre `CCCR 0x03` (IORDY) bit1 (≤ 10×100 ms).

**CMD52** : arg = `W<<31 | fn<<28 | RAW<<27 | addr(17)<<9 | data`. Réponse R5 : bits 15:8 = flags, Circle teste `resp & 0xCF00` (CRC, illegal cmd, error, fn number, out of range) ; écriture ré-essayée 10×.
**CMD53** (`sdiorwext`) : arg = `W<<31 | fn<<28 | blk<<27 | incr<<26 | addr<<9 | count(9)`. Taille bloc F1 = 64, F2 = 512. `len ≥ 511·bsize` → 511 blocs ; `len > bsize` → `len/bsize` blocs ; sinon mode octet (`count = len`, 512 encodé 0). F1 : adresse incrémentale ; F2 : adresse fixe 0 (FIFO), `incr=0`. **len multiple de 4 obligatoire**.

### 2.5 Backplane Broadcom (« Silicon Backplane »/AXI)
- Fenêtre 32 Ko : registres F1 `SBADDR` 0x1000A/B/C ← bits 15..31 de l'adresse (`sbwindow` écrit `addr>>8`, `>>16`, `>>24` après masquage `~0x7FFF`). Accès 32 bits : CMD53 F1 à `(addr & 0x7FFF) | 0x8000` (`Sb32bit`). `cfgreadl/cfgwritel` utilisent un tampon de 4 o **aligné 64** sur la pile (`ROUND(cbuf, CACHELINESZ)`) ; écriture ré-essayée 3× avec `sdioabort`.
- Autres registres F1 : `FRAMECTL` 0x1000D (RF_HALT 1, WF_HALT 2), **`CLKCSR` 0x1000E** (ForceALP 1, ForceHT 2, ForceILP 4, ReqALP 8, ReqHT 0x10, NoHWReq 0x20, ALPavail 0x40, HTavail 0x80), `PULLUPS` 0x1000F, `WFRMCNT` 0x10019, `RFRMCNT` 0x1001B.
- `sbinit` :
  1. `chipid = read32(0x18000000) & 0xFFFF` → **0x4345**, rev = bits 19:16 → **6** (CYW43455).
  2. EROM : pointeur à `0x18000000 + 63*4` ; `corescan` lit 512 o et parse les entrées (type `buf[i]&0xF` : 1 = core info (id 12 bits, rev), 5 = adresse ; bits 7:6 ≠ 0 ⇒ wrapper « ctl », sinon registres). Cœurs retenus : 0x800 ChipCommon, **0x83E ARM CR4** (43455), 0x829 SDIO device (`sdregs`), 0x812 D11 (MAC 802.11), 0x80E SOCRAM (absent sur CR4).
  3. Wrapper : `IOCTRL` +0x408, `RESETCTRL` +0x800. `sbdisable` / `sbreset` (RESETCTRL=1, 10 µs, attente ; puis RESETCTRL=0 boucle de 40 µs ; IOCTRL = 1|ioctl). CR4 : `sbreset(armctl, CPUHALT=0x20, CPUHALT)` (CPU maintenu à l'arrêt) ; `sbreset(d11ctl, 8|4, 4)`.
  4. `ramscan` (CR4) : `CR4_CAP` (armregs+0x04) → banks = `((n>>4)&0xF)+(n&0xF)` ; pour chaque bank : `BANKIDX` (+0x40) = i, `BANKINFO` (+0x44) → `8192*((n&0x3F)+1)` ; **`rambase = 0x198000`**.
  5. Horloge : `CLKCSR=0`, 10 µs ; `CLKCSR = NoHWReq|ReqALP`, attendre ALP/HT avail ; `CLKCSR = NoHWReq|ForceALP`, 65 µs ; `PULLUPS=0` ; ChipCommon `GPIOPULLUP` (+0x58) = `GPIOPULLDOWN` (+0x5C) = 0. (Réglage drive-strength via `Chipctladdr/data` uniquement pour 4330/43362.)
- `fwload` :
  1. `CLKCSR=ReqALP`, attendre ALPavail ; écrire 0 au dernier mot de RAM (`rambase+ramsize-4`).
  2. `upload(.bin)` par blocs de 2048 o à `rambase+off` via `sbmem` (découpe en fenêtres 32 Ko) ; **le 1er mot (vecteur de reset) est mémorisé**. Vérification par relecture complète (`Firmwarecmp=1`, double le temps).
  3. `upload(.txt)` : `condense()` (supprime `#…`, CR, lignes vides ; `\n`→`\0` ; double `\0` final ; padding à 4) puis écrit à `rambase + ramsize - n - 4` ; dernier mot = `(n/4) | (~(n/4))<<16`.
  4. CR4 : fenêtre `sdregs`, `INTSTATUS=~0` ; écrire le vecteur de reset à l'adresse 0 ; `sbreset(armctl, CPUHALT, 0)` → **le CPU du WiFi démarre**.
- `sbenable` : `CLKCSR=0`, 1 ms, `CLKCSR=ReqHT`, attendre HTavail (≤ 50×100 ms), `|= ForceHT`, 10 ms ; `sdregs+0x48 (SBMBOXDATA) = 4<<16` (version protocole SDPCM) ; `sdregs+0x24 (INTMASK) = FrameInt(1<<6)|MailboxInt(1<<7)|Fcchange(1<<5)` ; `CCCR IOEN |= 1<<2` ; attendre IORDY F2 (≤ 1 s) ; `CCCR INTEN = 1|2|4` (master+F1+F2).
- `reguload(.clm_blob)` : iovar `clmload` par paquets de 1400 o, en-tête 12 o `flag(2) type(2)=2 len(4) crc(4)=0`, flag = `0x1000` (CLM) | `0x2` (premier) | `0x4` (dernier), dernier paquet paddé à 8. **Obligatoire** sur 43455 (sinon pas de canaux/scan vide).

### 2.6 Protocole SDPCM / BCDC
Toute trame passe par F2, adresse fixe, découpée en 2048 o (`packetrw`, tampon de rebond si len non multiple de 4).

En-tête SDPCM (12 o, `struct Sdpcm`) : `len(2) ~len(2) seq(1) chan(1) nextlen(1) doffset(1) fcmask(1) window(1) version(1) pad(1)`. Canaux (`chan & 0xF`) : **0 = contrôle**, **1 = événement**, **2 = données**.

- **Contrôle (ioctl, BCDC/CDC)** — `wlcmd` : SDPCM (doffset=12) + `Cmd` 16 o `cmd(4) len(4) flags(2) id(2) status(4)` + données. `flags = 2` si SET. `GetVar=262`/`SetVar=263` : données = `"nom\0" + valeur`. Réponse sur canal 0 avec le même `id` ; `status ≠ 0` → erreur. Ioctls utilisés : UP 2, DOWN 3, SET_PROMISC 10, SET_INFRA 20 (0x14), SET_SSID 26, SET_CHANNEL 30, SET_PASSIVE_SCAN 49, DISASSOC 52, SET_PM 86 (0x56), SET_AP 118, 0xB9/0xBB/0x102 (temps de scan).
- **Données** — `txstart` : `doffset = 12 + (rp & 3)`, puis en-tête BDC 4 o (`0x20` = version 2, prio 0, flags2 0, data_offset 0), puis trame Ethernet. RX : `bdc = 4 + 4*b[doffset+3]`, trame à `doffset + bdc`.
- **Contrôle de flux** : `window` (crédit) et `fcmask` reçus dans chaque en-tête ; émission données bloquée si `txseq == txwindow` ou `fcmask & (1<<2)`. `txseq` est commun contrôle+données (les ioctls ne vérifient pas le crédit).
- **Réception** (`rproc` + `intwait`) : boucle `wlreadpkt` (lire 12 o d'en-tête, si `len==0` plus rien → attendre) ; contrôle `lenck == ~len`, sinon `FRAMECTL=RF_HALT` et attendre `RFRMCNT` à 0. Attente : `sdiocardintr` (bit CARD du SDHCI = interruption DAT1), `CCCR 0x05` (INTPEND), lire/acquitter `sdregs+0x20 INTSTATUS` ; `MailboxInt` → lire `HOSTMBOXDATA` (+0x4C), ack `SBMBOX` (+0x40) = 2 (bit 0x8 → « firmware ready ») ; `FrameInt` → relire.
- **Événements** (`bcmevent`) : après BDC : en-tête Ethernet 14 o + en-tête BRCM 10 o, puis `wl_event_msg` big-endian : `version(2) flags(2) event_type(4, Circle lit le demi-mot bas @6) status(4 @8) reason(4 @12) auth_type(4) datalen(4) addr(6 @24) ifname(16) …` (48 o). Gérés : 0 SET_SSID (fin de join, `joinstatus = 1+status`), 5/6 DEAUTH, 12 DISASSOC_IND, 16 LINK (flags&1 = up), 17 MIC_ERROR, 19 ROAM, 69 ESCAN_RESULT. Masque `event_msgs` (16 o) : tout sauf 20, 40, 44, 54, 71, 124.

### 2.7 Initialisation logique (`wlinit`) et attachement
Ordre dans `etherbcmattach` : `sdioinit` → `sbinit` → `fwload` → `sbenable` → threads → `reguload` → `wlinit`.
`wlinit` : `cur_etheraddr` (GET → MAC OTP) ; `assoc_listen=10` ; PM off (ioctl 86 = 0) ; `bus:txglom=0` ; `bcn_timeout=10` ; `assoc_retry_max=3` ; `event_msgs` ; temps de scan ; UP ; `ver` ; `roam_off=1` ; SET_INFRA 1 ; SET_PROMISC 0 ; UP.

### 2.8 Scan et join (open)
- `wlscanstart` → iovar `escan` : `version=1, action=1 (START), sync_id=0x1234, ssid_le(4+32)=vide, bssid=FF…, bss_type=2, scan_type=0, nprobes/active/passive/home=-1, nchans=14 | nssids<<16, chanspecs 2,4 GHz (0x2B01…0x2B0E, canaux 5–7 en 0x2Exx), ssids[]` ; précédé de PASSIVE_SCAN(49)=0. Résultats : événements 69, `escan_result` (`buflen, version, sync_id, bss_count`) + `wl_bss_info` (BSSID @8, capability @16, SSID_len @18, SSID @19, chanspec @72, RSSI @78, noise @80, ie_offset @0x74, ie_len @0x78) ; status 8 = partiel, 0 = fin. Circle met les résultats bruts en file (`ReceiveScanResult`).
- `JoinOpenNet` = `Control("join SSID FFFFFFFFFFFF 0 off")` → `auth=0` → `wljoin` : iovar `join` (72 o, 68 sans canal) = `ssid_le` + scan params (`0xff`, probes/temps) + bssid + `nchans` (+ chanspec `chan|0x2B00`) ; attente E_SET_SSID (status 0 OK, 1 échec, 3 réseau introuvable).
- AP ouvert (`wlcreateAP`) : DOWN, SET_INFRA 1, SET_AP 1, SET_CHANNEL, UP, SET_SSID, `closednet`.

### 2.9 WPA2 : que fait exactement Circle ?
**Circle n'utilise PAS le supplicant interne du firmware.** Aucune occurrence de `sup_wpa` ni de `WSEC_PMK` dans le dépôt (grep). Il porte `wpa_supplicant` de hostap (sous-module `rsta2/hostap`, `Makefile.circle`, classe `CWPASupplicant`, cf. `sample/hello_wlan/kernel.cpp`, `README`, `wpa_supplicant.conf`). Le sous-module n'est **pas initialisé** localement (dossier vide) ; le détail du « driver circle » de hostap n'a donc pas pu être lu. Ce que montre le pilote :
- Le supplicant hôte pilote la puce par les commandes texte : `escan`, `join <ssid> <bssid> <chan> <RSN-IE hex>` → `setauth` : iovar `wpaie`, `wpa_auth = 0x80|0x40` puis `0x80` (WPA2-PSK), `auth = 0`, `wsec = 4` (AES) → `wljoin`.
- Le 4-way handshake (trames EAPOL, ethertype 0x888E) circule sur le **canal données** et est traité **par l'hôte** (PBKDF2-SHA1 4096 itérations, PRF-SHA1, MIC HMAC, AES key unwrap).
- Installation des clés : commandes `txkey`/`rxkey0..3 <ea> ccmp:<hex>@<iv>` → `wlwpakey` → iovar `wsec_key` (164 o, algo 4 = AES_CCM, 2 = TKIP). Événement `E_MIC_ERROR` remonté au supplicant ; `IsLinkUp` délègue à `m_pIsConnected` fourni par le supplicant.

**Recommandation pour l'OS asm : supplicant interne (« idsup ») de la puce**, non utilisé par Circle mais employé par Infineon WHD et le pilote cyw43 (Pico W). Séquence (ioctls/iovars Broadcom, à valider) :
1. `GetVar "cap"` → vérifier la présence de `idsup` dans la chaîne (sinon essayer `cyfmac43455-sdio-standard.bin` plutôt que `-minimal`).
2. ioctl 134 `SET_WSEC` = 4 (AES).
3. iovar `bsscfg:sup_wpa` = `{u32 bsscfg=0, u32 1}` ; `bsscfg:sup_wpa2_eapver` = `{0, -1}` ; `bsscfg:sup_wpa_tmo` = `{0, 2500}`.
4. ioctl 268 `SET_WSEC_PMK` : `{u16 key_len, u16 flags=1 (WSEC_PASSPHRASE), u8 key[64]}` (passphrase 8–63 caractères).
5. ioctl 20 SET_INFRA=1, ioctl 22 SET_AUTH=0, ioctl 165 SET_WPA_AUTH=0x80.
6. iovar `join` (comme §2.8) ou ioctl 26 SET_SSID.
7. Attendre E_SET_SSID (0) status 0, E_LINK (16) up, et **E_PSK_SUP (46) status 6 (WLC_SUP_KEYED)** ; autre status/timeout = mauvais mot de passe.
Gain : supprime tout code crypto côté hôte (sinon +2500–4000 lignes asm : SHA-1, HMAC, PBKDF2, PRF, AES-128 + key wrap, parseur EAPOL/RSN).

### 2.10 Pièges WiFi (récapitulatif)
- **Alignement** : tampons CMD53 en mots (len multiple de 4) ; en DMA, alignés 64 o et len multiple de 64 sinon rebond ; tampons de 4 o pour `cfgreadl` alignés 64.
- **Taille de bloc** : F1 = 64, F2 = 512 ; count 9 bits (max 511 blocs, 512 octets = 0 en mode octet) ; `BLKSIZECNT` à jour avant **chaque** CMD53 ; multi-bloc dès que count ≠ 1.
- **Cache/DMA** : le moteur DMA legacy voit la RAM à `0xC0000000|phys` et le périphérique à `0x7E300020` ; invalider après lecture. Le PIO évite tout cela.
- **Timings** : 2/20 µs entre écritures SDHCI ; reset HC 10 ms ; CMD5 5×100 ms ; IORDY 10×100 ms ; ALP 10 µs ×n + 65 µs ; HT ≤ 5 s ; sbreset 10/40 µs ; DATA_DONE ≤ 3 s ; CMD_DONE ≤ 1 s.
- Firmware : ne pas oublier le vecteur de reset (1er mot) réécrit en 0 avant de libérer le CR4 ; mot de longueur nvram `(n/4)|~(n/4)<<16` ; CLM obligatoire ; `rambase=0x198000`.
- Erreurs : ré-essais CMD52 (10×), cfgwritel (3× + `CCCR 0x06 IOABORT`), packetrw (3× + abort F2) ; trames corrompues → RF_HALT ; erreur TX → WF_HALT.
- Concurrence : Circle bloque l'ioctl jusqu'à ce que le thread lecteur reçoive la réponse. En asm mono-thread : envoyer, puis **boucler sur la réception** en dispatchant événements/données jusqu'à la réponse de même `id`.
- L'interruption CARD (bit 8) est de niveau : acquitter la source (INTSTATUS du cœur SDIO) avant le W1C côté hôte.
- `chipid` 43455 lu comme 0x4345 (hex) mais 43430 en décimal : la table firmware mélange les deux.

---

## 3. Fichiers firmware et licences

- Circle (`addon/wlan/firmware/Makefile`) télécharge depuis **`https://github.com/RPi-Distro/firmware-nonfree`**, commit `c9d3ae6584ab79d19a4f94ccf701e888f9f87a53` (8 avril 2025), répertoire `debian/config/brcm80211/` :
  - `brcmfmac43455-sdio.bin` ← `cypress/cyfmac43455-sdio-minimal.bin` (variante « minimal » ; il existe aussi `-standard.bin`) — ordre de grandeur ~600 Ko ;
  - `brcmfmac43455-sdio.txt` ← `brcm/brcmfmac43455-sdio.txt` (nvram, ~2 Ko) ;
  - `brcmfmac43455-sdio.clm_blob` ← `cypress/cyfmac43455-sdio.clm_blob`.
  Les fichiers ne sont **pas** présents dans le clone (seulement `Makefile` + `.gitignore`). Circle les lit depuis `SD:/firmware/` via FatFs.
- Autre source : `linux-firmware` (kernel.org, répertoires `cypress/` et `brcm/`).
- **Licences** : `LICENCE.cypress` (Cypress/Infineon) et `LICENCE.broadcom_bcm43xx` dans les mêmes dépôts : redistribution autorisée **sous forme binaire non modifiée**, uniquement pour usage avec les puces du fabricant, sans rétro-ingénierie. Compatible avec une inclusion dans une image d'OS (en joignant le fichier de licence), pas avec une modification.
- Côté code : `ether4330.c`/`emmc.c` = Plan 9 sous **GPLv2** (`addon/wlan/Plan9License`) ; `bcm54213.cpp` = dérivé Linux **GPLv2** ; Circle = GPLv3. Une transposition ligne à ligne en asm est une œuvre dérivée → licence GPL à prévoir pour l'OS (ou réécriture « clean-room » à partir de la description de ce document).
- Astuce asm : embarquer les trois fichiers avec la directive FASM `file 'brcmfmac43455-sdio.bin'` (alignés 4) pour démarrer le WiFi **avant** d'avoir un pilote SD/FAT ; passer ensuite au chargement depuis EMMC2.

---

## 4. Pseudo-asm AArch64 (FASMARM) : CMD52 sur l'hôte SDHCI legacy

```asm
; --- constantes ---
EMMC_BASE      = 0xFE300000
R_BLKSIZECNT   = 0x04
R_ARG1         = 0x08
R_CMDTM        = 0x0C
R_RESP0        = 0x10
R_STATUS       = 0x24
R_INTERRUPT    = 0x30
INT_CMDDONE    = 1 shl 0
INT_CARD       = 1 shl 8
INT_ERR        = 1 shl 15
CMD52_TM       = (52 shl 24) or 0x001A0000   ; Resp48 | CRC chk | Index chk = 0x341A0000
R5_ERRMASK     = 0xCF00
WR_DELAY_TICKS = 108                          ; 2 us a 54 MHz (CNTFRQ RPi4) ; 1080 avant CMD7

; sdio_cmd52 : w0 = 1 ecriture / 0 lecture, w1 = fonction, w2 = adresse 17 bits, w3 = donnee
; sortie     : w0 = octet lu (ou relu), x1 = 0 si OK, sinon code d'erreur
sdio_cmd52:
        stp     x29, x30, [sp, -16]!
        lsl     w4, w0, 31                    ; R/W
        and     w1, w1, 7
        orr     w4, w4, w1, lsl 28            ; fonction
        and     w2, w2, 0x1FFFF
        orr     w4, w4, w2, lsl 9             ; adresse registre
        and     w3, w3, 0xFF
        orr     w4, w4, w3                    ; donnee (ignoree en lecture)
        movz    x5, 0xFE30, lsl 16            ; x5 = EMMC_BASE

        bl      timeout_start                 ; x9 = CNTPCT + 1 s
.inhibit:                                     ; CMD_INHIBIT doit etre a 0
        ldr     w6, [x5, R_STATUS]
        tbz     w6, 0, .free
        bl      timeout_check                 ; b.hs -> .err_to
        b.hs    .err_to
        b       .inhibit
.free:
        ldr     w6, [x5, R_INTERRUPT]         ; purge des IRQ pendantes sauf CARD
        bic     w6, w6, INT_CARD
        cbz     w6, .arg
        bl      wr_delay
        str     w6, [x5, R_INTERRUPT]         ; W1C
.arg:
        bl      wr_delay                      ; errata : >= 2 cycles SD entre 2 ecritures
        dsb     sy
        str     w4, [x5, R_ARG1]
        bl      wr_delay
        movz    w6, 0x341A, lsl 16            ; CMD52_TM
        dsb     sy
        str     w6, [x5, R_CMDTM]             ; ecriture 32 bits unique = lance la commande

        bl      timeout_start
.poll:
        ldr     w6, [x5, R_INTERRUPT]
        tst     w6, INT_CMDDONE or INT_ERR    ; 0x8001 : immediat logique non encodable
        b.ne    .done                         ;   -> en pratique : mov w7,0x8001 ; tst w6,w7
        bl      timeout_check
        b.hs    .err_to
        b       .poll
.done:
        tbnz    w6, 15, .err_cmd              ; ERR (CTO/CCRC/CEND/CBAD dans 16..19)
        bl      wr_delay
        mov     w7, INT_CMDDONE
        str     w7, [x5, R_INTERRUPT]         ; acquitte CMD_DONE
        ldr     w0, [x5, R_RESP0]             ; reponse R5
        mov     w7, R5_ERRMASK
        tst     w0, w7
        b.ne    .err_r5
        and     w0, w0, 0xFF
        mov     x1, 0
        ldp     x29, x30, [sp], 16
        ret
.err_cmd:                                     ; acquitter + reset ligne CMD (CONTROL1 bit 25)
        bl      wr_delay
        str     w6, [x5, R_INTERRUPT]
        bl      sdhci_reset_cmd
        mov     x1, 1
        b       .out
.err_r5: mov    x1, 2
        b       .out
.err_to: mov    x1, 3
.out:   ldp     x29, x30, [sp], 16
        ret

; wr_delay : attente active sur le compteur generique
wr_delay:
        mrs     x10, cntpct_el0
        ldr     x11, [wr_delay_ticks]         ; 108 (25 MHz) ou 1080 (400 kHz)
        add     x11, x10, x11
.w:     isb
        mrs     x10, cntpct_el0
        cmp     x10, x11
        b.lo    .w
        ret
```
Note de rédaction : en AArch64, `0x8001` et `0xCF00` ne sont pas des immédiats logiques encodables (`tst`/`and`) ; les charger dans un registre. `0x1FFFF`, `0xFF`, `7` le sont. Pour CMD53 : écrire `BLKSIZECNT = count<<16 | size` (avec délai), ajouter `Isdata` (bit 21), `Card2host` (bit 4) en lecture, `Multiblock|Blkcnten` (0x22) si count ≠ 1, puis boucle PIO sur `READ_RDY`/`WRITE_RDY` (bits 5/4) → `ldr/str w, [x5, 0x20]` × size/4, et enfin attente `DATA_DONE` (bit 1).

---

## 5. Plan de transposition WiFi en asm et estimation

| Module asm | Équivalent Circle | Contenu | Lignes asm estimées |
|---|---|---|---|
| `sdhci.asm` | `emmc.c` | reset, horloge (diviseur 10 bits), `cmd`, `iosetup`, `io` PIO, card-int polling, délais | 350–450 |
| `gpio_sdio.asm` | `sdioinit` (partie GPIO), `p9arch.cpp` | GPFSEL3 ALT3, PUP_PDN | 40 |
| `sdio.asm` | `sdioinit`, `sdiord/wr`, `sdiorwext` | CMD0/5/3/7, CCCR/FBR, CMD52/53 avec découpage blocs | 250–300 |
| `bcm_sb.asm` | `sbwindow`, `cfgreadl/writel`, `sbmem`, `sbreset/disable`, `corescan`, `ramscan`, `sbinit`, `sbenable` | backplane, EROM, horloges ALP/HT | 450–550 |
| `bcm_fw.asm` | `condense`, `upload`, `fwload`, `reguload` | chargement .bin/.txt/.clm (embarqués via `file`), vérif optionnelle | 250–300 |
| `sdpcm.asm` | `packetrw`, `wlreadpkt`, `txstart`, `rproc`, `intwait`, `wlcmd*`, `wlgetvar/setvar` | en-têtes SDPCM/CDC/BDC, crédit, dispatch synchrone | 400–500 |
| `wl.asm` | `wlinit`, `bcmevent`, `wlscanstart`, `wljoin`, `wlsetcountry` + séquence idsup | événements, escan, join open/WPA2-PSK interne | 400–500 |
| **Total** | | | **~2100–2600 lignes, 10–14 Ko de code** + ~600 Ko de firmware |

Jalons de validation :
1. SDHCI + SDIO : lire `0x18000000` → `0x4345`, rev 6.
2. Corescan/ramscan cohérents, firmware + nvram chargés, HT disponible, IORDY F2, mailbox « firmware ready ».
3. ioctl : `GetVar "ver"`, `cur_etheraddr`, `clmload`, `country`, `cap` (vérifier `idsup`).
4. `escan` → liste de BSS ; join ouvert → E_SET_SSID 0 + E_LINK.
5. WPA2-PSK via supplicant interne → E_PSK_SUP status 6 ; trafic ARP/DHCP via la pile IP commune avec GENET.

Prérequis communs : mailbox VC (MAC, horloge EMMC, éventuellement GPIO expandeur), timer `CNTPCT_EL0`, MMU (périphériques en Device-nGnRE, RAM cacheable + routines `dc cvac/ivac/civac`), GIC-400 si passage aux IRQ (GENET INTID 189/190, SDHCI INTID 158), pile ARP/IP/UDP/DHCP partagée (hors périmètre de ce document).

**Recommandation d'ordre** : (1) GENET en polling (~1 semaine-homme, risque faible) ; (2) pile IP sur GENET ; (3) WiFi jalons 1–4 ; (4) WPA2 via idsup. Ne pas porter hostap/wpa_supplicant.
