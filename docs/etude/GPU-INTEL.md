# GPU Intel du PC : Tiger Lake UHD (Gen12 LP), étude

Chantier commencé le 13/11/2026 sur la branche `claude/x86-64-pc` (voir [ETUDE-X86.md](ETUDE-X86.md) § 7.4 et
[PC-PILOTES.md](PC-PILOTES.md), section « GPU Intel, étape 1 »). But : le rendu 3D par le GPU intégré du ZBook, avec
Mesa (pilote **iris**), puis GlKit et GpuKit pour le PC (même table `.abi` que le Pi), avant .NET (Avalonia sur le GPU)
et Jet. La NVIDIA T1200 vient tout à la fin, si jamais.

**Rien ne peut être essayé ici sur le vrai GPU** : QEMU n'émule aucun GPU Intel. Ce qui est vérifié sous QEMU est dit
comme tel ; tout le reste attend le journal du ZBook.

## 0. En bref

| Question | Choix |
|---|---|
| Où est le pilote | un **service en C** `intelgpu` (`src/svc/intelgpu`, PC seulement), seul à toucher aux registres |
| Comment Mesa lui parle | un **faux libdrm** (`tools/mesa/libdrm-pc`) qui sert les ioctls `i915` d'iris, comme `tools/mesa/libdrm` sert v3d sur le Pi |
| Soumission | **execlists** (registres ELSQ du moteur), **sans GuC** : le mode par défaut d'i915 sur Tiger Lake |
| Espaces d'adresses | GGTT pour ce qui est au service (anneaux, contextes, page d'état) ; une **PPGTT à 4 niveaux par client** (adresses choisies par iris : *softpin*) |
| Micrologiciel | **aucun** pour le rendu : GuC et HuC facultatifs (HuC : vidéo seulement), DMC = affichage |
| Cohérence | Tiger Lake a un **LLC partagé** : BO en écriture différée (WB) des deux côtés, cohérents ; PAT 0 = WB |
| Interruptions | une **MSI** (`pci_irq_setup` de pcilib, `IRQ_MSI`), interruptions du GT seulement ; la sonde n'en a pas besoin (sondage) |
| Affichage | **on n'y touche pas** : l'écran reste au framebuffer du GOP ; plus tard, présenter par une copie du GPU vers ce framebuffer |
| Étape 1 (faite) | la sonde `intelgpu probe [submit]`, lancée à la main ; Mesa iris construit et relié pour Korin PC (`gltest --dry` passe sous QEMU) |
| Étape 2 (écrite, 13/11) | le service `intelgpu` (`test`, `start`, `status`, `stop`), lancé à la main ; le faux libdrm branché dessus ; `gltest --win` ; vérifié sous QEMU avec le faux GPU (`simu`), à essayer sur le ZBook (PC-PILOTES.md, « GPU Intel, étape 2 ») |

## 1. Le matériel du ZBook

Journal de pcid sur le vrai ZBook (13/11) :

| | |
|---|---|
| Fonction | `0000:00:02.0`, `8086:9A60` (Tiger Lake-H GT1, UHD Graphics, **32 EU**, Gen12 LP / Xe-LP) |
| BAR0 | `0x60_5200_0000`, 64 bits : **GTTMMADR**, 16 Mo : registres (MMIO) dans les 8 premiers Mo, **la GGTT** (tables de la traduction globale) dans les 8 Mo du haut |
| BAR2 | `0x40_0000_0000`, préchargeable : **GMADR**, l'ouverture (« aperture ») : une fenêtre du processeur vers l'espace de la GGTT ; le framebuffer du GOP y est vu (adresse `bi.fb_base` du noyau) |
| BAR4 | E/S `0x4000` : index/données vers le MMIO (inutile : la BAR0 suffit) |
| MSI | une capacité MSI (un vecteur, adresse sur 64 bits) ; pas de MSI-X |

Configuration PCI utile (lue par la sonde) :

* **GMCH_CTRL** (`0x50`, 16 bits) : **GGMS** (bits 7:6) = taille des tables de la GGTT (`1 << GGMS` Mo ; 8 Mo = 1 Mi
  d'entrées de 8 octets = 4 Go d'adresses GPU) ; **GMS** (bits 15:8) = taille de la mémoire « volée » (*stolen* :
  `GMS × 32 Mo` sous `0xF0`, sinon `(GMS − 0xF0 + 1) × 4 Mo`).
* **BDSM** : base de la mémoire volée, `0x5C` (32 bits) et, depuis Gen11, `0xC0`/`0xC4` (64 bits). Le framebuffer du
  GOP est dans cette mémoire, vu du GPU par les premières entrées de la GGTT.
