# Étude 01 — Cœur système de Circle (RPi4, AArch64) et transposition en FASMARM

> Périmètre : `RASPPI=4`, `AARCH=64`, BCM2711, 4× Cortex‑A72. Source analysée : `./ref\circle` (lecture seule).
> Objectif : extraire registres, adresses, séquences et pièges, puis proposer une transposition en assembleur procédural FASMARM (`processor cpu64_v8`, `code64`) pour un OS préemptif multi‑processus façon MenuetOS.
> Toutes les valeurs numériques ci‑dessous sont tirées du code Circle sauf mention « (proposition) ».

---

## 0. Vue d'ensemble et volumétrie

| Sous-système | Fichiers Circle (chemins relatifs à `ref/circle`) | Lignes C/C++/S | Estimation asm FASMARM |
|---|---|---|---|
| Boot / EL2→EL1 | `boot/config64.txt`, `boot/armstub/armstub8.S` (228), `lib/startup64.S` (135), `lib/sysinit.cpp` (450, ~120 utiles en 64 bits), `circle.ld` | ~500 | 150–250 |
| Carte mémoire | `include/circle/memorymap64.h` (129), `bcm2711.h` (97), `bcm2835.h`, `bcm2836.h`, `sysconfig.h` | ~400 de defines | 1 fichier `.inc` de constantes (~200) |
| MMU | `lib/translationtable64.cpp` (164), `lib/memory64.cpp` (235), `include/circle/armv8mmu.h` (178), `translationtable64.h` | ~580 | 300–500 (avec tables par processus + ASID) |
| Exceptions | `lib/exceptionstub64.S` (330), `lib/exceptionhandler64.cpp` (95), `include/circle/exceptionstub.h` | ~425 | 300–400 (trame complète + syscalls) |
| GIC-400 | `lib/interruptgic.cpp` (527), `include/circle/bcm2711int.h` (107) | ~630 | 150–250 |
| Timer | `lib/timer.cpp` (704, dont ~120 pour le timer générique) | ~700 | 100–150 |
| Multicœur / sync | `lib/multicore.cpp` (331), `lib/spinlock.cpp` (137), `lib/synchronize64.cpp` (389), `include/circle/synchronize64.h` (117) | ~970 | 250–350 |
| Ordonnanceur | `lib/sched/scheduler.cpp` (507), `task.cpp` (183), `taskswitch.S` (100, ~50 en 64 bits), `mutex/semaphore/synchronizationevent` (~280) | ~1 070 | 600–900 (préemptif SMP) |
| Mailbox / tags | `lib/bcmmailbox.cpp` (100), `lib/bcmpropertytags.cpp` (108), `include/circle/bcmpropertytags.h` (330) | ~540 | 120–180 |
| Framebuffer | `lib/bcmframebuffer.cpp` (464) | ~460 | 100–150 |
| UART PL011 + GPIO | `lib/serial.cpp` (1043), `lib/gpiopin.cpp` (480), `lib/machineinfo.cpp` (clock UART) | ~1 500 | 120–200 (polling) |

Total cœur Circle ≈ 7 500 lignes ; transposition asm réaliste du cœur (sans pilotes USB/réseau) : **≈ 2 500–3 500 lignes FASMARM**.

---

## 1. Démarrage

### 1.1 config.txt (`boot/config64.txt`)

```
arm_64bit=1
kernel_address=0x80000
initial_turbo=0
[pi4]
armstub=armstub8-rpi4.bin      ; facultatif : uniquement pour le FIQ
kernel=kernel8-rpi4.img
max_framebuffers=2
```

Fichiers firmware nécessaires sur la carte FAT (`boot/README`) : `start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb` (+ `bcm2711-rpi-400.dtb`, `-cm4.dtb`). `bootcode.bin` est inutile sur Pi 4 (EEPROM).