* **ASLS** (`0xFC`) : l'OpRegion (ACPI : rétroéclairage, VBT) ; affichage, pas pour nous.

Moteurs de Tiger Lake GT1 : **RCS0** (rendu et calcul, base MMIO `0x2000`), **BCS0** (copieur, `0x22000`), VCS0 et
VECS0 (vidéo). Iris n'utilise que RCS0 (et BCS0 sur Gen12.5+) ; le copieur nous servira pour la présentation (§ 10).

## 2. Architecture

```
  app (GL ES, Skia, Avalonia...)          app            app
          │ GlKit (Mesa : iris + armgl)
          │ faux libdrm « i915 » (korin_i915.c, dans le process)
          │   BO : tranches de tampons partagés (vue WB du client)
          │   ioctls → messages au service (SVC_CALL), ou servis sur place
          ▼
  service intelgpu (src/svc/intelgpu, C, x86-64)  ── seul à mapper la BAR0 (DEV_MAP), à écrire la GGTT
     contextes (LRC), PPGTT par client, execlists, interruptions (MSI), délais et remises à zéro
          │
          ▼
  noyau du PC : DEV_MAP (BAR 64 bits), DMA_ALLOC (contigu), IRQ_MSI, BUF_* (tampons partagés)
```

* **Un seul propriétaire du GPU** : le service. Il prend la fonction PCIe à pcid (`PCI_OP_CLAIM`), mappe la BAR0,
  tient le forcewake, les tables, les contextes. Les clients ne voient jamais un registre.
* **Le nom** : `intelgpu` (service « intelgpu »). `i915` est le nom du pilote de Linux, dont on imite l'interface
  côté Mesa seulement.
* **Lancement** : à l'étape 1, à la main seulement (`/sys/svc/intelgpu probe`). Quand le service sera sûr :
  `pci:8086:9a60 = /sys/svc/intelgpu` dans `drivers.txt` (hwmanager le lance pour la fonction, comme usbd).
* **Pourquoi pas dans le noyau** : la règle du micronoyau (PC-PILOTES § 1). Un GPU bloqué se remet à zéro depuis le
  service ; un service qui tombe rend sa fonction (pcid) et ses pages (noyau).

## 3. Les ioctls d'iris et leur traduction

Iris parle au noyau par `intel_ioctl` (`src/intel/common/intel_gem.h`) et, pour voir un BO, par `mmap` sur le
descripteur DRM. `tools/mesa/korin-pc.patch` renvoie le premier à `drmIoctl` du faux libdrm et remplace le second par
`korin_i915_bo_map(handle)`. La liste vient des sources de Mesa 26.2 (iris, `src/intel/common`, `src/intel/dev`) :

| ioctl | Ce qu'iris en attend | Sur Korin |
|---|---|---|
| `I915_GETPARAM` | `CHIPSET_ID` 0x9A60, `REVISION`, `EU_TOTAL` 32, `SLICE_MASK`, `SUBSLICE_MASK`, `CS_TIMESTAMP_FREQUENCY`, `HAS_EXEC_{SOFTPIN, NO_RELOC, HANDLE_LUT, BATCH_FIRST, FENCE_ARRAY, TIMELINE_FENCES}`, `HAS_CONTEXT_ISOLATION`, `MMAP_GTT_VERSION`, `HAS_USERPTR_PROBE`, `PXP_STATUS` (→ `ENODEV`) | réponses fixes du service, lues une fois (identité et fusibles relevés par lui : la sonde les journalise déjà) |
| `I915_QUERY` | `TOPOLOGY_INFO` (tranches, sous-tranches, EU), `ENGINE_INFO`, `MEMORY_REGIONS` ; `HWCONFIG_BLOB`, `GEOMETRY_SUBSLICES`, `GUC_SUBMISSION_VERSION` absents sur Gen12 LP | idem |
| `GEM_CONTEXT_CREATE(_EXT)`, `_DESTROY`, `_SETPARAM`, `_GETPARAM` (`GTT_SIZE` = 2^48, `VM`, `PRIORITY`, `RECOVERABLE`, `ENGINES`) | un contexte matériel par contexte d'iris | `IG_OP_CTX_CREATE` / `_DESTROY` : le service fait une image LRC (copie du contexte « doré », § 5), liée à la PPGTT du client |
| `GEM_VM_CREATE`, `_DESTROY` | un espace d'adresses (PPGTT) | une PPGTT par client (et par VM demandée) |
| `GEM_CREATE(_EXT)` | un BO de la mémoire du système | dans le process : tranche d'un **morceau** (tampon partagé `BUF_CREATE`, 32 à 64 Mo, comme le Pi) ; le morceau est donné au service une fois (`BUF_GRANT`), le service lit ses pages physiques (`BUF_INFO`) |
| `GEM_MMAP_OFFSET` + `mmap` | la vue du processeur | `korin_i915_bo_map` : adresse du morceau + décalage (WB) |
| `GEM_SET_DOMAIN`, `SET_CACHING`, `MADVISE`, `GET/SET_TILING`, `BUSY`, `GET_APERTURE` | cohérence et tuilage de l'ancien temps | servis sur place (LLC : rien à faire ; Gen12 n'a plus de clôtures de tuilage) ; `BUSY` demande au service |
| `GEM_EXECBUFFER2(_WR)` | la liste des BO (avec leur adresse GPU : *softpin*, `EXEC_OBJECT_PINNED`), le lot, le moteur, les syncobj | `IG_OP_EXEC` : (morceau, décalage, taille, adresse GPU) de chaque BO pas encore lié ; le service écrit les PTE de la PPGTT, puis met le lot dans l'anneau du contexte (`MI_BATCH_BUFFER_START` en PPGTT) suivi d'une écriture du numéro de fin ; rend un numéro de séquence |
| `GEM_WAIT`, `SYNCOBJ_*` | attendre un BO ou une fin | numéros de séquence par contexte ; `IG_OP_WAIT` bloque dans le service jusqu'à l'interruption de fin (ou délai) ; les syncobj du faux libdrm gardent le numéro du dernier lot qui les signale |
| `REG_READ` (horodatage) | requêtes de temps de GL | `IG_OP_TIMESTAMP` (RCS `0x2358`) |
| `GEM_CLOSE`, `GEM_FLINK` / `GEM_OPEN` | partage par nom | comme le Pi : le nom d'une image de fenêtre est son numéro |
| `GEM_USERPTR`, `PRIME_*`, perf | facultatifs | refusés (`ENODEV`, `ENOSYS`) |

Le mode **à blanc** (`ARMPI_DRM_DRY`, déjà écrit dans `korin_i915.c`) donne exactement ces réponses sans service :
c'est ainsi que `gltest --dry` fait tourner iris sous QEMU (§ 12).

**Mémoire partagée** : les morceaux viennent de `BUF_CREATE` (128 tampons dans tout le système, 64 Mo au plus) : assez
pour commencer (iris a son propre cache de BO et ses tranches). Plus tard, si c'est trop serré : des objets SHM du
noyau (non contigus, la PPGTT n'en a pas besoin) avec un appel qui donne au service les pages physiques d'un objet
(et les épingle). Les BO **ne sont jamais** dans la GGTT (sauf ceux que le service présente, § 10).

## 4. GGTT et PPGTT

**GGTT** (BAR0 + 8 Mo) : entrées de 64 bits, bit 0 = présente, bits 38:12 = adresse physique ; pas de bits de cache
sur Gen12 (index PAT 0). Après des écritures : `GFX_FLSH_CNTL` (`0x101008`) = 1 vide le TLB de la GGTT. Ce qu'on y
trouve au démarrage (relevé par la sonde) : le framebuffer du GOP dans la mémoire volée, à partir de l'adresse GGTT 0
(c'est `PLANE_SURF` du pipe A, lu sans écrire), et des entrées vers une page de garde. Le service :

* ne touche **jamais** aux entrées du micrologiciel (le framebuffer) ;
* prend ses entrées après l'ouverture (256 Mo) et après la dernière entrée du GOP, loin du haut (le GuC réserve
  `0xFEE0_0000`..4 Go, même inutilisé) ; il remet les anciennes valeurs en partant ;
* y met ce qui est à lui : anneaux, images de contexte, pages d'état (HWSP), PML4 des PPGTT si besoin.

**PPGTT** (une par client) : 4 niveaux, 48 bits, tables en mémoire du système (`DMA_ALLOC` du service). Entrée Gen12 :
bit 0 présente, bit 1 écriture, bits PAT (3 PWT, 4 PCD, 7 PAT) = index dans la table PAT du GPU, adresse en 51:12.
Le contexte est en mode « 64B legacy » (descripteur, bits 4:3 = 3) et son image porte l'adresse de la PML4 dans
`PDP0` (§ 5). Après un changement de PTE d'un client en marche : invalidation par `PIPE_CONTROL` (TLB) en tête du lot,
que le service ajoute dans l'anneau. Pages de garde : une page à zéro pour tout trou demandé par iris.

**Table PAT du GPU** (`0x4800` + 4 × i, Gen12) : à écrire par le service comme i915 le fait sur Tiger Lake : 0 = WB,
1 = WC, 2 = WT, 3 = UC (la sonde la lit seulement). **MOCS** (états de cache par surface, tables globales
`0x4000`…) : iris choisit des index MOCS (`isl_mocs`) ; le service doit charger la table de Tiger Lake que ces index
supposent (PRM, « Memory Object Control State »).

## 5. Soumission par execlists, sans GuC

Registres du moteur (RCS0, base `0x2000`) : `RING_MODE` (`+0x29C`, écriture masquée, bit 3 = plus de mode « anneau »
sur Gen11+), `HWS_PGA` (`+0x80`, page d'état), `MI_MODE` (`+0x9C`, bit 8 `STOP_RING`, bit 9 `MODE_IDLE`), file de
soumission `EXECLIST_SQ` (`+0x510` : port 0 bas/haut, port 1), `EXECLIST_CONTROL` (`+0x550`, bit 0 = charger),
état `EXECLIST_STATUS` (`+0x234`), tampon d'état des changements de contexte (CSB : dans la HWSP en `0x10`, 12 entrées
de 2 mots, pointeur d'écriture en `0x2F` ; aussi en MMIO `+0x370`).

**Descripteur de contexte** (64 bits) : adresse GGTT de l'image (alignée sur 4 Ko) | valide (bit 0) | mode
d'adressage (bits 4:3 : 3 = PPGTT à 4 niveaux) | privilégié (bit 8, les contextes du service seulement) ; mot haut :
numéro du contexte (bits 37+, 11 bits) et, sur Gen11+, classe et instance du moteur (bits 61, 48 : 0 pour RCS0).

**Image du contexte** (LRC) : une page par processus (PPHWSP) puis l'état des registres. L'état est une suite de
`MI_LOAD_REGISTER_IMM` (registres relatifs au moteur, `MI_LRI_CS_MMIO`) dont la disposition est fixée par le
matériel : `CTX_CONTEXT_CONTROL`, `RING_HEAD`, `RING_TAIL`, `RING_START`, `RING_CTL`, registres des lots, puis
l'horodatage du contexte et les quatre `PDP` (la PML4 en `PDP0`), puis `R_PWR_CLK_STATE`. Pour un contexte neuf :
« restauration du moteur interdite » (`CTX_CONTEXT_CONTROL` bit 0), l'image est fermée par `MI_BATCH_BUFFER_END`.
Après la première exécution, le matériel sauve l'image complète : le service la garde comme **contexte doré** et
la recopie pour chaque nouveau contexte (c'est ce que fait i915 avec `default_state`). Taille réservée : 22 pages
pour RCS (la sonde en prend 56 par prudence).

**Une soumission** : écrire les ports 1 puis 0 de la file (`EXECLIST_SQ`), puis `EXECLIST_CONTROL` = 1. Le moteur
charge l'image, lit l'anneau de `HEAD` à `TAIL`. Fin : écriture du numéro de séquence dans la HWSP du contexte par
`PIPE_CONTROL` (écriture après les caches) puis `MI_USER_INTERRUPT` ; l'entrée du CSB dit « contexte fini » ; le
service fait avancer la file (au plus deux contextes en vol : ports 0 et 1).

**Mise en route du moteur** (faite par la sonde) : forcewake, `HWSTAM` = tout masqué, `RING_MODE` bit 3,
`MI_MODE` sans `STOP_RING`, `HWS_PGA`. **Arrêt** : `STOP_RING`, attente de `MODE_IDLE`. **Blocage** : délai par lot ;
remise à zéro du seul moteur de rendu : `RESET_CTL` (`+0xD0`) demande (bit 0) → prêt (bit 1), puis `GDRST`
(`0x941C`) bit 1 (domaine du rendu, jamais le bit 0 « tout »), attente de sa remise à 0. Le moteur d'affichage n'est
pas dans ce domaine.

**Contournements** (« workarounds ») : i915 en applique une cinquantaine sur Tiger Lake (registres du GT, du moteur,
du contexte). La liste des registres et des valeurs est dans le volume « Workarounds » du PRM et se recoupe avec
`intel_workarounds.c` d'i915 (lu comme référence, réécrit, pas copié). Ceux du contexte vont dans le contexte doré
(LRI au premier lot) ; ceux du GT, une fois au démarrage du service. À faire à l'étape 2, avant tout dessin : sans
eux, des blocages ou des pixels faux sont possibles (pas de danger pour la machine).

## 6. Forcewake

Le GT dort (RC6) dès qu'il n'a rien à faire ; un accès aux registres d'un domaine endormi rend des zéros ou se perd.
Gen11+ : un registre par domaine, en **écriture masquée** (16 bits du haut = bits à changer) : GT `0xA188` (accusé
`0x130044`), rendu `0xA278` (accusé `0x0D84`), média (pas utile). Le bit 0 est celui du pilote : écrire `0x00010001`,
attendre l'accusé (50 ms au plus), travailler, écrire `0x00010000`. La sonde le prend pour toute sa durée. Le service
le tiendra tant qu'un lot est en vol (puis RC6 à régler : i915 programme ses seuils et ses minuteries ; sans eux, le
GT reste éveillé ou s'endort au gré du micrologiciel : à mesurer sur le ZBook).

## 7. Micrologiciels

| | Rôle | Pour le rendu |
|---|---|---|
| **GuC** | soumission par micrologiciel, gestion de la fréquence (SLPC) | **non** : Tiger Lake soumet par execlists dans i915 (le GuC n'y est chargé que pour authentifier le HuC) |
| **HuC** | accélère le contrôle du débit des encodeurs vidéo | non (vidéo seulement) |
| **DMC** | états d'économie d'énergie de l'**affichage** (DC5/DC6) | non : on ne touche pas à l'affichage ; le GOP a laissé l'écran allumé |

Donc : **aucun micrologiciel à charger** pour le rendu. Conséquence : la fréquence du GPU reste celle que le
micrologiciel d'HP a laissée (pas de SLPC) ; à régler plus tard par les registres RPS (`GEN6_RPNSWREQ`…) si le rendu
est lent. Les binaires d'Intel (licence redistribuable) ne sont pas nécessaires.

## 8. Cohérence des caches

Tiger Lake a un **dernier niveau de cache partagé** (LLC) entre les cœurs et le GPU. Avec un index PAT ou MOCS en
écriture différée (WB), le GPU passe par le LLC et voit ce que le processeur a écrit ; ce que le GPU écrit est vu par le
processeur. C'est le cas « LLC » d'i915 (BO cohérents en lecture et en écriture). Donc :

* les BO sont en **WB** chez le client (pas de vidage), comme les tampons partagés de Korin sur PC ; `DMA_ALLOC` du
  PC est aussi en WB (le DMA du PC est cohérent) ;
* la **sonde** vide quand même ses lignes (`clflush` + `mfence`) avant la soumission et avant de relire : ainsi le
  résultat ne dépend pas de la table PAT laissée par le micrologiciel (elle est journalisée) ;
* **exception, l'affichage** : le moteur d'affichage lit la mémoire sans passer par le LLC. Le framebuffer du GOP est
  en écriture combinée chez le processeur (noyau du PC) ; une copie du GPU vers lui devra être non cachée (MOCS UC)
  ou suivie d'un vidage.

## 9. Interruptions

Une **MSI** pour toute la fonction (`pci_irq_setup` de pcilib : capacité MSI, ligne `IRQ_MSI`, puis `IRQ_BIND` /
`IRQ_WAIT`). Gen11+ : contrôle maître `GEN11_GFX_MSTR_IRQ` (`0x190010`), registres du GT (`GEN11_GT_INTR_DW0`
`0x190018`, sélection et identité de la source `0x190070` / `0x190060`, activation par classe de moteur `0x190030`,
masques par moteur `0x1900A8`…). Le service n'active que « contexte changé » et « interruption utilisateur » de RCS0
(et BCS0 plus tard) ; **jamais** les interruptions de l'affichage (bit « display » du maître laissé coupé). Pas de MSI :
sondage (la sonde de l'étape 1 sonde toujours, à 1 ms).

## 10. Pourquoi ne PAS toucher à l'affichage

* L'UEFI d'HP a allumé l'écran interne (eDP) en 1920 × 1080, pipe A, plan 1, depuis la mémoire volée. Tout le reste
  (horloges, PLL, DDI, séquence d'alimentation du panneau, rétroéclairage, DMC, puits d'alimentation) est la plus
  grosse part d'i915 et la plus risquée : un mauvais registre et l'écran est noir, sans console pour le dire.
* Korin n'en a pas besoin : Elegant compose déjà dans le framebuffer du GOP (copie du processeur).
* **Plus tard, la présentation par le GPU** : une copie du copieur (BCS0, `XY_FAST_COPY_BLT`) de l'image d'une fenêtre
  (BO du client) vers le framebuffer, que le GPU atteint par **ses propres entrées de la GGTT** (celles que le GOP a
  mises, adresse de `PLANE_SURF`) — sans écrire un seul registre de l'affichage. Elegant pourrait aussi composer par
  le GPU. Pas de synchronisation verticale au début (le compteur d'images du pipe se lit sans écrire, plus tard).
* Prix : pas de changement de résolution, pas de second écran, pas de veille de l'écran : acceptés.

La sonde lit deux registres de l'affichage (`PLANE_CTL_1_A`, `PLANE_SURF_1_A`) pour savoir où est le framebuffer dans
la GGTT ; elle n'en écrit aucun.

## 11. Étape 1 : la sonde

`src/svc/intelgpu` (C, `svc.conf` : `arch = x86_64`, `sources = ../pcid/pcilib.c`), dans `/sys/svc/intelgpu` de
l'arbre du PC (`tools/pc/sysroot.sh` copie `build/svc/x86_64`). **Jamais** lancée par hwmanager ni `drivers.txt`.

```
/sys/svc/intelgpu probe            identité, fusibles, horodatage, moteur et GGTT (lecture), forcewake
/sys/svc/intelgpu probe submit     + un lot minimal par execlists
   -v          chaque écriture de registre et de la GGTT au journal
   simu        un faux GPU en mémoire (QEMU) ; simu-hang : il ne répond pas (délai, remise à zéro)
```

| Fichier | Rôle |
|---|---|
| `igpu.h` | registres (PRM de Tiger Lake), commandes `MI_*`, état commun |
| `intelgpu.c` | recherche de la fonction (classe 03, vendeur 8086 ; seuls les identifiants de Tiger Lake vont au-delà de la configuration PCI), prise par pcid, `DEV_MAP` de la BAR0 (16 Mo, au-dessus de 4 Go), forcewake, identité, relevé du moteur, balayage de toute la GGTT, fenêtre libre, remise du registre de commande PCI tel qu'il était |
| `submit.c` | 64 pages `DMA_ALLOC` (HWSP, anneau de 16 Ko, lot, cible, PML4 vide, contexte) ; 64 entrées de la GGTT dans la fenêtre libre (anciennes valeurs gardées) ; image du contexte (partie « anneau », restauration interdite) ; anneau : `MI_BATCH_BUFFER_START` (GGTT) → lot : `MI_STORE_DATA_IMM` (`0x4B4F5249` vers la cible), `MI_BATCH_BUFFER_END` ; puis `MI_STORE_DATA_IMM` (`0x31555047` dans la HWSP) ; soumission ; attente 500 ms ; CSB et registres au journal ; `STOP_RING` et repos ; registres du moteur remis comme au départ ; entrées de la GGTT remises ; si le moteur ne s'arrête pas : remise à zéro du rendu ; si elle échoue : **le process reste en vie** (ses pages ne sont pas rendues au noyau tant que le GPU peut y écrire) et le dit |
| `simu.c` | le faux GPU : écritures masquées, accusés, exécution de l'image (LRI) et des commandes de l'anneau et du lot par les adresses de la GGTT |

Ce qu'elle écrit : registre de commande PCI (prise par pcid ; remis à sa valeur d'avant), forcewake ; avec `submit` :
64 entrées de la GGTT hors de celles du micrologiciel, `HWSTAM`, `RING_MODE`, `MI_MODE`, `HWS_PGA`, la file
d'execlists, `GFX_FLSH_CNTL`, puis l'anneau (`CTL`, `HEAD`, `TAIL`, `START`) à l'arrêt ; en cas de blocage
`RESET_CTL` et `GDRST` (bit du rendu). Rien dans l'affichage, rien dans la mémoire volée.

**pcid** (corrigé) : à la fin d'un pilote, pcid coupait le maître du bus de sa fonction ; pour l'affichage (classe 03)
il le laisse (le framebuffer du GOP). La sonde remet elle-même le registre de commande avant de rendre la fonction.

**Noyau du PC : rien à ajouter pour l'étape 1.** Vérifié dans le code : `DEV_MAP` projette toute adresse physique
qui n'est pas de la RAM (masque de 52 bits des PTE : la BAR0 à `0x60_5200_0000` passe), non cachée (PAT 3) ;
`DMA_ALLOC` donne des pages contiguës, à zéro, en WB (cohérent sur PC), rendues à la fin du process ; `IRQ_MSI` existe
(nvme, usbd). À ajouter plus tard : rien d'obligatoire ; un `DMA_ALLOC` en WC pour une copie vers l'affichage peut
servir (aujourd'hui, `clflush`).

## 12. Mesa iris pour Korin x86-64 (étape 1 : construit et relié)

| Fichier | Rôle |
|---|---|
| `tools/mesa/build-clc.sh` | `mesa_clc` et `vtn_bindgen2` pour la machine de construction (WSL) : iris compile ses noyaux internes (`src/intel/shaders`, OpenCL C) par eux. Paquets : `llvm-21-dev libclang-21-dev libclang-cpp21-dev clang-21 libclc-21-dev llvm-spirv-21 libllvmspirvlib-21-dev spirv-tools-dev` |
| `tools/mesa/configure-iris.sh` | meson croisé `x86_64-onyx-elf`, libposix du PC, `-Dgallium-drivers=iris -Dmesa-clc=system -Dintel-elk=false`, sortie `~/armpi-mesa/out-x86_64` (le Pi garde `out/`) |
| `tools/mesa/korin-pc.patch` | deux retouches, sources d'Intel seulement : `intel_ioctl` → `drmIoctl`, `mmap` du BO → `korin_i915_bo_map` |
| `tools/mesa/libdrm-pc/` | le faux libdrm du PC : `xf86drm.h` (avec `drmDevice` : iris lit l'identifiant PCI par `drmGetDevice2`), `korin_i915.c` (ioctls i915, mode à blanc, interface `armpi_drm_*` d'armgl), `korin_pre.h` (les `-include` du Pi, pas pour l'assembleur de blake3) |
| `tools/mesa/build-iris.sh` | les 63 bibliothèques de Mesa, `libarmgl.a` du PC (`armgl.c` avec `-DARMGL_IRIS` : `iris_screen_create`, `driinfo_iris.h`, modificateur linéaire, Z24S8 d'iris), `gltest` relié en statique (28 Mo sans symboles) |
| `tools/mesa/armgl/armgl.c` | `#ifdef ARMGL_IRIS` : rien ne change pour le Pi sans cette macro |

**Vérifié sous QEMU q35 (13/11)**, `gltest --dry` dans Korin PC (bureau Elegant, `KORIN_DRM_DEBUG=1`) :
« armgl_init 2 (a blanc) 39 ms », « Intel / Mesa Intel(R) UHD Graphics (TGL GT1) / OpenGL ES 3.2 Mesa 26.2.0 /
OpenGL ES GLSL ES 3.20 », « shaders GLSL ES compiles et lies en 476 ms », « dessin 51 ms, jobs soumis : 2,
glGetError 0 », « programme texture, jobs 4 », « a blanc : Mesa va au bout. PASS » : le compilateur d'iris
(brw) produit le code Gen12, les lots sont construits et « soumis » au faux libdrm. Refus attendus (journal du mode
`KORIN_DRM_DEBUG`) : deux `GETPARAM` inconnus (`-EINVAL`) et `PXP_STATUS` (`-ENODEV`), un `CONTEXT_GETPARAM` inconnu,
une requête perf (`-ENODEV`) ; iris s'en passe. `gltest` sans `--dry` : « pas de GPU », repli (service absent).
Premier essai trouvé : iris veut un **vrai descripteur** (`fstat`, copie `F_DUPFD_CLOEXEC`) : le faux libdrm rend
le bout de lecture d'un tube. Seul manque de libposix : `mkfifoat` (INTEL_MEASURE), remplacé par un refus faible.
`armgl.o` du Pi recompilé avant et après les `#ifdef` : désassemblage et données identiques.

Reste pour GlKit sur PC : les fenêtres (`armpi_drm_window` : une surface d'Elegant, deux images WB, présentées par
`WS_OP_SURFACE_ATTACH` comme sur le Pi, sans vidage grâce au LLC), le kit partagé (`build-glkit.sh` pour x86-64 :
`glkit.S` en x86-64, `glkit.ld`), puis le vrai service.

## 13. Les étapes

1. **(fait)** La sonde, l'étude, Mesa iris construit. L'utilisateur lance la sonde sur le ZBook (PC-PILOTES.md,
   marche à suivre) et rapporte le journal.
2. **(écrite, à essayer sur le ZBook)** Le service `intelgpu` : prise, forcewake, PAT, MOCS et contournements du GT ; GGTT ; contexte doré ; PPGTT par
   client ; execlists avec deux ports et file ; MSI ; délais et remise à zéro ; protocole `IG_OP_*` (§ 3). Essai :
   un lot fait à la main (`MI_STORE_DATA_IMM` en PPGTT), puis un `PIPE_CONTROL`, puis un `XY_FAST_COPY_BLT`.
3. Le faux libdrm branché sur le service (morceaux `BUF_*`, `IG_OP_EXEC`, syncobj par numéros) ; `gltest` sur le
   ZBook (triangle, texture, pixels vérifiés comme sur le Pi).
4. GlKit du PC (`glkit.so`, même table `glkit.abi`), fenêtres d'Elegant ; GpuKit du PC (même `gpukit.abi`, sur iris
   ou sur le copieur) ; Skia sur GL ; puis Avalonia, Jet.
5. Présentation par le copieur vers le framebuffer du GOP ; Elegant composé par le GPU ; fréquence (RPS) et RC6.
   **Fréquence (16/11, écrite, à essayer sur le ZBook)** : `src/svc/intelgpu/rps.c`. Sans pilote, le GT garde la
   fréquence laissée par le micrologiciel (souvent RPn) ; le service lit `RP_STATE_CAP` (`0x145998`, pas de
   50 MHz), demande une fréquence fixe par `RPNSWREQ` (`0xA008`, bits 31:23 en pas de 50/3 MHz) et lit la
   fréquence courante dans `RPSTAT1` de Gen12 (`0x1381B4`, bits 19:11). Pas de régulation par les interruptions du
   PM : une demande fixe, `freq=max` par défaut (`max`, `rp1`, `min` ou des MHz, à la ligne de `drivers.txt`) ;
   le PCU garde la main sur les limites thermiques. La requête d'avant est remise à l'arrêt. Le journal donne
   RPn, RP1, RP0, la fréquence avant et après ; `intelgpu status` la fréquence courante. RC6 : pas fait (le
   service tient le forcewake, le GT ne s'endort pas).
   **Mesure de l'échange d'images (16/11)** : avec `/etc/armgl-stats` (fichier vide) ou `ARMGL_STATS`, chaque
   programme GL écrit au journal toutes les 5 s ses images par seconde et, par image, le temps de l'appli, de
   l'attente du GPU (`glFinish`) et de l'affichage (copie d'Elegant). Ces trois temps s'additionnent :
   `armgl_swap` attend le GPU à chaque image, le processeur ne prépare pas la suivante pendant ce temps. Si
   l'attente du GPU et l'affichage pèsent lourd, la suite est un échange sans `glFinish` (une barrière par image,
   attendue avant de réutiliser le tampon) et la copie par le copieur du GPU.
   **Cache de shaders (16/11)** : Mesa iris construit avec `-Dshader-cache=enabled` et une zlib compilée en
   `-fPIC` par `configure-iris.sh` (archive de `tools/git/build.sh`), en mode base de données, le défaut sous
   `__armpi__` (retouche de `util/disk_cache.c` : le mode à fichiers multiples projette son index en écriture
   partagée, que libposix n'a pas) dans `$HOME/.cache/mesa_shader_cache_db`. Le cache demande au programme 13
   fonctions qu'une `libglkit.a` plus ancienne ne donne pas (`flock`, `ftruncate`, `fileno`, `getpwuid_r`…) :
   le chargeur du kit (`tools/mesa/glkit/glkit.c`) les remplace alors par des fonctions « pas disponible » (et
   `strtoul` par `strtoull`, `rewind` par `fseek` + `clearerr`) ; une app déjà construite charge le nouveau
   kit, sans cache, une app reliée à nouveau a le cache. Identité du pilote : le sha1 des
   archives de Mesa, écrit dans `glkit.so` par `build-glkit-pc.sh` (`korin_glkit_id`, lu par la retouche
   d'`iris_disk_cache.c` dans `korin-pc.patch`) ; `flock` de libposix ne verrouille rien : deux programmes GL qui
   écrivent en même temps peuvent abîmer la base, que Mesa vérifie (sommes de contrôle) et refait.

## 14. Risques

| Risque | Ce qui protège |
|---|---|
| Écrire dans l'affichage → écran noir | aucune écriture dans les registres `0x40000`–`0x7FFFF`… de l'affichage ; lecture de `PLANE_*` seulement |
| Écraser le framebuffer ou une table du GOP | fenêtre de la GGTT choisie hors de toute entrée utilisée, après l'ouverture, hors de `PLANE_SURF` ; anciennes valeurs remises ; aucune écriture dans la mémoire volée |
| Le GPU écrit dans des pages rendues | arrêt et repos vérifiés avant de rendre ; sinon remise à zéro ; sinon le process garde ses pages et le dit |
| Maître du bus coupé à la fin d'un pilote | pcid ne le coupe plus pour l'affichage ; la sonde remet le registre de commande |
| GPU bloqué | délai de 500 ms, remise à zéro du seul domaine du rendu |
| Image de contexte incomplète (la sonde n'écrit que la partie « anneau ») | restauration interdite ; si le matériel la refuse : rien n'est écrit, délai, remise à zéro, journal ; c'est ce que l'étape 1 doit nous apprendre |

## 15. Sources

* Intel Graphics PRM, Tiger Lake (volumes publics, 01.org / intel.com) : « Command Reference: Registers »,
  « Command Reference: Instructions » (`MI_*`), « Memory Views » (GGTT, PPGTT, PAT, MOCS), « Render Engine Command
  Streamer » (execlists, LRC, CSB), « Configurations », « Workarounds ».
* Le pilote i915 de Linux (GPL) : **lu comme référence de séquences** (forcewake, execlists, image du contexte,
  remise à zéro, GMCH/BDSM) ; aucun code repris : tout est réécrit d'après le PRM.
* Mesa 26.2 (MIT) : iris, `src/intel/common`, `src/intel/dev`, `include/drm-uapi/i915_drm.h` (l'interface imitée).