Points clés :
- **Mode « low peripheral »** (défaut en 64 bits) : périphériques ARM à `0xFE000000`, GIC à `0xFF840000`. Si `arm_peri_high=1`, tout est déplacé à `0x4_7C00_0000` / `0x4_C004_0000` : Circle ne le gère pas, nous non plus (ne pas mettre cette option).
- `enable_gic=1` est le défaut sur Pi 4 : le GIC‑400 est utilisé (pas l'ancien contrôleur ARMC). Ne pas le désactiver.
- Pour notre OS : `kernel=kernel8.img`, `arm_64bit=1`. L'armstub par défaut intégré à `start4.elf` suffit (il a le même comportement que `armstub8.S`, sans la partie FIQ/EL3).

### 1.2 Armstub (`boot/armstub/armstub8.S`) — exécuté en EL3 avant le noyau

Séquence (tous les cœurs) :
1. `LOCAL_CONTROL (0xFF800000) ← 0` : timer local sur le cristal, incrément +1.
2. `LOCAL_PRESCALER (0xFF800008) ← 0x80000000` : diviseur 1.
3. `L2CTLR_EL1 (S3_1_C11_C0_2) |= 0x22` : latence L2 = 3.
4. `CNTFRQ_EL0 ← 54 000 000` (**oscillateur 54 MHz**).
5. `CNTVOFF_EL2 ← 0` ; `CPTR_EL3 ← 0` (FP/SIMD autorisé).
6. `SCR_EL3 ← RW|HCE|RES1(5)|RES1(4)|NS = 0x5B1`.
7. `VBAR_EL3 ← 0x70000`, `SP ← 0x70000` (pile EL3 4 Ko sous 0x70000).
8. `CPUECTLR_EL1 (S3_1_C15_C2_1) ← SMPEN (bit 6)` : **indispensable** à la cohérence SMP.
9. `setup_gic` : cœur 0 : `GICD_CTLR ← 3` ; tous : `GICC_CTLR ← 0x1E7`, `GICC_PMR ← 0xFF`, `GICD_IGROUPR0..7 ← 0xFFFFFFFF` (toutes les IRQ en **groupe 1 non sécurisé**).
10. `SCTLR_EL2 ← 0x30C50830` ; `SPSR_EL3 ← 0x3C9` (DAIF masqués, EL2h) ; `ELR_EL3 ← in_el2` ; `eret`.
11. En EL2 : cœur 0 saute à `kernel_entry32` (= 0x80000) avec `x0 = dtb_ptr32` ; cœurs 1–3 bouclent : `wfe ; ldr x4, [0xD8 + core*8] ; cbz x4, boucle ; br x4` (spin table).

Disposition du stub (adresses physiques basses) :

| Adresse | Contenu |
|---|---|
| `0x000–0x0FF` | code du stub |
| `0xD4` | magic `"FIQS"` (0x53514946), présent uniquement avec le stub Circle |
| `0xD8 / 0xE0 / 0xE8 / 0xF0` | `spin_cpu0..3` (u64) — adresse d'entrée des cœurs 0..3 |
| `0xF0` | `stub_magic 0x5AFE570B` (écrasé, partagé avec spin_cpu3) |
| `0xF8` | `dtb_ptr32` |
| `0xFC` | `kernel_entry32` |
| `0x6F000–0x6FFFF` | pile EL3 (stub Circle) |
| `0x70000` | vecteurs EL3 (stub Circle) |

**Piège** : les pages `0x0–0xFFFF` contiennent la spin table. Ne pas les réutiliser comme RAM libre avant que les cœurs 1–3 soient démarrés.

### 1.3 `lib/startup64.S` — entrée `_start` à 0x80000 (EL2)

Cœur 0 :
```
mrs x0, CurrentEL ; cmp x0, #4 ; beq 1f           ; déjà en EL1 ?
SP_EL1  ← MEM_EXCEPTION_STACK (0x308000)          ; pile des handlers (EL1h)
VBAR_EL2 ← VectorTable
; macro armv8_switch_to_el1_m :
CNTHCTL_EL2 |= 3            ; EL1PCTEN|EL1PCEN : accès EL1 au compteur/timer physique
CNTVOFF_EL2 ← 0
VPIDR_EL2 ← MIDR_EL1 ; VMPIDR_EL2 ← MPIDR_EL1
CPTR_EL2  ← 0x33FF          ; pas de trap FP/SIMD vers EL2
HSTR_EL2  ← 0
CPACR_EL1 ← 3<<20           ; FPEN=11 : FP/SIMD autorisé en EL1/EL0
HCR_EL2   ← 1<<31           ; RW=1 : EL1 en AArch64
SCTLR_EL1 ← 0x30D00800      ; RES1 seulement, MMU/caches OFF, A=0
SPSR_EL2  ← 0x3C4           ; DAIF masqués, mode EL1t (SP_EL0)
ELR_EL2   ← 1f ; eret
1: SP (=SP_EL0) ← MEM_KERNEL_STACK (0x2A0000)
VBAR_EL1 ← VectorTable ; b sysinit
```

**Particularité Circle** : le code noyau tourne en **EL1t** (pile = `SP_EL0`) et les exceptions en **EL1h** (pile `SP_EL1` par cœur, 32 Ko). Pour un OS avec espace utilisateur EL0, c'est inadapté : `SP_EL0` appartient à l'utilisateur. **Proposition** : noyau en **EL1h** (`SPSR_EL2 = 0x3C5`), une pile noyau par thread dans `SP_EL1`, `SP_EL0` réservé à EL0.

Cœurs secondaires (`_start_secondary`) : `core = MPIDR_EL1 & 3` ; `SP_EL1 = 0x308000 + core*0x8000` ; même bascule EL2→EL1 ; `SP = 0x2A0000 + core*0x20000` ; `VBAR_EL1` ; `b sysinit_secondary`.

**Piège** : le registre `x0` (pointeur DTB) est écrasé immédiatement par Circle (qui obtient tout via les property tags). Si l'on veut le DTB, le sauver dans `x19` en première instruction.

### 1.4 `lib/sysinit.cpp` (`sysinit`, branche 64 bits)

1. `msr DAIFClr,#1` puis `msr DAIFClr,#2` (FIQ puis IRQ démasqués — sans danger, rien n'est encore routé).
2. Mise à zéro de `.bss` (`__bss_start..__bss_end`) et `.tbss`.
3. Contrôle `_end <= MEM_KERNEL_END` (0x280000), sinon `halt`.
4. `CMemorySystem` : property tag `GET_ARM_MEMORY` → taille RAM basse ; tas ; page allocator ; tables MMU ; activation MMU (§2).
5. `CMachineInfo`, `SetupHighMem` (RAM > 1 Go, jusqu'à 3 Go).
6. `CInterruptSystem::Initialize` (GIC, §4).
7. Constructeurs statiques (`__init_start..__init_end`), puis `main()`.

(En 32 bits seulement : invalidation du D‑cache L1 et `ARM_LOCAL_PRESCALER ← 39768216` pour un 1 MHz. En 64 bits, rien : le timer générique est utilisé directement à 54 MHz.)

### 1.5 Carte mémoire physique Circle (RPi4, AArch64)

Constantes (`memorymap64.h`, `sysconfig.h`) : `KERNEL_MAX_SIZE = 2 Mo`, `KERNEL_STACK_SIZE = 0x20000`, `EXCEPTION_STACK_SIZE = 0x8000`, `PAGE_SIZE = 0x10000` (64 Ko), `PAGE_RESERVE = 16 Mo`.

| Base | Taille | Contenu |
|---|---|---|
| `0x00000000` | 256 o | armstub + spin table |
| `0x0006F000` | 4 Ko | pile EL3 (stub Circle) |
| `0x00070000` | 2 Ko | vecteurs EL3 (stub Circle) |
| `0x00080000` | ≤ 2 Mo | image noyau (`.init .text .rodata .init_array .eh_frame .data .bss`) |
| `0x00280000` | 4 × 128 Ko | piles noyau cœurs 0..3 (sommet cœur 0 = `0x2A0000`) |
| `0x00300000` | 4 × 32 Ko | piles d'exception cœurs 0..3 (sommet cœur 0 = `0x308000`) |
| `0x00500000` | 4 Mo | **région cohérente** (mailbox, VCHIQ, xHCI) — mappée Device |
| `0x00900000` | variable | tas bas (`new`/`malloc`) |
| `memsize-16Mo` | 16 Mo | page allocator (`palloc`, pages de 64 Ko) |
| `memsize` (≈0x3B400000) → `0x40000000` | — | mémoire GPU (framebuffer !) |
| `0x40000000` → `0xBFFFFFFF` | ≤ 2 Go | tas haut (« unused above 0xC0000000 » : DMA non sûr au-delà de 3 Go) |
| `0xFC000000` | 64 Mo | périphériques (`ARM_IO_BASE = 0xFE000000`) |
| `0xFD500000` | | contrôleur hôte PCIe |
| `0xFD580000` | | GENET / PHY BCM54213 |
| `0xFF800000` | | « ARM local » (LOCAL_CONTROL, PRESCALER…) |
| `0xFF841000` / `0xFF842000` | | GICD / GICC |
| `0x6_0000_0000` | 64 Mo | fenêtre PCIe sortante (xHCI VL805), vue PCIe `0xF8000000` |

Adresses de périphériques utiles (`ARM_IO_BASE = 0xFE000000`, vue bus VC = `0x7E000000`) :

| Périphérique | ARM | Remarque |
|---|---|---|
| System timer | `0xFE003000` | CLO +4, CHI +8 (1 MHz) — non utilisé par Circle sur Pi 4 |
| DMA legacy | `0xFE007000` | |
| Mailbox 0 | `0xFE00B880` | §8 |
| GPIO | `0xFE200000` | GPFSEL0 +0x00, GPPUPPDN0 +0xE4 |
| UART0 PL011 | `0xFE201000` | §10 |
| PWM | `0xFE20C000` | |
| RNG200 | `0xFE104000` | |
| EMMC2 (SD) | `0xFE340000` | |
| xHCI interne (Pi 400/CM4 via `USE_XHCI_INTERNAL`) | `0xFE9C0000` | |

Conversion pour DMA VC (DMA legacy, mailbox) : `BUS_ADDRESS(a) = (a & ~0xC0000000) | 0xC0000000` (alias non caché). Côté PCIe (xHCI), le DMA entrant mappe `0..4 Go` 1:1 (`MEM_PCIE_DMA_RANGE_*`) ; **ne pas donner de buffer DMA au‑dessus de 3 Go**.

### 1.6 Transposition asm proposée (boot)

- Fichier `boot.asm` : `format binary as 'img'`, `org 0x80000`, `processor cpu64_v8`, `code64`.
- `_start` : sauver `x0` (DTB) → `x19` ; lire `MPIDR_EL1 & 3` ; si ≠ 0 → `secondary_entry` (ne devrait pas arriver avec la spin table) ; sinon : piles, `VBAR_EL1`, bascule EL2→EL1h, invalidation D‑cache L1 par set/way (prudence), BSS à zéro, `kmain`.
- Constantes : un `include 'bcm2711.inc'` avec `ARM_IO_BASE = 0xFE000000`, etc. (FASM : `NAME = valeur`).
- Chargement des constantes 64 bits : macro `mov64` (movz/movk), car FASMARM n'a pas forcément de pool littéral `ldr x0,=imm` (à vérifier dans la doc FASMARM ; sinon `ldr x0, label_const`).

```asm
macro mov64 reg, val {
    movz reg, (val) and 0xFFFF
    movk reg, ((val) shr 16) and 0xFFFF, lsl 16
    movk reg, ((val) shr 32) and 0xFFFF, lsl 32
    movk reg, ((val) shr 48) and 0xFFFF, lsl 48
}
```

---

## 2. MMU (`translationtable64.cpp`, `memory64.cpp`, `armv8mmu.h`)

> Attention : `lib/memory.cpp` est la version **32 bits** (LPAE) ; la version AArch64 est `lib/memory64.cpp`.

### 2.1 Schéma Circle

- **Granule 64 Ko**, traduction **EL1 stage 1 uniquement**, **identité** (VA = PA), **TTBR0 seul** (TTBR1 désactivé par `EPD1`). Pas d'ASID, pas d'EL0.
- `T0SZ = 28` → VA 36 bits = **64 Go**. Avec 64 Ko de granule, le premier niveau est **L2** (une entrée = 512 Mo), puis **L3** (8192 entrées × 64 Ko = 512 Mo par table).
- Table L2 : une page de 64 Ko (`palloc`), seules les 128 premières entrées utilisées. Une table L3 (64 Ko) est créée pour chaque tranche de 512 Mo **< 4 Go**, plus la fenêtre PCIe `0x6_0000_0000` (entrée 48). Coût : 9 × 64 Ko = 576 Ko.

Descripteur de table L2 (`TARMV8MMU_LEVEL2_TABLE_DESCRIPTOR`) : `bits[1:0]=0b11`, adresse L3 en `[47:16]`, `APTable=0`, `PXNTable=UXNTable=NSTable=0`.

Descripteur de page L3 (`TARMV8MMU_LEVEL3_PAGE_DESCRIPTOR`) :

| Bits | Champ | Valeur Circle |
|---|---|---|
| 1:0 | type | `0b11` |
| 4:2 | AttrIndx | 0 normal / 1 device / 2 cohérent |
| 5 | NS | 0 |
| 7:6 | AP | `00` (RW EL1 seul) |
| 9:8 | SH | `11` inner (normal) / `10` outer (device, cohérent) |
| 10 | AF | **1** (sinon faute d'accès) |
| 11 | nG | 0 |
| 47:16 | adresse de sortie | PA >> 16 |
| 52 | Contiguous | 0 |
| 53 | PXN | 0 pour le code (`< _etext`), 1 ailleurs |
| 54 | UXN | 1 |

Règles d'attribut (`CreateLevel3Table`) :
- adresse `< _etext` → Normal, exécutable EL1 ;
- `[memsize, 1 Go)` (mémoire GPU, framebuffer) et `> 3 Go - 1` (dont périphériques 0xFC000000+, RAM 3–4 Go, PCIe) → **Device nGnRE**, outer shareable, PXN ;
- `[0x500000, 0x900000)` (région cohérente) → **Device nGnRnE** (AttrIndx 2), outer shareable ;
- le reste → Normal WB, inner shareable, PXN, UXN.

Valeurs exactes des registres système (`CMemorySystem::EnableMMU`) :

| Registre | Valeur | Détail |
|---|---|---|
| `MAIR_EL1` | `0x0004FF` | Attr0=`0xFF` Normal WB RW‑allocate inner/outer ; Attr1=`0x04` Device‑nGnRE ; Attr2=`0x00` Device‑nGnRnE |
| `TTBR0_EL1` | PA table L2 (alignée 64 Ko) | pas d'ASID |
| `TCR_EL1` | read‑modify‑write → `0x1_0080_751C` | `T0SZ=28`, `IRGN0=01`, `ORGN0=01` (WB‑WA), `SH0=11`, `TG0=01` (64 Ko), `EPD1=1`, `A1=0`, `IPS=001` (36 bits, 64 Go) |
| `SCTLR_EL1` | `0x30D00800` puis `|= M|C|I`, `&= ~(A|WXN)` → `0x30D01805` | MMU, D‑cache, I‑cache ; alignement non strict |

Ordre Circle : tables construites **MMU et D‑cache éteints** (écritures directes en RAM) → `DSB` → MAIR → TTBR0 → TCR → SCTLR → `ISB`. Les cœurs secondaires réutilisent la même table (`InitializeSecondary` → `EnableMMU`).

Désactivation (`Destructor`) : `SCTLR &= ~(M|C)`, `DSB ; ISB`, clean + invalidate D‑cache set/way, `tlbi vmalle1 ; DSB ; ISB`.

### 2.2 Pièges MMU

- **AF=1 obligatoire** (pas de gestion matérielle d'Access Flag sur A72).
- Après modification d'un descripteur actif : `dsb ishst` → `tlbi vae1is, Xt` (Xt = `ASID<<48 | VA>>12` — **toujours en unités de 4 Ko même en granule 64 Ko**) → `dsb ish` → `isb`.
- Tables construites MMU allumée : elles sont dans du Normal WB et le walker est cohérent avec le cache (IRGN/ORGN=WB) : pas de clean nécessaire, mais `DSB` obligatoire avant usage.
- TCR en read‑modify‑write chez Circle : `TG1` garde sa valeur de reset (UNKNOWN) ; sans conséquence avec `EPD1=1`, mais **écrire TCR entièrement** chez nous (TG1=`0b11` pour 64 Ko).
- Le framebuffer (mémoire GPU) est mappé **Device** : correct mais lent (pas de write‑combining). Voir §9.
- Les exclusives (`ldaxr/stxr`) des spinlocks ne fonctionnent de façon fiable qu'avec la **MMU active** et une mémoire Normal cacheable partagée.
- Les instructions `dc zva` / accès non alignés sur mémoire Device provoquent une faute d'alignement : ne jamais faire de `memcpy` rapide sur un mappage Device.

### 2.3 Transposition proposée (pagination par processus + ASID)

**Option B (recommandée phase 1, style MenuetOS)** : rester en identité pour le noyau, **une table L2 par processus**, les entrées « noyau » pointant vers des tables L3 **partagées et globales**.

- `TCR_EL1` (proposition) : `T0SZ=25` (VA 39 bits = 512 Go, table L2 de 1024 entrées = 8 Ko), `TG0=01`, `SH0=11`, `IRGN0=ORGN0=01`, `IPS=001`, **`AS=1`** (ASID 16 bits, bit 36), `A1=0` (ASID pris dans TTBR0), `EPD1=1`.
- Entrées L2 0..63 (0–32 Go) : tables L3 noyau partagées (AP=00, **nG=0**, UXN=1) — y compris périphériques et fenêtre PCIe.
- Entrées L2 512..1023 (256–512 Go) : espace utilisateur (AP=01 RW EL0/EL1, **nG=1**, PXN=1, UXN=0 pour le code).
- `TTBR0_EL1 = (ASID << 48) | PA_L2_processus`.
- Bascule de processus : `msr ttbr0_el1, x ; isb` — **aucun TLBI** grâce à l'ASID. Recyclage d'ASID (débordement de génération) : `tlbi vmalle1is ; dsb ish ; isb`. Destruction de processus : `tlbi aside1is, (asid<<48)`.

**Option A (plus tard)** : noyau en moitié haute via TTBR1 (`T1SZ`, `EPD1=0`), lié à `0xFFFF_FF80_0008_0000` ; exige un trampoline position‑indépendant (`adr`) avant l'activation MMU et un `org` haut dans FASM.

Structures asm proposées :
```
; descripteur de processus (offsets)
PROC_PID      = 0      ; u32
PROC_ASID     = 4      ; u16 (+ génération u16)
PROC_TTBR0    = 8      ; u64 = asid<<48 | pa_l2
PROC_L2       = 16     ; u64 PA table L2 (8 Ko)
PROC_THREADS  = 24     ; tête de liste des threads
PROC_SIZE     = 64
```
Routines : `mmu_init_kernel_tables`, `mmu_enable` (x0 = TTBR0), `vm_map_page(x0=proc, x1=va, x2=pa, x3=attr)`, `vm_unmap_page`, `vm_new_space(x0=proc)` (copie les 64 entrées noyau), `vm_switch(x0=proc)`.

---

## 3. Exceptions (`exceptionstub64.S`, `exceptionhandler64.cpp`)

### 3.1 Table Circle (`VectorTable`, `.align 11` = 2 Ko, 16 entrées × 0x80)

| Offset | Origine | Circle |
|---|---|---|
| 0x000/080/100/180 | EL courant, SP_EL0 (EL1t) | Sync / IRQ / FIQ / SError → `SynchronousStub`, `IRQStub`, `FIQStub`, `SErrorStub` |
| 0x200/280/300/380 | EL courant, SP_ELx (EL1h) | idem |
| 0x400 | EL inférieur AArch64, Sync | `HVCStub` (sert uniquement quand la table est en VBAR_EL2 : remet SPSR_EL2 en EL2h) |
| 0x480..0x780 | EL inférieur | `UnexpectedStub` |

Chaque entrée est un simple `b handler`.

- **Stubs d'abandon** (`stub` macro) : empilent `ESR_EL1, SPSR_EL1, x30, ELR_EL1, SP_EL0, SP, FAR_EL1` (trame `TAbortFrame`, 64 octets) puis `ExceptionHandler(n, frame)` → log fatal (EC = `ESR>>26 & 0x3F`, ISS = `ESR & 0x1FFFFFF`, FAR valide si EC ∈ 0x20–0x25, 0x34–0x35). Pas de retour.
- **IRQStub** : empile `x29,x30`, `ELR_EL1,SPSR_EL1`, **ré‑autorise le FIQ** (`msr DAIFClr,#1`), empile `q0–q31` si `SAVE_VFP_REGS_ON_IRQ` (actif automatiquement avec GCC ≥ 12), empile `x0–x28`, mémorise ELR dans `IRQReturnAddress`, `bl InterruptHandler`, dépile tout, `msr DAIFSet,#1`, restaure ELR/SPSR, `eret`. L'IRQ n'est **pas réentrante** (le bit I reste masqué).
- **FIQStub** : sauvegarde x0–x30 (+q si option), appelle `FIQData.pHandler(pParam)` ; si aucun handler, coupe le FIQ (`ARM_IC_FIQ_CONTROL`, legacy).
- **SMCStub** (EL3, Pi 4) : le stub Circle installe des vecteurs EL3 à `0x70000` (écrits par `CInterruptSystem::Initialize`, entrée 8 = SMC) pour faire basculer une IRQ en groupe 0 (FIQ) — cf. §4. Non nécessaire pour nous.

### 3.2 Pièges

- `VBAR_EL1` doit être aligné sur **2 Ko** ; chaque slot fait 128 octets (32 instructions max).
- Ne jamais toucher aux registres FP/SIMD dans un handler si on ne les sauve pas (le compilateur C l'aurait fait ; en asm, c'est nous qui contrôlons : **interdire FP en noyau** simplifie énormément).
- `ELR_EL1/SPSR_EL1` sont écrasés par toute exception imbriquée : les sauver **avant** de démasquer quoi que ce soit.
- La pile doit rester alignée sur 16 octets (`SCTLR_EL1.SA=0` chez Circle, mais garder la discipline).
- Après écriture d'une table de vecteurs en mémoire (code auto‑modifié) : `dc cvau` / clean set/way, `ic iallu`, `dsb`, `isb` (`SyncDataAndInstructionCache`).

### 3.3 Transposition : trame complète et vecteurs (pseudo‑asm FASMARM)

Trame (`TF_*`) empilée sur la **pile noyau du thread** (`SP_EL1`) — 272 octets, multiple de 16 :
```
TF_X0     = 0          ; x0..x30 : 31*8 = 248
TF_SP0    = 248        ; SP_EL0 (pile utilisateur)
TF_ELR    = 256
TF_SPSR   = 264
TF_SIZE   = 272
```

```asm
; ---------------------------------------------------------------
; Sauvegarde/restauration de la trame
; ---------------------------------------------------------------
macro SAVE_TF {
        sub     sp, sp, TF_SIZE
        stp     x0,  x1,  [sp, 16*0]
        stp     x2,  x3,  [sp, 16*1]
        stp     x4,  x5,  [sp, 16*2]
        stp     x6,  x7,  [sp, 16*3]
        stp     x8,  x9,  [sp, 16*4]
        stp     x10, x11, [sp, 16*5]
        stp     x12, x13, [sp, 16*6]
        stp     x14, x15, [sp, 16*7]
        stp     x16, x17, [sp, 16*8]
        stp     x18, x19, [sp, 16*9]
        stp     x20, x21, [sp, 16*10]
        stp     x22, x23, [sp, 16*11]
        stp     x24, x25, [sp, 16*12]
        stp     x26, x27, [sp, 16*13]
        stp     x28, x29, [sp, 16*14]
        mrs     x9,  sp_el0
        stp     x30, x9,  [sp, 16*15]        ; x30 @240, SP_EL0 @248
        mrs     x10, elr_el1
        mrs     x11, spsr_el1
        stp     x10, x11, [sp, TF_ELR]
}

macro RESTORE_TF {
        ldp     x10, x11, [sp, TF_ELR]
        msr     elr_el1, x10
        msr     spsr_el1, x11
        ldp     x30, x9,  [sp, 16*15]
        msr     sp_el0, x9
        ldp     x0,  x1,  [sp, 16*0]
        ldp     x2,  x3,  [sp, 16*1]
        ; ... x4..x27 symétrique ...
        ldp     x28, x29, [sp, 16*14]
        add     sp, sp, TF_SIZE
        eret
}

; ---------------------------------------------------------------
; Table de vecteurs (alignée 2 Ko)
; ---------------------------------------------------------------
macro VENTRY target {
        align   128
        b       target
}

        align   2048
vectors:
        VENTRY  bad_exception      ; EL1t sync   (non utilisé : noyau en EL1h)
        VENTRY  bad_exception      ; EL1t irq
        VENTRY  bad_exception      ; EL1t fiq
        VENTRY  bad_exception      ; EL1t serror
        VENTRY  el1_sync           ; EL1h sync   (faute noyau)
        VENTRY  el1_irq            ; EL1h irq    (IRQ pendant le noyau)
        VENTRY  bad_exception      ; EL1h fiq
        VENTRY  bad_exception      ; EL1h serror
        VENTRY  el0_sync           ; EL0 A64 sync (SVC, faute de page, FP trap)
        VENTRY  el0_irq            ; EL0 A64 irq  (préemption utilisateur)
        VENTRY  bad_exception      ; EL0 A64 fiq
        VENTRY  bad_exception      ; EL0 A64 serror
        VENTRY  bad_exception      ; EL0 A32 x4 (pas d'AArch32)
        VENTRY  bad_exception
        VENTRY  bad_exception
        VENTRY  bad_exception

el0_irq:
el1_irq:
        SAVE_TF
        mov     x0, sp                     ; x0 = trame
        bl      irq_dispatch               ; ack GIC, handler, EOI
        bl      preempt_check              ; peut changer SP (cf. §7.4)
        RESTORE_TF

el0_sync:
        SAVE_TF
        mrs     x9, esr_el1
        lsr     x10, x9, 26                ; EC
        cmp     x10, 0x15                  ; SVC AArch64
        b.eq    .svc
        cmp     x10, 0x07                  ; accès FP/SIMD piégé (FP paresseux)
        b.eq    .fp
        cmp     x10, 0x24                  ; data abort EL0
        b.eq    .dabt
        cmp     x10, 0x20                  ; instruction abort EL0
        b.eq    .iabt
        mov     x0, sp
        bl      kill_current_thread        ; ne revient pas
.svc:   ldr     x8, [sp, TF_X0+8*8]        ; n° de syscall dans x8
        mov     x0, sp
        bl      syscall_dispatch           ; résultat écrit dans TF_X0
        bl      preempt_check
        RESTORE_TF
.fp:    bl      fpu_switch_lazy
        RESTORE_TF
.dabt:
.iabt:  mov     x0, sp
        mrs     x1, far_el1
        bl      page_fault
        RESTORE_TF
```
Convention syscall proposée : `svc 0`, n° dans `x8`, arguments `x0–x5`, retour dans `x0` (comme Linux AArch64 ; MenuetOS utilise `eax` + `int 0x40` — même idée).

Codes EC utiles : `0x15` SVC64, `0x07` FP/SIMD piégé, `0x20/0x21` instruction abort EL0/EL1, `0x24/0x25` data abort EL0/EL1, `0x22` PC non aligné, `0x26` SP non aligné, `0x3C` BRK.

---

## 4. Interruptions — GIC‑400 (`interruptgic.cpp`, `bcm2711int.h`)

### 4.1 Registres

Base : `GICD = 0xFF841000`, `GICC = 0xFF842000` (bloc GIC 0xFF840000–0xFF847FFF).

| Registre | Offset | Usage |
|---|---|---|
| `GICD_CTLR` | D+0x000 | 0 = off, 1 = on (non sécurisé : bit0 = groupe 1) |
| `GICD_IGROUPRn` | D+0x080 | accès sécurisé seulement (armstub les met tous à 1) |
| `GICD_ISENABLERn` / `ICENABLERn` | D+0x100 / D+0x180 | 1 bit par IRQ, `n = irq/32` |
| `GICD_ISPENDRn` / `ICPENDRn` | D+0x200 / D+0x280 | |
| `GICD_ISACTIVERn` / `ICACTIVERn` | D+0x300 / D+0x380 | |
| `GICD_IPRIORITYRn` | D+0x400 | 1 octet par IRQ ; défaut Circle `0xA0`, FIQ `0x40` |
| `GICD_ITARGETSRn` | D+0x800 | 1 octet par IRQ, bit = cœur (lecture seule pour 0–31) |
| `GICD_ICFGRn` | D+0xC00 | 2 bits par IRQ ; bit1 = 1 front, 0 niveau |
| `GICD_SGIR` | D+0xF00 | `(1 << (16+core)) | sgi_id` |
| `GICC_CTLR` | C+0x000 | 1 = on |
| `GICC_PMR` | C+0x004 | masque de priorité, Circle `0xF0` |
| `GICC_IAR` | C+0x00C | ID = bits[9:0], CPU source SGI = bits[12:10] |
| `GICC_EOIR` | C+0x010 | écrire la valeur **brute** lue dans IAR |

`IRQ_LINES = 256` sur Pi 4 (8 registres de 32 bits).

### 4.2 Séquence d'init Circle (cœur 0)

1. (Pi 4 64 bits) Patch des vecteurs EL3 à `0x70000` pour le SMC + `SyncDataAndInstructionCache` — **inutile pour nous** (pas de FIQ).
2. `GICD_CTLR ← 0`.
3. Pour n = 0..7 : `ICENABLERn ← 0xFFFFFFFF`, `ICPENDRn ← 0xFFFFFFFF`, `ICACTIVERn ← 0xFFFFFFFF`.
4. Pour n = 0..63 : `IPRIORITYRn ← 0xA0A0A0A0`, `ITARGETSRn ← 0x01010101` (tout vers cœur 0).
5. Pour n = 0..15 : `ICFGRn ← 0` (tout en niveau ; ICFGR0 des SGI est en lecture seule).
6. `GICD_CTLR ← 1`.
7. `GICC_PMR ← 0xF0`, `GICC_CTLR ← 1`, `msr DAIFClr,#2`.

Cœurs secondaires (`InitializeSecondary`) : uniquement `GICC_PMR ← 0xF0`, `GICC_CTLR ← 1`.

Activation : `ConnectIRQ(n)` → table de handlers `m_apIRQHandler[256]` + `m_pParam[256]` + `ISENABLER[n/32] ← 1 << (n%32)`.

Traitement (`InterruptHandler`) : `iar = GICC_IAR` ; `id = iar & 0x3FF` ; si `id < 256` : `id > 15` → handler (s'il est absent, l'IRQ est désactivée) ; sinon SGI → `CMultiCoreSupport::LocalInterruptHandler(from, id)` ; puis `GICC_EOIR ← iar`. `id ≥ 1020` = spurious, **pas d'EOI**.

### 4.3 Numéros d'IRQ utiles (RPi4, `bcm2711int.h`) — `PPI(n)=16+n`, `SPI(n)=32+n`

| Source | Macro Circle | ID GIC |
|---|---|---|
| Timer physique non sécurisé (CNTP) | `ARM_IRQLOCAL0_CNTPNS` = PPI 14 | **30** |
| Timer virtuel (CNTV) — non utilisé par Circle | PPI 11 | 27 |
| Doorbell ARM 0 | SPI 34 | 66 |
| System timer C1 | SPI 65 | 97 |
| USB DWC OTG | `ARM_IRQ_USB` SPI 73 | 105 |
| DMA0–DMA14 | SPI 80–92 | 112–124 |
| GPIO0..3 | SPI 113–116 | 145–148 |
| I2C | SPI 117 | 149 |
| **UART (PL011)** | `ARM_IRQ_UART` SPI 121 | **153** |
| Arasan SDIO / EMMC | `ARM_IRQ_ARASANSDIO` SPI 126 | 158 (le pilote EMMC2 de Circle, `addon/SDCard/emmc.cpp`, travaille **en scrutation**, sans IRQ) |
| **PCIe INTA** (xHCI VL805 sur Pi 4B) | `ARM_IRQ_PCIE_HOST_INTA` SPI 143 | **175** |
| PCIe MSI | `ARM_IRQ_PCIE_HOST_MSI` SPI 148 | **180** (Circle route l'IRQ xHCI via MSI) |
| **GENET** (Ethernet BCM54213) | `ARM_IRQ_BCM54213_0/1` SPI 157/158 | **189 / 190** |
| xHCI interne (Pi 400/CM4) | `ARM_IRQ_XHCI_INTERNAL` SPI 176 | 208 |
| SGI (IPI) | — | 0–15 |

### 4.4 Pièges GIC

- **Registres bankés par cœur** : `ISENABLER0` (IRQ 0–31, donc SGI + PPI comme le timer 30), `IPRIORITYR0..7`, `ITARGETSR0..7` sont propres à chaque cœur. **Chaque cœur doit activer lui‑même l'IRQ 30** pour sa préemption. Circle ne le fait que sur le cœur 0.
- Tant que l'IRQ n'a pas été acquittée par EOI, elle reste active ; un EOI oublié = plus jamais d'IRQ de même priorité.
- Les accès GIC sont en mémoire Device (32 bits, `str w`) ; jamais d'accès 64 bits.
- Le groupe (0/1) n'est modifiable qu'en sécurisé : en EL1 non sécurisé on ne voit que le groupe 1 (fait par l'armstub).
- Toujours un `dsb sy` avant EOI si le handler a écrit sur le périphérique pour baisser la ligne (sinon l'IRQ niveau peut se redéclencher).

### 4.5 Transposition asm (init GIC + dispatch)

```asm
GICD           = 0xFF841000
GICC           = 0xFF842000
GICD_CTLR      = 0x000
GICD_ISENABLER = 0x100
GICD_ICENABLER = 0x180
GICD_ICPENDR   = 0x280
GICD_ICACTIVER = 0x380
GICD_IPRIORITY = 0x400
GICD_ITARGETS  = 0x800
GICD_ICFGR     = 0xC00
GICD_SGIR      = 0xF00
GICC_CTLR      = 0x000
GICC_PMR       = 0x004
GICC_IAR       = 0x00C
GICC_EOIR      = 0x010
IRQ_LINES      = 256

; gic_init_dist : cœur 0 seulement. Détruit x0-x4.
gic_init_dist:
        mov64   x0, GICD
        str     wzr, [x0, GICD_CTLR]
        mov     w1, -1
        mov     x2, 0
.l1:    add     x3, x0, x2
        str     w1, [x3, GICD_ICENABLER]
        str     w1, [x3, GICD_ICPENDR]
        str     w1, [x3, GICD_ICACTIVER]
        add     x2, x2, 4
        cmp     x2, IRQ_LINES/8            ; 8 registres * 4 octets
        b.ne    .l1
        mov     w1, 0xA0A0
        movk    w1, 0xA0A0, lsl 16         ; priorité par défaut
        mov     w4, 0x0101
        movk    w4, 0x0101, lsl 16         ; cible cœur 0
        mov     x2, 0
.l2:    add     x3, x0, x2
        str     w1, [x3, GICD_IPRIORITY]
        str     w4, [x3, GICD_ITARGETS]
        add     x2, x2, 4
        cmp     x2, IRQ_LINES              ; 64 registres * 4 octets
        b.ne    .l2
        mov     x2, 0
.l3:    add     x3, x0, x2
        str     wzr, [x3, GICD_ICFGR]      ; niveau
        add     x2, x2, 4
        cmp     x2, IRQ_LINES/4            ; 16 registres
        b.ne    .l3
        mov     w1, 1
        str     w1, [x0, GICD_CTLR]
        ; continue sur l'init CPU interface

; gic_init_cpu : chaque cœur. Active aussi le PPI 30 (timer) de CE cœur.
gic_init_cpu:
        mov64   x0, GICC
        mov     w1, 0xF0
        str     w1, [x0, GICC_PMR]
        mov     w1, 1
        str     w1, [x0, GICC_CTLR]
        mov64   x0, GICD
        mov     w1, 1 shl 30               ; IRQ 30 = CNTP non sécurisé
        str     w1, [x0, GICD_ISENABLER]   ; banké par cœur
        ret

; gic_enable_irq : x0 = n° IRQ
gic_enable_irq:
        mov64   x1, GICD + GICD_ISENABLER
        lsr     x2, x0, 5
        and     x3, x0, 31
        mov     w4, 1
        lsl     w4, w4, w3
        str     w4, [x1, x2, lsl 2]
        ret

; irq_dispatch : appelé depuis el0_irq / el1_irq
irq_dispatch:
        stp     x29, x30, [sp, -16]!
        mov64   x19, GICC
        ldr     w20, [x19, GICC_IAR]
        and     w0, w20, 0x3FF
        cmp     w0, 1020
        b.hs    .spurious                  ; pas d'EOI
        adr     x1, irq_table              ; 256 * (handler, param) = 16 o/entrée
        add     x1, x1, x0, lsl 4
        ldp     x2, x0, [x1]               ; x2 = handler, x0 = param
        cbz     x2, .none
        blr     x2
.eoi:   dsb     sy
        str     w20, [x19, GICC_EOIR]
.spurious:
        ldp     x29, x30, [sp], 16
        ret
.none:  ; désactiver la ligne orpheline (comme Circle)
        and     w0, w20, 0x3FF
        bl      gic_disable_irq
        b       .eoi
```
(`x19/x20` sont sauvés dans la trame, donc utilisables ici.)

---

## 5. Timers (`lib/timer.cpp`)

### 5.1 Ce que fait Circle sur Pi 4

- `USE_PHYSICAL_COUNTER` est **obligatoire** sur Pi 4 (`#error` sinon ; défini dans `sysconfig.h`). Le BCM system timer n'est pas utilisé.
- `HZ = 100` (`timer.h`), `CLOCKHZ = 1 000 000` (unité « clock ticks » exposée).
- Init (`CTimer::Initialize`) :
  1. `ConnectIRQ(30, InterruptHandler)` ;
  2. `CNTFRQ_EL0` lu → 54 000 000 ; `m_nClockTicksPerHZTick = 54e6/100 = 540 000` (assert `CNTFRQ % HZ == 0`) ;
  3. `CNTP_CVAL_EL0 ← CNTPCT_EL0 + 540 000` ;
  4. `CNTP_CTL_EL0 ← 1` (ENABLE=1, IMASK=0).
- IRQ : `CNTP_CVAL_EL0 += 540 000` (repart de l'ancienne échéance → pas de dérive ; écrire CVAL **désarme** la condition ISTATUS), `m_nTicks++`, toutes les 100 ticks `m_nUptime++`, `m_nTime++` ; `PollKernelTimers()` (liste triée de timers logiciels, handlers appelés en contexte IRQ) ; handlers périodiques.
- Temps fin : `GetClockTicks64() = CNTPCT_EL0 * 1 000 000 / CNTFRQ` (µs). `SimpleusDelay` = attente active sur ce compteur.
- Arrêt : `CNTP_CTL_EL0 ← 0`.

### 5.2 Pièges

- L'accès EL1 au timer physique dépend de `CNTHCTL_EL2 |= 3` (fait dans `startup64.S`). Pour exposer le compteur à EL0 : `CNTKCTL_EL1.EL0VCTEN` (bit 1) / `EL0PCTEN` (bit 0).
- La conversion Circle `CNTPCT * 1 000 000 / CNTFRQ` déborde dès que `CNTPCT > 2^64/10^6 ≈ 1,8e13`, soit après **≈ 3,95 jours** à 54 MHz : bug latent pour un OS longue durée. Préférer `CNTPCT / 54` (exact : 54 ticks par µs) ou `umulh` avec un inverse précalculé.
- `isb` avant de lire `CNTPCT_EL0` si l'on mesure précisément (lecture spéculative possible).
- Le timer est **par cœur** (CNTP_* sont bankés) ; la sortie va sur le PPI 30 du cœur concerné.

### 5.3 Transposition asm

Données par cœur (dans la zone per‑CPU pointée par `TPIDR_EL1`) : `PCPU_TICKS (u64)`, `PCPU_TICK_INTERVAL (u64)`, `PCPU_NEED_RESCHED (u32)`, `PCPU_CURRENT (u64)`. Données globales : `jiffies`, `uptime_s`, liste des timers triée.

```asm
timer_init_core:                     ; à appeler sur chaque cœur, HZ=100 ou 1000
        mrs     x0, cntfrq_el0
        mov     x1, HZ
        udiv    x0, x0, x1
        mrs     x2, tpidr_el1
        str     x0, [x2, PCPU_TICK_INTERVAL]
        isb
        mrs     x1, cntpct_el0
        add     x1, x1, x0
        msr     cntp_cval_el0, x1
        mov     x0, 1
        msr     cntp_ctl_el0, x0
        ret

timer_irq:                           ; handler de l'IRQ 30
        mrs     x2, tpidr_el1
        ldr     x0, [x2, PCPU_TICK_INTERVAL]
        mrs     x1, cntp_cval_el0
        add     x1, x1, x0
        msr     cntp_cval_el0, x1
        ldr     x0, [x2, PCPU_TICKS]
        add     x0, x0, 1
        str     x0, [x2, PCPU_TICKS]
        ; décrémenter la tranche de temps du thread courant
        ldr     x3, [x2, PCPU_CURRENT]
        ldr     w4, [x3, TH_QUANTUM]
        subs    w4, w4, 1
        str     w4, [x3, TH_QUANTUM]
        b.gt    .done
        mov     w4, 1
        str     w4, [x2, PCPU_NEED_RESCHED]
.done:  ; cœur 0 seulement : jiffies++, timers logiciels, réveil des dormeurs
        ret
```
Recommandation : `HZ = 1000` (intervalle 54 000) ou tick‑less plus tard (programmer CVAL sur la prochaine échéance).

---

## 6. Multicœur, spinlocks, barrières (`multicore.cpp`, `spinlock.cpp`, `synchronize64.*`)

### 6.1 Réveil des cœurs 1–3 (Pi 4, AArch64)

Spin table : `ARM_SPIN_TABLE_BASE = 0xD8` (`include/circle/startup.h`), entrée du cœur n = `0xD8 + 8*n` (0xE0, 0xE8, 0xF0).

Séquence `CMultiCoreSupport::Initialize` (cœur 0, MMU active) :
1. `CSpinLock::Enable()` (avant, les spinlocks ne font que masquer les IRQ).
2. `CleanDataCache()` (set/way L1+L2) : tout ce que les autres cœurs liront doit être en RAM.
3. Pour chaque cœur 1..3 :
   - attendre que `[0xD8+8n] == 0` (timeout 100 ms) ;
   - `[0xD8+8n] ← _start_secondary` (64 bits) ; `CleanDataCache()` (le cœur cible lit **sans MMU ni cache**) ;
   - boucle `sev ; delay 1 ms ; dmb` jusqu'à ce que le cœur remette l'entrée à 0 (timeout 500 ms).
4. Côté cœur n : `_start_secondary` → `sysinit_secondary` (DAIF démasqués) → `EntrySecondary` : `EnableMMU` (même table) → `[0xD8+8n] ← 0 ; dsb` → `GICC_PMR/CTLR` → `msr DAIFClr,#2` → `Run(n)`.

IPI : `GICD_SGIR ← (1 << (16+core)) | ipi` ; `IPI_HALT_CORE` arrête le cœur. Identifiant de cœur : `MPIDR_EL1 & 3` (Aff0).

### 6.2 Spinlocks (AArch64)

```
Acquire: [EnterCritical(level)]          ; masque IRQ (et FIQ si FIQ_LEVEL), pile de DAIF par cœur (20 niveaux)
         prfm pstl1keep,[x1] ; sevl
      1: wfe ; ldaxr w3,[x1] ; cbnz w3,1b ; stxr w3,w2,[x1] ; cbnz w3,1b
Release: stlr wzr,[x1]                   ; l'écriture libère le moniteur → réveille les WFE
         [LeaveCritical]                 ; dmb ; restaure DAIF
```
Niveaux : `TASK_LEVEL=0` (rien masqué), `IRQ_LEVEL=1` (I masqué), `FIQ_LEVEL=2` (I+F). `EnterCritical` mémorise DAIF dans `s_nFlags[core][level]`.

### 6.3 Barrières et cache (`synchronize64.h/.cpp`)

| Macro Circle | Instruction |
|---|---|
| `DataSyncBarrier` | `dsb sy` |
| `DataMemBarrier` | `dmb sy` |
| `InstructionSyncBarrier` | `isb` |
| `InvalidateInstructionCache` | `ic iallu` |
| `CleanDataCacheRange` | `dc cvac` par ligne de 64 o + `dsb sy` |
| `InvalidateDataCacheRange` | `dc ivac` … |
| `CleanAndInvalidateDataCacheRange` | `dc civac` … |
| `CleanDataCache` / `InvalidateDataCache` | set/way : L1 256 sets × 2 voies (way<<31, set<<6, level 0), L2 1024 sets × 16 voies (way<<28, set<<6, level 1<<1) |
| `SyncDataAndInstructionCache` | clean set/way + `ic iallu` + `dsb` + `isb` |

Ligne de cache = **64 octets** (`DATA_CACHE_LINE_LENGTH_MIN/MAX`). Buffers DMA : base **et** taille alignées 64 o (`doc/dma-buffer-requirements.txt`).

### 6.4 Pièges

- Les set/way ne sont **pas** diffusés aux autres cœurs : ne les utiliser qu'au boot/arrêt. En régime établi : opérations par VA (`dc cvac/ivac/civac`), qui sont diffusées dans le domaine inner‑shareable.
- Les spinlocks exigent la MMU active (exclusives sur Normal cacheable).
- Un cœur secondaire lit la spin table caches éteints → **clean** après écriture (Circle fait un clean complet ; un `dc civac` sur la ligne 0xC0–0xFF suffit, suivi de `dsb sy ; sev`).
- `EnterCritical` imbriqué ne doit jamais redescendre de FIQ_LEVEL à IRQ_LEVEL.
- Sur Pi 4, Circle utilise partout `dsb sy` ; dans un noyau SMP, `dsb ish`/`dmb ish` suffisent entre cœurs, `dsb sy` reste nécessaire avant d'agir sur un périphérique/DMA.
- Accès MMIO à des périphériques différents : le manuel BCM2835 recommande un `dmb` entre deux périphériques distincts (Circle a `PeripheralEntry/Exit` vides en 64 bits ; le mapping nGnRE ordonne déjà les accès au même périphérique). Mettre un `dmb sy` dans nos routines d'entrée de pilote, par sécurité.

### 6.5 Transposition asm

- Zone per‑CPU : 4 blocs de 4 Ko, `TPIDR_EL1 ← base + core*0x1000` dès le boot de chaque cœur (accès O(1) au « current », au compteur de critique, au flag de reschedule).
- Spinlock = un `u32` aligné sur 64 o (éviter le faux partage). Routines : `spin_lock(x0)`, `spin_unlock(x0)`, `spin_lock_irqsave(x0) → x0=daif`, `spin_unlock_irqrestore(x0=lock, x1=daif)`.

```asm
spin_lock_irqsave:          ; x0 = &lock ; retourne x1 = DAIF précédent
        mrs     x1, daif
        msr     daifset, 2
        mov     w2, 1
        sevl
.w:     wfe
        ldaxr   w3, [x0]
        cbnz    w3, .w
        stxr    w3, w2, [x0]
        cbnz    w3, .w
        ret
spin_unlock_irqrestore:     ; x0 = &lock, x1 = DAIF
        stlr    wzr, [x0]
        msr     daif, x1
        ret
```
- Démarrage des cœurs : `smp_boot` = pour n=1..3 : `str x_entry, [0xD8+8n]` ; `dc civac` ; `dsb sy` ; `sev` ; attendre un flag `cpu_online[n]` (en RAM cacheable, mis par le cœur après activation MMU).

---

## 7. Ordonnanceur (`lib/sched/`)

### 7.1 Fonctionnement Circle (coopératif, mono‑cœur)

- `CScheduler` : tableau `m_pTask[MAX_TASKS=20]`, `m_pCurrent`, `m_nCurrent`. Une seule instance, **uniquement sur le cœur 0** (les autres cœurs exécutent `Run(core)` sans ordonnanceur).
- États (`task.h`) : `New, Ready, Blocked, BlockedWithTimeout, Sleeping, Terminated, Unknown` ; drapeau `m_bSuspended`.
- `Yield()` : `GetNextTask()` = round‑robin à partir de `m_nCurrent+1` ; réveille les `Sleeping/BlockedWithTimeout` dont `WakeTicks` (en µs, `GetClockTicks`) est échu ; supprime les `Terminated` (sauf la tâche courante). **Si aucune tâche prête : boucle active** (`while (GetNextTask() == MAX_TASKS) {}`). Puis `TaskSwitch(old, new)`.
- `TaskSwitch` (`taskswitch.S`, branche 64 bits) : sauve `x16..x30, sp, d8..d15, fpcr, fpsr, tpidr_el0` dans `TTaskRegisters` (le registre `x0` n'est pas sauvé, seulement rechargé = paramètre de `TaskEntry`) ; recharge ceux de la nouvelle tâche ; `ret` (via x30).
- Création (`CTask::InitializeRegs`) : pile `new u8[TASK_STACK_SIZE]` ; `x0 = this`, `sp = pile+taille`, `x30 = TaskEntry`, `fpcr` courant. `TaskEntry` → `Run()` → `Terminated` → `Yield()`.
- Synchronisation : `BlockTask(&waitlist, µs, pState)` / `WakeTasks(&waitlist)` protégés par un spinlock ; `CMutex`, `CSemaphore`, `CSynchronizationEvent` construits dessus. `WakeTasks` est utilisable depuis une IRQ.
- Pas d'espace d'adressage par tâche, pas d'EL0, pas de priorité.

### 7.2 Ce qu'il faut changer pour du préemptif SMP multi‑processus

1. **Source de préemption** : IRQ 30 (CNTP) activée **sur chaque cœur**, quantum décrémenté au tick, flag `need_resched` per‑CPU (§5.3).
2. **Pile d'exception** : Circle utilise une pile d'exception **partagée par cœur** → impossible de changer de tâche au milieu d'une IRQ. Il faut **une pile noyau par thread** (`SP_EL1`), la trame d'exception étant empilée dessus (§3.3).
3. **Contexte complet** : la trame contient x0–x30, SP_EL0, ELR_EL1, SPSR_EL1 ; le changement de contexte se fait en **changeant de pile noyau** puis `RESTORE_TF ; eret`. `TaskSwitch` (callee‑saved seulement) reste valable pour les blocages volontaires dans le noyau, mais un modèle unique « tout passe par une trame » est plus simple en asm.
4. **Espace d'adressage** : TTBR0 + ASID par processus, commuté seulement si le processus change (threads du même processus partagent TTBR0).
5. **FPU** : `CPACR_EL1.FPEN = 0b01` (piège EL0 seulement) et sauvegarde paresseuse de `q0–q31 + FPCR + FPSR` (528 o) à la première instruction FP (EC 0x07) ; ou sauvegarde systématique si le thread a le drapeau `TH_USES_FP`. Le noyau asm n'utilise jamais FP/SIMD.
6. **TLS** : `TPIDR_EL0` sauvé/restauré (déjà dans Circle) ; `TPIDRRO_EL0` éventuellement pour l'ID de thread.
7. **Tâche idle par cœur** : `wfi` au lieu de la boucle active.
8. **Files** : file prête par cœur (ou globale + spinlock pour commencer), liste de dormeurs triée par échéance, IPI SGI 1 = « reschedule » pour réveiller un cœur en `wfi`.
9. **Sections critiques** : remplacer `EnterCritical` par `spin_lock_irqsave` ; ne jamais commuter en tenant un spinlock (compteur `preempt_count` per‑CPU).
10. **Terminaison** : libérer la pile noyau d'un thread mort depuis un autre thread (même problème que Circle : on ne peut pas libérer la pile sur laquelle on tourne).

### 7.3 Structures proposées

```
; Thread Control Block (TCB) — aligné 64 o
TH_KSP        = 0      ; u64 SP noyau sauvegardé (pointe sur la trame TF)
TH_PROC       = 8      ; u64 -> descripteur de processus (TTBR0/ASID)
TH_STATE      = 16     ; u32 : 0 ready, 1 running, 2 blocked, 3 sleeping, 4 zombie
TH_QUANTUM    = 20     ; u32 ticks restants
TH_PRIO       = 24     ; u32
TH_CPU        = 28     ; u32 dernier cœur
TH_WAKE       = 32     ; u64 échéance CNTPCT
TH_NEXT       = 40     ; u64 chaînage file
TH_TPIDR_EL0  = 48     ; u64
TH_FLAGS      = 56     ; u32 (USES_FP, KERNEL_THREAD…)
TH_KSTACK     = 64     ; u64 base pile noyau (16 Ko)
TH_QUANTUM_INIT = 72   ; u32 quantum par défaut (ticks)
TH_FPSTATE    = 128    ; 32*16 + 16 = 528 o (q0-q31, fpcr, fpsr), aligné 16
TH_SIZE       = 1024
```
Création d'un thread utilisateur : allouer pile noyau, y fabriquer une trame factice : `TF_ELR = entrée`, `TF_SPSR = 0x000` (EL0t, DAIF démasqués), `TF_SP0 = pile utilisateur`, `x0 = argument` ; `TH_KSP = adresse de la trame`. Thread noyau : `TF_SPSR = 0x005` (EL1h).

### 7.4 Context switch préemptif (pseudo‑asm FASMARM)

Appelé en fin de chemin IRQ/SVC, **sur la pile noyau du thread courant**, la trame complète étant déjà empilée (`SAVE_TF`) :

```asm
; preempt_check : si need_resched, choisit un autre thread et bascule de pile.
; Entrée : sp = trame du thread courant. Sortie : sp = trame du thread élu.
preempt_check:
        mrs     x9, tpidr_el1                    ; per-CPU
        ldr     w10, [x9, PCPU_NEED_RESCHED]
        cbz     w10, .ret
        ldr     w10, [x9, PCPU_PREEMPT_COUNT]
        cbnz    w10, .ret                        ; spinlock tenu : pas de bascule
        str     wzr, [x9, PCPU_NEED_RESCHED]
        mov     x20, x30                         ; x19-x28 libres : déjà dans la trame
        ldr     x19, [x9, PCPU_CURRENT]          ; prev
        bl      sched_pick_next                  ; x0 = next (remet prev en file si runnable)
        mrs     x9, tpidr_el1                    ; x9 est volatil : recharger après bl
        cmp     x0, x19
        b.eq    .same
        ; --- sauvegarder prev ---
        mov     x1, sp                           ; sp = trame de prev (feuille : rien empilé)
        str     x1, [x19, TH_KSP]
        mrs     x2, tpidr_el0
        str     x2, [x19, TH_TPIDR_EL0]
        ; --- FPU paresseuse : couper l'accès EL0, il sera repiégé ---
        mrs     x2, cpacr_el1
        bic     x2, x2, 3 shl 20
        orr     x2, x2, 1 shl 20                 ; FPEN=01 : trap EL0
        msr     cpacr_el1, x2
        ; --- charger next ---
        str     x0, [x9, PCPU_CURRENT]
        ldr     x2, [x0, TH_TPIDR_EL0]
        msr     tpidr_el0, x2
        ldr     x3, [x0, TH_PROC]
        ldr     x4, [x19, TH_PROC]
        cmp     x3, x4
        b.eq    .same_as
        ldr     x5, [x3, PROC_TTBR0]             ; (ASID << 48) | PA table L2
        msr     ttbr0_el1, x5
        isb                                      ; pas de TLBI : ASID + pages noyau nG=0
.same_as:
        ldr     w5, [x0, TH_QUANTUM_INIT]
        str     w5, [x0, TH_QUANTUM]
        ldr     x1, [x0, TH_KSP]
        mov     sp, x1                           ; SP_EL1 = trame de next
        br      x20                              ; retour vers RESTORE_TF ; eret
.same:  mov     x30, x20
.ret:   ret
```
Remarques :
- Version simplifiée : l'appelant (`el0_irq`) n'a rien empilé d'autre que la trame ; `preempt_check` est une feuille qui ne touche pas à la pile (sinon ajuster l'offset).
- Le spinlock de la file prête doit être relâché **avant** `mov sp, x1` (ou le thread élu le relâche — schéma « finish_switch »).
- Si un thread migre de cœur, la cohérence est assurée par le matériel (inner shareable) ; seules les données per‑CPU via `TPIDR_EL1` diffèrent.
- `ASID` : allocateur 16 bits avec génération ; à l'épuisement, `tlbi vmalle1is ; dsb ish ; isb` + réaffectation paresseuse.

---

## 8. Mailbox et property tags (`bcmmailbox.cpp`, `bcmpropertytags.cpp`)

### 8.1 Registres (`bcm2835.h`, Pi 4 : `MAILBOX_BASE = 0xFE00B880`)

| Registre | Adresse | Bits |
|---|---|---|
| `MAILBOX0_READ` | `0xFE00B880` | données[31:4] | canal[3:0] |
| `MAILBOX0_STATUS` | `0xFE00B898` | bit 30 = EMPTY |
| `MAILBOX1_WRITE` | `0xFE00B8A0` | données | canal |
| `MAILBOX1_STATUS` | `0xFE00B8B8` | bit 31 = FULL |

Canal **8** = property tags ARM → VC (`BCM_MAILBOX_PROP_OUT`).

### 8.2 Séquence `WriteRead`

1. (Spinlock global, sauf usage précoce.)
2. `Flush` : tant que `STATUS0 & EMPTY == 0`, lire `READ` (+ 20 ms d'attente).
3. `Write` : attendre `STATUS1 & FULL == 0` ; `WRITE ← bus_addr | 8` (adresse alignée **16 o**).
4. `Read` : attendre `!(STATUS0 & EMPTY)` ; lire `READ` ; recommencer tant que `(v & 0xF) != 8` ; résultat = `v & ~0xF`, doit égaler l'adresse envoyée.

### 8.3 Format du buffer (`GetTags`)

Buffer placé dans la **page cohérente slot 0** (`0x500000`, mappée Device‑nGnRnE ⇒ aucune maintenance de cache) :
```
u32 taille_totale        ; octets, multiple de 4 (12 + tags + 4)
u32 code                 ; 0 = requête ; réponse 0x80000000 succès, 0x80000001 erreur
--- pour chaque tag ---
u32 tag_id
u32 taille_tampon_valeur ; octets, multiple de 4
u32 longueur_requête     ; réponse : bit31 = 1 | longueur
u8  valeur[taille_tampon]
--- fin ---
u32 0                    ; PROPTAG_END
```
Puis `DSB` → `WriteRead(BUS_ADDRESS(buf) = buf | 0xC0000000)` → `DMB` → vérifier code `0x80000000` → recopier. Pour un tag isolé (`GetTag`), vérifier aussi `longueur & 0x7FFFFFFF != 0`.

Tags utiles (`bcmpropertytags.h`) :

| Tag | ID | Requête → réponse |
|---|---|---|
| `GET_FIRMWARE_REVISION` | 0x00000001 | → u32 |
| `GET_BOARD_MODEL / REVISION` | 0x00010001 / 0x00010002 | → u32 |
| `GET_MAC_ADDRESS` | 0x00010003 | → 6 o (GENET) |
| `GET_BOARD_SERIAL` | 0x00010004 | → u64 |
| **`GET_ARM_MEMORY`** | 0x00010005 | → base, taille (RAM ARM < 1 Go) |
| `GET_VC_MEMORY` | 0x00010006 | → base, taille |
| `SET_POWER_STATE` | 0x00028001 | id, état |
| **`GET_CLOCK_RATE`** | 0x00030002 | id → id, Hz (UART=2, ARM=3, CORE=4, EMMC2=12) |
| `GET_MAX_CLOCK_RATE` / `SET_CLOCK_RATE` | 0x00030004 / 0x00038002 | (montée en fréquence ARM) |
| `GET_TEMPERATURE` | 0x00030006 | |
| `NOTIFY_XHCI_RESET` | 0x00030058 | adresse PCI du VL805 — **obligatoire avant l'init xHCI sur Pi 4B** (charge le firmware VL805) |
| `SET_DOMAIN_STATE` | 0x00038030 | |
| `SET_SET_GPIO_STATE` | 0x00038041 | GPIO expander (LED, etc.) |
| `GET_EDID_BLOCK` | 0x00030020 | |
| framebuffer | 0x0004xxxx / 0x0004 8xxx | voir §9 |
| `GET_COMMAND_LINE` | 0x00050001 | cmdline.txt |
| `GET_DMA_CHANNELS` | 0x00060001 | masque des canaux DMA libres |

### 8.4 Transposition asm

- Buffer statique de 4 Ko aligné 16 dans une zone mappée **non cacheable** (ou Normal WB + `dc civac` avant/après).
- `mbox_call(x0 = adresse buffer)` → `x0 = 1` succès / `0` échec. Protection par un spinlock global (le firmware ne gère qu'une requête à la fois).
- Macros FASM pour construire les buffers statiquement :
```asm
        align 16
mbox_getmem:
        dd 8*4, 0                     ; taille, requête
        dd 0x00010005, 8, 0           ; GET_ARM_MEMORY
.base   dd 0
.size   dd 0
        dd 0                          ; END
```
(Directives FASM : `dd` = 32 bits, `dq` = 64 bits ; `dw` = 16 bits.)

---

## 9. Framebuffer (`bcmframebuffer.cpp`)

### 9.1 Séquence Circle

1. (Pi 4) `GET_NUM_DISPLAYS` (0x00040013) ; `SET_DISPLAY_NUM` (0x00048013, valeur = n° écran) avant toute opération si plusieurs écrans (`max_framebuffers=2`).
2. Si la taille n'est pas imposée : `GET_DISPLAY_DIMENSIONS` (0x00040003) ; repli 640×480 si hors [640..4096]×[480..2160].
3. **Un seul appel `GetTags`** avec 6 tags (structure `TBcmFrameBufferInitTags`) :

| Tag | ID | taille tampon / requête | Valeur envoyée |
|---|---|---|---|
| `SET_PHYS_WIDTH_HEIGHT` | 0x00048003 | 8 / 8 | largeur, hauteur |
| `SET_VIRT_WIDTH_HEIGHT` | 0x00048004 | 8 / 8 | largeur virtuelle, hauteur ×2 si double buffer |
| `SET_DEPTH` | 0x00048005 | 4 / 4 | **32** |
| `SET_VIRTUAL_OFFSET` | 0x00048009 | 8 / 8 | 0, 0 |
| `ALLOCATE_BUFFER` | 0x00040001 | 8 / 4 | alignement (0 = défaut firmware) → adresse bus, taille |
| `GET_PITCH` | 0x00040008 | 4 / 0 | → octets par ligne |

4. Vérifications : aucun champ de réponse nul.
5. `fb = adresse & 0x3FFFFFFF` (adresse bus → physique ARM) ; `pitch` tel que renvoyé (**ne pas supposer `largeur*4`**).
6. Optionnel : `SET_BACKLIGHT` (0x0004800F), `SET_PALETTE` (0x0004800B) pour 8 bpp.

Pendant l'exécution : `SET_VIRTUAL_OFFSET` (y = 0 ou hauteur) pour le double buffering ; `WAIT_FOR_VSYNC` (0x0004800E) ; curseur matériel `SET_CURSOR_INFO/STATE` (0x8010/0x8011).

Format 32 bpp : Circle déclare `ARGB8888` (pixel = `0xAARRGGBB` en little‑endian) ; `framebuffer_ignore_alpha=1` dans config.txt si l'alpha pose problème (commenté pour Pi 5). Le tag `SET_PIXEL_ORDER` (0x00048006) n'est pas utilisé par Circle.

### 9.2 Pièges

- Le framebuffer est dans la mémoire GPU `[memsize, 1 Go)` : Circle la mappe **Device‑nGnRE** ⇒ correct mais écritures lentes, et **interdiction** de `dc zva`, `ldp/stp` non alignés, `memcpy` vectoriel non aligné.
- **Proposition GUI** : mapper le framebuffer en **Normal Non‑Cacheable** (ajouter `MAIR Attr3 = 0x44`) : write‑combining, accès non alignés autorisés, aucune maintenance de cache. Variante plus rapide : Normal WB + `dc cvac` des lignes modifiées puis `dsb sy` avant le flip.
- Composer dans un back‑buffer en RAM cacheable puis copier les rectangles sales (modèle MenuetOS « redraw ») est le meilleur compromis.
- `gpu_mem` (config.txt) doit laisser assez de mémoire au GPU pour 2 × 1920×1080×4 ≈ 16 Mo.

### 9.3 Transposition asm

Structure globale `FB` : `FB_BASE (u64)`, `FB_SIZE (u32)`, `FB_W, FB_H, FB_VW, FB_VH, FB_PITCH, FB_DEPTH (u32)`, `FB_CUR_PAGE (u32)`. Routines : `fb_init(w0=w, w1=h)`, `fb_flip`, `fb_wait_vsync`, `fb_putpixel(x0=x, x1=y, w2=argb)` (adresse = `base + y*pitch + x*4`), `fb_fill_rect`, `fb_blit`.

---

## 10. UART PL011 (`serial.cpp`) + GPIO 14/15 (`gpiopin.cpp`)

### 10.1 Registres (`ARM_UART0_BASE = 0xFE201000`)

| Registre | Offset | Bits utiles |
|---|---|---|
| DR | 0x00 | données |
| FR | 0x18 | TXFF bit5, RXFE bit4, BUSY bit3 |
| IBRD | 0x24 | diviseur entier (16 bits) |
| FBRD | 0x28 | diviseur fractionnaire (6 bits) |
| LCRH | 0x2C | WLEN[6:5] (11 = 8 bits), FEN bit4, STP2 bit3, PEN bit1 |
| CR | 0x30 | UARTEN bit0, TXE bit8, RXE bit9 |
| IFLS | 0x34 | seuils FIFO (TX[2:0], RX[5:3]) |
| IMSC | 0x38 | RX bit4, TX bit5, RT bit6, OE bit10 |
| MIS / ICR | 0x40 / 0x44 | |

### 10.2 Séquence Circle (115200 8N1)

1. GPIO : `GPFSEL1 (0xFE200004)` : broche 14 bits[14:12] et broche 15 bits[17:15] ← `100` (**ALT0**). Pull‑up sur RXD : `GPPUPPDN0 (0xFE2000E4)` bits[31:30] (broche 15) ← `01` (codage BCM2711 : 00 aucun, 01 pull‑up, 10 pull‑down ; pas de séquence GPPUD/GPPUDCLK sur Pi 4).
2. Horloge UART : `GET_CLOCK_RATE(id=2)` → **48 000 000 Hz** (défaut si le tag échoue).
3. Diviseurs : `IBRD = clk / (16*baud)` ; `f2 = (clk % (16*baud)) * 8 / baud` ; `FBRD = f2/2 + f2%2`. Pour 115200 : **IBRD = 26, FBRD = 3**.
4. Écritures : `IMSC ← 0` ; `ICR ← 0x7FF` ; `IBRD ← 26` ; `FBRD ← 3` ; (mode IRQ : `IFLS ← 0x09` (1/4 – 1/4), `LCRH ← 0x70`, `IMSC ← RX|RT|OE = 0x450`) ou (polling : `LCRH ← 0x70`) ; `CR ← 0x301`.
5. IRQ : `ARM_IRQ_UART` = 153.

### 10.3 Pièges

- Circle n'écrit pas `CR ← 0` avant de reprogrammer : la spec PL011 demande UART désactivée et FIFO vide (`FR.BUSY == 0`) avant de modifier LCRH/IBRD. **Faire `CR ← 0`, attendre `!BUSY`, puis programmer.** La valeur de `IBRD/FBRD` n'est prise en compte qu'à l'écriture de `LCRH`.
- Sur Pi 4, par défaut le PL011 est câblé au Bluetooth et le mini‑UART sur GPIO14/15 : la reprogrammation des broches en ALT0 (faite par Circle) suffit à récupérer le PL011 sur les broches (le Bluetooth perd son UART). `enable_uart=1` est inutile ; `core_freq` n'affecte pas le PL011.
- Utiliser un adaptateur 3,3 V (TX=GPIO14 broche 8, RX=GPIO15 broche 10, GND broche 6).

### 10.4 Transposition asm (polling, dès le boot, avant MMU)

```asm
UART0  = 0xFE201000
GPIO   = 0xFE200000
uart_init:                       ; 115200 8N1, horloge 48 MHz supposée
        mov64   x0, GPIO
        ldr     w1, [x0, 0x04]           ; GPFSEL1
        bic     w1, w1, 0x3F shl 12      ; broches 14,15
        mov     w2, 0x24                 ; 0x24<<12 n'est pas un immédiat logique encodable
        orr     w1, w1, w2, lsl 12       ; ALT0 (100) pour les deux
        str     w1, [x0, 0x04]
        ldr     w1, [x0, 0xE4]           ; GPPUPPDN0
        bic     w1, w1, 0xF shl 28
        orr     w1, w1, 1 shl 30         ; GPIO15 pull-up
        str     w1, [x0, 0xE4]
        mov64   x0, UART0
        str     wzr, [x0, 0x30]          ; CR = 0
.busy:  ldr     w1, [x0, 0x18]
        tbnz    w1, 3, .busy
        str     wzr, [x0, 0x38]          ; IMSC = 0
        mov     w1, 0x7FF
        str     w1, [x0, 0x44]           ; ICR
        mov     w1, 26
        str     w1, [x0, 0x24]           ; IBRD
        mov     w1, 3
        str     w1, [x0, 0x28]           ; FBRD
        mov     w1, 0x70
        str     w1, [x0, 0x2C]           ; LCRH 8N1 + FIFO
        mov     w1, 0x301
        str     w1, [x0, 0x30]           ; CR
        ret
uart_putc:                       ; w0 = caractère
        mov64   x1, UART0
.w:     ldr     w2, [x1, 0x18]
        tbnz    w2, 5, .w                ; TXFF
        str     w0, [x1]
        ret
```
(Sans MMU, les accès MMIO sont de type Device‑nGnRnE par défaut : sans danger.)

---

## 11. Ordre d'initialisation proposé pour l'OS (cœur 0)

1. `_start` (EL2) : sauver DTB, piles, bascule EL1h, `VBAR_EL1`, BSS, `TPIDR_EL1` per‑CPU.
2. `uart_init` (polling) → traces dès la première milliseconde.
3. Property tags (buffer non caché, MMU off) : `GET_ARM_MEMORY`, `GET_BOARD_REVISION` (taille RAM totale), `GET_CLOCK_RATE(UART)`.
4. Allocateur de pages physiques (bitmap, pages de 64 Ko) : `[fin noyau, memsize)` + `[1 Go, 3 Go)` (exclure spin table, région cohérente).
5. Tables MMU noyau (identité, attributs §2), `mmu_enable` (MAIR=`0x440004FF` avec Attr3=Normal NC, TCR, TTBR0, SCTLR).
6. GIC (distributor + CPU interface), timer cœur 0, IRQ démasquées.
7. Framebuffer (`fb_init`), console graphique.
8. Ordonnanceur : threads idle, threads noyau.
9. `smp_boot` des cœurs 1–3 → chacun : MMU, `gic_init_cpu` (PPI 30), `timer_init_core`, entrée dans `schedule()`.
10. Pilotes : PCIe + `NOTIFY_XHCI_RESET` + xHCI (USB), EMMC2, GENET, puis WiFi (SDIO, cf. étude dédiée).

## 12. Récapitulatif des pièges transverses

- **Barrières** : `dsb` après écriture de tables/buffer mailbox/descripteurs DMA ; `isb` après tout `msr` qui change le contexte d'exécution (SCTLR, TTBR0, TCR, VBAR, CPACR) ; `dsb` + `isb` après `tlbi`.
- **Cache vs DMA** : buffers DMA alignés 64 o (base et taille) ; `dc cvac` avant lecture par le périphérique, `dc ivac` après écriture par le périphérique ; ou zone non cacheable dédiée (comme la région cohérente de Circle, 4 Mo à 0x500000, découpée en slots de 64 Ko : mailbox=0, VCHIQ, xHCI 1–4 Mo).
- **DMA ≤ 3 Go** sur Pi 4 ; adresse bus VC = `phys | 0xC0000000` pour le DMA legacy/mailbox.
- **Alignement** : VBAR 2 Ko, tables MMU alignées sur leur taille (64 Ko pour L3, ≥ taille de la table pour L2), pile 16 o, buffer mailbox 16 o.
- **MMIO** : accès 32 bits uniquement ; mémoire Device interdit les accès non alignés et `dc zva`.
- **SMP** : `SMPEN` (fait par l'armstub), MMU active avant tout spinlock, set/way réservés au boot, registres GIC bankés (PPI/SGI) à initialiser sur chaque cœur.
- **EL** : noyau en EL1h (pas EL1t comme Circle) pour libérer `SP_EL0` pour l'utilisateur.

## 13. Convention d'appel asm proposée (procédurale)

- Inspirée d'AAPCS64 pour pouvoir lier plus tard du C si besoin : arguments/retours `x0–x7`, temporaires `x9–x15`, **préservés `x19–x28`**, `x29` FP, `x30` LR, `x8` = n° de syscall.
- `x18` réservé (non utilisé) ; per‑CPU via `TPIDR_EL1`, TLS utilisateur via `TPIDR_EL0`.
- Feuilles sans pile ; fonctions non feuilles : `stp x29, x30, [sp,-16]!` … `ldp x29, x30, [sp], 16 ; ret`.
- Pas de FP/SIMD dans le noyau (simplifie la sauvegarde paresseuse).
- Un fichier `.inc` par sous‑système (constantes + offsets de structures) et un `.asm` par sous‑système inclus dans `kernel.asm` (FASM assemble en une seule passe multi‑fichiers, pas d'éditeur de liens).
